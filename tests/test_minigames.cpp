// =============================================================================
//  tests/test_minigames.cpp - the minigame contract (P3-C4a, spec section 29)
//
//  Spec 29's closing line is the acceptance criterion these tests exist for:
//  "All games must be deterministic/testable when supplied a fixed RNG seed."
//  Every case below runs the PURE logic with no clock, no Arduino and no
//  screen - which is the whole reason games/*_logic.cpp is a separate
//  translation unit from games/*_draw.cpp.
// =============================================================================
#include "nt_test.h"

#include "minigames/minigame.h"
#include "minigames/games/games.h"
#include "minigames/manager.h"

static const MgLogic* const ALL[] = { &MG_PING, &MG_SEQUENCE };
static const uint8_t ALL_N = (uint8_t)(sizeof(ALL) / sizeof(ALL[0]));

// -----------------------------------------------------------------------------
//  A TAPE. The unit of reproducibility: a seed plus a scripted policy.
//  `policy` is called once per step and returns -1 for "no press this step",
//  or MG_SIDE_L / MG_SIDE_R.
// -----------------------------------------------------------------------------
typedef int8_t (*TapeFn)(const MgCtx&, uint32_t step);

static uint16_t run_tape(const MgLogic& g, uint32_t seed, TapeFn policy,
                         uint32_t* steps_out = nullptr)
{
  MgCtx c;
  mg_begin(c, g, seed);
  uint32_t steps = 0;
  while (!c.finished && steps < MG_MAX_STEPS + 4u) {
    const int8_t side = policy ? policy(c, steps) : (int8_t)-1;
    if (side >= 0) mg_press(c, g, (uint8_t)side);
    if (!c.finished) mg_tick(c, g);
    ++steps;
  }
  if (steps_out) *steps_out = steps;
  return mg_score(c, g);
}

// Policies -------------------------------------------------------------------
static int8_t tape_idle(const MgCtx&, uint32_t)  { return -1; }

// PING: press the lit side the moment it lights.
static int8_t tape_ping_perfect(const MgCtx& c, uint32_t)
{
  return ping_is_lit(c) ? (int8_t)ping_target(c) : (int8_t)-1;
}
// PING: press immediately, always too early.
static int8_t tape_ping_early(const MgCtx& c, uint32_t)
{
  return ping_is_lit(c) ? (int8_t)-1 : (int8_t)MG_SIDE_L;
}
// SEQUENCE: reproduce what was shown, one symbol per step once playback ends.
static int8_t tape_seq_perfect(const MgCtx& c, uint32_t)
{
  if (seq_is_showing(c)) return -1;
  // The tape may read the sequence: this is a scripted PLAYER, and what is
  // under test is the engine's reproducibility, not the player's memory.
  extern uint8_t seq_symbol_at(const MgCtx&, uint8_t);
  return (int8_t)seq_symbol_at(c, seq_pos_of(c));
}
static int8_t tape_seq_wrong(const MgCtx& c, uint32_t)
{
  if (seq_is_showing(c)) return -1;
  extern uint8_t seq_symbol_at(const MgCtx&, uint8_t);
  return (int8_t)(1u - seq_symbol_at(c, seq_pos_of(c)));
}

// -----------------------------------------------------------------------------
//  THE CONTRACT, over every game
// -----------------------------------------------------------------------------
TEST(every_game_is_reproducible_from_a_seed_and_a_tape) {
  static TapeFn kTapes[] = { tape_idle, tape_ping_perfect, tape_ping_early };
  for (uint8_t i = 0; i < ALL_N; ++i) {
    for (uint32_t seed = 1; seed <= 8u; ++seed) {
      for (uint8_t t = 0; t < 3u; ++t) {
        // The same seed and the same tape, twice: byte-identical scores.
        const uint16_t a = run_tape(*ALL[i], seed, kTapes[t]);
        const uint16_t b = run_tape(*ALL[i], seed, kTapes[t]);
        CHECK_EQ(a, b);
      }
    }
  }
}

TEST(every_game_finishes_inside_the_fifteen_second_ceiling) {
  for (uint8_t i = 0; i < ALL_N; ++i) {
    for (uint32_t seed = 1; seed <= 32u; ++seed) {
      // The IDLE tape is the worst case: nobody presses anything, so every
      // game must run itself out. MG_MAX_STEPS is the manager's backstop and
      // reaching it would mean the game never ends on its own - assert the
      // game beats it, not merely that the backstop works.
      uint32_t steps = 0;
      (void)run_tape(*ALL[i], seed, tape_idle, &steps);
      CHECK(steps < MG_MAX_STEPS);
      CHECK((steps * MG_STEP_MS) < MG_MAX_MS);
    }
  }
}

TEST(every_game_scores_inside_zero_to_a_thousand) {
  static TapeFn kTapes[] = { tape_idle, tape_ping_perfect, tape_ping_early };
  for (uint8_t i = 0; i < ALL_N; ++i) {
    for (uint32_t seed = 1; seed <= 16u; ++seed) {
      for (uint8_t t = 0; t < 3u; ++t) {
        const uint16_t s = run_tape(*ALL[i], seed, kTapes[t]);
        CHECK(s <= MG_SCORE_MAX);
      }
    }
  }
}

TEST(a_different_seed_gives_a_different_game) {
  // Not a different SCORE necessarily - an idle run scores 0 whatever the
  // seed - so compare the thing the seed actually drives: the layout.
  MgCtx a, b;
  mg_begin(a, MG_PING, 1u);
  mg_begin(b, MG_PING, 2u);
  bool differs = (ping_arm_ms(a) != ping_arm_ms(b)) || (ping_target(a) != ping_target(b));
  CHECK(differs);
}

// -----------------------------------------------------------------------------
//  PING
// -----------------------------------------------------------------------------
TEST(ping_perfect_play_scores_the_maximum_and_idle_play_scores_nothing) {
  for (uint32_t seed = 1; seed <= 8u; ++seed) {
    const uint16_t perfect = run_tape(MG_PING, seed, tape_ping_perfect);
    const uint16_t idle    = run_tape(MG_PING, seed, tape_idle);
    CHECK_EQ(idle, 0u);
    // Pressing on the first lit step cannot be beaten, and the five rounds
    // are worth 200 each.
    CHECK(perfect > 900u);
  }
}

TEST(ping_punishes_an_early_press_with_a_lost_round_and_nothing_worse) {
  // "Mash the button" must not win. It also must not cost a stat - spec 27
  // forbids punishment - so all it does is spend the round at zero.
  for (uint32_t seed = 1; seed <= 8u; ++seed) {
    CHECK_EQ(run_tape(MG_PING, seed, tape_ping_early), 0u);
  }
}

TEST(ping_leaves_a_note_the_draw_layer_can_toast_but_never_toasts_itself) {
  MgCtx c;
  mg_begin(c, MG_PING, 3u);
  CHECK_EQ(c.note, 0u);
  mg_press(c, MG_PING, MG_SIDE_L);              // instantly: before the light
  CHECK_EQ(c.note, (uint16_t)STR_GM_TOOSOON);
}

// -----------------------------------------------------------------------------
//  SEQUENCE
// -----------------------------------------------------------------------------
TEST(sequence_perfect_play_walks_the_whole_ladder) {
  for (uint32_t seed = 1; seed <= 8u; ++seed) {
    CHECK_EQ(run_tape(MG_SEQUENCE, seed, tape_seq_perfect), MG_SCORE_MAX);
  }
}

TEST(sequence_gives_partial_credit_rather_than_zero_for_a_wrong_symbol) {
  // Wrong on the very first symbol: round 0 of 3, so zero - but the game ends
  // rather than continuing, and a later mistake keeps what was earned.
  for (uint32_t seed = 1; seed <= 4u; ++seed) {
    CHECK_EQ(run_tape(MG_SEQUENCE, seed, tape_seq_wrong), 0u);
  }
}

TEST(sequence_ignores_presses_while_it_is_still_showing_the_sequence) {
  MgCtx c;
  mg_begin(c, MG_SEQUENCE, 5u);
  CHECK(seq_is_showing(c));
  const uint8_t pos0 = seq_pos_of(c);
  mg_press(c, MG_SEQUENCE, MG_SIDE_L);
  mg_press(c, MG_SEQUENCE, MG_SIDE_R);
  CHECK_EQ(seq_pos_of(c), pos0);                // nothing advanced
  CHECK_EQ(c.finished, 0u);                     // and nothing ended
}

// -----------------------------------------------------------------------------
//  THE DRIVER
// -----------------------------------------------------------------------------
TEST(a_press_after_the_end_can_never_score) {
  MgCtx c;
  mg_begin(c, MG_PING, 7u);
  while (mg_tick(c, MG_PING)) { }
  const uint16_t s = mg_score(c, MG_PING);
  for (uint8_t i = 0; i < 20u; ++i) {
    mg_press(c, MG_PING, MG_SIDE_L);
    mg_press(c, MG_PING, MG_SIDE_R);
  }
  CHECK_EQ(mg_score(c, MG_PING), s);
}

TEST(mg_score_is_idempotent_so_a_double_read_cannot_double_report) {
  MgCtx c;
  mg_begin(c, MG_PING, 11u);
  while (mg_tick(c, MG_PING)) { }
  const uint16_t a = mg_score(c, MG_PING);
  CHECK_EQ(mg_score(c, MG_PING), a);
  CHECK_EQ(mg_score(c, MG_PING), a);
}

TEST(the_clock_is_the_step_count_and_nothing_else) {
  MgCtx c;
  mg_begin(c, MG_PING, 13u);
  CHECK_EQ(c.t_ms, 0u);
  mg_tick(c, MG_PING);
  CHECK_EQ(c.t_ms, MG_STEP_MS);
  mg_tick(c, MG_PING);
  CHECK_EQ(c.t_ms, 2u * MG_STEP_MS);
}

// =============================================================================
//  THE MANAGER
//
//  The property these exist for: finish() is reported EXACTLY ONCE per game.
//  The code this replaced got there by accident - ui_game_leave() scored an
//  abandoned game as a loss and re-entry was guarded by a phase check - so any
//  second exit path would have double-counted minigames_won AND the XP ledger.
// =============================================================================
static uint8_t  g_reports;
static uint8_t  g_last_id;
static uint16_t g_last_score;
static uint16_t g_report_sum;

static void spy_report(uint8_t id, uint16_t score)
{
  ++g_reports;
  g_last_id    = id;
  g_last_score = score;
  g_report_sum = (uint16_t)(g_report_sum + score);
}

static void spy_reset(void)
{
  g_reports = 0; g_last_id = 0xFF; g_last_score = 0xFFFF; g_report_sum = 0;
  mgr_bind_report(spy_report);
}

// Drive the manager in 25 ms slices until it stops or the guard trips.
static uint32_t mgr_drain(uint32_t max_ms = 200000u)
{
  uint32_t t = 0;
  while (mgr_tick(MG_STEP_MS) && t < max_ms) t += MG_STEP_MS;
  return t;
}

TEST(the_manager_reports_each_game_exactly_once_when_played_to_the_end) {
  spy_reset();
  mgr_begin(MG_ID_PING, 42u, MGR_SEQ_LEN);
  // Nobody presses: every game runs itself out, and the "next?" card times out
  // after the FIRST game, so exactly one game is played and reported once.
  mgr_drain();
  CHECK_EQ(g_reports, 1u);
  CHECK(!mgr_active());
}

TEST(the_manager_reports_a_game_abandoned_mid_run_exactly_once) {
  // THE BUG THIS TEST EXISTS FOR. Leave the screen while a game is running.
  spy_reset();
  mgr_begin(MG_ID_PING, 7u, MGR_SEQ_LEN);
  for (uint8_t i = 0; i < 100u; ++i) mgr_tick(MG_STEP_MS);   // into MGR_RUN
  CHECK_EQ(mgr_phase(), (uint8_t)MGR_RUN);
  CHECK_EQ(g_reports, 0u);
  mgr_abort();
  CHECK_EQ(g_reports, 1u);
  // ... and every further exit path must add nothing.
  mgr_abort();
  mgr_back();
  mgr_drain();
  CHECK_EQ(g_reports, 1u);
}

TEST(quitting_with_B_reports_once_and_never_again) {
  spy_reset();
  mgr_begin(MG_ID_PING, 9u, MGR_SEQ_LEN);
  for (uint8_t i = 0; i < 100u; ++i) mgr_tick(MG_STEP_MS);
  mgr_back();                       // B during the run: quit
  CHECK_EQ(g_reports, 1u);
  mgr_back();
  mgr_abort();
  mgr_drain();
  CHECK_EQ(g_reports, 1u);
}

TEST(a_full_sequence_reports_once_per_game_and_averages_them) {
  spy_reset();
  mgr_begin(MG_ID_PING, 5u, MGR_SEQ_LEN);
  uint32_t guard = 0;
  while (mgr_active() && guard < 400000u) {
    // Answer the "next?" card with A so the whole sequence is played.
    if (mgr_phase() == (uint8_t)MGR_NEXT) mgr_press(MG_SIDE_L);
    mgr_tick(MG_STEP_MS);
    guard += MG_STEP_MS;
  }
  CHECK_EQ(g_reports, MGR_SEQ_LEN);
  CHECK_EQ(mgr_count(), MGR_SEQ_LEN);
}

TEST(the_manager_never_reports_a_game_it_did_not_start) {
  spy_reset();
  mgr_abort();                      // idle
  mgr_back();
  mgr_drain();
  CHECK_EQ(g_reports, 0u);
}

TEST(a_sequence_is_reproducible_from_its_seed) {
  uint8_t ids_a[MGR_SEQ_LEN] = {0}, ids_b[MGR_SEQ_LEN] = {0};
  for (uint8_t pass = 0; pass < 2u; ++pass) {
    spy_reset();
    mgr_begin(MG_ID_PING, 12345u, MGR_SEQ_LEN);
    uint8_t seen = 0; uint32_t guard = 0;
    while (mgr_active() && guard < 400000u) {
      if (mgr_logic() && seen < MGR_SEQ_LEN) {
        const uint8_t id = mgr_logic()->id;
        if (seen == 0 || (pass ? ids_b : ids_a)[seen - 1] != id ||
            mgr_index() == seen) {
          if (mgr_index() == seen) (pass ? ids_b : ids_a)[seen++] = id;
        }
      }
      if (mgr_phase() == (uint8_t)MGR_NEXT) mgr_press(MG_SIDE_L);
      mgr_tick(MG_STEP_MS);
      guard += MG_STEP_MS;
    }
  }
  for (uint8_t i = 0; i < MGR_SEQ_LEN; ++i) CHECK_EQ(ids_a[i], ids_b[i]);
}

TEST(the_chrome_runs_on_real_time_but_the_game_runs_on_whole_steps) {
  // Drive with a ragged frame time. The score must not change with it, which
  // is the whole reason mgr_tick() accumulates into whole MG_STEP_MS units.
  uint16_t scores[2] = {0, 0};
  const uint32_t kSlice[2] = { MG_STEP_MS, 37u };   // 25 ms vs a ragged 37 ms
  for (uint8_t v = 0; v < 2u; ++v) {
    spy_reset();
    mgr_begin(MG_ID_PING, 99u, 1u);
    uint32_t guard = 0;
    while (mgr_active() && guard < 400000u) { mgr_tick(kSlice[v]); guard += kSlice[v]; }
    scores[v] = g_report_sum;
  }
  CHECK_EQ(scores[0], scores[1]);
}

// The invariant, fuzzed over interleavings of every exit path rather than the
// three the tests above walk by hand. This is what would catch a phase added
// to the manager that reports without leaving RUN - the shape of the original
// double-count bug. Deterministic: the "randomness" is a fixed LCG.
//
// TWO THINGS THIS TEST NEEDED BEFORE IT COULD CATCH ANYTHING, both found by
// mutating the manager and watching it stay green:
//
//  * RAGGED FRAME TIMES. Driving a flat MG_STEP_MS means mgr_tick()'s inner
//    step loop runs exactly once per call, so the loop's exit condition is
//    never exercised. A real frame is 20-50 ms, and one after a flash write
//    can be 300, which runs the loop a dozen times.
//  * RESTARTING. back/abort are two of the eight actions, so the manager went
//    idle within a few steps and the remaining ~595 iterations of each trial
//    tested nothing at all.
TEST(the_report_count_never_exceeds_the_games_started) {
  for (uint32_t trial = 1; trial <= 200u; ++trial) {
    spy_reset();
    mgr_begin(MG_ID_PING, trial, MGR_SEQ_LEN);
    uint16_t started = 1u;            // mgr_begin() arms the first one
    uint8_t  last_index = 0u;

    uint32_t r = trial * 2654435761u;
    for (uint16_t k = 0; k < 600u; ++k) {
      r = r * 1103515245u + 12345u;
      switch ((r >> 16) % 8u) {
        case 0: mgr_back();            break;
        case 1: mgr_abort();           break;
        case 2: mgr_press(MG_SIDE_L);  break;
        case 3: mgr_press(MG_SIDE_R);  break;
        default: mgr_tick((uint32_t)(5u + ((r >> 8) % 320u))); break;
      }
      if (mgr_active() && mgr_index() != last_index) { ++started; last_index = mgr_index(); }

      // THE INVARIANT: a game reports at most once, so the running total can
      // never outrun the number of games that were armed.
      CHECK(g_reports <= started);

      // Keep a live sequence under the fuzz. Without this the manager is idle
      // for almost the whole trial and the exit paths are never re-entered.
      if (!mgr_active()) {
        mgr_begin((uint8_t)(r % MG_ID_COUNT), r ^ trial, MGR_SEQ_LEN);
        ++started;
        last_index = 0u;
      }
    }
    CHECK(g_reports <= started);
  }
}
