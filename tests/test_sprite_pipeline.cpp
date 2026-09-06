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
//  IT ALSO CARRIES THE RUNTIME HALF OF THE LEGACY ATLAS'S SIZE GUARDS.
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
  // Same size, same shape, same bytes. Not "same picture" - the same 144 bytes,
  // because the whole point is that the generator's packing is checked and not
  // merely plausible.
  CHECK_EQ(sizeof pb_spr_egg_idle,  sizeof spr_egg_idle);
  CHECK_EQ(sizeof pb_spr_egg_crack, sizeof spr_egg_crack);
  CHECK_EQ(memcmp(pb_spr_egg_idle,  spr_egg_idle,  sizeof spr_egg_idle),  0);
  CHECK_EQ(memcmp(pb_spr_egg_crack, spr_egg_crack, sizeof spr_egg_crack), 0);

  // ...and the table rows agree too, so a set that happened to hold the right
  // bytes under the wrong dimensions is not mistaken for a pass.
  CHECK_EQ(PB_SPRITE_SETS[PBSPR_EGG_IDLE].w,      SPRITE_SETS[SPR_EGG_IDLE].w);
  CHECK_EQ(PB_SPRITE_SETS[PBSPR_EGG_IDLE].h,      SPRITE_SETS[SPR_EGG_IDLE].h);
  CHECK_EQ(PB_SPRITE_SETS[PBSPR_EGG_IDLE].frames, SPRITE_SETS[SPR_EGG_IDLE].frames);
  CHECK_EQ(PB_SPRITE_SETS[PBSPR_EGG_CRACK].w,      SPRITE_SETS[SPR_EGG_CRACK].w);
  CHECK_EQ(PB_SPRITE_SETS[PBSPR_EGG_CRACK].h,      SPRITE_SETS[SPR_EGG_CRACK].h);
  CHECK_EQ(PB_SPRITE_SETS[PBSPR_EGG_CRACK].frames, SPRITE_SETS[SPR_EGG_CRACK].frames);
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

  // Every body is 24x24x2. THE LOOP IS EMPTY TODAY - P9-C1 built the pipeline
  // and drew no creature - and that is stated rather than hidden: the count is
  // asserted so that "0 bodies" is a fact this file records, not an accident
  // that makes the loop vacuous without anyone noticing.
  CHECK_EQ((int)PB_SPRITE_BODY_COUNT, 0);      // P9-C3 raises this to 60
  for (int k = 0; k < (int)PB_SPRITE_BODY_COUNT; ++k) {
    const SpriteSet& s = PB_SPRITE_SETS[PB_SPRITE_BODY_FIRST + k];
    CHECK_EQ(s.w, 24);
    CHECK_EQ(s.h, 24);
    CHECK_EQ(s.frames, 2);
  }
  // A body may never be bound to a species the roster does not ship.
  CHECK(PB_SPRITE_BODY_COUNT <= SPECIES_PACK_COUNT);
}

// =============================================================================
//  4. THE BUDGET, ACROSS BOTH ATLASES
//
//  data/sprites.h's 24,576 B ceiling is a TRANSITION allowance: it was widened
//  for exactly this window, when the new art arrives before the old art can
//  safely go. Each header asserts its own half at compile time; this is the only
//  place both are visible, so it is the only place the SUM can be checked - and
//  the sum is what the flash budget actually pays for.
// =============================================================================
TEST(both_atlases_together_fit_the_transition_allowance) {
  const unsigned legacy = (unsigned)SPRITE_DATA_BYTES;
  const unsigned gen    = (unsigned)PB_SPRITE_DATA_BYTES;
  CHECK_EQ(legacy, (unsigned)SPRITE_DATA_BYTES_DECLARED);
  CHECK(legacy + gen <= (unsigned)SPRITE_DATA_BYTES_MAX);
  // The end state P9-C3 lands on, recorded here so the number in the header
  // comment is checked by something: 60 bodies x 144 B, plus the two eggs, plus
  // the 1,031 B of icons / mini-icons / badges / emotes that survive.
  const unsigned end_state = 60u * 144u + 2u * 144u + 1031u;
  CHECK(end_state < (unsigned)SPRITE_DATA_BYTES_MAX);
  CHECK_EQ(end_state, 9959u);
}

// =============================================================================
//  5. THE RUNTIME HALF OF THE LEGACY ATLAS'S GUARDS
//
//  NT_SPR_SET_FITS is a static_assert and fires only where it is included. This
//  is the same rule as a test, so a header that stops being included anywhere
//  cannot take its guard with it silently.
// =============================================================================
TEST(every_legacy_row_matches_the_array_it_points_at) {
  unsigned total = 0;
  for (int i = 0; i < (int)SPRITE_SET_COUNT; ++i) {
    const SpriteSet& s = SPRITE_SETS[i];
    CHECK(s.bits != nullptr);
    CHECK(s.frames >= 1);
    CHECK(s.w >= 1 && s.h >= 1 && s.w <= 40 && s.h <= 40);
    total += spr_set_bytes(s);
    // Distinctness: two rows pointing at ONE array is how a set silently
    // becomes a copy of its neighbour.
    for (int j = i + 1; j < (int)SPRITE_SET_COUNT; ++j)
      CHECK(SPRITE_SETS[i].bits != SPRITE_SETS[j].bits);
  }
  CHECK_EQ(total, 9448u);               // the 38 sets; the rest is icons+emotes
  CHECK_EQ((unsigned)SPRITE_DATA_BYTES, 10479u);
}
