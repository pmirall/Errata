// =============================================================================
//  PEBBLEBOL - ui/screen_encounter.h
//  THE ENCOUNTER TRANSIENT AND THE CAPTURE ATTEMPT (spec sections 22 and 23,
//  section 6 SCR_ENCOUNTER and SCR_CAPTURE). P5-C3 and P5-C4.
//
//  TWO ScreenIds, ONE translation unit, and that is deliberate rather than
//  lazy: the CAPTURE screen is about the creature the ENCOUNTER screen is
//  showing, so splitting them across two files would mean one of them owning
//  the EncounterResult and the other reaching for it through a seam. One
//  module, one 8 B result, one CaptureState.
//
//  IT REPLACES soon_encounter() and soon_capture().
//
//  THE FOUR OUTCOMES, and what each screen does with them:
//    WILD     "!PEBBLE SALVAJE!" with the species and level, and the two
//             options section 23 draws: [CAPTURAR] [DEJAR]. A opens
//             SCR_CAPTURE; B leaves, which is a legitimate answer and not a
//             failure.
//    ITEM     the drop is added to the bag on ENTRY and the screen says what
//             it was. Nothing to decide, so B is the only key. (The ITEM_REWARD
//             state is phase 6's own screen; this is the phase-5 shape of it.)
//    SPECIAL  the event is applied on ENTRY - an XP burst through
//             XP_SRC_SPECIAL, or the 24 h corruption status - and the screen
//             names the event.
//    NOTHING  never reaches here: ui/screen_network.cpp answers it with a toast
//             and goes back, because a screen that says "nothing happened" is a
//             keystroke charged for no information.
//
//  ON THE CAPTURE SCREEN, spec section 23 in four rules:
//    * one roll per press, at most CAPTURE_MAX_ATTEMPTS, then it flees;
//    * a capture item is spent ONLY on a roll that happened, so a refusal
//      (a full Box) never costs one;
//    * a full Box is a QUESTION, never a silent discard: the screen says so and
//      sends the player to the BOX to free a slot;
//    * the active Pebble is never touched, which game/box.h's invariant B4
//      makes structural rather than a rule this screen has to remember.
//
//  PURE translation unit.
// =============================================================================
#ifndef PB_SCREEN_ENCOUNTER_H
#define PB_SCREEN_ENCOUNTER_H

#include <stdint.h>

#include "../core/nt_types.h"
#include "../game/encounters.h"

// Armed by ui/screen_network.cpp on the frame the scan answers, exactly as
// battle_arm() arms the battle screen. `category` is carried for the header.
void encounter_arm(const EncounterResult& r, uint8_t category);

// The ENCOUNTER hooks.
void encounter_enter(void);
void encounter_render(void);
void encounter_input(Gesture g);
void encounter_leave(void);

// The CAPTURE hooks.
void capture_enter(void);
void capture_render(void);
void capture_input(Gesture g);
void capture_leave(void);

// State, for tests and the DIAG console.
enum CapScreenMode : uint8_t {
  CSM_READY = 0,      // waiting for the player to throw
  CSM_RESULT,         // an attempt has been resolved
  CSM_MODE_COUNT
};
const EncounterResult& encounter_screen_result(void);
uint8_t encounter_screen_cursor(void);
uint8_t capture_screen_mode(void);
uint8_t capture_screen_outcome(void);   // CaptureOutcome of the last attempt
uint8_t capture_screen_item(void);      // the capture item the throw will spend

#endif  // PB_SCREEN_ENCOUNTER_H
