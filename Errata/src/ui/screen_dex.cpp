// =============================================================================
//  ERRATA - ui/screen_dex.cpp
//  See screen_dex.h. PURE translation unit.
// =============================================================================
#include "screen_dex.h"

#include <stdio.h>
#include <string.h>

#include "../core/strings_es.h"
#include "../data/sprites.h"
#include "../game/dex.h"
#include "../game/species.h"
#include "gfx.h"
#include "pet_art.h"
#include "screen.h"
#include "ui.h"

// -----------------------------------------------------------------------------
//  GEOMETRY. The body is 24x24 and the panel is 64 rows with a header and an
//  affordance strip, so the middle band is what is left and the neighbours get
//  a sliver each. Asserted rather than eyeballed, as every other screen's is.
// -----------------------------------------------------------------------------
#define DX_BODY_X        3
#define DX_BODY_Y       20
#define DX_TEXT_X       32
#define DX_ROW_NUM      26      // "Nº 07 / 60"
#define DX_ROW_NAME     38      // the species name, or ???
#define DX_ROW_STATE    48      // VISTO / EN LA CAJA
#define DX_PREV_Y       13      // the sliver above
#define DX_NEXT_Y       54      // and below

static_assert(DX_BODY_Y + 24 <= UI_AFFORD_Y,
              "the 24x24 body reaches the affordance strip");
static_assert(DX_ROW_NUM - GFX_ASC_BODY + 1 > DX_PREV_Y,
              "the number line overlaps the previous-row sliver");
static_assert(DX_ROW_STATE < UI_AFFORD_Y,
              "the state line lands in the affordance strip");
static_assert(DX_NEXT_Y < UI_AFFORD_Y,
              "the next-row sliver lands in the affordance strip");

static uint8_t s_cur = 1;      // a SPECIES ID, never a list index

uint8_t dex_screen_cursor(void) { return s_cur; }

void dex_screen_enter(void)
{
  // OPENS ON THE FIRST THING YOU HAVE NOT GOT, and falls back to row 1. A wiki
  // that always opens at the top makes the player walk the same forty rows
  // every visit to find the frontier.
  s_cur = 1u;
  for (uint8_t id = 1u; id <= (uint8_t)SPECIES_TABLE_COUNT; ++id) {
    if (!dex_caught(id)) { s_cur = id; break; }
  }
}

static uint8_t ring_next(uint8_t id)
{
  return (uint8_t)((id >= (uint8_t)SPECIES_TABLE_COUNT) ? 1u : (id + 1u));
}
static uint8_t ring_prev(uint8_t id)
{
  return (uint8_t)((id <= 1u) ? (uint8_t)SPECIES_TABLE_COUNT : (id - 1u));
}

void dex_screen_input(Gesture g)
{
  if (g == (Gesture)GST_BOTH)   { ui_help(STR_HLP_DEX_ROW); return; }
  if (g == (Gesture)GST_TAP_L)  { s_cur = ring_next(s_cur); ui_request_frame(); return; }
  if (g == (Gesture)GST_HOLD_L) { s_cur = ring_prev(s_cur); ui_request_frame(); return; }
  // B is BACK and the router owns it. There is nothing to CHOOSE here: a wiki
  // row is a fact, not an action, and inventing one for the tap would be a
  // button that does something because it was free rather than because it was
  // wanted.
}

// One row's body, at (x, y). A species you have not met is a SILHOUETTE: the
// same 24x24 art with every lit pixel kept and nothing else drawn, which on a
// 1-bit panel is what a filled shape already is - so the "silhouette" is the
// ordinary sprite and the difference is that the NAME and the state line are
// withheld. Drawing a solid block instead would lose the shape, and the shape
// is the clue that makes an undiscovered row interesting.
static void draw_body(uint8_t id, int16_t x, int16_t y, bool known)
{
  const SpriteRef r = pet_body_ref(id, 0u, (uint8_t)STAGE_ADULT,
                                   (uint8_t)POSE_IDLE, 0u);
  if (r.bits == nullptr) return;
  if (known) {
    gfx_xbm(x, y, r.w, r.h, r.bits);
    return;
  }
  // UNKNOWN: the outline only. gfx_xbm() then a 50% dither over it leaves a
  // shape that reads as "something is there" without reading as a creature you
  // can identify, which is the whole point of a silhouette.
  gfx_xbm(x, y, r.w, r.h, r.bits);
  gfx_color(GFX_ERASE);
  gfx_dither_rect(x, y, r.w, r.h, GFX_D50);
  gfx_color(GFX_DRAW);
}

void dex_screen_render(void)
{
  char tag[12];
  snprintf(tag, sizeof tag, "%u/%u",
           (unsigned)dex_count_caught(), (unsigned)SPECIES_TABLE_COUNT);
  gfx_header(S(STR_DEX_TITLE), tag);

  const bool known = dex_seen(s_cur);

  draw_body(s_cur, DX_BODY_X, DX_BODY_Y, known);

  char num[16];
  // THE ORDINAL IS UTF-8, NOT A LATIN-1 BYTE. The first draft wrote (char)0xBA
  // and tests/fakes/gfx_fb.cpp's malformed-text recorder caught it in both
  // screen binaries: core/strings_es.h is UTF-8 and gfx draws UTF-8, so a lone
  // 0xBA is an invalid sequence that would have rendered as a replacement box
  // on the panel. "\xC2\xBA" is the same character, encoded.
  snprintf(num, sizeof num, "N\xC2\xBA %02u", (unsigned)s_cur);
  gfx_text_fit(GF_BODY, DX_TEXT_X, DX_ROW_NUM, OLED_W - DX_TEXT_X - 2, num);

  // THE NAME IS THE REWARD. Withheld until the creature has been met, which is
  // the only thing an undiscovered row is missing that a player would want.
  const char* name = known ? pet_species_name(s_cur) : nullptr;
  gfx_text_fit(GF_BODY, DX_TEXT_X, DX_ROW_NAME, OLED_W - DX_TEXT_X - 2,
               (name != nullptr) ? name : S(STR_DEX_UNKNOWN));

  gfx_text_fit(GF_TINY, DX_TEXT_X, DX_ROW_STATE, OLED_W - DX_TEXT_X - 2,
               dex_caught(s_cur) ? S(STR_DEX_HELD)
             : known             ? S(STR_DEX_SEEN)
                                 : S(STR_DEX_MISSING));

  // The neighbours, as slivers: two rows of the body above and below, so the
  // list reads as a ring rather than as one card at a time.
  {
    const uint8_t up = ring_prev(s_cur), dn = ring_next(s_cur);
    gfx_dither_rect(DX_BODY_X, DX_PREV_Y, 24, 2, dex_seen(up) ? GFX_D75 : GFX_D25);
    gfx_dither_rect(DX_BODY_X, DX_NEXT_Y, 24, 2, dex_seen(dn) ? GFX_D75 : GFX_D25);
  }

  gfx_countdown(ui_idle_ms());
  gfx_affordance(S(STR_AF_NEXT), S(STR_AF_BACK));
}
