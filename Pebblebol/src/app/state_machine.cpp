// =============================================================================
//  PEBBLEBOL - app/state_machine.cpp
//  The navigation state machine, lifted out of ui.cpp (ui_goto + nav_push /
//  nav_back / nav_home) unchanged in behaviour and given a table to dispatch
//  through. See state_machine.h for the contract.
//
//  No Arduino header: the only platform call is the millisecond clock, behind
//  the same shim hardware/input.cpp uses, so this file compiles on the host.
// =============================================================================
#include "state_machine.h"

#include "../ui/ui.h"

#if defined(ARDUINO)
  #include <Arduino.h>
  static inline uint32_t sm_now(void) { return (uint32_t)millis(); }
#else
  extern "C" uint32_t nt_input_test_millis(void);   // fake clock, milliseconds
  static inline uint32_t sm_now(void) { return nt_input_test_millis(); }
#endif

static uint8_t  s_screen    = SCR_HOME;
static uint8_t  s_stack[UI_STACK_DEPTH];
static uint8_t  s_sp        = 0;
static uint32_t s_enter_ms  = 0;
static uint32_t s_input_ms  = 0;
static uint8_t  s_block_ar  = 0;

static inline uint32_t since(uint32_t t0) { return (uint32_t)(sm_now() - t0); }

// -----------------------------------------------------------------------------
//  Screens that never time out (invariant 3) and are NOT in the table yet. The
//  list shrinks to nothing as they migrate; SF_STICKY is the real answer.
// -----------------------------------------------------------------------------
static bool legacy_sticky(uint8_t s) {
  return s == SCR_HOME || s == SCR_GAME || s == SCR_EGG || s == SCR_GOD;
}

bool sm_is_sticky(void) {
  const ScreenDef* d = screen_def(s_screen);
  if (d) return (d->flags & SF_STICKY) != 0u;
  return legacy_sticky(s_screen);
}

ScreenId sm_current(void)       { return (ScreenId)s_screen; }
const ScreenDef* sm_def(void)   { return screen_def(s_screen); }
uint32_t sm_idle_ms(void)       { return since(s_input_ms); }
uint32_t sm_screen_ms(void)     { return since(s_enter_ms); }
void     sm_note_input(void)    { s_input_ms = sm_now(); }
void     sm_block_autoreturn(bool blocked) { s_block_ar = blocked ? 1u : 0u; }

void sm_begin(void) {
  for (uint8_t i = 0; i < UI_STACK_DEPTH; i++) s_stack[i] = 0;
  s_sp       = 0;
  s_screen   = SCR_HOME;
  s_enter_ms = sm_now();
  s_input_ms = s_enter_ms;
  s_block_ar = 0;
}

void sm_goto(ScreenId s) {
  if (s >= SCR_COUNT) return;
  if (s_screen != (uint8_t)s) {
    const ScreenDef* from = screen_def(s_screen);
    if (from) { if (from->leave) from->leave(); }
    else      { ui_nav_leave(s_screen); }
  }
  s_screen   = (uint8_t)s;
  s_enter_ms = sm_now();
  s_input_ms = s_enter_ms;
  // Every interpolator that outlives a screen has to be cut here, because the
  // list and carousel widgets are shared across screens: otherwise the list
  // highlight flies in from the previous list's row and the carousel finishes
  // a step that belonged to a menu the player already left. It also closes any
  // modal, which is why ui.cpp owns it.
  ui_nav_reset();
  const ScreenDef* to = screen_def(s_screen);
  if (to) { if (to->enter) to->enter(); }
  else    { ui_nav_enter(s_screen); }
  // Entry dissolve, frame rate, frame request: presentation, still ui.cpp's.
  ui_nav_arrived(s_screen);
}

void sm_push(ScreenId s) {
  if (s_sp < UI_STACK_DEPTH) s_stack[s_sp++] = s_screen;
  sm_goto(s);
}

void sm_back(void) {
  const uint8_t to = (s_sp > 0) ? s_stack[--s_sp] : (uint8_t)SCR_HOME;
  sm_goto((ScreenId)to);
}

void sm_home(void) { s_sp = 0; sm_goto(SCR_HOME); }

void sm_replace_root(ScreenId s) { s_sp = 0; sm_goto(s); }

bool sm_service(uint32_t now_ms) {
  const ScreenDef* d = screen_def(s_screen);
  if (d && d->update) d->update(now_ms);

  // Invariant 3: 20 s without a gesture returns to HOME, except where the
  // screen says otherwise.
  if (s_block_ar || sm_is_sticky()) return false;
  if ((uint32_t)(now_ms - s_input_ms) < UI_AUTORETURN_MS) return false;
  sm_home();
  return true;
}

bool sm_handle(Gesture g) {
  const ScreenDef* d = screen_def(s_screen);
  if (!d || !d->input) return false;
  d->input(g);
  return true;
}

bool sm_draw(void) {
  const ScreenDef* d = screen_def(s_screen);
  if (!d) return false;
  d->render();
  return true;
}
