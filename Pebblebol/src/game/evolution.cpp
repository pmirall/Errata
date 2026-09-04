// =============================================================================
//  PEBBLEBOL - game/evolution.cpp
//  The evolution rules, read and applied. See evolution.h for the two
//  arguments that shape this file: an unsupplied input refuses, and "pending"
//  is a level gate rather than a promise.
// =============================================================================
#include "evolution.h"

#include "xp.h"    // xp_hp_max() / xp_hp_rescale(): the ONE hp rescale rule

// =============================================================================
//  LOOKUP
// =============================================================================
const EvolutionRule* evolution_rule_for(uint8_t species_id)
{
  const SpeciesDef* sp = species_get(species_id);
  if (sp == nullptr) return nullptr;                 // empty slot, or a custom id
  if (sp->evo_rule == (uint8_t)SPECIES_EVO_NONE) return nullptr;   // final stage

  const EvolutionRule* r = evolution_rule_at(sp->evo_rule);
  if (r == nullptr) return nullptr;
  // The table's compile-time guards already say these agree. Checking again at
  // runtime costs two bytes and means a CUSTOM species record (P8) that points
  // at someone else's rule can never take a Pebble out of its own family.
  if (r->species != species_id) return nullptr;
  return r;
}

// =============================================================================
//  THE GATES
// =============================================================================
uint8_t evolution_level_ready(const PebbleInstance& p)
{
  const EvolutionRule* r = evolution_rule_for(p.species_id);
  if (r == nullptr) return 0u;
  const uint8_t level = (p.level == 0u) ? 1u : p.level;
  return (level >= r->level) ? 1u : 0u;
}

uint8_t evolution_cond_holds(const EvolutionRule& r, const EvoContext& ctx)
{
  switch (r.cond) {
    case (uint8_t)EVOC_NONE:
      return 1u;                       // the level was the whole rule

    case (uint8_t)EVOC_HAPPINESS_GE:
      if (!(ctx.have & EVOCTX_HAPPINESS)) return 0u;
      return (ctx.happiness >= r.cond_value) ? 1u : 0u;

    case (uint8_t)EVOC_CORRUPTED:
      if (!(ctx.have & EVOCTX_CORRUPTED)) return 0u;
      return (ctx.corrupted != 0u) ? 1u : 0u;

    case (uint8_t)EVOC_ITEM:
      if (!(ctx.have & EVOCTX_ITEM)) return 0u;
      // 0 is "no item", never a match, whatever the rule asks for.
      return (ctx.item_id != 0u && (uint16_t)ctx.item_id == r.cond_value) ? 1u : 0u;

    case (uint8_t)EVOC_ACTIVITY_GE:
      if (!(ctx.have & EVOCTX_ACTIVITY)) return 0u;
      return (ctx.activity >= r.cond_value) ? 1u : 0u;

    default:
      // A condition this build does not know how to evaluate. Refusing is the
      // only safe answer: a newer content pack must not be able to evolve a
      // creature past a requirement this firmware cannot even name.
      return 0u;
  }
}

uint8_t evolution_ready(const PebbleInstance& p, const EvoContext& ctx)
{
  const EvolutionRule* r = evolution_rule_for(p.species_id);
  if (r == nullptr) return 0u;
  if (!evolution_level_ready(p)) return 0u;
  return evolution_cond_holds(*r, ctx);
}

// =============================================================================
//  THE ONLY WRITER
// =============================================================================
bool evolution_apply(PebbleInstance& p, const EvoContext& ctx)
{
  if (!evolution_ready(p, ctx)) return false;

  const EvolutionRule* r = evolution_rule_for(p.species_id);
  if (r == nullptr) return false;                  // evolution_ready() said yes
  const SpeciesDef* from = species_get(p.species_id);
  const SpeciesDef* to   = species_get(r->target);
  if (from == nullptr || to == nullptr) return false;

  // hp_max moves because base_hp moves, under a creature standing still. Same
  // rescale as a level-up, from the same pair of functions, so a full Pebble
  // stays full and a hurt one keeps its fraction.
  const uint8_t  level = (p.level == 0u) ? 1u : p.level;
  const uint16_t max0  = xp_hp_max(from->base_hp, level);
  const uint16_t max1  = xp_hp_max(to->base_hp, level);

  p.species_id = to->id;
  xp_hp_rescale(p, max0, max1);

  // Bits 6:2 of evo_state are persisted bytes this module does not own, so the
  // stage is masked in rather than assigned over the whole byte.
  p.evo_state = (uint8_t)((p.evo_state & (uint8_t)~(EVO_STATE_STAGE_MASK | EVO_STATE_PENDING))
                          | (uint8_t)(to->stage & EVO_STATE_STAGE_MASK));

  if (p.evolutions < 255u) p.evolutions = (uint8_t)(p.evolutions + 1u);

  // A CHAIN evolves without waiting for the next award. Clearing the pending
  // bit above is right - that offer has been answered - but a level-20 Paketo
  // becoming a Fragmar already satisfies Fragmar's own level-18 rule, and
  // leaving the bit down would hide the second offer until the next xp_add()
  // happened to raise it. Re-raising here keeps the bit's meaning exact: it is
  // set exactly when the level gate of the CURRENT species is met.
  if (evolution_level_ready(p)) p.evo_state |= (uint8_t)EVO_STATE_PENDING;

  // DELIBERATELY UNTOUCHED: moves, xp, level, care[], care_rem[], the genome
  // and the nickname. In particular the new species' LEARNSET is not applied.
  //
  // ANSWERED, and the answer is that this stays as it is (P4-C6). This comment
  // used to say "there is no attack table until P4-C1 ... P4-C1 adds the table
  // and decides what a learnset change owes an existing creature". P4-C1 landed
  // the table and did NOT decide; P4-C2 did, in another file, and the obligation
  // was left open here where the reader is standing. The decision, from
  // game/battle.h's BR_UNLEARNABLE_MOVE: a legal moveset is "the verbatim
  // learnset of SOME species in the same family at a stage <= this one's", so a
  // Paketo that becomes a Fragmar keeps {1,6,7,27} for life and the engine
  // accepts it outright (tests/test_validate.cpp
  // `the_learnset_walk_reaches_earlier_stages_and_not_later_ones`). An
  // evolution that rewrote moves[] would take four attacks off a creature the
  // player chose them for; teaching moves is spec section 13's own mechanic and
  // it is not this function's.
  //
  // THE PRICE, STATED: a later stage's own learnset is unreachable by any
  // player-facing path in V1. Fragmar's {5,27,31,33} exists in the table, is
  // legal to hold, and nothing today can put it on a creature.
  return true;
}
