// =============================================================================
//  PEBBLEBOL - tests/fakes/arduino/Arduino.h
//  A FAKE Arduino.h, and it exists for exactly one file.
//
//  hardware/gametime.cpp's gt_mono_ms() has two bodies, and the DEVICE one -
//  the one that reads esp_rtc_get_time_us() rather than millis() - was
//  protected only by a grep in tools/check.sh. A grep can say that the right
//  identifier appears; it cannot say that the clock BEHAVES like the RTC after
//  a sleep, which is the whole reason gametime.cpp moved off uptime at P6-C3.
//
//  So tests/Makefile compiles that one translation unit with -DARDUINO and this
//  include directory in FRONT of the system ones, and tests/test_clock.cpp
//  drives a deep-sleep wake through it: uptime resets to 0, the RTC keeps
//  counting, and gt_mono32() must carry the gap.
//
//  IT IS DELIBERATELY THE SMALLEST POSSIBLE FILE. gametime.cpp's own header
//  comment names what it takes from Arduino.h - millis() - and nothing else in
//  this tree may be compiled against this directory, which tools/check.sh
//  enforces. A wider fake would start being a second Arduino core that can
//  drift from the real one without anyone noticing.
// =============================================================================
#ifndef PB_FAKE_ARDUINO_H
#define PB_FAKE_ARDUINO_H

#include <stdint.h>

// UPTIME, in milliseconds. On the C3 this is esp_timer, which
// esp_light_sleep_start() resynchronises from the RTC on the way out and which
// a DEEP sleep or a reset takes back to zero.
unsigned long millis(void);

// The knobs. Defined in tests/fakes/arduino_clocks.cpp; declared here so the
// one translation unit compiled against this header and the test that drives it
// see the same two clocks.
void     fake_uptime_set_ms(unsigned long ms);
void     fake_rtc_set_us(uint64_t us);
void     fake_clocks_advance_ms(unsigned long ms);   // both clocks together

#endif  // PB_FAKE_ARDUINO_H
