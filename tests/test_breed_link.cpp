// =============================================================================
//  ERRATA host tests - test_breed_link.cpp
//
//  TWO DEVICES, ONE PAIR, AND THE ONE THING THAT MUST NOT DIFFER.
//
//  networking/breed_link.cpp exists so spec section 17 has a wire. Its whole
//  risk is stated in its own banner and in game/breeding.h's: breed_compute()
//  is deterministic in (a, b, seed), `a` is the INITIATOR's parent ON BOTH
//  ENDS, and ordering by "mine, then theirs" gives each device a DIFFERENT,
//  EQUALLY VALID child. Nothing downstream would notice - two players would
//  compare two screens and find two creatures, both correct.
//
//  So these cases run the real session over the real loopback and assert the
//  two computed plans BYTE FOR BYTE. `a_swapped_pair_is_caught_before_anything_
//  is_filed` is the one that matters: it drives the desync deliberately and
//  checks the session dies naming it, rather than filing two children.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "core/nt_types.h"
#include "data/species_table.h"
#include "fakes/kv_mem.h"
#include "game/box.h"
#include "game/breeding.h"
#include "game/evolution.h"
#include "game/genome.h"
#include "game/validate.h"
#include "game/species.h"
#include "game/xp.h"
#include "networking/breed_link.h"
#include "networking/protocol.h"
#include "networking/session.h"
#include "networking/transport.h"
#include "persistence/save_schema.h"

#define DEV_A 0x0000A001u
#define DEV_B 0x0000B002u

static GameState g_gs;

static Genome sealed_genome(uint32_t seed)
{
  genome_seed(seed);
  return genome_genesis();
}

static void mk_free_bug(BugInstance& p, uint8_t species, uint8_t level, uint32_t id)
{
  memset(&p, 0, sizeof p);
  p.magic         = (uint16_t)BUG_MAGIC;
  p.layout_ver    = (uint8_t)BUG_LAYOUT_VER;
  p.species_id    = species;
  p.id            = id;
  p.level         = level;
  p.origin        = (uint8_t)ORIGIN_WILD;
  p.custom_sprite = (uint8_t)ER_CUSTOM_SPRITE_NONE;
  p.genome        = sealed_genome(0x0BADF00Du + id);
  const SpeciesDef* sp = species_get(species);
  if (sp == nullptr) return;
  memcpy(p.moves, sp->moves, sizeof p.moves);
  p.hp_cur    = xp_hp_max(sp->base_hp, level);
  p.evo_state = (uint8_t)(sp->stage & (uint8_t)EVO_STATE_STAGE_MASK);
  for (uint8_t i = 0; i < (uint8_t)ER_CARE_COUNT; ++i)
    p.care[i] = (int32_t)ER_CARE_MILLI_MAX;
}

// -----------------------------------------------------------------------------
//  THE PAIR IS FOUND, NOT TYPED. Two species ids hard-coded here would be two
//  numbers that were compatible on the day this was written; the roster is
//  generated from tools/content/*.json and a content pack may move either of
//  them. breed_check() is the oracle, so the first pair it accepts is by
//  definition a legal pair - and if the roster ever stops containing one, this
//  fails loudly instead of testing a refusal by accident.
// -----------------------------------------------------------------------------
struct FoundPair { uint8_t a, b; bool ok; };

static FoundPair find_breedable_pair(void)
{
  FoundPair r = { 0u, 0u, false };
  for (uint8_t i = 1u; i <= (uint8_t)SPECIES_TABLE_COUNT && !r.ok; ++i) {
    for (uint8_t j = 1u; j <= (uint8_t)SPECIES_TABLE_COUNT && !r.ok; ++j) {
      if (i == j) continue;
      BugInstance x, y;
      mk_free_bug(x, i, 12u, 0x501u);
      mk_free_bug(y, j, 12u, 0x502u);
      if (breed_check(x, y) == BRD_OK) { r.a = i; r.b = j; r.ok = true; }
    }
  }
  return r;
}

static FoundPair find_incompatible_pair(void)
{
  FoundPair r = { 0u, 0u, false };
  for (uint8_t i = 1u; i <= (uint8_t)SPECIES_TABLE_COUNT && !r.ok; ++i) {
    for (uint8_t j = 1u; j <= (uint8_t)SPECIES_TABLE_COUNT && !r.ok; ++j) {
      if (i == j) continue;
      BugInstance x, y;
      mk_free_bug(x, i, 12u, 0x601u);
      mk_free_bug(y, j, 12u, 0x602u);
      if (breed_check(x, y) == BRD_COMPAT_GROUP) { r.a = i; r.b = j; r.ok = true; }
    }
  }
  return r;
}

// -----------------------------------------------------------------------------
//  THE TWO ENDPOINTS
//
//  Endpoint 0 files into the REAL Box (box_bind() binds one, globally, so only
//  one endpoint in this process can have a real one). Endpoint 1 is a MODEL: it
//  records the plan it was asked to file and answers BRD_OK. That is enough for
//  every claim here, because the claim is about the PLAN the two ends computed
//  and breed_link_plan() exposes it on both sides.
// -----------------------------------------------------------------------------
static uint8_t   g_model_filed = 0u;
static BreedPlan g_model_plan;
static uint8_t   g_model_reject = (uint8_t)BRD_OK;

static uint8_t model_commit(void* ctx, const BreedPlan& plan, uint8_t& slot_out)
{
  (void)ctx;
  if (g_model_reject != (uint8_t)BRD_OK) return g_model_reject;
  g_model_plan  = plan;
  g_model_filed = 1u;
  slot_out      = 3u;
  return (uint8_t)BRD_OK;
}
static bool model_save(void* ctx) { (void)ctx; return true; }
static void model_abort(void* ctx) { (void)ctx; }

static uint8_t g_real_reject = (uint8_t)BRD_OK;
static uint8_t real_commit(void* ctx, const BreedPlan& plan, uint8_t& slot_out)
{
  (void)ctx;
  if (g_real_reject != (uint8_t)BRD_OK) return g_real_reject;
  return (uint8_t)breed_commit(plan, 1700000000u, slot_out);
}
static bool real_save(void* ctx) { (void)ctx; return true; }

static BreedHooks hooks_real(void)
{
  BreedHooks h; h.commit = &real_commit; h.save = &real_save;
  h.abort = &model_abort; h.ctx = nullptr; return h;
}
static BreedHooks hooks_model(void)
{
  BreedHooks h; h.commit = &model_commit; h.save = &model_save;
  h.abort = &model_abort; h.ctx = nullptr; return h;
}

struct Wire {
  LoopbackLink lk;
  LoopbackPort port[2];
  Transport    tp[2];
  Session      s[2];
  BreedLink    bl[2];
  uint32_t     now, iters;
  bool         hung;
  uint8_t      accept[2];       // does this side's player say yes
};

static Wire& wire_arena(void) { static Wire w; return w; }

static void wire_begin(Wire& W, uint8_t sp_a, uint8_t sp_b, uint32_t seed)
{
  memset(&W, 0, sizeof W);
  W.accept[0] = 1u; W.accept[1] = 1u;
  LoopbackFault f; memset(&f, 0, sizeof f);
  loopback_init(W.lk, seed, f);
  W.now = 100000u;

  kv_mem_reset();
  memset(&g_gs, 0, sizeof g_gs);
  box_bind(g_gs);
  g_model_filed = 0u;
  g_model_reject = (uint8_t)BRD_OK;
  g_real_reject  = (uint8_t)BRD_OK;
  memset(&g_model_plan, 0, sizeof g_model_plan);

  for (uint8_t i = 0; i < 2u; ++i) W.tp[i] = transport_loopback(W.port[i], W.lk, i);

  SessionCfg cfg;
  for (uint8_t i = 0; i < 2u; ++i) {
    memset(&cfg, 0, sizeof cfg);
    cfg.tp = &W.tp[i];
    cfg.bl = &W.bl[i];
    cfg.op = (uint8_t)SOP_BREED;
    cfg.device_id = (i == 0u) ? DEV_A : DEV_B;
    cfg.nonce = 0xA11CE000u + i * 0x1000u + seed;
    cfg.lvl_lo = 1u; cfg.lvl_hi = (uint8_t)XP_LEVEL_MAX;
    session_init(W.s[i], cfg);
  }

  // Endpoint 0's parent goes into the REAL Box, so breed_commit() has somewhere
  // to file and so BRD_BOX_FULL is reachable by filling it.
  Genome gen = sealed_genome(0xC0FFEEu);
  const uint8_t slot = box_new_bug(sp_a, 12u, (uint8_t)ORIGIN_WILD, gen, 0x701u, 1000u);
  CHECK(slot != (uint8_t)BOX_SLOT_NONE);
  const BugInstance* mine = box_peek(slot);
  CHECK(mine != nullptr);
  if (mine == nullptr) return;
  CHECK_EQ((int)session_set_breed(W.s[0], *mine), (int)VR_OK);
  breed_link_init(W.bl[0], W.s[0], hooks_real());

  BugInstance theirs;
  mk_free_bug(theirs, sp_b, 12u, 0x702u);
  CHECK_EQ((int)session_set_breed(W.s[1], theirs), (int)VR_OK);
  breed_link_init(W.bl[1], W.s[1], hooks_model());

  session_start(W.s[0], W.now);
  session_start(W.s[1], W.now);
}

static void wire_run(Wire& W, uint32_t max_iters = 200000u)
{
  while (!(session_closed(W.s[0]) && session_closed(W.s[1]))) {
    if (W.iters++ >= max_iters) { W.hung = true; return; }
    const uint32_t moved = W.lk.stats.sent + W.lk.stats.delivered;
    for (uint8_t i = 0; i < 2u; ++i) {
      session_poll(W.s[i], W.now);
      if (breed_link_wants_consent(W.s[i]) && W.accept[i] != 0u)
        breed_link_accept(W.s[i]);
    }
    if ((W.lk.stats.sent + W.lk.stats.delivered) == moved) W.now += PROTO_RETX_MS;
  }
}

// =============================================================================
//  THE CASES
// =============================================================================

TEST(the_roster_still_contains_a_pair_that_can_breed) {
  const FoundPair p = find_breedable_pair();
  if (!p.ok) fprintf(stderr, "    no compatible pair in %d species\n",
                     (int)SPECIES_TABLE_COUNT);
  CHECK(p.ok);
  const FoundPair q = find_incompatible_pair();
  CHECK(q.ok);
  printf("  breedable: species %u x %u; incompatible: %u x %u\n",
         (unsigned)p.a, (unsigned)p.b, (unsigned)q.a, (unsigned)q.b);
}

TEST(a_clean_breeding_gives_both_ends_the_same_child) {
  const FoundPair p = find_breedable_pair();
  CHECK(p.ok);
  if (!p.ok) return;
  Wire& W = wire_arena();
  wire_begin(W, p.a, p.b, 0x1234u);
  wire_run(W);

  CHECK(!W.hung);
  CHECK(session_closed(W.s[0]));
  CHECK(session_closed(W.s[1]));
  CHECK_EQ((int)session_end(W.s[0]).reason, (int)SE_DONE);
  CHECK_EQ((int)session_end(W.s[1]).reason, (int)SE_DONE);

  // BOTH ENDS COMPUTED A CHILD...
  const BreedPlan* a = breed_link_plan(W.s[0]);
  const BreedPlan* b = breed_link_plan(W.s[1]);
  CHECK(a != nullptr);
  CHECK(b != nullptr);
  if (a == nullptr || b == nullptr) return;

  // ...AND IT IS THE SAME CHILD. This is the whole point of the module.
  CHECK_EQ((int)(a->seed == b->seed), 1);
  CHECK_EQ((int)a->species_id, (int)b->species_id);
  CHECK_EQ((int)a->level, (int)b->level);
  CHECK_EQ((int)a->flags, (int)b->flags);
  CHECK_EQ((int)a->from_b, (int)b->from_b);
  CHECK_EQ(memcmp(&a->genome, &b->genome, sizeof(Genome)), 0);

  // Each side filed its own, and each side knows the other did.
  CHECK(breed_link_completed(W.s[0]));
  CHECK(breed_link_completed(W.s[1]));
  CHECK_EQ((int)g_model_filed, 1);
  CHECK(W.bl[0].slot != (uint8_t)BOX_SLOT_NONE);
  const BugInstance* child = box_peek(W.bl[0].slot);
  CHECK(child != nullptr);
  if (child != nullptr) {
    CHECK_EQ((int)child->species_id, (int)a->species_id);
    CHECK_EQ((int)child->level, 1);
  }
}

TEST(neither_end_files_anything_until_both_people_have_said_yes) {
  const FoundPair p = find_breedable_pair();
  CHECK(p.ok);
  if (!p.ok) return;
  Wire& W = wire_arena();
  wire_begin(W, p.a, p.b, 0x2222u);
  W.accept[0] = 0u;                       // our player never presses A
  W.accept[1] = 1u;

  // Run until it is clear nobody is going anywhere: the peer confirmed and is
  // waiting, and we are parked at the question.
  for (uint32_t k = 0; k < 4000u && !session_closed(W.s[0]); ++k) {
    const uint32_t moved = W.lk.stats.sent + W.lk.stats.delivered;
    for (uint8_t i = 0; i < 2u; ++i) {
      session_poll(W.s[i], W.now);
      if (breed_link_wants_consent(W.s[i]) && W.accept[i] != 0u)
        breed_link_accept(W.s[i]);
    }
    if ((W.lk.stats.sent + W.lk.stats.delivered) == moved) W.now += PROTO_RETX_MS;
  }

  // NOTHING WAS FILED ANYWHERE. game/breeding.h names this audit risk 5: there
  // is no auto-accepted egg, and a peer's yes is not consent on this device.
  CHECK_EQ((int)W.bl[0].filed, 0);
  CHECK_EQ((int)g_model_filed, 0);
  CHECK_EQ((int)box_count(), 1);          // only the parent we put there
}

TEST(an_incompatible_pair_is_refused_by_name_and_nobody_is_asked) {
  const FoundPair q = find_incompatible_pair();
  CHECK(q.ok);
  if (!q.ok) return;
  Wire& W = wire_arena();
  wire_begin(W, q.a, q.b, 0x3333u);
  wire_run(W);

  CHECK(!W.hung);
  CHECK(session_closed(W.s[0]));
  CHECK(session_closed(W.s[1]));
  CHECK_EQ((int)session_end(W.s[0]).reason, (int)SE_REJECTED);
  // THE REASON IS THE PAIR'S, not the record's: both parents are perfectly
  // legal Bugs and validate_bug() said so on both sides.
  CHECK_EQ((int)W.bl[0].in_verdict, (int)VR_OK);
  CHECK_EQ((int)W.bl[0].pair_reject, (int)BRD_COMPAT_GROUP);
  CHECK_EQ((int)W.bl[0].filed, 0);
  CHECK_EQ((int)g_model_filed, 0);
  CHECK_EQ((int)box_count(), 1);
}

TEST(a_full_box_on_one_side_does_not_deny_the_other_its_child) {
  const FoundPair p = find_breedable_pair();
  CHECK(p.ok);
  if (!p.ok) return;
  Wire& W = wire_arena();
  wire_begin(W, p.a, p.b, 0x4444u);
  // OUR side cannot file. breed_link.h section 4 argues this is deliberately
  // NOT a session failure: nothing is lost or duplicated by it, the pair was
  // agreed, and one Box's housekeeping is not a reason to deny the other player
  // the thing they both said yes to.
  g_real_reject = (uint8_t)BRD_BOX_FULL;
  wire_run(W);

  CHECK(!W.hung);
  CHECK_EQ((int)session_end(W.s[0]).reason, (int)SE_DONE);
  CHECK_EQ((int)session_end(W.s[1]).reason, (int)SE_DONE);
  CHECK_EQ((int)W.bl[0].commit_reject, (int)BRD_BOX_FULL);
  CHECK_EQ((int)W.bl[0].slot, (int)BOX_SLOT_NONE);
  // The PEER still got its child, and OUR end was told the peer managed it.
  CHECK_EQ((int)g_model_filed, 1);
  CHECK_EQ((int)W.bl[0].peer_commit_reject, (int)BRD_OK);
  // ...and the peer was told OUR reason rather than being left to assume.
  CHECK_EQ((int)W.bl[1].peer_commit_reject, (int)BRD_BOX_FULL);
}

// THE DESYNC CHECK ITSELF, DRIVEN. The mutation that motivates it - ordering
// the parents by locality instead of by SessionRole - is caught by the case
// above because the TEST compares the two plans. On a board nothing compares
// them: the only thing standing between two devices and two different children
// is the plan CRC in BREED_CONFIRM. So this drives that check directly, by
// making one end's plan CRC disagree after it was computed, and asserts the
// session dies NAMING it instead of filing two different creatures.
TEST(two_ends_that_computed_different_children_close_before_filing) {
  const FoundPair p = find_breedable_pair();
  CHECK(p.ok);
  if (!p.ok) return;
  Wire& W = wire_arena();
  wire_begin(W, p.a, p.b, 0x5555u);

  bool poisoned = false;
  for (uint32_t k = 0; k < 8000u &&
       !(session_closed(W.s[0]) && session_closed(W.s[1])); ++k) {
    const uint32_t moved = W.lk.stats.sent + W.lk.stats.delivered;
    for (uint8_t i = 0; i < 2u; ++i) {
      session_poll(W.s[i], W.now);
      // The moment endpoint 1 has a plan, give it a different one's identity.
      if (!poisoned && W.bl[1].have_plan != 0u) {
        W.bl[1].plan_crc = (uint16_t)(W.bl[1].plan_crc ^ 0xA5A5u);
        poisoned = true;
      }
      if (breed_link_wants_consent(W.s[i]) && W.accept[i] != 0u)
        breed_link_accept(W.s[i]);
    }
    if ((W.lk.stats.sent + W.lk.stats.delivered) == moved) W.now += PROTO_RETX_MS;
  }

  CHECK(poisoned);
  CHECK(session_closed(W.s[0]));
  CHECK_EQ((int)session_end(W.s[0]).reason, (int)SE_PROTOCOL);
  CHECK_EQ((int)session_end(W.s[0]).detail, (int)SD_BREED_PLAN);
  // AND NOTHING WAS FILED. That is the half that matters: a named terminal
  // with two children in two Boxes would be a worse outcome than a crash.
  CHECK_EQ((int)W.bl[0].filed, 0);
  CHECK_EQ((int)g_model_filed, 0);
  CHECK_EQ((int)box_count(), 1);
}
