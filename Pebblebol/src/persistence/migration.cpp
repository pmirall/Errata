// =============================================================================
//  PEBBLEBOL - persistence/migration.cpp
//  v1 (Nottamagochi) -> v2 (SaveSchema) conversion. See migration.h for the
//  field map and the contract. Pure: no Arduino, no allocation, no clock.
// =============================================================================
#include "migration.h"

#include <string.h>

#include "kv_store.h"
#include "legacy_v1.h"
#include "../core/crc16.h"
#include "../core/strings_es.h"   // the dynasty-name syllables, v1's own tables

// -----------------------------------------------------------------------------
// The legacy family map. v1 had no species roster: appearance came out of the
// genome's four-bit species gene. Grouping it modulo 8 gives the eight legacy
// families, and each maps onto one of the eight starter Pebbles of the v2
// roster. The roster itself lands in P9 (src/data/species_table.h); until then
// these are the reserved built-in ids 1..8, which is exactly what a migrated
// pet should be: a starter of the right family, not a random monster.
// -----------------------------------------------------------------------------
static const uint8_t LEGACY_FAMILY_SPECIES[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };

uint8_t migrate_species_of(const Genome& g) {
  const uint8_t legacy = GN_GET(g.g0, GN_SPECIES_SH, GN_SPECIES_MK);   // 0..15
  return LEGACY_FAMILY_SPECIES[legacy & 7u];
}

// v1 had six life stages and no levels; v2 has thirty levels and no stages.
// The anchors below are the plan's: a migrated pet keeps the standing it had
// earned rather than restarting, and never lands above the level its stage
// could have reached.
uint8_t migrate_level_of(uint8_t legacy_stage) {
  switch (legacy_stage) {
    case LV1_STAGE_EGG:
    case LV1_STAGE_BABY:   return 1;
    case LV1_STAGE_CHILD:  return 5;
    case LV1_STAGE_TEEN:   return 10;
    case LV1_STAGE_ADULT:  return 15;
    case LV1_STAGE_SENIOR: return 20;
    default:               return 1;
  }
}

void migrate_default_name(uint32_t lineage_id, uint8_t generation,
                          char* out, size_t cap) {
  if (!out || cap == 0) return;
  // The v1 hash, unchanged (ui.cpp ui_name_for): same dynasty, same name.
  uint32_t h = lineage_id ^ 0x9E3779B9u;
  h ^= (uint32_t)generation * 0x85EBCA6Bu;
  h ^= h >> 15; h *= 0x2545F491u; h ^= h >> 13;
  const char* a = S_SYL_A(h % 12u);
  const char* b = S_SYL_B((h / 12u) % 12u);
  size_t o = 0;
  for (const char* s = a; *s && o + 1 < cap; ++s) out[o++] = *s;
  for (const char* s = b; *s && o + 1 < cap; ++s) out[o++] = *s;
  out[o] = '\0';
}

bool migration_needed(uint8_t found) {
  return found >= SAVE_SCHEMA_VERSION_V1 && found < (uint8_t)SAVE_SCHEMA_VERSION;
}

// -----------------------------------------------------------------------------
// v1 blob validation. Magic, version and CRC, in that order: a foreign magic is
// not a corrupt save, it is somebody else's data, and the two must not be
// reported the same way.
// -----------------------------------------------------------------------------
static bool legacy_petsave_ok(const LegacyPetSave& p) {
  if (p.magic != (uint16_t)LEGACY_SAVE_MAGIC) return false;
  if (p.version != (uint8_t)LEGACY_SAVE_VERSION) return false;
  return p.crc16 == crc16_ccitt(&p, LEGACY_PETSAVE_CRC_BYTES);
}

static bool legacy_config_ok(const LegacyConfig& c) {
  if (c.magic != (uint16_t)LEGACY_CFG_MAGIC) return false;
  if (c.version != (uint8_t)LEGACY_CFG_VERSION) return false;
  return c.crc16 == crc16_ccitt(&c, LEGACY_CONFIG_CRC_BYTES);
}

// A non-zero, stable Pebble id for a pet that never had one. Derived from the
// genome so the same v1 save always migrates to the same id (idempotent
// migration), and folded so that a zero lineage cannot produce id 0, which
// means "empty slot".
static uint32_t migrated_id(const Genome& g) {
  uint32_t h = g.lineage_id * 0x9E3779B1u;
  h ^= (uint32_t)g.generation << 16;
  h ^= (uint32_t)g.g1 * 0x85EBCA77u;
  h ^= h >> 15; h *= 0x2545F491u; h ^= h >> 13;
  return h ? h : 0x50424C31u;      // 'PBL1', the fallback identity
}

// v1 StatId index for each v2 CareId, i.e. care[i] = stat[V1_OF_CARE[i]].
static const uint8_t V1_OF_CARE[PB_CARE_COUNT] = {
  LV1_ST_HUNGER, LV1_ST_HAPPINESS, LV1_ST_HEALTH, LV1_ST_HYGIENE, LV1_ST_ENERGY
};

MigrateResult migrate_v1_to_v2(const uint8_t* petsave128, const uint8_t* cfg256,
                               GameState& out) {
  if (!petsave128) return MIGRATE_NONE;

  LegacyPetSave old;
  memcpy(&old, petsave128, sizeof old);
  if (!legacy_petsave_ok(old)) return MIGRATE_BAD_BLOB;

  LegacyConfig oldcfg;
  bool have_cfg = false;
  if (cfg256) {
    memcpy(&oldcfg, cfg256, sizeof oldcfg);
    have_cfg = legacy_config_ok(oldcfg);
  }

  memset(&out, 0, sizeof out);
  for (uint8_t s = 0; s < BOX_SLOTS; ++s) pebble_clear(out.pebbles[s]);
  box_defaults(out.box);
  cfgv2_defaults(out.cfg);
  inventory_defaults(out.inv);
  cooldowns_defaults(out.cds);
  trade_clear(out.trade);

  // --- the pet itself, into slot 0 ------------------------------------------
  PebbleInstance& p = out.pebbles[0];
  p.species_id         = migrate_species_of(old.genome);
  p.level              = migrate_level_of(old.stage);
  p.genome             = old.genome;
  p.id                 = migrated_id(old.genome);
  p.creation_seed      = old.genome.lineage_id ^ ((uint32_t)old.genome.g2 << 16);
  p.birth_epoch        = old.birth_epoch;
  p.last_updated_epoch = old.last_seen_epoch;
  p.age_s              = old.age_s;
  p.minigames_won      = old.minigames_won;
  p.lifetime_active_s  = old.age_s;      // v1 had exactly one pet, always active
  p.origin             = (old.genome.generation == 0) ? (uint8_t)ORIGIN_STARTER
                                                      : (uint8_t)ORIGIN_BRED;

  for (uint8_t i = 0; i < PB_CARE_COUNT; ++i) {
    p.care[i]     = old.stat[V1_OF_CARE[i]];          // milli-points, unchanged
    p.care_rem[i] = old.stat_rem[V1_OF_CARE[i]];      // exact remainders, unchanged
  }

  if (old.flags & LV1_PF_SICK)      p.status |= PBS_SICK;
  if (old.flags & LV1_PF_ASLEEP)    p.status |= PBS_ASLEEP;
  if (old.flags & LV1_PF_LIGHT_ON)  p.status |= PBS_LIGHT_ON;
  if (old.flags & LV1_PF_GOD_TAINTED) p.flags |= PBF_GOD_TAINTED;

  // The name: an explicit v1 pet_name wins, otherwise the dynasty name the v1
  // UI would have shown, so nothing appears to have been renamed by the update.
  if (have_cfg && oldcfg.pet_name[0] != '\0') {
    size_t o = 0;
    while (o + 1 < sizeof p.nickname && o < sizeof oldcfg.pet_name &&
           oldcfg.pet_name[o] != '\0') { p.nickname[o] = oldcfg.pet_name[o]; ++o; }
    p.nickname[o] = '\0';
  } else {
    migrate_default_name(old.genome.lineage_id, old.genome.generation,
                         p.nickname, sizeof p.nickname);
  }
  pebble_seal(p);

  // --- the Box index --------------------------------------------------------
  out.box.active_slot     = 0;
  out.box.slot_mask       = 0x0001u;
  out.box.next_id_counter = 2;
  out.box.saved_epoch     = old.last_seen_epoch;
  box_seal(out.box);

  // --- the config -----------------------------------------------------------
  if (have_cfg) {
    out.cfg.brightness       = oldcfg.brightness;
    out.cfg.last_known_epoch = oldcfg.saved_epoch;
    memcpy(out.cfg.tz, oldcfg.tz, sizeof out.cfg.tz);
    out.cfg.tz[sizeof out.cfg.tz - 1] = '\0';
    uint16_t f = 0;
    if ((oldcfg.flags & LV1_CF_MUTE) || (old.flags & LV1_PF_SOUND_MUTE)) f |= CFGV2_F_MUTE;
    f |= (uint16_t)((oldcfg.statusbar_mode << CFGV2_F_SBAR_SH) & CFGV2_F_SBAR_MASK);
    out.cfg.flags = f;
  } else if (old.flags & LV1_PF_SOUND_MUTE) {
    out.cfg.flags |= CFGV2_F_MUTE;
  }
  if (out.cfg.last_known_epoch < old.last_seen_epoch) {
    out.cfg.last_known_epoch = old.last_seen_epoch;
  }
  cfgv2_seal(out.cfg);

  inventory_seal(out.inv);
  cooldowns_seal(out.cds);
  return MIGRATE_OK;
}

// -----------------------------------------------------------------------------
// The step table. One row per hop; adding v3 means adding a row, not a branch.
// -----------------------------------------------------------------------------
typedef MigrateResult (*MigrateStepFn)(GameState& out);

static MigrateResult step_v1_to_v2(GameState& out) {
  uint8_t save_bytes[sizeof(LegacyPetSave)];
  uint8_t cfg_bytes[sizeof(LegacyConfig)];

  const int n = kv_get(KV_MAIN, KEY_V1_SAVE, save_bytes, sizeof save_bytes);
  if (n == 0) return MIGRATE_NONE;
  if (n != (int)sizeof save_bytes) return MIGRATE_BAD_BLOB;

  const int m = kv_get(KV_MAIN, KEY_V1_CFG, cfg_bytes, sizeof cfg_bytes);
  const uint8_t* cfg_ptr = (m == (int)sizeof cfg_bytes) ? cfg_bytes : nullptr;

  return migrate_v1_to_v2(save_bytes, cfg_ptr, out);
}

struct MigrateStep {
  uint8_t       from;
  uint8_t       to;
  MigrateStepFn fn;
};

static const MigrateStep MIGRATE_STEPS[] = {
  { 1, 2, &step_v1_to_v2 },
};

bool migrate_v1_present(void) {
  // kv_get refuses to truncate, so the whole blob is read even though only the
  // three header bytes are inspected here.
  uint8_t blob[sizeof(LegacyPetSave)];
  if (kv_get(KV_MAIN, KEY_V1_SAVE, blob, sizeof blob) != (int)sizeof blob) return false;
  const uint16_t magic = (uint16_t)(blob[0] | ((uint16_t)blob[1] << 8));
  return magic == (uint16_t)LEGACY_SAVE_MAGIC && blob[2] == (uint8_t)LEGACY_SAVE_VERSION;
}

MigrateResult migrate_run(uint8_t from, GameState& out) {
  if (!migration_needed(from)) return MIGRATE_NONE;

  uint8_t at = from;
  while (at < (uint8_t)SAVE_SCHEMA_VERSION) {
    const MigrateStep* step = nullptr;
    for (size_t i = 0; i < NT_ARRAY_LEN(MIGRATE_STEPS); ++i) {
      if (MIGRATE_STEPS[i].from == at) { step = &MIGRATE_STEPS[i]; break; }
    }
    if (!step) return MIGRATE_UNSUPPORTED;
    const MigrateResult r = step->fn(out);
    if (r != MIGRATE_OK) return r;
    at = step->to;
  }
  return MIGRATE_OK;
}
