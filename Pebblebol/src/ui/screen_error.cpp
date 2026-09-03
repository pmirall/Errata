// =============================================================================
//  PEBBLEBOL - ui/screen_error.cpp
//  The ERROR state, migrated into the screen table by P2-C11a.
//
//  PURE translation unit: gfx.h, strings and the save-manager verdicts, no
//  render.h and no Arduino.h. Everything that needs hardware (retrying the
//  panel, blinking the LED) or that belongs to ui.cpp's modal layer (the wipe
//  confirmation, toasts) is reached through a bound function pointer or
//  through the ui.h seam, which is what lets tests/test_screens.cpp render
//  this screen on the host in all three of its states.
//
//  SF_STICKY + SF_LOCK_INPUT (screen_table.cpp): the auto-return and the
//  global HOME / BACK grammar are both off here. Walking away from an
//  unanswered save question would leave the user playing a placeholder pet
//  that can never be written.
// =============================================================================
#include "screen_error.h"

#include "../core/strings_es.h"
#include "../persistence/save_manager.h"
#include "gfx.h"
#include "screen.h"
#include "ui.h"

// ---- state ------------------------------------------------------------------
static uint8_t     s_kind      = ERRK_NONE;
// A display failure and a save problem are independent, and the display one
// is armed second (app_setup runs the load first). Without this slot the
// later arming would erase the earlier question and the user would play on
// with an unreadable save and no warning.
static uint8_t s_pending_save_kind = ERRK_NONE;
static UiRecoverFn s_recover   = nullptr;
static UiRetryFn   s_retry     = nullptr;
static UiLedFn     s_led       = nullptr;
static uint32_t    s_blink_t0  = 0;
static uint8_t     s_led_on    = 0;
static uint8_t     s_blinking  = 0;

void    err_set_kind(uint8_t kind) { s_kind = kind; }
uint8_t err_kind(void)             { return s_kind; }

void ui_bind_recover(UiRecoverFn fn)     { s_recover = fn; }
void ui_bind_display_retry(UiRetryFn fn) { s_retry   = fn; }
void ui_bind_led(UiLedFn fn)             { s_led     = fn; }

// -----------------------------------------------------------------------------
//  LED. Only the panel failure blinks: a save error has a screen to say it on,
//  and a device that blinks at its owner for a reason already written on the
//  panel is just noise.
// -----------------------------------------------------------------------------
static void led_set(uint8_t on) {
  if (on == s_led_on) return;
  s_led_on = on;
  if (s_led) s_led(on != 0u);
}

// -----------------------------------------------------------------------------
//  HOOKS
// -----------------------------------------------------------------------------
void err_enter(void) {
  s_blink_t0 = 0;
  s_blinking = (s_kind == ERRK_DISPLAY) ? 1u : 0u;
}

void err_update(uint32_t now_ms) {
  if (!s_blinking) { led_set(0); return; }
  if (s_blink_t0 == 0) s_blink_t0 = now_ms ? now_ms : 1u;
  const uint32_t p = (uint32_t)(now_ms - s_blink_t0) % ERR_BLINK_PERIOD_MS;
  led_set((p < 120u || (p >= 240u && p < 360u)) ? 1u : 0u);
}

void err_leave(void) {
  s_blinking = 0;
  led_set(0);
}

void err_render(void) {
  const bool display = (s_kind == ERRK_DISPLAY);

  // Inverted title bar. Same composition draw_header() uses for the legacy
  // screens, expressed through the seam instead of through u8g2 directly.
  gfx_fill(0, 0, OLED_W, UI_HDR_H);
  gfx_color(GFX_ERASE);
  gfx_text_fit(GF_HEAD, 2, UI_HDR_BASE, OLED_W - 4,
               S(display ? STR_DISP_ERR_TITLE : STR_SAVE_ERR_TITLE));
  gfx_color(GFX_DRAW);

  uint16_t body = STR_SAVE_ERR_BODY;
  if (display)                        body = STR_ERR_OLED;
  else if (s_kind == ERRK_SAVE_NEWER) body = STR_SAVE_ERR_NEWER;
  gfx_text_wrap(GF_BODY, 2, 22, OLED_W - 4, GFX_LINE_BODY, 2, S(body));

  if (display) {
    gfx_text(GF_BODY, 2, 42, S(STR_DISP_ERR_A));
  } else if (s_kind == ERRK_SAVE_NEWER) {
    // A newer save is GOOD data, so NEITHER button may write and neither is
    // offered. "Recuperar" was the dangerous one: restoring an older nvs2
    // checkpoint over a save this firmware merely cannot READ would destroy
    // the collection the newer firmware wrote (spec 48, 60). The fix is a
    // firmware update, not a wipe and not a rollback.
    gfx_text(GF_BODY, 2, 42, S(STR_SAVE_UPDATE_FW));
  } else {
    gfx_text(GF_BODY, 2, 42, S(STR_SAVE_ERR_A));
    gfx_text(GF_BODY, 2, 52, S(STR_SAVE_ERR_B));
  }

  gfx_affordance(S(STR_AF_OK), display ? nullptr : S(STR_AF_SEL));
}

// "Recuperar": restore the nvs2 checkpoint, through the entry point, because
// only it can rebind the simulation to the pet that comes back.
static void error_recover(void) {
  // Defence in depth, not a redundant check: this is the only path on the
  // device that can write an OLD checkpoint over a save it could not read.
  // err_input() already refuses to route here for ERRK_SAVE_NEWER, but a
  // future re-route must not be able to reopen a data-loss hole silently.
  if (s_kind == ERRK_SAVE_NEWER) { ui_toast(STR_SAVE_UPDATE_FW); return; }
  if (!s_recover || !s_recover()) {
    ui_toast(STR_SAVE_NO_BACKUP);       // nothing was written; still read-only
    return;
  }
  ui_toast(STR_SAVE_FROM_BACKUP);
  s_kind = ERRK_NONE;
  ui_note_recovered();
}

void err_input(Gesture g) {
  if (s_kind == ERRK_DISPLAY) {
    // The panel is the thing that failed, so this branch is driven blind: the
    // user presses A because the LED is blinking, not because they can read a
    // prompt. A successful retry gives them the whole device back.
    if (g == GST_TAP_L && s_retry && s_retry()) {
      // The panel coming back does not answer the OTHER question. A save
      // problem raised earlier in this same boot was hidden behind this
      // screen, and dropping it here would drop the user into a read-only
      // session with no warning that their collection is unreadable.
      if (s_pending_save_kind != ERRK_NONE) {
        s_kind = s_pending_save_kind;
        s_pending_save_kind = ERRK_NONE;
        s_blinking = 0;
        led_set(0);
        return;                         // stay on ERROR, now asking about the save
      }
      s_kind = ERRK_NONE;
      ui_goto(SCR_HOME);
    }
    return;
  }

  // A save from a newer firmware is intact data this build cannot parse, so no
  // gesture here may write: not the wipe (B already refused it) and not the
  // checkpoint restore (A used to accept it, which silently overwrote the very
  // save the screen was warning about). Both buttons say the same true thing.
  if (s_kind == ERRK_SAVE_NEWER) {
    if (g == GST_TAP_L || g == GST_TAP_R) ui_toast(STR_SAVE_UPDATE_FW);
    return;
  }

  switch (g) {
    case GST_TAP_L: error_recover();  break;
    case GST_TAP_R: ui_confirm_wipe(); break;   // two dialogs, then the reset
    default: break;
  }
}

// -----------------------------------------------------------------------------
//  ENTRY POINTS
// -----------------------------------------------------------------------------
void ui_note_load(uint8_t result) {
  switch (result) {
    case LOAD_CORRUPT:
      err_set_kind(ERRK_SAVE_CORRUPT);
      ui_goto(SCR_ERROR);
      break;
    case LOAD_FOREIGN_NEWER:
      err_set_kind(ERRK_SAVE_NEWER);
      ui_goto(SCR_ERROR);
      break;
    case LOAD_MIGRATED:       ui_toast(STR_SAVE_UPDATED);     break;
    case LOAD_RECOVERED_PAIR: ui_toast(STR_SAVE_RECOVERED);   break;
    case LOAD_RECOVERED_CKPT: ui_toast(STR_SAVE_FROM_BACKUP); break;
    default: break;
  }
}

void ui_note_display_failure(void) {
  // Park a save question raised earlier this boot rather than overwriting it;
  // err_input() re-arms it once the panel is back.
  if (s_kind == ERRK_SAVE_CORRUPT || s_kind == ERRK_SAVE_NEWER) {
    s_pending_save_kind = s_kind;
  }
  err_set_kind(ERRK_DISPLAY);
  ui_goto(SCR_ERROR);
}
