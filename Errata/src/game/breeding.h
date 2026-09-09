// =============================================================================
//  ERRATA - game/breeding.h
//  BREEDING AS A GAME RULE (spec section 17, plan P7-C5), over the genetics
//  game/genome.cpp already owns.
//
//  genome_breed() is a GENETICS function: it averages, crosses over, mutates,
//  recombines a hybrid species, propagates the god taint as A | B, saturates
//  the generation counter and seals the result. It will breed ANYTHING it is
//  given, from any two genomes, out of the shared RNG_BREEDING stream. This
//  module is the four things around it that make a breeding a game event:
//
//    1. WHO MAY BREED WITH WHOM (compatibility, section 17, data-driven);
//    2. WHAT COMES OUT (a species, a level, a learnset, an origin, a flag);
//    3. HOW FAR A DYNASTY MAY DRIFT (the genesis envelope clamp);
//    4. WHETHER TWO DEVICES GET THE SAME CHILD (the shared-seed determinism).
//
// -----------------------------------------------------------------------------
//  1. COMPATIBILITY - SpeciesDef.compat_group, WHICH NOTHING READ UNTIL NOW
// -----------------------------------------------------------------------------
//  Section 17 says compatibility is data-driven, and the datum has existed
//  since P4-C1: SpeciesDef.compat_group (data/species_table.h), generator-
//  guarded non-zero, and read by NOTHING in src/ before this file. The rule is
//  the plan's, verbatim: the SAME non-zero compat_group, both parents at
//  stage >= 1, distinct ids.
//
//  WHY stage >= 1 IS A REAL RULE AND NOT A FORMALITY: a stage-0 creature is the
//  base of its family, the shape a bred offspring itself comes out as. Letting
//  two babies breed would make a dynasty that never grows up, and it is the one
//  rule of the three that a careless implementation drops without any test
//  noticing - so tests/test_breeding.cpp sweeps the WHOLE 36 x 36 roster matrix
//  rather than a handful of pairs.
//
//  genome_can_mate() IS DELIBERATELY NOT CALLED. It is the sex/luck rule of the
//  retired BLE mating protocol, it is GENETICS ONLY (its own header says so),
//  and it has had no caller since P2-C7b. Adding it here would make the compat
//  matrix depend on a 50/50 genome bit, i.e. make half of every legal pair
//  refuse for a reason section 17 does not name.
//
// -----------------------------------------------------------------------------
//  2. WHAT COMES OUT, AND THE PLAN BULLET THIS FILE CONTRADICTS ON PURPOSE
// -----------------------------------------------------------------------------
//  Species: species_base_of_family() of the family of ONE parent, chosen by the
//  shared seed. species_family_bases_resolve() already guarantees every family
//  has exactly one stage-0 row, which is what makes a bred offspring land on a
//  creature that can still evolve.
//
//  MOVES: THE BASE SPECIES' LEARNSET, VERBATIM. The plan bullet says "one
//  inherited move from each parent if legal" and THAT CANNOT BE BUILT AGAINST
//  THE SHIPPED VALIDATOR. game/validate.cpp's moveset_is_learnable() accepts a
//  moveset only when it is the verbatim, in-order moves[4] of some species in
//  the same family at this stage or below; a stage-0 offspring's only candidate
//  is its own base row, so ANY substituted move is VR_UNLEARNABLE_MOVESET - the
//  child would be quarantined on the next load and refused by pbw_decode() on
//  the wire. The alternative is to widen the validator to set membership, which
//  reverses a rule with a MEASUREMENT behind it (validate.cpp: injecting
//  attack 11 onto a species-1 Paketo took a scripted 1v1 from 0/200 wins to
//  100), and it would have to be widened identically in game/battle.cpp's
//  second learnset checker, which is pinned to the first by
//  `the_two_learnset_checkers_agree_on_every_roster_row`. The bullet is
//  rewritten in the plan rather than implemented.
//
//  Level 1, xp 0, full care, ORIGIN_BRED, PBF_BRED, hp at the derived maximum -
//  every one of those through game/box.cpp's box_new_bug(), the tree's one
//  Bug constructor, and then validate_bug() before the slot is kept.
//
// -----------------------------------------------------------------------------
//  3. THE ENVELOPE CLAMP - THE ONE CEILING THAT IS NOT ALREADY FREE
// -----------------------------------------------------------------------------
//  Section 17 asks for a "hard balance ceiling". THE BATTLE HALF OF IT ALREADY
//  HOLDS BY CONSTRUCTION AND THIS HEADER SAYS SO RATHER THAN CLAIMING CREDIT:
//  game/bug.h folds a gene through gvar(v) = v * 3 / 16, so 0..15 becomes
//  0..2 whatever any number of generations does, BUG_GENOME_VAR_MAX is 2,
//  and game/validate.h's rule (b) says a range check on a gene is a test that
//  cannot fail. Nothing this module does could raise a bred Bug's atk, def
//  or spd above base + level/3 + 2, and a test asserting that would be a test
//  that cannot fail.
//
//  WHAT IS NOT FREE IS THE CARE ENVELOPE. genome_genesis() rolls every numeric
//  gene in GENESIS_GENE_MIN..GENESIS_GENE_MAX (luck GENESIS_LUCK_MIN..MAX);
//  genome_breed()'s mutation reflects inside the GENE's own 0..15, so a lineage
//  drifts outside the band the roster was tuned in. That matters because
//  gene_appetite_mult(), gene_metabolism_mult() and gene_hardiness_mult() run
//  the full 600..1500 per-mille and drive care decay and battle damage taken -
//  MEASURED over 40 generations of unclamped breeding in tests/test_breeding.cpp
//  and reported there as a number.
//
//  So breed_compute() clamps the six numeric genes back into the genesis band
//  after genome_breed() returns, and reseals. That is the ceiling, it is the
//  half that can be broken, and it is the half the tests mutate.
//
//  ONE CONSEQUENCE, STATED: the clamp also truncates genome_breed()'s
//  inbreeding penalty (hardiness -2) when it would push hardiness below the
//  band, so inbreeding costs a full 2 only from 7 upward. EF_INBRED still rides
//  out in BreedPlan.flags, so the effect is nameable rather than silent.
//
// -----------------------------------------------------------------------------
//  4. DETERMINISM: A FILE-SCOPE RNG, AND WHY THAT IS THE SAFE END OF THE TRAP
// -----------------------------------------------------------------------------
//  Both devices must compute the SAME child. game/capture.h already names the
//  trap and the answer: "a caller that wanted a deterministic capture and
//  reached for genome_seed() would silently reseed the shared BREEDING stream
//  and change every later breeding outcome in the boot. If determinism is ever
//  wanted here, the fix is a capture-local source through genome_set_rng(),
//  never a reseed." breed_compute() does exactly that - installs a scripted
//  source seeded from the shared seed, calls genome_breed(), restores nullptr -
//  so RNG_BREEDING is not touched and a breeding changes no later draw.
//
//  TWO CONSEQUENCES, BOTH WRITTEN DOWN RATHER THAN DISCOVERED:
//    * genome_set_rng() takes a bare uint32_t(*)(void) with no context, so this
//      module's scripted state is FILE-SCOPE. Two endpoints cannot be
//      interleaved through breed_compute() inside one host process; they must
//      breed one after the other. The host test does exactly that and says so.
//    * THE ORDER OF THE DRAWS INSIDE genome_breed() IS NOW PART OF A
//      CROSS-DEVICE CONTRACT. Reordering them is a protocol change, not a
//      refactor, and it needs a PROTOCOL_VERSION bump like any other.
//
//  PURE MODULE. stdint, string.h, the save schema, the content tables,
//  game/box.h, game/genome.h, game/species.h, game/taint.h and game/validate.h.
//  No Arduino, no heap, no float, no clock (now_epoch is a parameter), no
//  esp_random(), no I/O.
// =============================================================================
#ifndef ER_GAME_BREEDING_H
#define ER_GAME_BREEDING_H

#include <stdint.h>

#include "../core/nt_types.h"              // Genome, GENESIS_GENE_*, GENESIS_LUCK_*
#include "../persistence/save_schema.h"    // BugInstance

// -----------------------------------------------------------------------------
//  WHY A PAIR WAS REFUSED. NEVER A BOOL, for game/battle.h's reason: a test
//  written against `!= BRD_OK` still passes when the wrong guard fires and
//  still passes after the guard it names is deleted.
//
//  ITS OWN ENUM AND NOT A 28th VReject. A policy refusal is not a validity
//  verdict (game/taint.h says the same about the taint gate), and the two
//  parents of a refused pair are perfectly legal Bugs.
//
//  THE ORDER IS THE EVALUATION ORDER: the first failing reason is the answer,
//  and tests/test_breeding.cpp pins the exact code for every case.
// -----------------------------------------------------------------------------
enum BreedReject : uint8_t {
  BRD_OK = 0,
  BRD_NO_BOX,             // box_bind() has not run: no Box to file a child in
  BRD_SAME_UNIT,          // one Bug cannot breed with itself (equal ids)
  BRD_UNKNOWN_SPECIES,    // species_get() answers nullptr for a parent. CHECKED
                          // BEFORE the validator on purpose: the other order
                          // makes this code unreachable, because
                          // validate_bug() answers VR_UNKNOWN_SPECIES first.
  BRD_INVALID_PARENT,     // validate_bug() refused a parent
  BRD_STAGE,              // a parent is still at stage 0
  BRD_COMPAT_GROUP,       // different compat_group, or a zero one
  BRD_TAINT,              // game/taint.h: a clean unit refuses a tainted one
  BRD_NO_BASE_SPECIES,    // the chosen family has no stage-0 row (unreachable
                          // while species_family_bases_resolve() holds, and a
                          // named answer rather than a silent species 0)
  BRD_BOX_FULL,           // ten slots and no more (invariant B1)
  BRD_INVALID_CHILD,      // the offspring failed validate_bug(): a bug HERE,
                          // never a peer capability, and the slot is released
  BRD_REJECT_COUNT
};

const char* breed_reject_name(BreedReject r);

// -----------------------------------------------------------------------------
//  THE OFFSPRING, COMPUTED BUT NOT YET FILED.
//
//  Two devices compute this from the same two parents and the same seed and
//  MUST get identical bytes; only then does each side ask its own player. It
//  carries no id and no slot: the id is minted locally at commit, because the
//  two devices file two different Bugs into two different Boxes.
// -----------------------------------------------------------------------------
struct BreedPlan {
  uint32_t seed;          // the shared seed this child is a function of
  Genome   genome;        // sealed; genome_valid() is true
  uint8_t  species_id;    // the base-stage row of the chosen parent's family
  uint8_t  level;         // always 1
  uint8_t  flags;         // EF_FROM_MATING | EF_INBRED | EF_NEW_LINEAGE | EF_HYBRID
  uint8_t  from_b;        // 1 when the family came from parent B, for the UI
};

// COMPATIBILITY ONLY. No RNG, no Box write, no side effect at all: the LINK
// screen calls it to grey out a row and breed_compute() calls it first.
// `a` and `b` are in the caller's canonical order and the answer is symmetric.
BreedReject breed_check(const BugInstance& a, const BugInstance& b);

// THE CHILD. Deterministic in (a, b, shared_seed) and in nothing else -
// RNG_BREEDING is not read and not reseeded (see section 4 of the banner).
//
// THE ARGUMENT ORDER IS PART OF THE CONTRACT: `a` is the INITIATOR's parent and
// `b` the responder's on BOTH devices, so the two ends must order them by
// SessionRole and not by which one is local. networking/breed_link.cpp is the
// one caller that does it, and swapping them produces a different, equally
// valid child - which is exactly the desync the test
// `the_two_ends_must_agree_on_which_parent_is_a` pins.
BreedReject breed_compute(const BugInstance& a, const BugInstance& b,
                          uint32_t shared_seed, BreedPlan& out);

// FILES IT. The caller has already asked its player (audit risk 5: there is no
// auto-accepted egg), and the caller commits the Box afterwards - this module
// performs no I/O. Fails BRD_BOX_FULL with nothing written when the Box is
// full, and BRD_INVALID_CHILD with the slot released when the offspring does
// not pass validate_bug().
BreedReject breed_commit(const BreedPlan& plan, uint32_t now_epoch, uint8_t& slot_out);

#endif  // ER_GAME_BREEDING_H
