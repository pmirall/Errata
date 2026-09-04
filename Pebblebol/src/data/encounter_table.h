// =============================================================================
//  PEBBLEBOL - data/encounter_table.h
//
//  GENERATED FILE. Do not edit: tools/gen_content.py rewrites it from
//  tools/content/*.json. `tools/gen_content.py --check` fails the gate if
//  this file and the JSON have drifted apart.
//
//  THE ENCOUNTER TABLE (plan 1.5.2, spec sections 20 and 22).
//
//  Six network categories x {WILD by rarity band, ITEM, SPECIAL, NOTHING},
//  each category's weights summing to exactly 100. P5-C3 rolls against it.
//
//  ITEM_DROPS is the table plan 1.5.2 does not list and the ITEM outcome
//  cannot resolve without: EncounterRow has no item column, so the drop is
//  picked here, by weight within the network category, then rejected if the
//  item's rarity falls outside the row's rarity_min..rarity_max.
//
//  SPECIAL HAS NO PAYLOAD TABLE AND THAT IS A REAL HOLE. Spec section 22
//  names four outcomes and this table has rows for all four, but the pack
//  carries nothing behind SPECIAL - no event roster, no per-category
//  weights, no ids - while it is 4 to 10 percent of every scan. P5-C3 must
//  define it. Recorded here rather than papered over.
//
//  ROWS CLAMPED FOR THIS ROSTER (36 species): a WILD row whose rarity band
//  holds no species in the shipped prefix is folded down to the highest
//  band the category does carry, so every row resolves. The full
//  60-species roster needs no clamping.
//    PUBLIC   rarity 3..3 -> 2
//    BUSINESS rarity 3..3 -> 2
//    OPEN     rarity 3..3 -> 2
//    HIDDEN   rarity 3..3 -> 2
// =============================================================================

#ifndef PB_ENCOUNTER_TABLE_H
#define PB_ENCOUNTER_TABLE_H

#include <stdint.h>
#include <stddef.h>

#include "network_table.h"
#include "species_table.h"
#include "items_table.h"

// NetCategory and NET_CATEGORY_BIT moved to network_table.h at P5-C1: the
// encoding is the classifier's OUTPUT and this table's INDEX, and it now
// has one owner. What stays here is the cross-check against the roster,
// which network_table.h cannot see.
static_assert((uint8_t)NET_CAT_COUNT == NET_CATEGORY_COUNT,
              "NetCategory and SPECIES_SPAWN_SUM disagree on the category count");

enum EncounterOutcome : uint8_t {
  ENC_OUT_WILD,
  ENC_OUT_ITEM,
  ENC_OUT_SPECIAL,
  ENC_OUT_NOTHING,
  ENC_OUT_COUNT
};

struct EncounterRow {       // 8 B, plan 1.5.2
  uint8_t category;         // NetCategory ORDINAL
  uint8_t outcome;          // EncounterOutcome
  uint8_t weight;           // per-category weights sum to exactly 100
  uint8_t rarity_min;       // WILD: the species rarity band this row draws from
  uint8_t rarity_max;
  uint8_t reserved[3];      // must be 0
};
static_assert(sizeof(EncounterRow) == 8, "EncounterRow layout drifted");

inline constexpr EncounterRow ENCOUNTER_TABLE[] = {
  //  category            outcome        wt  rarity
  { NET_CAT_UNKNOWN,  ENC_OUT_WILD,      38, 0, 0, { 0, 0, 0 } },
  { NET_CAT_UNKNOWN,  ENC_OUT_WILD,      10, 1, 1, { 0, 0, 0 } },
  { NET_CAT_UNKNOWN,  ENC_OUT_WILD,       2, 2, 2, { 0, 0, 0 } },
  { NET_CAT_UNKNOWN,  ENC_OUT_ITEM,      14, 0, 1, { 0, 0, 0 } },
  { NET_CAT_UNKNOWN,  ENC_OUT_SPECIAL,    4, 0, 0, { 0, 0, 0 } },
  { NET_CAT_UNKNOWN,  ENC_OUT_NOTHING,   32, 0, 0, { 0, 0, 0 } },
  { NET_CAT_HOME,     ENC_OUT_WILD,      40, 0, 0, { 0, 0, 0 } },
  { NET_CAT_HOME,     ENC_OUT_WILD,      12, 1, 1, { 0, 0, 0 } },
  { NET_CAT_HOME,     ENC_OUT_WILD,       2, 2, 2, { 0, 0, 0 } },
  { NET_CAT_HOME,     ENC_OUT_ITEM,      15, 0, 1, { 0, 0, 0 } },
  { NET_CAT_HOME,     ENC_OUT_SPECIAL,    4, 0, 0, { 0, 0, 0 } },
  { NET_CAT_HOME,     ENC_OUT_NOTHING,   27, 0, 0, { 0, 0, 0 } },
  { NET_CAT_PUBLIC,   ENC_OUT_WILD,      36, 0, 0, { 0, 0, 0 } },
  { NET_CAT_PUBLIC,   ENC_OUT_WILD,      17, 1, 1, { 0, 0, 0 } },
  { NET_CAT_PUBLIC,   ENC_OUT_WILD,       4, 2, 2, { 0, 0, 0 } },
  { NET_CAT_PUBLIC,   ENC_OUT_WILD,       1, 2, 2, { 0, 0, 0 } },
  { NET_CAT_PUBLIC,   ENC_OUT_ITEM,      14, 0, 1, { 0, 0, 0 } },
  { NET_CAT_PUBLIC,   ENC_OUT_SPECIAL,    5, 0, 0, { 0, 0, 0 } },
  { NET_CAT_PUBLIC,   ENC_OUT_NOTHING,   23, 0, 0, { 0, 0, 0 } },
  { NET_CAT_BUSINESS, ENC_OUT_WILD,      25, 0, 0, { 0, 0, 0 } },
  { NET_CAT_BUSINESS, ENC_OUT_WILD,      21, 1, 1, { 0, 0, 0 } },
  { NET_CAT_BUSINESS, ENC_OUT_WILD,       8, 2, 2, { 0, 0, 0 } },
  { NET_CAT_BUSINESS, ENC_OUT_WILD,       2, 2, 2, { 0, 0, 0 } },
  { NET_CAT_BUSINESS, ENC_OUT_ITEM,      16, 1, 2, { 0, 0, 0 } },
  { NET_CAT_BUSINESS, ENC_OUT_SPECIAL,    6, 0, 0, { 0, 0, 0 } },
  { NET_CAT_BUSINESS, ENC_OUT_NOTHING,   22, 0, 0, { 0, 0, 0 } },
  { NET_CAT_OPEN,     ENC_OUT_WILD,      33, 0, 0, { 0, 0, 0 } },
  { NET_CAT_OPEN,     ENC_OUT_WILD,      16, 1, 1, { 0, 0, 0 } },
  { NET_CAT_OPEN,     ENC_OUT_WILD,       5, 2, 2, { 0, 0, 0 } },
  { NET_CAT_OPEN,     ENC_OUT_WILD,       2, 2, 2, { 0, 0, 0 } },
  { NET_CAT_OPEN,     ENC_OUT_ITEM,      18, 0, 2, { 0, 0, 0 } },
  { NET_CAT_OPEN,     ENC_OUT_SPECIAL,    5, 0, 0, { 0, 0, 0 } },
  { NET_CAT_OPEN,     ENC_OUT_NOTHING,   21, 0, 0, { 0, 0, 0 } },
  { NET_CAT_HIDDEN,   ENC_OUT_WILD,      12, 0, 0, { 0, 0, 0 } },
  { NET_CAT_HIDDEN,   ENC_OUT_WILD,      17, 1, 1, { 0, 0, 0 } },
  { NET_CAT_HIDDEN,   ENC_OUT_WILD,      13, 2, 2, { 0, 0, 0 } },
  { NET_CAT_HIDDEN,   ENC_OUT_WILD,      12, 2, 2, { 0, 0, 0 } },
  { NET_CAT_HIDDEN,   ENC_OUT_ITEM,      12, 1, 2, { 0, 0, 0 } },
  { NET_CAT_HIDDEN,   ENC_OUT_SPECIAL,   10, 0, 0, { 0, 0, 0 } },
  { NET_CAT_HIDDEN,   ENC_OUT_NOTHING,   24, 0, 0, { 0, 0, 0 } },
};

inline constexpr uint8_t ENCOUNTER_ROW_COUNT =
    (uint8_t)(sizeof(ENCOUNTER_TABLE) / sizeof(ENCOUNTER_TABLE[0]));
#define ENCOUNTER_WEIGHT_TOTAL  100u

struct ItemDropRow {        // 4 B
  uint8_t category;         // NetCategory ORDINAL
  uint8_t item_id;          // into ITEMS_TABLE
  uint8_t weight;           // per-category weights sum to exactly 100
  uint8_t reserved;         // must be 0
};
static_assert(sizeof(ItemDropRow) == 4, "ItemDropRow layout drifted");

inline constexpr ItemDropRow ITEM_DROP_TABLE[] = {
  { NET_CAT_UNKNOWN,    1,  40, 0 },
  { NET_CAT_UNKNOWN,    2,  10, 0 },
  { NET_CAT_UNKNOWN,    4,  25, 0 },
  { NET_CAT_UNKNOWN,    6,  20, 0 },
  { NET_CAT_UNKNOWN,    8,   5, 0 },
  { NET_CAT_HOME,       1,  35, 0 },
  { NET_CAT_HOME,       2,  10, 0 },
  { NET_CAT_HOME,       4,  20, 0 },
  { NET_CAT_HOME,       6,  30, 0 },
  { NET_CAT_HOME,       7,   5, 0 },
  { NET_CAT_PUBLIC,     1,  30, 0 },
  { NET_CAT_PUBLIC,     2,  15, 0 },
  { NET_CAT_PUBLIC,     4,  30, 0 },
  { NET_CAT_PUBLIC,     6,  15, 0 },
  { NET_CAT_PUBLIC,    10,  10, 0 },
  { NET_CAT_BUSINESS,   2,  25, 0 },
  { NET_CAT_BUSINESS,   3,  10, 0 },
  { NET_CAT_BUSINESS,   5,  15, 0 },
  { NET_CAT_BUSINESS,   7,  10, 0 },
  { NET_CAT_BUSINESS,   8,  20, 0 },
  { NET_CAT_BUSINESS,  10,  20, 0 },
  { NET_CAT_OPEN,       1,  30, 0 },
  { NET_CAT_OPEN,       2,  15, 0 },
  { NET_CAT_OPEN,       4,  25, 0 },
  { NET_CAT_OPEN,       6,  15, 0 },
  { NET_CAT_OPEN,       8,   5, 0 },
  { NET_CAT_OPEN,      10,  10, 0 },
  { NET_CAT_HIDDEN,     2,  10, 0 },
  { NET_CAT_HIDDEN,     3,  15, 0 },
  { NET_CAT_HIDDEN,     5,  20, 0 },
  { NET_CAT_HIDDEN,     7,  25, 0 },
  { NET_CAT_HIDDEN,     8,  10, 0 },
  { NET_CAT_HIDDEN,     9,  20, 0 },
};

inline constexpr uint8_t ITEM_DROP_ROW_COUNT =
    (uint8_t)(sizeof(ITEM_DROP_TABLE) / sizeof(ITEM_DROP_TABLE[0]));

// --- generator-emitted compile-time guards (plan 1.5.2) ----------------------
constexpr bool encounter_rows_are_well_formed(void) {
  for (uint8_t i = 0; i < (uint8_t)(sizeof(ENCOUNTER_TABLE) / sizeof(ENCOUNTER_TABLE[0])); ++i) {
    const EncounterRow& r = ENCOUNTER_TABLE[i];
    if (r.category >= (uint8_t)NET_CAT_COUNT)      return false;
    if (r.outcome >= (uint8_t)ENC_OUT_COUNT)       return false;
    if (r.weight == 0u)                            return false;
    if (r.rarity_min > r.rarity_max)               return false;
    if (r.rarity_max > SPECIES_RARITY_SPECIAL)     return false;
    if (r.reserved[0] != 0u || r.reserved[1] != 0u || r.reserved[2] != 0u) return false;
  }
  return true;
}

// Every category's weights sum to exactly 100, so a roll is a plain 0..99 draw
// with no normalisation step to get wrong.
constexpr bool encounter_weights_sum_to_100(void) {
  for (uint8_t c = 0; c < (uint8_t)NET_CAT_COUNT; ++c) {
    uint32_t sum = 0;
    for (uint8_t i = 0; i < ENCOUNTER_ROW_COUNT; ++i)
      if (ENCOUNTER_TABLE[i].category == c) sum += ENCOUNTER_TABLE[i].weight;
    if (sum != ENCOUNTER_WEIGHT_TOTAL) return false;
  }
  return true;
}

// Spec section 22: NOTHING is at least 15 % of every scan, so exploring can
// come back empty and the player learns the device is not a slot machine.
#define ENCOUNTER_NOTHING_MIN_PCT  15u
constexpr bool encounter_nothing_is_common_enough(void) {
  for (uint8_t c = 0; c < (uint8_t)NET_CAT_COUNT; ++c) {
    uint32_t sum = 0;
    for (uint8_t i = 0; i < ENCOUNTER_ROW_COUNT; ++i)
      if (ENCOUNTER_TABLE[i].category == c &&
          ENCOUNTER_TABLE[i].outcome == (uint8_t)ENC_OUT_NOTHING)
        sum += ENCOUNTER_TABLE[i].weight;
    if (sum < ENCOUNTER_NOTHING_MIN_PCT) return false;
  }
  return true;
}

// EVERY WILD ROW RESOLVES TO A NON-EMPTY SPECIES POOL. This is the guard the
// generator's rarity clamp exists to keep true: a WILD row whose band holds no
// species in the shipped roster is an outcome the picker cannot answer.
constexpr bool encounter_wild_rows_have_a_pool(void) {
  for (uint8_t i = 0; i < ENCOUNTER_ROW_COUNT; ++i) {
    const EncounterRow& r = ENCOUNTER_TABLE[i];
    if (r.outcome != (uint8_t)ENC_OUT_WILD) continue;
    uint32_t weight = 0;
    for (uint8_t s = 0; s < SPECIES_TABLE_COUNT; ++s) {
      const SpeciesDef& sp = SPECIES_TABLE[s];
      if ((sp.category_mask & NET_CATEGORY_BIT[r.category]) == 0u) continue;
      if (sp.rarity < r.rarity_min || sp.rarity > r.rarity_max) continue;
      weight += sp.spawn_weight;
    }
    if (weight == 0u) return false;
  }
  return true;
}

constexpr bool item_drop_rows_are_well_formed(void) {
  for (uint8_t i = 0; i < (uint8_t)(sizeof(ITEM_DROP_TABLE) / sizeof(ITEM_DROP_TABLE[0])); ++i) {
    const ItemDropRow& d = ITEM_DROP_TABLE[i];
    if (d.category >= (uint8_t)NET_CAT_COUNT) return false;
    if (d.item_id < 1u || d.item_id > ITEM_COUNT) return false;
    if (d.weight == 0u) return false;
    if (d.reserved != 0u) return false;
  }
  for (uint8_t c = 0; c < (uint8_t)NET_CAT_COUNT; ++c) {
    uint32_t sum = 0;
    for (uint8_t i = 0; i < ITEM_DROP_ROW_COUNT; ++i)
      if (ITEM_DROP_TABLE[i].category == c) sum += ITEM_DROP_TABLE[i].weight;
    if (sum != ENCOUNTER_WEIGHT_TOTAL) return false;
  }
  return true;
}

static_assert(ENCOUNTER_ROW_COUNT >= 1, "the encounter table is empty");
static_assert(encounter_rows_are_well_formed(),
              "an encounter row has a bad category, outcome, weight or rarity band");
static_assert(encounter_weights_sum_to_100(),
              "a network category's encounter weights do not sum to 100");
static_assert(encounter_nothing_is_common_enough(),
              "a network category finds something too often (spec 22: NOTHING >= 15 %)");
static_assert(encounter_wild_rows_have_a_pool(),
              "a WILD encounter row draws from a rarity band no shipped species is in");
static_assert(item_drop_rows_are_well_formed(),
              "an item drop row has a bad category or item, or a category does not sum to 100");

#endif // PB_ENCOUNTER_TABLE_H
