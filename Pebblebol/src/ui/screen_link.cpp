// =============================================================================
//  PEBBLEBOL - ui/screen_link.cpp
//  LINK, migrated by P2-C11c. PURE translation unit.
// =============================================================================
#include "screen_link.h"

#include "../core/strings_es.h"
#include "../data/sprites.h"
#include "gfx.h"
#include "screen.h"
#include "ui.h"

void link_render(void) {
  gfx_header(S(STR_LINK_TITLE), nullptr);

  // MIC_BLE is the radio glyph the atlas has; LINK is a radio screen whatever
  // transport ends up under it (D2 chose ESP-NOW), so it is the honest icon.
  const SpriteRef r = sprite_mini(MIC_BLE);
  gfx_xbm((int16_t)((OLED_W - r.w) / 2), (int16_t)(UI_HDR_H + 4), r.w, r.h, r.bits);

  gfx_text_center(GF_NARR, 33, S(STR_LINK_PHASE));
  gfx_text_wrap(GF_BODY, 4, 43, OLED_W - 8, GFX_LINE_BODY, 2, S(STR_LINK_BODY));

  gfx_countdown(ui_idle_ms());
  gfx_affordance(nullptr, S(STR_AF_BACK));
}

// Nothing to steer. BACK and HOME are the global grammar's and never reach
// here; a stray tap on a screen with no options is a no-op, not a wiggle.
void link_input(Gesture g) {
  if (g == GST_BOTH) ui_help(STR_LINK_BODY);
}
