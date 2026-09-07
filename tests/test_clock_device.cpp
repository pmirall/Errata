// =============================================================================
//  Pebblebol host tests - test_clock_device.cpp
//
//  THE DEVICE BRANCH OF gt_mono_ms(), WHICH NO HOST BINARY COULD SEE UNTIL NOW.
//
//  hardware/gametime.cpp has two bodies for its monotonic clock. The HOST one
//  reads a fake millis() and extends it across 2^32; tests/test_clock.cpp
//  drives that. The DEVICE one reads esp_rtc_get_time_us() - THE RTC COUNTER -
//  and it is the whole of P6-C3's move off uptime, because a light sleep stops
//  esp_timer and a deep sleep or a reset takes it back to zero while the RTC
//  keeps counting. Until this file existed that choice was protected by a grep
//  in tools/check.sh: the grep can say the right identifier appears in the
//  right function, and it cannot say the clock BEHAVES like the RTC.
//
//  So tests/Makefile compiles gametime.cpp a second time with -DARDUINO and
//  tests/fakes/arduino in front of the system include path, and this binary
//  drives the two clocks APART - which is the only interesting thing about
//  them and the one thing the grep cannot reach.
//
//  gt_test_reset() is #if !defined(ARDUINO) and therefore absent here, which is
//  why every case below starts from gt_begin() rather than from a reset. That
//  is closer to the device anyway: on a board there is no reset either.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "core/nt_types.h"
#include "hardware/gametime.h"
#include "game/cooldowns.h"
#include "persistence/game_state.h"
#include "persistence/save_manager.h"
#include "fakes/kv_mem.h"
#include "fakes/arduino/Arduino.h"

// Sets the two clocks and brings the module up. gt_begin() is idempotent by
// its own `s_begun` guard, so only the FIRST case actually runs it - which is
// fine and is why every assertion below is about gt_mono32(), a function that
// reads the RTC on every call and carries no boot state at all.
static void device_begin(uint32_t uptime_ms, uint64_t rtc_us) {
  kv_mem_reset();
  fake_uptime_set_ms(uptime_ms);
  fake_rtc_set_us(rtc_us);
  gt_begin();
  cd_begin();
}

// =============================================================================
//  1. THE CLOCK IS THE RTC AND IT IS NOT UPTIME
// =============================================================================
TEST(the_device_clock_follows_the_rtc_counter_and_not_the_uptime_timer) {
  // The two clocks start deliberately far apart, so a gt_mono32() that read
  // millis() would answer a number this case can name.
  device_begin(1000u, 500000000ULL);          // uptime 1 s, RTC 500 s
  const uint32_t m0 = gt_mono32();
  CHECK_EQ(m0, 500000u);                       // 500,000,000 us = 500,000 ms
  CHECK(m0 != 1000u);                          // ... and NOT the uptime

  // Move ONLY the uptime. The monotonic clock must not notice.
  fake_uptime_set_ms(999000u);
  CHECK_EQ(gt_mono32(), m0);

  // Move ONLY the RTC. It must follow that, exactly.
  fake_rtc_set_us(500000000ULL + 4321ULL * 1000ULL);
  CHECK_EQ(gt_mono32(), m0 + 4321u);
}

// =============================================================================
//  2. THE WAKE THIS CHOICE EXISTS FOR
//
//  A deep-sleep wake or a reset takes UPTIME back to zero while the RTC keeps
//  counting through the sleep. hardware/power.h's static_assert keeps this
//  firmware on LIGHT sleep today - where esp_light_sleep_start()
//  resynchronises esp_timer on the way out - but the day a deep rung is
//  written, THIS is the property that has to already hold.
// =============================================================================
TEST(a_wake_that_zeroes_the_uptime_leaves_the_monotonic_clock_carrying_the_gap) {
  device_begin(120000u, 3600ULL * 1000000ULL);       // 2 min up, 1 h of RTC
  const uint32_t before = gt_mono32();
  CHECK_EQ(before, 3600u * 1000u);

  // THE WAKE: uptime to zero, RTC forward by twenty minutes of sleep.
  const uint32_t slept_ms = 20u * 60u * 1000u;
  fake_uptime_set_ms(0u);
  fake_rtc_set_us((uint64_t)(3600u * 1000u + slept_ms) * 1000ULL);

  const uint32_t after = gt_mono32();
  CHECK_EQ(after, before + slept_ms);                // the gap was carried
  CHECK(after > before);                             // and the clock never went back

  // A SECOND wake, on top of the first: the counter is cumulative, not per-boot.
  fake_uptime_set_ms(0u);
  fake_rtc_set_us((uint64_t)(after + slept_ms) * 1000ULL);
  CHECK_EQ(gt_mono32(), after + slept_ms);
}

// =============================================================================
//  3. AND THE THING THAT DEPENDS ON IT: THE UNCALIBRATED COOLDOWN TABLE
//
//  game/cooldowns.h's RAM path measures its deadlines in gt_mono32()
//  milliseconds - "ALWAYS read: it is what the RAM table's deadlines are
//  measured in". If that clock were uptime, a wake would take every armed
//  cooldown back to zero and the table would expire nothing on its own
//  schedule, which is the failure a grep cannot see.
// =============================================================================
TEST(an_uncalibrated_cooldown_still_expires_on_its_own_schedule_across_a_wake) {
  device_begin(5000u, 5000ULL * 1000ULL);
  // gt_cal_state() is NOT asserted here, and the reason is worth writing down:
  // on the device branch gt_begin() adopts the C library clock as
  // CAL_ESTIMATED when it already holds a sane time, and on THIS machine the
  // wall clock really is right - which is exactly why tests/test_clock.cpp's
  // binary is built with GT_HOST_NEVER_VALID instead. It does not matter: the
  // cooldown module reads the cal it is HANDED, and the RAM path is selected
  // below by CdClock.cal and by nothing else.
  CooldownTable t;
  memset(&t, 0, sizeof t);
  CdClock c;
  c.now_epoch = 0u;                                  // meaningless while CAL_UNSET
  c.cal       = (uint8_t)CAL_UNSET;
  c.now_ms    = gt_mono32();

  const uint32_t net = 0xC0FFEE01u;
  CHECK(cd_ready(t, net, c));                        // never armed: ready
  CHECK(cd_arm(t, net, c));
  CHECK(!cd_ready(t, net, c));                       // and now it is not

  // HALF the cooldown passes as a SLEEP: uptime is reset, the RTC carries it.
  const uint32_t half_ms = (uint32_t)ENCOUNTER_COOLDOWN_S * 1000u / 2u;
  fake_uptime_set_ms(0u);
  fake_rtc_set_us((uint64_t)(gt_mono32() + half_ms) * 1000ULL);
  c.now_ms = gt_mono32();
  CHECK(!cd_ready(t, net, c));                       // still armed, correctly

  // The rest of it, the same way. The cooldown expires on ITS OWN schedule and
  // not on how many times the device woke up.
  fake_uptime_set_ms(0u);
  fake_rtc_set_us((uint64_t)(gt_mono32() + half_ms + 1000u) * 1000ULL);
  c.now_ms = gt_mono32();
  CHECK(cd_ready(t, net, c));

  // THE CONTROL ARM, so this case cannot pass for the wrong reason: had the
  // clock been uptime, c.now_ms would have been ~0 at both checks above and the
  // second one would have answered "not ready" - the deadline is in the future
  // of a clock that keeps restarting. Assert that the number really did move.
  CHECK(c.now_ms > (uint32_t)ENCOUNTER_COOLDOWN_S * 1000u);
  CHECK_EQ(millis(), 0u);                            // while uptime is still zero
}

// =============================================================================
//  4. THE 32-BIT VIEW IS A TRUNCATION OF A 64-BIT COUNT
//
//  Defence 1 (the wrap extension) is not needed on this branch because the
//  source is already 64-bit - and gametime.cpp says so. What must still hold is
//  that gt_mono32() is the low 32 bits of it, so every existing consumer's
//  wrap-safe subtraction keeps working.
// =============================================================================
TEST(the_device_clock_truncates_rather_than_saturating_past_the_wrap) {
  const uint64_t just_under = 0xFFFFFF00ULL;         // ms
  device_begin(0u, just_under * 1000ULL);
  CHECK_EQ(gt_mono32(), (uint32_t)just_under);

  fake_rtc_set_us((just_under + 0x200ULL) * 1000ULL);
  CHECK_EQ(gt_mono32(), (uint32_t)(just_under + 0x200ULL));   // 0x00000100

  // And a consumer's wrap-safe difference still reads the real elapsed time.
  const uint32_t a = (uint32_t)just_under;
  const uint32_t b = gt_mono32();
  CHECK_EQ((uint32_t)(b - a), 0x200u);
}
