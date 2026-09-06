// =============================================================================
//  tests/tools/sprite_dump.cpp - THE ONLY INSTRUMENT FOR THE PROPERTY THAT
//  MATTERS (P9-C1).
//
//  Sixty bodies are about to be drawn by five different hands. Every automated
//  check this phase can offer is a check about BYTES: that the grid is
//  rectangular, that an array matches the w/h its table advertises, that a
//  frame is not empty, that the two frames differ. NONE OF THEM CAN SEE
//  WHETHER A BODY LOOKS LIKE A CREATURE. A 24x24 of noise passes all of them.
//
//  So this binary renders the COMPILED atlas - both of them - as text a person
//  can read, and as a PBM contact sheet a person can open. It is the review
//  instrument, and until it existed there was no path at all: fb_dump() prints
//  only a whole 128x64 screen and only from a FAILING golden, tests/golden
//  holds full screens of which six would ever contain a body, and
//  creator_decode prints sprite frames as hex.
//
//  IT READS THE HEADER, NOT THE ASCII SOURCES, and that is the point. A
//  rendering of tools/sprites/*.txt would only prove the .txt files say what
//  they say. This walks data/sprites_pebbles.h's PB_SPRITE_SETS through the
//  same
//  ((w+7)>>3) / LSB-first decode the device's drawXBM performs, so what appears
//  here is what the panel will show.
//
//  Usage
//    ./bin/sprite_dump list                 every set in the atlas
//    ./bin/sprite_dump text [NAME|INDEX]    one set (or all) as '#' and '.'
//    ./bin/sprite_dump sheet OUT.pbm        contact sheet, frame 0 of every set
//    ./bin/sprite_dump sheet OUT.pbm 1      contact sheet, frame 1
//    ./bin/sprite_dump blink [NAME]         frame 0 | THE BLINK | frame 1
//    ./bin/sprite_dump sleep [NAME]         frame 0 | THE SLEEPING BODY | diff
//
//  THE BLINK MODE IS NEW AT P9-C6 AND IT IS THE REASON THIS TOOL EXISTS,
//  RESTATED. The frame a player sees while a pet blinks is in NEITHER authored
//  frame: it is composited at draw time from the art, the generated eye band and
//  ui/petfx_core.cpp's pf_build_lids(). Nothing in this repository had ever drawn
//  it, and when the P9 exit review drew it by hand it found four bodies whose
//  blink filled a third of the creature solid. This mode runs the SHIPPED
//  pf_build_lids() over the SHIPPED atlas and prints the result beside the two
//  frames, with the band marked, so the next person can look instead of trusting
//  a percentage.
//
//  A name is matched case-insensitively against the enum tag with or without
//  its prefix: PAKETO, pb_spr_paketo and PBSPR_PAKETO all work.
//
//  Built by `make -C tests spritetool`, NOT by `make check`: it writes files and
//  answers a human, which is what tests/tools/ means. It asserts nothing.
//  tests/test_sprite_pipeline.cpp is the part that asserts.
//
//  All identifiers and comments English.
// =============================================================================
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   // strcasecmp / strncasecmp

#include "data/sprites.h"
#include "data/sprites_pebbles.h"
#include "ui/petfx_core.h"

// THE LEGACY NAME LIST IS GONE (P9-C3). It transcribed the 38 tags of
// data/sprites.h's hand-written `enum SpriteSetId`, because that enum carried
// no string form; that atlas was deleted with this chunk and there is exactly
// one atlas now, which emits its own PB_SPRITE_NAMES from tools/sprites/
// atlas.txt. So nothing here is transcribed and this tool needs no edit when a
// body is added - which is what the P9-C1 comment predicted and is worth
// recording as having come true.

struct Entry {
  const char*      atlas;   // "legacy" or "generated"
  const char*      name;
  const SpriteSet* set;
  int              index;
};

static Entry g_all[PB_SPRITE_SET_COUNT];
static int   g_n = 0;

static void collect(void)
{
  for (int i = 0; i < (int)PB_SPRITE_SET_COUNT; ++i) {
    Entry e = { "atlas", PB_SPRITE_NAMES[i], &PB_SPRITE_SETS[i], i };
    g_all[g_n++] = e;
  }
}

// THE DECODE, and it is deliberately open-coded rather than routed through a
// helper: this is the one place a reader can check the bit order against
// data/sprite_types.h's contract without following a call.
//   byte n of a row carries pixels 8n..8n+7, LSB = LEFTMOST pixel.
static int pixel_at(const SpriteSet& s, int frame, int x, int y)
{
  const int stride = ((int)s.w + 7) >> 3;
  const int base   = stride * (int)s.h * frame;
  const uint8_t b  = s.bits[base + y * stride + (x >> 3)];
  return (b >> (x & 7)) & 1;
}

static int ink_count(const SpriteSet& s, int frame)
{
  int n = 0;
  for (int y = 0; y < (int)s.h; ++y)
    for (int x = 0; x < (int)s.w; ++x) n += pixel_at(s, frame, x, y);
  return n;
}

static int frame_diff(const SpriteSet& s)
{
  if (s.frames < 2) return -1;
  int n = 0;
  for (int y = 0; y < (int)s.h; ++y)
    for (int x = 0; x < (int)s.w; ++x)
      if (pixel_at(s, 0, x, y) != pixel_at(s, 1, x, y)) ++n;
  return n;
}

static void print_list(void)
{
  printf("%-10s %3s  %-18s %5s %6s %7s %8s %9s\n",
         "atlas", "id", "name", "size", "frames", "bytes", "ink f0", "f0 vs f1");
  unsigned total = 0;
  for (int i = 0; i < g_n; ++i) {
    const SpriteSet& s = *g_all[i].set;
    const unsigned bytes = spr_set_bytes(s);
    total += bytes;
    char size[16];
    snprintf(size, sizeof size, "%ux%u", (unsigned)s.w, (unsigned)s.h);
    const int d = frame_diff(s);
    printf("%-10s %3d  %-18s %5s %6u %7u %8d %9s\n",
           g_all[i].atlas, g_all[i].index, g_all[i].name, size,
           (unsigned)s.frames, bytes, ink_count(s, 0),
           d < 0 ? "-" : (d == 0 ? "IDENTICAL" : "differs"));
  }
  printf("\n%d sets, %u B of art\n", g_n, total);
}

static void print_text(const Entry& e)
{
  const SpriteSet& s = *e.set;
  printf("== %s[%d] %s  %ux%u x%u  (%u B)  ink f0 %d",
         e.atlas, e.index, e.name, (unsigned)s.w, (unsigned)s.h,
         (unsigned)s.frames, spr_set_bytes(s), ink_count(s, 0));
  const int d = frame_diff(s);
  if (d == 0) printf("   FRAME 1 IS IDENTICAL TO FRAME 0");
  else if (d > 0) printf("   frames differ in %d px", d);
  printf("\n");

  // Frames side by side: an idle animation is a PAIR, and a pair read one above
  // the other is a pair nobody compares.
  for (int f = 0; f < (int)s.frames; ++f) {
    printf("   frame %-*d", (int)s.w - 6 > 1 ? (int)s.w - 6 : 1, f);
    printf("  ");
  }
  printf("\n");
  for (int y = 0; y < (int)s.h; ++y) {
    for (int f = 0; f < (int)s.frames; ++f) {
      printf("   ");
      for (int x = 0; x < (int)s.w; ++x)
        putchar(pixel_at(s, f, x, y) ? '#' : '.');
    }
    printf("\n");
  }
  printf("\n");
}

// -----------------------------------------------------------------------------
//  THE COMPOSITED BLINK FRAME. Body, then the fill strip at colour 1, then the
//  lash strip at colour 0 - the same three steps, in the same order, that
//  ui/petfx.cpp's draw path performs with drawXBM. The '+' marks a pixel the
//  blink ADDS and '-' one it takes away, so what the blink does is legible
//  without diffing two pictures by eye.
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
//  THE DERIVED SLEEPING BODY (P10-C3), for the same reason the blink mode
//  exists: it is composited at draw time and lives in NEITHER authored frame.
//  All sixty species shared one authored sleeping blob until this chunk; the
//  derivation keeps the silhouette and settles it. '+' is a pixel sleep ADDS,
//  '-' one it takes away, and the count at the end of each line is what
//  pf_build_sleep() returns - the number the floor in
//  tests/test_sprite_pipeline.cpp is measured against.
//
//  NOTHING HERE ASSERTS. It answers the one question no assertion in this
//  repository can: does this look like that creature, asleep?
// -----------------------------------------------------------------------------
static void print_sleep(const Entry& e)
{
  const SpriteSet& s = *e.set;
  printf("== %s[%d] %s  sleep\n", e.atlas, e.index, e.name);
  for (int f = 0; f < (int)s.frames; ++f) {
    const SpriteEyeBand b = sprite_eyes((uint8_t)e.index, (uint8_t)f);
    const int stride = ((int)s.w + 7) >> 3;
    const uint8_t* bits = s.bits + (long)stride * s.h * f;
    uint8_t out[PF_FRAME_BYTES];
    const unsigned diff = pf_build_sleep(bits, s.w, s.h, b.y0, b.y1, b.x0, b.x1, out);
    int ink = 0, slp = 0;
    for (int y = 0; y < (int)s.h; ++y)
      for (int x = 0; x < (int)s.w; ++x) {
        ink += pixel_at(s, f, x, y);
        slp += (out[y * stride + (x >> 3)] >> (x & 7)) & 1;
      }
    printf("   frame %d: %s, awake %d px, asleep %d px, changed %u px%s\n",
           f, (b.y1 < b.y0) ? "no eye band - the squash carries it alone"
                            : "eyes shut plus the squash",
           ink, slp, diff, diff < (unsigned)PF_SLEEP_MIN_DIFF ? "  << UNDER THE FLOOR" : "");
    printf("      frame %d                 ASLEEP\n", f);
    for (int y = 0; y < (int)s.h; ++y) {
      printf("      ");
      for (int x = 0; x < (int)s.w; ++x) putchar(pixel_at(s, f, x, y) ? '#' : '.');
      printf("   ");
      for (int x = 0; x < (int)s.w; ++x) {
        const int was = pixel_at(s, f, x, y);
        const int now = (out[y * stride + (x >> 3)] >> (x & 7)) & 1;
        if (now && !was)      putchar('+');
        else if (!now && was) putchar('-');
        else                  putchar(now ? '#' : '.');
      }
      putchar('\n');
    }
  }
}

static void print_blink(const Entry& e)
{
  const SpriteSet& s = *e.set;
  printf("== %s[%d] %s  blink\n", e.atlas, e.index, e.name);
  for (int f = 0; f < (int)s.frames; ++f) {
    const SpriteEyeBand b = sprite_eyes((uint8_t)e.index, (uint8_t)f);
    if (b.y1 < b.y0) {
      printf("   frame %d: this body does not blink (no band)\n", f);
      continue;
    }
    uint8_t fill[PF_STRIP_BYTES], lid[PF_STRIP_BYTES];
    const int stride = ((int)s.w + 7) >> 3;
    const uint8_t* bits = s.bits + (long)stride * s.h * f;
    const int rows = (int)pf_build_lids(bits, s.w, s.h, b.y0, b.y1, b.x0, b.x1,
                                        fill, lid);
    int added = 0, cut = 0, ink = 0;
    for (int y = 0; y < (int)s.h; ++y)
      for (int x = 0; x < (int)s.w; ++x) ink += pixel_at(s, f, x, y);
    printf("   frame %d: band y %u..%u x %u..%u, %d row(s), body %d px\n",
           f, (unsigned)b.y0, (unsigned)b.y1, (unsigned)b.x0, (unsigned)b.x1,
           rows, ink);
    printf("      frame %d                 BLINK                    band\n", f);
    for (int y = 0; y < (int)s.h; ++y) {
      printf("      ");
      for (int x = 0; x < (int)s.w; ++x) putchar(pixel_at(s, f, x, y) ? '#' : '.');
      printf("   ");
      for (int x = 0; x < (int)s.w; ++x) {
        const int r = y - (int)b.y0;
        int fb = 0, lb = 0;
        if (r >= 0 && r < rows) {
          fb = (fill[r * stride + (x >> 3)] >> (x & 7)) & 1;
          lb = (lid [r * stride + (x >> 3)] >> (x & 7)) & 1;
        }
        const int was = pixel_at(s, f, x, y);
        const int now = lb ? 0 : (fb ? 1 : was);
        if (now && !was)      { putchar('+'); ++added; }
        else if (!now && was) { putchar('-'); ++cut; }
        else                  putchar(now ? '#' : '.');
      }
      printf("   ");
      for (int x = 0; x < (int)s.w; ++x) {
        const int in = (y >= (int)b.y0 && y <= (int)b.y1
                        && x >= (int)b.x0 && x <= (int)b.x1);
        putchar(in ? (pixel_at(s, f, x, y) ? '#' : ' ') : '.');
      }
      printf("\n");
    }
    printf("      %d px added, %d px cut = %d%% of the body changes\n\n",
           added, cut, ink ? (100 * (added + cut)) / ink : 0);
  }
}

// ONE PBM, ASCII P1, the same format tests/golden/screens/*.pbm uses - so the
// sheet opens in any image viewer and diffs as text.
static int write_sheet(const char* path, int frame)
{
  int cell_w = 0, cell_h = 0;
  for (int i = 0; i < g_n; ++i) {
    if (g_all[i].set->w > cell_w) cell_w = g_all[i].set->w;
    if (g_all[i].set->h > cell_h) cell_h = g_all[i].set->h;
  }
  const int pad  = 2;
  const int cols = 8;
  const int rows = (g_n + cols - 1) / cols;
  const int W = cols * (cell_w + pad) + pad;
  const int H = rows * (cell_h + pad) + pad;

  unsigned char* fb = (unsigned char*)calloc((size_t)W * (size_t)H, 1);
  if (!fb) { fprintf(stderr, "sprite_dump: out of memory\n"); return 2; }

  for (int i = 0; i < g_n; ++i) {
    const SpriteSet& s = *g_all[i].set;
    const int f  = frame < (int)s.frames ? frame : 0;
    const int cx = (i % cols) * (cell_w + pad) + pad;
    const int cy = (i / cols) * (cell_h + pad) + pad;
    // Bottom-aligned inside the cell, centred horizontally: the atlas mixes
    // 24, 28, 32 and 40 px bodies and they all stand on the same floor on
    // screen (HOME_FLOOR_Y), so a sheet that centred them vertically would
    // misrepresent how they line up.
    const int ox = cx + (cell_w - (int)s.w) / 2;
    const int oy = cy + (cell_h - (int)s.h);
    for (int y = 0; y < (int)s.h; ++y)
      for (int x = 0; x < (int)s.w; ++x)
        if (pixel_at(s, f, x, y)) fb[(size_t)(oy + y) * (size_t)W + (ox + x)] = 1;
  }

  FILE* fp = fopen(path, "w");
  if (!fp) { fprintf(stderr, "sprite_dump: cannot write %s\n", path); free(fb); return 2; }
  fprintf(fp, "P1\n# pebblebol sprite contact sheet, frame %d, %d sets\n%d %d\n",
          frame, g_n, W, H);
  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) fputc(fb[(size_t)y * (size_t)W + x] ? '1' : '0', fp);
    fputc('\n', fp);
  }
  fclose(fp);
  free(fb);
  printf("sprite_dump: wrote %s (%dx%d, %d sets, frame %d)\n", path, W, H, g_n, frame);
  return 0;
}

static int name_eq(const char* a, const char* b)
{
  // Case-insensitive, and tolerant of the "spr_" / "SPR_" / "PBSPR_" prefixes a
  // reader is likely to paste in from the header.
  static const char* const kPrefix[] = { "pbspr_", "spr_", "pb_spr_" };
  for (int p = 0; p < 3; ++p) {
    const size_t n = strlen(kPrefix[p]);
    if (strncasecmp(a, kPrefix[p], n) == 0) { a += n; break; }
  }
  return strcasecmp(a, b) == 0;
}

int main(int argc, char** argv)
{
  collect();
  const char* cmd = argc > 1 ? argv[1] : "list";

  if (strcmp(cmd, "list") == 0) { print_list(); return 0; }

  if (strcmp(cmd, "text") == 0) {
    if (argc < 3) {
      for (int i = 0; i < g_n; ++i) print_text(g_all[i]);
      return 0;
    }
    int hits = 0;
    for (int i = 0; i < g_n; ++i) {
      if (name_eq(argv[2], g_all[i].name)) { print_text(g_all[i]); ++hits; }
    }
    if (!hits) {
      char* end = nullptr;
      const long idx = strtol(argv[2], &end, 10);
      if (end && *end == '\0' && idx >= 0 && idx < g_n) {
        print_text(g_all[idx]);
        return 0;
      }
      fprintf(stderr, "sprite_dump: no set named %s (try `list`)\n", argv[2]);
      return 1;
    }
    return 0;
  }

  if (strcmp(cmd, "blink") == 0) {
    int hits = 0;
    for (int i = 0; i < g_n; ++i)
      if (argc < 3 || name_eq(argv[2], g_all[i].name)) { print_blink(g_all[i]); ++hits; }
    if (!hits) {
      fprintf(stderr, "sprite_dump: no set named %s (try `list`)\n", argv[2]);
      return 2;
    }
    return 0;
  }

  if (strcmp(cmd, "sleep") == 0) {
    int hits = 0;
    for (int i = 0; i < g_n; ++i)
      if (argc < 3 || name_eq(argv[2], g_all[i].name)) { print_sleep(g_all[i]); ++hits; }
    if (!hits) {
      fprintf(stderr, "sprite_dump: no set named %s (try `list`)\n", argv[2]);
      return 2;
    }
    return 0;
  }

  if (strcmp(cmd, "sheet") == 0) {
    if (argc < 3) { fprintf(stderr, "sprite_dump: sheet needs an output path\n"); return 2; }
    const int frame = argc > 3 ? atoi(argv[3]) : 0;
    return write_sheet(argv[2], frame);
  }

  fprintf(stderr,
          "usage: sprite_dump list\n"
          "       sprite_dump blink [NAME]\n"
          "       sprite_dump sleep [NAME]\n"
          "       sprite_dump text [NAME|INDEX]\n"
          "       sprite_dump sheet OUT.pbm [FRAME]\n");
  return 2;
}
