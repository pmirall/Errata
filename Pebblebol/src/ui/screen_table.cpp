// =============================================================================
//  PEBBLEBOL - ui/screen_table.cpp
//  THE table. One row per ScreenId, in enum order.
//
//  A row with a render hook is a MIGRATED screen: app/state_machine.cpp
//  dispatches enter/update/render/input/leave straight to it and ui.cpp's three
//  old switches never see that id again. A zeroed row is a screen still living
//  in ui.cpp; screen_def() returns NULL for it and the legacy path runs.
//
//  P2-C11a migrated BOOT, LOAD_SAVE and ERROR. The remaining rows fill in one
//  commit at a time until the switches are empty (plan P2-C11, grep exit gate).
//
//  PURE translation unit: it names hooks, it does not draw.
// =============================================================================
#include "screen.h"

#include "screen_boot.h"
#include "screen_error.h"

// Positional initialisers, in this order:
//   enter, update, render, input, leave, fps, flags
const ScreenDef SCREENS[SCR_COUNT] = {
  { nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0 },   // SCR_HOME
  { nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0 },   // SCR_MENU
  { nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0 },   // SCR_FEED
  { nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0 },   // SCR_PLAY
  { nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0 },   // SCR_GAME
  { nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0 },   // SCR_STATUS_A
  { nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0 },   // SCR_STATUS_B
  { nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0 },   // SCR_SOCIAL
  { nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0 },   // SCR_SETTINGS
  { nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0 },   // SCR_CONFIRM
  { nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0 },   // SCR_ALERT
  { nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0 },   // SCR_EGG
  { nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0 },   // SCR_GOD
  { nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0 },   // SCR_QR
  { nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0 },   // SCR_CLOCK

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
