// =============================================================================
//  PEBBLEBOL - data/species_table.h
//  The species roster (plan 1.5.2). PARTIAL CONTENT: this file carries the
//  final SpeciesDef layout and the three REAL rows of family 1, copied from
//  the verified content pack. P4-C1 replaces the table wholesale with the
//  generated output of tools/gen_content.py (12 species, 4 families x 3) and
//  keeps this struct - and, because these three rows are already the final
//  ones, it will emit them byte for byte, so nothing recorded against ids
//  1..3 in Phase 3 has to be re-recorded.
//
//  WHY IT STOPS AT FAMILY 1 (decision D14, docs/decisions.md): the contiguity
//  guard below (id == index + 1) makes the roster all-or-nothing in whole
//  family blocks, and the first family that carries a real evolution CONDITION
//  is family 4 - shipping it would mean pulling P4-C1's entire 12-species
//  roster and its attack table forward into P3-C3. The conditions are covered
//  by tests/test_evolution.cpp instead.
//
//  Everything here is `inline constexpr`, so the rows live in flash and no
//  translation unit gets a private copy. Nothing derived is stored on a Pebble
//  (spec section 10): hp_max, atk, def and spd are recomputed from these base
//  numbers, the level and the genome on every read.
//
//  Pure header: stdint, the save schema's shared constants and core/strings_es.h
//  for the StrIds in name_idx / flavor_idx (an inline constexpr table, so no
//  translation unit that only wants a base_hp pays for a copy). No Arduino.
// =============================================================================
#ifndef PB_SPECIES_TABLE_H
#define PB_SPECIES_TABLE_H

#include <stdint.h>
#include <stddef.h>

#include "../core/strings_es.h"           // StrId: name_idx / flavor_idx below
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
  uint8_t  sprite_id;               // body index in the atlas - see the roster note
  uint16_t name_idx;                // StrId of the Spanish species name
  uint16_t flavor_idx;              // StrId of the Spanish flavor line
  uint8_t  reserved[2];             // must be 0
};
static_assert(sizeof(SpeciesDef) == 24, "SpeciesDef layout drifted");

// --- the roster --------------------------------------------------------------
// Family 1: Paketo -> Fragmar -> Rafagon, the three SIGNAL stages a fresh
// device starts on. EVOLUTION_RULES in data/evolution_table.h joins them; the
// evo_rule column is the index into that table and 0xFF ends the line.
//
// TWO PLACEHOLDERS IN THESE OTHERWISE FINAL ROWS, both deliberate:
//
//  * `moves` holds the FINAL attack ids. The attack table itself does not
//    exist until P4-C1, so nothing resolves them yet and there is deliberately
//    no cross-reference guard here - one would fail today. P4-C1 adds the
//    table and the guard without touching these numbers.
//  * `sprite_id` is placeholder ART until the P10 art pass. It indexes the
//    eight 24x24 bodies of data/sprites.h (SPR_BABY_BLOB + sprite_id), the
//    same atlas coordinate the genome's species nibble feeds sprite_set_id();
//    0/1/2 are three visibly different bodies. Nothing reads the field yet -
//    the renderer is keyed on the genome and the LIFE stage, and what changes
//    on screen when a Pebble evolves today is evo_state's stage bits, which
//    ui/pet_view.cpp already feeds to sprite_form_of(). P10 replaces both the
//    art and that mapping with a species-keyed atlas.
inline constexpr SpeciesDef SPECIES_TABLE[] = {
  //  id fam stg type          hp atk def spd   moves            evo  rar wt grp cat spr name flav  rsv
  {    1,  1,  0, TYPE_SIGNAL,  4,  4,  4,  4, {  1,  6,  7, 27 }, 0,
       SPECIES_RARITY_COMMON, 190, 1, 23, 0, STR_SPC_NAME_1, STR_SPC_FLAV_1, { 0, 0 } },
  {    2,  1,  1, TYPE_SIGNAL,  6,  6,  5,  5, {  1, 27,  7,  6 }, 1,
       SPECIES_RARITY_COMMON, 120, 1, 29, 1, STR_SPC_NAME_2, STR_SPC_FLAV_2, { 0, 0 } },
  {    3,  1,  2, TYPE_SIGNAL,  7,  7,  7,  7, {  2, 27, 33, 28 }, SPECIES_EVO_NONE,
       SPECIES_RARITY_RARE,     55, 1, 37, 2, STR_SPC_NAME_3, STR_SPC_FLAV_3, { 0, 0 } },
};

inline constexpr uint8_t SPECIES_TABLE_COUNT =
    (uint8_t)(sizeof(SPECIES_TABLE) / sizeof(SPECIES_TABLE[0]));

// Generator-emitted compile-time guards (plan 1.5.2). They held for the single
// placeholder row and they hold for the family-1 block, so P4-C1 cannot
// regress them silently.
constexpr bool species_rows_are_well_formed(void) {
  for (uint8_t i = 0; i < (uint8_t)(sizeof(SPECIES_TABLE) / sizeof(SPECIES_TABLE[0])); ++i) {
    const SpeciesDef& sp = SPECIES_TABLE[i];
    if (sp.id != (uint8_t)(i + 1u))                    return false;
    if (sp.family == 0u)                               return false;
    if (sp.stage > 2u)                                 return false;
    if (sp.type >= (uint8_t)TYPE_COUNT)                return false;
    if (sp.rarity > SPECIES_RARITY_SPECIAL)            return false;
    if (sp.name_idx   >= (uint16_t)STR_COUNT)          return false;
    if (sp.flavor_idx >= (uint16_t)STR_COUNT)          return false;
    if (sp.reserved[0] != 0u || sp.reserved[1] != 0u)  return false;
  }
  return true;
}

static_assert(SPECIES_TABLE_COUNT >= 1, "the roster needs at least the starter");
static_assert(SPECIES_TABLE_COUNT <= SPECIES_ID_BUILTIN_MAX, "roster exceeds id 199");
static_assert(SPECIES_TABLE[0].id == SPECIES_ID_MIN, "species ids start at 1");
static_assert(SPECIES_TABLE[SPECIES_TABLE_COUNT - 1].id == SPECIES_TABLE_COUNT,
              "species ids must be contiguous: id == index + 1");
static_assert(species_rows_are_well_formed(),
              "a species row has a bad id, stage, type, rarity or string index");
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
