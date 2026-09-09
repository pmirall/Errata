// =============================================================================
//  ERRATA - game/sim.h
//  THE GAME. The only module allowed to mutate the care half of a
//  BugInstance (plan 1.5.1: care[], care_rem[], status, flags,
//  last_updated_epoch, age_s).
//
//  Milli-point stat decay with remainder accumulators, a floored health track,
//  care quality, poop, sickness, stage transitions, action application and the
//  offline catch-up. Nothing here can kill a pet: HEALTH bottoms out at
//  HEALTH_FLOOR_PCT (spec section 27).
//
//  HARD CONSTRAINTS honoured by this module:
//   - ZERO floating point on any path. int32_t milli-points + int16 remainders.
//   - Every stat is clamped at BOTH ends after every operation.
//   - No Arduino headers, no WiFi.h / BLEDevice.h / WebServer.h / U8g2lib.h.
//     The module has no clock and no RNG of its own: the caller feeds it a
//     SimEnv once per logic tick, and every draw comes from the RNG_CARE
//     stream of rng.h (seeded by app.cpp at boot, by the tests explicitly).
//   - Nothing here reads millis(). Game logic time comes from
//     sim_step_seconds() only.
// =============================================================================
#ifndef NT_SIM_H
#define NT_SIM_H

#include <stdint.h>
#include <stddef.h>
#include "../core/nt_types.h"
#include "../persistence/save_schema.h"   // BugInstance, CareId

// -----------------------------------------------------------------------------
// 1. SIM ENVIRONMENT
//    Everything the simulation needs from the outside world, pushed in once per
//    logic tick by app.cpp main loop. A zeroed SimEnv is NOT valid - use
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
#define SIM_EV_POOP         0x00000010u
#define SIM_EV_SICK_START   0x00000020u
#define SIM_EV_SICK_END     0x00000040u
#define SIM_EV_SLEEP        0x00000080u
#define SIM_EV_WAKE         0x00000100u
#define SIM_EV_ALERT        0x00000400u  // sim_alert() changed to a new need
#define SIM_EV_WISH_START   0x00000800u
#define SIM_EV_WISH_OK      0x00001000u
#define SIM_EV_WISH_FAIL    0x00002000u
#define SIM_EV_BIRTHDAY     0x00004000u
#define SIM_EV_VISITA       0x00008000u
#define SIM_EV_LEVEL_UP     0x00010000u  // game/xp.h: at least one level gained
#define SIM_EV_EVOLVE_MINOR 0x00080000u  // child/teen variant chosen

// -----------------------------------------------------------------------------
// 2b. THE PRESENTATION VIEW
//     Everything the renderer and the screens read that is NOT a care number.
//     Four of its members mirror the bound BugInstance (genome, birth_epoch,
//     age_s and, through the legacy PF_* word, status/flags); the rest are the
//     v1 mechanics - life stage, care quality, poop, the minor form - that
//     SaveSchema v2 does not carry and that P3-C1 retires. They live in RAM for
//     as long as a Bug is the active one and are re-derived on sim_bind().
//     P2-C11 replaces this struct with ui/pet_view.h's PetView.
// -----------------------------------------------------------------------------
struct SimView {
  Genome   genome;         // mirror of BugInstance.genome
  uint32_t birth_epoch;    // mirror of BugInstance.birth_epoch
  uint32_t age_s;          // mirror of BugInstance.age_s
  uint16_t flags;          // the legacy PF_* word; the persisted bits mirror
                           // into BugInstance.status / .flags
  int16_t  cq;             // care quality 0..1000 (RAM only)
  uint8_t  stage;          // Stage, derived from BugInstance.level on bind
  uint8_t  minor_form;     // child/teen variant nibbles (RAM only)
  uint8_t  poop_count;     // 0..POOP_MAX (RAM only)
  uint8_t  pad;            // keeps the struct's size stable
};

// -----------------------------------------------------------------------------
// 3. MANDATORY PUBLIC INTERFACE
// -----------------------------------------------------------------------------

// Binds the simulation to the ACTIVE BugInstance the caller owns (the Box,
// through app.cpp). Clamps every care field into its legal range, re-derives
// the view from the persisted fields and resets BOTH the per-Bug
// accumulators and the device-wide anti-farm ledger: a bind is a boot.
// The reference must stay valid for as long as the sim runs.
void     sim_bind(BugInstance& bug);

// Makes `next` the active Bug WITHOUT restarting the device-wide anti-farm
// gain ledger (plan P2-C10). Only the per-Bug accumulators - cadence,
// cooldowns, play windows, care quality, poop, the day counters - are reset, so
// swapping the active slot cannot be used to farm: the hourly point ceiling is
// a property of the device and of real time, not of the creature holding it.
void     sim_switch(BugInstance& next);

// Re-points the simulation at the SAME creature after it MOVED IN MEMORY.
// game/box.cpp stores Bugs by value and box_swap() exchanges two slots'
// CONTENTS, so a swap that touches the active slot leaves the raw g.pb of this
// module aimed one slot away from the creature the player is carrying. The
// caller (game/box_sim.cpp) hands the new address here.
//
// This is NOT sim_switch(): nothing about the creature changed, so none of the
// per-Bug accumulators may be reset and the view must NOT be re-derived -
// care quality, bond, the poop count and the minor form live in this module,
// not in the BugInstance, and derive_view() would put all four back to their
// boot defaults. A rebind is an address correction and nothing else.
// No-op before sim_bind(), and a no-op when `p` is already the bound Bug.
void     sim_rebind(BugInstance& p);

// The sub-step grid. Every cadence in the design (60 s stage check, 600 s
// sickness roll, 600 s care-quality tick) is a multiple of it, and every
// integrator carries its remainder, so a caller that hands over 1800 s at once
// lands on the same bytes as one that ticks a second at a time. It is public
// because that equivalence is a contract a test has to be able to state: a
// rate CHANGE (a poop arriving, the loneliness multiplier turning on) is
// evaluated on this grid, so a finer step charges the new rate up to one
// sub-step earlier - a bounded offset, not a drift.
#define SIM_SUBSTEP_S            60u

// Advances the simulation by `seconds` simulated seconds. Internally sub-steps
// at SIM_SUBSTEP_S so thresholds, poop, sickness and stage checks land on the
// same grid regardless of the chunk size the caller uses.
void     sim_tick(uint32_t seconds);

// Applies one user action. Returns true when it landed; on false, out.err
// carries the ActionErr and out.cooldown_s the seconds remaining.
bool     sim_apply_action(ActionId action, ActionResult& out);

// Simulated seconds per logic tick: 1 normally, the god-mode time scale
// otherwise. The ONLY time source game logic is allowed to read.
//
// THE 1 IS SET BY sim_bind(), NOT BY GOD MODE. sim_set_time_scale() below is an
// override; it is called only from dev/godmode.cpp, which is compiled out of the
// release artefact, so a default that depended on it was a default the shipping
// build never got - and a step of 0 stops the whole simulation. See sim_bind().
uint32_t sim_step_seconds(void);

// 0..100 displayed mood score.
uint8_t  sim_mood_score(void);

// 0..100 whole-point projection of a stat.
uint8_t  sim_stat_pct(StatId id);

// The raw milli-point value of a stat, 0..100000. ST_BOND has no home in
// SaveSchema v2 and lives in RAM; every other StatId is one of the five care[]
// entries under the CareId name (plan P2-C10 field map).
int32_t  sim_stat_milli(StatId id);

// -----------------------------------------------------------------------------
// 4. ENVIRONMENT FEED  (sim owns no clock and no RNG)
// -----------------------------------------------------------------------------
void     sim_seed(uint32_t seed);            // reseeds RNG_CARE (wrapper on rng_seed)
void     sim_set_env(const SimEnv& env);     // once per logic tick
const SimEnv& sim_env(void);
// God mode's acceleration: 1/6/60/360/3600. An OVERRIDE of sim_bind()'s 1, and
// never the thing that sets it - dev/godmode.cpp is compiled out of `release`.
// A scale of 0 is clamped to 1: a step of zero is not a speed, it is a stop.
void     sim_set_time_scale(uint32_t scale);

// -----------------------------------------------------------------------------
// 5. LIFE CYCLE
// -----------------------------------------------------------------------------
// Initialises the bound Bug to a brand new egg carrying `g`. cold != 0
// applies the cold-egg penalty (hatches at EGG_COLD_HEALTH_PCT health). The
// live first boot no longer takes this path - it creates a level-1 starter
// through game/box.h - but god mode and the tests still do.
void     sim_new_pet(const Genome& g, uint32_t now_epoch, uint8_t cold);

// Hatch now (the egg screen's rub gesture / auto-hatch). No-op unless STAGE_EGG.
void     sim_hatch(void);

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
// sim.cpp does no I/O. These two calls are how the hourly gain
// budget survives a power cut: persistence owns the bytes and the NVS key, the
// entry point wires them together, and nothing here knows that NVS exists.
// -----------------------------------------------------------------------------

// Whole points still unspent, one entry per StatId; unmetered stats report 0.
// The fraction is dropped TOWARD ZERO, so a save/restore round trip can only
// under-report the budget - it can never invent a point. out_pts must have
// ST_COUNT elements.
void     sim_gain_snapshot(uint8_t out_pts[ST_COUNT]);

// Re-seeds the ledger from a snapshot taken at saved_epoch:
//
//     budget = min(cap, saved)
//
// IT AGES NOTHING FORWARD, SINCE P7-C6, and that is the whole point. It used to
// be min(cap, saved + elapsed * cap / 3600), which reads a wall clock at BOTH
// ends of the interval - and the wall clock is typed on the time screen, and
// one hour of "elapsed" refills a whole cap. Measured: 100 rounds of
// (clock +1 h, reboot) from a fully spent ledger manufactured 4,000 happiness
// gain points against a cap of 40 an hour, and the same restore with no gap
// manufactured 0. That is the same shape xp_ledger_restore() lost at P6-C4.
//
// WHAT THE HONEST PLAYER LOSES, stated because it is a real cost and not a
// rounding error: off-time no longer refills the hourly gain budget. Come back
// after an hour away and the ledger holds what it held when the device went
// off, so a full meal may be up to 29 minutes out. An unkind hour is
// recoverable; a stat budget a power cycle can refill is not.
//
// Call it once, immediately after sim_bind(), BEFORE the boot's catch-up: the
// catch-up's gain_refill() then covers the absence [last_seen, now] out of
// seconds THIS DEVICE WATCHED PASS, which is the only refill left.
//
// Returns 1 when the snapshot was used. It returns 0 - and seeds ZERO, today's
// safe behaviour, never the cap - when there is no snapshot or when either
// epoch is below NT_EPOCH_SANE_MIN, i.e. when the elapsed time cannot be
// trusted (the findings 1/2 reasoning: before SNTP lands these are uptime
// counters, and an uptime difference does not describe wall-clock time).
uint8_t  sim_gain_restore(const uint8_t* pts, uint8_t n,
                          uint32_t saved_epoch, uint32_t now_epoch);

// A MINIGAME HAS NO COOLDOWN. sim_minigame_cooldown_s() used to be declared
// here and it is gone, not stubbed: the 120 s lockout let the owner play one
// twenty-second game per visit to the device. data/balance.h's decay curve is
// the anti-farm now and it takes the REWARD down instead of the button away.

// Number of plays inside the rolling PLAY_DECAY_WINDOW_S window, 0..PLAY_DECAY_STEPS-1.
// The payout is scaled by PLAY_DECAY_0.. permille from this.
uint8_t  sim_play_window_count(void);

// Rolling-window happiness payout scale, permille (1000 / 700 / 450 / ...).
uint16_t sim_play_decay_permille(void);

// An on-device (GAME screen) minigame finished. win_permille 0..1000 is how well the
// player did; >= 500 counts as a win for minigames_won and the branch score.
//
// `paid_permille`, when given, receives the score AFTER the rolling-window decay
// - i.e. what this run was actually worth. THE CALLER MUST USE IT RATHER THAN
// RE-READING sim_play_decay_permille(): this call pushes the run into the
// window, so the curve answers differently before and after it, and an XP award
// computed on the wrong side would drift one step out of step with the
// happiness the same run paid. One reader, one moment, no order to get wrong.
bool     sim_apply_play_result(uint16_t win_permille, ActionResult& out,
                               uint16_t* paid_permille = nullptr);

// -----------------------------------------------------------------------------
// 7. QUERIES
// -----------------------------------------------------------------------------
const SimView*        sim_view(void);   // read-only presentation view for ui
const BugInstance* sim_bug(void); // the bound Bug, read-only
uint32_t sim_take_events(void);         // returns and CLEARS the event bitmask
// The one door for the OTHER game systems into the event word the UI drains.
// game/xp.h has no UI of its own and no clock, and a second event channel
// would mean a second place ui_note_events() has to be kept in step with; the
// bits it posts (SIM_EV_LEVEL_UP today) are drained by the same call as the
// care simulation's own. It ORs, so it can never clear somebody else's event.
void     sim_post_event(uint32_t mask);
uint8_t  sim_alert(void);               // AlertId currently demanding attention
uint8_t  sim_is_asleep(void);
uint8_t  sim_is_sick(void);
uint32_t sim_age_s(void);
uint32_t sim_now(void);                 // sim's own epoch cursor (offline-aware)

// Runs the offline integration for `absence_s` and reports what it did.
// clock_known = 0 takes the unknown-clock path, which charges ZERO and arms
// PF_ABS_UNKNOWN for sim_absence_retrofix() (plan section 1.7).
void     sim_catch_up_ex(uint32_t absence_s, uint8_t clock_known,
                         AbsenceReport& rep);

// Charges the truth once gt_set_epoch() lands after an unknown-clock return:
// nothing was integrated at boot, so this integrates the whole absence. No-op
// unless PF_ABS_UNKNOWN is armed.
void     sim_absence_retrofix(uint32_t true_absence_s);

// -----------------------------------------------------------------------------
// 8. GOD MODE HOOKS (godmode.cpp is compiled always; sim owns the mutations)
// -----------------------------------------------------------------------------
void     sim_god_set_stat(StatId id, uint8_t pct);
void     sim_god_set_sick(uint8_t sick);        // clears/sets PF_SICK by hand
void     sim_god_set_stage(uint8_t stage);      // Stage; recomputes minor forms

// Installs `g` into the live pet in place (genesis roll, pasted 32-hex genome,
// per-gene editor, synthetic BLE child). The genome is RESEALED and the
// god_tainted bit is FORCED on, so no path through god mode can produce an
// untainted genome. Nothing else is disturbed, so the pet
// keeps its age, stats and stage. No-op before sim_bind().
void     sim_god_set_genome(const Genome& g);

#endif // NT_SIM_H
