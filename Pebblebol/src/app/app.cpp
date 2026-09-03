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
#include "../game/sim.h"
#include "../persistence/game_state.h"
#include "../game/box.h"
#include "../game/xp.h"
#include "../data/species_table.h"
#include "../persistence/save_manager.h"
#include "../hardware/boot.h"
#include "../hardware/kv_nvs.h"
#include "../hardware/gametime.h"
#include "../hardware/input.h"
#include "../ui/render.h"
#include "../networking/net.h"
#include "../networking/ble_social.h"
#include "../ui/ui.h"
#include "../ui/screen_error.h"   // the ERROR screen's retry / LED bindings
#include "../networking/webui.h"
#include "../dev/godmode.h"
// data/index_html.h is deliberately NOT included: networking/webui.cpp is its
// single translation unit (a second inclusion doubles the page blob in .rodata).

// Guard rail for the 1 Hz scheduler: a stall longer than this (a long offline
// catch-up, an NVS write storm) resynchronises instead of firing a burst of
// catch-up ticks.
#define NT_TICK_RESYNC_MS   4000UL

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

// =============================================================================
//  THE TWO CLOCKS persistence/save_manager.cpp CANNOT COMPUTE ITSELF
//  A monotonic millisecond counter for the flash-wear filter, and the wall
//  clock for saved_epoch. Injecting them is what keeps the save policy pure and
//  host-testable (save_manager.h).
// =============================================================================
static uint32_t clock_ms(void)    { return millis(); }
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

  net_set_credentials(g_cfg.wifi_ssid, g_cfg.wifi_pass);

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
//  Only the ledger DECREASE is persisted. A refill needs no blob: it is
//  reconstructed from the elapsed time by xp_ledger_restore(), which is the
//  same composition the hourly gain budget uses above.
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
    // The XP ledger is the same composition over the same interval. Without a
    // trustworthy snapshot it stays at the zero xp_ledger_reset(0) seeded:
    // unkind for an hour, but the only direction that cannot be farmed by
    // power-cycling the device after spending the budget.
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
    if (bk == BOOT_FIRST_RUN || bk == BOOT_CRASH || bk == BOOT_SOFT_RESET ||
        !real_gap) {
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
  net_begin();
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

  g_tick_ms        = millis();
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
// =============================================================================
static void logic_tick(void)
{
  SimEnv env;
  build_env(env);
  sim_set_env(env);               // every tick, not once at boot

  const uint32_t step = sim_step_seconds();
  sim_tick(step);

  // XP from carried time (plan P3-C2): the ledger refills on real time, and a
  // Pebble that is awake and switched on earns for being carried. Both happen
  // BEFORE the event drain so a level-up reaches the UI in the same batch as
  // the tick that caused it.
  xp_ledger_tick(step);
  if (!sim_is_asleep()) {
    const uint16_t due = xp_carry_due(step);
    if (due != 0u) (void)app_award_xp(due, XP_SRC_CARRY);
  }

  const uint32_t ev = sim_take_events();
  ui_note_events(ev);

  gs_touch_lastseen(env.now_epoch);
  (void)gs_save_active(false);           // rate-limited by save_manager inside
  save_service();                        // flushes a write the 1 s floor deferred

  // The nvs2 checkpoint (D6). Daily, plus the events that change what the pet
  // IS. The plan's list is level-up / evolution / capture / trade; P3-C2 added
  // the first of them, evolution is still spelled as a stage transition until
  // P3-C3, and P5/P7 add capture and trade at the same call.
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

  // --- 1. input -------------------------------------------------------------
  for (uint8_t i = 0; i < NT_GESTURES_PER_LOOP; ++i) {
    const Gesture g = input_poll();
    if (g == GST_NONE) {
      break;
    }
    ui_handle(g);
  }

  // --- 2. logic, at 1 Hz of REAL time --------------------------------------
  if ((uint32_t)(ms - g_tick_ms) >= 1000UL) {
    g_tick_ms += 1000UL;
    if ((uint32_t)(ms - g_tick_ms) >= NT_TICK_RESYNC_MS) {
      g_tick_ms = ms;             // long stall: resync, never burst
    }
    logic_tick();
  }

  // --- 3. per-loop pumps that own presentation timing ----------------------
  ui_service();                   // auto-return, minigames, the hatch ceremony
  god_service();                  // soak log, serial genome paste

  // --- 4. render ------------------------------------------------------------
  rd_set_fps(ui_fps());
  if (rd_begin_frame()) {
    ui_draw();
    rd_end_frame();
  }

  // --- 5. radio ------------------------------------------------------------
  // NO POLICY HERE. The radio is OFF at boot and stays off:
  // the screen that needs it asks for it and releases it on the way out -
  // QR owns RADIO_WIFI, SOCIAL owns RADIO_BLE. net_service() only pumps
  // the state machine the screen put it in, including the settle timer that
  // replaced the blocking delay between the two stacks.
  net_service();
  web_service();
  ble_scan_service();
}
