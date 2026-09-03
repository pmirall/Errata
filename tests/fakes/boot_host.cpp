// =============================================================================
//  Pebblebol host tests - fakes/boot_host.cpp. See boot_host.h.
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
void     boot_rearm(void)         { s_boots = 1; s_rtc = 0; s_mirror = 0; }
