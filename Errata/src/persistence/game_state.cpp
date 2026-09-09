// =============================================================================
//  ERRATA - persistence/game_state.cpp
//  The live GameState, the Config view of ConfigV2, the "gl" anti-farm ledger
//  and the device identity. See game_state.h for what P2-C10 removed.
//
//  No Arduino include of its own: every clock comes from save_set_clock() and
//  the RTC mirror through hardware/boot.h. It is NOT one of the host-compiled
//  pure modules (plan 1.3 rule 2) and nothing may start treating it as one.
// =============================================================================
#include "game_state.h"

#include "../game/dex.h"

#include <string.h>

#include "kv_store.h"
#include "../hardware/boot.h"      // the free RTC mirror of the last-seen epoch
#include "../core/config.h"
#include "../core/crc16.h"
#include "../core/rng.h"           // the device id of spec section 43
#include "../data/balance.h"       // GAIN_CAP_*: the ledger the caps are asserted against

// The single live game state. 1,936 B of .bss, exactly the sum of the persisted
// blobs (save_schema.h section 8).
static GameState s_gs;

static bool         s_have_bug = false;
static bool         s_readonly   = false;
static GsGainFn s_gain_fn    = nullptr;
static uint8_t      s_gain_last[GS_GAIN_SLOTS];
static uint32_t     s_gain_epoch = 0;
static bool         s_gain_known = false;
static bool         s_gain_sane  = false;

// Signature of the Box header as last written, so a pet save does not drag a
// pair write of the Box along with it every time (the Box has no wear filter of
// its own: nothing about it changes at 1 Hz).
static uint32_t s_box_sig      = 0;
static bool     s_box_sig_seen = false;

void gs_set_readonly(bool ro) { s_readonly = ro; }
bool gs_readonly(void)        { return s_readonly; }

GameState& gs_state(void)  { return s_gs; }
bool       gs_have_bug(void) { return s_have_bug; }

static uint32_t box_signature(const BoxHeader& b) {
  return ((uint32_t)b.slot_mask << 16) ^ ((uint32_t)b.active_slot << 8) ^
         b.next_id_counter ^ ((uint32_t)b.captures << 4) ^ (uint32_t)b.battles ^
         ((uint32_t)b.flags << 24);
}

// The v1 Config the UI still reads is sealed exactly as v1 sealed it: app.cpp
// watches its CRC to notice a settings change.
static void cfg_seal(Config& c) {
  c.magic       = NT_CFG_MAGIC;
  c.version     = NT_CFG_VERSION;
  c.reserved[0] = 0;
  c.reserved[1] = 0;
  c.reserved[2] = 0;
  c.wifi_ssid[sizeof(c.wifi_ssid) - 1] = '\0';
  c.wifi_pass[sizeof(c.wifi_pass) - 1] = '\0';
  c.pet_name[sizeof(c.pet_name) - 1]   = '\0';
  c.tz[sizeof(c.tz) - 1]               = '\0';
  c.crc16 = crc16_ccitt(&c, CONFIG_CRC_BYTES);
}

// -----------------------------------------------------------------------------
// The config, both ways. Wi-Fi credentials are deliberately NOT persisted any
// more: the product never associates to a station (spec section 68 r5) and
// P5-C1 removes the code path entirely, so adding them to ConfigV2 would be
// designing in a dead field. They keep their compiled-in defaults each session.
// -----------------------------------------------------------------------------
static void cfg_from_v2(const ConfigV2& v2, Config& out) {
  gs_cfg_defaults(out);
  out.brightness  = v2.brightness ? v2.brightness : (uint8_t)OLED_CONTRAST_DEFAULT;
  out.saved_epoch = v2.last_known_epoch;
  memcpy(out.tz, v2.tz, sizeof v2.tz);
  out.tz[sizeof v2.tz - 1] = '\0';

  const uint8_t sbar = (uint8_t)((v2.flags & CFGV2_F_SBAR_MASK) >> CFGV2_F_SBAR_SH);
  out.statusbar_mode = (sbar < (uint8_t)SBAR_COUNT) ? sbar : (uint8_t)SBAR_ICONS;

  uint8_t f = 0;
  if (v2.flags & CFGV2_F_MUTE) f |= CF_MUTE;
  if (v2.flags & CFGV2_F_WEB)  f |= CF_WEB_ENABLED;
  if (v2.flags & CFGV2_F_BLE)  f |= CF_RESERVED_BLE;
  // The first-boot step (app/onboarding.h). Two bits, and BOTH ZERO MEANS
  // FINISHED - so a blob written by any earlier firmware decodes as "already
  // set up" and no played device is ever handed a setup wizard.
  f |= (uint8_t)((((uint8_t)((v2.flags & CFGV2_F_SETUP_MASK) >> CFGV2_F_SETUP_SH))
                  << CF_SETUP_SH) & CF_SETUP_MASK);
  out.flags = f;

  // The pet's display name. v2 keeps it on the Bug; the v1 UI reads it from
  // Config, so the two are held in step until P2-C11 moves the UI over. The
  // config copy wins when it has one: it is the field SETTINGS writes, so a
  // rename must not be undone by the nickname the last save mapped.
  //
  // SLOT 0, NOT THE ACTIVE SLOT, AND P5 IS WHERE THAT STARTS TO MATTER (found
  // by the P4-C6 sweep, left as it is on purpose). Today the only writer of any
  // nickname in the whole tree is migration.cpp mapping a TYPED v1 pet_name,
  // and that Bug is in slot 0 AND is the active one, so the two readings
  // cannot differ and changing the line now would change no behaviour and be
  // untestable. Once P5-C4 capture puts other Bugs in the Box and the player
  // moves the active slot, HOME will call the new Bug by slot 0's name -
  // Config.pet_name wins over everything downstream. The fix belongs in the
  // chunk that can write the failing test: read box_active() here.
  const char* name = (v2.device_name[0] != '\0') ? v2.device_name
                                                 : s_gs.bugs[0].nickname;
  size_t i = 0;
  while (name[i] != '\0' && i + 1 < sizeof out.pet_name) { out.pet_name[i] = name[i]; ++i; }
  out.pet_name[i] = '\0';

  cfg_seal(out);
}

static void cfg_to_v2(const Config& c, ConfigV2& v2) {
  v2.brightness = c.brightness;
  size_t i = 0;
  while (c.tz[i] != '\0' && i + 1 < (size_t)CFGV2_TZ_CAP) { v2.tz[i] = c.tz[i]; ++i; }
  v2.tz[i] = '\0';

  uint16_t f = (uint16_t)(v2.flags & (uint16_t)~(CFGV2_F_MUTE | CFGV2_F_WEB |
                                                 CFGV2_F_BLE | CFGV2_F_SBAR_MASK |
                                                 CFGV2_F_SETUP_MASK));
  if (c.flags & CF_MUTE)        f |= CFGV2_F_MUTE;
  if (c.flags & CF_WEB_ENABLED) f |= CFGV2_F_WEB;
  if (c.flags & CF_RESERVED_BLE) f |= CFGV2_F_BLE;
  f |= (uint16_t)((((uint16_t)((c.flags & CF_SETUP_MASK) >> CF_SETUP_SH))
                   << CFGV2_F_SETUP_SH) & CFGV2_F_SETUP_MASK);
  f |= (uint16_t)(((uint16_t)c.statusbar_mode << CFGV2_F_SBAR_SH) & CFGV2_F_SBAR_MASK);
  v2.flags = f;

  size_t n = 0;
  while (c.pet_name[n] != '\0' && n + 1 < (size_t)CFGV2_NAME_CAP) {
    v2.device_name[n] = c.pet_name[n];
    ++n;
  }
  v2.device_name[n] = '\0';
  // ConfigV2.dex is NOT written from the runtime Config and must not be: it is
  // the live array game/dex.cpp is bound to, so copying a v1-shaped struct over
  // it would undo every discovery made since the last load. See nt_types.h.
}

void gs_cfg_defaults(Config& c) {
  memset(&c, 0, sizeof c);
  size_t i = 0;
  const char* tz = CFG_TZ_STRING;
  while (tz[i] != '\0' && i + 1 < sizeof c.tz) { c.tz[i] = tz[i]; ++i; }
  c.tz[i] = '\0';

  // CF_WEB_ENABLED is DELIBERATELY CLEAR on a fresh device (plan section 2 row
  // G4): the radio is off by default and the creator server is opt-in.
  // CF_RESERVED_BLE (0x04) is DELIBERATELY CLEAR on a fresh device now that BLE
  // is deleted (P8-C0). The bit is still carried through both directions of the
  // v1/v2 conversion above, because a save written before the deletion has it
  // set and round-tripping a stored byte is not the same as minting one.
  uint8_t f = 0;
  c.flags          = f;
  c.brightness     = (uint8_t)OLED_CONTRAST_DEFAULT;
  c.statusbar_mode = (uint8_t)SBAR_ICONS;
  cfg_seal(c);
}

// -----------------------------------------------------------------------------
// Load
// -----------------------------------------------------------------------------
// True when any slot holds a Bug. The header's slot_mask is only an index;
// the slots themselves are the truth (plan 1.5.4).
static bool any_bug(void) {
  for (uint8_t i = 0; i < (uint8_t)BOX_SLOTS; ++i) {
    if (!bug_is_empty(s_gs.bugs[i])) return true;
  }
  return false;
}

LoadResult gs_load(Config& cfg) {
  const LoadResult r = save_load_all(s_gs);

  s_have_bug = false;
  s_box_sig_seen = false;
  s_readonly = (r == LOAD_CORRUPT || r == LOAD_FOREIGN_NEWER);

  if (s_readonly) {
    // Nothing has been written and nothing may be: the user chooses.
    gs_cfg_defaults(cfg);
    return r;
  }

  // --- the identity ---------------------------------------------------------
  // THE THREE LINES THAT ZEROED THE CREATOR PIN STATE HERE ARE GONE (P8-C1).
  // They read:
  //     s_gs.cfg.creator_pin = 0; pin_fail_count = 0; pin_lock_until = 0;
  // and their comment said "until P8 brings the creator server back: a stale
  // lockout deadline or fail count from a firmware that no longer has a PIN
  // screen could lock a user out of a feature they cannot reach". That was the
  // right guard while nothing produced the fields and is the wrong one now: a
  // PIN that does not survive gs_load() is a PIN that changes on every boot,
  // and the user's phone and the QR they scanned would both be stale by the
  // time they typed it.
  //
  // The concern it named is answered rather than ignored. A restored
  // pin_fail_count leaves the gate ARMED but NOT LOCKED - cg_open() never
  // restores a deadline, because no clock survives the power cut that lost it
  // (networking/creator_gate.h) - so a stale count costs exactly one wrong
  // guess before the 60 s throttle, and the correct PIN, which this device
  // prints on its own screen, is accepted immediately either way. Nobody can
  // be locked out of a feature they are standing in front of.

  // Spec section 43: one identity per device, drawn once from the rng service
  // app_setup() seeded with esp_random(), then persisted forever. Never on a
  // read-only session - that would be the first write onto a save the user has
  // not yet been asked about.
  if (s_gs.cfg.device_id == 0) {
    do {
      s_gs.cfg.device_id = rng_u32(RNG_MISC);
    } while (s_gs.cfg.device_id == 0);
    if (r != LOAD_FRESH) {
      save_config(s_gs.cfg);
    }
  }

  // THE WIKI IS BOUND TO THE LIVE BLOB, not copied out of it. game/dex.cpp owns
  // the bit arithmetic and nothing else, so binding here means a discovery made
  // by an encounter roll lands in the SAME fifteen bytes gs_save_cfg() writes -
  // there is no second copy to fall out of step, and no "flush the dex" step
  // anybody can forget.
  dex_bind(s_gs.cfg.dex);

  cfg_from_v2(s_gs.cfg, cfg);
  if (r == LOAD_FRESH) {
    return r;                       // an empty Box: app.cpp files the starter
  }

  s_have_bug = any_bug();
  if (s_have_bug) {
    // Key "t" and the RTC mirror are both newer than the active slot's own
    // stamp, and the catch-up measures the absence from it.
    const uint8_t  act  = (s_gs.box.active_slot < (uint8_t)BOX_SLOTS)
                            ? s_gs.box.active_slot : (uint8_t)0;
    const uint32_t seen = save_last_seen();
    if (seen > s_gs.bugs[act].last_updated_epoch) {
      s_gs.bugs[act].last_updated_epoch = seen;
    }
  }
  return r;
}

uint32_t gs_device_id(void) { return s_gs.cfg.device_id; }

void gs_boot_cal(uint8_t& state, uint32_t& epoch) {
  state = (s_gs.cfg.time_cal_state < (uint8_t)CAL_COUNT) ? s_gs.cfg.time_cal_state
                                                         : (uint8_t)CAL_UNSET;
  epoch = s_gs.cfg.last_known_epoch;
}

void gs_note_time_cal(uint8_t state, uint32_t epoch) {
  if (state >= (uint8_t)CAL_COUNT) return;
  if (s_gs.cfg.time_cal_state == state && s_gs.cfg.time_cal_epoch == epoch) return;
  s_gs.cfg.time_cal_state = state;
  s_gs.cfg.time_cal_epoch = epoch;
  if (epoch > s_gs.cfg.last_known_epoch) s_gs.cfg.last_known_epoch = epoch;
  if (s_readonly) return;
  save_config(s_gs.cfg);
}

LoadResult gs_recover(Config& cfg) {
  if (!save_restore_checkpoint(s_gs)) {
    return LOAD_CORRUPT;            // no copy: nothing written, still read-only
  }
  s_readonly     = false;
  s_box_sig_seen = false;
  cfg_from_v2(s_gs.cfg, cfg);
  s_have_bug = any_bug();
  return LOAD_RECOVERED_CKPT;
}

void gs_boot_tz(char* out, size_t cap) {
  if (!out || cap == 0) return;
  out[0] = '\0';
  size_t i = 0;
  while (s_gs.cfg.tz[i] != '\0' && i + 1 < cap && i + 1 < (size_t)CFGV2_TZ_CAP) {
    out[i] = s_gs.cfg.tz[i];
    ++i;
  }
  out[i] = '\0';
}

// -----------------------------------------------------------------------------
// The hourly-gain ledger, key "gl". Same wear filter as v1: a skipped write has
// to be PROVABLY lossless, which holds when the blob would be byte-identical,
// and when the points are unchanged AND every metered slot sits at its cap (the
// reconstruction clamps to the cap whatever the epoch says). Matching points
// alone is NOT sufficient - below the cap a stale epoch reads back as free
// refill, which is the exploit the ledger exists to bound.
// -----------------------------------------------------------------------------
static_assert(GAIN_CAP_HUNGER_H    <= 255, "GAIN_CAP_HUNGER_H no longer fits a byte");
static_assert(GAIN_CAP_HAPPINESS_H <= 255, "GAIN_CAP_HAPPINESS_H no longer fits a byte");
static_assert(GAIN_CAP_ENERGY_H    <= 255, "GAIN_CAP_ENERGY_H no longer fits a byte");
static_assert(GAIN_CAP_HYGIENE_H   <= 255, "GAIN_CAP_HYGIENE_H no longer fits a byte");

void gs_bind_gain(GsGainFn fn) { s_gain_fn = fn; }

bool gs_load_gain(uint8_t pts[GS_GAIN_SLOTS], uint32_t& epoch) {
  memset(pts, 0, (size_t)GS_GAIN_SLOTS);
  epoch = 0;

  LegacyGainSave g;
  if (kv_get(KV_MAIN, KEY_GAIN, &g, sizeof g) != (int)sizeof g) return false;
  if (g.magic != (uint16_t)LEGACY_GAIN_MAGIC) return false;
  if (g.version != (uint8_t)LEGACY_GAIN_VERSION) return false;
  if (g.slots != GS_GAIN_SLOTS) return false;      // StatId changed under us
  if (g.crc16 != crc16_ccitt(&g, LEGACY_GAINSAVE_CRC_BYTES)) return false;

  memcpy(pts, g.pts, (size_t)GS_GAIN_SLOTS);
  epoch = g.epoch;
  return true;
}

static bool gain_at_cap(const uint8_t pts[GS_GAIN_SLOTS]) {
  return pts[ST_HUNGER]    == (uint8_t)GAIN_CAP_HUNGER_H    &&
         pts[ST_HAPPINESS] == (uint8_t)GAIN_CAP_HAPPINESS_H &&
         pts[ST_ENERGY]    == (uint8_t)GAIN_CAP_ENERGY_H    &&
         pts[ST_HYGIENE]   == (uint8_t)GAIN_CAP_HYGIENE_H;
}

// -----------------------------------------------------------------------------
// The XP ledger, inside the inventory pair. save_load_all() has already checked
// the blob's magic, version and CRC, so what is in s_gs.inv is either what was
// stored or inventory_defaults()' zeros - and a ledger_epoch of 0 is exactly
// what xp_ledger_restore() refuses to trust.
// -----------------------------------------------------------------------------
bool gs_load_xp_ledger(uint8_t pts[XP_LEDGER_SLOTS], uint32_t& epoch) {
  memset(pts, 0, (size_t)XP_LEDGER_SLOTS);
  epoch = 0;
  if (s_gs.inv.ledger_epoch < (uint32_t)NT_EPOCH_SANE_MIN) return false;
  memcpy(pts, s_gs.inv.xp_ledger, (size_t)XP_LEDGER_SLOTS);
  epoch = s_gs.inv.ledger_epoch;
  return true;
}

bool gs_save_xp_ledger(const uint8_t pts[XP_LEDGER_SLOTS], uint32_t epoch) {
  if (s_readonly) return false;
  // An epoch that is not a wall clock describes no interval, so it is stored as
  // 0 - which retires the last trusted snapshot instead of leaving a never-synced
  // unit replaying an intact budget on every reboot (the "gl" reasoning).
  const uint32_t wire = (epoch >= (uint32_t)NT_EPOCH_SANE_MIN) ? epoch : 0u;
  if (memcmp(s_gs.inv.xp_ledger, pts, (size_t)XP_LEDGER_SLOTS) == 0 &&
      s_gs.inv.ledger_epoch == wire) {
    return true;                        // the blob would be byte-identical
  }
  memcpy(s_gs.inv.xp_ledger, pts, (size_t)XP_LEDGER_SLOTS);
  s_gs.inv.ledger_epoch = wire;
  return save_inventory(s_gs.inv);
}

static void gain_commit(void) {
  if (!s_gain_fn) return;

  uint8_t  pts[GS_GAIN_SLOTS];
  uint32_t epoch = 0;
  memset(pts, 0, sizeof pts);
  if (!s_gain_fn(pts, epoch)) return;

  const bool     sane = (epoch >= (uint32_t)NT_EPOCH_SANE_MIN);
  const uint32_t wire = sane ? epoch : 0u;

  if (s_gain_known && s_gain_sane == sane &&
      memcmp(s_gain_last, pts, sizeof pts) == 0 &&
      (wire == s_gain_epoch || gain_at_cap(pts))) {
    return;
  }

  LegacyGainSave g;
  memset(&g, 0, sizeof g);
  g.magic   = (uint16_t)LEGACY_GAIN_MAGIC;
  g.version = (uint8_t)LEGACY_GAIN_VERSION;
  g.slots   = GS_GAIN_SLOTS;
  g.epoch   = wire;
  memcpy(g.pts, pts, sizeof pts);
  g.crc16 = crc16_ccitt(&g, LEGACY_GAINSAVE_CRC_BYTES);

  if (!kv_put(KV_MAIN, KEY_GAIN, &g, sizeof g)) return;

  memcpy(s_gain_last, pts, sizeof pts);
  s_gain_epoch = wire;
  s_gain_sane  = sane;
  s_gain_known = true;
}

// -----------------------------------------------------------------------------
// Writes
// -----------------------------------------------------------------------------
bool gs_save_box(void) {
  if (s_readonly) return false;
  const uint32_t sig = box_signature(s_gs.box);
  if (s_box_sig_seen && sig == s_box_sig) return true;   // nothing changed
  if (!save_box_header(s_gs.box)) return false;
  s_box_sig      = sig;
  s_box_sig_seen = true;
  return true;
}

bool gs_save_slot(uint8_t slot, bool force) {
  if (s_readonly) return false;
  if (slot >= (uint8_t)BOX_SLOTS) return false;
  if (!save_bug(slot, s_gs.bugs[slot], force)) return false;
  if (!save_bug_landed()) return true;   // filtered or deferred: nothing else
  s_have_bug = any_bug();
  (void)gs_save_box();
  return true;
}

bool gs_save_active(bool force) {
  if (s_readonly) return false;
  const uint8_t act = s_gs.box.active_slot;
  if (act >= (uint8_t)BOX_SLOTS) return false;

  if (!save_bug(act, s_gs.bugs[act], force)) {
    return false;
  }
  if (!save_bug_landed()) {
    return true;                    // filtered or deferred: nothing to mirror
  }
  s_have_bug = true;
  (void)gs_save_box();
  gain_commit();

  const uint32_t stamp = s_gs.bugs[act].last_updated_epoch;
  if (stamp != 0) {
    gs_touch_lastseen(stamp);
  }
  return true;
}

bool gs_save_cfg(Config& c) {
  // Sealed IN THE CALLER'S STRUCT, before any store test: app.cpp watches
  // c.crc16 to notice a settings change and re-apply what lives outside the
  // blob (panel contrast), and a unit running RAM-only must still apply its
  // settings for this session.
  cfg_seal(c);
  if (s_readonly) return false;
  cfg_to_v2(c, s_gs.cfg);
  return save_config(s_gs.cfg);
}

void gs_creator_load(uint16_t& pin, uint8_t& fail_count, uint16_t& idle_s) {
  pin        = s_gs.cfg.creator_pin;
  fail_count = s_gs.cfg.pin_fail_count;
  idle_s     = s_gs.cfg.creator_idle_s;   // 0 = never set; the gate resolves it
}

bool gs_creator_store(uint16_t pin, uint8_t fail_count, uint32_t lock_until) {
  if (s_readonly) return false;
  // Nothing changed -> no write. See the header: the caller is reachable from
  // an unauthenticated HTTP request, so an unconditional write here would be a
  // flash-wear lever a remote client controls.
  if (s_gs.cfg.creator_pin    == pin &&
      s_gs.cfg.pin_fail_count == fail_count &&
      s_gs.cfg.pin_lock_until == lock_until) {
    return true;
  }
  s_gs.cfg.creator_pin    = pin;
  s_gs.cfg.pin_fail_count = fail_count;
  s_gs.cfg.pin_lock_until = lock_until;
  return save_config(s_gs.cfg);
}

void gs_touch_lastseen(uint32_t epoch) {
  if (s_readonly) return;
  // The RTC mirror is free and happens on every call: it is what makes a crash
  // reboot report an absence of ~0 instead of one whole "t" period.
  boot_touch_lastseen(epoch);
  save_touch_lastseen(epoch);
  if (epoch != 0) {
    s_gs.cfg.last_known_epoch = epoch;
  }
}

bool gs_factory_reset(void) {
  const bool ok = save_factory_reset();
  s_readonly     = false;
  s_have_bug     = false;
  s_box_sig_seen = false;
  s_gain_known   = false;
  s_gain_sane    = false;
  s_gain_epoch   = 0;
  memset(s_gain_last, 0, sizeof s_gain_last);
  memset(&s_gs, 0, sizeof s_gs);
  save_load_all(s_gs);              // back to sealed defaults, nothing written
  s_have_bug  = any_bug();
  boot_rearm();
  return ok;
}
