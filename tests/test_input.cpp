// =============================================================================
//  Errata host tests - test_input.cpp
//  Scripted button waveforms through the input.cpp recogniser: tap,
//  double tap, hold-repeat, chord, long chord, the boot-held swallow, and the
//  "no phantom TAP before BOTH" guarantee. The fake clock advances 1 ms per
//  poll, which is finer than INPUT_POLL_MS, so every timing below is exact.
// =============================================================================
#include "nt_test.h"

#include "core/config.h"
#include "hardware/input.h"
#include "host_shims.h"

#define GOT_MAX 32
static Gesture s_got[GOT_MAX];
static int     s_n;

static void fresh(void) {
  host_reset();
  input_begin();
  s_n = 0;
}

// Advances `ms` milliseconds one poll at a time, collecting every gesture.
static void run_ms(uint32_t ms) {
  for (uint32_t i = 0; i < ms; i++) {
    host_advance_ms(1);
    const Gesture g = input_poll();
    if (g != GST_NONE && s_n < GOT_MAX) s_got[s_n++] = g;
  }
}

static void press(int idx)   { host_set_button(idx, true); }
static void release(int idx) { host_set_button(idx, false); }

static int count_of(Gesture g) {
  int n = 0;
  for (int i = 0; i < s_n; i++) if (s_got[i] == g) n++;
  return n;
}

// THE LATENCY TEST (P3-C4a). A TAP must land at RELEASE, not a double-tap
// window later. The old recogniser took DOUBLE_TAP_WINDOW_MS (280 ms) to
// classify every single press in the product; the acceptance number is 30 ms,
// which is one debounce interval (25 ms) plus a poll.
TEST(input_tap_left_lands_within_30ms_of_release) {
  fresh();
  press(INPUT_BTN_L);
  run_ms(100);
  CHECK_EQ(s_n, 0);                          // still nothing on the press edge
  release(INPUT_BTN_L);
  run_ms(30);
  CHECK_EQ(s_n, 1);                          // ... and everything by 30 ms after
  CHECK_EQ(s_got[0], GST_TAP_L);
  run_ms(1000);
  CHECK_EQ(s_n, 1);                          // and nothing else, ever
}

TEST(input_tap_right) {
  fresh();
  press(INPUT_BTN_R);
  run_ms(80);
  release(INPUT_BTN_R);
  run_ms(500);
  CHECK_EQ(s_n, 1);
  CHECK_EQ(s_got[0], GST_TAP_R);
}

TEST(input_two_taps_in_a_row_keep_their_order) {
  fresh();
  press(INPUT_BTN_L);  run_ms(60);  release(INPUT_BTN_L);  run_ms(400);
  press(INPUT_BTN_R);  run_ms(60);  release(INPUT_BTN_R);  run_ms(400);
  CHECK_EQ(s_n, 2);
  CHECK_EQ(s_got[0], GST_TAP_L);
  CHECK_EQ(s_got[1], GST_TAP_R);
}

// There is no double tap any more (P3-C4a). Two quick presses of the same
// button are simply two taps - which is the behaviour the six list screens
// that used to own a "jump to first/last" shortcut now rely on, and the reason
// a fast double press no longer swallows the first press.
TEST(input_two_quick_presses_are_two_taps_not_one_shortcut) {
  fresh();
  press(INPUT_BTN_L);  run_ms(60);  release(INPUT_BTN_L);
  run_ms(100);                               // what used to be "inside the window"
  press(INPUT_BTN_L);  run_ms(60);  release(INPUT_BTN_L);
  run_ms(800);
  CHECK_EQ(s_n, 2);
  CHECK_EQ(s_got[0], GST_TAP_L);
  CHECK_EQ(s_got[1], GST_TAP_L);
  CHECK_EQ(count_of(GST_TAP_L), 2);
}

// The press EDGE seam the minigames run on. It reports the physical press, is
// consumed by the read, and is independent of the gesture queue.
TEST(input_pressed_edge_reports_each_press_exactly_once) {
  fresh();
  CHECK(!input_pressed_edge(INPUT_BTN_L));   // nothing yet
  press(INPUT_BTN_L);
  run_ms(30);
  CHECK(input_pressed_edge(INPUT_BTN_L));    // the press is visible...
  CHECK(!input_pressed_edge(INPUT_BTN_L));   // ...exactly once
  CHECK(!input_pressed_edge(INPUT_BTN_R));   // and never on the other button
  release(INPUT_BTN_L);
  run_ms(200);
  CHECK(!input_pressed_edge(INPUT_BTN_L));   // a RELEASE is not a press edge
  CHECK_EQ(count_of(GST_TAP_L), 1);          // the gesture still came out too

  // It survives a press that the recogniser goes on to swallow: a chord is
  // still two physical presses, and a game must see both.
  fresh();
  press(INPUT_BTN_L);  run_ms(10);  press(INPUT_BTN_R);  run_ms(30);
  CHECK(input_pressed_edge(INPUT_BTN_L));
  CHECK(input_pressed_edge(INPUT_BTN_R));

  // input_flush() drops the ones in flight, so a game cannot inherit a press
  // from before it started.
  fresh();
  press(INPUT_BTN_R);
  run_ms(30);
  input_flush();
  CHECK(!input_pressed_edge(INPUT_BTN_R));
  CHECK_EQ(input_pressed_edge(9), false);    // out of range
}

TEST(input_hold_left_fires_once_then_repeats) {
  fresh();
  press(INPUT_BTN_L);
  // The press edge is stamped by the first poll that sees it, so after
  // HOLD_MS polls the button has been down for HOLD_MS - 1 ms.
  run_ms(HOLD_MS);
  CHECK_EQ(s_n, 0);                          // not yet a hold
  run_ms(1);
  CHECK_EQ(s_n, 1);                          // exactly HOLD_MS after the edge
  CHECK_EQ(s_got[0], GST_HOLD_L);
  run_ms(1500 - HOLD_MS - 1);                // 1500 polls: held for 1499 ms
  // Repeats every REPEAT_RATE_MS after REPEAT_START_MS: 820, 1040, 1260, 1480.
  CHECK_EQ(s_n, 1 + (1499 - REPEAT_START_MS) / REPEAT_RATE_MS);
  CHECK_EQ(count_of(GST_HOLD_L), s_n);
  release(INPUT_BTN_L);
  const int before = s_n;
  run_ms(1000);
  CHECK_EQ(s_n, before);                     // release is silent: no TAP_L
}

TEST(input_hold_right_fires_exactly_once) {
  fresh();
  press(INPUT_BTN_R);
  run_ms(2500);
  CHECK_EQ(s_n, 1);
  CHECK_EQ(s_got[0], GST_HOLD_R);
  release(INPUT_BTN_R);
  run_ms(1000);
  CHECK_EQ(s_n, 1);
}

TEST(input_chord_emits_both_with_no_phantom_tap) {
  fresh();
  press(INPUT_BTN_L);
  run_ms(30);                                // inside BOTH_SYNC_MS
  CHECK_EQ(s_n, 0);                          // the first button produced nothing
  press(INPUT_BTN_R);
  run_ms(300);
  CHECK_EQ(s_n, 0);                          // BOTH comes on release
  release(INPUT_BTN_L);
  run_ms(50);
  release(INPUT_BTN_R);
  run_ms(1000);
  CHECK_EQ(s_n, 1);
  CHECK_EQ(s_got[0], GST_BOTH);
  CHECK_EQ(count_of(GST_TAP_L), 0);
  CHECK_EQ(count_of(GST_TAP_R), 0);
}

TEST(input_long_chord_emits_long_both_once) {
  fresh();
  press(INPUT_BTN_L);
  run_ms(20);
  press(INPUT_BTN_R);
  run_ms(LONG_BOTH_MS - 10);
  CHECK_EQ(s_n, 0);
  run_ms(20);
  CHECK_EQ(s_n, 1);
  CHECK_EQ(s_got[0], GST_LONG_BOTH);
  run_ms(2000);
  CHECK_EQ(s_n, 1);                          // no repeat
  release(INPUT_BTN_R);
  release(INPUT_BTN_L);
  run_ms(1000);
  CHECK_EQ(s_n, 1);                          // release is silent: no BOTH, no TAP
}

TEST(input_second_button_too_late_swallows_the_episode) {
  fresh();
  press(INPUT_BTN_L);
  run_ms(300);                               // > BOTH_SYNC_MS, < HOLD_MS
  press(INPUT_BTN_R);
  run_ms(1000);
  release(INPUT_BTN_L);
  release(INPUT_BTN_R);
  run_ms(1000);
  CHECK_EQ(s_n, 0);                          // no TAP, no HOLD, no BOTH
}

TEST(input_hold_then_other_button_emits_no_tap) {
  fresh();
  press(INPUT_BTN_L);
  run_ms(700);
  CHECK_EQ(s_n, 1);
  CHECK_EQ(s_got[0], GST_HOLD_L);
  press(INPUT_BTN_R);
  run_ms(1000);                              // repeats stop once the episode is swallowed
  release(INPUT_BTN_R);
  release(INPUT_BTN_L);
  run_ms(1000);
  CHECK_EQ(s_n, 1);
  CHECK_EQ(count_of(GST_TAP_R), 0);
  CHECK_EQ(count_of(GST_BOTH), 0);
}

TEST(input_button_held_through_boot_is_swallowed) {
  host_reset();
  press(INPUT_BTN_L);                        // held before input_begin()
  input_begin();
  s_n = 0;
  run_ms(3000);
  CHECK_EQ(s_n, 0);                          // no HOLD_L, no TAP_L
  release(INPUT_BTN_L);
  run_ms(1000);
  CHECK_EQ(s_n, 0);
  // The recogniser is healthy afterwards.
  press(INPUT_BTN_L);  run_ms(60);  release(INPUT_BTN_L);  run_ms(500);
  CHECK_EQ(s_n, 1);
  CHECK_EQ(s_got[0], GST_TAP_L);
}

TEST(input_glitch_shorter_than_debounce_is_ignored) {
  fresh();
  press(INPUT_BTN_L);
  run_ms(DEBOUNCE_MS - 15);
  release(INPUT_BTN_L);
  run_ms(1000);
  CHECK_EQ(s_n, 0);
  CHECK(!input_raw(INPUT_BTN_L));
}

TEST(input_flush_silences_the_current_episode) {
  fresh();
  press(INPUT_BTN_L);
  run_ms(700);
  CHECK_EQ(s_n, 1);
  input_flush();
  run_ms(1000);
  CHECK_EQ(s_n, 1);                          // no more repeats
  release(INPUT_BTN_L);
  run_ms(1000);
  CHECK_EQ(s_n, 1);                          // no TAP on release
  press(INPUT_BTN_R);  run_ms(60);  release(INPUT_BTN_R);  run_ms(500);
  CHECK_EQ(s_n, 2);
  CHECK_EQ(s_got[1], GST_TAP_R);
}

TEST(input_raw_and_hold_ms_track_the_debounced_level) {
  fresh();
  CHECK(!input_raw(INPUT_BTN_L));
  CHECK(!input_raw(INPUT_BTN_R));
  CHECK_EQ(input_hold_ms(INPUT_BTN_L), 0);
  CHECK(!input_raw(INPUT_BTN_N));            // out of range -> false
  CHECK_EQ(input_hold_ms(INPUT_BTN_N), 0);

  press(INPUT_BTN_R);
  run_ms(DEBOUNCE_MS - 5);
  CHECK(!input_raw(INPUT_BTN_R));            // still inside the debounce window
  run_ms(10);
  CHECK(input_raw(INPUT_BTN_R));
  run_ms(100);
  CHECK_NEAR(input_hold_ms(INPUT_BTN_R), DEBOUNCE_MS + 5 + 100, 2);
  release(INPUT_BTN_R);
  run_ms(DEBOUNCE_MS + 5);
  CHECK(!input_raw(INPUT_BTN_R));
  CHECK_EQ(input_hold_ms(INPUT_BTN_R), 0);
}
