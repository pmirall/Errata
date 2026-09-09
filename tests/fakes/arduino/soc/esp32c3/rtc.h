// =============================================================================
//  ERRATA - tests/fakes/arduino/soc/esp32c3/rtc.h
//  The one ESP-IDF symbol hardware/gametime.cpp's device branch reads.
//
//  esp_rtc_get_time_us() is THE RTC COUNTER, not the wall clock: settimeofday()
//  moves an offset applied on top of it and never the counter itself, which is
//  why gt_mono_ms() reads it and not millis(). The path is under the same
//  directory name the real header lives at, so the #include line in
//  gametime.cpp is the real one and not a test-only spelling.
// =============================================================================
#ifndef ER_FAKE_ESP32C3_RTC_H
#define ER_FAKE_ESP32C3_RTC_H

#include <stdint.h>

uint64_t esp_rtc_get_time_us(void);

#endif  // ER_FAKE_ESP32C3_RTC_H
