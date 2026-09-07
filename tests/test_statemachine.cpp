// =============================================================================
//  Pebblebol host tests - test_statemachine.cpp
//  THE NAVIGATION STATE MACHINE AND THE SCREEN TABLE (plan P2-C11d, test plan
//  section 4).
//
//  Five properties, and every one of them is a promise some other file makes:
//
//    1. TABLE COMPLETENESS. Every ScreenId has a row and every row names all
//       five hooks (spec section 6: "every state must define enter, update,
//       render, handleInput, exit"). This is the test that stops the next
//       screen from being added as a hole.
//    2. THE BACK STACK IS FIVE DEEP (ui.h UI_STACK_DEPTH) and, once full,
//       keeps the OLDEST trail and drops the newest push - which is what
//       nav_push() always did, and what makes a long walk still come home.
//    3. THE 20 s AUTO-RETURN (invariant 3) fires only for a row WITHOUT
//       SF_STICKY, and never while a modal has blocked it.
//    4. ERROR IS REACHABLE FROM EVERY LoadResult that means the save cannot be
//       used, and from no other: the five benign outcomes must never take a
//       working device to a dead end.
//    5. BOOT -> ERROR ON DISPLAY FAILURE (plan T10): rd_begin() answering
//       false replaces rd_fatal()'s "blink forever and never return".
//
//  It drives the REAL table and the REAL router, so the ui.cpp seams below are
//  recordings exactly as they are in test_screens.cpp.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "app/input_router.h"
#include "app/state_machine.h"
#include "core/nt_types.h"
#include "core/strings_es.h"
#include "fakes/gfx_fb.h"
#include "host_shims.h"
#include "persistence/save_manager.h"
#include "ui/dialog.h"
#include "ui/screen.h"
#include "ui/gfx.h"
#include "ui/screen_boot.h"
#include "ui/screen_battle.h"
#include "ui/screen_box.h"
#include "ui/screen_care.h"
#include "ui/screen_creator.h"
#include "ui/screen_diag.h"
#include "ui/screen_error.h"
#include "ui/screen_evolution.h"
#include "ui/screen_home.h"
#include "ui/screen_encounter.h"
#include "ui/screen_link.h"
#include "ui/screen_menu.h"
#include "ui/screen_network.h"
#include "networking/wifi_scanner.h"
#include "ui/screen_settings.h"
#include "ui/screen_soon.h"
#include "ui/screen_status.h"
#include "ui/screen_setup.h"
#include "ui/screen_time.h"
#include "ui/screen_view.h"
#include "ui/ui.h"

// =============================================================================
//  THE SEAMS. Same recording pattern test_screens.cpp uses.
// =============================================================================
static uint16_t g_toast = STR_EMPTY;
static uint16_t g_help  = STR_EMPTY;
static int      g_wipe  = 0;
static int      g_frames = 0;
static int      g_wiggles = 0;
static int      g_game_enters = 0, g_game_leaves = 0, g_game_updates = 0;
static int      g_game_renders = 0, g_game_inputs = 0;
static uint8_t  g_box_activated = 0xFF;
static uint8_t  g_battle_entry  = 0xFF;
static uint8_t  g_battle_won    = 0xFF;
static int      g_battle_reports = 0;
static uint8_t  g_hold_fps      = 0;
static Config   g_cfg;

void ui_toast(uint16_t id)        { g_toast = id; }
void ui_help(uint16_t id)         { g_help = id; }
void ui_goto(ScreenId s)          { sm_goto(s); }
void ui_push(ScreenId s)          { sm_push(s); }
void ui_back(void)                { sm_back(); }
void ui_home(void)                { sm_home(); }
void ui_replace_root(ScreenId s)  { sm_replace_root(s); }
void ui_confirm_wipe(void)        { g_wipe++; }
void ui_note_recovered(void)      { }
void ui_note_input(void)          { sm_note_input(); }
void ui_request_frame(void)       { g_frames++; }
void ui_wiggle(void)              { g_wiggles++; }
uint32_t ui_now_ms(void)          { return host_ms(); }
uint32_t ui_idle_ms(void)         { return sm_idle_ms(); }
Config*  ui_cfg(void)             { return &g_cfg; }
void ui_cfg_changed(void)         { }
void ui_setup_persist(void)       { }
bool ui_set_starter(uint8_t)      { return true; }
void ui_apply_brightness(uint8_t) { }
bool ui_do_action(uint8_t)        { return true; }
bool ui_act_and_show(uint8_t)     { return true; }
void ui_repeat_last_action(void)  { }
void ui_confirm_medicine(void)    { }
void ui_start_minigame(uint8_t)   { }
// The P4-C4 battle seams. This binary drives the REAL table, so SCR_BATTLE's
// row runs the real screen and these three have to exist for it to link.
void ui_start_battle(uint8_t e)   { g_battle_entry = e; }

// THE EXPLORATION SEAMS (P5-C3/C4). This binary is about NAVIGATION, so the
// stubs are the smallest thing that lets the two new screens link and render:
// a driver that never starts, a clock that never moves, an empty bag and an
// empty cooldown table. tests/test_screens.cpp is where they are actually
// driven, with a fake radio that answers.
static const WifiScanDriver& sm_null_driver(void) {
  static const WifiScanDriver d = {
    [](void) -> bool { return false; },
    [](void) -> int16_t { return WSCAN_POLL_FAILED; },
    [](ScanResult*, uint8_t) -> uint8_t { return 0; },
    [](void) { }
  };
  return d;
}

// P10-C2. A RADIO THAT STARTS AND NEVER ANSWERS - the only shape in which
// WIFI_SCAN_TIMEOUT_MS can be reached at all. The driver above refuses to start
// and the job goes straight to WSCAN_FAILED, which exercises the FAILED arm and
// not the timeout arm; spec section 47's radio question is about the scan that
// is still in flight when the user has given up on it. g_scan_stops counts the
// stop() calls, because "Wi-Fi shuts down after use" is the other half of the
// same sentence and a timeout that leaked the radio would satisfy a navigation
// check while leaving the antenna on.
static int g_scan_stops = 0;
static const WifiScanDriver& sm_stalled_driver(void) {
  static const WifiScanDriver d = {
    [](void) -> bool { return true; },
    [](void) -> int16_t { return WSCAN_POLL_RUNNING; },
    [](ScanResult*, uint8_t) -> uint8_t { return 0; },
    [](void) { ++g_scan_stops; }
  };
  return d;
}
static bool g_scan_stalled = false;
static CooldownTable g_sm_cds;
static Inventory     g_sm_inv;
const WifiScanDriver& ui_scan_driver(void) {
  return g_scan_stalled ? sm_stalled_driver() : sm_null_driver();
}
void ui_explore_clock(uint32_t* e, uint32_t* ms, uint8_t* cal) {
  if (e)   *e   = 1700000000u;
  if (ms)  *ms  = host_ms();
  if (cal) *cal = (uint8_t)CAL_USER;
}
uint32_t ui_device_seed(void)     { return 0x0BADC0DEu; }
uint32_t ui_explore_roll(void)    { return 0u; }
CooldownTable& ui_cooldowns(void) { return g_sm_cds; }
Inventory&     ui_inventory(void) { return g_sm_inv; }
void ui_explore_commit(void)      { }
Genome ui_fresh_genome(void)      { Genome g; memset(&g, 0, sizeof g); return g; }
void ui_award_xp(uint16_t, uint8_t) { }
PebbleInstance* ui_active_pebble(void) { return nullptr; }
void ui_battle_result(uint8_t, uint8_t won) { g_battle_won = won; ++g_battle_reports; }
void ui_hold_fps(uint8_t f, uint16_t) { g_hold_fps = f; }
void ui_flash(uint16_t)           { }
void ui_shake(uint8_t, uint16_t)  { }
uint8_t ui_god_progress(void)     { return 0; }
void ui_input_flush(void)         { }
void ui_request_hatch(void)       { }
// The CREATOR screen's radio seam, RECORDED here rather than ignored: P8-C2
// gave the screen two timeouts of its own, and the property worth checking over
// the real machine is that both of them still run the leave hook.
static int      g_creator_radio_calls = 0;
static int      g_creator_radio_last  = -1;
static uint8_t  g_creator_ap   = 0;
static uint8_t  g_creator_idle = 0;
void ui_creator_radio(bool on)    { g_creator_radio_last = on ? 1 : 0;
                                    g_creator_radio_calls++; }
bool ui_btn_down(uint8_t)         { return false; }
uint32_t ui_btn_hold_ms(uint8_t)  { return 0; }
void ui_box_activate(uint8_t s)   { g_box_activated = s; }
void ui_box_swap(uint8_t, uint8_t) { }
void ui_box_release(uint8_t)      { }

void ui_game_enter(void)          { g_game_enters++; }
void ui_game_update(uint32_t)     { g_game_updates++; }
void ui_game_render(void)         { g_game_renders++; }
void ui_game_input(Gesture)       { g_game_inputs++; }
void ui_game_leave(void)          { g_game_leaves++; }

void ui_creator_info(CreatorInfo& out) {
  memset(&out, 0, sizeof out);
  out.ap_up        = g_creator_ap;
  out.idle_expired = g_creator_idle;
}
void ui_info_lines(char lines[UI_INFO_LINES][UI_INFO_CAP]) {
  for (uint8_t i = 0; i < UI_INFO_LINES; ++i) lines[i][0] = '\0';
}
bool ui_get_clock(uint16_t*, uint8_t*, uint8_t*, uint8_t*, uint8_t*) { return false; }
bool ui_set_clock(uint16_t, uint8_t, uint8_t, uint8_t, uint8_t)      { return true; }

// The two ui.cpp halves of a screen change the machine still calls out to.
void ui_nav_reset(void)        { dialog_close(); gfx_list_reset(); }
void ui_nav_arrived(uint8_t)   { g_frames++; }

// The three ERROR seams, RECORDED (P10-C2). ui/screen_error.cpp reaches the
// checkpoint restore, the panel retry and the wipe modal through bound
// pointers, and an exit that is "the user answered the question" is only
// visible to an assertion if something on this side counts the answer.
static int  g_recover_calls = 0;
static bool g_recover_ok    = false;
static int  g_retry_calls   = 0;
static bool g_retry_ok      = false;
static bool sm_recover(void) { ++g_recover_calls; return g_recover_ok; }
static bool sm_retry(void)   { ++g_retry_calls;   return g_retry_ok; }
static void sm_led(bool)     { }

static void reset_all(void) {
  host_reset();
  host_set_ms(100000u);
  g_toast = STR_EMPTY;
  g_help  = STR_EMPTY;
  g_wipe  = 0;
  g_wiggles = 0;
  g_game_enters = g_game_leaves = g_game_updates = 0;
  g_game_renders = g_game_inputs = 0;
  g_box_activated = 0xFF;
  g_battle_entry = 0xFF;
  g_battle_won = 0xFF;
  g_battle_reports = 0;
  g_hold_fps = 0;
  g_creator_radio_calls = 0;
  g_creator_radio_last  = -1;
  g_creator_ap   = 0;
  g_creator_idle = 0;
  memset(&g_cfg, 0, sizeof g_cfg);
  dialog_reset();
  dialog_bind_commit(nullptr);
  diag_bind(nullptr, nullptr, nullptr);
  evo_bind_ceremony(nullptr);
  ui_bind_view(nullptr);
  err_set_kind(ERRK_NONE);
  g_recover_calls = 0; g_recover_ok = false;
  g_retry_calls   = 0; g_retry_ok   = false;
  g_scan_stops    = 0; g_scan_stalled = false;
  ui_bind_recover(&sm_recover);
  ui_bind_display_retry(&sm_retry);
  ui_bind_led(&sm_led);
  sm_begin();
}

// =============================================================================
//  1. TABLE COMPLETENESS
// =============================================================================
static const char* kName[SCR_COUNT] = {
  "BOOT", "LOAD_SAVE", "HOME", "MENU", "CARE", "PLAY", "GAME", "BOX",
  "STATUS", "STATUS_B", "NETWORK", "LINK", "CREATOR", "SETTINGS", "TIME",
  "SETUP_NAME", "SETUP_STARTER",
  "CONFIRM", "ALERT", "ENCOUNTER", "CAPTURE", "BATTLE", "TRADE", "BREED",
  "EVOLUTION", "ITEM_REWARD", "ERROR", "SLEEP", "DIAG"
};

TEST(table_complete) {
  for (uint8_t i = 0; i < (uint8_t)SCR_COUNT; ++i) {
    const ScreenDef* d = screen_def(i);
    if (!d) { fprintf(stderr, "  %s: no row\n", kName[i]); }
    CHECK(d != nullptr);
    if (!d->enter || !d->update || !d->render || !d->input || !d->leave) {
      fprintf(stderr, "  %s: hooks %p %p %p %p %p\n", kName[i],
              (const void*)d->enter,  (const void*)d->update,
              (const void*)d->render, (const void*)d->input,
              (const void*)d->leave);
    }
    CHECK(d->enter  != nullptr);
    CHECK(d->update != nullptr);
    CHECK(d->render != nullptr);
    CHECK(d->input  != nullptr);
    CHECK(d->leave  != nullptr);
    // No row may set both SF_LOCK_INPUT and SF_OWNS_BACK: the first already
    // includes the second, and a row that claims both is a row whose author
    // did not know which one it meant.
    CHECK(((d->flags & SF_LOCK_INPUT) && (d->flags & SF_OWNS_BACK)) == 0);
  }
  // Out of range is the ONLY null answer.
  CHECK(screen_def((uint8_t)SCR_COUNT) == nullptr);
  CHECK(screen_def(0xFF) == nullptr);
}

// THE ROWS ARE POSITIONAL. screen_table.cpp names each id in a trailing comment
// only, and its three static_asserts pin nothing but the count and the two
// ends - so an insertion in the middle that keeps the count would shift every
// row after it past all three and go unnoticed. Naming the render hook of all
// 27 rows here is the check those asserts cannot make: it is the same order
// kName[] above already encodes, spelled in symbols the linker resolves.
TEST(every_row_is_the_screen_its_position_claims) {
  void (*const kRender[SCR_COUNT])(void) = {
    boot_render, load_save_render, home_render, menu_render, care_render,
    play_render, ui_game_render, box_render, status_a_render, status_b_render,
    network_render, link_render, creator_render, settings_render, time_render,
    setup_name_render, setup_pick_render,
    soon_generic, soon_generic, encounter_render, capture_render, battle_render,
    soon_trade, soon_breed, evo_render, soon_item_reward, err_render,
    soon_sleep, diag_render
  };
  for (uint8_t i = 0; i < (uint8_t)SCR_COUNT; ++i) {
    const ScreenDef* d = screen_def(i);
    CHECK(d != nullptr);
    if (d->render != kRender[i]) fprintf(stderr, "  row %u is not %s\n",
                                         (unsigned)i, kName[i]);
    CHECK(d->render == kRender[i]);
  }
}

// Every state renders inside the panel: a placeholder that drew off the edge
// would be a hole of a different kind.
TEST(every_state_draws) {
  for (uint8_t i = 0; i < (uint8_t)SCR_COUNT; ++i) {
    reset_all();
    sm_goto((ScreenId)i);
    fb_reset();
    CHECK(sm_draw());
    if (fb_oob()) fprintf(stderr, "  %s: %d out-of-bounds\n", kName[i], fb_oob());
    CHECK(fb_oob() == 0);
  }
}

// =============================================================================
//  2. THE BACK STACK
// =============================================================================
TEST(back_stack_depth) {
  reset_all();
  CHECK(sm_current() == SCR_HOME);

  // Six pushes into a five-deep stack. The stack holds the five screens we
  // came FROM (HOME, MENU, CARE, PLAY, BOX); the sixth push is not recorded.
  const ScreenId walk[6] = { SCR_MENU, SCR_CARE, SCR_PLAY, SCR_BOX,
                             SCR_STATUS, SCR_SETTINGS };
  for (uint8_t i = 0; i < 6; ++i) sm_push(walk[i]);
  CHECK(sm_current() == SCR_SETTINGS);

  // Five backs unwind the recorded trail, newest first...
  const ScreenId home_trail[5] = { SCR_BOX, SCR_PLAY, SCR_CARE, SCR_MENU, SCR_HOME };
  for (uint8_t i = 0; i < 5; ++i) {
    sm_back();
    CHECK(sm_current() == home_trail[i]);
  }
  // ...and a back from an empty stack is HOME, not a wrap into nothing.
  sm_back();
  CHECK(sm_current() == SCR_HOME);
}

TEST(home_clears_the_stack) {
  reset_all();
  sm_push(SCR_MENU);
  sm_push(SCR_BOX);
  sm_home();
  CHECK(sm_current() == SCR_HOME);
  sm_back();
  CHECK(sm_current() == SCR_HOME);       // nothing was left behind

  sm_push(SCR_MENU);
  sm_replace_root(SCR_EVOLUTION);
  CHECK(sm_current() == SCR_EVOLUTION);
  sm_back();
  CHECK(sm_current() == SCR_HOME);       // a re-root is not a push
}

// =============================================================================
//  3. THE 20 s AUTO-RETURN
// =============================================================================
static bool timed_out_from(ScreenId s) {
  reset_all();
  sm_push(s);
  host_advance_ms(UI_AUTORETURN_MS + 1u);
  const bool moved = sm_service(host_ms());
  return moved && sm_current() == SCR_HOME;
}

TEST(autoreturn_follows_sticky) {
  for (uint8_t i = 0; i < (uint8_t)SCR_COUNT; ++i) {
    if (i == (uint8_t)SCR_HOME) continue;          // already home
    const bool sticky = (SCREENS[i].flags & SF_STICKY) != 0u;
    const bool timed  = timed_out_from((ScreenId)i);
    if (sticky == timed) fprintf(stderr, "  %s: sticky %d timed %d\n",
                                 kName[i], (int)sticky, (int)timed);
    CHECK(sticky != timed);
  }
}

// P8-C2. SF_STICKY on the CREATOR row is likewise not a preference, and the
// case it fixes is the ordinary one: the user enters CREATOR, picks up a phone
// and joins the access point. That takes longer than twenty seconds, and until
// this flag the navigation timeout fired first and tore the portal down. The
// flag itself is asserted in tests/test_screens.cpp; this is the consequence,
// over the real table and the real machine.
TEST(the_creator_portal_is_never_timed_out_from_under_the_user) {
  reset_all();
  g_creator_ap = 1;                     // the access point is serving
  g_creator_idle = 0;                   // and a client is still talking to it
  sm_push(SCR_CREATOR);
  CHECK(sm_current() == SCR_CREATOR);
  CHECK_EQ(g_creator_radio_last, 1);    // enter() asked for the radio

  for (int i = 0; i < 4; ++i) {
    host_advance_ms(UI_AUTORETURN_MS + 1u);
    CHECK(!sm_service(host_ms()));
  }
  CHECK(sm_current() == SCR_CREATOR);
  CHECK_EQ(g_creator_radio_last, 1);    // and still holds it

  // The D7 grace period is what ends it, and it leaves through the SAME hook a
  // B press would: the radio is released exactly once, by creator_leave().
  g_creator_idle = 1;
  host_advance_ms(2000u);
  (void)sm_service(host_ms());
  CHECK(sm_current() == SCR_HOME);
  CHECK_EQ(g_creator_radio_last, 0);
  g_creator_ap = 0;
  g_creator_idle = 0;
}

// The other exit, spec section 47: the access point never came up. Without it
// SF_STICKY would leave a user on "Conectando" with the radio powered and no
// route out but a button.
TEST(the_creator_screen_gives_up_when_the_radio_never_arrives) {
  reset_all();
  g_creator_ap = 0;
  sm_push(SCR_CREATOR);
  CHECK(sm_current() == SCR_CREATOR);

  host_advance_ms(CREATOR_AP_WAIT_MS - 1000u);
  (void)sm_service(host_ms());
  CHECK(sm_current() == SCR_CREATOR);

  host_advance_ms(1000u);
  (void)sm_service(host_ms());
  CHECK(sm_current() == SCR_HOME);
  CHECK_EQ(g_creator_radio_last, 0);    // the leave hook ran on the way out
}

// P4-C4. SF_STICKY on the BATTLE row is not a preference: leaving the screen
// runs its ONE report path, so invariant 3 firing twenty seconds into a fight
// would not merely move the player - it would end the battle and report it.
// The flag itself is asserted in tests/test_screens.cpp; this is the
// consequence, over the real table and the real machine.
TEST(a_battle_is_never_timed_out_from_under_the_player) {
  reset_all();
  sm_push(SCR_BATTLE);
  CHECK(sm_current() == SCR_BATTLE);
  host_advance_ms(UI_AUTORETURN_MS * 4u);
  CHECK(!sm_service(host_ms()));
  CHECK(sm_current() == SCR_BATTLE);
  CHECK_EQ(g_battle_reports, 0);        // nothing left the screen, so nothing reported

  // And leaving it deliberately DOES report, exactly once - which is what makes
  // the check above a statement about the auto-return and not about a hook that
  // never fires.
  sm_back();
  CHECK(sm_current() != SCR_BATTLE);
  CHECK_EQ(g_battle_reports, 1);
}

TEST(autoreturn_waits_and_blocks) {
  reset_all();
  sm_push(SCR_MENU);
  host_advance_ms(UI_AUTORETURN_MS - 1u);
  CHECK(!sm_service(host_ms()));
  CHECK(sm_current() == SCR_MENU);

  // A gesture restarts the countdown.
  sm_note_input();
  host_advance_ms(UI_AUTORETURN_MS - 1u);
  CHECK(!sm_service(host_ms()));

  // A modal freezes it: the countdown belongs to the screen underneath and
  // must neither fire nor restart while a question is on top of it.
  sm_block_autoreturn(true);
  host_advance_ms(UI_AUTORETURN_MS * 4u);
  CHECK(!sm_service(host_ms()));
  CHECK(sm_current() == SCR_MENU);
  sm_block_autoreturn(false);
  CHECK(sm_service(host_ms()));
  CHECK(sm_current() == SCR_HOME);
}

// =============================================================================
//  4. THE SECTION 7 GRAMMAR (app/input_router.cpp)
// =============================================================================
TEST(router_back_and_home) {
  // B is BACK on an ordinary screen.
  reset_all();
  sm_push(SCR_MENU);
  CHECK(router_global(GST_TAP_R));
  CHECK(sm_current() == SCR_HOME);

  // LONG_BOTH is HOME from anywhere, however deep.
  reset_all();
  sm_push(SCR_MENU);
  sm_push(SCR_SETTINGS);
  CHECK(router_global(GST_LONG_BOTH));
  CHECK(sm_current() == SCR_HOME);

  // On HOME neither is the router's: B is the caress and LONG_BOTH opens
  // SETTINGS, both of which belong to the screen.
  reset_all();
  CHECK(!router_global(GST_TAP_R));
  CHECK(!router_global(GST_LONG_BOTH));

  // SF_LOCK_INPUT takes both away from the router.
  reset_all();
  sm_push(SCR_TIME);
  CHECK(!router_global(GST_TAP_R));
  CHECK(!router_global(GST_LONG_BOTH));
  CHECK(sm_current() == SCR_TIME);

  // SF_OWNS_BACK takes only B: LONG_BOTH still leaves.
  reset_all();
  sm_push(SCR_BOX);
  CHECK(!router_global(GST_TAP_R));
  CHECK(sm_current() == SCR_BOX);
  CHECK(router_global(GST_LONG_BOTH));
  CHECK(sm_current() == SCR_HOME);

  // Nothing else is ever consumed globally: A, its hold, B held and BOTH all
  // belong to the screen.
  //
  // Written as a sweep of the WHOLE enum rather than a hand-listed array,
  // because the hand-listed one went stale the moment P3-C4a deleted
  // GST_DBL_L/R - it was still asserting a property of gestures that no longer
  // existed. This form cannot drift: a gesture added tomorrow is covered, and
  // one that starts being consumed globally fails here until it is listed.
  reset_all();
  sm_push(SCR_MENU);
  for (uint8_t g = 0; g < (uint8_t)GST_COUNT; ++g) {
    const Gesture gg = (Gesture)g;
    if (gg == GST_NONE || gg == GST_TAP_R || gg == GST_LONG_BOTH) continue;
    CHECK(!router_global(gg));
  }
  CHECK(sm_current() == SCR_MENU);
  CHECK(!router_handle(GST_NONE));
}

// The BOX walks back through its own modes before it leaves (SF_OWNS_BACK).
TEST(router_box_owns_back) {
  reset_all();
  sm_push(SCR_BOX);
  CHECK(box_screen_mode() == BOXM_LIST);
  // With an unbound Box every slot is empty, so the list refuses to open an
  // action page - which is itself the "no hole" answer.
  CHECK(router_handle(GST_HOLD_R));
  CHECK(box_screen_mode() == BOXM_LIST);
  CHECK(router_handle(GST_TAP_R));
  CHECK(sm_current() == SCR_HOME);
}

// =============================================================================
//  5. ERROR
// =============================================================================
TEST(error_from_every_load_result) {
  struct Row { uint8_t load; bool to_error; uint8_t kind; };
  const Row rows[] = {
    { LOAD_OK,             false, ERRK_NONE },
    { LOAD_FRESH,          false, ERRK_NONE },
    { LOAD_MIGRATED,       false, ERRK_NONE },
    { LOAD_RECOVERED_PAIR, false, ERRK_NONE },
    { LOAD_RECOVERED_CKPT, false, ERRK_NONE },
    { LOAD_CORRUPT,        true,  ERRK_SAVE_CORRUPT },
    { LOAD_FOREIGN_NEWER,  true,  ERRK_SAVE_NEWER },
  };
  // Every value of the enum is covered: a new LoadResult must land here.
  CHECK((int)(sizeof rows / sizeof rows[0]) == (int)LOAD_FOREIGN_NEWER + 1);

  for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
    reset_all();
    sm_goto(SCR_LOAD_SAVE);
    ui_note_load(rows[i].load);
    if (rows[i].to_error) {
      CHECK(sm_current() == SCR_ERROR);
      CHECK(err_kind() == rows[i].kind);
    } else {
      CHECK(sm_current() == SCR_LOAD_SAVE);
      CHECK(err_kind() == ERRK_NONE);
    }
  }
}

TEST(boot_to_error_on_display_failure) {
  reset_all();
  sm_goto(SCR_BOOT);
  CHECK(sm_current() == SCR_BOOT);
  ui_note_display_failure();
  CHECK(sm_current() == SCR_ERROR);
  CHECK(err_kind() == ERRK_DISPLAY);
  // And it holds: ERROR is SF_STICKY | SF_LOCK_INPUT, so neither the
  // auto-return nor the global grammar can walk away from the question.
  host_advance_ms(UI_AUTORETURN_MS * 3u);
  CHECK(!sm_service(host_ms()));
  CHECK(!router_global(GST_TAP_R));
  CHECK(!router_global(GST_LONG_BOTH));
  CHECK(sm_current() == SCR_ERROR);
}

// A save question raised earlier in the same boot is parked, not overwritten.
TEST(display_failure_parks_the_save_question) {
  reset_all();
  sm_goto(SCR_LOAD_SAVE);
  ui_note_load(LOAD_CORRUPT);
  CHECK(err_kind() == ERRK_SAVE_CORRUPT);
  ui_note_display_failure();
  CHECK(err_kind() == ERRK_DISPLAY);
  CHECK(sm_current() == SCR_ERROR);
}

// =============================================================================
//  6. SPEC SECTION 47 - EVERY STATE AND EVERY RADIO WAIT HAS A WAY OUT
//      (P10-C2, plan bullet 1: "listed in test_statemachine")
//
//  THE CLAIM THIS REPLACES WAS ASSERTED AND NOT CHECKED. autoreturn_follows_
//  sticky above already walks all SCR_COUNT rows and proves `sticky != timed
//  out`, which says the SEVENTEEN non-sticky rows carry the 20 s ceiling and
//  says NOTHING WHATEVER about the ten sticky ones - and the sticky rows are
//  precisely the rows that can hang. Two of them had a driven bound (CREATOR,
//  BATTLE) and the rest had a comment.
//
//  WHAT THE SWEEP FOUND. SCR_ERROR with s_kind == ERRK_SAVE_NEWER had NO WAY
//  OUT AT ALL. The row is SF_STICKY | SF_LOCK_INPUT, so app/input_router.cpp
//  returns false at its first line and invariant 2's LONG_BOTH escape never
//  runs; err_input() had no LONG_BOTH case of its own (ui/screen_time.cpp, the
//  same flag pair, has always had one); and both taps were deliberate refusals
//  that only toast, because a save from a newer firmware is GOOD data no button
//  here may overwrite. The refusal is right and is untouched. What was missing
//  was an exit, and it is reachable: insert a card written by a newer build and
//  app.cpp's ui_note_load(LOAD_FOREIGN_NEWER) parks the device there for ever,
//  under an affordance strip advertising two actions that do nothing.
//
//  THE TABLE IS THE TEST. Every row names HOW the state ends and the sweep
//  DRIVES it - it does not read a flag and believe a comment. A ScreenId with
//  no row fails the coverage check below, so a screen added in P10-C3 or
//  P10-C4 cannot arrive without an answer to this question.
// =============================================================================
enum ExitKind : uint8_t {
  XK_AUTORETURN = 0,   // invariant 3's 20 s ceiling, driven
  XK_TIMEOUT,          // the screen's OWN named ceiling, in its update hook
  XK_GESTURE,          // one gesture through the REAL router leaves the screen
  XK_ANSWER,           // one gesture answers the question through a bound seam
  XK_ROOT,             // HOME: where every exit above goes
  XK_NOT_RESIDENT      // never sm_current(); see the four reasons below
};

struct ExitRow {
  uint8_t     scr;
  uint8_t     err;        // ERRK_* on SCR_ERROR, ERRK_NONE elsewhere
  uint8_t     kind;
  uint8_t     g;          // Gesture, for XK_GESTURE / XK_ANSWER
  uint32_t    bound_ms;   // for XK_TIMEOUT
  const char* label;      // what a failure prints
  const char* bound;      // what the ceiling is CALLED, in the tree
};

// Preconditions a row needs before it can be driven. Kept out of the table so
// the table stays readable as a list of answers.
static void arm_row(const ExitRow& r) {
  switch (r.scr) {
    case SCR_NETWORK:
      // A scan that started and will never answer: the only state in which
      // WIFI_SCAN_TIMEOUT_MS is reachable at all.
      g_scan_stalled = true;
      break;
    case SCR_CREATOR:
      g_creator_ap = 0;      // the access point never came up
      g_creator_idle = 0;
      break;
    case SCR_ERROR:
      err_set_kind(r.err);
      g_retry_ok = (r.err == ERRK_DISPLAY);   // the panel comes back on retry
      break;
    default: break;
  }
}

static const ExitRow kExits[] = {
  // --- the boot pipeline: drawn, never navigated to ------------------------
  // ui.cpp's ui_boot_screen() renders ONE frame of each and returns; gs_load()
  // then runs synchronously with no loop underneath it, so neither is ever
  // sm_current() and neither has a wait the state machine owns. The wait is the
  // NVS read, bounded by the flash and not by a timer. tools/check.sh gates the
  // exemption: a navigation call to either fails the build, so this row cannot
  // quietly become a lie the way an unchecked comment would.
  { SCR_BOOT,      ERRK_NONE, XK_NOT_RESIDENT, GST_NONE, 0, "BOOT",      "no wait: one frame, then gs_load()" },
  { SCR_LOAD_SAVE, ERRK_NONE, XK_NOT_RESIDENT, GST_NONE, 0, "LOAD_SAVE", "no wait: bounded by the NVS read" },

  // --- the root ------------------------------------------------------------
  { SCR_HOME,      ERRK_NONE, XK_ROOT, GST_NONE, 0, "HOME", "the destination of every other exit" },

  // --- the ordinary screens: invariant 3 ------------------------------------
  { SCR_MENU,      ERRK_NONE, XK_AUTORETURN, GST_TAP_R, 0, "MENU",      "UI_AUTORETURN_MS" },
  { SCR_CARE,      ERRK_NONE, XK_AUTORETURN, GST_NONE,  0, "CARE",      "UI_AUTORETURN_MS" },
  { SCR_PLAY,      ERRK_NONE, XK_AUTORETURN, GST_TAP_R, 0, "PLAY",      "UI_AUTORETURN_MS" },
  { SCR_BOX,       ERRK_NONE, XK_AUTORETURN, GST_NONE,  0, "BOX",       "UI_AUTORETURN_MS" },
  { SCR_STATUS,    ERRK_NONE, XK_AUTORETURN, GST_TAP_R, 0, "STATUS",    "UI_AUTORETURN_MS" },
  { SCR_STATUS_B,  ERRK_NONE, XK_AUTORETURN, GST_TAP_R, 0, "STATUS_B",  "UI_AUTORETURN_MS" },
  { SCR_SETTINGS,  ERRK_NONE, XK_AUTORETURN, GST_NONE,  0, "SETTINGS",  "UI_AUTORETURN_MS" },
  { SCR_ENCOUNTER, ERRK_NONE, XK_AUTORETURN, GST_TAP_R, 0, "ENCOUNTER", "UI_AUTORETURN_MS" },
  { SCR_CAPTURE,   ERRK_NONE, XK_AUTORETURN, GST_NONE,  0, "CAPTURE",   "UI_AUTORETURN_MS" },
  // The four section 6 states that ship as ui/screen_soon.cpp placeholders.
  // None is SF_STICKY, so invariant 3 covers them - which is worth DRIVING
  // rather than assuming, because a placeholder given a flag by mistake would
  // be a screen with no implementation and no way off it.
  { SCR_TRADE,       ERRK_NONE, XK_AUTORETURN, GST_TAP_R, 0, "TRADE",       "UI_AUTORETURN_MS" },
  { SCR_BREED,       ERRK_NONE, XK_AUTORETURN, GST_TAP_R, 0, "BREED",       "UI_AUTORETURN_MS" },
  { SCR_ITEM_REWARD, ERRK_NONE, XK_AUTORETURN, GST_TAP_R, 0, "ITEM_REWARD", "UI_AUTORETURN_MS" },
  { SCR_SLEEP,       ERRK_NONE, XK_AUTORETURN, GST_TAP_R, 0, "SLEEP",       "UI_AUTORETURN_MS" },

  // --- THE RADIO WAITS (spec section 47's own subject) ---------------------
  // NETWORK: a scan in flight, driven to its own ceiling with a radio that
  // starts and never answers. The 20 s auto-return also applies to this row,
  // but the scan's 12 s bites first and it is the one that releases the radio.
  { SCR_NETWORK,   ERRK_NONE, XK_TIMEOUT, GST_NONE, (uint32_t)WIFI_SCAN_TIMEOUT_MS,
    "NETWORK (scan in flight)", "WIFI_SCAN_TIMEOUT_MS" },
  // LINK: browsing for a peer. Three ceilings stand over this screen and the
  // TIGHTEST is the one that fires - invariant 3's 20 s, ahead of
  // LINK_JOB_TIMEOUT_MS (90 s) and ahead of the session ladder's
  // PROTO_RETX_MAX x PROTO_RETX_MS. The row drives the one that actually ends
  // the wait; the other two are the backstops named beside it.
  { SCR_LINK,      ERRK_NONE, XK_AUTORETURN, GST_NONE, 0,
    "LINK (browsing)", "UI_AUTORETURN_MS, then LINK_JOB_TIMEOUT_MS" },
  // CREATOR: SF_STICKY since P8-C2, so invariant 3 is deliberately OFF here
  // (joining an access point takes longer than twenty seconds) and the screen
  // owns two ceilings of its own instead. This row drives the first; the D7
  // idle grace period has its own case above.
  { SCR_CREATOR,   ERRK_NONE, XK_TIMEOUT, GST_NONE, (uint32_t)CREATOR_AP_WAIT_MS,
    "CREATOR (access point never came up)", "CREATOR_AP_WAIT_MS, then ConfigV2.creator_idle_s" },

  // --- the sticky rows, whose exit is a gesture the ROUTER still delivers ---
  // SF_STICKY | SF_OWNS_BACK takes B away from the router and leaves invariant
  // 2 in place, so LONG_BOTH is a real, universal exit on all four.
  { SCR_GAME,      ERRK_NONE, XK_GESTURE, GST_LONG_BOTH, 0, "GAME",      "invariant 2, plus MGR_* phase clocks" },
  { SCR_BATTLE,    ERRK_NONE, XK_GESTURE, GST_LONG_BOTH, 0, "BATTLE",    "invariant 2, plus the session retransmit ladder" },
  { SCR_EVOLUTION, ERRK_NONE, XK_GESTURE, GST_LONG_BOTH, 0, "EVOLUTION", "invariant 2 (an egg has no timer by design)" },

  // --- the SF_LOCK_INPUT rows, where the router is OFF ----------------------
  // Each of these must answer the escape ITSELF. That is the property the
  // sweep below states in its own right, and the property ERRK_SAVE_NEWER
  // failed until P10-C2.
  { SCR_TIME,      ERRK_NONE, XK_GESTURE, GST_LONG_BOTH, 0, "TIME",      "handled by hand in time_input()" },
  // THE TWO FIRST-BOOT ROWS (P10-C4). They carry TIME's flags for TIME's
  // reasons, so they inherit TIME's obligation exactly: SF_LOCK_INPUT means the
  // router never runs, so each answers invariant 2 in its own input hook - and
  // on a setup screen that gesture also has to END THE FLOW, or the next boot
  // asks the same question again. Both halves are asserted in test_screens.cpp;
  // this row is the navigation half.
  { SCR_SETUP_NAME,    ERRK_NONE, XK_GESTURE, GST_LONG_BOTH, 0, "SETUP_NAME",
    "handled by hand in setup_name_input(), which also ends the flow" },
  { SCR_SETUP_STARTER, ERRK_NONE, XK_GESTURE, GST_LONG_BOTH, 0, "SETUP_STARTER",
    "handled by hand in setup_pick_input(), which also ends the flow" },
  { SCR_DIAG,      ERRK_NONE, XK_GESTURE, GST_TAP_R,     0, "DIAG",      "diag_input()/diag_update() go home once the console is off" },
  { SCR_ERROR, ERRK_SAVE_NEWER,  XK_GESTURE, GST_LONG_BOTH, 0,
    "ERROR/SAVE_NEWER",  "handled by hand in err_input() since P10-C2" },
  { SCR_ERROR, ERRK_SAVE_CORRUPT, XK_GESTURE, GST_LONG_BOTH, 0,
    "ERROR/SAVE_CORRUPT", "handled by hand in err_input() since P10-C2" },
  // ERRK_DISPLAY is deliberately NOT given the LONG_BOTH escape and this is
  // the one exemption in the table. The panel is what failed: there is nothing
  // to walk to and no way to read it, so its exit is A - the retry - and its
  // signal is the blinking LED. Driven as an ANSWER rather than waved through.
  { SCR_ERROR, ERRK_DISPLAY,     XK_ANSWER,  GST_TAP_L, 0,
    "ERROR/DISPLAY",     "A retries the panel (ui_bind_display_retry)" },

  // --- the two overlays ----------------------------------------------------
  // ui/dialog.cpp owns them and they never become sm_current(); their rows
  // exist so the table has no hole (ui/screen_soon.h). Their own waits - a
  // confirm and an alert have no timeout, deliberately, because a question
  // must not answer itself - are driven in the case below this one.
  { SCR_CONFIRM,   ERRK_NONE, XK_NOT_RESIDENT, GST_NONE, 0, "CONFIRM", "ui/dialog.cpp owns it" },
  { SCR_ALERT,     ERRK_NONE, XK_NOT_RESIDENT, GST_NONE, 0, "ALERT",   "ui/dialog.cpp owns it" },
};

// Drive one row and say, in words, what failed.
static bool exit_row_holds(const ExitRow& r) {
  reset_all();
  arm_row(r);

  switch (r.kind) {
    case XK_ROOT:
      // HOME is not exempt from the question: it is the ANSWER to it, and that
      // is only true while every other exit really lands here.
      if (sm_current() != SCR_HOME) return false;
      sm_push(SCR_MENU); sm_push(SCR_BOX);
      sm_home();
      return sm_current() == SCR_HOME;

    case XK_NOT_RESIDENT:
      // sm_begin() must not land on one, and nothing in the firmware may
      // navigate to one - the second half is tools/check.sh's, because a host
      // binary cannot see app/app.cpp or ui/ui.cpp.
      return sm_current() != (ScreenId)r.scr;

    case XK_AUTORETURN: {
      sm_push((ScreenId)r.scr);
      if (sm_current() != (ScreenId)r.scr) return false;
      host_advance_ms((uint32_t)UI_AUTORETURN_MS + 1u);
      (void)sm_service(host_ms());
      return sm_current() == SCR_HOME;
    }

    case XK_TIMEOUT: {
      sm_push((ScreenId)r.scr);
      if (sm_current() != (ScreenId)r.scr) return false;
      // INVARIANT 3 IS HELD OFF FOR THE WHOLE OF THIS ROW, so the only thing
      // that can end the wait is the ceiling the row NAMES. Without it a
      // NETWORK row whose WIFI_SCAN_TIMEOUT_MS was broken would pass on the
      // 20 s auto-return standing over the same screen - naming a bound
      // instead of checking it, which is this project's recurring defect in
      // the shape a test usually takes.
      sm_block_autoreturn(true);
      // In slices, because an update hook that only fires on the exact
      // millisecond would pass a single jump and fail on a device.
      const uint32_t slice = r.bound_ms / 8u + 1u;
      uint32_t waited = 0;
      for (uint8_t i = 0; i < 24u && sm_current() == (ScreenId)r.scr; ++i) {
        host_advance_ms(slice);
        waited += slice;
        (void)sm_service(host_ms());
      }
      sm_block_autoreturn(false);   // released on every path out of this arm
      if (sm_current() == (ScreenId)r.scr) return false;
      // AND IT WAS THIS ROW'S OWN CEILING THAT FIRED, not some other one that
      // happens to stand over the same screen.
      if (waited > r.bound_ms + slice) return false;
      // AND THE RADIO IS BACK OFF. A timeout that navigated away with the
      // antenna still up would satisfy every navigation check in this file and
      // leave spec section 40 broken.
      //
      // MEASURED LIMIT, REPORTED RATHER THAN DRESSED UP: breaking the timeout
      // arm's release ALONE does not fail this check, because
      // wifi_scan_cancel() carries a backstop that releases anyway and
      // network_leave() calls it on every route off the screen. Both had to be
      // removed before it fired. That is the scanner being right rather than
      // this check being weak - but it means the check catches a DOUBLE
      // failure, not a single one, and saying so is worth more than implying
      // otherwise.
      if (r.scr == SCR_NETWORK  && g_scan_stops < 1)        return false;
      if (r.scr == SCR_CREATOR  && g_creator_radio_last != 0) return false;
      return true;
    }

    case XK_GESTURE: {
      sm_push((ScreenId)r.scr);
      if (sm_current() != (ScreenId)r.scr) return false;
      (void)router_handle((Gesture)r.g);
      return sm_current() != (ScreenId)r.scr;
    }

    case XK_ANSWER: {
      sm_push((ScreenId)r.scr);
      if (sm_current() != (ScreenId)r.scr) return false;
      const int before = g_retry_calls + g_recover_calls + g_wipe;
      (void)router_handle((Gesture)r.g);
      // Either the answer moved the device, or it reached the seam that
      // resolves the question. Both are exits; a toast is not.
      return sm_current() != (ScreenId)r.scr ||
             (g_retry_calls + g_recover_calls + g_wipe) > before;
    }
  }
  return false;
}

TEST(every_state_and_every_radio_wait_has_a_way_out) {
  for (size_t i = 0; i < sizeof kExits / sizeof kExits[0]; ++i) {
    const ExitRow& r = kExits[i];
    if (!exit_row_holds(r))
      fprintf(stderr, "  %s: NO EXIT - the row claims %s and it did not fire\n",
              r.label, r.bound);
    CHECK(exit_row_holds(r));
  }
}

// THE ANTI-VACUITY CLAUSE. A table of answers proves nothing about a state it
// forgot, and "every state" is exactly the claim being made. A ScreenId with no
// row fails here by name, so a screen added in a later chunk arrives with an
// exit or does not arrive.
TEST(the_exit_table_covers_every_screen_id) {
  bool seen[SCR_COUNT];
  for (uint8_t i = 0; i < (uint8_t)SCR_COUNT; ++i) seen[i] = false;
  for (size_t i = 0; i < sizeof kExits / sizeof kExits[0]; ++i) {
    CHECK(kExits[i].scr < (uint8_t)SCR_COUNT);
    seen[kExits[i].scr] = true;
  }
  for (uint8_t i = 0; i < (uint8_t)SCR_COUNT; ++i) {
    if (!seen[i]) fprintf(stderr, "  %s: no row in kExits - spec section 47 is "
                                  "unanswered for this state\n", kName[i]);
    CHECK(seen[i]);
  }
  // And ERROR is covered for EVERY kind it can hold, not just for the one a
  // reader happened to think of. ERRK_SAVE_NEWER was the kind nobody thought
  // of: three kinds, three rows, and ERRK_NONE never puts the screen up.
  // P10-C6: DERIVED FROM THE ENUM, NOT FROM A LIST SOMEBODY TYPED. The three
  // were named in an array here and the assertion below counted ROWS IN kExits
  // - so adding a fourth ErrKind with no row left err_rows at 3, the array at
  // three members, and the whole suite green with GATE OK printed. The comment
  // said the assertion "is what fails if a fourth is added without a row"; it
  // was what fails if a fourth ROW is added, which is the harmless direction -
  // and it fired on the CORRECT fix rather than on the bug. ui/screen_error.h
  // has an ERRK_COUNT now and this walks it.
  for (uint8_t k = (uint8_t)ERRK_SAVE_CORRUPT; k < (uint8_t)ERRK_COUNT; ++k) {
    bool found = false;
    for (size_t i = 0; i < sizeof kExits / sizeof kExits[0]; ++i)
      if (kExits[i].scr == (uint8_t)SCR_ERROR && kExits[i].err == k) found = true;
    if (!found) fprintf(stderr, "  ERROR kind %u has no row in kExits - a state "
                                "the device can reach with no answer to spec 47\n",
                        (unsigned)k);
    CHECK(found);
  }
  uint8_t err_rows = 0;
  for (size_t i = 0; i < sizeof kExits / sizeof kExits[0]; ++i)
    if (kExits[i].scr == (uint8_t)SCR_ERROR) ++err_rows;
  // ERRK_NONE never puts the screen up, so the table holds one row per OTHER
  // kind. Both directions fail now: a kind with no row, and a row with no kind.
  CHECK_EQ((int)err_rows, (int)ERRK_COUNT - 1);
}

// THE SHARP HALF, AND THE ONE THE MUTATION RUN IS ABOUT. SF_LOCK_INPUT turns
// app/input_router.cpp OFF at its first line, so invariant 2's universal HOME
// escape does not exist on these rows - which means the sweep above could pass
// for a lock-input row only if the SCREEN answered the gesture itself. This
// case states that: the router really is off, and the row's exit is not the
// router's. Without it, deleting err_input()'s LONG_BOTH case could in
// principle be masked by an escape that was never there.
TEST(the_router_is_off_on_every_lock_input_row_so_each_answers_the_escape_itself) {
  uint8_t locked = 0;
  for (uint8_t i = 0; i < (uint8_t)SCR_COUNT; ++i) {
    if ((SCREENS[i].flags & SF_LOCK_INPUT) == 0u) continue;
    ++locked;

    // 1. the router is genuinely off on this row.
    reset_all();
    sm_push((ScreenId)i);
    if (router_global(GST_LONG_BOTH) || router_global(GST_TAP_R))
      fprintf(stderr, "  %s: SF_LOCK_INPUT but the router consumed a gesture\n",
              kName[i]);
    CHECK(!router_global(GST_LONG_BOTH));
    CHECK(!router_global(GST_TAP_R));
    CHECK(sm_current() == (ScreenId)i);

    // 2. and every row in the exit table for it answers WITHOUT the router.
    bool found = false;
    for (size_t k = 0; k < sizeof kExits / sizeof kExits[0]; ++k) {
      if (kExits[k].scr != i) continue;
      found = true;
      const uint8_t kk = kExits[k].kind;
      if (kk == XK_AUTORETURN)
        fprintf(stderr, "  %s: SF_LOCK_INPUT rows are all SF_STICKY too, so the "
                        "auto-return cannot be this row's exit\n", kExits[k].label);
      CHECK(kk != XK_AUTORETURN);
      CHECK(kk == XK_GESTURE || kk == XK_ANSWER || kk == XK_NOT_RESIDENT);
    }
    CHECK(found);

    // 3. every SF_LOCK_INPUT row is SF_STICKY as well, which is what makes (2)
    //    a real constraint: with both off, the row would have no ceiling at all.
    CHECK((SCREENS[i].flags & SF_STICKY) != 0u);
  }
  // BOOT, LOAD_SAVE, TIME, SETUP_NAME, SETUP_STARTER, ERROR, DIAG. 5 -> 7 at
  // P10-C4: the two first-boot screens carry SF_LOCK_INPUT for TIME's reason
  // (B means "change the thing under the cursor" there and BACK everywhere
  // else). One appearing without a row in kExits fails the coverage case above;
  // this pins the count so the set cannot shrink either.
  CHECK_EQ((int)locked, 7);
}

// THE OFF-TABLE WAITS. ui/dialog.cpp's modals float over a screen and freeze
// its auto-return (sm_block_autoreturn), so while one is open NOTHING times
// out - which is right for a question and is exactly why the question must
// always be answerable. MODAL_HELP is the one that expires on its own.
TEST(a_modal_freezes_the_screen_under_it_and_is_always_answerable) {
  reset_all();
  sm_push(SCR_MENU);
  dialog_open_confirm(CFM_WIPE1, STR_SOON_BODY);
  sm_block_autoreturn(true);
  host_advance_ms((uint32_t)UI_AUTORETURN_MS * 4u);
  CHECK(!sm_service(host_ms()));            // the screen underneath is frozen
  CHECK(dialog_modal() == MODAL_CONFIRM);

  // ...and B answers it. The cursor defaults to NO (invariant 5), so the
  // answer that costs nothing is the one a button lands on.
  CHECK(dialog_input(GST_TAP_R));
  sm_block_autoreturn(false);
  CHECK(dialog_modal() == MODAL_NONE);

  // The help overlay is the one wait that ends itself, and it must, because it
  // is raised by GST_BOTH on screens whose owner may have put the device down.
  reset_all();
  sm_push(SCR_MENU);
  dialog_open_help(STR_SOON_BODY);
  CHECK(dialog_modal() == MODAL_HELP);
  host_advance_ms((uint32_t)UI_MODAL_HELP_MS + 1u);
  (void)dialog_service(host_ms(), true);
  CHECK(dialog_modal() == MODAL_NONE);
}

// =============================================================================
//  THE GAME ROW (ui.h seams, until P3-C4)
// =============================================================================
TEST(game_row_dispatches) {
  reset_all();
  sm_push(SCR_GAME);
  CHECK(g_game_enters == 1);
  CHECK(sm_draw());
  CHECK(g_game_renders == 1);
  CHECK(router_handle(GST_TAP_R));       // SF_OWNS_BACK: the screen sees B
  CHECK(g_game_inputs == 1);
  CHECK(sm_current() == SCR_GAME);
  sm_service(host_ms());
  CHECK(g_game_updates == 1);
  sm_home();
  CHECK(g_game_leaves == 1);
}
