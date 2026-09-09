// =============================================================================
//  ERRATA - ui/screen_evolution.h
//  EVOLUTION (spec section 6 SCR_EVOLUTION), migrated by P2-C11c over the old
//  EGG screen.
//
//  The screen is a SHELL around ui/ceremony.cpp. While a ceremony is running -
//  a hatch today, an evolution from P3-C3 - the ceremony owns the entire frame
//  and every gesture; the row carries SF_OWNS_FRAME and SF_STICKY to say so.
//  While one is not, the screen is the egg incubator it has always been: the
//  shell, the "rub me" prompt and the ten alternating taps that bring a hatch
//  forward.
//
//  THE CEREMONY IS BOUND, NOT LINKED, exactly like HOME's body layer: it
//  drives the panel's flash and shake registers and draws through petfx, which
//  a pure screen may not touch. NULL on the host, where the incubator frame is
//  what the golden snapshot is a statement about.
// =============================================================================
#ifndef ER_SCREEN_EVOLUTION_H
#define ER_SCREEN_EVOLUTION_H

#include <stdint.h>

#include "../core/nt_types.h"

// Draw one ceremony frame. Returns false when none is running, in which case
// the screen draws the incubator instead.
typedef bool (*EvoCeremonyFn)(void);
void evo_bind_ceremony(EvoCeremonyFn fn);

// The screen-table hooks.
void evo_enter(void);
void evo_render(void);
void evo_input(Gesture g);

// How many alternating taps have landed inside the current window, 0..
// EGG_RUB_TAPS. Exposed for the tests and for the snapshot fixtures.
uint8_t evo_rub_count(void);

#endif  // ER_SCREEN_EVOLUTION_H
