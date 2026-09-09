// =============================================================================
//  ERRATA - ui/screen_home.h
//  HOME (spec section 8): the screen the owner stares at for days.
//
//  It shows exactly what section 8 asks for and nothing else:
//
//      [BUG SPRITE]  Name  Level  HP  Hunger  Happiness  XP
//
//  laid out as a name/level strip over a full-width XP rule, the stage the
//  Bug lives on, and three meters in the right-hand HUD column that the
//  body can never reach (config.h UI_HUD_R_BEGIN).
//
//  THE BODY LAYER IS BOUND, NOT LINKED. Everything in the stage that MOVES -
//  petfx's wandering automaton, the action choreography, the emotes, the
//  welcome-back banner - lives in ui.cpp and reaches the panel through
//  render.h, which a pure screen may not include. HOME therefore asks for it
//  through one function pointer: bound on the device, NULL on the host, where
//  HOME draws the species sprite standing still instead. That fallback is not
//  a stub - it is what the golden snapshot is a statement about, and it is
//  what a build with no petfx would show.
// =============================================================================
#ifndef ER_SCREEN_HOME_H
#define ER_SCREEN_HOME_H

#include <stdint.h>

#include "../core/nt_types.h"

// -----------------------------------------------------------------------------
//  GEOMETRY. The stage is rows HOME_STAGE_Y..HOME_STAGE_BOTTOM and columns
//  PETFX_STAGE_L..PETFX_STAGE_R; ui.cpp static_asserts these against petfx.h,
//  which a pure screen cannot include.
// -----------------------------------------------------------------------------
#define HOME_XP_RULE_Y     8                  // the 1 px XP progress rule
#define HOME_STAGE_Y       (HOME_XP_RULE_Y + 1)   // 9  == SPRITE_AREA_Y
#define HOME_FLOOR_Y      52                  // == PETFX_FLOOR_Y
#define HOME_METER_X     115                  // == UI_HUD_R_BEGIN
#define HOME_METER_W      12                  // == UI_HUD_BADGE_W
#define HOME_METER_Y0     10                  // first meter, then every 14 px
#define HOME_METER_PITCH  14

// The animated stage layer. Returns false when it drew nothing, in which case
// HOME falls back to the static sprite. `frame` is the two-frame idle phase.
// The second hook ends whatever it started: leaving HOME by ANY route (back,
// home, push) must end the action film and release the petfx hold it took, or
// the pet stays nailed to the floor until the device is power-cycled.
typedef bool (*HomeBodyFn)(uint8_t frame);
typedef void (*HomeLeaveFn)(void);
void home_bind_body(HomeBodyFn draw, HomeLeaveFn leave);

// The screen-table hooks.
void home_render(void);
void home_input(Gesture g);
void home_leave(void);

#endif  // ER_SCREEN_HOME_H
