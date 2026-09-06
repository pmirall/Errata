// =============================================================================
//  Pebblebol host tests - test_persistence.cpp
//  SaveSchema v2 end to end (plan P2-C9a, test plan section 4.3): the layouts,
//  the key table, the pair discipline, the tri-state load, the v1 migration and
//  the nvs2 checkpoint - each driven against tests/fakes/kv_mem.cpp so that
//  every failure mode a real NVS can produce is provoked on purpose.
//
//  The load-time rule under test throughout: a load NEVER destroys a save it
//  could not read. LOAD_CORRUPT and LOAD_FOREIGN_NEWER write nothing at all.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "fakes/kv_mem.h"
#include "persistence/save_schema.h"
#include "persistence/save_manager.h"
#include "persistence/migration.h"
#include "persistence/legacy_v1.h"
#include "core/crc16.h"
#include "data/species_table.h"   // the legacy family map's destinations
#include "game/genome.h"          // gene_set_species(), to drive the map
#include "game/species.h"         // species_base_of_family()
#include "game/xp.h"              // xp_hp_max()
#include "game/pebble.h"          // pebble_name_syllables(): the dynasty hash (P9-C4)
#include "game/validate.h"        // the load path is a spec 15 consumer (P4-C5)
#include "game/species_custom.h"   // the creator registry the load path rebuilds (P8-C3)
#include "data/creator_schema.h"

// --- a fake clock so the wear filter and the 1 s floor are deterministic -----
static uint32_t s_ms    = 0;
static uint32_t s_epoch = 1700300000u;
static uint32_t fake_ms(void)    { return s_ms; }
static uint32_t fake_epoch(void) { return s_epoch; }

static void begin(void) {
  kv_mem_reset();
  s_ms    = 10000;
  s_epoch = 1700300000u;
  save_set_clock(&fake_ms, &fake_epoch);
}

static void advance(uint32_t ms) { s_ms += ms; }

// --- fixtures ----------------------------------------------------------------
static size_t load_file(const char* rel, uint8_t* out, size_t cap) {
  char path[512];
  nt_path(rel, path, sizeof path);
  FILE* f = fopen(path, "rb");
  if (!f) { fprintf(stderr, "  cannot open %s\n", path); return 0; }
  const size_t n = fread(out, 1, cap, f);
  fclose(f);
  return n;
}

// A Pebble with every field set to something distinctive, so a layout slip
// shows up as a wrong value rather than as a zero that happened to match.
static PebbleInstance sample_pebble(uint8_t slot) {
  PebbleInstance p;
  pebble_clear(p);
  p.species_id         = (uint8_t)(7 + slot);
  p.id                 = 0xA1B2C300u + slot;
  p.creation_seed      = 0x0BADF00Du;
  p.birth_epoch        = 1700000000u;
  p.last_updated_epoch = 1700200000u;
  p.age_s              = 123456u;
  p.xp                 = 4321;
  p.level              = 12;
  p.evo_state          = 1;
  for (uint8_t i = 0; i < PB_CARE_COUNT; ++i) {
    p.care[i]     = (int32_t)(10000 + 1000 * i);
    p.care_rem[i] = (int16_t)(100 + i);
  }
  p.hp_cur            = 33;
  p.status            = PBS_RESERVED_LIGHT | PBS_SICK;   // 0x10 is reserved (P3-C2b)
  p.flags             = PBF_RARE;
  p.moves[0] = 1; p.moves[1] = 2; p.moves[2] = 3; p.moves[3] = 4;
  p.origin            = ORIGIN_WILD;
  p.trait_id          = 9;
  p.battles_won       = 11;
  p.battles_lost      = 5;
  p.minigames_won     = 21;
  p.evolutions        = 2;
  p.trades            = 1;
  p.lifetime_active_s = 98765u;
  p.genome.magic_ver  = GENOME_MAGIC_VER;
  p.genome.lineage_id = 0x0BADCAFEu;
  p.genome.g0 = 0x1234; p.genome.g1 = 0x5678; p.genome.g2 = 0x9ABC;
  p.genome.generation = 3;
  p.genome.parent_tag = 0x55;
  p.genome.crc16      = crc16_ccitt(&p.genome, GENOME_CRC_BYTES);
  snprintf(p.nickname, sizeof p.nickname, "Pebbli%u", (unsigned)slot);
  p.custom_sprite = PB_CUSTOM_SPRITE_NONE;
  pebble_seal(p);
  return p;
}

// =============================================================================
//  1. Layout and keys
// =============================================================================
TEST(schema_sizes_are_the_wire_sizes) {
  CHECK_EQ(sizeof(PebbleInstance),   128);
  CHECK_EQ(sizeof(BoxHeader),         32);
  CHECK_EQ(sizeof(ConfigV2),         256);
  CHECK_EQ(sizeof(Inventory),         32);
  CHECK_EQ(sizeof(CooldownTable),    272);
  CHECK_EQ(sizeof(CustomSpeciesRec), 192);
  CHECK_EQ(sizeof(PendingTrade),      64);
  CHECK_EQ(sizeof(GameState),       1936);

  CHECK_EQ(offsetof(PebbleInstance, care),     28);
  CHECK_EQ(offsetof(PebbleInstance, care_rem), 48);
  CHECK_EQ(offsetof(PebbleInstance, genome),   80);
  CHECK_EQ(offsetof(PebbleInstance, nickname), 96);
  CHECK_EQ(offsetof(PebbleInstance, seq),     110);
  CHECK_EQ(offsetof(PebbleInstance, crc16),   126);
  CHECK_EQ(SAVE_SCHEMA_VERSION, 2);
}

TEST(every_nvs_key_fits_in_fifteen_chars) {
  char key[KV_KEY_CAP];
  static const char* singles[] = {
    KEY_TRADE, KEY_GAIN, KEY_LASTSEEN, KEY_CANARY,
    KEY_CK_BOX, KEY_CK_CFG,
    KEY_V1_SAVE, KEY_V1_CFG, KEY_V1_EGG, KEY_V1_ANCESTORS
  };
  for (size_t i = 0; i < sizeof singles / sizeof singles[0]; ++i) {
    CHECK(strlen(singles[i]) <= (size_t)KV_KEY_MAX_LEN);
    CHECK(strlen(singles[i]) > 0);
  }
  static const char* prefixes[] = {
    KEY_BOX_PREFIX, KEY_CFG_PREFIX, KEY_INV_PREFIX, KEY_CD_PREFIX
  };
  for (size_t i = 0; i < sizeof prefixes / sizeof prefixes[0]; ++i) {
    for (uint8_t c = 0; c < 2; ++c) {
      key_pair(prefixes[i], c, key);
      CHECK(strlen(key) <= (size_t)KV_KEY_MAX_LEN);
      CHECK(strlen(key) > 0);
    }
  }
  for (uint8_t slot = 0; slot < BOX_SLOTS; ++slot) {
    for (uint8_t c = 0; c < 2; ++c) {
      key_pebble(slot, c, key);
      CHECK(strlen(key) <= (size_t)KV_KEY_MAX_LEN);
      CHECK_EQ(strlen(key), 4);
    }
    key_ck_pebble(slot, key);
    CHECK(strlen(key) <= (size_t)KV_KEY_MAX_LEN);
  }
  for (uint8_t slot = 0; slot < CUSTOM_SPECIES_SLOTS; ++slot) {
    key_custom(slot, key);
    CHECK(strlen(key) <= (size_t)KV_KEY_MAX_LEN);
  }
  // The builders are total: an out-of-range index yields no key at all.
  key_pebble(BOX_SLOTS, 0, key); CHECK_EQ(strlen(key), 0);
  key_pebble(0, 2, key);         CHECK_EQ(strlen(key), 0);
  key_custom(CUSTOM_SPECIES_SLOTS, key); CHECK_EQ(strlen(key), 0);

  // And every key the save manager actually used stayed inside the limit.
  begin();
  GameState gs;
  CHECK_EQ(save_load_all(gs), LOAD_FRESH);
  gs.pebbles[3] = sample_pebble(3);
  gs.box.slot_mask = 0x0008u;
  gs.box.active_slot = 3;
  CHECK(save_pebble(3, gs.pebbles[3], true));
  CHECK(save_box_header(gs.box));
  CHECK(save_config(gs.cfg));
  CHECK(save_inventory(gs.inv));
  CHECK(save_cooldowns(gs.cds));
  CHECK(save_trade_journal(gs.trade));
  CHECK(save_checkpoint_all());
  CHECK(kv_mem_longest_key() <= (size_t)KV_KEY_MAX_LEN);
}

// =============================================================================
//  2. Round trip: every blob out to flash and back, field by field
// =============================================================================
TEST(round_trip_every_blob) {
  begin();
  GameState gs;
  CHECK_EQ(save_load_all(gs), LOAD_FRESH);

  gs.pebbles[0] = sample_pebble(0);
  gs.pebbles[4] = sample_pebble(4);
  gs.box.slot_mask   = 0x0011u;         // slots 0 and 4
  gs.box.active_slot = 4;
  gs.box.next_id_counter = 77;
  gs.box.captures = 12;
  gs.box.battles  = 34;

  gs.cfg.device_id       = 0xDEADBEEFu;
  gs.cfg.time_cal_state  = CAL_PHONE;
  gs.cfg.time_cal_epoch  = 1700111111u;
  gs.cfg.last_known_epoch= 1700222222u;
  gs.cfg.creator_pin     = 4242;
  gs.cfg.creator_idle_s  = 120;
  gs.cfg.pin_fail_count  = 2;
  gs.cfg.pin_lock_until  = 1700333333u;
  gs.cfg.brightness      = 99;
  gs.cfg.flags           = CFGV2_F_MUTE;
  snprintf(gs.cfg.device_name, sizeof gs.cfg.device_name, "Bolita");
  snprintf(gs.cfg.ap_pass, sizeof gs.cfg.ap_pass, "ap-secret-01");

  gs.inv.ledger_epoch = 1700444444u;
  for (uint8_t i = 0; i < XP_LEDGER_SLOTS; ++i) gs.inv.xp_ledger[i] = (uint8_t)(i + 1);
  for (uint8_t i = 0; i < INVENTORY_SLOTS; ++i) {
    gs.inv.items[i].item_id = (uint8_t)(i + 1);
    gs.inv.items[i].count   = (uint8_t)(i * 3 + 1);
  }

  gs.cds.n = COOLDOWN_SLOTS;
  for (uint8_t i = 0; i < COOLDOWN_SLOTS; ++i) {
    gs.cds.rows[i].net_hash    = 0x1000u + i;
    gs.cds.rows[i].until_epoch = 1700500000u + i;
  }

  gs.trade.phase   = TRADE_COMMIT;
  gs.trade.out_id  = 0xA1B2C300u;
  gs.trade.peer_id = 0x0C0FFEE0u;
  for (uint8_t i = 0; i < TR_WIRE_BYTES; ++i) gs.trade.in_wire[i] = (uint8_t)(i * 5);

  CHECK(save_pebble(0, gs.pebbles[0], true));
  advance(2000);
  CHECK(save_pebble(4, gs.pebbles[4], true));
  CHECK(save_box_header(gs.box));
  CHECK(save_config(gs.cfg));
  CHECK(save_inventory(gs.inv));
  CHECK(save_cooldowns(gs.cds));
  CHECK(save_trade_journal(gs.trade));

  CustomSpeciesRec cs;
  memset(&cs, 0, sizeof cs);
  cs.slot         = 3;
  cs.budget_used  = 480;
  cs.type         = 2;
  cs.compat_group = 5;
  for (uint8_t i = 0; i < CS_BASE_COUNT; ++i) cs.base[i] = (uint8_t)(4 + i);
  for (uint8_t i = 0; i < PB_MOVE_COUNT; ++i) cs.moves[i] = (uint8_t)(10 + i);
  snprintf(cs.name, sizeof cs.name, "Cuarzo");
  for (uint8_t f = 0; f < CS_SPRITE_FRAMES; ++f) {
    for (uint8_t i = 0; i < CS_SPRITE_BYTES; ++i) cs.sprite[f][i] = (uint8_t)(f * 100 + i);
  }
  CHECK(save_custom_species(cs));

  GameState back;
  CHECK_EQ(save_load_all(back), LOAD_OK);

  // --- PebbleInstance -------------------------------------------------------
  const PebbleInstance& a = gs.pebbles[4];
  const PebbleInstance& b = back.pebbles[4];
  CHECK_EQ(b.species_id, a.species_id);
  CHECK_EQ(b.id, a.id);
  CHECK_EQ(b.creation_seed, a.creation_seed);
  CHECK_EQ(b.birth_epoch, a.birth_epoch);
  CHECK_EQ(b.last_updated_epoch, a.last_updated_epoch);
  CHECK_EQ(b.age_s, a.age_s);
  CHECK_EQ(b.xp, a.xp);
  CHECK_EQ(b.level, a.level);
  CHECK_EQ(b.evo_state, a.evo_state);
  for (uint8_t i = 0; i < PB_CARE_COUNT; ++i) {
    CHECK_EQ(b.care[i], a.care[i]);
    CHECK_EQ(b.care_rem[i], a.care_rem[i]);
  }
  CHECK_EQ(b.hp_cur, a.hp_cur);
  CHECK_EQ(b.status, a.status);
  CHECK_EQ(b.flags, a.flags);
  for (uint8_t i = 0; i < PB_MOVE_COUNT; ++i) CHECK_EQ(b.moves[i], a.moves[i]);
  CHECK_EQ(b.origin, a.origin);
  CHECK_EQ(b.trait_id, a.trait_id);
  CHECK_EQ(b.battles_won, a.battles_won);
  CHECK_EQ(b.battles_lost, a.battles_lost);
  CHECK_EQ(b.minigames_won, a.minigames_won);
  CHECK_EQ(b.evolutions, a.evolutions);
  CHECK_EQ(b.trades, a.trades);
  CHECK_EQ(b.lifetime_active_s, a.lifetime_active_s);
  CHECK_EQ(memcmp(&b.genome, &a.genome, sizeof(Genome)), 0);
  CHECK_STR_EQ(b.nickname, a.nickname);
  CHECK_EQ(b.custom_sprite, a.custom_sprite);
  CHECK(pebble_blob_ok(b));
  CHECK_EQ(back.pebbles[0].id, gs.pebbles[0].id);

  // --- BoxHeader ------------------------------------------------------------
  CHECK_EQ(back.box.slot_mask, 0x0011u);
  CHECK_EQ(back.box.active_slot, 4);
  CHECK_EQ(back.box.next_id_counter, 77);
  CHECK_EQ(back.box.captures, 12);
  CHECK_EQ(back.box.battles, 34);
  CHECK_EQ(back.box.schema_version, SAVE_SCHEMA_VERSION);
  CHECK_EQ(back.box.content_version, CONTENT_VERSION);
  CHECK_EQ(back.box.protocol_version, PROTOCOL_VERSION);
  CHECK_EQ(back.box.saved_epoch, s_epoch);

  // --- ConfigV2 -------------------------------------------------------------
  CHECK_EQ(back.cfg.device_id, 0xDEADBEEFu);
  CHECK_EQ(back.cfg.time_cal_state, CAL_PHONE);
  CHECK_EQ(back.cfg.time_cal_epoch, 1700111111u);
  CHECK_EQ(back.cfg.last_known_epoch, 1700222222u);
  CHECK_EQ(back.cfg.creator_pin, 4242);
  CHECK_EQ(back.cfg.creator_idle_s, 120);
  CHECK_EQ(back.cfg.pin_fail_count, 2);
  CHECK_EQ(back.cfg.pin_lock_until, 1700333333u);
  CHECK_EQ(back.cfg.brightness, 99);
  CHECK_EQ(back.cfg.flags, CFGV2_F_MUTE);
  CHECK_STR_EQ(back.cfg.device_name, "Bolita");
  CHECK_STR_EQ(back.cfg.ap_pass, "ap-secret-01");
  CHECK_STR_EQ(back.cfg.tz, CFG_TZ_STRING);

  // --- Inventory ------------------------------------------------------------
  CHECK_EQ(back.inv.slots, INVENTORY_SLOTS);
  CHECK_EQ(back.inv.ledger_epoch, 1700444444u);
  for (uint8_t i = 0; i < XP_LEDGER_SLOTS; ++i) CHECK_EQ(back.inv.xp_ledger[i], i + 1);
  for (uint8_t i = 0; i < INVENTORY_SLOTS; ++i) {
    CHECK_EQ(back.inv.items[i].item_id, i + 1);
    CHECK_EQ(back.inv.items[i].count, i * 3 + 1);
  }

  // --- CooldownTable --------------------------------------------------------
  CHECK_EQ(back.cds.n, COOLDOWN_SLOTS);
  for (uint8_t i = 0; i < COOLDOWN_SLOTS; ++i) {
    CHECK_EQ(back.cds.rows[i].net_hash, 0x1000u + i);
    CHECK_EQ(back.cds.rows[i].until_epoch, 1700500000u + i);
  }

  // --- PendingTrade ---------------------------------------------------------
  CHECK_EQ(back.trade.phase, TRADE_COMMIT);
  CHECK_EQ(back.trade.out_id, 0xA1B2C300u);
  CHECK_EQ(back.trade.peer_id, 0x0C0FFEE0u);
  CHECK_EQ(memcmp(back.trade.in_wire, gs.trade.in_wire, TR_WIRE_BYTES), 0);

  // --- CustomSpeciesRec -----------------------------------------------------
  CustomSpeciesRec cs_back;
  CHECK(save_load_custom_species(3, cs_back));
  CHECK_EQ(cs_back.budget_used, 480);
  CHECK_EQ(cs_back.type, 2);
  CHECK_EQ(cs_back.compat_group, 5);
  CHECK_STR_EQ(cs_back.name, "Cuarzo");
  CHECK_EQ(memcmp(cs_back.sprite, cs.sprite, sizeof cs.sprite), 0);
  CHECK(!save_load_custom_species(4, cs_back));    // an empty slot is not an error
}

// =============================================================================
//  3. Pair recovery
// =============================================================================
TEST(crc_flip_falls_back_to_the_other_copy) {
  begin();
  GameState gs;
  save_load_all(gs);
  gs.pebbles[0] = sample_pebble(0);
  gs.box.slot_mask = 1; gs.box.active_slot = 0;
  CHECK(save_pebble(0, gs.pebbles[0], true));
  advance(2000);
  // A second write fills the other copy of the pair, so both are now good.
  gs.pebbles[0].xp = 5555;
  pebble_seal(gs.pebbles[0]);
  CHECK(save_pebble(0, gs.pebbles[0], true));
  CHECK(save_box_header(gs.box));
  CHECK(kv_mem_exists(KV_MAIN, "pb00"));
  CHECK(kv_mem_exists(KV_MAIN, "pb01"));

  // Rot one byte of the copy holding the newest write.
  CHECK(kv_mem_corrupt("pb01", 20));

  GameState back;
  CHECK_EQ(save_load_all(back), LOAD_RECOVERED_PAIR);
  CHECK_EQ(back.pebbles[0].id, gs.pebbles[0].id);
  CHECK(pebble_blob_ok(back.pebbles[0]));

  // The repair was written back: a second load is clean again.
  GameState again;
  CHECK_EQ(save_load_all(again), LOAD_OK);
  CHECK_EQ(again.pebbles[0].id, gs.pebbles[0].id);
}

TEST(both_copies_bad_is_corrupt_and_writes_nothing) {
  begin();
  GameState gs;
  save_load_all(gs);
  gs.pebbles[0] = sample_pebble(0);
  gs.box.slot_mask = 1; gs.box.active_slot = 0;
  CHECK(save_pebble(0, gs.pebbles[0], true));
  advance(2000);
  CHECK(save_pebble(0, gs.pebbles[0], true));
  CHECK(save_box_header(gs.box));
  advance(2000);
  CHECK(save_box_header(gs.box));

  CHECK(kv_mem_corrupt("pb00", 10));
  CHECK(kv_mem_corrupt("pb01", 10));

  const uint32_t before = kv_mem_puts();
  GameState back;
  CHECK_EQ(save_load_all(back), LOAD_CORRUPT);
  CHECK_EQ(kv_mem_puts(), before);          // never auto-wipes, never rewrites
  CHECK(kv_mem_exists(KV_MAIN, "pb00"));
  CHECK(kv_mem_exists(KV_MAIN, "pb01"));
}

TEST(both_box_copies_bad_is_corrupt) {
  begin();
  GameState gs;
  save_load_all(gs);
  gs.pebbles[0] = sample_pebble(0);
  gs.box.slot_mask = 1; gs.box.active_slot = 0;
  save_pebble(0, gs.pebbles[0], true);
  save_box_header(gs.box);
  advance(2000);
  save_box_header(gs.box);

  CHECK(kv_mem_corrupt("box0", 8));
  CHECK(kv_mem_corrupt("box1", 8));

  const uint32_t before = kv_mem_puts();
  GameState back;
  CHECK_EQ(save_load_all(back), LOAD_CORRUPT);
  CHECK_EQ(kv_mem_puts(), before);
}

// =============================================================================
//  4. A write that fails mid-commit must not cost the previous save
// =============================================================================
TEST(failed_put_leaves_the_previous_copy_loadable) {
  begin();
  GameState gs;
  save_load_all(gs);
  gs.pebbles[0] = sample_pebble(0);
  snprintf(gs.pebbles[0].nickname, sizeof gs.pebbles[0].nickname, "Antes");
  gs.box.slot_mask = 1; gs.box.active_slot = 0;
  CHECK(save_pebble(0, gs.pebbles[0], true));
  advance(2000);
  CHECK(save_pebble(0, gs.pebbles[0], true));       // both copies now good
  CHECK(save_box_header(gs.box));

  PebbleInstance next = gs.pebbles[0];
  snprintf(next.nickname, sizeof next.nickname, "Despues");
  pebble_seal(next);

  advance(2000);
  kv_mem_fail_next_put();
  CHECK(!save_pebble(0, next, true));               // the store refused

  GameState back;
  CHECK_EQ(save_load_all(back), LOAD_OK);
  CHECK_STR_EQ(back.pebbles[0].nickname, "Antes");  // the old save is intact
}

TEST(deferred_write_is_never_dropped) {
  begin();
  GameState gs;
  save_load_all(gs);
  gs.pebbles[0] = sample_pebble(0);
  gs.box.slot_mask = 1; gs.box.active_slot = 0;
  CHECK(save_pebble(0, gs.pebbles[0], true));
  CHECK(save_box_header(gs.box));

  // Inside the 1 s floor: the write is deferred, not performed and not lost.
  advance(100);
  snprintf(gs.pebbles[0].nickname, sizeof gs.pebbles[0].nickname, "Tarde");
  pebble_seal(gs.pebbles[0]);
  const uint32_t before = kv_mem_puts();
  CHECK(save_pebble(0, gs.pebbles[0], true));
  CHECK_EQ(kv_mem_puts(), before);

  save_service();                                   // still inside the floor
  CHECK_EQ(kv_mem_puts(), before);

  advance(SAVE_MIN_GAP_MS + 1);
  save_service();
  CHECK(kv_mem_puts() > before);

  GameState back;
  CHECK_EQ(save_load_all(back), LOAD_OK);
  CHECK_STR_EQ(back.pebbles[0].nickname, "Tarde");
}

TEST(unforced_writes_obey_the_wear_filter) {
  begin();
  GameState gs;
  save_load_all(gs);
  gs.pebbles[0] = sample_pebble(0);
  gs.box.slot_mask = 1; gs.box.active_slot = 0;
  CHECK(save_pebble(0, gs.pebbles[0], true));
  const uint32_t after_first = kv_mem_puts();

  for (int i = 0; i < 50; ++i) {
    advance(1000);
    CHECK(save_pebble(0, gs.pebbles[0], false));
  }
  CHECK_EQ(kv_mem_puts(), after_first);             // 50 s < SAVE_FULL_PERIOD_MS

  advance(SAVE_FULL_PERIOD_MS);
  CHECK(save_pebble(0, gs.pebbles[0], false));
  CHECK(kv_mem_puts() > after_first);
}

// =============================================================================
//  5. Header/slot disagreement: the slot blobs are the truth
// =============================================================================
// =============================================================================
//  THE LOAD PATH IS A SPEC SECTION 15 CONSUMER (P4-C5). save_manager.h used to
//  advertise "runtime validation" over a pipeline whose only check was
//  blob_ok() - magic, CRC and a version byte. It runs game/validate.h's
//  validate_pebble() now, and the rule is QUARANTINE: flag the slot, keep the
//  Pebble, repair nothing.
// =============================================================================
static PebbleInstance valid_pebble(uint8_t slot, uint8_t species, uint8_t level) {
  PebbleInstance p;
  pebble_clear(p);
  p.species_id = species;
  p.id         = 0xC0FFEE00u + slot;
  p.level      = level;
  p.origin     = (uint8_t)ORIGIN_WILD;
  const SpeciesDef* sp = species_get(species);
  memcpy(p.moves, sp->moves, sizeof p.moves);
  p.hp_cur    = xp_hp_max(sp->base_hp, level);
  p.evo_state = sp->stage;
  for (uint8_t i = 0; i < PB_CARE_COUNT; ++i) p.care[i] = (int32_t)PB_CARE_MILLI_MAX;
  p.genome.lineage_id = 0x0BADF00Du + slot;
  p.genome.g0 = 0x1234; p.genome.g1 = 0x5678; p.genome.g2 = 0x09AB;
  genome_seal(p.genome);
  pebble_seal(p);
  return p;
}

TEST(a_valid_box_is_quarantined_nowhere) {
  begin();
  GameState gs;
  CHECK_EQ(save_load_all(gs), LOAD_FRESH);
  gs.pebbles[1] = valid_pebble(1, 1, 5);
  gs.pebbles[6] = valid_pebble(6, 9, 22);            // species 9 is stage 2
  gs.box.slot_mask   = 0x0042u;
  gs.box.active_slot = 1;
  CHECK(save_pebble(1, gs.pebbles[1], true));
  CHECK(save_pebble(6, gs.pebbles[6], true));
  CHECK(save_box_header(gs.box));

  GameState back;
  CHECK_EQ(save_load_all(back), LOAD_OK);
  CHECK_EQ((int)save_quarantine_mask(), 0);
  for (uint8_t slot = 0; slot < BOX_SLOTS; ++slot)
    CHECK_EQ((int)save_quarantine_reason(slot), (int)VR_OK);
  CHECK_EQ((int)validate_pebble(back.pebbles[6]), (int)VR_OK);
}

// -----------------------------------------------------------------------------
//  THE CREATOR RECORD ON THE LOAD PATH (P8-C3).
//
//  THE ORDER save_load_all() RUNS IN IS THE WHOLE POINT: load, then rebuild the
//  creator registry, THEN quarantine_scan(). Move the rebuild after the scan -
//  or delete it - and a Pebble the user made in the creator comes back
//  VR_UNKNOWN_SPECIES on the first power cycle, because species_get(200)
//  resolves through that registry and through nothing else.
// -----------------------------------------------------------------------------
static CustomSpeciesRec creator_record(uint8_t slot) {
  CustomSpeciesRec c;
  memset(&c, 0, sizeof c);
  c.magic   = (uint16_t)CS_MAGIC;
  c.version = (uint8_t)SAVE_SCHEMA_VERSION;
  c.slot    = slot;
  c.type    = (uint8_t)TYPE_SIGNAL;
  c.base[0] = 6; c.base[1] = 5; c.base[2] = 5; c.base[3] = 5;      // 21, in band
  c.moves[0] = 1; c.moves[1] = 6; c.moves[2] = 32; c.moves[3] = 34;
  memcpy(c.name, "Bicho", 6);
  uint16_t stat_used = 0, attack_used = 0;
  creator_cost_of(c, stat_used, attack_used);
  c.budget_used = attack_used;
  custom_species_seal(c);
  return c;
}

static PebbleInstance creator_pebble(uint8_t slot, uint8_t cs_slot) {
  const uint8_t species = (uint8_t)(CREATOR_SPECIES_ID_MIN + cs_slot);
  PebbleInstance p;
  pebble_clear(p);
  p.species_id    = species;
  p.id            = 0xCEA70000u + slot;
  p.level         = 1;
  p.origin        = (uint8_t)ORIGIN_CREATOR;
  p.flags         = (uint8_t)(PBF_CUSTOM | PBF_HAS_CUSTOM_SPRITE);
  p.custom_sprite = cs_slot;
  p.moves[0] = 1; p.moves[1] = 6; p.moves[2] = 32; p.moves[3] = 34;
  p.hp_cur    = xp_hp_max(6u, 1u);        // base_hp 6, the record's first stat
  p.evo_state = 1u;                       // a creator species is stage 1
  for (uint8_t i = 0; i < PB_CARE_COUNT; ++i) p.care[i] = (int32_t)PB_CARE_MILLI_MAX;
  memcpy(p.nickname, "Bicho", 6);
  p.genome.lineage_id = 0x5EED0000u + slot;
  p.genome.g0 = 0x1234; p.genome.g1 = 0x5678; p.genome.g2 = 0x09AB;
  genome_seal(p.genome);
  pebble_seal(p);
  return p;
}

TEST(a_creator_pebble_survives_a_reboot_instead_of_being_quarantined) {
  begin();
  csp_reset();
  GameState gs;
  CHECK_EQ(save_load_all(gs), LOAD_FRESH);

  const CustomSpeciesRec rec = creator_record(3);
  CHECK(save_custom_species(rec));
  gs.pebbles[2] = creator_pebble(2, 3);
  gs.box.slot_mask   = 0x0004u;
  gs.box.active_slot = 2;
  CHECK(save_pebble(2, gs.pebbles[2], true));
  CHECK(save_box_header(gs.box));

  // Forget everything a running device would know, so the reload really is a
  // reboot and not a read of state this process still holds.
  csp_reset();
  CHECK(species_get((uint8_t)(CREATOR_SPECIES_ID_MIN + 3u)) == nullptr);

  GameState back;
  CHECK_EQ(save_load_all(back), LOAD_OK);
  // THE REGISTRY WAS REBUILT BEFORE THE SCAN.
  CHECK(species_get((uint8_t)(CREATOR_SPECIES_ID_MIN + 3u)) != nullptr);
  CHECK_EQ((int)save_quarantine_mask(), 0);
  CHECK_EQ((int)save_quarantine_reason(2), (int)VR_OK);
  CHECK_EQ((int)validate_pebble(back.pebbles[2]), (int)VR_OK);
  CHECK_EQ((int)back.pebbles[2].origin, (int)ORIGIN_CREATOR);
  CHECK_STR_EQ(back.pebbles[2].nickname, "Bicho");
  csp_reset();
}

TEST(a_creator_pebble_whose_record_is_gone_is_quarantined_by_name) {
  // The other direction, and it is what makes the case above mean something: a
  // Pebble whose cs* record was lost or refused must be flagged BY NAME rather
  // than resolved to whatever else happens to be in the registry.
  begin();
  csp_reset();
  GameState gs;
  CHECK_EQ(save_load_all(gs), LOAD_FRESH);
  gs.pebbles[2] = creator_pebble(2, 3);      // no cs3 record is ever written
  gs.box.slot_mask   = 0x0004u;
  gs.box.active_slot = 2;
  CHECK(save_pebble(2, gs.pebbles[2], true));
  CHECK(save_box_header(gs.box));

  GameState back;
  CHECK_EQ(save_load_all(back), LOAD_OK);
  CHECK_EQ((int)save_quarantine_mask(), 0x0004);
  CHECK_EQ((int)save_quarantine_reason(2), (int)VR_UNKNOWN_SPECIES);
  // AND THE PEBBLE IS STILL THERE. Quarantine flags, it never destroys.
  CHECK_EQ((unsigned long)back.pebbles[2].id, 0xCEA70002UL);
  csp_reset();
}

TEST(a_stored_record_that_breaks_the_rules_leaves_its_slot_empty) {
  // "It survived a CRC" is not evidence about a stat total. A hand-written or
  // rotted-but-resealed cs* blob is judged by the SAME validator an upload is,
  // on the load path, and a refused one leaves species_get() answering nullptr.
  begin();
  csp_reset();
  GameState gs;
  CHECK_EQ(save_load_all(gs), LOAD_FRESH);

  CustomSpeciesRec bad = creator_record(3);
  bad.base[0] = 10; bad.base[1] = 10; bad.base[2] = 10; bad.base[3] = 10;  // 40
  custom_species_seal(bad);                 // a perfectly valid CRC
  CHECK(save_custom_species(bad));
  CHECK(custom_species_blob_ok(bad));       // the CRC really does pass

  // LOAD_FRESH, not LOAD_OK: only the cs* record was written, so there is no
  // Box to read - and the registry is still rebuilt, because save_load_all()
  // does it on EVERY outcome rather than on the happy one.
  GameState back;
  CHECK_EQ(save_load_all(back), LOAD_FRESH);
  CHECK_EQ((int)validate_custom_species(bad), (int)VR_CS_STAT_BUDGET);
  CHECK(species_get((uint8_t)(CREATOR_SPECIES_ID_MIN + 3u)) == nullptr);
  csp_reset();
}

TEST(a_load_quarantines_an_invalid_pebble_by_name_and_repairs_nothing) {
  begin();
  GameState gs;
  CHECK_EQ(save_load_all(gs), LOAD_FRESH);
  // sample_pebble() is the LAYOUT fixture: every field is set to something
  // distinctive rather than to something legal, so it carries trait_id 9, a
  // moveset no species teaches and an evo_state that disagrees with its row.
  // That makes it exactly the shape of save this stage exists to notice.
  gs.pebbles[3] = sample_pebble(3);
  gs.pebbles[5] = valid_pebble(5, 1, 5);
  gs.box.slot_mask   = 0x0028u;
  gs.box.active_slot = 5;
  CHECK(save_pebble(3, gs.pebbles[3], true));
  CHECK(save_pebble(5, gs.pebbles[5], true));
  CHECK(save_box_header(gs.box));

  GameState back;
  // THE BOX STILL LOADS. Refusing it would brick a device on a content-pack
  // change, because VR_UNKNOWN_SPECIES is what an older save legitimately
  // produces - so the failure is reported, not acted on.
  CHECK_EQ(save_load_all(back), LOAD_OK);
  CHECK_EQ((int)save_quarantine_mask(), (int)(1u << 3));
  CHECK(save_quarantine_reason(3) != VR_OK);
  CHECK_EQ((int)save_quarantine_reason(3),
           (int)validate_pebble(gs.pebbles[3]));      // the named reason, not a bool
  CHECK_EQ((int)save_quarantine_reason(5), (int)VR_OK);

  // AND NOTHING WAS MENDED: every byte the GAME owns comes back unchanged. The
  // comparison stops at offsetof(seq) because seq and the CRC over it are the
  // PAIR bookkeeping - save_pebble() stamps a fresh sequence number on every
  // write, so those four bytes differ for a reason that has nothing to do with
  // validation.
  CHECK_EQ(memcmp(&back.pebbles[3], &gs.pebbles[3],
                  offsetof(PebbleInstance, seq)), 0);
  CHECK_EQ((int)back.pebbles[3].trait_id, 9);
  CHECK_EQ((int)back.pebbles[3].evo_state, 1);
}

TEST(header_slot_mismatch_self_heals) {
  begin();
  GameState gs;
  save_load_all(gs);
  gs.pebbles[2] = sample_pebble(2);
  gs.box.slot_mask   = 0x0004u;
  gs.box.active_slot = 2;
  CHECK(save_pebble(2, gs.pebbles[2], true));
  CHECK(save_box_header(gs.box));

  // A header that claims four Pebbles and an active slot that never existed.
  gs.box.slot_mask   = 0x000Fu;
  gs.box.active_slot = 7;
  CHECK(save_box_header(gs.box));
  advance(2000);
  CHECK(save_box_header(gs.box));                   // both copies now lie

  GameState back;
  CHECK_EQ(save_load_all(back), LOAD_RECOVERED_PAIR);
  CHECK_EQ(back.box.slot_mask, 0x0004u);            // slot magic wins
  CHECK_EQ(back.box.active_slot, 2);
  CHECK_EQ(back.pebbles[2].id, gs.pebbles[2].id);
  CHECK(pebble_is_empty(back.pebbles[0]));

  GameState again;
  CHECK_EQ(save_load_all(again), LOAD_OK);          // the heal was persisted
  CHECK_EQ(again.box.slot_mask, 0x0004u);
  CHECK_EQ(again.box.active_slot, 2);
}

// =============================================================================
//  6. A save from a newer firmware is refused, not repaired and not wiped
// =============================================================================
TEST(foreign_newer_is_refused_and_nothing_is_written) {
  begin();
  GameState gs;
  save_load_all(gs);
  gs.pebbles[0] = sample_pebble(0);
  gs.box.slot_mask = 1; gs.box.active_slot = 0;
  save_pebble(0, gs.pebbles[0], true);
  save_box_header(gs.box);

  // Forge a box header from schema version 3, sealed correctly.
  BoxHeader future = gs.box;
  future.schema_version = SAVE_SCHEMA_VERSION + 1;
  future.seq = 99;
  const uint16_t crc = crc16_ccitt(&future, BOX_CRC_BYTES);
  memcpy(&future.crc16, &crc, sizeof crc);
  CHECK(kv_put(KV_MAIN, "box1", &future, sizeof future));

  const uint32_t before  = kv_mem_puts();
  const uint16_t keys_be = kv_mem_key_count(KV_MAIN);
  GameState back;
  CHECK_EQ(save_load_all(back), LOAD_FOREIGN_NEWER);
  CHECK_EQ(kv_mem_puts(), before);
  CHECK_EQ(kv_mem_key_count(KV_MAIN), keys_be);
  CHECK(schema_is_foreign_newer(SAVE_SCHEMA_VERSION + 1));
  CHECK(!schema_is_foreign_newer(SAVE_SCHEMA_VERSION));
}

// =============================================================================
//  7. Checkpoint recovery: the Arduino core erased the whole nvs partition
// =============================================================================
TEST(checkpoint_recovers_a_wiped_main_partition) {
  begin();
  GameState gs;
  save_load_all(gs);
  gs.pebbles[0] = sample_pebble(0);
  gs.pebbles[5] = sample_pebble(5);
  gs.box.slot_mask   = 0x0021u;
  gs.box.active_slot = 5;
  gs.cfg.device_id   = 0x12345678u;
  cfgv2_seal(gs.cfg);
  CHECK(save_pebble(0, gs.pebbles[0], true));
  advance(2000);
  CHECK(save_pebble(5, gs.pebbles[5], true));
  CHECK(save_box_header(gs.box));
  CHECK(save_config(gs.cfg));
  CHECK(save_checkpoint_all());

  // initArduino() erases "nvs" before setup() ever runs (audit section 5).
  kv_mem_wipe_partition(KV_MAIN);
  CHECK_EQ(kv_mem_key_count(KV_MAIN), 0);

  GameState back;
  CHECK_EQ(save_load_all(back), LOAD_RECOVERED_CKPT);
  CHECK_EQ(back.box.slot_mask, 0x0021u);
  CHECK_EQ(back.box.active_slot, 5);
  CHECK_EQ(back.pebbles[0].id, gs.pebbles[0].id);
  CHECK_EQ(back.pebbles[5].id, gs.pebbles[5].id);
  CHECK_EQ(back.cfg.device_id, 0x12345678u);

  // The recovered state was committed back to KV_MAIN, so the next boot is OK.
  GameState again;
  CHECK_EQ(save_load_all(again), LOAD_OK);
  CHECK_EQ(again.pebbles[5].id, gs.pebbles[5].id);
}

TEST(both_partitions_empty_is_a_fresh_unit) {
  begin();
  GameState gs;
  CHECK_EQ(save_load_all(gs), LOAD_FRESH);
  CHECK_EQ(gs.box.slot_mask, 0);
  CHECK_EQ(gs.box.active_slot, BOX_ACTIVE_NONE);
  CHECK_EQ(kv_mem_puts(), 0u);                      // a fresh boot writes nothing
  CHECK_EQ(gs.cfg.brightness, OLED_CONTRAST_DEFAULT);
  CHECK_STR_EQ(gs.cfg.tz, CFG_TZ_STRING);
}

TEST(factory_reset_clears_both_partitions) {
  begin();
  GameState gs;
  save_load_all(gs);
  gs.pebbles[0] = sample_pebble(0);
  gs.box.slot_mask = 1; gs.box.active_slot = 0;
  save_pebble(0, gs.pebbles[0], true);
  save_box_header(gs.box);
  save_checkpoint_all();
  CHECK(kv_mem_key_count(KV_MAIN) > 0);
  CHECK(kv_mem_key_count(KV_CKPT) > 0);

  CHECK(save_factory_reset());
  CHECK_EQ(kv_mem_key_count(KV_MAIN), 0);
  CHECK_EQ(kv_mem_key_count(KV_CKPT), 0);
  GameState back;
  CHECK_EQ(save_load_all(back), LOAD_FRESH);
}

// =============================================================================
//  8. The v1 migration, against the committed legacy fixtures
// =============================================================================
static void seed_v1(bool with_config) {
  uint8_t save_bytes[sizeof(LegacyPetSave)];
  CHECK_EQ(load_file("fixtures/petsave_v1_adult.bin", save_bytes, sizeof save_bytes),
           sizeof(LegacyPetSave));
  CHECK(kv_put(KV_MAIN, KEY_V1_SAVE, save_bytes, sizeof save_bytes));
  if (with_config) {
    uint8_t cfg_bytes[sizeof(LegacyConfig)];
    CHECK_EQ(load_file("fixtures/config_v1.bin", cfg_bytes, sizeof cfg_bytes),
             sizeof(LegacyConfig));
    CHECK(kv_put(KV_MAIN, KEY_V1_CFG, cfg_bytes, sizeof cfg_bytes));
  }
}

TEST(v1_fixtures_migrate_with_the_documented_field_map) {
  begin();
  seed_v1(true);
  CHECK(migrate_v1_present());
  CHECK(migration_needed(SAVE_SCHEMA_VERSION_V1));
  CHECK(!migration_needed(SAVE_SCHEMA_VERSION));
  CHECK(!migration_needed(0));

  GameState gs;
  CHECK_EQ(save_load_all(gs), LOAD_MIGRATED);
  CHECK(save_was_migrated());

  const PebbleInstance& p = gs.pebbles[0];
  CHECK_EQ(gs.box.slot_mask, 0x0001u);
  CHECK_EQ(gs.box.active_slot, 0);
  CHECK_EQ(gs.box.schema_version, SAVE_SCHEMA_VERSION);

  // SPECIES: gene_species == 4 in the fixture, so legacy family 4. P4-C1 moved
  // the destination from a literal { 1..8 } table to the generated
  // SPECIES_BASE_OF_FAMILY[], because ids 2 and 3 are STAGES of family 1 and
  // ids 4..8 resolved to nothing at all. Family 4's base stage is id 13
  // (Spamito). The number here IS the proof of that map - update it when the
  // roster grows, do not delete the check.
  CHECK_EQ(p.species_id, migrate_species_of(p.genome));
  CHECK_EQ(p.species_id, 13);
  // stage ADULT -> level 15
  CHECK_EQ(p.level, 15);
  CHECK_EQ(migrate_level_of(LV1_STAGE_EGG),    1);
  CHECK_EQ(migrate_level_of(LV1_STAGE_BABY),   1);
  CHECK_EQ(migrate_level_of(LV1_STAGE_CHILD),  5);
  CHECK_EQ(migrate_level_of(LV1_STAGE_TEEN),  10);
  CHECK_EQ(migrate_level_of(LV1_STAGE_ADULT), 15);
  CHECK_EQ(migrate_level_of(LV1_STAGE_SENIOR),20);

  // stat[] -> care[], reordered, milli-points unchanged.
  CHECK_EQ(p.care[CARE_HUNGER],      73000);
  CHECK_EQ(p.care[CARE_HAPPINESS],   61000);
  CHECK_EQ(p.care[CARE_HEALTH],      92000);
  CHECK_EQ(p.care[CARE_CLEANLINESS], 45000);
  CHECK_EQ(p.care[CARE_ENERGY],      88000);
  // stat_rem[] -> care_rem[], same reorder, remainders unchanged.
  CHECK_EQ(p.care_rem[CARE_HUNGER],       120);
  CHECK_EQ(p.care_rem[CARE_HAPPINESS],   1500);
  CHECK_EQ(p.care_rem[CARE_HEALTH],         0);
  CHECK_EQ(p.care_rem[CARE_CLEANLINESS], 2999);
  CHECK_EQ(p.care_rem[CARE_ENERGY],        33);

  // Genome copied whole, timestamps kept, counters carried.
  CHECK_EQ(p.genome.lineage_id, 0x0BADCAFEu);
  CHECK_EQ(p.genome.crc16, crc16_ccitt(&p.genome, GENOME_CRC_BYTES));
  CHECK_EQ(p.birth_epoch, 1700000000u);
  CHECK_EQ(p.last_updated_epoch, 1700200000u);
  CHECK_EQ(p.age_s, 200000u);
  CHECK_EQ(p.minigames_won, 7);
  // P3-C2b: the light mechanic is gone, so LV1_PF_LIGHT_ON is DROPPED rather
  // than migrated into the reserved bit 0x10.
  CHECK((p.status & PBS_RESERVED_LIGHT) == 0);
  CHECK((p.status & PBS_SICK) == 0);
  CHECK(p.id != 0);
  CHECK(pebble_blob_ok(p));

  // pet_name -> nickname
  CHECK_STR_EQ(p.nickname, "Pebble");

  // Config.tz / brightness carried into ConfigV2, credentials dropped.
  CHECK_STR_EQ(gs.cfg.tz, CFG_TZ_STRING);
  CHECK_EQ(gs.cfg.brightness, 128);
  CHECK_EQ(gs.cfg.time_cal_state, CAL_UNSET);
  CHECK(gs.cfg.last_known_epoch >= 1700200000u);

  // The legacy keys are gone and the v2 pair is in place.
  CHECK(!kv_mem_exists(KV_MAIN, KEY_V1_SAVE));
  CHECK(!kv_mem_exists(KV_MAIN, KEY_V1_CFG));
  CHECK(kv_mem_exists(KV_MAIN, "pb00"));
  CHECK(kv_mem_exists(KV_MAIN, "box0"));

  // Second boot: already v2, nothing to migrate.
  GameState again;
  CHECK_EQ(save_load_all(again), LOAD_OK);
  CHECK(!save_was_migrated());
  CHECK_EQ(again.pebbles[0].id, p.id);
  CHECK_STR_EQ(again.pebbles[0].nickname, "Pebble");
}

// =============================================================================
//  THE LEGACY FAMILY MAP (P4-C1 obligation 5)
//
//  Before P4-C1 this was a literal { 1, 2, 3, 4, 5, 6, 7, 8 } written when ids
//  1..8 were expected to be eight different families' starters. P3-C3 then put
//  the three STAGES of family 1 into ids 1..3, so five of the eight legacy
//  families landed on nothing (species_get() == nullptr: no hp_max, no
//  evolution, a fabricated full HP meter) and two landed mid-family. This is
//  the test that says where they land now.
// =============================================================================
TEST(every_legacy_family_migrates_onto_a_base_stage_species) {
  uint8_t seen[8];
  for (uint8_t legacy = 0; legacy < 8; ++legacy) {
    Genome g;
    memset(&g, 0, sizeof g);
    gene_set_species(g, legacy);
    const uint8_t id = migrate_species_of(g);
    seen[legacy] = id;

    const SpeciesDef* sp = species_get(id);
    CHECK(sp != nullptr);                 // it RESOLVES - five of eight did not
    if (!sp) continue;
    CHECK_EQ(sp->stage, 0);               // and it is a BASE stage, not a middle
    CHECK_EQ(sp->id, species_base_of_family(sp->family));
    CHECK(sp->evo_rule != SPECIES_EVO_NONE);   // so it can still evolve
  }
  // The map is stable under the high bits of the four-bit gene: v1 rolled 0..15
  // and the fold is modulo 8.
  for (uint8_t legacy = 8; legacy < 16; ++legacy) {
    Genome g;
    memset(&g, 0, sizeof g);
    gene_set_species(g, legacy);
    CHECK_EQ(migrate_species_of(g), seen[legacy & 7u]);
  }
  // WITH TWELVE FAMILIES THE EIGHT DESTINATIONS ARE DISTINCT, which is what
  // preserves v1's "different genomes looked different". This is a property of
  // THIS roster size, not a promise the code can keep at any size: at four
  // families the modulo would fold them onto 1/4/7/10 twice over.
  if (SPECIES_FAMILY_COUNT >= 8) {
    for (uint8_t a = 0; a < 8; ++a)
      for (uint8_t b = (uint8_t)(a + 1u); b < 8; ++b)
        CHECK(seen[a] != seen[b]);
  }
}

TEST(a_migrated_pet_arrives_with_a_learnset_and_at_full_health) {
  // Both fields used to be left at zero by migrate_v1_to_v2(): a pet that knew
  // no attacks (0 is the EMPTY move slot) and showed 0 % HP on every screen for
  // ever, because hp_cur is a Pebble's own and xp_hp_rescale() scales it.
  begin();
  seed_v1(true);
  GameState gs;
  CHECK_EQ(save_load_all(gs), LOAD_MIGRATED);
  const PebbleInstance& p = gs.pebbles[0];

  const SpeciesDef* sp = species_get(p.species_id);
  CHECK(sp != nullptr);
  if (!sp) return;
  for (uint8_t i = 0; i < (uint8_t)PB_MOVE_COUNT; ++i) {
    CHECK(p.moves[i] != 0);
    CHECK_EQ(p.moves[i], sp->moves[i]);
  }
  CHECK_EQ(p.hp_cur, xp_hp_max(sp->base_hp, p.level));
  CHECK(p.hp_cur > 0);
}

// -----------------------------------------------------------------------------
//  THE CASE THIS REPLACES, AND WHY IT HAD TO BE REPLACED RATHER THAN KEPT.
//
//  It was v1_without_a_config_gets_the_deterministic_name, and it asserted the
//  property P4-C4's follow-up deliberately reverses: that a v1 pet whose owner
//  never typed a name arrives carrying the DYNASTY NAME as its nickname. That
//  write was display-neutral when it was written (ui.cpp's ui_name_for()
//  produces the same word from the same hash, so an empty nickname drew exactly
//  the same thing) and it stopped being neutral when P4-C4a put the SPECIES
//  NAME in the middle of the display ladder: persistence/game_state.cpp copies
//  pebbles[0].nickname into Config.pet_name, ui_pet_name() answers that first,
//  and nothing ever clears it - so a migrated device showed the dynasty
//  syllables for ever and the species name never appeared once.
//
//  NO GUARD IS WEAKENED. This case holds the OPPOSITE and STRONGER property:
//  the nickname is empty, the fallback word is still bit-for-bit the one the v1
//  UI drew (checked against the hash written out here, since the function that
//  used to produce it is deleted), AND the migrated pet has a real roster row,
//  which is what makes an empty nickname show a species rather than nothing.
// -----------------------------------------------------------------------------
TEST(v1_without_a_config_arrives_unnamed_so_its_species_can_speak) {
  begin();
  seed_v1(false);
  GameState gs;
  CHECK_EQ(save_load_all(gs), LOAD_MIGRATED);

  const PebbleInstance& p = gs.pebbles[0];
  // (1) NO NICKNAME. Nobody typed one, so nothing pretends anybody did.
  CHECK_EQ(p.nickname[0], '\0');

  // (2) The word the ladder falls back to is still the v1 one. UNTIL P9-C4 THIS
  // BLOCK TRANSCRIBED THE HASH BY HAND, because ui.cpp's ui_name_for() includes
  // <Arduino.h> and no host binary could link it - a second implementation of a
  // shipped algorithm living inside a test, and the reason changing a constant
  // in ui.cpp failed nothing anywhere. The hash moved to game/pebble.cpp and
  // this case now CALLS it, so the two cannot drift apart again.
  uint8_t syl[2];
  pebble_name_syllables(p.genome.lineage_id, p.genome.generation, syl);
  CHECK(syl[0] < PB_NAME_SYLLABLES);
  CHECK(syl[1] < PB_NAME_SYLLABLES);
  CHECK(S_SYL_A(syl[0])[0] != '\0');
  CHECK(S_SYL_B(syl[1])[0] != '\0');
  // And the whole word the v1 UI drew, through the same join ui_name_for() now
  // uses - a real string comparison where there used to be two non-empty
  // checks that any hash at all would have satisfied.
  char dynasty[32];
  const uint8_t dn = pebble_name_join(S_SYL_A(syl[0]), S_SYL_B(syl[1]),
                                      dynasty, (uint16_t)sizeof dynasty);
  CHECK(dn > 0);
  // Derived independently from the rule for this fixture's genome
  // (lineage 0x0BADCAFE, generation 0), not transcribed from a run.
  CHECK_STR_EQ(dynasty, "Nuri");

  // (3) And the rung ABOVE that fallback is reachable, which is the whole point
  // of arriving unnamed: the migrated pet has a real roster row, so HOME says
  // what creature it is instead of a dynasty word that never changes.
  const SpeciesDef* sp = species_get(p.species_id);
  CHECK(sp != nullptr);
  if (sp) CHECK(S(sp->name_idx)[0] != '\0');

  CHECK_STR_EQ(gs.cfg.tz, CFG_TZ_STRING);         // defaults, not garbage
}

// A v1 owner who DID type a name keeps it, which is the rung above the species
// and the reason the nickname field exists at all.
TEST(v1_with_a_typed_name_keeps_it) {
  begin();
  seed_v1(true);
  GameState gs;
  CHECK_EQ(save_load_all(gs), LOAD_MIGRATED);
  CHECK_STR_EQ(gs.pebbles[0].nickname, "Pebble");
}

TEST(v1_egg_fixture_migrates_to_level_one) {
  begin();
  uint8_t save_bytes[sizeof(LegacyPetSave)];
  CHECK_EQ(load_file("fixtures/petsave_v1_egg.bin", save_bytes, sizeof save_bytes),
           sizeof(LegacyPetSave));
  GameState gs;
  CHECK_EQ(migrate_v1_to_v2(save_bytes, nullptr, gs), MIGRATE_OK);
  CHECK_EQ(gs.pebbles[0].level, 1);
  CHECK_EQ(gs.pebbles[0].genome.lineage_id, 0x00C0FFEEu);
  for (uint8_t i = 0; i < PB_CARE_COUNT; ++i) {
    CHECK_EQ(gs.pebbles[0].care[i], LEGACY_STAT_MILLI_MAX);
  }
  CHECK_EQ(gs.pebbles[0].origin, ORIGIN_STARTER);
  CHECK(pebble_blob_ok(gs.pebbles[0]));
}

TEST(a_rotten_v1_blob_is_corrupt_not_a_fresh_pet) {
  begin();
  seed_v1(true);
  CHECK(kv_mem_corrupt(KEY_V1_SAVE, 60));           // inside the genome

  const uint32_t before = kv_mem_puts();
  GameState gs;
  CHECK_EQ(save_load_all(gs), LOAD_CORRUPT);
  CHECK_EQ(kv_mem_puts(), before);
  CHECK(kv_mem_exists(KV_MAIN, KEY_V1_SAVE));       // the v1 save is still there

  uint8_t bad[sizeof(LegacyPetSave)];
  memset(bad, 0, sizeof bad);
  CHECK_EQ(migrate_v1_to_v2(bad, nullptr, gs), MIGRATE_BAD_BLOB);
  CHECK_EQ(migrate_v1_to_v2(nullptr, nullptr, gs), MIGRATE_NONE);
}

TEST(the_checkpoint_cadence_is_daily_and_event_driven) {
  begin();
  GameState gs;
  save_load_all(gs);
  gs.pebbles[0] = sample_pebble(0);
  gs.box.slot_mask = 1; gs.box.active_slot = 0;
  CHECK(save_pebble(0, gs.pebbles[0], true));
  CHECK(save_box_header(gs.box));

  // First call on a unit with no checkpoint at all: writes immediately.
  CHECK(save_checkpoint_service(s_epoch, false));
  CHECK(kv_mem_exists(KV_CKPT, KEY_CK_BOX));

  // Same day: nothing.
  CHECK(!save_checkpoint_service(s_epoch + 3600u, false));
  CHECK(!save_checkpoint_service(s_epoch + SAVE_CKPT_PERIOD_S - 1u, false));

  // An event writes one whatever the clock says ...
  const uint32_t puts_before = kv_mem_puts();
  CHECK(save_checkpoint_service(s_epoch + 3600u, true));
  CHECK(kv_mem_puts() > puts_before);

  // ... and a day later the ordinary cadence fires again.
  CHECK(save_checkpoint_service(s_epoch + SAVE_CKPT_PERIOD_S + 3600u, false));

  // An epoch that is not a date never triggers the daily write.
  CHECK(!save_checkpoint_service(1000u, false));
}

TEST(the_gain_ledger_is_left_alone_by_the_migration) {
  begin();
  uint8_t gain[sizeof(LegacyGainSave)];
  CHECK_EQ(load_file("fixtures/gainsave_v1.bin", gain, sizeof gain),
           sizeof(LegacyGainSave));
  CHECK(kv_put(KV_MAIN, KEY_GAIN, gain, sizeof gain));
  seed_v1(true);

  GameState gs;
  CHECK_EQ(save_load_all(gs), LOAD_MIGRATED);

  uint8_t back[sizeof(LegacyGainSave)];
  CHECK_EQ(kv_get(KV_MAIN, KEY_GAIN, back, sizeof back), (int)sizeof back);
  CHECK_EQ(memcmp(back, gain, sizeof gain), 0);     // byte for byte, untouched
}


// =============================================================================
//  THE TWO SLOT-WRITE PATHS, AND WHY THERE ARE TWO (P7-C6, the blocking find)
//
//  save_pebble(force=true) DEFERS a second write of one key inside
//  SAVE_MIN_GAP_MS and returns TRUE - correct for the care loop, which calls it
//  on every action and relies on save_service() to flush. It is a LIE to any
//  caller whose contract is "the bytes landed", which is why game/trade.cpp's
//  store seam goes through save_pebble_now() instead. Both halves are pinned
//  here, because the whole defect was that only one of them had a reader.
// =============================================================================
// Reads BOTH copies of a slot's pair straight out of the fake store, so a case
// can ask "are these bytes on flash right now?" without going through
// save_load_all() - which rebinds the module and would itself write.
static bool flash_slot_has_level(uint8_t slot, uint8_t level) {
  for (uint8_t copy = 0; copy < 2u; ++copy) {
    char key[KV_KEY_CAP];
    key[0] = 'p'; key[1] = 'b';
    key[2] = (char)('0' + slot); key[3] = (char)('0' + copy); key[4] = '\0';
    PebbleInstance p;
    if (kv_get(KV_MAIN, key, &p, sizeof p) != (int)sizeof p) continue;
    if (p.level == level) return true;
  }
  return false;
}

// A whole device with one Pebble in 'slot', so save_load_all() can read it back.
static void seed_one_slot(GameState& gs, uint8_t slot) {
  CHECK_EQ(save_load_all(gs), LOAD_FRESH);
  save_bind(gs);
  gs.box.slot_mask   = (uint16_t)(1u << slot);
  gs.box.active_slot = slot;
  CHECK(save_box_header(gs.box));
  CHECK(save_config(gs.cfg));
  CHECK(save_inventory(gs.inv));
  CHECK(save_cooldowns(gs.cds));
  CHECK(save_trade_journal(gs.trade));
}

TEST(a_second_write_of_one_slot_inside_the_floor_is_deferred_and_says_so) {
  begin();
  GameState gs;
  seed_one_slot(gs, 3u);

  const PebbleInstance first = sample_pebble(3);
  gs.pebbles[3] = first;
  CHECK(save_pebble(3, first, true));
  CHECK(save_pebble_landed());                 // the first write always lands

  PebbleInstance second = first;
  second.level = (uint8_t)(first.level + 5u);
  gs.pebbles[3] = second;

  advance(SAVE_MIN_GAP_MS / 2u);               // still inside the floor
  CHECK(save_pebble(3, second, true));         // "true" - and it did NOT land
  CHECK(!save_pebble_landed());

  // FLASH STILL HOLDS THE FIRST BYTES. This is the exact shape of the defect
  // P7-C6 found: a caller that read only the return value would have believed
  // otherwise, written a Box header over it and cleared its journal.
  CHECK(flash_slot_has_level(3u, first.level));
  CHECK(!flash_slot_has_level(3u, second.level));

  // save_service() is what makes the deferral honest for the care loop.
  advance(SAVE_MIN_GAP_MS);
  save_service();
  CHECK(save_pebble_landed());
  CHECK(flash_slot_has_level(3u, second.level));

  GameState back;
  CHECK_EQ((int)save_load_all(back), (int)LOAD_OK);
  CHECK_EQ((int)back.pebbles[3].level, (int)second.level);
}

TEST(save_pebble_now_reaches_flash_twice_inside_the_floor_and_never_says_it_did_not) {
  begin();
  GameState gs;
  seed_one_slot(gs, 2u);

  // THE TRADE'S SHAPE: B1 clears the outgoing slot, B2 files the incoming one
  // into the slot B1 just released, microseconds apart. Both must land.
  PebbleInstance out = sample_pebble(2);
  gs.pebbles[2] = out;
  CHECK(save_pebble_now(2, out));
  CHECK(save_pebble_landed());

  PebbleInstance mid = sample_pebble(5);
  mid.id = 0xFEED0001u;
  gs.pebbles[2] = mid;
  const uint32_t puts_b1 = kv_mem_puts();
  CHECK(save_pebble_now(2, mid));              // B1, same key, same millisecond
  CHECK(save_pebble_landed());
  CHECK(kv_mem_puts() > puts_b1);              // it really touched the store

  PebbleInstance in = sample_pebble(9);
  in.id = 0xFEED0002u;
  gs.pebbles[2] = in;
  const uint32_t puts_b2 = kv_mem_puts();
  CHECK(save_pebble_now(2, in));               // B2, same key, same millisecond
  CHECK(save_pebble_landed());
  CHECK(kv_mem_puts() > puts_b2);              // and so did this one

  CHECK(flash_slot_has_level(2u, in.level));   // the LAST write is what is there

  // AND IT CANCELS A PENDING DEFERRAL. What that buys is ONE FEWER FLASH
  // WRITE, not correctness - save_service() would have written the same RAM -
  // so the assertion below counts puts rather than comparing bytes, which is
  // the only thing that can actually tell the two behaviours apart.
  gs.box.slot_mask = (uint16_t)(gs.box.slot_mask | (1u << 4));
  CHECK(save_box_header(gs.box));
  PebbleInstance stale = sample_pebble(1);
  stale.id = 0xFEED0003u;
  gs.pebbles[4] = stale;
  CHECK(save_pebble(4, stale, true));
  PebbleInstance newer = stale;
  newer.level = (uint8_t)(stale.level + 3u);
  gs.pebbles[4] = newer;
  CHECK(save_pebble(4, newer, true));          // deferred
  CHECK(!save_pebble_landed());
  PebbleInstance traded = sample_pebble(6);
  traded.id = 0xFEED0004u;
  gs.pebbles[4] = traded;
  CHECK(save_pebble_now(4, traded));
  advance(SAVE_MIN_GAP_MS * 2u);
  const uint32_t puts_svc = kv_mem_puts();
  save_service();                              // must be a no-op for slot 4
  CHECK_EQ(kv_mem_puts(), puts_svc);
  CHECK(flash_slot_has_level(4u, traded.level));

  GameState back;
  CHECK_EQ((int)save_load_all(back), (int)LOAD_OK);
  CHECK_EQ(back.pebbles[2].id, 0xFEED0002u);
  CHECK_EQ(back.pebbles[4].id, 0xFEED0004u);
}

TEST(a_slot_out_of_range_is_refused_by_both_write_paths) {
  begin();
  GameState gs;
  seed_one_slot(gs, 0u);
  const PebbleInstance p = sample_pebble(0);
  CHECK(!save_pebble((uint8_t)BOX_SLOTS, p, true));
  CHECK(!save_pebble_now((uint8_t)BOX_SLOTS, p));
  CHECK(!save_pebble_landed());
}
