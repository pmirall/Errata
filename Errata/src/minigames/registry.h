// =============================================================================
//  ERRATA - minigames/registry.h
//  The SIXTH hook. minigames/manager.cpp is pure and owns the pure five; this
//  is the only place that pairs a game's logic with the drawing half it cannot
//  see.
//
//  IT IS NOT A DEVICE TRANSLATION UNIT, and both banners here said it was until
//  the final review ("the only file in the directory that includes ui/", and
//  registry.cpp's "DEVICE translation unit"). registry.cpp includes exactly
//  three headers - this one, games/games.h and manager.h - and reaches no ui/
//  header at all; the six draw functions are forward-declared locally. That
//  false sentence was the stated reason the file was not host-tested, and it is
//  why its table's ORDER was verified by nothing. tests/test_minigames.cpp links
//  it now.
// =============================================================================
#ifndef ER_MG_REGISTRY_H
#define ER_MG_REGISTRY_H

#include "minigame.h"

// The run frame of whichever game is live. Draws nothing when idle; the
// chrome around it belongs to the GAME screen, not to a game.
void mg_draw_current(void);

// THE PAIRING THIS FILE ACTUALLY USES, so a host test can check it instead of
// writing its own. Returns nullptr for an out-of-range id.
//
// Without this the only way to check the table was to re-derive it, which is
// what tests/test_screens.cpp does at each of its six mg_snapshot() call sites -
// so the eleven minigame goldens proved the pairing THAT FILE wrote, and a
// permuted DEFS[] left every one of them green while the device drew one game's
// frame from another game's context.
const MinigameDef* mg_def_at(uint8_t id);

#endif  // ER_MG_REGISTRY_H
