// =============================================================================
//  PEBBLEBOL - persistence/save_compat.h
//
//  *** TEMPORARY MODULE. P2-C10 DELETES IT. ***
//
//  SaveSchema v2 (save_schema.h / save_manager.h) is the persisted truth from
//  P2-C9 onwards, but the live simulation still runs on the v1 PetSave and
//  Config structs: moving game/sim.cpp onto PebbleInstance is P2-C10, one step
//  later, deliberately so that the storage rewrite and the game-state rewrite
//  are not one un-reviewable commit.
//
//  This file is the seam between those two facts. It owns the single live
//  GameState, maps it onto the PetSave/Config the rest of the firmware still
//  speaks, and routes every write through save_manager. It contains NO policy:
//  the pair discipline, the wear filter, the checkpoint and the load pipeline
//  all live in save_manager.cpp, and the Preferences handles in
//  hardware/kv_nvs.cpp.
//
//  WHAT P2-C10 REMOVES. sim_bind(PebbleInstance&) replaces sim_init(PetSave&);
//  at that point the pebble <-> pet maps below, the KEY_COMPAT_PET companion
//  blob and this whole header go, and the callers talk to save_manager
//  directly. Nothing else in the tree may grow a dependency on it.
//
//  THE COMPANION BLOB. PebbleInstance has no home for the parts of the v1 pet
//  that the v2 design moves elsewhere (care quality, the daily wish, poop,
//  snacks, the event bits). Rather than silently drop them for one commit, the
//  live PetSave is also written verbatim under KEY_COMPAT_PET, and the load
//  prefers it. When it is absent - a migration, or a checkpoint recovery - the
//  pet is rebuilt from PebbleInstance slot 0 with documented defaults for the
//  fields v2 does not carry.
//
//  Pure module: no Arduino. Everything it needs from the device comes through
//  kv_store.h and the clocks save_set_clock() injects.
// =============================================================================
#ifndef PB_SAVE_COMPAT_H
#define PB_SAVE_COMPAT_H

#include <stdint.h>
#include <stddef.h>

#include "save_manager.h"
#include "save_schema.h"
#include "legacy_v1.h"            // LegacyGainSave: the "gl" ledger, unchanged
#include "../core/nt_types.h"     // the live PetSave / Config

// The anti-farm ledger still has exactly one slot per live StatId.
#define COMPAT_GAIN_SLOTS   ((uint8_t)ST_COUNT)
static_assert((int)COMPAT_GAIN_SLOTS == LEGACY_STAT_COUNT,
              "the \"gl\" ledger is frozen at LEGACY_STAT_COUNT slots");

// -----------------------------------------------------------------------------
// Load. Runs save_load_all() and maps the result onto the live structs. The
// LoadResult is returned unchanged: refusing to write on LOAD_CORRUPT and
// LOAD_FOREIGN_NEWER is save_manager's contract and this module never
// second-guesses it.
//
// 'pet' is filled only when compat_have_pet() is true afterwards; on
// LOAD_FRESH / LOAD_CORRUPT / LOAD_FOREIGN_NEWER the caller decides what to do
// and nothing has been written.
// -----------------------------------------------------------------------------
LoadResult compat_load(PetSave& pet, Config& cfg);
bool       compat_have_pet(void);

// The SAVE ERROR screen's "Recuperar". Restores the nvs2 checkpoint over
// KV_MAIN and re-derives the live pet from it. Returns LOAD_RECOVERED_CKPT on
// success; LOAD_CORRUPT when there was no checkpoint to recover, in which case
// nothing was written and the session stays read-only. Never called except by
// an explicit user choice.
LoadResult compat_recover(PetSave& pet, Config& cfg);

// The live GameState this module owns, for the screens that already speak v2.
GameState& compat_state(void);

// -----------------------------------------------------------------------------
// Writes. compat_save_pet() maps the pet into slot 0, writes the companion
// blob and calls save_pebble()/save_box_header(); force=false keeps
// save_manager's period filter.
// -----------------------------------------------------------------------------
bool compat_save_pet(const PetSave& pet, bool force);

// READ-ONLY SESSION. Set when the load refused to touch flash (LOAD_CORRUPT,
// LOAD_FOREIGN_NEWER): the pet in RAM is a placeholder and writing it would
// destroy exactly the save the user is about to be asked about. Every write
// below returns false while it is set, and only an explicit factory reset or a
// successful recovery clears it. This is what replaces v1's silent fresh-egg
// overwrite (audit risk 3).
void compat_set_readonly(bool ro);
bool compat_readonly(void);
bool compat_save_cfg(Config& cfg);          // seals the caller's struct, as v1 did
void compat_cfg_defaults(Config& cfg);
void compat_touch_lastseen(uint32_t epoch); // RTC mirror + the 60 s "t" cadence
bool compat_factory_reset(void);            // both partitions, then the RTC nonce

// -----------------------------------------------------------------------------
// The hourly-gain ledger, NVS key "gl" (LegacyGainSave, layout frozen). It is
// carried forward untouched by the v2 migration and rides the pet's write
// cadence exactly as it did in v1: the provider is asked at the instant a pet
// blob is committed, so the snapshot is never one tick stale.
// -----------------------------------------------------------------------------
typedef bool (*CompatGainFn)(uint8_t pts[COMPAT_GAIN_SLOTS], uint32_t& epoch);
void compat_bind_gain(CompatGainFn fn);
bool compat_load_gain(uint8_t pts[COMPAT_GAIN_SLOTS], uint32_t& epoch);

// The persisted POSIX TZ string, for hardware/gametime.cpp's bootstrap. Empty
// when nothing has been persisted yet; the caller keeps its compiled default.
void compat_boot_tz(char* out, size_t cap);

// The persisted clock calibration (plan 1.7): what the clock last knew and
// when. A boot whose state is not CAL_UNSET and whose last_known_epoch is sane
// may call itself CAL_ESTIMATED instead of blind, which is the difference
// between charging an honest absence and charging none.
void compat_boot_cal(uint8_t& state, uint32_t& epoch);

// Records a calibration. gt_set_epoch() is the only caller: the state is
// persisted so the NEXT boot knows a real clock once existed.
void compat_note_time_cal(uint8_t state, uint32_t epoch);

// The device identity of spec section 43, generated exactly once from the rng
// service (which app_setup() seeds with the firmware's single esp_random()
// call) and never regenerated while a save survives. 0 until the first load.
uint32_t compat_device_id(void);

#endif // PB_SAVE_COMPAT_H
