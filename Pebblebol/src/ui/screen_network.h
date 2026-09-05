// =============================================================================
//  PEBBLEBOL - ui/screen_network.h
//  THE NETWORK SCREEN (spec section 6 SCR_NETWORK, sections 20, 40, 47). P5-C3.
//
//  The screen that turns a passive Wi-Fi scan into an encounter: it holds the
//  WifiScanJob, drives it through networking/wifi_scanner.h, and on an answer
//  picks the first access point that is off cooldown, arms that cooldown and
//  rolls the encounter.
//
//  IT REPLACES soon_network(), the placeholder that has stood since P2-C11d.
//
//  B CANCELS, and that is not decoration: spec section 47 requires a
//  cancellation on every subsystem, and a scan holds the radio. The row carries
//  SF_OWNS_BACK so the router hands B here instead of turning it into a plain
//  BACK, and the leave hook cancels anything still running - so there is no
//  path off this screen that leaves the radio on.
//
//  WHAT IT COSTS IN GLOBALS, because that is the scarce resource this phase is
//  measured on: one WifiScanJob at 8 + 8 x WIFI_SCAN_MAX_RESULTS = 136 B, plus
//  a handful of bytes of screen state. The job is caller-owned by design
//  (networking/wifi_scanner.h) so exactly one exists and it is here.
//
//  PURE translation unit: gfx.h, the game headers and ui.h's seams. The radio,
//  the clock and the RNG are all reached through ui.h, which is what lets
//  tests/test_screens.cpp render this screen with no hardware.
// =============================================================================
#ifndef PB_SCREEN_NETWORK_H
#define PB_SCREEN_NETWORK_H

#include <stdint.h>

#include "../core/nt_types.h"

// The screen-table hooks.
void network_enter(void);
void network_update(uint32_t now_ms);
void network_render(void);
void network_input(Gesture g);
void network_leave(void);

// What the screen is doing, for tests and for the DIAG console.
enum NetScreenPhase : uint8_t {
  NSP_SCANNING = 0,   // the radio is up and the job is in flight
  NSP_EMPTY,          // the scan finished and nothing was explorable
  NSP_FAILED,         // the driver refused, the scan failed, or it timed out
  NSP_CANCELLED,      // B
  NSP_HANDOFF,        // an encounter was rolled and SCR_ENCOUNTER was pushed
  NSP_COUNT
};
uint8_t network_screen_phase(void);
uint8_t network_screen_seen(void);      // access points the last scan returned
uint8_t network_screen_fresh(void);     // ...of which this many were off cooldown

#endif  // PB_SCREEN_NETWORK_H
