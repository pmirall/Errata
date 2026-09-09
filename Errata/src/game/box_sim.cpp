// =============================================================================
//  ERRATA - game/box_sim.cpp
//  See box_sim.h. PURE translation unit.
// =============================================================================
#include "box_sim.h"

#include "box.h"
#include "sim.h"

bool box_sim_swap(uint8_t a, uint8_t b)
{
  const uint8_t was = box_active();
  if (!box_swap(a, b)) return false;

  // Only a swap that MOVED the active Bug can have invalidated sim's
  // pointer; every other swap exchanges two stored slots the simulation is not
  // looking at. box_swap() has already carried box.active_slot across, so the
  // creature the player is carrying is at box_active() now.
  if (was == a || was == b) {
    BugInstance* now = box_slot(box_active());
    if (now) sim_rebind(*now);
  }
  return true;
}
