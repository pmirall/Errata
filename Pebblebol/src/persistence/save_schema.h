// =============================================================================
//  PEBBLEBOL - persistence/save_schema.h
//  SaveSchema v2: every byte that reaches flash, and nothing else.
//  Plan section 1.5 (data formats). Little-endian, packed where a field would
//  otherwise be padded, CRC-16/CCITT-FALSE over everything but the trailing
//  crc16 itself.
//
//  Rules this header exists to enforce (plan section 1.3 rule 5):
//    * A persisted layout is NEVER edited in place. Add to reserved[], or bump
//      SAVE_SCHEMA_VERSION and add a migration plus a test.
//    * Every struct pins its size and its load-bearing offsets with
//      static_assert, so a compiler/ABI change that would silently reshape a
//      blob fails the build instead of the field unit.
//    * Every NVS key is <= 15 characters (NVS_KEY_NAME_MAX_SIZE is 16 with the
//      NUL). The key builders below are the only place keys are spelled.
//
//  Pairing (T5): the blobs marked "pair" are stored twice, under <key>0 and
//  <key>1, and every write goes to the copy with the LOWER seq. The loader
//  takes the highest valid seq. One torn or rotted copy therefore always
//  leaves the other intact - that is LOAD_RECOVERED_PAIR, not data loss.
//  Single-key blobs (cs*, tr) carry no seq: there is nothing to choose between.
//
//  Pure header: stdint/stddef plus core types. No Arduino, no allocation.
// =============================================================================
#ifndef PB_SAVE_SCHEMA_H
#define PB_SAVE_SCHEMA_H

#include <stdint.h>
#include <stddef.h>

#include "../core/nt_types.h"     // Genome (copied whole into a Pebble)
#include "../core/version.h"      // SAVE_SCHEMA_VERSION, PROTOCOL_VERSION, ...
#include "../data/content_version.h"   // CONTENT_VERSION (generated hash)

// -----------------------------------------------------------------------------
// 0. Shared constants
// -----------------------------------------------------------------------------
#define BOX_SLOTS               10      // spec section 9: ten Pebbles, no more
#define PB_CARE_COUNT            5      // HUNGER HAPPINESS HEALTH CLEANLINESS ENERGY
#define PB_MOVE_COUNT            4
#define PB_NICKNAME_CAP         13      // NAME_MAX_LEN 12 + NUL
#define PB_LEVEL_MAX            30      // spec section 11: levels 1..30
#define CUSTOM_SPECIES_SLOTS    10      // cs0..cs9
#define COOLDOWN_SLOTS          32
#define INVENTORY_SLOTS          7      // see the note on the Inventory layout
#define XP_LEDGER_SLOTS          4      // one daily bucket per XpSource

#define KV_KEY_CAP              16      // NVS_KEY_NAME_MAX_SIZE, NUL included
#define KV_KEY_MAX_LEN          15

// Care indices. The order is contractual: it is the order of the care[] array
// on the wire. It deliberately differs from the legacy StatId order, which is
// exactly what persistence/migration.cpp translates.
enum CareId : uint8_t {
  CARE_HUNGER = 0,
  CARE_HAPPINESS,
  CARE_HEALTH,
  CARE_CLEANLINESS,
  CARE_ENERGY,
  CARE_COUNT
};
static_assert((int)CARE_COUNT == PB_CARE_COUNT, "CareId and care[] are out of sync");

// Clock calibration state, persisted in ConfigV2.time_cal_state (plan 1.7).
// hardware/gametime.h consumes these names from here rather than redefining
// them, so the persisted encoding has exactly one owner.
enum TimeCal : uint8_t {
  CAL_UNSET = 0,      // no trustworthy wall clock has ever been set
  CAL_ESTIMATED,      // reconstructed from uptime + last_known_epoch
  CAL_USER,           // typed on the device
  CAL_PHONE,          // POST /api/time
  CAL_COUNT
};

// PebbleInstance.origin
enum PebbleOrigin : uint8_t {
  ORIGIN_STARTER = 0,
  ORIGIN_WILD,
  ORIGIN_BRED,
  ORIGIN_TRADED,
  ORIGIN_CREATOR,
  ORIGIN_COUNT
};

// -----------------------------------------------------------------------------
// 1. PebbleInstance - 128 B, NVS keys "pb<slot><copy>" (pair), plan 1.5.1
//
//    Same size as the legacy v1 PetSave, so the NVS entry arithmetic is known.
//    Twelve reserved bytes buy small additions without a schema bump.
//    Nothing derived is stored (spec section 10): hp_max, atk, def and spd are
//    recomputed from the species base, the level and the genome on every read.
// -----------------------------------------------------------------------------
#define PEBBLE_MAGIC            0x4250u   // bytes 'P','B' on the wire
#define PEBBLE_LAYOUT_VER       1         // the schema version lives in BoxHeader
#define PEBBLE_CRC_BYTES        126

// PebbleInstance.status
#define PBS_SICK                0x01u
#define PBS_ASLEEP              0x02u
#define PBS_CORRUPTED           0x04u     // spec section 55
#define PBS_FAINTED             0x08u
// Bit 0x10 was PBS_LIGHT_ON. P3-C2b deleted the light mechanic, so the bit is
// RESERVED: nothing writes it and nothing reads it, and it is deliberately NOT
// recycled for a new meaning - a v2 save written before that commit may still
// carry it set, and the 128 B layout is pinned by offsetof asserts either way.
#define PBS_RESERVED_LIGHT      0x10u

// PebbleInstance.flags
#define PBF_CUSTOM              0x01u
#define PBF_TRADED              0x02u
#define PBF_BRED                0x04u
#define PBF_GOD_TAINTED         0x08u
#define PBF_RARE                0x10u
#define PBF_HAS_CUSTOM_SPRITE   0x20u

#define PB_CUSTOM_SPRITE_NONE   0xFFu
#define PB_CARE_MILLI_MAX       100000L   // care[] is milli-points, 0..100000

NT_PACK_PUSH
struct NT_PACKED PebbleInstance {
  uint16_t magic;                        //   0  PEBBLE_MAGIC
  uint8_t  layout_ver;                   //   2  PEBBLE_LAYOUT_VER
  uint8_t  species_id;                   //   3  0 empty, 1..199 built-in, 200..209 custom
  uint32_t id;                           //   4  0 empty; unique inside the Box
  uint32_t creation_seed;                //   8  individual variation source
  uint32_t birth_epoch;                  //  12  0 when the clock was CAL_UNSET
  uint32_t last_updated_epoch;           //  16  drives box_recover() and catch-up
  uint32_t age_s;                        //  20  accumulated by care ticks
  uint16_t xp;                           //  24  XP inside the current level
  uint8_t  level;                        //  26  1..30
  uint8_t  evo_state;                    //  27  bits1:0 stage in family, bit7 pending
  int32_t  care[PB_CARE_COUNT];          //  28  milli-points, indexed by CareId
  int16_t  care_rem[PB_CARE_COUNT];      //  48  exact integrator remainders
  uint16_t hp_cur;                       //  58  current battle HP (max is derived)
  uint8_t  status;                       //  60  PBS_*
  uint8_t  flags;                        //  61  PBF_*
  uint8_t  moves[PB_MOVE_COUNT];         //  62  AttackId, 0 = empty
  uint8_t  origin;                       //  66  PebbleOrigin
  uint8_t  trait_id;                     //  67  0 = none
  uint16_t battles_won;                  //  68
  uint16_t battles_lost;                 //  70
  uint16_t minigames_won;                //  72
  uint8_t  evolutions;                   //  74
  uint8_t  trades;                       //  75
  uint32_t lifetime_active_s;            //  76  seconds spent as the active Pebble
  Genome   genome;                       //  80  the existing 16 B genome, whole
  char     nickname[PB_NICKNAME_CAP];    //  96  NUL-terminated, may be empty
  uint8_t  custom_sprite;                // 109  PB_CUSTOM_SPRITE_NONE or a cs slot
  uint32_t seq;                          // 110  pair sequence number
  // THE CORRUPTION DEADLINE (P5-C3, spec sections 22 and 55). Four of the
  // twelve reserved bytes, spent deliberately.
  //
  // PBS_CORRUPTED existed with NOWHERE TO PUT ITS EXPIRY, which meant the
  // status could only ever be set and never time out - a 24 h effect with no
  // 24 h in it, clearable only by item 7. Nothing set the bit before this
  // commit (measured: no writer anywhere in src/), so no save in existence
  // carries a stale one and this needs no migration: an old blob reads 0,
  // which is exactly "not corrupted".
  //
  // IT IS NOT ON THE WIRE and it must never become so. networking/protocol.h's
  // 48 B record has its own field list, reserved[12] is not in it, and
  // PBW_STATUS_MASK already refuses PBS_CORRUPTED outright because the bit is
  // EVOC_CORRUPTED's input. A peer therefore cannot send either half.
  //
  // NO VALIDATOR RULE, and game/validate.h already states the reason for the
  // whole class: every epoch in this struct is "checked against NOTHING here,
  // because no rule exists to check them against". A deadline in the past is
  // simply an expired one, and game/corruption.cpp clears it on the next tick.
  uint32_t corrupt_until_epoch;          // 114  0 = not corrupted
  uint8_t  reserved[8];                  // 118  must be 0
  uint16_t crc16;                        // 126  over bytes 0..125
};
NT_PACK_POP

static_assert(sizeof(PebbleInstance) == 128, "PebbleInstance layout drifted");
static_assert(offsetof(PebbleInstance, species_id) ==   3, "PebbleInstance.species_id moved");
static_assert(offsetof(PebbleInstance, care)       ==  28, "PebbleInstance.care moved");
static_assert(offsetof(PebbleInstance, care_rem)   ==  48, "PebbleInstance.care_rem moved");
static_assert(offsetof(PebbleInstance, moves)      ==  62, "PebbleInstance.moves moved");
static_assert(offsetof(PebbleInstance, genome)     ==  80, "PebbleInstance.genome moved");
static_assert(offsetof(PebbleInstance, nickname)   ==  96, "PebbleInstance.nickname moved");
static_assert(offsetof(PebbleInstance, seq)        == 110, "PebbleInstance.seq moved");
static_assert(offsetof(PebbleInstance, corrupt_until_epoch) == 114,
              "PebbleInstance.corrupt_until_epoch moved - it was carved out of "
              "reserved[12] and every byte after it must stay where it was");
static_assert(offsetof(PebbleInstance, reserved)   == 118, "PebbleInstance.reserved moved");
static_assert(offsetof(PebbleInstance, crc16)      == 126, "PebbleInstance.crc16 moved");
static_assert(PEBBLE_CRC_BYTES == sizeof(PebbleInstance) - 2, "PebbleInstance CRC span drifted");

// -----------------------------------------------------------------------------
// 2. BoxHeader - 32 B, keys "box0"/"box1" (pair), plan 1.5.3
//    The Box index: which slots hold a Pebble, which one is active, and the
//    counters that must not restart when a slot is rewritten.
// -----------------------------------------------------------------------------
#define BOX_MAGIC               0x5842u   // bytes 'B','X'
#define BOX_CRC_BYTES           30
#define BOX_ACTIVE_NONE         0xFFu

// BoxHeader.flags
#define BOXF_TRADE_PENDING      0x01u     // a "tr" journal entry needs resolving

struct BoxHeader {
  uint16_t magic;              //  0  BOX_MAGIC
  uint8_t  schema_version;     //  2  SAVE_SCHEMA_VERSION
  uint8_t  active_slot;        //  3  0..9, or BOX_ACTIVE_NONE
  uint16_t slot_mask;          //  4  bit s set = slot s holds a Pebble
  uint16_t content_version;    //  6  CONTENT_VERSION the save was made against
  uint32_t next_id_counter;    //  8  monotonic; feeds the next Pebble id
  uint32_t saved_epoch;        // 12
  uint32_t seq;                // 16  pair sequence number
  uint8_t  protocol_version;   // 20  PROTOCOL_VERSION (spec section 31)
  uint8_t  flags;              // 21  BOXF_*
  uint16_t captures;           // 22  lifetime counters, spec section 49
  uint16_t battles;            // 24
  uint8_t  reserved[4];        // 26  must be 0
  uint16_t crc16;              // 30  over bytes 0..29
};

static_assert(sizeof(BoxHeader) == 32, "BoxHeader layout drifted");
static_assert(offsetof(BoxHeader, slot_mask) ==  4, "BoxHeader.slot_mask moved");
static_assert(offsetof(BoxHeader, seq)       == 16, "BoxHeader.seq moved");
static_assert(offsetof(BoxHeader, crc16)     == 30, "BoxHeader.crc16 moved");
static_assert(BOX_CRC_BYTES == sizeof(BoxHeader) - 2, "BoxHeader CRC span drifted");
static_assert(BOX_SLOTS <= 16, "slot_mask is 16 bits");

// -----------------------------------------------------------------------------
// 3. ConfigV2 - 256 B, keys "cfg0"/"cfg1" (pair), plan 1.5.3
//    No Wi-Fi credentials, no Telegram token, no coordinates: the device never
//    associates to a station (spec section 68 r5) and talks to nobody's cloud.
// -----------------------------------------------------------------------------
#define CFGV2_MAGIC             0x5643u   // bytes 'C','V'
#define CFGV2_CRC_BYTES         254
#define CFGV2_NAME_CAP          13        // device_name, NAME_MAX_LEN + NUL
#define CFGV2_AP_PASS_CAP       17        // creator soft-AP passphrase + NUL
#define CFGV2_TZ_CAP            40        // POSIX TZ string + NUL

// ConfigV2.flags
#define CFGV2_F_MUTE            0x0001u
#define CFGV2_F_SH1106          0x0002u   // panel controller override
#define CFGV2_F_SBAR_MASK       0x000Cu   // StatusBarMode, 2 bits at shift 2
#define CFGV2_F_SBAR_SH         2
#define CFGV2_F_WEB             0x0010u   // the creator server is opt-in (radio off
                                          // by default, spec section 68 r5)
#define CFGV2_F_BLE             0x0020u   // the short-range radio may be brought up

struct ConfigV2 {
  uint16_t magic;                        //   0  CFGV2_MAGIC
  uint8_t  version;                      //   2  SAVE_SCHEMA_VERSION
  uint8_t  time_cal_state;               //   3  TimeCal
  uint32_t device_id;                    //   4  spec section 43, generated once
  uint32_t time_cal_epoch;               //   8  when the clock was last set
  uint32_t last_known_epoch;             //  12  mirrors the 60 s "t" cadence
  uint32_t pin_lock_until;               //  16  creator lockout MIRROR, wall
                                         //      clock, DIAG only - never read
                                         //      back as a deadline. See
                                         //      networking/creator_gate.h.
  uint32_t seq;                          //  20  pair sequence number
  uint16_t creator_pin;                  //  24  0 = no PIN issued yet
  uint16_t creator_idle_s;               //  26  D7 idle timeout, seconds.
                                         //      0 = never set -> the compiled
                                         //      default, NOT an instant
                                         //      shutdown (cg_idle_seconds).
  uint16_t flags;                        //  28  CFGV2_F_*
  uint8_t  brightness;                   //  30  OLED contrast
  uint8_t  pin_fail_count;               //  31  0 or CREATOR_PIN_FAIL_MAX only:
                                         //      the ARMED EDGE, not a running
                                         //      count (cg_persist_fails)
  char     device_name[CFGV2_NAME_CAP];  //  32
  char     ap_pass[CFGV2_AP_PASS_CAP];   //  45  STILL HAS NO PRODUCER, and
                                         //      P8-C2 decided that on purpose
                                         //      rather than by omission: the
                                         //      soft AP stays OPEN because a
                                         //      WPA passphrase cannot be
                                         //      carried by the join QR at ANY
                                         //      length. The arithmetic is in
                                         //      ui/screen_creator.cpp next to
                                         //      the payload that would have to
                                         //      hold it.
  char     tz[CFGV2_TZ_CAP];             //  62  POSIX TZ, no network needed
  uint8_t  reserved[152];                // 102  must be 0
  uint16_t crc16;                        // 254  over bytes 0..253
};

static_assert(sizeof(ConfigV2) == 256, "ConfigV2 layout drifted");
static_assert(offsetof(ConfigV2, device_id)   ==   4, "ConfigV2.device_id moved");
static_assert(offsetof(ConfigV2, seq)         ==  20, "ConfigV2.seq moved");
static_assert(offsetof(ConfigV2, device_name) ==  32, "ConfigV2.device_name moved");
static_assert(offsetof(ConfigV2, tz)          ==  62, "ConfigV2.tz moved");
static_assert(offsetof(ConfigV2, crc16)       == 254, "ConfigV2.crc16 moved");
static_assert(CFGV2_CRC_BYTES == sizeof(ConfigV2) - 2, "ConfigV2 CRC span drifted");

// -----------------------------------------------------------------------------
// 4. Inventory - 32 B, keys "inv0"/"inv1" (pair), plan 1.5.3
//
//    NOTE ON SLOT COUNT. The plan's row asks for twelve {item_id, count} pairs
//    AND the header AND the XP ledger AND a u32 epoch AND a crc inside 32 B;
//    that is 41 B of content in a 32 B blob, so the two cannot both hold. The
//    32 B figure is load-bearing (it is quoted again in the NVS entry budget:
//    32 B = 3 entries, and the inv pair is 6 of the 262 entries), so the size
//    is kept and the slot count is what gives: 32 - 18 B of fixed fields = 14 B
//    = seven item kinds. Spec section 24 asks for "no enormous inventory", which
//    seven satisfies; widening it later is a reserved-free schema bump.
//
//    NOTE ON THE XP LEDGER. P3-C2 filled these two fields in, and what they
//    hold is the budget STILL SPENDABLE per metered XpSource plus the epoch the
//    snapshot was taken at - not "granted today", which the layout comment
//    guessed at before game/xp.h existed. The two are the same information, but
//    the spendable form is the one that reconstructs correctly across a power
//    cut: left = min(cap, saved + elapsed / refill_step), which can only
//    under-report. Four buckets is what fits, so the four sources a player can
//    repeat at will are metered and the later ones (capture, item) are
//    farm-proof by construction instead. See game/xp.h.
// -----------------------------------------------------------------------------
#define INV_MAGIC               0x5649u   // bytes 'I','V'
#define INV_CRC_BYTES           30

struct InvSlot {
  uint8_t item_id;             // 0 = empty
  uint8_t count;               // 1..255
};
static_assert(sizeof(InvSlot) == 2, "InvSlot must be 2 B");

struct Inventory {
  uint16_t magic;                        //  0  INV_MAGIC
  uint8_t  version;                      //  2  SAVE_SCHEMA_VERSION
  uint8_t  slots;                        //  3  == INVENTORY_SLOTS
  uint32_t seq;                          //  4  pair sequence number
  uint32_t ledger_epoch;                 //  8  when xp_ledger was snapshotted
  uint8_t  xp_ledger[XP_LEDGER_SLOTS];   // 12  XP still SPENDABLE, by source
  InvSlot  items[INVENTORY_SLOTS];       // 16
  uint16_t crc16;                        // 30  over bytes 0..29
};

static_assert(sizeof(Inventory) == 32, "Inventory layout drifted");
static_assert(offsetof(Inventory, seq)   ==  4, "Inventory.seq moved");
static_assert(offsetof(Inventory, items) == 16, "Inventory.items moved");
static_assert(offsetof(Inventory, crc16) == 30, "Inventory.crc16 moved");
static_assert(INV_CRC_BYTES == sizeof(Inventory) - 2, "Inventory CRC span drifted");

// -----------------------------------------------------------------------------
// 5. CooldownTable - 272 B, keys "cd0"/"cd1" (pair), plan 1.5.3
//    One row per network already explored (spec section 21). net_hash is the
//    abstract identity from wifi_scanner: no SSID or BSSID ever reaches flash.
//
//    The plan's header is "14 B (magic, ver, n, seq, reserved[6])", which lands
//    the u32 rows on an odd 4-byte boundary. The same six reserved bytes are
//    kept, split 4 before the rows and 2 after, so the rows stay naturally
//    aligned and the blob is still exactly 272 B.
//
//    THE ACTIVITY DAY BUCKET (P6-C2, spec section 25). The four bytes that were
//    reserved_a[4] are now act_day + act_score, carved out exactly the way
//    PebbleInstance.corrupt_until_epoch was carved out of reserved[12] at
//    P5-C3, and with the same argument: an old blob reads 0 in both, and 0 is
//    exactly "no activity day has been opened yet", which is the correct state
//    for a save written before this commit. NO SCHEMA BUMP, and the reason is
//    written down rather than assumed - see the four points below and
//    docs/save_schema.md section 5.
//
//    WHY HERE AND NOT IN Inventory, WHICH THE PLAN'S BULLET ASKED FOR:
//    Inventory is 32 B with ZERO padding and ZERO reserved bytes (measured,
//    not merely asserted: 2+1+1+4+4+4+14+2 = 32), so the day bucket has
//    literally nowhere to land in it. Growing it is a SAVE_SCHEMA_VERSION bump,
//    and a bump is not a row in migrate_run()'s step table: pair_load() sorts
//    any non-equal version into `bad` and load_all_inner() returns LOAD_CORRUPT
//    before the migration branch is ever reached, so every v2 save on every
//    device - box, cfg, inv, cd, cs and tr alike, five of which did not change -
//    would need a version-tolerant reader written first. That is the right
//    price for a real widening (INVENTORY_SLOTS 7 -> 12, say). It is the wrong
//    price for four bytes that are already reserved.
//
//    WHY NOT DERIVE THE DAY FROM Inventory.ledger_epoch, the plan's option (a):
//    ledger_epoch is re-stamped every time XP is SPENT (app.cpp's XP funnel),
//    so ledger_epoch / 86400 is "the day of the last XP spend", not "the day
//    these counters belong to". Two different facts, and deriving one from the
//    other would give one fact two owners - the same reason EncounterInput does
//    not carry the cooldown state.
//
//    WHY THIS BLOB AND NOT ConfigV2.reserved[152]: the day bucket is
//    EXPLORATION state and it changes on the same events the rows below do, so
//    it rides the cd_take_dirty() cadence that already exists instead of
//    dirtying a 256 B config blob once a minute. ConfigV2's 152 B stays the
//    tree's one large reserve.
//
//    WHAT IS AND IS NOT IN THESE FOUR BYTES. act_score is the whole persisted
//    half: it is the day's TOTAL, already capped per term at the moment each
//    increment was made, and it is what every reward is computed from. The four
//    per-term counters and the distinct-network set are per-boot RAM in
//    game/activity.cpp - see that header for what a power cycle can and cannot
//    buy with them, stated rather than implied.
// -----------------------------------------------------------------------------
#define CD_MAGIC                0x4443u   // bytes 'C','D'
#define CD_CRC_BYTES            270

struct CooldownRow {
  uint32_t net_hash;           // 0 = empty row
  uint32_t until_epoch;        // the network is explorable again at this epoch
};
static_assert(sizeof(CooldownRow) == 8, "CooldownRow must be 8 B");

struct CooldownTable {
  uint16_t    magic;                     //   0  CD_MAGIC
  uint8_t     version;                   //   2  SAVE_SCHEMA_VERSION
  uint8_t     n;                         //   3  rows in use, <= COOLDOWN_SLOTS
  uint32_t    seq;                       //   4  pair sequence number
  uint16_t    act_day;                   //   8  activity day index, 0 = none yet
  uint16_t    act_score;                 //  10  that day's score, <= ACT_SCORE_MAX
  CooldownRow rows[COOLDOWN_SLOTS];      //  12
  uint8_t     reserved_b[2];             // 268  must be 0
  uint16_t    crc16;                     // 270  over bytes 0..269
};

static_assert(sizeof(CooldownTable) == 272, "CooldownTable layout drifted");
static_assert(offsetof(CooldownTable, seq)   ==   4, "CooldownTable.seq moved");
static_assert(offsetof(CooldownTable, act_day)   ==  8,
              "CooldownTable.act_day moved - it was carved out of reserved_a[4] "
              "and every byte after it must stay where it was");
static_assert(offsetof(CooldownTable, act_score) == 10, "CooldownTable.act_score moved");
static_assert(offsetof(CooldownTable, rows)  ==  12, "CooldownTable.rows moved");
static_assert(offsetof(CooldownTable, crc16) == 270, "CooldownTable.crc16 moved");
static_assert(CD_CRC_BYTES == sizeof(CooldownTable) - 2, "CooldownTable CRC span drifted");

// -----------------------------------------------------------------------------
// 6. CustomSpeciesRec - 192 B, keys "cs0".."cs9" (single), plan 1.5.3
//    A creator upload. A CRC failure here is never fatal: the instance falls
//    back to a placeholder sprite and PBF_HAS_CUSTOM_SPRITE is cleared - the
//    Pebble itself is never destroyed by a bad sprite.
// -----------------------------------------------------------------------------
#define CS_MAGIC                0x5343u   // bytes 'C','S'
#define CS_CRC_BYTES            190
#define CS_SPRITE_BYTES         72        // 24x24 XBM, 3 B per row
#define CS_SPRITE_FRAMES        2
#define CS_NAME_CAP             13
#define CS_BASE_COUNT           4         // hp, atk, def, spd

struct CustomSpeciesRec {
  uint16_t magic;                                     //   0  CS_MAGIC
  uint8_t  version;                                   //   2  SAVE_SCHEMA_VERSION
  uint8_t  slot;                                      //   3  0..9, must match the key
  uint16_t budget_used;                               //   4  spec section 36
  uint8_t  type;                                      //   6  PebbleType
  uint8_t  compat_group;                              //   7  breeding, spec section 17
  uint8_t  base[CS_BASE_COUNT];                       //   8  hp, atk, def, spd
  uint8_t  moves[PB_MOVE_COUNT];                      //  12  learnset
  char     name[CS_NAME_CAP];                         //  16
  uint8_t  reserved[17];                              //  29  must be 0
  uint8_t  sprite[CS_SPRITE_FRAMES][CS_SPRITE_BYTES]; //  46
  uint16_t crc16;                                     // 190  over bytes 0..189
};

static_assert(sizeof(CustomSpeciesRec) == 192, "CustomSpeciesRec layout drifted");
static_assert(offsetof(CustomSpeciesRec, name)   ==  16, "CustomSpeciesRec.name moved");
static_assert(offsetof(CustomSpeciesRec, sprite) ==  46, "CustomSpeciesRec.sprite moved");
static_assert(offsetof(CustomSpeciesRec, crc16)  == 190, "CustomSpeciesRec.crc16 moved");
static_assert(CS_CRC_BYTES == sizeof(CustomSpeciesRec) - 2, "CustomSpeciesRec CRC span drifted");

// -----------------------------------------------------------------------------
// 7. PendingTrade - 64 B, key "tr" (single), plan 1.5.3
//    The two-phase trade journal (spec section 16). Written BEFORE either side
//    of a trade is committed and resolved at the next boot, so a power cut in
//    the middle can never duplicate or vaporise a Pebble.
//    Single key, so no seq: there is no second copy to choose between.
// -----------------------------------------------------------------------------
#define TR_MAGIC                0x5254u   // bytes 'T','R'
#define TR_CRC_BYTES            62
#define TR_WIRE_BYTES           48        // the wire subset of a PebbleInstance

// PendingTrade.phase
#define TRADE_IDLE              0u
#define TRADE_SENT              1u        // ours is out, theirs has not landed
#define TRADE_RECEIVED          2u        // theirs is here, ours is not yet gone
#define TRADE_COMMIT            3u        // both agreed; finish on the next boot

struct PendingTrade {
  uint16_t magic;                        //  0  TR_MAGIC
  uint8_t  version;                      //  2  SAVE_SCHEMA_VERSION
  uint8_t  phase;                        //  3  TRADE_*
  uint32_t out_id;                       //  4  the Pebble id we are sending
  uint32_t peer_id;                      //  8  device_id of the other side
  uint8_t  in_wire[TR_WIRE_BYTES];       // 12  the incoming Pebble, wire form
  uint8_t  reserved[2];                  // 60  must be 0
  uint16_t crc16;                        // 62  over bytes 0..61
};

static_assert(sizeof(PendingTrade) == 64, "PendingTrade layout drifted");
static_assert(offsetof(PendingTrade, in_wire) == 12, "PendingTrade.in_wire moved");
static_assert(offsetof(PendingTrade, crc16)   == 62, "PendingTrade.crc16 moved");
static_assert(TR_CRC_BYTES == sizeof(PendingTrade) - 2, "PendingTrade CRC span drifted");

// -----------------------------------------------------------------------------
// 8. GameState - the whole live game, 1,936 B (plan 1.5.3 "Live RAM")
//    Exactly the six persisted blobs and nothing else, so the RAM budget is
//    the sum of the wire sizes and can be asserted.
// -----------------------------------------------------------------------------
struct GameState {
  PebbleInstance pebbles[BOX_SLOTS];     //    0  10 x 128
  BoxHeader      box;                    // 1280
  ConfigV2       cfg;                    // 1312
  Inventory      inv;                    // 1568
  CooldownTable  cds;                    // 1600
  PendingTrade   trade;                  // 1872
};
static_assert(sizeof(GameState) == 1936, "GameState is no longer the sum of its blobs");

// -----------------------------------------------------------------------------
// 9. THE KEY TABLE. Every NVS key in the product is spelled exactly once here.
//    Keys are ASCII, <= KV_KEY_MAX_LEN characters, and the builders below are
//    total: an out-of-range slot yields an empty string rather than a key that
//    would collide with another blob.
// -----------------------------------------------------------------------------
#define KEY_BOX_PREFIX          "box"     // box0 / box1
#define KEY_CFG_PREFIX          "cfg"     // cfg0 / cfg1
#define KEY_INV_PREFIX          "inv"     // inv0 / inv1
#define KEY_CD_PREFIX           "cd"      // cd0 / cd1
#define KEY_PEBBLE_PREFIX       "pb"      // pb<slot><copy>
#define KEY_CUSTOM_PREFIX       "cs"      // cs0..cs9
#define KEY_TRADE               "tr"
#define KEY_GAIN                "gl"      // the legacy anti-farm ledger, untouched
#define KEY_LASTSEEN            "t"
#define KEY_CANARY              "ok"

// Checkpoint keys, KV_CKPT partition ("nvs2"), all single.
#define KEY_CK_BOX              "ck_box"
#define KEY_CK_CFG              "ck_cfg"
#define KEY_CK_PEBBLE_PREFIX    "ck_pb"   // ck_pb0..ck_pb9

// Legacy v1 keys, namespace "notta". Read once by the migration, then erased.
#define KEY_V1_SAVE             "save"
#define KEY_V1_CFG              "cfg"
#define KEY_V1_EGG              "egg"
#define KEY_V1_ANCESTORS        "anc"

// out must have room for KV_KEY_CAP bytes. Returns out.
const char* key_pebble(uint8_t slot, uint8_t copy, char* out);
const char* key_pair(const char* prefix, uint8_t copy, char* out);
const char* key_custom(uint8_t slot, char* out);
const char* key_ck_pebble(uint8_t slot, char* out);

// -----------------------------------------------------------------------------
// 10. Sealing and checking. Each blob's CRC covers everything but its own
//     trailing crc16; the seal helpers also stamp magic and version, so no
//     caller ever computes either by hand (the bug that a v1 caller could make).
// -----------------------------------------------------------------------------
void pebble_seal(PebbleInstance& p);
bool pebble_blob_ok(const PebbleInstance& p);          // magic + layout_ver + CRC

void box_seal(BoxHeader& b);
bool box_blob_ok(const BoxHeader& b);

void cfgv2_seal(ConfigV2& c);
bool cfgv2_blob_ok(const ConfigV2& c);

void inventory_seal(Inventory& i);
bool inventory_blob_ok(const Inventory& i);

void cooldowns_seal(CooldownTable& c);
bool cooldowns_blob_ok(const CooldownTable& c);

void custom_species_seal(CustomSpeciesRec& c);
bool custom_species_blob_ok(const CustomSpeciesRec& c);

void trade_seal(PendingTrade& t);
bool trade_blob_ok(const PendingTrade& t);

// A blob whose version is HIGHER than this firmware understands. Such a save is
// never rewritten and never wiped (LOAD_FOREIGN_NEWER, plan 1.5.4).
bool schema_is_foreign_newer(uint8_t version);

// True when the slot holds no Pebble at all (species_id and id both zero).
// Inline: game/box.cpp asks this question on every slot walk and is a pure
// module that must not link persistence/save_manager.cpp (plan 1.3 rule 2).
inline bool pebble_is_empty(const PebbleInstance& p) {
  return p.species_id == 0 && p.id == 0;
}

// Zeroes the struct and stamps magic/version/defaults. Used for a fresh unit
// and for every "the blob was absent, use defaults" path.
void pebble_clear(PebbleInstance& p);
void box_defaults(BoxHeader& b);
void cfgv2_defaults(ConfigV2& c);
void inventory_defaults(Inventory& i);
void cooldowns_defaults(CooldownTable& c);
void trade_clear(PendingTrade& t);

#endif // PB_SAVE_SCHEMA_H
