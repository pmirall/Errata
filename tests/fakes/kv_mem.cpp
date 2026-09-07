// =============================================================================
//  Pebblebol host tests - fakes/kv_mem.cpp
//  See kv_mem.h. Fixed-size tables, no allocation: the whole point is that a
//  test failure is a logic bug in the save manager, never in this fake.
// =============================================================================
#include "kv_mem.h"

#include <stdio.h>
#include <string.h>

#define KVM_MAX_KEYS    64
#define KVM_MAX_VALUE   512

struct KvmEntry {
  char    key[16];
  uint8_t data[KVM_MAX_VALUE];
  size_t  len;
  bool    used;
};

struct KvmPartition {
  KvmEntry entries[KVM_MAX_KEYS];
  bool     healthy;   // the partition opened (kv_mem_set_healthy)
  // A WRITE HAS FAILED SINCE BOOT. hardware/kv_nvs.cpp's s_wfail[], modelled
  // here since the final review: it is set by any failed kv_put and cleared
  // ONLY by a successful kv_wipe(), which on the device is the only way back.
  // persistence/kv_store.h has always documented kv_healthy() as both halves;
  // this fake implemented one, so a test could not tell the two apart.
  bool     wfail;
};

static KvmPartition s_part[KV_PART_COUNT];
static bool         s_fail_next_put = false;
static bool         s_countdown_armed = false;
static uint32_t     s_puts_until_death = 0;
static bool         s_dead            = false;
static uint32_t     s_puts          = 0;
static uint32_t     s_put_attempts  = 0;
static size_t       s_longest_key   = 0;

static void note_key(const char* key) {
  if (!key) return;
  const size_t n = strlen(key);
  if (n > s_longest_key) s_longest_key = n;
}

static KvmEntry* find(KvPart part, const char* key) {
  if (part >= KV_PART_COUNT || !key || key[0] == '\0') return nullptr;
  KvmPartition& p = s_part[part];
  for (size_t i = 0; i < KVM_MAX_KEYS; ++i) {
    if (p.entries[i].used && strcmp(p.entries[i].key, key) == 0) return &p.entries[i];
  }
  return nullptr;
}

static KvmEntry* allocate(KvPart part, const char* key) {
  KvmPartition& p = s_part[part];
  for (size_t i = 0; i < KVM_MAX_KEYS; ++i) {
    if (!p.entries[i].used) {
      KvmEntry& e = p.entries[i];
      e.used = true;
      snprintf(e.key, sizeof e.key, "%s", key);
      e.len = 0;
      return &e;
    }
  }
  return nullptr;
}

// A failed write is sticky for the rest of the boot, exactly as it is on the
// device (hardware/kv_nvs.cpp kv_fail -> s_wfail[part]).
static void note_write_fail(KvPart part) {
  if (part < KV_PART_COUNT) s_part[part].wfail = true;
}

// --- persistence/kv_store.h --------------------------------------------------
int kv_get(KvPart part, const char* key, void* buf, size_t cap) {
  note_key(key);
  if (part >= KV_PART_COUNT || !key || key[0] == '\0' || !buf) return -1;
  // KV_CLOSED, MATCHING hardware/kv_nvs.cpp SINCE THE FINAL REVIEW. Answering
  // -1 here made a partition that never opened read as a DAMAGED KEY, and
  // save_manager.cpp's pair_load() counts a negative as present-and-bad - so
  // both copies of the Box came back rotten and load_all_inner() returned
  // LOAD_CORRUPT on a board whose save was never touched. This fake is where
  // that branch has to be drivable from (kv_mem_set_healthy), so it has to
  // answer the same thing the device answers.
  if (!s_part[part].healthy) return KV_CLOSED;
  const KvmEntry* e = find(part, key);
  if (!e) return 0;                       // absent is not a fault
  if (e->len > cap) return -1;            // never truncate a blob into a CRC check
  memcpy(buf, e->data, e->len);
  return (int)e->len;
}

bool kv_put(KvPart part, const char* key, const void* buf, size_t n) {
  note_key(key);
  // ARGUMENTS FIRST, FAULT SECOND, MATCHING hardware/kv_nvs.cpp SINCE THE FINAL
  // REVIEW. This block used to sit above the validation, so an injected fault
  // was CONSUMED BY A CALL THE DEVICE WOULD HAVE REFUSED WITHOUT TOUCHING
  // FLASH: with fail_next_put armed, a kv_put with an empty key returned false
  // and ate the injection, and the next real write succeeded. It also meant
  // s_put_attempts counted calls no device would have made.
  if (part >= KV_PART_COUNT || !key || key[0] == '\0' || !buf) return false;
  if (n == 0 || n > KVM_MAX_VALUE) return false;
  if (strlen(key) > 15) return false;      // NVS_KEY_NAME_MAX_SIZE - 1

  s_put_attempts++;
  if (s_fail_next_put) { s_fail_next_put = false; note_write_fail(part); return false; }
  // The power cut. Checked BEFORE the write and never re-armed: a dead store
  // stays dead until the test plugs the device back in.
  if (s_dead) return false;
  if (s_countdown_armed && s_puts_until_death == 0u) { s_dead = true; return false; }
  if (!s_part[part].healthy) { note_write_fail(part); return false; }
  KvmEntry* e = find(part, key);
  if (!e) e = allocate(part, key);
  if (!e) { note_write_fail(part); return false; }
  memcpy(e->data, buf, n);
  e->len = n;
  s_puts++;
  if (s_countdown_armed && s_puts_until_death > 0u) s_puts_until_death--;
  return true;
}

bool kv_erase(KvPart part, const char* key) {
  note_key(key);
  // key[0] == '\0' REFUSED SINCE THE FINAL REVIEW, matching kv_nvs.cpp. The
  // fake guarded only `!key`, so an empty key fell through find() (which
  // answers nullptr for it) to the "already absent counts as erased" tail and
  // returned true where the device returns false. Reachable in principle:
  // save_manager.cpp's key builder returns an EMPTY string on overflow, by
  // design, rather than a colliding one.
  if (part >= KV_PART_COUNT || !key || key[0] == '\0') return false;
  if (!s_part[part].healthy) return false;
  // A DEAD STORE ERASES NOTHING EITHER. Without this line a kill sweep's
  // "power cut" still let save_checkpoint_all() erase unoccupied checkpoint
  // slots after the device was supposed to be gone - a fault model that
  // refuses writes and permits deletes is not a power cut. Found in the P7-C6
  // review of the trade's atomicity claim, which is measured on this fake.
  if (s_dead) return false;
  KvmEntry* e = find(part, key);
  if (e) { e->used = false; e->len = 0; e->key[0] = '\0'; }
  return true;                             // already absent counts as erased
}

bool kv_wipe(KvPart part) {
  if (part >= KV_PART_COUNT) return false;
  // *** AND A DEAD STORE WIPES NOTHING, WHICH IS THE HALF P7-C6 MISSED. ***
  // The paragraph in kv_erase() above says "a fault model that refuses writes
  // and permits deletes is not a power cut". kv_wipe IS a delete - the largest
  // one there is - and the model went on permitting it: after
  // kv_mem_fail_after_n_puts(0) this fake refused the write, refused the erase,
  // and then destroyed the whole partition and reported success. On a device a
  // power cut has ended the CPU, so kv_wipe() is not reached at all.
  //
  // It is not merely tidiness. save_factory_reset() is kv_wipe()'s only caller,
  // and "pull the power during Reset de fabrica" is the natural companion to
  // README section 2's factory-reset item: that sweep would have come back green
  // against a fake that wiped after the device was gone.
  if (s_dead) return false;
  if (!s_part[part].healthy) return false;
  for (size_t i = 0; i < KVM_MAX_KEYS; ++i) s_part[part].entries[i].used = false;
  // A SUCCESSFUL WIPE IS THE RECOVERY FROM A WRITE FAILURE, exactly as it is on
  // the device: kv_nvs.cpp clears s_wfail[part] here and nowhere else.
  s_part[part].wfail = false;
  return true;
}

bool kv_healthy(KvPart part) {
  if (part >= KV_PART_COUNT) return false;
  // BOTH HALVES OF THE CONTRACT SINCE THE FINAL REVIEW. persistence/kv_store.h
  // says "False when the partition could not be opened OR A WRITE HAS FAILED
  // SINCE BOOT", and hardware/kv_nvs.cpp honours both - kv_healthy() is
  // `part_ok(part) && !s_wfail[part]`, s_wfail is set by any short putBytes and
  // is cleared ONLY by a successful kv_wipe(). This fake used to return a bare
  // test knob that no failure ever touched, so it could never go unhealthy when
  // the device would and could never be wiped back to health, which on the
  // device is the only way back. Both halves were inverted at once.
  return s_part[part].healthy && !s_part[part].wfail;
}

// --- the test knobs ----------------------------------------------------------
void kv_mem_reset(void) {
  memset(s_part, 0, sizeof s_part);
  for (size_t i = 0; i < KV_PART_COUNT; ++i) s_part[i].healthy = true;
  s_fail_next_put   = false;
  s_countdown_armed = false;
  s_puts_until_death = 0;
  s_dead            = false;
  s_puts            = 0;
  s_put_attempts    = 0;
  s_longest_key     = 0;
}

void kv_mem_fail_next_put(void) { s_fail_next_put = true; }

void kv_mem_fail_after_n_puts(uint32_t n)
{
  s_countdown_armed  = true;
  s_puts_until_death = n;
  s_dead             = false;
}

void kv_mem_power_restore(void)
{
  s_countdown_armed  = false;
  s_puts_until_death = 0;
  s_dead             = false;
}

bool kv_mem_corrupt(const char* key, size_t byte_index) {
  for (uint8_t part = 0; part < KV_PART_COUNT; ++part) {
    KvmEntry* e = find((KvPart)part, key);
    if (!e) continue;
    if (byte_index >= e->len) return false;
    e->data[byte_index] = (uint8_t)~e->data[byte_index];
    return true;
  }
  return false;
}

void kv_mem_wipe_partition(KvPart part) {
  if (part >= KV_PART_COUNT) return;
  for (size_t i = 0; i < KVM_MAX_KEYS; ++i) s_part[part].entries[i].used = false;
}

void kv_mem_set_healthy(KvPart part, bool healthy) {
  if (part < KV_PART_COUNT) { s_part[part].healthy = healthy; s_part[part].wfail = false; }
}

uint32_t kv_mem_puts(void)         { return s_puts; }
uint32_t kv_mem_put_attempts(void) { return s_put_attempts; }
size_t   kv_mem_longest_key(void)  { return s_longest_key; }

bool kv_mem_exists(KvPart part, const char* key) { return find(part, key) != nullptr; }

int kv_mem_size(KvPart part, const char* key) {
  const KvmEntry* e = find(part, key);
  return e ? (int)e->len : -1;
}

uint16_t kv_mem_key_count(KvPart part) {
  if (part >= KV_PART_COUNT) return 0;
  uint16_t n = 0;
  for (size_t i = 0; i < KVM_MAX_KEYS; ++i) if (s_part[part].entries[i].used) n++;
  return n;
}
