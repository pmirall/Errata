// =============================================================================
//  ERRATA - ui/screen_creator.h
//  CREATOR (spec section 6 SCR_CREATOR), migrated by P2-C11c over the old QR
//  screen.
//
//  WHAT IT IS NOW: the one screen that owns the Wi-Fi access point. It asks for
//  the radio on the way in and gives it back on the way out (plan section 2 row
//  G4, "radio OFF by default"), and while it is up it prints the two things a
//  phone needs to reach the device - the URL as a QR symbol and the 4-digit
//  PIN in numbers big enough to read at arm's length.
//
//  IT IS SF_STICKY SINCE P8-C2, AND THAT IS A BUG FIX RATHER THAN A PREFERENCE.
//  The screen had the default flags, so invariant 3's 20 s navigation
//  auto-return applied to it: entering CREATOR and walking to your phone sent
//  the device back to HOME after twenty seconds and tore the access point down
//  with it, which is less time than joining a network takes. The 20 s clock
//  counts device GESTURES, and the whole point of this screen is that the user
//  is looking at a phone instead. What replaces it is TWO exits the screen owns
//  itself, both in creator_update():
//    * the access point never came up within CREATOR_AP_WAIT_MS (spec 47:
//      every radio wait has an exit), and
//    * the portal went ConfigV2.creator_idle_s without an authorised request
//      (spec 34 and 40, decision D7).
//  Both go through ui_back(), so leaving on a timeout runs exactly the leave
//  hook a B press runs.
//
//  WHAT IT SAYS SINCE P8-C5 IS SPEC SECTION 34's OWN SCREEN, and the placeholder
//  line is gone: "SCAN ME / [QR CODE] / PIN: 1234 / Scan with your phone", laid
//  out down the 62 px column beside the symbol because the symbol is 62 px tall
//  on a 64-row panel. "Creador - Fase 8" said the page this screen points at did
//  not exist yet; it does, so the line is deleted rather than kept saying a
//  phase number. The section 34 instruction that comes with the layout - "do not
//  clutter this screen with unrelated UI" - is why the connection hint and the
//  always-on IP line went with it: the one line under the headline is whichever
//  of the two the symbol currently encodes.
//
//  PURE translation unit: gfx.h, qr.h's portable encoder half, the strings and
//  the two ui.h seams below. The QR modules are painted here with gfx_fill()
//  instead of qr_draw(), which is fenced behind #ifdef ARDUINO - that is what
//  lets the symbol be snapshot-tested at the real 128x64.
//
//  GEOMETRY (config.h QR_BOX_*): a 62 px symbol box at (0,1) with a 3-module
//  quiet zone, and a 62 px text column at x = 65. The box owns the rows the
//  affordance strip would use, so invariant 6's hint moves into that column -
//  the one documented exception, inherited from the QR screen.
// =============================================================================
#ifndef ER_SCREEN_CREATOR_H
#define ER_SCREEN_CREATOR_H

#include <stdint.h>

#include "../core/config.h"
#include "../core/nt_types.h"

// Everything the screen needs to know about the radio and the web server, in
// one fill. ui.cpp answers it; on the host tests/test_screens.cpp does.
#define CREATOR_TEXT_MAX  QR_TEXT_MAX
struct CreatorInfo {
  uint8_t  ap_up;                    // the device's own access point is serving
  // `sta_up` was here. It meant "joined the user's network", which this
  // firmware cannot do since P5-C1 - the station path is deleted, not disabled
  // - so the field and the screen branch that read it are gone rather than
  // permanently false.
  // Was `reserved`. The D7 grace period (spec section 34): the portal has gone
  // ConfigV2.creator_idle_s without an AUTHORISED request and the screen should
  // give the radio back. The DECISION is networking/creator_gate.cpp's, which a
  // host binary drives; this is only how it reaches a pure screen.
  uint8_t  idle_expired;
  uint16_t pin;                      // web_pin(): 1..9999, 0 = none issued yet
  char     ssid[24];                 // the AP's SSID, when ap_up
  char     ip[24];                   // whichever address is live
  char     url[CREATOR_TEXT_MAX];    // net_url(); NO PIN IN IT since P8-C1
};

// The screen-table hooks.
void creator_enter(void);
void creator_update(uint32_t now_ms);
void creator_render(void);
void creator_input(Gesture g);
void creator_leave(void);

// -----------------------------------------------------------------------------
//  THE POWER LADDER'S `held` INPUT (P10-C6) - AND THIS IS THE SAME BUG FIX THE
//  SF_STICKY NOTE ABOVE DESCRIBES, ONE RUNG HIGHER UP.
//
//  P8-C2 stopped invariant 3's 20 s navigation auto-return from tearing the
//  access point down while the owner was looking at a phone. It did not stop
//  hardware/power.cpp's ladder from doing exactly the same thing at
//  PWR_IDLE_MS: app/app.cpp feeds `held` from ui_radio_job_busy(), which was
//  network_screen_busy() || link_screen_busy() and knew nothing about the third
//  radio owner in the tree. So after 120 s with no BUTTON pressed - and a phone
//  is precisely what the player is pressing instead - pwr_hook_release() called
//  ui_home(), sm_goto() ran creator_leave(), and the portal died at 120 s while
//  CREATOR_IDLE_S_DEFAULT is 300 s and CREATOR_IDLE_S_MAX is 3,600 s. Drawing a
//  24x24 sprite on a phone takes longer than two minutes. Nothing said so:
//  no host binary compiles app/app.cpp, ui/ui.cpp or networking/net.cpp, and
//  tests/test_power.cpp had a case for the SCAN being held and none for this.
//
//  networking/discovery.h:254 static_asserts the same invariant for the link
//  job (LINK_JOB_TIMEOUT_MS < PWR_IDLE_MS) with the reason spelled out. The
//  portal cannot take that shape - its ceiling is a user setting up to an hour
//  - so it takes the other one link_hold() takes: it HOLDS the ladder, and its
//  own D7 idle timer in creator_update() is what ends it, which then runs
//  creator_leave() and drops the hold. One teardown, as before.
bool creator_screen_busy(void);

// Which symbol is on screen: 0 = the URL, 1 = "join this network". Exposed for
// the tests and for the snapshot names.
uint8_t creator_variant(void);
void    creator_set_variant(uint8_t v);

// The NUL-terminated payload the symbol currently on screen encodes, or "" when
// nothing has been encoded. THE POINT OF IT IS SPEC SECTION 39: a QR is
// photographed, forwarded and posted, so the PIN must not be inside one, and
// "the PIN is not in the payload" is a claim only a reader of the payload can
// check. tools/check.sh's grep watches net_url() in src/networking and cannot
// see ui/screen_creator.cpp, so without this seam a PIN appended in build()
// would pass the whole gate.
const char* creator_payload(void);

#endif  // ER_SCREEN_CREATOR_H
