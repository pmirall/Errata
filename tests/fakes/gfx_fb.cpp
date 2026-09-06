// =============================================================================
//  Pebblebol host tests - fakes/gfx_fb.cpp
//  The host implementation of ui/gfx.h. See gfx_fb.h for what it is for.
// =============================================================================
#include "gfx_fb.h"

#include <stdio.h>
#include <string.h>

#include "ui/gfx.h"

static uint8_t     s_fb[FB_H][FB_W];
static uint8_t     s_color   = GFX_DRAW;
static uint32_t    s_oob     = 0;
static char        s_oob_msg[96];

// 4x4 ordered Bayer, the same matrix render.cpp dithers with.
static const uint8_t FB_BAYER[16] = {
   0,  8,  2, 10,
  12,  4, 14,  6,
   3, 11,  1,  9,
  15,  7, 13,  5
};

// -----------------------------------------------------------------------------
//  Recorder
// -----------------------------------------------------------------------------
static void oob(const char* what, int x, int y, int w, int h) {
  if (s_oob == 0) {
    snprintf(s_oob_msg, sizeof(s_oob_msg), "%s(x=%d,y=%d,w=%d,h=%d)", what, x, y, w, h);
  }
  s_oob++;
}

uint32_t    fb_oob(void)       { return s_oob; }
const char* fb_oob_first(void) { return s_oob_msg; }

// =============================================================================
//  THE WORK COUNTERS (P10-C2)
//
//  READ THE LIMIT BEFORE READING THE NUMBER. THESE ARE NOT A FRAME TIME AND
//  MAY NOT BE TURNED INTO ONE. On the device gfx_fill() writes into a RAM
//  buffer and the frame's dominant cost is a FIXED ~24 ms of I2C in
//  sendBuffer(), charged once however much was drawn (ui/render.cpp says so at
//  the top of rd_end_frame()). So pixels-touched is a proxy for the SMALL half
//  of a frame; a "frame <= 50 ms" gate built on it would be measuring the
//  minor term and calling it performance, which is precisely the shape of
//  defect this repository keeps finding.
//
//  WHAT THEY ARE FOR: a COMPOSITING RUNAWAY detector. A screen that fills the
//  same region under three other layers, or loops over all sixty species in a
//  render hook, blows the pixel count while the golden may not move at all -
//  and a golden cannot report "identical picture, three times the work".
//
//  AND ONE MORE LIMIT, which is the load-bearing one. ui/petfx.cpp, ui/actfx.cpp
//  and ui/ceremony.cpp include render.h and are compiled by NO host binary, so
//  HOME's number here is a count of a composition that does not ship:
//  home_render() draws the pet through s_body, which is home_body() (ui.cpp) on
//  the device and NULL here, so the wandering automaton, the action films and
//  the emotes are all absent. BATTLE, BOX, MENU and the list screens are pure
//  gfx.h and host-linked, so their numbers are honest. tests/test_screens.cpp
//  says which is which where it asserts the ceiling.
// -----------------------------------------------------------------------------
static uint32_t s_ops = 0;
static uint32_t s_px  = 0;

uint32_t fb_ops(void)    { return s_ops; }
uint32_t fb_pixels(void) { return s_px; }

void fb_reset(void) {
  memset(s_fb, 0, sizeof(s_fb));
  s_color      = GFX_DRAW;
  s_oob        = 0;
  s_oob_msg[0] = '\0';
  s_ops        = 0;
  s_px         = 0;
}

int fb_get(int x, int y) {
  if (x < 0 || y < 0 || x >= FB_W || y >= FB_H) return 0;
  return s_fb[y][x] ? 1 : 0;
}

// The one place a pixel is written. Silently clips; the CALLER records the
// out-of-bounds request, because "one primitive off the edge" is the
// interesting unit, not "480 pixels off the edge".
static void put(int x, int y) {
  if (x < 0 || y < 0 || x >= FB_W || y >= FB_H) return;
  ++s_px;
  if      (s_color == GFX_ERASE) s_fb[y][x] = 0;
  else if (s_color == GFX_XOR)   s_fb[y][x] = (uint8_t)(s_fb[y][x] ? 0 : 1);
  else                           s_fb[y][x] = 1;
}

static bool inside(int x, int y, int w, int h) {
  return x >= 0 && y >= 0 && w >= 0 && h >= 0 && x + w <= FB_W && y + h <= FB_H;
}

// -----------------------------------------------------------------------------
//  Primitives
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

// =============================================================================
//  THE TWO BLITS, AND WHY THIS FILE USED TO BE WRONG ABOUT THE FIRST ONE
//
//  Until P10-C3 there was one gfx_xbm() here and it drew ONLY the 1-bits, while
//  the device's drawXBM under render.cpp:368's setBitmapMode(0) paints the
//  WHOLE w*h box - the 0-bits in the inverse of the draw colour. So the two
//  backends disagreed about every blit in the firmware and no test in the tree
//  could see it, because every call site at that commit happened to draw onto
//  blank ground (where the inverse writes 0 over 0) or, in ui/dialog.cpp:130, in
//  GFX_ERASE onto a solid slab (where the inverse writes 1 over 1). All 65
//  goldens were correct BY LUCK OF COMPOSITION.
//
//  P10-C3 is the chunk that would have ended that luck silently: every film it
//  adds draws a sprite on top of the body, the floor or a filled panel, and the
//  first one to do so would have had the device erase a w*h hole that the
//  golden did not show. So the fake tells the truth now and the seam is
//  explicit. tests/test_screens.cpp drives both by name over prior ink.
//
//  THE INVERSE OF GFX_XOR IS GFX_XOR. No call site draws an opaque XBM in XOR
//  mode - it is not a picture anybody wants - and u8g2's own behaviour there is
//  undefined enough that guessing it here would be inventing a device fact.
//  Stated rather than silently folded into `else`.
// =============================================================================
static uint8_t inverse_of(uint8_t c) {
  if (c == GFX_DRAW)  return GFX_ERASE;
  if (c == GFX_ERASE) return GFX_DRAW;
  return GFX_XOR;
}

static void blit(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t* bits,
                 bool opaque, const char* what) {
  if (bits == nullptr || w <= 0 || h <= 0) return;
  ++s_ops;
  if (!inside(x, y, w, h)) oob(what, x, y, w, h);
  const uint8_t keep = s_color;
  const uint8_t inv  = inverse_of(keep);
  const int stride = (w + 7) / 8;
  for (int16_t r = 0; r < h; r++) {
    for (int16_t c = 0; c < w; c++) {
      const uint8_t byte = bits[r * stride + (c >> 3)];
      if (byte & (uint8_t)(1u << (c & 7))) { s_color = keep; put(x + c, y + r); }
      else if (opaque)                     { s_color = inv;  put(x + c, y + r); }
    }
  }
  s_color = keep;
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
  // Same masking render.cpp does, and for the same reason: a genome nibble is
  // passed in raw, so only the low 4 bits are meaningful.
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
//  Text. UTF-8 in, one fixed-advance cell per codepoint out.
// -----------------------------------------------------------------------------
static uint8_t seq_len(uint8_t c) {
  if (c < 0x80) return 1;
  if ((c & 0xE0) == 0xC0) return 2;
  if ((c & 0xF0) == 0xE0) return 3;
  if ((c & 0xF8) == 0xF0) return 4;
  return 1;                                   // malformed: step one byte
}

static uint32_t decode(const char* s, uint8_t len) {
  const uint8_t* p = (const uint8_t*)s;
  switch (len) {
    case 2:  return (uint32_t)(((p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu));
    case 3:  return (uint32_t)(((p[0] & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu));
    case 4:  return (uint32_t)(((p[0] & 0x07u) << 18) | ((p[1] & 0x3Fu) << 12) |
                               ((p[2] & 0x3Fu) << 6) | (p[3] & 0x3Fu));
    default: return p[0];
  }
}

static uint16_t count_cps(const char* s) {
  uint16_t n = 0;
  for (const char* p = s; *p; p += seq_len((uint8_t)*p)) n++;
  return n;
}

uint16_t gfx_text_w(GfxFont f, const char* s) {
  if (s == nullptr || *s == '\0') return 0;
  return (uint16_t)(count_cps(s) * gfx_font_adv(f));
}

// One glyph: the baseline row always, plus the rows the codepoint's bits pick
// out. Deterministic, string-sensitive, and never wider than the advance.
static void glyph(GfxFont f, int16_t x, int16_t y, uint32_t cp) {
  ++s_ops;
  const int adv = gfx_font_adv(f);
  const int asc = gfx_font_asc(f);
  const int w   = adv - 1;
  if (!inside(x, (int16_t)(y - asc + 1), (int16_t)w, (int16_t)asc)) {
    oob("text", x, (int16_t)(y - asc + 1), (int16_t)w, (int16_t)asc);
  }
  for (int r = 0; r < asc; r++) {
    const bool on = (r == asc - 1) || (((cp >> (r % 8)) & 1u) != 0u);
    if (!on) continue;
    for (int c = 0; c < w; c++) put(x + c, y - asc + 1 + r);
  }
}

uint16_t gfx_text(GfxFont f, int16_t x, int16_t y, const char* s) {
  if (s == nullptr || *s == '\0') return 0;
  const int adv = gfx_font_adv(f);
  int16_t cx = x;
  for (const char* p = s; *p; ) {
    const uint8_t n = seq_len((uint8_t)*p);
    glyph(f, cx, y, decode(p, n));
    cx = (int16_t)(cx + adv);
    p += n;
  }
  return (uint16_t)(cx - x);
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

// How many BYTES of s fit in max_w, on a codepoint boundary.
static size_t fit_bytes(GfxFont f, const char* s, int16_t max_w) {
  const int adv = gfx_font_adv(f);
  size_t used = 0;
  int16_t w = 0;
  for (const char* p = s; *p; ) {
    if ((int16_t)(w + adv) > max_w) break;
    const uint8_t n = seq_len((uint8_t)*p);
    w = (int16_t)(w + adv);
    used += n;
    p += n;
  }
  return used;
}

uint16_t gfx_text_fit(GfxFont f, int16_t x, int16_t y, int16_t max_w, const char* s) {
  if (s == nullptr || *s == '\0' || max_w <= 0) return 0;
  char buf[128];
  size_t take = fit_bytes(f, s, max_w);
  if (take >= sizeof(buf)) take = sizeof(buf) - 1;
  memcpy(buf, s, take);
  buf[take] = '\0';
  return gfx_text(f, x, y, buf);
}

// Mirrors rd_text_wrap(): collapse spaces at a break, honour '\n', break on
// spaces, hard-break a word wider than the box.
uint8_t gfx_text_wrap(GfxFont f, int16_t x, int16_t y, int16_t w,
                      uint8_t line_h, uint8_t max_lines, const char* s) {
  if (s == nullptr || *s == '\0' || w <= 0 || max_lines == 0) return 0;
  if (line_h == 0) line_h = gfx_font_line(f);

  char    line[128];
  size_t  line_len = 0;
  uint8_t drawn    = 0;
  int16_t cy       = y;
  line[0] = '\0';

  const char* p = s;
  while (*p != '\0' && drawn < max_lines) {
    while (*p == ' ') p++;
    if (*p == '\0') break;
    if (*p == '\n') {
      gfx_text(f, x, cy, line);
      drawn++; cy = (int16_t)(cy + line_h);
      line[0] = '\0'; line_len = 0; p++;
      continue;
    }

    const char* wstart = p;
    while (*p != '\0' && *p != ' ' && *p != '\n') p += seq_len((uint8_t)*p);
    const size_t wlen = (size_t)(p - wstart);
    if (wlen == 0) break;

    const size_t need = line_len + (line_len ? 1u : 0u) + wlen;
    if (need < sizeof(line)) {
      char cand[128];
      memcpy(cand, line, line_len);
      size_t cl = line_len;
      if (cl) cand[cl++] = ' ';
      memcpy(cand + cl, wstart, wlen);
      cl += wlen;
      cand[cl] = '\0';
      if ((int16_t)gfx_text_w(f, cand) <= w) {
        memcpy(line, cand, cl + 1);
        line_len = cl;
        continue;
      }
    }

    if (line_len > 0) {
      gfx_text(f, x, cy, line);
      drawn++; cy = (int16_t)(cy + line_h);
      line[0] = '\0'; line_len = 0;
      p = wstart;
      continue;
    }

    char part[128];
    size_t take = (wlen < sizeof(part) - 1) ? wlen : sizeof(part) - 1;
    memcpy(part, wstart, take);
    part[take] = '\0';
    const size_t used = fit_bytes(f, part, w);
    if (used == 0) break;
    part[used] = '\0';
    gfx_text(f, x, cy, part);
    drawn++; cy = (int16_t)(cy + line_h);
    p = wstart + used;
  }

  if (line_len > 0 && drawn < max_lines) {
    gfx_text(f, x, cy, line);
    drawn++;
  }
  return drawn;
}

// -----------------------------------------------------------------------------
//  Affordance strip. Same geometry as rd_affordance() - markers in columns
//  0..3 and 124..127, labels at 5 and right-aligned on 122, baseline 63, a
//  dotted divider between them - drawn with the primitives so that a label too
//  long for the strip is recorded as out of bounds like anything else.
// -----------------------------------------------------------------------------
void gfx_affordance(const char* left, const char* right) {
  const bool has_l = (left  != nullptr && *left  != '\0');
  const bool has_r = (right != nullptr && *right != '\0');
  if (!has_l && !has_r) return;

  const int16_t bl = FB_H - 1;               // 63
  int16_t l_end   = -1;
  int16_t r_start = FB_W;

  if (has_r) {
    gfx_fill(124, 58, 4, 5);                 // the marker, squared off
    char buf[64];
    size_t take = fit_bytes(GF_BODY, right, 60);
    if (take >= sizeof(buf)) take = sizeof(buf) - 1;
    memcpy(buf, right, take);
    buf[take] = '\0';
    const int16_t rw = (int16_t)gfx_text_w(GF_BODY, buf);
    if (buf[0] != '\0') {
      r_start = (int16_t)(122 - rw + 1);
      gfx_text(GF_BODY, r_start, bl, buf);
    } else {
      r_start = 124;
    }
  }

  if (has_l) {
    gfx_fill(0, 58, 4, 5);
    const int16_t avail = (int16_t)(r_start - 4 - 5);
    char buf[64];
    size_t take = fit_bytes(GF_BODY, left, avail);
    if (take >= sizeof(buf)) take = sizeof(buf) - 1;
    memcpy(buf, left, take);
    buf[take] = '\0';
    const int16_t lw = (int16_t)gfx_text_w(GF_BODY, buf);
    if (buf[0] != '\0') {
      gfx_text(GF_BODY, 5, bl, buf);
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
//  Golden files
// -----------------------------------------------------------------------------
bool fb_write_pbm(const char* path) {
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  fprintf(f, "P1\n# Pebblebol screen snapshot, %dx%d\n%d %d\n", FB_W, FB_H, FB_W, FB_H);
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
  char magic[3] = { 0, 0, 0 };
  if (fscanf(f, "%2s", magic) != 1 || strcmp(magic, "P1") != 0) { fclose(f); return -1; }
  int w = 0, h = 0;
  int c = fgetc(f);
  while (c == '\n' || c == ' ' || c == '\r' || c == '\t' || c == '#') {
    if (c == '#') { while (c != '\n' && c != EOF) c = fgetc(f); }
    c = fgetc(f);
  }
  ungetc(c, f);
  if (fscanf(f, "%d %d", &w, &h) != 2 || w != FB_W || h != FB_H) { fclose(f); return -1; }

  int diff = 0, seen = 0;
  while (seen < FB_W * FB_H) {
    c = fgetc(f);
    if (c == EOF) { fclose(f); return -1; }
    if (c != '0' && c != '1') continue;
    const int x = seen % FB_W, y = seen / FB_W;
    if ((c == '1' ? 1 : 0) != (s_fb[y][x] ? 1 : 0)) diff++;
    seen++;
  }
  fclose(f);
  return diff;
}

void fb_dump(void) {
  for (int y = 0; y < FB_H; y++) {
    for (int x = 0; x < FB_W; x++) fputc(s_fb[y][x] ? '#' : '.', stdout);
    fputc('\n', stdout);
  }
}
