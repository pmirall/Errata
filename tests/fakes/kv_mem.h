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

// THE POWER CUT AT AN ARBITRARY DEPTH (P7-C4). After `n` further SUCCESSFUL
// puts, every put fails and writes nothing, for ever, until this is called
// again or kv_mem_reset() runs. n == 0 means "the very next put fails".
//
// kv_mem_fail_next_put() cannot express this and that is why it exists: it is
// one-shot, so a test built on it can only ever cut at a point it already knew
// the index of. The trade journal's whole claim is that a cut ANYWHERE in the
// sequence leaves the pair invariant intact, and the only honest way to say
// that is to sweep k over every put the sequence performs - which needs a knob
// that counts. A sweep is a test that can fail; three hand-picked cut points
// are three tests that happen to pass.
//
// IT IS A DEAD DEVICE AND NOT A FLAKY ONE: once armed and reached, the store
// stays dead, because that is what a power cut is. The test reboots by calling
// kv_mem_power_restore() and then re-running the load path.
//
// A DEAD STORE REFUSES ERASES TOO, since the P7-C6 exit. It did not, and the
// gap mattered: save_checkpoint_all() erases unoccupied checkpoint slots, so a
// swept cut point could still mutate nvs2 AFTER the device was supposed to be
// gone. A fault model that refuses writes and permits deletes is not a power
// cut, and the whole of the trade's atomicity claim is measured on this fake.
void kv_mem_fail_after_n_puts(uint32_t n);

// Un-does the dead store above without touching a byte of its contents - the
// device is plugged back in and reads exactly what survived.
void kv_mem_power_restore(void);

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
