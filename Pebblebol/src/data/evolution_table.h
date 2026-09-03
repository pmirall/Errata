// =============================================================================
//  PEBBLEBOL - data/evolution_table.h
//  THE EVOLUTION RULES (spec section 18, plan 1.5.2, P3-C3).
//
//  One row per species that can leave its stage. A row is DATA: a minimum
//  level, an optional condition and the species on the other side. game/
//  evolution.cpp is the only code that reads it, and it refuses any rule whose
//  condition it cannot evaluate - see the EvoContext contract in evolution.h.
//
//  P4-C1's tools/gen_content.py emits this file. Until then the rows are
//  written by hand in exactly the shape the generator will produce, so the
//  content pipeline replaces the table without touching anything else.
//
//  Pure header: stdint plus the species roster. No Arduino.
// =============================================================================
#ifndef PB_EVOLUTION_TABLE_H
#define PB_EVOLUTION_TABLE_H

#include <stdint.h>
#include <stddef.h>

#include "species_table.h"

// -----------------------------------------------------------------------------
//  THE CONDITION KINDS (spec section 18: "level, plus an optional condition").
//
//  All five ship now even though family 1 uses only EVOC_NONE. The generator
//  emits all five at P4-C1 and this enum sits one hop from persisted content
//  (SpeciesDef.evo_rule indexes the table these tag), so renumbering it later
//  is strictly worse than carrying two names nothing reads yet.
//
//  EVERY kind needs an input, and game/evolution.cpp REFUSES a rule whose
//  input the caller did not supply. An unchecked requirement that evolves the
//  creature anyway is the exact bug this design exists to prevent.
// -----------------------------------------------------------------------------
enum EvoCond : uint8_t {
  EVOC_NONE = 0,        // the level is the whole rule
  EVOC_HAPPINESS_GE,    // care[CARE_HAPPINESS] percentage >= cond_value
  EVOC_CORRUPTED,       // status carries PBS_CORRUPTED  (spec section 55)
  EVOC_ITEM,            // an item is being applied, id == cond_value      (P6)
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

// --- the rules ---------------------------------------------------------------
// Family 1 only, matching the roster in species_table.h. SpeciesDef.evo_rule
// is the INDEX into this array, so the order here is contractual.
inline constexpr EvolutionRule EVOLUTION_RULES[] = {
  //  from  to  lvl  cond        value
  {     1,   2,   8, EVOC_NONE,      0 },   // Paketo  -> Fragmar
  {     2,   3,  18, EVOC_NONE,      0 },   // Fragmar -> Rafagon
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
//  device in someone's pocket. tests/test_evolution.cpp re-checks every one of
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
    if (r.level == 0u || r.level > (uint8_t)PB_LEVEL_MAX) return false;
    if (r.cond >= (uint8_t)EVOC_COUNT)           return false;
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

static_assert(EVOLUTION_RULES_COUNT >= 1, "the table needs at least one rule");
// SPECIES_EVO_NONE is the "no evolution" marker, so it must never also be a
// legal index: keep the table strictly shorter than 0xFF rows.
static_assert(EVOLUTION_RULES_COUNT < SPECIES_EVO_NONE,
              "0xFF must stay the 'no evolution' marker, not a valid index");
static_assert(evo_rules_are_well_formed(),
              "an evolution rule does not resolve, crosses families, or skips a stage");
static_assert(evo_sources_are_unique(), "two rules share a source species");
static_assert(evo_roster_agrees(), "SpeciesDef.evo_rule and EVOLUTION_RULES disagree");
static_assert(evo_final_stages_have_no_rule(),
              "a species marked final is the source of a rule");

#endif  // PB_EVOLUTION_TABLE_H
