// =============================================================================
//  ERRATA - game/capture.cpp
//  See capture.h. PURE translation unit.
// =============================================================================
#include "capture.h"

#include <string.h>

#include "box.h"
#include "genome.h"
#include "species.h"
#include "validate.h"

#define CAP_ROLL_SPAN  1000u

void cap_reset(CaptureState& st)
{
  memset(&st, 0, sizeof st);
}

uint16_t cap_chance_permille(uint8_t wild_species, uint8_t wild_level,
                             uint8_t active_level, uint8_t item_id)
{
  const SpeciesDef* sp = species_get(wild_species);
  // An unknown species is CAPTURE_MIN_PERMILLE and not 0: the clamp's whole job
  // is to say that no catch is impossible, and a bad id must not be the one
  // input that makes one.
  if (sp == nullptr) return (uint16_t)CAPTURE_MIN_PERMILLE;

  const uint8_t rar = (sp->rarity < (uint8_t)(sizeof(CAPTURE_BASE_PERMILLE) /
                                              sizeof(CAPTURE_BASE_PERMILLE[0])))
                          ? sp->rarity
                          : (uint8_t)0u;
  int32_t p = (int32_t)CAPTURE_BASE_PERMILLE[rar];

  // ONE-SIDED. A wild creature above you is harder; one below you is not free.
  if (wild_level > active_level)
    p -= (int32_t)CAPTURE_LEVEL_GAP_PERMILLE * (int32_t)(wild_level - active_level);

  const ItemDef* it = item_get(item_id);
  if (it != nullptr && it->klass == (uint8_t)ITEM_KLASS_CAPTURE)
    p += (int32_t)ITEM_CAPTURE_SCALE * (int32_t)it->value;

  if (p < (int32_t)CAPTURE_MIN_PERMILLE) p = (int32_t)CAPTURE_MIN_PERMILLE;
  if (p > (int32_t)CAPTURE_MAX_PERMILLE) p = (int32_t)CAPTURE_MAX_PERMILLE;
  return (uint16_t)p;
}

bool cap_attempt(CaptureState& st, const EncounterResult& enc,
                 uint8_t active_level, uint8_t item_id, uint32_t roll,
                 const Genome& genome, uint32_t creation_seed,
                 uint32_t now_epoch, CaptureReport& out)
{
  memset(&out, 0, sizeof out);
  out.slot   = (uint8_t)BOX_SLOT_NONE;
  out.reject = (uint8_t)VR_OK;
  out.attempts_left = (st.attempts < (uint8_t)CAPTURE_MAX_ATTEMPTS)
                          ? (uint8_t)((uint8_t)CAPTURE_MAX_ATTEMPTS - st.attempts)
                          : 0u;

  if (enc.outcome != (uint8_t)ENC_OUT_WILD || enc.species_id == 0u ||
      species_get(enc.species_id) == nullptr) {
    out.outcome = (uint8_t)CAP_NO_ENCOUNTER;
    return false;
  }
  // A creature that has already gone does not roll again, however many times
  // the button is pressed.
  if (st.fled || st.attempts >= (uint8_t)CAPTURE_MAX_ATTEMPTS) {
    out.outcome = (uint8_t)CAP_FLED;
    out.attempts_left = 0u;
    return false;
  }
  // THE BOX IS CHECKED BEFORE THE ROLL, and that is the player-facing decision
  // spec section 23 asks for: "if the Box is full the player must decide
  // whether to release/replace; never silently discard". Rolling first and
  // discovering the Box is full afterwards would spend the attempt AND the
  // capture item on a catch that could never land.
  if (box_count() >= box_capacity()) {
    out.outcome = (uint8_t)CAP_BOX_FULL;
    return false;
  }
  // THE ONE REJECT A CONSTRUCTED BUG CAN CARRY, refused before anything is
  // built. See capture.h: measured over the whole roster x levels 1..30, an
  // unsealed genome is VR_BAD_GENOME on every row and a sealed one is VR_OK on
  // every row.
  if (!genome_valid(genome)) {
    out.outcome = (uint8_t)CAP_BAD_GENOME;
    return false;
  }

  out.chance = cap_chance_permille(enc.species_id, enc.level, active_level, item_id);
  out.roll   = (uint16_t)(roll % CAP_ROLL_SPAN);
  if (out.roll >= out.chance) {
    st.attempts = (uint8_t)(st.attempts + 1u);
    out.attempts_left = (uint8_t)((uint8_t)CAPTURE_MAX_ATTEMPTS - st.attempts);
    if (st.attempts >= (uint8_t)CAPTURE_MAX_ATTEMPTS) {
      st.fled = 1u;
      out.outcome = (uint8_t)CAP_FLED;
    } else {
      out.outcome = (uint8_t)CAP_ESCAPED;
    }
    return false;
  }

  // THE TREE'S ONE CONSTRUCTOR. It mints AND files, so there is no box_add()
  // step on this path (game/box.h, and plan section 5's row is reconciled with
  // the tree there).
  const uint8_t slot = box_new_bug(enc.species_id, enc.level, (uint8_t)ORIGIN_WILD,
                                      genome, creation_seed, now_epoch);
  if (slot == (uint8_t)BOX_SLOT_NONE) {
    // Only reachable if the Box filled between the check above and here, which
    // one thread cannot do - but a full Box is a refusal and never a silent
    // discard, so it is answered as one rather than as a catch.
    out.outcome = (uint8_t)CAP_BOX_FULL;
    return false;
  }

  const BugInstance* p = box_peek(slot);
  const uint8_t vr = (p != nullptr) ? (uint8_t)validate_bug(*p)
                                    : (uint8_t)VR_UNKNOWN_SPECIES;
  if (vr != (uint8_t)VR_OK) {
    // A BUG IN THIS TREE, reported by its named code and NOT undone: see
    // capture.h for why the undo cannot be written correctly (box_release()
    // refuses the active slot, and a first capture into an empty Box IS the
    // active one). The creature stays; the caller shows an internal fault.
    out.outcome = (uint8_t)CAP_INTERNAL;
    out.slot    = slot;
    out.reject  = vr;
    return false;
  }

  out.outcome = (uint8_t)CAP_CAUGHT;
  out.slot    = slot;
  return true;
}
