// =============================================================================
//  PEBBLEBOL - app/app.h
//  The two entry points the Arduino runtime calls. Pebblebol.ino is a three
//  line shim over them so that no game code lives in a .ino (arduino-cli runs
//  ctags over a .ino and injects prototypes, which a .cpp is free of).
// =============================================================================
#ifndef PB_APP_H
#define PB_APP_H

#include <stdint.h>

#include "../game/xp.h"          // XpSource: the one currency app_award_xp takes

// Wires the modules in dependency order: display, entropy, persistence,
// settings, clock, pet, input, radio, UI. Called once.
void app_setup(void);

// Pumps input, the 1 Hz logic tick, presentation timing, the frame and the
// radio. Non-blocking; one iteration stays well below the 5 s Task WDT.
void app_loop(void);

// THE ONE DOOR FOR EXPERIENCE. Awards `amount` XP from `src` to the active
// Pebble, metered by the anti-farm ledger of game/xp.h, and owns everything a
// level-up implies: the ledger decrease reaches flash, SIM_EV_LEVEL_UP reaches
// ui_note_events(), and the new level is committed. Returns true when the
// Pebble levelled. Called by ui.cpp for care actions and minigames and by the
// logic tick for carried time; P4/P5/P6 add battles, captures and items here.
bool app_award_xp(uint16_t amount, XpSource src);

#endif  // PB_APP_H
