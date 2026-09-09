// =============================================================================
//  ERRATA - ui/screen_time.h
//  TIME ENTRY (spec section 26, "ask for the time").
//
//  The device has no radio policy and no SNTP any more, so the only way a
//  Errata learns what day it is on its own is a human typing it here.
//  Five fields, two buttons:
//      TAP L / HOLD L (repeating)  next field   (HOLD L confirms)
//      TAP R                       +1 on the current field, wrapping
//      HOLD R                      +1 auto-repeat
//      BOTH                        leave without saving
//      LONG BOTH                   HOME (invariant 2, untouched)
//
//  HOLD R is BACK everywhere else, and ui.cpp exempts THIS screen and only
//  this screen, because the recogniser deliberately never repeats HOLD_R and a
//  date entered one tap at a time is unusable. Confirming is a HOLD, so a
//  stray tap can never commit a wrong date.
// =============================================================================
#ifndef ER_SCREEN_TIME_H
#define ER_SCREEN_TIME_H

#include <stdint.h>

#include "../core/nt_types.h"

enum ClkField : uint8_t {
  CLK_YEAR = 0, CLK_MONTH, CLK_DAY, CLK_HOUR, CLK_MIN, CLK_FIELDS
};

#define CLK_YEAR_MIN  2020
#define CLK_YEAR_MAX  2099

void     time_enter(void);
void     time_update(uint32_t now_ms);
void     time_render(void);
void     time_input(Gesture g);

// For the tests: what is on screen and which field the cursor is on.
uint16_t time_field(uint8_t field);
uint8_t  time_cursor(void);

#endif  // ER_SCREEN_TIME_H
