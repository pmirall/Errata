// =============================================================================
//  ERRATA host tests - test_activity.cpp
//  THE DAILY ACTIVITY SCORE (spec sections 25 and 57, plan P6-C2).
//
//  game/activity.cpp is pure and takes its clock as a struct, so every case
//  below drives a wall clock, a calibration state and a persisted CooldownTable
//  directly - no device, no gametime, no save manager. The link line is
//  activity.o AND NOTHING ELSE; if it ever needs a second object, something
//  has leaked into the pure half.
//
//  FOUR OF THESE CASES ARE CHEAT CASES AND THEY ARE THE POINT OF THE FILE:
//  an uncalibrated clock, a clock rolled backwards, a device power-cycled
//  repeatedly, and one access point scanned thirty times. Each was written by
//  BREAKING the guard it covers and watching a named case fail - the mutations
//  are recorded above each one, because a cheat test that has never been seen
//  to fail is a comment.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "game/activity.h"
#include "persistence/save_schema.h"

// A calibrated wall clock, well above NT_EPOCH_SANE_MIN. 2026-01-01T00:00:00Z
// is a day boundary, which makes "plus an hour" and "plus a day" readable.
static const uint32_t EP0 = 1767225600u;   // day index 20454
static_assert(EP0 % ACT_DAY_S == 0u, "EP0 is meant to sit on a day boundary");

static ActClock clk(uint32_t epoch, uint8_t cal = (uint8_t)CAL_USER)
{
  ActClock c;
  c.now_epoch = epoch;
  c.cal       = cal;
  return c;
}

// A fresh device: an empty persisted table and an empty per-boot half.
static void fresh(CooldownTable& t)
{
  memset(&t, 0, sizeof t);
  act_begin();
}

// A POWER CYCLE, modelled exactly: the persisted table survives byte for byte
// (it comes off flash), the per-boot half is gone (it was .bss).
static void power_cycle(void) { act_begin(); }

// =============================================================================
//  1. THE FORMULA, TERM BY TERM
// =============================================================================
TEST(each_term_pays_its_own_multiplier) {
  CooldownTable t;

  fresh(t);
  CHECK_EQ(act_note_carried(t, 60u, clk(EP0)), (uint16_t)ACT_PTS_CARRY);
  CHECK_EQ(act_score_today(t, clk(EP0)), (uint16_t)ACT_PTS_CARRY);

  fresh(t);
  CHECK_EQ(act_note_interaction(t, clk(EP0)), (uint16_t)ACT_PTS_INTERACT);
  CHECK_EQ(act_score_today(t, clk(EP0)), (uint16_t)ACT_PTS_INTERACT);

  fresh(t);
  const uint32_t one = 0xA1B2C3D4u;
  CHECK_EQ(act_note_networks(t, &one, 1u, clk(EP0)), (uint16_t)ACT_PTS_NET);
  CHECK_EQ(act_score_today(t, clk(EP0)), (uint16_t)ACT_PTS_NET);

  fresh(t);
  CHECK_EQ(act_note_peer(t, 0x5EEDu, clk(EP0)), (uint16_t)ACT_PTS_PEER);
  CHECK_EQ(act_score_today(t, clk(EP0)), (uint16_t)ACT_PTS_PEER);
}

TEST(whole_minutes_are_credited_and_the_remainder_is_kept) {
  CooldownTable t;
  fresh(t);
  // Thirty steps of two seconds is one minute, and the answer must be the same
  // as one step of sixty: the integrator drops nothing and invents nothing.
  for (int i = 0; i < 29; ++i) CHECK_EQ(act_note_carried(t, 2u, clk(EP0)), 0u);
  CHECK_EQ(act_note_carried(t, 2u, clk(EP0)), (uint16_t)ACT_PTS_CARRY);
  CHECK_EQ(act_carry_min(), 1u);

  CooldownTable u;
  fresh(u);
  CHECK_EQ(act_note_carried(u, 60u, clk(EP0)), (uint16_t)ACT_PTS_CARRY);
  CHECK_EQ(u.act_score, t.act_score);
}

TEST(a_full_honest_day_is_exactly_the_declared_maximum) {
  CooldownTable t;
  fresh(t);
  // Four hours of carrying, twenty care actions, ten networks, four peers -
  // each term driven past its own cap so the caps, not the inputs, decide.
  for (uint32_t s = 0; s < 6u * 3600u; s += 60u) (void)act_note_carried(t, 60u, clk(EP0));
  for (int i = 0; i < 40; ++i) (void)act_note_interaction(t, clk(EP0));
  for (uint32_t k = 1; k <= 40u; ++k) {
    const uint32_t h = k * 2654435761u;
    (void)act_note_networks(t, &h, 1u, clk(EP0));
  }
  for (uint32_t k = 1; k <= 20u; ++k) (void)act_note_peer(t, k * 40503u, clk(EP0));

  CHECK_EQ(act_carry_min(),    (uint8_t)ACT_CAP_CARRY_MIN);
  CHECK_EQ(act_interactions(), (uint8_t)ACT_CAP_INTERACT);
  CHECK_EQ(act_nets_seen(),    (uint8_t)ACT_CAP_NETS);
  CHECK_EQ(act_peers_seen(),   (uint8_t)ACT_CAP_PEERS);
  CHECK_EQ(act_score_today(t, clk(EP0)), (uint16_t)ACT_SCORE_MAX);
}

// =============================================================================
//  2. THE CAPS. Each term is capped ON ITS OWN, which is what "each capped"
//     means and what a single total cap would NOT give: one term running away
//     must not be able to consume another term's room.
// =============================================================================
TEST(no_single_term_can_exceed_its_own_cap_however_hard_it_is_driven) {
  CooldownTable t;

  fresh(t);
  for (uint32_t s = 0; s < 24u * 3600u; s += 60u) (void)act_note_carried(t, 60u, clk(EP0));
  CHECK_EQ(act_carry_min(), (uint8_t)ACT_CAP_CARRY_MIN);
  CHECK_EQ(act_score_today(t, clk(EP0)),
           (uint16_t)(ACT_CAP_CARRY_MIN * ACT_PTS_CARRY));

  fresh(t);
  for (int i = 0; i < 500; ++i) (void)act_note_interaction(t, clk(EP0));
  CHECK_EQ(act_interactions(), (uint8_t)ACT_CAP_INTERACT);
  CHECK_EQ(act_score_today(t, clk(EP0)),
           (uint16_t)(ACT_CAP_INTERACT * ACT_PTS_INTERACT));

  fresh(t);
  for (uint32_t k = 1; k <= 500u; ++k) {
    const uint32_t h = k * 2654435761u;
    (void)act_note_networks(t, &h, 1u, clk(EP0));
  }
  CHECK_EQ(act_nets_seen(), (uint8_t)ACT_CAP_NETS);
  CHECK_EQ(act_score_today(t, clk(EP0)), (uint16_t)(ACT_CAP_NETS * ACT_PTS_NET));

  fresh(t);
  for (uint32_t k = 1; k <= 500u; ++k) (void)act_note_peer(t, k * 40503u, clk(EP0));
  CHECK_EQ(act_peers_seen(), (uint8_t)ACT_CAP_PEERS);
  CHECK_EQ(act_score_today(t, clk(EP0)), (uint16_t)(ACT_CAP_PEERS * ACT_PTS_PEER));
}

TEST(the_days_total_is_clamped_and_never_wraps) {
  CooldownTable t;
  fresh(t);
  // Start the day one point below the ceiling and then drive every term.
  t.act_day   = act_day_index(EP0);
  t.act_score = (uint16_t)(ACT_SCORE_MAX - 1u);
  (void)act_note_peer(t, 7u, clk(EP0));                 // worth 15, room for 1
  CHECK_EQ(t.act_score, (uint16_t)ACT_SCORE_MAX);
  for (int i = 0; i < 50; ++i) (void)act_note_interaction(t, clk(EP0));
  for (uint32_t s = 0; s < 3600u; s += 60u) (void)act_note_carried(t, 60u, clk(EP0));
  CHECK_EQ(t.act_score, (uint16_t)ACT_SCORE_MAX);
}

// =============================================================================
//  3. DAY ROLLOVER
// =============================================================================
TEST(the_day_index_is_the_wall_clock_divided_by_one_day_and_nothing_else) {
  CHECK_EQ(act_day_index(0u), 0u);
  CHECK_EQ(act_day_index((uint32_t)ACT_DAY_S - 1u), 0u);
  CHECK_EQ(act_day_index((uint32_t)ACT_DAY_S), 1u);
  CHECK_EQ(act_day_index(EP0), 20454u);
  CHECK_EQ(act_day_index(EP0 + (uint32_t)ACT_DAY_S - 1u), 20454u);
  CHECK_EQ(act_day_index(EP0 + (uint32_t)ACT_DAY_S), 20455u);
  // The widest epoch a u32 can hold still fits the u16 field, which is the
  // static_assert in activity.cpp restated where a reader will look for it.
  CHECK_EQ(act_day_index(0xFFFFFFFFu), 49710u);
}

TEST(a_new_day_resets_the_score_and_every_counter) {
  CooldownTable t;
  fresh(t);
  for (int i = 0; i < 10; ++i) (void)act_note_interaction(t, clk(EP0));
  const uint32_t h = 0xDEADBEEFu;
  (void)act_note_networks(t, &h, 1u, clk(EP0));
  CHECK(act_score_today(t, clk(EP0)) > 0u);
  CHECK_EQ(act_interactions(), 10u);

  const uint32_t tomorrow = EP0 + (uint32_t)ACT_DAY_S;
  CHECK_EQ(act_score_today(t, clk(tomorrow)), 0u);   // before anything is noted
  (void)act_note_interaction(t, clk(tomorrow));
  CHECK_EQ(t.act_day, act_day_index(tomorrow));
  CHECK_EQ(act_interactions(), 1u);
  CHECK_EQ(act_nets_seen(), 0u);
  CHECK_EQ(act_carry_min(), 0u);
  CHECK_EQ(act_score_today(t, clk(tomorrow)), (uint16_t)ACT_PTS_INTERACT);
  // AND THE SET ROLLED WITH IT: yesterday's network is new again today, which
  // is the whole reason the set is per-DAY and not per-boot.
  CHECK_EQ(act_note_networks(t, &h, 1u, clk(tomorrow)), (uint16_t)ACT_PTS_NET);
}

TEST(a_full_day_stays_full_until_the_clock_actually_rolls) {
  CooldownTable t;
  fresh(t);
  for (uint32_t s = 0; s < 24u * 3600u; s += 60u) (void)act_note_carried(t, 60u, clk(EP0));
  const uint16_t capped = act_score_today(t, clk(EP0));
  // 23 h 59 m later it is still the same day and still the same score.
  const uint32_t late = EP0 + (uint32_t)ACT_DAY_S - 60u;
  (void)act_note_carried(t, 3600u, clk(late));
  CHECK_EQ(act_score_today(t, clk(late)), capped);
  CHECK_EQ(t.act_day, act_day_index(EP0));
}

// =============================================================================
//  4. REWARD MONOTONICITY
// =============================================================================
TEST(the_rare_bonus_rises_with_the_score_and_lands_exactly_on_its_two_ends) {
  CHECK_EQ(act_rare_bonus_pm(0u), 0u);
  CHECK_EQ(act_rare_bonus_pm((uint16_t)ACT_SCORE_MAX), (uint16_t)ENC_RARE_BONUS_MAX_PM);
  uint16_t prev = 0u;
  int rises = 0;
  for (uint16_t s = 0; s <= (uint16_t)ACT_SCORE_MAX; ++s) {
    const uint16_t pm = act_rare_bonus_pm(s);
    CHECK(pm >= prev);                                  // never falls
    CHECK(pm <= (uint16_t)ENC_RARE_BONUS_MAX_PM);       // never exceeds the clamp
    if (pm > prev) rises++;
    prev = pm;
  }
  CHECK(rises > 0);
  // Above the maximum it saturates rather than wrapping - a caller that read a
  // corrupted blob must not be handed a 60,000 permille bonus.
  CHECK_EQ(act_rare_bonus_pm(0xFFFFu), (uint16_t)ENC_RARE_BONUS_MAX_PM);
}

TEST(xp_and_happiness_rise_with_the_score_and_a_full_day_pays_the_declared_total) {
  CooldownTable t;
  fresh(t);
  uint32_t xp = 0, happy = 0;
  uint16_t last_score = 0;
  for (uint32_t s = 0; s < 24u * 3600u; s += 60u) {
    (void)act_note_carried(t, 60u, clk(EP0));
    for (int i = 0; i < 2; ++i) (void)act_note_interaction(t, clk(EP0));
    const uint32_t h = (s / 60u + 1u) * 2654435761u;
    (void)act_note_networks(t, &h, 1u, clk(EP0));
    (void)act_note_peer(t, s / 60u + 1u, clk(EP0));
    const ActGain g = act_take_gain();
    // MONOTONE: the score never falls, so neither reward is ever negative and
    // a step that added no score pays nothing.
    CHECK(t.act_score >= last_score);
    if (t.act_score == last_score) { CHECK_EQ(g.xp, 0u); CHECK_EQ(g.happy_milli, 0u); }
    last_score = t.act_score;
    xp    += g.xp;
    happy += g.happy_milli;
  }
  CHECK_EQ(t.act_score, (uint16_t)ACT_SCORE_MAX);
  CHECK_EQ(xp,    (uint32_t)(ACT_SCORE_MAX / ACT_XP_STEP_POINTS));
  CHECK_EQ(happy, (uint32_t)(ACT_SCORE_MAX / ACT_HAPPY_STEP_POINTS) * ACT_HAPPY_STEP_MILLI);
  // The full day is worth 44 XP against XP_CAP_CARRY's 48, which is the "close
  // enough to bind, not so tight it is thrown away" claim in activity.h stated
  // as arithmetic rather than as a comment.
  CHECK(xp <= (uint32_t)XP_CAP_CARRY);
  CHECK(xp * 10u >= (uint32_t)XP_CAP_CARRY * 8u);
}

// =============================================================================
//  5. THE CHEAT CASES
// =============================================================================

// MUTATION RUN, and the guard is clock_ok()'s CAL_UNSET arm in activity.cpp.
// Dropping `c.cal == (uint8_t)CAL_UNSET ||` from it makes THIS case fail, and
// this is the run, not a prediction of one:
//   test_activity.cpp:270: FAIL CHECK_EQ(act_note_carried(t, 3600u, bad), 0u): 60 != 0
//   test_activity.cpp:271: FAIL CHECK_EQ(act_note_interaction(t, bad), 0u): 2 != 0
//   test_activity.cpp:272: FAIL CHECK_EQ(act_note_networks(t, &h, 1u, bad), 0u): 10 != 0
//   test_activity.cpp:273: FAIL CHECK_EQ(act_note_peer(t, ...), 0u): 15 != 0
//   FAIL an_uncalibrated_clock_scores_absolutely_nothing
TEST(an_uncalibrated_clock_scores_absolutely_nothing) {
  CooldownTable t;
  fresh(t);
  const ActClock bad = clk(EP0, (uint8_t)CAL_UNSET);
  const uint32_t h = 0x1234u;

  for (int i = 0; i < 100; ++i) {
    CHECK_EQ(act_note_carried(t, 3600u, bad), 0u);
    CHECK_EQ(act_note_interaction(t, bad), 0u);
    CHECK_EQ(act_note_networks(t, &h, 1u, bad), 0u);
    CHECK_EQ(act_note_peer(t, (uint32_t)(i + 1), bad), 0u);
  }
  CHECK_EQ(act_score_today(t, bad), 0u);
  CHECK_EQ(t.act_score, 0u);
  CHECK_EQ(t.act_day, 0u);              // no day was even opened
  CHECK_EQ(act_rare_bonus_pm(act_score_today(t, bad)), 0u);
  const ActGain g = act_take_gain();
  CHECK_EQ(g.points, 0u);
  CHECK_EQ(g.xp, 0u);
  CHECK_EQ(g.happy_milli, 0u);
  // AND THE OTHER HALF OF clock_ok(): a caller that claims CAL_USER while
  // handing over an epoch that is not a date is refused too. Without this the
  // uncalibrated estimate (uptime, a few thousand seconds) would divide down to
  // day 0 and every boot would look like the same brand-new day.
  const ActClock lying = clk(4000u, (uint8_t)CAL_USER);
  CHECK_EQ(act_note_interaction(t, lying), 0u);
  CHECK_EQ(t.act_day, 0u);
}

// MUTATION RUN, and the guard is open_day()'s `day > t.act_day`. Relaxing it
// to `day != t.act_day` - which is the obvious way to write "a new day" and is
// wrong - makes THIS case fail, and this is the run:
//   test_activity.cpp:313: FAIL CHECK_EQ(act_note_interaction(t, clk(rolled)), 0u): 2 != 0
//   test_activity.cpp:317: FAIL CHECK_EQ(t.act_day, day_before): 20453 != 20454
//   test_activity.cpp:318: FAIL CHECK_EQ(t.act_score, ACT_SCORE_MAX): 132 != 440
//   FAIL a_clock_rolled_backwards_cannot_reopen_a_spent_day
TEST(a_clock_rolled_backwards_cannot_reopen_a_spent_day) {
  CooldownTable t;
  fresh(t);
  for (uint32_t s = 0; s < 24u * 3600u; s += 60u) (void)act_note_carried(t, 60u, clk(EP0));
  for (int i = 0; i < 40; ++i) (void)act_note_interaction(t, clk(EP0));
  for (uint32_t k = 1; k <= 40u; ++k) {
    const uint32_t h = k * 2654435761u;
    (void)act_note_networks(t, &h, 1u, clk(EP0));
  }
  for (uint32_t k = 1; k <= 20u; ++k) (void)act_note_peer(t, k * 40503u, clk(EP0));
  CHECK_EQ(t.act_score, (uint16_t)ACT_SCORE_MAX);
  const uint16_t day_before = t.act_day;

  // The time screen accepts a CAL_USER rollback of any size (gametime.h), so
  // this is a move the player can really make. Ten days back, then a hundred.
  for (uint32_t back = 1; back <= 100u; ++back) {
    const uint32_t rolled = EP0 - back * (uint32_t)ACT_DAY_S;
    CHECK_EQ(act_note_interaction(t, clk(rolled)), 0u);
    CHECK_EQ(act_note_carried(t, 7200u, clk(rolled)), 0u);
    const uint32_t h = back * 40503u + 1u;
    CHECK_EQ(act_note_networks(t, &h, 1u, clk(rolled)), 0u);
    CHECK_EQ(t.act_day, day_before);                  // the day never moved
    CHECK_EQ(t.act_score, (uint16_t)ACT_SCORE_MAX);   // the budget stands
    // And the rewards read exactly what they read before the rollback.
    CHECK_EQ(act_score_today(t, clk(rolled)), (uint16_t)ACT_SCORE_MAX);
  }
  CHECK_EQ(act_score_today(t, clk(EP0)), (uint16_t)ACT_SCORE_MAX);

  // Rolling FORWARD past the stored day still works - the guard must stop a
  // rollback, not freeze the calendar.
  (void)act_note_interaction(t, clk(EP0 + (uint32_t)ACT_DAY_S));
  CHECK_EQ(t.act_day, (uint16_t)(day_before + 1u));
  CHECK_EQ(t.act_score, (uint16_t)ACT_PTS_INTERACT);
}

// MUTATION RUN, and the guard is that the day's total lives in the PERSISTED
// CooldownTable rather than in the per-boot half. Moving the before/after pair
// bank() reads onto a file-scope `static uint16_t s_ram_score` that act_begin()
// clears makes THIS case fail, and it fails with the size of the farm on the
// line - thirty reboots pay 420 XP where an honest day pays 44:
//   test_activity.cpp:358: FAIL CHECK_EQ(t.act_score, ACT_SCORE_MAX): 140 != 440
//   test_activity.cpp:360: FAIL CHECK_EQ(total_xp, 44): 420 != 44
//   test_activity.cpp:361: FAIL CHECK_EQ(total_happy, 5500): 52500 != 5500
//   FAIL power_cycling_thirty_times_cannot_beat_one_honest_day
// (and three more: the_days_total_is_clamped_and_never_wraps,
//  a_new_day_resets_the_score_and_every_counter and
//  a_clock_rolled_backwards_cannot_reopen_a_spent_day all go red with it.)
TEST(power_cycling_thirty_times_cannot_beat_one_honest_day) {
  CooldownTable t;
  fresh(t);
  uint32_t total_xp = 0, total_happy = 0;

  // The cheapest farm there is: walk past the same ten access points, reboot,
  // walk past them again. Thirty times, all inside one day.
  for (int boot = 0; boot < 30; ++boot) {
    power_cycle();                       // .bss gone, the blob survives
    for (uint32_t k = 1; k <= 10u; ++k) {
      const uint32_t h = k * 2654435761u;
      (void)act_note_networks(t, &h, 1u, clk(EP0 + (uint32_t)boot));
    }
    for (int i = 0; i < 40; ++i) (void)act_note_interaction(t, clk(EP0 + (uint32_t)boot));
    const ActGain g = act_take_gain();
    total_xp    += g.xp;
    total_happy += g.happy_milli;
  }

  // THE CLAIM, and it is the whole design: thirty reboots reach AT MOST what
  // one honest day reaches. Not less - the cheat does work, and it is disclosed
  // in activity.h - but never more, and never a second time.
  CHECK_EQ(t.act_score, (uint16_t)ACT_SCORE_MAX);
  CHECK_EQ(act_score_today(t, clk(EP0)), (uint16_t)ACT_SCORE_MAX);
  CHECK_EQ(total_xp,    (uint32_t)(ACT_SCORE_MAX / ACT_XP_STEP_POINTS));
  CHECK_EQ(total_happy, (uint32_t)(ACT_SCORE_MAX / ACT_HAPPY_STEP_POINTS) *
                        ACT_HAPPY_STEP_MILLI);
  CHECK_EQ(act_rare_bonus_pm(act_score_today(t, clk(EP0))),
           (uint16_t)ENC_RARE_BONUS_MAX_PM);

  // Thirty more reboots pay nothing at all: the day's total is already full and
  // the thresholds it crossed were crossed against a number that is on flash.
  for (int boot = 0; boot < 30; ++boot) {
    power_cycle();
    for (uint32_t k = 1; k <= 10u; ++k) {
      const uint32_t h = k * 2654435761u;
      (void)act_note_networks(t, &h, 1u, clk(EP0));
    }
    const ActGain g = act_take_gain();
    CHECK_EQ(g.xp, 0u);
    CHECK_EQ(g.happy_milli, 0u);
    CHECK_EQ(g.points, 0u);
  }
  CHECK_EQ(t.act_score, (uint16_t)ACT_SCORE_MAX);
}

// MUTATION RUN, and the guard is seen_or_add()'s membership walk. Deleting the
// two lines `for (uint8_t i = 0; i < n; ++i) if (set[i] == v) return true;`
// leaves a set that only ever counts to its cap - which is exactly the cheap
// "count the scans" version this module exists not to be. This is the run:
//   test_activity.cpp:393: FAIL CHECK_EQ(got, (i == 0) ? ACT_PTS_NET : 0u): 10 != 0
//   test_activity.cpp:395: FAIL CHECK_EQ(act_nets_seen(), 1u): 10 != 1
//   FAIL the_same_network_seen_thirty_times_scores_exactly_once
TEST(the_same_network_seen_thirty_times_scores_exactly_once) {
  CooldownTable t;
  fresh(t);
  const uint32_t one = 0xC0FFEE01u;

  for (int i = 0; i < 30; ++i) {
    const uint16_t got = act_note_networks(t, &one, 1u, clk(EP0 + (uint32_t)i * 60u));
    CHECK_EQ(got, (i == 0) ? (uint16_t)ACT_PTS_NET : 0u);
  }
  CHECK_EQ(act_nets_seen(), 1u);
  CHECK_EQ(act_score_today(t, clk(EP0)), (uint16_t)ACT_PTS_NET);

  // The same street, handed over as a whole scan array thirty times, which is
  // how ui/screen_network.cpp really calls it - and with the array in a
  // different order each time, since the driver returns strongest-first and the
  // strongest access point changes as the player moves.
  fresh(t);
  uint32_t street[6];
  for (uint32_t k = 0; k < 6u; ++k) street[k] = (k + 1u) * 2654435761u;
  CHECK_EQ(act_note_networks(t, street, 6u, clk(EP0)), (uint16_t)(6u * ACT_PTS_NET));
  for (int pass = 0; pass < 30; ++pass) {
    for (uint32_t k = 0; k < 6u; ++k) street[k] = (((uint32_t)pass + k) % 6u + 1u) * 2654435761u;
    CHECK_EQ(act_note_networks(t, street, 6u, clk(EP0)), 0u);
  }
  CHECK_EQ(act_nets_seen(), 6u);
  CHECK_EQ(act_score_today(t, clk(EP0)), (uint16_t)(6u * ACT_PTS_NET));

  // The same peer met thirty times, for the same reason.
  fresh(t);
  for (int i = 0; i < 30; ++i) {
    const uint16_t got = act_note_peer(t, 0xABCDEF01u, clk(EP0));
    CHECK_EQ(got, (i == 0) ? (uint16_t)ACT_PTS_PEER : 0u);
  }
  CHECK_EQ(act_peers_seen(), 1u);
}

TEST(a_zero_hash_and_a_zero_peer_id_are_never_diversity) {
  CooldownTable t;
  fresh(t);
  // net_hash 0 is CooldownRow's empty-row marker: a network that hashed to 0 is
  // never explorable (game/cooldowns.h refuses it outright), so it must never
  // be counted as somewhere new either - and it must not consume a set slot.
  uint32_t zeros[4] = { 0u, 0u, 0u, 0u };
  CHECK_EQ(act_note_networks(t, zeros, 4u, clk(EP0)), 0u);
  CHECK_EQ(act_nets_seen(), 0u);
  CHECK_EQ(act_note_peer(t, 0u, clk(EP0)), 0u);
  CHECK_EQ(act_peers_seen(), 0u);
  // A null array and an empty scan are ordinary, not errors.
  CHECK_EQ(act_note_networks(t, nullptr, 4u, clk(EP0)), 0u);
  CHECK_EQ(act_note_networks(t, zeros, 0u, clk(EP0)), 0u);
  CHECK_EQ(t.act_score, 0u);
}

// =============================================================================
//  6. THE PERSISTED HALF, AND WHAT act_begin() MAY AND MAY NOT TOUCH
// =============================================================================
TEST(act_begin_clears_the_boot_half_and_never_the_blob) {
  CooldownTable t;
  fresh(t);
  for (int i = 0; i < 5; ++i) (void)act_note_interaction(t, clk(EP0));
  const uint16_t day = t.act_day, score = t.act_score;
  CHECK(score > 0u);

  act_begin();
  CHECK_EQ(t.act_day, day);              // the blob is untouched...
  CHECK_EQ(t.act_score, score);
  CHECK_EQ(act_interactions(), 0u);      // ...and the .bss is gone
  CHECK_EQ(act_nets_seen(), 0u);
  CHECK_EQ(act_carry_min(), 0u);
  const ActGain g = act_take_gain();
  CHECK_EQ(g.points, 0u);
  CHECK_EQ(g.xp, 0u);
  CHECK(!act_take_dirty());
}

TEST(the_dirty_flag_is_take_and_clear_and_fires_for_both_writers) {
  CooldownTable t;
  fresh(t);
  CHECK(!act_take_dirty());

  (void)act_note_interaction(t, clk(EP0));      // opened a day AND banked points
  CHECK(act_take_dirty());
  CHECK(!act_take_dirty());                     // take-and-clear

  (void)act_note_interaction(t, clk(EP0));      // banked points only
  CHECK(act_take_dirty());

  // A note that changes nothing owes no write.
  for (int i = 0; i < 100; ++i) (void)act_note_interaction(t, clk(EP0));
  (void)act_take_dirty();
  CHECK_EQ(act_interactions(), (uint8_t)ACT_CAP_INTERACT);
  CHECK_EQ(act_note_interaction(t, clk(EP0)), 0u);
  CHECK(!act_take_dirty());

  // The day rolling is a write even when nothing is banked with it.
  (void)act_note_carried(t, 1u, clk(EP0 + (uint32_t)ACT_DAY_S));
  CHECK(act_take_dirty());
}

TEST(an_old_blob_reads_as_no_day_recorded_and_needs_no_migration) {
  // A save written before P6-C2 has zeroes in the four bytes that were
  // reserved_a[4]. Day index 0 is 1970-01-01, which is below NT_EPOCH_SANE_MIN
  // and can therefore never be a real day, so 0 is unambiguously "none yet" -
  // exactly the argument BugInstance.corrupt_until_epoch was carved out of
  // reserved[12] on. The first note opens today.
  CooldownTable t;
  fresh(t);
  CHECK_EQ(t.act_day, 0u);
  CHECK_EQ(t.act_score, 0u);
  CHECK_EQ(act_score_today(t, clk(EP0)), 0u);
  CHECK_EQ(act_note_interaction(t, clk(EP0)), (uint16_t)ACT_PTS_INTERACT);
  CHECK_EQ(t.act_day, act_day_index(EP0));

  // AND THE FOUR BYTES ARE WHERE THE SCHEMA SAYS THEY ARE. A compiler or ABI
  // change that reshaped the blob would move a live save's rows under it.
  CHECK_EQ(offsetof(CooldownTable, act_day), 8u);
  CHECK_EQ(offsetof(CooldownTable, act_score), 10u);
  CHECK_EQ(offsetof(CooldownTable, rows), 12u);
  CHECK_EQ(sizeof(CooldownTable), 272u);
}

// =============================================================================
//  7. THE DISTINCT SET, MEASURED RATHER THAN ASSERTED
// =============================================================================
TEST(the_network_set_is_exact_and_saturating_over_a_long_walk) {
  CooldownTable t;
  fresh(t);
  // 2,000 distinct access points, each offered six times in a shuffled order -
  // the shape of a real day, where the same routers reappear scan after scan.
  // The claim: EXACTLY ACT_CAP_NETS credited, never one more, never one fewer,
  // and no repeat ever pays twice.
  uint32_t credited = 0;
  for (uint32_t pass = 0; pass < 6u; ++pass) {
    for (uint32_t k = 1; k <= 2000u; ++k) {
      // A different visiting order every pass, over the same 2,000 identities.
      const uint32_t id = ((k * 7919u) + pass * 1013u) % 2000u + 1u;
      const uint32_t h  = id * 2654435761u;
      if (act_note_networks(t, &h, 1u, clk(EP0)) != 0u) credited++;
    }
  }
  CHECK_EQ(credited, (uint32_t)ACT_CAP_NETS);
  CHECK_EQ(act_nets_seen(), (uint8_t)ACT_CAP_NETS);
  CHECK_EQ(act_score_today(t, clk(EP0)), (uint16_t)(ACT_CAP_NETS * ACT_PTS_NET));

  // Saturation is not a degradation: once full the set answers "seen" to
  // everything, and everything is right, because the term is at its cap.
  const uint32_t brand_new = 0xFEEDFACEu;
  CHECK_EQ(act_note_networks(t, &brand_new, 1u, clk(EP0)), 0u);
}


// =============================================================================
//  8. THE DIRECTION THE CLOCK CAN BE MOVED THAT ACTUALLY PAYS  (P6-C4)
//
//  This file shipped with four named cheat cases - an uncalibrated clock, a
//  clock rolled BACKWARDS, a power cycle, and a rescan - and none for a clock
//  moved FORWARD, which is the only direction that resets act_score and the only
//  one worth typing. Worse, a_new_day_resets_the_score_and_every_counter blessed
//  the reset mechanism without bounding how often a day can be created, so the
//  suite certified the farm's engine as correct.
//
//  These two cases say what is true, in both directions, and neither of them
//  claims this module stops the farm - it cannot, and pretending otherwise is
//  how the hole survived. A typed day and a day the device spent switched off
//  are the same evidence: one clock, moved forward, by nobody who was watching.
//  What a manufactured day is WORTH is bounded in game/xp.cpp, and the cases
//  that measure THAT are in tests/test_xp.cpp, which is the only binary that
//  links both modules.
// =============================================================================
TEST(a_forward_clock_set_opens_one_day_at_a_time_and_never_more_than_one_budget) {
  CooldownTable t;
  fresh(t);

  // A hundred days typed onto the clock, one at a time, each one earning the
  // best score the module will give it. Every one of them is capped at
  // ACT_SCORE_MAX, the day index tracks the clock exactly, and no day is ever
  // opened twice.
  uint32_t days_opened = 0;
  for (uint32_t d = 0; d < 100u; ++d) {
    const uint32_t ep = EP0 + d * (uint32_t)ACT_DAY_S;
    const uint16_t before_day = t.act_day;
    power_cycle();                                   // maximally generous: reboot too
    (void)act_note_interaction(t, clk(ep));
    if (t.act_day != before_day) days_opened++;

    // Drive the day as hard as it can be driven, with all-new inputs.
    for (uint32_t k = 0; k < 500u; ++k) {
      (void)act_note_carried(t, 60u, clk(ep));
      (void)act_note_interaction(t, clk(ep));
      const uint32_t h = (d * 1000u + k) * 2654435761u + 1u;
      (void)act_note_networks(t, &h, 1u, clk(ep));
      (void)act_note_peer(t, h ^ 0x5A5A5A5Au, clk(ep));
    }
    CHECK_EQ(act_score_today(t, clk(ep)), (uint16_t)ACT_SCORE_MAX);
    CHECK_EQ(t.act_day, act_day_index(ep));
  }
  CHECK_EQ(days_opened, 100u);       // one open per day index, never two

  // AND SEVERAL JUMPS INSIDE ONE DAY OPEN NOTHING. A player who types the same
  // date twice, or an hour forward, gets no second budget: the day index is the
  // unit, not the edit.
  const uint32_t last = EP0 + 99u * (uint32_t)ACT_DAY_S;
  const uint16_t day_before = t.act_day;
  for (uint32_t h = 1; h < 24u; ++h) {
    (void)act_note_interaction(t, clk(last + h * 3600u));
    CHECK_EQ(t.act_day, day_before);
    CHECK_EQ(act_score_today(t, clk(last + h * 3600u)), (uint16_t)ACT_SCORE_MAX);
  }

  // A JUMP OF TWENTY-SEVEN YEARS IS STILL ONE DAY. It is a bigger number, not a
  // bigger budget - the score is the day's, and there is only ever one day open.
  // (10,000 days is as far as this can go and stay a date: EP0 plus a century of
  // seconds wraps u32 back to 1989, which clock_ok() then refuses outright.)
  const uint32_t far = EP0 + 10000u * (uint32_t)ACT_DAY_S;
  power_cycle();
  (void)act_note_interaction(t, clk(far));
  CHECK_EQ(t.act_day, act_day_index(far));
  CHECK_EQ(act_score_today(t, clk(far)), (uint16_t)ACT_PTS_INTERACT);
}

TEST(the_happiness_half_of_the_reward_is_scaled_by_the_xp_the_ledger_granted) {
  // act_happy_for_granted_xp() is the whole of the happiness rate limit: the
  // reward has no meter of its own and borrows XP_SRC_CARRY's. Until P6-C4 it
  // had none at all, and 100 typed days paid 125,000 care milli-points into a
  // bar that holds 100,000 - with no reboot and no real time.
  ActGain g;
  g.points = 100u; g.xp = 10u; g.happy_milli = 1250u;

  CHECK_EQ(act_happy_for_granted_xp(g, 0u), 0u);           // nothing granted, nothing paid
  CHECK_EQ(act_happy_for_granted_xp(g, 10u), 1250u);       // paid in full
  CHECK_EQ(act_happy_for_granted_xp(g, 99u), 1250u);       // and never more than in full
  CHECK_EQ(act_happy_for_granted_xp(g, 5u), 625u);         // half of it
  CHECK_EQ(act_happy_for_granted_xp(g, 1u), 125u);

  // Monotone in the XP granted, and never above the owed amount, over the whole
  // range - so a partial grant can never pay more than a complete one.
  uint16_t prev = 0u;
  for (uint16_t granted = 0; granted <= 40u; ++granted) {
    const uint16_t h = act_happy_for_granted_xp(g, granted);
    CHECK(h >= prev);
    CHECK(h <= g.happy_milli);
    prev = h;
  }

  // A gain with no XP behind it pays no happiness. That case cannot arise from
  // the score - ACT_HAPPY_STEP_POINTS is a multiple of ACT_XP_STEP_POINTS, so
  // every happiness threshold is an XP threshold - and the next block measures
  // that rather than trusting the static_assert.
  ActGain none; none.points = 20u; none.xp = 0u; none.happy_milli = 250u;
  CHECK_EQ(act_happy_for_granted_xp(none, 7u), 0u);

  // EVERY DRAIN THAT OWES HAPPINESS ALSO OWES XP, driven over a whole day at
  // every step size a real caller can produce (a carried minute is 1 point, an
  // interaction 2, a network 10, a peer 15).
  static const uint16_t kSteps[] = { (uint16_t)ACT_PTS_CARRY, (uint16_t)ACT_PTS_INTERACT,
                                     (uint16_t)ACT_PTS_NET,   (uint16_t)ACT_PTS_PEER };
  static const uint16_t kCaps[]  = { (uint16_t)ACT_CAP_CARRY_MIN, (uint16_t)ACT_CAP_INTERACT,
                                     (uint16_t)ACT_CAP_NETS,      (uint16_t)ACT_CAP_PEERS };
  for (uint8_t si = 0; si < 4u; ++si) {
    CooldownTable t;
    fresh(t);
    uint32_t total = 0;
    for (uint32_t k = 0; k < 600u; ++k) {
      const uint32_t h = (uint32_t)si * 100000u + k + 1u;
      switch (si) {
        case 0: (void)act_note_carried(t, 60u, clk(EP0)); break;
        case 1: (void)act_note_interaction(t, clk(EP0)); break;
        case 2: (void)act_note_networks(t, &h, 1u, clk(EP0)); break;
        default: (void)act_note_peer(t, h, clk(EP0)); break;
      }
      const ActGain gg = act_take_gain();
      if (gg.happy_milli != 0u) CHECK(gg.xp != 0u);
      CHECK_EQ(act_happy_for_granted_xp(gg, gg.xp), gg.happy_milli);
      total = (uint32_t)(total + gg.points);
    }
    // AND THE PASS REALLY DROVE THAT TERM TO ITS CAP, so a switch arm that
    // silently scored nothing could not sit here looking like a clean run.
    CHECK_EQ(total, (uint32_t)kCaps[si] * (uint32_t)kSteps[si]);
    CHECK_EQ(act_score_today(t, clk(EP0)), (uint16_t)total);
  }
}

// =============================================================================
//  A DAY INDEX THE CLOCK CAN NEVER REACH FREEZES THE SCORE FOR EVER (P7-C6)
//
//  open_day()'s only roll condition is `day > t.act_day`. act_day is a u16 that
//  holds 65,535 and the widest day a u32 epoch can name is 49,710, so a blob
//  carrying anything in between names a day nothing can beat: the day never
//  rolls, the score sticks, and every later day is worth one term's cap.
//  act_adopt() is the one look at that field, at boot, before anything scores.
// =============================================================================
TEST(a_day_index_beyond_the_clock_freezes_the_score_and_the_boot_clamp_removes_it) {
  // THE CEILING IS WHAT THE CLOCK CAN NAME, not what the field can hold.
  CHECK_EQ((uint32_t)ACT_DAY_MAX_INDEX, 0xFFFFFFFFul / (uint32_t)ACT_DAY_S);
  CHECK(ACT_DAY_MAX_INDEX < 0xFFFFu);

  // --- THE UNCLAMPED ARM, so the case cannot pass because the defect is gone
  //     for some other reason. Ten simulated years against a poisoned table.
  CooldownTable poisoned;
  fresh(poisoned);
  poisoned.act_day   = 0xFFFFu;
  poisoned.act_score = 0u;
  uint32_t frozen_total = 0u;
  for (uint16_t d = 0; d < 3650u; ++d) {
    const uint32_t ep = EP0 + (uint32_t)d * (uint32_t)ACT_DAY_S;
    act_begin();                                     // a boot every day
    for (uint8_t k = 0; k < (uint8_t)ACT_CAP_INTERACT; ++k) {
      frozen_total += act_note_interaction(poisoned, clk(ep));
    }
  }
  CHECK_EQ(poisoned.act_day, 0xFFFFu);               // it never rolled, not once
  // ONE day's ceiling for TEN YEARS of play: the score saturates at
  // ACT_SCORE_MAX and, because the day never rolls, nothing ever resets it.
  CHECK(frozen_total <= (uint32_t)ACT_SCORE_MAX);
  CHECK_EQ(poisoned.act_score, (uint16_t)frozen_total);

  // --- THE CLAMPED ARM: the same ten years, through the boot clamp.
  CooldownTable healed;
  fresh(healed);
  healed.act_day   = 0xFFFFu;
  healed.act_score = 1234u;
  CHECK(act_adopt(healed));                          // it repaired something
  CHECK_EQ(healed.act_day, 0u);                      // "no day recorded"
  CHECK_EQ(healed.act_score, 0u);                    // and nothing carried in

  uint32_t honest_total = 0u;
  for (uint16_t d = 0; d < 3650u; ++d) {
    const uint32_t ep = EP0 + (uint32_t)d * (uint32_t)ACT_DAY_S;
    act_begin();
    for (uint8_t k = 0; k < (uint8_t)ACT_CAP_INTERACT; ++k) {
      honest_total += act_note_interaction(healed, clk(ep));
    }
  }
  CHECK_EQ(honest_total,
           3650ul * (uint32_t)ACT_CAP_INTERACT * (uint32_t)ACT_PTS_INTERACT);
  CHECK(honest_total > frozen_total * 100ul);        // the gap is the defect
  fprintf(stderr, "     ten years: %u points frozen, %u points healed\n",
          (unsigned)frozen_total, (unsigned)honest_total);

  // --- AND IT TOUCHES NOTHING ELSE. Every honest table is left alone, so the
  //     clamp cannot cost a player a day they really earned.
  CooldownTable honest;
  fresh(honest);
  honest.act_day   = act_day_index(EP0);
  honest.act_score = 321u;
  CHECK(!act_adopt(honest));
  CHECK_EQ(honest.act_day, act_day_index(EP0));
  CHECK_EQ(honest.act_score, 321u);

  CooldownTable edge;
  fresh(edge);
  edge.act_day   = ACT_DAY_MAX_INDEX;                // the last reachable day
  edge.act_score = 7u;
  CHECK(!act_adopt(edge));
  CHECK_EQ(edge.act_day, (uint16_t)ACT_DAY_MAX_INDEX);
  CHECK_EQ(edge.act_score, 7u);

  CooldownTable just_over;
  fresh(just_over);
  just_over.act_day = (uint16_t)(ACT_DAY_MAX_INDEX + 1u);
  CHECK(act_adopt(just_over));                       // one past it is repaired
  CHECK_EQ(just_over.act_day, 0u);

  CooldownTable empty;
  fresh(empty);
  CHECK(!act_adopt(empty));                          // a fresh device, untouched
  CHECK_EQ(empty.act_day, 0u);
}
