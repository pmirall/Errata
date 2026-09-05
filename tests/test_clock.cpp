// =============================================================================
//  Pebblebol host tests - test_clock.cpp
//  gametime.cpp on the host, compiled with GT_HOST_NEVER_VALID so the module
//  stays on the estimated-clock path and the 32-bit millis() wrap extension
//  can be driven across 2^32 with the fake clock from host_shims.
// =============================================================================
#include "nt_test.h"

#include <string.h>
#include <time.h>

#include "core/nt_types.h"     // BootKind, nt_boot_charges_absence()
#include "hardware/gametime.h"
#include "host_shims.h"

static const char* fmt(uint32_t s, char* buf) {
  return gt_format_elapsed(s, buf, GT_ELAPSED_BUF);
}

TEST(clock_format_elapsed_units_and_truncation) {
  char b[GT_ELAPSED_BUF];
  CHECK_STR_EQ(fmt(0, b), "0 s");
  CHECK_STR_EQ(fmt(45, b), "45 s");
  CHECK_STR_EQ(fmt(59, b), "59 s");
  CHECK_STR_EQ(fmt(60, b), "1 min");
  CHECK_STR_EQ(fmt(119, b), "1 min");                    // seconds truncated, never rounded
  CHECK_STR_EQ(fmt(12 * 60, b), "12 min");
  CHECK_STR_EQ(fmt(3599, b), "59 min");
  CHECK_STR_EQ(fmt(3600, b), "1 h 0 min");
  CHECK_STR_EQ(fmt(3 * 3600 + 12 * 60 + 59, b), "3 h 12 min");
  CHECK_STR_EQ(fmt(86399, b), "23 h 59 min");
  CHECK_STR_EQ(fmt(86400, b), "1 d 0 h 0 min");
  CHECK_STR_EQ(fmt(2 * 86400 + 7 * 3600 + 41 * 60 + 59, b), "2 d 7 h 41 min");
  CHECK_STR_EQ(fmt(0xFFFFFFFFu, b), "49710 d 6 h 28 min");   // widest possible output
  CHECK(strlen(b) + 1 <= GT_ELAPSED_BUF);
}

TEST(clock_format_elapsed_buffer_edges) {
  char b[GT_ELAPSED_BUF];
  CHECK_STR_EQ(gt_format_elapsed(45, nullptr, GT_ELAPSED_BUF), "");
  CHECK_STR_EQ(gt_format_elapsed(45, b, 0), "");

  memset(b, 'x', sizeof b);
  CHECK(gt_format_elapsed(45, b, 1) == b);
  CHECK_STR_EQ(b, "");                                    // NUL-terminated, nothing else

  memset(b, 'x', sizeof b);
  CHECK(gt_format_elapsed(45, b, 3) == b);                // "45 s" truncated to 2 chars
  CHECK_STR_EQ(b, "45");
  CHECK(b[3] == 'x');                                     // never writes past buflen
}

TEST(clock_estimated_time_survives_the_millis_wrap) {
  // Boot 65 536 ms before the 2^32 ms rollover.
  host_reset();
  host_set_ms(0xFFFF0000u);
  gt_begin();
  CHECK(!gt_is_valid());                                  // GT_HOST_NEVER_VALID build
  CHECK_EQ(gt_now(), 0);                                  // no persisted epoch: base 0

  host_set_ms(0xFFFFFF00u);                               // +65 280 ms
  CHECK_EQ(gt_now(), 65);

  host_set_ms(0x00001000u);                               // wrapped: 69 632 ms since boot
  const uint32_t after_wrap = gt_now();
  CHECK_EQ(after_wrap, 69);                               // (0x10000 + 0x1000) / 1000

  host_set_ms(0x00010000u);
  CHECK_EQ(gt_now(), 131);                                // (0x10000 + 0x10000) / 1000
  CHECK(gt_now() >= after_wrap);                          // monotonic across the boundary

  // A second boundary is handled the same way.
  host_set_ms(0xFFFFFFFFu);
  const uint32_t before_second = gt_now();
  host_set_ms(0x00000000u);
  CHECK(gt_now() >= before_second);
  CHECK_EQ(gt_now(), (uint32_t)((0x100000000ULL + 0x100000000ULL - 0xFFFF0000ULL) / 1000ULL));
}

TEST(clock_skew_adds_to_the_estimate_and_saturates_at_zero) {
  gt_begin();                                             // no-op when already begun
  const uint32_t base = gt_now();
  gt_skew_add(3600);
  CHECK_EQ(gt_now(), base + 3600);
  gt_skew_add(-3600);
  CHECK_EQ(gt_now(), base);
  gt_skew_add(-(int64_t)base - 100000);                  // far below zero
  CHECK_EQ(gt_now(), 0);                                  // clamped, never wraps
  gt_skew_add((int64_t)base + 100000);                    // undo
  CHECK_EQ(gt_now(), base);
}

TEST(clock_local_tm_is_filled_but_flagged_as_estimate) {
  struct tm t;
  memset(&t, 0x5A, sizeof t);
  CHECK(!gt_local_tm(t));                                 // estimate, not a fact
  CHECK(t.tm_year >= 69 && t.tm_year <= 200);             // a sane broken-down time
  CHECK(t.tm_mon >= 0 && t.tm_mon <= 11);
  CHECK(t.tm_mday >= 1 && t.tm_mday <= 31);
}

// =============================================================================
//  Calibration (plan section 1.7). Every case starts from gt_test_reset() so it
//  is independent of the order the registry runs the file in, and ends with the
//  module back on the estimated path for whatever comes next.
// =============================================================================
#define SANE_EPOCH  1700000000u          // 2023-11-14, comfortably >= NT_EPOCH_SANE_MIN

static void clock_fresh(uint32_t at_ms) {
  gt_test_reset();
  host_reset();
  host_set_ms(at_ms);
  gt_begin();
}

TEST(clock_starts_uncalibrated) {
  clock_fresh(1000);
  CHECK_EQ((int)gt_cal_state(), (int)CAL_UNSET);
  CHECK(!gt_is_valid());
  gt_test_reset();
}

TEST(clock_set_epoch_makes_the_clock_valid) {
  clock_fresh(1000);
  CHECK(gt_set_epoch(SANE_EPOCH, CAL_USER));
  CHECK_EQ((int)gt_cal_state(), (int)CAL_USER);
  CHECK(gt_is_valid());                                   // false until the set landed
  CHECK_EQ(gt_now(), SANE_EPOCH);

  host_advance_ms(90000);                                 // the clock keeps running
  CHECK_EQ(gt_now(), SANE_EPOCH + 90);
  gt_test_reset();
}

TEST(clock_set_epoch_refuses_an_uptime) {
  clock_fresh(1000);
  CHECK(!gt_set_epoch(0, CAL_USER));
  CHECK(!gt_set_epoch(1483228799u, CAL_USER));            // NT_EPOCH_SANE_MIN - 1
  CHECK(!gt_set_epoch(SANE_EPOCH, CAL_UNSET));            // not a source
  CHECK_EQ((int)gt_cal_state(), (int)CAL_UNSET);
  CHECK(!gt_is_valid());
  CHECK(gt_set_epoch(1483228800u, CAL_PHONE));            // exactly the threshold: fine
  gt_test_reset();
}

TEST(clock_rollback_beyond_five_minutes_is_refused) {
  clock_fresh(1000);
  CHECK(gt_set_epoch(SANE_EPOCH, CAL_PHONE));

  // Inside the tolerance: accepted from any source.
  CHECK(gt_set_epoch(SANE_EPOCH - 299u, CAL_PHONE));
  CHECK_EQ(gt_now(), SANE_EPOCH - 299u);

  // A whole day backwards from a phone is a bug, not a correction.
  CHECK(!gt_set_epoch(SANE_EPOCH - 86400u, CAL_PHONE));
  CHECK_EQ(gt_now(), SANE_EPOCH - 299u);                  // unchanged
  CHECK_EQ((int)gt_cal_state(), (int)CAL_PHONE);

  // The same rollback from the person holding the device is always allowed.
  CHECK(gt_set_epoch(SANE_EPOCH - 86400u, CAL_USER));
  CHECK_EQ(gt_now(), SANE_EPOCH - 86400u);
  CHECK_EQ((int)gt_cal_state(), (int)CAL_USER);
  gt_test_reset();
}

TEST(clock_forward_jumps_are_always_accepted) {
  clock_fresh(1000);
  CHECK(gt_set_epoch(SANE_EPOCH, CAL_PHONE));
  CHECK(gt_set_epoch(SANE_EPOCH + 400u * 86400u, CAL_PHONE));   // 400 days ahead
  CHECK_EQ(gt_now(), SANE_EPOCH + 400u * 86400u);
  gt_test_reset();
}

TEST(clock_skew_survives_a_calibration) {
  clock_fresh(1000);
  CHECK(gt_set_epoch(SANE_EPOCH, CAL_USER));
  gt_skew_add(3600);
  CHECK_EQ(gt_now(), SANE_EPOCH + 3600u);
  // The rollback guard compares the UNSKEWED clock, so this is a +10 s forward
  // set and not a 3590 s rollback.
  CHECK(gt_set_epoch(SANE_EPOCH + 10u, CAL_PHONE));
  CHECK_EQ(gt_now(), SANE_EPOCH + 10u + 3600u);
  gt_skew_add(-3600);
  gt_test_reset();
}

TEST(clock_epoch_from_local_round_trips_and_rejects_impossible_dates) {
  clock_fresh(1000);
  const uint32_t e = gt_epoch_from_local(2024, 2, 29, 12, 30);   // leap day
  CHECK(e != 0);
  CHECK(gt_set_epoch(e, CAL_USER));

  struct tm lt;
  CHECK(gt_local_tm(lt));
  CHECK_EQ(lt.tm_year + 1900, 2024);
  CHECK_EQ(lt.tm_mon + 1, 2);
  CHECK_EQ(lt.tm_mday, 29);
  CHECK_EQ(lt.tm_hour, 12);
  CHECK_EQ(lt.tm_min, 30);

  CHECK_EQ(gt_epoch_from_local(2023, 2, 29, 12, 0), 0);          // not a leap year
  CHECK_EQ(gt_epoch_from_local(2024, 13, 1, 0, 0), 0);
  CHECK_EQ(gt_epoch_from_local(2024, 4, 31, 0, 0), 0);
  CHECK_EQ(gt_epoch_from_local(2024, 1, 1, 24, 0), 0);
  CHECK_EQ(gt_epoch_from_local(2024, 1, 1, 0, 60), 0);
  gt_test_reset();
}

// =============================================================================
//  P6-C3: ELAPSED TIME ACROSS A SLEEP GAP
//
//  THE SHAPE OF A SLEEP, ON THE HOST. During a light sleep the CPU is stopped:
//  no loop() runs, nothing samples any clock, and the ONLY thing that moves is
//  the monotonic counter. That is exactly what host_advance_ms() does in one
//  step with nothing in between, so these cases really are the device's
//  situation and not a slower version of the ordinary one.
//
//  WHAT THEY GUARD AND WHAT THEY DO NOT. They guard the arithmetic: a gap
//  nobody observed is charged in full, in one piece, and the 32-bit wrap
//  extension survives being crossed inside one. They do NOT guard the choice of
//  SOURCE - that gt_mono_ms() reads the RTC counter on the target rather than
//  esp_timer's uptime - because a host build has neither. That half is a
//  firmware-build fact (hardware/gametime.cpp, "Defence 2") and it has never
//  run on hardware.
// =============================================================================
TEST(clock_elapsed_is_charged_across_a_sleep_gap_the_loop_never_observed) {
  clock_fresh(1000);
  CHECK(gt_set_epoch(SANE_EPOCH, CAL_USER));
  CHECK_EQ(gt_now(), SANE_EPOCH);

  // Ten idle minutes with the CPU stopped, then one sample on the far side.
  host_advance_ms(600000u);
  CHECK_EQ(gt_now(), SANE_EPOCH + 600u);
  CHECK_EQ(gt_elapsed_since(SANE_EPOCH, gt_now()), 600u);

  // And again, from where it left off: the base is not re-seeded on a wake.
  host_advance_ms(1800000u);
  CHECK_EQ(gt_now(), SANE_EPOCH + 2400u);
  CHECK_EQ(gt_elapsed_since(SANE_EPOCH, gt_now()), 2400u);

  // A whole night of eight-second slices adds up to the same second as one
  // eight-hour gap would: the charge is a subtraction, not an accumulation, so
  // it cannot drift with the number of wakes.
  const uint32_t before = gt_now();
  for (uint32_t i = 0; i < 3600u; ++i) host_advance_ms(8000u);
  CHECK_EQ(gt_now(), before + 8u * 3600u);
  gt_test_reset();
}

TEST(clock_a_sleep_gap_that_spans_the_millisecond_wrap_is_still_charged_exactly) {
  // The one case a sleep makes reachable that ordinary running does not: the
  // 2^32 ms boundary crossed with NO sample in between, because the CPU was
  // off. Defence 1's precondition still holds - one gap, far under 49.7 days -
  // and the wrap counter has to catch it from a single observation.
  clock_fresh(0xFFFF0000u);
  CHECK(gt_set_epoch(SANE_EPOCH, CAL_USER));
  CHECK_EQ(gt_now(), SANE_EPOCH);

  // 0xFFFF0000 + 1,800,000 ms, modulo 2^32.
  host_set_ms((uint32_t)(0xFFFF0000u + 1800000u));
  CHECK_EQ(gt_now(), SANE_EPOCH + 1800u);
  CHECK_EQ(gt_elapsed_since(SANE_EPOCH, gt_now()), 1800u);

  // A second gap on the far side of the boundary keeps counting normally.
  host_advance_ms(600000u);
  CHECK_EQ(gt_now(), SANE_EPOCH + 2400u);
  gt_test_reset();
}

// =============================================================================
//  P6-C3: WHICH BOOTS DESCRIBE A GAP
//  core/nt_types.h owns the list app.cpp's boot_absence() branches on. The one
//  that matters is BOOT_DEEPSLEEP: a timed wake is not a restart, and the
//  interval IS elapsed game time that care, box recovery and the cooldown table
//  must all be charged over.
// =============================================================================
TEST(clock_a_deep_sleep_wake_charges_its_gap_and_a_crash_never_does) {
  CHECK(nt_boot_charges_absence(BOOT_DEEPSLEEP));
  CHECK(nt_boot_charges_absence(BOOT_POWER_LOSS));
  CHECK(nt_boot_charges_absence(BOOT_UNKNOWN));

  CHECK(!nt_boot_charges_absence(BOOT_FIRST_RUN));   // nobody to have abandoned
  CHECK(!nt_boot_charges_absence(BOOT_CRASH));       // dizzy, not abandoned
  CHECK(!nt_boot_charges_absence(BOOT_SOFT_RESET));  // a ~0 s gap

  // Total over the enum: every value answers, and exactly three say no.
  uint8_t no = 0;
  for (uint8_t k = 0; k < (uint8_t)BOOT_COUNT; ++k) {
    if (!nt_boot_charges_absence((BootKind)k)) ++no;
  }
  CHECK_EQ(no, 3);
}
