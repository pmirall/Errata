// =============================================================================
//  ERRATA - app/state_machine.cpp
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

// Invariant 3 is a table answer now: the last screen that needed a hard-coded
// exception (GAME) got its own row in P2-C11d.
bool sm_is_sticky(void) {
  const ScreenDef* d = screen_def(s_screen);
  return d && (d->flags & SF_STICKY) != 0u;
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
    if (from) from->leave();
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
  if (to) to->enter();
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
  if (d) d->update(now_ms);

  // Invariant 3: 20 s without a gesture returns to HOME, except where the
  // screen says otherwise.
  //
  // *** since(s_input_ms), NOT (now_ms - s_input_ms), AND THE DIFFERENCE COST AN
  //     EVENING ON A BOARD. ***
  //
  // The two are the same clock and they are NOT the same READING. `now_ms` is
  // the frame stamp ui.cpp sampled at the top of ui_service(), ninety-odd lines
  // and one care tick ago; s_input_ms is stamped with sm_now() by sm_goto(),
  // which the update hook above may have just run. So on any tick where a
  // screen NAVIGATES FROM ITS OWN update() hook, s_input_ms is a few
  // milliseconds AHEAD of now_ms - and `(uint32_t)(now_ms - s_input_ms)` is
  // then not a small number, it is four billion. Which is >= UI_AUTORETURN_MS.
  // So the freshly pushed screen was sent straight to HOME on the same tick
  // that pushed it, with the back stack cleared and no message.
  //
  // MEASURED ON HARDWARE, not reasoned about: ui/screen_network.cpp is the only
  // screen in the tree that navigates from update() (its scan answers on a
  // frame, not on a press), and every wild encounter on a real board vanished
  // into HOME. The console said "16 seen / 1 fresh, phase=4" - the roll had
  // happened and ui_push(SCR_ENCOUNTER) had been called - and the panel showed
  // the pet. The gap between those two facts is this line.
  //
  // AND NO HOST TEST COULD HAVE CAUGHT IT while the comparison read a caller's
  // parameter: on the host sm_now() is the fake clock, which does not advance
  // during an update hook, so now_ms and s_input_ms were always equal and the
  // subtraction was always 0. tests/test_statemachine.cpp now drives the two
  // readings apart on purpose, which is the only way this can fail again.
  //
  // The parameter is still handed to the update hook, because a screen wants
  // the frame's stamp. It just may not be the thing an internal deadline is
  // measured against.
  //
  // IS IT THE ONLY ONE? Checked, not assumed. Nineteen other sites in src/ui
  // compare an update hook's `now_ms` against a stamp of their own, and the
  // three that could underflow the same way - the R-button auto-repeat in
  // screen_setup (x2) and screen_time - are safe for a reason that belongs to
  // app/app.cpp and not to them: it dispatches input in step 1 and calls
  // ui_service() in step 3, so a stamp written by an input() hook is always
  // EARLIER than the frame stamp sampled after it. The rest (the battle beat
  // clock, the creator rebuild throttle, the scan spinner) write their stamp
  // inside update() but read it on the NEXT pass, where now_ms has moved on.
  // sm_service() was the only one that wrote a stamp and then read it in the
  // same call - which is what made a few milliseconds decide a navigation.
  if (s_block_ar || sm_is_sticky()) return false;
  if (since(s_input_ms) < UI_AUTORETURN_MS) return false;
  sm_home();
  return true;
}

bool sm_handle(Gesture g) {
  const ScreenDef* d = screen_def(s_screen);
  if (!d) return false;
  d->input(g);
  return true;
}

bool sm_draw(void) {
  const ScreenDef* d = screen_def(s_screen);
  if (!d) return false;
  d->render();
  return true;
}
