// =============================================================================
//  PEBBLEBOL - ui/gfx_widgets.cpp
//  The composites every migrated screen shares: the stat bar, the header bar,
//  the auto-return countdown and the vertical list.
//
//  ONE implementation, linked into BOTH the firmware and the host test binary.
//  They are written entirely on top of the gfx.h primitives, so whichever
//  backend is underneath - render.cpp on the panel, the framebuffer fake in
//  tests/ - produces the same pixels, and a golden recorded on the host is a
//  statement about the device.
//
//  Each of the four is a straight transcription of the ui.cpp helper it
//  replaces: rd_bar() (render.cpp), draw_header(), draw_countdown() and
//  draw_list(). Where a helper read a ui.cpp file-scope static, the value is a
//  parameter here instead - which is why gfx_countdown() takes the idle time
//  and gfx_list() takes the clock.
//
//  PURE translation unit: gfx.h, screen.h, ui.h. No Arduino.h, no render.h.
// =============================================================================
#include "gfx.h"

#include "screen.h"
#include "ui.h"

// -----------------------------------------------------------------------------
//  STAT BAR. 1 px frame plus a fill rounded to the nearest pixel, except that a
//  non-zero value never rounds down to zero: "almost empty" and "empty" must
//  not look the same. At or below GFX_BAR_LOW_PCT the fill is hatched at 50 %
//  with a solid 1 px leading edge, which still reads as "weak" at 3 px tall.
// -----------------------------------------------------------------------------
void gfx_bar(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t pct, uint8_t style) {
  if (x < 0 || y < 0) return;
  if (x + w > OLED_W) w = (int16_t)(OLED_W - x);
  if (y + h > OLED_H) h = (int16_t)(OLED_H - y);
  if (w < 3 || h < 3) return;
  if (pct > 100) pct = 100;

  gfx_rect(x, y, w, h);

  const int16_t iw = (int16_t)(w - 2);
  const int16_t ih = (int16_t)(h - 2);
  if (iw <= 0 || ih <= 0) return;

  int16_t fill = (int16_t)(((int32_t)iw * (int32_t)pct + 50) / 100);
  if (fill == 0 && pct > 0) fill = 1;
  if (fill > iw) fill = iw;
  if (fill <= 0) return;

  const bool hatched = (style == GFX_BAR_DITHER) ||
                       (style == GFX_BAR_AUTO && pct <= GFX_BAR_LOW_PCT);
  if (hatched) {
    gfx_dither_rect((int16_t)(x + 1), (int16_t)(y + 1), fill, ih, GFX_D50);
    gfx_vline((int16_t)(x + 1), (int16_t)(y + 1), ih);
  } else {
    gfx_fill((int16_t)(x + 1), (int16_t)(y + 1), fill, ih);
  }
}

// -----------------------------------------------------------------------------
//  HEADER BAR
// -----------------------------------------------------------------------------
void gfx_header(const char* title, const char* tag) {
  gfx_fill(0, 0, OLED_W, UI_HDR_H);
  gfx_color(GFX_ERASE);
  int16_t avail = OLED_W - 4;
  if (tag && tag[0]) {
    const int16_t tw = (int16_t)gfx_text_w(GF_BODY, tag);
    gfx_text_right(GF_BODY, OLED_W - 2, UI_HDR_BASE, tag);
    avail = (int16_t)(avail - tw - 4);
  }
  gfx_text_fit(GF_HEAD, 2, UI_HDR_BASE, avail, title ? title : "");
  gfx_color(GFX_DRAW);
}

// -----------------------------------------------------------------------------
//  AUTO-RETURN COUNTDOWN (navigation invariant 3)
// -----------------------------------------------------------------------------
void gfx_countdown(uint32_t idle_ms) {
  if (idle_ms + UI_COUNTDOWN_MS < UI_AUTORETURN_MS) return;
  const uint32_t left = (idle_ms >= UI_AUTORETURN_MS) ? 0u : (UI_AUTORETURN_MS - idle_ms);
  int16_t w = (int16_t)((left * (uint32_t)OLED_W) / UI_COUNTDOWN_MS);
  if (w > OLED_W) w = OLED_W;
  if (w > 0) gfx_invert_rect(0, UI_CONTENT_BOTTOM - 2, w, 3);
}

// -----------------------------------------------------------------------------
//  VERTICAL LIST
//
//  The highlight is XORed on top of rows that are all painted in colour 1,
//  rather than pre-filling the selected row and drawing its text in colour 0.
//  That inversion is what lets the highlight sit BETWEEN two rows while it
//  slides: whatever it covers inverts, glyphs included, in one pass.
// -----------------------------------------------------------------------------
static int16_t  s_from = 0;
static int16_t  s_to   = -1;      // -1 = nothing drawn yet: snap, do not slide
static uint32_t s_ms   = 0;

void gfx_list_reset(void) { s_from = 0; s_to = -1; s_ms = 0; }

// Where the highlight is RIGHT NOW, retargeting if the selection moved. Linear:
// the travel is one 11 px row and an ease would read as sluggish.
static int16_t cursor_y(int16_t want, uint32_t now_ms) {
  int16_t cur;
  if (s_to < 0) {
    cur    = want;
    s_from = want;
    s_to   = want;
    s_ms   = now_ms;
  } else {
    const uint32_t el = (uint32_t)(now_ms - s_ms);
    if (el >= UI_LIST_SLIDE_MS) {
      cur = s_to;
    } else {
      cur = (int16_t)(s_from +
            ((int32_t)(s_to - s_from) * (int32_t)el) / (int32_t)UI_LIST_SLIDE_MS);
    }
  }
  if (want != s_to) {              // a new target: leave from where we ARE
    s_from = cur;
    s_to   = want;
    s_ms   = now_ms;
  }
  return cur;
}

void gfx_list(const char* const* items, uint8_t n, uint8_t cur,
              const char* const* values, uint32_t now_ms) {
  if (n == 0 || !items) return;
  uint8_t first = 0;
  if (n > UI_LIST_ROWS) {
    if (cur + 2u > UI_LIST_ROWS) first = (uint8_t)(cur + 2u - UI_LIST_ROWS);
    if ((uint16_t)first + UI_LIST_ROWS > n) first = (uint8_t)(n - UI_LIST_ROWS);
  }
  const int16_t x_end = (n > UI_LIST_ROWS) ? (OLED_W - 5) : OLED_W;

  int16_t sel_y = -1;
  for (uint8_t i = 0; i < UI_LIST_ROWS && (uint16_t)(first + i) < n; ++i) {
    const uint8_t idx = (uint8_t)(first + i);
    const int16_t y   = (int16_t)(UI_HDR_H + i * UI_LIST_PITCH);
    if (idx == cur) sel_y = y;
    if (values && values[idx]) {
      const int16_t vw = (int16_t)gfx_text_w(GF_BODY, values[idx]);
      gfx_text_fit(GF_NARR, 3, (int16_t)(y + 8), (int16_t)(x_end - 9 - vw), items[idx]);
      gfx_text(GF_BODY, (int16_t)(x_end - 3 - vw), (int16_t)(y + 8), values[idx]);
    } else {
      gfx_text_fit(GF_NARR, 3, (int16_t)(y + 8), (int16_t)(x_end - 6), items[idx]);
    }
  }
  // Only when the cursor is on a VISIBLE row. When a long list scrolls, `first`
  // moves with `cur` and the target y does not change at all - correctly, no
  // animation is started.
  if (sel_y >= 0)
    gfx_invert_rect(0, cursor_y(sel_y, now_ms), x_end, UI_LIST_PITCH - 1);

  if (n > UI_LIST_ROWS) {
    const int16_t track_h = UI_LIST_ROWS * UI_LIST_PITCH - 1;
    gfx_rect(OLED_W - 4, UI_HDR_H, 4, track_h);
    int16_t th = (int16_t)((int32_t)(track_h - 2) * UI_LIST_ROWS / n);
    if (th < 3) th = 3;
    const int16_t span = (int16_t)(track_h - 2 - th);
    const int16_t ty   = (int16_t)(UI_HDR_H + 1 +
                                   ((int32_t)span * first) / (int32_t)(n - UI_LIST_ROWS));
    gfx_fill(OLED_W - 3, ty, 2, th);
  }
}

// -----------------------------------------------------------------------------
//  THE BANNER (P10-C4, spec section 65)
//
//  ONE picture with TWO callers, and until this chunk it was two
//  implementations of it in two files - one of which no host binary compiles:
//
//    ui/ui.cpp     draw_toast()  a solid 11 px slab, one CENTRED line, unbounded
//    ui/dialog.cpp draw_help()   a solid 12 px slab, one CENTRED line, unbounded
//
//  "Unbounded" is the part that mattered. Measured across the whole string
//  table: SIX of the twenty-five ids that reach ui_help() and FOUR of the fifty
//  that reach ui_toast() are wider than the 128 px panel at GF_BODY - the worst
//  is 220 px - and each is one GST_BOTH press or one ordinary refusal away.
//  ui/render.cpp's draw_utf8() drops leading codepoints on a negative x, so
//  what the player saw was the MIDDLE of the sentence with both ends gone. And
//  the toast half of it lived in ui/ui.cpp, which includes Arduino.h, so no
//  golden in this repository had ever drawn one.
//
//  So the slab grows to fit instead: one line stays centred and looks exactly
//  as it did, and a line that does not fit is WRAPPED into a taller slab. Two
//  callers, one implementation, in the translation unit both backends link -
//  which is the same argument gfx_header() and gfx_list() were moved here on.
//
//  THE HEIGHT IS COMPUTED FROM gfx_text_w(), which is a MEASUREMENT and not a
//  wrap: it can only over-estimate the number of lines a word-wrap needs by
//  the slack of one word per line, never under-estimate it below
//  ceil(w / inner). tests/test_screens.cpp drives EVERY help id and EVERY toast
//  id through the real wrap and requires the estimate to be exactly what the
//  wrap used, so an over-estimate is a failure here and not a blank row nobody
//  notices.
// -----------------------------------------------------------------------------
uint8_t gfx_banner_lines(const char* text) {
  if (text == nullptr || *text == '\0') return 0u;
  const uint16_t w = gfx_text_w(GF_BODY, text);
  if (w == 0u) return 0u;
  const uint16_t inner = (uint16_t)GFX_BANNER_INNER_W;
  uint16_t n = (uint16_t)(1u + (w - 1u) / inner);
  if (n > (uint16_t)GFX_BANNER_MAX_LINES) n = (uint16_t)GFX_BANNER_MAX_LINES;
  return (uint8_t)n;
}

uint8_t gfx_banner(const char* text) {
  const uint8_t lines = gfx_banner_lines(text);
  if (lines == 0u) return 0u;

  const int16_t h = (int16_t)((lines - 1) * GFX_LINE_BODY + GFX_BANNER_H1);
  const int16_t y = (int16_t)(UI_AFFORD_Y - h);
  gfx_fill(0, y, OLED_W, h);
  gfx_color(GFX_ERASE);
  // The baseline of line 0. GFX_ASC_BODY glyph rows sit above it and two blank
  // rows below the last one, which is exactly the 11 px slab the toast has had
  // since P2-C11 - so a one-line banner is pixel-for-pixel what it always was.
  const int16_t base = (int16_t)(y + GFX_BANNER_TOP_PAD + GFX_ASC_BODY - 1);
  uint8_t drawn;
  if (lines == 1u) {
    gfx_text_center(GF_BODY, base, text);      // unchanged for a line that fits
    drawn = 1u;
  } else {
    drawn = gfx_text_wrap(GF_BODY, GFX_BANNER_PAD_X, base,
                          (int16_t)GFX_BANNER_INNER_W, GFX_LINE_BODY, lines, text);
  }
  gfx_color(GFX_DRAW);
  return drawn;
}
