// =============================================================================
//  PEBBLEBOL - app/onboarding.h
//  FIRST BOOT: the three questions, and the rule for who is asked them.
//
//  Spec section 65 and plan P10-C4: "name the device, set the time on the
//  existing P2-C6 screen, pick a starter from three". The screens are
//  ui/screen_setup.cpp (name, starter) and the existing ui/screen_time.cpp; what
//  lives HERE is the only part that can be wrong in an interesting way - WHICH
//  step a boot resumes at.
//
//  WHY IT IS A PERSISTED STEP AND NOT AN INFERENCE. The obvious implementation
//  is app/app.cpp's existing test, `boot == BOOT_FIRST_RUN`. It is wrong the
//  moment onboarding can be interrupted: the player names the device, the
//  device saves, the power is cut - and the next boot is no longer a first run,
//  so the remaining two questions are never asked and the starter the player
//  was about to choose is silently the default. That is the exact class of
//  defect this project has hit in nine consecutive phases: a rule that reads
//  the state the DEVELOPER had in mind rather than the one the device is in.
//
//  So the step is a field, it is written before the first question is asked,
//  and every step writes the next one before it hands over.
//
//  AND THE ENCODING IS UPSIDE DOWN ON PURPOSE. OB_DONE is ZERO, so a Config
//  that has never heard of this field - every save written before P10-C4, and
//  gs_cfg_defaults()'s memset - reads DONE and is never asked anything. A
//  device that has been played for months must not be handed a setup wizard
//  because a new firmware learned a new word. The wizard is entered from
//  BOOT_FIRST_RUN and from nowhere else; it is RESUMED from the field.
//
//  PURE translation unit: core/nt_types.h and stdint. No Arduino, no gfx, no
//  Box - ob_starter_species() answers a species id and app/app.cpp is what
//  makes a Pebble out of it.
// =============================================================================
#ifndef PB_ONBOARDING_H
#define PB_ONBOARDING_H

#include <stdint.h>

#include "../core/nt_types.h"

// The steps, in the order they are asked. The VALUES are persisted (two bits of
// Config.flags / ConfigV2.flags), so they may not be renumbered without a save
// migration - and OB_DONE must stay 0 for the reason in the banner.
enum ObStep : uint8_t {
  OB_DONE    = 0,     // setup is over, and this is what an unset field means
  OB_NAME    = 1,     // ui/screen_setup.cpp, SCR_SETUP_NAME
  OB_TIME    = 2,     // ui/screen_time.cpp, SCR_TIME - the P2-C6 screen, reused
  OB_STARTER = 3,     // ui/screen_setup.cpp, SCR_SETUP_STARTER
  OB_STEP_COUNT = 4
};

// The three starters. One per corner of the section 12 type chart, all stage 0,
// all common, all with an evolution line - onboarding.cpp static_asserts every
// one of those against the roster, so a content edit that breaks the trio fails
// the build rather than shipping a starter that cannot grow.
#define OB_STARTER_COUNT 3
uint8_t ob_starter_species(uint8_t index);      // 0 for an index out of range

// The persisted field.
uint8_t ob_step(const Config& c);
void    ob_set_step(Config& c, uint8_t step);

// WHERE THIS BOOT SHOULD START. OB_DONE means "ask nothing".
//
//   readonly   -> OB_DONE. A read-only session cannot save a single answer, so
//                 asking three questions and throwing all three away is worse
//                 than asking none. The save error the user is about to see is
//                 the only thing that matters on that boot.
//   a stored step -> that step, WHATEVER the boot kind says. This is the case a
//                 power cut lands in and it is checked FIRST for that reason.
//   first_run  -> OB_NAME. A device with no save at all has answered nothing.
//   otherwise  -> OB_DONE, which is what every save that predates this feature
//                 and every device that finished decodes to.
uint8_t ob_boot_step(bool first_run, bool readonly, const Config& c);

// The screen a step is asked on, and the step that follows it.
ScreenId ob_screen_for(uint8_t step);
uint8_t  ob_next(uint8_t step);

// Is this step (the one a screen is being drawn for) part of a live setup?
// The setup screens are ALSO reachable from SETTINGS later - the name and the
// clock are ordinary settings once the device is set up - so a screen has to
// know which of the two it is being asked to be.
inline bool ob_active(uint8_t step) { return step != (uint8_t)OB_DONE; }

#endif  // PB_ONBOARDING_H
