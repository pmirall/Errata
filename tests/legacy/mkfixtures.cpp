// =============================================================================
//  Pebblebol host tests - legacy/mkfixtures.cpp
//  Writes the v1 persistence fixtures the SaveSchema v2 migration (P2-C9) is
//  tested against: byte images of the legacy PetSave / Config / GainSave
//  structs exactly as storage.cpp v1 puts them into NVS.
//
//      make -C tests fixtures        ->  tests/fixtures/*.bin
//
//  The fixtures are committed; regenerate only when the legacy layout itself
//  is the subject of the commit. Every field is set explicitly so the output
//  is a pure function of this file (no clock, no RNG).
// =============================================================================
#include <stdio.h>
#include <string.h>

#include "nt_types.h"
#include "storage.h"     // GainSave (host-includable: nt_types.h only)
#include "genome.h"
#include "crc16.h"

static_assert(sizeof(PetSave) == 128, "legacy PetSave is 128 B");
static_assert(sizeof(Config) == 256, "legacy Config is 256 B");
static_assert(sizeof(GainSave) == 20, "legacy GainSave is 20 B");

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

static PetSave petsave_adult(void) {
  PetSave p;
  memset(&p, 0, sizeof p);
  p.magic   = NT_SAVE_MAGIC;
  p.version = NT_SAVE_VERSION;
  p.stage   = STAGE_ADULT;
  static const int32_t stats[ST_COUNT] = { 73000, 61000, 88000, 45000, 92000, 70000, 55000 };
  memcpy(p.stat, stats, sizeof stats);
  p.birth_epoch         = 1700000000u;
  p.last_seen_epoch     = 1700200000u;
  p.death_epoch         = 0u;
  p.egg_epoch           = 1699990000u;
  p.last_interact_epoch = 1700199000u;
  p.age_s               = 200000u;
  p.genome              = fixture_genome(0x0BADCAFEu);
  static const int16_t rem[ST_COUNT] = { 120, 1500, 33, 2999, 0, 7, 3598 };
  memcpy(p.stat_rem, rem, sizeof rem);
  p.cq             = 640;
  p.weight_dg      = 420;
  static const uint16_t dmg[DMG_COUNT] = { 12, 3, 0, 5, 1 };
  memcpy(p.dmg_acc, dmg, sizeof dmg);
  p.care_miss      = 3;
  p.sick_episodes  = 1;
  p.minigames_won  = 7;
  p.overfeed       = 2;
  p.snacks_total   = 12;
  p.wish_left_s    = 0;
  p.flags          = PF_LIGHT_ON;
  p.adult_form     = FORM_BOLOTA;
  p.minor_form     = 0x21;
  p.poop_count     = 1;
  p.guilt_level    = 0;
  p.absence_tier   = ABS_NONE;
  p.death_cause    = 0;
  p.unjust_scolds  = 1;
  p.happiness_avg  = 66;
  p.wish_id        = 0;
  p.events_done    = (uint8_t)(EV_VISITA | (2u << EV_BIRTHDAY_SH));
  p.crc16 = crc16_ccitt(&p, PETSAVE_CRC_BYTES);
  return p;
}

static PetSave petsave_egg(void) {
  PetSave p;
  memset(&p, 0, sizeof p);
  p.magic   = NT_SAVE_MAGIC;
  p.version = NT_SAVE_VERSION;
  p.stage   = STAGE_EGG;
  for (uint8_t i = 0; i < ST_COUNT; i++) p.stat[i] = STAT_MILLI_MAX;
  p.birth_epoch         = 1700000000u;
  p.last_seen_epoch     = 1700000000u;
  p.egg_epoch           = 1700000000u;
  p.last_interact_epoch = 1700000000u;
  p.age_s               = 0u;
  p.genome              = fixture_genome(0x00C0FFEEu);
  p.cq                  = CQ_START;
  p.weight_dg           = (int16_t)gene_weight_ideal_dg(p.genome);
  p.flags               = PF_LIGHT_ON;
  p.adult_form          = FORM_UNSET;
  p.absence_tier        = ABS_NONE;
  p.crc16 = crc16_ccitt(&p, PETSAVE_CRC_BYTES);
  return p;
}

static Config config_v1(void) {
  Config c;
  memset(&c, 0, sizeof c);
  c.magic       = NT_CFG_MAGIC;
  c.version     = NT_CFG_VERSION;
  c.flags       = (uint8_t)(CF_BLE_ENABLED | CF_WEB_ENABLED);
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

static GainSave gainsave_v1(void) {
  GainSave g;
  memset(&g, 0, sizeof g);
  g.magic   = NT_GAIN_MAGIC;
  g.version = NT_GAIN_VERSION;
  g.slots   = NT_GAIN_SLOTS;
  g.epoch   = 1700200000u;
  static const uint8_t pts[NT_GAIN_SLOTS] = { 30, 20, 10, 25, 0, 0, 0 };
  memcpy(g.pts, pts, sizeof pts);
  g.crc16 = crc16_ccitt(&g, GAINSAVE_CRC_BYTES);
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
  const PetSave  adult = petsave_adult();
  const PetSave  egg   = petsave_egg();
  const Config   cfg   = config_v1();
  const GainSave gain  = gainsave_v1();
  rc |= write_blob(dir, "petsave_v1_adult.bin", &adult, sizeof adult);
  rc |= write_blob(dir, "petsave_v1_egg.bin",   &egg,   sizeof egg);
  rc |= write_blob(dir, "config_v1.bin",        &cfg,   sizeof cfg);
  rc |= write_blob(dir, "gainsave_v1.bin",      &gain,  sizeof gain);
  return rc;
}
