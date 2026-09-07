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
#include "../game/genome.h"          // gene_species(): the art key's fallback
#include "gfx.h"
#include "pet_art.h"                 // pet_art_key(): species -> body
#include "corrupt_fx.h"              // cfx_rows(): where the glitch may draw
#include "petfx_core.h"              // pf_build_sleep(): the derived sleeping body
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
// THE DERIVED SLEEPING FRAME, cached. 74 B of file scope rather than 74 B of
// stack per render: the derivation is ~1,200 byte operations over a 24x24
// frame, it only changes when the set or the frame does, and "no per-frame
// heap" is a claim about this whole screen (ui/battle_renderer.cpp's mirror
// scratch is the same argument). The key is (set, frame) + 1, so 0 means empty
// and a memset of this file's statics cannot look like a valid cache.
static uint8_t  s_sleep_bits[PF_FRAME_BYTES];
static uint16_t s_sleep_key = 0;
// THE FOURTH KEY, and it is not optional. `set` is the ATLAS set id, which two
// different creator Pebbles share - they have no atlas row of their own and both
// fold onto the same one - so a (set, frame) key alone would derive one player's
// sleeper and then hand it to the next custom creature that closed its eyes.
// The source frame IS the identity here, so it is what the cache compares.
// ui/petfx.cpp's own cache carries the same pointer for the same reason.
static const uint8_t* s_sleep_src = nullptr;

// Fill the cache for (set, frame) and answer 1 when there is a derived body to
// draw. 0 means fall back to the authored PBSPR_SLEEP blob - a blank frame or a
// set wider than the cache geometry, neither of which today's atlas contains.
// `body` is the frame the sleeper is DERIVED FROM, handed in rather than looked
// up from `set`: a creator species has no atlas row, so a lookup here would put
// species 1's silhouette under a custom Pebble the moment it fell asleep - the
// same defect this whole change is about, one pose further along. `set` stays
// as the CACHE KEY and as the source of the eye band, which is the only part a
// custom body has no answer for.
static uint8_t sleep_frame_of(uint8_t set, uint8_t frame, const SpriteRef& body) {
  const uint16_t key = (uint16_t)(((uint16_t)set << 1) | (frame & 1u)) + 1u;
  if (key == s_sleep_key && body.bits == s_sleep_src) return 1u;
  const SpriteSet   s = sprite_set(set);
  const SpriteRef   r = body;
  const SpriteEyeBand b = sprite_eyes(set, (uint8_t)(frame < s.frames ? frame : 0u));
  if (pf_build_sleep(r.bits, r.w, r.h, b.y0, b.y1, b.x0, b.x1, s_sleep_bits) == 0u) {
    s_sleep_key = 0;
    s_sleep_src = nullptr;
    return 0u;
  }
  s_sleep_key = key;
  s_sleep_src = body.bits;
  return 1u;
}

// -----------------------------------------------------------------------------
//  THE CORRUPTION GLITCH, PAINTED (P10-C3).
//
//  ui/corrupt_fx.cpp has owned WHERE the glitch may draw since P9-C5 - the row
//  count, each row's origin, width, Bayer level and phase, all clamped into the
//  caller's rectangle, with tests/test_corruption.cpp asserting the containment
//  on the pixels. What it has never had is a PICTURE: the only painter was in
//  ui/petfx.cpp, which includes render.h and is compiled by no host binary, so
//  a corrupted creature had never appeared in a golden and `./bin/corrupt_view`
//  was the only way to look at one.
//
//  This is the second painter, on the still body path, and it is deliberately
//  the SAME three lines: the ink box the body just drew, cfx_glitch_on() as the
//  gate, cfx_rows() as the geometry, and gfx_dither_rect_phase() at GFX_XOR -
//  which is exactly what render.h calls the shimmer and what petfx.cpp asks for.
//  Nothing here has an opinion about the Bayer matrix or about when to fire.
//
//  THE RECTANGLE IS THE INK BOX AND NOT THE SPRITE BOX, for the reason
//  corrupt_fx.h gives: a sprite box reaches down over the floor line, and noise
//  on the floor is noise on furniture.
// -----------------------------------------------------------------------------
static void draw_glitch(int16_t x, int16_t y, const uint8_t* bits,
                        uint8_t w, uint8_t h, uint32_t seed) {
  const uint32_t now = ui_now_ms();
  if (!cfx_glitch_on(now, seed)) return;
  uint8_t t, b, l, r;
  if (!pf_scan_ink(bits, w, h, &t, &b, &l, &r)) return;

  CfxRect box;
  box.x0 = (int16_t)(x + l); box.y0 = (int16_t)(y + t);
  box.x1 = (int16_t)(x + r); box.y1 = (int16_t)(y + b);
  CfxRow rows[CFX_ROWS_MAX];
  const uint8_t n = cfx_rows(box, now, seed, rows);
  if (n == 0u) return;
  gfx_color(GFX_XOR);
  for (uint8_t i = 0; i < n; ++i)
    gfx_dither_rect_phase(rows[i].x, rows[i].y, (int16_t)rows[i].w, 1,
                          rows[i].level, rows[i].phase);
  gfx_color(GFX_DRAW);
}

static void draw_static_body(const PebbleView& v, uint8_t frame) {
  gfx_dither_rect(0, HOME_FLOOR_Y, OLED_W, 1, GFX_D50);

  // THE SPECIES CHOOSES THE BODY (P4-C4a). This used to pass
  // gene_species(v.genome) to the lookup, so every one of the 36 species wore
  // one of eight genome bodies and an evolution moved nothing on this screen.
  // The still body and the animated one fold the SAME key through the SAME
  // function - pet_art_key() into sprite_form_of() - so the golden below is a
  // statement about the body the device draws and not about a second rule.
  const uint8_t key  = pet_art_key(v.species_id, gene_species(v.genome));
  const uint8_t form = sprite_form_of(key, (Stage)v.stage);

  // SLEEP IS DERIVED FROM THE SPECIES BODY SINCE P10-C3, and it is derived on
  // BOTH body paths. Phase 9 left all sixty species sharing one authored
  // sleeping blob; ui/petfx_core.h says why deriving it beat drawing forty new
  // sets. THE REASON IT IS HERE AND NOT ONLY IN ui/petfx.cpp IS THE WHOLE
  // LESSON OF THE PHASE-9 BLINK: HOME has two body paths, the animated one in
  // that device-only file and this still one, and this is the ONE a host binary
  // and a golden can see. Deriving in only the animated path would make the
  // device show a species while every snapshot in the suite kept showing the
  // blob - and the suite would stay green. tools/check.sh gates both call
  // sites.
  if (v.stage != (uint8_t)STAGE_EGG && v.pose == (uint8_t)POSE_SLEEP) {
    const uint8_t set = sprite_set_id(v.stage, form, (uint8_t)POSE_IDLE);
    // THE IDLE BODY, WHOEVER DREW IT - which for a creator species is the
    // player's own 24x24 and not the atlas row it has no claim to.
    const SpriteRef idle = pet_body_ref(v.species_id, gene_species(v.genome),
                                        v.stage, (uint8_t)POSE_IDLE, frame);
    if (sleep_frame_of(set, frame, idle)) {
      const int16_t x = (int16_t)sprite_center_x(idle.w);
      const int16_t y = (int16_t)(HOME_FLOOR_Y - idle.h);
      gfx_xbm(x, y, idle.w, idle.h, s_sleep_bits);
      if (v.corrupted) draw_glitch(x, y, s_sleep_bits, idle.w, idle.h, v.genome.lineage_id);
      return;
    }
  }

  const SpriteRef r  = pet_body_ref(v.species_id, gene_species(v.genome),
                                    v.stage, v.pose, frame);
  if (!r.bits || r.w == 0 || r.h == 0) return;
  const int16_t x = (int16_t)sprite_center_x(r.w);
  const int16_t y = (int16_t)(HOME_FLOOR_Y - r.h);
  gfx_xbm(x, y, r.w, r.h, r.bits);
  if (v.corrupted) draw_glitch(x, y, r.bits, r.w, r.h, v.genome.lineage_id);
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
    case GST_TAP_R:  ui_act_and_show(ACT_PET); break;
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
