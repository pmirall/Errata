// =============================================================================
//  ERRATA - ui/qr_paint.h
//
//  ONE PAINTER FOR EVERY QR ON THIS DEVICE. PURE translation unit.
//
//  It was a `static void draw_symbol()` inside ui/screen_creator.cpp until the
//  MANUAL screen needed the same picture. Copying twenty-five lines into a
//  second screen would have been two copies of one rule - the lit-paper
//  inversion, the quiet zone, the module scale clamp and the horizontal run
//  merge - and the two would have drifted the first time one of them was
//  tuned. This project has paid for that shape before.
//
//  WHY LIT PAPER AND NOT DARK MODULES. On an OLED a set pixel is WHITE, so the
//  symbol is drawn as a filled white square - quiet zone included - and the
//  dark modules are ERASED out of it. A scanner needs the quiet zone to be the
//  light colour, and on a panel whose background is black the only way to get
//  one is to paint it.
// =============================================================================
#ifndef ER_QR_PAINT_H
#define ER_QR_PAINT_H

#include <stdint.h>

#include "qr.h"

// Paints `modules` (as qr_encode() filled it, `size` modules a side) into the
// box at (x, y) of `box_px` pixels a side.
//
// THE SCALE IS CLAMPED TO 1..3 PIXELS PER MODULE and the clamp is the whole
// reason a caller cannot get this wrong: at 0 the symbol would vanish, and the
// box is what bounds it above. A payload that needs a version too large for
// `box_px` at 2 px/module silently falls to 1, which is legible on a phone at
// close range and is the honest outcome - the alternative is drawing outside
// the box the caller reserved.
//
// `size == 0` means "nothing has been encoded": an empty frame with an ellipsis
// in it, so a screen waiting for a payload looks like it is waiting rather than
// like it is broken.
void qrp_paint(const uint8_t* modules, uint8_t size,
               int16_t x, int16_t y, int16_t box_px);

#endif  // ER_QR_PAINT_H
