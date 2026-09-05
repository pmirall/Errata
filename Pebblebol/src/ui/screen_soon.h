// =============================================================================
//  PEBBLEBOL - ui/screen_soon.h
//  THE PLACEHOLDER SCREEN, one row per spec section 6 state that has no
//  implementation yet (plan P2-C11d).
//
//  BATTLE LEFT THIS FILE WITH P4-C4. ui/screen_battle.cpp is the real screen
//  now, and soon_battle() was deleted with the row that pointed at it rather
//  than left behind - an unreferenced non-static function raises no warning, so
//  a dead placeholder is a puzzle no gate would ever have failed on.
//
//  WHY THIS EXISTS. Spec section 6 lists the states this device has, and the
//  screen table is the enumeration of them: "every state must define enter,
//  update, render, handleInput, exit". A table with holes in it is not an
//  enumeration - it is a switch with a default branch, which is exactly the
//  shape section 6 forbids. So TRADE, BREED, ITEM_REWARD and
//  SLEEP all still have real rows from today: they can be
//  navigated to, they draw, they take BACK and HELP, and each one says what it
//  is and which phase brings it to life. Nothing here is a stub that silently
//  does nothing; a menu entry that answers with a blank screen is
//  indistinguishable from a broken button.
//
//  PURE translation unit.
// =============================================================================
#ifndef PB_SCREEN_SOON_H
#define PB_SCREEN_SOON_H

#include <stdint.h>

#include "../core/nt_types.h"

// One render hook per not-yet-implemented state. They differ only in the two
// string ids they pass to the shared frame, which is why they are one line
// each and why the table can still name every row explicitly.
void soon_network(void);      // phase 5
void soon_encounter(void);    // phase 5
void soon_capture(void);      // phase 5
void soon_trade(void);        // phase 7
void soon_breed(void);        // phase 7
void soon_item_reward(void);  // phase 6
void soon_sleep(void);        // phase 10

// The row for an id that is an OVERLAY (CONFIRM, ALERT): ui/dialog.cpp draws
// those over whatever screen is up and they never become sm_current(), so this
// hook is unreachable by construction. It exists so the table has no hole.
void soon_generic(void);

// BOTH opens the one-line help; everything else is the router's.
void soon_input(Gesture g);

// The SHARED do-nothing hooks, used by every row - placeholder or not - whose
// state genuinely has nothing to take, run or release. They are what makes
// "all five hooks non-null" cost nothing: one empty function, named once,
// instead of a null pointer the dispatcher has to test on every gesture and
// every frame.
void nop_enter(void);
void nop_update(uint32_t now_ms);
void nop_leave(void);
// Ignores every gesture. BOOT and LOAD_SAVE are not waiting on a button.
void nop_input(Gesture g);

#endif  // PB_SCREEN_SOON_H
