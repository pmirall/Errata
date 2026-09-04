// =============================================================================
//  PEBBLEBOL - game/species.cpp
//  Accessors over the generated content tables. See species.h for why this
//  translation unit includes every generated header.
// =============================================================================
#include "species.h"

#include "../data/creator_schema.h"   // included for its guards, not its numbers

// The generated headers cross-check each other at compile time, but two checks
// need a table this file is the first to see both halves of.
//
// THE SPRITE GUARD IS THE ONE plan 1.5.2 LISTS AND species_table.h CANNOT
// CARRY. Asserting it there would pull the 86 KB sprite atlas into every
// translation unit that only wants a base_hp. It belongs in exactly one TU,
// and this is it: the roster is resolved as SPR_BABY_BLOB + sprite_id, so the
// real bound is the SUM, not the field.
#include "../data/sprites.h"

static constexpr bool species_sprites_resolve(void) {
  for (uint8_t i = 0; i < SPECIES_TABLE_COUNT; ++i) {
    const SpeciesDef& sp = SPECIES_TABLE[i];
    if (sp.sprite_id >= (uint8_t)SPRITE_SET_COUNT) return false;
    if ((uint16_t)(SPR_BABY_BLOB + sp.sprite_id) >= (uint16_t)SPRITE_SET_COUNT) return false;
    if (sp.sprite_id != (uint8_t)(sp.id - 1u)) return false;   // the pack's invariant
  }
  return true;
}
static_assert(species_sprites_resolve(),
              "a species sprite_id is outside the atlas, or is not id - 1: the roster "
              "has outgrown SPRITE_SETS and needs the P10 art pass");

uint8_t species_base_of_family(uint8_t family)
{
  if (family < 1u || family > SPECIES_FAMILY_COUNT) return 0u;
  return SPECIES_BASE_OF_FAMILY[family - 1u];
}

int8_t species_type_mod(uint8_t attack_id, uint8_t defender_species_id)
{
  const AttackDef*  a = attack_get(attack_id);
  const SpeciesDef* d = species_get(defender_species_id);
  if (a == nullptr || d == nullptr) return 0;
  return type_mod_of(a->type, d->type);
}

// One walk, two uses: sum the pool, or find the row a roll lands on. Written
// once so the picker and the "is the pool empty" question can never disagree
// about which species are in it.
static uint16_t walk_pool(uint8_t category, uint8_t lo, uint8_t hi,
                          uint32_t target, uint8_t* picked)
{
  if (picked) *picked = 0u;
  if (category >= (uint8_t)NET_CAT_COUNT) return 0u;
  const uint8_t bit = NET_CATEGORY_BIT[category];

  uint32_t acc = 0;
  for (uint8_t i = 0; i < SPECIES_TABLE_COUNT; ++i) {
    const SpeciesDef& sp = SPECIES_TABLE[i];
    if ((sp.category_mask & bit) == 0u) continue;
    if (sp.rarity < lo || sp.rarity > hi) continue;
    acc += sp.spawn_weight;
    if (picked && *picked == 0u && acc > target) *picked = sp.id;
  }
  // The sums fit u16 by construction (SPECIES_SPAWN_SUM is u16 and the whole
  // 60-species roster's largest cell is 4,750), but say so rather than assume.
  return (acc > 0xFFFFu) ? (uint16_t)0xFFFFu : (uint16_t)acc;
}

uint16_t species_spawn_weight_in(uint8_t category, uint8_t rarity_lo, uint8_t rarity_hi)
{
  if (rarity_lo > rarity_hi) return 0u;
  return walk_pool(category, rarity_lo, rarity_hi, 0xFFFFFFFFu, nullptr);
}

uint8_t species_pick_by_weight(uint8_t category, uint8_t rarity_lo, uint8_t rarity_hi,
                               uint16_t roll)
{
  const uint16_t total = species_spawn_weight_in(category, rarity_lo, rarity_hi);
  if (total == 0u) return 0u;
  uint8_t picked = 0u;
  walk_pool(category, rarity_lo, rarity_hi, (uint32_t)(roll % total), &picked);
  return picked;
}

uint8_t item_pick_drop(uint8_t category, uint8_t rarity_lo, uint8_t rarity_hi,
                       uint16_t roll)
{
  if (category >= (uint8_t)NET_CAT_COUNT || rarity_lo > rarity_hi) return 0u;

  // Two stages, exactly as the pack states it: weight-pick inside the network
  // category, then REJECT anything outside the encounter row's rarity band. The
  // rejection is a filter on the pool, not a re-roll, so the pick stays
  // deterministic in `roll` alone.
  uint32_t total = 0;
  for (uint8_t i = 0; i < ITEM_DROP_ROW_COUNT; ++i) {
    const ItemDropRow& d = ITEM_DROP_TABLE[i];
    if (d.category != category) continue;
    const ItemDef* it = item_get(d.item_id);
    if (it == nullptr || it->rarity < rarity_lo || it->rarity > rarity_hi) continue;
    total += d.weight;
  }
  if (total == 0u) return 0u;

  uint32_t target = (uint32_t)(roll % total);
  uint32_t acc = 0;
  for (uint8_t i = 0; i < ITEM_DROP_ROW_COUNT; ++i) {
    const ItemDropRow& d = ITEM_DROP_TABLE[i];
    if (d.category != category) continue;
    const ItemDef* it = item_get(d.item_id);
    if (it == nullptr || it->rarity < rarity_lo || it->rarity > rarity_hi) continue;
    acc += d.weight;
    if (acc > target) return d.item_id;
  }
  return 0u;
}

uint16_t item_xp_value(uint8_t item_id)
{
  const ItemDef* it = item_get(item_id);
  if (it == nullptr || it->klass != (uint8_t)ITEM_KLASS_XP_CANDY) return 0u;
  return (uint16_t)((uint16_t)it->value * (uint16_t)ITEM_XP_CANDY_SCALE);
}
