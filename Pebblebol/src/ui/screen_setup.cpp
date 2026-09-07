// =============================================================================
//  PEBBLEBOL - ui/screen_setup.cpp
//  See screen_setup.h. PURE translation unit.
// =============================================================================
#include "screen_setup.h"

#include <stdio.h>
#include <string.h>

#include "../app/onboarding.h"
#include "../core/config.h"
#include "../core/strings_es.h"
#include "../core/utf8.h"
#include "../data/sprites.h"
#include "../hardware/input.h"      // INPUT_BTN_R only: an index, no driver
#include "gfx.h"
#include "pet_art.h"
#include "petfx_core.h"   // pf_scan_ink(): the body's real ink box
#include "screen.h"
#include "ui.h"

// =============================================================================
//  THE CHARACTER RING
//
//  Two buttons and no keyboard, so a name is typed the way a combination lock
//  is turned: one cell at a time, R steps the character under the cursor.
//
//  UPPERCASE ONLY, AND THAT IS A DELIBERATE HALVING. Adding lower case would
//  take the ring from 46 entries to 72 and cost the player up to 26 extra taps
//  per character for a distinction the header bar's bold face barely shows.
//  The accented vowels and the n-tilde ARE here, because a Spanish device that
//  cannot spell a Spanish name is the accessibility failure, not the ring
//  length - and they are what makes the stored name LATIN-1 rather than ASCII,
//  which is the whole reason core/utf8.h exists (a stored name is Latin-1, a
//  drawn one is UTF-8, and there is exactly one crossing).
//
//  The bytes are Latin-1, which is what game/validate.cpp's
//  creator_name_char_ok() admits and what ConfigV2.device_name holds. Index 0
//  is SPACE, so an untouched cell is blank and a name can be shorter than the
//  field without a separate "end" entry.
// =============================================================================
static const char kRing[] = {
  ' ',
  'A','B','C','D','E','F','G','H','I','J','K','L','M',
  'N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
  (char)0xD1,                                    // N-tilde
  (char)0xC1, (char)0xC9, (char)0xCD, (char)0xD3, (char)0xDA, (char)0xDC,
  '0','1','2','3','4','5','6','7','8','9',
  '-', '.'
};
#define SU_RING_LEN ((uint8_t)(sizeof kRing / sizeof kRing[0]))
static_assert(SU_RING_LEN == 46, "the character ring changed length - "
                                 "tools/check.sh counts it and screen_setup.h "
                                 "documents the tap cost");

// The name field, one ring index per cell. NAME_MAX_LEN cells, which is what
// ConfigV2.device_name holds minus its terminator.
static uint8_t s_cell[NAME_MAX_LEN];
static uint8_t s_cur     = 0;
static uint32_t s_rep_ms = 0;
static uint8_t s_pick    = 0;

// The Latin-1 name the cells spell, rebuilt on demand. TRAILING SPACES ARE
// TRIMMED and so are leading ones: this is the device's own name, typed on the
// device, and "   PACO" is a name nobody meant. game/validate.h argues at
// length against MENDING a name - and that argument is about a name arriving
// from OUTSIDE, where two names the user typed differently must not compare
// equal on a device that never said so. Here there is no outside and no
// comparison; there is a player who cannot see a trailing space.
static char s_name[NAME_MAX_LEN + 1];

static void rebuild_name(void) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < (uint8_t)NAME_MAX_LEN; ++i)
    s_name[n++] = kRing[s_cell[i] % SU_RING_LEN];
  s_name[n] = '\0';
  while (n > 0u && s_name[n - 1u] == ' ') s_name[--n] = '\0';
  uint8_t lead = 0;
  while (s_name[lead] == ' ') ++lead;
  if (lead > 0u) {
    uint8_t o = 0;
    while (s_name[lead] != '\0') s_name[o++] = s_name[lead++];
    s_name[o] = '\0';
  }
}

uint8_t     setup_name_cursor(void) { return s_cur; }
const char* setup_name_text(void)   { rebuild_name(); return s_name; }
uint8_t     setup_pick_cursor(void) { return s_pick; }
uint8_t     setup_ring_len(void)    { return SU_RING_LEN; }
char        setup_ring_at(uint8_t i){ return kRing[i % SU_RING_LEN]; }

// =============================================================================
//  THE FLOW
// =============================================================================
bool setup_in_flow(void) {
  const Config* c = ui_cfg();
  return c && ob_active(ob_step(*c));
}

void setup_advance(uint8_t done_step) {
  const uint8_t next = ob_next(done_step);
  Config* c = ui_cfg();
  if (c) {
    ob_set_step(*c, next);
    // THE CONFIG AND THE BOX, silently. ui.h says why the Box half is not
    // optional: a save with no Box in it is one persistence/save_manager.cpp
    // reports as LOAD_FRESH and whose config never reaches the caller.
    ui_setup_persist();
  }
  if (next == (uint8_t)OB_DONE) {
    ui_toast(STR_SU_DONE);
    ui_replace_root(SCR_HOME);
  } else {
    ui_replace_root(ob_screen_for(next));
  }
}

// Invariant 2 on a setup screen: end the flow, keep every default already in
// place, and land on HOME. Nothing is destroyed because nothing was replaced -
// see screen_setup.h.
void setup_finish_now(void) {
  Config* c = ui_cfg();
  if (c) { ob_set_step(*c, (uint8_t)OB_DONE); ui_setup_persist(); }
  ui_replace_root(SCR_HOME);
}

// =============================================================================
//  1. THE NAMING SCREEN
// =============================================================================
#define SU_CELL_W     6                       // GFX_ADV_NARR
#define SU_FIELD_X    ((int16_t)((OLED_W - NAME_MAX_LEN * SU_CELL_W) / 2))
#define SU_FIELD_BASE ((int16_t)38)
#define SU_RULE_Y     ((int16_t)40)

void setup_name_enter(void) {
  // START FROM THE NAME THE DEVICE ALREADY HAS, which is empty on a true first
  // boot and is whatever was typed when the step is resumed after a power cut.
  memset(s_cell, 0, sizeof s_cell);
  s_cur    = 0;
  s_rep_ms = 0;
  const Config* c = ui_cfg();
  if (c) {
    for (uint8_t i = 0; i < (uint8_t)NAME_MAX_LEN && c->pet_name[i] != '\0'; ++i) {
      for (uint8_t r = 0; r < SU_RING_LEN; ++r)
        if (kRing[r] == c->pet_name[i]) { s_cell[i] = r; break; }
    }
  }
}

static void name_bump(void) {
  s_cell[s_cur] = (uint8_t)((s_cell[s_cur] + 1u) % SU_RING_LEN);
  ui_note_input();
}

static void name_commit(void) {
  rebuild_name();
  Config* c = ui_cfg();
  if (c) {
    uint8_t i = 0;
    while (s_name[i] != '\0' && i + 1u < (uint8_t)sizeof c->pet_name) {
      c->pet_name[i] = s_name[i];
      ++i;
    }
    c->pet_name[i] = '\0';
  }
  ui_input_flush();          // the release of the confirming hold must not fire
  setup_advance((uint8_t)OB_NAME);
}

void setup_name_render(void) {
  gfx_header(S(STR_SU_NAME_TITLE), nullptr);
  gfx_text_fit(GF_BODY, 2, 21, OLED_W - 4, S(STR_SU_HELLO));

  // One cell at a time, so the cursor rule under it lines up exactly and a
  // two-byte character does not slide the field. Each cell is transcoded on
  // its own: the ring is Latin-1 and the panel draws UTF-8 (core/utf8.h).
  for (uint8_t i = 0; i < (uint8_t)NAME_MAX_LEN; ++i) {
    const char ch = kRing[s_cell[i] % SU_RING_LEN];
    const int16_t x = (int16_t)(SU_FIELD_X + i * SU_CELL_W);
    if (ch != ' ') {                       // a blank cell is its rule and nothing else
      char one[2] = { ch, '\0' };
      char utf[4];
      (void)u8_from_latin1(utf, (uint16_t)sizeof utf, one);
      gfx_text(GF_NARR, x, SU_FIELD_BASE, utf);
    }
    // Every cell gets a base rule so the field reads as a field even when it is
    // empty; the cursor's is solid across the whole cell.
    if (i == s_cur) gfx_hline(x, SU_RULE_Y, SU_CELL_W - 1);
    else            gfx_pixel(x, SU_RULE_Y);
  }

  gfx_text_fit(GF_TINY, 2, 49, OLED_W - 4, S(STR_SU_NAME_HINT));
  gfx_text_fit(GF_TINY, 2, 55, OLED_W - 4, S(STR_SU_SKIP));
  // NO gfx_countdown(): the row is SF_STICKY and drawing a drain that never
  // arrives is a lie the bar tells rather than a decoration.
  gfx_affordance(S(STR_AF_NEXT), S(STR_AF_CHAR));
}

// The right button's auto-repeat, character for character with ui/screen_time.cpp's.
void setup_name_update(uint32_t now_ms) {
  if (!ui_btn_down(INPUT_BTN_R)) { s_rep_ms = 0; return; }
  if (ui_btn_hold_ms(INPUT_BTN_R) < (uint32_t)REPEAT_START_MS) return;
  if (s_rep_ms != 0 && (uint32_t)(now_ms - s_rep_ms) < (uint32_t)REPEAT_RATE_MS) return;
  s_rep_ms = now_ms;
  name_bump();
}

void setup_name_input(Gesture g) {
  switch (g) {
    case GST_TAP_R:  name_bump(); break;
    case GST_HOLD_R: s_rep_ms = ui_now_ms(); name_bump(); break;
    case GST_TAP_L:  s_cur = (uint8_t)((s_cur + 1u) % (uint8_t)NAME_MAX_LEN); break;
    case GST_HOLD_L: name_commit(); break;
    case GST_BOTH:
      // SKIP. The device keeps the name it already has, which on a first boot
      // is none - and ui_pet_name() then falls through to the species, exactly
      // as it did before this screen existed.
      ui_input_flush();
      setup_advance((uint8_t)OB_NAME);
      break;
    case GST_LONG_BOTH:
      ui_input_flush();
      setup_finish_now();
      break;
    default: break;
  }
}

// =============================================================================
//  2. THE STARTER SCREEN
// =============================================================================
#define SU_PICK_BOX   ((int16_t)24)                 // the atlas body box
#define SU_PICK_PITCH ((int16_t)42)
#define SU_PICK_X0    ((int16_t)((OLED_W - (2 * SU_PICK_PITCH + SU_PICK_BOX)) / 2))
// The band the three bodies live in: under the header, above the species name.
// The FLOOR is computed per frame from the measured ink (see the render), so
// the row is centred on what is actually drawn rather than on a 24 px atlas box
// that is a third empty.
#define SU_BAND_TOP   ((int16_t)(UI_HDR_H + 1))                 // 12
#define SU_BAND_BOT   ((int16_t)42)                             // above the name line
#define SU_BAND_H     ((int16_t)(SU_BAND_BOT - SU_BAND_TOP + 1))

// The three bodies must fit between the header and the name line, with the
// selection frame's one pixel of margin on every side.
static_assert(SU_PICK_X0 >= 1, "the starter row would open off the left edge");
static_assert(SU_PICK_X0 + 2 * SU_PICK_PITCH + SU_PICK_BOX <= OLED_W - 1,
              "the starter row would open off the right edge");
static_assert(SU_BAND_TOP >= UI_HDR_H + 1,
              "a selection frame would draw into the header bar");
static_assert(SU_BAND_BOT + 1 <= 43,
              "a selection frame would draw through the species name line");
static_assert(SU_BAND_H >= SU_PICK_BOX + 2,
              "the band cannot hold a whole body box and its frame");

void setup_pick_enter(void) {
  s_pick   = 0;
  s_rep_ms = 0;
}

static void pick_step(void) {
  s_pick = (uint8_t)((s_pick + 1u) % (uint8_t)OB_STARTER_COUNT);
  ui_note_input();
}

static void pick_commit(void) {
  const uint8_t sp = ob_starter_species(s_pick);
  if (sp != 0u) (void)ui_set_starter(sp);
  ui_input_flush();
  setup_advance((uint8_t)OB_STARTER);
}

void setup_pick_render(void) {
  gfx_header(S(STR_SU_PICK_TITLE), nullptr);

  // THE FRAME HUGS THE ROW, NOT THE CELL. A baby body is about fourteen rows
  // tall inside a twenty-four row atlas box, so a frame drawn around the CELL
  // is mostly empty air above the creature - which reads as "this box is
  // selected" rather than "this creature is". One pass to find the tallest of
  // the three, so all three frames are the same size and the selection moves
  // sideways without changing shape.
  // MEASURED INK, NOT THE SPRITE BOX. Every body in the atlas is 24x24 with an
  // empty margin above it - ui/petfx_core.h says so at pf_scan_ink() and the
  // derived sleeping pose depends on the same fact - so a frame sized on r.h is
  // a frame around twenty-four rows of which ten are air.
  int16_t tallest = 0;
  for (uint8_t i = 0; i < (uint8_t)OB_STARTER_COUNT; ++i) {
    const uint8_t key = pet_art_key(ob_starter_species(i), 0u);
    const SpriteRef r = sprite_lookup_pose((uint8_t)STAGE_BABY,
                                           sprite_form_of(key, STAGE_BABY),
                                           (uint8_t)POSE_IDLE, 0u);
    if (!r.bits) continue;
    uint8_t top = 0, bot = 0, left = 0, right = 0;
    const int16_t h = pf_scan_ink(r.bits, r.w, r.h, &top, &bot, &left, &right)
                        ? (int16_t)(bot - top + 1)
                        : (int16_t)r.h;
    if (h > tallest) tallest = h;
  }
  if (tallest <= 0) tallest = SU_PICK_BOX;
  // Centre the frame - and therefore the row of bodies - in the band.
  const int16_t frame_top = (int16_t)(SU_BAND_TOP + (SU_BAND_H - (tallest + 2)) / 2);
  const int16_t floor_y   = (int16_t)(frame_top + tallest + 1);

  for (uint8_t i = 0; i < (uint8_t)OB_STARTER_COUNT; ++i) {
    const uint8_t  sp   = ob_starter_species(i);
    const uint8_t  key  = pet_art_key(sp, 0u);
    const uint8_t  form = sprite_form_of(key, STAGE_BABY);
    const SpriteRef r   = sprite_lookup_pose((uint8_t)STAGE_BABY, form,
                                             (uint8_t)POSE_IDLE, 0u);
    const int16_t bx = (int16_t)(SU_PICK_X0 + (int16_t)i * SU_PICK_PITCH);
    if (r.bits && r.w > 0 && r.h > 0) {
      // Centred in the cell and standing on the shared floor, so three bodies
      // of different sizes line up on one ground line.
      // TRANSPARENT, AND THE FIRST DRAFT WAS NOT - which put a 24 px hole in
      // the header bar. gfx_xbm() is OPAQUE (ui/gfx.h): it paints the whole
      // w*h box, the 0-bits in the INVERSE, so an atlas body whose ink is
      // fourteen rows tall still erases twenty-four - and the empty margin
      // above a body standing on this floor reaches up into UI_HDR_H. The
      // ink itself never leaves the band, so the picture is identical
      // everywhere it matters and correct where it did not. This is the seam
      // P10-C3 split in two, biting on the first new screen to draw a body
      // over anything.
      uint8_t top = 0, bot = 0, left = 0, right = 0;
      const int16_t ink_bot = pf_scan_ink(r.bits, r.w, r.h, &top, &bot, &left, &right)
                                ? (int16_t)bot : (int16_t)(r.h - 1);
      const int16_t x = (int16_t)(bx + (SU_PICK_BOX - (int16_t)r.w) / 2);
      const int16_t y = (int16_t)(floor_y - 1 - ink_bot);
      gfx_xbm_t(x, y, r.w, r.h, r.bits);
    }
    if (i == s_pick)
      gfx_rect((int16_t)(bx - 1), frame_top,
               (int16_t)(SU_PICK_BOX + 2), (int16_t)(tallest + 2));
  }

  const char* nm = pet_species_name(ob_starter_species(s_pick));
  gfx_text_center(GF_BODY, 48, nm ? nm : "");
  gfx_text_fit(GF_TINY, 2, 55, OLED_W - 4, S(STR_SU_PICK_HINT));
  gfx_affordance(S(STR_AF_NEXT), S(STR_AF_OTHER));
}

void setup_pick_update(uint32_t now_ms) {
  if (!ui_btn_down(INPUT_BTN_R)) { s_rep_ms = 0; return; }
  if (ui_btn_hold_ms(INPUT_BTN_R) < (uint32_t)REPEAT_START_MS) return;
  if (s_rep_ms != 0 && (uint32_t)(now_ms - s_rep_ms) < (uint32_t)REPEAT_RATE_MS) return;
  s_rep_ms = now_ms;
  pick_step();
}

void setup_pick_input(Gesture g) {
  switch (g) {
    case GST_TAP_R:  pick_step(); break;
    case GST_HOLD_R: s_rep_ms = ui_now_ms(); pick_step(); break;
    case GST_TAP_L:  pick_step(); break;      // one field: "next" IS "another"
    case GST_HOLD_L: pick_commit(); break;
    case GST_BOTH:
      // SKIP keeps the Pebble app/app.cpp already minted, which is
      // ob_starter_species(0) - the historical starter. Nothing is replaced.
      ui_input_flush();
      setup_advance((uint8_t)OB_STARTER);
      break;
    case GST_LONG_BOTH:
      ui_input_flush();
      setup_finish_now();
      break;
    default: break;
  }
}
