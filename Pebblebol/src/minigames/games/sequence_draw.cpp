// =============================================================================
//  PEBBLEBOL - minigames/games/sequence_draw.cpp
//  The DEVICE half of SEQUENCE. See ping_draw.cpp for the rule: gfx.h only.
// =============================================================================
#include "../../ui/gfx.h"
#include "../../ui/screen.h"
#include "../../core/strings_es.h"
#include "games.h"

void sequence_draw(const MgCtx& c)
{
  const int8_t lit = seq_lit_side(c);

  // Two panels. During playback one of them lights; while the player answers
  // both are outlines, so "showing" and "your turn" are never ambiguous - the
  // game is unplayable if the player cannot tell which half they are in.
  for (uint8_t side = 0; side < 2u; ++side) {
    const int16_t x = (int16_t)(side ? OLED_W / 2 + 2 : 2);
    const int16_t w = (int16_t)(OLED_W / 2 - 4);
    if (lit == (int8_t)side) gfx_fill(x, UI_CONTENT_Y, w, 26);
    else                     gfx_rect(x, UI_CONTENT_Y, w, 26);
  }

  // Progress: one pip per symbol of this level, filled as they are matched.
  const uint8_t len = seq_len_of(c);
  const uint8_t pos = seq_pos_of(c);
  for (uint8_t i = 0; i < len; ++i) {
    const int16_t x = (int16_t)(OLED_W / 2 - (int16_t)len * 3 + (int16_t)i * 6);
    if (i < pos) gfx_fill(x, (int16_t)(UI_CONTENT_Y + 30), 5, 5);
    else         gfx_rect(x, (int16_t)(UI_CONTENT_Y + 30), 5, 5);
  }

  gfx_text_center(GF_BODY, UI_CONTENT_BOTTOM,
                  S(seq_is_showing(c) ? STR_GM_READY : STR_GM_GO));
}
