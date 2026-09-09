// =============================================================================
//  ERRATA - ui/xbm_mirror.h
//  THE PIXEL CORE'S HORIZONTAL FLIP, LIFTED OUT SO TWO FILES CAN SHARE IT
//  (plan P4-C4).
//
//  This is petfx.cpp's pf_mirror_frame() and its bit-reversal table, moved
//  verbatim into a translation unit of their own. NOTHING ABOUT THE ALGORITHM
//  CHANGED - the comment below is the one that stood over it in petfx.cpp and
//  the padding shift it describes is the same shift.
//
//  WHY IT MOVED, and it is not tidiness. Two things now need to draw a body
//  facing the other way: ui/petfx.cpp, which turns the walking pet round, and
//  ui/battle_renderer.cpp, which faces the two combatants at each other. The
//  alternative was a second copy of a routine whose whole difficulty is one
//  off-by-four on a 28 px sprite, which is exactly the shape of bug that gets
//  fixed in one copy and not the other.
//
//  IT ALSO GETS A REAL TEST FOR THE FIRST TIME. petfx.cpp fences this code
//  between PIXEL CORE markers and says that scratchpad/petfx/mkharness.py
//  slices those lines out and compiles them on the host - but scratchpad/ IS
//  NOT IN THIS REPOSITORY, so no test in the tree has ever executed the mirror.
//  As a pure module it compiles into tests/ like every other pure module, and
//  tests/test_battle_screen.cpp drives it, including the 28 px case the
//  comment names.
//
//  PURE MODULE. stdint and nothing else: no Arduino, no U8G2, no render.h, no
//  gfx.h, no sim. tools/check.sh's petfx gate (no sim.h / genome.h) is
//  untouched by this header, which reaches for neither.
//
//  Identifiers and comments: English. No user-facing text lives here.
// =============================================================================
#ifndef ER_XBM_MIRROR_H
#define ER_XBM_MIRROR_H

#include <stdint.h>

// Bytes one row of a `w`-wide XBM occupies. XBM rows are padded to whole bytes,
// which is where the whole difficulty below comes from.
inline constexpr uint8_t xbm_stride(uint8_t w) { return (uint8_t)((w + 7u) >> 3); }

// -----------------------------------------------------------------------------
//  xbm_mirror_frame - horizontal flip of a whole XBM frame.
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
//
//  `dst` must hold xbm_stride(w) * h bytes and must not overlap `src`.
//  w > XBM_MIRROR_MAX_W is refused rather than smashing the internal row
//  buffer: the widest body in the atlas is the 40 px adult.
// -----------------------------------------------------------------------------
#define XBM_MIRROR_MAX_W  40

void xbm_mirror_frame(const uint8_t* src, uint8_t* dst, uint8_t w, uint8_t h);

#endif  // ER_XBM_MIRROR_H
