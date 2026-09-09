// =============================================================================
//  ERRATA - data/evolution_table.h
//
//  GENERATED FILE. Do not edit: tools/gen_content.py rewrites it from
//  tools/content/*.json. `tools/gen_content.py --check` fails the gate if
//  this file and the JSON have drifted apart.
//
//  THE EVOLUTION RULES (spec section 18, plan 1.5.2).
//
//  One row per species that can leave its stage. A row is DATA: a minimum
//  level, an optional condition and the species on the other side.
//  game/evolution.cpp is the only code that reads it, and it refuses any
//  rule whose condition it cannot evaluate - see EvoContext in
//  game/evolution.h.
//
//  SpeciesDef.evo_rule is the INDEX into this array, so the ORDER here is
//  contractual. The generator emits the pack's own ordering,
//  evo_rule == (family - 1) * 2 + stage, and evo_roster_agrees() below
//  fails the build if it ever drifts.
//
//  EvoCond KEEPS THE FIRMWARE'S NUMBERING, not the content pack's. The two
//  disagree on three of six values and this enum sits one hop from
//  persisted content, so gen_content.py maps by NAME and errors on a name
//  it does not know. BATTLES_WON_GE, which the pack lists and no rule uses,
//  stays dropped.
// =============================================================================

#ifndef ER_EVOLUTION_TABLE_H
#define ER_EVOLUTION_TABLE_H

#include <stdint.h>
#include <stddef.h>

#include "species_table.h"

enum EvoCond : uint8_t {
  EVOC_NONE = 0,        // the level is the whole rule
  EVOC_HAPPINESS_GE,    // care[CARE_HAPPINESS] percentage >= cond_value
  EVOC_CORRUPTED,       // status carries PBS_CORRUPTED  (spec section 55)
  EVOC_ITEM,            // an item is being applied, id == cond_value      (P5)
  EVOC_ACTIVITY_GE,     // activity score 0..100 >= cond_value             (P6)
  EVOC_COUNT
};

struct EvolutionRule {          // 6 B: four bytes and an aligned u16
  uint8_t  species;             // source species id
  uint8_t  target;              // target species id
  uint8_t  level;               // minimum level to leave the source
  uint8_t  cond;                // EvoCond
  uint16_t cond_value;          // what the condition compares against
};
static_assert(sizeof(EvolutionRule) == 6, "EvolutionRule layout drifted");

inline constexpr EvolutionRule EVOLUTION_RULES[] = {
  // from   to  lvl  cond                  value
  {   1,   2,   8, EVOC_NONE,                0 },   // [ 0] Paketo -> Fragmar
  {   2,   3,  18, EVOC_NONE,                0 },   // [ 1] Fragmar -> Rafagón
  {   4,   5,  10, EVOC_NONE,                0 },   // [ 2] Bippo -> Estátic
  {   5,   6,  20, EVOC_NONE,                0 },   // [ 3] Estátic -> Jamrón
  {   7,   8,  10, EVOC_NONE,                0 },   // [ 4] Lagui -> Jitera
  {   8,   9,  20, EVOC_NONE,                0 },   // [ 5] Jitera -> Timaut
  {  10,  11,  10, EVOC_NONE,                0 },   // [ 6] Pixio -> Artefax
  {  11,  12,  20, EVOC_CORRUPTED,           1 },   // [ 7] Artefax -> Burnix
  {  13,  14,   8, EVOC_NONE,                0 },   // [ 8] Spamito -> Kadenax
  {  14,  15,  18, EVOC_NONE,                0 },   // [ 9] Kadenax -> Blaklix
  {  16,  17,  10, EVOC_NONE,                0 },   // [10] Buggo -> Exploid
  {  17,  18,  20, EVOC_NONE,                0 },   // [11] Exploid -> Rootkar
  {  19,  20,  10, EVOC_NONE,                0 },   // [12] Wormi -> Parasix
  {  20,  21,  20, EVOC_NONE,                0 },   // [13] Parasix -> Plagón
  {  22,  23,  10, EVOC_NONE,                0 },   // [14] Karnada -> Klonix
  {  23,  24,  20, EVOC_NONE,                0 },   // [15] Klonix -> Estafex
  {  25,  26,  10, EVOC_NONE,                0 },   // [16] Nulix -> Voidina
  {  26,  27,  20, EVOC_NONE,                0 },   // [17] Voidina -> Segfalt
  {  28,  29,   8, EVOC_NONE,                0 },   // [18] Bitto -> Flipix
  {  29,  30,  18, EVOC_NONE,                0 },   // [19] Flipix -> Podrix
  {  31,  32,  12, EVOC_NONE,                0 },   // [20] Daemi -> Servik
  {  32,  33,  24, EVOC_HAPPINESS_GE,       70 },   // [21] Servik -> Kernon
  {  34,  35,  10, EVOC_NONE,                0 },   // [22] Proxi -> Gateón
  {  35,  36,  20, EVOC_NONE,                0 },   // [23] Gateón -> Murax
  {  37,  38,  10, EVOC_NONE,                0 },   // [24] Kachi -> Memoro
  {  38,  39,  20, EVOC_NONE,                0 },   // [25] Memoro -> Lekron
  {  40,  41,  12, EVOC_NONE,                0 },   // [26] Filito -> Arkivo
  {  41,  42,  24, EVOC_NONE,                0 },   // [27] Arkivo -> Zipbom
  {  43,  44,  10, EVOC_NONE,                0 },   // [28] Pingo -> Floodra
  {  44,  45,  20, EVOC_NONE,                0 },   // [29] Floodra -> Denyra
  {  46,  47,  10, EVOC_NONE,                0 },   // [30] Glitchi -> Errox
  {  47,  48,  20, EVOC_CORRUPTED,           1 },   // [31] Errox -> Panika
  {  49,  50,  10, EVOC_NONE,                0 },   // [32] Portu -> Skanor
  {  50,  51,  20, EVOC_NONE,                0 },   // [33] Skanor -> Bakdora
  {  52,  53,  12, EVOC_NONE,                0 },   // [34] Klavik -> Cifrax
  {  53,  54,  24, EVOC_ITEM,                9 },   // [35] Cifrax -> Ransora
  {  55,  56,  10, EVOC_NONE,                0 },   // [36] Probix -> Beakon
  {  56,  57,  20, EVOC_NONE,                0 },   // [37] Beakon -> Twinix
  {  58,  59,  10, EVOC_NONE,                0 },   // [38] Cookit -> Trakkar
  {  59,  60,  20, EVOC_ACTIVITY_GE,        60 },   // [39] Trakkar -> Panoptix
};

inline constexpr uint8_t EVOLUTION_RULES_COUNT =
    (uint8_t)(sizeof(EVOLUTION_RULES) / sizeof(EVOLUTION_RULES[0]));

// Resolves a rule index. Returns nullptr for SPECIES_EVO_NONE and for anything
// past the table, which is what a species with no evolution looks like.
inline const EvolutionRule* evolution_rule_at(uint8_t idx) {
  if (idx >= EVOLUTION_RULES_COUNT) return nullptr;
  return &EVOLUTION_RULES[idx];
}

// -----------------------------------------------------------------------------
//  GENERATOR-EMITTED COMPILE-TIME GUARDS (plan 1.5.2)
//
//  constexpr so a bad table is a BUILD failure, not a runtime surprise on a
//  device in someone's pocket. tests/test_content.cpp re-checks every one of
//  them at runtime as well, so a future table that somehow slipped past a
//  compiler still fails the gate.
// -----------------------------------------------------------------------------
constexpr const SpeciesDef* evo_species_at(uint8_t id) {
  return (id < SPECIES_ID_MIN || id > SPECIES_TABLE_COUNT)
             ? nullptr : &SPECIES_TABLE[id - 1u];
}

// Every rule resolves on both ends, moves WITHIN one family and moves UP
// exactly one stage.
constexpr bool evo_rules_are_well_formed(void) {
  for (uint8_t i = 0; i < EVOLUTION_RULES_COUNT; ++i) {
    const EvolutionRule& r = EVOLUTION_RULES[i];
    const SpeciesDef* s = evo_species_at(r.species);
    const SpeciesDef* t = evo_species_at(r.target);
    if (s == nullptr || t == nullptr)            return false;
    if (r.target == r.species)                   return false;
    if (s->family != t->family)                  return false;
    if ((int)t->stage != (int)s->stage + 1)      return false;
    if (r.level == 0u || r.level > (uint8_t)ER_LEVEL_MAX) return false;
    if (r.cond >= (uint8_t)EVOC_COUNT)           return false;
    // A condition that compares against a value needs one; EVOC_NONE and
    // EVOC_CORRUPTED are the two that answer without reading cond_value.
    if ((r.cond == (uint8_t)EVOC_HAPPINESS_GE || r.cond == (uint8_t)EVOC_ACTIVITY_GE ||
         r.cond == (uint8_t)EVOC_ITEM) && r.cond_value == 0u) return false;
  }
  return true;
}

// A species may leave its stage in exactly one direction: two rows with the
// same source would make the outcome depend on lookup order.
constexpr bool evo_sources_are_unique(void) {
  for (uint8_t i = 0; i < EVOLUTION_RULES_COUNT; ++i)
    for (uint8_t j = (uint8_t)(i + 1u); j < EVOLUTION_RULES_COUNT; ++j)
      if (EVOLUTION_RULES[i].species == EVOLUTION_RULES[j].species) return false;
  return true;
}

// The roster's evo_rule field and this table agree: a species either has no
// evolution or points at the row whose source it is.
constexpr bool evo_roster_agrees(void) {
  for (uint8_t i = 0; i < SPECIES_TABLE_COUNT; ++i) {
    const SpeciesDef& sp = SPECIES_TABLE[i];
    if (sp.evo_rule == SPECIES_EVO_NONE) continue;
    if (sp.evo_rule >= EVOLUTION_RULES_COUNT) return false;
    if (EVOLUTION_RULES[sp.evo_rule].species != sp.id) return false;
  }
  return true;
}

// A final-stage species must say so: nothing may point past the family.
constexpr bool evo_final_stages_have_no_rule(void) {
  for (uint8_t i = 0; i < SPECIES_TABLE_COUNT; ++i) {
    const SpeciesDef& sp = SPECIES_TABLE[i];
    if (sp.evo_rule != SPECIES_EVO_NONE) continue;
    for (uint8_t j = 0; j < EVOLUTION_RULES_COUNT; ++j)
      if (EVOLUTION_RULES[j].species == sp.id) return false;
  }
  return true;
}

// Every non-final species has a way out of its stage. A stage-0 or stage-1 row
// marked SPECIES_EVO_NONE would be a dead end in the middle of a family.
constexpr bool evo_every_non_final_stage_has_a_rule(void) {
  for (uint8_t i = 0; i < SPECIES_TABLE_COUNT; ++i) {
    const SpeciesDef& sp = SPECIES_TABLE[i];
    if (sp.stage >= 2u) continue;
    if (sp.evo_rule == SPECIES_EVO_NONE) return false;
  }
  return true;
}

static_assert(EVOLUTION_RULES_COUNT >= 1, "the table needs at least one rule");
// SPECIES_EVO_NONE is the "no evolution" marker, so it must never also be a
// legal index: keep the table strictly shorter than 0xFF rows.
static_assert(EVOLUTION_RULES_COUNT < SPECIES_EVO_NONE,
              "0xFF must stay the 'no evolution' marker, not a valid index");
static_assert(EVOLUTION_RULES_COUNT == (uint8_t)(SPECIES_FAMILY_COUNT * 2u),
              "every family owes exactly two rules: stage 0 -> 1 and 1 -> 2");
static_assert(evo_rules_are_well_formed(),
              "an evolution rule does not resolve, crosses families, skips a stage "
              "or asks a condition with no value");
static_assert(evo_sources_are_unique(), "two rules share a source species");
static_assert(evo_roster_agrees(), "SpeciesDef.evo_rule and EVOLUTION_RULES disagree");
static_assert(evo_final_stages_have_no_rule(),
              "a species marked final is the source of a rule");
static_assert(evo_every_non_final_stage_has_a_rule(),
              "a stage-0 or stage-1 species has no way out of its stage");

#endif  // ER_EVOLUTION_TABLE_H
