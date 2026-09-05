// =============================================================================
//  PEBBLEBOL - app/app.cpp
//  ENTRY POINT ONLY. app_setup() wires the modules in dependency order;
//  app_loop() pumps them. There is NO game logic in this file: every rule lives
//  in game/sim.cpp, every pixel in ui/render.cpp and ui/ui.cpp, every socket in
//  networking/net.cpp and networking/webui.cpp.
//
//  Pebblebol.ino is a three line shim that forwards setup()/loop() here, so the
//  ctags prototype injection arduino-cli performs on a .ino cannot reach any of
//  this code.
//
//  Identifiers and comments English; every user-facing byte comes from
//  core/strings_es.h.
// =============================================================================
#include "app.h"

#include <Arduino.h>
#include <string.h>
#include <esp_random.h>

#include "../core/config.h"
#include "../core/nt_types.h"
#include "../core/strings_es.h"
#include "../game/genome.h"
#include "../core/rng.h"
#include "../game/activity.h"    // the daily activity score (P6-C2)
#include "../game/cooldowns.h"   // cd_begin() from setup (P5-C2/C3)
#include "../game/sim.h"
#include "../persistence/game_state.h"
#include "../game/box.h"
#include "../game/xp.h"
#include "../game/evolution.h"
#include "../data/species_table.h"
#include "../persistence/save_manager.h"
#include "../hardware/boot.h"
#include "../hardware/kv_nvs.h"
#include "../hardware/gametime.h"
#include "../hardware/input.h"
#include "../hardware/power.h"    // the idle ladder (P6-C3)
#include "../hardware/audio.h"
#include "../ui/render.h"
#include "../networking/net.h"
#include "../networking/ble_social.h"
#include "../ui/ui.h"
#include "../ui/screen_error.h"   // the ERROR screen's retry / LED bindings
#include "../networking/webui.h"
#include "../dev/godmode.h"
#include "state_machine.h"       // sm_current(): the power ladder's release hook reads it
// data/index_html.h is deliberately NOT included: networking/webui.cpp is its
// single translation unit (a second inclusion doubles the page blob in .rodata).

// Guard rail for the 1 Hz scheduler. NT_TICK_MAX_OWED_S is in config.h beside
// the sleep slices, because it is the same number seen from the other side.
//
// P6-C3 RAISED IT FROM NT_TICK_RESYNC_MS's 4 s AND TURNED IT FROM A DROP INTO A
// CHARGE, and the difference is the whole point of the sleep rung. Before, the
// loop advanced g_tick_ms by exactly 1000 ms per pass and resynchronised past
// anything longer than 4 s - so an 8 s light-sleep slice would have run ONE
// second of simulation and thrown the other seven away, which is a pet whose
// timers stop while it sleeps (tests/test_care.cpp measures that shape). The
// bound still exists for the case it was written for: a stall is not elapsed
// game time and must not be charged.
static_assert((uint32_t)NT_TICK_MAX_OWED_S * 1000UL > (uint32_t)PWR_SLEEP_SLICE_MS,
              "a sleep slice must fit inside the tick scheduler's catch-up "
              "bound, or the seconds the device slept would be resynchronised "
              "away instead of charged to the pet");

// Gestures drained per loop(). input_poll() hands out one queued gesture per
// call; four is more than a human can generate inside one pass.
#define NT_GESTURES_PER_LOOP 4

// =============================================================================
//  MODULE-OWNED STATE
//  The live Pebbles belong to persistence/game_state.cpp and the active one is
//  an INDEX into that Box (plan 1.5.3), so the only long-lived object here is
//  the Config every other module reads through a pointer. It must outlive
//  setup(), hence file scope (and never a function-local static - see trap 2
//  above).
// =============================================================================
static Config   g_cfg;

static uint32_t g_tick_ms        = 0;      // scheduler cursor for the 1 Hz tick
static uint32_t g_boot_last_seen = 0;      // save_last_seen() as found at boot
static uint16_t g_cfg_crc        = 0;      // change detector for Config
static uint8_t  g_absence_unknown = 0;     // boot took the unknown-clock path
static uint8_t  g_clock_was_valid = 0;     // edge detector for a landing calibration
static uint8_t  g_nvs_ok          = 0;
static uint8_t  g_display_ok      = 0;     // rd_begin() answered on the I2C bus

// THE POWER LADDER'S OWN IDLE CLOCK (P6-C3). millis() of the last gesture this
// file dispatched. hardware/power.h says why it is not sm_idle_ms(): the IDLE
// rung releases the radio by navigating, and a navigation resets the NAVIGATION
// idle clock, so the ladder would have reset its own timer at the instant it
// arrived and oscillated for ever.
static uint32_t g_input_ms       = 0;
// A press that ended a light sleep was aimed at the DEVICE - the panel was dark
// - so the first gesture inside this window is consumed for the wake and never
// reaches the screen. The window exists so that a wake nobody followed up on
// cannot swallow an unrelated press a minute later.
#define NT_WAKE_SWALLOW_MS  1500UL
static uint32_t g_wake_ms        = 0;
static uint8_t  g_wake_armed     = 0;

// =============================================================================
//  THE TWO CLOCKS persistence/save_manager.cpp CANNOT COMPUTE ITSELF
//  A monotonic millisecond counter for the flash-wear filter, and the wall
//  clock for saved_epoch. Injecting them is what keeps the save policy pure and
//  host-testable (save_manager.h).
// =============================================================================
// gt_mono32(), not millis(): the wear filter defers a write by a millisecond
// period, and a period measured in UPTIME would be paused for the whole of a
// sleep - so the first tick after a ten-minute sleep would find the last write
// "recent" and defer again. gametime.h says which clock decides what.
static uint32_t clock_ms(void)    { return gt_mono32(); }
static uint32_t clock_epoch(void) { return gt_now(); }

// =============================================================================
//  CONFIG SIDE EFFECTS
//  Config is shared by pointer with ui (SETTINGS) and webui (the
//  CF_WEB_ENABLED toggle), so it can change under us from two directions.
//  Rather than have both of them
//  call back, the tick watches the struct's own CRC and re-applies the two
//  settings that live outside the struct: OLED contrast and the WiFi
//  credentials. Everything else is read straight from g_cfg by its owner.
// =============================================================================
static void apply_config(void)
{
  ui_note_brightness(g_cfg.brightness);

  // net_set_credentials() was here. It went with the station path (P5-C1):
  // this firmware never joins a network, so there is nothing to apply.
  // Config.wifi_ssid / wifi_pass stay in the struct as frozen padding - their
  // offsets are pinned by tests/fixtures/config_v1.bin - and nothing reads them.

  g_cfg_crc = g_cfg.crc16;
}

// =============================================================================
//  THE ERROR SCREEN'S TWO HARDWARE HANDS (plan T10)
//  ui/screen_error.cpp is a pure translation unit, so the panel bring-up and
//  PIN_LED are bound in from here. app_retry_display() is what "A: Reintentar"
//  runs: a display that answers on the second attempt gives the user the whole
//  device back, contrast and all.
// =============================================================================
static bool app_retry_display(void)
{
  if (!rd_begin()) return false;
  g_display_ok = 1;
  apply_config();
  return true;
}

// hardware/audio.h asks the firmware exactly one question - "is sound off?" -
// and this is the whole answer. It reads CF_MUTE off the live Config every
// time instead of the engine holding a copy, because a copy is a second place
// the flag lives and ui/screen_settings.cpp and ui/screen_home.cpp both toggle
// the original.
static bool app_audio_muted(void) { return (g_cfg.flags & CF_MUTE) != 0u; }

static void app_led(bool on)
{
  digitalWrite(PIN_LED, on ? LED_ON : LED_OFF);
}

// True when someone rewrote Config since the last call. gs_save_cfg() is
// what re-seals the CRC, so this fires exactly once per persisted change.
static bool config_changed(void)
{
  if (g_cfg.crc16 == g_cfg_crc) {
    return false;
  }
  g_cfg_crc = g_cfg.crc16;
  return true;
}

// =============================================================================
//  SIM ENVIRONMENT
//  sim owns no clock and no RNG (sim.h:13-17): the entry point
//  feeds it a complete SimEnv once per logic tick. Building it is the only
//  cross-module arithmetic in this file, and it is all integer.
// =============================================================================
static void build_env(SimEnv& env)
{
  sim_env_defaults(env);

  struct tm lt;
  const bool clock_ok = gt_local_tm(lt);

  env.now_epoch   = gt_now();
  env.clock_valid = clock_ok ? 1u : 0u;
  env.local_hour  = (uint8_t)lt.tm_hour;
  env.local_min   = (uint8_t)lt.tm_min;
  env.day_of_year = (uint16_t)lt.tm_yday;

  // Above x1 the sleep window is frozen, otherwise the pet
  // flickers in and out of it many times per second.
  uint8_t fh = 0;
  uint8_t fm = 0;
  if (god_freeze_clock(fh, fm)) {
    env.local_hour = fh;
    env.local_min  = fm;
  }
}

// =============================================================================
//  HOURLY-GAIN LEDGER SEAM
//  Two modules, neither of which may know about the other: sim.cpp owns the
//  anti-farm budget and does no I/O, persistence owns the NVS key and the write
//  cadence and knows nothing about game rules. This file is the only place
//  allowed to join them.
//
//  game_state calls this at the instant it commits a Pebble, so the snapshot is
//  the live one - ui.cpp forces a save right after every successful action, and
//  the points that action spent have to be inside the blob that write produces.
// =============================================================================
static bool gain_source(uint8_t pts[GS_GAIN_SLOTS], uint32_t& epoch)
{
  sim_gain_snapshot(pts);
  const uint32_t now = sim_now();
  // No trustworthy wall clock: before a calibration lands gt_now() - and
  // therefore sim_now() - is a seconds-since-boot counter, and a difference between two
  // of those describes nothing (the same reasoning as findings 1 and 2). Stamp
  // 0 so the loader refuses it. Writing the snapshot anyway, rather than
  // skipping the write, is the load-bearing half: it retires the last TRUSTED
  // blob, which a never-synced unit would otherwise replay - budget intact - on
  // every single reboot.
  epoch = (now >= (uint32_t)NT_EPOCH_SANE_MIN) ? now : 0u;
  return true;
}

// =============================================================================
//  THE XP FUNNEL
//  Every source of experience goes through here, and nowhere else, so the
//  three things a level-up owes the rest of the system happen exactly once:
//  the anti-farm ledger reaches flash the instant XP is SPENT (a reboot must
//  not be able to refill it), the UI is told through the event word it already
//  drains, and the new level is committed rather than left in RAM.
//
//  Only the ledger DECREASE is persisted, and since P6-C4 that is the whole of
//  what a reboot gets back. A refill needs no blob because it needs no wall
//  clock: xp_ledger_tick() puts the points back out of seconds the device
//  watched pass, and xp_ledger_restore() adds nothing at all. It used to add
//  (now_epoch - saved_epoch) / refill_step, which is the composition the hourly
//  gain budget above STILL uses - and which a player refills by typing a date on
//  the time screen. game/xp.cpp carries the measurement.
// =============================================================================
bool app_award_xp(uint16_t amount, XpSource src)
{
  if (amount == 0u) return false;
  const uint8_t act = box_active();
  if (act >= (uint8_t)BOX_SLOTS) return false;
  PebbleInstance* p = box_slot(act);
  if (!p) return false;

  uint8_t before[XP_LEDGER_SLOTS];
  uint8_t after[XP_LEDGER_SLOTS];
  xp_ledger_snapshot(before);

  uint8_t    ups     = 0;
  const bool leveled = xp_add(*p, amount, src, &ups);

  xp_ledger_snapshot(after);
  if (memcmp(before, after, sizeof after) != 0) {
    (void)gs_save_xp_ledger(after, sim_now());
  }

  if (leveled) {
    sim_post_event(SIM_EV_LEVEL_UP);
    (void)gs_save_active(true);
  }
  return leveled;
}

// =============================================================================
//  THE ACTIVITY SEAM (P6-C2, spec sections 25 and 57)
//
//  game/activity.cpp is pure and clock-free: it takes the wall clock and the
//  calibration state as an argument, exactly as game/cooldowns.cpp does, so
//  this file is where the two are read. app_act_clock() is the only place they
//  are assembled, so no caller can pass one without the other - and CAL_UNSET
//  is what makes the whole score zero.
// =============================================================================
ActClock app_act_clock(void)
{
  ActClock c;
  c.now_epoch = gt_now();
  c.cal       = (uint8_t)gt_cal_state();
  return c;
}

// -----------------------------------------------------------------------------
//  PAYING WHAT THE SCORE EARNED. Drained once per logic tick and NOWHERE else:
//  the screens and the care path only ever NOTE activity, so no screen can pay
//  XP or happiness on its own and there is exactly one place to look for either.
//
//  THE XP GOES THROUGH app_award_xp(XP_SRC_CARRY), which is the metered bucket,
//  which is the last of game/activity.h's anti-farm layers: even a bug in every
//  rule above it leaves the award rate-limited by a budget that only seconds the
//  device WATCHED PASS can refill. That sentence was false until P6-C4 - the
//  budget was also refilled by (now_epoch - saved_epoch) at every boot, and both
//  of those are a wall clock the player types on the time screen - which is why
//  game/xp.cpp's restore no longer ages anything forward.
//
//  THE HAPPINESS IS APPLIED THE WAY game/inventory.cpp's care items apply
//  theirs - clamped at PB_CARE_MILLI_MAX, never above, on the ACTIVE Pebble
//  only. A creature in the Box is not being carried. What it is NOT is a second,
//  unmetered reward: it is scaled to the XP the ledger actually paid for, so the
//  one budget covers both halves. See act_happy_for_granted_xp().
// -----------------------------------------------------------------------------
void app_pay_activity(void)
{
  // The persisted half of the score changed: it rides the "cd" pair, so this is
  // the same save cd_take_dirty() asks for.
  //
  // save_cooldowns() has no wear filter of its own - it is a straight
  // pair_write() - so the WRITE COUNT is whatever act_take_dirty() says, and
  // the caps are what bound it: 240 carried minutes + 20 interactions + 10
  // networks is AT MOST 270 writes a day, after which every further note
  // returns 0 before it can dirty anything. That is a fifth of the 1,440 the
  // "t" last-seen key already costs, and it is the reason the counters are
  // capped BEFORE the flag is set rather than after.
  //
  // The take-and-clear runs even in a read-only session, deliberately: the flag
  // must not survive to ask for a write later, and a read-only session is
  // exactly the one that may not write. gs_readonly() is checked second for
  // that reason and the short-circuit is the wrong way round on purpose.
  if (act_take_dirty() && !gs_readonly()) (void)save_cooldowns(gs_state().cds);

  const ActGain g = act_take_gain();

  // ONE BUDGET, BOTH HALVES OF THE REWARD. The award is what spends the meter,
  // so what it spent is read off the meter either side of it rather than
  // guessed: xp_add() takes min(owed, left) and nothing else can move the bucket
  // in between. A Pebble at XP_LEVEL_MAX is the one case where this pays less
  // than the score earned - xp_add() returns at the top of the curve before it
  // would spend anything, so granted is 0 and the happiness stops with the XP.
  // That is a loss, not a hole, and it is written down in game/activity.h.
  uint16_t granted = 0u;
  if (g.xp != 0u) {
    const uint16_t before = xp_daily_left(xp_ledger(), XP_SRC_CARRY);
    (void)app_award_xp(g.xp, XP_SRC_CARRY);
    const uint16_t after  = xp_daily_left(xp_ledger(), XP_SRC_CARRY);
    granted = (before > after) ? (uint16_t)(before - after) : 0u;
  }

  const uint16_t happy = act_happy_for_granted_xp(g, granted);
  if (happy != 0u) {
    const uint8_t act = box_active();
    PebbleInstance* p = (act < (uint8_t)BOX_SLOTS) ? box_slot(act) : nullptr;
    if (p != nullptr && p->care[CARE_HAPPINESS] < (int32_t)PB_CARE_MILLI_MAX) {
      int32_t v = p->care[CARE_HAPPINESS] + (int32_t)happy;
      if (v > (int32_t)PB_CARE_MILLI_MAX) v = (int32_t)PB_CARE_MILLI_MAX;
      p->care[CARE_HAPPINESS] = v;
    }
  }
}

void app_note_interaction(void)
{
  (void)act_note_interaction(gs_state().cds, app_act_clock());
}

// =============================================================================
//  EVOLUTION (spec section 18, plan P3-C3)
//
//  ui.cpp asks app_evolution_offer() whether to put the question, and if the
//  player answers yes it calls app_evolve_active() BEFORE it starts the show.
//  The model change and BOTH of its flushes are therefore finished before the
//  first ceremony frame is drawn: a brownout half way through the 4.5 s reboots
//  into the evolved creature, never into a half-applied one. That ordering is
//  the whole argument at the top of ui/ceremony.h and it must not be relaxed.
//
//  THE CONTEXT IS BUILT HERE because this is where a PebbleInstance can be
//  read. Happiness comes off the care array and corruption off the status byte;
//  items and the activity score do not exist until P6, so their EVOCTX_* bits
//  are left CLEAR and any rule that needs one REFUSES (game/evolution.h). An
//  unsupplied input that answered "true" would evolve a creature on a
//  requirement nobody checked.
// =============================================================================
static void evo_context_of(const PebbleInstance& p, EvoContext& ctx)
{
  evo_context_clear(ctx);

  int32_t happy = p.care[CARE_HAPPINESS];
  if (happy < 0)                  happy = 0;
  if (happy > PB_CARE_MILLI_MAX)  happy = PB_CARE_MILLI_MAX;
  ctx.happiness = (uint16_t)(happy / (PB_CARE_MILLI_MAX / 100L));   // milli -> %
  ctx.have |= EVOCTX_HAPPINESS;

  ctx.corrupted = (uint8_t)((p.status & PBS_CORRUPTED) != 0u);
  ctx.have |= EVOCTX_CORRUPTED;
}

bool app_evolution_offer(void)
{
  if (gs_readonly()) return false;          // a read-only session offers nothing
  const uint8_t act = box_active();
  if (act >= (uint8_t)BOX_SLOTS) return false;
  const PebbleInstance* p = box_peek(act);
  if (!p) return false;
  // The bit game/xp.cpp raised. It says the LEVEL requirement is met and
  // nothing more, so the whole rule is re-evaluated below with real context.
  if (!(p->evo_state & (uint8_t)EVO_STATE_PENDING)) return false;

  EvoContext ctx;
  evo_context_of(*p, ctx);
  return evolution_ready(*p, ctx) != 0u;
}

bool app_evolve_active(void)
{
  if (gs_readonly()) return false;
  const uint8_t act = box_active();
  if (act >= (uint8_t)BOX_SLOTS) return false;
  PebbleInstance* p = box_slot(act);
  if (!p) return false;

  EvoContext ctx;
  evo_context_of(*p, ctx);

  // Kept so the RAM change can be undone if the commit below refuses. Without
  // it a failed write would leave an evolved creature that flash has never
  // heard of - which is the exact state this function's ordering exists to
  // make impossible.
  const PebbleInstance before = *p;
  if (!evolution_apply(*p, ctx)) return false;

  // COMMIT, before anything animates. game/sim.cpp holds a raw pointer at this
  // same record and nothing moved it, so the simulation needs no re-bind:
  // species_id and hp_cur are not mirrored into its SimView, and the stage it
  // does mirror is derived from the level, which did not change.
  //
  // THE RETURN VALUE IS THE PROMISE. app.h tells the caller it may start a
  // 4.5 s ceremony because the change is already on flash; if the write did
  // not happen, saying "true" would turn a full NVS or a read-only session
  // into a silently lost evolution the moment the battery dips mid-show. Roll
  // back and refuse instead: the player sees no ceremony, keeps their pebble,
  // and the pending bit is still up so the offer returns.
  if (!gs_save_active(true)) { *p = before; return false; }

  // The checkpoint is a BACKUP of a save that already succeeded, so its failure
  // does not invalidate the evolution and must not roll one back. The ceremony
  // may proceed; only the nvs2 recovery copy is stale, which is what
  // save_checkpoint_all() failing means everywhere else in this file too.
  (void)save_checkpoint_all();
  return true;
}

// =============================================================================
//  BOOT: THE BOX AND THE ACTIVE PEBBLE
//  persistence decides whether there is anything to load; game/box.cpp decides
//  which slot is active and what a brand new Pebble is; the app.cpp only
//  sequences them.
//
//  NEVER an auto-wipe. A load that refused to touch flash (LOAD_CORRUPT,
//  LOAD_FOREIGN_NEWER) leaves the session read-only: the placeholder Pebble
//  below runs in RAM and no write can reach the save the user still owns.
// =============================================================================
static bool bind_active(void)
{
  const uint8_t act = box_active();
  if (act >= (uint8_t)BOX_SLOTS) return false;
  PebbleInstance* p = box_slot(act);
  if (!p) return false;
  sim_bind(*p);
  return true;
}

static void boot_box(void)
{
  box_bind(gs_state());

  // Empty is the safe seed for the anti-farm ledger; the branches below either
  // reconstruct it from what was actually left at the last save, or - on a
  // device with no history at all - hand it the full caps.
  xp_ledger_reset(0);

  if (gs_have_pebble() && bind_active()) {
    // sim_bind() seeded the hourly gain budget to 0 - safe, but a lie to anyone
    // who just had a power cut ("esta llena" at 19 % satiety for the first
    // minute, no full meal for 29 min). Replace it with what was actually left
    // at the last save, aged forward to the moment the device was last alive.
    // boot_absence()'s catch-up then refills the rest of the interval,
    // [last_seen, now], through its own gain_refill() - the two terms compose
    // into min(cap, saved + (now - saved_epoch) * cap / 3600), to within the
    // one milli-point that the two integer divisions can differ by.
    // A missing or corrupt blob leaves sim_bind()'s 0 in place.
    uint8_t  gpts[GS_GAIN_SLOTS];
    uint32_t gepoch = 0;
    if (gs_load_gain(gpts, gepoch)) {
      (void)sim_gain_restore(gpts, GS_GAIN_SLOTS, gepoch,
                             sim_pebble()->last_updated_epoch);
    }
    // THE XP LEDGER IS NOT THE SAME COMPOSITION AT ALL, SINCE P6-C4. It ages
    // nothing forward: the restore hands back exactly the bytes of the last
    // snapshot, and every point past that has to be refilled by
    // xp_ledger_tick() out of seconds this device watched pass. The two epochs
    // below still travel, because they are what says the blob came from a device
    // that knew the date, but they no longer buy a single point.
    //
    // They used to. left = min(cap, saved + (now - saved)/step) reads a wall
    // clock at both ends, the wall clock is typed on the time screen, and one
    // day of "elapsed" refills a whole bucket - so (clock +1 day, reboot, one
    // scan) x100 spent 990 metered XP in zero real seconds. game/xp.cpp carries
    // the measurement and what the honest player loses for closing it.
    //
    // THE CARE GAIN LEDGER ABOVE STILL HAS THAT SHAPE and is therefore still
    // refillable the same way; it bounds hourly stat gains rather than XP, its
    // clamp is one HOUR rather than one window, and closing it is a phase-7 item
    // written into the plan rather than something this chunk measured.
    // Without a trustworthy snapshot the XP budget stays at the zero
    // xp_ledger_reset(0) seeded: unkind for an hour, but the only direction that
    // cannot be farmed by power-cycling the device after spending the budget.
    uint8_t  xpts[XP_LEDGER_SLOTS];
    uint32_t xepoch = 0;
    if (gs_load_xp_ledger(xpts, xepoch)) {
      (void)xp_ledger_restore(xpts, (uint8_t)XP_LEDGER_SLOTS, xepoch,
                              sim_pebble()->last_updated_epoch);
    }
    return;
  }

  // FIRST BOOT (or a Box that lost every slot): the starter of plan P2-C10 -
  // species 1, level 1, ORIGIN_STARTER - into slot 0. Not an egg any more: v2
  // has levels, and the hatch ceremony becomes the evolution shell in P2-C11.
  // On a read-only session this Pebble is never written; it exists only so the
  // renderer has something to draw behind the error the user is about to see.
  // Nothing has ever been earned on this device, so a full ledger cannot be a
  // refill of a spent one - and a Pebble out of the box should not owe its
  // owner six minutes before the first care action is worth anything.
  xp_ledger_reset(1);

  const uint32_t now  = gt_now();
  const uint8_t  slot = box_new_pebble((uint8_t)SPECIES_ID_STARTER, 1,
                                       (uint8_t)ORIGIN_STARTER,
                                       genome_genesis(), rng_u32(RNG_MISC), now);
  if (slot != (uint8_t)BOX_SLOT_NONE) {
    (void)box_set_active(slot);
  }
  (void)bind_active();               // false only when the Box could not be made
}

// =============================================================================
//  SAVE RECOVERY
//  The SAVE ERROR screen's "Recuperar", bound into ui.cpp. It lives here and
//  not in ui.cpp because recovery ends with a DIFFERENT Pebble in the Box, and
//  rebinding the simulation is the entry point's job. False means "there was no
//  checkpoint": nothing was written and the session stays read-only.
// =============================================================================
static bool app_recover_save(void)
{
  if (gs_recover(g_cfg) != LOAD_RECOVERED_CKPT) {
    return false;
  }
  box_bind(gs_state());
  if (!bind_active()) {
    return false;
  }
  apply_config();
  g_boot_last_seen = save_last_seen();
  return true;
}

// =============================================================================
//  BOOT: ABSENCE
//  sim_catch_up_ex() reads SimEnv, so the environment must
//  already be pushed in. With no trustworthy clock (gt_cal_state() == CAL_UNSET)
//  it takes the unknown-clock path, which charges ZERO (plan section 1.7), and
//  we arm the retro-fix for the moment gt_set_epoch() lands.
//
//  "gt_is_valid() is false" alone is NOT evidence of an absence. The
//  crash-vs-abandonment discriminator hardware/boot.cpp already computes is the
//  missing input: BOOT_FIRST_RUN has nobody to have abandoned, and BOOT_CRASH /
//  BOOT_SOFT_RESET are explicitly not absences (the same reason the toast below
//  says "dizzy" rather than "abandoned"). On top of that the baseline itself
//  has to be a real wall clock: on a never-calibrated device save_last_seen()
//  returns the PREVIOUS boot's uptime, which cannot describe a gap at all.
// =============================================================================
static void boot_absence(void)
{
  SimEnv env;
  build_env(env);
  sim_set_env(env);

  const uint32_t now  = env.now_epoch;
  uint32_t absence_s  = (g_boot_last_seen != 0u)
                          ? gt_elapsed_since(g_boot_last_seen, now) : 0u;

  uint8_t known = (gt_cal_state() != CAL_UNSET) ? 1u : 0u;
  if (!known) {
    const BootKind bk       = boot_kind();
    const bool     real_gap = (g_boot_last_seen >= (uint32_t)NT_EPOCH_SANE_MIN) &&
                              (absence_s != 0u);
    // The list is nt_boot_charges_absence() in core/nt_types.h now, so this
    // file and tests/test_clock.cpp read the same one. BOOT_DEEPSLEEP is in the
    // charging set: a sleep gap IS elapsed time, and P6-C3's ladder is the
    // thing that makes the device produce one on purpose.
    if (!nt_boot_charges_absence(bk) || !real_gap) {
      known      = 1;     // nothing was abandoned: charge the true zero
      absence_s  = 0;
    }
  }

  // The nine stored Pebbles do not live: they only recover, each from its own
  // last_updated_epoch (spec section 9, plan P2-C10). The active one gets the
  // full simulation instead.
  if (known && now >= (uint32_t)NT_EPOCH_SANE_MIN) {
    (void)box_recover_all(now);
  }

  AbsenceReport rep;
  sim_catch_up_ex(absence_s, known, rep);
  g_absence_unknown = rep.clock_known ? 0u : 1u;

  ui_note_absence(rep);
  ui_note_events(sim_take_events());
  (void)gs_save_active(true);
}

// =============================================================================
//  THE POWER LADDER'S FOUR HOOKS (P6-C3, hardware/power.h)
//  The ladder is pure and knows nothing about Config, the panel, the radio or
//  the store. These four are the whole of what it can do to the device, and
//  each one is a single call into the module that already owns that thing.
// =============================================================================
static void pwr_hook_dim(bool on)
{
  // Through ui, not through rd_contrast_ramp(): ui.cpp's bright_service() is
  // the one owner of the contrast base and already arbitrates the user's
  // brightness against the asleep-pet dim. A second writer would fight it on
  // whichever of the two moved last.
  ui_note_power_dim(on);
}

static void pwr_hook_panel(bool on)
{
  rd_power(on);
}

static void pwr_hook_release(void)
{
  // NOTHING TO RELEASE FROM HOME, and skipping it there is not an optimisation.
  // The radio is only ever held by the screen that asked for it - NETWORK's scan
  // job, CREATOR's access point - and both give it back in their own leave()
  // hook, so on HOME it is already off. Re-entering HOME anyway would re-run its
  // enter hook, cut the shared interpolators and close whatever modal was up,
  // every time the device idled.
  if (sm_current() == SCR_HOME) return;

  // *** THE CARRIED RADIO DEBT, DISCHARGED HERE AND NOWHERE ELSE ***
  // NOT net_request(RADIO_OFF). ui_home() navigates; sm_goto() runs the
  // leaving screen's leave() hook; network_leave() calls wifi_scan_cancel(),
  // which is the single idempotent path that stops the driver and releases the
  // radio exactly once - and, inside wifi_down(), calls WiFi.scanDelete() on
  // the per-access-point array the driver heap-allocated. Going round that
  // would leave the job WSCAN_RUNNING with stopped == 0: wifi_scan_is_busy()
  // would go on answering true to this very ladder, and the next poll would
  // report FAILED to a player who did nothing. CREATOR's leave hook releases
  // the AP the same way. tools/check.sh gates hardware/power.* for the radio
  // call; tests/test_power.cpp is the case that watches the job end CANCELLED.
  ui_home();
}

static void pwr_hook_persist(void)
{
  // Once, on the way into IDLE, while the device is definitely awake and a
  // clean save is still cheap. save_touch_lastseen() first: without it the
  // absence baseline is however long ago the last 1 Hz tick was, and a sleep
  // measured from a stale baseline charges the awake time too.
  save_touch_lastseen(gt_now());
  (void)gs_save_active(true);
}

static const PowerHooks kPowerHooks = {
  &pwr_hook_dim, &pwr_hook_panel, &pwr_hook_release, &pwr_hook_persist
};

// =============================================================================
//  SETUP
// =============================================================================
void app_setup(void)
{
  // USB CDC. NEVER wait for the host to enumerate: a pet that only
  // runs when a laptop is attached is not a pet.
  Serial.begin(115200);
  Serial.println();
  Serial.println(F("[nt] " FW_NAME " " FW_VERSION));

  // The LED is the ERROR screen's only voice when the panel is missing.
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LED_OFF);

  // --- display first, so every later failure has somewhere to be shown ------
  // rd_begin() owns Wire on PIN_SDA/PIN_SCL, scans the bus and picks 0x3C/0x3D.
  // Risk 2: it is the ONLY place a U8G2 is constructed.
  // rd_begin() failing is no longer fatal (plan T10, audit risk 15). The boot
  // continues headless - drawing into a buffer nobody sends is harmless - and
  // the ERROR screen armed at the end of setup() blinks the LED and offers a
  // retry, so a loose cable costs a button press instead of a power cycle.
  g_display_ok = rd_begin() ? 1u : 0u;
  if (!g_display_ok) Serial.println(F("[nt] no display on the I2C bus"));
  ui_boot_screen(SCR_BOOT);       // S15: something on the panel before any I/O

  // --- entropy --------------------------------------------------------------
  // The ONE esp_random() call of the firmware (plan §1.4): it seeds every
  // rng.h stream before boot draws its RTC nonce and kv its canary pattern.
  // Without it every unit would hatch a bit-identical pet.
  rng_seed_all(esp_random());

  // --- how this boot started, then the store it will read -------------------
  boot_begin();                   // reads the RTC nonce BEFORE re-arming it
  g_nvs_ok = kv_begin() ? 1u : 0u;
  if (g_nvs_ok && !kv_selftest()) {
    g_nvs_ok = 0;                 // canary failed: run RAM-only, tell the user
  }

  // --- the save -------------------------------------------------------------
  ui_boot_screen(SCR_LOAD_SAVE);  // S16: the pipeline below owns the screen
  // The whole load pipeline runs here, before the clock: gt_begin() bootstraps
  // its estimate from save_last_seen() and its timezone from the persisted
  // config, and both are answers this call produces.
  save_set_clock(&clock_ms, &clock_epoch);
  const LoadResult load = gs_load(g_cfg);
  boot_note_save(gs_have_pebble());
  const BootKind boot = boot_kind();

  // The RTC mirror survives a crash that key "t" is up to 60 s behind.
  if (boot_rtc_last_seen() > save_last_seen()) {
    save_touch_lastseen(boot_rtc_last_seen());
  }
  g_boot_last_seen = save_last_seen();

  // --- clock ----------------------------------------------------------------
  gt_begin();                     // installs the TZ; never blocks, no radio

  // --- the pet --------------------------------------------------------------
  // Bind the ledger provider BEFORE the first pet write: boot_absence() forces
  // one, and that write is what retires a stale snapshot on a unit whose clock
  // is gone.
  gs_bind_gain(&gain_source);
  boot_box();

  // --- everything that reads Config or the pet ------------------------------
  input_begin();
  // THE PIEZO (P6-C1, decision D8). Bind first, begin second - audio_begin()
  // idles the pin THROUGH the bound sink, so the order is what makes PIN_PIEZO
  // quiet rather than floating from this line onwards. g_cfg is already loaded
  // by the time we get here, and the hook re-reads it on every cue, so a mute
  // toggled in SETTINGS needs nothing to be kept in step.
  audio_bind(audio_device_sink(), &app_audio_muted);
  audio_begin();
  net_begin();
  // The scan's per-device salt (spec section 44). gs_device_id() is drawn once
  // from RNG_MISC, is never 0 and is persisted, so the same access point hashes
  // differently on two units and identically across reboots on one. It must be
  // set before any scan runs; a salt of 0 is a usable hash but not a private
  // one. THE CONSEQUENCE, WRITTEN DOWN: a factory reset regenerates the device
  // id, so every armed cooldown goes stale at once - correct, a wiped device is
  // a new device, but it looks like a bug when nobody has said it.
  net_scan_salt_set(gs_device_id());
  // THE PER-BOOT HALF OF THE COOLDOWN TABLE (P5-C2, and P5-C3's obligation to
  // call it). The persisted rows come off flash with the rest of GameState;
  // this clears the RAM table an uncalibrated device falls back to, and the
  // dirty flag. Without it the fallback would carry whatever the .bss happened
  // to hold, and cd_take_dirty() could report a save nobody made.
  cd_begin();
  // THE PER-BOOT HALF OF THE ACTIVITY SCORE (P6-C2), and it is the same
  // sentence as the line above for the same reason: the day and the day's
  // total come off flash inside gs.cds, and this clears ONLY the term
  // counters, the two distinct sets and the dirty flag. game/activity.h says
  // plainly what a power cycle therefore still buys and what it cannot.
  act_begin();
  god_begin();

  ui_bind_recover(&app_recover_save);
  ui_bind_display_retry(&app_retry_display);
  ui_bind_led(&app_led);
  ui_bind_config(&g_cfg);
  web_bind_config(&g_cfg);
  ui_begin();
  (void)web_begin(WEB_PORT);

  apply_config();                 // contrast and WiFi credentials

  // --- splash, then the absence verdict over HOME ---------------------------
  rd_splash();
  boot_absence();

  if (!g_nvs_ok) {
    ui_toast(STR_ERR_NVS);
  } else if (boot == BOOT_FIRST_RUN) {
    ui_toast(STR_BOOT_FIRST);
  } else if (boot == BOOT_CRASH) {
    ui_toast(STR_BOOT_DIZZY);     // a crash is not an abandonment
  }

  // First boot: with no SNTP and no radio policy the device learns
  // the date from a human or not at all, so ask once, right here, before the
  // pet's first day starts running on an estimate. Every later visit is through
  // SETTINGS. Backing out is allowed - CAL_UNSET simply charges no absence.
  // The load's verdict wins the screen: a save this firmware refused to touch
  // is a question the user has to answer before anything else happens, and
  // nothing on the way there wipes it (audit risk 3).
  ui_note_load((uint8_t)load);

  if (!gs_readonly() && boot == BOOT_FIRST_RUN && gt_cal_state() == CAL_UNSET) {
    ui_goto(SCR_TIME);
  }

  // Last, so it wins the screen: with no panel there is nothing to read, and
  // every question above is moot until the user has a display again.
  if (!g_display_ok) ui_note_display_failure();

  // THE POWER LADDER, last: it must not be able to dim, blank or sleep the
  // device while the boot pipeline above is still putting questions on the
  // panel, and pwr_begin() starts it at PWR_ACTIVE with nothing applied.
  pwr_bind(&kPowerHooks);
  pwr_begin();

  g_tick_ms         = millis();
  g_input_ms        = g_tick_ms;      // the boot itself counts as activity
  g_wake_ms         = 0;
  g_wake_armed      = 0;
  g_clock_was_valid = gt_is_valid() ? 1u : 0u;

  Serial.printf("[nt] boot=%u nvs=%u load=%u slot=%u stage=%u free=%u\r\n",
                (unsigned)boot, (unsigned)g_nvs_ok, (unsigned)load,
                (unsigned)box_active(), (unsigned)sim_view()->stage,
                (unsigned)ESP.getFreeHeap());
}

// =============================================================================
//  1 Hz LOGIC TICK
//  The ONLY place the simulation advances. millis() here is scheduling, not
//  game time: how far the world moves is sim_step_seconds() (1 s, or the god
//  mode multiplier), exactly as the layering rule requires.
//
//  `owed` is how many WHOLE SECONDS of real time this call is being asked to
//  charge - 1 on an ordinary pass, more only when the CPU was stopped by the
//  power ladder (hardware/power.h) and the loop is settling up on the far side.
//  It multiplies the step, so every consumer below - the care sim, the XP
//  ledger, the carry drip, the activity score - sees one longer second rather
//  than a special case, which is exactly the shape god mode's speed multiplier
//  already put through here.
// =============================================================================
static void logic_tick(uint32_t owed)
{
  SimEnv env;
  build_env(env);
  sim_set_env(env);               // every tick, not once at boot

  if (owed == 0u) owed = 1u;
  const uint32_t step = sim_step_seconds() * owed;
  sim_tick(step);

  // XP from carried time (plan P3-C2): the ledger refills on real time, and a
  // Pebble that is awake and switched on earns for being carried. Both happen
  // BEFORE the event drain so a level-up reaches the UI in the same batch as
  // the tick that caused it.
  xp_ledger_tick(step);
  if (!sim_is_asleep()) {
    const uint16_t due = xp_carry_due(step);
    if (due != 0u) (void)app_award_xp(due, XP_SRC_CARRY);
    // THE ACTIVITY SCORE'S TIME TERM (P6-C2, spec section 25). The same
    // condition as the drip above - awake, switched on, being carried - and
    // deliberately the same XP bucket underneath, because the two measure the
    // same thing. game/activity.h writes down what that costs.
    (void)act_note_carried(gs_state().cds, step, app_act_clock());
  }
  app_pay_activity();

  const uint32_t ev = sim_take_events();
  ui_note_events(ev);

  gs_touch_lastseen(env.now_epoch);
  (void)gs_save_active(false);           // rate-limited by save_manager inside
  save_service();                        // flushes a write the 1 s floor deferred

  // The nvs2 checkpoint (D6). Daily, plus the events that change what the pet
  // IS. The plan's list is level-up / evolution / capture / trade; P3-C2 added
  // the first of them, P3-C3 checkpoints a real evolution from
  // app_evolve_active() above rather than from an event, and P5/P7 add capture
  // and trade at the same call.
  const bool grew = (ev & (SIM_EV_HATCHED | SIM_EV_STAGE_UP | SIM_EV_EVOLVE_MINOR |
                          SIM_EV_LEVEL_UP)) != 0;
  if (!gs_readonly()) {
    (void)save_checkpoint_service(env.now_epoch, grew);
  }

  // gt_set_epoch() landed after an unknown-clock boot: the boot charged nothing,
  // so charge the truth now. The edge is on gt_is_valid(),
  // which is exactly "gt_cal_state() left CAL_UNSET".
  const uint8_t clock_now = gt_is_valid() ? 1u : 0u;
  if (clock_now && !g_clock_was_valid) {
    if (g_absence_unknown) {
      g_absence_unknown = 0;
      uint32_t truth = 0;
      // g_boot_last_seen is only a wall clock if it reads like one.
      // On a device that has never been calibrated, gt_now() returns the uptime
      // (gametime.cpp seeds its estimate only from a >= NT_EPOCH_SANE_MIN
      // value), and logic_tick writes that uptime into NVS "t" every second.
      // Subtracting it from a freshly-set epoch yields ~55 YEARS, which would
      // be integrated as a 55-year absence at the exact moment the owner
      // finishes typing the date. Without a real baseline there is no truth to
      // charge, so charge nothing.
      if (g_boot_last_seen >= (uint32_t)NT_EPOCH_SANE_MIN) {
        truth = gt_elapsed_since(g_boot_last_seen, env.now_epoch);
      }
      sim_absence_retrofix(truth);
      ui_note_events(sim_take_events());
    }
  }
  g_clock_was_valid = clock_now;

  if (config_changed()) {
    apply_config();
  }
}

// =============================================================================
//  LOOP
//  Non-blocking throughout. Nothing below may stall the frame budget
//  (FRAME_BUDGET_US, 50 ms at 20 fps).
// =============================================================================
void app_loop(void)
{
  const uint32_t ms = millis();

  // --- 0. the power ladder's bookkeeping ------------------------------------
  pwr_note_loop(ms);              // DIAG's loop rate, counted against the rung

  // A light sleep that ended on a button, rather than on its own timer. The
  // panel was dark, so that press was aimed at the device: arm the swallow
  // window and count it as activity so the ladder climbs straight back up.
  if (pwr_take_wake()) {
    g_wake_ms    = ms;
    g_wake_armed = 1;
    g_input_ms   = ms;
  }
  if (g_wake_armed && (uint32_t)(ms - g_wake_ms) > NT_WAKE_SWALLOW_MS) {
    g_wake_armed = 0;             // nobody followed it up; stop swallowing
  }

  // --- 1. input -------------------------------------------------------------
  for (uint8_t i = 0; i < NT_GESTURES_PER_LOOP; ++i) {
    const Gesture g = input_poll();
    if (g == GST_NONE) {
      break;
    }
    g_input_ms = ms;              // the ladder's idle clock; see g_input_ms
    if (g_wake_armed) {
      g_wake_armed = 0;           // this is the press that did the waking
      continue;
    }
    ui_handle(g);
  }

  // --- 2. logic, at 1 Hz of REAL time --------------------------------------
  // WHOLE SECONDS OWED, CHARGED IN ONE CALL. Normally exactly one; more only
  // when the power ladder stopped the CPU for a slice, which is real elapsed
  // time and must reach the pet. NT_TICK_MAX_OWED_S is where a sleep stops
  // being a sleep and starts being a stall - see the macro.
  uint32_t owed = (uint32_t)(ms - g_tick_ms) / 1000UL;
  if (owed != 0u) {
    if (owed > (uint32_t)NT_TICK_MAX_OWED_S) {
      g_tick_ms = ms;             // long stall: resync, never burst
      owed      = 1u;
    } else {
      g_tick_ms += owed * 1000UL;
    }
    logic_tick(owed);
  }

  // --- 3. per-loop pumps that own presentation timing ----------------------
  ui_service();                   // auto-return, minigames, the hatch ceremony
  audio_service(ms);              // one tone step at most; never blocks (P6-C1)
  god_service();                  // soak log, serial genome paste

  // --- 4. the power ladder --------------------------------------------------
  // Before the render, so a rung entered on this pass owns this pass's frame
  // rate. `held` is the plan's "a screen holding a radio job counts as
  // activity": while a scan is in flight the ladder is clamped at DIM and
  // cannot reach the rung that drops the radio.
  PowerInput pin;
  pin.idle_ms = (uint32_t)(ms - g_input_ms);
  pin.held    = ui_radio_job_busy() ? 1u : 0u;
  (void)pwr_service(pin);

  // --- 5. render ------------------------------------------------------------
  // pwr_fps() CLAMPS what the screen asked for. It has to be applied here and
  // not set once by the ladder, because this line runs every pass and would
  // otherwise put the screen's rate straight back.
  rd_set_fps(pwr_fps(ui_fps()));
  const bool drew = rd_begin_frame();
  if (drew) {
    ui_draw();
    rd_end_frame();
  }

  // --- 6. radio ------------------------------------------------------------
  // NO POLICY HERE. The radio is OFF at boot and stays off:
  // the screen that needs it asks for it and releases it on the way out -
  // QR owns RADIO_WIFI, SOCIAL owns RADIO_BLE. net_service() only pumps
  // the state machine the screen put it in, including the settle timer that
  // replaced the blocking delay between the two stacks.
  net_service();
  web_service();
  ble_scan_service();

  // --- 7. yield -------------------------------------------------------------
  // AUDIT RISK 16: the loop must not run at 100 % duty cycle. Two rungs of one
  // rule, and both of them measure "how long until this loop owes anything",
  // which is the next 1 Hz logic tick.
  //
  //   ACTIVE / DIM  ->  delay(1). Cheap, keeps input_poll() well inside
  //                     INPUT_POLL_MS, and costs nothing to be wrong about.
  //   IDLE / SLEEP  ->  pwr_yield() STOPS THE CPU until the next tick or a
  //                     button. The wake is a hardware level interrupt on both
  //                     pins, so a press cannot be missed however long the
  //                     slice is - which is why this is better than polling
  //                     with a long delay(), not merely cheaper.
  //
  // The slice IS the tick cadence below DIM: IDLE's is one second, so the pet
  // goes on ticking at 1 Hz for a player who is probably still in the room;
  // SLEEP's is PWR_SLEEP_SLICE_MS, and the seconds it swallowed are charged in
  // one logic_tick(owed) on the far side. Nothing is lost either way, because
  // `owed` is computed from the clock and not from a count of passes.
  const uint32_t slice = pwr_slice_ms();
  if (slice != 0u) {
    (void)pwr_yield(slice);
  } else if (!drew && !pin.held) {
    delay(1);                     // no frame was due and no job is pending
  }
  // The `!pin.held` exception is the plan's own wording and it is worth naming
  // what it costs: while a scan is in flight ACTIVE and DIM spin without the
  // millisecond. It is bounded by WIFI_SCAN_TIMEOUT_MS (12 s) and it happens
  // with the radio already drawing far more than the core does, so it is the
  // cheap half of an expensive operation - but it is a spin, not a nap.
}
