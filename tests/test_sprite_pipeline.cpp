// =============================================================================
//  PEBBLEBOL host test - test_sprite_pipeline.cpp
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
#include "data/sprites_pebbles.h"

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
  CHECK_EQ(sizeof pb_spr_egg_idle,  144u);
  CHECK_EQ(sizeof pb_spr_egg_crack, 144u);
  CHECK_EQ(memcmp(pb_spr_egg_idle,  kEggIdleHead,  sizeof kEggIdleHead),  0);
  CHECK_EQ(memcmp(pb_spr_egg_crack, kEggCrackHead, sizeof kEggCrackHead), 0);

  // ...and the table rows, so a set that happened to hold the right bytes under
  // the wrong dimensions is not mistaken for a pass.
  CHECK_EQ(PB_SPRITE_SETS[PBSPR_EGG_IDLE].w,       24);
  CHECK_EQ(PB_SPRITE_SETS[PBSPR_EGG_IDLE].h,       24);
  CHECK_EQ(PB_SPRITE_SETS[PBSPR_EGG_IDLE].frames,   2);
  CHECK_EQ(PB_SPRITE_SETS[PBSPR_EGG_CRACK].w,      24);
  CHECK_EQ(PB_SPRITE_SETS[PBSPR_EGG_CRACK].h,      24);
  CHECK_EQ(PB_SPRITE_SETS[PBSPR_EGG_CRACK].frames,  2);

  // The egg is still slot 0, which sprite_set() falls back to for any
  // out-of-range id. Moving it is how every bad id starts drawing a creature.
  CHECK_EQ((int)PBSPR_EGG_IDLE, 0);
  CHECK_EQ(sprite_set(255).bits, PB_SPRITE_SETS[PBSPR_EGG_IDLE].bits);
}

// A pixel this test can point at BY COORDINATE, so the case above cannot pass
// by comparing two arrays that are both wrong in the same way. Row 1 of
// EGG_IDLE frame 0 is the six-pixel crown of the shell at x 9..14: the LSB rule
// says byte 1 of that row is 0x7E, and a big-endian-per-byte packer would write
// 0x7E reversed (0x7E is a palindrome under bit reversal - hence the SECOND
// row, whose 0x80,0xFF,0x01 is not).
TEST(the_leftmost_pixel_of_a_row_is_the_low_bit_of_its_first_byte) {
  const SpriteSet& e = PB_SPRITE_SETS[PBSPR_EGG_IDLE];
  // Row 1: pixels 9..14 lit, 0..8 and 15..23 clear.
  for (int x = 0; x < 24; ++x)
    CHECK_EQ(pixel_at(e, 0, x, 1), (x >= 9 && x <= 14) ? 1 : 0);
  // Row 3: 0x80,0xFF,0x01 - pixel 7 lit, 8..15 lit, 16 lit, nothing else.
  for (int x = 0; x < 24; ++x)
    CHECK_EQ(pixel_at(e, 0, x, 3), (x >= 7 && x <= 16) ? 1 : 0);
  // The raw bytes those two rows must be, stated rather than derived, so a
  // change to pixel_at() cannot make this case agree with itself.
  CHECK_EQ(pb_spr_egg_idle[3 * 1 + 0], 0x00);
  CHECK_EQ(pb_spr_egg_idle[3 * 1 + 1], 0x7E);
  CHECK_EQ(pb_spr_egg_idle[3 * 1 + 2], 0x00);
  CHECK_EQ(pb_spr_egg_idle[3 * 3 + 0], 0x80);
  CHECK_EQ(pb_spr_egg_idle[3 * 3 + 1], 0xFF);
  CHECK_EQ(pb_spr_egg_idle[3 * 3 + 2], 0x01);
}

// =============================================================================
//  2. THE GENERATED ATLAS IS WELL FORMED
// =============================================================================
TEST(every_generated_row_matches_the_array_it_points_at) {
  CHECK(PB_SPRITE_SET_COUNT >= 1);
  unsigned total = 0;
  for (int i = 0; i < (int)PB_SPRITE_SET_COUNT; ++i) {
    const SpriteSet& s = PB_SPRITE_SETS[i];
    CHECK(s.bits != nullptr);
    CHECK(s.w >= 1 && s.h >= 1);
    CHECK(s.w <= 40 && s.h <= 40);      // PF_MAX_W/H, XBM_MIRROR_MAX_W
    CHECK(s.frames >= 1);               // sprite_frame() does `frame % frames`
    CHECK(PB_SPRITE_NAMES[i] != nullptr && PB_SPRITE_NAMES[i][0] != '\0');
    total += spr_set_bytes(s);
  }
  // The generator's own arithmetic, the compiler's, and this test's all agree.
  CHECK_EQ(total, (unsigned)PB_SPRITE_DATA_BYTES);
  CHECK_EQ(total, (unsigned)PB_SPRITE_DATA_BYTES_DECLARED);
}

TEST(every_generated_frame_has_ink_and_no_stray_padding_bits) {
  for (int i = 0; i < (int)PB_SPRITE_SET_COUNT; ++i) {
    const SpriteSet& s = PB_SPRITE_SETS[i];
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
}

// The generator WARNS about this on every run; here it is fatal, because a warning
// on a 120-frame art drop is a warning nobody reads. If a set is ever
// deliberately still, name it in the exception below rather than deleting the
// case - "this one is meant to be" is a fact worth writing down.
TEST(a_two_frame_set_actually_animates) {
  for (int i = 0; i < (int)PB_SPRITE_SET_COUNT; ++i) {
    const SpriteSet& s = PB_SPRITE_SETS[i];
    if (s.frames < 2) continue;
    // (no deliberate exceptions today)
    int diff = 0;
    for (int y = 0; y < (int)s.h; ++y)
      for (int x = 0; x < (int)s.w; ++x)
        if (pixel_at(s, 0, x, y) != pixel_at(s, 1, x, y)) ++diff;
    if (diff == 0) {
      // Named, so the failure line says WHICH set was copy-pasted.
      nt_fail_at(__FILE__, __LINE__, PB_SPRITE_NAMES[i]);
    }
    CHECK(diff > 0);
  }
}

// =============================================================================
//  3. THE SLOT CONTRACT P9-C3 INHERITS
// =============================================================================
// The species bodies are the TAIL of the atlas and a body's slot is
// PB_SPRITE_BODY_FIRST + (species_id - 1) - the pack's `sprite_id == id - 1`
// invariant expressed in the atlas. tools/gen_sprites.py refuses a source tree
// that breaks it; this is the same statement over the emitted header, which is
// what the firmware will actually index.
TEST(the_species_block_is_the_tail_of_the_generated_atlas) {
  CHECK_EQ((int)PB_SPRITE_BODY_FIRST,
           (int)PB_SPRITE_SET_COUNT - (int)PB_SPRITE_BODY_COUNT);
  CHECK(PB_SPRITE_BODY_COUNT <= PB_SPRITE_SET_COUNT);

  // THE LOOP WAS EMPTY AT P9-C1 AND THE COUNT WAS ASSERTED TO BE 0 so that
  // "no bodies yet" was a recorded fact rather than a vacuous loop nobody
  // noticed. P9-C3 drew them; the assertion moved with the art rather than
  // being deleted, and 60 is now the number that has to hold.
  CHECK_EQ((int)PB_SPRITE_BODY_COUNT, 60);
  for (int k = 0; k < (int)PB_SPRITE_BODY_COUNT; ++k) {
    const SpriteSet& s = PB_SPRITE_SETS[PB_SPRITE_BODY_FIRST + k];
    CHECK_EQ(s.w, 24);
    CHECK_EQ(s.h, 24);
    CHECK_EQ(s.frames, 2);
  }
  // A body may never be bound to a species the roster does not ship.
  CHECK(PB_SPRITE_BODY_COUNT <= SPECIES_PACK_COUNT);
  // And the four fixed slots are the four this build's lookup names. A fifth
  // effect set appended in the WRONG place would push every species body one
  // slot along and every one of them would draw its neighbour.
  CHECK_EQ((int)PB_SPRITE_BODY_FIRST, 4);
  CHECK_EQ((int)PBSPR_EGG_IDLE,  0);
  CHECK_EQ((int)PBSPR_EGG_CRACK, 1);
  CHECK_EQ((int)PBSPR_SLEEP,     2);
  CHECK_EQ((int)PBSPR_SICK,      3);
}

// EVERY SPECIES BODY IS A DIFFERENT PICTURE, not merely a different pointer.
//
// The distinctness check in every_legacy_row_matches_the_array_it_points_at
// compared POINTERS, which catches a table row aimed at its neighbour's array
// and nothing else. Sixty bodies drawn by five different hands can collide in a
// way pointers cannot see: two files with the same pixels. This compares the
// bytes, both frames, all 1,770 pairs.
TEST(no_two_species_bodies_hold_the_same_pixels) {
  for (int a = 0; a < (int)PB_SPRITE_BODY_COUNT; ++a)
    for (int b = a + 1; b < (int)PB_SPRITE_BODY_COUNT; ++b) {
      const SpriteSet& x = PB_SPRITE_SETS[PB_SPRITE_BODY_FIRST + a];
      const SpriteSet& y = PB_SPRITE_SETS[PB_SPRITE_BODY_FIRST + b];
      if (x.bits == y.bits) {
        nt_fail_at(__FILE__, __LINE__, PB_SPRITE_NAMES[PB_SPRITE_BODY_FIRST + a]);
        continue;
      }
      if (memcmp(x.bits, y.bits, spr_set_bytes(x)) == 0)
        nt_fail_at(__FILE__, __LINE__, PB_SPRITE_NAMES[PB_SPRITE_BODY_FIRST + b]);
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
  for (int i = 0; i < (int)PB_SPRITE_SET_COUNT; ++i) {
    const SpriteSet& s = PB_SPRITE_SETS[i];
    if (i == (int)PBSPR_EGG_IDLE || i == (int)PBSPR_EGG_CRACK) continue;
    for (int f = 0; f < (int)s.frames; ++f) {
      int ink_on_last = 0;
      for (int x = 0; x < (int)s.w; ++x)
        ink_on_last += pixel_at(s, f, x, (int)s.h - 1);
      if (ink_on_last == 0) nt_fail_at(__FILE__, __LINE__, PB_SPRITE_NAMES[i]);
      CHECK(ink_on_last > 0);
    }
  }
  // The exception is BOUNDED: the eggs hover by exactly one row, not by
  // whatever they like, so a re-drawn egg that floated four pixels above the
  // floor would fail here rather than inherit the exemption.
  for (int i = 0; i < 2; ++i) {
    const SpriteSet& s = PB_SPRITE_SETS[i];
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
// compile time. data/sprites_pebbles.h static_asserts it; this is the same rule
// as a test, for the same reason section 5 below exists - and it additionally
// checks the property the static_assert cannot express cheaply: a band that
// says a body blinks must actually contain an unlit pixel to close.
TEST(every_eye_band_lies_inside_its_body_and_has_something_to_close) {
  int blinkers = 0;
  for (int i = 0; i < (int)PB_SPRITE_SET_COUNT; ++i) {
    const SpriteSet& s = PB_SPRITE_SETS[i];
    for (int f = 0; f < 2; ++f) {
      const SpriteEyeBand e = sprite_eyes((uint8_t)i, (uint8_t)f);
      if (e.y1 < e.y0) continue;        // "does not blink"
      ++blinkers;
      CHECK(e.y1 < s.h);
      CHECK(e.x1 < s.w);
      CHECK(e.x0 <= e.x1);
      int holes = 0;
      for (int y = e.y0; y <= (int)e.y1; ++y)
        for (int x = e.x0; x <= (int)e.x1; ++x)
          if (!pixel_at(s, f, x, y)) ++holes;
      if (holes == 0) nt_fail_at(__FILE__, __LINE__, PB_SPRITE_NAMES[i]);
      CHECK(holes > 0);
    }
  }
  // Most of the roster blinks. If this ever reads 0 the derivation has broken
  // in a way every other assertion above would pass: an all-{255,0,0,0} table
  // is "inside its body" for every set.
  CHECK(blinkers > (int)PB_SPRITE_SET_COUNT);
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
  CHECK_EQ((unsigned)PB_SPRITE_DATA_BYTES, 64u * 144u);
  CHECK_EQ((unsigned)PB_SPRITE_DATA_BYTES, 9216u);
  CHECK_EQ((unsigned)PB_SPRITE_DATA_BYTES,
           (unsigned)PB_SPRITE_DATA_BYTES_DECLARED);

  // The survivors data/sprites.h still owns: icons 384 + mini 96 + badges 312
  // + the twelve emotes 239.
  const unsigned survivors = (unsigned)SPRITE_DATA_BYTES
                           - (unsigned)PB_SPRITE_DATA_BYTES;
  CHECK_EQ(survivors, 1031u);

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
  for (int i = 0; i < (int)PB_SPRITE_SET_COUNT; ++i) {
    const SpriteSet& s = PB_SPRITE_SETS[i];
    CHECK(s.bits != nullptr);
    CHECK(s.frames >= 1);
    CHECK(s.w >= 1 && s.h >= 1 && s.w <= 40 && s.h <= 40);
    total += spr_set_bytes(s);
    // Distinctness: two rows pointing at ONE array is how a set silently
    // becomes a copy of its neighbour.
    for (int j = i + 1; j < (int)PB_SPRITE_SET_COUNT; ++j)
      CHECK(PB_SPRITE_SETS[i].bits != PB_SPRITE_SETS[j].bits);
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
    CHECK_EQ(sprite_set_id((uint8_t)STAGE_EGG, 7u, pose), (uint8_t)PBSPR_EGG_IDLE);

  // Then the body, at every stage above EGG. The stage no longer changes it,
  // and that is the deletion this chunk made - so it is asserted, not assumed.
  for (uint8_t st = STAGE_BABY; st <= STAGE_SENIOR; ++st)
    for (uint8_t key = 0; key < (uint8_t)PB_SPRITE_BODY_COUNT; ++key)
      CHECK_EQ(sprite_set_id(st, sprite_form_of(key, (Stage)st), (uint8_t)POSE_IDLE),
               (uint8_t)(PB_SPRITE_BODY_FIRST + key));

  // SLEEP and SICK are one set each, for every species and every stage.
  for (uint8_t st = STAGE_BABY; st <= STAGE_SENIOR; ++st) {
    CHECK_EQ(sprite_set_id(st, 41u, (uint8_t)POSE_SLEEP), (uint8_t)PBSPR_SLEEP);
    CHECK_EQ(sprite_set_id(st, 41u, (uint8_t)POSE_SICK),  (uint8_t)PBSPR_SICK);
  }
  // EAT HAS NO ART AND FALLS THROUGH TO THE BODY - the answer P9-C3 gave to the
  // pose question, pinned so that "we dropped it" cannot decay into "we forgot
  // it". data/sprites.h's LOOKUP banner carries the argument.
  for (uint8_t st = STAGE_BABY; st <= STAGE_SENIOR; ++st)
    CHECK_EQ(sprite_set_id(st, 41u, (uint8_t)POSE_EAT),
             (uint8_t)(PB_SPRITE_BODY_FIRST + 41u));

  // THE CLAMP. A form past the end of the atlas falls back to the FIRST body,
  // never past the end of the table - and never onto an egg or a pose set,
  // which is where an unclamped add would land a save written by a build with
  // more families than this one.
  for (unsigned bad = PB_SPRITE_BODY_COUNT; bad <= 255u; ++bad) {
    const uint8_t id = sprite_set_id((uint8_t)STAGE_ADULT, (uint8_t)bad,
                                     (uint8_t)POSE_IDLE);
    CHECK_EQ((int)id, (int)PB_SPRITE_BODY_FIRST);
    CHECK(id < (uint8_t)PB_SPRITE_SET_COUNT);
  }
  // ...and sprite_set() clamps the ID itself, one layer further in.
  for (unsigned bad = PB_SPRITE_SET_COUNT; bad <= 255u; ++bad)
    CHECK_EQ(sprite_set((uint8_t)bad).bits, PB_SPRITE_SETS[PBSPR_EGG_IDLE].bits);
}

// sprite_frame() is the other half, and its `frame % frames` is the line that
// divides by zero on a zero-frames row. NT_SPR_SET_FITS refuses such a row at
// compile time; this checks the behaviour on the rows that exist.
TEST(a_frame_index_past_the_end_wraps_instead_of_reading_past_the_array) {
  for (int i = 0; i < (int)PB_SPRITE_SET_COUNT; ++i) {
    const SpriteSet& s = PB_SPRITE_SETS[i];
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
