// =============================================================================
//  PEBBLEBOL - ui/battle_renderer.cpp
//  See battle_renderer.h. PURE translation unit.
// =============================================================================
#include "battle_renderer.h"

#include <stdio.h>

#include "../core/strings_es.h"
#include "../data/sprites.h"
#include "gfx.h"
#include "screen.h"
#include "xbm_mirror.h"

// The mirror scratch. FILE SCOPE, not a local: 72 B on a UI hook's stack is
// harmless, but "no per-frame heap" is a claim about this whole screen and a
// buffer whose lifetime a reader has to reason about is where that claim gets
// quietly broken later. Written before it is read on every mirrored draw.
static uint8_t s_mirror[BR_BODY_BYTES];

static_assert(BR_BODY_BYTES == 72, "a 24x24 XBM frame is 3 bytes a row");
static_assert(BR_BODY_W <= XBM_MIRROR_MAX_W, "the flip refuses a wider frame");
static_assert(BR_FOE_BODY_Y >= UI_CONTENT_Y, "the foe stands above the content band");
// THE SHADOW COUNTS. The first draft asserted the BODY clear of the transcript
// and put the two rows of contact shadow straight through the top of it: at
// BR_YOU_BODY_Y 23 the shadow landed on rows 47-48 and the text starts at 48,
// which the intro golden happened not to show only because "¡A COMBATIR!" is
// short enough to stop before the shadow's leftmost column.
static_assert(BR_YOU_BODY_Y + BR_BODY_H + BR_SHADOW_H - 1 < BR_MSG_BASE - GFX_ASC_BODY,
              "the player's body or its shadow would sit on the transcript line");
static_assert(BR_MSG_BASE <= UI_CONTENT_BOTTOM, "the transcript line is under the strip");
static_assert(BR_FOE_PANEL_X + BR_PANEL_W <= BR_FOE_BODY_X,
              "the foe's panel would run into the foe's body");
static_assert(BR_YOU_BODY_X + BR_BODY_W <= BR_YOU_PANEL_X,
              "the player's body would run into the player's panel");
static_assert(BR_YOU_PANEL_X + BR_PANEL_W <= OLED_W, "the player's panel runs off the panel");

uint8_t br_body_set_id(uint8_t art_key) {
  // STAGE_BABY, and the choice is stated rather than implied: the atlas authors
  // eight 24x24 bodies at BABY and six 40x40 / 32x32 ones at ADULT / SENIOR
  // (data/sprites.h), and two 40x40 creatures plus two name panels do not fit
  // on one 128x64 frame. sprite_design_of() is the SAME fold ui/pet_art.h's
  // pet_art_design() applies, so the combat body and the HOME body are chosen
  // by the same rule out of the same key - a different pool, never a different
  // rule.
  return sprite_set_id((uint8_t)STAGE_BABY,
                       sprite_design_of(art_key, STAGE_BABY),
                       (uint8_t)POSE_IDLE);
}

void br_draw_body(int16_t x, int16_t y, uint8_t art_key, uint8_t frame,
                  bool face_left, bool fainted, bool struck) {
  const SpriteRef r = sprite_frame(br_body_set_id(art_key), frame);
  if (r.bits == nullptr || r.w != BR_BODY_W || r.h != BR_BODY_H) return;

  const uint8_t* bits = r.bits;
  if (face_left) {
    xbm_mirror_frame(r.bits, s_mirror, r.w, r.h);
    bits = s_mirror;
  }
  gfx_xbm(x, y, r.w, r.h, bits);

  // A fainted body DISSOLVES rather than disappearing: erasing three quarters
  // of it leaves the silhouette readable for the one beat the transcript spends
  // saying who fell, which is what makes the event legible without a caption.
  if (fainted) {
    gfx_color(GFX_ERASE);
    gfx_dither_rect(x, y, r.w, r.h, GFX_D75);
    gfx_color(GFX_DRAW);
  }
  // The impact. XOR, not a fill: a solid overprint on a dark body is invisible
  // and on a light one erases it, while an inversion reads on both.
  if (struck) gfx_invert_rect(x, y, r.w, r.h);

  // The contact shadow, so a body reads as standing on something.
  gfx_dither_rect((int16_t)(x + 5), (int16_t)(y + r.h), (int16_t)(r.w - 10),
                  BR_SHADOW_H, GFX_D50);
}

// -----------------------------------------------------------------------------
//  ONE NAME / HP PANEL, plus the bench pips.
//
//  The pips are the "3-Pebble team" made visible: one 3x3 box per team member,
//  filled while it is still standing. A player who cannot see how many are left
//  cannot judge whether to switch, which is the decision spec section 14 is
//  built around.
// -----------------------------------------------------------------------------
static void draw_panel(int16_t x, int16_t y, const BattleCombatantArt& c) {
  char tag[8];
  tag[0] = '\0';
  if (c.level) snprintf(tag, sizeof tag, "%s%u", S(STR_ST_LEVEL), (unsigned)c.level);

  const int16_t tw = (int16_t)(tag[0] ? gfx_text_w(GF_TINY, tag) : 0);
  gfx_text_fit(GF_BODY, x, (int16_t)(y + GFX_ASC_BODY),
               (int16_t)(BR_PANEL_W - tw - 2), c.name);
  if (tag[0]) gfx_text_right(GF_TINY, (int16_t)(x + BR_PANEL_W),
                             (int16_t)(y + GFX_ASC_BODY), tag);

  const int16_t bar_y = (int16_t)(y + GFX_ASC_BODY + 2);
  // The pips take the right end of the bar row; the bar takes what is left.
  const int16_t pip_w = (int16_t)(c.team ? (c.team * 4) : 0);
  gfx_bar(x, bar_y, (int16_t)(BR_PANEL_W - pip_w), BR_BAR_H, c.hp_pct);
  for (uint8_t i = 0; i < c.team; ++i) {
    const int16_t px = (int16_t)(x + BR_PANEL_W - pip_w + i * 4);
    if (i < c.alive) gfx_fill(px, (int16_t)(bar_y + 1), 3, 3);
    else             gfx_rect(px, (int16_t)(bar_y + 1), 3, 3);
  }
}

void br_draw_field(const BattleCombatantArt& foe, const BattleCombatantArt& you,
                   uint8_t frame, const char* message) {
  // The foe faces LEFT (mirrored) and the player faces RIGHT, so the two look
  // at each other whatever the art was drawn facing.
  br_draw_body(BR_FOE_BODY_X, BR_FOE_BODY_Y, foe.art_key, frame,
               true, foe.fainted != 0u, foe.struck != 0u);
  br_draw_body(BR_YOU_BODY_X, BR_YOU_BODY_Y, you.art_key, frame,
               false, you.fainted != 0u, you.struck != 0u);

  draw_panel(BR_FOE_PANEL_X, BR_FOE_PANEL_Y, foe);
  draw_panel(BR_YOU_PANEL_X, BR_YOU_PANEL_Y, you);

  if (message && message[0])
    gfx_text_fit(GF_BODY, 2, BR_MSG_BASE, OLED_W - 4, message);
}
