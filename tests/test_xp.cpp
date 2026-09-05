// =============================================================================
//  Pebblebol host tests - test_xp.cpp
//  game/xp.cpp: the curve, the level-up carry and the anti-farm ledger
//  (plan P3-C2, section 4 "XP and leveling work").
//
//  The five things the plan asks this file to prove:
//    * the curve is monotonic and its whole sum fits u16;
//    * one award carries across MANY levels in a single call;
//    * level 30 saturates - no level 31, no wrapped xp;
//    * the caps are enforced, per source, and refill on real time only;
//    * a snapshot/restore round trip UNDER-reports the budget, never over.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "data/species_table.h"
#include "game/xp.h"
#include "game/activity.h"          // P6-C4: the farm lives between the two

#define SANE_EPOCH  1700000000u   // >= NT_EPOCH_SANE_MIN

// A minimal but legal Pebble: xp_add() refuses an empty slot, so species_id and
// id have to be real. hp_cur starts at the derived maximum for `level`.
static void make_pebble(PebbleInstance& p, uint8_t level) {
  memset(&p, 0, sizeof p);
  p.magic      = (uint16_t)PEBBLE_MAGIC;
  p.layout_ver = (uint8_t)PEBBLE_LAYOUT_VER;
  p.species_id = (uint8_t)SPECIES_ID_STARTER;
  p.id         = 0xABCDEF01u;
  p.level      = level;
  const SpeciesDef* sp = species_get(p.species_id);
  p.hp_cur = (uint16_t)(10u + 2u * (uint16_t)sp->base_hp + (uint16_t)level);
}

static uint16_t hp_max_at(uint8_t level) {
  const SpeciesDef* sp = species_get((uint8_t)SPECIES_ID_STARTER);
  return (uint16_t)(10u + 2u * (uint16_t)sp->base_hp + (uint16_t)level);
}

// =============================================================================
//  THE CURVE
// =============================================================================
TEST(the_curve_rises_strictly_and_fits_a_u16) {
  uint32_t total = 0;
  for (uint8_t lv = 1; lv < (uint8_t)XP_LEVEL_MAX; ++lv) {
    const uint16_t here = xp_for_level(lv);
    CHECK(here > 0);
    if (lv + 1 < (uint8_t)XP_LEVEL_MAX) CHECK(xp_for_level((uint8_t)(lv + 1)) > here);
    total += here;
  }
  // PebbleInstance.xp is u16 and holds XP INSIDE the level, so every entry must
  // fit on its own; the plan additionally asks that the whole curve does, so a
  // lifetime total never needs a wider type.
  CHECK(total < 65535u);
  CHECK_EQ(total, xp_table_total());

  // Both ends answer 0: there is no level 0, and level 30 has nothing to buy.
  CHECK_EQ(xp_for_level(0), 0);
  CHECK_EQ(xp_for_level((uint8_t)XP_LEVEL_MAX), 0);
  CHECK_EQ(xp_for_level(200), 0);
}

// =============================================================================
//  LEVELLING
// =============================================================================
TEST(an_award_below_the_threshold_only_moves_xp) {
  xp_ledger_reset(1);
  PebbleInstance p;
  make_pebble(p, 1);

  uint8_t ups = 9;
  CHECK(!xp_add(p, 2, XP_SRC_CARE, &ups));
  CHECK_EQ(ups, 0);
  CHECK_EQ(p.level, 1);
  CHECK_EQ(p.xp, 2);
}

TEST(one_award_carries_across_many_levels) {
  xp_ledger_reset(1);
  PebbleInstance p;
  make_pebble(p, 1);

  // Exactly what levels 1..4 cost, plus three XP of change. XP_SRC_ITEM is
  // unmetered, which is what lets a single award be this large.
  const uint32_t cost = (uint32_t)xp_for_level(1) + xp_for_level(2) +
                        xp_for_level(3) + xp_for_level(4);
  uint8_t ups = 0;
  CHECK(xp_add(p, (uint16_t)(cost + 3u), XP_SRC_ITEM, &ups));
  CHECK_EQ(ups, 4);
  CHECK_EQ(p.level, 5);
  CHECK_EQ(p.xp, 3);
}

TEST(a_level_up_rescales_hp_and_never_heals_to_full) {
  xp_ledger_reset(1);
  PebbleInstance p;
  make_pebble(p, 1);
  p.hp_cur = (uint16_t)(hp_max_at(1) / 2u);       // half dead going in
  const uint16_t before = p.hp_cur;

  uint8_t ups = 0;
  CHECK(xp_add(p, xp_for_level(1), XP_SRC_ITEM, &ups));
  CHECK_EQ(ups, 1);
  CHECK_EQ(p.level, 2);

  // Wider bar, same fraction: strictly under the new maximum (no free heal) and
  // never below where it started (the bar only grew).
  CHECK(p.hp_cur < hp_max_at(2));
  CHECK(p.hp_cur >= before);
  CHECK_EQ(p.hp_cur, (uint16_t)(((uint32_t)before * hp_max_at(2)) / hp_max_at(1)));
}

TEST(a_full_pebble_stays_full_over_a_level_up) {
  xp_ledger_reset(1);
  PebbleInstance p;
  make_pebble(p, 1);                                // hp_cur == hp_max_at(1)
  uint8_t ups = 0;
  CHECK(xp_add(p, xp_for_level(1), XP_SRC_ITEM, &ups));
  CHECK_EQ(p.hp_cur, hp_max_at(2));                 // and not one point over
}

TEST(level_thirty_saturates) {
  xp_ledger_reset(1);
  PebbleInstance p;
  make_pebble(p, 29);
  p.xp = 5;

  // Far more than the last level costs, in one unmetered award.
  uint8_t ups = 0;
  CHECK(xp_add(p, 60000u, XP_SRC_ITEM, &ups));
  CHECK_EQ(ups, 1);
  CHECK_EQ(p.level, (uint8_t)XP_LEVEL_MAX);
  CHECK_EQ(p.xp, 0);                                // nothing wrapped into it

  // And at the top every further award is a no-op, whatever its size.
  ups = 7;
  CHECK(!xp_add(p, 60000u, XP_SRC_ITEM, &ups));
  CHECK_EQ(ups, 0);
  CHECK_EQ(p.level, (uint8_t)XP_LEVEL_MAX);
  CHECK_EQ(p.xp, 0);
  CHECK(p.hp_cur <= hp_max_at((uint8_t)XP_LEVEL_MAX));
}

TEST(an_empty_slot_earns_nothing_and_spends_no_budget) {
  xp_ledger_reset(1);
  PebbleInstance p;
  memset(&p, 0, sizeof p);                          // species_id 0, id 0

  const uint16_t before = xp_daily_left(xp_ledger(), XP_SRC_CARE);
  CHECK(!xp_add(p, 2, XP_SRC_CARE, nullptr));
  CHECK_EQ(p.level, 0);
  CHECK_EQ(xp_daily_left(xp_ledger(), XP_SRC_CARE), before);
}

// =============================================================================
//  THE ANTI-FARM LEDGER
// =============================================================================
TEST(the_hourly_care_cap_is_enforced) {
  xp_ledger_reset(1);
  PebbleInstance p;
  make_pebble(p, 1);

  CHECK_EQ(xp_daily_left(xp_ledger(), XP_SRC_CARE), XP_CAP_CARE);

  // Spam care actions with no time passing: the cap, and only the cap, lands.
  uint32_t earned = 0;
  for (int i = 0; i < 500; ++i) {
    const uint16_t left = xp_daily_left(xp_ledger(), XP_SRC_CARE);
    (void)xp_add(p, xp_care_action_amount(), XP_SRC_CARE, nullptr);
    earned += left - xp_daily_left(xp_ledger(), XP_SRC_CARE);
  }
  CHECK_EQ(earned, (uint32_t)XP_CAP_CARE);
  CHECK_EQ(xp_daily_left(xp_ledger(), XP_SRC_CARE), 0);

  // A different source keeps its own budget: one bucket cannot drain another.
  CHECK_EQ(xp_daily_left(xp_ledger(), XP_SRC_MINIGAME), XP_CAP_MINIGAME);
  CHECK_EQ(xp_daily_left(xp_ledger(), XP_SRC_CARRY), XP_CAP_CARRY);
}

TEST(the_budget_refills_on_real_time_and_never_past_the_cap) {
  xp_ledger_reset(0);
  CHECK_EQ(xp_daily_left(xp_ledger(), XP_SRC_CARE), 0);

  const uint32_t step = (uint32_t)XP_WIN_CARE_S / (uint32_t)XP_CAP_CARE;

  // One refill step returns exactly one point, and not before.
  xp_ledger_tick(step - 1u);
  CHECK_EQ(xp_daily_left(xp_ledger(), XP_SRC_CARE), 0);
  xp_ledger_tick(1u);
  CHECK_EQ(xp_daily_left(xp_ledger(), XP_SRC_CARE), 1);

  // Chunking does not matter: the remainder carries exactly.
  for (uint32_t i = 0; i < step; ++i) xp_ledger_tick(1u);
  CHECK_EQ(xp_daily_left(xp_ledger(), XP_SRC_CARE), 2);

  // A week of elapsed time is still one cap, never more.
  xp_ledger_tick(7u * 86400u);
  CHECK_EQ(xp_daily_left(xp_ledger(), XP_SRC_CARE), XP_CAP_CARE);
  CHECK_EQ(xp_daily_left(xp_ledger(), XP_SRC_CARRY), XP_CAP_CARRY);
}

// 24 h awake, one minute at a time, from a given starting budget. Carried time
// ASKS for 144 XP a day (86,400 / 600); what it gets is what the ledger allows.
static uint32_t carry_a_day(uint8_t start_full) {
  xp_ledger_reset(start_full);
  PebbleInstance p;
  make_pebble(p, 1);

  uint32_t earned = 0;
  for (uint32_t s = 0; s < 86400u; s += 60u) {
    xp_ledger_tick(60u);
    const uint16_t due = xp_carry_due(60u);
    if (due == 0u) continue;
    const uint16_t left = xp_daily_left(xp_ledger(), XP_SRC_CARRY);
    (void)xp_add(p, due, XP_SRC_CARRY, nullptr);
    earned += left - xp_daily_left(xp_ledger(), XP_SRC_CARRY);
  }
  return earned;
}

TEST(the_daily_carry_cap_bounds_a_day_of_carrying) {
  // Steady state - the bucket is empty at the start of the day, which is where
  // a player who carried the device yesterday actually is - pays exactly the
  // cap, not the 144 XP the clock asked for.
  CHECK_EQ(carry_a_day(0), (uint32_t)XP_CAP_CARRY);

  // A budget that refills CONTINUOUSLY rather than resetting on a boundary can
  // be walked into full, which is deliberate: a player coming back after a day
  // away spends what accumulated while they were gone. That burst is bounded by
  // one bucket, so the worst any 24 h can pay is a full bucket plus a day of
  // refill - and never the 144 the clock keeps asking for.
  const uint32_t burst = carry_a_day(1);
  CHECK(burst > (uint32_t)XP_CAP_CARRY);
  CHECK(burst <= 2u * (uint32_t)XP_CAP_CARRY);
  CHECK(burst < 144u);
}

TEST(carried_time_pays_one_point_per_ten_minutes_and_keeps_the_remainder) {
  xp_ledger_reset(1);
  CHECK_EQ(xp_carry_due((uint32_t)XP_CARRY_STEP_S - 1u), 0);
  CHECK_EQ(xp_carry_due(1u), (uint16_t)XP_CARRY_STEP_XP);      // the remainder carried
  CHECK_EQ(xp_carry_due((uint32_t)XP_CARRY_STEP_S * 3u), (uint16_t)(3 * XP_CARRY_STEP_XP));
}

TEST(a_minigame_pays_permille_times_eight_over_a_thousand) {
  CHECK_EQ(xp_minigame_amount(0), 0);
  CHECK_EQ(xp_minigame_amount(1000), XP_MINIGAME_NUM);
  CHECK_EQ(xp_minigame_amount(500), XP_MINIGAME_NUM / 2);
  CHECK_EQ(xp_minigame_amount(9999), XP_MINIGAME_NUM);         // clamped, not wrapped
}

// =============================================================================
//  THE PERSISTENCE ROUND TRIP
// =============================================================================
// NARROWED IN P6-C4, and the second assertion is what changed. It used to be
// CHECK_NEAR(live, back, 1) - "a restore lands within a point of the live
// budget" - which was true only because the restore AGED the snapshot forward by
// the wall-clock gap. That term is gone (game/xp.cpp says why), so the restore
// now lands on the snapshot itself and the gap is refilled by xp_ledger_tick()
// instead. The property the case was written for survives verbatim and is the
// one the file's banner promises: a round trip never invents a point.
TEST(a_round_trip_under_reports_the_budget_and_never_over_reports_it) {
  const uint32_t step = (uint32_t)XP_WIN_CARE_S / (uint32_t)XP_CAP_CARE;

  // Walk a whole window one second at a time. At every instant, the budget a
  // save/restore pair reconstructs must be <= the live one.
  for (uint32_t elapsed = 0; elapsed <= (uint32_t)XP_WIN_CARE_S; elapsed += 7u) {
    xp_ledger_reset(0);
    xp_ledger_tick(step / 2u);            // half a point of fraction on the books
    uint8_t snap[XP_LEDGER_SLOTS];
    xp_ledger_snapshot(snap);
    const uint16_t at_save = xp_daily_left(xp_ledger(), XP_SRC_CARE);

    xp_ledger_tick(elapsed);
    const uint16_t live = xp_daily_left(xp_ledger(), XP_SRC_CARE);

    (void)xp_ledger_restore(snap, (uint8_t)XP_LEDGER_SLOTS,
                            SANE_EPOCH, SANE_EPOCH + elapsed);
    const uint16_t back = xp_daily_left(xp_ledger(), XP_SRC_CARE);

    CHECK(back <= live);                  // never invents a point
    CHECK_EQ(back, at_save);              // exactly the budget that was saved
  }
}

TEST(a_restore_cannot_exceed_the_cap_however_long_the_gap) {
  uint8_t snap[XP_LEDGER_SLOTS];
  xp_ledger_reset(1);
  xp_ledger_snapshot(snap);

  // A FULL snapshot restores full, and a gap of over a year adds nothing to it -
  // there is nothing left to add, and since P6-C4 there is no adding at all.
  CHECK_EQ(xp_ledger_restore(snap, (uint8_t)XP_LEDGER_SLOTS,
                             SANE_EPOCH, SANE_EPOCH + 400u * 86400u), 1);
  CHECK_EQ(xp_daily_left(xp_ledger(), XP_SRC_CARE), XP_CAP_CARE);
  CHECK_EQ(xp_daily_left(xp_ledger(), XP_SRC_CARRY), XP_CAP_CARRY);

  // A blob that claims more than the cap is clamped, not believed.
  uint8_t forged[XP_LEDGER_SLOTS];
  memset(forged, 0xFF, sizeof forged);
  CHECK_EQ(xp_ledger_restore(forged, (uint8_t)XP_LEDGER_SLOTS, SANE_EPOCH, SANE_EPOCH), 1);
  CHECK_EQ(xp_daily_left(xp_ledger(), XP_SRC_CARE), XP_CAP_CARE);
  CHECK_EQ(xp_daily_left(xp_ledger(), XP_SRC_MINIGAME), XP_CAP_MINIGAME);
}

// THE OLD BODY OF THIS CASE WAS A SENTENCE WIDER THAN ITS TREE, and the
// narrowing is recorded because it is the reason the farm shipped. It claimed
// "a reboot cannot refill a spent budget" and then exercised exactly two clocks:
// one advanced by a SINGLE refill step (which it asserted refilled by one point,
// i.e. it tested that a reboot DOES refill) and one run backwards. The one clock
// it never tried was a forward jump of a whole window, which is the only one
// that refills the bucket to its cap - and which a player can type on the time
// screen in about fifteen seconds. That case is now the second half of this one.
TEST(a_reboot_cannot_refill_a_spent_budget) {
  xp_ledger_reset(1);
  PebbleInstance p;
  make_pebble(p, 1);

  // Spend the hour's care budget, then "power cut" one refill step later.
  for (int i = 0; i < 500; ++i) (void)xp_add(p, xp_care_action_amount(), XP_SRC_CARE, nullptr);
  CHECK_EQ(xp_daily_left(xp_ledger(), XP_SRC_CARE), 0);

  uint8_t snap[XP_LEDGER_SLOTS];
  xp_ledger_snapshot(snap);
  CHECK_EQ(snap[XP_SRC_CARE], 0);

  const uint32_t step = (uint32_t)XP_WIN_CARE_S / (uint32_t)XP_CAP_CARE;
  xp_ledger_reset(0);                                   // the boot seed
  CHECK_EQ(xp_ledger_restore(snap, (uint8_t)XP_LEDGER_SLOTS,
                             SANE_EPOCH, SANE_EPOCH + step), 1);
  CHECK_EQ(xp_daily_left(xp_ledger(), XP_SRC_CARE), 0);  // spent is spent

  // THE CASE THAT WAS MISSING. A whole window of wall clock - what the time
  // screen hands over in one edit - and the budget is still exactly what was
  // saved. Every metered bucket, not just the one that was drained, because the
  // restore walks all four.
  for (uint32_t gap = 0; gap <= 4u * (uint32_t)XP_WIN_CARE_S; gap += 997u) {
    xp_ledger_reset(0);
    CHECK_EQ(xp_ledger_restore(snap, (uint8_t)XP_LEDGER_SLOTS,
                               SANE_EPOCH, SANE_EPOCH + gap), 1);
    for (uint8_t i = 0; i < (uint8_t)XP_LEDGER_SLOTS; ++i) {
      CHECK_EQ(xp_daily_left(xp_ledger(), (XpSource)i), snap[i]);
    }
  }

  // An untrustworthy clock at either end seeds ZERO - never the cap, which
  // would be the exploit a never-calibrated device could power-cycle for.
  CHECK_EQ(xp_ledger_restore(snap, (uint8_t)XP_LEDGER_SLOTS, 0u, SANE_EPOCH), 0);
  CHECK_EQ(xp_daily_left(xp_ledger(), XP_SRC_CARE), 0);
  CHECK_EQ(xp_ledger_restore(nullptr, 0, SANE_EPOCH, SANE_EPOCH), 0);
  CHECK_EQ(xp_daily_left(xp_ledger(), XP_SRC_CARE), 0);

  // A clock that ran BACKWARDS buys nothing either.
  CHECK_EQ(xp_ledger_restore(snap, (uint8_t)XP_LEDGER_SLOTS,
                             SANE_EPOCH + 86400u, SANE_EPOCH), 1);
  CHECK_EQ(xp_daily_left(xp_ledger(), XP_SRC_CARE), 0);

  // And the honest half of the same rule, so the narrowing above cannot be read
  // as "a restore always returns zero": a bucket that was HALF full comes back
  // half full, from any clock, and is then refilled by real ticked seconds.
  xp_ledger_reset(1);
  for (uint16_t i = 0; i < (uint16_t)(XP_CAP_CARE / 2u); ++i) {
    (void)xp_add(p, 1, XP_SRC_CARE, nullptr);
  }
  const uint16_t half = xp_daily_left(xp_ledger(), XP_SRC_CARE);
  xp_ledger_snapshot(snap);
  xp_ledger_reset(0);
  CHECK_EQ(xp_ledger_restore(snap, (uint8_t)XP_LEDGER_SLOTS,
                             SANE_EPOCH, SANE_EPOCH + 30u * 86400u), 1);
  CHECK_EQ(xp_daily_left(xp_ledger(), XP_SRC_CARE), half);
  xp_ledger_tick(step * 3u);
  CHECK_EQ(xp_daily_left(xp_ledger(), XP_SRC_CARE), (uint16_t)(half + 3u));
}

// REPLACED, NOT DELETED, and the old name said why it had to be:
// the_reserved_battle_slot_is_unmetered_and_persists_as_zero asserted that
// game/xp.cpp's battle row was {0, 0} - which was true, and which P4-C4 changed
// on purpose, because the practice battle it built is a minute of button
// presses repeatable at will and was the only unmetered repeatable XP source in
// the game. The case now holds the OPPOSITE property, which is a stronger one:
// the bucket is real, it is spent, it bounds an afternoon of fighting, and it
// survives a power cut.
TEST(the_battle_bucket_bounds_an_afternoon_of_fighting) {
  xp_ledger_reset(1);
  PebbleInstance p;
  make_pebble(p, 1);

  // A full bucket is exactly XP_CAP_BATTLE, not 0xFFFF: 0xFFFF is what
  // xp_daily_left() answers for a source with NO meter at all.
  CHECK_EQ(xp_daily_left(xp_ledger(), XP_SRC_BATTLE), (uint16_t)XP_CAP_BATTLE);

  // Two wins fit; the third pays nothing at all.
  uint8_t ups = 0;
  CHECK(xp_add(p, (uint16_t)XP_BATTLE_WIN, XP_SRC_BATTLE, &ups));
  CHECK_EQ(ups, 2);                                   // 25 XP is levels 1 and 2
  CHECK_EQ(xp_daily_left(xp_ledger(), XP_SRC_BATTLE),
           (uint16_t)(XP_CAP_BATTLE - XP_BATTLE_WIN));
  const uint16_t xp_after_one = p.xp;
  const uint8_t  lv_after_one = p.level;
  (void)xp_add(p, (uint16_t)XP_BATTLE_WIN, XP_SRC_BATTLE, &ups);
  CHECK_EQ(xp_daily_left(xp_ledger(), XP_SRC_BATTLE), 0);
  CHECK(p.level > lv_after_one || p.xp != xp_after_one);   // the second one paid

  const uint16_t xp_after_two = p.xp;
  const uint8_t  lv_after_two = p.level;
  CHECK(!xp_add(p, (uint16_t)XP_BATTLE_WIN, XP_SRC_BATTLE, &ups));
  CHECK_EQ(p.xp, xp_after_two);                        // and the third did not
  CHECK_EQ(p.level, lv_after_two);

  // The empty bucket reaches flash as a zero byte, and a window of real TICKED
  // time brings it all the way back - the same continuous refill every other
  // metered source gets.
  //
  // REWRITTEN IN P6-C4, and the old body is worth naming because it asserted the
  // exploit. It drove that refill through xp_ledger_restore(SANE_EPOCH,
  // SANE_EPOCH + XP_WIN_BATTLE_S) and said in its own comment "so a reboot
  // cannot shortcut it" - but a restore taking a wall-clock gap IS the shortcut,
  // and the gap is typed on the time screen. The refill that a reboot cannot
  // shortcut is xp_ledger_tick()'s, because it is fed seconds the device watched
  // pass. Same property, driven through the mechanism that actually has it.
  uint8_t snap[XP_LEDGER_SLOTS];
  xp_ledger_snapshot(snap);
  CHECK_EQ(snap[XP_SRC_BATTLE], 0);
  CHECK_EQ(xp_ledger_restore(snap, (uint8_t)XP_LEDGER_SLOTS,
                             SANE_EPOCH, SANE_EPOCH + (uint32_t)XP_WIN_BATTLE_S), 1);
  CHECK_EQ(xp_daily_left(xp_ledger(), XP_SRC_BATTLE), 0);   // the gap buys nothing
  xp_ledger_tick((uint32_t)XP_WIN_BATTLE_S);
  CHECK_EQ(xp_daily_left(xp_ledger(), XP_SRC_BATTLE), (uint16_t)XP_CAP_BATTLE);

  // Half a window of ticked time buys half a bucket and not one point more.
  xp_ledger_reset(0);
  xp_ledger_tick((uint32_t)(XP_WIN_BATTLE_S / 2u));
  CHECK_EQ(xp_daily_left(xp_ledger(), XP_SRC_BATTLE), (uint16_t)(XP_CAP_BATTLE / 2u));
}


// =============================================================================
//  P6-C4: THE ACTIVITY FARM, WHICH NEITHER MODULE'S OWN TEST BINARY COULD SEE
//
//  game/activity.h names five anti-farm layers and says of the last one that
//  "even a bug in every rule above it leaves the award rate-limited by a budget
//  a reboot cannot refill". That sentence was FALSE, and it was the sentence the
//  whole argument leaned on. xp_ledger_restore() aged the saved budget forward
//  by (now_epoch - saved_epoch) / refill_step, both epochs are wall clock, and
//  the wall clock is typed by the player on the time screen. So:
//
//     100 x (set the clock forward one day, reboot, run ONE Wi-Fi scan of THE
//     SAME ten access points)  ->  990 metered XP and 125,000 care milli-points,
//     in ZERO real seconds. An honest player at full tilt earns 37.8 XP a day
//     and the happiness bar only holds 100,000.
//
//  Neither half does it alone (both controls are below), and neither file's own
//  test binary contained both modules, which is why test_activity.cpp can hold
//  four named cheat cases and still miss this one.
//
//  These cases drive the real activity.o and the real xp.o through the payment
//  app.cpp performs, so they fail if EITHER module's guard is removed.
// =============================================================================
#define FARM_EPOCH  1767225600u                 // 2026-01-01T00:00:00Z, a day boundary

static ActClock farm_clk(uint32_t epoch) {
  ActClock c; c.now_epoch = epoch; c.cal = (uint8_t)CAL_USER; return c;
}

// Everything that survives a power cycle, and nothing that does not.
struct FarmFlash {
  CooldownTable  cds;
  uint8_t        xpts[XP_LEDGER_SLOTS];
  uint32_t       xepoch;
  PebbleInstance pet;
};

// One boot, exactly as app.cpp's setup() reaches these two modules: the per-boot
// half of the score is cleared, the ledger is seeded EMPTY and then re-seeded
// from the snapshot on flash.
static void farm_boot(FarmFlash& f, uint32_t now) {
  act_begin();
  xp_ledger_reset(0);
  (void)xp_ledger_restore(f.xpts, (uint8_t)XP_LEDGER_SLOTS, f.xepoch, now);
}

// app_pay_activity(), minus the NVS write and the Box lookup. Returns the XP the
// LEDGER actually paid for - which is what the happiness is scaled by, so a
// drained bucket stops both halves of the reward together.
static uint32_t farm_pay(FarmFlash& f, uint32_t now, uint32_t& happy_out) {
  const ActGain g = act_take_gain();
  uint16_t granted = 0u;
  if (g.xp != 0u) {
    const uint16_t before = xp_daily_left(xp_ledger(), XP_SRC_CARRY);
    (void)xp_add(f.pet, g.xp, XP_SRC_CARRY, nullptr);
    const uint16_t after  = xp_daily_left(xp_ledger(), XP_SRC_CARRY);
    granted = (uint16_t)(before - after);
    xp_ledger_snapshot(f.xpts);
    f.xepoch = now;
  }
  happy_out += act_happy_for_granted_xp(g, granted);
  return granted;
}

// The same ten access points, every time. Diversity is not what is being tested.
static void farm_scan(FarmFlash& f, uint32_t now) {
  uint32_t h[ACT_CAP_NETS];
  for (uint8_t i = 0; i < (uint8_t)ACT_CAP_NETS; ++i) h[i] = 0xA0000001u + (uint32_t)i * 0x1111u;
  (void)act_note_networks(f.cds, h, (uint8_t)ACT_CAP_NETS, farm_clk(now));
}

static void farm_fresh(FarmFlash& f, uint8_t full_bucket) {
  memset(&f, 0, sizeof f);
  make_pebble(f.pet, 1);
  act_begin();
  xp_ledger_reset(full_bucket);
  xp_ledger_snapshot(f.xpts);
  f.xepoch = FARM_EPOCH;
}

TEST(a_typed_day_plus_a_reboot_cannot_refill_a_spent_activity_budget) {
  // THE CHEAT, given the most generous start it can have: a FULL carry bucket.
  // Every round types one more day onto the clock, power-cycles, and scans the
  // same street. The bucket is spent once and never comes back, because nothing
  // refills it but seconds the device watched pass - and no seconds pass here.
  FarmFlash f;
  farm_fresh(f, 1);
  uint32_t now = FARM_EPOCH, spent = 0, happy = 0;
  for (int i = 0; i < 100; ++i) {
    now += (uint32_t)ACT_DAY_S;            // the time screen
    farm_boot(f, now);                     // the power cycle
    farm_scan(f, now);
    spent += farm_pay(f, now, happy);
  }
  CHECK_EQ(spent, (uint32_t)XP_CAP_CARRY);              // exactly one bucket, ever
  CHECK(happy <= (uint32_t)PB_CARE_MILLI_MAX / 10u);    // and a tenth of the bar

  // The bucket the cheat drained is the one an honest day would have used, so
  // there is no second bucket hiding behind a second Pebble either.
  CHECK_EQ(xp_daily_left(xp_ledger(), XP_SRC_CARRY), 0);

  // A hundred more rounds pay NOTHING, which is the shape that matters: the
  // exploit is not merely small, it does not scale with how long it is run.
  const uint32_t before = spent;
  for (int i = 0; i < 100; ++i) {
    now += (uint32_t)ACT_DAY_S;
    farm_boot(f, now);
    farm_scan(f, now);
    spent += farm_pay(f, now, happy);
  }
  CHECK_EQ(spent, before);
}

TEST(a_typed_day_without_a_reboot_cannot_pay_a_second_time_either) {
  // THE HALF THAT NEEDS NO POWER CYCLE AT ALL, and the half that was worse: the
  // happiness had no meter of its own, so 100 typed days inside ONE session paid
  // 125,000 care milli-points against a bar that holds 100,000 - an empty
  // Pebble to a full one, with no care action and no real time. It is now scaled
  // by the metered XP behind it, so it stops when the bucket does.
  FarmFlash f;
  farm_fresh(f, 1);
  uint32_t now = FARM_EPOCH, spent = 0, happy = 0;
  for (int i = 0; i < 100; ++i) {
    now += (uint32_t)ACT_DAY_S;
    farm_scan(f, now);
    spent += farm_pay(f, now, happy);
  }
  CHECK_EQ(spent, (uint32_t)XP_CAP_CARRY);
  CHECK(happy < (uint32_t)PB_CARE_MILLI_MAX);          // cannot fill the bar
  CHECK(happy <= (uint32_t)PB_CARE_MILLI_MAX / 10u);
}

TEST(an_honest_day_still_pays_what_it_always_paid) {
  // THE CONTROL THAT STOPS THE FIX FROM BEING "PAY NOBODY". One real day, lived
  // one minute at a time, with the ledger refilled by the seconds that actually
  // passed: four hours of carrying, twenty care actions and ten new networks -
  // which is every term the firmware can reach today, and pays 38 XP and 4,750
  // milli of happiness. The fourth term is added at the end, where the case says
  // what it is doing, to reach game/activity.h's advertised 44 and 5,500.
  FarmFlash f;
  farm_fresh(f, 1);
  uint32_t now = FARM_EPOCH, spent = 0, happy = 0;
  // Four hours of carrying is ACT_CAP_CARRY_MIN minutes, i.e. the whole of that
  // term - and it stays inside ONE day, which matters: a loop that ran the clock
  // through midnight would roll the day and reset the score under itself.
  for (uint32_t s = 0; s < (uint32_t)ACT_CAP_CARRY_MIN * 60u; s += 60u) {
    xp_ledger_tick(60u);
    now += 60u;
    (void)act_note_carried(f.cds, 60u, farm_clk(now));
    spent += farm_pay(f, now, happy);
  }
  for (int i = 0; i < (int)ACT_CAP_INTERACT; ++i) {
    (void)act_note_interaction(f.cds, farm_clk(now));
    spent += farm_pay(f, now, happy);
  }
  farm_scan(f, now);
  spent += farm_pay(f, now, happy);

  // THE BEST DAY THIS FIRMWARE CAN ACTUALLY HAVE is three terms, not four:
  // act_note_peer() has no caller until P7-C1 exists to discover peers, so the
  // 60 points of the peers term are unreachable on the device today. Saying 440
  // here would be a sentence wider than the tree.
  const uint16_t reachable = (uint16_t)(ACT_SCORE_MAX - ACT_CAP_PEERS * ACT_PTS_PEER);
  CHECK_EQ(act_score_today(f.cds, farm_clk(now)), reachable);                 // 380
  CHECK_EQ(spent, (uint32_t)(reachable / ACT_XP_STEP_POINTS));                // 38
  CHECK_EQ(happy, (uint32_t)(reachable / ACT_HAPPY_STEP_POINTS) *
                  (uint32_t)ACT_HAPPY_STEP_MILLI);                            // 4750

  // With the fourth term - the day P7-C1 makes possible - it is the full
  // ACT_SCORE_MAX and exactly the reward game/activity.h advertises.
  for (uint32_t k = 1; k <= (uint32_t)ACT_CAP_PEERS; ++k) {
    (void)act_note_peer(f.cds, 0x5EED0000u + k, farm_clk(now));
    spent += farm_pay(f, now, happy);
  }
  CHECK_EQ(act_score_today(f.cds, farm_clk(now)), (uint16_t)ACT_SCORE_MAX);
  CHECK_EQ(spent, (uint32_t)(ACT_SCORE_MAX / ACT_XP_STEP_POINTS));            // 44
  CHECK_EQ(happy, (uint32_t)(ACT_SCORE_MAX / ACT_HAPPY_STEP_POINTS) *
                  (uint32_t)ACT_HAPPY_STEP_MILLI);                            // 5500

  // And the honest day beats a hundred typed ones, which is the whole point of
  // the ordering: cheating is now strictly worse than playing.
  CHECK(spent > (uint32_t)XP_CAP_CARRY - (uint32_t)XP_CAP_CARRY / 4u);
}
