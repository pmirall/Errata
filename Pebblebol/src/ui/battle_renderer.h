// =============================================================================
//  PEBBLEBOL - ui/battle_renderer.h
//  THE COMBAT FIELD (plan P4-C4).
//
//  Two 24x24 creatures facing each other on a 128x64 panel, their name/HP
//  panels, the bench pips and one line of transcript. Nothing else: the header,
//  the list widget, the affordance strip and every decision about WHAT to say
//  belong to ui/screen_battle.cpp, which is also the only file that ever sees a
//  BattleState.
//
//  WHY THE VIEW AND NOT THE STATE. This module takes a BattleCombatantArt -
//  seven already-derived numbers - rather than a `const BattleState&`. The
//  layering rule is the same one ui/pet_view.h states for the body animator:
//  a renderer that reads the model re-derives the model's rules, and this one
//  would then own a second opinion about what "fainted" means and how a
//  percentage is rounded. It also makes the module compile with no game/
//  header at all, so a golden of the field is a statement about the DRAWING.
//
//  WHY IT IS PURE, AGAINST THE PLAN'S OWN WORDING. Plan P4-C4 names
//  `rd_bar / rd_flash / rd_shake / rd_dither_rect + gfx_xbm`. Three of those
//  five have a gfx.h face that forwards to exactly them on the device
//  (ui/gfx_u8g2.cpp: gfx_bar -> the shared widget, gfx_dither_rect ->
//  rd_dither_rect, gfx_xbm -> drawXBM), and the two that do NOT - rd_flash and
//  rd_shake - are frame-level effects that draw nothing. Routing the drawing
//  through gfx.h and the two effects through ui.h seams is what lets
//  tests/test_screens.cpp render this field at the real 128x64 and diff five
//  goldens; a render.h translation unit could not have been snapshotted at all,
//  and the survey's fallback for that was a static sprite standing in for the
//  thing under test.
//
//  THE FACING IS A REAL MIRROR, not a second sprite. ui/xbm_mirror.h is
//  ui/petfx.cpp's own flip, lifted out so there is one copy of it.
//
//  NO PER-FRAME HEAP, structurally: the only buffer is one file-scope
//  BR_BODY_BYTES mirror scratch, written before it is read on every call.
//
//  PURE translation unit: gfx.h, the atlas, xbm_mirror.h, strings. No
//  render.h, no Arduino.h, no U8G2, no game/ header.
//
//  Identifiers and comments: English. Every user-facing byte: strings_es.h.
// =============================================================================
#ifndef PB_BATTLE_RENDERER_H
#define PB_BATTLE_RENDERER_H

#include <stdint.h>

#include "../core/config.h"

// -----------------------------------------------------------------------------
//  GEOMETRY. The content band is rows UI_CONTENT_Y..UI_CONTENT_BOTTOM (11..55)
//  and these numbers partition it. The foe stands upper-right and the player
//  lower-left, so each body sits diagonally opposite the other's panel and
//  neither can ever overlap it however long a name is (gfx_text_fit clamps the
//  name to the panel width).
// -----------------------------------------------------------------------------
#define BR_BODY_W        24
#define BR_BODY_H        24

#define BR_FOE_BODY_X    94
#define BR_FOE_BODY_Y    12
#define BR_FOE_PANEL_X    2
#define BR_FOE_PANEL_Y   12
#define BR_YOU_BODY_X    10
#define BR_YOU_BODY_Y    22
#define BR_YOU_PANEL_X   66
#define BR_YOU_PANEL_Y   28

#define BR_PANEL_W       58
#define BR_BAR_H          5
#define BR_MSG_BASE      54          // the transcript line's text baseline
#define BR_SHADOW_H       2          // the contact shadow under each body

// One 24x24 XBM frame. The mirror scratch is exactly this and no more.
#define BR_BODY_BYTES    (((BR_BODY_W + 7) / 8) * BR_BODY_H)

// -----------------------------------------------------------------------------
//  ONE COMBATANT, ALREADY DERIVED.
//
//  `art_key` is ui/pet_art.h's pet_art_key(species_id, gene_species) - the
//  SAME resolution HOME and the BOX use, which is what makes the creature in a
//  battle the creature the player has been looking after. Since P9-C3 it is not
//  folded at all: every body in the atlas is 24x24 and there is one per species,
//  so the fighter on the field is the species. It used to be folded into the
//  eight authored 24x24 BABY designs, because the atlas also held 40x40 adults
//  and two of those plus two name panels do not fit on a 128x64 panel together;
//  br_body_set_id() still passes STAGE_BABY, which now changes nothing and is
//  kept so the clamp lives in one place.
// -----------------------------------------------------------------------------
struct BattleCombatantArt {
  const char* name;      // never NULL; drawn clipped to BR_PANEL_W
  uint8_t     art_key;
  uint8_t     level;     // 0 hides the level
  uint8_t     hp_pct;    // 0..100
  uint8_t     alive;     // team members still standing, drawn as pips
  uint8_t     team;      // team size, the pip track's length
  uint8_t     fainted;   // 1 = draw the body dissolved rather than solid
  uint8_t     struck;    // 1 = overprint the body this frame (the impact)
};

// The whole field: both bodies, both panels, both pip tracks, and `message`
// on the transcript line (NULL or "" draws no line).
void br_draw_field(const BattleCombatantArt& foe, const BattleCombatantArt& you,
                   uint8_t frame, const char* message);

// One body, exposed so the INTRO can draw a bench line-up and so a test can
// assert the flip without going through a whole field.
// `face_left` mirrors the frame through ui/xbm_mirror.h.
void br_draw_body(int16_t x, int16_t y, uint8_t art_key, uint8_t frame,
                  bool face_left, bool fainted, bool struck);

// The atlas set id `art_key` resolves to at BATTLE size. Exposed for the tests:
// asserting that two species draw two different bodies needs the id, not the
// pixels.
uint8_t br_body_set_id(uint8_t art_key);

#endif  // PB_BATTLE_RENDERER_H
