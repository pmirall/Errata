// =============================================================================
//  PEBBLEBOL - ui/dialog.cpp
//  The modal layer, migrated by P2-C11c. PURE translation unit: gfx.h, the
//  strings and the ui.h seams, no render.h and no Arduino.h.
// =============================================================================
#include "dialog.h"

#include "../core/strings_es.h"
#include "../data/sprites.h"
#include "gfx.h"
#include "screen.h"
#include "ui.h"

// ---- state ------------------------------------------------------------------
static uint8_t  s_modal       = MODAL_NONE;
static uint32_t s_modal_ms    = 0;
static uint16_t s_modal_str   = STR_EMPTY;
static uint8_t  s_confirm_id  = CFM_NONE;
static uint8_t  s_confirm_yes = 0;          // invariant 5: every dialog starts on NO

static uint8_t  s_alert_q[UI_ALERT_QUEUE];
static uint8_t  s_alert_n     = 0;
static uint8_t  s_alert_cur   = AL_NONE;

static DialogCommitFn s_commit = nullptr;

void dialog_bind_commit(DialogCommitFn fn) { s_commit = fn; }

uint8_t dialog_modal(void)       { return s_modal; }
uint8_t dialog_confirm_id(void)  { return s_confirm_id; }
uint8_t dialog_alert_id(void)    { return s_alert_cur; }
uint8_t dialog_confirm_yes(void) { return s_confirm_yes; }
uint8_t dialog_alert_pending(void) { return s_alert_n; }

// -----------------------------------------------------------------------------
//  OPEN / CLOSE
// -----------------------------------------------------------------------------
void dialog_close(void) {
  s_modal      = MODAL_NONE;
  s_alert_cur  = AL_NONE;
  s_confirm_id = CFM_NONE;
  s_modal_str  = STR_EMPTY;
}

void dialog_reset(void) {
  dialog_close();
  s_alert_n = 0;
}

void dialog_open_confirm(uint8_t confirm_id, uint16_t str_id) {
  s_confirm_id  = confirm_id;
  s_modal_str   = str_id;
  s_confirm_yes = 0;                 // invariant 5
  s_modal       = MODAL_CONFIRM;
  s_modal_ms    = ui_now_ms();
  ui_request_frame();
}

void dialog_open_help(uint16_t str_id) {
  s_modal_str = str_id;
  s_modal     = MODAL_HELP;
  s_modal_ms  = ui_now_ms();
}

void dialog_alert(uint8_t a) {
  if (a == AL_NONE || a >= (uint8_t)AL_COUNT) return;
  if (s_alert_cur == a) return;
  for (uint8_t i = 0; i < s_alert_n; ++i) if (s_alert_q[i] == a) return;
  if (s_alert_n >= UI_ALERT_QUEUE) return;
  s_alert_q[s_alert_n++] = a;
}

// -----------------------------------------------------------------------------
//  PUMP
// -----------------------------------------------------------------------------
bool dialog_service(uint32_t now_ms, bool allow_pop) {
  if (s_modal == MODAL_HELP && (uint32_t)(now_ms - s_modal_ms) >= UI_MODAL_HELP_MS) {
    dialog_close();
  }
  if (s_modal != MODAL_NONE || s_alert_n == 0 || !allow_pop) return false;

  s_alert_cur = s_alert_q[0];
  for (uint8_t i = 1; i < s_alert_n; ++i) s_alert_q[i - 1] = s_alert_q[i];
  --s_alert_n;
  s_modal    = MODAL_ALERT;
  s_modal_ms = now_ms;
  ui_request_frame();
  return true;
}

// -----------------------------------------------------------------------------
//  DRAWING
// -----------------------------------------------------------------------------
static void draw_confirm(void) {
  const int16_t y = 12, h = 40;
  gfx_fill(4, y, OLED_W - 8, h);
  gfx_color(GFX_ERASE);
  gfx_rect(5, (int16_t)(y + 1), OLED_W - 10, (int16_t)(h - 2));
  gfx_text_wrap(GF_BODY, 9, (int16_t)(y + 11), OLED_W - 18, GFX_LINE_BODY, 2,
                S(s_modal_str));
  gfx_color(GFX_DRAW);

  const int16_t by = (int16_t)(y + h - 14);
  for (uint8_t i = 0; i < 2; ++i) {
    const bool    yes = (i == 1);
    const int16_t bx  = (int16_t)(yes ? 68 : 14);
    const bool    sel = (yes == (s_confirm_yes != 0));
    const char*   lbl = S(yes ? STR_YES : STR_NO);
    gfx_color(GFX_ERASE);
    gfx_fill(bx, by, 46, 12);
    gfx_color(GFX_DRAW);
    if (sel) gfx_fill((int16_t)(bx + 1), (int16_t)(by + 1), 44, 10);
    gfx_color(sel ? GFX_ERASE : GFX_DRAW);
    {
      const int16_t w = (int16_t)gfx_text_w(GF_HEAD, lbl);
      gfx_text(GF_HEAD, (int16_t)(bx + 1 + (44 - w) / 2), (int16_t)(by + 9), lbl);
    }
    gfx_color(GFX_DRAW);
  }
  gfx_affordance(S(STR_AF_NEXT), S(STR_AF_BACK_SEL));
}

static void draw_alert(void) {
  const int16_t y = 16, h = 32;
  gfx_fill(2, y, OLED_W - 4, h);
  gfx_color(GFX_ERASE);
  gfx_rect(3, (int16_t)(y + 1), OLED_W - 6, (int16_t)(h - 2));
  {
    const SpriteRef r = sprite_mini(MIC_ALERT);
    gfx_xbm(7, (int16_t)(y + 11), r.w, r.h, r.bits);
  }
  gfx_text_wrap(GF_BODY, 19, (int16_t)(y + 13), OLED_W - 26, GFX_LINE_BODY, 2,
                S_ALERT(s_alert_cur));
  gfx_color(GFX_DRAW);
  // ONE affordance, and it is the only thing this overlay can do. The second
  // one used to read "SEL" and promised the remedy jump that invariant 7
  // forbids; a strip that offers an action the code must refuse is worse than
  // no strip at all.
  gfx_affordance(S(STR_AF_OK), nullptr);
}

static void draw_help(void) {
  const int16_t y = (int16_t)(UI_AFFORD_Y - 12);
  gfx_fill(0, y, OLED_W, 12);
  gfx_color(GFX_ERASE);
  gfx_text_center(GF_BODY, (int16_t)(y + 9), S(s_modal_str));
  gfx_color(GFX_DRAW);
}

void dialog_render(void) {
  switch (s_modal) {
    case MODAL_CONFIRM: draw_confirm(); break;
    case MODAL_ALERT:   draw_alert();   break;
    case MODAL_HELP:    draw_help();    break;
    default: break;
  }
}

// -----------------------------------------------------------------------------
//  INPUT
// -----------------------------------------------------------------------------
static void confirm_commit(void) {
  const uint8_t which = s_confirm_id;
  dialog_close();
  if (s_commit) s_commit(which);
}

// The section 7 grammar, and it is the SAME one every list uses: A steps the
// cursor, B held activates the row it is on, B tapped cancels. Invariant 5
// (every confirmation starts on NO) means the dangerous answer always costs a
// step and then a deliberate 600 ms hold.
static void handle_confirm(Gesture g) {
  switch (g) {
    case GST_TAP_L:     s_confirm_yes = (uint8_t)!s_confirm_yes; break;
    case GST_HOLD_R:    if (s_confirm_yes) confirm_commit(); else dialog_close(); break;
    case GST_TAP_R:     dialog_close(); break;
    case GST_LONG_BOTH: dialog_close(); ui_home(); break;
    default: break;
  }
}

// INVARIANT 7, and the audit's S11 defect (section 8.3).
//
// What this used to do: wait UI_ALERT_MIN_MS so the line was readable, then let
// the FIRST gesture close the alert AND run alert_act() - push SCR_CARE, run
// ACT_CLEAN, open the medicine confirmation. The documented rule at the top of
// ui.cpp said the opposite ("an alert never steals a press: the first gesture
// only dismisses it"), and the rule is the right one: an alert appears without
// being asked for, on top of whatever the player was already doing, and the
// press they had already decided on belongs to the screen they were looking at,
// not to the interruption that landed in front of it.
//
// So: the first gesture dismisses, and does nothing else. It is still consumed
// - forwarding it to the screen underneath would be the same theft in the other
// direction, acting on a screen the player could not see when they pressed -
// and LONG_BOTH still means HOME, because invariant 2 outranks everything.
// The remedy the alert suggests is one ordinary navigation away and is now the
// player's choice to make.
static void handle_alert(Gesture g) {
  if ((uint32_t)(ui_now_ms() - s_modal_ms) < UI_ALERT_MIN_MS) return;  // must be readable
  dialog_close();
  if (g == GST_LONG_BOTH) ui_home();
}

bool dialog_input(Gesture g) {
  switch (s_modal) {
    case MODAL_ALERT:   handle_alert(g);   return true;
    case MODAL_CONFIRM: handle_confirm(g); return true;
    case MODAL_HELP:
      dialog_close();
      if (g == GST_LONG_BOTH) ui_home();   // invariant 2 still wins
      return true;
    default: return false;
  }
}
