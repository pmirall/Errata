// =============================================================================
//  tests/test_capture_text.cpp
//  THE ANCHOR FOR fakes/gfx_real.cpp's fit_copy(), and for the rest of the
//  real-font backend's text path.
//
//  WHY IT HAS TO EXIST. tests/fakes/SHADOWS.txt refuses a fake that claims to
//  imitate a shipping body and names nothing that says so. gfx_real.cpp's
//  fit_copy() is a deliberate imitation of ui/render.cpp's: it exists so the
//  printed manual's screen captures truncate where the panel truncates. The
//  shipping body is static inside a translation unit no host binary compiles,
//  so this cannot be an equivalence test against it - it pins the BEHAVIOUR the
//  device's version is specified to have, which is what an IMITATION anchor is
//  for.
//
//  THE PROPERTY THAT MATTERS MOST is the third one. Truncating on a BYTE
//  instead of a codepoint is the defect that produced P10-C4: a lone Latin-1
//  lead byte reaching drawUTF8() does not merely draw wrong, u8g2 opens a
//  multi-byte state and swallows the NEXT character too. A capture that
//  truncated on a byte would draw a screen the panel never shows.
// =============================================================================
#include <string.h>

#include "fakes/gfx_fb.h"
#include "nt_test.h"
#include "ui/gfx.h"

// The rightmost lit column, or -1 when nothing was drawn.
static int right_edge(void) {
  for (int x = FB_W - 1; x >= 0; x--)
    for (int y = 0; y < FB_H; y++)
      if (fb_get(x, y)) return x;
  return -1;
}

static uint32_t lit(void) {
  uint32_t n = 0;
  for (int y = 0; y < FB_H; y++)
    for (int x = 0; x < FB_W; x++) n += (uint32_t)fb_get(x, y);
  return n;
}

TEST(the_capture_truncates_on_a_codepoint_and_never_past_the_box) {
  // 1. A string that fits is drawn whole and stays inside the box.
  fb_reset();
  gfx_text_fit(GF_BODY, 0, 20, 100, "hola");
  const uint32_t whole = lit();
  CHECK(whole > 0u);
  CHECK(right_edge() < 100);
  CHECK_EQ(fb_oob(), 0u);

  // 2. The same string in a box too small for it draws LESS, and still nothing
  //    past the box. This is fit_copy() dropping trailing codepoints.
  fb_reset();
  gfx_text_fit(GF_BODY, 0, 20, 12, "hola");
  CHECK(lit() < whole);
  CHECK(right_edge() < 12);
  CHECK_EQ(fb_oob(), 0u);

  // 3. THE ONE THAT MATTERS. Truncating an accented string must cut on a
  //    codepoint: a lone lead byte reaching the font is P10-C4's defect, and
  //    the malformed-text recorder is what would see it.
  for (int16_t w = 1; w <= 40; w++) {
    fb_reset();
    gfx_text_fit(GF_BODY, 0, 20, w, "Ni\xC3\xB1o Peque\xC3\xB1o");
    CHECK_EQ(fb_bad_utf8(), 0u);
    CHECK_EQ(fb_oob(), 0u);
    CHECK(right_edge() < w);
  }

  // 4. No room at all is nothing drawn, not a partial glyph.
  fb_reset();
  gfx_text_fit(GF_BODY, 0, 20, 0, "hola");
  CHECK_EQ(lit(), 0u);
  fb_reset();
  gfx_text_fit(GF_BODY, 0, 20, -5, "hola");
  CHECK_EQ(lit(), 0u);
}

TEST(the_capture_affordance_truncates_rather_than_running_off_the_panel) {
  // gfx_affordance() is the other fit_copy() caller, and the one with the
  // tightest box: two labels and a divider inside 128 px.
  fb_reset();
  gfx_affordance("MENU", "MIMO");
  CHECK_EQ(fb_oob(), 0u);
  CHECK(lit() > 0u);

  fb_reset();
  gfx_affordance("UNA ETIQUETA ABSURDAMENTE LARGA QUE NO CABE",
                 "OTRA IGUAL DE LARGA");
  CHECK_EQ(fb_oob(), 0u);        // truncated, not drawn off the edge
  CHECK_EQ(fb_bad_utf8(), 0u);
}

TEST(the_capture_reports_a_glyph_its_font_cannot_draw) {
  // The fixed-cell fake charges a full advance for every codepoint, so an
  // accented string in the ASCII-only face looked right in every golden and
  // wrong on every board. A real font can be asked, so this backend asks.
  fb_reset();
  gfx_text(GF_BODY, 0, 20, "Ni\xC3\xB1o");
  CHECK_EQ(fb_no_glyph(), 0u);           // 5x8_tf carries Latin-1

  fb_reset();
  gfx_text(GF_TINY, 0, 20, "Ni\xC3\xB1o");
  CHECK(fb_no_glyph() > 0u);             // 4x6_tr is ASCII only
  CHECK(fb_no_glyph_first()[0] != '\0');
}

TEST(the_capture_draws_text_solid_because_the_panel_does) {
  // render.cpp:366 leaves the panel in setFontMode(0): a glyph paints its own
  // background. fakes/gfx_fb.cpp draws transparent, which agrees on blank
  // ground and disagrees over ink - and a manual illustration is very often
  // over ink. Drawing onto a filled slab must therefore CLEAR pixels.
  fb_reset();
  gfx_fill(0, 10, 60, 12);
  const uint32_t slab = lit();
  gfx_text(GF_BODY, 2, 20, "ABC");
  CHECK(lit() < slab);                   // the glyph boxes punched holes
}

