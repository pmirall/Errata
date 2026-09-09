// =============================================================================
//  ERRATA - ui/screen_settings.h
//  SETTINGS: everything a two-button UI can honestly edit, plus the "Acerca
//  de" diagnostics page it flips to. Anything needing text entry (SSID,
//  password, nickname) belongs on the phone.
// =============================================================================
#ifndef ER_SCREEN_SETTINGS_H
#define ER_SCREEN_SETTINGS_H

#include <stdint.h>

#include "../core/nt_types.h"

enum SetRow : uint8_t {
  SET_SOUND = 0, SET_WEB, SET_BRIGHT,
  SET_QR, SET_CLOCK, SET_INFO, SET_MANUAL, SET_RESET, SET_BACK, SET_ROWS
};

void    settings_enter(void);
void    settings_render(void);
void    settings_input(Gesture g);
uint8_t settings_cursor(void);

// Which of the two pages is up: 0 = the list, 1 = "Acerca de". ui.cpp's global
// BACK grammar asks, because on the info page HOLD_R closes the page instead
// of leaving the screen.
uint8_t settings_page(void);
void    settings_close_page(void);

#endif  // ER_SCREEN_SETTINGS_H
