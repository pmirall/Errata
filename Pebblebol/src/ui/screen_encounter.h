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
// The three answers a wild encounter takes, in the order they are drawn.
// LUCHAR joined them after the owner played the build and asked for it: catching
// and walking away were the only two, so a creature you could not afford to keep
// was worth nothing at all - and a fight is the one thing an exploration loop
// full of creatures obviously wants to offer.
enum EncOption : uint8_t {
  ENC_OPT_CATCH = 0,
  ENC_OPT_FIGHT,
  ENC_OPT_LEAVE,
  ENC_OPT_COUNT
};

const EncounterResult& encounter_screen_result(void);
uint8_t encounter_screen_cursor(void);      // 0..ENC_OPT_COUNT-1
uint8_t capture_screen_mode(void);
uint8_t capture_screen_outcome(void);   // CaptureOutcome of the last attempt
uint8_t capture_screen_item(void);      // the capture item the throw will spend

// =============================================================================
//  THE FILMS (P10-C3, and the wild reveal after it)
//
//  Spec section 22's item pickup and section 23's successful capture had no
//  picture at all: the drop landed in the bag the instant the screen opened and
//  the caught creature was announced in prose. Both are events with a moment,
//  and a moment with no motion reads as a menu.
//
//  WHY THEY ARE HERE AND NOT IN ui/actfx.cpp, which is where the plan points.
//  actfx is welded to HOME - its two draw hooks are called only from
//  draw_home(), its props are parked against petfx_body_ink(), it takes
//  petfx_hold(), and actfx_begin() takes an ActionId, which neither of these
//  events is. SCR_ENCOUNTER and SCR_CAPTURE have no petfx body to hold and no
//  draw_home() to call into. And ui/actfx.cpp includes render.h, so a film
//  written there is compiled by NO host binary and can have no snapshot, which
//  is the one thing this chunk's brief will not accept. What IS reused is
//  actfx's ARITHMETIC, lifted into the pure ui/anim_ease.h at this chunk, and
//  its rule that a film fades by drawing FEWER PIXELS and never by drawing a 0.
//
//  THE INTERRUPTION CONTRACT IS ui/actfx.h:88-93's, AND HERE IT IS TESTABLE.
//  A film is bounded THREE independent ways: enc_film_cancel() ends it, leaving
//  either screen ends it, and THE CLOCK ENDS IT ANYWAY. Even if every explicit
//  cancel in this file were deleted, no film can last longer than its own
//  duration, because enc_film_phase() is a pure function of (now - t0) and
//  answers ENC_FILM_NONE past the end. tests/test_screens.cpp drives exactly
//  that: it arms a film, calls no input at all, advances the clock past the
//  duration and requires the frame to be identical to the un-armed one.
//
//  The ITEM film is an OVERLAY: the screen draws its ordinary text first and the
//  film goes on top through the TRANSPARENT blit (gfx_xbm_t), so an icon
//  crossing its own label does not punch a 12x12 hole in it. That is the seam
//  divergence P10-C3 found and fixed before writing a line of either film. The
//  CAPTURE and WILD films REPLACE the content band instead, so only containment
//  applies to them - see tests/test_screens.cpp, which drives both rules.
//
//  THE WILD REVEAL, AND WHY IT IS THE ONE THAT WAS MISSING. ENC_OUT_ITEM got a
//  film at P10-C3 and a successful capture got one; FINDING THE CREATURE - the
//  event the entire exploration loop exists to produce - opened straight onto
//  two menu options. The owner played the build and named exactly that. So the
//  wild encounter now spends about a second on the thing it is announcing: the
//  content band tears into scanlines, the body assembles out of the tear from
//  the feet up, and the band snaps to inverse on the last beat, which is what
//  covers the cut to the two options.
//
//  IT PLAYS ONCE PER ENCOUNTER, NOT ONCE PER ENTRY. encounter_enter() runs
//  again every time the player backs out of SCR_CAPTURE, and a screen that
//  replayed its establishing shot after every failed throw would be charging a
//  second of the player's time for a fact they already have.
// =============================================================================

// -----------------------------------------------------------------------------
//  THE FILM TIMETABLE. CUMULATIVE MILLISECOND BOUNDARIES, NOT DURATIONS -
//  ui/actfx.cpp's convention and for its reason: the draw code asks "where am I
//  now", and a table of durations makes every such question a running sum that
//  one edit puts out of step with the next.
//
//  IN THE HEADER, not the .cpp, since the retiming below: tests/test_screens.cpp
//  drives these boundaries by name, and a timetable copied into a test as
//  literals is how a film and the case that checks it come to disagree about
//  when a beat ends.
// -----------------------------------------------------------------------------
//  *** THE LENGTHS WENT UP ABOUT 70 % AFTER THE FIRST EVENING ON A BOARD. ***
//  They were written against a host golden, where a film is a still picture you
//  study one frame at a time; on a 0.96" panel at arm's length the same beats
//  are a flicker. The owner's words were "the animations should last longer so
//  they are more legible", and he is right about every one of them: a 300 ms
//  tear is four frames at 20 fps, and four frames of anything is a glitch in
//  the literal sense rather than the intended one.
//
//  NOTHING IS SLOWER TO SIT THROUGH THAN IT NEEDS TO BE. Every film is still
//  bounded by its own clock and skippable by any press (screen_encounter.h),
//  and the longest of them is under two seconds - which is the budget a beat
//  gets before it stops being a reveal and starts being a wait.
#define ENC_ITEM_RISE_MS    880u    // the climb ends
#define ENC_ITEM_END_MS    1660u    // the whole item film ends
#define ENC_CAP_CLAMP_MS    700u    // the brackets have closed
#define ENC_CAP_PULL_MS    1300u    // the body has gone
#define ENC_CAP_END_MS     1840u    // the whole capture film ends
#define ENC_WILD_TEAR_MS    520u    // the scanline tear is over
#define ENC_WILD_FORM_MS   1300u    // the body has finished assembling
#define ENC_WILD_SNAP_MS   1520u    // the band goes inverse from here
#define ENC_WILD_END_MS    1700u    // the whole wild film ends

// AND NO FILM MAY QUIETLY GROW INTO A WAIT. Two seconds is the ceiling: past it
// a player who has seen the beat forty times is being charged for it.
static_assert(ENC_ITEM_END_MS <= 2000u && ENC_CAP_END_MS <= 2000u &&
              ENC_WILD_END_MS <= 2000u,
              "an encounter film is longer than two seconds - that is a wait");

// Which film, if any, is running. Ordered so a test can walk them.
enum EncFilmPhase : uint8_t {
  ENC_FILM_NONE = 0,
  ENC_FILM_ITEM_RISE,     // the drop climbs out of the bag line
  ENC_FILM_ITEM_SETTLE,   // it hangs and sparks
  ENC_FILM_CAP_CLAMP,     // four brackets close on the wild body
  ENC_FILM_CAP_PULL,      // the body dissolves upward between them
  ENC_FILM_CAP_SEAL,      // the brackets collapse onto a sealed marker
  // THE WILD REVEAL, added after the owner played the build and said the one
  // moment the whole loop is named after had no picture at all. Appended
  // rather than grouped with the other two so no existing phase changes value.
  ENC_FILM_WILD_TEAR,     // the band tears into scanlines: corrupted memory
  ENC_FILM_WILD_FORM,     // the bug assembles out of the tear, feet first
  ENC_FILM_WILD_STARE,    // it is whole, it holds, and the band snaps white
  ENC_FILM_PHASE_COUNT
};

// The phase the film is in AT ui_now_ms(). Pure in the clock: nothing has to be
// serviced, and nothing can be left running by a missed call.
uint8_t enc_film_phase(void);

// End whatever is running. Idempotent, and NOT the thing that bounds a film -
// see the contract above.
void enc_film_cancel(void);

#endif  // PB_SCREEN_ENCOUNTER_H
