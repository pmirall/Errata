// =============================================================================
//  ERRATA - ui/screen_dex.h
//
//  THE WIKI. Sixty rows, one per species, and how far you have got.
//  PURE translation unit.
//
//  WHAT IT IS FOR. The Box holds ten and the roster is sixty, so without this
//  the other fifty exist only as creatures that got away. A list with holes in
//  it is a different object from a list of what you own: it is the reason to
//  walk somewhere else, which is the mechanic the whole exploration loop is
//  built on (spec section 21).
//
//  UNDISCOVERED ROWS KEEP THEIR PLACE. A species you have not met is drawn as
//  its NUMBER and a filled silhouette, not hidden - a gap you can see is what
//  makes it a gap. It is also why the list is indexed by species id and never
//  compacted: row 34 is species 34 whether or not you have met it, so the
//  cursor means the same thing on two devices at different stages.
//
//  THREE AT A TIME, WHICH IS THE PANEL'S DOING. 128x64 holds one 24x24 body
//  with its name and number, plus a strip of the one above and the one below
//  so the list reads as a list rather than as a slideshow.
// =============================================================================
#ifndef ER_SCREEN_DEX_H
#define ER_SCREEN_DEX_H

#include <stdint.h>

#include "../core/nt_types.h"

void dex_screen_enter(void);
void dex_screen_render(void);
void dex_screen_input(Gesture g);

// Which row the cursor is on, as a SPECIES ID (1..SPECIES_TABLE_COUNT). For the
// tests and for a snapshot name; nothing outside may write it.
uint8_t dex_screen_cursor(void);

#endif  // ER_SCREEN_DEX_H
