// =============================================================================
//  PEBBLEBOL - game/species_custom.cpp
//  The creator species registry. See species_custom.h for what a custom species
//  is, the four fields the page does not choose, and the quarantine defect this
//  closes.
//
//  ZERO floating point, no allocation, no I/O, no Arduino, no clock, no RNG.
// =============================================================================
#include "species_custom.h"

#include <string.h>

#include "validate.h"       // validate_custom_species - the ONE validator

// The projected rows. 10 x 24 B + 2 B of mask; the RECORDS stay on flash.
static SpeciesDef s_rows[CREATOR_SPECIES_SLOTS];
static uint16_t   s_mask = 0u;

static bool slot_ok(uint8_t slot)
{
  return slot < (uint8_t)CREATOR_SPECIES_SLOTS;
}

void csp_reset(void)
{
  s_mask = 0u;
  memset(s_rows, 0, sizeof s_rows);
  // Binding here rather than from app_setup() means there is exactly one place
  // the resolver can come from, and a host binary that links this object gets
  // the same wiring the firmware does the moment it resets the registry.
  species_bind_custom(&csp_get);
}

bool csp_install(const CustomSpeciesRec& rec)
{
  if (!slot_ok(rec.slot)) return false;

  // REFUSES, NEVER REPAIRS. game/validate.h's whole argument applies here: a
  // validator that mends turns an illegal record into a legal one, which is the
  // injection spec section 15's first sentence forbids. A refused record leaves
  // the slot EMPTY, so species_get() keeps answering nullptr and the Pebble
  // that pointed at it is quarantined with VR_UNKNOWN_SPECIES by name.
  if (validate_custom_species(rec) != VR_OK) return false;

  SpeciesDef& d = s_rows[rec.slot];
  memset(&d, 0, sizeof d);
  d.id            = (uint8_t)(CREATOR_SPECIES_ID_MIN + rec.slot);
  d.family        = 0u;                       // no family: see the header
  d.stage         = 1u;                       // the budget it was measured against
  d.type          = rec.type;
  d.base_hp       = rec.base[0];
  d.base_atk      = rec.base[1];
  d.base_def      = rec.base[2];
  d.base_spd      = rec.base[3];
  memcpy(d.moves, rec.moves, sizeof d.moves);
  d.evo_rule      = (uint8_t)SPECIES_EVO_NONE;
  d.rarity        = (uint8_t)SPECIES_RARITY_COMMON;
  d.spawn_weight  = 0u;                       // never a wild encounter
  d.compat_group  = 0u;                       // never breeds
  d.category_mask = 0u;                       // no network category spawns it
  // sprite_id 0 is the atlas body a custom Pebble is DRAWN as until the
  // renderer learns to read CustomSpeciesRec.sprite. The record's own 144 B of
  // pixels are stored, CRC-covered and served back to the page; what does not
  // exist yet is a pet renderer that draws them, and that is P8-C4/P9-C3's
  // work, recorded in the plan rather than implied by a zero here.
  d.sprite_id     = 0u;
  // The name lives on the PebbleInstance's nickname, not in a StrId: there is
  // no string table slot to point at for a name the user typed.
  d.name_idx      = (uint16_t)STR_EMPTY;
  d.flavor_idx    = (uint16_t)STR_EMPTY;

  s_mask = (uint16_t)(s_mask | (uint16_t)(1u << rec.slot));
  return true;
}

void csp_forget(uint8_t slot)
{
  if (!slot_ok(slot)) return;
  s_mask = (uint16_t)(s_mask & (uint16_t)~(1u << slot));
  memset(&s_rows[slot], 0, sizeof s_rows[slot]);
}

bool csp_occupied(uint8_t slot)
{
  if (!slot_ok(slot)) return false;
  return (s_mask & (uint16_t)(1u << slot)) != 0u;
}

uint8_t csp_count(void)
{
  uint8_t n = 0u;
  for (uint8_t i = 0u; i < (uint8_t)CREATOR_SPECIES_SLOTS; ++i)
    if (csp_occupied(i)) n++;
  return n;
}

uint8_t csp_free_slot(void)
{
  for (uint8_t i = 0u; i < (uint8_t)CREATOR_SPECIES_SLOTS; ++i)
    if (!csp_occupied(i)) return i;
  return (uint8_t)CSP_SLOT_NONE;
}

uint8_t csp_slot_of(uint8_t species_id)
{
  if (species_id < (uint8_t)CREATOR_SPECIES_ID_MIN) return (uint8_t)CSP_SLOT_NONE;
  const uint8_t slot = (uint8_t)(species_id - (uint8_t)CREATOR_SPECIES_ID_MIN);
  return slot_ok(slot) ? slot : (uint8_t)CSP_SLOT_NONE;
}

uint8_t csp_species_id(uint8_t slot)
{
  if (!slot_ok(slot)) return 0u;
  return (uint8_t)(CREATOR_SPECIES_ID_MIN + slot);
}

const SpeciesDef* csp_get(uint8_t species_id)
{
  const uint8_t slot = csp_slot_of(species_id);
  if (slot == (uint8_t)CSP_SLOT_NONE) return nullptr;
  if (!csp_occupied(slot)) return nullptr;
  return &s_rows[slot];
}
