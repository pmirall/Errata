// =============================================================================
//  PEBBLEBOL - ui/screen_menu.h
//  MENU: the horizontal icon ring, in the spec section 8 order
//
//      PEBBLE  CARE  PLAY  BOX  NETWORK  LINK  SETTINGS
//
//  MENU_ITEM_COUNT (config.h) is 7, S_MENU(i) indexes exactly those seven
//  labels and strings_es.h static_asserts the two against each other.
// =============================================================================
#ifndef PB_SCREEN_MENU_H
#define PB_SCREEN_MENU_H

#include <stdint.h>

#include "../core/nt_types.h"

// The ring's item order. Public because the destinations are the spec's, not
// this file's, and test_screens drives the cursor through them.
enum MenuItem : uint8_t {
  MENU_PEBBLE = 0,
  MENU_CARE,
  MENU_PLAY,
  MENU_BOX,
  MENU_NETWORK,
  MENU_LINK,
  MENU_SETTINGS
};

void    menu_enter(void);
void    menu_render(void);
void    menu_input(Gesture g);
uint8_t menu_cursor(void);      // for the tests; the ring is otherwise private

#endif  // PB_SCREEN_MENU_H
