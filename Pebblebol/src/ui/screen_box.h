// =============================================================================
//  PEBBLEBOL - ui/screen_box.h
//  THE BOX SCREEN (spec section 9, plan P2-C11d). Ten slots on the shared
//  vertical list, and the six things section 9 says you can do with one:
//  inspect, select the active Pebble, swap, release, trade, breed.
//
//  It reads game/box.h directly - box_count(), box_peek(), box_active() are
//  pure queries over the single live GameState, and copying them through
//  another view would only add a place for the two to disagree. Everything
//  that WRITES goes through the ui.cpp seams (ui_box_activate / ui_box_swap /
//  ui_box_release), because a write needs the simulation (sim_switch) and
//  flash, and neither may be linked into a pure translation unit.
//
//  FOUR MODES, one screen. They are not "global boolean spaghetti" (spec
//  section 6): they are this state's internal cursor, they never survive
//  box_enter(), and the navigation stack never sees them - B walks back
//  through them one at a time, which is why the row carries SF_OWNS_BACK.
//  The ladder is LIST -> ACTIONS -> {SWAP, CARD}: B leaves SWAP or CARD for the
//  action list it was opened from, leaves ACTIONS for the ten slots, and only
//  from the slot list does it leave the screen.
//
//  PURE translation unit.
// =============================================================================
#ifndef PB_SCREEN_BOX_H
#define PB_SCREEN_BOX_H

#include <stdint.h>

#include "../core/nt_types.h"

// The per-slot action list, in the order spec section 9 lists them. Public so
// the tests can drive it by name.
enum BoxAction : uint8_t {
  BOXA_VIEW = 0,
  BOXA_ACTIVATE,
  BOXA_SWAP,
  BOXA_RELEASE,
  BOXA_TRADE,        // stubbed until P7-C2
  BOXA_BREED,        // stubbed until P7-C2
  BOXA_BACK,
  BOXA_COUNT
};

// Which of the four modes the screen is in, for the tests.
enum BoxMode : uint8_t {
  BOXM_LIST = 0,     // the ten slots
  BOXM_ACTIONS,      // what to do with the chosen one
  BOXM_SWAP,         // pick the slot to exchange it with
  BOXM_CARD          // the stored Pebble's own card
};

void    box_enter(void);
void    box_update(uint32_t now_ms);
void    box_render(void);
void    box_input(Gesture g);
void    box_leave(void);

uint8_t box_screen_mode(void);      // BoxMode
uint8_t box_screen_cursor(void);    // the row inside the current mode
uint8_t box_screen_slot(void);      // the slot the action list is about

// Drops the screen back to the slot list. ui.cpp calls it when a release
// commits, because the action list is then about a slot that holds nothing.
void    box_screen_to_list(void);

#endif  // PB_SCREEN_BOX_H
