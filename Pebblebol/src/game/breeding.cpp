// =============================================================================
//  PEBBLEBOL - game/breeding.cpp
//  See breeding.h for the four rules, the plan bullet this file deliberately
//  contradicts (inherited moves), and the determinism trap it walks around.
//
//  THE ONE FILE-SCOPE MUTABLE IN THIS MODULE IS THE SCRIPTED RNG, and it is
//  there because genome_set_rng() takes a bare uint32_t(*)(void) with no
//  context. breeding.h states the consequence: two endpoints cannot be
//  interleaved through breed_compute() in one process.
// =============================================================================
#include "breeding.h"

#include <string.h>

#include "../core/rng.h"
#include "box.h"
#include "genome.h"
#include "species.h"
#include "taint.h"
#include "validate.h"

// -----------------------------------------------------------------------------
//  NAMES
// -----------------------------------------------------------------------------
static const char* const BRD_NAMES[] = {
  "BRD_OK", "BRD_NO_BOX", "BRD_SAME_UNIT", "BRD_UNKNOWN_SPECIES",
  "BRD_INVALID_PARENT", "BRD_STAGE", "BRD_COMPAT_GROUP", "BRD_TAINT",
  "BRD_NO_BASE_SPECIES", "BRD_BOX_FULL", "BRD_INVALID_CHILD"
};
static_assert(sizeof(BRD_NAMES) / sizeof(BRD_NAMES[0]) == (size_t)BRD_REJECT_COUNT,
              "a BreedReject was added without its name");

const char* breed_reject_name(BreedReject r)
{
  if ((uint8_t)r >= (uint8_t)BRD_REJECT_COUNT) return "BRD_?";
  return BRD_NAMES[(uint8_t)r];
}

// -----------------------------------------------------------------------------
//  THE SCRIPTED SOURCE (banner section 4)
// -----------------------------------------------------------------------------
static Rng s_breed_rng;

static uint32_t breed_rng_draw(void)
{
  return rng_next(s_breed_rng);
}

// -----------------------------------------------------------------------------
//  1. COMPATIBILITY
// -----------------------------------------------------------------------------
BreedReject breed_check(const PebbleInstance& a, const PebbleInstance& b)
{
  // Distinct UNITS, by id. Not by pointer and not by species: two Pebbles of
  // one species are a perfectly good pair, and the same Pebble handed in twice
  // is the thing spec section 9 calls a duplication.
  if (a.id == 0u || b.id == 0u || a.id == b.id) return BRD_SAME_UNIT;

  // THE SPECIES LOOKUP COMES BEFORE validate_pebble() AND THAT ORDER IS THE
  // REASON BRD_UNKNOWN_SPECIES IS REACHABLE AT ALL. The other way round, the
  // validator answers VR_UNKNOWN_SPECIES first and this code could never be
  // produced by this function - a named reject nothing can return is a name,
  // not a rule. A save from a newer content pack is exactly how a caller gets
  // here, and it deserves its own word rather than "invalid parent".
  const SpeciesDef* sa = species_get(a.species_id);
  const SpeciesDef* sb = species_get(b.species_id);
  if (sa == nullptr || sb == nullptr) return BRD_UNKNOWN_SPECIES;

  if (validate_pebble(a) != VR_OK || validate_pebble(b) != VR_OK)
    return BRD_INVALID_PARENT;

  if (sa->stage < 1u || sb->stage < 1u) return BRD_STAGE;

  if (sa->compat_group == 0u || sb->compat_group == 0u ||
      sa->compat_group != sb->compat_group)
    return BRD_COMPAT_GROUP;

  // THE GOD-TAINT GATE, ON BOTH PARENTS AND IN BOTH DIRECTIONS. genome_breed()
  // propagates the taint as A | B, so a clean parent bred against a tainted one
  // produces a tainted child and every descendant of it - which is precisely
  // the pollution game/taint.h refuses. Evaluated symmetrically here because a
  // breeding, unlike a trade, has no "incoming" side: each device is asked to
  // put one of its own Pebbles into the same pot.
  if (!taint_gate_ok(a, b) || !taint_gate_ok(b, a)) return BRD_TAINT;

  return BRD_OK;
}

// -----------------------------------------------------------------------------
//  3. THE ENVELOPE CLAMP
// -----------------------------------------------------------------------------
static uint8_t clamp_gene(uint8_t v, uint8_t lo, uint8_t hi)
{
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

// Every gene_set_*() masks and RESEALS, so the genome is never observable
// half-edited and genome_valid() holds on the way out.
static void clamp_to_genesis_envelope(Genome& g)
{
  const uint8_t lo = (uint8_t)GENESIS_GENE_MIN;
  const uint8_t hi = (uint8_t)GENESIS_GENE_MAX;
  gene_set_appetite   (g, clamp_gene(gene_appetite(g),    lo, hi));
  gene_set_metabolism (g, clamp_gene(gene_metabolism(g),  lo, hi));
  gene_set_sociability(g, clamp_gene(gene_sociability(g), lo, hi));
  gene_set_temperament(g, clamp_gene(gene_temperament(g), lo, hi));
  gene_set_hardiness  (g, clamp_gene(gene_hardiness(g),   lo, hi));
  gene_set_luck       (g, clamp_gene(gene_luck(g),
                                     (uint8_t)GENESIS_LUCK_MIN,
                                     (uint8_t)GENESIS_LUCK_MAX));
}

// -----------------------------------------------------------------------------
//  2 + 4. THE CHILD
// -----------------------------------------------------------------------------
BreedReject breed_compute(const PebbleInstance& a, const PebbleInstance& b,
                          uint32_t shared_seed, BreedPlan& out)
{
  memset(&out, 0, sizeof out);

  const BreedReject c = breed_check(a, b);
  if (c != BRD_OK) return c;

  out.seed = shared_seed;

  // The scripted source, installed for exactly the span below. RNG_BREEDING is
  // neither read nor reseeded: see game/capture.h for the trap this avoids.
  rng_init(s_breed_rng, shared_seed);
  genome_set_rng(&breed_rng_draw);

  // THE FAMILY PICK IS THE FIRST DRAW, BEFORE genome_breed()'s. It has to be
  // some fixed position in the one stream both devices run, and first is the
  // position that does not move when genome_breed() gains or loses a draw.
  const uint8_t from_b = (uint8_t)(breed_rng_draw() & 1u);
  out.from_b = from_b;

  // cq_hi WAS THE BLE BEACON'S QUALITY BYTE AND THE BEACON IS GOING AWAY, so
  // genome_breed() is handed the parent's LEVEL instead: a number both devices
  // already have (it is in the 48 B wire record), deterministic, and a fair
  // reading of "which dynasty is the established one". Ties go to A, which is
  // genome_breed()'s own rule.
  uint8_t flags = 0;
  Genome child = genome_breed(a.genome, b.genome, a.level, b.level, &flags);

  genome_set_rng(nullptr);

  clamp_to_genesis_envelope(child);

  out.genome = child;
  out.flags  = flags;
  out.level  = 1u;

  const SpeciesDef* parent = species_get(from_b ? b.species_id : a.species_id);
  if (parent == nullptr) return BRD_UNKNOWN_SPECIES;      // breed_check saw one
  const uint8_t base = species_base_of_family(parent->family);
  if (base == 0u || species_get(base) == nullptr) return BRD_NO_BASE_SPECIES;
  out.species_id = base;

  return BRD_OK;
}

// -----------------------------------------------------------------------------
//  FILING IT
// -----------------------------------------------------------------------------
BreedReject breed_commit(const BreedPlan& plan, uint32_t now_epoch, uint8_t& slot_out)
{
  slot_out = (uint8_t)BOX_SLOT_NONE;
  if (!box_bound()) return BRD_NO_BOX;
  if (plan.species_id == 0u || species_get(plan.species_id) == nullptr)
    return BRD_UNKNOWN_SPECIES;

  // THE TREE'S ONE CONSTRUCTOR. box_new_pebble() owns evo_state, the learnset,
  // the derived hp, full care and the minted id; re-deriving any of them here
  // would be the second copy game/box.h exists to prevent.
  const uint8_t slot = box_new_pebble(plan.species_id, plan.level,
                                      (uint8_t)ORIGIN_BRED, plan.genome,
                                      plan.seed, now_epoch);
  if (slot == (uint8_t)BOX_SLOT_NONE) return BRD_BOX_FULL;

  PebbleInstance* p = box_slot(slot);
  if (p == nullptr) return BRD_BOX_FULL;                  // unreachable
  p->flags = (uint8_t)(p->flags | (uint8_t)PBF_BRED);

  // The one validator, on the thing this module made, BEFORE the caller is told
  // it has a Pebble. A failure here is a bug in this file - never a peer
  // capability - so the slot goes back rather than being quarantined later.
  if (validate_pebble(*p) != VR_OK) {
    (void)box_release(slot, true);
    return BRD_INVALID_CHILD;
  }

  slot_out = slot;
  return BRD_OK;
}
