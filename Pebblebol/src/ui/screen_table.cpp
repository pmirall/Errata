// =============================================================================
//  PEBBLEBOL - ui/screen_table.cpp
//  THE table. One row per ScreenId, in enum order.
//
//  A row with a render hook is a MIGRATED screen: app/state_machine.cpp
//  dispatches enter/update/render/input/leave straight to it and ui.cpp's three
//  old switches never see that id again. A zeroed row is a screen still living
//  in ui.cpp; screen_def() returns NULL for it and the legacy path runs.
//
//  P2-C11a migrated BOOT, LOAD_SAVE and ERROR; P2-C11b added HOME, MENU, the
//  two CARE lists, both STATUS pages, SETTINGS and TIME. The remaining rows
//  fill in one commit at a time until the switches are empty (plan P2-C11,
//  grep exit gate).
//
//  PURE translation unit: it names hooks, it does not draw.
// =============================================================================
#include "screen.h"

#include "screen_boot.h"
#include "screen_care.h"
#include "screen_error.h"
#include "screen_home.h"
#include "screen_menu.h"
#include "screen_settings.h"
#include "screen_status.h"
#include "screen_time.h"

// Positional initialisers, in this order:
//   enter, update, render, input, leave, fps, flags
const ScreenDef SCREENS[SCR_COUNT] = {
  // HOME is where the auto-return goes, so it never times out: SF_STICKY, and
  // no countdown bar. Its leave hook ends the action choreography and releases
  // the petfx hold, which ui.cpp's screen_leave(SCR_HOME) used to do.
  { nullptr, nullptr, home_render, home_input, home_leave, 0,
    SF_STICKY },                                                  // SCR_HOME
  { menu_enter, nullptr, menu_render, menu_input, nullptr, 0, 0 },// SCR_MENU
  { care_enter, nullptr, care_render, care_input, nullptr, 0, 0 },// SCR_FEED (CARE)
  { play_enter, nullptr, play_render, play_input, nullptr, 0, 0 },// SCR_PLAY
  { nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0 },          // SCR_GAME
  { status_a_enter, nullptr, status_a_render, status_input, nullptr, 0, 0 },
                                                                  // SCR_STATUS_A
  { status_b_enter, nullptr, status_b_render, status_input, nullptr, 0, 0 },
                                                                  // SCR_STATUS_B
  { nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0 },          // SCR_SOCIAL
  { settings_enter, nullptr, settings_render, settings_input, nullptr, 0, 0 },
                                                                  // SCR_SETTINGS
  { nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0 },   // SCR_CONFIRM
  { nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0 },   // SCR_ALERT
  { nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0 },   // SCR_EGG
  { nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0 },   // SCR_GOD
  { nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0 },   // SCR_QR
  // TIME owns every gesture (SF_LOCK_INPUT), because HOLD_R means "+1" here
  // and BACK everywhere else, and it never times out (SF_STICKY), because
  // throwing away a half-entered date after 20 s of thinking would be wrong.
  // Its update hook drives the right button's auto-repeat.
  { time_enter, time_update, time_render, time_input, nullptr, 0,
    SF_STICKY | SF_LOCK_INPUT },                                  // SCR_CLOCK

  // --- migrated (P2-C11a) ---------------------------------------------------
  // BOOT and LOAD_SAVE take no input at all: they are frames the save pipeline
  // shows while it works, so SF_LOCK_INPUT keeps the navigation grammar off
  // them and SF_STICKY keeps the 20 s auto-return off a screen that is only
  // ever up for as long as the read takes.
  { nullptr, nullptr, boot_render,      nullptr,   nullptr,   0,
    SF_STICKY | SF_LOCK_INPUT },                                  // SCR_BOOT
  { nullptr, nullptr, load_save_render, nullptr,   nullptr,   0,
    SF_STICKY | SF_LOCK_INPUT },                                  // SCR_LOAD_SAVE
  // ERROR holds the device on an unanswered question, so both flags again -
  // and FPS_LOW, because nothing on it moves and a blinking LED is driven by
  // its update hook, not by the frame rate.
  { err_enter, err_update, err_render, err_input, err_leave, FPS_LOW,
    SF_STICKY | SF_LOCK_INPUT },                                  // SCR_ERROR
};

// If the enum is renumbered (plan P2-C11, the spec section 6 order) this fires
// and the rows above must be re-ordered with it.
static_assert((int)SCR_COUNT == 18, "screen table: rows and ScreenId drifted apart");
static_assert((int)SCR_ERROR == 17, "screen table: the migrated rows moved");
