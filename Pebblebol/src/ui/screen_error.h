// =============================================================================
//  PEBBLEBOL - ui/screen_error.h
//  The ERROR state: the one screen that holds the device on a question the
//  user has to answer. Two families of question, and they are NOT
//  interchangeable (P2-C9c, plan T10):
//
//    ERRK_SAVE_CORRUPT  a save this firmware cannot READ. It may still be
//                       recoverable from the nvs2 checkpoint, so A offers it
//                       and B offers the factory reset behind a double confirm.
//    ERRK_SAVE_NEWER    a save from a NEWER firmware: perfectly good data that
//                       only a newer build can read. NOTHING here may write -
//                       offering a reset would be offering to destroy a working
//                       save because the device is out of date.
//    ERRK_DISPLAY       the panel did not answer on the I2C bus. This replaces
//                       rd_fatal()'s "blink forever and never return" (plan
//                       T10, audit risk 15): the device keeps running, the LED
//                       carries the same double-blink pattern, and A retries
//                       the bring-up so a reseated cable fixes it without a
//                       power cycle.
// =============================================================================
#ifndef NT_SCREEN_ERROR_H
#define NT_SCREEN_ERROR_H

#include <stdint.h>

#include "../core/nt_types.h"

enum ErrKind : uint8_t {
  ERRK_NONE = 0,
  ERRK_SAVE_CORRUPT,
  ERRK_SAVE_NEWER,
  ERRK_DISPLAY
};

// The screen-table hooks.
void err_enter(void);
void err_update(uint32_t now_ms);
void err_render(void);
void err_input(Gesture g);
void err_leave(void);

// What the screen is asking about. err_set_kind() only arms the text and the
// two actions; navigating to SCR_ERROR is the caller's job (ui_note_load() and
// ui_note_display_failure() do both).
void    err_set_kind(uint8_t kind);
uint8_t err_kind(void);

// Bound by the entry point: retry rd_begin() after an ERRK_DISPLAY, and drive
// PIN_LED. Both are hardware, so neither may live in this pure translation
// unit; a NULL binding simply means the action is unavailable.
typedef bool (*UiRetryFn)(void);
typedef void (*UiLedFn)(bool on);
void ui_bind_display_retry(UiRetryFn fn);
void ui_bind_led(UiLedFn fn);

// The panel did not come up. Arms ERRK_DISPLAY and shows the screen.
void ui_note_display_failure(void);

// The double-blink period of the ERRK_DISPLAY LED pattern, in ms: on 120, off
// 120, on 120, then dark for the rest. Inherited from rd_fatal() so a unit
// with a dead panel signals exactly what it always signalled.
#define ERR_BLINK_PERIOD_MS  2120u

#endif  // NT_SCREEN_ERROR_H
