// =============================================================================
//  PEBBLEBOL - game/box_sim.h
//  THE TWO OPERATIONS THAT TOUCH BOTH THE BOX AND THE RUNNING SIMULATION.
//
//  game/box.cpp stores the ten Pebbles BY VALUE inside the single GameState and
//  game/sim.cpp holds a RAW POINTER into that array (its g.pb). Neither module
//  knows the other - box.h must stay linkable without the simulation, and
//  sim.cpp must stay linkable without the Box - so every mutation that moves a
//  PebbleInstance in memory leaves sim's pointer stale unless somebody repairs
//  it. That somebody is this file: the smallest translation unit that is
//  allowed to include both headers.
//
//  It exists so the repair is TESTABLE. Composing box_swap() with sim_rebind()
//  inside ui.cpp would put it in a translation unit the host suite cannot
//  compile, and tests/test_box_sim.cpp would then be testing its own stub.
//
//  Pure module (plan 1.3 rule 2): no Arduino header, no I/O. Persistence is
//  still the caller's job - these calls change RAM, not flash.
// =============================================================================
#ifndef PB_BOX_SIM_H
#define PB_BOX_SIM_H

#include <stdint.h>

// box_swap(a, b) plus the pointer repair the swap forces.
//
// box_swap() exchanges the two slots' CONTENTS and carries box.active_slot with
// the creature, so when the active slot is one of the two the active Pebble
// ends up at the OTHER index while sim is still bound to the old address - i.e.
// the player keeps playing the Pebble that moved into the slot they left. This
// re-points the simulation at the creature it was already running, without
// resetting a single one of its accumulators (see sim_rebind()).
//
// Returns what box_swap() returned: false for an out-of-range slot or an
// unbound Box, in which case nothing at all happened and the caller must not
// persist anything.
bool box_sim_swap(uint8_t a, uint8_t b);

#endif  // PB_BOX_SIM_H
