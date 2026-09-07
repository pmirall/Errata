// =============================================================================
//  Pebblebol host tests - test_fixtures.cpp
//  The committed legacy v1 blobs (tests/fixtures/*.bin, written by
//  legacy/mkfixtures.cpp) are byte images of the v1 LegacyPetSave / Config /
//  GainSave structs. This test pins them: size, magic, version, the stored
//  crc16 field against crc16_ccitt() over the v1 CRC span, and a whole-file
//  CRC so an accidental regeneration is noticed. The SaveSchema v2 migration
//  (P2-C9) reads these same files.
//
//  P2-C7 retired several LegacyPetSave fields into pad_* members at the same offsets,
//  so the v1 bytes are unchanged and are read back through the pads here.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "core/nt_types.h"
#include "persistence/legacy_v1.h"    // the frozen v1 PetSave / Config / GainSave
#include "core/crc16.h"

static uint8_t s_blob[512];

static size_t load(const char* rel) {
  char path[512];
  nt_path(rel, path, sizeof path);
  FILE* f = fopen(path, "rb");
  if (!f) {
    fprintf(stderr, "  cannot open %s\n", path);
    return 0;
  }
  const size_t n = fread(s_blob, 1, sizeof s_blob, f);
  fclose(f);
  return n;
}

static uint16_t u16_at(size_t off) {
  return (uint16_t)(s_blob[off] | ((uint16_t)s_blob[off + 1] << 8));   // little-endian wire
}

TEST(fixture_petsave_v1_adult) {
  const size_t n = load("fixtures/petsave_v1_adult.bin");
  CHECK_EQ(n, 128);
  if (n != sizeof(LegacyPetSave)) return;
  LegacyPetSave p;
  memcpy(&p, s_blob, sizeof p);
  CHECK_EQ(p.magic, LEGACY_SAVE_MAGIC);
  CHECK_EQ(p.version, LEGACY_SAVE_VERSION);
  CHECK_EQ(p.stage, STAGE_ADULT);
  CHECK_EQ(p.pad_adult_form, 0);            // v1 adult_form = FORM_BOLOTA
  CHECK_EQ(p.stat[ST_HUNGER], 73000);
  CHECK_EQ(p.pad_stat, 55000);              // v1 stat[ST_DISCIPLINE]
  CHECK_EQ(p.age_s, 200000u);
  CHECK_EQ(p.cq, 640);
  CHECK_EQ(p.flags, LV1_PF_LIGHT_ON);   // the frozen v1 bit, not a live one
  CHECK_EQ(p.genome.lineage_id, 0x0BADCAFEu);
  CHECK_EQ(p.genome.crc16, crc16_ccitt(&p.genome, GENOME_CRC_BYTES));
  CHECK_EQ(p.crc16, crc16_ccitt(&p, LEGACY_PETSAVE_CRC_BYTES));
  CHECK_EQ(u16_at(offsetof(LegacyPetSave, crc16)), 0xD65E);      // stored crc16 field
  CHECK_EQ(crc16_ccitt(s_blob, n), 0x82B1);                 // whole file
}

TEST(fixture_petsave_v1_egg) {
  const size_t n = load("fixtures/petsave_v1_egg.bin");
  CHECK_EQ(n, 128);
  if (n != sizeof(LegacyPetSave)) return;
  LegacyPetSave p;
  memcpy(&p, s_blob, sizeof p);
  CHECK_EQ(p.magic, LEGACY_SAVE_MAGIC);
  CHECK_EQ(p.version, LEGACY_SAVE_VERSION);
  CHECK_EQ(p.stage, STAGE_EGG);
  CHECK_EQ(p.pad_adult_form, 0xFF);         // v1 adult_form = FORM_UNSET
  CHECK_EQ(p.age_s, 0u);
  CHECK_EQ(p.egg_epoch, 1700000000u);
  for (int i = 0; i < ST_COUNT; i++) CHECK_EQ(p.stat[i], STAT_MILLI_MAX);
  CHECK_EQ(p.pad_stat, STAT_MILLI_MAX);     // v1 stat[ST_DISCIPLINE]
  CHECK_EQ(p.genome.lineage_id, 0x00C0FFEEu);
  CHECK_EQ(p.genome.crc16, crc16_ccitt(&p.genome, GENOME_CRC_BYTES));
  CHECK_EQ(p.crc16, crc16_ccitt(&p, LEGACY_PETSAVE_CRC_BYTES));
  CHECK_EQ(u16_at(offsetof(LegacyPetSave, crc16)), 0xFE27);
  CHECK_EQ(crc16_ccitt(s_blob, n), 0xF5EB);
}

TEST(fixture_config_v1) {
  const size_t n = load("fixtures/config_v1.bin");
  CHECK_EQ(n, 256);
  if (n != sizeof(Config)) return;
  Config c;
  memcpy(&c, s_blob, sizeof c);
  CHECK_EQ(c.magic, NT_CFG_MAGIC);
  CHECK_EQ(c.version, NT_CFG_VERSION);
  // 0x04 IS CF_RESERVED_BLE SINCE P8-C0, AND THIS LINE IS WHY IT WAS RESERVED
  // RATHER THAN REUSED: the fixture blob was written when the bit meant "BLE
  // enabled", and it still has it set. The macro was renamed; its value is the
  // same 0x04 and the bytes on disk are untouched.
  CHECK_EQ(c.flags, CF_RESERVED_BLE | CF_WEB_ENABLED);
  CHECK_STR_EQ(c.wifi_ssid, "legacy-ssid");
  CHECK_STR_EQ(c.pet_name, "Pebble");
  CHECK_STR_EQ(c.tz, CFG_TZ_STRING);
  // v1 kept lat[12] at offset 224; reserved_b[] holds those retired bytes.
  CHECK_STR_EQ((const char*)c.reserved_b, "41.3874");
  CHECK_STR_EQ((const char*)c.reserved_b + 12, "2.1686");
  CHECK_EQ(c.brightness, 128);
  CHECK_EQ(c.crc16, crc16_ccitt(&c, CONFIG_CRC_BYTES));
  CHECK_EQ(u16_at(offsetof(Config, crc16)), 0xB153);
  CHECK_EQ(crc16_ccitt(s_blob, n), 0xABBC);
}

TEST(fixture_gainsave_v1) {
  const size_t n = load("fixtures/gainsave_v1.bin");
  CHECK_EQ(n, 20);
  if (n != sizeof(LegacyGainSave)) return;
  LegacyGainSave g;
  memcpy(&g, s_blob, sizeof g);
  CHECK_EQ(g.magic, LEGACY_GAIN_MAGIC);
  CHECK_EQ(g.version, LEGACY_GAIN_VERSION);
  CHECK_EQ(g.slots, 7);                     // the v1 blob carried seven StatIds
  CHECK_EQ(g.epoch, 1700200000u);
  CHECK_EQ(g.pts[LV1_ST_HUNGER], 30);
  CHECK_EQ(g.pts[LV1_ST_HYGIENE], 25);
  CHECK_EQ(g.pts[LV1_ST_HEALTH], 0);
  CHECK_EQ(g.crc16, crc16_ccitt(&g, LEGACY_GAINSAVE_CRC_BYTES));
  CHECK_EQ(u16_at(offsetof(LegacyGainSave, crc16)), 0x77F0);
  CHECK_EQ(crc16_ccitt(s_blob, n), 0x6360);
}

TEST(fixture_crc_rejects_a_corrupted_byte) {
  const size_t n = load("fixtures/petsave_v1_adult.bin");
  CHECK_EQ(n, 128);
  if (n != sizeof(LegacyPetSave)) return;
  s_blob[40] ^= 0x01;                                       // v1 death_epoch, bit 0
  LegacyPetSave p;
  memcpy(&p, s_blob, sizeof p);
  CHECK(p.crc16 != crc16_ccitt(&p, LEGACY_PETSAVE_CRC_BYTES));
}
