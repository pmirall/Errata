// =============================================================================
//  ERRATA - game/dex.h
//
//  THE WIKI. Which bugs this device has met, and which it has held.
//  PURE translation unit: no Arduino.h, no I/O, no file-scope mutable state.
//
// -----------------------------------------------------------------------------
//  TWO BITS PER SPECIES AND NOT ONE.
//
//  SEEN is the weaker claim and it is the one that makes the wiki a reason to
//  walk: a wild bug that got away, a foe on somebody else's team, a creature
//  glimpsed in a link. CAUGHT is the stronger one and implies SEEN - you cannot
//  hold a thing you have not met, and dex_mark_caught() sets both so that
//  invariant cannot be broken by a caller forgetting.
//
//  Sixty species is 120 bits is fifteen bytes, which is why this lives in the
//  front of ConfigV2's reserved block instead of a blob of its own.
//
// -----------------------------------------------------------------------------
//  THE STORAGE IS THE CALLER'S, exactly as the Box's is.
//
//  This module owns the BIT ARITHMETIC and nothing else. dex_bind() takes the
//  fifteen bytes and every function works on them, so a host test drives the
//  same code the firmware does over its own array, and the persistence layer
//  keeps owning when those bytes reach flash.
//
//  SPECIES IDS ARE 1-BASED and index 0 is not a species. Every function here
//  answers false / does nothing for 0 and for anything past the roster rather
//  than indexing past the array: a dex is written from encounter rolls and peer
//  frames, and neither is a trustworthy source of an index.
// =============================================================================
#ifndef ER_GAME_DEX_H
#define ER_GAME_DEX_H

#include <stdint.h>

#include "../core/nt_types.h"        // DEX_BYTES
#include "../data/species_table.h"

// 60 species x 2 bits, rounded up to a byte. Declared against the roster so a
// content pack that grows it fails the static_assert below rather than quietly
// dropping the species past the end.
#define DEX_BITS_PER_SPECIES  2
// DEX_BYTES is core/nt_types.h's: struct Config and struct ConfigV2 both carve
// it out of their reserved blocks and neither may include a game header. This
// is where the number is JUSTIFIED, which is what the assert below is.

static_assert((int)SPECIES_TABLE_COUNT * DEX_BITS_PER_SPECIES <= DEX_BYTES * 8,
              "the roster outgrew the wiki's bit array: DEX_BYTES must cover "
              "SPECIES_TABLE_COUNT * 2 bits, and the two configs that carve it "
              "out of their reserved blocks have room to follow");

// Binds the fifteen bytes. Everything below is a no-op or false until this runs.
void dex_bind(uint8_t* bytes);
void dex_unbind(void);

// Clears every bit. The wipe path, and what a test starts from.
void dex_reset(void);

// SEEN: met, not held. Answers false and does nothing for id 0 or past the
// roster. Returns true when this call CHANGED something, so a caller can decide
// whether the save is worth a write - the difference between a new discovery
// and the four hundredth sighting of the same species.
bool dex_mark_seen(uint8_t species_id);

// CAUGHT: held. Sets SEEN too, because the alternative is a caller that
// forgets and a wiki that shows a creature you own as one you have not met.
bool dex_mark_caught(uint8_t species_id);

bool dex_seen(uint8_t species_id);
bool dex_caught(uint8_t species_id);

// How many of the roster are in each state. For the header line, and for a
// test that wants a number rather than sixty questions.
uint8_t dex_count_seen(void);
uint8_t dex_count_caught(void);

#endif  // ER_GAME_DEX_H
