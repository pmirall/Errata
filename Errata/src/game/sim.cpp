// =============================================================================
//  ERRATA - game/sim.cpp
//  The stat model. The ONLY module allowed to mutate the care half of a
//  BugInstance.
//
//  Numeric contract (BRIEF risk #10):
//    * every stat is int32_t milli-points in [0, 100000]
//    * every rate is milli-points PER HOUR; per-step delta is
//        (rate * dt + rem) / 3600   with rem carried in BugInstance.care_rem[]
//      so integer division never drifts, at any step size
//    * multiplier chains run in micro-points/hour (int64) and are rounded ONCE
//      back to milli-points/hour, so a five-deep chain costs < 0.001 pt/h
//    * clamps at BOTH ends after every operation
//    * ZERO floating point anywhere in this file
// =============================================================================
#include "sim.h"
#include "daylight.h"
#include "genome.h"
#include "../core/rng.h"
#include "../core/strings_es.h"
#include "../data/balance.h"   // every care rate, gain, cooldown and cap

#include <string.h>

// SIM_SUBSTEP_S, the sub-step grid, is declared in sim.h: the chunk-size
// equivalence it buys is a contract tests state, not a private detail.

// Auto-wake thresholds (see the note on "collapse" in sleep_machine()).
#define SIM_WAKE_DAY_ENERGY_PCT  60
#define SIM_ALERT_HEALTH_PCT     30

// =============================================================================
// 1. MODULE STATE - ONE struct, ONE instance (plan P2-C10 step 1)
//    The persisted half lives in the bound BugInstance (care[], care_rem[],
//    status, flags, age_s, birth_epoch, last_updated_epoch, genome,
//    minigames_won). Everything else is CareCtx, and CareCtx is split in two:
//
//      PER-BUG  cadence accumulators, alerts, cooldowns, the play windows,
//                  the day counters and the v1 mechanics SaveSchema v2 does not
//                  carry (stage, care quality, poop, wish, bond). sim_switch()
//                  RESETS these: they describe the creature in your hand.
//      DEVICE-WIDE gain_budget[] / gain_rem[], the hourly anti-farm ledger.
//                  sim_switch() KEEPS it. That is what makes swapping the
//                  active Bug worthless as a farming move: the ceiling is a
//                  property of the device and of real time, not of the
//                  creature, so ten Bugs share one hour's points.
//
//    PH3 #4 - THE ANTI-FARM LEDGERS ARE THE EXCEPTION to "RAM-only state is
//    free". Four of the fields below meter how much a player can gain per unit
//    of REAL time, so zeroing their phase on reboot converts directly into stat
//    points:
//      gain_budget[]  the hourly gain ceiling (BRIEF 1.6)
//      act_last[]     the six per-action cooldowns + the global one
//      play_at[]      the 3 h play-payout decay window
//    reset_bug_state(0) / reset_gain_ledger(0) seed all four as if they had
//    JUST been spent, and the boot's offline catch-up then refills them at
//    exactly the rate real time would have, so a reboot is worth its own
//    wall-clock duration and no more.
//
//    gain_budget[] IS NOW PERSISTED (PH4 section 6 item 1). "Assume the worst"
//    killed the exploit but lied to the player: measured on an adult at 20 %
//    satiety after a 5 s brownout, FEED_MEAL answered AERR_FULL - "esta llena"
//    - from 21 s to 59 s, and a full 30-point meal was unavailable for 1799 s
//    (29 min). The ledger is therefore written as whole points next to the
//    Bug, and sim_bind()'s zero is overwritten by the caller:
//
//      sim_gain_snapshot()  hands the live budget out, truncated toward zero
//      sim_gain_restore()   seeds min(cap, saved + elapsed*cap/3600)
//
//    This module still does NO I/O (BRIEF section 4). persistence owns the 20 B
//    blob and NVS key "gl", the entry point calls sim_gain_restore() right
//    after sim_bind() and binds a snapshot provider that storage calls at the
//    instant it commits a Bug; the ledger rides that write and adds no NVS
//    cadence of its own.
//
//    WHAT THE EXPLOIT COSTS NOW. A reboot restores the budget that was true at
//    the last save, not a fresh hour of it. ui.cpp forces a save after every
//    successful action, so a spend is on flash within STORE_SAVE_MIN_GAP_MS;
//    the widest window is a web/API action, which is only covered by the
//    periodic save, i.e. <= SAVE_FULL_PERIOD_S (300 s) of spending replayable
//    per power cycle instead of a whole hour's cap. The honest player pays
//    nothing: after a brownout the budget is what it was, so a hungry pet eats.
//
//    RESIDUAL 1, KNOWN AND ACCEPTED: play_at[] is NOT reconstructed and NOT
//    persisted. The honest reconstruction is "assume the window was full",
//    which would drop the play payout to its FLOOR for an hour after any
//    innocent power cut - a far larger punishment than the exploit it prevents,
//    and one the floor now bounds rather than zeroes. It still resets on reboot.
//    RESIDUAL 2: the cooldowns are still seeded as "just started", so the first
//    seconds after a reboot can answer AERR_COOLDOWN. That message is TRUE - it
//    says wait, and the wait is real - and it is bounded by each action's own
//    constant: 20 s feed, 15 s clean, 25 s play, 30 s med. The 120 s MINIGAME
//    cooldown that used to be the widest of them is GONE (data/balance.h), so
//    the worst a reboot now costs a player who wants to play is nothing at all.
//    RESIDUAL 3: with no snapshot (first boot on this firmware, a wiped unit, a
//    corrupt blob) or with no trustworthy clock at either end of the interval,
//    sim_gain_restore() seeds 0 and the 29-minute behaviour above is exactly
//    what the player gets. That is the safe direction, and it is deliberate:
//    any non-zero CONSTANT seed would be farmable again.
//
//    RESIDUAL 4, NEW IN P2-C10: the v1 fields SaveSchema v2 does not carry -
//    care quality, poop, the wish, snacks, the running happiness mean, the
//    per-stage sickness count and ST_BOND - do not survive a power cut or a
//    Box swap any more; sim_bind() re-derives them from the neutral middle of
//    their range. The companion "lgpet" blob that carried them through P2-C9 is
//    gone with this commit. P3-C1 retires the mechanics themselves.
// =============================================================================
struct CareCtx {
  // --- the bound Bug and the published view ------------------------------
  BugInstance* pb;
  SimView         view;
  SimEnv          env;
  uint32_t        scale;            // god-mode time scale
  uint32_t        events;

  // --- clocks (device-wide) -------------------------------------------------
  uint32_t now;                     // sim's epoch cursor (offline-aware)
  uint32_t sod;                     // local seconds-of-day, 0..86399
  uint32_t uptime_s;                // monotonic simulated seconds
  uint8_t  offline;                 // 1 while the catch-up loop runs

  // --- PER-BUG: cadence accumulators -------------------------------------
  uint32_t acc_stage;
  uint32_t acc_cq;
  uint32_t acc_sick;
  uint32_t acc_minute;
  uint32_t poop_timer;
  // Sub-second remainder of poop_timer, in thousandths of a second. The
  // advance is fractional while the bug sleeps (x0.35), so it is carried
  // rather than truncated - see poop_step().
  uint16_t poop_rem;
  int32_t  hapavg_rem;
  // How long at least one CORE stat has been pinned at zero. Health bleeds
  // only past CARE_ZERO_GRACE_S of this (spec section 27); it is RAM-only, so
  // a reboot forgives the dwell, which is the player-favouring direction.
  uint32_t zero_dwell_s;

  // --- PER-BUG: alerts ---------------------------------------------------
  uint8_t  alert;
  uint32_t alert_age_s;

  // --- PER-BUG: cooldowns and the play windows ---------------------------
  uint32_t act_last[ACT_COUNT];
  uint8_t  act_seen[ACT_COUNT];
  uint32_t last_any_act_s;
  uint8_t  any_act_seen;
  uint32_t play_at[PLAY_DECAY_STEPS];
  uint8_t  play_n;
  uint32_t pet_at[PET_DECAY_STEPS];
  uint8_t  pet_n;

  // --- PER-BUG: the sleep window (P3-C2b) --------------------------------
  // hold_s is when the player was last busy with this creature: bedtime waits
  // SLEEP_RELAPSE_S past it, and so does the relapse after a nudge woke the
  // pet in the middle of the night. hold_seen is 0 until the player does
  // ANYTHING, so a boot at 03:00 shows a sleeping bug at once instead of
  // holding it awake for five minutes.
  uint32_t hold_s;
  uint8_t  hold_seen;
  // The nudge counter: nudge_n gestures refused since nudge_t0_s.
  uint32_t nudge_t0_s;
  uint8_t  nudge_n;

  // --- PER-BUG: the v1 mechanics SaveSchema v2 does not carry ------------
  int32_t  bond;                    // ST_BOND, milli-points
  int16_t  bond_rem;
  uint16_t sick_episodes;
  uint16_t snacks_total;
  uint16_t wish_left_s;
  uint8_t  wish_id;
  uint8_t  wish_pets;
  uint8_t  events_done;
  uint8_t  happiness_avg;
  uint32_t last_interact_epoch;
  uint32_t suppress_until_s;        // 5th poop suppressed -> +12 %/h
  uint16_t cq_good_today;
  uint16_t cq_game_today;
  uint16_t day_stamp;

  // --- DEVICE-WIDE: the hourly anti-farm ledger. sim_switch() KEEPS this. ---
  int32_t  gain_budget[ST_COUNT];
  int32_t  gain_rem[ST_COUNT];
};

static CareCtx g;

// ST_BOND has no home in SaveSchema v2, so it is the one stat that lives in
// CareCtx; every other StatId is one of the five care[] entries under its
// CareId name. This table IS the plan P2-C10 field map, in one place.
static const uint8_t CARE_OF_STAT[ST_COUNT] = {
  (uint8_t)CARE_HUNGER,      // ST_HUNGER
  (uint8_t)CARE_HAPPINESS,   // ST_HAPPINESS
  (uint8_t)CARE_ENERGY,      // ST_ENERGY
  (uint8_t)CARE_CLEANLINESS, // ST_HYGIENE
  (uint8_t)CARE_HEALTH,      // ST_HEALTH
  0xFFu                      // ST_BOND: RAM only
};

// Defined with the rest of the life cycle in section 9; the sub-step needs it.
static void stage_commit(void);

// BugInstance is packed (seq lands on an odd 4-byte boundary), so a member
// of care[] cannot be bound to a reference. Read and write it through these
// four instead; everything else in the file is unchanged by that.
static inline int32_t stat_v(uint8_t id)
{
  const uint8_t c = CARE_OF_STAT[id];
  return (c == 0xFFu) ? g.bond : g.pb->care[c];
}

static inline void stat_set(uint8_t id, int32_t v)
{
  const uint8_t c = CARE_OF_STAT[id];
  if (c == 0xFFu) g.bond = v;
  else            g.pb->care[c] = v;
}

static inline int16_t stat_r(uint8_t id)
{
  const uint8_t c = CARE_OF_STAT[id];
  return (c == 0xFFu) ? g.bond_rem : g.pb->care_rem[c];
}

static inline void stat_rem_set(uint8_t id, int16_t v)
{
  const uint8_t c = CARE_OF_STAT[id];
  if (c == 0xFFu) g.bond_rem = v;
  else            g.pb->care_rem[c] = v;
}

// The legacy PF_* word is the working copy; these are the four bits that also
// have to reach flash. Called at the end of every public mutator, so no path
// can leave BugInstance.status disagreeing with the view.
static void status_sync(void)
{
  if (!g.pb) return;
  // PBS_RESERVED_LIGHT (bit 0x10) is deliberately absent from both the clear
  // mask and the set: P3-C2b retired it, so whatever an older save carries
  // there is left exactly as it was and nothing new is ever written into it.
  uint8_t st = (uint8_t)(g.pb->status & (uint8_t)~(PBS_SICK | PBS_ASLEEP));
  if (g.view.flags & PF_SICK)     st |= (uint8_t)PBS_SICK;
  if (g.view.flags & PF_ASLEEP)   st |= (uint8_t)PBS_ASLEEP;
  g.pb->status = st;
  if (g.view.flags & PF_GOD_TAINTED) g.pb->flags |= (uint8_t)PBF_GOD_TAINTED;
}

// =============================================================================
// 2. SMALL INTEGER PRIMITIVES
// =============================================================================
// Saturating u32 accumulate (plan section 1.7: "all u32 accumulators
// saturate"). age_s is fed by a catch-up that can advance 400 days in one call
// and by a god-mode time scale of up to x720, so the ceiling is reachable by
// arithmetic even though no pet lives 136 years; wrapping it would rewind the
// pet to an egg. Covered by tests/test_overflow.cpp.
static inline uint32_t sat_add_u32(uint32_t a, uint32_t b)
{
  return (a > 0xFFFFFFFFu - b) ? 0xFFFFFFFFu : (uint32_t)(a + b);
}

// Every draw of the simulation comes from the RNG_CARE stream (rng.h).
static inline uint32_t rnd(void)
{
  return rng_u32(RNG_CARE);
}

// Modulo on purpose: the legacy sim reduced its draws this way, and the care
// golden (tests/golden/care_v2.txt) pins that exact sequence of outcomes.
static inline uint32_t rnd_below(uint32_t n)
{
  return (n == 0u) ? 0u : (rnd() % n);
}

// Chains x1000 multipliers over a milli-points/hour base. The work is done in
// micro-points/hour so a deep chain does not bleed precision, then rounded once.
static int32_t rate_chain(int32_t base_mph, const uint16_t* m, uint8_t n)
{
  int64_t v = (int64_t)base_mph * 1000;          // -> micro-points per hour
  for (uint8_t i = 0; i < n; ++i) {
    v = (v * (int64_t)m[i]) / 1000;
  }
  v += (v >= 0) ? 500 : -500;                    // round half away from zero
  return (int32_t)(v / 1000);
}

// The one and only integrator. Exact at any dt: the remainder is carried.
static void accum(int32_t& v, int32_t& rem, int32_t rate_per_hour,
                  uint32_t dt_s, int32_t lo, int32_t hi)
{
  int64_t num = (int64_t)rate_per_hour * (int64_t)dt_s + (int64_t)rem;
  int32_t q   = (int32_t)(num / SEC_PER_HOUR);
  rem         = (int32_t)(num % SEC_PER_HOUR);
  v += q;
  if (v < lo) v = lo;
  else if (v > hi) v = hi;
}

// Same, for a stat whose remainder lives in care_rem[] (int16, |r| < 3600)
static void accum_stat(uint8_t id, int32_t rate_mph, uint32_t dt_s)
{
  int32_t v   = stat_v(id);
  int32_t rem = (int32_t)stat_r(id);
  accum(v, rem, rate_mph, dt_s, STAT_MILLI_MIN, STAT_MILLI_MAX);
  stat_set(id, v);
  stat_rem_set(id, (int16_t)rem);
}

static inline int32_t pct_milli(uint8_t id) { return stat_v(id) / 1000; }

static void cq_add(int32_t d)
{
  int32_t v = (int32_t)g.view.cq + d;
  g.view.cq = (int16_t)NT_CLAMP(v, (int32_t)CQ_MIN, (int32_t)CQ_MAX);
}

static uint16_t today_stamp(void)
{
  if (g.env.clock_valid) return g.env.day_of_year;
  return (uint16_t)((g.now / 86400u) % 366u);
}

// =============================================================================
// 3. PAYOUT TABLES
//    There is no stage multiplier table any more (P3-C1): decay depends on the
//    species and the genome, never on the creature's age.
//
//    CARE_DECAY_MPH is indexed by CareId, so pin that order here - balance.h is
//    a plain data header and cannot see the enum.
// =============================================================================
static_assert((int)CARE_HUNGER      == 0 &&
              (int)CARE_HAPPINESS   == 1 &&
              (int)CARE_HEALTH      == 2 &&
              (int)CARE_CLEANLINESS == 3 &&
              (int)CARE_ENERGY      == 4 &&
              (int)CARE_COUNT       == ER_BALANCE_CARE_COUNT,
              "CARE_DECAY_MPH is indexed by CareId and the order moved");

static const uint16_t PLAY_DECAY[PLAY_DECAY_STEPS] = {
  PLAY_DECAY_0, PLAY_DECAY_1, PLAY_DECAY_2, PLAY_DECAY_3,
  PLAY_DECAY_4, PLAY_DECAY_5, PLAY_DECAY_6, PLAY_DECAY_7
};
static_assert(NT_ARRAY_LEN(PLAY_DECAY) == (size_t)PLAY_DECAY_STEPS,
              "a PLAY_DECAY_n was added to balance.h and not to this table - the "
              "curve would then stop one step early and the floor would move");

static const uint8_t PET_DECAY[PET_DECAY_STEPS] = { 4, 3, 2, 1, 0 };

// =============================================================================
// 4. SHARED COOLDOWN / HOURLY-GAIN LEDGER
// =============================================================================
static int32_t gain_cap_milli(uint8_t id)
{
  switch (id) {
    case ST_HUNGER:    return (int32_t)GAIN_CAP_HUNGER_H    * 1000;
    case ST_HAPPINESS: return (int32_t)GAIN_CAP_HAPPINESS_H * 1000;
    case ST_ENERGY:    return (int32_t)GAIN_CAP_ENERGY_H    * 1000;
    case ST_HYGIENE:   return (int32_t)GAIN_CAP_HYGIENE_H   * 1000;
    default:           return -1;                       // unmetered
  }
}

static void gain_refill(uint32_t dt_s)
{
  for (uint8_t i = 0; i < ST_COUNT; ++i) {
    int32_t cap = gain_cap_milli(i);
    if (cap < 0) continue;
    accum(g.gain_budget[i], g.gain_rem[i], cap, dt_s, 0, cap);
  }
}

// Adds whole points to a stat. Positive gains are metered by the hourly budget;
// budget consumed but wasted on the 100.000 ceiling is refunded.
// Returns the whole points actually applied.
static int16_t stat_add(uint8_t id, int32_t points)
{
  int32_t milli = points * 1000;
  int32_t taken = 0;
  int32_t cap   = gain_cap_milli(id);

  if (milli > 0 && cap >= 0) {
    if (milli > g.gain_budget[id]) milli = g.gain_budget[id];
    taken = milli;
    g.gain_budget[id] -= milli;
  }

  int32_t before = stat_v(id);
  int32_t v = before + milli;
  v = NT_CLAMP(v, STAT_MILLI_MIN, STAT_MILLI_MAX);
  stat_set(id, v);

  int32_t applied = v - before;
  // Refund only what a metered GAIN failed to deliver against the 100.000
  // ceiling. A penalty never touches the budget (taken == 0 there).
  if (taken > 0 && taken > applied) {
    g.gain_budget[id] += (taken - applied);
    if (g.gain_budget[id] > cap) g.gain_budget[id] = cap;
  }
  return (int16_t)(applied / 1000);
}

static uint16_t cd_for(uint8_t action)
{
  switch (action) {
    case ACT_FEED_MEAL:
    case ACT_FEED_SNACK:   return ACT_CD_FEED_S;
    case ACT_CLEAN:        return ACT_CD_CLEAN_S;
    case ACT_MEDICINE:     return ACT_CD_MED_S;
    case ACT_PLAY:         return ACT_CD_PLAY_S;
    case ACT_SLEEP_TOGGLE: return ACT_CD_SLEEP_S;
    default:               return 0;
  }
}

uint16_t sim_action_cooldown_s(ActionId action)
{
  if (action >= ACT_COUNT) return 0;

  uint32_t left = 0;
  if (g.act_seen[action]) {
    uint32_t cd   = cd_for((uint8_t)action);
    uint32_t gone = g.uptime_s - g.act_last[action];
    if (gone < cd) left = cd - gone;
  }
  if (g.any_act_seen) {
    uint32_t gone = g.uptime_s - g.last_any_act_s;
    if (gone < (uint32_t)ACT_CD_GLOBAL_S) {
      uint32_t g = (uint32_t)ACT_CD_GLOBAL_S - gone;
      if (g > left) left = g;
    }
  }
  return (left > 65535u) ? 65535u : (uint16_t)left;
}

uint16_t sim_gain_left(StatId id)
{
  if (id >= ST_COUNT) return 0;
  if (gain_cap_milli((uint8_t)id) < 0) return 0xFFFFu;
  return (uint16_t)(g.gain_budget[id] / 1000);
}

// --- ledger persistence seam (sim owns the numbers, storage owns the bytes) ---
void sim_gain_snapshot(uint8_t out_pts[ST_COUNT])
{
  if (!out_pts) return;
  for (uint8_t i = 0; i < ST_COUNT; ++i) {
    int32_t cap = gain_cap_milli(i);
    if (cap < 0) { out_pts[i] = 0; continue; }      // unmetered: nothing to keep
    // Truncation toward zero is deliberate: the dropped fraction is at most
    // 0.999 pt and always in the player's disfavour, so a round trip can never
    // manufacture budget out of rounding.
    int32_t p = g.gain_budget[i] / 1000;
    if (p < 0)   p = 0;
    if (p > 255) p = 255;                            // caps are static_assert-ed < 256
    out_pts[i] = (uint8_t)p;
  }
}

uint8_t sim_gain_restore(const uint8_t* pts, uint8_t n,
                         uint32_t saved_epoch, uint32_t now_epoch)
{
  // THE ELAPSED TERM IS GONE (P7-C6). It read `saved_epoch` and `now_epoch`,
  // BOTH of which are wall clocks a player types on the time screen, and one
  // hour of "elapsed" refilled the whole cap - so (clock +1 h, reboot) x100
  // from a fully SPENT ledger manufactured 4,000 happiness gain points against
  // a cap of 40 an hour, in zero real seconds. This is the same hole
  // xp_ledger_restore() lost at P6-C4 and it is closed the same way: the
  // restore hands back exactly the bytes of the last snapshot, and every point
  // past that has to be refilled by gain_refill() out of seconds this device
  // watched pass (boot_absence()'s catch-up is where they come from).
  //
  // THE HONEST COST, WHICH IS REAL AND IS PAID BY EVERY HONEST PLAYER: off-time
  // no longer refills the hourly budget. A player who leaves the device off for
  // an hour comes back to whatever the ledger held when it was switched off,
  // and cannot feed a full meal for up to 29 minutes - which is the exact
  // complaint app/app.cpp says this restore was added to fix. It is paid
  // knowingly: an unkind hour is recoverable and a farmable stat budget is not.
  //
  // Both epochs still have to be real dates. They no longer buy anything, but
  // they are what says the blob came from a device that knew what time it was,
  // and without that the safe seed is ZERO rather than the snapshot.
  const uint8_t trust = (pts != 0 && n != 0 &&
                         saved_epoch >= (uint32_t)NT_EPOCH_SANE_MIN &&
                         now_epoch   >= (uint32_t)NT_EPOCH_SANE_MIN) ? 1u : 0u;

  for (uint8_t i = 0; i < ST_COUNT; ++i) {
    int32_t cap = gain_cap_milli(i);
    if (cap < 0) { g.gain_budget[i] = 0; g.gain_rem[i] = 0; continue; }

    int32_t v = 0;
    if (trust) {
      v = (i < n) ? ((int32_t)pts[i] * 1000) : 0;
      if (v > cap) v = cap;                 // a foreign/edited blob cannot exceed the cap
      if (v < 0)   v = 0;
    }
    g.gain_budget[i] = v;
    g.gain_rem[i]    = 0;
  }
  return trust;
}

uint8_t sim_play_window_count(void)
{
  uint8_t n = 0;
  for (uint8_t i = 0; i < g.play_n; ++i) {
    if (g.uptime_s - g.play_at[i] < PLAY_DECAY_WINDOW_S) n++;
  }
  return n;
}

uint16_t sim_play_decay_permille(void)
{
  uint8_t n = sim_play_window_count();
  if (n >= PLAY_DECAY_STEPS) n = PLAY_DECAY_STEPS - 1;
  return PLAY_DECAY[n];
}

static void play_window_push(void)
{
  // compact out anything older than the window, then append
  uint8_t w = 0;
  for (uint8_t i = 0; i < g.play_n; ++i) {
    if (g.uptime_s - g.play_at[i] < PLAY_DECAY_WINDOW_S) g.play_at[w++] = g.play_at[i];
  }
  if (w >= PLAY_DECAY_STEPS) {
    for (uint8_t i = 1; i < w; ++i) g.play_at[i - 1] = g.play_at[i];
    w = PLAY_DECAY_STEPS - 1;
  }
  g.play_at[w++] = g.uptime_s;
  g.play_n = w;
}

static uint8_t pet_window_bonus(void)
{
  uint8_t n = 0;
  for (uint8_t i = 0; i < g.pet_n; ++i) {
    if (g.uptime_s - g.pet_at[i] < 3600u) n++;
  }
  if (n >= PET_DECAY_STEPS) return PET_DECAY[PET_DECAY_STEPS - 1];
  return PET_DECAY[n];
}

static void pet_window_push(void)
{
  uint8_t w = 0;
  for (uint8_t i = 0; i < g.pet_n; ++i) {
    if (g.uptime_s - g.pet_at[i] < 3600u) g.pet_at[w++] = g.pet_at[i];
  }
  if (w >= PET_DECAY_STEPS) {
    for (uint8_t i = 1; i < w; ++i) g.pet_at[i - 1] = g.pet_at[i];
    w = PET_DECAY_STEPS - 1;
  }
  g.pet_at[w++] = g.uptime_s;
  g.pet_n = w;
}

// =============================================================================
// 5. ENVIRONMENT
// =============================================================================
void sim_env_defaults(SimEnv& env)
{
  memset(&env, 0, sizeof(env));
  env.clock_valid = 0;
}

void sim_set_env(const SimEnv& env)
{
  g.env = env;
  if (!g.offline) {
    if (g.env.now_epoch != 0) g.now = g.env.now_epoch;
    g.sod = ((uint32_t)g.env.local_hour * 3600u) + ((uint32_t)g.env.local_min * 60u);
  }
}

const SimEnv& sim_env(void) { return g.env; }

void sim_seed(uint32_t seed) { rng_seed(RNG_CARE, seed); }

void sim_set_time_scale(uint32_t scale)
{
  g.scale = (scale == 0u) ? 1u : scale;
}

uint32_t sim_step_seconds(void){ return g.scale; }

// =============================================================================
// 6. STAGE HELPERS
// =============================================================================
static uint32_t stage_enter_s(uint8_t st)
{
  switch (st) {
    case STAGE_BABY:   return AGE_BABY_S;
    case STAGE_CHILD:  return AGE_CHILD_S;
    case STAGE_TEEN:   return AGE_TEEN_S;
    case STAGE_ADULT:  return AGE_ADULT_S;
    case STAGE_SENIOR: return AGE_SENIOR_S;
    default:           return 0;
  }
}

static void set_minor_form(uint8_t stage)
{
  uint8_t variant = (g.view.cq >= 550) ? 0u : 1u;   // "bueno" / "descuidado"
  if (stage == STAGE_CHILD) {
    g.view.minor_form = (uint8_t)((g.view.minor_form & 0xF0u) | variant);
  } else if (stage == STAGE_TEEN) {
    g.view.minor_form = (uint8_t)((g.view.minor_form & 0x0Fu) | (uint8_t)(variant << 4));
  }
  g.events |= SIM_EV_EVOLVE_MINOR;
}

// =============================================================================
// 7. SUB-STEP - one SIM_SUBSTEP_S (or shorter) slice of simulation
// =============================================================================
static uint8_t compute_alert(void)
{
  if (stat_v(ST_HEALTH) < (int32_t)SIM_ALERT_HEALTH_PCT * 1000) return AL_LOW_HEALTH;
  if (g.view.flags & PF_SICK)                                        return AL_SICK;
  if (stat_v(ST_HUNGER)    < (int32_t)ALERT_LOW_STAT_PCT * 1000) return AL_HUNGRY;
  if (stat_v(ST_HYGIENE)   < (int32_t)ALERT_LOW_STAT_PCT * 1000) return AL_DIRTY;
  if (stat_v(ST_ENERGY)    < (int32_t)ALERT_LOW_STAT_PCT * 1000) return AL_TIRED;
  if (stat_v(ST_HAPPINESS) < (int32_t)ALERT_LOW_STAT_PCT * 1000) return AL_SAD;
  if (g.view.poop_count >= 3)                                          return AL_POOP;
  if (g.view.flags & PF_WISH_ACTIVE)                                   return AL_WISH;
  return AL_NONE;
}

// Is the sun down where the player probably is? P3-C2b replaced the fixed
// SLEEP_HOUR_START/END pair with the interpolated daylight table of
// data/balance.h: bedtime is sunset + SLEEP_AFTER_DUSK_MIN, morning is
// sunrise, both moving with the day of the year. With no trustworthy clock
// there is no window at all, exactly as before.
static uint8_t is_night(void)
{
  if (!g.env.clock_valid) return 0;
  return daylight_is_night(g.env.day_of_year, (uint16_t)(g.sod / 60u));
}

// The player has been quiet long enough for the creature to (go back to)
// sleep. Before the FIRST interaction there is nothing to wait for, so a boot
// inside the night window puts the bug straight to bed.
static uint8_t settled_enough(void)
{
  if (!g.hold_seen) return 1;
  const uint32_t gone = (g.uptime_s > g.hold_s) ? (g.uptime_s - g.hold_s) : 0u;
  return (gone >= (uint32_t)SLEEP_RELAPSE_S) ? 1u : 0u;
}

static void sleep_machine(void)
{
  const uint8_t asleep = (g.view.flags & PF_ASLEEP) ? 1u : 0u;
  const uint8_t night  = is_night();

  if (!asleep) {
    // Bedtime, and the relapse after a nudge woke it, are the SAME rule: it is
    // dark and nobody has touched the device for SLEEP_RELAPSE_S. Energy
    // hitting zero is a COLLAPSE, not sleep: GAME_DESIGN 1.5 keeps the energy
    // damage running for as long as energy stays at zero, which a regenerating
    // sleep would cancel. Collapse is a render state, not a stat state.
    if (night && settled_enough()) {
      g.view.flags |= PF_ASLEEP;
      g.events |= SIM_EV_SLEEP;
    }
  } else {
    // NOTHING WAKES IT WHILE THE WINDOW IS OPEN except the player (the nudge
    // counter in sim_apply_action, or the explicit ACT_SLEEP_TOGGLE). The old
    // "wake as soon as energy is full" rule predates a real window: energy
    // refills in 5 h and a winter night is 13 h long, so it woke the creature
    // at 00:30 and the bedtime rule immediately put it back - a SLEEP/WAKE
    // event pair every substep until dawn. By day the rule stands: a nap ends
    // once the battery is back up.
    if (!night && pct_milli(ST_ENERGY) >= SIM_WAKE_DAY_ENERGY_PCT) {
      g.view.flags &= (uint16_t)~PF_ASLEEP;
      g.events |= SIM_EV_WAKE;
    }
  }
}

static void decay_stats(uint32_t dt)
{
  const Genome& gen = g.pb->genome;

  uint16_t m_app     = gene_appetite_mult(gen);
  uint16_t m_met     = gene_metabolism_mult(gen);
  uint16_t m_soc     = gene_sociability_mult(gen);
  uint16_t m_sleep   = (g.view.flags & PF_ASLEEP) ? (uint16_t)MULT_SLEEP : (uint16_t)MULT_ONE;
  uint16_t m_off     = g.offline ? (uint16_t)MULT_OFFLINE_DECAY : (uint16_t)MULT_ONE;
  uint16_t m_lonely  = (uint16_t)MULT_ONE;
  // PH3 #3: last_interact_epoch can legitimately sit AHEAD of g.now - the
  // catch-up loop winds g.now back to the start of the absence, and a
  // clock-less reboot restarts the estimated clock near 0 while the loaded
  // save still carries the previous boot's uptime-valued timestamps. An
  // unsigned subtraction there yields ~4.29e9, which is >= LONELY_AFTER_S,
  // so the x1.5 loneliness penalty would be charged for the whole catch-up.
  // Clamp the idle time at 0 instead (every other epoch delta in the tree
  // already does this).
  const uint32_t idle_s = (g.now > g.last_interact_epoch)
                            ? (g.now - g.last_interact_epoch) : 0u;
  if (idle_s >= LONELY_AFTER_S) m_lonely = (uint16_t)MULT_LONELY;

  // --- hunger (satiety) ---
  {
    const uint16_t m[3] = { m_app, m_sleep, m_off };
    accum_stat(ST_HUNGER, rate_chain(CARE_DECAY_MPH[CARE_HUNGER], m, 3), dt);
  }
  // --- happiness ---
  {
    const uint16_t m[4] = { m_lonely, m_soc, m_sleep, m_off };
    accum_stat(ST_HAPPINESS, rate_chain(CARE_DECAY_MPH[CARE_HAPPINESS], m, 4), dt);
  }
  // --- energy ---
  if (g.view.flags & PF_ASLEEP) {
    const uint16_t m[1] = { m_off };
    accum_stat(ST_ENERGY, rate_chain(CARE_ENERGY_ASLEEP_MPH, m, 1), dt);
  } else {
    const uint16_t m[2] = { m_met, m_off };
    accum_stat(ST_ENERGY, rate_chain(CARE_DECAY_MPH[CARE_ENERGY], m, 2), dt);
  }
  // --- hygiene (base + per-poop) ---
  {
    int32_t base = CARE_DECAY_MPH[CARE_CLEANLINESS]
                 + (int32_t)g.view.poop_count * CARE_HYGIENE_POOP_MPH;
    const uint16_t m[2] = { m_sleep, m_off };
    accum_stat(ST_HYGIENE, rate_chain(base, m, 2), dt);
  }
  // --- bond (flat) ---
  {
    const uint16_t m[1] = { m_off };
    accum_stat(ST_BOND, rate_chain(CARE_BOND_MPH, m, 1), dt);
  }
}

// HEALTH is the one stat with a floor. It bleeds only while a CORE stat has
// been pinned at zero for CARE_ZERO_GRACE_S - illness merely blocks
// regeneration - and never falls below HEALTH_FLOOR_PCT, so total neglect ends
// in a miserable bug and never in a dead one (spec section 27,
// "inconveniently unhappy at worst").
//
// P3-C1 replaced the four per-stat damage rates with ONE rate behind a two-hour
// grace: a player who lets a bar touch bottom on the way home has done no
// damage at all, and a player who never comes back still needs 45 h of bleeding
// to reach a floor the model then refuses to cross.
static void health_step(uint32_t dt)
{
  uint16_t m_hardy = gene_hardiness_mult(g.pb->genome);
  uint16_t m_off   = g.offline ? (uint16_t)MULT_OFFLINE_DECAY : (uint16_t)MULT_ONE;

  uint8_t any_zero = 0;
  for (uint8_t i = 0; i < ST_CORE_COUNT; ++i) {
    if (stat_v(i) <= 0) { any_zero = 1; break; }
  }
  g.zero_dwell_s = any_zero ? sat_add_u32(g.zero_dwell_s, dt) : 0u;

  int32_t total = 0;
  if (any_zero && g.zero_dwell_s >= (uint32_t)CARE_ZERO_GRACE_S) {
    const uint16_t md[2] = { m_hardy, m_off };
    total = rate_chain(CARE_HEALTH_BLEED_MPH, md, 2);
  }

  // ---- regeneration -------------------------------------------------------
  int32_t regen = 0;
  if (total == 0 && !(g.view.flags & PF_SICK)) {
    uint8_t ok = 1;
    for (uint8_t i = 0; i < ST_CORE_COUNT; ++i) {
      if (pct_milli(i) < HEALTH_REGEN_MIN_PCT) { ok = 0; break; }
    }
    if (ok) {
      uint16_t m_sen = (g.view.stage == STAGE_SENIOR)
                         ? (uint16_t)SENIOR_REGEN_MULT : (uint16_t)MULT_ONE;
      const uint16_t mr[2] = { m_sen, m_off };
      regen = rate_chain(CARE_HEALTH_REGEN_MPH, mr, 2);
    }
  }

  accum_stat(ST_HEALTH, regen - total, dt);

  // ---- the floor ----------------------------------------------------------
  const int32_t floor_milli = (int32_t)HEALTH_FLOOR_PCT * 1000;
  if (stat_v(ST_HEALTH) < floor_milli) {
    stat_set(ST_HEALTH, floor_milli);
    stat_rem_set(ST_HEALTH, 0);
  }

  // ---- senior health ceiling ---------------------------------------------
  if (g.view.stage == STAGE_SENIOR) {
    int32_t age_h = (int32_t)(g.pb->age_s / 3600u);
    int32_t maxh  = 100 - ((age_h - 168) / 4);
    maxh = NT_CLAMP(maxh, (int32_t)SENIOR_MAXHEALTH_MIN, (int32_t)100);
    if (stat_v(ST_HEALTH) > maxh * 1000) {
      stat_set(ST_HEALTH, maxh * 1000);
      stat_rem_set(ST_HEALTH, 0);
    }
  }
}

static void poop_step(uint32_t dt)
{
  uint32_t base_min = g.offline ? (uint32_t)POOP_OFFLINE_MIN : (uint32_t)POOP_FIRST_MIN;
  uint32_t met = gene_metabolism_mult(g.pb->genome);
  if (met == 0) met = MULT_ONE;
  uint32_t period = (base_min * 60u * 1000u) / met;      // seconds
  if (period < 60u) period = 60u;

  // A sleeping pet digests at the same x0.35 the rest of its hygiene budget
  // runs at. Without this, an 8 h night produces 5 poops and GAME_DESIGN 1.5's
  // "overnight is free, sleeping the pet before bed is a real strategy" is
  // simply false: the hygiene collapse alone would cost ~7 h of decay.
  //
  // That x0.35 is FRACTIONAL, so it carries its remainder exactly the way
  // accum() does. Truncating it per sub-step was not a rounding error, it was
  // a hole: the live device ticks the sim one second at a time
  // (app.cpp -> sim_step_seconds() -> 1), and (1 * 350) / 1000 is 0 every
  // time, so a sleeping bug never advanced its timer at all and could not
  // poop overnight, ever - while the offline catch-up over the same night
  // (60 s sub-steps, where 60 * 350 / 1000 is an exact 21) produced two.
  // Same night, same bug, two different models. dt is at most
  // SIM_SUBSTEP_S, so dt * 1000 + 999 cannot overflow.
  const uint32_t mult  = (g.view.flags & PF_ASLEEP) ? (uint32_t)MULT_SLEEP
                                                    : (uint32_t)MULT_ONE;
  const uint32_t milli = dt * mult + (uint32_t)g.poop_rem;
  const uint32_t adv   = milli / 1000u;
  g.poop_rem           = (uint16_t)(milli % 1000u);

  g.poop_timer += adv;
  while (g.poop_timer >= period) {
    g.poop_timer -= period;
    if (g.view.poop_count < POOP_MAX) {
      g.view.poop_count++;
      g.events |= SIM_EV_POOP;
    } else {
      // A 5th poop with nowhere to go raises the sickness risk instead.
      g.suppress_until_s = g.uptime_s + SICK_SUPPRESS_WINDOW_S;
    }
  }
}

static void sickness_step(uint32_t dt)
{
  g.acc_sick += dt;
  while (g.acc_sick >= (uint32_t)SICK_ROLL_PERIOD_S) {
    g.acc_sick -= (uint32_t)SICK_ROLL_PERIOD_S;

    if (g.view.flags & PF_SICK) continue;

    int32_t pph = SICK_BASE_PPH
                + (int32_t)g.view.poop_count * SICK_PER_POOP_PPH
                + ((pct_milli(ST_HUNGER) < 15) ? SICK_HUNGRY_PPH : 0)
                + ((pct_milli(ST_HEALTH) < 50) ? SICK_LOWHEALTH_PPH : 0);
    if (g.uptime_s < g.suppress_until_s) pph += POOP_SUPPRESSED_SICK_PCT * 10;

    uint16_t m_inb = (g.view.flags & PF_INBRED) ? (uint16_t)INBRED_SICK_MULT : (uint16_t)MULT_ONE;
    const uint16_t ms[2] = { gene_hardiness_mult(g.pb->genome), m_inb };
    pph = rate_chain(pph, ms, 2);
    if (pph < 0) pph = 0;

    // p_hour / 6 evaluated every 10 minutes, in permille.
    if ((int32_t)rnd_below(6000u) < pph) {
      g.view.flags |= PF_SICK;
      if (g.sick_episodes < 0xFFFFu) g.sick_episodes++;
      cq_add(CQ_D_SICK_EPISODE);
      g.events |= SIM_EV_SICK_START;
    }
  }
}

static void cq_step(uint32_t dt)
{
  g.acc_cq += dt;
  while (g.acc_cq >= (uint32_t)CQ_PERIOD_S) {
    g.acc_cq -= (uint32_t)CQ_PERIOD_S;

    uint8_t all_good = 1, any_zero = 0;
    for (uint8_t i = 0; i < ST_CORE_COUNT; ++i) {
      if (pct_milli(i) < CQ_GOOD_STATS_PCT) all_good = 0;
      if (stat_v(i) <= 0) any_zero = 1;
    }
    if (all_good && g.cq_good_today < CQ_GOOD_DAILY_CAP) {
      cq_add(CQ_D_GOOD);
      g.cq_good_today++;
    }
    if (any_zero) cq_add(CQ_D_ZEROSTAT);
  }
}

static void wish_step(uint32_t dt)
{
  if (!g.env.clock_valid || g.offline) return;
  if (g.view.stage < STAGE_CHILD) return;

  if (g.view.flags & PF_WISH_ACTIVE) {
    uint32_t left = g.wish_left_s;
    if (dt >= left) {
      g.wish_left_s = 0;
      g.view.flags &= (uint16_t)~PF_WISH_ACTIVE;
      g.view.flags |= PF_WISH_DONE;
      cq_add(CQ_D_WISH_FAIL);
      g.events |= SIM_EV_WISH_FAIL;
    } else {
      g.wish_left_s = (uint16_t)(left - dt);
    }
    return;
  }
  if (g.view.flags & PF_WISH_DONE) return;

  uint32_t hour = g.sod / 3600u;
  uint32_t span = (uint32_t)(WISH_HOUR_MAX - WISH_HOUR_MIN + 1);
  uint32_t seed = g.pb->genome.lineage_id ^ (uint32_t)today_stamp();
  seed ^= seed >> 13; seed *= 0x9E3779B1u; seed ^= seed >> 15;
  uint32_t wish_hour = (uint32_t)WISH_HOUR_MIN + (seed % span);

  if (hour == wish_hour) {
    g.wish_id      = (uint8_t)(WISH_PLAY + (seed >> 8) % (uint32_t)(WISH_COUNT - 1));
    g.wish_left_s  = (uint16_t)NT_MIN((uint32_t)WISH_WINDOW_S, (uint32_t)65535u);
    g.view.flags       |= PF_WISH_ACTIVE;
    g.wish_pets    = 0;
    g.events      |= SIM_EV_WISH_START;
  }
}

static void events_step(void)
{
  uint32_t age_h = g.pb->age_s / 3600u;

  if (!(g.events_done & EV_VISITA) && age_h >= (uint32_t)EVENT_VISITA_H) {
    g.events_done |= EV_VISITA;
    if (gene_sociability(g.pb->genome) >= EVENT_VISITA_SOC_MIN) {
      stat_add(ST_HAPPINESS, EVENT_VISITA_HAPPINESS);
    }
    g.events |= SIM_EV_VISITA;
  }

  uint32_t bdays_due = age_h / (uint32_t)EVENT_BIRTHDAY_H;
  uint32_t bdays_had = (uint32_t)((g.events_done >> EV_BIRTHDAY_SH) & EV_BIRTHDAY_MK);
  if (bdays_due > bdays_had && bdays_had < EV_BIRTHDAY_MK) {
    bdays_had++;
    g.events_done = (uint8_t)((g.events_done & ~(EV_BIRTHDAY_MK << EV_BIRTHDAY_SH))
                              | (uint8_t)(bdays_had << EV_BIRTHDAY_SH));
    stat_add(ST_HAPPINESS, EVENT_BIRTHDAY_HAPPY);
    g.events |= SIM_EV_BIRTHDAY;
  }
}

// One subtraction below is only enough while a sub-step cannot overshoot the
// period by more than the period itself. Both are 60 today and the comment used
// to say so and stop there; pin it, or a sub-step grid coarser than the stage
// cadence would leave acc_stage growing without bound.
static_assert((uint32_t)SIM_SUBSTEP_S <= (uint32_t)STAGE_CHECK_PERIOD_S,
              "stage_step() carries with one subtraction: a sub-step may not "
              "exceed STAGE_CHECK_PERIOD_S");

static void stage_step(uint32_t dt)
{
  g.acc_stage += dt;
  if (g.acc_stage < (uint32_t)STAGE_CHECK_PERIOD_S) return;
  // Carry, do not discard: `= 0` loses whatever dt overshot the period by,
  // which is the same class of leak poop_step() used to have. dt is at most
  // SIM_SUBSTEP_S, which the static_assert above pins at or below the period,
  // so one subtraction is enough.
  g.acc_stage -= (uint32_t)STAGE_CHECK_PERIOD_S;

  while (g.view.stage < STAGE_SENIOR && g.pb->age_s >= stage_enter_s((uint8_t)(g.view.stage + 1))) {
    g.view.stage = (uint8_t)(g.view.stage + 1);
    g.events |= SIM_EV_STAGE_UP;
    if (g.view.stage == STAGE_CHILD || g.view.stage == STAGE_TEEN) {
      set_minor_form(g.view.stage);
    }
    // Per-stage counter snapshot then reset (GAME_DESIGN 2.1).
    g.sick_episodes = 0;
    stage_commit();
  }
}

static void alert_step(uint32_t dt)
{
  uint8_t a = compute_alert();

  if (a == AL_NONE) {
    if (g.alert != AL_NONE) g.events |= SIM_EV_ALERT;
    g.alert = AL_NONE;
    g.alert_age_s = 0;
  } else if (a != g.alert) {
    g.alert = a;
    g.alert_age_s = 0;
    g.events |= SIM_EV_ALERT;
  } else {
    g.alert_age_s += dt;
  }
}

static void hatch_now(void)
{
  g.view.stage        = STAGE_BABY;
  g.pb->age_s        = 0;
  g.pb->birth_epoch  = g.now;
  g.last_interact_epoch = g.now;
  g.view.minor_form   = 0;
  g.view.poop_count   = 0;
  g.poop_timer   = 0;
  g.poop_rem     = 0;
  for (uint8_t i = 0; i < ST_COUNT; ++i) {
    stat_set(i, STAT_MILLI_MAX);
    stat_rem_set(i, 0);
  }
  if (g.view.flags & PF_COLD_EGG) {
    stat_set(ST_HEALTH, (int32_t)EGG_COLD_HEALTH_PCT * 1000);
  }
  if (gene_tainted(g.pb->genome)) g.view.flags |= PF_GOD_TAINTED;
  stage_commit();
  g.events |= SIM_EV_HATCHED | SIM_EV_STAGE_UP;
}

// One slice. dt must be <= SIM_SUBSTEP_S so the cadence grid stays aligned.
static void sub_step(uint32_t dt)
{
  if (dt == 0) return;

  g.uptime_s += dt;
  g.now      += dt;
  g.sod       = (g.sod + dt) % 86400u;
  gain_refill(dt);

  g.pb->last_updated_epoch = g.now;

  // Eggs are immortal and never decay: they only age toward hatching.
  if (g.view.stage == STAGE_EGG) {
    g.pb->age_s = sat_add_u32(g.pb->age_s, dt);
    if (g.pb->age_s >= AGE_EGG_S) hatch_now();
    return;
  }

  g.pb->age_s = sat_add_u32(g.pb->age_s, dt);

  // day rollover: daily caps and the once-a-day wish
  uint16_t d = today_stamp();
  if (d != g.day_stamp) {
    g.day_stamp     = d;
    g.cq_good_today = 0;
    g.cq_game_today = 0;
    g.view.flags        &= (uint16_t)~PF_WISH_DONE;
  }

  sleep_machine();
  decay_stats(dt);
  poop_step(dt);
  sickness_step(dt);
  health_step(dt);
  cq_step(dt);
  wish_step(dt);
  events_step();
  alert_step(dt);

  // running happiness mean, for S_rebel
  g.acc_minute += dt;
  while (g.acc_minute >= 60u) {
    g.acc_minute -= 60u;
    int32_t cur = pct_milli(ST_HAPPINESS);
    int32_t acc = (int32_t)g.happiness_avg * 15 + cur + g.hapavg_rem;
    g.happiness_avg = (uint8_t)NT_CLAMP(acc / 16, (int32_t)0, (int32_t)100);
    g.hapavg_rem = acc - (int32_t)g.happiness_avg * 16;
    if (g.hapavg_rem > 15 || g.hapavg_rem < -15) g.hapavg_rem = 0;
  }

  stage_step(dt);
  status_sync();
}

// =============================================================================
// 8. PUBLIC TICK / QUERIES
// =============================================================================
void sim_tick(uint32_t seconds)
{
  if (!g.pb) return;
  while (seconds > 0) {
    uint32_t dt = (seconds > SIM_SUBSTEP_S) ? SIM_SUBSTEP_S : seconds;
    sub_step(dt);
    seconds -= dt;
  }
}

uint8_t sim_stat_pct(StatId id)
{
  if (!g.pb || id >= ST_COUNT) return 0;
  int32_t v = stat_v(id) / 1000;
  return (uint8_t)NT_CLAMP(v, (int32_t)0, (int32_t)100);
}

int32_t sim_stat_milli(StatId id)
{
  if (!g.pb || id >= ST_COUNT) return 0;
  return stat_v(id);
}

uint8_t sim_mood_score(void)
{
  if (!g.pb) return 0;
  int32_t s = (40 * pct_milli(ST_HAPPINESS)
             + 25 * pct_milli(ST_HEALTH)
             + 20 * pct_milli(ST_BOND)
             + 15 * pct_milli(ST_HUNGER)) / 100;
  return (uint8_t)NT_CLAMP(s, (int32_t)0, (int32_t)100);
}

const SimView* sim_view(void)
{
  // genome, birth_epoch and age_s are mirrors of the bound Bug, refreshed
  // here rather than written twice, so they can never drift out of step.
  if (g.pb) {
    g.view.genome      = g.pb->genome;
    g.view.birth_epoch = g.pb->birth_epoch;
    g.view.age_s       = g.pb->age_s;
  }
  return &g.view;
}

const BugInstance* sim_bug(void) { return g.pb; }

uint32_t sim_take_events(void)       { uint32_t e = g.events; g.events = 0; return e; }
void     sim_post_event(uint32_t m)  { g.events |= m; }
uint8_t  sim_alert(void)             { return g.alert; }
uint8_t  sim_is_asleep(void)         { return (g.pb && (g.view.flags & PF_ASLEEP)) ? 1u : 0u; }
uint8_t  sim_is_sick(void)           { return (g.pb && (g.view.flags & PF_SICK)) ? 1u : 0u; }
uint32_t sim_age_s(void)             { return g.pb ? g.pb->age_s : 0u; }
uint32_t sim_now(void)               { return g.now; }

// =============================================================================
// 9. LIFE CYCLE / BIND / SWITCH
// =============================================================================
// STAGE <-> LEVEL. SaveSchema v2 has thirty levels and no life stages, so the
// stage the v1 rules still run on is persisted THROUGH BugInstance.level,
// using exactly the anchors persistence/migration.cpp already maps a v1 pet
// with (migrate_level_of): a level never drops, so an XP-earned level is never
// undone by a stage that maps lower. P3-C2 makes level XP-driven and this map
// becomes read-only; P3-C1 retires the stage multipliers themselves.
static uint8_t stage_of_level(uint8_t level)
{
  if (level >= 20u) return (uint8_t)STAGE_SENIOR;
  if (level >= 15u) return (uint8_t)STAGE_ADULT;
  if (level >= 10u) return (uint8_t)STAGE_TEEN;
  if (level >=  5u) return (uint8_t)STAGE_CHILD;
  return (uint8_t)STAGE_BABY;
}

static uint8_t level_of_stage(uint8_t stage)
{
  switch (stage) {
    case STAGE_CHILD:  return 5u;
    case STAGE_TEEN:   return 10u;
    case STAGE_ADULT:  return 15u;
    case STAGE_SENIOR: return 20u;
    default:           return 1u;      // EGG and BABY are both level 1
  }
}

// Writes the stage back into the only field that survives a power cut.
static void stage_commit(void)
{
  if (!g.pb) return;
  const uint8_t want = level_of_stage(g.view.stage);
  if (g.pb->level < want) g.pb->level = want;
  if (g.pb->level == 0u)  g.pb->level = 1u;
}

// PH3 #4. `fresh` distinguishes the two callers:
//   1 - sim_new_pet(): a creature that has never been interacted with. Every
//       ledger starts empty, i.e. the full hourly budget and no cooldowns.
//   0 - sim_bind() / sim_switch(): a Bug just came back from NVS or out of
//       the Box. We have no record of how much of the budget the previous boot
//       had already spent, so we must assume the worst - budget exhausted,
//       every cooldown just started - and let the boot's catch-up refill it
//       from the real elapsed time (gain_refill() runs inside sub_step(), and
//       g.uptime_s advances there too). Refilling to `cap` here instead is what
//       made a reboot restore the whole hourly ceiling for free.
//       PH4 6.1: the caller is expected to follow sim_bind() with
//       sim_gain_restore(), which replaces this zero with the budget that was
//       actually left at the last save. The zero remains the fallback whenever
//       there is no trustworthy snapshot, so this function stays safe on its
//       own and nothing downstream depends on the restore having happened.

// --- PER-BUG. sim_switch() calls this and nothing else. -------------------
static void reset_bug_state(uint8_t fresh)
{
  g.events = 0;
  g.acc_stage = g.acc_cq = g.acc_sick = g.acc_minute = 0;
  g.poop_timer = 0;
  g.poop_rem   = 0;
  g.hapavg_rem = 0;
  g.zero_dwell_s = 0;
  g.alert = AL_NONE;
  g.alert_age_s = 0;
  // Cooldowns: "seen right now". On a reload or a Box swap that means the
  // residual cooldown is (cd - elapsed), clamped at 0 by
  // sim_action_cooldown_s(), so neither a reboot nor a swap buys anything and
  // both cost at most ACT_CD_MED_S (30 s) of friction.
  for (uint8_t i = 0; i < (uint8_t)ACT_COUNT; ++i) g.act_last[i] = g.uptime_s;
  memset(g.act_seen, fresh ? 0 : 1, sizeof(g.act_seen));
  g.last_any_act_s = g.uptime_s;
  g.any_act_seen = fresh ? 0u : 1u;
  memset(g.play_at, 0, sizeof(g.play_at));
  g.play_n = 0;
  memset(g.pet_at, 0, sizeof(g.pet_at));
  g.pet_n = 0;
  g.hold_s = g.uptime_s;
  g.hold_seen = 0;              // a boot inside the night window sleeps at once
  g.nudge_t0_s = 0;
  g.nudge_n = 0;
  g.suppress_until_s = 0;
  g.cq_good_today = 0;
  g.cq_game_today = 0;
  g.day_stamp = 0xFFFFu;
  g.wish_pets = 0;
  g.wish_left_s = 0;
  g.wish_id = 0;
  g.sick_episodes = 0;
  g.snacks_total = 0;
  g.events_done = 0;
  g.offline = 0;
}

// --- DEVICE-WIDE. Only a bind (== a boot) or a brand new creature may -------
//     restart the hourly ceiling; sim_switch() deliberately does NOT.
static void reset_gain_ledger(uint8_t fresh)
{
  for (uint8_t i = 0; i < ST_COUNT; ++i) {
    int32_t cap = gain_cap_milli(i);
    g.gain_budget[i] = (fresh && cap > 0) ? cap : 0;
    g.gain_rem[i] = 0;
  }
}

// The v1 fields SaveSchema v2 does not carry, re-derived at the neutral middle
// of their range. Same defaults the P2-C9 companion blob used when it was
// missing (a migration or a checkpoint recovery), now the only path there is.
static void derive_view(void)
{
  g.view.flags = 0;
  if (g.pb->status & PBS_SICK)         g.view.flags |= PF_SICK;
  if (g.pb->status & PBS_ASLEEP)       g.view.flags |= PF_ASLEEP;
  if (g.pb->flags  & PBF_GOD_TAINTED)  g.view.flags |= PF_GOD_TAINTED;

  g.view.stage      = stage_of_level(g.pb->level);
  g.view.cq         = (int16_t)CQ_START;
  g.view.minor_form = 0;
  g.view.poop_count = 0;
  g.view.pad        = 0;

  g.bond              = STAT_MILLI_MAX / 2;   // v2 folds bonding into care
  g.bond_rem          = 0;
  g.happiness_avg     = (uint8_t)(stat_v(ST_HAPPINESS) / 1000);
  g.last_interact_epoch = g.pb->last_updated_epoch;
}

static void clamp_care(void)
{
  for (uint8_t i = 0; i < ST_COUNT; ++i) {
    stat_set(i, NT_CLAMP(stat_v(i), STAT_MILLI_MIN, STAT_MILLI_MAX));
    stat_rem_set(i, (int16_t)NT_CLAMP((int32_t)stat_r(i), (int32_t)-3599, (int32_t)3599));
  }
}

void sim_bind(BugInstance& bug)
{
  g.pb = &bug;
  // THE STEP SIZE HAS A DEFAULT HERE, AND THIS IS THE ONLY PLACE IT GETS ONE.
  // g.scale is a zero-initialised static and sim_step_seconds() returns it raw;
  // app.cpp's logic_tick() multiplies it by the seconds owed. Until P6-C4 the
  // ONLY writer was sim_set_time_scale(), whose only two callers are both inside
  // dev/godmode.cpp's `#if GOD_MODE_ENABLED` half - so on the RELEASE artefact
  // (GOD_MODE_ENABLED=0) the scale stayed 0, every tick was sim_tick(0), and the
  // whole simulation - care decay, ageing, the XP carry drip, the activity
  // carried minute - stood still for as long as the device was switched on.
  // A default that lives inside a dev feature is not a default. It lives here,
  // where the module goes live, and god mode is now an OVERRIDE rather than the
  // thing that starts the clock. Set only when unset, so binding a second Bug
  // does not cancel an acceleration the player is watching.
  if (g.scale == 0u) g.scale = 1u;
  sim_env_defaults(g.env);
  g.uptime_s = 0;
  reset_bug_state(0);        // PH3 #4: a reload re-earns its ledgers
  reset_gain_ledger(0);
  clamp_care();
  derive_view();
  stage_commit();
  status_sync();

  g.now = (g.pb->last_updated_epoch != 0) ? g.pb->last_updated_epoch : 0u;
  g.sod = 0;
}

void sim_switch(BugInstance& next)
{
  if (g.pb == &next) return;
  g.pb = &next;
  reset_bug_state(0);        // per-Bug only: the gain ledger survives
  clamp_care();
  derive_view();
  stage_commit();
  status_sync();
}

// See sim.h: a swap moved the bound Bug, it did not replace it. Only the
// pointer is wrong. clamp_care() and status_sync() are re-asserted because they
// are idempotent and cost nothing; derive_view() is deliberately absent.
void sim_rebind(BugInstance& p)
{
  if (!g.pb || g.pb == &p) return;
  g.pb = &p;
  clamp_care();
  status_sync();
}

void sim_new_pet(const Genome& gn, uint32_t now_epoch, uint8_t cold)
{
  if (!g.pb) return;
  // Everything but the identity is reset. bug_clear() lives in
  // persistence/save_manager.cpp and sim.cpp links without it (plan 1.3 rule
  // 2), so the four identity fields are lifted out and put back by hand.
  const uint32_t keep_id      = g.pb->id;
  const uint8_t  keep_species = g.pb->species_id;
  const uint32_t keep_seed    = g.pb->creation_seed;
  const uint8_t  keep_origin  = g.pb->origin;
  memset(g.pb, 0, sizeof(BugInstance));
  g.pb->magic         = (uint16_t)BUG_MAGIC;
  g.pb->layout_ver    = (uint8_t)BUG_LAYOUT_VER;
  g.pb->custom_sprite = (uint8_t)ER_CUSTOM_SPRITE_NONE;
  g.pb->id            = keep_id;
  g.pb->species_id    = keep_species;
  g.pb->creation_seed = keep_seed;
  g.pb->origin        = keep_origin;
  g.pb->level         = 1;

  g.uptime_s = 0;
  reset_bug_state(1);        // brand new creature: full budget, no cooldowns
  reset_gain_ledger(1);

  g.view.stage  = STAGE_EGG;
  g.pb->genome  = gn;

  for (uint8_t i = 0; i < ST_COUNT; ++i) { stat_set(i, STAT_MILLI_MAX); stat_rem_set(i, 0); }
  g.pb->birth_epoch        = now_epoch;
  g.pb->last_updated_epoch = now_epoch;
  g.last_interact_epoch    = now_epoch;
  g.pb->age_s              = 0;
  g.view.cq                = CQ_START;
  g.view.minor_form        = 0;
  g.view.poop_count        = 0;
  g.view.pad               = 0;
  g.bond                   = STAT_MILLI_MAX;
  g.bond_rem               = 0;
  g.happiness_avg          = 0;
  g.view.flags             = 0;
  if (cold) g.view.flags |= PF_COLD_EGG;
  if (gene_tainted(gn)) g.view.flags |= PF_GOD_TAINTED;

  stage_commit();
  status_sync();
  g.now = now_epoch;
}

void sim_hatch(void)
{
  if (!g.pb || g.view.stage != STAGE_EGG) return;
  hatch_now();
  stage_commit();
  status_sync();
}

// =============================================================================
// 10. ACTIONS
// =============================================================================
static void result_begin(ActionResult& out, int32_t* snap)
{
  memset(&out, 0, sizeof(out));
  for (uint8_t i = 0; i < ST_COUNT; ++i) snap[i] = stat_v(i);
}

static void result_end(ActionResult& out, const int32_t* snap,
                       int16_t cq_before, uint16_t str_id)
{
  for (uint8_t i = 0; i < ST_COUNT; ++i) {
    out.d[i] = (int16_t)((stat_v(i) - snap[i]) / 1000);
  }
  out.d_cq        = (int16_t)(g.view.cq - cq_before);
  out.str_id      = str_id;
  out.ok          = 1;
  out.err         = AERR_NONE;
}

static void fail(ActionResult& out, uint8_t err, uint16_t cd)
{
  memset(&out, 0, sizeof(out));
  out.ok         = 0;
  out.err        = err;
  out.cooldown_s = cd;
  out.str_id     = (uint16_t)(STR_AERR_NONE + err);
}

// One refused gesture against a sleeping bug. Returns 1 when this is the
// WAKE_NUDGES-th inside the window, having woken the creature.
static uint8_t nudge(uint32_t now_s)
{
  const uint32_t gone = (now_s > g.nudge_t0_s) ? (now_s - g.nudge_t0_s) : 0u;
  if (g.nudge_n == 0u || gone > (uint32_t)WAKE_NUDGE_WINDOW_S) {
    g.nudge_t0_s = now_s;
    g.nudge_n    = 1;
  } else if (g.nudge_n < 0xFFu) {
    g.nudge_n++;
  }
  if (g.nudge_n < (uint8_t)WAKE_NUDGES) return 0;

  g.nudge_n = 0;
  g.view.flags &= (uint16_t)~PF_ASLEEP;
  g.events |= SIM_EV_WAKE;
  // Hold it awake for SLEEP_RELAPSE_S even if the action that woke it then
  // fails on a cooldown: the player asked for the bug, not for a race.
  g.hold_s    = now_s;
  g.hold_seen = 1;
  status_sync();
  return 1;
}

static void note_interaction(uint8_t action)
{
  // Any interaction at all - including the two toggles - postpones bedtime.
  g.hold_s    = g.uptime_s;
  g.hold_seen = 1;

  g.act_last[action] = g.uptime_s;
  g.act_seen[action] = 1;
  g.last_any_act_s   = g.uptime_s;
  g.any_act_seen     = 1;

  // GAME_DESIGN 7.1: only a MEANINGFUL interaction (feed / clean / play /
  // medicine / mimo) resets the loneliness clock.
  // Flipping the sleep switch is housekeeping, not company.
  if (action == ACT_SLEEP_TOGGLE) return;

  g.last_interact_epoch = g.now;
  g.alert = AL_NONE;               // any real interaction addresses the alert
  g.alert_age_s = 0;
}

// Wish satisfaction, checked after a successful action.
static void wish_check(uint8_t action)
{
  if (!(g.view.flags & PF_WISH_ACTIVE)) return;

  uint8_t hit = 0;
  switch (g.wish_id) {
    case WISH_PLAY:  hit = (action == ACT_PLAY); break;
    case WISH_SNACK: hit = (action == ACT_FEED_SNACK); break;
    case WISH_CLEAN: hit = (action == ACT_CLEAN && g.view.poop_count == 0 &&
                            pct_milli(ST_HYGIENE) >= 80); break;
    case WISH_PET3:  if (action == ACT_PET) { if (g.wish_pets < 255) g.wish_pets++; }
                     hit = (g.wish_pets >= 3); break;
    default: break;
  }
  if (!hit) return;

  g.view.flags &= (uint16_t)~PF_WISH_ACTIVE;
  g.view.flags |= PF_WISH_DONE;
  g.wish_left_s = 0;
  cq_add(CQ_D_WISH_OK);
  stat_add(ST_HAPPINESS, WISH_HAPPINESS);
  stat_add(ST_BOND, WISH_BOND);
  g.events |= SIM_EV_WISH_OK;
}

bool sim_apply_action(ActionId action, ActionResult& out)
{
  if (!g.pb)                 { fail(out, AERR_BAD_ARG, 0); return false; }
  if (action == ACT_NONE || action >= ACT_COUNT) { fail(out, AERR_BAD_ARG, 0); return false; }

  if (g.view.stage == STAGE_EGG)   { fail(out, AERR_IS_EGG, 0); return false; }

  // ASLEEP: THE PLAYER CAN INSIST (P3-C2b). The first gesture does not act -
  // it says the bug is asleep and counts as one nudge - but WAKE_NUDGES of
  // them inside WAKE_NUDGE_WINDOW_S wake it, and then this very gesture goes
  // through like any other. The window is what keeps idle taps hours apart
  // from ever adding up, and waking costs NO stat: spec section 27 forbids
  // punishing the player, and the energy that now drains awake is cost enough.
  // ACT_SLEEP_TOGGLE is the explicit switch and never needs a nudge.
  if ((g.view.flags & PF_ASLEEP) && action != ACT_SLEEP_TOGGLE) {
    if (!nudge(g.uptime_s)) {
      fail(out, AERR_ASLEEP, 0);
      return false;
    }
  }

  uint16_t cd = sim_action_cooldown_s(action);
  if (cd > 0) { fail(out, AERR_COOLDOWN, cd); return false; }

  int32_t snap[ST_COUNT];
  int16_t cq_before = g.view.cq;
  uint16_t str_id   = 0;
  result_begin(out, snap);

  switch (action) {

    case ACT_FEED_MEAL: {
      if (pct_milli(ST_HUNGER) > ACT_MEAL_REFUSE_PCT) {
        fail(out, AERR_FULL, 0);
        return false;
      }
      // The hourly satiety budget is the real anti-farm ceiling. A meal that
      // cannot deliver any satiety is refused outright.
      if (sim_gain_left(ST_HUNGER) == 0) { fail(out, AERR_FULL, 0); return false; }
      stat_add(ST_HUNGER, ACT_MEAL_HUNGER);
      cq_add(ACT_MEAL_CQ);
      g.poop_timer = 0;                       // next poop 90 min after the meal
      g.poop_rem   = 0;
      str_id = STR_RX_MEAL;
      break;
    }

    case ACT_FEED_SNACK: {
      stat_add(ST_HUNGER, ACT_SNACK_HUNGER);
      stat_add(ST_HAPPINESS, ACT_SNACK_HAPPINESS);
      if (g.snacks_total < 0xFFFFu) g.snacks_total++;
      str_id = STR_RX_SNACK;
      break;
    }

    case ACT_CLEAN: {
      if (g.view.poop_count == 0 && pct_milli(ST_HYGIENE) >= 95) {
        fail(out, AERR_NOTHING_TODO, 0);
        return false;
      }
      if (g.view.poop_count > 0) g.view.poop_count--;
      stat_add(ST_HYGIENE, ACT_CLEAN_HYGIENE);
      str_id = STR_RX_CLEAN;
      break;
    }

    case ACT_MEDICINE: {
      if (!(g.view.flags & PF_SICK)) { fail(out, AERR_NOT_SICK, 0); return false; }
      uint8_t second = (pct_milli(ST_HEALTH) < 25 &&
                        rnd_below(100u) < (uint32_t)ACT_MED_SECOND_DOSE_PCT) ? 1u : 0u;
      if (!second) {
        g.view.flags &= (uint16_t)~PF_SICK;
        g.events |= SIM_EV_SICK_END;
      }
      stat_add(ST_HAPPINESS, ACT_MED_HAPPINESS);
      str_id = STR_RX_MED;
      break;
    }

    case ACT_PLAY: {
      if (pct_milli(ST_ENERGY) < ACT_PLAY_MIN_ENERGY_PCT) {
        fail(out, AERR_TIRED, 0);
        return false;
      }
      // The minigame cooldown used to be checked here too; see minigame_guard().
      int32_t gain = ((int32_t)ACT_PLAY_HAPPINESS_MAX
                      * (int32_t)sim_play_decay_permille()) / 1000;
      stat_add(ST_HAPPINESS, gain);
      stat_add(ST_ENERGY, ACT_PLAY_ENERGY);
      if (g.pb->minigames_won < 0xFFFFu) g.pb->minigames_won++;
      if (g.cq_game_today < CQ_MINIGAME_DAILY_CAP) {
        cq_add(CQ_D_MINIGAME);
        g.cq_game_today = (uint16_t)(g.cq_game_today + CQ_D_MINIGAME);
      }
      play_window_push();
      str_id = STR_RX_PLAY;
      break;
    }

    case ACT_PET: {
      uint8_t bonus = pet_window_bonus();
      stat_add(ST_BOND, (int32_t)bonus);
      stat_add(ST_HAPPINESS, (bonus > 0) ? ACT_PET_HAPPINESS : 0);
      pet_window_push();
      str_id = STR_RX_PET;
      break;
    }

    case ACT_SLEEP_TOGGLE: {
      g.view.flags ^= PF_ASLEEP;
      if (g.view.flags & PF_ASLEEP) { g.events |= SIM_EV_SLEEP; str_id = STR_RX_SLEEP; }
      else                     { g.events |= SIM_EV_WAKE;  str_id = STR_RX_WAKE;  }
      break;
    }

    default:
      fail(out, AERR_BAD_ARG, 0);
      return false;
  }

  note_interaction((uint8_t)action);
  wish_check((uint8_t)action);
  result_end(out, snap, cq_before, str_id);
  status_sync();
  return true;
}

// =============================================================================
// 11. MINIGAMES - the on-device games and their shared ledger
// =============================================================================
static bool minigame_guard(ActionResult& out)
{
  if (!g.pb)                { fail(out, AERR_BAD_ARG, 0); return false; }
  if (g.view.stage == STAGE_EGG)  { fail(out, AERR_IS_EGG, 0); return false; }
  if (g.view.flags & PF_ASLEEP) { fail(out, AERR_ASLEEP, 0); return false; }
  if (pct_milli(ST_ENERGY) < ACT_PLAY_MIN_ENERGY_PCT) { fail(out, AERR_TIRED, 0); return false; }
  // NO COOLDOWN. There was a 120 s one here and it is gone: it let the owner
  // play exactly one twenty-second game per visit to the device and then showed
  // a countdown. data/balance.h's decay curve is the anti-farm now, and it
  // takes the REWARD down instead of taking the button away. What is left in
  // this guard is the three things that are about the CREATURE and not about
  // the clock: an egg cannot play, a sleeping Bug must not be woken to play,
  // and an exhausted one is what the energy floor is for.
  return true;
}

static void minigame_commit(uint8_t won)
{
  if (won && g.pb->minigames_won < 0xFFFFu) g.pb->minigames_won++;
  if (won && g.cq_game_today < CQ_MINIGAME_DAILY_CAP) {
    cq_add(CQ_D_MINIGAME);
    g.cq_game_today = (uint16_t)(g.cq_game_today + CQ_D_MINIGAME);
  }
  play_window_push();
  note_interaction((uint8_t)ACT_PLAY);
}

bool sim_apply_play_result(uint16_t win_permille, ActionResult& out,
                           uint16_t* paid_permille)
{
  if (paid_permille) *paid_permille = 0u;
  if (!minigame_guard(out)) return false;
  if (win_permille > 1000u) win_permille = 1000u;

  int32_t snap[ST_COUNT];
  int16_t cq_before = g.view.cq;
  result_begin(out, snap);

  // THE DECAY IS READ ONCE, HERE, AND HANDED BACK - which is the whole reason
  // for the out-parameter. minigame_commit() below pushes this run into the
  // rolling window, so a caller that asked sim_play_decay_permille() itself
  // would get a different answer depending on whether it asked before or after
  // this call, and the XP award would silently be one step out of step with the
  // happiness. One reader, one moment, no order to get wrong.
  const uint16_t decay = sim_play_decay_permille();
  if (paid_permille)
    *paid_permille = (uint16_t)(((uint32_t)win_permille * (uint32_t)decay) / 1000u);

  int32_t gain = ((int32_t)ACT_PLAY_HAPPINESS_MAX * (int32_t)win_permille) / 1000;
  gain = (gain * (int32_t)decay) / 1000;

  stat_add(ST_HAPPINESS, gain);
  stat_add(ST_ENERGY, ACT_PLAY_ENERGY);

  minigame_commit((win_permille >= 500u) ? 1u : 0u);
  result_end(out, snap, cq_before,
             (win_permille >= 500u) ? (uint16_t)STR_GM_WIN : (uint16_t)STR_GM_LOSE);
  wish_check((uint8_t)ACT_PLAY);
  status_sync();
  return true;
}

// =============================================================================
// 12. OFFLINE CATCH-UP  (GAME_DESIGN 5.2 / 5.3 / 5.4)
//     The escalation ladder is gone: an absence costs exactly what the
//     integration says it costs, at MULT_OFFLINE_DECAY, and nothing more.
// =============================================================================

// Integrates `absence_s` at the offline rate starting from g.now. Returns the
// number of OFFLINE_STEP_S blocks actually run.
static uint32_t run_offline(uint32_t absence_s)
{
  uint32_t steps = absence_s / OFFLINE_STEP_S;
  uint32_t tail  = absence_s % OFFLINE_STEP_S;
  if (steps > (uint32_t)OFFLINE_MAX_STEPS) {
    // Beyond 41.7 days the extra decay would land on stats that bottomed out
    // long ago, so the remainder is simply not simulated.
    steps = OFFLINE_MAX_STEPS;
    tail  = 0;
  }

  g.offline = 1;
  uint32_t done = 0;
  for (uint32_t i = 0; i < steps; ++i) {
    uint32_t left = OFFLINE_STEP_S;
    while (left > 0) {
      uint32_t dt = (left > SIM_SUBSTEP_S) ? SIM_SUBSTEP_S : left;
      sub_step(dt);
      left -= dt;
    }
    done++;
  }
  uint32_t left = tail;
  while (left > 0) {
    uint32_t dt = (left > SIM_SUBSTEP_S) ? SIM_SUBSTEP_S : left;
    sub_step(dt);
    left -= dt;
  }
  g.offline = 0;
  return done;
}

void sim_catch_up_ex(uint32_t absence_s, uint8_t clock_known, AbsenceReport& rep)
{
  memset(&rep, 0, sizeof(rep));
  if (!g.pb) return;

  // PH3 #1 (belt and braces; the caller applies the same test with more
  // context). "No trustworthy clock" is not the same as "abandoned". A
  // computed absence of exactly 0 is positive evidence that nothing elapsed -
  // a first run, or a boot whose baseline is not a real epoch and therefore
  // cannot describe a gap at all. There is nothing for a later calibration to
  // retro-fix either, so report it as the true, known zero rather than arming
  // PF_ABS_UNKNOWN on every clock-less boot.
  if (!clock_known && absence_s == 0u) {
    clock_known = 1u;
  }

  rep.clock_known = clock_known ? 1u : 0u;

  // Nonsense clock or no clock at all: AN UNKNOWN CLOCK CHARGES ZERO (plan
  // section 1.7). The device cannot measure the gap, so it does not get to
  // invent one; PF_ABS_UNKNOWN flags the save and the truth is charged in full
  // by sim_absence_retrofix() the moment gt_set_epoch() lands.
  if (!clock_known || absence_s > ABSENCE_MAX_S) {
    absence_s = 0;
    g.view.flags  |= PF_ABS_UNKNOWN;
    rep.clock_known = 0;
  } else {
    g.view.flags &= (uint16_t)~PF_ABS_UNKNOWN;
  }

  rep.absence_s = absence_s;

  // Wind the simulation clock back to when we last saw the player.
  uint32_t start_epoch = (g.env.now_epoch > absence_s)
                           ? (g.env.now_epoch - absence_s) : 0u;
  uint32_t sod_now = ((uint32_t)g.env.local_hour * 3600u)
                   + ((uint32_t)g.env.local_min * 60u);
  g.now = start_epoch;
  g.sod = (uint32_t)(((uint64_t)sod_now + 86400ull * 4ull
                      - (uint64_t)(absence_s % 86400u)) % 86400ull);

  rep.steps = (uint16_t)NT_MIN(run_offline(absence_s), 65535u);

  g.now = start_epoch + absence_s;
  g.sod = sod_now;
  g.pb->last_updated_epoch = g.now;
}

void sim_absence_retrofix(uint32_t true_absence_s)
{
  if (!g.pb) return;
  if (!(g.view.flags & PF_ABS_UNKNOWN)) return;
  g.view.flags &= (uint16_t)~PF_ABS_UNKNOWN;

  // Nothing was integrated at boot, so the whole absence is charged here. The
  // sim clock is already at "now": rewind it, integrate, and put it back.
  if (true_absence_s == 0u || true_absence_s > ABSENCE_MAX_S) return;

  const uint32_t end_epoch = g.now;
  const uint32_t end_sod   = g.sod;
  g.now = (end_epoch > true_absence_s) ? (end_epoch - true_absence_s) : 0u;
  g.sod = (uint32_t)(((uint64_t)end_sod + 86400ull * 4ull
                      - (uint64_t)(true_absence_s % 86400u)) % 86400ull);

  (void)run_offline(true_absence_s);

  g.now = end_epoch;
  g.sod = end_sod;
  g.pb->last_updated_epoch = g.now;
}

// =============================================================================
// 13. GOD MODE HOOKS
// =============================================================================
void sim_god_set_stat(StatId id, uint8_t pct)
{
  if (!g.pb || id >= ST_COUNT) return;
  if (pct > 100) pct = 100;
  stat_set(id, (int32_t)pct * 1000);
  stat_rem_set(id, 0);
  g.view.flags |= PF_GOD_TAINTED;
  status_sync();
}

void sim_god_set_sick(uint8_t sick)
{
  if (!g.pb) return;
  const uint8_t was = (g.view.flags & PF_SICK) ? 1u : 0u;
  if (sick) g.view.flags |= PF_SICK;
  else      g.view.flags &= (uint16_t)~PF_SICK;
  if (was != (sick ? 1u : 0u)) {
    g.events |= sick ? SIM_EV_SICK_START : SIM_EV_SICK_END;
  }
  g.view.flags |= PF_GOD_TAINTED;
  status_sync();
}

void sim_god_set_stage(uint8_t stage)
{
  if (!g.pb || stage >= STAGE_COUNT) return;
  g.view.flags |= PF_GOD_TAINTED;

  g.view.stage = stage;
  status_sync();
  if (stage == STAGE_EGG) { g.pb->age_s = 0; return; }
  g.pb->age_s = stage_enter_s(stage);
  stage_commit();
  if (stage == STAGE_CHILD || stage == STAGE_TEEN) set_minor_form(stage);
  g.events |= SIM_EV_STAGE_UP;
}

void sim_god_set_genome(const Genome& gn)
{
  if (!g.pb) return;
  g.pb->genome = gn;
  gene_set_tainted(g.pb->genome, 1);        // masks, stamps magic_ver and reseals
  g.view.flags |= PF_GOD_TAINTED;
  status_sync();
  // Nothing in this module caches a genome-derived value, so installing a new
  // genome disturbs nothing else: the pet keeps its age, stats and stage.
}
