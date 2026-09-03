// =============================================================================
//  PEBBLEBOL - minigames/games/packet_flood_draw.cpp
//  The DEVICE half of PACKET FLOOD. See ping_draw.cpp for the rule: gfx.h only,
//  no u8g2 and no render.h, which is what keeps packet_flood_logic.cpp free of
//  both and therefore runnable on the host.
//
//  A ROUTER with two labelled MOUTHS, fed by a backlog. The mouths are flush to
//  the screen edges and carry no letters of their own - their POSITION is which
//  button they are, and their DIGIT is the address they accept. Everything
//  comes from an accessor, so this file re-derives no rule: it does not know
//  what the schedule is, when the reroute happens or which side is correct.
// =============================================================================
#include "../../ui/gfx.h"
#include "../../ui/screen.h"
#include "../../core/strings_es.h"
#include "games.h"

#define PFD_ROUTER_X   46
#define PFD_ROUTER_W   36
#define PFD_MOUTH_W    34
#define PFD_ROW_Y      42
#define PFD_ROW_H      13

// ONE DECIMAL DIGIT, without stdio. Every number this file prints is a single
// digit - the addresses come from a five-element table of 1/2/4/7/9 and the
// backlog overflow is "+1" - so snprintf() was pulling <stdio.h> and a whole
// format parser into a device translation unit to do the work of one addition.
static void pf_digit_str(char* buf, uint8_t d)
{
  buf[0] = (char)('0' + (d % 10u));
  buf[1] = '\0';
}

static void pf_mouth(const MgCtx& c, uint8_t side)
{
  char d[4];
  pf_digit_str(d, pf_mouth_digit(c, side));
  const int16_t x = side ? (int16_t)(OLED_W - PFD_MOUTH_W) : (int16_t)0;
  gfx_text(GF_HEAD,
           (int16_t)(x + (PFD_MOUTH_W - (int16_t)gfx_text_w(GF_HEAD, d)) / 2),
           (int16_t)(PFD_ROW_Y + 10), d);
}

void packet_flood_draw(const MgCtx& c)
{
  char buf[4];

  // 1. PROGRESS. One cell per packet: solid once resolved, a dot while pending.
  for (uint8_t i = 0; i < pf_packets(); ++i) {
    const int16_t x = (int16_t)(28 + i * 4);
    if (i < pf_head(c)) gfx_fill(x, 11, 3, 3);
    else                gfx_pixel((int16_t)(x + 1), 12);
  }

  const uint8_t q = pf_queue_len(c);

  // 2. THE BACKLOG, stacked upward behind the router. Two are drawn; a fourth
  //    live packet becomes a "+1", which is all PF_MAX_ALIVE allows.
  for (uint8_t slot = 1; slot <= 2u; ++slot) {
    if (q <= slot) continue;
    const int16_t y = (int16_t)(slot == 2u ? 15 : 25);
    gfx_rect(52, y, 24, 9);
    pf_digit_str(buf, pf_digit_at(c, slot));
    gfx_text_center(GF_BODY, (int16_t)(y + 7), buf);
  }
  if (q > 3u) {
    // PF_MAX_ALIVE is 4, so this is only ever "+1" - but the string is built
    // rather than hardcoded, because a future gap change is allowed to make it
    // "+2" and the static_assert in the logic is what would let it.
    char more[3];
    more[0] = '+';
    pf_digit_str(more + 1, (uint8_t)(q - 3u));
    gfx_text(GF_TINY, 80, 21, more);           // ASCII only, as GF_TINY requires
  }

  // 3. THE LIFE BAR of the head packet. The one moving thing on the panel, and
  //    the cue to let an unreadable packet fall rather than guess at it.
  if (q) {
    gfx_rect(PFD_ROUTER_X, 36, PFD_ROUTER_W, 5);
    gfx_fill((int16_t)(PFD_ROUTER_X + 1), 37,
             (int16_t)(34u * pf_life_left_ms(c) / pf_life_ms()), 3);
  }

  // 4. THE ROUTER, with the head packet's address in it. An empty lane is
  //    hatched instead, so "nothing to route" and "route this" are never
  //    confusable.
  gfx_rect(PFD_ROUTER_X, PFD_ROW_Y, PFD_ROUTER_W, PFD_ROW_H);
  if (q) {
    pf_digit_str(buf, pf_digit_at(c, 0));
    gfx_text_center(GF_HEAD, (int16_t)(PFD_ROW_Y + 10), buf);
  } else {
    gfx_dither_rect((int16_t)(PFD_ROUTER_X + 1), (int16_t)(PFD_ROW_Y + 1),
                    (int16_t)(PFD_ROUTER_W - 2), (int16_t)(PFD_ROW_H - 2),
                    GFX_D25);
  }

  // 5. THE TWO MOUTHS, level with the router so the fork reads as a fork.
  gfx_rect(0, PFD_ROW_Y, PFD_MOUTH_W, PFD_ROW_H);
  gfx_rect((int16_t)(OLED_W - PFD_MOUTH_W), PFD_ROW_Y, PFD_MOUTH_W, PFD_ROW_H);
  gfx_hline(PFD_MOUTH_W, (int16_t)(PFD_ROW_Y + 6), (int16_t)(PFD_ROUTER_X - PFD_MOUTH_W));
  gfx_hline((int16_t)(PFD_ROUTER_X + PFD_ROUTER_W), (int16_t)(PFD_ROW_Y + 6),
            (int16_t)(OLED_W - PFD_MOUTH_W - PFD_ROUTER_X - PFD_ROUTER_W));
  pf_mouth(c, MG_SIDE_L);
  pf_mouth(c, MG_SIDE_R);

  // 6. THE REROUTE: both mouths blink inverted together, which is the visual
  //    half of the announcement (ui.cpp toasts the other half).
  if (pf_swap_flash(c)) {
    gfx_invert_rect(0, PFD_ROW_Y, PFD_MOUTH_W, PFD_ROW_H);
    gfx_invert_rect((int16_t)(OLED_W - PFD_MOUTH_W), PFD_ROW_Y,
                    PFD_MOUTH_W, PFD_ROW_H);
  }

  // 7. WHAT JUST HAPPENED, for 150 ms. A clean send is the loudest thing on the
  //    panel; a misroute is the same mouth HATCHED, because "it went somewhere"
  //    and "it went somewhere wrong" have to be different textures when there is
  //    no colour to spend; a lost packet marks the router it fell out of.
  const uint8_t last = pf_last(c);
  if (last == PF_LAST_GOOD || last == PF_LAST_BAD) {
    const uint8_t side = pf_last_side(c);
    const int16_t x    = side ? (int16_t)(OLED_W - PFD_MOUTH_W) : (int16_t)0;
    if (last == PF_LAST_GOOD) {
      gfx_fill(x, PFD_ROW_Y, PFD_MOUTH_W, PFD_ROW_H);
      gfx_color(GFX_ERASE);
      pf_mouth(c, side);
      gfx_color(GFX_DRAW);
    } else {
      gfx_dither_rect((int16_t)(x + 1), (int16_t)(PFD_ROW_Y + 1),
                      (int16_t)(PFD_MOUTH_W - 2), (int16_t)(PFD_ROW_H - 2),
                      GFX_D50);
    }
  } else if (last == PF_LAST_LOST) {
    gfx_dither_rect((int16_t)(PFD_ROUTER_X + 1), (int16_t)(PFD_ROW_Y + 1),
                    (int16_t)(PFD_ROUTER_W - 2), (int16_t)(PFD_ROW_H - 2),
                    GFX_D50);
  }
}
