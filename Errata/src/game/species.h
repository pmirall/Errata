// =============================================================================
//  ERRATA - game/species.h
//  ACCESSORS OVER THE GENERATED CONTENT TABLES (plan P4-C1).
//
//  The tables themselves are `inline constexpr` in data/*_table.h, and the
//  trivial lookups (species_get, attack_get, item_get, evolution_rule_at) stay
//  inline in those headers so a caller that only wants a base_hp pays for
//  nothing. What lives HERE is the arithmetic that would otherwise be
//  re-derived at every call site: the type modifier of one attack against one
//  defender, the encounter and drop pickers' cumulative walks, and the
//  roster-wide questions ("how many species can spawn under this category").
//
//  PURE MODULE. stdint and the data headers. No Arduino, no clock, no I/O, no
//  heap: tests/test_content.cpp compiles it directly.
//
//  species.cpp IS ALSO THE TRANSLATION UNIT THAT INCLUDES EVERY GENERATED
//  HEADER. That is not incidental. A static_assert in a header nobody includes
//  is a comment, and until this file existed nothing in the firmware pulled in
//  attacks_table.h, items_table.h, encounter_table.h or creator_schema.h - so
//  none of their compile-time guards would ever have been instantiated.
// =============================================================================
#ifndef ER_GAME_SPECIES_H
#define ER_GAME_SPECIES_H

#include <stdint.h>

#include "../data/species_table.h"
#include "../data/attacks_table.h"
#include "../data/items_table.h"
#include "../data/encounter_table.h"
#include "../data/evolution_table.h"

// The base-stage species of `family` (1-based), or 0 for a family the roster
// does not carry. Migration and breeding both need it; neither re-derives it.
uint8_t species_base_of_family(uint8_t family);

// The type modifier of `attack_id` used against `defender_species_id`:
// -1 disadvantage, 0 neutral, +1 advantage. Returns 0 - never an advantage -
// for an unknown attack, an unknown defender or a NEUTRAL attack, so a bad id
// can only ever cost the attacker, and never hand it a bonus it did not earn.
int8_t species_type_mod(uint8_t attack_id, uint8_t defender_species_id);

// Total spawn weight of the species that can appear under `category` (a
// NetCategory ORDINAL) inside the rarity band lo..hi inclusive. 0 when the
// pool is empty, which is what the caller must treat as "roll again".
uint16_t species_spawn_weight_in(uint8_t category, uint8_t rarity_lo, uint8_t rarity_hi);

// Picks a species out of that pool. `roll` is any value; it is reduced modulo
// the pool weight, so a caller may hand it a raw RNG draw. Returns 0 when the
// pool is empty. Deterministic: the same roll over the same table always gives
// the same species, walking the roster in id order.
uint8_t species_pick_by_weight(uint8_t category, uint8_t rarity_lo, uint8_t rarity_hi,
                               uint16_t roll);

// Picks an item for an ITEM encounter under `category`, rejecting any item
// whose rarity falls outside lo..hi (the encounter row's band). Returns 0 when
// nothing in the category's drop list is inside the band.
uint8_t item_pick_drop(uint8_t category, uint8_t rarity_lo, uint8_t rarity_hi,
                       uint16_t roll);

// How much XP an item grants, 0 for anything that is not an XP candy.
uint16_t item_xp_value(uint8_t item_id);

#endif  // ER_GAME_SPECIES_H
