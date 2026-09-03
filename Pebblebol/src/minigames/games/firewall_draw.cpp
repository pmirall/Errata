// =============================================================================
//  PEBBLEBOL - minigames/games/firewall_draw.cpp
//  The DEVICE half of FIREWALL. See ping_draw.cpp for the rule: gfx.h only.
//
//  NO TEXT AT ALL. The header and the score tag are the manager's chrome, and
//  everything this frame has to say - which lane, how far down, blocked or
//  leaked, how the run is going - is said with shape. Lane L spans
//  x = 2 + 25*L for 24 px, leaving a 1 px gutter for the dividers.
// =============================================================================
#include "../../ui/gfx.h"
#include "../../ui/screen.h"
#include "games.h"

#define FWD_LANE_X(l)  ((int16_t)(2 + 25 * (l)))
#define FWD_LANE_W     24
#define FWD_TOP_Y      15      // where a packet enters
#define FWD_DROP       26      // how far it travels before impact
#define FWD_SHIELD_Y   46
#define FWD_WALL_Y     51

void firewall_draw(const MgCtx& c)
{
  // 1. THE HISTORY, one pip per packet: solid blocked, a hollow ring leaked, a
  //    dot not yet played. One glance gives the score AND which ones got past.
  for (uint8_t i = 0; i < fw_packets(); ++i) {
    const int16_t x = (int16_t)(44 + i * 5);
    switch (fw_pip(c, i)) {
      case 1u:  gfx_fill(x, 11, 3, 3); break;
      case 2u:  gfx_rect(x, 11, 3, 3); break;
      default:  gfx_pixel((int16_t)(x + 1), 12); break;
    }
  }

  // 2. THE FIELD. Hatched dividers, so they guide the eye without competing
  //    with the one solid thing on screen.
  for (uint8_t l = 1; l < fw_lanes(); ++l)
    gfx_dither_rect((int16_t)(FWD_LANE_X(l) - 1), FWD_TOP_Y, 1, 30, GFX_D50);

  const uint8_t outcome = fw_outcome(c);
  const int16_t px      = FWD_LANE_X(fw_lane(c));

  // 3. THE PACKET, while it is still falling. A solid body with a hole punched
  //    in it, so it reads as a packet rather than a blob against the hatching.
  if (outcome == 0u) {
    const int16_t y = (int16_t)(FWD_TOP_Y + (int32_t)fw_fall_pos(c) * FWD_DROP / 256);
    gfx_fill((int16_t)(px + 6), y, 11, 7);
    gfx_color(GFX_ERASE);
    gfx_fill((int16_t)(px + 10), (int16_t)(y + 2), 3, 3);
    gfx_color(GFX_DRAW);
  }

  // 4. THE SHIELD. Two prongs, so it reads as a catcher and not a floor tile -
  //    and at either end lane the prong sits flush against the panel edge,
  //    which is how the player is told they have hit the wall.
  const int16_t sx = FWD_LANE_X(fw_shield(c));
  gfx_fill(sx, FWD_SHIELD_Y, FWD_LANE_W, 4);
  gfx_fill(sx, (int16_t)(FWD_SHIELD_Y - 2), 2, 2);
  gfx_fill((int16_t)(sx + FWD_LANE_W - 2), (int16_t)(FWD_SHIELD_Y - 2), 2, 2);

  // 5. THE WALL being defended.
  gfx_hline(0, FWD_WALL_Y, OLED_W);

  // 6. THE OUTCOME, for the 250 ms gap. An inversion is the loudest available
  //    yes; a hole in the wall with something coming through it needs no
  //    caption. Both clear themselves when the next packet spawns.
  if (outcome == 1u) {
    gfx_invert_rect(sx, (int16_t)(FWD_SHIELD_Y - 2), FWD_LANE_W, 8);
  } else if (outcome == 2u) {
    gfx_color(GFX_ERASE);
    gfx_hline(px, FWD_WALL_Y, FWD_LANE_W);
    gfx_color(GFX_DRAW);
    gfx_fill((int16_t)(px + 6), (int16_t)(FWD_WALL_Y + 1), 11, 3);
  }
}
