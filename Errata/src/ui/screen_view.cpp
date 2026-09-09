// =============================================================================
//  ERRATA - ui/screen_view.cpp
//  The one indirection behind screen_view.h. See that header for what the view
//  is and why the screens read it instead of the simulation.
//
//  PURE translation unit.
// =============================================================================
#include "screen_view.h"

static BugViewFn s_fn = nullptr;

void ui_bind_view(BugViewFn fn) { s_fn = fn; }

const BugView* ui_view(void) { return s_fn ? s_fn() : nullptr; }
