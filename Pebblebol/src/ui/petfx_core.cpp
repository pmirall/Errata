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

// =============================================================================
//  pf_build_sleep - see petfx_core.h for what it is for and why.
// =============================================================================

// One row's horizontal dilation: bit x survives, and its two neighbours light.
// Done pixel-wise rather than by shifting the packed row, because a 24 px body
// is three bytes and a shift across a byte boundary is exactly the off-by-four
// ui/xbm_mirror.h exists to have gone wrong once.
static void pf_dilate_row(const uint8_t* in, uint8_t* out, uint8_t w) {
  for (uint8_t x = 0; x < w; ++x) {
    if (!pf_get(in, x)) continue;
    pf_set(out, x);
    if (x > 0u)             pf_set(out, (uint8_t)(x - 1u));
    if (x + 1u < (uint16_t)w) pf_set(out, (uint8_t)(x + 1u));
  }
}

uint16_t pf_build_sleep(const uint8_t* bits, uint8_t w, uint8_t h,
                        uint8_t y0, uint8_t y1, uint8_t x0, uint8_t x1,
                        uint8_t* out) {
  if (bits == nullptr || out == nullptr) return 0u;
  if (w == 0u || h == 0u || w > PF_MAX_W || h > PF_MAX_H) return 0u;

  const uint8_t  stride = pf_stride(w);
  const uint16_t bytes  = (uint16_t)stride * h;

  uint8_t t, b, l, r;
  if (!pf_scan_ink(bits, w, h, &t, &b, &l, &r)) return 0u;   // blank: no pose

  memcpy(out, bits, bytes);

  // --- 1. the eyes shut ------------------------------------------------------
  // The blink's own strips, applied to the frame instead of blitted over it.
  // A band the generator marked "this body does not blink" answers 0 here and
  // the body sleeps on the squash alone, which is why the squash is not
  // optional.
  {
    uint8_t fill[PF_STRIP_BYTES];
    uint8_t lid[PF_STRIP_BYTES];
    const uint8_t bh = pf_build_lids(bits, w, h, y0, y1, x0, x1, fill, lid);
    for (uint8_t rr = 0; rr < bh; ++rr) {
      const uint16_t oy = (uint16_t)(y0 + rr);
      if (oy >= (uint16_t)h) break;
      uint8_t*       orow = out  + (uint16_t)oy * stride;
      const uint8_t* frow = fill + (uint16_t)rr * stride;
      const uint8_t* crow = lid  + (uint16_t)rr * stride;
      for (uint8_t k = 0; k < stride; ++k)
        orow[k] = (uint8_t)((orow[k] | frow[k]) & (uint8_t)~crow[k]);
    }
  }

  // --- 2. the weight settles -------------------------------------------------
  // The two topmost ink rows merge into one. A SQUASH, NOT A CHOP: the union
  // keeps the outline and loses a pixel of height, where deleting the top row
  // would flatten a horn or an ear and read as damage. A body only one row tall
  // has nothing to merge and is left alone.
  if (b > t) {
    uint8_t*       dst = out + (uint16_t)(t + 1u) * stride;
    const uint8_t* src = out + (uint16_t)t * stride;
    for (uint8_t k = 0; k < stride; ++k) dst[k] = (uint8_t)(dst[k] | src[k]);
    memset(out + (uint16_t)t * stride, 0, stride);
  }

  // ...and the bottom rows spread where the creature meets the floor. This is
  // the shape-independent half of the pose: it fires on a body whose eyes are
  // two pixels wide exactly as hard as on one whose eyes are nine, which is
  // what stops a derived sleep from being invisible on the quiet faces.
  {
    const uint8_t lo = (uint8_t)((b >= (uint8_t)(PF_SLEEP_SPREAD - 1u))
                                   ? (b - (PF_SLEEP_SPREAD - 1u)) : 0u);
    const uint8_t from = (uint8_t)((lo > (uint8_t)(t + 1u)) ? lo : (uint8_t)(t + 1u));
    for (uint8_t y = from; y <= b; ++y) {
      uint8_t  wide[PF_MAX_STRIDE];
      uint8_t* row = out + (uint16_t)y * stride;
      memset(wide, 0, stride);
      pf_dilate_row(row, wide, w);
      memcpy(row, wide, stride);
    }
  }

  // --- the receipt -----------------------------------------------------------
  // How many pixels this actually changed. The caller does not need it; the
  // FLOOR does, and the floor is the only thing standing between "the pose is
  // derived" and "the pose is a claim nobody checked".
  uint16_t diff = 0;
  for (uint8_t y = 0; y < h; ++y) {
    const uint8_t* a = bits + (uint16_t)y * stride;
    const uint8_t* c = out  + (uint16_t)y * stride;
    for (uint8_t x = 0; x < w; ++x)
      if (pf_get(a, x) != pf_get(c, x)) ++diff;
  }
  return diff;
}
