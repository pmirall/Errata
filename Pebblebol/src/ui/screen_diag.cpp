// =============================================================================
//  PEBBLEBOL - ui/screen_diag.cpp
//  DIAG, migrated by P2-C11c. PURE translation unit: the console it hosts is
//  reached through three bound function pointers and nothing else.
// =============================================================================
#include "screen_diag.h"

#include "../core/strings_es.h"
#include "gfx.h"
#include "screen.h"
#include "ui.h"

static DiagActiveFn s_active = nullptr;
static DiagDrawFn   s_draw   = nullptr;
static DiagInputFn  s_input  = nullptr;

void diag_bind(DiagActiveFn active, DiagDrawFn draw, DiagInputFn input) {
  s_active = active;
  s_draw   = draw;
  s_input  = input;
}

static bool console_on(void) { return s_active && s_active(); }

// A console that switched itself off must not keep the screen: the user is
// looking at a frame nobody is drawing any more.
void diag_update(uint32_t now_ms) {
  (void)now_ms;
  if (!console_on()) ui_home();
}

void diag_render(void) {
  if (console_on() && s_draw) { s_draw(); return; }

  // The console is off, or this is the host. Draw the frame that says so
  // rather than an empty panel: SF_OWNS_FRAME means nothing else will.
  gfx_header(S(STR_DIAG_TITLE), nullptr);
  gfx_text_center(GF_BODY, 34, S(STR_DIAG_OFF));
  gfx_affordance(nullptr, S(STR_AF_BACK));
}

void diag_input(Gesture g) {
  // godmode.h: "Never call this when god_active() is false." The console's own
  // exit row turns the MODE off from inside a previous handler, so the screen
  // can outlive it by exactly one gesture.
  if (!console_on()) { ui_home(); return; }
  if (s_input && s_input(g)) ui_home();
}
