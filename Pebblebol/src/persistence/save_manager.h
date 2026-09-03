// =============================================================================
//  PEBBLEBOL - persistence/save_manager.h
//  The save POLICY: what gets written, when, in what order, and what a load
//  actually means. Plan sections 1.4, 1.5.3 (commit order) and 1.5.4 (load
//  pipeline). Everything below the seam is persistence/kv_store.h.
//
//  WHY A TRI-STATE LOAD. The v1 loader answered a boolean: store_load() said
//  false for "no save yet", "the CRC failed" and "this save is from a newer
//  firmware" alike, and the caller's only reaction was to build a fresh pet -
//  which silently destroyed a recoverable save (audit risk 3). LoadResult
//  distinguishes all seven outcomes, and the two bad ones NEVER write anything:
//  the user is shown SAVE ERROR and chooses.
//
//  WRITE DISCIPLINE (plan 1.5.3 commit order):
//      construct -> validate -> serialise -> CRC -> kv_put the INACTIVE copy
//      -> read the bytes back and compare -> the new copy is authoritative
//  because it now carries the higher seq. The previous copy is never touched
//  during a write, so a failure at any point above leaves the last good state
//  loadable. Multi-blob transactions (trade) write the "tr" journal first.
//
//  Pure module: no Arduino, no allocation, no direct clock. The two things it
//  cannot compute itself - a monotonic millisecond count for the wear filter
//  and the wall clock for saved_epoch - are injected with save_set_clock(),
//  which keeps the whole policy host-testable.
// =============================================================================
#ifndef PB_SAVE_MANAGER_H
#define PB_SAVE_MANAGER_H

#include <stdint.h>
#include <stddef.h>

#include "save_schema.h"
#include "../core/config.h"       // SAVE_FULL_PERIOD_S, SAVE_LASTSEEN_PERIOD_S

// -----------------------------------------------------------------------------
// Wear policy. Flash endurance, not game rules, so all three are measured in
// milliseconds of uptime and none depends on the wall clock being known. The
// periods come from config.h so there is still exactly one place to retune them.
// -----------------------------------------------------------------------------
#define SAVE_FULL_PERIOD_MS     (SAVE_FULL_PERIOD_S * 1000UL)      // unforced writes
#define SAVE_LASTSEEN_PERIOD_MS (SAVE_LASTSEEN_PERIOD_S * 1000UL)  // key "t"
#define SAVE_MIN_GAP_MS         1000UL     // floor between two writes of one key

// -----------------------------------------------------------------------------
// The seven answers a load can give. Ordered by severity: save_load_all()
// reports the worst thing that happened, so a run that both recovered a pair
// and migrated is reported as LOAD_MIGRATED.
// -----------------------------------------------------------------------------
enum LoadResult : uint8_t {
  LOAD_OK = 0,            // everything read back clean
  LOAD_FRESH,             // no save anywhere: a first boot, build a starter
  LOAD_MIGRATED,          // an older schema was read and converted
  LOAD_RECOVERED_PAIR,    // one copy of some blob was bad; the other one served
  LOAD_RECOVERED_CKPT,    // KV_MAIN had nothing; the nvs2 checkpoint served
  LOAD_CORRUPT,           // both copies of the Box or of the active Pebble bad
  LOAD_FOREIGN_NEWER      // a save from a newer firmware: refused, untouched
};

// Injected clocks. now_ms is a monotonic millisecond counter (millis() on the
// device, the fake clock in the tests); now_epoch is the wall clock, or 0 when
// it is not trustworthy. Either may be null: with no now_ms the wear filter is
// disabled and every write goes through, with no now_epoch saved_epoch stays 0.
typedef uint32_t (*SaveClockFn)(void);
void save_set_clock(SaveClockFn now_ms, SaveClockFn now_epoch);

// Binds the live GameState the deferred writes and the checkpoint read from.
// save_load_all() binds the state it filled, so a caller normally never calls
// this directly.
void save_bind(GameState& gs);

// -----------------------------------------------------------------------------
// LOADING
// -----------------------------------------------------------------------------
// Runs the whole pipeline of plan 1.5.4 - read, checksum, schema validation,
// migration, runtime validation - and fills 'gs'. On LOAD_CORRUPT and
// LOAD_FOREIGN_NEWER nothing is written to flash and 'gs' is left in the
// defaults, so the ERROR screen can offer recovery or a factory reset without
// anything having been destroyed first.
LoadResult save_load_all(GameState& gs);

// -----------------------------------------------------------------------------
// WRITING. Each of these seals the blob (magic, version, seq, CRC), writes the
// inactive copy of the pair, verifies the bytes back and returns false only on
// a real store failure.
// -----------------------------------------------------------------------------
// force=false obeys SAVE_FULL_PERIOD_MS and is therefore safe to call every
// tick; force=true means "state changed" and still honours SAVE_MIN_GAP_MS by
// DEFERRING the write to save_service(), never by dropping it.
bool save_pebble(uint8_t slot, const PebbleInstance& p, bool force);

// True when the LAST save_pebble() call actually reached flash, rather than
// being dropped by the period filter or deferred to save_service(). A caller
// that mirrors a pebble write elsewhere (the P2-C9 compatibility blob) uses it
// to inherit this module's cadence instead of inventing a second one.
bool save_pebble_landed(void);

bool save_box_header(const BoxHeader& b);
bool save_config(ConfigV2& c);            // seals into the caller's struct
bool save_inventory(const Inventory& i);
bool save_cooldowns(const CooldownTable& c);
bool save_trade_journal(const PendingTrade& t);

// The creator records. Single keys "cs0".."cs9", so no pair and no seq: a
// custom species is content, not state, and a bad CRC costs a sprite rather
// than a Pebble (save_schema.h section 6). The slot comes from c.slot.
bool save_custom_species(const CustomSpeciesRec& c);
bool save_load_custom_species(uint8_t slot, CustomSpeciesRec& out);

// Flushes writes that SAVE_MIN_GAP_MS deferred. Safe to call every loop.
void save_service(void);

// Writes the Box header, every occupied slot and the config to KV_CKPT, which
// the Arduino core's wholesale erase of "nvs" cannot reach (decision D6).
bool save_checkpoint_all(void);

// EXPLICIT RECOVERY - the SAVE ERROR screen's "Recuperar" and nothing else.
// Loads the nvs2 checkpoint into 'gs' and commits it over KV_MAIN. Returns
// false when there is no usable checkpoint, and then writes NOTHING, so a user
// who asks to recover and has no copy still has whatever KV_MAIN held.
// save_load_all() reaches the checkpoint on its own only when KV_MAIN is empty;
// this is the path for the other case - KV_MAIN holds bytes, and they are rot.
bool save_restore_checkpoint(GameState& gs);

// Clears both partitions. The caller builds the new starter afterwards; this
// function deliberately does not, so nothing can wipe a unit "helpfully".
bool save_factory_reset(void);

// Mirrors the newest wall clock into KEY_LASTSEEN at the 60 s cadence.
void save_touch_lastseen(uint32_t epoch);

// The newest epoch this module knows, from KEY_LASTSEEN or the last save.
uint32_t save_last_seen(void);

// True when the last save_load_all() found a v1 save it converted; the caller
// uses it to decide whether to show the "save updated" toast.
bool save_was_migrated(void);

#endif // PB_SAVE_MANAGER_H
