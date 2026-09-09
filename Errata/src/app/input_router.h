// =============================================================================
//  ERRATA - app/input_router.h
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
//    GST_TAP_R      -                              B primary: choose the
//                                                  highlighted row, +1 on the
//                                                  TIME screen, ...
//    GST_HOLD_R     BACK (invariant 1)             only with SF_OWNS_BACK
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
//  WHERE BACK LIVES, AND THE FACT THAT IT HAS MOVED TWICE.
//
//  Until P2-C11d it was a 600 ms hold and B's short press was "select". P2-C11d
//  swapped them, on the argument that section 7 calls B "back / cancel" and a
//  player should not have to learn that the obvious button goes forwards.
//
//  *** THE FIRST HARDWARE SESSION SWAPPED THEM BACK, AND THE OWNER'S REASON
//      BEATS THE PAPER ONE: HE PLAYED IT. ***
//
//  With a thing in your hands the two presses are not symmetric. A tap is the
//  cheap, frequent, low-consequence gesture and a hold is the deliberate one -
//  so the cheap gesture should be the one you make constantly (walking a list
//  and picking a row) and the deliberate one should be the one that throws work
//  away. Under P2-C11d's grammar every confirmation on the device cost a
//  600 ms hold and every accidental brush of B left the screen. It reads fine
//  in a table and it is wrong in a hand.
//
//  WHAT DID NOT MOVE, and each is a real reason rather than an oversight:
//    * The R AUTO-REPEAT on SCR_TIME, SCR_SETUP_NAME and SCR_SETUP_STARTER.
//      There, B is not back or select at all - it is "+1 on the field under the
//      cursor", and holding it repeats. A 46-entry character ring needs that
//      repeat, and all three rows are SF_LOCK_INPUT so the router never reaches
//      them. They have no BACK to swap: A+B skips the question.
//    * The MINIGAME PAUSE stays on GST_HOLD_R (ui/ui.cpp handle_game). That one
//      is a collision fix, not a preference: game_service() feeds the raw press
//      edge of B straight into the game, and in_on_release() emits nothing after
//      a HOLD has fired - so a held B cannot also arrive as a tap. Putting the
//      pause on the tap is exactly the P3-C4a defect where one press both played
//      the game and opened a modal over it.
//    * SCR_HOME. It is the root, there is nowhere to go back TO, and both of B's
//      presses have always been the caress.
//    * The DEV CONSOLE (dev/godmode.cpp) needed no change at all: it has used
//      TAP_R to choose and HOLD_R to leave since it was written, which is some
//      evidence about which way round is natural.
//
//  Choosing is still NOT on TAP_L, and that argument is unchanged: GST_HOLD_L
//  REPEATS every REPEAT_RATE_MS while GST_HOLD_R and both taps fire exactly
//  once. Stepping a cursor is what a repeating gesture is for, and committing is
//  what a one-shot gesture is for.
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
#ifndef ER_INPUT_ROUTER_H
#define ER_INPUT_ROUTER_H

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

#endif  // ER_INPUT_ROUTER_H
