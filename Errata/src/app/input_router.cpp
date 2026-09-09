// =============================================================================
//  ERRATA - app/input_router.cpp
//  See input_router.h for the grammar table this implements.
// =============================================================================
#include "input_router.h"

#include "state_machine.h"

static inline uint8_t flags_now(void) {
  const ScreenDef* d = sm_def();
  return d ? d->flags : 0u;
}

bool router_global(Gesture g) {
  const uint8_t f = flags_now();
  if (f & SF_LOCK_INPUT) return false;          // the row owns every gesture

  const ScreenId scr = sm_current();

  // --- invariant 2: HOME from anywhere ---------------------------------------
  // Except on HOME, where the gesture is free and the screen spends it on
  // SETTINGS.
  if (g == GST_LONG_BOTH) {
    if (scr == SCR_HOME) return false;
    sm_home();
    return true;
  }

  // --- invariant 1: B is BACK on every screen --------------------------------
  //
  // ON THE HOLD, NOT ON THE TAP, SINCE THE FIRST HARDWARE SESSION. See the
  // banner in input_router.h for the whole argument; the short version is that
  // the owner played it on a board and the obvious button went backwards.
  if (g == GST_HOLD_R) {
    if (f & SF_OWNS_BACK) return false;
    if (scr == SCR_HOME)  return false;         // the root: nothing to cancel
    sm_back();
    return true;
  }

  return false;
}

bool router_handle(Gesture g) {
  if (g == GST_NONE) return false;
  if (router_global(g)) return true;
  return sm_handle(g);
}
