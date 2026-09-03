// =============================================================================
//  PEBBLEBOL - game/box.cpp
//  See box.h for the five invariants. This file is the only place that decides
//  which slot is active, which slot is free and what a brand new Pebble is.
//
//  ZERO floating point, no allocation, no I/O, no Arduino.
// =============================================================================
#include "box.h"

#include <string.h>

#include "../core/config.h"                // SEC_PER_HOUR, ABSENCE_MAX_S
#include "../data/balance.h"               // BOX_RECOVER_MPH
#include "../data/species_table.h"         // SpeciesDef, species_get()

static GameState* s_gs = nullptr;

void box_bind(GameState& gs) { s_gs = &gs; }
bool box_bound(void)         { return s_gs != nullptr; }

uint8_t box_capacity(void)   { return (uint8_t)BOX_SLOTS; }

static inline bool slot_ok(uint8_t slot) { return slot < (uint8_t)BOX_SLOTS; }

// The header's slot_mask is the index; the slot's own magic is the truth
// (plan 1.5.4). A slot that holds a Pebble is one that is not empty, and the
// mask is repaired from that rather than trusted over it.
static bool slot_filled(uint8_t slot)
{
  if (!s_gs || !slot_ok(slot)) return false;
  return !pebble_is_empty(s_gs->pebbles[slot]);
}

static void mask_sync(void)
{
  if (!s_gs) return;
  uint16_t m = 0;
  for (uint8_t i = 0; i < (uint8_t)BOX_SLOTS; ++i) {
    if (!pebble_is_empty(s_gs->pebbles[i])) m |= (uint16_t)(1u << i);
  }
  s_gs->box.slot_mask = m;

  // B3: exactly one active whenever the Box is non-empty. An active_slot that
  // points at an empty slot is repaired to the lowest occupied one; an empty
  // Box reports BOX_ACTIVE_NONE.
  const uint8_t a = s_gs->box.active_slot;
  if (m == 0u) {
    s_gs->box.active_slot = (uint8_t)BOX_ACTIVE_NONE;
    return;
  }
  if (!slot_ok(a) || (m & (uint16_t)(1u << a)) == 0u) {
    for (uint8_t i = 0; i < (uint8_t)BOX_SLOTS; ++i) {
      if (m & (uint16_t)(1u << i)) { s_gs->box.active_slot = i; return; }
    }
  }
}

uint8_t box_count(void)
{
  if (!s_gs) return 0;
  uint8_t n = 0;
  for (uint8_t i = 0; i < (uint8_t)BOX_SLOTS; ++i) if (slot_filled(i)) n++;
  return n;
}

uint8_t box_active(void)
{
  if (!s_gs) return (uint8_t)BOX_ACTIVE_NONE;
  mask_sync();
  return s_gs->box.active_slot;
}

bool box_occupied(uint8_t slot) { return slot_filled(slot); }

const PebbleInstance* box_peek(uint8_t slot)
{
  return slot_filled(slot) ? &s_gs->pebbles[slot] : nullptr;
}

PebbleInstance* box_slot(uint8_t slot)
{
  return slot_filled(slot) ? &s_gs->pebbles[slot] : nullptr;
}

bool box_id_in_use(uint32_t id)
{
  if (!s_gs || id == 0u) return false;
  for (uint8_t i = 0; i < (uint8_t)BOX_SLOTS; ++i) {
    if (slot_filled(i) && s_gs->pebbles[i].id == id) return true;
  }
  return false;
}

// -----------------------------------------------------------------------------
//  Identity (plan 1.5.1: id = hash32(device_id, next_id_counter))
// -----------------------------------------------------------------------------
static uint32_t hash32(uint32_t a, uint32_t b)
{
  uint32_t h = a ^ 0x9E3779B9u;
  h ^= b * 0x85EBCA6Bu;
  h ^= h >> 15; h *= 0x2545F491u; h ^= h >> 13;
  h *= 0xC2B2AE35u; h ^= h >> 16;
  return h;
}

uint32_t box_mint_id(void)
{
  if (!s_gs) return 0;
  // Bounded: BOX_SLOTS ids can collide at most BOX_SLOTS times before a free
  // value is reached, and the counter is monotonic, so this always terminates.
  for (uint16_t tries = 0; tries <= (uint16_t)(BOX_SLOTS + 1); ++tries) {
    const uint32_t counter = ++s_gs->box.next_id_counter;
    const uint32_t id = hash32(s_gs->cfg.device_id, counter);
    if (id != 0u && !box_id_in_use(id)) return id;
  }
  // Every hash collided (or device_id is degenerate): fall back on the counter
  // itself, which is monotonic and therefore still unique inside the Box.
  uint32_t id = s_gs->box.next_id_counter;
  while (id == 0u || box_id_in_use(id)) id = ++s_gs->box.next_id_counter;
  return id;
}

// -----------------------------------------------------------------------------
//  Mutation
// -----------------------------------------------------------------------------
bool box_set_active(uint8_t slot)
{
  if (!slot_filled(slot)) return false;
  s_gs->box.active_slot = slot;
  mask_sync();
  return true;
}

static uint8_t first_free(void)
{
  for (uint8_t i = 0; i < (uint8_t)BOX_SLOTS; ++i) {
    if (!slot_filled(i)) return i;
  }
  return (uint8_t)BOX_SLOT_NONE;
}

uint8_t box_add(const PebbleInstance& p)
{
  if (!s_gs) return (uint8_t)BOX_SLOT_NONE;
  if (p.species_id == 0u) return (uint8_t)BOX_SLOT_NONE;   // not a Pebble
  const uint8_t slot = first_free();
  if (slot == (uint8_t)BOX_SLOT_NONE) return slot;         // B1: ten and no more

  const uint32_t wanted = p.id;
  PebbleInstance& dst = s_gs->pebbles[slot];
  dst = p;
  dst.magic      = (uint16_t)PEBBLE_MAGIC;
  dst.layout_ver = (uint8_t)PEBBLE_LAYOUT_VER;
  dst.seq        = 0;
  // Zeroed FIRST, so the collision test below does not find the copy we have
  // just written and re-mint a perfectly good foreign id (B2).
  dst.id = 0u;
  dst.id = (wanted != 0u && !box_id_in_use(wanted)) ? wanted : box_mint_id();
  mask_sync();
  return slot;
}

uint8_t box_new_pebble(uint8_t species_id, uint8_t level, uint8_t origin,
                       const Genome& genome, uint32_t creation_seed,
                       uint32_t now_epoch)
{
  if (!s_gs) return (uint8_t)BOX_SLOT_NONE;
  const SpeciesDef* sp = species_get(species_id);
  if (!sp) return (uint8_t)BOX_SLOT_NONE;
  if (level == 0u) level = 1u;
  if (level > (uint8_t)PB_LEVEL_MAX) level = (uint8_t)PB_LEVEL_MAX;

  const uint8_t slot = first_free();
  if (slot == (uint8_t)BOX_SLOT_NONE) return slot;

  PebbleInstance& p = s_gs->pebbles[slot];
  memset(&p, 0, sizeof p);
  p.magic         = (uint16_t)PEBBLE_MAGIC;
  p.layout_ver    = (uint8_t)PEBBLE_LAYOUT_VER;
  p.species_id    = species_id;
  p.creation_seed = creation_seed;
  p.birth_epoch   = now_epoch;
  p.last_updated_epoch = now_epoch;
  p.level         = level;
  p.genome        = genome;
  p.origin        = (origin < (uint8_t)ORIGIN_COUNT) ? origin : (uint8_t)ORIGIN_WILD;
  p.custom_sprite = (uint8_t)PB_CUSTOM_SPRITE_NONE;
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) {
    p.care[i]     = (int32_t)PB_CARE_MILLI_MAX;   // a new Pebble is a well one
    p.care_rem[i] = 0;
  }
  memcpy(p.moves, sp->moves, sizeof p.moves);
  // hp_max = 10 + 2*base_hp + level (plan 1.5.1, "derived, never stored"); only
  // the CURRENT hp is a Pebble's own, and a new one starts at full.
  p.hp_cur = (uint16_t)(10u + 2u * (uint16_t)sp->base_hp + (uint16_t)level);
  p.id     = box_mint_id();

  mask_sync();
  return slot;
}

bool box_swap(uint8_t a, uint8_t b)
{
  if (!s_gs || !slot_ok(a) || !slot_ok(b)) return false;
  if (a == b) return true;

  PebbleInstance tmp = s_gs->pebbles[a];
  s_gs->pebbles[a]   = s_gs->pebbles[b];
  s_gs->pebbles[b]   = tmp;

  // The active Pebble is an INDEX, not a copy (plan 1.5.3), so a swap has to
  // carry the index with the creature or the player would be holding a
  // different Pebble than the one they moved.
  const uint8_t act = s_gs->box.active_slot;
  if      (act == a) s_gs->box.active_slot = b;
  else if (act == b) s_gs->box.active_slot = a;
  mask_sync();
  return true;
}

bool box_release(uint8_t slot, bool confirmed)
{
  if (!s_gs || !slot_filled(slot)) return false;
  if (!confirmed) return false;                       // B4: never implicit
  if (slot == s_gs->box.active_slot) return false;    // B4: never the active one

  memset(&s_gs->pebbles[slot], 0, sizeof s_gs->pebbles[slot]);
  s_gs->pebbles[slot].magic      = (uint16_t)PEBBLE_MAGIC;
  s_gs->pebbles[slot].layout_ver = (uint8_t)PEBBLE_LAYOUT_VER;
  mask_sync();
  return true;
}

// -----------------------------------------------------------------------------
//  Recovery. The live health-regeneration rule (game/sim.cpp health_step)
//  generalised to every care stat at one stored rate, with the same exact
//  remainder carry, so a stored week and 168 stored hours land on the same
//  number.
// -----------------------------------------------------------------------------
void box_recover(uint8_t slot, uint32_t elapsed_s)
{
  if (!slot_filled(slot)) return;
  if (slot == s_gs->box.active_slot) return;      // the sim owns the active one
  if (elapsed_s == 0u) return;
  if (elapsed_s > (uint32_t)ABSENCE_MAX_S) elapsed_s = (uint32_t)ABSENCE_MAX_S;

  PebbleInstance& p = s_gs->pebbles[slot];
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) {
    int64_t num = (int64_t)BOX_RECOVER_MPH * (int64_t)elapsed_s +
                  (int64_t)p.care_rem[i];
    int32_t q   = (int32_t)(num / (int64_t)SEC_PER_HOUR);
    int32_t rem = (int32_t)(num % (int64_t)SEC_PER_HOUR);

    int64_t v = (int64_t)p.care[i] + (int64_t)q;
    if (v > (int64_t)PB_CARE_MILLI_MAX) {
      v   = (int64_t)PB_CARE_MILLI_MAX;           // B5: caps at 100 %
      rem = 0;                                    // and stops carrying there
    } else if (v < 0) {
      v   = 0;
      rem = 0;
    }
    p.care[i]     = (int32_t)v;
    p.care_rem[i] = (int16_t)rem;
  }
  p.last_updated_epoch += elapsed_s;
  // xp, level, age_s, genome, moves and every identity field are deliberately
  // NOT touched: a Pebble left in the Box comes back healthy, not different.
}

uint8_t box_recover_all(uint32_t now_epoch)
{
  if (!s_gs) return 0;
  mask_sync();
  const uint8_t active = s_gs->box.active_slot;
  uint8_t touched = 0;
  for (uint8_t i = 0; i < (uint8_t)BOX_SLOTS; ++i) {
    if (!slot_filled(i) || i == active) continue;
    const uint32_t last = s_gs->pebbles[i].last_updated_epoch;
    if (last == 0u || now_epoch <= last) continue;   // no clock, or no gap
    uint32_t gap = now_epoch - last;
    if (gap > (uint32_t)ABSENCE_MAX_S) gap = (uint32_t)ABSENCE_MAX_S;
    box_recover(i, gap);
    touched++;
  }
  return touched;
}
