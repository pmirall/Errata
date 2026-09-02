// =============================================================================
//  NOTTAMAGOCHI - sim.cpp
//  The stat model. The ONLY module allowed to mutate PetSave.
//
//  Numeric contract (BRIEF risk #10):
//    * every stat is int32_t milli-points in [0, 100000]
//    * every rate is milli-points PER HOUR; per-step delta is
//        (rate * dt + rem) / 3600   with rem carried in PetSave.stat_rem[]
//      so integer division never drifts, at any step size
//    * multiplier chains run in micro-points/hour (int64) and are rounded ONCE
//      back to milli-points/hour, so a five-deep chain costs < 0.001 pt/h
//    * clamps at BOTH ends after every operation
//    * ZERO floating point anywhere in this file
// =============================================================================
#include "sim.h"
#include "genome.h"
#include "rng.h"
#include "strings_es.h"

#include <string.h>

// Sub-step grid. Every cadence in the design (60 s stage check, 600 s sickness
// roll, 600 s care-quality tick) is a multiple of this, so the event grid is
// identical whether the caller ticks 1 s at a time or hands us 1800 s at once.
#define SIM_SUBSTEP_S            60u

// Auto-wake thresholds (see the note on "collapse" in section 6 below).
#define SIM_WAKE_DAY_ENERGY_PCT  60
#define SIM_ALERT_HEALTH_PCT     30

// =============================================================================
// 1. MODULE STATE
//    PetSave holds everything that must survive a power cut. Most of what
//    follows is a RAM-only cadence accumulator, where losing it on reboot
//    costs at most one sub-step of phase and never a point of stat.
//
//    PH3 #4 - THE ANTI-FARM LEDGERS ARE THE EXCEPTION, AND THIS COMMENT USED
//    TO CLAIM OTHERWISE. Four of the blocks below meter how much a player can
//    gain per unit of REAL time, so zeroing their phase on reboot does convert
//    directly into stat points:
//      g_gain_budget[]  the hourly gain ceiling (BRIEF 1.6)
//      g_act_last[]     the six per-action cooldowns + the global one
//      g_mg_last_s      the minigame cooldown
//      g_play_at[]      the 3 h play-payout decay window
//    reset_ram_state(0) seeds all four as if they had JUST been spent, and the
//    boot's offline catch-up then refills them at exactly the rate real time
//    would have, so a reboot is worth its own wall-clock duration and no more.
//
//    g_gain_budget[] IS NOW PERSISTED (PH4 section 6 item 1). "Assume the
//    worst" killed the exploit but lied to the player: measured on an adult at
//    20 % satiety after a 5 s brownout, FEED_MEAL answered AERR_FULL - "esta
//    lleno" - from 21 s to 59 s, and a full 30-point meal was unavailable for
//    1799 s (29 min). The ledger is therefore written as whole points next to
//    the pet, and sim_init()'s zero is overwritten by the caller:
//
//      sim_gain_snapshot()  hands the live budget out, truncated toward zero
//      sim_gain_restore()   seeds min(cap, saved + elapsed*cap/3600)
//
//    This module still does NO I/O (BRIEF section 4). storage.cpp owns the 20 B
//    blob and NVS key "gl", the entry point calls sim_gain_restore() right
//    after sim_init() and binds a snapshot provider that storage calls at the
//    instant it commits a "save"; the ledger rides that write and adds no NVS
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
//    RESIDUAL 1, KNOWN AND ACCEPTED: g_play_at[] is NOT reconstructed and NOT
//    persisted. The honest reconstruction is "assume the window was full",
//    which would drop the play payout to PLAY_DECAY_5 (0 permille) for 3 h
//    after any innocent power cut - a far larger punishment than the exploit it
//    prevents. It still resets on reboot.
//    RESIDUAL 2: the cooldowns are still seeded as "just started", so the first
//    seconds after a reboot can answer AERR_COOLDOWN. That message is TRUE - it
//    says wait, and the wait is real - and it is bounded by each action's own
//    constant: 20 s feed, 15 s clean, 25 s play, 30 s med, and MG_COOLDOWN_S
//    (120 s) for a minigame, which is the widest of them (g_mg_seen is set on a
//    reload, so sim_minigame_cooldown_s() reads the full 120 s at uptime 0).
//    RESIDUAL 3: with no snapshot (first boot on this firmware, a wiped unit, a
//    corrupt blob) or with no trustworthy clock at either end of the interval,
//    sim_gain_restore() seeds 0 and the 29-minute behaviour above is exactly
//    what the player gets. That is the safe direction, and it is deliberate:
//    any non-zero CONSTANT seed would be farmable again.
// =============================================================================
static PetSave*  g_pet   = 0;
static SimEnv    g_env;
static uint32_t  g_scale = 1;             // god-mode time scale
static uint32_t  g_events = 0;

static uint32_t  g_now   = 0;             // sim's epoch cursor (offline-aware)
static uint32_t  g_sod   = 0;             // local seconds-of-day, 0..86399
static uint8_t   g_offline = 0;           // 1 while the catch-up loop runs
static uint32_t  g_uptime_s = 0;          // monotonic simulated seconds

// cadence accumulators
static uint32_t  g_acc_stage  = 0;
static uint32_t  g_acc_cq     = 0;
static uint32_t  g_acc_sick   = 0;
static uint32_t  g_acc_minute = 0;
static uint32_t  g_poop_timer = 0;

// fractional remainders that have no home in PetSave
static int32_t   g_weight_frac = 0;   // sub-decigram part of weight_dg, 0..999
static int32_t   g_weight_hrem = 0;   // the /3600 remainder of the weight rate
static int32_t   g_dmg_rem[DMG_COUNT];
static int32_t   g_hapavg_rem = 0;
static int32_t   g_lightsleep_rem = 0;

// alerts / care_miss
static uint8_t   g_alert = AL_NONE;
static uint32_t  g_alert_age_s = 0;
static uint32_t  g_since_miss_s = 0;

// post-absence grudge
static uint32_t  g_sulk_left_s = 0;
static uint32_t  g_absence_ctx_s = 0;     // length of the absence just applied

// shared ledger
static uint32_t  g_act_last[ACT_COUNT];
static uint8_t   g_act_seen[ACT_COUNT];
static uint32_t  g_last_any_act_s = 0;
static uint8_t   g_any_act_seen = 0;
static int32_t   g_gain_budget[ST_COUNT];
static int32_t   g_gain_rem[ST_COUNT];
static uint32_t  g_play_at[PLAY_DECAY_STEPS];
static uint8_t   g_play_n = 0;
static uint32_t  g_pet_at[PET_DECAY_STEPS];
static uint8_t   g_pet_n = 0;
static uint32_t  g_mg_last_s = 0;
static uint8_t   g_mg_seen = 0;

// misc gameplay bookkeeping
static uint8_t   g_force_feed = 0;        // consecutive refused FEED_MEAL
static uint32_t  g_misbehave_s = 0;       // SCOLD justification window
static uint8_t   g_misbehave = 0;
static uint32_t  g_overfeed_until_s = 0;  // 3 overfeeds -> +10 %/h for 2 h
static uint32_t  g_suppress_until_s = 0;  // 5th poop suppressed -> +12 %/h
static uint16_t  g_cq_good_today = 0;
static uint16_t  g_cq_game_today = 0;
static uint16_t  g_day_stamp = 0xFFFFu;
static uint32_t  g_storm_left_s = 0;
static uint8_t   g_storm_pets = 0;
static uint8_t   g_wish_pets = 0;
static uint8_t   g_sick_hours = 0;        // untreated sickness, for attribution

// =============================================================================
// 2. SMALL INTEGER PRIMITIVES
// =============================================================================
// Every draw of the simulation comes from the RNG_CARE stream (rng.h).
static inline uint32_t rnd(void)
{
  return rng_u32(RNG_CARE);
}

// Modulo on purpose: the legacy sim reduced its draws this way, and the care
// golden (tests/golden/sim_v1.txt) pins that exact sequence of outcomes.
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

// Same, for a stat whose remainder lives in PetSave.stat_rem[] (int16, |r|<3600)
static void accum_stat(uint8_t id, int32_t rate_mph, uint32_t dt_s)
{
  int32_t rem = (int32_t)g_pet->stat_rem[id];
  accum(g_pet->stat[id], rem, rate_mph, dt_s, STAT_MILLI_MIN, STAT_MILLI_MAX);
  g_pet->stat_rem[id] = (int16_t)rem;
}

static inline int32_t pct_milli(uint8_t id) { return g_pet->stat[id] / 1000; }

static void cq_add(int32_t d)
{
  int32_t v = (int32_t)g_pet->cq + d;
  g_pet->cq = (int16_t)NT_CLAMP(v, (int32_t)CQ_MIN, (int32_t)CQ_MAX);
}

static uint16_t today_stamp(void)
{
  if (g_env.clock_valid) return g_env.day_of_year;
  return (uint16_t)((g_now / 86400u) % 366u);
}

// =============================================================================
// 3. ADULT FORM MODIFIER TABLE (GAME_DESIGN 2.3), all x1000
//    QUIMERA takes the best value of every column, which is exactly the
//    "inherits the better multiplier of each pair" rule with the parent forms
//    unknown at runtime.
// =============================================================================
struct FormMods {
  uint16_t hun, hap, nrg, hyg, disc, regen, dmg, sick, life;
};

static const FormMods FORM_NEUTRAL =
  { 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000 };

static const FormMods FORM_MOD[FORM_COUNT] = {
  /* BOLOTA     */ { 1350,  750, 1200, 1000, 1000, 1000, 1000, 1000,  900 },
  /* ZAMPASALTO */ { 1000, 1200, 1250, 1000, 1000, 1400, 1000, 1000, 1050 },
  /* BUHO       */ {  900,  900,  900,  900, 1000, 1000,  850, 1000, 1150 },
  /* PUNKI      */ { 1000,  700, 1000, 1000, 2000, 1000, 1000, 1000, 1000 },
  /* MOHO       */ { 1000, 1300, 1000, 1000, 1000, 1000, 1300, 1600,  750 },
  /* QUIMERA    */ {  900,  700,  900,  900, 1000, 1400,  850, 1000, 1100 }
};

static const FormMods* form_of(void)
{
  if (g_pet->stage >= STAGE_ADULT && g_pet->stage < STAGE_DEAD &&
      g_pet->adult_form < (uint8_t)FORM_COUNT) {
    return &FORM_MOD[g_pet->adult_form];
  }
  return &FORM_NEUTRAL;
}

static const uint16_t STAGE_MULT[STAGE_COUNT] = {
  STAGE_MULT_EGG, STAGE_MULT_BABY, STAGE_MULT_CHILD, STAGE_MULT_TEEN,
  STAGE_MULT_ADULT, STAGE_MULT_SENIOR, STAGE_MULT_DEAD
};

static const uint16_t PLAY_DECAY[PLAY_DECAY_STEPS] = {
  PLAY_DECAY_0, PLAY_DECAY_1, PLAY_DECAY_2, PLAY_DECAY_3,
  PLAY_DECAY_4, PLAY_DECAY_5
};

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
    accum(g_gain_budget[i], g_gain_rem[i], cap, dt_s, 0, cap);
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
    if (milli > g_gain_budget[id]) milli = g_gain_budget[id];
    taken = milli;
    g_gain_budget[id] -= milli;
  }

  int32_t before = g_pet->stat[id];
  int32_t v = before + milli;
  v = NT_CLAMP(v, STAT_MILLI_MIN, STAT_MILLI_MAX);
  g_pet->stat[id] = v;

  int32_t applied = v - before;
  // Refund only what a metered GAIN failed to deliver against the 100.000
  // ceiling. A penalty never touches the budget (taken == 0 there).
  if (taken > 0 && taken > applied) {
    g_gain_budget[id] += (taken - applied);
    if (g_gain_budget[id] > cap) g_gain_budget[id] = cap;
  }
  return (int16_t)(applied / 1000);
}

static void weight_add(int32_t dg)
{
  int32_t v = (int32_t)g_pet->weight_dg + dg;
  g_pet->weight_dg = (int16_t)NT_CLAMP(v, (int32_t)WEIGHT_DG_MIN,
                                          (int32_t)WEIGHT_DG_MAX);
}

static uint16_t cd_for(uint8_t action)
{
  switch (action) {
    case ACT_FEED_MEAL:
    case ACT_FEED_SNACK:   return ACT_CD_FEED_S;
    case ACT_CLEAN:        return ACT_CD_CLEAN_S;
    case ACT_MEDICINE:     return ACT_CD_MED_S;
    case ACT_PLAY:         return ACT_CD_PLAY_S;
    case ACT_SLEEP_TOGGLE:
    case ACT_LIGHT_TOGGLE: return ACT_CD_SLEEP_S;
    default:               return 0;
  }
}

uint16_t sim_action_cooldown_s(ActionId action)
{
  if (action >= ACT_COUNT) return 0;

  uint32_t left = 0;
  if (g_act_seen[action]) {
    uint32_t cd   = cd_for((uint8_t)action);
    uint32_t gone = g_uptime_s - g_act_last[action];
    if (gone < cd) left = cd - gone;
  }
  if (g_any_act_seen) {
    uint32_t gone = g_uptime_s - g_last_any_act_s;
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
  return (uint16_t)(g_gain_budget[id] / 1000);
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
    int32_t p = g_gain_budget[i] / 1000;
    if (p < 0)   p = 0;
    if (p > 255) p = 255;                            // caps are static_assert-ed < 256
    out_pts[i] = (uint8_t)p;
  }
}

uint8_t sim_gain_restore(const uint8_t* pts, uint8_t n,
                         uint32_t saved_epoch, uint32_t now_epoch)
{
  // No snapshot, or no trustworthy clock at either end of the interval: there is
  // no elapsed time to reconstruct from. Seed 0 - the pre-PH4 behaviour, which
  // is merely unkind - rather than `cap`, which would be the exploit.
  const uint8_t trust = (pts != 0 && n != 0 &&
                         saved_epoch >= (uint32_t)NT_EPOCH_SANE_MIN &&
                         now_epoch   >= (uint32_t)NT_EPOCH_SANE_MIN) ? 1u : 0u;

  uint32_t elapsed = 0;
  if (trust && now_epoch > saved_epoch) {
    elapsed = now_epoch - saved_epoch;
  }
  // One hour refills the whole cap, so clamping here loses nothing and is what
  // keeps the multiply below inside int32 for an elapsed of years (or of a clock
  // that jumped): cap <= 90000 milli, elapsed <= 3600 => 324e6 < 2^31.
  if (elapsed > (uint32_t)SEC_PER_HOUR) elapsed = (uint32_t)SEC_PER_HOUR;

  for (uint8_t i = 0; i < ST_COUNT; ++i) {
    int32_t cap = gain_cap_milli(i);
    if (cap < 0) { g_gain_budget[i] = 0; g_gain_rem[i] = 0; continue; }

    int32_t v = 0;
    if (trust) {
      int32_t saved = (i < n) ? ((int32_t)pts[i] * 1000) : 0;
      if (saved > cap) saved = cap;                 // a foreign/edited blob cannot exceed the cap
      if (saved < 0)   saved = 0;
      v = saved + (int32_t)((cap * (int32_t)elapsed) / (int32_t)SEC_PER_HOUR);
      if (v > cap) v = cap;
    }
    g_gain_budget[i] = v;
    g_gain_rem[i]    = 0;
  }
  return trust;
}

uint16_t sim_minigame_cooldown_s(void)
{
  if (!g_mg_seen) return 0;
  uint32_t gone = g_uptime_s - g_mg_last_s;
  return (gone >= (uint32_t)MG_COOLDOWN_S)
           ? 0 : (uint16_t)((uint32_t)MG_COOLDOWN_S - gone);
}

uint8_t sim_play_window_count(void)
{
  uint8_t n = 0;
  for (uint8_t i = 0; i < g_play_n; ++i) {
    if (g_uptime_s - g_play_at[i] < PLAY_DECAY_WINDOW_S) n++;
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
  for (uint8_t i = 0; i < g_play_n; ++i) {
    if (g_uptime_s - g_play_at[i] < PLAY_DECAY_WINDOW_S) g_play_at[w++] = g_play_at[i];
  }
  if (w >= PLAY_DECAY_STEPS) {
    for (uint8_t i = 1; i < w; ++i) g_play_at[i - 1] = g_play_at[i];
    w = PLAY_DECAY_STEPS - 1;
  }
  g_play_at[w++] = g_uptime_s;
  g_play_n = w;
}

static uint8_t pet_window_bonus(void)
{
  uint8_t n = 0;
  for (uint8_t i = 0; i < g_pet_n; ++i) {
    if (g_uptime_s - g_pet_at[i] < 3600u) n++;
  }
  if (n >= PET_DECAY_STEPS) return PET_DECAY[PET_DECAY_STEPS - 1];
  return PET_DECAY[n];
}

static void pet_window_push(void)
{
  uint8_t w = 0;
  for (uint8_t i = 0; i < g_pet_n; ++i) {
    if (g_uptime_s - g_pet_at[i] < 3600u) g_pet_at[w++] = g_pet_at[i];
  }
  if (w >= PET_DECAY_STEPS) {
    for (uint8_t i = 1; i < w; ++i) g_pet_at[i - 1] = g_pet_at[i];
    w = PET_DECAY_STEPS - 1;
  }
  g_pet_at[w++] = g_uptime_s;
  g_pet_n = w;
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
  g_env = env;
  if (!g_offline) {
    if (g_env.now_epoch != 0) g_now = g_env.now_epoch;
    g_sod = ((uint32_t)g_env.local_hour * 3600u) + ((uint32_t)g_env.local_min * 60u);
  }
}

const SimEnv& sim_env(void) { return g_env; }

void sim_seed(uint32_t seed) { rng_seed(RNG_CARE, seed); }

void sim_set_time_scale(uint32_t scale)
{
  g_scale = (scale == 0u) ? 1u : scale;
}

uint32_t sim_step_seconds(void){ return g_scale; }

// =============================================================================
// 6. STAGE / FORM HELPERS
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

uint32_t sim_natural_death_s(void)
{
  int32_t h = (int32_t)DEATH_NATURAL_BASE_H
            + (((int32_t)g_pet->cq - (int32_t)CQ_START) / DEATH_NATURAL_CQ_DIV);
  h = NT_CLAMP(h, (int32_t)DEATH_NATURAL_MIN_H, (int32_t)DEATH_NATURAL_MAX_H);
  h = (int32_t)(((int64_t)h * (int64_t)form_of()->life) / 1000);
  if (h < 1) h = 1;
  return (uint32_t)h * 3600u;
}

uint8_t sim_care_grade(void)
{
  int16_t cq = g_pet->cq;
  if (cq >= 850) return GRADE_A;
  if (cq >= 700) return GRADE_B;
  if (cq >= 550) return GRADE_C;
  if (cq >= 400) return GRADE_D;
  if (cq >= 250) return GRADE_E;
  return GRADE_F;
}

// GAME_DESIGN 2.2 - branch scoring at TEEN -> ADULT.
static uint8_t pick_adult_form(void)
{
  const PetSave& p = *g_pet;

  // QUIMERA is a hard override: forced mutation load or a cross-species hybrid.
  if (gene_mutations(p.genome) >= 3 || (p.flags & PF_HYBRID_ELIG)) {
    return (uint8_t)FORM_QUIMERA;
  }
  // Neglect override: this one is unconditional per the spec.
  if (p.cq < 200 || p.care_miss >= 12) return (uint8_t)FORM_MOHO;

  int32_t disc = pct_milli(ST_DISCIPLINE);
  int32_t bond = pct_milli(ST_BOND);
  int32_t w    = (int32_t)p.weight_dg;

  int32_t s_glut  = 3 * (int32_t)p.overfeed + 2 * (int32_t)p.snacks_total
                  + NT_MAX((int32_t)0, (w - 450) / 10)
                  - 2 * (int32_t)p.minigames_won;
  int32_t s_athl  = 3 * (int32_t)p.minigames_won
                  + NT_MAX((int32_t)0, (450 - w) / 10)
                  + disc / 4 - 2 * (int32_t)p.overfeed;
  int32_t s_wise  = disc / 2 + bond / 2 + (int32_t)p.cq / 20
                  - 6 * (int32_t)p.care_miss - 4 * (int32_t)p.sick_episodes;
  int32_t s_rebel = bond / 2 + (int32_t)p.happiness_avg / 2
                  + 2 * (int32_t)p.unjust_scolds - disc / 2;
  int32_t s_rot   = 6 * (int32_t)p.care_miss + 5 * (int32_t)p.sick_episodes
                  + NT_MAX((int32_t)0, (500 - (int32_t)p.cq)) / 10;

  const int32_t score[5] = { s_glut, s_athl, s_wise, s_rebel, s_rot };
  const uint8_t form[5]  = { (uint8_t)FORM_BOLOTA, (uint8_t)FORM_ZAMPASALTO,
                             (uint8_t)FORM_BUHO,   (uint8_t)FORM_PUNKI,
                             (uint8_t)FORM_MOHO };

  uint8_t best = 0;
  uint8_t ties = 1;
  for (uint8_t i = 1; i < 5; ++i) {
    if (score[i] > score[best]) { best = i; ties = 1; }
    else if (score[i] == score[best]) { ties++; }
  }
  if (ties > 1) {
    // Ties broken by temperament parity (GAME_DESIGN 2.2).
    uint8_t parity = (uint8_t)(gene_temperament(g_pet->genome) & 1u);
    uint8_t seen = 0;
    for (uint8_t i = 0; i < 5; ++i) {
      if (score[i] == score[best]) {
        if (parity == 0) { best = i; break; }
        seen = i;                       // parity 1 -> take the LAST tied entry
      }
    }
    if (parity != 0) best = seen;
  }
  return form[best];
}

// =============================================================================
//  PH3 #6 - MUTATION LOAD RELIEF.  AMENDS GAME_DESIGN 2.3 row 5 / 4.3 step 6.
//
//  THE DEFECT. gene_mutations is monotonic: genome_death_egg adds +1
//  unconditionally, genome_breed takes max(A,B)+1, and the cold-egg rule adds
//  another. pick_adult_form() gates QUIMERA on `>= 3`. So generation 0/1/2 roll
//  a form and generation 3 onward is unconditionally QUIMERA, forever - the
//  branch-scoring block above becomes dead code about a week into play, the
//  silhouette stops changing, and QUIMERA's modifiers (best-in-class regen,
//  damage and lifespan) make the game monotonically easier from there on.
//
//  THE CHOICE. Of the three fixes on the table:
//    * raising the threshold to 12 only postpones the plateau to generation 12;
//      the counter is still monotonic, so every dynasty still ends up
//      permanently QUIMERA and the form is still "survived long enough",
//      not "earned";
//    * a gate relative to the parent's count needs the parent's count stored,
//      and Genome is a 16 B static_asserted struct broadcast verbatim over BLE
//      (BRIEF 6.2) with no spare field - a wire-format change for a balance bug;
//    * making the counter DECAY keeps the absolute `>= 3` gate the spec
//      already specifies, needs no layout change, and is the only one of the
//      three that restores the design intent, so that is what this is.
//
//  THE RULE. The mutation counter stops being an age counter and becomes a
//  genetic LOAD that husbandry can pay off. Reaching ADULT relieves it by the
//  care grade at that moment:  A (cq >= 850) -> -3,  B (cq >= 700) -> -2,
//  C (cq >= 550) -> -1,  D/E/F -> 0. Against the +1 every death-egg adds
//  (GAME_DESIGN 4.3 step 6, unchanged - the forced novelty guarantee stays),
//  the steady state per generation is:
//    grade A lineage  net -2 -> settles at 1, never QUIMERA;
//    grade B lineage  net -1 -> recovers from any load, slowly;
//    grade C lineage  net  0 -> plateaus wherever it is;
//    grade D or worse net +1 -> reaches 3 and turns QUIMERA, and stays there
//                               until the player raises a generation properly.
//  A pet that dies BEFORE adulthood never collects any relief at all, so a
//  lineage that keeps losing its young is the fastest route to a chimera.
//  A chimera is therefore RARE (three consecutive badly-raised or short-lived
//  generations, or the BLE cross-species route, which is untouched) and EARNED
//  (by a genuinely turbulent lineage - which is what "chimeric halves,
//  mismatched eyes, a 2 px offset seam" is meant to read as), and it is
//  ESCAPABLE in two well-raised generations, so the 60 lines of branch scoring
//  stay live for the whole life of the device instead of one week.
//
//  Applied AFTER pick_adult_form(): this pet's own form is decided by the load
//  it was born with, which it did not choose. The relief is the reward its
//  OFFSPRING inherit.
// =============================================================================
static void mutation_load_relief(void)
{
  PetSave& p = *g_pet;

  uint8_t relief = 0;
  const uint8_t grade = sim_care_grade();
  if      (grade == GRADE_A) relief = 3;
  else if (grade == GRADE_B) relief = 2;
  else if (grade == GRADE_C) relief = 1;
  if (relief == 0) return;

  const uint8_t mc = gene_mutations(p.genome);
  if (mc == 0) return;

  // gene_set_mutations() re-seals the CRC, so the genome stays wire-valid.
  gene_set_mutations(p.genome, (uint8_t)((mc > relief) ? (mc - relief) : 0u));
}

static void set_minor_form(uint8_t stage)
{
  uint8_t variant = (g_pet->cq >= 550) ? 0u : 1u;   // "bueno" / "descuidado"
  if (stage == STAGE_CHILD) {
    g_pet->minor_form = (uint8_t)((g_pet->minor_form & 0xF0u) | variant);
  } else if (stage == STAGE_TEEN) {
    g_pet->minor_form = (uint8_t)((g_pet->minor_form & 0x0Fu) | (uint8_t)(variant << 4));
  }
  g_events |= SIM_EV_EVOLVE_MINOR;
}

// =============================================================================
// 7. DEATH
// =============================================================================
static uint8_t cause_from_damage(void)
{
  uint16_t best = 0;
  uint8_t  bid  = DMG_HUNGER;
  for (uint8_t i = 0; i < DMG_COUNT; ++i) {
    if (g_pet->dmg_acc[i] > best) { best = g_pet->dmg_acc[i]; bid = i; }
  }
  switch (bid) {
    case DMG_HUNGER:  return DEATH_HUNGER;
    case DMG_FILTH:   return DEATH_FILTH;
    case DMG_ILLNESS: return DEATH_ILLNESS;
    case DMG_SADNESS: return DEATH_SADNESS;
    default:          return DEATH_ACCIDENT;
  }
}

static void do_death(uint8_t cause)
{
  PetSave& p = *g_pet;
  if (p.stage == STAGE_DEAD) return;

  // Neglect overrides every organic cause when the death happened inside an
  // offline absence of >= 24 h (GAME_DESIGN 9.1).
  if (g_offline && g_absence_ctx_s >= ABSENCE_NEGLECT_DEATH_S &&
      cause != DEATH_OLD_AGE) {
    cause = DEATH_NEGLECT;
  }

  p.stat[ST_HEALTH] = 0;
  p.stat_rem[ST_HEALTH] = 0;
  p.stage        = STAGE_DEAD;
  p.death_cause  = cause;
  p.death_epoch  = g_now;
  p.flags       |= PF_DEAD;
  p.flags       &= (uint16_t)~(PF_ASLEEP | PF_WISH_ACTIVE | PF_SEEKING_MATE);
  g_alert        = AL_NONE;
  g_sulk_left_s  = 0;
  g_events      |= SIM_EV_DIED;
}

// =============================================================================
// 8. SUB-STEP - one SIM_SUBSTEP_S (or shorter) slice of simulation
// =============================================================================
static uint8_t compute_alert(void)
{
  const PetSave& p = *g_pet;
  if (p.stat[ST_HEALTH] < (int32_t)SIM_ALERT_HEALTH_PCT * 1000) return AL_LOW_HEALTH;
  if (p.flags & PF_SICK)                                        return AL_SICK;
  if (p.stat[ST_HUNGER]    < (int32_t)CARE_MISS_ALERT_PCT * 1000) return AL_HUNGRY;
  if (p.stat[ST_HYGIENE]   < (int32_t)CARE_MISS_ALERT_PCT * 1000) return AL_DIRTY;
  if (p.stat[ST_ENERGY]    < (int32_t)CARE_MISS_ALERT_PCT * 1000) return AL_TIRED;
  if (p.stat[ST_HAPPINESS] < (int32_t)CARE_MISS_ALERT_PCT * 1000) return AL_SAD;
  if (p.poop_count >= 3)                                          return AL_POOP;
  if (p.flags & PF_WISH_ACTIVE)                                   return AL_WISH;
  return AL_NONE;
}

static void sleep_machine(uint32_t dt)
{
  PetSave& p = *g_pet;
  uint8_t asleep = (p.flags & PF_ASLEEP) ? 1u : 0u;
  uint8_t light  = (p.flags & PF_LIGHT_ON) ? 1u : 0u;

  uint8_t night = 0;
  if (g_env.clock_valid) {
    uint32_t h = g_sod / 3600u;
    night = (h >= (uint32_t)SLEEP_HOUR_START || h < (uint32_t)SLEEP_HOUR_END) ? 1u : 0u;
  }

  if (!asleep) {
    // Auto-sleep only at night with the light off. Energy hitting zero is a
    // COLLAPSE, not sleep: GAME_DESIGN 1.5 keeps the -1.5/h energy damage
    // running from 11.11 h all the way to death at 19 h, which a regenerating
    // sleep would cancel. Collapse is a render state, not a stat state.
    if (night && !light) {
      p.flags |= PF_ASLEEP;
      g_events |= SIM_EV_SLEEP;
    }
  } else {
    uint8_t wake = 0;
    if (p.stat[ST_ENERGY] >= STAT_MILLI_MAX) wake = 1;
    if (!night && pct_milli(ST_ENERGY) >= SIM_WAKE_DAY_ENERGY_PCT) wake = 1;
    if (wake) {
      p.flags &= (uint16_t)~PF_ASLEEP;
      g_events |= SIM_EV_WAKE;
    } else if (light) {
      // Sleeping with the light on: +1 care_miss per hour (GAME_DESIGN 1.6)
      g_lightsleep_rem += (int32_t)dt;
      while (g_lightsleep_rem >= SEC_PER_HOUR) {
        g_lightsleep_rem -= SEC_PER_HOUR;
        if (p.care_miss < 0xFFFFu) p.care_miss++;
        cq_add(CQ_D_CARE_MISS);
        g_events |= SIM_EV_CARE_MISS;
      }
    }
  }
}

static void decay_stats(uint32_t dt)
{
  PetSave& p = *g_pet;
  const FormMods* fm = form_of();
  const Genome& g = p.genome;

  uint16_t m_stage   = STAGE_MULT[p.stage];
  uint16_t m_app     = gene_appetite_mult(g);
  uint16_t m_met     = gene_metabolism_mult(g);
  uint16_t m_soc     = gene_sociability_mult(g);
  uint16_t m_sleep   = (p.flags & PF_ASLEEP) ? (uint16_t)MULT_SLEEP : (uint16_t)MULT_ONE;
  uint16_t m_off     = g_offline ? (uint16_t)MULT_OFFLINE_DECAY : (uint16_t)MULT_ONE;
  uint16_t m_lonely  = (uint16_t)MULT_ONE;
  // PH3 #3: last_interact_epoch can legitimately sit AHEAD of g_now - the
  // catch-up loop winds g_now back to the start of the absence, and a
  // clock-less reboot restarts the estimated clock near 0 while the loaded
  // save still carries the previous boot's uptime-valued timestamps. An
  // unsigned subtraction there yields ~4.29e9, which is >= LONELY_AFTER_S,
  // so the x1.5 loneliness penalty would be charged for the whole catch-up.
  // Clamp the idle time at 0 instead (every other epoch delta in the tree
  // already does this).
  const uint32_t idle_s = (g_now > p.last_interact_epoch)
                            ? (g_now - p.last_interact_epoch) : 0u;
  if (idle_s >= LONELY_AFTER_S) m_lonely = (uint16_t)MULT_LONELY;

  // --- hunger (satiety) ---
  {
    const uint16_t m[5] = { m_stage, m_app, m_sleep, fm->hun, m_off };
    accum_stat(ST_HUNGER, rate_chain(RATE_HUNGER_MPH, m, 5), dt);
  }
  // --- happiness ---
  {
    const uint16_t m[6] = { m_stage, m_lonely, m_soc, m_sleep, fm->hap, m_off };
    accum_stat(ST_HAPPINESS, rate_chain(RATE_HAPPINESS_MPH, m, 6), dt);
  }
  // --- energy ---
  if (p.flags & PF_ASLEEP) {
    uint16_t m_light = (p.flags & PF_LIGHT_ON)
                         ? (uint16_t)MULT_LIGHT_ON_SLEEP : (uint16_t)MULT_ONE;
    const uint16_t m[2] = { m_light, m_off };
    accum_stat(ST_ENERGY, rate_chain(RATE_ENERGY_ASLEEP_MPH, m, 2), dt);
  } else {
    const uint16_t m[4] = { m_stage, m_met, fm->nrg, m_off };
    accum_stat(ST_ENERGY, rate_chain(RATE_ENERGY_AWAKE_MPH, m, 4), dt);
  }
  // --- hygiene (base + per-poop) ---
  {
    int32_t base = RATE_HYGIENE_MPH
                 + (int32_t)p.poop_count * RATE_HYGIENE_POOP_MPH;
    const uint16_t m[4] = { m_stage, m_sleep, fm->hyg, m_off };
    accum_stat(ST_HYGIENE, rate_chain(base, m, 4), dt);
  }
  // --- bond (flat, doubled while the grudge is still running) ---
  //     Gated on the sulk timer, not on absence_tier: the tier stays in the
  //     save for the UI and the lineage, but the x2 bond bleed has to stop
  //     once the pet has been forgiven, or it never recovers.
  {
    uint16_t m_abs = (g_sulk_left_s > 0)
                       ? (uint16_t)BOND_ABSENCE_DECAY_MULT : (uint16_t)MULT_ONE;
    const uint16_t m[2] = { m_abs, m_off };
    accum_stat(ST_BOND, rate_chain(RATE_BOND_MPH, m, 2), dt);
  }
  // --- discipline (flat) ---
  {
    const uint16_t m[2] = { fm->disc, m_off };
    accum_stat(ST_DISCIPLINE, rate_chain(RATE_DISCIPLINE_MPH, m, 2), dt);
  }
  // --- weight, milli-decigrams per hour ---
  {
    const uint16_t m[2] = { m_met, m_off };
    int32_t rate = rate_chain(RATE_WEIGHT_DG_MPH, m, 2);
    // Two-level ledger, same no-drift contract as the stats: g_weight_hrem
    // carries the /3600 remainder, g_weight_frac the sub-decigram part.
    int32_t w = (int32_t)p.weight_dg * 1000 + g_weight_frac;
    accum(w, g_weight_hrem, rate, dt, (int32_t)WEIGHT_DG_MIN * 1000,
                                      (int32_t)WEIGHT_DG_MAX * 1000);
    int32_t dg = w / 1000;
    int32_t fr = w - dg * 1000;
    if (fr < 0) { dg -= 1; fr += 1000; }
    p.weight_dg   = (int16_t)dg;
    g_weight_frac = fr;
  }
}

static void health_step(uint32_t dt)
{
  PetSave& p = *g_pet;
  const FormMods* fm = form_of();
  uint16_t m_hardy = gene_hardiness_mult(p.genome);
  uint16_t m_off   = g_offline ? (uint16_t)MULT_OFFLINE_DECAY : (uint16_t)MULT_ONE;

  // ---- damage, per source, so the death cause can be attributed -----------
  int32_t src[DMG_COUNT];
  memset(src, 0, sizeof(src));

  if (p.stat[ST_HUNGER]    <= 0) src[DMG_HUNGER]  += DMG_HUNGER_ZERO_MPH;
  if (p.stat[ST_HYGIENE]   <= 0) src[DMG_FILTH]   += DMG_HYGIENE_ZERO_MPH;
  if (p.stat[ST_HAPPINESS] <= 0) src[DMG_SADNESS] += DMG_HAPPINESS_ZERO_MPH;
  if (p.stat[ST_ENERGY]    <= 0) src[DMG_SADNESS] += DMG_ENERGY_ZERO_MPH;
  if (p.flags & PF_SICK)         src[DMG_ILLNESS] += DMG_SICK_MPH;
  if (p.weight_dg > OBESE_WEIGHT_DG) {
    src[DMG_OTHER] += DMG_OBESE_MPH;
    g_events |= SIM_EV_WEIGHT_OBESE;
  }
  const uint16_t md[3] = { m_hardy, fm->dmg, m_off };
  int32_t total = 0;
  for (uint8_t i = 0; i < DMG_COUNT; ++i) {
    if (src[i] == 0) continue;
    int32_t r = rate_chain(src[i], md, 3);
    total += r;

    // Per-source lifetime accumulator in WHOLE points (uint16, saturating).
    int32_t whole = 0;
    accum(whole, g_dmg_rem[i], r, dt, -1000000, 1000000);
    if (whole > 0) {
      uint32_t v = (uint32_t)p.dmg_acc[i] + (uint32_t)whole;
      p.dmg_acc[i] = (v > 65535u) ? 65535u : (uint16_t)v;
    }
  }

  // ---- regeneration -------------------------------------------------------
  int32_t regen = 0;
  if (total == 0 && !(p.flags & PF_SICK)) {
    uint8_t ok = 1;
    for (uint8_t i = 0; i < ST_CORE_COUNT; ++i) {
      if (pct_milli(i) < HEALTH_REGEN_MIN_PCT) { ok = 0; break; }
    }
    if (ok) {
      uint16_t m_sen = (p.stage == STAGE_SENIOR)
                         ? (uint16_t)SENIOR_REGEN_MULT : (uint16_t)MULT_ONE;
      const uint16_t mr[3] = { fm->regen, m_sen, m_off };
      regen = rate_chain(RATE_HEALTH_REGEN_MPH, mr, 3);
    }
  }

  accum_stat(ST_HEALTH, regen - total, dt);

  // ---- senior health ceiling ---------------------------------------------
  if (p.stage == STAGE_SENIOR) {
    int32_t age_h = (int32_t)(p.age_s / 3600u);
    int32_t maxh  = 100 - ((age_h - 168) / 4);
    maxh = NT_CLAMP(maxh, (int32_t)SENIOR_MAXHEALTH_MIN, (int32_t)100);
    if (p.stat[ST_HEALTH] > maxh * 1000) {
      p.stat[ST_HEALTH] = maxh * 1000;
      p.stat_rem[ST_HEALTH] = 0;
    }
  }
}

static void poop_step(uint32_t dt)
{
  PetSave& p = *g_pet;
  uint32_t base_min = g_offline ? (uint32_t)POOP_OFFLINE_MIN : (uint32_t)POOP_FIRST_MIN;
  uint32_t met = gene_metabolism_mult(p.genome);
  if (met == 0) met = MULT_ONE;
  uint32_t period = (base_min * 60u * 1000u) / met;      // seconds
  if (period < 60u) period = 60u;

  // A sleeping pet digests at the same x0.35 the rest of its hygiene budget
  // runs at. Without this, an 8 h night produces 5 poops and GAME_DESIGN 1.5's
  // "overnight is free, sleeping the pet before bed is a real strategy" is
  // simply false: the hygiene collapse alone would cost ~7 h of decay.
  uint32_t adv = dt;
  if (p.flags & PF_ASLEEP) adv = (dt * (uint32_t)MULT_SLEEP) / 1000u;

  g_poop_timer += adv;
  while (g_poop_timer >= period) {
    g_poop_timer -= period;
    if (p.poop_count < POOP_MAX) {
      p.poop_count++;
      g_misbehave = 1;
      g_misbehave_s = g_uptime_s;
      g_events |= SIM_EV_POOP;
    } else {
      // A 5th poop with nowhere to go raises the sickness risk instead.
      g_suppress_until_s = g_uptime_s + SICK_OVERFEED_WINDOW_S;
    }
  }
}

static void sickness_step(uint32_t dt)
{
  PetSave& p = *g_pet;
  g_acc_sick += dt;
  while (g_acc_sick >= (uint32_t)SICK_ROLL_PERIOD_S) {
    g_acc_sick -= (uint32_t)SICK_ROLL_PERIOD_S;

    if (p.flags & PF_SICK) {
      // Untreated illness compounds into the DEATH_ILLNESS attribution.
      if (g_sick_hours < 255) g_sick_hours++;
      continue;
    }

    int32_t pph = SICK_BASE_PPH
                + (int32_t)p.poop_count * SICK_PER_POOP_PPH
                + ((pct_milli(ST_HUNGER) < 15) ? SICK_HUNGRY_PPH : 0)
                + ((pct_milli(ST_HEALTH) < 50) ? SICK_LOWHEALTH_PPH : 0);
    if (g_uptime_s < g_overfeed_until_s) pph += SICK_OVERFEED_PPH;
    if (g_uptime_s < g_suppress_until_s) pph += POOP_SUPPRESSED_SICK_PCT * 10;

    const FormMods* fm = form_of();
    uint16_t m_inb = (p.flags & PF_INBRED) ? (uint16_t)INBRED_SICK_MULT : (uint16_t)MULT_ONE;
    const uint16_t ms[3] = { gene_hardiness_mult(p.genome), fm->sick, m_inb };
    pph = rate_chain(pph, ms, 3);
    if (pph < 0) pph = 0;

    // p_hour / 6 evaluated every 10 minutes, in permille.
    if ((int32_t)rnd_below(6000u) < pph) {
      p.flags |= PF_SICK;
      if (p.sick_episodes < 0xFFFFu) p.sick_episodes++;
      cq_add(CQ_D_SICK_EPISODE);
      g_sick_hours = 0;
      g_events |= SIM_EV_SICK_START;
    }
  }
}

static void cq_step(uint32_t dt)
{
  PetSave& p = *g_pet;
  g_acc_cq += dt;
  while (g_acc_cq >= (uint32_t)CQ_PERIOD_S) {
    g_acc_cq -= (uint32_t)CQ_PERIOD_S;

    uint8_t all_good = 1, any_zero = 0;
    for (uint8_t i = 0; i < ST_CORE_COUNT; ++i) {
      if (pct_milli(i) < CQ_GOOD_STATS_PCT) all_good = 0;
      if (p.stat[i] <= 0) any_zero = 1;
    }
    if (all_good && g_cq_good_today < CQ_GOOD_DAILY_CAP) {
      cq_add(CQ_D_GOOD);
      g_cq_good_today++;
    }
    if (any_zero) cq_add(CQ_D_ZEROSTAT);
  }
}

static void wish_step(uint32_t dt)
{
  PetSave& p = *g_pet;
  if (!g_env.clock_valid || g_offline) return;
  if (p.stage < STAGE_CHILD || p.stage >= STAGE_DEAD) return;

  if (p.flags & PF_WISH_ACTIVE) {
    uint32_t left = p.wish_left_s;
    if (dt >= left) {
      p.wish_left_s = 0;
      p.flags &= (uint16_t)~PF_WISH_ACTIVE;
      p.flags |= PF_WISH_DONE;
      if (p.care_miss < 0xFFFFu) p.care_miss++;
      cq_add(CQ_D_WISH_FAIL);
      g_events |= SIM_EV_WISH_FAIL;
    } else {
      p.wish_left_s = (uint16_t)(left - dt);
    }
    return;
  }
  if (p.flags & PF_WISH_DONE) return;

  uint32_t hour = g_sod / 3600u;
  uint32_t span = (uint32_t)(WISH_HOUR_MAX - WISH_HOUR_MIN + 1);
  uint32_t seed = p.genome.lineage_id ^ (uint32_t)today_stamp();
  seed ^= seed >> 13; seed *= 0x9E3779B1u; seed ^= seed >> 15;
  uint32_t wish_hour = (uint32_t)WISH_HOUR_MIN + (seed % span);

  if (hour == wish_hour) {
    p.wish_id      = (uint8_t)(WISH_PLAY + (seed >> 8) % (uint32_t)(WISH_COUNT - 1));
    p.wish_left_s  = (uint16_t)NT_MIN((uint32_t)WISH_WINDOW_S, (uint32_t)65535u);
    p.flags       |= PF_WISH_ACTIVE;
    g_wish_pets    = 0;
    g_events      |= SIM_EV_WISH_START;
  }
}

static void events_step(uint32_t dt)
{
  PetSave& p = *g_pet;
  uint32_t age_h = p.age_s / 3600u;

  if (!(p.events_done & EV_VISITA) && age_h >= (uint32_t)EVENT_VISITA_H) {
    p.events_done |= EV_VISITA;
    if (gene_sociability(p.genome) >= EVENT_VISITA_SOC_MIN) {
      stat_add(ST_HAPPINESS, EVENT_VISITA_HAPPINESS);
    }
    g_events |= SIM_EV_VISITA;
  }

  if (!(p.events_done & EV_STORM) && age_h >= (uint32_t)EVENT_STORM_H) {
    p.events_done |= EV_STORM;
    g_storm_left_s = EVENT_STORM_DUR_S;
    g_storm_pets   = 0;
    g_events |= SIM_EV_STORM;
  }
  if (g_storm_left_s > 0) {
    if (dt >= g_storm_left_s) {
      g_storm_left_s = 0;
      if (g_storm_pets < EVENT_STORM_PETS_NEEDED) {
        stat_add(ST_HEALTH, EVENT_STORM_HEALTH);
        uint32_t v = (uint32_t)p.dmg_acc[DMG_OTHER] + 8u;
        p.dmg_acc[DMG_OTHER] = (v > 65535u) ? 65535u : (uint16_t)v;
        g_events |= SIM_EV_STORM_HURT;
      }
    } else {
      g_storm_left_s -= dt;
    }
  }

  uint32_t bdays_due = age_h / (uint32_t)EVENT_BIRTHDAY_H;
  uint32_t bdays_had = (uint32_t)((p.events_done >> EV_BIRTHDAY_SH) & EV_BIRTHDAY_MK);
  if (bdays_due > bdays_had && bdays_had < EV_BIRTHDAY_MK) {
    bdays_had++;
    p.events_done = (uint8_t)((p.events_done & ~(EV_BIRTHDAY_MK << EV_BIRTHDAY_SH))
                              | (uint8_t)(bdays_had << EV_BIRTHDAY_SH));
    stat_add(ST_HAPPINESS, EVENT_BIRTHDAY_HAPPY);
    g_events |= SIM_EV_BIRTHDAY;
  }
}

static void stage_step(uint32_t dt)
{
  PetSave& p = *g_pet;
  g_acc_stage += dt;
  if (g_acc_stage < (uint32_t)STAGE_CHECK_PERIOD_S) return;
  g_acc_stage = 0;

  while (p.stage < STAGE_SENIOR && p.age_s >= stage_enter_s((uint8_t)(p.stage + 1))) {
    p.stage = (uint8_t)(p.stage + 1);
    g_events |= SIM_EV_STAGE_UP;
    if (p.stage == STAGE_CHILD || p.stage == STAGE_TEEN) {
      set_minor_form(p.stage);
    } else if (p.stage == STAGE_ADULT) {
      p.adult_form = pick_adult_form();
      mutation_load_relief();     // PH3 #6, after the form is decided
      g_events |= SIM_EV_ADULT_FORM;
    }
    // Per-stage counters snapshot then reset (GAME_DESIGN 2.1).
    p.care_miss     = 0;
    p.sick_episodes = 0;
  }

  if (p.age_s >= sim_natural_death_s()) {
    do_death(DEATH_OLD_AGE);
    return;
  }

  // Accident: 0.15 %/day after ADULT, only with a poor care record.
  if (p.stage >= STAGE_ADULT && p.cq < ACCIDENT_CQ_MAX) {
    uint32_t p_ppm = ((uint32_t)ACCIDENT_PPM_PER_DAY * (uint32_t)STAGE_CHECK_PERIOD_S)
                     / 86400u;
    if (p_ppm > 0 && rnd_below(1000000u) < p_ppm) {
      uint32_t v = (uint32_t)p.dmg_acc[DMG_OTHER] + 100u;
      p.dmg_acc[DMG_OTHER] = (v > 65535u) ? 65535u : (uint16_t)v;
      do_death(DEATH_ACCIDENT);
    }
  }
}

static void alert_step(uint32_t dt)
{
  PetSave& p = *g_pet;
  uint8_t a = compute_alert();

  if (a == AL_NONE) {
    if (g_alert != AL_NONE) g_events |= SIM_EV_ALERT;
    g_alert = AL_NONE;
    g_alert_age_s = 0;
  } else if (a != g_alert) {
    g_alert = a;
    g_alert_age_s = 0;
    g_events |= SIM_EV_ALERT;
  } else {
    g_alert_age_s += dt;
    if (g_alert_age_s >= CARE_MISS_GRACE_S &&
        g_since_miss_s >= CARE_MISS_MIN_GAP_S) {
      if (p.care_miss < 0xFFFFu) p.care_miss++;
      cq_add(CQ_D_CARE_MISS);
      g_since_miss_s = 0;
      g_alert_age_s  = 0;
      g_events |= SIM_EV_CARE_MISS;
    }
  }
  g_since_miss_s += dt;
}

static void hatch_now(void)
{
  PetSave& p = *g_pet;
  p.stage        = STAGE_BABY;
  p.age_s        = 0;
  p.birth_epoch  = g_now;
  p.last_interact_epoch = g_now;
  p.adult_form   = FORM_UNSET;
  p.minor_form   = 0;
  p.poop_count   = 0;
  g_poop_timer   = 0;
  for (uint8_t i = 0; i < ST_COUNT; ++i) {
    p.stat[i] = STAT_MILLI_MAX;
    p.stat_rem[i] = 0;
  }
  if (p.flags & PF_COLD_EGG) {
    p.stat[ST_HEALTH] = (int32_t)EGG_COLD_HEALTH_PCT * 1000;
  }
  p.weight_dg = (int16_t)NT_CLAMP((int32_t)gene_weight_ideal_dg(p.genome),
                                  (int32_t)WEIGHT_DG_MIN, (int32_t)WEIGHT_DG_MAX);
  if (gene_tainted(p.genome)) p.flags |= PF_GOD_TAINTED;
  g_events |= SIM_EV_HATCHED | SIM_EV_STAGE_UP;
}

// One slice. dt must be <= SIM_SUBSTEP_S so the cadence grid stays aligned.
static void sub_step(uint32_t dt)
{
  PetSave& p = *g_pet;
  if (dt == 0) return;

  g_uptime_s += dt;
  g_now      += dt;
  g_sod       = (g_sod + dt) % 86400u;
  gain_refill(dt);

  if (p.stage == STAGE_DEAD) { p.last_seen_epoch = g_now; return; }

  p.last_seen_epoch = g_now;

  // Eggs are immortal and never decay: they only age toward hatching.
  if (p.stage == STAGE_EGG) {
    p.age_s += dt;
    if (p.age_s >= AGE_EGG_S) hatch_now();
    return;
  }

  p.age_s += dt;

  // grudge timer
  if (g_sulk_left_s > 0) g_sulk_left_s = (dt >= g_sulk_left_s) ? 0 : (g_sulk_left_s - dt);

  // day rollover: daily caps and the once-a-day wish
  uint16_t d = today_stamp();
  if (d != g_day_stamp) {
    g_day_stamp     = d;
    g_cq_good_today = 0;
    g_cq_game_today = 0;
    p.flags        &= (uint16_t)~PF_WISH_DONE;
  }

  sleep_machine(dt);
  decay_stats(dt);
  poop_step(dt);
  sickness_step(dt);
  health_step(dt);
  cq_step(dt);
  wish_step(dt);
  events_step(dt);
  alert_step(dt);

  // running happiness mean, for S_rebel
  g_acc_minute += dt;
  while (g_acc_minute >= 60u) {
    g_acc_minute -= 60u;
    int32_t cur = pct_milli(ST_HAPPINESS);
    int32_t acc = (int32_t)p.happiness_avg * 15 + cur + g_hapavg_rem;
    p.happiness_avg = (uint8_t)NT_CLAMP(acc / 16, (int32_t)0, (int32_t)100);
    g_hapavg_rem = acc - (int32_t)p.happiness_avg * 16;
    if (g_hapavg_rem > 15 || g_hapavg_rem < -15) g_hapavg_rem = 0;
  }

  stage_step(dt);

  if (p.stage != STAGE_DEAD && p.stat[ST_HEALTH] <= 0) {
    uint8_t cause = cause_from_damage();
    if (g_sick_hours >= 36) cause = DEATH_ILLNESS;   // untreated > 6 h
    do_death(cause);
  }
}

// =============================================================================
// 9. PUBLIC TICK / QUERIES
// =============================================================================
void sim_tick(uint32_t seconds)
{
  if (!g_pet) return;
  while (seconds > 0) {
    uint32_t dt = (seconds > SIM_SUBSTEP_S) ? SIM_SUBSTEP_S : seconds;
    sub_step(dt);
    seconds -= dt;
    if (g_pet->stage == STAGE_DEAD) {
      // keep the clock moving but stop simulating a corpse
      if (seconds > 0) { g_now += seconds; g_uptime_s += seconds;
                         g_pet->last_seen_epoch = g_now; }
      return;
    }
  }
}

uint8_t sim_stat_pct(StatId id)
{
  if (!g_pet || id >= ST_COUNT) return 0;
  int32_t v = g_pet->stat[id] / 1000;
  return (uint8_t)NT_CLAMP(v, (int32_t)0, (int32_t)100);
}

uint8_t sim_mood_score(void)
{
  if (!g_pet) return 0;
  int32_t s = (40 * pct_milli(ST_HAPPINESS)
             + 25 * pct_milli(ST_HEALTH)
             + 20 * pct_milli(ST_BOND)
             + 15 * pct_milli(ST_HUNGER)) / 100;
  return (uint8_t)NT_CLAMP(s, (int32_t)0, (int32_t)100);
}

const PetSave* sim_save(void)        { return g_pet; }
uint32_t sim_take_events(void)       { uint32_t e = g_events; g_events = 0; return e; }
uint8_t  sim_alert(void)             { return g_alert; }
uint16_t sim_sulk_left_s(void)       { return (uint16_t)NT_MIN(g_sulk_left_s, 65535u); }
uint8_t  sim_is_asleep(void)         { return (g_pet && (g_pet->flags & PF_ASLEEP)) ? 1u : 0u; }
uint8_t  sim_is_sick(void)           { return (g_pet && (g_pet->flags & PF_SICK)) ? 1u : 0u; }
uint8_t  sim_is_dead(void)           { return (g_pet && g_pet->stage == STAGE_DEAD) ? 1u : 0u; }
uint32_t sim_age_s(void)             { return g_pet ? g_pet->age_s : 0u; }
uint32_t sim_now(void)               { return g_now; }

// =============================================================================
// 10. LIFE CYCLE / INIT
// =============================================================================
// PH3 #4. `fresh` distinguishes the two callers:
//   1 - sim_new_pet(): a creature that has never been interacted with. Every
//       ledger starts empty, i.e. the full hourly budget and no cooldowns.
//   0 - sim_init(): a save just came back from NVS. We have no record of how
//       much of the budget the previous boot had already spent, so we must
//       assume the worst - budget exhausted, every cooldown just started -
//       and let the boot's catch-up refill it from the real elapsed time
//       (gain_refill() runs inside sub_step(), and g_uptime_s advances there
//       too). Refilling to `cap` here instead is what made a reboot restore
//       the whole hourly ceiling for free.
//       PH4 6.1: the caller is expected to follow sim_init() with
//       sim_gain_restore(), which replaces this zero with the budget that was
//       actually left at the last save. The zero remains the fallback whenever
//       there is no trustworthy snapshot, so this function stays safe on its
//       own and nothing downstream depends on the restore having happened.
static void reset_ram_state(uint8_t fresh)
{
  g_events = 0;
  g_acc_stage = g_acc_cq = g_acc_sick = g_acc_minute = 0;
  g_poop_timer = 0;
  g_weight_frac = 0;
  g_weight_hrem = 0;
  memset(g_dmg_rem, 0, sizeof(g_dmg_rem));
  g_hapavg_rem = 0;
  g_lightsleep_rem = 0;
  g_alert = AL_NONE;
  g_alert_age_s = 0;
  g_since_miss_s = CARE_MISS_MIN_GAP_S;
  g_sulk_left_s = 0;
  g_absence_ctx_s = 0;
  // Cooldowns: "seen at g_uptime_s == 0". On a reload that means the residual
  // cooldown is (cd - elapsed), clamped at 0 by sim_action_cooldown_s(), so a
  // reboot buys nothing and costs at most ACT_CD_MED_S (30 s) of friction.
  memset(g_act_last, 0, sizeof(g_act_last));
  memset(g_act_seen, fresh ? 0 : 1, sizeof(g_act_seen));
  g_last_any_act_s = 0;
  g_any_act_seen = fresh ? 0u : 1u;
  for (uint8_t i = 0; i < ST_COUNT; ++i) {
    int32_t cap = gain_cap_milli(i);
    g_gain_budget[i] = (fresh && cap > 0) ? cap : 0;
    g_gain_rem[i] = 0;
  }
  memset(g_play_at, 0, sizeof(g_play_at));
  g_play_n = 0;
  memset(g_pet_at, 0, sizeof(g_pet_at));
  g_pet_n = 0;
  g_mg_last_s = 0;
  g_mg_seen = fresh ? 0u : 1u;
  g_force_feed = 0;
  g_misbehave_s = 0;
  g_misbehave = 0;
  g_overfeed_until_s = 0;
  g_suppress_until_s = 0;
  g_cq_good_today = 0;
  g_cq_game_today = 0;
  g_day_stamp = 0xFFFFu;
  g_storm_left_s = 0;
  g_storm_pets = 0;
  g_wish_pets = 0;
  g_sick_hours = 0;
  g_uptime_s = 0;
  g_offline = 0;
}

void sim_init(PetSave& save)
{
  g_pet = &save;
  sim_env_defaults(g_env);
  reset_ram_state(0);           // PH3 #4: a reload re-earns its ledgers

  PetSave& p = save;
  for (uint8_t i = 0; i < ST_COUNT; ++i) {
    p.stat[i]     = NT_CLAMP(p.stat[i], STAT_MILLI_MIN, STAT_MILLI_MAX);
    p.stat_rem[i] = (int16_t)NT_CLAMP((int32_t)p.stat_rem[i], (int32_t)-3599, (int32_t)3599);
  }
  p.cq        = (int16_t)NT_CLAMP((int32_t)p.cq, (int32_t)CQ_MIN, (int32_t)CQ_MAX);
  p.weight_dg = (int16_t)NT_CLAMP((int32_t)p.weight_dg,
                                  (int32_t)WEIGHT_DG_MIN, (int32_t)WEIGHT_DG_MAX);
  if (p.poop_count > POOP_MAX) p.poop_count = POOP_MAX;
  if (p.stage >= STAGE_COUNT) p.stage = STAGE_EGG;
  if (p.guilt_level < 0) p.guilt_level = 0;
  if (p.guilt_level > 6) p.guilt_level = 6;
  if (p.stage == STAGE_DEAD) p.flags |= PF_DEAD;

  g_now = (p.last_seen_epoch != 0) ? p.last_seen_epoch : 0u;
  g_sod = 0;
}

void sim_new_pet(const Genome& g, uint32_t now_epoch, uint8_t cold)
{
  if (!g_pet) return;
  PetSave& p = *g_pet;
  memset(&p, 0, sizeof(p));

  p.magic   = NT_SAVE_MAGIC;
  p.version = NT_SAVE_VERSION;
  p.stage   = STAGE_EGG;
  p.genome  = g;

  for (uint8_t i = 0; i < ST_COUNT; ++i) p.stat[i] = STAT_MILLI_MAX;
  p.birth_epoch         = now_epoch;
  p.last_seen_epoch     = now_epoch;
  p.egg_epoch           = now_epoch;
  p.last_interact_epoch = now_epoch;
  p.age_s               = 0;
  p.cq                  = CQ_START;
  p.weight_dg           = (int16_t)NT_CLAMP((int32_t)gene_weight_ideal_dg(g),
                                            (int32_t)WEIGHT_DG_MIN,
                                            (int32_t)WEIGHT_DG_MAX);
  p.adult_form          = FORM_UNSET;
  p.absence_tier        = ABS_NONE;
  p.death_cause         = DEATH_NONE;
  p.flags               = PF_LIGHT_ON;
  if (cold) p.flags |= PF_COLD_EGG;
  if (gene_tainted(g)) p.flags |= PF_GOD_TAINTED;

  reset_ram_state(1);           // brand new creature: full budget, no cooldowns
  g_now = now_epoch;
}

void sim_hatch(void)
{
  if (!g_pet || g_pet->stage != STAGE_EGG) return;
  hatch_now();
}

void sim_bury(void)
{
  if (!g_pet || g_pet->stage != STAGE_DEAD) return;
  g_pet->flags |= PF_BURIED;
}

// =============================================================================
// 11. ACTIONS
// =============================================================================
static void result_begin(ActionResult& out, int32_t* snap)
{
  memset(&out, 0, sizeof(out));
  for (uint8_t i = 0; i < ST_COUNT; ++i) snap[i] = g_pet->stat[i];
}

static void result_end(ActionResult& out, const int32_t* snap,
                       int32_t w_before, int16_t cq_before, uint16_t str_id)
{
  for (uint8_t i = 0; i < ST_COUNT; ++i) {
    out.d[i] = (int16_t)((g_pet->stat[i] - snap[i]) / 1000);
  }
  out.d_weight_dg = (int16_t)((int32_t)g_pet->weight_dg - w_before);
  out.d_cq        = (int16_t)(g_pet->cq - cq_before);
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

static void note_interaction(uint8_t action)
{
  PetSave& p = *g_pet;
  g_act_last[action] = g_uptime_s;
  g_act_seen[action] = 1;
  g_last_any_act_s   = g_uptime_s;
  g_any_act_seen     = 1;

  // GAME_DESIGN 7.1: only a MEANINGFUL interaction (feed / clean / play /
  // medicine / mimo) resets the guilt ladder and the loneliness clock.
  // Flipping the light or the sleep switch is housekeeping, not company.
  if (action == ACT_LIGHT_TOGGLE || action == ACT_SLEEP_TOGGLE) return;

  p.last_interact_epoch = g_now;
  p.guilt_level = 0;
  g_alert = AL_NONE;               // any real interaction addresses the alert
  g_alert_age_s = 0;
}

// Wish satisfaction, checked after a successful action.
static void wish_check(uint8_t action)
{
  PetSave& p = *g_pet;
  if (!(p.flags & PF_WISH_ACTIVE)) return;

  uint8_t hit = 0;
  switch (p.wish_id) {
    case WISH_PLAY:  hit = (action == ACT_PLAY); break;
    case WISH_SNACK: hit = (action == ACT_FEED_SNACK); break;
    case WISH_CLEAN: hit = (action == ACT_CLEAN && p.poop_count == 0 &&
                            pct_milli(ST_HYGIENE) >= 80); break;
    case WISH_PET3:  if (action == ACT_PET) { if (g_wish_pets < 255) g_wish_pets++; }
                     hit = (g_wish_pets >= 3); break;
    default: break;
  }
  if (!hit) return;

  p.flags &= (uint16_t)~PF_WISH_ACTIVE;
  p.flags |= PF_WISH_DONE;
  p.wish_left_s = 0;
  cq_add(CQ_D_WISH_OK);
  stat_add(ST_HAPPINESS, WISH_HAPPINESS);
  stat_add(ST_BOND, WISH_BOND);
  g_events |= SIM_EV_WISH_OK;
}

bool sim_apply_action(ActionId action, ActionResult& out)
{
  if (!g_pet)                 { fail(out, AERR_BAD_ARG, 0); return false; }
  if (action == ACT_NONE || action >= ACT_COUNT) { fail(out, AERR_BAD_ARG, 0); return false; }

  PetSave& p = *g_pet;
  if (p.stage == STAGE_DEAD)  { fail(out, AERR_DEAD, 0);   return false; }
  if (p.stage == STAGE_EGG)   { fail(out, AERR_IS_EGG, 0); return false; }

  // The grudge: only a mimo gets through, and it buys forgiveness.
  if (g_sulk_left_s > 0 && action != ACT_PET) {
    fail(out, AERR_SULKING, (uint16_t)NT_MIN(g_sulk_left_s, 65535u));
    return false;
  }
  if ((p.flags & PF_ASLEEP) &&
      action != ACT_SLEEP_TOGGLE && action != ACT_LIGHT_TOGGLE) {
    fail(out, AERR_ASLEEP, 0);
    return false;
  }

  uint16_t cd = sim_action_cooldown_s(action);
  if (cd > 0) { fail(out, AERR_COOLDOWN, cd); return false; }

  // PUNKI refuses one command in six (GAME_DESIGN 2.3).
  if (p.stage >= STAGE_ADULT && p.adult_form == FORM_PUNKI &&
      action != ACT_LIGHT_TOGGLE && action != ACT_SLEEP_TOGGLE &&
      rnd_below(6u) == 0u) {
    g_misbehave = 1;
    g_misbehave_s = g_uptime_s;
    fail(out, AERR_REFUSED, 0);
    return false;
  }

  int32_t snap[ST_COUNT];
  int32_t w_before  = g_pet->weight_dg;
  int16_t cq_before = g_pet->cq;
  uint16_t str_id   = 0;
  result_begin(out, snap);

  switch (action) {

    case ACT_FEED_MEAL: {
      if (pct_milli(ST_HUNGER) > ACT_MEAL_REFUSE_PCT) {
        g_force_feed++;
        if (g_force_feed >= ACT_FORCE_FEED_LIMIT) {
          g_force_feed = 0;
          stat_add(ST_BOND, ACT_FORCE_FEED_BOND);
        }
        fail(out, AERR_FULL, 0);
        return false;
      }
      // The hourly satiety budget is the real anti-farm ceiling. A meal that
      // cannot deliver any satiety is refused outright instead of silently
      // fattening the pet for nothing.
      if (sim_gain_left(ST_HUNGER) == 0) { fail(out, AERR_FULL, 0); return false; }
      g_force_feed = 0;
      int16_t got = stat_add(ST_HUNGER, ACT_MEAL_HUNGER);
      // Weight follows the calories that actually landed, not the button press.
      weight_add(((int32_t)ACT_MEAL_WEIGHT_DG * (int32_t)got) / ACT_MEAL_HUNGER);
      cq_add(ACT_MEAL_CQ);
      g_poop_timer = 0;                       // next poop 90 min after the meal
      str_id = STR_RX_MEAL;
      break;
    }

    case ACT_FEED_SNACK: {
      uint8_t over = (pct_milli(ST_HUNGER) > ACT_SNACK_OVERFEED_PCT) ? 1u : 0u;
      stat_add(ST_HUNGER, ACT_SNACK_HUNGER);
      stat_add(ST_HAPPINESS, ACT_SNACK_HAPPINESS);
      weight_add(ACT_SNACK_WEIGHT_DG);
      if (p.snacks_total < 0xFFFFu) p.snacks_total++;
      str_id = STR_RX_SNACK;
      if (over) {
        if (p.overfeed < 0xFFFFu) p.overfeed++;
        if ((p.overfeed % OVERFEED_TRIGGER) == 0u) {
          g_overfeed_until_s = g_uptime_s + SICK_OVERFEED_WINDOW_S;
          g_events |= SIM_EV_OVERFED;
          str_id = STR_RX_OVERFED;
        }
      }
      break;
    }

    case ACT_CLEAN: {
      if (p.poop_count == 0 && pct_milli(ST_HYGIENE) >= 95) {
        fail(out, AERR_NOTHING_TODO, 0);
        return false;
      }
      if (p.poop_count > 0) p.poop_count--;
      stat_add(ST_HYGIENE, ACT_CLEAN_HYGIENE);
      str_id = STR_RX_CLEAN;
      break;
    }

    case ACT_MEDICINE: {
      if (!(p.flags & PF_SICK)) { fail(out, AERR_NOT_SICK, 0); return false; }
      uint8_t second = (pct_milli(ST_HEALTH) < 25 &&
                        rnd_below(100u) < (uint32_t)ACT_MED_SECOND_DOSE_PCT) ? 1u : 0u;
      if (!second) {
        p.flags &= (uint16_t)~PF_SICK;
        g_sick_hours = 0;
        g_events |= SIM_EV_SICK_END;
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
      uint16_t cdmg = sim_minigame_cooldown_s();
      if (cdmg > 0) { fail(out, AERR_COOLDOWN, cdmg); return false; }
      int32_t gain = ((int32_t)ACT_PLAY_HAPPINESS_MAX
                      * (int32_t)sim_play_decay_permille()) / 1000;
      stat_add(ST_HAPPINESS, gain);
      stat_add(ST_ENERGY, ACT_PLAY_ENERGY);
      weight_add(ACT_PLAY_WEIGHT_DG);
      if (p.minigames_won < 0xFFFFu) p.minigames_won++;
      if (g_cq_game_today < CQ_MINIGAME_DAILY_CAP) {
        cq_add(CQ_D_MINIGAME);
        g_cq_game_today = (uint16_t)(g_cq_game_today + CQ_D_MINIGAME);
      }
      play_window_push();
      g_mg_last_s = g_uptime_s;
      g_mg_seen   = 1;
      str_id = STR_RX_PLAY;
      break;
    }

    case ACT_PET: {
      uint8_t bonus = pet_window_bonus();
      stat_add(ST_BOND, (int32_t)bonus);
      stat_add(ST_HAPPINESS, (bonus > 0) ? ACT_PET_HAPPINESS : 0);
      pet_window_push();
      if (g_sulk_left_s > 0) {
        g_sulk_left_s = (g_sulk_left_s > (uint32_t)SULK_PET_FORGIVE_S)
                          ? (g_sulk_left_s - (uint32_t)SULK_PET_FORGIVE_S) : 0u;
      }
      if (g_storm_left_s > 0 && g_storm_pets < 255) g_storm_pets++;
      str_id = STR_RX_PET;
      break;
    }

    case ACT_SCOLD: {
      uint8_t just = (g_misbehave &&
                      (g_uptime_s - g_misbehave_s) <= (uint32_t)ACT_SCOLD_WINDOW_S) ? 1u : 0u;
      if (just) {
        g_misbehave = 0;
        stat_add(ST_DISCIPLINE, ACT_SCOLD_DISCIPLINE);
        stat_add(ST_HAPPINESS, ACT_SCOLD_HAPPINESS);
        cq_add(ACT_SCOLD_CQ);
        str_id = STR_RX_SCOLD_OK;
      } else {
        stat_add(ST_BOND, ACT_SCOLD_UNJUST_BOND);
        stat_add(ST_HAPPINESS, ACT_SCOLD_UNJUST_HAP);
        cq_add(ACT_SCOLD_UNJUST_CQ);
        if (p.unjust_scolds < 255) p.unjust_scolds++;
        str_id = STR_RX_SCOLD_BAD;
      }
      break;
    }

    case ACT_LIGHT_TOGGLE: {
      p.flags ^= PF_LIGHT_ON;
      str_id = (p.flags & PF_LIGHT_ON) ? STR_RX_LIGHT_ON : STR_RX_LIGHT_OFF;
      break;
    }

    case ACT_SLEEP_TOGGLE: {
      p.flags ^= PF_ASLEEP;
      if (p.flags & PF_ASLEEP) { g_events |= SIM_EV_SLEEP; str_id = STR_RX_SLEEP; }
      else                     { g_events |= SIM_EV_WAKE;  str_id = STR_RX_WAKE;  }
      break;
    }

    default:
      fail(out, AERR_BAD_ARG, 0);
      return false;
  }

  note_interaction((uint8_t)action);
  wish_check((uint8_t)action);
  result_end(out, snap, w_before, cq_before, str_id);
  return true;
}

// =============================================================================
// 12. MINIGAMES - the on-device (S4) games and their shared ledger
// =============================================================================
static bool minigame_guard(ActionResult& out)
{
  if (!g_pet)                { fail(out, AERR_BAD_ARG, 0); return false; }
  if (g_pet->stage == STAGE_DEAD) { fail(out, AERR_DEAD, 0);   return false; }
  if (g_pet->stage == STAGE_EGG)  { fail(out, AERR_IS_EGG, 0); return false; }
  if (g_sulk_left_s > 0)     { fail(out, AERR_SULKING, (uint16_t)NT_MIN(g_sulk_left_s, 65535u)); return false; }
  if (g_pet->flags & PF_ASLEEP) { fail(out, AERR_ASLEEP, 0); return false; }
  if (pct_milli(ST_ENERGY) < ACT_PLAY_MIN_ENERGY_PCT) { fail(out, AERR_TIRED, 0); return false; }
  uint16_t cd = sim_minigame_cooldown_s();
  if (cd > 0)                { fail(out, AERR_COOLDOWN, cd); return false; }
  return true;
}

static void minigame_commit(uint8_t won)
{
  PetSave& p = *g_pet;
  if (won && p.minigames_won < 0xFFFFu) p.minigames_won++;
  if (won && g_cq_game_today < CQ_MINIGAME_DAILY_CAP) {
    cq_add(CQ_D_MINIGAME);
    g_cq_game_today = (uint16_t)(g_cq_game_today + CQ_D_MINIGAME);
  }
  play_window_push();
  g_mg_last_s = g_uptime_s;
  g_mg_seen   = 1;
  note_interaction((uint8_t)ACT_PLAY);
}

bool sim_apply_play_result(uint16_t win_permille, ActionResult& out)
{
  if (!minigame_guard(out)) return false;
  if (win_permille > 1000u) win_permille = 1000u;

  int32_t snap[ST_COUNT];
  int32_t w_before  = g_pet->weight_dg;
  int16_t cq_before = g_pet->cq;
  result_begin(out, snap);

  int32_t gain = ((int32_t)ACT_PLAY_HAPPINESS_MAX * (int32_t)win_permille) / 1000;
  gain = (gain * (int32_t)sim_play_decay_permille()) / 1000;

  stat_add(ST_HAPPINESS, gain);
  stat_add(ST_ENERGY, ACT_PLAY_ENERGY);
  weight_add(ACT_PLAY_WEIGHT_DG);

  minigame_commit((win_permille >= 500u) ? 1u : 0u);
  result_end(out, snap, w_before, cq_before,
             (win_permille >= 500u) ? (uint16_t)STR_GM_WIN : (uint16_t)STR_GM_LOSE);
  wish_check((uint8_t)ACT_PLAY);
  return true;
}

// =============================================================================
// 13. OFFLINE CATCH-UP  (GAME_DESIGN 5.2 / 5.3 / 5.4)
// =============================================================================
static uint8_t tier_for(uint32_t absence_s)
{
  if (absence_s <  ABSENCE_CORTA_S)     return ABS_NONE;
  if (absence_s <  ABSENCE_LARGA_S)     return ABS_CORTA;
  if (absence_s <  ABSENCE_ABANDONO_S)  return ABS_LARGA;
  if (absence_s <  ABSENCE_GRAVE_S)     return ABS_ABANDONO;
  return ABS_GRAVE;
}

static void apply_tier(uint8_t tier, AbsenceReport& rep)
{
  PetSave& p = *g_pet;
  int32_t dbond = 0, dhap = 0, dhea = 0, dcq = 0;
  uint16_t misses = 0, sulk = 0;

  switch (tier) {
    case ABS_CORTA:
      dbond = ABS_CORTA_BOND; dhap = ABS_CORTA_HAP; dhea = ABS_CORTA_HEA;
      break;
    case ABS_LARGA:
      dbond = ABS_LARGA_BOND; dhap = ABS_LARGA_HAP; dhea = ABS_LARGA_HEA;
      sulk = SULK_LARGA_S;
      break;
    case ABS_ABANDONO:
      dbond = ABS_ABANDONO_BOND; dhap = ABS_ABANDONO_HAP; dhea = ABS_ABANDONO_HEA;
      dcq = CQ_D_ABANDONO; misses = ABS_ABANDONO_MISSES; sulk = SULK_ABANDONO_S;
      break;
    case ABS_GRAVE:
      dbond = ABS_GRAVE_BOND; dhap = ABS_GRAVE_HAP; dhea = ABS_GRAVE_HEA;
      dcq = CQ_D_ABANDONO_GRAVE; misses = ABS_GRAVE_MISSES; sulk = SULK_GRAVE_S;
      p.flags |= PF_SCAR;                       // permanent, inherited
      break;
    default:
      break;
  }

  // Deltas are applied raw (not through the gain ledger: they are penalties)
  // and clamped at 0, never negative.
  if (dbond) { p.stat[ST_BOND]      = NT_CLAMP(p.stat[ST_BOND]      + dbond * 1000, STAT_MILLI_MIN, STAT_MILLI_MAX); }
  if (dhap)  { p.stat[ST_HAPPINESS] = NT_CLAMP(p.stat[ST_HAPPINESS] + dhap  * 1000, STAT_MILLI_MIN, STAT_MILLI_MAX); }
  if (dhea)  {
    p.stat[ST_HEALTH] = NT_CLAMP(p.stat[ST_HEALTH] + dhea * 1000, STAT_MILLI_MIN, STAT_MILLI_MAX);
    uint32_t v = (uint32_t)p.dmg_acc[DMG_OTHER] + (uint32_t)(-dhea);
    p.dmg_acc[DMG_OTHER] = (v > 65535u) ? 65535u : (uint16_t)v;
  }
  if (dcq)    cq_add(dcq);
  if (misses) {
    uint32_t v = (uint32_t)p.care_miss + misses;
    p.care_miss = (v > 65535u) ? 65535u : (uint16_t)v;
  }

  g_sulk_left_s = sulk;
  rep.sulk_s    = sulk;
  p.absence_tier = tier;
}

void sim_catch_up_ex(uint32_t absence_s, uint8_t clock_known, AbsenceReport& rep)
{
  memset(&rep, 0, sizeof(rep));
  if (!g_pet) return;
  PetSave& p = *g_pet;

  // PH3 #1 (belt and braces; the caller applies the same test with more
  // context). "No trustworthy clock" is not the same as "abandoned". A
  // computed absence of exactly 0 is positive evidence that nothing elapsed -
  // a first run, or a boot whose baseline is not a real epoch and therefore
  // cannot describe a gap at all. Charging the ABSENCE_LARGA_S floor there
  // invents a 6 h abandonment out of nothing, on every clock-less boot, and
  // the retro-fix that is supposed to correct it never arrives on a device
  // that has no radio. Treat it as the true zero instead; GAME_DESIGN 5.1's
  // floor still applies whenever there IS an unmeasurable but real gap.
  if (!clock_known && absence_s == 0u) {
    clock_known = 1u;
  }

  rep.clock_known = clock_known ? 1u : 0u;

  // Nonsense clock or no clock at all: apply AUSENCIA_LARGA as a floor and
  // flag the save so SNTP can retro-fix it later (GAME_DESIGN 5.1).
  uint8_t unknown = 0;
  if (!clock_known || absence_s > ABSENCE_MAX_S) {
    unknown   = 1;
    absence_s = ABSENCE_LARGA_S;
    p.flags  |= PF_ABS_UNKNOWN;
    rep.clock_known = 0;
  } else {
    p.flags &= (uint16_t)~PF_ABS_UNKNOWN;
  }

  rep.absence_s = absence_s;
  g_absence_ctx_s = absence_s;

  if (p.stage == STAGE_DEAD) {
    rep.tier = ABS_MUERTO;
    rep.died = 1;
    rep.cause = p.death_cause;
    rep.death_epoch = p.death_epoch;
    p.absence_tier = ABS_MUERTO;
    g_absence_ctx_s = 0;
    return;
  }

  // Wind the simulation clock back to when we last saw the player.
  uint32_t start_epoch = (g_env.now_epoch > absence_s)
                           ? (g_env.now_epoch - absence_s) : 0u;
  uint32_t sod_now = ((uint32_t)g_env.local_hour * 3600u)
                   + ((uint32_t)g_env.local_min * 60u);
  g_now = start_epoch;
  g_sod = (uint32_t)(((uint64_t)sod_now + 86400ull * 4ull
                      - (uint64_t)(absence_s % 86400u)) % 86400ull);

  uint32_t steps = absence_s / OFFLINE_STEP_S;
  uint32_t tail  = absence_s % OFFLINE_STEP_S;
  uint8_t  overrun = 0;
  if (steps > (uint32_t)OFFLINE_MAX_STEPS) {
    steps = OFFLINE_MAX_STEPS;
    tail  = 0;
    overrun = 1;                        // beyond 41.7 days: straight to death
  }

  g_offline = 1;
  uint32_t done = 0;
  for (uint32_t i = 0; i < steps && p.stage != STAGE_DEAD; ++i) {
    uint32_t left = OFFLINE_STEP_S;
    while (left > 0 && p.stage != STAGE_DEAD) {
      uint32_t dt = (left > SIM_SUBSTEP_S) ? SIM_SUBSTEP_S : left;
      sub_step(dt);
      left -= dt;
    }
    done++;
    if (p.stage == STAGE_DEAD) break;
  }
  if (p.stage != STAGE_DEAD && tail > 0) {
    uint32_t left = tail;
    while (left > 0 && p.stage != STAGE_DEAD) {
      uint32_t dt = (left > SIM_SUBSTEP_S) ? SIM_SUBSTEP_S : left;
      sub_step(dt);
      left -= dt;
    }
  }
  if (overrun && p.stage != STAGE_DEAD) {
    do_death(DEATH_NEGLECT);
  }
  g_offline = 0;

  rep.steps = (uint16_t)NT_MIN(done, 65535u);

  if (p.stage == STAGE_DEAD) {
    rep.tier  = ABS_MUERTO;
    rep.died  = 1;
    rep.cause = p.death_cause;
    // The corpse does not keep decaying: death_epoch is the instant it happened.
    rep.death_epoch = p.death_epoch;
    p.absence_tier  = ABS_MUERTO;
    // NOT PF_EGG_PENDING: that flag means a PendingEgg blob is sitting in NVS
    // key "egg" from a BLE mating. A death-egg has no blob - genome.cpp builds
    // it from this corpse and sim_new_pet() overwrites PetSave.genome in place.
    g_absence_ctx_s = 0;
    // Snap the clock forward so the UI shows "the egg has been waiting {t}".
    g_now = start_epoch + absence_s;
    p.last_seen_epoch = g_now;
    return;
  }

  // Alive: apply the escalation ladder on top of the integration.
  uint8_t tier = unknown ? (uint8_t)ABS_LARGA : tier_for(absence_s);
  if (unknown && tier < ABS_LARGA) tier = ABS_LARGA;    // never better than LARGA
  apply_tier(tier, rep);
  rep.tier = tier;

  g_now = start_epoch + absence_s;
  g_sod = sod_now;
  p.last_seen_epoch = g_now;
  g_absence_ctx_s = 0;
}

void sim_absence_retrofix(uint32_t true_absence_s)
{
  if (!g_pet) return;
  PetSave& p = *g_pet;
  if (!(p.flags & PF_ABS_UNKNOWN)) return;
  p.flags &= (uint16_t)~PF_ABS_UNKNOWN;

  uint8_t was = p.absence_tier;
  uint8_t now = tier_for(true_absence_s);
  if (now <= was) return;                 // never make it better retroactively

  // Apply only the difference between the floor already charged and the truth.
  // Indexed by AbsenceTier: NONE, CORTA, LARGA, ABANDONO, GRAVE, MUERTO, UNKNOWN
  const int32_t bond[ABS_COUNT] = { 0, ABS_CORTA_BOND, ABS_LARGA_BOND,
                                    ABS_ABANDONO_BOND, ABS_GRAVE_BOND, 0, 0 };
  const int32_t hap[ABS_COUNT]  = { 0, ABS_CORTA_HAP, ABS_LARGA_HAP,
                                    ABS_ABANDONO_HAP, ABS_GRAVE_HAP, 0, 0 };
  const int32_t hea[ABS_COUNT]  = { 0, ABS_CORTA_HEA, ABS_LARGA_HEA,
                                    ABS_ABANDONO_HEA, ABS_GRAVE_HEA, 0, 0 };

  int32_t dbond = bond[now] - bond[was];
  int32_t dhap  = hap[now]  - hap[was];
  int32_t dhea  = hea[now]  - hea[was];

  p.stat[ST_BOND]      = NT_CLAMP(p.stat[ST_BOND]      + dbond * 1000, STAT_MILLI_MIN, STAT_MILLI_MAX);
  p.stat[ST_HAPPINESS] = NT_CLAMP(p.stat[ST_HAPPINESS] + dhap  * 1000, STAT_MILLI_MIN, STAT_MILLI_MAX);
  p.stat[ST_HEALTH]    = NT_CLAMP(p.stat[ST_HEALTH]    + dhea  * 1000, STAT_MILLI_MIN, STAT_MILLI_MAX);
  if (now == ABS_GRAVE) p.flags |= PF_SCAR;
  p.absence_tier = now;
}

// =============================================================================
// 14. GOD MODE HOOKS
// =============================================================================
void sim_god_set_stat(StatId id, uint8_t pct)
{
  if (!g_pet || id >= ST_COUNT) return;
  if (pct > 100) pct = 100;
  g_pet->stat[id]     = (int32_t)pct * 1000;
  g_pet->stat_rem[id] = 0;
  g_pet->flags |= PF_GOD_TAINTED;
}

void sim_god_set_stage(uint8_t stage)
{
  if (!g_pet || stage >= STAGE_COUNT) return;
  PetSave& p = *g_pet;
  p.flags |= PF_GOD_TAINTED;

  if (stage == STAGE_DEAD) { do_death(DEATH_NONE); return; }

  p.stage  = stage;
  p.flags &= (uint16_t)~PF_DEAD;
  if (stage == STAGE_EGG) { p.age_s = 0; return; }
  p.age_s = stage_enter_s(stage);
  if (stage >= STAGE_ADULT) {
    if (p.adult_form >= FORM_COUNT) p.adult_form = pick_adult_form();
  } else {
    p.adult_form = FORM_UNSET;
    if (stage == STAGE_CHILD || stage == STAGE_TEEN) set_minor_form(stage);
  }
  g_events |= SIM_EV_STAGE_UP;
}

void sim_god_set_form(uint8_t form)
{
  if (!g_pet || form >= FORM_COUNT) return;
  g_pet->adult_form = form;
  g_pet->flags |= PF_GOD_TAINTED;
  g_events |= SIM_EV_ADULT_FORM;
}

void sim_god_kill(uint8_t cause)
{
  if (!g_pet) return;
  if (cause >= DEATH_COUNT) cause = DEATH_ACCIDENT;
  g_pet->flags |= PF_GOD_TAINTED;
  do_death(cause);
}

void sim_god_set_genome(const Genome& g)
{
  if (!g_pet) return;
  PetSave& p = *g_pet;
  p.genome = g;
  gene_set_tainted(p.genome, 1);        // masks, stamps magic_ver and reseals
  p.flags |= PF_GOD_TAINTED;
  // Nothing in this module caches a genome-derived value except the weight,
  // which is bounded by the (possibly new) body_size gene.
  p.weight_dg = (int16_t)NT_CLAMP((int32_t)p.weight_dg,
                                  (int32_t)WEIGHT_DG_MIN, (int32_t)WEIGHT_DG_MAX);
}

void sim_god_set_sick(uint8_t on)
{
  if (!g_pet) return;
  PetSave& p = *g_pet;
  if (on) {
    if (!(p.flags & PF_SICK)) { p.sick_episodes++; g_events |= SIM_EV_SICK_START; }
    p.flags |= PF_SICK;
  } else {
    if (p.flags & PF_SICK) g_events |= SIM_EV_SICK_END;
    p.flags &= (uint16_t)~PF_SICK;
  }
  g_sick_hours = 0;
  p.flags |= PF_GOD_TAINTED;
}

void sim_god_set_poop(uint8_t count)
{
  if (!g_pet) return;
  if (count > POOP_MAX) count = POOP_MAX;
  if (count > g_pet->poop_count) g_events |= SIM_EV_POOP;
  g_pet->poop_count = count;
  g_poop_timer      = 0;
  g_pet->flags |= PF_GOD_TAINTED;
}
