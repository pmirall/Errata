// =============================================================================
//  PEBBLEBOL - persistence/game_state.cpp
//  The live GameState, the Config view of ConfigV2, the "gl" anti-farm ledger
//  and the device identity. See game_state.h for what P2-C10 removed.
//
//  No Arduino include of its own: every clock comes from save_set_clock() and
//  the RTC mirror through hardware/boot.h. It is NOT one of the host-compiled
//  pure modules (plan 1.3 rule 2) and nothing may start treating it as one.
// =============================================================================
#include "game_state.h"

#include <string.h>

#include "kv_store.h"
#include "../hardware/boot.h"      // the free RTC mirror of the last-seen epoch
#include "../core/config.h"
#include "../core/crc16.h"
#include "../core/rng.h"           // the device id of spec section 43

// The single live game state. 1,936 B of .bss, exactly the sum of the persisted
// blobs (save_schema.h section 8).
static GameState s_gs;

static bool         s_have_pebble = false;
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
bool       gs_have_pebble(void) { return s_have_pebble; }

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
  if (v2.flags & CFGV2_F_BLE)  f |= CF_BLE_ENABLED;
  out.flags = f;

  // The pet's display name. v2 keeps it on the Pebble; the v1 UI reads it from
  // Config, so the two are held in step until P2-C11 moves the UI over. The
  // config copy wins when it has one: it is the field SETTINGS writes, so a
  // rename must not be undone by the nickname the last save mapped.
  const char* name = (v2.device_name[0] != '\0') ? v2.device_name
                                                 : s_gs.pebbles[0].nickname;
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
                                                 CFGV2_F_BLE | CFGV2_F_SBAR_MASK));
  if (c.flags & CF_MUTE)        f |= CFGV2_F_MUTE;
  if (c.flags & CF_WEB_ENABLED) f |= CFGV2_F_WEB;
  if (c.flags & CF_BLE_ENABLED) f |= CFGV2_F_BLE;
  f |= (uint16_t)(((uint16_t)c.statusbar_mode << CFGV2_F_SBAR_SH) & CFGV2_F_SBAR_MASK);
  v2.flags = f;

  size_t n = 0;
  while (c.pet_name[n] != '\0' && n + 1 < (size_t)CFGV2_NAME_CAP) {
    v2.device_name[n] = c.pet_name[n];
    ++n;
  }
  v2.device_name[n] = '\0';
}

void gs_cfg_defaults(Config& c) {
  memset(&c, 0, sizeof c);
  size_t i = 0;
  const char* tz = CFG_TZ_STRING;
  while (tz[i] != '\0' && i + 1 < sizeof c.tz) { c.tz[i] = tz[i]; ++i; }
  c.tz[i] = '\0';

  // CF_WEB_ENABLED is DELIBERATELY CLEAR on a fresh device (plan section 2 row
  // G4): the radio is off by default and the creator server is opt-in.
  uint8_t f = 0;
#if FEATURE_BLE
  f |= CF_BLE_ENABLED;
#endif
  c.flags          = f;
  c.brightness     = (uint8_t)OLED_CONTRAST_DEFAULT;
  c.statusbar_mode = (uint8_t)SBAR_ICONS;
  cfg_seal(c);
}

// -----------------------------------------------------------------------------
// Load
// -----------------------------------------------------------------------------
// True when any slot holds a Pebble. The header's slot_mask is only an index;
// the slots themselves are the truth (plan 1.5.4).
static bool any_pebble(void) {
  for (uint8_t i = 0; i < (uint8_t)BOX_SLOTS; ++i) {
    if (!pebble_is_empty(s_gs.pebbles[i])) return true;
  }
  return false;
}

LoadResult gs_load(Config& cfg) {
  const LoadResult r = save_load_all(s_gs);

  s_have_pebble = false;
  s_box_sig_seen = false;
  s_readonly = (r == LOAD_CORRUPT || r == LOAD_FOREIGN_NEWER);

  if (s_readonly) {
    // Nothing has been written and nothing may be: the user chooses.
    gs_cfg_defaults(cfg);
    return r;
  }

  // --- the identity and the fields no screen owns yet -----------------------
  // Creator PIN state is zeroed until P8 brings the creator server back: a
  // stale lockout deadline or fail count from a firmware that no longer has a
  // PIN screen could lock a user out of a feature they cannot reach.
  s_gs.cfg.creator_pin    = 0;
  s_gs.cfg.pin_fail_count = 0;
  s_gs.cfg.pin_lock_until = 0;

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

  cfg_from_v2(s_gs.cfg, cfg);
  if (r == LOAD_FRESH) {
    return r;                       // an empty Box: app.cpp files the starter
  }

  s_have_pebble = any_pebble();
  if (s_have_pebble) {
    // Key "t" and the RTC mirror are both newer than the active slot's own
    // stamp, and the catch-up measures the absence from it.
    const uint8_t  act  = (s_gs.box.active_slot < (uint8_t)BOX_SLOTS)
                            ? s_gs.box.active_slot : (uint8_t)0;
    const uint32_t seen = save_last_seen();
    if (seen > s_gs.pebbles[act].last_updated_epoch) {
      s_gs.pebbles[act].last_updated_epoch = seen;
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
  s_have_pebble = any_pebble();
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
  if (!save_pebble(slot, s_gs.pebbles[slot], force)) return false;
  if (!save_pebble_landed()) return true;   // filtered or deferred: nothing else
  s_have_pebble = any_pebble();
  (void)gs_save_box();
  return true;
}

bool gs_save_active(bool force) {
  if (s_readonly) return false;
  const uint8_t act = s_gs.box.active_slot;
  if (act >= (uint8_t)BOX_SLOTS) return false;

  if (!save_pebble(act, s_gs.pebbles[act], force)) {
    return false;
  }
  if (!save_pebble_landed()) {
    return true;                    // filtered or deferred: nothing to mirror
  }
  s_have_pebble = true;
  (void)gs_save_box();
  gain_commit();

  const uint32_t stamp = s_gs.pebbles[act].last_updated_epoch;
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
  s_have_pebble     = false;
  s_box_sig_seen = false;
  s_gain_known   = false;
  s_gain_sane    = false;
  s_gain_epoch   = 0;
  memset(s_gain_last, 0, sizeof s_gain_last);
  memset(&s_gs, 0, sizeof s_gs);
  save_load_all(s_gs);              // back to sealed defaults, nothing written
  s_have_pebble  = any_pebble();
  boot_rearm();
  return ok;
}
