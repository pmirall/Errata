// =============================================================================
//  PEBBLEBOL - persistence/legacy_v1.h
//  The Nottamagochi v1 wire layouts, FROZEN. Nothing here may ever change:
//  these are the bytes already sitting in the NVS of every unit that ran the
//  old firmware, and persistence/migration.cpp is the only reader.
//
//  The structs are byte-for-byte the v1 blobs, including the fields that the
//  Phase 2 removals retired - death, discipline, weight, adult forms and the
//  rest. They keep their original OFFSETS under pad_* names, because a v1 blob
//  does not care what we renamed. tests/fixtures/*.bin are images of exactly
//  these three layouts (tests/legacy/mkfixtures.cpp).
//
//  These are deliberately separate types from nt_types.h's live PetSave/Config
//  and storage.h's GainSave: those three are still the running v1 code today
//  and are deleted in P2-C9b/P2-C10, at which point this file is the only
//  surviving description of the old format.
//
//  Pure header: stdint/stddef plus Genome, which survives into v2 unchanged.
// =============================================================================
#ifndef PB_LEGACY_V1_H
#define PB_LEGACY_V1_H

#include <stdint.h>
#include <stddef.h>

#include "../core/nt_types.h"     // Genome, and the gene bit map the map uses

// -----------------------------------------------------------------------------
// v1 constants. Spelled here rather than reused from nt_types.h/config.h so
// that retuning a live constant can never silently redefine the frozen format.
// -----------------------------------------------------------------------------
#define LEGACY_SAVE_MAGIC        0x544Eu   // bytes 'N','T'
#define LEGACY_SAVE_VERSION      1
#define LEGACY_PETSAVE_CRC_BYTES 126

#define LEGACY_CFG_MAGIC         0x4643u   // bytes 'C','F'
#define LEGACY_CFG_VERSION       1
#define LEGACY_CONFIG_CRC_BYTES  254

#define LEGACY_GAIN_MAGIC        0x474Cu   // bytes 'L','G'
#define LEGACY_GAIN_VERSION      1
#define LEGACY_GAINSAVE_CRC_BYTES 18

#define LEGACY_STAT_COUNT        6         // v1 StatId as of the P2-C7 removals
#define LEGACY_STAT_MILLI_MAX    100000L

// v1 StatId order. NOT the v2 CareId order - translating between the two is
// most of what the migration does.
#define LV1_ST_HUNGER            0
#define LV1_ST_HAPPINESS         1
#define LV1_ST_ENERGY            2
#define LV1_ST_HYGIENE           3
#define LV1_ST_HEALTH            4
#define LV1_ST_BOND              5

// v1 Stage
#define LV1_STAGE_EGG            0
#define LV1_STAGE_BABY           1
#define LV1_STAGE_CHILD          2
#define LV1_STAGE_TEEN           3
#define LV1_STAGE_ADULT          4
#define LV1_STAGE_SENIOR         5
#define LV1_STAGE_COUNT          6

// v1 PetSave.flags bits the migration still cares about.
#define LV1_PF_SICK              0x0001u
#define LV1_PF_ASLEEP            0x0002u
#define LV1_PF_LIGHT_ON          0x0004u
#define LV1_PF_INBRED            0x0080u
#define LV1_PF_GOD_TAINTED       0x1000u
#define LV1_PF_SOUND_MUTE        0x4000u

// v1 Config.flags bits.
#define LV1_CF_MUTE              0x20u

#define LEGACY_NAME_CAP          13        // pet_name[NAME_MAX_LEN + 1]
#define LEGACY_TZ_CAP            40

// -----------------------------------------------------------------------------
// v1 PetSave - NVS key "save" in namespace "notta", exactly 128 B.
// -----------------------------------------------------------------------------
struct LegacyPetSave {
  uint16_t magic;                           //   0  LEGACY_SAVE_MAGIC
  uint8_t  version;                         //   2  LEGACY_SAVE_VERSION
  uint8_t  stage;                           //   3  v1 Stage

  int32_t  stat[LEGACY_STAT_COUNT];         //   4  milli-points, v1 StatId order
  int32_t  pad_stat;                        //  28  was stat[ST_DISCIPLINE]

  uint32_t birth_epoch;                     //  32
  uint32_t last_seen_epoch;                 //  36
  uint32_t pad_death_epoch;                 //  40
  uint32_t egg_epoch;                       //  44
  uint32_t last_interact_epoch;             //  48
  uint32_t age_s;                           //  52

  Genome   genome;                          //  56  survives into v2 whole

  int16_t  stat_rem[LEGACY_STAT_COUNT];     //  72  integrator remainders
  int16_t  pad_stat_rem;                    //  84

  int16_t  cq;                              //  86  care quality 0..1000
  int16_t  pad_weight;                      //  88
  uint8_t  pad_dmg[12];                     //  90
  uint16_t sick_episodes;                   // 102
  uint16_t minigames_won;                   // 104
  uint16_t pad_overfeed;                    // 106
  uint16_t snacks_total;                    // 108
  uint16_t wish_left_s;                     // 110

  uint16_t flags;                           // 112  LV1_PF_*
  uint8_t  pad_adult_form;                  // 114
  uint8_t  minor_form;                      // 115
  uint8_t  poop_count;                      // 116
  uint8_t  pad_ledger[4];                   // 117
  uint8_t  happiness_avg;                   // 121
  uint8_t  wish_id;                         // 122
  uint8_t  events_done;                     // 123
  uint8_t  reserved[2];                     // 124

  uint16_t crc16;                           // 126  over bytes 0..125
};

static_assert(sizeof(LegacyPetSave) == 128, "v1 PetSave is 128 B and may not move");
static_assert(offsetof(LegacyPetSave, stat)     ==   4, "v1 PetSave.stat moved");
static_assert(offsetof(LegacyPetSave, genome)   ==  56, "v1 PetSave.genome moved");
static_assert(offsetof(LegacyPetSave, stat_rem) ==  72, "v1 PetSave.stat_rem moved");
static_assert(offsetof(LegacyPetSave, cq)       ==  86, "v1 PetSave.cq moved");
static_assert(offsetof(LegacyPetSave, flags)    == 112, "v1 PetSave.flags moved");
static_assert(offsetof(LegacyPetSave, crc16)    == 126, "v1 PetSave.crc16 moved");
static_assert(LEGACY_PETSAVE_CRC_BYTES == sizeof(LegacyPetSave) - 2, "v1 PetSave CRC span");

// -----------------------------------------------------------------------------
// v1 Config - NVS key "cfg" in namespace "notta", exactly 256 B.
// The Wi-Fi credentials and the retired Telegram/coordinate fields are read
// only to be dropped: v2 stores none of them (spec section 68 r5).
// -----------------------------------------------------------------------------
struct LegacyConfig {
  uint16_t magic;                           //   0  LEGACY_CFG_MAGIC
  uint8_t  version;                         //   2  LEGACY_CFG_VERSION
  uint8_t  flags;                           //   3  LV1_CF_*
  uint32_t saved_epoch;                     //   4
  char     wifi_ssid[33];                   //   8  dropped by the migration
  char     wifi_pass[65];                   //  41  dropped by the migration
  char     pet_name[LEGACY_NAME_CAP];       // 106
  uint8_t  pad_telegram[65];                // 119  was tg_token[48] + tg_chat[17]
  char     tz[LEGACY_TZ_CAP];               // 184
  uint8_t  pad_coords[24];                  // 224  was lat[12] + lon[12]
  uint8_t  pad_tg_mode;                     // 248
  uint8_t  brightness;                      // 249
  uint8_t  statusbar_mode;                  // 250
  uint8_t  reserved[3];                     // 251
  uint16_t crc16;                           // 254  over bytes 0..253
};

static_assert(sizeof(LegacyConfig) == 256, "v1 Config is 256 B and may not move");
static_assert(offsetof(LegacyConfig, wifi_ssid)    ==   8, "v1 Config.wifi_ssid moved");
static_assert(offsetof(LegacyConfig, pet_name)     == 106, "v1 Config.pet_name moved");
static_assert(offsetof(LegacyConfig, pad_telegram) == 119, "v1 Config.pad_telegram moved");
static_assert(offsetof(LegacyConfig, tz)           == 184, "v1 Config.tz moved");
static_assert(offsetof(LegacyConfig, pad_coords)   == 224, "v1 Config.pad_coords moved");
static_assert(offsetof(LegacyConfig, brightness)   == 249, "v1 Config.brightness moved");
static_assert(offsetof(LegacyConfig, crc16)        == 254, "v1 Config.crc16 moved");
static_assert(LEGACY_CONFIG_CRC_BYTES == sizeof(LegacyConfig) - 2, "v1 Config CRC span");

// -----------------------------------------------------------------------------
// v1 GainSave - NVS key "gl", exactly 20 B. The anti-farm ledger is carried
// forward untouched (plan 1.5.3), so this layout is frozen but not migrated.
// -----------------------------------------------------------------------------
struct LegacyGainSave {
  uint16_t magic;                           //  0  LEGACY_GAIN_MAGIC
  uint8_t  version;                         //  2  LEGACY_GAIN_VERSION
  uint8_t  slots;                           //  3  StatId count the blob was made with
  uint32_t epoch;                           //  4  0 = no trustworthy clock
  uint8_t  pts[LEGACY_STAT_COUNT];          //  8  whole points still unspent
  uint8_t  reserved[4];                     // 14
  uint16_t crc16;                           // 18  over bytes 0..17
};

static_assert(sizeof(LegacyGainSave) == 20, "v1 GainSave is 20 B and may not move");
static_assert(offsetof(LegacyGainSave, pts)   ==  8, "v1 GainSave.pts moved");
static_assert(offsetof(LegacyGainSave, crc16) == 18, "v1 GainSave.crc16 moved");
static_assert(LEGACY_GAINSAVE_CRC_BYTES == sizeof(LegacyGainSave) - 2, "v1 GainSave CRC span");

#endif // PB_LEGACY_V1_H
