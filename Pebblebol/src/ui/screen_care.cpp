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

// The row order IS MgId order, which is what lets play_input() start game
// number `s_play` without a lookup table. P3-C4b appends four more rows here
// and the static_assert in screen_care.h is what makes a mismatch a build
// error rather than a game that launches its neighbour.
static constexpr uint16_t kPlayItem[PLAY_ROWS] = {
  STR_MG_PING, STR_MG_SEQ, STR_MG_FLOOD, STR_MG_FIREWALL, STR_MG_BUFFER,
  STR_MG_DELETE, STR_ITEM_BACK
};
static constexpr uint16_t kPlayHelp[PLAY_ROWS] = {
  STR_MG_PING_HINT, STR_MG_SEQ_HINT, STR_MG_FLOOD_HINT, STR_MG_FIREWALL_HINT,
  STR_MG_BUFFER_HINT, STR_MG_DELETE_HINT, STR_HLP_BACK
};

// THE ORDER, not merely the count. screen_care.h asserts PLAY_ROWS against
// MG_ID_COUNT + 1, and registry.cpp asserts its own length - but neither can
// see a row in the WRONG PLACE, which is exactly the mistake that matters here:
// play_input() launches game number s_play with no lookup, so a swapped pair
// starts the wrong game while every count in the tree still matches. The names
// and hints are index-parallel to MgId (guarded in core/strings_es.h), so the
// whole order is one fold.
static constexpr bool play_rows_are_in_mgid_order()
{
  for (uint8_t i = 0; i < (uint8_t)MG_ID_COUNT; ++i) {
    if (kPlayItem[i] != (uint16_t)(STR_MG_PING + i))      return false;
    if (kPlayHelp[i] != (uint16_t)(STR_MG_PING_HINT + i)) return false;
  }
  return kPlayItem[PLAY_ROWS - 1] == STR_ITEM_BACK &&
         kPlayHelp[PLAY_ROWS - 1] == STR_HLP_BACK;
}
static_assert(play_rows_are_in_mgid_order(),
              "the PLAY rows must be in MgId order, plus the way out");

static uint8_t s_care = 0;
static uint8_t s_play = 0;

uint8_t care_cursor(void) { return s_care; }
uint8_t play_cursor(void) { return s_play; }

void care_enter(void) { s_care = 0; }
void play_enter(void) { s_play = 0; }

// -----------------------------------------------------------------------------
//  The shared frame and the shared grammar.
// -----------------------------------------------------------------------------
// The row array is sized by the LONGER of the two lists (LIST_ROWS_MAX), so the
// clamp below is a bounds guard on the array and can no longer truncate either
// caller. It used to be sized CARE_ROWS (5): at PLAY_ROWS = 7 that clamp would
// have drawn only the first five PLAY rows - BORRAR and "Volver" never
// appearing - while list_common() rang the cursor through all seven and
// gfx_list() lost the highlight entirely on the two it could not see.
static void draw_str_list(uint16_t title, const uint16_t* ids, uint8_t n, uint8_t cur) {
  const char* items[LIST_ROWS_MAX];
  if (n > LIST_ROWS_MAX) n = LIST_ROWS_MAX;
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
