// =============================================================================
//  PEBBLEBOL - app/input_router.h
//  THE SPEC SECTION 7 INPUT GRAMMAR, in one place (plan P2-C11d).
//
//  Section 7 gives this device two buttons and four rules:
//
//      A   primary action / next / confirm
//      B   back / cancel / secondary action
//      short press, long press, both together
//      nothing may need a third input
//
//  The router is the half of that which is the SAME on every screen. It runs
//  before the current screen's input hook and consumes the gestures that mean
//  the same thing everywhere; whatever it does not consume reaches the screen.
//
//    gesture        router                         screen
//    ------------------------------------------------------------------------
//    GST_TAP_L      -                              A: step a cursor, or the
//                                                  primary action where there
//                                                  is no cursor
//    GST_HOLD_L     -                              the auto-repeat of TAP_L
//    GST_TAP_R      BACK (invariant 1)             only with SF_OWNS_BACK
//    GST_HOLD_R     -                              B secondary: choose the
//                                                  highlighted row, +1 on the
//                                                  TIME screen, ...
//    GST_BOTH       -                              help (the screen owns the
//                                                  topic, so only it can)
//    GST_LONG_BOTH  HOME (invariant 2)             only on HOME itself
//
//  P3-C4a DECIDED THE FATE OF GST_DBL_L/R: they are gone. They bought a
//  "jump to first / last" on six list screens and cost 280 ms of latency on
//  EVERY press in the product, because a tap could not be classified until the
//  double-tap window had expired with no second press. A TAP now lands at
//  RELEASE (~25 ms). Every list already wraps, so first and last are still a
//  short walk with A held; the two shortcuts that were not list jumps moved
//  (STATUS's genome hex to HOLD_R) or went with their only caller (the menu's
//  repeat-last-action).
//
//  WHY BACK MOVED FROM HOLD_R TO TAP_R. Until P2-C11d, B's short press was
//  "select" and BACK was a 600 ms hold - the opposite of section 7, and the
//  reason every list needed the player to learn that the obvious button went
//  forwards. Now B always cancels on a tap. Choosing moved to HOLD_R rather
//  than to TAP_L because GST_HOLD_L REPEATS every REPEAT_RATE_MS and GST_HOLD_R
//  fires exactly once: a repeating "choose" would commit again, on the screen
//  it had just navigated to, 220 ms later. Stepping a cursor is what a
//  repeating gesture is for, and committing is what a one-shot gesture is for.
//
//  THREE ESCAPES, all declared in the screen's own table row (ui/screen.h):
//    SF_LOCK_INPUT  the row owns EVERY gesture (BOOT, LOAD_SAVE, ERROR, TIME,
//                   DIAG). The router applies nothing at all.
//    SF_OWNS_BACK   the row handles B itself (BOX walks back through its own
//                   modes, SETTINGS closes its info page, EVOLUTION rubs the
//                   egg with both buttons, GAME must not be left by accident).
//    SCR_HOME       the root. There is nowhere to go back TO, so B stays the
//                   caress it has always been.
//
//  PURE translation unit: it speaks to the state machine and the table, and to
//  no hardware at all.
// =============================================================================
#ifndef PB_INPUT_ROUTER_H
#define PB_INPUT_ROUTER_H

#include <stdint.h>

#include "../core/nt_types.h"

// Apply the global grammar and then dispatch to the current screen. Returns
// true when the gesture was consumed by either half; false only for GST_NONE
// or when the current row has no input hook, which the caller may treat as
// "nothing happened".
bool router_handle(Gesture g);

// The global half on its own, for the tests: true when the router consumed the
// gesture and the screen must not see it.
bool router_global(Gesture g);

#endif  // PB_INPUT_ROUTER_H
