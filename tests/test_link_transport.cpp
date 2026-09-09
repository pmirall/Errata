// =============================================================================
//  ERRATA host test - test_link_transport.cpp
//  P7-C1: THE RING THE RADIO CALLBACK POSTS INTO, AND THE SEAM OVER IT.
//
//  TWO HALVES, AND THE SECOND IS THE ONE THAT ANSWERS THE CHUNK'S QUESTION.
//
//  Part 1 drives networking/rxring.cpp directly: full, oversize, wrap, drain,
//  two rings in one process, and the one invariant the seam rests on - a pop
//  returns 0 ONLY when the ring is empty. transport_loopback.cpp:107-118 wrote
//  that hole down for P7 to inherit, in so many words ("IT IS ALSO THE ONE PATH
//  WHERE THIS FUNCTION'S 0 IS A LIE ... Nothing in this tree can reach it, but
//  P7's driver will not have that guarantee"), and this is where it is closed.
//
//  Part 2 runs a WHOLE BATTLE between two real sessions over a Transport whose
//  send() is rxring_push() and whose recv() is rxring_pop() - THE SAME RING
//  CODE networking/transport_espnow.cpp fills from the Wi-Fi task. The chunk's
//  standing claim is that the session runs over the radio with no change to
//  networking/session.cpp; session.cpp and battle_link.cpp are byte-identical
//  across P7-C1 and this binary is what makes that claim mean something,
//  because it links those objects and drives them over the radio's own ring.
//
//  WHAT THIS BINARY DOES NOT PROVE, SAID PLAINLY. It is single-threaded. It
//  says nothing about the Wi-Fi task, nothing about real preemption of a pop by
//  a push, and nothing about ESP-NOW itself - the acknowledgement semantics,
//  the duplicate suppression, the real MTU enforcement and the callback
//  threading are all unobserved until two boards are on a bench. The SPSC claim
//  rests on the discipline in rxring.h and on the two precedents it names.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "core/crc16.h"
#include "core/rng.h"
#include "data/species_table.h"
#include "game/battle.h"
#include "game/battle_ai.h"
#include "game/evolution.h"
#include "game/genome.h"
#include "game/bug.h"
#include "game/validate.h"
#include "game/xp.h"
#include "core/config.h"
#include "networking/battle_link.h"
#include "networking/protocol.h"
#include "networking/rxring.h"
#include "networking/session.h"
#include "networking/transport.h"

// =============================================================================
//  PART 1 - THE RING
// =============================================================================

#define TR_SLOTS   8u
#define TR_STRIDE  16u

struct RingBox {
  RxRing   r;
  uint8_t  bytes[TR_SLOTS * TR_STRIDE];
  uint16_t lens[TR_SLOTS];
};

static void box_init(RingBox& b)
{
  memset(&b, 0, sizeof b);
  CHECK(rxring_init(b.r, b.bytes, b.lens, (uint8_t)TR_SLOTS, (uint16_t)TR_STRIDE));
}

// A record whose every byte is `marker`, so a mix-up is visible in the payload
// and not only in a counter.
static void mk_rec(uint8_t* out, uint16_t n, uint8_t marker)
{
  for (uint16_t i = 0; i < n; ++i) out[i] = marker;
}

TEST(rxring_init_refuses_a_shape_it_could_not_wrap_correctly)
{
  RingBox b;
  uint8_t  bytes[64];
  uint16_t lens[8];
  // Not a power of two: the mask would be wrong and the ring would overwrite.
  CHECK(!rxring_init(b.r, bytes, lens, 7u, 8u));
  CHECK_EQ((int)rxring_pending(b.r), 0);
  CHECK(!rxring_push(b.r, bytes, 4u));          // and it refuses everything
  CHECK_EQ((int)rxring_pop(b.r, bytes, sizeof bytes), 0);

  CHECK(!rxring_init(b.r, bytes, lens, 8u, 0u));   // zero stride
  CHECK(!rxring_init(b.r, nullptr, lens, 8u, 8u)); // no storage
  CHECK(!rxring_init(b.r, bytes, nullptr, 8u, 8u));
  CHECK(!rxring_init(b.r, bytes, lens, 1u, 8u));   // one slot cannot hold one
  CHECK(rxring_init(b.r, bytes, lens, 8u, 8u));    // ...and a legal one takes
}

// THE OVERFLOW RULE, ASSERTED ON THE PAYLOAD AND NOT ONLY ON THE COUNTER.
// A ring that overwrote at tail when full would still count an overflow if the
// counter were bumped first; what separates the two implementations is WHICH
// seven records come back out.
TEST(a_full_ring_refuses_the_newest_and_keeps_every_older_one)
{
  RingBox b;
  box_init(b);

  uint8_t rec[TR_STRIDE];
  // A ring of 8 slots holds 7: one slot is the empty/full discriminator.
  for (uint8_t i = 0; i < 7u; ++i) {
    mk_rec(rec, TR_STRIDE, (uint8_t)(0x10u + i));
    CHECK(rxring_push(b.r, rec, (uint16_t)TR_STRIDE));
  }
  CHECK_EQ((int)rxring_pending(b.r), 7);
  CHECK_EQ((int)b.r.overflow, 0);

  mk_rec(rec, TR_STRIDE, 0xEE);                   // the eighth
  CHECK(!rxring_push(b.r, rec, (uint16_t)TR_STRIDE));
  CHECK_EQ((int)b.r.overflow, 1);
  CHECK_EQ((int)rxring_pending(b.r), 7);

  // The seven that come out are the seven that went in, in order, and 0xEE is
  // nowhere among them.
  for (uint8_t i = 0; i < 7u; ++i) {
    uint8_t out[TR_STRIDE];
    memset(out, 0, sizeof out);
    CHECK_EQ((int)rxring_pop(b.r, out, (uint16_t)sizeof out), (int)TR_STRIDE);
    CHECK_EQ((int)out[0], (int)(0x10u + i));
    CHECK_EQ((int)out[TR_STRIDE - 1u], (int)(0x10u + i));
  }
  CHECK_EQ((int)rxring_pop(b.r, rec, (uint16_t)sizeof rec), 0);
  CHECK_EQ((int)b.r.overflow, 1);
}

TEST(an_oversize_record_is_refused_and_never_truncated)
{
  RingBox b;
  box_init(b);

  uint8_t big[TR_STRIDE + 1u];
  mk_rec(big, (uint16_t)sizeof big, 0xAB);
  CHECK(!rxring_push(b.r, big, (uint16_t)sizeof big));
  CHECK_EQ((int)b.r.oversize, 1);
  // THE ASSERTION THAT SEPARATES "REFUSED" FROM "CLAMPED TO stride": the ring
  // is still empty. A clamp would have enqueued 16 bytes of it.
  CHECK_EQ((int)rxring_pending(b.r), 0);
  CHECK_EQ((int)b.r.overflow, 0);

  // ...and a legal record straight afterwards is unaffected.
  uint8_t rec[TR_STRIDE];
  mk_rec(rec, (uint16_t)TR_STRIDE, 0x5A);
  CHECK(rxring_push(b.r, rec, (uint16_t)TR_STRIDE));
  uint8_t out[TR_STRIDE];
  CHECK_EQ((int)rxring_pop(b.r, out, (uint16_t)sizeof out), (int)TR_STRIDE);
  CHECK_EQ((int)out[0], 0x5A);
}

TEST(a_null_or_empty_push_is_a_caller_bug_and_inflates_no_counter)
{
  RingBox b;
  box_init(b);
  uint8_t rec[TR_STRIDE];
  mk_rec(rec, (uint16_t)TR_STRIDE, 1u);
  CHECK(!rxring_push(b.r, nullptr, 4u));
  CHECK(!rxring_push(b.r, rec, 0u));
  CHECK_EQ((int)b.r.overflow, 0);
  CHECK_EQ((int)b.r.oversize, 0);
  CHECK_EQ((int)rxring_pending(b.r), 0);
}

// THE INVARIANT THE SEAM RESTS ON. networking/session.cpp drains with
// `for(;;) { n = recv(...); if (n == 0) break; }`, so a 0 that does not mean
// "empty" stalls the session behind whatever produced it. Here the caller's
// buffer is deliberately too small for the queued record.
TEST(a_pop_never_returns_zero_while_the_ring_still_holds_a_record)
{
  RingBox b;
  box_init(b);

  uint8_t big[TR_STRIDE];
  mk_rec(big, (uint16_t)TR_STRIDE, 0xC3);
  CHECK(rxring_push(b.r, big, (uint16_t)TR_STRIDE));
  uint8_t small_rec[4];
  mk_rec(small_rec, (uint16_t)sizeof small_rec, 0x77);
  CHECK(rxring_push(b.r, small_rec, (uint16_t)sizeof small_rec));
  CHECK_EQ((int)rxring_pending(b.r), 2);

  // A 4-byte buffer cannot take the first record. The 0 that a naive
  // implementation returns here would be a lie: the ring holds two.
  uint8_t out[4];
  const uint16_t n = rxring_pop(b.r, out, (uint16_t)sizeof out);
  CHECK_EQ((int)n, 4);                        // the SECOND record, not a 0
  CHECK_EQ((int)out[0], 0x77);
  CHECK_EQ((int)b.r.oversize, 1);             // the skipped one was counted
  CHECK_EQ((int)rxring_pending(b.r), 0);
  // ...and only now does 0 appear, meaning exactly what it says.
  CHECK_EQ((int)rxring_pop(b.r, out, (uint16_t)sizeof out), 0);
}

TEST(a_wrapped_ring_hands_back_the_right_bytes_for_three_whole_laps)
{
  RingBox b;
  box_init(b);
  uint8_t marker = 0u;
  for (uint16_t lap = 0; lap < 3u; ++lap) {
    for (uint8_t i = 0; i < 7u; ++i) {
      uint8_t rec[TR_STRIDE];
      mk_rec(rec, (uint16_t)TR_STRIDE, ++marker);
      CHECK(rxring_push(b.r, rec, (uint16_t)TR_STRIDE));
    }
    for (uint8_t i = 0; i < 7u; ++i) {
      uint8_t out[TR_STRIDE];
      memset(out, 0, sizeof out);
      CHECK_EQ((int)rxring_pop(b.r, out, (uint16_t)sizeof out), (int)TR_STRIDE);
      CHECK_EQ((int)out[0], (int)(lap * 7u + i + 1u));
      CHECK_EQ((int)out[TR_STRIDE - 1u], (int)(lap * 7u + i + 1u));
    }
  }
  CHECK_EQ((int)b.r.overflow, 0);
  CHECK_EQ((int)b.r.oversize, 0);
}

// A SHORT RECORD IS A WHOLE RECORD. The length array is what makes the ring a
// DATAGRAM queue rather than a byte queue, and the exact count is what
// networking/protocol.h's decoder treats the `len` field as a check against.
TEST(records_of_different_lengths_keep_their_own_exact_counts)
{
  RingBox b;
  box_init(b);
  for (uint16_t n = 1u; n <= 7u; ++n) {
    uint8_t rec[TR_STRIDE];
    mk_rec(rec, n, (uint8_t)(0xA0u + n));
    CHECK(rxring_push(b.r, rec, n));
  }
  for (uint16_t n = 1u; n <= 7u; ++n) {
    uint8_t out[TR_STRIDE];
    memset(out, 0, sizeof out);
    CHECK_EQ((int)rxring_pop(b.r, out, (uint16_t)sizeof out), (int)n);
    CHECK_EQ((int)out[0], (int)(0xA0u + n));
    CHECK_EQ((int)out[n], 0);                 // nothing beyond the count
  }
}

// rxring_drain() is the consumer emptying the ring by moving ITS OWN cursor.
// ble_social.cpp:345-350 recorded what the other implementation cost (that file
// went with BLE in P8-C0; `git show ed9b099:Pebblebol/src/networking/ble_social.cpp`).
TEST(drain_empties_the_ring_from_the_consumer_side_and_leaves_the_producer_alone)
{
  RingBox b;
  box_init(b);
  uint8_t rec[TR_STRIDE];
  for (uint8_t i = 0; i < 5u; ++i) {
    mk_rec(rec, (uint16_t)TR_STRIDE, (uint8_t)(i + 1u));
    CHECK(rxring_push(b.r, rec, (uint16_t)TR_STRIDE));
  }
  const uint8_t tail_before = b.r.tail;
  rxring_drain(b.r);
  CHECK_EQ((int)rxring_pending(b.r), 0);
  CHECK_EQ((int)b.r.tail, (int)tail_before);   // the producer's cursor is untouched
  CHECK_EQ((int)b.r.head, (int)tail_before);

  // A drained ring still works: the next push is visible.
  mk_rec(rec, (uint16_t)TR_STRIDE, 0x99);
  CHECK(rxring_push(b.r, rec, (uint16_t)TR_STRIDE));
  uint8_t out[TR_STRIDE];
  CHECK_EQ((int)rxring_pop(b.r, out, (uint16_t)sizeof out), (int)TR_STRIDE);
  CHECK_EQ((int)out[0], 0x99);
}

TEST(two_rings_in_one_process_do_not_share_a_single_byte)
{
  RingBox a, b;
  box_init(a);
  box_init(b);
  uint8_t rec[TR_STRIDE];
  mk_rec(rec, (uint16_t)TR_STRIDE, 0xA1);
  CHECK(rxring_push(a.r, rec, (uint16_t)TR_STRIDE));
  CHECK_EQ((int)rxring_pending(a.r), 1);
  CHECK_EQ((int)rxring_pending(b.r), 0);       // the file has no state of its own
  uint8_t out[TR_STRIDE];
  CHECK_EQ((int)rxring_pop(b.r, out, (uint16_t)sizeof out), 0);
  CHECK_EQ((int)rxring_pop(a.r, out, (uint16_t)sizeof out), (int)TR_STRIDE);
  CHECK_EQ((int)out[0], 0xA1);
}

// =============================================================================
//  PART 2 - A WHOLE BATTLE OVER THE RADIO'S OWN RING
// =============================================================================

// One endpoint's port: it pushes into the OTHER endpoint's inbox and pops from
// its own, which is exactly what a pair of radios does. Caller-owned, so the
// factory below has no file-scope anything - the same property
// transport_loopback.cpp has and tools/check.sh gates.
// THE DEPTH IS THE PRODUCTION CONSTANT AND NOT A NUMBER THIS TEST PICKED.
// LINK_RX_SLOTS is what networking/transport_espnow.cpp gives its session ring,
// so `refused == 0` below is a statement about the shipping depth. Reverting
// core/config.h to the eight the P7-C1 survey proposed makes this binary red.
struct RingLink {
  RxRing   ring[2];
  uint8_t  bytes[2][(size_t)LINK_RX_SLOTS * (size_t)PROTO_FRAME_MAX];
  uint16_t lens[2][LINK_RX_SLOTS];
  uint32_t refused[2];        // sends the ring would not take
  uint32_t moved;             // frames pushed or popped, either direction
  uint32_t burst_max;         // most frames one session_poll() put in one ring
  uint32_t refused_open;      // ...of those, the ones refused while BOTH sessions
                              // were still draining. See ring_run().
};

struct RingPort {
  RingLink* link;
  uint8_t   endpoint;
};

// A PER-FRAME DROP, SEEDED, AND IT IS HERE FOR ONE MEASUREMENT ONLY: how big a
// burst the retransmission ladder can put in one direction. This is NOT a
// second fault-injecting loopback - tests/test_session.cpp owns that census and
// owns the argument about what a uniform independent drop does and does not
// model. What the case at the bottom of this file needs is the WORST OCCUPANCY
// the ring has to survive, and that number is larger on a lossy link than on a
// clean one, which is exactly the thing a clean-link test cannot tell you.
static unsigned g_drop_pm = 0;
static Rng      g_drop_rng;

static bool rl_send(void* ctx, const uint8_t* frame, uint16_t n)
{
  RingPort* p = (RingPort*)ctx;
  if (p == nullptr || p->link == nullptr) return false;
  const uint8_t dst = (uint8_t)(p->endpoint ^ 1u);
  p->link->moved++;
  // A dropped frame still returns true - transport.h:36-40's rule, and the same
  // statement esp_now_send()'s ESP_OK makes about a frame that may still be
  // lost in the air.
  if (g_drop_pm != 0u && rng_next_below(g_drop_rng, 1000u) < (uint32_t)g_drop_pm) {
    return true;
  }
  if (!rxring_push(p->link->ring[dst], frame, n)) {
    p->link->refused[p->endpoint]++;
    // TRUE ANYWAY, and that is the seam's rule rather than a convenience: the
    // session must not be able to tell a frame the radio dropped from one it
    // delivered (transport.h:36-40). esp_now_send() returning ESP_OK is the
    // same statement about a frame that may still be lost in the air.
    return true;
  }
  return true;
}

static uint16_t rl_recv(void* ctx, uint8_t* buf, uint16_t cap)
{
  RingPort* p = (RingPort*)ctx;
  if (p == nullptr || p->link == nullptr) return 0u;
  const uint16_t n = rxring_pop(p->link->ring[p->endpoint], buf, cap);
  if (n != 0u) p->link->moved++;
  return n;
}

static Transport transport_ring(RingPort& port, RingLink& lk, uint8_t endpoint)
{
  port.link     = &lk;
  port.endpoint = (uint8_t)(endpoint & 1u);
  Transport t;
  t.ctx  = &port;
  t.send = &rl_send;
  t.recv = &rl_recv;
  t.mtu  = (uint16_t)PROTO_FRAME_MAX;     // what transport_espnow() sets, exactly
  return t;
}

static void ring_link_init(RingLink& lk)
{
  memset(&lk, 0, sizeof lk);
  for (uint8_t e = 0; e < 2u; ++e) {
    CHECK(rxring_init(lk.ring[e], lk.bytes[e], lk.lens[e],
                      (uint8_t)LINK_RX_SLOTS, (uint16_t)PROTO_FRAME_MAX));
  }
}

// --- the two endpoints, the same shape tests/test_session.cpp uses -----------
static Genome sealed_genome(uint32_t lineage)
{
  Genome g;
  memset(&g, 0, sizeof g);
  g.lineage_id = lineage ? lineage : 1u;
  g.g0 = 0x1234u; g.g1 = 0x5678u; g.g2 = 0x09ABu;
  g.generation = 2u;
  genome_seal(g);
  return g;
}

static void mk_valid(BugInstance& p, uint8_t species, uint8_t level, uint32_t id)
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
  if (evolution_level_ready(p) != 0u) p.evo_state |= (uint8_t)EVO_STATE_PENDING;
}

struct RingEndpoint {
  Session        s;
  BattleSetup    setup;
  BattleState    st;
  BattleLog      blog;
  BattleEvent    bev[1024];
  LinkLog        llog;
  LinkEvent      lev[256];
  RingPort       port;
  Transport      tp;
  BattleAi       ai;
  BugInstance box[BATTLE_TEAM_MAX];
};

struct RingTrial {
  RingLink     lk;
  RingEndpoint e[2];
  uint32_t     now;
  uint32_t     iters;
};

static RingTrial& ring_arena(void) { static RingTrial t; return t; }

// Varies the two teams, the two nonces and the two AI seeds, so a sweep is a
// sweep over real different battles rather than the same one 200 times.
static unsigned g_seed = 0;

static void ring_trial_begin(RingTrial& T)
{
  memset(&T, 0, sizeof T);
  rng_init(g_drop_rng, 0xBEEF0000u + (uint32_t)g_seed);
  ring_link_init(T.lk);
  T.now = 100000u;
  for (uint8_t i = 0; i < 2u; ++i) {
    RingEndpoint& E = T.e[i];
    for (uint8_t k = 0; k < (uint8_t)BATTLE_TEAM_MAX; ++k) {
      mk_valid(E.box[k], (uint8_t)(1u + ((i * 12u + k * 4u + g_seed) % 24u)),
               (uint8_t)(5u + (g_seed % 20u)),
               (uint32_t)(0x100u * (i + 1u) + k));
    }
    battle_log_init(E.blog, E.bev, (uint16_t)(sizeof E.bev / sizeof E.bev[0]));
    link_log_init(E.llog, E.lev, (uint16_t)(sizeof E.lev / sizeof E.lev[0]));
    E.tp = transport_ring(E.port, T.lk, i);
    SessionCfg cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.tp = &E.tp; cfg.setup = &E.setup; cfg.st = &E.st;
    cfg.blog = &E.blog; cfg.llog = &E.llog;
    cfg.device_id = (i == 0u) ? 0x0000A001u : 0x0000B002u;
    cfg.nonce     = ((i == 0u) ? 0xA11CE000u : 0xB0B00000u) ^ (uint32_t)g_seed;
    cfg.lvl_lo = 1u; cfg.lvl_hi = 30u;
    session_init(E.s, cfg);
    battle_ai_init(E.ai, i, 0x51EED000u + i + (uint32_t)g_seed * 7919u);
    uint8_t bad = 0xFFu;
    CHECK_EQ((int)session_set_team(E.s, E.box, (uint8_t)BATTLE_TEAM_MAX, bad), (int)VR_OK);
  }
}

static void ring_half(RingTrial& T, uint8_t i)
{
  RingEndpoint& E = T.e[i];
  const uint8_t  dst    = (uint8_t)(i ^ 1u);
  const uint8_t  before = rxring_pending(T.lk.ring[dst]);
  const uint32_t ref0   = T.lk.refused[i];
  session_poll(E.s, T.now);
  for (uint8_t guard = 0; guard < 4u && session_wants_action(E.s); ++guard) {
    const BattleAction a = battle_ai_choose(E.ai, E.st);
    session_submit_action(E.s, a, T.now);
  }
  // HOW BIG ONE BURST GETS, measured rather than assumed. A poll drains
  // everything queued and answers all of it, so the peer's ring sees the whole
  // answer at once - and on a real pair of boards it is worse, because the
  // Wi-Fi task fills the ring between two loop() passes and neither device
  // waits for the other.
  const uint32_t burst = (uint32_t)(rxring_pending(T.lk.ring[dst]) - before) +
                         (T.lk.refused[i] - ref0);
  if (burst > T.lk.burst_max) T.lk.burst_max = burst;
}

static void ring_run(RingTrial& T)
{
  while (!(session_closed(T.e[0].s) && session_closed(T.e[1].s))) {
    if (T.iters++ >= 400000u) return;
    // A CLOSED SESSION STOPS DRAINING ITS RING, and the other side may still be
    // re-sending a GOODBYE it has not been answered for. Frames refused after
    // that point say nothing about the depth the LIVE link needs - on the
    // device the screen has torn the transport down by then - so they are
    // counted separately from the ones that matter.
    const bool     both_open = !session_closed(T.e[0].s) && !session_closed(T.e[1].s);
    const uint32_t ref_before = T.lk.refused[0] + T.lk.refused[1];
    // THE VIRTUAL CLOCK ADVANCES ONLY WHEN A ROUND MOVED NO FRAME AT ALL, so
    // one idle round is exactly one rung of the retransmission ladder and the
    // whole nine-second deadline costs zero wall clock. Counting the frames is
    // what makes that exact: an earlier version of this loop compared the ring
    // OCCUPANCY before and after, which is unchanged whenever one frame is
    // pushed and one popped in the same round - so the ladder fired on rounds
    // that had made progress, and the retransmissions it produced then really
    // did overflow a ring of eight. The harness was wrong, not the depth.
    const uint32_t moved0 = T.lk.moved;
    ring_half(T, 0u);
    ring_half(T, 1u);
    if (T.lk.moved == moved0) {
      T.now += PROTO_RETX_MS;
    }
    if (both_open) {
      T.lk.refused_open += (T.lk.refused[0] + T.lk.refused[1]) - ref_before;
    }
  }
}

// THE CASE THE WHOLE CHUNK RESTS ON. The transport under these two sessions is
// the radio's ring and nothing else - the same rxring.cpp
// networking/transport_espnow.cpp fills from the Wi-Fi task, with the same mtu
// transport_espnow() sets - and networking/session.cpp is not recompiled, not
// conditionally compiled and not edited. tools/check.sh now fails the build if
// session.cpp or battle_link.cpp grows a single preprocessor conditional.
TEST(a_whole_battle_runs_over_the_ring_the_radio_fills_with_no_change_to_the_session)
{
  RingTrial& T = ring_arena();
  ring_trial_begin(T);
  session_start(T.e[0].s, T.now);
  session_start(T.e[1].s, T.now);
  ring_run(T);

  CHECK(session_closed(T.e[0].s));
  CHECK(session_closed(T.e[1].s));
  CHECK_EQ((int)session_end(T.e[0].s).reason, (int)SE_DONE);
  CHECK_EQ((int)session_end(T.e[1].s).reason, (int)SE_DONE);
  // Both sides agree on the battle, which is the property a silent desync
  // would break.
  CHECK_EQ(session_end(T.e[0].s).local_hash, session_end(T.e[1].s).local_hash);
  CHECK(session_end(T.e[0].s).local_hash != 0u);
  CHECK(session_rewards_authorised(T.e[0].s));
  CHECK(session_rewards_authorised(T.e[1].s));
  // NOT VACUOUS: a run that aborted immediately would satisfy "no desync" and
  // prove nothing, so the battle really has to have happened.
  CHECK(session_end(T.e[0].s).round >= 1u);
  CHECK(session_end(T.e[0].s).rx_ok >= 8u);
  // NOT ONE FRAME WAS REFUSED BY A RING OF LINK_RX_SLOTS...
  CHECK_EQ((int)T.lk.refused[0], 0);
  CHECK_EQ((int)T.lk.refused[1], 0);
  CHECK_EQ((int)T.lk.ring[0].overflow, 0);
  CHECK_EQ((int)T.lk.ring[1].overflow, 0);
  CHECK_EQ((int)T.lk.ring[0].oversize, 0);
  CHECK_EQ((int)T.lk.ring[1].oversize, 0);
  // ...AND A RING OF EIGHT WOULD HAVE REFUSED SOME, which is what stops the
  // two lines above from passing vacuously and is why core/config.h says 32.
  // Measured on this fixture: the largest single burst is 10 frames, and eight
  // slots hold seven. This assertion is the reason the constant cannot quietly
  // go back down.
  CHECK(T.lk.burst_max > 7u);
  CHECK(T.lk.burst_max < (uint32_t)LINK_RX_SLOTS);
}

// AND THE OTHER HALF: every frame the session puts on this transport fits the
// mtu transport_espnow() publishes, so ESP-NOW's own 250 B ceiling is never
// approached and PROTO_FRAME_MAX really is the tighter bound.
TEST(no_frame_the_session_emits_is_larger_than_the_mtu_the_radio_publishes)
{
  RingTrial& T = ring_arena();
  ring_trial_begin(T);
  CHECK_EQ((int)T.e[0].tp.mtu, (int)PROTO_FRAME_MAX);
  session_start(T.e[0].s, T.now);
  session_start(T.e[1].s, T.now);
  ring_run(T);
  // Nothing was refused for size at either end, and the ring's stride IS the
  // mtu - so "no frame exceeded 164 bytes" is what these two counters say.
  CHECK_EQ((int)T.lk.ring[0].oversize, 0);
  CHECK_EQ((int)T.lk.ring[1].oversize, 0);
  CHECK_EQ((int)T.lk.ring[0].stride, (int)PROTO_FRAME_MAX);
  CHECK(PROTO_FRAME_MAX <= 250u);          // one ESP-NOW datagram, with room over
}

// =============================================================================
//  WHY core/config.h SAYS 32 AND NOT 8, AND NOT 16 EITHER
//
//  The P7-C1 survey argued from the shape of the protocol that "a lockstep
//  round has at most a handful of frames in flight per direction ... Eight is
//  generous, not tuned". Measured over 300 varied battles on this transport,
//  ONE session_poll() puts up to TEN frames in the peer's ring - and on a LOSSY
//  link, where the nine-rung ladder is re-sending an obligation while the peer
//  is still catching up, up to NINETEEN. Sixteen slots hold fifteen, so a ring
//  of sixteen would have overflowed too, and an overflowed frame is
//  indistinguishable to networking/session.cpp from one the radio dropped: the
//  symptom on a bench is a link that is quietly slower than it should be, with
//  no error anywhere. This case is that measurement, kept.
//
//  IT IS NOT A CENSUS OF THE PROTOCOL and does not try to be:
//  tests/test_session.cpp owns the 3,000-trial fault census and owns the
//  argument about what a uniform independent drop models. This is a sizing
//  measurement for one buffer.
// =============================================================================
TEST(a_lossy_link_bursts_harder_than_a_clean_one_and_the_ring_still_absorbs_it)
{
  RingTrial& T = ring_arena();
  uint32_t worst_clean = 0, worst_lossy = 0, completed = 0, trials = 0;

  for (unsigned sd = 0; sd < 40u; ++sd) {
    for (unsigned arm = 0; arm < 2u; ++arm) {
      g_seed    = sd;
      g_drop_pm = (arm == 0u) ? 0u : 100u;      // 0 % and 10 % per frame
      ring_trial_begin(T);
      session_start(T.e[0].s, T.now);
      session_start(T.e[1].s, T.now);
      ring_run(T);
      ++trials;
      if (session_end(T.e[0].s).reason == (uint8_t)SE_DONE &&
          session_end(T.e[1].s).reason == (uint8_t)SE_DONE) {
        ++completed;
        // NEVER A SILENT DIVERGENCE, on either arm.
        CHECK_EQ(session_end(T.e[0].s).local_hash, session_end(T.e[1].s).local_hash);
      }
      // NOT ONE FRAME REFUSED BY A RING OF LINK_RX_SLOTS while both endpoints
      // were live, on either arm.
      CHECK_EQ((int)T.lk.refused_open, 0);
      if (arm == 0u) { if (T.lk.burst_max > worst_clean) worst_clean = T.lk.burst_max; }
      else           { if (T.lk.burst_max > worst_lossy) worst_lossy = T.lk.burst_max; }
    }
  }
  g_drop_pm = 0u;
  g_seed    = 0u;

  CHECK_EQ((int)trials, 80);
  // NOT VACUOUS: the battles really happened, on both arms.
  CHECK(completed >= 70u);
  // The clean-link worst is the ten core/config.h quotes...
  CHECK_EQ((int)worst_clean, 10);
  // ...and the lossy one is strictly larger, which is the whole reason the
  // constant is not sized from a clean run.
  CHECK(worst_lossy > worst_clean);
  // A RING OF SIXTEEN HOLDS FIFTEEN AND WOULD HAVE OVERFLOWED HERE. This is
  // the assertion that stops LINK_RX_SLOTS from being trimmed back on the
  // grounds that ten frames fit in sixteen slots.
  CHECK(worst_lossy > 15u);
  // ...and the configured ring still absorbed every one of them.
  CHECK(worst_lossy < (uint32_t)LINK_RX_SLOTS);
}
