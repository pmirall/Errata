// =============================================================================
//  PEBBLEBOL - ui/ceremony.h
//  THE CEREMONY: the 4,480 ms show a Pebble gets when it changes into
//  something else. Lifted out of ui.cpp by P2-C11c (plan section 1.4,
//  "ceremony.cpp (ex hatch)") and PARAMETERISED, because Phase 3's EVOLUTION
//  state (P3-C3) runs the same choreography over a different event.
//
//  TWO KINDS, one timeline:
//
//    CEREMONY_HATCH   the whole show. The egg rocks, cracks, flashes, throws
//                     its shell outwards, and the baby is revealed by a
//                     descending dither, looks around and is named.
//    CEREMONY_EVOLVE  the same show from the FLASH onwards. There is no shell
//                     to rock or crack - the animal is already standing there
//                     - so the clock starts at HATCH_T_FLASH and the shard
//                     phase throws sparks instead of fragments.
//
//  ORDERING IS THE WHOLE SAFETY ARGUMENT. The model change (sim_hatch(), and
//  in Phase 3 evolution_apply()) has ALREADY run and has ALREADY been flushed
//  to flash before the first frame below is drawn, so every phase here is pure
//  presentation. A brownout half way through reboots into an ordinary baby:
//  the player loses the show, never the pet. Deferring the model change to the
//  END would do the opposite.
//
//  This is a DEVICE translation unit: it drives the panel's flash / shake
//  registers and draws the body through petfx. ui/screen_evolution.cpp is the
//  pure screen that hosts it, and binds ceremony_draw() the way HOME binds its
//  body layer.
// =============================================================================
#ifndef PB_CEREMONY_H
#define PB_CEREMONY_H

#include <stdint.h>

#include "pet_view.h"

enum CeremonyKind : uint8_t {
  CEREMONY_NONE = 0,
  CEREMONY_HATCH,
  CEREMONY_EVOLVE
};

// Where the body comes from. ui.cpp binds it. A NULL answer means "no pet", and
// the ceremony then ENDS rather than drawing frames with nothing in them: the
// renderer clears the buffer every frame, so a phase that draws no body is a
// black panel, not a held one. (This header used to claim the phases "simply
// hold the last frame"; they did not, and the claim is the sort a future change
// gets read against - see ceremony.cpp.)
typedef const PetView* (*CeremonyBodyFn)(void);
void     ceremony_bind_body(CeremonyBodyFn fn);

// Arm the show. Returns false when one is already running or when the last one
// ended so recently that this is the SAME event arriving twice (the manual rub
// path calls sim_hatch() itself and the simulation then reports SIM_EV_HATCHED
// one tick later, so this is reached twice for one birth).
bool     ceremony_begin(uint8_t kind, uint32_t now_ms);

bool     ceremony_active(void);
uint8_t  ceremony_kind(void);

// Phase edges only: every register effect is armed EXACTLY once here. Doing it
// from the draw path would re-arm it on every frame and the shake would never
// decay. Call once per loop while active.
void     ceremony_service(uint32_t now_ms);

// One whole frame - no header, no affordance strip, no toast, no countdown.
// Chrome would turn a birth into a screen. Returns false when nothing is
// running, in which case the caller draws its own frame.
bool     ceremony_draw(uint32_t now_ms);

// Forget everything, including the "this event already played" window.
void     ceremony_reset(void);

#endif  // PB_CEREMONY_H
