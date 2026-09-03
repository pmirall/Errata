// =============================================================================
//  PEBBLEBOL - data/balance.h
//  Every number that decides how the game FEELS, in one file (plan P3-C1).
//  config.h keeps pins, timings, feature flags, size caps and versions; the
//  tunables a designer wants to move live here.
//
//  THE CARE MODEL (spec section 27, "relaxed"):
//    A Pebble cannot die. Neglect makes it inconveniently unhappy and nothing
//    worse. The scale is HOURS, not seconds: a full satiety bar takes about a
//    day to empty, so a player who forgets the device over a weekend comes
//    back to a sad Pebble, never to a lost one.
//
//  Units: every *_MPH is milli-points per hour over a 0..100000 stat, integrated
//  by game/sim.cpp with an exact remainder carry (care_rem[]), so the same
//  elapsed time gives the same result at any step size. No floating point.
//
//  Pure header: <stdint.h> only. No Arduino, no allocation, no code.
// =============================================================================
#ifndef PB_BALANCE_H
#define PB_BALANCE_H

#include <stdint.h>

// =============================================================================
// 1. CARE DECAY  -- the hours-scale rates of spec section 27
//    Indexed by CareId (persistence/save_schema.h): HUNGER, HAPPINESS, HEALTH,
//    CLEANLINESS, ENERGY. game/sim.cpp static_asserts that mapping.
//    These are the ADULT-free baseline with every gene at 8; the genome
//    multipliers (appetite / metabolism / sociability) still apply, and the
//    life stage deliberately does NOT: P3-C1 deleted STAGE_MULT_* so that a
//    fresh baby and a week-old veteran decay at exactly the same speed.
// =============================================================================
#define PB_BALANCE_CARE_COUNT   5

// Full bar -> empty, awake, no genome bias:
//   hunger      100000 / 4200 = 23.8 h
//   happiness   100000 / 3000 = 33.3 h
//   cleanliness 100000 / 2000 = 50.0 h (before poops, see below)
//   energy      100000 / 6000 = 16.7 h (one night of sleep refills it)
// HEALTH is not a decay at all: see CARE_ZERO_GRACE_S.
inline constexpr int32_t CARE_DECAY_MPH[PB_BALANCE_CARE_COUNT] = {
  -4200,      // CARE_HUNGER       satiety; 100 = full, do NOT invert
  -3000,      // CARE_HAPPINESS
      0,      // CARE_HEALTH       NOT a decay: see CARE_HEALTH_BLEED_MPH
  -2000,      // CARE_CLEANLINESS  base, before CARE_HYGIENE_POOP_MPH
  -6000       // CARE_ENERGY       awake; asleep uses CARE_ENERGY_ASLEEP_MPH
};

// HEALTH IS NOT A DECAY RATE AND MUST NOT LIVE IN THE ARRAY ABOVE. Its slot
// there used to hold -2,000 and game/sim.cpp consumed it SIGN-FLIPPED, as a
// damage rate behind CARE_ZERO_GRACE_S - so the one refactor everybody reaches
// for ("loop over all five and accumulate") would have applied health twice,
// with the wrong sign, and silently. P3-C2 zeroed the slot and gave the bleed
// its own name: the array is now a pure decay table for the four stats that do
// decay, and this is the positive milli-points per hour health LOSES while a
// core stat has been pinned at 0 for at least CARE_ZERO_GRACE_S. The number is
// unchanged (2,000/h, i.e. 45 h from full to the floor), so nothing moved.
#define CARE_HEALTH_BLEED_MPH   (+2000L)

// Sleeping is a full recharge, not a slow one: 100000 / 20000 = 5 h.
#define CARE_ENERGY_ASLEEP_MPH  (+20000L)

// Extra cleanliness cost per poop on screen. With POOP_MAX 4 the worst case is
// 2000 + 4*1000 = 6000 mph, i.e. 16.7 h from full to filthy - still hours.
#define CARE_HYGIENE_POOP_MPH   (-1000L)

// ST_BOND lives in RAM only (SaveSchema v2 does not carry it).
#define CARE_BOND_MPH           (-800L)

// Health climbs back on its own once all four core stats are comfortable and
// the Pebble is not sick.
#define CARE_HEALTH_REGEN_MPH   (+4000L)
#define HEALTH_REGEN_MIN_PCT    55

// HEALTH is the one stat that cannot be driven to zero. It bleeds ONLY while a
// core stat has been pinned at 0 for at least this long, at
// CARE_DECAY_MPH[CARE_HEALTH], and never past HEALTH_FLOOR_PCT. Total neglect
// therefore costs (100 - 10) / 2 = 45 h of bleeding after the 2 h grace and
// then simply stops: an inconveniently unhappy Pebble, never a dead one.
#define CARE_ZERO_GRACE_S       7200UL       // 2 h
#define HEALTH_FLOOR_PCT        10

// Box recovery (spec section 9 / section 27). A stored Pebble is not simulated:
// it neither decays nor acts, it only heals toward 100 % at this one rate,
// integrated from its own last_updated_epoch. 100000 / 4200 = 23.8 h, so a
// Pebble left in the Box overnight comes back full and a week is far past it.
#define BOX_RECOVER_MPH         (+4200L)

// =============================================================================
// 2. ACTION GAINS  -- whole points, converted to milli by the sim
//    A REFUSED action is a friendly toast and nothing else: no stat moves, no
//    care-quality penalty, no cooldown is charged (game/sim.cpp fail()).
//    Sized against section 1: a meal buys about 7 h of satiety, a clean about
//    12 h of cleanliness, so 3-4 touches a day is a well-kept Pebble.
// =============================================================================
#define ACT_MEAL_HUNGER         30
#define ACT_MEAL_CQ             2
#define ACT_MEAL_REFUSE_PCT     90           // refused above this satiety
#define ACT_SNACK_HUNGER        10
#define ACT_SNACK_HAPPINESS     12
#define ACT_CLEAN_HYGIENE       25
#define ACT_MED_HAPPINESS       (-10)
#define ACT_MED_SECOND_DOSE_PCT 30           // if health < 25
#define ACT_PLAY_HAPPINESS_MAX  6
#define ACT_PLAY_ENERGY         (-8)
#define ACT_PLAY_MIN_ENERGY_PCT 12
#define ACT_PET_HAPPINESS       3

// Diminishing returns on minigame happiness, permille, rolling 3 h window.
#define PLAY_DECAY_WINDOW_S     10800UL
#define PLAY_DECAY_0            1000
#define PLAY_DECAY_1            700
#define PLAY_DECAY_2            450
#define PLAY_DECAY_3            250
#define PLAY_DECAY_4            100
#define PLAY_DECAY_5            0
#define PLAY_DECAY_STEPS        6

// Mimo (PET) decay per hour: 4/3/2/1/0
#define PET_DECAY_STEPS         5

// =============================================================================
// 3. ANTI-FARM  -- cooldowns and the hourly gain ceiling
//    Both are properties of the DEVICE and of real time, not of the creature,
//    so swapping the active Pebble is worthless as a farming move. Hitting
//    either one refuses the action; it never punishes it.
// =============================================================================
#define ACT_CD_GLOBAL_S         2
#define ACT_CD_FEED_S           20
#define ACT_CD_CLEAN_S          15
#define ACT_CD_SLEEP_S          10
#define ACT_CD_PLAY_S           25
#define ACT_CD_MED_S            30

// Shared cooldown for the on-device (S4) minigames.
#define MG_COOLDOWN_S           120

// Per-stat hourly gain budget (whole points). The real anti-farm ceiling; it
// refills continuously, so an hour of real time is worth exactly one cap.
#define GAIN_CAP_HUNGER_H       60
#define GAIN_CAP_HYGIENE_H      80
#define GAIN_CAP_ENERGY_H       90
#define GAIN_CAP_HAPPINESS_H    40

// =============================================================================
// 4. XP AND LEVELS  -- spec section 11 (levels 1..30), plan P3-C2
//
//    XP_TABLE[L] is what it costs to go from level L to level L+1, so
//    PebbleInstance.xp is always "XP inside the current level" and never a
//    running total. Index 0 is unused (there is no level 0) and index 30 is 0
//    (level 30 is the end of the curve, and xp_add() saturates there).
//
//    The curve is 10 + L*L. It is written out rather than computed so the
//    numbers a designer would move are visible, and so the two static_asserts
//    below are asserting the real table and not a formula:
//      * every entry fits u16 - PebbleInstance.xp is u16 (plan 1.5.1);
//      * the WHOLE curve sums to 8,845, which also fits u16, so any code that
//        wants a lifetime total can hold one without a wider type.
// =============================================================================
#define XP_LEVEL_MAX            30

inline constexpr uint16_t XP_TABLE[XP_LEVEL_MAX + 1] = {
  /*  0 */    0,   // no level 0
  /*  1 */   11, /*  2 */   14, /*  3 */   19, /*  4 */   26, /*  5 */   35,
  /*  6 */   46, /*  7 */   59, /*  8 */   74, /*  9 */   91, /* 10 */  110,
  /* 11 */  131, /* 12 */  154, /* 13 */  179, /* 14 */  206, /* 15 */  235,
  /* 16 */  266, /* 17 */  299, /* 18 */  334, /* 19 */  371, /* 20 */  410,
  /* 21 */  451, /* 22 */  494, /* 23 */  539, /* 24 */  586, /* 25 */  635,
  /* 26 */  686, /* 27 */  739, /* 28 */  794, /* 29 */  851,
  /* 30 */    0    // the top of the curve: nothing left to buy
};

// Compile-time shape checks (plan 1.5.2 "generator-emitted compile-time
// guards"): strictly increasing over 1..29, and the whole curve inside u16.
inline constexpr uint32_t xp_table_total(void) {
  uint32_t t = 0;
  for (uint8_t i = 0; i <= (uint8_t)XP_LEVEL_MAX; ++i) t += XP_TABLE[i];
  return t;
}
inline constexpr bool xp_table_monotonic(void) {
  for (uint8_t i = 1; i + 1 < (uint8_t)XP_LEVEL_MAX; ++i) {
    if (XP_TABLE[i + 1] <= XP_TABLE[i]) return false;
  }
  return XP_TABLE[0] == 0 && XP_TABLE[XP_LEVEL_MAX] == 0;
}
static_assert(xp_table_monotonic(),
              "XP_TABLE must rise strictly over levels 1..29 and be 0 at both ends");
static_assert(xp_table_total() < 65535u,
              "the whole XP curve must fit u16 (PebbleInstance.xp is u16)");

// --- what each source is worth ----------------------------------------------
// A CARE ACTION is worth XP_CARE_ACTION, and EVERY care action pays: feeding,
// cleaning, medicine, petting and playing alike. That is deliberate. A meal is
// refused above ACT_MEAL_REFUSE_PCT (90 % satiety) and hunger only falls at
// 4,200 milli/h, so FEED_MEAL is available roughly once every 2.4 h and could
// never spend an hourly budget on its own.
#define XP_CARE_ACTION          2

// A minigame pays permille * XP_MINIGAME_NUM / 1000, i.e. 0..8 for a run.
#define XP_MINIGAME_NUM         8
#define XP_MINIGAME_DEN         1000

// Carried time: +XP_CARRY_STEP_XP for every XP_CARRY_STEP_S seconds the active
// Pebble spends AWAKE with the device on. Asleep time and time in the Box pay
// nothing - what is rewarded is carrying the thing around.
#define XP_CARRY_STEP_S         600UL
#define XP_CARRY_STEP_XP        1

// --- the anti-farm ledger ----------------------------------------------------
// Same shape as the hourly gain ceiling of section 3, and the same reasoning:
// the budget belongs to the DEVICE and to real time, so swapping the active
// Pebble is worthless as a farming move, and it refills continuously rather
// than resetting on a boundary, so a reboot cannot refill it either.
//
// Each metered source has a cap in whole XP and a window in seconds; the
// refill step is window / cap, and the static_assert below pins that the
// division is exact so no XP is lost to rounding over a full window.
#define XP_CAP_CARE             10          // per hour  (5 care actions)
#define XP_WIN_CARE_S           3600UL
#define XP_CAP_MINIGAME         16          // per hour  (2 perfect runs)
#define XP_WIN_MINIGAME_S       3600UL
#define XP_CAP_CARRY            48          // per day   (8 h of carrying)
#define XP_WIN_CARRY_S          86400UL

static_assert(XP_WIN_CARE_S     % XP_CAP_CARE     == 0, "care refill step is not exact");
static_assert(XP_WIN_MINIGAME_S % XP_CAP_MINIGAME == 0, "minigame refill step is not exact");
static_assert(XP_WIN_CARRY_S    % XP_CAP_CARRY    == 0, "carry refill step is not exact");
static_assert(XP_CAP_CARE < 256 && XP_CAP_MINIGAME < 256 && XP_CAP_CARRY < 256,
              "a ledger bucket is persisted as one byte (Inventory.xp_ledger)");

#endif // PB_BALANCE_H
