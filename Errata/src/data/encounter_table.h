// =============================================================================
//  ERRATA - data/encounter_table.h
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
//  SPECIAL_EVENTS is the SAME SHAPE for the outcome that had NO payload
//  at all until P5-C3 (tools/content/specials.json). It is 4 to 10 percent
//  of every scan and the pack used to carry no roster, no ids and no
//  weights behind it. Now: an event roster indexed by id == index + 1, and
//  SPECIAL_DROP_TABLE, per-category weights summing to 100, picked exactly
//  the way an item drop is.
//
//  AND BOTH TWO-STAGE PICKS ARE GUARDED, which only WILD's was. Every ITEM
//  row's rarity band is checked against ITS OWN category's drop list and
//  every SPECIAL row against its own category's event list, at compile
//  time, next to encounter_wild_rows_have_a_pool() - see the three guards
//  at the foot of this file.
// =============================================================================

#ifndef ER_ENCOUNTER_TABLE_H
#define ER_ENCOUNTER_TABLE_H

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
  { NET_CAT_PUBLIC,   ENC_OUT_WILD,       1, 3, 3, { 0, 0, 0 } },
  { NET_CAT_PUBLIC,   ENC_OUT_ITEM,      14, 0, 1, { 0, 0, 0 } },
  { NET_CAT_PUBLIC,   ENC_OUT_SPECIAL,    5, 0, 0, { 0, 0, 0 } },
  { NET_CAT_PUBLIC,   ENC_OUT_NOTHING,   23, 0, 0, { 0, 0, 0 } },
  { NET_CAT_BUSINESS, ENC_OUT_WILD,      25, 0, 0, { 0, 0, 0 } },
  { NET_CAT_BUSINESS, ENC_OUT_WILD,      21, 1, 1, { 0, 0, 0 } },
  { NET_CAT_BUSINESS, ENC_OUT_WILD,       8, 2, 2, { 0, 0, 0 } },
  { NET_CAT_BUSINESS, ENC_OUT_WILD,       2, 3, 3, { 0, 0, 0 } },
  { NET_CAT_BUSINESS, ENC_OUT_ITEM,      16, 1, 2, { 0, 0, 0 } },
  { NET_CAT_BUSINESS, ENC_OUT_SPECIAL,    6, 0, 0, { 0, 0, 0 } },
  { NET_CAT_BUSINESS, ENC_OUT_NOTHING,   22, 0, 0, { 0, 0, 0 } },
  { NET_CAT_OPEN,     ENC_OUT_WILD,      33, 0, 0, { 0, 0, 0 } },
  { NET_CAT_OPEN,     ENC_OUT_WILD,      16, 1, 1, { 0, 0, 0 } },
  { NET_CAT_OPEN,     ENC_OUT_WILD,       5, 2, 2, { 0, 0, 0 } },
  { NET_CAT_OPEN,     ENC_OUT_WILD,       2, 3, 3, { 0, 0, 0 } },
  { NET_CAT_OPEN,     ENC_OUT_ITEM,      18, 0, 2, { 0, 0, 0 } },
  { NET_CAT_OPEN,     ENC_OUT_SPECIAL,    5, 0, 0, { 0, 0, 0 } },
  { NET_CAT_OPEN,     ENC_OUT_NOTHING,   21, 0, 0, { 0, 0, 0 } },
  { NET_CAT_HIDDEN,   ENC_OUT_WILD,      12, 0, 0, { 0, 0, 0 } },
  { NET_CAT_HIDDEN,   ENC_OUT_WILD,      17, 1, 1, { 0, 0, 0 } },
  { NET_CAT_HIDDEN,   ENC_OUT_WILD,      13, 2, 2, { 0, 0, 0 } },
  { NET_CAT_HIDDEN,   ENC_OUT_WILD,      12, 3, 3, { 0, 0, 0 } },
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

// --- THE SPECIAL PAYLOAD (P5-C3, tools/content/specials.json) ---------------
enum SpecialKind : uint8_t {
  SPEV_XP_BURST,
  SPEV_CORRUPTION,
  SPEV_COUNT
};

// XP_BURST value is scaled by this to get the XP it pays.
#define SPECIAL_XP_SCALE  8

struct SpecialEvent {       // 6 B, padding-free
  uint8_t  id;              // 1..SPECIAL_EVENT_COUNT, contiguous == index + 1
  uint8_t  kind;            // SpecialKind
  uint8_t  value;           // XP_BURST: x SPECIAL_XP_SCALE = XP. CORRUPTION: 0
  uint8_t  reserved;        // must be 0
  uint16_t name_idx;        // StrId of the Spanish event name
};
static_assert(sizeof(SpecialEvent) == 6, "SpecialEvent layout drifted");

inline constexpr SpecialEvent SPECIAL_EVENTS[] = {
  //  id kind           val  rsv name
  {  1, SPEV_XP_BURST,   5, 0, STR_SPECIAL_NAME_1     },   // Caché Suelta
  {  2, SPEV_XP_BURST,  12, 0, STR_SPECIAL_NAME_2     },   // Nodo Fantasma
  {  3, SPEV_XP_BURST,  25, 0, STR_SPECIAL_NAME_3     },   // Núcleo Roto
  {  4, SPEV_CORRUPTION,   0, 0, STR_SPECIAL_NAME_4     },   // Virus Errante
};

inline constexpr uint8_t SPECIAL_EVENT_COUNT =
    (uint8_t)(sizeof(SPECIAL_EVENTS) / sizeof(SPECIAL_EVENTS[0]));

// Byte-for-byte the shape of ItemDropRow, on purpose: the two outcomes that
// need a second stage are resolved by the same walk, so neither picker can
// drift into a rule the other one does not have.
struct SpecialDropRow {     // 4 B
  uint8_t category;         // NetCategory ORDINAL
  uint8_t event_id;         // into SPECIAL_EVENTS
  uint8_t weight;           // per-category weights sum to exactly 100
  uint8_t reserved;         // must be 0
};
static_assert(sizeof(SpecialDropRow) == 4, "SpecialDropRow layout drifted");

inline constexpr SpecialDropRow SPECIAL_DROP_TABLE[] = {
  { NET_CAT_UNKNOWN,    1,  55, 0 },
  { NET_CAT_UNKNOWN,    2,  30, 0 },
  { NET_CAT_UNKNOWN,    4,  15, 0 },
  { NET_CAT_HOME,       1,  60, 0 },
  { NET_CAT_HOME,       2,  30, 0 },
  { NET_CAT_HOME,       4,  10, 0 },
  { NET_CAT_PUBLIC,     1,  50, 0 },
  { NET_CAT_PUBLIC,     2,  35, 0 },
  { NET_CAT_PUBLIC,     4,  15, 0 },
  { NET_CAT_BUSINESS,   1,  35, 0 },
  { NET_CAT_BUSINESS,   2,  40, 0 },
  { NET_CAT_BUSINESS,   3,  10, 0 },
  { NET_CAT_BUSINESS,   4,  15, 0 },
  { NET_CAT_OPEN,       1,  45, 0 },
  { NET_CAT_OPEN,       2,  35, 0 },
  { NET_CAT_OPEN,       4,  20, 0 },
  { NET_CAT_HIDDEN,     1,  20, 0 },
  { NET_CAT_HIDDEN,     2,  30, 0 },
  { NET_CAT_HIDDEN,     3,  20, 0 },
  { NET_CAT_HIDDEN,     4,  30, 0 },
};

inline constexpr uint8_t SPECIAL_DROP_ROW_COUNT =
    (uint8_t)(sizeof(SPECIAL_DROP_TABLE) / sizeof(SPECIAL_DROP_TABLE[0]));

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

// EVERY ITEM ROW RESOLVES TO A NON-EMPTY DROP POOL - the twin of
// encounter_wild_rows_have_a_pool() that did not exist until P5-C3, and the
// reason it had to (plan's carried-forward bullet):
// item_drop_rows_are_well_formed() above checks category, id, weight and the
// per-category sum of 100 and NOTHING pairs a row's own rarity band with its
// own category's drop list - which is the second stage of the pick this table's
// banner describes. It holds at this pack (100 of 100 eligible weight in all
// six categories, so the rejection step never fires), which is exactly why one
// item edit could turn ITEM into an outcome encounter_roll() cannot answer with
// nothing in the FIRMWARE saying so. tools/content/verify.py has checked the
// same property since P4-C1, and a Python gate is skipped when python3 is
// missing (tools/check.sh says so in a word and continues); a static_assert is
// not skippable.
constexpr bool encounter_item_rows_have_a_drop(void) {
  for (uint8_t i = 0; i < ENCOUNTER_ROW_COUNT; ++i) {
    const EncounterRow& r = ENCOUNTER_TABLE[i];
    if (r.outcome != (uint8_t)ENC_OUT_ITEM) continue;
    uint32_t weight = 0;
    for (uint8_t d = 0; d < ITEM_DROP_ROW_COUNT; ++d) {
      if (ITEM_DROP_TABLE[d].category != r.category) continue;
      const ItemDef* it = item_get(ITEM_DROP_TABLE[d].item_id);
      if (it == nullptr) return false;
      if (it->rarity < r.rarity_min || it->rarity > r.rarity_max) continue;
      weight += ITEM_DROP_TABLE[d].weight;
    }
    if (weight == 0u) return false;
  }
  return true;
}

constexpr bool special_drop_rows_are_well_formed(void) {
  for (uint8_t i = 0; i < (uint8_t)(sizeof(SPECIAL_EVENTS) / sizeof(SPECIAL_EVENTS[0])); ++i) {
    const SpecialEvent& e = SPECIAL_EVENTS[i];
    if (e.id != (uint8_t)(i + 1u))              return false;
    if (e.kind >= (uint8_t)SPEV_COUNT)          return false;
    if (e.reserved != 0u)                       return false;
    if (e.name_idx >= (uint16_t)STR_COUNT)      return false;
    // An XP_BURST that pays nothing is an event with no effect - the exact
    // shape of the item-9 hole this phase closed on the other table.
    if (e.kind == (uint8_t)SPEV_XP_BURST && e.value == 0u) return false;
  }
  for (uint8_t i = 0; i < (uint8_t)(sizeof(SPECIAL_DROP_TABLE) / sizeof(SPECIAL_DROP_TABLE[0])); ++i) {
    const SpecialDropRow& d = SPECIAL_DROP_TABLE[i];
    if (d.category >= (uint8_t)NET_CAT_COUNT)   return false;
    if (d.event_id < 1u || d.event_id > SPECIAL_EVENT_COUNT) return false;
    if (d.weight == 0u)                         return false;
    if (d.reserved != 0u)                       return false;
  }
  for (uint8_t c = 0; c < (uint8_t)NET_CAT_COUNT; ++c) {
    uint32_t sum = 0;
    for (uint8_t i = 0; i < SPECIAL_DROP_ROW_COUNT; ++i)
      if (SPECIAL_DROP_TABLE[i].category == c) sum += SPECIAL_DROP_TABLE[i].weight;
    if (sum != ENCOUNTER_WEIGHT_TOTAL) return false;
  }
  return true;
}

// And the same question asked of the outcome that had no payload at all.
constexpr bool encounter_special_rows_have_an_event(void) {
  for (uint8_t i = 0; i < ENCOUNTER_ROW_COUNT; ++i) {
    const EncounterRow& r = ENCOUNTER_TABLE[i];
    if (r.outcome != (uint8_t)ENC_OUT_SPECIAL) continue;
    uint32_t weight = 0;
    for (uint8_t d = 0; d < SPECIAL_DROP_ROW_COUNT; ++d)
      if (SPECIAL_DROP_TABLE[d].category == r.category)
        weight += SPECIAL_DROP_TABLE[d].weight;
    if (weight == 0u) return false;
  }
  return true;
}

// Every kind has an event, so no branch of the SPECIAL switch is unreachable.
constexpr bool special_kinds_are_all_populated(void) {
  for (uint8_t k = 0; k < (uint8_t)SPEV_COUNT; ++k) {
    bool seen = false;
    for (uint8_t i = 0; i < SPECIAL_EVENT_COUNT; ++i)
      if (SPECIAL_EVENTS[i].kind == k) seen = true;
    if (!seen) return false;
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
static_assert(encounter_item_rows_have_a_drop(),
              "an ITEM encounter row draws from a rarity band no item in its own "
              "category's drop list is in: the outcome cannot be answered");
static_assert(special_drop_rows_are_well_formed(),
              "a special event or weight row is malformed, or a category does not sum to 100");
static_assert(encounter_special_rows_have_an_event(),
              "a SPECIAL encounter row has no event in its category: the outcome "
              "cannot be answered");
static_assert(special_kinds_are_all_populated(),
              "a SpecialKind has no event - a branch encounters.cpp could never reach");

#endif // ER_ENCOUNTER_TABLE_H
