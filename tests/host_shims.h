// =============================================================================
//  Errata host tests - host_shims.h
//  Fake clock and fake button levels for the host builds of input.cpp and
//  gametime.cpp. The sketch declares the hooks it needs
//  (nt_input_test_millis / nt_input_test_level / gt_host_millis32); the test
//  drives them through the setters below.
// =============================================================================
#ifndef NT_HOST_SHIMS_H
#define NT_HOST_SHIMS_H

#include <stdint.h>

// Fake millisecond clock shared by input.cpp and gametime.cpp on the host.
void     host_set_ms(uint32_t ms);
void     host_advance_ms(uint32_t ms);
uint32_t host_ms(void);

// Fake button levels. idx is INPUT_BTN_L / INPUT_BTN_R; pressed == true
// presents BTN_ACTIVE_LEVEL to the debouncer. Both start released.
void     host_set_button(int idx, bool pressed);

// Everything back to t = 0, both buttons released.
void     host_reset(void);

#endif  // NT_HOST_SHIMS_H
