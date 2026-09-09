// =============================================================================
//  ERRATA - hardware/boot_reason.h
//  THE RESET-REASON TABLE, LIFTED OUT OF hardware/boot.cpp AT P10-C6 SO THAT
//  SOMETHING CAN EXECUTE IT.
//
//  WHY THIS FILE EXISTS. §67's "Time-based calculations work across reboot" was
//  ticked on the arithmetic that DECIDES what a BootKind means -
//  nt_boot_charges_absence() is total over BootKind and swept value by value -
//  and nothing anywhere, host or bench, ever turned a real esp_reset_reason()
//  into a BootKind. hardware/boot.cpp is compiled by NO host binary (it includes
//  Arduino.h and esp_system.h), and tests/fakes/boot_host.cpp stands in for it
//  with `s_kind = have_save ? BOOT_SOFT_RESET : BOOT_FIRST_RUN` - TWO of the
//  seven kinds, and BOOT_SOFT_RESET is the one value in that set that answers
//  FALSE to nt_boot_charges_absence(). So every host boot that had a save was,
//  by construction, a boot that charges no absence, and the classifier that
//  feeds the whole absence path had never produced a value in any instrument.
//
//  hardware/boot.cpp's own header names the historical instance: "v1 folded
//  ESP_RST_DEEPSLEEP into BOOT_SOFT_RESET and lost it". A wrong entry in this
//  table silently stops a whole class of elapsed time being charged to the pet,
//  which is phase 6's defect - the shipping build advancing no game time for
//  four phases - arriving through a different door.
//
//  THE SPLIT. The table is here, pure, over a plain uint8_t, and swept over all
//  256 reason values by tests/test_clock.cpp. The MAPPING from this file's
//  BR_* values to ESP-IDF's esp_reset_reason_t is asserted at COMPILE TIME in
//  hardware/boot.cpp, where the real enum is in scope - so a toolchain that
//  renumbered them would fail the firmware build by name rather than silently
//  reclassifying every reboot. Neither half can drift without something saying
//  so, and neither half needs a board.
//
//  PURE. stdint and nt_types only: no Arduino, no esp_system. On the
//  Arduino-free red line in tools/check.sh.
// =============================================================================
#ifndef ER_HARDWARE_BOOT_REASON_H
#define ER_HARDWARE_BOOT_REASON_H

#include <stdint.h>

#include "../core/nt_types.h"   // BootKind

// ESP-IDF's esp_reset_reason_t, by value. hardware/boot.cpp static_asserts each
// one against the real symbol; these numbers are never used on the device.
enum BootReason : uint8_t {
  BR_UNKNOWN    = 0,
  BR_POWERON    = 1,
  BR_EXT        = 2,
  BR_SW         = 3,
  BR_PANIC      = 4,
  BR_INT_WDT    = 5,
  BR_TASK_WDT   = 6,
  BR_WDT        = 7,
  BR_DEEPSLEEP  = 8,
  BR_BROWNOUT   = 9,
  BR_SDIO       = 10,
  BR_USB        = 11,
  BR_JTAG       = 12,
  BR_EFUSE      = 13,
  BR_PWR_GLITCH = 14,
  BR_CPU_LOCKUP = 15
};

// 'rtc_intact' is the tie-breaker for every reason that does not itself prove a
// power cut: without the nonce we cannot tell a restart from a brownout that
// happened to report something odd, and BOOT_UNKNOWN is the honest answer.
//
// The four groups, and what each one means for elapsed game time:
//   POWER LOSS  - power really was removed; the nonce is meaningless even if it
//                 survived a very short brownout, so trust the reason. CHARGES.
//   CRASH       - the firmware died. NEVER an absence: the player was there.
//   DEEPSLEEP   - the pet was asleep, not abandoned, and the interval IS
//                 elapsed game time the absence path must be free to charge.
//   SOFT RESET  - deliberate restart, reset pin, USB/JTAG re-plug. ~0 s gap.
inline BootKind boot_classify(bool rtc_intact, uint8_t reason)
{
  switch (reason) {
    case BR_POWERON:
    case BR_BROWNOUT:
    case BR_PWR_GLITCH:
    case BR_EFUSE:
      return BOOT_POWER_LOSS;

    case BR_DEEPSLEEP:
      return rtc_intact ? BOOT_DEEPSLEEP : BOOT_UNKNOWN;

    case BR_PANIC:
    case BR_INT_WDT:
    case BR_TASK_WDT:
    case BR_WDT:
    case BR_CPU_LOCKUP:
      return rtc_intact ? BOOT_CRASH : BOOT_UNKNOWN;


    case BR_SW:
    case BR_EXT:
    case BR_SDIO:
    case BR_USB:
    case BR_JTAG:
      return rtc_intact ? BOOT_SOFT_RESET : BOOT_UNKNOWN;

    case BR_UNKNOWN:
    default:
      // An intact nonce proves the chip never lost power, so this cannot be an
      // abandonment however unhelpful the reason code is.
      return rtc_intact ? BOOT_SOFT_RESET : BOOT_UNKNOWN;
  }
}

#endif  // ER_HARDWARE_BOOT_REASON_H
