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
  GF_BIG,           // logisoso16_tn  DIGITS ONLY, 9x19
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
// GF_BIG is the digits-only face the TIME entry screen shows the field being
// edited in. Its ascent is asserted against RD_ASC_BIGNUM in gfx_u8g2.cpp; the
// ADVANCE is the nominal 9 px cell plus its 1 px side bearing, so - exactly as
// with the proportional GF_HEAD - a host golden containing GF_BIG text is
// LAYOUT-approximate rather than glyph-exact. Nothing is laid out against it
// other than the value itself, which is left-aligned.
#define GFX_ASC_BIG    16
#define GFX_LINE_BODY   8
#define GFX_LINE_NARR  10
#define GFX_LINE_HEAD  11
#define GFX_LINE_TINY   7
#define GFX_LINE_BIG   20
#define GFX_ADV_BODY    5
#define GFX_ADV_NARR    6
#define GFX_ADV_HEAD    6
#define GFX_ADV_TINY    4
#define GFX_ADV_BIG    10

// Metrics, shared by both backends so a screen can lay itself out without
// knowing which one it is linked against.
inline uint8_t gfx_font_asc(GfxFont f) {
  switch (f) {
    case GF_NARR: return GFX_ASC_NARR;
    case GF_HEAD: return GFX_ASC_HEAD;
    case GF_TINY: return GFX_ASC_TINY;
    case GF_BIG:  return GFX_ASC_BIG;
    default:      return GFX_ASC_BODY;
  }
}
inline uint8_t gfx_font_line(GfxFont f) {
  switch (f) {
    case GF_NARR: return GFX_LINE_NARR;
    case GF_HEAD: return GFX_LINE_HEAD;
    case GF_TINY: return GFX_LINE_TINY;
    case GF_BIG:  return GFX_LINE_BIG;
    default:      return GFX_LINE_BODY;
  }
}
inline uint8_t gfx_font_adv(GfxFont f) {
  switch (f) {
    case GF_NARR: return GFX_ADV_NARR;
    case GF_HEAD: return GFX_ADV_HEAD;
    case GF_TINY: return GFX_ADV_TINY;
    case GF_BIG:  return GFX_ADV_BIG;
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

// -----------------------------------------------------------------------------
//  XBM bitmap, LSB-first rows padded to whole bytes (the sprites.h layout).
//
//  THERE ARE TWO OF THESE AND THE DIFFERENCE IS NOT COSMETIC. It was found at
//  P10-C3, before a single new film was written, and it had been latent since
//  the seam was cut at P2-C11:
//
//    gfx_xbm()   OPAQUE.  Every pixel of the w*h box is written: the 1-bits in
//                the current draw colour and the 0-BITS IN THE INVERSE. A blit
//                over existing ink ERASES a w*h hole and then draws into it.
//                This is what the device has always done - ui/render.cpp:368
//                puts the panel in setBitmapMode(0) and hands it back that way
//                after every exception (ui/ui.cpp:790, ui/petfx.cpp:1306) - and
//                it is what ui/screen_menu.cpp:70 already knew and said.
//    gfx_xbm_t() TRANSPARENT. Only the 1-bits are written; the 0-bits leave
//                whatever was underneath alone. setBitmapMode(1) on the device.
//
//  THE HOST FAKE IMPLEMENTED THE TRANSPARENT RULE FOR BOTH UNTIL P10-C3, so
//  the two backends disagreed and nothing in the tree could see it. It was
//  unobserved only by luck of composition: every one of the thirteen call
//  sites at that commit drew either onto blank ground, or (ui/dialog.cpp:130)
//  in GFX_ERASE onto a solid slab where the inverse colour is 1 and the slab
//  was already 1. THIS CHUNK ENDS THAT LUCK - every film it adds draws a
//  sprite ON TOP OF the body, the floor or a filled panel - so the seam is
//  explicit now and tests/test_screens.cpp asserts both behaviours by name.
//  Pick deliberately: a badge on empty background is fine either way, a sprite
//  over ink is not.
// -----------------------------------------------------------------------------
void gfx_xbm(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t* bits);
void gfx_xbm_t(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t* bits);

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

// The same, with the matrix SLID: the low 2 bits of `phase` shift the column
// and the next 2 the row, exactly as render.h's rd_dither_rect_phase() defines
// it. gfx_dither_rect() is gfx_dither_rect_phase(..., 0).
//
// IT IS HERE BECAUSE ui/corrupt_fx.cpp's CfxRow CARRIES A PHASE and, until
// P10-C3, the only painter that honoured it was ui/petfx.cpp - a translation
// unit no host binary compiles. So the corruption glitch had geometry the suite
// could drive and a PICTURE nothing had ever drawn. ui/screen_home.cpp paints
// the same rows on the still body path now, which is the path the goldens see.
void gfx_dither_rect_phase(int16_t x, int16_t y, int16_t w, int16_t h,
                           uint8_t level, uint8_t phase);

// =============================================================================
//  THE BANNER: the toast line and the HELP strip, which are the same picture.
//
//  A solid slab sitting on top of the affordance strip, carrying prose in the
//  inverse. One line is CENTRED (which is what both callers always drew); a
//  line too wide for the panel makes the slab taller and wraps into it, up to
//  GFX_BANNER_MAX_LINES. gfx_banner() returns the number of lines it drew and
//  gfx_banner_lines() is the same arithmetic without the drawing, so a test can
//  say the slab is exactly as tall as the text turned out to be.
//
//  Implemented ONCE, in ui/gfx_widgets.cpp, for the reason gfx_header() is:
//  ui/ui.cpp's copy of it was in a translation unit no host binary compiles.
// =============================================================================
#define GFX_BANNER_PAD_X       2
#define GFX_BANNER_INNER_W     (OLED_W - 2 * GFX_BANNER_PAD_X)   // 124
#define GFX_BANNER_TOP_PAD     3      // blank rows above the first ascender
#define GFX_BANNER_BOT_PAD     2      // blank rows under the last baseline
// A one-line banner is 3 + GFX_ASC_BODY + 2 = 11 px, which is the slab
// ui/ui.cpp's toast has drawn since P2-C11, so nothing about a line that fits
// changes. Each further line adds one body pitch.
#define GFX_BANNER_H1          (GFX_BANNER_TOP_PAD + GFX_ASC_BODY + GFX_BANNER_BOT_PAD)
#define GFX_BANNER_MAX_LINES   3

uint8_t gfx_banner(const char* text);
uint8_t gfx_banner_lines(const char* text);

// XOR a rectangle (list highlight, tactile echo).
void gfx_invert_rect(int16_t x, int16_t y, int16_t w, int16_t h);

// The persistent bottom strip: what L and R do on this screen. NULL or "" for
// a side that does nothing. Composite rather than primitive because the device
// side also carries the pressed-button echo bookkeeping (render.h), which a
// re-implementation on top of the primitives would silently break.
void gfx_affordance(const char* left, const char* right);

// =============================================================================
//  WIDGETS (ui/gfx_widgets.cpp)
//
//  Composites every migrated screen shares. They are implemented ONCE, on top
//  of the primitives above, in a translation unit that is linked into both the
//  firmware and the host test binary - so the panel and the golden cannot
//  disagree about a header bar, a stat bar or a list row.
// =============================================================================

// Dither levels, n/16. Same numbers render.h publishes as RD_D*.
#define GFX_D25          4
#define GFX_D50          8
#define GFX_D75         12
#define GFX_DITHER_MAX  16

// gfx_bar() styles. AUTO hatches a value at or below GFX_BAR_LOW_PCT so
// "nearly empty" is unmistakable at 3 px tall.
#define GFX_BAR_AUTO     0
#define GFX_BAR_SOLID    1
#define GFX_BAR_DITHER   2
#define GFX_BAR_LOW_PCT 20

// Stat bar: 1 px frame + fill, pct 0..100. Legible down to h = 5.
void gfx_bar(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t pct,
             uint8_t style = GFX_BAR_AUTO);

// The inverted title bar, rows 0..UI_HDR_H-1 (screen.h). The tag is
// right-aligned and the title is fitted into whatever is left, so the two can
// never collide. Either may be NULL.
void gfx_header(const char* title, const char* tag);

// Navigation invariant 3: the 3 px bar that drains right to left over the last
// UI_COUNTDOWN_MS of the auto-return. XORed, so it stays visible over whatever
// the screen already drew. Draws nothing until the window opens, and nothing
// at all on a screen that never times out - an SF_STICKY screen simply does
// not call it. `idle_ms` is the time since the last gesture (ui_idle_ms()).
void gfx_countdown(uint32_t idle_ms);

// The shared vertical list: UI_LIST_ROWS rows of UI_LIST_PITCH px under the
// header, an optional right-aligned value per row, a scrollbar when the list
// is longer than the window, and a highlight that SLIDES between rows over
// UI_LIST_SLIDE_MS. `now_ms` drives that slide; gfx_list_reset() cuts it and
// must be called on every screen change, or the highlight flies in from
// whatever row the previous list left it on.
void gfx_list(const char* const* items, uint8_t n, uint8_t cur,
              const char* const* values, uint32_t now_ms);
void gfx_list_reset(void);

#endif  // NT_GFX_H
