// =============================================================================
//  PEBBLEBOL - ui/screen_link.h
//  LINK (spec section 6 SCR_LINK), migrated by P2-C11c over the old SOCIAL
//  screen.
//
//  SOCIAL was a BLE peer browser: it took RADIO_BLE on entry, advertised a
//  beacon, listed whatever it could hear and offered mating. Every one of
//  those is gone - the mating protocol was removed before Phase 2 and D2 chose
//  ESP-NOW over BLE for the session transport, so a screen built on BLE
//  advertisements is a screen built on a transport this firmware no longer
//  uses. Nothing here holds a radio.
//
//  What LINK will be - trade, battle and breeding between two Pebbles - is
//  Phase 7 (P7-C2). Until then the screen exists so the MENU entry goes
//  somewhere honest, and says which phase brings it to life.
//
//  PURE translation unit.
// =============================================================================
#ifndef PB_SCREEN_LINK_H
#define PB_SCREEN_LINK_H

#include <stdint.h>

#include "../core/nt_types.h"

// The screen-table hooks.
void link_render(void);
void link_input(Gesture g);

#endif  // PB_SCREEN_LINK_H
