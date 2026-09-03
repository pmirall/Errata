// =============================================================================
//  PEBBLEBOL - persistence/kv_store.h
//  The whole key/value seam between the save policy and the flash.
//  Plan section 1.4. Five functions, two partitions, no game rules.
//
//  WHY TWO PARTITIONS (decision D6, audit section 5). Arduino's initArduino()
//  erases the ENTIRE default "nvs" partition when esp_nvs_flash_init() returns
//  ESP_ERR_NVS_NO_FREE_PAGES or ESP_ERR_NVS_NEW_VERSION_FOUND - before setup()
//  ever runs, so no amount of care in this firmware can prevent it. KV_CKPT is
//  a second, private partition ("nvs2") that the core never touches; the
//  checkpoint written there is what turns that erase from "the pet is gone"
//  into LOAD_RECOVERED_CKPT.
//
//  IMPLEMENTATIONS. Exactly one per build:
//    device  hardware/kv_nvs.cpp  - Preferences, namespace "pbbl" (D3)
//    host    tests/fakes/kv_mem.cpp - RAM, plus the fault injection the
//                                     persistence tests need
//  Nothing else in the tree may open Preferences (plan 1.3 rule 2).
//
//  CONTRACT. Keys are ASCII and <= 15 characters (persistence/save_schema.h
//  spells every one of them). A get never writes; a put is atomic per key at
//  the NVS level, which is why the pair-and-seq discipline in save_manager.h
//  is about firmware bugs and bit rot rather than torn writes.
// =============================================================================
#ifndef PB_KV_STORE_H
#define PB_KV_STORE_H

#include <stdint.h>
#include <stddef.h>

enum KvPart : uint8_t {
  KV_MAIN = 0,        // partition "nvs",  namespace "pbbl" - the live save
  KV_CKPT,            // partition "nvs2", namespace "pbbl" - the checkpoint
  KV_PART_COUNT
};

// Reads at most 'cap' bytes of 'key' into 'buf'.
// Returns the number of bytes read, 0 when the key is absent, and a negative
// value on a store error (partition closed, read failure). A stored value
// LARGER than 'cap' is an error, not a truncation: a short read of a blob is
// indistinguishable from a corrupt one and must never be handed to a CRC check.
int  kv_get(KvPart part, const char* key, void* buf, size_t cap);

// Writes exactly 'n' bytes. Returns false on any short or failed write; the
// caller (save_manager) verifies the bytes back before treating them as
// authoritative.
bool kv_put(KvPart part, const char* key, const void* buf, size_t n);

// Removes one key. Returns true when the key is gone afterwards, including
// when it was already absent.
bool kv_erase(KvPart part, const char* key);

// Clears the whole namespace in that partition. Factory reset only.
bool kv_wipe(KvPart part);

// False when the partition could not be opened or a write has failed since
// boot. The UI shows ERR_NVS; the game still runs, RAM-only.
bool kv_healthy(KvPart part);

#endif // PB_KV_STORE_H
