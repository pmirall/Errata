// =============================================================================
//  Pebblebol host tests - test_care.cpp
//  Spec section 27, "Pebbles should be inconveniently unhappy at worst, not
//  permanently destroyed". P2-C7 removed death outright and gave HEALTH a
//  floor; this pins both, in the only way that matters - by playing the worst
//  possible player for a month and checking the pebble is still there.
//
//  P3-C1 moved the whole model onto the HOURS scale of spec section 27 and
//  added the five cases the plan names for it: eight ignored hours are still a
//  happy pebble, a day of neglect never crosses the floor, a week in the Box
//  comes back full, one big catch-up equals many small ones, and the sleep
//  window really does at least halve the decay. Plus one the plan implies and
//  the balance table states outright: a refused action is a toast, not a fine.
//
//  P3-C2b deleted the light mechanic and put the sleep window on the
//  approximated daylight table (game/daylight.h). Sections 12 to 15 below are
//  the behaviour that replaces it: the pebble sleeps by the sun, three nudges
//  inside ten seconds wake it for a player who wants to play at night, it
//  relapses when left alone again, and none of that costs a stat.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "core/config.h"
#include "data/balance.h"
#include "data/species_table.h"
#include "game/sim.h"
#include "game/genome.h"
#include "game/box.h"
#include "persistence/save_schema.h"

#define CARE_SEED    0x5EED0C7Au
#define CARE_EPOCH0  1700000000u
#define CARE_DAYS    30u

static PebbleInstance g_care;

// A hatched baby with a valid clock at `hour` local on day `doy`, and nothing
// else. Day 100 is mid-April: sunrise about 07:20, sunset about 20:55, so
// bedtime lands about 22:25 (game/daylight.h).
static void care_pet_at_day(PebbleInstance& p, uint8_t hour, uint16_t doy) {
  genome_seed(CARE_SEED);
  sim_seed(CARE_SEED);
  memset(&p, 0, sizeof(PebbleInstance));
  sim_bind(p);
  sim_new_pet(genome_genesis(), CARE_EPOCH0, 0);
  sim_hatch();

  SimEnv env;
  sim_env_defaults(env);
  env.now_epoch   = CARE_EPOCH0;
  env.clock_valid = 1;
  env.local_hour  = hour;
  env.local_min   = 0;
  env.day_of_year = doy;
  sim_set_env(env);
}

static void care_pet_at(PebbleInstance& p, uint8_t hour) {
  care_pet_at_day(p, hour, 100);
}

// The original fixture: the same pebble, at 10:00.
static void care_pet(void) { care_pet_at(g_care, 10); }

// Advances the wall clock and the sim by one minute, keeping SimEnv in step.
static void care_minute(SimEnv& env) {
  sim_tick(60);
  env.now_epoch += 60;
  if (++env.local_min >= 60) {
    env.local_min = 0;
    if (++env.local_hour >= 24) {
      env.local_hour = 0;
      env.day_of_year = (uint16_t)((env.day_of_year + 1u) % 366u);
    }
  }
  sim_set_env(env);
}

// -----------------------------------------------------------------------------
//  1. Thirty days of total neglect
// -----------------------------------------------------------------------------
TEST(care_thirty_days_of_neglect_never_kills_the_pet) {
  care_pet();
  SimEnv env = sim_env();

  const uint8_t stage0 = sim_view()->stage;
  uint8_t min_health = 100;

  for (uint32_t m = 0; m < CARE_DAYS * 24u * 60u; ++m) {
    care_minute(env);

    // The floor holds on EVERY minute, not merely at the end.
    CHECK(sim_stat_milli(ST_HEALTH) >= (int32_t)HEALTH_FLOOR_PCT * 1000);
    const uint8_t h = sim_stat_pct(ST_HEALTH);
    if (h < min_health) min_health = h;
  }

  CHECK(sim_stat_milli(ST_HEALTH) > 0);
  CHECK(sim_stat_pct(ST_HEALTH) >= HEALTH_FLOOR_PCT);
  // Inconveniently unhappy: the neglect really did bite, it just did not kill.
  CHECK_EQ((int)min_health, (int)HEALTH_FLOOR_PCT);
  CHECK_EQ(sim_stat_pct(ST_HUNGER), 0);
  CHECK_EQ(sim_stat_pct(ST_HAPPINESS), 0);
  // And it grew up regardless: a neglected pebble is still a pebble.
  CHECK(sim_view()->stage > stage0);
  CHECK(sim_view()->stage < STAGE_COUNT);
}

// -----------------------------------------------------------------------------
//  2. No death flag survives anywhere in the live pet
//     PF_DEAD / PF_BURIED / PF_SCAR were bits 0x0008 / 0x0010 / 0x0020 and are
//     retired. Nothing may set them again, and there is no STAGE_DEAD to reach.
// -----------------------------------------------------------------------------
#define CARE_RETIRED_DEATH_FLAGS ((uint16_t)0x0038u)

TEST(care_neglect_sets_no_death_flag) {
  care_pet();
  SimEnv env = sim_env();

  for (uint32_t m = 0; m < CARE_DAYS * 24u * 60u; ++m) {
    care_minute(env);
    CHECK((sim_view()->flags & CARE_RETIRED_DEATH_FLAGS) == 0);
    CHECK(sim_view()->stage < STAGE_COUNT);
  }

  // sim_take_events() has been accumulating for a month: no event bit above
  // SIM_EV_EVOLVE_MINOR exists any more, and none of them means "died".
  const uint32_t ev = sim_take_events();
  CHECK((ev & ~(SIM_EV_HATCHED | SIM_EV_STAGE_UP | SIM_EV_POOP |
                SIM_EV_SICK_START | SIM_EV_SICK_END | SIM_EV_SLEEP |
                SIM_EV_WAKE | SIM_EV_ALERT | SIM_EV_WISH_START |
                SIM_EV_WISH_OK | SIM_EV_WISH_FAIL | SIM_EV_BIRTHDAY |
                SIM_EV_VISITA | SIM_EV_EVOLVE_MINOR)) == 0u);
}

// -----------------------------------------------------------------------------
//  3. A month offline is no worse than a month present
//     The offline catch-up runs the same integrator at MULT_OFFLINE_DECAY, so
//     the escalation ladder's absence is not a hole the floor can fall through.
// -----------------------------------------------------------------------------
TEST(care_a_month_offline_also_stops_at_the_floor) {
  care_pet();
  AbsenceReport rep;
  sim_catch_up_ex(CARE_DAYS * 86400u, 1 /* clock known */, rep);

  CHECK_EQ(rep.clock_known, 1);
  CHECK(rep.steps > 0);
  CHECK(sim_stat_milli(ST_HEALTH) >= (int32_t)HEALTH_FLOOR_PCT * 1000);
  CHECK((sim_view()->flags & CARE_RETIRED_DEATH_FLAGS) == 0);
  CHECK(sim_view()->stage < STAGE_COUNT);
}

// -----------------------------------------------------------------------------
//  4. Health recovers once the core stats are back up
//     A floor that only ever floors would be a different bug: the pebble has to
//     be able to come back from it.
// -----------------------------------------------------------------------------
TEST(care_health_regenerates_after_the_neglect_ends) {
  care_pet();
  SimEnv env = sim_env();

  for (uint32_t m = 0; m < 7u * 24u * 60u; ++m) care_minute(env);
  const int32_t rock_bottom = sim_stat_milli(ST_HEALTH);
  CHECK_EQ(rock_bottom, (int32_t)HEALTH_FLOOR_PCT * 1000);

  // The owner comes back and does the one thing the model cannot do for itself.
  for (uint8_t i = 0; i < ST_CORE_COUNT; ++i) {
    sim_god_set_stat((StatId)i, 100);
  }
  sim_god_set_sick(0);

  for (uint32_t m = 0; m < 120u; ++m) care_minute(env);
  CHECK(sim_stat_milli(ST_HEALTH) > rock_bottom);
}

// -----------------------------------------------------------------------------
//  5. Swapping the active Pebble is not a way to farm (plan P2-C10)
//     sim_switch() resets the PER-PEBBLE accumulators and keeps the DEVICE-WIDE
//     hourly gain ledger, so ten Pebbles share one hour's points.
// -----------------------------------------------------------------------------
static PebbleInstance g_other;

TEST(care_switching_the_active_pebble_keeps_the_gain_ledger) {
  care_pet();

  // Spend the satiety budget down on the pet we are holding.
  sim_god_set_stat(ST_HUNGER, 0);
  ActionResult r;
  CHECK(sim_apply_action(ACT_FEED_MEAL, r));
  const uint16_t left_after_meal = sim_gain_left(ST_HUNGER);
  CHECK(left_after_meal < (uint16_t)GAIN_CAP_HUNGER_H);

  // A second Pebble, straight out of the Box.
  memset(&g_other, 0, sizeof g_other);
  g_other.magic      = (uint16_t)PEBBLE_MAGIC;
  g_other.layout_ver = (uint8_t)PEBBLE_LAYOUT_VER;
  g_other.species_id = 1;
  g_other.id         = 0x2222u;
  g_other.level      = 1;
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) {
    g_other.care[i] = (int32_t)PB_CARE_MILLI_MAX;
  }
  g_other.care[CARE_HUNGER] = 0;

  sim_switch(g_other);
  CHECK_EQ(sim_pebble(), &g_other);

  // The ledger did NOT restart with the new creature: that is the whole point.
  CHECK_EQ(sim_gain_left(ST_HUNGER), left_after_meal);

  // Per-Pebble state DID: the new Pebble carries no care quality, no poop and
  // no wish of the old one.
  CHECK_EQ((int)sim_view()->poop_count, 0);
  CHECK_EQ((int)sim_view()->cq, (int)CQ_START);

  // And switching back and forth cannot mint points either.
  sim_switch(g_care);
  CHECK_EQ(sim_gain_left(ST_HUNGER), left_after_meal);
}

// -----------------------------------------------------------------------------
//  6. Eight ignored hours are still a happy pebble (plan P3-C1)
//     The point of the hours scale: a working day away from the device costs
//     real points and nothing else. Happiness decays at 3.000 milli/h, so eight
//     hours - six of them before the loneliness multiplier even starts - land
//     comfortably above 60 %, and no bar has reached zero, so health is intact.
// -----------------------------------------------------------------------------
TEST(care_eight_ignored_hours_stay_above_sixty_percent_happiness) {
  care_pet();
  SimEnv env = sim_env();

  for (uint32_t m = 0; m < 8u * 60u; ++m) care_minute(env);

  CHECK(sim_stat_pct(ST_HAPPINESS) > 60);
  CHECK(sim_stat_pct(ST_HUNGER)    > 50);   // 8 h of 4.200/h off a full bar
  CHECK_EQ((int)sim_stat_pct(ST_HEALTH), 100);
}

// -----------------------------------------------------------------------------
//  7. A full day of neglect never crosses the floor
//     A day of silence is the worst a single absence can do. Hunger is only
//     just empty by then, and a bar that does bottom out costs health nothing
//     for the first CARE_ZERO_GRACE_S and then bleeds so slowly that a whole
//     day still ends far above the floor. Since P3-C2b the day also contains a
//     night, which the pebble spends asleep recharging, so this is now the
//     easier case rather than the harder one.
// -----------------------------------------------------------------------------
TEST(care_twentyfour_hours_of_neglect_never_crosses_the_floor) {
  care_pet();
  SimEnv env = sim_env();

  for (uint32_t m = 0; m < 24u * 60u; ++m) {
    care_minute(env);
    CHECK(sim_stat_milli(ST_HEALTH) >= (int32_t)HEALTH_FLOOR_PCT * 1000);
    for (uint8_t i = 0; i < ST_CORE_COUNT; ++i) {
      CHECK(sim_stat_milli((StatId)i) >= 0);
    }
  }
  // Inconvenient, nowhere near destroyed: a day of neglect is worth well under
  // half the distance from full health to the floor.
  CHECK(sim_stat_pct(ST_HEALTH) > 60);
  // And the first hours really were free: 2 h of grace at 2.000 milli/h means
  // a day cannot cost more than (24 - 2) * 2 = 44 points even in the worst case.
  CHECK(sim_stat_pct(ST_HEALTH) >= 100 - 44);
}

// -----------------------------------------------------------------------------
//  8. A week in the Box comes back full (spec section 9 / section 27)
//     Stored Pebbles are not simulated: they only recover, at BOX_RECOVER_MPH,
//     which fills an empty bar in about a day. A week is far past that.
// -----------------------------------------------------------------------------
static GameState g_box_state;

TEST(care_a_pebble_stored_for_a_week_comes_back_full) {
  memset(&g_box_state, 0, sizeof g_box_state);
  for (uint8_t i = 0; i < (uint8_t)BOX_SLOTS; ++i) {
    g_box_state.pebbles[i].magic      = (uint16_t)PEBBLE_MAGIC;
    g_box_state.pebbles[i].layout_ver = (uint8_t)PEBBLE_LAYOUT_VER;
  }
  g_box_state.box.magic           = (uint16_t)BOX_MAGIC;
  g_box_state.box.active_slot     = (uint8_t)BOX_ACTIVE_NONE;
  g_box_state.box.next_id_counter = 1;
  g_box_state.cfg.device_id       = 0xB0FFE501u;
  box_bind(g_box_state);

  Genome gz;
  memset(&gz, 0, sizeof gz);
  const uint8_t held   = box_new_pebble(SPECIES_ID_STARTER, 1, (uint8_t)ORIGIN_STARTER,
                                        gz, 0x1001u, CARE_EPOCH0);
  const uint8_t stored = box_new_pebble(SPECIES_ID_STARTER, 1, (uint8_t)ORIGIN_WILD,
                                        gz, 0x1002u, CARE_EPOCH0);
  CHECK(held   != (uint8_t)BOX_SLOT_NONE);
  CHECK(stored != (uint8_t)BOX_SLOT_NONE);
  CHECK(box_set_active(held));

  PebbleInstance* p = box_slot(stored);
  CHECK(p != nullptr);
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) { p->care[i] = 0; p->care_rem[i] = 0; }

  // A single day already fills it; a week cannot do less and cannot overshoot.
  box_recover(stored, 86400u);
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) {
    CHECK_EQ(p->care[i], (int32_t)PB_CARE_MILLI_MAX);
  }

  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) { p->care[i] = 0; p->care_rem[i] = 0; }
  box_recover(stored, 7u * 86400u);
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) {
    CHECK_EQ(p->care[i], (int32_t)PB_CARE_MILLI_MAX);
    CHECK_EQ((int)p->care_rem[i], 0);
  }
  // Stored means stored: the sim never ran, so nothing else moved.
  CHECK_EQ((int)p->level, 1);
  CHECK_EQ((int)p->age_s, 0);
}

// -----------------------------------------------------------------------------
//  9. Catch-up chunk equivalence (plan P3-C1, plan 1.7)
//     One hour handed over in one call, in sixty, or in six must land on the
//     same bytes: the integrator carries its remainder, so step size is not a
//     tuning knob a player could find.
// -----------------------------------------------------------------------------
static PebbleInstance g_chunk_a;
static PebbleInstance g_chunk_b;

static void care_check_same(const PebbleInstance& a, const PebbleInstance& b) {
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) {
    CHECK_EQ(b.care[i], a.care[i]);
    CHECK_EQ((int)b.care_rem[i], (int)a.care_rem[i]);
  }
  CHECK_EQ(b.age_s, a.age_s);
  CHECK_EQ(b.last_updated_epoch, a.last_updated_epoch);
  CHECK_EQ((int)b.status, (int)a.status);
}

TEST(care_one_hour_of_catch_up_is_the_same_however_it_is_chunked) {
  care_pet_at(g_chunk_a, 10);
  sim_tick(3600u);
  const PebbleInstance one_call = g_chunk_a;

  care_pet_at(g_chunk_b, 10);
  for (uint32_t i = 0; i < 60u; ++i) sim_tick(60u);
  care_check_same(one_call, g_chunk_b);

  care_pet_at(g_chunk_b, 10);
  for (uint32_t i = 0; i < 6u; ++i) sim_tick(600u);
  care_check_same(one_call, g_chunk_b);
}

// -----------------------------------------------------------------------------
// 10. The sleep window at least halves the decay
//     MULT_SLEEP is x0.35, so a sleeping hour costs well under half of a waking
//     one. Asserting "at most half" is the contract; the exact factor is a
//     tuning value and the golden pins that.
// -----------------------------------------------------------------------------
static PebbleInstance g_awake;
static PebbleInstance g_asleep;

TEST(care_the_sleep_window_at_least_halves_the_decay) {
  // Awake: 10:00 -> 11:00, broad daylight.
  care_pet_at(g_awake, 10);
  sim_god_set_stat(ST_ENERGY, 50);          // below the auto-wake ceiling
  SimEnv env_a = sim_env();
  const int32_t hunger_a0 = sim_stat_milli(ST_HUNGER);
  const int32_t happy_a0  = sim_stat_milli(ST_HAPPINESS);
  for (uint32_t m = 0; m < 60u; ++m) care_minute(env_a);
  CHECK((sim_view()->flags & PF_ASLEEP) == 0);
  const int32_t hunger_awake = hunger_a0 - sim_stat_milli(ST_HUNGER);
  const int32_t happy_awake  = happy_a0  - sim_stat_milli(ST_HAPPINESS);
  CHECK(hunger_awake > 0);
  CHECK(happy_awake  > 0);

  // Asleep: 23:00 -> 00:00, past mid-April's 22:25 bedtime. Nothing has to be
  // switched off first any more - the sun going down is the whole condition.
  care_pet_at(g_asleep, 23);
  sim_god_set_stat(ST_ENERGY, 50);
  SimEnv env_s = sim_env();
  const int32_t hunger_s0 = sim_stat_milli(ST_HUNGER);
  const int32_t happy_s0  = sim_stat_milli(ST_HAPPINESS);
  for (uint32_t m = 0; m < 60u; ++m) care_minute(env_s);
  CHECK((sim_view()->flags & PF_ASLEEP) != 0);
  const int32_t hunger_sleep = hunger_s0 - sim_stat_milli(ST_HUNGER);
  const int32_t happy_sleep  = happy_s0  - sim_stat_milli(ST_HAPPINESS);

  CHECK(hunger_sleep * 2 <= hunger_awake);
  CHECK(happy_sleep  * 2 <= happy_awake);
  // And the night is when energy comes back, not when it drains.
  CHECK(sim_stat_milli(ST_ENERGY) > 50 * 1000);
}

// -----------------------------------------------------------------------------
// 11. A refused action is a friendly toast, never a penalty (balance.h section 2)
// -----------------------------------------------------------------------------
static PebbleInstance g_refuse;

TEST(care_a_refused_action_costs_the_player_nothing) {
  care_pet_at(g_refuse, 10);

  int32_t before[PB_CARE_COUNT];
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) before[i] = g_refuse.care[i];
  const int16_t cq_before = sim_view()->cq;

  // Full satiety: a meal is refused.
  ActionResult r;
  CHECK(!sim_apply_action(ACT_FEED_MEAL, r));
  CHECK_EQ((int)r.err, (int)AERR_FULL);
  // Not sick: medicine is refused.
  CHECK(!sim_apply_action(ACT_MEDICINE, r));
  CHECK_EQ((int)r.err, (int)AERR_NOT_SICK);

  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) {
    CHECK_EQ(g_refuse.care[i], before[i]);
  }
  CHECK_EQ((int)sim_view()->cq, (int)cq_before);
  // And a refusal does not even start the cooldown it would have charged.
  CHECK_EQ((int)sim_action_cooldown_s(ACT_FEED_MEAL), 0);
  CHECK_EQ((int)sim_action_cooldown_s(ACT_MEDICINE), 0);
}

// -----------------------------------------------------------------------------
// 12. The sleep window follows the sun, not a pair of fixed hours (P3-C2b)
//     Day 14 is mid-January (sunrise 08:35, sunset 18:00) and day 195 is
//     mid-July (07:00 / 21:40). Both are night at 03:00 and day at 10:00, and
//     the pebble has to agree with the table in both.
// -----------------------------------------------------------------------------
static PebbleInstance g_jan;
static PebbleInstance g_jul;

TEST(care_sleeps_at_three_in_the_morning_in_january_and_in_july) {
  SimEnv env;

  care_pet_at_day(g_jan, 3, 14);
  env = sim_env();
  care_minute(env);
  CHECK((sim_view()->flags & PF_ASLEEP) != 0);

  care_pet_at_day(g_jul, 3, 195);
  env = sim_env();
  care_minute(env);
  CHECK((sim_view()->flags & PF_ASLEEP) != 0);
}

TEST(care_is_awake_at_ten_in_the_morning_in_january_and_in_july) {
  SimEnv env;

  care_pet_at_day(g_jan, 10, 14);
  env = sim_env();
  for (uint32_t m = 0; m < 60u; ++m) care_minute(env);
  CHECK((sim_view()->flags & PF_ASLEEP) == 0);

  care_pet_at_day(g_jul, 10, 195);
  env = sim_env();
  for (uint32_t m = 0; m < 60u; ++m) care_minute(env);
  CHECK((sim_view()->flags & PF_ASLEEP) == 0);
}

// A January evening is night at 21:00 and a July evening is not: bedtime is
// 19:30 in January and 23:10 in July. This is the whole point of the table.
TEST(care_goes_to_bed_earlier_in_january_than_in_july) {
  SimEnv env;

  care_pet_at_day(g_jan, 21, 14);
  env = sim_env();
  care_minute(env);
  CHECK((sim_view()->flags & PF_ASLEEP) != 0);

  care_pet_at_day(g_jul, 21, 195);
  env = sim_env();
  care_minute(env);
  CHECK((sim_view()->flags & PF_ASLEEP) == 0);
}

// -----------------------------------------------------------------------------
// 13. Insistence wakes it, and only insistence (P3-C2b)
// -----------------------------------------------------------------------------
static PebbleInstance g_nudge;

// Puts the pebble to sleep at 23:00 in mid-April and returns the live SimEnv.
static SimEnv care_sleeping_pet(PebbleInstance& p) {
  care_pet_at(p, 23);
  SimEnv env = sim_env();
  care_minute(env);
  CHECK((sim_view()->flags & PF_ASLEEP) != 0);
  return env;
}

TEST(care_three_nudges_inside_the_window_wake_the_pet) {
  SimEnv env = care_sleeping_pet(g_nudge);
  ActionResult r;

  // The first two gestures do not act. They say "asleep" and cost nothing.
  CHECK(!sim_apply_action(ACT_PET, r));
  CHECK_EQ((int)r.err, (int)AERR_ASLEEP);
  CHECK((sim_view()->flags & PF_ASLEEP) != 0);

  CHECK(!sim_apply_action(ACT_PET, r));
  CHECK_EQ((int)r.err, (int)AERR_ASLEEP);
  CHECK((sim_view()->flags & PF_ASLEEP) != 0);

  // The third one wakes it AND lands: the player asked three times.
  CHECK(sim_apply_action(ACT_PET, r));
  CHECK_EQ((int)r.err, (int)AERR_NONE);
  CHECK((sim_view()->flags & PF_ASLEEP) == 0);
  CHECK((sim_take_events() & SIM_EV_WAKE) != 0u);

  // Awake, a care action that AERR_ASLEEP would have refused now works.
  sim_god_set_stat(ST_HUNGER, 10);
  for (uint32_t m = 0; m < 2u; ++m) care_minute(env);   // clear the global cooldown
  CHECK(sim_apply_action(ACT_FEED_MEAL, r));
  CHECK_EQ((int)r.err, (int)AERR_NONE);
}

static PebbleInstance g_nudge_slow;

TEST(care_three_nudges_spread_over_an_hour_do_not_wake_the_pet) {
  SimEnv env = care_sleeping_pet(g_nudge_slow);
  ActionResult r;

  for (uint8_t i = 0; i < 3u; ++i) {
    CHECK(!sim_apply_action(ACT_PET, r));
    CHECK_EQ((int)r.err, (int)AERR_ASLEEP);
    CHECK((sim_view()->flags & PF_ASLEEP) != 0);
    for (uint32_t m = 0; m < 20u; ++m) care_minute(env);   // 20 min apart
  }
  CHECK((sim_view()->flags & PF_ASLEEP) != 0);

  // The counter decayed, so the very next burst still needs its full three.
  CHECK(!sim_apply_action(ACT_PET, r));
  CHECK(!sim_apply_action(ACT_PET, r));
  CHECK((sim_view()->flags & PF_ASLEEP) != 0);
  CHECK(sim_apply_action(ACT_PET, r));
  CHECK((sim_view()->flags & PF_ASLEEP) == 0);
}

// Waking is free. Spec section 27 forbids punishing the player, and the energy
// that drains while awake is cost enough - CLEAN on a spotless pebble is the
// gesture that proves it, because the wake is then the ONLY thing that happened
// and the action itself is refused on its own merits, not on AERR_ASLEEP.
static PebbleInstance g_nudge_free;

TEST(care_waking_the_pet_costs_no_stat) {
  care_sleeping_pet(g_nudge_free);

  int32_t before[PB_CARE_COUNT];
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) before[i] = g_nudge_free.care[i];
  const int16_t cq_before = sim_view()->cq;

  ActionResult r;
  CHECK(!sim_apply_action(ACT_CLEAN, r));
  CHECK_EQ((int)r.err, (int)AERR_ASLEEP);
  CHECK(!sim_apply_action(ACT_CLEAN, r));
  CHECK_EQ((int)r.err, (int)AERR_ASLEEP);
  CHECK(!sim_apply_action(ACT_CLEAN, r));
  CHECK_EQ((int)r.err, (int)AERR_NOTHING_TODO);   // awake, and nothing to clean

  CHECK((sim_view()->flags & PF_ASLEEP) == 0);
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) {
    CHECK_EQ(g_nudge_free.care[i], before[i]);
  }
  CHECK_EQ((int)sim_view()->cq, (int)cq_before);
}

// -----------------------------------------------------------------------------
// 14. ...and it goes back to sleep when left alone again (P3-C2b)
// -----------------------------------------------------------------------------
static PebbleInstance g_relapse;

TEST(care_goes_back_to_sleep_after_the_relapse_period) {
  SimEnv env = care_sleeping_pet(g_relapse);
  ActionResult r;

  CHECK(!sim_apply_action(ACT_PET, r));
  CHECK(!sim_apply_action(ACT_PET, r));
  CHECK(sim_apply_action(ACT_PET, r));
  CHECK((sim_view()->flags & PF_ASLEEP) == 0);

  // Still awake a minute before the relapse period is up...
  for (uint32_t m = 0; m < (SLEEP_RELAPSE_S / 60u) - 1u; ++m) care_minute(env);
  CHECK((sim_view()->flags & PF_ASLEEP) == 0);

  // ...and back under the covers a minute after, with the night still young.
  for (uint32_t m = 0; m < 2u; ++m) care_minute(env);
  CHECK((sim_view()->flags & PF_ASLEEP) != 0);
  CHECK_EQ((int)sim_env().local_hour, 23);
}

// -----------------------------------------------------------------------------
// 15. THE REGRESSION DECISION D13 EXISTED FOR
//     A whole day with zero interaction: the pebble must spend the night
//     asleep, recharge while it is there, and cross the boundary exactly once
//     in each direction. Before P3-C2b it never slept at all (a fresh pebble
//     was handed over with its light ON), so energy pinned at 0 after 16.7 h
//     and health bled to the floor by about 64 h - the worst outcome in the
//     game, reached by doing nothing.
// -----------------------------------------------------------------------------
static PebbleInstance g_night;

TEST(care_a_full_night_of_neglect_is_spent_asleep_and_recharging) {
  care_pet_at(g_night, 10);          // 10:00 -> 10:00 the next morning
  sim_god_set_stat(ST_ENERGY, 30);
  SimEnv env = sim_env();

  uint16_t sleeps = 0, wakes = 0;
  uint16_t asleep_minutes = 0;
  (void)sim_take_events();

  for (uint32_t m = 0; m < 24u * 60u; ++m) {
    care_minute(env);
    const uint32_t ev = sim_take_events();
    if (ev & SIM_EV_SLEEP) sleeps++;
    if (ev & SIM_EV_WAKE)  wakes++;
    if (sim_view()->flags & PF_ASLEEP) asleep_minutes++;
  }

  // Once asleep, ONCE awake. The old rule woke the pebble the moment energy
  // filled and the window put it straight back, which flip-flopped every
  // substep for the rest of the night.
  CHECK_EQ((int)sleeps, 1);
  CHECK_EQ((int)wakes, 1);
  // Mid-April: bedtime about 22:25, sunrise about 07:20, so about 9 h of it.
  CHECK(asleep_minutes > 8u * 60u);
  CHECK(asleep_minutes < 11u * 60u);
  // And the night did its job: a pebble nobody touched wakes up rested.
  CHECK((sim_view()->flags & PF_ASLEEP) == 0);
  CHECK(sim_stat_pct(ST_ENERGY) > 60);
}
