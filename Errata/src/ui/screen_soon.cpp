// =============================================================================
//  ERRATA - ui/screen_soon.cpp
//  The placeholder frame. See screen_soon.h for why every unimplemented state
//  gets a real row instead of a hole in the table.
//
//  PURE translation unit.
// =============================================================================
#include "screen_soon.h"

#include "../core/strings_es.h"
#include "gfx.h"
#include "screen.h"
#include "ui.h"

// -----------------------------------------------------------------------------
//  The frame. Title, the phase that brings the state to life, one wrapped line
//  of what it will be, and the ordinary chrome: a countdown (none of these is
//  SF_STICKY, so invariant 3 applies and the bar must say so) and BACK.
// -----------------------------------------------------------------------------
static void frame(uint16_t title, uint16_t phase) {
  gfx_header(S(title), nullptr);
  gfx_text_center(GF_NARR, (int16_t)(UI_HDR_H + 12), S(STR_SOON_TITLE));
  gfx_text_center(GF_BODY, (int16_t)(UI_HDR_H + 23), S(phase));
  gfx_text_wrap(GF_BODY, 4, (int16_t)(UI_HDR_H + 33), OLED_W - 8,
                GFX_LINE_BODY, 2, S(STR_SOON_BODY));
  gfx_countdown(ui_idle_ms());
  gfx_affordance(nullptr, S(STR_AF_BACK));
}

// soon_battle() IS GONE, and so are soon_network(), soon_encounter() and
// soon_capture(). P4-C4 gave SCR_BATTLE a real screen and P5-C3/C4 gave the
// other three theirs, so each line stopped being reachable the moment its table
// row moved - and an unreferenced non-static function raises no warning, so
// nothing would ever have failed on account of one. STR_PHASE_4, STR_PHASE_5,
// STR_SOON_NETWORK, STR_SOON_ENCOUNTER and STR_SOON_CAPTURE are now strings in
// core/strings_es.h with no consumer; they stay because deleting an id
// renumbers the generated block behind it.
void soon_trade(void)       { frame(STR_SOON_TRADE,     STR_PHASE_7);  }
void soon_breed(void)       { frame(STR_SOON_BREED,     STR_PHASE_7);  }
void soon_item_reward(void) { frame(STR_SOON_ITEM,      STR_PHASE_6);  }
void soon_sleep(void)       { frame(STR_SOON_SLEEP,     STR_PHASE_10); }
void soon_generic(void)     { frame(STR_SOON_TITLE,     STR_PHASE_7);  }

// Nothing to take, nothing to run, nothing to release. They are here because
// the table row names all five hooks: a screen that needs none of them says so
// with an empty function, not with a null pointer the dispatcher has to test.
void nop_enter(void) {}
void nop_update(uint32_t) {}
void nop_leave(void) {}
void nop_input(Gesture) {}

// BACK and HOME are the router's (app/input_router.cpp) and never reach here.
void soon_input(Gesture g) {
  if (g == GST_BOTH) ui_help(STR_SOON_BODY);
}
