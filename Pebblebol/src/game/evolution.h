// =============================================================================
//  PEBBLEBOL - game/evolution.h
//  EVOLUTION (spec section 18, plan P3-C3).
//
//  Data-driven and nothing else: the rules live in data/evolution_table.h and
//  this module only reads them, asks whether one holds, and - once - performs
//  it. There is no hard-coded species anywhere below.
//
//  THE ONE RULE THAT MATTERS: A CONDITION WHOSE INPUT WAS NOT SUPPLIED REFUSES.
//  Phase 3 cannot answer every question section 18 allows a rule to ask - there
//  are no items and no activity score until P6 - so EvoContext carries a
//  VALIDITY MASK beside its fields and every evaluator checks its own bit
//  first. An unsupplied EVOC_ACTIVITY_GE that answered "true" would evolve a
//  creature on a requirement nobody checked, which is the exact bug this shape
//  exists to prevent. Defaulting to "no" costs a player one evolution they can
//  still have later; defaulting to "yes" costs them a different animal.
//
//  PENDING IS NOT A PROMISE. game/xp.cpp sets EVO_STATE_PENDING when the LEVEL
//  requirement is met, because the level gate is all a pure XP module can see:
//  it has no happiness, no item and no activity. "Pending" therefore means
//  *the level requirement is met*, never *it will evolve*. The consumer
//  re-evaluates the whole rule with real context, and when the condition does
//  not hold the bit STAYS SET so the offer comes back when it does.
//
//  PURE MODULE. stdint, the tables and the save schema. No Arduino, no clock,
//  no RNG, no I/O: tests/test_evolution.cpp compiles it directly.
//
//  It links against game/xp.cpp for the HP rescale, and game/xp.cpp links
//  against this one for the level gate. That mutual pair is deliberate: there
//  is exactly ONE rescale rule and ONE level gate in the firmware, and each
//  lives with the code that owns it.
// =============================================================================
#ifndef PB_EVOLUTION_H
#define PB_EVOLUTION_H

#include <stdint.h>

#include "../data/evolution_table.h"       // EvolutionRule, EvoCond, the table
#include "../persistence/save_schema.h"    // PebbleInstance

// -----------------------------------------------------------------------------
//  PebbleInstance.evo_state (offset 27, pinned by the save schema)
//
//      bits 1:0   stage inside the family, 0 base / 1 mid / 2 final
//      bits 6:2   unused - persisted bytes, so leave them exactly as found
//      bit    7   EVOLVE_PENDING
// -----------------------------------------------------------------------------
#define EVO_STATE_STAGE_MASK   0x03u
#define EVO_STATE_PENDING      0x80u

// -----------------------------------------------------------------------------
//  THE CONTEXT
//
//  `have` says which of the fields below the caller actually filled in. A field
//  whose bit is clear is not "zero", it is UNKNOWN, and any condition that
//  needs it refuses.
// -----------------------------------------------------------------------------
#define EVOCTX_HAPPINESS   0x01u    // happiness is a real 0..100 reading
#define EVOCTX_CORRUPTED   0x02u    // corrupted was read from the status byte
#define EVOCTX_ITEM        0x04u    // item_id is the item being applied  (P6)
#define EVOCTX_ACTIVITY    0x08u    // activity is a real 0..100 score    (P6)

struct EvoContext {
  uint8_t  have;            // EVOCTX_* bits: which fields below are meaningful
  uint16_t happiness;       // 0..100, from the care array
  uint8_t  corrupted;       // status has PBS_CORRUPTED
  uint8_t  item_id;         // the item being applied, 0 = none          (P6)
  uint16_t activity;        // 0..100 activity score                     (P6)
};

// An empty context: nothing supplied, so every condition but EVOC_NONE refuses.
inline void evo_context_clear(EvoContext& c) {
  c.have = 0u; c.happiness = 0u; c.corrupted = 0u; c.item_id = 0u; c.activity = 0u;
}

// -----------------------------------------------------------------------------
//  THE API
// -----------------------------------------------------------------------------
// The rule that takes `species_id` out of its stage, or nullptr when the
// species is final-stage, unknown or empty.
const EvolutionRule* evolution_rule_for(uint8_t species_id);

// The LEVEL half of a rule, and nothing else. This is what game/xp.cpp can see.
uint8_t evolution_level_ready(const PebbleInstance& p);

// One condition, against one context. Public because it is the piece the tests
// have to be able to aim a hand-built rule at.
uint8_t evolution_cond_holds(const EvolutionRule& r, const EvoContext& ctx);

// The WHOLE rule: there is one, the level is reached, and the condition holds
// with the inputs the caller actually supplied.
uint8_t evolution_ready(const PebbleInstance& p, const EvoContext& ctx);

// Performs the evolution. Re-checks evolution_ready() and returns false when it
// does not hold: this is the only writer, and a caller that got its context
// wrong must not be able to mutate the Pebble.
//
// On success: species_id becomes the target, evo_state's stage bits become the
// target's stage and EVO_STATE_PENDING clears, `evolutions` increments
// (saturating at 255) and hp_cur is rescaled against the new derived hp_max by
// the same rule a level-up uses. moves, xp, level, the care array and the
// genome are left exactly as they were.
bool evolution_apply(PebbleInstance& p, const EvoContext& ctx);

#endif  // PB_EVOLUTION_H
