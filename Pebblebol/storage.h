// =============================================================================
//  NOTTAMAGOCHI - storage.h
//  NVS persistence (Preferences), write-cadence policy, RTC_NOINIT boot nonce
//  and reset-reason classification.
//
//  Namespace "notta", keys "t" / "save" / "cfg" / "egg" / "ok" / "gl".
//  Every key and the namespace are <= 15 chars (NVS_KEY_NAME_MAX_SIZE 16).
//
//  This is the ONLY module that talks to Preferences. It interprets no game
//  rules at all: it validates blobs, rate-limits flash writes, and tells the
//  rest of the firmware how this boot started so the absence mechanic never
//  accuses the user of an abandonment that was really a crash.
//
//  Identifiers/comments English. No user-facing strings here (strings_es.h).
// =============================================================================
#ifndef NT_STORAGE_H
#define NT_STORAGE_H

#include <stdint.h>
#include <stddef.h>
#include "nt_types.h"

// -----------------------------------------------------------------------------
// Storage-local constants
// -----------------------------------------------------------------------------

// Flash-wear floors. These are wear policy, not game logic: they are measured
// with millis() because the game clock may be unknown or may jump under SNTP.
#define STORE_SAVE_MIN_GAP_MS   1000UL   // two "save" writes may not be closer
#define STORE_T_MIN_GAP_MS      5000UL   // floor for a clock-correction "t" write
#define STORE_CLOCK_JUMP_S      3600UL   // epoch moving this much forces a "t" write

// store_error() bit field. Sticky until store_clear_error().
#define STORE_E_NONE            0x00u
#define STORE_E_OPEN            0x01u    // Preferences::begin() failed
#define STORE_E_CANARY          0x02u    // store_selftest() failed
#define STORE_E_SAVE_W          0x04u    // putBytes("save") short write
#define STORE_E_LASTSEEN_W      0x08u    // putULong64("t") short write
#define STORE_E_CFG_W           0x10u    // putBytes("cfg") short write
#define STORE_E_EGG_W           0x40u    // putBytes("egg") short write
#define STORE_E_SAVE_CRC        0x80u    // a "save" blob failed its CRC
// Bit 0x20 is retired with the ancestor ring. The gain ledger has no bit of its
// own either: store_error() is a uint8_t the UI and god mode already read, and
// "gl" is written only as part of a "save" transaction - so a failure there is
// reported as STORE_E_SAVE_W with the label "gain", which is what the Serial
// line prints.

// -----------------------------------------------------------------------------
// HOURLY-GAIN LEDGER  (PH3 finding 4 / PH4 section 6 item 1), NVS key "gl"
//
// The anti-farm ceiling (config.h section 12, BRIEF 1.6) is a per-stat budget of
// whole points per hour, spent by every action that can raise a stat. It lives
// in sim.cpp RAM. Persisting it is what turns "assume the worst on every reboot"
// - which told a 19 % pet it was full for 29 minutes - into "remember what was
// actually left", while still capping a reboot-farm exploit at the points that
// could be spent since the last save instead of a whole hour's worth.
//
// The blob is deliberately NOT part of PetSave: PetSave is exactly 128 B on the
// wire, static_assert-ed with five offsetof guards, and reserved[124..125] is
// two bytes. Widening it would refuse every save already in the field.
//
// Whole points only. Every cap is < 256 (see the static_asserts in storage.cpp),
// the fraction is dropped toward zero by the writer, so a round trip can only
// ever under-report the budget - never invent a point.
// -----------------------------------------------------------------------------
#define NT_GAIN_SLOTS           ((uint8_t)ST_COUNT)
#define GAINSAVE_CRC_BYTES      18

struct GainSave {
  uint16_t magic;                    //  0  NT_GAIN_MAGIC
  uint8_t  version;                  //  2  NT_GAIN_VERSION
  uint8_t  slots;                    //  3  == NT_GAIN_SLOTS; a StatId change is refused
  uint32_t epoch;                    //  4  wall clock the snapshot was taken at,
                                     //     or 0 when there was no trustworthy one
  uint8_t  pts[NT_GAIN_SLOTS];       //  8  whole points still unspent, by StatId
  uint8_t  reserved[4];              // 14  grew by one when ST_DISCIPLINE went,
                                     //     so the blob stays exactly 20 B
  uint16_t crc16;                    // 18  CRC-16/CCITT-FALSE over bytes 0..17
};
static_assert(sizeof(GainSave) == 20, "GainSave must be 20 B on the wire to NVS");
static_assert(GAINSAVE_CRC_BYTES == sizeof(GainSave) - 2, "GainSave CRC span drifted");

// Reads and validates key "gl". Returns false - and leaves pts[] zeroed and
// epoch 0 - when the key is absent, short, foreign, or fails its CRC. A false
// return means "seed nothing", i.e. the safe pre-PH4 behaviour; it is never a
// reason to refuse the pet.
bool     store_load_gain(uint8_t pts[NT_GAIN_SLOTS], uint32_t& epoch);

// Writes key "gl". epoch below NT_EPOCH_SANE_MIN is stored as 0, which the
// loader treats as untrustworthy: on a unit that has never met NTP there is no
// elapsed time to reconstruct from, and replaying a stale but TRUSTED snapshot
// on every reboot would hand the exploit straight back.
bool     store_save_gain(const uint8_t pts[NT_GAIN_SLOTS], uint32_t epoch);

// The layering seam (BRIEF section 4: sim.cpp does no I/O, storage.cpp owns the
// bytes). store_save() calls this provider at the instant it commits a "save"
// and writes whatever it hands back, so the snapshot is never stale and "gl"
// inherits the save's cadence exactly - no new timer, and at worst one 20 B blob
// per save that already happens. The wear filter at store_save_gain() drops that
// to zero writes for an idle pet (every budget at its cap) and for a unit with
// no clock; it deliberately does NOT skip on matching points alone, because
// below the cap a stale epoch reconstructs as free budget. The entry point binds
// this to a thunk over sim_gain_snapshot(); with nothing bound, store_save()
// behaves exactly as it did before.
typedef bool (*StoreGainFn)(uint8_t pts[NT_GAIN_SLOTS], uint32_t& epoch);
void     store_bind_gain(StoreGainFn fn);

// -----------------------------------------------------------------------------
// PUBLIC INTERFACE - BRIEF section 4, row 6
// -----------------------------------------------------------------------------

// Opens NVS read-write, re-arms the RTC nonce, runs the canary self-test and
// classifies this boot. Call once, first thing in setup(), before any other
// store_* call. Returns false if NVS could not be opened (the firmware still
// runs, RAM-only; store_healthy() stays false and the UI should show ERR_NVS).
bool     store_begin(void);

// Reads and validates key "save". Returns false and leaves 'out' zeroed when
// there is no save, the magic/version is foreign, or the CRC fails - the caller
// must then start a fresh gen-0 egg. On success out.last_seen_epoch is
// reconciled with key "t" and with the RTC mirror, whichever is newest.
bool     store_load(PetSave& out);

// Writes key "save". Unforced calls obey SAVE_FULL_PERIOD_S and are therefore
// safe to make every tick. force=true means "state changed" (input, stage
// transition, hatch); it still honours a STORE_SAVE_MIN_GAP_MS floor and defers
// to the next call rather than dropping the write. magic/version/crc16 are
// stamped by this function; the caller never computes them.
// Returns false only on a real NVS failure.
bool     store_save(const PetSave& s, bool force = false);

// Mirrors 'epoch' into RTC fast memory on every call (free) and writes key "t"
// at most once per SAVE_LASTSEEN_PERIOD_S - except a clock correction of
// STORE_CLOCK_JUMP_S or more, which forces a write. Pass 0 to reuse the last
// epoch this module was given. Safe to call every tick.
bool     store_touch_lastseen(uint32_t epoch = 0);

// Reads and validates key "cfg". On any failure 'out' is filled with the
// defaults compiled in from the config.h user block and false is returned
// (which means "defaults in use", not an error).
bool     store_load_cfg(Config& out);

// Stamps magic/version/reserved/the NUL terminators and re-seals crc16 IN THE
// CALLER'S STRUCT, then writes key "cfg". Call on settings change only.
//
// The reference is deliberately non-const (PH3 finding 5). 'c' is normally the
// entry point's live g_cfg, shared by pointer with ui and webui; the
// .ino watches g_cfg.crc16 to decide when to re-apply the two settings that
// live outside the struct (OLED contrast, WiFi credentials).
// Sealing into a private copy left the caller's crc16 frozen forever, so that
// detector could never fire and e.g. a brightness change never reached the
// panel until the next reboot. The seal happens even when NVS is closed, so a
// RAM-only unit still applies its settings for the current session.
bool     store_save_cfg(Config& c);

// How this boot started. Valid after store_begin().
BootKind store_boot_kind(void);

// -----------------------------------------------------------------------------
// ADDITIONS beyond the BRIEF section 4 row (documented in the module report).
// Nothing here renames or reshapes the eight entries above.
// -----------------------------------------------------------------------------

// Writes a fresh random canary to key "ok" and reads it back. Called by
// store_begin(); exposed so god mode / settings can re-run it on demand.
bool     store_selftest(void);

// true when NVS is open and no write has failed since boot.
bool     store_healthy(void);
uint8_t  store_error(void);           // STORE_E_* bit field, sticky
void     store_clear_error(void);
uint16_t store_write_fails(void);     // count of short/failed writes since boot

// Raw esp_reset_reason() value captured at store_begin() (esp_reset_reason_t).
uint8_t  store_reset_reason(void);

// Newest last_seen_epoch this module knows (NVS "t" / RTC / last save).
uint32_t store_last_seen(void);
// The RTC mirror as found at boot, before it was re-armed. 0 if the nonce was
// lost (i.e. the device really did lose power).
uint32_t store_rtc_last_seen(void);
// true when the RTC_NOINIT nonce survived, i.e. this was NOT a power loss.
bool     store_rtc_intact(void);

uint32_t store_boot_count(void);
void     store_rtc_mark_god(void);    // god mode was entered this power cycle
bool     store_rtc_god_tainted(void);

// Pending egg blob, NVS key "egg" (nt_types.h section 4). No caller produces
// one today; breeding comes back in Phase 7.
bool     store_load_egg(PendingEgg& out);   // validates magic/version/EF_VALID/genome CRC
bool     store_save_egg(const PendingEgg& e);
bool     store_clear_egg(void);

// Factory reset: clears the whole namespace, re-arms the canary and the RTC
// nonce. The caller is responsible for building the new gen-0 egg afterwards.
bool     store_wipe(void);

// Fills 'c' with the compiled-in defaults from the config.h user block,
// including a valid crc16. Used by store_load_cfg() on a miss and by the
// settings "reset" path.
void     store_cfg_defaults(Config& c);

// CRC-16/CCITT-FALSE over a blob: a thin wrapper on crc16_ccitt() (crc16.h),
// the one CRC that guards PetSave, Config, GainSave, Genome and the BLE frames.
uint16_t store_crc16(const void* data, size_t len);

#endif // NT_STORAGE_H
