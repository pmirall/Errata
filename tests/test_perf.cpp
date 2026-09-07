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

// =============================================================================
//  THE FRAME-RATE HOLD MUST NOT CANCEL THE PACING IT ASKED FOR
//  Added at the FINAL REVIEW, for a defect that had been shipping since P2.
//
//  ui/render.h tells a caller to RENEW rd_hold_fps() every tick for as long as
//  its film lasts, and both callers do - ui/screen_battle.cpp's battle_update()
//  and ui/actfx.cpp's actfx_service() run on every app_loop() pass.
//  rd_hold_fps() ended in an UNCONDITIONAL rd_request_frame(), which sets the
//  frame deadline to "now", so every renewal cancelled rd_begin_frame()'s
//  "not due yet, skip" gate and the renderer drew on EVERY PASS. Measured
//  against a modelled 28 ms drawing pass: 20 fps requested, 35.7 delivered,
//  frames/passes 1.00 against 0.08 on an idle HOME. The requested VALUE was
//  irrelevant - a 5 fps hold free-ran identically - which is the tell that the
//  rate was not being used at all.
//
//  The consequence is not only frames. app/app.cpp gives its `delay(1)` only
//  `if (!drew && !pin.held)`, so with drew always true the loop ran at 100 %
//  duty cycle in exactly the two states docs/bench.md A1 sits in (audit risk
//  16); and the pass period became the whole frame (~28 ms of I2C) instead of
//  ~2 ms, so audio_service() was called ~36 times a second instead of ~500 and
//  every 40 ms step of SFX_RISE/SFX_FALL lasted ~56 - the five-step SFX_FALL a
//  faint plays runs ~280 ms instead of 200, which is the audible half.
//
//  The decision now lives in core/perf.cpp so that this file can hold it.
//  ui/render.cpp is one of the translation units no host binary compiles, and
//  that is exactly why it was wrong for ten phases.
// =============================================================================
TEST(an_arming_frame_rate_hold_brings_the_frame_forward) {
  // Nothing live: any hold is an arming, whatever rate it asks for. Without
  // this the raise would not take effect until the CURRENT period elapsed -
  // up to 250 ms of the film still played at 4 fps, which is most of a phase.
  CHECK(perf_hold_raises(false, 0u,  (uint8_t)FPS_NORMAL));
  CHECK(perf_hold_raises(false, 60u, 1u));

  // A hold that expired but has not been cleared yet reads as not-live, so
  // re-arming after an expiry is an arming. ui/render.cpp's flag is cleared
  // lazily, so this is the case that keeps a second film from starting slow.
  CHECK(perf_hold_raises(false, (uint8_t)FPS_NORMAL, (uint8_t)FPS_NORMAL));

  // A genuine raise over a live, lower hold.
  CHECK(perf_hold_raises(true, (uint8_t)FPS_LOW, (uint8_t)FPS_NORMAL));
}

TEST(a_renewed_frame_rate_hold_leaves_the_deadline_alone) {
  // THE DEFECT, DIRECTLY. This is what both shipping callers do on every single
  // app_loop() pass for the whole length of a film, and it must be inert.
  CHECK(!perf_hold_raises(true, (uint8_t)FPS_NORMAL, (uint8_t)FPS_NORMAL));

  // Renewing at a LOWER rate is not a raise either - rd_fps() takes the higher
  // of the request and the live hold, so there is still nothing to bring
  // forward.
  CHECK(!perf_hold_raises(true, (uint8_t)FPS_NORMAL, (uint8_t)FPS_LOW));

  // A disarm asks for nothing and must never request a frame.
  CHECK(!perf_hold_raises(false, 0u, 0u));
  CHECK(!perf_hold_raises(true, (uint8_t)FPS_NORMAL, 0u));
}

TEST(the_hold_rule_and_the_deadline_together_pace_a_whole_film) {
  // The two halves of the pacing, composed the way rd_hold_fps() and
  // rd_begin_frame() compose them, over a film renewed on every pass. The
  // model is app_loop()'s: a 2 ms pass that skips, a 28 ms pass that draws.
  //
  // With the defect (`raises` forced true) this loop draws on all 500 passes.
  const uint32_t period = 1000u / (uint32_t)FPS_NORMAL;
  uint32_t now  = 0u;
  uint32_t next = 0u;            // armed: a frame is due immediately
  bool     live = false;
  uint8_t  live_fps = 0u;
  int      frames = 0;

  for (int pass = 0; pass < 500; ++pass) {
    // Every pass renews the hold, exactly as actfx_service() does.
    if (perf_hold_raises(live, live_fps, (uint8_t)FPS_NORMAL)) next = now;
    live = true;
    live_fps = (uint8_t)FPS_NORMAL;

    if ((int32_t)(now - next) >= 0) {
      ++frames;
      next = perf_advance_deadline(now, next, period);
      now += 28u;                // a drawing pass costs a whole sendBuffer()
    } else {
      now += 2u;                 // an idle pass
    }
  }

  // 500 passes spanning `now` ms; the film must have been paced at the rate it
  // asked for and not at the loop rate. One frame per period, within one.
  const int want = (int)(now / period);
  if (frames > want + 1 || frames < want - 1)
    fprintf(stderr, "  %d frames over %u ms at %u fps: expected about %d\n",
            frames, (unsigned)now, (unsigned)FPS_NORMAL, want);
  CHECK(frames <= want + 1);
  CHECK(frames >= want - 1);
  CHECK(frames < 500);           // the defect draws on every pass
}
