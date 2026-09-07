// =============================================================================
//  PEBBLEBOL - ui/ui.cpp
//  GAME_DESIGN 8.2 (global invariants) and 8.3 (per-screen gesture map), for
//  the screens that have not yet moved into the screen table: GAME, SOCIAL,
//  QR, EGG and the modal layer. Everything else is a ScreenDef row in
//  ui/screen_*.cpp, and what those rows need from the device - the animated
//  HOME stage, the diagnostics page, the clock, the simulation, the modals -
//  is bound and forwarded from section 20 of this file.
//
//  Invariants implemented literally (GAME_DESIGN 8.2):
//    1. GST_TAP_R == BACK on every screen (spec section 7: B is back/cancel).
//       Since P2-C11d this lives in app/input_router.cpp, and the screens that
//       answer B themselves say so with SF_OWNS_BACK rather than by name.
//    2. GST_LONG_BOTH == HOME from anywhere.
//    3. Every screen without SF_STICKY auto-returns to HOME after
//       UI_AUTORETURN_MS, with a 3 px countdown bar in the last
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
#include "../core/utf8.h"
#include "../app/onboarding.h"   // the first-boot step, read by ui_set_starter()
#include "../data/sprites.h"
#include "../data/balance.h"   // ACT_PLAY_MIN_ENERGY_PCT for the PLAY entry
#include "render.h"
#include "../hardware/input.h"
#include "../minigames/manager.h"
#include "../minigames/registry.h"
#include "../game/activity.h"    // act_take_dirty(): the other half of "cd"
#include "../game/cooldowns.h"   // cd_take_dirty(): the exploration commit
#include "../game/sim.h"
#include "../game/genome.h"
#include "../core/rng.h"
#include "../persistence/game_state.h"
#include "../persistence/save_manager.h"  // save_cooldowns / save_inventory (P5-C3)
#include "../hardware/kv_nvs.h"      // kv_error(), for the DIAG line
#include "../hardware/gametime.h"
#include "../hardware/audio.h"   // the P6-C1 tone engine: cues, never policy
#include "ceremony.h"  // the hatch / evolution show (P2-C11c)
#include "dialog.h"    // the CONFIRM / ALERT / HELP overlays (P2-C11c)
#include "../game/corruption.h"   // cor_left_s(): the STATUS readout
#include "screen_creator.h"   // CreatorInfo, for the radio seam below
#include "screen_diag.h"
#include "screen_evolution.h"
#include "screen_network.h"  // network_screen_busy(): the power ladder's `held` input
#include "screen_link.h"     // link_screen_busy() and the P7-C3 session seams
#include "../game/trade.h"           // the P7-C4 journal, driven from here
#include "../networking/trade_link.h" // TradeHooks: the screen's trade seam
#include "../networking/net.h"
#include "../networking/webui.h"      // web_pin() only - no network header comes with it
#include "../dev/godmode.h"    // GodEvt, god_active/handle/draw/entry_progress/marker
#include "pet_art.h"   // pet_species_name(): what the creature IS
#include "pet_view.h"  // PetView: what petfx and actfx are allowed to know
#include "petfx.h"      // the body's own presentation layer: floor, position, gaze
#include "actfx.h"      // the choreography of every action the player can take
#include "../game/box.h"          // the active slot: level, xp, hp for the view
#include "../game/box_sim.h"      // and what a swap does to sim's raw pointer
#include "../data/species_table.h" // and the base_hp hp_max is derived from
#include "../game/pebble.h"    // pebble_name_syllables(): the dynasty hash (P9-C4)
#include "../game/xp.h"        // the XP curve HOME draws and the award amounts
#include "../app/app.h"        // app_award_xp(): the one door for experience
#include "gfx.h"           // the header bar, the countdown and the list widgets
#include "screen.h"        // the ScreenDef table this file is being dissolved into
#include "../app/input_router.h"  // the section 7 grammar (P2-C11d)
#include "screen_error.h"  // the ERROR screen moved out first (P2-C11a)
// P2-C11b moved HOME, MENU, CARE, PLAY, both STATUS pages, SETTINGS and TIME
// out. What is left here is the DEVICE half each of them needs - the animated
// body layer, the diagnostics page, the minigame start - which this file binds
// and hands over. It draws none of them.
#include "screen_battle.h"   // BT_ENTRY_*, BT_DIAG_SEED
#include "screen_box.h"      // box_screen_to_list(), for the release commit
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
// UI_CONTENT_Y moved to ui/screen.h (P3-C4a): the minigame draw halves need it
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

// ---- screen / navigation ----------------------------------------------------
// The current screen, the back stack and the two navigation clocks live in
// app/state_machine.cpp now (P2-C11a); reach them through sm_current(),
// sm_idle_ms() and sm_screen_ms().
static uint32_t s_wiggle_ms   = 0;
// The slot the two Box release dialogs are about. Written only by
// ui_box_release(), read only by the CFM_BOX_REL2 commit - and box_release()
// re-checks it there, so a stale value can still destroy nothing.
static uint8_t  s_box_slot    = BOX_ACTIVE_NONE;
static uint8_t  s_fps_want    = FPS_NORMAL;
static uint8_t  s_god_prog    = 0;      // god_entry_progress(), 0..100

// ---- the last accepted action, replayed by the MENU's DBL_R -----------------

// ---- modal layer ------------------------------------------------------------
// The CONFIRM dialog, the ALERT overlay and the HELP strip are ui/dialog.cpp's
// since P2-C11c, queue and cursor included. This file opens them, answers the
// commit callback and draws them into the frame.

// ---- toast ------------------------------------------------------------------
static char     s_toast[72];
static uint32_t s_toast_ms    = 0;

// ---- home -------------------------------------------------------------------
static uint32_t s_mimo_ms     = 0;
static uint32_t s_evolve_ms   = 0;
// When the section 18 evolution question was last put. 0 = never asked this
// session. See the offer in ui_service() and UI_EVOLVE_ASK_MS.
static uint32_t s_evo_ask_ms  = 0;

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

// ---- the ceremony, the egg, the QR symbol and the peer list -----------------
// All four are other files' now (P2-C11c): ui/ceremony.cpp, and the pure
// screens ui/screen_evolution.cpp, ui/screen_creator.cpp and
// ui/screen_link.cpp. This file keeps only the ORDERING of a birth, in
// ceremony_start().

// ---- GAME -------------------------------------------------------------------
// The games moved out to src/minigames/ (P3-C4a). What used to be here - a
// MinigameState struct, three games and their drawing, all interleaved with
// this file's chrome - is now minigames/minigame.{h,cpp} (the contract),
// minigames/manager.{h,cpp} (the sequence, PURE so the "one report per game"
// property is testable), minigames/games/*_logic.cpp (pure) and
// minigames/games/*_draw.cpp (device). ui.cpp keeps only the SCREEN.

// =============================================================================
//  3. SMALL HELPERS
// =============================================================================

static inline uint32_t now_ms(void) { return millis(); }
static inline uint32_t since(uint32_t t) { return (uint32_t)(millis() - t); }
static inline const SimView* pet(void) { return sim_view(); }

// ---- the BODY's view (ui/pet_view.h) ----------------------------------------
// petfx and actfx read a PetView and nothing else since P2-C11c, so this file
// is what fills one: the simulation owns the RAM half (pose, poop, the alert
// flags), the Box owns the stored half (the identity petfx seeds its automaton
// from, and the level). One static, refilled per call and never stored.
static PetView s_body_view;

static const PetView& body_view(const SimView& p, uint8_t pose) {
  pet_view_fill_sim(s_body_view, p, pose);
  const uint8_t slot = box_active();
  pet_view_attach(s_body_view, (slot == BOX_ACTIVE_NONE) ? nullptr : box_peek(slot));
  return s_body_view;
}

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

// px_hline() went with the SALTO minigame it drew the ground line for.

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
//
// THE HASH AND THE INDEX ARITHMETIC LEFT THIS FILE AT P9-C4 and live in
// game/pebble.cpp, which a host binary can link; this file is now the LOOKUP
// and nothing else. What that bought is written in pebble.h: the algorithm had
// no test and could not have one (ui.cpp includes <Arduino.h>), so
// tests/test_persistence.cpp carried a hand-copied second implementation of the
// same hash and changing a constant here failed nothing anywhere.
//
// snprintf IS GONE TOO, and that is a fix rather than a move. It truncated on a
// BYTE boundary, and the syllables are UTF-8 with a Latin-1 repertoire, so the
// widest name in the repertoire - "Ña" + "rrón" - is six glyphs and EIGHT bytes:
// a buffer sized in characters cut a two-byte sequence in half and handed
// drawUTF8() a broken lead byte. pebble_name_join() truncates on a CHARACTER
// boundary and always yields a PREFIX of the whole name.
void ui_name_for(uint32_t lineage_id, uint8_t generation, char* out, size_t cap) {
  if (!out || cap == 0) return;
  uint8_t syl[2];
  pebble_name_syllables(lineage_id, generation, syl);
  const uint16_t room = (cap > 0xFFFFu) ? 0xFFFFu : (uint16_t)cap;
  (void)pebble_name_join(S_SYL_A(syl[0]), S_SYL_B(syl[1]), out, room);
}

// THE WORD BESIDE THE BODY, in the order the player earns it (P4-C4a).
//
//   1. the nickname the owner typed, if there is one - nothing outranks that;
//   2. THE SPECIES the Pebble is, from the roster's own Spanish name
//      (STR_SPC_NAME_1..36). This is the half that closes the P3-C3 obligation
//      on the text side: HOME used to show the dynasty syllables, which are a
//      pure function of (lineage_id, generation) and therefore say the SAME
//      word before and after an evolution. Paketo becoming Fragmar has to
//      change the name as well as the pixels;
//   3. the dynasty syllables, unchanged, for a Pebble with no species row -
//      an unfiled egg, a creator custom (200..209), or a save from a build with
//      more families than this one. ui_name_for() keeps its job; it is the
//      FALLBACK now rather than the answer.
// THE LADDER IS WRITTEN ONCE, IN THE STORED ENCODING, AND CROSSED ONCE - which
// is the P10-C6 fix and the reason this is two functions rather than one.
//
// P10-C4 made ui_pet_name() emit UTF-8, correctly, because everything that
// DRAWS a name goes through drawUTF8(). But one caller does not draw: screen_
// link.cpp's fill_self() puts this name into DiscBeacon.name, which is LATIN-1
// by wire contract - networking/discovery.cpp's name_ok() refuses 0x80..0x9F,
// and every Latin-1 byte in 0xC0..0xDF (exactly the uppercase accent set the
// first-boot naming ring can type) becomes 0xC3 followed by a byte inside that
// window. disc_encode() answered DE_NAME and link_service() dropped the frame,
// so a device called "NUNO" with an n-tilde emitted NO BEACON AT ALL: invisible
// to every peer, and because the peer never saw it the link could not be
// offered from either side. LINK, trade and P2P battle, all dead, silently -
// beacons_tx stayed 0 and nothing reported it.
//
// It was invisible to the suite for the phase-9 reason: no host binary compiles
// this file, and tests/fakes/link_fake.cpp stood in for this function with the
// PRE-P10-C4 body, so every discovery test drove the old one. The fake now
// implements both halves the way this file does, and there is a case that puts
// an accented name on the air through the real disc_encode().
static void pet_name_stored(char* out, size_t cap) {
  if (!out || cap == 0) return;
  const uint16_t c16 = (uint16_t)((cap > 0xFFFFu) ? 0xFFFFu : cap);
  // Rung 1: Config.pet_name is already the stored encoding - a raw copy.
  if (s_cfg && s_cfg->pet_name[0] != '\0') {
    size_t i = 0;
    while (s_cfg->pet_name[i] != '\0' && i + 1u < cap) { out[i] = s_cfg->pet_name[i]; ++i; }
    out[i] = '\0';
    return;
  }
  const SimView* p = pet();
  // Rungs 2 and 3 come from the string table and the dynasty generator, which
  // are UTF-8; they cross BACK so the ladder answers one encoding whichever
  // rung it lands on. A caller that got UTF-8 from one rung and Latin-1 from
  // another would be a bug that only shows up for players with no set name.
  char utf[64];
  if (!p) {
    snprintf(utf, sizeof utf, "%s", S(STR_EGG_TITLE));
  } else {
    const uint8_t slot = box_active();
    const PebbleInstance* pb = (slot == BOX_ACTIVE_NONE) ? nullptr : box_peek(slot);
    const char* species = pb ? pet_species_name(pb->species_id) : nullptr;
    if (species) snprintf(utf, sizeof utf, "%s", species);
    else         ui_name_for(p->genome.lineage_id, p->genome.generation, utf, sizeof utf);
  }
  (void)u8_to_latin1(out, c16, utf);
}

// THE STORED FORM. For the wire and for anything else that is not a draw.
void ui_pet_name_latin1(char* out, size_t cap) { pet_name_stored(out, cap); }

// THE DRAWN FORM. Stored, then the one crossing (core/utf8.h).
void ui_pet_name(char* out, size_t cap) {
  if (!out || cap == 0) return;
  char stored[64];
  pet_name_stored(stored, sizeof stored);
  (void)u8_from_latin1(out, (uint16_t)((cap > 0xFFFFu) ? 0xFFFFu : cap), stored);
}

// GAME_DESIGN 6.3's score -> face table. The ladder lives in ui/pet_view.cpp
// since P2-C11c - it is a render decision, and the web mirror that used to own
// it was never the right home for one.
static uint8_t mood_of(void) { return pet_mood_index((uint8_t)sim_mood_score()); }

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
// The power ladder's DIM rung, an override on top of both of the above.
static uint8_t s_pwr_dim      = 0;

static void bright_service(void) {
  const SimView* p = pet();
  uint8_t want = (p && (p->flags & PF_ASLEEP)) ? (uint8_t)OLED_CONTRAST_DIM
                                               : s_bright_base;
  // THE POWER LADDER'S DIM RUNG (P6-C3), and it is a third input to the same
  // decision rather than a fourth writer of the register. It is about the
  // PLAYER being away where the two above are about the pet and the setting,
  // so when both are true the panel takes the lower of them - a sleeping pet
  // on a device nobody has touched for a minute must not be BRIGHTER than
  // either case alone.
  if (s_pwr_dim && want > (uint8_t)PWR_DIM_CONTRAST) want = (uint8_t)PWR_DIM_CONTRAST;
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
  // u8_cat() and not snprintf("%s"): the copy is cut on a CODEPOINT boundary,
  // so a line longer than the buffer cannot leave half a sequence behind for
  // drawUTF8() (core/utf8.h).
  s_toast[0] = '\0';
  (void)u8_cat(s_toast, (uint16_t)sizeof s_toast, txt);
  s_toast_ms = now_ms();
}

void ui_toast(uint16_t str_id) {
  if (str_id >= (uint16_t)STR_COUNT) return;
  toast_text(S(str_id));
}

static void cfg_persist(bool announce) {
  if (!s_cfg) return;
  s_cfg->saved_epoch = gt_now();
  gs_save_cfg(*s_cfg);
  if (announce) ui_toast(STR_SET_SAVED);
}

void ui_alert(AlertId a) { dialog_alert((uint8_t)a); }

// =============================================================================
//  5. ACTIONS
// =============================================================================
// The care actions experience is paid for. SLEEP is a toggle: it changes what
// the Pebble is doing, not how well it is being looked after.
static bool act_earns_xp(ActionId a) {
  switch (a) {
    case ACT_FEED_MEAL:
    case ACT_FEED_SNACK:
    case ACT_CLEAN:
    case ACT_MEDICINE:
    case ACT_PLAY:
    case ACT_PET:      return true;
    default:           return false;
  }
}

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
    if (a == ACT_PET) s_mimo_ms = now_ms();
    // Only a SUCCESSFUL action gets a film. A rejected one (cooldown, full,
    // asleep) keeps its toast and nothing else, which is the honest
    // reading: nothing happened to the pet, so nothing happens on screen.
    if (had) actfx_begin((uint8_t)a, body_view(before, pet_pose_of(before.flags)));
    if (r.str_id) ui_toast(r.str_id);
    // EVERY care action pays XP, not just feeding. A meal is refused above 90 %
    // satiety and satiety only falls at 4,200 milli/h, so FEED_MEAL comes round
    // about once every 2.4 h and could never spend an hourly budget by itself;
    // the two toggles are excluded because they care for nothing. The hourly
    // ceiling inside app_award_xp() is what makes a spammable action harmless.
    if (act_earns_xp(a)) {
      (void)app_award_xp(xp_care_action_amount(), XP_SRC_CARE);
      // AND THE SAME ACTION IS THE ACTIVITY SCORE'S "interactions" TERM (spec
      // section 25). The same act_earns_xp() gate, so the two toggles that care
      // for nothing score nothing here either, and the same argument for why a
      // spammable action is harmless: the reward is capped by ACT_CAP_INTERACT
      // per day on top of the hourly ceiling that already bounds the XP.
      app_note_interaction();
    }
    const SimView* p = pet();
    if (p) gs_save_active(true);
  } else {
    // A REFUSAL IS WHERE A CUE EARNS ITS KEEP: the toast says why, but the
    // player pressing A on a pet in a pocket only finds out that nothing
    // happened. One SFX_BUZZ for every refusal, whichever branch names it -
    // armed once here rather than once per branch, so a fourth refusal reason
    // cannot arrive silent.
    audio_play(SFX_BUZZ);
    if (r.err == AERR_COOLDOWN && r.cooldown_s) {
      // The base line has no {t}; the countdown is appended so the wait is honest.
      char buf[64];
      snprintf(buf, sizeof(buf), "%s %u s", S(STR_AERR_COOLDOWN), (unsigned)r.cooldown_s);
      toast_text(buf);
    } else {
      ui_toast(r.str_id ? r.str_id : (uint16_t)(STR_AERR_NONE + r.err));
    }
  }
  rd_request_frame();
  return ok;
}

// =============================================================================
//  6. NAVIGATION
// =============================================================================
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
  if (ceremony_active()) return FPS_NORMAL;
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

// The two parts of a screen change that are still this file's: the
// interpolators the shared widgets animate, and the entry dissolve.
// state_machine.cpp calls them in that order (ui.h).
void ui_nav_reset(void) {
  dialog_close();
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
// nothing else. One thing therefore never navigates, deliberately: a REJECTED
// action, which has no film to watch - including the first nudges against a
// sleeping pebble. The caller keeps whatever destination it had, and the toast
// explains itself where the player is standing.
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
  const uint8_t m = dialog_modal();
  if (m == MODAL_ALERT)   return SCR_ALERT;
  if (m == MODAL_CONFIRM) return SCR_CONFIRM;
  return sm_current();
}

bool ui_input_locked(void) {
  // The birth ceremony is unskippable. It is also short enough that locking the
  // buttons costs the player nothing.
  return ceremony_active();
}

// =============================================================================
//  7. SHARED CHROME
// =============================================================================

// The header bar is gfx_widgets.cpp's now, so the migrated screens and GAME
// draw the identical thing. The countdown wrapper went with the last screen in
// this file that was not sticky (P2-C11c): every migrated row calls
// gfx_countdown(ui_idle_ms()) itself, and GAME never counts down.
static void draw_header(const char* title, const char* tag) {
  gfx_header(title, tag);
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
  return s != SCR_GAME && s != SCR_DIAG;
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

// THE TOAST IS gfx_banner() NOW - the same picture as the HELP strip, drawn by
// the same code, in a translation unit a host binary compiles.
//
// It was eleven pixels of slab and one CENTRED, UNBOUNDED line. Measured over
// the fifty distinct ids that reach ui_toast(), FOUR are wider than the panel at
// GF_BODY, the worst being STR_BOX_NO_RELEASE_ACTIVE at 185 px - and because
// this file includes Arduino.h, no golden in this repository had ever drawn a
// toast at all. Both halves of that are fixed here: the slab grows to hold the
// line, and tests/test_screens.cpp now renders EVERY toast id through this
// exact code.
static void draw_toast(void) {
  if (s_toast[0] == '\0' || since(s_toast_ms) > UI_TOAST_MS) return;
  (void)gfx_banner(s_toast);
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
// for a two-second animation. Everything else defers to pet_pose_of() so the
// phone and the panel share one table.
//
// The pose window now belongs to actfx, not to a UI_EAT_POSE_MS constant: it is
// the length of the FILM, so the pose and the bowl standing on the floor cannot
// disagree about when the meal ended. A sleeping pet keeps its own pose whatever
// the choreography thinks - except that it never asks, because
// ACT_SLEEP_TOGGLE's film is carried entirely by actfx_body_dy().
static uint8_t home_pose(const SimView* p) {
  if (!p) return POSE_IDLE;
  const uint8_t base = pet_pose_of(p->flags);
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
  petfx_draw_body(body_view(*p, home_pose(p)), home_pose(p), frame, dy, dx);

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


// =============================================================================
//  11. GAME - the screen. The games themselves live in src/minigames/.
//
//  This section used to be three games, their drawing, their scoring and their
//  chrome, all in one place. P3-C4a moved everything but the SCREEN out:
//  minigames/manager.cpp runs the sequence and reports each result exactly
//  once, minigames/registry.cpp draws whichever game is live. What is left
//  here is the frame around it and the two ways out.
//
//  Canonical for minigames_won (BRIEF 1.6): every result still goes through
//  sim_apply_play_result(), which owns the shared cooldown and hourly ledger,
//  so no surface can be farmed. It is reached through the manager's ONE report
//  callback, bound below - which is the point of the extraction: there is now
//  exactly one line in the firmware that can report a minigame result.
//
//  INPUT: a game is judged on the PRESS, not on the gesture the press turns
//  into, so the run loop reads input_pressed_edge() - the debounced press
//  edge, consumed once - and ignores the GST_TAP_* that arrives at release.
//  Since P3-C4a that release is only ~25 ms behind the press rather than
//  280 ms, but the distinction still matters: "press A as the indicator
//  crosses the zone" is timed from the button going DOWN.
// =============================================================================

// THE one reporting path. Bound into the manager by ui_begin().
static void game_report(uint8_t /* game_id */, uint16_t permille) {
  ActionResult r;
  if (permille > MG_SCORE_MAX) permille = MG_SCORE_MAX;
  sim_apply_play_result(permille, r);
  (void)app_award_xp(xp_minigame_amount(permille), XP_SRC_MINIGAME);
  if (pet()) gs_save_active(true);
}

static uint32_t s_game_ms = 0;        // last service instant, for the real dt

static void game_service(void) {
  if (!mgr_active()) { if (sm_current() == SCR_GAME) nav_back(); return; }

  const uint32_t t  = now_ms();
  const uint32_t dt = s_game_ms ? (uint32_t)(t - s_game_ms) : 0u;
  s_game_ms = t;

  // A MODAL IS A PAUSE, AND THE PAUSE HAS TO REACH THE GAME. This hook is
  // called by sm_service() every frame regardless of what is on top of the
  // screen, and the confirm layer collects GESTURES - so without this the quit
  // confirm went up while the run underneath it kept stepping, kept scoring and
  // kept eating the raw press edges that the player was using to ANSWER the
  // dialog. Returning here (after s_game_ms has been moved forward, so the
  // paused time is dropped rather than delivered as one enormous dt on resume)
  // is what makes "PAUSA" true. The two edges are drained rather than left
  // latched, because input_pressed_edge() holds a press until someone consumes
  // it and the presses spent on the dialog belong to the dialog.
  if (dialog_modal() != MODAL_NONE) {
    (void)input_pressed_edge(INPUT_BTN_L);
    (void)input_pressed_edge(INPUT_BTN_R);
    return;
  }

  // Presses first, so a button pressed in the same frame the game ends still
  // counts; mg_press() refuses once the game is over.
  if (mgr_phase() == MGR_RUN || mgr_phase() == MGR_NEXT) {
    if (input_pressed_edge(INPUT_BTN_L)) mgr_press(MG_SIDE_L);
    if (input_pressed_edge(INPUT_BTN_R)) mgr_press(MG_SIDE_R);
  }

  // A note the pure logic left for us (PING's "too soon"): the presentation
  // layer toasts it, because logic that could toast would not be pure.
  const MgCtx& c = mgr_ctx();
  if (c.note) { ui_toast(c.note); const_cast<MgCtx&>(c).note = 0u; }

  if (!mgr_tick(dt) && sm_current() == SCR_GAME) nav_back();
}

static void draw_game(void) {
  const MgLogic* g = mgr_logic();
  if (g == nullptr) return;

  const uint16_t sc = mgr_ctx().score;
  char tag[12];
  snprintf(tag, sizeof(tag), "%u", (unsigned)(sc / 10u));
  draw_header(S(g->name_idx), (mgr_phase() == MGR_RUN) ? tag : nullptr);

  switch (mgr_phase()) {
    case MGR_INTRO: {
      const bool go = mgr_phase_ms() >= MGR_GO_MS;
      rd_text_center((int16_t)(UI_CONTENT_Y + 16), RD_FONT_HEAD,
                     S(go ? STR_GM_GO : STR_GM_READY));
      rd_text_center((int16_t)(UI_CONTENT_Y + 32), RD_FONT_BODY, S(g->hint_idx));
      break;
    }
    case MGR_RESULT: {
      rd_text_center((int16_t)(UI_CONTENT_Y + 16), RD_FONT_HEAD,
                     S(sc >= 500u ? STR_GM_WIN : STR_GM_LOSE));
      char buf[24];
      snprintf(buf, sizeof(buf), "%s %u", S(STR_GM_SCORE), (unsigned)(sc / 10u));
      rd_text_center((int16_t)(UI_CONTENT_Y + 32), RD_FONT_BODY, buf);
      break;
    }
    case MGR_NEXT: {
      // The 1.2 s card between games. A continues, B stops - and it says so,
      // because GAME_DESIGN 8.3 forbids hidden gestures.
      rd_text_center((int16_t)(UI_CONTENT_Y + 20), RD_FONT_HEAD, S(STR_GM_NEXT));
      char buf[24];
      snprintf(buf, sizeof(buf), "%u/%u", (unsigned)(mgr_index() + 1u),
               (unsigned)mgr_count());
      rd_text_center((int16_t)(UI_CONTENT_Y + 36), RD_FONT_BODY, buf);
      rd_affordance(S(STR_AF_OK), S(STR_AF_BACK));
      return;
    }
    case MGR_TOTAL: {
      char buf[24];
      snprintf(buf, sizeof(buf), "%s %u", S(STR_GM_SCORE),
               (unsigned)(mgr_total_score() / 10u));
      rd_text_center((int16_t)(UI_CONTENT_Y + 24), RD_FONT_HEAD, buf);
      break;
    }
    default:
      mg_draw_current();      // the run frame, from minigames/registry.cpp
      break;
  }
  // DURING A RUN BOTH BUTTONS ARE PLAY INPUTS, in all six games, so the strip
  // may not offer B as PAUSA alone - the pause is B HELD, in the same tap/hold
  // shape the lists already write "ATRAS/SEL" in. Outside the run B is the way
  // out and nothing else. See handle_game() for the collision this closes.
  if (mgr_phase() == MGR_RUN)
    rd_affordance(S(STR_GM_AF_PLAY), S(STR_GM_AF_PLAY_PAUSE));
  else
    rd_affordance(nullptr, S(STR_AF_PAUSE));
}

// GAME_DESIGN 8.3: "games must not have hidden gestures."
//
// THE PAUSE IS ON HOLD_R DURING A RUN, and that is a collision fix rather than
// a preference. game_service() feeds input_pressed_edge(INPUT_BTN_R) straight
// into mgr_press() while in_on_release() turns the same physical press into a
// GST_TAP_R - so with the pause on the tap, ONE press of B both played the game
// and opened a modal over it. PING has been able to trip that since P3-C4a
// whenever its target was R; P3-C4b would have given four more games the same
// problem, with B as a play input in every one.
//
// The OTHER half of that bug was that the run did not actually stop: the modal
// went up and the game kept ticking, scoring and consuming press edges behind
// it, because dialog_input() consumes gestures and never touches the raw edge.
// That half is fixed in game_service() above, which is where the clock is.
//
// GST_HOLD_R is the natural home: it is already SELECT on every list screen,
// and in_on_release() emits nothing at all after a HOLD has fired, so a held B
// cannot also arrive as a tap. The press edge underneath it still reaches the
// game as one press, which is correct - the player did press the button.
static void handle_game(Gesture g) {
  if (mgr_phase() == MGR_RUN) {
    if (g == GST_HOLD_R) dialog_open_confirm(CFM_QUIT_GAME, STR_CF_QUIT_GAME);
    return;
  }
  if (g == GST_TAP_R) mgr_back();
}

// =============================================================================
//  12. STATUS_A / STATUS_B moved to ui/screen_status.cpp (P2-C11b). The god
//      entry bar the genome page paints is reported through ui_god_progress().
// =============================================================================

// =============================================================================
//  13. SOCIAL is gone. ui/screen_link.cpp is the LINK placeholder that replaced
//      it (P2-C11c): the BLE peer browser it used to be was built on a
//      transport this firmware no longer uses (D2 chose ESP-NOW) and on a
//      mating protocol that was removed before Phase 2. Nothing here holds a
//      radio any more, and what LINK is FOR arrives in P7-C2.
// =============================================================================
// =============================================================================
//  14. SETTINGS moved to ui/screen_settings.cpp (P2-C11b). The five lines of
//      its "Acerca de" page are device facts, so they are still produced here
//      and handed over by ui_info_lines().
// =============================================================================

// =============================================================================
//  15. QR is gone. ui/screen_creator.cpp is the CREATOR screen that replaced it
//      (P2-C11c): same 62 px symbol box, same PIN, and it still owns the Wi-Fi
//      station on entry and gives it back on the way out - but the symbol and
//      the modules are painted through gfx.h now, so the whole screen is
//      snapshot-tested on the host. What the page it points at will SERVE is
//      Phase 8. ui_creator_info() / ui_creator_radio() in section 20 are the
//      two seams it reaches the radio through.
// =============================================================================
// =============================================================================
//  15b. TIME ENTRY moved to ui/screen_time.cpp (P2-C11b). The clock itself is
//       still reached through gametime.h, from the ui_get_clock() /
//       ui_set_clock() seams in section 19.
// =============================================================================

// =============================================================================
//  16. THE CEREMONY moved to ui/ceremony.cpp and the EGG screen to
//      ui/screen_evolution.cpp (P2-C11c). The phase machine is parameterised
//      by CeremonyKind now, so P3-C3 drives the same show for an evolution,
//      and the screen that hosts it is a pure translation unit with the
//      ceremony bound into it. What is left here is the ORDERING argument that
//      cannot move: the model change and its flush happen BEFORE the first
//      frame, in ceremony_start() below.
// =============================================================================

// The body ui/ceremony.cpp draws, through the same view petfx reads.
static const PetView* ceremony_body(void) {
  const SimView* p = pet();
  if (!p) return nullptr;
  return &body_view(*p, POSE_IDLE);
}

// The pure EVOLUTION screen's ceremony hook (screen_evolution.h): draw one
// ceremony frame, or answer false so the screen draws the incubator instead.
static bool evo_ceremony_frame(void) { return ceremony_draw(now_ms()); }

// ONE place every GodEvt is acted on, whether a GESTURE raised it (god_handle)
// or a TYPED SERIAL LINE did (god_take_evt, P10-C1). Two switches would be two
// places to forget an arm, and the serial half is the half no host test can
// reach. Returns true when the console wants the SCREEN closed.
static bool apply_god_evt(GodEvt e, uint8_t arg) {
  switch (e) {
    case GOD_EVT_LEAVE:
      return true;
    // test_battle / start_battle (spec sections 49 and 66). The battle screen
    // is PUSHED on top of the console rather than replacing it, so B walks back
    // into the god menu the operator started from and god mode stays on - which
    // is the point: the heap panel is where the per-frame delta this entry
    // exists to measure is read back.
    case GOD_EVT_BATTLE:
      ui_start_battle(BT_ENTRY_DIAG);
      return false;
    // spec section 66's start_creator and scan_wifi. Both are PUSHES for the
    // same reason: the console cannot navigate, and the radio and the scan job
    // have exactly one owner each (ui/screen_network.cpp), so the console asks
    // for the screen instead of starting a second scan behind its back.
    case GOD_EVT_CREATOR:
      nav_push(SCR_CREATOR);
      return false;
    case GOD_EVT_SCAN:
      nav_push(SCR_NETWORK);
      return false;
    // spec section 49's test_minigame. ui_start_minigame() applies the SAME
    // cooldown and energy refusals the PLAY menu does; a diagnostic that
    // bypassed them would be testing a path no player can reach.
    case GOD_EVT_MINIGAME:
      ui_start_minigame(arg);
      return false;
    case GOD_EVT_WIPED: {
      if (s_cfg) gs_cfg_defaults(*s_cfg);
      const SimView* np = pet();
      if (np) petfx_reset(body_view(*np, POSE_IDLE));  // a different animal entirely
      s_stat_ok = 0;
      sm_replace_root(SCR_EVOLUTION);
      return false;                 // already re-rooted; do not send HOME on top
    }
    default:
      return false;                 // handled inside the console
  }
}

// The pure DIAG screen's input hook (screen_diag.h). Returns true when the
// console wants the SCREEN closed - which is not the same as the console being
// switched off: watching an accelerated life on the ordinary screens is the
// entire point of it.
static bool diag_gesture(Gesture g) {
  return apply_god_evt(god_handle(g), 0);
}

// Idempotent, and it has to be: the manual rub path calls sim_hatch() itself
// and the simulation then reports SIM_EV_HATCHED on the NEXT logic tick, so
// this is reached twice for one birth. ceremony_begin() holds the guard.
static void ceremony_start(uint8_t kind) {
  const SimView* p = pet();
  if (!p) return;
  // THE GATE IS KIND-AWARE. A hatch is only ever the moment a baby appears, so
  // it still demands STAGE_BABY. An evolution happens to a creature that is
  // already standing there and may be any age, so it demands only that there IS
  // one and that it is not still inside its shell.
  if (kind == CEREMONY_HATCH) {
    if (p->stage != STAGE_BABY) return;
  } else if (kind == CEREMONY_EVOLVE) {
    if (p->stage == STAGE_EGG) return;
  } else {
    return;
  }

  // Commit FIRST: everything below is presentation. EXCEPT for an evolution,
  // which app_evolve_active() has already applied AND flushed before calling
  // in - it has to, because its return value is what promises the change
  // survives a brownout mid-show. Writing again here would be a second forced
  // NVS write for one event, for nothing.
  if (kind != CEREMONY_EVOLVE) gs_save_active(true);

  if (!ceremony_begin(kind, now_ms())) return;

  // The show is armed BEFORE the navigation: sm_replace_root() applies the
  // frame rate on arrival, and ui_fps() answers FPS_NORMAL only once
  // ceremony_active() is true. It also runs the leave hook of whatever screen
  // the ceremony arrived on - which is what drops an in-flight minigame.
  dialog_reset();
  s_toast[0]    = '\0';
  s_absence_ms  = 0;
  sm_replace_root(SCR_EVOLUTION);
  s_trans_ms    = 0;           // the ceremony owns the frame: no dissolve on top
  actfx_cancel();              // whatever the previous animal was doing, it is over
  petfx_reset(body_view(*p, POSE_IDLE));   // the body about to appear is brand new
  petfx_freeze(1);             // and it must be born in the centre, not mid-walk
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
void ui_confirm_wipe(void) { dialog_open_confirm(CFM_WIPE1, STR_CF_WIPE); }

void ui_note_recovered(void) {
  s_stat_ok = 0;                 // show the recovered pet's truth at once
  const SimView* p = pet();
  if (p) petfx_reset(body_view(*p, POSE_IDLE));
  sm_replace_root(SCR_HOME);
}

// =============================================================================
//  17. CONFIRM / ALERT overlays
// =============================================================================
// The dialog layer collects the answer; committing needs the simulation, the
// Box and flash, so it comes back here. Bound in ui_begin().
static void dialog_commit(uint8_t which) {
  switch (which) {
    case CFM_QUIT_GAME:
      // Quitting is a result of whatever was earned so far, reported through
      // the manager's ONE path like every other ending. It used to zero the
      // score and call game_finish() here, which was a second place in the
      // firmware that could report a minigame.
      mgr_back();
      break;
    case CFM_MEDICINE: act_and_show(ACT_MEDICINE); break;   // BRIEF D
    // Spec section 18. app_evolve_active() performs the change and flushes it -
    // the Pebble AND the nvs2 checkpoint - before it returns, so by the time
    // ceremony_start() arms the show there is nothing left to lose to a
    // brownout. That order is ui/ceremony.h's argument and it is why the call
    // is here and not after the show.
    case CFM_EVOLVE:
      if (app_evolve_active()) {
        ui_toast(STR_RX_EVOLVE);          // cleared by the show, as a hatch's is
        ceremony_start(CEREMONY_EVOLVE);
      }
      break;
    case CFM_WIPE1:    dialog_open_confirm(CFM_WIPE2, STR_CF_WIPE2); break;  // two dialogs
    // The Box release, behind the same two dialogs and for the same reason:
    // it is the one action in the game that destroys a Pebble (spec section 9,
    // invariant B4).
    case CFM_BOX_REL1: dialog_open_confirm(CFM_BOX_REL2, STR_BOX_REL_Q2); break;
    case CFM_BOX_REL2: {
      if (box_release(s_box_slot, true)) {
        gs_save_slot(s_box_slot, true);
        gs_save_box();
        // The action list this came from is now about an EMPTY slot, so send
        // the screen back to the ten rows rather than leave VIEW / ACTIVATE /
        // SWAP / RELEASE offered for a Pebble that no longer exists.
        box_screen_to_list();
        ui_toast(STR_BOX_RELEASED);
      } else if (s_box_slot == box_active()) {
        ui_toast(STR_BOX_NO_RELEASE_ACTIVE);
      } else {
        // Out of range, or a slot that holds nothing: "you cannot release the
        // one you are carrying" would be the wrong explanation for both.
        ui_toast(STR_BOX_EMPTY);
      }
      break;
    }
    case CFM_WIPE2: {
      gs_factory_reset();
      if (s_cfg) { gs_cfg_defaults(*s_cfg); gs_save_cfg(*s_cfg); }
      const Genome g0 = genome_genesis();
      sim_new_pet(g0, gt_now(), 0);
      const SimView* p = pet();
      if (p) { gs_save_active(true); petfx_reset(body_view(*p, POSE_IDLE)); }
      s_stat_ok  = 0;                // a wiped device shows the truth at once
      err_set_kind(ERRK_NONE);       // the save the ERROR screen was about is gone
      sm_replace_root(SCR_EVOLUTION);
      break;
    }
    default: break;
  }
}

// =============================================================================
//  18. THE GAME ROW (ui.h, retired by P3-C4)
//      SCR_GAME's five hooks. The minigames themselves are still in this file;
//      the table is what dispatches to them, exactly as it does for every
//      other state.
// =============================================================================
void ui_game_enter(void) {
  // Nothing: game_start() is what arms a minigame, and it runs before the
  // navigation that brings this screen up.
}

void ui_game_update(uint32_t /* now_ms */) { game_service(); }
void ui_game_render(void)                  { draw_game(); }
void ui_game_input(Gesture g)              { handle_game(g); }

void ui_game_leave(void) {
  // Leaving the screen by ANY route abandons the run. This used to call
  // sim_apply_play_result() itself, which made it a second reporting path
  // guarded only by a phase check - two exits, two chances to count a game
  // twice in minigames_won and in the XP ledger. mgr_abort() reports the
  // running game exactly once and is a no-op if it already has.
  mgr_abort();
  s_game_ms = 0;
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

void ui_note_power_dim(bool on) {
  const uint8_t want = on ? 1u : 0u;
  if (want == s_pwr_dim) return;
  s_pwr_dim = want;
  bright_service();
}

// BOTH radio screens since P7-C2. A single-screen answer here would have been
// a gate that stopped biting the moment a second screen took the radio, which
// is the shape of defect this project keeps finding: the NETWORK screen's scan
// and the LINK screen's discovery job and session both hold it.
bool ui_radio_job_busy(void) {
  // ALL THREE RADIO OWNERS, AND THE THIRD WAS MISSING UNTIL P10-C6. The
  // creator portal was not in this predicate, so the ladder reached PWR_IDLE
  // at 120 s and pwr_hook_release() navigated home - tearing the access point
  // down under a phone that was still drawing on it, 180 s before D7's own
  // timer would have. See ui/screen_creator.h for the whole account; the gate
  // in tools/check.sh is on this line naming all three.
  return network_screen_busy() || link_screen_busy() || creator_screen_busy();
}

// Defined with the rest of the seams in section 20; ui_begin() binds it.
static const PebbleView* ui_fill_view(void);

void ui_begin(void) {
  mgr_bind_report(game_report);   // THE one path a minigame result can take
  mgr_abort();                    // a wipe must not leave a run half-played
  // The screens P2-C11b migrated read the pet through this snapshot and draw
  // the animated stage through the bound layer. Both bindings are re-made on
  // every ui_begin(), which is also what a factory reset runs.
  ui_bind_view(&ui_fill_view);
  home_bind_body(&home_body, &home_leave_layer);
  // The three layers a pure screen cannot link against: the ceremony's panel
  // registers, the developer console, and the body the ceremony draws.
  ceremony_bind_body(&ceremony_body);
  evo_bind_ceremony(&evo_ceremony_frame);
  diag_bind(&god_active, &god_draw, &diag_gesture);
  sm_begin();
  dialog_bind_commit(&dialog_commit);
  dialog_reset();
  s_toast[0]    = '\0';
  s_absence_ms  = 0;
  s_evo_ask_ms  = 0;                 // a boot or a wipe re-offers a pending evolution
  s_stat_ok     = 0;                 // boot: show the truth, do not animate to it
  ceremony_reset();
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
  if (p) petfx_reset(body_view(*p, POSE_IDLE));  // AFTER petfx_begin(), before a draw
  if (p && p->stage == STAGE_EGG) { ui_goto(SCR_EVOLUTION); return; }
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
    // (one tick late) from the manual rub, so ceremony_start() is idempotent. When
    // the egg hatches while the player is on some other screen they get pulled
    // into the ceremony.
    //
    // UNLESS it happened while nobody was watching. An egg hatches at AGE_EGG_S
    // (900 s), so leaving the device off for a quarter of an hour is enough for
    // it to hatch inside sim_catch_up_ex(); ceremony_start() would then clear
    // s_absence_ms and the alert queue and the welcome-back report - armed moments
    // earlier by ui_note_absence() - would be destroyed by a 4.5 s show about a
    // moment the player did not see. The ceremony is for when you are IN FRONT
    // OF IT: rubbing the shell, or coming of age with the device switched on.
    // An offline hatch gets the old bare nav_home() and the banner stands.
    if (offline) nav_home();
    else         ceremony_start(CEREMONY_HATCH);
  }
  // Being born is not evolving. sim_hatch() raises SIM_EV_HATCHED and
  // SIM_EV_STAGE_UP in the SAME batch (sim.cpp), and the two do not compose:
  // ceremony_start() has just cleared the alert queue, this branch would refill it,
  // the queue does not expire and ui_service() returns early for the whole
  // ceremony - so the alert fires on the first frame of HOME after the birth,
  // with UI_ALERT_MIN_MS forcing the player to sit through a modal telling them
  // their newborn is evolving. HATCHED wins the batch outright.
  if (!(ev & SIM_EV_HATCHED) && (ev & (SIM_EV_STAGE_UP | SIM_EV_EVOLVE_MINOR))) {
    s_evolve_ms = now_ms();
    ui_alert(AL_EVOLVING);
    ui_toast(STR_RX_EVOLVE);
  }
  // A level-up is the one moment the numbers under the sprite change without
  // the player doing anything to the sprite, so it gets the flash as well as
  // the toast: HOME's XP rule snaps back to empty and the level in the strip
  // ticks over, and the frame flash is what points at it.
  if (ev & SIM_EV_LEVEL_UP)  {
    rd_flash(HATCH_FLASH_MS);
    audio_play(SFX_RISE);          // the numbers went up; so does the cue
    ui_toast(STR_RX_LEVEL_UP);
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

  // godmode.h: while the entry hold is in progress GST_LONG_BOTH must be
  // swallowed, or 1500 ms into a 5000 ms hold the user is thrown HOME and the
  // gesture becomes unreachable.
  if (s_god_prog && g == GST_LONG_BOTH) return;

  // --- invariant 7: an alert never lets a press through --------------------
  // ui/dialog.cpp owns all three overlays and consumes the gesture whenever one
  // is open. That is the invariant, in one call.
  if (dialog_input(g)) return;

  // --- the section 7 grammar, then the screen ------------------------------
  // app/input_router.cpp applies the two global invariants (LONG_BOTH = HOME,
  // B = BACK) subject to the row's SF_LOCK_INPUT / SF_OWNS_BACK, and hands
  // whatever is left to the current screen's input hook. Every state has a
  // row, so there is nothing after this line.
  (void)router_handle(g);
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
      petfx_service(body_view(*p, home_pose(p)), t);
    }
  }

  // ABOVE every early return below, unconditionally, and deliberately NOT gated
  // on being on HOME. This is what BOUNDS the petfx_hold() a choreography takes:
  // the clock keeps running whatever screen is up, so the hold is released
  // within one film (2.6 s worst case) even if every explicit actfx_cancel() in
  // this file were deleted. The five explicit ones are:
  //   screen_leave(SCR_HOME)  - leaving HOME by any route, back or home or push
  //   ceremony_start()        - the birth ceremony, which may arrive on any screen
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
    sm_replace_root(SCR_DIAG);
    return;
  }

  // Returning here is what keeps the alert layer and the auto-return off the
  // ceremony's back for its whole 4.5 s. ui/ceremony.cpp ends it itself, with
  // an input flush and a trip HOME.
  if (ceremony_active()) { ceremony_service(t); return; }

  // P10-C1: the serial command layer's navigation latch. god_service() runs in
  // app/app.cpp's loop, nowhere near the screen stack, so a typed
  // `start_creator` leaves an event here and this drains it - through
  // apply_god_evt(), the same arms a gesture goes through.
  //
  // BELOW the ceremony's early return ON PURPOSE. A birth or an evolution owns
  // the screen for 4.5 s and ends itself with an input flush and a trip HOME, so
  // a push landing inside one would be clobbered by the trip. The latch holds
  // until the film is over instead, which is what a latch is for.
  //
  // One deep: a second event before the first is drained replaces it, because a
  // queue of console navigations is a queue of surprises. Inert in the release
  // build (god_take_evt() stubs to GOD_EVT_NONE).
  {
    uint8_t garg = 0;
    const GodEvt ge = god_take_evt(garg);
    if (ge != GOD_EVT_NONE) {
      if (apply_god_evt(ge, garg) && sm_current() == SCR_DIAG) nav_home();
    }
  }

  // The alert layer surfaces only when nothing else owns the screen, and the
  // HELP strip expires on its own clock. Both are dialog_service()'s. A screen
  // that composed its own frame (SF_OWNS_FRAME: the console) would never draw
  // the overlay, so it must not be handed one either.
  {
    const ScreenDef* d = sm_def();
    const bool owns_frame = (d != nullptr) && ((d->flags & SF_OWNS_FRAME) != 0u);
    (void)dialog_service(t, sm_current() != SCR_GAME && !owns_frame);
  }

  // SECTION 18's CONFIRMATION. An evolution is offered, never imposed, and the
  // question goes up only on HOME with nothing else on top of it. Invariant 5
  // starts the cursor on NO, so a stray press is a decline; a decline costs
  // nothing, leaves EVO_STATE_PENDING set and simply comes back in
  // UI_EVOLVE_ASK_MS. app_evolution_offer() is what knows whether the whole
  // rule holds - this file never evaluates one.
  if (dialog_modal() == MODAL_NONE && sm_current() == SCR_HOME &&
      (s_evo_ask_ms == 0 || since(s_evo_ask_ms) >= UI_EVOLVE_ASK_MS) &&
      app_evolution_offer()) {
    s_evo_ask_ms = (t == 0u) ? 1u : t;   // 0 is the "never asked" sentinel
    dialog_open_confirm(CFM_EVOLVE, STR_CF_EVOLVE);
  }

  // Invariant 3, plus the update hook of a migrated screen. A modal freezes
  // the countdown: the one that is running belongs to the screen underneath.
  sm_block_autoreturn(dialog_modal() != MODAL_NONE);
  if (sm_service(t)) return;

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
    // A screen owns the frame either by flag (the console) or because a
    // ceremony is running on it.
    if ((def->flags & SF_OWNS_FRAME) || ceremony_active()) {
      // The ceremony draws no strip, so the echo finds nothing to invert - it
      // is called only to leave the per-frame state clean. The marker goes on
      // top of it, but NOT on top of the console: that frame is the god
      // screen's own status display and a second "GOD xN" bar would overwrite
      // it with what it already says.
      rd_affordance_echo();
      if (sm_current() != SCR_DIAG) god_draw_marker();
      return;
    }
  }

  if (dialog_modal() != MODAL_NONE) dialog_render();
  else                              draw_toast();

  // ONE tactile echo per frame, here, after the base screen and the modal have
  // both had their say. rd_invert_rect() is a real XOR, and draw_home() and
  // dialog_render() each call rd_affordance() in the same frame:
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
void ui_replace_root(ScreenId s) { sm_replace_root(s); }
void ui_wiggle(void)       { s_wiggle_ms = now_ms(); }
void ui_back(void)         { sm_back(); }
void ui_home(void)         { sm_home(); }
void ui_note_input(void)   { sm_note_input(); }
void ui_request_frame(void){ rd_request_frame(); }

// --- the BOX seams (ui.h) ----------------------------------------------------
void ui_box_activate(uint8_t slot) {
  if (slot == box_active()) return;
  PebbleInstance* next = box_slot(slot);
  if (!next) { ui_toast(STR_BOX_EMPTY); return; }
  // FLUSH THE OUTGOING PEBBLE FIRST. gs_save_active() only ever writes the slot
  // box.active_slot names, so once the index moves the Pebble being put away
  // can no longer be saved at all and its last SAVE_FULL_PERIOD_S window would
  // be lost - on a deliberate action, not on a power cut.
  gs_save_active(true);
  if (!box_set_active(slot)) { ui_toast(STR_ERR_BUSY); return; }
  // sim_switch() resets the PER-PEBBLE accumulators and keeps the device-wide
  // gain ledger, so changing the active slot cannot be used to farm (P2-C10).
  sim_switch(*next);
  gs_save_box();
  gs_save_active(true);
  s_stat_ok = 0;                     // the new pet's bars start at the truth
  actfx_cancel();
  const SimView* p = pet();
  if (p) petfx_reset(body_view(*p, POSE_IDLE));
  ui_toast(STR_BOX_ACTIVATED);
}

// FIRST BOOT'S STARTER CHOICE (P10-C4). See ui.h for why there are two locks.
//
// THE FIRST LOCK IS HERE and it is the persisted step: this may only run while
// app/onboarding.h says the flow is standing on OB_STARTER. Without it the
// entry point exists on every device for ever, one seam away from any screen
// that later wants to "just change the species".
//
// THE SECOND IS game/box.cpp's, and it is the one that actually protects a
// player: box_reroll_starter() refuses any Pebble that has earned or been named
// anything, whatever the caller believes about the step.
bool ui_set_starter(uint8_t species_id) {
  if (!s_cfg || ob_step(*s_cfg) != (uint8_t)OB_STARTER) return false;
  const uint8_t slot = box_active();
  if (slot == (uint8_t)BOX_ACTIVE_NONE) return false;
  if (!box_reroll_starter(slot, species_id, gt_now())) return false;

  PebbleInstance* p = box_slot(slot);
  if (!p) return false;
  // Same tail as ui_box_activate(): the simulation is rebound to the Pebble
  // that is actually in the slot, the bars start at the truth rather than
  // crawling from the old creature's, and the body cache is reset so the
  // renderer does not keep drawing the species that was there a frame ago.
  sim_switch(*p);
  gs_save_box();
  gs_save_active(true);
  s_stat_ok = 0;
  actfx_cancel();
  const SimView* v = pet();
  if (v) petfx_reset(body_view(*v, POSE_IDLE));
  return true;
}

void ui_box_swap(uint8_t a, uint8_t b) {
  // box_sim_swap(), not box_swap(): when the swap moves the ACTIVE slot the two
  // creatures exchange addresses and sim's raw pointer has to follow the one
  // the player is carrying (game/box_sim.h). Doing it here instead would put
  // the repair in a translation unit no host test can compile.
  if (!box_sim_swap(a, b)) return;
  gs_save_slot(a, true);
  gs_save_slot(b, true);
  gs_save_box();
}

void ui_box_release(uint8_t slot) {
  s_box_slot = slot;
  dialog_open_confirm(CFM_BOX_REL1, STR_BOX_REL_Q1);
}

Config* ui_cfg(void)       { return s_cfg; }
void ui_cfg_changed(void)     { cfg_persist(true); }

void ui_setup_persist(void) {
  cfg_persist(false);
  // See ui.h: a config with no Box beside it is a save the loader throws away.
  gs_save_box();
  gs_save_active(true);
}

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

void ui_help(uint16_t str_id)    { dialog_open_help(str_id); }
void ui_confirm_medicine(void)   { dialog_open_confirm(CFM_MEDICINE, STR_CF_SURE); }

void ui_start_minigame(uint8_t idx) {
  const uint16_t cd = sim_minigame_cooldown_s();
  if (cd) {
    char buf[48];
    snprintf(buf, sizeof(buf), "%s %u s", S(STR_GM_COOLDOWN), (unsigned)cd);
    toast_text(buf);
    return;
  }
  if (sim_stat_pct(ST_ENERGY) < ACT_PLAY_MIN_ENERGY_PCT) { ui_toast(STR_AERR_TIRED); return; }

  // One seed for the whole sequence, drawn once from the minigame stream: the
  // three games AND their layouts are reproducible from it, which is what
  // makes a run replayable in a test and a bug report.
  mgr_begin(idx, rng_u32(RNG_MINIGAME), MGR_SEQ_LEN);
  s_game_ms = 0;
  (void)input_pressed_edge(INPUT_BTN_L);   // drop the press that opened the game
  (void)input_pressed_edge(INPUT_BTN_R);
  nav_push(SCR_GAME);
}

// -----------------------------------------------------------------------------
//  THE BATTLE SEAMS (P4-C4). See ui.h.
// -----------------------------------------------------------------------------
void ui_start_battle(uint8_t entry) {
  // ONE seed per battle, drawn once. rng_u32(RNG_BATTLE) is legal here and
  // illegal inside src/game/battle* - tools/check.sh's second battle gate greps
  // for exactly that - because the ENGINE must draw only from BattleState.rng
  // or two peers stop reproducing each other's rounds. Handing it in from the
  // outside is what keeps both true at once.
  const uint32_t seed = (entry == BT_ENTRY_DIAG) ? (uint32_t)BT_DIAG_SEED
                                                 : rng_u32(RNG_BATTLE);
  battle_arm(entry, seed);
  // The console cannot navigate (ui.cpp owns that), so a DIAG battle is pushed
  // ON TOP of SCR_DIAG and B walks back into the console it was started from.
  nav_push(SCR_BATTLE);
}

void ui_battle_result(uint8_t entry, uint8_t won) {
  // A diagnostic pays nothing. Entering god mode already sets
  // genome.god_tainted for ever, and a test entry that also handed out XP would
  // be a cheat wearing a developer tool's clothes.
  //
  // A LINKED WIN PAYS THE SAME AS A PRACTICE ONE, THROUGH THE SAME METER
  // (P7-C3). It is deliberately not worth more: XP_CAP_BATTLE is two wins an
  // hour for the device, and a source that a second device can supply on demand
  // is the last one that should have its own larger bucket. What makes the
  // linked win honest is not the amount, it is WHERE `won` came from -
  // ui/screen_battle.cpp's won_now() reads ui_link_battle_status(), which is
  // true only where networking/session.h's session_rewards_authorised() is, so
  // a desync or a lost link arrives here as won == 0 and falls out on the line
  // below with no Box written and no ledger touched.
  if (entry != BT_ENTRY_PRACTICE && entry != BT_ENTRY_LINK) return;
  if (!won) return;
  // METERED LIKE EVERY OTHER SOURCE, and P4-C4 had to SIZE that meter to be able
  // to say so: game/xp.cpp carried XP_SRC_BATTLE as {0, 0} - "reserved but not
  // yet metered, until P4-C4 exists to spend it" - which would have made the
  // practice battle the one repeatable XP source in the game with no ceiling at
  // all. XP_CAP_BATTLE is two wins an hour (data/balance.h), refilling
  // continuously, belonging to the device rather than to the Pebble.
  //
  // The return value is whether a LEVEL was gained, NOT whether anything was
  // paid: a win against an empty bucket returns false and is not an error.
  // ui_note_events() is what turns SIM_EV_LEVEL_UP into the flash and the toast.
  (void)app_award_xp((uint16_t)XP_BATTLE_WIN, XP_SRC_BATTLE);
  if (pet()) gs_save_active(true);
  ui_toast(STR_BT_XP);
}

// -----------------------------------------------------------------------------
//  THE EXPLORATION SEAMS (P5-C3/C4). See ui.h.
// -----------------------------------------------------------------------------
const WifiScanDriver& ui_scan_driver(void) { return net_scan_driver(); }

// -----------------------------------------------------------------------------
//  THE PEER LINK'S SEAMS (P7-C2/C3). See ui.h.
//
//  networking/net.cpp is the ONE radio owner on the far side of every one of
//  them, exactly as it is for the scan. ui/screen_link.cpp is pure and reaches
//  none of this itself.
// -----------------------------------------------------------------------------
const LinkRadioDriver& ui_link_driver(void)   { return net_link_driver(); }
const Transport&       ui_link_transport(void){ return net_link_transport(); }
bool     ui_link_bind(uint8_t slot)           { return net_link_bind(slot); }
void     ui_link_unbind(void)                 { net_link_unbind(); }
// RNG_MISC is core/rng.h's "tokens, nonces, PINs, canaries" stream - the same
// one the creator PIN, the device id, the RTC nonce and the flash canary are
// drawn from - and a session nonce is exactly that kind of value. A pure screen
// may not reach a named global stream, which is the whole reason this line is
// here and not in ui/screen_link.cpp.
uint32_t ui_link_nonce(void)                  { return rng_u32(RNG_MISC); }

// -----------------------------------------------------------------------------
//  THE TRADE'S SEAM (P7-C4). See ui.h for why it is a pointer to an incomplete
//  type over there and a whole struct here.
//
//  THE FIVE HOOKS ARE THE ONLY PLACE THE TRADE TOUCHES FLASH, and every one of
//  them returns whether the bytes LANDED: save_manager.cpp verifies its own
//  writes by reading them back, and game/trade.cpp stops the sequence on a
//  false rather than assuming. The WRITE ORDER - W3, B1, B2, B3, W4 - belongs
//  to game/trade.cpp and not to this file; these are the four verbs it drives.
// -----------------------------------------------------------------------------
static bool ui_tr_store_journal(void* ctx, const PendingTrade& t)
{
  (void)ctx;
  gs_state().trade = t;
  return save_trade_journal(t);
}
static bool ui_tr_store_slot(void* ctx, uint8_t slot)
{
  (void)ctx;
  if (slot >= (uint8_t)BOX_SLOTS) return false;
  // save_pebble_now(), NOT save_pebble(..., true). B1 and B2 write the SAME key
  // microseconds apart whenever box_add() reuses the slot B1 released, and
  // save_pebble() DEFERS a second write inside SAVE_MIN_GAP_MS and returns
  // true - so this shim would tell game/trade.cpp that bytes landed which had
  // not, and the trade would clear its journal over an empty flash slot. See
  // persistence/save_manager.h.
  return save_pebble_now(slot, gs_state().pebbles[slot]);
}
static bool ui_tr_store_box(void* ctx)
{
  (void)ctx;
  return save_box_header(gs_state().box);
}
static void ui_tr_store_checkpoint(void* ctx)
{
  (void)ctx;
  // A failed checkpoint does not invalidate the trade - the same sentence
  // app/app.cpp makes about the evolution ceremony. Only the nvs2 copy is stale.
  (void)save_checkpoint_all();
}

static TradeStore ui_trade_store(void)
{
  TradeStore s;
  s.write_journal = &ui_tr_store_journal;
  s.write_slot    = &ui_tr_store_slot;
  s.write_box     = &ui_tr_store_box;
  s.checkpoint    = &ui_tr_store_checkpoint;
  s.ctx           = nullptr;
  return s;
}

// W1: our offer is on the wire.
static bool ui_tr_journal_sent(void* ctx, uint32_t out_id, uint32_t peer_id)
{
  (void)ctx;
  PendingTrade t;
  trade_journal_sent(t, out_id, peer_id);
  return ui_tr_store_journal(nullptr, t);
}

// THE POLICY VERDICT, which is game/trade.cpp's and not the transport's: the
// taint gate, a duplicate id, a peer that is us. The decode has already run in
// networking/trade_link.cpp, so what is left is what a legal Pebble may do.
static uint8_t ui_tr_judge(void* ctx, const uint8_t rec[48])
{
  (void)ctx;
  PebbleInstance in;
  if (pbw_decode(rec, in) != VR_OK) return (uint8_t)TDR_PEER_INVALID;
  const PebbleInstance* mine = box_peek(link_trade_slot());
  if (mine == nullptr) return (uint8_t)TDR_NO_SLOT;
  return (uint8_t)trade_accept_check(*mine, in, gs_device_id(), link_trade_peer_id());
}

// W2: theirs is here and ours is not gone.
static bool ui_tr_journal_received(void* ctx, const uint8_t rec[48])
{
  (void)ctx;
  PendingTrade t = gs_state().trade;
  trade_journal_received(t, rec);
  return ui_tr_store_journal(nullptr, t);
}

// W3 -> B1 -> B2 -> B3 -> W4, in that order, inside game/trade.cpp.
static bool ui_tr_commit(void* ctx)
{
  (void)ctx;
  PendingTrade t = gs_state().trade;
  const TradeStore st = ui_trade_store();
  const TradeReject r = trade_execute(t, trade_wire_codec(), st, gt_now());
  gs_state().trade = t;
  return r == TDR_OK;
}

static void ui_tr_abort(void* ctx)
{
  (void)ctx;
  PendingTrade t;
  trade_journal_idle(t);
  (void)ui_tr_store_journal(nullptr, t);
}

const TradeHooks* ui_trade_hooks(void)
{
  static const TradeHooks H = {
    &ui_tr_journal_sent, &ui_tr_judge, &ui_tr_journal_received,
    &ui_tr_commit, &ui_tr_abort, nullptr
  };
  return &H;
}

uint16_t ui_trade_quarantine(void) { return save_quarantine_mask(); }

uint8_t  ui_link_battle_status(void)          { return link_battle_status(); }
uint8_t  ui_link_battle_side(void)            { return link_battle_side(); }
void     ui_link_battle_pump(uint32_t now_ms) { link_battle_pump(now_ms); }
bool     ui_link_battle_wants_action(void)    { return link_battle_wants_action(); }
void     ui_link_battle_submit(uint8_t kind, uint8_t index) {
  link_battle_submit(kind, index);
}
uint32_t ui_link_battle_move_ms_left(uint32_t now_ms) {
  return link_battle_move_ms_left(now_ms);
}
void     ui_link_battle_done(void)            { link_battle_done(); }

void ui_explore_clock(uint32_t* now_epoch, uint32_t* now_ms, uint8_t* cal)
{
  // All three at once, in the shape game/cooldowns.h takes them, so a screen
  // cannot read two of them a frame apart and decide with a mixed clock.
  if (now_epoch) *now_epoch = gt_now();
  // gt_mono32(), NOT millis(), AND THIS ONE DECIDES A GAME OUTCOME (P6-C3).
  // While the clock is CAL_UNSET the cooldown table's deadlines are monotonic
  // MILLISECONDS (game/cooldowns.h), and millis() is uptime: it is the clock
  // that stops at a deep-sleep wake, and the one that only carries a light
  // sleep because esp_timer happens to be resynchronised from the RTC on the
  // way out. gt_mono32() IS the RTC counter, so an armed cooldown survives
  // every sleep this device can take. The other two millis() readers in this
  // file are animation phase and modal lifetimes and stay as they are.
  if (now_ms)    *now_ms    = gt_mono32();
  if (cal)       *cal       = (uint8_t)gt_cal_state();
}

uint32_t ui_device_seed(void) { return gs_device_id(); }

// RNG_ENCOUNTER, and this is the only draw from it in the firmware. The
// ENCOUNTER itself is deterministic from its inputs (game/encounters.h says
// why); what needs real randomness is the capture roll.
uint32_t ui_explore_roll(void) { return rng_u32(RNG_ENCOUNTER); }

CooldownTable& ui_cooldowns(void) { return gs_state().cds; }
Inventory&     ui_inventory(void) { return gs_state().inv; }

void ui_explore_commit(void)
{
  GameState& gs = gs_state();
  // cd_take_dirty() rather than "save after cd_arm": cd_ready() can dirty the
  // table too, by promoting rows armed while the clock was CAL_UNSET, and a
  // caller that only saved after arming would drop thirty-two promoted rows on
  // the next power cut (game/cooldowns.h).
  // BOTH HALVES OF THE "cd" BLOB, and NEITHER call may be skipped by a
  // short-circuit: cd_take_dirty() is take-and-clear, so `a() || b()` would
  // leave b's flag standing whenever a fired. P6-C2 put the activity day and
  // its score in the same blob's reserved bytes, so a scan that credited a new
  // network dirties it through act_take_dirty() even when no cooldown moved.
  const bool cd_dirty  = cd_take_dirty();
  const bool act_dirty = act_take_dirty();
  if (cd_dirty || act_dirty) (void)save_cooldowns(gs.cds);
  (void)save_inventory(gs.inv);
  if (pet()) gs_save_active(true);
}

Genome ui_fresh_genome(void) { return genome_genesis(); }

PebbleInstance* ui_active_pebble(void)
{
  const uint8_t slot = box_active();
  return (slot < (uint8_t)BOX_SLOTS) ? box_slot(slot) : nullptr;
}

void ui_award_xp(uint16_t amount, uint8_t src)
{
  if (amount == 0u) return;
  (void)app_award_xp(amount, (XpSource)src);
  if (pet()) gs_save_active(true);
}

void ui_hold_fps(uint8_t fps, uint16_t ms) { rd_hold_fps(fps, ms); }
void ui_flash(uint16_t ms)                 { rd_flash(ms); }
void ui_shake(uint8_t amp_px, uint16_t ms) { rd_shake(amp_px, ms); }

uint8_t ui_god_progress(void) { return s_god_prog; }

// -----------------------------------------------------------------------------
//  THE CREATOR SCREEN'S RADIO SEAM
// -----------------------------------------------------------------------------
void ui_creator_info(CreatorInfo& out) {
  memset(&out, 0, sizeof(out));
  out.ap_up  = net_is_ap_up()  ? 1u : 0u;
  out.pin    = web_pin();
  // The D7 grace period. The screen owns the radio, so the screen is what acts
  // on this - webui only answers the question (spec sections 34 and 40).
  out.idle_expired = web_portal_idle_expired() ? 1u : 0u;
  snprintf(out.ssid, sizeof(out.ssid), "%s", net_ap_ssid());
  snprintf(out.ip,   sizeof(out.ip),   "%s", net_ip());
  // NO PIN IN THE URL SINCE P8-C1 (spec section 39): net_url() lost the
  // argument, not just the substitution, so there is nothing left to pass.
  if (net_url(out.url, sizeof(out.url)) == 0) out.url[0] = '\0';
}

// THE ONE TEARDOWN (P8-C2). Leaving the screen, the D7 idle timeout and the
// section 47 "the access point never came up" exit all arrive here with
// on == false, so there is exactly one order of operations and no path that
// leaves half of the portal standing.
void ui_creator_radio(bool on) {
#if FEATURE_WEB
  if (on) {
    // The screen that wants the radio is the screen that asks for it; there is
    // no radio policy in the entry point any more (plan section 2 row G4).
    // Was cfg_flag(CF_WEB_ENABLED): the helper had exactly this one caller left
    // once SETTINGS and the HOME status bar moved out, and that caller is
    // inside #if FEATURE_WEB - so in the no-web variant the function was
    // defined and never used, which this build treats as an error.
    if (s_cfg && (s_cfg->flags & CF_WEB_ENABLED) != 0) {
      // The PIN first: it is loaded or minted and persisted BEFORE the access
      // point exists, so there is no window in which the portal is reachable
      // and the device does not yet know what it will ask for.
      web_portal_open();
      // net_request_portal(), NOT net_request(RADIO_WIFI): the latter answers
      // "already on the WiFi track" for a scan or a peer link and would leave
      // this screen waiting for an access point that is never coming (net.h).
      (void)net_request_portal();
    }
  } else {
    // Socket first, then the radio under it. web_service() re-opens the socket
    // whenever the phase is still NPH_AP_PORTAL, so dropping the radio first
    // would leave one pump in which the server is listening on a netif that is
    // going away.
    web_portal_close();
    if (net_mode() == RADIO_WIFI) {
      // Radio OFF by default: CREATOR is the only owner of RADIO_WIFI, so
      // leaving it gives back the ~50 KB of heap and the largest current draw
      // on the board instead of holding the radio powered until the next
      // reboot.
      (void)net_request(RADIO_OFF);
    }
  }
#else
  (void)on;
#endif
}

// -----------------------------------------------------------------------------
//  THE EVOLUTION SCREEN'S HATCH SEAM
// -----------------------------------------------------------------------------
void ui_request_hatch(void) {
  sim_hatch();
  // Neither the save nor the toast happen here: ceremony_start() owns the save,
  // and sim_hatch() raises SIM_EV_HATCHED so ui_note_events() owns the toast.
  ceremony_start(CEREMONY_HATCH);
}

void ui_info_lines(char lines[UI_INFO_LINES][UI_INFO_CAP]) {
  for (uint8_t i = 0; i < UI_INFO_LINES; ++i) lines[i][0] = '\0';
  snprintf(lines[0], UI_INFO_CAP, "%s %s", FW_NAME, FW_VERSION);
  // Was "IP %s  rssi %d". net_rssi() was station-only and went with the
  // station (P5-C1); the radio's phase is what this line can still report.
  snprintf(lines[1], UI_INFO_CAP, "IP %s  net %u/%u", net_ip(),
           (unsigned)net_mode(), (unsigned)net_phase());
  // "----" AND NOT "0000" WHEN NOTHING HAS BEEN ISSUED. 0 is the sentinel and
  // is a value the PIN can never take (networking/creator_gate.h), so printing
  // it as four zeros would be a diagnostic screen stating a PIN that does not
  // exist. It is minted on the first CREATOR entry, not at boot.
  {
    const uint16_t pin = web_pin();
    char pin_txt[8];
    if (pin == 0u) snprintf(pin_txt, sizeof pin_txt, "----");
    else           snprintf(pin_txt, sizeof pin_txt, "%04u", (unsigned)pin);
    snprintf(lines[2], UI_INFO_CAP, "PIN %s  spr rev %u",
             pin_txt, (unsigned)SPRITE_REV);
  }
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
//  holds, and the two numbers spec section 8 asks for: hp_max derived from the
//  species base (P4-C1 gives it a real roster) and the XP cost of the current
//  level from XP_TABLE[31].
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

  const uint8_t slot = box_active();
  const PebbleInstance* pb = (slot == BOX_ACTIVE_NONE) ? nullptr : box_peek(slot);
  if (pb) {
    s_view.species_id = pb->species_id;
    s_view.level      = pb->level;
    // §55, for the STILL body path's glitch painter (P10-C3). The animated path
    // reads the same bit off PetView.corrupted in ui/pet_view.cpp; this is the
    // one a golden can see.
    s_view.corrupted  = (uint8_t)((pb->status & PBS_CORRUPTED) != 0u);
    // ...and how much longer, for SCR_STATUS's readout (P10-C6). Rounded UP so
    // a corrupted creature never reports 0 h left while it is still shimmering.
    s_view.corrupt_h  = 0u;
    if (s_view.corrupted) {
      uint32_t ep = 0; uint8_t cal = 0;
      ui_explore_clock(&ep, nullptr, &cal);
      const uint32_t left = cor_left_s(*pb, ep, cal);
      const uint32_t h = (left + 3599u) / 3600u;
      s_view.corrupt_h = (uint8_t)((h > 255u) ? 255u : ((h == 0u) ? 1u : h));
    }
    s_view.xp         = pb->xp;
    s_view.hp_cur     = pb->hp_cur;
    const SpeciesDef* sp = species_get(pb->species_id);
    // hp_max is recomputed and never stored (plan 1.5.1). With no species row
    // there is nothing honest to show, so the meter reports the current value
    // as full rather than inventing a maximum. P4-C1: this used to open-code
    // `10 + 2*base_hp + level`, a second copy of a formula game/xp.cpp owns.
    s_view.hp_max = sp ? xp_hp_max(sp->base_hp, pb->level) : pb->hp_cur;
  }
  if (s_view.level == 0) s_view.level = 1;
  // What THIS level costs to leave (game/xp.h). 0 at XP_LEVEL_MAX, which is how
  // a screen is told the curve is finished rather than being handed a divisor.
  s_view.xp_next = xp_for_level(s_view.level);
  return &s_view;
}
