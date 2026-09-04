// =============================================================================
//  PEBBLEBOL - ui/screen_creator.h
//  CREATOR (spec section 6 SCR_CREATOR), migrated by P2-C11c over the old QR
//  screen.
//
//  WHAT IT IS NOW: the one screen that owns the Wi-Fi station. It asks for the
//  radio on the way in and gives it back on the way out (plan section 2 row
//  G4, "radio OFF by default"), and while it is up it prints the two things a
//  phone needs to reach the device - the URL as a QR symbol and the 4-digit
//  PIN in numbers big enough to read at arm's length.
//
//  WHAT IT IS NOT YET: the creator itself. Sprite import, the custom-species
//  editor and everything else the page will serve is Phase 8, so the screen
//  says "Creador - Fase 8" rather than implying a page that is not there.
//
//  PURE translation unit: gfx.h, qr.h's portable encoder half, the strings and
//  the two ui.h seams below. The QR modules are painted here with gfx_fill()
//  instead of qr_draw(), which is fenced behind #ifdef ARDUINO - that is what
//  lets the symbol be snapshot-tested at the real 128x64.
//
//  GEOMETRY (config.h QR_BOX_*): a 62 px symbol box at (0,1) with a 3-module
//  quiet zone, and a 62 px text column at x = 65. The box owns the rows the
//  affordance strip would use, so invariant 6's hint moves into that column -
//  the one documented exception, inherited from the QR screen.
// =============================================================================
#ifndef PB_SCREEN_CREATOR_H
#define PB_SCREEN_CREATOR_H

#include <stdint.h>

#include "../core/config.h"
#include "../core/nt_types.h"

// Everything the screen needs to know about the radio and the web server, in
// one fill. ui.cpp answers it; on the host tests/test_screens.cpp does.
#define CREATOR_TEXT_MAX  QR_TEXT_MAX
struct CreatorInfo {
  uint8_t  ap_up;                    // the device's own access point is serving
  // `sta_up` was here. It meant "joined the user's network", which this
  // firmware cannot do since P5-C1 - the station path is deleted, not disabled
  // - so the field and the screen branch that read it are gone rather than
  // permanently false.
  uint8_t  reserved;                 // must be 0
  uint16_t pin;                      // web_pin(), printed % 10000
  char     ssid[24];                 // the AP's SSID, when ap_up
  char     ip[24];                   // whichever address is live
  char     url[CREATOR_TEXT_MAX];    // net_url() with the PIN already in it
};

// The screen-table hooks.
void creator_enter(void);
void creator_update(uint32_t now_ms);
void creator_render(void);
void creator_input(Gesture g);
void creator_leave(void);

// Which symbol is on screen: 0 = the URL, 1 = "join this network". Exposed for
// the tests and for the snapshot names.
uint8_t creator_variant(void);
void    creator_set_variant(uint8_t v);

#endif  // PB_SCREEN_CREATOR_H
