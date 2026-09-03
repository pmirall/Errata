// =============================================================================
//  PEBBLEBOL - ui/screen_view.h
//  WHAT A SCREEN IS ALLOWED TO KNOW ABOUT THE ACTIVE PEBBLE.
//
//  A migrated screen is a pure translation unit and may not reach into
//  game/sim.h, persistence/game_state.h or the smoothing layer that lives in
//  ui.cpp. It reads this flat, already-derived snapshot instead: ui.cpp fills
//  one in per frame from the simulation and the Box, and tests/test_screens.cpp
//  fills one in by hand. Nothing here is stored and nothing here is authority -
//  it is a view, and writing to it changes nothing.
//
//  P2-C11's later `ui/pet_view.{h,cpp}` bullet is the BODY's view (pose,
//  identity, orientation) that petfx.cpp and actfx.cpp will read instead of
//  sim.h. This is the SCREEN's view, and the two are expected to merge there.
//
//  Pure header: stdint plus the shared enums and the genome.
// =============================================================================
#ifndef PB_SCREEN_VIEW_H
#define PB_SCREEN_VIEW_H

#include <stdint.h>

#include "../core/nt_types.h"
#include "../persistence/save_schema.h"    // PB_NICKNAME_CAP

// XP per level until P3-C2 lands XP_TABLE[31] in data/balance.h. The HOME bar
// and the PEBBLE page both draw xp / xp_next, so the placeholder lives in ONE
// place and is a plain constant rather than a curve.
#define PB_XP_PER_LEVEL_PLACEHOLDER  100u

struct PebbleView {
  // Identity
  char     name[PB_NICKNAME_CAP];   // nickname, else the deterministic name
  Genome   genome;
  uint8_t  species_id;
  uint8_t  level;                   // 1..30
  uint16_t xp;                      // inside the current level
  uint16_t xp_next;                 // what the current level costs

  // Battle numbers (spec section 8 "HP / health"). hp_max is derived, never
  // stored, and stays a placeholder until the Phase 4 stat block exists.
  uint16_t hp_cur;
  uint16_t hp_max;

  // Care, as PERCENTAGES already smoothed for display, indexed by StatId.
  uint8_t  care_pct[ST_COUNT];
  uint8_t  mood_pct;                // 0..100, the care-quality score
  uint8_t  mood_face;               // enum Mood, the 12x12 badge index

  // Body
  uint8_t  stage;                   // enum Stage, for the sprite lookup
  uint8_t  minor_form;
  uint8_t  pose;                    // enum SpritePose
  uint8_t  poop_count;
  uint16_t flags;                   // PF_*
  uint32_t age_s;
  // Pre-formatted by ui.cpp with gt_format_elapsed(): the elapsed-time
  // formatter is hardware/gametime.cpp's and a pure screen may not reach it.
  // 24 bytes, matching GT_ELAPSED_BUF.
  char     age_txt[24];

  uint8_t  present;                 // 0 = there is no active Pebble at all
};

// The provider. ui.cpp binds one at ui_begin(); until then, and on the host
// until a fixture is bound, ui_view() answers NULL and every screen falls back
// to its "no hay nadie" frame.
typedef const PebbleView* (*PebbleViewFn)(void);
void              ui_bind_view(PebbleViewFn fn);
const PebbleView* ui_view(void);

#endif  // PB_SCREEN_VIEW_H
