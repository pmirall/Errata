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
//  the TIME entry screen. Every later screen adds its fixtures here.
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
#include "fakes/gfx_fb.h"
#include "persistence/save_manager.h"
#include "ui/screen.h"
#include "ui/screen_care.h"
#include "ui/screen_error.h"
#include "ui/screen_home.h"
#include "ui/screen_menu.h"
#include "ui/screen_settings.h"
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
static int      g_repeat   = 0;
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
void ui_repeat_last_action(void)  { g_repeat++; }
void ui_help(uint16_t id)         { g_help = id; }
void ui_confirm_medicine(void)    { g_medicine++; }
void ui_start_minigame(uint8_t i) { g_minigame = i; }
uint8_t ui_god_progress(void)     { return g_god; }
void ui_input_flush(void)         { g_flushes++; }
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
  g_view.xp       = 12;
  g_view.xp_next  = PB_XP_PER_LEVEL_PLACEHOLDER;
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
  g_view.level    = 30;
  g_view.xp       = 99;
  g_view.xp_next  = PB_XP_PER_LEVEL_PLACEHOLDER;
  g_view.hp_cur   = 250;
  g_view.hp_max   = 250;
  g_view.mood_pct = 100;
  for (uint8_t i = 0; i < ST_COUNT; i++) g_view.care_pct[i] = 100;
}

static void fixture_none(void) {
  memset(&g_view, 0, sizeof g_view);
}

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
  g_repeat = 0;
  g_minigame = 0xFF;
  g_god = 0;
  g_bright = 0;
  g_cfg_saves = 0;
  g_flushes = 0;
  g_cfg_p = &g_cfg;
  memset(&g_cfg, 0, sizeof g_cfg);
  g_cfg.brightness = OLED_CONTRAST_DEFAULT;
  ui_bind_view(&fixture_view);
  fixture_starter();
}

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
static void snapshot(uint8_t screen, const char* name) {
  const ScreenDef* d = screen_def(screen);
  CHECK(d != nullptr);
  if (!d) return;

  fb_reset();
  d->render();

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

// =============================================================================
//  THE TABLE
// =============================================================================
TEST(table_rows_are_consistent) {
  // A row is migrated exactly when it has a render hook, and screen_def()
  // must agree with that for every id.
  for (uint8_t s = 0; s < (uint8_t)SCR_COUNT; s++) {
    const ScreenDef* d = screen_def(s);
    CHECK((d == nullptr) == (SCREENS[s].render == nullptr));
    if (!d) continue;
    // No row may ask for a frame rate the scheduler cannot honour.
    CHECK(d->fps == 0 || (d->fps >= 1 && d->fps <= 60));
  }

  // Everything P2-C11a and P2-C11b migrated.
  static const uint8_t kMigrated[] = {
    SCR_BOOT, SCR_LOAD_SAVE, SCR_ERROR,
    SCR_HOME, SCR_MENU, SCR_FEED, SCR_PLAY,
    SCR_STATUS_A, SCR_STATUS_B, SCR_SETTINGS, SCR_CLOCK
  };
  for (size_t i = 0; i < sizeof kMigrated / sizeof kMigrated[0]; i++)
    CHECK(screen_def(kMigrated[i]) != nullptr);

  // And nothing else: GAME, SOCIAL, EGG, GOD and QR are still ui.cpp's, and
  // CONFIRM / ALERT are modals with no base frame of their own.
  static const uint8_t kLegacy[] = {
    SCR_GAME, SCR_SOCIAL, SCR_CONFIRM, SCR_ALERT, SCR_EGG, SCR_GOD, SCR_QR
  };
  for (size_t i = 0; i < sizeof kLegacy / sizeof kLegacy[0]; i++)
    CHECK(screen_def(kLegacy[i]) == nullptr);

  // HOME is where the auto-return goes, so it must never time out itself.
  CHECK((SCREENS[SCR_HOME].flags & SF_STICKY) != 0);
  // Leaving HOME has to end the choreography and release the petfx hold.
  CHECK(SCREENS[SCR_HOME].leave != nullptr);
  // TIME owns HOLD_R (which is BACK everywhere else) and must not be timed out
  // half way through a date.
  CHECK((SCREENS[SCR_CLOCK].flags & (SF_STICKY | SF_LOCK_INPUT))
        == (SF_STICKY | SF_LOCK_INPUT));
  CHECK(SCREENS[SCR_CLOCK].update != nullptr);   // the right button's repeat
  // Every other migrated row is an ordinary screen: it times out, and the
  // global navigation grammar runs before its own handler.
  static const uint8_t kOrdinary[] = {
    SCR_MENU, SCR_FEED, SCR_PLAY, SCR_STATUS_A, SCR_STATUS_B, SCR_SETTINGS
  };
  for (size_t i = 0; i < sizeof kOrdinary / sizeof kOrdinary[0]; i++)
    CHECK(SCREENS[kOrdinary[i]].flags == 0);

  // All three hold the device: no auto-return, no global navigation grammar.
  CHECK((SCREENS[SCR_BOOT].flags      & (SF_STICKY | SF_LOCK_INPUT)) == (SF_STICKY | SF_LOCK_INPUT));
  CHECK((SCREENS[SCR_LOAD_SAVE].flags & (SF_STICKY | SF_LOCK_INPUT)) == (SF_STICKY | SF_LOCK_INPUT));
  CHECK((SCREENS[SCR_ERROR].flags     & (SF_STICKY | SF_LOCK_INPUT)) == (SF_STICKY | SF_LOCK_INPUT));

  // BOOT and LOAD_SAVE are frames, not states: no hooks beyond render.
  CHECK(SCREENS[SCR_BOOT].input      == nullptr);
  CHECK(SCREENS[SCR_LOAD_SAVE].input == nullptr);
  CHECK(SCREENS[SCR_ERROR].input     != nullptr);
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
  snapshot(SCR_FEED, "care_list");
}

TEST(snapshot_care_list_scrolled) {
  seams2_reset();
  care_enter();
  for (uint8_t i = 0; i < CARE_BACK; i++) care_input(GST_TAP_L);
  CHECK_EQ(care_cursor(), (uint8_t)CARE_BACK);
  snapshot(SCR_FEED, "care_list_back");
}

TEST(snapshot_play_list) {
  seams2_reset();
  play_enter();
  snapshot(SCR_PLAY, "play_list");
}

TEST(snapshot_status_a) {
  seams2_reset();
  status_a_enter();
  snapshot(SCR_STATUS_A, "status_a_starter");
}

TEST(snapshot_status_a_maxed) {
  seams2_reset();
  fixture_maxed();
  status_a_enter();
  snapshot(SCR_STATUS_A, "status_a_maxed");
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
  snapshot(SCR_CLOCK, "time_entry");
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
    { MENU_PEBBLE,   1, SCR_STATUS_A },
    { MENU_CARE,     1, SCR_FEED     },
    { MENU_PLAY,     1, SCR_PLAY     },
    { MENU_BOX,      0, 0xFF         },
    { MENU_NETWORK,  0, 0xFF         },
    { MENU_LINK,     1, SCR_SOCIAL   },
    { MENU_SETTINGS, 1, SCR_SETTINGS },
  };
  for (size_t i = 0; i < sizeof kWant / sizeof kWant[0]; i++) {
    seams_reset();
    g_push = 0xFF;
    CHECK_EQ(menu_cursor(), kWant[i].item);
    menu_input(GST_TAP_R);
    if (kWant[i].pushes) {
      CHECK_EQ(g_push, kWant[i].to);
    } else {
      CHECK_EQ(g_push, (uint8_t)0xFF);         // nothing was opened...
      CHECK_EQ(g_toast, STR_UI_SOON);          // ...and the user was told why
    }
    menu_input(GST_TAP_L);                     // on to the next item
  }
  CHECK_EQ(menu_cursor(), (uint8_t)MENU_PEBBLE);   // invariant 4: it is a ring
}

TEST(menu_help_and_repeat) {
  seams2_reset();
  menu_to(MENU_PEBBLE);
  menu_input(GST_BOTH);
  CHECK_EQ(g_help, STR_HLP_STATUS);
  menu_input(GST_DBL_R);
  CHECK_EQ(g_repeat, 1);
  menu_input(GST_TAP_L);
  menu_input(GST_TAP_L);
  menu_input(GST_DBL_L);                        // a jump back to the first item
  CHECK_EQ(menu_cursor(), (uint8_t)MENU_PEBBLE);
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

  care_input(GST_TAP_L);                        // light: no film, so no journey
  g_shown = ACT_NONE;
  care_input(GST_TAP_R);
  CHECK_EQ(g_action, (uint8_t)ACT_LIGHT_TOGGLE);
  CHECK_EQ(g_shown, (uint8_t)ACT_NONE);

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
  play_input(GST_DBL_R);                        // jump to the last row
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
  CHECK_EQ(g_push, (uint8_t)SCR_STATUS_A);
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
  CHECK_EQ(g_goto, (uint8_t)SCR_STATUS_A);
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

