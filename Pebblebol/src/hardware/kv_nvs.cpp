// =============================================================================
//  PEBBLEBOL - hardware/kv_nvs.cpp
//  The Preferences implementation of persistence/kv_store.h. See kv_nvs.h.
//
//  Design notes that are load-bearing:
//   * Preferences::putBytes() returns BYTES WRITTEN, not a bool. Every write
//     compares the return against the expected length and raises a sticky
//     KV_E_* bit plus one Serial line on the transition (audit risk 9).
//   * begin() is opened READ-WRITE even on a first boot: begin(name, true)
//     fails outright for a namespace that does not exist yet.
//   * A stored value LARGER than the caller's buffer is an error, never a
//     truncation: a short read of a blob is indistinguishable from a corrupt
//     one and must not reach a CRC check (kv_store.h contract).
//   * No game rules, no clock, no cadence. The write policy is save_manager's.
// =============================================================================
#include "kv_nvs.h"

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

#include "../core/rng.h"
#include "../persistence/save_schema.h"     // the key table, incl. the v1 keys
#include "../persistence/legacy_v1.h"       // the v1 blob sizes, for the import

static Preferences s_prefs[KV_PART_COUNT];
static bool        s_open[KV_PART_COUNT]  = { false, false };
static bool        s_wfail[KV_PART_COUNT] = { false, false };

static bool     s_begun    = false;
static uint8_t  s_err      = KV_E_NONE;
static uint16_t s_fails    = 0;
static bool     s_imported = false;

// Loud but never blocking: HWCDC's operator bool() is false when the USB host
// has not enumerated, so this cannot stall the boot.
static void kv_log(const char* what, long rc) {
  if (Serial) {
    Serial.printf("[NVS] FAIL %s rc=%ld err=0x%02X\r\n", what, rc, (unsigned)s_err);
  }
}

// A dead partition is reached once per tick, so the Serial line is emitted only
// when the bit transitions. kv_write_fails() keeps the full count.
static void kv_fail(uint8_t bit, const char* what, long rc) {
  const bool first = ((s_err & bit) == 0);
  s_err |= bit;
  if (s_fails < 0xFFFFu) {
    s_fails++;
  }
  if (first) {
    kv_log(what, rc);
  }
}

static bool part_ok(KvPart part) {
  return (part < KV_PART_COUNT) && s_open[part];
}

// -----------------------------------------------------------------------------
// The pure seam (persistence/kv_store.h)
// -----------------------------------------------------------------------------
int kv_get(KvPart part, const char* key, void* buf, size_t cap) {
  if (!key || key[0] == '\0' || !buf) {
    return -1;
  }
  if (!part_ok(part)) {
    return -1;
  }
  if (!s_prefs[part].isKey(key)) {
    return 0;                       // absent is not a fault
  }
  const size_t len = s_prefs[part].getBytesLength(key);
  if (len == 0) {
    return 0;
  }
  if (len > cap) {
    kv_fail(KV_E_READ, key, (long)len);
    return -1;
  }
  const size_t n = s_prefs[part].getBytes(key, buf, len);
  if (n != len) {
    kv_fail(KV_E_READ, key, (long)n);
    return -1;
  }
  return (int)n;
}

bool kv_put(KvPart part, const char* key, const void* buf, size_t n) {
  if (!key || key[0] == '\0' || !buf || n == 0) {
    return false;
  }
  if (strlen(key) > (size_t)KV_KEY_MAX_LEN) {
    return false;                   // over 15 characters NVS fails silently
  }
  if (!part_ok(part)) {
    kv_fail(KV_E_WRITE, key, 0);
    return false;
  }
  const size_t w = s_prefs[part].putBytes(key, buf, n);
  if (w != n) {
    s_wfail[part] = true;
    kv_fail(KV_E_WRITE, key, (long)w);
    return false;
  }
  return true;
}

bool kv_erase(KvPart part, const char* key) {
  if (!key || key[0] == '\0') {
    return false;
  }
  if (!part_ok(part)) {
    return false;
  }
  if (!s_prefs[part].isKey(key)) {
    return true;                    // already gone
  }
  if (!s_prefs[part].remove(key)) {
    kv_fail(KV_E_ERASE, key, 0);
    return false;
  }
  return true;
}

bool kv_wipe(KvPart part) {
  if (!part_ok(part)) {
    return false;
  }
  const bool ok = s_prefs[part].clear();
  if (!ok) {
    kv_fail(KV_E_ERASE, "wipe", 0);
  } else {
    s_wfail[part] = false;
  }
  return ok;
}

bool kv_healthy(KvPart part) {
  return part_ok(part) && !s_wfail[part];
}

// -----------------------------------------------------------------------------
// Canary. A fresh value every boot: NVS skips writes of an identical value, so
// a constant pattern would pass without ever exercising the flash path.
// -----------------------------------------------------------------------------
static bool canary_one(KvPart part) {
  if (!part_ok(part)) {
    return false;
  }
  uint32_t pattern = rng_u32(RNG_MISC);
  if (pattern == 0) {
    pattern = 0xB1C0FEEDu;
  }
  if (!kv_put(part, KEY_CANARY, &pattern, sizeof pattern)) {
    kv_fail(KV_E_CANARY, "canary write", 0);
    return false;
  }
  uint32_t back = 0;
  if (kv_get(part, KEY_CANARY, &back, sizeof back) != (int)sizeof back || back != pattern) {
    kv_fail(KV_E_CANARY, "canary readback", (long)back);
    return false;
  }
  return true;
}

bool kv_selftest(void) {
  bool ok = canary_one(KV_MAIN);
  if (s_open[KV_CKPT]) {
    ok = canary_one(KV_CKPT) && ok;
  }
  return ok;
}

// -----------------------------------------------------------------------------
// The one-shot v1 import (decision D3).
//
// The migration is pure and reads through kv_get(KV_MAIN, "save"/"cfg"), so the
// import's whole job is to move the bytes across the namespace boundary. It
// runs only when "pbbl" holds neither a v2 Box nor an already-imported v1 save,
// so it can never overwrite live state, and "notta" is cleared afterwards: a
// factory reset must not be able to resurrect a pet the owner deleted.
// -----------------------------------------------------------------------------
static bool import_blob(Preferences& src, const char* key, size_t expect) {
  if (!src.isKey(key) || src.getBytesLength(key) != expect) {
    return false;
  }
  uint8_t buf[sizeof(LegacyConfig)];
  if (expect > sizeof buf) {
    return false;
  }
  if (src.getBytes(key, buf, expect) != expect) {
    return false;
  }
  return kv_put(KV_MAIN, key, buf, expect);
}

static void legacy_import(void) {
  char key0[KV_KEY_CAP];
  char key1[KV_KEY_CAP];
  key_pair(KEY_BOX_PREFIX, 0, key0);
  key_pair(KEY_BOX_PREFIX, 1, key1);
  if (s_prefs[KV_MAIN].isKey(key0) || s_prefs[KV_MAIN].isKey(key1)) {
    return;                          // a v2 save is already here
  }
  if (s_prefs[KV_MAIN].isKey(KEY_V1_SAVE)) {
    return;                          // imported by an earlier boot, not yet migrated
  }

  Preferences old;
  if (!old.begin(PB_NVS_LEGACY_NS, true)) {
    return;                          // no v1 namespace: a genuinely new unit
  }
  if (!import_blob(old, KEY_V1_SAVE, sizeof(LegacyPetSave))) {
    old.end();
    return;                          // nothing worth importing
  }
  // Everything below is best effort: the pet is the part that must not be lost.
  (void)import_blob(old, KEY_V1_CFG, sizeof(LegacyConfig));
  (void)import_blob(old, KEY_GAIN,   sizeof(LegacyGainSave));

  const uint64_t seen = old.getULong64(KEY_LASTSEEN, 0);
  if (seen != 0) {
    // v1 wrote "t" as a U64 entry; v2 writes the same eight bytes as a blob.
    (void)kv_put(KV_MAIN, KEY_LASTSEEN, &seen, sizeof seen);
  }
  old.end();

  // Verify the pet crossed before dropping the original.
  uint8_t check[sizeof(LegacyPetSave)];
  if (kv_get(KV_MAIN, KEY_V1_SAVE, check, sizeof check) != (int)sizeof check) {
    kv_fail(KV_E_IMPORT, "v1 import", 0);
    return;
  }
  Preferences wipe;
  if (wipe.begin(PB_NVS_LEGACY_NS, false)) {
    (void)wipe.clear();
    wipe.end();
  }
  s_imported = true;
}

bool kv_legacy_imported(void) {
  return s_imported;
}

// -----------------------------------------------------------------------------
// Open
// -----------------------------------------------------------------------------
bool kv_begin(void) {
  if (s_begun) {
    return s_open[KV_MAIN];
  }
  s_begun = true;
  s_err   = KV_E_NONE;
  s_fails = 0;

  // Read-write on the first boot too: begin(name, true) fails for a namespace
  // that does not exist yet.
  s_open[KV_MAIN] = s_prefs[KV_MAIN].begin(PB_NVS_NAMESPACE, false);
  if (!s_open[KV_MAIN]) {
    s_prefs[KV_MAIN].end();
    s_open[KV_MAIN] = s_prefs[KV_MAIN].begin(PB_NVS_NAMESPACE, false);
  }
  if (!s_open[KV_MAIN]) {
    kv_fail(KV_E_OPEN_MAIN, "begin(" PB_NVS_NAMESPACE ")", 0);
  }

  // The checkpoint partition. Missing "nvs2" is a degraded unit, not a dead
  // one: it loses D6's recovery, nothing else.
  s_open[KV_CKPT] = s_prefs[KV_CKPT].begin(PB_NVS_NAMESPACE, false, PB_NVS_CKPT_PART);
  if (!s_open[KV_CKPT]) {
    kv_fail(KV_E_OPEN_CKPT, "begin(" PB_NVS_CKPT_PART ")", 0);
  }

  if (s_open[KV_MAIN]) {
    (void)kv_selftest();
    legacy_import();
  }
  return s_open[KV_MAIN];
}

uint8_t  kv_error(void)       { return s_err; }
void     kv_clear_error(void) { s_err = KV_E_NONE; }
uint16_t kv_write_fails(void) { return s_fails; }
