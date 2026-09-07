// =============================================================================
//  PEBBLEBOL - ui/screen_setup.h
//  THE TWO FIRST-BOOT SCREENS: name the device, pick one of three starters.
//  The third question of the flow - the date - is asked on ui/screen_time.cpp,
//  which has existed since P2-C6 and is reused unchanged.
//
//  ONE GRAMMAR ACROSS ALL THREE, and it is the TIME screen's, because that is
//  the one a first-boot player is about to meet anyway:
//
//    L tapped   move on          (next character cell / next candidate)
//    L held     ACCEPT           (take this name / take this Pebble)
//    R tapped   change           (next character / next candidate)
//    R held     keep changing    (auto-repeat, same as TIME's +1)
//    A + B      SKIP this step   (keep the default and move on)
//    A + B held FINISH NOW       (invariant 2: leave with what we have)
//
//  A PLAYER WHO READS NOTHING STILL ENDS UP WITH A WORKING DEVICE. That is the
//  requirement, not a nicety: every step has a default that is already in
//  place before the question is asked (the name is empty, the clock is
//  CAL_UNSET, the Pebble is the historical starter), so SKIP and FINISH are
//  never destructive, and holding both buttons from the first frame lands on
//  HOME with exactly the device every pre-P10-C4 build shipped.
//
//  BOTH ROWS ARE SF_STICKY | SF_LOCK_INPUT, for TIME's two reasons: B means
//  "change" here and BACK everywhere else, so the global grammar may not run
//  first; and a half-typed name may not be thrown away by invariant 3's twenty
//  second auto-return. Spec section 65's "no time-critical menus" is that
//  second flag, and tests/test_screens.cpp asserts it over the whole flow.
//
//  PURE translation unit: gfx.h, the sprite atlas, app/onboarding.h and the
//  ui.h seams. No Arduino, no render.h.
// =============================================================================
#ifndef PB_SCREEN_SETUP_H
#define PB_SCREEN_SETUP_H

#include <stdint.h>

#include "../app/onboarding.h"     // ObStep, which the callers name
#include "../core/nt_types.h"

// --- the naming screen -------------------------------------------------------
void setup_name_enter(void);
void setup_name_update(uint32_t now_ms);
void setup_name_render(void);
void setup_name_input(Gesture g);

// --- the starter screen ------------------------------------------------------
void setup_pick_enter(void);
void setup_pick_update(uint32_t now_ms);
void setup_pick_render(void);
void setup_pick_input(Gesture g);

// =============================================================================
//  THE FIRST-BOOT INTRO
//
//  WHY IT IS A PHASE OF THE STARTER SCREEN AND NOT A ScreenId OF ITS OWN.
//  The brief was "the animation's last frame is that screen's first". A
//  separate screen can only approximate that - two enter hooks, two renders and
//  a cut between them - while a phase of THIS screen gets it structurally: the
//  three bodies climb out of the compile failure and stop exactly where
//  setup_pick_render() draws them, because BOTH ask the same pick_geometry()
//  where that is. The frame, the species name and the hint are the only things
//  that appear at the cut.
//
//  IT PLAYS ON A TRUE FIRST RUN AND NOWHERE ELSE. app/app.cpp arms it, and only
//  when boot == BOOT_FIRST_RUN, so a flow RESUMED after a power cut goes
//  straight to the question - the step is already stored, the cinematic is a
//  first impression rather than a gate, and making somebody watch it twice
//  because their battery died is the opposite of what it is for. That is also
//  why the intro is not an ObStep: the four two-bit values are all spoken for
//  (onboarding.h), and buying a fifth would cost a save migration for a beat
//  that must not be resumable in the first place.
//
//  THE INTERRUPTION CONTRACT IS ui/screen_encounter.h's, and for its reasons:
//  ANY press skips it, and THE CLOCK ENDS IT ANYWAY. setup_intro_phase() is a
//  pure function of (ui_now_ms() - t0) and answers SU_IN_NONE past the end, so
//  even with every explicit cancel deleted no intro can outlive its own length.
//  A press SKIPS AND ONLY SKIPS: a press that also chose a starter would make
//  the impatient player's very first act on the device an accident.
//
//  ABOUT SIXTEEN SECONDS, which is the number the whole thing was cut to. The
//  brief was "cinematic plus onboarding under two minutes", and the onboarding
//  is three questions the player answers at their own pace - so the only part
//  of that budget this file can actually spend is this one.
// =============================================================================
enum SuIntroPhase : uint8_t {
  SU_IN_NONE = 0,
  SU_IN_TYPE,      // pseudo-C types itself onto the panel, one character at a time
  SU_IN_BUILD,     // the header says COMPILANDO and a bar fills
  SU_IN_FAIL,      // the bar stalls, the band tears, the header says ERROR: 3 BUGS
  SU_IN_BUGS,      // three bodies climb out of the tear, one at a time
  SU_IN_HOLD,      // all three standing where the picker will draw them
  SU_IN_PHASE_COUNT
};

// Arm it. app/app.cpp's ONLY call, on a true first run. Deliberately NOT done
// in setup_pick_enter(), which also runs on a resumed flow.
void setup_intro_arm(void);

// End it. Idempotent, and NOT the thing that bounds it - see the contract above.
void setup_intro_cancel(void);

// The phase AT ui_now_ms(). Pure in the clock.
uint8_t setup_intro_phase(void);

// -----------------------------------------------------------------------------
//  WHAT THE TESTS READ. The two screens hold their working state in file-scope
//  statics like every other screen here; these are how a test says "the cursor
//  moved" or "the ring wrapped" without a golden.
// -----------------------------------------------------------------------------
uint8_t     setup_name_cursor(void);        // 0..NAME_MAX_LEN-1
const char* setup_name_text(void);          // the LATIN-1 name being typed
uint8_t     setup_pick_cursor(void);        // 0..OB_STARTER_COUNT-1

// The character ring, exposed so a test can drive it and so the gate can count
// it. Latin-1 bytes; index 0 is the space that leaves a cell blank.
uint8_t     setup_ring_len(void);
char        setup_ring_at(uint8_t i);

// THE STEP ADVANCE, shared with ui/screen_time.cpp. It is here and not in
// app/onboarding.cpp because it touches the Config through ui.h's seams and
// hands the navigation to the state machine, and app/onboarding.cpp is the
// pure DECISION - which step follows which - with no opinion about screens.
//
// `done_step` is the ObStep that has just been answered. Writes the next step
// to the Config, persists it WITHOUT a toast, and re-roots the navigation on
// the screen that asks it (or on HOME when the flow is over).
void setup_advance(uint8_t done_step);

// Invariant 2 inside the flow: mark setup finished with every default already
// in place and re-root on HOME. ui/screen_time.cpp is the second caller - the
// middle question has to be able to end the flow as well as the other two, or
// holding both buttons there would leave the step persisted and the next boot
// would ask again.
void setup_finish_now(void);

// True when a live first-boot flow is on this screen, which is what tells
// ui/screen_time.cpp whether HOLD_L means "back to SETTINGS" or "next step".
bool setup_in_flow(void);

#endif  // PB_SCREEN_SETUP_H
