// =============================================================================
//  PEBBLEBOL - Pebblebol.ino (formerly Nottamagochi sketch_aug30b.ino)
//  ENTRY POINT ONLY (BRIEF section 4, row 1). setup() wires the modules in
//  dependency order; loop() pumps them. There is NO game logic in this file:
//  every rule lives in sim.cpp, every pixel in render.cpp/ui.cpp, every socket
//  in net.cpp/webui.cpp.
//
//  TWO TRAPS THIS FILE IS BUILT AROUND (AUDIT 13 + AUDIT 2)
//  --------------------------------------------------------
//  1. arduino-cli runs ctags over the .ino and injects the prototypes of every
//     function defined here ABOVE the first function definition. If any
//     #include sat below a function, `const PetSave* nt_pet_view();` would be
//     emitted before nt_types.h and the build would die with
//     "'PetSave' does not name a type". EVERY #include is therefore at the very
//     top of the file, before the first definition. Do not move them.
//  2. ctags mis-tags a function whose opening brace shares a line with a
//     `static` local: it hoists a `static` prototype, silently giving the
//     definition INTERNAL linkage. For nt_pet_view()/nt_cfg_view() that would
//     leave telegram.cpp bound to its weak nullptr stubs with no diagnostic
//     whatsoever. No function below opens its body on the same line as any
//     declaration, and this file uses no function-local statics at all.
//
//  Identifiers and comments English; every user-facing byte comes from
//  strings_es.h.
// =============================================================================

#include <Arduino.h>
#include <esp_random.h>

#include "config.h"
#include "nt_types.h"
#include "strings_es.h"
#include "genome.h"
#include "sim.h"
#include "storage.h"
#include "gametime.h"
#include "input.h"
#include "render.h"
#include "net.h"
#include "weather.h"
#include "telegram.h"
#include "ble_social.h"
#include "ui.h"
#include "webui.h"
#include "godmode.h"
// index_html.h is deliberately NOT included: webui.cpp is its single translation
// unit (AUDIT 10 - a second inclusion doubles 46 KB of .rodata).

// -----------------------------------------------------------------------------
//  Build-time policy: is there any reason at all to power the WiFi stack?
// -----------------------------------------------------------------------------
#define NT_WANT_WIFI (FEATURE_WEB || FEATURE_WEATHER || FEATURE_TELEGRAM)

// How often the radio policy retries after the stack fell back to RADIO_OFF.
#define NT_WIFI_RETRY_MS   30000UL

// Guard rail for the 1 Hz scheduler: a stall longer than this (a blocking TLS
// send, a long offline catch-up) resynchronises instead of firing a burst of
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
static uint32_t g_wifi_try_ms    = 0;      // last radio-policy attempt
static uint32_t g_boot_last_seen = 0;      // store_last_seen() as found at boot
static uint16_t g_cfg_crc        = 0;      // change detector for Config
static uint8_t  g_absence_unknown = 0;     // boot took the ABS_UNKNOWN path
static uint8_t  g_clock_was_valid = 0;     // edge detector for SNTP landing
static uint8_t  g_nvs_ok          = 0;

// =============================================================================
//  INTEGRATION SEAM (telegram.h:97-98)
//  telegram.cpp ships __attribute__((weak)) definitions returning nullptr so it
//  links standalone. These strong definitions override them. The signatures are
//  copied verbatim from the header on purpose: any drift is a hard error there,
//  never a silently mute Telegram module.
// =============================================================================
const PetSave* nt_pet_view(void)
{
  return sim_save();
}

const Config* nt_cfg_view(void)
{
  return &g_cfg;
}

// =============================================================================
//  CONFIG SIDE EFFECTS
//  Config is shared by pointer with ui (S9 SETTINGS) and webui (POST /api/cfg),
//  so it can change under us from two directions. Rather than have both of them
//  call back, the tick watches the struct's own CRC and re-applies the three
//  settings that live outside the struct: OLED contrast, Telegram mode and the
//  WiFi credentials. Everything else is read straight from g_cfg by its owner.
// =============================================================================
static void apply_config(void)
{
  ui_note_brightness(g_cfg.brightness);

  TgMode tg = TG_OFF;
  if (g_cfg.tg_mode < (uint8_t)TG_MODE_COUNT) {
    tg = (TgMode)g_cfg.tg_mode;
  }
  tg_set_mode(tg);

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
//  sim owns no clock, no RNG and no weather (sim.h:13-17): the entry point
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

  // GAME_DESIGN 10.2: above x1 the sleep window is frozen, otherwise the pet
  // flickers in and out of it many times per second.
  uint8_t fh = 0;
  uint8_t fm = 0;
  if (god_freeze_clock(fh, fm)) {
    env.local_hour = fh;
    env.local_min  = fm;
  }

  const WeatherState& w = wx_state();
  const uint8_t temper  = gene_temperament(g_pet.genome);

  env.wx_group        = w.group;
  env.wx_app_dc       = w.app_dc;
  env.wx_hunger_x1000 = wx_mult_hunger();
  env.wx_happy_x1000  = wx_mult_hap(temper, sim_stat_pct(ST_BOND));
  env.wx_energy_x1000 = wx_mult_en();
  env.wx_sick_pph     = wx_sick_bonus_pph();
  env.wx_mood_off     = wx_mood_offset(temper);
}

// =============================================================================
//  RADIO POLICY
//  net.cpp owns every WiFi/BLE lifecycle call; this decides only WHETHER we
//  want the station up. ui.cpp owns RADIO_BLE for S8 SOCIAL and restores the
//  previous mode when it leaves, so the policy never fights it: it acts only
//  from RADIO_OFF and never while the social screen is open.
// =============================================================================
static bool wifi_wanted(void)
{
#if !NT_WANT_WIFI
  return false;
#else
  if (FEATURE_WEB && (g_cfg.flags & CF_WEB_ENABLED)) {
    return true;
  }
  if (FEATURE_WEATHER && (g_cfg.flags & CF_WX_ENABLED)) {
    return true;
  }
  if (FEATURE_TELEGRAM && g_cfg.tg_mode != (uint8_t)TG_OFF) {
    return true;
  }
  return false;
#endif
}

static void radio_policy(uint32_t ms)
{
  if (!wifi_wanted()) {
    // PH3 #7: nothing wants the station any more (WEB and WEATHER off in S9,
    // Telegram OFF). Give the ~50 KB and the radio back instead of holding
    // them powered until the next reboot - on a battery-bound device the
    // station is the single largest current draw. ui.cpp owns RADIO_BLE while
    // S8 is open, so never fight it there; net.cpp restores the previous mode
    // when S8 closes. Testing RADIO_WIFI (not != RADIO_OFF) is what keeps this
    // off the BLE stack.
    if (net_mode() == RADIO_WIFI && ui_screen() != SCR_SOCIAL) {
      (void)net_request(RADIO_OFF);
    }
    return;
  }
  if (net_mode() != RADIO_OFF) {
    return;                       // WIFI already up, or ui owns the BLE stack
  }
  if (ui_screen() == SCR_SOCIAL) {
    return;                       // S8 is between two BLE sessions; keep out
  }
  if (g_wifi_try_ms != 0 && (uint32_t)(ms - g_wifi_try_ms) < NT_WIFI_RETRY_MS) {
    return;
  }
  g_wifi_try_ms = ms;
  (void)net_request(RADIO_WIFI);  // failure is reported through net_last_err()
}

// =============================================================================
//  HOURLY-GAIN LEDGER SEAM  (PH3 finding 4 / PH4 section 6 item 1)
//  Two modules, neither of which may know about the other: sim.cpp owns the
//  anti-farm budget and does no I/O (BRIEF section 4), storage.cpp owns the NVS
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
  // No trustworthy wall clock: before SNTP lands gt_now() - and therefore
  // sim_now() - is a seconds-since-boot counter, and a difference between two
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
    // PH4 6.1. sim_init() seeded the hourly gain budget to 0 - safe, but a lie
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
  // rather than a garbage pet (BRIEF 6.4).
  sim_init(g_pet);
  sim_new_pet(genome_genesis(), gt_now(), 0);
}

// =============================================================================
//  BOOT: ABSENCE
//  GAME_DESIGN 5.1-5.3. sim_catch_up_ex() reads SimEnv, so the environment must
//  already be pushed in. With no trustworthy clock it takes the ABS_UNKNOWN
//  path and we arm the retro-fix for the moment SNTP lands.
//
//  PH3 #1: "gt_is_valid() is false" alone is NOT evidence of an absence, and
//  handing sim_catch_up_ex() a bare 0 there made it substitute the 6 h
//  ABSENCE_LARGA_S floor - on the very first boot of a brand new device, and
//  again on every reboot of any unit without a working clock, which is a fully
//  supported configuration (CF_WEB_ENABLED / CF_WX_ENABLED are user-togglable
//  and Telegram can be off). The crash-vs-abandonment discriminator storage.cpp
//  already computes is the missing input: BOOT_FIRST_RUN has nobody to have
//  abandoned, and BOOT_CRASH / BOOT_SOFT_RESET are explicitly not absences
//  (the same reason the toast below says "dizzy" rather than "abandoned").
//  On top of that the baseline itself has to be a real wall clock: on a
//  never-synced device store_last_seen() returns the PREVIOUS boot's uptime,
//  which cannot describe a gap at all.
// =============================================================================
static void boot_absence(void)
{
  SimEnv env;
  build_env(env);
  sim_set_env(env);

  const uint32_t now  = env.now_epoch;
  uint32_t absence_s  = 0;
  if (g_boot_last_seen != 0 && now > g_boot_last_seen) {
    absence_s = now - g_boot_last_seen;
  }

  uint8_t known = gt_is_valid() ? 1u : 0u;
  if (!known) {
    const BootKind bk       = store_boot_kind();
    const bool     real_gap = (g_boot_last_seen >= (uint32_t)NT_EPOCH_SANE_MIN) &&
                              (now > g_boot_last_seen);
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
void setup()
{
  // USB CDC. NEVER wait for the host to enumerate (BRIEF 1.7): a pet that only
  // runs when a laptop is attached is not a pet.
  Serial.begin(115200);
  Serial.println();
  Serial.println(F("[nt] " FW_NAME " " FW_VERSION));

  // --- display first, so every later failure has somewhere to be shown ------
  // rd_begin() owns Wire on PIN_SDA/PIN_SCL, scans the bus and picks 0x3C/0x3D.
  // BRIEF risk 2: it is the ONLY place a U8G2 is constructed.
  if (!rd_begin()) {
    rd_fatal(S(STR_ERR_OLED));    // noreturn: draws, logs and blinks forever
  }

  // --- persistence ----------------------------------------------------------
  g_nvs_ok = store_begin() ? 1u : 0u;
  if (g_nvs_ok && !store_selftest()) {
    g_nvs_ok = 0;                 // canary failed: run RAM-only, tell the user
  }
  const BootKind boot = store_boot_kind();
  g_boot_last_seen    = store_last_seen();

  // --- entropy --------------------------------------------------------------
  // AUDIT 15: without this every unit hatches a bit-identical pet.
  genome_set_rng(&esp_random);
  sim_seed(esp_random());

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
  wx_bind_config(&g_cfg);         // PH3 #9: one Config object, one writer.
  wx_begin();                     // must follow the bind: it seeds the coords
  tg_begin();
  god_begin();

  ui_bind_config(&g_cfg);
  web_bind_config(&g_cfg);
  ui_begin();
  (void)web_begin(WEB_PORT);

  apply_config();                 // contrast, Telegram mode, WiFi credentials

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

  g_tick_ms        = millis();
  g_wifi_try_ms    = 0;
  g_clock_was_valid = gt_is_valid() ? 1u : 0u;

  Serial.printf("[nt] boot=%u nvs=%u stage=%u free=%u\r\n",
                (unsigned)boot, (unsigned)g_nvs_ok,
                (unsigned)g_pet.stage, (unsigned)ESP.getFreeHeap());
}

// =============================================================================
//  1 Hz LOGIC TICK
//  The ONLY place the simulation advances. millis() here is scheduling, not
//  game time: how far the world moves is sim_step_seconds() (1 s, or the god
//  mode multiplier), exactly as the BRIEF section 4 layering rule requires.
// =============================================================================
static void logic_tick(void)
{
  SimEnv env;
  build_env(env);
  sim_set_env(env);               // AUDIT 15: every tick, not once at boot

  sim_tick(sim_step_seconds());
  ui_note_events(sim_take_events());

  store_touch_lastseen(env.now_epoch);
  (void)store_save(g_pet, false); // rate-limited to SAVE_FULL_PERIOD_S inside

  // SNTP landed after an ABS_UNKNOWN boot: charge the difference between the
  // AUSENCIA_LARGA floor already applied and the truth (GAME_DESIGN 5.1).
  const uint8_t clock_now = gt_is_valid() ? 1u : 0u;
  if (clock_now && !g_clock_was_valid) {
    if (g_absence_unknown) {
      g_absence_unknown = 0;
      uint32_t truth = 0;
      // PH3 #2: g_boot_last_seen is only a wall clock if it reads like one.
      // On a device that has never met NTP, gt_now() returns the uptime
      // (gametime.cpp seeds its estimate only from a >= NT_EPOCH_SANE_MIN
      // value), and logic_tick writes that uptime into NVS "t" every second.
      // Subtracting it from a freshly-landed SNTP epoch yields ~55 YEARS,
      // which tier_for() resolves to ABS_GRAVE: -40 health on top of the
      // LARGA floor plus the permanent, inheritable PF_SCAR, at the exact
      // moment the owner finishes provisioning WiFi. Without a real baseline
      // there is no truth to charge, so charge nothing.
      if (g_boot_last_seen >= (uint32_t)NT_EPOCH_SANE_MIN &&
          env.now_epoch > g_boot_last_seen) {
        truth = env.now_epoch - g_boot_last_seen;
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
//  (FRAME_BUDGET_US, 50 ms at 20 fps); the one call that can legitimately take
//  seconds is tg_service(), which gates itself behind an idle window.
// =============================================================================
void loop()
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
  ui_service();                   // auto-return, minigames, death staging
  god_service();                  // soak log, serial paste, synthetic BLE

  // --- 4. render ------------------------------------------------------------
  rd_set_fps(ui_fps());
  if (rd_begin_frame()) {
    ui_draw();
    rd_end_frame();
  }

  // --- 5. radio ------------------------------------------------------------
  // Everything below this line may block for seconds (tg_service() owns a TLS
  // handshake worth 1-3 s, up to 8 s on timeouts), and a register effect can
  // normally only be cleared by the next rd_end_frame(). So KILL the transients
  // here, live ones included, not just the expired ones: at every hatch the
  // ceremony arms rd_flash() and rd_shake() one frame before the MSG_P04 that
  // the same hatch queued at PRIO_P0 blocks the loop, and an expired-only sweep
  // runs microseconds after the arm, when nothing has expired yet. The cost is
  // that a flash or a shake lasts the one frame that applied it; the bug it
  // replaces is a panel left inverted or shifted for seconds.
  rd_fx_settle_now();

  radio_policy(ms);
  net_service();
  if (net_is_sta_up()) {
    gt_sync_start();              // idempotent + self-rate-limiting
  }
  web_service();
  tg_service();
  wx_poll();
  ble_scan_service();

  // --- 6. cross-module notifications ---------------------------------------
  // An action taken on the phone. webui.cpp mutates the pet through
  // sim_apply_action() directly, so before this drain existed a remote feed
  // moved the bars and the panel showed nothing at all - the QR feature's
  // natural ending (feed it from your phone, watch it eat on the device) was
  // simply missing. Same shape as the web_cfg_dirty() drain below it, and the
  // "before" copy is what ACT_CLEAN needs to have anything left to dissolve.
  {
    PetSave       before;
    const uint8_t web_act = web_take_action(&before);
    if (web_act != ACT_NONE) ui_note_web_action(web_act, before);
  }

  if (web_cfg_dirty()) {
    web_cfg_clear_dirty();
    if (config_changed()) {
      apply_config();
    }
    ui_toast(STR_SET_SAVED);
  }
}
