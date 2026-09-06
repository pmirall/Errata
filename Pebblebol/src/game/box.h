// =============================================================================
//  PEBBLEBOL - game/box.h
//  THE BOX (spec section 9, plan P2-C10). Ten slots, at most one of which is
//  the ACTIVE Pebble - the one game/sim.cpp is bound to and the only one that
//  is simulated. The other nine are stored: they do not decay, they do not age,
//  they only RECOVER toward 100 % at BOX_RECOVER_MPH, integrated from each
//  slot's own last_updated_epoch.
//
//  INVARIANTS this module exists to hold (every one has a test):
//    B1  capacity is BOX_SLOTS (10) and never grows
//    B2  every occupied slot carries a NON-ZERO id, unique inside the Box
//        (spec section 9, "no duplication")
//    B3  exactly one slot is active whenever the Box is non-empty; an empty
//        Box reports BOX_ACTIVE_NONE
//    B4  release is DESTRUCTIVE and therefore explicit: it refuses without the
//        confirmed flag, and it refuses the active slot outright
//    B5  recovery caps at 100 % and never touches xp, level or any identity
//        field - a Pebble left in the Box comes back healthy, not different
//
//  Pure module (plan 1.3 rule 2): stdint plus the save schema. It performs NO
//  I/O of its own - persistence decides when a mutated slot reaches flash - and
//  includes no Arduino header.
// =============================================================================
#ifndef PB_BOX_H
#define PB_BOX_H

#include <stdint.h>
#include <stddef.h>

#include "../core/nt_types.h"              // Genome
#include "../persistence/save_schema.h"    // GameState, PebbleInstance, BoxHeader

#define BOX_SLOT_NONE   0xFFu              // "no slot" for the int8_t returns

// -----------------------------------------------------------------------------
// Binding. The Box does not own memory: the single live GameState does
// (persistence/game_state.cpp). Every call below is a no-op returning a safe
// answer until this has run.
// -----------------------------------------------------------------------------
void  box_bind(GameState& gs);
bool  box_bound(void);

// -----------------------------------------------------------------------------
// Queries
// -----------------------------------------------------------------------------
uint8_t box_capacity(void);                       // BOX_SLOTS
uint8_t box_count(void);                          // occupied slots, 0..BOX_SLOTS
uint8_t box_active(void);                         // slot, or BOX_ACTIVE_NONE
bool    box_occupied(uint8_t slot);
const PebbleInstance* box_peek(uint8_t slot);     // nullptr when empty/out of range
PebbleInstance*       box_slot(uint8_t slot);     // mutable, same rules
bool    box_id_in_use(uint32_t id);

// -----------------------------------------------------------------------------
// Mutation. None of these writes flash; the caller commits.
// -----------------------------------------------------------------------------
// Makes `slot` the active one. Fails for an empty or out-of-range slot, so the
// "exactly one active" invariant cannot be broken by a bad argument (B3).
bool    box_set_active(uint8_t slot);

// Copies `p` into the first free slot. The id is taken as given when it is
// non-zero and unused, and minted otherwise, so a captured or traded Pebble
// keeps its identity while a locally made one always gets a fresh one (B2).
// Returns the slot, or BOX_SLOT_NONE when the Box is full.
uint8_t box_add(const PebbleInstance& p);

// Builds a brand new Pebble of `species_id` at `level` and files it (B2).
// Returns the slot, or BOX_SLOT_NONE. Nothing here is derived and stored: the
// battle numbers are recomputed from the species, the level and the genome.
uint8_t box_new_pebble(uint8_t species_id, uint8_t level, uint8_t origin,
                       const Genome& genome, uint32_t creation_seed,
                       uint32_t now_epoch);

// Exchanges two slots, active-ness included, so the display order of the Box
// screen is the player's to arrange.
bool    box_swap(uint8_t a, uint8_t b);

// DESTRUCTIVE. Refuses unless `confirmed` is true and refuses the active slot
// (B4): releasing what you are holding is never a single keystroke away.
bool    box_release(uint8_t slot, bool confirmed);

// FIRST BOOT ONLY IN PRACTICE, BUT THE RULE IS ABOUT THE PEBBLE. Replace the
// Pebble in `slot` with a freshly minted one of `species_id`, keeping the slot,
// the active flag, the genome and the creation seed. Refuses unless the slot
// holds an ORIGIN_STARTER at level 1 that has earned nothing and been named
// nothing - see the banner in box.cpp for why the lock is achievement and not
// elapsed time. This is what "pick a starter from three" writes.
bool    box_reroll_starter(uint8_t slot, uint8_t species_id, uint32_t now_epoch);

// -----------------------------------------------------------------------------
// Recovery (B5). Stored Pebbles heal, they do not live.
// -----------------------------------------------------------------------------
// Raises every care stat of `slot` toward 100 % by `elapsed_s` at
// BOX_RECOVER_MPH, carrying the exact remainder in care_rem[] exactly as the
// live integrator does, and stamps last_updated_epoch forward by the same
// amount. xp, level, age_s and every identity field are untouched. A no-op for
// the ACTIVE slot: that one belongs to the simulation.
void    box_recover(uint8_t slot, uint32_t elapsed_s);

// Boot: brings every STORED slot up to `now_epoch` from its own
// last_updated_epoch, clamped to [0, ABSENCE_MAX_S]. Returns how many slots
// were advanced. The active slot is left to sim_catch_up_ex().
uint8_t box_recover_all(uint32_t now_epoch);

// The next Pebble id: hash32(device_id, next_id_counter), never 0, never one
// already in the Box (plan 1.5.1). Advances the counter in the BoxHeader.
uint32_t box_mint_id(void);

#endif // PB_BOX_H
