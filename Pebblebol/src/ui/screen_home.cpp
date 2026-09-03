// =============================================================================
//  PEBBLEBOL - ui/screen_home.cpp
//  HOME, migrated into the screen table by P2-C11b. See screen_home.h for the
//  layout contract and for why the animated body layer is bound rather than
//  linked.
//
//  PURE translation unit: gfx.h, the strings, the sprite atlas and the screen
//  view. No render.h and no Arduino.h, which is what lets
//  tests/test_screens.cpp render HOME at the real 128x64 on the host.
//
//  SF_STICKY: HOME is where the auto-return goes, so it never times out and it
//  never draws the countdown bar.
// =============================================================================
#include "screen_home.h"

#include <stdio.h>

#include "../core/strings_es.h"
#include "../data/sprites.h"
#include "../game/genome.h"          // gene_species(), for the sprite lookup
#include "gfx.h"
#include "screen.h"
#include "screen_view.h"
#include "ui.h"

static HomeBodyFn  s_body  = nullptr;
static HomeLeaveFn s_leave = nullptr;

void home_bind_body(HomeBodyFn draw, HomeLeaveFn leave) {
  s_body  = draw;
  s_leave = leave;
}

void home_leave(void) { if (s_leave) s_leave(); }

// -----------------------------------------------------------------------------
//  The identity strip and the XP rule, rows 0..HOME_XP_RULE_Y.
//
//  Name and level share one 8 px line, and the level is measured FIRST so the
//  name is fitted into whatever is actually left: a 12-character nickname (the
//  schema maximum, PB_NICKNAME_CAP - 1) and a two-digit level must not be able
//  to collide, at any font, on any Pebble.
// -----------------------------------------------------------------------------
static void draw_identity(const PebbleView& v) {
  char lv[12];
  snprintf(lv, sizeof lv, "%s%u", S(STR_ST_LEVEL), (unsigned)v.level);
  const int16_t lw = (int16_t)gfx_text_w(GF_BODY, lv);
  gfx_text_fit(GF_BODY, 1, 7, (int16_t)(OLED_W - 3 - lw), v.name);
  gfx_text_right(GF_BODY, OLED_W - 1, 7, lv);
}

// The XP bar of spec section 8, as a rule rather than a widget: solid for the
// part of the level that is done, dotted for the part that is not. It is the
// separator row the status strip needed anyway, so it costs no vertical space
// at all - which is the whole reason HOME can carry six numbers and a stage.
static void draw_xp_rule(const PebbleView& v) {
  // xp_next == 0 is XP_LEVEL_MAX (game/xp.h): there is nothing left to buy, so
  // the rule is drawn solid end to end instead of dividing by zero.
  const uint16_t next = v.xp_next;
  int16_t w = OLED_W;
  if (next != 0u) {
    const uint16_t have = (v.xp > next) ? next : v.xp;
    w = (int16_t)(((int32_t)OLED_W * (int32_t)have) / (int32_t)next);
    if (w > OLED_W) w = OLED_W;
  }
  if (w > 0) gfx_hline(0, HOME_XP_RULE_Y, w);
  for (int16_t x = w; x < OLED_W; x = (int16_t)(x + 3)) gfx_pixel(x, HOME_XP_RULE_Y);
}

// -----------------------------------------------------------------------------
//  The three meters, in the right-hand HUD column. HP first because it is the
//  one that ends a battle, then the two care stats section 8 names.
// -----------------------------------------------------------------------------
static void draw_meter(uint8_t slot, uint8_t mini_icon, uint8_t pct) {
  const int16_t y = (int16_t)(HOME_METER_Y0 + slot * HOME_METER_PITCH);
  const SpriteRef r = sprite_mini(mini_icon);
  gfx_xbm((int16_t)(HOME_METER_X + 2), y, r.w, r.h, r.bits);
  gfx_bar(HOME_METER_X, (int16_t)(y + 8), HOME_METER_W, 4, pct);
}

static void draw_meters(const PebbleView& v) {
  const uint8_t hp = v.hp_max ? (uint8_t)(((uint32_t)v.hp_cur * 100u) / v.hp_max) : 0u;
  draw_meter(0, MIC_HEALTH, (hp > 100u) ? 100u : hp);
  draw_meter(1, MIC_HUNGER, v.care_pct[ST_HUNGER]);
  draw_meter(2, MIC_HAPPY,  v.care_pct[ST_HAPPINESS]);
}

// -----------------------------------------------------------------------------
//  The stage, when nothing animated is bound to it. The floor is the same
//  50 % dithered rule petfx_draw_floor() lays down, and the body stands on it
//  with the two-frame idle bob - so a headless build, and every golden
//  snapshot, shows the Pebble where the device shows it.
// -----------------------------------------------------------------------------
static void draw_static_body(const PebbleView& v, uint8_t frame) {
  gfx_dither_rect(0, HOME_FLOOR_Y, OLED_W, 1, GFX_D50);

  const uint8_t form = sprite_form_of(v.genome, v.minor_form, (Stage)v.stage);
  const SpriteRef r  = sprite_lookup_pose(gene_species(v.genome), v.stage, form,
                                          v.pose, frame);
  if (!r.bits || r.w == 0 || r.h == 0) return;
  const int16_t x = (int16_t)sprite_center_x(r.w);
  const int16_t y = (int16_t)(HOME_FLOOR_Y - r.h);
  gfx_xbm(x, y, r.w, r.h, r.bits);
}

// -----------------------------------------------------------------------------
//  HOOKS
// -----------------------------------------------------------------------------
void home_render(void) {
  const PebbleView* v = ui_view();
  if (!v || !v->present) {
    gfx_text_center(GF_NARR, 34, S(STR_UI_NOBODY));
    gfx_affordance(nullptr, nullptr);
    return;
  }

  draw_identity(*v);
  draw_xp_rule(*v);

  const uint8_t frame = (uint8_t)((ui_now_ms() / UI_ANIM_FRAME_MS) & 1u);
  if (!s_body || !s_body(frame)) draw_static_body(*v, frame);

  draw_meters(*v);
  gfx_affordance(S(STR_AF_MENU), S(STR_AF_PET));
}

void home_input(Gesture g) {
  switch (g) {
    case GST_TAP_L:  ui_push(SCR_MENU); break;
    // TAP_R strokes the pet. It used to cycle the three status-bar modes, and
    // there are no modes left to cycle: the strip now says the one thing spec
    // section 8 asks it to say, always.
    case GST_TAP_R:
    case GST_DBL_R:  ui_act_and_show(ACT_PET); break;
    case GST_HOLD_L: ui_push(SCR_STATUS); break;
    // HOME is the root, so the router leaves B alone and A/B both do something
    // here. B HELD is the one gesture left with nothing to mean: the body
    // shakes instead of the press vanishing.
    case GST_HOLD_R: ui_wiggle(); break;
    case GST_BOTH: {
      Config* c = ui_cfg();
      if (!c) break;
      c->flags = (uint8_t)(c->flags ^ CF_MUTE);
      ui_cfg_changed();
      ui_toast((c->flags & CF_MUTE) ? STR_SET_MUTE_ON : STR_SET_MUTE_OFF);
      break;
    }
    // Invariant 2 sends LONG_BOTH home from everywhere else, so on HOME the
    // gesture is free and it opens SETTINGS, exactly as it always did.
    case GST_LONG_BOTH: ui_push(SCR_SETTINGS); break;
    default: break;
  }
}
