// =============================================================================
//  Pebblebol host tests - fakes/kv_mem.h
//  The host implementation of persistence/kv_store.h: two RAM partitions and
//  the fault injection the persistence tests need (plan section 1.4).
//
//  Everything a real NVS can do to a save - a key that vanishes, a write that
//  fails halfway through a transaction, a bit that rots in place, a whole
//  partition erased by the Arduino core before setup() runs - has a knob here,
//  because a recovery path nobody can provoke is a recovery path nobody has
//  tested.
// =============================================================================
#ifndef PB_KV_MEM_H
#define PB_KV_MEM_H

#include <stdint.h>
#include <stddef.h>

#include "persistence/kv_store.h"

// Empties both partitions, clears every injected fault and zeroes the counters.
void kv_mem_reset(void);

// The next kv_put() - whatever key, whatever partition - fails and writes
// nothing. One-shot: it re-arms only when called again.
void kv_mem_fail_next_put(void);

// Flips every bit of one byte of a stored value. The key is looked up in both
// partitions (the key names are disjoint by construction, save_schema.h
// section 9). Returns false when the key does not exist or the byte is past
// the end of the value, so a test cannot silently corrupt nothing.
bool kv_mem_corrupt(const char* key, size_t byte_index);

// Erases a whole partition, the way initArduino() erases "nvs" (audit 5).
void kv_mem_wipe_partition(KvPart part);

// Marks a partition unopenable, so kv_healthy() is false and every get/put on
// it fails - the "NVS would not open" branch.
void kv_mem_set_healthy(KvPart part, bool healthy);

// Counters and inspection for the tests.
uint32_t kv_mem_puts(void);            // successful kv_put() calls since reset
uint32_t kv_mem_put_attempts(void);    // including the ones that failed
bool     kv_mem_exists(KvPart part, const char* key);
int      kv_mem_size(KvPart part, const char* key);   // -1 when absent
uint16_t kv_mem_key_count(KvPart part);

// The longest key ever handed to kv_put()/kv_get() since the reset. The test
// asserts it stays within NVS's 15-character limit.
size_t   kv_mem_longest_key(void);

#endif  // PB_KV_MEM_H
