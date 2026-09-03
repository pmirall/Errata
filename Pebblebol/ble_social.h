// =============================================================================
//  NOTTAMAGOCHI - ble_social.h
//  Connectionless BLE: advertisement-only beacon + passive scan, 3-frame
//  mating handshake (BEACON / MATE_OFFER / MATE_ACK) and proximity contagion.
//
//  NO BLEServer, NO BLEClient, NO GATT. Nothing here includes a BLE header:
//  ui/godmode can include this file without dragging Bluedroid into their TU.
//
//  BRIEF 1.3 (one radio stack resident), BRIEF 6.2 (frame layout),
//  BRIEF 7 risk #8 (shipped-library crash bugs), GAME_DESIGN 4.2 (mating).
// =============================================================================
#ifndef BLE_SOCIAL_H
#define BLE_SOCIAL_H

#include <stdint.h>
#include "config.h"
#include "nt_types.h"

// -----------------------------------------------------------------------------
// 1. TUNABLES THAT ARE NOT (YET) IN config.h
//    Every one is #ifndef-guarded so it can be lifted into config.h verbatim
//    without touching this file. See the module report.
// -----------------------------------------------------------------------------

// --- handshake timing --------------------------------------------------------
// The idle beacon advertises at 1-2 s (BLE_ADV_MIN/MAX_RAW) and the scanner
// runs at a 10 % duty cycle (100 ms window / 1000 ms interval). Expected time
// to catch one packet of a peer is therefore ~15 s, which CANNOT carry a
// handshake. While a handshake is live both sides switch to a fast profile
// (~150 ms advertising, 90 % scan duty) so a round trip lands in well under a
// second, then drop straight back to the battery-friendly idle profile.
#ifndef BLE_ADV_FAST_MIN_RAW
#define BLE_ADV_FAST_MIN_RAW     0x00A0   // RAW 0.625 ms units = 100 ms
#endif
#ifndef BLE_ADV_FAST_MAX_RAW
#define BLE_ADV_FAST_MAX_RAW     0x0140   // RAW 0.625 ms units = 200 ms
#endif
#ifndef BLE_SCAN_FAST_INTERVAL_MS
#define BLE_SCAN_FAST_INTERVAL_MS 200     // MILLISECONDS (BLEScan::setInterval)
#endif
#ifndef BLE_SCAN_FAST_WINDOW_MS
#define BLE_SCAN_FAST_WINDOW_MS   180     // MILLISECONDS (BLEScan::setWindow)
#endif
#ifndef BLE_OFFER_TIMEOUT_MS
#define BLE_OFFER_TIMEOUT_MS      12000UL // initiator waits this long for an ACK
#endif
#ifndef BLE_ACK_WINDOW_MS
#define BLE_ACK_WINDOW_MS         6000UL  // responder repeats the ACK this long
#endif
#ifndef BLE_RESULT_TTL_MS
#define BLE_RESULT_TTL_MS         30000UL // latched result expires if unread
#endif
#ifndef BLE_SCAN_WATCHDOG_MS
#define BLE_SCAN_WATCHDOG_MS      3000UL  // grace over the requested duration
#endif

// --- proximity quality -------------------------------------------------------
// A peer must be heard BLE_PEER_MIN_HITS times before it may be courted: one
// stray packet bouncing off a wall is not "the two of you are in the room".
#ifndef BLE_PEER_MIN_HITS
#define BLE_PEER_MIN_HITS         3
#endif
// Freshness required at the moment of a protocol decision.
#ifndef BLE_PEER_FRESH_MS
#define BLE_PEER_FRESH_MS         8000UL
#endif

// --- contagion by proximity --------------------------------------------------
// A sick pet radiates. -60 dBm is deliberately tighter than the -70 dBm peer
// gate: at 2.44 GHz with 0 dBm of TX power, free-space loss is ~40 dB at 1 m
// and ~54 dB at 5 m, so -60 dBm is "same table", not "same flat".
#ifndef BLE_CONTAGION_RSSI
#define BLE_CONTAGION_RSSI       (-60)
#endif
#ifndef BLE_CONTAGION_EXPOSURE_S
#define BLE_CONTAGION_EXPOSURE_S  120     // 2 min of continuous close contact
#endif
#ifndef BLE_CONTAGION_P_PCT
#define BLE_CONTAGION_P_PCT       25      // rolled once per completed exposure
#endif
#ifndef BLE_CONTAGION_REARM_S
#define BLE_CONTAGION_REARM_S     1800UL  // same peer cannot re-roll for 30 min
#endif

// -----------------------------------------------------------------------------
// 2. WIRE FLAGS - BEACON frame flags byte (BRIEF 6.2 defines b0 and b1)
// -----------------------------------------------------------------------------
#define BLE_BF_DEBUG    0x01u   // b0: god-mode tainted, real units must refuse
#define BLE_BF_SEEKING  0x02u   // b1: sitting on SOCIAL, open to courting
#define BLE_BF_SICK     0x04u   // b2: EXTENSION - contagion source (see report)

// -----------------------------------------------------------------------------
// 3. LOCAL STATE BITS - ble_set_self()
// -----------------------------------------------------------------------------
#define BLE_SELF_SEEKING    0x01u  // user is on SOCIAL: advertise + accept
#define BLE_SELF_SICK       0x02u  // PF_SICK mirror: we radiate, we cannot catch
#define BLE_SELF_GOD        0x04u  // PF_GOD_TAINTED mirror: sets BLE_BF_DEBUG
#define BLE_SELF_MATE_LOCK  0x08u  // caller-side lock (pending egg / persisted CD)

// -----------------------------------------------------------------------------
// 4. MATING STATE MACHINE
// -----------------------------------------------------------------------------
enum BleMateState : uint8_t {
  BLE_MATE_IDLE = 0,   // beaconing, nothing pending
  BLE_MATE_OFFERING,   // initiator: MATE_OFFER on air, waiting for MATE_ACK
  BLE_MATE_ACKING,     // responder: MATE_ACK on air, egg already latched
  BLE_MATE_OK,         // result latched, egg genome valid, waiting to be read
  BLE_MATE_FAIL,       // result latched, no egg, waiting to be read
  BLE_MATE_STATE_COUNT
};

#define BLE_ROLE_INITIATOR  0
#define BLE_ROLE_RESPONDER  1

// Latched outcome of one mating attempt. Consumed by ble_mate_take().
struct BleMateEvent {
  uint8_t  ok;            // 1 = child holds a valid egg genome
  uint8_t  role;          // BLE_ROLE_*
  uint8_t  peer_mac[6];   // 00:00:00:00:00:00 when the attempt never left home
  uint8_t  peer_stage;    // Stage of the partner, STAGE_COUNT when unknown
  Genome   child;         // valid only when ok == 1
  uint16_t str_id;        // StrId of the Spanish line to show
};

// -----------------------------------------------------------------------------
// 5. PUBLIC INTERFACE
//    The first seven are the BRIEF 4 contract. Everything after the marker is
//    an ADDITIVE extension - no existing signature changes - without which the
//    protocol has no way to be fed local state or to report a result.
// -----------------------------------------------------------------------------

// Arm the social layer on top of a Bluedroid stack that is ALREADY up.
// net.cpp owns BLEDevice::init/deinit (BRIEF 4 layering), so the caller must
// have done net_request(RADIO_BLE) first - which also tears WiFi down per
// BRIEF 1.3. Returns false if the stack is not up, if the controller never
// actually started, or past BLE_SESSION_CAP.
bool ble_begin(void);

// Stop advertising, stop scanning, drop the callback, clear the peer table.
// The stack itself (and the ~70 KB) is released by net_request(RADIO_OFF) ->
// BLEDevice::deinit(false). Call this BEFORE asking net for another mode.
void ble_end(void);

// Publish the identity beacon. Also registers who we are for the protocol, so
// it must be called at least once before any mating can happen. Safe to call
// on every entry to SOCIAL; the radio is only re-keyed when something moved.
void ble_advertise_beacon(const Genome &g, uint8_t stage, uint8_t cq_hi);

// Become the initiator: court the peer whose MAC ends in mac3 with this
// pre-bred child genome. Runs the sociability roll, and on a pass puts a
// MATE_OFFER on air and arms the ACK timeout. Every outcome - including an
// instant refusal - is reported through ble_mate_take().
void ble_advertise_offer(const Genome &child, const uint8_t mac3[3]);

// Pump. Call from loop() while BLE is up: drains the BTC-task queue, ages the
// peer table, accumulates contagion exposure, runs handshake timeouts and
// restarts the scanner. Never call any BLE API from anywhere else.
void ble_scan_service(void);

// Live peers, most recently heard first. Indices are stable between two calls
// to ble_scan_service() and only between two calls to ble_scan_service().
uint8_t ble_peer_count(void);
const BlePeerInfo *ble_peer(uint8_t i);

// ---- ADDITIVE EXTENSION (not in BRIEF 4; see the module report) -------------

// Feed the protocol the local state the three setters above do not carry.
// energy_pct is 0..100; self_flags is a mask of BLE_SELF_*.
void ble_set_self(uint8_t energy_pct, uint8_t self_flags);

// BleMateState.
uint8_t ble_mate_state(void);

// Consume a latched result. Returns false when there is nothing to report.
bool ble_mate_take(BleMateEvent &ev);

// Seconds left on the in-RAM mating cooldown, 0 when clear. This cooldown is
// per boot; the persistent 24 h rule is the caller's BLE_SELF_MATE_LOCK.
uint32_t ble_mate_cooldown_left_s(void);

// True exactly once per infection: a sick peer was close enough for long
// enough and the roll landed. The caller sets PF_SICK.
bool ble_take_contagion(void);

// ble_begin() cycles used this boot. net_ble_sessions_used() is the
// authoritative counter; this one refuses at the same BLE_SESSION_CAP as a
// second line of defence. At the cap the UI must show STR_SO_CAP.
uint8_t ble_session_count(void);

// True between a successful ble_begin() and ble_end().
bool ble_is_up(void);

#if GOD_MODE_ENABLED
// God mode command 10, "BLE FALSO": inject a synthetic peer straight into the
// receive queue, then let it answer the offer, so the whole mating flow can be
// exercised with a single unit on the bench.
bool ble_debug_inject_beacon(const Genome &g, uint8_t stage, uint8_t cq_hi, uint8_t peer_flags, int8_t rssi);
bool ble_debug_inject_ack(void);
#endif

#endif // BLE_SOCIAL_H
