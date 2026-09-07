// =============================================================================
//  Pebblebol host tests - fakes/gfx_real.cpp
//  THE SECOND HOST BACKEND: the one whose contract is FAITHFUL PIXELS.
//
//  WHY THERE ARE TWO. fakes/gfx_fb.cpp is the backend the goldens run on, and
//  its own header says what it is: "Text is NOT glyph-accurate and does not
//  pretend to be". It paints each codepoint as a fixed-width barcode, which is
//  exactly right for what the goldens ask - does a name overflow its row, does
//  the truncation land on the correct codepoint, does anything draw off the
//  panel - and exactly wrong for a picture. The printed manual illustrated
//  itself from those goldens and every screen in it came out as bars.
//
//  The fix is not to change gfx_fb.cpp. It answers its question well and 75
//  goldens depend on the answer. This is a SEPARATE backend with a DIFFERENT
//  contract, linked instead of it by tests/tools/screen_shot.cpp alone. The two
//  never appear in the same binary.
//
//  WHAT "FAITHFUL" MEANS HERE, precisely - the three places the cheap fake is
//  deliberately unlike the panel, all of which this file has to undo:
//
//    1. GLYPHS. Real U8g2, the same version the firmware links (see
//       tools/fetch_u8g2.sh), the same five faces render.h names.
//    2. SOLID TEXT. render.cpp:366 leaves the panel in setFontMode(0), so a
//       glyph paints its own background. gfx_fb.cpp draws transparent. On
//       blank ground the two agree; over existing ink they do not.
//    3. TRIANGLES. render.cpp:878/890 draws the affordance markers with
//       drawTriangle(); gfx_fb.cpp squares them off. Every golden's bottom
//       strip therefore shows a shape the panel never draws.
//
//  WHAT IS NOT HERE, on purpose: the out-of-bounds recorder, the malformed-UTF8
//  recorder, the op counters and the golden diff. Those are how a TEST
//  interrogates a screen. This file draws a picture and hands it over; the
//  policing stays where the goldens are.
// =============================================================================
#include <stdio.h>
#include <string.h>

#include "gfx_fb.h"
#include "core/utf8.h"
#include "ui/gfx.h"

extern "C" {
#include "u8g2.h"
}

extern "C" {
extern const uint8_t u8g2_font_5x8_tf[];
extern const uint8_t u8g2_font_6x10_tf[];
extern const uint8_t u8g2_font_t0_11b_tf[];
extern const uint8_t u8g2_font_4x6_tr[];
extern const uint8_t u8g2_font_logisoso16_tn[];
}

static uint8_t  s_fb[FB_H][FB_W];
static uint8_t  s_color = GFX_DRAW;
static u8g2_t   s_u8;
static bool     s_ready = false;

// 4x4 ordered Bayer, the matrix render.cpp dithers with.
static const uint8_t FB_BAYER[16] = {
   0,  8,  2, 10,
  12,  4, 14,  6,
   3, 11,  1,  9,
  15,  7, 13,  5
};


// =============================================================================
//  THE RECORDERS
//
//  tests/test_screens.cpp asserts on all three of these, and this backend could
//  have returned zero from each and let every capture "pass". It does not, for
//  the reason this file exists: the fake's numbers are answers about a fixed
//  cell, and a real font can answer the same questions properly.
//
//  fb_oob() is the one that changes meaning. gfx_fb.cpp measures the ASCENT box
//  only, so it is blind to a descender or an accented capital leaving the panel
//  - render.cpp:854 already notes an A-acute reaching row 56 under a baseline
//  the fake thinks starts at 58. Here the box is the glyph's REAL extent, so
//  vertical overflow is visible for the first time. Expect findings; they are
//  findings about the firmware, not about this file.
// =============================================================================
static void     set_face(GfxFont f);
static uint32_t s_oob = 0;
static char     s_oob_msg[96];
static uint32_t s_bad_utf8 = 0;
static char     s_bad_utf8_msg[96];
static uint32_t s_no_glyph = 0;
static char     s_no_glyph_msg[80];
static uint32_t s_ops = 0;
static uint32_t s_px  = 0;

static void oob(const char* what, int x, int y, int w, int h) {
  if (s_oob == 0) {
    snprintf(s_oob_msg, sizeof(s_oob_msg), "%s(x=%d,y=%d,w=%d,h=%d)", what, x, y, w, h);
  }
  s_oob++;
}

uint32_t    fb_oob(void)            { return s_oob; }
const char* fb_oob_first(void)      { return s_oob_msg; }
uint32_t    fb_bad_utf8(void)       { return s_bad_utf8; }
const char* fb_bad_utf8_first(void) { return s_bad_utf8_msg; }
uint32_t    fb_no_glyph(void)       { return s_no_glyph; }
const char* fb_no_glyph_first(void) { return s_no_glyph_msg; }
uint32_t    fb_ops(void)            { return s_ops; }
uint32_t    fb_pixels(void)         { return s_px; }

// Every byte a screen hands the font must be a whole, well-formed sequence:
// on the device u8g2 opens a multi-byte state on a bad lead byte and swallows
// the NEXT character as well.
static void note_text(const char* s) {
  // core/utf8.h already owns this question, and fakes/gfx_fb.cpp already asks
  // it this way. Hand-rolling a second validator here got it wrong: a bare
  // Latin-1 lead byte with no continuation passed, which is exactly the shape
  // P10-C4's byte-wise truncation produces and the one the recorder exists for.
  if (s == nullptr || u8_well_formed(s)) return;
  if (s_bad_utf8 == 0) {
    size_t o = 0;
    for (const unsigned char* p = (const unsigned char*)s;
         *p != 0 && o + 5u < sizeof s_bad_utf8_msg; ++p) {
      if (*p >= 0x20u && *p < 0x7Fu) s_bad_utf8_msg[o++] = (char)*p;
      else o += (size_t)snprintf(s_bad_utf8_msg + o, sizeof s_bad_utf8_msg - o,
                                 "<%02X>", (unsigned)*p);
    }
    s_bad_utf8_msg[o] = '\0';
  }
  s_bad_utf8++;
}

// A face that has no such glyph draws NOTHING and advances NOTHING: the
// character vanishes and the line closes up. GF_TINY is ASCII-only and GF_BIG
// is digits-only, so this is a live hazard for Spanish strings. u8g2 can be
// asked directly, which is something the fixed-cell fake could never do.
static void note_missing(GfxFont f, const char* s) {
  if (s == nullptr) return;
  set_face(f);
  for (const char* p = s; *p; ) {
    const uint8_t n = u8_len(p);
    if (n == 0) return;
    char gl[5];
    uint8_t i = 0;
    while (i < n && p[i] != '\0') { gl[i] = p[i]; i++; }
    gl[i] = '\0';
    if (u8g2_GetUTF8Width(&s_u8, gl) == 0) {
      if (s_no_glyph == 0) {
        snprintf(s_no_glyph_msg, sizeof(s_no_glyph_msg), "font %d, \"%s\" in \"%s\"",
                 (int)f, gl, s);
      }
      s_no_glyph++;
    }
    p += n;
  }
}

// -----------------------------------------------------------------------------
//  The panel
// -----------------------------------------------------------------------------
static void put(int x, int y, uint8_t color) {
  if (x < 0 || y < 0 || x >= FB_W || y >= FB_H) return;
  ++s_px;
  if      (color == GFX_ERASE) s_fb[y][x] = 0;
  else if (color == GFX_XOR)   s_fb[y][x] = (uint8_t)(s_fb[y][x] ? 0 : 1);
  else                         s_fb[y][x] = 1;
}
static void put(int x, int y) { put(x, y, s_color); }

static bool inside(int x, int y, int w, int h) {
  return x >= 0 && y >= 0 && w >= 0 && h >= 0 && x + w <= FB_W && y + h <= FB_H;
}

int fb_get(int x, int y) {
  if (x < 0 || y < 0 || x >= FB_W || y >= FB_H) return 0;
  return s_fb[y][x] ? 1 : 0;
}

// -----------------------------------------------------------------------------
//  The one U8g2 drawing primitive the glyph decoder calls.
//
//  u8g2 coordinates are UNSIGNED (u8g2_uint_t is uint16_t), which is why
//  render.cpp's draw_utf8() drops whole leading codepoints instead of passing a
//  negative x: a negative x wraps to ~65500 and the string vanishes. The same
//  arithmetic is reproduced below rather than worked around.
//
//  In solid font mode u8g2 draws the glyph background by flipping its own
//  draw_color, so the colour has to be read from u8g2 per call, not captured.
// -----------------------------------------------------------------------------
extern "C" void u8g2_DrawHVLine(u8g2_t* u8g2, u8g2_uint_t x, u8g2_uint_t y,
                                u8g2_uint_t len, uint8_t dir) {
  // u8g2's draw_color: 1 = foreground, 0 = background. Map onto the seam's
  // sticky colour so a glyph drawn while the screen is in GFX_ERASE erases.
  uint8_t fg = s_color;
  uint8_t bg = (s_color == GFX_ERASE) ? GFX_DRAW : GFX_ERASE;
  const uint8_t color = (u8g2->draw_color != 0) ? fg : bg;
  for (u8g2_uint_t i = 0; i < len; i++) {
    const int px = (dir == 0) ? (int)(u8g2_uint_t)(x + i) : (int)x;
    const int py = (dir == 0) ? (int)y : (int)(u8g2_uint_t)(y + i);
    put(px, py, color);
  }
}

extern "C" uint8_t u8g2_IsIntersection(u8g2_t* u8g2, u8g2_uint_t x0, u8g2_uint_t y0,
                                       u8g2_uint_t x1, u8g2_uint_t y1) {
  // There is no tile window to clip against here: the framebuffer IS the panel
  // and put() clips. Always intersecting keeps every glyph on the decode path.
  (void)u8g2; (void)x0; (void)y0; (void)x1; (void)y1;
  return 1;
}

static const uint8_t* face_of(GfxFont f) {
  switch (f) {
    case GF_BODY: return u8g2_font_5x8_tf;
    case GF_NARR: return u8g2_font_6x10_tf;
    case GF_HEAD: return u8g2_font_t0_11b_tf;
    case GF_TINY: return u8g2_font_4x6_tr;
    case GF_BIG:  return u8g2_font_logisoso16_tn;
    default:      return u8g2_font_5x8_tf;
  }
}

static void ensure_u8g2(void) {
  if (s_ready) return;
  memset(&s_u8, 0, sizeof(s_u8));
  u8x8_utf8_init(u8g2_GetU8x8(&s_u8));
  u8g2_SetFontDirection(&s_u8, 0);
  u8g2_SetFontPosBaseline(&s_u8);
  u8g2_SetFontMode(&s_u8, 0);        // SOLID, as render.cpp:366 leaves the panel
  s_u8.draw_color = 1;
  s_ready = true;
}

static void set_face(GfxFont f) {
  ensure_u8g2();
  u8g2_SetFont(&s_u8, face_of(f));
}

void fb_reset(void) {
  memset(s_fb, 0, sizeof(s_fb));
  s_color = GFX_DRAW;
  s_oob = 0;  s_oob_msg[0] = '\0';
  s_bad_utf8 = 0; s_bad_utf8_msg[0] = '\0';
  s_no_glyph = 0; s_no_glyph_msg[0] = '\0';
  s_ops = 0;  s_px = 0;
}

// -----------------------------------------------------------------------------
//  Primitives. Same arithmetic as fakes/gfx_fb.cpp, without the recorder: a
//  divergence here would be the P10-C3 defect all over again, so they are ports
//  rather than re-derivations. tests/tools/screen_shot.cpp --selftest proves
//  the two backends still agree on every non-text primitive.
// -----------------------------------------------------------------------------
void gfx_color(uint8_t c) { s_color = c; }

void gfx_pixel(int16_t x, int16_t y) {
  ++s_ops;
  if (!inside(x, y, 1, 1)) oob("pixel", x, y, 1, 1);
  put(x, y);
}

void gfx_hline(int16_t x, int16_t y, int16_t w) {
  if (w <= 0) return;
  ++s_ops;
  if (!inside(x, y, w, 1)) oob("hline", x, y, w, 1);
  for (int16_t i = 0; i < w; i++) put(x + i, y);
}

void gfx_vline(int16_t x, int16_t y, int16_t h) {
  if (h <= 0) return;
  ++s_ops;
  if (!inside(x, y, 1, h)) oob("vline", x, y, 1, h);
  for (int16_t i = 0; i < h; i++) put(x, y + i);
}

void gfx_rect(int16_t x, int16_t y, int16_t w, int16_t h) {
  if (w <= 0 || h <= 0) return;
  ++s_ops;
  if (!inside(x, y, w, h)) oob("rect", x, y, w, h);
  for (int16_t i = 0; i < w; i++) { put(x + i, y); put(x + i, y + h - 1); }
  for (int16_t i = 0; i < h; i++) { put(x, y + i); put(x + w - 1, y + i); }
}

void gfx_fill(int16_t x, int16_t y, int16_t w, int16_t h) {
  if (w <= 0 || h <= 0) return;
  ++s_ops;
  if (!inside(x, y, w, h)) oob("fill", x, y, w, h);
  for (int16_t r = 0; r < h; r++)
    for (int16_t c = 0; c < w; c++) put(x + c, y + r);
}

static uint8_t inverse_of(uint8_t c) {
  if (c == GFX_DRAW)  return GFX_ERASE;
  if (c == GFX_ERASE) return GFX_DRAW;
  return GFX_XOR;
}

// Opaque, because render.cpp:367 leaves setBitmapMode(0): the 0-bits paint in
// the inverse of the draw colour rather than being skipped.
static void blit(int16_t x, int16_t y, int16_t w, int16_t h,
                 const uint8_t* bits, bool opaque, const char* what) {
  if (bits == nullptr || w <= 0 || h <= 0) return;
  ++s_ops;
  if (!inside(x, y, w, h)) oob(what, x, y, w, h);
  const uint8_t keep = s_color;
  const uint8_t inv  = inverse_of(keep);
  const int stride = (w + 7) / 8;
  for (int16_t r = 0; r < h; r++) {
    for (int16_t c = 0; c < w; c++) {
      const uint8_t byte = bits[r * stride + (c >> 3)];
      if (byte & (uint8_t)(1u << (c & 7))) put(x + c, y + r, keep);
      else if (opaque)                     put(x + c, y + r, inv);
    }
  }
}

void gfx_xbm(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t* bits) {
  blit(x, y, w, h, bits, true, "xbm");
}
void gfx_xbm_t(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t* bits) {
  blit(x, y, w, h, bits, false, "xbm_t");
}

void gfx_dither_rect_phase(int16_t x, int16_t y, int16_t w, int16_t h,
                           uint8_t level, uint8_t phase) {
  if (level == 0 || w <= 0 || h <= 0) return;
  ++s_ops;
  if (!inside(x, y, w, h)) oob("dither", x, y, w, h);
  const int dx = (int)(phase & 3u);
  const int dy = (int)((phase >> 2) & 3u);
  for (int16_t r = 0; r < h; r++) {
    for (int16_t c = 0; c < w; c++) {
      const int px = x + c, py = y + r;
      if (px < 0 || py < 0) continue;
      if (FB_BAYER[(((py + dy) & 3) * 4) + ((px + dx) & 3)] < level) put(px, py);
    }
  }
}

void gfx_dither_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t level) {
  gfx_dither_rect_phase(x, y, w, h, level, 0u);
}

void gfx_invert_rect(int16_t x, int16_t y, int16_t w, int16_t h) {
  const uint8_t keep = s_color;
  s_color = GFX_XOR;
  gfx_fill(x, y, w, h);
  s_color = keep;
}

// -----------------------------------------------------------------------------
//  Text. Real glyphs, and the DEVICE's arithmetic around them - render.cpp's
//  draw_utf8() and fit_copy(), not gfx_fb.cpp's fixed-cell versions.
// -----------------------------------------------------------------------------
uint16_t gfx_text_w(GfxFont f, const char* s) {
  if (s == nullptr || *s == '\0') return 0;
  note_text(s);
  set_face(f);
  return (uint16_t)u8g2_GetUTF8Width(&s_u8, s);
}

uint16_t gfx_text(GfxFont f, int16_t x, int16_t y, const char* s) {
  if (s == nullptr || *s == '\0') return 0;
  if (y < 0 || y >= (int16_t)(FB_H + 16)) return 0;
  note_text(s);
  note_missing(f, s);
  set_face(f);
  // render.cpp:141-152: a negative x drops WHOLE leading codepoints, because
  // u8g2 would wrap it to ~65500 and clip the string away entirely.
  while (x < 0 && *s != '\0') {
    char gl[5];
    const uint8_t n = u8_len(s);
    uint8_t i = 0;
    while (i < n && s[i] != '\0') { gl[i] = s[i]; i++; }
    gl[i] = '\0';
    if (i == 0) break;
    int16_t gw = (int16_t)u8g2_GetUTF8Width(&s_u8, gl);
    if (gw <= 0) gw = 1;                     // no glyph in this face: still advance
    x = (int16_t)(x + gw);
    s += i;
  }
  if (*s == '\0' || x >= (int16_t)FB_W) return 0;
  ++s_ops;
  // THE REAL GLYPH BOX, which is the point. u8g2 gives the face's ascent and
  // descent, so an accent that overshoots or a descender that drops below the
  // panel is visible here for the first time.
  const int16_t asc  = (int16_t)u8g2_GetAscent(&s_u8);
  const int16_t desc = (int16_t)u8g2_GetDescent(&s_u8);   // negative
  const int16_t top  = (int16_t)(y - asc);
  const int16_t hgt  = (int16_t)(asc - desc);
  const int16_t wid  = (int16_t)u8g2_GetUTF8Width(&s_u8, s);
  if (!inside(x, top, wid, hgt)) oob("text", x, top, wid, hgt);
  return (uint16_t)u8g2_DrawUTF8(&s_u8, (u8g2_uint_t)x, (u8g2_uint_t)y, s);
}

uint16_t gfx_text_center(GfxFont f, int16_t y, const char* s) {
  if (s == nullptr || *s == '\0') return 0;
  const int16_t w = (int16_t)gfx_text_w(f, s);
  return gfx_text(f, (int16_t)((FB_W - w) / 2), y, s);
}

uint16_t gfx_text_right(GfxFont f, int16_t x_right, int16_t y, const char* s) {
  if (s == nullptr || *s == '\0') return 0;
  const int16_t w = (int16_t)gfx_text_w(f, s);
  return gfx_text(f, (int16_t)(x_right - w + 1), y, s);
}

// render.cpp's fit_copy(): drop whole trailing codepoints until it measures no
// more than max_w. dst and s may alias.
static uint16_t fit_copy(GfxFont f, char* dst, size_t dst_sz,
                         const char* s, int16_t max_w) {
  size_t len = 0;
  if (s != nullptr) {
    const size_t room = (dst_sz > 0u) ? (dst_sz - 1u) : 0u;
    len = u8_fit(s, (room > 0xFFFFu) ? 0xFFFFu : (uint16_t)room);
    if (dst != s) memmove(dst, s, len);
  }
  dst[len] = '\0';
  if (max_w <= 0) { dst[0] = '\0'; return 0; }
  uint16_t w = gfx_text_w(f, dst);
  while (w > (uint16_t)max_w && len > 0) {
    size_t cut = len - 1;
    while (cut > 0 && ((uint8_t)dst[cut] & 0xC0) == 0x80) cut--;
    len = cut;
    dst[len] = '\0';
    w = gfx_text_w(f, dst);
  }
  return w;
}

uint16_t gfx_text_fit(GfxFont f, int16_t x, int16_t y, int16_t max_w, const char* s) {
  if (s == nullptr || *s == '\0' || max_w <= 0) return 0;
  char buf[96];                              // render.cpp's RD_SCRATCH
  fit_copy(f, buf, sizeof(buf), s, max_w);
  if (buf[0] == '\0') return 0;
  return gfx_text(f, x, y, buf);
}

uint8_t gfx_text_wrap(GfxFont f, int16_t x, int16_t y, int16_t w,
                      uint8_t line_h, uint8_t max_lines, const char* s) {
  if (s == nullptr || *s == '\0' || w <= 0 || max_lines == 0) return 0;
  if (line_h == 0) line_h = GFX_LINE_BODY;   // render.cpp: BODY pitch, any font
  char line[96];
  uint8_t drawn = 0;
  int16_t cy = y;
  const char* p = s;
  line[0] = '\0';

  while (*p != '\0' && drawn < max_lines) {
    const char* word = p;
    while (*p != '\0' && *p != ' ' && *p != '\n') p += u8_len(p);
    const size_t wl = (size_t)(p - word);

    char cand[96];
    const size_t have = strlen(line);
    size_t need = have + (have ? 1u : 0u) + wl;
    if (need >= sizeof(cand)) need = sizeof(cand) - 1u;
    memcpy(cand, line, have);
    size_t k = have;
    if (have && k < sizeof(cand) - 1u) cand[k++] = ' ';
    for (size_t i = 0; i < wl && k < sizeof(cand) - 1u; i++) cand[k++] = word[i];
    cand[k] = '\0';

    if ((int16_t)gfx_text_w(f, cand) <= w) {
      memcpy(line, cand, k + 1u);
    } else {
      if (line[0] != '\0') {
        gfx_text(f, x, cy, line);
        cy = (int16_t)(cy + line_h);
        drawn++;
        line[0] = '\0';
        if (drawn >= max_lines) break;
        p = word;                            // retry the word on the new line
        continue;
      }
      // A single word wider than the box: hard-break it.
      char part[96];
      const size_t used = fit_copy(f, part, sizeof(part), word, w);
      (void)used;
      if (part[0] == '\0') break;
      gfx_text(f, x, cy, part);
      cy = (int16_t)(cy + line_h);
      drawn++;
      p = word + strlen(part);
      continue;
    }
    while (*p == ' ') p++;
    if (*p == '\n') {
      p++;
      gfx_text(f, x, cy, line);
      cy = (int16_t)(cy + line_h);
      drawn++;
      line[0] = '\0';
    }
  }
  if (line[0] != '\0' && drawn < max_lines) {
    gfx_text(f, x, cy, line);
    drawn++;
  }
  return drawn;
}

// -----------------------------------------------------------------------------
//  Affordance. render.cpp:870-900 - and the markers are TRIANGLES.
// -----------------------------------------------------------------------------
static void triangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                     int16_t x2, int16_t y2) {
  // The two markers are 4x5 right triangles pointing at the panel edge. Filling
  // by scanline over the bounding box is exact at this size and needs no edge
  // walker: the shape is only four columns wide.
  const int16_t minx = (x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2));
  const int16_t maxx = (x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2));
  const int16_t miny = (y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2));
  const int16_t maxy = (y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2));
  for (int16_t py = miny; py <= maxy; py++) {
    for (int16_t px = minx; px <= maxx; px++) {
      // Barycentric sign test, integer.
      const int d1 = (px - x1) * (y0 - y1) - (x0 - x1) * (py - y1);
      const int d2 = (px - x2) * (y1 - y2) - (x1 - x2) * (py - y2);
      const int d3 = (px - x0) * (y2 - y0) - (x2 - x0) * (py - y0);
      const bool neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
      const bool pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
      if (!(neg && pos)) put(px, py);
    }
  }
}

void gfx_affordance(const char* left, const char* right) {
  const bool has_l = (left  != nullptr && *left  != '\0');
  const bool has_r = (right != nullptr && *right != '\0');
  if (!has_l && !has_r) return;

  const int16_t bl = FB_H - 1;             // 63
  int16_t l_end   = -1;
  int16_t r_start = FB_W;

  if (has_r) {
    triangle(127, 60, 124, 58, 124, 62);     // render.cpp:878
    char rbuf[40];                           // render.cpp's own buffer size
    const int16_t rw = (int16_t)fit_copy(GF_BODY, rbuf, sizeof(rbuf), right, 60);
    if (rbuf[0] != '\0') {
      r_start = (int16_t)(122 - rw + 1);
      gfx_text(GF_BODY, r_start, bl, rbuf);
    } else {
      r_start = 124;
    }
  }

  if (has_l) {
    triangle(0, 60, 3, 58, 3, 62);           // render.cpp:890
    const int16_t avail = (int16_t)(r_start - 4 - 5);
    char lbuf[40];
    const int16_t lw = (int16_t)fit_copy(GF_BODY, lbuf, sizeof(lbuf), left, avail);
    if (lbuf[0] != '\0') {
      gfx_text(GF_BODY, 5, bl, lbuf);
      l_end = (int16_t)(5 + lw - 1);
    } else {
      l_end = 3;
    }
  }

  const int16_t gap0 = (int16_t)(l_end + 4);
  const int16_t gap1 = (int16_t)(r_start - 4);
  if (has_l && has_r && (gap1 - gap0) >= 8) {
    for (int16_t gx = gap0; gx <= gap1; gx = (int16_t)(gx + 3)) gfx_pixel(gx, 60);
  }
}

// -----------------------------------------------------------------------------
//  Output
// -----------------------------------------------------------------------------
bool fb_write_pbm(const char* path) {
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  fprintf(f, "P1\n# Pebblebol screen capture, real fonts, %dx%d\n%d %d\n",
          FB_W, FB_H, FB_W, FB_H);
  for (int y = 0; y < FB_H; y++) {
    for (int x = 0; x < FB_W; x++) fputc(s_fb[y][x] ? '1' : '0', f);
    fputc('\n', f);
  }
  fclose(f);
  return true;
}

int fb_diff_pbm(const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) return -1;
  char line[512];
  int  w = 0, h = 0, got = 0, diff = 0, row = 0;
  while (fgets(line, sizeof(line), f)) {
    if (line[0] == '#') continue;
    if (!got) {
      if (line[0] == 'P') continue;
      if (sscanf(line, "%d %d", &w, &h) == 2) { got = 1; continue; }
      continue;
    }
    if (row >= FB_H) break;
    for (int x = 0; x < FB_W && line[x] == '0'; x++) { }
    for (int x = 0; x < FB_W; x++) {
      const int want = (line[x] == '1') ? 1 : 0;
      if (want != (s_fb[row][x] ? 1 : 0)) diff++;
    }
    row++;
  }
  fclose(f);
  if (w != FB_W || h != FB_H) return -1;
  return diff;
}

void fb_dump(void) {
  for (int y = 0; y < FB_H; y++) {
    for (int x = 0; x < FB_W; x++) fputc(s_fb[y][x] ? '#' : '.', stdout);
    fputc('\n', stdout);
  }
}
