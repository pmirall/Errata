// =============================================================================
//  PEBBLEBOL - hardware/boot.cpp
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
// The reset-reason table. 'rtc_intact' is the tie-breaker for every reason that
// does not itself prove a power cut: without the nonce we cannot tell a restart
// from a brownout that happened to report something odd, and BOOT_UNKNOWN is
// the honest answer.
// -----------------------------------------------------------------------------
static BootKind classify(bool rtc_intact, uint8_t reason) {
  switch (reason) {
    // Power really was removed. RTC fast memory is meaningless here even if it
    // happens to look intact after a very short brownout: trust the reason.
    case ESP_RST_POWERON:
    case ESP_RST_BROWNOUT:
    case ESP_RST_PWR_GLITCH:
    case ESP_RST_EFUSE:
      return BOOT_POWER_LOSS;

    // The firmware died. Never report this as an absence.
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
    case ESP_RST_CPU_LOCKUP:
      return rtc_intact ? BOOT_CRASH : BOOT_UNKNOWN;

    // A timed wake. The pet was asleep, not abandoned, but time DID pass.
    case ESP_RST_DEEPSLEEP:
      return rtc_intact ? BOOT_DEEPSLEEP : BOOT_UNKNOWN;

    // Deliberate restart, reset pin, USB/JTAG re-plug, esp_restart().
    case ESP_RST_SW:
    case ESP_RST_EXT:
    case ESP_RST_SDIO:
    case ESP_RST_USB:
    case ESP_RST_JTAG:
      return rtc_intact ? BOOT_SOFT_RESET : BOOT_UNKNOWN;

    case ESP_RST_UNKNOWN:
    default:
      // An intact nonce proves the chip never lost power, so this cannot be an
      // abandonment however unhelpful the reason code is.
      return rtc_intact ? BOOT_SOFT_RESET : BOOT_UNKNOWN;
  }
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
