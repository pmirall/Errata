// =============================================================================
//  tests/tools/corrupt_view.cpp - WHAT THE CORRUPTION GLITCH ACTUALLY LOOKS
//  LIKE (P9-C5).
//
//  WHY IT EXISTS, and it is the same argument tests/tools/sprite_dump.cpp
//  makes about bodies. Every automated check this chunk can offer about the
//  glitch is a check about GEOMETRY: that the rows are inside the ink box, that
//  they change some pixels and not too many, that the gate lights about one
//  slot in eight. NONE OF THEM CAN SEE WHETHER THE EFFECT READS AS CORRUPTION.
//  Three rows of noise at 1/16 would pass every one of them and be invisible on
//  the panel; three rows at 15/16 would pass them too and erase the animal.
//
//  So this binary draws the real thing and prints it. It places a real 24x24
//  body from the compiled atlas at the real floor line, scans its real ink
//  bounds exactly as ui/petfx.cpp's pf_scan_ink() does, asks the SHIPPED
//  ui/corrupt_fx.cpp for the rows, and XORs them in with the SHIPPED Bayer
//  matrix. What appears here is what the panel will show.
//
//  WHAT IT IS NOT. It is not a test and it asserts nothing: it prints a picture
//  and exits 0. tests/test_corruption.cpp is the part that asserts, and it is
//  deliberately a different question - "does it stay inside the box" rather
//  than "does it look right".
//
//  WHAT IT STILL CANNOT SHOW, said rather than implied: MOTION. The altered
//  idle animation is a row of dwell times and state weights in
//  ui/petfx.cpp's PF_TEMPER, and a still frame cannot render "the creature
//  stutters instead of settling". The `temper` mode below prints the two rows
//  side by side as NUMBERS, which is honest about being a table and not a
//  picture, and the only way to see the motion is a board.
//
//  Usage
//    ./bin/corrupt_view                       species 1, 8 lit slots
//    ./bin/corrupt_view SPECIES [SLOTS]       any species id, any slot count
//    ./bin/corrupt_view strip SPECIES         one line per slot over a minute,
//                                             so the 1-in-8 rate is visible
//    ./bin/corrupt_view temper                the two behaviour rows, numbered
//
//  Built by `make -C tests corrupttool`, NOT by `make check`.
//
//  All identifiers and comments English.
// =============================================================================
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "data/sprites.h"
#include "data/sprites_pebbles.h"
#include "ui/corrupt_fx.h"

// The panel, as ui/render.cpp sees it.
#define VW  128
#define VH   64

// ui/petfx.h's floor line. Not included, because petfx.h drags core/config.h
// and the whole ui layer; the number is quoted with its source so a reader can
// check it, and it is the ONLY thing borrowed rather than linked.
#define VIEW_FLOOR_Y     52    // PETFX_FLOOR_Y
#define VIEW_STAGE_L     14    // PETFX_STAGE_L
#define VIEW_SPRITE_TOP   9    // SPRITE_AREA_Y

// ui/render.cpp:44's RD_BAYER, and tests/fakes/gfx_fb.cpp's FB_BAYER, which are
// the same sixteen numbers. Copied here for the same reason the floor line is:
// render.cpp lives behind U8g2lib.h and cannot be linked on the host.
static const uint8_t VIEW_BAYER[16] = {
   0,  8,  2, 10,
  12,  4, 14,  6,
   3, 11,  1,  9,
  15,  7, 13,  5
};

static uint8_t g_fb[VH][VW];

static void fb_clear(void) { memset(g_fb, 0, sizeof g_fb); }

// drawXBM in setBitmapMode(1): the 1 bits paint, the 0 bits are transparent.
// LSB-first rows padded to whole bytes, which is the sprites.h layout.
static void fb_xbm(int x, int y, const SpriteRef& s)
{
  const int stride = (s.w + 7) / 8;
  for (int r = 0; r < (int)s.h; ++r) {
    for (int c = 0; c < (int)s.w; ++c) {
      if ((s.bits[r * stride + (c >> 3)] & (uint8_t)(1u << (c & 7))) == 0u) continue;
      const int px = x + c, py = y + r;
      if (px < 0 || px >= VW || py < 0 || py >= VH) continue;
      g_fb[py][px] = 1;
    }
  }
}

// setDrawColor(2): XOR. Exactly what rd_dither_rect_phase() does for one row.
static void fb_xor_row(const CfxRow& row)
{
  const uint8_t dx = (uint8_t)(row.phase & 3u);
  const uint8_t dy = (uint8_t)((row.phase >> 2) & 3u);
  for (int i = 0; i < (int)row.w; ++i) {
    const int px = row.x + i, py = row.y;
    if (px < 0 || px >= VW || py < 0 || py >= VH) continue;
    if (VIEW_BAYER[((((py + dy) & 3) * 4) + ((px + dx) & 3))] >= row.level) continue;
    g_fb[py][px] = (uint8_t)(g_fb[py][px] ? 0 : 1);
  }
}

// ui/petfx.cpp's pf_scan_ink(), in the one form this tool needs: the rows and
// columns of the frame that carry any ink at all.
struct Ink { int t, b, l, r; };
static Ink scan_ink(const SpriteRef& s)
{
  Ink k = { (int)s.h, -1, (int)s.w, -1 };
  const int stride = (s.w + 7) / 8;
  for (int r = 0; r < (int)s.h; ++r) {
    for (int c = 0; c < (int)s.w; ++c) {
      if ((s.bits[r * stride + (c >> 3)] & (uint8_t)(1u << (c & 7))) == 0u) continue;
      if (r < k.t) k.t = r;
      if (r > k.b) k.b = r;
      if (c < k.l) k.l = c;
      if (c > k.r) k.r = c;
    }
  }
  if (k.b < 0) { k.t = 0; k.b = (int)s.h - 1; k.l = 0; k.r = (int)s.w - 1; }
  return k;
}

// Print the sprite band only (rows 9..55): the status bar and the affordance
// strip are not this tool's subject and printing them wastes half the screen.
// `mark` draws the ink box's corners so a reader can see the boundary the
// noise is required to stay inside.
// The window is the body's neighbourhood plus the floor line and both shadow
// rows, with absolute row numbers down the left so "where on the panel" stays
// checkable. Columns are the whole 128 so the HUD keep-out is visible: the
// stage runs 14..113 and a '|' marks each edge.
//
// '#' lit  '.' the ink-box outline  '_' the floor line  '|' the stage edge.
static void fb_print(const CfxRect& box, bool mark)
{
  const int top = (box.y0 - 4 > VIEW_SPRITE_TOP) ? box.y0 - 4 : VIEW_SPRITE_TOP;
  for (int y = top; y <= VIEW_FLOOR_Y + 2; ++y) {
    printf("  %2d ", y);
    for (int x = 0; x < VW; ++x) {
      const bool on = g_fb[y][x] != 0u;
      const bool edge = mark &&
          (((y == box.y0 - 1 || y == box.y1 + 1) &&
             x >= box.x0 - 1 && x <= box.x1 + 1) ||
           ((x == box.x0 - 1 || x == box.x1 + 1) &&
             y >= box.y0 - 1 && y <= box.y1 + 1));
      char c = ' ';
      if (on)                              c = '#';
      else if (edge)                       c = '.';
      else if (y == VIEW_FLOOR_Y)          c = '_';
      else if (x == VIEW_STAGE_L - 1 ||
               x == VIEW_STAGE_L + 100)    c = '|';   // PETFX_STAGE_R + 1
      putchar(c);
    }
    putchar('\n');
  }
}

// Place species `sid`'s body where ui/petfx.cpp would: x on the stage, and the
// LAST ROW OF ITS INK on VIEW_FLOOR_Y - 1. Fills `box` with the absolute ink
// rectangle, which is exactly what petfx_draw_body() publishes and hands to
// cfx_rows().
static SpriteRef place(uint8_t sid, uint8_t frame, int x, CfxRect& box)
{
  const uint8_t set = (uint8_t)(PB_SPRITE_BODY_FIRST + (sid - 1u));
  const SpriteRef s = sprite_frame(set, frame);
  const Ink k = scan_ink(s);
  int y = VIEW_FLOOR_Y - k.b - 1;
  if (y < VIEW_SPRITE_TOP - k.t) y = VIEW_SPRITE_TOP - k.t;
  fb_xbm(x, y, s);
  box.x0 = (int16_t)(x + k.l);
  box.x1 = (int16_t)(x + k.r);
  box.y0 = (int16_t)(y + k.t);
  box.y1 = (int16_t)(y + k.b);
  if (box.y1 > (int16_t)(VIEW_FLOOR_Y - 1)) box.y1 = (int16_t)(VIEW_FLOOR_Y - 1);
  return s;
}

static void show_species(uint8_t sid, int want_slots)
{
  if (sid < 1u || sid > (uint8_t)PB_SPRITE_BODY_COUNT) sid = 1u;
  const uint8_t set = (uint8_t)(PB_SPRITE_BODY_FIRST + (sid - 1u));
  const uint32_t seed = 0xA5C3F17Bu ^ (0x9E3779B9u * sid);
  const int x = VIEW_STAGE_L + 40;

  printf("species %u  atlas set %u  %s   seed 0x%08X\n",
         (unsigned)sid, (unsigned)set, PB_SPRITE_NAMES[set], (unsigned)seed);
  printf("gate: %u ms slots, about 1 in %u lit\n\n",
         (unsigned)CFX_GLITCH_SLOT_MS, (unsigned)CFX_GLITCH_ONE_IN);

  CfxRect box;
  fb_clear();
  (void)place(sid, 0u, x, box);
  printf("CLEAN (frame 0). ink box x %d..%d  y %d..%d, %d x %d px\n",
         box.x0, box.x1, box.y0, box.y1,
         box.x1 - box.x0 + 1, box.y1 - box.y0 + 1);
  fb_print(box, true);

  int shown = 0;
  for (uint32_t slot = 0; shown < want_slots && slot < 20000u; ++slot) {
    const uint32_t now = slot * (uint32_t)CFX_GLITCH_SLOT_MS;
    if (!cfx_glitch_on(now, seed)) continue;
    fb_clear();
    (void)place(sid, (uint8_t)(shown & 1), x, box);
    CfxRow rows[CFX_ROWS_MAX];
    const uint8_t n = cfx_rows(box, now, seed, rows);
    printf("\nGLITCHED slot %u (t = %u ms), %u row%s:",
           (unsigned)slot, (unsigned)now, (unsigned)n, n == 1u ? "" : "s");
    for (uint8_t i = 0; i < n; ++i)
      printf("  y=%d x=%d w=%u lvl=%u ph=%u", rows[i].y, rows[i].x,
             (unsigned)rows[i].w, (unsigned)rows[i].level, (unsigned)rows[i].phase);
    putchar('\n');
    for (uint8_t i = 0; i < n; ++i) fb_xor_row(rows[i]);
    fb_print(box, true);
    ++shown;
  }
}

// One character per slot over a minute, so the RATE is something a person can
// look at instead of a number a test asserts.
static void show_strip(uint8_t sid)
{
  const uint32_t seed = 0xA5C3F17Bu ^ (0x9E3779B9u * sid);
  const uint32_t per_line = 1000u / (uint32_t)CFX_GLITCH_SLOT_MS;   // ~1 s
  printf("species %u, one char per %u ms slot, one line per ~second:\n",
         (unsigned)sid, (unsigned)CFX_GLITCH_SLOT_MS);
  uint32_t on = 0, total = 0;
  for (uint32_t s = 0; s < 60u; ++s) {
    printf("  %2us  ", (unsigned)s);
    for (uint32_t k = 0; k < per_line; ++k) {
      const uint32_t slot = s * per_line + k;
      const uint8_t v = cfx_glitch_on(slot * (uint32_t)CFX_GLITCH_SLOT_MS, seed);
      putchar(v ? '#' : '.');
      on += v;
      ++total;
    }
    putchar('\n');
  }
  printf("\n  %u lit of %u slots = 1 in %.1f\n",
         (unsigned)on, (unsigned)total, on ? (double)total / (double)on : 0.0);
}

// The behaviour rows as NUMBERS, because a still frame cannot show motion.
// The values are ui/petfx.cpp's PF_TEMPER, quoted; this tool cannot link that
// file and says so rather than pretending to read it.
static void show_temper(void)
{
  static const char* const NAME[CFX_TEMPER_ROWS] = {
    "SOLAR", "TRANQUILO", "NERVIOSO", "GOTICO", "CORRUPTED"
  };
  static const int ROW[CFX_TEMPER_ROWS][12] = {
    {  500, 1600,  500, 1500, 13, 18, 14, 10,  4,  6, 0, 6 },
    { 2200, 5500,  900, 2600,  6, 16,  3,  5, 12,  8, 0, 3 },
    {  250,  900,  250,  800, 18, 20, 10, 20,  2,  4, 1, 5 },
    { 3500,11000, 1200, 3200,  4, 10,  1,  3, 16,  6, 0, 2 },
    {  150,  700,  150,  600, 22, 14,  6, 26,  8,  4, 1, 4 },
  };
  printf("ui/petfx.cpp PF_TEMPER, quoted (this tool cannot link petfx.cpp):\n\n");
  printf("  %-10s %11s %11s %5s %5s %5s %5s %5s %5s %5s %6s %4s\n",
         "row", "stand ms", "walk ms", "speed", "wWALK", "wHOP", "wTURN",
         "wSIT", "wLOOK", "STAND", "jitter", "hop");
  for (unsigned i = 0; i < (unsigned)CFX_TEMPER_ROWS; ++i) {
    const int* R = ROW[i];
    const int stand = 64 - (R[5] + R[6] + R[7] + R[8] + R[9]);
    printf("  %-10s %5d-%-5d %5d-%-5d %5d %5d %5d %5d %5d %5d %5d %6d %4d\n",
           NAME[i], R[0], R[1], R[2], R[3], R[4], R[5], R[6], R[7], R[8], R[9],
           stand, R[10], R[11]);
  }
  printf("\n  Weights are out of 64; STAND is whatever is left over. The\n"
         "  CORRUPTED row is selected by cfx_temper_index() and by nothing\n"
         "  else - no genome reaches it - and it is left the moment the\n"
         "  status clears:\n\n");
  for (unsigned t = 0; t < (unsigned)CFX_TEMPER_ROWS; ++t) {
    printf("    temper %u -> clean row %u, corrupted row %u\n", t,
           (unsigned)cfx_temper_index((uint8_t)t, 0u),
           (unsigned)cfx_temper_index((uint8_t)t, 1u));
  }
}

int main(int argc, char** argv)
{
  if (argc >= 2 && strcmp(argv[1], "temper") == 0) { show_temper(); return 0; }
  if (argc >= 2 && strcmp(argv[1], "strip") == 0) {
    show_strip(argc >= 3 ? (uint8_t)strtoul(argv[2], nullptr, 0) : 1u);
    return 0;
  }
  const uint8_t sid = (argc >= 2) ? (uint8_t)strtoul(argv[1], nullptr, 0) : 1u;
  const int slots   = (argc >= 3) ? atoi(argv[2]) : 8;
  show_species(sid, slots > 0 ? slots : 1);
  return 0;
}
