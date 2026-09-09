// =============================================================================
//  ERRATA - game/daylight.h
//  When it is dark outside, as well as a device with no light sensor, no GPS
//  and no latitude can know it (plan P3-C2b, decision D13).
//
//  WHAT THIS IS, PLAINLY: AN APPROXIMATION, AND A DELIBERATE ONE.
//  The firmware has no way to compute a true sunrise. Computing one needs a
//  latitude, and there is no latitude anywhere in this tree - it went with the
//  weather module and it is not coming back (spec section 44 is explicit that
//  the Wi-Fi scanner is a sensor, not a geolocator). What the device does have
//  is the local wall clock: SimEnv.local_hour / .local_min, already corrected
//  by the TZ string, and SimEnv.day_of_year.
//
//  So this module interpolates a twelve-entry table of sunrise / sunset times
//  (data/balance.h) by day of year. The table describes mid-northern latitudes
//  - about 40 deg N, peninsular Spain, which is where the default CFG_TZ_STRING
//  points - in LOCAL OFFICIAL TIME. The TZ string has ALREADY applied daylight
//  saving, so the table must not apply it again; that is why the March -> April
//  and October -> November steps in the table are an hour wide.
//
//  A player far from that latitude sees a drift: the further north, the more
//  the real summer evening outruns the table, and near the equator the table's
//  seasonal swing is simply too large. That is a known cost, not an oversight.
//  The creature goes to bed a bit early in a Norwegian June. Nothing breaks.
//
//  Integer arithmetic only - the stat paths carry no floating point and this
//  needs none. Every value is minutes from local midnight, 0..1439.
//
//  PURE translation unit: <stdint.h> and data/balance.h, nothing else.
// =============================================================================
#ifndef ER_DAYLIGHT_H
#define ER_DAYLIGHT_H

#include <stdint.h>

// Minutes from local midnight of the approximated sunrise / sunset for
// `day_of_year` (0..365; anything larger is folded into the year). The month
// samples are anchored at the middle of each month and interpolated linearly
// between neighbours, wrapping December into January, so the curve is
// continuous everywhere - there is no jump on the 1st of any month.
uint16_t daylight_sunrise_min(uint16_t day_of_year);
uint16_t daylight_sunset_min(uint16_t day_of_year);

// Bedtime: sunset + SLEEP_AFTER_DUSK_MIN. Nothing falls asleep the instant the
// sun goes down, so the creature settles a while after dark and wakes at
// sunrise. Never wraps past midnight with the committed table (the latest
// bedtime is 23:15 in late June), but the modulo is applied anyway so a future
// table edit cannot produce a bedtime of 24:30.
uint16_t daylight_bedtime_min(uint16_t day_of_year);

// 1 while the clock is inside [bedtime, sunrise) for that day, 0 otherwise.
// `minute_of_day` is 0..1439; larger values are folded.
uint8_t  daylight_is_night(uint16_t day_of_year, uint16_t minute_of_day);

#endif  // ER_DAYLIGHT_H
