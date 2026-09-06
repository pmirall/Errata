// =============================================================================
//  PEBBLEBOL - data/sprites_pebbles.h - THE GENERATED ATLAS
//
//  GENERATED FILE. Do not edit: tools/gen_sprites.py rewrites it from
//  tools/sprites/*.txt (ASCII art, one character per pixel).
//  `tools/gen_sprites.py --check` fails the gate if this file and the ASCII
//  sources have drifted apart, in either direction.
//
//  Format: XBM - data/sprite_types.h owns the stride, the bit order and the
//  two size-guard macros. Row stride ((w+7)>>3) bytes, LSB of a byte is the
//  LEFTMOST pixel, frames stored back to back in one array.
//
//  NOTHING IN THE FIRMWARE INCLUDES THIS HEADER YET, and that is deliberate:
//  P9-C1 built the pipeline, P9-C3 draws the 60 bodies and performs the swap
//  (delete the 36 legacy body/pose sets from data/sprites.h, point
//  sprite_set_id() at PB_SPRITE_BODY_FIRST + sprite_id, re-record the nine
//  goldens that contain body art). Until then the only consumer is
//  tests/test_sprite_pipeline.cpp, which is what keeps this file compiling
//  and what proves the two EGG sets here are byte-identical to the ones
//  data/sprites.h has been drawing since phase 1.
//
//  2 set(s), 0 species bodies, 288 B of art.
// =============================================================================
#ifndef PB_SPRITES_PEBBLES_H
#define PB_SPRITES_PEBBLES_H

#include <stdint.h>
#include "sprite_types.h"

// Derived from the emitted bytes and the set order (FNV-1a 32 folded to 16),
// so it moves when the art moves and cannot be forgotten. P9-C3 wires
// SPRITE_REV to it; ui/petfx.cpp:41 then breaks the build on purpose when the
// art its hand-measured eyelid table was read off changes.
#define PB_SPRITE_ART_HASH 0x778Cu

// Set ids. The ORDER IS A CONTRACT and comes from tools/sprites/atlas.txt,
// never from the filesystem. The species block is the tail: a body's slot is
// PB_SPRITE_BODY_FIRST + (species_id - 1), which is the pack's
// `sprite_id == id - 1` invariant expressed in the atlas.
enum PbSpriteSetId : uint8_t {
  PBSPR_EGG_IDLE = 0,                    // fixed slot
  PBSPR_EGG_CRACK = 1,                   // fixed slot
  PB_SPRITE_SET_COUNT = 2
};

#define PB_SPRITE_BODY_FIRST  2
#define PB_SPRITE_BODY_COUNT  0

// The set names, for tools/ and tests/ that have to PRINT one - chiefly
// tests/tools/sprite_dump.cpp, which renders this atlas for a human to look
// at. Emitted rather than transcribed so a name list can never drift from the
// art it labels. `inline constexpr` and referenced by no firmware translation
// unit, so the linker keeps none of it in .rodata.
inline constexpr const char* const PB_SPRITE_NAMES[PB_SPRITE_SET_COUNT] = {
  "EGG_IDLE",
  "EGG_CRACK",
};

// -----------------------------------------------------------------------------
//  PIXEL DATA
// -----------------------------------------------------------------------------

// EGG_IDLE  24x24 x2  (144 B)  <- tools/sprites/egg_idle.txt
// The unhatched egg. FIXED SLOT 0: sprite_set() falls back to this set for any
// out-of-range id, so it is the one body that must always exist.
// Frame 1 is the same shell shifted one pixel right and up - the wobble.
// Transcribed from the hand-typed hex in data/sprites.h at P9-C1, pixel for
// pixel; tests/test_sprite_pipeline.cpp asserts the two are byte-identical.
inline constexpr uint8_t pb_spr_egg_idle[] = {
  0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0xFF, 0x00, 0x80, 0xFF, 0x01, 0xC0, 0xFF, 0x03, 0x60,
  0xFC, 0x07, 0x60, 0xFE, 0x07, 0xF0, 0xFF, 0x0F, 0xF0, 0xFF, 0x0F, 0xF8, 0xFF, 0x1F, 0xF8, 0xFF,
  0x1F, 0xCC, 0xF3, 0x3C, 0x84, 0x61, 0x38, 0xCC, 0xF3, 0x3C, 0xFC, 0xFF, 0x3F, 0xFC, 0xFF, 0x3F,
  0xFC, 0xFF, 0x3F, 0xF8, 0xFF, 0x1F, 0xF8, 0xFF, 0x1F, 0xF0, 0xFF, 0x0F, 0xE0, 0xFF, 0x07, 0x80,
  0xFF, 0x01, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFC, 0x00, 0x00, 0xFE,
  0x01, 0x00, 0xFF, 0x03, 0x80, 0xFF, 0x07, 0xC0, 0xF8, 0x0F, 0xC0, 0xFC, 0x0F, 0xE0, 0xFF, 0x1F,
  0xE0, 0xFF, 0x1F, 0xF8, 0xFF, 0x1F, 0xF8, 0xFF, 0x1F, 0xCC, 0xF3, 0x3C, 0x84, 0x61, 0x38, 0xCC,
  0xF3, 0x3C, 0xFC, 0xFF, 0x3F, 0xFC, 0xFF, 0x3F, 0xFC, 0xFF, 0x3F, 0xF8, 0xFF, 0x1F, 0xF8, 0xFF,
  0x1F, 0xF0, 0xFF, 0x0F, 0xE0, 0xFF, 0x07, 0x80, 0xFF, 0x01, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00,
};

// EGG_CRACK  24x24 x2  (144 B)  <- tools/sprites/egg_crack.txt
// The egg on its last minutes, drawn by ceremony.cpp and screen_evolution.cpp.
// Frame 0 is the fissure, frame 1 the shell coming apart.
// Transcribed from data/sprites.h at P9-C1; the pipeline test pins it.
inline constexpr uint8_t pb_spr_egg_crack[] = {
  0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0xE7, 0x00, 0x80, 0xE7, 0x03, 0xC0, 0xE7, 0x03, 0x60,
  0xAC, 0x07, 0x60, 0xCE, 0x07, 0xF0, 0xE7, 0x0F, 0xF0, 0xF3, 0x0F, 0xF8, 0xF9, 0x1F, 0xF8, 0xFC,
  0x1F, 0xCC, 0xCD, 0x33, 0x84, 0xC9, 0x30, 0xCC, 0xF3, 0x3C, 0xFC, 0xFF, 0x3F, 0xFC, 0xFF, 0x3F,
  0xFC, 0xFF, 0x3F, 0xF8, 0xFF, 0x1F, 0xF8, 0xFF, 0x1F, 0xF0, 0xFF, 0x0F, 0xE0, 0xFF, 0x07, 0x80,
  0xFF, 0x01, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC6, 0x00, 0x00, 0x87,
  0x03, 0x80, 0x07, 0x0F, 0xC0, 0x03, 0x1E, 0xE0, 0x00, 0x1C, 0x60, 0x30, 0x38, 0xF0, 0x78, 0x3C,
  0xF0, 0xFD, 0x3E, 0xF8, 0xFD, 0x7E, 0xF8, 0xFC, 0x7C, 0xCC, 0xFD, 0x6E, 0x84, 0x79, 0x66, 0xCC,
  0x03, 0x6F, 0xFC, 0xFF, 0x3F, 0xFC, 0xE7, 0x3F, 0xFC, 0xC3, 0x3F, 0xF8, 0xF3, 0x1F, 0xF8, 0xFF,
  0x1F, 0xF0, 0xFF, 0x0F, 0xE0, 0xFF, 0x07, 0x80, 0xFF, 0x01, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00,
};

// -----------------------------------------------------------------------------
//  TABLE
// -----------------------------------------------------------------------------
inline constexpr SpriteSet PB_SPRITE_SETS[PB_SPRITE_SET_COUNT] = {
  { pb_spr_egg_idle, 24, 24, 2 },  // EGG_IDLE
  { pb_spr_egg_crack, 24, 24, 2 },  // EGG_CRACK
};

// Every row tied to the array it points at, frames included. drawXBM() reads
// ((w+7)>>3)*h bytes with no bound of its own, so a row that disagrees with
// its array is a silent read into the NEXT sprite - see data/sprite_types.h.
NT_SPR_SET_FITS(pb_spr_egg_idle, PB_SPRITE_SETS, PBSPR_EGG_IDLE);
NT_SPR_SET_FITS(pb_spr_egg_crack, PB_SPRITE_SETS, PBSPR_EGG_CRACK);

// The art budget, MEASURED by the compiler through the table. The declared
// number is what the generator counted; the equality assert is what makes a
// disagreement between the two a named build failure instead of a comment.
constexpr unsigned pb_sprite_atlas_bytes() {
  unsigned n = 0;
  for (unsigned i = 0; i < (unsigned)PB_SPRITE_SET_COUNT; ++i)
    n += spr_set_bytes(PB_SPRITE_SETS[i]);
  return n;
}
#define PB_SPRITE_DATA_BYTES          (pb_sprite_atlas_bytes())
#define PB_SPRITE_DATA_BYTES_DECLARED 288u
#define PB_SPRITE_DATA_BYTES_MAX      12288u
static_assert(PB_SPRITE_DATA_BYTES == PB_SPRITE_DATA_BYTES_DECLARED,
              "generated atlas size disagrees with the generator");
static_assert(PB_SPRITE_DATA_BYTES <= PB_SPRITE_DATA_BYTES_MAX,
              "generated sprite art over its half of the flash budget");

#endif  // PB_SPRITES_PEBBLES_H
