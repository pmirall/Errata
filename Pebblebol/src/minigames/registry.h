// =============================================================================
//  PEBBLEBOL - minigames/registry.h
//  The SIXTH hook. minigames/manager.cpp is pure and owns the pure five; this
//  is the only place that pairs a game's logic with the drawing half it cannot
//  see, and the only file in the directory that includes ui/.
// =============================================================================
#ifndef PB_MG_REGISTRY_H
#define PB_MG_REGISTRY_H

#include "minigame.h"

// The run frame of whichever game is live. Draws nothing when idle; the
// chrome around it belongs to the GAME screen, not to a game.
void mg_draw_current(void);

#endif  // PB_MG_REGISTRY_H
