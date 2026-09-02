// =============================================================================
//  NOTTAMAGOCHI - sim.h
//  THE GAME. The only module allowed to mutate PetSave.
//
//  Implements GAME_DESIGN 0 / 1 / 2 / 5.2 : milli-point stat decay with
//  remainder accumulators, health damage attribution, care quality, poop,
//  sickness, stage transitions, adult branch scoring, action application and
//  the offline catch-up.
//
//  HARD CONSTRAINTS honoured by this module:
//   - ZERO floating point on any path. int32_t milli-points + int16 remainders.
//   - Every stat is clamped at BOTH ends after every operation.
//   - No Arduino headers, no WiFi.h / BLEDevice.h / WebServer.h / U8g2lib.h.
//     The module has no clock and no RNG of its own: the caller feeds it a
//     SimEnv once per logic tick, and every draw comes from the RNG_CARE
//     stream of rng.h (seeded by the .ino at boot, by the tests explicitly).
//   - Nothing here reads millis(). Game logic time comes from
//     sim_step_seconds() only.
// =============================================================================
#ifndef NT_SIM_H
#define NT_SIM_H

#include <stdint.h>
#include <stddef.h>
#include "nt_types.h"

// -----------------------------------------------------------------------------
// 1. SIM ENVIRONMENT
//    Everything the simulation needs from the outside world, pushed in once per
//    logic tick by the .ino main loop. A zeroed SimEnv is NOT valid - use
//    sim_env_defaults() and then fill in what you know.
// -----------------------------------------------------------------------------
struct SimEnv {
  uint32_t now_epoch;         //  0  gt_now(). Timestamps written into PetSave.
  uint16_t day_of_year;       //  4  0..365. Daily caps + the wish seed.
  uint8_t  local_hour;        //  6  0..23 local time
  uint8_t  local_min;         //  7  0..59 local time
  uint8_t  clock_valid;       //  8  0 = no SNTP yet: no sleep window, no wish
  uint8_t  reserved[15];      //  9  was the weather block; holds sizeof at 24
};
static_assert(sizeof(SimEnv) == 24, "SimEnv layout drifted");

// Fills env with the neutral, clock-less defaults.
void sim_env_defaults(SimEnv& env);

// -----------------------------------------------------------------------------
// 2. EVENT BITMASK - sim_take_events() returns and clears these.
//    One call drains everything the UI and render layers need to react to;
//    the simulation never calls into them.
// -----------------------------------------------------------------------------
#define SIM_EV_HATCHED      0x00000001u  // egg -> BABY
#define SIM_EV_STAGE_UP     0x00000002u  // any stage transition (freeze + anim)
#define SIM_EV_ADULT_FORM   0x00000004u  // TEEN -> ADULT, adult_form now valid
#define SIM_EV_DIED         0x00000008u  // health <= 0 or age threshold
#define SIM_EV_POOP         0x00000010u
#define SIM_EV_SICK_START   0x00000020u
#define SIM_EV_SICK_END     0x00000040u
#define SIM_EV_SLEEP        0x00000080u
#define SIM_EV_WAKE         0x00000100u
#define SIM_EV_CARE_MISS    0x00000200u
#define SIM_EV_ALERT        0x00000400u  // sim_alert() changed to a new need
#define SIM_EV_WISH_START   0x00000800u
#define SIM_EV_WISH_OK      0x00001000u
#define SIM_EV_WISH_FAIL    0x00002000u
#define SIM_EV_BIRTHDAY     0x00004000u
#define SIM_EV_VISITA       0x00008000u
#define SIM_EV_STORM        0x00010000u  // scripted night storm begins
#define SIM_EV_STORM_HURT   0x00020000u  // storm ended without 3 mimos
#define SIM_EV_OVERFED      0x00040000u
#define SIM_EV_EVOLVE_MINOR 0x00080000u  // child/teen variant chosen
#define SIM_EV_WEIGHT_OBESE 0x00100000u

// -----------------------------------------------------------------------------
// 3. MANDATORY PUBLIC INTERFACE (BRIEF 4, row 5)
// -----------------------------------------------------------------------------

// Binds the simulation to a PetSave the caller owns (storage.cpp). Clamps every
// field into its legal range and resets the RAM-only cadence accumulators.
// The pointer must stay valid for as long as the sim runs.
void     sim_init(PetSave& save);

// Advances the simulation by `seconds` simulated seconds. Internally sub-steps
// at SIM_SUBSTEP_S so thresholds, poop, sickness and stage checks land on the
// same grid regardless of the chunk size the caller uses.
void     sim_tick(uint32_t seconds);

// Applies one user action. Returns true when it landed; on false, out.err
// carries the ActionErr and out.cooldown_s the seconds remaining.
bool     sim_apply_action(ActionId action, ActionResult& out);

// Simulated seconds per logic tick: 1 normally, the god-mode time scale
// otherwise. The ONLY time source game logic is allowed to read.
uint32_t sim_step_seconds(void);

// 0..100 displayed mood score (GAME_DESIGN 6.3).
uint8_t  sim_mood_score(void);

// 0..100 whole-point projection of a stat.
uint8_t  sim_stat_pct(StatId id);

// -----------------------------------------------------------------------------
// 4. ENVIRONMENT FEED  (sim owns no clock and no RNG)
// -----------------------------------------------------------------------------
void     sim_seed(uint32_t seed);            // reseeds RNG_CARE (wrapper on rng_seed)
void     sim_set_env(const SimEnv& env);     // once per logic tick
const SimEnv& sim_env(void);
void     sim_set_time_scale(uint32_t scale); // god mode: 1/6/60/360/3600

// -----------------------------------------------------------------------------
// 5. LIFE CYCLE
// -----------------------------------------------------------------------------
// Initialises the bound save to a brand new egg carrying `g`. cold != 0 applies
// the GAME_DESIGN 5.4 cold-egg penalty (hatches at EGG_COLD_HEALTH_PCT health).
void     sim_new_pet(const Genome& g, uint32_t now_epoch, uint8_t cold);

// Hatch now (S13 "frotar el huevo" / auto-hatch). No-op unless STAGE_EGG.
void     sim_hatch(void);

// Marks the corpse buried (S12 -> lineage -> egg). No-op unless dead.
void     sim_bury(void);

// Seconds of age at which this pet dies of old age, form multiplier included.
uint32_t sim_natural_death_s(void);

// A..F from the care-quality accumulator. Feeds the lineage screen.
uint8_t  sim_care_grade(void);

// -----------------------------------------------------------------------------
// 6. SHARED COOLDOWN / HOURLY-GAIN LEDGER
//    ONE ledger for the on-device minigames and the S1/S2/S3 menu actions.
//    Everything that can raise a stat goes through it, so no surface can be
//    farmed.
// -----------------------------------------------------------------------------
// Seconds left before `action` may be used again. 0 = ready now.
uint16_t sim_action_cooldown_s(ActionId action);

// Whole points still available this hour for a metered stat
// (ST_HUNGER / ST_HAPPINESS / ST_ENERGY / ST_HYGIENE). 0xFFFF = unmetered.
uint16_t sim_gain_left(StatId id);

// -----------------------------------------------------------------------------
// LEDGER PERSISTENCE SEAM  (PH3 finding 4 / PH4 section 6 item 1)
// sim.cpp does no I/O (BRIEF section 4). These two calls are how the hourly gain
// budget survives a power cut: storage.cpp owns the bytes and the NVS key, the
// entry point wires them together, and nothing here knows that NVS exists.
// -----------------------------------------------------------------------------

// Whole points still unspent, one entry per StatId; unmetered stats report 0.
// The fraction is dropped TOWARD ZERO, so a save/restore round trip can only
// under-report the budget - it can never invent a point. out_pts must have
// ST_COUNT elements.
void     sim_gain_snapshot(uint8_t out_pts[ST_COUNT]);

// Re-seeds the ledger from a snapshot taken at saved_epoch, as of now_epoch:
//
//     budget = min(cap, saved + elapsed * cap / 3600)
//
// integer throughout, with elapsed clamped to one hour before the multiply (an
// hour refills the whole cap, so anything longer is the same answer and the
// clamp is what makes an elapsed of years harmless).
//
// Call it once, immediately after sim_init(), BEFORE the boot's catch-up: the
// catch-up's own refill then covers the absence [last_seen, now] on top, which
// is exactly the remaining term of the same expression.
//
// Returns 1 when the snapshot was used. It returns 0 - and seeds ZERO, today's
// safe behaviour, never the cap - when there is no snapshot or when either
// epoch is below NT_EPOCH_SANE_MIN, i.e. when the elapsed time cannot be
// trusted (the findings 1/2 reasoning: before SNTP lands these are uptime
// counters, and an uptime difference does not describe wall-clock time).
uint8_t  sim_gain_restore(const uint8_t* pts, uint8_t n,
                          uint32_t saved_epoch, uint32_t now_epoch);

// Seconds left before a minigame may be played again.
uint16_t sim_minigame_cooldown_s(void);

// Number of plays inside the rolling PLAY_DECAY_WINDOW_S window, 0..5.
// The happiness payout is scaled by PLAY_DECAY_0..5 permille from this.
uint8_t  sim_play_window_count(void);

// Rolling-window happiness payout scale, permille (1000 / 700 / 450 / ...).
uint16_t sim_play_decay_permille(void);

// An on-device (S4) minigame finished. win_permille 0..1000 is how well the
// player did; >= 500 counts as a win for minigames_won and the branch score.
bool     sim_apply_play_result(uint16_t win_permille, ActionResult& out);

// -----------------------------------------------------------------------------
// 7. QUERIES
// -----------------------------------------------------------------------------
const PetSave* sim_save(void);          // read-only view for ui
uint32_t sim_take_events(void);         // returns and CLEARS the event bitmask
uint8_t  sim_alert(void);               // AlertId currently demanding attention
uint16_t sim_sulk_left_s(void);         // post-absence refusal timer, 0 = none
uint8_t  sim_is_asleep(void);
uint8_t  sim_is_sick(void);
uint8_t  sim_is_dead(void);
uint32_t sim_age_s(void);
uint32_t sim_now(void);                 // sim's own epoch cursor (offline-aware)

// Full absence report (tier, death, exact elapsed, sulk timer).
// clock_known = 0 takes the ABS_UNKNOWN path.
void     sim_catch_up_ex(uint32_t absence_s, uint8_t clock_known,
                         AbsenceReport& rep);

// Retro-applies the difference once SNTP lands after an ABS_UNKNOWN return.
void     sim_absence_retrofix(uint32_t true_absence_s);

// -----------------------------------------------------------------------------
// 8. GOD MODE HOOKS (godmode.cpp is compiled always; sim owns the mutations)
// -----------------------------------------------------------------------------
void     sim_god_set_stat(StatId id, uint8_t pct);
void     sim_god_set_stage(uint8_t stage);      // Stage; recomputes forms
void     sim_god_set_form(uint8_t form);        // AdultForm
void     sim_god_kill(uint8_t cause);           // DeathCause; never resurrects

// Installs `g` into the live pet in place (genesis roll, pasted 32-hex genome,
// per-gene editor, synthetic BLE child). The genome is RESEALED and the
// god_tainted bit is FORCED on, so no path through god mode can produce an
// untainted genome (GAME_DESIGN 10.2). Weight is re-clamped against the new
// body_size gene; nothing else is disturbed, so the pet keeps its age, stats
// and stage. No-op before sim_init().
void     sim_god_set_genome(const Genome& g);

// PF_SICK on/off, resetting the untreated-sickness attribution counter so a
// forced illness is attributed from now, not from whenever the last real one
// started.
void     sim_god_set_sick(uint8_t on);

// poop_count 0..POOP_MAX, resetting the poop cadence accumulator.
void     sim_god_set_poop(uint8_t count);

#endif // NT_SIM_H
