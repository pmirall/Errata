// =============================================================================
//  Pebblebol host tests - legacy/mkfixtures.cpp
//  Writes the v1 persistence fixtures the SaveSchema v2 migration (P2-C9) is
//  tested against: byte images of the legacy PetSave / Config / GainSave
//  structs exactly as the v1 firmware put them into NVS.
//
//      make -C tests fixtures        ->  tests/fixtures/*.bin
//
//  The fixtures are committed; regenerate only when the legacy layout itself
//  is the subject of the commit. Every field is set explicitly so the output
//  is a pure function of this file (no clock, no RNG).
//
//  P2-C7 retired several LegacyPetSave fields into pad_* members at the SAME offsets
//  and dropped ST_DISCIPLINE from StatId. The v1 bytes did not move, so this
//  file writes the retired values through the pads and the blobs it emits are
//  still byte-identical to the committed ones.
// =============================================================================
#include <stdio.h>
#include <string.h>

#include "core/nt_types.h"
#include "persistence/legacy_v1.h"    // the frozen v1 PetSave / Config / GainSave
#include "game/genome.h"
#include "core/crc16.h"

static_assert(sizeof(LegacyPetSave) == 128, "legacy PetSave is 128 B");
static_assert(sizeof(Config) == 256, "legacy Config is 256 B");
static_assert(sizeof(LegacyGainSave) == 20, "legacy GainSave is 20 B");

static void copy_str(char* dst, size_t cap, const char* src) {
  snprintf(dst, cap, "%s", src);
}

// A fully specified, valid legacy genome (no RNG involved).
static Genome fixture_genome(uint32_t lineage) {
  Genome g;
  memset(&g, 0, sizeof g);
  g.lineage_id = lineage;
  gene_set_species(g, 4);
  gene_set_pattern(g, 9);
  gene_set_palette(g, 2);
  gene_set_body_size(g, 5);
  gene_set_ear_horn(g, 1);
  gene_set_appetite(g, 7);
  gene_set_metabolism(g, 8);
  gene_set_sociability(g, 10);
  gene_set_temperament(g, 6);
  gene_set_hardiness(g, 9);
  gene_set_luck(g, 3);
  gene_set_mutations(g, 2);
  gene_set_sex(g, 1);
  gene_set_rare(g, 0);
  gene_set_tainted(g, 0);
  genome_seal(g);
  return g;
}

static LegacyPetSave petsave_adult(void) {
  LegacyPetSave p;
  memset(&p, 0, sizeof p);
  p.magic   = LEGACY_SAVE_MAGIC;
  p.version = LEGACY_SAVE_VERSION;
  p.stage   = STAGE_ADULT;
  static const int32_t stats[ST_COUNT] = { 73000, 61000, 88000, 45000, 92000, 70000 };
  memcpy(p.stat, stats, sizeof stats);
  p.pad_stat            = 55000;            // v1 stat[ST_DISCIPLINE]
  p.birth_epoch         = 1700000000u;
  p.last_seen_epoch     = 1700200000u;
  p.pad_death_epoch     = 0u;               // v1 death_epoch
  p.egg_epoch           = 1699990000u;
  p.last_interact_epoch = 1700199000u;
  p.age_s               = 200000u;
  p.genome              = fixture_genome(0x0BADCAFEu);
  static const int16_t rem[ST_COUNT] = { 120, 1500, 33, 2999, 0, 7 };
  memcpy(p.stat_rem, rem, sizeof rem);
  p.pad_stat_rem   = 3598;                  // v1 stat_rem[ST_DISCIPLINE]
  p.cq             = 640;
  p.pad_weight     = 420;                   // v1 weight_dg
  // v1 dmg_acc[5] (uint16 LE) then care_miss (uint16 LE), 12 bytes at offset 90.
  static const uint8_t dmg_and_miss[12] = {
    12, 0,  3, 0,  0, 0,  5, 0,  1, 0,      // dmg_acc[]
     3, 0                                    // care_miss
  };
  memcpy(p.pad_dmg, dmg_and_miss, sizeof dmg_and_miss);
  p.sick_episodes  = 1;
  p.minigames_won  = 7;
  p.pad_overfeed   = 2;                     // v1 overfeed
  p.snacks_total   = 12;
  p.wish_left_s    = 0;
  p.flags          = LV1_PF_LIGHT_ON;
  p.pad_adult_form = 0;                     // v1 adult_form = FORM_BOLOTA
  p.minor_form     = 0x21;
  p.poop_count     = 1;
  // v1 guilt_level, absence_tier, death_cause, unjust_scolds at 117..120.
  p.pad_ledger[0]  = 0;
  p.pad_ledger[1]  = 0;
  p.pad_ledger[2]  = 0;
  p.pad_ledger[3]  = 1;
  p.happiness_avg  = 66;
  p.wish_id        = 0;
  p.events_done    = (uint8_t)(EV_VISITA | (2u << EV_BIRTHDAY_SH));
  p.crc16 = crc16_ccitt(&p, LEGACY_PETSAVE_CRC_BYTES);
  return p;
}

static LegacyPetSave petsave_egg(void) {
  LegacyPetSave p;
  memset(&p, 0, sizeof p);
  p.magic   = LEGACY_SAVE_MAGIC;
  p.version = LEGACY_SAVE_VERSION;
  p.stage   = STAGE_EGG;
  for (uint8_t i = 0; i < ST_COUNT; i++) p.stat[i] = STAT_MILLI_MAX;
  p.pad_stat            = STAT_MILLI_MAX;   // v1 stat[ST_DISCIPLINE]
  p.birth_epoch         = 1700000000u;
  p.last_seen_epoch     = 1700000000u;
  p.egg_epoch           = 1700000000u;
  p.last_interact_epoch = 1700000000u;
  p.age_s               = 0u;
  p.genome              = fixture_genome(0x00C0FFEEu);
  p.cq                  = CQ_START;
  p.pad_weight          = (int16_t)gene_weight_ideal_dg(p.genome);
  p.flags               = LV1_PF_LIGHT_ON;
  p.pad_adult_form      = 0xFF;             // v1 adult_form = FORM_UNSET
  p.crc16 = crc16_ccitt(&p, LEGACY_PETSAVE_CRC_BYTES);
  return p;
}

static Config config_v1(void) {
  Config c;
  memset(&c, 0, sizeof c);
  c.magic       = NT_CFG_MAGIC;
  c.version     = NT_CFG_VERSION;
  c.flags       = (uint8_t)(CF_RESERVED_BLE | CF_WEB_ENABLED);   // 0x04 unchanged
  c.saved_epoch = 1700200000u;
  copy_str(c.wifi_ssid, sizeof c.wifi_ssid, "legacy-ssid");
  copy_str(c.wifi_pass, sizeof c.wifi_pass, "legacy-pass");
  copy_str(c.pet_name,  sizeof c.pet_name,  "Pebble");
  // The v1 layout had tg_token[48]@119 and tg_chat[17]@167 where reserved_a[65]
  // now sits, and tg_mode@248 where reserved_c does. This fixture left all three
  // empty/zero, and memset() above already wrote those bytes, so the file stays
  // identical to the committed one.
  copy_str(c.tz,        sizeof c.tz,        CFG_TZ_STRING);
  // The v1 layout had lat[12] and lon[12] where reserved_b[24] now sits. The
  // fixture is a byte image of THAT layout, so the coordinates are written at
  // their original offsets and the file stays identical to the committed one.
  copy_str((char*)c.reserved_b,      12, "41.3874");
  copy_str((char*)c.reserved_b + 12, 12, "2.1686");
  c.brightness     = 128;
  c.statusbar_mode = 0;
  c.crc16 = crc16_ccitt(&c, CONFIG_CRC_BYTES);
  return c;
}

static LegacyGainSave gainsave_v1(void) {
  LegacyGainSave g;
  memset(&g, 0, sizeof g);
  g.magic   = LEGACY_GAIN_MAGIC;
  g.version = LEGACY_GAIN_VERSION;
  g.slots   = 7;                            // the v1 blob carried seven StatIds
  g.epoch   = 1700200000u;
  // v1 pts[7] = { 30, 20, 10, 25, 0, 0, 0 }. The seventh byte is reserved[0]
  // now and memset() above already wrote the same zero there.
  static const uint8_t pts[LEGACY_STAT_COUNT] = { 30, 20, 10, 25, 0, 0 };
  memcpy(g.pts, pts, sizeof pts);
  g.crc16 = crc16_ccitt(&g, LEGACY_GAINSAVE_CRC_BYTES);
  return g;
}

static int write_blob(const char* dir, const char* name, const void* p, size_t n) {
  char path[512];
  snprintf(path, sizeof path, "%s/%s", dir, name);
  FILE* f = fopen(path, "wb");
  if (!f) { perror(path); return 1; }
  const size_t w = fwrite(p, 1, n, f);
  fclose(f);
  if (w != n) { fprintf(stderr, "%s: short write\n", path); return 1; }
  printf("%s  %zu B  crc16=0x%04X\n", path, n, (unsigned)crc16_ccitt(p, n));
  return 0;
}

int main(int argc, char** argv) {
  const char* dir = (argc > 1) ? argv[1] : "fixtures";
  int rc = 0;
  const LegacyPetSave  adult = petsave_adult();
  const LegacyPetSave  egg   = petsave_egg();
  const Config   cfg   = config_v1();
  const LegacyGainSave gain = gainsave_v1();
  rc |= write_blob(dir, "petsave_v1_adult.bin", &adult, sizeof adult);
  rc |= write_blob(dir, "petsave_v1_egg.bin",   &egg,   sizeof egg);
  rc |= write_blob(dir, "config_v1.bin",        &cfg,   sizeof cfg);
  rc |= write_blob(dir, "gainsave_v1.bin",      &gain,  sizeof gain);
  return rc;
}
