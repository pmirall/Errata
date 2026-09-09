// =============================================================================
//  ERRATA - ui/qr_paint.cpp
//  See qr_paint.h. PURE translation unit.
// =============================================================================
#include "qr_paint.h"

#include "gfx.h"

void qrp_paint(const uint8_t* modules, uint8_t size,
               int16_t x, int16_t y, int16_t box_px) {
  if (modules == nullptr || size == 0) {
    gfx_rect(x, y, box_px, box_px);
    const int16_t w = (int16_t)gfx_text_w(GF_BODY, "...");
    // VERTICALLY CENTRED, AND THE ARITHMETIC IS THE POINT. A GF_BODY run on
    // baseline b covers rows b - GFX_ASC_BODY + 1 .. b, so centring that span
    // in [y, y + box_px - 1] puts the baseline at y + (box_px + GFX_ASC_BODY)/2
    // - 1. For the 62 px box at y = 1 that is 34, which is the literal the
    // creator screen carried before this painter was extracted: the extraction
    // moves no pixel, and the golden proves it.
    gfx_text(GF_BODY, (int16_t)(x + (box_px - w) / 2),
             (int16_t)(y + (box_px + GFX_ASC_BODY) / 2 - 1), "...");
    return;
  }

  const int16_t q  = QR_QUIET_MODULES;
  int16_t px = (int16_t)(box_px / (size + 2 * q));
  if (px < 1) px = 1;
  if (px > 3) px = 3;
  const int16_t box = (int16_t)((size + 2 * q) * px);

  // Lit paper first: on an OLED a lit pixel is white, so the whole symbol area
  // including the quiet zone is drawn set and the dark modules are cleared.
  gfx_fill(x, y, box, box);
  gfx_color(GFX_ERASE);
  for (int16_t r = 0; r < (int16_t)size; ++r) {
    int16_t c = 0;
    while (c < (int16_t)size) {
      if (!QR_MODULE_AT(modules, r, c)) { ++c; continue; }
      // HORIZONTAL RUNS MERGED INTO ONE FILL. A 25x25 symbol is 625 modules and
      // a fill per module is 625 calls per frame; the runs cut that to about a
      // fifth, which is the difference between a QR screen that redraws inside
      // its frame budget and one that does not.
      int16_t run = 1;
      while (c + run < (int16_t)size && QR_MODULE_AT(modules, r, c + run)) ++run;
      gfx_fill((int16_t)(x + (q + c) * px), (int16_t)(y + (q + r) * px),
               (int16_t)(run * px), px);
      c = (int16_t)(c + run);
    }
  }
  gfx_color(GFX_DRAW);
}
