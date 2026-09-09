// =============================================================================
//  Errata host tests - fakes/boot_host.h
//  hardware/boot.cpp is Arduino-only (esp_reset_reason, RTC_NOINIT memory), so
//  the host provides the WHOLE boot.h surface over plain RAM: ELEVEN functions,
//  not five. The caller that needs them is persistence/game_state.cpp
//  (gs_touch_lastseen, gs_factory_reset).
//
//  THIS BANNER WAS WRONG IN FOUR WAYS UNTIL THE FINAL REVIEW, and that matters
//  more than the arithmetic: this is the file a reader consults BEFORE trusting
//  the fake, and a banner claiming a five-function shim that classifies nothing
//  is exactly what lets somebody add a boot_kind() assertion without opening
//  boot_host.cpp. That is the P10-C6 root shape - not a wrong body caught by a
//  wrong comment, but a wrong comment that stops anyone reading the body.
//
//  WHAT IS ACTUALLY IMITATED, and it is two functions:
//    boot_touch_lastseen / boot_rtc_last_seen. These carry the non-obvious
//    invariant the device has: a mirror write is NOT visible to the getter
//    within the same power cycle, because the shipping getter returns the
//    snapshot boot_begin() took while the writer touches the live RtcKeep. Two
//    separate variables here reproduce that. app/app.cpp depends on it - it is
//    what makes a crash reboot report ~0 s of absence instead of up to one 60 s
//    save period.
//    boot_mark_god / boot_god_tainted / boot_rearm are imitated too, since the
//    final review: a rearm relabels the session BOOT_FIRST_RUN and LAUNDERS the
//    taint, because the shipping body memsets the whole RtcKeep.
//
//  WHAT IS NOT AN IMITATION, and a test that reads it as one is misreading it:
//    boot_begin()        always reports an intact RTC and a clean boot;
//    boot_rtc_intact()   always true - the device answers false on a cold power-on;
//    boot_reset_reason() always 0 - only a board makes a real one;
//    boot_kind()         collapses seven kinds to two, and the one it usually
//                        answers (BOOT_SOFT_RESET) is the value in that pair
//                        that reports FALSE to nt_boot_charges_absence();
//    boot_note_save()    sets that pair from have_save, where the device
//                        restores the HARDWARE verdict;
//    boot_count()        a test-set counter.
//  NO HOST TEST MAY CONCLUDE ANYTHING ABOUT A BOOT CLASSIFICATION FROM THESE.
//
//  WHERE THE REAL CLASSIFIER IS HELD INSTEAD: hardware/boot_reason.h, which is
//  deliberately host-linkable, swept over all 256 esp_reset_reason_t values by
//  tests/test_clock.cpp, and gated in tools/check.sh (boot.cpp must go through
//  boot_classify(), and must carry the 16 static_asserts against the real enum).
//  That is the shape to copy: lift the decision into a pure translation unit
//  BOTH sides call, rather than trying to make a fake tell the truth about
//  silicon.
//
//  Every row of this file is enumerated and classified in tests/fakes/SHADOWS.txt
//  and enforced by tools/check.sh section 10.
// =============================================================================
#ifndef ER_TEST_BOOT_HOST_H
#define ER_TEST_BOOT_HOST_H

#include "hardware/boot.h"

// Test hooks.
void     boot_host_reset(void);
uint32_t boot_host_mirror(void);            // the epoch last mirrored
void     boot_host_set_rtc_last_seen(uint32_t epoch);

#endif // ER_TEST_BOOT_HOST_H
