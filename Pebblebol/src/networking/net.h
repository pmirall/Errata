// =============================================================================
//  net.h - Nottamagochi radio state machine.
//
//  THE ONLY MODULE ALLOWED TO TOUCH RADIO LIFECYCLE.
//  Nothing else in the firmware may call WiFi.mode(), WiFi.begin(),
//  WiFi.softAP(), DNSServer::start(), BLEDevice::init() or
//  BLEDevice::deinit(). Consumers ask for a RadioMode and poll the accessors.
//
//  INVARIANT (binding): exactly one radio stack is resident.
//    RADIO_OFF  -> no WiFi, no Bluedroid. Simulation + OLED only.
//    RADIO_WIFI -> WiFi STA (or the AP provisioning portal). Bluedroid down.
//    RADIO_BLE  -> Bluedroid up. WiFi driver deinitialised (WIFI_MODE_NULL).
//  Therefore net_is_sta_up() == true implies BLEDevice::getInitialized()
//  == false: a consumer that has the station up needs no separate BLE check.
//
//  This header deliberately pulls in NO network headers (no WiFi.h, no
//  BLEDevice.h, no WebServer.h) so ui/render/qr may include it without
//  breaking the layering rule. The IP is exposed as a dotted-quad string.
//
//  All user-facing text is Spanish and lives in strings_es.h; every
//  identifier and comment here is English (amendment A3).
// =============================================================================
#ifndef NT_NET_H
#define NT_NET_H

#include <stdint.h>
#include <stddef.h>

#include "../core/nt_types.h"     // -> config.h  (RadioMode, SSID_MAX_LEN, BLE_SESSION_CAP)

// NOT #include "strings_es.h". The only thing this header needs from the string
// table is the TYPE of net_last_err_str()'s return value, and every consumer of
// net.h - ui, render, qr, webui, the .ino - would otherwise drag in the whole
// 347-entry table. An opaque-enum declaration with a fixed underlying type is a
// complete type as far as a prototype is concerned; the translation unit that
// actually indexes ES[] includes strings_es.h itself.
enum StrId : uint16_t;

// -----------------------------------------------------------------------------
// Detailed phase inside the coarse RadioMode. For the debug/QR/social screens.
// -----------------------------------------------------------------------------
enum NetPhase : uint8_t {
  NPH_OFF = 0,          // RADIO_OFF, nothing resident
  NPH_STA_CONNECTING,   // WiFi.begin() issued, waiting for association
  NPH_STA_UP,           // WL_CONNECTED, IP valid
  NPH_STA_RETRY_WAIT,   // backoff between association attempts
  NPH_AP_PORTAL,        // softAP + captive DNS up (provisioning fallback)
  NPH_BLE_UP,           // Bluedroid initialised
  NPH_SETTLING,         // one stack torn down, waiting RADIO_SETTLE_MS for the other
  NPH_COUNT
};

// -----------------------------------------------------------------------------
// Why the last net_request()/net_service() step failed. Never fatal.
// -----------------------------------------------------------------------------
enum NetErr : uint8_t {
  NERR_NONE = 0,
  NERR_BAD_ARG,           // mode out of range
  NERR_BUSY,              // the other stack refused to go down
  NERR_WIFI_DISABLED,     // built with no WiFi consumer enabled
  NERR_BLE_DISABLED,      // built with FEATURE_BLE == 0
  NERR_BLE_SESSION_CAP,   // BLE_SESSION_CAP init/deinit cycles used this boot
  NERR_BLE_INIT_FAILED,   // BLEDevice::init() did not take
  NERR_NO_CREDENTIALS,    // no SSID -> went straight to the AP portal
  NERR_STA_TIMEOUT,       // association timed out WIFI_MAX_FAILS times
  NERR_AP_FAILED,         // softAP() returned false
  NERR_DNS_FAILED,        // captive DNSServer::start() returned false
  NERR_COUNT
};

// -----------------------------------------------------------------------------
// Heap census taken at every mode transition.
// Transient/telemetry: not persisted, not on the wire, size is not a contract
// with any other module - the static_assert below only pins the local layout.
// -----------------------------------------------------------------------------
struct NetHeapStats {
  uint32_t free_b;        // ESP.getFreeHeap()
  uint32_t min_free_b;    // ESP.getMinFreeHeap()   (low-water since boot)
  uint32_t max_alloc_b;   // ESP.getMaxAllocHeap()  (largest single block)
  uint32_t at_ms;         // millis() of the sample
  uint8_t  from_mode;     // RadioMode before the transition
  uint8_t  to_mode;       // RadioMode after the transition
  uint8_t  pad[2];
};
static_assert(sizeof(NetHeapStats) == 20, "NetHeapStats layout drifted");

// -----------------------------------------------------------------------------
// Core interface
// -----------------------------------------------------------------------------

// Idempotent. Radio stays OFF; caches the AP SSID and the compile-time
// credentials. Safe to call before Serial is enumerated.
void        net_begin(void);

// The stack that is resident right now.
RadioMode   net_mode(void);

// Ask for a stack. Tears the other one down first (BLE: stop advertising,
// stop scan, clearResults, deinit(false); WiFi: DNS stop, softAP
// down, WIFI_MODE_NULL). Returns false and sets net_last_err() when the
// request cannot be honoured (BLE session cap, feature disabled, ...).
//
// NEVER BLOCKS, so the bring-up may be DEFERRED. When the other stack had to
// be torn down first, the driver needs RADIO_SETTLE_MS to itself: the phase
// becomes NPH_SETTLING, the request returns true, and net_service() finishes
// the bring-up when the timer expires. A caller that needs the stack resident
// (SOCIAL before ble_begin(), god mode's GBS_RADIO) must therefore POLL
// net_mode() rather than assume the call was enough. RADIO_WIFI has always
// only *started* associating - poll net_is_sta_up() / net_phase().
// RADIO_OFF is the exception: it is always immediate and always succeeds.
bool        net_request(RadioMode want);

// Pump. Call once per loop(). Never blocks: drives the settle timer, the
// association timeout, the retry backoff and the AP provisioning fallback.
void        net_service(void);

// Dotted-quad of the active interface, "0.0.0.0" when down. Never NULL.
const char *net_ip(void);

// True only in STA mode with WL_CONNECTED and a routable IP.
bool        net_is_sta_up(void);

// -----------------------------------------------------------------------------
// Extended interface
// -----------------------------------------------------------------------------
NetPhase    net_phase(void);
bool        net_is_ap_up(void);          // provisioning portal is serving
NetErr      net_last_err(void);
StrId       net_last_err_str(void);      // Spanish line for the UI

// "NOTTAMAGOCHI-XXXX", derived from the STA MAC. Never NULL.
const char *net_ap_ssid(void);

// Builds "http://<ip>/?k=NNNN" for the QR screen.
// Returns the number of characters written, 0 if it did not fit or no IP.
size_t      net_url(char *out, size_t cap, uint16_t pin);

// Last heap census, and a way to force one (ble calls this around its
// allocations so the debug screen shows the real peak).
const NetHeapStats &net_heap_last(void);
void        net_heap_log(const char *tag);

// BLEDevice::deinit(false)/init() leaks ~672 B per cycle -> hard cap.
uint8_t     net_ble_sessions_used(void);
uint8_t     net_ble_sessions_left(void);

// Runtime credentials (from Config in NVS). Pass NULL/"" to clear. Applied on
// the next STA bring-up; if called while WiFi is up with a different SSID the
// station is restarted.
void        net_set_credentials(const char *ssid, const char *pass);
bool        net_has_credentials(void);

// 0 when not associated.
int8_t      net_rssi(void);

// Compile-time sanity on the constants this module contracts against.
static_assert(RADIO_COUNT == 3, "RadioMode must stay OFF/WIFI/BLE");
static_assert(NPH_COUNT == 7, "NetPhase gained or lost a phase");
static_assert(BLE_SESSION_CAP > 0 && BLE_SESSION_CAP <= 255, "BLE_SESSION_CAP must fit uint8_t");
static_assert(SSID_MAX_LEN == 32 && PASS_MAX_LEN == 64, "WiFi credential caps drifted");

#endif  // NT_NET_H
