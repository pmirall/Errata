// =============================================================================
//  ERRATA - game/daylight.cpp
//  The interpolation behind daylight.h. Integer only, no allocation, no clock
//  of its own: every answer is a pure function of (day_of_year, minute).
// =============================================================================
#include "daylight.h"

#include "../data/balance.h"

// The year the interpolation is laid out on. day_of_year is 0..365, and day
// 365 (29 February's leap-year tail) simply lands one day further into the
// December -> January segment, which is 31 days long. Nothing needs to know
// whether the year is a leap year.
#define DL_YEAR_DAYS   365u

static inline uint16_t fold_day(uint16_t d)
{
  return (uint16_t)(d % DL_YEAR_DAYS);
}

// Linear interpolation between two month samples, rounded to the nearest
// minute in BOTH directions (the sign of b - a changes across the year, and a
// truncating divide would bias sunrise and sunset in opposite ways).
static uint16_t lerp_min(uint16_t a, uint16_t b, uint16_t pos, uint16_t span)
{
  if (span == 0u) return a;
  const int32_t num = (int32_t)((int32_t)b - (int32_t)a) * (int32_t)pos;
  const int32_t half = (int32_t)span / 2;
  const int32_t step = (num >= 0) ? ((num + half) / (int32_t)span)
                                  : -(((-num) + half) / (int32_t)span);
  int32_t v = (int32_t)a + step;
  if (v < 0) v = 0;
  if (v > (int32_t)(ER_MINUTES_PER_DAY - 1u)) v = (int32_t)(ER_MINUTES_PER_DAY - 1u);
  return (uint16_t)v;
}

// Finds the segment `d` falls in and interpolates `table` across it. The
// samples sit at the MIDDLE of each month (DAYLIGHT_ANCHOR_DOY), so the
// segment before January's anchor and the one after December's are the same
// wrapped segment, and the curve has no discontinuity anywhere - in
// particular none on the 1st of a month, which is where a
// "one constant per month" table would jump by half an hour.
static uint16_t sample(const uint16_t* table, uint16_t day_of_year)
{
  const uint16_t d = fold_day(day_of_year);

  const uint16_t first = DAYLIGHT_ANCHOR_DOY[0];
  const uint16_t last  = DAYLIGHT_ANCHOR_DOY[ER_DAYLIGHT_MONTHS - 1];

  // The wrapped December -> January segment, from either end of the year.
  if (d < first || d >= last) {
    const uint16_t span = (uint16_t)(first + DL_YEAR_DAYS - last);
    const uint16_t pos  = (d >= last) ? (uint16_t)(d - last)
                                      : (uint16_t)(d + DL_YEAR_DAYS - last);
    return lerp_min(table[ER_DAYLIGHT_MONTHS - 1], table[0], pos, span);
  }

  for (uint8_t m = 0; m + 1u < (uint8_t)ER_DAYLIGHT_MONTHS; ++m) {
    const uint16_t a0 = DAYLIGHT_ANCHOR_DOY[m];
    const uint16_t a1 = DAYLIGHT_ANCHOR_DOY[m + 1];
    if (d >= a0 && d < a1) {
      return lerp_min(table[m], table[m + 1], (uint16_t)(d - a0), (uint16_t)(a1 - a0));
    }
  }
  return table[ER_DAYLIGHT_MONTHS - 1];   // unreachable; the loop covers it
}

uint16_t daylight_sunrise_min(uint16_t day_of_year)
{
  return sample(DAYLIGHT_SUNRISE_MIN, day_of_year);
}

uint16_t daylight_sunset_min(uint16_t day_of_year)
{
  return sample(DAYLIGHT_SUNSET_MIN, day_of_year);
}

uint16_t daylight_bedtime_min(uint16_t day_of_year)
{
  const uint32_t s = (uint32_t)daylight_sunset_min(day_of_year)
                   + (uint32_t)SLEEP_AFTER_DUSK_MIN;
  return (uint16_t)(s % ER_MINUTES_PER_DAY);
}

uint8_t daylight_is_night(uint16_t day_of_year, uint16_t minute_of_day)
{
  const uint16_t m    = (uint16_t)(minute_of_day % ER_MINUTES_PER_DAY);
  const uint16_t rise = daylight_sunrise_min(day_of_year);
  const uint16_t bed  = daylight_bedtime_min(day_of_year);

  // The normal case: bedtime is in the evening and sunrise the next morning, so
  // night is the range that WRAPS midnight.
  if (bed > rise) return (m >= bed || m < rise) ? 1u : 0u;

  // bed <= rise. Unreachable with the committed table — daylight_every_day_of_
  // the_year_is_a_sane_day asserts bed > rise on all 366 days — and it would
  // only arise if a future table edit pushed bedtime past midnight. Note this
  // branch then describes a SAME-DAY range, which is the wrong shape for that
  // case: a bedtime after midnight needs `m >= bed || m < rise` too, against a
  // rise on the following day. Left as the conservative reading (a narrow night
  // rather than an inverted one), but anyone editing the table past midnight
  // must revisit it rather than trust it.
  return (m >= bed && m < rise) ? 1u : 0u;
}
