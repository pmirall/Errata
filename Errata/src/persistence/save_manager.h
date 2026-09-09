// =============================================================================
//  ERRATA - persistence/save_manager.h
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
#ifndef ER_SAVE_MANAGER_H
#define ER_SAVE_MANAGER_H

#include <stdint.h>
#include <stddef.h>

#include "save_schema.h"
#include "../game/validate.h"   // VReject: the load path is a section 15 consumer
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
  LOAD_CORRUPT,           // both copies of the Box or of the active Bug bad
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
//
// "RUNTIME VALIDATION" IS THE QUARANTINE BELOW, AND UNTIL P4-C5 THIS SENTENCE
// WAS WIDER THAN THE TREE: `grep -c valid save_manager.cpp` inside the old
// save_load_all() was 0, and blob_ok() - magic, CRC and a version byte - was the
// whole of it. Spec section 15 names the load path as a consumer of the shared
// validator, so the stage is real now rather than advertised.
LoadResult save_load_all(GameState& gs);

// -----------------------------------------------------------------------------
// QUARANTINE (spec section 15, P4-C5). After every load, every occupied slot is
// run through game/validate.h's validate_bug(). A slot that fails is FLAGGED
// AND KEPT, never repaired and never dropped:
//
//   * repairing is the failure spec section 15's first sentence is written
//     against, and validate_bug() takes a const Bug so this module could
//     not repair even if it wanted to;
//   * refusing the Box would brick a device on a content-pack change, since
//     VR_UNKNOWN_SPECIES is exactly what an older save legitimately produces.
//
// A quarantined Bug may be shown to its owner. It may NOT enter a battle or
// a trade - that is P7's obligation and this header is where it is written
// down. The mask is in RAM only: nothing about the quarantine is persisted, so
// a content pack that brings a species back clears it on the next boot by
// itself.
uint16_t save_quarantine_mask(void);          // bit s set = slot s failed
VReject  save_quarantine_reason(uint8_t slot); // VR_OK for a slot that passed

// -----------------------------------------------------------------------------
// WRITING. Each of these seals the blob (magic, version, seq, CRC), writes the
// inactive copy of the pair, verifies the bytes back and returns false only on
// a real store failure.
// -----------------------------------------------------------------------------
// force=false obeys SAVE_FULL_PERIOD_MS and is therefore safe to call every
// tick; force=true means "state changed" and still honours SAVE_MIN_GAP_MS by
// DEFERRING the write to save_service(), never by dropping it.
bool save_bug(uint8_t slot, const BugInstance& p, bool force);

// True when the LAST save_bug() call actually reached flash, rather than
// being dropped by the period filter or deferred to save_service(). A caller
// that mirrors a bug write elsewhere (the P2-C9 compatibility blob) uses it
// to inherit this module's cadence instead of inventing a second one.
bool save_bug_landed(void);

// A SLOT WRITE THAT IS NEVER FILTERED AND NEVER DEFERRED, for the one caller
// that cannot survive either: game/trade.cpp's apply step.
//
// WHY IT HAS TO EXIST. The trade writes TWO slots microseconds apart (B1 clears
// the outgoing one, B2 files the incoming one) and box_add() fills the lowest
// free slot, which is USUALLY THE SLOT B1 JUST RELEASED - so the sequence writes
// one key twice inside SAVE_MIN_GAP_MS. save_bug(force=true) defers the
// second write and RETURNS TRUE, which is the right answer for the care loop
// (save_service() flushes it a second later) and a lie for a caller whose whole
// contract is "the bytes landed": the trade would then write the Box header and
// CLEAR ITS JOURNAL over a slot that is still empty on flash, and the boot
// resolver - the last line of defence - would have nothing left to repair.
//
// force=true is deliberately NOT widened to mean this. persistence/
// game_state.cpp calls it on every care action and RELIES on the deferral for
// flash wear; changing what force means would take that away from it.
//
// The wear argument does not apply here: this runs a handful of times per
// TRADE, not per tick. Returns whether the bytes reached flash and read back
// equal - there is no third answer, which is the whole point.
bool save_bug_now(uint8_t slot, const BugInstance& p);

bool save_box_header(const BoxHeader& b);
bool save_config(ConfigV2& c);            // seals into the caller's struct
bool save_inventory(const Inventory& i);
bool save_cooldowns(const CooldownTable& c);
bool save_trade_journal(const PendingTrade& t);

// The creator records. Single keys "cs0".."cs9", so no pair and no seq: a
// custom species is content, not state, and a bad CRC costs a sprite rather
// than a Bug (save_schema.h section 6). The slot comes from c.slot.
bool save_custom_species(const CustomSpeciesRec& c);
// 'found_ver', when given, receives the schema version the record was STORED
// with, which may be older than this firmware's (P10-C5). The record itself is
// handed back exactly as it was read; re-sealing it is the caller's job, and
// save_load_all() does it - see custom_species_install_all().
bool save_load_custom_species(uint8_t slot, CustomSpeciesRec& out,
                              uint8_t* found_ver = nullptr);

// Flushes writes that SAVE_MIN_GAP_MS deferred. Safe to call every loop.
void save_service(void);

// Writes the Box header, every occupied slot and the config to KV_CKPT, which
// the Arduino core's wholesale erase of "nvs" cannot reach (decision D6).
//
// IT DELIBERATELY DOES NOT CHECKPOINT THE TRADE JOURNAL, and that is a decision
// rather than an omission (P7-C4). The checkpoint is a snapshot of a CONSISTENT
// Box, and game/trade.h's write order takes one immediately AFTER the journal
// has been cleared. A mid-trade checkpoint would be a second, stale source of
// truth for the same transaction, and the boot resolver would then have to
// choose between two records that disagree. The "tr" key lives in KV_MAIN only.
bool save_checkpoint_all(void);

// The checkpoint CADENCE. Safe to call every tick with the wall clock: it
// writes at most once per SAVE_CKPT_PERIOD_S, and force=true is the event path
// (level-up, evolution, capture, trade - the things that change what a Bug
// is). Returns true when a checkpoint was actually written. An epoch below
// NT_EPOCH_SANE_MIN is not a date and never triggers the daily write; a forced
// one still goes through, because the event happened whatever the clock says.
// The first call after a boot adopts the stored checkpoint's own saved_epoch,
// so a unit that is power-cycled ten times a day still writes one checkpoint.
bool save_checkpoint_service(uint32_t now_epoch, bool force);

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

#endif // ER_SAVE_MANAGER_H
