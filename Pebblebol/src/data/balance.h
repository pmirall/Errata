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
//
//    THIS CURVE IS NOT THE CONTENT PACK'S, AND THAT IS A P4-C1 DECISION.
//    tools/content/balance.json ships a second 31-entry XP_TABLE
//    (25 + 12*(L-1) + 4*(L-1)^2, total 36,453), and adopting it would multiply
//    time-to-level-30 by 4.12x. gen_content.py deliberately does NOT emit
//    XP_TABLE: it is not one of the content TABLES of plan 1.5.2, the shipped
//    curve predates the pack (P3-C2), and swapping it moves a recorded pixel
//    golden - screen_home.cpp draws the XP rule at 128*xp/xp_next, and
//    tests/test_screens.cpp sets xp = 2 against XP_TABLE[1], which is 23 px on
//    11 and 10 px on 25. A whole-game pacing change is its own commit with its
//    own re-recorded golden, not a side effect of "regenerate the content".
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
//
// THE TWO ARE NOT EQUALLY STRONG, AND THE WEAK ONE IS LABELLED RATHER THAN
// DRESSED UP. xp_table_monotonic() is the one that catches real edits - it has
// been made to fire three ways (flat at L15, backward at L20, a non-zero
// XP_TABLE[30]). xp_table_total() < 65535 is a FLOOR, not a live guard: the
// shipped curve totals 8,845 and even balance.json's rejected 4.12x curve
// totals 36,453, so no plausible table trips it. It is also not quite the
// quantity the u16 is about - PebbleInstance.xp holds xp toward the NEXT level,
// never the total, and each XP_TABLE entry already fits u16 by its own type, so
// the bound that would matter is max(XP_TABLE[i]) and it is true by
// construction. Kept because a curve whose sum overflows u16 is broken anyway;
// stated because the P4-C1 exit called the pair "stronger" without saying that
// only one half does any work.
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

// =============================================================================
// 5. BATTLE  -- spec sections 11-14, plan 1.5.1, P4-C1
//
//    EVERY NUMBER BELOW IS INTEGER AND EVERY DIVISION TRUNCATES TOWARDS ZERO on
//    non-negative operands, which is what C gives us. There is no remainder
//    accumulator anywhere in this section and there must not be one: the care
//    model carries remainders (care_rem[], xp.cpp's rem_s[]) because it
//    INTEGRATES a rate over time, and a battle does not. Every expression here
//    is evaluated once per round on fresh inputs, so truncation is
//    deterministic and idempotent. Adding an accumulator would make a round's
//    result depend on the rounds before it.
//
//    PROVENANCE. The constants come from tools/content/balance.json, which is
//    the tuned content pack; three of them do NOT and are named as decisions
//    here rather than smuggled in as defaults - see BATTLE_MAX_ROUNDS,
//    BATTLE_TEAM_MAX and XP_BATTLE_WIN.
// =============================================================================

// --- damage (plan 1.5.1) -----------------------------------------------------
//    raw = max(DMG_MIN, power * atk_eff / (def_eff * BATTLE_K))
//    dmg = max(DMG_MIN, raw * TYPE_MUL_NUM[m+1] / TYPE_MUL_DEN[m+1]) + rng(0..2)
// Widest intermediate: power 100 * atk_eff 24 = 2,400, and raw * 5 = 1,710.
// uint16_t suffices; use uint32_t for headroom. No 64-bit anywhere.
#define BATTLE_K                7       // the damage divisor
#define DMG_MIN                 1       // a hit always costs at least one point
#define DMG_RNG_SPAN            3       // rng(0..2), a 3-wide uniform

// THE TYPE MODIFIER IS MULTIPLICATIVE, WHICH SUPERSEDES plan 1.5.1's `+ 2*type_mod`.
// Hits land for 4-8 points, so a flat +-2 is a 50-100 % swing and type decides
// every battle - the exact failure spec section 12 forbids ("type advantage
// matters without deciding every battle automatically"). The content pack
// measured this and shipped x4/5, x1, x5/4 instead, indexed by (type_mod + 1),
// with the additive term scaled to zero. TYPE_MOD_SCALE is kept as a real
// constant rather than deleted so the superseded path is visibly dead and not
// merely absent.
#define TYPE_MOD_SCALE          0
inline constexpr uint8_t TYPE_MUL_NUM[3] = { 4, 1, 5 };   // index (type_mod + 1)
inline constexpr uint8_t TYPE_MUL_DEN[3] = { 5, 1, 4 };
// ...and the edge applies at most this many times per attacker per battle. A
// per-hit edge COMPOUNDS over the ~5 exchanges a fight lasts, so capping the
// hits is the other half of the fix. It is per-battle STATE, not a table: P4-C2
// owns one counter per side and this is its bound.
#define TYPE_MOD_MAX_HITS       1

// --- defence, buffs and accuracy ---------------------------------------------
// Protection HALVES incoming damage with a floor of 1 (plan 1.5.1). The divisor
// and the shift are the SAME rule: use the divisor, so a future value other
// than 2 cannot silently disagree with a >>1 somewhere else.
#define PROTECT_DIVISOR         2

// Buff/debuff stages clamp to +-2. plan 1.5.1 says "buffs +-1 stage"; the
// content pack ships a two-stage move (attack 16 Frenesi, effect_value 2), so
// +-1 is the plan's assumption and +-2 is what the content actually needs.
// stat_eff = max(1, (int)stat + stage), computed SIGNED: stat is uint8_t and
// stage can be negative, and an unsigned subtraction there wraps to 65535 and
// looks like an invincible Pebble.
#define BUFF_STAGE_MIN          (-2)
#define BUFF_STAGE_MAX          (+2)
#define STAT_EFF_MIN            1

// SPD buys evasion, not just a tiebreak. Without this SPD does nothing but
// break turn order and a fast Pebble pays 6-7 of its 16/22/28 stat points for
// it (the pack measured the FAST archetype at a 24 % win rate).
//    pen     = EVASION_PER_SPD * min(EVASION_MAX_SPD_GAP, max(0, spd_def - spd_atk))
//    acc_eff = max(ACCURACY_MIN, (int16_t)accuracy - pen)     // SIGNED
//    hit     = rng(0..99) < acc_eff
#define EVASION_PER_SPD         2
#define EVASION_MAX_SPD_GAP     10
#define ACCURACY_MIN            40
#define ACCURACY_ROLL_SPAN      100

// Risk/reward moves pay through RECOIL_PCT and SELF_STUN, not through a flat
// self-damage percentage. The constant is 0 and is named so the absence is a
// stated design choice rather than a missing line.
#define RISK_SELF_HP_PCT        0

// --- corruption in battle (spec section 55) ----------------------------------
#define CORRUPT_BATTLE_ATK_STAGE      (+1)
#define CORRUPT_BATTLE_DEF_STAGE      (-1)
#define CORRUPT_BATTLE_INFECT_PERMILLE 120
#define CORRUPT_DURATION_S            86400UL

// --- THE THREE NUMBERS THE CONTENT PACK DOES NOT CARRY -----------------------
// Each of these is a P4-C1 DECISION, written down because the next reader will
// look for its source and not find one in balance.json.
//
// 1. THE ROUND CAP. Spec section 14 step 8 says "determine end of round" and
//    nothing anywhere states when a battle that neither side can win ends. The
//    pack's simulator used 60 rounds for 3v3 and 40 for 1v1 as Python defaults
//    it never promoted. 60 is taken here for both, because a device battle MUST
//    terminate and the smaller number would end fights the simulator counted as
//    wins. The timeout is broken by remaining HP as a FRACTION of each side's
//    maximum, cross-multiplied so no float and no ratio is needed:
//        A wins if hp_a * hp_max_b > hp_b * hp_max_a
//    and an exact tie is a DRAW. The simulator broke that tie with a coin flip;
//    a coin flip is not a rule anybody stated and it makes a replay depend on
//    the RNG stream position, which P4-C5's lockstep protocol cannot afford.
#define BATTLE_MAX_ROUNDS       60

// 2. THE TEAM SIZE. Spec section 14 says "up to 3 Pebbles" in prose and names
//    no constant. One active per side; a switch costs the turn.
#define BATTLE_TEAM_MAX         3

// 3. WHAT A WIN PAYS. Plan line 487 says "+25" and no table carries it.
//    game/xp.h already reserves XP_SRC_BATTLE with a {0,0} ledger row.
#define XP_BATTLE_WIN           25

static_assert(BATTLE_K > 0, "the damage divisor must not be zero");
static_assert(DMG_RNG_SPAN > 0, "the damage roll needs a range");
static_assert(PROTECT_DIVISOR >= 2, "protection that does not at least halve is not protection");
static_assert(BUFF_STAGE_MIN < 0 && BUFF_STAGE_MAX > 0, "stages must go both ways");
static_assert(ACCURACY_MIN > 0 && ACCURACY_MIN <= ACCURACY_ROLL_SPAN,
              "a move nothing can ever land is not a move");
static_assert(EVASION_PER_SPD * EVASION_MAX_SPD_GAP < ACCURACY_ROLL_SPAN,
              "evasion could drive accuracy below zero before the ACCURACY_MIN floor");
static_assert(TYPE_MUL_NUM[1] == 1 && TYPE_MUL_DEN[1] == 1,
              "a neutral matchup must be exactly x1");
static_assert(TYPE_MUL_NUM[0] < TYPE_MUL_DEN[0] && TYPE_MUL_NUM[2] > TYPE_MUL_DEN[2],
              "the type multipliers are the wrong way round");
static_assert(BATTLE_MAX_ROUNDS > 0, "a battle must terminate");
static_assert(BATTLE_TEAM_MAX >= 1, "a team needs a Pebble");

// =============================================================================
// 6. DAYLIGHT AND SLEEP  -- when it is dark, and what wakes the creature
//    (plan P3-C2b, decision D13)
//
//    THIS TABLE IS AN APPROXIMATION AND SAYS SO. A true sunrise needs a
//    latitude; there is no latitude anywhere in this firmware and there is not
//    going to be one (it went with the weather module, and spec section 44 is
//    explicit that the Wi-Fi scanner is a sensor, not a geolocator). What the
//    device has is the local wall clock and the day of the year, so
//    game/daylight.cpp interpolates the twelve pairs below by day of year and
//    calls the result sunrise and sunset.
//
//    The values describe mid-northern latitudes - about 40 deg N, peninsular
//    Spain, where the default CFG_TZ_STRING points - in LOCAL OFFICIAL TIME.
//    The TZ string has ALREADY applied daylight saving, so nothing downstream
//    may apply it again; that is why the March -> April and the
//    October -> November steps below are an hour wide. A player at another
//    latitude sees a drift (too early a bedtime in a Nordic June, too much
//    seasonal swing near the equator). That cost was accepted with the
//    mechanic: it is a deliberate approximation, not an oversight.
//
//        month  sunrise  sunset        month  sunrise  sunset
//        Jan     08:35    18:00        Jul     07:00    21:40
//        Feb     08:10    18:35        Aug     07:30    21:10
//        Mar     07:25    19:10        Sep     08:00    20:20
//        Apr     07:30    21:00        Oct     08:30    19:30
//        May     06:50    21:30        Nov     08:10    17:55
//        Jun     06:45    21:45        Dec     08:30    17:50
//
//    Each pair is anchored at the MIDDLE of its month and interpolated
//    linearly to its neighbours, December wrapping into January, so the curve
//    is continuous every day of the year - a table read as one constant per
//    month would jump by half an hour on the 1st.
// =============================================================================
#define PB_DAYLIGHT_MONTHS      12
#define PB_MINUTES_PER_DAY      1440u

// Day of year (0-based, non-leap) of the 15th of each month: the anchor each
// sample below is taken at.
inline constexpr uint16_t DAYLIGHT_ANCHOR_DOY[PB_DAYLIGHT_MONTHS] = {
   14,  45,  73, 104, 134, 165, 195, 226, 257, 287, 318, 348
};
// Minutes from local midnight.
inline constexpr uint16_t DAYLIGHT_SUNRISE_MIN[PB_DAYLIGHT_MONTHS] = {
  515, 490, 445, 450, 410, 405, 420, 450, 480, 510, 490, 510
};
inline constexpr uint16_t DAYLIGHT_SUNSET_MIN[PB_DAYLIGHT_MONTHS] = {
 1080,1115,1150,1260,1290,1305,1300,1270,1220,1170,1075,1070
};

// Nothing falls asleep the instant the sun sets. Bedtime is sunset plus this,
// and the creature wakes at sunrise.
#define SLEEP_AFTER_DUSK_MIN    90

// INSISTENCE WAKES IT (spec section 27 forbids punishment, and a player who
// wants to play at 23:00 should be able to). While the creature is asleep the
// first gesture does not act: it shows that the pet is asleep and counts as
// one nudge. WAKE_NUDGES gestures inside WAKE_NUDGE_WINDOW_S wake it, and the
// window is what stops idle taps hours apart from ever accumulating. Waking
// costs NO stat: energy already drains while awake, which is cost enough.
#define WAKE_NUDGES             3
#define WAKE_NUDGE_WINDOW_S     10u

// ...and it goes back to sleep on its own after this long with no interaction,
// as long as the night window is still open. The same number decides when it
// first goes to bed: at dusk it waits until the player has been quiet this
// long, so bedtime never interrupts somebody mid-caress.
#define SLEEP_RELAPSE_S         300u

#endif // PB_BALANCE_H
