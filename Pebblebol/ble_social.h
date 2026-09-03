// =============================================================================
//  NOTTAMAGOCHI - ble_social.h
//  Connectionless BLE discovery: advertisement-only identity beacon plus a
//  passive scan that keeps a table of the pets currently in the room.
//
//  NO BLEServer, NO BLEClient, NO GATT. Nothing here includes a BLE header:
//  ui/godmode can include this file without dragging Bluedroid into their TU.
//
//  This module is TRANSPORT ONLY. It carries no game rules: the peer table is
//  raw discovery data and every decision about what to do with a peer belongs
//  to the game layer (Phase 7 LINK, on whichever transport decision D2 picks).
// =============================================================================
#ifndef BLE_SOCIAL_H
#define BLE_SOCIAL_H

#include <stdint.h>
#include "config.h"
#include "nt_types.h"

// -----------------------------------------------------------------------------
// 1. TUNABLES THAT ARE NOT (YET) IN config.h
//    Every one is #ifndef-guarded so it can be lifted into config.h verbatim
//    without touching this file.
// -----------------------------------------------------------------------------

// The idle beacon advertises at 1-2 s (BLE_ADV_MIN/MAX_RAW) and the scanner
// runs at a 10 % duty cycle (100 ms window / 1000 ms interval), so a peer
// takes on the order of 15 s to appear. Discovery is patient by design; it is
// the only thing this module does.
#ifndef BLE_SCAN_WATCHDOG_MS
#define BLE_SCAN_WATCHDOG_MS      3000UL  // grace over the requested duration
#endif

// -----------------------------------------------------------------------------
// 2. WIRE FLAGS - BEACON frame flags byte
// -----------------------------------------------------------------------------
#define BLE_BF_DEBUG    0x01u   // b0: god-mode tainted, real units must refuse
#define BLE_BF_SEEKING  0x02u   // b1: the user is on the social screen

// -----------------------------------------------------------------------------
// 3. LOCAL STATE BITS - ble_set_self()
// -----------------------------------------------------------------------------
#define BLE_SELF_SEEKING    0x01u  // user is on SOCIAL: advertise as discoverable
#define BLE_SELF_GOD        0x04u  // PF_GOD_TAINTED mirror: sets BLE_BF_DEBUG

// -----------------------------------------------------------------------------
// 4. PUBLIC INTERFACE
// -----------------------------------------------------------------------------

// Arm the social layer on top of a Bluedroid stack that is ALREADY up.
// net.cpp owns BLEDevice::init/deinit, so the caller must have done
// net_request(RADIO_BLE) first - which also tears WiFi down (one radio stack
// resident at a time). Returns false if the stack is not up, if the controller
// never actually started, or past BLE_SESSION_CAP.
bool ble_begin(void);

// Stop advertising, stop scanning, drop the callback, clear the peer table.
// The stack itself (and the ~70 KB) is released by net_request(RADIO_OFF) ->
// BLEDevice::deinit(false). Call this BEFORE asking net for another mode.
void ble_end(void);

// Publish the identity beacon. Safe to call on every entry to the social
// screen; the radio is only re-keyed when something moved.
void ble_advertise_beacon(const Genome &g, uint8_t stage, uint8_t cq_hi);

// Pump. Call from loop() while BLE is up: drains the BTC-task queue, ages the
// peer table and restarts the scanner. Never call any BLE API from anywhere
// else.
void ble_scan_service(void);

// Live peers, most recently heard first. Indices are stable between two calls
// to ble_scan_service() and only between two calls to ble_scan_service().
uint8_t ble_peer_count(void);
const BlePeerInfo *ble_peer(uint8_t i);

// Feed the beacon the local state ble_advertise_beacon() does not carry.
// self_flags is a mask of BLE_SELF_*.
void ble_set_self(uint8_t self_flags);

// ble_begin() cycles used this boot. net_ble_sessions_used() is the
// authoritative counter; this one refuses at the same BLE_SESSION_CAP as a
// second line of defence. At the cap the UI must show STR_SO_CAP.
uint8_t ble_session_count(void);

// True between a successful ble_begin() and ble_end().
bool ble_is_up(void);

#endif // BLE_SOCIAL_H
