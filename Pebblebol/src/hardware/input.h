// =============================================================================
//  PEBBLEBOL - hardware/input.h
//  Debounce + gesture recogniser for the two buttons.
//  No screen knowledge, no game logic, no allocation.
//
//  Emitted gestures (nt_types.h, enum Gesture):
//      GST_TAP_L  GST_TAP_R  GST_DBL_L  GST_DBL_R
//      GST_HOLD_L (repeating)  GST_HOLD_R (once)  GST_BOTH  GST_LONG_BOTH
//
//  Timing constants all come from config.h section 5:
//      DEBOUNCE_MS 25 / DOUBLE_TAP_WINDOW_MS 280 / HOLD_MS 600 / TAP_MAX_MS 599
//      LONG_BOTH_MS 1500 / BOTH_SYNC_MS 80 / REPEAT_START_MS 600
//      REPEAT_RATE_MS 220 / INPUT_POLL_MS 5
//
//  This header is host-compilable: it pulls in nt_types.h only.
// =============================================================================
#ifndef NT_INPUT_H
#define NT_INPUT_H

#include "../core/nt_types.h"

// Button indices used by input_raw() / input_hold_ms(). NOT GPIO numbers.
#define INPUT_BTN_L   0
#define INPUT_BTN_R   1
#define INPUT_BTN_N   2

// -----------------------------------------------------------------------------
// Public interface
// -----------------------------------------------------------------------------

// Configure PIN_BTN_L / PIN_BTN_R as INPUT_PULLUP and seed the debouncer from
// the current pin levels, so a button already held at boot never synthesises a
// press. Call once from setup(), before the first input_poll().
void input_begin(void);

// Sample, debounce, run the recogniser and return AT MOST ONE gesture.
// Returns GST_NONE when nothing is ready. Gestures produced in the same
// sampling instant are queued and handed out one per call, in order.
// Non-blocking. Call it every INPUT_POLL_MS (5 ms) or faster; polling slower
// than DEBOUNCE_MS (25 ms) can miss a very short press entirely.
Gesture input_poll(void);

// Debounced pressed state of one button. which = INPUT_BTN_L / INPUT_BTN_R.
// true == physically down (the pin reads BTN_ACTIVE_LEVEL). Out of range -> false.
bool input_raw(uint8_t which);

// -----------------------------------------------------------------------------
// Extensions beyond the core interface above (additive; nothing above changes).
// Needed because the god-mode entry keys off a hold DURATION, not off a
// gesture: BOTH held GOD_ENTER_HOLD_MS (5000 ms) on the STATUS_B screen. It
// needs a progress bar, i.e. a live "how long has it been down" query.
// -----------------------------------------------------------------------------

// Milliseconds since the debounced press of one button; 0 if it is not down.
// For "both held for N ms" use the smaller of the two values.
uint32_t input_hold_ms(uint8_t which);

// Drop every queued gesture and swallow the presses that are in flight right
// now: nothing more is emitted from the current button episode until BOTH
// buttons have been released. Use it after a screen transition that was
// triggered by a hold duration rather than by a gesture (god-mode entry), so
// the eventual release cannot fire a stale gesture on the new screen.
void input_flush(void);

#endif // NT_INPUT_H
