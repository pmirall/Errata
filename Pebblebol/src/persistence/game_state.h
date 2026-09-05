// =============================================================================
//  PEBBLEBOL - persistence/game_state.h
//
//  THE LIVE GAME STATE and the single door to flash.
//
//  This module owns the one GameState of save_schema.h section 8 - ten
//  PebbleInstances, the Box header, ConfigV2, the inventory, the cooldown table
//  and the trade journal, 1,936 B of .bss - and routes every write through
//  save_manager. It contains NO policy: the pair discipline, the wear filter,
//  the checkpoint and the load pipeline all live in save_manager.cpp, and the
//  Preferences handles in hardware/kv_nvs.cpp.
//
//  P2-C10 REPLACED THE P2-C9c ADAPTER. Until this commit the file was
//  save_compat.{h,cpp}: the simulation still ran on the v1 PetSave, so the
//  module carried a PetSave <-> PebbleInstance map and wrote the live pet a
//  second time under the companion key "lgpet". game/sim.cpp now runs on
//  PebbleInstance directly (sim_bind), so the maps, the companion blob and the
//  key are gone. What survives is the state ownership, which was never
//  temporary, and one genuine remnant: the v1 `Config` struct, which the UI and
//  webui still read by pointer. ConfigV2 is the persisted truth and Config the
//  runtime view of it, until P2-C11 moves the screens over.
//
//  Pure module: no Arduino. Everything it needs from the device comes through
//  kv_store.h and the clocks save_set_clock() injects.
// =============================================================================
#ifndef PB_GAME_STATE_H
#define PB_GAME_STATE_H

#include <stdint.h>
#include <stddef.h>

#include "save_manager.h"
#include "save_schema.h"
#include "legacy_v1.h"            // LegacyGainSave: the "gl" ledger, unchanged
#include "../core/nt_types.h"     // the live Config the UI still reads

// The anti-farm ledger still has exactly one slot per live StatId.
#define GS_GAIN_SLOTS   ((uint8_t)ST_COUNT)
static_assert((int)GS_GAIN_SLOTS == LEGACY_STAT_COUNT,
              "the \"gl\" ledger is frozen at LEGACY_STAT_COUNT slots");

// -----------------------------------------------------------------------------
// Load. Runs save_load_all() and derives the runtime Config from ConfigV2. The
// LoadResult is returned unchanged: refusing to write on LOAD_CORRUPT and
// LOAD_FOREIGN_NEWER is save_manager's contract and this module never
// second-guesses it.
//
// On LOAD_FRESH there is no Pebble in the Box yet and gs_have_pebble() is
// false: app.cpp creates the starter through game/box.h. On LOAD_CORRUPT and
// LOAD_FOREIGN_NEWER nothing has been written and the session is read-only.
// -----------------------------------------------------------------------------
LoadResult gs_load(Config& cfg);

// True when the loaded Box holds at least one Pebble.
bool       gs_have_pebble(void);

// The SAVE ERROR screen's "Recuperar". Restores the nvs2 checkpoint over
// KV_MAIN. Returns LOAD_RECOVERED_CKPT on success; LOAD_CORRUPT when there was
// no checkpoint to recover, in which case nothing was written and the session
// stays read-only. Never called except by an explicit user choice.
LoadResult gs_recover(Config& cfg);

// The live GameState this module owns. game/box.cpp binds to it; nothing else
// may keep a pointer into it across a load.
GameState& gs_state(void);

// -----------------------------------------------------------------------------
// Writes. gs_save_active() commits the ACTIVE Pebble through save_pebble() and,
// when the index actually changed, the Box header; force=false keeps
// save_manager's period filter. gs_save_slot() is the Box-mutation path: a
// stored slot is written only when it changes (plan 1.5.3).
// -----------------------------------------------------------------------------
bool gs_save_active(bool force);
bool gs_save_slot(uint8_t slot, bool force);
bool gs_save_box(void);

// READ-ONLY SESSION. Set when the load refused to touch flash (LOAD_CORRUPT,
// LOAD_FOREIGN_NEWER): the pet in RAM is a placeholder and writing it would
// destroy exactly the save the user is about to be asked about. Every write
// below returns false while it is set, and only an explicit factory reset or a
// successful recovery clears it. This is what replaces v1's silent fresh-egg
// overwrite (audit risk 3).
void gs_set_readonly(bool ro);
bool gs_readonly(void);
bool gs_save_cfg(Config& cfg);              // seals the caller's struct, as v1 did
void gs_cfg_defaults(Config& cfg);
void gs_touch_lastseen(uint32_t epoch);     // RTC mirror + the 60 s "t" cadence
bool gs_factory_reset(void);                // both partitions, then the RTC nonce

// -----------------------------------------------------------------------------
// The hourly-gain ledger, NVS key "gl" (LegacyGainSave, layout frozen). It is
// carried forward untouched by the v2 migration and rides the pet's write
// cadence exactly as it did in v1: the provider is asked at the instant a pet
// blob is committed, so the snapshot is never one tick stale.
// -----------------------------------------------------------------------------
typedef bool (*GsGainFn)(uint8_t pts[GS_GAIN_SLOTS], uint32_t& epoch);
void gs_bind_gain(GsGainFn fn);
bool gs_load_gain(uint8_t pts[GS_GAIN_SLOTS], uint32_t& epoch);

// -----------------------------------------------------------------------------
// The XP anti-farm ledger (plan P3-C2). Unlike "gl" it needs no key of its own:
// SaveSchema v2 already reserved Inventory.xp_ledger[4] and Inventory.ledger_epoch
// for it, so it rides the inventory pair. It is NOT bound to a provider the way
// the gain ledger is, because it must be written at a different moment: the
// instant XP is SPENT, and only then. A refill costs no write - xp_ledger_tick()
// puts the points back out of seconds the device watched pass - so a save is owed
// only when the budget went DOWN, which is the half a reboot could otherwise
// undo. (Before P6-C4 the refill came from xp_ledger_restore() and a wall-clock
// gap, which a player refills by typing a date; see game/xp.cpp.)
// gs_load_xp_ledger() returns false when nothing trustworthy was ever stored.
// -----------------------------------------------------------------------------
bool gs_load_xp_ledger(uint8_t pts[XP_LEDGER_SLOTS], uint32_t& epoch);
bool gs_save_xp_ledger(const uint8_t pts[XP_LEDGER_SLOTS], uint32_t epoch);

// The persisted POSIX TZ string, for hardware/gametime.cpp's bootstrap. Empty
// when nothing has been persisted yet; the caller keeps its compiled default.
void gs_boot_tz(char* out, size_t cap);

// -----------------------------------------------------------------------------
// THE CREATOR PIN STATE (P8-C1). ConfigV2 has carried creator_pin (offset 24),
// creator_idle_s (26), pin_lock_until (16) and pin_fail_count (31) since the v2
// schema was written and NOTHING had ever read or written them; these two
// functions are their producer and consumer. They are here rather than in
// networking/webui.cpp for one reason: webui.cpp is a device module no host
// binary compiles, and "the PIN survives a reboot" is a claim about the SAVE
// path, so it belongs where tests/test_game_state.cpp can drive it against the
// real save_manager and the kv_mem fake.
//
// cfg_to_v2() does not touch any of these four, so a SETTINGS write cannot
// clobber a PIN and a PIN write cannot clobber a setting.
//
// gs_creator_store() is a NO-OP WHEN NOTHING CHANGED, and that is a wear
// property rather than an optimisation: the only client that can drive this
// path is an unauthenticated HTTP request, and one flash write per wrong PIN
// would be write amplification a rate limiter cannot stop. See
// networking/creator_gate.h (cg_persist_fails) for the full argument - the
// persisted fail count is only ever 0 or CREATOR_PIN_FAIL_MAX.
//
// `lock_until` is a MIRROR FOR THE DIAG SCREEN, in wall-clock seconds, and no
// code may read it back as a deadline: the authority is a monotonic value in
// RAM, because the lockout must not be movable by a phone that pushes the clock
// (creator_gate.h, "THE CLOCK QUESTION").
// -----------------------------------------------------------------------------
void gs_creator_load(uint16_t& pin, uint8_t& fail_count, uint16_t& idle_s);
bool gs_creator_store(uint16_t pin, uint8_t fail_count, uint32_t lock_until);

// The persisted clock calibration (plan 1.7): what the clock last knew and
// when. A boot whose state is not CAL_UNSET and whose last_known_epoch is sane
// may call itself CAL_ESTIMATED instead of blind, which is the difference
// between charging an honest absence and charging none.
void gs_boot_cal(uint8_t& state, uint32_t& epoch);

// Records a calibration. gt_set_epoch() is the only caller: the state is
// persisted so the NEXT boot knows a real clock once existed.
void gs_note_time_cal(uint8_t state, uint32_t epoch);

// The device identity of spec section 43, generated exactly once from the rng
// service (which app_setup() seeds with the firmware's single esp_random()
// call) and never regenerated while a save survives. 0 until the first load.
uint32_t gs_device_id(void);

#endif // PB_GAME_STATE_H
