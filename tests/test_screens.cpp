// =============================================================================
//  Errata host tests - test_screens.cpp
//  The spec section 63 gate: every migrated screen is rendered at the REAL
//  128x64 into tests/fakes/gfx_fb.cpp and then
//    (a) asserted to have made ZERO out-of-bounds drawing calls, and
//    (b) compared byte for byte against tests/golden/screens/<name>.pbm.
//
//  ./bin/test_screens --record rewrites the goldens; do that only from a
//  commit that means to change what a screen looks like, and read the diff.
//
//  P2-C11a migrated BOOT, LOAD_SAVE and ERROR; P2-C11b added HOME, MENU, the
//  CARE and PLAY lists, both BUG pages, SETTINGS (list and "Acerca de") and
//  the TIME entry screen; P2-C11c added LINK, EVOLUTION, DIAG, CREATOR and the
//  CONFIRM / ALERT / HELP overlays. Every later screen adds its fixtures here.
//
//  TWO FIXTURES render on every screen that shows a Bug: a fresh starter,
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
#include "fakes/alloc_count.h"
#include "fakes/gfx_fb.h"
#include "game/box.h"
#include "minigames/games/games.h"
#include "minigames/minigame.h"
#include "persistence/save_manager.h"
#include "ui/dialog.h"
#include "ui/screen.h"
#include "ui/screen_care.h"
#include "hardware/power.h"
#include "ui/screen_creator.h"
#include "ui/screen_diag.h"
#include "ui/screen_evolution.h"
#include "ui/screen_error.h"
#include "ui/screen_home.h"
#include "ui/screen_encounter.h"
#include "ui/screen_link.h"
#include "networking/discovery.h"
#include "fakes/link_fake.h"
#include "ui/screen_menu.h"
#include "ui/screen_network.h"
#include "networking/wifi_scanner.h"
#include "game/capture.h"     // CaptureOutcome, for the capture film
#include "ui/corrupt_fx.h"    // the glitch gate and its rows, for the HOME painter
#include "ui/petfx_core.h"    // pf_scan_ink(): the body's own ink box
#include "ui/pet_art.h"
#include "game/cooldowns.h"
#include "game/genome.h"
#include "game/inventory.h"
#include "game/xp.h"
#include "game/battle.h"      // BattleReject / BattleLogEvent, for the battle snapshots
#include "ui/screen_battle.h"
#include "ui/battle_renderer.h"
#include "ui/screen_box.h"
#include "core/utf8.h"
#include "ui/screen_boot.h"
#include "ui/screen_settings.h"
#include "ui/screen_setup.h"
#include "ui/screen_soon.h"
#include "ui/screen_status.h"
#include "ui/screen_time.h"
#include "ui/screen_view.h"
#include "ui/gfx.h"        // gfx_xbm(): the clipping proof draws one directly
#include "ui/pet_art.h"    // pet_art_key(): the body box a species-difference check measures
#include "data/attacks_table.h"   // ATTACK_COUNT: the move-set search walks it
#include "game/validate.h"        // creator_cost_of(): a legal record prices itself
#include "game/species_custom.h"  // the registry the drawn body comes out of
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
static uint8_t  g_root      = 0xFF;   // the last ui_replace_root()
static int      g_cfg_silent = 0;     // config writes that raised no toast
static uint8_t  g_starter   = 0;      // the last ui_set_starter() argument
static int      g_starter_calls = 0;
static bool     g_starter_ok = true;
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
// The D7 grace period, as the CREATOR screen sees it. On the device
// networking/creator_gate.cpp decides this from a monotonic clock and
// webui.cpp answers it; here the test sets it, because the SCREEN's half of
// the property is "what does it do when told", and the decision itself has its
// own binary (tests/test_creator_gate.cpp).
static uint8_t  g_idle_exp = 0;
// THE PIN THE SCREEN IS TOLD TO PRINT. It was the literal 1234 inside
// ui_creator_info() until P8-C5, which is fine for a golden and useless for the
// spec-39 property below: "the QR carries no secret" is a claim about EVERY
// pin, and a fixture that can only produce one cannot make it.
static uint16_t g_pin      = 1234;
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
// THE FIRST-BOOT SEAMS (P10-C4). ui_replace_root() is recorded SEPARATELY from
// ui_goto(): the difference between them - does the back stack survive - is
// exactly what the setup flow depends on, so a test that could not tell them
// apart could not say the flow re-roots rather than stacking.
void ui_replace_root(ScreenId s) { g_root = (uint8_t)s; }
void ui_setup_persist(void)      { g_cfg_silent++; }
bool ui_set_starter(uint8_t sp)  { g_starter = sp; g_starter_calls++; return g_starter_ok; }
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
// The wild fight (the encounter's third answer). Recorded rather than run: the
// real one draws a seed and navigates, and neither belongs in a screen test.
static uint8_t g_wild_sp = 0, g_wild_lv = 0; static int g_wild_starts = 0;
void ui_start_wild_battle(uint8_t sp, uint8_t lv) {
  g_wild_sp = sp; g_wild_lv = lv; ++g_wild_starts;
}
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
  out.idle_expired = g_idle_exp;
  out.pin    = g_pin;
  snprintf(out.ssid, sizeof out.ssid, "ERRATA-1234");
  snprintf(out.ip,   sizeof out.ip,   "192.168.4.1");
  // NO "?k=NNNN" SINCE P8-C1. net_url() lost the PIN and the argument that
  // carried it (spec section 39); this fixture matches what net.cpp now emits,
  // and tools/check.sh has the gate that stops the query parameter coming back
  // - a fixture could only ever assert what the fixture typed.
  if (g_ap_up) snprintf(out.url, sizeof out.url, "http://192.168.4.1/");
}
bool ui_btn_down(uint8_t)         { return false; }
uint32_t ui_btn_hold_ms(uint8_t)  { return 0; }

void ui_info_lines(char lines[UI_INFO_LINES][UI_INFO_CAP]) {
  // Fixed text: the real ones carry a heap figure and an IP, neither of which
  // a golden could ever be stable about.
  snprintf(lines[0], UI_INFO_CAP, "ERRATA 0.2.0");
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
static uint8_t       g_save_slot = 0xFFu;  // and the slot it was told about
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
void ui_explore_commit(uint8_t s) { g_saves++; g_save_slot = s; }
Genome ui_fresh_genome(void) {
  Genome g;
  memset(&g, 0, sizeof g);
  g.lineage_id = 0x51DE0001u;
  g.g0 = 0x1234u; g.g1 = 0x5678u; g.g2 = 0x09ABu;
  g.generation = 1u;
  genome_seal(g);
  return g;
}
static BugInstance* g_active_p = nullptr;
void ui_award_xp(uint16_t amount, uint8_t src) { g_xp_amt = amount; g_xp_src = src; }
BugInstance* ui_active_bug(void) { return g_active_p; }

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
//  THE TWO BUG FIXTURES
// =============================================================================
static BugView g_view;

static const BugView* fixture_view(void) { return &g_view; }

static void fixture_common(void) {
  memset(&g_view, 0, sizeof g_view);
  g_view.present     = 1;
  // A fixed genome: deterministic, and every gene accessor the BUG page
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
// nickname (ER_NICKNAME_CAP - 1), level 30, and every meter pinned at 100.
static void fixture_maxed(void) {
  fixture_common();
  snprintf(g_view.name, sizeof g_view.name, "ABCDEFGHIJKL");
  // Level 30 is the top of the curve: xp_for_level() answers 0 there and the
  // Bug holds no in-level XP, which is what makes the HOME rule solid.
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
  g_root = 0xFF;
  g_cfg_silent = 0;
  g_starter = 0;
  g_starter_calls = 0;
  g_starter_ok = true;
  g_flushes = 0;
  g_cfg_p = &g_cfg;
  memset(&g_cfg, 0, sizeof g_cfg);
  g_cfg.brightness = OLED_CONTRAST_DEFAULT;
  g_frames = 0;
  g_hatches = 0;
  g_radio = -1;
  g_radio_calls = 0;
  g_ap_up = 0;
  g_idle_exp = 0;
  g_pin = 1234;
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
//  THE TWO WORK GATES ON A SCREEN RENDER (P10-C2, spec section 46)
//
//  (a) ZERO ALLOCATIONS. This is the only honest HOST half of section 46's
//  "per-frame heap delta 0 on HOME/BATTLE/GAME", and it is worth being precise
//  about which half. The device figure is ESP.getFreeHeap() sampled either side
//  of a frame and it has no host equivalent; what a host CAN say is that
//  rendering a screen performs no allocation at all, which is the property that
//  makes the device figure zero in the first place. It is EXACT - the answer is
//  0, not "small" - it cannot drift with content, and nobody had ever asserted
//  it. tests/fakes/alloc_count.h carries what the counter does and does not see;
//  docs/bench.md step A3 is where the real allocator is watched.
//
//  (b) A COMPOSITING CEILING on the work counters in tests/fakes/gfx_fb.cpp.
//  MEASURED over all 65 snapshots at this commit: the worst is creator_portal
//  at 5,933 pixels and 212 leaf primitives, and the median is far below both.
//  The ceiling is three whole panels (24,576 px) and 600 primitives - loose
//  enough that a legitimate redesign or P10-C4's twelve-character-name fixtures
//  will not trip it, tight enough that a screen drawing itself three times over
//  will.
//
//  IT HAS BEEN SEEN TO FIRE, which is the only reason it is here rather than
//  being a comment. Eight whole-panel gfx_invert_rect() calls inserted at the
//  top of menu_render() are an IDENTITY - the golden does not move by one byte,
//  every pixel comparison still passes - and this ceiling fails snapshot_menu
//  and snapshot_menu_countdown by name at 68,335 pixels. That is the class of
//  regression a pixel golden structurally cannot report.
//
//  AND IT IS WORK, NOT TIME, AND MUST NEVER BECOME A FRAME BUDGET. The frame's
//  dominant cost is a fixed ~24 ms of I2C that no host number contains, HOME's
//  composition here is not the one that ships (ui/petfx.cpp is host-invisible,
//  so s_body is null and the wandering automaton is absent), and ui_draw()
//  composites a toast, the affordance echo, the transition and the god marker
//  on top of everything measured here. See the banner in tests/fakes/gfx_fb.cpp.
// =============================================================================
#define SNAP_MAX_PIXELS  ((uint32_t)(FB_W * FB_H * 3))
#define SNAP_MAX_OPS     ((uint32_t)600)

// A counter that reads 0 because it is switched off is indistinguishable from
// one that reads 0 because the code is clean, and every snapshot below depends
// on knowing which. Found by the mutation run on tests/test_soak.cpp's twin of
// this counter: disarming it left every case green and measuring nothing.
TEST(the_allocation_counter_is_live) {
  const uint32_t before = alloc_count();
  alloc_count_probe();
  CHECK_EQ(alloc_count() - before, 1u);
}

// =============================================================================
//  ...AND SO ARE THE TWO TEXT RECORDERS, which is the same rule applied to the
//  two counters that were BOUGHT WITH SHIPPED DEFECTS and had never been held
//  to it. Added at the FINAL REVIEW.
//
//  fb_bad_utf8() came from P10-C4 (a truncation that split a multi-byte
//  sequence) and fb_no_glyph() from P10-C6 (five accented Spanish strings drawn
//  in GF_TINY, a 95-glyph ASCII-only face, which rendered correctly in every
//  golden and on no board). Every assertion on either is `== 0u`, and the only
//  protection they had was two GREPS in tools/check.sh that COUNT THE FUNCTION
//  NAME - which the definition line and the single call site satisfy whatever
//  the body does.
//
//  MEASURED: putting the PRE-P10-C6 body back - font_has() returning true for
//  every codepoint, the exact defect that chunk was created to fix - plus a
//  one-line disarm of note_text(), left the full gate at ALL PASS 59/59,
//  ASAN OK 8/8, PAGE TEST OK 51/51, GATE OK. Both instruments could be switched
//  off with everything green.
//
//  These two cases render into a buffer no snapshot reads, so they move no
//  golden. They are the difference between the next accented-name defect being
//  caught by the suite and being caught by the owner on a board.
// =============================================================================
TEST(the_malformed_text_recorder_is_live) {
  fb_reset();
  CHECK_EQ(fb_bad_utf8(), 0u);
  // A bare Latin-1 n-tilde: a lead byte with no continuation. This is the SHAPE
  // P10-C4's truncation produced, and core/utf8.cpp's u8_len() is what refuses
  // to believe it.
  gfx_text(GF_BODY, 0, 20, "Ni\xF1o");
  if (fb_bad_utf8() == 0u)
    fprintf(stderr, "  note_text() recorded nothing for malformed UTF-8: the "
                    "P10-C4 recorder is switched off and every `== 0u` "
                    "assertion in this file is vacuous\n");
  CHECK(fb_bad_utf8() > 0u);
  CHECK(fb_bad_utf8_first()[0] != '\0');   // ...and it says which string
  fb_reset();
  CHECK_EQ(fb_bad_utf8(), 0u);             // fb_reset() really clears it
}

TEST(the_font_repertoire_recorder_is_live) {
  fb_reset();
  CHECK_EQ(fb_no_glyph(), 0u);
  // U+00F1 in GF_TINY. u8g2_font_4x6_tr is ASCII only, and drawUTF8() emits
  // nothing AND ADVANCES NOTHING for a codepoint the face lacks - the character
  // does not become a box, it disappears and the line closes up. That is
  // exactly what shipped in five strings at P10-C6.
  gfx_text(GF_TINY, 0, 20, "\xC3\xB1");
  if (fb_no_glyph() == 0u)
    fprintf(stderr, "  note_glyph() recorded nothing for U+00F1 in GF_TINY: the "
                    "P10-C6 recorder is switched off\n");
  CHECK(fb_no_glyph() > 0u);
  CHECK(fb_no_glyph_first()[0] != '\0');

  // ...and it is per-FONT, not a blanket refusal: the same codepoint in a _tf
  // face is legitimate and must not be recorded. A recorder that fired for
  // everything would be as useless as one that fired for nothing, and would
  // make every accented string in the product unusable.
  fb_reset();
  gfx_text(GF_BODY, 0, 20, "\xC3\xB1");
  CHECK_EQ(fb_no_glyph(), 0u);

  // GF_BIG is the digits-only face; a letter there is the same class.
  fb_reset();
  gfx_text(GF_BIG, 0, 30, "A");
  CHECK(fb_no_glyph() > 0u);
  fb_reset();
  gfx_text(GF_BIG, 0, 30, "12:30");
  CHECK_EQ(fb_no_glyph(), 0u);
}

// =============================================================================
//  THE DRAWING SEAM'S TWO BLITS (P10-C3)
//
//  THIS IS THE TEST THAT WOULD HAVE CAUGHT A LATENT DIVERGENCE OF FIVE PHASES.
//  ui/gfx_u8g2.cpp's gfx_xbm() forwards to drawXBM with the panel in
//  setBitmapMode(0) (ui/render.cpp:368), which paints the ENTIRE w*h box - the
//  1-bits in the draw colour and the 0-bits in the INVERSE. tests/fakes/gfx_fb.
//  cpp drew only the 1-bits. So the two backends disagreed about every blit in
//  the firmware and nothing in the tree could see it.
//
//  IT WAS UNOBSERVED BY LUCK OF COMPOSITION, not by design: every one of the
//  thirteen call sites drew onto blank ground, or (ui/dialog.cpp:130) in
//  GFX_ERASE onto a solid slab where the inverse writes 1 over 1. Correcting
//  the fake moved ZERO of the sixty-five goldens, which is the receipt for that
//  claim - and P10-C3 is the chunk that would have ended the luck, because its
//  item film drives an icon straight across its own label.
//
//  Both behaviours are asserted here BY NAME, in both draw colours, so neither
//  backend can drift back.
// =============================================================================
TEST(an_opaque_blit_paints_its_whole_box_and_a_transparent_one_only_its_ink) {
  // A sprite with a hole: 8x8, all set except the middle 4x4.
  uint8_t spr[8];
  for (int y = 0; y < 8; ++y) spr[y] = 0xFFu;
  for (int y = 2; y < 6; ++y) spr[y] = (uint8_t)(0xFFu & ~0x3Cu);   // clear x 2..5

  // Ground: a solid 12x12 slab the blit lands in the middle of.
  fb_reset();
  gfx_fill(10, 10, 12, 12);
  gfx_xbm_t(12, 12, 8, 8, spr);
  // TRANSPARENT: the hole is still slab, so the whole 12x12 is lit.
  int lit = 0;
  for (int y = 10; y < 22; ++y) for (int x = 10; x < 22; ++x) lit += fb_get(x, y);
  CHECK_EQ(lit, 144);

  fb_reset();
  gfx_fill(10, 10, 12, 12);
  gfx_xbm(12, 12, 8, 8, spr);
  // OPAQUE: the hole is punched THROUGH the slab - sixteen pixels of it.
  lit = 0;
  for (int y = 10; y < 22; ++y) for (int x = 10; x < 22; ++x) lit += fb_get(x, y);
  CHECK_EQ(lit, 144 - 16);
  for (int y = 14; y < 18; ++y)
    for (int x = 14; x < 18; ++x) CHECK_EQ(fb_get(x, y), 0);

  // AND IN GFX_ERASE, where the inverse runs the other way: the 1-bits clear
  // and the 0-bits DRAW. This is the arm ui/dialog.cpp:130 uses, and the arm
  // that made the old fake look correct on a solid slab.
  fb_reset();
  gfx_fill(10, 10, 12, 12);
  gfx_color(GFX_ERASE);
  gfx_xbm(12, 12, 8, 8, spr);
  gfx_color(GFX_DRAW);
  lit = 0;
  for (int y = 10; y < 22; ++y) for (int x = 10; x < 22; ++x) lit += fb_get(x, y);
  // The 48 set bits cleared; the 16 hole bits drew over slab that was already
  // lit, so they change nothing. 144 - 48.
  CHECK_EQ(lit, 144 - 48);

  // The transparent blit in GFX_ERASE clears only the ink and leaves the hole.
  fb_reset();
  gfx_fill(10, 10, 12, 12);
  gfx_color(GFX_ERASE);
  gfx_xbm_t(12, 12, 8, 8, spr);
  gfx_color(GFX_DRAW);
  lit = 0;
  for (int y = 10; y < 22; ++y) for (int x = 10; x < 22; ++x) lit += fb_get(x, y);
  CHECK_EQ(lit, 144 - 48);
  for (int y = 14; y < 18; ++y)
    for (int x = 14; x < 18; ++x) CHECK_EQ(fb_get(x, y), 1);

  // The colour is STICKY and both blits must hand it back untouched, which is
  // the contract gfx_color() states and the one a blit that flipped a mode
  // internally is most likely to break.
  fb_reset();
  gfx_color(GFX_ERASE);
  gfx_xbm_t(0, 0, 8, 8, spr);
  gfx_fill(40, 40, 4, 4);            // still ERASE: draws nothing on blank
  CHECK_EQ(fb_get(41, 41), 0);
  gfx_color(GFX_DRAW);
}

// The phase-shifted dither, which ui/corrupt_fx.cpp's rows carry and which no
// host backend implemented until P10-C3. Two different phases of the same
// rectangle must be two different pictures, or the glitch's shimmer is a
// constant.
// =============================================================================
//  THE GEOMETRY PRIMITIVES CLIP AND COMPOSE, AND THE RULE IS ui/gfx.h's
//  Added at the FINAL REVIEW, because `tests/fakes/SHADOWS.txt` marks eight of
//  these IMITATION and an IMITATION row has to name an assertion that actually
//  holds the claim. Before this they all pointed at the two-blits case above,
//  which pins the COMPOSITING rule and says nothing about clipping — nine rows
//  resting on one test about a different function is the shape of overclaim this
//  review exists to find, and writing it into the manifest would have been the
//  same defect one level up.
//
//  What the eleven non-text primitives are held by in this tree: these
//  assertions, plus the 75 goldens. They were separately measured pixel-exact
//  against the real vendored U8g2 at the final review over a 223,776-case
//  differential sweep — every draw colour, every dither level and phase, every
//  panel edge, the degenerate rectangles — but that harness needs the Arduino
//  library tree and is not committed, so it is a measurement taken once and NOT
//  a standing instrument. These are the standing part.
//
//  ui/gfx.h's rule: "Out-of-range rectangles are clipped, never wrapped."
// =============================================================================
// A COUNT OF LIT PIXELS, which is not what fb_pixels() is - that is a count of
// WRITES inside the panel, so an XOR that turns a pixel off still counts one.
// The distinction is the point of half the cases below.
static uint32_t fb_lit(void) {
  uint32_t n = 0;
  for (int y = 0; y < FB_H; ++y)
    for (int x = 0; x < FB_W; ++x) n += (uint32_t)(fb_get(x, y) ? 1 : 0);
  return n;
}

TEST(every_geometry_primitive_clips_instead_of_wrapping) {
  // Off the left and top edges: the visible part is drawn at the panel edge and
  // NOTHING appears on the opposite side. A wrap would light the right-hand
  // columns, which on the device is what an unguarded negative origin does to a
  // u8g2_uint_t (unsigned) - the hazard gfx_pixel's own comment names.
  fb_reset();
  gfx_fill(-4, -4, 8, 8);
  CHECK_EQ(fb_lit(), 16u);                    // the 4x4 that is on the panel
  for (int y = 0; y < 4; ++y)
    for (int x = 0; x < 4; ++x) CHECK_EQ(fb_get(x, y), 1);
  for (int y = 0; y < 8; ++y) CHECK_EQ(fb_get(FB_W - 1, y), 0);   // no wrap

  // Off the right and bottom edges, the same both ways.
  fb_reset();
  gfx_fill((int16_t)(FB_W - 4), (int16_t)(FB_H - 4), 8, 8);
  CHECK_EQ(fb_lit(), 16u);
  for (int y = 0; y < FB_H; ++y) CHECK_EQ(fb_get(0, y), 0);

  // Fully outside draws nothing at all. (fb_oob() DOES rise - it is the
  // recorder doing its job on a draw the product would never make - so these
  // cases are deliberately kept away from the snapshots that assert it is 0.)
  fb_reset();
  gfx_fill(-40, -40, 8, 8);
  gfx_fill(200, 200, 8, 8);
  gfx_rect(-40, -40, 8, 8);
  gfx_hline(-40, -40, 8);
  gfx_vline(-40, -40, 8);
  gfx_pixel(-1, -1);
  gfx_pixel(FB_W, FB_H);
  CHECK_EQ(fb_lit(), 0u);

  // Degenerate rectangles draw nothing. A zero-width fill that painted one
  // column would put ink in every list highlight in the product.
  fb_reset();
  gfx_fill(10, 10, 0, 8);
  gfx_fill(10, 10, 8, 0);
  gfx_rect(10, 10, 0, 8);
  gfx_rect(10, 10, 8, 0);
  CHECK_EQ(fb_lit(), 0u);

  // gfx_rect is an OUTLINE and gfx_fill is solid; conflating them would make
  // every panel and card in the UI a slab.
  fb_reset(); gfx_rect(10, 10, 10, 10);
  const uint32_t outline = fb_lit();
  const int mid_after_rect = fb_get(14, 14);
  fb_reset(); gfx_fill(10, 10, 10, 10);
  const uint32_t solid = fb_lit();
  CHECK_EQ(solid, 100u);
  CHECK_EQ(outline, 36u);                     // 10x10 border: 100 - 8x8 interior
  CHECK(outline < solid);
  CHECK_EQ(mid_after_rect, 0);                // ...and the outline is hollow

  // The hline/vline halves agree with the box they bound, and clip on both
  // ends rather than wrapping.
  fb_reset(); gfx_hline(5, 5, 10); CHECK_EQ(fb_lit(), 10u);
  fb_reset(); gfx_vline(5, 5, 10); CHECK_EQ(fb_lit(), 10u);
  fb_reset(); gfx_hline(-5, 5, 10); CHECK_EQ(fb_lit(), 5u);
  fb_reset(); gfx_vline(5, -5, 10); CHECK_EQ(fb_lit(), 5u);
  fb_reset(); gfx_pixel(3, 3); CHECK_EQ(fb_lit(), 1u); CHECK_EQ(fb_get(3, 3), 1);
}

TEST(the_draw_colour_is_sticky_and_erase_is_the_inverse_of_draw) {
  // ui/gfx.h: "Sticky, exactly like u8g2's setDrawColor: whoever changes it puts
  // it back to GFX_DRAW before returning." Everything composited on this panel
  // depends on it, and gfx_color() is one of the fifty shadowed functions.
  fb_reset();
  gfx_fill(0, 0, 16, 16);
  CHECK_EQ(fb_lit(), 256u);

  gfx_color(GFX_ERASE);
  gfx_fill(4, 4, 8, 8);                       // a hole in the slab
  CHECK_EQ(fb_get(5, 5), 0);
  CHECK_EQ(fb_get(1, 1), 1);
  CHECK_EQ(fb_lit(), 192u);
  // STICKY: the colour is still ERASE until somebody puts it back.
  gfx_fill(0, 0, 2, 2);
  CHECK_EQ(fb_get(0, 0), 0);
  gfx_color(GFX_DRAW);
  gfx_fill(0, 0, 2, 2);
  CHECK_EQ(fb_get(0, 0), 1);
  gfx_color(GFX_DRAW);

  // XOR flips what is there, both ways, which is what makes it XOR.
  fb_reset();
  gfx_fill(0, 0, 8, 8);
  CHECK_EQ(fb_lit(), 64u);
  gfx_color(GFX_XOR);
  gfx_fill(0, 0, 8, 8);
  CHECK_EQ(fb_lit(), 0u);                     // lit ^ 1 -> dark
  gfx_fill(0, 0, 8, 8);
  CHECK_EQ(fb_lit(), 64u);                    // ...and dark ^ 1 -> lit
  gfx_color(GFX_DRAW);

  // gfx_invert_rect is the same idea with its own entry point, and it must
  // restore the colour itself: every list highlight in the product is one.
  fb_reset();
  gfx_fill(0, 0, 8, 8);
  gfx_invert_rect(0, 0, 8, 8);
  CHECK_EQ(fb_lit(), 0u);
  gfx_fill(20, 20, 4, 4);                     // still GFX_DRAW afterwards
  CHECK_EQ(fb_get(20, 20), 1);
  CHECK_EQ(fb_oob(), 0u);                     // nothing here left the panel
}

TEST(the_dither_phase_actually_slides_the_matrix) {
  static uint8_t at_zero[FB_H][FB_W];
  fb_reset();
  gfx_dither_rect_phase(8, 8, 16, 16, GFX_D50, 0u);
  for (int y = 0; y < FB_H; ++y)
    for (int x = 0; x < FB_W; ++x) at_zero[y][x] = (uint8_t)fb_get(x, y);
  // gfx_dither_rect() IS phase 0 - the two must be the same picture, or every
  // screen drawn before P10-C3 has quietly moved.
  fb_reset();
  gfx_dither_rect(8, 8, 16, 16, GFX_D50);
  int same = 0;
  for (int y = 0; y < FB_H; ++y)
    for (int x = 0; x < FB_W; ++x) same += (fb_get(x, y) == at_zero[y][x]) ? 1 : 0;
  CHECK_EQ(same, FB_H * FB_W);

  // ...and phase 1 is a DIFFERENT picture with the same amount of ink.
  fb_reset();
  gfx_dither_rect_phase(8, 8, 16, 16, GFX_D50, 1u);
  int diff = 0, ink = 0, ink0 = 0;
  for (int y = 0; y < FB_H; ++y)
    for (int x = 0; x < FB_W; ++x) {
      const int v = fb_get(x, y);
      ink += v; ink0 += at_zero[y][x];
      if (v != (int)at_zero[y][x]) ++diff;
    }
  CHECK(diff > 0);
  CHECK_EQ(ink, ink0);
  CHECK_EQ(ink, 16 * 16 / 2);        // D50 over a 16x16 is exactly half
}

// =============================================================================
//  Snapshot helper
// =============================================================================
static void snapshot_fn(void (*render)(void), const char* name) {
  fb_reset();
  const uint32_t allocs_before = alloc_count();
  render();
  const uint32_t allocs = alloc_count() - allocs_before;

  // (a) nothing may draw off the panel.
  if (fb_oob() != 0) {
    fprintf(stderr, "  %s: %u out-of-bounds primitive(s), first %s\n",
            name, (unsigned)fb_oob(), fb_oob_first());
  }
  CHECK_EQ(fb_oob(), 0u);

  // (a1) and nothing may hand the font a byte sequence it cannot decode. The
  // same kind of instrument as the recorder above and for the same reason: a
  // name is stored as raw Latin-1, every list row is composed with
  // snprintf("%s") which cuts on a BYTE, and on the device a broken lead byte
  // does not merely draw wrong - u8g2 opens a multi-byte state and swallows
  // the NEXT character too. tests/fakes/gfx_fb.cpp carries the argument.
  if (fb_bad_utf8() != 0) {
    fprintf(stderr, "  %s: %u malformed UTF-8 string(s) drawn, first \"%s\"\n",
            name, (unsigned)fb_bad_utf8(), fb_bad_utf8_first());
  }
  CHECK_EQ(fb_bad_utf8(), 0u);

  // (a1b) AND NO GLYPH MAY BE DRAWN IN A FONT THAT HAS NO SUCH GLYPH (P10-C6).
  // GF_TINY is ASCII-only and u8g2's drawUTF8() emits nothing and advances
  // nothing for a codepoint the face lacks: the character vanishes and the line
  // closes up. This fake used to paint every codepoint at a fixed advance, so
  // an accented Spanish string in GF_TINY was correct in every golden and wrong
  // on every board. It is a property of the seam now.
  if (fb_no_glyph() != 0) {
    fprintf(stderr, "  %s: %u glyph(s) the font cannot draw, first %s\n",
            name, (unsigned)fb_no_glyph(), fb_no_glyph_first());
  }
  CHECK_EQ(fb_no_glyph(), 0u);

  // (a2) and nothing may allocate. A screen render is a pure function of the
  // model into a fixed buffer; the day one is not, the device's per-frame heap
  // delta stops being zero and this is the line that says so first.
  if (allocs != 0u)
    fprintf(stderr, "  %s: %u allocation(s) during render - spec 46 wants a "
                    "per-frame heap delta of 0\n", name, (unsigned)allocs);
  CHECK_EQ(allocs, 0u);

  // (a3) and it may not composite itself into the ground. Work, not time.
  if (fb_pixels() > SNAP_MAX_PIXELS || fb_ops() > SNAP_MAX_OPS)
    fprintf(stderr, "  %s: %u pixels / %u primitives - over the compositing "
                    "ceiling (%u / %u). This is a WORK ceiling, not a frame "
                    "budget: see tests/fakes/gfx_fb.cpp\n",
            name, (unsigned)fb_pixels(), (unsigned)fb_ops(),
            (unsigned)SNAP_MAX_PIXELS, (unsigned)SNAP_MAX_OPS);
  CHECK(fb_pixels() <= SNAP_MAX_PIXELS);
  CHECK(fb_ops() <= SNAP_MAX_OPS);

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
  // SCR_LINK LEFT THIS LIST AT P7-C2 for the reason SCR_NETWORK left it at
  // P5-C3 and then some: B means CANCEL THE LINK there, and a link holds the
  // radio AND a session.
  // SCR_CREATOR LEFT IT AT P8-C2, and it is the row this list was WRONG about
  // rather than one whose rules changed. It held the radio all along, and the
  // 20 s auto-return measures device GESTURES - so the one screen whose whole
  // purpose is that the user is holding a phone instead was also the one screen
  // the timeout was guaranteed to fire on. It is SF_STICKY now and owns two
  // timeouts of its own; both are asserted by name below.
  static const uint8_t kOrdinary[] = {
    SCR_MENU, SCR_PLAY, SCR_STATUS, SCR_STATUS_B,
    SCR_ENCOUNTER, SCR_SLEEP
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
                                      SCR_BATTLE, SCR_LINK };
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
  // CREATOR: sticky and nothing else - it does NOT own B (B is BACK, and its
  // leave hook is the teardown either way) and it does not lock input.
  CHECK_EQ(SCREENS[SCR_CREATOR].flags, (uint8_t)SF_STICKY);
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
//  THE TWO POSE BODIES, RECORDED (P9-C3)
//
//  P9-C3 replaced seven legacy pose sets with ONE generic SLEEP and ONE generic
//  SICK, and dropped EAT's art entirely. Before these two cases NOTHING DREW
//  EITHER OF THEM: `pet_pose_of()` returns POSE_SLEEP and POSE_SICK from the
//  simulation, HOME shows them on the still path, and not one of the 63
//  recorded goldens contained a pose body - so two brand-new 24x24 drawings
//  would have shipped with the suite green and nobody's eye on them.
//
//  They are the SAME fixture as home_starter with one field changed, so the
//  pair of goldens beside it is a diff a reviewer can read: the body is the
//  only thing that may move.
// =============================================================================
TEST(snapshot_home_sleeping) {
  seams2_reset();
  fixture_starter();
  g_view.pose = POSE_SLEEP;
  snapshot(SCR_HOME, "home_sleeping");
}

// THE CORRUPTION GLITCH, WHICH HAD NEVER APPEARED IN A GOLDEN.
//
// ui/corrupt_fx.cpp shipped at P9-C5 with nineteen host tests on its geometry
// and its PAINTER in ui/petfx.cpp, a translation unit no binary here compiles -
// so "the glitch is contained" was asserted on five numbers and never on a
// picture. ui/screen_home.cpp paints the same rows on the still body path now.
//
// THE CLOCK IS THE FIXTURE. cfx_glitch_on() lights roughly one 60 ms slot in
// eight and is a pure hash of (slot, seed), so the test walks slots until one
// is lit rather than pinning a magic millisecond - which would silently stop
// meaning anything the day the hash changed.
TEST(snapshot_home_corrupted) {
  seams2_reset();
  fixture_starter();
  g_view.corrupted = 1u;
  const uint32_t seed = g_view.genome.lineage_id;
  int slots = 0;
  while (!cfx_glitch_on(g_now, seed) && slots < 200) { g_now += CFX_GLITCH_SLOT_MS; ++slots; }
  // A golden of an UNLIT slot would be a golden of home_starter with a corrupted
  // flag nobody drew - which is exactly the hole this snapshot exists to close.
  CHECK(cfx_glitch_on(g_now, seed) != 0u);
  CHECK(slots < 200);
  snapshot(SCR_HOME, "home_corrupted");
}

// ...and the pixels it moved are inside the body, which is the property
// tests/test_corruption.cpp asserts about cfx_rows()' five numbers and could
// not assert about a painter. Driven over every lit slot of a whole minute.
TEST(the_glitch_only_ever_moves_pixels_inside_the_body_it_is_drawn_over) {
  seams2_reset();
  fixture_starter();
  const uint32_t seed = g_view.genome.lineage_id;
  const uint32_t t0 = g_now;

  // THE BOUND IS PER FRAME AND THE FIRST DRAFT OF THIS TEST WAS NOT - it
  // measured frame 0's ink box and applied it to both, and the sweep failed on
  // slot 7 at row 39 because HOME's two-frame idle bob makes frame 1 a pixel
  // taller. The test was wrong and the painter was right; a union of the two
  // boxes would also have passed and would have been a looser statement than
  // the one worth making, so each frame is bounded by its own ink.
  static uint8_t clean[2][FB_H][FB_W];
  int bx0[2], by0[2], bx1[2], by1[2];
  for (int f = 0; f < 2; ++f) {
    g_now = t0 + (uint32_t)f * UI_ANIM_FRAME_MS;
    CHECK_EQ((int)((g_now / UI_ANIM_FRAME_MS) & 1u), f);
    g_view.corrupted = 0u;
    fb_reset();
    home_render();
    for (int y = 0; y < FB_H; ++y)
      for (int x = 0; x < FB_W; ++x) clean[f][y][x] = (uint8_t)fb_get(x, y);
    const SpriteRef r = sprite_lookup_pose(g_view.stage,
                          sprite_form_of(pet_art_key(g_view.species_id, 0u),
                                         (Stage)g_view.stage),
                          g_view.pose, (uint8_t)f);
    uint8_t t, b, l, rr;
    CHECK_EQ(pf_scan_ink(r.bits, r.w, r.h, &t, &b, &l, &rr), 1u);
    const int ox = (int)sprite_center_x(r.w);
    const int oy = (int)HOME_FLOOR_Y - (int)r.h;
    bx0[f] = ox + l; bx1[f] = ox + rr; by0[f] = oy + t; by1[f] = oy + b;
  }

  g_view.corrupted = 1u;
  int lit = 0, moved = 0;
  for (uint32_t k = 0; k < 1000u; ++k) {
    g_now = t0 + k * CFX_GLITCH_SLOT_MS;
    const int f = (int)((g_now / UI_ANIM_FRAME_MS) & 1u);
    const bool on = (cfx_glitch_on(g_now, seed) != 0u);
    fb_reset();
    home_render();
    CHECK_EQ(fb_oob(), 0u);
    int diff = 0;
    for (int y = 0; y < FB_H; ++y) {
      for (int x = 0; x < FB_W; ++x) {
        if ((uint8_t)fb_get(x, y) == clean[f][y][x]) continue;
        ++diff; ++moved;
        if (x < bx0[f] || x > bx1[f] || y < by0[f] || y > by1[f]) {
          fprintf(stderr, "  GLITCH slot %u frame %d: pixel (%d,%d) outside the "
                          "body ink box [%d..%d]x[%d..%d]\n", (unsigned)k, f, x, y,
                  bx0[f], bx1[f], by0[f], by1[f]);
          CHECK(false);
        }
      }
    }
    // An UNLIT slot must draw nothing at all: the glitch is a stutter, and one
    // that never stopped would be a coat pattern.
    if (on) ++lit; else CHECK_EQ(diff, 0);
  }
  // Roughly one slot in eight, and neither zero nor all of them: without this
  // "no pixel moved outside the body" would pass on a painter that drew nothing.
  CHECK(lit > 60);
  CHECK(lit < 250);
  CHECK(moved > 200);
  g_now = 100000u;
  g_view.corrupted = 0u;
}

TEST(snapshot_home_sick) {
  seams2_reset();
  fixture_starter();
  g_view.pose = POSE_SICK;
  snapshot(SCR_HOME, "home_sick");
}

// A POSE IS THE ONLY THING A POSE CHANGES. Both pose sets are generic - every
// species sleeps as the same body, which is the identity loss P9-C3 INHERITED
// rather than introduced - so the two frames above must differ from
// home_starter INSIDE the body box and nowhere else. Without this the pair
// could be two recordings of a screen whose pose plumbing had come unhooked.
TEST(a_pose_changes_the_body_and_nothing_else_on_home) {
  static uint8_t idle[FB_H][FB_W];
  const ScreenDef* home = screen_def(SCR_HOME);
  CHECK(home != nullptr);
  if (!home) return;

  seams2_reset();
  fixture_starter();
  fb_reset();
  home->render();
  for (int y = 0; y < FB_H; ++y)
    for (int x = 0; x < FB_W; ++x) idle[y][x] = (uint8_t)fb_get(x, y);

  static const uint8_t kPoses[] = { POSE_SLEEP, POSE_SICK };
  for (int k = 0; k < 2; ++k) {
    seams2_reset();
    fixture_starter();
    g_view.pose = kPoses[k];
    fb_reset();
    home->render();

    const SpriteRef r = sprite_lookup_pose(
        g_view.stage,
        sprite_form_of(pet_art_key(g_view.species_id, gene_species(g_view.genome)),
                       (Stage)g_view.stage),
        g_view.pose, 0u);
    CHECK_EQ(r.w, 24);
    CHECK_EQ(r.h, 24);
    const int bx = (int)sprite_center_x(r.w);
    const int by = (int)HOME_FLOOR_Y - (int)r.h;
    int changed = 0, outside = 0;
    for (int y = 0; y < FB_H; ++y)
      for (int x = 0; x < FB_W; ++x) {
        if ((uint8_t)fb_get(x, y) == idle[y][x]) continue;
        ++changed;
        if (x < bx || x >= bx + (int)r.w || y < by || y >= by + (int)r.h) ++outside;
      }
    CHECK(changed > 0);        // the pose reached the pixels
    CHECK_EQ(outside, 0);      // and reached nothing else
  }

  // EAT DOES NOT, AND THAT IS THE ANSWER P9-C3 GAVE RATHER THAN AN OVERSIGHT.
  // POSE_EAT has no art: it falls through to the species body, and the feeding
  // film carries the pose from ui/actfx.cpp, which no host binary compiles.
  // Pinned here so "we dropped it" cannot decay into "we forgot it".
  seams2_reset();
  fixture_starter();
  g_view.pose = POSE_EAT;
  fb_reset();
  home->render();
  int eat_diff = 0;
  for (int y = 0; y < FB_H; ++y)
    for (int x = 0; x < FB_W; ++x)
      if ((uint8_t)fb_get(x, y) != idle[y][x]) ++eat_diff;
  CHECK_EQ(eat_diff, 0);
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
//  AT ALL and that every differing pixel is inside the body box the LOOKUP
//  reports - i.e. that the species moved the creature and nothing else on the
//  screen.
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

  // THE BODY BOX IS DERIVED, NOT TYPED (P9-C3). It read
  // `sprite_center_x(40)` / `HOME_FLOOR_Y - 40`, the legacy adult body's box,
  // and the atlas is 24x24 now - so the box would have described a rectangle
  // the pet is no longer in, and the "nothing changed outside it" check would
  // have passed VACUOUSLY as long as the smaller body happened to sit inside
  // the larger rectangle. That is the shape of an assertion that stops being
  // about anything, so the box comes from the lookup the screen itself calls.
  const SpriteRef body = sprite_lookup_pose(
      g_view.stage,
      sprite_form_of(pet_art_key(g_view.species_id, gene_species(g_view.genome)),
                     (Stage)g_view.stage),
      g_view.pose, 0u);
  CHECK(body.bits != nullptr);
  CHECK_EQ(body.w, 24);
  CHECK_EQ(body.h, 24);
  const int bx = (int)sprite_center_x(body.w);
  const int by = (int)HOME_FLOOR_Y - (int)body.h;
  int changed = 0, changed_outside_the_body = 0;
  for (int y = 0; y < FB_H; ++y) {
    for (int x = 0; x < FB_W; ++x) {
      if ((uint8_t)fb_get(x, y) == starter[y][x]) continue;
      ++changed;
      if (x < bx || x >= bx + (int)body.w || y < by || y >= by + (int)body.h)
        ++changed_outside_the_body;
    }
  }
  CHECK(changed > 0);                        // the species reached the pixels
  CHECK_EQ(changed_outside_the_body, 0);     // and reached nothing else
  // AND THE BOX IS NOT VACUOUS. A rectangle that contains every changed pixel
  // proves nothing if it also contains the whole screen; this pins it to the
  // body's own size, which is 24x24 = 576 of the 8,192 pixels on the panel.
  CHECK(bx >= 0 && by >= 0);
  CHECK(bx + (int)body.w <= FB_W);
  CHECK(by + (int)body.h <= FB_H);
  CHECK(changed <= (int)body.w * (int)body.h);

  snapshot(SCR_HOME, "home_species");
}

// =============================================================================
//  THE SECTION 63 SWEEP - EVERY SPECIES, EVERY POSE, EVERY FRAME, THREE SCREENS
//  (P9-C3)
//
//  Section 63 asks for three things about sprites: "consistent pixel scale",
//  "predictable bounding boxes" and "no accidental clipping". Sixty bodies drawn
//  by five different hands is exactly the change that can break all three, and
//  the sixty-five recorded goldens contain FOUR SPECIES between them - Paketo on
//  three HOME frames, Rafagon on the fourth, and two per battle frame. Fifty-six
//  species are drawn by no golden at all, so a body that clipped, floated or
//  came out the wrong size would ship with the suite green.
//
//  WHAT "1x AND 2x" IN THE PLAN LINE MEANS, SAID PLAINLY RATHER THAN FUDGED.
//  P9-C3's plan line asks for "every species at 1x and 2x". THERE IS NO 2x BLIT
//  IN THIS REPOSITORY: ui/gfx.h has no scale parameter, u8g2's drawXBM has none,
//  and the host fake has none. That line was written when the atlas held bodies
//  at 24, 28, 32 and 40 px and "1x and 2x" meant the smallest and the largest -
//  a check that the layout survived a body nearly twice as wide as a baby's.
//  Every body is 24x24 now, so there is no second scale to render at, and
//  claiming to have rendered one would be the kind of tick this project keeps
//  being caught by. What IS checked, for all sixty, is the property list
//  section 63 actually names, plus the OOB requirement:
//
//    * CONSISTENT PIXEL SCALE - every body reports 24x24 through the same
//      lookup the screen calls, at every stage and every pose.
//    * PREDICTABLE BOUNDING BOX - the draw origin is derived, the box is inside
//      the panel, and the body's INK reaches its own last row so it stands on
//      HOME_FLOOR_Y instead of hovering above it.
//    * NO ACCIDENTAL CLIPPING - fb_oob() is 0 after every render.
//
//  AND THE SWEEP IS PROVED NOT TO BE VACUOUS by the case after it, which draws
//  a body where it WOULD clip and requires the recorder to count it.
// =============================================================================
static int sweep_species_on_home(uint8_t stage, uint8_t pose, uint8_t frame) {
  int drawn = 0;
  for (uint8_t id = 1; id <= (uint8_t)SPECIES_TABLE_COUNT; ++id) {
    seams2_reset();
    fixture_starter();
    g_view.species_id = id;
    g_view.stage      = stage;
    g_view.pose       = pose;
    g_now = 100000u + (uint32_t)frame * UI_ANIM_FRAME_MS;

    const ScreenDef* home = screen_def(SCR_HOME);
    CHECK(home != nullptr);
    if (!home) return drawn;
    fb_reset();
    home->render();
    if (fb_oob() != 0)
      fprintf(stderr, "  species %u stage %u pose %u frame %u: %u OOB, first %s\n",
              (unsigned)id, (unsigned)stage, (unsigned)pose, (unsigned)frame,
              (unsigned)fb_oob(), fb_oob_first());
    CHECK_EQ(fb_oob(), 0u);

    // The body the screen resolved, through the screen's own expression.
    const SpriteRef r = sprite_lookup_pose(
        stage,
        sprite_form_of(pet_art_key(id, gene_species(g_view.genome)), (Stage)stage),
        pose, frame);
    CHECK(r.bits != nullptr);
    CHECK_EQ(r.w, 24);                       // consistent pixel scale
    CHECK_EQ(r.h, 24);
    const int bx = (int)sprite_center_x(r.w);
    const int by = (int)HOME_FLOOR_Y - (int)r.h;
    CHECK(bx >= 0 && by >= 0);               // predictable bounding box
    CHECK(bx + (int)r.w <= FB_W);
    CHECK(by + (int)r.h <= FB_H);
    CHECK(by >= (int)SPRITE_AREA_Y);         // inside the sprite band

    // AND THE BODY IS ACTUALLY ON THE PANEL. A lookup that answered a valid
    // SpriteRef nobody drew would pass every line above; this reads the
    // framebuffer back and requires ink inside the box the box says it is in.
    int ink_in_box = 0, ink_on_floor_row = 0;
    for (int y = by; y < by + (int)r.h; ++y)
      for (int x = bx; x < bx + (int)r.w; ++x)
        if (fb_get(x, y)) {
          ++ink_in_box;
          if (y == by + (int)r.h - 1) ++ink_on_floor_row;
        }
    CHECK(ink_in_box > 0);
    // The body's own last row carries ink, so it STANDS on the floor rule
    // rather than hovering above it - the failure no byte check can see and
    // the reason tools/gen_sprites.py counts blank rows under a body.
    CHECK(ink_on_floor_row > 0);
    ++drawn;
  }
  return drawn;
}

// =============================================================================
//  THE CREATURE THE PLAYER DREW (P10-C4b)
//
//  REPORTED FROM A BOARD, AND IT WAS REAL: a Bug made in the creator showed
//  up in the BOX by name and then walked onto HOME wearing SOMEBODY ELSE'S
//  BODY. csp_install() had always parked CustomSpeciesRec.sprite - 144 bytes
//  the player drew a pixel at a time - in a record NOTHING EVER READ. A grep
//  for a reader across the whole tree found none, so what got drawn was
//  whichever atlas row the id happened to fold onto.
//
//  So these cases assert PIXELS, and they assert the NEGATIVE alongside them.
//  "It drew a body" would have passed before the fix, because it always drew
//  one: what has to be true is that the ink on the panel IS the ink in the
//  record and IS NOT the atlas body for the same key.
// =============================================================================
// THE MOVE SET IS FOUND, NOT TYPED. tests/test_validate.cpp holds four move ids
// as #defines and is welcome to - it is the file about the rules. A second copy
// of them here would be a second place the attack table can quietly outgrow, so
// the VALIDATOR is the oracle instead: the first four-move set it accepts is by
// definition legal, and if the table ever stops containing one this fails
// loudly rather than installing a record the registry would refuse.
static_assert(ER_MOVE_COUNT == 4, "the search below fills exactly four slots");
static bool cs_find_moves(CustomSpeciesRec& c)
{
  for (uint8_t a = 1u; a <= (uint8_t)ATTACK_COUNT; ++a)
    for (uint8_t b = (uint8_t)(a + 1u); b <= (uint8_t)ATTACK_COUNT; ++b)
      for (uint8_t d = (uint8_t)(b + 1u); d <= (uint8_t)ATTACK_COUNT; ++d)
        for (uint8_t e = (uint8_t)(d + 1u); e <= (uint8_t)ATTACK_COUNT; ++e) {
          c.moves[0] = a; c.moves[1] = b; c.moves[2] = d; c.moves[3] = e;
          uint16_t stat_used = 0, attack_used = 0;
          creator_cost_of(c, stat_used, attack_used);
          c.budget_used = attack_used;
          if (validate_custom_species(c) == (uint8_t)VR_OK) return true;
        }
  return false;
}

static void mk_custom(CustomSpeciesRec& c, uint8_t slot, uint8_t seed)
{
  memset(&c, 0, sizeof c);
  c.magic   = (uint16_t)CS_MAGIC;
  c.version = (uint8_t)SAVE_SCHEMA_VERSION;
  c.slot    = slot;
  c.type    = (uint8_t)TYPE_SIGNAL;
  c.base[0] = 6u; c.base[1] = 5u; c.base[2] = 5u; c.base[3] = 5u;   // 21
  memcpy(c.name, "Bicho", 6);
  c.compat_group = 0u;                       // a custom species does not breed
  CHECK(cs_find_moves(c));

  // A DRAWING NO ATLAS ROW COULD BE, and one whose two frames differ from each
  // other: the frame index is folded into every byte, so "the renderer drew
  // frame 0 twice" cannot pass as "the renderer drew the drawing".
  for (uint8_t f = 0; f < (uint8_t)CS_SPRITE_FRAMES; ++f)
    for (uint8_t i = 0; i < (uint8_t)CS_SPRITE_BYTES; ++i)
      c.sprite[f][i] = (uint8_t)(0x55u ^ (uint8_t)(i * 7u + f * 33u + seed));
}

// The 24x24 box a HOME body stands in, read straight off the framebuffer and
// packed back into XBM rows so it can be compared with the record byte for
// byte. XBM is little-endian per row: bit 0 of a byte is its LEFTMOST pixel.
static void read_body_box(const SpriteRef& r, uint8_t* out)
{
  const int bx = (int)sprite_center_x(r.w);
  const int by = (int)HOME_FLOOR_Y - (int)r.h;
  const int stride = (r.w + 7) / 8;
  memset(out, 0, (size_t)stride * r.h);
  for (int y = 0; y < (int)r.h; ++y)
    for (int x = 0; x < (int)r.w; ++x)
      if (fb_get(bx + x, by + y))
        out[y * stride + (x >> 3)] |= (uint8_t)(1u << (x & 7));
}

TEST(a_creature_from_the_creator_wears_the_body_it_was_drawn_with) {
  seams2_reset();
  fixture_starter();
  csp_reset();

  CustomSpeciesRec c;
  mk_custom(c, 0u, 0x11u);
  CHECK(csp_install(c));

  const uint8_t id = csp_species_id(0);
  CHECK(id != 0u);
  g_view.species_id = id;
  g_view.stage      = (uint8_t)STAGE_ADULT;
  g_view.pose       = (uint8_t)POSE_IDLE;

  // The atlas body this id folds onto: the WRONG creature, and the one the
  // device drew before this change. It has to still be there to be refused.
  const SpriteRef atlas = sprite_lookup_pose(
      g_view.stage,
      sprite_form_of(pet_art_key(id, gene_species(g_view.genome)),
                     (Stage)g_view.stage),
      g_view.pose, 0u);
  CHECK(atlas.bits != nullptr);

  const SpriteRef r = pet_body_ref(id, gene_species(g_view.genome),
                                   g_view.stage, g_view.pose, 0u);
  CHECK_EQ((int)r.w, (int)CS_SPRITE_W);
  CHECK_EQ((int)r.h, (int)CS_SPRITE_H);
  CHECK(r.bits != atlas.bits);          // it is not the atlas row any more
  CHECK_EQ(memcmp(r.bits, c.sprite[0], (size_t)CS_SPRITE_BYTES), 0);

  // AND ON THE PANEL, which is the only claim that is about the device: the
  // helper above is what screen_home.cpp calls, and this is what it painted.
  fb_reset();
  home_render();
  CHECK_EQ(fb_oob(), 0u);
  uint8_t seen[CS_SPRITE_BYTES];
  read_body_box(r, seen);
  CHECK_EQ(memcmp(seen, c.sprite[0], sizeof seen), 0);
  CHECK(memcmp(seen, atlas.bits, sizeof seen) != 0);

  csp_reset();
}

TEST(both_of_the_creators_frames_reach_home) {
  seams2_reset();
  fixture_starter();
  csp_reset();

  CustomSpeciesRec c;
  mk_custom(c, 0u, 0x2Au);
  CHECK(csp_install(c));
  g_view.species_id = csp_species_id(0);

  // The frame HOME draws is (ui_now_ms() / UI_ANIM_FRAME_MS) & 1, so the clock
  // is what selects it - exactly as it does for an atlas body.
  const uint32_t t0 = g_now;
  uint8_t seen[2][CS_SPRITE_BYTES];
  for (uint8_t f = 0; f < 2u; ++f) {
    g_now = t0 + (uint32_t)f * UI_ANIM_FRAME_MS;
    CHECK_EQ((int)((g_now / UI_ANIM_FRAME_MS) & 1u), (int)f);
    const SpriteRef r = pet_body_ref(g_view.species_id,
                                     gene_species(g_view.genome),
                                     g_view.stage, g_view.pose, f);
    fb_reset();
    home_render();
    read_body_box(r, seen[f]);
    CHECK_EQ(memcmp(seen[f], c.sprite[f], sizeof seen[f]), 0);
  }
  // The record's two frames differ, so the panel's two have to as well: a
  // renderer that ignored `frame` would pass every check above but this one.
  CHECK(memcmp(seen[0], seen[1], sizeof seen[0]) != 0);
  g_now = t0;
  csp_reset();
}

TEST(a_roster_species_still_wears_the_atlas_body) {
  // THE CONTROL. Sixty authored creatures must not have moved a pixel, and the
  // registry must not answer for an id it was never given: csp_sprite() is
  // keyed on the OCCUPIED MASK, so an empty slot inside the custom id range is
  // as much "not custom" as species 3 is.
  seams2_reset();
  fixture_starter();
  csp_reset();

  for (uint8_t id = 1u; id <= 8u; ++id) {
    const SpriteRef want = sprite_lookup_pose(
        (uint8_t)STAGE_ADULT,
        sprite_form_of(pet_art_key(id, gene_species(g_view.genome)),
                       STAGE_ADULT),
        (uint8_t)POSE_IDLE, 0u);
    const SpriteRef got = pet_body_ref(id, gene_species(g_view.genome),
                                       (uint8_t)STAGE_ADULT,
                                       (uint8_t)POSE_IDLE, 0u);
    CHECK(got.bits == want.bits);
  }
  for (uint8_t slot = 0; slot < (uint8_t)CREATOR_SPECIES_SLOTS; ++slot)
    CHECK(csp_sprite(csp_species_id(slot), 0u) == nullptr);
}

TEST(an_egg_and_a_sick_bug_are_never_the_players_drawing) {
  // ui/pet_art.h names three poses it will not override and gives a reason for
  // each. Two of them are decided HERE, and they are decided because a player
  // reads "sick" off a shared silhouette and an egg off a shell: replacing
  // either with a drawing takes a state the player needs and hides it.
  seams2_reset();
  fixture_starter();
  csp_reset();

  CustomSpeciesRec c;
  mk_custom(c, 0u, 0x71u);
  CHECK(csp_install(c));
  const uint8_t id = csp_species_id(0);
  const uint8_t gs = gene_species(g_view.genome);

  const SpriteRef egg  = pet_body_ref(id, gs, (uint8_t)STAGE_EGG,
                                      (uint8_t)POSE_IDLE, 0u);
  const SpriteRef sick = pet_body_ref(id, gs, (uint8_t)STAGE_ADULT,
                                      (uint8_t)POSE_SICK, 0u);
  CHECK(egg.bits  != nullptr);
  CHECK(sick.bits != nullptr);
  CHECK(egg.bits  != c.sprite[0]);
  CHECK(sick.bits != c.sprite[0]);
  CHECK_EQ(memcmp(egg.bits,  c.sprite[0], (size_t)CS_SPRITE_BYTES) != 0, true);
  CHECK_EQ(memcmp(sick.bits, c.sprite[0], (size_t)CS_SPRITE_BYTES) != 0, true);

  // But IDLE at the same stage is the drawing, so the two above are a rule and
  // not simply a renderer that never works.
  const SpriteRef idle = pet_body_ref(id, gs, (uint8_t)STAGE_ADULT,
                                      (uint8_t)POSE_IDLE, 0u);
  CHECK_EQ(memcmp(idle.bits, c.sprite[0], (size_t)CS_SPRITE_BYTES), 0);
  csp_reset();
}

TEST(two_drawn_bugs_do_not_share_one_derived_sleeper) {
  // THE CACHE KEY, and it is the bug this file caught while the fix was being
  // written. screen_home.cpp derives the sleeping body from the idle one and
  // caches it under (atlas set id, frame) - and TWO creator species fold onto
  // the SAME atlas set id, because neither has a row of its own. Without the
  // source frame in the key, the second custom Bug to fall asleep wears the
  // first one's face.
  seams2_reset();
  fixture_starter();
  csp_reset();

  CustomSpeciesRec a, b;
  mk_custom(a, 0u, 0x03u);
  mk_custom(b, 1u, 0xC0u);
  CHECK(csp_install(a));
  CHECK(csp_install(b));

  g_view.stage = (uint8_t)STAGE_ADULT;
  g_view.pose  = (uint8_t)POSE_SLEEP;

  uint8_t seen[2][CS_SPRITE_BYTES];
  for (uint8_t k = 0; k < 2u; ++k) {
    g_view.species_id = csp_species_id(k);
    const SpriteRef r = pet_body_ref(g_view.species_id,
                                     gene_species(g_view.genome),
                                     g_view.stage, (uint8_t)POSE_IDLE, 0u);
    CHECK_EQ(memcmp(r.bits, (k == 0u ? a.sprite[0] : b.sprite[0]),
                    (size_t)CS_SPRITE_BYTES), 0);
    fb_reset();
    home_render();
    CHECK_EQ(fb_oob(), 0u);
    read_body_box(r, seen[k]);
  }
  CHECK(memcmp(seen[0], seen[1], sizeof seen[0]) != 0);

  // And each sleeper really is DERIVED from its own drawing rather than being
  // it: pf_build_sleep() closes the eye band, so the frame on the panel differs
  // from the idle frame it came from.
  CHECK(memcmp(seen[0], a.sprite[0], sizeof seen[0]) != 0);
  CHECK(memcmp(seen[1], b.sprite[0], sizeof seen[1]) != 0);
  csp_reset();
}

TEST(every_species_draws_on_home_at_every_stage_and_pose_without_clipping) {
  int drawn = 0;
  static const uint8_t kStages[] = { STAGE_BABY, STAGE_CHILD, STAGE_TEEN,
                                     STAGE_ADULT, STAGE_SENIOR };
  for (uint8_t s = 0; s < (uint8_t)(sizeof kStages / sizeof kStages[0]); ++s)
    for (uint8_t pose = 0; pose < (uint8_t)POSE_COUNT; ++pose)
      for (uint8_t frame = 0; frame < 2u; ++frame)
        drawn += sweep_species_on_home(kStages[s], pose, frame);
  // 60 species x 5 stages x 4 poses x 2 frames.
  CHECK_EQ(drawn, (int)SPECIES_TABLE_COUNT * 5 * (int)POSE_COUNT * 2);
  CHECK_EQ(drawn, 2400);
  printf("  %d species/stage/pose/frame renders on HOME, 0 out of bounds\n", drawn);
}

// THE PROOF THAT THE OOB ARM ABOVE IS NOT VACUOUS. Every body is 24x24 and the
// panel is 128x64, so a sweep that never clips is exactly what a sweep looks
// like when the recorder is broken. This draws the same bodies at origins that
// MUST clip and requires the recorder to count every one of them.
TEST(the_out_of_bounds_recorder_would_see_a_body_that_clipped) {
  for (uint8_t id = 1; id <= (uint8_t)SPECIES_TABLE_COUNT; ++id) {
    const SpriteRef r = sprite_lookup_pose(
        (uint8_t)STAGE_ADULT, sprite_form_of((uint8_t)(id - 1u), STAGE_ADULT),
        (uint8_t)POSE_IDLE, 0u);
    CHECK(r.bits != nullptr);
    // Off each of the four edges by one pixel, and once fully off the bottom.
    static const int kdx[] = { -1, FB_W - (int)23, 0, 0, 0 };
    static const int kdy[] = { 0, 0, -1, FB_H - (int)23, FB_H };
    for (int k = 0; k < 5; ++k) {
      fb_reset();
      gfx_xbm((int16_t)kdx[k], (int16_t)kdy[k], r.w, r.h, r.bits);
      CHECK(fb_oob() > 0u);
    }
    // ...and once where it fits, so the recorder is not simply always on.
    fb_reset();
    gfx_xbm((int16_t)sprite_center_x(r.w),
            (int16_t)((int)HOME_FLOOR_Y - (int)r.h), r.w, r.h, r.bits);
    CHECK_EQ(fb_oob(), 0u);
  }
}

// THE BATTLE FIELD, all sixty, on BOTH sides and BOTH frames. The foe is drawn
// MIRRORED through ui/xbm_mirror.cpp into a scratch buffer sized by
// XBM_MIRROR_MAX_W, so this is also the only sweep that exercises the mirror on
// sixty different bitmaps.
TEST(every_species_draws_on_the_battle_field_without_clipping) {
  for (uint8_t id = 1; id <= (uint8_t)SPECIES_TABLE_COUNT; ++id) {
    const uint8_t key = pet_art_key(id, 0u);
    const uint8_t set = br_body_set_id(key);
    CHECK_EQ((int)set, (int)ER_SPRITE_BODY_FIRST + (int)(id - 1u));
    CHECK_EQ(sprite_set(set).w, (uint8_t)BR_BODY_W);
    CHECK_EQ(sprite_set(set).h, (uint8_t)BR_BODY_H);
    for (uint8_t frame = 0; frame < 2u; ++frame) {
      // The player: upright, at the player's slot.
      fb_reset();
      br_draw_body(BR_YOU_BODY_X, BR_YOU_BODY_Y, key, frame, false, false, false, false);
      if (fb_oob() != 0)
        fprintf(stderr, "  BATTLE you species %u frame %u: %u OOB, first %s\n",
                (unsigned)id, (unsigned)frame, (unsigned)fb_oob(), fb_oob_first());
      CHECK_EQ(fb_oob(), 0u);
      int ink_you = 0;
      for (int y = BR_YOU_BODY_Y; y < BR_YOU_BODY_Y + BR_BODY_H; ++y)
        for (int x = BR_YOU_BODY_X; x < BR_YOU_BODY_X + BR_BODY_W; ++x)
          ink_you += fb_get(x, y) ? 1 : 0;
      CHECK(ink_you > 0);

      // The foe: mirrored, at the foe's slot, and in the three states the field
      // can put a body in. k == 3 IS THE GUARD (P10-C3) AND IT IS THE ONE THAT
      // NEEDED THIS SWEEP: the fainted dissolve and the struck inversion are
      // both confined to the body's own 24x24 box, so no art can push them off
      // the panel, while the ward is the first thing this renderer has ever
      // drawn OUTSIDE that box - three columns and a pair of bracket arms
      // hanging off whichever side the creature faces. Both facings are driven
      // below; the foe's barrier is the one that reaches toward x = 0.
      for (int k = 0; k < 4; ++k) {
        for (int face = 0; face < 2; ++face) {
          const int16_t bx = face ? (int16_t)BR_FOE_BODY_X : (int16_t)BR_YOU_BODY_X;
          const int16_t by = face ? (int16_t)BR_FOE_BODY_Y : (int16_t)BR_YOU_BODY_Y;
          fb_reset();
          br_draw_body(bx, by, key, frame, face != 0, k == 1, k == 2, k == 3);
          if (fb_oob() != 0)
            fprintf(stderr, "  BATTLE species %u frame %u k %d face %d: %u OOB, first %s\n",
                    (unsigned)id, (unsigned)frame, k, face,
                    (unsigned)fb_oob(), fb_oob_first());
          CHECK_EQ(fb_oob(), 0u);
        }
      }

      // MIRRORED IS A DIFFERENT PICTURE. A mirror that quietly became a copy
      // would leave both fighters facing the same way and nothing else here
      // would notice - the OOB count is the same and the ink count is the same.
      static uint8_t upright[FB_H][FB_W];
      fb_reset();
      br_draw_body(BR_YOU_BODY_X, BR_YOU_BODY_Y, key, frame, false, false, false, false);
      for (int y = 0; y < FB_H; ++y)
        for (int x = 0; x < FB_W; ++x) upright[y][x] = (uint8_t)fb_get(x, y);
      fb_reset();
      br_draw_body(BR_YOU_BODY_X, BR_YOU_BODY_Y, key, frame, true, false, false, false);
      int mirror_diff = 0;
      for (int y = 0; y < FB_H; ++y)
        for (int x = 0; x < FB_W; ++x)
          if ((uint8_t)fb_get(x, y) != upright[y][x]) ++mirror_diff;
      // Only a body that is exactly symmetric about its own centre column can
      // mirror to itself. None of the sixty is, and if one ever is, it should
      // be named here rather than allowed to pass silently.
      if (mirror_diff == 0)
        nt_fail_at(__FILE__, __LINE__, ER_SPRITE_NAMES[set]);
    }
  }
}

TEST(snapshot_home_empty) {
  seams2_reset();
  fixture_none();
  snapshot(SCR_HOME, "home_no_bug");
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
  menu_to(MENU_BUG);
  snapshot(SCR_MENU, "menu_bug");
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

// P10-C6: THE CORRUPTION READOUT. Spec section 55's 24 h state had no readout
// anywhere in the product - one line at onset, an intermittent shimmer, and no
// page a player could go to and ask. cor_left_s() had no caller in src/ at all.
TEST(snapshot_status_a_corrupted) {
  seams2_reset();
  g_view.corrupted = 1u;
  g_view.corrupt_h = 18u;
  status_a_enter();
  snapshot(SCR_STATUS, "status_a_corrupted");
}

TEST(the_status_page_says_a_bug_is_corrupted_and_for_how_much_longer) {
  // The page is DIFFERENT when the status is set - which is the whole finding:
  // rendering STATUS_A with and without the bit gave a diff of exactly zero.
  seams2_reset();
  g_view.corrupted = 0u;
  g_view.corrupt_h = 0u;
  status_a_enter();
  fb_reset();
  status_a_render();
  static uint8_t clean[OLED_W * OLED_H];
  for (int y = 0; y < OLED_H; ++y)
    for (int x = 0; x < OLED_W; ++x) clean[y * OLED_W + x] = (uint8_t)fb_get(x, y);

  g_view.corrupted = 1u;
  g_view.corrupt_h = 18u;
  fb_reset();
  status_a_render();
  int diff = 0;
  for (int y = 0; y < OLED_H; ++y)
    for (int x = 0; x < OLED_W; ++x)
      if (clean[y * OLED_W + x] != (uint8_t)fb_get(x, y)) ++diff;
  CHECK(diff > 0);
  CHECK_EQ(fb_oob(), 0u);
  CHECK_EQ(fb_bad_utf8(), 0u);
  CHECK_EQ(fb_no_glyph(), 0u);

  // ...and the HOURS are on the page, not just the word: a readout that could
  // not count down would say no more than the shimmer already does. Every hour
  // from 1 to 24 draws something, and two different hours draw differently.
  fb_reset();
  g_view.corrupt_h = 1u;
  status_a_render();
  static uint8_t one_h[OLED_W * OLED_H];
  for (int y = 0; y < OLED_H; ++y)
    for (int x = 0; x < OLED_W; ++x) one_h[y * OLED_W + x] = (uint8_t)fb_get(x, y);
  fb_reset();
  g_view.corrupt_h = 24u;
  status_a_render();
  int hdiff = 0;
  for (int y = 0; y < OLED_H; ++y)
    for (int x = 0; x < OLED_W; ++x)
      if (one_h[y * OLED_W + x] != (uint8_t)fb_get(x, y)) ++hdiff;
  CHECK(hdiff > 0);
  for (uint8_t h = 1u; h <= 24u; ++h) {
    fb_reset();
    g_view.corrupt_h = h;
    status_a_render();
    CHECK_EQ(fb_oob(), 0u);
    CHECK_EQ(fb_no_glyph(), 0u);
  }
  g_view.corrupted = 0u;
  g_view.corrupt_h = 0u;
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
  settings_input(GST_TAP_R);
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
  menu_to(MENU_BUG);

  struct { uint8_t item; int pushes; uint8_t to; } kWant[] = {
    { MENU_BUG,   1, SCR_STATUS },
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
    menu_input(GST_TAP_R);                    // section 7: B held chooses
    CHECK_EQ(g_push, kWant[i].to);
    CHECK(kWant[i].pushes == 1);
    menu_input(GST_TAP_L);                     // on to the next item
  }
  CHECK_EQ(menu_cursor(), (uint8_t)MENU_BUG);   // invariant 4: it is a ring
}

TEST(menu_help_and_the_ring_wraps_back_to_the_first_item) {
  seams2_reset();
  menu_to(MENU_BUG);
  menu_input(GST_BOTH);
  CHECK_EQ(g_help, STR_HLP_STATUS);

  // P3-C4a took the double tap away, and with it this screen's two shortcuts:
  // DBL_L jumped to the first item and DBL_R repeated the last care action.
  // The jump is replaced by the ring itself - stepping A all the way round
  // comes back to where it started - and repeat-last-action was deleted with
  // its only caller (it is in no spec section and its "nothing to repeat"
  // path toasted "bad argument").
  for (uint8_t i = 0; i < (uint8_t)MENU_ITEM_COUNT; ++i) menu_input(GST_TAP_L);
  CHECK_EQ(menu_cursor(), (uint8_t)MENU_BUG);
}

// An accepted care action leaves the player on HOME watching the film; a
// REJECTED one has no film, so the list gives the screen back instead.
TEST(care_actions_and_the_rejected_path) {
  seams2_reset();
  care_enter();

  care_input(GST_TAP_R);                        // meal
  CHECK_EQ(g_shown, (uint8_t)ACT_FEED_MEAL);
  CHECK_EQ(g_backs, 0);

  g_action_ok = false;
  care_input(GST_TAP_R);
  CHECK_EQ(g_backs, 1);                         // refused: back to where we were
  g_action_ok = true;

  care_input(GST_TAP_L);                        // snack
  care_input(GST_TAP_R);
  CHECK_EQ(g_shown, (uint8_t)ACT_FEED_SNACK);

  care_input(GST_TAP_L);                        // clean
  care_input(GST_TAP_R);
  CHECK_EQ(g_shown, (uint8_t)ACT_CLEAN);

  care_input(GST_TAP_L);                        // medicine: always a confirmation
  care_input(GST_TAP_R);
  CHECK_EQ(g_medicine, 1);

  care_input(GST_TAP_L);                        // the bag: a MODE, not an action
  care_input(GST_TAP_R);
  CHECK_EQ(care_mode(), (uint8_t)CAREM_BAG);
  care_input(GST_HOLD_R);                        // ...and B closes it, not the screen
  CHECK_EQ(care_mode(), (uint8_t)CAREM_LIST);
  CHECK_EQ(g_backs, 1);                         // still the one from the refusal

  care_input(GST_TAP_L);                        // volver
  g_backs = 0;
  care_input(GST_TAP_R);
  CHECK_EQ(g_backs, 1);
}

TEST(play_list_starts_a_game_or_leaves) {
  seams2_reset();
  play_enter();
  play_input(GST_TAP_R);
  CHECK_EQ(g_minigame, (uint8_t)0);
  play_input(GST_TAP_L);
  play_input(GST_TAP_R);
  CHECK_EQ(g_minigame, (uint8_t)1);
  // The "jump to the last row" shortcut went with the double tap; the list
  // wraps, so one step back from the first row is the last one.
  while (play_cursor() != (uint8_t)(PLAY_ROWS - 1)) play_input(GST_TAP_L);
  CHECK_EQ(play_cursor(), (uint8_t)(PLAY_ROWS - 1));
  g_backs = 0;
  play_input(GST_TAP_R);
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

  settings_input(GST_TAP_R);                    // SET_SOUND
  CHECK_EQ((uint8_t)(g_cfg.flags & CF_MUTE), (uint8_t)CF_MUTE);
  CHECK_EQ(g_cfg_saves, 1);

  settings_input(GST_TAP_L);                    // SET_WEB
  settings_input(GST_TAP_R);
  CHECK_EQ((uint8_t)(g_cfg.flags & CF_WEB_ENABLED), (uint8_t)CF_WEB_ENABLED);

  settings_input(GST_TAP_L);                    // SET_BRIGHT: a five-step ring
  const uint8_t b0 = g_cfg.brightness;
  settings_input(GST_TAP_R);
  CHECK(g_cfg.brightness != b0);
  CHECK_EQ(g_bright, g_cfg.brightness);         // and it went through the arbiter

  // The "Acerca de" page is read-only and any gesture gives the list back.
  settings_enter();
  for (uint8_t i = 0; i < SET_INFO; i++) settings_input(GST_TAP_L);
  settings_input(GST_TAP_R);
  CHECK_EQ(settings_page(), 1);
  settings_close_page();
  CHECK_EQ(settings_page(), 0);

  // With no Config bound nothing may be written and the user is told.
  settings_enter();
  g_cfg_p = nullptr;
  g_cfg_saves = 0;
  settings_input(GST_TAP_R);
  CHECK_EQ(g_toast, STR_ERR_BUSY);
  CHECK_EQ(g_cfg_saves, 0);
  g_cfg_p = &g_cfg;

  // Section 7: B TAPPED leaves the screen, and it reaches the hook because the
  // row carries SF_OWNS_BACK (the info page is one level below the stack).
  settings_enter();
  g_backs = 0;
  settings_input(GST_HOLD_R);
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
  time_input(GST_HOLD_R);                        // February
  CHECK_EQ(time_field(CLK_MONTH), (uint16_t)2);
  time_input(GST_TAP_L);                        // -> day
  for (uint8_t i = 0; i < 28; i++) time_input(GST_HOLD_R);
  CHECK_EQ(time_field(CLK_DAY), (uint16_t)29);
  time_input(GST_HOLD_R);
  CHECK_EQ(time_field(CLK_DAY), (uint16_t)1);   // wrapped, never a 30 February

  // 31 January -> February must not leave an impossible day on screen.
  time_enter();
  time_input(GST_TAP_L);
  time_input(GST_TAP_L);                        // -> day
  for (uint8_t i = 0; i < 30; i++) time_input(GST_HOLD_R);
  CHECK_EQ(time_field(CLK_DAY), (uint16_t)31);
  // DAY -> HOUR -> MIN -> YEAR -> MONTH: the field cursor is a ring too.
  for (uint8_t i = 0; i < 4; i++) time_input(GST_TAP_L);
  CHECK_EQ(time_cursor(), (uint8_t)CLK_MONTH);
  time_input(GST_HOLD_R);
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
  menu_to(MENU_BUG);

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
// =============================================================================
//  P7-C2: THE LINK SCREEN'S FOUR FRAMES
//
//  All four are the REAL screen over the REAL discovery job and the REAL codec,
//  with only the radio faked (tests/fakes/link_fake.h). link_phase7.pbm is gone
//  with the placeholder that drew it - it printed "Enlace - Fase 7", an
//  internal plan phase number, at a player.
// =============================================================================
static void link_reset_screen(void) {
  seams2_reset();
  lf_reset();
  lf_set_pet_name("BOLOTA");
  g_now = 1000;
  link_enter();
}

// Three beacons at LINK_BEACON_MS apart is what crosses LINK_PEER_HITS_MIN, so
// this is the shortest honest way to make a peer real (networking/discovery.h).
static void link_make_peer(uint32_t id, const char* name, uint16_t caps,
                           int8_t rssi, uint8_t slot) {
  for (uint8_t i = 0; i < (uint8_t)LINK_PEER_HITS_MIN; ++i) {
    lf_push_beacon(id, name, caps, rssi, slot);
    g_now += 600;
    link_update(g_now);
  }
}

TEST(snapshot_link_searching) {
  link_reset_screen();
  link_update(g_now);
  CHECK_EQ(link_screen_peers(), (uint8_t)0);
  snapshot(SCR_LINK, "link_searching");
}

TEST(snapshot_link_peers) {
  link_reset_screen();
  link_make_peer(0x2001u, "PIEDRIN", (uint16_t)DISC_CAP_BATTLE, -42, 0);
  link_make_peer(0x2002u, "CANTO", (uint16_t)DISC_CAP_BATTLE, -66, 1);
  CHECK_EQ(link_screen_peers(), (uint8_t)2);
  snapshot(SCR_LINK, "link_peers");
}

TEST(snapshot_link_card) {
  link_reset_screen();
  link_make_peer(0x2001u, "PIEDRIN", (uint16_t)DISC_CAP_BATTLE, -42, 0);
  link_input(GST_TAP_R);                 // open the card on the first peer
  CHECK_EQ(link_screen_mode(), (uint8_t)LKM_CARD);
  snapshot(SCR_LINK, "link_card");
}

TEST(snapshot_link_lost) {
  link_reset_screen();
  // The browse's own section 47 ceiling, which is the failure a player can
  // reach without a second device in the room at all.
  g_now += (uint32_t)LINK_JOB_TIMEOUT_MS + 1u;
  link_update(g_now);
  CHECK_EQ(link_screen_mode(), (uint8_t)LKM_LOST);
  snapshot(SCR_LINK, "link_lost");
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

// =============================================================================
//  SPEC SECTION 39: WHAT THE SYMBOL ACTUALLY ENCODES
//
//  A QR is photographed, forwarded and posted. The PIN is the authorisation
//  layer and must not be in it - and until P8-C5 nothing in this tree could see
//  whether it was. tools/check.sh greps src/networking for a formatted query
//  parameter, which is where net_url()'s old "?k=%04u" lived; ui/screen_creator.cpp
//  is not in that directory and is not in that grep, so a PIN appended in
//  build() - the ONE function that decides what is encoded - would have shipped
//  green. creator_payload() is the seam these three cases read through.
// =============================================================================

// The two payloads, named. Anything else on screen is a symbol pointing
// somewhere nobody chose.
TEST(creator_encodes_the_join_string_and_the_url_and_nothing_else) {
  seams2_reset();
  g_ap_up = 1;
  creator_enter();
  CHECK_EQ(creator_variant(), (uint8_t)1);
  CHECK_STR_EQ(creator_payload(), "WIFI:S:ERRATA-1234;;");

  creator_input(GST_TAP_L);                       // flip to the URL
  CHECK_EQ(creator_variant(), (uint8_t)0);
  CHECK_STR_EQ(creator_payload(), "http://192.168.4.1/");
}

// THE PAYLOAD IS NOT A FUNCTION OF THE PIN, at any PIN the device can mint.
//
// A substring search would be the obvious test and it is the WRONG one: the
// SSID is AP_SSID_PREFIX plus four hex characters of the device id, so
// "ERRATA-1234" is a perfectly ordinary real SSID and a search for the
// digits "1234" inside it reports a leak that is not there. What is actually
// being claimed is stronger and has no false positive: the bytes encoded at
// every PIN are the SAME bytes, so no addition anywhere in build() can be
// carrying one.
TEST(creator_payload_carries_no_pin_for_any_pin) {
  seams2_reset();
  g_ap_up = 1;

  g_pin = 1u;
  creator_enter();
  char join[CREATOR_TEXT_MAX];
  char url[CREATOR_TEXT_MAX];
  snprintf(join, sizeof join, "%s", creator_payload());
  creator_input(GST_TAP_L);
  snprintf(url, sizeof url, "%s", creator_payload());
  CHECK(join[0] != '\0');
  CHECK(url[0]  != '\0');
  CHECK(strcmp(join, url) != 0);                  // the two really are different

  // Every PIN cg_mint_pin() can produce: 1..WEB_PIN_MAX-1, never the 0
  // sentinel. Both symbols at each one.
  int bad = 0;
  for (uint32_t pin = 1u; pin < (uint32_t)WEB_PIN_MAX; ++pin) {
    g_pin = (uint16_t)pin;
    creator_enter();                              // re-encodes from scratch
    if (strcmp(creator_payload(), join) != 0) { bad++; break; }
    creator_input(GST_TAP_L);
    if (strcmp(creator_payload(), url) != 0) { bad++; break; }
  }
  if (bad) fprintf(stderr, "    payload moved at pin %u: \"%s\"\n",
                   (unsigned)g_pin, creator_payload());
  CHECK_EQ(bad, 0);
}

// A QUERY PARAMETER OF ANY NAME, not just "?k=". The gate in tools/check.sh
// makes this claim about net.cpp with a grep; this makes it about the bytes
// that reach qr_encode(), which is the only place it is finally true.
TEST(creator_payload_never_carries_a_query_parameter) {
  seams2_reset();
  g_ap_up = 1;
  g_pin = 4242u;
  creator_enter();
  for (int i = 0; i < 2; ++i) {
    const char* p = creator_payload();
    CHECK(strchr(p, '?') == nullptr);
    CHECK(strchr(p, '=') == nullptr);
    CHECK(strchr(p, '&') == nullptr);
    creator_input(GST_TAP_L);
  }
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

// -----------------------------------------------------------------------------
//  THE TWO EXITS AND THE STICKY FLAG (P8-C2)
// -----------------------------------------------------------------------------

// CREATOR MUST BE STICKY, AND THIS IS THE BUG THAT MADE IT SO. With the default
// flags, invariant 3's 20 s auto-return applied here: enter CREATOR, put the
// device down, pick up a phone - and twenty seconds later the firmware went
// HOME and tore the access point down, which is less time than joining a
// network takes. The 20 s clock counts DEVICE gestures and this is the one
// screen whose whole purpose is that the user is not making any.
TEST(creator_is_sticky_so_the_portal_outlives_the_navigation_timeout) {
  CHECK((SCREENS[SCR_CREATOR].flags & SF_STICKY) != 0);
}

// Exit 1, spec section 47: every radio wait has an exit. The access point can
// fail to start outright (NERR_AP_FAILED) or be refused because a scan holds
// the radio, and with the auto-return gone there would otherwise be no route
// off this screen but a button - with the radio drawing current the whole time.
TEST(creator_gives_up_when_the_access_point_never_comes_up) {
  seams2_reset();
  g_ap_up = 0;
  g_now = 100000u;
  creator_enter();
  CHECK_EQ(g_backs, 0);

  // One second short of the budget: still waiting.
  g_now = 100000u + CREATOR_AP_WAIT_MS - 1000u;
  creator_update(g_now);
  CHECK_EQ(g_backs, 0);

  g_now = 100000u + CREATOR_AP_WAIT_MS;
  creator_update(g_now);
  CHECK_EQ(g_backs, 1);
}

// ...and it does NOT give up while the access point is up, however long the
// user leaves it there, because that is the case the idle timer owns.
// =============================================================================
//  P10-C6: THE PORTAL AND THE POWER LADDER
//
//  creator_waits_indefinitely_once_the_access_point_is_up, immediately below,
//  was a TRUE statement about creator_update() and a FALSE statement about the
//  device. The screen does not give up - and hardware/power.cpp took the radio
//  away from it anyway.
//
//  app/app.cpp feeds PowerInput.held from ui/ui.cpp's ui_radio_job_busy(),
//  which was network_screen_busy() || link_screen_busy(). The portal is the
//  THIRD radio owner in this tree and was in neither. `held` clamps the ladder
//  at DIM (hardware/power.cpp:63); without it the ladder reached PWR_IDLE at
//  120,000 ms, called hook_release(), and app.cpp's pwr_hook_release() ran
//  ui_home() - whose sm_goto() runs creator_leave(), the one teardown. The
//  portal died at two minutes while CREATOR_IDLE_S_DEFAULT is 300 s, and
//  drawing a sprite on a phone takes longer than two minutes. Neither the
//  mobile editor nor the sprite editor could be used, and docs/bench.md D2 -
//  which polls for IDLE_S + 60 seconds without pressing a button - could not
//  pass as written.
//
//  This is the only host binary that compiles the ladder AND the screen.
//  g_idle here is the ladder's OWN input, not a UI countdown: no button is
//  pressed for the whole run, which is exactly the bench operator's situation.
// =============================================================================
// THE SPLIT, SAID OUT LOUD. ui_radio_job_busy() lives in ui/ui.cpp, which no
// host binary compiles, so this case drives creator_screen_busy() - the half
// with a right answer - straight into the real ladder, and tools/check.sh
// holds the other half: that ui_radio_job_busy() names all THREE owners. A
// test here that re-wrote the disjunction would be asserting a copy of the one
// line that decides, which is the defect this whole phase keeps finding.
static int  s_pwr_releases = 0;
static void pwr_t_dim(bool)     {}
static void pwr_t_panel(bool)   {}
static void pwr_t_release(void) { ++s_pwr_releases; }
static void pwr_t_persist(void) {}

TEST(the_portal_holds_the_power_ladder_off_the_rung_that_would_close_it) {
  static const PowerHooks kH = { pwr_t_dim, pwr_t_panel, pwr_t_release, pwr_t_persist };
  pwr_bind(&kH);
  seams2_reset();
  g_ap_up = 1;
  g_now = 100000u;
  creator_enter();
  CHECK(creator_screen_busy());                 // the screen owns the radio

  pwr_begin();
  s_pwr_releases = 0;
  // Ten minutes of a player looking at a phone: no gesture, so idle_ms only
  // grows. PWR_IDLE_MS is 120 s and PWR_SLEEP_MS is 600 s; both are passed.
  for (uint32_t t = 1000u; t <= 600000u; t += 1000u) {
    PowerInput in;
    in.idle_ms = t;
    in.held    = creator_screen_busy() ? 1u : 0u;
    (void)pwr_service(in);
  }
  CHECK_EQ(s_pwr_releases, 0);                  // the AP is still up
  CHECK_EQ((int)pwr_state(), (int)PWR_DIM);     // clamped exactly one rung down

  // ANTI-VACUITY, AND IT IS THE HALF THAT MATTERS: the same ten minutes with
  // the screen left says the ladder really does fire. A test that only proved
  // "no release happened" would pass with the ladder switched off entirely.
  creator_leave();
  CHECK(!creator_screen_busy());
  pwr_begin();
  s_pwr_releases = 0;
  for (uint32_t t = 1000u; t <= 600000u; t += 1000u) {
    PowerInput in;
    in.idle_ms = t;
    in.held    = creator_screen_busy() ? 1u : 0u;
    (void)pwr_service(in);
  }
  CHECK(s_pwr_releases > 0);
}

// The ladder's rung and the portal's own ceiling, side by side, so the
// relationship that made this a defect is written down as arithmetic rather
// than as a comment. networking/discovery.h:254 states the LINK half the other
// way round - LINK_JOB_TIMEOUT_MS is BELOW the rung, so that job never needs to
// hold. The portal's ceiling is a user setting of up to an hour and can never
// fit under 120 s, so holding is the only answer available to it.
TEST(the_portals_own_timeout_is_the_ceiling_and_it_is_above_the_idle_rung) {
  CHECK((uint32_t)CREATOR_IDLE_S_DEFAULT * 1000u > (uint32_t)PWR_IDLE_MS);
  CHECK((uint32_t)CREATOR_IDLE_S_MAX * 1000u > (uint32_t)PWR_IDLE_MS);
  // ...and the OTHER exit, the one for an access point that never came up,
  // must fire BEFORE the rung, because that wait is not held by anything the
  // player can see and spec 47 wants it bounded.
  CHECK((uint32_t)CREATOR_AP_WAIT_MS < (uint32_t)PWR_IDLE_MS);
}

TEST(creator_waits_indefinitely_once_the_access_point_is_up) {
  seams2_reset();
  g_ap_up = 1;
  g_now = 100000u;
  creator_enter();
  for (uint32_t t = 1000u; t <= 10u * CREATOR_AP_WAIT_MS; t += 1000u) {
    g_now = 100000u + t;
    creator_update(g_now);
  }
  CHECK_EQ(g_backs, 0);
}

// Exit 2, spec sections 34 and 40 and decision D7: the portal went its whole
// grace period without an authorised request. The screen leaves through
// ui_back() rather than dropping the radio where it stands, so the leave hook -
// the ONE teardown - is what actually runs.
TEST(creator_leaves_when_the_portal_idles_out) {
  seams2_reset();
  g_ap_up = 1;
  g_now = 100000u;
  creator_enter();
  g_now += 5000u;
  creator_update(g_now);
  CHECK_EQ(g_backs, 0);

  g_idle_exp = 1;
  g_now += 5000u;
  creator_update(g_now);
  CHECK_EQ(g_backs, 1);
}

// And the teardown that follows is the same one a B press runs: ui_back() ->
// sm_back() -> sm_goto() -> creator_leave() -> ui_creator_radio(false). This
// binary stubs ui_back(), so what it can prove is that the leave hook releases
// the radio however it is reached; tests/test_statemachine.cpp drives the real
// ui_back() through the real table.
TEST(creator_leave_releases_the_radio_however_it_is_reached) {
  seams2_reset();
  g_ap_up = 1;
  creator_enter();
  CHECK_EQ(g_radio, 1);
  g_idle_exp = 1;
  g_now += 2000u;
  creator_update(g_now);
  CHECK_EQ(g_backs, 1);
  creator_leave();                    // what sm_goto() calls next
  CHECK_EQ(g_radio, 0);
}

// The PIN is printed on the device and is NOT in the QR payload (spec 39).
// What this binary can see is the payload the screen chooses; the format string
// itself lives in net.cpp, which no host binary compiles, and tools/check.sh
// gates that half.
// THIS CASE WAS CALLED creator_payload_carries_no_pin AND DID NOT READ THE
// PAYLOAD. It called ui_creator_info() and asserted three things about
// `in.url` - no "k=", no "1234", equal to "http://192.168.4.1/" - which are
// three things the FIXTURE thirty lines above types into that field. It could
// not fail for the reason its name gave: a PIN appended inside
// screen_creator.cpp's build(), between reading in.url and encoding the symbol,
// left every one of those checks green. That is this project's recurring defect
// with a spec-39 label on it, and the three cases above are the replacement -
// they read creator_payload(), which is the string qr_encode() was actually
// handed.
//
// WHAT SURVIVES IS THE HALF THAT WAS NEVER ABOUT THE FIXTURE: the join symbol
// must fit QR version 2's 32 B byte budget, because 33 B forces version 3 -> 29
// modules -> 70 px on a 64-row panel. ui/screen_creator.cpp argues at length why
// no WPA passphrase can ever be added to it; this is the arithmetic that would
// catch a longer SSID prefix trying.
TEST(creator_join_string_fits_the_version_2_byte_budget) {
  seams2_reset();
  g_ap_up = 1;
  creator_enter();
  CHECK_EQ(creator_variant(), (uint8_t)1);
  CHECK(strlen(creator_payload()) <= 32u);      // the encoded bytes, not a copy
  CHECK(strncmp(creator_payload(), "WIFI:S:", 7) == 0);
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
  dialog_input(GST_TAP_R);                    // choosing NO closes it
  CHECK_EQ(dialog_modal(), (uint8_t)MODAL_NONE);
  CHECK_EQ(g_commits, 0);

  dialog_open_confirm(CFM_WIPE2, STR_CF_WIPE2);
  dialog_input(GST_TAP_L);                     // onto YES
  dialog_input(GST_TAP_R);
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
    // data/species_table.h, and box_new_bug() correctly refuses an id that
    // has no row.
    const uint8_t slot = box_new_bug(1u, (uint8_t)(1u + i * 3u),
                                        ORIGIN_STARTER, gen, 0xC0FFEEu + i, 1000u);
    CHECK(slot != BOX_SLOT_NONE);
    BugInstance* p = box_slot(slot);
    if (!p) continue;
    for (uint8_t c = 0; c < ER_CARE_COUNT; ++c)
      p->care[c] = (int32_t)(ER_CARE_MILLI_MAX - (int32_t)c * 12000);
    p->hp_cur = (uint16_t)(15u + i);
    // The second row carries the longest nickname the schema allows: the list
    // has a number, a marker, a name and a right-aligned level to fit in 128 px.
    if (i == 1u) snprintf(p->nickname, sizeof p->nickname, "ABCDEFGHIJKL");
  }
  if (occupied) CHECK(box_set_active(0));
  box_enter();
}

// THE BOX SCREEN, all sixty. screen_box.cpp draws NAMES and badges, not bodies
// (the badge is indexed by the genome nibble and is 13 rows wide whatever the
// roster does), so what this sweep is really about is the LIST: a 60-species
// roster means names the list has never been asked to draw, and section 63's
// "text must not overflow" is the half of it that can move here.
TEST(every_species_draws_in_the_box_list_without_clipping) {
  for (uint8_t id = 1; id <= (uint8_t)SPECIES_TABLE_COUNT; ++id) {
    seams2_reset();
    memset(&g_gs, 0, sizeof g_gs);
    box_bind(g_gs);
    Genome gen;
    memset(&gen, 0, sizeof gen);
    gen.magic_ver  = GENOME_MAGIC_VER;
    gen.lineage_id = 0x0BADF00Du;
    gen.g0 = 0x1234u; gen.g1 = 0x5678u; gen.g2 = 0x9ABCu;
    gen.generation = 3;
    const uint8_t slot = box_new_bug(id, 30u, ORIGIN_STARTER, gen,
                                        0xC0FFEEu + id, 1000u);
    CHECK(slot != BOX_SLOT_NONE);
    if (slot == BOX_SLOT_NONE) continue;
    CHECK(box_set_active(slot));
    box_enter();

    const ScreenDef* d = screen_def(SCR_BOX);
    CHECK(d != nullptr);
    if (!d) return;
    fb_reset();
    d->render();
    if (fb_oob() != 0)
      fprintf(stderr, "  BOX species %u: %u OOB, first %s\n",
              (unsigned)id, (unsigned)fb_oob(), fb_oob_first());
    CHECK_EQ(fb_oob(), 0u);
    // The row is not blank: a list that drew nothing would also not clip.
    int ink = 0;
    for (int y = 0; y < FB_H; ++y)
      for (int x = 0; x < FB_W; ++x) ink += fb_get(x, y) ? 1 : 0;
    CHECK(ink > 0);
    // And the roster's own name for it really is what the screen can reach.
    CHECK(pet_species_name(id) != nullptr);
  }
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
  box_input(GST_TAP_R);                // choose it
  CHECK_EQ(box_screen_mode(), (uint8_t)BOXM_ACTIONS);
  CHECK_EQ(box_screen_slot(), (uint8_t)1);
  snapshot(SCR_BOX, "box_actions");
}

TEST(snapshot_box_card) {
  seams2_reset();
  box_fixture(3);
  box_input(GST_TAP_L);
  box_input(GST_TAP_R);                // the action list for slot 2
  box_input(GST_TAP_R);                // BOXA_VIEW: a stored Bug's card
  CHECK_EQ(box_screen_mode(), (uint8_t)BOXM_CARD);
  snapshot(SCR_BOX, "box_card");
}

// Spec section 9, and invariants B3 / B4: select active, swap, and a release
// that refuses the Bug you are carrying before any dialog is opened.
TEST(box_does_what_section_9_says) {
  seams2_reset();
  box_fixture(3);

  // VIEW on the ACTIVE slot goes to the BUG pages, because that one IS the
  // simulated pet; a stored one gets the card above instead.
  CHECK_EQ(box_screen_cursor(), (uint8_t)0);
  box_input(GST_TAP_R);
  box_input(GST_TAP_R);
  CHECK_EQ(g_push, (uint8_t)SCR_STATUS);
  CHECK_EQ(box_screen_mode(), (uint8_t)BOXM_ACTIONS);

  // Select active.
  box_enter();
  box_input(GST_TAP_L);                 // slot 2
  box_input(GST_TAP_R);
  box_input(GST_TAP_L);                 // BOXA_ACTIVATE
  box_input(GST_TAP_R);
  CHECK_EQ(g_box_active, (uint8_t)1);
  CHECK_EQ(box_screen_mode(), (uint8_t)BOXM_LIST);

  // Swap: pick the slot, pick the target, and the exchange goes through the
  // ui.cpp seam because it has to reach flash.
  box_enter();
  box_input(GST_TAP_R);                // slot 1
  box_input(GST_TAP_L); box_input(GST_TAP_L);   // BOXA_SWAP
  box_input(GST_TAP_R);
  CHECK_EQ(box_screen_mode(), (uint8_t)BOXM_SWAP);
  CHECK_EQ(g_toast, STR_BOX_SWAP_PICK);
  box_input(GST_TAP_L);                 // onto slot 2
  box_input(GST_TAP_R);
  CHECK_EQ(g_box_swap_a, (uint8_t)0);
  CHECK_EQ(g_box_swap_b, (uint8_t)1);
  CHECK_EQ(box_screen_mode(), (uint8_t)BOXM_LIST);

  // B4: the active Bug is refused before a dialog is ever opened.
  box_enter();                          // opens on the active slot
  const uint8_t active = box_active();
  CHECK(active != BOX_ACTIVE_NONE);
  box_input(GST_TAP_R);
  for (uint8_t i = 0; i < BOXA_RELEASE; ++i) box_input(GST_TAP_L);
  g_box_released = 0xFF;
  box_input(GST_TAP_R);
  CHECK_EQ(g_box_released, (uint8_t)0xFF);
  CHECK_EQ(g_toast, STR_BOX_NO_RELEASE_ACTIVE);

  // A stored one is offered, and only to the two dialogs.
  box_enter();
  box_input(GST_TAP_L);
  box_input(GST_TAP_R);
  for (uint8_t i = 0; i < BOXA_RELEASE; ++i) box_input(GST_TAP_L);
  box_input(GST_TAP_R);
  CHECK_EQ(g_box_released, (uint8_t)1);

  // TRADE AND BREED ARE ENTRY POINTS NOW (P7-C2), not a toast. Each one
  // PRE-SELECTS this slot, arms the LINK screen with the operation and pushes
  // it - and consents to nothing on the way: the radio is untouched and the
  // session still needs A on the card and A on the other device.
  box_enter();
  box_input(GST_TAP_L);
  box_input(GST_TAP_R);
  for (uint8_t i = 0; i < BOXA_TRADE; ++i) box_input(GST_TAP_L);
  g_push = 0xFF;
  box_input(GST_TAP_R);
  CHECK_EQ(g_push, (uint8_t)SCR_LINK);
  CHECK_EQ(box_screen_mode(), (uint8_t)BOXM_ACTIONS);   // still here underneath

  box_enter();
  box_input(GST_TAP_L);
  box_input(GST_TAP_R);
  for (uint8_t i = 0; i < BOXA_BREED; ++i) box_input(GST_TAP_L);
  g_push = 0xFF;
  box_input(GST_TAP_R);
  CHECK_EQ(g_push, (uint8_t)SCR_LINK);

  // B walks back through the modes and only then leaves the screen.
  box_enter();
  box_input(GST_TAP_L);
  box_input(GST_TAP_R);
  g_backs = 0;
  box_input(GST_HOLD_R);
  CHECK_EQ(box_screen_mode(), (uint8_t)BOXM_LIST);
  CHECK_EQ(g_backs, 0);
  box_input(GST_HOLD_R);
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
  box_input(GST_TAP_R);
  box_input(GST_TAP_R);                // BOXA_VIEW
  CHECK_EQ(box_screen_mode(), (uint8_t)BOXM_CARD);
  box_input(GST_HOLD_R);
  CHECK_EQ(box_screen_mode(), (uint8_t)BOXM_ACTIONS);
  CHECK_EQ(box_screen_cursor(), (uint8_t)BOXA_VIEW);
  CHECK_EQ(g_backs, 0);

  // The card's own single row means the same thing as B does.
  box_input(GST_TAP_R);
  CHECK_EQ(box_screen_mode(), (uint8_t)BOXM_CARD);
  box_input(GST_TAP_R);
  CHECK_EQ(box_screen_mode(), (uint8_t)BOXM_ACTIONS);

  // ACTIONS -> SWAP -> ACTIONS, cancelled by B and by picking the slot itself.
  box_input(GST_TAP_L); box_input(GST_TAP_L);   // BOXA_SWAP
  box_input(GST_TAP_R);
  CHECK_EQ(box_screen_mode(), (uint8_t)BOXM_SWAP);
  box_input(GST_HOLD_R);
  CHECK_EQ(box_screen_mode(), (uint8_t)BOXM_ACTIONS);
  CHECK_EQ(box_screen_cursor(), (uint8_t)BOXA_SWAP);

  g_box_swap_a = 0xFF;
  box_input(GST_TAP_R);                // into SWAP again, cursor on its own slot
  CHECK_EQ(box_screen_mode(), (uint8_t)BOXM_SWAP);
  box_input(GST_TAP_R);                // choosing itself is a cancel
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
  box_input(GST_TAP_R);
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
    CHECK(dialog_input(GST_HOLD_R));
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

// =============================================================================
//  THE PING CUE LETTER IS INSIDE THE HALF IT LABELS
//  Added at the FINAL REVIEW, with the golden above re-recorded, for a defect
//  that shipped in the only cue REFLEJOS has.
//
//  ping_draw() used gfx_text_center(), which centres over the whole 128 px panel
//  while the slab it labels is 64 px wide - so the letter ALWAYS straddled the
//  midline. gfx_text_w(GF_HEAD,"B") is 6, so x was (128-6)/2 = 61 and the glyph
//  ran 61..66: three columns on the slab and three on unlit ground, in BOTH
//  directions. The golden froze that as correct: the letter appeared as two
//  visible columns out of five, which nobody would read as a "B".
//
//  On the panel it is worse, and it is why this site had to move rather than be
//  left to the font-mode seam. ui/render.cpp holds the display in setFontMode(0)
//  - SOLID - for the whole session, so a glyph's 0-bits are painted in the
//  inverse of the draw colour; under GFX_ERASE that is LIT. The half of the
//  glyph box hanging off the slab therefore became a bright mark standing alone
//  in the DARK half, which no golden contains at all because the host fake draws
//  transparent text. With the whole box inside the slab, those background pixels
//  land on ground that is already lit - the "1 over 1" coincidence every other
//  text-over-ink site in the tree relies on - so the two backends agree here.
//
//  The snapshot alone would not have caught the original: it was ALREADY wrong
//  when it was recorded. This case measures the property instead.
// =============================================================================
TEST(the_ping_cue_letter_is_wholly_inside_the_half_it_labels) {
  // The lit half for target B is x 64..127; for A it is 0..63. The letter is
  // centred in that half, so its box must clear both the midline and the panel
  // edge with room to spare.
  const int16_t half = (int16_t)(OLED_W / 2);
  const int16_t tw_b = (int16_t)gfx_text_w(GF_HEAD, "B");
  const int16_t tw_a = (int16_t)gfx_text_w(GF_HEAD, "A");
  CHECK(tw_b > 0 && tw_b < half);
  CHECK(tw_a > 0 && tw_a < half);

  const int16_t xb = (int16_t)(half + (half - tw_b) / 2);
  CHECK(xb >= half);                       // no part of it in the dark half
  CHECK((int16_t)(xb + tw_b) <= (int16_t)OLED_W);

  const int16_t xa = (int16_t)(0 + (half - tw_a) / 2);
  CHECK(xa >= 0);
  CHECK((int16_t)(xa + tw_a) <= half);     // no part of it in the dark half

  // AND THE DRAWN FRAME AGREES, measured the one way the host CAN see it.
  // The fake draws transparent text, so the part of the glyph that hangs off
  // the slab writes 0 over 0 and leaves no trace - which is exactly why the
  // original defect was invisible to a golden. What IS visible is the KNOCKOUT:
  // the cue is drawn in GFX_ERASE, so its ink is a hole in the lit slab. Find
  // that hole and require it to be clear of BOTH edges of the half. Panel
  // centring puts it flush against the midline, which is the failure.
  fb_reset();
  mg_begin(g_mg, MG_PING, 1u);
  for (uint32_t i = 0; i < 30u && !g_mg.finished; ++i) mg_tick(g_mg, MG_PING);
  CHECK(ping_is_lit(g_mg));                // the cue really is up in this frame
  ping_draw(g_mg);
  const bool right = ping_target(g_mg) != 0;
  const int16_t lit_l = right ? half : (int16_t)0;
  const int16_t lit_r = right ? (int16_t)OLED_W : half;      // exclusive

  int16_t hole_l = (int16_t)OLED_W, hole_r = -1;
  for (int16_t y = (int16_t)UI_CONTENT_Y; y < (int16_t)(UI_CONTENT_Y + 32); ++y) {
    for (int16_t x = lit_l; x < lit_r; ++x) {
      if (!fb_get(x, y)) {                 // a dark pixel inside the lit slab
        if (x < hole_l) hole_l = x;
        if (x > hole_r) hole_r = x;
      }
    }
  }
  if (hole_r < 0) fprintf(stderr, "  the cue letter left no mark on the slab\n");
  CHECK(hole_r >= 0);                      // anti-vacuity: it really is drawn

  // Centred in the half means clear of both its edges. A letter flush against
  // an edge is one whose other half is off the slab.
  if (hole_l <= lit_l || hole_r >= (int16_t)(lit_r - 1))
    fprintf(stderr, "  cue ink spans %d..%d in a half of %d..%d: it is not "
                    "inside the slab it labels\n",
            (int)hole_l, (int)hole_r, (int)lit_l, (int)(lit_r - 1));
  CHECK(hole_l > lit_l);
  CHECK(hole_r < (int16_t)(lit_r - 1));
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
//  DETERMINISM: one fixed seed, a Box built by box_new_bug(), a clock the
//  seams hold still, and every gesture below is one a player could make.
// =============================================================================
static void battle_choose(uint8_t n) {
  for (uint8_t i = 0; i < n; ++i) {
    battle_input(GST_TAP_R);            // choose the slot under the cursor
    battle_input(GST_TAP_L);             // step to the next occupied one
  }
  while (battle_screen_cursor() < (uint8_t)BOX_SLOTS) battle_input(GST_TAP_L);
}

static void battle_pick_team(uint8_t n) {
  battle_choose(n);
  battle_input(GST_TAP_R);              // LISTO
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
    battle_input(GST_TAP_R);
  }
  return false;
}

static void battle_to_result(void) {
  for (int guard = 0; guard < 4000 && battle_screen_mode() != BTM_RESULT; ++guard)
    battle_input(GST_TAP_R);
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
  battle_input(GST_TAP_R);              // skip the stare-down
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
  battle_input(GST_TAP_R);
  CHECK(battle_to_beat((uint8_t)RLE_HIT));
  CHECK_EQ(battle_screen_event(), (uint8_t)RLE_HIT);
  CHECK_EQ(g_shakes, 1);                 // one shake per landed blow, not per frame
  snapshot(SCR_BATTLE, "battle_hit");
  battle_leave();
}

// A PROTECT: the ward standing in front of the defender.
//
// WHY THIS FIXTURE IS NOT box_fixture(3). Species 1's learnset holds no
// ATK_CAT_PROTECT move at all, so no practice battle between two of them can
// ever reach an RLE_PROTECT beat - a sweep of four hundred seeds against the
// shared fixture found none, which is exactly why the effect went five phases
// without a picture. Species 2 knows Sandbox (attack id 31, ATK_EFF_PROTECT_HALF)
// at learnset index 2, so the player can ASK for the beat rather than waiting
// for the AI to hand it over, and the resulting golden is of a protect the test
// caused on purpose.
static void protect_fixture(void) {
  memset(&g_gs, 0, sizeof g_gs);
  box_bind(g_gs);
  Genome gen;
  memset(&gen, 0, sizeof gen);
  gen.magic_ver  = GENOME_MAGIC_VER;
  gen.lineage_id = 0x0BADF00Du;
  gen.g0 = 0x1234u; gen.g1 = 0x5678u; gen.g2 = 0x9ABCu;
  gen.generation = 3;
  for (uint8_t i = 0; i < 3u; ++i) {
    const uint8_t slot = box_new_bug(2u, (uint8_t)(6u + i), ORIGIN_STARTER,
                                        gen, 0xC0FFEEu + i, 1000u);
    CHECK(slot != BOX_SLOT_NONE);
  }
  CHECK(box_set_active(0));
  box_enter();
}

// Play to the first RLE_PROTECT beat, choosing Sandbox (learnset row 2) every
// round. False when the battle ended first.
static bool battle_to_protect(void) {
  for (int g = 0; g < 4000; ++g) {
    const uint8_t m = battle_screen_mode();
    if (m == (uint8_t)BTM_RESULT) return false;
    if (m == (uint8_t)BTM_RESOLVE) {
      if (battle_screen_event() == (uint8_t)RLE_PROTECT) return true;
      battle_input(GST_TAP_L);
      continue;
    }
    if (m == (uint8_t)BTM_MENU) {
      for (int t = 0; t < 2; ++t) battle_input(GST_TAP_L);
    }
    battle_input(GST_TAP_R);
  }
  return false;
}

TEST(snapshot_battle_protect) {
  seams2_reset();
  protect_fixture();
  battle_arm(BT_ENTRY_PRACTICE, 0xB0A71E00u);
  battle_enter();
  battle_pick_team((uint8_t)BATTLE_TEAM_MAX);
  battle_input(GST_TAP_R);
  CHECK(battle_to_protect());
  CHECK_EQ(battle_screen_event(), (uint8_t)RLE_PROTECT);

  // THE WARD IS ON THE CREATURE THAT ASKED FOR IT, and this is the check that
  // an inverted comparison in fill_art() fails. The player is the one who chose
  // Sandbox, so the barrier must stand in front of battle_screen_side() and in
  // front of nobody else. Asserting only "exactly one is guarded" would pass
  // with the ward drawn on the wrong creature in every protect in the game.
  const uint8_t me = battle_screen_side();
  CHECK_EQ(battle_screen_guard(me), 1u);
  CHECK_EQ(battle_screen_guard((uint8_t)(me ^ 1u)), 0u);

  // AND NEITHER PANEL EFFECT FIRED. A protect is the beat where nothing landed;
  // borrowing rd_shake or rd_flash would tell the player the opposite.
  const int shakes_before = g_shakes, flashes_before = g_flashes;
  snapshot(SCR_BATTLE, "battle_protect");
  CHECK_EQ(g_shakes, shakes_before);
  CHECK_EQ(g_flashes, flashes_before);
  battle_leave();
}

// The transcript, beat by beat, from the first to the last: a ward on exactly
// one combatant at every RLE_PROTECT and on NEITHER at every other beat.
//
// This is the anti-vacuity half of the golden above. A single snapshot says
// what one frame looks like; what makes the effect correct is that it is a
// BEAT and not a status - protect_left keeps burning for two more rounds after
// the arm, and a barrier that stayed up while the player chose their next move
// would be a claim about a round that has not been played.
TEST(the_ward_marks_one_combatant_on_protect_beats_and_nobody_on_the_others) {
  seams2_reset();
  protect_fixture();
  battle_arm(BT_ENTRY_PRACTICE, 0xB0A71E00u);
  battle_enter();
  battle_pick_team((uint8_t)BATTLE_TEAM_MAX);
  battle_input(GST_TAP_R);

  int protects = 0, beats = 0, menus = 0;
  for (int g = 0; g < 4000; ++g) {
    const uint8_t m = battle_screen_mode();
    if (m == (uint8_t)BTM_RESULT) break;
    if (m == (uint8_t)BTM_RESOLVE) {
      const int warded = (int)battle_screen_guard(0) + (int)battle_screen_guard(1);
      const bool is_protect = (battle_screen_event() == (uint8_t)RLE_PROTECT);
      if (warded != (is_protect ? 1 : 0)) {
        fprintf(stderr, "  BATTLE beat %d: event %u has %d warded combatant(s), "
                        "expected %d\n", beats, (unsigned)battle_screen_event(),
                warded, is_protect ? 1 : 0);
      }
      CHECK_EQ(warded, is_protect ? 1 : 0);
      if (is_protect) ++protects;
      ++beats;
      battle_input(GST_TAP_L);
      continue;
    }
    if (m == (uint8_t)BTM_MENU) {
      // The ward is OFF while the player is choosing, however much protect_left
      // is still in the bank.
      CHECK_EQ(battle_screen_guard(0), 0u);
      CHECK_EQ(battle_screen_guard(1), 0u);
      ++menus;
      for (int t = 0; t < 2; ++t) battle_input(GST_TAP_L);
    }
    battle_input(GST_TAP_R);
  }
  // "No beat was warded" must not be able to pass as "every beat was right".
  CHECK(protects > 0);
  CHECK(beats > protects);
  CHECK(menus > 0);
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
  battle_input(GST_TAP_R);
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
  battle_input(GST_TAP_R);
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
  play_input(GST_TAP_R);
  CHECK_EQ(g_battle_starts, 1);
  CHECK_EQ(g_battle_entry, (uint8_t)BT_ENTRY_PRACTICE);
  CHECK_EQ(g_minigame, 0xFF);            // and NOT a minigame
  CHECK_EQ(g_backs, 0);

  // The row before it is still the last minigame, which is the mistake the
  // widened fold exists to prevent.
  play_enter();
  for (uint8_t i = 0; i + 1u < PLAY_BATTLE; ++i) play_input(GST_TAP_L);
  play_input(GST_TAP_R);
  CHECK_EQ(g_minigame, (uint8_t)(PLAY_BATTLE - 1u));
  CHECK_EQ(g_battle_starts, 1);

  // And the row after it still means "leave".
  play_enter();
  for (uint8_t i = 0; i < PLAY_BACK; ++i) play_input(GST_TAP_L);
  play_input(GST_TAP_R);
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
  network_input((Gesture)GST_HOLD_R);
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

  // WILD: THREE options now - catch it, fight it, or walk away - and the ring
  // walks all three and wraps.
  r.outcome = (uint8_t)ENC_OUT_WILD; r.species_id = 1; r.level = 7;
  encounter_arm(r, (uint8_t)NET_CAT_HOME);
  encounter_enter();
  CHECK_EQ(encounter_screen_cursor(), (int)ENC_OPT_CATCH);
  encounter_input((Gesture)GST_TAP_L);
  CHECK_EQ(encounter_screen_cursor(), (int)ENC_OPT_FIGHT);
  encounter_input((Gesture)GST_TAP_L);
  CHECK_EQ(encounter_screen_cursor(), (int)ENC_OPT_LEAVE);
  encounter_input((Gesture)GST_TAP_L);
  CHECK_EQ(encounter_screen_cursor(), (int)ENC_OPT_CATCH);   // and it wraps
  encounter_input((Gesture)GST_HOLD_L);
  CHECK_EQ(g_push, (uint8_t)SCR_CAPTURE);

  // LUCHAR hands the ENCOUNTER'S OWN creature to the battle - the species and
  // the level the player was just looking at, not a re-roll - and it does NOT
  // push, because a beaten wild Bug must not be left underneath still
  // offering to be caught.
  g_push = 0xFF; g_wild_starts = 0;
  encounter_input((Gesture)GST_TAP_L);
  CHECK_EQ(encounter_screen_cursor(), (int)ENC_OPT_FIGHT);
  encounter_input((Gesture)GST_HOLD_L);
  CHECK_EQ(g_wild_starts, 1);
  CHECK_EQ((int)g_wild_sp, 1);
  CHECK_EQ((int)g_wild_lv, 7);
  CHECK_EQ(g_push, 0xFF);

  // DEJAR is a real answer and leaves without capturing or fighting.
  const int backs = g_backs;
  encounter_input((Gesture)GST_TAP_L);
  CHECK_EQ(encounter_screen_cursor(), (int)ENC_OPT_LEAVE);
  encounter_input((Gesture)GST_HOLD_L);
  CHECK_EQ(g_push, 0xFF);
  CHECK_EQ(g_wild_starts, 1);                      // still one: no second fight
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

// =============================================================================
//  THE TWO FILMS (P10-C3)
//
//  Spec section 22's item pickup and section 23's successful capture had no
//  picture at all before this chunk. They are written in ui/screen_encounter.cpp
//  - a PURE, host-linked translation unit - and not in ui/actfx.cpp, which
//  includes render.h and is compiled by no binary here, so unlike actfx's seven
//  these two can be snapshotted, bounded and interrupted under test.
// =============================================================================

// A caught wild Bug. The roll and the device seed are the two numbers
// game/capture.cpp mixes, and these two produce CAP_CAUGHT on the first throw;
// the Box is empty, so nothing can refuse the file.
static void caught_fixture(void) {
  seams2_reset();
  explore_reset();
  memset(&g_gs, 0, sizeof g_gs);
  box_bind(g_gs);
  EncounterResult r;
  memset(&r, 0, sizeof r);
  r.outcome = (uint8_t)ENC_OUT_WILD; r.species_id = 1; r.level = 1;
  encounter_arm(r, (uint8_t)NET_CAT_HOME);
  inv_add(g_inv, 5u, 3u);
  g_roll = 0x1000u;
  g_dev  = 0xABCD0000u;
  capture_enter();
}

static void item_fixture(uint8_t item_id) {
  seams2_reset();
  explore_reset();
  EncounterResult r;
  memset(&r, 0, sizeof r);
  r.outcome = (uint8_t)ENC_OUT_ITEM; r.item_id = item_id;
  encounter_arm(r, (uint8_t)NET_CAT_HOME);
  encounter_enter();
}

// THE DROP, MID-CLIMB. The icon is crossing its own label here, which is the
// frame that made the gfx_xbm()/gfx_xbm_t() divergence worth finding first: the
// device's opaque blit would have punched a 12x12 hole through the name.
TEST(snapshot_encounter_item_film) {
  item_fixture(1u);                                   // Bit Dulce - XP_CANDY
  CHECK_EQ(enc_film_phase(), (uint8_t)ENC_FILM_ITEM_RISE);
  // 450 ms: near the top of the climb, still crossing the label, and with the
  // spark at three quarters of its reach. A film golden taken early enough that
  // nothing has happened yet is a golden of the resting screen with an icon on
  // it, which is what the second snapshot below is for.
  g_now += (ENC_ITEM_RISE_MS * 7u) / 8u;
  CHECK_EQ(enc_film_phase(), (uint8_t)ENC_FILM_ITEM_RISE);
  snapshot(SCR_ENCOUNTER, "encounter_item_film");
  encounter_leave();
}

// And the same screen once the film has run out: the words alone, which is
// what the player is left looking at. The pair is the point - a golden of a
// film with no golden of its resting state cannot say what the film added.
TEST(snapshot_encounter_item_settled) {
  item_fixture(1u);
  g_now += 4000u;                                     // long past the end
  CHECK_EQ(enc_film_phase(), (uint8_t)ENC_FILM_NONE);
  snapshot(SCR_ENCOUNTER, "encounter_item");
  encounter_leave();
}

// =============================================================================
//  THE WILD REVEAL
//
//  The moment the whole exploration loop exists to produce, and until the owner
//  played the build it was the only one in the chain with no picture: an item
//  drop had a film, a successful capture had a film, and FINDING THE CREATURE
//  opened straight onto two menu options.
// =============================================================================
static void wild_fixture(uint8_t species) {
  seams2_reset();
  explore_reset();
  EncounterResult r;
  memset(&r, 0, sizeof r);
  r.outcome = (uint8_t)ENC_OUT_WILD; r.species_id = species; r.level = 7;
  encounter_arm(r, (uint8_t)NET_CAT_HOME);
  encounter_enter();
}

TEST(the_wild_reveal_walks_its_three_beats_and_the_clock_ends_it) {
  wild_fixture(1u);
  // DRIVEN BY THE TIMETABLE'S OWN NAMES (screen_encounter.h), not by literals:
  // the beats were retimed once already and a case full of magic milliseconds
  // is a second copy of the schedule.
  CHECK_EQ(enc_film_phase(), (uint8_t)ENC_FILM_WILD_TEAR);
  g_now += ENC_WILD_TEAR_MS - 1u;
  CHECK_EQ(enc_film_phase(), (uint8_t)ENC_FILM_WILD_TEAR);
  g_now += 2u;
  CHECK_EQ(enc_film_phase(), (uint8_t)ENC_FILM_WILD_FORM);
  g_now += ENC_WILD_FORM_MS - ENC_WILD_TEAR_MS;
  CHECK_EQ(enc_film_phase(), (uint8_t)ENC_FILM_WILD_STARE);
  g_now += ENC_WILD_END_MS - ENC_WILD_FORM_MS;
  CHECK_EQ(enc_film_phase(), (uint8_t)ENC_FILM_NONE);
  encounter_leave();
}

// THE REVEAL IS PAID FOR ONCE. encounter_enter() runs again on every walk back
// out of SCR_CAPTURE, and this is the check that a failed throw does not buy
// the player a second establishing shot. Deleting the s_applied guard in
// encounter_enter()'s WILD arm fails here by name.
TEST(the_wild_reveal_does_not_replay_when_the_player_comes_back_from_capture) {
  wild_fixture(1u);
  CHECK(enc_film_phase() != (uint8_t)ENC_FILM_NONE);
  encounter_input((Gesture)GST_HOLD_L);               // -> SCR_CAPTURE
  CHECK_EQ(g_push, (uint8_t)SCR_CAPTURE);
  encounter_leave();
  capture_enter();
  capture_leave();
  encounter_enter();                                  // back on the encounter
  CHECK_EQ(enc_film_phase(), (uint8_t)ENC_FILM_NONE);
  encounter_leave();
}

// THE TEAR IS REPRODUCIBLE. Two runs of the same beat draw the same frame -
// which is what makes a golden of a glitch possible at all. Replacing
// enc_noise() with anything that carries state across calls fails here.
TEST(the_wild_tear_is_the_same_picture_every_time_it_is_played) {
  wild_fixture(1u);
  const uint32_t t0 = g_now;
  g_now = t0 + ENC_WILD_TEAR_MS / 2u;
  fb_reset();
  encounter_render();
  uint8_t first[FB_H][FB_W];
  for (int y = 0; y < FB_H; ++y)
    for (int x = 0; x < FB_W; ++x) first[y][x] = (uint8_t)fb_get(x, y);
  encounter_leave();

  wild_fixture(1u);
  g_now += ENC_WILD_TEAR_MS / 2u;
  fb_reset();
  encounter_render();
  int diff = 0;
  int lit  = 0;
  for (int y = 0; y < FB_H; ++y)
    for (int x = 0; x < FB_W; ++x) {
      if ((uint8_t)fb_get(x, y) != first[y][x]) ++diff;
      lit += (int)fb_get(x, y);
    }
  CHECK_EQ(diff, 0);
  CHECK(lit > 0);                                     // anti-vacuity
  encounter_leave();
}

// THE BAND SNAPS. The last beat inverts the whole content band, which is what
// covers the cut from a 24x24 creature to a two-option menu that shares no
// pixel with it. Measured as "most of the band changed", not as an exact count.
TEST(the_wild_reveal_ends_on_a_full_band_snap) {
  wild_fixture(1u);
  const uint32_t t0 = g_now;
  g_now = t0 + ENC_WILD_SNAP_MS - 60u;                // STARE, before the snap
  fb_reset();
  encounter_render();
  uint8_t before[FB_H][FB_W];
  for (int y = 0; y < FB_H; ++y)
    for (int x = 0; x < FB_W; ++x) before[y][x] = (uint8_t)fb_get(x, y);

  g_now = t0 + ENC_WILD_SNAP_MS + 60u;                // inside the snap
  fb_reset();
  encounter_render();
  int flipped = 0;
  for (int y = (int)UI_CONTENT_Y; y <= (int)UI_CONTENT_BOTTOM; ++y)
    for (int x = 0; x < FB_W; ++x)
      if ((uint8_t)fb_get(x, y) != before[y][x]) ++flipped;
  const int band = (int)(UI_CONTENT_BOTTOM - UI_CONTENT_Y + 1) * FB_W;
  CHECK(flipped > (band * 3) / 4);
  encounter_leave();
}

// The tear, mid-corruption: no body yet, and the band torn into scanlines.
TEST(snapshot_encounter_wild_tear) {
  wild_fixture(1u);
  g_now += ENC_WILD_TEAR_MS / 2u;
  CHECK_EQ(enc_film_phase(), (uint8_t)ENC_FILM_WILD_TEAR);
  snapshot(SCR_ENCOUNTER, "encounter_wild_tear");
  encounter_leave();
}

// And the body half-assembled, which is the frame the film is for: the feet are
// on, the head is not, and two bars of the tear are still standing.
TEST(snapshot_encounter_wild_form) {
  wild_fixture(1u);
  g_now += (ENC_WILD_TEAR_MS + ENC_WILD_FORM_MS) / 2u;
  CHECK_EQ(enc_film_phase(), (uint8_t)ENC_FILM_WILD_FORM);
  snapshot(SCR_ENCOUNTER, "encounter_wild_form");
  encounter_leave();
}

// =============================================================================
//  THE CALL SITE THE HOST COULD NOT SEE UNTIL NOW.
//
//  tests/test_box_persist.cpp proves the PERSISTENCE half - that a slot other
//  than the active one only reaches flash through gs_save_slot(). This case is
//  the other half: that ui/screen_encounter.cpp actually TELLS the seam which
//  slot it filled. Neither half alone would have caught the defect, because the
//  defect was a call site that named no slot at all while the header it wrote
//  claimed one, and ui.cpp is compiled by no host binary.
//
//  The assertion is on the SLOT and not on the call count: g_saves was already
//  non-zero on this path before the fix.
// =============================================================================
TEST(a_catch_tells_the_commit_which_slot_it_filled) {
  seams2_reset();
  explore_reset();
  memset(&g_gs, 0, sizeof g_gs);
  box_bind(g_gs);

  // A BUG IN SLOT 0 FIRST, AND THAT IS THE WHOLE POINT OF THE FIXTURE.
  // caught_fixture() catches into an EMPTY Box, so the catch lands in slot 0,
  // which is also box_active() - the ONE case the broken commit handled. The
  // defect only appears from the SECOND creature onward.
  Genome gen; memset(&gen, 0, sizeof gen);
  gen = genome_genesis();
  const uint8_t starter = box_new_bug(1u, 5u, (uint8_t)ORIGIN_STARTER,
                                         gen, 0xC0FFEEu, 1700300000u);
  CHECK_EQ((int)starter, 0);
  CHECK(box_set_active(starter));

  EncounterResult r;
  memset(&r, 0, sizeof r);
  r.outcome = (uint8_t)ENC_OUT_WILD; r.species_id = 1; r.level = 1;
  encounter_arm(r, (uint8_t)NET_CAT_HOME);
  inv_add(g_inv, 5u, 3u);
  g_roll = 0x1000u;
  g_dev  = 0xABCD0000u;
  capture_enter();

  const uint8_t before = box_count();
  capture_input((Gesture)GST_HOLD_L);                 // throw
  CHECK_EQ(capture_screen_outcome(), (uint8_t)CAP_CAUGHT);
  CHECK_EQ((int)box_count(), (int)(before + 1u));
  CHECK(g_saves > 0);
  // THE ASSERTION THAT WOULD HAVE FAILED BEFORE THE FIX: the commit was told
  // BOX_SLOT_NONE, so the creature in slot 1 never reached flash while the Box
  // header written by the same commit already claimed it.
  CHECK(g_save_slot < (uint8_t)BOX_SLOTS);
  CHECK(g_save_slot != box_active());
  CHECK_EQ((int)g_save_slot, 1);                      // first_free() past the starter
  CHECK(box_occupied(g_save_slot));
  capture_leave();
}

// THE CATCH, MID-DISSOLVE: the creature coming apart upward between four
// brackets that have already closed.
TEST(snapshot_capture_caught_pull) {
  caught_fixture();
  capture_input((Gesture)GST_HOLD_L);                 // throw
  CHECK_EQ(capture_screen_outcome(), (uint8_t)CAP_CAUGHT);
  // capture_input() cancels any film in flight before it acts - that is the
  // skip contract - so the throw's own film is armed AFTER the cancel and is
  // live here. Asserting it rather than assuming it: an ordering slip would
  // leave every successful capture with no film at all and every golden below
  // would still be a valid picture of the screen.
  CHECK_EQ(enc_film_phase(), (uint8_t)ENC_FILM_CAP_CLAMP);
  g_now += (ENC_CAP_CLAMP_MS + ENC_CAP_PULL_MS) / 2u;
  CHECK_EQ(enc_film_phase(), (uint8_t)ENC_FILM_CAP_PULL);
  snapshot(SCR_CAPTURE, "capture_caught_pull");
  capture_leave();
}

// And the seal, which is the beat the film ends on: something arriving rather
// than something gone.
TEST(snapshot_capture_caught_seal) {
  caught_fixture();
  capture_input((Gesture)GST_HOLD_L);
  g_now += (ENC_CAP_PULL_MS + ENC_CAP_END_MS) / 2u;
  CHECK_EQ(enc_film_phase(), (uint8_t)ENC_FILM_CAP_SEAL);
  snapshot(SCR_CAPTURE, "capture_caught_seal");
  capture_leave();
}

// -----------------------------------------------------------------------------
//  THE INTERRUPTION CONTRACT (ui/actfx.h:88-93), DRIVEN.
//
//  actfx states this guarantee for its seven films and NOTHING IN THIS
//  REPOSITORY ASSERTS IT, because ui/actfx.cpp is compiled by no host binary.
//  These two films are the first whose bound can actually be checked, so it is.
// -----------------------------------------------------------------------------

// THE CLOCK ENDS IT, WITH NO CANCEL ANYWHERE. Not one input is delivered and
// not one leave hook is called: the film simply runs out. If enc_film_phase()
// ever grows a latch instead of being a pure function of (now - t0), this is
// the case that fails.
TEST(a_film_ends_by_the_clock_alone_even_though_nothing_cancels_it) {
  item_fixture(1u);
  CHECK(enc_film_phase() != (uint8_t)ENC_FILM_NONE);
  g_now += ENC_ITEM_END_MS - 1u;                   // one millisecond short
  CHECK(enc_film_phase() != (uint8_t)ENC_FILM_NONE);
  g_now += 1u;                                     // and exactly at the end
  CHECK_EQ(enc_film_phase(), (uint8_t)ENC_FILM_NONE);
  g_now += 100000u;
  CHECK_EQ(enc_film_phase(), (uint8_t)ENC_FILM_NONE);

  caught_fixture();
  capture_input((Gesture)GST_HOLD_L);
  CHECK(enc_film_phase() != (uint8_t)ENC_FILM_NONE);
  g_now += ENC_CAP_END_MS - 1u;
  CHECK(enc_film_phase() != (uint8_t)ENC_FILM_NONE);
  g_now += 1u;
  CHECK_EQ(enc_film_phase(), (uint8_t)ENC_FILM_NONE);
  capture_leave();
}

// ANY GESTURE SKIPS IT, and the words arrive on the same frame. A player who
// has pressed something has stopped watching.
TEST(any_gesture_skips_a_film_and_the_screen_answers_at_once) {
  static const Gesture kAll[] = { GST_TAP_L, GST_HOLD_R, GST_HOLD_L, GST_TAP_R,
                                  GST_BOTH, GST_LONG_BOTH };
  for (unsigned i = 0; i < sizeof(kAll) / sizeof(kAll[0]); ++i) {
    item_fixture(1u);
    CHECK(enc_film_phase() != (uint8_t)ENC_FILM_NONE);
    encounter_input(kAll[i]);
    CHECK_EQ(enc_film_phase(), (uint8_t)ENC_FILM_NONE);
    encounter_leave();
  }
  // And leaving the screen ends it too, which is the third of the three
  // independent bounds - on BOTH screens. Covering only ENCOUNTER here is what
  // the mutation run found: deleting enc_film_cancel() from capture_leave()
  // left every test green and slipped under the call-site gate, which counts
  // and cannot see which function a call is in.
  item_fixture(1u);
  CHECK(enc_film_phase() != (uint8_t)ENC_FILM_NONE);
  encounter_leave();
  CHECK_EQ(enc_film_phase(), (uint8_t)ENC_FILM_NONE);

  caught_fixture();
  capture_input((Gesture)GST_HOLD_L);
  CHECK(enc_film_phase() != (uint8_t)ENC_FILM_NONE);
  capture_leave();
  CHECK_EQ(enc_film_phase(), (uint8_t)ENC_FILM_NONE);

  // AND THE FIFTH CALL SITE, capture_enter(), WHICH NEITHER INSTRUMENT COULD
  // SEE UNTIL P10-C6. Deleting its enc_film_cancel() left the whole suite green
  // AND printed GATE OK - the count gate has a spare unit because the FUNCTION
  // DEFINITION matches the same pattern as the five call sites, so any one of
  // them could go. It is unreachable on today's navigation (encounter_input()
  // cancels nine lines above the only ui_push(SCR_CAPTURE)), which is exactly
  // why nothing noticed; a screen reached from a second place later would find
  // the redundancy gone. The state is driven directly here, and the gate is
  // exact now rather than "at least five".
  item_fixture(1u);
  CHECK(enc_film_phase() != (uint8_t)ENC_FILM_NONE);
  capture_enter();
  CHECK_EQ(enc_film_phase(), (uint8_t)ENC_FILM_NONE);
  capture_leave();
  encounter_leave();

  // ...and CAPTURE's every gesture skips it too. A film that only the ENCOUNTER
  // screen could interrupt would leave the player watching a creature dissolve
  // with both buttons dead.
  for (unsigned i = 0; i < sizeof(kAll) / sizeof(kAll[0]); ++i) {
    caught_fixture();
    capture_input((Gesture)GST_HOLD_L);
    CHECK(enc_film_phase() != (uint8_t)ENC_FILM_NONE);
    capture_input(kAll[i]);
    CHECK_EQ(enc_film_phase(), (uint8_t)ENC_FILM_NONE);
    capture_leave();
  }
}

// THE millis() WRAP. A film armed 40 ms before the wrap must measure 40 ms
// after it and not 4,294,967,256 - which would end it instantly and, worse,
// would end it instantly only once every 49.7 days.
TEST(a_film_armed_before_the_millis_wrap_measures_forward_and_not_backward) {
  // The fixture resets the clock, so the arm has to happen AFTER it is moved.
  seams2_reset();
  explore_reset();
  EncounterResult r;
  memset(&r, 0, sizeof r);
  r.outcome = (uint8_t)ENC_OUT_ITEM; r.item_id = 1u;
  encounter_arm(r, (uint8_t)NET_CAT_HOME);
  g_now = 0xFFFFFFC0u;                    // 64 ms before the wrap
  encounter_enter();                      // arms the film at that instant
  CHECK_EQ(enc_film_phase(), (uint8_t)ENC_FILM_ITEM_RISE);
  g_now += 64u;                           // the wrap itself
  CHECK_EQ(g_now, 0u);
  CHECK_EQ(enc_film_phase(), (uint8_t)ENC_FILM_ITEM_RISE);
  g_now += ENC_ITEM_RISE_MS;              // past the climb, across the wrap
  CHECK_EQ(enc_film_phase(), (uint8_t)ENC_FILM_ITEM_SETTLE);
  g_now += ENC_ITEM_END_MS;               // and over
  CHECK_EQ(enc_film_phase(), (uint8_t)ENC_FILM_NONE);
  encounter_leave();
  g_now = 100000u;
}

// -----------------------------------------------------------------------------
//  THE TWO BOUNDS ON THE PIXELS, both driven every 10 ms of both films.
//
//  (a) CONTAINMENT. Every pixel a film changes lies inside the content band.
//      The header and the affordance strip are drawn by other modules on every
//      frame, so a film reaching either would be scribbling on furniture - and
//      it is the kind of defect that shows up as a flicker on hardware and as
//      nothing at all in a single golden. This is the same shape
//      tests/test_corruption.cpp asserts for the glitch rows: the check is on
//      the PIXELS A PAINTER ACTUALLY TOUCHED, not on the five numbers it was
//      handed.
//
//  (b) THE OVERLAY NEVER ERASES. The item film crosses its own label. Every
//      pixel that is lit with the film cancelled must still be lit with the
//      film running: the icon may ADD ink and may never take it away. That is
//      exactly what gfx_xbm_t() buys and exactly what gfx_xbm() would break -
//      and until P10-C3 the host fake drew both the same way, so this test
//      could not have failed however wrong the call was.
// -----------------------------------------------------------------------------
static uint8_t g_base_fb[FB_H][FB_W];

static void capture_base(void (*render)(void)) {
  fb_reset();
  render();
  for (int y = 0; y < FB_H; ++y)
    for (int x = 0; x < FB_W; ++x) g_base_fb[y][x] = (uint8_t)fb_get(x, y);
}

TEST(every_frame_of_both_films_stays_inside_the_content_band) {
  int frames = 0, moved = 0;

  // ITEM. The baseline is the same screen with the film already over.
  item_fixture(1u);
  const uint32_t t0_item = g_now;
  g_now = t0_item + 5000u;                       // film over
  CHECK_EQ(enc_film_phase(), (uint8_t)ENC_FILM_NONE);
  capture_base(encounter_render);
  for (uint32_t t = 0; t < ENC_ITEM_END_MS; t += 10u) {
    g_now = t0_item + t;
    fb_reset();
    encounter_render();
    CHECK_EQ(fb_oob(), 0u);
    ++frames;
    for (int y = 0; y < FB_H; ++y) {
      for (int x = 0; x < FB_W; ++x) {
        const uint8_t now = (uint8_t)fb_get(x, y);
        if (now == g_base_fb[y][x]) continue;
        ++moved;
        if (y < (int)UI_CONTENT_Y || y > (int)UI_CONTENT_BOTTOM) {
          fprintf(stderr, "  ITEM film t=%u: pixel (%d,%d) changed OUTSIDE the "
                          "content band %d..%d\n", (unsigned)t, x, y,
                  (int)UI_CONTENT_Y, (int)UI_CONTENT_BOTTOM);
          CHECK(false);
        }
        // (b): the overlay may add ink and may never take it away.
        if (g_base_fb[y][x] == 1u && now == 0u) {
          fprintf(stderr, "  ITEM film t=%u: pixel (%d,%d) was ERASED - the "
                          "overlay must not punch a hole in its own label\n",
                  (unsigned)t, x, y);
          CHECK(false);
        }
      }
    }
  }
  encounter_leave();

  // CAPTURE. Its film REPLACES the outcome line rather than overlaying it, so
  // only the containment half applies; the baseline is the resolved screen.
  caught_fixture();
  capture_input((Gesture)GST_HOLD_L);
  const uint32_t t0_cap = g_now;
  g_now = t0_cap + 5000u;
  CHECK_EQ(enc_film_phase(), (uint8_t)ENC_FILM_NONE);
  capture_base(capture_render);
  for (uint32_t t = 0; t < ENC_CAP_END_MS; t += 10u) {
    g_now = t0_cap + t;
    fb_reset();
    capture_render();
    CHECK_EQ(fb_oob(), 0u);
    ++frames;
    for (int y = 0; y < FB_H; ++y) {
      for (int x = 0; x < FB_W; ++x) {
        if ((uint8_t)fb_get(x, y) == g_base_fb[y][x]) continue;
        ++moved;
        if (y < (int)UI_CONTENT_Y || y > (int)UI_CONTENT_BOTTOM) {
          fprintf(stderr, "  CAPTURE film t=%u: pixel (%d,%d) changed OUTSIDE "
                          "the content band %d..%d\n", (unsigned)t, x, y,
                  (int)UI_CONTENT_Y, (int)UI_CONTENT_BOTTOM);
          CHECK(false);
        }
      }
    }
  }
  capture_leave();

  // THE WILD REVEAL. It replaces the band too, and it is the one film that ends
  // on a deliberate full-band INVERT - so the erase half of the rule cannot
  // apply to it by construction, and the containment half is the whole check.
  wild_fixture(1u);
  const uint32_t t0_wild = g_now;
  g_now = t0_wild + 5000u;
  CHECK_EQ(enc_film_phase(), (uint8_t)ENC_FILM_NONE);
  capture_base(encounter_render);
  for (uint32_t t = 0; t < ENC_WILD_END_MS; t += 10u) {
    g_now = t0_wild + t;
    fb_reset();
    encounter_render();
    CHECK_EQ(fb_oob(), 0u);
    ++frames;
    for (int y = 0; y < FB_H; ++y) {
      for (int x = 0; x < FB_W; ++x) {
        if ((uint8_t)fb_get(x, y) == g_base_fb[y][x]) continue;
        ++moved;
        if (y < (int)UI_CONTENT_Y || y > (int)UI_CONTENT_BOTTOM) {
          fprintf(stderr, "  WILD film t=%u: pixel (%d,%d) changed OUTSIDE the "
                          "content band %d..%d\n", (unsigned)t, x, y,
                  (int)UI_CONTENT_Y, (int)UI_CONTENT_BOTTOM);
          CHECK(false);
        }
      }
    }
  }
  encounter_leave();

  // ANTI-VACUITY. "Nothing moved outside the band" must not be able to pass
  // because nothing moved at all.
  CHECK_EQ(frames, (int)(((ENC_ITEM_END_MS + 9u) / 10u) + ((ENC_CAP_END_MS + 9u) / 10u) +
                         ((ENC_WILD_END_MS + 9u) / 10u)));
  CHECK(moved > 1000);
  g_now = 100000u;
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
  // PAST THE REVEAL. encounter_enter() now arms a film on a wild encounter, and
  // this golden is the RESTING screen - the two options the player answers. The
  // film has goldens of its own below; taking this one at t=0 would replace the
  // question with the first frame of the picture that asks it.
  g_now += 2000u;
  CHECK_EQ(enc_film_phase(), (uint8_t)ENC_FILM_NONE);
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
// =============================================================================
//  P10-C6: THE ITEM SAYS WHAT IT DID
//
//  bag_use() declared an ItemEffect, handed it to inv_use(), and never read a
//  field of it. Every successful use of every item in the game answered
//  "Usado": filling five care bars from empty, curing SICK and CORRUPTED at
//  once and jumping a Bug eight levels were the same single word, on the
//  half of the care loop that carries the rewards from exploring - and there is
//  no item description anywhere in the product, so the bag row ("NAME xN") is
//  all a player ever learns about what they are holding.
//
//  Driven through the REAL inv_use() over the REAL pack, not by handing
//  item_reaction() a struct: the point is that the fields the pack fills are
//  the fields the screen reads.
// =============================================================================
TEST(using_an_item_says_which_thing_it_did_and_not_just_that_it_was_used) {
  // TWO ACCEPTABLE ANSWERS PER KLASS, BECAUSE THE LADDER IS ORDERED AND THE
  // PACK DECIDES WHICH RUNG IT LANDS ON - and my first draft of this case got
  // that wrong twice: "Parche" heals a Bug whose status bits are set, so it
  // reports the CURE rather than the bar, and "Bit Dulce" on a level-5 Bug
  // crosses a level boundary, so it reports the LEVEL rather than the XP. Both
  // times the code was right and the expectation was a guess. What the case
  // states is the property that matters: the answer is a REACTION to what the
  // pack recorded, never the old catch-all.
  struct Arm { uint8_t klass; uint16_t a; uint16_t b; };
  static const Arm kArms[] = {
    { (uint8_t)ITEM_KLASS_CARE,       STR_ITEM_FED,      STR_ITEM_CURED   },
    { (uint8_t)ITEM_KLASS_XP_CANDY,   STR_ITEM_XP,       STR_ITEM_LEVELED },
    { (uint8_t)ITEM_KLASS_BATTLE_MOD, STR_ITEM_BOOST,    STR_ITEM_BOOST   },
  };
  uint8_t seen = 0;
  for (size_t a = 0; a < sizeof kArms / sizeof kArms[0]; ++a) {
    uint8_t id = 0;
    for (uint8_t i = 0; i < ITEM_COUNT; ++i)
      if (ITEMS_TABLE[i].klass == kArms[a].klass && id == 0) id = ITEMS_TABLE[i].id;
    if (id == 0) continue;                       // no such klass in the pack
    seams2_reset();
    explore_reset();
    BugInstance pet;
    memset(&pet, 0, sizeof pet);
    pet.magic = (uint16_t)BUG_MAGIC;
    pet.layout_ver = (uint8_t)BUG_LAYOUT_VER;
    pet.species_id = 1; pet.id = 0x5EED1000u + a; pet.level = 5;
    for (uint8_t i = 0; i < (uint8_t)ER_CARE_COUNT; ++i) pet.care[i] = 0;
    g_active_p = &pet;
    CHECK_EQ(inv_add(g_inv, id, 1), 1);
    care_enter();
    while (care_cursor() != (uint8_t)CARE_BAG) care_input(GST_TAP_L);
    care_input(GST_TAP_R);                      // into the bag
    CHECK_EQ(care_mode(), (uint8_t)CAREM_BAG);
    g_toast = STR_EMPTY;
    care_input(GST_TAP_R);                      // use the only row
    CHECK(g_toast == kArms[a].a || g_toast == kArms[a].b);
    CHECK(g_toast != (uint16_t)STR_ITEM_USED);   // ...and NOT the old one word
    ++seen;
    care_leave();
  }
  CHECK(seen >= 3u);                             // anti-vacuity on the pack

  // A LEVEL-UP OUTRANKS THE XP THAT CAUSED IT. Enough candy to cross a level
  // boundary must say so; the same candy that does not must say the other
  // thing. Without this the ladder could collapse to one arm and still pass.
  uint8_t candy = 0;
  for (uint8_t i = 0; i < ITEM_COUNT; ++i)
    if (ITEMS_TABLE[i].klass == (uint8_t)ITEM_KLASS_XP_CANDY && candy == 0)
      candy = ITEMS_TABLE[i].id;
  if (candy != 0) {
    seams2_reset();
    explore_reset();
    BugInstance pet;
    memset(&pet, 0, sizeof pet);
    pet.magic = (uint16_t)BUG_MAGIC;
    pet.layout_ver = (uint8_t)BUG_LAYOUT_VER;
    pet.species_id = 1; pet.id = 0x5EED2000u; pet.level = 1; pet.xp = 0;
    g_active_p = &pet;
    CHECK_EQ(inv_add(g_inv, candy, 1), 1);
    care_enter();
    while (care_cursor() != (uint8_t)CARE_BAG) care_input(GST_TAP_L);
    care_input(GST_TAP_R);
    g_toast = STR_EMPTY;
    care_input(GST_TAP_R);
    // A level-1 Bug handed a candy either levels or does not; whichever it
    // is, the toast must be the one that matches what the pack recorded.
    CHECK(g_toast == (uint16_t)STR_ITEM_LEVELED || g_toast == (uint16_t)STR_ITEM_XP);
    CHECK_EQ(g_toast == (uint16_t)STR_ITEM_LEVELED, pet.level > 1u);
    care_leave();
  }
}

// AND THE BAG ANSWERS BOTH-BUTTONS, which it was the one list in the product
// not to do - on the screen holding items the game describes nowhere else.
TEST(the_bag_answers_both_buttons_like_every_other_list_in_the_product) {
  seams2_reset();
  explore_reset();
  care_enter();
  while (care_cursor() != (uint8_t)CARE_BAG) care_input(GST_TAP_L);
  g_help = STR_EMPTY;
  care_input(GST_BOTH);
  const uint16_t on_the_row = g_help;
  CHECK(on_the_row != (uint16_t)STR_EMPTY);      // the row that opens it: covered
  care_input(GST_TAP_R);
  CHECK_EQ(care_mode(), (uint8_t)CAREM_BAG);
  g_help = STR_EMPTY;
  care_input(GST_BOTH);
  CHECK_EQ(g_help, (uint16_t)STR_HLP_BAG);       // ...and INSIDE it, which was 0
  care_leave();
}

TEST(the_bag_lists_what_is_held_uses_one_and_walks_back_out) {
  seams2_reset();
  explore_reset();
  care_enter();
  CHECK_EQ(care_mode(), (uint8_t)CAREM_LIST);

  // Empty is an ordinary state and draws a line saying so.
  CHECK_EQ(care_bag_rows(), 0);
  while (care_cursor() != (uint8_t)CARE_BAG) care_input(GST_TAP_L);
  care_input(GST_TAP_R);
  CHECK_EQ(care_mode(), (uint8_t)CAREM_BAG);
  snapshot(SCR_CARE, "care_bag_empty");
  care_input(GST_HOLD_R);
  CHECK_EQ(care_mode(), (uint8_t)CAREM_LIST);

  // Two kinds in the bag, one of them a care item the active Bug needs.
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

  BugInstance pet;
  memset(&pet, 0, sizeof pet);
  pet.magic = (uint16_t)BUG_MAGIC;
  pet.layout_ver = (uint8_t)BUG_LAYOUT_VER;
  pet.species_id = 1; pet.id = 0x5EED0009u; pet.level = 5;
  for (uint8_t i = 0; i < (uint8_t)ER_CARE_COUNT; ++i) pet.care[i] = 0;
  g_active_p = &pet;

  care_input(GST_TAP_R);                       // open the bag again
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
  care_input(GST_TAP_R);
  // P10-C6: the toast now says WHAT HAPPENED. This is a CARE item on a Bug
  // whose bars are all empty, so the reaction is the one for a bar that moved.
  CHECK_EQ(g_toast, (uint16_t)STR_ITEM_FED);
  CHECK_EQ(inv_count(g_inv, care_id), 1);
  CHECK(pet.care[0] > 0);
  CHECK(g_saves > 0);

  // A CAPTURE item refuses BY NAME from a menu and is not consumed: it is the
  // one item a player could otherwise throw away by accident.
  while (care_bag_cursor() != cap_row) care_input(GST_TAP_L);
  care_input(GST_TAP_R);
  CHECK_EQ(g_toast, (uint16_t)STR_ITEM_NOT_HERE);
  CHECK_EQ(inv_count(g_inv, cap_id), 1);

  // The last row is the way out of the mode, not out of the screen.
  const int backs = g_backs;
  while (care_bag_cursor() != care_bag_rows()) care_input(GST_TAP_L);
  care_input(GST_TAP_R);
  CHECK_EQ(care_mode(), (uint8_t)CAREM_LIST);
  CHECK_EQ(g_backs, backs);
  g_active_p = nullptr;
}

TEST(an_item_used_with_no_active_bug_is_refused_by_name_and_kept) {
  seams2_reset();
  explore_reset();
  g_active_p = nullptr;
  const uint8_t candy = 1;
  CHECK_EQ(item_get(candy)->klass, (uint8_t)ITEM_KLASS_XP_CANDY);
  CHECK_EQ(inv_add(g_inv, candy, 1), 1);

  care_enter();
  while (care_cursor() != (uint8_t)CARE_BAG) care_input(GST_TAP_L);
  care_input(GST_TAP_R);
  CHECK_EQ(care_mode(), (uint8_t)CAREM_BAG);
  care_input(GST_TAP_R);
  CHECK_EQ(g_toast, (uint16_t)STR_ITEM_NO_PET);
  CHECK_EQ(inv_count(g_inv, candy), 1);
}

// =============================================================================
//  THE SPEC SECTION 63 AUDIT, AS A COMPLETENESS CLAIM (P10-C4)
//
//  Everything above this line is a snapshot somebody wrote when they added a
//  screen. That is a good instrument and it has caught real defects, but it
//  cannot make the claim section 63 actually asks for, because it is a list of
//  what was REMEMBERED. The survey that opened this chunk measured the gap:
//  nineteen screen files, sixty-five goldens, and of the eight screens that
//  can show a twelve-character name only TWO had a fixture that did; the Box
//  had never once been drawn full; and the longest Spanish string in the table
//  was driven by nothing at all.
//
//  So the audit is a TABLE with one row per ScreenId and a compile-time
//  assertion that the table is exactly SCR_COUNT long. A screen added to the
//  enum without a fixture here does not quietly go untested - the BUILD fails,
//  and it fails naming what is missing. That is the difference between a
//  failure and a silence, and it is the whole point of this block.
//
//  WHAT "WORST CASE" MEANS HERE, and every part of it is a number the product
//  really allows:
//    * a nickname of NAME_MAX_LEN characters, ALL of them multi-byte. Twelve
//      stored bytes, twenty-four drawn ones - the widest a name can be - and it
//      is built by running the REAL Latin-1 -> UTF-8 crossing (core/utf8.h)
//      rather than by typing UTF-8 into a fixture, so the conversion is under
//      the audit too.
//    * level 30 and every meter at 100: two digits and three-digit percentages
//      everywhere they can appear.
//    * a FULL Box - ten of ten, every slot named the same way at level 30 -
//      which nothing in this repository had ever rendered.
//    * the widest string core/strings_es.h holds, through the one path that can
//      show an arbitrary one: the banner (the toast line and the HELP strip).
//
//  AND EVERY ROW IS PINNED TO ITS RENDER HOOK BY IDENTITY. The screen table is
//  positional and P10-C4 INSERTED two ids in the middle of the enum; a row that
//  did not move with it would draw the wrong screen under the right name and
//  the count would still match. tests/test_statemachine.cpp makes the same
//  check for the same reason - two instruments, because this one also has to
//  say WHICH fixture belongs to which row.
// =============================================================================

// Twelve Latin-1 characters, every one of them two bytes once drawn. These are
// exactly the high bytes game/validate.cpp's creator_name_char_ok() admits.
#define AUDIT_NAME_L1 "\xD1\xC1\xC9\xCD\xD3\xDA\xDC\xD1\xC1\xC9\xCD\xD3"

static void audit_full_box(void) {
  memset(&g_gs, 0, sizeof g_gs);
  box_bind(g_gs);
  Genome gen;
  memset(&gen, 0, sizeof gen);
  gen.magic_ver  = GENOME_MAGIC_VER;
  gen.lineage_id = 0x0BADF00Du;
  gen.g0 = 0x1234u; gen.g1 = 0x5678u; gen.g2 = 0x9ABCu;
  gen.generation = 3;
  for (uint8_t i = 0; i < (uint8_t)BOX_SLOTS; ++i) {
    // Different species per slot, so the list is not ten copies of one row and
    // the badge column varies with it.
    const uint8_t sp = (uint8_t)(1u + ((uint16_t)i * 6u) % (uint16_t)SPECIES_TABLE_COUNT);
    const uint8_t slot = box_new_bug(sp, 30u, ORIGIN_STARTER, gen,
                                        0xC0FFEEu + i, 1000u);
    CHECK(slot != BOX_SLOT_NONE);
    BugInstance* p = box_slot(slot);
    if (!p) continue;
    for (uint8_t c = 0; c < ER_CARE_COUNT; ++c) p->care[c] = (int32_t)ER_CARE_MILLI_MAX;
    p->hp_cur = 250u;
    // The STORED form: raw Latin-1, exactly as networking/creator_parse.cpp
    // writes one and as game/validate.cpp accepts one.
    memcpy(p->nickname, AUDIT_NAME_L1, sizeof AUDIT_NAME_L1);
  }
  CHECK_EQ(box_count(), (uint8_t)BOX_SLOTS);
  CHECK(box_set_active(0));
}

// The model every audited screen is drawn against.
static void audit_model(void) {
  seams2_reset();
  explore_reset();
  fixture_maxed();
  // THE NAME GOES THROUGH THE REAL CROSSING. A fixture that typed the UTF-8
  // in by hand would prove the LAYOUT and say nothing about the conversion,
  // which is the half that was wrong.
  const uint16_t n = u8_from_latin1(g_view.name, (uint16_t)sizeof g_view.name,
                                    AUDIT_NAME_L1);
  CHECK_EQ((int)n, 24);                       // 12 characters, 24 bytes
  CHECK_EQ((int)u8_count(g_view.name), 12);
  audit_full_box();
  // WHAT ui_nav_reset() DOES ON EVERY SCREEN CHANGE, and the fixture has to do
  // it for the same reason: gfx_list()'s highlight SLIDES from wherever the
  // last list left it, and that anchor is file-scope in ui/gfx_widgets.cpp. A
  // golden recorded without this is a picture of whichever test ran before it.
  gfx_list_reset();
}

// --- the per-row extras ------------------------------------------------------
static void au_none(void)     { }
static void au_menu(void)     { menu_enter(); }
static void au_care(void)     { care_enter(); }
static void au_play(void)     { play_enter(); }
static void au_box(void)      { box_enter(); }
static void au_status_a(void) { status_a_enter(); }
static void au_status_b(void) { status_b_enter(); }
static void au_network(void)  { network_enter(); }
static void au_creator(void)  { g_ap_up = 1; creator_enter(); }
static void au_settings(void) { settings_enter(); }
static void au_time(void)     { time_enter(); }

static void au_link(void) {
  lf_reset();
  lf_set_pet_name("BOLOTA");
  g_now = 1000;
  link_enter();
  // A PEER NAMED IN LATIN-1, which is what networking/discovery.cpp's name_ok()
  // accepts off the air. This is the second half of the name story: the Box
  // holds one form of it and the radio hands over another.
  for (uint8_t i = 0; i < (uint8_t)LINK_PEER_HITS_MIN; ++i) {
    lf_push_beacon(0x2001u, AUDIT_NAME_L1, (uint16_t)DISC_CAP_BATTLE, -42, 0);
    g_now += 600;
    link_update(g_now);
  }
}

static void au_setup_name(void) {
  setup_name_enter();
  // Every cell driven to the WIDEST character the ring offers, which is one of
  // the accented ones - so the field is twelve two-byte characters.
  for (uint8_t cell = 0; cell < (uint8_t)NAME_MAX_LEN; ++cell) {
    for (uint8_t k = 0; k < 28u; ++k) setup_name_input(GST_HOLD_R);  // ... up to N-tilde
    setup_name_input(GST_TAP_L);
  }
}
static void au_setup_pick(void) { setup_pick_enter(); }

static void au_encounter(void) {
  EncounterResult r;
  memset(&r, 0, sizeof r);
  r.outcome = (uint8_t)ENC_OUT_WILD; r.species_id = 1; r.level = 30;
  encounter_arm(r, (uint8_t)NET_CAT_HOME);
  encounter_enter();
}

static void au_capture(void) {
  EncounterResult r;
  memset(&r, 0, sizeof r);
  r.outcome = (uint8_t)ENC_OUT_WILD; r.species_id = 1; r.level = 30;
  encounter_arm(r, (uint8_t)NET_CAT_HOME);
  inv_add(g_inv, 5u, 3u);
  g_roll = 0x1000u;
  g_dev  = 0xABCD0000u;
  capture_enter();
}

static void au_battle(void) {
  battle_arm(BT_ENTRY_PRACTICE, 0xB0A71E02u);
  battle_enter();
  battle_pick_team((uint8_t)BATTLE_TEAM_MAX);
  battle_input(GST_TAP_R);                    // past the stare-down
}

static void au_evolution(void) {
  g_view.stage = STAGE_EGG;
  g_view.age_s = 60;
  evo_enter();
}

static void au_error(void)    { err_set_kind(ERRK_SAVE_CORRUPT); err_enter(); }

struct AuditRow {
  uint8_t     id;
  const char* name;
  void      (*render)(void);
  void      (*setup)(void);
};

static const AuditRow kAudit[] = {
  { SCR_BOOT,          "BOOT",          boot_render,       au_none      },
  { SCR_LOAD_SAVE,     "LOAD_SAVE",     load_save_render,  au_none      },
  { SCR_HOME,          "HOME",          home_render,       au_none      },
  { SCR_MENU,          "MENU",          menu_render,       au_menu      },
  { SCR_CARE,          "CARE",          care_render,       au_care      },
  { SCR_PLAY,          "PLAY",          play_render,       au_play      },
  // GAME's row is five forwarders into ui.cpp, which this binary stubs (the
  // minigame frames have their own snapshots above, drawn through the registry
  // rather than through the row). The row is audited for its IDENTITY and its
  // flags; there is nothing else here that could clip.
  { SCR_GAME,          "GAME",          ui_game_render,    au_none      },
  { SCR_BOX,           "BOX",           box_render,        au_box       },
  { SCR_STATUS,        "STATUS",        status_a_render,   au_status_a  },
  { SCR_STATUS_B,      "STATUS_B",      status_b_render,   au_status_b  },
  { SCR_NETWORK,       "NETWORK",       network_render,    au_network   },
  { SCR_LINK,          "LINK",          link_render,       au_link      },
  { SCR_CREATOR,       "CREATOR",       creator_render,    au_creator   },
  { SCR_SETTINGS,      "SETTINGS",      settings_render,   au_settings  },
  { SCR_TIME,          "TIME",          time_render,       au_time      },
  { SCR_SETUP_NAME,    "SETUP_NAME",    setup_name_render, au_setup_name},
  { SCR_SETUP_STARTER, "SETUP_STARTER", setup_pick_render, au_setup_pick},
  { SCR_CONFIRM,       "CONFIRM",       soon_generic,      au_none      },
  { SCR_ALERT,         "ALERT",         soon_generic,      au_none      },
  { SCR_ENCOUNTER,     "ENCOUNTER",     encounter_render,  au_encounter },
  { SCR_CAPTURE,       "CAPTURE",       capture_render,    au_capture   },
  { SCR_BATTLE,        "BATTLE",        battle_render,     au_battle    },
  { SCR_TRADE,         "TRADE",         soon_trade,        au_none      },
  { SCR_BREED,         "BREED",         soon_breed,        au_none      },
  { SCR_EVOLUTION,     "EVOLUTION",     evo_render,        au_evolution },
  { SCR_ITEM_REWARD,   "ITEM_REWARD",   soon_item_reward,  au_none      },
  { SCR_ERROR,         "ERROR",         err_render,        au_error     },
  { SCR_SLEEP,         "SLEEP",         soon_sleep,        au_none      },
  { SCR_DIAG,          "DIAG",          diag_render,       au_none      },
};

// THE LINE THAT MAKES THIS A COMPLETENESS CLAIM. Add a ScreenId and this fails
// to COMPILE, by name, before any test runs. A screen with no fixture is a
// build error and not a silence, which is the whole difference between an audit
// and a list of what somebody remembered.
static_assert(sizeof kAudit / sizeof kAudit[0] == (size_t)SCR_COUNT,
              "spec section 63: every ScreenId needs a row in kAudit[]. A screen "
              "was added to the enum without a worst-case fixture, so it would "
              "have shipped without ever being drawn at 12-character names, "
              "level 30, a full Box and the longest Spanish string.");

TEST(every_screen_is_audited_at_the_worst_case_the_product_allows) {
  uint32_t drawn = 0;
  for (uint8_t i = 0; i < (uint8_t)SCR_COUNT; ++i) {
    const AuditRow& r = kAudit[i];
    // 1. the table is in ScreenId order and the row really is that screen.
    if (r.id != i) fprintf(stderr, "  kAudit row %u claims id %u (%s)\n",
                           (unsigned)i, (unsigned)r.id, r.name);
    CHECK_EQ((int)r.id, (int)i);
    const ScreenDef* d = screen_def(i);
    CHECK(d != nullptr);
    if (!d) continue;
    if (d->render != r.render)
      fprintf(stderr, "  the screen table's row %u is not %s\n", (unsigned)i, r.name);
    CHECK(d->render == r.render);

    // 2. the worst-case model, then whatever this screen needs on top of it.
    audit_model();
    r.setup();

    // 3. draw it and hold it to every rule snapshot_fn() holds a golden to,
    //    minus the golden itself - there is no picture to approve here, the
    //    claim is that nothing overflows, nothing allocates and nothing hands
    //    the font a sequence it cannot decode.
    fb_reset();
    const uint32_t allocs_before = alloc_count();
    d->render();
    const uint32_t allocs = alloc_count() - allocs_before;

    if (fb_oob() != 0)
      fprintf(stderr, "  %s at the worst case: %u out-of-bounds primitive(s), "
                      "first %s\n", r.name, (unsigned)fb_oob(), fb_oob_first());
    CHECK_EQ(fb_oob(), 0u);

    if (fb_bad_utf8() != 0)
      fprintf(stderr, "  %s at the worst case: %u malformed UTF-8 string(s), "
                      "first \"%s\"\n", r.name, (unsigned)fb_bad_utf8(),
              fb_bad_utf8_first());
    CHECK_EQ(fb_bad_utf8(), 0u);

    if (allocs != 0u)
      fprintf(stderr, "  %s at the worst case: %u allocation(s)\n",
              r.name, (unsigned)allocs);
    CHECK_EQ(allocs, 0u);

    if (fb_pixels() > SNAP_MAX_PIXELS || fb_ops() > SNAP_MAX_OPS)
      fprintf(stderr, "  %s at the worst case: %u pixels / %u primitives\n",
              r.name, (unsigned)fb_pixels(), (unsigned)fb_ops());
    CHECK(fb_pixels() <= SNAP_MAX_PIXELS);
    CHECK(fb_ops() <= SNAP_MAX_OPS);

    // 4. ANTI-VACUITY. A screen that drew nothing at all would satisfy every
    //    rule above. GAME is the one honest exception - its row forwards into
    //    ui.cpp, which this binary stubs - and it is named rather than skipped.
    int ink = 0;
    for (int y = 0; y < FB_H; ++y)
      for (int x = 0; x < FB_W; ++x) ink += fb_get(x, y) ? 1 : 0;
    if (i != (uint8_t)SCR_GAME) {
      if (ink == 0) fprintf(stderr, "  %s drew NOTHING at the worst case\n", r.name);
      CHECK(ink > 0);
      ++drawn;
    }
  }
  CHECK_EQ((int)drawn, (int)SCR_COUNT - 1);
}

// =============================================================================
//  THE LONGEST SPANISH STRING (P10-C4, spec sections 63 and 65)
//
//  Section 63 asks for the longest Spanish string to be driven at 128x64 and
//  nothing in this repository had ever driven it. The survey measured the
//  reason it mattered: TWENTY-THREE entries in core/strings_es.h are wider than
//  the panel at GF_BODY, and TEN of them reach a draw with no fit and no wrap -
//  six through the HELP strip and four through the toast line.
//
//  THE SWEEP IS OVER THE WHOLE TABLE AND NOT OVER A LIST OF THE TEN. A list of
//  which ids reach which draw is exactly the thing that rots: a new toast in a
//  new screen would not be in it. Every string in the product has to be
//  showable, so every string in the product is what is checked.
// =============================================================================
static uint16_t widest_str_id(void) {
  uint16_t worst = STR_EMPTY, w = 0;
  for (uint16_t id = 0; id < (uint16_t)STR_COUNT; ++id) {
    const uint16_t sw = gfx_text_w(GF_BODY, S(id));
    if (sw > w) { w = sw; worst = id; }
  }
  return worst;
}

TEST(every_string_in_the_product_fits_a_banner_and_the_banner_is_the_size_it_used) {
  uint32_t multiline = 0, widest = 0;
  uint16_t widest_id = STR_EMPTY;

  for (uint16_t id = 0; id < (uint16_t)STR_COUNT; ++id) {
    const char* s = S(id);
    if (s == nullptr || *s == '\0') continue;
    const uint16_t w = gfx_text_w(GF_BODY, s);
    if (w > widest) { widest = w; widest_id = id; }

    // (a) HOW MANY LINES THE REAL WRAP NEEDS, measured by running it with room
    //     to spare rather than by trusting the estimate that sizes the slab.
    fb_reset();
    const uint8_t need = gfx_text_wrap(GF_BODY, GFX_BANNER_PAD_X, 8,
                                       (int16_t)GFX_BANNER_INNER_W, GFX_LINE_BODY,
                                       (uint8_t)(GFX_BANNER_MAX_LINES + 2), s);
    if (need > (uint8_t)GFX_BANNER_MAX_LINES)
      fprintf(stderr, "  string %u (%u px) needs %u wrapped lines and a banner "
                      "holds %u: \"%s\"\n",
              (unsigned)id, (unsigned)w, (unsigned)need,
              (unsigned)GFX_BANNER_MAX_LINES, s);
    CHECK(need <= (uint8_t)GFX_BANNER_MAX_LINES);

    // (b) THE SLAB IS EXACTLY AS TALL AS THE TEXT TURNED OUT TO BE. Under-size
    //     and the wrap silently drops the tail; over-size and the banner eats
    //     rows of the screen underneath for a line it never draws.
    fb_reset();
    const uint8_t sized = gfx_banner_lines(s);
    const uint8_t drew  = gfx_banner(s);
    if (sized != need || drew != need)
      fprintf(stderr, "  string %u: wrap needs %u, banner sized %u, drew %u: \"%s\"\n",
              (unsigned)id, (unsigned)need, (unsigned)sized, (unsigned)drew, s);
    CHECK_EQ((int)sized, (int)need);
    CHECK_EQ((int)drew,  (int)need);

    // (c) and it stayed on the panel and stayed decodable.
    if (fb_oob() != 0)
      fprintf(stderr, "  string %u drew off the panel (%s): \"%s\"\n",
              (unsigned)id, fb_oob_first(), s);
    CHECK_EQ(fb_oob(), 0u);
    CHECK_EQ(fb_bad_utf8(), 0u);

    // (d) the banner never touches the affordance strip and never leaves the
    //     panel at the top, whatever it is asked to hold.
    for (int x = 0; x < FB_W; ++x)
      CHECK_EQ(fb_get(x, UI_AFFORD_Y), 0);

    if (need > 1u) ++multiline;
  }

  // ANTI-VACUITY, AND IT IS THE MEASUREMENT THAT MOTIVATED THE WHOLE CHANGE:
  // strings that need more than one line have to EXIST, or this sweep is a
  // sweep over a table that always fitted. The survey counted 23 over 128 px.
  if (multiline < 10u)
    fprintf(stderr, "  only %u strings need a second banner line - the sweep is "
                    "not exercising the wrap\n", (unsigned)multiline);
  CHECK(multiline >= 10u);
  CHECK_EQ((int)widest_id, (int)widest_str_id());
  CHECK(widest > (uint16_t)OLED_W);        // the widest really is off-panel
}

// THE HELP OVERLAY, id by id, through the REAL ui/dialog.cpp. This is the draw
// the survey measured six overflows on - each of them one GST_BOTH press away
// on a shipping screen - and snapshot_help covered exactly one of the
// twenty-five, the one that happened to fit.
TEST(every_help_line_the_product_can_open_stays_on_the_panel) {
  uint32_t opened = 0;
  for (uint16_t id = 1; id < (uint16_t)STR_COUNT; ++id) {
    if (S(id) == nullptr || S(id)[0] == '\0') continue;
    seams2_reset();
    dialog_open_help(id);
    CHECK_EQ(dialog_modal(), (uint8_t)MODAL_HELP);
    fb_reset();
    dialog_render();
    if (fb_oob() != 0)
      fprintf(stderr, "  HELP %u drew off the panel (%s): \"%s\"\n",
              (unsigned)id, fb_oob_first(), S(id));
    CHECK_EQ(fb_oob(), 0u);
    CHECK_EQ(fb_bad_utf8(), 0u);
    // It drew SOMETHING: an overlay that vanished would also not clip.
    int ink = 0;
    for (int y = 0; y < FB_H; ++y)
      for (int x = 0; x < FB_W; ++x) ink += fb_get(x, y) ? 1 : 0;
    CHECK(ink > 0);
    ++opened;
  }
  CHECK(opened > 300u);                    // the table really was walked
}

// =============================================================================
//  A FULL BOX (P10-C4)
//
//  Ten of ten, every slot named with twelve two-byte characters at level 30,
//  swept across every cursor position so the scrolled window AND the scrollbar
//  are both drawn. The survey found this had never been rendered: box_list is a
//  3/10 Box and the scrollbar geometry was pinned by nothing.
// =============================================================================
TEST(a_full_box_draws_at_every_cursor_position) {
  audit_model();
  box_enter();
  // BOX_SLOTS rows plus the BACK row.
  for (uint8_t pos = 0; pos <= (uint8_t)BOX_SLOTS; ++pos) {
    fb_reset();
    box_render();
    if (fb_oob() != 0)
      fprintf(stderr, "  full BOX at cursor %u: %u OOB, first %s\n",
              (unsigned)pos, (unsigned)fb_oob(), fb_oob_first());
    CHECK_EQ(fb_oob(), 0u);
    CHECK_EQ(fb_bad_utf8(), 0u);
    box_input(GST_TAP_L);                  // A steps the cursor
  }
  // and it came back round to the top, so the sweep really covered the list.
  fb_reset();
  box_render();
  CHECK_EQ(fb_oob(), 0u);
}

TEST(snapshot_box_full) {
  audit_model();
  box_enter();
  // Park the cursor on the last Bug so the window has SCROLLED and the
  // scrollbar is at the bottom of its track - the state no golden had.
  for (uint8_t i = 0; i < (uint8_t)(BOX_SLOTS - 1u); ++i) box_input(GST_TAP_L);
  // LET THE HIGHLIGHT SETTLE ON THE ROW IT WAS SENT TO. cursor_y() retargets on
  // the frame it first sees a new selection and returns the OLD position on
  // that frame, so one render is needed to aim and the clock has to pass
  // UI_LIST_SLIDE_MS for it to arrive. Found by this golden passing alone and
  // failing in the suite - the slide anchor is file-scope in gfx_widgets.cpp.
  box_render();
  g_now += (uint32_t)UI_LIST_SLIDE_MS * 4u;
  snapshot(SCR_BOX, "box_list_full");
}

// =============================================================================
//  FIRST BOOT ON THE PANEL (P10-C4)
//
//  app/onboarding.h owns WHICH question comes next and tests/test_onboarding.cpp
//  drives that through the real save pipeline. What is here is the other half:
//  the two screens, their grammar, and the promise that a player who reads
//  nothing still ends up with a working device.
// =============================================================================
static void flow_begin(uint8_t step) {
  seams2_reset();
  audit_full_box();                    // app.cpp has already minted a starter
  ob_set_step(g_cfg, step);
  CHECK(setup_in_flow());
}

TEST(the_naming_screen_types_a_name_one_character_at_a_time) {
  flow_begin(OB_NAME);
  setup_name_enter();
  CHECK_EQ((int)setup_name_cursor(), 0);
  CHECK_EQ((int)setup_name_text()[0], 0);          // an empty field, not spaces

  // R steps the ring; the first stop past the blank is 'A'.
  setup_name_input(GST_HOLD_R);
  CHECK_EQ((int)setup_name_text()[0], (int)'A');
  CHECK_EQ((int)setup_name_cursor(), 0);           // and R does not move on

  // A moves on, R types again.
  setup_name_input(GST_TAP_L);
  CHECK_EQ((int)setup_name_cursor(), 1);
  for (int i = 0; i < 2; ++i) setup_name_input(GST_HOLD_R);
  CHECK_EQ(strcmp(setup_name_text(), "AB"), 0);

  // The ring wraps, and it wraps back to the blank rather than to 'A' - which
  // is what lets a player undo a character without walking the whole alphabet
  // twice.
  setup_name_enter();
  const uint8_t n = setup_ring_len();
  for (uint8_t i = 0; i < n; ++i) setup_name_input(GST_HOLD_R);
  CHECK_EQ((int)setup_name_text()[0], 0);          // back to blank
  CHECK_EQ((int)setup_ring_at(0), (int)' ');

  // The cursor wraps too, so twelve taps of A return to the first cell.
  setup_name_enter();
  for (uint8_t i = 0; i < (uint8_t)NAME_MAX_LEN; ++i) setup_name_input(GST_TAP_L);
  CHECK_EQ((int)setup_name_cursor(), 0);
}

// THE RING CARRIES THE SPANISH REPERTOIRE, and the name it produces is stored
// as LATIN-1 - which is the whole reason core/utf8.h exists.
TEST(a_name_typed_on_the_device_is_latin1_and_is_drawn_as_utf8) {
  flow_begin(OB_NAME);
  setup_name_enter();
  // Walk to the n-tilde and take it.
  uint8_t taps = 0;
  while (setup_ring_at(taps) != (char)0xD1) {
    ++taps;
    CHECK(taps < setup_ring_len());
  }
  for (uint8_t i = 0; i < taps; ++i) setup_name_input(GST_HOLD_R);
  CHECK_EQ((int)(uint8_t)setup_name_text()[0], 0xD1);
  CHECK_EQ((int)strlen(setup_name_text()), 1);     // ONE stored byte

  setup_name_input(GST_HOLD_L);                    // accept
  CHECK_EQ((int)(uint8_t)g_cfg.pet_name[0], 0xD1); // ...stored as Latin-1
  CHECK_EQ((int)strlen(g_cfg.pet_name), 1);

  // And what reaches the panel is the two-byte UTF-8 form of the same
  // character. Drawn through the real screen, not through a fixture.
  fb_reset();
  setup_name_enter();
  setup_name_render();
  CHECK_EQ(fb_bad_utf8(), 0u);
  CHECK_EQ(fb_oob(), 0u);
  // ...and re-entering the screen finds the character it stored.
  CHECK_EQ((int)(uint8_t)setup_name_text()[0], 0xD1);
}

// A name is TRIMMED at both ends. game/validate.h argues at length against
// mending a name, and that argument is about a name arriving from OUTSIDE the
// device; this one is typed on it, by a player who cannot see a trailing space.
TEST(a_typed_name_is_trimmed_at_both_ends) {
  flow_begin(OB_NAME);
  setup_name_enter();
  setup_name_input(GST_TAP_L);                     // leave cell 0 blank
  setup_name_input(GST_HOLD_R);                     // 'A' in cell 1
  setup_name_input(GST_TAP_L);
  setup_name_input(GST_TAP_L);                     // cell 3, left blank
  CHECK_EQ(strcmp(setup_name_text(), "A"), 0);
  setup_name_input(GST_HOLD_L);
  CHECK_EQ(strcmp(g_cfg.pet_name, "A"), 0);
}

TEST(the_flow_walks_starter_name_time_and_re_roots_at_every_step) {
  // STARTER -> NAME, and the chosen species really is handed over. The pick is
  // FIRST now: naming the device before the player has met the creature meant
  // typing a name for nothing in particular, and then being shown three bugs
  // one of which was suddenly called that.
  flow_begin(OB_STARTER);
  setup_pick_enter();
  CHECK_EQ((int)setup_pick_cursor(), 0);
  setup_pick_input(GST_HOLD_R);
  CHECK_EQ((int)setup_pick_cursor(), 1);
  setup_pick_input(GST_HOLD_L);
  CHECK_EQ((int)g_starter_calls, 1);
  CHECK_EQ((int)g_starter, (int)ob_starter_species(1));
  CHECK_EQ((int)ob_step(g_cfg), (int)OB_NAME);
  CHECK_EQ((int)g_root, (int)SCR_SETUP_NAME);      // RE-ROOTED, not pushed
  CHECK_EQ((int)g_push, 0xFF);
  CHECK_EQ(g_backs, 0);
  CHECK(g_cfg_silent > 0);                         // and persisted, with no toast
  CHECK_EQ((int)g_toast, (int)STR_EMPTY);

  // NAME -> TIME
  flow_begin(OB_NAME);
  setup_name_enter();
  setup_name_input(GST_HOLD_R);
  setup_name_input(GST_HOLD_L);
  CHECK_EQ((int)ob_step(g_cfg), (int)OB_TIME);
  CHECK_EQ((int)g_root, (int)SCR_TIME);
  CHECK_EQ(g_backs, 0);
  CHECK_EQ((int)g_toast, (int)STR_EMPTY);

  // TIME -> HOME, through the P2-C6 screen, unchanged except for the branch.
  flow_begin(OB_TIME);
  time_enter();
  g_clock_ok = true;
  time_input(GST_HOLD_L);                          // commit the date
  CHECK_EQ((int)ob_step(g_cfg), (int)OB_DONE);
  CHECK_EQ((int)g_root, (int)SCR_HOME);
  CHECK_EQ(g_backs, 0);                            // NOT ui_back(): it would
                                                   // return to the naming step
  CHECK_EQ((int)g_toast, (int)STR_SU_DONE);
  CHECK(!setup_in_flow());
}

// THE REQUIREMENT, ON THE PANEL: a player who reads nothing still lands on a
// working device. Two ways out of every step, neither of which destroys
// anything, because every default is already in place before the question is
// asked.
TEST(a_player_who_reads_nothing_still_reaches_a_playable_device) {
  // (a) SKIP each step in turn: the flow still ends, and ends finished.
  static const uint8_t kSteps[3] = { OB_NAME, OB_TIME, OB_STARTER };
  for (uint8_t i = 0; i < 3u; ++i) {
    flow_begin(kSteps[i]);
    switch (kSteps[i]) {
      case OB_NAME:    setup_name_enter(); setup_name_input(GST_BOTH); break;
      case OB_TIME:    time_enter();       time_input(GST_BOTH);       break;
      default:         setup_pick_enter(); setup_pick_input(GST_BOTH); break;
    }
    CHECK_EQ((int)ob_step(g_cfg), (int)ob_next(kSteps[i]));
    CHECK_EQ((int)g_root, (int)ob_screen_for(ob_next(kSteps[i])));
    // Nothing was destroyed on the way past: no name written, no starter
    // rerolled, and the Box is still ten Bugs.
    CHECK_EQ((int)g_cfg.pet_name[0], 0);
    CHECK_EQ((int)g_starter_calls, 0);
    CHECK_EQ(box_count(), (uint8_t)BOX_SLOTS);
  }

  // (b) HOLD BOTH on any step ends the whole flow at once - the one gesture a
  //     player who is reading nothing will find, because invariant 2 makes it
  //     mean HOME everywhere else in the product.
  for (uint8_t i = 0; i < 3u; ++i) {
    flow_begin(kSteps[i]);
    switch (kSteps[i]) {
      case OB_NAME:    setup_name_enter(); setup_name_input(GST_LONG_BOTH); break;
      case OB_TIME:    time_enter();       time_input(GST_LONG_BOTH);       break;
      default:         setup_pick_enter(); setup_pick_input(GST_LONG_BOTH); break;
    }
    CHECK_EQ((int)ob_step(g_cfg), (int)OB_DONE);
    CHECK_EQ((int)g_root, (int)SCR_HOME);
    CHECK(!setup_in_flow());
    CHECK_EQ((int)g_starter_calls, 0);
  }
}

// AND A DEVICE THAT IS ALREADY SET UP NEVER SEES ANY OF IT. The TIME screen is
// the one that can be reached both ways - it is the flow's middle question AND
// an ordinary SETTINGS page - so it is the one where the branch can be wrong.
TEST(a_device_that_is_set_up_gets_the_ordinary_time_screen_back) {
  seams2_reset();
  ob_set_step(g_cfg, OB_DONE);
  CHECK(!setup_in_flow());
  time_enter();
  g_clock_ok = true;
  time_input(GST_HOLD_L);                          // commit
  CHECK_EQ(g_backs, 1);                            // BACK, not forward
  CHECK_EQ((int)g_root, 0xFF);                     // and nothing re-rooted
  CHECK_EQ((int)g_toast, (int)STR_CLK_SAVED);      // and it says so, as always
  CHECK_EQ((int)ob_step(g_cfg), (int)OB_DONE);

  // B is BACK here and SKIP in the flow.
  seams2_reset();
  ob_set_step(g_cfg, OB_DONE);
  time_enter();
  time_input(GST_BOTH);
  CHECK_EQ(g_backs, 1);
  CHECK_EQ((int)g_root, 0xFF);
}

// =============================================================================
//  SPEC SECTION 65: NO TIME-CRITICAL MENUS
//
//  Stated as a property of the whole table rather than as a comment on three
//  rows: any screen that holds something the player is COMPOSING - a date, a
//  name, a choice - must be SF_STICKY, because invariant 3's twenty second
//  auto-return would otherwise throw the work away while they thought about it.
//  The list is named, and the negative half is named too: the screens that are
//  deliberately NOT sticky, so this cannot be satisfied by making everything
//  sticky and calling it accessible.
// =============================================================================
TEST(no_screen_that_holds_the_players_work_can_time_out_from_under_them) {
  static const uint8_t kComposing[] = {
    SCR_TIME,          // five fields of a date
    SCR_SETUP_NAME,    // twelve cells of a name
    SCR_SETUP_STARTER, // a choice that mints a creature
    SCR_GAME,          // a minigame scored in milliseconds
    SCR_BATTLE,        // a round in progress
    SCR_EVOLUTION,     // an egg being rubbed
  };
  for (size_t i = 0; i < sizeof kComposing / sizeof kComposing[0]; ++i) {
    const uint8_t s = kComposing[i];
    if ((SCREENS[s].flags & SF_STICKY) == 0u)
      fprintf(stderr, "  %s holds the player's work and is NOT SF_STICKY\n",
              kAudit[s].name);
    CHECK((SCREENS[s].flags & SF_STICKY) != 0u);
  }

  // The other half: an ordinary list holds nothing and DOES time out, so
  // "sticky" still means something.
  static const uint8_t kOrdinary[] = { SCR_MENU, SCR_PLAY, SCR_STATUS, SCR_BOX };
  for (size_t i = 0; i < sizeof kOrdinary / sizeof kOrdinary[0]; ++i)
    CHECK_EQ((int)(SCREENS[kOrdinary[i]].flags & SF_STICKY), 0);

  // AND NO STEP OF THE FLOW IS TIMED IN ANY OTHER WAY EITHER. The right
  // button's auto-repeat is the only clock any of the three reads, and it only
  // ever makes a HELD button repeat - it can neither expire a choice nor
  // advance a step. Driven: a minute of wall time with no press at all changes
  // nothing on any of the three screens.
  flow_begin(OB_NAME);
  setup_name_enter();
  setup_name_input(GST_HOLD_R);
  const char first = setup_name_text()[0];
  for (uint32_t t = 0; t < 60000u; t += 250u) {
    g_now += 250u;
    setup_name_update(g_now);
    setup_pick_update(g_now);
    time_update(g_now);
  }
  CHECK_EQ((int)setup_name_text()[0], (int)first);
  CHECK_EQ((int)setup_name_cursor(), 0);
  CHECK_EQ((int)setup_pick_cursor(), 0);
  CHECK_EQ((int)ob_step(g_cfg), (int)OB_NAME);     // still the same question
  CHECK_EQ((int)g_root, 0xFF);                     // and nowhere else
}

// =============================================================================
//  THE TWO NEW GOLDENS
// =============================================================================
TEST(snapshot_setup_name) {
  flow_begin(OB_NAME);
  setup_name_enter();
  // "PACO" with an n-tilde on the end, typed the way a player types it: the
  // field carries an accented character AND a blank tail, which is the layout
  // the cursor rule has to survive.
  static const char kWanted[] = { 'P', 'A', 'C', 'O', (char)0xD1, '\0' };
  for (uint8_t i = 0; kWanted[i] != '\0'; ++i) {
    uint8_t taps = 0;
    while (setup_ring_at(taps) != kWanted[i]) { ++taps; CHECK(taps < setup_ring_len()); }
    for (uint8_t k = 0; k < taps; ++k) setup_name_input(GST_HOLD_R);
    setup_name_input(GST_TAP_L);
  }
  CHECK_EQ((int)strlen(setup_name_text()), 5);     // five stored bytes
  snapshot(SCR_SETUP_NAME, "setup_name");
}

TEST(snapshot_setup_starter) {
  flow_begin(OB_STARTER);
  setup_pick_enter();
  setup_pick_input(GST_HOLD_R);                     // the middle of the three
  CHECK_EQ((int)setup_pick_cursor(), 1);
  snapshot(SCR_SETUP_STARTER, "setup_starter");
}

// =============================================================================
//  THE FIRST-BOOT INTRO
//
//  Sixteen seconds of pseudo-C typing itself, a compile that fails, and three
//  bugs climbing out of the failure to stand exactly where the picker draws
//  them. It is a PHASE of SCR_SETUP_STARTER rather than a screen of its own,
//  which is what makes the last claim below checkable at all.
// =============================================================================
static void intro_fixture(void) {
  flow_begin(OB_STARTER);
  setup_pick_enter();
  setup_intro_arm();
}

TEST(the_intro_walks_its_five_beats_and_the_clock_ends_it) {
  intro_fixture();
  CHECK_EQ((int)setup_intro_phase(), (int)SU_IN_TYPE);
  g_now += 5999u;
  CHECK_EQ((int)setup_intro_phase(), (int)SU_IN_TYPE);
  g_now += 2u;                                     // 6001
  CHECK_EQ((int)setup_intro_phase(), (int)SU_IN_BUILD);
  g_now += 3200u;                                  // 9201
  CHECK_EQ((int)setup_intro_phase(), (int)SU_IN_FAIL);
  g_now += 1400u;                                  // 10601
  CHECK_EQ((int)setup_intro_phase(), (int)SU_IN_BUGS);
  g_now += 3300u;                                  // 13901
  CHECK_EQ((int)setup_intro_phase(), (int)SU_IN_HOLD);
  // THE CLOCK ENDS IT. Not one cancel, not one input, not one leave hook.
  g_now += 1600u;                                  // 15501
  CHECK_EQ((int)setup_intro_phase(), (int)SU_IN_NONE);
  setup_intro_cancel();
}

// A PRESS SKIPS AND ONLY SKIPS. The impatient player's very first act on the
// device must not be a permanent choice they did not know they were making, and
// it must not spin the cursor either.
TEST(a_press_during_the_intro_skips_it_and_chooses_nothing) {
  static const Gesture kAll[5] = { GST_TAP_L, GST_HOLD_R, GST_HOLD_L,
                                   GST_TAP_R, GST_BOTH };
  for (uint8_t i = 0; i < 5u; ++i) {
    intro_fixture();
    g_now += 2000u;
    CHECK(setup_intro_phase() != (uint8_t)SU_IN_NONE);
    setup_pick_input(kAll[i]);
    CHECK_EQ((int)setup_intro_phase(), (int)SU_IN_NONE);
    CHECK_EQ((int)g_starter_calls, 0);             // nothing was chosen
    CHECK_EQ((int)setup_pick_cursor(), 0);         // and the cursor did not move
    CHECK_EQ((int)ob_step(g_cfg), (int)OB_STARTER);// the flow did not advance
  }
  // ...and the very next press, with the intro gone, does all three.
  setup_pick_input(GST_HOLD_R);
  CHECK_EQ((int)setup_pick_cursor(), 1);
  setup_pick_input(GST_HOLD_L);
  CHECK_EQ((int)g_starter_calls, 1);
}

// THE CLAIM THE WHOLE DESIGN IS FOR: the last frame of the animation is the
// first frame of the screen it hands over to. Both ask pick_geometry() where
// the three bodies stand, so every pixel the intro's last beat lights inside
// the picker's band is lit by the picker too - the only thing that appears at
// the cut is the selection frame around one of them.
//
// Moving either drawing off the shared geometry fails here by name.
TEST(the_last_frame_of_the_intro_is_the_first_frame_of_the_picker) {
  intro_fixture();
  g_now += 14500u;                                 // SU_IN_HOLD
  CHECK_EQ((int)setup_intro_phase(), (int)SU_IN_HOLD);
  fb_reset();
  setup_pick_render();
  uint8_t held[FB_H][FB_W];
  int ink = 0;
  for (int y = 0; y < FB_H; ++y)
    for (int x = 0; x < FB_W; ++x) {
      held[y][x] = (uint8_t)fb_get(x, y);
      if (y >= 12 && y <= 42) ink += held[y][x];   // SU_BAND_TOP..SU_BAND_BOT
    }

  setup_intro_cancel();
  CHECK_EQ((int)setup_intro_phase(), (int)SU_IN_NONE);
  fb_reset();
  setup_pick_render();
  int lost = 0;
  for (int y = 12; y <= 42; ++y)
    for (int x = 0; x < FB_W; ++x)
      if (held[y][x] == 1u && (uint8_t)fb_get(x, y) == 0u) ++lost;

  CHECK_EQ(lost, 0);
  CHECK(ink > 150);                                // anti-vacuity: three bodies
}

// CONTAINMENT, driven every 20 ms of the whole sixteen seconds. Nothing leaves
// the panel, and nothing before the bugs arrive touches the affordance strip -
// which is the row the compile bar and the tear are one arithmetic slip away
// from, and the strip is redrawn by another module on every frame.
TEST(no_frame_of_the_intro_draws_off_the_panel_or_into_the_affordance_strip) {
  intro_fixture();
  const uint32_t t0 = g_now;
  int frames = 0, moved = 0;
  uint8_t prev[FB_H][FB_W];
  memset(prev, 0, sizeof prev);
  for (uint32_t t = 0; t < 15400u; t += 20u) {
    g_now = t0 + t;
    fb_reset();
    setup_pick_render();
    CHECK_EQ(fb_oob(), 0u);
    ++frames;
    if (t < 13900u) {                              // before the affordance is drawn
      for (int y = (int)UI_AFFORD_Y; y < FB_H; ++y)
        for (int x = 0; x < FB_W; ++x)
          if (fb_get(x, y) != 0) {
            fprintf(stderr, "  INTRO t=%u: pixel (%d,%d) is under the "
                            "affordance strip\n", (unsigned)t, x, y);
            CHECK(false);
          }
    }
    for (int y = 0; y < FB_H; ++y)
      for (int x = 0; x < FB_W; ++x) {
        const uint8_t now = (uint8_t)fb_get(x, y);
        if (now != prev[y][x]) ++moved;
        prev[y][x] = now;
      }
  }
  CHECK_EQ(frames, 770);
  CHECK(moved > 2000);                             // anti-vacuity
  setup_intro_cancel();
  g_now = t0;
}

// The four beats, as pictures. The pair that matters is intro_bugs and
// setup_starter: the bodies are in the same place in both.
TEST(snapshot_intro_type) {
  intro_fixture();
  g_now += 3000u;                                  // half the listing typed
  CHECK_EQ((int)setup_intro_phase(), (int)SU_IN_TYPE);
  snapshot(SCR_SETUP_STARTER, "intro_type");
  setup_intro_cancel();
}

TEST(snapshot_intro_build) {
  intro_fixture();
  g_now += 8000u;                                  // the bar most of the way up
  CHECK_EQ((int)setup_intro_phase(), (int)SU_IN_BUILD);
  snapshot(SCR_SETUP_STARTER, "intro_build");
  setup_intro_cancel();
}

TEST(snapshot_intro_fail) {
  intro_fixture();
  g_now += 9900u;
  CHECK_EQ((int)setup_intro_phase(), (int)SU_IN_FAIL);
  snapshot(SCR_SETUP_STARTER, "intro_fail");
  setup_intro_cancel();
}

TEST(snapshot_intro_bugs) {
  intro_fixture();
  g_now += 12400u;                                 // two standing, one climbing
  CHECK_EQ((int)setup_intro_phase(), (int)SU_IN_BUGS);
  snapshot(SCR_SETUP_STARTER, "intro_bugs");
  setup_intro_cancel();
}

// THE HEADER BAR SURVIVES THE THREE BODIES, and this is the P10-C3 seam biting
// on the first screen written after it. An atlas body is 24 rows with an empty
// margin above its ink; standing one on this floor puts that margin inside
// UI_HDR_H, and gfx_xbm() is OPAQUE - it paints the 0-bits in the inverse, so
// the FIRST DRAFT of this screen punched a 24 px hole through the title bar
// under each creature. It is drawn with gfx_xbm_t() now. Stated as its own
// property rather than left to the golden, because a golden recorded from the
// broken draft would have made the hole permanent.
TEST(the_starter_bodies_never_erase_the_title_bar) {
  for (uint8_t i = 0; i < (uint8_t)OB_STARTER_COUNT; ++i) {
    flow_begin(OB_STARTER);
    setup_pick_enter();
    for (uint8_t k = 0; k < i; ++k) setup_pick_input(GST_HOLD_R);
    fb_reset();
    setup_pick_render();
    // The inverted title bar is solid from edge to edge on its last row, which
    // is the row a body box reaches into.
    for (int x = 0; x < FB_W; ++x) {
      if (!fb_get(x, UI_HDR_H - 1))
        fprintf(stderr, "  starter %u: the title bar has a hole at x=%d\n",
                (unsigned)i, x);
      CHECK(fb_get(x, UI_HDR_H - 1) != 0);
    }
    // ANTI-VACUITY: the bar is not solid everywhere - the title is ERASED into
    // it - so "row 10 is solid" is a real constraint and not a tautology about
    // an inverted slab.
    int erased = 0;
    for (int y = 0; y < UI_HDR_H - 1; ++y)
      for (int x = 0; x < FB_W; ++x) if (!fb_get(x, y)) ++erased;
    CHECK(erased > 20);
  }
}

// The three bodies are three DIFFERENT bodies, which a golden of one cursor
// position cannot say and which is the only thing that makes the choice
// visible. Driven over all three, comparing the panel against itself.
TEST(the_three_starters_draw_three_different_creatures) {
  static uint8_t shot[OB_STARTER_COUNT][FB_H][FB_W];
  for (uint8_t i = 0; i < (uint8_t)OB_STARTER_COUNT; ++i) {
    flow_begin(OB_STARTER);
    setup_pick_enter();
    for (uint8_t k = 0; k < i; ++k) setup_pick_input(GST_HOLD_R);
    CHECK_EQ((int)setup_pick_cursor(), (int)i);
    fb_reset();
    setup_pick_render();
    CHECK_EQ(fb_oob(), 0u);
    CHECK_EQ(fb_bad_utf8(), 0u);
    for (int y = 0; y < FB_H; ++y)
      for (int x = 0; x < FB_W; ++x) shot[i][y][x] = (uint8_t)fb_get(x, y);
  }
  // Compare only the BODY band, so the difference is the creatures and not the
  // selection frame or the name line underneath them.
  for (uint8_t a = 0; a < (uint8_t)OB_STARTER_COUNT; ++a) {
    for (uint8_t b = (uint8_t)(a + 1u); b < (uint8_t)OB_STARTER_COUNT; ++b) {
      int diff = 0;
      for (int y = 16; y < 39; ++y)
        for (int x = 0; x < FB_W; ++x) if (shot[a][y][x] != shot[b][y][x]) ++diff;
      if (diff < 20)
        fprintf(stderr, "  starters %u and %u differ by only %d pixels in the "
                        "body band - the choice is not visible\n",
                (unsigned)a, (unsigned)b, diff);
      CHECK(diff >= 20);
    }
  }
}
