// =============================================================================
//  Pebblebol host tests - test_trade.cpp
//  THE ATOMIC TRADE (spec section 16, plan P7-C4), over the REAL
//  persistence/save_manager.cpp, the REAL game/box.cpp, the REAL section 15
//  codec and - for the wire half - two REAL networking/session.cpp endpoints
//  over the REAL fault-injecting loopback. Nothing about the journal is faked.
//
// -----------------------------------------------------------------------------
//  WHY THE KILL POINTS ARE A SWEEP AND NOT A LIST
// -----------------------------------------------------------------------------
//  The plan asks for "fault injection at each of the 5 phases". Three or five
//  hand-chosen cut points are three or five tests that happen to pass: they
//  cover the moments whoever wrote them was already thinking about, which are
//  exactly the moments the code already handles. THE PROPERTY IS ABOUT EVERY
//  MOMENT, so the cut point is swept - k = 0, 1, 2, ... over every flash write
//  the sequence performs, plus one arm past the end - and each k is a whole
//  power cut, reboot and resolve with the pair invariant asserted afterwards.
//
//  tests/fakes/kv_mem.h grew kv_mem_fail_after_n_puts() for this and says so:
//  the pre-existing knob was one-shot, so a test built on it could only ever
//  cut where it already knew the index.
//
// -----------------------------------------------------------------------------
//  ONE DEVICE IS REAL AND THE PEER IS A MODEL, AND THAT IS NOT A SHORTCUT
// -----------------------------------------------------------------------------
//  game/box.cpp and persistence/save_manager.cpp are file-scope singletons - one
//  Box, one bound GameState, one quarantine mask per process - so two real
//  devices cannot exist here at once. That is a fact about the modules and not
//  about this test, and it is the same reason tests/test_link_screen.cpp runs
//  ONE screen against one real peer.
//
//  It costs nothing for the claim that matters: WITHIN ONE DEVICE, never
//  duplicated and never lost at any power-loss point, is a statement about ONE
//  Box and one flash, and that Box and that flash are the shipping ones. The
//  peer's half is a Session with real trade_link.cpp and real protocol.cpp over
//  the real transport, with hooks that record whether it applied - which is all
//  the CROSS-PAIR question needs, and the cross-pair answer is REPORTED AS A
//  MEASUREMENT rather than asserted, because game/trade.h says why it cannot be
//  a guarantee.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "core/crc16.h"
#include "core/rng.h"
#include "data/species_table.h"
#include "fakes/kv_mem.h"
#include "game/box.h"
#include "game/evolution.h"
#include "game/genome.h"
#include "game/taint.h"
#include "game/trade.h"
#include "game/validate.h"
#include "game/xp.h"
#include "networking/protocol.h"
#include "networking/session.h"
#include "networking/trade_link.h"
#include "networking/transport.h"
#include "persistence/save_manager.h"
#include "persistence/save_schema.h"

#define TRD_EPOCH0   1700000000u
#define DEV_A        0x0000A001u
#define DEV_B        0x0000B002u

// =============================================================================
//  THE ONE REAL DEVICE
// =============================================================================
static GameState g_gs;

static uint32_t clock_epoch(void) { return TRD_EPOCH0; }

// THE MILLISECOND CLOCK IS REAL HERE, AND THAT IS THE WHOLE POINT.
// app/app.cpp:804 binds save_set_clock(&clock_ms, &clock_epoch); a fixture that
// passes nullptr switches OFF save_manager.cpp's entire wear-filter branch
// (`if (s_now_ms && s_have_written[slot])`) and therefore tests a save_manager
// the release artefact never executes. g_ms is FROZEN by default because that
// is what a real device's millis() does across the microseconds B1 and B2 are
// apart, which is exactly the window SAVE_MIN_GAP_MS defers in.
static uint32_t g_ms = 0;
static uint32_t clock_ms(void) { return g_ms; }

// A REBOOT IS MODELLED BY AGEING THE CLOCK, and here is why rather than a
// save_reset_write_state() written for a test. save_manager.cpp's per-slot
// throttle (s_have_written[], s_last_write_ms[]) is file-scope state that is
// ZERO at every real boot, and save_bind() deliberately does not clear it
// because on the device there is nothing to clear. This host process keeps it
// across a fake power cycle, so the fixture walks the clock past
// SAVE_MIN_GAP_MS instead - which is what a real reboot buys - rather than
// growing a shipping entry point nothing on the device would call.
static void age_past_the_write_floor(void) { g_ms += SAVE_MIN_GAP_MS + 1u; }

static Genome sealed_genome(uint32_t seed)
{
  genome_seed(seed);
  return genome_genesis();
}

// A legal Pebble that is NOT in any Box: the peer's offer is built from one.
static void mk_free_pebble(PebbleInstance& p, uint8_t species, uint8_t level, uint32_t id)
{
  memset(&p, 0, sizeof p);
  p.magic         = (uint16_t)PEBBLE_MAGIC;
  p.layout_ver    = (uint8_t)PEBBLE_LAYOUT_VER;
  p.species_id    = species;
  p.id            = id;
  p.level         = level;
  p.origin        = (uint8_t)ORIGIN_WILD;
  p.custom_sprite = (uint8_t)PB_CUSTOM_SPRITE_NONE;
  p.genome        = sealed_genome(0x0BADF00Du + id);
  const SpeciesDef* sp = species_get(species);
  if (sp == nullptr) return;
  memcpy(p.moves, sp->moves, sizeof p.moves);
  p.hp_cur    = xp_hp_max(sp->base_hp, level);
  p.evo_state = (uint8_t)(sp->stage & (uint8_t)EVO_STATE_STAGE_MASK);
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) p.care[i] = (int32_t)PB_CARE_MILLI_MAX;
}

// Three Pebbles on a fresh device, slot 0 active, everything committed. Slot 1
// is the one every case offers: it is not the active one, so the offer rule
// accepts it.
static void device_fresh(void)
{
  kv_mem_reset();
  age_past_the_write_floor();
  save_set_clock(&clock_ms, &clock_epoch);

  memset(&g_gs, 0, sizeof g_gs);
  for (uint8_t s = 0; s < (uint8_t)BOX_SLOTS; ++s) pebble_clear(g_gs.pebbles[s]);
  box_defaults(g_gs.box);
  cfgv2_defaults(g_gs.cfg);
  inventory_defaults(g_gs.inv);
  cooldowns_defaults(g_gs.cds);
  trade_clear(g_gs.trade);
  g_gs.cfg.device_id      = DEV_A;
  g_gs.box.next_id_counter = 1u;
  save_bind(g_gs);
  box_bind(g_gs);

  for (uint8_t i = 0; i < 3u; ++i) {
    const uint8_t slot = box_new_pebble((uint8_t)(1u + i * 4u), (uint8_t)(8u + i),
                                        (uint8_t)ORIGIN_WILD,
                                        sealed_genome(0x5A5A0000u + i), 0x1000u + i,
                                        TRD_EPOCH0);
    CHECK(slot != (uint8_t)BOX_SLOT_NONE);
  }
  CHECK(box_set_active(0u));

  for (uint8_t s = 0; s < (uint8_t)BOX_SLOTS; ++s) {
    if (box_occupied(s)) CHECK(save_pebble(s, g_gs.pebbles[s], true));
  }
  CHECK(save_box_header(g_gs.box));
  CHECK(save_config(g_gs.cfg));
  CHECK(save_inventory(g_gs.inv));
  CHECK(save_cooldowns(g_gs.cds));
  CHECK(save_trade_journal(g_gs.trade));
}

// A POWER CYCLE. Nothing in RAM survives it; the whole load pipeline runs.
static LoadResult device_boot(void)
{
  age_past_the_write_floor();
  memset(&g_gs, 0, sizeof g_gs);
  const LoadResult r = save_load_all(g_gs);
  box_bind(g_gs);
  return r;
}

// =============================================================================
//  THE STORE AND CODEC SEAMS, wired to the SHIPPING modules
// =============================================================================
static uint32_t g_store_writes = 0;

static bool st_write_journal(void* ctx, const PendingTrade& t)
{
  (void)ctx; g_store_writes++;
  g_gs.trade = t;
  return save_trade_journal(t);
}
// THE SHAPE OF THE SHIPPING SHIMS, and it must stay that shape: ui/ui.cpp's
// ui_tr_store_slot() and app/app.cpp's app_trade_write_slot() both call
// save_pebble_now(). A fixture that called save_pebble(..., true) here would be
// testing a store the release artefact does not have.
static bool st_write_slot(void* ctx, uint8_t slot)
{
  (void)ctx; g_store_writes++;
  return save_pebble_now(slot, g_gs.pebbles[slot]);
}
static bool st_write_box(void* ctx)
{
  (void)ctx; g_store_writes++;
  return save_box_header(g_gs.box);
}
static void st_checkpoint(void* ctx) { (void)ctx; (void)save_checkpoint_all(); }

static TradeStore real_store(void)
{
  TradeStore s;
  s.write_journal = &st_write_journal;
  s.write_slot    = &st_write_slot;
  s.write_box     = &st_write_box;
  s.checkpoint    = &st_checkpoint;
  s.ctx           = nullptr;
  return s;
}

// =============================================================================
//  THE INVARIANT. Everything a Box has to be, checked after every reboot.
// =============================================================================
struct BoxFacts {
  uint8_t  count;
  bool     has_out;          // the Pebble we were giving away
  uint8_t  traded;           // how many carry PBF_TRADED / ORIGIN_TRADED
  uint32_t traded_id;
  bool     bystanders_ok;    // the two Pebbles that were never part of the trade
};

static BoxFacts box_facts(uint32_t out_id, uint32_t bystander_a, uint32_t bystander_b)
{
  BoxFacts f; memset(&f, 0, sizeof f);
  bool saw_a = false, saw_b = false;
  for (uint8_t s = 0; s < (uint8_t)BOX_SLOTS; ++s) {
    const PebbleInstance* p = box_peek(s);
    if (p == nullptr) continue;
    f.count++;
    if (p->id == out_id)       f.has_out = true;
    if (p->id == bystander_a)  saw_a = true;
    if (p->id == bystander_b)  saw_b = true;
    if ((p->flags & (uint8_t)PBF_TRADED) != 0u) { f.traded++; f.traded_id = p->id; }
  }
  f.bystanders_ok = saw_a && saw_b;
  return f;
}

// The one thing the load path can tell us that the Box cannot: no slot was
// quarantined, i.e. every stored blob passed the shared validator on the way in.
static uint8_t box_quarantine_mask(void) { return (uint8_t)save_quarantine_mask(); }

// B1..B5 of game/box.h, plus the one validator on every occupied slot. A trade
// that leaves the right NUMBER of Pebbles but a broken Box has still failed.
static void check_box_is_sane(const char* where)
{
  uint32_t ids[BOX_SLOTS];
  uint8_t  n = 0;
  uint16_t mask = 0;
  for (uint8_t s = 0; s < (uint8_t)BOX_SLOTS; ++s) {
    const PebbleInstance* p = box_peek(s);
    if (p == nullptr) continue;
    mask |= (uint16_t)(1u << s);
    if (p->id == 0u) fprintf(stderr, "    %s: slot %u has id 0\n", where, (unsigned)s);
    CHECK(p->id != 0u);                                     // B2
    for (uint8_t k = 0; k < n; ++k) {
      if (ids[k] == p->id) fprintf(stderr, "    %s: duplicate id\n", where);
      CHECK(ids[k] != p->id);                               // B2
    }
    ids[n++] = p->id;
    const VReject v = validate_pebble(*p);
    if (v != VR_OK)
      fprintf(stderr, "    %s: slot %u is %s\n", where, (unsigned)s,
              validate_reject_name(v));
    CHECK_EQ((int)v, (int)VR_OK);
  }
  CHECK_EQ((int)mask, (int)g_gs.box.slot_mask);             // the cache agrees
  if (n > 0) {
    CHECK(box_active() < (uint8_t)BOX_SLOTS);               // B3
    CHECK(box_occupied(box_active()));
  }
  CHECK_EQ((int)box_quarantine_mask(), 0);
}

// =============================================================================
//  PART 1 - THE RULES. Which Pebble may be offered, and which may be taken.
// =============================================================================
TEST(the_pebble_you_are_holding_is_not_for_sale_and_every_refusal_is_named) {
  device_fresh();
  CHECK_EQ((int)box_active(), 0);

  // The active slot: game/trade.h says why this is a rule and not a shortcut.
  CHECK_EQ((int)trade_offer_check(0u, 0u), (int)TDR_ACTIVE);
  // A stored one is fine.
  CHECK_EQ((int)trade_offer_check(1u, 0u), (int)TDR_OK);
  CHECK_EQ((int)trade_offer_check(2u, 0u), (int)TDR_OK);
  // An empty slot and one past the end.
  CHECK_EQ((int)trade_offer_check(5u, 0u), (int)TDR_NO_SLOT);
  CHECK_EQ((int)trade_offer_check(99u, 0u), (int)TDR_NO_SLOT);
  // save_manager.h: "A quarantined Pebble may be shown to its owner. It may NOT
  // enter a battle or a trade." This is where P7 discharges that sentence.
  CHECK_EQ((int)trade_offer_check(1u, (uint16_t)(1u << 1)), (int)TDR_QUARANTINED);
  CHECK_EQ((int)trade_offer_check(2u, (uint16_t)(1u << 1)), (int)TDR_OK);
  // A Pebble the one validator refuses is not offerable either, whatever the
  // quarantine mask says - the mask is a snapshot from the last load and the
  // Pebble may have been edited since.
  PebbleInstance* p = box_slot(2u);
  CHECK(p != nullptr);
  const uint8_t keep = p->moves[0];
  p->moves[0] = 11u;
  CHECK_EQ((int)trade_offer_check(2u, 0u), (int)TDR_INVALID);
  p->moves[0] = keep;
  CHECK_EQ((int)trade_offer_check(2u, 0u), (int)TDR_OK);
}

// THIS CASE EXISTS BECAUSE A SEPARATE RULE FOR IT WAS WRITTEN AND MEASURED TO BE
// UNREACHABLE. "The last Pebble on the device cannot be given away" is real, and
// TDR_ACTIVE plus game/box.h invariant B3 already hold it: box_active() runs
// mask_sync(), which repairs an active_slot pointing at an empty slot to the
// lowest occupied one, so a Box with one Pebble ALWAYS reports that Pebble as
// active. The dedicated code came out; the property is asserted here instead.
TEST(the_last_pebble_on_the_device_is_the_active_one_and_that_is_what_refuses_it) {
  device_fresh();
  CHECK(box_release(2u, true));
  CHECK(box_release(1u, true));
  CHECK_EQ((int)box_count(), 1);
  CHECK_EQ((int)box_active(), 0);
  CHECK_EQ((int)trade_offer_check(0u, 0u), (int)TDR_ACTIVE);

  // Even with the header hand-broken to say "nothing is active", which invariant
  // B3 forbids: mask_sync() repairs it before the rule is applied, so there is
  // no window in which the only Pebble is offerable.
  device_fresh();
  CHECK(box_release(2u, true));
  CHECK(box_set_active(1u));
  CHECK(box_release(0u, true));
  CHECK_EQ((int)box_count(), 1);
  g_gs.box.active_slot = (uint8_t)BOX_ACTIVE_NONE;
  CHECK_EQ((int)trade_offer_check(1u, 0u), (int)TDR_ACTIVE);
  CHECK_EQ((int)box_active(), 1);                  // repaired, not left at NONE
}

TEST(a_tainted_pebble_cannot_enter_a_clean_dynasty_through_a_trade) {
  device_fresh();
  PebbleInstance incoming;
  mk_free_pebble(incoming, 7u, 11u, 0x77770001u);
  const PebbleInstance* mine = box_peek(1u);
  CHECK(mine != nullptr);

  CHECK_EQ((int)trade_accept_check(*mine, incoming, DEV_A, DEV_B), (int)TDR_OK);

  // MARKER 1: the genome bit, which genome_breed() would then propagate into
  // every descendant as A | B.
  gene_set_tainted(incoming.genome, 1u);
  CHECK(pb_is_tainted(incoming));
  CHECK_EQ((int)trade_accept_check(*mine, incoming, DEV_A, DEV_B), (int)TDR_TAINT);

  // A TAINTED UNIT ACCEPTS ANYTHING (game/taint.h): two testers keep a
  // playground with each other.
  PebbleInstance mine_dirty = *mine;
  gene_set_tainted(mine_dirty.genome, 1u);
  CHECK_EQ((int)trade_accept_check(mine_dirty, incoming, DEV_A, DEV_B), (int)TDR_OK);

  // MARKER 2: the instance FLAG on its own, which persistence/migration.cpp
  // sets from a v1 save without touching the genome. Reading only the genome is
  // the hole this half names.
  mk_free_pebble(incoming, 7u, 11u, 0x77770002u);
  incoming.flags = (uint8_t)(incoming.flags | (uint8_t)PBF_GOD_TAINTED);
  CHECK_EQ((int)gene_tainted(incoming.genome), 0);
  CHECK_EQ((int)trade_accept_check(*mine, incoming, DEV_A, DEV_B), (int)TDR_TAINT);
}

TEST(a_peer_that_is_us_or_sends_an_id_we_already_hold_is_refused_by_name) {
  device_fresh();
  PebbleInstance incoming;
  mk_free_pebble(incoming, 7u, 11u, 0x77770003u);
  const PebbleInstance* mine = box_peek(1u);
  CHECK(mine != nullptr);

  CHECK_EQ((int)trade_accept_check(*mine, incoming, DEV_A, DEV_A), (int)TDR_SELF);
  CHECK_EQ((int)trade_accept_check(*mine, incoming, DEV_A, 0u), (int)TDR_SELF);

  // A peer sending a Pebble whose id we already hold. box_add() would re-mint
  // it silently; spec section 15's first sentence says refuse rather than mend.
  PebbleInstance clash = incoming;
  clash.id = box_peek(2u)->id;
  pebble_seal(clash);
  CHECK_EQ((int)trade_accept_check(*mine, clash, DEV_A, DEV_B), (int)TDR_DUPLICATE_ID);

  // And an object the one validator refuses.
  PebbleInstance bad = incoming;
  bad.level = 0u;
  CHECK_EQ((int)trade_accept_check(*mine, bad, DEV_A, DEV_B), (int)TDR_PEER_INVALID);
}

TEST(every_trade_reject_and_resolution_has_a_name) {
  for (int r = 0; r < (int)TDR_REJECT_COUNT; ++r) {
    const char* n = trade_reject_name((TradeReject)r);
    CHECK(n != nullptr && n[0] == 'T' && n[1] == 'D' && n[2] == 'R');
    CHECK(strcmp(n, "TDR_?") != 0);
  }
  CHECK_EQ(strcmp(trade_reject_name((TradeReject)TDR_REJECT_COUNT), "TDR_?"), 0);
  for (int r = 0; r < (int)TRS_COUNT; ++r) {
    CHECK(strcmp(trade_resolution_name((TradeResolution)r), "TRS_?") != 0);
  }
  CHECK_EQ(strcmp(trade_resolution_name((TradeResolution)TRS_COUNT), "TRS_?"), 0);
  for (int p = 0; p < (int)TLP_PHASE_COUNT; ++p) {
    CHECK(strcmp(trade_phase_name((TradeLinkPhase)p), "TLP_?") != 0);
  }
  CHECK_EQ(strcmp(trade_phase_name((TradeLinkPhase)TLP_PHASE_COUNT), "TLP_?"), 0);
}

// =============================================================================
//  PART 2 - THE KILL SWEEP. The whole chunk is here.
// =============================================================================
struct TradeFixture {
  uint32_t out_id;                 // the Pebble this device is giving
  uint32_t bystander_a, bystander_b;
  uint8_t  wire[TR_WIRE_BYTES];    // the peer's record, as it arrived
};

// A device three Pebbles deep with the journal already at TRADE_RECEIVED:
// our offer is out (W1) and theirs is journalled (W2). Both writes are asserted
// to have LANDED, so a sweep that cuts inside trade_execute() is cutting where
// it means to.
static TradeFixture arm_trade(uint32_t peer_pebble_id)
{
  device_fresh();
  device_boot();

  TradeFixture f;
  memset(&f, 0, sizeof f);
  f.bystander_a = box_peek(0u)->id;
  f.out_id      = box_peek(1u)->id;
  f.bystander_b = box_peek(2u)->id;

  PebbleInstance peer;
  mk_free_pebble(peer, 7u, 11u, peer_pebble_id);
  pbw_encode(peer, f.wire);

  PendingTrade t;
  trade_journal_sent(t, f.out_id, DEV_B);
  CHECK(st_write_journal(nullptr, t));                       // W1
  trade_journal_received(t, f.wire);
  CHECK(st_write_journal(nullptr, t));                       // W2
  return f;
}

// Reboot, resolve, and assert the invariant. Returns what the resolver decided.
static TradeResolution reboot_and_resolve(const TradeFixture& f, const char* where)
{
  device_boot();
  PendingTrade j = g_gs.trade;
  const TradeResolution res = trade_resolve(j, trade_wire_codec(), real_store(),
                                            TRD_EPOCH0);
  g_gs.trade = j;

  check_box_is_sane(where);
  const BoxFacts b = box_facts(f.out_id, f.bystander_a, f.bystander_b);
  if (b.count != 3u || !b.bystanders_ok)
    fprintf(stderr, "    %s: count %u bystanders %d\n", where,
            (unsigned)b.count, (int)b.bystanders_ok);
  CHECK_EQ((int)b.count, 3);                       // never lost, never duplicated
  CHECK(b.bystanders_ok);                          // and nothing else moved
  // EXACTLY ONE of the two: either we still hold what we were giving, or we
  // hold what we were given. Both, or neither, is the failure this whole file
  // exists for.
  if (!((b.has_out && b.traded == 0u) || (!b.has_out && b.traded == 1u)))
    fprintf(stderr, "    %s: has_out %d traded %u\n", where,
            (int)b.has_out, (unsigned)b.traded);
  CHECK((b.has_out && b.traded == 0u) || (!b.has_out && b.traded == 1u));
  return res;
}

TEST(a_clean_trade_moves_exactly_one_pebble_each_way_and_clears_its_journal) {
  const TradeFixture f = arm_trade(0x77770200u);
  g_store_writes = 0;
  const uint32_t puts_before = kv_mem_puts();

  PendingTrade t = g_gs.trade;
  CHECK_EQ((int)t.phase, (int)TRADE_RECEIVED);
  CHECK_EQ((int)trade_execute(t, trade_wire_codec(), real_store(), TRD_EPOCH0),
           (int)TDR_OK);
  g_gs.trade = t;
  CHECK_EQ((int)t.phase, (int)TRADE_IDLE);

  const uint32_t puts = kv_mem_puts() - puts_before;
  printf("     one committed trade costs %u flash writes (%u through the store seam)\n",
         (unsigned)puts, (unsigned)g_store_writes);

  const TradeResolution res = reboot_and_resolve(f, "clean");
  CHECK_EQ((int)res, (int)TRS_NONE);               // there was nothing left to do
  const BoxFacts b = box_facts(f.out_id, f.bystander_a, f.bystander_b);
  CHECK(!b.has_out);
  CHECK_EQ((int)b.traded, 1);

  // The incoming Pebble is OURS now: a fresh local id, the flag, the origin and
  // the counter, and it passes the one validator on the way back off flash.
  const PebbleInstance* got = nullptr;
  for (uint8_t s = 0; s < (uint8_t)BOX_SLOTS; ++s) {
    const PebbleInstance* p = box_peek(s);
    if (p != nullptr && (p->flags & (uint8_t)PBF_TRADED) != 0u) got = p;
  }
  CHECK(got != nullptr);
  if (got != nullptr) {
    CHECK_EQ((int)got->origin, (int)ORIGIN_TRADED);
    CHECK_EQ((int)got->species_id, 7);
    CHECK_EQ((int)got->trades, 1);
    CHECK(got->id != 0x77770200u);                 // NOT the sender's id
    CHECK(got->id != f.out_id);
    CHECK_EQ((int)validate_pebble(*got), (int)VR_OK);
  }
}

// THE SWEEP. Every flash write of the sequence is a power cut in its own right.
TEST(a_power_cut_at_every_single_flash_write_leaves_the_box_whole) {
  // The clean run above prints the cost (10 writes: five through the store seam
  // and five inside save_checkpoint_all()). 14 is past it, and the tail of this
  // case ASSERTS that the sweep ran past the end, so a sequence that grows
  // cannot silently escape the sweep.
  const uint32_t KMAX = 14u;
  int completed = 0, rolled_back = 0, nothing = 0, failed = 0;

  for (uint32_t k = 0; k <= KMAX; ++k) {
    const TradeFixture f = arm_trade(0x77770300u + k);
    PendingTrade t = g_gs.trade;

    kv_mem_fail_after_n_puts(k);                   // the device dies at write k
    (void)trade_execute(t, trade_wire_codec(), real_store(), TRD_EPOCH0);
    kv_mem_power_restore();                        // and is plugged back in

    char where[48];
    snprintf(where, sizeof where, "cut at write %u", (unsigned)k);
    const TradeResolution res = reboot_and_resolve(f, where);
    if      (res == TRS_COMPLETED)   completed++;
    else if (res == TRS_ROLLED_BACK) rolled_back++;
    else if (res == TRS_NONE)        nothing++;
    else                             failed++;

    // A SECOND POWER CYCLE CHANGES NOTHING. Roll-forward is two presence tests
    // and both are no-ops the second time; roll-back cleared the record.
    const TradeResolution again = reboot_and_resolve(f, "second boot");
    CHECK_EQ((int)again, (int)TRS_NONE);
  }
  printf("     %u cut points: %d completed, %d rolled back, %d already done, %d failed\n",
         (unsigned)(KMAX + 1u), completed, rolled_back, nothing, failed);
  CHECK_EQ(failed, 0);
  // Both outcomes must actually occur, or the sweep is measuring one branch.
  CHECK(rolled_back > 0);
  CHECK(completed > 0);
  // And the sweep must reach past the end of the sequence: the last cut points
  // have to be the ones where nothing was cut at all.
  CHECK(nothing > 0);
}

// The same sweep over the two EARLIER journal writes, which trade_execute()
// never reaches: a cut inside W1 or W2 must roll back, and rolling back is
// "both sides keep their originals" (spec section 16).
TEST(a_power_cut_while_the_journal_is_being_opened_rolls_the_trade_back) {
  int rolled = 0, nothing = 0;
  for (uint32_t k = 0; k <= 3u; ++k) {
    device_fresh();
    device_boot();
    TradeFixture f;
    memset(&f, 0, sizeof f);
    f.bystander_a = box_peek(0u)->id;
    f.out_id      = box_peek(1u)->id;
    f.bystander_b = box_peek(2u)->id;
    PebbleInstance peer;
    mk_free_pebble(peer, 7u, 11u, 0x77770400u + k);
    pbw_encode(peer, f.wire);

    PendingTrade t;
    kv_mem_fail_after_n_puts(k);
    trade_journal_sent(t, f.out_id, DEV_B);
    const bool w1 = st_write_journal(nullptr, t);
    if (w1) {
      trade_journal_received(t, f.wire);
      (void)st_write_journal(nullptr, t);
    }
    kv_mem_power_restore();

    char where[48];
    snprintf(where, sizeof where, "journal cut at write %u", (unsigned)k);
    const TradeResolution res = reboot_and_resolve(f, where);
    // NOTHING WAS APPLIED AND NOTHING MAY BE: no Box byte moved before W3.
    const BoxFacts b = box_facts(f.out_id, f.bystander_a, f.bystander_b);
    CHECK(b.has_out);
    CHECK_EQ((int)b.traded, 0);
    if (res == TRS_ROLLED_BACK) rolled++;
    else if (res == TRS_NONE)   nothing++;
    else                        CHECK(false);
    CHECK_EQ((int)reboot_and_resolve(f, "second boot"), (int)TRS_NONE);
  }
  printf("     4 journal-open cut points: %d rolled back, %d never opened\n",
         rolled, nothing);
  CHECK(rolled > 0);
  CHECK(nothing > 0);
}

// A cut INSIDE THE RESOLVER. The device dies while finishing a trade it found
// at boot; the next boot has to finish the same one, and the one after that has
// to find nothing left to do.
TEST(a_power_cut_inside_the_resolver_is_finished_by_the_next_boot) {
  int converged = 0;
  for (uint32_t k = 0; k <= 8u; ++k) {
    const TradeFixture f = arm_trade(0x77770500u + k);
    PendingTrade t = g_gs.trade;

    // Get a COMMIT record onto flash and then die immediately, so every run of
    // this loop starts the resolver from the same place.
    kv_mem_fail_after_n_puts(1u);
    (void)trade_execute(t, trade_wire_codec(), real_store(), TRD_EPOCH0);
    kv_mem_power_restore();
    device_boot();
    CHECK_EQ((int)g_gs.trade.phase, (int)TRADE_COMMIT);

    // Now the resolver itself is cut at write k.
    PendingTrade j = g_gs.trade;
    kv_mem_fail_after_n_puts(k);
    (void)trade_resolve(j, trade_wire_codec(), real_store(), TRD_EPOCH0);
    kv_mem_power_restore();

    char where[56];
    snprintf(where, sizeof where, "resolver cut at write %u", (unsigned)k);
    const TradeResolution res = reboot_and_resolve(f, where);
    CHECK(res == TRS_COMPLETED || res == TRS_NONE);
    const BoxFacts b = box_facts(f.out_id, f.bystander_a, f.bystander_b);
    CHECK(!b.has_out);
    CHECK_EQ((int)b.traded, 1);
    CHECK_EQ((int)reboot_and_resolve(f, "second boot"), (int)TRS_NONE);
    converged++;
  }
  printf("     %d resolver cut points all converged on the completed trade\n", converged);
  CHECK_EQ(converged, 9);
}

// A STORE THAT REFUSES A WRITE IS NOT A POWER CUT, AND THIS CASE EXISTS BECAUSE
// A MUTATION PROVED THE DIFFERENCE WAS INVISIBLE. Deleting every `if (!write)
// return TDR_STORE` from game/trade.cpp left the whole sweep above GREEN: once
// the fake store is dead the RAM changes never reach flash either way, so a
// reboot cannot tell "stopped at the failure" from "carried on regardless".
//
// The difference is only visible WITHOUT the reboot - a device whose NVS page
// went bad and which keeps running - so this case asserts the two things a
// reboot hides: the call REPORTS TDR_STORE rather than TDR_OK, and RAM stopped
// exactly where the write failed instead of running to the end.
TEST(a_write_that_did_not_land_stops_the_sequence_where_it_failed) {
  // k is the index of the failing write inside trade_execute(): 0 = W3,
  // 1 = B1 (the outgoing slot), 2 = B2 (the incoming slot), 3 = B3 (the header),
  // 4 = W4 (clearing the journal).
  struct Want { uint32_t k; uint8_t count; bool has_out; uint8_t traded; };
  static const Want WANTS[] = {
    { 0u, 3u, true,  0u },      // nothing moved at all
    { 1u, 2u, false, 0u },      // the outgoing slot is gone and no more
    { 2u, 3u, false, 1u },      // both halves applied, the header did not
    { 3u, 3u, false, 1u },
    { 4u, 3u, false, 1u },
  };
  for (size_t i = 0; i < sizeof WANTS / sizeof WANTS[0]; ++i) {
    const TradeFixture f = arm_trade(0x77771200u + WANTS[i].k);
    PendingTrade t = g_gs.trade;
    kv_mem_fail_after_n_puts(WANTS[i].k);
    const TradeReject r = trade_execute(t, trade_wire_codec(), real_store(),
                                        TRD_EPOCH0);
    if (r != TDR_STORE)
      fprintf(stderr, "    write %u: %s, wanted TDR_STORE\n",
              (unsigned)WANTS[i].k, trade_reject_name(r));
    CHECK_EQ((int)r, (int)TDR_STORE);
    // RAM, BEFORE ANY REBOOT. This is the half the sweep cannot see.
    const BoxFacts b = box_facts(f.out_id, f.bystander_a, f.bystander_b);
    if (b.count != WANTS[i].count || b.has_out != WANTS[i].has_out ||
        b.traded != WANTS[i].traded)
      fprintf(stderr, "    write %u: count %u has_out %d traded %u\n",
              (unsigned)WANTS[i].k, (unsigned)b.count, (int)b.has_out,
              (unsigned)b.traded);
    CHECK_EQ((int)b.count, (int)WANTS[i].count);
    CHECK_EQ((int)b.has_out, (int)WANTS[i].has_out);
    CHECK_EQ((int)b.traded, (int)WANTS[i].traded);
    kv_mem_power_restore();
    // And the next boot still converges on a whole Box, whichever way it went.
    (void)reboot_and_resolve(f, "after a refused write");
    CHECK_EQ((int)reboot_and_resolve(f, "second boot"), (int)TRS_NONE);
  }
}

TEST(the_journal_names_the_id_the_incoming_pebble_will_have_here) {
  // THE IDEMPOTENCE HINGE (game/trade.h). Without it a replayed COMMIT would
  // mint a DIFFERENT id and the resolver could not tell "already done" from
  // "not yet started" - so the record is checked to carry the local id, and
  // the record is checked to still decode after the patch.
  const TradeFixture f = arm_trade(0x77770600u);
  PendingTrade t = g_gs.trade;

  kv_mem_fail_after_n_puts(1u);        // die immediately after W3 lands
  (void)trade_execute(t, trade_wire_codec(), real_store(), TRD_EPOCH0);
  kv_mem_power_restore();
  device_boot();

  const PendingTrade j = g_gs.trade;
  CHECK_EQ((int)j.phase, (int)TRADE_COMMIT);
  CHECK_EQ((long long)j.out_id, (long long)f.out_id);
  CHECK_EQ((long long)j.peer_id, (long long)DEV_B);

  PebbleInstance in;
  CHECK_EQ((int)trade_wire_codec().decode(j.in_wire, in), (int)VR_OK);  // still decodes
  CHECK(in.id != 0x77770600u);                          // NOT the sender's id
  CHECK(!box_id_in_use(in.id));                         // not filed yet
  const uint32_t promised = in.id;

  PendingTrade r = j;
  CHECK_EQ((int)trade_resolve(r, trade_wire_codec(), real_store(), TRD_EPOCH0),
           (int)TRS_COMPLETED);
  g_gs.trade = r;
  CHECK(box_id_in_use(promised));                       // exactly the promised id
  (void)reboot_and_resolve(f, "after the promise was kept");
}

TEST(a_commit_record_whose_wire_bytes_rotted_undoes_the_trade_instead_of_guessing) {
  // A "tr" record that passes its own 64 B CRC but whose 48 B payload does not
  // decode cannot happen while this module writes it - and the resolver's input
  // is a PERSISTED blob, so the answer is a named one rather than silence.
  const TradeFixture f = arm_trade(0x77770700u);
  PendingTrade t = g_gs.trade;
  kv_mem_fail_after_n_puts(1u);
  (void)trade_execute(t, trade_wire_codec(), real_store(), TRD_EPOCH0);
  kv_mem_power_restore();
  device_boot();
  CHECK_EQ((int)g_gs.trade.phase, (int)TRADE_COMMIT);

  PendingTrade j = g_gs.trade;
  j.in_wire[PBW_OFF_LEVEL] = 0u;                 // VR_BAD_LEVEL, resealed below
  const uint16_t crc = crc16_ccitt(j.in_wire, (size_t)PBW_CRC_BYTES);
  j.in_wire[PBW_OFF_CRC + 0] = (uint8_t)(crc & 0xFFu);
  j.in_wire[PBW_OFF_CRC + 1] = (uint8_t)((crc >> 8) & 0xFFu);

  CHECK_EQ((int)trade_resolve(j, trade_wire_codec(), real_store(), TRD_EPOCH0),
           (int)TRS_ROLLED_BACK);
  CHECK_EQ((int)trade_last_reject(), (int)TDR_PEER_INVALID);
  g_gs.trade = j;
  check_box_is_sane("after a rotten commit record");
  const BoxFacts b = box_facts(f.out_id, f.bystander_a, f.bystander_b);
  CHECK(b.has_out);                              // nothing was destroyed
  CHECK_EQ((int)b.traded, 0);
  CHECK_EQ((int)b.count, 3);
}

// THE ONE OUTCOME NOBODY WANTS, AND IT HAS A NAME RATHER THAN SILENCE. The
// outgoing Pebble has already left and the incoming record will not decode:
// nothing in game/trade.cpp can bring it back, so the journal is cleared and
// the answer is TRS_LOST - NOT TRS_ROLLED_BACK, because nothing was rolled
// back. It is unreachable while this module writes the record and the 64 B CRC
// holds; it is reachable HERE because the resolver's input is a persisted blob,
// and that is exactly the reason the code exists.
TEST(a_pebble_that_left_before_its_record_rotted_is_reported_as_lost_not_undone) {
  const TradeFixture f = arm_trade(0x77770B10u);
  PendingTrade t = g_gs.trade;
  kv_mem_fail_after_n_puts(2u);        // W3 lands, B1 lands, B2's write dies
  (void)trade_execute(t, trade_wire_codec(), real_store(), TRD_EPOCH0);
  kv_mem_power_restore();
  device_boot();
  CHECK_EQ((int)g_gs.trade.phase, (int)TRADE_COMMIT);
  CHECK(!box_id_in_use(f.out_id));                 // ours is gone from flash
  CHECK_EQ((int)box_count(), 2);

  PendingTrade j = g_gs.trade;
  j.in_wire[PBW_OFF_LEVEL] = 0u;                   // and theirs will not decode
  const uint16_t crc = crc16_ccitt(j.in_wire, (size_t)PBW_CRC_BYTES);
  j.in_wire[PBW_OFF_CRC + 0] = (uint8_t)(crc & 0xFFu);
  j.in_wire[PBW_OFF_CRC + 1] = (uint8_t)((crc >> 8) & 0xFFu);

  CHECK_EQ((int)trade_resolve(j, trade_wire_codec(), real_store(), TRD_EPOCH0),
           (int)TRS_LOST);
  CHECK_EQ((int)trade_last_reject(), (int)TDR_LOST);
  g_gs.trade = j;
  check_box_is_sane("after a lost pebble");
  CHECK_EQ((int)box_count(), 2);                   // and it stays lost
  CHECK_EQ((int)g_gs.trade.phase, (int)TRADE_IDLE);
  // The next boot has nothing left to do, which is what makes the report the
  // only thing the player gets - and why it must not say "cancelado".
  device_boot();
  PendingTrade again = g_gs.trade;
  CHECK_EQ((int)trade_resolve(again, trade_wire_codec(), real_store(), TRD_EPOCH0),
           (int)TRS_NONE);
}

TEST(a_trade_journal_that_names_a_pebble_this_device_does_not_hold_is_refused) {
  const TradeFixture f = arm_trade(0x77770800u);
  PendingTrade t = g_gs.trade;
  t.out_id = 0xDEADBEEFu;                        // a Pebble we never had
  CHECK_EQ((int)trade_execute(t, trade_wire_codec(), real_store(), TRD_EPOCH0),
           (int)TDR_NO_SLOT);
  check_box_is_sane("after a bad out_id");
  CHECK_EQ((int)box_count(), 3);

  // And the phase gate: trade_execute() only ever runs from TRADE_RECEIVED.
  PendingTrade s = g_gs.trade;
  s.phase = (uint8_t)TRADE_SENT;
  CHECK_EQ((int)trade_execute(s, trade_wire_codec(), real_store(), TRD_EPOCH0),
           (int)TDR_PHASE);
  s.phase = (uint8_t)TRADE_COMMIT;
  CHECK_EQ((int)trade_execute(s, trade_wire_codec(), real_store(), TRD_EPOCH0),
           (int)TDR_PHASE);
  (void)f;
}

TEST(the_checkpoint_recovery_path_no_longer_leaves_a_stale_journal_behind) {
  // THE DEFECT P7-C4's SURVEY FOUND IN commit_all(): it rewrote five of the six
  // blobs and not the journal, so "Recuperar" left RAM saying IDLE and flash
  // holding a live trade record - which the next boot then resolved against a
  // Box restored from a checkpoint that predates it.
  const TradeFixture f = arm_trade(0x77770900u);
  CHECK(save_checkpoint_all());                  // a checkpoint of a CLEAN Box

  // Now put a live COMMIT record on flash and cut the power immediately.
  PendingTrade t = g_gs.trade;
  kv_mem_fail_after_n_puts(1u);
  (void)trade_execute(t, trade_wire_codec(), real_store(), TRD_EPOCH0);
  kv_mem_power_restore();
  device_boot();
  CHECK_EQ((int)g_gs.trade.phase, (int)TRADE_COMMIT);

  // The user chooses "Recuperar".
  CHECK(save_restore_checkpoint(g_gs));
  box_bind(g_gs);
  CHECK_EQ((int)g_gs.trade.phase, (int)TRADE_IDLE);       // RAM says idle

  // AND SO DOES FLASH. Reverting the one line in commit_all() makes the next
  // boot read TRADE_COMMIT here and apply a trade the recovered Box never made.
  device_boot();
  CHECK_EQ((int)g_gs.trade.phase, (int)TRADE_IDLE);
  PendingTrade j = g_gs.trade;
  CHECK_EQ((int)trade_resolve(j, trade_wire_codec(), real_store(), TRD_EPOCH0),
           (int)TRS_NONE);
  check_box_is_sane("after Recuperar");
  const BoxFacts b = box_facts(f.out_id, f.bystander_a, f.bystander_b);
  CHECK(b.has_out);
  CHECK_EQ((int)b.traded, 0);
  CHECK_EQ((int)b.count, 3);
}

// =============================================================================
//  PART 3 - THE WIRE. Two REAL sessions, the REAL codec, the REAL ladder and
//  the REAL fault-injecting loopback. Endpoint 0 is the device whose Box and
//  flash are real; endpoint 1 is a model peer that records what it did.
// =============================================================================
struct ModelPeer {
  uint8_t  journal_sent, journal_recv, applied, aborted;
  uint8_t  judge_code;          // what this peer answers about OUR record
  uint8_t  store_ok;            // 0 makes its journal writes fail
  uint32_t out_id;
};

static bool mp_sent(void* ctx, uint32_t out_id, uint32_t peer_id)
{
  ModelPeer& m = *(ModelPeer*)ctx;
  (void)peer_id;
  m.journal_sent++; m.out_id = out_id;
  return m.store_ok != 0u;
}
static uint8_t mp_judge(void* ctx, const uint8_t rec[PBW_BYTES])
{
  ModelPeer& m = *(ModelPeer*)ctx;
  (void)rec;
  return m.judge_code;
}
static bool mp_recv(void* ctx, const uint8_t rec[PBW_BYTES])
{
  ModelPeer& m = *(ModelPeer*)ctx;
  (void)rec; m.journal_recv++;
  return m.store_ok != 0u;
}
static bool mp_commit(void* ctx)
{
  ModelPeer& m = *(ModelPeer*)ctx;
  m.applied++;
  return m.store_ok != 0u;
}
static void mp_abort(void* ctx) { ((ModelPeer*)ctx)->aborted++; }

static TradeHooks model_hooks(ModelPeer& m)
{
  TradeHooks h;
  h.journal_sent = &mp_sent; h.judge = &mp_judge; h.journal_received = &mp_recv;
  h.commit = &mp_commit; h.abort = &mp_abort; h.ctx = &m;
  return h;
}

// --- the real device's hooks, over game/trade.cpp and save_manager.cpp -------
static uint32_t g_dev_peer_id  = DEV_B;
static uint8_t  g_dev_out_slot = 1u;
static uint8_t  g_dev_applied  = 0;

static bool dev_sent(void* ctx, uint32_t out_id, uint32_t peer_id)
{
  (void)ctx;
  PendingTrade t;
  trade_journal_sent(t, out_id, peer_id);
  return st_write_journal(nullptr, t);
}
static uint8_t dev_judge(void* ctx, const uint8_t rec[PBW_BYTES])
{
  (void)ctx;
  PebbleInstance in;
  if (pbw_decode(rec, in) != VR_OK) return (uint8_t)TDR_PEER_INVALID;
  const PebbleInstance* mine = box_peek(g_dev_out_slot);
  if (mine == nullptr) return (uint8_t)TDR_NO_SLOT;
  return (uint8_t)trade_accept_check(*mine, in, DEV_A, g_dev_peer_id);
}
static bool dev_recv(void* ctx, const uint8_t rec[PBW_BYTES])
{
  (void)ctx;
  PendingTrade t = g_gs.trade;
  trade_journal_received(t, rec);
  return st_write_journal(nullptr, t);
}
static bool dev_commit(void* ctx)
{
  (void)ctx;
  PendingTrade t = g_gs.trade;
  const TradeReject r = trade_execute(t, trade_wire_codec(), real_store(), TRD_EPOCH0);
  g_gs.trade = t;
  if (r == TDR_OK) g_dev_applied++;
  return r == TDR_OK;
}
static void dev_abort(void* ctx)
{
  (void)ctx;
  PendingTrade t;
  trade_journal_idle(t);
  (void)st_write_journal(nullptr, t);
}

static TradeHooks device_hooks(void)
{
  TradeHooks h;
  h.journal_sent = &dev_sent; h.judge = &dev_judge; h.journal_received = &dev_recv;
  h.commit = &dev_commit; h.abort = &dev_abort; h.ctx = nullptr;
  return h;
}

struct Wire {
  LoopbackLink lk;
  LoopbackPort port[2];
  Transport    tp[2];
  Session      s[2];
  TradeLink    tl[2];
  ModelPeer    peer;
  uint32_t     now, iters;
  bool         hung;
};

static Wire& wire_arena(void) { static Wire w; return w; }

// Endpoint 0's Pebble is slot g_dev_out_slot of the REAL Box; endpoint 1's is a
// free-standing legal one. Both sides run the identical shipping code.
static void wire_begin(Wire& W, uint32_t seed, const LoopbackFault& f,
                       uint32_t peer_pebble_id)
{
  memset(&W, 0, sizeof W);
  loopback_init(W.lk, seed, f);
  W.now = 100000u;
  W.peer.store_ok = 1u;

  device_fresh();
  device_boot();
  g_dev_applied = 0;

  for (uint8_t i = 0; i < 2u; ++i) W.tp[i] = transport_loopback(W.port[i], W.lk, i);

  SessionCfg cfg;
  for (uint8_t i = 0; i < 2u; ++i) {
    memset(&cfg, 0, sizeof cfg);
    cfg.tp = &W.tp[i];
    cfg.tl = &W.tl[i];
    cfg.op = (uint8_t)SOP_TRADE;
    cfg.device_id = (i == 0u) ? DEV_A : DEV_B;
    cfg.nonce = 0xA11CE000u + i * 0x1000u + seed;
    cfg.lvl_lo = 1u; cfg.lvl_hi = (uint8_t)XP_LEVEL_MAX;
    session_init(W.s[i], cfg);
  }

  const PebbleInstance* mine = box_peek(g_dev_out_slot);
  CHECK(mine != nullptr);
  CHECK_EQ((int)session_set_trade(W.s[0], *mine), (int)VR_OK);
  trade_link_init(W.tl[0], W.s[0], mine->id, device_hooks());

  PebbleInstance theirs;
  mk_free_pebble(theirs, 7u, 11u, peer_pebble_id);
  CHECK_EQ((int)session_set_trade(W.s[1], theirs), (int)VR_OK);
  trade_link_init(W.tl[1], W.s[1], theirs.id, model_hooks(W.peer));

  session_start(W.s[0], W.now);
  session_start(W.s[1], W.now);
}

// One poll round over both endpoints. The clock advances only when a round
// moved no frame at all, so one idle round is exactly one rung of the ladder
// and the whole nine-second deadline costs zero wall clock.
static bool wire_step(Wire& W)
{
  const uint32_t moved = W.lk.stats.sent + W.lk.stats.delivered;
  for (uint8_t i = 0; i < 2u; ++i) {
    session_poll(W.s[i], W.now);
    if (trade_link_wants_consent(W.s[i])) {
      // THE CONSENT, given by the test where a player would give it. Endpoint 0
      // is driven case by case; endpoint 1 always says yes.
      if (i == 1u) trade_link_accept(W.s[i]);
    }
  }
  const bool progressed = (W.lk.stats.sent + W.lk.stats.delivered) != moved;
  if (!progressed) W.now += PROTO_RETX_MS;
  return progressed;
}

static void wire_run(Wire& W, uint32_t max_iters = 200000u)
{
  while (!(session_closed(W.s[0]) && session_closed(W.s[1]))) {
    if (W.iters++ >= max_iters) { W.hung = true; return; }
    (void)wire_step(W);
  }
}

static bool wire_run_until_consent(Wire& W, uint32_t max_iters = 200000u)
{
  while (!trade_link_wants_consent(W.s[0])) {
    if (session_closed(W.s[0]) || session_closed(W.s[1])) return false;
    if (W.iters++ >= max_iters) { W.hung = true; return false; }
    (void)wire_step(W);
  }
  return true;
}

TEST(two_devices_that_both_consent_swap_exactly_one_pebble_each) {
  Wire& W = wire_arena();
  LoopbackFault clean; memset(&clean, 0, sizeof clean);
  wire_begin(W, 1u, clean, 0x77770A00u);
  const uint32_t out_id = W.tl[0].out_id;
  const uint32_t by_a = box_peek(0u)->id, by_b = box_peek(2u)->id;

  CHECK(wire_run_until_consent(W));
  CHECK(!W.hung);
  // NOTHING HAS MOVED YET. Both offers are on the table, both were validated on
  // both sides, and the Box is exactly as it was: the A press is the only thing
  // that can change it.
  CHECK_EQ((int)box_count(), 3);
  CHECK(box_id_in_use(out_id));
  CHECK_EQ((int)g_dev_applied, 0);
  CHECK_EQ((int)W.peer.applied, 0);
  CHECK_EQ((int)g_gs.trade.phase, (int)TRADE_RECEIVED);
  CHECK_EQ((int)W.tl[0].phase, (int)TLP_ASK_PLAYER);

  trade_link_accept(W.s[0]);
  wire_run(W);
  CHECK(!W.hung);

  CHECK_EQ((int)session_end(W.s[0]).reason, (int)SE_DONE);
  CHECK_EQ((int)session_end(W.s[1]).reason, (int)SE_DONE);
  CHECK(trade_link_completed(W.s[0]));
  CHECK(trade_link_completed(W.s[1]));
  CHECK_EQ((int)W.tl[0].phase, (int)TLP_DONE);
  CHECK_EQ((int)g_dev_applied, 1);
  CHECK_EQ((int)W.peer.applied, 1);

  // And on flash, after a reboot: our Pebble is gone, theirs is here, the
  // journal is clear and the two bystanders never moved.
  device_boot();
  check_box_is_sane("after a wire trade");
  const BoxFacts b = box_facts(out_id, by_a, by_b);
  CHECK_EQ((int)b.count, 3);
  CHECK(!b.has_out);
  CHECK_EQ((int)b.traded, 1);
  CHECK(b.bystanders_ok);
  CHECK_EQ((int)g_gs.trade.phase, (int)TRADE_IDLE);
}

TEST(a_trade_nobody_confirmed_dies_on_the_ladder_and_rolls_back_at_the_next_boot) {
  Wire& W = wire_arena();
  LoopbackFault clean; memset(&clean, 0, sizeof clean);
  wire_begin(W, 2u, clean, 0x77770B00u);
  const uint32_t out_id = W.tl[0].out_id;
  const uint32_t by_a = box_peek(0u)->id, by_b = box_peek(2u)->id;

  CHECK(wire_run_until_consent(W));
  const uint32_t t0 = W.now;
  // The local player never presses A. The peer confirmed and waits; both
  // ladders run out and NOTHING is applied on either side.
  wire_run(W);
  CHECK(!W.hung);
  CHECK_EQ((int)session_end(W.s[0]).reason, (int)SE_LOST);
  CHECK_EQ((int)g_dev_applied, 0);
  CHECK_EQ((int)W.peer.applied, 0);
  printf("     an unanswered trade ends after %u ms on the session's own ladder\n",
         (unsigned)(W.now - t0));
  CHECK((W.now - t0) <= (uint32_t)(PROTO_RETX_MS * (PROTO_RETX_MAX + 2u)));

  device_boot();
  CHECK_EQ((int)g_gs.trade.phase, (int)TRADE_RECEIVED);   // the journal survived
  PendingTrade j = g_gs.trade;
  CHECK_EQ((int)trade_resolve(j, trade_wire_codec(), real_store(), TRD_EPOCH0),
           (int)TRS_ROLLED_BACK);
  g_gs.trade = j;
  check_box_is_sane("after a trade nobody confirmed");
  const BoxFacts b = box_facts(out_id, by_a, by_b);
  CHECK(b.has_out);
  CHECK_EQ((int)b.traded, 0);
  CHECK_EQ((int)b.count, 3);
}

TEST(a_tainted_offer_is_refused_on_the_wire_by_name_and_moves_nothing) {
  Wire& W = wire_arena();
  LoopbackFault clean; memset(&clean, 0, sizeof clean);
  wire_begin(W, 3u, clean, 0x77770C00u);
  const uint32_t out_id = W.tl[0].out_id;

  // Re-freeze the peer's record with the god-taint bit set. The peer's own
  // judge still says yes, so the refusal is entirely ours - which is what
  // game/taint.h means by "both sides evaluate it on their own incoming record".
  PebbleInstance dirty;
  mk_free_pebble(dirty, 7u, 11u, 0x77770C01u);
  gene_set_tainted(dirty.genome, 1u);
  CHECK_EQ((int)session_set_trade(W.s[1], dirty), (int)VR_OK);
  memcpy(W.tl[1].out_rec, W.s[1].my_rec[0], (size_t)PBW_BYTES);

  wire_run(W);
  CHECK(!W.hung);
  CHECK_EQ((int)session_end(W.s[0]).reason, (int)SE_REJECTED);
  CHECK_EQ((int)session_end(W.s[0]).detail, (int)SD_TRADE_REFUSED);
  CHECK_EQ((int)W.tl[0].in_policy, (int)TDR_TAINT);
  CHECK_EQ((int)W.tl[0].phase, (int)TLP_REFUSED);
  CHECK_EQ((int)g_dev_applied, 0);
  CHECK_EQ((int)W.peer.applied, 0);
  // The peer hears the refusal by the same name rather than timing out.
  CHECK_EQ((int)session_end(W.s[1]).reason, (int)SE_REJECTED);
  CHECK_EQ((int)session_end(W.s[1]).detail, (int)SD_TRADE_REFUSED);

  device_boot();
  check_box_is_sane("after a refused offer");
  CHECK(box_id_in_use(out_id));
  CHECK_EQ((int)box_count(), 3);
  CHECK_EQ((int)g_gs.trade.phase, (int)TRADE_IDLE);
}

TEST(a_battle_session_and_a_trade_session_refuse_each_other_by_name) {
  Wire& W = wire_arena();
  LoopbackFault clean; memset(&clean, 0, sizeof clean);
  wire_begin(W, 4u, clean, 0x77770D00u);
  // Endpoint 1 came to fight. The operation is agreed in the handshake, at the
  // last moment before a Pebble is on the wire.
  W.s[1].op = (uint8_t)SOP_BATTLE;
  wire_run(W);
  CHECK(!W.hung);
  CHECK_EQ((int)session_end(W.s[0]).reason, (int)SE_REJECTED);
  CHECK_EQ((int)session_end(W.s[0]).detail, (int)SD_OP);
  CHECK_EQ((int)session_end(W.s[1]).detail, (int)SD_OP);
  CHECK_EQ((int)g_dev_applied, 0);
  CHECK_EQ((int)box_count(), 3);
  // Nothing was journalled: the refusal happens before SS_TRADE is entered.
  CHECK_EQ((int)g_gs.trade.phase, (int)TRADE_IDLE);
}

TEST(a_commit_frame_carries_its_senders_confirm_and_that_is_what_saves_the_pair) {
  // COMMIT SUBSUMES CONFIRM (networking/trade_link.h). Endpoint 0's CONFIRM is
  // killed BY NAME - every copy of it - so endpoint 1 can only ever learn that
  // endpoint 0 agreed from the COMMIT that follows.
  Wire& W = wire_arena();
  LoopbackFault kill; memset(&kill, 0, sizeof kill);
  kill.block_type[0] = (uint8_t)PT_TRADE_CONFIRM;
  kill.block_left[0] = 0xFFu;
  wire_begin(W, 5u, kill, 0x77770E00u);
  const uint32_t out_id = W.tl[0].out_id;

  CHECK(wire_run_until_consent(W));
  trade_link_accept(W.s[0]);
  wire_run(W);
  CHECK(!W.hung);
  CHECK(W.lk.stats.blocked > 0u);
  CHECK_EQ((int)g_dev_applied, 1);
  CHECK_EQ((int)W.peer.applied, 1);      // it applied on the COMMIT alone
  CHECK(trade_link_completed(W.s[0]));
  CHECK(trade_link_completed(W.s[1]));

  device_boot();
  check_box_is_sane("after a lost confirm");
  CHECK(!box_id_in_use(out_id));
  CHECK_EQ((int)box_count(), 3);
}

// THE FIRST OF THE TWO DEFECTS THE LOSSY ARM FOUND, PINNED ON ITS OWN. Killing
// exactly ONE copy of endpoint 1's OFFER makes its READY arrive at endpoint 0
// FIRST - the READY is re-sent on the same rung as the OFFER, so a single drop
// is enough and no reordering is needed. Before maybe_ask_player() was called
// from on_offer() as well, endpoint 0 sat in TLP_REVIEW with both halves in
// hand and waited out its whole ladder.
TEST(a_ready_that_arrives_before_its_own_offer_still_reaches_the_player) {
  Wire& W = wire_arena();
  LoopbackFault kill; memset(&kill, 0, sizeof kill);
  kill.block_type[1] = (uint8_t)PT_TRADE_OFFER;
  kill.block_left[1] = 1u;                       // exactly one, then the link heals
  wire_begin(W, 11u, kill, 0x77770F00u);
  const uint32_t out_id = W.tl[0].out_id;

  CHECK(wire_run_until_consent(W));
  CHECK(!W.hung);
  CHECK_EQ((int)W.lk.stats.blocked, 1);
  CHECK_EQ((int)W.tl[0].peer_ready, 1);          // their verdict came first
  CHECK_EQ((int)W.tl[0].have_in, 1);             // their record came second
  trade_link_accept(W.s[0]);
  wire_run(W);
  CHECK(!W.hung);
  CHECK_EQ((int)session_end(W.s[0]).reason, (int)SE_DONE);
  CHECK_EQ((int)g_dev_applied, 1);
  CHECK_EQ((int)W.peer.applied, 1);
  device_boot();
  check_box_is_sane("after a ready that overtook its offer");
  CHECK(!box_id_in_use(out_id));
}

// THE SECOND. The ladder REGENERATES a frame from state rather than replaying
// bytes, so the operation byte has to be written in TWO places - send_request()
// and resend() - and a miss shows only on a link that loses the first copy.
// Killing exactly one SESSION_REQUEST makes the RETRANSMITTED one the first the
// responder ever sees, which is the frame the defect was in.
TEST(the_retransmitted_session_request_still_says_which_operation_it_is) {
  Wire& W = wire_arena();
  LoopbackFault kill; memset(&kill, 0, sizeof kill);
  // Endpoint 0 is the initiator: its device id is the lower of the two, which
  // is the rule networking/session.cpp fixes the roles by.
  kill.block_type[0] = (uint8_t)PT_SESSION_REQUEST;
  kill.block_left[0] = 1u;
  wire_begin(W, 12u, kill, 0x77771100u);

  CHECK(wire_run_until_consent(W));
  CHECK(!W.hung);
  // The roles are not decided until the two HELLOs are in, which is why this is
  // asserted here and not next to wire_begin().
  CHECK_EQ((int)W.s[0].role, (int)SR_INITIATOR);
  CHECK_EQ((int)W.lk.stats.blocked, 1);
  // Reverting the one line in resend() closes BOTH sessions here instead, the
  // responder with SD_OP and the initiator reading the refusal as an agreement.
  CHECK_EQ((int)session_state(W.s[0]), (int)SS_TRADE);
  CHECK_EQ((int)session_state(W.s[1]), (int)SS_TRADE);
  trade_link_accept(W.s[0]);
  wire_run(W);
  CHECK_EQ((int)session_end(W.s[0]).reason, (int)SE_DONE);
  CHECK_EQ((int)session_end(W.s[1]).reason, (int)SE_DONE);
}

TEST(a_lossy_link_either_completes_the_trade_on_both_sides_or_on_neither) {
  // THE CROSS-PAIR MEASUREMENT (game/trade.h). Two parties over a lossy link
  // cannot make an exchange atomic, so this case REPORTS the residual instead
  // of asserting it away - and asserts the two things that ARE guaranteed: the
  // device's own Box is whole after every trial, and no trial ends with this
  // device holding both Pebbles or neither.
  struct Arm { const char* name; uint16_t drop, dup, reorder; uint8_t window; };
  static const Arm ARMS[] = {
    { "clean",                     0u,   0u,   0u, 0u },
    { "10 % drop",               100u,   0u,   0u, 0u },
    { "10 % drop + dup + reorder", 100u, 100u, 100u, 4u },
    { "30 % drop, 20 % reorder", 300u,   0u, 200u, 4u },
  };
  const int TRIALS = 200;
  for (size_t arm = 0; arm < sizeof ARMS / sizeof ARMS[0]; ++arm) {
  int both = 0, neither = 0, split = 0, hung = 0;
  int diag_state[12] = {0}, diag_reason[10] = {0};
  for (int i = 0; i < TRIALS; ++i) {
    Wire& W = wire_arena();
    LoopbackFault f; memset(&f, 0, sizeof f);
    f.drop_permille    = ARMS[arm].drop;
    f.dup_permille     = ARMS[arm].dup;
    f.reorder_permille = ARMS[arm].reorder;
    f.reorder_window   = ARMS[arm].window;
    wire_begin(W, (uint32_t)(0x5EED0000u + i), f, (uint32_t)(0x77771000u + i));
    const uint32_t out_id = W.tl[0].out_id;
    const uint32_t by_a = box_peek(0u)->id, by_b = box_peek(2u)->id;

    if (wire_run_until_consent(W)) trade_link_accept(W.s[0]);
    wire_run(W);
    if (W.hung) { hung++; continue; }

    const int a = (g_dev_applied != 0) ? 1 : 0;
    const int b = (W.peer.applied != 0) ? 1 : 0;
    if (a && b)       both++;
    else if (!a && !b) neither++;
    else               split++;
    if (!a) {
      const SessionEnd& e0 = session_end(W.s[0]);
      diag_state[e0.state % 12]++;
      diag_reason[e0.reason % 10]++;
    }

    // WHATEVER HAPPENED ON THE WIRE, THIS DEVICE IS WHOLE. Reboot, resolve
    // whatever the journal holds, and assert the invariant.
    device_boot();
    PendingTrade j = g_gs.trade;
    (void)trade_resolve(j, trade_wire_codec(), real_store(), TRD_EPOCH0);
    g_gs.trade = j;
    check_box_is_sane("lossy trial");
    const BoxFacts bf = box_facts(out_id, by_a, by_b);
    CHECK_EQ((int)bf.count, 3);
    CHECK(bf.bystanders_ok);
    CHECK((bf.has_out && bf.traded == 0u) || (!bf.has_out && bf.traded == 1u));
    // And what the device did matches what it decided: applied means the
    // Pebble left, not applied means it stayed.
    CHECK_EQ((int)(a != 0), (int)(!bf.has_out));
  }
  printf("     %-26s %d trials: %3d completed on both, %3d on neither, "
         "%d SPLIT, %d hung\n",
         ARMS[arm].name, TRIALS, both, neither, split, hung);
  // WHERE the trials that did not complete died, by name. A row that moves is
  // the first thing to look at when this table changes, and it is what turned
  // two real defects into two named lines during P7-C4: a READY that arrived
  // before its OFFER left an endpoint parked in TLP_REVIEW, and the ladder's
  // regeneration of SESSION_REQUEST dropped the operation byte.
  for (int k = 0; k < 12; ++k)
    if (diag_state[k])
      printf("        died in %s x%d\n", session_state_name((SessionState)k),
             diag_state[k]);
  CHECK_EQ(hung, 0);
  CHECK(both > 0);           // else the fault arm is simply killing every trial
  }
}
