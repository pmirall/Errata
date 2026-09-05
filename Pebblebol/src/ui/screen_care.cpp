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

#include <stdio.h>

#include "../core/strings_es.h"
#include "../game/inventory.h"
#include "gfx.h"
#include "screen.h"
#include "screen_battle.h"   // BT_ENTRY_PRACTICE
#include "ui.h"

static constexpr uint16_t kCareItem[CARE_ROWS] = {
  STR_ACT_FEED_MEAL, STR_ACT_FEED_SNACK, STR_MENU_CLEAN,
  STR_MENU_HEALTH,   STR_ITEM_BAG,       STR_ITEM_BACK
};
static constexpr uint16_t kCareHelp[CARE_ROWS] = {
  STR_HLP_MEAL, STR_HLP_SNACK, STR_HLP_CLEAN,
  STR_HLP_HEALTH, STR_HLP_BAG, STR_HLP_BACK
};
// The two rows whose POSITION is load-bearing: the bag row opens a mode and the
// last row leaves, and swapping them would leave every count in the tree
// matching (the same defect play_rows_are_in_mgid_order() exists for).
static_assert(kCareItem[CARE_BAG]  == STR_ITEM_BAG &&
              kCareItem[CARE_BACK] == STR_ITEM_BACK,
              "the CARE list's bag row and its way out have moved");

// The row order IS MgId order, which is what lets play_input() start game
// number `s_play` without a lookup table. P3-C4b appends four more rows here
// and the static_assert in screen_care.h is what makes a mismatch a build
// error rather than a game that launches its neighbour.
static constexpr uint16_t kPlayItem[PLAY_ROWS] = {
  STR_MG_PING, STR_MG_SEQ, STR_MG_FLOOD, STR_MG_FIREWALL, STR_MG_BUFFER,
  STR_MG_DELETE, STR_BT_PRACTICE, STR_ITEM_BACK
};
static constexpr uint16_t kPlayHelp[PLAY_ROWS] = {
  STR_MG_PING_HINT, STR_MG_SEQ_HINT, STR_MG_FLOOD_HINT, STR_MG_FIREWALL_HINT,
  STR_MG_BUFFER_HINT, STR_MG_DELETE_HINT, STR_BT_HELP, STR_HLP_BACK
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
  // P4-C4: the battle row is pinned by NAME and by POSITION. Swapping it with
  // "Volver" would leave every count in the tree matching and start a battle
  // from the row that means "leave".
  return kPlayItem[PLAY_BATTLE] == STR_BT_PRACTICE &&
         kPlayHelp[PLAY_BATTLE] == STR_BT_HELP &&
         kPlayItem[PLAY_BACK]   == STR_ITEM_BACK &&
         kPlayHelp[PLAY_BACK]   == STR_HLP_BACK;
}
static_assert(play_rows_are_in_mgid_order(),
              "the PLAY rows must be in MgId order, then the battle, then the way out");

static uint8_t s_care = 0;
static uint8_t s_play = 0;
// Four bytes, and they are the whole cost of the inventory surface.
static uint8_t s_mode = (uint8_t)CAREM_LIST;
static uint8_t s_bag  = 0;

uint8_t care_mode(void)       { return s_mode; }
uint8_t care_bag_cursor(void) { return s_bag; }

uint8_t care_bag_rows(void) {
  const Inventory& inv = ui_inventory();
  uint8_t n = 0;
  for (uint8_t id = 1; id <= ITEM_COUNT; ++id)
    if (inv_count(inv, id) > 0u) ++n;
  return n;
}

// The id in bag row `row`, walking the ITEM TABLE in id order rather than the
// InvSlot array: the slots are filled in pickup order, so a list built off them
// would reshuffle itself under the player's cursor every time something was
// spent. Returns 0 for the "Volver" row and for a row past the end.
static uint8_t bag_item_at(uint8_t row) {
  const Inventory& inv = ui_inventory();
  uint8_t n = 0;
  for (uint8_t id = 1; id <= ITEM_COUNT; ++id) {
    if (inv_count(inv, id) == 0u) continue;
    if (n == row) return id;
    ++n;
  }
  return 0u;
}

uint8_t care_cursor(void) { return s_care; }
uint8_t play_cursor(void) { return s_play; }

void care_enter(void) { s_care = 0; s_mode = (uint8_t)CAREM_LIST; s_bag = 0; }
void care_leave(void) { s_mode = (uint8_t)CAREM_LIST; s_bag = 0; }
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
// The bag list: one row per kind held, "NAME xN", then the way out.
static void bag_render(void) {
  const Inventory& inv = ui_inventory();
  const uint8_t n = care_bag_rows();
  gfx_header(S(STR_ITEM_BAG), nullptr);
  if (n == 0u) {
    gfx_text_center(GF_BODY, 30, S(STR_ITEM_BAG_EMPTY));
    gfx_countdown(ui_idle_ms());
    gfx_affordance(nullptr, S(STR_AF_BACK));
    return;
  }
  static char rows[LIST_ROWS_MAX][22];
  const char* items[LIST_ROWS_MAX];
  uint8_t shown = (uint8_t)(n + 1u);
  if (shown > LIST_ROWS_MAX) shown = LIST_ROWS_MAX;
  for (uint8_t i = 0; i < shown; ++i) {
    const uint8_t id = bag_item_at(i);
    if (id == 0u) snprintf(rows[i], sizeof rows[i], "%s", S(STR_ITEM_BACK));
    else snprintf(rows[i], sizeof rows[i], "%s x%u", S(item_get(id)->name_idx),
                  (unsigned)inv_count(inv, id));
    items[i] = rows[i];
  }
  gfx_list(items, shown, s_bag, nullptr, ui_now_ms());
  gfx_countdown(ui_idle_ms());
  gfx_affordance(S(STR_AF_NEXT), S(STR_AF_BACK_SEL));
}

void care_render(void) {
  if (s_mode == (uint8_t)CAREM_BAG) { bag_render(); return; }
  draw_str_list(STR_MENU_CARE, kCareItem, CARE_ROWS, s_care);
}

// USING ONE. The screen says WHICH item and on WHOM; game/inventory.cpp decides
// what it does and whether it is consumed, and ui.cpp commits. Nothing about an
// item's effect is decided here - that was the whole point of giving the pack
// the target and param columns.
static void bag_use(void) {
  const uint8_t n = care_bag_rows();
  if (s_bag >= n) { s_mode = (uint8_t)CAREM_LIST; s_bag = 0; return; }   // Volver
  const uint8_t id = bag_item_at(s_bag);
  if (id == 0u) return;

  uint32_t now_epoch = 0;
  uint8_t  cal = 0;
  ui_explore_clock(&now_epoch, nullptr, &cal);
  ItemEffect eff;
  const uint8_t r = inv_use(ui_inventory(), id, ui_active_pebble(), now_epoch, cal, eff);
  switch ((ItemUse)r) {
    case IU_OK:        ui_toast(STR_ITEM_USED);     break;
    case IU_NOT_HERE:  ui_toast(STR_ITEM_NOT_HERE); break;
    case IU_NO_TARGET: ui_toast(STR_ITEM_NO_PET);   break;
    default:           ui_toast(STR_ITEM_NO_USE);   break;
  }
  if (r == (uint8_t)IU_OK) ui_explore_commit();
  // The row under the cursor may have just been spent, so the cursor is
  // re-clamped rather than left pointing past the end of a shorter list.
  const uint8_t left = care_bag_rows();
  if (s_bag > left) s_bag = left;
}

void care_input(Gesture g) {
  if (s_mode == (uint8_t)CAREM_BAG) {
    if (g == (Gesture)GST_TAP_R) { s_mode = (uint8_t)CAREM_LIST; s_bag = 0; return; }
    if (g != GST_HOLD_R) {
      const uint8_t n = (uint8_t)(care_bag_rows() + 1u);
      list_common(g, s_bag, n, nullptr);
      return;
    }
    bag_use();
    return;
  }

  // SF_OWNS_BACK, so B arrives here. On the verb list it means what it always
  // meant.
  if (g == (Gesture)GST_TAP_R) { ui_back(); return; }
  if (g != GST_HOLD_R) { list_common(g, s_care, CARE_ROWS, kCareHelp); return; }
  switch (s_care) {
    case CARE_MEAL:     if (!ui_act_and_show(ACT_FEED_MEAL))  ui_back(); break;
    case CARE_SNACK:    if (!ui_act_and_show(ACT_FEED_SNACK)) ui_back(); break;
    case CARE_CLEAN:    if (!ui_act_and_show(ACT_CLEAN))      ui_back(); break;
    case CARE_MEDICINE: ui_confirm_medicine(); break;
    case CARE_BAG:      s_mode = (uint8_t)CAREM_BAG; s_bag = 0; break;
    default:            ui_back(); break;
  }
}

// -----------------------------------------------------------------------------
//  PLAY
// -----------------------------------------------------------------------------
void play_render(void) { draw_str_list(STR_MENU_PLAY, kPlayItem, PLAY_ROWS, s_play); }

void play_input(Gesture g) {
  if (g != GST_HOLD_R) { list_common(g, s_play, PLAY_ROWS, kPlayHelp); return; }
  if (s_play == PLAY_BACK)   { ui_back(); return; }
  // The seed comes from RNG_BATTLE and the Box has to be read, neither of which
  // a pure translation unit may do: the screen says WHICH battle, ui.cpp draws
  // the seed and pushes SCR_BATTLE.
  if (s_play == PLAY_BATTLE) { ui_start_battle(BT_ENTRY_PRACTICE); return; }
  // The cooldown, the energy floor and the game itself are all still ui.cpp's
  // until P3-C4 lifts the minigames out; this screen only says which one.
  ui_start_minigame(s_play);
}
