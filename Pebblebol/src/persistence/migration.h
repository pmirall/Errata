// =============================================================================
//  PEBBLEBOL - persistence/migration.h
//  Reading a save written by an older firmware. Plan section 1.4 / 1.5.4.
//
//  A migration is a one-way, total function from the frozen bytes of an older
//  schema (persistence/legacy_v1.h) to a live GameState. It never guesses and
//  never partially applies: either the whole state is produced and the caller
//  commits it, or nothing at all is written and the old keys stay untouched.
//
//  Chaining is table-driven, so v3 adds a row instead of a branch:
//      migrate_run(found_version, out)  ->  1 -> 2 -> ... -> SAVE_SCHEMA_VERSION
//
//  Pure module: no Arduino, no allocation. It talks to flash only through
//  persistence/kv_store.h, so the host tests drive it against tests/fakes.
// =============================================================================
#ifndef PB_MIGRATION_H
#define PB_MIGRATION_H

#include <stdint.h>
#include <stddef.h>

#include "save_schema.h"

enum MigrateResult : uint8_t {
  MIGRATE_OK = 0,        // 'out' holds a complete v2 state, ready to commit
  MIGRATE_NONE,          // there was nothing older to read
  MIGRATE_BAD_BLOB,      // the old blob failed magic / version / CRC
  MIGRATE_UNSUPPORTED    // no path exists from that version to this one
};

// True when a save carrying schema version 'found' must be converted before it
// can be used: older than this firmware, and not zero (0 = no save at all).
// A version ABOVE SAVE_SCHEMA_VERSION is not a migration, it is
// LOAD_FOREIGN_NEWER - see schema_is_foreign_newer().
bool migration_needed(uint8_t found);

// The v1 -> v2 transform. Pure: 'petsave128' and 'cfg256' are the raw NVS bytes
// of the legacy "save" and "cfg" keys; 'cfg256' may be null, in which case the
// config half falls back to cfgv2_defaults(). 'out' is fully overwritten,
// sealed and internally consistent (one Pebble in slot 0, marked active).
//
// FIELD MAP (plan P2-C9a), asserted field by field in tests/test_persistence.cpp:
//   species        gene_species(genome) & 7 -> LEGACY_FAMILY_SPECIES[]
//   stage          -> level: EGG/BABY 1, CHILD 5, TEEN 10, ADULT 15, SENIOR 20
//   stat[]         -> care[]      reordered from v1 StatId to v2 CareId
//   stat_rem[]     -> care_rem[]  same reorder; milli-points and remainders
//                                 both carry over unchanged
//   Genome         copied whole
//   pet_name       -> nickname, or the deterministic dynasty name when empty
//   birth_epoch    -> birth_epoch
//   last_seen      -> last_updated_epoch
//   age_s          -> age_s
//   flags          SICK/ASLEEP -> status, GOD_TAINTED -> flags; LIGHT_ON is
//                  dropped (P3-C2b deleted the light mechanic)
//   minigames_won  -> minigames_won
//   Config.tz/brightness/mute/statusbar -> ConfigV2
//   the "gl" gain ledger is NOT touched: it keeps its own key and layout
MigrateResult migrate_v1_to_v2(const uint8_t* petsave128, const uint8_t* cfg256,
                               GameState& out);

// Reads whatever the older firmware left in KV_MAIN and runs every step from
// 'from' up to SAVE_SCHEMA_VERSION. Writes nothing: committing the result and
// erasing the legacy keys is save_manager's job, so a power cut in the middle
// of a migration leaves the v1 save intact and the migration simply runs again.
MigrateResult migrate_run(uint8_t from, GameState& out);

// True when KV_MAIN still holds a legacy v1 "save" blob (magic and version only
// - the CRC is checked by the migration itself).
bool migrate_v1_present(void);

// The deterministic dynasty name: a pure function of (lineage_id, generation),
// so a pet is called the same thing on every device, forever. This is the same
// hash and the same syllable tables the v1 UI used, kept here so a migrated
// pet does not silently get renamed. 'out' receives at most cap-1 characters
// plus a NUL.
void migrate_default_name(uint32_t lineage_id, uint8_t generation,
                          char* out, size_t cap);

// Legacy family (gene species & 7) -> v2 species id. Exposed so the test can
// assert the map instead of re-deriving it.
uint8_t migrate_species_of(const Genome& g);

// stage -> level, exposed for the same reason.
uint8_t migrate_level_of(uint8_t legacy_stage);

#endif // PB_MIGRATION_H
