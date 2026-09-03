// =============================================================================
//  PEBBLEBOL - minigames/games/ping_draw.cpp
//  The DEVICE half of PING: the run frame, and nothing else. The chrome
//  (header, score tag, READY/GO, the result card, the affordance strip) is the
//  manager's and is drawn once for every game rather than six times.
//
//  Drawing ONLY through ui/gfx.h. No u8g2, no render.h: this file is what
//  keeps games/ping_logic.cpp free of both, which is what lets the host run it.
// =============================================================================
#include "../../ui/gfx.h"
#include "../../ui/screen.h"
#include "../../core/strings_es.h"
#include "games.h"

#include <stdio.h>

void ping_draw(const MgCtx& c)
{
  if (ping_is_lit(c)) {
    // Half the screen goes solid on the side the player must press. It is the
    // loudest thing a 1-bit panel can do, which is what a reaction test needs.
    const int16_t x = ping_target(c) ? (int16_t)(OLED_W / 2) : (int16_t)0;
    gfx_fill(x, UI_CONTENT_Y, OLED_W / 2, 32);
    gfx_color(GFX_ERASE);
    gfx_text_center(GF_HEAD, (int16_t)(UI_CONTENT_Y + 22),
                    ping_target(c) ? "B" : "A");
    gfx_color(GFX_DRAW);
  } else {
    gfx_text_center(GF_HEAD, (int16_t)(UI_CONTENT_Y + 20), S(STR_GM_READY));
  }

  // The last reaction time, which is the only feedback that teaches the game.
  char buf[16];
  const uint16_t rt = ping_last_ms(c);
  if (rt) snprintf(buf, sizeof(buf), "%u ms", (unsigned)rt);
  else    snprintf(buf, sizeof(buf), "--");
  gfx_text_center(GF_BODY, UI_CONTENT_BOTTOM, buf);
}
