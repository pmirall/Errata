// =============================================================================
//  PEBBLEBOL host test - test_session.cpp
//  THE SESSION FSM, THE LOCKSTEP AND THE FAULT-INJECTING LOOPBACK (P4-C5).
//
//  WHAT THIS FILE HAS TO PROVE, in the plan's own words: "full battle between
//  two endpoints; disconnect at each state -> SESSION_LOST, both Boxes
//  untouched; duplicate seq dropped; gap detected; 10 % drop/dup/reorder ->
//  battle still completes or aborts cleanly, NEVER DESYNCS SILENTLY."
//
//  THE TWO ENDPOINTS ARE REAL. Each runs the real networking/session.cpp, the
//  real networking/battle_link.cpp, the real game/battle.cpp and its OWN
//  game/battle_ai.cpp over its own side. Nothing here scripts a battle: the two
//  engines are handed the same seed and have to stay identical for sixty rounds
//  across a channel that is dropping, duplicating and reordering their frames.
//
//  THE CENSUS IS THE ACCEPTANCE CRITERION AND IT IS WRITTEN SO IT CANNOT PASS
//  VACUOUSLY. A run in which every trial aborted immediately would satisfy
//  "never desyncs silently" and prove nothing, so the clean arm must show
//  EXACTLY N completions and every faulty arm must show BOTH a completion and a
//  non-completion. Every trial is a pure function of one printable seed.
//
//  WHERE THE FORGED FRAMES COME FROM. Several cases reach into LoopbackLink and
//  edit or inject a frame in flight, resealing its CRC. That is deliberate: it
//  is the only way to play a MODIFIED PEER without a second implementation of
//  the protocol, and it is what makes SD_INPUTS, SD_RULES, SD_TEAM_CHANGED,
//  SD_ACTION_CHANGED, SD_BASIS_SKEW and SE_INCOMPATIBLE reachable at all.
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
#include "game/pebble.h"
#include "game/validate.h"
#include "game/xp.h"
#include "networking/battle_link.h"
#include "networking/protocol.h"
#include "networking/session.h"
#include "networking/transport.h"

// =============================================================================
//  FIXTURES
// =============================================================================
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

// A Pebble validate_pebble() accepts AND the wire may carry.
static void mk_valid(PebbleInstance& p, uint8_t species, uint8_t level, uint32_t id)
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
  if (evolution_level_ready(p) != 0u) p.evo_state |= (uint8_t)EVO_STATE_PENDING;
}

static void mk_team(PebbleInstance* m, uint8_t s0, uint8_t s1, uint8_t s2,
                    uint8_t level, uint32_t id_base)
{
  mk_valid(m[0], s0, level, id_base + 0u);
  mk_valid(m[1], s1, level, id_base + 1u);
  mk_valid(m[2], s2, level, id_base + 2u);
}

// =============================================================================
//  THE TWO-ENDPOINT HARNESS
// =============================================================================
struct Endpoint {
  Session        s;
  BattleSetup    setup;
  BattleState    st;
  BattleLog      blog;
  BattleEvent    bev[2048];
  LinkLog        llog;
  LinkEvent      lev[512];
  LoopbackPort   port;
  Transport      tp;
  BattleAi       ai;
  PebbleInstance box[BATTLE_TEAM_MAX];      // "the Box": the caller's Pebbles
  PebbleInstance box_before[BATTLE_TEAM_MAX];
  uint8_t        count;
};

struct Trial {
  LoopbackLink lk;
  Endpoint     e[2];
  uint32_t     now;
  uint32_t     iters;
  bool         hung;
};

// One reused arena: a Trial is about 22 kB and 3,000 of them are run.
static Trial& arena(void) { static Trial t; return t; }

static const uint32_t DEV_ID[2] = { 0x0000A001u, 0x0000B002u };

static void ep_setup(Trial& T, uint8_t i, uint8_t lo, uint8_t hi, uint32_t nonce)
{
  Endpoint& E = T.e[i];
  battle_log_init(E.blog, E.bev, (uint16_t)(sizeof E.bev / sizeof E.bev[0]));
  link_log_init(E.llog, E.lev, (uint16_t)(sizeof E.lev / sizeof E.lev[0]));
  E.tp = transport_loopback(E.port, T.lk, i);
  SessionCfg cfg;
  memset(&cfg, 0, sizeof cfg);
  cfg.tp = &E.tp; cfg.setup = &E.setup; cfg.st = &E.st;
  cfg.blog = &E.blog; cfg.llog = &E.llog;
  cfg.device_id = DEV_ID[i];
  cfg.nonce = nonce;
  cfg.lvl_lo = lo; cfg.lvl_hi = hi;
  session_init(E.s, cfg);
  battle_ai_init(E.ai, i, 0x51EED000u + i);
  memcpy(E.box_before, E.box, sizeof E.box_before);
}

// The default pair: two legal level-10 teams with disjoint ids and a band that
// accepts them both.
static void trial_begin(Trial& T, uint32_t seed, const LoopbackFault& f)
{
  memset(&T, 0, sizeof T);
  loopback_init(T.lk, seed, f);
  T.now = 100000u;
  mk_team(T.e[0].box, 1u, 5u, 9u, 10u, 0x100u);
  mk_team(T.e[1].box, 13u, 17u, 21u, 10u, 0x200u);
  T.e[0].count = (uint8_t)BATTLE_TEAM_MAX;
  T.e[1].count = (uint8_t)BATTLE_TEAM_MAX;
  ep_setup(T, 0u, 1u, 30u, 0xA11CE000u ^ seed);
  ep_setup(T, 1u, 1u, 30u, 0xB0B00000u ^ seed);
}

static VReject trial_set_teams(Trial& T)
{
  uint8_t bad = 0xFFu;
  const VReject a = session_set_team(T.e[0].s, T.e[0].box, T.e[0].count, bad);
  if (a != VR_OK) return a;
  return session_set_team(T.e[1].s, T.e[1].box, T.e[1].count, bad);
}

// ONE POLL ROUND OVER BOTH ENDPOINTS. The virtual clock advances only when a
// round moved NO frame at all, so one idle round is exactly one rung of the
// retransmission ladder and the whole nine-second deadline costs zero wall
// clock. now_ms is a parameter of session_poll(), never a call inside it, which
// is what makes that possible.
static void trial_half(Trial& T, uint8_t i)
{
  Endpoint& E = T.e[i];
  session_poll(E.s, T.now);
  for (uint8_t guard = 0; guard < 4u && session_wants_action(E.s); ++guard) {
    const BattleAction a = battle_ai_choose(E.ai, E.st);
    session_submit_action(E.s, a, T.now);
  }
}

static bool trial_step(Trial& T)
{
  const uint32_t moved0 = T.lk.stats.sent + T.lk.stats.delivered;
  trial_half(T, 0u);
  trial_half(T, 1u);
  const bool progressed = (T.lk.stats.sent + T.lk.stats.delivered) != moved0;
  if (!progressed) T.now += PROTO_RETX_MS;
  return progressed;
}

static void trial_run(Trial& T, uint32_t max_iters = 400000u)
{
  while (!(session_closed(T.e[0].s) && session_closed(T.e[1].s))) {
    if (T.iters++ >= max_iters) { T.hung = true; return; }
    (void)trial_step(T);
  }
}

// Runs until `st` is reached on endpoint `who`, or the session closes.
static bool trial_run_until_state(Trial& T, uint8_t who, SessionState want,
                                  uint32_t max_iters = 400000u)
{
  while (session_state(T.e[who].s) != want) {
    if (session_closed(T.e[who].s) || session_closed(T.e[who ^ 1u].s)) return false;
    if (T.iters++ >= max_iters) { T.hung = true; return false; }
    (void)trial_step(T);
  }
  return true;
}

// A DISCONNECT DROPS WHAT IS ALREADY IN THE AIR. Setting `dead` alone only
// stops new sends, so an endpoint would go on consuming a queue the peer can no
// longer add to - and would advance past the state the case meant to kill it in.
static void link_cut(Trial& T)
{
  T.lk.fault.dead = 1u;
  T.lk.inbox[0].count = 0u;
  T.lk.inbox[1].count = 0u;
}

static bool boxes_untouched(const Trial& T)
{
  return memcmp(T.e[0].box, T.e[0].box_before, sizeof T.e[0].box) == 0 &&
         memcmp(T.e[1].box, T.e[1].box_before, sizeof T.e[1].box) == 0;
}

static bool log_has(const LinkLog& l, LinkEventKind k)
{
  for (uint16_t i = 0; i < l.count; ++i) {
    const LinkEvent* e = link_log_at(l, i);
    if (e != nullptr && e->kind == (uint8_t)k) return true;
  }
  return false;
}

// The ordered list of states an endpoint entered, from its own event ring.
static uint8_t log_states(const LinkLog& l, uint8_t* out, uint8_t cap)
{
  uint8_t n = 0;
  for (uint16_t i = 0; i < l.count && n < cap; ++i) {
    const LinkEvent* e = link_log_at(l, i);
    if (e != nullptr && e->kind == (uint8_t)LEK_STATE) out[n++] = e->type;
  }
  return n;
}

// =============================================================================
//  IN-FLIGHT FORGERY - the modified peer, without a second protocol stack
// =============================================================================
static void put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)(v & 0xFFu); p[1] = (uint8_t)(v >> 8); }
static uint16_t get16(const uint8_t* p) { return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }
static void put32(uint8_t* p, uint32_t v)
{ p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }

static void reseal(uint8_t* f, uint16_t n) { put16(f + n - 2u, crc16_ccitt(f, (size_t)n - 2u)); }

static int in_flight(LoopbackLink& lk, uint8_t dst, uint8_t type)
{
  for (uint8_t i = 0; i < lk.inbox[dst].count; ++i)
    if (lk.inbox[dst].frame[i][1] == type) return (int)i;
  return -1;
}

// Runs poll rounds until a frame of `type` is waiting for `dst`, then hands it
// back for editing. Returns -1 if it never appears.
// The frame has to be caught BETWEEN the two endpoint polls: a sender and its
// receiver both run inside one trial_step(), so a frame that is sent and
// consumed in the same step is never visible from outside it.
static int wait_for_frame(Trial& T, uint8_t dst, uint8_t type, uint32_t max_iters = 200000u)
{
  for (;;) {
    int at = in_flight(T.lk, dst, type);
    if (at >= 0) return at;
    if (session_closed(T.e[0].s) && session_closed(T.e[1].s)) return -1;
    if (T.iters++ >= max_iters) { T.hung = true; return -1; }
    const uint32_t moved0 = T.lk.stats.sent + T.lk.stats.delivered;
    trial_half(T, 0u);
    at = in_flight(T.lk, dst, type);
    if (at >= 0) return at;
    trial_half(T, 1u);
    at = in_flight(T.lk, dst, type);
    if (at >= 0) return at;
    if ((T.lk.stats.sent + T.lk.stats.delivered) == moved0) T.now += PROTO_RETX_MS;
  }
}

static void inject(LoopbackLink& lk, uint8_t dst, const uint8_t* f, uint16_t n)
{
  LoopbackQueue& q = lk.inbox[dst];
  if (q.count >= (uint8_t)LB_QUEUE_CAP) return;
  memcpy(q.frame[q.count], f, n);
  q.len[q.count] = n;
  q.count++;
}

// =============================================================================
//  1. THE CLEAN RUN
// =============================================================================
static const LoopbackFault CLEAN = {};

TEST(two_endpoints_walk_the_whole_state_table_and_finish_one_battle)
{
  Trial& T = arena();
  trial_begin(T, 0xC0FFEEu, CLEAN);
  CHECK_EQ(trial_set_teams(T), VR_OK);
  session_start(T.e[0].s, T.now);
  session_start(T.e[1].s, T.now);
  trial_run(T);
  CHECK(!T.hung);

  // Both agreed, and the agreement is on all three things at once.
  CHECK_EQ(session_end(T.e[0].s).reason, SE_DONE);
  CHECK_EQ(session_end(T.e[1].s).reason, SE_DONE);
  CHECK(session_rewards_authorised(T.e[0].s));
  CHECK(session_rewards_authorised(T.e[1].s));
  CHECK_EQ(T.e[0].st.outcome, T.e[1].st.outcome);
  CHECK(T.e[0].st.outcome != (uint8_t)BO_UNDECIDED);
  CHECK(T.e[0].st.outcome != (uint8_t)BO_ABORT);
  CHECK_EQ(battle_state_hash(T.e[0].st), battle_state_hash(T.e[1].st));
  CHECK_EQ(memcmp(&T.e[0].st, &T.e[1].st, sizeof(BattleState)), 0);

  // A real battle happened rather than an immediate agreement about nothing.
  CHECK(T.e[0].st.round >= 2u);
  CHECK(T.e[0].blog.count > 20u);
  CHECK_EQ(T.e[0].blog.dropped, 0);

  // The roles came from the device ids and not from who spoke first.
  CHECK_EQ(T.e[0].s.role, SR_INITIATOR);
  CHECK_EQ(T.e[1].s.role, SR_RESPONDER);
  CHECK_EQ(T.e[0].s.side, 0);
  CHECK_EQ(T.e[1].s.side, 1);

  // A clean link shows no duplicate, no gap and NO LADDER RUNG AT ALL: if the
  // ordinary path needed a retransmission, every timing claim below it would be
  // measuring the wrong thing.
  for (uint8_t i = 0; i < 2u; ++i) {
    CHECK_EQ(session_end(T.e[i].s).rx_dup, 0);
    CHECK_EQ(session_end(T.e[i].s).rx_gap, 0);
    CHECK_EQ(session_end(T.e[i].s).tx_retx, 0);
    CHECK_EQ(session_end(T.e[i].s).rx_reject, 0);
  }
  CHECK(boxes_untouched(T));

  // THE WHOLE TABLE, IN ORDER, out of the endpoint's own event ring - which is
  // what makes "walks the state table" a checked claim and not a title.
  uint8_t path[16];
  const uint8_t n = log_states(T.e[0].llog, path, 16u);
  CHECK_EQ(n, 7);
  CHECK_EQ(path[0], SS_HELLO);
  CHECK_EQ(path[1], SS_CAPS);
  CHECK_EQ(path[2], SS_SESSION);
  CHECK_EQ(path[3], SS_TEAM);
  CHECK_EQ(path[4], SS_VERIFY);
  CHECK_EQ(path[5], SS_BATTLE);
  CHECK_EQ(path[6], SS_ENDING);
  CHECK_EQ(session_end(T.e[0].s).state, SS_ENDING);
}

TEST(only_one_endpoint_needs_to_speak_first)
{
  // The responder never calls session_start(): it answers from SS_IDLE, which
  // is the shape a real link screen has when one player presses first.
  Trial& T = arena();
  trial_begin(T, 0x5EED01u, CLEAN);
  CHECK_EQ(trial_set_teams(T), VR_OK);
  session_start(T.e[1].s, T.now);          // the RESPONDER speaks first
  trial_run(T);
  CHECK(!T.hung);
  CHECK_EQ(session_end(T.e[0].s).reason, SE_DONE);
  CHECK_EQ(session_end(T.e[1].s).reason, SE_DONE);
  CHECK_EQ(T.e[0].s.role, SR_INITIATOR);   // still by device id
  CHECK_EQ(T.e[1].s.role, SR_RESPONDER);
  CHECK_EQ(memcmp(&T.e[0].st, &T.e[1].st, sizeof(BattleState)), 0);
}

TEST(both_endpoints_derive_the_same_session_id_and_the_same_shared_seed)
{
  Trial& T = arena();
  trial_begin(T, 0x1234u, CLEAN);
  CHECK_EQ(trial_set_teams(T), VR_OK);
  session_start(T.e[0].s, T.now);
  session_start(T.e[1].s, T.now);
  CHECK(trial_run_until_state(T, 0u, SS_BATTLE));

  CHECK(T.e[0].s.id != 0u);
  CHECK_EQ(T.e[0].s.id, T.e[1].s.id);
  // Computed from the ordered pair and the INITIATOR's nonce, so both ends get
  // the same answer with no extra round trip.
  CHECK_EQ(T.e[0].s.id,
           session_derive_id(DEV_ID[0], DEV_ID[1], T.e[0].s.nonce_local));
  CHECK_EQ(T.e[0].setup.seed, T.e[1].setup.seed);
  CHECK_EQ(T.e[0].setup.seed,
           session_derive_seed(T.e[0].s.id, T.e[0].s.nonce_local, T.e[1].s.nonce_local,
                               T.e[0].s.team_crc_local, T.e[1].s.team_crc_local));
  // BOTH nonces AND BOTH team CRCs: neither side fixes the seed alone.
  CHECK(T.e[0].setup.seed !=
        session_derive_seed(T.e[0].s.id, T.e[0].s.nonce_local, T.e[1].s.nonce_local,
                            (uint16_t)(T.e[0].s.team_crc_local ^ 1u),
                            T.e[1].s.team_crc_local));
  CHECK(T.e[0].setup.seed !=
        session_derive_seed(T.e[0].s.id, T.e[0].s.nonce_local,
                            (uint32_t)(T.e[1].s.nonce_local ^ 1u),
                            T.e[0].s.team_crc_local, T.e[1].s.team_crc_local));
}

TEST(the_local_team_enters_the_engine_through_the_decoder_the_peer_s_team_uses)
{
  Trial& T = arena();
  trial_begin(T, 0x77u, CLEAN);
  // Give the local Pebbles a nickname, care and a battle history: none of it is
  // on the wire, so if the local team took a shortcut into the setup the two
  // endpoints would build DIFFERENT setups and this case would show it.
  for (uint8_t i = 0; i < 3u; ++i) {
    memcpy(T.e[0].box[i].nickname, "ROCA", 5);
    T.e[0].box[i].care[0]     = 90000;
    T.e[0].box[i].battles_won = 40u;
    T.e[0].box[i].age_s       = 123456u;
  }
  memcpy(T.e[0].box_before, T.e[0].box, sizeof T.e[0].box_before);
  CHECK_EQ(trial_set_teams(T), VR_OK);
  session_start(T.e[0].s, T.now);
  session_start(T.e[1].s, T.now);
  CHECK(trial_run_until_state(T, 0u, SS_BATTLE));

  // THE TWO SETUPS ARE BYTE-IDENTICAL. That is the property the lockstep rests
  // on, and it holds because the local team is encoded and decoded through
  // pbw_decode() exactly as the peer's is.
  CHECK_EQ(memcmp(&T.e[0].setup, &T.e[1].setup, sizeof(BattleSetup)), 0);
  CHECK_EQ(T.e[0].setup.member[0][0].nickname[0], 0);
  CHECK_EQ(T.e[0].setup.member[0][0].care[0], 0);
  CHECK_EQ(T.e[0].setup.member[0][0].battles_won, 0);
  CHECK_EQ(T.e[0].setup.member[0][0].age_s, 0);
  // and the fields that DO travel are intact
  CHECK_EQ(T.e[0].setup.member[0][0].species_id, T.e[0].box[0].species_id);
  CHECK_EQ(T.e[0].setup.member[0][0].level, T.e[0].box[0].level);
  CHECK_EQ(T.e[0].setup.member[0][0].id, T.e[0].box[0].id);
  CHECK(boxes_untouched(T));
}

// =============================================================================
//  2. LOSING THE PEER, IN EVERY STATE THAT CAN LOSE ONE
// =============================================================================
TEST(every_state_that_can_lose_its_peer_says_so_by_name_and_pays_nobody)
{
  // SS_IDLE is absent ON PURPOSE and the absence is the claim: an endpoint that
  // has not started has no session to lose and no ladder running, so it waits
  // rather than reporting a loss. SS_CLOSED is terminal.
  const SessionState WHERE[] = { SS_HELLO, SS_CAPS, SS_SESSION, SS_TEAM,
                                 SS_VERIFY, SS_BATTLE, SS_ENDING };
  for (uint8_t k = 0; k < sizeof WHERE / sizeof WHERE[0]; ++k) {
    Trial& T = arena();
    // SS_ENDING needs the peer's BATTLE_END killed as well: once BOTH final
    // hashes have been compared the session has AGREED, and a link that dies
    // after that is an SE_DONE with a missing farewell, not a loss. The row
    // below is about the endpoint that never got to agree.
    // TWO OF THE SEVEN STATES ARE TRANSIENT ON A CLEAN LINK and have to be held
    // open before they can be killed in, which is itself worth knowing:
    //   SS_VERIFY - the peer's BATTLE_STATE is usually already queued when we
    //               enter, so the endpoint passes through inside one poll;
    //   SS_ENDING - once BOTH final hashes have been compared the session has
    //               AGREED, and a link that dies after that is an SE_DONE with
    //               a missing farewell, not a loss.
    LoopbackFault f = {};
    if (WHERE[k] == SS_VERIFY) { f.block_type[1] = (uint8_t)PT_BATTLE_STATE; f.block_left[1] = 0xFFu; }
    if (WHERE[k] == SS_ENDING) { f.block_type[1] = (uint8_t)PT_BATTLE_END;   f.block_left[1] = 0xFFu; }
    trial_begin(T, 0x900u + k, f);
    CHECK_EQ(trial_set_teams(T), VR_OK);
    session_start(T.e[0].s, T.now);
    session_start(T.e[1].s, T.now);
    CHECK(trial_run_until_state(T, 0u, WHERE[k]));
    link_cut(T);                          // the peer walks out of range
    trial_run(T);
    CHECK(!T.hung);

    const SessionEnd& e = session_end(T.e[0].s);
    CHECK_EQ(e.reason, SE_LOST);
    CHECK_EQ(e.state, WHERE[k]);
    CHECK_EQ(e.tx_retx, PROTO_RETX_MAX);   // it climbed the WHOLE ladder
    CHECK(e.waiting_for != 0u);            // and it names what it was owed
    CHECK(!session_rewards_authorised(T.e[0].s));
    CHECK_EQ(e.peer_hash, 0);              // a loss is not a desync
    CHECK(boxes_untouched(T));
  }
}

TEST(a_lost_peer_is_never_reported_as_a_desync_and_a_desync_is_never_a_loss)
{
  // The pair of sentences the two reasons exist to keep apart, checked in one
  // place: a dead link says SE_LOST with no peer hash, and a forged hash says
  // SE_DESYNC with both hashes side by side.
  Trial& T = arena();
  trial_begin(T, 0xDEADu, CLEAN);
  CHECK_EQ(trial_set_teams(T), VR_OK);
  session_start(T.e[0].s, T.now);
  session_start(T.e[1].s, T.now);
  CHECK(trial_run_until_state(T, 0u, SS_BATTLE));
  link_cut(T);
  trial_run(T);
  CHECK_EQ(session_end(T.e[0].s).reason, SE_LOST);
  CHECK_EQ(session_end(T.e[1].s).reason, SE_LOST);
  // A loss carries NO peer hash on either side. The two local hashes may
  // legitimately differ - one endpoint can have resolved a round the other
  // could not - and reading that difference as a disagreement is exactly the
  // mistake the two reasons exist to prevent.
  CHECK_EQ(session_end(T.e[0].s).peer_hash, 0);
  CHECK_EQ(session_end(T.e[1].s).peer_hash, 0);
  CHECK(session_end(T.e[0].s).local_hash != 0u);
}

// =============================================================================
//  3. THE SEQUENCE WINDOW
// =============================================================================
TEST(a_duplicated_frame_is_dropped_by_the_window_and_changes_nothing)
{
  // Every frame delivered TWICE. The battle must come out bit-identical to the
  // clean run on the same seed, and the count of duplicates must equal the
  // count of accepted frames - a duplicate that reached the FSM would move that
  // equality and a window that dropped a first copy would move it the other way.
  Trial& T = arena();
  LoopbackFault f = {}; f.dup_permille = 1000u;
  trial_begin(T, 0xD0D0u, f);
  CHECK_EQ(trial_set_teams(T), VR_OK);
  session_start(T.e[0].s, T.now);
  session_start(T.e[1].s, T.now);
  trial_run(T);
  CHECK(!T.hung);
  const uint32_t hash_dup = battle_state_hash(T.e[0].st);
  const uint8_t  out_dup  = T.e[0].st.outcome;
  for (uint8_t i = 0; i < 2u; ++i) {
    CHECK_EQ(session_end(T.e[i].s).reason, SE_DONE);
    CHECK(session_end(T.e[i].s).rx_dup > 0);
    // ONE COPY ACCEPTED AND ONE REFUSED, for every frame but the last: the
    // frame that closes the session is never polled again, so its twin is
    // never counted. A window that let duplicates through would put rx_ok at
    // roughly twice rx_dup and fail both bounds.
    CHECK(session_end(T.e[i].s).rx_dup + 1u >= session_end(T.e[i].s).rx_ok);
    CHECK(session_end(T.e[i].s).rx_dup <= session_end(T.e[i].s).rx_ok);
    CHECK_EQ(session_end(T.e[i].s).tx_retx, 0);   // nothing was ever LOST
    CHECK(log_has(T.e[i].llog, LEK_DUP));
  }

  trial_begin(T, 0xD0D0u, CLEAN);
  CHECK_EQ(trial_set_teams(T), VR_OK);
  session_start(T.e[0].s, T.now);
  session_start(T.e[1].s, T.now);
  trial_run(T);
  CHECK_EQ(session_end(T.e[0].s).reason, SE_DONE);
  CHECK_EQ(battle_state_hash(T.e[0].st), hash_dup);
  CHECK_EQ(T.e[0].st.outcome, out_dup);
  CHECK_EQ(session_end(T.e[0].s).rx_dup, 0);
}

TEST(a_gap_is_named_on_a_lossy_link_and_a_clean_link_shows_none)
{
  // BOTH HALVES, because a gap counter that is always zero satisfies one of
  // them and a counter that always fires satisfies the other.
  Trial& T = arena();
  trial_begin(T, 0x6A9u, CLEAN);
  CHECK_EQ(trial_set_teams(T), VR_OK);
  session_start(T.e[0].s, T.now);
  session_start(T.e[1].s, T.now);
  trial_run(T);
  CHECK_EQ(session_end(T.e[0].s).rx_gap, 0);
  CHECK_EQ(session_end(T.e[1].s).rx_gap, 0);

  uint32_t with_gap = 0;
  for (uint32_t seed = 0; seed < 24u; ++seed) {
    LoopbackFault f = {}; f.drop_permille = 200u;
    trial_begin(T, 0x6A90000u + seed, f);
    CHECK_EQ(trial_set_teams(T), VR_OK);
    session_start(T.e[0].s, T.now);
    session_start(T.e[1].s, T.now);
    trial_run(T);
    if (session_end(T.e[0].s).rx_gap > 0 || session_end(T.e[1].s).rx_gap > 0) with_gap++;
  }
  CHECK(with_gap > 0);
}

TEST(a_sequence_number_no_honest_peer_could_reach_does_not_move_the_window)
{
  // THE HOLE THIS FILE FOUND. The window slides forward to whatever it is
  // shown, so one injected frame carrying a seq thousands ahead pushed it past
  // every number the honest peer would ever send, and every real frame after
  // that was dropped as stale - a permanent deafness bought with a single
  // forged packet, on a protocol whose session id travels in clear.
  Trial& T = arena();
  trial_begin(T, 0x5A2Fu, CLEAN);
  CHECK_EQ(trial_set_teams(T), VR_OK);
  session_start(T.e[0].s, T.now);
  session_start(T.e[1].s, T.now);
  CHECK(trial_run_until_state(T, 1u, SS_BATTLE));

  ProtoMsg m;
  proto_msg_init(m, PT_BATTLE_STATE, T.e[1].s.id, 0x7000u);
  m.round = 1u;
  m.p.bstate.open_hash = T.e[1].s.open_hash;
  uint8_t buf[PROTO_FRAME_MAX]; size_t n = 0;
  CHECK_EQ(proto_encode(m, buf, sizeof buf, n), PE_OK);
  const uint16_t last_before = T.e[1].s.rx_seq_last;
  inject(T.lk, 1u, buf, (uint16_t)n);
  session_poll(T.e[1].s, T.now);
  CHECK_EQ(T.e[1].s.rx_seq_last, last_before);      // THE WINDOW DID NOT MOVE
  CHECK(session_end(T.e[1].s).rx_stale > 0);

  // and the battle finishes exactly as it would have without the injection
  trial_run(T);
  CHECK(!T.hung);
  CHECK_EQ(session_end(T.e[0].s).reason, SE_DONE);
  CHECK_EQ(session_end(T.e[1].s).reason, SE_DONE);
}

TEST(the_very_first_sequence_number_is_bounded_too_and_not_only_the_jumps_after_it)
{
  // THE HALF THE RULE ABOVE DID NOT COVER. SESSION_SEQ_MAX_JUMP bounded a jump
  // from an ESTABLISHED last, but the FIRST frame an endpoint accepted set that
  // last to whatever it said, with no bound at all - so the same one-packet
  // deafness was still available BEFORE the peer had spoken. HELLO is the frame
  // to do it with: it carries session 0 by definition, so an off-path device
  // needs no session id at all, and the id travels in clear anyway.
  //
  // MEASURED before the bound: e0 took rx_seq_last = 28672 from the injected
  // HELLO and BOTH endpoints ended SE_LOST on a link that dropped nothing.
  Trial& T = arena();
  trial_begin(T, 0x5A30u, CLEAN);
  CHECK_EQ(trial_set_teams(T), VR_OK);

  ProtoMsg m;
  proto_msg_init(m, PT_HELLO, 0u, 0x7000u);
  m.p.hello.device_id   = 0x0BADBADu;          // a device that is not the peer
  m.p.hello.hello_nonce = 0x12345678u;
  uint8_t buf[PROTO_FRAME_MAX]; size_t n = 0;
  CHECK_EQ(proto_encode(m, buf, sizeof buf, n), PE_OK);
  inject(T.lk, 0u, buf, (uint16_t)n);          // BEFORE either endpoint speaks
  session_poll(T.e[0].s, T.now);               // the ONLY frame it has ever seen

  // It was DELIVERED and REFUSED - not absent, which would make the rest
  // vacuous - and the window is still where session_init() left it.
  CHECK_EQ(session_end(T.e[0].s).rx_stale, 1);
  CHECK_EQ(session_end(T.e[0].s).rx_ok, 0);    // it never reached the FSM
  CHECK_EQ(T.e[0].s.rx_seq_last, 0);
  CHECK_EQ(T.e[0].s.rx_seq_seen, 0);
  CHECK_EQ(T.e[0].s.peer_device_id, 0);        // no phantom peer was bound

  session_start(T.e[0].s, T.now);
  session_start(T.e[1].s, T.now);
  trial_run(T);
  CHECK(!T.hung);
  CHECK_EQ(T.e[0].s.peer_device_id, DEV_ID[1]);
  // And the honest battle happens exactly as it would have without it.
  CHECK_EQ(session_end(T.e[0].s).reason, SE_DONE);
  CHECK_EQ(session_end(T.e[1].s).reason, SE_DONE);
  CHECK_EQ(memcmp(&T.e[0].st, &T.e[1].st, sizeof(BattleState)), 0);
}

TEST(a_frame_from_far_below_the_window_is_stale_and_not_a_duplicate)
{
  // Reordering is what produces one, and the window has to tell it from a
  // repeat: a stale frame is dropped as stale, a repeat is dropped as a
  // duplicate, and the two counters are separate.
  Trial& T = arena();
  LoopbackFault f = {}; f.reorder_permille = 500u; f.reorder_window = 8u;
  trial_begin(T, 0x5A1Eu, f);
  CHECK_EQ(trial_set_teams(T), VR_OK);
  session_start(T.e[0].s, T.now);
  session_start(T.e[1].s, T.now);
  CHECK(trial_run_until_state(T, 1u, SS_BATTLE));
  // Run on until the window has moved more than its own width, or "far below"
  // is not far below anything: a seq inside the window that was already seen is
  // a DUPLICATE, which is a different answer and a different counter.
  while (T.e[1].s.rx_seq_last < 40u && !session_closed(T.e[1].s)) (void)trial_step(T);
  CHECK(T.e[1].s.rx_seq_last >= 40u);
  // Cut the link and drain, so the injected frame is the ONLY thing the next
  // poll sees and the two counters below mean what they say.
  T.lk.fault.dead = 1u;                    // drained deliberately, not cut
  for (uint8_t g = 0; g < 32u && loopback_pending(T.lk, 1u) > 0u; ++g)
    session_poll(T.e[1].s, T.now);
  CHECK_EQ(loopback_pending(T.lk, 1u), 0);

  // A frame the peer sent long ago, replayed now: seq 1 is far below the window.
  ProtoMsg m;
  proto_msg_init(m, PT_ACTION, T.e[0].s.id, 1u);
  m.round = 1u;
  m.p.action.kind = 1u; m.p.action.index = 0u; m.p.action.open_hash = 0u;
  uint8_t buf[PROTO_FRAME_MAX]; size_t n = 0;
  CHECK_EQ(proto_encode(m, buf, sizeof buf, n), PE_OK);
  const uint16_t stale_before = session_end(T.e[1].s).rx_stale;
  const uint16_t ok_before    = session_end(T.e[1].s).rx_ok;
  inject(T.lk, 1u, buf, (uint16_t)n);
  session_poll(T.e[1].s, T.now);
  CHECK_EQ(session_end(T.e[1].s).rx_stale, stale_before + 1);
  CHECK_EQ(session_end(T.e[1].s).rx_ok, ok_before);     // it never reached the FSM
  CHECK(!session_closed(T.e[1].s));
}

// =============================================================================
//  4. THE LOCKSTEP HASHES
// =============================================================================
static void run_to_battle(Trial& T, uint32_t seed)
{
  trial_begin(T, seed, CLEAN);
  CHECK_EQ(trial_set_teams(T), VR_OK);
  session_start(T.e[0].s, T.now);
  session_start(T.e[1].s, T.now);
  CHECK(trial_run_until_state(T, 0u, SS_BATTLE));
}

TEST(a_forged_round_hash_ends_the_battle_by_name_and_pays_nobody)
{
  // hash_after forged: we agreed on the inputs and disagree on the RULES.
  Trial& T = arena();
  run_to_battle(T, 0xF0111u);
  const int at = wait_for_frame(T, 1u, (uint8_t)PT_ROUND_RESULT);
  CHECK(at >= 0);
  if (at >= 0) {
    uint8_t* f = T.lk.inbox[1].frame[at];
    const uint16_t n = T.lk.inbox[1].len[at];
    put32(f + PROTO_HDR_BYTES + 4, get16(f + PROTO_HDR_BYTES + 4) ^ 0xA5A5u);
    reseal(f, n);
  }
  trial_run(T);
  CHECK(!T.hung);
  CHECK_EQ(session_end(T.e[1].s).reason, SE_DESYNC);
  CHECK_EQ(session_end(T.e[1].s).detail, SD_RULES);
  CHECK(session_end(T.e[1].s).peer_hash != session_end(T.e[1].s).local_hash);
  CHECK_EQ(session_end(T.e[0].s).reason, SE_DESYNC);   // told, not left to time out
  CHECK(!session_rewards_authorised(T.e[0].s));
  CHECK(!session_rewards_authorised(T.e[1].s));
  CHECK(boxes_untouched(T));
}

TEST(a_forged_input_hash_names_the_inputs_and_not_the_rules)
{
  // hash_before forged: we disagree about WHICH ACTIONS were played, which is a
  // different sentence, and the two must not collapse into one code.
  Trial& T = arena();
  run_to_battle(T, 0xF0222u);
  const int at = wait_for_frame(T, 1u, (uint8_t)PT_ROUND_RESULT);
  CHECK(at >= 0);
  if (at >= 0) {
    uint8_t* f = T.lk.inbox[1].frame[at];
    const uint16_t n = T.lk.inbox[1].len[at];
    put32(f + PROTO_HDR_BYTES + 0, get16(f + PROTO_HDR_BYTES + 0) ^ 0x5A5Au);
    reseal(f, n);
  }
  trial_run(T);
  CHECK_EQ(session_end(T.e[1].s).reason, SE_DESYNC);
  CHECK_EQ(session_end(T.e[1].s).detail, SD_INPUTS);
  CHECK(!session_rewards_authorised(T.e[1].s));
}

TEST(an_action_that_does_not_match_our_open_state_is_refused_before_it_is_submitted)
{
  Trial& T = arena();
  run_to_battle(T, 0xF0333u);
  const int at = wait_for_frame(T, 1u, (uint8_t)PT_ACTION);
  CHECK(at >= 0);
  if (at >= 0) {
    uint8_t* f = T.lk.inbox[1].frame[at];
    const uint16_t n = T.lk.inbox[1].len[at];
    put32(f + PROTO_HDR_BYTES + 4, 0xDEADBEEFu);      // the open hash
    reseal(f, n);
  }
  const uint8_t before_kind = T.e[1].st.side[0].pending_kind;
  trial_run(T);
  CHECK_EQ(session_end(T.e[1].s).reason, SE_DESYNC);
  CHECK_EQ(session_end(T.e[1].s).detail, SD_OPEN_HASH);
  CHECK_EQ(session_end(T.e[1].s).peer_hash, 0xDEADBEEF);
  // NOTHING WAS SUBMITTED: the engine never saw the action.
  CHECK_EQ(T.e[1].st.side[0].pending_kind, before_kind);
  CHECK(!session_rewards_authorised(T.e[1].s));
}

TEST(an_injected_state_that_disagrees_is_a_named_desync_and_never_a_silent_one)
{
  // The session id travels in clear, so an off-path device that has read one
  // frame can forge well-formed ones. THIS IS WHAT ITS POWER IS BOUNDED TO: it
  // can burn the battle by name, and it cannot move our state - a BATTLE_STATE
  // carries a digest we compare against our own and nothing we adopt.
  Trial& T = arena();
  run_to_battle(T, 0xF0555u);
  const uint32_t before = battle_state_hash(T.e[1].st);
  ProtoMsg m;
  proto_msg_init(m, PT_BATTLE_STATE, T.e[1].s.id, (uint16_t)(T.e[1].s.rx_seq_last + 8u));
  m.round = (uint8_t)T.e[1].st.round;
  m.p.bstate.open_hash = 0x1234ABCDu;
  uint8_t buf[PROTO_FRAME_MAX]; size_t n = 0;
  CHECK_EQ(proto_encode(m, buf, sizeof buf, n), PE_OK);
  inject(T.lk, 1u, buf, (uint16_t)n);
  session_poll(T.e[1].s, T.now);
  CHECK_EQ(session_end(T.e[1].s).reason, SE_DESYNC);
  CHECK_EQ(session_end(T.e[1].s).detail, SD_PROBE);
  CHECK_EQ(session_end(T.e[1].s).peer_hash, 0x1234ABCD);
  CHECK_EQ(battle_state_hash(T.e[1].st), before);   // NOT ONE BYTE MOVED
  CHECK(!session_rewards_authorised(T.e[1].s));
  CHECK(boxes_untouched(T));
}

TEST(a_second_action_for_one_round_with_other_bytes_is_named_and_stopped)
{
  Trial& T = arena();
  run_to_battle(T, 0xF0444u);
  const int at = wait_for_frame(T, 1u, (uint8_t)PT_ACTION);
  CHECK(at >= 0);
  uint8_t copy[PROTO_FRAME_MAX]; uint16_t n = 0;
  if (at >= 0) {
    n = T.lk.inbox[1].len[at];
    memcpy(copy, T.lk.inbox[1].frame[at], n);
  }
  // session_poll() WITHOUT the driver's own submission, so the receiving side
  // still owes an action and the round cannot resolve between the honest frame
  // and the forged one. Otherwise the second ACTION would arrive a round late
  // and be answered as a retransmission - a different rule entirely.
  session_poll(T.e[1].s, T.now);
  CHECK(T.e[1].st.side[0].pending_kind != BACT_NONE);
  CHECK_EQ(T.e[1].st.side[1].pending_kind, BACT_NONE);
  if (n != 0u) {
    copy[PROTO_HDR_BYTES + 1] = (uint8_t)(copy[PROTO_HDR_BYTES + 1] ^ 1u);  // other index
    // A seq just above what the window has seen. A far-future one is REFUSED
    // without moving the window - see a_sequence_number_no_honest_peer_could_
    // reach_does_not_move_the_window, which is a hole this file found.
    put16(copy + 8, (uint16_t)(T.e[1].s.rx_seq_last + 1u));
    reseal(copy, n);
    inject(T.lk, 1u, copy, n);
  }
  session_poll(T.e[1].s, T.now);
  CHECK_EQ(session_end(T.e[1].s).reason, SE_PROTOCOL);
  CHECK_EQ(session_end(T.e[1].s).detail, SD_ACTION_CHANGED);
  CHECK(!session_rewards_authorised(T.e[1].s));
}

TEST(a_retransmitted_action_for_a_resolved_round_is_answered_and_not_submitted)
{
  // THE RULE THAT KEEPS AN HONEST PAIR ALIVE. One ROUND_RESULT is killed, so a
  // peer sits with its agreement barrier up and re-drives the round we already
  // finished. Without the answer-from-state rule both ladders expire on a link
  // that lost exactly one frame.
  Trial& T = arena();
  LoopbackFault f = {};
  f.block_type[0] = (uint8_t)PT_ROUND_RESULT;
  f.block_left[0] = 1u;                        // exactly ONE, then the link is clean
  trial_begin(T, 0x1EACu, f);
  CHECK_EQ(trial_set_teams(T), VR_OK);
  session_start(T.e[0].s, T.now);
  session_start(T.e[1].s, T.now);
  trial_run(T);
  CHECK(!T.hung);
  CHECK_EQ(T.lk.stats.blocked, 1);
  CHECK_EQ(session_end(T.e[0].s).reason, SE_DONE);
  CHECK_EQ(session_end(T.e[1].s).reason, SE_DONE);
  CHECK_EQ(memcmp(&T.e[0].st, &T.e[1].st, sizeof(BattleState)), 0);
  CHECK(log_has(T.e[0].llog, LEK_REACK));      // it was ANSWERED from state
  CHECK(session_end(T.e[0].s).rx_stale > 0);
}

// Replays ONE legal, in-state frame into a cut link, one per ladder rung, and
// answers the only question that matters about R4: does the ladder still climb?
// `make` fills the message; the seq is always one past what the endpoint has
// accepted, so the window calls every replay new and the FSM really sees it.
static uint32_t replay_until_closed(Trial& T, uint8_t who,
                                    void (*make)(Trial&, uint8_t, ProtoMsg&),
                                    uint32_t cap)
{
  uint8_t buf[PROTO_FRAME_MAX]; size_t n = 0;
  uint32_t poured = 0;
  while (!session_closed(T.e[who].s) && poured < cap) {
    ProtoMsg m;
    make(T, who, m);
    m.seq = (uint16_t)(T.e[who].s.rx_seq_last + 1u);
    if (proto_encode(m, buf, sizeof buf, n) != PE_OK) break;
    inject(T.lk, who, buf, (uint16_t)n);
    poured++;
    session_poll(T.e[who].s, T.now);
    T.now += PROTO_RETX_MS;                  // the attacker chooses the spacing
  }
  return poured;
}

static void mk_replayed_action_result(Trial& T, uint8_t who, ProtoMsg& m)
{
  proto_msg_init(m, PT_ACTION_RESULT, T.e[who].s.id, 0u);
  m.round             = T.e[who].s.my_act_round;
  m.p.ares.reject     = (uint8_t)BR_OK;
  m.p.ares.kind_echo  = T.e[who].s.my_act_kind;
  m.p.ares.index_echo = T.e[who].s.my_act_index;
  m.p.ares.open_hash  = T.e[who].s.my_act_hash;
}

static void mk_replayed_battle_state(Trial& T, uint8_t who, ProtoMsg& m)
{
  proto_msg_init(m, PT_BATTLE_STATE, T.e[who].s.id, 0u);
  m.round              = (uint8_t)T.e[who].st.round;
  m.p.bstate.open_hash = T.e[who].s.open_hash;
  m.p.bstate.phase     = T.e[who].st.phase;
  m.p.bstate.outcome   = T.e[who].st.outcome;
}

TEST(one_legal_frame_replayed_forever_cannot_hold_the_session_open)
{
  // R4 SAYS ONLY PROGRESS TOUCHES THE LADDER, and docs/protocol.md promises a
  // peer that stalls gets nine attempts and then SE_LOST. BOTH WERE FALSE FOR
  // ONE TYPE: on_action_result() called session_progress() for EVERY
  // ACTION_RESULT whose round matched our outstanding ACTION, including the
  // second and every one after - and a re-acknowledged ACTION_RESULT is exactly
  // what an honest peer regenerates from state. MEASURED before the guard: one
  // frame replayed at a spacing the ATTACKER chooses held the session open for
  // over a thousand injections and seventeen hours of virtual time, ending
  // SE_PROTOCOL / SD_RX_BUDGET with tx_retx 0 - not the SE_LOST with
  // tx_retx == 9 that SessionEnd's own documented signature relies on.
  //
  // WHY THE EXISTING FLOOD CASE COULD NOT SEE IT: junk_never_reaches_... injects
  // PT_ACTION_RESULT from SS_TEAM, where it is a wrong-state frame that never
  // reaches this handler. Right type, wrong state, branch never entered.
  struct Arm { void (*make)(Trial&, uint8_t, ProtoMsg&); const char* what; };
  const Arm ARMS[] = {
    { &mk_replayed_action_result, "ACTION_RESULT" },
    { &mk_replayed_battle_state,  "BATTLE_STATE"  },   // the control: always obeyed R4
  };
  for (uint8_t k = 0; k < sizeof ARMS / sizeof ARMS[0]; ++k) {
    Trial& T = arena();
    trial_begin(T, 0x5300u + k, CLEAN);
    CHECK_EQ(trial_set_teams(T), VR_OK);
    session_start(T.e[0].s, T.now);
    session_start(T.e[1].s, T.now);
    CHECK(trial_run_until_state(T, 0u, SS_BATTLE));
    // Run on until endpoint 0 has an ACTION outstanding, so the replayed frame
    // is IN STATE and reaches the handler under test.
    while (T.e[0].s.my_act_round == 0u && !session_closed(T.e[0].s)) (void)trial_step(T);
    CHECK(T.e[0].s.my_act_round != 0u);
    link_cut(T);

    const uint32_t poured = replay_until_closed(T, 0u, ARMS[k].make, 200u);
    const SessionEnd& e = session_end(T.e[0].s);
    if (e.reason != (uint8_t)SE_LOST || e.tx_retx != (uint8_t)PROTO_RETX_MAX)
      fprintf(stderr, "  replaying %s: %u injections, reason=%s detail=%s tx_retx=%u\n",
              ARMS[k].what, (unsigned)poured,
              session_reason_name((SessionEndReason)e.reason),
              session_detail_name((SessionDetail)e.detail),
              (unsigned)e.tx_retx);
    CHECK_EQ(e.reason, SE_LOST);
    CHECK_EQ(e.tx_retx, PROTO_RETX_MAX);
    // ON EXACTLY ITS OWN SCHEDULE. One rung per injection plus what was already
    // on the clock; anything more means the replay bought the peer time.
    CHECK(poured <= (uint32_t)PROTO_RETX_MAX + 2u);
    CHECK(!session_rewards_authorised(T.e[0].s));
    CHECK(boxes_untouched(T));
  }
}

TEST(losing_one_frame_of_every_type_in_turn_still_finishes_the_battle)
{
  // The re-ack rules are not one rule: each message type has its own way back,
  // and a single loss of ANY of them has to heal. Six types, each killed once.
  const uint8_t TYPES[] = { (uint8_t)PT_CAPABILITIES, (uint8_t)PT_SESSION_ACCEPT,
                            (uint8_t)PT_TEAM_SUBMIT,  (uint8_t)PT_TEAM_VALIDATION,
                            (uint8_t)PT_ACTION,       (uint8_t)PT_ACTION_RESULT };
  for (uint8_t k = 0; k < sizeof TYPES / sizeof TYPES[0]; ++k) {
    Trial& T = arena();
    LoopbackFault f = {};
    f.block_type[k & 1u] = TYPES[k];
    f.block_left[k & 1u] = 1u;
    trial_begin(T, 0x2000u + k, f);
    CHECK_EQ(trial_set_teams(T), VR_OK);
    session_start(T.e[0].s, T.now);
    session_start(T.e[1].s, T.now);
    trial_run(T);
    CHECK(!T.hung);
    CHECK_EQ(T.lk.stats.blocked, 1);
    CHECK_EQ(session_end(T.e[0].s).reason, SE_DONE);
    CHECK_EQ(session_end(T.e[1].s).reason, SE_DONE);
    CHECK_EQ(memcmp(&T.e[0].st, &T.e[1].st, sizeof(BattleState)), 0);
  }
}

// =============================================================================
//  5. THE VALIDATOR ON BOTH SIDES
// =============================================================================
// A MODIFIED PEER, PLAYED ON THE WIRE AND NOT IN THE PEER'S OWN STORE. The
// TEAM_SUBMIT frame in flight has one 48 B record replaced by a poisoned one,
// both seals recomputed. Editing the sender's own my_rec[] instead would model
// a device whose OWN records are broken - which fails on its own load path and
// never exercises the receiving validator at all.
static bool poison_in_flight(Trial& T, uint8_t dst, uint8_t slot,
                             void (*edit)(PebbleInstance&))
{
  const int at = wait_for_frame(T, dst, (uint8_t)PT_TEAM_SUBMIT);
  if (at < 0) return false;
  uint8_t* f = T.lk.inbox[dst].frame[at];
  const uint16_t n = T.lk.inbox[dst].len[at];
  PebbleInstance p = T.e[dst ^ 1u].box[slot];
  edit(p);
  uint8_t rec[PBW_BYTES];
  pbw_encode(p, rec);
  memcpy(f + PROTO_HDR_BYTES + 4 + (size_t)slot * PBW_BYTES, rec, PBW_BYTES);
  // team_crc is left AS THE SENDER WROTE IT: a modified peer lies consistently,
  // and TEAM_VALIDATION echoes that number back so the sender can tell whose
  // team the verdict is about. Rewriting it here would make the honest reply
  // unrecognisable to the peer and the case would stop testing the reply at all.
  reseal(f, n);
  return true;
}

static void ed_level31(PebbleInstance& p)   { p.level = 31u; }
static void ed_species(PebbleInstance& p)   { p.species_id = 99u; }
static void ed_three_moves(PebbleInstance& p){ p.moves[3] = 0u; }
static void ed_stolen_move(PebbleInstance& p){ p.moves[1] = 11u; }
static void ed_hp_over(PebbleInstance& p)   { p.hp_cur = (uint16_t)(p.hp_cur + 1u); }
static void ed_genome_crc(PebbleInstance& p){ p.genome.crc16 = (uint16_t)(p.genome.crc16 ^ 1u); }
// The one rule NO per-member check can own: two members of one team sharing an
// id. pbw_decode() runs validate_pebble() on each record and would accept both,
// so this is the vector that makes deleting validate_team() fail a named case
// rather than passing on the strength of the decoder underneath it.
static void ed_same_id(PebbleInstance& p)   { p.id = 0x200u; }
// THE LAST THING THREE CHECKERS ACCEPTED AND THE ENGINE REFUSED. hp_cur == 0 is
// a legal STORED state, so validate_pebble(), validate_team() and pbw_decode()
// all answer VR_OK for it; battle_init() answers BR_MEMBER_FAINTED. Before
// validate_battle_ready() this vector closed the honest endpoint SE_PROTOCOL /
// SD_INTERNAL with bad_index 0xFF - measured - which is this device recording a
// bug in its own validator for a lie the peer told. PBS_FAINTED cannot make the
// trip (VR_WIRE_STATUS_BITS refuses it), so hp_cur is the only way through.
static void ed_fainted(PebbleInstance& p)   { p.hp_cur = 0u; }

struct PoisonCase { void (*edit)(PebbleInstance&); VReject want; const char* what; };

TEST(a_team_the_peer_could_not_have_raised_is_refused_by_the_right_code)
{
  const PoisonCase CASES[] = {
    { &ed_level31,     VR_BAD_LEVEL,            "level 31" },
    { &ed_species,     VR_UNKNOWN_SPECIES,      "a species with no row" },
    { &ed_three_moves, VR_UNKNOWN_MOVE,         "three moves" },
    { &ed_stolen_move, VR_UNLEARNABLE_MOVESET,  "a move it cannot learn" },
    { &ed_hp_over,     VR_HP_OVER_MAX,          "hp above the derived max" },
    { &ed_genome_crc,  VR_BAD_GENOME,           "a broken genome seal" },
    { &ed_same_id,     VR_DUPLICATE_ID,         "one Pebble sent twice" },
    { &ed_fainted,     VR_MEMBER_FAINTED,       "a member that has fainted" },
  };
  for (uint8_t k = 0; k < sizeof CASES / sizeof CASES[0]; ++k) {
    Trial& T = arena();
    trial_begin(T, 0x3000u + k, CLEAN);
    CHECK_EQ(trial_set_teams(T), VR_OK);
    session_start(T.e[0].s, T.now);
    session_start(T.e[1].s, T.now);
    // THE SECOND member, so a check that stopped at the first would miss it.
    CHECK(poison_in_flight(T, 0u, 1u, CASES[k].edit));
    trial_run(T);
    CHECK(!T.hung);
    CHECK_EQ(session_end(T.e[0].s).reason, SE_REJECTED);
    CHECK_EQ(session_end(T.e[0].s).detail, CASES[k].want);
    CHECK_EQ(session_end(T.e[0].s).bad_index, 1);
    // NO BATTLE EVER EXISTED on either side.
    CHECK_EQ(T.e[0].st.phase, BP_INIT);
    CHECK_EQ(T.e[1].st.phase, BP_INIT);
    CHECK(!session_rewards_authorised(T.e[0].s));
    CHECK(!session_rewards_authorised(T.e[1].s));
    // AND THE PEER IS TOLD WHY, by the same code, rather than being left to
    // time out: TEAM_VALIDATION carries the verdict.
    CHECK_EQ(session_end(T.e[1].s).reason, SE_REJECTED);
    CHECK_EQ(session_end(T.e[1].s).detail, CASES[k].want);
    CHECK(boxes_untouched(T));
  }
}

TEST(a_peer_that_echoes_our_own_pebble_ids_is_refused_as_a_duplicate)
{
  // WITHOUT THIS RULE THE DEVICE BLAMES ITSELF. battle_init() refuses
  // BR_DUPLICATE_ID across ALL SIX members, so a peer that copies one of our
  // ids reaches the engine and comes back as an internal fault - this device
  // reporting a bug in game/validate.cpp for a lie the peer told.
  Trial& T = arena();
  trial_begin(T, 0x3100u, CLEAN);
  T.e[1].box[2].id = T.e[0].box[0].id;
  memcpy(T.e[1].box_before, T.e[1].box, sizeof T.e[1].box_before);
  CHECK_EQ(trial_set_teams(T), VR_OK);          // legal WITHIN each team
  session_start(T.e[0].s, T.now);
  session_start(T.e[1].s, T.now);
  trial_run(T);
  CHECK(!T.hung);
  CHECK_EQ(session_end(T.e[0].s).reason, SE_REJECTED);
  CHECK_EQ(session_end(T.e[0].s).detail, VR_DUPLICATE_ID);
  CHECK(session_end(T.e[0].s).detail != SD_INTERNAL);
  CHECK_EQ(T.e[0].st.phase, BP_INIT);
}

TEST(a_fainted_member_is_the_peer_s_lie_and_our_own_teams_refusal_never_an_internal_bug)
{
  // THE ARM ABOVE proves the peer is answered by name. This case proves the two
  // things that arm cannot: that the honest device NEVER records SD_INTERNAL for
  // it, and that OUR OWN team is refused before a single frame goes out - which
  // is the half with no adversary in it at all. Both endpoints used to close
  // SE_PROTOCOL / SD_INTERNAL on a team nobody had tampered with.
  Trial& T = arena();

  // (a) THE PEER'S LIE. Same vector as the table above, asserted against the
  //     code it used to produce rather than only against the one it should.
  trial_begin(T, 0x3200u, CLEAN);
  CHECK_EQ(trial_set_teams(T), VR_OK);
  session_start(T.e[0].s, T.now);
  session_start(T.e[1].s, T.now);
  CHECK(poison_in_flight(T, 0u, 1u, &ed_fainted));
  trial_run(T);
  CHECK(!T.hung);
  CHECK_EQ(session_end(T.e[0].s).reason, SE_REJECTED);
  CHECK_EQ(session_end(T.e[0].s).detail, VR_MEMBER_FAINTED);
  CHECK_EQ(session_end(T.e[0].s).bad_index, 1);          // WHICH member, not 0xFF
  CHECK(session_end(T.e[0].s).reason != SE_PROTOCOL);
  CHECK(session_end(T.e[0].s).detail != SD_INTERNAL);
  CHECK_EQ(T.e[0].st.phase, BP_INIT);                    // the engine never ran
  CHECK(boxes_untouched(T));

  // (b) OUR OWN TEAM, WITH NOBODY LYING. The refusal is at session_set_team(),
  //     so the session is never even started - and the code names the member.
  trial_begin(T, 0x3201u, CLEAN);
  T.e[0].box[2].hp_cur = 0u;
  memcpy(T.e[0].box_before, T.e[0].box, sizeof T.e[0].box_before);
  uint8_t bad = 0xFFu;
  CHECK_EQ(session_set_team(T.e[0].s, T.e[0].box, T.e[0].count, bad), VR_MEMBER_FAINTED);
  CHECK_EQ(bad, 2);
  CHECK_EQ(T.e[0].s.my_count, 0);          // nothing was frozen for the wire
  CHECK_EQ(session_state(T.e[0].s), SS_IDLE);

  // (c) THE POSITIVE CONTROL, IN THE SAME FUNCTION: PBS_FAINTED on a full-hp
  //     Pebble is the other half of the engine's own rule, and healing the hp
  //     alone must not make the team eligible.
  T.e[0].box[2].hp_cur = T.e[0].box[0].hp_cur;
  T.e[0].box[2].status = (uint8_t)PBS_FAINTED;
  CHECK_EQ(session_set_team(T.e[0].s, T.e[0].box, T.e[0].count, bad), VR_MEMBER_FAINTED);
  CHECK_EQ(bad, 2);
  T.e[0].box[2].status = 0u;
  CHECK_EQ(session_set_team(T.e[0].s, T.e[0].box, T.e[0].count, bad), VR_OK);
  CHECK_EQ(bad, 0xFF);
  CHECK_EQ(T.e[0].s.my_count, 3);
}

TEST(the_agreed_level_band_is_refused_before_a_team_is_ever_sent)
{
  Trial& T = arena();
  trial_begin(T, 0x3200u, CLEAN);
  // The responder will only play 1..8; the initiator asks for 1..30.
  T.e[1].s.lvl_lo = 1u; T.e[1].s.lvl_hi = 8u;
  CHECK_EQ(trial_set_teams(T), VR_OK);
  session_start(T.e[0].s, T.now);
  session_start(T.e[1].s, T.now);
  trial_run(T);
  CHECK(!T.hung);
  CHECK_EQ(session_end(T.e[1].s).reason, SE_REJECTED);
  CHECK_EQ(session_end(T.e[1].s).detail, SD_BAND);
  CHECK_EQ(session_end(T.e[0].s).reason, SE_REJECTED);
  CHECK_EQ(session_end(T.e[0].s).detail, VR_LEVEL_OUT_OF_BAND);
  CHECK_EQ(session_end(T.e[0].s).state, SS_SESSION);   // before any TEAM_SUBMIT
  CHECK_EQ(T.e[0].s.team_seen, 0);
}

TEST(a_level_outside_an_agreed_band_is_refused_even_when_the_pebble_is_legal)
{
  // The band accepted the SESSION, and the team is still out of it: the rule
  // has to fire on the members and not only on the request.
  Trial& T = arena();
  trial_begin(T, 0x3300u, CLEAN);
  mk_team(T.e[1].box, 13u, 17u, 21u, 25u, 0x200u);     // level 25, all legal
  memcpy(T.e[1].box_before, T.e[1].box, sizeof T.e[1].box_before);
  T.e[0].s.lvl_lo = 1u; T.e[0].s.lvl_hi = 12u;
  T.e[1].s.lvl_lo = 1u; T.e[1].s.lvl_hi = 12u;
  uint8_t bad = 0xFFu;
  CHECK_EQ(session_set_team(T.e[0].s, T.e[0].box, T.e[0].count, bad), VR_OK);
  CHECK_EQ(session_set_team(T.e[1].s, T.e[1].box, T.e[1].count, bad), VR_OK);
  session_start(T.e[0].s, T.now);
  session_start(T.e[1].s, T.now);
  trial_run(T);
  CHECK_EQ(session_end(T.e[0].s).reason, SE_REJECTED);
  CHECK_EQ(session_end(T.e[0].s).detail, VR_LEVEL_OUT_OF_BAND);
  CHECK_EQ(T.e[0].st.phase, BP_INIT);
}

TEST(a_round_one_hash_that_does_not_match_is_a_setup_desync_and_no_round_runs)
{
  // The agreement barrier before a single action has been chosen. Two engines
  // that disagree HERE are playing different games - a content pack CAPABILITIES
  // could not name, a padding difference, a big-endian build - and the battle
  // must not start rather than start and diverge on round one.
  Trial& T = arena();
  trial_begin(T, 0x3400u, CLEAN);
  CHECK_EQ(trial_set_teams(T), VR_OK);
  session_start(T.e[0].s, T.now);
  session_start(T.e[1].s, T.now);
  const int at = wait_for_frame(T, 1u, (uint8_t)PT_BATTLE_STATE);
  CHECK(at >= 0);
  if (at >= 0) {
    uint8_t* f = T.lk.inbox[1].frame[at];
    const uint16_t n = T.lk.inbox[1].len[at];
    put32(f + PROTO_HDR_BYTES + 0, 0x0BADC0DEu);
    reseal(f, n);
  }
  trial_run(T);
  CHECK(!T.hung);
  CHECK_EQ(session_end(T.e[1].s).reason, SE_DESYNC);
  CHECK_EQ(session_end(T.e[1].s).detail, SD_SETUP);
  CHECK_EQ(session_end(T.e[1].s).peer_hash, 0x0BADC0DE);
  // AND THE PEER IS TOLD IT WAS A DESYNC, not left to read a farewell: a
  // BATTLE_END carries a REASON and this state must not flatten it into
  // SE_LOST, which is the one code that exists to mean the opposite.
  CHECK_EQ(session_end(T.e[0].s).reason, SE_DESYNC);
  CHECK_EQ(session_end(T.e[0].s).detail, SD_SETUP);
  CHECK_EQ(T.e[1].st.round, 1);                 // NOT ONE ROUND RAN
  CHECK_EQ(T.e[1].st.side[0].pending_kind, BACT_NONE);
  CHECK(!session_rewards_authorised(T.e[0].s));
  CHECK(!session_rewards_authorised(T.e[1].s));
  CHECK(boxes_untouched(T));
}

TEST(a_final_hash_that_disagrees_ends_the_session_unpaid)
{
  // The last comparison in the protocol, and the one that decides whether
  // anybody is paid. Two engines can agree on every round and still disagree
  // about the final state - and a peer can simply claim they do.
  Trial& T = arena();
  trial_begin(T, 0x3500u, CLEAN);
  CHECK_EQ(trial_set_teams(T), VR_OK);
  session_start(T.e[0].s, T.now);
  session_start(T.e[1].s, T.now);
  const int at = wait_for_frame(T, 1u, (uint8_t)PT_BATTLE_END);
  CHECK(at >= 0);
  if (at >= 0) {
    uint8_t* f = T.lk.inbox[1].frame[at];
    const uint16_t n = T.lk.inbox[1].len[at];
    put32(f + PROTO_HDR_BYTES + 4, 0xFEEDFACEu);        // the final hash
    reseal(f, n);
  }
  trial_run(T);
  CHECK(!T.hung);
  CHECK_EQ(session_end(T.e[1].s).reason, SE_DESYNC);
  CHECK_EQ(session_end(T.e[1].s).detail, SD_END_DISAGREE);
  CHECK_EQ(session_end(T.e[1].s).peer_hash, 0xFEEDFACE);
  CHECK(!session_rewards_authorised(T.e[1].s));
  // The battle itself finished and the engines never disagreed about a round:
  // the loss is the AGREEMENT, not the simulation.
  CHECK_EQ(T.e[0].st.outcome, T.e[1].st.outcome);
  CHECK(boxes_untouched(T));
}

// =============================================================================
//  6. CAPABILITIES
// =============================================================================
TEST(a_peer_on_another_content_pack_is_named_before_a_team_is_sent)
{
  Trial& T = arena();
  trial_begin(T, 0x4000u, CLEAN);
  CHECK_EQ(trial_set_teams(T), VR_OK);
  session_start(T.e[0].s, T.now);
  session_start(T.e[1].s, T.now);
  const int at = wait_for_frame(T, 1u, (uint8_t)PT_CAPABILITIES);
  CHECK(at >= 0);
  if (at >= 0) {
    uint8_t* f = T.lk.inbox[1].frame[at];
    const uint16_t n = T.lk.inbox[1].len[at];
    put16(f + PROTO_HDR_BYTES + 8, (uint16_t)(get16(f + PROTO_HDR_BYTES + 8) ^ 0x0001u));
    reseal(f, n);
  }
  trial_run(T);
  CHECK(!T.hung);
  CHECK_EQ(session_end(T.e[1].s).reason, SE_INCOMPATIBLE);
  CHECK_EQ(session_end(T.e[1].s).detail, SCW_CONTENT_VER);
  CHECK_EQ(T.e[1].s.team_seen, 0);
  CHECK_EQ(T.e[1].st.phase, BP_INIT);
}

TEST(matching_version_words_over_a_different_hash_basis_are_a_named_skew)
{
  // The one thing the words cannot see: a build where a version word never
  // reached the hash. It is the exact bug the P4-C2/C3 follow-up fixed, and
  // CAPABILITIES carries the basis so it surfaces before a battle exists.
  Trial& T = arena();
  trial_begin(T, 0x4100u, CLEAN);
  CHECK_EQ(trial_set_teams(T), VR_OK);
  session_start(T.e[0].s, T.now);
  session_start(T.e[1].s, T.now);
  const int at = wait_for_frame(T, 1u, (uint8_t)PT_CAPABILITIES);
  CHECK(at >= 0);
  if (at >= 0) {
    uint8_t* f = T.lk.inbox[1].frame[at];
    const uint16_t n = T.lk.inbox[1].len[at];
    f[PROTO_HDR_BYTES + 12] = (uint8_t)(f[PROTO_HDR_BYTES + 12] ^ 0x40u);
    reseal(f, n);
  }
  trial_run(T);
  CHECK_EQ(session_end(T.e[1].s).reason, SE_PROTOCOL);
  CHECK_EQ(session_end(T.e[1].s).detail, SD_BASIS_SKEW);
}

TEST(a_peer_that_answers_with_our_own_identity_is_not_a_peer)
{
  Trial& T = arena();
  trial_begin(T, 0x4200u, CLEAN);
  CHECK_EQ(trial_set_teams(T), VR_OK);
  session_start(T.e[0].s, T.now);
  ProtoMsg m;
  proto_msg_init(m, PT_HELLO, 0u, 0x40u);
  m.p.hello.device_id   = DEV_ID[0];              // our own, reflected
  m.p.hello.hello_nonce = T.e[0].s.nonce_local;
  uint8_t buf[PROTO_FRAME_MAX]; size_t n = 0;
  CHECK_EQ(proto_encode(m, buf, sizeof buf, n), PE_OK);
  inject(T.lk, 0u, buf, (uint16_t)n);
  session_poll(T.e[0].s, T.now);
  CHECK_EQ(session_end(T.e[0].s).reason, SE_PROTOCOL);
  CHECK_EQ(session_end(T.e[0].s).detail, SD_SELF);
  CHECK_EQ(T.e[0].s.id, 0);
}

// =============================================================================
//  7. THE FOUR RULES BEFORE THE TABLE
// =============================================================================
TEST(junk_never_reaches_the_state_machine_and_never_holds_the_session_open)
{
  // R1 AND R4 IN ONE CASE. The link is cut, and then a flood of malformed
  // frames, frames for another session and legal frames from the wrong state is
  // poured in. The ladder must expire on EXACTLY its own schedule: a session
  // that an attacker can keep alive by typing is a session that never times out.
  Trial& T = arena();
  trial_begin(T, 0x5000u, CLEAN);
  CHECK_EQ(trial_set_teams(T), VR_OK);
  session_start(T.e[0].s, T.now);
  session_start(T.e[1].s, T.now);
  CHECK(trial_run_until_state(T, 0u, SS_TEAM));
  link_cut(T);

  uint8_t buf[PROTO_FRAME_MAX]; size_t n = 0;
  ProtoMsg m;
  const uint16_t ok_before = session_end(T.e[0].s).rx_ok;
  uint16_t poured = 0;
  while (!session_closed(T.e[0].s)) {
    // a) a frame for a session that is not ours
    proto_msg_init(m, PT_GOODBYE, T.e[0].s.id ^ 0xFFFFu,
                   (uint16_t)(T.e[0].s.rx_seq_last + 1u));
    m.p.bye.reason = 1u;
    CHECK_EQ(proto_encode(m, buf, sizeof buf, n), PE_OK);
    inject(T.lk, 0u, buf, (uint16_t)n);
    // b) a legal frame from a state that cannot send it here
    proto_msg_init(m, PT_ACTION_RESULT, T.e[0].s.id,
                   (uint16_t)(T.e[0].s.rx_seq_last + 1u));
    m.round = 1u;
    CHECK_EQ(proto_encode(m, buf, sizeof buf, n), PE_OK);
    inject(T.lk, 0u, buf, (uint16_t)n);
    // c) plain rubbish
    memset(buf, 0x5Au, 40);
    inject(T.lk, 0u, buf, 40u);
    poured++;
    if (poured > 200u) break;
    session_poll(T.e[0].s, T.now);
    // The driver only advances its virtual clock on an idle round, and the
    // injections are not idle - so this case advances it itself. That IS the
    // property under test: the ladder must climb on schedule while the junk
    // arrives, because a session an attacker can hold open by typing never
    // times out at all.
    T.now += PROTO_RETX_MS;
  }
  CHECK(poured > 1u);
  const SessionEnd& e = session_end(T.e[0].s);
  CHECK_EQ(e.reason, SE_LOST);
  CHECK_EQ(e.state, SS_TEAM);
  CHECK_EQ(e.tx_retx, PROTO_RETX_MAX);      // the flood bought the peer NOTHING
  CHECK(e.rx_reject > 0);                   // R1: the codec refused them
  CHECK(e.rx_wrong_state > 0);              // R3: legal, unexpected, counted
  CHECK_EQ(e.rx_ok, ok_before + e.rx_wrong_state);
  CHECK(!session_rewards_authorised(T.e[0].s));
}

TEST(a_frame_larger_than_the_link_carries_is_refused_and_looks_like_a_loss)
{
  // mtu is DATA. At 60 bytes the 164-byte TEAM_SUBMIT cannot go out at all, and
  // the session's reaction has to be identical to a dropped frame - which is
  // the property that makes a host loopback test mean something about a radio.
  Trial& T = arena();
  trial_begin(T, 0x5100u, CLEAN);
  CHECK_EQ(trial_set_teams(T), VR_OK);
  T.e[0].tp.mtu = 60u;
  T.e[1].tp.mtu = 60u;
  session_start(T.e[0].s, T.now);
  session_start(T.e[1].s, T.now);
  trial_run(T);
  CHECK(!T.hung);
  CHECK_EQ(session_end(T.e[0].s).reason, SE_LOST);
  CHECK_EQ(session_end(T.e[0].s).state, SS_TEAM);
  CHECK_EQ(session_end(T.e[0].s).tx_retx, PROTO_RETX_MAX);
  CHECK_EQ(T.e[0].st.phase, BP_INIT);
}

TEST(a_second_team_with_other_members_is_named_and_stopped)
{
  Trial& T = arena();
  trial_begin(T, 0x5200u, CLEAN);
  CHECK_EQ(trial_set_teams(T), VR_OK);
  session_start(T.e[0].s, T.now);
  session_start(T.e[1].s, T.now);
  CHECK(trial_run_until_state(T, 0u, SS_TEAM));
  // Wait until the peer's real TEAM_SUBMIT has been accepted, then send another.
  // The endpoint may already have moved on by then, and the rule has to hold
  // from wherever it is: "the peer changed its team" is a termination and it is
  // checked BEFORE the answer-from-state leash that covers the ordinary repeat.
  while (T.e[0].s.team_seen == 0u && !session_closed(T.e[0].s)) (void)trial_step(T);
  CHECK_EQ(T.e[0].s.team_seen, 1);
  ProtoMsg m;
  // A seq a little ahead of the window rather than exactly one past it: frames
  // the peer really sent may still be queued, and two frames sharing a seq make
  // the second one a duplicate the window drops before the FSM ever sees it.
  proto_msg_init(m, PT_TEAM_SUBMIT, T.e[0].s.id, (uint16_t)(T.e[0].s.rx_seq_last + 8u));
  m.p.team.count    = (uint8_t)BATTLE_TEAM_MAX;
  m.p.team.team_crc = (uint16_t)(T.e[0].s.team_crc_peer ^ 0x1234u);
  memcpy(m.p.team.rec, T.e[1].s.my_rec, sizeof m.p.team.rec);
  uint8_t buf[PROTO_FRAME_MAX]; size_t n = 0;
  CHECK_EQ(proto_encode(m, buf, sizeof buf, n), PE_OK);
  inject(T.lk, 0u, buf, (uint16_t)n);
  session_poll(T.e[0].s, T.now);
  CHECK_EQ(session_end(T.e[0].s).reason, SE_PROTOCOL);
  CHECK_EQ(session_end(T.e[0].s).detail, SD_TEAM_CHANGED);
}

// =============================================================================
//  8. THE TWO GENERALS
// =============================================================================
TEST(a_battle_end_that_never_arrives_pays_one_side_and_not_the_other)
{
  // THE LIMIT, MEASURED RATHER THAN ARGUED. Rewards need our BATTLE_END sent
  // and the peer's received with a matching outcome and final hash. Kill every
  // BATTLE_END in one direction and the asymmetry appears - and it falls on the
  // side of NOT awarding, which is the only safe direction.
  Trial& T = arena();
  LoopbackFault f = {};
  f.block_type[1] = (uint8_t)PT_BATTLE_END;
  f.block_left[1] = 0xFFu;
  trial_begin(T, 0x8000u, f);
  CHECK_EQ(trial_set_teams(T), VR_OK);
  session_start(T.e[0].s, T.now);
  session_start(T.e[1].s, T.now);
  trial_run(T);
  CHECK(!T.hung);
  CHECK_EQ(session_end(T.e[1].s).reason, SE_DONE);      // it saw ours
  CHECK(session_rewards_authorised(T.e[1].s));
  CHECK_EQ(session_end(T.e[0].s).reason, SE_LOST);      // we never saw its
  CHECK_EQ(session_end(T.e[0].s).state, SS_ENDING);
  CHECK(!session_rewards_authorised(T.e[0].s));
  // Both engines still AGREE about what happened; only the payment is uneven.
  CHECK_EQ(T.e[0].st.outcome, T.e[1].st.outcome);
  CHECK_EQ(memcmp(&T.e[0].st, &T.e[1].st, sizeof(BattleState)), 0);
  CHECK(boxes_untouched(T));
}

// =============================================================================
//  9. THE ACCEPTANCE RUN
// =============================================================================
enum LinkCensus : uint8_t {
  LE_COMPLETED_AGREED = 0, LE_DESYNC_DECLARED, LE_SESSION_LOST, LE_HALF_PAID,
  LE_OTHER, LE_SILENT, LE_BUCKETS
};
static const char* const CENSUS_NAME[] = {
  "completed+agreed", "desync declared", "session lost", "half paid",
  "other terminal", "SILENT DIVERGENCE"
};

// INV-3: at the moment both endpoints are terminal, EITHER both carry SE_DONE
// with the same outcome, the same hash and byte-identical state, OR at least
// one is not SE_DONE and neither wrote its Box. Any other pair is silent.
static LinkCensus classify(const Trial& T)
{
  const bool pa = session_rewards_authorised(T.e[0].s);
  const bool pb = session_rewards_authorised(T.e[1].s);
  const bool same = (T.e[0].st.outcome == T.e[1].st.outcome) &&
                    (battle_state_hash(T.e[0].st) == battle_state_hash(T.e[1].st)) &&
                    (memcmp(&T.e[0].st, &T.e[1].st, sizeof(BattleState)) == 0);
  if (pa && pb) return same ? LE_COMPLETED_AGREED : LE_SILENT;
  // A HALF-PAID TRIAL IS STILL CHECKED FOR AGREEMENT. This line used to return
  // LE_HALF_PAID before `same` was consulted at all, so a trial that paid
  // exactly one side ON A DIVERGENT STATE would have landed in an ACCEPTED
  // bucket - the census's own blind spot. It has never fired (0 in every arm),
  // which is what makes it a latent hole rather than a defect, and it costs one
  // condition to close.
  if (pa != pb) return same ? LE_HALF_PAID : LE_SILENT;
  if (session_end(T.e[0].s).reason == SE_DESYNC ||
      session_end(T.e[1].s).reason == SE_DESYNC) return LE_DESYNC_DECLARED;
  if (session_end(T.e[0].s).reason == SE_LOST ||
      session_end(T.e[1].s).reason == SE_LOST) return LE_SESSION_LOST;
  return LE_OTHER;
}

// What an arm has to show. THE 10 % ARMS ALL COMPLETE, MEASURED: nine rungs at
// one attempt each leave an obligation about 3e-7 short of a loss under an
// independent 10 % per-direction model, and 500 trials of each arm found none.
// So the "or aborts cleanly" half of the promise is exercised by a HARSHER arm
// rather than pretended into the 10 % ones.
enum ArmExpect : uint8_t { ARM_ALL_COMPLETE = 0, ARM_MOSTLY_COMPLETE, ARM_MIXED };

static void arm(const char* name, const LoopbackFault& f, uint32_t trials,
                uint32_t* tally, ArmExpect expect)
{
  Trial& T = arena();
  uint32_t overflow = 0, sends = 0;
  uint32_t detail_tally[(size_t)SD_DETAIL_COUNT] = { 0 };
  for (uint32_t i = 0; i < trials; ++i) {
    const uint32_t seed = 0x5E5510u + i;
    trial_begin(T, seed, f);
    if (trial_set_teams(T) != VR_OK) { CHECK(false); return; }
    session_start(T.e[0].s, T.now);
    session_start(T.e[1].s, T.now);
    trial_run(T);
    if (T.hung) {
      // NAMED, not numbered. A hung trial is the one failure whose whole
      // content is WHERE the two endpoints stopped, and until P4-C6 this line
      // printed neither - session_state_name() existed for exactly this and
      // had no caller anywhere in the tree.
      fprintf(stderr, "  arm %s: seed 0x%X DID NOT TERMINATE (%s / %s)\n",
              name, (unsigned)seed,
              session_state_name((SessionState)T.e[0].s.state),
              session_state_name((SessionState)T.e[1].s.state));
      CHECK(false);
      return;
    }
    const LinkCensus c = classify(T);
    tally[c]++;
    if (c == LE_SILENT) {
      fprintf(stderr, "  arm %s: seed 0x%X SILENT DIVERGENCE\n", name, (unsigned)seed);
    }
    // WHY a trial did not complete, by NAME. The census counts terminals; this
    // records which SessionDetail each non-completing pair carried, so an arm
    // that starts losing trials says SD_OPEN_HASH or SD_RX_BUDGET instead of a
    // number the reader has to look up. Both endpoints are consulted: only one
    // of them is usually the one that declared.
    if (c != LE_COMPLETED_AGREED) {
      for (uint8_t q = 0; q < 2u; ++q) {
        const uint8_t d = session_end(T.e[q].s).detail;
        if (d < (uint8_t)SD_DETAIL_COUNT) detail_tally[d]++;
      }
    }
    if (!boxes_untouched(T)) {
      fprintf(stderr, "  arm %s: seed 0x%X wrote a Box\n", name, (unsigned)seed);
      CHECK(false);
      return;
    }
    overflow += T.lk.stats.overflow;
    sends    += T.lk.stats.sent;
  }
  printf("  arm %-22s", name);
  for (uint8_t b = 0; b < (uint8_t)LE_BUCKETS; ++b)
    printf(" %s=%u", CENSUS_NAME[b], (unsigned)tally[b]);
  printf(" overflow=%u/%u", (unsigned)overflow, (unsigned)sends);
  // SD_NONE INCLUDED ON PURPOSE. The harsh arm's 28 losses and 1 half-pay carry
  // no detail at all, and "SD_NONE=58" - 29 trials x 2 endpoints - is exactly the
  // fact worth printing: they ran out of retransmissions, they did not disagree.
  for (uint8_t d = 0; d < (uint8_t)SD_DETAIL_COUNT; ++d)
    if (detail_tally[d]) printf(" %s=%u", session_detail_name((SessionDetail)d),
                                (unsigned)detail_tally[d]);
  printf("\n");
  CHECK_EQ(tally[LE_SILENT], 0);
  // THE INSTRUMENT'S OWN ERROR BAR, REPORTED RATHER THAN ASSUMED AWAY, and it
  // is NOT zero. A send onto a full LB_QUEUE_CAP queue is counted as a drop, so
  // an arm's EFFECTIVE loss rate is its declared one plus this. MEASURED at this
  // commit: 0 of 70,298 sends on the clean arm, 283 of 109,656 at 10 % drop, 192
  // of 88,042 on the reorder arm, 540 of 120,856 with all three, 43 of 157,896
  // on the harsh arm - i.e. under half a percent everywhere, which is why the
  // arms still measure roughly what they say. Both halves are asserted: an
  // UNFAULTED link must overflow exactly nothing (or the harness itself is
  // losing frames), and no arm may let queue overflow grow into the dominant
  // fault and quietly become a different experiment.
  const bool unfaulted = (f.drop_permille == 0u && f.dup_permille == 0u &&
                          f.reorder_permille == 0u && f.dead == 0u &&
                          f.block_type[0] == 0u && f.block_type[1] == 0u);
  if (unfaulted) CHECK_EQ(overflow, 0);
  CHECK(overflow * 100u < sends);
  if (expect == ARM_ALL_COMPLETE) {
    CHECK_EQ(tally[LE_COMPLETED_AGREED], trials);
  } else if (expect == ARM_MOSTLY_COMPLETE) {
    // A threshold rather than an equality: the arm is probabilistic and 500/500
    // is what it measured, but a run that lost a handful is still inside the
    // promise. Below this it is not, and the census says which bucket took them.
    CHECK(tally[LE_COMPLETED_AGREED] >= trials - trials / 100u);
  } else {
    // NOT VACUOUS: the arm has to have exercised BOTH halves of the promise,
    // because "every trial aborted immediately" satisfies "never diverges
    // silently" and proves nothing at all.
    CHECK(tally[LE_COMPLETED_AGREED] > 0);
    CHECK(tally[LE_COMPLETED_AGREED] < trials);
    CHECK(tally[LE_SESSION_LOST] + tally[LE_DESYNC_DECLARED] + tally[LE_HALF_PAID] > 0);
  }
}

TEST(the_acceptance_run_completes_or_aborts_by_name_and_never_diverges_in_silence)
{
  const uint32_t N = 500u;
  uint32_t tally[LE_BUCKETS];

  LoopbackFault clean = {};
  memset(tally, 0, sizeof tally);
  arm("clean", clean, N, tally, ARM_ALL_COMPLETE);

  LoopbackFault drop = {}; drop.drop_permille = 100u;
  memset(tally, 0, sizeof tally);
  arm("10% drop", drop, N, tally, ARM_MOSTLY_COMPLETE);

  LoopbackFault dup = {}; dup.dup_permille = 100u;
  memset(tally, 0, sizeof tally);
  arm("10% duplicate", dup, N, tally, ARM_ALL_COMPLETE);   // duplication loses nothing

  LoopbackFault reo = {}; reo.reorder_permille = 100u; reo.reorder_window = 4u;
  memset(tally, 0, sizeof tally);
  arm("10% reorder w4", reo, N, tally, ARM_ALL_COMPLETE);  // reordering loses nothing

  LoopbackFault all = {};
  all.drop_permille = 100u; all.dup_permille = 100u;
  all.reorder_permille = 100u; all.reorder_window = 4u;
  memset(tally, 0, sizeof tally);
  arm("10% drop+dup+reorder", all, N, tally, ARM_MOSTLY_COMPLETE);

  LoopbackFault hard = {};
  hard.drop_permille = 300u; hard.dup_permille = 100u;
  hard.reorder_permille = 200u; hard.reorder_window = 4u;
  memset(tally, 0, sizeof tally);
  arm("30% drop, 20% reorder", hard, N, tally, ARM_MIXED);
}

TEST(a_peer_that_vanishes_mid_battle_is_lost_at_the_round_it_died_on)
{
  // The vanish arm as its OWN case, because the census would happily absorb it:
  // 100 % SE_LOST, every ladder fully climbed, the round recorded, and the
  // refusal proved by the ABSENCE of a side effect - not one Box byte moved.
  uint32_t lost = 0, at_round = 0;
  for (uint32_t i = 0; i < 60u; ++i) {
    Trial& T = arena();
    trial_begin(T, 0x7E5700u + i, CLEAN);
    CHECK_EQ(trial_set_teams(T), VR_OK);
    session_start(T.e[0].s, T.now);
    session_start(T.e[1].s, T.now);
    CHECK(trial_run_until_state(T, 0u, SS_BATTLE));
    const uint8_t want_round = (uint8_t)(1u + (i % 3u));
    while (T.e[0].st.round < want_round && !session_closed(T.e[0].s)) (void)trial_step(T);
    const uint16_t died_at = T.e[0].st.round;
    link_cut(T);
    trial_run(T);
    CHECK(!T.hung);
    if (session_end(T.e[0].s).reason == SE_LOST) lost++;
    // The record names the round the session was ON when it closed, which is at
    // or past the round the link died on: an endpoint holding both actions may
    // still resolve one more round before its ladder expires.
    if (session_end(T.e[0].s).round == T.e[0].st.round &&
        session_end(T.e[0].s).round >= died_at) at_round++;
    CHECK_EQ(session_end(T.e[0].s).tx_retx, PROTO_RETX_MAX);
    CHECK(!session_rewards_authorised(T.e[0].s));
    CHECK(!session_rewards_authorised(T.e[1].s));
    CHECK(boxes_untouched(T));
  }
  CHECK_EQ(lost, 60);
  CHECK_EQ(at_round, 60);
}

TEST(rewards_are_authorised_on_agreement_and_on_nothing_else)
{
  // Every terminal this file can produce, collected in one place: exactly one
  // reason pays, and it pays only when the outcome AND the final hash agreed.
  struct Seen { uint8_t reason; bool paid; };
  Seen seen[SE_REASON_COUNT];
  for (uint8_t i = 0; i < (uint8_t)SE_REASON_COUNT; ++i) { seen[i].reason = 0u; seen[i].paid = false; }

  Trial& T = arena();
  // SE_DONE
  trial_begin(T, 0x9001u, CLEAN);
  CHECK_EQ(trial_set_teams(T), VR_OK);
  session_start(T.e[0].s, T.now); session_start(T.e[1].s, T.now);
  trial_run(T);
  seen[session_end(T.e[0].s).reason].reason = 1u;
  seen[session_end(T.e[0].s).reason].paid   = session_rewards_authorised(T.e[0].s);
  // SE_LOST
  trial_begin(T, 0x9002u, CLEAN);
  CHECK_EQ(trial_set_teams(T), VR_OK);
  session_start(T.e[0].s, T.now); session_start(T.e[1].s, T.now);
  CHECK(trial_run_until_state(T, 0u, SS_BATTLE));
  link_cut(T); trial_run(T);
  seen[session_end(T.e[0].s).reason].reason = 1u;
  seen[session_end(T.e[0].s).reason].paid   = session_rewards_authorised(T.e[0].s);
  // SE_REJECTED
  trial_begin(T, 0x9003u, CLEAN);
  CHECK_EQ(trial_set_teams(T), VR_OK);
  session_start(T.e[0].s, T.now); session_start(T.e[1].s, T.now);
  CHECK(poison_in_flight(T, 0u, 0u, &ed_level31));
  trial_run(T);
  seen[session_end(T.e[0].s).reason].reason = 1u;
  seen[session_end(T.e[0].s).reason].paid   = session_rewards_authorised(T.e[0].s);
  // SE_DESYNC
  run_to_battle(T, 0x9004u);
  {
    const int at = wait_for_frame(T, 1u, (uint8_t)PT_ROUND_RESULT);
    if (at >= 0) {
      uint8_t* f = T.lk.inbox[1].frame[at];
      const uint16_t n = T.lk.inbox[1].len[at];
      f[PROTO_HDR_BYTES + 4] = (uint8_t)(f[PROTO_HDR_BYTES + 4] ^ 0x11u);
      reseal(f, n);
    }
  }
  trial_run(T);
  seen[session_end(T.e[1].s).reason].reason = 1u;
  seen[session_end(T.e[1].s).reason].paid   = session_rewards_authorised(T.e[1].s);
  // SE_LOCAL_CANCEL
  trial_begin(T, 0x9005u, CLEAN);
  CHECK_EQ(trial_set_teams(T), VR_OK);
  session_start(T.e[0].s, T.now); session_start(T.e[1].s, T.now);
  CHECK(trial_run_until_state(T, 0u, SS_BATTLE));
  session_cancel(T.e[0].s, T.now);
  seen[session_end(T.e[0].s).reason].reason = 1u;
  seen[session_end(T.e[0].s).reason].paid   = session_rewards_authorised(T.e[0].s);

  uint8_t reached = 0;
  for (uint8_t i = 1; i < (uint8_t)SE_REASON_COUNT; ++i) {
    if (seen[i].reason == 0u) continue;
    reached++;
    CHECK_EQ(seen[i].paid, (i == (uint8_t)SE_DONE));
  }
  CHECK_EQ(reached, 5);        // DONE, LOST, DESYNC, REJECTED, LOCAL_CANCEL
  CHECK(seen[SE_DONE].paid);
}

// Runs a clean pair with the PEER'S GOODBYE blocked, so endpoint 0 agrees the
// battle, commits its rewards and then SITS in SS_ENDING with the session still
// open - the one window in which "we have already paid" and "the session has not
// ended" are both true. Leaves the link drained so the next poll sees only what
// the case injects.
static bool run_to_paid_and_still_open(Trial& T, uint32_t seed)
{
  LoopbackFault f = {};
  f.block_type[1] = (uint8_t)PT_GOODBYE;
  f.block_left[1] = 0xFFu;
  trial_begin(T, seed, f);
  if (trial_set_teams(T) != VR_OK) return false;
  session_start(T.e[0].s, T.now);
  session_start(T.e[1].s, T.now);
  while (!session_closed(T.e[0].s) && !session_rewards_authorised(T.e[0].s)) {
    if (T.iters++ >= 400000u) { T.hung = true; return false; }
    (void)trial_step(T);
  }
  T.lk.inbox[0].count = 0u;
  T.lk.inbox[1].count = 0u;
  return session_rewards_authorised(T.e[0].s) && !session_closed(T.e[0].s) &&
         session_state(T.e[0].s) == (uint8_t)SS_ENDING;
}

static void inject_msg(Trial& T, uint8_t who, ProtoMsg& m)
{
  uint8_t buf[PROTO_FRAME_MAX]; size_t n = 0;
  m.seq = (uint16_t)(T.e[who].s.rx_seq_last + 1u);
  CHECK_EQ((int)proto_encode(m, buf, sizeof buf, n), (int)PE_OK);
  inject(T.lk, who, buf, (uint16_t)n);
}

TEST(a_peer_cannot_relabel_a_terminal_after_the_rewards_are_committed)
{
  // session.h says session_rewards_authorised() is true ONLY after SE_DONE and
  // docs/protocol.md's LIMIT 1 says SE_DESYNC pays nobody on BOTH sides. BOTH
  // WERE FALSE ONCE WE HAD PAID: on_battle_end() closed SE_DESYNC or SE_LOST
  // from the peer's own reason byte without ever consulting s.paid, and nothing
  // cleared s.paid - so a peer that had already made us pay could then CHOOSE
  // our terminal label, and a caller following LIMIT 1 (`if reason == SE_DONE`)
  // and one following the header (`if rewards_authorised()`) disagreed about
  // the same session, with the PEER picking which.
  //
  // The rule now lives in session_close(): a session that has authorised
  // rewards has agreed everything a battle decides - our own comparison of the
  // peer's outcome AND final hash against ours - and a later terminal cannot
  // un-agree it. THREE VECTORS, and the third is not a BATTLE_END at all,
  // which is why the fix could not live in on_battle_end() alone.
  Trial& T = arena();

  // (a) A FORGED DESYNC, carrying a final hash this endpoint never computed.
  CHECK(run_to_paid_and_still_open(T, 0x9200u));
  {
    ProtoMsg m; proto_msg_init(m, PT_BATTLE_END, T.e[0].s.id, 0u);
    m.p.bend.reason     = (uint8_t)SE_DESYNC;
    m.p.bend.detail     = (uint8_t)SD_RULES;
    m.p.bend.final_hash = 0xDEADBEEFu;
    inject_msg(T, 0u, m);
    session_poll(T.e[0].s, T.now);
  }
  CHECK_EQ(session_rewards_authorised(T.e[0].s),
           session_end(T.e[0].s).reason == (uint8_t)SE_DONE);
  CHECK_EQ(session_end(T.e[0].s).reason, SE_DONE);
  CHECK(session_rewards_authorised(T.e[0].s));

  // (b) A PEER THAT SAYS IT GAVE UP. Pre-fix this closed SE_LOST while paid.
  CHECK(run_to_paid_and_still_open(T, 0x9201u));
  {
    ProtoMsg m; proto_msg_init(m, PT_BATTLE_END, T.e[0].s.id, 0u);
    m.p.bend.reason = (uint8_t)SE_LOCAL_CANCEL;
    inject_msg(T, 0u, m);
    session_poll(T.e[0].s, T.now);
  }
  CHECK_EQ(session_rewards_authorised(T.e[0].s),
           session_end(T.e[0].s).reason == (uint8_t)SE_DONE);
  CHECK_EQ(session_end(T.e[0].s).reason, SE_DONE);

  // (c) NOT A BATTLE_END AT ALL: a flood that exhausts SESSION_MAX_RX after the
  //     rewards are committed. The clock is deliberately NOT advanced, so the
  //     ladder cannot get there first and the budget is what closes the session.
  CHECK(run_to_paid_and_still_open(T, 0x9202u));
  {
    uint32_t poured = 0;
    while (!session_closed(T.e[0].s) && poured < (uint32_t)SESSION_MAX_RX + 64u) {
      ProtoMsg m; mk_replayed_battle_state(T, 0u, m);
      inject_msg(T, 0u, m);
      session_poll(T.e[0].s, T.now);
      poured++;
    }
    CHECK(session_closed(T.e[0].s));
    CHECK(poured > (uint32_t)SESSION_MAX_RX / 2u);   // the budget, not the ladder
  }
  CHECK_EQ(session_rewards_authorised(T.e[0].s),
           session_end(T.e[0].s).reason == (uint8_t)SE_DONE);
  CHECK_EQ(session_end(T.e[0].s).reason, SE_DONE);
  CHECK(boxes_untouched(T));
}

TEST(a_cancelled_session_says_goodbye_and_the_peer_is_not_left_hanging)
{
  Trial& T = arena();
  trial_begin(T, 0x9100u, CLEAN);
  CHECK_EQ(trial_set_teams(T), VR_OK);
  session_start(T.e[0].s, T.now);
  session_start(T.e[1].s, T.now);
  CHECK(trial_run_until_state(T, 0u, SS_BATTLE));
  session_cancel(T.e[0].s, T.now);
  CHECK_EQ(session_end(T.e[0].s).reason, SE_LOCAL_CANCEL);
  trial_run(T);
  CHECK_EQ(session_end(T.e[1].s).reason, SE_LOST);
  CHECK_EQ(session_end(T.e[1].s).detail, SD_PEER_GOODBYE);
  CHECK(session_end(T.e[1].s).tx_retx < PROTO_RETX_MAX);   // told, not timed out
  CHECK(!session_rewards_authorised(T.e[1].s));
}

// =============================================================================
//  10. THE LOOPBACK ITSELF
// =============================================================================
TEST(the_loopback_injects_exactly_the_faults_it_was_asked_for)
{
  // The fault model is the instrument every case above is measured with, so it
  // is calibrated here rather than trusted: 0 permille never fires and 1000
  // always does, and the same seed gives the same pattern twice.
  LoopbackLink lk;
  LoopbackPort p0, p1;
  uint8_t f[PROTO_FRAME_MAX]; memset(f, 0x11, sizeof f);
  f[1] = (uint8_t)PT_ACTION;

  LoopbackFault none = {};
  loopback_init(lk, 1u, none);
  Transport t0 = transport_loopback(p0, lk, 0u);
  Transport t1 = transport_loopback(p1, lk, 1u);
  uint8_t rx[PROTO_FRAME_MAX];
  for (uint8_t i = 0; i < 10u; ++i) CHECK(transport_send(t0, f, 20u));
  CHECK_EQ(loopback_pending(lk, 1u), 10);
  CHECK_EQ(lk.stats.dropped, 0);
  for (uint8_t i = 0; i < 10u; ++i) CHECK_EQ(transport_recv(t1, rx, sizeof rx), 20);
  CHECK_EQ(transport_recv(t1, rx, sizeof rx), 0);

  LoopbackFault all_drop = {}; all_drop.drop_permille = 1000u;
  loopback_init(lk, 1u, all_drop);
  t0 = transport_loopback(p0, lk, 0u);
  t1 = transport_loopback(p1, lk, 1u);
  for (uint8_t i = 0; i < 10u; ++i) CHECK(transport_send(t0, f, 20u));
  CHECK_EQ(loopback_pending(lk, 1u), 0);
  CHECK_EQ(lk.stats.dropped, 10);

  LoopbackFault all_dup = {}; all_dup.dup_permille = 1000u;
  loopback_init(lk, 1u, all_dup);
  t0 = transport_loopback(p0, lk, 0u);
  for (uint8_t i = 0; i < 5u; ++i) CHECK(transport_send(t0, f, 20u));
  CHECK_EQ(loopback_pending(lk, 1u), 10);
  CHECK_EQ(lk.stats.duplicated, 5);

  // The mtu is a field, and a frame above it never leaves.
  loopback_init(lk, 1u, none);
  t0 = transport_loopback(p0, lk, 0u);
  t0.mtu = 19u;
  CHECK(!transport_send(t0, f, 20u));
  CHECK_EQ(lk.stats.sent, 0);

  // The scripted block kills a NAMED type and counts down.
  LoopbackFault blk = {}; blk.block_type[0] = (uint8_t)PT_ACTION; blk.block_left[0] = 2u;
  loopback_init(lk, 1u, blk);
  t0 = transport_loopback(p0, lk, 0u);
  for (uint8_t i = 0; i < 5u; ++i) CHECK(transport_send(t0, f, 20u));
  CHECK_EQ(lk.stats.blocked, 2);
  CHECK_EQ(loopback_pending(lk, 1u), 3);

  // REORDERING: every frame delivered exactly once, and not in the order it was
  // sent. Both halves are needed - a queue that dropped one would satisfy the
  // order half, and one that never reordered would satisfy the count half.
  LoopbackFault reo = {}; reo.reorder_permille = 1000u; reo.reorder_window = 8u;
  loopback_init(lk, 7u, reo);
  t0 = transport_loopback(p0, lk, 0u);
  t1 = transport_loopback(p1, lk, 1u);
  for (uint8_t i = 0; i < 8u; ++i) { f[PROTO_HDR_BYTES] = i; CHECK(transport_send(t0, f, 20u)); }
  uint8_t seen[8]; uint8_t got = 0; bool in_order = true;
  for (uint8_t i = 0; i < 8u; ++i) {
    CHECK_EQ(transport_recv(t1, rx, sizeof rx), 20);
    seen[got] = rx[PROTO_HDR_BYTES];
    if (seen[got] != got) in_order = false;
    got++;
  }
  CHECK_EQ(got, 8);
  CHECK(!in_order);
  CHECK(lk.stats.reordered > 0);
  uint8_t mask = 0;
  for (uint8_t i = 0; i < 8u; ++i) mask = (uint8_t)(mask | (1u << seen[i]));
  CHECK_EQ(mask, 0xFF);                        // each exactly once, none lost
  CHECK_EQ(transport_recv(t1, rx, sizeof rx), 0);

  // and a window of one is a queue that cannot reorder at all
  LoopbackFault noreo = {}; noreo.reorder_permille = 1000u; noreo.reorder_window = 1u;
  loopback_init(lk, 7u, noreo);
  t0 = transport_loopback(p0, lk, 0u);
  t1 = transport_loopback(p1, lk, 1u);
  for (uint8_t i = 0; i < 8u; ++i) { f[PROTO_HDR_BYTES] = i; CHECK(transport_send(t0, f, 20u)); }
  for (uint8_t i = 0; i < 8u; ++i) {
    CHECK_EQ(transport_recv(t1, rx, sizeof rx), 20);
    CHECK_EQ(rx[PROTO_HDR_BYTES], i);
  }
  CHECK_EQ(lk.stats.reordered, 0);
  f[PROTO_HDR_BYTES] = 0x11u;

  // Same seed, same fault pattern - which is what makes a failing acceptance
  // trial reproducible from the number the census prints.
  LoopbackFault mix = {}; mix.drop_permille = 300u; mix.dup_permille = 300u;
  uint32_t sig[2] = { 0u, 0u };
  for (uint8_t pass = 0; pass < 2u; ++pass) {
    loopback_init(lk, 0xABCDEFu, mix);
    t0 = transport_loopback(p0, lk, 0u);
    for (uint8_t i = 0; i < 60u; ++i) (void)transport_send(t0, f, 20u);
    sig[pass] = lk.stats.dropped * 1000u + lk.stats.duplicated;
  }
  CHECK_EQ(sig[0], sig[1]);
  CHECK(sig[0] != 0u);
}

TEST(the_abort_record_is_forty_bytes_and_the_session_fits_the_budget_it_claims)
{
  // The two numbers docs/protocol.md quotes, checked rather than remembered.
  CHECK_EQ(sizeof(SessionEnd), 40);
  CHECK_EQ(sizeof(LinkEvent), 8);
  // sizeof(Session) IS A HOST NUMBER HERE AND THE DOC SAYS SO. This binary is
  // x86-64, where it is 360; the DEVICE figure, measured by compiling the same
  // header with riscv32-esp-elf-g++, is 340. Only the bound is asserted, because
  // an equality here would pin the host's alignment and claim nothing about the
  // target.
  CHECK(sizeof(Session) <= 512);
  CHECK_EQ(sizeof(Transport), sizeof(void*) * 3 + sizeof(void*));
}

// P4-C6. session_reason_name() had a caller and its two siblings did not: a
// P4-C6 sweep for symbols present in an object file and absent from the linked
// ELF found session_state_name() and session_detail_name() uncalled ANYWHERE -
// not in Pebblebol/src, not in tests/ - and unlike proto_err_name() and
// validate_reject_name() neither had the totality case its enum deserves. Both
// are wired into the acceptance census above now, and this is the case they
// were missing: a name that is not distinct is a name that cannot tell two
// terminals apart in the one report anybody reads.
TEST(every_session_state_and_detail_has_a_distinct_english_name_and_the_lookup_is_total)
{
  for (int i = 0; i < (int)SS_STATE_COUNT; ++i) {
    const char* n = session_state_name((SessionState)i);
    CHECK(n != nullptr);
    CHECK(strncmp(n, "SS_", 3) == 0);
    CHECK(strcmp(n, "SS_?") != 0);
    for (int q = 0; q < i; ++q)
      CHECK(strcmp(n, session_state_name((SessionState)q)) != 0);
  }
  CHECK(strcmp(session_state_name((SessionState)SS_STATE_COUNT), "SS_?") == 0);
  CHECK(strcmp(session_state_name((SessionState)255), "SS_?") == 0);

  for (int i = 0; i < (int)SD_DETAIL_COUNT; ++i) {
    const char* n = session_detail_name((SessionDetail)i);
    CHECK(n != nullptr);
    CHECK(strncmp(n, "SD_", 3) == 0);
    CHECK(strcmp(n, "SD_?") != 0);
    for (int q = 0; q < i; ++q)
      CHECK(strcmp(n, session_detail_name((SessionDetail)q)) != 0);
  }
  CHECK(strcmp(session_detail_name((SessionDetail)SD_DETAIL_COUNT), "SD_?") == 0);
  CHECK(strcmp(session_detail_name((SessionDetail)255), "SD_?") == 0);

  // The third lookup, which DID have a caller and did NOT have this case.
  for (int i = 0; i < (int)SE_REASON_COUNT; ++i) {
    const char* n = session_reason_name((SessionEndReason)i);
    CHECK(n != nullptr);
    CHECK(strncmp(n, "SE_", 3) == 0);
    CHECK(strcmp(n, "SE_?") != 0);
    for (int q = 0; q < i; ++q)
      CHECK(strcmp(n, session_reason_name((SessionEndReason)q)) != 0);
  }
  CHECK(strcmp(session_reason_name((SessionEndReason)SE_REASON_COUNT), "SE_?") == 0);
  CHECK(strcmp(session_reason_name((SessionEndReason)255), "SE_?") == 0);
}
