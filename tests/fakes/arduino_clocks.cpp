// =============================================================================
//  PEBBLEBOL - tests/fakes/arduino_clocks.cpp
//  The two clocks tests/fakes/arduino/Arduino.h declares, as drivable
//  variables. See that header for why this fake exists at all.
//
//  THEY ARE SEPARATE ON PURPOSE. The whole point of the case they serve is that
//  a deep-sleep wake takes UPTIME back to zero while the RTC keeps counting, so
//  a test that could only move them together could not tell the two clocks
//  apart - which is exactly the mistake gametime.cpp's device branch exists to
//  avoid.
// =============================================================================
#include "arduino/Arduino.h"
#include "arduino/soc/esp32c3/rtc.h"

static unsigned long s_uptime_ms = 0;
static uint64_t      s_rtc_us    = 0;

unsigned long millis(void)             { return s_uptime_ms; }
uint64_t      esp_rtc_get_time_us(void){ return s_rtc_us; }

void fake_uptime_set_ms(unsigned long ms) { s_uptime_ms = ms; }
void fake_rtc_set_us(uint64_t us)         { s_rtc_us = us; }

void fake_clocks_advance_ms(unsigned long ms)
{
  s_uptime_ms += ms;
  s_rtc_us    += (uint64_t)ms * 1000ULL;
}
