// =============================================================================
//  PEBBLEBOL - ui/anim_ease.h
//  THE MOTION MATHS, IN A TRANSLATION UNIT A HOST BINARY CAN LINK. P10-C3.
//
//  WHY THIS FILE EXISTS. Every film in this firmware - the seven in
//  ui/actfx.cpp since P6-C3, and the two ui/screen_encounter.cpp gains in this
//  chunk - is built out of the same five pieces of integer arithmetic: a
//  parabola, a lunge, a lerp, a percentage of a window, and an ordered Bayer
//  test that crumbles the frontier of a dissolve. Those five lived as `static`
//  functions inside ui/actfx.cpp, which includes render.h and therefore
//  Arduino.h, SO NO HOST BINARY HAS EVER COMPILED OR EXECUTED ONE OF THEM.
//
//  THAT IS THE SAME BUILD FACT THAT COST PHASE 9 A BLINK DRAWN OVER FOUR
//  BODIES, and it is the reason the plan's "reuse the actfx pixel writer" is
//  read here as REUSE THE MATHS AND NOT THE MODULE. ui/actfx.cpp cannot be
//  reused by ui/screen_encounter.cpp in any case: actfx is welded to HOME (its
//  two draw hooks are called only from draw_home(), its props are parked
//  against petfx_body_ink() and it takes petfx_hold()), and SCR_CAPTURE and
//  SCR_ENCOUNTER have none of those. What CAN be shared is this, and sharing it
//  is what puts it inside the test suite for the first time.
//
//  WHAT IS NOT HERE, DELIBERATELY: af_px(), af_blit(), af_px_panel() and
//  af_band_y(). Those write pixels through rd_u8g2() and are clipped to petfx's
//  STAGE; they are the part of actfx that is genuinely about actfx. A screen
//  that wants a dissolving sprite composes ae_dissolve_skip() with the gfx.h
//  seam, which is host-linkable, instead of borrowing a writer that clips to
//  another screen's furniture.
//
//  PURE MODULE: stdint only. No Arduino, no u8g2, no render.h, no gfx.h, no
//  clock of its own, no RNG, no heap, no float, no I/O, no state. Every
//  function is a pure function of its arguments. tools/check.sh fails the build
//  if a device header appears here.
//
//  ARITHMETIC RULES THE CALLERS DEPEND ON, stated once so a test can drive them:
//    * every function is TOTAL - a zero-length window, a t past the end and an
//      inverted range all return a defined value rather than dividing by zero;
//    * every function is MONOTONIC in t where its shape says it should be;
//    * ae_hop() and ae_lunge() are EXACTLY 0 at both ends, so repeated beats
//      cannot accumulate drift - which is the property that makes a film that
//      is interrupted and restarted land in the same place as one that was not.
//      ae_lunge() DID NOT HAVE THIS PROPERTY until P10-C3 and its old home said
//      it did; see the note at the ph == 0 guard in anim_ease.cpp.
//
//  TWO ARITHMETIC FACTS THAT SURPRISE PEOPLE, both pinned by tests/test_anim.cpp
//  rather than quietly corrected, because correcting either would move every
//  existing film by a pixel somewhere in its middle:
//    * ae_lerp() truncates TOWARD a, not toward negative infinity, because C
//      integer division truncates toward zero. A downward lerp therefore lags a
//      rounded one by up to one pixel - ae_lerp(50, 100, 43, 26) is 35, not 34.
//      It is exact at both ends, which is the part that matters.
//    * ae_hop() on a SHORT window is truncated away entirely: ae_hop(1, 3, 1) is
//      4*1*1*2/9 = 0, so a one-pixel hop over three milliseconds never leaves
//      the ground. The peak is EXACT on an even window (4*a*(L/2)^2/L^2 = a) and
//      degrades below it as len*len grows against 4*amp*t*(len-t). Nothing in
//      the firmware hops over a window shorter than AF_PET_HOP_MS (1,150 ms).
//
//  Identifiers and comments: English. This module draws no glyphs.
// =============================================================================
#ifndef PB_UI_ANIM_EASE_H
#define PB_UI_ANIM_EASE_H

#include <stdint.h>

// -----------------------------------------------------------------------------
//  ae_hop - integer parabola, 4*a*t*(len-t)/len^2, peaking at `amp`.
//
//  RETURNED NEGATIVE because up is -y: every hop, bounce and yawn in this
//  firmware uses it as a dy. 0 at t = 0 and at t >= len.
// -----------------------------------------------------------------------------
int16_t ae_hop(uint32_t t, uint32_t len, uint8_t amp);

// -----------------------------------------------------------------------------
//  ae_lunge - OUT, HOLD, BACK: the shape of one bite.
//
//  ae_hop()'s parabola spends most of its length near zero, and at 20 fps a
//  bite is only six or seven frames long, so a parabola gives a bite that never
//  quite arrives - it is sampled on the way out and on the way back and rarely
//  at the extreme. This reaches full extension in the first 35 % of the beat,
//  HOLDS it for the middle 30 % (one or two whole frames with the animal's head
//  in the bowl) and comes back over the last 35 %. Always 0 at ph = 0 and at
//  ph >= len, so bites cannot accumulate drift.
//
//  POSITIVE, unlike ae_hop(): a lunge is a distance the caller signs itself.
// -----------------------------------------------------------------------------
int16_t ae_lunge(uint32_t ph, uint32_t len, uint8_t amp);

// -----------------------------------------------------------------------------
//  ae_lerp - straight line from a to b over [0, len]. EXACTLY b at t >= len.
// -----------------------------------------------------------------------------
int16_t ae_lerp(uint32_t t, uint32_t len, int16_t a, int16_t b);

// -----------------------------------------------------------------------------
//  ae_pct - where t sits inside the window [t0, t1], as 0..100.
//  Saturates at both ends; an inverted or empty window answers 0.
// -----------------------------------------------------------------------------
uint8_t ae_pct(uint32_t t, uint32_t t0, uint32_t t1);

// -----------------------------------------------------------------------------
//  THE DISSOLVE
//
//  A film fades a sprite by drawing FEWER PIXELS, never by drawing a 0: this
//  panel is 1-bit and there is no grey, and a colour-0 overprint erases
//  whatever the sprite was standing on. So a dissolve is a decision, per pixel,
//  about whether to skip it.
//
//  ae_dissolve_front() answers "which sprite row has the frontier reached" for
//  a sprite `h` rows tall at `pct` per cent gone. The frontier travels three
//  rows past the FINISHING end, so pct = 100 means exactly NONE OF IT whatever
//  the height happens to be.
//
//  AND IT IS ASYMMETRIC, WHICH THE ORIGINAL COMMENT IN ui/actfx.cpp CLAIMED IT
//  WAS NOT. It said pct = 0 also means exactly "all of it"; for AE_DIS_DOWN
//  that is true, and FOR AE_DIS_UP IT IS NOT. At pct = 0 the upward frontier
//  sits at row h, and the two-row crumble band that trails it therefore covers
//  rows h-2 and h-1 - so the first frame of an upward dissolve already loses
//  about half the pixels of the sprite's bottom two rows. tests/test_anim.cpp
//  pins that, by name, as behaviour rather than as intent.
//
//  IT IS NOT FIXED, AND THAT IS A DECISION. Starting the upward frontier three
//  rows higher would make the two directions agree and the old claim true, and
//  it would also shift every upward dissolve in ui/actfx.cpp by three rows over
//  its whole range - the poop replicas of ACT_CLEAN are the only caller, in a
//  translation unit no host binary compiles and no golden covers. At one frame
//  of a 300 ms dissolve the artefact is invisible; re-timing an untested
//  shipped film to satisfy a comment, in the last build phase, is the worse
//  trade. The comment was wrong; the code is what ships; both now say so.
//  Callers that need a guaranteed-solid first frame pass AE_DIS_NONE until the
//  percentage leaves zero, which is what ui/screen_encounter.cpp does.
//
//  ae_dissolve_skip() answers, for one pixel, whether this frame draws it. The
//  Bayer test is on ABSOLUTE SCREEN COORDINATES on purpose: a prop that is also
//  sliding would otherwise re-roll its own crumble every frame and shimmer
//  instead of dissolving.
// -----------------------------------------------------------------------------
enum : uint8_t { AE_DIS_NONE = 0, AE_DIS_UP, AE_DIS_DOWN };

// AE_DIS_UP eats the sprite from the bottom upwards as pct goes 0 -> 100;
// AE_DIS_DOWN from the top downwards.
int16_t ae_dissolve_front(uint8_t dir, uint8_t h, uint8_t pct);

// 1 = do not draw this pixel. `row` is the sprite row, `front` the value
// ae_dissolve_front() returned, and (sx, sy) the ABSOLUTE screen position.
uint8_t ae_dissolve_skip(uint8_t dir, uint8_t row, int16_t front,
                         int16_t sx, int16_t sy);

// The 4x4 ordered Bayer threshold at (x, y). 0..15. Exposed because both the
// dissolve and any hand-rolled shading want the same matrix, and two copies of
// a 16-byte table is how two effects come to shimmer against each other.
uint8_t ae_bayer(int16_t x, int16_t y);

#endif  // PB_UI_ANIM_EASE_H
