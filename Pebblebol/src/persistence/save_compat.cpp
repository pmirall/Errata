// =============================================================================
//  PEBBLEBOL - persistence/save_compat.cpp
//  *** TEMPORARY. P2-C10 DELETES THIS FILE. *** See save_compat.h.
//
//  Two maps and no policy:
//    pet_to_pebble()  the live PetSave -> PebbleInstance slot 0. It is exactly
//                     the v1 -> v2 field map, so it CALLS migrate_v1_to_v2()
//                     rather than restating it: the live PetSave and Config
//                     still have the frozen v1 layouts (legacy_v1.h), which is
//                     the whole reason this transition commit is possible.
//    pet_from_state() the inverse, used only when the companion blob is gone
//                     (a migration, or a checkpoint recovery). The v1 fields
//                     v2 does not carry get the documented defaults below.
//
//  No Arduino include of its own: every clock comes from save_set_clock() and
//  the RTC mirror through hardware/boot.h. It is NOT one of the host-compiled
//  pure modules (plan 1.3 rule 2) and nothing may start treating it as one.
// =============================================================================
#include "save_compat.h"

#include <string.h>

#include "kv_store.h"
#include "migration.h"
#include "../hardware/boot.h"      // the free RTC mirror of the last-seen epoch
#include "../core/config.h"
#include "../core/crc16.h"

// The single live game state. 1,936 B of .bss, exactly the sum of the persisted
// blobs (save_schema.h section 8).
static GameState s_gs;

static bool         s_have_pet   = false;
static bool         s_readonly   = false;
static CompatGainFn s_gain_fn    = nullptr;
static uint8_t      s_gain_last[COMPAT_GAIN_SLOTS];
static uint32_t     s_gain_epoch = 0;
static bool         s_gain_known = false;
static bool         s_gain_sane  = false;

// Signature of the Box header as last written, so a pet save does not drag a
// pair write of the Box along with it every time (the Box has no wear filter of
// its own: nothing about it changes at 1 Hz).
static uint32_t s_box_sig      = 0;
static bool     s_box_sig_seen = false;

void compat_set_readonly(bool ro) { s_readonly = ro; }
bool compat_readonly(void)        { return s_readonly; }

GameState& compat_state(void)  { return s_gs; }
bool       compat_have_pet(void) { return s_have_pet; }

static uint32_t box_signature(const BoxHeader& b) {
  return ((uint32_t)b.slot_mask << 16) ^ ((uint32_t)b.active_slot << 8) ^
         b.next_id_counter ^ ((uint32_t)b.captures << 4) ^ (uint32_t)b.battles ^
         ((uint32_t)b.flags << 24);
}

// -----------------------------------------------------------------------------
// The pet, forwards. The caller's struct is sealed into a local first: the map
// runs through migrate_v1_to_v2(), which validates magic/version/CRC exactly as
// it would for a blob read out of a v1 unit's flash.
// -----------------------------------------------------------------------------
static void pet_seal(PetSave& p) {
  p.magic       = NT_SAVE_MAGIC;
  p.version     = NT_SAVE_VERSION;
  p.reserved[0] = 0;
  p.reserved[1] = 0;
  p.crc16       = crc16_ccitt(&p, PETSAVE_CRC_BYTES);
}

static bool pet_blob_ok(const PetSave& p) {
  return p.magic == NT_SAVE_MAGIC && p.version == NT_SAVE_VERSION &&
         p.crc16 == crc16_ccitt(&p, PETSAVE_CRC_BYTES);
}

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

// Maps the sealed pet into slot 0. Identity fields the v1 pet never had - the
// Pebble id, the creation seed, the XP and the battle counters - are PRESERVED
// from the slot when it already holds this pet, so a save cannot renumber it.
static void pet_to_pebble(const PetSave& sealed, const Config& cfg) {
  static GameState scratch;            // 1,936 B; too big for the loop stack
  if (migrate_v1_to_v2((const uint8_t*)&sealed, (const uint8_t*)&cfg, scratch) != MIGRATE_OK) {
    return;
  }
  PebbleInstance mapped = scratch.pebbles[0];
  const PebbleInstance& old = s_gs.pebbles[0];

  if (!pebble_is_empty(old)) {
    mapped.id            = old.id;
    mapped.creation_seed = old.creation_seed;
    mapped.xp            = old.xp;
    mapped.evo_state     = old.evo_state;
    mapped.hp_cur        = old.hp_cur;
    mapped.battles_won   = old.battles_won;
    mapped.battles_lost  = old.battles_lost;
    mapped.evolutions    = old.evolutions;
    mapped.trades        = old.trades;
    mapped.origin        = old.origin;
    mapped.trait_id      = old.trait_id;
    mapped.custom_sprite = old.custom_sprite;
    memcpy(mapped.moves, old.moves, sizeof mapped.moves);
    mapped.flags = (uint8_t)((old.flags & (uint8_t)~PBF_GOD_TAINTED) | mapped.flags);
  }
  s_gs.pebbles[0] = mapped;
  s_gs.box.slot_mask  |= 0x0001u;
  s_gs.box.active_slot = 0;
  if (s_gs.box.next_id_counter < 2u) {
    s_gs.box.next_id_counter = 2u;
  }
}

// The inverse. Only the fields SaveSchema v2 actually carries can be restored;
// the rest get these defaults, which are the neutral middle of their range:
//   stat[ST_BOND]  50 %      v2 folds bonding into care, not a stat of its own
//   cq             500       "average care" out of 1000
//   happiness_avg  derived from the restored happiness
//   wish / poop / snacks / events   zero: a fresh day, never a punishment
// A v1 EGG becomes a BABY, because level 1 is the lowest v2 records.
static void pet_from_state(PetSave& out) {
  const PebbleInstance& p = s_gs.pebbles[0];
  memset(&out, 0, sizeof out);

  out.stage = (p.level >= 20u) ? (uint8_t)STAGE_SENIOR
            : (p.level >= 15u) ? (uint8_t)STAGE_ADULT
            : (p.level >= 10u) ? (uint8_t)STAGE_TEEN
            : (p.level >=  5u) ? (uint8_t)STAGE_CHILD
                               : (uint8_t)STAGE_BABY;

  out.stat[ST_HUNGER]    = p.care[CARE_HUNGER];
  out.stat[ST_HAPPINESS] = p.care[CARE_HAPPINESS];
  out.stat[ST_ENERGY]    = p.care[CARE_ENERGY];
  out.stat[ST_HYGIENE]   = p.care[CARE_CLEANLINESS];
  out.stat[ST_HEALTH]    = p.care[CARE_HEALTH];
  out.stat[ST_BOND]      = PB_CARE_MILLI_MAX / 2;

  out.stat_rem[ST_HUNGER]    = p.care_rem[CARE_HUNGER];
  out.stat_rem[ST_HAPPINESS] = p.care_rem[CARE_HAPPINESS];
  out.stat_rem[ST_ENERGY]    = p.care_rem[CARE_ENERGY];
  out.stat_rem[ST_HYGIENE]   = p.care_rem[CARE_CLEANLINESS];
  out.stat_rem[ST_HEALTH]    = p.care_rem[CARE_HEALTH];

  out.birth_epoch         = p.birth_epoch;
  out.last_seen_epoch     = p.last_updated_epoch;
  out.last_interact_epoch = p.last_updated_epoch;
  out.egg_epoch           = p.birth_epoch;
  out.age_s               = p.age_s;
  out.genome              = p.genome;
  out.minigames_won       = p.minigames_won;
  out.cq                  = 500;
  out.happiness_avg       = (uint8_t)(p.care[CARE_HAPPINESS] / (PB_CARE_MILLI_MAX / 100));

  if (p.status & PBS_SICK)     out.flags |= PF_SICK;
  if (p.status & PBS_ASLEEP)   out.flags |= PF_ASLEEP;
  if (p.status & PBS_LIGHT_ON) out.flags |= PF_LIGHT_ON;
  if (p.flags  & PBF_GOD_TAINTED) out.flags |= PF_GOD_TAINTED;
  if (s_gs.cfg.flags & CFGV2_F_MUTE) out.flags |= PF_SOUND_MUTE;

  pet_seal(out);
}

// -----------------------------------------------------------------------------
// The config, both ways. Wi-Fi credentials are deliberately NOT persisted any
// more: the product never associates to a station (spec section 68 r5) and
// P5-C1 removes the code path entirely, so adding them to ConfigV2 would be
// designing in a dead field. They keep their compiled-in defaults each session.
// -----------------------------------------------------------------------------
static void cfg_from_v2(const ConfigV2& v2, Config& out) {
  compat_cfg_defaults(out);
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

void compat_cfg_defaults(Config& c) {
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
LoadResult compat_load(PetSave& pet, Config& cfg) {
  const LoadResult r = save_load_all(s_gs);

  memset(&pet, 0, sizeof pet);
  s_have_pet = false;
  s_box_sig_seen = false;
  s_readonly = (r == LOAD_CORRUPT || r == LOAD_FOREIGN_NEWER);

  if (s_readonly) {
    // Nothing has been written and nothing may be: the user chooses.
    compat_cfg_defaults(cfg);
    return r;
  }

  cfg_from_v2(s_gs.cfg, cfg);
  if (r == LOAD_FRESH) {
    return r;
  }

  // The companion blob first: it is the only copy of the v1 fields v2 does not
  // carry. A missing or rotten one is not a failure - the pet is rebuilt from
  // slot 0, which IS the authoritative save.
  PetSave blob;
  if (kv_get(KV_MAIN, KEY_COMPAT_PET, &blob, sizeof blob) == (int)sizeof blob &&
      pet_blob_ok(blob)) {
    pet = blob;
    s_have_pet = true;
  } else if (!pebble_is_empty(s_gs.pebbles[0])) {
    pet_from_state(pet);
    s_have_pet = true;
  }

  if (s_have_pet) {
    // Key "t" and the RTC mirror are both newer than the blob's own stamp.
    const uint32_t seen = save_last_seen();
    if (seen > pet.last_seen_epoch) {
      pet.last_seen_epoch = seen;
    }
    pet_seal(pet);
  }
  return r;
}

void compat_boot_tz(char* out, size_t cap) {
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

void compat_bind_gain(CompatGainFn fn) { s_gain_fn = fn; }

bool compat_load_gain(uint8_t pts[COMPAT_GAIN_SLOTS], uint32_t& epoch) {
  memset(pts, 0, (size_t)COMPAT_GAIN_SLOTS);
  epoch = 0;

  LegacyGainSave g;
  if (kv_get(KV_MAIN, KEY_GAIN, &g, sizeof g) != (int)sizeof g) return false;
  if (g.magic != (uint16_t)LEGACY_GAIN_MAGIC) return false;
  if (g.version != (uint8_t)LEGACY_GAIN_VERSION) return false;
  if (g.slots != COMPAT_GAIN_SLOTS) return false;      // StatId changed under us
  if (g.crc16 != crc16_ccitt(&g, LEGACY_GAINSAVE_CRC_BYTES)) return false;

  memcpy(pts, g.pts, (size_t)COMPAT_GAIN_SLOTS);
  epoch = g.epoch;
  return true;
}

static bool gain_at_cap(const uint8_t pts[COMPAT_GAIN_SLOTS]) {
  return pts[ST_HUNGER]    == (uint8_t)GAIN_CAP_HUNGER_H    &&
         pts[ST_HAPPINESS] == (uint8_t)GAIN_CAP_HAPPINESS_H &&
         pts[ST_ENERGY]    == (uint8_t)GAIN_CAP_ENERGY_H    &&
         pts[ST_HYGIENE]   == (uint8_t)GAIN_CAP_HYGIENE_H;
}

static void gain_commit(void) {
  if (!s_gain_fn) return;

  uint8_t  pts[COMPAT_GAIN_SLOTS];
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
  g.slots   = COMPAT_GAIN_SLOTS;
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
bool compat_save_pet(const PetSave& pet, bool force) {
  if (s_readonly) return false;
  PetSave sealed = pet;
  pet_seal(sealed);

  Config live;
  cfg_from_v2(s_gs.cfg, live);
  pet_to_pebble(sealed, live);

  if (!save_pebble(0, s_gs.pebbles[0], force)) {
    return false;
  }
  if (!save_pebble_landed()) {
    return true;                    // filtered or deferred: nothing to mirror
  }

  // The companion blob rides the pet's cadence exactly, so it can never be more
  // than one write behind slot 0.
  (void)kv_put(KV_MAIN, KEY_COMPAT_PET, &sealed, sizeof sealed);

  const uint32_t sig = box_signature(s_gs.box);
  if (!s_box_sig_seen || sig != s_box_sig) {
    if (save_box_header(s_gs.box)) {
      s_box_sig      = sig;
      s_box_sig_seen = true;
    }
  }

  gain_commit();

  if (sealed.last_seen_epoch != 0) {
    compat_touch_lastseen(sealed.last_seen_epoch);
  }
  return true;
}

bool compat_save_cfg(Config& c) {
  // Sealed IN THE CALLER'S STRUCT, before any store test: app.cpp watches
  // c.crc16 to notice a settings change and re-apply what lives outside the
  // blob (panel contrast), and a unit running RAM-only must still apply its
  // settings for this session.
  cfg_seal(c);
  if (s_readonly) return false;
  cfg_to_v2(c, s_gs.cfg);
  return save_config(s_gs.cfg);
}

void compat_touch_lastseen(uint32_t epoch) {
  if (s_readonly) return;
  // The RTC mirror is free and happens on every call: it is what makes a crash
  // reboot report an absence of ~0 instead of one whole "t" period.
  boot_touch_lastseen(epoch);
  save_touch_lastseen(epoch);
  if (epoch != 0) {
    s_gs.cfg.last_known_epoch = epoch;
  }
}

bool compat_factory_reset(void) {
  const bool ok = save_factory_reset();
  s_readonly     = false;
  s_have_pet     = false;
  s_box_sig_seen = false;
  s_gain_known   = false;
  s_gain_sane    = false;
  s_gain_epoch   = 0;
  memset(s_gain_last, 0, sizeof s_gain_last);
  memset(&s_gs, 0, sizeof s_gs);
  save_load_all(s_gs);              // back to sealed defaults, nothing written
  boot_rearm();
  return ok;
}
