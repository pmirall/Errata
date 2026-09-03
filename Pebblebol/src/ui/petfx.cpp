// =============================================================================
//  NOTTAMAGOCHI - ui/petfx.cpp
//  The creature layer. Three things live here and they only barely touch:
//
//   1. A pixel core (mirroring, ink bounds, eyelids). Pure bit twiddling on
//      XBM rows, no U8G2, no globals from the sim. It is fenced between the
//      PIXEL CORE markers below because scratchpad/petfx/mkharness.py slices
//      exactly those lines out and compiles them on the host: that is how the
//      28 px mirror case and the blink masks are actually verified, since the
//      whole sketch cannot be built without hardware headers.
//
//   2. A behaviour automaton. Deterministic integer hash PRNG seeded from
//      PetView.identity, so the same pet always moves the same way and its
//      siblings do not. Temperament picks the timings, the other genes bend
//      them. No floats anywhere, ever.
//
//   3. A draw path. Budget is ~1-2 ms; sendBuffer() already eats 24 of the
//      50 ms frame, so everything expensive (mirroring, ink scan, eyelid
//      masks) is cached and only recomputed when (set, orientation) changes.
// =============================================================================
#include "petfx.h"

#include <string.h>

#include "render.h"
#include "../data/sprites.h"

// The eyelid table below is indexed by SpriteSetId and holds hand-verified row
// numbers read out of the art. If the art is regenerated the ids and the row
// numbers move together and the blink would land on a cheek, so make that a
// build error rather than a bug report.
static_assert(SPRITE_REV == 1, "petfx eyelid table was measured against SPRITE_REV 1 - re-verify it");

// Every horizontal limit in this file comes from PETFX_STAGE_L / PETFX_STAGE_R
// (petfx.h) and from nothing else. There is deliberately no softer, second
// keep-out any more: a keep-out that only nudged the resting ANCHOR is exactly
// what let a walking body reach the HUD columns in the first place.
//
// "Every" is meant literally, and it was not true until it was checked one write
// at a time. The four that were still guarded against the PANEL - the dilation
// copy, the contact shadow, the gene_rare spark and the gene_mutations pixel -
// all reached columns 13 or 114 on a body standing against a wall, and the
// dilation copy was destroying real pixels of the HUD while it did it.
// If you add a write to this file, route its x through pf_stage_lo() /
// pf_stage_hi() or through the PETFX_STAGE_* constants; there is no other
// correct answer, and the host sweep in the scratchpad checks it.

// Props the walk must not cross - see petfx_set_obstacles(). petfx knows about
// bodies and walls; it never goes looking in the sim for what the walls are.
#define PF_OBST_MAX     4

// Geometry of the biggest body we will ever cache: ADULT is 40x40, stride 5.
#define PF_MAX_W        40
#define PF_MAX_H        40
#define PF_MAX_STRIDE   5
#define PF_FRAME_BYTES  (PF_MAX_STRIDE * PF_MAX_H)   // 200 B
#define PF_EYE_MAX_H    12                           // ADULT_BUHO needs 11
#define PF_STRIP_BYTES  (PF_MAX_STRIDE * PF_EYE_MAX_H)

// =============================================================================
// ---8<--- PETFX PIXEL CORE BEGIN ---8<---
// Everything between these markers is host-testable: it depends on sprites.h
// and stdint only. Do not reach for U8G2, millis() or the sim in here.
// =============================================================================

// Bit-reversal LUT. XBM rows are LSB-first (LSB = leftmost pixel), so
// mirroring a row is "reverse the byte order AND reverse the bits in each
// byte". 256 B in flash beats a per-pixel loop by ~8x.
static const uint8_t PF_REV8[256] = {
  0x00, 0x80, 0x40, 0xC0, 0x20, 0xA0, 0x60, 0xE0, 0x10, 0x90, 0x50, 0xD0, 0x30, 0xB0, 0x70, 0xF0,
  0x08, 0x88, 0x48, 0xC8, 0x28, 0xA8, 0x68, 0xE8, 0x18, 0x98, 0x58, 0xD8, 0x38, 0xB8, 0x78, 0xF8,
  0x04, 0x84, 0x44, 0xC4, 0x24, 0xA4, 0x64, 0xE4, 0x14, 0x94, 0x54, 0xD4, 0x34, 0xB4, 0x74, 0xF4,
  0x0C, 0x8C, 0x4C, 0xCC, 0x2C, 0xAC, 0x6C, 0xEC, 0x1C, 0x9C, 0x5C, 0xDC, 0x3C, 0xBC, 0x7C, 0xFC,
  0x02, 0x82, 0x42, 0xC2, 0x22, 0xA2, 0x62, 0xE2, 0x12, 0x92, 0x52, 0xD2, 0x32, 0xB2, 0x72, 0xF2,
  0x0A, 0x8A, 0x4A, 0xCA, 0x2A, 0xAA, 0x6A, 0xEA, 0x1A, 0x9A, 0x5A, 0xDA, 0x3A, 0xBA, 0x7A, 0xFA,
  0x06, 0x86, 0x46, 0xC6, 0x26, 0xA6, 0x66, 0xE6, 0x16, 0x96, 0x56, 0xD6, 0x36, 0xB6, 0x76, 0xF6,
  0x0E, 0x8E, 0x4E, 0xCE, 0x2E, 0xAE, 0x6E, 0xEE, 0x1E, 0x9E, 0x5E, 0xDE, 0x3E, 0xBE, 0x7E, 0xFE,
  0x01, 0x81, 0x41, 0xC1, 0x21, 0xA1, 0x61, 0xE1, 0x11, 0x91, 0x51, 0xD1, 0x31, 0xB1, 0x71, 0xF1,
  0x09, 0x89, 0x49, 0xC9, 0x29, 0xA9, 0x69, 0xE9, 0x19, 0x99, 0x59, 0xD9, 0x39, 0xB9, 0x79, 0xF9,
  0x05, 0x85, 0x45, 0xC5, 0x25, 0xA5, 0x65, 0xE5, 0x15, 0x95, 0x55, 0xD5, 0x35, 0xB5, 0x75, 0xF5,
  0x0D, 0x8D, 0x4D, 0xCD, 0x2D, 0xAD, 0x6D, 0xED, 0x1D, 0x9D, 0x5D, 0xDD, 0x3D, 0xBD, 0x7D, 0xFD,
  0x03, 0x83, 0x43, 0xC3, 0x23, 0xA3, 0x63, 0xE3, 0x13, 0x93, 0x53, 0xD3, 0x33, 0xB3, 0x73, 0xF3,
  0x0B, 0x8B, 0x4B, 0xCB, 0x2B, 0xAB, 0x6B, 0xEB, 0x1B, 0x9B, 0x5B, 0xDB, 0x3B, 0xBB, 0x7B, 0xFB,
  0x07, 0x87, 0x47, 0xC7, 0x27, 0xA7, 0x67, 0xE7, 0x17, 0x97, 0x57, 0xD7, 0x37, 0xB7, 0x77, 0xF7,
  0x0F, 0x8F, 0x4F, 0xCF, 0x2F, 0xAF, 0x6F, 0xEF, 0x1F, 0x9F, 0x5F, 0xDF, 0x3F, 0xBF, 0x7F, 0xFF,
};

static inline uint8_t pf_stride(uint8_t w) { return (uint8_t)((w + 7u) >> 3); }

static inline uint8_t pf_get(const uint8_t* row, uint8_t x) {
  return (uint8_t)((row[x >> 3] >> (x & 7u)) & 1u);
}

static inline void pf_set(uint8_t* row, uint8_t x) {
  row[x >> 3] = (uint8_t)(row[x >> 3] | (uint8_t)(1u << (x & 7u)));
}

// -----------------------------------------------------------------------------
//  pf_mirror_frame - horizontal flip of a whole XBM frame.
//
//  THE TRAP, and it is a real one: w = 24, 32 and 40 are multiples of 8 and
//  fall out clean, but CHILD is 28 px wide. Its stride is 4 bytes = 32 bit
//  slots, so every row carries 4 slots of padding ABOVE the sprite. Reversing
//  the 32 slots puts the sprite at slots 4..31 instead of 0..27 - the body
//  comes out shifted 4 px to the right and the last 4 px of the silhouette
//  fall off the row. After reversing you MUST shift the row down by
//  pad = stride*8 - w bit slots.
//
//  In this layout "shift down by k" means new_bit[i] = old_bit[i + k], which
//  in byte terms is dst[j] = (tmp[j] >> k) | (tmp[j+1] << (8-k)) - it looks
//  like a right shift and reads like a left shift; that inversion is exactly
//  what makes this easy to get wrong. The padding bits themselves land in the
//  low nibble of tmp[0] and are shifted out, so the source padding never has
//  to be masked.
// -----------------------------------------------------------------------------
static void pf_mirror_frame(const uint8_t* src, uint8_t* dst, uint8_t w, uint8_t h) {
  const uint8_t stride = pf_stride(w);
  const uint8_t pad    = (uint8_t)((uint8_t)(stride * 8u) - w);
  uint8_t tmp[PF_MAX_STRIDE + 1];

  for (uint8_t y = 0; y < h; y++) {
    const uint8_t* s = src + (uint16_t)y * stride;
    uint8_t*       d = dst + (uint16_t)y * stride;
    for (uint8_t i = 0; i < stride; i++) tmp[i] = PF_REV8[s[stride - 1u - i]];
    tmp[stride] = 0;
    if (pad == 0) {
      for (uint8_t i = 0; i < stride; i++) d[i] = tmp[i];
    } else {
      for (uint8_t i = 0; i < stride; i++)
        d[i] = (uint8_t)((uint8_t)(tmp[i] >> pad) | (uint8_t)(tmp[i + 1u] << (8u - pad)));
    }
  }
}

// -----------------------------------------------------------------------------
//  pf_scan_ink - tightest box that contains a lit pixel.
//  The art has empty margins (spr_adult_bolota is 40x40 with ink only in rows
//  1..36) and they are not the same on every body, so the floor, the shadow
//  and the coat pattern all have to be measured, never assumed.
//  Returns 0 when the frame is blank; the callers then fall back to the box.
// -----------------------------------------------------------------------------
static uint8_t pf_scan_ink(const uint8_t* bits, uint8_t w, uint8_t h,
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

// -----------------------------------------------------------------------------
//  EYELIDS
//
//  Verified fact about this art: the eyes are HOLES in the silhouette, with
//  the pupil as a lit island inside the hole. spr_adult_bolota rows 15-20:
//      .######......##############......######.
//      .######..##..##############..##..######.
//  So "close the eye" = fill the hole (the socket goes solid, the pupil was
//  already solid) and then carve one dark row back out as the lash line.
//
//  WHY THIS IS A TABLE AND NOT A HEURISTIC.
//  The obvious rule - "fill every interior run of zeros in the top 55 % of the
//  ink" - was implemented and run against all 38 sets (scratchpad/petfx/
//  eyes.py, eyes2.py). It is wrong on this art for two independent reasons:
//    * decorative silhouette gaps are geometrically identical to eye sockets.
//      BABY_CACTUS spines, ADULT_PUNKI's mohawk, TEEN_POOR's spikes and
//      BABY_SETA's cap notches all read as "a pair of interior runs high up in
//      the body" and win the vote over the real eyes.
//    * the 55 % cut is simply false for some bodies. BABY_SETA's face is on
//      the STEM, at 67-79 % of the ink height; a 55 % or 60 % band cannot
//      reach it, and widening the band to 80 % starts swallowing mouths.
//  Refining the heuristic (pupil-split detection, symmetry scoring) got 30 of
//  38 sets right, which is worse than useless for a cosmetic effect: seven
//  bodies would blink with their spikes. So the rows were read off the decoded
//  art by hand, once, and frozen with the SPRITE_REV assert above. 192 B of
//  flash buys an exact answer.
//
//  x0/x1 clip the band horizontally. Some bodies have a hole in the eye rows
//  that is not an eye - ADULT_QUIMERA has a 2 px seam between its two heads at
//  x19, BABY_PEZ has a tail notch, BABY_ROBOT has an armpit - and filling
//  those would fuse body parts for 90 ms. The window keeps the fill on the
//  face. Coordinates are in UNMIRRORED sprite space; the mirror flips them.
// -----------------------------------------------------------------------------
struct PfEyeBand { uint8_t y0, y1, x0, x1; };

#define PF_EYE_FIRST  SPR_BABY_BLOB
#define PF_EYE_LAST   SPR_SENIOR_QUIMERA
#define PF_EYE_SETS   (PF_EYE_LAST - PF_EYE_FIRST + 1)

// [set - PF_EYE_FIRST][frame]
static const PfEyeBand PF_EYE[PF_EYE_SETS][2] = {
  /* BABY_BLOB         */ { {  8, 12,  5, 18 }, { 11, 12,  5, 18 } },
  /* BABY_ORUGA        */ { {  9, 13,  5, 18 }, { 11, 12,  5, 18 } },
  /* BABY_PAJARO       */ { {  8, 12,  5, 18 }, { 10, 11,  5, 18 } },
  /* BABY_GATO         */ { {  8, 12,  4, 19 }, { 10, 11,  3, 19 } },
  /* BABY_SETA         */ { { 16, 19,  9, 13 }, { 16, 19,  9, 13 } },  // f1 already blinks
  /* BABY_CACTUS       */ { { 12, 14,  8, 15 }, { 13, 13,  8, 15 } },
  /* BABY_PEZ          */ { {  9, 12,  4,  6 }, {  9, 12,  4,  6 } },  // one eye, faces left
  /* BABY_ROBOT        */ { {  9, 12,  6, 20 }, {  9, 12,  6, 20 } },
  /* CHILD_GOOD        */ { { 11, 15,  7, 20 }, { 13, 14,  7, 20 } },
  /* CHILD_POOR        */ { { 11, 15,  6, 20 }, { 13, 15,  6, 20 } },
  /* TEEN_GOOD         */ { {  9, 13,  9, 22 }, { 11, 12,  9, 22 } },
  /* TEEN_POOR         */ { {  9, 13,  8, 23 }, { 11, 12,  8, 22 } },
  /* ADULT_BOLOTA      */ { { 15, 20,  7, 32 }, { 16, 17,  7, 32 } },
  /* ADULT_ZAMPASALTO  */ { {  9, 13, 11, 26 }, { 11, 12, 11, 26 } },
  /* ADULT_BUHO        */ { {  9, 19,  5, 34 }, {  9, 19,  5, 34 } },  // 11 rows of owl
  /* ADULT_PUNKI       */ { { 12, 19,  7, 29 }, { 12, 19,  6, 28 } },
  /* ADULT_MOHO        */ { { 14, 17,  8, 33 }, { 13, 16,  8, 33 } },
  /* ADULT_QUIMERA     */ { { 12, 17,  6, 13 }, { 14, 17,  6, 13 } },  // left head only
  /* SENIOR_BOLOTA     */ { {  9, 11,  5, 22 }, { 11, 12,  5, 22 } },
  /* SENIOR_ZAMPASALTO */ { {  8, 10,  6, 19 }, { 10, 11,  6, 19 } },
  /* SENIOR_BUHO       */ { {  5, 11,  5, 26 }, {  5, 11,  5, 26 } },
  /* SENIOR_PUNKI      */ { {  8, 11,  6, 23 }, {  9, 10,  6, 23 } },
  /* SENIOR_MOHO       */ { {  8, 10,  7, 22 }, {  8, 10,  7, 22 } },
  /* SENIOR_QUIMERA    */ { {  8, 10,  5,  9 }, {  9, 11,  5,  9 } },  // left head only
};

// -----------------------------------------------------------------------------
//  pf_build_lids - turn one eye band into two little XBM strips.
//
//    fill : every interior hole inside the window, drawn with colour 1, so the
//           socket goes solid.
//    lid  : the holes of the band's FIRST row, placed on the band's MIDDLE row
//           and drawn with colour 0. The first row is the un-split socket
//           outline (lower rows are cut in two by the pupil), so re-opening it
//           one row down gives a continuous slit instead of two notches.
//
//  Both strips are full sprite width so they can be blitted at (x, y + y0)
//  with a plain drawXBM. Returns the band height, 0 when there is nothing to
//  do (blank band - BABY_SETA frame 1 is already drawn blinking).
// -----------------------------------------------------------------------------
static uint8_t pf_build_lids(const uint8_t* bits, uint8_t w, uint8_t h,
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
// ---8<--- PETFX PIXEL CORE END ---8<---
// =============================================================================

// =============================================================================
//  PRNG
//  Deterministic 32-bit integer hash (Murmur-style finaliser). Not rand(), not
//  esp_random(): the whole point is that pet #7 always walks like pet #7, on
//  every boot, on every device that loads the same save.
// =============================================================================
static inline uint32_t pf_mix(uint32_t x) {
  x ^= x >> 16; x *= 0x7FEB352Du;
  x ^= x >> 15; x *= 0x846CA68Bu;
  x ^= x >> 16;
  return x;
}

static uint32_t s_rng  = 1u;
static uint32_t s_seed = 1u;

static inline uint32_t pf_rand(void) { s_rng = pf_mix(s_rng + 0x9E3779B9u); return s_rng; }

// Inclusive range. lo <= hi is the caller's problem; equal is fine.
static inline uint16_t pf_range(uint16_t lo, uint16_t hi) {
  if (hi <= lo) return lo;
  return (uint16_t)(lo + (uint16_t)(pf_rand() % (uint32_t)(hi - lo + 1u)));
}

static inline uint8_t pf_chance(uint8_t n, uint8_t of) {
  return (uint8_t)((pf_rand() % (uint32_t)of) < n);
}

// =============================================================================
//  TEMPERAMENT
//  gene_temper_class() 0..3 is the character. Everything else bends these.
//  Weights are out of 64; whatever is left over becomes STAND, which is why
//  GOTICO (36 spoken for) stands around twice as much as NERVIOSO (56).
// =============================================================================
enum PfState : uint8_t {
  PF_STAND = 0, PF_WALK, PF_TURN, PF_SIT, PF_LOOK_UP, PF_HOP, PF_ATTENTION
};

struct PfTemper {
  uint16_t stand_lo, stand_hi;   // ms
  uint16_t walk_lo,  walk_hi;    // ms
  uint8_t  speed;                // px/s at neutral body size and full mood
  uint8_t  w_walk, w_hop, w_turn, w_sit, w_look;   // out of 64
  uint8_t  jitter;               // 1 px twitch while standing
  uint8_t  hop_amp;              // px at the apex
};

static const PfTemper PF_TEMPER[TEMPER_COUNT] = {
  /* SOLAR     hops often and short, changes its mind cheerfully */
  {  500, 1600,  500, 1500, 13, 18, 14, 10,  4,  6, 0, 6 },
  /* TRANQUILO long stops, slow walk, little amplitude            */
  { 2200, 5500,  900, 2600,  6, 16,  3,  5, 12,  8, 0, 3 },
  /* NERVIOSO  short bursts, constant turns, 1 px twitch at rest  */
  {  250,  900,  250,  800, 18, 20, 10, 20,  2,  4, 1, 5 },
  /* GOTICO    nearly immobile, drifts to a corner and stays      */
  { 3500,11000, 1200, 3200,  4, 10,  1,  3, 16,  6, 0, 2 },
};

// gene_pattern -> coat dither, as an ERASE level out of 16.
//
// THE CEILING IS 2, AND THAT IS A DELIBERATE DOWNGRADE. The old table went up
// to 5 on the theory that "5/16 is the most that still leaves a solid animal",
// which measured density and ignored SHAPE. The Bayer matrix is
//     0  8  2 10
//    12  4 14  6
//     3 11  1  9
//    15  7 13  5
// and a threshold cuts it ANISOTROPICALLY: at level 5 its four rows erase 2/4,
// 1/4, 2/4 and 0/4 of their pixels, so one row in four is combed to a lattice
// and the next is untouched. On a 40 px adult that is not fur, it is a venetian
// blind: ADULT_BUHO at level 5 turns rows 17-21 and 35-44 into a grille, erodes
// its ear tips to one pixel and splits both pupils down the middle. Level 3
// already combs one row in four (2/4, 0/4, 1/4, 0/4). Only 1 and 2 keep every
// row of the silhouette recognisable - 2 lights one pixel per 4x4 cell on rows
// 0 and 2, which reads as speckle, and 1 lights one per 4x4 cell on row 0 only.
//
// The honest cost is variety, and it is paid where it hurts least. render.h
// documents how many distinct looks the phase argument buys: 16 at odd levels,
// 8 at 2/6/10/14. So the 16 pattern genes no longer map to 16 radically
// different coats; they map to 3 densities x up to 16 phase offsets, which is
// still 16 distinct coats - just quieter ones. Sixteen legible animals beat
// sixteen grades of damage.
// Patterns 0..2 are "plain", which has to exist or every pet looks textured.
// The phase is the gene itself, so no two pattern values share a look.
// One more constraint the level cut brought back, and it is the reason gene 13
// sits on level 1 and not on 2: the phase is only worth 16 distinct looks when
// the thresholded matrix has no translation that maps it to itself. Level 1
// keeps one cell of the 4x4 and has none, so all 16 phases differ. Level 2 keeps
// (0,0) and (2,2), which a (+2,+2) shift swaps - so its 16 phases collapse to 8,
// and phases 7 and 13 are one of those pairs. Left on level 2, pattern genes 7
// and 13 rendered the SAME coat. Moving 13 down to level 1 restores 14 distinct
// coats out of 16, which is exactly what the old 1/3/5 table delivered (genes
// 0..2 are all "plain" on purpose and always shared one look).
static const uint8_t PF_PAT_LEVEL[PATTERN_COUNT] = {
  0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 1, 1, 2
};

// A 12-step integer orbit for the gene_rare sparkle. cos/sin * 24 / 16,
// rounded by hand. No trig, no floats, no table lookups per pixel.
static const int8_t PF_ORB_X[12] = {   0,  12,  21,  24,  21,  12,   0, -12, -21, -24, -21, -12 };
static const int8_t PF_ORB_Y[12] = { -18, -16,  -9,   0,   9,  16,  18,  16,   9,   0,  -9, -16 };

// =============================================================================
//  MODULE STATE
// =============================================================================
static uint8_t  s_began       = 0;
static uint8_t  s_seeded      = 0;     // petfx_reset() has run at least once
static uint32_t s_identity    = 0;     // pebble_identity(), for auto-reseed
// The two live numbers the automaton bends to. Read off the PetView on every
// petfx_service()/petfx_reset(), never from the simulation: this module has not
// been allowed to see game/sim.h since P2-C11c.
static uint8_t  s_mood_pct    = 50;
static uint8_t  s_energy_pct  = 100;
static uint32_t s_now         = 0;     // last now_ms handed to petfx_service()
static uint32_t s_last_ms     = 0;

// --- character, derived from the genome once per reset ---
static uint8_t  s_temper      = TEMPER_TRANQUILO;
static uint16_t s_speed_q4    = 96;    // 1/16 px per second
static uint8_t  s_hop_amp     = 4;
static int16_t  s_anchor      = 64;    // preferred body-centre x
static uint8_t  s_roam        = 24;    // half-width of the roaming window
static uint8_t  s_pat_level   = 0;
static uint8_t  s_pat_phase   = 0;
static uint8_t  s_dilate      = 0;
static uint8_t  s_rare        = 0;
static uint8_t  s_asym        = 0;
static uint8_t  s_luck        = 4;

// --- automaton ---
static uint8_t  s_state       = PF_STAND;
static uint32_t s_state_ms    = 0;
static uint16_t s_state_len   = 800;
static int32_t  s_x_q4        = 52 * 16;   // body-left x, 1/16 px
static int32_t  s_walk_acc    = 0;         // q4 * ms remainder
static int8_t   s_dir         = 1;
static int8_t   s_facing      = -1;        // -1 = as authored, +1 = mirrored
static int16_t  s_hop_y       = 0;         // negative = airborne
static uint8_t  s_squash      = 0;         // frames of landing squash left
// The CHOREOGRAPHY's compression (petfx_squash), kept apart from the landing
// squash above because the s_hold branch of petfx_service() zeroes that one on
// every suspended tick and an action choreography is exactly when s_hold is on.
static uint32_t s_act_sq_until = 0;
static uint8_t  s_act_sq_armed = 0;
static uint8_t  s_freeze      = 0;
static uint8_t  s_hold        = 0;     // actfx: suspended in place, not centred
static uint32_t s_last_input  = 0;
static uint32_t s_startle_end = 0;
static int8_t   s_jitter      = 0;
static uint8_t  s_body_w_hint = 32;

// --- registered props (poops), left edges + one shared width ---
static int16_t  s_obst_x[PF_OBST_MAX];
static uint8_t  s_obst_n      = 0;
static uint8_t  s_obst_w      = 0;

// --- blink ---
static uint32_t s_blink_at    = 0;     // when the next blink starts
static uint32_t s_blink_end   = 0;

// --- last drawn geometry, for petfx_body_*() ---
static int16_t  s_draw_x = 44, s_draw_y = 12;
static uint8_t  s_draw_w = 40, s_draw_h = 40;
// The INK box of that same frame, absolute and inclusive, published for actfx
// so a prop can be parked flush against the animal instead of against the empty
// margin of its sprite box. Written by petfx_draw_body() where the box is
// already computed, so the two cannot drift apart.
static int16_t  s_ink_x0 = 44, s_ink_y0 = 12, s_ink_x1 = 83, s_ink_y1 = 51;

// The identity petfx_draw_body() last drew, kept ONLY so petfx_pose_ink_x() can
// answer "how wide would this animal be in pose X" without the caller having to
// carry a PetView into a query. Written where set_id is derived, three lines
// below, so the two cannot drift apart.
static uint8_t  s_qry_ok      = 0;
static uint8_t  s_qry_species = 0;
static uint8_t  s_qry_stage   = 0;
static uint8_t  s_qry_form    = 0;

// --- sprite cache: mirrored body + ink bounds + eyelid strips, per frame ---
static uint8_t  s_cache_set   = 0xFF;
static uint8_t  s_cache_mir   = 0xFF;
static uint8_t  s_cache_w     = 0;
static uint8_t  s_cache_h     = 0;
static uint8_t  s_cache_bits[2][PF_FRAME_BYTES];
static uint8_t  s_ink_t[2], s_ink_b[2], s_ink_l[2], s_ink_r[2];
static uint8_t  s_lid_fill[2][PF_STRIP_BYTES];
static uint8_t  s_lid_cut[2][PF_STRIP_BYTES];
static uint8_t  s_lid_y[2], s_lid_h[2];

// =============================================================================
//  SMALL HELPERS
// =============================================================================
static inline uint32_t pf_since(uint32_t t) { return (uint32_t)(s_now - t); }

static inline int16_t pf_clamp16(int16_t v, int16_t lo, int16_t hi) {
  return (v < lo) ? lo : ((v > hi) ? hi : v);
}

// THE stage bounds, in whole pixels, for a body w px wide. Every x clamp in
// this file goes through these two - grep for them before adding a third.
static inline int16_t pf_stage_lo(void) { return (int16_t)PETFX_STAGE_L; }
static inline int16_t pf_stage_hi(uint8_t w) {
  const int16_t hi = (int16_t)((int16_t)PETFX_STAGE_R + 1 - (int16_t)w);
  return (hi < (int16_t)PETFX_STAGE_L) ? (int16_t)PETFX_STAGE_L : hi;
}

// The identity is DERIVED BY pet_view.cpp now (pebble_identity: the Pebble's
// id ^ creation_seed, with the old genome hash as the fallback for a pet that
// has never been filed into the Box). This module only reads it.

// The width the automaton clamps against MUST be the width that actually gets
// drawn, and the old "poses never change a set's width" claim was simply false.
// sprite_set_id() sends SENIOR + SLEEP / SICK / EAT to its `default:` branches,
// which return SPR_SLEEP_ADULT / SPR_SICK_ADULT / SPR_EAT_ADULT - 40x40 - while
// a senior's idle body is 32x32. The automaton clamped with 32 (x_hi = 82) and
// petfx_draw_body() clamped with 40 (x_hi = 74), so a senior between x = 75 and
// 82 that fell asleep, fell ill or ate jumped 8 px left and jumped back when the
// pose ended; a SICK senior is not immobilised, so it kept walking right while
// the draw clamp pinned it at 74 and the last 8 px of every stroll rendered as a
// frozen body against an invisible wall. pf_obstacle_block() believed the 32 as
// well, which parked a 40 px body 8 px INSIDE a poop - the exact silhouette
// fusion the obstacle code exists to prevent.
//
// So take the MAXIMUM over the poses this stage can actually reach. Using the
// live pose instead would fix the overlap but move the wall in the middle of a
// stroll, which is the same jump wearing a different hat; the maximum is stable
// for as long as the stage is, which is what a clamp has to be.
static uint8_t pf_width_of(const PetView& p) {
  static const uint8_t kPoses[] = { POSE_IDLE, POSE_SLEEP, POSE_SICK, POSE_EAT };
  const uint8_t species = p.gene_species;
  const uint8_t form    = p.form;
  uint8_t w = 0;
  for (uint8_t i = 0; i < (uint8_t)(sizeof(kPoses) / sizeof(kPoses[0])); i++) {
    const uint8_t id = sprite_set_id(species, p.stage, form, kPoses[i]);
    const uint8_t sw = sprite_set(id).w;
    if (sw > w) w = sw;
  }
  return w ? w : (uint8_t)24;
}

// The horizontal span of the body's INK that a prop can actually run into.
//
// NOT the ink BOX. Measuring the whole box was the first version of this fix and
// it is a no-op on the very body that motivated it: ADULT_BOLOTA's ink reaches
// both edges of its 40 px box - at HEAD height, rows the poop cannot reach. A
// poop is 12 px tall standing on the affordance line (screen rows 44..55) and
// the body's ink ends on PETFX_FLOOR_Y - 1 (row 51), so the only rows where the
// two silhouettes can meet are the body's last eight. Over those, the bolota is
// 3 px narrower on each side, and its LAST row is 12 px narrower - which is the
// "it turns round before it gets there" the review measured.
//
// The window is derived, not guessed: rows = PETFX_FLOOR_Y - RD_AFFORD_Y +
// prop_w, which is 8 for the 12 px icons ui.cpp registers. It assumes those
// props are as tall as they are wide and stand on the strip, which is what
// draw_poop() does; a prop that broke that assumption would only move the stop
// point a pixel or two, never through the prop.
//
// Widest of the TWO animation frames, deliberately: the wall must not move under
// the pet every UI_ANIM_FRAME_MS, and erring wide can only stop it earlier.
// Before the sprite cache has been built once there is nothing measured, so fall
// back to the whole box - which is what the code did unconditionally until now.
static void pf_ink_span(int16_t* l, int16_t* r) {
  *l = 0;
  *r = (int16_t)((s_body_w_hint ? s_body_w_hint : (uint8_t)1u) - 1u);
  if (s_cache_set == 0xFFu || s_cache_w == 0u) return;

  // THE SPAN HAS TO MATCH THE CLEAR BOX, NOT THE SILHOUETTE AT PROP HEIGHT.
  // Measuring only the rows a prop can stand beside looks smarter - it parks the
  // animal flush against the poop instead of bouncing off air - but
  // petfx_draw_body() clears the FULL ink bounding box before its transparent
  // blit, and that box is up to 8 px wider than the silhouette down there. Park
  // by the narrow span and the invisible clear rectangle reaches past the body
  // into the prop: measured on the real art, a TEEN_GOOD parked at the x=99
  // poop erased 31 of its 71 ink pixels, leaving a clean vertical cut in mid-air
  // 8 px from the animal. That is the opaque-halo artefact all over again, only
  // mirrored - the actor erasing the prop instead of the prop erasing the actor.
  // A few pixels of air in front of a poop is much the cheaper defect.
  //
  // Widest of the two frames, so the wall does not move every UI_ANIM_FRAME_MS.
  uint8_t lo = s_ink_l[0], hi = s_ink_r[0];
  if (s_ink_l[1] < lo) lo = s_ink_l[1];
  if (s_ink_r[1] > hi) hi = s_ink_r[1];
  *l = (int16_t)lo;
  *r = (int16_t)hi;
}

// 30..100. In MISERIA the pet barely moves; this scales speed and hop apex,
// never the timings, so a miserable pet is slow rather than catatonic.
static uint8_t pf_amp(void) {
  const uint16_t m = s_mood_pct;
  return (uint8_t)(30u + (m * 70u) / 100u);
}

// 0..4 - one step per idle minute. Longer states, rarer specials.
static uint8_t pf_boredom(void) {
  const uint32_t idle = pf_since(s_last_input);
  const uint32_t b    = idle / 60000u;
  return (uint8_t)((b > 4u) ? 4u : b);
}

static uint16_t pf_stretch(uint16_t ms) {
  const uint32_t v = ((uint32_t)ms * (4u + pf_boredom())) / 4u;
  return (uint16_t)((v > 60000u) ? 60000u : v);
}

// =============================================================================
//  SPRITE CACHE
//  Rebuilt only when the set or the orientation changes - a few times a
//  minute, ~600 byte operations. Never in the steady-state frame path.
// =============================================================================
static void pf_cache_sync(uint8_t set_id, uint8_t mirrored) {
  if (set_id == s_cache_set && mirrored == s_cache_mir) return;

  const SpriteSet s = sprite_set(set_id);
  const uint8_t   stride = pf_stride(s.w);
  const uint16_t  fbytes = (uint16_t)stride * s.h;

  s_cache_set = set_id;
  s_cache_mir = mirrored;
  s_cache_w   = s.w;
  s_cache_h   = s.h;

  for (uint8_t f = 0; f < 2; f++) {
    const uint8_t  src_f = (uint8_t)((f < s.frames) ? f : 0u);
    const uint8_t* src   = s.bits + (uint32_t)fbytes * src_f;
    uint8_t*       dst   = s_cache_bits[f];

    if (mirrored) pf_mirror_frame(src, dst, s.w, s.h);
    else          memcpy(dst, src, fbytes);

    if (!pf_scan_ink(dst, s.w, s.h, &s_ink_t[f], &s_ink_b[f], &s_ink_l[f], &s_ink_r[f])) {
      s_ink_t[f] = 0; s_ink_b[f] = (uint8_t)(s.h - 1u);
      s_ink_l[f] = 0; s_ink_r[f] = (uint8_t)(s.w - 1u);
    }

    s_lid_h[f] = 0;
    s_lid_y[f] = 0;
    if (set_id >= PF_EYE_FIRST && set_id <= PF_EYE_LAST) {
      const PfEyeBand& b = PF_EYE[set_id - PF_EYE_FIRST][src_f];
      uint8_t x0 = b.x0, x1 = b.x1;
      if (mirrored) {                       // the window mirrors with the art
        const uint8_t nx0 = (uint8_t)(s.w - 1u - b.x1);
        const uint8_t nx1 = (uint8_t)(s.w - 1u - b.x0);
        x0 = nx0; x1 = nx1;
      }
      s_lid_y[f] = b.y0;
      s_lid_h[f] = pf_build_lids(dst, s.w, s.h, b.y0, b.y1, x0, x1,
                                 s_lid_fill[f], s_lid_cut[f]);
    }
  }
}

// =============================================================================
//  BEHAVIOUR AUTOMATON
// =============================================================================

// Where the pet would like to end up. Sociable pets come to the middle where
// the player is looking; unsociable ones pick a side and hug it. The side is
// chosen from the lineage, not from a coin toss, so it is part of the pet.
static void pf_derive(const PetView& p) {
  s_temper = p.temper;
  if (s_temper >= TEMPER_COUNT) s_temper = TEMPER_TRANQUILO;
  const PfTemper& T = PF_TEMPER[s_temper];

  const uint8_t bs  = p.body_size;            // 0..7
  const uint8_t soc = p.sociability;          // 0..15

  // Big bodies are slower and hop lower. 20-bs over 16 keeps it integer and
  // lands between 0.81x and 1.25x.
  s_speed_q4 = (uint16_t)(((uint32_t)T.speed * 16u * (20u - bs)) / 16u);
  s_hop_amp  = (uint8_t)(T.hop_amp + (7u - bs) / 3u);

  if (soc >= 8u) {
    s_anchor = 64;
    s_roam   = (uint8_t)(18u + (soc - 8u) * 4u);
  } else {
    s_anchor = (p.lineage_bits & 1u) ? 22 : 106;
    s_roam   = (uint8_t)(10u + soc * 2u);
  }
  if (s_temper == TEMPER_GOTICO) {            // drifts to a corner and stays
    s_anchor = (p.lineage_bits & 2u) ? 14 : 114;
    s_roam   = 12;
  }

  s_luck  = p.luck;                           // 0..7
  s_rare  = p.rare;
  s_asym  = (uint8_t)(p.mutations >= 3u);
  s_dilate = (uint8_t)(bs >= 6u);

  // The phase is the whole gene nibble, not just its low 2 bits: render.h
  // reads phase as (dy << 2) | dx, so all 4 bits move the matrix and all 16
  // pattern values land on a different coat.
  const uint8_t pat = p.pattern;
  s_pat_level = PF_PAT_LEVEL[pat & 0x0Fu];
  s_pat_phase = (uint8_t)(pat & 0x0Fu);
}

// Keep the resting target REACHABLE. This is no longer about the badges at all
// - the stage already stops the body - it is about the automaton not chasing an
// impossible goal. pf_pick_next() steers toward s_anchor, and a GOTICO
// corner-hugger asks for 114, which a 40 px body cannot reach (its centre tops
// out at 94). Left unclamped it would want the same direction for ever: bounce
// off the wall, turn, immediately want to turn back, and twitch in place
// instead of resting against it.
//
// Still called from petfx_service() as well as petfx_reset(): pf_identity()
// deliberately ignores p.stage, so a baby growing into a 40 px adult changes
// s_body_w_hint without a reset and shrinks the reachable band under it.
static void pf_clamp_anchor(void) {
  const int16_t half = (int16_t)(s_body_w_hint / 2);
  s_anchor = pf_clamp16(s_anchor, (int16_t)(pf_stage_lo() + half),
                                  (int16_t)(pf_stage_hi(s_body_w_hint) + half));
}

// A registered prop is a wall only for a body that was CLEAR of it before this
// step. That single condition is the whole "do not teleport" rule: a body the
// prop appeared underneath - or that the caller's list grew around - simply
// never satisfies it on the way out, so it walks free, and satisfies it again
// the moment it comes back. Returns 1 when the step was blocked, with *x_q4
// parked flush against the prop.
// The prop is measured by its BOX because px_spr() stamps it in
// setBitmapMode(0): all 144 pixels of a 12x12 poop are written, the zeros in
// the inverse colour, so its box IS its silhouette. The BODY is measured by its
// INK AT PROP HEIGHT (pf_ink_span above), because it is blitted transparent and
// its box is mostly empty margin down there - ADULT_BOLOTA's last ink row stops
// 12 px short of the right edge of its 40 px box - so parking at (a - box_w)
// turned the pet round before it reached the poop and it visibly bounced off
// thin air. Narrow-inked bodies never got close enough for the wall to read as
// a wall at all.
static uint8_t pf_obstacle_block(int32_t prev_q4, int32_t* x_q4) {
  if (s_obst_n == 0u || s_obst_w == 0u) return 0;
  int16_t il, ir;
  pf_ink_span(&il, &ir);
  const int16_t p0 = (int16_t)(prev_q4 >> 4);
  const int16_t n0 = (int16_t)(*x_q4 >> 4);
  uint8_t hit  = 0;
  int32_t park = *x_q4;
  for (uint8_t i = 0; i < s_obst_n; i++) {
    const int16_t a = s_obst_x[i];                              // prop left
    const int16_t b = (int16_t)(a + (int16_t)s_obst_w - 1);     // prop right
    if (s_dir > 0) {
      if ((int16_t)(p0 + ir) < a && (int16_t)(n0 + ir) >= a) {
        const int32_t q = (int32_t)(a - 1 - ir) * 16;           // ink flush to a-1
        if (!hit || q < park) park = q;
        hit = 1;
      }
    } else {
      if ((int16_t)(p0 + il) > b && (int16_t)(n0 + il) <= b) {
        const int32_t q = (int32_t)(b + 1 - il) * 16;           // ink flush to b+1
        if (!hit || q > park) park = q;
        hit = 1;
      }
    }
  }
  if (hit) *x_q4 = park;
  return hit;
}

static void pf_enter(uint8_t st, uint16_t len_ms) {
  s_state     = st;
  s_state_ms  = s_now;
  s_state_len = len_ms;
  if (st != PF_HOP) s_hop_y = 0;
}

static void pf_pick_next(void) {
  const PfTemper& T = PF_TEMPER[s_temper];

  uint8_t w_walk = T.w_walk, w_hop = T.w_hop, w_turn = T.w_turn;
  uint8_t w_sit  = T.w_sit,  w_look = T.w_look;

  // A tired pet sits down and stops bouncing.
  const uint8_t energy = s_energy_pct;
  if (energy < 25u) { w_walk = (uint8_t)(w_walk / 3u); w_hop = 0; w_sit = (uint8_t)(w_sit + 22u); }
  else if (energy < 50u) { w_walk = (uint8_t)(w_walk / 2u); w_hop = (uint8_t)(w_hop / 2u); w_sit = (uint8_t)(w_sit + 8u); }

  // Rare animations are a luck gene, thinned out by boredom.
  const uint8_t bore = pf_boredom();
  w_look = (uint8_t)(((uint16_t)w_look * (2u + s_luck)) / 6u);
  w_look = (uint8_t)(((uint16_t)w_look * 4u) / (4u + bore));
  w_hop  = (uint8_t)(((uint16_t)w_hop  * 4u) / (4u + bore));

  const uint16_t total = (uint16_t)(64u);
  uint16_t roll = pf_range(0, (uint16_t)(total - 1u));

  if (roll < w_walk) {
    // Head somewhere inside the roaming window, turning first if needed.
    const int16_t centre = (int16_t)((s_x_q4 >> 4) + s_body_w_hint / 2);
    int8_t want = s_dir;
    if (centre > s_anchor + (int16_t)s_roam)      want = -1;
    else if (centre < s_anchor - (int16_t)s_roam) want = +1;
    else if (pf_chance(1, 3))                     want = (int8_t)-s_dir;
    if (want != s_dir) { s_dir = want; pf_enter(PF_TURN, pf_range(160, 280)); return; }
    s_facing = s_dir;
    pf_enter(PF_WALK, pf_stretch(pf_range(T.walk_lo, T.walk_hi)));
    return;
  }
  roll = (uint16_t)(roll - w_walk);

  if (roll < w_hop)  { pf_enter(PF_HOP, 520); return; }
  roll = (uint16_t)(roll - w_hop);

  if (roll < w_turn) { s_dir = (int8_t)-s_dir; pf_enter(PF_TURN, pf_range(160, 300)); return; }
  roll = (uint16_t)(roll - w_turn);

  if (roll < w_sit)  { pf_enter(PF_SIT, pf_stretch(pf_range(1200, 3600))); return; }
  roll = (uint16_t)(roll - w_sit);

  if (roll < w_look) { pf_enter(PF_LOOK_UP, pf_stretch(pf_range(700, 1600))); return; }

  pf_enter(PF_STAND, pf_stretch(pf_range(T.stand_lo, T.stand_hi)));
}

static void pf_schedule_blink(void) {
  // 3-6 s, irregular by hash rather than by a fixed period: a metronome blink
  // reads as a hardware fault, not as an animal.
  s_blink_at  = s_now + pf_range(2900, 6200);
  s_blink_end = 0;
}

// 90 ms shut, then a fresh irregular interval. Split out because three
// different gates in petfx_service() have to keep the eyelids running.
static void pf_run_blink(void) {
  if (s_blink_end) {
    if ((int32_t)(s_now - s_blink_end) >= 0) { s_blink_end = 0; pf_schedule_blink(); }
  } else if ((int32_t)(s_now - s_blink_at) >= 0) {
    s_blink_end = s_now + 90u;
  }
}

// =============================================================================
//  PUBLIC API
// =============================================================================
void petfx_begin(void) {
  s_began      = 1;
  s_cache_set  = 0xFF;
  s_cache_mir  = 0xFF;
  s_state      = PF_STAND;
  s_state_len  = 900;
  s_x_q4       = (int32_t)((OLED_W - 32) / 2) * 16;
  s_facing     = -1;
  s_dir        = 1;
  s_hop_y      = 0;
  s_squash     = 0;
  s_act_sq_armed = 0;
  s_freeze     = 0;
  s_hold       = 0;
  s_jitter     = 0;
  s_identity   = 0;
  s_seeded     = 0;
  s_obst_n     = 0;
  s_obst_w     = 0;
}

void petfx_reset(const PetView& p) {
  if (!s_began) petfx_begin();

  s_seeded   = 1;
  s_identity   = p.identity;
  s_mood_pct   = p.mood_pct;
  s_energy_pct = p.care_pct[CARE_ENERGY];
  s_seed     = pf_mix(s_identity ^ 0xA5C3F17Bu);
  s_rng      = s_seed | 1u;

  pf_derive(p);

  s_body_w_hint = pf_width_of(p);
  pf_clamp_anchor();
  // The anchor is a CENTRE; turning it into a left edge can land outside the
  // stage for a wide body. Start on the stage or the pet is born mid-wall-bump.
  {
    const int32_t lo = (int32_t)pf_stage_lo() * 16;
    const int32_t hi = (int32_t)pf_stage_hi(s_body_w_hint) * 16;
    int32_t x0 = (int32_t)(s_anchor - s_body_w_hint / 2) * 16;
    if (x0 < lo) x0 = lo;
    if (x0 > hi) x0 = hi;
    s_x_q4 = x0;
  }
  s_walk_acc    = 0;
  s_dir         = (int8_t)((s_seed & 1u) ? 1 : -1);
  s_facing      = s_dir;
  s_hop_y       = 0;
  s_squash      = 0;
  s_act_sq_armed = 0;
  s_startle_end = 0;
  s_jitter      = 0;
  s_last_input  = s_now;
  s_cache_set   = 0xFF;          // the body probably changed too
  s_cache_mir   = 0xFF;
  pf_enter(PF_STAND, 700);
  pf_schedule_blink();
}

void petfx_service(const PetView& p, uint32_t now_ms) {
  if (!s_began) petfx_begin();

  s_now = now_ms;
  s_mood_pct   = p.mood_pct;
  s_energy_pct = p.care_pct[CARE_ENERGY];
  const uint32_t id = p.identity;
  if (!s_seeded || id != s_identity) { petfx_reset(p); return; }

  uint32_t dt = (uint32_t)(now_ms - s_last_ms);
  s_last_ms = now_ms;
  if (dt > 250u) dt = 250u;      // a WiFi stall must not teleport the pet

  s_body_w_hint = pf_width_of(p);
  pf_clamp_anchor();             // the body may have grown since the last reset
  const int32_t x_lo = (int32_t)pf_stage_lo() * 16;
  const int32_t x_hi = (int32_t)pf_stage_hi(s_body_w_hint) * 16;

  // --- gates, in order of severity ------------------------------------------
  // Two different gates, deliberately not the same one:
  //   pinned  - the body must not move. EGG has no legs, DEAD has no business
  //             walking, and a ceremony freeze lets the choreography rely on
  //             the position.
  //   no_eyes - the body must not blink either. An egg and a tomb have no
  //             eyes, and the sleeping art is already drawn with them shut.
  // A FROZEN pet is still alive: it keeps blinking through the ceremony,
  // which is the difference between "paused" and "switched off".
  //   no_face - the body has no left and no right to speak of. An egg does not
  //             face anywhere and neither does a tomb, so their orientation is
  //             forced back to the default. A ceremony freeze is NOT one of
  //             these: it pins the BODY, not the head, so the hatch
  //             choreography can still turn the newborn to look around.
  //   s_hold  - an ACTION choreography is running (actfx.cpp) and the body
  //             must stay exactly where it is while a bowl slides up to it.
  //             That is the PF_ASLEEP suspension and nothing else: no new
  //             states, no walking, but the eyes still blink, the head still
  //             turns and the caller's bob still moves the body. It is
  //             deliberately NOT part of `pinned`, because pinned also means
  //             "recentre", which is exactly what an action must not do.
  const uint8_t asleep  = (uint8_t)((p.flags & PF_ASLEEP) != 0);
  const uint8_t no_face = (uint8_t)(p.stage == STAGE_EGG);
  const uint8_t no_eyes = (uint8_t)(no_face || asleep);
  const uint8_t pinned  = (uint8_t)(no_face || s_freeze);

  if (pinned || asleep || s_hold) {
    if (pinned) {
      s_x_q4 = (int32_t)sprite_center_x(s_body_w_hint) * 16;
      if (no_face) s_facing = -1;
    }
    s_state  = PF_STAND;
    s_hop_y  = 0;
    s_squash = 0;
    s_jitter = 0;
    if (no_eyes) { s_blink_end = 0; s_blink_at = s_now + 3000u; }
    else         pf_run_blink();
    return;
  }

  // Startled: recoil already applied, now hold still.
  if (s_startle_end && (int32_t)(s_now - s_startle_end) < 0) {
    s_hop_y = 0;
    pf_run_blink();
    return;
  }
  s_startle_end = 0;

  // --- run the current state -------------------------------------------------
  const PfTemper& T = PF_TEMPER[s_temper];
  const uint32_t in_state = pf_since(s_state_ms);

  if (s_squash) s_squash--;

  switch (s_state) {
    case PF_WALK: {
      // Integer walk: accumulate q4*ms and spend whole 1/16 px. No floats and
      // no drift, whatever the loop() rate happens to be.
      uint32_t sp = ((uint32_t)s_speed_q4 * pf_amp()) / 100u;
      s_walk_acc += (int32_t)(sp * dt);
      const int32_t step = s_walk_acc / 1000;
      s_walk_acc -= step * 1000;
      const int32_t prev_q4 = s_x_q4;
      s_x_q4 += (int32_t)s_dir * step;
      s_facing = s_dir;

      // Two kinds of wall, one reaction. The stage edge is the scenario
      // contract in petfx.h; a prop is a solid object standing on the same
      // ground, which a body would otherwise fuse with. Both stop the body
      // where it stands and turn it round.
      uint8_t bump = 0;
      if (s_x_q4 <= x_lo)      { s_x_q4 = x_lo; bump = 1; }
      else if (s_x_q4 >= x_hi) { s_x_q4 = x_hi; bump = 1; }
      else if (pf_obstacle_block(prev_q4, &s_x_q4)) bump = 1;

      if (bump) {
        // Belt and braces: a prop parked flush against a body wider than the
        // gap to the stage edge could in principle land outside it. It cannot
        // with today's four poop positions, and this is two comparisons.
        if (s_x_q4 < x_lo) s_x_q4 = x_lo;
        if (s_x_q4 > x_hi) s_x_q4 = x_hi;
        s_dir      = (int8_t)-s_dir;
        s_facing   = s_dir;
        s_walk_acc = 0;
        pf_enter(PF_TURN, 240);
      }
      break;
    }

    case PF_HOP: {
      // Parabola, integer: 4*a*t*(T-t)/T^2 peaks at a. Squash on landing.
      const uint32_t T2 = (uint32_t)s_state_len * (uint32_t)s_state_len;
      const uint32_t t  = (in_state > s_state_len) ? s_state_len : in_state;
      uint32_t amp = ((uint32_t)s_hop_amp * pf_amp()) / 100u;
      if (amp > 12u) amp = 12u;
      s_hop_y = (int16_t)-(int32_t)((4u * amp * t * (s_state_len - t)) / (T2 ? T2 : 1u));
      // A hop carries the pet forward a little, otherwise it reads as a twitch.
      // Props are deliberately NOT walls here: it is airborne, so it may clear
      // one and land on it - and pf_obstacle_block()'s "was it clear before"
      // rule then lets it walk back out instead of pinning it inside.
      s_x_q4 += (int32_t)s_dir * (int32_t)(dt / 8u);
      s_x_q4 = (s_x_q4 < x_lo) ? x_lo : ((s_x_q4 > x_hi) ? x_hi : s_x_q4);
      break;
    }

    case PF_TURN:
      if (in_state * 2u >= s_state_len) s_facing = s_dir;
      break;

    default:
      break;
  }

  // NERVIOSO twitches 1 px while it is not going anywhere. Mutated pets twitch
  // unevenly - the jitter only fires on one side - which is the cheapest
  // readable asymmetry a 1-bit panel can carry.
  if (T.jitter && (s_state == PF_STAND || s_state == PF_LOOK_UP)) {
    if ((s_now / 120u) != ((s_now - dt) / 120u)) {
      const uint32_t h = pf_mix((uint32_t)(s_now / 120u) ^ s_seed);
      s_jitter = (int8_t)((h & 1u) ? 1 : 0);
      if (s_asym && s_facing > 0) s_jitter = 0;
    }
  } else {
    s_jitter = 0;
  }

  // --- state expiry ----------------------------------------------------------
  if (in_state >= s_state_len) {
    if (s_state == PF_HOP) { s_hop_y = 0; s_squash = 2; }
    pf_pick_next();
  }

  pf_run_blink();
}

void petfx_draw_floor(void) {
  U8G2& u = rd_u8g2();
  const uint8_t c = u.getDrawColor();
  u.setDrawColor(1);
  rd_dither_rect(0, PETFX_FLOOR_Y, OLED_W, 1, RD_D50);
  u.setDrawColor(c);
}

// Elliptical-ish contact shadow: two dithered rows, the lower one inset, so it
// reads as a disc and not as a bar. It narrows as the pet rises, which is what
// sells the jump - the body and the shadow separating is the whole trick.
static void pf_draw_shadow(int16_t cx, uint8_t ink_w, int16_t lift) {
  int16_t sw = (int16_t)ink_w - 6 - lift;
  if (sw < 5) sw = 5;
  int16_t sx = (int16_t)(cx - sw / 2);
  // Clipped to the STAGE, not to the panel. Rows 53-54 carry no HUD, so this is
  // not covering for a collision - it is the file's one-sentence contract
  // ("every horizontal limit here comes from PETFX_STAGE_L / PETFX_STAGE_R")
  // being true of every write and not just of the ones that were caught.
  if (sx < (int16_t)PETFX_STAGE_L) { sw += (int16_t)(sx - (int16_t)PETFX_STAGE_L); sx = (int16_t)PETFX_STAGE_L; }
  if (sx + sw > (int16_t)PETFX_STAGE_R + 1) sw = (int16_t)((int16_t)PETFX_STAGE_R + 1 - sx);
  if (sw < 3) return;

  rd_dither_rect(sx, PETFX_SHADOW_Y, sw, 1, RD_D50);
  if (sw >= 10) rd_dither_rect((int16_t)(sx + 2), (int16_t)(PETFX_SHADOW_Y + 1), (int16_t)(sw - 4), 1, RD_D50);
}

void petfx_draw_body(const PetView& p, uint8_t pose, uint8_t frame, int16_t dy,
                     int16_t dx) {
  U8G2& u = rd_u8g2();
  const uint8_t entry_color = u.getDrawColor();   // restored on the way out
  frame = (uint8_t)(frame & 1u);

  const uint8_t no_face = (uint8_t)(p.stage == STAGE_EGG);
  const uint8_t pinned  = (uint8_t)(no_face || s_freeze);

  uint8_t set_id;
  if (p.stage == STAGE_EGG) {
    // Same rule ui.cpp uses, kept in sync deliberately: the egg starts
    // cracking a minute before it hatches.
    set_id = (uint8_t)(((uint32_t)p.age_s + 60u >= AGE_EGG_S) ? SPR_EGG_CRACK : SPR_EGG_IDLE);
  } else {
    // sprite_form_of() is the ONLY correct source of `form`: the adult body
    // comes from the species gene and child/teen variants live in minor_form.
    s_qry_species = p.gene_species;
    s_qry_stage   = p.stage;
    s_qry_form    = p.form;
    s_qry_ok      = 1;
    set_id = sprite_set_id(s_qry_species, s_qry_stage, s_qry_form, pose);
  }
  if (p.stage == STAGE_EGG) s_qry_ok = 0;   // an egg has no poses to ask about

  // no_face, not pinned: a frozen pet keeps whatever way it is facing.
  const uint8_t mirrored = (uint8_t)((!no_face && s_facing > 0) ? 1u : 0u);
  pf_cache_sync(set_id, mirrored);

  const uint8_t w      = s_cache_w;
  const uint8_t h      = s_cache_h;
  const uint8_t stride = pf_stride(w);
  const uint8_t itop   = s_ink_t[frame];
  const uint8_t ibot   = s_ink_b[frame];

  // --- where ------------------------------------------------------------------
  int16_t x = pinned ? (int16_t)sprite_center_x(w) : (int16_t)(s_x_q4 >> 4);
  // The stage, not the panel. This is the last line of defence: the automaton
  // already keeps s_x_q4 inside it, but s_jitter, a pinned centre, the caller's
  // lunge and a body that changed width since the last service() all arrive
  // here unfiltered. dx goes THROUGH this clamp and not around it: a
  // choreography may push the animal sideways, it may not push it off the
  // stage, and the whole HUD keep-out rests on there being no second, softer
  // limit anywhere in this file.
  x = pf_clamp16((int16_t)(x + s_jitter + dx), pf_stage_lo(), pf_stage_hi(w));

  // The INK base lands on the floor, not the sprite box - see petfx.h.
  int16_t y = (int16_t)(PETFX_FLOOR_Y - (int16_t)ibot - 1 + dy + s_hop_y);
  if (s_state == PF_LOOK_UP && !pinned) y -= 1;

  // Clamp the INK, not the sprite box, and clamp it to the BAND, not to the
  // panel. Clamping to 0..OLED_H-1 was not enough: ADULT_BUHO and ADULT_PUNKI
  // carry ink from row 0 of their 40 px box, so their resting y is 13, and a
  // hop (s_hop_y reaches -8 for a light SOLAR at full mood) plus the caller's
  // dy (-1 from kBob or the "nope" wiggle) lands them at y=4. This function
  // blits in setBitmapMode(1), so that ink would OR straight over the status
  // bar's icons, bars and separator instead of covering them. The body may
  // not leave rows SPRITE_AREA_Y..UI_BODY_BOTTOM, airborne or not.
  {
    const int16_t lo = (int16_t)((int16_t)SPRITE_AREA_Y - (int16_t)itop);
    const int16_t hi = (int16_t)((int16_t)(SPRITE_AREA_Y + SPRITE_AREA_H - 1) - (int16_t)ibot);
    y = (hi < lo) ? lo : pf_clamp16(y, lo, hi);   // ink taller than the band: top wins
  }

  // Three sources, one pixel effect: the automaton's landing squash, a
  // choreography's deliberate compression (petfx_squash - a yawn, a waking
  // stretch), and sitting down. The action latch is read against s_now, which
  // petfx_service() writes before every early return, so it expires on time
  // even while the automaton is suspended.
  const uint8_t act_sq = (uint8_t)((s_act_sq_armed &&
                                    (int32_t)(s_now - s_act_sq_until) < 0) ? 1u : 0u);
  if (s_act_sq_armed && !act_sq) s_act_sq_armed = 0;
  const uint8_t squash = (uint8_t)((s_squash != 0 || act_sq ||
                                    (s_state == PF_SIT && !pinned)) ? 1u : 0u);

  s_draw_x = x; s_draw_y = y; s_draw_w = w; s_draw_h = h;

  // TRANSPARENT BLITS FOR THE WHOLE OF THIS FUNCTION. render.cpp:357 puts the
  // panel in setBitmapMode(0), where drawXBM paints the ENTIRE w*h box - the
  // 1 bits in draw_color and the 0 bits in the inverse (u8g2_bitmap.c:111,
  // 156). That is right for ui.cpp, whose sprites want to erase their own
  // background, and catastrophic here for three separate reasons:
  //   * a 40 px body sits at y=15, so its box covers rows 15..54 and would
  //     wipe the floor line (52) and both shadow rows (53, 54);
  //   * the dilation copy's zeros would erase the first copy's ones, so
  //     "draw it twice, one pixel over" would just move the sprite;
  //   * the eyelid strips would paint solid bars instead of masks.
  // Restored before returning: everything else in the firmware expects mode 0.
  u.setBitmapMode(1);

  // --- shadow, under everything ------------------------------------------------
  // The lift is measured, not taken from s_hop_y: the caller's dy and the
  // LOOK_UP tiptoe raise the body too, and a shadow that ignores them is the
  // one thing that gives the trick away.
  {
    const int16_t cx   = (int16_t)(x + (s_ink_l[frame] + s_ink_r[frame]) / 2);
    const uint8_t inkw = (uint8_t)(s_ink_r[frame] - s_ink_l[frame] + 1u);
    int16_t lift = (int16_t)((PETFX_FLOOR_Y - 1) - (y + (int16_t)ibot));
    if (lift < 0) lift = 0;
    pf_draw_shadow(cx, inkw, lift);
  }

  // --- body --------------------------------------------------------------------
  // Squash drops sprite row 0 and starts at y+1, so every remaining row keeps
  // the screen position it had: the feet stay planted and the pet loses 1 px
  // off the top. Widening by one column is the "stretch" half of the pair.
  const uint8_t* bits = s_cache_bits[frame];
  int16_t  by = y;
  uint8_t  bh = h;
  if (squash) { bits += stride; bh = (uint8_t)(h - 1u); by = (int16_t)(y + 1); }

  // gene_body_size high, or a landing squash: draw the same bitmap one pixel
  // to the side. Two overlapping copies IS a dilation - the cheapest "this pet
  // is fat" the panel can express.
  //
  // WHERE the copy may land is decided HERE, before anything is blitted, for
  // two reasons. The clear below has to cover it, and - this was the last x
  // limit in the file that did not come from the stage - it used to be guarded
  // against the PANEL (x2 + w <= OLED_W), not against the scenario. At x = 14
  // the copy landed in column 13, inside the left HUD band: measured over the
  // real atlas, a mirrored squashing ADULT_BUHO at x = 14 lost 3 px, ADULT_MOHO
  // 4, and those 112 px were every single destroyed body pixel in the sweep.
  // Against a wall the offset is FLIPPED rather than dropped, so a fat pet stays
  // fat at the edges of the stage; the direction of a 1 px dilation carries no
  // meaning, only its presence does.
  int16_t xw0 = x, xw1 = (int16_t)(x + w - 1);
  int16_t x2  = x;
  uint8_t dup = 0;
  if (s_dilate || squash) {
    const int16_t slo = pf_stage_lo();
    const int16_t shi = pf_stage_hi(w);
    const int16_t off = squash ? -1 : ((s_facing > 0) ? 1 : -1);
    x2 = (int16_t)(x + off);
    if (x2 < slo || x2 > shi) x2 = (int16_t)(x - off);
    if (x2 >= slo && x2 <= shi && by >= 0) {
      dup = 1;
      if (x2 < xw0) xw0 = x2;
      if (x2 + w - 1 > xw1) xw1 = (int16_t)(x2 + w - 1);
    }
  }

  // OPAQUE AGAINST THE SCENERY, TRANSPARENT AGAINST ITSELF.
  //
  // Scenery (the floor line, the poops, the action props) is drawn BEFORE the
  // body. The price of that, with a transparent blit, would be that the scenery
  // shows through every hole in the silhouette - eye sockets, mouth, the gap
  // between the legs, the notch between an owl's ears - and a body you can see
  // the ground through is not a body. So the blit clears its own ink box first
  // and paints into the hole.
  //
  // So clear the body's INK BOX first and blit into the hole. Deliberately the
  // INK box and not the sprite box: the sprite box of a 40 px body reaches row
  // 54. The bottom is additionally clamped to PETFX_FLOOR_Y - 1, because the
  // ink is NOT always above the floor line - the caller's idle bob reaches
  // dy = +1 and y is derived as FLOOR_Y - ibot - 1 + dy, which puts the last
  // ink row exactly ON row 52 once a cycle. Without the clamp that one frame
  // would punch the pet's own width out of the floor line. The two shadow rows
  // (53, 54) are never reachable at all.
  //
  // It also erases whatever poop is underneath the body, and that is correct:
  // the pet now treats a poop as a wall and only ever passes in FRONT of one on
  // its way somewhere, so "the body occludes the prop it is walking past" is the
  // reading we want. What used to happen - a prop drawn after the body, inside
  // an opaque halo, deleting up to 141 px of the actor - is not.
  {
    const int16_t iy0 = (int16_t)(y + ((squash && itop < 1u) ? 1 : (int16_t)itop));
    int16_t       iy1 = (int16_t)(y + (int16_t)ibot);
    if (iy1 > (int16_t)(PETFX_FLOOR_Y - 1)) iy1 = (int16_t)(PETFX_FLOOR_Y - 1);
    const int16_t ix0 = (int16_t)(xw0 + (int16_t)s_ink_l[frame]);
    const int16_t ix1 = (int16_t)(xw1 - (int16_t)(w - 1 - s_ink_r[frame]));
    // Published here, from the box the body actually clears, so petfx_body_ink()
    // can never disagree with what was drawn. iy1 is the floor-clamped value on
    // purpose: a prop standing beside the animal wants the visible extent, and
    // the clamp only bites on the one frame in four where the idle bob drops
    // the last ink row onto the floor line.
    s_ink_x0 = ix0; s_ink_y0 = iy0; s_ink_x1 = ix1; s_ink_y1 = iy1;
    if (iy1 >= iy0 && ix1 >= ix0 && iy0 >= 0 && ix0 >= 0) {
      u.setDrawColor(0);
      u.drawBox((u8g2_uint_t)ix0, (u8g2_uint_t)iy0,
                (u8g2_uint_t)(ix1 - ix0 + 1), (u8g2_uint_t)(iy1 - iy0 + 1));
    }
  }

  u.setDrawColor(1);
  if (by >= 0 && x >= 0) u.drawXBM((u8g2_uint_t)x, (u8g2_uint_t)by, w, bh, bits);
  if (dup) u.drawXBM((u8g2_uint_t)x2, (u8g2_uint_t)by, w, bh, bits);

  // --- coat pattern (gene_pattern) ---------------------------------------------
  // Draw solid, then ERASE a dither over the ink box. Erasing a 0 leaves a 0,
  // so the texture can only appear inside the silhouette: perfect clipping,
  // no mask, no second bitmap. The box is the INK box, never the sprite box -
  // the sprite box reaches down to row 54 on a 40 px body and would eat the
  // floor line and the shadow.
  if (s_pat_level) {
    uint8_t lvl = s_pat_level;
    // Small bodies dissolve faster, and after fix 9 the old "cap babies at 2"
    // was dead code - nothing exceeds 2 any more. Cap them at 1 instead, which
    // is what the rule always meant: measured on the art, level 2 eats outline
    // pixels off a 24 px body (BABY_GATO's ear row goes from ###### to ###.#)
    // while level 1 leaves every outline row whole and still reads as texture.
    if (p.stage <= STAGE_BABY && lvl > 1u) lvl = 1u;

    const int16_t py0 = (int16_t)(y + ((squash && itop < 1u) ? 1 : itop));
    // Same clamp as the clear box above, and for the same reason: the caller's
    // bob reaches dy = +1, which drops the last ink row ONTO the floor line
    // (PETFX_FLOOR_Y). The coat dithers in colour 0, so without this it erases
    // floor pixels under and beside the body - measured up to 11 px, about half
    // the lit floor pixels in the animal's footprint, blinking in and out with
    // the bob and with x & 3. The floor visibly flickered between a 50 % and a
    // 25 % dither. Costs the last ink row its texture on that one frame in four.
    int16_t py1 = (int16_t)(y + ibot);
    if (py1 > (int16_t)(PETFX_FLOOR_Y - 1)) py1 = (int16_t)(PETFX_FLOOR_Y - 1);
    const int16_t px0 = (int16_t)(xw0 + s_ink_l[frame]);
    const int16_t px1 = (int16_t)(xw1 - (int16_t)(w - 1 - s_ink_r[frame]));
    if (py1 >= py0 && px1 >= px0) {
      // rd_dither_rect_phase() evaluates the Bayer matrix on ABSOLUTE rows and
      // columns - the pixel is touched when RD_BAYER[((yy + dy) & 3) * 4 +
      // ((xx + dx) & 3)] < level, with dx = ph & 3 and dy = ph >> 2. A CONSTANT
      // ph therefore nails the coat to the SCREEN and the body slides under its
      // own texture: measured on the real art, moving the body one pixel flips
      // up to 64 % of the pixels inside its own silhouette (494 of ADULT_BUHO's
      // 774 ink pixels), so 13 of the 16 gene_pattern values shimmered instead
      // of showing a texture and the declared point of the gene was lost.
      //
      // The fix is to subtract the body's own origin, PER AXIS - never from the
      // nibble as a whole, where a borrow out of the low half would leak the x
      // position into dy. That turns (yy + dy) and (xx + dx) into BODY-LOCAL
      // coordinates and makes the coat exactly translation invariant.
      //
      // WHICH origin is the whole game, and the first attempt got it wrong.
      // Subtracting the INK bounds (py0 / px0) is translation invariant but NOT
      // frame invariant: 24 of the 38 sprite sets have different ink bounds in
      // frame 0 and frame 1 (measured), so the anchor jumped a row or a column
      // every UI_ANIM_FRAME_MS (420 ms) and re-rolled the coat inside the
      // silhouette the two frames share - 5979 pixels across the 34 body sets
      // at level 5, worst 530 on ADULT_BOLOTA and 525 on ADULT_PUNKI. The same
      // shimmer, moved from the x axis to the frame counter.
      //
      // These two anchors are frame-independent BY CONSTRUCTION:
      //   x - the sprite BOX origin. It never depended on the frame at all.
      //   y - the FLOOR CONTACT row, y + ibot, and deliberately NOT the box
      //       top. y is derived as PETFX_FLOOR_Y - ibot - 1 + dy + hop, so
      //       y + ibot cancels ibot out and is the same screen row in both
      //       frames whatever the art does, while the box top is only safe as
      //       long as no set ever changes its ink BOTTOM between frames. None
      //       of the 38 does today, so the two are numerically equal on this
      //       art; the floor row is the one that stays correct if that changes.
      //       It is also the row petfx.h registers every body against, so the
      //       coat ends up pinned to the feet exactly like the art is.
      //
      // Verified on the host over every set: moving the body 1 px in x or in y
      // changes 0 coat pixels, and toggling the frame changes 0 coat pixels
      // inside the silhouette the two frames share on screen.
      const int16_t ax = x;
      const int16_t ay = (int16_t)(y + (int16_t)ibot);
      const uint8_t ph = (uint8_t)(
          ((((uint8_t)(s_pat_phase >> 2) - (uint8_t)(ay & 3)) & 3u) << 2) |
          (((uint8_t)s_pat_phase        - (uint8_t)(ax & 3)) & 3u));
      u.setDrawColor(0);
      rd_dither_rect_phase(px0, py0, (int16_t)(px1 - px0 + 1), (int16_t)(py1 - py0 + 1),
                           lvl, ph);
      u.setDrawColor(1);
    }
  }

  // --- blink --------------------------------------------------------------------
  // Gated on no_eyes, not on `pinned`: a pet held still by a ceremony is
  // paused, not switched off, and a body that never blinks reads as dead.
  const uint8_t no_eyes = (uint8_t)((p.stage == STAGE_EGG) ||
                                    ((p.flags & PF_ASLEEP) != 0));
  if (s_blink_end && s_lid_h[frame] && !no_eyes) {
    const int16_t ly = (int16_t)(y + s_lid_y[frame]);
    if (ly >= 0 && x >= 0) {
      u.setDrawColor(1);
      u.drawXBM((u8g2_uint_t)x, (u8g2_uint_t)ly, w, s_lid_h[frame], s_lid_fill[frame]);
      u.setDrawColor(0);
      u.drawXBM((u8g2_uint_t)x, (u8g2_uint_t)ly, w, s_lid_h[frame], s_lid_cut[frame]);
      u.setDrawColor(1);
    }
  }

  // --- gene_rare: a permanent orbiting spark -------------------------------------
  // Not while pinned: ceremonies (evolution, hatch) run their own spark
  // choreography in ui.cpp and two sets of sparks read as noise.
  if (s_rare && !pinned) {
    const SpriteRef sp = sprite_emote(EMO_SPARK);
    const uint8_t   k  = (uint8_t)((s_now / 220u) % 12u);
    const int16_t   cx = (int16_t)(x + w / 2);
    const int16_t   cy = (int16_t)(y + ibot / 2);
    const int16_t   sx = (int16_t)(cx + PF_ORB_X[k] - sp.w / 2);
    const int16_t   sy = (int16_t)(cy + PF_ORB_Y[k] - sp.h / 2);
    // The STAGE, not the panel. The orbit is +/-24 px around the body centre, so
    // on a 40 px adult against either wall it reached columns 6..13 and 114..121
    // - inside both badges. A spark under an opaque badge is not a bug you can
    // see, which is exactly why it survived four rounds; it is still the actor
    // writing outside its own scenario. Skipped rather than clamped: bunching
    // the orbit against the wall would read as a stuck pixel, and a sparkle that
    // blinks out for part of its circle reads as a sparkle.
    if (sx >= (int16_t)PETFX_STAGE_L && (int16_t)(sx + sp.w - 1) <= (int16_t)PETFX_STAGE_R &&
        sy >= SPRITE_AREA_Y && sy + sp.h <= RD_AFFORD_Y)
      u.drawXBM((u8g2_uint_t)sx, (u8g2_uint_t)sy, sp.w, sp.h, sp.bits);
  }

  // --- gene_mutations >= 3: one pixel that will not sit still ---------------------
  if (s_asym && !pinned) {
    const uint32_t t = s_now / 170u;
    if (pf_mix(t ^ s_seed) & 1u) {
      const uint8_t span = (uint8_t)((ibot > itop) ? (uint8_t)((ibot - itop) / 2u + 1u) : 1u);
      const int16_t mx = (int16_t)(x + ((s_facing > 0) ? (int16_t)s_ink_l[frame] - 1
                                                      : (int16_t)s_ink_r[frame] + 1));
      const int16_t my = (int16_t)(y + itop + (int16_t)(pf_mix(t) % span));
      // The STAGE again: this pixel sits one column OUTSIDE the ink, so a body
      // whose ink touches the edge of its own box puts it at column 13 or 114.
      if (mx >= (int16_t)PETFX_STAGE_L && mx <= (int16_t)PETFX_STAGE_R &&
          my >= SPRITE_AREA_Y && my < RD_AFFORD_Y)
        u.drawPixel((u8g2_uint_t)mx, (u8g2_uint_t)my);
    }
  }

  u.setBitmapMode(0);          // hand the panel back exactly as render.cpp set it up
  u.setDrawColor(entry_color);
}

int16_t petfx_body_x(void) { return s_draw_x; }
int16_t petfx_body_y(void) { return s_draw_y; }
uint8_t petfx_body_w(void) { return s_draw_w; }
uint8_t petfx_body_h(void) { return s_draw_h; }

// See petfx.h. Scans the raw atlas rather than the sprite cache on purpose: the
// cache holds the pose being DRAWN, and rebuilding it for a question would make
// the next frame pay for a re-sync of ~600 bytes. This costs one scan of a
// 40x40 bitmap, once per choreography.
void petfx_pose_ink_x(uint8_t pose, int16_t* x0, int16_t* x1) {
  int16_t lo = s_ink_x0, hi = s_ink_x1;      // the honest fallback: what is drawn
  if (s_qry_ok) {
    const uint8_t   id = sprite_set_id(s_qry_species, s_qry_stage, s_qry_form, pose);
    const SpriteSet s  = sprite_set(id);
    const uint8_t   st = pf_stride(s.w);
    const uint16_t  fb = (uint16_t)((uint16_t)st * s.h);
    uint8_t l = 0xFFu, r = 0u, seen = 0u;
    for (uint8_t f = 0; f < 2u; f++) {
      const uint8_t  src_f = (uint8_t)((f < s.frames) ? f : 0u);
      uint8_t t2, b2, l2, r2;
      if (!pf_scan_ink(s.bits + (uint32_t)fb * src_f, s.w, s.h, &t2, &b2, &l2, &r2))
        continue;
      if (l2 < l) l = l2;
      if (r2 > r) r = r2;
      seen = 1u;
    }
    if (!seen) { l = 0u; r = (uint8_t)(s.w - 1u); }

    // Both facings. pf_cache_sync() mirrors the whole frame, which maps column
    // c to (w-1-c), so an authored span [l,r] becomes [w-1-r, w-1-l]. The pet
    // turns to look at its dinner in the middle of the film, so the union of
    // the two is the only span that is still true on the last frame.
    const uint8_t ml = (uint8_t)(s.w - 1u - r);
    const uint8_t mr = (uint8_t)(s.w - 1u - l);
    const uint8_t ul = (uint8_t)((ml < l) ? ml : l);
    const uint8_t ur = (uint8_t)((mr > r) ? mr : r);

    // Same x derivation as petfx_draw_body(), with THIS pose's width: the draw
    // clamp uses the drawn width, and SPR_EAT_ADULT is 40 px where a senior's
    // idle body is 32, so the two poses do not always land on the same column.
    const uint8_t no_face = (uint8_t)(s_qry_stage == STAGE_EGG);
    const uint8_t pinned  = (uint8_t)(no_face || s_freeze);
    int16_t x = pinned ? (int16_t)sprite_center_x(s.w) : (int16_t)(s_x_q4 >> 4);
    x = pf_clamp16((int16_t)(x + s_jitter), pf_stage_lo(), pf_stage_hi(s.w));

    lo = (int16_t)(x + (int16_t)ul);
    hi = (int16_t)(x + (int16_t)ur);
    // The dilation copy, unconditionally and in both directions. It is drawn at
    // x-1 or x+1 depending on the facing and on whether the stage edge flipped
    // it, and the clear box covers whichever it was; guessing which costs a
    // column of gap if it is wrong and a hole in the prop if it is not.
    lo = (int16_t)(lo - 1);
    hi = (int16_t)(hi + 1);
  }
  if (x0) *x0 = lo;
  if (x1) *x1 = hi;
}

void petfx_squash(uint16_t ms) {
  if (ms == 0u) { s_act_sq_armed = 0; return; }
  if (ms > 2000u) ms = 2000u;          // an expression, not a body shape
  s_act_sq_until = s_now + ms;
  s_act_sq_armed = 1;
}

void petfx_body_ink(int16_t* x0, int16_t* y0, int16_t* x1, int16_t* y1) {
  if (x0) *x0 = s_ink_x0;
  if (y0) *y0 = s_ink_y0;
  if (x1) *x1 = s_ink_x1;
  if (y1) *y1 = s_ink_y1;
}

void petfx_startle(uint16_t ms) {
  // Recoil away from wherever it was looking, then hold. The recoil is applied
  // once, here, so the effect survives even if service() is not called again
  // before the next frame.
  const int32_t back = (int32_t)((s_facing > 0) ? -3 : 3) * 16;
  s_x_q4 += back;
  const int32_t lo = (int32_t)pf_stage_lo() * 16;
  const int32_t hi = (int32_t)pf_stage_hi(s_body_w_hint) * 16;
  if (s_x_q4 < lo) s_x_q4 = lo;
  if (s_x_q4 > hi) s_x_q4 = hi;
  s_walk_acc    = 0;
  s_hop_y       = 0;
  s_squash      = 2;
  s_startle_end = s_now + (ms ? ms : 1u);
  pf_enter(PF_STAND, ms ? ms : 1u);
}

void petfx_attention(void) {
  s_last_input = s_now;                 // boredom scaling starts over
  if (s_freeze) return;
  petfx_face_point(OLED_W / 2);
  pf_enter(PF_ATTENTION, 1100);
  s_blink_at = s_now + 400u;            // a quick blink acknowledges the poke
}

void petfx_face_point(int16_t x) {
  const int16_t centre = (int16_t)((s_x_q4 >> 4) + s_body_w_hint / 2);
  const int8_t  want   = (int8_t)((x < centre) ? -1 : 1);
  if (want == s_facing) return;
  s_dir = want;
  // petfx_service() returns early while s_freeze or s_hold is set, so PF_TURN
  // would never run and a ceremony - or a choreography asking the pet to watch
  // its dinner arrive - would get 800 ms of a motionless body facing the wrong
  // way. A suspended pet turns on the spot instead.
  if (s_freeze || s_hold) { s_facing = want; return; }
  pf_enter(PF_TURN, 220);
}

void petfx_set_obstacles(const int16_t* xs, uint8_t n, uint8_t w) {
  if (xs == nullptr || w == 0u) n = 0u;
  if (n > (uint8_t)PF_OBST_MAX) n = (uint8_t)PF_OBST_MAX;
  for (uint8_t i = 0; i < n; i++) s_obst_x[i] = xs[i];
  s_obst_n = n;
  s_obst_w = (uint8_t)(n ? w : 0u);
}

void petfx_freeze(uint8_t on) {
  s_freeze = (uint8_t)(on ? 1u : 0u);
  // s_facing is deliberately NOT reset: freezing pins the body, and the hatch
  // ceremony drives the head through petfx_face_point() while it is held.
  if (on) { s_hop_y = 0; s_squash = 0; s_jitter = 0; }
}

void petfx_hold(uint8_t on) {
  // Same suspension as freeze, minus the recentring - see petfx.h. s_x_q4 is
  // deliberately untouched in BOTH directions: taking the hold must not move
  // the pet, and releasing it must not either, or every meal would end with the
  // animal jumping a pixel.
  s_hold = (uint8_t)(on ? 1u : 0u);
  if (on) { s_hop_y = 0; s_squash = 0; s_jitter = 0; }
  // Releasing the hold ends the film, and the film is the only thing that ever
  // asks for a compression. Belt and braces on top of the deadline: this is the
  // path actfx_cancel() takes, so a pet cannot be left squat by an action that
  // was interrupted a millisecond before its own timer would have let go.
  else s_act_sq_armed = 0;
}
