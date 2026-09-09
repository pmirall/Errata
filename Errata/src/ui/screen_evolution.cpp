// =============================================================================
//  ERRATA - ui/screen_evolution.cpp
//  EVOLUTION, migrated by P2-C11c. PURE translation unit.
// =============================================================================
#include "screen_evolution.h"

#include "../core/strings_es.h"
#include "../data/sprites.h"
#include "gfx.h"
#include "screen.h"
#include "screen_view.h"
#include "ui.h"

static EvoCeremonyFn s_ceremony = nullptr;

static uint8_t  s_rub_count = 0;
static uint8_t  s_rub_last  = 0xFF;      // which side landed last, 0 = L, 1 = R
static uint32_t s_rub_ms    = 0;

void evo_bind_ceremony(EvoCeremonyFn fn) { s_ceremony = fn; }
uint8_t evo_rub_count(void) { return s_rub_count; }

void evo_enter(void) {
  s_rub_count = 0;
  s_rub_last  = 0xFF;
  s_rub_ms    = 0;
}

// -----------------------------------------------------------------------------
//  THE INCUBATOR
// -----------------------------------------------------------------------------
static void draw_incubator(void) {
  const BugView* v = ui_view();
  gfx_header(S(STR_EGG_TITLE), nullptr);
  if (!v || !v->present) { gfx_affordance(nullptr, S(STR_AF_BACK)); return; }

  const uint32_t now   = ui_now_ms();
  const int16_t  wob   = (int16_t)(((now / 260u) & 1u) ? 1 : -1);
  const uint8_t  frame = (uint8_t)((now / UI_ANIM_FRAME_MS) & 1u);
  // The shell starts cracking a minute before the egg is due, or as soon as
  // the player has rubbed it all the way.
  const uint8_t  phase = (uint8_t)(((v->age_s + 60u >= AGE_EGG_S) ||
                                    (s_rub_count >= EGG_RUB_TAPS)) ? 1u : 0u);
  const SpriteRef r    = sprite_egg(phase, frame);
  gfx_xbm((int16_t)((int16_t)sprite_center_x(r.w) + wob), (int16_t)(UI_HDR_H + 3),
          r.w, r.h, r.bits);

  gfx_text_center(GF_BODY, 45, S(STR_EGG_RUB));
  for (uint8_t i = 0; i < EGG_RUB_TAPS; ++i) {
    const int16_t x = (int16_t)(OLED_W / 2 - EGG_RUB_TAPS * 3 + i * 6);
    if (i < s_rub_count) gfx_fill(x, 50, 5, 5);
    else                 gfx_rect(x, 50, 5, 5);
  }

  if (v->flags & PF_COLD_EGG)
    gfx_text_fit(GF_BODY, 2, 19, OLED_W - 4, S(STR_EGG_COLD));
  else if (v->genome.generation > 0 && (v->flags & PF_INBRED))
    gfx_text_fit(GF_BODY, 2, 19, OLED_W - 4, S(STR_EGG_KIN));

  gfx_affordance(S(STR_AF_RUB), S(STR_AF_RUB));
}

void evo_render(void) {
  // The ceremony owns the WHOLE frame when it is running: no header, no
  // affordance strip, no countdown. Chrome would turn a birth into a screen.
  if (s_ceremony && s_ceremony()) return;
  draw_incubator();
}

// -----------------------------------------------------------------------------
//  THE RUB
//
//  Ten taps that ALTERNATE sides, inside one EGG_RUB_WINDOW_MS window. Two taps
//  on the same button are a player leaning on a button, not a player rubbing an
//  egg, so they reset nothing and earn nothing.
// -----------------------------------------------------------------------------
// SF_OWNS_BACK, and this is why: the rub ALTERNATES the two buttons, so B is
// half of the only gesture this screen has. LONG_BOTH is how you leave.
void evo_input(Gesture g) {
  if (g != GST_TAP_L && g != GST_TAP_R) { ui_toast(STR_EGG_NOT_YET); return; }

  const uint8_t  side = (g == GST_TAP_L) ? 0u : 1u;
  const uint32_t now  = ui_now_ms();
  if (s_rub_ms == 0 || (uint32_t)(now - s_rub_ms) > EGG_RUB_WINDOW_MS) {
    s_rub_ms    = now;
    s_rub_count = 0;
    s_rub_last  = 0xFF;
  }
  if (s_rub_last == side) { ui_toast(STR_EGG_NOT_YET); return; }
  s_rub_last = side;
  if (++s_rub_count >= EGG_RUB_TAPS) {
    s_rub_count = 0;
    // The model change and the save are ui.cpp's; the ceremony starts from the
    // event the simulation raises, not from here.
    ui_request_hatch();
  }
}
