// =============================================================================
//  PEBBLEBOL - ui/actfx.cpp
//  Eight choreographies, one clock, one pixel writer. See actfx.h for the seam.
//
//  THE DESIGN DECISION THE WHOLE FILE HANGS OFF: THE PROP COMES TO THE PET.
//  The bowl slides in along the floor from the nearest edge of the stage and
//  parks flush against the side of the body's INK; the pet turns its head to
//  look at it. It is not the other way round. Walking the pet to the food would
//  mean teaching petfx's automaton a "go to x and stop there" state, and that
//  automaton is the one piece of this firmware where a bug is a pet that walks
//  into a wall forever - while the moment the player actually sees (animal,
//  food, animal eats food) is identical either way. The prop goes on whichever
//  side of the pet it FITS on, so the choice never pushes it into the HUD.
//
//  WHAT MAY NOT HAPPEN, EVER, AND HOW THAT IS ENFORCED
//   1. Nothing here may take ink off the body. Everything goes through af_px(),
//      which calls drawPixel() in colour 1 and nothing else - no drawBox, no
//      drawXBM (which paints the zeros too in setBitmapMode(0)), no colour-0
//      dither. Fading is done by drawing FEWER pixels, never by erasing them.
//      A dissolve that erased would be harmless in the ground layer and would
//      eat the animal in the emote layer, and nothing at the call site tells
//      you which layer you are in - so neither layer is allowed to erase.
//   2. Nothing here may write in the HUD columns 0..UI_HUD_L_END-1 or
//      UI_HUD_R_BEGIN..OLED_W-1, nor above the sprite band, nor into the
//      affordance strip. af_px() clips to the stage and the band. One writer,
//      no exceptions to audit - except the one below, which is spelled out.
//   3. THE ONE EXCEPTION: ACT_CLEAN'S POOP LAYER. It re-draws the poops the
//      model has already thrown away, so they can dissolve instead of blinking
//      out; it sweeps bubbles along them; and it leaves a spark where each one
//      stood. None of those are new props - they are the pixels draw_poop() was
//      putting on the panel one frame earlier, at the same kPoopX[] origins, two
//      of which (2 and 114) have always been in the HUD columns, and the two
//      things that visit them. All three go through af_px_panel(), which clips
//      to the PANEL and the band but not to the stage, and all three live in
//      rows 43..55. Both 12x12 badges are drawn at rows 10..21 and are still
//      stamped opaquely after this layer, so they cannot even be reached, let
//      alone damaged. Clipping this layer to the stage instead is what the code
//      used to do and it cost the feature its meaning: the two outer poops
//      popped out of existence the moment the player pressed clean, the bubbles
//      appeared from nowhere in column 14, and the sparks were dragged inland to
//      columns the mess had never been in - two of them onto each other.
//
//  THE RULE THAT DECIDES WHERE EVERY MOVING PROP GOES: IN THIS BAND THERE IS NO
//  ROOM ABOVE. The sprite area is 47 rows (SPRITE_AREA_Y 9 to RD_AFFORD_Y-1, 55)
//  and an adult body is 40 of them, standing on the floor line. That leaves
//  three to nine rows over its head, which is less than the height of the 10 and
//  12 px emotes this file draws. Every "it rises / it falls / it hovers above"
//  written against that space measures out as a clamp: a pill interpolated from
//  row 9 to `s_iy0 - pill.h` travelled from 9 to 9 and rendered IDENTICAL PIXELS
//  at +0, +150 and +300 ms; the heart of a finished meal stuck after 3 px; the
//  note of a finished game never moved at all. ui.cpp had already found and
//  documented exactly this for the stroking hearts, and it was rediscovered here
//  from scratch.
//
//  So: ANY VERTICAL TRAVEL OF A PROP HAPPENS IN THE FREE COLUMNS BESIDE THE
//  BODY, never over it. The stage is 100 columns and the widest body is 40, so
//  the roomier flank always has at least 30 columns of clear air - which is why
//  the pill falls down the side of the animal (40 rows of travel, all of them
//  real), and why the heart and the note leave the SIDE of the head and drift
//  diagonally up and out instead of straight up into a clamp.
// =============================================================================
#include "actfx.h"

#include "anim_ease.h"

#include "../data/sprites.h"
#include "render.h"
#include "petfx.h"

// =============================================================================
//  1. THE SCRIPTS
//
//  Every number below is a millisecond offset from the start of its own
//  choreography, and every one is a cumulative BOUNDARY, not a duration:
//  AF_MEAL_LEAN_MS is when the lean ENDS and the first bite begins. Written
//  that way because the drawing code asks "where am I now", and a table of
//  durations makes every one of those questions a running sum.
// =============================================================================

// ---- C1 ACT_FEED_MEAL, 2600 ms ---------------------------------------------
#define AF_MEAL_IN_MS        400u   // the bowl has arrived and the pet is looking
#define AF_MEAL_LEAN_MS      600u   // it has leaned over it
#define AF_MEAL_EAT_MS      1900u   // AF_MEAL_BITES bites happened in here
#define AF_MEAL_EMPTY_MS    2100u   // the bowl has lost its steam
#define AF_MEAL_OUT_MS      2400u   // and has slid back out the way it came
#define AF_MEAL_MS          2600u   // hop + a heart
#define AF_MEAL_BITES          4u

// ---- C2 ACT_FEED_SNACK, 1800 ms --------------------------------------------
// The same film, shorter, two bites, a bigger hop at the end. A treat is not a
// meal and must not read like one.
#define AF_SNK_IN_MS         300u
#define AF_SNK_LEAN_MS       450u
#define AF_SNK_EAT_MS       1300u
#define AF_SNK_EMPTY_MS     1450u
#define AF_SNK_OUT_MS       1650u
#define AF_SNK_MS           1800u
#define AF_SNK_BITES           2u

// ---- C3 ACT_CLEAN, 2000 ms -------------------------------------------------
#define AF_CLN_SWEEP_MS     1400u   // the bubbles cross the panel
#define AF_CLN_MS           2000u   // sparks where each poop was + a hop
#define AF_CLN_DISSOLVE_MS   300u   // per poop, from the moment they are reached

// ---- C4 ACT_MEDICINE, 2200 ms ----------------------------------------------
#define AF_MED_FALL_MS       500u   // the pill reaches the top of the head
#define AF_MED_GULP_MS       700u   // startled; the pill is gone
#define AF_MED_GERM_MS      1800u   // the germ has finished fading
#define AF_MED_MS           2200u

// ---- C5 ACT_PET, 1400 ms ---------------------------------------------------
// ui.cpp still owns the three rising hearts (s_mimo_ms); this only adds the
// look-at-the-player and the pleased little hop at the end.
#define AF_PET_HOP_MS       1150u
#define AF_PET_MS           1400u

// ---- C6 was ACT_LIGHT_TOGGLE. P3-C2b deleted the light mechanic; the number
//      is left as a gap so C7 and C8 below still mean what the notes say.

// ---- C7 ACT_SLEEP_TOGGLE ---------------------------------------------------
#define AF_YAWN_MS           900u   // falling asleep: one slow, big stretch
#define AF_WAKE_MS           700u   // waking: two quick ones
#define AF_WAKE_HOP_MS       250u

// ---- C8 ACT_PLAY, 2000 ms --------------------------------------------------
#define AF_PLAY_BALL_MS     1700u
#define AF_PLAY_MS          2000u
#define AF_PLAY_BOUNCES        3u

// Crumbs: 2-3 pixels per bite, falling past the prop to the floor.
#define AF_CRUMB_MS          260u
#define AF_CRUMB_N             3u
#define AF_CRUMB_RISE          4u   // rows above the prop they start from

// ---- THE BITE ---------------------------------------------------------------
// How far the whole animal drives at its dinner, in pixels, on every bite.
//
// The bite used to be a vertical alternation between dy = 2 and dy = 1. It was
// invisible, and not as a matter of taste: ONE pixel of movement, of which the
// bottom edge showed none at all because petfx_draw_body() clamps the body's
// ink to PETFX_FLOOR_Y - 1, added on top of an idle bob that is itself +/-1 px
// and never stops. The signal was smaller than the noise it was summed with.
//
// So the bite is horizontal, which is the one axis with room (see the rule in
// the file header), and it moves the WHOLE silhouette. Three pixels is the
// smallest displacement of a 40 px body that is unmistakable at 20 fps, and it
// is exactly one pixel short of AF_PROP_GAP: at full extension the animal is
// still separated from the bowl by a single background column, so it lunges AT
// its dinner without ever merging with it.
#define AF_BITE_PX             3

// ---- HOW FAR A PARKED PROP STANDS FROM THE BODY, IN BACKGROUND COLUMNS ------
// One column was not enough, and that is a measurement rather than an opinion.
// On a 1-bit panel two solid silhouettes with a single dark column between them
// read as one silhouette: parked at a gap of one, the prop's outline TOUCHED
// the body's outright on 19 % of a meal's frames and 11 % of a snack's. The
// question the whole feature was built to answer - "does that look like a bowl
// beside the animal, or like something growing out of its belly?" - had the
// wrong answer for a fifth of the film.
//
// Four columns is where two shapes separate at this pixel pitch, and it is
// affordable: the stage is 100 columns, the widest body 40, so the roomier
// flank always has at least 30 free columns against the 18 a bowl and its gap
// need. AF_PROP_GAP_MIN is the floor for a case today's art cannot produce but
// which would otherwise become a silent regression if the art changed.
#define AF_PROP_GAP            4
#define AF_PROP_GAP_MIN        2

// ---- KEEPING THE FILM AT FULL FRAME RATE ------------------------------------
// Renewed on every actfx_service() tick for as long as a film is running. See
// rd_hold_fps(): rd_fps() caps the rate at FPS_LOW for RENDER_WEB_BUSY_MS after
// every request the web server serves, the phone's page polls /state every
// 700 ms, and the very request that starts a web-launched action arms that cap
// itself - so without this a 2600 ms meal played as ten frames.
//
// Comfortably longer than one FPS_LOW frame period (250 ms) so the hold cannot
// lapse between two ticks of a slow loop(), and short enough that it is gone
// almost as soon as the film is. It self-releases either way.
#define AF_FPS_HOLD_MS       400u

// =============================================================================
//  2. STATE
// =============================================================================
static uint8_t  s_act      = ACT_NONE;
static uint32_t s_t0       = 0;
static uint32_t s_now      = 0;
static uint16_t s_dur      = 0;
static uint8_t  s_held     = 0;      // we are the ones holding petfx

// What actfx_begin() kept out of the "before" pet.
static uint8_t  s_poop_n   = 0;
static uint8_t  s_asleep0  = 0;      // it was ASLEEP before ACT_SLEEP_TOGGLE

// One-shot latch, so a beat that must happen exactly once (the medicine
// flinch) cannot fire again on the next frame.
static uint8_t  s_fired    = 0;

// The poop layout ui.cpp lends us.
static const int16_t* s_poop_xs = nullptr;
static uint8_t        s_poop_ns = 0;

// The body, sampled in actfx_draw_over() from what petfx_draw_body() JUST drew,
// so it is never a frame stale and never a guess. s_geo goes to 1 on the first
// sample; until then actfx_draw_props() draws nothing, which costs one frame
// (50 ms at FPS_NORMAL) and buys the guarantee that a prop is never parked
// against a body box left over from another screen.
static uint8_t  s_geo      = 0;
static int16_t  s_ix0 = 44, s_iy0 = 12, s_ix1 = 83, s_iy1 = 51;   // ink, inclusive

// Decided ONCE, at the first sample, then frozen: which side the prop comes
// from and where it parks. Re-deriving the park every frame would make the bowl
// jitter a pixel sideways every UI_ANIM_FRAME_MS, because 24 of the 38 sprite
// sets have different ink bounds in frame 0 and frame 1.
static int8_t   s_side     = -1;     // -1 = prop on the pet's left, +1 = right
static int16_t  s_park_x   = 0;

// =============================================================================
//  3. THE ONE PIXEL WRITER, AND THE BLITTER ON TOP OF IT
// =============================================================================

// THE BAYER MATRIX AND THE FIVE EASING FUNCTIONS MOVED TO ui/anim_ease.cpp AT
// P10-C3, unchanged. They were `static` here, in a translation unit that
// includes render.h and therefore Arduino.h, so no host binary had ever
// compiled one of them; ui/screen_encounter.cpp's two new films need the same
// arithmetic, and a second copy of a parabola is how two films come to
// disagree about where a beat ends. See the banner in ui/anim_ease.h.

// Clipped to the STAGE and the sprite band. Sets, never clears. Everything in
// this file that is not a poop replica goes through here.
static inline void af_px(int16_t x, int16_t y) {
  if (x < (int16_t)PETFX_STAGE_L || x > (int16_t)PETFX_STAGE_R) return;
  if (y < (int16_t)SPRITE_AREA_Y || y >= (int16_t)RD_AFFORD_Y)  return;
  rd_u8g2().drawPixel((u8g2_uint_t)x, (u8g2_uint_t)y);
}

// The documented exception (see the file header): clipped to the PANEL and the
// band, for the poop replicas only, which stand exactly where draw_poop() had
// already drawn them.
static inline void af_px_panel(int16_t x, int16_t y) {
  if (x < 0 || x >= (int16_t)OLED_W)                            return;
  if (y < (int16_t)SPRITE_AREA_Y || y >= (int16_t)RD_AFFORD_Y)  return;
  rd_u8g2().drawPixel((u8g2_uint_t)x, (u8g2_uint_t)y);
}

// The three dissolve directions are ui/anim_ease.h's now; the old spellings
// stay as aliases so the seven films below read exactly as they did.
#define AF_DIS_NONE  AE_DIS_NONE
#define AF_DIS_UP    AE_DIS_UP
#define AF_DIS_DOWN  AE_DIS_DOWN

// Transparent, clipped, row-windowed, optionally dissolving XBM blit.
//   row0..row1  which sprite rows to draw at all; clamped to the art. This is
//               how the bowl empties: it loses its top rows, one at a time.
//   dis / pct   AF_DIS_UP eats the sprite from the bottom upwards as pct goes
//               0 -> 100, AF_DIS_DOWN from the top downwards. Both work by
//               SKIPPING lit pixels; neither ever writes a 0.
//   panel       1 only for the poop replicas.
static void af_blit(int16_t x, int16_t y, const SpriteRef& r,
                    uint8_t row0, uint8_t row1,
                    uint8_t dis, uint8_t pct, uint8_t panel) {
  if (!r.bits || r.w == 0u || r.h == 0u) return;
  if (row1 >= r.h) row1 = (uint8_t)(r.h - 1u);
  if (row0 > row1) return;

  // The front is where the dissolve has got to, in sprite rows. See
  // ae_dissolve_front(): it deliberately travels three rows past each end, so
  // pct = 0 and pct = 100 mean exactly "all of it" and "none of it" whatever
  // the height happens to be.
  const int16_t front = ae_dissolve_front(dis, r.h, pct);

  const uint8_t stride = (uint8_t)((r.w + 7u) >> 3);
  for (uint8_t rr = row0; rr <= row1; ++rr) {
    const uint8_t* row = r.bits + (uint16_t)rr * (uint16_t)stride;
    for (uint8_t cc = 0; cc < r.w; ++cc) {
      if (((row[cc >> 3] >> (cc & 7u)) & 1u) == 0u) continue;
      const int16_t sx = (int16_t)(x + (int16_t)cc);
      const int16_t sy = (int16_t)(y + (int16_t)rr);
      if (ae_dissolve_skip(dis, rr, front, sx, sy)) continue;
      if (panel) af_px_panel(sx, sy);
      else       af_px(sx, sy);
    }
  }
}

// =============================================================================
//  4. SMALL MATHS
// =============================================================================
static inline uint32_t af_t(void) { return (uint32_t)(s_now - s_t0); }

// THE FOUR EASING FUNCTIONS ARE ui/anim_ease.cpp's SINCE P10-C3 - lifted byte
// for byte, not rewritten. These four names are kept as one-line forwards so
// the seven films below are unchanged and so `git log -L` on any of them still
// lands on the arithmetic. Every one of them is now driven by
// tests/test_anim.cpp; none of them had ever been executed by a host binary.
static inline int16_t af_hop(uint32_t t, uint32_t len, uint8_t amp) {
  return ae_hop(t, len, amp);
}
static inline int16_t af_lunge(uint32_t ph, uint32_t len, uint8_t amp) {
  return ae_lunge(ph, len, amp);
}
static inline int16_t af_lerp(uint32_t t, uint32_t len, int16_t a, int16_t b) {
  return ae_lerp(t, len, a, b);
}
static inline uint8_t af_pct(uint32_t t, uint32_t t0, uint32_t t1) {
  return ae_pct(t, t0, t1);
}


// Keep an emote of height h inside the sprite band, CLAMPING and not clipping.
// af_px() clips, which is the safety net and the wrong answer for a thing that
// is supposed to be read: an EMO_NOTE hung off a hopping adult asks for row -2
// and loses all ten of its rows, so the pet finishes its game of ball with
// nothing over its head. This is the same rule emote_spr() applies in ui.cpp,
// and for the same reason - half an emote is a message, none is not.
static int16_t af_band_y(int16_t y, uint8_t h) {
  const int16_t lo = (int16_t)SPRITE_AREA_Y;
  const int16_t hi = (int16_t)((int16_t)RD_AFFORD_Y - (int16_t)h);
  if (hi < lo) return lo;                       // taller than the band: top wins
  return (y < lo) ? lo : ((y > hi) ? hi : y);
}

// =============================================================================
//  5. GEOMETRY: WHERE THE PROP GOES
// =============================================================================

// Where a thing of width w sits when it hangs off the ink span [ix0,ix1] on
// `side`, with AF_PROP_GAP background columns between the two.
//
// The gap is the point of the function. What it will NOT do is leave the stage
// to get it: a prop half off the panel is a worse defect than a prop a little
// close, so a body pressed against a wall gives up separation down to
// AF_PROP_GAP_MIN and then gives up no more. af_place_prop() picks the side
// where that never has to happen, so with today's art this fallback is
// unreachable - it is here so that it stays a decision and not an accident.
static int16_t af_hang_x(int8_t side, uint8_t w, int16_t ix0, int16_t ix1) {
  const int16_t lo = (int16_t)PETFX_STAGE_L;
  int16_t       hi = (int16_t)((int16_t)PETFX_STAGE_R + 1 - (int16_t)w);
  if (hi < lo) hi = lo;                       // a prop wider than the stage
  int16_t x = (side < 0) ? (int16_t)(ix0 - (int16_t)AF_PROP_GAP - (int16_t)w)
                         : (int16_t)(ix1 + 1 + (int16_t)AF_PROP_GAP);
  if (x >= lo && x <= hi) return x;
  x = (side < 0) ? (int16_t)(ix0 - (int16_t)AF_PROP_GAP_MIN - (int16_t)w)
                 : (int16_t)(ix1 + 1 + (int16_t)AF_PROP_GAP_MIN);
  return (x < lo) ? lo : ((x > hi) ? hi : x);
}

// The LIVE version: hangs off the ink box as it is on THIS frame, on the side
// af_place_prop() froze. Used by the things that have to follow the 3 px recoil
// of a startle - the germ, the sweat, the tear - and never by the ground props,
// which are frozen (see af_place_prop).
static inline int16_t af_side_x(uint8_t w) {
  return af_hang_x(s_side, w, s_ix0, s_ix1);
}

// The flank with more clear air on it. The stage is 100 columns and the widest
// body 40, so the answer always has at least 30 free columns - enough for an
// emote plus the whole of its drift, wherever the animal happens to be standing.
static int8_t af_room_side(void) {
  const int16_t gap_l = (int16_t)(s_ix0 - (int16_t)PETFX_STAGE_L);
  const int16_t gap_r = (int16_t)((int16_t)PETFX_STAGE_R - s_ix1);
  return (gap_r >= gap_l) ? (int8_t)+1 : (int8_t)-1;
}

// Called once, on the first frame the body box is known. The prop comes from
// the NEAREST edge of the stage, so it travels the short way - unless it does
// not FIT in the gap between the body's ink and that edge, in which case it
// comes from the other one. Frozen afterwards: a bowl that changed its mind
// halfway across would read as a bug, not as a bowl.
//
// [wx0,wx1] IS THE WALL THE PROP IS PARKED AGAINST, and it is a parameter
// rather than s_ix0/s_ix1 because the box on the panel on frame 1 is not the
// box the film ends with. The body is POSE_IDLE while the bowl slides in and
// POSE_EAT by the time it leans over it, and sprite_set_id() maps POSE_EAT onto
// one shared per-band set whose ink is WIDER than the idle it replaces in
// fifteen of the sixteen species - 1 to 3 px for an adult, and 9 to 12 px on the
// right for a senior, whose idle body is 32 px and whose eating art is the
// shared 40 px adult set. petfx_draw_body() clears the CURRENT ink box in colour
// 0 on every frame, so a bowl parked against the narrow wall had a rectangular
// hole punched in it the instant the pose changed: measured over the full sweep,
// 7 % of frames lost more than a tenth of the prop, 4 % lost more than half, and
// the worst cell left 20 of a bowl's 63 pixels standing. The caller asks
// petfx_pose_ink_x() what the pose it is ABOUT to use would occupy and passes
// the wider of the two, so the wall does not move mid-film.
static void af_place_prop(uint8_t w, int16_t wx0, int16_t wx1) {
  if (wx0 > s_ix0) wx0 = s_ix0;               // the union, never the newer box
  if (wx1 < s_ix1) wx1 = s_ix1;
  const int16_t gap_l = (int16_t)(wx0 - (int16_t)PETFX_STAGE_L);
  const int16_t gap_r = (int16_t)((int16_t)PETFX_STAGE_R - wx1);
  int8_t side = (gap_l <= gap_r) ? (int8_t)-1 : (int8_t)+1;
  const int16_t near_gap = (side < 0) ? gap_l : gap_r;
  const int16_t far_gap  = (side < 0) ? gap_r : gap_l;
  const int16_t need     = (int16_t)((int16_t)w + (int16_t)AF_PROP_GAP);
  if (near_gap < need && far_gap >= need) side = (int8_t)-side;
  s_side   = side;
  s_park_x = af_hang_x(side, w, wx0, wx1);
}

// WHERE POOP i'S SPARK GOES: exactly where that poop was standing, centred on
// it, clipped to the PANEL and NOT dragged into the stage.
//
// Dragging it in is what the code used to do, and it broke the beat twice over.
// The four poops stand at x = 2, 114, 17 and 99, so the sparks want columns 4,
// 116, 19 and 101; the stage clamp moved the first two to 14 and 106, which is
// somewhere the mess had never been - and put 14..21 on top of 19..26 and
// 101..108 on top of 106..113. Since the twinkle gate was the index parity, and
// 0 pairs with 2 and 1 with 3, the two overlapping sparks were also the two
// DRAWN TOGETHER: what the panel showed was a 13 px smear at the left edge
// alternating with another at the right edge every 110 ms. A strobe, not a
// sparkle, and two of the four in the wrong place.
static int16_t af_spark_x(uint8_t i, uint8_t w) {
  return (int16_t)(s_poop_xs[i] + 6 - (int16_t)(w / 2u));
}

// The twinkle phase of every spark, so that two of them can never share a frame
// if they are close enough to smear into one another.
//
// The default is the index parity, which is what makes the four blink in two
// alternating pairs and is worth keeping. What it must not do is put two sparks
// that are less than one spark-width apart on the SAME beat, so any spark that
// would collide takes the other beat instead - the blink already exists, so the
// separation costs nothing but a decision about when. Recomputed from scratch on
// every frame over at most POOP_MAX entries: nothing stored, nothing to drift.
// A spark that collides on BOTH beats keeps the second one; with the real poop
// layout that cannot happen (the closest pair is 15 columns apart against a
// width of 8), and if the layout ever changed, one shared frame is a far
// smaller defect than a spark that has moved somewhere the mess never was.
static void af_spark_phases(uint8_t w, uint8_t* phase) {
  for (uint8_t i = 0; i < s_poop_n; ++i) {
    const int16_t xi = af_spark_x(i, w);
    uint8_t want = (uint8_t)(i & 1u);
    for (uint8_t attempt = 0; attempt < 2u; ++attempt) {
      uint8_t clash = 0;
      for (uint8_t j = 0; j < i && !clash; ++j) {
        if (phase[j] != want) continue;
        int16_t d = (int16_t)(af_spark_x(j, w) - xi);
        if (d < 0) d = (int16_t)-d;
        if (d < (int16_t)w) clash = 1;
      }
      if (!clash) break;
      want = (uint8_t)(want ^ 1u);
    }
    phase[i] = want;
  }
}

// Where a prop of width w sits while it is off stage on its own side. It is
// drawn CLIPPED, so the slide reads as the bowl coming out from behind the HUD
// badge instead of materialising on the edge column.
static inline int16_t af_offstage(uint8_t w) {
  return (s_side < 0) ? (int16_t)((int16_t)PETFX_STAGE_L - (int16_t)w)
                      : (int16_t)((int16_t)PETFX_STAGE_R + 1);
}

// The x a sliding prop is at right now. arrive_ms = when it has finished coming
// in, leave_ms = when it starts going out, gone_ms = when it is out of sight.
static int16_t af_prop_x(uint32_t t, uint8_t w, uint32_t arrive_ms,
                         uint32_t leave_ms, uint32_t gone_ms) {
  const int16_t off = af_offstage(w);
  if (t < arrive_ms) return af_lerp(t, arrive_ms, off, s_park_x);
  if (t < leave_ms)  return s_park_x;
  return af_lerp(t - leave_ms, gone_ms - leave_ms, s_park_x, off);
}

// AN EMOTE LEAVING THE ANIMAL: out of the SIDE of the head, then diagonally up
// and away as pct runs 0 -> 100.
//
// Diagonal because straight up does not exist here. See the rule in the file
// header: there are three to nine rows over an adult's head, so the heart of a
// finished meal moved three pixels and then sat against af_band_y()'s clamp for
// the rest of the beat, and the note of a finished game never moved at all -
// both measured on the framebuffer, identical pixels frame after frame. The
// horizontal half of the diagonal is drawn from the 30-plus free columns on the
// roomier flank, so the emote travels its whole length whatever the vertical
// happens to allow, and it starts flush against the silhouette so that it reads
// as coming OUT of the animal rather than appearing beside it.
static void af_drift_out(const SpriteRef& e, uint8_t pct, int16_t rise,
                         int16_t out) {
  const int8_t  dir = af_room_side();
  const int16_t up  = (int16_t)(((int32_t)rise * (int32_t)pct) / 100);
  const int16_t ax  = (int16_t)(((int32_t)out  * (int32_t)pct) / 100);
  const int16_t x0  = (dir < 0) ? (int16_t)(s_ix0 - (int16_t)e.w)
                                : (int16_t)(s_ix1 + 1);
  af_blit((int16_t)(x0 + (int16_t)dir * ax),
          af_band_y((int16_t)(s_iy0 + 3 - up), e.h),
          e, 0u, (uint8_t)(e.h - 1u), AF_DIS_NONE, 0u, 0u);
}

// =============================================================================
//  6. THE FEEDING PAIR (C1 / C2), which are one film with two sets of numbers
// =============================================================================
struct AfMealScript {
  uint32_t in_ms, lean_ms, eat_ms, empty_ms, out_ms, all_ms;
  uint8_t  bites, crumbs, hop_amp;
  uint8_t  icon, is_icon;      // is_icon picks sprite_icon() over sprite_emote()
  uint8_t  keep_row;           // the first row that survives being emptied
  uint8_t  last_ink;           // last row with ink: what stands ON the floor
};

// EMO_BOWL is 14x8: rows 0-1 are the steam over it, row 2 is blank, rows 3-7
// are the bowl. Emptying it means dropping rows 0..2, which reads exactly as
// "the food is gone" and leaves the crockery behind.
// ICO_SNACK is 12x12: rows 0-6 are the lolly, 7-10 the stick. Dropping rows
// 0..6 leaves the stick, which is the same joke told faster.
static const AfMealScript AF_MEAL = {
  AF_MEAL_IN_MS, AF_MEAL_LEAN_MS, AF_MEAL_EAT_MS, AF_MEAL_EMPTY_MS,
  AF_MEAL_OUT_MS, AF_MEAL_MS,
  (uint8_t)AF_MEAL_BITES, 3u, 3u, (uint8_t)EMO_BOWL, 0u, 3u, 7u
};
static const AfMealScript AF_SNACK = {
  AF_SNK_IN_MS, AF_SNK_LEAN_MS, AF_SNK_EAT_MS, AF_SNK_EMPTY_MS,
  AF_SNK_OUT_MS, AF_SNK_MS,
  (uint8_t)AF_SNK_BITES, 2u, 5u, (uint8_t)ICO_SNACK, 1u, 7u, 10u
};

static inline const AfMealScript& af_meal(void) {
  return (s_act == ACT_FEED_SNACK) ? AF_SNACK : AF_MEAL;
}

static inline SpriteRef af_meal_spr(const AfMealScript& m) {
  return m.is_icon ? sprite_icon(m.icon) : sprite_emote(m.icon);
}

// The prop stands ON the floor: its LAST INK ROW lands on PETFX_FLOOR_Y - 1,
// the same rule petfx.h applies to the body, so food and animal share a ground
// line instead of one of them floating above it.
static inline int16_t af_meal_y(const AfMealScript& m) {
  return (int16_t)((int16_t)PETFX_FLOOR_Y - 1 - (int16_t)m.last_ink);
}

// =============================================================================
//  7. PUBLIC API
// =============================================================================
void actfx_bind_poop_layout(const int16_t* xs, uint8_t n) {
  if (xs == nullptr) n = 0u;
  if (n > (uint8_t)POOP_MAX) n = (uint8_t)POOP_MAX;
  s_poop_xs = xs;
  s_poop_ns = n;
}

uint8_t actfx_active(void) { return (uint8_t)(s_act != ACT_NONE); }

void actfx_cancel(void) {
  // THE ONE LINE THIS WHOLE FEATURE CAN DIE ON. petfx_hold() stops the
  // automaton dead; a hold that outlives its choreography is a pet nailed to
  // the floor until the device is power-cycled. It is released here, it is
  // released again by petfx_begin(), and actfx_service() bounds the hold in
  // TIME anyway - three independent reasons why it cannot stick.
  if (s_held) { petfx_hold(0); s_held = 0; }
  s_act   = ACT_NONE;
  s_dur   = 0;
  s_geo   = 0;
  s_fired = 0;
}

void actfx_begin(uint8_t action, const PetView& before) {
  actfx_cancel();                       // no queue: the newest action wins

  uint16_t dur = 0;
  switch (action) {
    case ACT_FEED_MEAL:    dur = (uint16_t)AF_MEAL_MS;  break;
    case ACT_FEED_SNACK:   dur = (uint16_t)AF_SNK_MS;   break;
    case ACT_CLEAN:        dur = (uint16_t)AF_CLN_MS;   break;
    case ACT_MEDICINE:     dur = (uint16_t)AF_MED_MS;   break;
    case ACT_PET:          dur = (uint16_t)AF_PET_MS;   break;
    case ACT_PLAY:         dur = (uint16_t)AF_PLAY_MS;  break;
    case ACT_SLEEP_TOGGLE:
      // Which way round it goes is decided HERE, from the flags BEFORE the
      // action: by the time anyone else can look, PF_ASLEEP has already been
      // toggled and both films would be the same one.
      s_asleep0 = (uint8_t)((before.flags & PF_ASLEEP) != 0u);
      dur = (uint16_t)(s_asleep0 ? AF_WAKE_MS : AF_YAWN_MS);
      break;
    default: return;                    // nothing else has a film
  }

  s_act = action;
  // millis(), NOT s_now, AND THAT IS THE WHOLE FIX.
  //
  // s_now is only ever written by actfx_service(), which runs in section 3 of
  // loop(). This function is called from section 1 (a button, through
  // do_action()), and section 5 - where the radio pumps run, on top of a
  // ~24 ms sendBuffer() and an NVS write - sits after it. Stamped from a clock
  // that stale, the
  // very first tick measures t >= s_dur and cancels the film before one frame
  // of it is drawn: the action works, the toast appears, the screen jumps to
  // HOME and NOTHING HAPPENS. With smaller delays the bowl teleports to its
  // parking spot instead of sliding in.
  //
  // ui_begin() already primes s_now to patch the BOOT case of exactly this bug.
  // The steady-state case is the same bug, and reading the clock here is the fix
  // that needs no priming anywhere.
  s_t0  = millis();
  s_now = s_t0;             // so a frame drawn before the first tick reads t = 0
  s_dur = dur;

  s_poop_n = (uint8_t)((before.poop_count > s_poop_ns) ? s_poop_ns : before.poop_count);

  // Hold, do not freeze. petfx_freeze() would ALSO recentre the body, and the
  // whole point of this feature is that the pet is fed WHERE IT STANDS.
  petfx_hold(1);
  s_held = 1;

  if (action == ACT_PET) petfx_face_point((int16_t)(OLED_W / 2));

  // The film outranks the web frame-rate cap from its first frame - see
  // AF_FPS_HOLD_MS and rd_hold_fps(). Armed HERE and not only in
  // actfx_service(), because the request that starts a web action arms the cap
  // before this function is even entered.
  rd_hold_fps((uint8_t)FPS_NORMAL, (uint16_t)AF_FPS_HOLD_MS);
  rd_request_frame();
}

void actfx_service(uint32_t now_ms) {
  s_now = now_ms;
  if (s_act == ACT_NONE) return;

  const uint32_t t = af_t();
  if (t >= (uint32_t)s_dur) { actfx_cancel(); return; }

  // Idempotent, once per tick. If anything ever cleared the hold behind our
  // back (a petfx_reset() from a wipe, a god-mode hand-over) the pet would
  // wander off mid-meal rather than the choreography losing its grip on it.
  if (s_held) petfx_hold(1);

  // Renewed every tick for as long as the film lasts, and self-releasing
  // AF_FPS_HOLD_MS after the last renewal. ui_fps() already returns FPS_NORMAL
  // while a film is running, but that only moves the REQUEST: rd_fps() caps the
  // request at FPS_LOW for RENDER_WEB_BUSY_MS after every served web request,
  // and index_html.h polls /state every 700 ms, so with a phone watching - or
  // with the action itself launched from the phone - the cap was permanent and
  // a 2600 ms meal ran at ten frames instead of fifty-two, with every phase
  // shorter than 250 ms drawn once or not at all.
  rd_hold_fps((uint8_t)FPS_NORMAL, (uint16_t)AF_FPS_HOLD_MS);

  // C7's body language, and it is a COMPRESSION in both directions of the
  // toggle. See actfx_body_dy(): the yawn used to be a 4 px lift of the entire
  // body with the sprite already in POSE_SLEEP, which at +450 ms left the ink in
  // rows 23..48 and the shadow abandoned on 53-54 - a sleeping pet levitating.
  // petfx_squash() presses it into the ground instead; the 1 px of dy that goes
  // with it is in actfx_body_dy(). Renewed per tick, so it releases itself.
  if (s_act == ACT_SLEEP_TOGGLE) {
    const uint8_t press = (uint8_t)(s_asleep0
        ? ((t < 2u * AF_WAKE_HOP_MS) && ((t % AF_WAKE_HOP_MS) < (AF_WAKE_HOP_MS / 2u)))
        : ((t >= AF_YAWN_MS / 4u) && (t < (AF_YAWN_MS * 3u) / 4u)));
    if (press) petfx_squash(120u);
  }

  // Gaze. petfx_face_point() early-returns when the pet is already facing that
  // way, so calling it every tick costs one comparison and keeps the head on
  // whatever is moving.
  if (s_geo) {
    switch (s_act) {
      case ACT_FEED_MEAL:
      case ACT_FEED_SNACK: {
        const AfMealScript& m = af_meal();
        if (t < m.out_ms) {
          const SpriteRef r = af_meal_spr(m);
          petfx_face_point((int16_t)(s_park_x + (int16_t)(r.w / 2u)));
        }
        break;
      }
      case ACT_CLEAN:
        if (t < AF_CLN_SWEEP_MS)
          petfx_face_point(af_lerp(t, AF_CLN_SWEEP_MS, -12, (int16_t)(OLED_W + 12)));
        break;
      case ACT_MEDICINE:
        // "The pet turns, takes it": the pill falls down the free column beside
        // the animal, so the animal has to be looking at that column.
        if (t < AF_MED_FALL_MS)
          petfx_face_point((int16_t)(s_park_x + 6));
        break;
      case ACT_PLAY:
        if (t < AF_PLAY_BALL_MS)
          petfx_face_point(af_lerp(t, AF_PLAY_BALL_MS,
                                   (int16_t)(PETFX_STAGE_L + 6),
                                   (int16_t)(PETFX_STAGE_R - 5)));
        break;
      default: break;
    }
  }

  // The medicine flinch, exactly once, on the frame the pill reaches the mouth.
  if (s_act == ACT_MEDICINE && !s_fired && t >= AF_MED_FALL_MS) {
    s_fired = 1;
    petfx_startle(200u);
  }

  // NO rd_request_frame() HERE, SINCE THE FINAL REVIEW, AND THE HOLD ABOVE IS
  // WHY. actfx_service() runs on EVERY app_loop() pass (sm_service ->
  // screen update -> here), so an unconditional request set the frame deadline
  // to "now" every pass and the renderer free-ran for the whole film - the same
  // defect rd_hold_fps() carried, arriving by a second route. The rate this
  // film wants is already stated once, at actfx_begin(), by
  // rd_hold_fps(FPS_NORMAL, AF_FPS_HOLD_MS) plus the single rd_request_frame()
  // beside it that brings the first frame forward; rd_begin_frame() then paces
  // the rest through perf_advance_deadline(). Asking again every pass did not
  // make the film smoother, it removed the pacing.
  //
  // The hold IS still renewed every pass - by the rd_hold_fps() call near the
  // top of this function, which is where it belongs and which no longer moves
  // the frame deadline (see ui/render.cpp rd_hold_fps).
}

uint8_t actfx_pose(uint8_t fallback) {
  if (s_act == ACT_NONE) return fallback;
  const uint32_t t = af_t();
  switch (s_act) {
    case ACT_FEED_MEAL:
    case ACT_FEED_SNACK: {
      const AfMealScript& m = af_meal();
      // From the lean until the bowl is empty. POSE_EAT has its own art for
      // CHILD / TEEN / ADULT and falls back to IDLE for a baby inside
      // sprite_set_id(), which is the right answer: a baby has no eating pose.
      if (t >= m.lean_ms && t < m.empty_ms) return (uint8_t)POSE_EAT;
      break;
    }
    case ACT_MEDICINE:
      // Ill right up to the moment the germ finishes dissolving, healthy
      // afterwards. If the pet really was sick this simply agrees with
      // pet_pose_of(); if it was not, it is the story the animation is telling.
      if (t < AF_MED_GERM_MS) return (uint8_t)POSE_SICK;
      break;
    default: break;
  }
  return fallback;
}

uint8_t actfx_owns_body(void) { return (uint8_t)(s_act != ACT_NONE); }

// The bite, in one place, so dy and dx cannot tell different stories: how far
// out of `bites` this beat has driven, 0 .. AF_BITE_PX. Zero outside the eating
// phase and before the body box is known, because the direction of the lunge is
// the side af_place_prop() froze and that does not exist yet.
static int16_t af_bite_reach(const AfMealScript& m, uint32_t t) {
  if (!s_geo || t < m.lean_ms || t >= m.eat_ms || m.bites == 0u) return 0;
  const uint32_t len = (m.eat_ms - m.lean_ms) / m.bites;
  if (len == 0u) return 0;
  return af_lunge((t - m.lean_ms) % len, len, (uint8_t)AF_BITE_PX);
}

int16_t actfx_body_dy(void) {
  if (s_act == ACT_NONE) return 0;
  const uint32_t t = af_t();

  switch (s_act) {
    case ACT_FEED_MEAL:
    case ACT_FEED_SNACK: {
      const AfMealScript& m = af_meal();
      if (t < m.in_ms)    return 0;
      if (t < m.lean_ms)  return 1;                      // leaning over it
      if (t < m.eat_ms) {
        // ONE PIXEL, AND ONLY WHILE THE HEAD IS IN THE BOWL. The bite itself is
        // horizontal (actfx_body_dx); this is the accent that stops the lunge
        // reading as a flat slide. It cannot be bigger: the ink already stands
        // on PETFX_FLOOR_Y - 1, and petfx_draw_body() clamps its clear box to
        // that row, so a dip of 2 puts the last ink row on the shadow. Timed off
        // af_bite_reach() rather than off its own division of the beat, so the
        // dip lands exactly on the bite instead of drifting against it.
        return (int16_t)((af_bite_reach(m, t) >= (int16_t)AF_BITE_PX) ? 1 : 0);
      }
      if (t < m.empty_ms) return 1;                      // still nose-down
      if (t < m.out_ms)   return 0;                      // head up, bowl leaving
      return af_hop(t - m.out_ms, m.all_ms - m.out_ms, m.hop_amp);
    }

    case ACT_CLEAN:
      if (t < AF_CLN_SWEEP_MS) return 0;
      return af_hop(t - AF_CLN_SWEEP_MS, 400u, 4u);

    case ACT_MEDICINE:
      if (t < AF_MED_GERM_MS) return 0;
      return af_hop(t - AF_MED_GERM_MS, AF_MED_MS - AF_MED_GERM_MS, 4u);

    case ACT_PET:
      if (t < AF_PET_HOP_MS) return 0;
      return af_hop(t - AF_PET_HOP_MS, AF_PET_MS - AF_PET_HOP_MS, 3u);

    case ACT_PLAY:
      if (t < AF_PLAY_BALL_MS) return 0;
      return af_hop(t - AF_PLAY_BALL_MS, AF_PLAY_MS - AF_PLAY_BALL_MS, 4u);

    case ACT_SLEEP_TOGGLE:
      // DOWN, NEVER UP, in both directions of the toggle. What this replaces
      // was af_hop(t, AF_YAWN_MS, 4) - a 4 px lift of the entire body, with the
      // sprite already in POSE_SLEEP: at +450 ms the ink stood in rows 23..48
      // and the shadow lay abandoned on rows 53-54, which reads as a sleeping
      // animal levitating away from its own shadow. A body off its ground line
      // is precisely what the PETFX_FLOOR_Y contract exists to forbid, and a
      // yawn is not a jump anyway - it is a COMPRESSION. So the body presses
      // 1 px INTO the floor line and petfx_squash() (armed in actfx_service)
      // widens it by one at the same time, which is the other half of a squash.
      // The pet never leaves the ground here; the only things in this file that
      // do are the explicit hops of a finished meal, wash, medicine or game.
      if (s_asleep0) {
        // Waking: the same gesture reversed, twice and quickly - it gathers
        // itself and lets go, gathers itself and lets go.
        if (t >= 2u * AF_WAKE_HOP_MS) return 0;
        return (int16_t)(((t % AF_WAKE_HOP_MS) < (AF_WAKE_HOP_MS / 2u)) ? 1 : 0);
      }
      // Falling asleep: one slow press, held through the middle of the beat and
      // released before the contrast ramp takes the panel.
      return (int16_t)(((t >= AF_YAWN_MS / 4u) &&
                        (t < (AF_YAWN_MS * 3u) / 4u)) ? 1 : 0);

    default: return 0;
  }
}

// THE BITE. See actfx.h for why it is horizontal; here is what it does.
//
// The whole silhouette drives AF_BITE_PX pixels at the bowl, holds for a frame
// or two with its head in it, and comes back - once per bite, four times in a
// meal and twice in a snack. It is directed at the side af_place_prop() chose,
// so it is always a lunge TOWARDS the food and never away from it, and because
// petfx_draw_body() puts dx through the same stage clamp as everything else, an
// animal already pressed against a wall simply lunges the other way, into the
// open, which is the side its dinner is on anyway.
int16_t actfx_body_dx(void) {
  if (s_act == ACT_NONE) return 0;
  const uint32_t t = af_t();
  switch (s_act) {
    case ACT_FEED_MEAL:
    case ACT_FEED_SNACK: {
      const int16_t reach = af_bite_reach(af_meal(), t);
      return (int16_t)((s_side < 0) ? -reach : reach);
    }
    default: return 0;
  }
}

// =============================================================================
//  8. THE GROUND LAYER
// =============================================================================

// The crumbs of every bite so far, falling past the prop to the floor.
// Deterministic and stateless: no RNG, no array, and the same bite always
// throws the same crumbs.
//
// THEY FALL BESIDE THE PROP, NOT DOWN IT, and that is a measurement rather than
// a preference. EMO_BOWL is a SOLID silhouette from its rim (sprite row 3, i.e.
// screen row 47) to the floor line, so a crumb dropping from the rim to the
// ground travels its whole path inside lit pixels and is invisible for every
// frame of it - checked on the framebuffer, not guessed. The columns
// immediately outside the prop are empty on both sides, and one of them is the
// gap between the bowl and the animal, so crumbs there read as falling out of
// the pet's mouth. They start ABOVE the prop for the same reason.
static void af_draw_crumbs(uint32_t t, const AfMealScript& m, int16_t px, int16_t py,
                           uint8_t pw) {
  const uint32_t len = (m.eat_ms - m.lean_ms) / m.bites;
  if (len == 0u || t < m.lean_ms) return;
  const uint32_t elapsed = t - m.lean_ms;
  const uint32_t done    = elapsed / len;                 // bites started so far
  const int16_t  top     = (int16_t)(py - (int16_t)AF_CRUMB_RISE);
  const int16_t  ground  = (int16_t)(PETFX_FLOOR_Y - 1);
  for (uint32_t k = 0; k <= done && k < (uint32_t)m.bites; ++k) {
    const uint32_t age = elapsed - k * len;
    if (age >= AF_CRUMB_MS) continue;
    for (uint8_t c = 0; c < m.crumbs && c < (uint8_t)AF_CRUMB_N; ++c) {
      // Alternating sides, one column further out each time round.
      const int16_t cx = (c & 1u) ? (int16_t)(px + (int16_t)pw + (int16_t)(c >> 1))
                                  : (int16_t)(px - 1 - (int16_t)(c >> 1));
      // Staggered, so three crumbs are a scatter and not a bar.
      const uint32_t a = age + (uint32_t)c * 40u;
      if (a >= AF_CRUMB_MS) continue;
      af_px(cx, af_lerp(a, AF_CRUMB_MS, top, ground));
    }
  }
}

void actfx_draw_props(void) {
  if (s_act == ACT_NONE || !s_geo) return;
  U8G2& u = rd_u8g2();
  const uint8_t entry = u.getDrawColor();
  u.setDrawColor(1);

  const uint32_t t = af_t();
  switch (s_act) {
    // ---- C1 / C2: the bowl, or the treat -----------------------------------
    case ACT_FEED_MEAL:
    case ACT_FEED_SNACK: {
      const AfMealScript& m = af_meal();
      if (t < m.out_ms) {
        const SpriteRef r  = af_meal_spr(m);
        const int16_t   px = af_prop_x(t, r.w, m.in_ms, m.empty_ms, m.out_ms);
        const int16_t   py = af_meal_y(m);
        // Row 0 of what is left. It climbs to the first row the crockery (or
        // the lolly stick) occupies while the prop is being emptied.
        uint8_t row0 = 0u;
        if (t >= m.eat_ms) {
          const uint8_t pc = af_pct(t, m.eat_ms, m.empty_ms);
          row0 = (uint8_t)(((uint16_t)m.keep_row * (uint16_t)pc) / 100u);
        }
        af_blit(px, py, r, row0, (uint8_t)(r.h - 1u), AF_DIS_NONE, 0u, 0u);
        if (t < m.eat_ms) af_draw_crumbs(t, m, px, py, r.w);
      }
      break;
    }

    // ---- C3: the bubbles sweep, and every poop they cross dissolves --------
    case ACT_CLEAN: {
      // The sweep is VIRTUAL and crosses the whole 128 px panel, while the
      // bubbles themselves are clipped to the stage. That is what lets the two
      // poops standing outside the stage (kPoopX 2 and 114) dissolve when the
      // sweep reaches THEIR column instead of at some arbitrary moment.
      const int16_t   sweep = af_lerp(t, AF_CLN_SWEEP_MS, -12, (int16_t)(OLED_W + 12));
      const SpriteRef poop  = sprite_icon((uint8_t)ICO_POOP);
      const int16_t   ppy   = (int16_t)(RD_AFFORD_Y - 12);    // exactly draw_poop()

      for (uint8_t i = 0; i < s_poop_n; ++i) {
        const int16_t cx = (int16_t)(s_poop_xs[i] + 6);
        if (sweep < cx) {
          af_blit(s_poop_xs[i], ppy, poop, 0u, 11u, AF_DIS_NONE, 0u, 1u);
        } else {
          // The bubbles have passed: dissolve upwards. The clock is DERIVED
          // from the sweep rather than stored per poop, so it cannot drift and
          // needs no array.
          const uint32_t reach = (uint32_t)((((int32_t)cx + 12) * (int32_t)AF_CLN_SWEEP_MS)
                                            / (int32_t)(OLED_W + 24));
          const uint8_t pc = af_pct(t, reach, reach + AF_CLN_DISSOLVE_MS);
          if (pc < 100u) af_blit(s_poop_xs[i], ppy, poop, 0u, 11u, AF_DIS_UP, pc, 1u);
        }
      }

      if (t < AF_CLN_SWEEP_MS) {
        const SpriteRef b = sprite_emote((uint8_t)EMO_BUBBLES);
        // AT THE MESS'S HEIGHT, AND ACROSS THE WHOLE PANEL.
        //
        // The bubbles used to travel rows 40..52, clipped to the stage, while
        // the poops stand in rows 44..55 at columns 2, 17, 99 and 114. So they
        // appeared out of nowhere in column 14, passed FOUR ROWS ABOVE the
        // thing they were supposed to be washing, and vanished in column 113 -
        // while the two outer poops dissolved with nothing visible anywhere
        // near them. Both halves of that are fixed here: ppy is exactly
        // draw_poop()'s row, so the bubbles overlap eleven of a poop's twelve
        // rows, and the blit takes the same PANEL clip the replicas do, because
        // a wash that stops at the stage edge leaves half the mess untouched.
        // Rows 43..55: no HUD badge reaches below row 21.
        const int16_t by = (int16_t)(ppy - 1 + (int16_t)((t / 130u) & 1u));
        af_blit((int16_t)(sweep - 6), by, b, 0u, 11u, AF_DIS_NONE, 0u, 1u);
      }
      break;
    }

    // ---- C8: the ball ------------------------------------------------------
    case ACT_PLAY: {
      if (t >= AF_PLAY_BALL_MS) break;
      const SpriteRef ball = sprite_icon((uint8_t)ICO_BALL);
      const int16_t   bx   = af_lerp(t, AF_PLAY_BALL_MS, (int16_t)PETFX_STAGE_L,
                                     (int16_t)(PETFX_STAGE_R + 1 - 12));
      const uint32_t  len  = AF_PLAY_BALL_MS / AF_PLAY_BOUNCES;
      const int16_t   by   = (int16_t)((int16_t)PETFX_FLOOR_Y - 12
                                       + af_hop(t % len, len, 14u));
      af_blit(bx, by, ball, 0u, 11u, AF_DIS_NONE, 0u, 0u);
      break;
    }

    default: break;
  }

  u.setDrawColor(entry);
}

// =============================================================================
//  9. THE EMOTE LAYER
//
//  Everything below is additive and clipped, so none of it can damage the body
//  it is drawn on top of. That is the whole reason af_px() exists.
// =============================================================================
void actfx_draw_over(void) {
  if (s_act == ACT_NONE) return;

  // Sample the body FIRST: petfx_draw_body() has just run, so this is the only
  // moment in the frame when the ink box is exactly right, and it is what
  // actfx_draw_props() consumes on the NEXT frame.
  petfx_body_ink(&s_ix0, &s_iy0, &s_ix1, &s_iy1);
  if (!s_geo) {
    s_geo = 1;
    // THE WALL THE PROP IS PARKED AGAINST IS THE WALL THE FILM WILL END WITH,
    // not the one on the panel right now. On this frame the body is still in its
    // resting pose; a few hundred milliseconds later actfx_pose() switches it to
    // POSE_EAT or POSE_SICK, whose art is wider in fifteen of the sixteen
    // species, and petfx_draw_body() clears THAT box in colour 0 on every frame.
    // Parked against the narrow box, the bowl had a rectangle cut out of it the
    // moment the pose changed - see af_place_prop() for the numbers.
    int16_t px0 = s_ix0, px1 = s_ix1;
    if (s_act == ACT_FEED_MEAL || s_act == ACT_FEED_SNACK) {
      petfx_pose_ink_x((uint8_t)POSE_EAT, &px0, &px1);
      af_place_prop(af_meal_spr(af_meal()).w, px0, px1);
    } else if (s_act == ACT_MEDICINE) {
      // The pill and the germ are both 12 px and share the parked column.
      petfx_pose_ink_x((uint8_t)POSE_SICK, &px0, &px1);
      af_place_prop(sprite_emote((uint8_t)EMO_GERM).w, px0, px1);
    }
  }

  U8G2& u = rd_u8g2();
  const uint8_t entry = u.getDrawColor();
  u.setDrawColor(1);

  const uint32_t t = af_t();

  switch (s_act) {
    // ---- C1 / C2: the heart of a pet that has just been fed ----------------
    case ACT_FEED_MEAL:
    case ACT_FEED_SNACK: {
      const AfMealScript& m = af_meal();
      if (t >= m.out_ms) {
        // Off the SIDE of the head and diagonally away - see af_drift_out().
        // Straight up, which is what this used to do, is three to nine rows of
        // room on an adult: the heart moved three pixels and then sat against
        // the band clamp for the rest of the beat, rendering identical frames.
        af_drift_out(sprite_emote((uint8_t)EMO_HEART),
                     af_pct(t, m.out_ms, m.all_ms), 10, 10);
      }
      break;
    }

    // ---- C3: a spark where each poop used to be ----------------------------
    case ACT_CLEAN: {
      if (t < AF_CLN_SWEEP_MS) break;
      const SpriteRef sp  = sprite_emote((uint8_t)EMO_SPARK);
      const int16_t   ppy = (int16_t)(RD_AFFORD_Y - 12);       // draw_poop()'s row
      const uint8_t   now_ph = (uint8_t)((t / 110u) & 1u);
      uint8_t phase[POOP_MAX];
      af_spark_phases(sp.w, phase);
      for (uint8_t i = 0; i < s_poop_n; ++i) {
        if (phase[i] != now_ph) continue;                      // twinkling
        af_blit(af_spark_x(i, sp.w), af_band_y(ppy, sp.h), sp,
                0u, (uint8_t)(sp.h - 1u), AF_DIS_NONE, 0u, 1u);
      }
      break;
    }

    // ---- C4: the pill, the germ, the sparks --------------------------------
    case ACT_MEDICINE: {
      if (t < AF_MED_FALL_MS) {
        // DOWN THE FREE COLUMN BESIDE THE ANIMAL, TO THE FLOOR.
        //
        // The pill used to fall onto the top of the head: from SPRITE_AREA_Y to
        // `s_iy0 - pill.h`, which for an adult is row 12 - 12 = 0, clamped back
        // up to SPRITE_AREA_Y. Both ends of the interpolation were row 9, so the
        // pill rendered IDENTICAL PIXELS at +0, +150 and +300 ms: there was no
        // fall. There is no height over the head to fall through - the band is
        // 47 rows and the body 40 - so it falls where there IS height, which is
        // the parked column af_place_prop() reserved beside the body: from row 9
        // to the floor line, forty rows of actual travel. The animal turns to
        // watch it come down (see the gaze in actfx_service), takes it at
        // AF_MED_FALL_MS with a startle, and the germ fades off it afterwards.
        const SpriteRef pill = sprite_icon((uint8_t)ICO_MED);
        const int16_t   land = (int16_t)((int16_t)PETFX_FLOOR_Y - (int16_t)pill.h);
        af_blit(s_park_x,
                af_lerp(t, AF_MED_FALL_MS, (int16_t)SPRITE_AREA_Y, land),
                pill, 0u, (uint8_t)(pill.h - 1u), AF_DIS_NONE, 0u, 0u);
      } else if (t < AF_MED_GERM_MS) {
        const SpriteRef germ = sprite_emote((uint8_t)EMO_GERM);
        // BESIDE THE HEAD, NOT ON THE BODY, and that is a measurement and not a
        // taste. A transparent 12x12 germ ORed onto a filled silhouette adds
        // exactly zero visible pixels - checked on the framebuffer - and the
        // only way to show it ON the animal is to draw it in colour 0, which is
        // the one thing this module may never do. So it hangs off the side of
        // the head that has room, which is the grammar every other emote in
        // this firmware already uses, and it tracks the CURRENT ink box so the
        // 3 px recoil of the flinch does not leave it behind.
        //
        // Solid for a moment so the player sees WHAT is leaving, then a
        // descending dissolve. Additive: the fade removes lit pixels of the
        // germ, it never paints a 0 over anything.
        const uint8_t pc = af_pct(t, AF_MED_GULP_MS + 200u, AF_MED_GERM_MS);
        af_blit(af_side_x(germ.w), af_band_y((int16_t)(s_iy0 + 1), germ.h),
                germ, 0u, (uint8_t)(germ.h - 1u), AF_DIS_DOWN, pc, 0u);
      } else if (((t / 110u) & 1u) == 0u) {
        // One on each flank, at the same separation everything else parks at,
        // so a spark cannot fuse with the silhouette it is celebrating.
        const SpriteRef sp = sprite_emote((uint8_t)EMO_SPARK);
        af_blit(af_hang_x(-1, sp.w, s_ix0, s_ix1),
                af_band_y((int16_t)(s_iy0 + 2), sp.h),
                sp, 0u, (uint8_t)(sp.h - 1u), AF_DIS_NONE, 0u, 0u);
        af_blit(af_hang_x(+1, sp.w, s_ix0, s_ix1),
                af_band_y((int16_t)(s_iy0 + 6), sp.h),
                sp, 0u, (uint8_t)(sp.h - 1u), AF_DIS_NONE, 0u, 0u);
      }
      break;
    }

    // ---- C8: the note it hums when the ball stops --------------------------
    case ACT_PLAY: {
      if (t < AF_PLAY_BALL_MS) break;
      // Clear of the head from the first frame, for the same reason the germ is
      // beside it - a note drawn transparently over a filled body is nothing -
      // and diagonally OUT rather than up, for the same reason as the heart:
      // asked for `s_iy0 - n.h + 2 - rise`, an adult's note was clamped to
      // SPRITE_AREA_Y before it had moved a single pixel and never moved after.
      af_drift_out(sprite_emote((uint8_t)EMO_NOTE),
                   af_pct(t, AF_PLAY_BALL_MS, AF_PLAY_MS), 10, 10);
      break;
    }

    default: break;                     // PET keeps ui.cpp's hearts; SLEEP is dy
  }

  u.setDrawColor(entry);
}
