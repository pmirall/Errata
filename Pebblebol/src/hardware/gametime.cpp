// =============================================================================
//  PEBBLEBOL - hardware/gametime.cpp
//  Wall clock, calibration, virtual skew, exact elapsed formatting.
//
//  The whole file is deliberately host-compilable: everything that needs
//  Arduino / ESP-IDF sits behind #if defined(ARDUINO), so gt_format_elapsed()
//  and the millis() wrap extension are unit-tested on the host by
//  tests/test_clock.cpp (built with -DGT_HOST_NEVER_VALID) before they ever
//  touch the board.
//
//  Zero floating point. Zero String. Zero blocking calls.
// =============================================================================
#include "gametime.h"

#include <stdio.h>      // snprintf
#include <string.h>     // memset, strlen
#include <stdlib.h>     // setenv/_putenv_s

#include "../core/config.h"
#include "../core/nt_types.h"   // struct Config, NT_CFG_MAGIC, CONFIG_CRC_BYTES, ...

#if defined(ARDUINO)
  #include <Arduino.h>        // millis()
  #include <sys/time.h>       // settimeofday()
  #include "esp_timer.h"      // esp_timer_get_time() - the exact 64-bit uptime
  #include "../persistence/storage.h"        // store_last_seen(), store_load_cfg() - read only
#else
  // Host builds supply the 32-bit millisecond tick themselves so the wrap
  // extension below can be driven across a rollover boundary by a unit test.
  extern "C" uint32_t gt_host_millis32(void);
#endif

// The compile-time default must fit Config.tz, or a persisted config could
// never round-trip the value we install at boot.
static_assert(sizeof(CFG_TZ_STRING) <= TZ_MAX_LEN + 1, "CFG_TZ_STRING is longer than Config.tz");

// -----------------------------------------------------------------------------
// A real clock reads at least 2017-01-01T00:00:00Z. This mirrors the core's own
// heuristic in getLocalTime() (esp32-hal-time.c: `info->tm_year > (2016-1900)`),
// which is the definition every other Arduino-ESP32 sketch agrees on. Before
// anything calibrates the clock, time() returns seconds-since-boot, i.e. a
// value near zero, so the test is unambiguous - there is no plausible way to be
// wrong by 47 years.
// -----------------------------------------------------------------------------
// PH3 #2: this is now config.h's NT_EPOCH_SANE_MIN, so app.cpp's absence
// discriminator and this module cannot drift apart. The value is unchanged.
#define GT_EPOCH_SANE_MIN   NT_EPOCH_SANE_MIN   // 1483228800 = 2017-01-01T00:00:00Z

// Skew is clamped so that base + skew can never leave the uint32_t epoch range
// by more than one full wrap, which keeps the saturation in gt_now() honest.
#define GT_SKEW_LIMIT_S     ((int64_t)0xFFFFFFFF)

// ---- module state (~48 B of .bss) -------------------------------------------
static char     s_tz[TZ_MAX_LEN + 1];   // active POSIX TZ string
static bool     s_begun        = false;
static uint8_t  s_cal          = (uint8_t)CAL_UNSET;   // TimeCal
static uint32_t s_est_base_s   = 0;     // last persisted wall clock (NVS "t")
static uint64_t s_est_base_ms  = 0;     // uptime when s_est_base_s was taken
static int64_t  s_skew_s       = 0;     // god-mode virtual time travel

// ---- 32-bit millis() rollover state -----------------------------------------
static uint32_t s_ms32_last    = 0;
static uint32_t s_ms32_wraps   = 0;

// =============================================================================
//  MONOTONIC UPTIME - the 49.7-day millis() rollover, handled and proven
// =============================================================================
//  millis() is uint32_t milliseconds since boot. It wraps at 2^32 ms =
//  4,294,967,296 ms = 49.710269... days. A Nottamagochi left plugged in on a
//  shelf reaches that in under two months, so this is a real, reachable bug,
//  not a theoretical one - and every timestamp in the absence mechanic hangs
//  off it while the clock is estimated.
//
//  DEFENCE 1 - the wrap extension (the actual algorithm, used on every target).
//    Observe millis() as m[0], m[1], ... at successive calls. The counter is
//    monotonically increasing modulo 2^32. Therefore:
//        m[i] < m[i-1]  <=>  the counter crossed exactly one 2^32 boundary
//                            between the two observations,
//    PROVIDED the true elapsed time between two consecutive observations is
//    strictly less than 2^32 ms (otherwise a whole wrap could be skipped
//    unseen, or two wraps could look like one). Under that precondition the
//    running counter s_ms32_wraps is exact, and
//        uptime_ms = ((uint64_t)s_ms32_wraps << 32) | m[i]
//    is exact for all time, with no drift, no division and no 64-bit clock.
//
//    The precondition holds by seven orders of magnitude: the main loop calls
//    gt_now()/gt_is_valid() at least once per 1 Hz sim tick, so the sampling
//    period is milliseconds against a 49.7-day bound. The only way to violate
//    it would be a >49.7-day stall inside one loop() iteration, which the task
//    watchdog would have killed 49.7 days earlier.
//
//  DEFENCE 2 - self-healing from the exact source (ESP32 only).
//    On this core millis() is *literally* esp_timer_get_time()/1000
//    (esp32-hal-misc.c:202), and esp_timer_get_time() is an int64_t microsecond
//    counter: 2^63 us = 292,471 years, so it cannot wrap in the life of the
//    device. We therefore recompute the true 64-bit millisecond uptime from it
//    and, if the extended value ever disagrees, repair s_ms32_wraps from the
//    authoritative high word. This makes the precondition of Defence 1
//    unnecessary on hardware: even a hypothetical multi-month gap between calls
//    self-corrects on the very next call instead of silently losing 49.7 days.
//
//  Host builds keep Defence 1 only - which is exactly the code path the unit
//  test drives across a synthetic 0xFFFFFF00 -> 0x00000100 boundary.
// =============================================================================
static uint64_t gt_mono_ms(void)
{
#if defined(ARDUINO)
  const uint32_t m32 = millis();
#else
  const uint32_t m32 = gt_host_millis32();
#endif

  if (m32 < s_ms32_last) {
    s_ms32_wraps++;             // Defence 1: exactly one boundary crossed
  }
  s_ms32_last = m32;

  uint64_t ext = ((uint64_t)s_ms32_wraps << 32) | (uint64_t)m32;

#if defined(ARDUINO)
  // Defence 2: repair from the wrap-free 64-bit source.
  const uint64_t exact = (uint64_t)(esp_timer_get_time() / 1000);
  if (exact != ext) {
    s_ms32_wraps = (uint32_t)(exact >> 32);
    ext = exact;
  }
#endif

  return ext;
}

// -----------------------------------------------------------------------------
// Install the POSIX TZ string into the C library.
// -----------------------------------------------------------------------------
static void gt_apply_tz(void)
{
#if defined(_MSC_VER)
  _putenv_s("TZ", s_tz);
  _tzset();
#else
  setenv("TZ", s_tz, 1);
  tzset();
#endif
}

// -----------------------------------------------------------------------------
// Bootstrap from persistence, through storage.h - read only, no writes, no NVS
// handle of our own (storage.cpp is the single owner of Preferences).
//   store_last_seen() -> newest of NVS "t", PetSave.last_seen_epoch and the
//                        RTC mirror. This is the base of the ESTIMATED clock,
//                        so a device whose clock was never calibrated still
//                        reports plausible, monotonically increasing epochs
//                        and an absence of ~0 instead of inventing one.
//   store_load_cfg()  -> Config.tz, so a timezone changed from SETTINGS is
//                        honoured on the next boot with no
//                        network at all. Returns false (and fills the
//                        CFG_TZ_STRING defaults) when nothing is persisted.
//
// ORDERING: store_begin() must run before gt_begin(). That is safe and
// one-way - storage.cpp never calls into gametime; it takes epochs as
// arguments. If gt_begin() is called first, the seed is simply empty and the
// estimated clock starts at the epoch, which gt_is_valid() already reports as
// untrustworthy.
// -----------------------------------------------------------------------------
static void gt_load_seed(void)
{
  // Compile-time default first; a persisted Config may override it.
  snprintf(s_tz, sizeof(s_tz), "%s", CFG_TZ_STRING);
  s_est_base_s = 0;

#if defined(ARDUINO)
  const uint32_t seen = store_last_seen();
  if (seen >= (uint32_t)GT_EPOCH_SANE_MIN) {
    s_est_base_s = seen;
  }

  Config c;
  memset(&c, 0, sizeof(c));
  if (store_load_cfg(c)) {
    c.tz[TZ_MAX_LEN] = '\0';        // a short/odd blob must not run off the end
    if (c.tz[0] != '\0') {
      snprintf(s_tz, sizeof(s_tz), "%s", c.tz);
    }
  }
#endif
}

// =============================================================================
//  PUBLIC INTERFACE
// =============================================================================

void gt_begin(void)
{
  if (s_begun) {
    return;
  }

  s_ms32_last   = 0;
  s_ms32_wraps  = 0;
  s_cal         = (uint8_t)CAL_UNSET;
  s_skew_s      = 0;

  gt_load_seed();
  gt_apply_tz();

  s_est_base_ms = gt_mono_ms();
  s_begun       = true;

  // If the RTC timer already holds a sane time (a soft reset and a deep sleep
  // both leave it running, so the previous run's calibration survives), adopt
  // it as CAL_ESTIMATED immediately instead of pretending we are blind.
  (void)gt_is_valid();
}

// -----------------------------------------------------------------------------
// Is the C library's own clock a real wall clock right now? On the target that
// is the RTC timer, which keeps running across a soft reset and a deep sleep,
// so a calibration made in a previous run is still here. The host build under
// GT_HOST_NEVER_VALID answers "no" unconditionally: the test machine's wall
// clock is genuinely correct, and the module must stay on the estimated path
// so the millis() wrap extension can be driven across 2^32.
// -----------------------------------------------------------------------------
static bool sys_clock_ok(void)
{
#if !defined(ARDUINO) && defined(GT_HOST_NEVER_VALID)
  return false;
#else
  // t > 0 first: time_t is signed (64-bit on IDF 5.3, 32-bit elsewhere) and a
  // negative value cast to unsigned would sail past the threshold.
  const time_t t = time(NULL);
  return (t > 0) && ((uint64_t)t >= (uint64_t)GT_EPOCH_SANE_MIN);
#endif
}

// The current epoch WITHOUT the god-mode skew - what a calibration is compared
// against, because the skew is virtual time travel and must survive the set.
static uint32_t gt_base_now(void)
{
  if (sys_clock_ok()) {
    const time_t t = time(NULL);
    return (t > 0) ? (uint32_t)t : 0u;
  }
  const uint64_t up_ms = gt_mono_ms() - s_est_base_ms;
  const uint64_t v     = (uint64_t)s_est_base_s + (up_ms / 1000ULL);
  return (v > 0xFFFFFFFFULL) ? 0xFFFFFFFFu : (uint32_t)v;
}

bool gt_set_epoch(uint32_t epoch, TimeCal src)
{
  if (src == CAL_UNSET || (uint8_t)src >= (uint8_t)CAL_COUNT) {
    return false;
  }
  // An uptime counter is not a date. This is the same threshold the absence
  // discriminator uses, so the two cannot drift apart.
  if (epoch < (uint32_t)GT_EPOCH_SANE_MIN) {
    return false;
  }

  // Rollback guard (plan section 1.7). Moving an already-calibrated clock
  // backwards re-runs cooldowns, re-opens the sleep window and makes every
  // persisted last_seen sit in the future, so it takes a human who is looking
  // at the device. Forward jumps are always allowed: that is a correction.
  if (s_cal != (uint8_t)CAL_UNSET && src != CAL_USER) {
    const uint32_t cur = gt_base_now();
    if (epoch < cur && (uint32_t)(cur - epoch) > GT_ROLLBACK_TOLERANCE_S) {
      return false;
    }
  }

  // The estimated base is updated too, so the two paths agree the instant the
  // set lands and a host build with no settimeofday() still reads the new time.
  s_est_base_s  = epoch;
  s_est_base_ms = gt_mono_ms();

#if defined(ARDUINO)
  struct timeval tv;
  tv.tv_sec  = (time_t)epoch;
  tv.tv_usec = 0;
  settimeofday(&tv, NULL);
#endif

  s_cal = (uint8_t)src;
  return true;
}

uint32_t gt_epoch_from_local(int year, uint8_t month, uint8_t day,
                             uint8_t hour, uint8_t minute)
{
  if (year < 1970 || year > 2200 || month < 1 || month > 12 ||
      day < 1 || day > 31 || hour > 23 || minute > 59) {
    return 0;
  }

  struct tm t;
  memset(&t, 0, sizeof(t));
  t.tm_year  = year - 1900;
  t.tm_mon   = (int)month - 1;
  t.tm_mday  = (int)day;
  t.tm_hour  = (int)hour;
  t.tm_min   = (int)minute;
  t.tm_sec   = 0;
  t.tm_isdst = -1;              // let the TZ rule decide; the screen cannot

  const time_t e = mktime(&t);
  if (e <= 0 || (uint64_t)e > 0xFFFFFFFFULL) {
    return 0;
  }
  // mktime() normalises out-of-range fields (31 April -> 1 May). Refuse that
  // instead of silently accepting a date the user did not type.
  if (t.tm_mday != (int)day || t.tm_mon != (int)month - 1) {
    return 0;
  }
  return (uint32_t)e;
}

TimeCal gt_cal_state(void)
{
  return (TimeCal)s_cal;
}

bool gt_is_valid(void)
{
  // Always sample the monotonic clock: this is what keeps the millis() wrap
  // counter honest on the host path, and it is a handful of nanoseconds.
  (void)gt_mono_ms();

  if (s_cal != (uint8_t)CAL_UNSET) {
    return true;
  }

  // Nobody has calibrated us this run, but the RTC timer may still be carrying
  // a previous run's calibration across the reset. Adopt it as CAL_ESTIMATED.
  // time() is cheap (a systimer read). No blocking poll loop, no getLocalTime()
  // with its internal delay(10) - loop() must never stall here.
  if (sys_clock_ok()) {
    s_cal = (uint8_t)CAL_ESTIMATED;
    return true;
  }
  return false;
}

uint32_t gt_now(void)
{
  // Called for the calibration-state adoption and the wrap counter, not for
  // the answer: gt_base_now() picks the source.
  (void)gt_is_valid();

  // Estimated clock (gt_base_now's second branch): last persisted wall time +
  // uptime. Monotonic by construction, and it starts exactly at
  // last_seen_epoch so a device that has never been calibrated reports an
  // absence of ~0 rather than inventing one. gt_is_valid() is what tells the
  // caller not to trust it.
  int64_t v = (int64_t)gt_base_now() + s_skew_s;
  if (v < 0) {
    v = 0;
  } else if (v > (int64_t)0xFFFFFFFF) {
    v = (int64_t)0xFFFFFFFF;
  }
  return (uint32_t)v;
}

void gt_skew_add(int64_t delta_s)
{
  int64_t s = s_skew_s + delta_s;
  if (s >  GT_SKEW_LIMIT_S) s =  GT_SKEW_LIMIT_S;
  if (s < -GT_SKEW_LIMIT_S) s = -GT_SKEW_LIMIT_S;
  s_skew_s = s;
}

const char* gt_format_elapsed(uint32_t seconds, char* buf, size_t buflen)
{
  static const char EMPTY[] = "";
  if (buf == NULL || buflen == 0) {
    return EMPTY;
  }
  buf[0] = '\0';
  if (buflen < 2) {
    return buf;
  }

  // Pure integer decomposition. GAME_DESIGN 5.3: exact, never rounded - the
  // sub-unit is truncated downward, so "2 d 7 h 41 min 59 s" prints as
  // "2 d 7 h 41 min" and never creeps up to 42.
  uint32_t rem = seconds;
  const uint32_t d = rem / 86400UL; rem -= d * 86400UL;
  const uint32_t h = rem / 3600UL;  rem -= h * 3600UL;
  const uint32_t m = rem / 60UL;    rem -= m * 60UL;

  int n;
  if (d > 0) {
    n = snprintf(buf, buflen, "%u d %u h %u min",
                 (unsigned)d, (unsigned)h, (unsigned)m);
  } else if (h > 0) {
    n = snprintf(buf, buflen, "%u h %u min", (unsigned)h, (unsigned)m);
  } else if (m > 0) {
    n = snprintf(buf, buflen, "%u min", (unsigned)m);
  } else {
    n = snprintf(buf, buflen, "%u s", (unsigned)rem);
  }
  if (n < 0) {
    buf[0] = '\0';               // encoding error: hand back an empty string
  }
  return buf;
}

uint32_t gt_elapsed_since(uint32_t then, uint32_t now)
{
  // Plan section 1.7: an epoch delta is clamped to [0, ...]. `now <= then` is
  // never an elapsed time - it is a rollback, an uncalibrated baseline or a
  // save written by a device whose clock was ahead - and subtracting would
  // wrap into 136 years of "abandonment".
  return (now > then) ? (uint32_t)(now - then) : 0u;
}

bool gt_local_tm(struct tm& out)
{
  memset(&out, 0, sizeof(out));
  const time_t t = (time_t)gt_now();

#if defined(_MSC_VER)
  localtime_s(&out, &t);
#else
  localtime_r(&t, &out);
#endif

  return gt_is_valid();
}

#if !defined(ARDUINO)
void gt_test_reset(void)
{
  s_begun       = false;
  s_cal         = (uint8_t)CAL_UNSET;
  s_est_base_s  = 0;
  s_est_base_ms = 0;
  s_skew_s      = 0;
  s_ms32_last   = 0;
  s_ms32_wraps  = 0;
}
#endif
