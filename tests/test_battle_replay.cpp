// =============================================================================
//  ERRATA host test - test_battle_replay.cpp
//  DETERMINISM (plan P4-C2, spec section 14: "battle must be deterministic
//  enough to reproduce bugs from logs").
//
//  Four properties, because no one of them is sufficient:
//
//   1. TWO ENGINES, SAME SEED AND SAME ACTIONS, IDENTICAL HASH EVERY ROUND -
//      run INTERLEAVED (A round 1, B round 1, A round 2, B round 2, ...) and
//      not one after the other. A sequential pair still passes with hidden
//      file-scope state in battle.cpp; an interleaved pair does not.
//   2. THE NEGATIVE CONTROL, without which (1) is vacuous: a third engine on
//      seed + 1 must DIVERGE, and diverge EARLY. If two differently seeded
//      engines agreed for long, the RNG would not be reaching the outcome at
//      all and "two engines agree" would be satisfied by comparing an object
//      with itself.
//   3. THE PINNED CURSOR AND THE DRAW COUNT. rng.s after a scripted battle is
//      pinned to a constant, and the exact NUMBER of draws is recovered by
//      stepping a reference Rng. One extra rng_next_below() anywhere - in the
//      status tick, in the log path, in the validator - moves both.
//   4. REPLAY. Re-running the recorded actions reproduces the final state, and
//      changing ONE action at round k is reported at EXACTLY round k.
//
//  The action script is a pure function of the state over the module's public
//  CONST query surface - the same surface P4-C3's AI will use - so both engines
//  choose identically without either of them running an AI. REPLAY NEVER RUNS
//  AN AI EITHER: it reads the actions out of the log.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "game/battle.h"
#include "game/xp.h"

#define REPLAY_SEED   0x5EEDBEEFu
#define REPLAY_LEVEL  12u

static const uint8_t T_A[3] = {  3, 12, 15 };   // SIGNAL
static const uint8_t T_B[3] = { 18, 21, 33 };   // CORRUPT, CORRUPT, SYSTEM

static void mk_member(BugInstance& p, uint8_t species, uint8_t level, uint32_t id)
{
  memset(&p, 0, sizeof p);
  p.magic      = (uint16_t)BUG_MAGIC;
  p.layout_ver = (uint8_t)BUG_LAYOUT_VER;
  p.species_id = species;
  p.id         = id;
  p.level      = level;
  const SpeciesDef* sp = species_get(species);
  if (sp == nullptr) return;
  for (uint8_t m = 0; m < (uint8_t)ER_MOVE_COUNT; ++m) p.moves[m] = sp->moves[m];
  p.hp_cur = xp_hp_max(sp->base_hp, level);
}

static void mk_setup(BattleSetup& s, uint32_t seed)
{
  battle_setup_clear(s);
  s.seed     = seed;
  s.count[0] = 3u;
  s.count[1] = 3u;
  for (uint8_t i = 0; i < 3u; ++i) {
    mk_member(s.member[0][i], T_A[i], (uint8_t)REPLAY_LEVEL, 0x1000u + i);
    mk_member(s.member[1][i], T_B[i], (uint8_t)REPLAY_LEVEL, 0x2000u + i);
  }
}

// THE SCRIPT. Pure, deterministic, and built only out of the module's public
// const queries - no AI, no RNG, no hidden state.
static BattleAction script(const BattleState& st, uint8_t side)
{
  BattleAction a; a.kind = (uint8_t)BACT_NONE; a.index = 0u;
  const BattleCombatant* u = battle_active(st, side);
  if (u == nullptr) return a;

  if (battle_side_must_switch(st, side)) {
    for (uint8_t i = 0; i < (uint8_t)BATTLE_TEAM_MAX; ++i) {
      const BattleCombatant* c = battle_combatant(st, side, i);
      if (i != st.side[side].active && c != nullptr &&
          (c->flags & BCF_PRESENT) != 0u && (c->flags & BCF_FAINTED) == 0u &&
          c->hp_cur > 0u) {
        a.kind = (uint8_t)BACT_SWITCH; a.index = i; return a;
      }
    }
  }
  // Cycle the four slots so the script exercises cooldowns, buffs and DOTs
  // rather than hammering one move.
  for (uint8_t k = 0; k < (uint8_t)ER_MOVE_COUNT; ++k) {
    const uint8_t slot = (uint8_t)((st.round + side + k) % (uint16_t)ER_MOVE_COUNT);
    if (battle_move_ready(*u, slot)) { a.kind = (uint8_t)BACT_ATTACK; a.index = slot; return a; }
  }
  // Every learnset ships at least one cooldown-free move (battle.h asserts it),
  // so this is unreachable; a switch is the honest fallback anyway.
  for (uint8_t i = 0; i < (uint8_t)BATTLE_TEAM_MAX; ++i) {
    const BattleCombatant* c = battle_combatant(st, side, i);
    if (i != st.side[side].active && c != nullptr &&
        (c->flags & BCF_PRESENT) != 0u && c->hp_cur > 0u) {
      a.kind = (uint8_t)BACT_SWITCH; a.index = i; return a;
    }
  }
  a.kind = (uint8_t)BACT_ATTACK; a.index = 0u;
  return a;
}

// One scripted round. Returns the step result; hashes are the caller's business.
static BattleStepResult scripted_round(BattleState& st, BattleLog* log)
{
  const BattleAction a = script(st, 0u);
  const BattleAction b = script(st, 1u);
  CHECK_EQ(battle_submit_action(st, 0u, a), BR_OK);
  CHECK_EQ(battle_submit_action(st, 1u, b), BR_OK);
  return battle_step_round(st, log);
}

// How many rng_next() steps separate `seed` from `final_s`. Exact, not a
// bound: the xorshift orbit is long enough that a false early match would be a
// 1-in-2^32 accident inside a 4,096-step window.
static uint32_t draws_taken(uint32_t seed, uint32_t final_s)
{
  Rng r; rng_init(r, seed);
  for (uint32_t n = 0; n <= 4096u; ++n) {
    if (r.s == final_s) return n;
    (void)rng_next(r);
  }
  return 0xFFFFFFFFu;
}

// =============================================================================
TEST(two_engines_with_one_seed_and_one_script_agree_on_every_round_hash) {
  BattleSetup s;
  mk_setup(s, REPLAY_SEED);
  BattleState a, b;
  CHECK_EQ(battle_init(a, s), BR_OK);
  CHECK_EQ(battle_init(b, s), BR_OK);
  CHECK_EQ(battle_state_hash(a), battle_state_hash(b));

  int rounds = 0;
  bool over_a = false, over_b = false;
  // INTERLEAVED. A sequential pair would still pass with a file-scope static.
  while (!over_a && rounds < BATTLE_MAX_ROUNDS + 2) {
    const BattleStepResult ra = scripted_round(a, nullptr);
    const BattleStepResult rb = scripted_round(b, nullptr);
    ++rounds;
    CHECK_EQ(ra, rb);
    CHECK_EQ(battle_state_hash(a), battle_state_hash(b));   // EVERY round
    CHECK(memcmp(&a, &b, sizeof a) == 0);
    over_a = (ra == BS_BATTLE_OVER);
    over_b = (rb == BS_BATTLE_OVER);
  }
  CHECK(over_a);
  CHECK(over_b);
  CHECK(rounds >= 3);                       // the script really fought
  CHECK_EQ(a.outcome, b.outcome);
  CHECK(a.outcome != (uint8_t)BO_UNDECIDED);
  CHECK(a.outcome != (uint8_t)BO_ABORT);
}

TEST(a_neighbouring_seed_diverges_early_and_reaches_a_different_outcome) {
  BattleSetup s0, s1;
  mk_setup(s0, REPLAY_SEED);
  mk_setup(s1, REPLAY_SEED + 1u);
  BattleState a, b;
  CHECK_EQ(battle_init(a, s0), BR_OK);
  CHECK_EQ(battle_init(b, s1), BR_OK);
  // Identical teams, so the two states differ ONLY in rng.s at round 0.
  CHECK(battle_state_hash(a) != battle_state_hash(b));

  int first_diff = 0;
  for (int r = 1; r <= 8; ++r) {
    const BattleStepResult ra = scripted_round(a, nullptr);
    const BattleStepResult rb = scripted_round(b, nullptr);
    if (first_diff == 0 && memcmp(a.side, b.side, sizeof a.side) != 0) first_diff = r;
    if (ra == BS_BATTLE_OVER || rb == BS_BATTLE_OVER) break;
  }
  // MEASURED AT 4 for this script and this pair of seeds, and it is 4 rather
  // than 1 for an honest reason: the damage roll is only three wide, so two
  // streams agree on it about a third of the time and a couple of rounds can
  // match by luck. Against a 23-round battle that is still early, and the
  // sweep below is the part that proves the seed reaches the OUTCOME.
  CHECK_EQ(first_diff, 4);

  // TWELVE NEIGHBOURING SEEDS. Every one must diverge inside eight rounds, and
  // the batch must not all end the same way - otherwise "two engines agree"
  // would be a claim about an engine the RNG never touches.
  int outcomes[8];
  memset(outcomes, 0, sizeof outcomes);
  for (uint32_t k = 1; k <= 12u; ++k) {
    BattleSetup sk;
    mk_setup(sk, REPLAY_SEED + k);
    BattleState x, y;
    CHECK_EQ(battle_init(x, s0), BR_OK);
    CHECK_EQ(battle_init(y, sk), BR_OK);
    int d = 0;
    for (int r = 1; r <= 8; ++r) {
      const BattleStepResult rx = scripted_round(x, nullptr);
      const BattleStepResult ry = scripted_round(y, nullptr);
      if (d == 0 && memcmp(x.side, y.side, sizeof x.side) != 0) d = r;
      if (rx == BS_BATTLE_OVER || ry == BS_BATTLE_OVER) break;
    }
    CHECK(d != 0);
    int guard = 0;
    while (y.outcome == (uint8_t)BO_UNDECIDED && guard++ < 70) scripted_round(y, nullptr);
    if (y.outcome < 8) outcomes[y.outcome]++;
  }
  int distinct = 0;
  for (int i = 0; i < 8; ++i) if (outcomes[i] > 0) distinct++;
  CHECK(distinct >= 2);
}

TEST(the_random_cursor_and_the_exact_draw_count_are_pinned) {
  BattleSetup s;
  mk_setup(s, REPLAY_SEED);
  BattleState st;
  CHECK_EQ(battle_init(st, s), BR_OK);
  CHECK_EQ(st.rng.s, REPLAY_SEED);           // rng_init keeps a non-zero seed

  int rounds = 0;
  while (scripted_round(st, nullptr) != BS_BATTLE_OVER) {
    if (++rounds > BATTLE_MAX_ROUNDS + 2) break;
  }
  ++rounds;

  const uint32_t draws = draws_taken(REPLAY_SEED, st.rng.s);
  CHECK(draws != 0xFFFFFFFFu);
  // THE DOCUMENTED BUDGET: at most five draws per round (one order tie-break
  // plus two per action). This is the bound battle.h publishes.
  CHECK(draws <= (uint32_t)(5 * rounds));
  // AND THE EXACT VALUES, which is what an inserted draw moves. Both numbers
  // are recorded from this build and any change to the round is a change here.
  CHECK_EQ(rounds, 23);
  CHECK_EQ(draws, 74);
  CHECK_EQ(st.rng.s, 0xAC8778B5u);
  CHECK_EQ(st.outcome, BO_WIN_B);
}

TEST(a_battle_never_touches_a_named_rng_stream) {
  // The other half of the check.sh grep gate, at runtime: the module must draw
  // ONLY from st.rng. RNG_BATTLE is the named global stream and a battle must
  // not touch it, or a care roll on one device would move the other device's
  // damage numbers.
  rng_seed_all(0x1234u);
  uint32_t before[RNG_STREAM_COUNT];
  for (uint8_t i = 0; i < (uint8_t)RNG_STREAM_COUNT; ++i) before[i] = rng_u32((RngStream)i);
  rng_seed_all(0x1234u);
  for (uint8_t i = 0; i < (uint8_t)RNG_STREAM_COUNT; ++i) (void)i;

  BattleSetup s;
  mk_setup(s, REPLAY_SEED);
  BattleState st;
  CHECK_EQ(battle_init(st, s), BR_OK);
  int guard = 0;
  while (scripted_round(st, nullptr) != BS_BATTLE_OVER) { if (++guard > 70) break; }

  // Every named stream must still hand back exactly what it would have handed
  // back had the battle never happened.
  for (uint8_t i = 0; i < (uint8_t)RNG_STREAM_COUNT; ++i) {
    CHECK_EQ(rng_u32((RngStream)i), before[i]);
  }
}

TEST(replaying_the_recorded_actions_reproduces_the_final_state_exactly) {
  BattleSetup s;
  mk_setup(s, REPLAY_SEED);
  BattleState live;
  CHECK_EQ(battle_init(live, s), BR_OK);

  static BattleEvent buf[8192];
  BattleLog log;
  battle_log_init(log, buf, (uint16_t)8192);
  int guard = 0;
  while (scripted_round(live, &log) != BS_BATTLE_OVER) { if (++guard > 70) break; }
  CHECK_EQ(log.dropped, 0);
  const uint32_t live_hash = battle_state_hash(live);

  static BattleEvent rbuf[8192];
  BattleLog rlog;
  battle_log_init(rlog, rbuf, (uint16_t)8192);
  BattleState out;
  BattleReplayReport rep;
  CHECK_EQ(battle_replay(s, log, out, &rlog, rep), BR_OK);
  CHECK_EQ(rep.first_bad_round, 0);
  CHECK_EQ(rep.final_hash, live_hash);
  CHECK_EQ(rep.outcome, live.outcome);
  CHECK(memcmp(&live, &out, sizeof out) == 0);
  CHECK_EQ(rep.rounds, live.round - 1);

  // A replay with NO log of its own must land on the same state: the log is not
  // a back channel into the rules on the replay path either.
  BattleState out2;
  BattleReplayReport rep2;
  CHECK_EQ(battle_replay(s, log, out2, nullptr, rep2), BR_OK);
  CHECK(memcmp(&out, &out2, sizeof out) == 0);
}

TEST(one_changed_action_at_round_k_is_reported_at_exactly_round_k) {
  BattleSetup s;
  mk_setup(s, REPLAY_SEED);
  BattleState live;
  CHECK_EQ(battle_init(live, s), BR_OK);
  static BattleEvent buf[8192];
  BattleLog log;
  battle_log_init(log, buf, (uint16_t)8192);
  int guard = 0;
  while (scripted_round(live, &log) != BS_BATTLE_OVER) { if (++guard > 70) break; }
  CHECK(log.count > 0);

  for (uint16_t k = 2u; k <= 4u; ++k) {
    // A private copy of the transcript with side 0's action at round k moved to
    // a DIFFERENT, still legal, move slot.
    static BattleEvent mbuf[8192];
    BattleLog mlog = log;
    mlog.ev = mbuf;
    memcpy(mbuf, buf, sizeof(BattleEvent) * log.cap);
    bool changed = false;
    for (uint16_t i = 0; i < mlog.count; ++i) {
      BattleEvent* e = (BattleEvent*)battle_log_at(mlog, i);
      if (e && e->kind == (uint8_t)RLE_ACTION && e->side == 0u && e->round == k &&
          e->a == (uint8_t)BACT_ATTACK) {
        e->b = (uint16_t)((e->b + 1u) % (uint16_t)ER_MOVE_COUNT);
        changed = true;
        break;
      }
    }
    if (!changed) continue;

    BattleState out;
    BattleReplayReport rep;
    const BattleReject r = battle_replay(s, mlog, out, nullptr, rep);
    // Either the changed action is illegal there (the log is then not a
    // transcript of anything this engine could produce) or it replays and the
    // FIRST disagreeing hash is round k. Both are named outcomes; neither is a
    // silent success.
    if (r == BR_OK) {
      CHECK_EQ(rep.first_bad_round, k);
    } else {
      CHECK_EQ(r, BR_LOG_INCOMPLETE);
    }
  }
}

TEST(a_replay_refuses_a_foreign_engine_a_foreign_pack_and_a_holed_log_by_name) {
  BattleSetup s;
  mk_setup(s, REPLAY_SEED);
  BattleState live;
  CHECK_EQ(battle_init(live, s), BR_OK);
  static BattleEvent buf[8192];
  BattleLog log;
  battle_log_init(log, buf, (uint16_t)8192);
  int guard = 0;
  while (scripted_round(live, &log) != BS_BATTLE_OVER) { if (++guard > 70) break; }

  BattleState out;
  BattleReplayReport rep;
  // POSITIVE CONTROL FIRST.
  CHECK_EQ(battle_replay(s, log, out, nullptr, rep), BR_OK);

  BattleSetup bad = s;
  bad.engine_ver = (uint16_t)(BATTLE_ENGINE_VER + 1u);
  CHECK_EQ(battle_replay(bad, log, out, nullptr, rep), BR_VERSION_MISMATCH);
  bad = s;
  bad.content_ver = (uint16_t)(CONTENT_VERSION ^ 0x5Au);
  CHECK_EQ(battle_replay(bad, log, out, nullptr, rep), BR_VERSION_MISMATCH);

  // A ring that dropped events is not a transcript.
  BattleLog holed = log;
  holed.dropped = 1u;
  CHECK_EQ(battle_replay(s, holed, out, nullptr, rep), BR_LOG_INCOMPLETE);

  // ...and one that simply stops in the middle is refused for the same reason.
  BattleLog cut = log;
  cut.count = (uint16_t)(log.count / 2u);
  cut.head  = cut.count;
  const BattleReject rc = battle_replay(s, cut, out, nullptr, rep);
  CHECK_EQ(rc, BR_LOG_INCOMPLETE);

  // AND THE CONTROL AGAIN.
  CHECK_EQ(battle_replay(s, log, out, nullptr, rep), BR_OK);
}

TEST(the_localisation_ladder_tells_a_round_that_started_wrong_from_one_that_went_wrong) {
  // THE TWO RUNGS, and each needs its own case. A changed ACTION shows up in
  // the round's hash_before, so it is caught by that comparison alone - which
  // is why deleting the hash_after comparison left the case above green. A
  // round whose ENGINE diverged shows up only in hash_after. Both rungs are
  // exercised here by corrupting one recorded hash at a time.
  BattleSetup s;
  mk_setup(s, REPLAY_SEED);
  BattleState live;
  CHECK_EQ(battle_init(live, s), BR_OK);
  static BattleEvent buf[8192];
  BattleLog log;
  battle_log_init(log, buf, (uint16_t)8192);
  int guard = 0;
  while (scripted_round(live, &log) != BS_BATTLE_OVER) { if (++guard > 70) break; }

  BattleState out;
  BattleReplayReport rep;
  CHECK_EQ(battle_replay(s, log, out, nullptr, rep), BR_OK);
  CHECK_EQ(rep.first_bad_round, 0);                      // the control

  for (int which = 0; which < 2; ++which) {
    const uint8_t want = (which == 0) ? (uint8_t)RLE_ROUND_BEGIN : (uint8_t)RLE_ROUND_END;
    static BattleEvent mbuf[8192];
    BattleLog mlog = log;
    mlog.ev = mbuf;
    memcpy(mbuf, buf, sizeof(BattleEvent) * log.cap);
    bool touched = false;
    for (uint16_t i = 0; i < mlog.count; ++i) {
      BattleEvent* e = (BattleEvent*)battle_log_at(mlog, i);
      if (e && e->kind == want && e->round == 3u) { e->v ^= 0x00010000u; touched = true; break; }
    }
    CHECK(touched);
    BattleState o2;
    BattleReplayReport r2;
    CHECK_EQ(battle_replay(s, mlog, o2, nullptr, r2), BR_OK);
    CHECK_EQ(r2.first_bad_round, 3);
    // The replay itself still ran to the same end: a reported mismatch is a
    // REPORT, not an abort.
    CHECK_EQ(r2.final_hash, rep.final_hash);
  }
}
