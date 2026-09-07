// =============================================================================
//  PEBBLEBOL - ui/screen_settings.cpp
//  SETTINGS and its "Acerca de" page, migrated into the screen table by
//  P2-C11b. Both are transcriptions of ui.cpp's draw_settings() /
//  draw_settings_info() onto the gfx.h seam.
//
//  The five diagnostic lines are DEVICE facts - the IP, the free heap, the NVS
//  error word, the web PIN, the age - so ui.cpp fills them in through
//  ui_info_lines() and this file only lays them out.
//
//  PURE translation unit.
// =============================================================================
#include "../hardware/audio.h"
#include "screen_settings.h"

#include "../core/strings_es.h"
#include "gfx.h"
#include "screen.h"
#include "ui.h"

static const uint16_t kLabel[SET_ROWS] = {
  STR_SET_SOUND, STR_SET_WEB,   STR_SET_BRIGHT,
  STR_WEB_TITLE, STR_SET_CLOCK, STR_SET_INFO,  STR_SET_RESET,
  STR_ITEM_BACK
};
static const uint16_t kHelp[SET_ROWS] = {
  STR_HLP_SOUND, STR_HLP_WEB,   STR_HLP_BRIGHT,
  STR_HLP_WEB,   STR_HLP_CLOCK, STR_HLP_INFO,  STR_HLP_RESET,
  STR_HLP_BACK
};
static const uint8_t kBrightSteps[5] = {
  OLED_CONTRAST_DIM, 90, OLED_CONTRAST_DEFAULT, 200, 255
};

static uint8_t s_cur  = 0;
static uint8_t s_page = 0;      // 0 = list, 1 = "Acerca de"

uint8_t settings_cursor(void)    { return s_cur; }
uint8_t settings_page(void)      { return s_page; }
void    settings_close_page(void){ s_page = 0; }
void    settings_enter(void)     { s_cur = 0; s_page = 0; }

static bool cfg_flag(uint8_t bit) {
  const Config* c = ui_cfg();
  return c && (c->flags & bit) != 0;
}

static const char* set_value(uint8_t row) {
  switch (row) {
    case SET_SOUND: return cfg_flag(CF_MUTE)        ? S(STR_OFF) : S(STR_ON);
    case SET_WEB:   return cfg_flag(CF_WEB_ENABLED) ? S(STR_ON)  : S(STR_OFF);
    default: return nullptr;
  }
}

static void render_info(void) {
  gfx_header(S(STR_SET_INFO), nullptr);
  char lines[UI_INFO_LINES][UI_INFO_CAP];
  ui_info_lines(lines);
  for (uint8_t i = 0; i < UI_INFO_LINES; ++i) {
    if (lines[i][0] == '\0') continue;
    gfx_text_fit(GF_TINY, 2, (int16_t)(19 + i * 8), 124, lines[i]);
  }
  gfx_countdown(ui_idle_ms());
  gfx_affordance(nullptr, S(STR_AF_BACK));
}

void settings_render(void) {
  if (s_page == 1) { render_info(); return; }
  const char* items[SET_ROWS];
  const char* vals[SET_ROWS];
  for (uint8_t i = 0; i < SET_ROWS; ++i) {
    items[i] = S(kLabel[i]);
    vals[i]  = set_value(i);
  }
  gfx_header(S(STR_SET_TITLE), ui_cfg() ? nullptr : "!");
  gfx_list(items, SET_ROWS, s_cur, vals, ui_now_ms());
  gfx_countdown(ui_idle_ms());
  gfx_affordance(S(STR_AF_NEXT), S(STR_AF_SEL));
}

static void settings_select(void) {
  switch (s_cur) {
    case SET_BACK:  ui_back();                  return;
    case SET_INFO:  s_page = 1; ui_note_input(); return;
    case SET_QR:    ui_push(SCR_CREATOR);            return;
    case SET_CLOCK: ui_push(SCR_TIME);         return;
    case SET_RESET: ui_confirm_wipe();          return;
    default: break;
  }
  Config* c = ui_cfg();
  if (!c) { ui_toast(STR_ERR_BUSY); return; }
  switch (s_cur) {
    case SET_SOUND: c->flags = (uint8_t)(c->flags ^ CF_MUTE);        break;
    case SET_WEB:   c->flags = (uint8_t)(c->flags ^ CF_WEB_ENABLED); break;
    case SET_BRIGHT: {
      uint8_t i = 0;
      while (i < 5u && kBrightSteps[i] <= c->brightness) ++i;
      if (i >= 5u) i = 0;
      c->brightness = kBrightSteps[i];
      // Through ui.cpp's arbiter, not straight to the panel: while the pet is
      // asleep the dim override still wins and the new setting takes effect on
      // waking. Writing the contrast here would cancel the sleep ramp for good.
      ui_apply_brightness(c->brightness);
      break;
    }
    default: break;
  }
  ui_cfg_changed();
}

static uint8_t ring_next(uint8_t cur, uint8_t n) {
  return n ? (uint8_t)((cur + 1u) % n) : 0u;
}

void settings_input(Gesture g) {
  // Any gesture at all closes the info page: it is a read-only panel, and the
  // one thing every button on it should do is give the list back. SF_OWNS_BACK
  // is what lets B reach this line instead of being spent on sm_back() by the
  // router: the page is one level BELOW the navigation stack.
  if (s_page == 1) { s_page = 0; return; }
  switch (g) {
    case GST_HOLD_R: ui_back(); break;      // section 7: B cancels, on the HOLD
    case GST_TAP_R:  settings_select(); break;
    case GST_TAP_L:
    // The click, and the rule behind it, is written once in ui/screen_menu.cpp.
    case GST_HOLD_L: s_cur = ring_next(s_cur, SET_ROWS); audio_play(SFX_TICK); break;
    case GST_BOTH:   ui_help(kHelp[s_cur]); break;
    default: break;
  }
}
