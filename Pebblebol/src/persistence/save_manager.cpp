// =============================================================================
//  PEBBLEBOL - persistence/save_manager.cpp
//  The save policy and the schema helpers it needs. See save_manager.h for the
//  contract and save_schema.h for the layouts. Pure: no Arduino, no allocation.
// =============================================================================
#include "save_manager.h"

#include <string.h>

#include "kv_store.h"
#include "migration.h"
#include "../core/crc16.h"
#include "../game/species_custom.h"   // the creator species registry (P8-C3)

// The widest blob in the schema; every scratch buffer here is one of these.
#define SAVE_BLOB_MAX  sizeof(CooldownTable)
static_assert(sizeof(CooldownTable) >= sizeof(ConfigV2),      "blob buffer too small");
static_assert(sizeof(CooldownTable) >= sizeof(PebbleInstance),"blob buffer too small");
static_assert(sizeof(CooldownTable) >= sizeof(CustomSpeciesRec), "blob buffer too small");

// =============================================================================
//  PART 1 - the schema helpers declared by save_schema.h
// =============================================================================

// --- keys --------------------------------------------------------------------
// Every builder is total: an out-of-range index yields the empty string, which
// kv_* rejects, rather than a key that could alias another blob.
static char* key_copy(char* out, const char* s) {
  size_t i = 0;
  while (s[i] != '\0' && i < (size_t)KV_KEY_MAX_LEN) { out[i] = s[i]; ++i; }
  out[i] = '\0';
  return out;
}

const char* key_pair(const char* prefix, uint8_t copy, char* out) {
  if (!out) return "";
  if (!prefix || copy > 1) { out[0] = '\0'; return out; }
  key_copy(out, prefix);
  const size_t n = strlen(out);
  if (n + 1 >= (size_t)KV_KEY_CAP) { out[0] = '\0'; return out; }
  out[n]     = (char)('0' + copy);
  out[n + 1] = '\0';
  return out;
}

const char* key_pebble(uint8_t slot, uint8_t copy, char* out) {
  if (!out) return "";
  if (slot >= BOX_SLOTS || copy > 1) { out[0] = '\0'; return out; }
  out[0] = 'p'; out[1] = 'b';
  out[2] = (char)('0' + slot);
  out[3] = (char)('0' + copy);
  out[4] = '\0';
  return out;
}

const char* key_custom(uint8_t slot, char* out) {
  if (!out) return "";
  if (slot >= CUSTOM_SPECIES_SLOTS) { out[0] = '\0'; return out; }
  out[0] = 'c'; out[1] = 's';
  out[2] = (char)('0' + slot);
  out[3] = '\0';
  return out;
}

const char* key_ck_pebble(uint8_t slot, char* out) {
  if (!out) return "";
  if (slot >= BOX_SLOTS) { out[0] = '\0'; return out; }
  key_copy(out, KEY_CK_PEBBLE_PREFIX);
  const size_t n = strlen(out);
  out[n]     = (char)('0' + slot);
  out[n + 1] = '\0';
  return out;
}

// --- sealing and checking ----------------------------------------------------
// One description per blob, so the pair machinery below needs no templates and
// no per-type duplication: magic, where the version and the seq live, and how
// many bytes the CRC covers.
struct BlobOps {
  uint16_t magic;
  uint8_t  version;      // the value this firmware writes
  size_t   size;
  size_t   ver_off;
  size_t   seq_off;      // SIZE_MAX for the single-key blobs, which carry none
};

#define NO_SEQ ((size_t)-1)

static const BlobOps OPS_PEBBLE = { PEBBLE_MAGIC, PEBBLE_LAYOUT_VER, sizeof(PebbleInstance),
                                    offsetof(PebbleInstance, layout_ver),
                                    offsetof(PebbleInstance, seq) };
static const BlobOps OPS_BOX    = { BOX_MAGIC, SAVE_SCHEMA_VERSION, sizeof(BoxHeader),
                                    offsetof(BoxHeader, schema_version),
                                    offsetof(BoxHeader, seq) };
static const BlobOps OPS_CFG    = { CFGV2_MAGIC, SAVE_SCHEMA_VERSION, sizeof(ConfigV2),
                                    offsetof(ConfigV2, version),
                                    offsetof(ConfigV2, seq) };
static const BlobOps OPS_INV    = { INV_MAGIC, SAVE_SCHEMA_VERSION, sizeof(Inventory),
                                    offsetof(Inventory, version),
                                    offsetof(Inventory, seq) };
static const BlobOps OPS_CD     = { CD_MAGIC, SAVE_SCHEMA_VERSION, sizeof(CooldownTable),
                                    offsetof(CooldownTable, version),
                                    offsetof(CooldownTable, seq) };
static const BlobOps OPS_CS     = { CS_MAGIC, SAVE_SCHEMA_VERSION, sizeof(CustomSpeciesRec),
                                    offsetof(CustomSpeciesRec, version), NO_SEQ };
static const BlobOps OPS_TRADE  = { TR_MAGIC, SAVE_SCHEMA_VERSION, sizeof(PendingTrade),
                                    offsetof(PendingTrade, version), NO_SEQ };

static uint16_t u16_at(const void* p, size_t off) {
  uint16_t v; memcpy(&v, (const uint8_t*)p + off, sizeof v); return v;
}
static uint32_t u32_at(const void* p, size_t off) {
  uint32_t v; memcpy(&v, (const uint8_t*)p + off, sizeof v); return v;
}

// Magic and CRC only: the version is judged separately, because a blob from a
// NEWER firmware is intact data we must not touch, not damaged data.
static bool blob_intact(const void* blob, const BlobOps& o) {
  if (u16_at(blob, 0) != o.magic) return false;
  const uint16_t stored = u16_at(blob, o.size - 2);
  return stored == crc16_ccitt(blob, o.size - 2);
}

static bool blob_ok(const void* blob, const BlobOps& o) {
  if (!blob_intact(blob, o)) return false;
  return ((const uint8_t*)blob)[o.ver_off] == o.version;
}

static void blob_seal(void* blob, const BlobOps& o) {
  uint8_t* b = (uint8_t*)blob;
  const uint16_t magic = o.magic;
  memcpy(b, &magic, sizeof magic);
  b[o.ver_off] = o.version;
  const uint16_t crc = crc16_ccitt(b, o.size - 2);
  memcpy(b + o.size - 2, &crc, sizeof crc);
}

void pebble_seal(PebbleInstance& p)          { blob_seal(&p, OPS_PEBBLE); }
bool pebble_blob_ok(const PebbleInstance& p) { return blob_ok(&p, OPS_PEBBLE); }
void box_seal(BoxHeader& b)                  { blob_seal(&b, OPS_BOX); }
bool box_blob_ok(const BoxHeader& b)         { return blob_ok(&b, OPS_BOX); }
void cfgv2_seal(ConfigV2& c)                 { blob_seal(&c, OPS_CFG); }
bool cfgv2_blob_ok(const ConfigV2& c)        { return blob_ok(&c, OPS_CFG); }
void inventory_seal(Inventory& i)            { blob_seal(&i, OPS_INV); }
bool inventory_blob_ok(const Inventory& i)   { return blob_ok(&i, OPS_INV); }
void cooldowns_seal(CooldownTable& c)        { blob_seal(&c, OPS_CD); }
bool cooldowns_blob_ok(const CooldownTable& c) { return blob_ok(&c, OPS_CD); }
void custom_species_seal(CustomSpeciesRec& c)  { blob_seal(&c, OPS_CS); }
bool custom_species_blob_ok(const CustomSpeciesRec& c) { return blob_ok(&c, OPS_CS); }
void trade_seal(PendingTrade& t)             { blob_seal(&t, OPS_TRADE); }
bool trade_blob_ok(const PendingTrade& t)    { return blob_ok(&t, OPS_TRADE); }

bool schema_is_foreign_newer(uint8_t version) {
  return version > (uint8_t)SAVE_SCHEMA_VERSION;
}

// --- defaults ----------------------------------------------------------------
void pebble_clear(PebbleInstance& p) {
  memset(&p, 0, sizeof p);
  p.custom_sprite = PB_CUSTOM_SPRITE_NONE;
  pebble_seal(p);
}

void box_defaults(BoxHeader& b) {
  memset(&b, 0, sizeof b);
  b.active_slot     = BOX_ACTIVE_NONE;
  b.content_version = CONTENT_VERSION;
  b.next_id_counter = 1;
  b.protocol_version = PROTOCOL_VERSION;
  box_seal(b);
}

void cfgv2_defaults(ConfigV2& c) {
  memset(&c, 0, sizeof c);
  c.time_cal_state  = CAL_UNSET;
  c.brightness      = OLED_CONTRAST_DEFAULT;
  c.creator_idle_s  = 300;
  const char* tz = CFG_TZ_STRING;
  size_t i = 0;
  while (tz[i] != '\0' && i + 1 < (size_t)CFGV2_TZ_CAP) { c.tz[i] = tz[i]; ++i; }
  c.tz[i] = '\0';
  cfgv2_seal(c);
}

void inventory_defaults(Inventory& i) {
  memset(&i, 0, sizeof i);
  i.slots = INVENTORY_SLOTS;
  inventory_seal(i);
}

void cooldowns_defaults(CooldownTable& c) {
  memset(&c, 0, sizeof c);
  cooldowns_seal(c);
}

void trade_clear(PendingTrade& t) {
  memset(&t, 0, sizeof t);
  t.phase = TRADE_IDLE;
  trade_seal(t);
}

// =============================================================================
//  PART 2 - module state
// =============================================================================
static SaveClockFn s_now_ms    = nullptr;
static SaveClockFn s_now_epoch = nullptr;
static GameState*  s_state     = nullptr;

static uint32_t s_last_write_ms[BOX_SLOTS];
static bool     s_have_written[BOX_SLOTS];
static uint16_t s_pending_mask  = 0;      // slots whose forced write was deferred
static uint32_t s_lastseen      = 0;
static uint32_t s_lastseen_ms   = 0;
static bool     s_lastseen_sent = false;
static bool     s_migrated      = false;
static bool     s_landed        = false;   // the last save_pebble() reached flash
static uint32_t s_ckpt_epoch    = 0;       // wall clock of the last checkpoint
static bool     s_ckpt_known    = false;   // ... and whether we have looked yet

void save_set_clock(SaveClockFn now_ms, SaveClockFn now_epoch) {
  s_now_ms    = now_ms;
  s_now_epoch = now_epoch;
}

void save_bind(GameState& gs) { s_state = &gs; }

static uint32_t now_ms(void)    { return s_now_ms ? s_now_ms() : 0; }
static uint32_t now_epoch(void) { return s_now_epoch ? s_now_epoch() : 0; }

bool save_was_migrated(void)  { return s_migrated; }
uint32_t save_last_seen(void) { return s_lastseen; }

// =============================================================================
//  PART 3 - the pair primitive
//
//  A pair is two keys holding the same struct. The authoritative copy is the
//  one with the higher seq; a write always targets the OTHER one, so the copy
//  a reader would have chosen is never the copy a writer is touching.
// =============================================================================
struct PairStat {
  uint8_t  present;      // copies that exist at all
  uint8_t  good;         // copies that passed magic + CRC + version
  uint8_t  bad;          // copies that exist but did not pass
  uint8_t  foreign;      // copies that are intact but from a newer schema
  uint8_t  best_copy;    // 0/1, valid when good > 0
  uint32_t best_seq;
};

// Reads both copies of 'prefix' and leaves the best one in 'out'.
static void pair_load(KvPart part, const char* prefix, const BlobOps& o,
                      void* out, PairStat& st) {
  memset(&st, 0, sizeof st);
  st.best_copy = 0xFF;

  uint8_t tmp[SAVE_BLOB_MAX];
  for (uint8_t copy = 0; copy < 2; ++copy) {
    char key[KV_KEY_CAP];
    key_pair(prefix, copy, key);
    const int n = kv_get(part, key, tmp, o.size);
    if (n == 0) continue;                       // absent: not a fault
    st.present++;
    if (n != (int)o.size) { st.bad++; continue; }
    if (!blob_intact(tmp, o)) { st.bad++; continue; }
    // A HIGHER version is intact data this firmware does not understand; a
    // lower one is a migration's job, not this primitive's.
    if (tmp[o.ver_off] > o.version) { st.foreign++; continue; }
    if (tmp[o.ver_off] != o.version) { st.bad++; continue; }

    const uint32_t seq = (o.seq_off == NO_SEQ) ? 0u : u32_at(tmp, o.seq_off);
    if (st.good == 0 || seq > st.best_seq) {
      st.best_seq  = seq;
      st.best_copy = copy;
      memcpy(out, tmp, o.size);
    }
    st.good++;
  }
}

// Writes 'blob' into the copy a reader would NOT pick, with a seq one above the
// highest one already stored, then reads the bytes back and compares them.
static bool pair_write(KvPart part, const char* prefix, const BlobOps& o, void* blob) {
  uint8_t  tmp[SAVE_BLOB_MAX];
  bool     valid[2] = { false, false };
  uint32_t seq[2]   = { 0u, 0u };

  for (uint8_t copy = 0; copy < 2; ++copy) {
    char key[KV_KEY_CAP];
    key_pair(prefix, copy, key);
    if (kv_get(part, key, tmp, o.size) != (int)o.size) continue;
    if (!blob_ok(tmp, o)) continue;
    valid[copy] = true;
    seq[copy]   = (o.seq_off == NO_SEQ) ? 0u : u32_at(tmp, o.seq_off);
  }

  // Target: an unusable copy first, otherwise the older one. On a tie copy 1 is
  // the target, because pair_load() picks copy 0 when the seqs are equal - the
  // copy a reader would choose is never the copy a writer touches.
  uint8_t target;
  if (!valid[0])      target = 0;
  else if (!valid[1]) target = 1;
  else                target = (seq[0] < seq[1]) ? 0 : 1;

  uint32_t best = 0u;
  if (valid[0])                 best = seq[0];
  if (valid[1] && seq[1] > best) best = seq[1];

  if (o.seq_off != NO_SEQ) {
    const uint32_t next = best + 1u;
    memcpy((uint8_t*)blob + o.seq_off, &next, sizeof next);
  }
  blob_seal(blob, o);

  char key[KV_KEY_CAP];
  key_pair(prefix, target, key);
  if (!kv_put(part, key, blob, o.size)) return false;

  // Verify after write (plan 1.5.3): bytes that did not land are not a save.
  if (kv_get(part, key, tmp, o.size) != (int)o.size) return false;
  return memcmp(tmp, blob, o.size) == 0;
}

// A single-key blob: seal, write, read back, compare.
static bool single_write(KvPart part, const char* key, const BlobOps& o, void* blob) {
  blob_seal(blob, o);
  if (!kv_put(part, key, blob, o.size)) return false;
  uint8_t tmp[SAVE_BLOB_MAX];
  if (kv_get(part, key, tmp, o.size) != (int)o.size) return false;
  return memcmp(tmp, blob, o.size) == 0;
}

static bool single_load(KvPart part, const char* key, const BlobOps& o, void* out) {
  uint8_t tmp[SAVE_BLOB_MAX];
  if (kv_get(part, key, tmp, o.size) != (int)o.size) return false;
  if (!blob_ok(tmp, o)) return false;
  memcpy(out, tmp, o.size);
  return true;
}

// =============================================================================
//  PART 4 - writing
// =============================================================================
bool save_pebble_landed(void) { return s_landed; }

bool save_pebble(uint8_t slot, const PebbleInstance& p, bool force) {
  s_landed = false;
  if (slot >= BOX_SLOTS) return false;

  if (s_now_ms && s_have_written[slot]) {
    const uint32_t since = now_ms() - s_last_write_ms[slot];   // wrap-safe
    if (!force && since < SAVE_FULL_PERIOD_MS) return true;    // wear filter
    if (since < SAVE_MIN_GAP_MS && s_state) {
      // Deferred, never dropped: save_service() writes it as soon as the floor
      // has passed, from the bound state, which is where the caller's copy is.
      s_pending_mask |= (uint16_t)(1u << slot);
      return true;
    }
  }

  PebbleInstance blob = p;
  char prefix[KV_KEY_CAP];
  prefix[0] = 'p'; prefix[1] = 'b'; prefix[2] = (char)('0' + slot); prefix[3] = '\0';
  if (!pair_write(KV_MAIN, prefix, OPS_PEBBLE, &blob)) return false;

  s_last_write_ms[slot] = now_ms();
  s_have_written[slot]  = true;
  s_pending_mask &= (uint16_t)~(1u << slot);
  s_landed = true;
  return true;
}

// No filter, no deferral, no third answer. See save_manager.h for why the trade
// cannot use save_pebble() and why force=true was not widened to mean this.
bool save_pebble_now(uint8_t slot, const PebbleInstance& p) {
  s_landed = false;
  if (slot >= BOX_SLOTS) return false;

  PebbleInstance blob = p;
  char prefix[KV_KEY_CAP];
  prefix[0] = 'p'; prefix[1] = 'b'; prefix[2] = (char)('0' + slot); prefix[3] = '\0';
  if (!pair_write(KV_MAIN, prefix, OPS_PEBBLE, &blob)) return false;

  // The throttle's bookkeeping is updated exactly as save_pebble() updates it.
  // Clearing the pending bit COSTS ONE FEWER FLASH WRITE and nothing more: a
  // deferred flush that did fire would write s_state->pebbles[slot], which is
  // the same RAM this call just persisted, so the claim here is wear and not
  // correctness. Said plainly because the wider claim was tempting and false.
  s_last_write_ms[slot] = now_ms();
  s_have_written[slot]  = true;
  s_pending_mask &= (uint16_t)~(1u << slot);
  s_landed = true;
  return true;
}

void save_service(void) {
  if (!s_pending_mask || !s_state) return;
  for (uint8_t slot = 0; slot < BOX_SLOTS; ++slot) {
    if (!(s_pending_mask & (1u << slot))) continue;
    if (s_now_ms && (now_ms() - s_last_write_ms[slot]) < SAVE_MIN_GAP_MS) continue;
    s_pending_mask &= (uint16_t)~(1u << slot);
    save_pebble(slot, s_state->pebbles[slot], true);
  }
}

bool save_box_header(const BoxHeader& b) {
  BoxHeader blob = b;
  const uint32_t e = now_epoch();
  if (e >= NT_EPOCH_SANE_MIN) blob.saved_epoch = e;
  blob.content_version  = CONTENT_VERSION;
  blob.protocol_version = PROTOCOL_VERSION;
  return pair_write(KV_MAIN, KEY_BOX_PREFIX, OPS_BOX, &blob);
}

bool save_config(ConfigV2& c) {
  // Sealed IN THE CALLER'S STRUCT: the entry point watches cfg.crc16 to notice
  // that a setting changed and re-apply the ones that live outside the blob
  // (panel contrast). Sealing into a private copy froze that detector forever
  // in v1 (the retired storage.h, store_save_cfg).
  return pair_write(KV_MAIN, KEY_CFG_PREFIX, OPS_CFG, &c);
}

bool save_inventory(const Inventory& i) {
  Inventory blob = i;
  blob.slots = INVENTORY_SLOTS;
  return pair_write(KV_MAIN, KEY_INV_PREFIX, OPS_INV, &blob);
}

bool save_cooldowns(const CooldownTable& c) {
  CooldownTable blob = c;
  return pair_write(KV_MAIN, KEY_CD_PREFIX, OPS_CD, &blob);
}

bool save_trade_journal(const PendingTrade& t) {
  PendingTrade blob = t;
  return single_write(KV_MAIN, KEY_TRADE, OPS_TRADE, &blob);
}

bool save_custom_species(const CustomSpeciesRec& c) {
  if (c.slot >= CUSTOM_SPECIES_SLOTS) return false;
  CustomSpeciesRec blob = c;
  char key[KV_KEY_CAP];
  key_custom(c.slot, key);
  return single_write(KV_MAIN, key, OPS_CS, &blob);
}

bool save_load_custom_species(uint8_t slot, CustomSpeciesRec& out) {
  if (slot >= CUSTOM_SPECIES_SLOTS) return false;
  char key[KV_KEY_CAP];
  key_custom(slot, key);
  if (!single_load(KV_MAIN, key, OPS_CS, &out)) return false;
  return out.slot == slot;      // a record filed under the wrong key is not ours
}

void save_touch_lastseen(uint32_t epoch) {
  if (epoch >= NT_EPOCH_SANE_MIN && epoch > s_lastseen) s_lastseen = epoch;
  if (s_lastseen < NT_EPOCH_SANE_MIN) return;
  const uint32_t ms = now_ms();
  if (s_lastseen_sent && s_now_ms && (ms - s_lastseen_ms) < SAVE_LASTSEEN_PERIOD_MS) return;
  const uint64_t wire = (uint64_t)s_lastseen;      // key "t" is 8 B, as in v1
  if (!kv_put(KV_MAIN, KEY_LASTSEEN, &wire, sizeof wire)) return;
  s_lastseen_ms   = ms;
  s_lastseen_sent = true;
}

// =============================================================================
//  PART 5 - the checkpoint (KV_CKPT / partition "nvs2", decision D6)
// =============================================================================
bool save_checkpoint_all(void) {
  if (!s_state) return false;
  bool ok = true;

  BoxHeader box = s_state->box;
  ok = single_write(KV_CKPT, KEY_CK_BOX, OPS_BOX, &box) && ok;

  for (uint8_t slot = 0; slot < BOX_SLOTS; ++slot) {
    char key[KV_KEY_CAP];
    key_ck_pebble(slot, key);
    if (!(s_state->box.slot_mask & (1u << slot))) { kv_erase(KV_CKPT, key); continue; }
    PebbleInstance p = s_state->pebbles[slot];
    ok = single_write(KV_CKPT, key, OPS_PEBBLE, &p) && ok;
  }

  ConfigV2 cfg = s_state->cfg;
  ok = single_write(KV_CKPT, KEY_CK_CFG, OPS_CFG, &cfg) && ok;
  return ok;
}

bool save_checkpoint_service(uint32_t now_epoch, bool force) {
  if (!force) {
    if (now_epoch < NT_EPOCH_SANE_MIN) return false;
    if (!s_ckpt_known) {
      // What is already on nvs2 decides when the next one is due, so a unit
      // that reboots often does not rewrite the checkpoint on every boot.
      BoxHeader ck;
      s_ckpt_epoch = single_load(KV_CKPT, KEY_CK_BOX, OPS_BOX, &ck) ? ck.saved_epoch : 0u;
      s_ckpt_known = true;
    }
    if (s_ckpt_epoch != 0 && now_epoch < s_ckpt_epoch + SAVE_CKPT_PERIOD_S) return false;
  }
  if (!save_checkpoint_all()) return false;
  s_ckpt_known = true;
  if (now_epoch >= NT_EPOCH_SANE_MIN) s_ckpt_epoch = now_epoch;
  return true;
}

// Fills 'gs' from the checkpoint. False when there is no usable checkpoint.
static bool checkpoint_load(GameState& gs) {
  BoxHeader box;
  if (!single_load(KV_CKPT, KEY_CK_BOX, OPS_BOX, &box)) return false;

  gs.box = box;
  uint16_t mask = 0;
  for (uint8_t slot = 0; slot < BOX_SLOTS; ++slot) {
    char key[KV_KEY_CAP];
    key_ck_pebble(slot, key);
    PebbleInstance p;
    if (!single_load(KV_CKPT, key, OPS_PEBBLE, &p)) continue;
    if (pebble_is_empty(p)) continue;
    gs.pebbles[slot] = p;
    mask |= (uint16_t)(1u << slot);
  }
  gs.box.slot_mask = mask;                 // slot truth beats the header, always
  if (gs.box.active_slot >= BOX_SLOTS || !(mask & (1u << gs.box.active_slot))) {
    gs.box.active_slot = BOX_ACTIVE_NONE;
    for (uint8_t slot = 0; slot < BOX_SLOTS; ++slot) {
      if (mask & (1u << slot)) { gs.box.active_slot = slot; break; }
    }
  }
  box_seal(gs.box);

  ConfigV2 cfg;
  if (single_load(KV_CKPT, KEY_CK_CFG, OPS_CFG, &cfg)) gs.cfg = cfg;
  return true;
}

// =============================================================================
//  PART 6 - loading (plan 1.5.4)
// =============================================================================
static void state_defaults(GameState& gs) {
  for (uint8_t slot = 0; slot < BOX_SLOTS; ++slot) pebble_clear(gs.pebbles[slot]);
  box_defaults(gs.box);
  cfgv2_defaults(gs.cfg);
  inventory_defaults(gs.inv);
  cooldowns_defaults(gs.cds);
  trade_clear(gs.trade);
}

// A first pass that only looks for a save from a NEWER firmware. It runs before
// anything is written, because LOAD_FOREIGN_NEWER must leave flash untouched.
static bool scan_foreign(void) {
  struct Scan { const char* prefix; const BlobOps* ops; };
  const Scan scans[] = {
    { KEY_BOX_PREFIX, &OPS_BOX },
    { KEY_CFG_PREFIX, &OPS_CFG },
    { KEY_INV_PREFIX, &OPS_INV },
    { KEY_CD_PREFIX,  &OPS_CD  },
  };
  uint8_t tmp[SAVE_BLOB_MAX];
  for (size_t i = 0; i < NT_ARRAY_LEN(scans); ++i) {
    for (uint8_t copy = 0; copy < 2; ++copy) {
      char key[KV_KEY_CAP];
      key_pair(scans[i].prefix, copy, key);
      if (kv_get(KV_MAIN, key, tmp, scans[i].ops->size) != (int)scans[i].ops->size) continue;
      if (!blob_intact(tmp, *scans[i].ops)) continue;
      if (tmp[scans[i].ops->ver_off] > scans[i].ops->version) return true;
    }
  }
  for (uint8_t slot = 0; slot < BOX_SLOTS; ++slot) {
    for (uint8_t copy = 0; copy < 2; ++copy) {
      char key[KV_KEY_CAP];
      key_pebble(slot, copy, key);
      if (kv_get(KV_MAIN, key, tmp, OPS_PEBBLE.size) != (int)OPS_PEBBLE.size) continue;
      if (!blob_intact(tmp, OPS_PEBBLE)) continue;
      if (tmp[OPS_PEBBLE.ver_off] > OPS_PEBBLE.version) return true;
    }
  }
  return false;
}

// Commits a whole state to KV_MAIN. Used after a migration and after a
// checkpoint recovery, both of which produce a state that flash does not hold.
static bool commit_all(GameState& gs) {
  bool ok = true;
  for (uint8_t slot = 0; slot < BOX_SLOTS; ++slot) {
    if (!(gs.box.slot_mask & (1u << slot))) continue;
    PebbleInstance p = gs.pebbles[slot];
    char prefix[KV_KEY_CAP];
    prefix[0] = 'p'; prefix[1] = 'b'; prefix[2] = (char)('0' + slot); prefix[3] = '\0';
    ok = pair_write(KV_MAIN, prefix, OPS_PEBBLE, &p) && ok;
  }
  ok = save_box_header(gs.box) && ok;
  ok = save_config(gs.cfg) && ok;
  ok = save_inventory(gs.inv) && ok;
  ok = save_cooldowns(gs.cds) && ok;
  // THE TRADE JOURNAL, AND IT WAS MISSING (found by P7-C4's survey, fixed here).
  // This function rewrites a whole state that flash does not hold - after a
  // migration and after "Recuperar" on the SAVE ERROR screen - and it wrote
  // five of the six blobs. save_restore_checkpoint() calls state_defaults(),
  // which sets tmp.trade to IDLE in RAM, so before this line RAM said IDLE and
  // FLASH still held the old "tr" record: the next boot read the flash copy and
  // resolved a trade against a Box restored from a checkpoint that predates it.
  // The v1 migration branch had the same hole from the other direction.
  //
  // IT WRITES A SEALED IDLE RECORD RATHER THAN kv_erase()ing THE KEY, on
  // purpose: an absent key and a rotted key look identical to single_load()
  // (both fail), and what the resolver wants to know is "there is nothing
  // pending", which only a record can say.
  ok = save_trade_journal(gs.trade) && ok;
  return ok;
}

bool save_restore_checkpoint(GameState& gs) {
  // Static, not a local: a GameState is 1,936 B and this runs on the loop
  // task's stack. Nothing here re-enters.
  static GameState tmp;
  state_defaults(tmp);
  if (!checkpoint_load(tmp)) return false;      // nothing written, nothing lost
  gs = tmp;
  save_bind(gs);
  s_pending_mask = 0;
  for (uint8_t slot = 0; slot < BOX_SLOTS; ++slot) s_have_written[slot] = false;
  return commit_all(gs);
}

// -----------------------------------------------------------------------------
//  THE SAVE PATH IS THE SHARED VALIDATOR'S SECOND LIVE CALL SITE (spec 15).
//
//  IT QUARANTINES. It does not refuse the Box and it does not repair one byte.
//  Refusing would brick a device on a content-pack change, because
//  VR_UNKNOWN_SPECIES is exactly what an older save legitimately produces; and
//  repairing is the failure spec 15's first sentence is written against, so
//  game/validate.h takes its Pebble CONST and this file could not repair even
//  if it wanted to. A quarantined Pebble is loaded, drawn and reported with its
//  named VReject, and P7 must keep it out of a battle and out of a trade.
//
//  It runs on EVERY load outcome, after migration and after a checkpoint
//  restore, because those two paths build Pebbles too. On LOAD_CORRUPT and
//  LOAD_FOREIGN_NEWER the Box is still at its defaults and every slot is empty,
//  so the scan is a no-op rather than a special case.
// -----------------------------------------------------------------------------
static uint16_t s_quarantine_mask = 0;
static uint8_t  s_quarantine_why[BOX_SLOTS] = { 0 };

static void quarantine_scan(const GameState& gs) {
  s_quarantine_mask = 0;
  for (uint8_t slot = 0; slot < BOX_SLOTS; ++slot) {
    s_quarantine_why[slot] = (uint8_t)VR_OK;
    if (pebble_is_empty(gs.pebbles[slot])) continue;
    const VReject r = validate_pebble(gs.pebbles[slot]);
    if (r != VR_OK) {
      s_quarantine_mask     |= (uint16_t)(1u << slot);
      s_quarantine_why[slot] = (uint8_t)r;
    }
  }
}

uint16_t save_quarantine_mask(void) { return s_quarantine_mask; }

VReject save_quarantine_reason(uint8_t slot) {
  if (slot >= (uint8_t)BOX_SLOTS) return VR_OK;
  return (VReject)s_quarantine_why[slot];
}

// -----------------------------------------------------------------------------
//  THE CREATOR SPECIES REGISTRY, REBUILT ON EVERY LOAD (P8-C3).
//
//  IT MUST RUN BEFORE quarantine_scan(), and that ordering is the whole point
//  of the function. A creator Pebble carries species_id 200..209, which
//  species_get() resolves through game/species_custom.cpp - so a scan that ran
//  first would answer VR_UNKNOWN_SPECIES for every custom Pebble in the Box and
//  quarantine the user's own creature on the first power cycle after it was
//  made. That defect was already waiting in the tree before this chunk: the
//  resolver simply did not exist.
//
//  A RECORD THAT DOES NOT VALIDATE LEAVES ITS SLOT EMPTY, which is deliberate
//  and is why csp_install() returns a bool nobody has to check here. The
//  consequence is stated rather than hidden: the Pebble that pointed at that
//  slot is then quarantined by NAME (VR_UNKNOWN_SPECIES) instead of being
//  resolved to a species whose stats survived a CRC and nothing else.
//  save_manager.h's own policy line - "a custom species is content, not state,
//  and a bad CRC costs a sprite rather than a Pebble" - is about the CRC half;
//  this is the rules half, and a species with a 400-point stat total is not a
//  sprite problem.
// -----------------------------------------------------------------------------
static void custom_species_install_all(void) {
  csp_reset();                      // also binds the resolver into species_get()
  for (uint8_t slot = 0; slot < (uint8_t)CUSTOM_SPECIES_SLOTS; ++slot) {
    CustomSpeciesRec rec;
    if (!save_load_custom_species(slot, rec)) continue;   // absent or rotten
    (void)csp_install(rec);                               // refused -> stays empty
  }
}

static LoadResult load_all_inner(GameState& gs) {
  state_defaults(gs);
  save_bind(gs);
  s_migrated      = false;
  s_pending_mask  = 0;
  s_lastseen_sent = false;
  for (uint8_t slot = 0; slot < BOX_SLOTS; ++slot) s_have_written[slot] = false;

  if (scan_foreign()) return LOAD_FOREIGN_NEWER;

  BoxHeader box;
  PairStat  bst;
  pair_load(KV_MAIN, KEY_BOX_PREFIX, OPS_BOX, &box, bst);

  // --- nothing usable in KV_MAIN -------------------------------------------
  if (bst.good == 0) {
    if (bst.bad > 0) return LOAD_CORRUPT;         // both copies rotten: ask the user
    if (migrate_v1_present()) {
      GameState tmp;
      if (migrate_run(SAVE_SCHEMA_VERSION_V1, tmp) != MIGRATE_OK) return LOAD_CORRUPT;
      gs = tmp;
      commit_all(gs);
      kv_erase(KV_MAIN, KEY_V1_SAVE);
      kv_erase(KV_MAIN, KEY_V1_CFG);
      kv_erase(KV_MAIN, KEY_V1_EGG);
      kv_erase(KV_MAIN, KEY_V1_ANCESTORS);
      s_migrated = true;
      return LOAD_MIGRATED;
    }
    if (checkpoint_load(gs)) {
      commit_all(gs);
      return LOAD_RECOVERED_CKPT;
    }
    return LOAD_FRESH;
  }

  // --- the Box is readable --------------------------------------------------
  gs.box = box;
  uint8_t repaired = bst.bad;                    // one copy served for the other

  uint16_t truth_mask = 0;
  bool active_lost = false;
  for (uint8_t slot = 0; slot < BOX_SLOTS; ++slot) {
    char prefix[KV_KEY_CAP];
    prefix[0] = 'p'; prefix[1] = 'b'; prefix[2] = (char)('0' + slot); prefix[3] = '\0';
    PebbleInstance p;
    PairStat pst;
    pair_load(KV_MAIN, prefix, OPS_PEBBLE, &p, pst);
    if (pst.good > 0 && !pebble_is_empty(p)) {
      gs.pebbles[slot] = p;
      truth_mask |= (uint16_t)(1u << slot);
      if (pst.bad > 0) repaired++;
    } else if (pst.bad > 0) {
      // Both copies of an occupied slot are unreadable. Losing the ACTIVE
      // Pebble is what SAVE ERROR exists for; losing a stored one is a repair.
      if (slot == gs.box.active_slot) active_lost = true;
      repaired++;
    }
  }
  if (active_lost) return LOAD_CORRUPT;

  // Header/slot self-heal: the slot blobs are the truth, the mask is a cache.
  const bool mask_healed = (truth_mask != gs.box.slot_mask);
  gs.box.slot_mask = truth_mask;
  bool active_healed = false;
  if (gs.box.active_slot != BOX_ACTIVE_NONE &&
      (gs.box.active_slot >= BOX_SLOTS || !(truth_mask & (1u << gs.box.active_slot)))) {
    gs.box.active_slot = BOX_ACTIVE_NONE;
    for (uint8_t slot = 0; slot < BOX_SLOTS; ++slot) {
      if (truth_mask & (1u << slot)) { gs.box.active_slot = slot; break; }
    }
    active_healed = true;
  }

  // --- the rest of the blobs: absent means defaults, never an error ---------
  ConfigV2 cfg; PairStat cst;
  pair_load(KV_MAIN, KEY_CFG_PREFIX, OPS_CFG, &cfg, cst);
  if (cst.good > 0) gs.cfg = cfg;
  repaired += cst.bad;

  Inventory inv; PairStat ist;
  pair_load(KV_MAIN, KEY_INV_PREFIX, OPS_INV, &inv, ist);
  if (ist.good > 0) gs.inv = inv;
  repaired += ist.bad;

  CooldownTable cds; PairStat dst;
  pair_load(KV_MAIN, KEY_CD_PREFIX, OPS_CD, &cds, dst);
  if (dst.good > 0) gs.cds = cds;
  repaired += dst.bad;

  PendingTrade tr;
  if (single_load(KV_MAIN, KEY_TRADE, OPS_TRADE, &tr)) gs.trade = tr;

  uint64_t seen = 0;
  if (kv_get(KV_MAIN, KEY_LASTSEEN, &seen, sizeof seen) == (int)sizeof seen) {
    if ((uint32_t)seen > s_lastseen) s_lastseen = (uint32_t)seen;
  }
  if (gs.box.saved_epoch > s_lastseen) s_lastseen = gs.box.saved_epoch;

  // Repairs are written only now, once the outcome is known to be recoverable.
  if (repaired || mask_healed || active_healed) {
    box_seal(gs.box);
    save_box_header(gs.box);
    for (uint8_t slot = 0; slot < BOX_SLOTS; ++slot) {
      if (truth_mask & (1u << slot)) {
        PebbleInstance p = gs.pebbles[slot];
        char prefix[KV_KEY_CAP];
        prefix[0] = 'p'; prefix[1] = 'b'; prefix[2] = (char)('0' + slot); prefix[3] = '\0';
        pair_write(KV_MAIN, prefix, OPS_PEBBLE, &p);
      }
    }
    if (cst.bad) save_config(gs.cfg);
    if (ist.bad) save_inventory(gs.inv);
    if (dst.bad) save_cooldowns(gs.cds);
    return LOAD_RECOVERED_PAIR;
  }
  return LOAD_OK;
}

// The whole pipeline of plan 1.5.4, with runtime validation as its last stage.
// The scan runs on every outcome so no return path can forget it.
LoadResult save_load_all(GameState& gs) {
  const LoadResult r = load_all_inner(gs);
  // ORDER IS LOAD -> RESOLVE -> SCAN, and the middle step is P8-C3's. See
  // custom_species_install_all(): a scan that runs first quarantines every
  // creator Pebble the device made.
  custom_species_install_all();
  quarantine_scan(gs);
  return r;
}

// =============================================================================
//  PART 7 - factory reset
// =============================================================================
bool save_factory_reset(void) {
  const bool a = kv_wipe(KV_MAIN);
  const bool b = kv_wipe(KV_CKPT);
  // The cs* records went with the partition, so the registry must go with them:
  // an id that still resolved after a reset would hand the fresh save a species
  // whose record no longer exists.
  csp_reset();
  for (uint8_t slot = 0; slot < BOX_SLOTS; ++slot) s_have_written[slot] = false;
  s_pending_mask  = 0;
  s_lastseen      = 0;
  s_lastseen_sent = false;
  s_migrated      = false;
  s_ckpt_epoch    = 0;
  s_ckpt_known    = true;      // the partition was just wiped: nothing to read
  return a && b;
}
