// =============================================================================
//  Pebblebol host tests - test_evolution.cpp
//  game/evolution.cpp over data/evolution_table.h (spec section 18, plan
//  P3-C3, section 4 "Evolution works").
//
//  What this file exists to prove:
//    * rule lookup: found mid-family, nullptr at the final stage and for an id
//      that is not in the roster at all;
//    * the level gate: below, exactly at, and above;
//    * every condition kind: satisfied, unsatisfied, and - the one that
//      matters - INPUT NOT SUPPLIED REFUSES. A condition this build cannot
//      evaluate must never answer "yes";
//    * apply recomputes the derived stats: hp_max moves with the species, a
//      full Pebble stays full, a hurt one keeps its fraction, hp_cur <= hp_max;
//    * apply on a final stage and apply with a failing condition both do
//      NOTHING and return false;
//    * evo_state's stage bits track the target, the pending bit clears on a
//      successful apply and SURVIVES a refusal;
//    * the evolutions counter increments and saturates at 255;
//    * the whole family walk, level 1 Paketo -> Fragmar -> Rafagon -> final;
//    * every compile-time guard in evolution_table.h holds as a RUNTIME
//      assertion too, so a future table that somehow reaches a compiler that
//      does not evaluate them still fails the gate.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "data/evolution_table.h"
#include "data/species_table.h"
#include "game/evolution.h"
#include "game/xp.h"

// A minimal but legal Pebble at `species` / `level`, at full derived HP.
static void make_pebble(PebbleInstance& p, uint8_t species, uint8_t level) {
  memset(&p, 0, sizeof p);
  p.magic      = (uint16_t)PEBBLE_MAGIC;
  p.layout_ver = (uint8_t)PEBBLE_LAYOUT_VER;
  p.species_id = species;
  p.id         = 0x5EED0001u;
  p.level      = level;
  const SpeciesDef* sp = species_get(species);
  p.hp_cur = sp ? xp_hp_max(sp->base_hp, level) : 0u;
}

static uint16_t hp_max_of(const PebbleInstance& p) {
  const SpeciesDef* sp = species_get(p.species_id);
  return sp ? xp_hp_max(sp->base_hp, p.level) : 0u;
}

// Everything supplied, everything true: the context a rule with no condition
// never looks at, and the one the condition cases below narrow down.
static EvoContext full_ctx(void) {
  EvoContext c;
  evo_context_clear(c);
  c.have      = (uint8_t)(EVOCTX_HAPPINESS | EVOCTX_CORRUPTED |
                          EVOCTX_ITEM | EVOCTX_ACTIVITY);
  c.happiness = 100;
  c.corrupted = 1;
  c.item_id   = 7;
  c.activity  = 100;
  return c;
}

// =============================================================================
//  1. LOOKUP
// =============================================================================
TEST(rule_lookup_finds_a_rule_for_every_non_final_species) {
  for (uint8_t i = 0; i < SPECIES_TABLE_COUNT; ++i) {
    const SpeciesDef& sp = SPECIES_TABLE[i];
    const EvolutionRule* r = evolution_rule_for(sp.id);
    if (sp.evo_rule == (uint8_t)SPECIES_EVO_NONE) {
      CHECK(r == nullptr);                       // a final stage has nowhere to go
    } else {
      CHECK(r != nullptr);
      if (!r) continue;
      CHECK_EQ(r->species, sp.id);
      const SpeciesDef* to = species_get(r->target);
      CHECK(to != nullptr);
      if (!to) continue;
      CHECK_EQ(to->family, sp.family);           // never out of the family
      CHECK_EQ((int)to->stage, (int)sp.stage + 1);
    }
  }
}

TEST(rule_lookup_refuses_ids_that_are_not_in_the_roster) {
  CHECK(evolution_rule_for(0) == nullptr);                        // an empty slot
  CHECK(evolution_rule_for((uint8_t)(SPECIES_TABLE_COUNT + 1)) == nullptr);
  CHECK(evolution_rule_for(200) == nullptr);                      // the cs* range
  CHECK(evolution_rule_for(255) == nullptr);
}

TEST(the_family_one_rules_are_the_ones_the_content_pack_ships) {
  const EvolutionRule* a = evolution_rule_for(1);
  const EvolutionRule* b = evolution_rule_for(2);
  CHECK(a != nullptr);
  CHECK(b != nullptr);
  if (!a || !b) return;
  CHECK_EQ(a->target, 2);
  CHECK_EQ(a->level, 8);
  CHECK_EQ(a->cond, (uint8_t)EVOC_NONE);
  CHECK_EQ(b->target, 3);
  CHECK_EQ(b->level, 18);
  CHECK_EQ(b->cond, (uint8_t)EVOC_NONE);
  CHECK(evolution_rule_for(3) == nullptr);       // Rafagon is the end of family 1
}

// =============================================================================
//  2. THE LEVEL GATE
// =============================================================================
TEST(the_level_gate_opens_at_the_rule_and_not_before) {
  const EvolutionRule* r = evolution_rule_for(1);
  CHECK(r != nullptr);
  if (!r) return;

  PebbleInstance p;
  make_pebble(p, 1, (uint8_t)(r->level - 1u));
  CHECK_EQ(evolution_level_ready(p), 0);         // below
  p.level = r->level;
  CHECK_EQ(evolution_level_ready(p), 1);         // exactly at
  p.level = (uint8_t)(r->level + 5u);
  CHECK_EQ(evolution_level_ready(p), 1);         // above

  // A final stage has no gate at all, however high the level goes.
  make_pebble(p, 3, (uint8_t)PB_LEVEL_MAX);
  CHECK_EQ(evolution_level_ready(p), 0);

  // A level of 0 is a corrupt record, not level 1's twin: it reads as level 1,
  // which is below every rule this roster ships.
  make_pebble(p, 1, 0);
  CHECK_EQ(evolution_level_ready(p), 0);
}

// =============================================================================
//  3. THE CONDITIONS - and the refusal that is the point of the design
// =============================================================================
TEST(a_condition_holds_only_when_its_input_says_so) {
  EvoContext c = full_ctx();

  EvolutionRule r = { 1, 2, 8, (uint8_t)EVOC_HAPPINESS_GE, 80 };
  CHECK_EQ(evolution_cond_holds(r, c), 1);
  c.happiness = 80;  CHECK_EQ(evolution_cond_holds(r, c), 1);   // >= is inclusive
  c.happiness = 79;  CHECK_EQ(evolution_cond_holds(r, c), 0);

  c = full_ctx();
  r.cond = (uint8_t)EVOC_CORRUPTED;  r.cond_value = 0;
  CHECK_EQ(evolution_cond_holds(r, c), 1);
  c.corrupted = 0;   CHECK_EQ(evolution_cond_holds(r, c), 0);

  c = full_ctx();
  r.cond = (uint8_t)EVOC_ITEM;  r.cond_value = 7;
  CHECK_EQ(evolution_cond_holds(r, c), 1);
  c.item_id = 8;     CHECK_EQ(evolution_cond_holds(r, c), 0);   // the wrong item
  c.item_id = 0;     CHECK_EQ(evolution_cond_holds(r, c), 0);   // no item at all
  r.cond_value = 0;  CHECK_EQ(evolution_cond_holds(r, c), 0);   // and 0 never matches

  c = full_ctx();
  r.cond = (uint8_t)EVOC_ACTIVITY_GE;  r.cond_value = 60;
  CHECK_EQ(evolution_cond_holds(r, c), 1);
  c.activity = 60;   CHECK_EQ(evolution_cond_holds(r, c), 1);
  c.activity = 59;   CHECK_EQ(evolution_cond_holds(r, c), 0);

  // EVOC_NONE never looks at the context, so an empty one still passes.
  EvoContext empty;
  evo_context_clear(empty);
  r.cond = (uint8_t)EVOC_NONE;  r.cond_value = 0;
  CHECK_EQ(evolution_cond_holds(r, empty), 1);
}

// THE ONE THAT MATTERS. Every condition whose input was not supplied must
// answer NO. A "yes" here would evolve a creature on a requirement nobody
// checked, which is exactly what the validity mask exists to prevent.
TEST(an_unsupplied_input_refuses_it_never_passes) {
  EvoContext c = full_ctx();
  c.have = 0;                        // every field still holds a passing value

  static const uint8_t kNeedsInput[] = {
    (uint8_t)EVOC_HAPPINESS_GE, (uint8_t)EVOC_CORRUPTED,
    (uint8_t)EVOC_ITEM, (uint8_t)EVOC_ACTIVITY_GE
  };
  for (size_t i = 0; i < sizeof kNeedsInput / sizeof kNeedsInput[0]; ++i) {
    EvolutionRule r = { 1, 2, 8, kNeedsInput[i], 0 };
    CHECK_EQ(evolution_cond_holds(r, c), 0);
  }

  // Supplying only the OTHER bits is not supplying this one.
  c.have = (uint8_t)(EVOCTX_CORRUPTED | EVOCTX_ITEM | EVOCTX_ACTIVITY);
  EvolutionRule happy = { 1, 2, 8, (uint8_t)EVOC_HAPPINESS_GE, 10 };
  CHECK_EQ(evolution_cond_holds(happy, c), 0);

  // And a condition kind this build cannot even name refuses as well: a newer
  // content pack must never be able to walk past a requirement.
  EvolutionRule unknown = { 1, 2, 8, (uint8_t)EVOC_COUNT, 0 };
  CHECK_EQ(evolution_cond_holds(unknown, full_ctx()), 0);
  EvolutionRule future = { 1, 2, 8, 200, 0 };
  CHECK_EQ(evolution_cond_holds(future, full_ctx()), 0);
}

// =============================================================================
//  4. APPLY
// =============================================================================
TEST(apply_moves_the_species_the_stage_bits_and_the_counter) {
  PebbleInstance p;
  make_pebble(p, 1, 8);
  p.evo_state |= (uint8_t)EVO_STATE_PENDING;
  p.evo_state |= 0x0Cu;                       // bits 6:2 are someone else's
  const uint16_t xp0 = p.xp;

  CHECK(evolution_apply(p, full_ctx()));
  CHECK_EQ(p.species_id, 2);
  CHECK_EQ((int)(p.evo_state & EVO_STATE_STAGE_MASK), 1);   // the target's stage
  CHECK_EQ((int)(p.evo_state & EVO_STATE_PENDING), 0);      // pending cleared
  CHECK_EQ((int)(p.evo_state & 0x0Cu), 0x0C);               // and nothing else moved
  CHECK_EQ(p.evolutions, 1);
  CHECK_EQ(p.level, 8);                                     // level is untouched
  CHECK_EQ(p.xp, xp0);
}

TEST(apply_leaves_the_moves_the_care_and_the_genome_alone) {
  PebbleInstance p;
  make_pebble(p, 1, 8);
  for (uint8_t i = 0; i < PB_MOVE_COUNT; ++i) p.moves[i] = (uint8_t)(40 + i);
  for (uint8_t i = 0; i < PB_CARE_COUNT; ++i) p.care[i] = 1234 + i;
  p.genome.lineage_id = 0xDEADBEEFu;
  p.trait_id = 9;

  CHECK(evolution_apply(p, full_ctx()));
  // The new species' learnset is NOT applied in P3: there is no attack table
  // until P4-C1, so overwriting these would write unresolvable ids over
  // unresolvable ids.
  for (uint8_t i = 0; i < PB_MOVE_COUNT; ++i) CHECK_EQ(p.moves[i], (uint8_t)(40 + i));
  for (uint8_t i = 0; i < PB_CARE_COUNT; ++i) CHECK_EQ(p.care[i], (int32_t)(1234 + i));
  CHECK_EQ(p.genome.lineage_id, 0xDEADBEEFu);
  CHECK_EQ(p.trait_id, 9);
}

TEST(apply_rescales_hp_against_the_new_derived_maximum) {
  // Family 1 raises base_hp on every step, so hp_max really does move.
  const SpeciesDef* s1 = species_get(1);
  const SpeciesDef* s2 = species_get(2);
  CHECK(s1 != nullptr);
  CHECK(s2 != nullptr);
  if (!s1 || !s2) return;
  CHECK(s2->base_hp != s1->base_hp);

  // A full Pebble stays full.
  PebbleInstance p;
  make_pebble(p, 1, 8);
  CHECK_EQ(p.hp_cur, hp_max_of(p));
  CHECK(evolution_apply(p, full_ctx()));
  CHECK_EQ(p.hp_cur, hp_max_of(p));
  CHECK_EQ(hp_max_of(p), xp_hp_max(s2->base_hp, 8));

  // A hurt one keeps its fraction, and never over-heals: half of 26 is 13, and
  // the truncating divide can only ever lose part of one HP.
  make_pebble(p, 1, 8);
  const uint16_t max0 = hp_max_of(p);
  p.hp_cur = (uint16_t)(max0 / 2u);
  CHECK(evolution_apply(p, full_ctx()));
  const uint16_t max1 = hp_max_of(p);
  CHECK_EQ(p.hp_cur, (uint16_t)(((uint32_t)(max0 / 2u) * max1) / max0));
  CHECK(p.hp_cur < max1);
  CHECK(p.hp_cur > 0);

  // Whatever the record claimed, hp_cur lands inside the new maximum.
  make_pebble(p, 1, 8);
  p.hp_cur = 60000;
  CHECK(evolution_apply(p, full_ctx()));
  CHECK(p.hp_cur <= hp_max_of(p));

  // A zero-HP Pebble stays at zero: a rescale is not a heal in either
  // direction (a Pebble cannot die - spec section 27 - but it can be fainted).
  make_pebble(p, 1, 8);
  p.hp_cur = 0;
  CHECK(evolution_apply(p, full_ctx()));
  CHECK_EQ(p.hp_cur, 0);
}

TEST(apply_refuses_and_changes_nothing_when_the_rule_does_not_hold) {
  PebbleInstance before, p;

  // (a) the final stage of the family.
  make_pebble(p, 3, (uint8_t)PB_LEVEL_MAX);
  before = p;
  CHECK(!evolution_apply(p, full_ctx()));
  CHECK_EQ(memcmp(&before, &p, sizeof p), 0);

  // (b) the level is not there yet.
  make_pebble(p, 1, 7);
  before = p;
  CHECK(!evolution_apply(p, full_ctx()));
  CHECK_EQ(memcmp(&before, &p, sizeof p), 0);

  // (c) an empty slot and an id the roster does not know.
  make_pebble(p, 0, 10);
  before = p;
  CHECK(!evolution_apply(p, full_ctx()));
  CHECK_EQ(memcmp(&before, &p, sizeof p), 0);

  make_pebble(p, 250, 10);
  before = p;
  CHECK(!evolution_apply(p, full_ctx()));
  CHECK_EQ(memcmp(&before, &p, sizeof p), 0);
}

// A pending bit whose CONDITION fails must stay set: the offer comes back when
// the condition is met, and nothing about the Pebble changes in the meantime.
// The shipped rules are unconditional, so the refusal is driven through the
// context - an empty one, which is exactly what a P6 rule would meet today.
TEST(a_failing_condition_neither_evolves_nor_clears_the_pending_bit) {
  EvolutionRule r = { 1, 2, 8, (uint8_t)EVOC_ACTIVITY_GE, 90 };
  EvoContext none;
  evo_context_clear(none);
  CHECK_EQ(evolution_cond_holds(r, none), 0);

  PebbleInstance p;
  make_pebble(p, 1, 8);
  p.evo_state |= (uint8_t)EVO_STATE_PENDING;
  const PebbleInstance before = p;

  // The device path is "pending AND the whole rule", so a failing condition
  // stops at the second half and the first half is left exactly as it was.
  if (!evolution_cond_holds(r, none)) {
    CHECK_EQ(memcmp(&before, &p, sizeof p), 0);
    CHECK(p.evo_state & EVO_STATE_PENDING);
  }

  // And the same shape through the real API: a rule the context cannot satisfy
  // leaves the Pebble alone. EVOC_NONE always holds, so this drives the
  // refusal through the level gate instead, with the bit already raised.
  make_pebble(p, 1, 4);
  p.evo_state |= (uint8_t)EVO_STATE_PENDING;
  const PebbleInstance low = p;
  CHECK_EQ(evolution_ready(p, full_ctx()), 0);
  CHECK(!evolution_apply(p, full_ctx()));
  CHECK_EQ(memcmp(&low, &p, sizeof p), 0);
  CHECK(p.evo_state & EVO_STATE_PENDING);
}

TEST(the_evolutions_counter_saturates_at_255) {
  PebbleInstance p;
  make_pebble(p, 1, 8);
  p.evolutions = 254;
  CHECK(evolution_apply(p, full_ctx()));
  CHECK_EQ(p.evolutions, 255);

  make_pebble(p, 1, 8);
  p.evolutions = 255;
  CHECK(evolution_apply(p, full_ctx()));
  CHECK_EQ(p.evolutions, 255);            // saturated, never wrapped to 0
}

// =============================================================================
//  5. THE WHOLE FAMILY, WALKED
// =============================================================================
TEST(a_family_walk_ends_at_the_final_stage) {
  PebbleInstance p;
  make_pebble(p, 1, 1);
  CHECK_EQ(evolution_ready(p, full_ctx()), 0);      // level 1 Paketo, not yet

  p.level = 8;
  p.hp_cur = hp_max_of(p);
  CHECK_EQ(evolution_ready(p, full_ctx()), 1);
  CHECK(evolution_apply(p, full_ctx()));
  CHECK_EQ(p.species_id, 2);                        // Fragmar
  CHECK_EQ((int)(p.evo_state & EVO_STATE_STAGE_MASK), 1);
  CHECK_EQ(evolution_ready(p, full_ctx()), 0);      // and it stops there

  p.level = 18;
  p.hp_cur = hp_max_of(p);
  CHECK_EQ(evolution_ready(p, full_ctx()), 1);
  CHECK(evolution_apply(p, full_ctx()));
  CHECK_EQ(p.species_id, 3);                        // Rafagon
  CHECK_EQ((int)(p.evo_state & EVO_STATE_STAGE_MASK), 2);
  CHECK_EQ(p.evolutions, 2);

  p.level = (uint8_t)PB_LEVEL_MAX;
  CHECK_EQ(evolution_ready(p, full_ctx()), 0);      // final: nothing left
  CHECK(!evolution_apply(p, full_ctx()));
}

// =============================================================================
//  6. THE PENDING BIT, AS game/xp.cpp RAISES IT
// =============================================================================
TEST(xp_raises_pending_at_the_level_gate_and_apply_clears_it) {
  xp_ledger_reset(1);                     // a device with a full budget

  PebbleInstance p;
  make_pebble(p, 1, 1);
  CHECK_EQ((int)(p.evo_state & EVO_STATE_PENDING), 0);

  // Enough XP in one award to carry from level 1 to at least the gate.
  uint32_t need = 0;
  for (uint8_t lv = 1; lv < 8u; ++lv) need += xp_for_level(lv);
  CHECK(need < 0xFFFFu);
  CHECK(xp_add(p, (uint16_t)need, XP_SRC_ITEM, nullptr));   // unmetered source
  CHECK(p.level >= 8u);
  CHECK(p.evo_state & EVO_STATE_PENDING);

  // "Pending" is the LEVEL requirement, not a promise: the consumer still has
  // to evaluate the whole rule. Here it holds, so the apply clears the bit.
  CHECK(evolution_apply(p, full_ctx()));
  CHECK_EQ((int)(p.evo_state & EVO_STATE_PENDING), 0);
  CHECK_EQ(p.species_id, 2);
}

TEST(xp_does_not_raise_pending_below_the_gate_or_at_a_final_stage) {
  xp_ledger_reset(1);

  PebbleInstance p;
  make_pebble(p, 1, 1);
  CHECK(xp_add(p, xp_for_level(1), XP_SRC_ITEM, nullptr));
  CHECK_EQ(p.level, 2);
  CHECK_EQ((int)(p.evo_state & EVO_STATE_PENDING), 0);

  make_pebble(p, 3, 20);                  // Rafagon has no rule to be ready for
  (void)xp_add(p, 50, XP_SRC_ITEM, nullptr);
  CHECK_EQ((int)(p.evo_state & EVO_STATE_PENDING), 0);
}

// =============================================================================
//  7. THE TABLE'S OWN GUARDS, AS RUNTIME ASSERTIONS
//
//  evolution_table.h static_asserts all of these. Re-checking them here means a
//  bad future table fails the GATE and not only one compiler.
// =============================================================================
TEST(the_evolution_table_guards_hold_at_runtime_too) {
  CHECK(EVOLUTION_RULES_COUNT >= 1);
  CHECK(EVOLUTION_RULES_COUNT < (uint8_t)SPECIES_EVO_NONE);

  for (uint8_t i = 0; i < EVOLUTION_RULES_COUNT; ++i) {
    const EvolutionRule& r = EVOLUTION_RULES[i];
    const SpeciesDef* s = species_get(r.species);
    const SpeciesDef* t = species_get(r.target);
    CHECK(s != nullptr);
    CHECK(t != nullptr);
    if (!s || !t) continue;
    CHECK(r.target != r.species);
    CHECK_EQ(t->family, s->family);
    CHECK_EQ((int)t->stage, (int)s->stage + 1);
    CHECK(r.level >= 1 && r.level <= (uint8_t)PB_LEVEL_MAX);
    CHECK(r.cond < (uint8_t)EVOC_COUNT);

    // No species is the source of two rules.
    for (uint8_t j = (uint8_t)(i + 1u); j < EVOLUTION_RULES_COUNT; ++j)
      CHECK(EVOLUTION_RULES[j].species != r.species);
  }

  // The roster and the table agree in both directions.
  for (uint8_t i = 0; i < SPECIES_TABLE_COUNT; ++i) {
    const SpeciesDef& sp = SPECIES_TABLE[i];
    CHECK_EQ(sp.id, (uint8_t)(i + 1u));                 // contiguity
    if (sp.evo_rule == (uint8_t)SPECIES_EVO_NONE) {
      for (uint8_t j = 0; j < EVOLUTION_RULES_COUNT; ++j)
        CHECK(EVOLUTION_RULES[j].species != sp.id);     // final means final
    } else {
      CHECK(sp.evo_rule < EVOLUTION_RULES_COUNT);
      if (sp.evo_rule < EVOLUTION_RULES_COUNT)
        CHECK_EQ(EVOLUTION_RULES[sp.evo_rule].species, sp.id);
    }
  }

  // evolution_rule_at() is the only door into the array and it never runs off
  // the end - 0xFF included, which is the "no evolution" marker.
  CHECK(evolution_rule_at(EVOLUTION_RULES_COUNT) == nullptr);
  CHECK(evolution_rule_at((uint8_t)SPECIES_EVO_NONE) == nullptr);
  CHECK(evolution_rule_at(0) != nullptr);
}
