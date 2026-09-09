// =============================================================================
//  ERRATA - persistence/migration.cpp
//  v1 (Nottamagochi) -> v2 (SaveSchema) conversion. See migration.h for the
//  field map and the contract. Pure: no Arduino, no allocation, no clock.
// =============================================================================
#include "migration.h"

#include <string.h>

#include "kv_store.h"
#include "legacy_v1.h"
#include "../core/crc16.h"
#include "../core/strings_es.h"   // the dynasty-name syllables, v1's own tables
#include "../data/species_table.h"   // SPECIES_BASE_OF_FAMILY: where a v1 pet lands
#include "../game/xp.h"              // xp_hp_max(): inline constexpr, no link edge

// -----------------------------------------------------------------------------
// THE LEGACY FAMILY MAP (fixed in P4-C1, obligation 5).
//
// v1 had no species roster: appearance came out of the genome's four-bit
// species gene, and a gen-0 pet rolled 0..7 (GENESIS_SPECIES_MAX). Folding the
// gene modulo LEGACY_FAMILY_COUNT gives the eight legacy families.
//
// WHAT WAS WRONG WITH THE OLD TABLE. It was a literal { 1, 2, 3, 4, 5, 6, 7, 8 },
// written when ids 1..8 were expected to be eight different families' starters.
// P3-C3 then filled ids 1..3 with the three EVOLUTION STAGES of family 1, so
// legacy family 1 landed on a MID-stage creature (and, at the ADULT/SENIOR
// level anchors, one already past its own level-18 evolution gate), legacy
// family 2 landed on a final-stage RARE dead end, and legacy families 3..7
// resolved to nothing at all - species_get() returned nullptr, which meant a
// migrated pet showed a fabricated "full" HP meter, silently skipped the
// level-up HP rescale and could never evolve.
//
// THE RULE NOW: every legacy family lands on the BASE STAGE of a v2 family, so
// a migrated pet starts a family rather than arriving mid-way through one. The
// destination is read from the generated SPECIES_BASE_OF_FAMILY[] rather than
// typed out here, so growing the roster cannot leave a stale literal behind.
//
// WITH THE ROSTER AT 12 FAMILIES the eight legacy families land on eight
// DISTINCT base species (1, 4, 7, 10, 13, 16, 19, 22), which preserves v1's
// "different genomes looked different". A SMALLER ROSTER CANNOT DO THAT: the
// modulo folds, and at 4 families the map would be 1/4/7/10/1/4/7/10. The
// static_assert below states the requirement that actually matters (every
// destination exists and is a base stage) and the distinctness is a property of
// this roster size, not a promise the code can keep at any size.
// -----------------------------------------------------------------------------
#define LEGACY_FAMILY_COUNT  8u

static constexpr bool legacy_families_land_on_a_base_stage(void) {
  for (uint8_t l = 0; l < LEGACY_FAMILY_COUNT; ++l) {
    const uint8_t id = SPECIES_BASE_OF_FAMILY[l % SPECIES_FAMILY_COUNT];
    if (id < SPECIES_ID_MIN || id > SPECIES_TABLE_COUNT) return false;
    if (SPECIES_TABLE[id - 1u].stage != 0u) return false;
  }
  return true;
}
static_assert(legacy_families_land_on_a_base_stage(),
              "a legacy v1 family migrates onto a species that does not exist or is "
              "not a base stage");

uint8_t migrate_species_of(const Genome& g) {
  const uint8_t legacy = GN_GET(g.g0, GN_SPECIES_SH, GN_SPECIES_MK);   // 0..15
  return SPECIES_BASE_OF_FAMILY[(legacy & (LEGACY_FAMILY_COUNT - 1u))
                                % SPECIES_FAMILY_COUNT];
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

// migrate_default_name() USED TO LIVE HERE and was deleted by P4-C4's
// follow-up, together with the only call it had. It wrote the v1 dynasty name
// into BugInstance.nickname for a pet whose owner had never typed one, so
// that "nothing appears to have been renamed by the update". That was
// DISPLAY-NEUTRAL when it was written - ui.cpp's ui_name_for() computes the
// same hash over the same syllable tables, so an empty nickname rendered the
// identical word - and it stopped being neutral the moment P4-C4a gave the
// name ladder a middle rung. See the nickname block in migrate_v1_to_v2().

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

// A non-zero, stable Bug id for a pet that never had one. Derived from the
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
static const uint8_t V1_OF_CARE[ER_CARE_COUNT] = {
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
  for (uint8_t s = 0; s < BOX_SLOTS; ++s) bug_clear(out.bugs[s]);
  box_defaults(out.box);
  cfgv2_defaults(out.cfg);
  inventory_defaults(out.inv);
  cooldowns_defaults(out.cds);
  trade_clear(out.trade);

  // --- the pet itself, into slot 0 ------------------------------------------
  BugInstance& p = out.bugs[0];
  p.species_id         = migrate_species_of(old.genome);
  p.level              = migrate_level_of(old.stage);
  // TWO FIELDS THIS FUNCTION USED TO LEAVE AT ZERO (found by the P4-C1 survey,
  // fixed here because the species row it needs only exists now).
  //   * moves[] stayed {0,0,0,0}: a migrated pet knew no attacks at all, and
  //     0 is the EMPTY move slot, so it would have walked into P4's battle with
  //     nothing to do on any of its four buttons.
  //   * hp_cur stayed 0: hp_max is derived but hp_cur is a Bug's own, and
  //     xp_hp_rescale() scales it, so 0 stays 0 for ever. Every screen that
  //     draws the HP meter (screen_home, screen_status, screen_box) would have
  //     shown a migrated pet at 0 % HP permanently.
  // A migration is not a punishment: the pet arrives with its family's learnset
  // and at full health, exactly like box_new_bug() gives a fresh one.
  {
    const SpeciesDef* msp = species_get(p.species_id);
    if (msp != nullptr) {
      memcpy(p.moves, msp->moves, sizeof p.moves);
      p.hp_cur = xp_hp_max(msp->base_hp, (p.level == 0u) ? 1u : p.level);
    }
  }
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

  for (uint8_t i = 0; i < ER_CARE_COUNT; ++i) {
    p.care[i]     = old.stat[V1_OF_CARE[i]];          // milli-points, unchanged
    p.care_rem[i] = old.stat_rem[V1_OF_CARE[i]];      // exact remainders, unchanged
  }

  if (old.flags & LV1_PF_SICK)      p.status |= PBS_SICK;
  if (old.flags & LV1_PF_ASLEEP)    p.status |= PBS_ASLEEP;
  // LV1_PF_LIGHT_ON is deliberately DROPPED (P3-C2b): the light mechanic no
  // longer exists, so carrying its bit forward would only put a meaning that
  // nothing implements into a fresh v2 save.
  if (old.flags & LV1_PF_GOD_TAINTED) p.flags |= PBF_GOD_TAINTED;

  // THE NAME. An explicit v1 pet_name is a name the OWNER TYPED and nothing
  // outranks it, so it comes across as the nickname. Anything else leaves the
  // nickname EMPTY, and that is a deliberate change of behaviour rather than an
  // omission.
  //
  // WHY IT CHANGED. This used to synthesize the v1 dynasty name here when there
  // was no typed one. At the time that was display-neutral: ui.cpp's
  // ui_name_for() computes the same hash over the same syllable tables, so an
  // empty nickname drew the identical word, and the write only mattered to the
  // BOX list. P4-C4a then made the display ladder nickname -> SPECIES NAME ->
  // dynasty, and this write pinned every migrated device to the first rung for
  // ever: persistence/game_state.cpp copies bugs[0].nickname into
  // Config.pet_name when device_name is empty, ui_pet_name() answers
  // Config.pet_name before anything else, and no v2 code path ever clears it.
  // The result was that a migrated player's HOME showed the dynasty syllables
  // for the life of the device and the species name never appeared once - the
  // exact complaint P3-C3 raised and P4-C4a set out to close - while a fresh v2
  // device (box.cpp writes no nickname, ever) showed Paketo and then Fragmar.
  //
  // WHAT LEAVING IT EMPTY COSTS, NARROWED IN P4-C6 - this used to say "NOTHING
  // IS LOST", and that sentence contradicted its own next clause. The dynasty
  // syllables are the LAST rung of ui_pet_name()'s ladder, reached only by a
  // Bug with NO species row; migrate_species_of() always lands on a real
  // roster id, so a migrated pet never reaches that rung and the v1 dynasty word
  // is gone from the device for good. There is no rename UI in V1 either -
  // nothing outside persistence/game_state.cpp writes Config.pet_name - so an
  // update renames a player's pet with no way back to the old word.
  //
  // IT IS STILL THE RIGHT TRADE, and it is a trade rather than a free win: a
  // name that never changes on a creature that now evolves is worse than a name
  // the roster owns, because the evolution is the thing V1 has to be able to
  // show. Restoring the old word would mean a nickname field on the SETTINGS
  // screen, which is P8's keyboard and not this function's business.
  if (have_cfg && oldcfg.pet_name[0] != '\0') {
    size_t o = 0;
    while (o + 1 < sizeof p.nickname && o < sizeof oldcfg.pet_name &&
           oldcfg.pet_name[o] != '\0') { p.nickname[o] = oldcfg.pet_name[o]; ++o; }
    p.nickname[o] = '\0';
  }
  bug_seal(p);

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

// -----------------------------------------------------------------------------
//  v2 -> v3 (P10-C5). A NO-OP TRANSFORM, AND SAYING SO IS THE POINT.
//
//  Not one field moves between v2 and v3: every struct in save_schema.h has the
//  same size, the same offsets and the same meaning, and the static_asserts in
//  that header pin all three. The version number moves because spec section 31
//  asks for the migration path to be EXERCISED before the release rather than
//  merely to exist, and because a release is the honest moment to find out
//  whether "bump the number and add a row" is really all it takes.
//
//  IT TURNED OUT NOT TO BE, and that is recorded in three places rather than
//  fixed in silence: core/version.h (why a naive bump is LOAD_CORRUPT on every
//  played device), save_manager.cpp's BlobOps::min_version (the read side) and
//  its pair_write() (the write side, where a converging upgrade needs the
//  WRITER to accept what the reader accepts).
//
//  WHAT IT ACTUALLY DOES is re-seal: blob_seal() stamps o.version and
//  recomputes the CRC, so after this step every blob in 'out' says 3.
//
//  AND THE MUTATION SAYS EXACTLY HOW MUCH OF THAT IS LOAD-BEARING, which is
//  worth more than the claim it replaced. Emptied to `return MIGRATE_OK;`, the
//  suite fails on THREE of the five: `inv.version 2 != 3`, `cds.version 2 != 3`
//  and `trade.version 2 != 3`. The Box and the config come out right ANYWAY -
//  load_all_inner() box_seal()s the header before it calls the chain (the
//  slot_mask may have healed), and save_config() seals into the CALLER'S struct
//  rather than a copy - so those two would have been an accident rather than a
//  migration, and this comment would have been claiming credit for it.
//
//  THE BUGS ARE DELIBERATELY NOT TOUCHED. BugInstance carries
//  BUG_LAYOUT_VER, a version axis of its own that has not moved; sealing
//  them here would produce identical bytes and imply a change that did not
//  happen. If a later bump does move a Bug field, BUG_LAYOUT_VER is what
//  moves with it, and this is where the loop goes.
// -----------------------------------------------------------------------------
static MigrateResult step_v2_to_v3(GameState& out) {
  box_seal(out.box);
  cfgv2_seal(out.cfg);
  inventory_seal(out.inv);
  cooldowns_seal(out.cds);
  trade_seal(out.trade);
  return MIGRATE_OK;
}

struct MigrateStep {
  uint8_t       from;
  uint8_t       to;
  MigrateStepFn fn;
};

static constexpr MigrateStep MIGRATE_STEPS[] = {
  { 1, 2, &step_v1_to_v2 },
  { 2, 3, &step_v2_to_v3 },
};

// -----------------------------------------------------------------------------
//  THE CHAIN IS COMPLETE, CHECKED BY THE COMPILER.
//
//  The failure this exists to prevent is a one-character edit: somebody bumps
//  SAVE_SCHEMA_VERSION in core/version.h and does not add a row here. Nothing
//  would complain - migrate_run() would answer MIGRATE_UNSUPPORTED at runtime,
//  the loader would turn that into LOAD_CORRUPT, and the first person to find
//  out would be a player watching SAVE ERROR after a firmware update. A grep
//  gate cannot check it (the numbers live in two files and one of them is a
//  table), so it is a static_assert: the walk from the oldest readable save to
//  this firmware's version must land exactly, using only rows that exist.
// -----------------------------------------------------------------------------
static constexpr bool migrate_chain_reaches_current(void) {
  uint8_t at = (uint8_t)SAVE_SCHEMA_VERSION_V1;
  // At most one hop per row, so a table with a cycle terminates here rather
  // than hanging the compiler.
  for (size_t guard = 0; guard <= NT_ARRAY_LEN(MIGRATE_STEPS); ++guard) {
    if (at == (uint8_t)SAVE_SCHEMA_VERSION) return true;
    bool moved = false;
    for (size_t i = 0; i < NT_ARRAY_LEN(MIGRATE_STEPS); ++i) {
      if (MIGRATE_STEPS[i].from == at && MIGRATE_STEPS[i].to > at) {
        at = MIGRATE_STEPS[i].to;
        moved = true;
        break;
      }
    }
    if (!moved) return false;
  }
  return at == (uint8_t)SAVE_SCHEMA_VERSION;
}
static_assert(migrate_chain_reaches_current(),
              "SAVE_SCHEMA_VERSION was bumped without a matching MIGRATE_STEPS row. "
              "A save written by the previous firmware would reach migrate_run(), "
              "find no step, and be reported to the player as LOAD_CORRUPT - "
              "SAVE ERROR on a device whose data is perfectly intact.");

// The in-place range is a claim about LAYOUT, not about kindness: a blob inside
// it is read straight into the live struct, so its fields must still be where
// this firmware expects them. v1 is outside it and has a transform instead.
static_assert(SAVE_SCHEMA_INPLACE_MIN >= 2 &&
              SAVE_SCHEMA_INPLACE_MIN <= (uint8_t)SAVE_SCHEMA_VERSION,
              "SAVE_SCHEMA_INPLACE_MIN must name a version whose layout is this "
              "firmware's, and never v1, which is a different set of keys");

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
