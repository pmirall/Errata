// =============================================================================
//  Pebblebol host tests - test_care.cpp
//  Spec section 27, "Pebbles should be inconveniently unhappy at worst, not
//  permanently destroyed". P2-C7 removed death outright and gave HEALTH a
//  floor; this pins both, in the only way that matters - by playing the worst
//  possible player for a month and checking the pebble is still there.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "core/config.h"
#include "game/sim.h"
#include "game/genome.h"
#include "persistence/save_schema.h"

#define CARE_SEED    0x5EED0C7Au
#define CARE_EPOCH0  1700000000u
#define CARE_DAYS    30u

static PebbleInstance g_care;

// A hatched baby at 10:00 local with a valid clock, and nothing else.
static void care_pet(void) {
  genome_seed(CARE_SEED);
  sim_seed(CARE_SEED);
  memset(&g_care, 0, sizeof(g_care));
  sim_bind(g_care);
  sim_new_pet(genome_genesis(), CARE_EPOCH0, 0);
  sim_hatch();

  SimEnv env;
  sim_env_defaults(env);
  env.now_epoch   = CARE_EPOCH0;
  env.clock_valid = 1;
  env.local_hour  = 10;
  env.local_min   = 0;
  env.day_of_year = 100;
  sim_set_env(env);
}

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
