// =============================================================================
//  PEBBLEBOL - data/items_table.h
//
//  GENERATED FILE. Do not edit: tools/gen_content.py rewrites it from
//  tools/content/*.json. `tools/gen_content.py --check` fails the gate if
//  this file and the JSON have drifted apart.
//
//  THE ITEM TABLE (plan 1.5.2, spec section 24).
//
//  ItemDef.value is a MAGNITUDE whose unit depends on klass, which is the
//  pack's own contract (balance.json _ITEM_VALUE_doc):
//    XP_CANDY    value * ITEM_XP_CANDY_SCALE = XP granted
//    CAPTURE     value = capture-chance bonus in permille
//    CARE        value = care points restored
//    BATTLE_MOD  value = stat stages granted at battle start
//
//  TWO OF THOSE FOUR UNITS NAME NO TARGET, and the pack has no column for
//  one: a CARE item does not say WHICH of the five care stats it restores,
//  and a BATTLE_MOD does not say which stat it buffs. P5-C4 and P4-C2
//  cannot resolve them from this table alone. Recorded here rather than
//  guessed - see the completeness pass in the P4-C1 exit.
//
//  A THIRD, SMALLER HOLE OF THE SAME SHAPE, found the same way and
//  left in the content rather than patched here:
//    item 9 Llave Raíz is CARE with value 0 - a care item that
//    restores nothing by the contract above. Its real role is
//    the EVOC_ITEM key: evolution.json turns species 53
//    into 54 with cond ITEM, cond_value 9.
//    Spec section 24 names no evolution-item class, so the pack
//    folded it onto CARE. P5-C4 must not read it as a care item.
// =============================================================================

#ifndef PB_ITEMS_TABLE_H
#define PB_ITEMS_TABLE_H

#include <stdint.h>
#include <stddef.h>

#include "species_table.h"        // SPECIES_RARITY_*
#include "../core/strings_es.h"   // StrId: name_idx

enum ItemKlass : uint8_t {
  ITEM_KLASS_XP_CANDY,
  ITEM_KLASS_CAPTURE,
  ITEM_KLASS_CARE,
  ITEM_KLASS_BATTLE_MOD,
  ITEM_KLASS_COUNT
};

// XP_CANDY value is scaled by this to get the XP it grants.
#define ITEM_XP_CANDY_SCALE  8

struct ItemDef {            // 8 B, plan 1.5.2
  uint8_t  id;              // 1..ITEM_COUNT, contiguous == index + 1
  uint8_t  klass;           // ItemKlass
  uint8_t  value;           // magnitude, unit per klass (see the banner)
  uint8_t  rarity;          // SPECIES_RARITY_*
  uint16_t name_idx;        // StrId of the Spanish item name
  uint8_t  reserved[2];     // must be 0
};
static_assert(sizeof(ItemDef) == 8, "ItemDef layout drifted");

inline constexpr ItemDef ITEMS_TABLE[] = {
  //  id klass                 val rarity                    name
  {  1, ITEM_KLASS_XP_CANDY,    5, SPECIES_RARITY_COMMON,    STR_ITEM_NAME_1,   { 0, 0 } },   // Bit Dulce
  {  2, ITEM_KLASS_XP_CANDY,   24, SPECIES_RARITY_UNCOMMON,  STR_ITEM_NAME_2,   { 0, 0 } },   // Byte Dulce
  {  3, ITEM_KLASS_XP_CANDY,  100, SPECIES_RARITY_RARE,      STR_ITEM_NAME_3,   { 0, 0 } },   // Megadulce
  {  4, ITEM_KLASS_CAPTURE,    15, SPECIES_RARITY_COMMON,    STR_ITEM_NAME_4,   { 0, 0 } },   // Cebo
  {  5, ITEM_KLASS_CAPTURE,    45, SPECIES_RARITY_RARE,      STR_ITEM_NAME_5,   { 0, 0 } },   // Jaula Hash
  {  6, ITEM_KLASS_CARE,       40, SPECIES_RARITY_COMMON,    STR_ITEM_NAME_6,   { 0, 0 } },   // Parche
  {  7, ITEM_KLASS_CARE,      100, SPECIES_RARITY_UNCOMMON,  STR_ITEM_NAME_7,   { 0, 0 } },   // Antivirus
  {  8, ITEM_KLASS_BATTLE_MOD,   1, SPECIES_RARITY_UNCOMMON,  STR_ITEM_NAME_8,   { 0, 0 } },   // Turbo Chip
  {  9, ITEM_KLASS_CARE,        0, SPECIES_RARITY_RARE,      STR_ITEM_NAME_9,   { 0, 0 } },   // Llave Raíz
  { 10, ITEM_KLASS_BATTLE_MOD,   1, SPECIES_RARITY_UNCOMMON,  STR_ITEM_NAME_10,  { 0, 0 } },   // Escudo RAM
};

inline constexpr uint8_t ITEM_COUNT =
    (uint8_t)(sizeof(ITEMS_TABLE) / sizeof(ITEMS_TABLE[0]));

constexpr bool item_rows_are_well_formed(void) {
  for (uint8_t i = 0; i < (uint8_t)(sizeof(ITEMS_TABLE) / sizeof(ITEMS_TABLE[0])); ++i) {
    const ItemDef& it = ITEMS_TABLE[i];
    if (it.id != (uint8_t)(i + 1u))                     return false;
    if (it.klass >= (uint8_t)ITEM_KLASS_COUNT)          return false;
    if (it.rarity > SPECIES_RARITY_SPECIAL)             return false;
    if (it.name_idx >= (uint16_t)STR_COUNT)             return false;
    if (it.reserved[0] != 0u || it.reserved[1] != 0u)   return false;
  }
  return true;
}

// Every one of spec section 24's four classes has at least one row. A class
// with no item is a menu entry the player can never fill.
constexpr bool item_klasses_are_all_populated(void) {
  for (uint8_t k = 0; k < (uint8_t)ITEM_KLASS_COUNT; ++k) {
    bool seen = false;
    for (uint8_t i = 0; i < ITEM_COUNT; ++i)
      if (ITEMS_TABLE[i].klass == k) seen = true;
    if (!seen) return false;
  }
  return true;
}

static_assert(ITEM_COUNT >= 1, "the item table is empty");
static_assert(item_rows_are_well_formed(),
              "an item row has a bad id, klass, rarity or string index");
static_assert(item_klasses_are_all_populated(),
              "a spec section 24 item class has no row");

inline const ItemDef* item_get(uint8_t id) {
  if (id < 1u || id > ITEM_COUNT) return nullptr;
  return &ITEMS_TABLE[id - 1u];
}

#endif // PB_ITEMS_TABLE_H
