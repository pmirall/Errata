// =============================================================================
//  ERRATA - minigames/games/delete_draw.cpp
//  The DEVICE half of DELETE. See ping_draw.cpp for the rule: gfx.h only.
//
//  NO TEXT AT ALL, which also sidesteps GF_TINY's ASCII-only clause. Filled
//  versus outlined is the loudest distinction a 1-bit panel has, so it is spent
//  on the one thing the player must read: corrupted or not. The fuses are what
//  make triage possible when two corrupted blocks share the board - which the
//  schedule guarantees will happen - and the charge pips are the standing
//  answer to "why did that press cost me", now that the toast fires only once.
// =============================================================================
#include "../../ui/gfx.h"
#include "../../ui/screen.h"
#include "games.h"

#define DELD_SLOT_X(i)  ((int16_t)(4 + 24 * (i)))
#define DELD_SLOT_W     22
#define DELD_SLOT_Y     14
#define DELD_SLOT_H     22
#define DELD_FUSE_Y     38
#define DELD_CARET_Y    41
#define DELD_PIP_Y      48

void delete_draw(const MgCtx& c)
{
  for (uint8_t i = 0; i < del_slots(); ++i) {
    const int16_t x = DELD_SLOT_X(i);
    switch (del_kind_at(c, i)) {
      case DEL_KIND_HEALTHY:
        // Quiet, structurally intact: leave it alone.
        gfx_rect(x, DELD_SLOT_Y, DELD_SLOT_W, DELD_SLOT_H);
        break;

      case DEL_KIND_CORRUPT: {
        // Solid, shot through with jittering tears. The jitter is a pure
        // function of c.t_ms, so this stays a const draw with no state.
        gfx_fill(x, DELD_SLOT_Y, DELD_SLOT_W, DELD_SLOT_H);
        gfx_color(GFX_ERASE);
        for (uint8_t k = 0; k < 3u; ++k) {
          const int16_t off = (int16_t)((c.t_ms / 50u + 7u * i + 3u * k) % 18u);
          gfx_fill(x, (int16_t)(DELD_SLOT_Y + off), DELD_SLOT_W, 2);
        }
        gfx_color(GFX_DRAW);
        break;
      }

      default:
        // Nothing here - a dashed floor, which can never be mistaken for a
        // block the player might spend a charge on.
        for (uint8_t d = 0; d < 5u; ++d)
          gfx_pixel((int16_t)(x + 2 + d * 4), (int16_t)(DELD_SLOT_Y + DELD_SLOT_H - 1));
        break;
    }

    // THE FUSE, under every occupied slot of either type. It drains right to
    // left over the block's life; the short bar is the one to kill first.
    if (del_kind_at(c, i) != DEL_KIND_EMPTY)
      gfx_fill(x, DELD_FUSE_Y,
               (int16_t)(DELD_SLOT_W * del_life_pct(c, i) / 100u), 2);
  }

  // THE CARET. Deliberately an underline rather than an inversion of the block
  // itself, which would flip a corrupted block into looking healthy.
  gfx_fill(DELD_SLOT_X(del_cursor(c)), DELD_CARET_Y, DELD_SLOT_W, 3);

  // THE CHARGES: the whole economy of the game in one glance, emptying left to
  // right, and the reason the run ends when the last one goes hollow.
  for (uint8_t i = 0; i < del_charges_max(); ++i) {
    const int16_t x = (int16_t)(41 + i * 7);
    if (i < del_charges(c)) gfx_fill(x, DELD_PIP_Y, 4, 6);
    else                    gfx_rect(x, DELD_PIP_Y, 4, 6);
  }
}
