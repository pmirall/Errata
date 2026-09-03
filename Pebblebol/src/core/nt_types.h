// =============================================================================
//  PEBBLEBOL - core/nt_types.h
//  Every POD struct and enum shared across modules, plus compile-time size
//  guards. NOT named types.h on purpose (libc shadowing).
//
//  MUST compile on a host compiler (MSVC / gcc) as well as on the ESP32:
//  no Arduino headers, no allocation, no code beyond trivial macros.
// =============================================================================
#ifndef NT_TYPES_H
#define NT_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include "config.h"

// -----------------------------------------------------------------------------
// 0. PORTABLE PACKING
// -----------------------------------------------------------------------------
#if defined(_MSC_VER)
  #define NT_PACK_PUSH __pragma(pack(push, 1))
  #define NT_PACK_POP  __pragma(pack(pop))
  #define NT_PACKED
#else
  #define NT_PACK_PUSH
  #define NT_PACK_POP
  #define NT_PACKED __attribute__((packed))
#endif

// -----------------------------------------------------------------------------
// 1. ENUMS
//    Every enum has an explicit underlying type so struct layout is stable and
//    so a value can be memcpy'd into a save blob without surprises.
// -----------------------------------------------------------------------------

// Life stage. Ordering is load-bearing: stage multipliers and age thresholds
// are indexed by it, and it is compared with < / >.
enum Stage : uint8_t {
  STAGE_EGG = 0,
  STAGE_BABY,
  STAGE_CHILD,
  STAGE_TEEN,
  STAGE_ADULT,
  STAGE_SENIOR,
  STAGE_COUNT
};

// Screen state machine. Renumbered when MEMORIAL and LINEAGE were removed, so
// the S-numbers below are the current ones, not the historical ones.
enum ScreenId : uint8_t {
  // ---- the spec section 6 set, in the spec's own order --------------------
  SCR_BOOT = 0,      // splash, before anything has been read
  SCR_LOAD_SAVE,     // the save pipeline is running
  SCR_HOME,          // the pet, the one screen you stare at for days
  SCR_MENU,          // the 7-icon ring
  SCR_CARE,          // the care list (ex SCR_FEED)
  SCR_PLAY,          // the minigame list
  SCR_GAME,          // a minigame is running
  SCR_BOX,           // the ten slots (spec section 9)
  SCR_STATUS,        // PEBBLE page A: vitals
  SCR_STATUS_B,      // PEBBLE page B: genome. See the note below.
  SCR_NETWORK,       // Wi-Fi exploration, phase 5
  SCR_LINK,          // peer link, phase 7 (ex SCR_SOCIAL)
  SCR_CREATOR,       // the creator portal (ex SCR_QR)
  SCR_SETTINGS,
  SCR_TIME,          // on-device time entry (ex SCR_CLOCK)
  SCR_CONFIRM,       // overlay, cursor defaults to NO
  SCR_ALERT,         // overlay
  SCR_ENCOUNTER,     // phase 5
  SCR_CAPTURE,       // phase 5
  SCR_BATTLE,        // phase 4
  SCR_TRADE,         // phase 7
  SCR_BREED,         // phase 7
  SCR_EVOLUTION,     // the egg / the evolution ceremony (ex SCR_EGG)
  SCR_ITEM_REWARD,   // phase 6
  SCR_ERROR,         // SAVE ERROR / PANTALLA and the two choices
  SCR_SLEEP,         // phase 10
  SCR_DIAG,          // the developer console (ex SCR_GOD)
  SCR_COUNT
};

// STATUS_B is the ONE id here that spec section 6 does not name, and it is not
// a new state: it is the second page of the same PEBBLE state. The pages are
// two table rows rather than one row plus a page byte because the screen table
// is what the navigation stack, the back gesture and the snapshot tests all
// address - collapsing them would move page changes out of the state machine
// and back into a screen-local variable the stack cannot see, which is exactly
// the "global boolean spaghetti" section 6 forbids.

// Gesture recogniser output. Prefixed GST_ deliberately: bare BOTH/NONE at
// global scope in a header shared by 20 files is a collision waiting to happen.
enum Gesture : uint8_t {
  GST_NONE = 0,
  GST_TAP_L,
  GST_TAP_R,
  GST_DBL_L,
  GST_DBL_R,
  GST_HOLD_L,        // repeats every REPEAT_RATE_MS
  GST_HOLD_R,        // CHOOSES (section 7); fires once, never repeats
  GST_BOTH,
  GST_LONG_BOTH,     // HOME from anywhere
  GST_COUNT
};

// Displayed mood face. Ordered worst -> best.
enum Mood : uint8_t {
  MOOD_MISERIA = 0,  //  0-15
  MOOD_TRISTE,       // 16-35
  MOOD_NEUTRO,       // 36-55
  MOOD_CONTENTO,     // 56-75
  MOOD_FELIZ,        // 76-90
  MOOD_EUFORICO,     // 91-100
  MOOD_COUNT
};

// Exactly one radio stack may be resident.
enum RadioMode : uint8_t {
  RADIO_OFF = 0,
  RADIO_WIFI,
  RADIO_BLE,
  RADIO_COUNT
};

// Anything that mutates the pet goes through sim_apply_action(ActionId, ...).
enum ActionId : uint8_t {
  ACT_NONE = 0,
  ACT_FEED_MEAL,
  ACT_FEED_SNACK,
  ACT_CLEAN,
  ACT_MEDICINE,
  ACT_PLAY,
  ACT_PET,
  ACT_LIGHT_TOGGLE,
  ACT_SLEEP_TOGGLE,
  ACT_COUNT
};

enum ActionErr : uint8_t {
  AERR_NONE = 0,
  AERR_COOLDOWN,     // -> 429 {"err":"cool","s":N}
  AERR_FULL,         // -> 409 {"err":"full"}
  AERR_TIRED,        // -> 409 {"err":"tired"}
  AERR_NOT_SICK,
  AERR_NOTHING_TODO, // nothing to clean
  AERR_ASLEEP,
  AERR_IS_EGG,
  AERR_BAD_ARG,      // -> 400 {"err":"arg"}
  AERR_COUNT
};

// Continuous stats. The sim's stat accessors are indexed by this, so the
// order is contractual and shared with sim, ui, webui and godmode.
enum StatId : uint8_t {
  ST_HUNGER = 0,     // satiety: 100 = full. Do NOT invert.
  ST_HAPPINESS,
  ST_ENERGY,
  ST_HYGIENE,
  ST_HEALTH,
  ST_BOND,
  ST_COUNT
};
#define ST_CORE_COUNT 4   // ST_HUNGER..ST_HYGIENE: the four "core" stats

enum AlertId : uint8_t {
  AL_NONE = 0,
  AL_HUNGRY,
  AL_DIRTY,
  AL_SAD,
  AL_TIRED,
  AL_SICK,
  AL_POOP,
  AL_LOW_HEALTH,
  AL_WISH,
  AL_EVOLVING,
  AL_BIRTHDAY,
  AL_MATE_FOUND,
  AL_COUNT
};

// How this boot started (hardware/boot.cpp: esp_reset_reason() + RTC nonce).
enum BootKind : uint8_t {
  BOOT_FIRST_RUN = 0,  // nothing loadable in NVS
  BOOT_POWER_LOSS,     // ESP_RST_POWERON / _BROWNOUT -> absence path
  BOOT_CRASH,          // ESP_RST_PANIC / _TASK_WDT + RTC nonce intact
  BOOT_SOFT_RESET,     // ESP_RST_SW / _EXT / _USB / _JTAG + RTC nonce intact
  BOOT_DEEPSLEEP,      // ESP_RST_DEEPSLEEP + RTC nonce intact. Split out of
                       // SOFT_RESET in P2-C9b: a timed wake is not a restart,
                       // and unlike a restart its interval IS elapsed time the
                       // absence path may charge.
  BOOT_UNKNOWN,
  BOOT_COUNT
};

// Daily wish.
enum WishId : uint8_t {
  WISH_NONE = 0,
  WISH_PLAY,
  WISH_SNACK,
  WISH_CLEAN,
  WISH_PET3,
  WISH_COUNT
};

// On-device 2-button minigames (the GAME screen). Canonical for minigames_won.
enum DevGameId : uint8_t {
  DG_REFLEX = 0,
  DG_MEMORY,
  DG_JUMP,
  DG_COUNT
};

// S0 TAP_R cycles this.
enum StatusBarMode : uint8_t {
  SBAR_ICONS = 0, SBAR_BARS, SBAR_TEXT, SBAR_COUNT
};

// -----------------------------------------------------------------------------
// 2. GENOME - 16 BYTES EXACTLY, packed, little-endian
// -----------------------------------------------------------------------------
#define GENOME_SIG          0x4E50u   // magic_ver bits[15:4] = 0x4E5
#define GENOME_SIG_MASK     0xFFF0u
#define GENOME_PROTO_VER    1u
#define GENOME_MAGIC_VER    (GENOME_SIG | GENOME_PROTO_VER)   // 0x4E51
#define GENOME_CRC_INIT     0xFFFFu
#define GENOME_CRC_POLY     0x1021u
#define GENOME_CRC_BYTES    14        // CRC covers bytes 0..13

NT_PACK_PUSH
struct NT_PACKED Genome {
  uint16_t magic_ver;    // [0..1]   GENOME_MAGIC_VER
  uint32_t lineage_id;   // [2..5]   dynasty id, 0 is invalid
  uint16_t g0;           // [6..7]   morphology
  uint16_t g1;           // [8..9]   physiology
  uint16_t g2;           // [10..11] resilience / meta
  uint8_t  generation;   // [12]     saturating
  uint8_t  parent_tag;   // [13]     (linA ^ linB ^ g1A ^ g1B) & 0xFF
  uint16_t crc16;        // [14..15] CRC-16/CCITT-FALSE over bytes 0..13
};
NT_PACK_POP

static_assert(sizeof(Genome) == 16, "Genome must be exactly 16 bytes");

// --- gene bit map (shift, mask) ---------------------------------------------
#define GN_SPECIES_SH   0
#define GN_SPECIES_MK   0x0Fu
#define GN_PATTERN_SH   4
#define GN_PATTERN_MK   0x0Fu
#define GN_PALETTE_SH   8
#define GN_PALETTE_MK   0x07u
#define GN_BODYSIZE_SH  11
#define GN_BODYSIZE_MK  0x07u
#define GN_EARHORN_SH   14
#define GN_EARHORN_MK   0x03u

#define GN_APPETITE_SH  0
#define GN_APPETITE_MK  0x0Fu
#define GN_METAB_SH     4
#define GN_METAB_MK     0x0Fu
#define GN_SOCIAB_SH    8
#define GN_SOCIAB_MK    0x0Fu
#define GN_TEMPER_SH    12
#define GN_TEMPER_MK    0x0Fu

#define GN_HARDY_SH     0
#define GN_HARDY_MK     0x0Fu
#define GN_LUCK_SH      4
#define GN_LUCK_MK      0x07u
#define GN_MUTCNT_SH    7
#define GN_MUTCNT_MK    0x0Fu
#define GN_SEX_SH       11
#define GN_SEX_MK       0x01u
#define GN_RARE_SH      12
#define GN_RARE_MK      0x01u
#define GN_TAINT_SH     13
#define GN_TAINT_MK     0x01u
#define GN_G2RSV_SH     14
#define GN_G2RSV_MK     0x03u

#define GN_GET(word, sh, mk)     ((uint8_t)(((word) >> (sh)) & (mk)))
#define GN_SET(word, sh, mk, v)  ((word) = (uint16_t)(((word) & (uint16_t)~((uint16_t)(mk) << (sh))) \
                                          | (uint16_t)(((uint16_t)(v) & (mk)) << (sh))))

#define SPECIES_COUNT   16    // 0..11 real, 12..15 hybrid-exclusive
#define PATTERN_COUNT   16
#define GENESIS_SPECIES_MAX 7 // gen 0 rolls species in [0,7]
#define GENE_NEUTRAL    8
#define GENESIS_GENE_MIN 4
#define GENESIS_GENE_MAX 12

// Temperament classes
enum Temperament : uint8_t {
  TEMPER_SOLAR = 0,     // gene 0-3
  TEMPER_TRANQUILO,     // gene 4-7
  TEMPER_NERVIOSO,      // gene 8-11
  TEMPER_GOTICO,        // gene 12-15
  TEMPER_COUNT
};
#define TEMPER_CLASS(v) ((uint8_t)((v) >> 2))

// -----------------------------------------------------------------------------
// 3. THE LIVE PET FLAG WORD
//    P2-C10 deleted the v1 `PetSave` struct: the persisted truth is
//    PebbleInstance (persistence/save_schema.h) and the only surviving
//    description of the 128 B v1 blob is persistence/legacy_v1.h, which the
//    migration reads. What is left here is the flag word game/sim.cpp still
//    works in and publishes through SimView; the four bits that reach flash
//    are mirrored into PebbleInstance.status / .flags by the sim.
// -----------------------------------------------------------------------------
// SimView.flags bits
#define PF_SICK          0x0001u
#define PF_ASLEEP        0x0002u
#define PF_LIGHT_ON      0x0004u
// bits 0x0008 / 0x0010 / 0x0020 are retired (PF_SCAR / PF_DEAD / PF_BURIED).
#define PF_COLD_EGG      0x0040u   // hatched from an egg older than EGG_COLD_AFTER_S
#define PF_INBRED        0x0080u   // sick probability x1.25 for life
#define PF_WISH_ACTIVE   0x0100u
#define PF_WISH_DONE     0x0200u   // today's wish already resolved
#define PF_ABS_UNKNOWN   0x0400u   // absence charged with no clock; retro-fix pending
#define PF_SEEKING_MATE  0x0800u   // BLE beacon "seeking" bit
#define PF_GOD_TAINTED   0x1000u   // mirror of Genome g2 bit 13, for cheap reads
// bit 0x2000 is retired (PF_HYBRID_ELIG).
#define PF_SOUND_MUTE    0x4000u
#define PF_EGG_PENDING   0x8000u   // a PendingEgg blob exists in NVS key "egg"

// The sim's events_done bits (RAM only since P2-C10)
#define EV_VISITA        0x01u
// bit 0x02 is retired (EV_STORM). EV_BIRTHDAY_SH keeps its shift so a v1 blob
// still reads its birthday count from the same bits.
#define EV_BIRTHDAY_SH   2         // bits 7:2 = birthday count 0..63
#define EV_BIRTHDAY_MK   0x3Fu


// -----------------------------------------------------------------------------
// 4. PENDING EGG - NVS key "egg", 24 B.
//    An egg produced while the pet is still alive must survive a reboot without
//    displacing the living pet, so it cannot live on the Pebble. Nothing writes
//    this blob today: breeding comes back in Phase 7.
// -----------------------------------------------------------------------------
#define NT_EGG_MAGIC     0x4745u   // 'E','G'
#define NT_EGG_VERSION   1
#define EF_VALID         0x01u
#define EF_FROM_MATING   0x02u
#define EF_INBRED        0x04u
#define EF_NEW_LINEAGE   0x08u     // "mutacion fundadora" -> NUEVA ESTIRPE
#define EF_HYBRID        0x10u     // recombinant species

struct PendingEgg {
  uint16_t magic;          //  0
  uint8_t  version;        //  2
  uint8_t  flags;          //  3  EF_*
  Genome   genome;         //  4
  uint32_t created_epoch;  // 20
};
static_assert(sizeof(PendingEgg) == 24, "PendingEgg must be 24 bytes");

// -----------------------------------------------------------------------------
// 5. CONFIG - NVS key "cfg", 256 B.
//    Seeded from the CFG_* defaults in config.h on first boot; editable at
//    runtime from SETTINGS and the captive portal.
//    reserved_a[] holds the retired Telegram token and chat id, reserved_b[]
//    the retired weather coordinates and reserved_c the retired Telegram mode:
//    the offsets of every field after them are frozen by the asserts below, so
//    the bytes stay put and a v1 blob still loads.
// -----------------------------------------------------------------------------
#define NT_CFG_MAGIC     0x4643u   // 'C','F'
#define NT_CFG_VERSION   1
#define CF_PROVISIONED   0x01u     // WiFi credentials confirmed working at least once
#define CF_BLE_ENABLED   0x04u
#define CF_WEB_ENABLED   0x08u
#define CF_MUTE          0x20u

struct Config {
  uint16_t magic;                        //   0  NT_CFG_MAGIC
  uint8_t  version;                      //   2  NT_CFG_VERSION
  uint8_t  flags;                        //   3  CF_*
  uint32_t saved_epoch;                  //   4
  char     wifi_ssid[SSID_MAX_LEN + 1];  //   8  33
  char     wifi_pass[PASS_MAX_LEN + 1];  //  41  65
  char     pet_name[NAME_MAX_LEN + 1];   // 106  13
  uint8_t  reserved_a[65];               // 119  65  was tg_token[48]+tg_chat[17]
  char     tz[TZ_MAX_LEN + 1];           // 184  40
  uint8_t  reserved_b[24];               // 224  24  was lat[12]+lon[12], now 0
  uint8_t  reserved_c;                   // 248  was tg_mode, now 0
  uint8_t  brightness;                   // 249  OLED contrast
  uint8_t  statusbar_mode;               // 250  StatusBarMode
  uint8_t  reserved[3];                  // 251  must be 0
  uint16_t crc16;                        // 254  over bytes 0..253
};

static_assert(sizeof(Config) == 256, "Config layout drifted");
static_assert(offsetof(Config, wifi_ssid) ==   8, "Config.wifi_ssid moved");
static_assert(offsetof(Config, reserved_a)== 119, "Config.reserved_a moved");
static_assert(offsetof(Config, tz)        == 184, "Config.tz moved");
static_assert(offsetof(Config, reserved_b)== 224, "Config.reserved_b moved");
static_assert(offsetof(Config, reserved_c)== 248, "Config.reserved_c moved");
static_assert(offsetof(Config, crc16)     == 254, "Config.crc16 moved");
#define CONFIG_CRC_BYTES 254

// -----------------------------------------------------------------------------
// 6. RTC RETENTION - RTC_NOINIT_ATTR, survives soft reset, lost on power loss.
//    The crash-vs-power-loss discriminator.
// -----------------------------------------------------------------------------
struct RtcKeep {
  uint32_t magic;             // RTC_NONCE_MAGIC
  uint32_t nonce;             // rng_u32(RNG_MISC) at first boot, echoed forever
  uint32_t last_seen_epoch;
  uint32_t boot_count;
  uint32_t uptime_s;
  uint32_t god_taint;         // non-zero if god mode was entered this power cycle
  uint32_t reserved[2];
};
static_assert(sizeof(RtcKeep) <= RTC_STRUCT_MAX_BYTES, "RtcKeep exceeds the RTC budget");

// -----------------------------------------------------------------------------
// 7. TRANSIENT SHARED STRUCTS (never persisted, no size contract)
// -----------------------------------------------------------------------------

// Result of sim_apply_action(). Deltas are WHOLE POINTS, already applied.
struct ActionResult {
  uint8_t  ok;             // 1 = the action landed
  uint8_t  err;            // ActionErr
  uint16_t cooldown_s;     // seconds remaining when err == AERR_COOLDOWN
  int16_t  d[ST_COUNT];    // applied delta per StatId
  int16_t  d_cq;
  uint16_t str_id;         // StrId of the reaction line, 0 = none
};

// One BLE peer seen in the last BLE_PEER_TTL_S seconds.
struct BlePeerInfo {
  uint8_t  mac[6];
  Genome   genome;
  uint8_t  stage;          // Stage
  uint8_t  cq_hi;          // cq >> 2
  uint8_t  peer_flags;     // b0 = debug/godmode, b1 = seeking
  int8_t   rssi;
  uint32_t last_seen_ms;
};

// The absence report handed from sim_catch_up_ex() to the UI.
struct AbsenceReport {
  uint8_t  clock_known;    // 0 = the unknown-clock path: nothing was charged
  uint32_t absence_s;      // exact, never rounded: the precision is the joke
  uint16_t steps;          // simulated steps actually run
};

// -----------------------------------------------------------------------------
// 8. SMALL SHARED HELPERS (pure, integer, host-safe)
// -----------------------------------------------------------------------------
#define NT_MIN(a, b)          (((a) < (b)) ? (a) : (b))
#define NT_MAX(a, b)          (((a) > (b)) ? (a) : (b))
#define NT_CLAMP(v, lo, hi)   NT_MIN(NT_MAX((v), (lo)), (hi))
#define NT_ARRAY_LEN(a)       (sizeof(a) / sizeof((a)[0]))

// milli-points <-> whole points. Never introduce a float here.
#define NT_PTS(milli)         ((int32_t)((milli) / 1000))
#define NT_MILLI(points)      ((int32_t)((int32_t)(points) * 1000))
#define NT_PCT(milli)         ((uint8_t)NT_CLAMP((milli) / 1000, 0, 100))

// Fixed-point multiply by an x1000 multiplier, rounding toward zero.
#define NT_MULX(v, m1000)     ((int32_t)(((int64_t)(v) * (int32_t)(m1000)) / 1000))

#endif // NT_TYPES_H
