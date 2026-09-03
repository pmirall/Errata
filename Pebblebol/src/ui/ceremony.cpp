// =============================================================================
//  PEBBLEBOL - ui/ceremony.cpp
//  The hatch / evolution ceremony, moved here from ui.cpp by P2-C11c and
//  parameterised by CeremonyKind. See ceremony.h for the ordering argument.
//
//  DEVICE translation unit: it drives the panel's flash and shake registers
//  through render.h and draws the body through petfx. The pure screen that
//  hosts it is ui/screen_evolution.cpp.
// =============================================================================
#include "ceremony.h"

#include <stdio.h>

#include "../core/config.h"
#include "../core/nt_types.h"
#include "../core/strings_es.h"
#include "../data/sprites.h"
#include "petfx.h"
#include "render.h"
#include "ui.h"

// The phases, as cumulative offsets from t0 (config.h owns the numbers).
enum CeremonyPhase : uint8_t {
  CP_NONE = 0,
  CP_WOBBLE,   //    0 .. 1200   the egg rocks, accelerating
  CP_CRACK,    // 1200 .. 1800   crack art + three 0xD3 jolts
  CP_FLASH,    // 1800 .. 1880   one 0xA7 white frame
  CP_SHARDS,   // 1880 .. 2280   shell fragments (or sparks) thrown outwards
  CP_GROW,     // 2280 .. 3080   the body revealed by a descending dither
  CP_LOOK,     // 3080 .. 3880   looks left, then right
  CP_NAME      // 3880 .. 4480   the name
};

static CeremonyBodyFn s_body   = nullptr;
static uint8_t        s_kind   = CEREMONY_NONE;
static uint8_t        s_phase  = CP_NONE;
static uint32_t       s_t0     = 0;
static uint32_t       s_last   = 0;     // when the last show started, for the guard
static uint8_t        s_jolts  = 0;     // 0..3, one rd_shake() per jolt
static uint8_t        s_look   = 0;     // 0 = not yet, 1 = left done, 2 = right done

void ceremony_bind_body(CeremonyBodyFn fn) { s_body = fn; }

bool     ceremony_active(void) { return s_phase != CP_NONE; }
uint8_t  ceremony_kind(void)   { return s_kind; }

void ceremony_reset(void) {
  s_kind  = CEREMONY_NONE;
  s_phase = CP_NONE;
  s_t0    = 0;
  s_last  = 0;
  s_jolts = 0;
  s_look  = 0;
}

static inline uint32_t elapsed(uint32_t now_ms) { return (uint32_t)(now_ms - s_t0); }

bool ceremony_begin(uint8_t kind, uint32_t now_ms) {
  if (kind != CEREMONY_HATCH && kind != CEREMONY_EVOLVE) return false;
  if (ceremony_active()) return false;
  // The same event arriving twice must play once. sim_hatch() raises
  // SIM_EV_HATCHED on the tick AFTER the manual rub already started the show.
  if (s_last != 0 && (uint32_t)(now_ms - s_last) < HATCH_TOTAL_MS + 3000UL) return false;

  s_kind  = kind;
  s_jolts = 0;
  s_look  = 0;
  if (kind == CEREMONY_EVOLVE) {
    // There is no shell to rock or crack: start the clock already at the
    // flash, so one timeline serves both kinds and Phase 3 changes nothing.
    s_phase = CP_FLASH;
    s_t0    = (uint32_t)(now_ms - HATCH_T_FLASH);
    rd_flash(HATCH_FLASH_MS);
  } else {
    s_phase = CP_WOBBLE;
    s_t0    = now_ms;
  }
  s_last = s_t0;
  return true;
}

// -----------------------------------------------------------------------------
//  PHASE EDGES
// -----------------------------------------------------------------------------
void ceremony_service(uint32_t now_ms) {
  if (!ceremony_active()) return;
  const uint32_t el = elapsed(now_ms);

  switch (s_phase) {
    case CP_WOBBLE:
      if (el >= HATCH_T_CRACK) s_phase = CP_CRACK;
      break;

    case CP_CRACK: {
      // Three separate jolts, not one long decay: the shell gives way in steps.
      // A single decaying shake reads as a rumble; three read as a crack.
      const uint32_t ce   = (el > HATCH_T_CRACK) ? (el - HATCH_T_CRACK) : 0u;
      const uint8_t  want = (uint8_t)((ce / HATCH_JOLT_GAP_MS) + 1u);
      while (s_jolts < want && s_jolts < 3u) {
        ++s_jolts;
        rd_shake(HATCH_JOLT_PX, HATCH_JOLT_MS);
      }
      if (el >= HATCH_T_FLASH) { s_phase = CP_FLASH; rd_flash(HATCH_FLASH_MS); }
      break;
    }

    case CP_FLASH:
      if (el >= HATCH_T_SHARDS) s_phase = CP_SHARDS;
      break;

    case CP_SHARDS:
      if (el >= HATCH_T_GROW) s_phase = CP_GROW;
      break;

    case CP_GROW:
      if (el >= HATCH_T_LOOK) s_phase = CP_LOOK;
      break;

    case CP_LOOK:
      // petfx_freeze() stops LOCOMOTION, not the head. The first thing a
      // newborn does is check whether the world has anything in it.
      if (s_look == 0) {
        s_look = 1;
        petfx_face_point(0);
      } else if (s_look == 1 && el >= HATCH_T_LOOK + HATCH_LOOK_MS / 2u) {
        s_look = 2;
        petfx_face_point(OLED_W - 1);
      }
      if (el >= HATCH_T_NAME) { s_phase = CP_NAME; petfx_face_point(OLED_W / 2); }
      break;

    case CP_NAME:
      if (el >= HATCH_TOTAL_MS) {
        s_phase = CP_NONE;
        s_kind  = CEREMONY_NONE;
        petfx_freeze(0);
        ui_input_flush();   // a button held through the ceremony must not fire a
        ui_home();          // stale gesture on the HOME it lands on (AUDIT 15)
      }
      break;

    default:
      break;
  }
}

// -----------------------------------------------------------------------------
//  DRAWING
// -----------------------------------------------------------------------------
// Shell fragments. Six fixed directions scaled by an expanding radius; the
// table is deliberately asymmetric so it does not read as a mechanical star.
static const int8_t kShardDX[6] = { -4,  4, -4,  4, -1,  1 };
static const int8_t kShardDY[6] = { -2, -2,  1,  1,  3,  3 };

static void px_spr(int16_t x, int16_t y, const SpriteRef& r) {
  if (!r.bits || x < 0 || y < 0 || x >= OLED_W || y >= OLED_H) return;
  rd_u8g2().drawXBM((u8g2_uint_t)x, (u8g2_uint_t)y, r.w, r.h, r.bits);
}

static void px_box(int16_t x, int16_t y, int16_t w, int16_t h) {
  if (w <= 0 || h <= 0) return;
  if (x < 0) { w = (int16_t)(w + x); x = 0; }
  if (y < 0) { h = (int16_t)(h + y); y = 0; }
  if (w <= 0 || h <= 0) return;
  rd_u8g2().drawBox((u8g2_uint_t)x, (u8g2_uint_t)y, (u8g2_uint_t)w, (u8g2_uint_t)h);
}

bool ceremony_draw(uint32_t now_ms) {
  if (!ceremony_active()) return false;

  U8G2& u = rd_u8g2();
  const PetView* p     = s_body ? s_body() : nullptr;
  const uint32_t el    = elapsed(now_ms);
  const uint8_t  frame = (uint8_t)((now_ms / 120u) & 1u);   // fast: it is straining
  const bool     shell = (s_kind == CEREMONY_HATCH);

  // ---- 1. the egg rocking, accelerating ----------------------------------
  if (s_phase == CP_WOBBLE) {
    // Quadratic swing count: the rocking gets FASTER, which is what reads as
    // effort. el <= HATCH_WOBBLE_MS so el*el <= 1.44e6 and this stays in uint32.
    const uint32_t swings = (el * el) / HATCH_WOBBLE_K;
    const int16_t  amp    = (int16_t)(1 + (el * 3u) / HATCH_WOBBLE_MS);
    const SpriteRef r = sprite_egg(0, frame);
    px_spr((int16_t)((int16_t)sprite_center_x(r.w) + ((swings & 1u) ? amp : (int16_t)-amp)),
           (int16_t)sprite_center_y(r.h), r);
    rd_text_center(52, RD_FONT_BODY, S(STR_EGG_HATCHING));
    return true;
  }

  // ---- 2. the crack, and 3. the flash ------------------------------------
  // CP_FLASH draws the same thing: the 0xA7 invert is a PANEL state, so the
  // white frame costs nothing to draw and the egg simply reads as a dark
  // silhouette on white for 80 ms.
  if (shell && (s_phase == CP_CRACK || s_phase == CP_FLASH)) {
    const SpriteRef r = sprite_egg(1, frame);
    px_spr((int16_t)((int16_t)sprite_center_x(r.w) + (frame ? 2 : -2)),
           (int16_t)sprite_center_y(r.h), r);
    if (s_phase == CP_CRACK) rd_text_center(52, RD_FONT_BODY, S(STR_EGG_HATCHING));
    return true;
  }

  // ---- 4. the shards ------------------------------------------------------
  if (s_phase == CP_SHARDS || (!shell && s_phase == CP_FLASH)) {
    // Same clamp-the-input rule as CP_GROW below: an overrun must hold the last
    // frame, not run the radius off the panel and the dissolve past full.
    uint32_t t = (el > HATCH_T_SHARDS) ? (el - HATCH_T_SHARDS) : 0u;
    if (t > HATCH_SHARDS_MS) t = HATCH_SHARDS_MS;
    const int16_t rad = (int16_t)((t * 22u) / HATCH_SHARDS_MS);
    if (shell) {
      const SpriteRef r = sprite_egg(1, frame);
      px_spr((int16_t)sprite_center_x(r.w), (int16_t)sprite_center_y(r.h), r);
      // The shell disintegrates while the fragments leave: erase an increasing
      // share of it. Colour 0 over a colour-0 background is a no-op, so this is
      // clipped to the silhouette for free.
      u.setDrawColor(0);
      rd_dither_rect((int16_t)sprite_center_x(r.w), (int16_t)sprite_center_y(r.h),
                     r.w, r.h, (uint8_t)((t * RD_DITHER_MAX) / HATCH_SHARDS_MS));
      u.setDrawColor(1);
    }
    const int16_t cx = (int16_t)(OLED_W / 2 - 3);
    const int16_t cy = (int16_t)(SPRITE_AREA_Y + SPRITE_AREA_H / 2 - 3);
    for (uint8_t i = 0; i < 6; ++i)
      px_spr((int16_t)(cx + ((int16_t)kShardDX[i] * rad) / 4),
             (int16_t)(cy + ((int16_t)kShardDY[i] * rad) / 4), sprite_emote(EMO_SPARK));
    return true;
  }

  if (!p) return true;

  // ---- 5..7: the body is out. petfx owns where it is from here on ---------
  petfx_draw_body(*p, POSE_IDLE, frame, 0);

  if (s_phase == CP_GROW) {
    // A window that opens outwards from the body's waist (so it reads as
    // growing, not as wiping) and a dither that thins out (so it materialises,
    // not pops).
    // Clamp t, NOT lvl: with t past HATCH_GROW_MS the subtraction below
    // underflows a uint8 to ~255 and a "lvl > RD_DITHER_MAX" clamp would pin it
    // to a FULL erase of the sprite band - a black flash at the exact moment the
    // body finishes materialising. Clamping the input makes the overrun a no-op
    // that simply holds the final frame.
    uint32_t t = (el > HATCH_T_GROW) ? (el - HATCH_T_GROW) : 0u;
    if (t > HATCH_GROW_MS) t = HATCH_GROW_MS;
    const int16_t by = petfx_body_y();
    const int16_t bh = (int16_t)petfx_body_h();
    const int16_t cy = (int16_t)(by + bh / 2);
    const int16_t hh = (int16_t)(((int32_t)t * (int32_t)(bh / 2 + 1)) / (int32_t)HATCH_GROW_MS);
    const uint8_t lvl = (uint8_t)(RD_DITHER_MAX - (t * RD_DITHER_MAX) / HATCH_GROW_MS);
    u.setDrawColor(0);
    px_box(0, SPRITE_AREA_Y, OLED_W, (int16_t)(cy - hh - SPRITE_AREA_Y));
    px_box(0, (int16_t)(cy + hh), OLED_W,
           (int16_t)(SPRITE_AREA_Y + SPRITE_AREA_H - (cy + hh)));
    rd_dither_rect(0, SPRITE_AREA_Y, OLED_W, SPRITE_AREA_H, lvl);
    u.setDrawColor(1);
    return true;
  }

  if (s_phase == CP_NAME) {
    char name[16], line[64];
    ui_pet_name(name, sizeof(name));
    // {n} is the only key the template has; one substitution, no String.
    const char* tpl = S(STR_EGG_NAMED);
    size_t o = 0;
    for (size_t i = 0; tpl[i] != '\0' && o + 1 < sizeof(line); ) {
      if (tpl[i] == '{' && tpl[i + 1] == 'n' && tpl[i + 2] == '}') {
        for (const char* v = name; *v != '\0' && o + 1 < sizeof(line); ++v) line[o++] = *v;
        i += 3;
        continue;
      }
      line[o++] = tpl[i++];
    }
    line[o] = '\0';
    rd_text_center(52, RD_FONT_NARR, line);
    rd_text_center(61, RD_FONT_BODY, S(STR_HATCH_WELCOME));
    return true;
  }

  rd_text_center(52, RD_FONT_BODY, S(STR_HATCH_LOOK));   // CP_LOOK
  return true;
}
