// =============================================================================
//  PEBBLEBOL - data/species_table.h
//
//  GENERATED FILE. Do not edit: tools/gen_content.py rewrites it from
//  tools/content/*.json. `tools/gen_content.py --check` fails the gate if
//  this file and the JSON have drifted apart.
//
//  THE SPECIES ROSTER (plan 1.5.2, spec sections 11/12/19/22).
//
//  36 species = 12 families x 3 stages, a PREFIX of the 60-species content
//  pack in tools/content/. Contiguity (id == index + 1) is a static_assert,
//  so the only legal subset of the pack is a prefix; see the banner in
//  tools/gen_content.py for the flash and sprite-atlas measurements that
//  chose this number.
//
//  Everything is `inline constexpr`, so the rows live in flash and no
//  translation unit gets a private copy. Nothing derived is stored on a
//  Pebble (spec section 10): hp_max, atk, def and spd are recomputed from
//  these base numbers, the level and the genome on every read - see
//  game/pebble.h.
//
//  Pure header: stdint, the save schema's shared constants and
//  core/strings_es.h for the StrIds in name_idx / flavor_idx. No Arduino.
// =============================================================================

#ifndef PB_SPECIES_TABLE_H
#define PB_SPECIES_TABLE_H

#include <stdint.h>
#include <stddef.h>

#include "../core/strings_es.h"           // StrId: name_idx / flavor_idx below
#include "../persistence/save_schema.h"   // PB_MOVE_COUNT, PB_LEVEL_MAX

// PebbleType (spec section 12). The chart is three-cornered and TYPE_COUNT is
// its dimension: SIGNAL beats CORRUPT beats SYSTEM beats SIGNAL.
//
// TYPE_NEUTRAL SHARES THE VALUE 3 WITH TYPE_COUNT, AND THAT IS DELIBERATE. It
// is an ATTACK-ONLY type (8 of the 34 attacks are NEUTRAL, two of them the
// damage moves every learnset is required to carry): it has no row and no
// column in TYPE_CHART and its modifier is 0 against everything. A SPECIES may
// never be NEUTRAL, which is why the species guard below still reads
// `type >= TYPE_COUNT` while the attack guard reads `type > TYPE_NEUTRAL`.
enum PebbleType : uint8_t {
  TYPE_SIGNAL = 0,
  TYPE_CORRUPT = 1,
  TYPE_SYSTEM = 2,
  TYPE_COUNT = 3,        // species types, and the dimension of TYPE_CHART
  TYPE_NEUTRAL = 3,      // attacks only: no chart row, modifier always 0
  TYPE_ATTACK_COUNT = 4
};

// Rarity bands (spec section 22).
#define SPECIES_RARITY_COMMON    0
#define SPECIES_RARITY_UNCOMMON  1
#define SPECIES_RARITY_RARE      2
#define SPECIES_RARITY_SPECIAL   3
#define SPECIES_RARITY_COUNT     4

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
  uint8_t  sprite_id;               // body index in the atlas
  uint16_t name_idx;                // StrId of the Spanish species name
  uint16_t flavor_idx;              // StrId of the Spanish flavor line
  uint8_t  reserved[2];             // must be 0
};
static_assert(sizeof(SpeciesDef) == 24, "SpeciesDef layout drifted");

// --- the roster -------------------------------------------------------------
inline constexpr SpeciesDef SPECIES_TABLE[] = {
  //  id fam stg type          hp atk def spd  moves                evo
  //    rarity                    wt grp cat spr  name             flavor
  {   1,  1,  0, TYPE_SIGNAL,   4,  4,  4,  4, {  1,  6,  7, 27 }, 0,
    SPECIES_RARITY_COMMON,    190,  1,  23,  0, STR_SPC_NAME_1,  STR_SPC_FLAV_1,  { 0, 0 } },   // Paketo
  {   2,  1,  1, TYPE_SIGNAL,   6,  6,  5,  5, {  5, 27, 31, 33 }, 1,
    SPECIES_RARITY_COMMON,    120,  1,  29,  1, STR_SPC_NAME_2,  STR_SPC_FLAV_2,  { 0, 0 } },   // Fragmar
  {   3,  1,  2, TYPE_SIGNAL,   7,  7,  7,  7, {  3, 27, 33, 31 }, SPECIES_EVO_NONE,
    SPECIES_RARITY_RARE,       55,  1,  37,  2, STR_SPC_NAME_3,  STR_SPC_FLAV_3,  { 0, 0 } },   // Rafagón
  {   4,  2,  0, TYPE_SIGNAL,   4,  3,  3,  6, {  1, 33,  7, 27 }, 2,
    SPECIES_RARITY_COMMON,    180,  2,  21,  3, STR_SPC_NAME_4,  STR_SPC_FLAV_4,  { 0, 0 } },   // Bippo
  {   5,  2,  1, TYPE_SIGNAL,   5,  5,  4,  8, { 33, 27,  1, 32 }, 3,
    SPECIES_RARITY_COMMON,    115,  2,  21,  4, STR_SPC_NAME_5,  STR_SPC_FLAV_5,  { 0, 0 } },   // Estátic
  {   6,  2,  2, TYPE_SIGNAL,   7,  6,  6,  9, {  8,  2, 27,  6 }, SPECIES_EVO_NONE,
    SPECIES_RARITY_RARE,       50,  2,  48,  5, STR_SPC_NAME_6,  STR_SPC_FLAV_6,  { 0, 0 } },   // Jamrón
  {   7,  3,  0, TYPE_SIGNAL,   5,  3,  3,  5, {  1, 33,  7, 27 }, 4,
    SPECIES_RARITY_COMMON,    170,  5,   7,  6, STR_SPC_NAME_7,  STR_SPC_FLAV_7,  { 0, 0 } },   // Lagui
  {   8,  3,  1, TYPE_SIGNAL,   6,  5,  4,  7, {  4, 27, 33, 31 }, 5,
    SPECIES_RARITY_COMMON,    110,  5,  13,  7, STR_SPC_NAME_8,  STR_SPC_FLAV_8,  { 0, 0 } },   // Jitera
  {   9,  3,  2, TYPE_SIGNAL,   7,  7,  5,  9, {  8, 28,  1,  6 }, SPECIES_EVO_NONE,
    SPECIES_RARITY_UNCOMMON,   80,  5,  40,  8, STR_SPC_NAME_9,  STR_SPC_FLAV_9,  { 0, 0 } },   // Timaut
  {  10,  4,  0, TYPE_SIGNAL,   3,  7,  2,  4, {  1,  6,  7, 27 }, 6,
    SPECIES_RARITY_COMMON,    175,  3,   3,  9, STR_SPC_NAME_10, STR_SPC_FLAV_10, { 0, 0 } },   // Pixio
  {  11,  4,  1, TYPE_SIGNAL,   4,  9,  3,  6, {  1, 27,  7,  6 }, 7,
    SPECIES_RARITY_COMMON,    105,  3,   7, 10, STR_SPC_NAME_11, STR_SPC_FLAV_11, { 0, 0 } },   // Artefax
  {  12,  4,  2, TYPE_SIGNAL,   6, 10,  4,  8, {  3, 30, 27,  6 }, SPECIES_EVO_NONE,
    SPECIES_RARITY_RARE,       45,  3,  34, 11, STR_SPC_NAME_12, STR_SPC_FLAV_12, { 0, 0 } },   // Burnix
  {  13,  5,  0, TYPE_SIGNAL,   5,  3,  6,  2, {  1,  6,  7, 27 }, 8,
    SPECIES_RARITY_COMMON,    200,  2,  23, 12, STR_SPC_NAME_13, STR_SPC_FLAV_13, { 0, 0 } },   // Spamito
  {  14,  5,  1, TYPE_SIGNAL,   7,  4,  8,  3, {  8, 27, 34,  6 }, 9,
    SPECIES_RARITY_COMMON,    125,  2,  21, 13, STR_SPC_NAME_14, STR_SPC_FLAV_14, { 0, 0 } },   // Kadenax
  {  15,  5,  2, TYPE_SIGNAL,   9,  5, 10,  4, {  1, 33, 28,  8 }, SPECIES_EVO_NONE,
    SPECIES_RARITY_UNCOMMON,   85,  2,  40, 14, STR_SPC_NAME_15, STR_SPC_FLAV_15, { 0, 0 } },   // Blaklix
  {  16,  6,  0, TYPE_CORRUPT,  4,  7,  2,  3, {  9, 27, 29, 16 }, 10,
    SPECIES_RARITY_COMMON,    185,  4,  51, 15, STR_SPC_NAME_16, STR_SPC_FLAV_16, { 0, 0 } },   // Buggo
  {  17,  6,  1, TYPE_CORRUPT,  5,  9,  3,  5, {  9, 27, 29, 16 }, 11,
    SPECIES_RARITY_UNCOMMON,  100,  4,  25, 16, STR_SPC_NAME_17, STR_SPC_FLAV_17, { 0, 0 } },   // Exploid
  {  18,  6,  2, TYPE_CORRUPT,  7, 10,  5,  6, { 14, 27, 31, 11 }, SPECIES_EVO_NONE,
    SPECIES_RARITY_RARE,       40,  4,  40, 17, STR_SPC_NAME_18, STR_SPC_FLAV_18, { 0, 0 } },   // Rootkar
  {  19,  7,  0, TYPE_CORRUPT,  6,  3,  5,  2, { 12, 27, 29, 16 }, 12,
    SPECIES_RARITY_COMMON,    180,  1,  17, 18, STR_SPC_NAME_19, STR_SPC_FLAV_19, { 0, 0 } },   // Wormi
  {  20,  7,  1, TYPE_CORRUPT,  8,  4,  7,  3, { 10, 27, 31, 16 }, 13,
    SPECIES_RARITY_UNCOMMON,   95,  1,  21, 19, STR_SPC_NAME_20, STR_SPC_FLAV_20, { 0, 0 } },   // Parasix
  {  21,  7,  2, TYPE_CORRUPT,  9,  6,  9,  4, { 11, 28, 15, 14 }, SPECIES_EVO_NONE,
    SPECIES_RARITY_RARE,       45,  1,  33, 20, STR_SPC_NAME_21, STR_SPC_FLAV_21, { 0, 0 } },   // Plagón
  {  22,  8,  0, TYPE_CORRUPT,  5,  5,  4,  2, { 12, 27, 29, 16 }, 14,
    SPECIES_RARITY_COMMON,    165,  2,  21, 21, STR_SPC_NAME_22, STR_SPC_FLAV_22, { 0, 0 } },   // Karnada
  {  23,  8,  1, TYPE_CORRUPT,  7,  7,  5,  3, { 17, 27, 33, 16 }, 15,
    SPECIES_RARITY_UNCOMMON,   90,  2,  21, 22, STR_SPC_NAME_23, STR_SPC_FLAV_23, { 0, 0 } },   // Klonix
  {  24,  8,  2, TYPE_CORRUPT,  8,  9,  7,  4, {  9, 27, 28, 16 }, SPECIES_EVO_NONE,
    SPECIES_RARITY_RARE,       42,  2,  48, 23, STR_SPC_NAME_24, STR_SPC_FLAV_24, { 0, 0 } },   // Estafex
  {  25,  9,  0, TYPE_CORRUPT,  3,  8,  2,  3, { 12, 27, 29, 16 }, 16,
    SPECIES_RARITY_COMMON,    120,  5,  33, 24, STR_SPC_NAME_25, STR_SPC_FLAV_25, { 0, 0 } },   // Nulix
  {  26,  9,  1, TYPE_CORRUPT,  4, 10,  3,  5, { 11, 27, 30, 16 }, 17,
    SPECIES_RARITY_UNCOMMON,   85,  5,  40, 25, STR_SPC_NAME_26, STR_SPC_FLAV_26, { 0, 0 } },   // Voidina
  {  27,  9,  2, TYPE_CORRUPT,  6, 10,  4,  8, {  9, 28, 17, 33 }, SPECIES_EVO_NONE,
    SPECIES_RARITY_RARE,       38,  5,  40, 26, STR_SPC_NAME_27, STR_SPC_FLAV_27, { 0, 0 } },   // Segfalt
  {  28, 10,  0, TYPE_CORRUPT,  5,  6,  3,  2, { 12, 27, 29, 16 }, 18,
    SPECIES_RARITY_COMMON,    195,  3,  35, 27, STR_SPC_NAME_28, STR_SPC_FLAV_28, { 0, 0 } },   // Bitto
  {  29, 10,  1, TYPE_CORRUPT,  6,  8,  5,  3, { 31, 27, 10, 16 }, 19,
    SPECIES_RARITY_UNCOMMON,   90,  3,  35, 28, STR_SPC_NAME_29, STR_SPC_FLAV_29, { 0, 0 } },   // Flipix
  {  30, 10,  2, TYPE_CORRUPT,  8, 10,  6,  4, { 10, 27, 28, 16 }, SPECIES_EVO_NONE,
    SPECIES_RARITY_UNCOMMON,   75,  3,  34, 29, STR_SPC_NAME_30, STR_SPC_FLAV_30, { 0, 0 } },   // Podrix
  {  31, 11,  0, TYPE_SYSTEM,   5,  4,  4,  3, { 18, 27, 25, 24 }, 20,
    SPECIES_RARITY_COMMON,    185,  1,  11, 30, STR_SPC_NAME_31, STR_SPC_FLAV_31, { 0, 0 } },   // Daemi
  {  32, 11,  1, TYPE_SYSTEM,   6,  6,  6,  4, { 18, 27, 31, 21 }, 21,
    SPECIES_RARITY_COMMON,    120,  1,  11, 31, STR_SPC_NAME_32, STR_SPC_FLAV_32, { 0, 0 } },   // Servik
  {  33, 11,  2, TYPE_SYSTEM,   8,  7,  7,  6, { 19, 18, 33, 27 }, SPECIES_EVO_NONE,
    SPECIES_RARITY_UNCOMMON,   80,  1,  40, 32, STR_SPC_NAME_33, STR_SPC_FLAV_33, { 0, 0 } },   // Kernon
  {  34, 12,  0, TYPE_SYSTEM,   5,  4,  6,  1, { 18, 27, 23, 24 }, 22,
    SPECIES_RARITY_COMMON,    175,  4,  11, 33, STR_SPC_NAME_34, STR_SPC_FLAV_34, { 0, 0 } },   // Proxi
  {  35, 12,  1, TYPE_SYSTEM,   7,  5,  8,  2, { 21, 27, 31, 18 }, 23,
    SPECIES_RARITY_COMMON,    115,  4,  11, 34, STR_SPC_NAME_35, STR_SPC_FLAV_35, { 0, 0 } },   // Gateón
  {  36, 12,  2, TYPE_SYSTEM,   9,  6, 10,  3, { 20, 26, 28, 31 }, SPECIES_EVO_NONE,
    SPECIES_RARITY_UNCOMMON,   78,  4,  40, 35, STR_SPC_NAME_36, STR_SPC_FLAV_36, { 0, 0 } },   // Murax
};

inline constexpr uint8_t SPECIES_TABLE_COUNT =
    (uint8_t)(sizeof(SPECIES_TABLE) / sizeof(SPECIES_TABLE[0]));
inline constexpr uint8_t SPECIES_FAMILY_COUNT = 12;

// The BASE-stage species of every family, indexed by (family - 1). Two callers
// need it and neither should re-derive it: persistence/migration.cpp lands each
// legacy v1 family on a base-stage creature, and P7 breeding gives an offspring
// the base stage of its parent's family (plan line 633).
inline constexpr uint8_t SPECIES_BASE_OF_FAMILY[SPECIES_FAMILY_COUNT] = {
    1,   4,   7,  10,  13,  16,  19,  22,
   25,  28,  31,  34,
};

// SPAWN WEIGHT SUMS, precomputed per (network category, rarity band).
// plan 1.5.2 asks for the guard `sum(spawn_weight) > 0 per category`; the
// PICKER needs the sums themselves, and they must be uint16_t - a single
// category's common band already sums past 255 on this roster, so a u8 table
// would silently truncate the range the encounter roll draws from.
#define NET_CATEGORY_COUNT  6
inline constexpr uint16_t SPECIES_SPAWN_SUM[NET_CATEGORY_COUNT][SPECIES_RARITY_COUNT] = {
  {  2930,   375,   100,     0 },   // UNKNOWN
  {  1815,   165,    45,     0 },   // HOME
  {  1480,   185,    55,     0 },   // PUBLIC
  {   825,   508,    78,     0 },   // BUSINESS
  {  1460,   285,    92,     0 },   // OPEN
  {   500,   573,   315,     0 },   // HIDDEN
};

// --- generator-emitted compile-time guards (plan 1.5.2) ----------------------
constexpr bool species_rows_are_well_formed(void) {
  for (uint8_t i = 0; i < (uint8_t)(sizeof(SPECIES_TABLE) / sizeof(SPECIES_TABLE[0])); ++i) {
    const SpeciesDef& sp = SPECIES_TABLE[i];
    if (sp.id != (uint8_t)(i + 1u))                    return false;
    if (sp.family == 0u)                               return false;
    if (sp.family > SPECIES_FAMILY_COUNT)              return false;
    if (sp.stage > 2u)                                 return false;
    if (sp.type >= (uint8_t)TYPE_COUNT)                return false;   // never NEUTRAL
    if (sp.base_hp == 0u || sp.base_atk == 0u)         return false;
    if (sp.base_def == 0u || sp.base_spd == 0u)        return false;
    if (sp.rarity > SPECIES_RARITY_SPECIAL)            return false;
    if (sp.compat_group == 0u)                         return false;
    if (sp.category_mask == 0u)                        return false;
    if (sp.name_idx   >= (uint16_t)STR_COUNT)          return false;
    if (sp.flavor_idx >= (uint16_t)STR_COUNT)          return false;
    if (sp.reserved[0] != 0u || sp.reserved[1] != 0u)  return false;
  }
  return true;
}

// Every family has exactly one stage-0 row and SPECIES_BASE_OF_FAMILY points at
// it. This is what makes a legacy migration and a bred offspring land on a
// creature that can still evolve.
constexpr bool species_family_bases_resolve(void) {
  for (uint8_t f = 0; f < SPECIES_FAMILY_COUNT; ++f) {
    const uint8_t id = SPECIES_BASE_OF_FAMILY[f];
    if (id < SPECIES_ID_MIN || id > SPECIES_TABLE_COUNT) return false;
    const SpeciesDef& sp = SPECIES_TABLE[id - 1u];
    if (sp.family != (uint8_t)(f + 1u)) return false;
    if (sp.stage != 0u)                 return false;
  }
  for (uint8_t i = 0; i < SPECIES_TABLE_COUNT; ++i) {
    const SpeciesDef& sp = SPECIES_TABLE[i];
    if (sp.stage != 0u) continue;
    if (SPECIES_BASE_OF_FAMILY[sp.family - 1u] != sp.id) return false;
  }
  return true;
}

// plan 1.5.2: `sum(spawn_weight) > 0 per category`. Nothing may be spawnable
// nowhere, and no category may be empty of common creatures.
constexpr bool species_spawn_sums_are_usable(void) {
  for (uint8_t c = 0; c < NET_CATEGORY_COUNT; ++c) {
    uint32_t total = 0;
    for (uint8_t r = 0; r < SPECIES_RARITY_COUNT; ++r) total += SPECIES_SPAWN_SUM[c][r];
    if (total == 0u) return false;
    if (SPECIES_SPAWN_SUM[c][SPECIES_RARITY_COMMON] == 0u) return false;
  }
  for (uint8_t i = 0; i < SPECIES_TABLE_COUNT; ++i)
    if (SPECIES_TABLE[i].spawn_weight == 0u) return false;
  return true;
}

static_assert(SPECIES_TABLE_COUNT >= 1, "the roster needs at least the starter");
static_assert(SPECIES_TABLE_COUNT <= SPECIES_ID_BUILTIN_MAX, "roster exceeds id 199");
static_assert(SPECIES_TABLE[0].id == SPECIES_ID_MIN, "species ids start at 1");
static_assert(SPECIES_TABLE[SPECIES_TABLE_COUNT - 1].id == SPECIES_TABLE_COUNT,
              "species ids must be contiguous: id == index + 1");
static_assert(SPECIES_TABLE_COUNT == SPECIES_FAMILY_COUNT * 3u,
              "a roster is whole families: 3 stages each");
static_assert(species_rows_are_well_formed(),
              "a species row has a bad id, family, stage, type, rarity or string index");
static_assert(species_family_bases_resolve(),
              "SPECIES_BASE_OF_FAMILY does not point at every family's stage-0 row");
static_assert(species_spawn_sums_are_usable(),
              "a network category has no spawnable species, or a row has weight 0");
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
