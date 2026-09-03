// =============================================================================
//  NOTTAMAGOCHI - ui/ui.cpp
//  GAME_DESIGN 8.2 (global invariants) and 8.3 (per-screen gesture map), for
//  the screens that have not yet moved into the screen table: GAME, SOCIAL,
//  QR, EGG and the modal layer. Everything else is a ScreenDef row in
//  ui/screen_*.cpp, and what those rows need from the device - the animated
//  HOME stage, the diagnostics page, the clock, the simulation, the modals -
//  is bound and forwarded from section 20 of this file.
//
//  Invariants implemented literally (GAME_DESIGN 8.2):
//    1. GST_HOLD_R == BACK on every screen except GAME.
//    2. GST_LONG_BOTH == HOME from anywhere.
//    3. Every screen except HOME / GAME / EGG / GOD auto-returns to HOME
//       after UI_AUTORETURN_MS, with a 3 px countdown bar in the last
//       UI_COUNTDOWN_MS.
//    4. Every menu is a ring.
//    5. Every confirmation starts on NO.
//    6. The affordances are always drawn in the bottom 8 px. (QR is the one
//       documented exception: BRIEF 1.4 gives the 62 px symbol box those
//       rows, so the hint moves into the right-hand column.)
//    7. An alert never steals a press: the first gesture only dismisses it.
//
//  TYPOGRAPHY. Header bars are UI_HDR_H = 11 px, not STATUS_BAR_H = 9, and the
//  baseline sits at UI_HDR_BASE = 9. u8g2_font_t0_11b_tf has maxH 11 / asc 8 /
//  desc -2, so an accented capital (the "O" of MOVIL, the "I" of BUHO) needs
//  all eleven rows: at a 9 px bar the accent is silently clipped. HOME keeps
//  the 9 px STATUS_BAR_H because sprite_center_y() is defined against
//  SPRITE_AREA_Y and nothing in that strip is taller than 5x8_tf.
//
//  This module owns no game state. sim_* is the only thing that mutates the
//  pet, storage the only thing that writes flash, net the only thing that
//  touches a radio. millis() appears here for PRESENTATION timing only.
// =============================================================================

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "../core/config.h"
#include "../core/nt_types.h"
#include "../core/strings_es.h"
#include "../data/sprites.h"
#include "render.h"
#include "../hardware/input.h"
#include "../game/sim.h"
#include "../game/genome.h"
#include "../core/rng.h"
#include "../persistence/game_state.h"
#include "../hardware/kv_nvs.h"      // kv_error(), for the DIAG line
#include "../hardware/gametime.h"
#include "qr.h"
#include "../networking/net.h"
#include "../networking/ble_social.h"
#include "../networking/webui.h"      // web_pin() only - no network header comes with it
#include "../dev/godmode.h"    // GodEvt, god_active/handle/draw/entry_progress/marker
#include "petfx.h"      // the body's own presentation layer: floor, position, gaze
#include "actfx.h"      // the choreography of every action the player can take
#include "../game/box.h"          // the active slot: level, xp, hp for the view
#include "../data/species_table.h" // and the base_hp hp_max is derived from
#include "gfx.h"           // the header bar, the countdown and the list widgets
#include "screen.h"        // the ScreenDef table this file is being dissolved into
#include "screen_error.h"  // the ERROR screen moved out first (P2-C11a)
// P2-C11b moved HOME, MENU, CARE, PLAY, both STATUS pages, SETTINGS and TIME
// out. What is left here is the DEVICE half each of them needs - the animated
// body layer, the diagnostics page, the minigame start - which this file binds
// and hands over. It draws none of them.
#include "screen_home.h"
#include "screen_settings.h"
#include "screen_view.h"
// app/ sits ABOVE ui/, and this include points the wrong way on purpose: the
// strangler is moving the state machine out of this file, not wiring a new
// dependency into it. Every one of these calls disappears with the switch that
// still needs it (plan P2-C11).
#include "../app/state_machine.h"
#include "ui.h"

// =============================================================================
//  1. LAYOUT
// =============================================================================
// UI_HDR_H, UI_HDR_BASE, UI_AFFORD_Y and UI_CONTENT_BOTTOM moved to screen.h:
// a migrated screen may not include render.h, so the shared geometry has to
// live where both sides can see it.
#define UI_CONTENT_Y        UI_HDR_H                  // 11
#define UI_CONTENT_H        (RD_AFFORD_Y - UI_CONTENT_Y)   // 45

static_assert(UI_AFFORD_Y == RD_AFFORD_Y,
              "screen.h and render.h disagree about where the affordance strip starts");

static_assert(UI_LIST_ROWS * UI_LIST_PITCH <= UI_CONTENT_H + 1,
              "the vertical list does not fit under the header");
static_assert(UI_HDR_H >= 11, "t0_11b_tf accents clip in a shorter header");

// ui/screen_home.h has to name the stage geometry without including petfx.h
// (a migrated screen is a pure translation unit and petfx.h is not one). This
// is where the two are tied together: if petfx ever moves the floor or the
// HUD keep-out, HOME's meters and its static-body fallback move with it or
// this stops compiling.
static_assert(HOME_FLOOR_Y   == PETFX_FLOOR_Y,  "HOME and petfx disagree about the floor");
static_assert(HOME_STAGE_Y   == SPRITE_AREA_Y,  "HOME and config disagree about the stage top");
static_assert(HOME_METER_X   == UI_HUD_R_BEGIN, "HOME's meters would stand on the stage");
static_assert(HOME_METER_W   == UI_HUD_BADGE_W, "HOME's meters are wider than the HUD column");
static_assert(HOME_METER_X   > PETFX_STAGE_R,   "HOME's meters would stand on the stage");
// Three meters at HOME_METER_PITCH, the last one's bar included, must clear
// the floor line the body stands on.
static_assert(HOME_METER_Y0 + 2 * HOME_METER_PITCH + 12 <= HOME_FLOOR_Y,
              "HOME's third meter overlaps the floor");

// =============================================================================
//  2. LOCAL TYPES AND STATE
// =============================================================================

enum UiModal : uint8_t {
  MODAL_NONE = 0,
  MODAL_ALERT,      // ALERT overlay
  MODAL_CONFIRM,    // CONFIRM modal
  MODAL_HELP        // BOTH on a list item, UI_MODAL_HELP_MS
};

enum ConfirmId : uint8_t {
  CFM_NONE = 0,
  CFM_QUIT_GAME,
  CFM_MEDICINE,
  CFM_WIPE1,
  CFM_WIPE2
};

enum SocPhase : uint8_t {
  SOC_ENTER = 0,
  SOC_SCAN,
  SOC_ERROR
};

// ---- screen / navigation ----------------------------------------------------
// The current screen, the back stack and the two navigation clocks live in
// app/state_machine.cpp now (P2-C11a); reach them through sm_current(),
// sm_idle_ms() and sm_screen_ms().
static uint32_t s_wiggle_ms   = 0;
static uint8_t  s_fps_want    = FPS_NORMAL;
static uint8_t  s_god_prog    = 0;      // god_entry_progress(), 0..100

// ---- the last accepted action, replayed by the MENU's DBL_R -----------------
static uint8_t  s_last_action = ACT_NONE;

// ---- modal layer ------------------------------------------------------------
static uint8_t  s_modal       = MODAL_NONE;
static uint32_t s_modal_ms    = 0;
static uint16_t s_modal_str   = STR_EMPTY;
static uint8_t  s_confirm_id  = CFM_NONE;

// ---- SAVE ERROR (P2-C9c, moved to ui/screen_error.cpp by P2-C11a) ----------
static uint8_t  s_confirm_yes = 0;      // invariant 5: NO

// ---- alert queue ------------------------------------------------------------
static uint8_t  s_alert_q[UI_ALERT_QUEUE];
static uint8_t  s_alert_n     = 0;
static uint8_t  s_alert_cur   = AL_NONE;

// ---- toast ------------------------------------------------------------------
static char     s_toast[72];
static uint32_t s_toast_ms    = 0;

// ---- home -------------------------------------------------------------------
static uint32_t s_mimo_ms     = 0;
static uint32_t s_evolve_ms   = 0;

// ---- screen entry dissolve --------------------------------------------------
// The carousel and list interpolators moved into ui/gfx_widgets.cpp and
// ui/screen_menu.cpp with the screens that animate them (P2-C11b); what is
// left here is the dissolve, which belongs to the navigation and not to any
// one screen.
static uint32_t s_trans_ms    = 0;

// ---- welcome-back banner ----------------------------------------------------
static uint32_t s_absence_ms   = 0;
static uint32_t s_absence_s    = 0;
static uint8_t  s_absence_known = 1;   // 0 = the clock could not measure the gap

// ---- settings ---------------------------------------------------------------
static Config*  s_cfg         = nullptr;

// ---- egg --------------------------------------------------------------------
static uint8_t  s_rub_count   = 0;
static uint8_t  s_rub_last    = 0xFF;
static uint32_t s_rub_ms      = 0;

// ---- birth staging ----------------------------------------------------------
// A phase enum riding on the EGG screen rather than a ScreenId of its own.
// P2-C11's renumbering bullet is where the ceremony gets an id of its own.
enum HatchPhase : uint8_t {
  HP_NONE = 0,
  HP_WOBBLE,   //    0 .. 1200   the egg rocks, accelerating
  HP_CRACK,    // 1200 .. 1800   crack art + three 0xD3 jolts
  HP_FLASH,    // 1800 .. 1880   one 0xA7 white frame
  HP_SHARDS,   // 1880 .. 2280   shell fragments thrown outwards
  HP_GROW,     // 2280 .. 3080   the baby revealed by a descending dither
  HP_LOOK,     // 3080 .. 3880   looks left, then right
  HP_NAME      // 3880 .. 4480   the name
};

static uint8_t  s_hatch_phase = HP_NONE;
static uint32_t s_hatch_ms    = 0;
static uint8_t  s_hatch_jolts = 0;    // 0..3, one rd_shake() per jolt
static uint8_t  s_hatch_look  = 0;    // 0 = not yet, 1 = left done, 2 = right done

// Declared here, not in section 16, because ui_fps() and ui_input_locked()
// (section 6) are both above the ceremony and both have to know about it.
static inline bool hatch_active(void) { return s_hatch_phase != HP_NONE; }

// ---- QR ---------------------------------------------------------------------
static uint8_t  s_qr_mod[QR_BUF_BYTES];
static uint8_t  s_qr_size     = 0;
static uint8_t  s_qr_variant  = 0;      // 0 = URL, 1 = join-the-AP
static uint32_t s_qr_ms       = 0;
static uint32_t s_qr_manual   = 0;      // suppress auto-alternation after a tap
static char     s_qr_key[QR_TEXT_MAX];

// ---- SOCIAL -----------------------------------------------------------------
// The old s_cursor[SCR_COUNT] went with the screens that used it; SOCIAL is
// the only one left in this file that has a list.
static uint8_t  s_soc_cursor  = 0;
static uint8_t  s_soc_phase   = SOC_ENTER;
static uint8_t  s_soc_prev    = RADIO_OFF;
static uint16_t s_soc_err     = STR_EMPTY;

// ---- GAME -------------------------------------------------------------------
// MinigameState, not GameState: SaveSchema v2 owns that name for the whole
// persisted world (persistence/save_schema.h section 8). This is the transient
// state of the minigame currently on screen and never reaches flash.
struct MinigameState {
  uint8_t  id;
  uint8_t  phase;        // 0 = ready, 1 = running, 2 = result
  uint8_t  round;
  uint8_t  target;       // reflex: the lit side
  uint8_t  seq[10];
  uint8_t  seq_len;
  uint8_t  seq_pos;
  uint8_t  play_idx;
  uint8_t  passed;
  uint8_t  obs_n;
  uint16_t score;        // per-mille, 0..1000
  uint16_t last_ms;
  uint32_t arm_ms;       // reflex: dead time before the light
  int16_t  obs_x[3];
  int16_t  pet_dy;
  uint32_t t0;
  uint32_t step_ms;
  uint32_t jump_ms;      // jump: 0 = grounded, else takeoff timestamp
};
static MinigameState s_g;
static uint8_t  s_raw_prev[INPUT_BTN_N];

#define GAME_STEP_MS       25UL
#define GAME_INTRO_MS    1600UL
#define GAME_RESULT_MS   2200UL
#define REFLEX_ROUNDS       5
#define REFLEX_WINDOW_MS  900UL
#define MEMORY_LEVELS       6      // sequences of 3 .. 8
#define MEMORY_FLASH_MS   380UL
#define MEMORY_GAP_MS     170UL
#define JUMP_TARGET        12
#define JUMP_GROUND_Y      50
#define JUMP_RISE_MS      700UL
#define JUMP_HEIGHT        18

// =============================================================================
//  3. SMALL HELPERS
// =============================================================================

static inline uint32_t now_ms(void) { return millis(); }
static inline uint32_t since(uint32_t t) { return (uint32_t)(millis() - t); }
static inline const SimView* pet(void) { return sim_view(); }

// u8g2 coordinates are unsigned: a negative value wraps to ~65500 and paints
// at the far edge. Everything the UI computes goes through these.
static void px_box(int16_t x, int16_t y, int16_t w, int16_t h) {
  if (w <= 0 || h <= 0) return;
  if (x < 0) { w = (int16_t)(w + x); x = 0; }
  if (y < 0) { h = (int16_t)(h + y); y = 0; }
  if (w <= 0 || h <= 0) return;
  rd_u8g2().drawBox((u8g2_uint_t)x, (u8g2_uint_t)y, (u8g2_uint_t)w, (u8g2_uint_t)h);
}

static void px_frame(int16_t x, int16_t y, int16_t w, int16_t h) {
  if (w <= 0 || h <= 0 || x < 0 || y < 0) return;
  rd_u8g2().drawFrame((u8g2_uint_t)x, (u8g2_uint_t)y, (u8g2_uint_t)w, (u8g2_uint_t)h);
}

static void px_hline(int16_t x, int16_t y, int16_t w) {
  if (w <= 0 || y < 0) return;
  if (x < 0) { w = (int16_t)(w + x); x = 0; }
  if (w <= 0) return;
  rd_u8g2().drawHLine((u8g2_uint_t)x, (u8g2_uint_t)y, (u8g2_uint_t)w);
}

// drawXBM cannot clip a negative origin, so an off-screen sprite is dropped.
static void px_spr(int16_t x, int16_t y, const SpriteRef& r) {
  if (!r.bits || x < 0 || y < 0 || x >= OLED_W || y >= OLED_H) return;
  rd_u8g2().drawXBM((u8g2_uint_t)x, (u8g2_uint_t)y, r.w, r.h, r.bits);
}

// {key} substitution over a strings_es.h template. No String, no heap.
struct FmtArg { char key; const char* val; };

static void fmt_apply(char* out, size_t cap, const char* tpl,
                      const FmtArg* args, uint8_t n) {
  if (!out || cap == 0) return;
  if (!tpl) { out[0] = '\0'; return; }
  size_t o = 0;
  for (size_t i = 0; tpl[i] != '\0' && o + 1 < cap; ) {
    if (tpl[i] == '{' && tpl[i + 1] != '\0' && tpl[i + 2] == '}') {
      const char k = tpl[i + 1];
      const char* v = nullptr;
      for (uint8_t a = 0; a < n; ++a) if (args[a].key == k) { v = args[a].val; break; }
      if (v) {
        while (*v != '\0' && o + 1 < cap) out[o++] = *v++;
        i += 3;
        continue;
      }
    }
    out[o++] = tpl[i++];
  }
  out[o] = '\0';
}

// GAME_DESIGN 9.3: the name is a pure function of (lineage_id, generation), so
// a given pet is called the same thing on every device, forever.
void ui_name_for(uint32_t lineage_id, uint8_t generation, char* out, size_t cap) {
  if (!out || cap == 0) return;
  uint32_t h = lineage_id ^ 0x9E3779B9u;
  h ^= (uint32_t)generation * 0x85EBCA6Bu;
  h ^= h >> 15; h *= 0x2545F491u; h ^= h >> 13;
  snprintf(out, cap, "%s%s", S_SYL_A(h % 12u), S_SYL_B((h / 12u) % 12u));
}

void ui_pet_name(char* out, size_t cap) {
  if (!out || cap == 0) return;
  if (s_cfg && s_cfg->pet_name[0] != '\0') { snprintf(out, cap, "%s", s_cfg->pet_name); return; }
  const SimView* p = pet();
  if (!p) { snprintf(out, cap, "%s", S(STR_EGG_TITLE)); return; }
  ui_name_for(p->genome.lineage_id, p->genome.generation, out, cap);
}

// GAME_DESIGN 6.3's score -> face table, via webui's copy rather than a second
// one here: two independent ladders would eventually disagree about the pet's
// face. The policy moves into the render layer with the PetView struct.
static uint8_t mood_of(void) { return web_mood_index(sim_mood_score()); }

// ---- displayed stat smoothing ----------------------------------------------
// rd_bar() paints the truth instantly, so a meal that moves hunger 30 points is
// over before the eye registers it. This layer walks the DISPLAYED value
// towards sim_stat_pct() at UI_STAT_RATE_PCT per UI_STAT_STEP_MS - 4 % per frame
// at FPS_NORMAL - and is advanced on a wall clock rather than per frame, so the
// speed does not change when ui_fps() drops to FPS_LOW.
//
// It SNAPS, never animates, in three cases, because animating them is what
// makes bars look like they are leaking rather than reporting:
//   - the pet identity changed (hatch, wipe): the bars would crawl from the old
//     pet's values to the newborn's,
//   - ui_note_absence() landed: after 3 days away the bars ARE what they are,
//   - more than UI_STAT_SNAP_GAP_MS passed between pumps (a long radio stall).
// Integer throughout: percentages, no remainder, no float.
static uint8_t  s_stat_shown[ST_COUNT];
static uint32_t s_stat_ms  = 0;
static uint32_t s_stat_key = 0;
static uint8_t  s_stat_ok  = 0;      // 0 = nothing shown yet -> snap on first use

static uint32_t stat_identity(const SimView* p) {
  if (!p) return 0;
  return p->genome.lineage_id ^ ((uint32_t)p->genome.generation << 24) ^ p->birth_epoch;
}

static void stat_snap(void) {
  const SimView* p = pet();
  for (uint8_t i = 0; i < ST_COUNT; ++i)
    s_stat_shown[i] = p ? sim_stat_pct((StatId)i) : 0u;
  s_stat_key = stat_identity(p);
  s_stat_ms  = now_ms();
  s_stat_ok  = 1;
}

static void stat_service(void) {
  const SimView* p   = pet();
  const uint32_t key = stat_identity(p);
  if (!s_stat_ok || key != s_stat_key)  { stat_snap(); return; }
  const uint32_t el = since(s_stat_ms);
  if (el >= UI_STAT_SNAP_GAP_MS)        { stat_snap(); return; }
  if (el <  UI_STAT_STEP_MS) return;

  const uint32_t steps = el / UI_STAT_STEP_MS;
  s_stat_ms += steps * UI_STAT_STEP_MS;          // keep the phase, never drift
  uint16_t room = (uint16_t)(steps * (uint32_t)UI_STAT_RATE_PCT);
  if (room > 100u) room = 100u;

  for (uint8_t i = 0; i < ST_COUNT; ++i) {
    const uint8_t want = p ? sim_stat_pct((StatId)i) : 0u;
    const uint8_t have = s_stat_shown[i];
    if (want > have)
      s_stat_shown[i] = ((uint16_t)(want - have) <= room) ? want : (uint8_t)(have + room);
    else if (want < have)
      s_stat_shown[i] = ((uint16_t)(have - want) <= room) ? want : (uint8_t)(have - room);
  }
}

// The number every bar on every screen should be drawn from.
static uint8_t ui_stat_shown(StatId id) {
  if ((uint8_t)id >= ST_COUNT) return 0;
  if (!s_stat_ok) stat_snap();
  return s_stat_shown[(uint8_t)id];
}

// True for exactly one ui_note_events() batch: the one boot_absence() drains
// straight after ui_note_absence(). Everything in that batch happened while the
// device was off, and nothing in it may claim the player's attention as if it
// were happening now.
static uint8_t s_events_offline = 0;

// ---- panel contrast intent --------------------------------------------------
// ui owns WHAT the brightness should be, render owns HOW it gets there.
// s_bright_base is the USER's setting; the sleep dim is an override on top of
// it, so waking restores the user's number and not a constant. Idempotent by
// construction - it writes nothing when the target has not changed - which is
// what lets it be called from an event AND from the per-loop pump.
static uint8_t s_bright_base  = OLED_CONTRAST_DEFAULT;
static uint8_t s_bright_now   = OLED_CONTRAST_DEFAULT;  // last value handed to render
// "Nothing has been handed to render yet" is a FLAG, never a magic value of
// s_bright_now. 0xFF used to play that part and 255 is a perfectly legal
// contrast: kBrightSteps[4] is 255 and webui.cpp accepts br up to 255. A user
// who picked maximum brightness got want == 255 == the sentinel on every boot,
// bright_service() returned without writing, and the panel stayed on
// OLED_CONTRAST_DEFAULT - the setting silently lost at every power-up.
static uint8_t s_bright_valid = 0;

static void bright_service(void) {
  const SimView* p = pet();
  const uint8_t want = (p && (p->flags & PF_ASLEEP)) ? (uint8_t)OLED_CONTRAST_DIM
                                                     : s_bright_base;
  if (s_bright_valid && want == s_bright_now) return;
  // Falling asleep is slow and reluctant; waking is quicker. Not symmetric on
  // purpose: 2 s down reads as drifting off, 2 s up reads as a fault. The FIRST
  // application has no previous value to compare against, so it takes the fast
  // ramp - it is a boot, not a pet dozing off.
  const uint16_t ms = (s_bright_valid && want < s_bright_now)
                        ? (uint16_t)UI_SLEEP_DIM_MS : (uint16_t)UI_WAKE_RAMP_MS;
  s_bright_now   = want;
  s_bright_valid = 1;
  rd_contrast_ramp(want, ms);
}

// =============================================================================
//  4. TOASTS, ALERTS, MODALS
// =============================================================================

static void toast_text(const char* txt) {
  if (!txt) return;
  snprintf(s_toast, sizeof(s_toast), "%s", txt);
  s_toast_ms = now_ms();
}

void ui_toast(uint16_t str_id) {
  if (str_id >= (uint16_t)STR_COUNT) return;
  toast_text(S(str_id));
}

static void cfg_persist(void) {
  if (!s_cfg) return;
  s_cfg->saved_epoch = gt_now();
  gs_save_cfg(*s_cfg);
  ui_toast(STR_SET_SAVED);
}

void ui_alert(AlertId a) {
  if (a == AL_NONE || a >= AL_COUNT) return;
  if (s_alert_cur == (uint8_t)a) return;
  for (uint8_t i = 0; i < s_alert_n; ++i) if (s_alert_q[i] == (uint8_t)a) return;
  if (s_alert_n >= UI_ALERT_QUEUE) return;
  s_alert_q[s_alert_n++] = (uint8_t)a;
}

static void alert_pop(void) {
  if (s_alert_n == 0) return;
  s_alert_cur = s_alert_q[0];
  for (uint8_t i = 1; i < s_alert_n; ++i) s_alert_q[i - 1] = s_alert_q[i];
  --s_alert_n;
  s_modal    = MODAL_ALERT;
  s_modal_ms = now_ms();
  rd_request_frame();
}

static void modal_close(void) {
  s_modal      = MODAL_NONE;
  s_alert_cur  = AL_NONE;
  s_confirm_id = CFM_NONE;
  s_modal_str  = STR_EMPTY;
}

static void confirm_open(uint8_t which, uint16_t str_id) {
  s_confirm_id  = which;
  s_modal_str   = str_id;
  s_confirm_yes = 0;                 // invariant 5
  s_modal       = MODAL_CONFIRM;
  s_modal_ms    = now_ms();
  rd_request_frame();
}

static void help_open(uint16_t str_id) {
  s_modal_str = str_id;
  s_modal     = MODAL_HELP;
  s_modal_ms  = now_ms();
}

// =============================================================================
//  5. ACTIONS
// =============================================================================
static bool do_action(ActionId a) {
  // THE COPY HAS TO BE TAKEN FIRST. sim_apply_action(ACT_CLEAN) sets poop_count
  // to 0 on the spot, so a snapshot taken afterwards has nothing left for the
  // choreography to dissolve; ACT_SLEEP_TOGGLE has the same problem with
  // PF_ASLEEP, where "before" is the only thing that says which way round the
  // animation goes. 128 bytes of stack, once per button press.
  SimView        before;
  const SimView* p0  = pet();
  const bool     had = (p0 != nullptr);
  if (had) before = *p0;

  ActionResult r;
  const bool ok = sim_apply_action(a, r);
  if (ok) {
    s_last_action = (uint8_t)a;
    if (a == ACT_PET) s_mimo_ms = now_ms();
    // Only a SUCCESSFUL action gets a film. A rejected one (cooldown, full,
    // asleep) keeps its toast and nothing else, which is the honest
    // reading: nothing happened to the pet, so nothing happens on screen.
    if (had) actfx_begin((uint8_t)a, before);
    if (r.str_id) ui_toast(r.str_id);
    const SimView* p = pet();
    if (p) gs_save_active(true);
  } else if (r.err == AERR_COOLDOWN && r.cooldown_s) {
    // The base line has no {t}; the countdown is appended so the wait is honest.
    char buf[64];
    snprintf(buf, sizeof(buf), "%s %u s", S(STR_AERR_COOLDOWN), (unsigned)r.cooldown_s);
    toast_text(buf);
  } else {
    ui_toast(r.str_id ? r.str_id : (uint16_t)(STR_AERR_NONE + r.err));
  }
  rd_request_frame();
  return ok;
}

// =============================================================================
//  6. NAVIGATION
// =============================================================================
static void screen_enter(uint8_t s);
static void screen_leave(uint8_t s);

// Defined further down, next to the widgets they animate; ui_goto() is above
// all of them and is the one place that has to cut them. Only the FUNCTIONS
// are forward declared: the state they move is defined once in section 2,
// because a file-scope static cannot be declared then defined in C++.
static bool screen_wants_transition(uint8_t s);

uint8_t ui_fps(void) {
  // A migrated screen states the rate it wants in its table row; 0 means "no
  // opinion" and falls through to the rules below.
  const ScreenDef* d = sm_def();
  if (d && d->fps) return d->fps;
  // The birth is 4.5 s of animation on a pet that has just been created: the
  // energy / PF_ASLEEP fallbacks below would be reading a state that did not
  // exist a moment ago, and a 4 fps hatch is not a hatch.
  if (hatch_active()) return FPS_NORMAL;
  if (sm_current() == SCR_GAME) return FPS_NORMAL;
  // A choreography is 28 to 52 frames. At FPS_LOW (4 fps) a meal would be ten
  // frames long and read as a fault - and the two states that ASK for FPS_LOW,
  // a sleeping pet and an exhausted one, are exactly the ones ACT_SLEEP_TOGGLE
  // and a feed are aimed at.
  if (actfx_active()) return FPS_NORMAL;
  const SimView* p = pet();
  if (p && (p->flags & PF_ASLEEP)) return FPS_LOW;
  if (p && sim_stat_pct(ST_ENERGY) < FPS_LOW_ENERGY_PCT) return FPS_LOW;
  return FPS_NORMAL;
}

static void apply_fps(void) {
  const uint8_t want = ui_fps();
  if (want != s_fps_want) { s_fps_want = want; rd_set_fps(want); }
}

// The five movers are sm_*() now. ui_goto() stays because it is the mandated
// public entry point (ui.h) and because the entry point calls it.
void ui_goto(ScreenId s) { sm_goto(s); }

static void nav_push(uint8_t to) { sm_push((ScreenId)to); }
static void nav_back(void)       { sm_back(); }
static void nav_home(void)       { sm_home(); }

// The three parts of a screen change that are still this file's: the legacy
// enter / leave hooks, the interpolators the shared widgets animate, and the
// entry dissolve. state_machine.cpp calls them in that order (ui.h).
void ui_nav_leave(uint8_t from) { screen_leave(from); }
void ui_nav_enter(uint8_t to)   { screen_enter(to); }

void ui_nav_reset(void) {
  modal_close();
  gfx_list_reset();
}

void ui_nav_arrived(uint8_t to) {
  // A 3-step dither dissolve on entry (see draw_transition).
  s_trans_ms = screen_wants_transition(to) ? now_ms() : 0u;
  s_fps_want = 0xFF;                 // force a re-apply on the new screen
  apply_fps();
  rd_request_frame();
}

// EVERY ACTION WITH A CHOREOGRAPHY SENDS THE PLAYER HOME TO WATCH IT. That is
// the whole point of the feature - "if I feed the pet, I have to see it eat" -
// and it is what the old code did not do: handle_feed() fed the pet and then
// nav_back()ed to the carousel, menu_select() cleaned it and stayed on the
// carousel, and the medicine confirmation closed onto the carousel too.
//
// The test is actfx_active() and NOT a list of ActionIds, so an action that
// gains or loses a film changes this behaviour by changing actfx.cpp and
// nothing else. Two things therefore never navigate, both deliberately:
//   * ACT_LIGHT_TOGGLE, which arms no film - its whole choreography is a
//     register flash plus the contrast ramp, and both are PANEL-wide and read
//     perfectly from wherever the player is standing;
//   * a REJECTED action, which has no film to watch. The caller keeps whatever
//     destination it had, and the toast explains itself where the player is.
static bool act_and_show(ActionId a) {
  const bool ok = do_action(a);
  // ALREADY HOME IS NOT A NAVIGATION, and calling ui_goto(SCR_HOME) as if it
  // were had a visible cost. ui_goto() skips screen_leave() when the screen does
  // not change - so the film survived, which is why this looked harmless - but
  // it still stamps s_trans_ms, and screen_wants_transition(SCR_HOME) is true,
  // so draw_transition() dither-dissolved the ENTIRE panel for 150 ms on top of
  // the choreography that had just started. A double-tap to stroke the pet is
  // made ON HOME, and so is answering an AL_DIRTY / AL_POOP / AL_TIRED alert,
  // which is the normal case for all three: the player pressed a button and the
  // screen fell apart. In the firmware the user has flashed, that gesture did
  // not navigate and there was no dissolve.
  if (ok && actfx_active() && sm_current() != SCR_HOME) nav_home();
  return ok;
}

ScreenId ui_screen(void) {
  if (s_modal == MODAL_ALERT)   return SCR_ALERT;
  if (s_modal == MODAL_CONFIRM) return SCR_CONFIRM;
  return sm_current();
}

bool ui_input_locked(void) {
  // The birth ceremony is unskippable. It is also short enough that locking the
  // buttons costs the player nothing.
  return hatch_active();
}

static uint8_t ring_next(uint8_t cur, uint8_t n) { return n ? (uint8_t)((cur + 1u) % n) : 0u; }

// =============================================================================
//  7. SHARED CHROME
// =============================================================================

// The header bar and the auto-return countdown are gfx_widgets.cpp's now, so
// the migrated screens and the four that are still here draw the identical
// thing. draw_countdown() keeps the sticky test, which is navigation state and
// not a drawing decision.
static void draw_header(const char* title, const char* tag) {
  gfx_header(title, tag);
}

static void draw_countdown(void) {
  if (sm_is_sticky()) return;
  gfx_countdown(sm_idle_ms());
}

// ---- screen entry dissolve --------------------------------------------------
// The new screen is drawn in full and then 75 % / 50 % / 25 % of its pixels are
// ERASED, so it materialises over three steps.
//
// TIME-based, not frame-counted. At FPS_NORMAL that is three frames; at
// FPS_LOW (a sleeping pet, or a phone hammering the web server) exactly one
// dim frame lands inside the window and the transition degrades gracefully
// instead of stretching to three quarters of a second.
//
// Cost: three frames that would have been drawn anyway. No extra sendBuffer().
static bool screen_wants_transition(uint8_t s) {
  // GAME costs reaction time and the reflex game is scored in milliseconds.
  // GOD owns the whole frame by contract.
  return s != SCR_GAME && s != SCR_GOD;
}

static void draw_transition(void) {
  if (s_trans_ms == 0) return;
  const uint32_t el = since(s_trans_ms);
  if (el >= UI_TRANS_MS) { s_trans_ms = 0; return; }
  const uint8_t step = (uint8_t)((el * 3u) / UI_TRANS_MS);        // 0..2
  U8G2& u = rd_u8g2();
  u.setDrawColor(0);
  rd_dither_rect(0, 0, OLED_W, OLED_H,
                 (uint8_t)(RD_D75 - step * (RD_DITHER_MAX / 4u)));   // 12, 8, 4
  u.setDrawColor(1);
}

static void draw_toast(void) {
  if (s_toast[0] == '\0' || since(s_toast_ms) > UI_TOAST_MS) return;
  U8G2& u = rd_u8g2();
  const int16_t y = RD_AFFORD_Y - 11;
  px_box(0, y, OLED_W, 11);
  u.setDrawColor(0);
  rd_text_center((int16_t)(y + 8), RD_FONT_BODY, s_toast);
  u.setDrawColor(1);
}

// =============================================================================
//  8. HOME - the screen the user stares at for days
// =============================================================================

// The three status-bar modes are gone with the legacy HOME: ui/screen_home.cpp
// draws the one strip spec section 8 asks for (name, level, HP, hunger,
// happiness, XP) and it has no modes to cycle. Config.statusbar_mode is left
// in the persisted struct - the layout is frozen - and nothing reads it.

// POSE_EAT, and the POSE_SICK the medicine film borrows, are device-only
// transients: the web mirror caches sprite sets by id and would have to refetch
// for a two-second animation. Everything else defers to web_pose_of() so the
// phone and the panel share one table.
//
// The pose window now belongs to actfx, not to a UI_EAT_POSE_MS constant: it is
// the length of the FILM, so the pose and the bowl standing on the floor cannot
// disagree about when the meal ended. A sleeping pet keeps its own pose whatever
// the choreography thinks - except that it never asks, because
// ACT_SLEEP_TOGGLE's film is carried entirely by actfx_body_dy().
static uint8_t home_pose(const SimView* p) {
  if (!p) return POSE_IDLE;
  const uint8_t base = web_pose_of(*p);
  if (p->flags & PF_ASLEEP) return base;
  return actfx_pose(base);
}

// px_spr() DROPS a sprite whose origin leaves the panel - it does not clip it.
// Harmless while the body was nailed to x = 44; now that petfx walks the pet to
// either wall it silently deletes STATE. EMO_EXCLAM is how "clean me", "feed me"
// and "I am ill" announce themselves, and sim_alert() != AL_NONE is an ordinary
// condition, so an emote that vanishes is a message that never arrives.
//
// FALLING OFF THE PANEL IS NOT THE ONLY WAY TO VANISH, and that was the hole in
// the first version of this helper. draw_home() stamps the OPAQUE 12x12 HUD
// badge AFTER the entire pet layer, so an emote that lands in the HUD columns is
// erased just as completely as one that fell off the edge - only later, and only
// sometimes, which is harder to see and no better. Measured: EMO_EXCLAM with the
// pet at x = 14..17 asks for x = 6..9 and loses 10 of its 11 rows under the
// left HUD band; EMO_NOTE with the pet at x = 74 asks for 115 and keeps one row.
// The "an emote is never lost" promise this module makes was false for exactly
// the alert that says feed me / clean me / I am ill.
//
// So "does not fit" now means "does not fit CLEAR OF THE HUD", against the same
// UI_HUD_L_END / UI_HUD_R_BEGIN the stage itself is built from (config.h). An
// emote that cannot have the side it asked for is REFLECTED to the other side of
// the body - mirrored about the silhouette, so it keeps its distance from the
// pet. If neither side is clear we keep whichever ends up LESS covered, clamped
// onto the panel. Half an emote under a badge is still a message; none is not.
static uint8_t emote_hud_overlap(int16_t x, uint8_t ew) {
  const int16_t x1 = (int16_t)(x + (int16_t)ew - 1);
  int16_t n = 0;
  if (x < (int16_t)UI_HUD_L_END) {
    const int16_t e = (x1 < (int16_t)(UI_HUD_L_END - 1)) ? x1 : (int16_t)(UI_HUD_L_END - 1);
    n = (int16_t)(n + (e - x + 1));
  }
  if (x1 >= (int16_t)UI_HUD_R_BEGIN) {
    const int16_t s = (x > (int16_t)UI_HUD_R_BEGIN) ? x : (int16_t)UI_HUD_R_BEGIN;
    n = (int16_t)(n + (x1 - s + 1));
  }
  return (uint8_t)((n < 0) ? 0 : n);
}

static int16_t emote_x(int16_t want, int16_t body_x, int16_t body_w, uint8_t ew) {
  const int16_t last = (int16_t)(OLED_W - (int16_t)ew);
  if (last <= 0) return 0;                    // emote wider than the panel
  // Reflect the span about the body box: u -> body_x + (body_x + body_w - 1) - u.
  const int16_t mir = (int16_t)(2 * body_x + body_w - want - (int16_t)ew);
  const int16_t a   = (want < 0) ? (int16_t)0 : ((want > last) ? last : want);
  const int16_t b   = (mir  < 0) ? (int16_t)0 : ((mir  > last) ? last : mir);
  const uint8_t oa  = emote_hud_overlap(a, ew);
  const uint8_t ob  = emote_hud_overlap(b, ew);
  if (a == want && oa == 0u) return a;        // the side it asked for, and clear
  if (b == mir  && ob == 0u) return b;        // the other side of the body, clear
  return (ob < oa) ? b : a;                   // neither: keep the less covered
}

// The y axis needs the SAME guard as the x axis and for the same reason:
// px_spr() drops a sprite whose origin leaves the panel, and y < 0 is dropped
// exactly like x < 0. The mimo hearts rise 22 px from y + 6, an adult rests at
// y = 12, so the origin went negative 1145 ms into the 1400 ms animation and
// all three hearts vanished before reaching their apex - on every adult, every
// time. Clamped into the sprite band they stop at row 9 instead.
//
// BE HONEST ABOUT WHAT ROW 9 IS. It is not a ceiling above the pet, it is the
// pet's HEAD: an adult's ink starts there. That clamp only stopped being a
// deletion once the blit below stopped being opaque. While the emotes went out
// in setBitmapMode(0) - drawXBM paints the zeros too - every clamped heart
// stamped its whole 12x10 box across the face and took 61 px off ADULT_BUHO,
// 54 off the widest ADULT body and 46 off the next, on every caress, at every x.
// That traded "the emote disappears" for "the actor is erased", which is the
// one swap the scene contract in draw_home() exists to forbid.
//
// So emotes blit TRANSPARENT, exactly like petfx_draw_body(): only their lit
// pixels land, and the mode is handed back to render.cpp's 0 on the way out.
// Three hearts drifting over the top of the head now OR with it - visibly
// crowded, which is the honest reading of "the pet is bigger than the band" -
// instead of punching three rectangular holes in the animal.
//
// EVERY emote goes through here, EMO_ZZZ included: it was the only one that
// skipped emote_x(), and a 16 px wide ZZZ hung off a 40 px sleeper at the right
// wall lost 10 of its 16 columns to u8g2's clipping - all night, because a
// sleeping pet stays where it fell asleep. One helper, both axes, no exceptions;
// the next emote added gets the guard for free instead of rediscovering this.
static void emote_spr(int16_t want_x, int16_t want_y, int16_t body_x,
                      int16_t body_w, const SpriteRef& e) {
  const int16_t lo = (int16_t)SPRITE_AREA_Y;
  const int16_t hi = (int16_t)(RD_AFFORD_Y - (int16_t)e.h);
  int16_t ey = want_y;
  if (hi < lo)      ey = lo;          // emote taller than the band: top wins
  else if (ey < lo) ey = lo;
  else if (ey > hi) ey = hi;
  U8G2& u = rd_u8g2();
  u.setBitmapMode(1);
  px_spr(emote_x(want_x, body_x, body_w, e.w), ey, e);
  u.setBitmapMode(0);                 // hand the panel back as render.cpp set it
}

static void draw_pet_body(int16_t dy, int16_t dx, uint8_t frame) {
  const SimView* p = pet();
  if (!p) return;

  // petfx owns WHERE the body is (wandering, orientation, coat pattern, shadow)
  // and the sprite lookup that goes with it - including the STAGE_EGG case, and
  // including AUDIT 3's rule that sprite_form_of() is the only correct source of
  // `form`. ui keeps only what is genuinely UI: the device-only POSE_EAT
  // transient, the idle bob and the "nope" wiggle it passes in as dy, and every
  // emote below.
  petfx_draw_body(*p, home_pose(p), frame, dy, dx);

  // Emotes anchor to the LIVE body box. Hanging them off the old fixed centre
  // would make a wandering pet trail its own hearts across the screen.
  // petfx_body_y() is where the body ACTUALLY landed this frame - petfx.cpp
  // stores it AFTER adding dy and its own hop - so dy must NOT be added again
  // here, or every emote drifts a pixel and oscillates at twice the amplitude.
  const int16_t x = petfx_body_x();
  const int16_t y = petfx_body_y();
  const uint8_t w = petfx_body_w();

  if (p->flags & PF_ASLEEP) {
    const int16_t drift = (int16_t)((now_ms() / 300u) % 4u);
    emote_spr((int16_t)(x + w - 6), (int16_t)(y - 1 - drift),
              x, (int16_t)w, sprite_emote(EMO_ZZZ));
  }

  if (s_mimo_ms && since(s_mimo_ms) < UI_MIMO_MS) {
    const uint32_t mimo = since(s_mimo_ms);
    const SpriteRef h = sprite_emote(EMO_HEART);
    for (uint8_t i = 0; i < 3; ++i) {
      const uint32_t ph = mimo + (uint32_t)i * 260u;
      if (ph > UI_MIMO_MS) continue;
      const int16_t rise = (int16_t)((ph * 22u) / UI_MIMO_MS);
      emote_spr((int16_t)(x + 4 + i * 9 + (int16_t)((ph / 120u) & 1u)),
                (int16_t)(y + 6 - rise), x, (int16_t)w, h);
    }
  }

  if (s_evolve_ms && since(s_evolve_ms) < UI_EVOLVE_FREEZE_MS) {
    const SpriteRef sp = sprite_emote(EMO_SPARK);
    const uint32_t t = since(s_evolve_ms);
    for (uint8_t i = 0; i < 4; ++i) {
      const int16_t a = (int16_t)(((t / 90u) + (uint32_t)i * 3u) % 12u);
      emote_spr((int16_t)(x + ((i & 1u) ? (int16_t)(w + 1) : (int16_t)-9)),
                (int16_t)(y + 2 + a * 2), x, (int16_t)w, sp);
    }
  }

  // These three hang off the SIDES of the body, so on a pet standing at either
  // end of the stage the side they asked for is inside the HUD columns. The HUD
  // is genuinely in front - that part of the scene contract has not changed -
  // which is exactly why emote_x() now counts a destination under a badge as
  // "does not fit" and reflects the emote to the other side of the body rather
  // than letting the badge quietly eat it.
  const int16_t ey = (int16_t)(y - 10);
  if (sim_alert() != AL_NONE) {
    emote_spr((int16_t)(x - 8), (int16_t)(ey + (int16_t)((now_ms() / 250u) & 1u)),
              x, (int16_t)w, sprite_emote(EMO_EXCLAM));
  } else if (mood_of() >= MOOD_FELIZ && ((now_ms() / 1500u) & 1u)) {
    emote_spr((int16_t)(x + w + 1), ey, x, (int16_t)w, sprite_emote(EMO_NOTE));
  }

  // The action choreography's own emote layer: last of the pet layer, still
  // before the HUD badges. It needs no emote_x() reflection and cannot damage
  // the body, because every pixel it writes goes through one additive,
  // stage-clipped writer (actfx.cpp) - it ORs, it never clears and it never
  // blits an opaque box.
  //
  // CALL IT ON EVERY FRAME, including the ones where it draws nothing: it is
  // also where actfx samples the ink box petfx_draw_body() has just produced,
  // and actfx_draw_props() on the next frame is what consumes that sample.
  actfx_draw_over();
}

// Where the poops stand. Placed by hand to be reachable by the player, and now
// also handed to petfx as walls - see the obstacle feed in ui_service(). With
// the stage bounded to columns 14..113 the two outer ones (2 and 114) are off
// the stage entirely and can never be walked into; only 17 and 99 are real
// obstacles.
static const int16_t kPoopX[POOP_MAX] = { 2, 114, 17, 99 };

// Drawn BEFORE the body and plainly, with no halo, which is where it started.
//
// The round in between drew it AFTER the body inside a 14x13 opaque halo, to
// stop the two solid silhouettes fusing into one blob (the pet grows a hump,
// and the coat dither then hatches the poop as if it were fur). Swept over
// every reachable x, that halo ERASED body ink in 52 to 58 of the ~95 legal
// positions with four poops on screen, and took 141 pixels - both legs, in one
// clean horizontal slice - off an ADULT_BOLOTA at the left wall. Deleting the
// actor to keep a prop tidy is the wrong trade, every time.
//
// The fusion it was covering for is real, and it is now fixed where it belongs:
// petfx treats a poop as a wall, so the pet walks up to it, stops and turns
// round instead of standing inside it. Behaviour, not draw order.
static void draw_poop(void) {
  const SimView* p = pet();
  if (!p || p->poop_count == 0) return;
  const uint8_t n = (p->poop_count > POOP_MAX) ? (uint8_t)POOP_MAX : p->poop_count;
  const int16_t py = (int16_t)(RD_AFFORD_Y - 12);        // icon rows 44..55
  for (uint8_t i = 0; i < n; ++i) px_spr(kPoopX[i], py, sprite_icon(ICO_POOP));
}

// The absence line, with the EXACT elapsed time. The precision is the joke.
static void draw_absence_banner(void) {
  if (!s_absence_ms || since(s_absence_ms) > 5000UL) return;
  char t[GT_ELAPSED_BUF], line[96];
  gt_format_elapsed(s_absence_s, t, sizeof(t));
  const FmtArg fa[1] = { { 't', t } };
  fmt_apply(line, sizeof(line),
            S(s_absence_known ? STR_ABS_AWAY : STR_ABS_UNKNOWN), fa, 1);
  U8G2& u = rd_u8g2();
  px_box(0, SPRITE_AREA_Y, OLED_W, 28);
  u.setDrawColor(0);
  rd_text_wrap(3, (int16_t)(SPRITE_AREA_Y + 9), OLED_W - 6, RD_LINE_BODY, 3,
               RD_FONT_BODY, line);
  u.setDrawColor(1);
  px_frame(0, SPRITE_AREA_Y, OLED_W, 28);
}

// The animated stage layer HOME asks for through home_bind_body(). Everything
// in it reaches the panel through render.h - petfx's wandering automaton, the
// action choreography, the emotes, the welcome-back banner - which is exactly
// why it cannot live in the pure screen and is bound instead.
//
// Returns false when there is no pet at all, so HOME draws its own empty
// frame rather than a floor with nothing standing on it.
static bool home_body(uint8_t frame) {
  const SimView* p = pet();
  if (!p) return false;

  static const int8_t kBob[8] = { 0, 0, 1, 1, 0, 0, -1, -1 };

  // ONE OWNER OF THE BODY'S OFFSET AT A TIME.
  //
  // While a choreography is running it owns both axes outright, and this used
  // to ADD its dy to the idle bob instead. That looked conservative and was the
  // single reason the feature did not work: the bob is +/-1 px on a 220 ms
  // cycle that never stops, the accent of a bite is 1 px, and a signal summed
  // with noise of its own size is noise. Measured frame by frame on the real
  // framebuffer, a pet eating a meal was indistinguishable from a pet standing
  // still. The film also carries the whole of ACT_SLEEP_TOGGLE, which plays on
  // a pet whose PF_ASLEEP is ALREADY set - so it has to survive the dy = 0
  // below - and it must not be shaken by the "nope" wiggle either.
  int16_t dy = 0;
  int16_t dx = 0;
  if (actfx_owns_body()) {
    dy = actfx_body_dy();
    dx = actfx_body_dx();          // the bite: a lunge at the bowl. See actfx.h.
  } else {
    dy = kBob[(now_ms() / 220u) & 7u];
    if (p->flags & PF_ASLEEP) dy = 0;
    if (s_wiggle_ms && since(s_wiggle_ms) < UI_WIGGLE_MS)
      dy = (int16_t)(((now_ms() / 60u) & 1u) ? 1 : -1);
  }

  // =========================================================================
  //  THE COMPOSITION ORDER OF THE HOME SCREEN. It is a contract, not a taste.
  //
  //      floor -> poops -> ACTION PROPS -> BODY -> EMOTES -> ACTION EMOTES ->
  //      HUD badge -> banner / toast -> affordance strip
  //
  //  The actor lives on the STAGE, columns PETFX_STAGE_L..PETFX_STAGE_R
  //  (petfx.h); the HUD lives in the margins outside it, at the columns config.h
  //  names UI_HUD_L_END / UI_HUD_R_BEGIN. They never share a pixel, which is the
  //  only way a 1-bit framebuffer can hold both "the badge is opaque and always
  //  legible" and "the body is never damaged" at once. petfx.h static_asserts
  //  the two against each other so that stays true by construction.
  //
  //  WHAT EACH LAYER PROMISES, because "draw order" alone does not say it:
  //   * the BODY is opaque against the scenery and transparent against itself.
  //     petfx_draw_body() clears its own INK BOX with colour 0 and then blits
  //     into the hole, so nothing behind it can be seen through the eye
  //     sockets, the mouth or the gap between the legs. The clear stops at
  //     PETFX_FLOOR_Y - 1, so the floor line and both shadow rows survive it;
  //     it does erase whatever poop is directly under the body, which is the
  //     correct reading now that the pet treats a poop as a wall and only ever
  //     passes in front of one.
  //   * EMOTES are transparent and go AFTER the body on purpose - they belong
  //     to the pet, not to the HUD - so a heart that the band clamp pushes onto
  //     the head lands on it instead of through it. emote_x() also keeps them
  //     out of the HUD columns, because a badge drawn on top of an emote and a
  //     panel edge delete it equally well.
  //   * the HUD BADGE is opaque and last of the pet layer. Nothing of the
  //     actor can reach its columns, so there is nothing of it to stamp over.
  //   * the ACTION layers (actfx.cpp) slot into the two places that already
  //     exist rather than adding a third. Its GROUND props - the bowl, the
  //     bubbles, the ball, the crumbs, the poops ACT_CLEAN is dissolving - go
  //     in the poop slot, so the animal passes in front of its own dinner and
  //     the ink-box clear erases whatever ends up underneath it. Its EMOTES -
  //     the pill, the germ, the hearts, the sparks - go in the emote slot, and
  //     are additive and stage-clipped by construction, so they can damage
  //     neither the body they sit on nor the badges that come after them.
  //
  //  Do NOT reorder this to fix a local artefact. Every pairwise fix that was
  //  tried - badges first so the pet passes in front, poops last inside an
  //  opaque halo - simply traded one destroyed thing for another, and those two
  //  are what this contract replaces. If two things fight over pixels, move one
  //  of them out of the other's pixels; do not re-shuffle the stack.
  // =========================================================================

  // The floor goes down before anything stands on it: poop and body both sit
  // ON the ground line, and drawing it afterwards would cut through them.
  petfx_draw_floor();
  draw_poop();
  actfx_draw_props();
  draw_pet_body(dy, dx, frame);

  // The mood badge that used to stand in the right-hand HUD column is gone:
  // the column now carries HOME's three meters (ui/screen_home.cpp), and the
  // mood score itself is on the PEBBLE page where the rest of the numbers are.
  // The keep-out contract is unchanged, so an emote still cannot be eaten by
  // whatever occupies those columns.
  draw_absence_banner();
  return true;
}

// Leaving HOME by ANY route ends the film and, far more importantly, releases
// the petfx_hold() it took. Bound as HOME's leave hook.
static void home_leave_layer(void) { actfx_cancel(); }

// =============================================================================
//  9-10. MENU, CARE and PLAY moved to ui/screen_menu.cpp and ui/screen_care.cpp
//        (P2-C11b). What the lists still need from this file - starting a
//        minigame, opening a confirmation, replaying the last action - is
//        reached through the ui.h seams in section 19.
// =============================================================================

static void game_start(uint8_t dev_id);

// =============================================================================
//  11. GAME - the three on-device 2-button minigames
//
//  Canonical for minigames_won (BRIEF 1.6): every result goes through
//  sim_apply_play_result(), which owns the shared cooldown and hourly ledger,
//  so no surface can be farmed.
//
//  INPUT: the recogniser only classifies a TAP after DOUBLE_TAP_WINDOW_MS
//  (280 ms), which is fatal for a reflex game. The games therefore read
//  debounced RAW edges (~25 ms) and IGNORE the GST_TAP_*/GST_DBL_* that arrive
//  later, so nothing is ever counted twice. GST_HOLD_R (pause) and
//  GST_LONG_BOTH (force quit) still arrive as gestures, exactly as 8.3 wants.
// =============================================================================

static void game_start(uint8_t dev_id) {
  memset(&s_g, 0, sizeof(s_g));
  s_g.id      = (dev_id < DG_COUNT) ? dev_id : (uint8_t)DG_REFLEX;
  s_g.t0      = now_ms();
  s_g.step_ms = s_g.t0;
  s_g.seq_len = 3;
  for (uint8_t i = 0; i < INPUT_BTN_N; ++i) s_raw_prev[i] = input_raw(i) ? 1u : 0u;
  nav_push(SCR_GAME);
}

static void game_finish(void) {
  ActionResult r;
  const uint16_t permille = (s_g.score > 1000u) ? 1000u : s_g.score;
  sim_apply_play_result(permille, r);
  s_last_action = ACT_PLAY;
  const SimView* p = pet();
  if (p) gs_save_active(true);
  s_g.phase = 2;
  s_g.t0    = now_ms();
  ui_toast(permille >= 500u ? STR_GM_WIN : STR_GM_LOSE);
}

// ---- REFLEX -----------------------------------------------------------------
static void reflex_arm(void) {
  s_g.target = (uint8_t)(rng_u32(RNG_MINIGAME) & 1u);
  s_g.t0     = now_ms();
  s_g.arm_ms = 700UL + rng_below(RNG_MINIGAME, 1500u);   // dead time before the light
}

static void reflex_step(void) {
  if (since(s_g.t0) > s_g.arm_ms + REFLEX_WINDOW_MS) {
    s_g.last_ms = 0;                                 // missed the window
    if (++s_g.round >= REFLEX_ROUNDS) { game_finish(); return; }
    reflex_arm();
  }
}

static void reflex_press(uint8_t side) {
  const uint32_t el = since(s_g.t0);
  if (el < s_g.arm_ms) {
    ui_toast(STR_GM_TOOSOON);
    s_g.last_ms = 0;
  } else {
    const uint32_t rt = el - s_g.arm_ms;
    if (side == s_g.target && rt <= REFLEX_WINDOW_MS) {
      s_g.last_ms = (uint16_t)rt;
      s_g.score = (uint16_t)(s_g.score +
                  (REFLEX_WINDOW_MS - rt) * 200UL / REFLEX_WINDOW_MS);  // 5 x 200
    } else {
      s_g.last_ms = 0;
    }
  }
  if (++s_g.round >= REFLEX_ROUNDS) { game_finish(); return; }
  reflex_arm();
}

static void reflex_draw(void) {
  U8G2& u = rd_u8g2();
  const uint32_t el = since(s_g.t0);
  const bool lit = (el >= s_g.arm_ms) && (el <= s_g.arm_ms + REFLEX_WINDOW_MS);
  if (lit) {
    const int16_t x = s_g.target ? (int16_t)(OLED_W / 2) : (int16_t)0;
    px_box(x, UI_CONTENT_Y, OLED_W / 2, 32);
    u.setDrawColor(0);
    px_spr((int16_t)(x + OLED_W / 4 - 4), (int16_t)(UI_CONTENT_Y + 12),
           sprite_mini(s_g.target ? MIC_ARROW_R : MIC_ARROW_L));
    u.setDrawColor(1);
  } else {
    rd_text_center((int16_t)(UI_CONTENT_Y + 20), RD_FONT_HEAD, S(STR_GM_READY));
  }
  char buf[16];
  if (s_g.last_ms) snprintf(buf, sizeof(buf), "%u ms", (unsigned)s_g.last_ms);
  else             snprintf(buf, sizeof(buf), "--");
  rd_text_center(UI_CONTENT_BOTTOM, RD_FONT_TINY, buf);
}

// ---- MEMORY -----------------------------------------------------------------
static void memory_new_round(void) {
  s_g.seq_len = (uint8_t)(3u + s_g.round);
  if (s_g.seq_len > 8u) s_g.seq_len = 8u;
  for (uint8_t i = 0; i < s_g.seq_len; ++i) s_g.seq[i] = (uint8_t)(rng_u32(RNG_MINIGAME) & 1u);
  s_g.play_idx = 0;
  s_g.seq_pos  = 0;
  s_g.t0       = now_ms();
}

static void memory_step(void) {
  if (s_g.play_idx >= s_g.seq_len) return;
  const uint32_t slot = MEMORY_FLASH_MS + MEMORY_GAP_MS;
  const uint32_t idx  = since(s_g.t0) / slot;
  s_g.play_idx = (idx > s_g.seq_len) ? s_g.seq_len : (uint8_t)idx;
}

static void memory_press(uint8_t side) {
  if (s_g.play_idx < s_g.seq_len) return;             // still showing the sequence
  if (side != s_g.seq[s_g.seq_pos]) {
    s_g.score = (uint16_t)((uint32_t)s_g.round * 1000UL / MEMORY_LEVELS);
    game_finish();
    return;
  }
  if (++s_g.seq_pos >= s_g.seq_len) {
    if (++s_g.round >= MEMORY_LEVELS) { s_g.score = 1000; game_finish(); return; }
    memory_new_round();
  }
}

static void memory_draw(void) {
  U8G2& u = rd_u8g2();
  const uint32_t slot = MEMORY_FLASH_MS + MEMORY_GAP_MS;
  const bool showing  = (s_g.play_idx < s_g.seq_len);
  int8_t lit = -1;
  if (showing && (since(s_g.t0) % slot) < MEMORY_FLASH_MS)
    lit = (int8_t)s_g.seq[s_g.play_idx];

  for (uint8_t side = 0; side < 2; ++side) {
    const int16_t x = (int16_t)(side ? 66 : 2);
    if (lit == (int8_t)side) px_box(x, (int16_t)(UI_CONTENT_Y + 2), 60, 24);
    else                     px_frame(x, (int16_t)(UI_CONTENT_Y + 2), 60, 24);
    u.setDrawColor(lit == (int8_t)side ? 0 : 1);
    px_spr((int16_t)(x + 26), (int16_t)(UI_CONTENT_Y + 10),
           sprite_mini(side ? MIC_ARROW_R : MIC_ARROW_L));
    u.setDrawColor(1);
  }

  for (uint8_t i = 0; i < s_g.seq_len; ++i) {
    const int16_t x = (int16_t)(OLED_W / 2 - s_g.seq_len * 3 + i * 6);
    if (i < s_g.seq_pos) px_box(x, (int16_t)(UI_CONTENT_Y + 30), 5, 4);
    else                 px_frame(x, (int16_t)(UI_CONTENT_Y + 30), 5, 4);
  }

  char buf[24];
  snprintf(buf, sizeof(buf), "%s %u/%u", S(STR_GM_ROUND),
           (unsigned)(s_g.round + 1u), (unsigned)MEMORY_LEVELS);
  rd_text_center(UI_CONTENT_BOTTOM, RD_FONT_BODY, buf);
}

// ---- JUMP -------------------------------------------------------------------
static void jump_spawn(void) {
  if (s_g.obs_n >= 3) return;
  int16_t x = (int16_t)(OLED_W + (int16_t)rng_below(RNG_MINIGAME, 40u));
  for (uint8_t i = 0; i < s_g.obs_n; ++i)
    if (x - s_g.obs_x[i] < 36) x = (int16_t)(s_g.obs_x[i] + 36);
  s_g.obs_x[s_g.obs_n++] = x;
}

static void jump_press(void) { if (s_g.jump_ms == 0) s_g.jump_ms = now_ms(); }

static void jump_step(void) {
  if (s_g.jump_ms) {
    const uint32_t t = since(s_g.jump_ms);
    if (t >= JUMP_RISE_MS) { s_g.jump_ms = 0; s_g.pet_dy = 0; }
    else {
      const int32_t half = (int32_t)(JUMP_RISE_MS / 2u);
      const int32_t d    = (int32_t)t - half;
      s_g.pet_dy = (int16_t)(-(JUMP_HEIGHT - (d * d * JUMP_HEIGHT) / (half * half)));
    }
  }

  for (uint8_t i = 0; i < s_g.obs_n; ) {
    s_g.obs_x[i] = (int16_t)(s_g.obs_x[i] - 3);
    if (s_g.obs_x[i] < 30 && s_g.obs_x[i] + 6 > 14 && s_g.pet_dy > -9) {
      s_g.score = (uint16_t)((uint32_t)s_g.passed * 1000UL / JUMP_TARGET);
      game_finish();
      return;
    }
    if (s_g.obs_x[i] < -6) {
      ++s_g.passed;
      for (uint8_t k = (uint8_t)(i + 1u); k < s_g.obs_n; ++k) s_g.obs_x[k - 1] = s_g.obs_x[k];
      --s_g.obs_n;
      if (s_g.passed >= JUMP_TARGET) { s_g.score = 1000; game_finish(); return; }
      continue;
    }
    ++i;
  }
  if (s_g.obs_n == 0 || s_g.obs_x[s_g.obs_n - 1] < 72) jump_spawn();
}

static void jump_draw(void) {
  px_hline(0, JUMP_GROUND_Y + 2, OLED_W);
  const SimView* p = pet();
  const uint8_t fr = (uint8_t)((now_ms() / 150u) & 1u);
  SpriteRef r = sprite_frame(SPR_BABY_BLOB, fr);
  if (p) {
    const Stage st = (Stage)((p->stage == STAGE_EGG) ? (uint8_t)STAGE_BABY
                                                     : p->stage);
    r = sprite_lookup_pose(gene_species(p->genome), (uint8_t)st,
                           sprite_form_of(p->genome, p->minor_form, st), POSE_IDLE, fr);
  }
  int16_t py = (int16_t)(JUMP_GROUND_Y + 2 - (int16_t)r.h + s_g.pet_dy);
  if (py < UI_CONTENT_Y) py = UI_CONTENT_Y;
  px_spr(14, py, r);

  for (uint8_t i = 0; i < s_g.obs_n; ++i)
    px_box(s_g.obs_x[i], (int16_t)(JUMP_GROUND_Y - 8), 6, 10);

  char buf[16];
  snprintf(buf, sizeof(buf), "%u/%u", (unsigned)s_g.passed, (unsigned)JUMP_TARGET);
  rd_text_right(OLED_W - 2, (int16_t)(UI_CONTENT_Y + 6), RD_FONT_TINY, buf);
}

// ---- shared -----------------------------------------------------------------
static void game_press(uint8_t side) {
  if (s_g.phase != 1) return;
  switch (s_g.id) {
    case DG_REFLEX: reflex_press(side); break;
    case DG_MEMORY: memory_press(side); break;
    default:        jump_press();       break;
  }
}

static void game_service(void) {
  const uint32_t t = now_ms();

  if (s_g.phase == 0) {
    if (since(s_g.t0) >= GAME_INTRO_MS) {
      s_g.phase = 1;
      s_g.round = 0;
      s_g.score = 0;
      if      (s_g.id == DG_REFLEX) reflex_arm();
      else if (s_g.id == DG_MEMORY) memory_new_round();
      else { s_g.t0 = t; s_g.step_ms = t; jump_spawn(); }
    }
    return;
  }
  if (s_g.phase == 2) {
    if (since(s_g.t0) >= GAME_RESULT_MS) nav_back();
    return;
  }

  for (uint8_t b = 0; b < 2; ++b) {
    const uint8_t down = input_raw(b) ? 1u : 0u;
    const uint8_t prev = s_raw_prev[b];
    s_raw_prev[b] = down;
    if (down && !prev) game_press(b);
    if (s_g.phase != 1) return;
  }

  // Fixed-step physics: the speed must not track the frame rate.
  while ((uint32_t)(t - s_g.step_ms) >= GAME_STEP_MS) {
    s_g.step_ms += GAME_STEP_MS;
    switch (s_g.id) {
      case DG_REFLEX: reflex_step(); break;
      case DG_MEMORY: memory_step(); break;
      default:        jump_step();   break;
    }
    if (s_g.phase != 1) return;
  }
}

static void draw_game(void) {
  const uint16_t title = (s_g.id == DG_REFLEX) ? STR_DG_REFLEX
                       : (s_g.id == DG_MEMORY) ? STR_DG_MEMORY : STR_DG_JUMP;
  const uint16_t sc = (s_g.score > 1000u) ? 1000u : s_g.score;
  char tag[12];
  snprintf(tag, sizeof(tag), "%u", (unsigned)(sc / 10u));
  draw_header(S(title), (s_g.phase == 1) ? tag : nullptr);

  if (s_g.phase == 0) {
    const bool go = since(s_g.t0) >= 1000UL;
    rd_text_center((int16_t)(UI_CONTENT_Y + 16), RD_FONT_HEAD,
                   S(go ? STR_GM_GO : STR_GM_READY));
    const uint16_t hint = (s_g.id == DG_REFLEX) ? STR_DG_REFLEX_HINT
                        : (s_g.id == DG_MEMORY) ? STR_DG_MEMORY_HINT : STR_DG_JUMP_HINT;
    rd_text_center((int16_t)(UI_CONTENT_Y + 32), RD_FONT_BODY, S(hint));
  } else if (s_g.phase == 2) {
    rd_text_center((int16_t)(UI_CONTENT_Y + 16), RD_FONT_HEAD,
                   S(sc >= 500u ? STR_GM_WIN : STR_GM_LOSE));
    char buf[24];
    snprintf(buf, sizeof(buf), "%s %u", S(STR_GM_SCORE), (unsigned)(sc / 10u));
    rd_text_center((int16_t)(UI_CONTENT_Y + 32), RD_FONT_BODY, buf);
  } else {
    switch (s_g.id) {
      case DG_REFLEX: reflex_draw(); break;
      case DG_MEMORY: memory_draw(); break;
      default:        jump_draw();   break;
    }
  }
  rd_affordance(S(STR_AF_PAUSE), nullptr);
}

// GAME_DESIGN 8.3: "games must not have hidden gestures."
static void handle_game(Gesture g) {
  if (g != GST_HOLD_R) return;
  if (s_g.phase == 1) confirm_open(CFM_QUIT_GAME, STR_CF_QUIT_GAME);
  else                nav_back();
}

// =============================================================================
//  12. STATUS_A / STATUS_B moved to ui/screen_status.cpp (P2-C11b). The god
//      entry bar the genome page paints is reported through ui_god_progress().
// =============================================================================

// =============================================================================
//  13. SOCIAL - connectionless BLE discovery
//      Discovery only: the screen owns the radio (request on enter, release on
//      leave) and lists the pets it can hear. Everything a user can DO with a
//      peer - trade, battle, breed - is the Phase 7 LINK screen; until then
//      this screen says so.
// =============================================================================

// How many peer rows fit under the placeholder line.
#define SOC_LIST_ROWS   3
#define SOC_LIST_Y      (UI_CONTENT_Y + 7)

// Bring the BLE stack up and start advertising. net_request() no longer blocks
// (net.cpp replaced delay(RADIO_SETTLE_MS) by the NPH_SETTLING phase), so the
// stack may still be settling when it returns true: stay in SOC_ENTER and let
// social_service() call this again on the next pump until net_mode() agrees.
static void social_try_bringup(void) {
#if FEATURE_BLE
  if (net_ble_sessions_left() == 0) { s_soc_phase = SOC_ERROR; s_soc_err = STR_SO_CAP; return; }
  if (!net_request(RADIO_BLE)) {
    s_soc_phase = SOC_ERROR;
    s_soc_err   = (net_last_err() == NERR_BLE_SESSION_CAP)
                    ? (uint16_t)STR_SO_CAP : (uint16_t)net_last_err_str();
    return;
  }
  if (net_mode() != RADIO_BLE) {
    return;                       // settling; try again next pump
  }
  if (!ble_begin()) {
    s_soc_phase = SOC_ERROR;
    s_soc_err   = (ble_session_count() >= BLE_SESSION_CAP) ? (uint16_t)STR_SO_CAP
                                                           : (uint16_t)STR_ERR_BUSY;
    return;
  }
  const SimView* p = pet();
  if (p) ble_advertise_beacon(p->genome, p->stage, (uint8_t)(p->cq >> 2));
  s_soc_phase = SOC_SCAN;
#else
  s_soc_phase = SOC_ERROR;
  s_soc_err   = STR_ERR_BUSY;
#endif
}

static void social_enter(void) {
  s_soc_phase          = SOC_ENTER;
  s_soc_err            = STR_EMPTY;
  s_soc_cursor = 0;
  s_soc_prev           = (uint8_t)net_mode();

  social_try_bringup();
}

static void social_leave(void) {
#if FEATURE_BLE
  if (ble_is_up()) ble_end();
  // The radio is screen-owned (plan section 2 row G4). SOCIAL took the stack, so
  // SOCIAL gives it back - to whatever was resident on the way in, which with no
  // policy left in the entry point is RADIO_OFF unless QR is below us.
  if (net_mode() == RADIO_BLE) net_request((RadioMode)s_soc_prev);
#endif
  s_soc_phase = SOC_ENTER;
}

static void social_service(void) {
#if FEATURE_BLE
  if (s_soc_phase == SOC_ERROR) return;
  if (s_soc_phase == SOC_ENTER && !ble_is_up()) { social_try_bringup(); return; }
  if (!ble_is_up()) return;

  const SimView* p = pet();
  uint8_t self = BLE_SELF_SEEKING;
  if (p && (p->flags & PF_GOD_TAINTED)) self = (uint8_t)(self | BLE_SELF_GOD);
  ble_set_self(self);
  ble_scan_service();
#endif
}

static void draw_social(void) {
  char tag[10];
#if FEATURE_BLE
  snprintf(tag, sizeof(tag), "%u", (unsigned)ble_peer_count());
#else
  tag[0] = '\0';
#endif
  draw_header(S(STR_SO_TITLE), tag);

  if (s_soc_phase == SOC_ERROR) {
    rd_text_wrap(3, (int16_t)(UI_CONTENT_Y + 10), OLED_W - 6, RD_LINE_BODY, 4,
                 RD_FONT_BODY, S(s_soc_err ? s_soc_err : (uint16_t)STR_ERR_BUSY));
    draw_countdown();
    rd_affordance(nullptr, nullptr);
    return;
  }

  // The screen is honest about what it is: a radio that finds neighbours and
  // nothing else yet.
  rd_text_center((int16_t)(UI_CONTENT_Y + 5), RD_FONT_TINY, S(STR_SO_LINK_SOON));

#if FEATURE_BLE
  U8G2& u = rd_u8g2();
  const uint8_t n = ble_peer_count();
  if (n == 0) {
    rd_text_center((int16_t)(UI_CONTENT_Y + 18), RD_FONT_NARR, S(STR_SO_SEARCHING));
    const uint8_t dots = (uint8_t)((now_ms() / 400u) % 4u);
    for (uint8_t i = 0; i < dots; ++i)
      px_box((int16_t)(OLED_W / 2 - 8 + i * 6), (int16_t)(UI_CONTENT_Y + 24), 4, 4);
    rd_text_wrap(3, (int16_t)(UI_CONTENT_Y + 38), OLED_W - 6, RD_LINE_BODY, 2,
                 RD_FONT_BODY, S(STR_SO_NOBODY));
  } else {
    if (s_soc_cursor >= n) s_soc_cursor = 0;
    const uint8_t rows = (n < SOC_LIST_ROWS) ? n : (uint8_t)SOC_LIST_ROWS;
    for (uint8_t i = 0; i < rows; ++i) {
      const BlePeerInfo* pi = ble_peer(i);
      if (!pi) continue;
      const int16_t y   = (int16_t)(SOC_LIST_Y + i * UI_LIST_PITCH);
      const bool    sel = (i == s_soc_cursor);
      if (sel) px_box(0, y, OLED_W, UI_LIST_PITCH - 1);
      u.setDrawColor(sel ? 0 : 1);
      char nm[16];
      ui_name_for(pi->genome.lineage_id, pi->genome.generation, nm, sizeof(nm));
      rd_text_fit(3, (int16_t)(y + 8), 58, RD_FONT_NARR, nm);
      rd_text_fit(64, (int16_t)(y + 8), 48, RD_FONT_BODY,
                  (pi->stage < STAGE_COUNT) ? S_STAGE(pi->stage) : "-");
      for (uint8_t b = 0; b < 3; ++b) {          // signal strength, three bars
        const int16_t bx = (int16_t)(OLED_W - 12 + b * 4);
        const int8_t  th = (int8_t)(-50 - b * 10);
        if (pi->rssi >= th) px_box(bx, (int16_t)(y + 6 - b * 2), 3, (int16_t)(2 + b * 2));
        else                px_hline(bx, (int16_t)(y + 7), 3);
      }
      u.setDrawColor(1);
    }
  }
#endif
  draw_countdown();
  rd_affordance(S(STR_AF_NEXT), nullptr);
}

static void handle_social(Gesture g) {
#if FEATURE_BLE
  const uint8_t n = ble_peer_count();
#else
  const uint8_t n = 0;
#endif
  switch (g) {
    case GST_TAP_L:
    case GST_HOLD_L: s_soc_cursor = ring_next(s_soc_cursor, n); break;
    case GST_DBL_L:  s_soc_cursor = 0; break;
    case GST_DBL_R:  if (n) s_soc_cursor = (uint8_t)(n - 1u); break;
    case GST_BOTH:   help_open(STR_HLP_PEER); break;
    default: break;
  }
}

// =============================================================================
//  14. SETTINGS moved to ui/screen_settings.cpp (P2-C11b). The five lines of
//      its "Acerca de" page are device facts, so they are still produced here
//      and handed over by ui_info_lines().
// =============================================================================

// =============================================================================
//  15. QR
//      BRIEF 1.4 geometry: a 62 px white box at (0,1) with a 3-module quiet
//      zone. That box owns the rows the affordance strip would use, so the
//      hint lives in the right-hand column instead.
// =============================================================================
static void qr_build(void) {
  char text[QR_TEXT_MAX];
  text[0] = '\0';

  if (s_qr_variant == 1 && net_is_ap_up()) {
    // Open network. "WIFI:S:<ssid>;;" is 26 B and fits QR version 2 (25
    // modules -> 62 px at 2 px/module). Adding "T:nopass" makes it 35 B, which
    // forces version 3 -> 29 modules -> 70 px, and 70 does not fit in 64 rows.
    snprintf(text, sizeof(text), "WIFI:S:%s;;", net_ap_ssid());
  } else if (net_url(text, sizeof(text), web_pin()) == 0) {
    text[0] = '\0';
  }

  if (text[0] == '\0') { s_qr_size = 0; s_qr_key[0] = '\0'; return; }
  if (strcmp(text, s_qr_key) == 0) return;                 // already cached

  uint8_t size = 0;
  if (qr_encode(text, s_qr_mod, size)) {
    s_qr_size = size;
    snprintf(s_qr_key, sizeof(s_qr_key), "%s", text);
  } else {
    s_qr_size   = 0;
    s_qr_key[0] = '\0';
  }
}

static void draw_qr(void) {
  U8G2& u = rd_u8g2();

  if (s_qr_size) {
    uint8_t px = (uint8_t)(QR_BOX_SIZE / (s_qr_size + 2 * QR_QUIET_MODULES));
    if (px == 0) px = 1;
    if (px > 3)  px = 3;
    qr_draw(u, QR_BOX_X, QR_BOX_Y, px);
  } else {
    px_frame(QR_BOX_X, QR_BOX_Y, QR_BOX_SIZE, QR_BOX_SIZE);
    rd_text_center_in(QR_BOX_X, QR_BOX_SIZE, 34, RD_FONT_BODY, "...");
  }

  const int16_t rx = QR_BOX_SIZE + 3;        // 65
  const int16_t rw = OLED_W - rx - 1;        // 62

  if (net_is_ap_up()) {
    rd_text_fit(rx, 8, rw, RD_FONT_BODY, S(STR_WEB_NOWIFI));
    rd_text_wrap(rx, 17, rw, RD_LINE_BODY, 2, RD_FONT_BODY, S(STR_WEB_AP_HINT));
    rd_text_fit(rx, 33, rw, RD_FONT_TINY, net_ap_ssid());
    rd_text_fit(rx, 41, rw, RD_FONT_TINY, net_ip());   // softAP address, not a literal
    // The PIN used to be drawn only in the STA branch, while this screen tells
    // the user to open the address by hand - so a hand-typed URL hit the PIN
    // prompt with the PIN shown nowhere. The alternating URL symbol carries
    // ?k=NNNN, but only if you scan it. Show it here too.
    {
      char pin[12];
      snprintf(pin, sizeof(pin), "%s %04u",
               S(STR_WEB_PIN), (unsigned)(web_pin() % 10000u));
      rd_text_fit(rx, 51, rw, RD_FONT_BODY, pin);
    }
  } else if (net_is_sta_up()) {
    rd_text_fit(rx, 8, rw, RD_FONT_BODY, S(STR_WEB_TITLE));
    rd_text_fit(rx, 16, rw, RD_FONT_TINY, net_ip());
    rd_text(rx, 23, RD_FONT_TINY, S(STR_WEB_PIN));
    char pin[8];
    snprintf(pin, sizeof(pin), "%04u", (unsigned)(web_pin() % 10000u));
    rd_text(rx, 40, RD_FONT_BIGNUM, pin);    // 9x19 digits: unmissable at arm's length
  } else {
    rd_text_fit(rx, 8, rw, RD_FONT_BODY, S(STR_WEB_TITLE));
    rd_text_wrap(rx, 20, rw, RD_LINE_BODY, 3, RD_FONT_BODY, S(STR_WEB_CONNECTING));
  }

  // Invariant 6, relocated: the hint lives in the right column's bottom strip.
  px_spr(rx, RD_AFFORD_Y, sprite_mini(MIC_ARROW_L));
  rd_text_fit((int16_t)(rx + 10), RD_AFFORD_BASELINE, 40, RD_FONT_BODY, S(STR_AF_BACK));
  if (net_is_ap_up()) px_spr(OLED_W - 8, RD_AFFORD_Y, sprite_mini(MIC_ARROW_R));
}

static void handle_qr(Gesture g) {
  if ((g == GST_TAP_R || g == GST_TAP_L) && net_is_ap_up()) {
    s_qr_variant = (uint8_t)!s_qr_variant;
    s_qr_key[0]  = '\0';
    s_qr_manual  = now_ms();
    qr_build();
  }
}

// =============================================================================
//  15b. TIME ENTRY moved to ui/screen_time.cpp (P2-C11b). The clock itself is
//       still reached through gametime.h, from the ui_get_clock() /
//       ui_set_clock() seams in section 19.
// =============================================================================

// =============================================================================
//  16. THE HATCH CEREMONY
//
//  Birth used to be a toast. It is the only ceremony left, so it gets a real
//  one: one phase enum, one cumulative clock read from config.h, nothing
//  skippable, no chrome.
//
//  ORDERING IS THE WHOLE SAFETY ARGUMENT. sim_hatch() has ALREADY run and the
//  SimView has ALREADY been flushed to flash before the first frame below is
//  drawn, so every phase here is pure presentation. A brownout half way through
//  therefore reboots into ui_begin() -> STAGE_BABY -> HOME with a perfectly
//  ordinary baby: the player loses the show, never the pet. Deferring
//  sim_hatch() to the END of the ceremony would do the opposite - a reboot at
//  second 3 leaves a STAGE_EGG that the sim is simultaneously trying to hatch
//  on age, which is exactly the weird state to avoid.
// =============================================================================

// Idempotent. The manual rub path calls sim_hatch() itself and the sim then
// reports SIM_EV_HATCHED on the NEXT logic tick, so this is reached twice for
// one birth; the second call must do nothing. The s_hatch_ms window also covers
// an event that arrives after the ceremony has already finished.
static void hatch_begin(void) {
  const SimView* p = pet();
  if (!p || p->stage != STAGE_BABY) return;
  if (hatch_active()) return;
  if (s_hatch_ms != 0 && since(s_hatch_ms) < HATCH_TOTAL_MS + 3000UL) return;

  gs_save_active(true);   // commit FIRST: everything below is presentation

  // The phase is armed BEFORE the navigation: sm_replace_root() applies the
  // frame rate on arrival, and ui_fps() answers FPS_NORMAL only once
  // hatch_active() is true. It also runs the leave hook of whatever screen the
  // ceremony arrived on - which is what drops BLE or an in-flight minigame.
  s_hatch_phase = HP_WOBBLE;
  s_hatch_ms    = now_ms();
  s_hatch_jolts = 0;
  s_hatch_look  = 0;
  s_alert_n     = 0;
  s_alert_cur   = AL_NONE;
  s_toast[0]    = '\0';
  s_absence_ms  = 0;
  sm_replace_root(SCR_EGG);
  s_trans_ms    = 0;           // the ceremony owns the frame: no dissolve on top
  actfx_cancel();              // whatever the previous animal was doing, it is over
  petfx_reset(*p);             // the body about to appear is a brand new one
  petfx_freeze(1);             // and it must be born in the centre, not mid-walk
}

// Phase edges only. Every register effect is armed EXACTLY once here: doing it
// from the draw path would re-arm it on every frame and the shake would never
// decay.
static void hatch_service(void) {
  const uint32_t el = since(s_hatch_ms);

  switch (s_hatch_phase) {
    case HP_WOBBLE:
      if (el >= HATCH_T_CRACK) s_hatch_phase = HP_CRACK;
      break;

    case HP_CRACK: {
      // Three separate jolts, not one long decay: the shell gives way in steps.
      // A single decaying shake reads as a rumble; three read as a crack.
      const uint32_t ce   = (el > HATCH_T_CRACK) ? (el - HATCH_T_CRACK) : 0u;
      const uint8_t  want = (uint8_t)((ce / HATCH_JOLT_GAP_MS) + 1u);
      while (s_hatch_jolts < want && s_hatch_jolts < 3u) {
        ++s_hatch_jolts;
        rd_shake(HATCH_JOLT_PX, HATCH_JOLT_MS);
      }
      if (el >= HATCH_T_FLASH) { s_hatch_phase = HP_FLASH; rd_flash(HATCH_FLASH_MS); }
      break;
    }

    case HP_FLASH:
      if (el >= HATCH_T_SHARDS) s_hatch_phase = HP_SHARDS;
      break;

    case HP_SHARDS:
      if (el >= HATCH_T_GROW) s_hatch_phase = HP_GROW;
      break;

    case HP_GROW:
      if (el >= HATCH_T_LOOK) s_hatch_phase = HP_LOOK;
      break;

    case HP_LOOK:
      // petfx_freeze() stops LOCOMOTION, not the head. The first thing a newborn
      // does is check whether the world has anything in it.
      if (s_hatch_look == 0) {
        s_hatch_look = 1;
        petfx_face_point(0);
      } else if (s_hatch_look == 1 && el >= HATCH_T_LOOK + HATCH_LOOK_MS / 2u) {
        s_hatch_look = 2;
        petfx_face_point(OLED_W - 1);
      }
      if (el >= HATCH_T_NAME) { s_hatch_phase = HP_NAME; petfx_face_point(OLED_W / 2); }
      break;

    case HP_NAME:
      if (el >= HATCH_TOTAL_MS) {
        s_hatch_phase = HP_NONE;
        petfx_freeze(0);
        input_flush();      // a button held through the ceremony must not fire a
        nav_home();         // stale gesture on the HOME it lands on (AUDIT 15)
      }
      break;

    default:
      break;
  }
}

// Shell fragments. Six fixed directions scaled by an expanding radius; the
// table is deliberately asymmetric so it does not read as a mechanical star.
static const int8_t kShardDX[6] = { -4,  4, -4,  4, -1,  1 };
static const int8_t kShardDY[6] = { -2, -2,  1,  1,  3,  3 };

// The ceremony owns the WHOLE frame: no header, no affordance strip, no toast,
// no countdown. Chrome would turn a birth into a screen.
static void draw_hatch(void) {
  U8G2& u = rd_u8g2();
  const SimView* p     = pet();
  const uint32_t el    = since(s_hatch_ms);
  const uint8_t  frame = (uint8_t)((now_ms() / 120u) & 1u);   // fast: it is straining

  // ---- 1. the egg rocking, accelerating ----------------------------------
  if (s_hatch_phase == HP_WOBBLE) {
    // Quadratic swing count: the rocking gets FASTER, which is what reads as
    // effort. el <= HATCH_WOBBLE_MS so el*el <= 1.44e6 and this stays in uint32.
    const uint32_t swings = (el * el) / HATCH_WOBBLE_K;
    const int16_t  amp    = (int16_t)(1 + (el * 3u) / HATCH_WOBBLE_MS);
    const SpriteRef r = sprite_egg(0, frame);
    px_spr((int16_t)((int16_t)sprite_center_x(r.w) + ((swings & 1u) ? amp : (int16_t)-amp)),
           (int16_t)sprite_center_y(r.h), r);
    rd_text_center(52, RD_FONT_BODY, S(STR_EGG_HATCHING));
    return;
  }

  // ---- 2. the crack, and 3. the flash ------------------------------------
  // HP_FLASH draws the same thing: the 0xA7 invert is a PANEL state, so the
  // white frame costs nothing to draw and the egg simply reads as a dark
  // silhouette on white for 80 ms.
  if (s_hatch_phase == HP_CRACK || s_hatch_phase == HP_FLASH) {
    const SpriteRef r = sprite_egg(1, frame);
    px_spr((int16_t)((int16_t)sprite_center_x(r.w) + (frame ? 2 : -2)),
           (int16_t)sprite_center_y(r.h), r);
    if (s_hatch_phase == HP_CRACK)
      rd_text_center(52, RD_FONT_BODY, S(STR_EGG_HATCHING));
    return;
  }

  // ---- 4. the shards ------------------------------------------------------
  if (s_hatch_phase == HP_SHARDS) {
    // Same clamp-the-input rule as HP_GROW below: an overrun must hold the last
    // frame, not run the radius off the panel and the dissolve past full.
    uint32_t t = el - HATCH_T_SHARDS;
    if (t > HATCH_SHARDS_MS) t = HATCH_SHARDS_MS;
    const int16_t  rad = (int16_t)((t * 22u) / HATCH_SHARDS_MS);
    const SpriteRef r  = sprite_egg(1, frame);
    px_spr((int16_t)sprite_center_x(r.w), (int16_t)sprite_center_y(r.h), r);
    // The shell disintegrates while the fragments leave: erase an increasing
    // share of it. Colour 0 over a colour-0 background is a no-op, so this is
    // clipped to the silhouette for free (render.cpp:637).
    u.setDrawColor(0);
    rd_dither_rect((int16_t)sprite_center_x(r.w), (int16_t)sprite_center_y(r.h),
                   r.w, r.h, (uint8_t)((t * RD_DITHER_MAX) / HATCH_SHARDS_MS));
    u.setDrawColor(1);
    const int16_t cx = (int16_t)(OLED_W / 2 - 3);
    const int16_t cy = (int16_t)(SPRITE_AREA_Y + SPRITE_AREA_H / 2 - 3);
    for (uint8_t i = 0; i < 6; ++i)
      px_spr((int16_t)(cx + ((int16_t)kShardDX[i] * rad) / 4),
             (int16_t)(cy + ((int16_t)kShardDY[i] * rad) / 4), sprite_emote(EMO_SPARK));
    return;
  }

  if (!p) return;

  // ---- 5..7: the body is out. petfx owns where it is from here on ---------
  petfx_draw_body(*p, POSE_IDLE, frame, 0);

  if (s_hatch_phase == HP_GROW) {
    // A window that opens outwards from the body's waist (so it reads as
    // growing, not as wiping) and a dither that thins out (so it materialises,
    // not pops).
    // Clamp t, NOT lvl. The old code clamped the RESULT: with t past
    // HATCH_GROW_MS the subtraction below underflows a uint8 to ~255 and a
    // "lvl > RD_DITHER_MAX" clamp pins it to RD_DITHER_MAX - a FULL erase of
    // the sprite band - instead of to 0, which is the state the phase is
    // travelling towards. One such frame is a black flash at the exact moment
    // the baby finishes materialising. Clamping the input makes the overrun a
    // no-op that simply holds the final frame.
    uint32_t t = el - HATCH_T_GROW;
    if (t > HATCH_GROW_MS) t = HATCH_GROW_MS;
    const int16_t  by = petfx_body_y();
    const int16_t  bh = (int16_t)petfx_body_h();
    const int16_t  cy = (int16_t)(by + bh / 2);
    const int16_t  hh = (int16_t)(((int32_t)t * (int32_t)(bh / 2 + 1)) / (int32_t)HATCH_GROW_MS);
    const uint8_t lvl = (uint8_t)(RD_DITHER_MAX - (t * RD_DITHER_MAX) / HATCH_GROW_MS);
    u.setDrawColor(0);
    px_box(0, SPRITE_AREA_Y, OLED_W, (int16_t)(cy - hh - SPRITE_AREA_Y));
    px_box(0, (int16_t)(cy + hh), OLED_W,
           (int16_t)(SPRITE_AREA_Y + SPRITE_AREA_H - (cy + hh)));
    rd_dither_rect(0, SPRITE_AREA_Y, OLED_W, SPRITE_AREA_H, lvl);
    u.setDrawColor(1);
    return;
  }

  if (s_hatch_phase == HP_NAME) {
    char name[16], line[64];
    ui_pet_name(name, sizeof(name));
    const FmtArg fa[1] = { { 'n', name } };
    fmt_apply(line, sizeof(line), S(STR_EGG_NAMED), fa, 1);
    rd_text_center(52, RD_FONT_NARR, line);
    rd_text_center(61, RD_FONT_BODY, S(STR_HATCH_WELCOME));
    return;
  }

  rd_text_center(52, RD_FONT_BODY, S(STR_HATCH_LOOK));   // HP_LOOK
}

// ---- THE EGG SCREEN ---------------------------------------------------------

static void draw_egg(void) {
  const SimView* p = pet();
  draw_header(S(STR_EGG_TITLE), nullptr);
  if (!p) { rd_affordance(nullptr, nullptr); return; }

  const int16_t wob   = (int16_t)(((now_ms() / 260u) & 1u) ? 1 : -1);
  const uint8_t frame = (uint8_t)((now_ms() / UI_ANIM_FRAME_MS) & 1u);
  const uint8_t phase = (p->age_s + 60u >= AGE_EGG_S || s_rub_count >= EGG_RUB_TAPS) ? 1u : 0u;
  const SpriteRef r   = sprite_egg(phase, frame);
  px_spr((int16_t)((int16_t)sprite_center_x(r.w) + wob), (int16_t)(UI_CONTENT_Y + 2), r);

  rd_text_center(45, RD_FONT_BODY, S(STR_EGG_RUB));
  for (uint8_t i = 0; i < EGG_RUB_TAPS; ++i) {
    const int16_t x = (int16_t)(OLED_W / 2 - EGG_RUB_TAPS * 3 + i * 6);
    if (i < s_rub_count) px_box(x, 50, 5, 5);
    else                 px_frame(x, 50, 5, 5);
  }
  rd_affordance(S(STR_AF_RUB), S(STR_AF_RUB));

  if (p->flags & PF_COLD_EGG)
    rd_text_fit(2, 19, 124, RD_FONT_BODY, S(STR_EGG_COLD));
  else if (p->genome.generation > 0 && (p->flags & PF_INBRED))
    rd_text_fit(2, 19, 124, RD_FONT_BODY, S(STR_EGG_KIN));
}

static void handle_egg(Gesture g) {
  if (g != GST_TAP_L && g != GST_TAP_R) { ui_toast(STR_EGG_NOT_YET); return; }

  const uint8_t side = (g == GST_TAP_L) ? 0u : 1u;
  if (s_rub_ms == 0 || since(s_rub_ms) > EGG_RUB_WINDOW_MS) {
    s_rub_ms    = now_ms();
    s_rub_count = 0;
    s_rub_last  = 0xFF;
  }
  if (s_rub_last == side) { ui_toast(STR_EGG_NOT_YET); return; }   // must alternate
  s_rub_last = side;
  if (++s_rub_count >= EGG_RUB_TAPS) {
    sim_hatch();
    s_rub_count = 0;
    // Neither the save nor the toast happen here any more: hatch_begin() owns
    // the save, and sim_hatch() raises SIM_EV_HATCHED so ui_note_events() owns
    // the toast.
    hatch_begin();
  }
}

// =============================================================================
//  16b. BOOT / LOAD_SAVE / ERROR
//
//  Gone from this file. All three are screen-table rows now (P2-C11a):
//  ui/screen_boot.cpp draws the two splashes, ui/screen_error.cpp owns the
//  ERROR state including the checkpoint recovery, the newer-save refusal and
//  the panel-failure retry that replaced rd_fatal(). What is left here is the
//  wipe confirmation the ERROR screen asks for and the re-prime after a
//  successful recovery, both of which are modal-layer and pet-presentation
//  work that belongs to ui.cpp until those move too.
// =============================================================================
void ui_confirm_wipe(void) { confirm_open(CFM_WIPE1, STR_CF_WIPE); }

void ui_note_recovered(void) {
  s_stat_ok = 0;                 // show the recovered pet's truth at once
  const SimView* p = pet();
  if (p) petfx_reset(*p);
  sm_replace_root(SCR_HOME);
}

// =============================================================================
//  17. CONFIRM / ALERT overlays
// =============================================================================
static void confirm_commit(void) {
  const uint8_t which = s_confirm_id;
  modal_close();
  switch (which) {
    case CFM_QUIT_GAME:
      s_g.score = 0;
      game_finish();                                   // counts as a loss
      break;
    case CFM_MEDICINE: act_and_show(ACT_MEDICINE); break;   // BRIEF D
    case CFM_WIPE1:    confirm_open(CFM_WIPE2, STR_CF_WIPE2); break;   // two dialogs
    case CFM_WIPE2: {
      gs_factory_reset();
      if (s_cfg) { gs_cfg_defaults(*s_cfg); gs_save_cfg(*s_cfg); }
      const Genome g0 = genome_genesis();
      sim_new_pet(g0, gt_now(), 0);
      const SimView* p = pet();
      if (p) { gs_save_active(true); petfx_reset(*p); }
      s_stat_ok  = 0;                // a wiped device shows the truth at once
      err_set_kind(ERRK_NONE);       // the save the ERROR screen was about is gone
      sm_replace_root(SCR_EGG);
      break;
    }
    default: break;
  }
}

static void draw_confirm(void) {
  U8G2& u = rd_u8g2();
  const int16_t y = 12, h = 40;
  px_box(4, y, OLED_W - 8, h);
  u.setDrawColor(0);
  px_frame(5, (int16_t)(y + 1), OLED_W - 10, (int16_t)(h - 2));
  rd_text_wrap(9, (int16_t)(y + 11), OLED_W - 18, RD_LINE_BODY, 2,
               RD_FONT_BODY, S(s_modal_str));
  u.setDrawColor(1);

  const int16_t by = (int16_t)(y + h - 14);
  for (uint8_t i = 0; i < 2; ++i) {
    const bool    yes = (i == 1);
    const int16_t bx  = (int16_t)(yes ? 68 : 14);
    const bool    sel = (yes == (s_confirm_yes != 0));
    u.setDrawColor(0);
    px_box(bx, by, 46, 12);
    u.setDrawColor(1);
    if (sel) px_box((int16_t)(bx + 1), (int16_t)(by + 1), 44, 10);
    u.setDrawColor(sel ? 0 : 1);
    rd_text_center_in((int16_t)(bx + 1), 44, (int16_t)(by + 9), RD_FONT_HEAD,
                      S(yes ? STR_YES : STR_NO));
    u.setDrawColor(1);
  }
  rd_affordance(S(STR_AF_NEXT), S(STR_AF_OK));
}

static void handle_confirm(Gesture g) {
  switch (g) {
    case GST_TAP_L:     s_confirm_yes = (uint8_t)!s_confirm_yes; break;
    case GST_TAP_R:     if (s_confirm_yes) confirm_commit(); else modal_close(); break;
    case GST_HOLD_R:    modal_close(); break;
    case GST_LONG_BOTH: modal_close(); nav_home(); break;
    default: break;
  }
}

static void draw_alert(void) {
  U8G2& u = rd_u8g2();
  const int16_t y = 16, h = 32;
  px_box(2, y, OLED_W - 4, h);
  u.setDrawColor(0);
  px_frame(3, (int16_t)(y + 1), OLED_W - 6, (int16_t)(h - 2));
  px_spr(7, (int16_t)(y + 11), sprite_mini(MIC_ALERT));
  rd_text_wrap(19, (int16_t)(y + 13), OLED_W - 26, RD_LINE_BODY, 2,
               RD_FONT_BODY, S_ALERT(s_alert_cur));
  u.setDrawColor(1);
  rd_affordance(S(STR_AF_OK), S(STR_AF_SEL));
}

// One press solves it: dismiss AND jump to the screen that fixes the problem.
static void alert_act(void) {
  const uint8_t a = s_alert_cur;
  modal_close();
  switch (a) {
    case AL_HUNGRY:     nav_push(SCR_FEED);      break;
    case AL_SAD:        nav_push(SCR_PLAY);      break;
    case AL_DIRTY:
    case AL_POOP:       act_and_show(ACT_CLEAN); break;
    case AL_TIRED:      act_and_show(ACT_SLEEP_TOGGLE); break;
    case AL_SICK:       confirm_open(CFM_MEDICINE, STR_CF_SURE); break;
    case AL_LOW_HEALTH: nav_push(SCR_STATUS_A);  break;
    case AL_MATE_FOUND: nav_push(SCR_SOCIAL);    break;
    default: break;
  }
}

static void handle_alert(Gesture g) {
  if (since(s_modal_ms) < UI_ALERT_MIN_MS) return;      // must be readable first
  if (g == GST_HOLD_R)    { modal_close(); return; }    // dismiss without acting
  if (g == GST_LONG_BOTH) { modal_close(); nav_home(); return; }
  alert_act();
}

// =============================================================================
//  18. SCREEN ENTER / LEAVE HOOKS
// =============================================================================
// Only the screens that have NOT moved into the table reach these two: a
// migrated row carries its own enter / leave hooks and state_machine.cpp calls
// those instead (state_machine.cpp, sm_goto).
static void screen_enter(uint8_t s) {
  switch (s) {
    case SCR_SOCIAL:
      social_enter();
      break;
    case SCR_QR:
#if FEATURE_WEB
      // QR is the screen that wants the station; there is no radio policy in
      // the entry point any more (plan section 2 row G4). It is released again
      // in screen_leave().
      // Was cfg_flag(CF_WEB_ENABLED). The helper had exactly this one caller
      // left once SETTINGS and the HOME status bar moved out, and that caller
      // is inside #if FEATURE_WEB - so in the no-web variant the function was
      // defined and never used, which is a warning and this build treats as an
      // error. Read the flag here instead of keeping a helper for one site.
      if (net_mode() != RADIO_WIFI && s_cfg && (s_cfg->flags & CF_WEB_ENABLED) != 0)
        net_request(RADIO_WIFI);
#endif
      s_qr_variant = net_is_ap_up() ? 1u : 0u;
      s_qr_key[0]  = '\0';
      s_qr_ms      = now_ms();
      s_qr_manual  = 0;
      qr_build();
      break;
    case SCR_EGG:
      s_rub_count = 0;
      s_rub_last  = 0xFF;
      s_rub_ms    = 0;
      break;
    default:
      break;
  }
}

static void screen_leave(uint8_t s) {
  // HOME's own leave hook (home_leave_layer) is what ends the choreography
  // now; this one only sees the screens still living in this file.
  if (s == SCR_SOCIAL) social_leave();
#if FEATURE_WEB
  // Radio OFF by default: the QR screen is the only owner of RADIO_WIFI, so
  // leaving it gives the ~50 KB and the largest current draw on the board back
  // instead of holding the station powered until the next reboot.
  if (s == SCR_QR && net_mode() == RADIO_WIFI) (void)net_request(RADIO_OFF);
#endif
  if (s == SCR_GAME && s_g.phase == 1) {
    // Abandoning a game in any way at all is a loss.
    ActionResult r;
    s_g.score = 0;
    sim_apply_play_result(0, r);
    s_g.phase = 2;
  }
}

// =============================================================================
//  19. PUBLIC ENTRY POINTS
// =============================================================================

void ui_bind_config(Config* cfg) {
  s_cfg = cfg;
  if (!s_cfg) return;
  s_bright_base  = s_cfg->brightness ? s_cfg->brightness : (uint8_t)OLED_CONTRAST_DEFAULT;
  s_bright_valid = 0;             // force the first write, whatever the value
  bright_service();
}

// Draws one frame of a screen that needs no state at all, so the entry point
// can show BOOT and LOAD_SAVE while the save pipeline runs - before ui_begin().
// Straight out of the table: neither row has an enter hook, so drawing one
// without navigating to it is exactly what the row promises.
void ui_boot_screen(ScreenId s) {
  if (s != SCR_BOOT && s != SCR_LOAD_SAVE) return;
  const ScreenDef* d = screen_def((uint8_t)s);
  if (!d) return;
  if (!rd_begin_frame()) return;
  d->render();
  rd_end_frame();
}

void ui_note_brightness(uint8_t contrast) {
  s_bright_base = contrast ? contrast : (uint8_t)OLED_CONTRAST_DEFAULT;
  bright_service();
}

// Defined with the rest of the seams in section 20; ui_begin() binds it.
static const PebbleView* ui_fill_view(void);

void ui_begin(void) {
  memset(&s_g,     0, sizeof(s_g));
  // The screens P2-C11b migrated read the pet through this snapshot and draw
  // the animated stage through the bound layer. Both bindings are re-made on
  // every ui_begin(), which is also what a factory reset runs.
  ui_bind_view(&ui_fill_view);
  home_bind_body(&home_body, &home_leave_layer);
  memset(s_raw_prev, 0, sizeof(s_raw_prev));
  sm_begin();
  s_modal       = MODAL_NONE;
  s_alert_n     = 0;
  s_alert_cur   = AL_NONE;
  s_toast[0]    = '\0';
  s_qr_key[0]   = '\0';
  s_qr_size     = 0;
  s_soc_cursor  = 0;
  s_last_action = ACT_NONE;
  s_absence_ms  = 0;
  s_soc_phase   = SOC_ENTER;
  s_stat_ok     = 0;                 // boot: show the truth, do not animate to it
  s_hatch_phase = HP_NONE;
  s_hatch_ms    = 0;
  s_trans_ms    = 0;
  gfx_list_reset();
  s_bright_valid = 0;                // force the first contrast decision
  petfx_begin();
  actfx_cancel();                    // nothing survives a reboot or a wipe
  // Prime actfx's clock. actfx_begin() timestamps from whatever
  // actfx_service() last handed it, and loop() drains the INPUT queue - and so
  // reaches do_action() - BEFORE it calls ui_service(). Without this line a
  // button pressed inside the very first loop() after boot would start a film
  // stamped t0 = 0, which the next tick would measure as millis() ms old and
  // cancel on the spot: the action would work and the animation would not.
  actfx_service(now_ms());
  // ui.cpp owns kPoopX because petfx_set_obstacles() is fed from it too; actfx
  // only borrows it, so ACT_CLEAN can dissolve poops the model has already
  // thrown away. One table, three consumers, no copy to drift.
  actfx_bind_poop_layout(kPoopX, (uint8_t)POOP_MAX);

  const SimView* p = pet();
  if (p) petfx_reset(*p);            // AFTER petfx_begin(), BEFORE any draw
  if (p && p->stage == STAGE_EGG) { ui_goto(SCR_EGG); return; }
  ui_goto(SCR_HOME);
}

void ui_note_events(uint32_t ev) {
  // Consumed here, before anything can return: this batch is either the
  // catch-up's or live play's, and the answer must not survive into the next
  // batch. See s_events_offline and ui_note_absence().
  const uint8_t offline = s_events_offline;
  s_events_offline = 0;

  if (ev & SIM_EV_HATCHED) {
    ui_toast(STR_EGG_HATCHED);
    // Replaces the bare nav_home(). Reached from BOTH the age-driven hatch and
    // (one tick late) from the manual rub, so hatch_begin() is idempotent. When
    // the egg hatches while the player is on some other screen they get pulled
    // into the ceremony.
    //
    // UNLESS it happened while nobody was watching. An egg hatches at AGE_EGG_S
    // (900 s), so leaving the device off for a quarter of an hour is enough for
    // it to hatch inside sim_catch_up_ex(); hatch_begin() would then clear
    // s_absence_ms and s_alert_n and the welcome-back report - armed moments
    // earlier by ui_note_absence() - would be destroyed by a 4.5 s show about a
    // moment the player did not see. The ceremony is for when you are IN FRONT
    // OF IT: rubbing the shell, or coming of age with the device switched on.
    // An offline hatch gets the old bare nav_home() and the banner stands.
    if (offline) nav_home();
    else         hatch_begin();
  }
  // Being born is not evolving. sim_hatch() raises SIM_EV_HATCHED and
  // SIM_EV_STAGE_UP in the SAME batch (sim.cpp), and the two do not compose:
  // hatch_begin() has just cleared the alert queue, this branch would refill it,
  // the queue does not expire and ui_service() returns early for the whole
  // ceremony - so the alert fires on the first frame of HOME after the birth,
  // with UI_ALERT_MIN_MS forcing the player to sit through a modal telling them
  // their newborn is evolving. HATCHED wins the batch outright.
  if (!(ev & SIM_EV_HATCHED) && (ev & (SIM_EV_STAGE_UP | SIM_EV_EVOLVE_MINOR))) {
    s_evolve_ms = now_ms();
    ui_alert(AL_EVOLVING);
    ui_toast(STR_RX_EVOLVE);
  }
  if (ev & SIM_EV_POOP)        ui_alert(AL_POOP);
  if (ev & SIM_EV_SICK_START)  ui_alert(AL_SICK);
  if (ev & SIM_EV_SICK_END)    ui_toast(STR_RX_MED);
  if (ev & SIM_EV_WISH_START)  ui_alert(AL_WISH);
  if (ev & SIM_EV_WISH_OK)     ui_toast(STR_WISH_OK);
  if (ev & SIM_EV_WISH_FAIL)   ui_toast(STR_WISH_FAIL);
  if (ev & SIM_EV_BIRTHDAY)  { ui_alert(AL_BIRTHDAY); ui_toast(STR_EV_BIRTHDAY); }
  if (ev & SIM_EV_VISITA)      ui_toast(STR_EV_VISITA);
  // PF_ASLEEP is already set / cleared by the time the event is delivered, so
  // bright_service() derives the target itself instead of taking it as an
  // argument. Calling it here rather than waiting for the next ui_service()
  // only buys one loop of latency, but it is the loop the player is looking at.
  if (ev & SIM_EV_SLEEP)     { ui_toast(STR_RX_SLEEP); bright_service(); }
  if (ev & SIM_EV_WAKE)      { ui_toast(STR_RX_WAKE);  bright_service(); }
  if (ev & SIM_EV_ALERT) {
    const uint8_t a = sim_alert();
    if (a != AL_NONE) ui_alert((AlertId)a);
  }
}

void ui_note_absence(const AbsenceReport& rep) {
  // Arm the "these events happened offline" marker. boot_absence() calls this
  // and then, with nothing in between, the single ui_note_events() that drains
  // sim_catch_up_ex() - so the flag is an exact marker for that one batch and
  // is cleared by it. Every later ui_note_events() comes from logic_tick(),
  // i.e. from live play with the player watching.
  s_events_offline = 1;

  // After an absence the bars are simply what they are: animating 3 days of
  // decay would look like the pet was deflating in front of the player.
  s_stat_ok       = 0;
  s_absence_known = rep.clock_known ? 1u : 0u;
  s_absence_s     = rep.absence_s;
  s_absence_ms    = now_ms();
}

void ui_handle(Gesture g) {
  if (g == GST_NONE) return;
  if (ui_input_locked()) return;                       // the hatch ceremony

  sm_note_input();
  s_wiggle_ms = 0;
  // Any gesture, on any screen: the pet turns to look at the player. Placed
  // after the ui_input_locked() gate above, so the hatch ceremony cannot be
  // poked out of its staging.
  petfx_attention();
  rd_request_frame();

  // --- god mode owns its own screen; we act on what it tells us ------------
  if (sm_current() == SCR_GOD) {
    // godmode.h: "Never call this when god_active() is false." The console's
    // own exit row turns the MODE off from inside a previous god_handle(), so
    // the screen can outlive it by one gesture.
    if (!god_active()) { nav_home(); return; }
    switch (god_handle(g)) {
      case GOD_EVT_LEAVE:
        // Leaves the SCREEN. god_active() may well still be true - watching an
        // accelerated life on the normal screens is the entire point.
        nav_home();
        break;
      case GOD_EVT_WIPED: {
        if (s_cfg) gs_cfg_defaults(*s_cfg);
        const SimView* np = pet();
        if (np) petfx_reset(*np);    // god handed us a different animal entirely
        s_stat_ok = 0;
        sm_replace_root(SCR_EGG);
        break;
      }
      default:
        break;                                         // handled internally
    }
    return;
  }

  // godmode.h: while the entry hold is in progress GST_LONG_BOTH must be
  // swallowed, or 1500 ms into a 5000 ms hold the user is thrown HOME and the
  // gesture becomes unreachable.
  if (s_god_prog && g == GST_LONG_BOTH) return;

  // --- invariant 7: an alert never lets a press through --------------------
  if (s_modal == MODAL_ALERT)   { handle_alert(g);   return; }
  if (s_modal == MODAL_CONFIRM) { handle_confirm(g); return; }
  if (s_modal == MODAL_HELP) {
    modal_close();
    if (g == GST_LONG_BOTH) nav_home();                // invariant 2 still wins
    return;
  }

  // --- the screen table, for the screens that have moved ------------------
  // SF_LOCK_INPUT means the row owns every gesture: the two global invariants
  // below are not applied to it. ERROR uses that to hold the device on an
  // unanswered question - walking away would leave the user playing a
  // placeholder pet that can never be written - and BOOT / LOAD_SAVE use it
  // because they are not waiting on a button at all.
  const ScreenDef* def = sm_def();
  const bool owns_input = (def != nullptr) && ((def->flags & SF_LOCK_INPUT) != 0u);
  const uint8_t scr = (uint8_t)sm_current();

  if (!owns_input) {
    // --- invariant 2: HOME from anywhere ----------------------------------
    if (g == GST_LONG_BOTH && scr != SCR_HOME) { nav_home(); return; }

    // --- invariant 1: BACK on every screen except GAME -------------------
    // The TIME screen needs a repeating right button to enter a date, and it
    // says so through SF_LOCK_INPUT rather than through a name checked here.
    if (g == GST_HOLD_R && scr != SCR_GAME) {
      if (scr == SCR_HOME) { s_wiggle_ms = now_ms(); return; }
      // On the "Acerca de" page BACK closes the page, not the screen.
      if (scr == SCR_SETTINGS && settings_page() == 1) { settings_close_page(); return; }
      nav_back();
      return;
    }
  }

  if (sm_handle(g)) return;
  if (def) return;             // migrated, and this row takes no input at all

  switch (scr) {
    case SCR_GAME:      handle_game(g);     break;
    case SCR_SOCIAL:    handle_social(g);   break;
    case SCR_EGG:       handle_egg(g);      break;
    case SCR_QR:        handle_qr(g);       break;
    default:            break;
  }
}

void ui_service(void) {
  const uint32_t t = now_ms();

  // Above every early return below. The displayed stats and the body's own
  // clock must keep running on the god screen too, or leaving it shows a body
  // frozen where it was minutes ago and six bars oozing into place.
  stat_service();
  bright_service();
  {
    const SimView* p = pet();
    if (p) {
      // The props go in BEFORE the automaton steps, and from here rather than
      // from draw_home(): the body keeps walking on loops that never draw a
      // frame (FPS_LOW, panel asleep, a modal on top), and a wall that only
      // exists while something is looking at it is not a wall.
      const uint8_t np = (p->poop_count > POOP_MAX) ? (uint8_t)POOP_MAX
                                                    : p->poop_count;
      petfx_set_obstacles(kPoopX, np, 12);   // sprite_icon() is 12x12
      petfx_service(*p, t);
    }
  }

  // ABOVE every early return below, unconditionally, and deliberately NOT gated
  // on being on HOME. This is what BOUNDS the petfx_hold() a choreography takes:
  // the clock keeps running whatever screen is up, so the hold is released
  // within one film (2.6 s worst case) even if every explicit actfx_cancel() in
  // this file were deleted. The five explicit ones are:
  //   screen_leave(SCR_HOME)  - leaving HOME by any route, back or home or push
  //   hatch_begin()           - the birth ceremony, which may arrive on any screen
  //   the god-mode entry just below, belt and braces: it re-roots the machine
  //     through sm_replace_root(), so the leave hook does run
  //   ui_begin()              - boot, and the wipe that re-runs it
  // A hold that outlives its film is a pet nailed to the floor until the device
  // is power-cycled, and it is the single most likely way this feature breaks.
  actfx_service(t);

  // The undocumented GOD entry. godmode.cpp owns the hold timing and performs
  // the entry itself (including input_flush()); at 100 we only switch screen.
  s_god_prog = god_entry_progress((uint8_t)sm_current());
  if (s_god_prog >= 100u) {
    actfx_cancel();
    s_god_prog = 0;
    sm_replace_root(SCR_GOD);
    return;
  }

  // god_service() is the entry point's to call; ui only owns the screen.
  if (sm_current() == SCR_GOD) { if (!god_active()) nav_home(); return; }
  // Returning here is what keeps the alert layer, the auto-return and the QR
  // pump off the ceremony's back for its whole 4.5 s.
  if (hatch_active()) { hatch_service(); return; }
  if (sm_current() == SCR_GAME)   game_service();
  if (sm_current() == SCR_SOCIAL) social_service();

  // The alert layer surfaces only when nothing else owns the screen.
  if (s_modal == MODAL_NONE && s_alert_n > 0 && sm_current() != SCR_GAME) {
    alert_pop();
  }
  if (s_modal == MODAL_HELP && since(s_modal_ms) >= UI_MODAL_HELP_MS) modal_close();

  // Invariant 3, plus the update hook of a migrated screen. A modal freezes
  // the countdown: the one that is running belongs to the screen underneath.
  sm_block_autoreturn(s_modal != MODAL_NONE);
  if (sm_service(t)) return;

  // The QR payload changes when the IP or the PIN does. In AP-provisioning
  // mode the two symbols alternate every 5 s: join the network first, then
  // open the page. A manual tap pins the choice for 10 s.
  if (sm_current() == SCR_QR && since(s_qr_ms) >= 1000UL) {
    s_qr_ms = t;
    if (net_is_ap_up() && (s_qr_manual == 0 || since(s_qr_manual) > 10000UL)) {
      const uint8_t want = (uint8_t)(((sm_screen_ms() / 5000UL) & 1u) ? 0u : 1u);
      if (want != s_qr_variant) { s_qr_variant = want; s_qr_key[0] = '\0'; }
    }
    qr_build();
  }

  apply_fps();
}

void ui_draw(void) {
  // Pushed once per frame, before anything draws: rd_affordance() may be called
  // several times in one frame (base screen + a modal) and every one of them
  // must agree about what is being held.
  rd_affordance_pressed((uint8_t)((input_raw(INPUT_BTN_L) ? 1u : 0u) |
                                  (input_raw(INPUT_BTN_R) ? 2u : 0u)));
  // The screen table first: a migrated row draws the whole base frame, and
  // SF_OWNS_FRAME means it also owns the layers below (no toast, no modal, no
  // dissolve on top of it).
  const ScreenDef* def = sm_def();
  if (def) {
    def->render();
    if (def->flags & SF_OWNS_FRAME) { rd_affordance_echo(); return; }
  }
  // Not migrated yet: the old switch, one case shorter every commit.
  else switch (sm_current()) {
    case SCR_GAME:     draw_game();     break;
    case SCR_SOCIAL:   draw_social();   break;
    case SCR_EGG:
      // The ceremony owns the frame and draws no strip, so the echo below finds
      // nothing to invert - it is called only to leave the per-frame state clean.
      if (hatch_active()) { draw_hatch(); rd_affordance_echo(); god_draw_marker(); return; }
      draw_egg();
      break;
    case SCR_QR:       draw_qr();       break;
    case SCR_GOD:      god_draw();      rd_affordance_echo(); return;  // god owns the frame
    // Every remaining id is either migrated (handled above) or a modal that
    // has no base frame of its own. Drawing nothing is the honest answer: the
    // modal layer below still runs.
    default:           break;
  }

  if (s_modal == MODAL_CONFIRM)      draw_confirm();
  else if (s_modal == MODAL_ALERT)   draw_alert();
  else if (s_modal == MODAL_HELP) {
    U8G2& u = rd_u8g2();
    const int16_t y = RD_AFFORD_Y - 12;
    px_box(0, y, OLED_W, 12);
    u.setDrawColor(0);
    rd_text_center((int16_t)(y + 9), RD_FONT_BODY, S(s_modal_str));
    u.setDrawColor(1);
  } else {
    draw_toast();
  }

  // ONE tactile echo per frame, here, after the base screen and the modal have
  // both had their say. rd_invert_rect() is a real XOR, and draw_home() and
  // draw_confirm() / draw_alert() each call rd_affordance() in the same frame:
  // inverting inside rd_affordance() meant the second call undid the first, so
  // with any modal open no button press produced any feedback at all. Held
  // BEFORE draw_transition(), where the inversion used to happen, so a
  // dissolving screen still dissolves the echo with everything else.
  rd_affordance_echo();

  // Over the modals too: they are part of the frame the player is arriving at.
  // Before the marker, because a dissolving "GOD xN" would be unreadable at
  // exactly the moment it matters most.
  draw_transition();

  // godmode.h: the "GOD xN" marker goes LAST on every screen, so the state can
  // never hide behind a modal. It is a no-op while god mode is off.
  god_draw_marker();
}

// =============================================================================
//  20. THE SCREEN SEAMS (P2-C11b)
//
//  A migrated screen is a pure translation unit. Everything it needs that is
//  not pure - the wall clock, the navigation machine, the modal layer, the
//  simulation, the panel contrast, the radio, the NVS error word - arrives
//  through these one-line forwarders, which is what lets test_screens.cpp
//  compile and render every one of those screens on the host.
// =============================================================================

uint32_t ui_now_ms(void)   { return now_ms(); }
uint32_t ui_idle_ms(void)  { return sm_idle_ms(); }

void ui_push(ScreenId s)   { sm_push(s); }
void ui_back(void)         { sm_back(); }
void ui_note_input(void)   { sm_note_input(); }

Config* ui_cfg(void)       { return s_cfg; }
void ui_cfg_changed(void)  { cfg_persist(); }

void ui_apply_brightness(uint8_t contrast) {
  s_bright_base = contrast ? contrast : (uint8_t)OLED_CONTRAST_DEFAULT;
  bright_service();
}

bool ui_do_action(uint8_t action) {
  return (action < (uint8_t)ACT_COUNT) ? do_action((ActionId)action) : false;
}

bool ui_act_and_show(uint8_t action) {
  return (action < (uint8_t)ACT_COUNT) ? act_and_show((ActionId)action) : false;
}

void ui_repeat_last_action(void) {
  if (s_last_action != ACT_NONE) act_and_show((ActionId)s_last_action);
  else                           ui_toast(STR_AERR_BAD_ARG);
}

void ui_help(uint16_t str_id)    { help_open(str_id); }
void ui_confirm_medicine(void)   { confirm_open(CFM_MEDICINE, STR_CF_SURE); }

void ui_start_minigame(uint8_t idx) {
  const uint16_t cd = sim_minigame_cooldown_s();
  if (cd) {
    char buf[48];
    snprintf(buf, sizeof(buf), "%s %u s", S(STR_GM_COOLDOWN), (unsigned)cd);
    toast_text(buf);
    return;
  }
  if (sim_stat_pct(ST_ENERGY) < ACT_PLAY_MIN_ENERGY_PCT) { ui_toast(STR_AERR_TIRED); return; }
  game_start(idx);
}

uint8_t ui_god_progress(void) { return s_god_prog; }

void ui_info_lines(char lines[UI_INFO_LINES][UI_INFO_CAP]) {
  for (uint8_t i = 0; i < UI_INFO_LINES; ++i) lines[i][0] = '\0';
  snprintf(lines[0], UI_INFO_CAP, "%s %s", FW_NAME, FW_VERSION);
  snprintf(lines[1], UI_INFO_CAP, "IP %s  rssi %d", net_ip(), (int)net_rssi());
  snprintf(lines[2], UI_INFO_CAP, "PIN %04u  spr rev %u",
           (unsigned)(web_pin() % 10000u), (unsigned)SPRITE_REV);
  snprintf(lines[3], UI_INFO_CAP, "heap %lu  nvs %02X",
           (unsigned long)ESP.getFreeHeap(), (unsigned)kv_error());
  if (gt_is_valid()) {
    char t[GT_ELAPSED_BUF];
    gt_format_elapsed(sim_age_s(), t, sizeof(t));
    snprintf(lines[4], UI_INFO_CAP, "age %s", t);
  } else {
    snprintf(lines[4], UI_INFO_CAP, "%s", S(STR_UI_NO_CLOCK));
  }
}

bool ui_get_clock(uint16_t* year, uint8_t* month, uint8_t* day,
                  uint8_t* hour, uint8_t* minute) {
  struct tm lt;
  if (!year || !month || !day || !hour || !minute) return false;
  if (!gt_local_tm(lt)) return false;
  *year   = (uint16_t)(lt.tm_year + 1900);
  *month  = (uint8_t)(lt.tm_mon + 1);
  *day    = (uint8_t)lt.tm_mday;
  *hour   = (uint8_t)lt.tm_hour;
  *minute = (uint8_t)lt.tm_min;
  return true;
}

// gt_set_epoch(CAL_USER) is the one source allowed to move the clock BACKWARDS,
// so a user correcting a wrong date is never refused; gs_touch_lastseen() then
// rewrites the persisted baseline so the next boot measures its absence from
// the truth and not from an uptime.
bool ui_set_clock(uint16_t year, uint8_t month, uint8_t day,
                  uint8_t hour, uint8_t minute) {
  const uint32_t e = gt_epoch_from_local((int)year, month, day, hour, minute);
  if (e == 0 || !gt_set_epoch(e, CAL_USER)) return false;
  gs_touch_lastseen(gt_now());
  return true;
}

void     ui_input_flush(void)            { input_flush(); }
bool     ui_btn_down(uint8_t btn)        { return input_raw(btn); }
uint32_t ui_btn_hold_ms(uint8_t btn)     { return input_hold_ms(btn); }

// -----------------------------------------------------------------------------
//  THE SCREEN VIEW. One snapshot per frame, derived and never stored: the
//  smoothed care percentages this file already computes, the identity the Box
//  holds, and the two numbers spec section 8 asks for that Phases 3 and 4 will
//  fill in properly (hp_max is derived from the species base, and the XP curve
//  is a flat placeholder until P3-C2 lands XP_TABLE[31]).
// -----------------------------------------------------------------------------
static PebbleView s_view;

static const PebbleView* ui_fill_view(void) {
  memset(&s_view, 0, sizeof(s_view));
  const SimView* p = pet();
  if (!p) return &s_view;                        // present == 0

  s_view.present = 1;
  ui_pet_name(s_view.name, sizeof(s_view.name));
  s_view.genome     = p->genome;
  s_view.stage      = p->stage;
  s_view.minor_form = p->minor_form;
  s_view.pose       = home_pose(p);
  s_view.poop_count = p->poop_count;
  s_view.flags      = p->flags;
  s_view.age_s      = sim_age_s();
  gt_format_elapsed(s_view.age_s, s_view.age_txt, sizeof(s_view.age_txt));

  for (uint8_t i = 0; i < ST_COUNT; ++i) s_view.care_pct[i] = ui_stat_shown((StatId)i);
  s_view.mood_pct  = (uint8_t)sim_mood_score();
  s_view.mood_face = mood_of();

  const uint8_t slot = box_active();
  const PebbleInstance* pb = (slot == BOX_ACTIVE_NONE) ? nullptr : box_peek(slot);
  if (pb) {
    s_view.species_id = pb->species_id;
    s_view.level      = pb->level;
    s_view.xp         = pb->xp;
    s_view.hp_cur     = pb->hp_cur;
    const SpeciesDef* sp = species_get(pb->species_id);
    // hp_max = 10 + 2*base_hp + level (plan 1.5.1), recomputed and never
    // stored. With no species row there is nothing honest to show, so the
    // meter reports the current value as full rather than inventing a maximum.
    s_view.hp_max = sp ? (uint16_t)(10u + 2u * (uint16_t)sp->base_hp + (uint16_t)pb->level)
                       : pb->hp_cur;
  }
  if (s_view.level == 0) s_view.level = 1;
  s_view.xp_next = (uint16_t)PB_XP_PER_LEVEL_PLACEHOLDER;
  return &s_view;
}
