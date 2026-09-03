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
  -2000,      // CARE_HEALTH       bleed rate, gated by CARE_ZERO_GRACE_S
  -2000,      // CARE_CLEANLINESS  base, before CARE_HYGIENE_POOP_MPH
  -6000       // CARE_ENERGY       awake; asleep uses CARE_ENERGY_ASLEEP_MPH
};

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

#endif // PB_BALANCE_H
