// =============================================================================
//  PEBBLEBOL - ui/screen_care.cpp
//  CARE and PLAY, migrated into the screen table by P2-C11b.
//
//  EVERY ACCEPTED CARE ACTION SENDS THE PLAYER HOME TO WATCH IT (BRIEF D, and
//  the long note over ui_act_and_show()). A REJECTED one does not: there is no
//  film to watch, so the list keeps whatever destination it had and the toast
//  explains itself where the player is standing.
//
//  PURE translation unit.
// =============================================================================
#include "screen_care.h"

#include "../core/strings_es.h"
#include "gfx.h"
#include "screen.h"
#include "ui.h"

static const uint16_t kCareItem[CARE_ROWS] = {
  STR_ACT_FEED_MEAL, STR_ACT_FEED_SNACK, STR_MENU_CLEAN,
  STR_MENU_HEALTH,   STR_ITEM_BACK
};
static const uint16_t kCareHelp[CARE_ROWS] = {
  STR_HLP_MEAL, STR_HLP_SNACK, STR_HLP_CLEAN,
  STR_HLP_HEALTH, STR_HLP_BACK
};

static const uint16_t kPlayItem[PLAY_ROWS] = {
  STR_DG_REFLEX, STR_DG_MEMORY, STR_DG_JUMP, STR_ITEM_BACK
};
static const uint16_t kPlayHelp[PLAY_ROWS] = {
  STR_DG_REFLEX_HINT, STR_DG_MEMORY_HINT, STR_DG_JUMP_HINT, STR_HLP_BACK
};

static uint8_t s_care = 0;
static uint8_t s_play = 0;

uint8_t care_cursor(void) { return s_care; }
uint8_t play_cursor(void) { return s_play; }

void care_enter(void) { s_care = 0; }
void play_enter(void) { s_play = 0; }

// -----------------------------------------------------------------------------
//  The shared frame and the shared grammar.
// -----------------------------------------------------------------------------
static void draw_str_list(uint16_t title, const uint16_t* ids, uint8_t n, uint8_t cur) {
  const char* items[CARE_ROWS];
  if (n > CARE_ROWS) n = CARE_ROWS;
  for (uint8_t i = 0; i < n; ++i) items[i] = S(ids[i]);
  gfx_header(S(title), nullptr);
  gfx_list(items, n, cur, nullptr, ui_now_ms());
  gfx_countdown(ui_idle_ms());
  gfx_affordance(S(STR_AF_NEXT), S(STR_AF_BACK_SEL));
}

static uint8_t ring_next(uint8_t cur, uint8_t n) {
  return n ? (uint8_t)((cur + 1u) % n) : 0u;
}

static void list_common(Gesture g, uint8_t& cur, uint8_t n, const uint16_t* help) {
  switch (g) {
    case GST_TAP_L:
    case GST_HOLD_L: cur = ring_next(cur, n); break;
    case GST_DBL_L:  cur = 0; break;
    case GST_DBL_R:  cur = (uint8_t)(n - 1u); break;
    case GST_BOTH:   if (help) ui_help(help[cur]); break;
    default: break;
  }
}

// -----------------------------------------------------------------------------
//  CARE
// -----------------------------------------------------------------------------
void care_render(void) { draw_str_list(STR_MENU_CARE, kCareItem, CARE_ROWS, s_care); }

void care_input(Gesture g) {
  if (g != GST_HOLD_R) { list_common(g, s_care, CARE_ROWS, kCareHelp); return; }
  switch (s_care) {
    case CARE_MEAL:     if (!ui_act_and_show(ACT_FEED_MEAL))  ui_back(); break;
    case CARE_SNACK:    if (!ui_act_and_show(ACT_FEED_SNACK)) ui_back(); break;
    case CARE_CLEAN:    if (!ui_act_and_show(ACT_CLEAN))      ui_back(); break;
    case CARE_MEDICINE: ui_confirm_medicine(); break;
    default:            ui_back(); break;
  }
}

// -----------------------------------------------------------------------------
//  PLAY
// -----------------------------------------------------------------------------
void play_render(void) { draw_str_list(STR_MENU_PLAY, kPlayItem, PLAY_ROWS, s_play); }

void play_input(Gesture g) {
  if (g != GST_HOLD_R) { list_common(g, s_play, PLAY_ROWS, kPlayHelp); return; }
  if (s_play >= (uint8_t)(PLAY_ROWS - 1)) { ui_back(); return; }
  // The cooldown, the energy floor and the game itself are all still ui.cpp's
  // until P3-C4 lifts the minigames out; this screen only says which one.
  ui_start_minigame(s_play);
}
