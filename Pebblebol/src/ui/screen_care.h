// =============================================================================
//  PEBBLEBOL - ui/screen_care.h
//  The two vertical lists MENU opens: CARE and PLAY.
//
//  CARE is the old FEED list widened into the whole of spec section 27's verb
//  set - meal, snack, clean, medicine - so the ring does not have to carry one
//  entry per verb any more. The light row went with the light mechanic itself
//  (P3-C2b): the creature sleeps by the sun now, not by a switch. PLAY is
//  unchanged: the three on-device minigames plus the way out, until P3-C4
//  replaces them with the section 29 set.
//
//  PLAY also carries "COMBATE DE PRACTICA" since P4-C4 - the ONLY single-device
//  battle entry in V1, because there are no wild battles (spec section 68 rule
//  18). The DIAG console's test_battle is the other entry and it is a developer
//  action, not a way to play.
//
//  They share one grammar (navigation invariant 4, and the BOTH-for-help rule)
//  and therefore one input handler.
// =============================================================================
#ifndef PB_SCREEN_CARE_H
#define PB_SCREEN_CARE_H

#include <stdint.h>

#include "../core/nt_types.h"
#include "../minigames/minigame.h"   // MgId: the PLAY list's row order IS it

// CARE list rows, in display order.
enum CareRow : uint8_t {
  CARE_MEAL = 0,
  CARE_SNACK,
  CARE_CLEAN,
  CARE_MEDICINE,
  CARE_BACK,
  CARE_ROWS
};

// The PLAY list: one row per game IN MgId ORDER, then the practice battle, then
// "Volver". The static_assert is what makes a mismatch a build error rather
// than a game that launches its neighbour - screen_care.cpp's comment has
// promised it since P3-C4a, and P3-C4b is the commit that made it true.
//
// P4-C4 WIDENED THE FOLD RATHER THAN APPENDING A ROW, and the difference
// matters: play_input() launches game number s_play WITH NO LOOKUP TABLE, so a
// battle row inserted anywhere among the first MG_ID_COUNT rows would silently
// start the wrong minigame while every count in the tree still matched. The
// rule is now "the first MG_ID_COUNT rows are the games, then PLAY_BATTLE, then
// PLAY_BACK", and play_rows_are_in_mgid_order() in screen_care.cpp checks all
// three parts of it at compile time.
#define PLAY_ROWS     ((uint8_t)((uint8_t)MG_ID_COUNT + 2u))
#define PLAY_BATTLE   ((uint8_t)MG_ID_COUNT)          // the row after the games
#define PLAY_BACK     ((uint8_t)(PLAY_ROWS - 1u))
static_assert(PLAY_ROWS == (uint8_t)MG_ID_COUNT + 2u,
              "PLAY needs one row per minigame, the practice battle, and the way out");
static_assert(PLAY_BATTLE < PLAY_BACK,
              "the battle row must sit between the games and the way out");

// The widest list either screen draws. draw_str_list() sizes its row array by
// this and clamps NOTHING: it used to be CARE_ROWS (5) with a silent
// `if (n > CARE_ROWS) n = CARE_ROWS`, which at PLAY_ROWS = 7 would have drawn
// only the first five rows - BORRAR and "Volver" never appearing - while
// list_common() still rang the cursor through all seven and play_input()
// happily launched game 5 from an invisible row.
#define LIST_ROWS_MAX  ((CARE_ROWS > PLAY_ROWS) ? (uint8_t)CARE_ROWS : (uint8_t)PLAY_ROWS)

void    care_enter(void);
void    care_render(void);
void    care_input(Gesture g);
uint8_t care_cursor(void);

void    play_enter(void);
void    play_render(void);
void    play_input(Gesture g);
uint8_t play_cursor(void);

#endif  // PB_SCREEN_CARE_H
