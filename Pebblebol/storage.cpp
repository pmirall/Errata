// =============================================================================
//  NOTTAMAGOCHI - storage.cpp
//  Preferences/NVS wrapper, write-cadence policy, RTC_NOINIT boot nonce and
//  reset-reason classification.
//
//  Design notes that are load-bearing:
//   * Preferences::putX returns BYTES WRITTEN, not a bool. Every write below
//     compares the return against the expected length and raises a sticky
//     STORE_E_* bit plus a Serial line on a mismatch (BRIEF risk 9).
//   * Keys and the namespace are the short literals from config.h and nothing
//     else. Over 15 characters NVS fails silently.
//   * The RTC_NOINIT nonce is evaluated BEFORE it is re-armed. It survives a
//     software reset / panic and is lost on power loss - that asymmetry is the
//     whole crash-vs-abandonment discriminator (BRIEF 1.7, GAME_DESIGN 5.1).
//   * millis() is used only as a flash-wear floor, never for game logic. The
//     game clock (epoch) is passed in by the caller.
// =============================================================================
#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>
#include <esp_attr.h>
#include <string.h>

#include "storage.h"
#include "crc16.h"
#include "rng.h"

// -----------------------------------------------------------------------------
// Contract guards
// -----------------------------------------------------------------------------
static_assert(sizeof(PetSave) == 128, "PetSave must be 128 B on the wire to NVS");
static_assert(sizeof(Config) == 256, "Config must be 256 B on the wire to NVS");
static_assert(sizeof(PendingEgg) == 24, "PendingEgg must be 24 B on the wire to NVS");
static_assert(sizeof(RtcKeep) <= RTC_STRUCT_MAX_BYTES, "RtcKeep exceeds the RTC budget");
static_assert(PETSAVE_CRC_BYTES == 126, "PetSave CRC span drifted");
static_assert(CONFIG_CRC_BYTES == 254, "Config CRC span drifted");
// NVS key/namespace length limit is 16 including the NUL.
static_assert(sizeof(NVS_NS) <= 16, "NVS namespace too long");
static_assert(sizeof(NVS_KEY_LASTSEEN) <= 16, "NVS key 't' too long");
static_assert(sizeof(NVS_KEY_SAVE) <= 16, "NVS key 'save' too long");
static_assert(sizeof(NVS_KEY_CFG) <= 16, "NVS key 'cfg' too long");
static_assert(sizeof(NVS_KEY_EGG) <= 16, "NVS key 'egg' too long");
static_assert(sizeof(NVS_KEY_CANARY) <= 16, "NVS key 'ok' too long");
static_assert(sizeof(NVS_KEY_GAIN) <= 16, "NVS key 'gl' too long");
// GainSave stores whole points in a uint8_t. If a cap ever went above 255 the
// snapshot would silently wrap, so make that a build failure instead.
static_assert(GAIN_CAP_HUNGER_H    <= 255, "GAIN_CAP_HUNGER_H no longer fits a byte");
static_assert(GAIN_CAP_HAPPINESS_H <= 255, "GAIN_CAP_HAPPINESS_H no longer fits a byte");
static_assert(GAIN_CAP_ENERGY_H    <= 255, "GAIN_CAP_ENERGY_H no longer fits a byte");
static_assert(GAIN_CAP_HYGIENE_H   <= 255, "GAIN_CAP_HYGIENE_H no longer fits a byte");

// -----------------------------------------------------------------------------
// Module state
// -----------------------------------------------------------------------------
static Preferences s_prefs;

// Survives a software reset / panic, is lost on power loss. No initializer.
static RTC_NOINIT_ATTR RtcKeep s_rtc;

static bool     s_begun            = false;
static bool     s_open             = false;   // Preferences namespace is open RW
static uint8_t  s_err              = STORE_E_NONE;
static uint16_t s_fails            = 0;

static uint8_t  s_reset_reason     = 0;       // esp_reset_reason_t
static bool     s_rtc_intact       = false;   // nonce survived => no power loss
static uint32_t s_rtc_boot_lastsee = 0;       // RTC mirror as found at boot
static BootKind s_boot_kind        = BOOT_UNKNOWN;
static bool     s_have_save        = false;

static uint32_t s_nvs_last_seen    = 0;       // value of key "t" as last written/read
static uint32_t s_cached_last_seen = 0;       // newest epoch this module has seen

static uint32_t s_last_save_ms     = 0;
static uint32_t s_last_t_ms        = 0;
static bool     s_save_written     = false;
static bool     s_t_written        = false;
static bool     s_save_dirty       = false;   // a forced save was deferred by the floor

// Hourly-gain ledger, key "gl". s_gain_last/-_epoch/-_sane mirror the blob that
// is actually ON FLASH and drive the wear filter described at store_save_gain();
// s_gain_fn is the sim-side provider the .ino binds.
static StoreGainFn s_gain_fn       = 0;
static uint8_t     s_gain_last[NT_GAIN_SLOTS];
static uint32_t    s_gain_last_epoch = 0;   // the epoch as WRITTEN (0 when insane)
static bool        s_gain_have_last = false;
static bool        s_gain_last_sane = false;

// -----------------------------------------------------------------------------
// Small helpers
// -----------------------------------------------------------------------------

// Loud but never blocking: HWCDC's operator bool() is false when the USB host
// has not enumerated, so this cannot stall the boot (BRIEF 1.7).
static void store_log(const char* what, long rc) {
  if (Serial) {
    Serial.printf("[NVS] FAIL %s rc=%ld err=0x%02X\r\n", what, rc, (unsigned)s_err);
  }
}

// A dead NVS means this is reached once per tick, so the Serial line is emitted
// only when the bit transitions. store_write_fails() keeps the full count.
static void store_fail(uint8_t bit, const char* what, long rc) {
  bool first = ((s_err & bit) == 0);
  s_err |= bit;
  if (s_fails < 0xFFFFu) {
    s_fails++;
  }
  if (first) {
    store_log(what, rc);
  }
}

// Zero-fill then copy at most cap-1 chars, so every unused byte of a fixed
// char field is deterministic and the Config CRC is reproducible.
static void nt_setstr(char* dst, size_t cap, const char* src) {
  if (cap == 0) {
    return;
  }
  memset(dst, 0, cap);
  if (src == NULL) {
    return;
  }
  size_t n = strlen(src);
  if (n > cap - 1) {
    n = cap - 1;
  }
  memcpy(dst, src, n);
}

uint16_t store_crc16(const void* data, size_t len) {
  return crc16_ccitt(data, len);
}

// -----------------------------------------------------------------------------
// Boot classification
// -----------------------------------------------------------------------------
static BootKind classify_boot(bool have_save, bool rtc_intact, uint8_t reason) {
  // No usable save beats everything: there is nobody to have abandoned.
  if (!have_save) {
    return BOOT_FIRST_RUN;
  }

  switch (reason) {
    // Power really was removed. RTC fast memory is meaningless here even if it
    // happens to look intact after a very short brownout: trust the reason.
    case ESP_RST_POWERON:
    case ESP_RST_BROWNOUT:
    case ESP_RST_PWR_GLITCH:
    case ESP_RST_EFUSE:
      return BOOT_POWER_LOSS;

    // The firmware died. Never report this as an absence.
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
    case ESP_RST_CPU_LOCKUP:
      return rtc_intact ? BOOT_CRASH : BOOT_UNKNOWN;

    // Deliberate restart, reset pin, USB/JTAG re-plug, esp_restart().
    case ESP_RST_SW:
    case ESP_RST_EXT:
    case ESP_RST_DEEPSLEEP:
    case ESP_RST_SDIO:
    case ESP_RST_USB:
    case ESP_RST_JTAG:
      return rtc_intact ? BOOT_SOFT_RESET : BOOT_UNKNOWN;

    case ESP_RST_UNKNOWN:
    default:
      // An intact nonce proves the chip never lost power, so this cannot be an
      // abandonment however unhelpful the reason code is.
      return rtc_intact ? BOOT_SOFT_RESET : BOOT_UNKNOWN;
  }
}

// -----------------------------------------------------------------------------
// Self test
// -----------------------------------------------------------------------------
bool store_selftest(void) {
  if (!s_open) {
    store_fail(STORE_E_CANARY, "canary(closed)", 0);
    return false;
  }
  // A fresh value every boot: NVS skips writes of an identical value, so a
  // constant pattern would pass without ever exercising the flash path.
  uint32_t pattern = rng_u32(RNG_MISC);
  if (pattern == 0) {
    pattern = (uint32_t)RTC_NONCE_MAGIC;
  }
  size_t n = s_prefs.putUInt(NVS_KEY_CANARY, pattern);
  if (n != sizeof(uint32_t)) {
    store_fail(STORE_E_CANARY, "canary write", (long)n);
    return false;
  }
  uint32_t back = s_prefs.getUInt(NVS_KEY_CANARY, 0);
  if (back != pattern) {
    store_fail(STORE_E_CANARY, "canary readback", (long)back);
    return false;
  }
  return true;
}

// -----------------------------------------------------------------------------
// Boot
// -----------------------------------------------------------------------------
bool store_begin(void) {
  if (s_begun) {
    return s_open;
  }
  s_begun = true;
  s_err = STORE_E_NONE;
  s_fails = 0;

  s_reset_reason = (uint8_t)esp_reset_reason();

  // --- RTC nonce: read the previous power cycle BEFORE re-arming it ---------
  s_rtc_intact = (s_rtc.magic == (uint32_t)RTC_NONCE_MAGIC) && (s_rtc.nonce != 0);
  if (s_rtc_intact) {
    s_rtc_boot_lastsee = s_rtc.last_seen_epoch;
  } else {
    s_rtc_boot_lastsee = 0;
    memset(&s_rtc, 0, sizeof(s_rtc));
    s_rtc.magic = (uint32_t)RTC_NONCE_MAGIC;
    do {
      s_rtc.nonce = rng_u32(RNG_MISC);
    } while (s_rtc.nonce == 0);
  }
  s_rtc.boot_count++;
  s_rtc.uptime_s = 0;
  s_rtc.reserved[0] = 0;
  s_rtc.reserved[1] = 0;

  // --- NVS ------------------------------------------------------------------
  // Read-write on the first boot too: begin(name, true) fails for a namespace
  // that does not exist yet (BRIEF risk 9).
  s_open = s_prefs.begin(NVS_NS, false);
  if (!s_open) {
    s_prefs.end();
    s_open = s_prefs.begin(NVS_NS, false);
  }
  if (!s_open) {
    store_fail(STORE_E_OPEN, "begin(" NVS_NS ")", 0);
    s_boot_kind = classify_boot(false, s_rtc_intact, s_reset_reason);
    return false;
  }

  store_selftest();

  // --- probe the save so the boot kind can be decided -----------------------
  PetSave probe;
  s_have_save = false;
  if (s_prefs.getBytes(NVS_KEY_SAVE, &probe, sizeof(probe)) == sizeof(probe)) {
    if (probe.magic == NT_SAVE_MAGIC && probe.version == NT_SAVE_VERSION &&
        store_crc16(&probe, PETSAVE_CRC_BYTES) == probe.crc16) {
      s_have_save = true;
      s_cached_last_seen = probe.last_seen_epoch;
    }
  }

  s_nvs_last_seen = (uint32_t)s_prefs.getULong64(NVS_KEY_LASTSEEN, 0);
  if (s_nvs_last_seen > s_cached_last_seen) {
    s_cached_last_seen = s_nvs_last_seen;
  }
  if (s_rtc_intact && s_rtc_boot_lastsee > s_cached_last_seen) {
    s_cached_last_seen = s_rtc_boot_lastsee;
  }

  s_boot_kind = classify_boot(s_have_save, s_rtc_intact, s_reset_reason);

  uint32_t now_ms = millis();
  s_last_save_ms = now_ms;
  s_last_t_ms = now_ms;
  s_save_written = false;
  s_t_written = false;
  s_save_dirty = false;

  return true;
}

BootKind store_boot_kind(void) {
  return s_boot_kind;
}

uint8_t store_reset_reason(void) {
  return s_reset_reason;
}

bool store_rtc_intact(void) {
  return s_rtc_intact;
}

uint32_t store_rtc_last_seen(void) {
  return s_rtc_boot_lastsee;
}

uint32_t store_last_seen(void) {
  return s_cached_last_seen;
}

uint32_t store_boot_count(void) {
  return s_rtc.boot_count;
}

void store_rtc_mark_god(void) {
  s_rtc.god_taint = (uint32_t)RTC_NONCE_MAGIC;
}

bool store_rtc_god_tainted(void) {
  return (s_rtc.magic == (uint32_t)RTC_NONCE_MAGIC) && (s_rtc.god_taint != 0);
}

bool store_healthy(void) {
  return s_open && (s_err == STORE_E_NONE);
}

uint8_t store_error(void) {
  return s_err;
}

void store_clear_error(void) {
  s_err = STORE_E_NONE;
}

uint16_t store_write_fails(void) {
  return s_fails;
}

// -----------------------------------------------------------------------------
// PetSave
// -----------------------------------------------------------------------------
bool store_load(PetSave& out) {
  memset(&out, 0, sizeof(out));
  if (!s_open) {
    return false;
  }

  PetSave t;
  size_t n = s_prefs.getBytes(NVS_KEY_SAVE, &t, sizeof(t));
  if (n != sizeof(PetSave)) {
    // Missing key (0) is the normal first-run path and is not an error.
    if (n != 0) {
      store_log("save length", (long)n);
    }
    return false;
  }
  if (t.magic != NT_SAVE_MAGIC) {
    store_log("save magic", (long)t.magic);
    return false;
  }
  if (t.version != NT_SAVE_VERSION) {
    // Refuse a foreign version outright rather than reinterpreting the bytes.
    store_log("save version", (long)t.version);
    return false;
  }
  if (store_crc16(&t, PETSAVE_CRC_BYTES) != t.crc16) {
    store_fail(STORE_E_SAVE_CRC, "save crc", (long)t.crc16);
    return false;
  }

  // Key "t" is written every 60 s, the blob every 5 min, and the RTC mirror
  // every tick. Whichever is newest is the honest last_seen.
  uint32_t ls = t.last_seen_epoch;
  if (s_nvs_last_seen > ls) {
    ls = s_nvs_last_seen;
  }
  if (s_rtc_intact && s_rtc_boot_lastsee > ls) {
    ls = s_rtc_boot_lastsee;
  }
  t.last_seen_epoch = ls;
  if (ls > s_cached_last_seen) {
    s_cached_last_seen = ls;
  }

  s_have_save = true;
  out = t;
  return true;
}

bool store_save(const PetSave& s, bool force) {
  if (!s_open) {
    store_fail(STORE_E_SAVE_W, "save(closed)", 0);
    return false;
  }

  uint32_t now_ms = millis();
  uint32_t since = (uint32_t)(now_ms - s_last_save_ms);

  bool due = force || s_save_dirty ||
             (since >= (uint32_t)(SAVE_FULL_PERIOD_S * 1000UL));
  if (!due) {
    return true;
  }
  // Never lose a forced write, just defer it: the 1 Hz tick calls store_save()
  // again and s_save_dirty flushes it then.
  if (s_save_written && since < STORE_SAVE_MIN_GAP_MS) {
    s_save_dirty = true;
    return true;
  }

  PetSave t = s;
  t.magic = NT_SAVE_MAGIC;
  t.version = NT_SAVE_VERSION;
  t.reserved[0] = 0;
  t.reserved[1] = 0;
  t.crc16 = store_crc16(&t, PETSAVE_CRC_BYTES);

  size_t n = s_prefs.putBytes(NVS_KEY_SAVE, &t, sizeof(t));
  if (n != sizeof(PetSave)) {
    store_fail(STORE_E_SAVE_W, "save", (long)n);
    // Keep the dirty flag up so the next tick retries.
    s_save_dirty = true;
    return false;
  }

  s_last_save_ms = now_ms;
  s_save_written = true;
  s_save_dirty = false;
  s_have_save = true;

  // PH4 6.1. The hourly-gain ledger rides THIS write and no other, so it adds
  // no NVS cadence: "gl" is written when, and only when, "save" is, and only
  // when the numbers actually moved. Asking the provider here rather than
  // caching a copy is what keeps the snapshot honest - ui.cpp forces a save
  // immediately after every successful action, and the spend must be inside the
  // blob that write produces, not one tick behind it. A failure is logged but
  // never fails store_save(): the pet is already on flash, and a lost ledger
  // degrades to the safe seed-0 behaviour, never to a free budget.
  if (s_gain_fn) {
    uint8_t  gpts[NT_GAIN_SLOTS];
    uint32_t gep = 0;
    memset(gpts, 0, sizeof(gpts));
    if (s_gain_fn(gpts, gep)) {
      (void)store_save_gain(gpts, gep);
    }
  }

  if (t.last_seen_epoch != 0) {
    store_touch_lastseen(t.last_seen_epoch);
  }
  return true;
}

// -----------------------------------------------------------------------------
// Hourly-gain ledger (key "gl")
// -----------------------------------------------------------------------------
void store_bind_gain(StoreGainFn fn) {
  s_gain_fn = fn;
}

bool store_load_gain(uint8_t pts[NT_GAIN_SLOTS], uint32_t& epoch) {
  memset(pts, 0, (size_t)NT_GAIN_SLOTS);
  epoch = 0;
  if (!s_open) {
    return false;
  }

  GainSave g;
  size_t n = s_prefs.getBytes(NVS_KEY_GAIN, &g, sizeof(g));
  if (n != sizeof(GainSave)) {
    // Absent (0) is the normal path on the first boot after this firmware and
    // on a wiped unit. Neither is an error: the caller seeds nothing.
    if (n != 0) {
      store_log("gain length", (long)n);
    }
    return false;
  }
  if (g.magic != (uint16_t)NT_GAIN_MAGIC) {
    return false;
  }
  if (g.version != (uint8_t)NT_GAIN_VERSION) {
    return false;
  }
  if (g.slots != NT_GAIN_SLOTS) {
    // StatId grew or shrank under a save written by another build.
    return false;
  }
  if (store_crc16(&g, GAINSAVE_CRC_BYTES) != g.crc16) {
    store_log("gain crc", (long)g.crc16);
    return false;
  }

  memcpy(pts, g.pts, (size_t)NT_GAIN_SLOTS);
  epoch = g.epoch;
  return true;
}

// True when every METERED slot already holds its whole hourly cap. That is the
// one state in which the epoch stamped next to the points cannot matter:
// sim_gain_restore() computes min(cap, saved + elapsed * cap / 3600), so with
// saved == cap the answer is cap for every possible elapsed. Unmetered slots are
// always handed over as 0 by sim_gain_snapshot() and take no part in the test.
// The cap constants are the same ones the static_asserts at the top of this file
// already pin, so this adds no coupling that was not there.
static bool gain_at_cap(const uint8_t pts[NT_GAIN_SLOTS]) {
  return pts[ST_HUNGER]    == (uint8_t)GAIN_CAP_HUNGER_H    &&
         pts[ST_HAPPINESS] == (uint8_t)GAIN_CAP_HAPPINESS_H &&
         pts[ST_ENERGY]    == (uint8_t)GAIN_CAP_ENERGY_H    &&
         pts[ST_HYGIENE]   == (uint8_t)GAIN_CAP_HYGIENE_H;
}

bool store_save_gain(const uint8_t pts[NT_GAIN_SLOTS], uint32_t epoch) {
  if (!s_open) {
    store_fail(STORE_E_SAVE_W, "gain(closed)", 0);
    return false;
  }

  const bool     sane       = (epoch >= (uint32_t)NT_EPOCH_SANE_MIN);
  const uint32_t wire_epoch = sane ? epoch : 0u;

  // Wear filter. A skipped write has to be PROVABLY lossless - what stays on
  // flash must reconstruct, at every future reboot instant, to exactly what the
  // write we are declining would have. That holds in two cases and no others:
  //
  //   1. the blob is byte-identical to the one already there (same points, same
  //      wire epoch). A unit with no clock stamps 0 every time, so this is what
  //      keeps a never-synced device from writing "gl" on every single save;
  //   2. the points are unchanged AND every metered slot is at its cap, where
  //      the reconstruction clamps to the cap whatever the epoch says.
  //
  // Matching POINTS ALONE is NOT sufficient, which is what an earlier version of
  // this filter assumed. Below the cap a stale epoch is read by
  // sim_gain_restore() as elapsed refill: a ledger held at 30/60 by spending
  // exactly the refill rate, across an hour of skipped saves, would come back
  // from a reboot as 60/60 - a whole free hourly cap per power cycle, which is
  // the very exploit the ledger exists to bound. Verified in t_adv.cpp [3].
  if (s_gain_have_last && s_gain_last_sane == sane &&
      memcmp(s_gain_last, pts, (size_t)NT_GAIN_SLOTS) == 0 &&
      (wire_epoch == s_gain_last_epoch || gain_at_cap(pts))) {
    return true;
  }

  GainSave g;
  memset(&g, 0, sizeof(g));
  g.magic = (uint16_t)NT_GAIN_MAGIC;
  g.version = (uint8_t)NT_GAIN_VERSION;
  g.slots = NT_GAIN_SLOTS;
  g.epoch = wire_epoch;
  memcpy(g.pts, pts, (size_t)NT_GAIN_SLOTS);
  g.crc16 = store_crc16(&g, GAINSAVE_CRC_BYTES);

  size_t n = s_prefs.putBytes(NVS_KEY_GAIN, &g, sizeof(g));
  if (n != sizeof(GainSave)) {
    store_fail(STORE_E_SAVE_W, "gain", (long)n);
    return false;
  }

  memcpy(s_gain_last, pts, (size_t)NT_GAIN_SLOTS);
  s_gain_last_epoch = wire_epoch;
  s_gain_last_sane = sane;
  s_gain_have_last = true;
  return true;
}

bool store_touch_lastseen(uint32_t epoch) {
  if (epoch == 0) {
    epoch = s_cached_last_seen;
  }
  if (epoch == 0) {
    return false;   // no clock yet: nothing honest to record
  }

  // PH3 #2. Two kinds of value reach this function: a real wall clock (SNTP
  // has landed, or the estimate was seeded from a real one) and, on a device
  // that has never met NTP, gt_now()'s uptime counter. They are trivially
  // distinguishable - NT_EPOCH_SANE_MIN is 47 years above any plausible
  // uptime - and mixing them is what let a ~55-year "absence" be computed the
  // first time SNTP landed. Once a real epoch has been recorded, an
  // uptime-valued one must never replace it: that would push "t", the RTC
  // mirror and the cache back across the boundary and poison the next boot's
  // baseline. Forward across the boundary (estimate -> real) is exactly what
  // SNTP is for and stays allowed.
  if (epoch < (uint32_t)NT_EPOCH_SANE_MIN &&
      s_cached_last_seen >= (uint32_t)NT_EPOCH_SANE_MIN) {
    return false;
  }

  uint32_t prev = s_cached_last_seen;

  // The RTC mirror is free and must happen on every call: it is what makes a
  // crash reboot report an absence of ~0 instead of up to 60 s.
  s_rtc.last_seen_epoch = epoch;
  s_rtc.uptime_s = millis() / 1000UL;
  s_cached_last_seen = epoch;

  if (!s_open) {
    return false;
  }

  uint32_t now_ms = millis();
  uint32_t since = (uint32_t)(now_ms - s_last_t_ms);
  uint32_t delta = (epoch > prev) ? (epoch - prev) : (prev - epoch);
  bool jumped = (prev != 0) && (delta >= STORE_CLOCK_JUMP_S);

  bool due = (!s_t_written) || jumped ||
             (since >= (uint32_t)(SAVE_LASTSEEN_PERIOD_S * 1000UL));
  if (!due) {
    return true;
  }
  // Wear floor: an SNTP correction storm must not turn into a write storm.
  if (s_t_written && since < STORE_T_MIN_GAP_MS) {
    return true;
  }

  size_t n = s_prefs.putULong64(NVS_KEY_LASTSEEN, (uint64_t)epoch);
  if (n != sizeof(uint64_t)) {
    store_fail(STORE_E_LASTSEEN_W, "t", (long)n);
    return false;
  }
  s_last_t_ms = now_ms;
  s_t_written = true;
  s_nvs_last_seen = epoch;
  return true;
}

// -----------------------------------------------------------------------------
// Config
// -----------------------------------------------------------------------------
void store_cfg_defaults(Config& c) {
  memset(&c, 0, sizeof(c));
  c.magic = NT_CFG_MAGIC;
  c.version = NT_CFG_VERSION;
  c.saved_epoch = 0;

  nt_setstr(c.wifi_ssid, sizeof(c.wifi_ssid), CFG_WIFI_SSID);
  nt_setstr(c.wifi_pass, sizeof(c.wifi_pass), CFG_WIFI_PASS);
  nt_setstr(c.pet_name, sizeof(c.pet_name), CFG_PET_NAME);
  nt_setstr(c.tz, sizeof(c.tz), CFG_TZ_STRING);
  memset(c.reserved_a, 0, sizeof(c.reserved_a));
  memset(c.reserved_b, 0, sizeof(c.reserved_b));
  c.reserved_c = 0;

  // CF_WEB_ENABLED is DELIBERATELY CLEAR on a fresh device (plan section 2 row
  // G4): the radio is off by default and the web server is opt-in from
  // SETTINGS. CF_BLE_ENABLED stays on because SOCIAL brings the stack up
  // and down inside the screen.
  uint8_t f = 0;
#if FEATURE_BLE
  f |= CF_BLE_ENABLED;
#endif
  c.flags = f;

  c.brightness = (uint8_t)OLED_CONTRAST_DEFAULT;
  c.statusbar_mode = (uint8_t)SBAR_ICONS;
  c.reserved[0] = 0;
  c.reserved[1] = 0;
  c.reserved[2] = 0;
  c.crc16 = store_crc16(&c, CONFIG_CRC_BYTES);
}

bool store_load_cfg(Config& out) {
  if (s_open) {
    Config t;
    size_t n = s_prefs.getBytes(NVS_KEY_CFG, &t, sizeof(t));
    if (n == sizeof(Config) && t.magic == NT_CFG_MAGIC &&
        t.version == NT_CFG_VERSION &&
        store_crc16(&t, CONFIG_CRC_BYTES) == t.crc16) {
      out = t;
      return true;
    }
    if (n != 0 && n != sizeof(Config)) {
      store_log("cfg length", (long)n);
    }
  }
  store_cfg_defaults(out);
  return false;
}

bool store_save_cfg(Config& c) {
  // Seal IN PLACE, before the s_open test. 'c' is the caller's live object -
  // usually the entry point's g_cfg, which ui and webui both hold a
  // pointer to - and the .ino's config_changed() compares c.crc16 against the
  // value apply_config() latched. Sealing into a local copy (as this function
  // used to) left that CRC frozen for the whole session, so config_changed()
  // could never return true, apply_config() never re-ran, and a brightness set
  // from the phone never reached rd_set_contrast() until the next reboot.
  // magic/version/reserved and the NUL terminators were silently divergent for
  // the same reason. Sealing before the NVS test also means a unit running
  // RAM-only (store_begin() failed) still applies its settings this session.
  c.magic = NT_CFG_MAGIC;
  c.version = NT_CFG_VERSION;
  c.reserved[0] = 0;
  c.reserved[1] = 0;
  c.reserved[2] = 0;
  // Retired fields - the Telegram token/chat id, the weather coordinates and
  // the Telegram mode - keep their offsets but are zeroed on every save, so a
  // v1 blob loses them the first time the user changes anything.
  memset(c.reserved_a, 0, sizeof(c.reserved_a));
  memset(c.reserved_b, 0, sizeof(c.reserved_b));
  c.reserved_c = 0;
  // Every fixed char field must be NUL terminated before it is hashed or used.
  c.wifi_ssid[sizeof(c.wifi_ssid) - 1] = '\0';
  c.wifi_pass[sizeof(c.wifi_pass) - 1] = '\0';
  c.pet_name[sizeof(c.pet_name) - 1] = '\0';
  c.tz[sizeof(c.tz) - 1] = '\0';
  c.crc16 = store_crc16(&c, CONFIG_CRC_BYTES);

  if (!s_open) {
    store_fail(STORE_E_CFG_W, "cfg(closed)", 0);
    return false;
  }

  size_t n = s_prefs.putBytes(NVS_KEY_CFG, &c, sizeof(c));
  if (n != sizeof(Config)) {
    store_fail(STORE_E_CFG_W, "cfg", (long)n);
    return false;
  }
  return true;
}

// -----------------------------------------------------------------------------
// Pending egg
// -----------------------------------------------------------------------------
bool store_load_egg(PendingEgg& out) {
  memset(&out, 0, sizeof(out));
  if (!s_open) {
    return false;
  }
  PendingEgg e;
  size_t n = s_prefs.getBytes(NVS_KEY_EGG, &e, sizeof(e));
  if (n != sizeof(PendingEgg)) {
    return false;
  }
  if (e.magic != NT_EGG_MAGIC || e.version != NT_EGG_VERSION) {
    return false;
  }
  if ((e.flags & EF_VALID) == 0) {
    return false;
  }
  // Same integrity rule the BLE path applies to a received genome.
  if ((e.genome.magic_ver & GENOME_SIG_MASK) != GENOME_SIG) {
    return false;
  }
  if (store_crc16(&e.genome, GENOME_CRC_BYTES) != e.genome.crc16) {
    store_log("egg genome crc", (long)e.genome.crc16);
    return false;
  }
  out = e;
  return true;
}

bool store_save_egg(const PendingEgg& e) {
  if (!s_open) {
    store_fail(STORE_E_EGG_W, "egg(closed)", 0);
    return false;
  }
  PendingEgg t = e;
  t.magic = NT_EGG_MAGIC;
  t.version = NT_EGG_VERSION;
  t.flags = (uint8_t)(t.flags | EF_VALID);
  size_t n = s_prefs.putBytes(NVS_KEY_EGG, &t, sizeof(t));
  if (n != sizeof(PendingEgg)) {
    store_fail(STORE_E_EGG_W, "egg", (long)n);
    return false;
  }
  return true;
}

bool store_clear_egg(void) {
  if (!s_open) {
    return false;
  }
  if (!s_prefs.isKey(NVS_KEY_EGG)) {
    return true;   // already absent
  }
  if (!s_prefs.remove(NVS_KEY_EGG)) {
    store_fail(STORE_E_EGG_W, "egg remove", 0);
    return false;
  }
  return true;
}

// -----------------------------------------------------------------------------
// Factory reset
// -----------------------------------------------------------------------------
bool store_wipe(void) {
  if (!s_open) {
    store_fail(STORE_E_OPEN, "wipe(closed)", 0);
    return false;
  }
  bool ok = s_prefs.clear();
  if (!ok) {
    store_fail(STORE_E_OPEN, "wipe", 0);
  }

  s_have_save = false;
  s_nvs_last_seen = 0;
  s_cached_last_seen = 0;

  uint32_t now_ms = millis();
  s_last_save_ms = now_ms;
  s_last_t_ms = now_ms;
  s_save_written = false;
  s_t_written = false;
  s_save_dirty = false;

  // clear() removed key "gl" with everything else; drop the wear filter too, or
  // the next save would compare against a blob that no longer exists and skip.
  memset(s_gain_last, 0, sizeof(s_gain_last));
  s_gain_last_epoch = 0;
  s_gain_have_last = false;
  s_gain_last_sane = false;

  memset(&s_rtc, 0, sizeof(s_rtc));
  s_rtc.magic = (uint32_t)RTC_NONCE_MAGIC;
  do {
    s_rtc.nonce = rng_u32(RNG_MISC);
  } while (s_rtc.nonce == 0);
  s_rtc.boot_count = 1;

  s_boot_kind = BOOT_FIRST_RUN;

  // clear() removed the canary too: put it back so store_selftest() stays
  // meaningful for the rest of this power cycle.
  store_selftest();
  return ok;
}
