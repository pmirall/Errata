// =============================================================================
//  PEBBLEBOL - ui/screen_status.h
//  The PEBBLE pages (spec section 8's "PEBBLE" menu entry), two of them:
//
//    STATUS_A  the vitals: level, HP, the six care stats and the age.
//    STATUS_B  the genome: species, pattern, generation, sex, luck, mutations,
//              temperament - and, held down long enough, the god-mode door.
//
//  A tap on the left button flips between them; that is the whole navigation.
// =============================================================================
#ifndef PB_SCREEN_STATUS_H
#define PB_SCREEN_STATUS_H

#include <stdint.h>

#include "../core/nt_types.h"

void status_a_enter(void);
void status_a_render(void);
void status_b_enter(void);
void status_b_render(void);
void status_input(Gesture g);

#endif  // PB_SCREEN_STATUS_H
