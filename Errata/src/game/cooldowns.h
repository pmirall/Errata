// =============================================================================
//  ERRATA - game/cooldowns.h
//  ENCOUNTER COOLDOWNS (spec section 21, plan T9). P5-C2.
//
//  One row per network already explored, keyed by the abstract net_hash the
//  scanner produced (networking/net_classify.h). Two hours, absolute deadlines,
//  persisted in the "cd" pair, LRU over COOLDOWN_SLOTS rows - and a SECOND,
//  per-boot RAM table used instead whenever the clock is not trustworthy.
//
//  PURE MODULE: no Arduino, no heap, no float, and no clock of its own. The
//  three things it cannot compute - the wall clock, a monotonic millisecond
//  count and how the clock came to hold its value - are passed in as a CdClock,
//  the same shape persistence/save_manager.h injects its clocks with. That is
//  what makes the uncalibrated path testable at all: a host test drives
//  CAL_UNSET and a calibration landing mid-session directly, with no device.
//
//  WHY TWO TABLES, AND WHAT EACH ONE CLOSES
//  ----------------------------------------
//  While gt_cal_state() == CAL_UNSET, gt_now() is "last persisted epoch plus
//  uptime" and on a never-calibrated unit that is a SMALL NUMBER that restarts
//  near zero on every boot. Writing such a deadline into the persisted table
//  would be a farm: the first real calibration is a forward jump of about
//  1.7e9 seconds, it is accepted unconditionally (gt_set_epoch() only guards
//  BACKWARDS moves), and it would step past every uptime-scale deadline at
//  once - all thirty-two networks freed by one keystroke on the time screen.
//  Reading the persisted table against an uptime estimate is the mirror
//  failure: rows written while calibrated hold real epochs, so every network
//  would read "not ready" for about fifty-four years, which is precisely spec
//  section 47's "must never get stuck permanently ... clock is invalid".
//
//  So the fallback is a separate table IN BOTH DIRECTIONS, and its deadlines
//  are MONOTONIC MILLISECONDS rather than gt_now(): within a boot gt_now()
//  still carries the god-mode skew and can jump the instant a calibration
//  lands, and a cooldown that a cheat menu can expire is not a cooldown.
//
//  WHAT THE FALLBACK DOES NOT CLOSE, SAID PLAINLY. A per-boot table is cleared
//  by a power cycle, so an uncalibrated device can still farm by rebooting.
//  Spec section 21's "avoid resetting when battery dies" is UNACHIEVABLE while
//  CAL_UNSET - there is no trustworthy timestamp to persist - and the design
//  trades a CATASTROPHIC farm (one calibration frees every network) for a
//  LINEAR one (one reboot frees everything, at the cost of a boot). That is the
//  right trade and it is a comment here rather than an implication.
//
//  A ROLLBACK NEVER SHORTENS A COOLDOWN, and that is true BY CONSTRUCTION
//  rather than by a check: only an absolute deadline is stored, so a clock that
//  moves backwards makes `now < until` MORE true and the wait gets longer. The
//  dangerous direction is FORWARD, which is what the two tables above are for.
// =============================================================================
#ifndef ER_COOLDOWNS_H
#define ER_COOLDOWNS_H

#include <stdint.h>
#include <stddef.h>

#include "../core/config.h"                 // ENCOUNTER_COOLDOWN_S
#include "../persistence/save_schema.h"     // CooldownTable, COOLDOWN_SLOTS, TimeCal

// -----------------------------------------------------------------------------
// Everything this module needs from the outside, in one struct.
//   now_epoch - gt_now(). Meaningless while cal == CAL_UNSET, and not read then.
//   now_ms    - a monotonic millisecond count (millis()). ALWAYS read: it is
//               what the RAM table's deadlines are measured in.
//   cal       - gt_cal_state(). CAL_UNSET selects the RAM table for reads AND
//               writes; anything above it selects the persisted one.
// -----------------------------------------------------------------------------
struct CdClock {
  uint32_t now_epoch;
  uint32_t now_ms;
  uint8_t  cal;        // TimeCal
};

// -----------------------------------------------------------------------------
// cd_begin()
//   Clears the per-boot RAM table and the dirty flag. Call once from setup(),
//   and from a test between cases. Never touches the persisted table: that one
//   comes off flash with the rest of GameState.
// -----------------------------------------------------------------------------
void cd_begin(void);

// -----------------------------------------------------------------------------
// cd_ready(t, net_hash, c) -> may this network be explored now?
//
//   true  - no row for it, or its deadline has passed.
//   false - armed and not yet expired, OR net_hash is 0.
//
//   net_hash 0 IS NEVER READY. Zero is CooldownRow's "empty row" marker
//   (persistence/save_schema.h), so a network that hashed to 0 would be
//   permanently off cooldown and could be farmed forever. The scanner already
//   folds 0 away (NET_HASH_NEVER_ZERO), and this is the second half of that
//   argument: even if one arrived, it is refused rather than free.
//
//   May write to `t`: a calibration that lands mid-session promotes the RAM
//   rows into the persisted table before it is consulted (see cd_arm). Poll
//   cd_take_dirty() afterwards.
// -----------------------------------------------------------------------------
bool cd_ready(CooldownTable& t, uint32_t net_hash, const CdClock& c);

// -----------------------------------------------------------------------------
// cd_arm(t, net_hash, c) -> was the network armed?
//
//   Sets the deadline to ENCOUNTER_COOLDOWN_S from now, in the table `c.cal`
//   selects. false only for net_hash 0.
//
//   EVICTION, WHEN ALL COOLDOWN_SLOTS ROWS ARE TAKEN: the row with the smallest
//   deadline is reused. With one fixed cooldown period, deadline order IS arm
//   order, so "smallest deadline" is exactly "least recently used" - and it
//   also picks an already-expired row first, since an expired deadline is by
//   definition smaller than a live one. One rule, both jobs; a separate
//   "expired first" pass would be a second rule that could disagree with it.
//
//   THE PROMOTION, WHICH BOTH ENTRY POINTS DO SO NEITHER CAN FORGET IT: when
//   the clock has become trustworthy since the RAM rows were armed, each live
//   RAM row is converted to now_epoch + its remaining time and written into the
//   persisted table, and the RAM table is emptied. Leaving them uncompared
//   would re-open the farm from the other side - the player arms thirty-two
//   networks uncalibrated, calibrates, and the persisted table has never heard
//   of any of them.
// -----------------------------------------------------------------------------
bool cd_arm(CooldownTable& t, uint32_t net_hash, const CdClock& c);

// -----------------------------------------------------------------------------
// cd_take_dirty()
//   True once after any call that CHANGED the persisted table, then false until
//   the next one. The caller saves on true. It is a take-and-clear rather than
//   a return value on cd_arm() because cd_ready() can dirty the table too (the
//   promotion above), and a caller that only saved after cd_arm() would drop
//   thirty-two promoted rows on the next power cut.
// -----------------------------------------------------------------------------
bool cd_take_dirty(void);

// Rows currently held in the per-boot RAM table. Diagnostics and tests.
uint8_t cd_ram_used(void);

#endif  // ER_COOLDOWNS_H
