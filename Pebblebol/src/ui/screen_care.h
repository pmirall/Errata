// =============================================================================
//  PEBBLEBOL - ui/screen_care.h
//  The two vertical lists MENU opens: CARE and PLAY.
//
//  CARE is the old FEED list widened into the whole of spec section 27's verb
//  set - meal, snack, clean, medicine, light - so the ring does not have to
//  carry one entry per verb any more. PLAY is unchanged: the three on-device
//  minigames plus the way out, until P3-C4 replaces them with the section 29
//  set.
//
//  They share one grammar (navigation invariant 4, and the BOTH-for-help rule)
//  and therefore one input handler.
// =============================================================================
#ifndef PB_SCREEN_CARE_H
#define PB_SCREEN_CARE_H

#include <stdint.h>

#include "../core/nt_types.h"

// CARE list rows, in display order.
enum CareRow : uint8_t {
  CARE_MEAL = 0,
  CARE_SNACK,
  CARE_CLEAN,
  CARE_MEDICINE,
  CARE_LIGHT,
  CARE_BACK,
  CARE_ROWS
};

#define PLAY_ROWS  4        // three minigames plus "Volver"

void    care_enter(void);
void    care_render(void);
void    care_input(Gesture g);
uint8_t care_cursor(void);

void    play_enter(void);
void    play_render(void);
void    play_input(Gesture g);
uint8_t play_cursor(void);

#endif  // PB_SCREEN_CARE_H
