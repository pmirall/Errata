// =============================================================================
//  PEBBLEBOL - ui/petfx_core.cpp
//  See petfx_core.h for why this is its own translation unit.
//  PURE: stdint + xbm_mirror.h. No Arduino, no u8g2, no render.h.
// =============================================================================
#include "petfx_core.h"

#include <string.h>

uint8_t pf_scan_ink(const uint8_t* bits, uint8_t w, uint8_t h,
                    uint8_t* top, uint8_t* bot, uint8_t* left, uint8_t* right) {
  const uint8_t stride = pf_stride(w);
  uint8_t t = 0xFF, b = 0, l = 0xFF, r = 0;
  for (uint8_t y = 0; y < h; y++) {
    const uint8_t* row = bits + (uint16_t)y * stride;
    for (uint8_t x = 0; x < w; x++) {
      if (!pf_get(row, x)) continue;
      if (t == 0xFF) t = y;
      b = y;
      if (x < l) l = x;
      if (x > r) r = x;
    }
  }
  if (t == 0xFF) return 0;
  *top = t; *bot = b; *left = l; *right = r;
  return 1;
}

uint8_t pf_build_lids(const uint8_t* bits, uint8_t w, uint8_t h,
                      uint8_t y0, uint8_t y1, uint8_t x0, uint8_t x1,
                      uint8_t* fill, uint8_t* lid) {
  const uint8_t stride = pf_stride(w);
  if (y1 >= h || y1 < y0) return 0;
  uint8_t bh = (uint8_t)(y1 - y0 + 1u);
  if (bh > PF_EYE_MAX_H) bh = PF_EYE_MAX_H;
  if (x1 >= w) x1 = (uint8_t)(w - 1u);

  memset(fill, 0, (uint16_t)stride * bh);
  memset(lid,  0, (uint16_t)stride * bh);

  uint8_t any = 0;
  for (uint8_t r = 0; r < bh; r++) {
    const uint8_t* row = bits + (uint16_t)(y0 + r) * stride;
    uint8_t*       out = fill + (uint16_t)r * stride;

    // last lit pixel of the row bounds the search: a run of zeros is only
    // "interior" when there is ink on BOTH sides of it.
    uint8_t last = 0, seen = 0;
    for (uint8_t x = 0; x < w; x++) if (pf_get(row, x)) { last = x; seen = 1; }
    if (!seen) continue;

    uint8_t x = 0;
    while (x < last && !pf_get(row, x)) x++;   // skip the left margin
    while (x < last) {
      if (pf_get(row, x)) { x++; continue; }
      const uint8_t s = x;
      while (x <= last && !pf_get(row, x)) x++;
      // [s, x) is bounded by ink on both sides by construction
      if (s >= x0 && (uint8_t)(x - 1u) <= x1) {
        for (uint8_t k = s; k < x; k++) { pf_set(out, k); any = 1; }
      }
    }
  }
  if (!any) return 0;

  // The lash line: copy the top row's mask onto the middle row. A one-row band
  // gets no lash (it would undo the whole fill).
  if (bh >= 2) memcpy(lid + (uint16_t)(bh / 2u) * stride, fill, stride);
  return bh;
}
