// =============================================================================
//  ERRATA - ui/screen_diag.h
//  DIAG (spec section 6 SCR_DIAG), migrated by P2-C11c over the old GOD screen.
//
//  The developer console in dev/godmode.cpp owns the WHOLE frame and its own
//  gesture grammar, and it is not a pure module: it reads the simulation, the
//  radio and flash. So this row does not draw the console - it HOSTS it, the
//  same way HOME hosts the animated body layer: three function pointers bound
//  by ui.cpp on the device, all NULL on the host, where the screen draws the
//  "console off" frame that the golden snapshot is a statement about.
//
//  That is the whole point of the migration: it takes `case SCR_DIAG` out of
//  ui.cpp's three switches (the plan's grep exit gate) without pretending that
//  a console which can wipe the save is a pure translation unit.
//
//  SF_STICKY | SF_LOCK_INPUT | SF_OWNS_FRAME: no auto-return off a console
//  somebody is typing into, no global navigation grammar over its own, and no
//  toast or modal layer on top of a frame it composed itself.
// =============================================================================
#ifndef ER_SCREEN_DIAG_H
#define ER_SCREEN_DIAG_H

#include <stdint.h>

#include "../core/nt_types.h"

// Is the console still switched on? A console can turn ITSELF off (its own
// exit row), so the screen has to ask every pump rather than assume.
typedef bool (*DiagActiveFn)(void);
// Draw one whole frame. Never called when the console is off.
typedef void (*DiagDrawFn)(void);
// One gesture. Returns true when the console wants the SCREEN closed - which
// is not the same as the console being turned off: watching an accelerated
// life on the ordinary screens is the entire point of it.
typedef bool (*DiagInputFn)(Gesture g);

void diag_bind(DiagActiveFn active, DiagDrawFn draw, DiagInputFn input);

// The screen-table hooks.
void diag_update(uint32_t now_ms);
void diag_render(void);
void diag_input(Gesture g);

#endif  // ER_SCREEN_DIAG_H
