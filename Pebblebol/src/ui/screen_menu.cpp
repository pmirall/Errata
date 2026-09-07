// =============================================================================
//  PEBBLEBOL - ui/screen_menu.cpp
//  MENU, migrated into the screen table by P2-C11b.
//
//  The carousel itself is unchanged from ui.cpp's draw_menu(): five slots at a
//  24 px pitch, the centre one framed, the neighbours dithered out with
//  distance, and a quadratic ease-out that leaves fast and settles slow so one
//  step reads as weight rather than as a scroll.
//
//  WHAT CHANGED IS THE CONTENT. The ring used to be the eight Nottamagochi
//  care verbs; it is now the seven destinations of spec section 8. Two of them
//  have no screen yet - BOX is a later commit of this same plan step and
//  NETWORK is Phase 5 - and they answer with a toast rather than with a blank
//  screen or a silent no-op, because a menu entry that does nothing at all is
//  indistinguishable from a broken button.
//
//  PURE translation unit.
// =============================================================================
#include "screen_menu.h"

#include "../core/strings_es.h"
#include "../data/sprites.h"
#include "gfx.h"
#include "screen.h"
#include "ui.h"

static const uint8_t kIcon[MENU_ITEM_COUNT] = {
  ICO_DNA, ICO_MEAL, ICO_BALL, ICO_HOME, ICO_WIFI, ICO_BLE, ICO_GEAR
};
static const uint16_t kHelp[MENU_ITEM_COUNT] = {
  STR_HLP_STATUS, STR_HLP_FEED, STR_HLP_PLAY, STR_HLP_BOX,
  STR_HLP_NETWORK, STR_HLP_SOCIAL, STR_HLP_SETTINGS
};

static uint8_t  s_idx      = 0;
static int16_t  s_ring_from = 0;      // px still to travel, 0 = settled
static uint32_t s_ring_ms  = 0;

uint8_t menu_cursor(void) { return s_idx; }

void menu_enter(void) { s_ring_from = 0; s_ring_ms = 0; }

static uint8_t ring_next(uint8_t cur, uint8_t n) {
  return n ? (uint8_t)((cur + 1u) % n) : 0u;
}

// Quadratic ease-out, (T-t)^2 / T^2. Integer only: amp <= 24 and T = 140, so
// the numerator peaks at 24 * 19600 and stays inside int32.
static int16_t ring_offset_px(uint32_t now_ms) {
  if (s_ring_from == 0) return 0;
  const uint32_t el = (uint32_t)(now_ms - s_ring_ms);
  if (el >= UI_RING_MS) { s_ring_from = 0; return 0; }
  const uint32_t left = UI_RING_MS - el;
  return (int16_t)(((int32_t)s_ring_from * (int32_t)(left * left)) /
                   (int32_t)(UI_RING_MS * UI_RING_MS));
}

void menu_render(void) {
  gfx_header(S(STR_APP_NAME), nullptr);

  const int16_t cx   = OLED_W / 2;
  const int16_t cy   = UI_HDR_H + 8;                  // 19: icon rows 19..30
  const int16_t ring = ring_offset_px(ui_now_ms());

  for (int8_t d = -2; d <= 2; ++d) {
    const uint8_t idx = (uint8_t)((s_idx + MENU_ITEM_COUNT * 2 + d) % MENU_ITEM_COUNT);
    const int16_t x   = (int16_t)(cx - 6 + d * 24 + ring);
    const SpriteRef r = sprite_icon(kIcon[idx]);
    // A slot that does not fit ENTIRELY is dropped, art and fade together: the
    // icons are opaque (setBitmapMode 0 on the device) and half a badge hanging
    // off an edge reads as damage rather than as a carousel. The ease-out opens
    // that gap at both ends for about three quarters of the 140 ms slide, and
    // an honest cheap gap beats a slot that is paid for and does nothing.
    if (x < 0 || x + (int16_t)r.w > OLED_W) continue;
    gfx_xbm(x, cy, r.w, r.h, r.bits);
    if (d == 0) {
      gfx_rect((int16_t)(x - 4), (int16_t)(cy - 4), 20, 20);
    } else {
      // Neighbours fade out with distance: erase 50 % then 75 % of their pixels.
      gfx_color(GFX_ERASE);
      gfx_dither_rect(x, cy, 12, 12, (d == -1 || d == 1) ? GFX_D50 : GFX_D75);
      gfx_color(GFX_DRAW);
    }
  }

  gfx_text_center(GF_HEAD, (int16_t)(cy + 30), S_MENU(s_idx));

  for (uint8_t i = 0; i < MENU_ITEM_COUNT; ++i) {
    const int16_t x = (int16_t)(cx - MENU_ITEM_COUNT * 2 + i * 4);
    if (i == s_idx) gfx_fill(x, UI_CONTENT_BOTTOM - 2, 3, 3);
    else            gfx_fill((int16_t)(x + 1), UI_CONTENT_BOTTOM - 1, 1, 1);
  }

  gfx_countdown(ui_idle_ms());
  gfx_affordance(S(STR_AF_NEXT), S(STR_AF_BACK_SEL));
}

static void menu_select(void) {
  switch (s_idx) {
    case MENU_PEBBLE:   ui_push(SCR_STATUS);   break;
    case MENU_CARE:     ui_push(SCR_CARE);     break;
    case MENU_PLAY:     ui_push(SCR_PLAY);     break;
    case MENU_BOX:      ui_push(SCR_BOX);      break;
    // NETWORK is Phase 5. The entry goes to a real screen that says so rather
    // than to a toast: every section 6 state has a row now (ui/screen_soon.h).
    case MENU_NETWORK:  ui_push(SCR_NETWORK);  break;
    case MENU_LINK:     ui_push(SCR_LINK);     break;
    default:            ui_push(SCR_SETTINGS); break;
  }
}

void menu_input(Gesture g) {
  switch (g) {
    case GST_TAP_L:
    case GST_HOLD_L:
      s_idx = ring_next(s_idx, MENU_ITEM_COUNT);
      // The new centre starts one slot to the RIGHT and slides in. HOLD_L
      // repeats every REPEAT_RATE_MS (220 ms) > UI_RING_MS, so a held button
      // still gets a full settle between steps instead of a smear.
      s_ring_from = UI_RING_STEP_PX;
      s_ring_ms   = ui_now_ms();
      break;
    // Section 7: B taps back (the router took it), B held chooses. HOLD_R
    // fires exactly ONCE, which is why choosing sits on it and stepping does
    // not - see app/input_router.h.
    case GST_HOLD_R: menu_select(); break;
    case GST_BOTH:  ui_help(kHelp[s_idx]); break;
    default: break;
  }
}
