// =============================================================================
//  PEBBLEBOL - ui/screen_creator.cpp
//  CREATOR, migrated by P2-C11c. PURE translation unit.
// =============================================================================
#include "screen_creator.h"

#include <stdio.h>
#include <string.h>

#include "../core/strings_es.h"
#include "../data/sprites.h"
#include "gfx.h"
#include "qr.h"
#include "screen.h"
#include "ui.h"

// The right-hand text column. The symbol box owns columns 0..61.
#define CR_COL_X   (QR_BOX_SIZE + 3)              // 65
#define CR_COL_W   (OLED_W - CR_COL_X - 1)        // 62

// How long one symbol stays up while the provisioning AP alternates the two,
// and how long a manual tap pins the choice.
#define CR_ALTERNATE_MS  5000UL
#define CR_MANUAL_MS    10000UL
#define CR_REBUILD_MS    1000UL

static uint8_t  s_mod[QR_BUF_BYTES];
static uint8_t  s_size     = 0;
static uint8_t  s_variant  = 0;          // 0 = the URL, 1 = join-the-AP
static uint32_t s_build_ms = 0;
static uint32_t s_manual   = 0;
static uint32_t s_open_ms  = 0;
static char     s_key[CREATOR_TEXT_MAX]; // the payload the cached symbol encodes

uint8_t creator_variant(void) { return s_variant; }
void    creator_set_variant(uint8_t v) { s_variant = (uint8_t)(v ? 1u : 0u); s_key[0] = '\0'; }

// -----------------------------------------------------------------------------
//  THE PAYLOAD
// -----------------------------------------------------------------------------
static void build(const CreatorInfo& in) {
  char text[CREATOR_TEXT_MAX];
  text[0] = '\0';

  if (s_variant == 1 && in.ap_up) {
    // Open network. "WIFI:S:<ssid>;;" is 26 B and fits QR version 2 (25
    // modules -> 62 px at 2 px/module). Adding "T:nopass" makes it 35 B, which
    // forces version 3 -> 29 modules -> 70 px, and 70 does not fit in 64 rows.
    snprintf(text, sizeof(text), "WIFI:S:%s;;", in.ssid);
  } else {
    snprintf(text, sizeof(text), "%s", in.url);
  }

  if (text[0] == '\0') { s_size = 0; s_key[0] = '\0'; return; }
  if (strcmp(text, s_key) == 0) return;              // already cached

  uint8_t size = 0;
  if (qr_encode(text, s_mod, size)) {
    s_size = size;
    snprintf(s_key, sizeof(s_key), "%s", text);
  } else {
    s_size   = 0;
    s_key[0] = '\0';
  }
}

// -----------------------------------------------------------------------------
//  HOOKS
// -----------------------------------------------------------------------------
void creator_enter(void) {
  // The screen that wants the station is the screen that asks for it; there is
  // no radio policy in the entry point any more (plan section 2 row G4). It is
  // released again in creator_leave().
  ui_creator_radio(true);

  CreatorInfo in;
  ui_creator_info(in);
  s_variant  = in.ap_up ? 1u : 0u;
  s_key[0]   = '\0';
  s_size     = 0;
  s_build_ms = ui_now_ms();
  s_open_ms  = s_build_ms;
  s_manual   = 0;
  build(in);
}

void creator_leave(void) {
  // Radio OFF by default: this screen is the only owner of the station, so
  // leaving it gives back the ~50 KB of heap and the largest current draw on
  // the board instead of holding the radio powered until the next reboot.
  ui_creator_radio(false);
  s_size   = 0;
  s_key[0] = '\0';
}

void creator_update(uint32_t now_ms) {
  if ((uint32_t)(now_ms - s_build_ms) < CR_REBUILD_MS) return;
  s_build_ms = now_ms;

  CreatorInfo in;
  ui_creator_info(in);

  // In AP-provisioning mode the two symbols alternate: join the network first,
  // then open the page. A manual tap pins the choice for CR_MANUAL_MS.
  if (in.ap_up && (s_manual == 0 || (uint32_t)(now_ms - s_manual) > CR_MANUAL_MS)) {
    const uint32_t up   = (uint32_t)(now_ms - s_open_ms);
    const uint8_t  want = (uint8_t)(((up / CR_ALTERNATE_MS) & 1u) ? 0u : 1u);
    if (want != s_variant) { s_variant = want; s_key[0] = '\0'; }
  }
  build(in);
}

void creator_input(Gesture g) {
  if (g != GST_TAP_L && g != GST_TAP_R) return;
  CreatorInfo in;
  ui_creator_info(in);
  if (!in.ap_up) return;                    // one symbol only: nothing to flip
  s_variant  = (uint8_t)!s_variant;
  s_key[0]   = '\0';
  s_manual   = ui_now_ms();
  build(in);
}

// -----------------------------------------------------------------------------
//  DRAWING
//
//  qr_draw() is fenced behind #ifdef ARDUINO because it speaks u8g2 directly,
//  so the symbol is painted here through gfx.h instead - same composition
//  (lit paper, cleared dark modules, horizontal runs merged) and one less
//  thing that only exists on the target.
// -----------------------------------------------------------------------------
static void draw_symbol(void) {
  if (s_size == 0) {
    gfx_rect(QR_BOX_X, QR_BOX_Y, QR_BOX_SIZE, QR_BOX_SIZE);
    const int16_t w = (int16_t)gfx_text_w(GF_BODY, "...");
    gfx_text(GF_BODY, (int16_t)(QR_BOX_X + (QR_BOX_SIZE - w) / 2), 34, "...");
    return;
  }

  const int16_t q  = QR_QUIET_MODULES;
  int16_t px = (int16_t)(QR_BOX_SIZE / (s_size + 2 * q));
  if (px < 1) px = 1;
  if (px > 3) px = 3;
  const int16_t box = (int16_t)((s_size + 2 * q) * px);

  // Lit paper first: on an OLED a lit pixel is white, so the whole symbol area
  // including the quiet zone is drawn set and the dark modules are cleared.
  gfx_fill(QR_BOX_X, QR_BOX_Y, box, box);
  gfx_color(GFX_ERASE);
  for (int16_t r = 0; r < (int16_t)s_size; ++r) {
    int16_t c = 0;
    while (c < (int16_t)s_size) {
      if (!QR_MODULE_AT(s_mod, r, c)) { ++c; continue; }
      int16_t run = 1;
      while (c + run < (int16_t)s_size && QR_MODULE_AT(s_mod, r, c + run)) ++run;
      gfx_fill((int16_t)(QR_BOX_X + (q + c) * px), (int16_t)(QR_BOX_Y + (q + r) * px),
               (int16_t)(run * px), px);
      c = (int16_t)(c + run);
    }
  }
  gfx_color(GFX_DRAW);
}

void creator_render(void) {
  CreatorInfo in;
  ui_creator_info(in);

  draw_symbol();

  const int16_t rx = CR_COL_X;
  const int16_t rw = CR_COL_W;

  gfx_text_fit(GF_BODY, rx, 8, rw, S(STR_CREATOR_TITLE));
  gfx_text_fit(GF_TINY, rx, 15, rw, S(STR_CREATOR_PHASE));

  char pin[16];
  snprintf(pin, sizeof(pin), "%04u", (unsigned)(in.pin % 10000u));

  if (in.ap_up) {
    gfx_text_wrap(GF_BODY, rx, 24, rw, GFX_LINE_BODY, 2, S(STR_WEB_AP_HINT));
    gfx_text_fit(GF_TINY, rx, 41, rw, in.ssid);
    gfx_text_fit(GF_TINY, rx, 48, rw, in.ip);
    // The PIN used to be drawn only in the STA branch, while this screen tells
    // the user to open the address by hand - so a hand-typed URL hit the PIN
    // prompt with the PIN shown nowhere.
    {
      char line[24];
      snprintf(line, sizeof(line), "%s %s", S(STR_WEB_PIN), pin);
      gfx_text_fit(GF_BODY, rx, 55, rw, line);
    }
  } else if (in.sta_up) {
    gfx_text_fit(GF_TINY, rx, 23, rw, in.ip);
    gfx_text(GF_TINY, rx, 30, S(STR_WEB_PIN));
    gfx_text(GF_BIG, rx, 48, pin);          // 9x19 digits: readable at arm's length
  } else {
    gfx_text_wrap(GF_BODY, rx, 26, rw, GFX_LINE_BODY, 3, S(STR_WEB_CONNECTING));
  }

  // Invariant 6, relocated: the symbol box owns the affordance rows, so the
  // hint lives in the right column's bottom strip instead.
  {
    const SpriteRef l = sprite_mini(MIC_ARROW_L);
    gfx_xbm(rx, UI_AFFORD_Y, l.w, l.h, l.bits);
    gfx_text_fit(GF_BODY, (int16_t)(rx + 10), (int16_t)(UI_AFFORD_Y + GFX_ASC_BODY),
                 40, S(STR_AF_BACK));
    if (in.ap_up) {
      const SpriteRef r = sprite_mini(MIC_ARROW_R);
      gfx_xbm((int16_t)(OLED_W - 8), UI_AFFORD_Y, r.w, r.h, r.bits);
    }
  }
}
