// =============================================================================
//  ERRATA - ui/screen_manual.cpp
//  See screen_manual.h. PURE translation unit.
// =============================================================================
#include "screen_manual.h"

#include <stdio.h>
#include <string.h>

#include "../core/config.h"
#include "../core/strings_es.h"
#include "../data/sprites.h"
#include "gfx.h"
#include "qr.h"
#include "qr_paint.h"
#include "screen.h"
#include "ui.h"

// THE RIGHT COLUMN'S TWO BLOCKS. The symbol owns rows 1..62 of the left 62 px,
// so these are the only rows there are. Asserted rather than eyeballed, exactly
// as ui/screen_creator.cpp asserts its four: a title that grows to three lines
// would push the hint into the affordance strip and nothing would say so.
// BOTH ARE BASELINES, NOT TOPS, and the first draft of these asserts got that
// wrong in both directions: it counted a wrapped block of n lines as reaching
// n * GFX_LINE_BODY below its origin when the last BASELINE is (n - 1) below.
// The assert caught it - `MAN_ROW_HINT + 3 * GFX_LINE_BODY < UI_AFFORD_Y` was
// 56 < 56 - which is the only reason these numbers are the ones the panel can
// hold rather than the ones that looked right. ui/screen_creator.cpp's
// CR_ROW_WAIT block is the same arithmetic and is where the convention comes
// from.
#define MAN_ROW_TITLE   8      // rows 2..8, and 10..16 if it wraps to two
#define MAN_ROW_HINT   26      // rows 20..26, 28..34, 36..42 at three lines

static_assert(MAN_ROW_TITLE - GFX_ASC_BODY + 1 >= 0,
              "the manual title is off the top of the panel");
static_assert(MAN_ROW_TITLE + GFX_LINE_BODY < MAN_ROW_HINT - GFX_ASC_BODY + 1,
              "a two-line title overlaps the top of the hint block");
static_assert(MAN_ROW_HINT + 2 * GFX_LINE_BODY < UI_AFFORD_Y,
              "the three-line hint reaches the affordance strip");

// THE SYMBOL IS BUILT ONCE PER VISIT, NOT ONCE PER FRAME. 165 B of modules and
// an encode that walks a Reed-Solomon generator; MANUAL_URL cannot change while
// the screen is open, so re-encoding every frame would be the same answer at
// FPS_LOW forever.
static uint8_t s_mod[QR_BUF_BYTES];
static uint8_t s_size = 0;
static char    s_payload[QR_TEXT_MAX];

const char* manual_payload(void) { return s_payload; }

void manual_enter(void) {
  s_size = 0;
  s_payload[0] = '\0';
  uint8_t size = 0;
  if (qr_encode(MANUAL_URL, s_mod, size)) {
    s_size = size;
    // The payload is copied back from the constant rather than assumed: what
    // this reports is what was HANDED to the encoder, which is the only thing
    // a test asserting "the QR points at the manual" can honestly read.
    snprintf(s_payload, sizeof s_payload, "%s", MANUAL_URL);
  }
}

// Nothing to press. B is BACK and the router owns it; A has nothing to mean on
// a screen with one static picture, so it does nothing rather than something
// invented to keep it busy.
void manual_input(Gesture g) { (void)g; }

void manual_render(void) {
  qrp_paint(s_mod, s_size, QR_BOX_X, QR_BOX_Y, QR_BOX_SIZE);

  // The right-hand column, same geometry the creator screen uses: the symbol is
  // 62 px tall on a 64-row panel, so the words go beside it or nowhere.
  const int16_t rx = (int16_t)(QR_BOX_SIZE + 3);
  const int16_t rw = (int16_t)(OLED_W - rx - 1);

  gfx_text_wrap(GF_BODY, rx, MAN_ROW_TITLE, rw, GFX_LINE_BODY, 2,
                S(STR_MAN_TITLE));
  gfx_text_wrap(GF_BODY, rx, MAN_ROW_HINT, rw, GFX_LINE_BODY, 3,
                S(STR_MAN_HINT));

  {
    const SpriteRef l = sprite_mini(MIC_ARROW_L);
    gfx_xbm(rx, UI_AFFORD_Y, l.w, l.h, l.bits);
    gfx_text_fit(GF_BODY, (int16_t)(rx + 10), (int16_t)(UI_AFFORD_Y + GFX_ASC_BODY),
                 40, S(STR_AF_BACK));
  }
}
