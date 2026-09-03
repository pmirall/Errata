// =============================================================================
//  PEBBLEBOL - minigames/registry.cpp
//  DEVICE translation unit: logic + draw, paired. See registry.h.
// =============================================================================
#include "registry.h"

#include "games/games.h"
#include "manager.h"

void ping_draw(const MgCtx& c);
void sequence_draw(const MgCtx& c);

// Indexed by MgId, exactly like the pool in manager.cpp and exactly like the
// PLAY screen's rows. The static_assert is what keeps the three in step: get
// it wrong and a game draws its neighbour's frame.
static const MinigameDef DEFS[] = {
  { &MG_PING,     ping_draw     },
  { &MG_SEQUENCE, sequence_draw },
};
static_assert(sizeof(DEFS) / sizeof(DEFS[0]) == (size_t)MG_ID_COUNT,
              "every MgId needs exactly one MinigameDef");

void mg_draw_current(void)
{
  const MgLogic* g = mgr_logic();
  if (g == nullptr) return;
  if (g->id >= (uint8_t)MG_ID_COUNT) return;
  const MinigameDef& d = DEFS[g->id];
  if (d.draw) d.draw(mgr_ctx());
}
