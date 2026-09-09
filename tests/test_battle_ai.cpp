// =============================================================================
//  ERRATA host test - test_battle_ai.cpp
//  THE LOCAL OPPONENT (P4-C3), over the real game/battle_ai.cpp and the real
//  game/battle.cpp.
//
//  FOUR RULES THIS FILE OBEYS, because this project has already shipped SIX
//  assertions that could not fail:
//
//   (a) THE HEADLINE PROPERTY IS NEVER ASSERTED VACUOUSLY. "The AI never
//       produces an invalid action" is trivially true of an AI that produces
//       nothing, so every case that accepts BACT_NONE PAIRS it with a
//       brute-force sweep of all 65,536 action bit patterns proving that not one
//       of them was legal either, and the campaign that submits thousands of
//       real actions asserts a FLOOR on how many it submitted.
//
//   (b) A RANKING IS ASSERTED AS A RANKING. A single choice is consistent with
//       many rankings, so the cases that are about the score assert the EXACT
//       integer score of every candidate as well as the choice. That is what
//       makes a broken type lookup fail here instead of surviving because the
//       right move happened to win for the wrong reason.
//
//   (c) EVERY THRESHOLD IS PINNED ON BOTH SIDES. The 25 % switch rule is
//       asserted at exactly the threshold (stand and fight) and one point below
//       it (switch), so moving the constant or loosening the comparison fails.
//
//   (d) EVERY CASE THAT SAYS "IT DOES NOT DO X" CARRIES THE CONTROL THAT MAKES
//       IT DO X. A test that only ever sees the negative answer passes with the
//       feature deleted.
//
//  CONTROLLED POSITIONS. Most cases build a legal battle with battle_init() and
//  then OVERWRITE combatant fields to get the exact matchup the case is about -
//  the roster does not happen to contain "same stats, three moves of equal power
//  and different types". That is honest for a plain struct and is why the
//  campaign cases run on untouched roster teams.
// =============================================================================
#include "nt_test.h"

#include <string.h>
#include <type_traits>

#include "game/battle.h"
#include "game/battle_ai.h"
#include "game/bug.h"
#include "game/xp.h"

// The three power-35, accuracy-100, no-effect, no-cooldown damage moves - one
// per type. They differ in NOTHING but their type, which is what lets a case
// isolate the type modifier.
#define MV_PING       1u    // SIGNAL  power 35 acc 100
#define MV_BYTAZO     9u    // CORRUPT power 35 acc 100
#define MV_ESCANEO   18u    // SYSTEM  power 35 acc 100
#define MV_CHOQUE    27u    // NEUTRAL power 50 acc 100
#define MV_APUESTA   28u    // NEUTRAL power 100 acc 55, cd 2
#define MV_AMPLIFICAR 6u    // SIGNAL  power 0 acc 100 BUFF_ATK
#define MV_RAFAGA     3u    // SIGNAL  power 75 acc 85

// =============================================================================
//  FIXTURES
// =============================================================================
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

static void mk_setup(BattleSetup& s, uint32_t seed,
                     const uint8_t* a, uint8_t na, const uint8_t* b, uint8_t nb,
                     uint8_t level)
{
  battle_setup_clear(s);
  s.seed     = seed;
  s.count[0] = na;
  s.count[1] = nb;
  for (uint8_t i = 0; i < na; ++i) mk_member(s.member[0][i], a[i], level, 0x1000u + i);
  for (uint8_t i = 0; i < nb; ++i) mk_member(s.member[1][i], b[i], level, 0x2000u + i);
}

// Four real 3v3 matchups from the roster, so the campaign is not one fight run
// 64 times. Every combination of the three types appears.
static const uint8_t TEAM_SIGNAL[3]  = {  1,  4, 13 };
static const uint8_t TEAM_CORRUPT[3] = { 16, 19, 22 };
static const uint8_t TEAM_SYSTEM[3]  = { 31, 34, 32 };
static const uint8_t TEAM_MIXED[3]   = {  9, 24, 33 };

static const uint8_t* const ROSTERS[4] = {
  TEAM_SIGNAL, TEAM_CORRUPT, TEAM_SYSTEM, TEAM_MIXED
};

static void fixture(BattleState& st, uint32_t seed = 0xC0FFEEu, uint8_t level = 10u)
{
  BattleSetup s;
  mk_setup(s, seed, TEAM_SIGNAL, 3u, TEAM_CORRUPT, 3u, level);
  CHECK_EQ(battle_init(st, s), BR_OK);
}

// Overwrites one combatant with a chosen matchup. Used only by the cases whose
// subject is the ranking, never by a campaign case.
static void arm(BattleCombatant& c, uint8_t type, uint8_t atk, uint8_t def, uint8_t spd,
                uint16_t hp, uint8_t m0, uint8_t m1, uint8_t m2, uint8_t m3)
{
  memset(&c, 0, sizeof c);
  c.flags          = BCF_PRESENT;
  c.type           = type;
  c.atk            = atk;
  c.def            = def;
  c.spd            = spd;
  c.hp_max         = hp;
  c.hp_cur         = hp;
  c.moves[0]       = m0; c.moves[1] = m1; c.moves[2] = m2; c.moves[3] = m3;
  c.type_edge_left = (uint8_t)TYPE_MOD_MAX_HITS;
  c.level          = 10u;
  c.species_id     = 1u;
}

// THE VACUITY GUARD. Sweeps all 65,536 (kind, index) bit patterns and answers
// whether ANY of them is legal for this side. A BACT_NONE from the AI is only
// acceptable when this says false.
static bool any_action_is_legal(const BattleState& st, uint8_t side)
{
  for (uint32_t k = 0; k < 256u; ++k) {
    for (uint32_t i = 0; i < 256u; ++i) {
      BattleAction a;
      a.kind  = (uint8_t)k;
      a.index = (uint8_t)i;
      if (battle_validate_action(st, side, a) == BR_OK) return true;
    }
  }
  return false;
}

static bool is_none(BattleAction a)
{
  return a.kind == (uint8_t)BACT_NONE && a.index == 0u;
}

// Runs one AI-vs-AI battle to its end. Returns the rounds resolved. Both actions
// are CHOSEN before either is SUBMITTED, which is the lockstep shape P4-C5 needs:
// neither AI ever sees the other's action.
struct AiRunStats {
  uint16_t rounds;
  uint16_t actions;
  uint16_t ai_draws;      // choices that moved an AI cursor, i.e. real ties
  uint8_t  outcome;
};

static void run_ai_battle(BattleState& st, BattleAi& ai0, BattleAi& ai1,
                          BattleLog* log, AiRunStats& out)
{
  memset(&out, 0, sizeof out);
  for (uint16_t r = 0; r < (uint16_t)(BATTLE_MAX_ROUNDS + 4); ++r) {
    if (st.outcome != (uint8_t)BO_UNDECIDED) break;

    const uint32_t c0 = ai0.rng.s;
    const uint32_t c1 = ai1.rng.s;
    const BattleAction a0 = battle_ai_choose(ai0, st);
    const BattleAction a1 = battle_ai_choose(ai1, st);
    if (ai0.rng.s != c0) ++out.ai_draws;
    if (ai1.rng.s != c1) ++out.ai_draws;

    // THE HEADLINE ASSERTION, on every single action of every single round.
    CHECK_EQ(battle_submit_action(st, 0u, a0), BR_OK);
    CHECK_EQ(battle_submit_action(st, 1u, a1), BR_OK);
    out.actions = (uint16_t)(out.actions + 2u);

    const BattleStepResult sr = battle_step_round(st, log);
    ++out.rounds;
    if (sr == BS_BATTLE_OVER) break;
  }
  out.outcome = st.outcome;
}

// =============================================================================
//  THE OBJECT AND THE TWO STREAMS
// =============================================================================
TEST(the_ai_carries_its_own_stream_in_its_own_object_and_nothing_else)
{
  CHECK_EQ(sizeof(BattleAi), 8u);
  CHECK(std::has_unique_object_representations<BattleAi>::value);

  BattleAi ai;
  memset(&ai, 0xAB, sizeof ai);
  battle_ai_init(ai, 1u, 0x1234u);
  CHECK_EQ(ai.side, 1u);
  CHECK_EQ(ai.reserved[0], 0u);
  CHECK_EQ(ai.reserved[1], 0u);
  CHECK_EQ(ai.reserved[2], 0u);
  CHECK(ai.rng.s != 0u);            // rng_init never leaves the absorbing state

  // Two AIs built from ONE number must not run the same sequence: the side is
  // mixed into the seed for exactly that reason.
  BattleAi a0, a1;
  battle_ai_init(a0, 0u, 0xC0FFEEu);
  battle_ai_init(a1, 1u, 0xC0FFEEu);
  CHECK(a0.rng.s != a1.rng.s);

  // ...and the same side from the same seed IS the same stream, which is what
  // makes an AI reproducible at all.
  BattleAi a0b;
  battle_ai_init(a0b, 0u, 0xC0FFEEu);
  CHECK_EQ(a0.rng.s, a0b.rng.s);
}

TEST(choosing_does_not_move_one_byte_of_the_battle_state)
{
  // The const parameter already makes this a compile-time fact; the case is
  // here because a const_cast would compile and this is what would catch it.
  // It also pins the OTHER half: the battle's random cursor is untouched, so
  // an AI decision costs the battle nothing.
  BattleAi ai;
  battle_ai_init(ai, 0u, 0x777u);

  for (uint32_t seed = 1u; seed <= 24u; ++seed) {
    BattleState st;
    BattleSetup s;
    mk_setup(s, seed, ROSTERS[seed & 3u], 3u, ROSTERS[(seed + 1u) & 3u], 3u, 10u);
    CHECK_EQ(battle_init(st, s), BR_OK);

    BattleState before = st;
    const uint32_t h = battle_state_hash(st);
    const uint32_t cursor = st.rng.s;

    (void)battle_ai_choose(ai, st);

    CHECK_EQ(memcmp(&before, &st, sizeof st), 0);
    CHECK_EQ(battle_state_hash(st), h);
    CHECK_EQ(st.rng.s, cursor);
  }

  // ...AND ON A STATE THAT IS GUARANTEED TO MAKE THE AI DRAW. The loop above
  // only proves the claim on rounds where the ranking happened to be
  // unambiguous, and an AI that reached into st.rng would sail through it. Two
  // identical moves force a tie, so the draw certainly happens - and the battle
  // cursor must still not have moved.
  {
    BattleState st;
    fixture(st);
    arm(st.side[0].team[0], TYPE_SIGNAL, 10u, 5u, 5u, 100u,
        MV_CHOQUE, MV_CHOQUE, 0u, 0u);          // two identical moves: a certain tie
    arm(st.side[1].team[0], TYPE_CORRUPT, 5u, 5u, 5u, 100u,
        MV_BYTAZO, MV_BYTAZO, MV_BYTAZO, MV_BYTAZO);
    CHECK_EQ(battle_ai_move_score(st, 0u, 0u), battle_ai_move_score(st, 0u, 1u));

    BattleAi tied;
    battle_ai_init(tied, 0u, 0xDEADu);
    const BattleState before = st;
    const uint32_t ai_cursor = tied.rng.s;

    const BattleAction a = battle_ai_choose(tied, st);

    CHECK(tied.rng.s != ai_cursor);             // the control: it really drew
    CHECK_EQ(memcmp(&before, &st, sizeof st), 0);
    CHECK_EQ(st.rng.s, before.rng.s);           // ...out of its OWN stream
    CHECK_EQ(battle_validate_action(st, 0u, a), BR_OK);
  }
}

TEST(one_choice_costs_at_most_one_draw_from_the_ais_own_stream)
{
  // The header promises "draws at most once". This pins it exactly: after a
  // choice the AI cursor is either where it was, or exactly ONE xorshift step
  // beyond it - never two. A second draw slipped into the chooser fails here.
  BattleAi ai;
  battle_ai_init(ai, 0u, 0x2468u);

  BattleState st;
  fixture(st, 0xBEEFu, 12u);
  BattleAi other;
  battle_ai_init(other, 1u, 0x1357u);

  uint16_t moved = 0u;
  for (uint16_t r = 0; r < 40u && st.outcome == (uint8_t)BO_UNDECIDED; ++r) {
    const uint32_t before = ai.rng.s;
    Rng probe = { before };
    (void)rng_next(probe);

    const BattleAction a0 = battle_ai_choose(ai, st);
    CHECK(ai.rng.s == before || ai.rng.s == probe.s);
    if (ai.rng.s != before) ++moved;

    const BattleAction a1 = battle_ai_choose(other, st);
    CHECK_EQ(battle_submit_action(st, 0u, a0), BR_OK);
    CHECK_EQ(battle_submit_action(st, 1u, a1), BR_OK);
    if (battle_step_round(st, nullptr) == BS_BATTLE_OVER) break;
  }
  // ...and the AI really did draw, so the assertion above was not answered by a
  // chooser that never touches its stream.
  CHECK(moved > 0u);
}

// =============================================================================
//  NEVER INVALID - the property that matters most
// =============================================================================
TEST(every_action_the_ai_produces_over_sixty_four_battles_is_accepted_by_the_validator)
{
  uint32_t actions = 0u, wins_a = 0u, wins_b = 0u, other = 0u, finished = 0u;
  uint32_t total_rounds = 0u;

  for (uint32_t seed = 1u; seed <= 64u; ++seed) {
    BattleState st;
    BattleSetup s;
    mk_setup(s, seed * 2654435761u, ROSTERS[seed & 3u], 3u,
             ROSTERS[(seed >> 2) & 3u], 3u, (uint8_t)(6u + (seed % 20u)));
    CHECK_EQ(battle_init(st, s), BR_OK);

    BattleAi ai0, ai1;
    battle_ai_init(ai0, 0u, seed * 7u + 1u);
    battle_ai_init(ai1, 1u, seed * 13u + 2u);

    AiRunStats st_out;
    run_ai_battle(st, ai0, ai1, nullptr, st_out);

    actions      += st_out.actions;
    total_rounds += st_out.rounds;

    // A battle the AI drives must END, and it must never end in BO_ABORT -
    // an abort means step 1 found a submitted action had gone stale, i.e. the
    // producer and the validator disagreed after all.
    CHECK(st.outcome != (uint8_t)BO_UNDECIDED);
    CHECK(st.outcome != (uint8_t)BO_ABORT);
    ++finished;
    if (st.outcome == (uint8_t)BO_WIN_A) ++wins_a;
    else if (st.outcome == (uint8_t)BO_WIN_B) ++wins_b;
    else ++other;
  }

  CHECK_EQ(finished, 64u);
  // NOT VACUOUS: this many actions really were produced and really were
  // accepted. An AI that answered BACT_NONE would have failed the CHECK_EQ in
  // run_ai_battle, and one that answered nothing at all would fail here.
  CHECK(actions >= 500u);
  CHECK(total_rounds >= 250u);
  // NOT DEGENERATE either: both sides win somewhere, so the AI is not simply
  // losing on purpose or timing out every fight.
  CHECK(wins_a > 0u);
  CHECK(wins_b > 0u);
  CHECK_EQ(wins_a + wins_b + other, 64u);
}

TEST(the_ai_answers_nothing_only_when_the_sweep_agrees_that_nothing_is_legal)
{
  BattleAi ai;
  battle_ai_init(ai, 0u, 0x99u);

  // --- POSITIVE CONTROL: a healthy battle. The sweep finds legal actions and
  //     the AI returns one of them. Without this, every case below would pass
  //     on a validator that refuses everything.
  {
    BattleState st;
    fixture(st);
    CHECK(any_action_is_legal(st, 0u));
    const BattleAction a = battle_ai_choose(ai, st);
    CHECK(!is_none(a));
    CHECK_EQ(battle_validate_action(st, 0u, a), BR_OK);
  }

  // --- this side has already submitted
  {
    BattleState st;
    fixture(st);
    BattleAction first;
    first.kind = (uint8_t)BACT_ATTACK; first.index = 0u;
    CHECK_EQ(battle_submit_action(st, 0u, first), BR_OK);
    CHECK(!any_action_is_legal(st, 0u));
    CHECK(is_none(battle_ai_choose(ai, st)));
    // ...and the OTHER side is untouched, which is the control for this one.
    CHECK(any_action_is_legal(st, 1u));
  }

  // --- the battle is over
  {
    BattleState st;
    fixture(st);
    st.outcome = (uint8_t)BO_WIN_B;
    CHECK(!any_action_is_legal(st, 0u));
    CHECK(is_none(battle_ai_choose(ai, st)));
  }

  // --- the battle never started
  {
    BattleState st;
    memset(&st, 0, sizeof st);
    CHECK(!any_action_is_legal(st, 0u));
    CHECK(is_none(battle_ai_choose(ai, st)));
  }

  // --- the active fainted and NOTHING is left to switch to (BR_ACTOR_FAINTED)
  {
    BattleState st;
    fixture(st);
    for (uint8_t i = 0; i < (uint8_t)BATTLE_TEAM_MAX; ++i) {
      st.side[0].team[i].hp_cur = 0u;
      st.side[0].team[i].flags  = (uint8_t)(BCF_PRESENT | BCF_FAINTED);
    }
    CHECK(!any_action_is_legal(st, 0u));
    CHECK(is_none(battle_ai_choose(ai, st)));

    // ...and the CONTROL: revive one bench member and the same state suddenly
    // has exactly the switches legal, and the AI takes one.
    st.side[0].team[2].hp_cur = 20u;
    st.side[0].team[2].flags  = BCF_PRESENT;
    CHECK(any_action_is_legal(st, 0u));
    const BattleAction a = battle_ai_choose(ai, st);
    CHECK(!is_none(a));
    CHECK_EQ(a.kind, (uint8_t)BACT_SWITCH);
    CHECK_EQ(a.index, 2u);
    CHECK_EQ(battle_validate_action(st, 0u, a), BR_OK);
  }

  // --- the active slot holds no Bug at all (only reachable from a
  //     wire-supplied state; battle.h keeps BR_EMPTY_ACTIVE for the same reason)
  {
    BattleState st;
    fixture(st);
    st.side[0].active = 1u;
    memset(&st.side[0].team[1], 0, sizeof st.side[0].team[1]);
    CHECK(!any_action_is_legal(st, 0u));
    CHECK(is_none(battle_ai_choose(ai, st)));
  }
}

TEST(an_out_of_range_side_produces_nothing_and_is_never_quietly_corrected)
{
  // Clamping a side to 0 would make the AI choose for the WRONG TEAM, which is
  // a far worse answer than choosing nothing. game/battle.h's never-clamp rule,
  // applied to the producer.
  BattleState st;
  fixture(st);
  const BattleState before = st;

  const uint8_t bad[3] = { 2u, 3u, 255u };
  for (uint8_t i = 0; i < 3u; ++i) {
    BattleAi ai;
    battle_ai_init(ai, bad[i], 0x51u);
    CHECK_EQ(ai.side, bad[i]);                     // stored AS GIVEN
    CHECK(is_none(battle_ai_choose(ai, st)));
    CHECK(!any_action_is_legal(st, bad[i]));       // and nothing was legal anyway
    CHECK_EQ(memcmp(&before, &st, sizeof st), 0);
  }

  // THE CONTROL: side 0 on the very same state answers with a legal action.
  BattleAi good;
  battle_ai_init(good, 0u, 0x51u);
  const BattleAction a = battle_ai_choose(good, st);
  CHECK(!is_none(a));
  CHECK_EQ(battle_validate_action(st, 0u, a), BR_OK);
}

TEST(the_highest_scoring_move_is_skipped_when_it_is_illegal_and_the_validator_is_the_only_reason)
{
  // THE SHAPE THAT MATTERS: the BEST move is the ILLEGAL one. A chooser that
  // ranked first and filtered afterwards - or did not filter at all - would take
  // it, and the ranking assertions below prove it really was the best.
  BattleState st;
  fixture(st);
  arm(st.side[0].team[0], TYPE_SIGNAL, 10u, 10u, 5u, 100u,
      MV_APUESTA, MV_CHOQUE, MV_PING, 0u);
  arm(st.side[1].team[0], TYPE_SIGNAL, 5u, 10u, 5u, 100u,
      MV_CHOQUE, MV_CHOQUE, MV_CHOQUE, MV_CHOQUE);

  const uint32_t s_apuesta = battle_ai_move_score(st, 0u, 0u);
  const uint32_t s_choque  = battle_ai_move_score(st, 0u, 1u);
  const uint32_t s_ping    = battle_ai_move_score(st, 0u, 2u);
  CHECK(s_apuesta > s_choque);
  CHECK(s_choque  > s_ping);

  // ...now put the best one out of reach and nothing else.
  st.side[0].team[0].cooldown[0] = 3u;
  BattleAction probe;
  probe.kind = (uint8_t)BACT_ATTACK; probe.index = 0u;
  CHECK_EQ(battle_validate_action(st, 0u, probe), BR_MOVE_ON_COOLDOWN);

  BattleAi ai;
  battle_ai_init(ai, 0u, 0x31337u);
  const BattleAction a = battle_ai_choose(ai, st);
  CHECK_EQ(a.kind, (uint8_t)BACT_ATTACK);
  CHECK_EQ(a.index, 1u);                    // the best LEGAL move, not the best move
  CHECK_EQ(battle_validate_action(st, 0u, a), BR_OK);

  // THE POSITIVE CONTROL: clear the cooldown and the same AI on the same state
  // takes slot 0 after all, so the case above is about legality and not about
  // slot 1 being preferred for some other reason.
  st.side[0].team[0].cooldown[0] = 0u;
  BattleAi ai2;
  battle_ai_init(ai2, 0u, 0x31337u);
  const BattleAction b = battle_ai_choose(ai2, st);
  CHECK_EQ(b.kind, (uint8_t)BACT_ATTACK);
  CHECK_EQ(b.index, 0u);

  // The score itself is deliberately NOT a legality check: it answers what a
  // move is WORTH, and the validator answers whether it may be used. Stated
  // here because a reader will otherwise expect a cooldown to zero the score.
  st.side[0].team[0].cooldown[0] = 3u;
  CHECK_EQ(battle_ai_move_score(st, 0u, 0u), s_apuesta);
}

TEST(a_switch_to_a_corpse_or_an_empty_slot_is_never_chosen_even_when_it_would_rank_first)
{
  // A fainted bench member with a WINNING type: its switch score is the highest
  // of the three, and the AI must still never name it.
  BattleState st;
  fixture(st);
  arm(st.side[1].team[0], TYPE_CORRUPT, 5u, 5u, 5u, 100u,
      MV_BYTAZO, MV_BYTAZO, MV_BYTAZO, MV_BYTAZO);
  arm(st.side[0].team[0], TYPE_SYSTEM, 5u, 5u, 5u, 100u,
      MV_ESCANEO, 0u, 0u, 0u);
  arm(st.side[0].team[1], TYPE_SIGNAL, 5u, 5u, 5u, 100u,
      MV_PING, 0u, 0u, 0u);
  memset(&st.side[0].team[2], 0, sizeof st.side[0].team[2]);   // empty slot

  // Slot 1 is SIGNAL against CORRUPT: the best possible bench score.
  CHECK_EQ(battle_ai_switch_score(st, 0u, 1u), 2u * BATTLE_AI_SWITCH_TYPE_WEIGHT + 100u);
  st.side[0].team[0].hp_cur = 1u;            // well under the 25 % rule
  BattleAi ai;
  battle_ai_init(ai, 0u, 0x600Du);
  const BattleAction want = battle_ai_choose(ai, st);
  CHECK_EQ(want.kind, (uint8_t)BACT_SWITCH);
  CHECK_EQ(want.index, 1u);                  // the control: it WOULD switch there

  // ...now faint that same slot. Its score does not change, its legality does.
  st.side[0].team[1].hp_cur = 0u;
  st.side[0].team[1].flags  = (uint8_t)(BCF_PRESENT | BCF_FAINTED);
  CHECK_EQ(battle_ai_switch_score(st, 0u, 1u), 2u * BATTLE_AI_SWITCH_TYPE_WEIGHT + 0u);

  BattleAi ai2;
  battle_ai_init(ai2, 0u, 0x600Du);
  const BattleAction a = battle_ai_choose(ai2, st);
  CHECK(a.kind != (uint8_t)BACT_SWITCH);     // slot 1 dead, slot 2 empty, slot 0 is self
  CHECK_EQ(a.kind, (uint8_t)BACT_ATTACK);
  CHECK_EQ(battle_validate_action(st, 0u, a), BR_OK);
}

// =============================================================================
//  GREEDY EXPECTED DAMAGE, WITH THE TYPE MODIFIER
// =============================================================================
TEST(the_type_modifier_decides_between_three_moves_that_differ_in_nothing_else)
{
  // Ping, Bytazo and Escaneo are power 35, accuracy 100, no effect, no cooldown,
  // one per type. Against each of the three defender types a DIFFERENT one of
  // them is the advantaged move, so all three arms of the type chart are walked
  // and no single wrong answer can satisfy the case.
  //
  // The literals: atk_eff 10, def_eff 5, so raw = 35*10/(5*7) = 10.
  //   advantage    10 * 5/4 = 12  ->  half 2*12 + (3-1) = 26  ->  x100 = 2600
  //   neutral      10 * 1/1 = 10  ->  half 22            ->  x100 = 2200
  //   disadvantage 10 * 4/5 =  8  ->  half 18            ->  x100 = 1800
  const uint8_t def_types[3] = { TYPE_CORRUPT, TYPE_SYSTEM, TYPE_SIGNAL };
  //                             Ping wins     Bytazo wins   Escaneo wins
  const uint8_t winner[3]    = { 0u, 1u, 2u };
  const uint32_t want[3][3]  = {
    { 2600u, 2200u, 1800u },   // vs CORRUPT: SIGNAL > CORRUPT > SYSTEM
    { 1800u, 2600u, 2200u },   // vs SYSTEM:  CORRUPT > SYSTEM  > SIGNAL
    { 2200u, 1800u, 2600u },   // vs SIGNAL:  SYSTEM  > SIGNAL  > CORRUPT
  };

  for (uint8_t t = 0; t < 3u; ++t) {
    BattleState st;
    fixture(st);
    arm(st.side[0].team[0], TYPE_SIGNAL, 10u, 5u, 5u, 100u,
        MV_PING, MV_BYTAZO, MV_ESCANEO, 0u);
    arm(st.side[1].team[0], def_types[t], 5u, 5u, 5u, 100u,
        MV_CHOQUE, MV_CHOQUE, MV_CHOQUE, MV_CHOQUE);

    // THE RANKING, asserted as a ranking and by exact integer.
    for (uint8_t m = 0; m < 3u; ++m) CHECK_EQ(battle_ai_move_score(st, 0u, m), want[t][m]);
    // The empty fourth slot is worth nothing and is not legal either.
    CHECK_EQ(battle_ai_move_score(st, 0u, 3u), 0u);

    BattleAi ai;
    battle_ai_init(ai, 0u, 0x4321u);
    const BattleAction a = battle_ai_choose(ai, st);
    CHECK_EQ(a.kind, (uint8_t)BACT_ATTACK);
    CHECK_EQ(a.index, winner[t]);
    CHECK_EQ(battle_validate_action(st, 0u, a), BR_OK);
  }
}

TEST(a_spent_type_edge_stops_the_ai_paying_for_an_advantage_it_can_no_longer_get)
{
  // data/balance.h caps the type edge at TYPE_MOD_MAX_HITS per ATTACKING
  // COMBATANT. Once the budget is gone the engine multiplies by 1 whatever the
  // chart says, so an estimate that still read the chart would rank a move it
  // cannot get an advantage from above one it can.
  BattleState st;
  fixture(st);
  arm(st.side[0].team[0], TYPE_SIGNAL, 10u, 5u, 5u, 100u,
      MV_PING, MV_BYTAZO, MV_ESCANEO, 0u);
  arm(st.side[1].team[0], TYPE_CORRUPT, 5u, 5u, 5u, 100u,
      MV_CHOQUE, MV_CHOQUE, MV_CHOQUE, MV_CHOQUE);

  // With budget: the chart separates them (the control).
  CHECK_EQ(st.side[0].team[0].type_edge_left, (uint8_t)TYPE_MOD_MAX_HITS);
  CHECK_EQ(battle_ai_move_score(st, 0u, 0u), 2600u);
  CHECK_EQ(battle_ai_move_score(st, 0u, 2u), 1800u);

  // Budget spent: all three collapse onto the neutral figure, advantage AND
  // disadvantage alike - which is what `if m != 0` means in the engine.
  st.side[0].team[0].type_edge_left = 0u;
  CHECK_EQ(battle_ai_move_score(st, 0u, 0u), 2200u);
  CHECK_EQ(battle_ai_move_score(st, 0u, 1u), 2200u);
  CHECK_EQ(battle_ai_move_score(st, 0u, 2u), 2200u);

  // Reading the budget must not SPEND it: only the engine spends.
  CHECK_EQ(st.side[0].team[0].type_edge_left, 0u);
  st.side[0].team[0].type_edge_left = 1u;
  CHECK_EQ(battle_ai_move_score(st, 0u, 0u), 2600u);
  CHECK_EQ(st.side[0].team[0].type_edge_left, 1u);
}

TEST(accuracy_is_part_of_the_score_and_the_ai_takes_the_weaker_move_that_lands)
{
  // Apuesta is power 100 accuracy 55; Choque is power 50 accuracy 100. On raw
  // damage Apuesta wins by nearly two to one. Give the defender a speed lead and
  // evasion drives Apuesta's accuracy to the ACCURACY_MIN floor while Choque
  // keeps most of its own, and the ORDER FLIPS - which only happens if accuracy
  // is in the score at all.
  //
  // THE FIXTURE MOVED AT P9-C4 AND THE PROPERTY DID NOT. This case used ONE
  // defender at spd 20 (a 15-point gap, capped to EVASION_MAX_SPD_GAP) and read
  // the flip off it; that stopped working when EVASION_PER_SPD went 2 -> 3, and
  // it stopped working for a reason worth writing down rather than patching
  // around. With hA and hC the two half-point damage figures, the flip needs
  //     55*hA > 100*hC   (no gap: the power move wins)
  //     accA*hA < accC*hC (with the gap: the accurate move wins)
  // and at a FULL-CAP gap accA is floored to 40 while accC is 100 - 3*10 = 70,
  // so the second line needs hA/hC < 1.75 and the first needs hA/hC > 1.818.
  // THE BAND IS EMPTY: at the cap, evasion is now strong enough that no pair of
  // damages can show the flip. A smaller gap re-opens it, so the case is split
  // into the two things it was always asserting at once - the CAP AND FLOOR of
  // the accuracy rule, and the ORDERING - each on the gap that can show it.
  //
  // atk_eff 10, def_eff 10 -> divisor 70, and both are half-POINT scores:
  //   Apuesta: 100*10/70 = 14 -> 2*14 + 2 = 30
  //   Choque :  50*10/70 =  7 -> 2*7  + 2 = 16
  BattleState st;
  fixture(st);
  arm(st.side[0].team[0], TYPE_SIGNAL, 10u, 10u, 5u, 100u,
      MV_APUESTA, MV_CHOQUE, 0u, 0u);
  arm(st.side[1].team[0], TYPE_SIGNAL, 5u, 10u, 20u, 100u,
      MV_CHOQUE, MV_CHOQUE, MV_CHOQUE, MV_CHOQUE);

  const AttackDef* apuesta = attack_get(MV_APUESTA);
  const AttackDef* choque  = attack_get(MV_CHOQUE);
  CHECK(apuesta != nullptr && choque != nullptr);

  // (1) THE CAP AND THE FLOOR, both ends of balance.h's rule, at a 15-point gap
  // that EVASION_MAX_SPD_GAP clamps to 10: penalty 3*10 = 30.
  CHECK_EQ(battle_accuracy_eff(st.side[0].team[0], st.side[1].team[0], *apuesta),
           (uint8_t)ACCURACY_MIN);                       // 55 - 30 = 25, floored
  CHECK_EQ(battle_accuracy_eff(st.side[0].team[0], st.side[1].team[0], *choque),
           (uint8_t)(100u - EVASION_PER_SPD * EVASION_MAX_SPD_GAP));
  CHECK_EQ(battle_ai_move_score(st, 0u, 0u), 30u * (uint32_t)ACCURACY_MIN);
  CHECK_EQ(battle_ai_move_score(st, 0u, 1u),
           16u * (uint32_t)(100u - EVASION_PER_SPD * EVASION_MAX_SPD_GAP));

  // (2) THE ORDER FLIP, on a 6-point gap: penalty 18, so Apuesta lands at
  // 55 - 18 = 37 and is STILL floored to 40 while Choque keeps 82.
  //   Apuesta 30 * 40 = 1200      Choque 16 * 82 = 1312
  st.side[1].team[0].spd = 11u;
  CHECK_EQ(battle_accuracy_eff(st.side[0].team[0], st.side[1].team[0], *apuesta),
           (uint8_t)ACCURACY_MIN);
  CHECK_EQ(battle_accuracy_eff(st.side[0].team[0], st.side[1].team[0], *choque), 82u);
  CHECK_EQ(battle_ai_move_score(st, 0u, 0u), 1200u);
  CHECK_EQ(battle_ai_move_score(st, 0u, 1u), 1312u);

  BattleAi ai;
  battle_ai_init(ai, 0u, 0x1111u);
  const BattleAction a = battle_ai_choose(ai, st);
  CHECK_EQ(a.kind, (uint8_t)BACT_ATTACK);
  CHECK_EQ(a.index, 1u);

  // (3) THE CONTROL: take the speed gap away and the raw-power move wins again,
  // so the flip above was evasion and not something about slot 1.
  st.side[1].team[0].spd = 5u;
  CHECK_EQ(battle_ai_move_score(st, 0u, 0u), 30u * 55u);
  CHECK_EQ(battle_ai_move_score(st, 0u, 1u), 16u * 100u);
  CHECK(30u * 55u > 16u * 100u);        // the band's first line, as an assertion
  BattleAi ai2;
  battle_ai_init(ai2, 0u, 0x1111u);
  CHECK_EQ(battle_ai_choose(ai2, st).index, 0u);
}

TEST(a_protected_defender_halves_every_estimate_and_the_ai_still_ranks_them_the_same)
{
  BattleState st;
  fixture(st);
  arm(st.side[0].team[0], TYPE_SIGNAL, 10u, 5u, 5u, 100u,
      MV_PING, MV_BYTAZO, MV_ESCANEO, 0u);
  arm(st.side[1].team[0], TYPE_CORRUPT, 5u, 5u, 5u, 100u,
      MV_CHOQUE, MV_CHOQUE, MV_CHOQUE, MV_CHOQUE);

  CHECK_EQ(battle_ai_move_score(st, 0u, 0u), 2600u);
  st.side[1].team[0].protect_left = 1u;
  // Half-points halved: 26 -> 13, 22 -> 11, 18 -> 9.
  CHECK_EQ(battle_ai_move_score(st, 0u, 0u), 1300u);
  CHECK_EQ(battle_ai_move_score(st, 0u, 1u), 1100u);
  CHECK_EQ(battle_ai_move_score(st, 0u, 2u), 900u);

  BattleAi ai;
  battle_ai_init(ai, 0u, 0x2222u);
  CHECK_EQ(battle_ai_choose(ai, st).index, 0u);
}

TEST(a_status_move_scores_zero_and_the_ai_still_answers_when_zero_is_all_there_is)
{
  // The stated limit, asserted rather than admitted: a power-0 move is worth
  // nothing to this chooser, so it is taken only when every legal move is.
  BattleState st;
  fixture(st);
  arm(st.side[0].team[0], TYPE_SIGNAL, 10u, 5u, 5u, 100u,
      MV_AMPLIFICAR, MV_CHOQUE, 0u, 0u);
  arm(st.side[1].team[0], TYPE_CORRUPT, 5u, 5u, 5u, 100u,
      MV_CHOQUE, MV_CHOQUE, MV_CHOQUE, MV_CHOQUE);

  CHECK_EQ(battle_ai_move_score(st, 0u, 0u), 0u);
  CHECK(battle_ai_move_score(st, 0u, 1u) > 0u);

  BattleAi ai;
  battle_ai_init(ai, 0u, 0x3333u);
  CHECK_EQ(battle_ai_choose(ai, st).index, 1u);       // the damaging move wins

  // Now the damaging move is out of reach. Every legal score is 0, and the AI
  // must still answer with a LEGAL action rather than with nothing - the branch
  // where the tie-break is the only thing choosing.
  st.side[0].team[0].cooldown[1] = 2u;
  BattleAi ai2;
  battle_ai_init(ai2, 0u, 0x3333u);
  const BattleAction a = battle_ai_choose(ai2, st);
  CHECK(!is_none(a));
  CHECK_EQ(battle_validate_action(st, 0u, a), BR_OK);
  CHECK_EQ(a.kind, (uint8_t)BACT_ATTACK);
  CHECK_EQ(a.index, 0u);
}

// =============================================================================
//  THE SWITCH RULE
// =============================================================================
// A fixture for the switch cases: our active is SYSTEM (disadvantaged against
// the CORRUPT foe), bench slot 1 is SIGNAL (advantaged), bench slot 2 is SYSTEM
// (no better). hp_max is 100 everywhere, so "HP < 25 %" is "hp_cur < 25".
static void switch_fixture(BattleState& st, uint16_t active_hp,
                           uint8_t bench1_type = TYPE_SIGNAL)
{
  fixture(st);
  arm(st.side[0].team[0], TYPE_SYSTEM, 5u, 5u, 5u, 100u,
      MV_ESCANEO, MV_CHOQUE, 0u, 0u);
  arm(st.side[0].team[1], bench1_type, 5u, 5u, 5u, 100u, MV_PING, 0u, 0u, 0u);
  arm(st.side[0].team[2], TYPE_SYSTEM, 5u, 5u, 5u, 100u, MV_ESCANEO, 0u, 0u, 0u);
  arm(st.side[1].team[0], TYPE_CORRUPT, 5u, 5u, 5u, 100u,
      MV_BYTAZO, MV_BYTAZO, MV_BYTAZO, MV_BYTAZO);
  st.side[0].team[0].hp_cur = active_hp;
}

TEST(the_twenty_five_percent_threshold_is_strict_and_both_sides_of_it_are_pinned)
{
  // hp_max is 100, so the rule is hp_cur * 100 < 100 * 25, i.e. hp_cur < 25.
  // AT the threshold the Bug stands and fights; ONE POINT BELOW it switches.
  // Moving BATTLE_AI_SWITCH_HP_PCT, or loosening `<` to `<=`, fails one of these
  // two arms.
  {
    BattleState st;
    switch_fixture(st, 25u);
    CHECK(!battle_ai_wants_to_switch(st, 0u));
    BattleAi ai;
    battle_ai_init(ai, 0u, 0x5150u);
    const BattleAction a = battle_ai_choose(ai, st);
    CHECK_EQ(a.kind, (uint8_t)BACT_ATTACK);
  }
  {
    BattleState st;
    switch_fixture(st, 24u);
    CHECK(battle_ai_wants_to_switch(st, 0u));
    BattleAi ai;
    battle_ai_init(ai, 0u, 0x5150u);
    const BattleAction a = battle_ai_choose(ai, st);
    CHECK_EQ(a.kind, (uint8_t)BACT_SWITCH);
    CHECK_EQ(a.index, 1u);                      // the SIGNAL bench member
    CHECK_EQ(battle_validate_action(st, 0u, a), BR_OK);
  }

  // The rule is a FRACTION, not a raw hit-point count: the same 24 HP on a
  // 400-point Bug is 6 % and must still switch, and 200 of 400 is 50 % and
  // must not - so a version that compared raw HP against 25 fails here.
  {
    BattleState st;
    switch_fixture(st, 24u);
    st.side[0].team[0].hp_max = 400u;
    st.side[0].team[0].hp_cur = 200u;
    CHECK(!battle_ai_wants_to_switch(st, 0u));
    st.side[0].team[0].hp_cur = 24u;
    CHECK(battle_ai_wants_to_switch(st, 0u));
    st.side[0].team[0].hp_cur = 100u;           // exactly 25 %
    CHECK(!battle_ai_wants_to_switch(st, 0u));
    st.side[0].team[0].hp_cur = 99u;
    CHECK(battle_ai_wants_to_switch(st, 0u));
  }
}

TEST(a_hurt_bug_with_no_better_type_on_the_bench_stands_and_fights)
{
  // The rule is an AND, and this is the half the threshold case cannot see.
  BattleState st;
  switch_fixture(st, 1u, TYPE_SYSTEM);        // bench slot 1 is now no better
  CHECK(!battle_ai_wants_to_switch(st, 0u));
  BattleAi ai;
  battle_ai_init(ai, 0u, 0x7070u);
  CHECK_EQ(battle_ai_choose(ai, st).kind, (uint8_t)BACT_ATTACK);

  // ...and a bench member that is WORSE is not a reason to switch either.
  st.side[0].team[1].type = TYPE_CORRUPT;     // CORRUPT vs CORRUPT is neutral: better
  CHECK(battle_ai_wants_to_switch(st, 0u));   // (control: neutral beats our -1)
  st.side[0].team[1].type = TYPE_SYSTEM;
  st.side[0].team[2].type = TYPE_SYSTEM;
  CHECK(!battle_ai_wants_to_switch(st, 0u));

  // THE CONTROL that proves the fixture can switch at all: give slot 1 the
  // winning type back and the same Bug at the same 1 HP leaves.
  st.side[0].team[1].type = TYPE_SIGNAL;
  CHECK(battle_ai_wants_to_switch(st, 0u));
  BattleAi ai2;
  battle_ai_init(ai2, 0u, 0x7070u);
  const BattleAction a = battle_ai_choose(ai2, st);
  CHECK_EQ(a.kind, (uint8_t)BACT_SWITCH);
  CHECK_EQ(a.index, 1u);
}

TEST(a_better_type_that_cannot_legally_be_switched_to_is_not_a_reason_to_switch)
{
  // The hole a mutation found: every earlier fixture's better-typed bench member
  // was also a LEGAL one, so rule 3 scanning the bench without asking the
  // validator changed nothing and no test could tell. Here the ONLY better type
  // on the bench is a corpse, and the only living bench member is no better - so
  // a validator-less scan would leave a healthy-typed attacker and switch to a
  // worse matchup for no reason at all.
  BattleState st;
  switch_fixture(st, 1u, TYPE_SIGNAL);        // slot 1 SIGNAL (better), slot 2 SYSTEM
  CHECK(battle_ai_wants_to_switch(st, 0u));   // the control, while slot 1 lives

  st.side[0].team[1].hp_cur = 0u;
  st.side[0].team[1].flags  = (uint8_t)(BCF_PRESENT | BCF_FAINTED);
  BattleAction probe;
  probe.kind = (uint8_t)BACT_SWITCH; probe.index = 1u;
  CHECK_EQ(battle_validate_action(st, 0u, probe), BR_SWITCH_TO_FAINTED);
  // Its TYPE is still the better one - only its legality changed.
  CHECK(type_mod_of(st.side[0].team[1].type, st.side[1].team[0].type) >
        type_mod_of(st.side[0].team[0].type, st.side[1].team[0].type));

  CHECK(!battle_ai_wants_to_switch(st, 0u));
  BattleAi ai;
  battle_ai_init(ai, 0u, 0xC1C1u);
  const BattleAction a = battle_ai_choose(ai, st);
  CHECK_EQ(a.kind, (uint8_t)BACT_ATTACK);     // stands and fights at 1 HP
  CHECK_EQ(battle_validate_action(st, 0u, a), BR_OK);

  // The same for an EMPTY slot, which is the other way a bench member is not
  // there: give slot 2 the winning type and take the Bug away.
  st.side[0].team[1].flags = 0u;
  st.side[0].team[2].type  = TYPE_SIGNAL;
  CHECK(battle_ai_wants_to_switch(st, 0u));   // control: slot 2 lives and is better
  memset(&st.side[0].team[2], 0, sizeof st.side[0].team[2]);
  CHECK(!battle_ai_wants_to_switch(st, 0u));
}

TEST(the_three_public_queries_range_check_before_they_index)
{
  // Defence on functions P4-C4 and P4-C5 will call with values this module did
  // not choose. The fixture is arranged so a MISSING guard reads a neighbouring
  // field that is NOT zero and would therefore answer with a real score: move
  // slot 4 lands on cooldown[0], so that byte is set to a valid attack id.
  BattleState st;
  fixture(st);
  arm(st.side[0].team[0], TYPE_SIGNAL, 10u, 5u, 5u, 100u,
      MV_CHOQUE, MV_CHOQUE, MV_CHOQUE, MV_CHOQUE);
  arm(st.side[1].team[0], TYPE_CORRUPT, 5u, 5u, 5u, 100u,
      MV_BYTAZO, MV_BYTAZO, MV_BYTAZO, MV_BYTAZO);
  st.side[0].team[0].cooldown[0] = MV_CHOQUE;      // the byte just past moves[3]

  CHECK(battle_ai_move_score(st, 0u, 3u) > 0u);    // the control: slot 3 is real
  CHECK_EQ(battle_ai_move_score(st, 0u, (uint8_t)ER_MOVE_COUNT), 0u);
  CHECK_EQ(battle_ai_move_score(st, 0u, 255u), 0u);
  CHECK_EQ(battle_ai_move_score(st, 2u, 0u), 0u);
  CHECK_EQ(battle_ai_move_score(st, 255u, 0u), 0u);

  CHECK(battle_ai_switch_score(st, 0u, 1u) > 0u);  // the control
  CHECK_EQ(battle_ai_switch_score(st, 0u, (uint8_t)BATTLE_TEAM_MAX), 0u);
  CHECK_EQ(battle_ai_switch_score(st, 0u, 255u), 0u);
  CHECK_EQ(battle_ai_switch_score(st, 2u, 0u), 0u);
  CHECK_EQ(battle_ai_switch_score(st, 255u, 0u), 0u);

  CHECK_EQ(battle_ai_wants_to_switch(st, 2u), false);
  CHECK_EQ(battle_ai_wants_to_switch(st, 255u), false);
}

TEST(a_healthy_bug_never_switches_however_good_the_bench_is)
{
  BattleState st;
  switch_fixture(st, 100u);                   // full health, bench is SIGNAL
  CHECK(!battle_ai_wants_to_switch(st, 0u));
  BattleAi ai;
  battle_ai_init(ai, 0u, 0x8080u);
  CHECK_EQ(battle_ai_choose(ai, st).kind, (uint8_t)BACT_ATTACK);

  // THE CONTROL: only the health changes, and the answer flips.
  st.side[0].team[0].hp_cur = 10u;
  CHECK(battle_ai_wants_to_switch(st, 0u));
  BattleAi ai2;
  battle_ai_init(ai2, 0u, 0x8080u);
  CHECK_EQ(battle_ai_choose(ai2, st).kind, (uint8_t)BACT_SWITCH);
}

TEST(a_fainted_active_is_replaced_and_the_validator_is_the_only_thing_that_says_so)
{
  // The forced replacement is NOT a special case in game/battle_ai.cpp: the
  // validator refuses every attack with BR_MUST_SWITCH, so the attack pool is
  // empty on its own. This asserts that mechanism, not just the outcome.
  BattleState st;
  switch_fixture(st, 100u);
  st.side[0].team[0].hp_cur = 0u;
  st.side[0].team[0].flags  = (uint8_t)(BCF_PRESENT | BCF_FAINTED);

  CHECK(battle_side_must_switch(st, 0u));
  for (uint8_t m = 0; m < 2u; ++m) {
    BattleAction probe;
    probe.kind = (uint8_t)BACT_ATTACK; probe.index = m;
    CHECK_EQ(battle_validate_action(st, 0u, probe), BR_MUST_SWITCH);
  }

  // AND HERE IS THE MECHANISM, not just the outcome: arm the bench so that
  // RULE 3 WOULD SAY NO - nothing on it is better typed than the Bug that
  // just fainted - and the AI must switch anyway, because the attack pool is
  // empty. If the replacement were riding on rule 3, this arm would fail.
  //
  // THE ANSWER HERE IS DELIBERATELY NOT THE FIRST LEGAL SWITCH. Slot 1 is the
  // first candidate and slot 2 is the better one, so a chooser that fell out of
  // its ranking and returned whatever came first would name slot 1 and fail -
  // which is exactly what one earlier mutant did while still looking right.
  st.side[0].team[1].type   = TYPE_SYSTEM;
  st.side[0].team[1].hp_cur = 50u;
  st.side[0].team[2].type   = TYPE_SYSTEM;
  st.side[0].team[2].hp_cur = 100u;
  CHECK(!battle_ai_wants_to_switch(st, 0u));
  // SYSTEM attacking CORRUPT is the DISADVANTAGED corner of the chart, so the
  // type term is 0 and only health separates these two.
  CHECK_EQ(battle_ai_switch_score(st, 0u, 1u), 0u * BATTLE_AI_SWITCH_TYPE_WEIGHT + 50u);
  CHECK_EQ(battle_ai_switch_score(st, 0u, 2u), 0u * BATTLE_AI_SWITCH_TYPE_WEIGHT + 100u);

  BattleAi ai;
  battle_ai_init(ai, 0u, 0x9090u);
  const BattleAction a = battle_ai_choose(ai, st);
  CHECK_EQ(a.kind, (uint8_t)BACT_SWITCH);
  CHECK_EQ(a.index, 2u);                      // the healthier of two equal types
  CHECK_EQ(battle_validate_action(st, 0u, a), BR_OK);

  // The other arm: rule 3 now says yes as well, and the answer is the SAME
  // ranking read the same way - slot 1's winning type now outweighs slot 2's
  // health. Two reasons, one mechanism, which is what "not a special case"
  // means, and the answer MOVES, so the arms are not the same assertion twice.
  st.side[0].team[1].type = TYPE_SIGNAL;
  CHECK(battle_ai_wants_to_switch(st, 0u));
  CHECK_EQ(battle_ai_switch_score(st, 0u, 1u), 2u * BATTLE_AI_SWITCH_TYPE_WEIGHT + 50u);
  BattleAi ai2;
  battle_ai_init(ai2, 0u, 0x9090u);
  const BattleAction b = battle_ai_choose(ai2, st);
  CHECK_EQ(b.kind, (uint8_t)BACT_SWITCH);
  CHECK_EQ(b.index, 1u);
}

TEST(a_replacement_is_picked_by_type_first_and_by_health_second)
{
  // Both keys of battle_ai_switch_score(), and the ORDER between them: a nearly
  // dead Bug with the winning type must outrank a healthy one with a losing
  // type, or BATTLE_AI_SWITCH_TYPE_WEIGHT is not doing its job.
  {
    BattleState st;
    switch_fixture(st, 100u);
    st.side[0].team[0].hp_cur = 0u;
    st.side[0].team[0].flags  = (uint8_t)(BCF_PRESENT | BCF_FAINTED);
    st.side[0].team[1].type   = TYPE_SYSTEM;   // healthy, losing type
    st.side[0].team[2].type   = TYPE_SIGNAL;   // nearly dead, winning type
    st.side[0].team[2].hp_cur = 10u;

    CHECK_EQ(battle_ai_switch_score(st, 0u, 1u), 0u * BATTLE_AI_SWITCH_TYPE_WEIGHT + 100u);
    CHECK_EQ(battle_ai_switch_score(st, 0u, 2u), 2u * BATTLE_AI_SWITCH_TYPE_WEIGHT + 10u);

    BattleAi ai;
    battle_ai_init(ai, 0u, 0xA0A0u);
    const BattleAction a = battle_ai_choose(ai, st);
    CHECK_EQ(a.kind, (uint8_t)BACT_SWITCH);
    CHECK_EQ(a.index, 2u);                     // type beat health
  }
  {
    // Same types, so only health can separate them - and it does. THE TWO
    // BENCH MEMBERS HAVE DIFFERENT hp_max ON PURPOSE: the health term is a
    // FRACTION, and slot 2 holds more raw hit points than slot 1 while being
    // the emptier of the two. A version that compared raw hp_cur would pick
    // slot 2 here, and on a fixture where every hp_max were 100 the two
    // readings would be indistinguishable.
    BattleState st;
    switch_fixture(st, 100u);
    st.side[0].team[0].hp_cur = 0u;
    st.side[0].team[0].flags  = (uint8_t)(BCF_PRESENT | BCF_FAINTED);
    st.side[0].team[1].type   = TYPE_SIGNAL;
    st.side[0].team[1].hp_max = 40u;
    st.side[0].team[1].hp_cur = 30u;            // 75 % of 40, and 30 raw
    st.side[0].team[2].type   = TYPE_SIGNAL;
    st.side[0].team[2].hp_max = 400u;
    st.side[0].team[2].hp_cur = 80u;            // 20 % of 400, but 80 raw

    CHECK_EQ(battle_ai_switch_score(st, 0u, 1u), 2u * BATTLE_AI_SWITCH_TYPE_WEIGHT + 75u);
    CHECK_EQ(battle_ai_switch_score(st, 0u, 2u), 2u * BATTLE_AI_SWITCH_TYPE_WEIGHT + 20u);

    BattleAi ai;
    battle_ai_init(ai, 0u, 0xA0A0u);
    const BattleAction a = battle_ai_choose(ai, st);
    CHECK_EQ(a.kind, (uint8_t)BACT_SWITCH);
    CHECK_EQ(a.index, 1u);                      // the fuller one, not the fatter one
  }
}

TEST(switching_costs_the_turn_and_the_ai_pays_it_like_anybody_else)
{
  // The AI is an action producer and gets no privileges: a switch it chose
  // resolves through the same engine, and the incoming Bug is the one that
  // takes the hit that round.
  BattleState st;
  switch_fixture(st, 10u);
  const uint16_t in_hp_before = st.side[0].team[1].hp_cur;

  BattleAi ai;
  battle_ai_init(ai, 0u, 0xB0B0u);
  const BattleAction a0 = battle_ai_choose(ai, st);
  CHECK_EQ(a0.kind, (uint8_t)BACT_SWITCH);
  CHECK_EQ(a0.index, 1u);

  BattleAction a1;
  a1.kind = (uint8_t)BACT_ATTACK; a1.index = 0u;
  CHECK_EQ(battle_submit_action(st, 0u, a0), BR_OK);
  CHECK_EQ(battle_submit_action(st, 1u, a1), BR_OK);
  (void)battle_step_round(st, nullptr);

  CHECK_EQ(st.side[0].active, 1u);                        // the switch happened
  CHECK(st.side[0].team[1].hp_cur < in_hp_before);        // and it ate the hit
  CHECK_EQ(st.side[0].team[0].hp_cur, 10u);               // the one that left did not
}

// =============================================================================
//  DETERMINISM, AND THE TWO STREAMS UNDER LOAD
// =============================================================================
TEST(the_same_seeds_reproduce_the_same_battle_byte_for_byte)
{
  BattleState a, b;
  BattleSetup s;
  mk_setup(s, 0xABCDEFu, TEAM_MIXED, 3u, TEAM_CORRUPT, 3u, 14u);
  CHECK_EQ(battle_init(a, s), BR_OK);
  CHECK_EQ(battle_init(b, s), BR_OK);

  BattleAi a0, a1, b0, b1;
  battle_ai_init(a0, 0u, 0x1234u); battle_ai_init(a1, 1u, 0x5678u);
  battle_ai_init(b0, 0u, 0x1234u); battle_ai_init(b1, 1u, 0x5678u);

  AiRunStats sa, sb;
  run_ai_battle(a, a0, a1, nullptr, sa);
  run_ai_battle(b, b0, b1, nullptr, sb);

  CHECK_EQ(memcmp(&a, &b, sizeof a), 0);
  CHECK_EQ(battle_state_hash(a), battle_state_hash(b));
  CHECK_EQ(a0.rng.s, b0.rng.s);
  CHECK_EQ(a1.rng.s, b1.rng.s);
  CHECK_EQ(sa.rounds, sb.rounds);
  CHECK_EQ(sa.ai_draws, sb.ai_draws);
  CHECK(sa.rounds >= 3u);
  CHECK(sa.ai_draws > 0u);        // the tie-break really ran, twice over
}

TEST(the_ai_seed_reaches_the_choice_and_sixteen_consecutive_seeds_do_not_play_one_fight)
{
  // A seed that never changed anything would make every determinism case above
  // pass for the wrong reason, so this asserts that the seed REACHES both the
  // action and the outcome. It is also the case that found a real defect: the
  // seeds below are CONSECUTIVE on purpose - it is what a caller derived from a
  // counter would hand over - and before battle_ai_init() finalised its seed,
  // all sixteen produced ONE action sequence and ONE final state. One xorshift
  // step does not carry a low-bit difference as far as bit 31, and bit 31 is
  // exactly what rng_next_below(r, 2) reads.
  uint32_t act_seen[16], end_seen[16];
  uint32_t distinct_actions = 0u, distinct_ends = 0u;
  uint32_t total_draws = 0u;

  for (uint32_t k = 0; k < 16u; ++k) {
    BattleState st;
    BattleSetup s;
    mk_setup(s, 0x0BADC0DEu, TEAM_SIGNAL, 3u, TEAM_SYSTEM, 3u, 12u);
    CHECK_EQ(battle_init(st, s), BR_OK);

    BattleAi ai0, ai1;
    battle_ai_init(ai0, 0u, 0x100u + k);
    battle_ai_init(ai1, 1u, 0x200u + k);

    // FNV-1a over every action byte both sides produced: this is the ACTION
    // sequence, which is what the AI actually decides. The final state is the
    // stronger claim and is counted beside it.
    uint32_t ah = 2166136261u;
    // BOUNDED, and not because a battle can run forever - the engine's round cap
    // guarantees it cannot. A MUTANT can: an AI whose action the engine refuses
    // leaves both sides unsubmitted, battle_step_round() answers BS_NEED_ACTIONS
    // and the outcome never moves. An unbounded loop here would hang the suite
    // instead of failing it, which is a test that cannot report.
    for (uint16_t r = 0; r < (uint16_t)(BATTLE_MAX_ROUNDS + 4) &&
                         st.outcome == (uint8_t)BO_UNDECIDED; ++r) {
      const uint32_t c0 = ai0.rng.s, c1 = ai1.rng.s;
      const BattleAction a0 = battle_ai_choose(ai0, st);
      const BattleAction a1 = battle_ai_choose(ai1, st);
      if (ai0.rng.s != c0) ++total_draws;
      if (ai1.rng.s != c1) ++total_draws;
      ah = (ah ^ a0.kind) * 16777619u; ah = (ah ^ a0.index) * 16777619u;
      ah = (ah ^ a1.kind) * 16777619u; ah = (ah ^ a1.index) * 16777619u;
      CHECK_EQ(battle_submit_action(st, 0u, a0), BR_OK);
      CHECK_EQ(battle_submit_action(st, 1u, a1), BR_OK);
      if (battle_step_round(st, nullptr) == BS_BATTLE_OVER) break;
    }
    act_seen[k] = ah;
    end_seen[k] = battle_state_hash(st);

    bool novel_a = true, novel_e = true;
    for (uint32_t j = 0; j < k; ++j) {
      if (act_seen[j] == act_seen[k]) novel_a = false;
      if (end_seen[j] == end_seen[k]) novel_e = false;
    }
    if (novel_a) ++distinct_actions;
    if (novel_e) ++distinct_ends;
  }

  CHECK(total_draws > 0u);        // there were ties for the seed to break
  CHECK(distinct_actions >= 2u);  // ...and it broke at least two of them differently
  CHECK(distinct_ends >= 2u);     // ...and that reached the outcome, not only the log
}

TEST(an_ai_driven_battle_replays_exactly_through_an_engine_that_never_runs_the_ai)
{
  // THE MEASUREMENT BEHIND THE TWO-STREAM ARGUMENT. battle_replay() re-runs the
  // recorded ACTIONS and never constructs a BattleAi. If one AI draw had come
  // out of BattleState.rng, the replayed cursor would sit a step behind the
  // recorded one, and hashing the whole state - rng.s included - would catch it
  // on the round it happened.
  static BattleEvent buf[8192];
  BattleLog log;
  battle_log_init(log, buf, (uint16_t)(sizeof buf / sizeof buf[0]));

  BattleSetup s;
  mk_setup(s, 0x5EED5EEDu, TEAM_MIXED, 3u, TEAM_SYSTEM, 3u, 16u);
  BattleState st;
  CHECK_EQ(battle_init(st, s), BR_OK);

  BattleAi ai0, ai1;
  battle_ai_init(ai0, 0u, 0xA1A1u);
  battle_ai_init(ai1, 1u, 0xB2B2u);
  const uint32_t c0 = ai0.rng.s, c1 = ai1.rng.s;

  AiRunStats out;
  run_ai_battle(st, ai0, ai1, &log, out);

  CHECK_EQ(log.dropped, 0u);
  CHECK(out.rounds >= 3u);
  // THE CONTROL: the AI really used its stream during this battle, so the
  // replay below is not agreeing because nothing was ever drawn.
  CHECK(out.ai_draws > 0u);
  CHECK(ai0.rng.s != c0 || ai1.rng.s != c1);

  BattleState replayed;
  BattleReplayReport rep;
  CHECK_EQ(battle_replay(s, log, replayed, nullptr, rep), BR_OK);
  CHECK_EQ(rep.first_bad_round, 0u);
  CHECK_EQ(rep.rounds, out.rounds);
  CHECK_EQ(rep.outcome, st.outcome);
  CHECK_EQ(rep.final_hash, battle_state_hash(st));
  CHECK_EQ(memcmp(&replayed, &st, sizeof st), 0);
}

TEST(an_ai_chosen_action_replayed_by_hand_uses_exactly_the_same_battle_draws)
{
  // The sharper half of the same claim, without the log: record what the AI
  // chose, then run the SAME battle again feeding those bytes in by hand and
  // constructing no AI at all. Identical states after every single round means
  // the AI's draws were never in the battle's budget.
  BattleSetup s;
  mk_setup(s, 0x424242u, TEAM_CORRUPT, 3u, TEAM_SIGNAL, 3u, 11u);

  BattleAction rec[2][BATTLE_MAX_ROUNDS + 4];
  uint32_t     cursor_after[BATTLE_MAX_ROUNDS + 4];   // st.rng.s at each round's end
  uint32_t     hash_after[BATTLE_MAX_ROUNDS + 4];
  uint16_t n = 0u;

  BattleState st;
  CHECK_EQ(battle_init(st, s), BR_OK);
  const uint32_t cursor_at_init = st.rng.s;
  BattleAi ai0, ai1;
  battle_ai_init(ai0, 0u, 0xF00Du);
  battle_ai_init(ai1, 1u, 0x0BADu);
  uint16_t ai_draws = 0u;
  while (st.outcome == (uint8_t)BO_UNDECIDED && n < (uint16_t)(BATTLE_MAX_ROUNDS + 4)) {
    const uint32_t c0 = ai0.rng.s, c1 = ai1.rng.s;
    rec[0][n] = battle_ai_choose(ai0, st);
    rec[1][n] = battle_ai_choose(ai1, st);
    if (ai0.rng.s != c0) ++ai_draws;
    if (ai1.rng.s != c1) ++ai_draws;
    // Choosing is free: the battle cursor has not moved since the last round
    // ended, however many times the two AIs drew.
    CHECK_EQ(st.rng.s, (n == 0u) ? cursor_at_init : cursor_after[n - 1u]);

    CHECK_EQ(battle_submit_action(st, 0u, rec[0][n]), BR_OK);
    CHECK_EQ(battle_submit_action(st, 1u, rec[1][n]), BR_OK);
    const BattleStepResult sr = battle_step_round(st, nullptr);
    cursor_after[n] = st.rng.s;
    hash_after[n]   = battle_state_hash(st);
    ++n;
    if (sr == BS_BATTLE_OVER) break;
  }
  CHECK(n >= 3u);
  CHECK(ai_draws > 0u);                     // the control: the AI did use its stream

  BattleState hand;
  CHECK_EQ(battle_init(hand, s), BR_OK);
  for (uint16_t i = 0; i < n; ++i) {
    CHECK_EQ(battle_submit_action(hand, 0u, rec[0][i]), BR_OK);
    CHECK_EQ(battle_submit_action(hand, 1u, rec[1][i]), BR_OK);
    (void)battle_step_round(hand, nullptr);
    // ROUND BY ROUND, not only at the end: a battle draw the AI had stolen shows
    // up on the round it was stolen rather than three rounds later, and the
    // cursor moves before any visible number does.
    CHECK_EQ(hand.rng.s, cursor_after[i]);
    CHECK_EQ(battle_state_hash(hand), hash_after[i]);
  }
  CHECK_EQ(memcmp(&hand, &st, sizeof st), 0);
  CHECK_EQ(battle_state_hash(hand), battle_state_hash(st));
}
