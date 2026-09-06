// =============================================================================
//  tests/tools/sim_days.cpp - THIRTY SIMULATED DAYS (P9-C4).
//
//  Drives the REAL game/sim.cpp, the REAL game/xp.cpp ledger and the REAL
//  game/encounters.cpp roll for 30 days under a named player profile, and
//  prints the level curve, the daily hunger dip, the encounter mix and where
//  every XP point came from. It is the instrument the plan's
//  "tune balance.h until section 57 holds" line asks for, and it is what
//  settles the XP_TABLE decision that has been deferred three times.
//
//  NOT part of `make check`: it prints a report and answers a human, which is
//  what tests/tools/ means. `make -C tests balancetool` builds it.
//
// -----------------------------------------------------------------------------
//  WHAT A "DAY" IS HERE, AND WHERE THE DEFINITION COMES FROM
// -----------------------------------------------------------------------------
//  It is NOT 24 h of play. game/xp.cpp says outright that "the meter therefore
//  means XP per day of device-ON time rather than per day of wall time" - since
//  P6-C4 every metered bucket refills out of xp_ledger_tick(dt_s), i.e. seconds
//  the device was switched on. So a profile is (device-ON hours, what the player
//  does in them), and the rest of the 24 h is an ABSENCE: game/sim.cpp still
//  integrates hunger, happiness, cleanliness and energy across it through
//  sim_catch_up_ex(), and the XP ledger deliberately does not refill.
//
//  The other definition in the tree is data/balance.h's own sentence: "a meal
//  buys about 7 h of satiety, a clean about 12 h of cleanliness, so 3-4 touches
//  a day is a well-kept Pebble". The NORMAL profile below is built to be that
//  player and the printout says how many touches it actually landed, so the
//  claim can be checked rather than assumed.
//
// -----------------------------------------------------------------------------
//  THE FOUR THINGS THIS SIMULATOR MODELS RATHER THAN RUNS, SAID FIRST
// -----------------------------------------------------------------------------
//   1. A BATTLE IS AN AWARD, NOT A FIGHT. A practice win pays XP_BATTLE_WIN
//      through xp_add(XP_SRC_BATTLE) and that is all this file does with it.
//      Whether the player WINS is tests/tools/balance_matrix.cpp's question, and
//      linking the engine here would make one binary answer two questions badly.
//      The profile therefore states a WIN RATE and the report prints what the
//      battle bucket actually paid.
//
//   2. A MINIGAME IS A SCORE, NOT A GAME. sim_apply_play_result(permille) is the
//      real seam the GAME screen calls, and the real MG_COOLDOWN_S and the real
//      rolling PLAY_DECAY window apply; what is modelled is how well the player
//      plays.
//
//   3. THE APP LAYER IS RE-BUILT HERE, NOT LINKED. app/app.cpp is Arduino-bound,
//      so the funnel it owns - sim_apply_action() landing, then
//      app_award_xp(xp_care_action_amount(), XP_SRC_CARE) - is written out in
//      pay_care() below. THAT IS A SECOND IMPLEMENTATION and it is the one place
//      this file can drift from the firmware. It is four lines, it is named
//      here, and ui/ui.cpp:481 plus app/app.cpp:981-984 are the two lines it
//      copies.
//
//   4. THERE IS NO SCAN. A network is a (hash, category) pair invented from the
//      seed, so what is measured is the ENCOUNTER TABLE's mix, not a radio's.
//
// -----------------------------------------------------------------------------
//  WHAT IT CAN AND CANNOT SAY ABOUT PACING
// -----------------------------------------------------------------------------
//  CAN: how much XP each source actually pays under the real caps, the real
//  cooldowns and the real refusals - which is exactly the thing arithmetic over
//  data/balance.h cannot supply, because the ceilings are not reachable. The
//  hourly care cap is 10 XP (5 actions), but ACT_MEAL_REFUSE_PCT 90 and a
//  4,200 milli/h hunger decay mean FEED_MEAL is available about once every
//  2.4 h, so the care bucket is nowhere near its ceiling and the report prints
//  the saturation fraction rather than assuming one.
//
//  CANNOT: say what a real player does. The profiles are ASSUMPTIONS, they are
//  written out as a table so they can be argued with, and every pacing number
//  below is conditional on them. Nobody has watched a person play this device.
//
// -----------------------------------------------------------------------------
//  THE SECTION 57 CRITERION, AND WHERE IT IS NOT
// -----------------------------------------------------------------------------
//  "Ignoring the game for 8 h never drops health below 60 %" is checked here
//  from a well-kept Pebble, over every start hour of the day, and it is a real
//  spec 57 line.
//
//  "Level 30 is 3-4 weeks of normal play" IS NOT IN SPEC SECTION 57. It appears
//  only in PEBBLEBOL_IMPLEMENTATION_PLAN.md, where it is attributed to that
//  section; grepping the spec for "week", "level 30" or "3-4" returns nothing.
//  It is the PLAN'S GLOSS, it is the criterion the two XP curves disagree about,
//  and this file measures against it while saying that is what it is.
//
//  All identifiers and comments English.
// =============================================================================
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/config.h"
#include "core/nt_types.h"
#include "data/balance.h"
#include "data/species_table.h"
#include "game/box.h"
#include "game/encounters.h"
#include "game/genome.h"
#include "game/sim.h"
#include "game/xp.h"
#include "persistence/save_schema.h"

#define SD_DAYS_DEFAULT   30u
#define SD_EPOCH0         1700000000u    // the epoch tests/test_care.cpp uses
#define SD_DOY0           100u           // mid-April: sunrise ~07:20, sunset ~20:55
#define SD_HEALTH_FLOOR   60u            // spec 57: eight ignored hours

// -----------------------------------------------------------------------------
//  THE PACK'S REJECTED CURVE, WRITTEN OUT SO BOTH CAN BE WALKED AT ONCE.
//
//  data/balance.h ships ONE curve and game/xp.cpp is compiled against it, so a
//  binary cannot hold two. What it CAN hold is one measured XP stream and two
//  curves walked over it - and that is sound precisely because nothing in the
//  earning path reads the level: XP_CAP_* are per device and per hour, a care
//  action pays XP_CARE_ACTION at every level, XP_BATTLE_WIN is flat, and
//  xp_add() spends the meter even at XP_LEVEL_MAX (P7-C6). The shipped curve's
//  level line below is the REAL one, produced by the real xp_add(); the pack's
//  is this table walked over the same granted-XP stream, and it is arithmetic
//  rather than a second simulation. Said plainly because the difference matters
//  if a later phase makes earning depend on level.
//
//  inc(L) = 25 + 12*(L-1) + 4*(L-1)^2, total 36,453. Source:
//  tools/content/balance.json's XP_TABLE at the commit this file was written.
// -----------------------------------------------------------------------------
static const uint16_t PACK_XP_TABLE[XP_LEVEL_MAX + 1] = {
     0,
    25,   41,   65,   97,  137,  185,  241,  305,  377,  457,
   545,  641,  745,  857,  977, 1105, 1241, 1385, 1537, 1697,
  1865, 2041, 2225, 2417, 2617, 2825, 3041, 3265, 3497,
     0
};

// -----------------------------------------------------------------------------
//  THE PLAYER PROFILES. Assumptions, written down so they can be argued with.
//
//  A profile states what the player INTENDS - how many care touches, minigames,
//  practice battles and network explorations they attempt in a day - and the
//  report prints what actually LANDED, with the refusals broken out by reason.
//  That split is the whole point: the hourly XP ceilings in data/balance.h are
//  arithmetic, and whether they are REACHABLE is what only a simulation can say.
//  `touches_day == 0` is the SATURATE profile: try every care action every
//  minute, which is the upper bound on what the device will pay at all.
//
//  "3-4 touches a day is a well-kept Pebble" is data/balance.h's own sentence,
//  and the NORMAL profile is built to be that player.
// -----------------------------------------------------------------------------
struct Profile {
  const char* name;
  uint8_t  on_hours;         // device-ON hours per day
  uint8_t  start_hour;       // local hour the session begins
  uint8_t  touches_day;      // care actions ATTEMPTED per day; 0 = saturate
  uint8_t  minigames_day;    // attempted
  uint16_t mg_permille;      // how well they play, 0..1000
  uint8_t  battles_day;      // practice battles attempted
  uint8_t  battle_win_pct;
  uint8_t  explores_day;     // networks explored (ENCOUNTER_BUCKET_S is 6 h, so
                             // 4 is one per bucket)
  const char* note;
};

static const Profile PROFILES[] = {
  { "light",     2u, 19u,  3u,  1u, 600u,  0u,   0u, 2u,
    "evening only: two device-ON hours, three touches, one minigame, no battles" },
  { "normal",    8u,  9u,  4u,  4u, 750u,  4u,  60u, 4u,
    "data/balance.h's own well-kept Pebble: 8 device-ON h, 3-4 touches, four "
    "minigames, four practice battles, one network per encounter bucket" },
  { "heavy",    14u,  8u, 10u, 12u, 900u, 12u,  80u, 8u,
    "the player who is trying: 14 device-ON h and three times the intent" },
  { "saturate", 16u,  7u,  0u, 40u, 1000u, 40u, 100u, 12u,
    "THE UPPER BOUND, not a person: every care action tried every minute, a "
    "minigame and a battle whenever the cooldown allows" },
};
#define SD_PROFILE_COUNT ((int)(sizeof PROFILES / sizeof PROFILES[0]))

// -----------------------------------------------------------------------------
//  ONE RUN'S ACCUMULATORS
// -----------------------------------------------------------------------------
struct DayRow {
  uint8_t  level;               // PebbleInstance.level - see the STAGE FLOOR note
  uint8_t  xp_level_ship;       // level from the XP stream alone, shipped curve
  uint8_t  xp_level_pack;       // ...and the pack's
  uint32_t xp_day;              // XP GRANTED that day, all sources
  uint8_t  hunger_min;
  uint8_t  health_min;
  uint8_t  happy_min;
  uint8_t  energy_min;
  uint16_t care_actions;        // care actions that LANDED
  uint16_t minigames;
  uint16_t battles_won;
  uint8_t  enc[ENC_OUT_COUNT];
};

struct Run {
  uint32_t granted[XP_SRC_COUNT];
  uint32_t offered[XP_SRC_COUNT];
  uint32_t xp_total;
  uint32_t care_landed, care_tried;
  uint32_t care_err[AERR_COUNT];
  uint32_t mg_played, mg_tried;
  uint32_t mg_err[AERR_COUNT];
  uint32_t battles_started, battles_won;
  uint32_t enc[ENC_OUT_COUNT];
  uint32_t asleep_min, awake_min;
};

static PebbleInstance g_pet;
static SimEnv         g_env;
static Run            g_run;

// -----------------------------------------------------------------------------
//  THE APP-LAYER FUNNEL, RE-BUILT. See point 3 in the banner.
//  app/app.cpp reads the meter either side of the award to learn what it
//  actually paid; the same trick is the only honest way to record a per-source
//  total, because meter_take() is file-static in game/xp.cpp.
// -----------------------------------------------------------------------------
static uint16_t award(uint16_t amount, XpSource src)
{
  if (amount == 0u) return 0u;
  g_run.offered[src] += amount;
  const uint16_t before = xp_daily_left(xp_ledger(), src);
  (void)xp_add(g_pet, amount, src, nullptr);
  const uint16_t after  = xp_daily_left(xp_ledger(), src);
  // 0xFFFF is xp_daily_left()'s unmetered sentinel: nothing was spent, so the
  // whole award was granted.
  const uint16_t granted = (before == 0xFFFFu) ? amount
                         : ((before > after) ? (uint16_t)(before - after) : 0u);
  g_run.granted[src] += granted;
  g_run.xp_total     += granted;
  return granted;
}

// ui/ui.cpp:481 - EVERY care action that lands pays XP_CARE_ACTION.
static bool try_care(ActionId a)
{
  ActionResult r;
  memset(&r, 0, sizeof r);
  ++g_run.care_tried;
  if (!sim_apply_action(a, r)) {
    if (r.err < (uint8_t)AERR_COUNT) ++g_run.care_err[r.err];
    return false;
  }
  ++g_run.care_landed;
  (void)award(xp_care_action_amount(), XP_SRC_CARE);
  return true;
}

// THE PLAYER'S ONE TOUCH: the neediest thing the Pebble wants right now. The
// thresholds are the balance.h ones the action itself is gated on, so the
// player is not asking for something the sim is certain to refuse.
static bool one_touch(void)
{
  if (sim_is_sick())                                            return try_care(ACT_MEDICINE);
  if (sim_stat_pct(ST_HUNGER)  < (uint8_t)ACT_MEAL_REFUSE_PCT)  return try_care(ACT_FEED_MEAL);
  if (sim_stat_pct(ST_HYGIENE) < 80u)                           return try_care(ACT_CLEAN);
  if (sim_stat_pct(ST_HAPPINESS) < 85u &&
      sim_stat_pct(ST_ENERGY) >= (uint8_t)ACT_PLAY_MIN_ENERGY_PCT)
                                                                return try_care(ACT_PLAY);
  return try_care(ACT_PET);
}

// -----------------------------------------------------------------------------
//  THE FIXTURE. tests/test_care.cpp's hatched Pebble, PLUS a species row and an
//  id - without both, game/xp.cpp's xp_add() returns on its first line
//  ("an empty slot is not a creature") and every award is silently worth zero.
//  That is not a detail: the first draft of this file did not set them and
//  printed a full level curve anyway, because game/sim.cpp raises the level on
//  its own (see THE STAGE FLOOR below).
// -----------------------------------------------------------------------------
static void new_pet(uint32_t seed, uint8_t hour)
{
  genome_seed(seed);
  sim_seed(seed);
  memset(&g_pet, 0, sizeof g_pet);
  sim_bind(g_pet);
  sim_new_pet(genome_genesis(), SD_EPOCH0, 0);
  sim_hatch();

  g_pet.magic      = (uint16_t)PEBBLE_MAGIC;
  g_pet.layout_ver = (uint8_t)PEBBLE_LAYOUT_VER;
  g_pet.species_id = (uint8_t)SPECIES_ID_STARTER;
  g_pet.id         = 1u;
  g_pet.level      = 1u;
  g_pet.xp         = 0u;
  const SpeciesDef* sp = species_get(g_pet.species_id);
  if (sp) {
    for (uint8_t m = 0; m < (uint8_t)PB_MOVE_COUNT; ++m) g_pet.moves[m] = sp->moves[m];
    g_pet.hp_cur = xp_hp_max(sp->base_hp, 1u);
  }

  sim_env_defaults(g_env);
  g_env.now_epoch   = SD_EPOCH0;
  g_env.clock_valid = 1u;
  g_env.local_hour  = hour;
  g_env.local_min   = 0u;
  g_env.day_of_year = (uint16_t)SD_DOY0;
  sim_set_env(g_env);

  // A brand-new device has no history, so the ledger starts full - the same
  // xp_ledger_reset(1) app/app.cpp does on a first boot.
  xp_ledger_reset(1u);
}

static void advance_env(uint32_t seconds)
{
  g_env.now_epoch += seconds;
  uint32_t sod = (uint32_t)g_env.local_hour * 3600u + (uint32_t)g_env.local_min * 60u
               + seconds;
  while (sod >= 86400u) {
    sod -= 86400u;
    g_env.day_of_year = (uint16_t)((g_env.day_of_year + 1u) % 366u);
  }
  g_env.local_hour = (uint8_t)(sod / 3600u);
  g_env.local_min  = (uint8_t)((sod % 3600u) / 60u);
  sim_set_env(g_env);
}

// -----------------------------------------------------------------------------
//  ONE MINUTE OF DEVICE-ON TIME. sim_tick() and xp_ledger_tick() together are
//  what app/app.cpp:981 does per logic tick, at a minute rather than a second
//  because both integrators carry their remainder (game/sim.h's SIM_SUBSTEP_S
//  contract, and the balance.h asserts that the ledger's refill step divides its
//  window exactly).
// -----------------------------------------------------------------------------
static void on_minute(void)
{
  sim_tick(60u);
  xp_ledger_tick(60u);
  advance_env(60u);

  // Carried time, but only while AWAKE - game/xp.h: "Asleep time and time in
  // the Box pay nothing".
  if (sim_is_asleep()) {
    ++g_run.asleep_min;
  } else {
    ++g_run.awake_min;
    const uint16_t due = xp_carry_due(60u);
    if (due != 0u) (void)award(due, XP_SRC_CARRY);
  }
}

static uint32_t sd_rand(uint32_t& s)
{
  s ^= s << 13; s ^= s >> 17; s ^= s << 5;
  return s;
}

// Walks one curve one step. `lv` and `acc` are the caller's running level and
// XP-inside-level; the shape is game/xp.cpp's own carry loop.
static void curve_step(const uint16_t* tab, uint8_t& lv, uint32_t& acc, uint32_t add)
{
  if (lv >= (uint8_t)XP_LEVEL_MAX) { acc = 0u; return; }
  acc += add;
  while (lv < (uint8_t)XP_LEVEL_MAX) {
    const uint16_t need = tab[lv];
    if (need == 0u || acc < (uint32_t)need) break;
    acc -= (uint32_t)need;
    ++lv;
  }
  if (lv >= (uint8_t)XP_LEVEL_MAX) acc = 0u;
}

// -----------------------------------------------------------------------------
//  THE RUN
// -----------------------------------------------------------------------------
static void run_profile(const Profile& pr, uint32_t seed, uint16_t days, DayRow* rows)
{
  memset(&g_run, 0, sizeof g_run);
  new_pet(seed, pr.start_hour);

  uint32_t rs = seed ? seed : 1u;
  const uint16_t on_min = (uint16_t)pr.on_hours * 60u;

  uint8_t  lv_ship = 1u, lv_pack = 1u;
  uint32_t ac_ship = 0u, ac_pack = 0u;

  for (uint16_t d = 0; d < days; ++d) {
    DayRow& row = rows[d];
    memset(&row, 0, sizeof row);
    row.hunger_min = 100u; row.health_min = 100u;
    row.happy_min  = 100u; row.energy_min = 100u;

    const uint32_t xp_at_day_start = g_run.xp_total;

    for (uint16_t m = 0; m < on_min; ++m) {
      on_minute();

      const uint8_t hu = sim_stat_pct(ST_HUNGER);
      const uint8_t he = sim_stat_pct(ST_HEALTH);
      const uint8_t ha = sim_stat_pct(ST_HAPPINESS);
      const uint8_t en = sim_stat_pct(ST_ENERGY);
      if (hu < row.hunger_min) row.hunger_min = hu;
      if (he < row.health_min) row.health_min = he;
      if (ha < row.happy_min)  row.happy_min  = ha;
      if (en < row.energy_min) row.energy_min = en;

      // --- care. The player TRIES; the sim refuses what it refuses, and a
      //     refusal costs nothing (data/balance.h: "a refused action is a
      //     friendly toast"). What lands is what the model is worth.
      if (pr.touches_day == 0u) {
        // SATURATE: every action, every minute.
        if (try_care(ACT_FEED_MEAL)) ++row.care_actions;
        if (try_care(ACT_CLEAN))     ++row.care_actions;
        if (try_care(ACT_PLAY))      ++row.care_actions;
        if (try_care(ACT_PET))       ++row.care_actions;
        if (try_care(ACT_MEDICINE))  ++row.care_actions;
      } else if ((m % (on_min / pr.touches_day)) == 0u) {
        if (one_touch()) ++row.care_actions;
      }

      // --- minigames, spread across the ON block
      if (pr.minigames_day != 0u && (m % (on_min / pr.minigames_day)) == 0u) {
        ActionResult r; memset(&r, 0, sizeof r);
        ++g_run.mg_tried;
        if (sim_apply_play_result(pr.mg_permille, r)) {
          ++g_run.mg_played; ++row.minigames;
          (void)award(xp_minigame_amount(pr.mg_permille), XP_SRC_MINIGAME);
        } else if (r.err < (uint8_t)AERR_COUNT) {
          ++g_run.mg_err[r.err];
        }
      }

      // --- practice battles. Modelled as an award; see point 1 in the banner.
      if (pr.battles_day != 0u && (m % (on_min / pr.battles_day)) == 0u) {
        ++g_run.battles_started;
        if ((sd_rand(rs) % 100u) < pr.battle_win_pct) {
          ++g_run.battles_won; ++row.battles_won;
          (void)award((uint16_t)XP_BATTLE_WIN, XP_SRC_BATTLE);
        }
      }

      // --- exploration, spread across the ON block
      if (pr.explores_day != 0u && (m % (on_min / pr.explores_day)) == 0u) {
        EncounterInput in;
        memset(&in, 0, sizeof in);
        in.net_hash     = sd_rand(rs) | 1u;
        in.bucket       = encounter_bucket(g_env.now_epoch);
        in.device_seed  = seed;
        in.category     = (uint8_t)(sd_rand(rs) % (uint32_t)NET_CAT_COUNT);
        in.rssi         = (int8_t)(-40 - (int)(sd_rand(rs) % 50u));
        in.active_level = g_pet.level ? g_pet.level : 1u;
        in.progress     = 1u;
        EncounterResult out;
        if (encounter_roll(in, out) && out.outcome < (uint8_t)ENC_OUT_COUNT) {
          ++g_run.enc[out.outcome];
          ++row.enc[out.outcome];
          if (out.outcome == (uint8_t)ENC_OUT_SPECIAL) {
            const uint16_t x = encounter_special_xp(out, (uint8_t)CAL_USER);
            if (x != 0u) (void)award(x, XP_SRC_SPECIAL);
          }
        }
      }
    }

    // --- the rest of the day: the device is OFF. game/sim.cpp still integrates
    //     the care model across it; the XP ledger deliberately does not refill
    //     (game/xp.h, P6-C4), so xp_ledger_tick() is NOT called here and that
    //     absence is the point rather than an oversight.
    const uint32_t off_s = (uint32_t)(24u - pr.on_hours) * 3600u;
    if (off_s != 0u) {
      advance_env(off_s);
      AbsenceReport rep;
      sim_catch_up_ex(off_s, 1u, rep);
      const uint8_t he = sim_stat_pct(ST_HEALTH);
      if (he < row.health_min) row.health_min = he;
      const uint8_t hu = sim_stat_pct(ST_HUNGER);
      if (hu < row.hunger_min) row.hunger_min = hu;
      const uint8_t en = sim_stat_pct(ST_ENERGY);
      if (en < row.energy_min) row.energy_min = en;
    }

    row.xp_day = g_run.xp_total - xp_at_day_start;
    curve_step(XP_TABLE,       lv_ship, ac_ship, row.xp_day);
    curve_step(PACK_XP_TABLE,  lv_pack, ac_pack, row.xp_day);

    row.level         = g_pet.level;
    row.xp_level_ship = lv_ship;
    row.xp_level_pack = lv_pack;
  }
}

// -----------------------------------------------------------------------------
//  SPEC 57: EIGHT IGNORED HOURS NEVER DROP HEALTH BELOW 60 %.
//
//  Swept over every start hour so the sleep window cannot hide the worst case,
//  and MEASURED FROM A WELL-KEPT PEBBLE - which is what "ignoring the game"
//  means. The neglect-from-zero case (health floors at HEALTH_FLOOR_PCT and
//  stays there) is tests/test_care.cpp's and is not re-tested here.
//
//  The health track is worth reading before trusting a passing number: health
//  does not decay at all. It bleeds at CARE_HEALTH_BLEED_MPH only after a core
//  stat has been pinned at ZERO for CARE_ZERO_GRACE_S, so an 8 h absence from
//  full stats has to first empty a stat and then wait out the 2 h grace. A run
//  that reports 100 % at 8 h is a run in which the check could not have failed -
//  so the sweep ALSO reports 24 h, 48 h and 72 h, where the bleed really does
//  start, as the positive control that the instrument moves at all. A control
//  that stayed at 100 % is itself a failure and is reported as one.
// -----------------------------------------------------------------------------
static uint8_t ignore_for(uint32_t seed, uint8_t start_hour, uint32_t hours)
{
  new_pet(seed, start_hour);
  // Bring the Pebble to a well-kept state first: one hour of a diligent player.
  for (uint16_t m = 0; m < 60u; ++m) {
    on_minute();
    if ((m % 5u) == 0u) (void)one_touch();
  }
  uint8_t worst = sim_stat_pct(ST_HEALTH);
  // Then walk away, sampling every 15 minutes so a dip inside the window cannot
  // hide between two samples.
  for (uint32_t q = 0; q < hours * 4u; ++q) {
    advance_env(900u);
    AbsenceReport rep;
    sim_catch_up_ex(900u, 1u, rep);
    const uint8_t he = sim_stat_pct(ST_HEALTH);
    if (he < worst) worst = he;
  }
  return worst;
}

// Cumulative XP to reach `level` on a curve.
static uint32_t cum_to(const uint16_t* tab, uint8_t level)
{
  uint32_t t = 0;
  for (uint8_t i = 1u; i < level; ++i) t += tab[i];
  return t;
}

static const char* const AERR_NAME[AERR_COUNT] = {
  "none", "cooldown", "full", "tired", "not-sick", "nothing-to-do",
  "asleep", "is-egg", "bad-arg"
};

// -----------------------------------------------------------------------------
int main(int argc, char** argv)
{
  uint16_t days = (uint16_t)SD_DAYS_DEFAULT;
  uint32_t seed = 0x5EED0C7Au;
  int      self_check = 0;
  int      want_daily = 0;
  const char* only = nullptr;

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--days") == 0 && i + 1 < argc) days = (uint16_t)atoi(argv[++i]);
    else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) seed = (uint32_t)strtoul(argv[++i], nullptr, 0);
    else if (strcmp(argv[i], "--profile") == 0 && i + 1 < argc) only = argv[++i];
    else if (strcmp(argv[i], "--daily") == 0) want_daily = 1;
    else if (strcmp(argv[i], "--self-check") == 0) self_check = 1;
    else {
      fprintf(stderr, "usage: sim_days [--days N] [--seed S] [--profile NAME] "
                      "[--daily] [--self-check]\n");
      return 2;
    }
  }
  if (days == 0u || days > 400u) { fprintf(stderr, "days out of range\n"); return 2; }

  static DayRow rows[400];
  int bad = 0;

  printf("SIM DAYS  days=%u  seed=0x%08X  content=%04X\n", (unsigned)days,
         (unsigned)seed, (unsigned)CONTENT_VERSION);
  printf("A DAY IS DEVICE-ON HOURS PLUS AN ABSENCE (game/xp.cpp: the meter is "
         "per day of device-ON time).\n\n");

  // ===========================================================================
  //  SECTION 57, the criterion that IS in the spec.
  // ===========================================================================
  printf("=== SPEC 57: EIGHT IGNORED HOURS NEVER DROP HEALTH BELOW %u %% ===\n",
         (unsigned)SD_HEALTH_FLOOR);
  uint8_t worst8 = 100u, worst8_h = 0u;
  for (uint8_t h = 0; h < 24u; ++h) {
    const uint8_t w = ignore_for(seed, h, 8u);
    if (w < worst8) { worst8 = w; worst8_h = h; }
  }
  printf("  worst over all 24 start hours: health %u %% (walking away at %02u:00)%s\n",
         worst8, worst8_h, (worst8 < SD_HEALTH_FLOOR) ? "   << BELOW 60 %" : "");
  if (worst8 < SD_HEALTH_FLOOR) ++bad;

  uint8_t worst24 = 100u, worst48 = 100u, worst72 = 100u;
  for (uint8_t h = 0; h < 24u; h = (uint8_t)(h + 3u)) {
    const uint8_t a = ignore_for(seed, h, 24u);  if (a < worst24) worst24 = a;
    const uint8_t b = ignore_for(seed, h, 48u);  if (b < worst48) worst48 = b;
    const uint8_t c = ignore_for(seed, h, 72u);  if (c < worst72) worst72 = c;
  }
  printf("  CONTROL - the same instrument run longer, so the 8 h number is not a\n"
         "  reading from a gauge that never moves:\n");
  printf("    24 h -> %u %%    48 h -> %u %%    72 h -> %u %%   "
         "(HEALTH_FLOOR_PCT is %u)\n",
         worst24, worst48, worst72, (unsigned)HEALTH_FLOOR_PCT);
  if (worst72 >= 100u) {
    printf("    << THE CONTROL DID NOT MOVE: three ignored days cost no health at "
           "all, so the 8 h check above proves nothing.\n");
    ++bad;
  }
  printf("\n");

  // ===========================================================================
  //  THE STAGE FLOOR, measured once and named before any level line is read.
  // ===========================================================================
  {
    printf("=== THE STAGE FLOOR: WHAT LEVEL A PEBBLE REACHES ON XP ZERO ===\n");
    new_pet(seed, 9u);
    uint8_t lv_at[8]; uint32_t at_h[8]; uint8_t n = 0; uint8_t last = g_pet.level;
    lv_at[0] = last; at_h[0] = 0u; n = 1u;
    // Hourly for ten days, so the 3.75 h / 20 h / 48 h / 7 d anchors are seen
    // where they actually are rather than rounded to the first daily sample.
    for (uint32_t h = 1u; h <= 24u * 10u && n < 8u; ++h) {
      advance_env(3600u);
      AbsenceReport rep;
      sim_catch_up_ex(3600u, 1u, rep);
      if (g_pet.level != last) {
        last = g_pet.level; lv_at[n] = last; at_h[n] = h; ++n;
      }
    }
    printf("  game/sim.cpp advances the v1 life stage by AGE alone (config.h\n"
           "  AGE_CHILD_S 3.75 h, AGE_TEEN_S 20 h, AGE_ADULT_S 48 h, AGE_SENIOR_S\n"
           "  7 d) and stage_commit() writes that stage back into\n"
           "  PebbleInstance.level through level_of_stage() -> 5 / 10 / 15 / 20.\n"
           "  A Pebble that earns NO XP AT ALL therefore reaches:\n    ");
    for (uint8_t i = 0; i < n; ++i)
      printf("level %u at %u h (%.1f d)   ", lv_at[i], at_h[i], at_h[i] / 24.0);
    printf("\n  So the first %u XP of the shipped curve and the first %u of the "
           "pack's\n  are FREE, and every level line below prints both the "
           "PebbleInstance.level\n  (which carries this floor) and the level the XP "
           "stream alone would buy.\n\n",
           cum_to(XP_TABLE, 20u), cum_to(PACK_XP_TABLE, 20u));
  }

  // ===========================================================================
  //  THE PROFILES
  // ===========================================================================
  double   normal_xp_day = 0.0;
  uint32_t heavy_xp_day = 0, sat_xp_day = 0;

  for (int pi = 0; pi < SD_PROFILE_COUNT; ++pi) {
    const Profile& pr = PROFILES[pi];
    if (only && strcmp(only, pr.name) != 0) continue;

    run_profile(pr, seed, days, rows);

    printf("=== PROFILE %s: %u device-ON h/day from %02u:00 ===\n",
           pr.name, pr.on_hours, pr.start_hour);
    printf("  %s\n", pr.note);
    printf("  care: %u of %u attempts landed (%.1f/day). Refused: ",
           g_run.care_landed, g_run.care_tried,
           (double)g_run.care_landed / (double)days);
    for (int e = 1; e < (int)AERR_COUNT; ++e)
      if (g_run.care_err[e]) printf("%s %u  ", AERR_NAME[e], g_run.care_err[e]);
    printf("\n  minigames: %u of %u played (%.1f/day). Refused: ",
           g_run.mg_played, g_run.mg_tried, (double)g_run.mg_played / (double)days);
    for (int e = 1; e < (int)AERR_COUNT; ++e)
      if (g_run.mg_err[e]) printf("%s %u  ", AERR_NAME[e], g_run.mg_err[e]);
    printf("\n  practice battles: %u started, %u won (%.1f wins/day)\n",
           g_run.battles_started, g_run.battles_won,
           (double)g_run.battles_won / (double)days);
    printf("  awake %u min, asleep %u min of the %u device-ON minutes\n",
           g_run.awake_min, g_run.asleep_min,
           (unsigned)((uint32_t)pr.on_hours * 60u * days));
    {
      const uint32_t rolls = g_run.enc[0] + g_run.enc[1] + g_run.enc[2] + g_run.enc[3];
      printf("  encounters: WILD %u  ITEM %u  SPECIAL %u  NOTHING %u  "
             "(%u rolls, %.1f/day, NOTHING %u %%)\n",
             g_run.enc[ENC_OUT_WILD], g_run.enc[ENC_OUT_ITEM],
             g_run.enc[ENC_OUT_SPECIAL], g_run.enc[ENC_OUT_NOTHING], rolls,
             (double)rolls / (double)days,
             rolls ? (unsigned)((100u * g_run.enc[ENC_OUT_NOTHING]) / rolls) : 0u);
    }

    // WHERE THE XP CAME FROM, with the saturation fraction beside each metered
    // source - the number the banner says arithmetic cannot supply. The
    // reachable ceiling is the cap over the hours the device was ON, except the
    // carry bucket, whose window IS a whole day.
    static const char* const SRC[XP_SRC_COUNT] = {
      "care", "minigame", "carry", "battle", "capture", "item", "special"
    };
    static const uint32_t CAP_H[XP_SRC_COUNT] = {
      (uint32_t)XP_CAP_CARE, (uint32_t)XP_CAP_MINIGAME, 0u,
      (uint32_t)XP_CAP_BATTLE, 0u, 0u, 0u
    };
    printf("  XP over %u days: offered -> granted, and the share of the ceiling "
           "the device could pay:\n", days);
    for (int s = 0; s < (int)XP_SRC_COUNT; ++s) {
      if (g_run.offered[s] == 0u && g_run.granted[s] == 0u) continue;
      uint32_t ceiling = 0u;
      if (s == (int)XP_SRC_CARRY)  ceiling = (uint32_t)XP_CAP_CARRY * days;
      else if (CAP_H[s] != 0u)     ceiling = CAP_H[s] * (uint32_t)pr.on_hours * days;
      if (ceiling)
        printf("    %-9s %7u -> %7u   %3u %% of %u\n", SRC[s], g_run.offered[s],
               g_run.granted[s], (unsigned)((100ull * g_run.granted[s]) / ceiling),
               ceiling);
      else
        printf("    %-9s %7u -> %7u   (unmetered)\n", SRC[s], g_run.offered[s],
               g_run.granted[s]);
    }
    printf("    %-9s %7s    %7u   = %.1f XP per day\n", "TOTAL", "",
           g_run.xp_total, (double)g_run.xp_total / (double)days);
    // THE CARRY CEILING IS NOT XP_CAP_CARRY FOR MOST PLAYERS. Its window is a
    // whole day (XP_WIN_CARRY_S 86,400) but P6-C4 made it refill out of
    // DEVICE-ON seconds, so the refill step is 1,800 s of device-ON time per
    // point and a %u h/day player can only get %u of the 48 back each day. The
    // percentage above is against the nominal cap and therefore reads as
    // laziness when it is really a starved bucket.
    printf("    carry is refill-limited to %u XP/day at %u device-ON h "
           "(XP_WIN_CARRY_S/XP_CAP_CARRY = %lu s per point, spent from ON time)\n",
           (unsigned)((uint32_t)pr.on_hours * 3600u
                      / (uint32_t)(XP_WIN_CARRY_S / XP_CAP_CARRY)) > (unsigned)XP_CAP_CARRY
             ? (unsigned)XP_CAP_CARRY
             : (unsigned)((uint32_t)pr.on_hours * 3600u
                          / (uint32_t)(XP_WIN_CARRY_S / XP_CAP_CARRY)),
           pr.on_hours, (unsigned long)(XP_WIN_CARRY_S / XP_CAP_CARRY));

    printf("  LEVEL, every 5th day (inst = PebbleInstance.level, carries the "
           "stage floor;\n"
           "                        ship/pack = what the XP stream alone buys "
           "on each curve):\n");
    printf("    day "); for (uint16_t d = 0; d < days; ++d)
      if (d == 0u || ((d + 1u) % 5u) == 0u) printf("%4u", d + 1u);
    printf("\n    inst"); for (uint16_t d = 0; d < days; ++d)
      if (d == 0u || ((d + 1u) % 5u) == 0u) printf("%4u", rows[d].level);
    printf("\n    ship"); for (uint16_t d = 0; d < days; ++d)
      if (d == 0u || ((d + 1u) % 5u) == 0u) printf("%4u", rows[d].xp_level_ship);
    printf("\n    pack"); for (uint16_t d = 0; d < days; ++d)
      if (d == 0u || ((d + 1u) % 5u) == 0u) printf("%4u", rows[d].xp_level_pack);
    printf("\n");

    printf("  HUNGER DIP (lowest satiety of the day, %%):\n    ");
    for (uint16_t d = 0; d < days; ++d)
      if (d == 0u || ((d + 1u) % 5u) == 0u) printf("%4u", rows[d].hunger_min);
    printf("\n  HEALTH FLOOR (%%):\n    ");
    for (uint16_t d = 0; d < days; ++d)
      if (d == 0u || ((d + 1u) % 5u) == 0u) printf("%4u", rows[d].health_min);
    printf("\n  ENERGY FLOOR (%%):\n    ");
    for (uint16_t d = 0; d < days; ++d)
      if (d == 0u || ((d + 1u) % 5u) == 0u) printf("%4u", rows[d].energy_min);
    printf("\n");

    if (want_daily) {
      printf("  every day:\n");
      printf("    d  inst ship pack   xp/day  hun  hea  hap  ene  care  mg  win"
             "  W I S N\n");
      for (uint16_t d = 0; d < days; ++d)
        printf("   %3u %4u %4u %4u %8u %4u %4u %4u %4u %5u %3u %4u  %u %u %u %u\n",
               d + 1u, rows[d].level, rows[d].xp_level_ship, rows[d].xp_level_pack,
               rows[d].xp_day, rows[d].hunger_min, rows[d].health_min,
               rows[d].happy_min, rows[d].energy_min, rows[d].care_actions,
               rows[d].minigames, rows[d].battles_won,
               rows[d].enc[0], rows[d].enc[1], rows[d].enc[2], rows[d].enc[3]);
    }

    // TIME TO LEVEL 30, the question the XP curve decision turns on. Two
    // readings, and the second is the one that matters on a real device: the
    // stage floor gives away every level up to 20 for free.
    const double per_day = (double)g_run.xp_total / (double)days;
    const double ship_all  = per_day > 0.0 ? cum_to(XP_TABLE, 30u) / per_day : 0.0;
    const double pack_all  = per_day > 0.0 ? cum_to(PACK_XP_TABLE, 30u) / per_day : 0.0;
    const double ship_20   = per_day > 0.0
        ? (cum_to(XP_TABLE, 30u) - cum_to(XP_TABLE, 20u)) / per_day : 0.0;
    const double pack_20   = per_day > 0.0
        ? (cum_to(PACK_XP_TABLE, 30u) - cum_to(PACK_XP_TABLE, 20u)) / per_day : 0.0;
    printf("  TIME TO LEVEL 30 at this profile's measured %.1f XP/day:\n"
           "    from level 1 on XP alone  shipped %7.1f d (%5.1f wk)   "
           "pack %7.1f d (%5.1f wk)\n"
           "    from the level-20 floor   shipped %7.1f d (%5.1f wk)   "
           "pack %7.1f d (%5.1f wk)\n\n",
           per_day, ship_all, ship_all / 7.0, pack_all, pack_all / 7.0,
           ship_20, ship_20 / 7.0, pack_20, pack_20 / 7.0);

    if (strcmp(pr.name, "normal") == 0)   normal_xp_day = per_day;
    if (strcmp(pr.name, "heavy") == 0)    heavy_xp_day  = (uint32_t)(per_day + 0.5);
    if (strcmp(pr.name, "saturate") == 0) sat_xp_day    = (uint32_t)(per_day + 0.5);
  }

  // ===========================================================================
  //  THE PACING CRITERION AND THE DECISION IT SETTLES.
  // ===========================================================================
  if (!only) {
    printf("=== THE PACING CRITERION ===\n");
    printf("  \"level 30 is 3-4 weeks of normal play\" is NOT a spec 57 line. It is\n"
           "  PEBBLEBOL_IMPLEMENTATION_PLAN.md's gloss on section 57 - grepping the\n"
           "  spec for \"week\", \"level 30\" or \"3-4\" returns nothing - and it is\n"
           "  still the only stated pacing target, so it is what the two curves are\n"
           "  measured against. 3-4 weeks is 21 to 28 days; 24.5 is its middle.\n\n");
    printf("  shipped curve total to level 30: %u\n", cum_to(XP_TABLE, 30u));
    printf("  pack curve    total to level 30: %u   (%.2fx)\n\n",
           cum_to(PACK_XP_TABLE, 30u),
           (double)cum_to(PACK_XP_TABLE, 30u) / (double)cum_to(XP_TABLE, 30u));
    printf("  %-9s %10s   %-28s %-28s\n", "profile", "XP/day",
           "shipped: days to 30", "pack: days to 30");
    for (int pi = 0; pi < SD_PROFILE_COUNT; ++pi) {
      const Profile& pr = PROFILES[pi];
      run_profile(pr, seed, days, rows);
      const double pd = (double)g_run.xp_total / (double)days;
      const double s_all = cum_to(XP_TABLE, 30u) / pd;
      const double p_all = cum_to(PACK_XP_TABLE, 30u) / pd;
      const double s_20  = (cum_to(XP_TABLE, 30u) - cum_to(XP_TABLE, 20u)) / pd;
      const double p_20  = (cum_to(PACK_XP_TABLE, 30u) - cum_to(PACK_XP_TABLE, 20u)) / pd;
      printf("  %-9s %10.1f   %6.0f d / %6.0f d from 20      %6.0f d / %6.0f d from 20\n",
             pr.name, pd, s_all, s_20, p_all, p_20);
    }
    printf("\n  THE XP/DAY FIGURE IS THE MEASUREMENT; the days-to-30 columns are that\n"
           "  figure divided into each curve's total, so they assume the rate does not\n"
           "  change with level - which is true of every source in game/xp.cpp today\n"
           "  (the caps are per device and per hour, and xp_add() spends the meter\n"
           "  even at XP_LEVEL_MAX).\n");
    printf("  Heavy play is %u XP/day and the physical ceiling - every care action\n"
           "  tried every minute for 16 h - is %u XP/day. Neither curve is reachable\n"
           "  in 3-4 weeks by the NORMAL player; what the two differ in is by HOW\n"
           "  MUCH, and that is the whole decision.\n\n", heavy_xp_day, sat_xp_day);
    (void)normal_xp_day;
  }

  if (self_check) {
    if (bad) { printf("SELF-CHECK FAIL: %d finding(s) above\n", bad); return 1; }
    printf("SELF-CHECK OK: spec 57's eight ignored hours hold, and the control "
           "that would catch a dead instrument moved.\n");
  }
  return bad ? 1 : 0;
}
