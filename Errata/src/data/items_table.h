// =============================================================================
//  ERRATA - data/items_table.h
//
//  GENERATED FILE. Do not edit: tools/gen_content.py rewrites it from
//  tools/content/*.json. `tools/gen_content.py --check` fails the gate if
//  this file and the JSON have drifted apart.
//
//  THE ITEM TABLE (plan 1.5.2, spec section 24).
//
//  THREE COLUMNS, ONE CONTRACT. value is a MAGNITUDE, target is WHAT it
//  acts on and param is the one extra byte a klass needs; all three are
//  klass-scoped and balance.json (_ITEM_VALUE_doc, _ITEM_TARGET_doc) is
//  where the contract lives:
//
//    klass       value                        target        param
//    XP_CANDY    x ITEM_XP_CANDY_SCALE = XP   -             -
//    CAPTURE     x ITEM_CAPTURE_SCALE = the   -             -
//                capture bonus in permille
//    CARE        PERCENT of a full care bar   CareTarget    status bits
//                                             (ALL = five)  it clears
//    BATTLE_MOD  stat stages at battle start  BattleStat    rounds
//    EVOLUTION   -                            -             -
//
//  THE TWO UNITS THAT NAMED NO TARGET ARE CLOSED (P5-C4, plan line 552).
//  A CARE row now names one of the five care stats or ALL, and a
//  BATTLE_MOD row names the stat AND how many rounds it lasts. The
//  CAPTURE unit was stated THREE incompatible ways by the pack (the
//  generated banner said permille, balance.json's own worked example said
//  permille/10, the Spanish flavour line said percent); it is settled as
//  a NUMBER below - ITEM_CAPTURE_SCALE - so prose can no longer disagree
//  with prose. The player-facing +15 % / +45 % won.
//
//  THE FIFTH CLASS, AND WHY SECTION 24's FOUR WERE NOT ENOUGH.
//    item 9 Llave Raíz is ITEM_KLASS_EVOLUTION: it restores
//    nothing and buffs nothing, it is a KEY.
//    evolution.json turns species 53 into 54 on
//    cond ITEM, cond_value 9.
//    IT WAS ITEM_KLASS_CARE WITH value 0 UNTIL P5-C4, which is
//    a care item that restores nothing by the contract above.
//    game/inventory.cpp routes this class to EvoContext.item_id
//    and refuses to spend the key when no rule consumes it.
// =============================================================================

#ifndef ER_ITEMS_TABLE_H
#define ER_ITEMS_TABLE_H

#include <stdint.h>
#include <stddef.h>

#include "species_table.h"        // SPECIES_RARITY_*
#include "../core/strings_es.h"   // StrId: name_idx

enum ItemKlass : uint8_t {
  ITEM_KLASS_XP_CANDY,
  ITEM_KLASS_CAPTURE,
  ITEM_KLASS_CARE,
  ITEM_KLASS_BATTLE_MOD,
  ITEM_KLASS_EVOLUTION,
  ITEM_KLASS_COUNT
};

// THE TWO UNITS AS NUMBERS, not as sentences (see the banner).
#define ITEM_XP_CANDY_SCALE  8
#define ITEM_CAPTURE_SCALE   10

// WHICH care stat a CARE item restores. ALL is not a sixth stat: it
// means every one of them. The ordinals 1..5 are CareId + 1, and
// game/inventory.h asserts that against persistence/save_schema.h -
// this header cannot see CareId and must not guess at it.
enum CareTarget : uint8_t {
  CARE_TGT_ALL,
  CARE_TGT_HUNGER,
  CARE_TGT_HAPPINESS,
  CARE_TGT_HEALTH,
  CARE_TGT_CLEANLINESS,
  CARE_TGT_ENERGY,
  CARE_TGT_COUNT
};

// A BATTLE_MOD's target is game/battle.h's BattleStat, and the status
// bits a CARE item's param clears are save_schema.h's PBS_*. Both are
// reproduced from the pack as ordinals because a data header may not
// include a game or a persistence one; game/inventory.h is the single
// place that sees both sides and asserts they agree.
#define ITEM_BSTAT_ATK         0
#define ITEM_BSTAT_DEF         1
#define ITEM_BSTAT_SPD         2
#define ITEM_PBS_SICK          0x01u
#define ITEM_PBS_ASLEEP        0x02u
#define ITEM_PBS_CORRUPTED     0x04u
#define ITEM_PBS_FAINTED       0x08u
#define ITEM_PBS_MASK          0x0Fu   // every bit a CARE item may clear

struct ItemDef {            // 8 B, plan 1.5.2
  uint8_t  id;              // 1..ITEM_COUNT, contiguous == index + 1
  uint8_t  klass;           // ItemKlass
  uint8_t  value;           // magnitude, unit per klass (see the banner)
  uint8_t  rarity;          // SPECIES_RARITY_*
  uint16_t name_idx;        // StrId of the Spanish item name
  uint8_t  target;          // CARE: CareTarget. BATTLE_MOD: ITEM_BSTAT_*. else 0
  uint8_t  param;           // CARE: the PBS_* bits it clears. BATTLE_MOD: rounds
};
// The two bytes were `reserved[2]` until P5-C4 spent them on the columns the
// pack had no room for. The struct did not grow: 8 B, and item_rows_are_well_
// formed() checks the new fields where it used to check they were zero.
static_assert(sizeof(ItemDef) == 8, "ItemDef layout drifted");

inline constexpr ItemDef ITEMS_TABLE[] = {
  //  id klass                 val rarity                    name               target             param
  {  1, ITEM_KLASS_XP_CANDY,    5, SPECIES_RARITY_COMMON,    STR_ITEM_NAME_1,   0,                 0 },   // Bit Dulce
  {  2, ITEM_KLASS_XP_CANDY,   24, SPECIES_RARITY_UNCOMMON,  STR_ITEM_NAME_2,   0,                 0 },   // Byte Dulce
  {  3, ITEM_KLASS_XP_CANDY,  100, SPECIES_RARITY_RARE,      STR_ITEM_NAME_3,   0,                 0 },   // Megadulce
  {  4, ITEM_KLASS_CAPTURE,    15, SPECIES_RARITY_COMMON,    STR_ITEM_NAME_4,   0,                 0 },   // Cebo
  {  5, ITEM_KLASS_CAPTURE,    45, SPECIES_RARITY_RARE,      STR_ITEM_NAME_5,   0,                 0 },   // Jaula Hash
  {  6, ITEM_KLASS_CARE,       40, SPECIES_RARITY_COMMON,    STR_ITEM_NAME_6,   CARE_TGT_ALL,      0 },   // Parche
  {  7, ITEM_KLASS_CARE,      100, SPECIES_RARITY_UNCOMMON,  STR_ITEM_NAME_7,   CARE_TGT_HEALTH,   ITEM_PBS_SICK | ITEM_PBS_CORRUPTED },   // Antivirus
  {  8, ITEM_KLASS_BATTLE_MOD,   1, SPECIES_RARITY_UNCOMMON,  STR_ITEM_NAME_8,   ITEM_BSTAT_ATK,    6 },   // Turbo Chip
  {  9, ITEM_KLASS_EVOLUTION,   0, SPECIES_RARITY_RARE,      STR_ITEM_NAME_9,   0,                 0 },   // Llave Raíz
  { 10, ITEM_KLASS_BATTLE_MOD,   1, SPECIES_RARITY_UNCOMMON,  STR_ITEM_NAME_10,  ITEM_BSTAT_DEF,    6 },   // Escudo RAM
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
  }
  return true;
}

// Every item class has at least one row. A class with no item is a menu entry
// the player can never fill. Spec section 24 names FOUR classes and calls them
// "initial"; the fifth is the evolution key, which had been folded onto CARE.
constexpr bool item_klasses_are_all_populated(void) {
  for (uint8_t k = 0; k < (uint8_t)ITEM_KLASS_COUNT; ++k) {
    bool seen = false;
    for (uint8_t i = 0; i < ITEM_COUNT; ++i)
      if (ITEMS_TABLE[i].klass == k) seen = true;
    if (!seen) return false;
  }
  return true;
}

// EVERY ITEM SAYS WHAT IT ACTS ON (P5-C4). The compile-time half of the two
// units that named no target: a CARE row must name a care stat and restore a
// percentage somebody can feel, a BATTLE_MOD row must name a battle stat AND a
// number of rounds, and a klass with no target vocabulary must carry zero in
// both bytes rather than a value nothing reads. gen_content.py checks the same
// rules against the NAMES in tools/content/items.json, where a diagnostic can
// say which row and which word; this is the half that holds when the emitted
// ordinal is hand-edited.
constexpr bool item_targets_are_well_formed(void) {
  for (uint8_t i = 0; i < ITEM_COUNT; ++i) {
    const ItemDef& it = ITEMS_TABLE[i];
    if (it.klass == (uint8_t)ITEM_KLASS_CARE) {
      if (it.target >= (uint8_t)CARE_TGT_COUNT)       return false;
      if (it.value == 0u || it.value > 100u)          return false;  // percent of full
      // Every set bit must be one a Bug can carry. Written as an OR so the
      // whole expression stays unsigned: ~ on a uint8_t promotes to a signed
      // int and this file is compiled with -Wall -Wextra -Werror.
      if ((it.param | (uint8_t)ITEM_PBS_MASK) != (uint8_t)ITEM_PBS_MASK) return false;
    } else if (it.klass == (uint8_t)ITEM_KLASS_BATTLE_MOD) {
      if (it.target > (uint8_t)ITEM_BSTAT_SPD)        return false;
      if (it.param == 0u)                             return false;  // rounds
      if (it.value == 0u)                             return false;  // stages
    } else {
      if (it.target != 0u || it.param != 0u)          return false;
    }
  }
  return true;
}

static_assert(ITEM_COUNT >= 1, "the item table is empty");
static_assert(item_rows_are_well_formed(),
              "an item row has a bad id, klass, rarity or string index");
static_assert(item_klasses_are_all_populated(),
              "an item class has no row");
static_assert(item_targets_are_well_formed(),
              "an item does not say what it acts on: a CARE row with no care stat "
              "or a value outside 1..100 percent of a full bar, a BATTLE_MOD with "
              "no stat or no rounds, or a klass carrying a target byte nothing reads");

// constexpr since P5-C3: data/encounter_table.h's encounter_item_rows_have_a_drop()
// guard pairs an ITEM encounter row's rarity band with its own category's drop
// list AT COMPILE TIME, and it has to read a rarity out of this table to do it.
inline constexpr const ItemDef* item_get(uint8_t id) {
  if (id < 1u || id > ITEM_COUNT) return nullptr;
  return &ITEMS_TABLE[id - 1u];
}

#endif // ER_ITEMS_TABLE_H
