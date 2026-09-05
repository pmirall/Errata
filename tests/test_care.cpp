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
//     One hour handed over in one call, in sixty, in six or in three thousand
//     six hundred must land on the same bytes.
//
//     THE FIRST THREE CANNOT DISAGREE ON THIS HOUR. sim_tick() is nothing but
//     a chopper: it splits any dt into SIM_SUBSTEP_S pieces and holds no state
//     of its own, so 60x60 and 6x600 emit the identical stream of sixty
//     sub_step(60) calls. 1x3600 DOES hand sim_tick a chunk sixty times the
//     grid - but 10:00 -> 11:00 on a fresh hatchling contains no rate CHANGE,
//     and with every rate constant a single sub_step(3600) lands on the same
//     bytes as sixty sub_step(60), so this hour cannot see the chopper either.
//     As written the case was a tautology whose entire detection power was
//     "SIM_SUBSTEP_S divides 60" - and not even that; see the comment on the
//     60x60 case below.
//
//     THE 1 s CASE IS THE ONE THAT PROBES BELOW THE GRID - and it is the step
//     size the live device actually runs (app.cpp asks sim_step_seconds(),
//     which is 1 outside god mode). It found a real hole; see section 9b.
//
//     THE CHOPPER ITSELF IS TESTED IN 9c, which is the only place that hands
//     sim_tick a chunk above the grid over a span that HAS a rate change in
//     it - the one shape that can tell a chopped tick from an unchopped one.
// -----------------------------------------------------------------------------
static PebbleInstance g_chunk_a;
static PebbleInstance g_chunk_b;
static PebbleInstance g_chunk_fresh;

static void care_check_same(const PebbleInstance& a, const PebbleInstance& b) {
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) {
    CHECK_EQ(b.care[i], a.care[i]);
    CHECK_EQ((int)b.care_rem[i], (int)a.care_rem[i]);
  }
  CHECK_EQ(b.age_s, a.age_s);
  CHECK_EQ(b.last_updated_epoch, a.last_updated_epoch);
  CHECK_EQ((int)b.status, (int)a.status);
  // And nothing else moved either: the whole 128 B instance, byte for byte.
  CHECK(memcmp(&a, &b, sizeof(PebbleInstance)) == 0);
}

// Runs `total_s` from `hour` local on day 100 in fixed `chunk` handovers, with
// the wall clock FROZEN at `hour`: SimEnv is set once and never advanced. The
// pebble settles into whatever the start hour implies on its first sub-step and
// stays there, so NO sleep/wake edge can fall inside the span - which is what
// sections 9, 9b and 9c(a,b) want, because it isolates the poop and loneliness
// rate changes. care_run_chunked_clock() below is the one that moves the clock.
//
// `chunk` must divide `total_s`, or the loop overshoots and the case silently
// measures a longer span than the one it names.
static void care_run_chunked(PebbleInstance& p, uint8_t hour,
                             uint32_t total_s, uint32_t chunk) {
  CHECK_EQ((int)(total_s % chunk), 0);
  care_pet_at_day(p, hour, 100);
  for (uint32_t t = 0; t < total_s; t += chunk) sim_tick(chunk);
}

// The same run, but SimEnv follows simulated time the way app/app.cpp does, so
// the sleep window's edges - bedtime and sunrise, the model's LARGEST rate
// change (section 9c) - land inside the span. The frozen-clock helper above
// cannot reach them at any chunk size.
static void care_run_chunked_clock(PebbleInstance& p, uint8_t hour,
                                   uint32_t total_s, uint32_t chunk) {
  CHECK_EQ((int)(total_s % chunk), 0);
  care_pet_at_day(p, hour, 100);
  SimEnv env = sim_env();
  const uint32_t base_sod = (uint32_t)hour * 3600u;
  for (uint32_t t = 0; t < total_s; t += chunk) {
    sim_tick(chunk);
    uint32_t sod    = base_sod + t + chunk;
    env.now_epoch   = CARE_EPOCH0 + t + chunk;
    env.day_of_year = (uint16_t)((100u + sod / 86400u) % 366u);
    sod            %= 86400u;
    env.local_hour  = (uint8_t)(sod / 3600u);
    env.local_min   = (uint8_t)((sod % 3600u) / 60u);
    sim_set_env(env);
  }
}

TEST(care_one_hour_of_catch_up_is_the_same_however_it_is_chunked) {
  // Liveness FIRST. Every case here is an equality, and an equality holds just
  // as well when sim_tick() does nothing at all: with the tick stubbed out to a
  // no-op this case passed all of its checks. So pin that the hour happened.
  care_pet_at_day(g_chunk_fresh, 10, 100);
  const PebbleInstance fresh = g_chunk_fresh;

  care_run_chunked(g_chunk_a, 10, 3600u, 3600u);
  const PebbleInstance one_call = g_chunk_a;
  CHECK_EQ(one_call.age_s, 3600u);
  CHECK(one_call.care[CARE_HUNGER] < fresh.care[CARE_HUNGER]);

  // 60 x 60 s and 6 x 600 s: the same sub-step sequence. On their own these two
  // guard NOTHING - not even the "SIM_SUBSTEP_S still divides an hour" they
  // used to claim: with SIM_SUBSTEP_S set to 7u, which divides neither 60 nor
  // 3600, this whole case still passes and only 9c and the golden notice. They
  // are the plain statement of the contract every catch-up caller relies on.
  care_run_chunked(g_chunk_b, 10, 3600u, 60u);
  care_check_same(one_call, g_chunk_b);
  care_run_chunked(g_chunk_b, 10, 3600u, 600u);
  care_check_same(one_call, g_chunk_b);

  // 3600 x 1 s: below the grid. Exact equality is the right contract for THIS
  // hour because 10:00 -> 11:00 on a fresh hatchling contains no rate CHANGE -
  // no poop lands, the loneliness multiplier is six hours away, and the pebble
  // neither falls asleep nor wakes. Every integrator carries its remainder, so
  // with the rates held constant the chunk size cannot matter. Section 9c
  // states what happens once a rate change is in the span.
  care_run_chunked(g_chunk_b, 10, 3600u, 1u);
  care_check_same(one_call, g_chunk_b);
  CHECK_EQ((int)sim_view()->poop_count, 0);
  CHECK((sim_view()->flags & PF_ASLEEP) == 0);
}

// -----------------------------------------------------------------------------
//  9b. The poop clock advances at every step size, asleep included
//      WHAT THE 1 s CASE FOUND. poop_step() scaled its advance by MULT_SLEEP
//      (x0.35) and TRUNCATED it every sub-step with no carry: (1 * 350) / 1000
//      is 0, so at the 1 s step the device runs, a SLEEPING pebble never
//      advanced its timer at all and could not poop overnight, ever - while an
//      offline catch-up over the same night (60 s sub-steps, an exact 21)
//      produced two. Same night, same pebble, two different models, and the
//      whole "sleeping the pet before bed is a real strategy" line in
//      poop_step() was accidentally absolute on hardware.
//
//      poop_step() carries its remainder now, the way accum() always has.
//      Measured over eight hours from 23:00 before the fix: 2 poops at every
//      step of 10 s or more, 1 at 5 s, 0 at 2 s and 0 at 1 s.
// -----------------------------------------------------------------------------
static PebbleInstance g_poop_chunk;

TEST(care_the_poop_clock_advances_at_every_step_size) {
  // Every divisor of the sub-step, plus two chunk sizes above it.
  static const uint32_t STEPS[] = { 3600u, 600u, 60u, 30u, 20u, 10u, 5u, 2u, 1u };

  care_run_chunked(g_poop_chunk, 23, 8u * 3600u, 60u);
  const uint8_t ref = sim_view()->poop_count;
  // The span has to be a real night, or this case passes for the wrong reason.
  CHECK((sim_view()->flags & PF_ASLEEP) != 0);
  CHECK(ref > 0);

  for (unsigned i = 0; i < sizeof(STEPS) / sizeof(STEPS[0]); ++i) {
    care_run_chunked(g_poop_chunk, 23, 8u * 3600u, STEPS[i]);
    CHECK((sim_view()->flags & PF_ASLEEP) != 0);
    CHECK_EQ((int)sim_view()->poop_count, (int)ref);
  }
}

// -----------------------------------------------------------------------------
//  9c. Past the first rate change, the bound - not equality
//      A rate CHANGE is evaluated on the sub-step grid, so a finer step charges
//      the new rate up to one sub-step early. That is a bounded, one-off offset
//      per change, not a drift, and it is not a defect to be fixed: the grid is
//      what makes the cadences (60 s stage check, 600 s sickness roll) land in
//      the same places whatever the caller hands over.
//
//      The budget per change is one sub-step at that change's size. THREE rate
//      changes exist in the care model, not the two this comment used to name:
//        - a poop arriving, which adds CARE_HYGIENE_POOP_MPH (1,000 milli/h)
//          to the cleanliness rate;
//        - the loneliness multiplier turning on at LONELY_AFTER_S, which adds
//          (MULT_LONELY - MULT_ONE)/1000 = x0.5 of the happiness base
//          (1,500 milli/h). One sub-step of that, the larger of the two, is
//          CARE_GRID_EVENT_MILLI = 25 milli;
//        - the sleep/wake flip, which is far the largest and was missed: energy
//          goes from CARE_DECAY_MPH[CARE_ENERGY] (-6,000 milli/h) to
//          CARE_ENERGY_ASLEEP_MPH (+20,000), a 26,000 milli/h swing. One
//          sub-step of it is CARE_GRID_SLEEP_MILLI = 433 milli, 17x the other
//          two. The frozen-clock helper cannot see it - it never advances the
//          wall clock, so no sleep/wake edge can occur inside any span it runs
//          - which is why case (c) below drives the clock as app.cpp does.
//
//      THIS IS ALSO THE ONLY PLACE THE CHOPPER ITSELF IS TESTED. Cases (a2),
//      (b2) and (c) hand sim_tick a chunk ABOVE SIM_SUBSTEP_S over a span that
//      HAS a rate change in it, which is the only shape that can tell a chopped
//      tick from an unchopped one. Replacing sim_tick()'s loop with a single
//      sub_step(seconds) leaves sections 9 and 9b, the golden and the other 20
//      binaries green, and moves these three gaps from 25 / 11 / 126 milli to
//      1,858 / 3,594 / 3,655. It is reachable on the device: GOD_SCALE_4 is
//      3600 and app.cpp ticks sim_tick(sim_step_seconds()), so god mode hands
//      sim_tick a full hour in one call.
//
//      Measured: 9 milli over two awake hours (1 poop), 25 over four (3 poops),
//      11 and 9 over an eight-hour night (2 poops at x0.35, plus the loneliness
//      edge), 43 across bedtime and 126 across sunrise with the clock moving.
//      None of them approaches a displayed percent, which is 1,000 milli.
// -----------------------------------------------------------------------------
#define CARE_GRID_EVENT_MILLI  (((int32_t)MULT_LONELY - (int32_t)MULT_ONE) \
                                * -(int32_t)CARE_DECAY_MPH[CARE_HAPPINESS] \
                                / 1000 * (int32_t)SIM_SUBSTEP_S / 3600)

// One sub-step of the sleep/wake energy flip: the model's largest rate change.
#define CARE_GRID_SLEEP_MILLI  (((int32_t)CARE_ENERGY_ASLEEP_MPH \
                                 - (int32_t)CARE_DECAY_MPH[CARE_ENERGY]) \
                                * (int32_t)SIM_SUBSTEP_S / 3600)

static PebbleInstance g_grid_a;
static PebbleInstance g_grid_b;

// The worst per-stat distance between the two runs just performed.
static int32_t care_grid_worst(void) {
  int32_t worst = 0;
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) {
    int32_t d = g_grid_b.care[i] - g_grid_a.care[i];
    if (d < 0) d = -d;
    if (d > worst) worst = d;
  }
  return worst;
}

// |dt=1 minus dt=`chunk`| on the worst stat, over `total_s` from `hour`, clock
// frozen. `chunk` is the COARSE side: passing one ABOVE SIM_SUBSTEP_S is what
// puts sim_tick()'s chopper under test.
static int32_t care_grid_gap_chunk(uint8_t hour, uint32_t total_s,
                                   uint32_t chunk, uint8_t& poops_out) {
  care_run_chunked(g_grid_a, hour, total_s, chunk);
  poops_out = sim_view()->poop_count;
  care_run_chunked(g_grid_b, hour, total_s, 1u);
  CHECK_EQ((int)sim_view()->poop_count, (int)poops_out);
  return care_grid_worst();
}

static int32_t care_grid_gap(uint8_t hour, uint32_t total_s, uint8_t& poops_out) {
  return care_grid_gap_chunk(hour, total_s, (uint32_t)SIM_SUBSTEP_S, poops_out);
}

// The same, with the wall clock moving, so a sleep/wake edge is in the span.
static int32_t care_grid_gap_clock(uint8_t hour, uint32_t total_s,
                                   uint32_t chunk, uint8_t& poops_out) {
  care_run_chunked_clock(g_grid_a, hour, total_s, chunk);
  poops_out = sim_view()->poop_count;
  care_run_chunked_clock(g_grid_b, hour, total_s, 1u);
  CHECK_EQ((int)sim_view()->poop_count, (int)poops_out);
  return care_grid_worst();
}

TEST(care_a_finer_step_moves_a_rate_change_by_less_than_one_substep) {
  CHECK_EQ((int)CARE_GRID_EVENT_MILLI, 25);
  CHECK_EQ((int)CARE_GRID_SLEEP_MILLI, 433);

  // (a) Four awake hours from 10:00: three poops, no loneliness edge yet.
  uint8_t poops = 0;
  int32_t gap = care_grid_gap(10, 4u * 3600u, poops);
  CHECK_EQ((int)poops, 3);
  CHECK(gap <= (int32_t)poops * CARE_GRID_EVENT_MILLI);
  // And it is not zero, or the bound is being asserted against nothing.
  CHECK(gap > 0);

  // (a2) The same four hours handed over an HOUR at a time - sixty times the
  // sub-step, and the size god mode hands over at GOD_SCALE_4. The bound is the
  // same one: the chunk size may not buy the caller a different simulation.
  uint8_t poops_coarse = 0;
  int32_t gap_coarse = care_grid_gap_chunk(10, 4u * 3600u, 3600u, poops_coarse);
  CHECK_EQ((int)poops_coarse, (int)poops);
  CHECK(gap_coarse <= (int32_t)poops * CARE_GRID_EVENT_MILLI);

  // (b) Eight asleep hours from 23:00: two poops plus the loneliness edge at 6 h.
  gap = care_grid_gap(23, 8u * 3600u, poops);
  CHECK_EQ((int)poops, 2);
  CHECK(gap <= ((int32_t)poops + 1) * CARE_GRID_EVENT_MILLI);
  CHECK(gap > 0);

  // Nowhere near a displayed percent, which is what the bound has to mean.
  CHECK(gap < 1000);

  // (b2) The whole night in ONE call: 28,800 s, 480 times the sub-step, the
  // shape an offline catch-up takes. Same bound again.
  gap_coarse = care_grid_gap_chunk(23, 8u * 3600u, 8u * 3600u, poops_coarse);
  CHECK_EQ((int)poops_coarse, (int)poops);
  CHECK(gap_coarse <= ((int32_t)poops + 1) * CARE_GRID_EVENT_MILLI);
  CHECK(gap_coarse < 1000);

  // (c) With the clock moving, the sleep/wake flip is inside the span and the
  // budget is CARE_GRID_SLEEP_MILLI, not CARE_GRID_EVENT_MILLI. An hour at a
  // time, so this also runs above the grid.
  uint8_t poops_c = 0;
  gap = care_grid_gap_clock(21, 4u * 3600u, 3600u, poops_c);   // through bedtime
  CHECK((sim_view()->flags & PF_ASLEEP) != 0);          // ends asleep ...
  care_run_chunked_clock(g_grid_b, 21, 60u, 60u);
  CHECK((sim_view()->flags & PF_ASLEEP) == 0);          // ... and began awake,
  // so the flip really is inside the span and not off one end of it.
  CHECK(gap <= CARE_GRID_SLEEP_MILLI + (int32_t)poops_c * CARE_GRID_EVENT_MILLI);
  // Larger than the poop/loneliness budget: this IS the third rate change, and
  // the old "one sub-step of the largest is 25 milli" was simply wrong.
  CHECK(gap > CARE_GRID_EVENT_MILLI);
  CHECK(gap < 1000);

  gap = care_grid_gap_clock(5, 4u * 3600u, 3600u, poops_c);    // through sunrise
  CHECK((sim_view()->flags & PF_ASLEEP) == 0);          // ends awake ...
  care_run_chunked_clock(g_grid_b, 5, 60u, 60u);
  CHECK((sim_view()->flags & PF_ASLEEP) != 0);          // ... and began asleep.
  CHECK(gap <= CARE_GRID_SLEEP_MILLI + (int32_t)poops_c * CARE_GRID_EVENT_MILLI);
  CHECK(gap > CARE_GRID_EVENT_MILLI);
  CHECK(gap < 1000);
}

// -----------------------------------------------------------------------------
//  9d. The stage check keeps its cadence at any chunk size
//      The OTHER carry the poop fix pulled in: stage_step() used to reset
//      g.acc_stage to 0 instead of subtracting STAGE_CHECK_PERIOD_S, which
//      throws away whatever the sub-step overshot the period by. That is
//      invisible whenever dt divides 60 - which is every step the device takes
//      outside an offline catch-up - so nothing in the suite noticed it, and it
//      shipped in the exit commit unchecked. This is the shape that sees it: a
//      handover that is NOT a multiple of the period, so every period ends
//      part-way through a sub-step.
//
//      With the carry, the k-th check happens as soon as elapsed simulated time
//      reaches 60k, at the sub-step boundary that reaches it; discarding the
//      remainder stretches the cadence to the caller's whole chunk. Measured at
//      the baby -> child transition (AGE_CHILD_S, 13,500 s, an exact multiple of
//      both 60 and the chunks below): on time to the second with the carry, 90 s
//      and 100 s late with `= 0`.
// -----------------------------------------------------------------------------
static PebbleInstance g_stage_chunk;

// Sim time at which the pebble is first seen past STAGE_BABY, driven `chunk` at
// a time. 0 if it never happens inside `limit_s`.
static uint32_t care_child_seen_at(uint32_t chunk, uint32_t limit_s) {
  care_pet_at_day(g_stage_chunk, 10, 100);
  CHECK_EQ((int)sim_view()->stage, (int)STAGE_BABY);
  for (uint32_t t = chunk; t <= limit_s; t += chunk) {
    sim_tick(chunk);
    if (sim_view()->stage > (uint8_t)STAGE_BABY) return t;
  }
  return 0;
}

TEST(care_the_stage_check_keeps_its_cadence_at_any_chunk_size) {
  const uint32_t due = (uint32_t)AGE_CHILD_S;
  CHECK_EQ(due % (uint32_t)STAGE_CHECK_PERIOD_S, 0u);

  // The control: 60 s handovers, where the accumulator lands exactly on the
  // period and the carry cannot matter.
  CHECK_EQ(care_child_seen_at(60u, due + 3600u), due);

  // 90 s and 100 s: neither is a multiple of STAGE_CHECK_PERIOD_S. The check
  // must still land on the 60 s grid of elapsed time, not on the caller's.
  CHECK_EQ(care_child_seen_at(90u, due + 3600u), due);
  CHECK_EQ(care_child_seen_at(100u, due + 3600u), due);
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

// -----------------------------------------------------------------------------
// 16. THE PHASE-3 EXIT SOAK (plan P3-C5)
//     The criterion the plan carried in was "no stat pinned at 0 for more than
//     6 simulated hours of neglect". Before P3-C2b closed D13 that was
//     unreachable: a pebble whose light nobody switched off never slept and
//     energy pinned at 0 for ever. It is reachable now - but only for the one
//     stat the simulation refills BY ITSELF. Restated and measured:
//
//       Fourteen simulated days of total neglect - a hatched pebble, a
//       trustworthy clock, and not one action for a fortnight.
//
//       ENERGY, the one core stat the simulation restores on its own, is never
//       at 0 for more than 6 continuous simulated hours, and is back above 90 %
//       at every sunrise.
//
//       HUNGER, HAPPINESS and CLEANLINESS are at 0 for most of the fortnight,
//       and are MEANT to be: they are the ones only the player can refill, and
//       refilling them is what the player is for. They are asserted here as
//       design, so that "no stat is pinned" cannot quietly come back.
//
//       HEALTH is what the floor protects. It never reaches 0 at all and never
//       falls below HEALTH_FLOOR_PCT.
//
//     Measured on this fixture (seed 0x5EED0C7A, 10:00, day 100, 336 h), as the
//     longest CONTINUOUS run each stat spends at 0:
//
//       hunger      first 0 at  30.8 h   longest run 305.2 h   ends   0 %
//       happiness   first 0 at  27.5 h   longest run 160.2 h   ends  40 %
//       energy      first 0 at  34.7 h   longest run   2.1 h   ends  79 %
//       cleanliness first 0 at  24.7 h   longest run 311.4 h   ends   0 %
//       health      never 0                longest run   0.0 h   ends  10 %
//
//     14 wake-ups, all of them above 90 % energy (99 % every time). Robustness,
//     measured outside the suite over 40 genesis genomes x 12 month anchors
//     (480 fortnights): energy's longest run at 0 is at most 3.87 h and never
//     exceeds 6 h in any run; health reaches 0 in 0 of 480. Forcing the
//     metabolism gene to its maximum 15 (x1.50), which genesis cannot roll,
//     over all 366 start days: 5.40 h, still under 6.
//
//     SCOPED TO A VALID CLOCK ON PURPOSE. Without one there is no night -
//     is_night() refuses to answer - the pebble never sleeps, and energy pins
//     at 0 for 322.78 h of the same 336. That is D13's known cost, not a defect
//     in this model, and section 13 of docs/decisions.md records it.
// -----------------------------------------------------------------------------
#define SOAK_DAYS            14u
#define SOAK_ENERGY_MAX_S    (6u * 3600u)     // the criterion
#define SOAK_PLAYER_MIN_S    (24u * 3600u)    // what the player is for

static PebbleInstance g_soak;

TEST(care_a_fortnight_of_neglect_only_pins_the_stats_the_player_owns) {
  care_pet_at(g_soak, 10);
  SimEnv env = sim_env();
  (void)sim_take_events();

  uint32_t run_s[ST_COUNT];
  uint32_t max_run_s[ST_COUNT];
  for (uint8_t i = 0; i < (uint8_t)ST_COUNT; ++i) { run_s[i] = 0; max_run_s[i] = 0; }

  uint32_t wakes = 0;
  uint8_t  worst_wake_pct = 100;

  for (uint32_t m = 0; m < SOAK_DAYS * 24u * 60u; ++m) {
    care_minute(env);

    if (sim_take_events() & SIM_EV_WAKE) {
      wakes++;
      const uint8_t pct = sim_stat_pct(ST_ENERGY);
      if (pct < worst_wake_pct) worst_wake_pct = pct;
    }

    // The floor holds on every one of the 20,160 minutes, not just at the end.
    CHECK(sim_stat_pct(ST_HEALTH) >= HEALTH_FLOOR_PCT);

    for (uint8_t i = 0; i < (uint8_t)ST_COUNT; ++i) {
      if (sim_stat_milli((StatId)i) <= 0) {
        run_s[i] += 60u;
        if (run_s[i] > max_run_s[i]) max_run_s[i] = run_s[i];
      } else {
        run_s[i] = 0;
      }
    }
  }

  // --- THE CRITERION ---------------------------------------------------------
  CHECK(max_run_s[ST_ENERGY] < SOAK_ENERGY_MAX_S);
  // It is not a vacuous bound: energy really does empty, it just does not stay
  // empty. A model that never let energy reach 0 at all would be a different
  // (and wrong) thing, so the run has to be non-zero too.
  CHECK(max_run_s[ST_ENERGY] > 0);
  CHECK_EQ((int)wakes, (int)SOAK_DAYS);
  CHECK(worst_wake_pct > 90);

  // --- HEALTH: what the floor protects ---------------------------------------
  CHECK_EQ((int)max_run_s[ST_HEALTH], 0);
  CHECK(sim_stat_pct(ST_HEALTH) >= HEALTH_FLOOR_PCT);

  // --- THE THREE THE PLAYER OWNS: pinned, on purpose -------------------------
  CHECK(max_run_s[ST_HUNGER]    > SOAK_PLAYER_MIN_S);
  CHECK(max_run_s[ST_HAPPINESS] > SOAK_PLAYER_MIN_S);
  CHECK(max_run_s[ST_HYGIENE]   > SOAK_PLAYER_MIN_S);
  CHECK_EQ(sim_stat_pct(ST_HUNGER), 0);
  CHECK_EQ(sim_stat_pct(ST_HYGIENE), 0);

  // Happiness is the one exception among the three, and it is not the player:
  // events_step() pays EVENT_VISITA_HAPPINESS at 72 h of age and
  // EVENT_BIRTHDAY_HAPPY every 168 h, which is why its longest run is about
  // 160 h rather than 300 and why it does not finish at 0.
  CHECK(sim_stat_pct(ST_HAPPINESS) > 0);
  CHECK(max_run_s[ST_HAPPINESS] < max_run_s[ST_HUNGER]);

  // And after a fortnight of nothing, it is still here.
  CHECK(sim_view()->stage < STAGE_COUNT);
  CHECK(sim_stat_milli(ST_HEALTH) > 0);
}

// -----------------------------------------------------------------------------
//  P6-C3: RECOVERY ACCRUES ACROSS A SLEEP
//
//  A sleep gap is not a special kind of time. The whole of the power ladder's
//  correctness, once elapsed time is right (tests/test_clock.cpp), is that the
//  gap reaches the same two integrators a boot gap reaches: sim_catch_up_ex()
//  for the active pebble and box_recover() for the stored ones. These cases pin
//  that at the plan's own bench number - "a 30 min deep sleep charges 30 min of
//  care and box recovery" - so the acceptance can be read off a host run
//  instead of a multimeter.
// -----------------------------------------------------------------------------
static GameState g_sleep_state;

// NARROWED IN P6-C4, and the old name is kept here because the narrowing is the
// finding: this case was called care_a_thirty_minute_sleep_charges_thirty_minutes
// _of_care, which claimed a property of the SLEEP PATH. It never touched that
// path - it calls sim_tick(n) with its own n and never sim_step_seconds(), never
// logic_tick()'s `owed`, never the millisecond scheduler - so it passed happily
// on a build whose step size was zero and whose sleep therefore charged nothing
// (see care_a_freshly_bound_sim_advances_one_real_second_per_tick, below). What
// it really guards is worth having under its own name: the care INTEGRATOR is
// chunk-invariant, so slicing a gap cannot change what the gap costs.
TEST(care_thirty_minutes_of_care_is_the_same_however_a_sleep_slices_it) {
  // WHAT THE LADDER ACTUALLY DOES, and it is why this case is about sim_tick()
  // and not about sim_catch_up_ex(): a light sleep is not a reboot, so the loop
  // comes back and settles up through the ordinary 1 Hz tick with the whole
  // slice as its step (app.cpp, logic_tick(owed)). PWR_SLEEP_SLICE_MS is 8 s,
  // so half an hour of sleeping is 225 of those.
  static int32_t awake[PB_CARE_COUNT];
  care_pet_at(g_care, 10);
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) {
    g_care.care[i]     = (int32_t)PB_CARE_MILLI_MAX / 2;
    g_care.care_rem[i] = 0;
  }
  for (uint16_t sec = 0; sec < 1800u; ++sec) sim_tick(1);
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) awake[i] = g_care.care[i];

  // The same half hour, spent asleep in eight-second slices.
  care_pet_at(g_care, 10);
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) {
    g_care.care[i]     = (int32_t)PB_CARE_MILLI_MAX / 2;
    g_care.care_rem[i] = 0;
  }
  const uint32_t slice_s = (uint32_t)PWR_SLEEP_SLICE_MS / 1000u;
  for (uint32_t t = 0; t < 1800u; t += slice_s) sim_tick(slice_s);

  bool moved = false;
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) {
    CHECK_EQ(g_care.care[i], awake[i]);                        // to the byte
    if (g_care.care[i] != (int32_t)PB_CARE_MILLI_MAX / 2) moved = true;
  }
  // Not every bar decays in half an hour (health regenerates while the others
  // are still above their thresholds), so the claim is that the half hour was
  // CHARGED, not that all five bars fell.
  CHECK(moved);

  // And in one call, which is what a stall-shaped catch-up would hand over.
  care_pet_at(g_care, 10);
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) {
    g_care.care[i]     = (int32_t)PB_CARE_MILLI_MAX / 2;
    g_care.care_rem[i] = 0;
  }
  sim_tick(1800);
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) CHECK_EQ(g_care.care[i], awake[i]);
}

TEST(care_a_sleep_the_scheduler_resynchronised_away_would_charge_one_second) {
  // THE DEFECT THE TICK BOUND EXISTS TO STOP, stated as a measurement. Before
  // P6-C3 app_loop() advanced its cursor by exactly 1000 ms per pass and
  // resynchronised past anything longer than four seconds, so an eight-second
  // sleep slice would have reached the pet as ONE second. This is what that
  // looks like: thirty minutes of sleeping charged as 225 seconds.
  static int32_t slept[PB_CARE_COUNT];
  care_pet_at(g_care, 10);
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) {
    g_care.care[i]     = (int32_t)PB_CARE_MILLI_MAX / 2;
    g_care.care_rem[i] = 0;
  }
  for (uint32_t t = 0; t < 1800u; t += 8u) sim_tick(8);
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) slept[i] = g_care.care[i];

  care_pet_at(g_care, 10);
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) {
    g_care.care[i]     = (int32_t)PB_CARE_MILLI_MAX / 2;
    g_care.care_rem[i] = 0;
  }
  for (uint32_t t = 0; t < 1800u; t += 8u) sim_tick(1);        // one per wake
  bool any_lost = false;
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) {
    if (g_care.care[i] > slept[i]) any_lost = true;            // decayed less
  }
  CHECK(any_lost);
}

TEST(care_a_sleep_charged_as_an_unknown_clock_would_move_nothing_at_all) {
  // The counter-case for the OTHER route a sleep gap can take - a deep-sleep
  // wake, which is a reboot and therefore goes through boot_absence() and
  // sim_catch_up_ex(). Hand the gap over with clock_known = 0, which is what an
  // absence the device cannot vouch for gets, and the pet is charged NOTHING. A
  // sleep wrongly classed as a crash or a soft reset looks exactly like this,
  // and it is silent. tests/test_clock.cpp holds the list that decides it.
  care_pet_at(g_care, 10);
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) {
    g_care.care[i]     = (int32_t)PB_CARE_MILLI_MAX / 2;
    g_care.care_rem[i] = 0;
  }
  SimEnv env;
  sim_env_defaults(env);
  env.now_epoch   = CARE_EPOCH0 + 1800u;
  env.clock_valid = 1;
  env.local_hour  = 10;
  env.local_min   = 30;
  env.day_of_year = 100;
  sim_set_env(env);

  AbsenceReport rep;
  sim_catch_up_ex(1800u, 0 /* no trustworthy clock */, rep);
  CHECK_EQ(rep.clock_known, 0);
  CHECK_EQ(rep.absence_s, 0u);
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) {
    CHECK_EQ(g_care.care[i], (int32_t)PB_CARE_MILLI_MAX / 2);
  }

  // And with the clock known - which is what BOOT_DEEPSLEEP gets - the same
  // gap is integrated in full.
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) {
    g_care.care[i]     = (int32_t)PB_CARE_MILLI_MAX / 2;
    g_care.care_rem[i] = 0;
  }
  sim_set_env(env);
  sim_catch_up_ex(1800u, 1, rep);
  CHECK_EQ(rep.clock_known, 1);
  CHECK_EQ(rep.absence_s, 1800u);
  bool moved = false;
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) {
    if (g_care.care[i] != (int32_t)PB_CARE_MILLI_MAX / 2) moved = true;
  }
  CHECK(moved);
}

TEST(care_a_stored_pebble_recovers_across_a_sleep_exactly_as_it_does_across_a_boot) {
  // box_recover() is the OTHER half of the plan's bench line ("a 30 min sleep
  // charges 30 min of care AND box recovery"). Stored pebbles are not
  // simulated, they only refill - so the property to pin is that the sleep gap
  // reaches them at all, and that thirty minutes of it is thirty minutes' worth
  // and not a day's.
  memset(&g_sleep_state, 0, sizeof g_sleep_state);
  for (uint8_t i = 0; i < (uint8_t)BOX_SLOTS; ++i) {
    g_sleep_state.pebbles[i].magic      = (uint16_t)PEBBLE_MAGIC;
    g_sleep_state.pebbles[i].layout_ver = (uint8_t)PEBBLE_LAYOUT_VER;
  }
  g_sleep_state.box.magic           = (uint16_t)BOX_MAGIC;
  g_sleep_state.box.active_slot     = (uint8_t)BOX_ACTIVE_NONE;
  g_sleep_state.box.next_id_counter = 1;
  g_sleep_state.cfg.device_id       = 0xB0FFE503u;
  box_bind(g_sleep_state);

  Genome gz;
  memset(&gz, 0, sizeof gz);
  // TWO pebbles, and the first is made active: box_recover() refuses the active
  // slot outright, because the sim owns that one.
  const uint8_t held   = box_new_pebble(SPECIES_ID_STARTER, 1, (uint8_t)ORIGIN_STARTER,
                                        gz, 0x1003u, CARE_EPOCH0);
  const uint8_t stored = box_new_pebble(SPECIES_ID_STARTER, 1, (uint8_t)ORIGIN_WILD,
                                        gz, 0x1004u, CARE_EPOCH0);
  CHECK(held   != (uint8_t)BOX_SLOT_NONE);
  CHECK(stored != (uint8_t)BOX_SLOT_NONE);
  CHECK(box_set_active(held));
  PebbleInstance* p = box_slot(stored);
  CHECK(p != nullptr);

  static int32_t half_hour[PB_CARE_COUNT];
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) { p->care[i] = 0; p->care_rem[i] = 0; }
  box_recover(stored, 1800u);
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) {
    half_hour[i] = p->care[i];
    CHECK(half_hour[i] > 0);                                  // the gap arrived
    CHECK(half_hour[i] < (int32_t)PB_CARE_MILLI_MAX);         // and only that much
  }

  // Three ten-minute slices - the shape the ladder really produces - land on
  // the same bytes as one thirty-minute gap. That is what care_rem is for, and
  // it is the property a sliced sleep needs.
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) { p->care[i] = 0; p->care_rem[i] = 0; }
  box_recover(stored, 600u);
  box_recover(stored, 600u);
  box_recover(stored, 600u);
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) CHECK_EQ(p->care[i], half_hour[i]);

  // A sleep of zero seconds - a wake on the very millisecond it slept - charges
  // nothing and cannot overshoot.
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) { p->care[i] = 0; p->care_rem[i] = 0; }
  box_recover(stored, 0u);
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) CHECK_EQ(p->care[i], 0);
}

// -----------------------------------------------------------------------------
//  P6-C4: THE STEP SIZE THE SHIPPING BUILD ACTUALLY USES
//
//  app.cpp's logic_tick() does not tick the world by a constant. It ticks it by
//  `sim_step_seconds() * owed`, and sim_step_seconds() returns a static that,
//  until P6-C4, was written by exactly one function - sim_set_time_scale() -
//  whose only two callers live inside dev/godmode.cpp's `#if GOD_MODE_ENABLED`
//  half. The RELEASE artefact is built with GOD_MODE_ENABLED=0, so on the build
//  that actually gets flashed the scale stayed at its zero-initialised value and
//  the whole simulation stood still: sim_tick(0) every second, no care decay, no
//  ageing, no XP carry drip, no activity minute, for as long as the device was
//  switched on.
//
//  NOTHING IN THIS SUITE COULD SEE IT, and that is the lesson rather than an
//  aside: every other case in this file calls sim_tick(n) with its own n, so the
//  one number the firmware actually multiplies by was never on the line. The
//  proof is a measurement - `uint32_t sim_step_seconds(void){ return 0; }` was
//  planted in game/sim.cpp and the whole suite still reported ALL PASS 41/41.
// -----------------------------------------------------------------------------
TEST(care_a_freshly_bound_sim_advances_one_real_second_per_tick) {
  // THE FIRST ASSERTION IS THE WHOLE CASE, and it has to stay the first thing
  // this binary asks about the scale: no case above calls sim_set_time_scale(),
  // so what sim_step_seconds() answers here is the value a release boot gets.
  care_pet_at(g_care, 10);
  CHECK_EQ(sim_step_seconds(), 1u);

  // And the value is not just non-zero, it is ONE REAL SECOND: app.cpp's own
  // expression over a half-hour sleep must land on the same bytes as the half
  // hour this file already pins one second at a time.
  static int32_t by_the_second[PB_CARE_COUNT];
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) {
    g_care.care[i]     = (int32_t)PB_CARE_MILLI_MAX / 2;
    g_care.care_rem[i] = 0;
  }
  for (uint16_t s = 0; s < 1800u; ++s) sim_tick(1);
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) by_the_second[i] = g_care.care[i];

  care_pet_at(g_care, 10);
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) {
    g_care.care[i]     = (int32_t)PB_CARE_MILLI_MAX / 2;
    g_care.care_rem[i] = 0;
  }
  const uint32_t owed = (uint32_t)PWR_SLEEP_SLICE_MS / 1000u;   // one sleep slice
  bool moved = false;
  for (uint32_t t = 0; t < 1800u; t += owed) {
    sim_tick(sim_step_seconds() * owed);                        // app.cpp, verbatim
  }
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) {
    CHECK_EQ(g_care.care[i], by_the_second[i]);
    if (g_care.care[i] != (int32_t)PB_CARE_MILLI_MAX / 2) moved = true;
  }
  CHECK(moved);                                   // the half hour was CHARGED

  // God mode is an OVERRIDE of that 1, and a zero is not a speed: it is a stop,
  // so the setter clamps it. Binding another Pebble must not cancel an
  // acceleration the player is watching either - the default is set only when
  // the scale is still unset.
  sim_set_time_scale(60u);
  CHECK_EQ(sim_step_seconds(), 60u);
  care_pet_at(g_care, 10);
  CHECK_EQ(sim_step_seconds(), 60u);
  sim_set_time_scale(0u);
  CHECK_EQ(sim_step_seconds(), 1u);
}
