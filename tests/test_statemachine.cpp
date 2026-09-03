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
#include "ui/screen_box.h"
#include "ui/screen_care.h"
#include "ui/screen_creator.h"
#include "ui/screen_diag.h"
#include "ui/screen_error.h"
#include "ui/screen_evolution.h"
#include "ui/screen_home.h"
#include "ui/screen_link.h"
#include "ui/screen_menu.h"
#include "ui/screen_settings.h"
#include "ui/screen_soon.h"
#include "ui/screen_status.h"
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
static Config   g_cfg;

void ui_toast(uint16_t id)        { g_toast = id; }
void ui_help(uint16_t id)         { g_help = id; }
void ui_goto(ScreenId s)          { sm_goto(s); }
void ui_push(ScreenId s)          { sm_push(s); }
void ui_back(void)                { sm_back(); }
void ui_home(void)                { sm_home(); }
void ui_confirm_wipe(void)        { g_wipe++; }
void ui_note_recovered(void)      { }
void ui_note_input(void)          { sm_note_input(); }
void ui_request_frame(void)       { g_frames++; }
void ui_wiggle(void)              { g_wiggles++; }
uint32_t ui_now_ms(void)          { return host_ms(); }
uint32_t ui_idle_ms(void)         { return sm_idle_ms(); }
Config*  ui_cfg(void)             { return &g_cfg; }
void ui_cfg_changed(void)         { }
void ui_apply_brightness(uint8_t) { }
bool ui_do_action(uint8_t)        { return true; }
bool ui_act_and_show(uint8_t)     { return true; }
void ui_repeat_last_action(void)  { }
void ui_confirm_medicine(void)    { }
void ui_start_minigame(uint8_t)   { }
uint8_t ui_god_progress(void)     { return 0; }
void ui_input_flush(void)         { }
void ui_request_hatch(void)       { }
void ui_creator_radio(bool)       { }
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

void ui_creator_info(CreatorInfo& out) { memset(&out, 0, sizeof out); }
void ui_info_lines(char lines[UI_INFO_LINES][UI_INFO_CAP]) {
  for (uint8_t i = 0; i < UI_INFO_LINES; ++i) lines[i][0] = '\0';
}
bool ui_get_clock(uint16_t*, uint8_t*, uint8_t*, uint8_t*, uint8_t*) { return false; }
bool ui_set_clock(uint16_t, uint8_t, uint8_t, uint8_t, uint8_t)      { return true; }

// The two ui.cpp halves of a screen change the machine still calls out to.
void ui_nav_reset(void)        { dialog_close(); gfx_list_reset(); }
void ui_nav_arrived(uint8_t)   { g_frames++; }

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
  memset(&g_cfg, 0, sizeof g_cfg);
  dialog_reset();
  dialog_bind_commit(nullptr);
  diag_bind(nullptr, nullptr, nullptr);
  evo_bind_ceremony(nullptr);
  ui_bind_view(nullptr);
  err_set_kind(ERRK_NONE);
  sm_begin();
}

// =============================================================================
//  1. TABLE COMPLETENESS
// =============================================================================
static const char* kName[SCR_COUNT] = {
  "BOOT", "LOAD_SAVE", "HOME", "MENU", "CARE", "PLAY", "GAME", "BOX",
  "STATUS", "STATUS_B", "NETWORK", "LINK", "CREATOR", "SETTINGS", "TIME",
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
    soon_network, link_render, creator_render, settings_render, time_render,
    soon_generic, soon_generic, soon_encounter, soon_capture, soon_battle,
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

  // Nothing else is ever consumed globally: A, its hold, the double taps and
  // BOTH all belong to the screen.
  reset_all();
  sm_push(SCR_MENU);
  const Gesture pass[6] = { GST_TAP_L, GST_HOLD_L, GST_HOLD_R,
                            GST_DBL_L, GST_DBL_R, GST_BOTH };
  for (uint8_t i = 0; i < 6; ++i) CHECK(!router_global(pass[i]));
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
