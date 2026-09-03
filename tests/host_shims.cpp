// =============================================================================
//  Pebblebol host tests - host_shims.cpp
//  Provides the host hooks the sketch declares when ARDUINO is not defined:
//    input.cpp    extern "C" uint32_t nt_input_test_millis(void);
//                 extern "C" int      nt_input_test_level(int idx);
//    gametime.cpp extern "C" uint32_t gt_host_millis32(void);
// =============================================================================
#include "host_shims.h"

#include "core/config.h"   // BTN_ACTIVE_LEVEL
#include "hardware/input.h"    // INPUT_BTN_N

static uint32_t s_ms = 0;
static bool     s_pressed[INPUT_BTN_N] = { false, false };

void     host_set_ms(uint32_t ms)     { s_ms = ms; }
void     host_advance_ms(uint32_t ms) { s_ms += ms; }
uint32_t host_ms(void)                { return s_ms; }

void host_set_button(int idx, bool pressed) {
  if (idx < 0 || idx >= INPUT_BTN_N) return;
  s_pressed[idx] = pressed;
}

void host_reset(void) {
  s_ms = 0;
  for (int i = 0; i < INPUT_BTN_N; i++) s_pressed[i] = false;
}

extern "C" uint32_t nt_input_test_millis(void) { return s_ms; }

extern "C" int nt_input_test_level(int idx) {
  const bool pressed = (idx >= 0 && idx < INPUT_BTN_N) ? s_pressed[idx] : false;
  // Pull-up wiring: the pin reads BTN_ACTIVE_LEVEL while pressed, its inverse
  // while released.
  return pressed ? BTN_ACTIVE_LEVEL : (BTN_ACTIVE_LEVEL ? 0 : 1);
}

extern "C" uint32_t gt_host_millis32(void) { return s_ms; }
