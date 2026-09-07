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
// which the intro golden happened not to show only because the intro line
// (STR_BT_VS) is short enough to stop before the shadow's leftmost column.
// The string itself is quoted nowhere here: Spanish belongs in
// core/strings_es.h, comments included.
static_assert(BR_YOU_BODY_Y + BR_BODY_H + BR_SHADOW_H - 1 < BR_MSG_BASE - GFX_ASC_BODY,
              "the player's body or its shadow would sit on the transcript line");
static_assert(BR_MSG_BASE <= UI_CONTENT_BOTTOM, "the transcript line is under the strip");
static_assert(BR_FOE_PANEL_X + BR_PANEL_W <= BR_FOE_BODY_X,
              "the foe's panel would run into the foe's body");
static_assert(BR_YOU_BODY_X + BR_BODY_W <= BR_YOU_PANEL_X,
              "the player's body would run into the player's panel");
static_assert(BR_YOU_PANEL_X + BR_PANEL_W <= OLED_W, "the player's panel runs off the panel");

// THE GUARD BARRIER'S OWN KEEP-OUT, both combatants, both directions. The foe's
// barrier stands to the LEFT of the foe and the player's to the RIGHT of the
// player, so each one runs at its own side's opposite plate; asserting only one
// of them would leave the other free to slide under a name.
static_assert(BR_FOE_BODY_X - BR_GUARD_GAP - BR_GUARD_W >= BR_FOE_PANEL_X + BR_PANEL_W,
              "the foe's guard barrier would run into the foe's name plate");
static_assert(BR_YOU_BODY_X + BR_BODY_W + BR_GUARD_GAP + BR_GUARD_W <= BR_YOU_PANEL_X,
              "the player's guard barrier would run into the player's name plate");
static_assert(BR_FOE_BODY_X - BR_GUARD_GAP - BR_GUARD_W - BR_GUARD_CAP >= 0,
              "the foe's guard bracket would run off the left of the panel");
// The barrier is exactly as tall as the body, so the band checks the bodies
// already pass cover it - but the BRACKET ARMS reach sideways at those two
// rows, and on the foe they reach into the columns the transcript never uses.
static_assert(BR_FOE_BODY_Y + BR_BODY_H - 1 <= UI_CONTENT_BOTTOM,
              "the foe's guard barrier would run under the affordance strip");

uint8_t br_body_set_id(uint8_t art_key) {
  // THE COMBAT BODY IS NOW THE SAME BODY HOME DRAWS (P9-C3). It used to fold
  // the art key into the eight authored BABY designs with `% 8`, because the
  // atlas had 8 baby bodies and 6 forty-pixel adult ones and two 40x40
  // creatures plus two name panels do not fit on a 128x64 frame. Every body in
  // the atlas is 24x24 now and there is one per species, so there is nothing
  // to fold and nothing to choose: the fighter on screen is the species.
  //
  // STAGE_BABY and POSE_IDLE are still passed rather than assumed - a battle
  // body is never asleep, never ill and never eating, and the stage is
  // irrelevant to the body since P9-C3, but going through sprite_set_id() means
  // the clamp lives in ONE place instead of two.
  return sprite_set_id((uint8_t)STAGE_BABY,
                       sprite_form_of(art_key, STAGE_BABY),
                       (uint8_t)POSE_IDLE);
}

// THE GUARD BARRIER. Drawn BEFORE the body so the bracket arms pass behind the
// creature rather than over it: a ward the defender is standing inside reads as
// protection, one painted on top of its face reads as damage - which is the
// hit's idiom and the one thing this effect must not borrow.
//
// The geometry is asserted, not trusted: both combatants' barriers are checked
// against the panel and against the other side's name plate at compile time
// below, because a barrier is the first thing this file has ever drawn OUTSIDE
// a body box and the two bodies sit at different x on purpose.
static void draw_guard(int16_t x, int16_t y, int16_t w, int16_t h, bool face_left) {
  // In FRONT of the defender: the foe faces left, so its front is its left side.
  const int16_t bx = face_left ? (int16_t)(x - BR_GUARD_GAP - BR_GUARD_W)
                               : (int16_t)(x + w + BR_GUARD_GAP);
  gfx_dither_rect(bx, y, BR_GUARD_W, h, GFX_D50);
  // The hard face, on the side a blow would arrive from.
  gfx_vline(face_left ? bx : (int16_t)(bx + BR_GUARD_W - 1), y, h);
  // Two bracket arms, reaching back toward the creature.
  const int16_t ax = face_left ? (int16_t)(bx + BR_GUARD_W) : (int16_t)(bx - BR_GUARD_CAP);
  gfx_hline(ax, y, BR_GUARD_CAP);
  gfx_hline(ax, (int16_t)(y + h - 1), BR_GUARD_CAP);
}

void br_draw_body(int16_t x, int16_t y, uint8_t art_key, uint8_t frame,
                  bool face_left, bool fainted, bool struck, bool guard) {
  const SpriteRef r = sprite_frame(br_body_set_id(art_key), frame);
  if (r.bits == nullptr || r.w != BR_BODY_W || r.h != BR_BODY_H) return;

  const uint8_t* bits = r.bits;
  if (face_left) {
    xbm_mirror_frame(r.bits, s_mirror, r.w, r.h);
    bits = s_mirror;
  }
  if (guard) draw_guard(x, y, r.w, r.h, face_left);

  // OPAQUE, and that is the right blit here: the body is the first thing drawn
  // in its own box, so erasing its own background costs nothing and keeps the
  // silhouette clean against the barrier drawn a moment ago two pixels away.
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
               true, foe.fainted != 0u, foe.struck != 0u, foe.guard != 0u);
  br_draw_body(BR_YOU_BODY_X, BR_YOU_BODY_Y, you.art_key, frame,
               false, you.fainted != 0u, you.struck != 0u, you.guard != 0u);

  draw_panel(BR_FOE_PANEL_X, BR_FOE_PANEL_Y, foe);
  draw_panel(BR_YOU_PANEL_X, BR_YOU_PANEL_Y, you);

  if (message && message[0])
    gfx_text_fit(GF_BODY, 2, BR_MSG_BASE, OLED_W - 4, message);
}
