// =============================================================================
//  ERRATA - minigames/games/ping_draw.cpp
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
    // *** CENTRED IN THE LIT HALF, NOT IN THE PANEL, SINCE THE FINAL REVIEW. ***
    // This was gfx_text_center(), which centres over the whole 128 px panel
    // while the slab it is supposed to sit in is 64 px wide - so the letter
    // ALWAYS straddled the midline and was never legible in either direction.
    // gfx_text_w(GF_HEAD, "B") is 6, so x was (128-6)/2 = 61 and the glyph
    // occupied 61..66: three columns on the slab and three on unlit ground.
    // tests/golden/screens/mg_ping_lit.pbm froze the broken picture as correct -
    // the letter is two visible columns of five, which nobody reading that
    // golden would take for a "B".
    //
    // On the panel it is worse than illegible, and that is the second reason
    // this is the site that had to move. ui/render.cpp puts the display in
    // setFontMode(0) - SOLID - for the whole session, so a glyph's 0-bits are
    // painted in the INVERSE of the draw colour; under GFX_ERASE that is LIT.
    // The half of the glyph hanging off the slab therefore became a bright mark
    // standing alone in the DARK half, which no golden contains at all (the
    // host fake draws transparent text). Putting the whole glyph box inside the
    // slab removes this site's exposure to that seam entirely: the background
    // pixels then land on ground that is already lit, which is the same "1 over
    // 1" coincidence every other text-over-ink site in the tree relies on.
    const char* const lbl = ping_target(c) ? "B" : "A";
    const int16_t tw = (int16_t)gfx_text_w(GF_HEAD, lbl);
    gfx_text(GF_HEAD, (int16_t)(x + ((int16_t)(OLED_W / 2) - tw) / 2),
             (int16_t)(UI_CONTENT_Y + 22), lbl);
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
