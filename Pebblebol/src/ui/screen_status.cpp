// =============================================================================
//  PEBBLEBOL - ui/screen_status.cpp
//  STATUS_A / STATUS_B, migrated into the screen table by P2-C11b.
//
//  Both pages are a straight transcription of ui.cpp's draw_status_a() and
//  draw_status_b() onto the gfx.h seam, with two additions the spec asks for:
//  the header tag now carries the LEVEL as well as the mood score, and the
//  first cell of page A is HP - the number that ends a battle - rather than
//  another care stat.
//
//  PURE translation unit.
// =============================================================================
#include "screen_status.h"

#include <stdio.h>
#include <string.h>

#include "../core/strings_es.h"
#include "../data/sprites.h"
#include "../game/genome.h"
#include "gfx.h"
#include "screen.h"
#include "screen_view.h"
#include "ui.h"

static uint8_t  s_page   = 0;        // 0 = A (vitals), 1 = B (genome)
static uint32_t s_hex_ms = 0;
static char     s_hex[33];

void status_a_enter(void) { s_page = 0; }
void status_b_enter(void) { s_page = 1; s_hex_ms = 0; }

// -----------------------------------------------------------------------------
//  A - the vitals
// -----------------------------------------------------------------------------
void status_a_render(void) {
  const PebbleView* v = ui_view();
  char tag[16];
  if (v && v->present) {
    snprintf(tag, sizeof tag, "%s%u %u%%", S(STR_ST_LEVEL),
             (unsigned)v->level, (unsigned)v->mood_pct);
  } else {
    tag[0] = '\0';
  }
  gfx_header(S(STR_ST_TITLE_A), tag);

  if (!v || !v->present) {
    gfx_text_center(GF_NARR, 34, S(STR_UI_NOBODY));
    gfx_countdown(ui_idle_ms());
    gfx_affordance(S(STR_ST_TITLE_B), S(STR_AF_BACK));
    return;
  }

  static const uint8_t kMic[ST_COUNT] = {
    MIC_HUNGER, MIC_HAPPY, MIC_ENERGY, MIC_HYGIENE, MIC_HEALTH, MIC_BOND
  };
  // Two columns of four cells at an 11 px pitch: rows 12, 23, 34, 45.
  for (uint8_t i = 0; i < ST_COUNT; ++i) {
    const int16_t x   = (int16_t)(1 + (i / 4) * 64);
    const int16_t y   = (int16_t)(UI_HDR_H + 1 + (i % 4) * 11);
    // Bar AND number come from the same smoothed value: if the digits jumped
    // while the bar crawled, the screen would be arguing with itself.
    const uint8_t pct = v->care_pct[i];
    const SpriteRef r = sprite_mini(kMic[i]);
    gfx_xbm(x, y, r.w, r.h, r.bits);
    gfx_bar((int16_t)(x + 10), (int16_t)(y + 1), 36, 6, pct);
    char n[6];
    snprintf(n, sizeof n, "%u", (unsigned)pct);
    gfx_text_right(GF_TINY, (int16_t)(x + 61), (int16_t)(y + 6), n);
  }

  // The seventh cell: HP, the derived battle number. The eighth: the age, in
  // the same format the welcome-back banner uses.
  {
    const int16_t x = 65, y = (int16_t)(UI_HDR_H + 1 + 2 * 11);
    char n[16];
    snprintf(n, sizeof n, "%s %u/%u", S(STR_ST_HP),
             (unsigned)v->hp_cur, (unsigned)v->hp_max);
    gfx_text_fit(GF_TINY, x, (int16_t)(y + 6), 61, n);
  }
  {
    const int16_t x = 65, y = (int16_t)(UI_HDR_H + 1 + 3 * 11);
    const SpriteRef r = sprite_mini(MIC_CLOCK);
    gfx_xbm(x, y, r.w, r.h, r.bits);
    gfx_text_fit(GF_TINY, (int16_t)(x + 10), (int16_t)(y + 6), 51, v->age_txt);
  }

  gfx_countdown(ui_idle_ms());
  gfx_affordance(S(STR_ST_TITLE_B), S(STR_AF_BACK));
}

// -----------------------------------------------------------------------------
//  B - the genome
// -----------------------------------------------------------------------------
void status_b_render(void) {
  const PebbleView* v = ui_view();
  gfx_header(S(STR_ST_TITLE_B), nullptr);
  if (!v || !v->present) {
    gfx_text_center(GF_NARR, 34, S(STR_UI_NOBODY));
    gfx_affordance(S(STR_ST_TITLE_A), S(STR_AF_BACK));
    return;
  }

  // DBL_R: the raw genome, 32 hex chars over two lines, UI_HEX_DUMP_MS long.
  if (s_hex_ms && (uint32_t)(ui_now_ms() - s_hex_ms) < UI_HEX_DUMP_MS) {
    char a[17], b[17];
    memcpy(a, s_hex, 16);      a[16] = '\0';
    memcpy(b, s_hex + 16, 16); b[16] = '\0';
    gfx_text_center(GF_TINY, (int16_t)(UI_HDR_H + 14), a);
    gfx_text_center(GF_TINY, (int16_t)(UI_HDR_H + 24), b);
    gfx_rect(8, (int16_t)(UI_HDR_H + 5), OLED_W - 16, 24);
    gfx_countdown(ui_idle_ms());
    gfx_affordance(S(STR_ST_TITLE_A), S(STR_AF_BACK));
    return;
  }

  const Genome& g = v->genome;
  char line[48];

  const SpriteRef badge = sprite_species_badge(gene_species(g));
  gfx_xbm(2, 13, badge.w, badge.h, badge.bits);
  gfx_text_fit(GF_NARR, 17, 20, 108, S_SPECIES(gene_species(g)));
  gfx_text_fit(GF_BODY, 17, 30, 108, S_PATTERN(gene_pattern(g)));

  snprintf(line, sizeof line, "%s %u   %s %s",
           S(STR_ST_GEN), (unsigned)g.generation,
           S(STR_ST_SEX), gene_sex(g) ? S(STR_ST_SEX_X) : S(STR_ST_SEX_O));
  gfx_text_fit(GF_BODY, 2, 39, 124, line);

  snprintf(line, sizeof line, "%s %u   %s %u",
           S(STR_ST_LUCK), (unsigned)gene_luck(g),
           S(STR_ST_MUTATIONS), (unsigned)gene_mutations(g));
  gfx_text_fit(GF_BODY, 2, 47, 124, line);

  gfx_text_fit(GF_BODY, 2, 54, 80, S_TEMPER(gene_temper_class(g)));
  if (gene_rare(g))    gfx_text_right(GF_BODY, OLED_W - 2, 54, S(STR_ST_RARE));
  if (gene_tainted(g)) gfx_text_right(GF_BODY, OLED_W - 2, 39, "*");

  // God-mode entry: BOTH held GOD_ENTER_HOLD_MS. godmode.cpp owns the timing
  // and the entry itself; this only paints the fill bar it reports. Nothing on
  // screen hints at any of it until the hold actually starts.
  const uint8_t prog = ui_god_progress();
  if (prog) {
    const int16_t full = OLED_W - 20;
    int16_t w = (int16_t)(((uint32_t)prog * (uint32_t)full) / 100u);
    if (w > full) w = full;
    gfx_color(GFX_ERASE);
    gfx_fill(6, 42, OLED_W - 12, 14);
    gfx_color(GFX_DRAW);
    gfx_rect(9, 48, (int16_t)(full + 2), 7);
    gfx_fill(10, 49, w, 5);
    gfx_text_center(GF_BODY, 47, S(STR_UI_GOD_HOLD));
  }

  gfx_countdown(ui_idle_ms());
  gfx_affordance(S(STR_ST_TITLE_A), S(STR_AF_BACK));
}

// -----------------------------------------------------------------------------
//  Shared input
// -----------------------------------------------------------------------------
void status_input(Gesture g) {
  switch (g) {
    case GST_TAP_L:
      s_hex_ms = 0;
      ui_goto(s_page ? SCR_STATUS : SCR_STATUS_B);
      break;
    case GST_DBL_R: {
      const PebbleView* v = ui_view();
      if (v && v->present) {
        genome_to_hex32(v->genome, s_hex);
        s_hex_ms = ui_now_ms() ? ui_now_ms() : 1u;
      }
      break;
    }
    default: break;
  }
}
