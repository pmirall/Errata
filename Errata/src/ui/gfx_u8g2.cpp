// =============================================================================
//  ERRATA - ui/gfx_u8g2.cpp
//  The DEVICE side of the gfx.h seam: every call forwards to the render.cpp
//  primitive that already draws it, so a screen that moves into the screen
//  table renders on the panel byte for byte as it did before the move.
//
//  This is the only translation unit that knows both halves. It therefore also
//  holds the static_asserts that stop the two font metric tables from drifting.
// =============================================================================
#include "gfx.h"

#include "render.h"

// gfx.h publishes its own metrics so the host fake can measure without u8g2.
// If render.h ever changes a font, these fire here rather than producing a
// silently wrong golden.
static_assert(GFX_ASC_BODY  == RD_ASC_BODY,  "gfx/render font metrics drifted (body ascent)");
static_assert(GFX_ASC_NARR  == RD_ASC_NARR,  "gfx/render font metrics drifted (narrow ascent)");
static_assert(GFX_ASC_HEAD  == RD_ASC_HEAD,  "gfx/render font metrics drifted (head ascent)");
static_assert(GFX_ASC_TINY  == RD_ASC_TINY,  "gfx/render font metrics drifted (tiny ascent)");
static_assert(GFX_LINE_BODY == RD_LINE_BODY, "gfx/render font metrics drifted (body pitch)");
static_assert(GFX_LINE_NARR == RD_LINE_NARR, "gfx/render font metrics drifted (narrow pitch)");
static_assert(GFX_LINE_HEAD == RD_LINE_HEAD, "gfx/render font metrics drifted (head pitch)");
static_assert(GFX_LINE_TINY == RD_LINE_TINY, "gfx/render font metrics drifted (tiny pitch)");
static_assert(GFX_ASC_BIG   == RD_ASC_BIGNUM, "gfx/render font metrics drifted (bignum ascent)");

static const uint8_t* font_of(GfxFont f) {
  switch (f) {
    case GF_NARR: return RD_FONT_NARR;
    case GF_HEAD: return RD_FONT_HEAD;
    case GF_TINY: return RD_FONT_TINY;
    case GF_BIG:  return RD_FONT_BIGNUM;
    default:      return RD_FONT_BODY;
  }
}

void gfx_color(uint8_t c) { rd_u8g2().setDrawColor(c); }

void gfx_pixel(int16_t x, int16_t y) {
  if (x < 0 || y < 0 || x >= OLED_W || y >= OLED_H) return;   // u8g2 wraps
  rd_u8g2().drawPixel((u8g2_uint_t)x, (u8g2_uint_t)y);
}

void gfx_hline(int16_t x, int16_t y, int16_t w) {
  if (w <= 0 || y < 0 || y >= OLED_H) return;
  if (x < 0) { w = (int16_t)(w + x); x = 0; }
  if (w <= 0 || x >= OLED_W) return;
  if (x + w > OLED_W) w = (int16_t)(OLED_W - x);
  rd_u8g2().drawHLine((u8g2_uint_t)x, (u8g2_uint_t)y, (u8g2_uint_t)w);
}

void gfx_vline(int16_t x, int16_t y, int16_t h) {
  if (h <= 0 || x < 0 || x >= OLED_W) return;
  if (y < 0) { h = (int16_t)(h + y); y = 0; }
  if (h <= 0 || y >= OLED_H) return;
  if (y + h > OLED_H) h = (int16_t)(OLED_H - y);
  rd_u8g2().drawVLine((u8g2_uint_t)x, (u8g2_uint_t)y, (u8g2_uint_t)h);
}

void gfx_rect(int16_t x, int16_t y, int16_t w, int16_t h) {
  if (w <= 0 || h <= 0) return;
  gfx_hline(x, y, w);
  gfx_hline(x, (int16_t)(y + h - 1), w);
  gfx_vline(x, y, h);
  gfx_vline((int16_t)(x + w - 1), y, h);
}

void gfx_fill(int16_t x, int16_t y, int16_t w, int16_t h) {
  if (w <= 0 || h <= 0) return;
  for (int16_t r = 0; r < h; r++) gfx_hline(x, (int16_t)(y + r), w);
}

// OPAQUE. render.cpp:368 leaves the panel in setBitmapMode(0) for the whole
// session, so this paints the ENTIRE w*h box: the 1-bits in the draw colour and
// the 0-bits in the inverse. Nothing is set here, deliberately - a call that
// set the mode itself would be a second opinion about the panel's state, and
// ui/ui.cpp:790 and ui/petfx.cpp:1306 both already promise to hand it back as
// render.cpp set it.
void gfx_xbm(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t* bits) {
  if (bits == nullptr || w <= 0 || h <= 0) return;
  rd_u8g2().drawXBM((u8g2_uint_t)x, (u8g2_uint_t)y, (u8g2_uint_t)w, (u8g2_uint_t)h, bits);
}

// TRANSPARENT. The mode is flipped and PUT BACK, which is the same contract
// gfx_color() states for the draw colour: whoever changes it restores it. This
// is the blit a film uses - a sprite drawn over the body, the floor or a filled
// panel must not erase a w*h hole to stand in.
void gfx_xbm_t(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t* bits) {
  if (bits == nullptr || w <= 0 || h <= 0) return;
  U8G2& u = rd_u8g2();
  u.setBitmapMode(1);
  u.drawXBM((u8g2_uint_t)x, (u8g2_uint_t)y, (u8g2_uint_t)w, (u8g2_uint_t)h, bits);
  u.setBitmapMode(0);
}

uint16_t gfx_text_w(GfxFont f, const char* s)                          { return rd_text_width(font_of(f), s); }
uint16_t gfx_text(GfxFont f, int16_t x, int16_t y, const char* s)      { return rd_text(x, y, font_of(f), s); }
uint16_t gfx_text_center(GfxFont f, int16_t y, const char* s)          { return rd_text_center(y, font_of(f), s); }
uint16_t gfx_text_right(GfxFont f, int16_t xr, int16_t y, const char* s) { return rd_text_right(xr, y, font_of(f), s); }

uint16_t gfx_text_fit(GfxFont f, int16_t x, int16_t y, int16_t max_w, const char* s) {
  return rd_text_fit(x, y, max_w, font_of(f), s);
}

uint8_t gfx_text_wrap(GfxFont f, int16_t x, int16_t y, int16_t w,
                      uint8_t line_h, uint8_t max_lines, const char* s) {
  return rd_text_wrap(x, y, w, line_h, max_lines, font_of(f), s);
}

void gfx_dither_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t level) {
  rd_dither_rect(x, y, w, h, level);
}

void gfx_dither_rect_phase(int16_t x, int16_t y, int16_t w, int16_t h,
                           uint8_t level, uint8_t phase) {
  rd_dither_rect_phase(x, y, w, h, level, phase);
}

void gfx_invert_rect(int16_t x, int16_t y, int16_t w, int16_t h) {
  rd_invert_rect(x, y, w, h);
}

void gfx_affordance(const char* left, const char* right) {
  rd_affordance(left, right);
}
