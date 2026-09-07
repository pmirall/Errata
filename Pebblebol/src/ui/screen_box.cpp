// =============================================================================
//  PEBBLEBOL - ui/screen_box.cpp
//  See screen_box.h. PURE translation unit.
// =============================================================================
#include "screen_box.h"

#include <stdio.h>
#include <string.h>

#include "../core/strings_es.h"
#include "../core/utf8.h"
#include "../game/box.h"
#include "../game/genome.h"
#include "gfx.h"
#include "pet_art.h"     // pet_species_name(): the roster's own Spanish name
#include "screen.h"
#include "screen_link.h"   // link_arm_intent(): the section 9 entry points
#include "ui.h"

#define BOX_LIST_ROWS   ((uint8_t)(BOX_SLOTS + 1u))   // ten slots plus "Volver"
#define BOX_ROW_CAP     22
#define BOX_VAL_CAP     8

static uint8_t s_mode = BOXM_LIST;
static uint8_t s_cur  = 0;        // the row inside the current mode
static uint8_t s_slot = 0;        // the slot BOXM_ACTIONS / BOXM_CARD is about

uint8_t box_screen_mode(void)   { return s_mode; }
uint8_t box_screen_cursor(void) { return s_cur; }
uint8_t box_screen_slot(void)   { return s_slot; }

void box_enter(void) {
  s_mode = BOXM_LIST;
  s_slot = 0;
  // Open on the Pebble the player is carrying: that is the row they came to
  // look at, and it is the only row whose position they already know.
  const uint8_t act = box_active();
  s_cur = (act == BOX_ACTIVE_NONE || act >= BOX_SLOTS) ? 0u : act;
}

void box_update(uint32_t) {}
void box_leave(void) { s_mode = BOXM_LIST; }

// -----------------------------------------------------------------------------
//  ROW TEXT
//
//  "1 CANTO" plus a right-aligned "Nv7", and a leading '*' on the Pebble that
//  is out walking. A slot with no nickname shows its SPECIES: inventing a name
//  here would disagree with the one HOME shows for the same Pebble.
//
//  AND IT SHOWS THE SPECIES IT ACTUALLY IS (P4-C4a). Both rows used to read
//  S_SPECIES(gene_species(genome)) - the sixteen-word genome vocabulary
//  (BLOB / ORUGA / PAJARO / GATO / SETA / ...) that predates the roster - so ten
//  slots holding ten different creatures could all be called SETA, and an
//  evolution never changed a word. The roster carries all 36 names; the genome
//  word is the fallback for a Pebble with no species row, which is the same
//  ladder ui_pet_name() walks.
// -----------------------------------------------------------------------------
static const char* slot_species_name(const PebbleInstance& p) {
  const char* sp = pet_species_name(p.species_id);
  return sp ? sp : S_SPECIES(gene_species(p.genome));
}

static void slot_row(uint8_t slot, char* out, size_t cap, char* val, size_t vcap) {
  const PebbleInstance* p = box_peek(slot);
  if (!p) {
    snprintf(out, cap, "%u %s", (unsigned)(slot + 1u), S(STR_BOX_EMPTY));
    val[0] = '\0';
    return;
  }
  // THE NAME IS APPENDED, NOT PRINTED. snprintf("%s") cuts on a BYTE, and the
  // only wide part of this row is the name - so once a name can hold a
  // two-byte character (a nickname is stored as Latin-1: core/utf8.h) the cut
  // lands inside a sequence and hands drawUTF8() a broken lead byte.
  snprintf(out, cap, "%u %s", (unsigned)(slot + 1u),
           (slot == box_active()) ? S(STR_BOX_ACTIVE) : "");
  if (p->nickname[0] != '\0') (void)u8_cat_latin1(out, (uint16_t)cap, p->nickname);
  else                        (void)u8_cat(out, (uint16_t)cap, slot_species_name(*p));
  snprintf(val, vcap, "%s%u", S(STR_ST_LEVEL), (unsigned)p->level);
}

static void draw_slot_list(uint16_t title) {
  static char rows[BOX_LIST_ROWS][BOX_ROW_CAP];
  static char vals[BOX_LIST_ROWS][BOX_VAL_CAP];
  const char* items[BOX_LIST_ROWS];
  const char* values[BOX_LIST_ROWS];

  for (uint8_t i = 0; i < BOX_SLOTS; ++i) {
    slot_row(i, rows[i], sizeof rows[i], vals[i], sizeof vals[i]);
    items[i]  = rows[i];
    values[i] = vals[i];
  }
  items[BOX_SLOTS]  = S(STR_ITEM_BACK);
  values[BOX_SLOTS] = "";

  char tag[8];
  snprintf(tag, sizeof tag, "%u/%u", (unsigned)box_count(), (unsigned)box_capacity());
  gfx_header(S(title), tag);
  gfx_list(items, BOX_LIST_ROWS, s_cur, values, ui_now_ms());
  gfx_countdown(ui_idle_ms());
  gfx_affordance(S(STR_AF_NEXT), S(STR_AF_BACK_SEL));
}

// -----------------------------------------------------------------------------
//  THE ACTION LIST
// -----------------------------------------------------------------------------
static const uint16_t kAction[BOXA_COUNT] = {
  STR_BOX_ACT_VIEW, STR_BOX_ACT_ACTIVATE, STR_BOX_ACT_SWAP,
  STR_BOX_ACT_RELEASE, STR_BOX_ACT_TRADE, STR_BOX_ACT_BREED, STR_ITEM_BACK
};

static void draw_actions(void) {
  const char* items[BOXA_COUNT];
  for (uint8_t i = 0; i < BOXA_COUNT; ++i) items[i] = S(kAction[i]);
  char tag[8];
  snprintf(tag, sizeof tag, "%u", (unsigned)(s_slot + 1u));
  gfx_header(S(STR_BOX_TITLE), tag);
  gfx_list(items, BOXA_COUNT, s_cur, nullptr, ui_now_ms());
  gfx_countdown(ui_idle_ms());
  gfx_affordance(S(STR_AF_NEXT), S(STR_AF_BACK_SEL));
}

// -----------------------------------------------------------------------------
//  THE STORED PEBBLE'S CARD
//
//  A stored Pebble is NOT simulated: it has no smoothed bars, no mood face and
//  no bond score, because none of those are stored per instance. The PEBBLE
//  pages (SCR_STATUS) show the pet the simulation is running, so BOXA_VIEW
//  pushes them for the ACTIVE slot and draws this instead for a stored one -
//  what the schema actually holds, and nothing invented to fill a layout.
// -----------------------------------------------------------------------------
static const uint16_t kCareLabel[PB_CARE_COUNT] = {
  STR_STAT_HUNGER, STR_STAT_HAPPINESS, STR_STAT_HEALTH,
  STR_STAT_HYGIENE, STR_STAT_ENERGY
};

static uint8_t care_pct(int32_t milli) {
  if (milli <= 0) return 0;
  if (milli >= PB_CARE_MILLI_MAX) return 100;
  return (uint8_t)(milli / (PB_CARE_MILLI_MAX / 100L));
}

static void draw_card(void) {
  const PebbleInstance* p = box_peek(s_slot);
  if (!p) { draw_slot_list(STR_BOX_TITLE); return; }

  char tag[10];
  snprintf(tag, sizeof tag, "%s%u", S(STR_ST_LEVEL), (unsigned)p->level);
  char name[PB_NAME_DRAW_CAP];
  name[0] = '\0';
  if (p->nickname[0] != '\0') (void)u8_cat_latin1(name, (uint16_t)sizeof name, p->nickname);
  else                        (void)u8_cat(name, (uint16_t)sizeof name, slot_species_name(*p));
  gfx_header(name, tag);

  char line[32];
  snprintf(line, sizeof line, "%s %u   %s %u",
           S(STR_ST_GEN), (unsigned)p->genome.generation,
           S(STR_ST_HP),  (unsigned)p->hp_cur);
  gfx_text_fit(GF_TINY, 2, (int16_t)(UI_HDR_H + 6), 124, line);

  // The five stored care stats, two columns, 8 px pitch: the whole point of a
  // stored Pebble is that these RECOVER, so they are what the card is for.
  for (uint8_t i = 0; i < PB_CARE_COUNT; ++i) {
    const int16_t x = (int16_t)(2 + (i / 3) * 64);
    const int16_t y = (int16_t)(UI_HDR_H + 10 + (i % 3) * 10);
    const uint8_t pct = care_pct(p->care[i]);
    // GF_BODY AND NOT GF_TINY, AND THE COLUMN IS WIDER FOR IT (P10-C6).
    // GF_TINY is u8g2_font_4x6_tr, 95 glyphs, ASCII only (ui/render.h and
    // ui/gfx.h:37 both say so) and drawUTF8() emits NOTHING and ADVANCES
    // NOTHING for a codepoint the face lacks - so "Ánimo" read "nimo" and
    // "Energía" read "Energa" on every board while every golden was correct,
    // because tests/fakes/gfx_fb.cpp painted a synthetic glyph for any
    // codepoint at a fixed advance. The recorder in that fake now refuses it.
    // 35 px is "Energía" at GF_BODY's 5 px advance; the bar moves to x + 37 so
    // the second column still ends at 125 of OLED_W's 128.
    gfx_text_fit(GF_BODY, x, (int16_t)(y + 6), 35, S(kCareLabel[i]));
    gfx_bar((int16_t)(x + 37), (int16_t)(y + 1), 22, 6, pct);
  }

  gfx_countdown(ui_idle_ms());
  gfx_affordance(nullptr, S(STR_AF_BACK));
}

void box_render(void) {
  switch (s_mode) {
    case BOXM_ACTIONS: draw_actions(); break;
    case BOXM_SWAP:    draw_slot_list(STR_BOX_ACT_SWAP); break;
    case BOXM_CARD:    draw_card(); break;
    default:           draw_slot_list(STR_BOX_TITLE); break;
  }
}

// -----------------------------------------------------------------------------
//  INPUT. The section 7 grammar: A steps, HOLD_R chooses, B walks back one
//  mode at a time (SF_OWNS_BACK), BOTH is help.
// -----------------------------------------------------------------------------
static uint8_t ring_next(uint8_t cur, uint8_t n) {
  return n ? (uint8_t)((cur + 1u) % n) : 0u;
}

static uint8_t mode_rows(void) {
  switch (s_mode) {
    case BOXM_ACTIONS: return (uint8_t)BOXA_COUNT;
    case BOXM_CARD:    return 1u;
    default:           return BOX_LIST_ROWS;
  }
}

static void to_list(void) { s_mode = BOXM_LIST; s_cur = s_slot; gfx_list_reset(); }

// One level up the ladder LIST -> ACTIONS -> {SWAP, CARD}, landing on the
// action row the sub-mode was opened from. BOXM_CARD and BOXM_SWAP both hang
// off the action list, so dropping straight back to the ten slots would skip a
// level the header promises B walks one at a time.
static void to_actions(uint8_t row) {
  s_mode = BOXM_ACTIONS;
  s_cur  = row;
  gfx_list_reset();
}

// The release commit lands in ui.cpp (it needs flash), and it leaves this
// screen pointed at an action list for a slot that is now empty.
void box_screen_to_list(void) { to_list(); }

static void choose_action(void) {
  switch (s_cur) {
    case BOXA_VIEW:
      if (s_slot == box_active()) ui_push(SCR_STATUS);
      else                      { s_mode = BOXM_CARD; }
      break;
    case BOXA_ACTIVATE:
      ui_box_activate(s_slot);
      to_list();
      break;
    case BOXA_SWAP:
      s_mode = BOXM_SWAP;
      s_cur  = s_slot;
      gfx_list_reset();
      ui_toast(STR_BOX_SWAP_PICK);
      break;
    case BOXA_RELEASE:
      // B4: releasing the Pebble you are carrying is refused outright, and the
      // refusal is said here so the player is not sent through two dialogs to
      // be told no at the end of them.
      if (s_slot == box_active()) { ui_toast(STR_BOX_NO_RELEASE_ACTIVE); break; }
      ui_box_release(s_slot);           // opens the first of the two confirms
      break;
    case BOXA_TRADE:
    case BOXA_BREED:
      // Spec section 9's "initiate breeding; initiate trade" (P7-C2). The BOX
      // is where the player is already looking at the Pebble they mean, so this
      // row PRE-SELECTS it and opens LINK with that intent rather than making
      // them find the same creature again from the other side.
      //
      // IT CONSENTS TO NOTHING. link_arm_intent() sets a pre-selection and
      // touches no radio; the session still needs A on the LINK card and A on
      // the other device (ui/screen_link.h). And the OPERATIONS themselves are
      // P7-C4 and P7-C5: the card will say so. That is deliberately better than
      // the toast this used to be - the entry point is what P7-C2 owes, and a
      // player who takes it now sees the peer list and the real menu.
      link_arm_intent((uint8_t)((s_cur == (uint8_t)BOXA_TRADE) ? LOP_TRADE : LOP_BREED),
                      s_slot);
      ui_push(SCR_LINK);
      break;
    default: to_list(); break;
  }
}

static void choose_slot(void) {
  if (s_cur >= BOX_SLOTS) { ui_back(); return; }     // the "Volver" row
  if (!box_occupied(s_cur)) { ui_toast(STR_BOX_EMPTY); return; }
  s_slot = s_cur;
  s_mode = BOXM_ACTIONS;
  s_cur  = 0;
  gfx_list_reset();
}

static void choose_swap_target(void) {
  // The "Volver" row and the slot itself both mean "never mind": that is a
  // cancel, so it goes back where the swap was chosen, not out to the list.
  if (s_cur >= BOX_SLOTS || s_cur == s_slot) { to_actions((uint8_t)BOXA_SWAP); return; }
  ui_box_swap(s_slot, s_cur);
  s_slot = s_cur;
  to_list();
  ui_toast(STR_BOX_SWAP_DONE);
}

void box_input(Gesture g) {
  switch (g) {
    case GST_TAP_L:
    case GST_HOLD_L:
      if (s_mode != BOXM_CARD) s_cur = ring_next(s_cur, mode_rows());
      break;
    case GST_HOLD_R:
      switch (s_mode) {
        case BOXM_LIST:    choose_slot();        break;
        case BOXM_ACTIONS: choose_action();      break;
        case BOXM_SWAP:    choose_swap_target(); break;
        // BOXM_CARD has one row and it reads "back".
        default:           to_actions((uint8_t)BOXA_VIEW); break;
      }
      break;
    // SF_OWNS_BACK: B walks back through the modes and only leaves the screen
    // from the top one, so a half-finished swap is cancelled rather than
    // committed by the same gesture that closes the Box.
    case GST_TAP_R:
      switch (s_mode) {
        case BOXM_LIST:    ui_back();                      break;
        case BOXM_ACTIONS: to_list();                      break;
        case BOXM_SWAP:    to_actions((uint8_t)BOXA_SWAP); break;
        default:           to_actions((uint8_t)BOXA_VIEW); break;
      }
      break;
    case GST_BOTH: ui_help(STR_HLP_BOX); break;
    default: break;
  }
}
