// =============================================================================
//  PEBBLEBOL - game/species_custom.h
//  THE CREATOR SPECIES REGISTRY (P8-C3).
//
//  data/species_table.h has said, since P4-C1, that "P8 resolves cs* records
//  here so no battle or validator code ever branches on custom". This module is
//  that resolution, and it is the reason a Pebble the creator made is an
//  ORDINARY Pebble everywhere else in the tree.
//
//  ==========================================================================
//  THE DEFECT THIS CLOSES, AND IT WAS ALREADY WAITING
//  ==========================================================================
//  Before this file, species_get(200) answered nullptr. Two things followed and
//  both are worse than they look:
//
//    * game/box.cpp's box_new_pebble() - the tree's ONE Pebble constructor -
//      returns BOX_SLOT_NONE when species_get() answers nullptr, so a creator
//      Pebble could not be built at all.
//    * If one were built anyway (box_add() copies verbatim), the NEXT BOOT
//      would quarantine it: persistence/save_manager.cpp's quarantine_scan()
//      runs validate_pebble() over every occupied slot and VR_UNKNOWN_SPECIES
//      is exactly what an unresolvable species id produces. The upload would
//      have worked, the screen would have shown the creature, and it would have
//      come back broken after the first power cycle.
//
//  ==========================================================================
//  WHAT A CUSTOM SPECIES IS, AND WHAT IT DELIBERATELY IS NOT
//  ==========================================================================
//  A CustomSpeciesRec is projected into a SpeciesDef with four fields that are
//  NOT the page's to choose, each for a structural reason rather than a policy:
//
//    stage = 1        It is measured against the STAGE-1 budgets (spec 36,
//                     data/creator_schema.h), so it must BE stage 1 or
//                     validate_pebble()'s VR_BAD_EVO_STAGE rule and the budget
//                     it was checked against would disagree.
//    family = 0       There is no family. That is what answers spec section
//                     35's thirteenth input - "evolution validity" - with a
//                     structural NO rather than a check: evo_rule is
//                     SPECIES_EVO_NONE, there is no next stage to evolve into,
//                     and game/evolution.cpp needs no special case.
//    compat_group = 0 It does not breed, and game/breeding.cpp already refuses
//                     a zero group by name (BRD_COMPAT_GROUP). Breeding derives
//                     the child's species from the parents' FAMILIES, and a
//                     species with no family has no child to derive.
//    spawn_weight = 0 It is never a wild encounter. The creator makes one
//                     Pebble, not a species the world can hand out again.
//
//  AND IT DOES NOT TRAVEL. networking/protocol.cpp refuses any wire record
//  with species > SPECIES_ID_BUILTIN_MAX as VR_WIRE_CUSTOM_UNRESOLVED, because
//  the receiver holds no cs record and would be handed a creature it cannot
//  resolve. So a custom Pebble is local until a later phase sends the record
//  with it - that is a decision, it is tested by name, and it is written down
//  in docs/decisions.md rather than left to be discovered.
//
//  ==========================================================================
//  WHERE THE STATE IS, AND WHY IT IS FILE-SCOPE HERE
//  ==========================================================================
//  Ten projected rows, 24 B each, plus an occupancy mask. It is file-scope
//  because species_get() is a free function every layer calls with no context
//  argument - the same reason game/box.cpp holds one GameState pointer. The
//  RECORDS are not held here: 10 x 192 B would be 1,920 B of .bss for sprite
//  bytes nothing in this phase draws.
//
//  Pure module: stdint, the save schema, the generated tables and
//  game/validate.h. No Arduino, no heap, no float, no clock, no RNG, no I/O -
//  persistence/save_manager.cpp is what reads flash and calls csp_install().
//
//  All identifiers and comments English.
// =============================================================================
#ifndef PB_GAME_SPECIES_CUSTOM_H
#define PB_GAME_SPECIES_CUSTOM_H

#include <stdint.h>

#include "../data/creator_schema.h"        // CREATOR_SPECIES_ID_MIN / _SLOTS
#include "../data/species_table.h"         // SpeciesDef, species_bind_custom()
#include "../persistence/save_schema.h"    // CustomSpeciesRec

#define CSP_SLOT_NONE   0xFFu

// Forgets every installed row AND binds this module as species_table.h's
// resolver. It is safe to call twice and it is what a load path calls first:
// a save that no longer holds a cs record must stop resolving the id, or a
// Pebble would keep resolving to a species the device has forgotten.
void csp_reset(void);

// Projects `rec` into the registry at rec.slot. REFUSES rather than repairs: a
// record that validate_custom_species() does not accept is not installed and
// false is returned, so a rotted or hostile cs blob leaves the slot empty and
// its Pebble is quarantined by name at the next scan.
bool csp_install(const CustomSpeciesRec& rec);

// Removes one slot. Nothing in P8-C3 calls it - a cs slot is only freed by an
// explicit user action, which is P8-C4's screen - and it exists so the registry
// has an inverse rather than only a way in.
void csp_forget(uint8_t slot);

bool    csp_occupied(uint8_t slot);
uint8_t csp_count(void);

// The lowest unoccupied slot, or CSP_SLOT_NONE when all ten are taken. THE
// DEVICE PICKS THE SLOT AND THE PAGE IS TOLD WHICH ONE: a page-chosen slot is
// an attacker-chosen one, and writing over cs4 would silently change the
// species of a Pebble already in the Box.
uint8_t csp_free_slot(void);

// The bound resolver. Answers nullptr for a built-in id, for an id outside the
// custom range and for an unoccupied slot.
const SpeciesDef* csp_get(uint8_t species_id);

// The two directions of the id <-> slot map. Total: an out-of-range argument
// answers CSP_SLOT_NONE / 0 rather than indexing anything.
uint8_t csp_slot_of(uint8_t species_id);
uint8_t csp_species_id(uint8_t slot);

// -----------------------------------------------------------------------------
//  THE PIXELS. 24x24, two frames, 72 B each - byte for byte the shape of every
//  body in data/sprites_pebbles.h, which is why wiring them into the renderer
//  needed no new format and no scaling.
//
//  UNTIL THIS EXISTED THE CREATOR WAS A LIE BY OMISSION. csp_install() set
//  sprite_id = 0 with a comment saying the renderer would learn to read these
//  "at P8-C4/P9-C3"; it never did, so the 144 B were stored, CRC-covered and
//  served back to the phone - and the device drew the player the body of
//  species 1. The owner drew a creature, the page showed it to him, and his
//  Pebble came out wearing somebody else's face.
//
//  Answers nullptr for a species that is not a custom one, for an empty slot,
//  and for a frame past the second - so a caller that forgets to check gets a
//  refusal rather than a pointer into the next slot.
const uint8_t* csp_sprite(uint8_t species_id, uint8_t frame);

static_assert(CREATOR_SPECIES_SLOTS == CUSTOM_SPECIES_SLOTS,
              "the creator schema's slot count and the save schema's disagree: "
              "one of them would index past the other");
static_assert(CREATOR_SPECIES_ID_MAX <= 255u,
              "a custom species id must fit PebbleInstance.species_id");

#endif  // PB_GAME_SPECIES_CUSTOM_H
