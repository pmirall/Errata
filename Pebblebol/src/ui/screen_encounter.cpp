// =============================================================================
//  PEBBLEBOL - ui/screen_encounter.cpp
//  See screen_encounter.h. PURE translation unit.
// =============================================================================
#include "screen_encounter.h"

#include <stdio.h>
#include <string.h>

#include "../core/strings_es.h"
#include "../data/sprites.h"
#include "../game/box.h"
#include "../game/capture.h"
#include "../game/corruption.h"
#include "../game/inventory.h"
#include "../game/species.h"
#include "../game/xp.h"
#include "anim_ease.h"      // the film maths, lifted out of ui/actfx.cpp at P10-C3
#include "../hardware/audio.h"
#include "gfx.h"
#include "pet_art.h"          // pet_species_name(): the roster's own Spanish name
#include "screen.h"
#include "ui.h"

#define ENC_ROW_CAP   24

// ~28 B of globals, and every byte of it is the encounter the player is
// looking at.
static EncounterResult s_enc;
static CaptureState    s_cap;
static uint8_t         s_cat     = 0;
static uint8_t         s_cursor  = 0;    // 0 = CAPTURAR, 1 = DEJAR
static uint8_t         s_applied = 0;    // this encounter's entry work has run
static uint8_t         s_mode    = (uint8_t)CSM_READY;
static uint8_t         s_out     = (uint8_t)CAP_ESCAPED;
static uint8_t         s_item    = 0;    // the capture item a throw will spend
static uint16_t        s_reward  = 0;    // XP the SPECIAL burst paid

const EncounterResult& encounter_screen_result(void) { return s_enc; }
uint8_t encounter_screen_cursor(void)  { return s_cursor; }
uint8_t capture_screen_mode(void)      { return s_mode; }
uint8_t capture_screen_outcome(void)   { return s_out; }
uint8_t capture_screen_item(void)      { return s_item; }

// =============================================================================
//  THE TWO FILMS. See screen_encounter.h for why they live here and what the
//  interruption contract is.
//
//  CUMULATIVE MILLISECOND BOUNDARIES, NOT DURATIONS - ui/actfx.cpp's convention
//  and for its reason: the draw code asks "where am I now", and a table of
//  durations makes every such question a running sum that one edit puts out of
//  step with the next.
// =============================================================================
#define ENC_ITEM_RISE_MS    520u    // the climb ends
#define ENC_ITEM_END_MS     980u    // the whole item film ends
#define ENC_CAP_CLAMP_MS    420u    // the brackets have closed
#define ENC_CAP_PULL_MS     760u    // the body has gone
#define ENC_CAP_END_MS     1080u    // the whole capture film ends
#define ENC_WILD_TEAR_MS    300u    // the scanline tear is over
#define ENC_WILD_FORM_MS    760u    // the body has finished assembling
#define ENC_WILD_SNAP_MS    880u    // the band goes inverse from here
#define ENC_WILD_END_MS     980u    // the whole wild film ends

// HOW LONG ONE TORN PATTERN STANDS. Corruption reads as DISCONTINUITY: anything
// that eases between two states looks alive, which is the opposite of what a
// glitch is. So the bars jump every ENC_WILD_STEP_MS and do not move in
// between - three frames at 30 fps, slow enough to be seen as a pattern and
// fast enough that six of them fit in the tear.
#define ENC_WILD_STEP_MS     70u
#define ENC_WILD_BARS         6u    // scanlines at full corruption

static_assert(ENC_WILD_TEAR_MS < ENC_WILD_FORM_MS &&
              ENC_WILD_FORM_MS < ENC_WILD_SNAP_MS &&
              ENC_WILD_SNAP_MS < ENC_WILD_END_MS,
              "the wild film's boundaries are not in order");

// GEOMETRY. Everything either film draws is inside this box, which is the
// content band and nothing else: the header owns rows 0..UI_HDR_H-1 and the
// affordance strip owns UI_AFFORD_Y.., and a film that reached either would be
// writing in furniture another module redraws every frame.
#define ENC_STAGE_Y0   ((int16_t)UI_CONTENT_Y)
#define ENC_STAGE_Y1   ((int16_t)UI_CONTENT_BOTTOM)

#define ENC_ICON_W      12
#define ENC_ICON_X     ((int16_t)((OLED_W - ENC_ICON_W) / 2))
#define ENC_ICON_TOP    26          // where the drop finishes
#define ENC_ICON_BOT    43          // where it starts, down on the name line

#define ENC_BODY_W      24
#define ENC_BODY_X     ((int16_t)((OLED_W - ENC_BODY_W) / 2))
#define ENC_BODY_Y      22          // rows 22..45
#define ENC_CLAMP_FAR    8          // how far out the brackets start
#define ENC_CLAMP_NEAR   2          // where they stop
#define ENC_CLAMP_ARM    5          // each bracket arm's length

static_assert(ENC_ICON_TOP >= (int)UI_CONTENT_Y, "the drop starts in the header");
static_assert(ENC_ICON_BOT + ENC_ICON_W - 1 <= (int)UI_CONTENT_BOTTOM,
              "the drop starts under the affordance strip");
static_assert(ENC_BODY_Y + ENC_BODY_W - 1 <= (int)UI_CONTENT_BOTTOM,
              "the captured body would stand on the affordance strip");
// THE BRACKETS ARE CHECKED AT THEIR WIDEST, NOT AT THEIR NEAREST, and that is
// the whole value of these five lines. enc_px() clips, so a bracket that opened
// past the band would simply lose its bottom arm on the first frames of every
// capture and the golden would show a film that starts half-drawn - a defect
// that looks like art. The vertical reach is the binding one: the body's box
// ends at row ENC_BODY_Y + ENC_BODY_W - 1 and the affordance strip starts three
// rows later.
static_assert(ENC_BODY_Y - 1 - ENC_CLAMP_FAR >= (int)UI_CONTENT_Y,
              "a bracket would open into the header");
static_assert(ENC_BODY_Y + ENC_BODY_W + ENC_CLAMP_FAR <= (int)UI_CONTENT_BOTTOM,
              "a bracket would open under the affordance strip");
static_assert(ENC_BODY_X - 1 - ENC_CLAMP_FAR >= 0,
              "a bracket would open off the left of the panel");
static_assert(ENC_BODY_X + ENC_BODY_W + ENC_CLAMP_FAR < (int)OLED_W,
              "a bracket would open off the right of the panel");
static_assert(ENC_CLAMP_NEAR < ENC_CLAMP_FAR, "the brackets close the wrong way");

// WHICH FILM AND WHEN IT STARTED. Two bytes and a word; there is no service()
// hook and no per-frame state, because enc_film_phase() is a pure function of
// (ui_now_ms() - s_film_t0) and cannot be left running by a call somebody
// forgot to make.
enum : uint8_t { ENC_F_NONE = 0, ENC_F_ITEM, ENC_F_CAP, ENC_F_WILD };
static uint8_t  s_film    = ENC_F_NONE;
static uint32_t s_film_t0 = 0;

// UNSIGNED SUBTRACTION, so a film armed 40 ms before the millis() wrap measures
// 40 ms after it and not 4,294,967,256. The same rule ui/actfx.cpp's af_t()
// states; tests/test_screens.cpp drives a film across the wrap.
static uint32_t enc_film_t(void) { return (uint32_t)(ui_now_ms() - s_film_t0); }

static uint32_t enc_film_len(void) {
  if (s_film == ENC_F_ITEM) return ENC_ITEM_END_MS;
  if (s_film == ENC_F_CAP)  return ENC_CAP_END_MS;
  if (s_film == ENC_F_WILD) return ENC_WILD_END_MS;
  return 0u;
}

static void enc_film_begin(uint8_t which) {
  s_film    = which;
  s_film_t0 = ui_now_ms();
}

void enc_film_cancel(void) { s_film = ENC_F_NONE; }

uint8_t enc_film_phase(void) {
  if (s_film == ENC_F_NONE) return (uint8_t)ENC_FILM_NONE;
  const uint32_t t = enc_film_t();
  if (t >= enc_film_len()) return (uint8_t)ENC_FILM_NONE;   // THE CLOCK ENDS IT
  if (s_film == ENC_F_ITEM)
    return (uint8_t)((t < ENC_ITEM_RISE_MS) ? ENC_FILM_ITEM_RISE
                                            : ENC_FILM_ITEM_SETTLE);
  if (s_film == ENC_F_WILD) {
    if (t < ENC_WILD_TEAR_MS) return (uint8_t)ENC_FILM_WILD_TEAR;
    if (t < ENC_WILD_FORM_MS) return (uint8_t)ENC_FILM_WILD_FORM;
    return (uint8_t)ENC_FILM_WILD_STARE;
  }
  if (t < ENC_CAP_CLAMP_MS) return (uint8_t)ENC_FILM_CAP_CLAMP;
  if (t < ENC_CAP_PULL_MS)  return (uint8_t)ENC_FILM_CAP_PULL;
  return (uint8_t)ENC_FILM_CAP_SEAL;
}

// -----------------------------------------------------------------------------
//  THE ONE PIXEL WRITER, ui/actfx.cpp's rule in this screen's coordinates: sets,
//  never clears, and clipped to the content band. Everything below that is not
//  a whole-sprite blit goes through here, so no film can write in the header or
//  under the affordance strip however wrong its arithmetic gets.
// -----------------------------------------------------------------------------
static void enc_px(int16_t x, int16_t y) {
  if (x < 0 || x >= (int16_t)OLED_W)          return;
  if (y < ENC_STAGE_Y0 || y > ENC_STAGE_Y1)   return;
  gfx_pixel(x, y);
}

static void enc_hline(int16_t x, int16_t y, int16_t w) {
  for (int16_t i = 0; i < w; ++i) enc_px((int16_t)(x + i), y);
}

static void enc_vline(int16_t x, int16_t y, int16_t h) {
  for (int16_t i = 0; i < h; ++i) enc_px(x, (int16_t)(y + i));
}

// A dissolving sprite, transparent and clipped, drawn one pixel at a time.
// This is the plan's "reuse the actfx pixel writer" read correctly: the MATHS
// is shared (ae_dissolve_*), the WRITER is this screen's, because actfx's
// clips to petfx's stage - furniture that does not exist here.
static void enc_blit(int16_t x, int16_t y, const SpriteRef& r,
                     uint8_t dir, uint8_t pct) {
  if (!r.bits || r.w == 0u || r.h == 0u) return;
  const int16_t front  = ae_dissolve_front(dir, r.h, pct);
  const uint8_t stride = (uint8_t)((r.w + 7u) >> 3);
  for (uint8_t rr = 0; rr < r.h; ++rr) {
    const uint8_t* row = r.bits + (uint16_t)rr * (uint16_t)stride;
    for (uint8_t cc = 0; cc < r.w; ++cc) {
      if (((row[cc >> 3] >> (cc & 7u)) & 1u) == 0u) continue;
      const int16_t sx = (int16_t)(x + (int16_t)cc);
      const int16_t sy = (int16_t)(y + (int16_t)rr);
      if (ae_dissolve_skip(dir, rr, front, sx, sy)) continue;
      enc_px(sx, sy);
    }
  }
}

// Four short rays around a point - the spark. `reach` is how far out they go.
static void enc_sparks(int16_t cx, int16_t cy, int16_t reach) {
  if (reach <= 0) return;
  for (int16_t i = 1; i <= reach; ++i) {
    enc_px((int16_t)(cx - i), cy);
    enc_px((int16_t)(cx + i), cy);
    enc_px(cx, (int16_t)(cy - i));
    enc_px(cx, (int16_t)(cy + i));
  }
}

// One corner bracket, `d` pixels out from the body box. `sx`/`sy` are -1 or +1.
static void enc_bracket(int16_t d, int8_t sx, int8_t sy) {
  const int16_t x = (sx < 0) ? (int16_t)(ENC_BODY_X - 1 - d)
                             : (int16_t)(ENC_BODY_X + ENC_BODY_W + d);
  const int16_t y = (sy < 0) ? (int16_t)(ENC_BODY_Y - 1 - d)
                             : (int16_t)(ENC_BODY_Y + ENC_BODY_W + d);
  enc_hline((sx < 0) ? x : (int16_t)(x - ENC_CLAMP_ARM + 1), y, ENC_CLAMP_ARM);
  enc_vline(x, (sy < 0) ? y : (int16_t)(y - ENC_CLAMP_ARM + 1), ENC_CLAMP_ARM);
}

// THE ITEM HAS NO ICON OF ITS OWN AND THAT IS A CONTENT FACT, NOT AN OVERSIGHT:
// ItemDef is 8 B with a static_assert on its size (data/items_table.h) and
// carries no art field, so there are ten items and no ten drawings. The CLASS
// is what the player is being told - a sweet, a trap, a repair, a battle chip,
// a key - and the icon atlas already has one of each. An item that ever wants
// its own picture needs an ItemDef field and ITEM_COUNT drawings, which is a
// content decision and not this chunk's.
static uint8_t enc_item_icon(uint8_t item_id) {
  const ItemDef* it = item_get(item_id);
  if (it == nullptr) return (uint8_t)ICO_GEAR;
  switch ((ItemKlass)it->klass) {
    case ITEM_KLASS_XP_CANDY:   return (uint8_t)ICO_SNACK;
    case ITEM_KLASS_CAPTURE:    return (uint8_t)ICO_BALL;
    case ITEM_KLASS_CARE:       return (uint8_t)ICO_MED;
    case ITEM_KLASS_EVOLUTION:  return (uint8_t)ICO_DNA;
    default:                    return (uint8_t)ICO_GEAR;   // BATTLE_MOD
  }
}

// -----------------------------------------------------------------------------
//  THE ITEM PICKUP FILM. The drop climbs out of its own name line, arcs, and
//  hangs sparking above it.
//
//  IT DRAWS THE ICON THROUGH gfx_xbm_t() AND THAT IS LOAD-BEARING. The icon
//  crosses the item's name on the way up. The OPAQUE blit - which is what
//  gfx_xbm() is and what the device has always done - would punch a 12x12 hole
//  through the label on every frame of the climb. Until P10-C3 the host fake
//  drew both transparently, so this defect would have shipped with a green
//  golden that showed the picture the panel does not draw. See ui/gfx.h.
// -----------------------------------------------------------------------------
static void draw_item_film(void) {
  const uint32_t t = enc_film_t();
  const SpriteRef r = sprite_icon(enc_item_icon(s_enc.item_id));

  // The climb: a straight lerp with a parabola laid over it, so it leaves fast
  // and arrives slowly instead of sliding at one speed.
  int16_t y = ae_lerp(t, ENC_ITEM_RISE_MS, (int16_t)ENC_ICON_BOT, (int16_t)ENC_ICON_TOP);
  y = (int16_t)(y + ae_hop(t, ENC_ITEM_RISE_MS, 4));
  if (t >= ENC_ITEM_RISE_MS) {
    // The hang: a one-pixel bob so the drop is alive rather than parked.
    y = (int16_t)((int16_t)ENC_ICON_TOP +
                  ae_hop((uint32_t)(t - ENC_ITEM_RISE_MS),
                         (uint32_t)(ENC_ITEM_END_MS - ENC_ITEM_RISE_MS), 2));
  }
  gfx_xbm_t(ENC_ICON_X, y, r.w, r.h, r.bits);

  // The spark, only over the second half, growing and then shrinking with the
  // same lunge shape a bite uses - out, hold, back.
  const uint8_t pct = ae_pct(t, ENC_ITEM_RISE_MS / 2u, ENC_ITEM_END_MS);
  const int16_t reach = ae_lunge(pct, 100u, 5u);
  enc_sparks((int16_t)(ENC_ICON_X + ENC_ICON_W / 2),
             (int16_t)(y + ENC_ICON_W / 2 - 8), reach);
}

// THE WILD BODY, at one animation frame. STAGE_BABY and POSE_IDLE for the same
// reason ui/battle_renderer.cpp passes them: a wild Pebble on this screen has no
// stage of its own to show, and every body in the atlas is 24x24 since P9-C3.
// Shared by the capture film and the wild reveal so the creature that assembles
// is byte-for-byte the creature that dissolves.
static SpriteRef enc_wild_sprite(uint8_t frame) {
  const uint8_t key = pet_art_key(s_enc.species_id, 0u);
  const uint8_t set = sprite_set_id((uint8_t)STAGE_BABY,
                                    sprite_form_of(key, STAGE_BABY),
                                    (uint8_t)POSE_IDLE);
  return sprite_frame(set, frame);
}

// -----------------------------------------------------------------------------
//  THE CAPTURE SUCCESS FILM. Four brackets close on the wild creature, the
//  creature dissolves upward between them, and the brackets collapse onto a
//  sealed marker.
//
//  THE DISSOLVE IS ui/actfx.cpp's, not a second one: ae_dissolve_front() and
//  ae_dissolve_skip() are the two lines af_blit() open-coded, moved into
//  ui/anim_ease.cpp at this chunk so both films and all seven of actfx's share
//  one frontier. A creature that is being caught FADES BY LOSING PIXELS - it is
//  never overprinted and never erased, because this panel is 1-bit and a
//  colour-0 overprint would take the floor with it.
// -----------------------------------------------------------------------------
static void draw_capture_film(uint8_t frame) {
  const uint32_t t = enc_film_t();
  const uint8_t  phase = enc_film_phase();

  // Where the brackets are, in pixels out from the body box.
  int16_t d = ENC_CLAMP_NEAR;
  if (phase == (uint8_t)ENC_FILM_CAP_CLAMP)
    d = ae_lerp(t, ENC_CAP_CLAMP_MS, (int16_t)ENC_CLAMP_FAR, (int16_t)ENC_CLAMP_NEAR);

  if (phase != (uint8_t)ENC_FILM_CAP_SEAL) {
    // The creature, solid while the brackets close and dissolving while they
    // hold.
    const SpriteRef r  = enc_wild_sprite(frame);
    const uint8_t pct  = (phase == (uint8_t)ENC_FILM_CAP_PULL)
                           ? ae_pct(t, ENC_CAP_CLAMP_MS, ENC_CAP_PULL_MS) : 0u;
    // DOWNWARD, AND THE FIRST DRAFT HAD IT THE OTHER WAY. AE_DIS_UP eats a
    // sprite from its bottom row upwards, and every body in the atlas is drawn
    // standing on the bottom of its 24x24 box with an empty margin above it -
    // so an upward dissolve took the whole creature away in the first fifth of
    // the beat and left three hundred milliseconds of four brackets around
    // nothing. It was recorded, looked at, and changed. Eating downward takes
    // the head first and leaves the silhouette readable for most of the pull,
    // which is the same argument ui/battle_renderer.cpp makes for dissolving a
    // fainted body rather than deleting it.
    enc_blit(ENC_BODY_X, (int16_t)ENC_BODY_Y, r,
             (uint8_t)(pct ? AE_DIS_DOWN : AE_DIS_NONE), pct);
  }

  enc_bracket(d, -1, -1);
  enc_bracket(d, +1, -1);
  enc_bracket(d, -1, +1);
  enc_bracket(d, +1, +1);

  if (phase == (uint8_t)ENC_FILM_CAP_SEAL) {
    // The seal: a marker at the centre with a ring pulsing out of it, so the
    // beat ends on something arriving rather than on something gone.
    const int16_t cx = (int16_t)(ENC_BODY_X + ENC_BODY_W / 2);
    const int16_t cy = (int16_t)(ENC_BODY_Y + ENC_BODY_W / 2);
    const uint8_t pct = ae_pct(t, ENC_CAP_PULL_MS, ENC_CAP_END_MS);
    const int16_t rad = ae_lerp(pct, 100u, 1, 9);
    enc_hline((int16_t)(cx - rad), (int16_t)(cy - rad), (int16_t)(rad * 2 + 1));
    enc_hline((int16_t)(cx - rad), (int16_t)(cy + rad), (int16_t)(rad * 2 + 1));
    enc_vline((int16_t)(cx - rad), (int16_t)(cy - rad), (int16_t)(rad * 2 + 1));
    enc_vline((int16_t)(cx + rad), (int16_t)(cy - rad), (int16_t)(rad * 2 + 1));
    enc_sparks(cx, cy, 3);
  }
}

// The two-frame idle phase, the same one HOME and the battle field breathe on.
static uint8_t enc_body_frame(void) {
  return (uint8_t)((ui_now_ms() / UI_ANIM_FRAME_MS) & 1u);
}

// -----------------------------------------------------------------------------
//  THE WILD REVEAL. See screen_encounter.h for why this film exists at all.
//
//  THREE BEATS AND A CUT:
//    TEAR   six scanlines jump around the band. No body. This is the panel
//           failing, which is what a wild Pebble IS in this fiction - something
//           that got into memory that should not be there.
//    FORM   the bars thin out while the body assembles feet-first out of them,
//           one pixel of horizontal tear on the same step clock, so the body
//           and the corruption are visibly the same event and not two effects
//           that happen to overlap.
//    STARE  the body stands still. On the last ENC_WILD_END_MS - ENC_WILD_SNAP_MS
//           the whole band goes inverse, and THAT IS THE POINT OF THE BEAT: the
//           film ends on a hard cut to a two-option menu that shares not one
//           pixel with it, and a full-band flash is what a cut hides behind.
//           Without it the creature simply vanishes and the menu appears, which
//           reads as a dropped frame rather than as an answer.
//
//  NO RNG. Every bar comes out of enc_noise(), a pure function of the step
//  number, because a film that drew from a real generator would record a
//  different golden every time - and a golden nobody can reproduce is a
//  picture, not a test.
// -----------------------------------------------------------------------------

// A deterministic 8-bit scramble. NOT a random number generator, and it is
// never asked to be one: it is asked to make six numbers that do not look
// related, from a step counter, identically on every machine.
static uint8_t enc_noise(uint8_t a, uint8_t b) {
  uint8_t h = (uint8_t)((uint8_t)(a * 37u) + (uint8_t)(b * 97u) + 0x5Au);
  h = (uint8_t)(h ^ (uint8_t)(h >> 3));
  return (uint8_t)((uint8_t)(h * 5u) + 1u);
}

#define ENC_BAND_H  ((int16_t)(ENC_STAGE_Y1 - ENC_STAGE_Y0 + 1))

// `bars` scanlines, placed by the step clock. enc_hline() clips, so the widths
// below cannot reach off the panel however the scramble comes out.
static void enc_tear(uint32_t t, uint8_t bars) {
  const uint8_t step = (uint8_t)((t / ENC_WILD_STEP_MS) & 0xFFu);
  for (uint8_t i = 0; i < bars; ++i) {
    const int16_t y = (int16_t)(ENC_STAGE_Y0 +
                                (int16_t)(enc_noise(step, i) % (uint8_t)ENC_BAND_H));
    const int16_t x = (int16_t)(enc_noise(step, (uint8_t)(i + 64u)) % 80u);
    const int16_t w = (int16_t)(12u + (enc_noise(step, (uint8_t)(i + 128u)) % 44u));
    enc_hline(x, y, w);
  }
}

static void draw_wild_film(void) {
  const uint32_t t  = enc_film_t();
  const uint8_t  ph = enc_film_phase();

  // How much corruption is left: all of it through the tear, none of it once
  // the body is whole.
  uint8_t bars = 0u;
  if (ph == (uint8_t)ENC_FILM_WILD_TEAR) {
    bars = (uint8_t)ENC_WILD_BARS;
  } else if (ph == (uint8_t)ENC_FILM_WILD_FORM) {
    const uint8_t gone = ae_pct(t, ENC_WILD_TEAR_MS, ENC_WILD_FORM_MS);
    bars = (uint8_t)((uint16_t)ENC_WILD_BARS * (uint16_t)(100u - gone) / 100u);
  }
  enc_tear(t, bars);

  if (ph == (uint8_t)ENC_FILM_WILD_TEAR) return;   // nothing has arrived yet

  // THE BODY, RUN BACKWARDS. AE_DIS_DOWN eats a sprite from its top row down,
  // so a percentage that falls from 100 to 0 puts it back on from the bottom
  // up - feet, then torso, then head, which is the direction something climbing
  // out of the floor arrives in. (The upward direction was tried first and is
  // wrong here for the same reason the capture film gives for preferring DOWN:
  // every body stands on the bottom of its 24x24 box with empty margin above,
  // so an upward frontier spends most of its travel over nothing.)
  const uint8_t pct = (ph == (uint8_t)ENC_FILM_WILD_FORM)
                        ? (uint8_t)(100u - ae_pct(t, ENC_WILD_TEAR_MS, ENC_WILD_FORM_MS))
                        : 0u;
  // One pixel of horizontal tear while it is still assembling, stepped on the
  // bars' clock rather than on the frame clock.
  const int16_t dx = (ph == (uint8_t)ENC_FILM_WILD_FORM)
                       ? (int16_t)((int16_t)(enc_noise((uint8_t)((t / ENC_WILD_STEP_MS) & 0xFFu),
                                                       200u) % 3u) - 1)
                       : (int16_t)0;
  enc_blit((int16_t)(ENC_BODY_X + dx), (int16_t)ENC_BODY_Y, enc_wild_sprite(enc_body_frame()),
           (uint8_t)(pct ? AE_DIS_DOWN : AE_DIS_NONE), pct);

  // THE SNAP. gfx_invert_rect() and not a fill: the body has to survive it, and
  // on a 1-bit panel the only way to flash something without deleting it is to
  // swap the ink for the paper.
  if (t >= ENC_WILD_SNAP_MS)
    gfx_invert_rect(0, ENC_STAGE_Y0, (int16_t)OLED_W, ENC_BAND_H);
}


void encounter_arm(const EncounterResult& r, uint8_t category)
{
  s_enc     = r;
  s_cat     = category;
  s_cursor  = 0;
  s_applied = 0;
  s_reward  = 0;
  cap_reset(s_cap);
}

static uint8_t active_level(void)
{
  const PebbleInstance* p = ui_active_pebble();
  return (p != nullptr && p->level >= 1u) ? p->level : 1u;
}

// The best capture item in the bag: the highest-value CAPTURE row held. The
// player never picks one, which is a deliberate simplification for a transient
// - and it is stated here rather than left as a hole where a picker should be.
static uint8_t best_capture_item(void)
{
  const Inventory& inv = ui_inventory();
  uint8_t best = 0, best_v = 0;
  for (uint8_t i = 1; i <= ITEM_COUNT; ++i) {
    const ItemDef* it = item_get(i);
    if (it == nullptr || it->klass != (uint8_t)ITEM_KLASS_CAPTURE) continue;
    if (inv_count(inv, i) == 0u) continue;
    if (it->value >= best_v) { best_v = it->value; best = i; }
  }
  return best;
}

// -----------------------------------------------------------------------------
//  THE ENCOUNTER TRANSIENT
// -----------------------------------------------------------------------------
void encounter_enter(void)
{
  s_cursor = 0;
  if (s_applied) return;              // re-entering from CAPTURE, already paid

  if (s_enc.outcome == (uint8_t)ENC_OUT_WILD) {
    // s_applied is doing double duty here and the flag's comment says so: for
    // ITEM and SPECIAL it means "the reward has been paid", and for WILD it
    // means "the reveal has played". Both are the same question - has this
    // encounter's one-shot entry work already run - and answering it twice with
    // two bytes is how the two answers come to disagree.
    s_applied = 1u;
    enc_film_begin(ENC_F_WILD);
    audio_play(SFX_GLITCH);           // corruption, which is what this is
    return;
  }

  uint32_t now_epoch = 0, now_ms = 0;
  uint8_t  cal = 0;
  ui_explore_clock(&now_epoch, &now_ms, &cal);

  if (s_enc.outcome == (uint8_t)ENC_OUT_ITEM) {
    // The drop lands in the bag now, not when the player presses something: a
    // reward that a stray BACK can lose is a reward the player will not trust.
    const uint8_t added = inv_add(ui_inventory(), s_enc.item_id, 1u);
    if (added == 0u) ui_toast(STR_ENC_BAG_FULL);
    ui_explore_commit((uint8_t)BOX_SLOT_NONE);   // the bag moved, no slot did
    s_applied = 1u;
    // THE FILM IS ARMED ONLY ON A DROP THAT LANDED. A full bag already raises a
    // toast and adds nothing; playing the pickup over it would be the screen
    // celebrating a reward the player did not get.
    if (added != 0u) enc_film_begin(ENC_F_ITEM);
    return;
  }

  if (s_enc.outcome == (uint8_t)ENC_OUT_SPECIAL) {
    if (s_enc.event_kind == (uint8_t)SPEV_XP_BURST) {
      // THE WITHHOLDING LIVES IN game/encounters.cpp, not here: a screen that
      // decided when an anti-farm rule applied would be a rule with no test.
      s_reward = encounter_special_xp(s_enc, cal);
      if (s_reward > 0u) ui_award_xp(s_reward, (uint8_t)XP_SRC_SPECIAL);
      else               ui_toast(STR_ENC_NO_CLOCK);
    } else if (s_enc.event_kind == (uint8_t)SPEV_CORRUPTION) {
      PebbleInstance* p = ui_active_pebble();
      // cor_apply() refuses on an untrustworthy clock, because a 24 h status
      // has nowhere to live without one. The event still happened; it just did
      // not stick, and the player is told rather than left guessing.
      if (p == nullptr || !cor_apply(*p, now_epoch, cal)) ui_toast(STR_ENC_NO_CLOCK);
    }
    ui_explore_commit((uint8_t)BOX_SLOT_NONE);   // corruption is on the ACTIVE Pebble
    s_applied = 1u;
  }
}

void encounter_leave(void) { s_cursor = 0; enc_film_cancel(); }

void encounter_input(Gesture g)
{
  // ANY GESTURE SKIPS THE FILM. A player who has pressed something has stopped
  // watching, and a screen that made them sit through an animation before it
  // would answer is exactly the "no time-critical menus" rule of spec section
  // 65 read backwards. The cancel is a courtesy, not the bound: see the
  // contract in screen_encounter.h.
  enc_film_cancel();
  if (g == (Gesture)GST_BOTH) { ui_help(STR_ENC_HELP); return; }
  if (s_enc.outcome != (uint8_t)ENC_OUT_WILD) return;   // nothing to steer
  if (g == (Gesture)GST_TAP_L) {
    s_cursor = (uint8_t)(s_cursor ^ 1u);
    audio_play(SFX_TICK);      // the rule is in ui/screen_menu.cpp
    ui_note_input();
    return;
  }
  if (g == (Gesture)GST_HOLD_L) {
    if (s_cursor == 0u) ui_push(SCR_CAPTURE);   // CAPTURAR
    else                ui_back();              // DEJAR - a real answer
  }
}

static void wild_row(char* out, size_t cap)
{
  const char* name = pet_species_name(s_enc.species_id);
  snprintf(out, cap, "%s Nv%u", name ? name : "?", (unsigned)s_enc.level);
}

void encounter_render(void)
{
  gfx_header(S(STR_ENC_TITLE), nullptr);
  char row[ENC_ROW_CAP];

  switch ((EncounterOutcome)s_enc.outcome) {
    case ENC_OUT_WILD: {
      if (enc_film_phase() != (uint8_t)ENC_FILM_NONE) {
        // THE REVEAL OWNS THE BAND, unlike the item film which overlays its own
        // label. The two options are a QUESTION, and asking it over a picture
        // that has not finished arriving is asking it before the player knows
        // what they are answering about. The affordance strip is still drawn,
        // because any press does answer it - see encounter_input().
        draw_wild_film();
        gfx_affordance(S(STR_AF_SEL), S(STR_AF_BACK));
        break;
      }
      gfx_text_center(GF_NARR, 22, S(STR_ENC_WILD));
      wild_row(row, sizeof row);
      gfx_text_center(GF_BODY, 33, row);
      // The two options section 23 draws, with the cursor inverted rather than
      // marked, which is the BOX screen's own idiom.
      const char* opt[2] = { S(STR_ENC_CATCH), S(STR_ENC_LEAVE) };
      for (uint8_t i = 0; i < 2u; ++i) {
        const int16_t x = (int16_t)(6 + i * 62);
        gfx_text(GF_BODY, (int16_t)(x + 2), 46, opt[i]);
        if (i == s_cursor) gfx_invert_rect(x, 38, 60, 11);
      }
      gfx_affordance(S(STR_AF_SEL), S(STR_AF_BACK));
      break;
    }
    case ENC_OUT_ITEM: {
      const ItemDef* it = item_get(s_enc.item_id);
      gfx_text_center(GF_NARR, 24, S(STR_ENC_ITEM));
      gfx_text_center(GF_BODY, 38, it ? S(it->name_idx) : "?");
      // THE FILM IS AN OVERLAY AND IT GOES LAST. The words are what the player
      // needs and the picture is what makes the moment; drawing the icon first
      // would put the label on top of it.
      if (enc_film_phase() != (uint8_t)ENC_FILM_NONE) draw_item_film();
      gfx_affordance(nullptr, S(STR_AF_BACK));
      break;
    }
    case ENC_OUT_SPECIAL: {
      const SpecialEvent* ev = encounter_event_of(s_enc);
      gfx_text_center(GF_NARR, 22, S(STR_ENC_SPECIAL));
      gfx_text_center(GF_BODY, 34, ev ? S(ev->name_idx) : "?");
      if (s_reward > 0u) {
        snprintf(row, sizeof row, "+%u XP", (unsigned)s_reward);
        gfx_text_center(GF_BODY, 46, row);
      } else if (s_enc.event_kind == (uint8_t)SPEV_CORRUPTION) {
        gfx_text_center(GF_BODY, 46, S(STR_ENC_CORRUPT));
      }
      gfx_affordance(nullptr, S(STR_AF_BACK));
      break;
    }
    default:
      // NOTHING never reaches this screen (screen_network.cpp answers it with a
      // toast), so this arm is the one that must not silently draw a blank.
      gfx_text_center(GF_BODY, 32, S(STR_NET_NOTHING));
      gfx_affordance(nullptr, S(STR_AF_BACK));
      break;
  }
  gfx_countdown(ui_idle_ms());
}

// -----------------------------------------------------------------------------
//  THE CAPTURE ATTEMPT
// -----------------------------------------------------------------------------
void capture_enter(void)
{
  enc_film_cancel();
  s_mode = (uint8_t)CSM_READY;
  s_out  = (uint8_t)CAP_ESCAPED;
  s_item = best_capture_item();
}

void capture_leave(void) { s_mode = (uint8_t)CSM_READY; enc_film_cancel(); }

static void throw_once(void)
{
  uint32_t now_epoch = 0, now_ms = 0;
  uint8_t  cal = 0;
  ui_explore_clock(&now_epoch, &now_ms, &cal);

  CaptureReport rep;
  const Genome g = ui_fresh_genome();
  const bool caught = cap_attempt(s_cap, s_enc, active_level(), s_item,
                                  ui_explore_roll(), g,
                                  ui_device_seed() ^ now_ms, now_epoch, rep);
  s_out  = rep.outcome;
  s_mode = (uint8_t)CSM_RESULT;

  // THE ITEM IS SPENT ONLY ON A ROLL THAT HAPPENED. CAP_BOX_FULL, CAP_FLED and
  // CAP_BAD_GENOME never reached the roll, so they never cost a chip - which is
  // the whole reason game/capture.cpp checks the Box BEFORE rolling.
  const bool rolled = (rep.outcome == (uint8_t)CAP_CAUGHT ||
                       rep.outcome == (uint8_t)CAP_ESCAPED ||
                       rep.outcome == (uint8_t)CAP_FLED);
  if (rolled && s_item != 0u) {
    (void)inv_remove(ui_inventory(), s_item, 1u);
    s_item = best_capture_item();
  }
  if (caught) {
    ui_award_xp(XP_CAPTURE, (uint8_t)XP_SRC_CAPTURE);
    enc_film_begin(ENC_F_CAP);             // only a catch gets the film
  }
  // THE SLOT THE CATCH LANDED IN, WHICH IS NOT THE ACTIVE ONE UNLESS THE BOX WAS
  // EMPTY. cap_attempt() files through box_new_pebble() -> first_free(), and
  // CaptureReport.slot has carried the answer since P5-C3 - nothing read it.
  // Without this the creature lived in RAM until the next brown-out while the
  // Box HEADER, written by the same commit, claimed the slot was occupied.
  ui_explore_commit(caught ? rep.slot : (uint8_t)BOX_SLOT_NONE);
}

void capture_input(Gesture g)
{
  enc_film_cancel();                       // see encounter_input()
  if (g == (Gesture)GST_BOTH) { ui_help(STR_ENC_HELP); return; }
  if (g == (Gesture)GST_TAP_R) { ui_back(); return; }

  if (g != (Gesture)GST_HOLD_L) return;
  if (s_mode == (uint8_t)CSM_READY) { throw_once(); return; }

  // From a resolved attempt: A throws again while attempts remain, and
  // otherwise leaves. A full Box sends the player where the decision is - spec
  // section 23's "the player must decide whether to release/replace", not a
  // dead end.
  if (s_out == (uint8_t)CAP_BOX_FULL) { ui_push(SCR_BOX); return; }
  if (s_out == (uint8_t)CAP_ESCAPED)  { s_mode = (uint8_t)CSM_READY; return; }
  ui_back();
}

void capture_render(void)
{
  gfx_header(S(STR_CAP_TITLE), nullptr);

  // THE CAPTURE FILM OWNS THE WHOLE CONTENT BAND, and that is arithmetic rather
  // than taste. The band is rows 11..55; a 24 px body plus a bracket that opens
  // ENC_CLAMP_FAR pixels clear of it on both sides needs 24 + 2*(1 + FAR) rows,
  // which at FAR = 8 is 42 of the 45 there are. The first draft kept the
  // species line at baseline 21 and the brackets drew straight through it -
  // recorded, looked at, and changed. The name is on screen for the whole
  // encounter before the throw and comes back with the result line one second
  // later; what it is not is legible under a bracket.
  if (enc_film_phase() != (uint8_t)ENC_FILM_NONE) {
    draw_capture_film(enc_body_frame());
    gfx_affordance(nullptr, S(STR_AF_BACK));
    gfx_countdown(ui_idle_ms());
    return;
  }

  char row[ENC_ROW_CAP];
  wild_row(row, sizeof row);
  gfx_text_center(GF_BODY, 21, row);

  if (s_mode == (uint8_t)CSM_READY) {
    const uint16_t p = cap_chance_permille(s_enc.species_id, s_enc.level,
                                           active_level(), s_item);
    snprintf(row, sizeof row, "%u%%", (unsigned)((p + 5u) / 10u));
    gfx_text_center(GF_NARR, 35, row);
    const ItemDef* it = item_get(s_item);
    gfx_text_center(GF_BODY, 46, it ? S(it->name_idx) : S(STR_CAP_NO_ITEM));
    gfx_affordance(S(STR_CAP_THROW), S(STR_AF_BACK));
    gfx_countdown(ui_idle_ms());
    return;
  }

  uint16_t msg = STR_CAP_ESCAPED;
  switch ((CaptureOutcome)s_out) {
    case CAP_CAUGHT:   msg = STR_CAP_CAUGHT;   break;
    case CAP_FLED:     msg = STR_CAP_FLED;     break;
    case CAP_BOX_FULL: msg = STR_CAP_BOX_FULL; break;
    // CAP_BAD_GENOME and CAP_INTERNAL are BUGS IN THIS TREE, not player
    // outcomes (game/capture.h), and they say so rather than borrowing the word
    // for an ordinary miss.
    case CAP_BAD_GENOME:
    case CAP_INTERNAL:
    case CAP_NO_ENCOUNTER: msg = STR_CAP_ERROR; break;
    default: break;
  }
  gfx_text_wrap(GF_BODY, 4, 33, OLED_W - 8, GFX_LINE_BODY, 2, S(msg));

  const bool again = (s_out == (uint8_t)CAP_ESCAPED);
  const bool tobox = (s_out == (uint8_t)CAP_BOX_FULL);
  gfx_affordance(again ? S(STR_CAP_THROW) : (tobox ? S(STR_CAP_TO_BOX) : nullptr),
                 S(STR_AF_BACK));
  gfx_countdown(ui_idle_ms());
}
