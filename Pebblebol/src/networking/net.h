// =============================================================================
//  net.h - Nottamagochi radio state machine.
//
//  THE ONLY MODULE ALLOWED TO TOUCH RADIO LIFECYCLE.
//  Nothing else in the firmware may call WiFi.mode(), the station association
//  entry point, WiFi.softAP(), WiFi.scanNetworks(), DNSServer::start(),
//  BLEDevice::init() or BLEDevice::deinit(). Consumers ask for a RadioMode and
//  poll the accessors.
//
//  THIS FIRMWARE NEVER JOINS A NETWORK (P5-C1, spec section 68 r5). The station
//  path - credentials, association, retry backoff, link-loss re-association -
//  was DELETED, not disabled: there is no call site left anywhere under src/,
//  and tools/check.sh counts them and fails the build at anything but zero.
//  That gate is a literal grep for the association call's name, which is why
//  the sentence above spells the rule out instead of naming the function: a
//  comment naming it would fail the gate that enforces it. What Wi-Fi is FOR is
//  the passive scan (networking/wifi_scanner.h) and the creator's own access
//  point, and neither one associates to anything.
//
//  WHAT THE GATE IS, EXACTLY (narrowed at the phase-5 exit): ONE grep for ONE
//  spelling of ONE call. It catches the regression that actually happens - the
//  line comes back - and it is not a proof that nothing can associate. Two ways
//  past it were tried on this tree: spaces around the dot compile and pass, and
//  the IDF entry point is not declared in this translation unit at all (no WiFi
//  library header pulls in esp_wifi.h), so reaching it needs a deliberate second
//  act - adding that include to a file no gate forbids it in. What makes the rule
//  STRUCTURAL is the deletion itself: there are no credentials to associate with,
//  no NPH_STA_* phase to enter and no retry state to re-enter it from. The gate
//  guards the deletion; it does not replace it.
//
//  INVARIANT (binding): exactly one radio stack is resident.
//    RADIO_OFF  -> no WiFi, no Bluedroid. Simulation + OLED only.
//    RADIO_WIFI -> WiFi up as a scanner or as the AP portal. Bluedroid down.
//    RADIO_BLE  -> Bluedroid up. WiFi driver deinitialised (WIFI_MODE_NULL).
//  Therefore net_is_ap_up() == true implies BLEDevice::getInitialized()
//  == false: a consumer that has the portal up needs no separate BLE check.
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
// net.h - ui, render, qr, webui, app.cpp - would otherwise drag in the whole
// 347-entry table. An opaque-enum declaration with a fixed underlying type is a
// complete type as far as a prototype is concerned; the translation unit that
// actually indexes ES[] includes strings_es.h itself.
enum StrId : uint16_t;

// -----------------------------------------------------------------------------
// Detailed phase inside the coarse RadioMode. For the debug/QR/social screens.
// -----------------------------------------------------------------------------
enum NetPhase : uint8_t {
  NPH_OFF = 0,          // RADIO_OFF, nothing resident
  NPH_SCANNING,         // WIFI_STA enabled for a passive scan. NEVER associated.
  NPH_AP_PORTAL,        // softAP + captive DNS up (the creator's own network)
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
  NERR_SCAN_FAILED,       // the driver refused to start a scan
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
// net_mode() rather than assume the call was enough.
// RADIO_OFF is the exception: it is always immediate and always succeeds.
//
// RADIO_WIFI brings up the AP PORTAL. The scan is a different intent on the
// same stack and has its own entry point below, so a screen cannot get one by
// asking for the other.
bool        net_request(RadioMode want);

// Pump. Call once per loop(). Never blocks: drives the settle timer, the
// association timeout, the retry backoff and the AP provisioning fallback.
void        net_service(void);

// Dotted-quad of the access point, "0.0.0.0" when down. Never NULL.
// NOT station-only and therefore NOT part of the P5-C1 deletion: the creator
// portal is what serves this address (net_ap_ssid(), webui.cpp).
const char *net_ip(void);

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

// -----------------------------------------------------------------------------
// THE SCANNER'S RADIO (P5-C1, spec sections 20, 40, 44).
//
// net_scan_driver() hands back the WifiScanDriver that networking/
// wifi_scanner.h drives: start a passive asynchronous scan, poll it, read the
// results as ScanResult rows, and put the radio back to OFF. It is the ONLY
// route from the game to WiFi.scanNetworks(), and the only place a beacon's
// name or hardware address is ever touched - both die inside the read loop,
// which hands out a salted hash and an abstract category (spec 44).
//
// net_scan_salt_set(device_id) must be called once, from the entry point, with
// gs_device_id(): the hash is salted per device so the same access point reads
// differently on two units and identically across reboots on one. Without it
// the salt is 0, which is a usable hash but not a private one.
// -----------------------------------------------------------------------------
struct WifiScanDriver;
const WifiScanDriver &net_scan_driver(void);
void        net_scan_salt_set(uint32_t device_id);

// Compile-time sanity on the constants this module contracts against.
static_assert(RADIO_COUNT == 3, "RadioMode must stay OFF/WIFI/BLE");
// Was 7. Three of them - the two station phases and the retry backoff - are
// gone with the station path (P5-C1); NPH_SCANNING is new. The assertion is
// RESTATED rather than deleted: its job is to make a phase appearing or
// vanishing a deliberate act, and that job survives its subject changing.
static_assert(NPH_COUNT == 5, "NetPhase gained or lost a phase");
static_assert(BLE_SESSION_CAP > 0 && BLE_SESSION_CAP <= 255, "BLE_SESSION_CAP must fit uint8_t");
// Was `SSID_MAX_LEN == 32 && PASS_MAX_LEN == 64, "WiFi credential caps"`. There
// are no credentials any more, so that assertion lost its subject; what these
// two constants still size is this module's ACCESS POINT name buffer and the
// frozen Config padding (core/nt_types.h). Restated in those terms, not
// dropped, so the file still pins what it depends on.
static_assert(SSID_MAX_LEN >= 15, "s_ap_ssid must hold AP_SSID_PREFIX + 4 hex digits");

#endif  // NT_NET_H
