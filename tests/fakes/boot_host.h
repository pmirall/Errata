// =============================================================================
//  Pebblebol host tests - fakes/boot_host.h
//  hardware/boot.cpp is Arduino-only (esp_reset_reason, RTC_NOINIT memory), so
//  the host provides the same five entry points over plain RAM. Nothing here
//  classifies a boot: the classification table is device code and is exercised
//  on the device. This exists so persistence/save_compat.cpp - which mirrors
//  the last-seen epoch into RTC through boot.h - links on the host.
// =============================================================================
#ifndef PB_TEST_BOOT_HOST_H
#define PB_TEST_BOOT_HOST_H

#include "hardware/boot.h"

// Test hooks.
void     boot_host_reset(void);
uint32_t boot_host_mirror(void);            // the epoch last mirrored
void     boot_host_set_rtc_last_seen(uint32_t epoch);

#endif // PB_TEST_BOOT_HOST_H
