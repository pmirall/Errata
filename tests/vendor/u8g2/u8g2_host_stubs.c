// =============================================================================
//  tests/vendor/u8g2/u8g2_host_stubs.c
//  The three U8g2 symbols the vendored slice references but never reaches.
//
//  Vendoring csrc/u8g2_font.c and csrc/u8x8_8x8.c verbatim - rather than
//  retyping U8g2's glyph decoder and UTF-8 state machine, which is how a
//  capture stops matching the panel - pulls in three references to code that
//  lives in files this build has no other reason to carry. Each is unreachable
//  from the text path, and the reason is stated rather than assumed:
//
//    u8g2_GetKerning / u8g2_GetKerningByTable
//        u8g2_font.c consults these only when u8g2->kerning_table is non-null.
//        gfx_real.cpp zeroes the whole u8g2_t and never sets one, so a kerning
//        table cannot exist. Returning 0 is also what U8g2 itself returns for
//        a pair absent from a table.
//
//    u8x8_DrawTile
//        The 8x8 tile display path. u8x8_8x8.c is here for u8x8_utf8_next()
//        alone; nothing in gfx_real.cpp calls u8x8's own drawing.
//
//  If one of these ever DOES get called the picture would be silently wrong, so
//  they abort rather than return quietly.
// =============================================================================
#include <stdio.h>
#include <stdlib.h>

#include "u8g2.h"

static void unreachable(const char* who) {
  fprintf(stderr, "gfx_real: %s was called; the vendored U8g2 slice assumed it "
                  "could not be. The capture would be wrong.\n", who);
  abort();
}

uint8_t u8g2_GetKerning(u8g2_t *u8g2, u8g2_kerning_t *kerning, uint16_t e1, uint16_t e2) {
  (void)u8g2; (void)kerning; (void)e1; (void)e2;
  unreachable("u8g2_GetKerning");
  return 0;
}

uint8_t u8g2_GetKerningByTable(u8g2_t *u8g2, const uint16_t *kt, uint16_t e1, uint16_t e2) {
  (void)u8g2; (void)kt; (void)e1; (void)e2;
  unreachable("u8g2_GetKerningByTable");
  return 0;
}

uint8_t u8x8_DrawTile(u8x8_t *u8x8, uint8_t x, uint8_t y, uint8_t cnt, uint8_t *tile_ptr) {
  (void)u8x8; (void)x; (void)y; (void)cnt; (void)tile_ptr;
  unreachable("u8x8_DrawTile");
  return 0;
}
