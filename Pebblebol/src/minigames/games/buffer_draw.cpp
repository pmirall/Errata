// =============================================================================
//  PEBBLEBOL - minigames/games/buffer_draw.cpp
//  The DEVICE half of BUFFER. See ping_draw.cpp for the rule: gfx.h only.
//
//  The zone accessors use the SAME derived step count the logic scores with, so
//  the zone can never render at a width it is not being judged against - during
//  the lead-in it draws at its full opening width rather than at nothing.
//
//  SOLID versus OUTLINE is the whole readout: it says the one thing being
//  scored, at the instant it is scored, with no colour and no sound.
// =============================================================================
#include "../../ui/gfx.h"
#include "../../ui/screen.h"
#include "../../core/strings_es.h"
#include "games.h"

#define BUFD_X0        4       // screen x of track position 0
#define BUFD_TRACK_Y  26
#define BUFD_TRACK_H  14
#define BUFD_BAR_Y    48

void buffer_draw(const MgCtx& c)
{
  const int16_t cx = (int16_t)(BUFD_X0 + buf_cursor_px(c));
  const int16_t zx = (int16_t)(BUFD_X0 + buf_zone_lo_px(c));
  const int16_t zw = (int16_t)(buf_zone_hi_px(c) - buf_zone_lo_px(c));

  // 1. The lead-in, which is the only cue that the fill bar has started
  //    counting. It disappears at 1.5 s and never comes back.
  if (!buf_is_live(c))
    gfx_text_center(GF_BODY, (int16_t)(UI_CONTENT_Y + 6), S(STR_GM_READY));

  // 2. THE CALIPER, drawn above and below the track so the zone slab can never
  //    hide it and no XOR is needed to keep both visible.
  gfx_fill((int16_t)(cx - 2), 20, 5, 3);
  gfx_vline(cx, 23, 3);
  gfx_vline(cx, 40, 3);
  gfx_fill((int16_t)(cx - 2), 43, 5, 3);

  // 3. THE TRACK.
  gfx_rect(3, BUFD_TRACK_Y, (int16_t)(buf_track_px() + 2), BUFD_TRACK_H);

  // 4. THE SAFE ZONE. Solid when the cursor is inside it, an outline when it is
  //    not - unmistakable at arm's length over a block 24 px wide narrowing to
  //    16 across the run.
  if (buf_is_inside(c)) gfx_fill(zx, (int16_t)(BUFD_TRACK_Y + 2), zw, 10);
  else                  gfx_rect(zx, (int16_t)(BUFD_TRACK_Y + 2), zw, 10);

  // 5. THE BUFFER FILLING - the only persistent feedback in the game. It creeps
  //    forward on every step spent inside and freezes on every step spent
  //    outside, so "continuous, not events" is legible from the screen alone.
  //    Driven by c.score, which the logic rewrites from `inside` every scored
  //    step, so the bar cannot disagree with the number in the header.
  gfx_rect(3, BUFD_BAR_Y, (int16_t)(buf_track_px() + 2), 6);
  gfx_fill(BUFD_X0, (int16_t)(BUFD_BAR_Y + 1),
           (int16_t)((uint32_t)buf_track_px() * c.score / MG_SCORE_MAX), 4);
}
