// =============================================================================
//  PEBBLEBOL - data/species_table.h
//  The species roster (plan 1.5.2). PLACEHOLDER CONTENT: this file carries the
//  final SpeciesDef layout and exactly ONE row, the starter every fresh device
//  boots with. P4-C1 replaces the table wholesale with the generated output of
//  tools/gen_content.py (12 species, 4 families x 3) and keeps this struct.
//
//  Everything here is `inline constexpr`, so the rows live in flash and no
//  translation unit gets a private copy. Nothing derived is stored on a Pebble
//  (spec section 10): hp_max, atk, def and spd are recomputed from these base
//  numbers, the level and the genome on every read.
//
//  Pure header: stdint plus the save schema's shared constants. No Arduino.
// =============================================================================
#ifndef PB_SPECIES_TABLE_H
#define PB_SPECIES_TABLE_H

#include <stdint.h>
#include <stddef.h>

#include "../persistence/save_schema.h"   // PB_MOVE_COUNT, PebbleInstance ids

// PebbleType (spec section 12). The type chart is three-cornered.
enum PebbleType : uint8_t {
  TYPE_SIGNAL = 0,
  TYPE_CORRUPT,
  TYPE_SYSTEM,
  TYPE_COUNT
};

// Rarity bands (spec section 22).
#define SPECIES_RARITY_COMMON    0
#define SPECIES_RARITY_UNCOMMON  1
#define SPECIES_RARITY_RARE      2
#define SPECIES_RARITY_SPECIAL   3

#define SPECIES_EVO_NONE         0xFFu   // SpeciesDef.evo_rule: no evolution
#define SPECIES_ID_MIN           1u      // 0 marks an empty slot
#define SPECIES_ID_BUILTIN_MAX   199u    // 200..209 are the creator's cs0..cs9
#define SPECIES_ID_STARTER       1u      // the fresh-device starter (P2-C10)

struct SpeciesDef {                 // 24 B, plan 1.5.2
  uint8_t  id;                      // 1..199, contiguous == index + 1
  uint8_t  family;                  // 1..20+
  uint8_t  stage;                   // 0 base, 1 mid, 2 final (spec section 19)
  uint8_t  type;                    // PebbleType (spec section 12)
  uint8_t  base_hp, base_atk, base_def, base_spd;   // 1..10 (spec section 11)
  uint8_t  moves[PB_MOVE_COUNT];    // learnset
  uint8_t  evo_rule;                // index into EVOLUTION_RULES[], 0xFF = none
  uint8_t  rarity;                  // SPECIES_RARITY_*
  uint8_t  spawn_weight;            // relative weight inside its rarity band
  uint8_t  compat_group;            // breeding (spec section 17)
  uint8_t  category_mask;           // NetCategory bits it can spawn under
  uint8_t  sprite_id;               // index into SPRITE_SETS
  uint16_t name_idx;                // index into SPECIES_NAMES[]
  uint16_t flavor_idx;              // StrId of the Spanish flavor line
  uint8_t  reserved[2];             // must be 0
};
static_assert(sizeof(SpeciesDef) == 24, "SpeciesDef layout drifted");

// --- the roster --------------------------------------------------------------
// ONE row until P4-C1. It is deliberately the most ordinary creature the design
// allows: a base-stage SIGNAL common, so nothing downstream can come to depend
// on the starter being special.
inline constexpr SpeciesDef SPECIES_TABLE[] = {
  //  id fam stg type          hp atk def spd   moves         evo   rar wt grp cat spr name flav  rsv
  {    1,  1,  0, TYPE_SIGNAL,  5,  5,  5,  5, { 0, 0, 0, 0 }, SPECIES_EVO_NONE,
       SPECIES_RARITY_COMMON, 100, 1, 0xFF, 0, 0, 0, { 0, 0 } },
};

inline constexpr uint8_t SPECIES_TABLE_COUNT =
    (uint8_t)(sizeof(SPECIES_TABLE) / sizeof(SPECIES_TABLE[0]));

// Generator-emitted compile-time guards (plan 1.5.2). They hold for the
// placeholder row too, so P4-C1 cannot regress them silently.
static_assert(SPECIES_TABLE_COUNT >= 1, "the roster needs at least the starter");
static_assert(SPECIES_TABLE_COUNT <= SPECIES_ID_BUILTIN_MAX, "roster exceeds id 199");
static_assert(SPECIES_TABLE[0].id == SPECIES_ID_MIN, "species ids start at 1");
static_assert(SPECIES_TABLE[SPECIES_TABLE_COUNT - 1].id == SPECIES_TABLE_COUNT,
              "species ids must be contiguous: id == index + 1");
static_assert(SPECIES_TABLE[SPECIES_ID_STARTER - 1].id == SPECIES_ID_STARTER,
              "the starter species must be the first row");

// Resolves a built-in species id. Returns nullptr for 0 (empty slot), for an
// id beyond the roster and for the custom range - P8 resolves cs* records here
// so no battle or validator code ever branches on "custom".
inline const SpeciesDef* species_get(uint8_t id) {
  if (id < SPECIES_ID_MIN || id > SPECIES_TABLE_COUNT) return nullptr;
  return &SPECIES_TABLE[id - 1u];
}

#endif // PB_SPECIES_TABLE_H
