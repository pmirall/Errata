// =============================================================================
//  Errata host tests - fakes/boot_host.cpp. See boot_host.h.
// =============================================================================
#include "boot_host.h"

static BootKind s_kind    = BOOT_FIRST_RUN;
static uint32_t s_mirror  = 0;
static uint32_t s_rtc     = 0;
static uint32_t s_boots   = 1;
static bool     s_tainted = false;

void boot_host_reset(void) {
  s_kind    = BOOT_FIRST_RUN;
  s_mirror  = 0;
  s_rtc     = 0;
  s_boots   = 1;
  s_tainted = false;
}

uint32_t boot_host_mirror(void) { return s_mirror; }
void     boot_host_set_rtc_last_seen(uint32_t epoch) { s_rtc = epoch; }

bool     boot_begin(void)         { return true; }
void     boot_note_save(bool have_save) { s_kind = have_save ? BOOT_SOFT_RESET : BOOT_FIRST_RUN; }
BootKind boot_kind(void)          { return s_kind; }
uint8_t  boot_reset_reason(void)  { return 0; }
bool     boot_rtc_intact(void)    { return true; }
uint32_t boot_rtc_last_seen(void) { return s_rtc; }
uint32_t boot_count(void)         { return s_boots; }
void     boot_touch_lastseen(uint32_t epoch) { if (epoch) s_mirror = epoch; }
void     boot_mark_god(void)      { s_tainted = true; }
bool     boot_god_tainted(void)   { return s_tainted; }
// FOUR EFFECTS, NOT TWO, SINCE THE FINAL REVIEW. The shipping boot_rearm()
// opens with nonce_new(), which MEMSETS THE WHOLE RtcKeep - so last_seen_epoch,
// boot_count, uptime_s AND god_taint all go to zero - and then sets
// s_kind = BOOT_FIRST_RUN. This fake imitated the two effects nothing observes
// (the counter and the epoch) and missed both effects that change a verdict.
// Measured against the real body compiled on the host: after mark_god() then
// rearm(), boot_god_tainted() was fake=1 / ship=0 and boot_kind() was
// fake=SOFT_RESET(3) / ship=FIRST_RUN(0).
//
// gs_factory_reset() is the sole caller and is reachable from the shipping UI
// and from bench item G1, so on the device a factory reset re-labels the
// session BOOT_FIRST_RUN and LAUNDERS THE GOD-MODE TAINT. Nothing asserted on
// either until the final review, which is exactly what made it the P10 shape
// one edit early: the first test anybody wrote about post-factory-reset boot
// state would have been written against the fake's answer, and been green and
// wrong. tests/test_game_state.cpp holds it now, and tests/fakes/SHADOWS.txt
// names that case as this row's anchor.
void     boot_rearm(void)         {
  s_boots   = 1;
  s_rtc     = 0;
  s_mirror  = 0;
  s_kind    = BOOT_FIRST_RUN;   // the session is a first run again
  s_tainted = false;            // ...and the RtcKeep memset took the taint
}
