// =============================================================================
//  Pebblebol host tests - test_compat.cpp
//  persistence/save_compat.cpp: the TEMPORARY bridge between SaveSchema v2 on
//  flash and the v1 PetSave/Config the simulation still runs on until P2-C10.
//
//  It is temporary and it is still the only thing standing between a v1 owner's
//  pet and SaveSchema v2, so it gets the same treatment as the permanent code:
//    * a v1 save migrates and comes back as the SAME animal;
//    * a save/load round trip is lossless, including the v1-only fields
//      (care quality, the wish, poop) that PebbleInstance does not carry;
//    * losing the companion blob degrades to slot 0 rather than to a new pet;
//    * a refused load (LOAD_CORRUPT) writes NOTHING - the one rule the whole
//      P2-C9 pipeline exists to enforce.
//  This file is deleted together with save_compat.cpp in P2-C10.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "fakes/kv_mem.h"
#include "fakes/boot_host.h"
#include "persistence/save_compat.h"
#include "persistence/save_manager.h"
#include "persistence/legacy_v1.h"
#include "core/crc16.h"

static uint32_t s_ms    = 0;
static uint32_t s_epoch = 1700300000u;
static uint32_t fake_ms(void)    { return s_ms; }
static uint32_t fake_epoch(void) { return s_epoch; }

static void begin(void) {
  kv_mem_reset();
  boot_host_reset();
  s_ms    = 10000;
  s_epoch = 1700300000u;
  save_set_clock(&fake_ms, &fake_epoch);
  compat_set_readonly(false);
}

static size_t load_file(const char* rel, uint8_t* out, size_t cap) {
  char path[512];
  nt_path(rel, path, sizeof path);
  FILE* f = fopen(path, "rb");
  if (!f) { fprintf(stderr, "  cannot open %s\n", path); return 0; }
  const size_t n = fread(out, 1, cap, f);
  fclose(f);
  return n;
}

// Seeds KV_MAIN with the committed v1 blobs, exactly as hardware/kv_nvs.cpp's
// one-shot import leaves them for the migration.
static bool seed_v1(void) {
  uint8_t save_bytes[sizeof(LegacyPetSave)];
  uint8_t cfg_bytes[sizeof(LegacyConfig)];
  if (load_file("fixtures/petsave_v1_adult.bin", save_bytes, sizeof save_bytes)
      != sizeof save_bytes) return false;
  if (load_file("fixtures/config_v1.bin", cfg_bytes, sizeof cfg_bytes)
      != sizeof cfg_bytes) return false;
  return kv_put(KV_MAIN, KEY_V1_SAVE, save_bytes, sizeof save_bytes) &&
         kv_put(KV_MAIN, KEY_V1_CFG, cfg_bytes, sizeof cfg_bytes);
}

TEST(compat_migrates_a_v1_save_into_the_live_pet) {
  begin();
  if (!seed_v1()) { CHECK(false); return; }

  PetSave pet;
  Config  cfg;
  const LoadResult r = compat_load(pet, cfg);
  CHECK_EQ((int)r, (int)LOAD_MIGRATED);
  CHECK(compat_have_pet());
  CHECK(!compat_readonly());

  // The pet that came back is the migrated slot 0, field for field.
  const PebbleInstance& p = compat_state().pebbles[0];
  CHECK(!pebble_is_empty(p));
  CHECK_EQ(pet.stat[ST_HUNGER],    p.care[CARE_HUNGER]);
  CHECK_EQ(pet.stat[ST_HAPPINESS], p.care[CARE_HAPPINESS]);
  CHECK_EQ(pet.stat[ST_HYGIENE],   p.care[CARE_CLEANLINESS]);
  CHECK_EQ(pet.age_s,              p.age_s);
  CHECK_EQ(pet.birth_epoch,        p.birth_epoch);
  CHECK_EQ(memcmp(&pet.genome, &p.genome, sizeof(Genome)), 0);
  // stage <- level: the adult fixture migrates to level 15, back to ADULT.
  CHECK_EQ(p.level, 15);
  CHECK_EQ(pet.stage, (uint8_t)STAGE_ADULT);
  // The v1 keys are gone and the pet is on flash twice over.
  CHECK(!kv_mem_exists(KV_MAIN, KEY_V1_SAVE));
  CHECK(kv_mem_exists(KV_MAIN, "pb00") || kv_mem_exists(KV_MAIN, "pb01"));
}

TEST(compat_round_trip_keeps_the_fields_v2_does_not_carry) {
  begin();
  if (!seed_v1()) { CHECK(false); return; }

  PetSave pet;
  Config  cfg;
  CHECK_EQ((int)compat_load(pet, cfg), (int)LOAD_MIGRATED);

  // Fields with no home in PebbleInstance: they survive only because the
  // companion blob does.
  pet.cq          = 812;
  pet.wish_id     = 3;
  pet.wish_left_s = 640;
  pet.poop_count  = 2;
  pet.snacks_total = 47;
  pet.events_done = 0x15;
  pet.stat[ST_HUNGER] = 61234;
  s_ms += 2000;
  CHECK(compat_save_pet(pet, true));
  CHECK(kv_mem_exists(KV_MAIN, KEY_COMPAT_PET));

  PetSave back;
  Config  cfg2;
  CHECK_EQ((int)compat_load(back, cfg2), (int)LOAD_OK);
  CHECK(compat_have_pet());
  CHECK_EQ(back.cq, 812);
  CHECK_EQ(back.wish_id, 3);
  CHECK_EQ(back.wish_left_s, 640);
  CHECK_EQ(back.poop_count, 2);
  CHECK_EQ(back.snacks_total, 47);
  CHECK_EQ(back.events_done, 0x15);
  CHECK_EQ(back.stat[ST_HUNGER], 61234);
  // ... and slot 0 followed the same write.
  CHECK_EQ(compat_state().pebbles[0].care[CARE_HUNGER], 61234);
}

TEST(compat_rebuilds_the_pet_from_slot0_when_the_companion_blob_is_gone) {
  begin();
  if (!seed_v1()) { CHECK(false); return; }

  PetSave pet;
  Config  cfg;
  CHECK_EQ((int)compat_load(pet, cfg), (int)LOAD_MIGRATED);
  pet.stat[ST_HAPPINESS] = 44000;
  pet.cq = 777;
  s_ms += 2000;
  CHECK(compat_save_pet(pet, true));

  const uint32_t id_before = compat_state().pebbles[0].id;
  CHECK(kv_erase(KV_MAIN, KEY_COMPAT_PET));

  PetSave back;
  Config  cfg2;
  CHECK_EQ((int)compat_load(back, cfg2), (int)LOAD_OK);
  CHECK(compat_have_pet());                       // a pet, not a new egg
  CHECK_EQ(back.stat[ST_HAPPINESS], 44000);       // v2 carries this one
  CHECK_EQ(back.cq, 500);                         // v2 does not: documented default
  CHECK_EQ(compat_state().pebbles[0].id, id_before);
}

TEST(compat_never_writes_after_a_refused_load) {
  begin();
  if (!seed_v1()) { CHECK(false); return; }

  PetSave pet;
  Config  cfg;
  CHECK_EQ((int)compat_load(pet, cfg), (int)LOAD_MIGRATED);
  s_ms += 2000;
  CHECK(compat_save_pet(pet, true));

  // Rot both copies of the Box: the load must refuse, and refuse in silence.
  CHECK(kv_mem_corrupt("box0", 6));
  CHECK(kv_mem_corrupt("box1", 6));

  PetSave pet2;
  Config  cfg2;
  const LoadResult r = compat_load(pet2, cfg2);
  CHECK_EQ((int)r, (int)LOAD_CORRUPT);
  CHECK(compat_readonly());
  CHECK(!compat_have_pet());

  const uint32_t puts_before = kv_mem_puts();
  CHECK(!compat_save_pet(pet, true));
  CHECK(!compat_save_cfg(cfg));
  compat_touch_lastseen(s_epoch);
  CHECK_EQ(kv_mem_puts(), puts_before);      // not one byte reached flash

  // A factory reset is the only thing that clears it, and it is explicit.
  CHECK(compat_factory_reset());
  CHECK(!compat_readonly());
}

TEST(compat_config_maps_both_ways) {
  begin();
  // A pet first: save_load_all() reports LOAD_FRESH and reads nothing else when
  // KV_MAIN holds no Box at all, so a config on its own is not a save. On a
  // real unit the first boot writes both, which is what this reproduces.
  if (!seed_v1()) { CHECK(false); return; }
  PetSave pet;
  Config  cfg;
  CHECK_EQ((int)compat_load(pet, cfg), (int)LOAD_MIGRATED);

  cfg.brightness     = 96;
  cfg.statusbar_mode = (uint8_t)SBAR_TEXT;
  cfg.flags          = (uint8_t)(CF_MUTE | CF_WEB_ENABLED);
  snprintf(cfg.pet_name, sizeof cfg.pet_name, "Pebo");
  snprintf(cfg.tz, sizeof cfg.tz, "UTC0");
  CHECK(compat_save_cfg(cfg));

  const ConfigV2& v2 = compat_state().cfg;
  CHECK_EQ(v2.brightness, 96);
  CHECK((v2.flags & CFGV2_F_MUTE) != 0);
  CHECK((v2.flags & CFGV2_F_WEB) != 0);
  CHECK((v2.flags & CFGV2_F_BLE) == 0);
  CHECK_EQ((v2.flags & CFGV2_F_SBAR_MASK) >> CFGV2_F_SBAR_SH, (uint16_t)SBAR_TEXT);
  CHECK_STR_EQ(v2.device_name, "Pebo");
  CHECK_STR_EQ(v2.tz, "UTC0");

  PetSave pet2;
  Config  back;
  CHECK_EQ((int)compat_load(pet2, back), (int)LOAD_OK);
  CHECK_EQ(back.brightness, 96);
  CHECK_EQ(back.statusbar_mode, (uint8_t)SBAR_TEXT);
  CHECK((back.flags & CF_MUTE) != 0);
  CHECK((back.flags & CF_WEB_ENABLED) != 0);
  CHECK_STR_EQ(back.pet_name, "Pebo");
  CHECK_STR_EQ(back.tz, "UTC0");

  char tz[CFGV2_TZ_CAP];
  compat_boot_tz(tz, sizeof tz);
  CHECK_STR_EQ(tz, "UTC0");
}

TEST(compat_gain_ledger_survives_a_write_and_refuses_a_foreign_one) {
  begin();
  uint8_t pts[COMPAT_GAIN_SLOTS];
  uint32_t epoch = 0;
  CHECK(!compat_load_gain(pts, epoch));       // nothing stored yet

  LegacyGainSave g;
  memset(&g, 0, sizeof g);
  g.magic   = (uint16_t)LEGACY_GAIN_MAGIC;
  g.version = (uint8_t)LEGACY_GAIN_VERSION;
  g.slots   = COMPAT_GAIN_SLOTS;
  g.epoch   = 1700200000u;
  g.pts[ST_HUNGER]  = 30;
  g.pts[ST_HYGIENE] = 25;
  g.crc16 = crc16_ccitt(&g, LEGACY_GAINSAVE_CRC_BYTES);
  CHECK(kv_put(KV_MAIN, KEY_GAIN, &g, sizeof g));

  CHECK(compat_load_gain(pts, epoch));
  CHECK_EQ(epoch, 1700200000u);
  CHECK_EQ(pts[ST_HUNGER], 30);
  CHECK_EQ(pts[ST_HYGIENE], 25);

  // A blob written by a build with a different StatId count is refused whole,
  // never reinterpreted byte by byte.
  g.slots = (uint8_t)(COMPAT_GAIN_SLOTS + 1u);
  g.crc16 = crc16_ccitt(&g, LEGACY_GAINSAVE_CRC_BYTES);
  CHECK(kv_put(KV_MAIN, KEY_GAIN, &g, sizeof g));
  CHECK(!compat_load_gain(pts, epoch));
  CHECK_EQ(epoch, 0u);
}

TEST(compat_mirrors_the_last_seen_epoch_into_rtc) {
  begin();
  PetSave pet;
  Config  cfg;
  CHECK_EQ((int)compat_load(pet, cfg), (int)LOAD_FRESH);
  compat_touch_lastseen(1700400000u);
  CHECK_EQ(boot_host_mirror(), 1700400000u);
  CHECK_EQ(save_last_seen(), 1700400000u);
}

