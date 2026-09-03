// =============================================================================
//  PEBBLEBOL - ui/screen.h
//  THE SCREEN TABLE (plan P2-C11, section 1.4).
//
//  One row per screen, five hooks and two bytes of policy. app/state_machine.cpp
//  dispatches through it; ui.cpp keeps its three old switches only for the
//  screens that have not moved yet. That is the strangler: a screen is
//  "migrated" exactly when its row has a render hook, and screen_def() returns
//  NULL for every other id so the caller falls through to the legacy path.
//
//  A migrated screen is a PURE translation unit: it includes gfx.h, never
//  render.h or Arduino.h, and is therefore compiled and snapshot-tested on the
//  host by tests/test_screens.cpp.
//
//  Identifiers and comments: English. User-facing bytes: core/strings_es.h.
// =============================================================================
#ifndef NT_SCREEN_H
#define NT_SCREEN_H

#include <stdint.h>

#include "../core/config.h"
#include "../core/nt_types.h"

// -----------------------------------------------------------------------------
//  FLAGS
// -----------------------------------------------------------------------------
// No 20 s auto-return (invariant 3). HOME, a running minigame, the ERROR
// screen: places where being dropped somewhere else would be wrong.
#define SF_STICKY       0x01u
// The screen owns EVERY gesture: the global navigation grammar (LONG_BOTH =
// HOME, TAP_R = BACK) is not applied before its input hook runs. ERROR uses
// it to hold the device on an unanswered question; BOOT and LOAD_SAVE use it
// because they are not waiting on a button at all.
#define SF_LOCK_INPUT   0x02u
// The screen draws the whole frame, including its own affordance strip and
// without the toast / modal / dissolve layers on top (god mode, the hatch
// ceremony). ui_draw() returns immediately after the render hook.
#define SF_OWNS_FRAME   0x04u
// The screen handles B (GST_TAP_R) itself: app/input_router.cpp does NOT turn
// it into sm_back(). For screens whose B means something one level down from
// the navigation stack - the BOX walking back through its own modes, SETTINGS
// closing its info page, EVOLUTION rubbing the egg with both buttons, a
// running minigame that must not be left by an accidental tap. LONG_BOTH still
// goes HOME: this is narrower than SF_LOCK_INPUT on purpose.
#define SF_OWNS_BACK    0x08u

// -----------------------------------------------------------------------------
//  THE ROW
//
//  ALL FIVE HOOKS ARE NON-NULL, on every row, and tests/test_statemachine.cpp
//  is what holds that. Spec section 6 says "every state must define enter,
//  update, render, handleInput, exit"; a table with holes in it is a switch
//  with a default branch wearing a table's clothes. A state that needs nothing
//  on the way in says so with an empty function (ui/screen_soon.cpp), not with
//  a null pointer every caller has to test. The strangler that let a row be
//  half-filled ended with P2-C11d: there is no legacy path left to fall
//  through to.
//    enter()  - the screen is now on top. Cursors, radio requests, timers.
//    update() - once per loop(), with the presentation clock. Never draws.
//    render() - one frame, through gfx.h only.
//    input()  - exactly one gesture.
//    leave()  - the screen is going away. Release whatever enter() took.
//    fps      - the rate this screen wants (0 = let ui_fps() decide).
//    flags    - SF_* above.
// -----------------------------------------------------------------------------
struct ScreenDef {
  void    (*enter)(void);
  void    (*update)(uint32_t now_ms);
  void    (*render)(void);
  void    (*input)(Gesture g);
  void    (*leave)(void);
  uint8_t fps;
  uint8_t flags;
};

// Defined once, in ui/screen_table.cpp.
extern const ScreenDef SCREENS[SCR_COUNT];

// The row for s. NULL only for an id outside the enum, which is a caller bug
// rather than an unmigrated screen: every id in the enum has a row.
inline const ScreenDef* screen_def(uint8_t s) {
  if (s >= (uint8_t)SCR_COUNT) return nullptr;
  return &SCREENS[s];
}

// -----------------------------------------------------------------------------
//  SHARED SCREEN GEOMETRY
//  Lived in ui.cpp until the table landed; a migrated screen may not include
//  render.h, so the numbers move here where both sides can see them (ui.cpp
//  asserts UI_AFFORD_Y against RD_AFFORD_Y).
// -----------------------------------------------------------------------------
#define UI_HDR_H            11                        // inverted title bar
#define UI_HDR_BASE          9                        // its text baseline
#define UI_AFFORD_Y         (OLED_H - AFFORDANCE_BAR_H)   // 56
#define UI_CONTENT_BOTTOM   (UI_AFFORD_Y - 1)             // 55

#endif  // NT_SCREEN_H
