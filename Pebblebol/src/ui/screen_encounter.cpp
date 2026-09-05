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
static uint8_t         s_applied = 0;    // the ITEM / SPECIAL arm has run
static uint8_t         s_mode    = (uint8_t)CSM_READY;
static uint8_t         s_out     = (uint8_t)CAP_ESCAPED;
static uint8_t         s_item    = 0;    // the capture item a throw will spend
static uint16_t        s_reward  = 0;    // XP the SPECIAL burst paid

const EncounterResult& encounter_screen_result(void) { return s_enc; }
uint8_t encounter_screen_cursor(void)  { return s_cursor; }
uint8_t capture_screen_mode(void)      { return s_mode; }
uint8_t capture_screen_outcome(void)   { return s_out; }
uint8_t capture_screen_item(void)      { return s_item; }

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

  uint32_t now_epoch = 0, now_ms = 0;
  uint8_t  cal = 0;
  ui_explore_clock(&now_epoch, &now_ms, &cal);

  if (s_enc.outcome == (uint8_t)ENC_OUT_ITEM) {
    // The drop lands in the bag now, not when the player presses something: a
    // reward that a stray BACK can lose is a reward the player will not trust.
    const uint8_t added = inv_add(ui_inventory(), s_enc.item_id, 1u);
    if (added == 0u) ui_toast(STR_ENC_BAG_FULL);
    ui_explore_commit();
    s_applied = 1u;
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
    ui_explore_commit();
    s_applied = 1u;
  }
}

void encounter_leave(void) { s_cursor = 0; }

void encounter_input(Gesture g)
{
  if (g == (Gesture)GST_BOTH) { ui_help(STR_ENC_HELP); return; }
  if (s_enc.outcome != (uint8_t)ENC_OUT_WILD) return;   // nothing to steer
  if (g == (Gesture)GST_TAP_L) {
    s_cursor = (uint8_t)(s_cursor ^ 1u);
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
  s_mode = (uint8_t)CSM_READY;
  s_out  = (uint8_t)CAP_ESCAPED;
  s_item = best_capture_item();
}

void capture_leave(void) { s_mode = (uint8_t)CSM_READY; }

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
  if (caught) ui_award_xp(XP_CAPTURE, (uint8_t)XP_SRC_CAPTURE);
  ui_explore_commit();
}

void capture_input(Gesture g)
{
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
