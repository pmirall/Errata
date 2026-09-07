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
#include "../hardware/audio.h"      // the intro's five cues; pure, no GPIO
#include "../hardware/input.h"      // INPUT_BTN_R only: an index, no driver
#include "anim_ease.h"              // the film maths, shared with the encounter films
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

// -----------------------------------------------------------------------------
//  WHERE THE THREE BODIES STAND.
//
//  Lifted out of setup_pick_render() so the INTRO can ask the same question and
//  get the same answer. That is what makes "the animation's last frame is this
//  screen's first" a structural fact rather than two sets of numbers somebody
//  keeps in step by hand: the bugs climb out of the compile failure and stop
//  exactly where the picker draws them, because both call this.
//
//  THE FRAME HUGS THE ROW, NOT THE CELL. A baby body is about fourteen rows
//  tall inside a twenty-four row atlas box, so a frame drawn around the CELL is
//  mostly empty air above the creature - which reads as "this box is selected"
//  rather than "this creature is". One pass to find the tallest of the three,
//  so all three frames are the same size and the selection moves sideways
//  without changing shape.
//  MEASURED INK, NOT THE SPRITE BOX. ui/petfx_core.h says so at pf_scan_ink()
//  and the derived sleeping pose depends on the same fact - so a frame sized on
//  r.h is a frame around twenty-four rows of which ten are air.
// -----------------------------------------------------------------------------
static void pick_geometry(int16_t* frame_top, int16_t* floor_y, int16_t* tallest) {
  int16_t tall = 0;
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
    if (h > tall) tall = h;
  }
  if (tall <= 0) tall = SU_PICK_BOX;
  // Centre the frame - and therefore the row of bodies - in the band.
  const int16_t ft = (int16_t)(SU_BAND_TOP + (SU_BAND_H - (tall + 2)) / 2);
  if (tallest)    *tallest    = tall;
  if (frame_top)  *frame_top  = ft;
  if (floor_y)    *floor_y    = (int16_t)(ft + tall + 1);
}

// One body: the sprite, and the top-left it is drawn at so that its INK stands
// on `floor_y`. `r.bits` is null for a species with no art, and every caller
// must check - a starter the atlas lost is a blank cell, not a crash.
static SpriteRef pick_body(uint8_t i, int16_t floor_y, int16_t* x, int16_t* y) {
  const uint8_t  key  = pet_art_key(ob_starter_species(i), 0u);
  const SpriteRef r   = sprite_lookup_pose((uint8_t)STAGE_BABY,
                                           sprite_form_of(key, STAGE_BABY),
                                           (uint8_t)POSE_IDLE, 0u);
  if (!r.bits || r.w == 0u || r.h == 0u) return r;
  const int16_t bx = (int16_t)(SU_PICK_X0 + (int16_t)i * SU_PICK_PITCH);
  uint8_t top = 0, bot = 0, left = 0, right = 0;
  const int16_t ink_bot = pf_scan_ink(r.bits, r.w, r.h, &top, &bot, &left, &right)
                            ? (int16_t)bot : (int16_t)(r.h - 1);
  if (x) *x = (int16_t)(bx + (SU_PICK_BOX - (int16_t)r.w) / 2);
  if (y) *y = (int16_t)(floor_y - 1 - ink_bot);
  return r;
}

// =============================================================================
//  THE FIRST-BOOT INTRO. See screen_setup.h for what it is and why it lives
//  inside this screen instead of beside it.
//
//  CUMULATIVE MILLISECOND BOUNDARIES, NOT DURATIONS - ui/actfx.cpp's convention
//  and for its reason: the draw code asks "where am I now", and a table of
//  durations makes every such question a running sum that one edit puts out of
//  step with the next.
// =============================================================================
#define SU_IN_TYPE_MS    6000u    // the code has finished typing itself
#define SU_IN_BUILD_MS   9200u    // the bar has finished filling
#define SU_IN_FAIL_MS   10600u    // the failure has been shown
#define SU_IN_BUG_MS     1100u    // one bug's slot
#define SU_IN_BUGS_MS   (SU_IN_FAIL_MS + 3u * SU_IN_BUG_MS)     // 13900
#define SU_IN_END_MS    (SU_IN_BUGS_MS + 1500u)                 // 15400

// THE CLIMB IS SHORTER THAN THE SLOT ON PURPOSE: a bug arrives, and then it
// STANDS THERE for half a second before the next one does. Three arrivals with
// no gap between them read as one event with three bodies in it.
#define SU_IN_CLIMB_MS    620u
#define SU_IN_CLIMB_DROP   22     // how far below the floor a bug starts

// The typewriter. ONE TICK EVERY TWO CHARACTERS: every character is a machine
// gun at this rate and every fourth is a stutter. Measured against the length
// of the listing below, this is about seven clicks a second for six seconds.
#define SU_IN_TICK_EVERY   2u

// The tear, the same shape ui/screen_encounter.cpp's wild reveal uses and out
// of the same ae_noise(): corruption reads as DISCONTINUITY, so the bars jump
// every SU_IN_STEP_MS and do not move in between.
#define SU_IN_STEP_MS      70u
#define SU_IN_BARS          7u

static_assert(SU_IN_TYPE_MS < SU_IN_BUILD_MS && SU_IN_BUILD_MS < SU_IN_FAIL_MS &&
              SU_IN_FAIL_MS < SU_IN_BUGS_MS && SU_IN_BUGS_MS < SU_IN_END_MS,
              "the intro's boundaries are not in order");
static_assert(SU_IN_CLIMB_MS < SU_IN_BUG_MS,
              "a bug would still be climbing when the next one starts");
static_assert(SU_IN_END_MS <= 20000u,
              "the intro is longer than the budget it was cut to - the brief is "
              "cinematic PLUS three questions under two minutes, and the "
              "questions are answered at the player's pace, not this file's");

// THE LISTING. Pseudo-C with Spanish identifiers, because the fiction is that
// this device is compiling its own pet and the player reads Spanish. It is a
// LITERAL and not a core/strings_es.h row: translating `nueva_vida()` would be
// translating a variable name, and GF_TINY is ASCII-only anyway (ui/gfx.h), so
// a row here could never carry an accent it would draw.
#define SU_IN_LINES   5u
static const char* const kCode[SU_IN_LINES] = {
  "pebble_t nuevo(void) {",
  "  vida  = 100;",
  "  humor = FELIZ;",
  "  return nueva_vida();",
  "}"
};
#define SU_IN_ROW0    ((int16_t)18)
#define SU_IN_ROW_DY  ((int16_t)8)
#define SU_IN_CODE_X  ((int16_t)3)

// EVERY LINE FITS THE PANEL, CHECKED AT BUILD TIME rather than by looking at
// it: GF_TINY advances GFX_ADV_TINY per character (ui/gfx.h), so the widest
// line here is a number this file can compute.
static_assert(SU_IN_CODE_X + 22 * GFX_ADV_TINY <= (int)OLED_W,
              "the widest listing line would run off the panel");
static_assert(SU_IN_ROW0 - GFX_ASC_TINY >= (int)UI_CONTENT_Y,
              "the first listing line would draw into the header");
static_assert(SU_IN_ROW0 + (int)(SU_IN_LINES - 1u) * SU_IN_ROW_DY <=
              (int)UI_CONTENT_BOTTOM,
              "the last listing line would draw under the affordance strip");

// The bar the compile fills, on the last four rows of the content band.
#define SU_IN_BAR_X   ((int16_t)6)
#define SU_IN_BAR_Y   ((int16_t)51)
#define SU_IN_BAR_W   ((int16_t)(OLED_W - 2 * SU_IN_BAR_X))
#define SU_IN_BAR_H   ((int16_t)5)
static_assert(SU_IN_BAR_Y + SU_IN_BAR_H - 1 <= (int)UI_CONTENT_BOTTOM,
              "the compile bar would draw under the affordance strip");
// It stalls short of full, because a bar that reaches 100 % and THEN fails is
// a bar that lied; one that stops at 92 is the build dying where it died.
#define SU_IN_BAR_STALL  92

// ARMED, AND WHEN. Two bytes and a word, exactly as the encounter films: there
// is no per-frame state, because setup_intro_phase() is a pure function of
// (ui_now_ms() - s_in_t0) and cannot be left running by a call somebody forgot.
static uint8_t  s_in      = 0;
static uint32_t s_in_t0   = 0;
// THE CUE BOOKKEEPING, and it is the only mutable thing here that is not a pure
// function of the clock - because a SOUND is an edge and a picture is a state.
static uint8_t  s_in_cued = 0;    // one bit per one-shot cue already fired
static uint16_t s_in_tick = 0;    // typewriter clicks already played

// UNSIGNED SUBTRACTION, so an intro armed 40 ms before the millis() wrap
// measures 40 ms after it and not 4,294,967,256.
static uint32_t su_in_t(void) { return (uint32_t)(ui_now_ms() - s_in_t0); }

void setup_intro_arm(void) {
  s_in      = 1u;
  s_in_t0   = ui_now_ms();
  s_in_cued = 0u;
  s_in_tick = 0u;
}

void setup_intro_cancel(void) { s_in = 0u; }

uint8_t setup_intro_phase(void) {
  if (s_in == 0u) return (uint8_t)SU_IN_NONE;
  const uint32_t t = su_in_t();
  if (t >= SU_IN_END_MS)   return (uint8_t)SU_IN_NONE;   // THE CLOCK ENDS IT
  if (t <  SU_IN_TYPE_MS)  return (uint8_t)SU_IN_TYPE;
  if (t <  SU_IN_BUILD_MS) return (uint8_t)SU_IN_BUILD;
  if (t <  SU_IN_FAIL_MS)  return (uint8_t)SU_IN_FAIL;
  if (t <  SU_IN_BUGS_MS)  return (uint8_t)SU_IN_BUGS;
  return (uint8_t)SU_IN_HOLD;
}

// How many characters of the listing have been typed at `t`. One expression,
// because the DRAWING and the TICKING must not be able to disagree about it.
static uint16_t su_in_typed(uint32_t t) {
  uint16_t total = 0;
  for (uint8_t i = 0; i < SU_IN_LINES; ++i) total = (uint16_t)(total + strlen(kCode[i]));
  if (t >= SU_IN_TYPE_MS) return total;
  return (uint16_t)((uint32_t)total * t / SU_IN_TYPE_MS);
}

// When bug `i` starts climbing.
static uint32_t su_in_bug_t0(uint8_t i) {
  return (uint32_t)(SU_IN_FAIL_MS + (uint32_t)i * SU_IN_BUG_MS);
}

// -----------------------------------------------------------------------------
//  THE CUES. Driven from setup_pick_update(), which is the hook that runs once
//  per frame - never from the render, because a render is called for reasons
//  other than time passing and a cue armed there fires again on every one.
//
//  AT MOST ONE TYPEWRITER CLICK PER CALL, and that is deliberate: a frame that
//  arrives late has several characters' worth of clicks owed to it, and paying
//  the debt would empty the whole queue in one burst. The clicks are a texture,
//  not a count, so dropping the arrears is the right answer.
// -----------------------------------------------------------------------------
static void intro_cues(void) {
  const uint32_t t = su_in_t();
  const uint16_t want = (uint16_t)(su_in_typed(t) / SU_IN_TICK_EVERY);
  if (want > s_in_tick) { s_in_tick = want; audio_play(SFX_TICK); }

  struct Cue { uint32_t at; uint8_t sfx; };
  const Cue kCues[5] = {
    { SU_IN_TYPE_MS,     (uint8_t)SFX_BEEP   },   // the build starts
    { SU_IN_BUILD_MS,    (uint8_t)SFX_GLITCH },   // and dies
    { su_in_bug_t0(0u),  (uint8_t)SFX_CHIRP  },   // one bug
    { su_in_bug_t0(1u),  (uint8_t)SFX_CHIRP  },   // two
    { su_in_bug_t0(2u),  (uint8_t)SFX_CHIRP  }    // three
  };
  for (uint8_t i = 0; i < 5u; ++i) {
    const uint8_t bit = (uint8_t)(1u << i);
    if (t < kCues[i].at || (s_in_cued & bit) != 0u) continue;
    s_in_cued = (uint8_t)(s_in_cued | bit);
    audio_play(kCues[i].sfx);
  }
}

// -----------------------------------------------------------------------------
//  THE DRAWING. One clipped writer, ui/screen_encounter.cpp's rule in this
//  screen's coordinates: sets, never clears, and confined to the content band,
//  so no arithmetic here can scribble on the header or the affordance strip.
// -----------------------------------------------------------------------------
static void su_hline(int16_t x, int16_t y, int16_t w) {
  if (y < (int16_t)UI_CONTENT_Y || y > (int16_t)UI_CONTENT_BOTTOM) return;
  for (int16_t i = 0; i < w; ++i) {
    const int16_t px = (int16_t)(x + i);
    if (px < 0 || px >= (int16_t)OLED_W) continue;
    gfx_pixel(px, y);
  }
}

#define SU_IN_BAND_H ((int16_t)(UI_CONTENT_BOTTOM - UI_CONTENT_Y + 1))

static void su_in_tear(uint32_t t, uint8_t bars) {
  const uint8_t step = (uint8_t)((t / SU_IN_STEP_MS) & 0xFFu);
  for (uint8_t i = 0; i < bars; ++i) {
    const int16_t y = (int16_t)((int16_t)UI_CONTENT_Y +
                                (int16_t)(ae_noise(step, i) % (uint8_t)SU_IN_BAND_H));
    const int16_t x = (int16_t)(ae_noise(step, (uint8_t)(i + 64u)) % 90u);
    const int16_t w = (int16_t)(10u + (ae_noise(step, (uint8_t)(i + 128u)) % 50u));
    su_hline(x, y, w);
  }
}

// The listing, typed. `n` characters of it are on the panel; the rest is not
// drawn at all, and the cursor sits after the last one.
static void su_in_code(uint16_t n) {
  char row[40];
  uint16_t left = n;
  for (uint8_t i = 0; i < SU_IN_LINES; ++i) {
    const size_t len = strlen(kCode[i]);
    const size_t take = (left >= len) ? len : (size_t)left;
    if (take > sizeof row - 1u) return;               // unreachable; not assumed
    memcpy(row, kCode[i], take);
    row[take] = '\0';
    const int16_t y = (int16_t)(SU_IN_ROW0 + (int16_t)i * SU_IN_ROW_DY);
    gfx_text(GF_TINY, SU_IN_CODE_X, y, row);
    if (take < len || (left == len && i + 1u == SU_IN_LINES)) {
      // THE CARET, a solid cell after the last character typed. It is what
      // makes six seconds of static text read as somebody writing.
      const int16_t cx = (int16_t)(SU_IN_CODE_X + (int16_t)take * GFX_ADV_TINY);
      gfx_fill(cx, (int16_t)(y - GFX_ASC_TINY + 1), (int16_t)GFX_ADV_TINY,
               (int16_t)GFX_ASC_TINY);
      return;                                         // nothing below is typed yet
    }
    left = (uint16_t)(left - take);
  }
}

static void draw_intro(void) {
  const uint32_t t  = su_in_t();
  const uint8_t  ph = setup_intro_phase();

  if (ph == (uint8_t)SU_IN_TYPE || ph == (uint8_t)SU_IN_BUILD) {
    // THE FILENAME IS THE TITLE, and it is a literal for the same reason the
    // listing is: `pebble.c` is not copy, it is the name of the thing being
    // compiled. The tag is what changes when the build starts.
    gfx_header("pebble.c",
               (ph == (uint8_t)SU_IN_BUILD) ? S(STR_IN_BUILD) : nullptr);
    su_in_code(su_in_typed(t));
    if (ph == (uint8_t)SU_IN_BUILD) {
      const uint8_t pct = (uint8_t)((uint32_t)SU_IN_BAR_STALL *
                                    ae_pct(t, SU_IN_TYPE_MS, SU_IN_BUILD_MS) / 100u);
      gfx_bar(SU_IN_BAR_X, SU_IN_BAR_Y, SU_IN_BAR_W, SU_IN_BAR_H, pct,
              GFX_BAR_SOLID);
    }
    return;
  }

  if (ph == (uint8_t)SU_IN_FAIL) {
    // THE BUILD DIED WHERE IT DIED. The listing is still there under the tear,
    // and the bar is frozen at SU_IN_BAR_STALL rather than snapped to 0 or run
    // to 100: a progress bar that reaches full and then reports a failure is a
    // progress bar that lied about the last frame.
    gfx_header(S(STR_IN_ERROR), nullptr);
    su_in_code(su_in_typed(t));
    gfx_bar(SU_IN_BAR_X, SU_IN_BAR_Y, SU_IN_BAR_W, SU_IN_BAR_H,
            (uint8_t)SU_IN_BAR_STALL, GFX_BAR_SOLID);
    su_in_tear(t, (uint8_t)SU_IN_BARS);
    return;
  }

  // BUGS and HOLD. The header is ALREADY the picker's, and the bodies are
  // ALREADY where the picker draws them - which is the whole reason this is a
  // phase of this screen. What appears at the cut is the selection frame, the
  // species name and the hint, and nothing moves.
  gfx_header(S(STR_SU_PICK_TITLE), nullptr);
  int16_t floor_y = 0;
  pick_geometry(nullptr, &floor_y, nullptr);

  for (uint8_t i = 0; i < (uint8_t)OB_STARTER_COUNT; ++i) {
    const uint32_t t0 = su_in_bug_t0(i);
    if (t < t0) continue;                             // this one has not arrived
    int16_t x = 0, y = 0;
    const SpriteRef r = pick_body(i, floor_y, &x, &y);
    if (!r.bits) continue;
    // OUT OF THE FLOOR AND PAST IT. A straight lerp lands like a lift; the
    // parabola on top overshoots by two pixels and settles, which is the
    // difference between a body arriving and a body being placed.
    const uint32_t el = (uint32_t)(t - t0);
    int16_t dy = 0;
    if (el < SU_IN_CLIMB_MS) {
      dy = (int16_t)(ae_lerp(el, SU_IN_CLIMB_MS, (int16_t)SU_IN_CLIMB_DROP, 0) +
                     ae_hop(el, SU_IN_CLIMB_MS, 2));
    }
    gfx_xbm_t(x, (int16_t)(y + dy), r.w, r.h, r.bits);
  }

  // The tear thins out under the first arrival and is gone by the second: the
  // corruption is what the bugs CAME OUT OF, so it may not outlive them.
  if (t < su_in_bug_t0(1u)) {
    const uint8_t gone = ae_pct(t, SU_IN_FAIL_MS, su_in_bug_t0(1u));
    su_in_tear(t, (uint8_t)((uint16_t)SU_IN_BARS * (uint16_t)(100u - gone) / 100u));
  }

  if (ph == (uint8_t)SU_IN_HOLD) {
    gfx_text_center(GF_BODY, 48, S(STR_IN_FOUND));
    gfx_affordance(S(STR_AF_NEXT), S(STR_AF_OTHER));
  }
}

static void pick_step(void) {
  s_pick = (uint8_t)((s_pick + 1u) % (uint8_t)OB_STARTER_COUNT);
  ui_note_input();
}

static void pick_commit(void) {
  const uint8_t sp = ob_starter_species(s_pick);
  if (sp != 0u) (void)ui_set_starter(sp);
  // THE ONLY OTHER FANFARE IN THE PRODUCT IS WINNING A BATTLE, and that is the
  // company this moment should keep: it is the one press on a fresh device that
  // the player will remember. SKIP (A+B) keeps the default Pebble and gets no
  // fanfare, because nothing was chosen.
  audio_play(SFX_FANFARE);
  ui_input_flush();
  setup_advance((uint8_t)OB_STARTER);
}

void setup_pick_render(void) {
  if (setup_intro_phase() != (uint8_t)SU_IN_NONE) { draw_intro(); return; }
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
  int16_t tallest = 0, frame_top = 0, floor_y = 0;
  pick_geometry(&frame_top, &floor_y, &tallest);

  for (uint8_t i = 0; i < (uint8_t)OB_STARTER_COUNT; ++i) {
    const int16_t bx = (int16_t)(SU_PICK_X0 + (int16_t)i * SU_PICK_PITCH);
    int16_t x = 0, y = 0;
    const SpriteRef r = pick_body(i, floor_y, &x, &y);
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
  // THE INTRO OWNS THE FRAME AND THE BUTTONS WHILE IT RUNS. Its cues are armed
  // here and nowhere else, because this is the hook that runs once per frame;
  // a cue armed in the render fires again every time something else asks for a
  // redraw. And the auto-repeat below must not run under it: a player holding
  // the button down through the cinematic would arrive at the picker with the
  // cursor already spun, which is a choice they never made.
  if (setup_intro_phase() != (uint8_t)SU_IN_NONE) { intro_cues(); s_rep_ms = 0; return; }
  if (!ui_btn_down(INPUT_BTN_R)) { s_rep_ms = 0; return; }
  if (ui_btn_hold_ms(INPUT_BTN_R) < (uint32_t)REPEAT_START_MS) return;
  if (s_rep_ms != 0 && (uint32_t)(now_ms - s_rep_ms) < (uint32_t)REPEAT_RATE_MS) return;
  s_rep_ms = now_ms;
  pick_step();
}

void setup_pick_input(Gesture g) {
  // ANY PRESS SKIPS THE INTRO, AND ONLY SKIPS. A player who has pressed
  // something has stopped watching - but a press that also chose a starter
  // would make the impatient player's very first act on the device an accident,
  // and the choice it made would be permanent. The cancel is a courtesy; the
  // bound is the clock (screen_setup.h).
  if (setup_intro_phase() != (uint8_t)SU_IN_NONE) {
    setup_intro_cancel();
    ui_input_flush();
    ui_note_input();
    return;
  }
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
