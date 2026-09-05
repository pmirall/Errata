// =============================================================================
//  PEBBLEBOL - game/capture.h
//  CAPTURE (spec section 23, plan P5-C4).
//
//  The chance, the attempt, the flee, and the one call that mints the creature.
//
// -----------------------------------------------------------------------------
//  A CAPTURED PEBBLE MUST PASS THE VALIDATOR - the carried-forward debt this
//  file discharges, and what it actually took
// -----------------------------------------------------------------------------
//  game/validate.h records that box_new_pebble(), the tree's ONE constructor,
//  deliberately does NOT call validate_pebble(): five shipped host tests hand
//  it a zeroed or hand-built genome, so making the constructor refuse would
//  break them. That left capture as a third minting path with no VR_OK
//  requirement anywhere.
//
//  MEASURED BEFORE DESIGNING ANYTHING, because the shape of the answer depends
//  on it: driving box_new_pebble() -> validate_pebble() across the whole
//  36-species roster at every level 1..30 gives 0 of 1,080 rejects WHEN THE
//  GENOME IS SEALED, and VR_BAD_GENOME for every single row when it is not.
//  The genome seal is the ONLY input that can make a constructed Pebble
//  invalid; the level clamp, the stage bits, the moves, the hp and the evo
//  state cannot.
//
//  SO THE REFUSAL IS A PRE-CHECK AND THE VALIDATION IS A POST-CONDITION:
//    * cap_attempt() refuses with CAP_BAD_GENOME, by name, BEFORE it constructs
//      anything, if the genome it was handed is not sealed. That is the one
//      reject the constructor can produce, and refusing it early is what stops
//      a Pebble the wire path would later reject from ever being filed.
//    * it then constructs, and validates the FILED slot. A non-VR_OK there is
//      reported as CAP_INTERNAL with the named VReject in `out.reject` - the
//      same posture game/validate.h prescribes for a BattleReject after
//      validate_team(): a bug in this tree, not a peer capability and not a
//      user-visible "capture failed".
//
//  ONE HONEST GAP, MEASURED AND NOT PAPERED OVER: THE POST-CONDITION HAS NO
//  REACHABLE FALSIFIER AT THIS ROSTER, so no mutation can prove it fires.
//  Deleting the validate_pebble() call entirely and hardcoding VR_OK leaves
//  every host case green - it was tried - because with a sealed genome the
//  answer is VR_OK on all 1,080 rows and the tests assert the FILED PEBBLE is
//  valid (which it is) rather than that this function checked. The pre-check
//  above IS mutation-covered (drop it and 1,080 rows change answer by name).
//  What would make the post-condition reachable is a content pack whose
//  learnsets disagree with validate_pebble()'s family walk, which is exactly
//  what the P9 roster change risks - so it stays, as an assertion with its
//  falsifier stated rather than implied.
//
//  AND THE PEBBLE IS NOT DESTROYED ON THAT PATH, which is a decision rather
//  than an omission. "File, validate, undo" reads better and CANNOT BE WRITTEN
//  CORRECTLY here: box_new_pebble() mints AND files in one call, mask_sync()
//  makes the first Pebble in an empty Box the ACTIVE one (invariant B3), and
//  box_release() refuses the active slot outright (B4). Measured: releasing the
//  slot a first capture landed in returns 0. So the undo branch would be
//  unreachable in exactly the case a first-boot player hits, and an unreachable
//  branch that looks like a safety net is worse than none. Spec section 23's
//  "the encounter must never delete the active Pebble" and section 55's "never
//  destroys" point the same way: report the fault, keep the creature.
//
// -----------------------------------------------------------------------------
//  DETERMINISM, AND THE TRAP IN THE GENOME
// -----------------------------------------------------------------------------
//  The capture ROLL is real randomness and must be: re-entering an encounter
//  and getting the same failure again is not a game. `roll` is handed in, so
//  this module draws from nothing - ui/screen_encounter.cpp draws it from
//  RNG_ENCOUNTER, which is what the plan's bullet names that stream for.
//
//  The genome is handed in too, and the reason is a trap worth naming:
//  genome_genesis() draws through rng_u32(RNG_BREEDING), so a caller that
//  wanted a deterministic capture and reached for genome_seed() would silently
//  reseed the shared BREEDING stream and change every later breeding outcome in
//  the boot. If determinism is ever wanted here, the fix is a capture-local
//  source through genome_set_rng(), never a reseed.
//
// -----------------------------------------------------------------------------
//  PURE MODULE. stdint, the save schema, the content tables, game/box.h,
//  game/genome.h and game/validate.h. No Arduino, no clock, no RNG, no heap,
//  no float, no I/O. tests/test_capture.cpp compiles it directly.
// =============================================================================
#ifndef PB_GAME_CAPTURE_H
#define PB_GAME_CAPTURE_H

#include <stdint.h>

#include "../core/nt_types.h"              // Genome
#include "../data/balance.h"               // CAPTURE_*
#include "../persistence/save_schema.h"    // PebbleInstance
#include "encounters.h"                    // EncounterResult

// -----------------------------------------------------------------------------
//  WHY AN ATTEMPT ENDED. NEVER A BOOL, for game/validate.h's reason: a test
//  written against "it did not work" still passes when the WRONG refusal fires.
// -----------------------------------------------------------------------------
enum CaptureOutcome : uint8_t {
  CAP_CAUGHT = 0,     // filed, validated, and out.slot names it
  CAP_ESCAPED,        // the roll failed and attempts remain
  CAP_FLED,           // the roll failed and that was CAPTURE_MAX_ATTEMPTS
  CAP_BOX_FULL,       // nothing was rolled: the player must free a slot first
                      // (spec 23: "never silently discard")
  CAP_NO_ENCOUNTER,   // the result handed in is not a WILD one
  CAP_BAD_GENOME,     // the genome is not sealed - refused BEFORE constructing
  CAP_INTERNAL,       // constructed and then failed validate_pebble(): a bug in
                      // this tree. out.reject carries the named VReject.
  CAP_OUTCOME_COUNT
};

// -----------------------------------------------------------------------------
//  THE ATTEMPT COUNTER, which belongs to the ENCOUNTER and not to this module.
//  A caller-owned struct, exactly as game/cooldowns.h takes its table and
//  networking/wifi_scanner.h takes its job: this file holds no state, so two
//  encounters (or two host cases) cannot interfere.
// -----------------------------------------------------------------------------
struct CaptureState {
  uint8_t attempts;   // failed attempts so far, 0..CAPTURE_MAX_ATTEMPTS
  uint8_t fled;       // 1 once the creature has gone
  uint8_t reserved[2];
};

struct CaptureReport {
  uint8_t  outcome;      // CaptureOutcome
  uint8_t  slot;         // CAP_CAUGHT: the Box slot. Else BOX_SLOT_NONE.
  uint8_t  reject;       // CAP_INTERNAL: the VReject. Else VR_OK.
  uint8_t  attempts_left;
  uint16_t chance;       // the permille this attempt was rolled against
  uint16_t roll;         // the 0..999 it was rolled with, so a failure is
                         // reproducible from the report alone
};

void cap_reset(CaptureState& st);

// -----------------------------------------------------------------------------
// cap_chance_permille(wild_species, wild_level, active_level, item_id)
//
//   CAPTURE_BASE_PERMILLE[the wild species' rarity]
//     - CAPTURE_LEVEL_GAP_PERMILLE * max(0, wild_level - active_level)
//     + ITEM_CAPTURE_SCALE * the capture item's value
//   clamped to [CAPTURE_MIN_PERMILLE, CAPTURE_MAX_PERMILLE].
//
//   item_id 0, an unknown item and an item that is not ITEM_KLASS_CAPTURE all
//   contribute exactly nothing - a bad id may never be worth more than no item.
//   An unknown species answers CAPTURE_MIN_PERMILLE rather than 0, because 0
//   would be an impossible catch and the clamp exists to say there is no such
//   thing.
//
//   MONOTONIC IN ALL THREE ARGUMENTS, and tests/test_capture.cpp asserts it:
//   rarer is never easier, a bigger level gap is never easier, and a better
//   capture item is never worse.
// -----------------------------------------------------------------------------
uint16_t cap_chance_permille(uint8_t wild_species, uint8_t wild_level,
                             uint8_t active_level, uint8_t item_id);

// -----------------------------------------------------------------------------
// cap_attempt(st, enc, active_level, item_id, roll, genome, creation_seed,
//             now_epoch, out) -> out.outcome == CAP_CAUGHT
//
//   ONE attempt. `roll` is any u32; it is reduced to 0..999 here, so a caller
//   may hand it a raw draw. The item is NOT consumed here - the inventory owns
//   that (game/inventory.h) and a module that both rolled and spent would make
//   the two impossible to test apart.
//
//   A fled or already-caught encounter refuses rather than rolling again, so a
//   double tap cannot buy a third attempt.
// -----------------------------------------------------------------------------
bool cap_attempt(CaptureState& st, const EncounterResult& enc,
                 uint8_t active_level, uint8_t item_id, uint32_t roll,
                 const Genome& genome, uint32_t creation_seed,
                 uint32_t now_epoch, CaptureReport& out);

#endif  // PB_GAME_CAPTURE_H
