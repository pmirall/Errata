// =============================================================================
//  Pebblebol host tests - fakes/gfx_fb.h
//  The HOST side of the ui/gfx.h seam: a real 128x64 1-bit framebuffer, fixed
//  advance font metrics, and a recorder that counts every primitive which
//  asked to touch a pixel outside the panel.
//
//  This is what makes spec section 63 ("every screen tested at the actual
//  physical resolution") checkable without hardware: tests/test_screens.cpp
//  renders a migrated screen into this buffer, asserts fb_oob() == 0 and
//  compares the result against tests/golden/screens/<screen>_<state>.pbm.
//
//  Text is NOT glyph-accurate and does not pretend to be: each codepoint is
//  drawn as a fixed-width cell whose row pattern is a function of the
//  codepoint, so the golden is sensitive to the STRING and to its LAYOUT -
//  position, advance, wrapping, truncation, overflow - which is exactly what a
//  128x64 panel gets wrong.
// =============================================================================
#ifndef NT_GFX_FB_H
#define NT_GFX_FB_H

#include <stdint.h>

#define FB_W 128
#define FB_H 64

// Blank the buffer and forget every recorded out-of-bounds primitive.
void fb_reset(void);

// One pixel, 0 or 1. Out-of-range reads answer 0.
int  fb_get(int x, int y);

// How many primitives asked for a pixel outside 0..127 / 0..63, and what the
// first one was ("" when there was none).
uint32_t    fb_oob(void);
const char* fb_oob_first(void);

// Golden files, ASCII PBM (P1), one 128-character row per line.
bool fb_write_pbm(const char* path);

// Pixels that differ from the golden, or -1 when the file cannot be read.
int  fb_diff_pbm(const char* path);

// THE WORK COUNTERS (P10-C2). fb_ops() is one per leaf primitive - pixel,
// hline, vline, rect, fill, xbm, dither and one per GLYPH - so gfx_invert_rect,
// which delegates to gfx_fill, is charged once and the widgets in
// ui/gfx_widgets.cpp decompose into the primitives they really call.
// fb_pixels() is one per pixel actually written INSIDE the panel.
//
// THEY ARE A WORK REGRESSION DETECTOR AND NOT A TIME MEASUREMENT, and the
// difference is written out at length in gfx_fb.cpp above the counters. Do not
// attach a millisecond to either of them.
uint32_t fb_ops(void);
uint32_t fb_pixels(void);

// The buffer as text on stdout, for a failing test to be readable.
void fb_dump(void);

#endif  // NT_GFX_FB_H
