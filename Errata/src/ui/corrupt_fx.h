// =============================================================================
//  ERRATA - ui/corrupt_fx.h
//  THE VISIBLE HALF OF CORRUPTION (spec section 55). P9-C5.
//
//  WHAT THIS FILE IS. game/corruption.{h,cpp} owns the STATUS - the bit, the
//  24 h deadline, arming, expiring and the cure. This file owns the two effects
//  that are made of PIXELS and MOTION rather than of state:
//
//    * WHERE the sprite glitch is allowed to draw, and when;
//    * WHICH row of ui/petfx.cpp's behaviour table the automaton reads.
//
//  IT EXISTS AS ITS OWN TRANSLATION UNIT FOR ONE REASON: ui/petfx.cpp INCLUDES
//  render.h, WHICH INCLUDES Arduino.h AND U8g2lib.h, SO NO HOST BINARY CAN LINK
//  IT. Every rule left inside petfx.cpp is a rule no test in this repository can
//  execute - which is exactly how xbm_mirror.cpp came to exist (tests/Makefile
//  says so: "ui/petfx.cpp's own horizontal flip, which nothing in this
//  repository compiled or executed until it moved out of that device-only
//  translation unit"). The brief for this chunk asks for a BOUND that is
//  mutation-tested by name; a bound that lives where no test can reach it is a
//  bound nobody can check. So the geometry moved and the painting stayed.
//
//  THE SPLIT, STATED EXACTLY, because the honest version of "the glitch is
//  bounded" depends on it:
//    * THIS FILE decides how many rows there are, where each one starts, how
//      wide it is, which Bayer level and phase it carries, and clamps every one
//      of those into the caller's rectangle. That is shipped code and
//      tests/test_corruption.cpp drives it directly.
//    * ui/petfx.cpp turns each row into pixels with rd_dither_rect_phase() at
//      draw colour 2 (render.h: "setDrawColor(2) -> XORs it (shimmer)"), which
//      is an existing primitive with its own clipping. This file never touches
//      a framebuffer and has no opinion about the Bayer matrix.
//
//  PURE MODULE: stdint only. No Arduino, no u8g2, no render.h, no PetView, no
//  clock of its own, no RNG, no heap, no float, no I/O. Deterministic in its
//  arguments, so the same Bug glitches the same way on every device.
//
//  Identifiers and comments: English. This module draws no glyphs and needs no
//  strings_es.h.
// =============================================================================
#ifndef ER_UI_CORRUPT_FX_H
#define ER_UI_CORRUPT_FX_H

#include <stdint.h>

// -----------------------------------------------------------------------------
//  THE BEHAVIOUR ROW
//
//  "Altered idle animation" is one extra row in ui/petfx.cpp's PF_TEMPER table
//  and one line in pf_derive(). It is NOT a new sprite pose: POSE_GLITCH would
//  need one drawn body per species, which is 60 more 24x24 sets against an
//  atlas P9-C3 just spent the phase shrinking, for an effect that is supposed
//  to look like the creature behaving wrongly rather than like a different
//  creature. The automaton is where "behaving wrongly" lives.
//
//  THE INDEX IS COMPUTED HERE so that the ONE property that matters can be
//  asserted on the host: the corrupted row is used exactly while the status is
//  set, and the pet's own row comes back the instant it is not. An effect that
//  outlived its status would be the §55 violation this chunk is guarding
//  against, and petfx.cpp cannot be linked to prove it does not.
//
//  CFX_TEMPER_CORRUPT is TEMPER_COUNT, i.e. one past the last genome
//  temperament. petfx.cpp static_asserts that against core/nt_types.h, so the
//  two cannot drift: add a fifth temperament and the BUILD fails here rather
//  than the corrupted pet quietly inheriting the new one's dwell times.
// -----------------------------------------------------------------------------
#define CFX_TEMPER_CORRUPT   4u
#define CFX_TEMPER_ROWS      5u    // CFX_TEMPER_CORRUPT + 1

// The row an out-of-range temperament folds to. It is TEMPER_TRANQUILO, which
// is what pf_derive() folded to before this function existed, and petfx.cpp
// static_asserts the two against each other: a refactor that quietly changed
// which body a garbage genome nibble animates as would be a behaviour change
// hiding inside a move.
#define CFX_TEMPER_FALLBACK  1u

// Which behaviour row to read. `temper` is a genome Temperament class; anything
// out of range folds to CFX_TEMPER_FALLBACK, so a garbage genome cannot index
// past the table. The result is always < CFX_TEMPER_ROWS.
uint8_t cfx_temper_index(uint8_t temper, uint8_t corrupted);

// -----------------------------------------------------------------------------
//  THE GLITCH GATE
//
//  "1 in 8 frames" (the plan's words) is measured in MILLISECONDS here and not
//  in frames, because the caller's frame counter is UI_ANIM_FRAME_MS (420 ms)
//  while the panel is redrawn at FRAME_BUDGET_US (50 ms) - two different
//  "frames", and the glitch wants the fast one. A 60 ms slot is about one
//  drawn frame at 20 fps, so a lit slot reads as a single dropped frame and the
//  gate below lights roughly one slot in eight: a stutter a bit under twice a
//  second.
//
//  IT IS A HASH AND NOT slot % 8, so the stutter is irregular. That is a
//  deliberate look and not an accident, and tests/test_corruption.cpp measures
//  the RATE rather than pinning a pattern.
// -----------------------------------------------------------------------------
#define CFX_GLITCH_SLOT_MS   60u
#define CFX_GLITCH_ONE_IN    8u

// Which slot `now_ms` falls in. Exposed so a test can sweep slots rather than
// milliseconds, and so petfx can tell "same slot" from "next slot".
uint32_t cfx_glitch_slot(uint32_t now_ms);

// True on roughly one slot in CFX_GLITCH_ONE_IN. Pure in `now_ms` and `seed`:
// two pets on the same device glitch on different slots.
uint8_t cfx_glitch_on(uint32_t now_ms, uint32_t seed);

// -----------------------------------------------------------------------------
//  THE GLITCH ROWS
//
//  A rectangle, INCLUSIVE on all four sides, in absolute screen coordinates.
//  petfx.cpp hands it the INK box it just drew and published through
//  petfx_body_ink() - not the sprite box, which on a 40 px body reaches down
//  over the floor line and both shadow rows.
// -----------------------------------------------------------------------------
struct CfxRect {
  int16_t x0, y0, x1, y1;
};

// One XOR-noise row: a 1 px tall horizontal strip and the dither that fills it.
struct CfxRow {
  int16_t x;        // left edge, absolute
  int16_t y;        // the row, absolute
  uint8_t w;        // >= 1
  uint8_t level;    // n/16 for the Bayer dither; always odd (see the .cpp)
  uint8_t phase;    // 0..15, (dy << 2) | dx as render.h defines it
};

#define CFX_ROWS_MAX  3u

// Fill `out` with the rows to XOR this slot. Returns how many were written,
// 0..CFX_ROWS_MAX.
//
// THE CONTRACT, AND IT IS THE WHOLE POINT OF THE FUNCTION: every returned row
// satisfies  ink.x0 <= x  and  x + w - 1 <= ink.x1  and  ink.y0 <= y <= ink.y1.
// A degenerate or inverted rectangle returns 0 rows rather than a clamped
// guess. The caller may therefore paint what it is given without re-checking,
// and tests/test_corruption.cpp asserts the containment on the PIXELS a painter
// actually touched, not only on these five numbers.
//
// Returns 0 when `out` is null.
uint8_t cfx_rows(const CfxRect& ink, uint32_t now_ms, uint32_t seed, CfxRow* out);

#endif  // ER_UI_CORRUPT_FX_H
