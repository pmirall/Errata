// =============================================================================
//  PEBBLEBOL - ui/gfx.h
//  THE DRAWING SEAM (plan P2-C11, section 1.4).
//
//  Every screen that has been migrated into the screen table draws through
//  these calls and through nothing else. There are exactly two implementations:
//
//    device : ui/gfx_u8g2.cpp    - forwards to the render.cpp primitives, so a
//                                  migrated screen renders on the panel exactly
//                                  as it did before it moved.
//    host   : tests/fakes/gfx_fb.cpp - a 128x64 1-bit framebuffer with
//                                  fixed-advance font metrics and an
//                                  out-of-bounds recorder, which is what makes
//                                  the spec section 63 rule ("every screen
//                                  tested at the actual physical resolution")
//                                  a compiled assertion instead of a hope.
//
//  A screen source file that includes this header must NOT include render.h,
//  Arduino.h or U8g2lib.h: that is the whole point of the seam, and it is what
//  lets tests/test_screens.cpp compile the screen itself on the host.
//
//  COORDINATES. Same convention as render.h: x grows right, y grows down,
//  (0,0) is the top-left pixel, and a TEXT y is the BASELINE, not the top of
//  the glyph. Out-of-range rectangles are clipped, never wrapped.
//
//  Identifiers and comments: English. No Spanish literal lives in this module.
// =============================================================================
#ifndef NT_GFX_H
#define NT_GFX_H

#include <stdint.h>

#include "../core/config.h"

// -----------------------------------------------------------------------------
//  FONTS. The four the migrated screens are allowed to use, named by role so a
//  pure screen never mentions a u8g2 symbol. GF_TINY is the ASCII-only 4x6:
//  legal for version strings, IPs and hex, never for Spanish prose.
// -----------------------------------------------------------------------------
enum GfxFont : uint8_t {
  GF_BODY = 0,      // 5x8_tf      Latin-1, the default body font
  GF_NARR,          // 6x10_tf     Latin-1
  GF_HEAD,          // t0_11b_tf   Latin-1, bold
  GF_TINY,          // 4x6_tr      ASCII ONLY
  GF_COUNT
};

// Draw colours, u8g2's numbering (gfx_u8g2.cpp asserts nothing else is used).
#define GFX_ERASE   0u
#define GFX_DRAW    1u
#define GFX_XOR     2u

// Ascent ('A' height above the baseline), line pitch and the FIXED horizontal
// advance per codepoint. The advances are what the host fake measures with;
// gfx_u8g2.cpp static_asserts the ascents and pitches against the RD_* values
// so the two backends cannot drift apart silently.
#define GFX_ASC_BODY    6
#define GFX_ASC_NARR    7
#define GFX_ASC_HEAD    8
#define GFX_ASC_TINY    5
#define GFX_LINE_BODY   8
#define GFX_LINE_NARR  10
#define GFX_LINE_HEAD  11
#define GFX_LINE_TINY   7
#define GFX_ADV_BODY    5
#define GFX_ADV_NARR    6
#define GFX_ADV_HEAD    6
#define GFX_ADV_TINY    4

// Metrics, shared by both backends so a screen can lay itself out without
// knowing which one it is linked against.
inline uint8_t gfx_font_asc(GfxFont f) {
  switch (f) {
    case GF_NARR: return GFX_ASC_NARR;
    case GF_HEAD: return GFX_ASC_HEAD;
    case GF_TINY: return GFX_ASC_TINY;
    default:      return GFX_ASC_BODY;
  }
}
inline uint8_t gfx_font_line(GfxFont f) {
  switch (f) {
    case GF_NARR: return GFX_LINE_NARR;
    case GF_HEAD: return GFX_LINE_HEAD;
    case GF_TINY: return GFX_LINE_TINY;
    default:      return GFX_LINE_BODY;
  }
}
inline uint8_t gfx_font_adv(GfxFont f) {
  switch (f) {
    case GF_NARR: return GFX_ADV_NARR;
    case GF_HEAD: return GFX_ADV_HEAD;
    case GF_TINY: return GFX_ADV_TINY;
    default:      return GFX_ADV_BODY;
  }
}

// =============================================================================
//  PRIMITIVES
// =============================================================================

// GFX_ERASE / GFX_DRAW / GFX_XOR. Sticky, exactly like u8g2's setDrawColor:
// whoever changes it puts it back to GFX_DRAW before returning.
void gfx_color(uint8_t c);

void gfx_pixel(int16_t x, int16_t y);
void gfx_hline(int16_t x, int16_t y, int16_t w);
void gfx_vline(int16_t x, int16_t y, int16_t h);
void gfx_rect(int16_t x, int16_t y, int16_t w, int16_t h);   // 1 px outline
void gfx_fill(int16_t x, int16_t y, int16_t w, int16_t h);   // solid box

// XBM bitmap, LSB-first rows padded to whole bytes (the sprites.h layout).
void gfx_xbm(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t* bits);

// =============================================================================
//  TEXT. UTF-8 in, advance width out (0 when nothing was drawn).
// =============================================================================
uint16_t gfx_text_w(GfxFont f, const char* s);
uint16_t gfx_text(GfxFont f, int16_t x, int16_t y, const char* s);
uint16_t gfx_text_center(GfxFont f, int16_t y, const char* s);
uint16_t gfx_text_right(GfxFont f, int16_t x_right, int16_t y, const char* s);

// Truncated on a codepoint boundary so it never exceeds max_w.
uint16_t gfx_text_fit(GfxFont f, int16_t x, int16_t y, int16_t max_w, const char* s);

// Word-wrapped block, breaking on spaces, hard-breaking words wider than w.
// Returns the number of lines drawn (<= max_lines).
uint8_t  gfx_text_wrap(GfxFont f, int16_t x, int16_t y, int16_t w,
                       uint8_t line_h, uint8_t max_lines, const char* s);

// =============================================================================
//  SHADING AND CHROME
// =============================================================================

// n/16 of the pixels, 4x4 ordered Bayer, honouring the current draw colour.
void gfx_dither_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t level);

// XOR a rectangle (list highlight, tactile echo).
void gfx_invert_rect(int16_t x, int16_t y, int16_t w, int16_t h);

// The persistent bottom strip: what L and R do on this screen. NULL or "" for
// a side that does nothing. Composite rather than primitive because the device
// side also carries the pressed-button echo bookkeeping (render.h), which a
// re-implementation on top of the primitives would silently break.
void gfx_affordance(const char* left, const char* right);

#endif  // NT_GFX_H
