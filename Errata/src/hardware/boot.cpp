// =============================================================================
//  ERRATA - hardware/boot.cpp
//  See boot.h. The RTC nonce and the reset-reason table, and nothing else.
//
//  Load-bearing details:
//   * the nonce is EVALUATED before it is re-armed, or every boot would look
//     like a soft reset;
//   * millis() is never consulted for game time here - only the uptime mirror,
//     which is a diagnostic;
//   * BOOT_DEEPSLEEP is its own verdict (P2-C9b). v1 folded ESP_RST_DEEPSLEEP
//     into BOOT_SOFT_RESET, which was wrong in both directions: a deep-sleep
//     wake is not a user-initiated restart, and the sleep interval IS elapsed
//     game time that the absence path must be free to charge, while a soft
//     reset is a ~0 s gap.
// =============================================================================
#include "boot.h"

#include <Arduino.h>
#include <esp_system.h>
#include <esp_attr.h>
#include <string.h>

#include "../core/config.h"        // RTC_NONCE_MAGIC
#include "../core/rng.h"
#include "boot_reason.h"   // P10-C6: the table, pure and host-swept

// Survives a software reset / panic / deep sleep, is lost on power loss.
// No initializer: RTC_NOINIT memory must not be written by the startup code.
static RTC_NOINIT_ATTR RtcKeep s_rtc;

static bool     s_begun       = false;
static bool     s_intact      = false;
static uint8_t  s_reason      = 0;      // esp_reset_reason_t
static uint32_t s_boot_seen   = 0;      // the RTC mirror as found at boot
static BootKind s_hw_kind     = BOOT_UNKNOWN;
static BootKind s_kind        = BOOT_UNKNOWN;

// -----------------------------------------------------------------------------
//  THE RESET-REASON TABLE MOVED TO hardware/boot_reason.h AT P10-C6, so that
//  something could execute it: this file is compiled by NO host binary, and the
//  fake that stands in for it collapsed seven BootKinds to two. See that header.
//
//  WHAT STAYS HERE IS THE MAPPING, ASSERTED AT COMPILE TIME. boot_reason.h
//  spells ESP-IDF's esp_reset_reason_t by value because the host cannot see the
//  real enum; these are the assertions that make that a fact rather than a copy
//  somebody hoped was right. A toolchain that renumbered them fails the FIRMWARE
//  build by name instead of silently reclassifying every reboot on the device.
// -----------------------------------------------------------------------------
static_assert((int)BR_UNKNOWN    == (int)ESP_RST_UNKNOWN,    "esp_reset_reason_t drifted from hardware/boot_reason.h: BR_UNKNOWN");
static_assert((int)BR_POWERON    == (int)ESP_RST_POWERON,    "esp_reset_reason_t drifted: BR_POWERON - a power cut would stop charging the absence");
static_assert((int)BR_EXT        == (int)ESP_RST_EXT,        "esp_reset_reason_t drifted: BR_EXT");
static_assert((int)BR_SW         == (int)ESP_RST_SW,         "esp_reset_reason_t drifted: BR_SW");
static_assert((int)BR_PANIC      == (int)ESP_RST_PANIC,      "esp_reset_reason_t drifted: BR_PANIC - a crash would be charged as an absence");
static_assert((int)BR_INT_WDT    == (int)ESP_RST_INT_WDT,    "esp_reset_reason_t drifted: BR_INT_WDT");
static_assert((int)BR_TASK_WDT   == (int)ESP_RST_TASK_WDT,   "esp_reset_reason_t drifted: BR_TASK_WDT");
static_assert((int)BR_WDT        == (int)ESP_RST_WDT,        "esp_reset_reason_t drifted: BR_WDT");
static_assert((int)BR_DEEPSLEEP  == (int)ESP_RST_DEEPSLEEP,  "esp_reset_reason_t drifted: BR_DEEPSLEEP - the v1 defect boot.cpp's own header records, exactly");
static_assert((int)BR_BROWNOUT   == (int)ESP_RST_BROWNOUT,   "esp_reset_reason_t drifted: BR_BROWNOUT");
static_assert((int)BR_SDIO       == (int)ESP_RST_SDIO,       "esp_reset_reason_t drifted: BR_SDIO");
static_assert((int)BR_USB        == (int)ESP_RST_USB,        "esp_reset_reason_t drifted: BR_USB");
static_assert((int)BR_JTAG       == (int)ESP_RST_JTAG,       "esp_reset_reason_t drifted: BR_JTAG");
static_assert((int)BR_EFUSE      == (int)ESP_RST_EFUSE,      "esp_reset_reason_t drifted: BR_EFUSE");
static_assert((int)BR_PWR_GLITCH == (int)ESP_RST_PWR_GLITCH, "esp_reset_reason_t drifted: BR_PWR_GLITCH");
static_assert((int)BR_CPU_LOCKUP == (int)ESP_RST_CPU_LOCKUP, "esp_reset_reason_t drifted: BR_CPU_LOCKUP");

static BootKind classify(bool rtc_intact, uint8_t reason) {
  return boot_classify(rtc_intact, reason);
}

static void nonce_new(void) {
  memset(&s_rtc, 0, sizeof(s_rtc));
  s_rtc.magic = (uint32_t)RTC_NONCE_MAGIC;
  do {
    s_rtc.nonce = rng_u32(RNG_MISC);
  } while (s_rtc.nonce == 0);
}

bool boot_begin(void) {
  if (s_begun) {
    return s_intact;
  }
  s_begun  = true;
  s_reason = (uint8_t)esp_reset_reason();

  // Read the previous power cycle BEFORE re-arming it.
  s_intact = (s_rtc.magic == (uint32_t)RTC_NONCE_MAGIC) && (s_rtc.nonce != 0);
  if (s_intact) {
    s_boot_seen = s_rtc.last_seen_epoch;
  } else {
    s_boot_seen = 0;
    nonce_new();
  }
  s_rtc.boot_count++;
  s_rtc.uptime_s = 0;
  s_rtc.reserved[0] = 0;
  s_rtc.reserved[1] = 0;

  s_hw_kind = classify(s_intact, s_reason);
  s_kind    = s_hw_kind;
  return s_intact;
}

void boot_note_save(bool have_save) {
  // No usable save beats everything: there is nobody to have abandoned.
  s_kind = have_save ? s_hw_kind : BOOT_FIRST_RUN;
}

BootKind boot_kind(void)          { return s_kind; }
uint8_t  boot_reset_reason(void)  { return s_reason; }
bool     boot_rtc_intact(void)    { return s_intact; }
uint32_t boot_rtc_last_seen(void) { return s_boot_seen; }
uint32_t boot_count(void)         { return s_rtc.boot_count; }

void boot_touch_lastseen(uint32_t epoch) {
  if (epoch == 0) {
    return;
  }
  s_rtc.last_seen_epoch = epoch;
  s_rtc.uptime_s = (uint32_t)(millis() / 1000UL);
}

void boot_mark_god(void) {
  s_rtc.god_taint = (uint32_t)RTC_NONCE_MAGIC;
}

bool boot_god_tainted(void) {
  return (s_rtc.magic == (uint32_t)RTC_NONCE_MAGIC) && (s_rtc.god_taint != 0);
}

void boot_rearm(void) {
  nonce_new();
  s_rtc.boot_count = 1;
  s_boot_seen = 0;
  s_kind      = BOOT_FIRST_RUN;
}
