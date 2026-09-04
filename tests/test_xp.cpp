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
TEST(a_round_trip_under_reports_the_budget_and_never_over_reports_it) {
  const uint32_t step = (uint32_t)XP_WIN_CARE_S / (uint32_t)XP_CAP_CARE;

  // Walk a whole window one second at a time. At every instant, the budget a
  // save/restore pair reconstructs must be <= the live one.
  for (uint32_t elapsed = 0; elapsed <= (uint32_t)XP_WIN_CARE_S; elapsed += 7u) {
    xp_ledger_reset(0);
    xp_ledger_tick(step / 2u);            // half a point of fraction on the books
    uint8_t snap[XP_LEDGER_SLOTS];
    xp_ledger_snapshot(snap);

    xp_ledger_tick(elapsed);
    const uint16_t live = xp_daily_left(xp_ledger(), XP_SRC_CARE);

    (void)xp_ledger_restore(snap, (uint8_t)XP_LEDGER_SLOTS,
                            SANE_EPOCH, SANE_EPOCH + elapsed);
    const uint16_t back = xp_daily_left(xp_ledger(), XP_SRC_CARE);

    CHECK(back <= live);                  // never invents a point
    CHECK_NEAR(live, back, 1);            // and loses at most the dropped fraction
  }
}

TEST(a_restore_cannot_exceed_the_cap_however_long_the_gap) {
  uint8_t snap[XP_LEDGER_SLOTS];
  xp_ledger_reset(1);
  xp_ledger_snapshot(snap);

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
  CHECK_EQ(xp_daily_left(xp_ledger(), XP_SRC_CARE), 1);  // one step, one point

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

  // The empty bucket reaches flash as a zero byte, and an hour of real time
  // brings it all the way back - the same continuous refill every other metered
  // source gets, so a reboot cannot shortcut it.
  uint8_t snap[XP_LEDGER_SLOTS];
  xp_ledger_snapshot(snap);
  CHECK_EQ(snap[XP_SRC_BATTLE], 0);
  CHECK_EQ(xp_ledger_restore(snap, (uint8_t)XP_LEDGER_SLOTS,
                             SANE_EPOCH, SANE_EPOCH + (uint32_t)XP_WIN_BATTLE_S), 1);
  CHECK_EQ(xp_daily_left(xp_ledger(), XP_SRC_BATTLE), (uint16_t)XP_CAP_BATTLE);

  // Half a window buys half a bucket and not one point more.
  CHECK_EQ(xp_ledger_restore(snap, (uint8_t)XP_LEDGER_SLOTS,
                             SANE_EPOCH, SANE_EPOCH + (uint32_t)(XP_WIN_BATTLE_S / 2u)), 1);
  CHECK_EQ(xp_daily_left(xp_ledger(), XP_SRC_BATTLE), (uint16_t)(XP_CAP_BATTLE / 2u));
}
