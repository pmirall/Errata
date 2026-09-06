// =============================================================================
//  PEBBLEBOL - ui/petfx_core.h
//  THE PIXEL HALF OF petfx, IN A TRANSLATION UNIT A HOST BINARY CAN LINK.
//  P9-C6.
//
//  WHY THIS FILE EXISTS. ui/petfx.cpp has carried a block marked
//  "---8<--- PETFX PIXEL CORE ---8<---" since phase 4, whose banner said
//  "everything between these markers is host-testable: it depends on sprites.h
//  and stdint only". That was true of the CODE and false of the BUILD: petfx.cpp
//  includes render.h, which includes Arduino.h and U8g2lib.h, so no host binary
//  has ever compiled a line of it. The marker described an intention; nothing
//  enforced it and nothing executed it.
//
//  IT COST SOMETHING. The P9 exit review composited the blink frame by hand -
//  a picture no test in the tree had ever drawn - and found that on four bodies
//  pf_build_lids() did not close a pair of eyes, it filled a third of the
//  creature solid: DENYRA +79 px on a 283 px body, BLAKLIX +78 on 264, MURAX
//  +68 on 242, PANOPTIX losing all nine of the eyes the species is named for.
//  Every byte check in the tree passed while it happened, because the blink
//  frame is not in the atlas; it is computed at draw time from the atlas plus
//  this code, and this code was unreachable.
//
//  SO THE PIXEL CORE MOVED OUT, exactly as ui/xbm_mirror.cpp (P4-C4) and
//  ui/corrupt_fx.cpp (P9-C5) moved out before it and for the same reason: a
//  rule that lives where no test can reach it is a rule nobody can check.
//  tests/test_sprite_pipeline.cpp now drives the SHIPPED pf_build_lids() over
//  the SHIPPED atlas and asserts the property that matters - a blink may close
//  holes and may not draw over the body - and tests/tools/sprite_dump.cpp's
//  `blink` mode renders the composited frame for a human to look at.
//
//  PURE MODULE: stdint plus ui/xbm_mirror.h (itself pure) only. No Arduino, no
//  u8g2, no render.h, no PetView, no clock, no RNG, no heap, no float, no I/O.
//  tools/check.sh fails the build if a device header appears here.
//
//  WHAT STAYED IN petfx.cpp: the caches these functions fill, the automaton,
//  the choreography and every call into the renderer. This file has no state.
// =============================================================================
#ifndef PB_UI_PETFX_CORE_H
#define PB_UI_PETFX_CORE_H

#include <stdint.h>

#include "xbm_mirror.h"

// -----------------------------------------------------------------------------
//  Geometry of the biggest body we will ever cache. THIS WENT FROM 40 TO 24 AT
//  P9-C3 and it is a RAM saving, not a cost: the legacy atlas held five body
//  sizes (24 baby, 28 child, 32 teen, 40 adult, 32 senior) and petfx's caches
//  were sized for the largest. Every body in the generated atlas is 24x24, and
//  data/sprites_pebbles.h's own guard refuses a set wider than that.
//
//  MEASURED IN THE RELEASE .elf, because P9-C3's arithmetic for this was wrong
//  in two documents and the P9 exit re-measured it:
//    s_cache_bits  2 x 200 B -> 2 x 72 B   (400 -> 144)
//    s_lid_fill    2 x  60 B -> 2 x 36 B   (120 ->  72)
//    s_lid_cut     2 x  60 B -> 2 x 36 B   (120 ->  72)
//  640 B of .bss becomes 288 B. Phase 10 gets the 352 B, and 352 is exactly the
//  globals delta the matrix printed (59,396 -> 59,044). PF_STRIP_BYTES is
//  PF_MAX_STRIDE * PF_EYE_MAX_H = 3 * 12 = 36; it was described as 24 in
//  petfx.cpp and in docs/budget.md, which is the one number a reader would use
//  to re-derive the strip size.
//
//  If a future set is ever wider than 24, pf_load_cache()'s own bounds check
//  (s.w > PF_MAX_W) refuses to cache it and the body simply does not animate -
//  it does not overflow. Raise these two numbers and BODY_W/H in
//  tools/gen_sprites.py together, or not at all.
// -----------------------------------------------------------------------------
#define PF_MAX_W        24
#define PF_MAX_H        24
#define PF_MAX_STRIDE   3
#define PF_FRAME_BYTES  (PF_MAX_STRIDE * PF_MAX_H)         // 72 B
#define PF_EYE_MAX_H    12
#define PF_STRIP_BYTES  (PF_MAX_STRIDE * PF_EYE_MAX_H)     // 36 B

// The row stride and the horizontal flip live in ui/xbm_mirror.h with P4-C4:
// ui/battle_renderer.cpp needs the same flip to face two combatants at each
// other, and a second copy of a routine whose whole difficulty is one
// off-by-four on a 28 px sprite is exactly the kind that gets fixed once.
static inline uint8_t pf_stride(uint8_t w) { return xbm_stride(w); }

static inline uint8_t pf_get(const uint8_t* row, uint8_t x) {
  return (uint8_t)((row[x >> 3] >> (x & 7u)) & 1u);
}

static inline void pf_set(uint8_t* row, uint8_t x) {
  row[x >> 3] = (uint8_t)(row[x >> 3] | (uint8_t)(1u << (x & 7u)));
}

// -----------------------------------------------------------------------------
//  pf_scan_ink - tightest box that contains a lit pixel.
//  The art has empty margins and they are not the same on every body, so the
//  floor, the shadow and the coat pattern all have to be measured, never
//  assumed. Returns 0 when the frame is blank; the callers then fall back to
//  the sprite box.
// -----------------------------------------------------------------------------
uint8_t pf_scan_ink(const uint8_t* bits, uint8_t w, uint8_t h,
                    uint8_t* top, uint8_t* bot, uint8_t* left, uint8_t* right);

// -----------------------------------------------------------------------------
//  pf_build_lids - turn one eye band into two little XBM strips.
//
//    fill : every interior run of unlit pixels on a row of the band, inside the
//           x window, drawn with colour 1, so the socket goes solid.
//    lid  : the fill mask of the band's FIRST row, placed on the band's MIDDLE
//           row and drawn with colour 0. The first row is the un-split socket
//           outline (lower rows are cut in two by the pupil), so re-opening it
//           one row down gives a continuous slit instead of two notches.
//
//  Both strips are full sprite width so they can be blitted at (x, y + y0) with
//  a plain drawXBM. Returns the band height, 0 when there is nothing to do (a
//  blank band, or a band the generator marked "this body does not blink" by
//  emitting y1 < y0).
//
//  THE RULE IS ROW-WISE AND THAT IS THE WHOLE DIFFICULTY. A run of zeros
//  bounded by ink ON ITS OWN ROW is filled whether or not it is an enclosed
//  hole in the drawing - the gap between two legs qualifies, and so does the
//  seam between two blocks. That is cheap (no flood fill on a device) and it is
//  correct ONLY IF THE BAND IS THE EYES. Keeping it so is
//  tools/gen_sprites.py's eye_band()'s job, and
//  tests/test_sprite_pipeline.cpp's the_blink_closes_holes_and_never_draws_over
//  _the_body is what proves it did that job, over all 128 (set, frame) pairs of
//  the shipped atlas, with this exact function.
// -----------------------------------------------------------------------------
uint8_t pf_build_lids(const uint8_t* bits, uint8_t w, uint8_t h,
                      uint8_t y0, uint8_t y1, uint8_t x0, uint8_t x1,
                      uint8_t* fill, uint8_t* lid);

// =============================================================================
//  pf_build_sleep - THE SLEEPING BODY, DERIVED (P10-C3).
//
//  THE PROBLEM PHASE 9 NAMED AND LEFT OPEN. data/sprites.h's sprite_set_id()
//  answered POSE_SLEEP with ONE generic body for all sixty species, and SLEEP
//  is not a film - pet_pose_of() returns it from PF_ASLEEP, it persists for
//  hours, and it is drawn on HOME where the creature's silhouette is the ONLY
//  thing distinguishing one player's Pebble from another's. So the pose a
//  player stares at longest was the pose where every Pebble looked the same.
//
//  WHY IT IS DERIVED AND NOT DRAWN. Twenty families x two poses is 5,760 B of
//  art, which fits the flash budget easily and does NOT fit either declared
//  art cap (PB_SPRITE_DATA_BYTES_MAX has 1,024 B of headroom, exactly seven
//  sets) - so it is a re-plan of both caps plus forty hand-drawn 24x24 bodies
//  that a person has to look at one at a time. What is NOT expensive is this:
//  every piece of the derivation already ships, is already host-linked and is
//  already mutation-tested. A sleeping species is its own body with its eyes
//  shut and its weight settled, and pf_build_lids() has closed eyes over the
//  shipped atlas since P9-C6.
//
//  WHAT IT DOES, in the order it does it:
//    1. THE EYES SHUT. pf_build_lids() over the same band the blink uses, its
//       fill ORed in and its lash line cut out. Not a second rule: literally
//       the function the blink calls, so a body that blinks correctly sleeps
//       correctly and a body the generator marked "does not blink" (y1 < y0)
//       simply keeps its eyes open and relies on step 2.
//    2. THE WEIGHT SETTLES. The two topmost rows of ink are merged into one, so
//       the creature is a pixel shorter without losing its outline - a squash,
//       not a chop - and the bottom PF_SLEEP_SPREAD rows are dilated one column
//       each way, so it splays where it meets the floor. ONE column and not
//       two: ui/petfx.cpp's petfx_pose_ink_x() grows its answer by exactly one
//       column in each direction to cover the walk's dilation copy, and a pose
//       two columns wider than that would put a prop a pixel inside a body.
//
//  STEP 2 IS THE ONE THAT CARRIES IT, and that is deliberate.
//  tests/test_sprite_pipeline.cpp already records that LEKRON's blink changes
//  2 px of a 303 px body and is invisible at 1x. Two pixels is fine for a 90 ms
//  blink and is NOT fine for a pose held for hours: a sleeping body identical
//  to its idle body except for two pixels reads as broken plumbing, which is
//  worse than a generic blob that at least reads as a sleeping lump. The squash
//  is shape-independent, so it fires on every body whatever its face is like.
//
//  THE FLOOR IS THE FALSIFIABLE PART. The return value is the number of pixels
//  that DIFFER from the source, and tests/test_sprite_pipeline.cpp requires
//  every one of the sixty bodies, on both frames, to clear PF_SLEEP_MIN_DIFF.
//  A body that cannot is not a body this derivation may be used on, and the
//  owner step is to author a sleep set for it - which is what the seven sets of
//  atlas headroom are for. THE BLINK TEST DELIBERATELY HAS NO SUCH FLOOR and
//  says so; this one has, because the failure mode is different.
//
//  `out` must be at least pf_stride(w) * h bytes. Returns 0 - and leaves `out`
//  untouched - for a blank frame or a frame larger than the cache geometry, in
//  which case the caller falls back to the authored PBSPR_SLEEP body.
//
//  CONTAINMENT, which is what makes "not a mangled body" checkable: every lit
//  pixel of `out` lies inside the source's own ink box grown by one column on
//  each side and shrunk by one row at the top. Nothing is added below, nothing
//  is added above, and the body cannot grow past its own sprite box.
// =============================================================================
#define PF_SLEEP_SPREAD    4     // bottom rows that dilate sideways

// MEASURED OVER THE SHIPPED ATLAS AT P10-C3, NOT CHOSEN, and the measurement
// changed the code rather than the other way round. The first settle spread
// only PF_SLEEP_SPREAD = 2 rows and the sweep failed on ESTATIC at 7 px - an
// aerial whose base is already fifteen pixels wide, so a one-row dilation had
// almost nothing to add. Widening the settle to four rows took it to 13 and
// took LEKRON - the body tests/test_sprite_pipeline.cpp names as the one whose
// BLINK changes 2 px and is invisible at 1x - to 20 and 24. Both were looked at
// with `./bin/sprite_dump sleep ESTATIC` and `... LEKRON` before the number
// below was written down.
//
// So: the roster's smallest change is 13 px, the floor is 10, and a body that
// falls under it FAILS THE SUITE BY NAME rather than shipping as a Pebble that
// sleeps by changing nothing a player can see. Re-measure it if the art
// changes; do NOT lower it to make a build pass. If a future body genuinely
// cannot clear it, the owner step is to author a sleep set for it - the atlas
// has 1,024 B of headroom, which is exactly seven sets, and that budget exists
// for these exceptions.
#define PF_SLEEP_MIN_DIFF  10

uint16_t pf_build_sleep(const uint8_t* bits, uint8_t w, uint8_t h,
                        uint8_t y0, uint8_t y1, uint8_t x0, uint8_t x1,
                        uint8_t* out);

#endif // PB_UI_PETFX_CORE_H
