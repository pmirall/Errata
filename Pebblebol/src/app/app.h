// =============================================================================
//  PEBBLEBOL - app/app.h
//  The two entry points the Arduino runtime calls. Pebblebol.ino is a three
//  line shim over them so that no game code lives in a .ino (arduino-cli runs
//  ctags over a .ino and injects prototypes, which a .cpp is free of).
// =============================================================================
#ifndef PB_APP_H
#define PB_APP_H

#include <stdint.h>

#include "../game/activity.h"     // ActClock: the clock the pure score takes
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

// -----------------------------------------------------------------------------
//  THE ACTIVITY SEAM (P6-C2, spec sections 25 and 57)
// -----------------------------------------------------------------------------
// The wall clock and the calibration state, assembled in ONE place so no caller
// can hand game/activity.h a clock without saying how trustworthy it is - and
// CAL_UNSET is what makes the whole score zero. A pure screen cannot read
// gt_now() itself, which is why this exists rather than a comment asking it to.
ActClock app_act_clock(void);

// Drains what the score earned since the last drain and pays it: XP through
// app_award_xp(XP_SRC_CARRY) - the metered bucket, deliberately not a fifth
// slot, see game/activity.h - and happiness onto the ACTIVE Pebble, clamped
// exactly as a care item's restore is. Also flushes the persisted half of the
// score, which rides the "cd" pair. Called once per logic tick and nowhere
// else: screens and the care path only ever NOTE activity, so there is exactly
// one place in the firmware where an activity reward can be paid.
void app_pay_activity(void);

// One care action landed. ui.cpp's action path calls it beside its
// app_award_xp(XP_SRC_CARE); the reward is paid on the next tick's drain.
void app_note_interaction(void);

// -----------------------------------------------------------------------------
//  EVOLUTION (spec section 18, plan P3-C3). Two doors, and the ORDER between
//  them is the safety argument written at the top of ui/ceremony.h.
// -----------------------------------------------------------------------------
// Should the player be asked? True when the active Pebble carries
// EVO_STATE_PENDING *and* the whole rule - level and condition - holds against
// the context this build can actually supply. Reads nothing, writes nothing.
bool app_evolution_offer(void);

// Performs the evolution the player just confirmed and COMMITS it: on TRUE the
// Pebble has reached flash before this returns, so the caller may start the
// ceremony knowing a brownout mid-show reboots into the evolved creature.
// FALSE means nothing evolved AND nothing was written - including the case
// where the model change succeeded but the save refused it, which is rolled
// back rather than shown. The nvs2 checkpoint is refreshed too, but it is a
// backup of a save that already landed: if only the checkpoint fails this
// still returns true and only the recovery copy is stale.
bool app_evolve_active(void);

#endif  // PB_APP_H
