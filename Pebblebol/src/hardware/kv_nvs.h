// =============================================================================
//  PEBBLEBOL - hardware/kv_nvs.h
//  The DEVICE half of persistence/kv_store.h: Preferences, two partitions, and
//  the handful of facts only a real NVS can report (canary, sticky error bits,
//  write-failure count). The pure seam itself is kv_store.h; nothing outside
//  this translation unit may open a Preferences handle (plan 1.3 rule 2).
//
//  NAMESPACE "pbbl" (decision D3). The v1 firmware used "notta"; a Pebblebol
//  unit never writes there again. kv_begin() performs a ONE-SHOT import of a
//  legacy save - the raw v1 blobs are copied into "pbbl" under their old key
//  names, where persistence/migration.cpp finds them, and "notta" is then
//  cleared so a later factory reset cannot resurrect the pet.
//
//  PARTITIONS. KV_MAIN is the default "nvs" partition; KV_CKPT is the private
//  "nvs2" partition of decision D6, which the Arduino core's wholesale erase of
//  "nvs" cannot reach. An absent "nvs2" (a unit still flashed with a stock
//  partition table) is not fatal: KV_CKPT simply reports unhealthy and every
//  checkpoint write fails harmlessly.
// =============================================================================
#ifndef PB_KV_NVS_H
#define PB_KV_NVS_H

#include <stdint.h>

#include "../persistence/kv_store.h"

#define PB_NVS_NAMESPACE    "pbbl"     // D3
#define PB_NVS_LEGACY_NS    "notta"    // v1, read once and cleared
#define PB_NVS_CKPT_PART    "nvs2"     // D6

// kv_error() bit field. Sticky until kv_clear_error().
#define KV_E_NONE           0x00u
#define KV_E_OPEN_MAIN      0x01u      // the "nvs" namespace would not open
#define KV_E_OPEN_CKPT      0x02u      // the "nvs2" partition is missing or full
#define KV_E_CANARY         0x04u      // kv_selftest() failed
#define KV_E_WRITE          0x08u      // a put was short or refused
#define KV_E_ERASE          0x10u      // a remove failed
#define KV_E_READ           0x20u      // a value was longer than the caller's buffer
#define KV_E_IMPORT         0x40u      // the one-shot v1 import failed halfway

// Opens both partitions, runs the canary and performs the one-shot legacy
// import. Call once, after boot_begin() and before any kv_get/kv_put.
// Returns false when KV_MAIN could not be opened (the firmware still runs,
// RAM-only; the UI shows ERR_NVS).
bool     kv_begin(void);

// Writes a fresh random canary and reads it back, on every open partition.
// Called by kv_begin(); exposed so god mode can re-run it on demand.
bool     kv_selftest(void);

uint8_t  kv_error(void);               // KV_E_* bit field, sticky
void     kv_clear_error(void);
uint16_t kv_write_fails(void);         // short/failed writes since boot

// True when this boot copied a v1 "notta" save into "pbbl". The migration in
// save_load_all() is what actually converts it; this only reports the copy.
bool     kv_legacy_imported(void);

#endif // PB_KV_NVS_H
