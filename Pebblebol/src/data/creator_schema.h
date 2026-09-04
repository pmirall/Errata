// =============================================================================
//  PEBBLEBOL - data/creator_schema.h
//
//  GENERATED FILE. Do not edit: tools/gen_content.py rewrites it from
//  tools/content/*.json. `tools/gen_content.py --check` fails the gate if
//  this file and the JSON have drifted apart.
//
//  THE CREATOR VALIDATION SCHEMA (spec sections 35 and 36, plan line 526).
//
//  One copy of every budget a custom Pebble is measured against, so the
//  on-device validator (P8) and the phone page read the same numbers.
//
//  WHAT IS HERE IS WHAT THE CONTENT PACK CARRIES, AND IT IS NOT ALL OF
//  SPEC SECTION 35. Section 35 names thirteen validation inputs, and all
//  thirteen are accounted for below - 2 + 1 + 2 + 8 - because a header
//  whose job is to say what has no row has to add up.
//
//    TWO HAVE A ROW HERE: the stat budget and the four-move rule, plus
//    the section 36 budget arithmetic they are measured with.
//
//    ONE THIS HEADER CAN STATE: species / custom definition validity, as
//    the custom species id RANGE, because species_table.h already fixes
//    SPECIES_ID_BUILTIN_MAX.
//
//    TWO ARE DERIVABLE FROM TABLES THAT ALREADY SHIP, so P8 does not need
//    a row and must not invent one: TYPE VALIDITY is `type < TYPE_COUNT`
//    (data/attacks_table.h), and MOVE LEGALITY is the own-type-or-NEUTRAL,
//    four-distinct, at-least-one-damaging rule that
//    attacks_table.h::species_learnsets_are_legal() already holds every
//    built-in row to.
//
//    EIGHT HAVE NO ROW ANYWHERE and P8 cannot invent them from this
//    header. They are listed rather than guessed:
//      sprite dimensions and sprite data size
//      palette limits
//      name length and the allowed character set
//      creator payload size
//      the creator protocol version
//      whether a custom Pebble may carry an evolution rule at all
// =============================================================================

#ifndef PB_CREATOR_SCHEMA_H
#define PB_CREATOR_SCHEMA_H

#include <stdint.h>

#include "species_table.h"
#include "attacks_table.h"

// Custom species ids: cs0..cs9 live directly above the built-in roster.
#define CREATOR_SPECIES_ID_MIN   (SPECIES_ID_BUILTIN_MAX + 1u)
#define CREATOR_SPECIES_SLOTS    10u
#define CREATOR_SPECIES_ID_MAX   (SPECIES_ID_BUILTIN_MAX + CREATOR_SPECIES_SLOTS)

// Spec section 36: a custom Pebble is capped at the STAGE-1 budget so it can
// never out-stat a final evolution (spec section 68 r17).
#define CREATOR_TOTAL_STAT_POINTS  22
#define CREATOR_ATTACK_BUDGET      185
#define CREATOR_MOVE_COUNT         PB_MOVE_COUNT
#define CREATOR_BASE_STAT_MIN      1
#define CREATOR_BASE_STAT_MAX      10

// The built-in budgets a custom Pebble is measured against, by stage.
inline constexpr uint8_t CREATOR_STAT_POINTS_BY_STAGE[3] = { 16, 22, 28 };
inline constexpr uint16_t CREATOR_ATTACK_BUDGET_BY_STAGE[3] = { 140, 185, 225 };
inline constexpr uint8_t CREATOR_POWER_CAP_BY_STAGE[3] = { 60, 90, 100 };
inline constexpr uint8_t CREATOR_RARITY_BUDGET_BONUS[SPECIES_RARITY_COUNT] = { 0, 0, 12, 24 };

// A custom Pebble may never be stronger than a stage-1 built-in, and never
// weaker than a stage-0 one. Both halves matter: the first is spec 68 r17, the
// second stops the creator being a way to make deliberately useless trade bait.
static_assert(CREATOR_TOTAL_STAT_POINTS == CREATOR_STAT_POINTS_BY_STAGE[1],
              "the creator stat budget is not the stage-1 budget");
static_assert(CREATOR_ATTACK_BUDGET == CREATOR_ATTACK_BUDGET_BY_STAGE[1],
              "the creator attack budget is not the stage-1 budget");
static_assert(CREATOR_TOTAL_STAT_POINTS >= CREATOR_STAT_POINTS_BY_STAGE[0],
              "the creator stat budget is below a stage-0 built-in");
static_assert(CREATOR_TOTAL_STAT_POINTS < CREATOR_STAT_POINTS_BY_STAGE[2],
              "a custom Pebble could out-stat a final evolution (spec 68 r17)");
static_assert(CREATOR_BASE_STAT_MIN >= 1 && CREATOR_BASE_STAT_MAX <= 10,
              "spec section 11 fixes base stats at 1..10");
static_assert(CREATOR_SPECIES_ID_MAX <= 255u,
              "custom species ids must fit PebbleInstance.species_id");
static_assert(CREATOR_MOVE_COUNT == 4u, "spec section 13: exactly four attacks");

// Every built-in row is inside the budgets the creator is held to for its own
// stage. A roster that breaks its own rules cannot be used to judge a player's.
constexpr bool builtin_rows_respect_the_creator_budgets(void) {
  for (uint8_t i = 0; i < SPECIES_TABLE_COUNT; ++i) {
    const SpeciesDef& sp = SPECIES_TABLE[i];
    // Bounds first, and not because they are expected to fire: species_table.h
    // and attacks_table.h already reject a bad stage, rarity or move id by
    // name. But this function INDEXES all three, and a constexpr read past an
    // array end is not a false return - it is "non-constant condition for
    // static assertion", an error with no sentence in it. If one of those named
    // guards is ever deleted, the failure should still say what broke.
    if (sp.stage >= 3u)                       return false;
    if (sp.rarity >= (uint8_t)SPECIES_RARITY_COUNT) return false;
    for (uint8_t m = 0; m < (uint8_t)PB_MOVE_COUNT; ++m) {
      if (sp.moves[m] < 1u || sp.moves[m] > ATTACK_COUNT) return false;
    }
    const uint16_t total = (uint16_t)sp.base_hp + sp.base_atk + sp.base_def + sp.base_spd;
    if (total != CREATOR_STAT_POINTS_BY_STAGE[sp.stage]) return false;
    if (sp.base_hp  < CREATOR_BASE_STAT_MIN || sp.base_hp  > CREATOR_BASE_STAT_MAX) return false;
    if (sp.base_atk < CREATOR_BASE_STAT_MIN || sp.base_atk > CREATOR_BASE_STAT_MAX) return false;
    if (sp.base_def < CREATOR_BASE_STAT_MIN || sp.base_def > CREATOR_BASE_STAT_MAX) return false;
    if (sp.base_spd < CREATOR_BASE_STAT_MIN || sp.base_spd > CREATOR_BASE_STAT_MAX) return false;

    uint16_t cost = 0;
    uint8_t  cap  = 0;
    for (uint8_t m = 0; m < (uint8_t)PB_MOVE_COUNT; ++m) {
      const AttackDef& a = ATTACKS_TABLE[sp.moves[m] - 1u];
      cost += a.budget_cost;
      if (a.power > cap) cap = a.power;
    }
    if (cost > (uint16_t)(CREATOR_ATTACK_BUDGET_BY_STAGE[sp.stage] +
                          CREATOR_RARITY_BUDGET_BONUS[sp.rarity])) return false;
    if (cap > CREATOR_POWER_CAP_BY_STAGE[sp.stage]) return false;
  }
  return true;
}
static_assert(builtin_rows_respect_the_creator_budgets(),
              "a built-in species breaks the stat total, the attack budget or the "
              "power cap its own stage is held to (or its stage, rarity or a move "
              "id is out of range, which the tables themselves name first)");

#endif // PB_CREATOR_SCHEMA_H
