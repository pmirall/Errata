// =============================================================================
//  PEBBLEBOL - game/corruption.h
//  THE CORRUPTED STATUS AND ITS DEADLINE (spec section 55). P5-C3.
//
//  WHAT THIS FILE IS AND IS NOT. Spec section 55's corruption has two halves:
//  a STATUS with a 24 h life, and a set of EFFECTS (the sprite glitch, the
//  altered idle film, the battle modifier, the two evolutions it unlocks).
//  P9-C5 owns the effects and the plan says so. What lands here is only the
//  half P5-C3 must own, because P5-C3 is the first code in the tree that can
//  set the bit at all: the SPECIAL encounter's corruption event.
//
//  IT EXISTS AS ITS OWN MODULE RATHER THAN AS THREE FUNCTIONS INSIDE
//  encounters.cpp BECAUSE IT HAS TWO CALLERS THAT ARE NOT EACH OTHER: the
//  encounter sets the status and game/inventory.cpp's Antivirus clears it
//  (balance.json's CORRUPTION.cleared_by_item is item 7 and items.json's
//  `clears` column now says so in the table, which is what lets the pack's own
//  verify.py compare the two). One fact, one owner; P9-C5 grows this file
//  rather than finding the rule in a screen.
//
//  THE DEADLINE IS PERSISTED AND IS NOT ON THE WIRE.
//  PebbleInstance.corrupt_until_epoch is four of the twelve reserved bytes
//  (persistence/save_schema.h says why, and why it needs no migration and no
//  VReject). networking/protocol.h refuses PBS_CORRUPTED on ingest outright,
//  so a peer can send neither the bit nor the deadline.
//
//  A CLOCK THAT CANNOT BE TRUSTED CANNOT ARM ONE, and that is the whole reason
//  cor_apply() takes the calibration state. While gt_cal_state() is CAL_UNSET,
//  gt_now() is uptime-shaped and restarts near zero every boot, so a deadline
//  written from it would be a 24 h status that a power cycle clears - or, after
//  the first real calibration moves the clock forward by ~1.7e9 seconds, one
//  that clears instantly. game/cooldowns.cpp met the same problem and answered
//  it with a per-boot RAM table; corruption answers it the other way, by NOT
//  SETTING THE STATUS AT ALL, because a 24 h effect has nowhere to live on a
//  device with no idea what 24 h is and there is no second table worth 128 B of
//  globals for an effect nothing reads until P9-C5.
//
//  PURE MODULE: stdint, the save schema and data/balance.h. No Arduino, no
//  clock of its own, no RNG, no heap, no float, no I/O.
// =============================================================================
#ifndef PB_GAME_CORRUPTION_H
#define PB_GAME_CORRUPTION_H

#include <stdint.h>

#include "../data/balance.h"               // CORRUPT_DURATION_S
#include "../persistence/save_schema.h"    // PebbleInstance, PBS_CORRUPTED, TimeCal

// -----------------------------------------------------------------------------
// cor_apply(p, now_epoch, cal) -> was the Pebble corrupted?
//
//   Sets PBS_CORRUPTED and a deadline of now_epoch + CORRUPT_DURATION_S.
//   Refuses (returns false, changing nothing) for an empty slot and for a clock
//   that is not trustworthy - see the banner. Re-applying to an already
//   corrupted Pebble EXTENDS the deadline rather than adding a second status,
//   which is the same rule game/battle.cpp's DOT uses: one representation per
//   fact, refreshed rather than stacked.
// -----------------------------------------------------------------------------
bool cor_apply(PebbleInstance& p, uint32_t now_epoch, uint8_t cal);

// -----------------------------------------------------------------------------
// cor_expire(p, now_epoch, cal) -> did the status just end?
//
//   Clears the bit and the deadline once now_epoch has reached it. A no-op on a
//   Pebble that is not corrupted, and a no-op while the clock is CAL_UNSET: an
//   uptime estimate is not a wall clock and would expire a real deadline on the
//   first boot after one was armed (spec section 47's "must never get stuck" is
//   about the OTHER direction, and cor_clear() below is the escape from it).
// -----------------------------------------------------------------------------
bool cor_expire(PebbleInstance& p, uint32_t now_epoch, uint8_t cal);

// -----------------------------------------------------------------------------
// cor_clear(p) -> was it corrupted?
//   The item route (balance.json CORRUPTION.cleared_by_item). No clock, so it
//   works on an uncalibrated device: the cure must never be the thing that gets
//   stuck. Also clears the deadline, so nothing is left to re-fire.
// -----------------------------------------------------------------------------
bool cor_clear(PebbleInstance& p);

// True while the status is set. Reads the BIT and not the deadline: they are
// kept in step by the three functions above and the bit is what every consumer
// (evolution's EVOC_CORRUPTED, ui/pet_view.cpp, app.cpp) already asks about.
bool cor_is_corrupted(const PebbleInstance& p);

// Seconds left, 0 when not corrupted or already due. Diagnostics and tests; the
// screens do not draw it yet.
uint32_t cor_left_s(const PebbleInstance& p, uint32_t now_epoch, uint8_t cal);

#endif  // PB_GAME_CORRUPTION_H
