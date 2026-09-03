// =============================================================================
//  PEBBLEBOL - hardware/boot.h
//  How this power cycle started, and the RTC_NOINIT nonce that answers it.
//  Plan section 1.4 (module contracts) and section 1.7 (time model).
//
//  Split out of the v1 persistence/storage.cpp: classifying a boot is a
//  HARDWARE question (esp_reset_reason() plus a word of RTC fast memory) and
//  has nothing to do with key/value storage, which is why the two modules
//  parted company in P2-C9b.
//
//  THE NONCE. RtcKeep lives in RTC_NOINIT_ATTR memory: it survives a software
//  reset, a panic and a deep sleep, and is lost when power is actually removed.
//  That asymmetry is the whole crash-versus-abandonment discriminator, so the
//  nonce is read BEFORE it is re-armed and never earlier than boot_begin().
//
//  This module writes nothing to flash and knows nothing about saves. The one
//  fact it cannot compute - whether a usable save exists - is pushed in with
//  boot_note_save() once persistence has answered.
// =============================================================================
#ifndef PB_BOOT_H
#define PB_BOOT_H

#include <stdint.h>

#include "../core/nt_types.h"     // BootKind, RtcKeep

// Reads the RTC nonce, classifies the reset reason and re-arms the nonce for
// the next cycle. Call once, first thing in app_setup(), before persistence.
// Returns true when the nonce survived, i.e. this was NOT a power loss.
bool     boot_begin(void);

// Refines the verdict once the save pipeline has an answer: with nothing
// loadable there is nobody to have abandoned, so the kind becomes
// BOOT_FIRST_RUN. Passing true restores the hardware verdict.
void     boot_note_save(bool have_save);

BootKind boot_kind(void);
uint8_t  boot_reset_reason(void);      // raw esp_reset_reason_t, captured at begin
bool     boot_rtc_intact(void);        // the nonce survived => no power loss
uint32_t boot_rtc_last_seen(void);     // the RTC mirror as FOUND at boot, 0 if lost
uint32_t boot_count(void);

// Mirrors 'epoch' into RTC fast memory. Free (no flash), so the caller may do
// it every tick: it is what makes a crash reboot report an absence of ~0
// instead of up to one save period.
void     boot_touch_lastseen(uint32_t epoch);

void     boot_mark_god(void);          // god mode was entered this power cycle
bool     boot_god_tainted(void);

// Factory reset: forget the nonce's history and start a fresh one.
void     boot_rearm(void);

#endif // PB_BOOT_H
