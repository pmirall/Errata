// =============================================================================
//  Pebblebol host tests - test_perf.cpp
//  core/perf.cpp: THE PERFORMANCE INSTRUMENT'S ARITHMETIC (P10-C2, spec 46).
//
//  READ THIS BEFORE ADDING A CASE. NOTHING IN THIS FILE MEASURES TIME AND
//  NOTHING IN THIS FILE MAY. Every stamp below is a number this file made up.
//  What is under test is the arithmetic between a pair of micros() stamps and
//  the figure a bench operator reads off a board: the wrap, the saturation, the
//  discard, the overrun boundary, the per-screen billing and the render
//  scheduler's deadline. The microseconds themselves come from ui/render.cpp
//  and app/app.cpp, which NO host binary compiles, over an I2C bus and a panel
//  that do not exist here - see the refusal written into core/perf.h.
//
//  A case in this file that asserted `worst < FRAME_BUDGET_US` over stamps this
//  file wrote would be the exact defect this repository keeps finding: an
//  assertion that can fail, checking the wrong thing. docs/bench.md is where
//  the five section 46 thresholds are answered, on a board, by a person.
// =============================================================================
#include "nt_test.h"

#include "core/perf.h"

// The two screens used below are real ids, so an index bug shows up as a
// neighbour's number rather than as a segfault nobody can read.
#define SCR_A ((uint8_t)SCR_HOME)
#define SCR_B ((uint8_t)SCR_BATTLE)

static void begin(uint8_t scr) {
  perf_reset();
  perf_set_screen(scr);
}

// =============================================================================
//  1. THE SPAN: WRAP, SATURATION, DISCARD
// =============================================================================

// micros() is a 32-bit microsecond counter, so it wraps every 71.6 minutes and
// a frame that straddles the wrap is an ordinary frame on a board that has been
// on for an hour.
//
// AND THE OBVIOUS MUTATION DOES NOT FIRE, WHICH IS WORTH RECORDING RATHER THAN
// DRESSING UP. Casting the subtraction through int32_t is BIT-IDENTICAL - two's
// complement subtraction gives the same 32 bits either way - so it is not a
// defect and this case is right to pass over it. The two forms that ARE wrong
// are the ones a careful author actually writes, and both were run: computing
// the span in int64_t and discarding a negative result, and refusing a pair
// whose `end` is numerically below its `begin`. Each fails this case with all
// five of its checks.
TEST(a_span_that_straddles_the_micros_wrap_is_the_true_elapsed_time) {
  begin(SCR_A);
  const uint32_t begin_us = 0xFFFFF000u;      // 4,096 us before the wrap
  const uint32_t end_us   = 0x00000100u;      //   256 us after it
  perf_note_frame(begin_us, end_us);
  CHECK_EQ((int)perf_frames(), 1);
  CHECK_EQ((int)perf_discards(), 0);
  CHECK_EQ((int)perf_frame_worst_us(), 4352);          // 4096 + 256
  CHECK_EQ((int)perf_frame_max_us(SCR_A), 4352);

  // And the same for a pass, because they share span_of() and a future edit
  // that only fixed one of them would leave the other lying.
  perf_note_pass(begin_us, end_us);
  CHECK_EQ((int)perf_pass_worst_us(), 4352);
}

// A stamp pair wider than one wrap can explain is a BAD PAIR, not a slow frame.
// It is dropped - and COUNTED, because a silently dropped sample is the phase-7
// defect (a fixture that bound a null clock and turned a whole subsystem off).
TEST(an_impossible_span_is_discarded_and_counted_rather_than_swallowed) {
  begin(SCR_A);
  perf_note_frame(0u, (uint32_t)PERF_SANE_MAX_US + 1u);
  CHECK_EQ((int)perf_frames(), 0);            // it was not recorded...
  CHECK_EQ((int)perf_frame_worst_us(), 0);
  CHECK_EQ((int)perf_discards(), 1);          // ...and it was not hidden either

  perf_note_pass(0u, (uint32_t)PERF_SANE_MAX_US + 1u);
  CHECK_EQ((int)perf_passes(), 0);
  CHECK_EQ((int)perf_discards(), 2);

  // Exactly PERF_SANE_MAX_US is still a span: the boundary is not a discard.
  perf_note_frame(0u, (uint32_t)PERF_SANE_MAX_US);
  CHECK_EQ((int)perf_frames(), 1);
  CHECK_EQ((int)perf_discards(), 2);
}

// The per-screen figure is a uint16, chosen because 65,535 us is ABOVE the
// 50,000 us budget so saturation can never hide an overrun. It CAN hide how bad
// a bad frame was, so the saturation is a flag of its own and the all-time
// worst keeps its full width.
TEST(the_per_screen_maximum_saturates_and_says_so_while_the_worst_keeps_its_width) {
  begin(SCR_A);
  perf_note_frame(0u, 40000u);
  CHECK_EQ((int)perf_frame_max_us(SCR_A), 40000);
  CHECK_EQ((int)perf_frame_saturated(), 0);

  perf_note_frame(0u, 900000u);               // 0.9 s: a genuinely bad frame
  CHECK_EQ((int)perf_frame_max_us(SCR_A), 65535);
  CHECK_EQ((int)perf_frame_saturated(), 1);
  CHECK_EQ((int)perf_frame_worst_us(), 900000);   // not truncated here
}

// =============================================================================
//  2. THE OVERRUN BOUNDARY
//
//  ui/render.cpp's live scheduler backoff and this counter must agree about
//  what an overrun IS, because the backoff is what stops the loop running at
//  100 % duty cycle and the counter is what a bench operator reads. Both are
//  STRICTLY GREATER now, and both come out of core/perf.cpp - which is what
//  makes "they agree" a fact rather than a hope.
// =============================================================================
TEST(a_frame_of_exactly_the_budget_is_not_an_overrun_in_either_place) {
  begin(SCR_A);
  perf_note_frame(0u, (uint32_t)FRAME_BUDGET_US);
  CHECK_EQ((int)perf_frame_overruns(), 0);
  CHECK_EQ((int)perf_overrun_backoff_ms((uint32_t)FRAME_BUDGET_US), 0);

  perf_note_frame(0u, (uint32_t)FRAME_BUDGET_US + 1u);
  CHECK_EQ((int)perf_frame_overruns(), 1);

  begin(SCR_A);
  perf_note_pass(0u, (uint32_t)PERF_PASS_BUDGET_US);
  CHECK_EQ((int)perf_pass_overruns(), 0);
  perf_note_pass(0u, (uint32_t)PERF_PASS_BUDGET_US + 1u);
  CHECK_EQ((int)perf_pass_overruns(), 1);
}

// =============================================================================
//  3. PER-SCREEN BILLING
// =============================================================================

// A maximum SINCE BOOT and not the last frame: a number that flickers twenty
// times a second cannot be read off a 128x64 panel, and the bench walk is one
// pass over nineteen screens followed by one look at the page.
TEST(each_screen_keeps_its_own_maximum_and_a_later_smaller_frame_does_not_erase_it) {
  begin(SCR_A);
  perf_note_frame(0u, 31000u);
  perf_set_screen(SCR_B);
  perf_note_frame(0u, 47000u);
  perf_set_screen(SCR_A);
  perf_note_frame(0u,  9000u);        // a cheap frame back on the first screen

  CHECK_EQ((int)perf_frame_max_us(SCR_A), 31000);
  CHECK_EQ((int)perf_frame_max_us(SCR_B), 47000);
  CHECK_EQ((int)perf_frame_worst_us(), 47000);
  CHECK_EQ((int)perf_frame_worst_screen(), (int)SCR_B);
  CHECK_EQ((int)perf_frames(), 3);
  // Every other screen is untouched, which is what makes a walk over the whole
  // table readable: a screen nobody visited reads 0, honestly.
  CHECK_EQ((int)perf_frame_max_us((uint8_t)SCR_MENU), 0);
}

// The worst PASS carries the screen that was up when it happened, because
// "which screen was the device on when the loop stalled" is the whole question.
TEST(the_worst_pass_names_the_screen_that_was_up) {
  begin(SCR_A);
  perf_note_pass(0u, 5000u);
  perf_set_screen(SCR_B);
  perf_note_pass(0u, 120000u);
  perf_set_screen(SCR_A);
  perf_note_pass(0u, 6000u);
  CHECK_EQ((int)perf_pass_worst_us(), 120000);
  CHECK_EQ((int)perf_pass_worst_screen(), (int)SCR_B);
  CHECK_EQ((int)perf_pass_overruns(), 1);
  CHECK_EQ((int)perf_passes(), 3);
}

// A screen id out of range must not scribble past the array. It is CLAMPED,
// not ignored, so the frames still land somewhere a reader can see.
TEST(an_out_of_range_screen_id_is_clamped_and_reading_one_answers_zero) {
  begin(SCR_A);
  perf_set_screen(0xFF);
  CHECK_EQ((int)perf_screen(), (int)SCR_COUNT - 1);
  perf_note_frame(0u, 12000u);
  CHECK_EQ((int)perf_frame_max_us((uint8_t)(SCR_COUNT - 1)), 12000);
  CHECK_EQ((int)perf_frame_max_us((uint8_t)SCR_COUNT), 0);
  CHECK_EQ((int)perf_frame_max_us(0xFF), 0);
}

TEST(reset_clears_every_recorded_span_but_not_the_screen_the_device_is_showing) {
  begin(SCR_B);
  perf_note_frame(0u, 44000u);
  perf_note_pass(0u, 90000u);
  perf_note_frame(0u, (uint32_t)PERF_SANE_MAX_US + 1u);
  CHECK_EQ((int)perf_discards(), 1);

  perf_reset();
  CHECK_EQ((int)perf_frames(), 0);
  CHECK_EQ((int)perf_passes(), 0);
  CHECK_EQ((int)perf_discards(), 0);
  CHECK_EQ((int)perf_frame_worst_us(), 0);
  CHECK_EQ((int)perf_pass_worst_us(), 0);
  CHECK_EQ((int)perf_frame_saturated(), 0);
  for (uint8_t i = 0; i < (uint8_t)SCR_COUNT; ++i) CHECK_EQ((int)perf_frame_max_us(i), 0);
  // A reset asked for from the console must not make the next frame arrive on
  // a screen the device is not showing.
  CHECK_EQ((int)perf_screen(), (int)SCR_B);
}

// =============================================================================
//  4. THE RENDER SCHEDULER'S ARITHMETIC
//
//  This is ui/render.cpp's rd_begin_frame() and rd_end_frame(), which have
//  shipped since 8aff64f (P2-C8) WITH NO TEST OF ANY KIND because render.cpp is
//  compiled by no host binary. It is also the second of the two links in spec
//  section 46's "input-to-render latency <= 2 frames": the "not due yet, skip"
//  gate is what costs the second frame. The first link - press to recognised
//  gesture - is tests/test_input.cpp's input_tap_left_lands_within_30ms_of_release.
// =============================================================================
TEST(the_frame_deadline_advances_by_one_period_and_does_not_drift) {
  // Four frames arriving a millisecond late each: the deadline must stay on
  // the 50 ms grid rather than sliding by a millisecond a frame.
  uint32_t next = 1000u;
  const uint32_t period = 50u;
  uint32_t now = 1001u;
  for (int i = 0; i < 4; ++i) {
    next = perf_advance_deadline(now, next, period);
    now  = next + 1u;
  }
  CHECK_EQ((int)next, (int)(1000u + 4u * 50u));
}

// A 2,000-step offline catch-up can eat several seconds. Firing every frame
// that was missed afterwards would spend those seconds again.
TEST(a_deadline_more_than_one_period_late_resynchronises_instead_of_bursting) {
  const uint32_t period = 50u;
  // One period late exactly: still on the grid, no resync.
  CHECK_EQ((int)perf_advance_deadline(1100u, 1000u, period), 1050);
  // Just over one period late past the ADVANCED deadline: resync to now+period.
  CHECK_EQ((int)perf_advance_deadline(1101u, 1000u, period), 1151);
  // Five seconds late: one frame, now, not a hundred.
  CHECK_EQ((int)perf_advance_deadline(6000u, 1000u, period), 6050);
}

TEST(the_deadline_is_correct_across_the_millis_wrap) {
  const uint32_t period = 50u;
  // next is 20 ms before the wrap; now is 10 ms before it. On the grid.
  CHECK_EQ((unsigned)perf_advance_deadline(0xFFFFFFF6u, 0xFFFFFFECu, period),
           (unsigned)(0xFFFFFFECu + 50u));
  // And far past it, so the resync branch runs across the wrap too.
  CHECK_EQ((unsigned)perf_advance_deadline(0x00000064u, 0xFFFFFFECu, period),
           (unsigned)(0x00000064u + 50u));
}

// The backoff is what stops the renderer running the loop at 100 % duty cycle.
// It is capped, because an eight-second frame must not stop the screen for
// eight seconds as well.
TEST(the_overrun_backoff_is_the_overrun_in_milliseconds_and_it_is_capped) {
  CHECK_EQ((int)perf_overrun_backoff_ms(0u), 0);
  CHECK_EQ((int)perf_overrun_backoff_ms((uint32_t)FRAME_BUDGET_US), 0);
  CHECK_EQ((int)perf_overrun_backoff_ms((uint32_t)FRAME_BUDGET_US + 999u), 0);
  CHECK_EQ((int)perf_overrun_backoff_ms((uint32_t)FRAME_BUDGET_US + 1000u), 1);
  CHECK_EQ((int)perf_overrun_backoff_ms((uint32_t)FRAME_BUDGET_US + 37000u), 37);
  CHECK_EQ((int)perf_overrun_backoff_ms((uint32_t)FRAME_BUDGET_US + 8000000u),
           (int)FRAME_BACKOFF_MAX_MS);
}

// =============================================================================
//  5. THE ONE PROPERTY THAT IS ABOUT THE BENCH RATHER THAN THE ARITHMETIC
//
//  The per-screen array is indexed by ScreenId, and the console page and the
//  DIAG,perf line both print an id. If SCR_COUNT and the array ever disagree
//  the numbers would be attributed to the wrong screen on a board, silently,
//  and no other case here would notice.
// =============================================================================
TEST(every_screen_id_has_a_slot_and_a_frame_billed_to_it_comes_back_from_it) {
  perf_reset();
  for (uint8_t i = 0; i < (uint8_t)SCR_COUNT; ++i) {
    perf_set_screen(i);
    CHECK_EQ((int)perf_screen(), (int)i);
    perf_note_frame(0u, (uint32_t)(1000u + i));
  }
  for (uint8_t i = 0; i < (uint8_t)SCR_COUNT; ++i) {
    if (perf_frame_max_us(i) != (uint16_t)(1000u + i))
      fprintf(stderr, "  screen %u: %u, expected %u\n", (unsigned)i,
              (unsigned)perf_frame_max_us(i), (unsigned)(1000u + i));
    CHECK_EQ((int)perf_frame_max_us(i), (int)(1000u + i));
  }
  CHECK_EQ((int)perf_frames(), (int)SCR_COUNT);
}
