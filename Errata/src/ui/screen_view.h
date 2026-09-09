// =============================================================================
//  ERRATA - ui/screen_view.h
//  WHAT A SCREEN IS ALLOWED TO KNOW ABOUT THE ACTIVE BUG.
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
#ifndef ER_SCREEN_VIEW_H
#define ER_SCREEN_VIEW_H

#include <stdint.h>

#include "../core/nt_types.h"
#include "../persistence/save_schema.h"    // ER_NAME_DRAW_CAP

struct BugView {
  // Identity
  // ALREADY UTF-8 AND ALREADY TRUNCATED ON A CODEPOINT BOUNDARY. ui.cpp's
  // ui_pet_name() fills it, and the nickname rung it can take is stored as
  // Latin-1 (core/utf8.h), so this is the DRAW cap and not the schema one.
  char     name[ER_NAME_DRAW_CAP];
  Genome   genome;
  uint8_t  species_id;
  uint8_t  level;                   // 1..30
  uint16_t xp;                      // inside the current level
  // What the current level costs to leave, from XP_TABLE[31] (data/balance.h).
  // ZERO means XP_LEVEL_MAX: the curve is finished, and a screen must draw that
  // as "done" rather than dividing by it.
  uint16_t xp_next;

  // Battle numbers (spec section 8 "HP / health"). hp_max is derived, never
  // stored, and stays a placeholder until the Phase 4 stat block exists.
  uint16_t hp_cur;
  uint16_t hp_max;

  // Care, as PERCENTAGES already smoothed for display, indexed by StatId.
  uint8_t  care_pct[ST_COUNT];
  uint8_t  mood_pct;                // 0..100, the care-quality score
  // NO mood_face (P4-C6). ui.cpp:1939 wrote it every frame and no screen ever
  // drew it: its consumer, sprite_mood_face(), had had zero references in the
  // whole tree since the last caller went away at P2-C11b, and the 144 B of
  // 12x12 art behind it (spr_mood12) was gc-sectioned out of every build. The
  // whole chain is deleted rather than carried; recover the art with
  // `git show 250f73e:Pebblebol/src/data/sprites.h` if P10 designs a screen
  // that wants a mood badge. The mood WORD ladder is untouched and live -
  // ui.cpp's mood_of() still gates the HOME heart on MOOD_FELIZ.

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

  // §55's status, as a bit rather than as a PBS_ mask: a pure screen may not
  // reach game/corruption.h's BugInstance, and the ONE thing HOME does with
  // it is decide whether to XOR ui/corrupt_fx.cpp's glitch rows over the body.
  //
  // IT IS HERE BECAUSE THE STILL BODY PATH IS THE ONE A GOLDEN CAN SEE (P10-C3).
  // The glitch shipped at P9-C5 with its geometry in a pure, host-tested module
  // and its PAINTING in ui/petfx.cpp - a translation unit no host binary
  // compiles - so no snapshot in this repository had ever drawn a corrupted
  // creature. ui/screen_home.cpp paints the same rows now, and home_corrupted
  // is the golden.
  uint8_t  corrupted;               // 1 = PBS_CORRUPTED is set on the Bug
  // HOURS LEFT OF THE CORRUPTION, 0..24, ROUNDED UP (P10-C6). Spec section 55
  // makes corruption a 24 h state and the product had no readout of it at all:
  // one centred line at onset that a player can walk past, a shimmer on HOME,
  // and then nothing - no page showing the status, no hours remaining, no
  // AlertId, and cor_left_s() had no caller outside the tests. A player whose
  // creature suddenly glitches had no way to learn whether it still was, or
  // that it wears off. 1 means "less than an hour to go", never 0, so the
  // reading and the glitch can never disagree.
  uint8_t  corrupt_h;

  uint8_t  present;                 // 0 = there is no active Bug at all
};

// The provider. ui.cpp binds one at ui_begin(); until then, and on the host
// until a fixture is bound, ui_view() answers NULL and every screen falls back
// to its "no hay nadie" frame.
typedef const BugView* (*BugViewFn)(void);
void              ui_bind_view(BugViewFn fn);
const BugView* ui_view(void);

#endif  // ER_SCREEN_VIEW_H
