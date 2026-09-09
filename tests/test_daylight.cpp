// =============================================================================
//  Errata host tests - test_daylight.cpp
//  The approximated daylight table and its interpolation (plan P3-C2b,
//  decision D13). game/daylight.cpp is pure arithmetic over data/balance.h: no
//  clock, no state, no RNG, so every case here is a value check.
//
//  What is worth pinning is the SHAPE of the curve, not the table's minutes -
//  the minutes are a designer's approximation and are allowed to move. So:
//  the samples are reproduced exactly at their anchor days, the curve is
//  continuous every day of the year (a "one constant per month" table would
//  jump on the 1st), it wraps December into January, and the seasons come out
//  the right way round.
// =============================================================================
#include "nt_test.h"

#include "data/balance.h"
#include "game/daylight.h"

#define DL_DAYS   365u

// -----------------------------------------------------------------------------
//  1. The table is reproduced exactly where it is anchored
// -----------------------------------------------------------------------------
TEST(daylight_reproduces_every_month_sample_at_its_anchor_day) {
  for (uint8_t m = 0; m < (uint8_t)ER_DAYLIGHT_MONTHS; ++m) {
    const uint16_t d = DAYLIGHT_ANCHOR_DOY[m];
    CHECK_EQ(daylight_sunrise_min(d), DAYLIGHT_SUNRISE_MIN[m]);
    CHECK_EQ(daylight_sunset_min(d),  DAYLIGHT_SUNSET_MIN[m]);
  }
}

// -----------------------------------------------------------------------------
//  2. Continuity: no jump on the 1st of a month, or on any other day
//     The widest month-to-month step in the table is the daylight-saving one
//     (March -> April, +110 min of sunset over a 31 day segment), so no single
//     day may move either curve by more than a few minutes.
// -----------------------------------------------------------------------------
#define DL_MAX_DAILY_STEP  5

TEST(daylight_is_continuous_across_every_day_boundary) {
  for (uint16_t d = 0; d < DL_DAYS; ++d) {
    const uint16_t next = (uint16_t)((d + 1u) % DL_DAYS);

    const int32_t dr = (int32_t)daylight_sunrise_min(next)
                     - (int32_t)daylight_sunrise_min(d);
    const int32_t ds = (int32_t)daylight_sunset_min(next)
                     - (int32_t)daylight_sunset_min(d);

    CHECK(dr <= DL_MAX_DAILY_STEP && dr >= -DL_MAX_DAILY_STEP);
    CHECK(ds <= DL_MAX_DAILY_STEP && ds >= -DL_MAX_DAILY_STEP);
  }
}

// The wrap is the case an "index by month" table gets wrong: 31 December and
// 1 January are neighbours, and the December -> January segment must be walked
// from both ends.
TEST(daylight_wraps_december_into_january) {
  const int32_t dr = (int32_t)daylight_sunrise_min(0)
                   - (int32_t)daylight_sunrise_min((uint16_t)(DL_DAYS - 1u));
  const int32_t ds = (int32_t)daylight_sunset_min(0)
                   - (int32_t)daylight_sunset_min((uint16_t)(DL_DAYS - 1u));
  CHECK(dr <= DL_MAX_DAILY_STEP && dr >= -DL_MAX_DAILY_STEP);
  CHECK(ds <= DL_MAX_DAILY_STEP && ds >= -DL_MAX_DAILY_STEP);

  // Day 365 exists (a leap year's tail) and stays inside the same segment.
  CHECK(daylight_sunrise_min(365) > 0);
  CHECK(daylight_sunset_min(365) > daylight_sunrise_min(365));
}

// -----------------------------------------------------------------------------
//  3. The seasons are the right way round, and every day is a real day
// -----------------------------------------------------------------------------
TEST(daylight_summer_evenings_outlast_winter_ones) {
  const uint16_t jun = DAYLIGHT_ANCHOR_DOY[5];
  const uint16_t dec = DAYLIGHT_ANCHOR_DOY[11];

  CHECK(daylight_sunset_min(jun)  > daylight_sunset_min(dec));
  CHECK(daylight_sunrise_min(jun) < daylight_sunrise_min(dec));

  // Bedtime follows sunset by the same constant, so the June/December gap is
  // the table's own difference and nothing else.
  CHECK_EQ((int)(daylight_bedtime_min(jun) - daylight_bedtime_min(dec)),
           (int)(daylight_sunset_min(jun)  - daylight_sunset_min(dec)));
  CHECK_EQ((int)daylight_bedtime_min(jun),
           (int)daylight_sunset_min(jun) + SLEEP_AFTER_DUSK_MIN);
}

TEST(daylight_every_day_of_the_year_is_a_sane_day) {
  for (uint16_t d = 0; d <= 365u; ++d) {
    const uint16_t rise = daylight_sunrise_min(d);
    const uint16_t set  = daylight_sunset_min(d);
    const uint16_t bed  = daylight_bedtime_min(d);

    CHECK(rise < set);                            // the sun rises before it sets
    CHECK(set < ER_MINUTES_PER_DAY);
    CHECK(bed < ER_MINUTES_PER_DAY);              // bedtime never wraps midnight
    CHECK(bed > rise);                            // ...so the night is one range
    // Nothing pathological: the shortest day still has 8 h of light and the
    // longest still has a night.
    CHECK((int)(set - rise) >= 8 * 60);
    CHECK((int)(set - rise) <= 16 * 60);
  }
}

// -----------------------------------------------------------------------------
//  4. daylight_is_night() - the answer the simulation actually asks for
// -----------------------------------------------------------------------------
TEST(daylight_is_night_at_three_in_the_morning_in_january_and_in_july) {
  const uint16_t jan = DAYLIGHT_ANCHOR_DOY[0];
  const uint16_t jul = DAYLIGHT_ANCHOR_DOY[6];

  CHECK_EQ((int)daylight_is_night(jan, 3 * 60), 1);
  CHECK_EQ((int)daylight_is_night(jul, 3 * 60), 1);
  CHECK_EQ((int)daylight_is_night(jan, 10 * 60), 0);
  CHECK_EQ((int)daylight_is_night(jul, 10 * 60), 0);

  // 22:00 is bedtime in January (sunset 18:00 + 90 min) and not yet in July
  // (sunset 21:40 + 90 min = 23:10). That difference IS the mechanic.
  CHECK_EQ((int)daylight_is_night(jan, 22 * 60), 1);
  CHECK_EQ((int)daylight_is_night(jul, 22 * 60), 0);
}

TEST(daylight_is_night_agrees_with_the_window_it_is_built_from) {
  for (uint16_t d = 0; d <= 365u; d = (uint16_t)(d + 7u)) {
    const uint16_t rise = daylight_sunrise_min(d);
    const uint16_t bed  = daylight_bedtime_min(d);

    CHECK_EQ((int)daylight_is_night(d, bed), 1);                 // the first minute
    CHECK_EQ((int)daylight_is_night(d, (uint16_t)(bed - 1u)), 0);
    CHECK_EQ((int)daylight_is_night(d, (uint16_t)(rise - 1u)), 1);
    CHECK_EQ((int)daylight_is_night(d, rise), 0);                // the first minute of day
    CHECK_EQ((int)daylight_is_night(d, 0), 1);                   // midnight is always night

    // Minutes past a whole day fold rather than answering nonsense.
    CHECK_EQ((int)daylight_is_night(d, (uint16_t)(ER_MINUTES_PER_DAY + 0u)),
             (int)daylight_is_night(d, 0));
  }
}
