// -----------------------------------------------------------------------------
//  sprite_types.h - THE XBM CONTRACT. Hand-written; not art, not generated.
//
//  Every bitmap this product draws - the legacy atlas in data/sprites.h, the
//  generated one in data/sprites_pebbles.h, the creator's uploaded 24x24, the
//  browser editor's grid - is the same shape, and this file is the one place
//  that says what that shape is:
//
//    ROW STRIDE  ((w + 7) >> 3) bytes. Rows run TOP to BOTTOM.
//    BIT ORDER   Within a row, byte n carries pixels 8n..8n+7 and the LSB is
//                the LEFTMOST pixel (u8g2_bitmap.c:117 mask=1, :143 mask<<=1).
//    FRAMES      Stored back to back in ONE array. Frame f begins at
//                spr_xbm_bytes(w, h) * f.
//
//  drawXBM() reads ((w+7)>>3)*h bytes from the pointer it is given and has NO
//  bound of its own - on the device (u8g2) and in the host fake alike. A table
//  row whose w/h disagrees with the array it points at is therefore a silent
//  out-of-bounds read into the NEXT sprite, not a crash and not a wrong
//  picture. The two macros at the bottom are what make that disagreement a
//  compile error, and they are the reason this header exists separately: an
//  atlas emitted by tools/gen_sprites.py must be able to assert its own rows
//  without including the hand-written atlas it will one day replace.
// -----------------------------------------------------------------------------
#ifndef PB_SPRITE_TYPES_H
#define PB_SPRITE_TYPES_H

#include <stdint.h>

struct SpriteRef {
  const uint8_t* bits;   // XBM rows, LSB-first
  uint8_t        w;
  uint8_t        h;
};

struct SpriteSet {
  const uint8_t* bits;   // frames stored back to back
  uint8_t        w;
  uint8_t        h;
  uint8_t        frames;
};

// Bytes one w*h XBM frame occupies. Same row-stride rule the runtime uses, so
// the static_asserts below measure exactly what drawXBM() will read.
constexpr unsigned spr_xbm_bytes(unsigned w, unsigned h) {
  return ((w + 7u) >> 3) * h;
}

// Bytes a whole SET occupies: every frame, back to back.
constexpr unsigned spr_set_bytes(const SpriteSet& s) {
  return spr_xbm_bytes(s.w, s.h) * s.frames;
}

// The rows a blink closes, for one frame of one set. GENERATED beside the
// pixels by tools/gen_sprites.py (PB_SPRITE_EYES in data/sprites_pebbles.h) and
// consumed by ui/petfx.cpp's pf_build_lids(), which fills every enclosed hole
// inside the window and re-opens the band's first row one row down.
//
// y1 < y0 means THIS BODY DOES NOT BLINK, and { 255, 0, 0, 0 } is how the
// generator says it. pf_build_lids() already returns 0 for that case, so it
// needs no new branch. x0/x1 clip the band horizontally: a body can have a hole
// in its eye rows that is not an eye, and filling it would fuse two body parts
// for the length of a blink.
struct SpriteEyeBand {
  uint8_t y0;
  uint8_t y1;
  uint8_t x0;
  uint8_t x1;
};

// -----------------------------------------------------------------------------
//  THE TWO SIZE GUARDS
//
//  NT_SPR_REF_FITS ties a single-frame array (an emote) to its SpriteRef row.
//  NT_SPR_SET_FITS ties a multi-frame array to its SpriteSet row, FRAMES
//  INCLUDED, and additionally refuses a row claiming zero frames - sprite_frame()
//  reduces an out-of-range frame with `frame % s.frames`, so a 0 there is a
//  division by zero at runtime rather than a compile error.
//
//  NT_SPR_SET_FITS WAS MISSING UNTIL P9-C1 AND ITS ABSENCE WAS DEMONSTRABLE.
//  Widening SPRITE_SETS[SPR_ADULT_BOLOTA] from 40,40,2 to 48,40,2 compiled
//  clean: every per-array `sizeof(spr_adult_bolota) == 400` assert still held
//  (they compare an array against a LITERAL, which says nothing about what the
//  table advertises), while frame 1 moved to byte 240 of a 400-byte array and
//  drawXBM read 80 bytes past its end into spr_adult_zampasalto. The emote
//  table had this guard and the set table did not.
// -----------------------------------------------------------------------------
#define NT_SPR_REF_FITS(arr, tbl, idx) \
  static_assert(sizeof(arr) == spr_xbm_bytes((tbl)[idx].w, (tbl)[idx].h), \
                #arr " size vs " #tbl "[" #idx "]")

#define NT_SPR_SET_FITS(arr, tbl, idx) \
  static_assert((tbl)[idx].frames >= 1u, \
                #tbl "[" #idx "] claims zero frames"); \
  static_assert(sizeof(arr) == spr_set_bytes((tbl)[idx]), \
                #arr " size vs " #tbl "[" #idx "] w/h/frames")

#endif  // PB_SPRITE_TYPES_H
