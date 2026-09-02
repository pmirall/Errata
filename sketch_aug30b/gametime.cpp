// =============================================================================
//  NOTTAMAGOCHI - gametime.cpp
//  Wall clock, SNTP bring-up, virtual skew, exact elapsed formatting.
//
//  The whole file is deliberately host-compilable: everything that needs
//  Arduino / ESP-IDF sits behind #if defined(ARDUINO), so gt_format_elapsed()
//  and the millis() wrap extension can be unit-tested with MSVC before they
//  ever touch the board (TOOLING.md 2).
//
//  Zero floating point. Zero String. Zero blocking calls.
// =============================================================================
#include "gametime.h"

#include <stdio.h>      // snprintf
#include <string.h>     // memset, strlen
#include <stdlib.h>     // setenv/_putenv_s

#include "config.h"
#include "nt_types.h"   // struct Config, NT_CFG_MAGIC, CONFIG_CRC_BYTES, ...

#if defined(ARDUINO)
  #include <Arduino.h>        // millis(), configTzTime()
  #include "esp_timer.h"      // esp_timer_get_time() - the exact 64-bit uptime
  #include "storage.h"        // store_last_seen(), store_load_cfg() - read only
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
// SNTP lands, time() returns seconds-since-boot, i.e. a value near zero, so the
// test is unambiguous - there is no plausible way to be wrong by 47 years.
// -----------------------------------------------------------------------------
// PH3 #2: this is now config.h's NT_EPOCH_SANE_MIN, so the .ino's absence
// discriminator and this module cannot drift apart. The value is unchanged.
#define GT_EPOCH_SANE_MIN   NT_EPOCH_SANE_MIN   // 1483228800 = 2017-01-01T00:00:00Z

// Skew is clamped so that base + skew can never leave the uint32_t epoch range
// by more than one full wrap, which keeps the saturation in gt_now() honest.
#define GT_SKEW_LIMIT_S     ((int64_t)0xFFFFFFFF)

// One SNTP attempt per this window while the clock is still an estimate:
// SNTP_GIVEUP_S (30 s, the budget a single attempt gets - SNTP's own random
// startup delay is up to 5 s) plus SNTP_RETRY_S (300 s, GAME_DESIGN 5.1
// "retry SNTP every 5 min"). Declaring the attempt dead at 30 s is the
// ABSENCE_UNKNOWN trigger, and that decision belongs to the absence code
// polling gt_is_valid() - here it is only a retry cadence.
#define GT_SYNC_CYCLE_MS  (((uint64_t)SNTP_GIVEUP_S + (uint64_t)SNTP_RETRY_S) * 1000ULL)

// ---- module state (~64 B of .bss) -------------------------------------------
static char     s_tz[TZ_MAX_LEN + 1];   // active POSIX TZ string
static bool     s_begun        = false;
static bool     s_real         = false; // a real SNTP timestamp has been seen
static bool     s_sntp_armed   = false; // configTzTime() issued at least once
static uint64_t s_sync_ms      = 0;     // uptime at the last configTzTime()
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
//                        so a device that never meets an NTP server still
//                        reports plausible, monotonically increasing epochs
//                        and an absence of ~0 instead of inventing one.
//   store_load_cfg()  -> Config.tz, so a timezone changed from S9 SETTINGS or
//                        POST /api/cfg is honoured on the next boot with no
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
  s_real        = false;
  s_sntp_armed  = false;
  s_sync_ms     = 0;
  s_skew_s      = 0;

  gt_load_seed();
  gt_apply_tz();

  s_est_base_ms = gt_mono_ms();
  s_begun       = true;

  // If the RTC already holds a sane time (a soft reset does not clear it, and
  // the SNTP fix from the previous run survives), adopt it immediately instead
  // of pretending we are blind for the next 30 s.
  (void)gt_is_valid();
}

void gt_sync_start(void)
{
  const uint64_t now_ms = gt_mono_ms();

  if (s_sntp_armed) {
    // Already on real time: lwIP re-polls by itself every SNTP_RESYNC_S
    // (CONFIG_LWIP_SNTP_UPDATE_DELAY = 3 h). Nothing to do.
    if (gt_is_valid()) {
      return;
    }
    // Still an estimate: an attempt is either in flight or cooling down.
    if ((now_ms - s_sync_ms) < GT_SYNC_CYCLE_MS) {
      return;
    }
  }

#if defined(ARDUINO)
  // Exactly three servers: CONFIG_LWIP_SNTP_MAX_SERVERS = 3 (BRIEF 1.7).
  // Non-blocking - configTzTime() only does sntp_stop()/sntp_init() + setenv,
  // and it is safe to re-issue (esp32-hal-time.c stops SNTP first).
  configTzTime(s_tz, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
#endif

  s_sntp_armed = true;
  s_sync_ms    = now_ms;
}

bool gt_is_valid(void)
{
  // Always sample the monotonic clock: this is what keeps the millis() wrap
  // counter honest on the host path, and it is a handful of nanoseconds.
  const uint64_t now_ms = gt_mono_ms();

#if !defined(ARDUINO) && defined(GT_HOST_NEVER_VALID)
  // Host unit-test hook only. ARDUINO is always defined by the Arduino build,
  // so this cannot exist in shipped firmware. It pins the module on the
  // estimated-clock path so the millis() wrap extension can be driven across
  // the 2^32 ms boundary on a machine whose wall clock is genuinely correct.
  (void)now_ms;
  return false;
#else

  if (s_real) {
    return true;
  }

  // time() is cheap (a systimer read). No blocking poll loop, no getLocalTime()
  // with its internal delay(10) - loop() must never stall here.
  // t > 0 first: time_t is signed (64-bit on IDF 5.3, 32-bit elsewhere) and a
  // negative value cast to unsigned would sail past the threshold.
  const time_t t = time(NULL);
  if (t > 0 && (uint64_t)t >= (uint64_t)GT_EPOCH_SANE_MIN) {
    s_real = true;
    return true;
  }

  (void)now_ms;   // sampled purely to advance the millis() wrap extension
  return false;
#endif
}

uint32_t gt_now(void)
{
  int64_t base;

  if (gt_is_valid()) {
    const time_t t = time(NULL);
    base = (t > 0) ? (int64_t)t : 0;
  } else {
    // Estimated clock: last persisted wall time + uptime. Monotonic by
    // construction, and it starts exactly at last_seen_epoch so a device that
    // has never met an NTP server reports an absence of ~0 rather than
    // inventing one. gt_is_valid() is what tells the caller not to trust it.
    const uint64_t up_ms = gt_mono_ms() - s_est_base_ms;
    base = (int64_t)s_est_base_s + (int64_t)(up_ms / 1000ULL);
  }

  int64_t v = base + s_skew_s;
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
