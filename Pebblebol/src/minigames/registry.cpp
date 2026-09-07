// =============================================================================
//  PEBBLEBOL - minigames/registry.cpp
//  DEVICE translation unit: logic + draw, paired. See registry.h.
// =============================================================================
#include "registry.h"

#include "games/games.h"
#include "manager.h"

void ping_draw(const MgCtx& c);
void sequence_draw(const MgCtx& c);
void packet_flood_draw(const MgCtx& c);
void firewall_draw(const MgCtx& c);
void buffer_draw(const MgCtx& c);
void delete_draw(const MgCtx& c);

// Indexed by MgId, exactly like the pool in manager.cpp and exactly like the
// PLAY screen's rows.
//
// *** THE static_assert BELOW COUNTS ROWS. IT DOES NOT CHECK THE ORDER. ***
// The comment that used to stand here said "the static_assert is what keeps the
// three in step: get it wrong and a game draws its neighbour's frame" - and the
// second half was right while the first was not. MEASURED at the final review:
// swapping two rows of this table compiles clean under -Wall -Wextra -Werror,
// satisfies the length assert, and passes the whole gate; MG_PING then draws
// SEQUENCE's two panels and pip row while PING's logic runs, and
// mg_draw_current() hands one game's MgCtx to another game's draw half, which
// reinterprets MgCtx::state as a different private struct.
//
// It was invisible to everything. This file is compiled by no host binary, and
// tests/test_screens.cpp re-derives the pairing BY HAND at each of its six call
// sites - mg_snapshot(MG_PING, ping_draw, ...) - so the eleven minigame goldens
// prove the pairing test_screens.cpp wrote, not the pairing that ships.
// ui/screen_care.cpp had already diagnosed this exact gap IN WRITING for its own
// table ("neither can see a row in the WRONG PLACE, which is exactly the mistake
// that matters here") and nobody came back here.
//
// A constexpr fold cannot close it: MG_PING and its siblings are extern const
// with no visible initialiser, so DEFS[i].logic->id is not a constant
// expression. So it is closed twice instead, in the two places that CAN see it:
//   - tests/test_minigames.cpp asserts DEFS[i].logic == mg_logic_by_id(i) for
//     every MgId, over this real table, in a host binary that now links this
//     file (which is also why registry.h's "DEVICE translation unit" banner was
//     wrong: this file reaches no ui/ header and is perfectly host-compilable);
//   - and mg_draw_current() below fails SAFE rather than drawing a neighbour.
static const MinigameDef DEFS[] = {
  { &MG_PING,          ping_draw         },
  { &MG_SEQUENCE,      sequence_draw     },
  { &MG_PACKET_FLOOD,  packet_flood_draw },
  { &MG_FIREWALL,      firewall_draw     },
  { &MG_BUFFER,        buffer_draw       },
  { &MG_DELETE,        delete_draw       },
};
static_assert(sizeof(DEFS) / sizeof(DEFS[0]) == (size_t)MG_ID_COUNT,
              "every MgId needs exactly one MinigameDef");

// The row this MgId is paired with, so a host test can check the pairing this
// file actually uses rather than re-deriving one of its own.
const MinigameDef* mg_def_at(uint8_t id)
{
  if (id >= (uint8_t)MG_ID_COUNT) return nullptr;
  return &DEFS[id];
}

void mg_draw_current(void)
{
  const MgLogic* g = mgr_logic();
  if (g == nullptr) return;
  if (g->id >= (uint8_t)MG_ID_COUNT) return;
  const MinigameDef& d = DEFS[g->id];
  // FAIL SAFE, NOT SIDEWAYS. One pointer compare per frame, and it is the
  // difference between a blank content area and a game whose frame belongs to a
  // different game, drawn from a context whose bytes mean something else. A
  // mispaired table is a programming error; drawing the neighbour is the worst
  // possible way to report one.
  if (d.logic != g) return;
  if (d.draw) d.draw(mgr_ctx());
}
