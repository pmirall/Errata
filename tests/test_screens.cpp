// =============================================================================
//  Pebblebol host tests - test_screens.cpp
//  The spec section 63 gate: every migrated screen is rendered at the REAL
//  128x64 into tests/fakes/gfx_fb.cpp and then
//    (a) asserted to have made ZERO out-of-bounds drawing calls, and
//    (b) compared byte for byte against tests/golden/screens/<name>.pbm.
//
//  ./bin/test_screens --record rewrites the goldens; do that only from a
//  commit that means to change what a screen looks like, and read the diff.
//
//  P2-C11a migrated BOOT, LOAD_SAVE and ERROR; P2-C11b added HOME, MENU, the
//  CARE and PLAY lists, both PEBBLE pages, SETTINGS (list and "Acerca de") and
//  the TIME entry screen; P2-C11c added LINK, EVOLUTION, DIAG, CREATOR and the
//  CONFIRM / ALERT / HELP overlays. Every later screen adds its fixtures here.
//
//  TWO FIXTURES render on every screen that shows a Pebble: a fresh starter,
//  and a MAXED one whose nickname is the full twelve characters the schema
//  allows and whose every stat is at 100. The second one is the layout test:
//  a 12-character name beside a two-digit level, six three-digit percentages
//  and a full HP fraction is the widest this UI can ever be asked to draw, and
//  fb_oob() == 0 on it is the promise that it still fits a 128x64 panel.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "core/nt_types.h"
#include "core/strings_es.h"
#include "data/sprites.h"      // POSE_IDLE, for the body fixture
#include "data/balance.h"      // XP_TABLE: what a level costs, for the HOME bar
#include "fakes/gfx_fb.h"
#include "game/box.h"
#include "minigames/games/games.h"
#include "minigames/minigame.h"
#include "persistence/save_manager.h"
#include "ui/dialog.h"
#include "ui/screen.h"
#include "ui/screen_care.h"
#include "ui/screen_creator.h"
#include "ui/screen_diag.h"
#include "ui/screen_evolution.h"
#include "ui/screen_error.h"
#include "ui/screen_home.h"
#include "ui/screen_encounter.h"
#include "ui/screen_link.h"
#include "ui/screen_menu.h"
#include "ui/screen_network.h"
#include "networking/wifi_scanner.h"
#include "game/cooldowns.h"
#include "game/genome.h"
#include "game/inventory.h"
#include "game/xp.h"
#include "game/battle.h"      // BattleReject / BattleLogEvent, for the battle snapshots
#include "ui/screen_battle.h"
#include "ui/battle_renderer.h"
#include "ui/screen_box.h"
#include "ui/screen_settings.h"
#include "ui/screen_soon.h"
#include "ui/screen_status.h"
#include "ui/screen_time.h"
#include "ui/screen_view.h"
#include "ui/ui.h"

// =============================================================================
//  The ui.cpp seams screen_error.cpp calls. On the device they are the modal
//  layer, the toast line and the navigation; here they are a recording.
// =============================================================================
static uint16_t g_toast   = STR_EMPTY;
static uint8_t  g_goto    = 0xFF;
static int      g_wipe    = 0;
static int      g_recovered = 0;

void ui_toast(uint16_t str_id)   { g_toast = str_id; }
void ui_goto(ScreenId s)         { g_goto = (uint8_t)s; }
void ui_confirm_wipe(void)       { g_wipe++; }
void ui_note_recovered(void)     { g_recovered++; }

static void seams_reset(void) {
  g_toast = STR_EMPTY;
  g_goto  = 0xFF;
  g_wipe  = 0;
  g_recovered = 0;
}


// =============================================================================
//  The rest of the ui.cpp seams the P2-C11b screens call. Same idea: on the
//  device they are the clock, the navigation machine, the modal layer and the
//  simulation; here they are a recording the tests can read and steer.
// =============================================================================
static uint32_t g_now      = 0;
static uint32_t g_idle     = 0;
static uint8_t  g_push     = 0xFF;
static int      g_backs    = 0;
static int      g_inputs   = 0;
static uint16_t g_help     = STR_EMPTY;
static int      g_medicine = 0;
static uint8_t  g_action   = ACT_NONE;      // the last ui_do_action()
static uint8_t  g_shown    = ACT_NONE;      // the last ui_act_and_show()
static bool     g_action_ok = true;
static uint8_t  g_minigame = 0xFF;
static uint8_t  g_god      = 0;
static uint8_t  g_bright   = 0;
static int      g_cfg_saves = 0;
static int      g_flushes  = 0;
static Config   g_cfg;
static Config*  g_cfg_p    = &g_cfg;
static bool     g_clock_known = false;
static bool     g_clock_ok = true;
static uint16_t g_set_y = 0;
static uint8_t  g_set_mo = 0, g_set_d = 0, g_set_h = 0, g_set_mi = 0;
static int      g_frames   = 0;
static int      g_hatches  = 0;
static int      g_radio    = -1;      // the last ui_creator_radio() argument
static int      g_radio_calls = 0;
static uint8_t  g_ap_up    = 0;
// g_sta_up went with CreatorInfo.sta_up at P5-C1: the station path is deleted,
// so "joined the user's network" is not a state this firmware can be in.
static int      g_commits  = 0;
static uint8_t  g_commit_id = CFM_NONE;
static int      g_wiggles   = 0;
static uint8_t  g_battle_entry = 0xFF;
static int      g_battle_starts = 0;
static uint8_t  g_result_entry = 0xFF;
static uint8_t  g_result_won   = 0xFF;
static int      g_results      = 0;
static uint8_t  g_hold_fps     = 0;
static int      g_hold_calls   = 0;
static int      g_flashes      = 0;
static int      g_shakes       = 0;
static uint8_t  g_box_active   = 0xFF;
static uint8_t  g_box_released = 0xFF;
static uint8_t  g_box_swap_a   = 0xFF;
static uint8_t  g_box_swap_b   = 0xFF;

uint32_t ui_now_ms(void)  { return g_now; }
uint32_t ui_idle_ms(void) { return g_idle; }
void ui_push(ScreenId s)  { g_push = (uint8_t)s; }
void ui_back(void)        { g_backs++; }
void ui_note_input(void)  { g_inputs++; }
Config* ui_cfg(void)      { return g_cfg_p; }
void ui_cfg_changed(void) { g_cfg_saves++; }
void ui_apply_brightness(uint8_t c) { g_bright = c; }
bool ui_do_action(uint8_t a)      { g_action = a; return g_action_ok; }
bool ui_act_and_show(uint8_t a)   { g_shown  = a; return g_action_ok; }
void ui_help(uint16_t id)         { g_help = id; }
void ui_confirm_medicine(void)    { g_medicine++; }
void ui_start_minigame(uint8_t i) { g_minigame = i; }
// The P4-C4 battle seams. The screen is pure and runs the REAL engine here, so
// these are the only three things it cannot do for itself: draw a seed, award
// XP, and reach render.h's frame-level effects.
void ui_start_battle(uint8_t e)          { g_battle_entry = e; g_battle_starts++; }
void ui_battle_result(uint8_t e, uint8_t won) {
  g_result_entry = e; g_result_won = won; g_results++;
}
void ui_hold_fps(uint8_t f, uint16_t)    { g_hold_fps = f; g_hold_calls++; }
void ui_flash(uint16_t)                  { g_flashes++; }
void ui_shake(uint8_t, uint16_t)         { g_shakes++; }
uint8_t ui_god_progress(void)     { return g_god; }
void ui_input_flush(void)         { g_flushes++; }
void ui_home(void)                { g_goto = (uint8_t)SCR_HOME; }
void ui_request_frame(void)       { g_frames++; }
void ui_request_hatch(void)       { g_hatches++; }
void ui_creator_radio(bool on)    { g_radio = on ? 1 : 0; g_radio_calls++; }
void ui_wiggle(void)              { g_wiggles++; }

// The BOX seams: the screen reads game/box.h directly and every WRITE goes
// through ui.cpp, so here they are a recording (P2-C11d).
void ui_box_activate(uint8_t slot)      { g_box_active = slot; }
void ui_box_swap(uint8_t a, uint8_t b)  { g_box_swap_a = a; g_box_swap_b = b; }
void ui_box_release(uint8_t slot)       { g_box_released = slot; }

// SCR_GAME's five hooks are still ui.cpp's until P3-C4; the table names them,
// so this binary has to define them. Nothing here renders a minigame.
void ui_game_enter(void)          { }
void ui_game_update(uint32_t)     { }
void ui_game_render(void)         { }
void ui_game_input(Gesture)       { }
void ui_game_leave(void)          { }

void ui_creator_info(CreatorInfo& out) {
  memset(&out, 0, sizeof out);
  out.ap_up  = g_ap_up;
  out.pin    = 1234;
  snprintf(out.ssid, sizeof out.ssid, "PEBBLEBOL-1234");
  snprintf(out.ip,   sizeof out.ip,   "192.168.4.1");
  if (g_ap_up) snprintf(out.url, sizeof out.url, "http://192.168.4.1/?k=1234");
}
bool ui_btn_down(uint8_t)         { return false; }
uint32_t ui_btn_hold_ms(uint8_t)  { return 0; }

void ui_info_lines(char lines[UI_INFO_LINES][UI_INFO_CAP]) {
  // Fixed text: the real ones carry a heap figure and an IP, neither of which
  // a golden could ever be stable about.
  snprintf(lines[0], UI_INFO_CAP, "PEBBLEBOL 0.2.0");
  snprintf(lines[1], UI_INFO_CAP, "IP 0.0.0.0  rssi 0");
  snprintf(lines[2], UI_INFO_CAP, "PIN 1234  spr rev 1");
  snprintf(lines[3], UI_INFO_CAP, "heap 200000  nvs 00");
  snprintf(lines[4], UI_INFO_CAP, "age 3 d 4 h");
}

bool ui_get_clock(uint16_t* y, uint8_t* mo, uint8_t* d, uint8_t* h, uint8_t* mi) {
  if (!g_clock_known) return false;
  *y = 2024; *mo = 2; *d = 29; *h = 7; *mi = 5;
  return true;
}

bool ui_set_clock(uint16_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t mi) {
  g_set_y = y; g_set_mo = mo; g_set_d = d; g_set_h = h; g_set_mi = mi;
  return g_clock_ok;
}

// =============================================================================
//  THE EXPLORATION SEAMS (P5-C3/C4), AND THE FAKE RADIO
//
//  Everything ui/screen_network.cpp and ui/screen_encounter.cpp reach outside
//  themselves. The RADIO is the interesting one: networking/net.cpp is a device
//  module tests/Makefile never compiles, so the four-function WifiScanDriver
//  seam is filled in here - which is what makes the 12 s timeout, the B cancel
//  and "the radio is released exactly once" drivable at 128x64 with no
//  hardware at all.
// =============================================================================
static uint32_t      g_epoch   = 1700000000u;
static uint8_t       g_cal     = (uint8_t)CAL_USER;
static uint32_t      g_dev     = 0x0BADC0DEu;
static uint32_t      g_roll    = 0;
static int           g_saves   = 0;   // ui_explore_commit() calls
static uint16_t      g_xp_amt  = 0;
static uint8_t       g_xp_src  = 0xFF;
static CooldownTable g_cds;
static Inventory     g_inv;

// The fake driver, and its instrumentation.
static int      g_drv_start = 0, g_drv_stop = 0, g_drv_poll = 0;
static bool     g_drv_ok    = true;      // start() succeeds
static int16_t  g_drv_ans   = WSCAN_POLL_RUNNING;
static uint8_t  g_drv_n     = 0;
static ScanResult g_drv_res[WIFI_SCAN_MAX_RESULTS];

static bool    fk_start(void)  { g_drv_start++; return g_drv_ok; }
static int16_t fk_poll(void)   { g_drv_poll++;  return g_drv_ans; }
static uint8_t fk_read(ScanResult* out, uint8_t cap) {
  const uint8_t n = (g_drv_n < cap) ? g_drv_n : cap;
  for (uint8_t i = 0; i < n; ++i) out[i] = g_drv_res[i];
  return n;
}
static void    fk_stop(void)   { g_drv_stop++; }
static const WifiScanDriver g_drv = { fk_start, fk_poll, fk_read, fk_stop };

const WifiScanDriver& ui_scan_driver(void) { return g_drv; }
void ui_explore_clock(uint32_t* e, uint32_t* ms, uint8_t* cal) {
  if (e)   *e   = g_epoch;
  if (ms)  *ms  = g_now;
  if (cal) *cal = g_cal;
}
uint32_t ui_device_seed(void)     { return g_dev; }
uint32_t ui_explore_roll(void)    { return g_roll; }
CooldownTable& ui_cooldowns(void) { return g_cds; }
Inventory&     ui_inventory(void) { return g_inv; }
void ui_explore_commit(void)      { g_saves++; }
Genome ui_fresh_genome(void) {
  Genome g;
  memset(&g, 0, sizeof g);
  g.lineage_id = 0x51DE0001u;
  g.g0 = 0x1234u; g.g1 = 0x5678u; g.g2 = 0x09ABu;
  g.generation = 1u;
  genome_seal(g);
  return g;
}
static PebbleInstance* g_active_p = nullptr;
void ui_award_xp(uint16_t amount, uint8_t src) { g_xp_amt = amount; g_xp_src = src; }
PebbleInstance* ui_active_pebble(void) { return g_active_p; }

// Everything but the cooldown table, so a case can prove that a network stays
// armed across a second visit to the screen.
static void explore_reset_keep_cooldowns(void) {
  g_drv_start = g_drv_stop = g_drv_poll = 0;
  g_drv_ok = true;
  g_drv_ans = WSCAN_POLL_RUNNING;
  g_drv_n = 0;
  memset(g_drv_res, 0, sizeof g_drv_res);
  g_saves = 0;
  g_xp_amt = 0;
  g_xp_src = 0xFF;
  g_push = 0xFF;
  g_backs = 0;
  g_toast = STR_EMPTY;
}

static void explore_reset(void) {
  explore_reset_keep_cooldowns();
  memset(&g_cds, 0, sizeof g_cds);
  inv_begin(g_inv);
  inv_mod_clear();
  cd_begin();
  g_epoch = 1700000000u;
  g_cal = (uint8_t)CAL_USER;
}

// =============================================================================
//  THE TWO PEBBLE FIXTURES
// =============================================================================
static PebbleView g_view;

static const PebbleView* fixture_view(void) { return &g_view; }

static void fixture_common(void) {
  memset(&g_view, 0, sizeof g_view);
  g_view.present     = 1;
  // A fixed genome: deterministic, and every gene accessor the PEBBLE page
  // reads is a plain bit field, so no seeding is involved.
  g_view.genome.magic_ver  = GENOME_MAGIC_VER;
  g_view.genome.lineage_id = 0x0BADF00Du;
  g_view.genome.g0         = 0x1234u;
  g_view.genome.g1         = 0x5678u;
  g_view.genome.g2         = 0x9ABCu;
  g_view.genome.generation = 3;
  g_view.species_id  = 1;
  g_view.stage       = STAGE_ADULT;
  g_view.pose        = POSE_IDLE;
  snprintf(g_view.age_txt, sizeof g_view.age_txt, "3 d 4 h");
}

// A device three days out of the box: the starter, level 1, well fed.
static void fixture_starter(void) {
  fixture_common();
  snprintf(g_view.name, sizeof g_view.name, "BOLOTA");
  g_view.level    = 1;
  g_view.xp       = 2;
  g_view.xp_next  = XP_TABLE[1];
  g_view.hp_cur   = 15;
  g_view.hp_max   = 21;
  g_view.mood_pct = 64;
  for (uint8_t i = 0; i < ST_COUNT; i++) g_view.care_pct[i] = (uint8_t)(60 + i * 5);
}

// The widest frame this UI can be asked to draw: twelve characters of
// nickname (PB_NICKNAME_CAP - 1), level 30, and every meter pinned at 100.
static void fixture_maxed(void) {
  fixture_common();
  snprintf(g_view.name, sizeof g_view.name, "ABCDEFGHIJKL");
  // Level 30 is the top of the curve: xp_for_level() answers 0 there and the
  // Pebble holds no in-level XP, which is what makes the HOME rule solid.
  g_view.level    = 30;
  g_view.xp       = 0;
  g_view.xp_next  = 0;
  g_view.hp_cur   = 250;
  g_view.hp_max   = 250;
  g_view.mood_pct = 100;
  for (uint8_t i = 0; i < ST_COUNT; i++) g_view.care_pct[i] = 100;
}

static void fixture_none(void) {
  memset(&g_view, 0, sizeof g_view);
}

static void fake_commit(uint8_t id);

static void seams2_reset(void) {
  g_now = 100000u;
  g_idle = 0;
  g_push = 0xFF;
  g_backs = 0;
  g_inputs = 0;
  g_help = STR_EMPTY;
  g_medicine = 0;
  g_action = ACT_NONE;
  g_shown = ACT_NONE;
  g_action_ok = true;
  g_minigame = 0xFF;
  g_god = 0;
  g_bright = 0;
  g_cfg_saves = 0;
  g_flushes = 0;
  g_cfg_p = &g_cfg;
  memset(&g_cfg, 0, sizeof g_cfg);
  g_cfg.brightness = OLED_CONTRAST_DEFAULT;
  g_frames = 0;
  g_hatches = 0;
  g_radio = -1;
  g_radio_calls = 0;
  g_ap_up = 0;
  g_commits = 0;
  g_commit_id = CFM_NONE;
  g_goto = 0xFF;
  g_wiggles = 0;
  g_box_active = 0xFF;
  g_box_released = 0xFF;
  g_box_swap_a = 0xFF;
  g_box_swap_b = 0xFF;
  g_battle_entry = 0xFF;
  g_battle_starts = 0;
  g_result_entry = 0xFF;
  g_result_won = 0xFF;
  g_results = 0;
  g_hold_fps = 0;
  g_hold_calls = 0;
  g_flashes = 0;
  g_shakes = 0;
  dialog_reset();
  dialog_bind_commit(&fake_commit);
  diag_bind(nullptr, nullptr, nullptr);
  evo_bind_ceremony(nullptr);
  ui_bind_view(&fixture_view);
  fixture_starter();
}

static void fake_commit(uint8_t id) { g_commits++; g_commit_id = id; }

// Bound "Recuperar" / "Reintentar" outcomes the tests can steer.
static bool g_backup_ok  = false;
static bool g_display_ok = false;
static int  g_led_calls  = 0;
static bool g_led_on     = false;

static bool fake_recover(void) { return g_backup_ok; }
static bool fake_retry(void)   { return g_display_ok; }
static void fake_led(bool on)  { g_led_on = on; g_led_calls++; }

// =============================================================================
//  Snapshot helper
// =============================================================================
static void snapshot_fn(void (*render)(void), const char* name) {
  fb_reset();
  render();

  // (a) nothing may draw off the panel.
  if (fb_oob() != 0) {
    fprintf(stderr, "  %s: %u out-of-bounds primitive(s), first %s\n",
            name, (unsigned)fb_oob(), fb_oob_first());
  }
  CHECK_EQ(fb_oob(), 0u);

  char path[512];
  snprintf(path, sizeof path, "%s/golden/screens/%s.pbm", NT_TESTS_DIR, name);

  if (nt_flag("--record")) {
    CHECK(fb_write_pbm(path));
    printf("  recorded %s\n", path);
    return;
  }

  // (b) and it must look like it did when a human last approved it.
  const int diff = fb_diff_pbm(path);
  if (diff != 0) {
    fprintf(stderr, "  %s: golden %s %s\n", name, path,
            diff < 0 ? "missing or malformed" : "differs");
    fb_dump();
  }
  CHECK_EQ(diff, 0);
}

// The same, for a screen-table row. The overlays in ui/dialog.cpp have no row
// - they float over one - so they go through snapshot_fn() directly.
static void snapshot(uint8_t screen, const char* name) {
  const ScreenDef* d = screen_def(screen);
  CHECK(d != nullptr);
  if (!d) return;
  snapshot_fn(d->render, name);
}

// =============================================================================
//  THE TABLE
// =============================================================================
TEST(table_rows_are_consistent) {
  // The table is COMPLETE since P2-C11d: every id has a row, and every row
  // names all five hooks. tests/test_statemachine.cpp is where that property
  // lives; here it is only the floor under the snapshots below.
  for (uint8_t s = 0; s < (uint8_t)SCR_COUNT; s++) {
    const ScreenDef* d = screen_def(s);
    CHECK(d != nullptr);
    CHECK(d->render != nullptr);
    // No row may ask for a frame rate the scheduler cannot honour.
    CHECK(d->fps == 0 || (d->fps >= 1 && d->fps <= 60));
  }

  // HOME is where the auto-return goes, so it must never time out itself.
  CHECK((SCREENS[SCR_HOME].flags & SF_STICKY) != 0);
  // Leaving HOME has to end the choreography and release the petfx hold.
  CHECK(SCREENS[SCR_HOME].leave != nullptr);
  // TIME owns B (which is BACK everywhere else) and must not be timed out
  // half way through a date.
  CHECK((SCREENS[SCR_TIME].flags & (SF_STICKY | SF_LOCK_INPUT))
        == (SF_STICKY | SF_LOCK_INPUT));
  // CARE LEFT THE ORDINARY LIST AT P5-C4: its bag is a second mode one level
  // below the navigation stack, so B closes the bag before it closes the
  // screen - the BOX's rule, for the BOX's reason.
  CHECK((SCREENS[SCR_CARE].flags & SF_OWNS_BACK) != 0);
  CHECK_EQ((SCREENS[SCR_CARE].flags & SF_STICKY), 0);
  CHECK(SCREENS[SCR_CARE].leave != nullptr);
  CHECK(SCREENS[SCR_TIME].update != nullptr);   // the right button's repeat
  // Every other migrated row is an ordinary screen: it times out, and the
  // global navigation grammar runs before its own handler.
  // SCR_NETWORK LEFT THIS LIST AT P5-C3: B means CANCEL THE SCAN there, and a
  // scan holds the radio, so the row owns B (asserted by name in
  // b_cancels_the_scan_releases_the_radio_and_goes_back). SCR_ENCOUNTER joins
  // it as an ordinary row and SCR_CAPTURE owns B for the BOX screen's reason.
  static const uint8_t kOrdinary[] = {
    SCR_MENU, SCR_PLAY, SCR_STATUS, SCR_STATUS_B,
    SCR_LINK, SCR_CREATOR, SCR_ENCOUNTER, SCR_SLEEP
  };
  for (size_t i = 0; i < sizeof kOrdinary / sizeof kOrdinary[0]; i++)
    CHECK(SCREENS[kOrdinary[i]].flags == 0);

  // All three hold the device: no auto-return, no global navigation grammar.
  CHECK((SCREENS[SCR_BOOT].flags      & (SF_STICKY | SF_LOCK_INPUT)) == (SF_STICKY | SF_LOCK_INPUT));
  CHECK((SCREENS[SCR_LOAD_SAVE].flags & (SF_STICKY | SF_LOCK_INPUT)) == (SF_STICKY | SF_LOCK_INPUT));
  CHECK((SCREENS[SCR_ERROR].flags     & (SF_STICKY | SF_LOCK_INPUT)) == (SF_STICKY | SF_LOCK_INPUT));

  // BOOT and LOAD_SAVE are frames, not states: their input hook exists (every
  // row's does) and ignores everything.
  CHECK(SCREENS[SCR_BOOT].input      == nop_input);
  CHECK(SCREENS[SCR_LOAD_SAVE].input == nop_input);
  CHECK(SCREENS[SCR_ERROR].input     != nop_input);

  // SETTINGS, BOX, EVOLUTION and GAME answer B themselves; nothing that has
  // SF_LOCK_INPUT needs to say so twice.
  static const uint8_t kOwnsBack[] = { SCR_SETTINGS, SCR_BOX, SCR_EVOLUTION, SCR_GAME,
                                      SCR_BATTLE };
  for (size_t i = 0; i < sizeof kOwnsBack / sizeof kOwnsBack[0]; i++)
    CHECK((SCREENS[kOwnsBack[i]].flags & SF_OWNS_BACK) != 0);

  // BATTLE left kOrdinary with P4-C4 and its exact flags are ASSERTED rather
  // than merely permitted: SF_STICKY, because invariant 3 dropping the player
  // on HOME twenty seconds into a fight would abandon it; SF_OWNS_BACK, because
  // B walks SWITCH -> MENU and skips a round's transcript before it ever leaves
  // the screen; and NOT SF_OWNS_FRAME, because "ahora no puedes" and the
  // victory XP notice are both toasts, and ui_service() hands an owns-frame
  // screen no toast and no modal at all.
  CHECK_EQ(SCREENS[SCR_BATTLE].flags, (uint8_t)(SF_STICKY | SF_OWNS_BACK));
  CHECK(SCREENS[SCR_BATTLE].update != nullptr);   // the 20 fps hold and the beat clock
  CHECK(SCREENS[SCR_BATTLE].leave  != nullptr);   // THE one report path

  // CREATOR is the one screen that owns the Wi-Fi station, so it is the one
  // screen whose enter AND leave hooks must both exist: taking the radio
  // without a hook to give it back is exactly the always-on policy the plan
  // removed (section 2 row G4).
  CHECK(SCREENS[SCR_CREATOR].enter  != nullptr);
  CHECK(SCREENS[SCR_CREATOR].leave  != nullptr);
  CHECK(SCREENS[SCR_CREATOR].update != nullptr);   // the payload follows the IP

  // The console composes its own frame and answers its own gestures.
  CHECK((SCREENS[SCR_DIAG].flags & (SF_STICKY | SF_LOCK_INPUT | SF_OWNS_FRAME))
        == (SF_STICKY | SF_LOCK_INPUT | SF_OWNS_FRAME));
  // EVOLUTION is sticky and owns B, and nothing else: the rub ALTERNATES the
  // two buttons, so the router may not spend B on BACK - LONG_BOTH is how you
  // leave. The ceremony's "no chrome, no buttons" is dynamic and belongs to
  // ui/ceremony.cpp.
  CHECK_EQ(SCREENS[SCR_EVOLUTION].flags, (uint8_t)(SF_STICKY | SF_OWNS_BACK));
}

// =============================================================================
//  SNAPSHOTS
// =============================================================================
TEST(snapshot_boot) {
  snapshot(SCR_BOOT, "boot_splash");
}

TEST(snapshot_load_save) {
  snapshot(SCR_LOAD_SAVE, "load_save_reading");
}

TEST(snapshot_error_corrupt) {
  err_set_kind(ERRK_SAVE_CORRUPT);
  snapshot(SCR_ERROR, "error_save_corrupt");
}

TEST(snapshot_error_newer) {
  err_set_kind(ERRK_SAVE_NEWER);
  snapshot(SCR_ERROR, "error_save_newer");
}

TEST(snapshot_error_display) {
  err_set_kind(ERRK_DISPLAY);
  snapshot(SCR_ERROR, "error_display");
}

// =============================================================================
//  ERROR BEHAVIOUR
// =============================================================================
TEST(error_newer_never_writes) {
  seams_reset();
  ui_bind_recover(&fake_recover);
  g_backup_ok = true;                       // a checkpoint IS available
  err_set_kind(ERRK_SAVE_NEWER);

  err_input(GST_TAP_L);
  CHECK_EQ(g_toast, STR_SAVE_UPDATE_FW);
  CHECK_EQ(g_recovered, 0);                 // the dangerous one: never taken
  err_input(GST_TAP_R);
  CHECK_EQ(g_toast, STR_SAVE_UPDATE_FW);
  CHECK_EQ(g_wipe, 0);                      // and no factory reset offered
  CHECK_EQ(err_kind(), ERRK_SAVE_NEWER);
}

TEST(error_corrupt_offers_both_choices) {
  seams_reset();
  ui_bind_recover(&fake_recover);
  err_set_kind(ERRK_SAVE_CORRUPT);

  // A with nothing to recover: says so, and writes nothing.
  g_backup_ok = false;
  err_input(GST_TAP_L);
  CHECK_EQ(g_toast, STR_SAVE_NO_BACKUP);
  CHECK_EQ(g_recovered, 0);
  CHECK_EQ(err_kind(), ERRK_SAVE_CORRUPT);

  // B always goes through the confirmation, never straight to the reset.
  err_input(GST_TAP_R);
  CHECK_EQ(g_wipe, 1);

  // A with a checkpoint: recovered, and the screen lets go.
  g_backup_ok = true;
  err_input(GST_TAP_L);
  CHECK_EQ(g_toast, STR_SAVE_FROM_BACKUP);
  CHECK_EQ(g_recovered, 1);
  CHECK_EQ(err_kind(), ERRK_NONE);
}

TEST(error_display_retries_and_blinks) {
  seams_reset();
  ui_bind_display_retry(&fake_retry);
  ui_bind_led(&fake_led);
  ui_note_display_failure();
  CHECK_EQ(err_kind(), ERRK_DISPLAY);
  CHECK_EQ(g_goto, (uint8_t)SCR_ERROR);

  // The LED carries rd_fatal()'s old double blink: on, off, on, then dark.
  err_enter();
  err_update(10000u);
  CHECK(g_led_on);
  err_update(10000u + 150u);
  CHECK(!g_led_on);
  err_update(10000u + 300u);
  CHECK(g_led_on);
  err_update(10000u + 500u);
  CHECK(!g_led_on);
  err_update(10000u + ERR_BLINK_PERIOD_MS);      // the pattern repeats
  CHECK(g_led_on);

  // A failed retry changes nothing at all.
  g_display_ok = false;
  g_goto = 0xFF;
  err_input(GST_TAP_L);
  CHECK_EQ(err_kind(), ERRK_DISPLAY);
  CHECK_EQ(g_goto, 0xFF);

  // A successful one hands the device back and stops the blinking.
  g_display_ok = true;
  err_input(GST_TAP_L);
  CHECK_EQ(err_kind(), ERRK_NONE);
  CHECK_EQ(g_goto, (uint8_t)SCR_HOME);
  err_leave();
  CHECK(!g_led_on);
}

// A dead panel and an unreadable save are independent failures, and app_setup
// arms them in that order, so the display one used to overwrite the save one.
// Answering the panel must not silently answer the save.
TEST(a_display_failure_does_not_swallow_a_save_question) {
  seams_reset();
  ui_bind_display_retry(&fake_retry);
  ui_bind_led(&fake_led);

  // Boot order: the load runs first and raises a corrupt save...
  ui_note_load(LOAD_CORRUPT);
  CHECK_EQ(err_kind(), ERRK_SAVE_CORRUPT);
  // ...then the panel turns out to be dead too.
  ui_note_display_failure();
  CHECK_EQ(err_kind(), ERRK_DISPLAY);

  // The panel comes back. The device must NOT drop the user on HOME with an
  // unreadable save and no warning: the parked question takes the screen back.
  g_display_ok = true;
  g_goto = 0xFF;
  err_input(GST_TAP_L);
  CHECK_EQ(err_kind(), ERRK_SAVE_CORRUPT);
  CHECK_EQ(g_goto, 0xFF);                 // still on ERROR, now asking about the save
  CHECK(!g_led_on);                       // and no longer blinking at the owner

  // The save question then behaves exactly as it does on its own.
  g_wipe = 0;
  err_input(GST_TAP_R);
  CHECK_EQ(g_wipe, 1);                    // two-dialog wipe, not an instant reset
}

// A newer save is parked and re-armed the same way, and stays unwritable.
TEST(a_display_failure_does_not_swallow_a_newer_save_question) {
  seams_reset();
  ui_bind_display_retry(&fake_retry);
  ui_bind_led(&fake_led);
  ui_bind_recover(&fake_recover);

  ui_note_load(LOAD_FOREIGN_NEWER);
  ui_note_display_failure();
  g_display_ok = true;
  err_input(GST_TAP_L);
  CHECK_EQ(err_kind(), ERRK_SAVE_NEWER);

  // Neither button may write to a save this build merely cannot parse.
  g_recovered = 0;
  err_input(GST_TAP_L);
  CHECK_EQ(g_recovered, 0);
  err_input(GST_TAP_R);
  CHECK_EQ(g_wipe, 0);
}

// The save pipeline's verdicts, which is how the ERROR screen is ever reached.
TEST(load_result_routes_to_the_right_screen) {
  seams_reset();
  err_set_kind(ERRK_NONE);

  ui_note_load(LOAD_CORRUPT);
  CHECK_EQ(err_kind(), ERRK_SAVE_CORRUPT);
  CHECK_EQ(g_goto, (uint8_t)SCR_ERROR);

  seams_reset();
  ui_note_load(LOAD_FOREIGN_NEWER);
  CHECK_EQ(err_kind(), ERRK_SAVE_NEWER);
  CHECK_EQ(g_goto, (uint8_t)SCR_ERROR);

  // The three recoverable outcomes are a toast, not a screen: the pet is
  // playable and nothing is waiting on the user.
  seams_reset();
  ui_note_load(LOAD_MIGRATED);
  CHECK_EQ(g_toast, STR_SAVE_UPDATED);
  CHECK_EQ(g_goto, 0xFF);

  seams_reset();
  ui_note_load(LOAD_RECOVERED_PAIR);
  CHECK_EQ(g_toast, STR_SAVE_RECOVERED);
  CHECK_EQ(g_goto, 0xFF);

  seams_reset();
  ui_note_load(LOAD_RECOVERED_CKPT);
  CHECK_EQ(g_toast, STR_SAVE_FROM_BACKUP);
  CHECK_EQ(g_goto, 0xFF);

  seams_reset();
  ui_note_load(LOAD_OK);
  CHECK_EQ(g_toast, STR_EMPTY);
  CHECK_EQ(g_goto, 0xFF);
}

// =============================================================================
//  P2-C11b SNAPSHOTS
// =============================================================================
TEST(snapshot_home) {
  seams2_reset();
  snapshot(SCR_HOME, "home_starter");
}

TEST(snapshot_home_maxed) {
  seams2_reset();
  fixture_maxed();
  snapshot(SCR_HOME, "home_maxed");
}

// =============================================================================
//  P4-C4a: THE SPECIES - NOT THE GENOME - CHOSE THE CREATURE.
//
//  This fixture is home_starter with ONE BYTE CHANGED: species_id, 1 -> 3.
//  Same genome, same name, same level, same meters, same everything else. Until
//  this step HOME resolved the body from gene_species(v.genome), so the two
//  frames were PIXEL-IDENTICAL and a golden could not have told them apart.
//
//  The two goldens together are the artefact; the checks below are what stops
//  them from being two files nobody diffs. They assert that the frames differ
//  AT ALL and that every differing pixel is inside the 40x40 body box - i.e.
//  that the species moved the creature and nothing else on the screen.
// =============================================================================
TEST(snapshot_home_species) {
  seams2_reset();

  const ScreenDef* home = screen_def(SCR_HOME);
  CHECK(home != nullptr);
  if (!home) return;

  static uint8_t starter[FB_H][FB_W];
  fb_reset();
  home->render();                      // species 1, the starter
  for (int y = 0; y < FB_H; ++y)
    for (int x = 0; x < FB_W; ++x) starter[y][x] = (uint8_t)fb_get(x, y);

  g_view.species_id = 3;               // Rafagon: sprite_id 2, a different body
  fb_reset();
  home->render();

  // The adult body box: 40x40, centred by sprite_center_x() and standing on
  // HOME_FLOOR_Y, which is where draw_static_body() puts it.
  const int bx = (int)sprite_center_x(40);
  const int by = (int)HOME_FLOOR_Y - 40;
  int changed = 0, changed_outside_the_body = 0;
  for (int y = 0; y < FB_H; ++y) {
    for (int x = 0; x < FB_W; ++x) {
      if ((uint8_t)fb_get(x, y) == starter[y][x]) continue;
      ++changed;
      if (x < bx || x >= bx + 40 || y < by || y >= by + 40)
        ++changed_outside_the_body;
    }
  }
  CHECK(changed > 0);                        // the species reached the pixels
  CHECK_EQ(changed_outside_the_body, 0);     // and reached nothing else

  snapshot(SCR_HOME, "home_species");
}

TEST(snapshot_home_empty) {
  seams2_reset();
  fixture_none();
  snapshot(SCR_HOME, "home_no_pebble");
}

// The ring REMEMBERS where it was left, exactly as it always did, so every
// test that cares about a particular item spins to it rather than assuming.
static void menu_to(uint8_t item) {
  menu_enter();
  for (uint8_t i = 0; i < MENU_ITEM_COUNT && menu_cursor() != item; i++)
    menu_input(GST_TAP_L);
  g_now += UI_RING_MS;      // let the carousel settle: a golden of a slide in
                            // progress would be a golden of the clock stub
}

TEST(snapshot_menu) {
  seams2_reset();
  menu_to(MENU_PEBBLE);
  snapshot(SCR_MENU, "menu_pebble");
}

// The last ring item, and the drain bar of navigation invariant 3 at the same
// time. This is the trap the plan wrote down before P2-C11b started: MENU is
// the first NON-sticky screen in the table, and without gfx_countdown() it
// would have silently lost the bar that says the screen is about to leave.
TEST(snapshot_menu_countdown) {
  seams2_reset();
  menu_to(MENU_SETTINGS);
  CHECK_EQ(menu_cursor(), (uint8_t)MENU_SETTINGS);
  g_idle = UI_AUTORETURN_MS - 2000u;          // 2 s left: the bar is 2/5 wide
  snapshot(SCR_MENU, "menu_settings_countdown");
}

TEST(snapshot_care_list) {
  seams2_reset();
  care_enter();
  snapshot(SCR_CARE, "care_list");
}

TEST(snapshot_care_list_scrolled) {
  seams2_reset();
  care_enter();
  for (uint8_t i = 0; i < CARE_BACK; i++) care_input(GST_TAP_L);
  CHECK_EQ(care_cursor(), (uint8_t)CARE_BACK);
  snapshot(SCR_CARE, "care_list_back");
}

TEST(snapshot_play_list) {
  seams2_reset();
  play_enter();
  snapshot(SCR_PLAY, "play_list");
}

TEST(snapshot_status_a) {
  seams2_reset();
  status_a_enter();
  snapshot(SCR_STATUS, "status_a_starter");
}

TEST(snapshot_status_a_maxed) {
  seams2_reset();
  fixture_maxed();
  status_a_enter();
  snapshot(SCR_STATUS, "status_a_maxed");
}

TEST(snapshot_status_b) {
  seams2_reset();
  status_b_enter();
  snapshot(SCR_STATUS_B, "status_b_genome");
}

TEST(snapshot_status_b_god_hold) {
  seams2_reset();
  status_b_enter();
  g_god = 60;
  snapshot(SCR_STATUS_B, "status_b_god_hold");
}

TEST(snapshot_settings) {
  seams2_reset();
  settings_enter();
  snapshot(SCR_SETTINGS, "settings_list");
}

TEST(snapshot_settings_info) {
  seams2_reset();
  settings_enter();
  for (uint8_t i = 0; i < SET_INFO; i++) settings_input(GST_TAP_L);
  settings_input(GST_HOLD_R);
  CHECK_EQ(settings_page(), 1);
  snapshot(SCR_SETTINGS, "settings_info");
}

TEST(snapshot_time_entry) {
  seams2_reset();
  g_clock_known = false;
  time_enter();
  snapshot(SCR_TIME, "time_entry");
}

// =============================================================================
//  P2-C11b BEHAVIOUR
// =============================================================================

// Spec section 8's menu, in its order, with the destinations it implies. The
// two that have no screen yet must SAY so rather than doing nothing at all.
TEST(menu_goes_where_section_8_says) {
  seams2_reset();
  menu_to(MENU_PEBBLE);

  struct { uint8_t item; int pushes; uint8_t to; } kWant[] = {
    { MENU_PEBBLE,   1, SCR_STATUS },
    { MENU_CARE,     1, SCR_CARE     },
    { MENU_PLAY,     1, SCR_PLAY     },
    { MENU_BOX,      1, SCR_BOX      },
    { MENU_NETWORK,  1, SCR_NETWORK  },
    { MENU_LINK,     1, SCR_LINK   },
    { MENU_SETTINGS, 1, SCR_SETTINGS },
  };
  for (size_t i = 0; i < sizeof kWant / sizeof kWant[0]; i++) {
    seams_reset();
    g_push = 0xFF;
    CHECK_EQ(menu_cursor(), kWant[i].item);
    menu_input(GST_HOLD_R);                    // section 7: B held chooses
    CHECK_EQ(g_push, kWant[i].to);
    CHECK(kWant[i].pushes == 1);
    menu_input(GST_TAP_L);                     // on to the next item
  }
  CHECK_EQ(menu_cursor(), (uint8_t)MENU_PEBBLE);   // invariant 4: it is a ring
}

TEST(menu_help_and_the_ring_wraps_back_to_the_first_item) {
  seams2_reset();
  menu_to(MENU_PEBBLE);
  menu_input(GST_BOTH);
  CHECK_EQ(g_help, STR_HLP_STATUS);

  // P3-C4a took the double tap away, and with it this screen's two shortcuts:
  // DBL_L jumped to the first item and DBL_R repeated the last care action.
  // The jump is replaced by the ring itself - stepping A all the way round
  // comes back to where it started - and repeat-last-action was deleted with
  // its only caller (it is in no spec section and its "nothing to repeat"
  // path toasted "bad argument").
  for (uint8_t i = 0; i < (uint8_t)MENU_ITEM_COUNT; ++i) menu_input(GST_TAP_L);
  CHECK_EQ(menu_cursor(), (uint8_t)MENU_PEBBLE);
}

// An accepted care action leaves the player on HOME watching the film; a
// REJECTED one has no film, so the list gives the screen back instead.
TEST(care_actions_and_the_rejected_path) {
  seams2_reset();
  care_enter();

  care_input(GST_HOLD_R);                        // meal
  CHECK_EQ(g_shown, (uint8_t)ACT_FEED_MEAL);
  CHECK_EQ(g_backs, 0);

  g_action_ok = false;
  care_input(GST_HOLD_R);
  CHECK_EQ(g_backs, 1);                         // refused: back to where we were
  g_action_ok = true;

  care_input(GST_TAP_L);                        // snack
  care_input(GST_HOLD_R);
  CHECK_EQ(g_shown, (uint8_t)ACT_FEED_SNACK);

  care_input(GST_TAP_L);                        // clean
  care_input(GST_HOLD_R);
  CHECK_EQ(g_shown, (uint8_t)ACT_CLEAN);

  care_input(GST_TAP_L);                        // medicine: always a confirmation
  care_input(GST_HOLD_R);
  CHECK_EQ(g_medicine, 1);

  care_input(GST_TAP_L);                        // the bag: a MODE, not an action
  care_input(GST_HOLD_R);
  CHECK_EQ(care_mode(), (uint8_t)CAREM_BAG);
  care_input(GST_TAP_R);                        // ...and B closes it, not the screen
  CHECK_EQ(care_mode(), (uint8_t)CAREM_LIST);
  CHECK_EQ(g_backs, 1);                         // still the one from the refusal

  care_input(GST_TAP_L);                        // volver
  g_backs = 0;
  care_input(GST_HOLD_R);
  CHECK_EQ(g_backs, 1);
}

TEST(play_list_starts_a_game_or_leaves) {
  seams2_reset();
  play_enter();
  play_input(GST_HOLD_R);
  CHECK_EQ(g_minigame, (uint8_t)0);
  play_input(GST_TAP_L);
  play_input(GST_HOLD_R);
  CHECK_EQ(g_minigame, (uint8_t)1);
  // The "jump to the last row" shortcut went with the double tap; the list
  // wraps, so one step back from the first row is the last one.
  while (play_cursor() != (uint8_t)(PLAY_ROWS - 1)) play_input(GST_TAP_L);
  CHECK_EQ(play_cursor(), (uint8_t)(PLAY_ROWS - 1));
  g_backs = 0;
  play_input(GST_HOLD_R);
  CHECK_EQ(g_backs, 1);
}

TEST(home_gestures) {
  seams2_reset();
  home_input(GST_TAP_L);
  CHECK_EQ(g_push, (uint8_t)SCR_MENU);
  home_input(GST_TAP_R);
  CHECK_EQ(g_shown, (uint8_t)ACT_PET);
  home_input(GST_HOLD_L);
  CHECK_EQ(g_push, (uint8_t)SCR_STATUS);
  home_input(GST_LONG_BOTH);
  CHECK_EQ(g_push, (uint8_t)SCR_SETTINGS);

  // BOTH toggles the mute flag and persists it.
  const uint8_t before = g_cfg.flags;
  home_input(GST_BOTH);
  CHECK_EQ((uint8_t)(g_cfg.flags ^ before), (uint8_t)CF_MUTE);
  CHECK_EQ(g_cfg_saves, 1);
}

TEST(status_pages_flip) {
  seams2_reset();
  status_a_enter();
  status_input(GST_TAP_L);
  CHECK_EQ(g_goto, (uint8_t)SCR_STATUS_B);
  status_b_enter();
  status_input(GST_TAP_L);
  CHECK_EQ(g_goto, (uint8_t)SCR_STATUS);
}

TEST(settings_toggles_persist_and_the_info_page_closes) {
  seams2_reset();
  settings_enter();

  settings_input(GST_HOLD_R);                    // SET_SOUND
  CHECK_EQ((uint8_t)(g_cfg.flags & CF_MUTE), (uint8_t)CF_MUTE);
  CHECK_EQ(g_cfg_saves, 1);

  settings_input(GST_TAP_L);                    // SET_WEB
  settings_input(GST_HOLD_R);
  CHECK_EQ((uint8_t)(g_cfg.flags & CF_WEB_ENABLED), (uint8_t)CF_WEB_ENABLED);

  settings_input(GST_TAP_L);                    // SET_BRIGHT: a five-step ring
  const uint8_t b0 = g_cfg.brightness;
  settings_input(GST_HOLD_R);
  CHECK(g_cfg.brightness != b0);
  CHECK_EQ(g_bright, g_cfg.brightness);         // and it went through the arbiter

  // The "Acerca de" page is read-only and any gesture gives the list back.
  settings_enter();
  for (uint8_t i = 0; i < SET_INFO; i++) settings_input(GST_TAP_L);
  settings_input(GST_HOLD_R);
  CHECK_EQ(settings_page(), 1);
  settings_close_page();
  CHECK_EQ(settings_page(), 0);

  // With no Config bound nothing may be written and the user is told.
  settings_enter();
  g_cfg_p = nullptr;
  g_cfg_saves = 0;
  settings_input(GST_HOLD_R);
  CHECK_EQ(g_toast, STR_ERR_BUSY);
  CHECK_EQ(g_cfg_saves, 0);
  g_cfg_p = &g_cfg;

  // Section 7: B TAPPED leaves the screen, and it reaches the hook because the
  // row carries SF_OWNS_BACK (the info page is one level below the stack).
  settings_enter();
  g_backs = 0;
  settings_input(GST_TAP_R);
  CHECK_EQ(g_backs, 1);
}

// The five fields, the wrap, the leap-year clamp and the commit.
TEST(time_entry_edits_a_real_calendar) {
  seams2_reset();
  g_clock_known = false;
  time_enter();
  CHECK_EQ(time_field(CLK_YEAR), (uint16_t)CLK_YEAR_MIN);
  CHECK_EQ(time_field(CLK_MONTH), (uint16_t)1);
  CHECK_EQ(time_field(CLK_DAY), (uint16_t)1);
  CHECK_EQ(time_field(CLK_HOUR), (uint16_t)12);

  // 2020 is a leap year: February has 29 days and the 30th is unreachable.
  time_input(GST_TAP_L);                        // -> month
  time_input(GST_TAP_R);                        // February
  CHECK_EQ(time_field(CLK_MONTH), (uint16_t)2);
  time_input(GST_TAP_L);                        // -> day
  for (uint8_t i = 0; i < 28; i++) time_input(GST_TAP_R);
  CHECK_EQ(time_field(CLK_DAY), (uint16_t)29);
  time_input(GST_TAP_R);
  CHECK_EQ(time_field(CLK_DAY), (uint16_t)1);   // wrapped, never a 30 February

  // 31 January -> February must not leave an impossible day on screen.
  time_enter();
  time_input(GST_TAP_L);
  time_input(GST_TAP_L);                        // -> day
  for (uint8_t i = 0; i < 30; i++) time_input(GST_TAP_R);
  CHECK_EQ(time_field(CLK_DAY), (uint16_t)31);
  // DAY -> HOUR -> MIN -> YEAR -> MONTH: the field cursor is a ring too.
  for (uint8_t i = 0; i < 4; i++) time_input(GST_TAP_L);
  CHECK_EQ(time_cursor(), (uint8_t)CLK_MONTH);
  time_input(GST_TAP_R);
  CHECK_EQ(time_field(CLK_DAY), (uint16_t)29);

  // A HOLD confirms; a tap never can. The recogniser is flushed either way, so
  // the release of the confirming hold cannot fire on the screen underneath.
  g_flushes = 0;
  g_backs   = 0;
  time_input(GST_HOLD_L);
  CHECK_EQ(g_set_y, (uint16_t)CLK_YEAR_MIN);
  CHECK_EQ(g_set_mo, (uint8_t)2);
  CHECK_EQ(g_set_d, (uint8_t)29);
  CHECK_EQ(g_toast, STR_CLK_SAVED);
  CHECK_EQ(g_flushes, 1);
  CHECK_EQ(g_backs, 1);

  // A refused stamp writes nothing and does not leave the screen.
  g_clock_ok = false;
  g_backs = 0;
  time_input(GST_HOLD_L);
  CHECK_EQ(g_toast, STR_CLK_BAD);
  CHECK_EQ(g_backs, 0);
  g_clock_ok = true;

  // BOTH leaves without saving; LONG BOTH is invariant 2, by hand, because the
  // row is SF_LOCK_INPUT and the global grammar never runs on it.
  g_backs = 0;
  time_input(GST_BOTH);
  CHECK_EQ(g_backs, 1);
  g_goto = 0xFF;
  time_input(GST_LONG_BOTH);
  CHECK_EQ(g_goto, (uint8_t)SCR_HOME);
}

// The clock screen starts from what the device already believes, when it
// believes anything at all.
TEST(time_entry_starts_from_the_known_clock) {
  seams2_reset();
  g_clock_known = true;
  time_enter();
  CHECK_EQ(time_field(CLK_YEAR), (uint16_t)2024);
  CHECK_EQ(time_field(CLK_MONTH), (uint16_t)2);
  CHECK_EQ(time_field(CLK_DAY), (uint16_t)29);
  CHECK_EQ(time_field(CLK_HOUR), (uint16_t)7);
  CHECK_EQ(time_field(CLK_MIN), (uint16_t)5);
  g_clock_known = false;
}

// Navigation invariant 3's drain bar. It exists on every migrated screen that
// is not SF_STICKY, it is empty until the last UI_COUNTDOWN_MS, and it shrinks.
TEST(the_countdown_bar_drains) {
  seams2_reset();
  menu_to(MENU_PEBBLE);

  auto bar_width = [](void) {
    int w = 0;
    for (int x = 0; x < FB_W; x++) if (fb_get(x, UI_CONTENT_BOTTOM - 1)) w++;
    return w;
  };

  g_idle = 0;
  fb_reset();
  menu_render();
  const int quiet = bar_width();

  g_idle = UI_AUTORETURN_MS - UI_COUNTDOWN_MS / 2u;   // half the window left
  fb_reset();
  menu_render();
  const int half = bar_width();

  g_idle = UI_AUTORETURN_MS - UI_COUNTDOWN_MS / 4u;   // a quarter left
  fb_reset();
  menu_render();
  const int quarter = bar_width();

  CHECK(half > quiet);
  CHECK(quarter < half);
  CHECK_EQ(fb_oob(), 0u);

  // HOME is SF_STICKY and never draws one, however long the player stares.
  fixture_starter();
  g_idle = UI_AUTORETURN_MS * 4u;
  fb_reset();
  home_render();
  CHECK_EQ(fb_oob(), 0u);
}

// =============================================================================
//  P2-C11c: LINK, EVOLUTION, DIAG, CREATOR and the overlays
// =============================================================================
TEST(snapshot_link) {
  seams2_reset();
  snapshot(SCR_LINK, "link_phase7");
}

TEST(snapshot_evolution_incubator) {
  seams2_reset();
  g_view.stage = STAGE_EGG;
  g_view.age_s = 60;
  evo_enter();
  snapshot(SCR_EVOLUTION, "evolution_egg");
}

// The same screen with the shell already cracking, five of the ten taps in,
// and the "this egg went cold" line: the widest the incubator ever gets.
TEST(snapshot_evolution_cold_egg) {
  seams2_reset();
  g_view.stage = STAGE_EGG;
  g_view.age_s = AGE_EGG_S - 10u;
  g_view.flags = PF_COLD_EGG;
  evo_enter();
  for (uint8_t i = 0; i < 5; i++) evo_input((i & 1u) ? GST_TAP_R : GST_TAP_L);
  CHECK_EQ(evo_rub_count(), (uint8_t)5);
  snapshot(SCR_EVOLUTION, "evolution_egg_cold");
}

// With no ceremony bound - which is every host build, and any device build
// where nothing is hatching - the screen draws the incubator, not an empty
// panel.
TEST(evolution_falls_back_to_the_incubator) {
  seams2_reset();
  g_view.stage = STAGE_EGG;
  evo_bind_ceremony(nullptr);
  fb_reset();
  evo_render();
  int lit = 0;
  for (int y = 0; y < FB_H; y++) for (int x = 0; x < FB_W; x++) lit += fb_get(x, y);
  CHECK(lit > 0);
  CHECK_EQ(fb_oob(), 0u);
}

// Ten taps hatch the egg; ten taps on the SAME side do not. The alternation is
// the whole gesture: leaning on one button is not rubbing an egg.
TEST(evolution_rub_needs_alternating_taps) {
  seams2_reset();
  g_view.stage = STAGE_EGG;
  evo_enter();
  for (uint8_t i = 0; i < EGG_RUB_TAPS; i++) evo_input(GST_TAP_L);
  CHECK_EQ(g_hatches, 0);
  CHECK_EQ(evo_rub_count(), (uint8_t)1);      // only the first one counted

  evo_enter();
  for (uint8_t i = 0; i < EGG_RUB_TAPS; i++) evo_input((i & 1u) ? GST_TAP_R : GST_TAP_L);
  CHECK_EQ(g_hatches, 1);
  CHECK_EQ(evo_rub_count(), (uint8_t)0);      // and the counter starts over
}

TEST(snapshot_diag_console_off) {
  seams2_reset();
  snapshot(SCR_DIAG, "diag_console_off");
}

// A console that switched itself off must not keep the screen.
TEST(diag_leaves_when_the_console_is_gone) {
  seams2_reset();
  diag_bind(nullptr, nullptr, nullptr);
  g_goto = 0xFF;
  diag_update(0);
  CHECK_EQ(g_goto, (uint8_t)SCR_HOME);

  g_goto = 0xFF;
  diag_input(GST_TAP_L);
  CHECK_EQ(g_goto, (uint8_t)SCR_HOME);
}

// snapshot_creator_station and tests/golden/screens/creator_station.pbm were
// here. Both were the frozen image of the "joined the user's network" branch,
// which P5-C1 deleted from ui/screen_creator.cpp - a golden of a branch that
// cannot be reached is a test that cannot fail. The case below keeps the part
// of it that survives: the screen takes and releases the radio.

TEST(snapshot_creator_portal) {
  seams2_reset();
  g_ap_up = 1;
  creator_enter();
  CHECK_EQ(creator_variant(), (uint8_t)1);     // the portal opens on "join me"
  snapshot(SCR_CREATOR, "creator_portal");
}

TEST(snapshot_creator_offline) {
  seams2_reset();
  creator_enter();
  snapshot(SCR_CREATOR, "creator_offline");
}

// The radio is SCREEN-OWNED: taken on the way in, given back on the way out.
// Holding the station after the screen closes is the always-on policy the plan
// removed, and it costs ~50 KB of heap and the largest current draw on the
// board.
TEST(creator_takes_and_releases_the_radio) {
  seams2_reset();
  g_ap_up = 1;                                 // was g_sta_up, see above
  creator_enter();
  CHECK_EQ(g_radio, 1);
  creator_leave();
  CHECK_EQ(g_radio, 0);
  CHECK_EQ(g_radio_calls, 2);
}

// With the portal up a tap flips between the two symbols; with one symbol
// there is nothing to flip and the tap is a no-op rather than a broken frame.
TEST(creator_alternates_only_while_the_portal_is_up) {
  seams2_reset();
  g_ap_up = 1;
  creator_enter();
  const uint8_t first = creator_variant();
  creator_input(GST_TAP_L);
  CHECK(creator_variant() != first);

  // ...and with NO radio at all there is likewise one symbol and nothing to
  // flip. This half used to drive g_sta_up, which no longer exists; the offline
  // state is what is left of "the portal is not up".
  seams2_reset();
  creator_enter();
  const uint8_t only = creator_variant();
  creator_input(GST_TAP_L);
  CHECK_EQ(creator_variant(), only);
}

// =============================================================================
//  THE MODAL LAYER (ui/dialog.cpp)
// =============================================================================
static void render_confirm(void) { dialog_render(); }

TEST(snapshot_confirm) {
  seams2_reset();
  dialog_open_confirm(CFM_WIPE1, STR_CF_WIPE);
  snapshot_fn(&render_confirm, "confirm_wipe");
}

// Section 18's evolution offer. THE CEREMONY ITSELF CANNOT BE SNAPSHOTTED
// HERE: ui/ceremony.cpp is a device translation unit - it drives the panel's
// flash and shake registers through render.h and draws the body through petfx -
// so what this file can render of the P3-C3 flow is its PURE half, the modal
// that asks. (The other half of the claim, that the body on the far side of the
// show is a different one, is tests/test_pet_view.cpp's.)
TEST(snapshot_confirm_evolve) {
  seams2_reset();
  dialog_open_confirm(CFM_EVOLVE, STR_CF_EVOLVE);
  CHECK_EQ(dialog_confirm_id(), (uint8_t)CFM_EVOLVE);
  CHECK_EQ(dialog_confirm_yes(), (uint8_t)0);   // invariant 5: it starts on NO
  snapshot_fn(&render_confirm, "confirm_evolve");
}

TEST(snapshot_confirm_yes) {
  seams2_reset();
  dialog_open_confirm(CFM_WIPE1, STR_CF_WIPE);
  dialog_input(GST_TAP_L);                     // move the cursor onto YES
  CHECK_EQ(dialog_confirm_yes(), (uint8_t)1);
  snapshot_fn(&render_confirm, "confirm_wipe_yes");
}

TEST(snapshot_alert) {
  seams2_reset();
  dialog_alert(AL_HUNGRY);
  CHECK(dialog_service(g_now, true));
  snapshot_fn(&render_confirm, "alert_hungry");
}

TEST(snapshot_help) {
  seams2_reset();
  dialog_open_help(STR_HLP_MEAL);
  snapshot_fn(&render_confirm, "help_line");
}

// Invariant 5: every confirmation starts on NO, so the destructive answer is
// never one press away.
TEST(a_confirmation_starts_on_no) {
  seams2_reset();
  dialog_open_confirm(CFM_WIPE2, STR_CF_WIPE2);
  CHECK_EQ(dialog_modal(), (uint8_t)MODAL_CONFIRM);
  CHECK_EQ(dialog_confirm_yes(), (uint8_t)0);
  dialog_input(GST_HOLD_R);                    // choosing NO closes it
  CHECK_EQ(dialog_modal(), (uint8_t)MODAL_NONE);
  CHECK_EQ(g_commits, 0);

  dialog_open_confirm(CFM_WIPE2, STR_CF_WIPE2);
  dialog_input(GST_TAP_L);                     // onto YES
  dialog_input(GST_HOLD_R);
  CHECK_EQ(g_commits, 1);
  CHECK_EQ(g_commit_id, (uint8_t)CFM_WIPE2);
}

// =============================================================================
//  THE BOX (P2-C11d)
//
//  ui/screen_box.cpp reads game/box.h directly, so the fixture here is a REAL
//  GameState with a real Box bound to it - which also means these snapshots
//  exercise box_add()/box_set_active() rather than a hand-drawn mock.
// =============================================================================
static GameState g_gs;

static void box_fixture(uint8_t occupied) {
  memset(&g_gs, 0, sizeof g_gs);
  box_bind(g_gs);
  Genome gen;
  memset(&gen, 0, sizeof gen);
  gen.magic_ver  = GENOME_MAGIC_VER;
  gen.lineage_id = 0x0BADF00Du;
  gen.g0 = 0x1234u; gen.g1 = 0x5678u; gen.g2 = 0x9ABCu;
  gen.generation = 3;
  for (uint8_t i = 0; i < occupied; ++i) {
    // Species 1 for all three: the roster is one species until Phase 9 fills
    // data/species_table.h, and box_new_pebble() correctly refuses an id that
    // has no row.
    const uint8_t slot = box_new_pebble(1u, (uint8_t)(1u + i * 3u),
                                        ORIGIN_STARTER, gen, 0xC0FFEEu + i, 1000u);
    CHECK(slot != BOX_SLOT_NONE);
    PebbleInstance* p = box_slot(slot);
    if (!p) continue;
    for (uint8_t c = 0; c < PB_CARE_COUNT; ++c)
      p->care[c] = (int32_t)(PB_CARE_MILLI_MAX - (int32_t)c * 12000);
    p->hp_cur = (uint16_t)(15u + i);
    // The second row carries the longest nickname the schema allows: the list
    // has a number, a marker, a name and a right-aligned level to fit in 128 px.
    if (i == 1u) snprintf(p->nickname, sizeof p->nickname, "ABCDEFGHIJKL");
  }
  if (occupied) CHECK(box_set_active(0));
  box_enter();
}

TEST(snapshot_box_list) {
  seams2_reset();
  box_fixture(3);
  snapshot(SCR_BOX, "box_list");
}

TEST(snapshot_box_empty) {
  seams2_reset();
  box_fixture(0);
  snapshot(SCR_BOX, "box_empty");
}

TEST(snapshot_box_actions) {
  seams2_reset();
  box_fixture(3);
  box_input(GST_TAP_L);                 // onto slot 2, which is not the active one
  box_input(GST_HOLD_R);                // choose it
  CHECK_EQ(box_screen_mode(), (uint8_t)BOXM_ACTIONS);
  CHECK_EQ(box_screen_slot(), (uint8_t)1);
  snapshot(SCR_BOX, "box_actions");
}

TEST(snapshot_box_card) {
  seams2_reset();
  box_fixture(3);
  box_input(GST_TAP_L);
  box_input(GST_HOLD_R);                // the action list for slot 2
  box_input(GST_HOLD_R);                // BOXA_VIEW: a stored Pebble's card
  CHECK_EQ(box_screen_mode(), (uint8_t)BOXM_CARD);
  snapshot(SCR_BOX, "box_card");
}

// Spec section 9, and invariants B3 / B4: select active, swap, and a release
// that refuses the Pebble you are carrying before any dialog is opened.
TEST(box_does_what_section_9_says) {
  seams2_reset();
  box_fixture(3);

  // VIEW on the ACTIVE slot goes to the PEBBLE pages, because that one IS the
  // simulated pet; a stored one gets the card above instead.
  CHECK_EQ(box_screen_cursor(), (uint8_t)0);
  box_input(GST_HOLD_R);
  box_input(GST_HOLD_R);
  CHECK_EQ(g_push, (uint8_t)SCR_STATUS);
  CHECK_EQ(box_screen_mode(), (uint8_t)BOXM_ACTIONS);

  // Select active.
  box_enter();
  box_input(GST_TAP_L);                 // slot 2
  box_input(GST_HOLD_R);
  box_input(GST_TAP_L);                 // BOXA_ACTIVATE
  box_input(GST_HOLD_R);
  CHECK_EQ(g_box_active, (uint8_t)1);
  CHECK_EQ(box_screen_mode(), (uint8_t)BOXM_LIST);

  // Swap: pick the slot, pick the target, and the exchange goes through the
  // ui.cpp seam because it has to reach flash.
  box_enter();
  box_input(GST_HOLD_R);                // slot 1
  box_input(GST_TAP_L); box_input(GST_TAP_L);   // BOXA_SWAP
  box_input(GST_HOLD_R);
  CHECK_EQ(box_screen_mode(), (uint8_t)BOXM_SWAP);
  CHECK_EQ(g_toast, STR_BOX_SWAP_PICK);
  box_input(GST_TAP_L);                 // onto slot 2
  box_input(GST_HOLD_R);
  CHECK_EQ(g_box_swap_a, (uint8_t)0);
  CHECK_EQ(g_box_swap_b, (uint8_t)1);
  CHECK_EQ(box_screen_mode(), (uint8_t)BOXM_LIST);

  // B4: the active Pebble is refused before a dialog is ever opened.
  box_enter();                          // opens on the active slot
  const uint8_t active = box_active();
  CHECK(active != BOX_ACTIVE_NONE);
  box_input(GST_HOLD_R);
  for (uint8_t i = 0; i < BOXA_RELEASE; ++i) box_input(GST_TAP_L);
  g_box_released = 0xFF;
  box_input(GST_HOLD_R);
  CHECK_EQ(g_box_released, (uint8_t)0xFF);
  CHECK_EQ(g_toast, STR_BOX_NO_RELEASE_ACTIVE);

  // A stored one is offered, and only to the two dialogs.
  box_enter();
  box_input(GST_TAP_L);
  box_input(GST_HOLD_R);
  for (uint8_t i = 0; i < BOXA_RELEASE; ++i) box_input(GST_TAP_L);
  box_input(GST_HOLD_R);
  CHECK_EQ(g_box_released, (uint8_t)1);

  // TRADE and BREED exist, are reachable, and say which phase brings them.
  box_enter();
  box_input(GST_TAP_L);
  box_input(GST_HOLD_R);
  for (uint8_t i = 0; i < BOXA_TRADE; ++i) box_input(GST_TAP_L);
  box_input(GST_HOLD_R);
  CHECK_EQ(g_toast, STR_UI_SOON);
  CHECK_EQ(box_screen_mode(), (uint8_t)BOXM_ACTIONS);

  // B walks back through the modes and only then leaves the screen.
  g_backs = 0;
  box_input(GST_TAP_R);
  CHECK_EQ(box_screen_mode(), (uint8_t)BOXM_LIST);
  CHECK_EQ(g_backs, 0);
  box_input(GST_TAP_R);
  CHECK_EQ(g_backs, 1);
}

// The ladder is LIST -> ACTIONS -> {SWAP, CARD} and B climbs it ONE RUNG at a
// time, landing back on the action row the sub-mode was opened from - a card
// that dropped straight to the ten slots would skip the level the header
// promises, and a cancelled swap would too.
TEST(box_b_climbs_the_mode_ladder_one_rung_at_a_time) {
  seams2_reset();
  box_fixture(3);
  g_backs = 0;

  // ACTIONS -> CARD -> ACTIONS, on the row that opened it.
  box_input(GST_TAP_L);                 // slot 2, a stored one
  box_input(GST_HOLD_R);
  box_input(GST_HOLD_R);                // BOXA_VIEW
  CHECK_EQ(box_screen_mode(), (uint8_t)BOXM_CARD);
  box_input(GST_TAP_R);
  CHECK_EQ(box_screen_mode(), (uint8_t)BOXM_ACTIONS);
  CHECK_EQ(box_screen_cursor(), (uint8_t)BOXA_VIEW);
  CHECK_EQ(g_backs, 0);

  // The card's own single row means the same thing as B does.
  box_input(GST_HOLD_R);
  CHECK_EQ(box_screen_mode(), (uint8_t)BOXM_CARD);
  box_input(GST_HOLD_R);
  CHECK_EQ(box_screen_mode(), (uint8_t)BOXM_ACTIONS);

  // ACTIONS -> SWAP -> ACTIONS, cancelled by B and by picking the slot itself.
  box_input(GST_TAP_L); box_input(GST_TAP_L);   // BOXA_SWAP
  box_input(GST_HOLD_R);
  CHECK_EQ(box_screen_mode(), (uint8_t)BOXM_SWAP);
  box_input(GST_TAP_R);
  CHECK_EQ(box_screen_mode(), (uint8_t)BOXM_ACTIONS);
  CHECK_EQ(box_screen_cursor(), (uint8_t)BOXA_SWAP);

  g_box_swap_a = 0xFF;
  box_input(GST_HOLD_R);                // into SWAP again, cursor on its own slot
  CHECK_EQ(box_screen_mode(), (uint8_t)BOXM_SWAP);
  box_input(GST_HOLD_R);                // choosing itself is a cancel
  CHECK_EQ(box_screen_mode(), (uint8_t)BOXM_ACTIONS);
  CHECK_EQ(g_box_swap_a, (uint8_t)0xFF);
  CHECK_EQ(g_backs, 0);                 // none of that left the screen
}

// The release commits in ui.cpp, and it has to send the screen back to the ten
// slots: the action list it came from is now about a slot that holds nothing.
TEST(box_screen_to_list_leaves_the_emptied_action_list) {
  seams2_reset();
  box_fixture(3);
  box_input(GST_TAP_L);
  box_input(GST_HOLD_R);
  CHECK_EQ(box_screen_mode(), (uint8_t)BOXM_ACTIONS);

  box_screen_to_list();
  CHECK_EQ(box_screen_mode(), (uint8_t)BOXM_LIST);
  CHECK_EQ(box_screen_cursor(), box_screen_slot());
}

// One placeholder is enough to prove the frame: they differ only in two string
// ids, and test_statemachine.cpp renders every single row. It was SCR_NETWORK
// until P5-C3 built that screen; SCR_TRADE is the same frame with a different
// pair of strings, and the golden moves with it rather than being kept as a
// frozen image of a row nothing can reach (which is what P5-C1 deleted
// snapshot_creator_station for).
TEST(snapshot_soon_trade) {
  seams2_reset();
  snapshot(SCR_TRADE, "soon_trade");
}

// INVARIANT 7, and the audit's S11 defect. The first gesture after the read
// guard DISMISSES the alert and does nothing else: it does not act on the
// alert, and it does not reach the screen underneath either.
TEST(an_alert_never_steals_a_press) {
  seams2_reset();
  dialog_alert(AL_HUNGRY);
  CHECK(dialog_service(g_now, true));
  CHECK_EQ(dialog_modal(), (uint8_t)MODAL_ALERT);

  // Inside the read guard nothing at all happens: the line must be readable
  // before it can be dismissed.
  CHECK(dialog_input(GST_TAP_L));
  CHECK_EQ(dialog_modal(), (uint8_t)MODAL_ALERT);

  g_now += UI_ALERT_MIN_MS;
  CHECK(dialog_input(GST_TAP_L));
  CHECK_EQ(dialog_modal(), (uint8_t)MODAL_NONE);
  // The old alert_act() would have pushed SCR_CARE here. Nothing may.
  CHECK_EQ(g_push, (uint8_t)0xFF);
  CHECK_EQ(g_action, (uint8_t)ACT_NONE);
  CHECK_EQ(g_shown, (uint8_t)ACT_NONE);
  CHECK_EQ(g_medicine, 0);
  CHECK_EQ(g_goto, (uint8_t)0xFF);

  // Every alert, not just the hungry one: the two that used to run an action
  // on the spot are the ones the defect was worst on.
  static const uint8_t kActing[] = { AL_POOP, AL_TIRED, AL_SICK, AL_LOW_HEALTH };
  for (size_t i = 0; i < sizeof kActing / sizeof kActing[0]; i++) {
    dialog_alert(kActing[i]);
    CHECK(dialog_service(g_now, true));
    g_now += UI_ALERT_MIN_MS;
    CHECK(dialog_input(GST_TAP_R));
    CHECK_EQ(dialog_modal(), (uint8_t)MODAL_NONE);
  }
  CHECK_EQ(g_action, (uint8_t)ACT_NONE);
  CHECK_EQ(g_shown, (uint8_t)ACT_NONE);
  CHECK_EQ(g_medicine, 0);
  CHECK_EQ(g_push, (uint8_t)0xFF);

  // Invariant 2 still outranks it: LONG_BOTH dismisses AND goes home, which is
  // not the alert acting - it is the global grammar the alert may not block.
  dialog_alert(AL_SAD);
  CHECK(dialog_service(g_now, true));
  g_now += UI_ALERT_MIN_MS;
  dialog_input(GST_LONG_BOTH);
  CHECK_EQ(g_goto, (uint8_t)SCR_HOME);
}

// The queue is UI_ALERT_QUEUE deep, duplicates collapse, and nothing surfaces
// while something else owns the screen.
TEST(the_alert_queue_collapses_and_waits) {
  seams2_reset();
  dialog_alert(AL_HUNGRY);
  dialog_alert(AL_HUNGRY);
  CHECK_EQ(dialog_alert_pending(), (uint8_t)1);

  for (uint8_t i = 0; i < UI_ALERT_QUEUE + 4u; i++) dialog_alert(AL_POOP);
  CHECK(dialog_alert_pending() <= UI_ALERT_QUEUE);

  // allow_pop false (a running minigame, or a screen that composed its own
  // frame): nothing is raised, and nothing is lost either.
  const uint8_t pending = dialog_alert_pending();
  CHECK(!dialog_service(g_now, false));
  CHECK_EQ(dialog_modal(), (uint8_t)MODAL_NONE);
  CHECK_EQ(dialog_alert_pending(), pending);

  CHECK(dialog_service(g_now, true));
  CHECK_EQ(dialog_alert_id(), (uint8_t)AL_HUNGRY);   // FIFO, not a stack
}

// The one-line help strip expires on its own clock, and any gesture closes it.
TEST(the_help_strip_expires) {
  seams2_reset();
  dialog_open_help(STR_HLP_MEAL);
  CHECK_EQ(dialog_modal(), (uint8_t)MODAL_HELP);
  CHECK(!dialog_service(g_now + UI_MODAL_HELP_MS - 1u, true));
  CHECK_EQ(dialog_modal(), (uint8_t)MODAL_HELP);
  dialog_service(g_now + UI_MODAL_HELP_MS, true);
  CHECK_EQ(dialog_modal(), (uint8_t)MODAL_NONE);
}

// =============================================================================
//  THE MINIGAME RUN FRAMES (P3-C4b, spec section 29)
//
//  Every other snapshot here is a screen-table row; these are not - a game's
//  run frame is drawn INSIDE the GAME screen, under the manager's header and
//  over its affordance strip, neither of which lives in a pure translation
//  unit. So the game is driven headlessly to a chosen step and its draw half is
//  called on the bare framebuffer: what is snapshotted is exactly the content
//  band, which is exactly the part these four commits wrote.
//
//  Note WHICH objects this links: games/*_logic.o AND games/*_draw.o. The draw
//  halves are device code in the sense that they draw, but they draw through
//  gfx.h and nothing else - which is what the seam is for, and what makes spec
//  section 63 ("every screen tested at the actual physical resolution") reach
//  them at all. tests/test_minigames.cpp still links NO draw object, and must
//  not: that binary is where the determinism argument lives.
// =============================================================================
void ping_draw(const MgCtx& c);
void sequence_draw(const MgCtx& c);
void packet_flood_draw(const MgCtx& c);
void firewall_draw(const MgCtx& c);
void buffer_draw(const MgCtx& c);
void delete_draw(const MgCtx& c);

static MgCtx    g_mg;
static MgDrawFn g_mg_draw = nullptr;
static void mg_render_shim(void) { if (g_mg_draw) g_mg_draw(g_mg); }

typedef int8_t (*MgTape)(const MgCtx&, uint32_t);

static void mg_snapshot(const MgLogic& g, MgDrawFn draw, uint32_t seed,
                        uint32_t steps, MgTape tape, const char* name) {
  mg_begin(g_mg, g, seed);
  for (uint32_t i = 0; i < steps && !g_mg.finished; ++i) {
    const int8_t side = tape ? tape(g_mg, i) : (int8_t)-1;
    if (side >= 0) mg_press(g_mg, g, (uint8_t)side);
    if (!g_mg.finished) mg_tick(g_mg, g);
  }
  g_mg_draw = draw;
  snapshot_fn(mg_render_shim, name);
}

// The pursuit tape from test_minigames.cpp, so BUFFER is snapshotted mid-chase
// rather than parked against the wall it starts at - the frame is only worth
// looking at if the cursor is somewhere the player would have put it.
static int8_t snap_buf_track(const MgCtx& c, uint32_t) {
  const int16_t err  = (int16_t)(buf_zone_q4(c) - buf_cursor_q4(c));
  const int16_t want = (int16_t)(buf_zone_vel_q4(c) + err / 4);
  const int16_t v    = buf_vel_q4(c);
  const int16_t half = (int16_t)(buf_kick_q4() / 2);
  if (v + half < want) return MG_SIDE_R;
  if (v - half > want) return MG_SIDE_L;
  return -1;
}

// The triage tape, so DELETE is snapshotted with charges spent and blocks
// cleared instead of an untouched board.
static int8_t snap_del_triage(const MgCtx& c, uint32_t) {
  uint8_t  best = 0xFFu;
  uint16_t best_life = 0xFFFFu;
  for (uint8_t i = 0; i < del_slots(); ++i) {
    if (del_kind_at(c, i) != DEL_KIND_CORRUPT) continue;
    const uint16_t l = del_life_left_ms(c, i);
    if (l < best_life) { best_life = l; best = i; }
  }
  if (best == 0xFFu) return -1;
  return (del_cursor(c) != best) ? (int8_t)MG_SIDE_L : (int8_t)MG_SIDE_R;
}

// Three packets in the lane, the router holding an address and both mouths
// showing theirs: the frame the player spends most of the run reading.
TEST(snapshot_packet_flood_run) {
  mg_snapshot(MG_PACKET_FLOOD, packet_flood_draw, 3u, 60u, nullptr,
              "mg_packet_flood_run");
}

// THE REROUTE, at the step packet 9 spawns: both mouths inverted together, and
// the backlog deep enough to show the "+1".
TEST(snapshot_packet_flood_reroute) {
  mg_snapshot(MG_PACKET_FLOOD, packet_flood_draw, 3u, 234u, nullptr,
              "mg_packet_flood_reroute");
}

// A packet halfway down its lane, the shield still in the middle.
TEST(snapshot_firewall_run) {
  mg_snapshot(MG_FIREWALL, firewall_draw, 2u, 30u, nullptr, "mg_firewall_run");
}

// The 250 ms after an impact the shield was not there for: the wall breached in
// that lane, with the packet coming through the hole.
TEST(snapshot_firewall_leaked) {
  mg_snapshot(MG_FIREWALL, firewall_draw, 2u, 64u, nullptr, "mg_firewall_leaked");
}

// Mid-chase, the caliper inside the zone (so the slab is solid) and the fill
// bar part way along.
TEST(snapshot_buffer_run) {
  mg_snapshot(MG_BUFFER, buffer_draw, 4u, 200u, snap_buf_track, "mg_buffer_run");
}

// THE BOARD the game is about: two corrupted blocks up at once - which the
// schedule forces at least once in every run - plus a healthy one, on three
// different fuse lengths, with every charge still in hand. This is the frame
// the whole selection dimension is made of.
TEST(snapshot_delete_run) {
  mg_snapshot(MG_DELETE, delete_draw, 1u, 100u, nullptr, "mg_delete_run");
}

// Part way through a triage: charges spent, the caret parked on its next target.
TEST(snapshot_delete_purged) {
  mg_snapshot(MG_DELETE, delete_draw, 5u, 200u, snap_del_triage, "mg_delete_purged");
}

// PING, waiting: both halves dark, the round armed and nothing to press yet.
// This is most of the game - the whole point of REFLEJOS is that the cue is
// rare - so it is the frame a player looks at longest.
TEST(snapshot_ping_wait) {
  mg_snapshot(MG_PING, ping_draw, 1u, 20u, nullptr, "mg_ping_wait");
}

// PING, lit: seed 1 arms at step 25, so five steps later the cue is up and half
// the screen is solid on the side the player must hit.
TEST(snapshot_ping_lit) {
  mg_snapshot(MG_PING, ping_draw, 1u, 30u, nullptr, "mg_ping_lit");
}

// SEQUENCE, showing: level 1 (three symbols), the first symbol lit. The half
// of the game the player may not touch.
TEST(snapshot_sequence_show) {
  mg_snapshot(MG_SEQUENCE, sequence_draw, 1u, 5u, nullptr, "mg_sequence_show");
}

// SEQUENCE, answering: the demonstration ended at step 54 and no tape presses
// anything, so the game sits waiting for the player with the progress at 0 of
// 3. The two states have to look different or the game is unplayable, which is
// exactly what a pair of goldens pins.
TEST(snapshot_sequence_answer) {
  mg_snapshot(MG_SEQUENCE, sequence_draw, 1u, 60u, nullptr, "mg_sequence_answer");
}

// =============================================================================
//  THE BATTLE SCREEN (P4-C4)
//
//  ui/screen_battle.cpp is a PURE translation unit that runs the REAL engine
//  and the REAL AI, so these five frames are a battle actually being fought at
//  128x64 - not a mock of one, and not a static sprite standing in for the
//  thing under test. That is what the pure-renderer decision bought: had the
//  field gone through render.h, none of it could have been snapshotted and the
//  goldens would have been of the chrome around a hole.
//
//  DETERMINISM: one fixed seed, a Box built by box_new_pebble(), a clock the
//  seams hold still, and every gesture below is one a player could make.
// =============================================================================
static void battle_choose(uint8_t n) {
  for (uint8_t i = 0; i < n; ++i) {
    battle_input(GST_HOLD_R);            // choose the slot under the cursor
    battle_input(GST_TAP_L);             // step to the next occupied one
  }
  while (battle_screen_cursor() < (uint8_t)BOX_SLOTS) battle_input(GST_TAP_L);
}

static void battle_pick_team(uint8_t n) {
  battle_choose(n);
  battle_input(GST_HOLD_R);              // LISTO
}

// Play until the transcript is showing a beat of `kind`, choosing whatever the
// ring's first legal row is each round. False when the battle ended first.
static bool battle_to_beat(uint8_t kind) {
  for (int guard = 0; guard < 4000; ++guard) {
    const uint8_t m = battle_screen_mode();
    if (m == BTM_RESULT) return false;
    if (m == BTM_RESOLVE) {
      if (battle_screen_event() == kind) return true;
      battle_input(GST_TAP_L);
      continue;
    }
    battle_input(GST_HOLD_R);
  }
  return false;
}

static void battle_to_result(void) {
  for (int guard = 0; guard < 4000 && battle_screen_mode() != BTM_RESULT; ++guard)
    battle_input(GST_HOLD_R);
}

// The team pick: the Box on the shared list widget, three slots chosen, the
// LISTO row carrying 3/3. The only mode the practice entry has that the DIAG
// entry does not.
TEST(snapshot_battle_pick) {
  seams2_reset();
  box_fixture(3);
  battle_arm(BT_ENTRY_PRACTICE, 0xB0A71E01u);
  battle_enter();
  CHECK_EQ(battle_screen_mode(), (uint8_t)BTM_PICK);
  battle_choose(2);                      // two chosen, the cursor left on LISTO
  CHECK_EQ(battle_screen_mode(), (uint8_t)BTM_PICK);
  CHECK_EQ(battle_screen_picked(), 2u);
  snapshot(SCR_BATTLE, "battle_pick");
  battle_leave();
}

// INTRO: both 24x24 bodies on the field, facing each other, both name panels
// and both bench pip tracks. The foe's body is the MIRRORED frame - the flip
// ui/petfx.cpp has always had and nothing in this repository ever drew twice.
TEST(snapshot_battle_intro) {
  seams2_reset();
  box_fixture(3);
  battle_arm(BT_ENTRY_PRACTICE, 0xB0A71E02u);
  battle_enter();
  battle_pick_team((uint8_t)BATTLE_TEAM_MAX);
  CHECK_EQ(battle_screen_mode(), (uint8_t)BTM_INTRO);
  snapshot(SCR_BATTLE, "battle_intro");
  battle_leave();
}

// MENU: four attacks and CAMBIAR on the shared list widget, the round in the
// title and both sides' HP percentages in the header tag - which is where they
// have to be, because the content band belongs to the list.
TEST(snapshot_battle_menu) {
  seams2_reset();
  box_fixture(3);
  battle_arm(BT_ENTRY_PRACTICE, 0xB0A71E02u);
  battle_enter();
  battle_pick_team((uint8_t)BATTLE_TEAM_MAX);
  battle_input(GST_HOLD_R);              // skip the stare-down
  CHECK_EQ(battle_screen_mode(), (uint8_t)BTM_MENU);
  CHECK_EQ(battle_screen_cursor_reject(), (uint8_t)BR_OK);
  snapshot(SCR_BATTLE, "battle_menu");
  battle_leave();
}

// A HIT landing: the move's own name and the damage on the transcript line, the
// struck body inverted, and - the part a golden is really for - the HP bar
// drawn as it stood AT THIS BEAT rather than where the already-resolved round
// left it.
TEST(snapshot_battle_hit) {
  seams2_reset();
  box_fixture(3);
  battle_arm(BT_ENTRY_PRACTICE, 0xB0A71E03u);
  battle_enter();
  battle_pick_team((uint8_t)BATTLE_TEAM_MAX);
  battle_input(GST_HOLD_R);
  CHECK(battle_to_beat((uint8_t)RLE_HIT));
  CHECK_EQ(battle_screen_event(), (uint8_t)RLE_HIT);
  CHECK_EQ(g_shakes, 1);                 // one shake per landed blow, not per frame
  snapshot(SCR_BATTLE, "battle_hit");
  battle_leave();
}

// A FAINT: the fallen body dissolved at 3/4 rather than deleted, so the
// silhouette is still readable while the line says who it was.
TEST(snapshot_battle_faint) {
  seams2_reset();
  box_fixture(3);
  battle_arm(BT_ENTRY_PRACTICE, 0xB0A71E03u);
  battle_enter();
  battle_pick_team((uint8_t)BATTLE_TEAM_MAX);
  battle_input(GST_HOLD_R);
  CHECK(battle_to_beat((uint8_t)RLE_FAINT));
  CHECK_EQ(battle_screen_event(), (uint8_t)RLE_FAINT);
  CHECK_EQ(g_flashes, 1);
  snapshot(SCR_BATTLE, "battle_faint");
  battle_leave();
}

// RESULT. The report has already gone through the ONE path by the time this is
// drawn, which is why g_results is 1 here and stays 1 through battle_leave().
TEST(snapshot_battle_result) {
  seams2_reset();
  box_fixture(3);
  battle_arm(BT_ENTRY_PRACTICE, 0xB0A71E03u);
  battle_enter();
  battle_pick_team((uint8_t)BATTLE_TEAM_MAX);
  battle_input(GST_HOLD_R);
  battle_to_result();
  CHECK_EQ(battle_screen_mode(), (uint8_t)BTM_RESULT);
  CHECK(battle_screen_outcome() != (uint8_t)BO_UNDECIDED);
  CHECK_EQ(g_results, 1);
  snapshot(SCR_BATTLE, "battle_result");
  battle_leave();
  CHECK_EQ(g_results, 1);
}

// The PLAY list grew a row and the fold that keeps it in MgId order grew with
// it. The row order is a static_assert in ui/screen_care.cpp; what a test has
// to hold is that the two NEW branches of play_input() do what the row says -
// the battle row starts a battle and does not launch minigame number 6, and
// "Volver" still leaves.
TEST(play_launches_the_battle_from_its_own_row) {
  seams2_reset();
  play_enter();
  for (uint8_t i = 0; i < PLAY_BATTLE; ++i) play_input(GST_TAP_L);
  CHECK_EQ(play_cursor(), PLAY_BATTLE);
  play_input(GST_HOLD_R);
  CHECK_EQ(g_battle_starts, 1);
  CHECK_EQ(g_battle_entry, (uint8_t)BT_ENTRY_PRACTICE);
  CHECK_EQ(g_minigame, 0xFF);            // and NOT a minigame
  CHECK_EQ(g_backs, 0);

  // The row before it is still the last minigame, which is the mistake the
  // widened fold exists to prevent.
  play_enter();
  for (uint8_t i = 0; i + 1u < PLAY_BATTLE; ++i) play_input(GST_TAP_L);
  play_input(GST_HOLD_R);
  CHECK_EQ(g_minigame, (uint8_t)(PLAY_BATTLE - 1u));
  CHECK_EQ(g_battle_starts, 1);

  // And the row after it still means "leave".
  play_enter();
  for (uint8_t i = 0; i < PLAY_BACK; ++i) play_input(GST_TAP_L);
  play_input(GST_HOLD_R);
  CHECK_EQ(g_backs, 1);
}

// =============================================================================
//  THE EXPLORATION SCREENS (P5-C3/C4)
//
//  These are BEHAVIOUR cases first and snapshots second, because the two things
//  worth proving about the NETWORK screen are not pixels: that B really cancels
//  a scan and that the radio is released on every route off the screen. Both
//  run against the fake WifiScanDriver above - the real one lives in
//  networking/net.cpp, which tests/Makefile never compiles.
// =============================================================================
static ScanResult mk_scan(uint32_t hash, uint8_t cat, int8_t rssi) {
  ScanResult r;
  memset(&r, 0, sizeof r);
  r.net_hash = hash;
  r.category = cat;
  r.rssi     = rssi;
  return r;
}

// Runs the scan to an answer of `n` access points and returns after the update
// that consumed it.
static void run_scan(uint8_t n) {
  network_enter();
  g_drv_ans = WSCAN_POLL_RUNNING;
  network_update(g_now);                 // still running
  g_drv_n   = n;
  g_drv_ans = (int16_t)n;
  network_update(g_now);                 // the answer
}

TEST(the_network_screen_holds_the_radio_only_while_it_is_scanning) {
  seams2_reset();
  explore_reset();

  network_enter();
  CHECK_EQ(g_drv_start, 1);
  CHECK_EQ(g_drv_stop, 0);               // still held
  CHECK_EQ(network_screen_phase(), (uint8_t)NSP_SCANNING);

  // ...and the leave hook releases it EXACTLY ONCE however the player left.
  network_leave();
  CHECK_EQ(g_drv_stop, 1);
  network_leave();                       // idempotent: a second route out
  CHECK_EQ(g_drv_stop, 1);
}

TEST(b_cancels_the_scan_releases_the_radio_and_goes_back) {
  seams2_reset();
  explore_reset();
  network_enter();
  CHECK_EQ(g_drv_stop, 0);

  const int backs = g_backs;
  network_input((Gesture)GST_TAP_R);
  CHECK_EQ(network_screen_phase(), (uint8_t)NSP_CANCELLED);
  CHECK_EQ(g_drv_stop, 1);               // the radio went down with the press
  CHECK_EQ(g_backs, backs + 1);          // and the screen went away
  // No encounter was rolled and nothing was armed.
  CHECK_EQ(g_push, 0xFF);
  CHECK_EQ(g_saves, 0);

  // THE ROW MUST OWN B, or the router would turn it into a plain BACK and the
  // press above would never reach network_input() on the device.
  CHECK((SCREENS[SCR_NETWORK].flags & SF_OWNS_BACK) != 0);
  // ...and it must NOT be sticky: a scan the player walked away from should
  // time out like anything else, and the leave hook is what frees the radio.
  CHECK_EQ((SCREENS[SCR_NETWORK].flags & SF_STICKY), 0);
}

TEST(a_scan_that_times_out_says_so_and_does_not_spin_for_ever) {
  seams2_reset();
  explore_reset();
  network_enter();
  g_drv_ans = WSCAN_POLL_RUNNING;
  network_update(g_now);
  CHECK_EQ(network_screen_phase(), (uint8_t)NSP_SCANNING);

  // Past the software ceiling (spec section 47). The Arduino core's own scan
  // timeout is five times this and cannot tell "timed out" from "never
  // started", which is why the job carries its own clock.
  g_now += (uint32_t)WIFI_SCAN_TIMEOUT_MS + 1u;
  network_update(g_now);
  CHECK_EQ(network_screen_phase(), (uint8_t)NSP_FAILED);
  CHECK_EQ(g_drv_stop, 1);
  CHECK_EQ(g_toast, (uint16_t)STR_NET_FAILED);
  g_now -= (uint32_t)WIFI_SCAN_TIMEOUT_MS + 1u;
}

TEST(a_driver_that_refuses_the_radio_is_a_named_failure_and_not_a_spinner) {
  seams2_reset();
  explore_reset();
  g_drv_ok = false;
  network_enter();
  CHECK_EQ(network_screen_phase(), (uint8_t)NSP_FAILED);
  CHECK_EQ(g_drv_start, 1);
  CHECK_EQ(g_drv_stop, 1);               // released even though it never ran
}

TEST(a_scan_that_finds_nothing_toasts_and_goes_back_without_an_encounter) {
  seams2_reset();
  explore_reset();
  run_scan(0);
  CHECK_EQ(network_screen_phase(), (uint8_t)NSP_EMPTY);
  CHECK_EQ(network_screen_seen(), 0);
  CHECK_EQ(g_toast, (uint16_t)STR_NET_EMPTY);
  CHECK_EQ(g_push, 0xFF);                // no ENCOUNTER
  CHECK_EQ(g_drv_stop, 1);
}

TEST(a_scan_that_finds_a_network_arms_its_cooldown_and_hands_off_an_encounter) {
  seams2_reset();
  explore_reset();
  g_drv_res[0] = mk_scan(0xA1B2C3D4u, (uint8_t)NET_CAT_HOME, -55);
  run_scan(1);
  CHECK_EQ(network_screen_seen(), 1);
  CHECK_EQ(network_screen_fresh(), 1);
  CHECK_EQ(g_drv_stop, 1);
  CHECK(g_saves > 0);                    // the cooldown reached the commit

  // THE COOLDOWN IS ARMED WHETHER OR NOT ANYTHING WAS FOUND. Re-scanning the
  // same network now finds it on cooldown - which is the farm spec section 21
  // exists to close, asserted rather than described.
  const uint8_t first = network_screen_phase();
  CHECK(first == (uint8_t)NSP_HANDOFF || first == (uint8_t)NSP_EMPTY);
  explore_reset_keep_cooldowns();
  g_drv_res[0] = mk_scan(0xA1B2C3D4u, (uint8_t)NET_CAT_HOME, -55);
  run_scan(1);
  CHECK_EQ(network_screen_phase(), (uint8_t)NSP_EMPTY);
  CHECK_EQ(network_screen_seen(), 1);
  CHECK_EQ(network_screen_fresh(), 0);   // seen, but not explorable
  CHECK_EQ(g_toast, (uint16_t)STR_NET_COOLING);
}

TEST(the_encounter_transient_draws_all_four_outcomes_and_only_wild_can_be_steered) {
  seams2_reset();
  explore_reset();

  EncounterResult r;
  memset(&r, 0, sizeof r);

  // WILD: two options, and A on the first one opens CAPTURE.
  r.outcome = (uint8_t)ENC_OUT_WILD; r.species_id = 1; r.level = 7;
  encounter_arm(r, (uint8_t)NET_CAT_HOME);
  encounter_enter();
  CHECK_EQ(encounter_screen_cursor(), 0);
  encounter_input((Gesture)GST_TAP_L);
  CHECK_EQ(encounter_screen_cursor(), 1);          // moved to DEJAR
  encounter_input((Gesture)GST_TAP_L);
  CHECK_EQ(encounter_screen_cursor(), 0);          // and back
  encounter_input((Gesture)GST_HOLD_L);
  CHECK_EQ(g_push, (uint8_t)SCR_CAPTURE);
  // DEJAR is a real answer and leaves without capturing.
  g_push = 0xFF;
  const int backs = g_backs;
  encounter_input((Gesture)GST_TAP_L);
  encounter_input((Gesture)GST_HOLD_L);
  CHECK_EQ(g_push, 0xFF);
  CHECK_EQ(g_backs, backs + 1);

  // ITEM: the drop is in the bag the moment the screen opens, so a stray BACK
  // cannot lose it.
  explore_reset();
  memset(&r, 0, sizeof r);
  r.outcome = (uint8_t)ENC_OUT_ITEM; r.item_id = 1;
  encounter_arm(r, (uint8_t)NET_CAT_HOME);
  CHECK_EQ(inv_count(g_inv, 1), 0);
  encounter_enter();
  CHECK_EQ(inv_count(g_inv, 1), 1);
  CHECK(g_saves > 0);
  // ...and entering twice does not pay twice.
  encounter_enter();
  CHECK_EQ(inv_count(g_inv, 1), 1);
  // Nothing to steer: A does not push anything.
  g_push = 0xFF;
  encounter_input((Gesture)GST_HOLD_L);
  CHECK_EQ(g_push, 0xFF);

  // SPECIAL / XP burst: paid once, through XP_SRC_SPECIAL.
  explore_reset();
  memset(&r, 0, sizeof r);
  r.outcome = (uint8_t)ENC_OUT_SPECIAL; r.event_id = 1;
  r.event_kind = (uint8_t)SPEV_XP_BURST; r.event_value = 5;
  encounter_arm(r, (uint8_t)NET_CAT_HOME);
  encounter_enter();
  CHECK_EQ(g_xp_amt, (uint16_t)(5u * SPECIAL_XP_SCALE));
  CHECK_EQ(g_xp_src, (uint8_t)XP_SRC_SPECIAL);

  // ...and NOT paid at all while the clock is untrustworthy, which is P5-C3's
  // written-down anti-farm choice for an unmetered source.
  explore_reset();
  g_cal = (uint8_t)CAL_UNSET;
  encounter_arm(r, (uint8_t)NET_CAT_HOME);
  encounter_enter();
  CHECK_EQ(g_xp_amt, 0);
  CHECK_EQ(g_xp_src, 0xFF);
  CHECK_EQ(g_toast, (uint16_t)STR_ENC_NO_CLOCK);
  g_cal = (uint8_t)CAL_USER;
}

TEST(the_exploration_screens_render_without_drawing_off_the_panel) {
  seams2_reset();
  explore_reset();
  network_enter();
  snapshot(SCR_NETWORK, "network_scanning");

  EncounterResult r;
  memset(&r, 0, sizeof r);
  r.outcome = (uint8_t)ENC_OUT_WILD; r.species_id = 1; r.level = 7;
  encounter_arm(r, (uint8_t)NET_CAT_HOME);
  encounter_enter();
  snapshot(SCR_ENCOUNTER, "encounter_wild");

  memset(&r, 0, sizeof r);
  r.outcome = (uint8_t)ENC_OUT_SPECIAL; r.event_id = 1;
  r.event_kind = (uint8_t)SPEV_XP_BURST; r.event_value = 5;
  encounter_arm(r, (uint8_t)NET_CAT_HOME);
  encounter_enter();
  snapshot(SCR_ENCOUNTER, "encounter_special");

  memset(&r, 0, sizeof r);
  r.outcome = (uint8_t)ENC_OUT_WILD; r.species_id = 1; r.level = 7;
  encounter_arm(r, (uint8_t)NET_CAT_HOME);
  capture_enter();
  snapshot(SCR_CAPTURE, "capture_ready");
  network_leave();
}

// =============================================================================
//  THE BAG (P5-C4), which is the only way an item ever gets spent.
//
//  Without this surface every ITEM encounter would fill a bag nothing can
//  empty - which is the exact shape of defect this phase spent its content
//  budget closing on item 9.
// =============================================================================
TEST(the_bag_lists_what_is_held_uses_one_and_walks_back_out) {
  seams2_reset();
  explore_reset();
  care_enter();
  CHECK_EQ(care_mode(), (uint8_t)CAREM_LIST);

  // Empty is an ordinary state and draws a line saying so.
  CHECK_EQ(care_bag_rows(), 0);
  while (care_cursor() != (uint8_t)CARE_BAG) care_input(GST_TAP_L);
  care_input(GST_HOLD_R);
  CHECK_EQ(care_mode(), (uint8_t)CAREM_BAG);
  snapshot(SCR_CARE, "care_bag_empty");
  care_input(GST_TAP_R);
  CHECK_EQ(care_mode(), (uint8_t)CAREM_LIST);

  // Two kinds in the bag, one of them a care item the active Pebble needs.
  uint8_t care_id = 0, cap_id = 0;
  for (uint8_t i = 0; i < ITEM_COUNT; ++i) {
    if (ITEMS_TABLE[i].klass == (uint8_t)ITEM_KLASS_CARE && care_id == 0)
      care_id = ITEMS_TABLE[i].id;
    if (ITEMS_TABLE[i].klass == (uint8_t)ITEM_KLASS_CAPTURE && cap_id == 0)
      cap_id = ITEMS_TABLE[i].id;
  }
  CHECK(care_id != 0 && cap_id != 0);
  CHECK_EQ(inv_add(g_inv, care_id, 2), 2);
  CHECK_EQ(inv_add(g_inv, cap_id, 1), 1);
  CHECK_EQ(care_bag_rows(), 2);

  PebbleInstance pet;
  memset(&pet, 0, sizeof pet);
  pet.magic = (uint16_t)PEBBLE_MAGIC;
  pet.layout_ver = (uint8_t)PEBBLE_LAYOUT_VER;
  pet.species_id = 1; pet.id = 0x5EED0009u; pet.level = 5;
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) pet.care[i] = 0;
  g_active_p = &pet;

  care_input(GST_HOLD_R);                       // open the bag again
  CHECK_EQ(care_mode(), (uint8_t)CAREM_BAG);
  CHECK_EQ(care_bag_cursor(), 0);
  snapshot(SCR_CARE, "care_bag_two");

  // THE ROWS ARE IN ITEM-ID ORDER, not pickup order, so the cursor does not
  // reshuffle under the player when something is spent. Which row is which is
  // therefore derived rather than assumed - the capture item is id 4 and the
  // care item id 6, so the bag lists them in that order whatever order they
  // were picked up in.
  const uint8_t care_row = (care_id < cap_id) ? 0u : 1u;
  const uint8_t cap_row  = (uint8_t)(1u - care_row);

  while (care_bag_cursor() != care_row) care_input(GST_TAP_L);
  care_input(GST_HOLD_R);
  CHECK_EQ(g_toast, (uint16_t)STR_ITEM_USED);
  CHECK_EQ(inv_count(g_inv, care_id), 1);
  CHECK(pet.care[0] > 0);
  CHECK(g_saves > 0);

  // A CAPTURE item refuses BY NAME from a menu and is not consumed: it is the
  // one item a player could otherwise throw away by accident.
  while (care_bag_cursor() != cap_row) care_input(GST_TAP_L);
  care_input(GST_HOLD_R);
  CHECK_EQ(g_toast, (uint16_t)STR_ITEM_NOT_HERE);
  CHECK_EQ(inv_count(g_inv, cap_id), 1);

  // The last row is the way out of the mode, not out of the screen.
  const int backs = g_backs;
  while (care_bag_cursor() != care_bag_rows()) care_input(GST_TAP_L);
  care_input(GST_HOLD_R);
  CHECK_EQ(care_mode(), (uint8_t)CAREM_LIST);
  CHECK_EQ(g_backs, backs);
  g_active_p = nullptr;
}

TEST(an_item_used_with_no_active_pebble_is_refused_by_name_and_kept) {
  seams2_reset();
  explore_reset();
  g_active_p = nullptr;
  const uint8_t candy = 1;
  CHECK_EQ(item_get(candy)->klass, (uint8_t)ITEM_KLASS_XP_CANDY);
  CHECK_EQ(inv_add(g_inv, candy, 1), 1);

  care_enter();
  while (care_cursor() != (uint8_t)CARE_BAG) care_input(GST_TAP_L);
  care_input(GST_HOLD_R);
  CHECK_EQ(care_mode(), (uint8_t)CAREM_BAG);
  care_input(GST_HOLD_R);
  CHECK_EQ(g_toast, (uint16_t)STR_ITEM_NO_PET);
  CHECK_EQ(inv_count(g_inv, candy), 1);
}
