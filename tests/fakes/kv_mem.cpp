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
  bool     healthy;
};

static KvmPartition s_part[KV_PART_COUNT];
static bool         s_fail_next_put = false;
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

// --- persistence/kv_store.h --------------------------------------------------
int kv_get(KvPart part, const char* key, void* buf, size_t cap) {
  note_key(key);
  if (part >= KV_PART_COUNT || !key || key[0] == '\0' || !buf) return -1;
  if (!s_part[part].healthy) return -1;
  const KvmEntry* e = find(part, key);
  if (!e) return 0;                       // absent is not a fault
  if (e->len > cap) return -1;            // never truncate a blob into a CRC check
  memcpy(buf, e->data, e->len);
  return (int)e->len;
}

bool kv_put(KvPart part, const char* key, const void* buf, size_t n) {
  note_key(key);
  s_put_attempts++;
  if (s_fail_next_put) { s_fail_next_put = false; return false; }
  if (part >= KV_PART_COUNT || !key || key[0] == '\0' || !buf) return false;
  if (!s_part[part].healthy) return false;
  if (n == 0 || n > KVM_MAX_VALUE) return false;
  if (strlen(key) > 15) return false;      // NVS_KEY_NAME_MAX_SIZE - 1
  KvmEntry* e = find(part, key);
  if (!e) e = allocate(part, key);
  if (!e) return false;
  memcpy(e->data, buf, n);
  e->len = n;
  s_puts++;
  return true;
}

bool kv_erase(KvPart part, const char* key) {
  note_key(key);
  if (part >= KV_PART_COUNT || !key) return false;
  if (!s_part[part].healthy) return false;
  KvmEntry* e = find(part, key);
  if (e) { e->used = false; e->len = 0; e->key[0] = '\0'; }
  return true;                             // already absent counts as erased
}

bool kv_wipe(KvPart part) {
  if (part >= KV_PART_COUNT) return false;
  if (!s_part[part].healthy) return false;
  for (size_t i = 0; i < KVM_MAX_KEYS; ++i) s_part[part].entries[i].used = false;
  return true;
}

bool kv_healthy(KvPart part) {
  if (part >= KV_PART_COUNT) return false;
  return s_part[part].healthy;
}

// --- the test knobs ----------------------------------------------------------
void kv_mem_reset(void) {
  memset(s_part, 0, sizeof s_part);
  for (size_t i = 0; i < KV_PART_COUNT; ++i) s_part[i].healthy = true;
  s_fail_next_put = false;
  s_puts          = 0;
  s_put_attempts  = 0;
  s_longest_key   = 0;
}

void kv_mem_fail_next_put(void) { s_fail_next_put = true; }

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
  if (part < KV_PART_COUNT) s_part[part].healthy = healthy;
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
