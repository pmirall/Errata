// =============================================================================
//  ERRATA - ui/screen_manual.h
//
//  THE USER MANUAL, AS A PICTURE OF A LINK. PURE translation unit.
//
//  A handheld with two buttons and a 128x64 panel cannot hold a manual, and a
//  printed card gets lost. What the device CAN do is hand the player the
//  address of one in the only form a phone reads without typing: a QR.
//
//  IT IS THE SIMPLEST SCREEN IN THE PRODUCT AND THAT IS THE DESIGN. No radio,
//  no timeout, no state that can go stale. core/config.h's MANUAL_URL is a
//  compile-time constant, so the symbol is encoded ONCE on the way in and the
//  render is a blit. Compare ui/screen_creator.h, which needs an access point,
//  a PIN, an idle deadline and a power-ladder hold to show the same picture -
//  none of which this screen has any business inheriting just because the two
//  both draw a QR.
//
//  THE ENCODE CAN FAIL AND THE SCREEN SAYS SO. qr_encode() refuses a payload
//  that will not fit the versions ui/qr.cpp implements; core/config.h
//  static_asserts MANUAL_URL against the version-2 budget so that cannot happen
//  by accident, but a build that changed the URL and the encoder together would
//  still land here, and an empty frame with an ellipsis is a screen that admits
//  it rather than one that draws a symbol nobody can scan.
// =============================================================================
#ifndef ER_SCREEN_MANUAL_H
#define ER_SCREEN_MANUAL_H

#include <stdint.h>

#include "../core/nt_types.h"

void manual_enter(void);
void manual_render(void);
void manual_input(Gesture g);

// The NUL-terminated payload the symbol encodes, or "" if the encode failed.
// Exported for the same reason ui/screen_creator.h exports creator_payload():
// a host test has no scanner, and "the QR points at the manual" is a claim only
// a reader of the bytes can check.
const char* manual_payload(void);

#endif  // ER_SCREEN_MANUAL_H
