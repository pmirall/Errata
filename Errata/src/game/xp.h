// =============================================================================
//  ERRATA - game/xp.h
//  EXPERIENCE AND LEVELS (spec section 11, plan P3-C2).
//
//  Two things live here and nothing else:
//
//   1. THE CURVE. XP_TABLE[L] (data/balance.h) is what level L costs to leave,
//      so BugInstance.xp is always "XP inside the current level" - never a
//      running total, which is what keeps it inside its u16. xp_add() spends an
//      award across as many levels as it reaches IN ONE CALL, saturates at
//      XP_LEVEL_MAX, and rescales hp_cur with the derived hp_max so a level-up
//      neither heals to full nor leaves the creature over its maximum.
//
//   2. THE ANTI-FARM LEDGER. Every XP source that a player could repeat at will
//      is metered by a budget that belongs to the DEVICE and to real time, in
//      the exact shape of the hourly gain ceiling in game/sim.h: it refills
//      continuously (so an hour of real time is worth exactly one cap, and a
//      boundary reset cannot be waited out), swapping the active Bug does
//      not reset it, and it survives a power cut through
//      xp_ledger_snapshot()/xp_ledger_restore(), whose round trip can only
//      UNDER-report the budget - never invent a point.
//
//  PURE MODULE. stdint plus the save schema, the species table, balance.h and
//  game/evolution.h (the level gate behind EVO_STATE_PENDING - see xp_add()).
//  No Arduino, no clock, no RNG, no I/O: the caller feeds elapsed seconds in
//  and carries the persisted bytes out. tests/test_xp.cpp compiles it directly.
// =============================================================================
#ifndef ER_XP_H
#define ER_XP_H

#include <stdint.h>

#include "../data/balance.h"              // XP_TABLE, the caps and the windows
#include "../persistence/save_schema.h"   // BugInstance, XP_LEDGER_SLOTS

// -----------------------------------------------------------------------------
//  WHERE XP COMES FROM.
//
//  The first XP_LEDGER_SLOTS entries are the METERED sources: each owns one
//  byte of Inventory.xp_ledger and one cap/window pair in balance.h. The
//  sources after them arrive with later phases and are unmetered for now -
//  they are farm-proof by construction (a capture consumes an encounter, an
//  item consumes the item), so they need no bucket, and the enum leaves them
//  room rather than renumbering the persisted ones later.
// -----------------------------------------------------------------------------
enum XpSource : uint8_t {
  XP_SRC_CARE = 0,      // a care action landed              (metered, hourly)
  XP_SRC_MINIGAME,      // a minigame finished               (metered, hourly)
  // P6-C2 WIDENED WHAT THIS ONE MEANS AND DID NOT ADD A FIFTH. The daily
  // activity score of game/activity.h pays through here: its largest term IS
  // "time carried awake", XP_CAP_CARRY / XP_WIN_CARRY_S is already the daily
  // window, and a fifth METERED source is not an enum edit - it makes
  // Inventory.xp_ledger 5 B, which moves offsetof(items) and fails that
  // struct's own assert, or keeps 32 B by deleting the seventh item stack out
  // of every save. THE COST, said rather than glossed: activity XP and the
  // passive carry drip now share ONE XP_CAP_CARRY budget, so a heavy-walking
  // day crowds out the drip. They measure the same thing, which is the argument
  // for it; a day that earns 48 XP from carrying earns none from networks,
  // which is the price.
  XP_SRC_CARRY,         // time carried awake, and the activity score
                        //                                    (metered, daily)
  XP_SRC_BATTLE,        // a practice-battle win             (metered, hourly)
  XP_SRC_CAPTURE,       // P5 capture                        (unmetered)
  XP_SRC_ITEM,          // P6 XP candy                       (unmetered)
  // P5-C3. UNMETERED LIKE THE TWO ABOVE, AND WITH A WEAKER ARGUMENT FOR IT,
  // which is why the argument is written down instead of assumed. A capture
  // consumes an encounter and an item consumes the item; a SPECIAL XP burst
  // consumes NEITHER, so the only thing behind it is the two-hour
  // ENCOUNTER_COOLDOWN_S - and game/cooldowns.h says plainly that while
  // gt_cal_state() is CAL_UNSET that table is per-boot, so a reboot frees every
  // network. Metering it properly is not available: Inventory.xp_ledger is four
  // persisted bytes and the static_assert below pins the metered sources to
  // exactly those four, so a fifth would be a save-schema change. The answer is
  // in game/encounters.cpp's encounter_special_xp(): on an uncalibrated device
  // the burst pays NOTHING. The event still fires; the award does not.
  XP_SRC_SPECIAL,       // P5 SPECIAL encounter              (unmetered, and
                        //                                    withheld while the
                        //                                    clock is CAL_UNSET)
  XP_SRC_COUNT
};
static_assert((int)XP_SRC_BATTLE + 1 == XP_LEDGER_SLOTS,
              "the metered XpSources must be exactly the persisted ledger slots");
static_assert((int)XP_SRC_COUNT <= 8, "XpSource is persisted nowhere, but keep it small");

// The live ledger. One bucket per metered source, in WHOLE XP, plus the exact
// second-remainder of its refill so the same elapsed time gives the same answer
// at any step size (the care integrator's discipline, without milli-points:
// the refill step divides its window exactly, see the balance.h asserts).
struct XpLedger {
  uint16_t left[XP_LEDGER_SLOTS];    // XP still spendable from this source
  uint32_t rem_s[XP_LEDGER_SLOTS];   // seconds toward the next returned point
  uint32_t carry_s;                  // awake seconds not yet paid out as XP
};

// -----------------------------------------------------------------------------
//  THE CURVE
// -----------------------------------------------------------------------------
// What level `level` costs to leave. 0 for level 0 and for XP_LEVEL_MAX, which
// is what "the curve is finished" looks like to a caller (the HOME bar draws a
// full rule on it rather than dividing by zero).
uint16_t xp_for_level(uint8_t level);

// Awards `amount` XP from `src`, already metered by the ledger, and carries it
// across as many levels as it reaches. Returns true when at least one level was
// gained; *levels_gained (optional) receives how many. An empty slot, a zero
// award, a source with no budget left and a Bug already at XP_LEVEL_MAX all
// return false, and the last of those also pins xp at 0: past the top of the
// curve there is nothing for it to mean.
//
// THE METER IS SPENT EVEN AT XP_LEVEL_MAX (P7-C6). The ledger counts XP HANDED
// OUT, not XP that found a home. That is what it has to count, because
// app/app.cpp reads the meter either side of an award to learn what a day's
// activity score was worth in happiness - and while the top of the curve
// returned before meter_take(), a level-30 Bug earned no activity happiness
// at all (measured: 5,500 milli owed, 0 paid). The price of the fix is that a
// maxed Bug now drains the device-wide daily budget it used to leave for a
// Box-mate, which is pinned by a named case in tests/test_xp.cpp.
//
// It also raises EVO_STATE_PENDING (game/evolution.h) whenever the award
// leaves the Bug at or past the level its evolution rule asks for. THE
// LEVEL GATE IS ALL THIS MODULE CAN SEE - it is pure, with no happiness, no
// item and no activity score - so the bit means "the level requirement is
// met", NOT "it will evolve". Whoever consumes it re-evaluates the whole rule
// with real context and leaves the bit set when the condition does not hold.
bool xp_add(BugInstance& p, uint16_t amount, XpSource src, uint8_t* levels_gained);

// What each source pays, before metering.
uint16_t xp_care_action_amount(void);
uint16_t xp_minigame_amount(uint16_t win_permille);   // permille * 8 / 1000

// -----------------------------------------------------------------------------
//  THE DERIVED HP RULE - shared with game/evolution.cpp
//
//  hp_max = 10 + 2*base_hp + level (plan 1.5.1): derived, never stored. Two
//  things move it under a creature that is standing still - a level-up, which
//  is this module's, and an evolution, which is game/evolution.cpp's - and both
//  must rescale hp_cur the SAME way, so there is exactly one rescale in the
//  firmware and it is xp_hp_rescale().
// -----------------------------------------------------------------------------
//  IT IS `inline constexpr` IN THE HEADER, not a symbol in xp.o, since P4-C1.
//  Two pure layers outside this module need it - persistence/migration.cpp
//  gives a migrated pet its full HP, and game/bug.cpp derives hp_max - and
//  linking the whole XP module (which drags game/evolution.cpp with it, the two
//  call each other) into the persistence tests to reach one addition would be a
//  worse trade than an inline definition. It is still ONE definition.
//
//  The widest possible input is base_hp 255 at level 255, which is 775: no u16
//  overflow is reachable however badly a content pack is edited.
inline constexpr uint16_t xp_hp_max(uint8_t base_hp, uint8_t level)
{
  return (uint16_t)(10u + 2u * (uint16_t)base_hp + (uint16_t)level);
}

// A full Bug stays full, a hurt one keeps its fraction, nothing over-heals
// and nothing lands above the new maximum. Integer and truncating: the lost
// fraction is under one HP and always in the honest direction. A zero
// hp_max_before leaves hp_cur alone - there is no fraction to keep.
void xp_hp_rescale(BugInstance& p, uint16_t hp_max_before, uint16_t hp_max_after);

// -----------------------------------------------------------------------------
//  THE LEDGER
// -----------------------------------------------------------------------------
// The device-wide ledger. There is exactly one, for the same reason the care
// ledger is one: a budget that followed the creature could be farmed by
// swapping Box slots.
const XpLedger& xp_ledger(void);

// full != 0 seeds every bucket at its cap (a device with no history), full == 0
// seeds them empty (the safe direction, refilled by xp_ledger_restore()).
void xp_ledger_reset(uint8_t full);

// Refills every metered bucket for `dt_s` seconds of real time.
void xp_ledger_tick(uint32_t dt_s);

// Whole XP still available from `src`. 0xFFFF for an unmetered source, matching
// sim_gain_left()'s convention.
uint16_t xp_daily_left(const XpLedger& l, XpSource src);

// Carried time: accumulates `dt_s` awake seconds and returns the whole XP they
// have earned, dropping nothing (the remainder is kept for the next call). The
// caller passes the answer to xp_add(XP_SRC_CARRY), which is where the daily
// cap is actually applied.
uint16_t xp_carry_due(uint32_t dt_s);

// --- persistence seam (the module does no I/O) -------------------------------
// Hands the live budget out as one byte per metered bucket, truncated toward
// zero, for Inventory.xp_ledger.
void xp_ledger_snapshot(uint8_t out[XP_LEDGER_SLOTS]);

// Re-seeds the ledger from a snapshot taken at saved_epoch, as of now_epoch:
//
//     left = min(cap, saved)
//
// A RESTORE ADDS NOTHING. It hands back exactly the budget that was true at the
// last save, and every point beyond that has to be refilled by xp_ledger_tick()
// out of seconds the device watched pass. Until P6-C4 it also aged the snapshot
// forward by (now_epoch - saved_epoch) / refill_step, and that term was an
// unbounded farm: both epochs are wall clock, the wall clock is typed on the
// time screen, and a day of "elapsed" refills a whole bucket - so 100 rounds of
// (clock +1 day, reboot, one Wi-Fi scan) spent 990 metered XP in zero real
// seconds against an honest 37.8 per real day. game/xp.cpp carries the
// measurement and what the honest player loses with it.
//
// Returns 1 when the snapshot was used; it returns 0 - and seeds ZERO, never the
// cap - when there is no snapshot or when either epoch is below
// NT_EPOCH_SANE_MIN, i.e. when the blob cannot be shown to come from a device
// that knew the date.
uint8_t xp_ledger_restore(const uint8_t* pts, uint8_t n,
                          uint32_t saved_epoch, uint32_t now_epoch);

#endif  // ER_XP_H
