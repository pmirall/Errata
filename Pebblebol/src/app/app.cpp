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
#include <esp_random.h>

#include "../core/config.h"
#include "../core/nt_types.h"
#include "../core/strings_es.h"
#include "../game/genome.h"
#include "../core/rng.h"
#include "../game/sim.h"
#include "../persistence/storage.h"
#include "../hardware/gametime.h"
#include "../hardware/input.h"
#include "../ui/render.h"
#include "../networking/net.h"
#include "../networking/ble_social.h"
#include "../ui/ui.h"
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
//  The .ino owns exactly two long-lived objects: the PetSave the simulation is
//  bound to and the Config every other module reads through a pointer. Both
//  must outlive setup(), hence file scope (and never a function-local static -
//  see trap 2 above).
// =============================================================================
static PetSave  g_pet;
static Config   g_cfg;

static uint32_t g_tick_ms        = 0;      // scheduler cursor for the 1 Hz tick
static uint32_t g_boot_last_seen = 0;      // store_last_seen() as found at boot
static uint16_t g_cfg_crc        = 0;      // change detector for Config
static uint8_t  g_absence_unknown = 0;     // boot took the unknown-clock path
static uint8_t  g_clock_was_valid = 0;     // edge detector for a landing calibration
static uint8_t  g_nvs_ok          = 0;

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

// True when someone rewrote Config since the last call. store_save_cfg() is
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
//  anti-farm budget and does no I/O, storage.cpp owns the NVS
//  key and the write cadence and knows nothing about game rules. This file is
//  the only place allowed to join them.
//
//  storage calls this at the instant it commits a "save", so the snapshot is
//  the live one - ui.cpp forces a save right after every successful action, and
//  the points that action spent have to be inside the blob that write produces.
// =============================================================================
static bool gain_source(uint8_t pts[NT_GAIN_SLOTS], uint32_t& epoch)
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
//  BOOT: PET
//  storage decides whether there is anything to load; genome/sim decide what a
//  fresh pet is. The .ino only sequences them.
// =============================================================================
static void boot_pet(void)
{
  if (store_load(g_pet)) {
    sim_init(g_pet);
    // sim_init() seeded the hourly gain budget to 0 - safe, but a lie
    // to anyone who just had a power cut ("esta lleno" at 19 % satiety for the
    // first minute, no full meal for 29 min). Replace it with what was actually
    // left at the last save, aged forward to the moment the device was last
    // alive. boot_absence()'s catch-up then refills the rest of the interval,
    // [last_seen, now], through its own gain_refill() - the two terms compose
    // into min(cap, saved + (now - saved_epoch) * cap / 3600), to within the
    // one milli-point that the two integer divisions can differ by.
    // A missing or corrupt blob leaves sim_init()'s 0 in place.
    uint8_t  gpts[NT_GAIN_SLOTS];
    uint32_t gepoch = 0;
    if (store_load_gain(gpts, gepoch)) {
      (void)sim_gain_restore(gpts, NT_GAIN_SLOTS, gepoch, g_pet.last_seen_epoch);
    }
    return;
  }
  // No save, foreign version or a failed CRC: a brand new generation-0 egg
  // rather than a garbage pet.
  sim_init(g_pet);
  sim_new_pet(genome_genesis(), gt_now(), 0);
}

// =============================================================================
//  BOOT: ABSENCE
//  sim_catch_up_ex() reads SimEnv, so the environment must
//  already be pushed in. With no trustworthy clock (gt_cal_state() == CAL_UNSET)
//  it takes the unknown-clock path, which charges ZERO (plan section 1.7), and
//  we arm the retro-fix for the moment gt_set_epoch() lands.
//
//  "gt_is_valid() is false" alone is NOT evidence of an absence. The
//  crash-vs-abandonment discriminator storage.cpp already computes is the
//  missing input: BOOT_FIRST_RUN has nobody to have abandoned, and BOOT_CRASH /
//  BOOT_SOFT_RESET are explicitly not absences (the same reason the toast below
//  says "dizzy" rather than "abandoned"). On top of that the baseline itself
//  has to be a real wall clock: on a never-calibrated device store_last_seen()
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
    const BootKind bk       = store_boot_kind();
    const bool     real_gap = (g_boot_last_seen >= (uint32_t)NT_EPOCH_SANE_MIN) &&
                              (absence_s != 0u);
    if (bk == BOOT_FIRST_RUN || bk == BOOT_CRASH || bk == BOOT_SOFT_RESET ||
        !real_gap) {
      known      = 1;     // nothing was abandoned: charge the true zero
      absence_s  = 0;
    }
  }

  AbsenceReport rep;
  sim_catch_up_ex(absence_s, known, rep);
  g_absence_unknown = rep.clock_known ? 0u : 1u;

  ui_note_absence(rep);
  ui_note_events(sim_take_events());
  (void)store_save(g_pet, true);
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

  // --- display first, so every later failure has somewhere to be shown ------
  // rd_begin() owns Wire on PIN_SDA/PIN_SCL, scans the bus and picks 0x3C/0x3D.
  // Risk 2: it is the ONLY place a U8G2 is constructed.
  if (!rd_begin()) {
    rd_fatal(S(STR_ERR_OLED));    // noreturn: draws, logs and blinks forever
  }

  // --- entropy --------------------------------------------------------------
  // The ONE esp_random() call of the firmware (plan §1.4): it seeds every
  // rng.h stream before storage draws its RTC nonce and canary pattern.
  // Without it every unit would hatch a bit-identical pet.
  rng_seed_all(esp_random());

  // --- persistence ----------------------------------------------------------
  g_nvs_ok = store_begin() ? 1u : 0u;
  if (g_nvs_ok && !store_selftest()) {
    g_nvs_ok = 0;                 // canary failed: run RAM-only, tell the user
  }
  const BootKind boot = store_boot_kind();
  g_boot_last_seen    = store_last_seen();

  // --- settings + clock -----------------------------------------------------
  (void)store_load_cfg(g_cfg);    // false only means "compiled-in defaults"
  gt_begin();                     // installs the TZ; never blocks, no radio

  // --- the pet --------------------------------------------------------------
  // Bind the ledger provider BEFORE the first store_save(): boot_absence()
  // forces one, and that write is what retires a stale snapshot on a unit
  // whose clock is gone.
  store_bind_gain(&gain_source);
  boot_pet();

  // --- everything that reads Config or the pet ------------------------------
  input_begin();
  net_begin();
  god_begin();

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
  if (boot == BOOT_FIRST_RUN && gt_cal_state() == CAL_UNSET) {
    ui_goto(SCR_CLOCK);
  }

  g_tick_ms        = millis();
  g_clock_was_valid = gt_is_valid() ? 1u : 0u;

  Serial.printf("[nt] boot=%u nvs=%u stage=%u free=%u\r\n",
                (unsigned)boot, (unsigned)g_nvs_ok,
                (unsigned)g_pet.stage, (unsigned)ESP.getFreeHeap());
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

  sim_tick(sim_step_seconds());
  ui_note_events(sim_take_events());

  store_touch_lastseen(env.now_epoch);
  (void)store_save(g_pet, false); // rate-limited to SAVE_FULL_PERIOD_S inside

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
