// =============================================================================
//  PEBBLEBOL - app/app.h
//  The two entry points the Arduino runtime calls. Pebblebol.ino is a three
//  line shim over them so that no game code lives in a .ino (arduino-cli runs
//  ctags over a .ino and injects prototypes, which a .cpp is free of).
// =============================================================================
#ifndef PB_APP_H
#define PB_APP_H

// Wires the modules in dependency order: display, entropy, persistence,
// settings, clock, pet, input, radio, UI. Called once.
void app_setup(void);

// Pumps input, the 1 Hz logic tick, presentation timing, the frame and the
// radio. Non-blocking; one iteration stays well below the 5 s Task WDT.
void app_loop(void);

#endif  // PB_APP_H
