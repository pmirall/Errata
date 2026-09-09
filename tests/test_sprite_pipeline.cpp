// =============================================================================
//  ERRATA host test - test_sprite_pipeline.cpp
//  THE GENERATED ATLAS, AND THE ONE PROPERTY THAT PROVES THE BIT ORDER (P9-C1).
//
//  tools/gen_sprites.py turns ASCII art into XBM. The single most expensive way
//  for it to be wrong is the BIT ORDER - LSB of a byte is the LEFTMOST pixel -
//  because a mirrored generator produces a header that compiles, asserts
//  clean, sizes correctly, and draws 120 frames backwards. No size check, no
//  budget check and no gate can see it.
//
//  So the pipeline is pinned against art that has been on screen since phase 1.
//  tools/sprites/egg_idle.txt and egg_crack.txt are data/sprites.h's two egg
//  sets transcribed back into ASCII, and the first case below asserts that the
//  288 bytes the generator produces from them are byte-identical to the
//  hand-typed hex. If the shift in pack_frame() were backwards, that case fails
//  on the first row of the first egg.
//
//  WHAT THIS FILE CANNOT ASSERT, said plainly because the rest of the phase
//  depends on believing it: nothing here knows whether a body looks like a
//  creature. It checks rectangles, byte counts, table agreement, ink presence,
//  padding hygiene and that the two frames of a set differ. A 24x24 of noise
//  passes every case in this file. The instrument for the other property is
//  `make -C tests spritetool && ./bin/sprite_dump text <NAME>`, which renders
//  the compiled atlas for a person to look at, and it is a person that has to
//  look.
//
//  IT ALSO CARRIES THE RUNTIME HALF OF THE ATLAS'S SIZE GUARDS.
//  data/sprite_types.h's NT_SPR_SET_FITS is a static_assert, and a static_assert
//  only fires in a build that includes it - the same argument test_content.cpp's
//  banner makes about the generated content tables.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "data/species_table.h"
#include "data/sprites.h"
#include "data/sprites_bugs.h"
#include "ui/petfx_core.h"

// The decode, written out once. Same rule as data/sprite_types.h, same rule as
// u8g2's drawXBM and the host fake's gfx_xbm: row stride ((w+7)>>3) bytes, byte
// n of a row carries pixels 8n..8n+7, LSB is the LEFTMOST pixel.
static int pixel_at(const SpriteSet& s, int frame, int x, int y) {
  const int stride = ((int)s.w + 7) >> 3;
  const uint8_t b  = s.bits[stride * (int)s.h * frame + y * stride + (x >> 3)];
  return (b >> (x & 7)) & 1;
}

// =============================================================================
//  1. THE BIT ORDER, PINNED AGAINST SHIPPED ART
// =============================================================================
TEST(the_generated_eggs_are_the_legacy_eggs_byte_for_byte) {
  // THE LEGACY ARRAYS THIS USED TO COMPARE AGAINST ARE GONE. P9-C3 deleted
  // data/sprites.h's 38 hand-typed sets, the two eggs included, so this case
  // could no longer read `spr_egg_idle`. Deleting it with them would have
  // thrown away the ONE property that proves the bit order against art nobody
  // generated, so the twelve bytes it depended on are transcribed here instead,
  // verbatim from data/sprites.h at 88b4499 - the last commit that had them.
  //
  // These are rows 0..3 of EGG_IDLE frame 0 and rows 0..3 of EGG_CRACK frame 0,
  // as the device drew them from phase 1 to P9-C3. If pack_frame()'s shift were
  // reversed, this case fails on the first non-palindromic byte.
  static const uint8_t kEggIdleHead[12] = {
    0x00, 0x00, 0x00,  0x00, 0x7E, 0x00,  0x00, 0xFF, 0x00,  0x80, 0xFF, 0x01,
  };
  static const uint8_t kEggCrackHead[12] = {
    0x00, 0x00, 0x00,  0x00, 0x7E, 0x00,  0x00, 0xE7, 0x00,  0x80, 0xE7, 0x03,
  };
  CHECK_EQ(sizeof bug_spr_egg_idle,  144u);
  CHECK_EQ(sizeof bug_spr_egg_crack, 144u);
  CHECK_EQ(memcmp(bug_spr_egg_idle,  kEggIdleHead,  sizeof kEggIdleHead),  0);
  CHECK_EQ(memcmp(bug_spr_egg_crack, kEggCrackHead, sizeof kEggCrackHead), 0);

  // ...and the table rows, so a set that happened to hold the right bytes under
  // the wrong dimensions is not mistaken for a pass.
  CHECK_EQ(ER_SPRITE_SETS[BUGSPR_EGG_IDLE].w,       24);
  CHECK_EQ(ER_SPRITE_SETS[BUGSPR_EGG_IDLE].h,       24);
  CHECK_EQ(ER_SPRITE_SETS[BUGSPR_EGG_IDLE].frames,   2);
  CHECK_EQ(ER_SPRITE_SETS[BUGSPR_EGG_CRACK].w,      24);
  CHECK_EQ(ER_SPRITE_SETS[BUGSPR_EGG_CRACK].h,      24);
  CHECK_EQ(ER_SPRITE_SETS[BUGSPR_EGG_CRACK].frames,  2);

  // The egg is still slot 0, which sprite_set() falls back to for any
  // out-of-range id. Moving it is how every bad id starts drawing a creature.
  CHECK_EQ((int)BUGSPR_EGG_IDLE, 0);
  CHECK_EQ(sprite_set(255).bits, ER_SPRITE_SETS[BUGSPR_EGG_IDLE].bits);
}

// A pixel this test can point at BY COORDINATE, so the case above cannot pass
// by comparing two arrays that are both wrong in the same way. Row 1 of
// EGG_IDLE frame 0 is the six-pixel crown of the shell at x 9..14: the LSB rule
// says byte 1 of that row is 0x7E, and a big-endian-per-byte packer would write
// 0x7E reversed (0x7E is a palindrome under bit reversal - hence the SECOND
// row, whose 0x80,0xFF,0x01 is not).
TEST(the_leftmost_pixel_of_a_row_is_the_low_bit_of_its_first_byte) {
  const SpriteSet& e = ER_SPRITE_SETS[BUGSPR_EGG_IDLE];
  // Row 1: pixels 9..14 lit, 0..8 and 15..23 clear.
  for (int x = 0; x < 24; ++x)
    CHECK_EQ(pixel_at(e, 0, x, 1), (x >= 9 && x <= 14) ? 1 : 0);
  // Row 3: 0x80,0xFF,0x01 - pixel 7 lit, 8..15 lit, 16 lit, nothing else.
  for (int x = 0; x < 24; ++x)
    CHECK_EQ(pixel_at(e, 0, x, 3), (x >= 7 && x <= 16) ? 1 : 0);
  // The raw bytes those two rows must be, stated rather than derived, so a
  // change to pixel_at() cannot make this case agree with itself.
  CHECK_EQ(bug_spr_egg_idle[3 * 1 + 0], 0x00);
  CHECK_EQ(bug_spr_egg_idle[3 * 1 + 1], 0x7E);
  CHECK_EQ(bug_spr_egg_idle[3 * 1 + 2], 0x00);
  CHECK_EQ(bug_spr_egg_idle[3 * 3 + 0], 0x80);
  CHECK_EQ(bug_spr_egg_idle[3 * 3 + 1], 0xFF);
  CHECK_EQ(bug_spr_egg_idle[3 * 3 + 2], 0x01);
}

// =============================================================================
//  2. THE GENERATED ATLAS IS WELL FORMED
// =============================================================================
TEST(every_generated_row_matches_the_array_it_points_at) {
  CHECK(ER_SPRITE_SET_COUNT >= 1);
  unsigned total = 0;
  for (int i = 0; i < (int)ER_SPRITE_SET_COUNT; ++i) {
    const SpriteSet& s = ER_SPRITE_SETS[i];
    CHECK(s.bits != nullptr);
    CHECK(s.w >= 1 && s.h >= 1);
    CHECK(s.w <= 40 && s.h <= 40);      // PF_MAX_W/H, XBM_MIRROR_MAX_W
    CHECK(s.frames >= 1);               // sprite_frame() does `frame % frames`
    CHECK(ER_SPRITE_NAMES[i] != nullptr && ER_SPRITE_NAMES[i][0] != '\0');
    total += spr_set_bytes(s);
  }
  // The generator's own arithmetic, the compiler's, and this test's all agree.
  CHECK_EQ(total, (unsigned)ER_SPRITE_DATA_BYTES);
  CHECK_EQ(total, (unsigned)ER_SPRITE_DATA_BYTES_DECLARED);
}

TEST(every_generated_frame_has_ink_and_no_stray_padding_bits) {
  for (int i = 0; i < (int)ER_SPRITE_SET_COUNT; ++i) {
    const SpriteSet& s = ER_SPRITE_SETS[i];
    const int stride = ((int)s.w + 7) >> 3;
    const int pad    = stride * 8 - (int)s.w;
    const unsigned mask = pad ? (unsigned)((0xFFu << (8 - pad)) & 0xFFu) : 0u;
    for (int f = 0; f < (int)s.frames; ++f) {
      int ink = 0;
      for (int y = 0; y < (int)s.h; ++y) {
        for (int x = 0; x < (int)s.w; ++x) ink += pixel_at(s, f, x, y);
        // The bits past the right edge of a row are not drawn by drawXBM, but
        // they ARE copied by ui/xbm_mirror.cpp and they ARE hashed by anything
        // that digests the art, so a generator that left junk there would
        // produce two different headers for one picture.
        if (mask) {
          const unsigned last = s.bits[stride * (int)s.h * f + y * stride + stride - 1];
          CHECK_EQ(last & mask, 0u);
        }
      }
      CHECK(ink > 0);                   // a blank frame makes the pet vanish
    }
  }

  // THE PADDING HALF ABOVE IS DEAD CODE ON THIS ATLAS AND SAYING SO IS THE
  // POINT (P9-C6). Every one of the 64 generated sets is 24x24, so stride is 3,
  // pad is 0, mask is 0 and the `if (mask)` body never executes: the case's name
  // claims two properties and the input can only exercise one. The emote sets in
  // data/sprites.h are 5x7, 6x8, 12x10 and 14x8 - they carry REAL padding bits,
  // they are the shape this check was written for, and they go through the same
  // xbm_mirror and the same art hash. So the padding half is given something to
  // check rather than left latent under a name that implies it ran.
  int padded_rows = 0;
  for (int i = 0; i < (int)EMO_COUNT; ++i) {
    const SpriteRef& e = SPRITE_EMOTES[i];
    const int stride = ((int)e.w + 7) >> 3;
    const int pad    = stride * 8 - (int)e.w;
    if (!pad) continue;
    const unsigned mask = (unsigned)((0xFFu << (8 - pad)) & 0xFFu);
    for (int y = 0; y < (int)e.h; ++y) {
      ++padded_rows;
      CHECK_EQ(e.bits[y * stride + stride - 1] & mask, 0u);
    }
  }
  // ...and the walk is not vacuous: some emote really does have a padded row.
  CHECK(padded_rows > 0);
}

// The generator WARNS about this on every run; here it is fatal, because a warning
// on a 120-frame art drop is a warning nobody reads. If a set is ever
// deliberately still, name it in the exception below rather than deleting the
// case - "this one is meant to be" is a fact worth writing down.
TEST(a_two_frame_set_actually_animates) {
  for (int i = 0; i < (int)ER_SPRITE_SET_COUNT; ++i) {
    const SpriteSet& s = ER_SPRITE_SETS[i];
    if (s.frames < 2) continue;
    // (no deliberate exceptions today)
    int diff = 0;
    for (int y = 0; y < (int)s.h; ++y)
      for (int x = 0; x < (int)s.w; ++x)
        if (pixel_at(s, 0, x, y) != pixel_at(s, 1, x, y)) ++diff;
    if (diff == 0) {
      // Named, so the failure line says WHICH set was copy-pasted.
      nt_fail_at(__FILE__, __LINE__, ER_SPRITE_NAMES[i]);
    }
    CHECK(diff > 0);
  }
}

// THE IDLE HAS A SIZE AND A SHAPE, NOT JUST A NONZERO DIFFERENCE (P9-C6).
//
// `diff > 0` above was the ONLY thing this repository said about the idle
// animation, and the P9 exit review measured what that permits: amplitude across
// the shipped roster varied 22x with no floor and no ceiling. At the bottom,
// KLONIX's entire idle was SIX PIXELS ON ONE ROW - a 7px bar swapping sides in a
// 187 px body, 3.2 % of its ink, a diff box 1.6 % of its body box - which on a
// mono panel reads as a display fault rather than as a creature breathing, and
// which passed `diff > 0` with room to spare. At the top, JITERA changed 70 of
// its 109 px, the two body halves shearing 3 px in opposite directions, which
// reads as horizontal tearing; three other files in the roster explicitly refuse
// that for themselves ("the jump moves 96 px and reads as a second drawing") so
// the roster contained both the failure and its own written cure.
//
// FOUR BODIES WERE EDITED RATHER THAN EXEMPTED: KLONIX (its crest now leans, 6 ->
// 16 px over 4 rows), MEMORO (2.9 % -> 5.7 %), MURAX (3.3 % -> 7.4 %) and JITERA
// (64.2 % -> 27.5 %, by halving both jitter offsets to 1 px, which is the change
// jitera.txt's own header offered a reviewer in as many words).
//
// WHAT THE BOUNDS ARE AND WHERE THEY COME FROM. They are MEASURED, not chosen:
// after those four edits the roster runs from 4.3 % (EXPLOID, SICK) to 41.5 %
// (ROOTKAR, which the review called the best idle in the set - the whole body
// drops 1 px while the legs splay). So 4 % and 45 % are the measured range with a
// small margin. Re-measure them if the art changes; do not widen one to make a
// build pass.
//
// AND WHAT THEY DO NOT SAY, because a bound that is quietly weaker than its name
// is this project's recurring defect. `>= 2 rows and >= 2 columns` is a floor on
// SHAPE that catches exactly the KLONIX failure (one row) and nothing subtler. A
// 3-row rule would fail six bodies today - EXPLOID, GATEON, NULIX, BEAKON,
// PROBIX and BUGGO, each of which moves one 4-8 px feature between two rows - and
// for family 20 it would fight the design: cookit.txt's whole idea is that the
// PUPIL is the only moving part. Six exceptions would make the rule a list. It is
// written down here as owed instead, with the numbers, so the next art pass can
// decide it rather than rediscover it.
TEST(the_idle_animation_has_an_amplitude_and_a_spread) {
  int lo = 1000, hi = 0;
  for (int i = 0; i < (int)ER_SPRITE_SET_COUNT; ++i) {
    const SpriteSet& s = ER_SPRITE_SETS[i];
    if (s.frames < 2) continue;
    int diff = 0, ink = 0;
    bool row_hit[24] = {false}, col_hit[24] = {false};
    for (int y = 0; y < (int)s.h; ++y)
      for (int x = 0; x < (int)s.w; ++x) {
        ink += pixel_at(s, 0, x, y);
        if (pixel_at(s, 0, x, y) != pixel_at(s, 1, x, y)) {
          ++diff;
          row_hit[y] = true;
          col_hit[x] = true;
        }
      }
    int rows = 0, cols = 0;
    for (int k = 0; k < 24; ++k) { rows += row_hit[k]; cols += col_hit[k]; }

    const int pct = (100 * diff) / (ink ? ink : 1);
    if (pct < 4 || pct > 45) nt_fail_at(__FILE__, __LINE__, ER_SPRITE_NAMES[i]);
    CHECK(pct >= 4);
    CHECK(pct <= 45);
    // An absolute floor as well as a relative one: 4 % of a 300 px body is 12 px,
    // but 4 % of a 79 px body is 3, and three pixels is not an animation.
    if (diff < 6) nt_fail_at(__FILE__, __LINE__, ER_SPRITE_NAMES[i]);
    CHECK(diff >= 6);
    // THE SHAPE. One row of pixels flickering is the defect this half exists for.
    if (rows < 2 || cols < 2) nt_fail_at(__FILE__, __LINE__, ER_SPRITE_NAMES[i]);
    CHECK(rows >= 2);
    CHECK(cols >= 2);

    if (pct < lo) lo = pct;
    if (pct > hi) hi = pct;
  }
  // The measured range, pinned so a change to the roster's spread is a visible
  // fact in the diff rather than a silent drift inside the band.
  CHECK_EQ(lo, 4);
  CHECK_EQ(hi, 41);
}

// =============================================================================
//  3. THE SLOT CONTRACT P9-C3 INHERITS
// =============================================================================
// The species bodies are the TAIL of the atlas and a body's slot is
// ER_SPRITE_BODY_FIRST + (species_id - 1) - the pack's `sprite_id == id - 1`
// invariant expressed in the atlas. tools/gen_sprites.py refuses a source tree
// that breaks it; this is the same statement over the emitted header, which is
// what the firmware will actually index.
TEST(the_species_block_is_the_tail_of_the_generated_atlas) {
  CHECK_EQ((int)ER_SPRITE_BODY_FIRST,
           (int)ER_SPRITE_SET_COUNT - (int)ER_SPRITE_BODY_COUNT);
  CHECK(ER_SPRITE_BODY_COUNT <= ER_SPRITE_SET_COUNT);

  // THE LOOP WAS EMPTY AT P9-C1 AND THE COUNT WAS ASSERTED TO BE 0 so that
  // "no bodies yet" was a recorded fact rather than a vacuous loop nobody
  // noticed. P9-C3 drew them; the assertion moved with the art rather than
  // being deleted, and 60 is now the number that has to hold.
  CHECK_EQ((int)ER_SPRITE_BODY_COUNT, 60);
  for (int k = 0; k < (int)ER_SPRITE_BODY_COUNT; ++k) {
    const SpriteSet& s = ER_SPRITE_SETS[ER_SPRITE_BODY_FIRST + k];
    CHECK_EQ(s.w, 24);
    CHECK_EQ(s.h, 24);
    CHECK_EQ(s.frames, 2);
  }
  // A body may never be bound to a species the roster does not ship.
  CHECK(ER_SPRITE_BODY_COUNT <= SPECIES_PACK_COUNT);
  // And the four fixed slots are the four this build's lookup names. A fifth
  // effect set appended in the WRONG place would push every species body one
  // slot along and every one of them would draw its neighbour.
  CHECK_EQ((int)ER_SPRITE_BODY_FIRST, 4);
  CHECK_EQ((int)BUGSPR_EGG_IDLE,  0);
  CHECK_EQ((int)BUGSPR_EGG_CRACK, 1);
  CHECK_EQ((int)BUGSPR_SLEEP,     2);
  CHECK_EQ((int)BUGSPR_SICK,      3);
}

// EVERY SPECIES BODY IS A DIFFERENT PICTURE, not merely a different pointer.
//
// The distinctness check in every_legacy_row_matches_the_array_it_points_at
// compared POINTERS, which catches a table row aimed at its neighbour's array
// and nothing else. Sixty bodies drawn by five different hands can collide in a
// way pointers cannot see: two files with the same pixels. This compares the
// bytes, both frames, all 1,770 pairs.
TEST(no_two_species_bodies_hold_the_same_pixels) {
  for (int a = 0; a < (int)ER_SPRITE_BODY_COUNT; ++a)
    for (int b = a + 1; b < (int)ER_SPRITE_BODY_COUNT; ++b) {
      const SpriteSet& x = ER_SPRITE_SETS[ER_SPRITE_BODY_FIRST + a];
      const SpriteSet& y = ER_SPRITE_SETS[ER_SPRITE_BODY_FIRST + b];
      if (x.bits == y.bits) {
        nt_fail_at(__FILE__, __LINE__, ER_SPRITE_NAMES[ER_SPRITE_BODY_FIRST + a]);
        continue;
      }
      if (memcmp(x.bits, y.bits, spr_set_bytes(x)) == 0)
        nt_fail_at(__FILE__, __LINE__, ER_SPRITE_NAMES[ER_SPRITE_BODY_FIRST + b]);
    }
}

// EVERY BODY STANDS ON THE FLOOR. ui/screen_home.cpp draws a body at
// `y = HOME_FLOOR_Y - r.h`, so its LAST ROW is the floor rule. A body whose ink
// stops short of its own last row hovers, and no byte check can see it: the
// grid is still 24x24, the frames still differ, the bytes still match the table.
// tools/gen_sprites.py --self-check prints "N blank row(s) under the body" for a
// human; this is the same statement, over the compiled atlas, as a failure.
//
// TWO NAMED EXCEPTIONS, AND THEY ARE NAMED RATHER THAN SKIPPED BY A RANGE.
// The eggs carry ONE blank row under them, and that is the art the device has
// drawn since phase 1, byte for byte - P9-C1 transcribed it and asserted the
// bytes rather than re-drawing it, so changing it here would be re-drawing
// shipped art inside a test's exception list. An egg has no feet: ui/ceremony.
// cpp and ui/screen_evolution.cpp draw it through sprite_center_y(), centred in
// the sprite band, and only HOME's still path puts it on the floor rule - one
// pixel high, which is where it has always been. Listing the two by NAME means
// a sixty-first body that hovers cannot join them by accident.
TEST(every_body_has_ink_on_its_last_row) {
  for (int i = 0; i < (int)ER_SPRITE_SET_COUNT; ++i) {
    const SpriteSet& s = ER_SPRITE_SETS[i];
    if (i == (int)BUGSPR_EGG_IDLE || i == (int)BUGSPR_EGG_CRACK) continue;
    for (int f = 0; f < (int)s.frames; ++f) {
      int ink_on_last = 0;
      for (int x = 0; x < (int)s.w; ++x)
        ink_on_last += pixel_at(s, f, x, (int)s.h - 1);
      if (ink_on_last == 0) nt_fail_at(__FILE__, __LINE__, ER_SPRITE_NAMES[i]);
      CHECK(ink_on_last > 0);
    }
  }
  // The exception is BOUNDED: the eggs hover by exactly one row, not by
  // whatever they like, so a re-drawn egg that floated four pixels above the
  // floor would fail here rather than inherit the exemption.
  for (int i = 0; i < 2; ++i) {
    const SpriteSet& s = ER_SPRITE_SETS[i];
    for (int f = 0; f < (int)s.frames; ++f) {
      int last_inked = -1;
      for (int y = 0; y < (int)s.h; ++y)
        for (int x = 0; x < (int)s.w; ++x)
          if (pixel_at(s, f, x, y)) { last_inked = y; break; }
      CHECK_EQ(last_inked, (int)s.h - 2);
    }
  }
}

// THE EYE BANDS ARE INSIDE THE BODIES THEY INDEX, at runtime as well as at
// compile time. data/sprites_bugs.h static_asserts it; this is the same rule
// as a test, for the same reason section 5 below exists - and it additionally
// checks the property the static_assert cannot express cheaply: a band that
// says a body blinks must actually contain an unlit pixel to close.
//
// THE COUNT IS PINNED AT P9-C6 AND THAT IS THE POINT OF THIS EDIT. It used to
// read `CHECK(blinkers > ER_SPRITE_SET_COUNT)` against a value of 118, i.e. 53
// of the 128 bands could be dropped ONE AT A TIME with the suite green - the
// aggregate-mask shape the phase-8 review was caught with, and the exit review
// demonstrated it: moving the derivation's cut from 60 % to 40 % took 26 bodies'
// blinks away and printed ALL PASS 51/51 and GATE OK. An equality cannot do
// that. It is a RECORDED FACT, not a rule: if you change the art or the
// derivation, run `python3 tools/gen_sprites.py --self-check tools/sprites/*.txt`,
// count the frames that report eyes, and re-record it here in the same commit.
TEST(every_eye_band_lies_inside_its_body_and_has_something_to_close) {
  int blinkers = 0;
  for (int i = 0; i < (int)ER_SPRITE_SET_COUNT; ++i) {
    const SpriteSet& s = ER_SPRITE_SETS[i];
    for (int f = 0; f < 2; ++f) {
      const SpriteEyeBand e = sprite_eyes((uint8_t)i, (uint8_t)f);
      if (e.y1 < e.y0) continue;        // "does not blink"
      ++blinkers;
      CHECK(e.y1 < s.h);
      CHECK(e.x1 < s.w);
      CHECK(e.x0 <= e.x1);
      // The device truncates a band taller than PF_EYE_MAX_H WITHOUT SAYING SO
      // (ui/petfx_core.cpp: `if (bh > PF_EYE_MAX_H) bh = PF_EYE_MAX_H;`), which
      // would silently blink half a socket. The generator's own EYE_MAX_H is 8;
      // this is the other end of that agreement.
      if ((int)e.y1 - (int)e.y0 + 1 > PF_EYE_MAX_H)
        nt_fail_at(__FILE__, __LINE__, ER_SPRITE_NAMES[i]);
      CHECK((int)e.y1 - (int)e.y0 + 1 <= PF_EYE_MAX_H);
      int holes = 0;
      for (int y = e.y0; y <= (int)e.y1; ++y)
        for (int x = e.x0; x <= (int)e.x1; ++x)
          if (!pixel_at(s, f, x, y)) ++holes;
      if (holes == 0) nt_fail_at(__FILE__, __LINE__, ER_SPRITE_NAMES[i]);
      CHECK(holes > 0);
    }
  }
  CHECK_EQ(blinkers, 121);
}

// =============================================================================
//  3b. THE BLINK ITSELF - THE FRAME NOTHING IN THIS TREE HAD EVER DRAWN
//
//  WHY THIS CASE EXISTS. The atlas holds two authored frames per set. The frame
//  the player sees while a pet blinks is in NEITHER of them: it is composited at
//  draw time from the art plus pf_build_lids() plus one eye band. Until P9-C6
//  pf_build_lids() lived in ui/petfx.cpp, which includes render.h -> Arduino.h,
//  so no host binary could link it and no test could draw that frame. The P9
//  exit review composited it by hand in Python and found four bodies whose
//  "blink" filled a third of the creature solid - DENYRA +79 px on 283, BLAKLIX
//  +78 on 264, MURAX +68 on 242, PANOPTIX losing all nine eyes it is named for -
//  while every byte check, every golden and the gate stayed green.
//
//  THE PROPERTY, stated so it is clear what it does and does not say:
//    A BLINK MAY CLOSE HOLES. IT MAY NOT DRAW OVER THE BODY.
//  Every pixel pf_build_lids() fills must be a pixel the background cannot
//  reach - an enclosed hole of that frame's own drawing. That is computed HERE,
//  from the pixels, by a flood fill; it is not read back from the generator, so
//  a generator that emitted a nonsense band cannot satisfy it by agreeing with
//  itself.
//
//  WHAT IT STILL CANNOT SAY: that the hole being closed is an EYE. A body whose
//  only small hole is a porthole blinks with the porthole and passes here. That
//  needs a person and `./bin/sprite_dump blink NAME` is what they look at.
// =============================================================================

// Unlit pixels the background can reach, 4-connected from the border. Anything
// unlit and unreached is an enclosed hole.
static void reachable_gaps(const SpriteSet& s, int frame, bool* out /*w*h*/) {
  const int w = s.w, h = s.h;
  for (int k = 0; k < w * h; ++k) out[k] = false;
  int stack[24 * 24 * 2];
  int sp = 0;
  const int edge_y[2] = {0, h - 1};
  const int edge_x[2] = {0, w - 1};
  for (int x = 0; x < w; ++x)
    for (int k = 0; k < 2; ++k) {
      const int y = edge_y[k];
      if (!pixel_at(s, frame, x, y) && !out[y * w + x]) { out[y * w + x] = true; stack[sp++] = y * w + x; }
    }
  for (int y = 0; y < h; ++y)
    for (int k = 0; k < 2; ++k) {
      const int x = edge_x[k];
      if (!pixel_at(s, frame, x, y) && !out[y * w + x]) { out[y * w + x] = true; stack[sp++] = y * w + x; }
    }
  while (sp > 0) {
    const int c = stack[--sp], cx = c % w, cy = c / w;
    const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    for (int d = 0; d < 4; ++d) {
      const int nx = cx + dx[d], ny = cy + dy[d];
      if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
      if (pixel_at(s, frame, nx, ny) || out[ny * w + nx]) continue;
      out[ny * w + nx] = true;
      stack[sp++] = ny * w + nx;
    }
  }
}

// Runs the SHIPPED pf_build_lids() for one (set, frame) with one band and
// reports how many pixels it fills and how many of those are not holes.
struct BlinkResult { int rows; int filled; int over_body; int max_x; int changed; };
static BlinkResult blink_of(const SpriteSet& s, int frame, SpriteEyeBand e) {
  uint8_t fill[PF_STRIP_BYTES], lid[PF_STRIP_BYTES];
  memset(fill, 0xAA, sizeof fill);
  memset(lid,  0xAA, sizeof lid);
  const int stride = ((int)s.w + 7) >> 3;
  const uint8_t* bits = s.bits + (ptrdiff_t)stride * s.h * frame;
  BlinkResult r{0, 0, 0, -1, 0};
  r.rows = (int)pf_build_lids(bits, s.w, s.h, e.y0, e.y1, e.x0, e.x1, fill, lid);
  if (r.rows == 0) return r;
  bool reach[24 * 24];
  reachable_gaps(s, frame, reach);
  for (int row = 0; row < r.rows; ++row)
    for (int x = 0; x < (int)s.w; ++x) {
      const int y   = (int)e.y0 + row;
      const int fbit = (fill[row * stride + (x >> 3)] >> (x & 7)) & 1;
      const int lbit = (lid [row * stride + (x >> 3)] >> (x & 7)) & 1;
      // What the panel ends up with: the body, then the fill at colour 1, then
      // the lash at colour 0. render.h's drawXBM order, in two bits.
      const int was = pixel_at(s, frame, x, y);
      const int now = lbit ? 0 : (fbit ? 1 : was);
      if (was != now) ++r.changed;
      if (!fbit) continue;
      ++r.filled;
      if (x > r.max_x) r.max_x = x;
      if (was || reach[y * (int)s.w + x]) ++r.over_body;
    }
  return r;
}

TEST(the_blink_closes_holes_and_never_draws_over_the_body) {
  int worst_pct = 0, blinkers = 0;
  for (int i = 0; i < (int)ER_SPRITE_SET_COUNT; ++i) {
    const SpriteSet& s = ER_SPRITE_SETS[i];
    for (int f = 0; f < (int)s.frames; ++f) {
      const SpriteEyeBand e = sprite_eyes((uint8_t)i, (uint8_t)f);
      const BlinkResult r = blink_of(s, f, e);
      if (e.y1 < e.y0) {
        // "does not blink" must actually not blink - the band and the fill are
        // two different mechanisms and this is where they have to agree.
        CHECK_EQ(r.rows, 0);
        continue;
      }
      ++blinkers;
      // A band that claims to blink and fills nothing is a lie the old
      // assertion could not tell from a blink.
      if (r.filled == 0) nt_fail_at(__FILE__, __LINE__, ER_SPRITE_NAMES[i]);
      CHECK(r.filled > 0);
      // THE PROPERTY.
      if (r.over_body != 0) nt_fail_at(__FILE__, __LINE__, ER_SPRITE_NAMES[i]);
      CHECK_EQ(r.over_body, 0);
      // Nothing outside the sprite, ever.
      CHECK(r.max_x < (int)s.w);
      CHECK_EQ(r.rows, (int)e.y1 - (int)e.y0 + 1);

      int ink = 0;
      for (int y = 0; y < (int)s.h; ++y)
        for (int x = 0; x < (int)s.w; ++x) ink += pixel_at(s, f, x, y);
      const int pct = (100 * r.changed) / (ink ? ink : 1);
      if (pct > worst_pct) worst_pct = pct;
    }
  }
  CHECK_EQ(blinkers, 121);
  // AND A BOUND ON HOW MUCH OF THE CREATURE A BLINK IS ALLOWED TO BE, measured
  // on the COMPOSITED frame - body, then fill, then lash - because that is the
  // picture the panel shows. Closing a socket that IS the face is the biggest
  // legitimate blink in the roster: FLIPIX 29 %, BEAKON 29 %, TWINIX 23 %. It is
  // a MEASURED ceiling, so re-measure it if the art changes; do not raise it to
  // make a build pass.
  //
  // THERE IS NO FLOOR AND I AM NOT PRETENDING OTHERWISE. LEKRON's blink changes
  // 2 px of a 303 px body and is invisible at 1x; a body whose only small hole
  // is 3 px gets a 3 px blink. That is a judgement about whether an animation
  // reads, which is the class of question no assertion in this file can settle -
  // `./bin/sprite_dump blink LEKRON` is the instrument and it needs a person.
  CHECK(worst_pct <= 30);
}

// THE CONTROL. Every case above would also pass if blink_of() could not see an
// over-fill at all - which is exactly the shape of a test that cannot fail, and
// the roster passes it by construction. So hand the same instrument a band that
// MUST over-fill (the whole upper half of a body, which is what the pre-P9-C6
// derivation produced for DENYRA) and require it to say so, by name.
TEST(the_over_fill_recorder_would_see_a_blink_that_swallowed_the_body) {
  int caught = 0, tried = 0;
  for (int i = (int)ER_SPRITE_BODY_FIRST; i < (int)ER_SPRITE_SET_COUNT; ++i) {
    const SpriteSet& s = ER_SPRITE_SETS[i];
    // rows 3..14 across the full width: the shape of the band the old rule
    // emitted for DENYRA ({3, 14, 0, 23}).
    SpriteEyeBand wide{3, 14, 0, (uint8_t)(s.w - 1)};
    const BlinkResult r = blink_of(s, 0, wide);
    if (r.rows == 0) continue;          // no interior run up there at all
    ++tried;
    if (r.over_body > 0) ++caught;
  }
  // Not every body has a fillable gap in that band, so this is a rate, not a
  // sweep - but it must be most of the roster, or the recorder is asleep.
  CHECK(tried >= 30);
  CHECK(caught >= 20);
  // And the one the review actually measured, by name.
  {
    const SpriteSet& d = ER_SPRITE_SETS[BUGSPR_DENYRA];
    const BlinkResult r = blink_of(d, 0, SpriteEyeBand{3, 14, 0, 23});
    CHECK(r.over_body > 40);
  }
}

// =============================================================================
//  4. THE BUDGET, NOW AT ITS END STATE
//
//  This section used to check that BOTH atlases fitted a 24,576 B TRANSITION
//  allowance, which was widened for exactly the window in which the new art
//  arrives before the old art can safely go. That window closed in this chunk:
//  there is one atlas. What is checked now is the end state and the margin,
//  from the test side, so the arithmetic in data/sprites.h's banner is verified
//  by something that runs rather than by a reader.
// =============================================================================
TEST(the_atlas_is_at_its_end_state_and_the_transition_allowance_is_gone) {
  // 2 eggs + SLEEP + SICK + 60 bodies, every one of them 24x24x2 at 144 B.
  CHECK_EQ((unsigned)ER_SPRITE_DATA_BYTES, 64u * 144u);
  CHECK_EQ((unsigned)ER_SPRITE_DATA_BYTES, 9216u);
  CHECK_EQ((unsigned)ER_SPRITE_DATA_BYTES,
           (unsigned)ER_SPRITE_DATA_BYTES_DECLARED);

  // The survivors data/sprites.h still owns, ASSERTED TERM BY TERM (P9-C6).
  // This used to be one CHECK_EQ on the SUM, sitting under a comment that said
  // "icons 384 + mini 96 + badges 312 + the twelve emotes 239". Two of those
  // four numbers were wrong - spr_mini8 is 21 icons x 8 B = 168, and the emotes
  // are 167 - by +72 and -72, so they cancelled and the aggregate assertion
  // could not see either. That is the phase-8 shape exactly: an assertion next
  // to the wrong numbers, checking only their total. Whoever next tries to shave
  // the survivors to make room for art would have sized two arrays wrong in
  // opposite directions.
  unsigned emotes = 0;
  for (unsigned i = 0; i < (unsigned)EMO_COUNT; ++i)
    emotes += spr_xbm_bytes(SPRITE_EMOTES[i].w, SPRITE_EMOTES[i].h);
  CHECK_EQ((unsigned)sizeof(spr_icon12),  384u);
  CHECK_EQ((unsigned)sizeof(spr_mini8),   168u);
  CHECK_EQ((unsigned)sizeof(spr_badge12), 312u);
  CHECK_EQ(emotes,                        167u);
  const unsigned survivors = (unsigned)SPRITE_DATA_BYTES
                           - (unsigned)ER_SPRITE_DATA_BYTES;
  CHECK_EQ(survivors, 1031u);
  CHECK_EQ(survivors, 384u + 168u + 312u + 167u);

  CHECK_EQ((unsigned)SPRITE_DATA_BYTES, 10247u);
  CHECK_EQ((unsigned)SPRITE_DATA_BYTES, (unsigned)SPRITE_DATA_BYTES_DECLARED);

  // THE CEILING IS THE END STATE PLUS A STATED MARGIN, and the margin is
  // asserted rather than described: 1,017 B, which is seven more 24x24x2 sets
  // (1,008 B) - one more three-stage family and four effect sets.
  CHECK_EQ((unsigned)SPRITE_DATA_BYTES_MAX, 11264u);
  CHECK_EQ((unsigned)SPRITE_DATA_BYTES_MAX - (unsigned)SPRITE_DATA_BYTES, 1017u);
  CHECK(1017u >= 7u * 144u);
  // And the transition allowance is not merely unused, it is gone. A 24,576 B
  // ceiling would swallow the next 14,000 B of art without a word.
  CHECK(SPRITE_DATA_BYTES_MAX < 24576u);
}

// =============================================================================
//  5. THE RUNTIME HALF OF THE ATLAS'S COMPILE-TIME GUARDS
//
//  NT_SPR_SET_FITS is a static_assert and fires only where it is included. This
//  is the same rule as a test, so a header that stops being included anywhere
//  cannot take its guard with it silently.
// =============================================================================
TEST(every_row_matches_the_array_it_points_at) {
  unsigned total = 0;
  for (int i = 0; i < (int)ER_SPRITE_SET_COUNT; ++i) {
    const SpriteSet& s = ER_SPRITE_SETS[i];
    CHECK(s.bits != nullptr);
    CHECK(s.frames >= 1);
    CHECK(s.w >= 1 && s.h >= 1 && s.w <= 40 && s.h <= 40);
    total += spr_set_bytes(s);
    // Distinctness: two rows pointing at ONE array is how a set silently
    // becomes a copy of its neighbour.
    for (int j = i + 1; j < (int)ER_SPRITE_SET_COUNT; ++j)
      CHECK(ER_SPRITE_SETS[i].bits != ER_SPRITE_SETS[j].bits);
  }
  CHECK_EQ(total, 9216u);
  CHECK_EQ((unsigned)SPRITE_DATA_BYTES, 10247u);
}

// =============================================================================
//  6. THE LOOKUP THE FIRMWARE RUNS
//
//  sprite_set_id() is a constexpr in a header, so nothing links it and nothing
//  would notice it rotting. These are its three branches and its clamp, stated
//  as inputs and outputs rather than as an inspection of the source.
// =============================================================================
TEST(the_lookup_answers_the_body_the_species_asked_for) {
  // EGG first, at every pose: an egg is an egg, and it must not fall through to
  // a pose set - a cracking egg that goes to sleep is not a picture this
  // product has.
  for (uint8_t pose = 0; pose < (uint8_t)POSE_COUNT; ++pose)
    CHECK_EQ(sprite_set_id((uint8_t)STAGE_EGG, 7u, pose), (uint8_t)BUGSPR_EGG_IDLE);

  // Then the body, at every stage above EGG. The stage no longer changes it,
  // and that is the deletion this chunk made - so it is asserted, not assumed.
  for (uint8_t st = STAGE_BABY; st <= STAGE_SENIOR; ++st)
    for (uint8_t key = 0; key < (uint8_t)ER_SPRITE_BODY_COUNT; ++key)
      CHECK_EQ(sprite_set_id(st, sprite_form_of(key, (Stage)st), (uint8_t)POSE_IDLE),
               (uint8_t)(ER_SPRITE_BODY_FIRST + key));

  // SLEEP and SICK are one set each, for every species and every stage.
  for (uint8_t st = STAGE_BABY; st <= STAGE_SENIOR; ++st) {
    CHECK_EQ(sprite_set_id(st, 41u, (uint8_t)POSE_SLEEP), (uint8_t)BUGSPR_SLEEP);
    CHECK_EQ(sprite_set_id(st, 41u, (uint8_t)POSE_SICK),  (uint8_t)BUGSPR_SICK);
  }
  // EAT HAS NO ART AND FALLS THROUGH TO THE BODY - the answer P9-C3 gave to the
  // pose question, pinned so that "we dropped it" cannot decay into "we forgot
  // it". data/sprites.h's LOOKUP banner carries the argument.
  for (uint8_t st = STAGE_BABY; st <= STAGE_SENIOR; ++st)
    CHECK_EQ(sprite_set_id(st, 41u, (uint8_t)POSE_EAT),
             (uint8_t)(ER_SPRITE_BODY_FIRST + 41u));

  // THE CLAMP. A form past the end of the atlas falls back to the FIRST body,
  // never past the end of the table - and never onto an egg or a pose set,
  // which is where an unclamped add would land a save written by a build with
  // more families than this one.
  for (unsigned bad = ER_SPRITE_BODY_COUNT; bad <= 255u; ++bad) {
    const uint8_t id = sprite_set_id((uint8_t)STAGE_ADULT, (uint8_t)bad,
                                     (uint8_t)POSE_IDLE);
    CHECK_EQ((int)id, (int)ER_SPRITE_BODY_FIRST);
    CHECK(id < (uint8_t)ER_SPRITE_SET_COUNT);
  }
  // ...and sprite_set() clamps the ID itself, one layer further in.
  for (unsigned bad = ER_SPRITE_SET_COUNT; bad <= 255u; ++bad)
    CHECK_EQ(sprite_set((uint8_t)bad).bits, ER_SPRITE_SETS[BUGSPR_EGG_IDLE].bits);
}

// sprite_frame() is the other half, and its `frame % frames` is the line that
// divides by zero on a zero-frames row. NT_SPR_SET_FITS refuses such a row at
// compile time; this checks the behaviour on the rows that exist.
TEST(a_frame_index_past_the_end_wraps_instead_of_reading_past_the_array) {
  for (int i = 0; i < (int)ER_SPRITE_SET_COUNT; ++i) {
    const SpriteSet& s = ER_SPRITE_SETS[i];
    for (int f = 0; f < 8; ++f) {
      const SpriteRef r = sprite_frame((uint8_t)i, (uint8_t)f);
      CHECK_EQ(r.w, s.w);
      CHECK_EQ(r.h, s.h);
      const ptrdiff_t off = r.bits - s.bits;
      CHECK(off >= 0);
      CHECK(off + (ptrdiff_t)spr_xbm_bytes(s.w, s.h)
            <= (ptrdiff_t)spr_set_bytes(s));
    }
  }
}

// =============================================================================
//  THE DERIVED SLEEPING BODY (P10-C3)
//
//  Phase 9 left a named gap: all sixty species shared ONE sleeping body, and
//  SLEEP is the pose a player looks at longest. P10-C3 derives it instead of
//  authoring forty new sets, and these are the checks that make "derived" mean
//  something other than "hoped".
//
//  THE FOUR PROPERTIES, and what each one catches:
//    (a) CONTAINMENT - the sleeping body lives inside its own idle ink box,
//        grown one column each side and one row down from the top. This is the
//        "not a mangled body" bound, in the same shape tests/test_corruption.cpp
//        asserts for the glitch: on the PIXELS, not on the arguments.
//    (b) A FLOOR - every body must change at least PF_SLEEP_MIN_DIFF pixels.
//        This is what the blink test deliberately does NOT have, and the reason
//        is written there: 2 px is fine for a 90 ms blink and is not fine for a
//        pose held for hours.
//    (c) A CEILING - and it is not the same kind of number. A derivation that
//        rewrote half the creature would be a different creature, so the
//        surviving ink must stay close to the original's.
//    (d) THE HEIGHT ACTUALLY DROPS. The squash is the shape-independent half of
//        the pose, so it must fire on every body, including the seven the atlas
//        marks as non-blinking on one frame.
// =============================================================================
TEST(every_species_derives_a_sleeping_body_that_is_still_that_species) {
  uint16_t worst_diff = 0xFFFFu;
  const char* worst_name = "?";
  int bodies = 0, no_band = 0, eyes_proved = 0, eyes_in_the_splay = 0;

  for (int i = (int)ER_SPRITE_BODY_FIRST; i < (int)ER_SPRITE_SET_COUNT; ++i) {
    const SpriteSet& s = ER_SPRITE_SETS[i];
    const uint8_t stride = pf_stride(s.w);
    const uint16_t fbytes = (uint16_t)stride * s.h;
    for (int f = 0; f < (int)s.frames; ++f) {
      const uint8_t* src = s.bits + (uint32_t)fbytes * (uint32_t)f;
      const SpriteEyeBand e = sprite_eyes((uint8_t)i, (uint8_t)f);
      if (e.y1 < e.y0) ++no_band;

      uint8_t out[PF_FRAME_BYTES];
      const uint16_t diff = pf_build_sleep(src, s.w, s.h, e.y0, e.y1, e.x0, e.x1, out);
      ++bodies;

      // (b) THE FLOOR. A body that sleeps by changing nothing a player can see
      // is the defect this whole decision turns on, so it fails BY NAME.
      if (diff < (uint16_t)PF_SLEEP_MIN_DIFF)
        nt_fail_at(__FILE__, __LINE__, ER_SPRITE_NAMES[i]);
      CHECK(diff >= (uint16_t)PF_SLEEP_MIN_DIFF);
      if (diff < worst_diff) { worst_diff = diff; worst_name = ER_SPRITE_NAMES[i]; }

      uint8_t t, b, l, r;
      CHECK_EQ(pf_scan_ink(src, s.w, s.h, &t, &b, &l, &r), 1u);
      uint8_t st, sb, sl, sr;
      CHECK_EQ(pf_scan_ink(out, s.w, s.h, &st, &sb, &sl, &sr), 1u);

      // (a) CONTAINMENT, pixel by pixel.
      for (uint8_t y = 0; y < s.h; ++y) {
        const uint8_t* row = out + (uint16_t)y * stride;
        for (uint8_t x = 0; x < s.w; ++x) {
          if (!pf_get(row, x)) continue;
          const bool inside = (y >= (uint8_t)(t + 1u)) && (y <= b) &&
                              (x + 1u >= (uint16_t)l) &&
                              ((uint16_t)x <= (uint16_t)r + 1u);
          if (!inside) {
            fprintf(stderr, "  %s frame %d: sleeping pixel (%u,%u) outside the "
                            "idle ink box [%u..%u]x[%u..%u] grown by one\n",
                    ER_SPRITE_NAMES[i], f, (unsigned)x, (unsigned)y,
                    (unsigned)l, (unsigned)r, (unsigned)t, (unsigned)b);
            nt_fail_at(__FILE__, __LINE__, ER_SPRITE_NAMES[i]);
          }
          CHECK(inside);
        }
      }

      // (d) THE HEIGHT DROPS BY EXACTLY ONE ROW, on every body, blinker or not.
      CHECK_EQ((int)st, (int)t + 1);
      CHECK_EQ((int)sb, (int)b);

      // (e) AND THE EYES ACTUALLY SHUT, measured where the squash cannot pay
      //     for it.
      //
      // THIS CHECK EXISTS BECAUSE THE MUTATION RUN FOUND IT MISSING. Deleting
      // the whole eye-shutting step from pf_build_sleep() left every other
      // check green: the squash alone clears the floor on all 120 frames, the
      // containment box does not care, and the height still drops. So the
      // semantic half of the pose - the half that says ASLEEP rather than
      // merely SETTLED - was unguarded.
      //
      // AND ITS FIRST FORM WAS WRONG, which is worth writing down. It asked
      // whether the band's rows changed at all, and failed on ERROX, whose band
      // is a single row at y 22 on a body whose ink ends at 23 - inside the
      // splay. The eyes DO shut there (`.####.....#####.` becomes
      // `.####+++++#####.`) but nothing at that row can tell the eye fill from
      // the dilation. So the measurement moved: it runs the SHIPPED
      // pf_build_lids() and requires every pixel of its fill to be lit in the
      // sleeping frame - but only on rows the squash provably cannot touch,
      // which is above the splay and below the merge. Frames whose whole band
      // lies inside those zones are counted and reported instead of guessed at.
      if (e.y1 >= e.y0) {
        uint8_t fill[PF_STRIP_BYTES], cut[PF_STRIP_BYTES];
        const uint8_t bh = pf_build_lids(src, s.w, s.h, e.y0, e.y1, e.x0, e.x1,
                                         fill, cut);
        if (bh == 0u) nt_fail_at(__FILE__, __LINE__, ER_SPRITE_NAMES[i]);
        int provable = 0;
        for (uint8_t rr = 0; rr < bh; ++rr) {
          const uint8_t y = (uint8_t)(e.y0 + rr);
          if (y >= s.h) break;
          if (y < (uint8_t)(t + 2u)) continue;                 // the merge zone
          if (y + PF_SLEEP_SPREAD > b) continue;               // the splay zone
          const uint8_t* fr = fill + (uint16_t)rr * stride;
          const uint8_t* cr = cut  + (uint16_t)rr * stride;
          const uint8_t* orow = out + (uint16_t)y * stride;
          for (uint8_t x = 0; x < s.w; ++x) {
            if (!pf_get(fr, x) || pf_get(cr, x)) continue;     // cut by the lash
            ++provable;
            if (!pf_get(orow, x)) {
              fprintf(stderr, "  %s frame %d: eye pixel (%u,%u) is OPEN in the "
                              "sleeping body - the pose settled but the eyes "
                              "never shut\n",
                      ER_SPRITE_NAMES[i], f, (unsigned)x, (unsigned)y);
              nt_fail_at(__FILE__, __LINE__, ER_SPRITE_NAMES[i]);
            }
          }
        }
        if (provable > 0) ++eyes_proved; else ++eyes_in_the_splay;
      }

      // (c) STILL THAT SPECIES. Count the ink either way: the squash removes a
      // row and the spread adds up to two columns, so the sleeping body is
      // near the idle one and not a rewrite of it.
      int ink = 0, slp = 0;
      for (uint8_t y = 0; y < s.h; ++y) {
        const uint8_t* a = src + (uint16_t)y * stride;
        const uint8_t* c = out + (uint16_t)y * stride;
        for (uint8_t x = 0; x < s.w; ++x) { ink += pf_get(a, x); slp += pf_get(c, x); }
      }
      CHECK(ink > 0);
      if (slp * 100 < ink * 80 || slp * 100 > ink * 160) {
        fprintf(stderr, "  %s frame %d: %d px asleep against %d awake\n",
                ER_SPRITE_NAMES[i], f, slp, ink);
        nt_fail_at(__FILE__, __LINE__, ER_SPRITE_NAMES[i]);
      }
    }
  }

  CHECK_EQ(bodies, 120);          // 60 species, two frames each
  CHECK_EQ(no_band, 7);           // the seven the atlas marks as non-blinkers
  printf("  sleep: %d bodies, smallest change %u px on %s (floor %d); eyes "
         "proved shut on %d frames, %d bands lie inside the splay\n",
         bodies, (unsigned)worst_diff, worst_name, (int)PF_SLEEP_MIN_DIFF,
         eyes_proved, eyes_in_the_splay);
  // ANTI-VACUITY on (e): "no eye was left open" must not be able to pass
  // because no eye was ever looked at. The overwhelming majority of blinkable
  // frames must have a band the squash provably cannot reach.
  CHECK(eyes_proved > 100);
  CHECK(eyes_in_the_splay < 10);
  // The floor is MEASURED, so it must actually sit under the roster. A floor
  // above the worst body would fail above; a floor so far below it that no
  // conceivable art could trip it would be decoration, and this is the line
  // that says which.
  CHECK(worst_diff >= (uint16_t)PF_SLEEP_MIN_DIFF);
}

// THE CONTROL, and it is the same argument the blink's over-fill control makes.
// Every check above would also pass if pf_build_sleep() did nothing detectable,
// so hand it inputs it MUST refuse or MUST fail the floor on, and require it to
// say so.
TEST(the_sleep_floor_would_see_a_pose_that_changed_nothing) {
  uint8_t out[PF_FRAME_BYTES];
  uint8_t blank[PF_FRAME_BYTES];
  memset(blank, 0, sizeof blank);

  // A blank frame has no pose to derive and must REFUSE rather than return a
  // buffer of zeros that the caller would happily draw.
  CHECK_EQ(pf_build_sleep(blank, 24, 24, 0, 0, 0, 23, out), 0u);

  // A frame larger than the cache geometry is refused, not truncated.
  CHECK_EQ(pf_build_sleep(ER_SPRITE_SETS[ER_SPRITE_BODY_FIRST].bits,
                          (uint8_t)(PF_MAX_W + 1), 24, 0, 0, 0, 23, out), 0u);

  // A body that is a single lit pixel has no second ink row to merge and no
  // bottom rows above its own top to spread, so the derivation changes NOTHING
  // and must say so rather than handing back a frame the caller would draw.
  uint8_t dot[PF_FRAME_BYTES];
  memset(dot, 0, sizeof dot);
  pf_set(dot + (uint16_t)12 * pf_stride(24), 12);
  CHECK_EQ(pf_build_sleep(dot, 24, 24, 0, 0, 0, 23, out), 0u);

  // AND THE ONE THAT PINS THE FLOOR FROM BELOW.
  //
  // THE MUTATION RUN FOUND THIS MISSING TOO. Lowering PF_SLEEP_MIN_DIFF from 10
  // to 3 - which is exactly the "do not lower it to make a build pass" failure
  // the constant's own comment warns about - left every case green, because the
  // only other statement about the floor is that the roster CLEARS it, and a
  // lower floor is easier to clear. So a threshold that could only ever be too
  // HIGH was being called measured.
  //
  // This is a QUIET BODY: a one-pixel-wide vertical bar. It is the least a real
  // silhouette can give the derivation - one row merged away at the top and two
  // columns gained on each of the bottom PF_SLEEP_SPREAD rows - and it must land
  // UNDER the floor, because a Bug that slept by changing this little would
  // not read as asleep at 1x. Together with the roster sweep's
  // `worst_diff >= PF_SLEEP_MIN_DIFF`, the floor is now pinned into a band from
  // both sides: it cannot be raised past the quietest real body (13 px on
  // ESTATIC) and it cannot be lowered past this.
  uint8_t bar[PF_FRAME_BYTES];
  memset(bar, 0, sizeof bar);
  for (uint8_t y = 5; y <= 20u; ++y) pf_set(bar + (uint16_t)y * pf_stride(24), 12);
  const uint16_t d = pf_build_sleep(bar, 24, 24, 0, 0, 0, 23, out);
  CHECK(d > 0u);
  if (d >= (uint16_t)PF_SLEEP_MIN_DIFF) {
    fprintf(stderr, "  a one-pixel-wide bar changes %u px and the floor is %d - "
                    "the floor is too low to reject anything\n",
            (unsigned)d, (int)PF_SLEEP_MIN_DIFF);
    nt_fail_at(__FILE__, __LINE__, "the sleep floor accepts a bare bar");
  }
  CHECK(d < (uint16_t)PF_SLEEP_MIN_DIFF);
}
