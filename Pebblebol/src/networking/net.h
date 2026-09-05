// =============================================================================
//  net.h - Pebblebol radio state machine.
//
//  THE ONLY MODULE ALLOWED TO TOUCH RADIO LIFECYCLE.
//  Nothing else in the firmware may call WiFi.mode(), the station association
//  entry point, WiFi.softAP(), WiFi.scanNetworks() or DNSServer::start().
//  Consumers ask for a RadioMode and poll the accessors.
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
//  THERE IS ONE RADIO STACK (P8-C0). The invariant used to be "exactly one is
//  resident" and it was a real constraint: Bluedroid and the Wi-Fi driver could
//  not both be up, so every transition between them cost a RADIO_SETTLE_MS
//  window in NPH_SETTLING. BLE is deleted, so what is left is:
//    RADIO_OFF  -> nothing resident. Simulation + OLED only.
//    RADIO_WIFI -> Wi-Fi up as a scanner, as the peer link, or as the AP.
//  A Wi-Fi to Wi-Fi transition never settled even when BLE was here, which is
//  why NPH_SETTLING and its timer went with it rather than being kept "just in
//  case": a phase nothing can enter is a phase nobody maintains.
//
//  This header deliberately pulls in NO network headers (no WiFi.h, no
//  WebServer.h) so ui/render/qr may include it without
//  breaking the layering rule. The IP is exposed as a dotted-quad string.
//
//  All user-facing text is Spanish and lives in strings_es.h; every
//  identifier and comment here is English (amendment A3).
// =============================================================================
#ifndef NT_NET_H
#define NT_NET_H

#include <stdint.h>
#include <stddef.h>

#include "../core/nt_types.h"     // -> config.h  (RadioMode, SSID_MAX_LEN)

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
  NPH_LINK,             // WIFI_STA on PB_LINK_CHANNEL with ESP-NOW up. NEVER
                        // associated either: ESP-NOW's whole "association" is
                        // its peer table and its channel (P7-C1, decision D2).
  NPH_COUNT
};

// -----------------------------------------------------------------------------
// Why the last net_request()/net_service() step failed. Never fatal.
// -----------------------------------------------------------------------------
enum NetErr : uint8_t {
  NERR_NONE = 0,
  NERR_BAD_ARG,           // mode out of range
  NERR_BUSY,              // reserved: nothing can refuse to go down any more
  NERR_WIFI_DISABLED,     // built with no WiFi consumer enabled
  NERR_SCAN_FAILED,       // the driver refused to start a scan
  NERR_AP_FAILED,         // softAP() returned false
  NERR_DNS_FAILED,        // captive DNSServer::start() returned false
  NERR_ESPNOW_DISABLED,   // built with FEATURE_ESPNOW == 0
  NERR_ESPNOW_FAILED,     // the ESP-NOW bring-up or its broadcast peer did not take
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

// Ask for the stack. RADIO_OFF tears Wi-Fi down (DNS stop, softAP down,
// WIFI_MODE_NULL). Returns false and sets net_last_err() when the request
// cannot be honoured (feature disabled, the driver refused, ...).
//
// NEVER BLOCKS. It no longer DEFERS either: the deferral existed to give the
// other stack RADIO_SETTLE_MS to itself, and there is no other stack (P8-C0).
// POLLING net_mode() REMAINS THE CONTRACT ANYWAY - a bring-up can still fail
// inside the driver, and a caller that assumes the call was enough is wrong for
// that reason rather than for the timer's.
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

// -----------------------------------------------------------------------------
// THE PEER LINK'S RADIO (P7-C1, decision D2 = ESP-NOW; spec sections 42, 43).
//
// net_link_driver() hands back the LinkRadioDriver that networking/discovery.h
// drives: bring WIFI_STA up on PB_LINK_CHANNEL with ESP-NOW resident, put one
// broadcast beacon on the air, drain what came back, and put the radio back to
// OFF. It is a THIRD INTENT ON THE SAME WI-FI STACK, exactly like the scan and
// the creator portal, which is why RadioMode does not gain a value.
//
// IT STILL NEVER ASSOCIATES. WiFi.mode(WIFI_STA) is the whole residency ESP-NOW
// needs - nothing in esp_now.h mentions credentials, an access point, an IP or
// a netif - so the P5-C1 deletion above is untouched and the gate that counts
// association call sites still reads zero.
// -----------------------------------------------------------------------------
struct LinkRadioDriver;
const LinkRadioDriver &net_link_driver(void);

// -----------------------------------------------------------------------------
// THE SESSION'S SIDE OF THE SAME LINK (P7-C2/C3).
//
// net_link_transport() is the networking/transport.h seam the section 15
// session runs over, unicast to whichever peer net_link_bind() opened. A
// REFERENCE and not a value because a device has one radio; the port object
// behind it is this module's, exactly as the scan driver's state is.
//
// THE SLOT IS AN OPAQUE INDEX AND NOT AN ADDRESS. It is a DiscPeer::slot handed
// out by the discovery job, and the six bytes it stands for never leave
// networking/transport_espnow.cpp (spec sections 43/44). A caller that has not
// bound gets a transport that refuses to send and never receives, which is
// exactly what "consent is not implied by proximity" needs from the radio.
// -----------------------------------------------------------------------------
struct Transport;
const Transport &net_link_transport(void);
bool        net_link_bind(uint8_t slot);
void        net_link_unbind(void);

// Compile-time sanity on the constants this module contracts against.
// Was 3 (OFF/WIFI/BLE) until P8-C0 deleted RADIO_BLE. BOTH ASSERTIONS ARE
// RESTATED RATHER THAN DELETED EVERY TIME THEIR SUBJECT MOVES, which is the
// third time for one and the fourth for the other: their job is to make a mode
// or a phase appearing or vanishing a DELIBERATE act, and that job outlives any
// particular count.
static_assert(RADIO_COUNT == 2, "RadioMode must stay OFF/WIFI");
// Was 7 before P5-C1 deleted the station path, 5 after it, 6 when P7-C1 added
// NPH_LINK - the peer link is a PHASE of the Wi-Fi stack, not a second
// RadioMode, which is why that change moved this line and not the one above -
// and 4 now that NPH_BLE_UP and NPH_SETTLING have gone with BLE.
static_assert(NPH_COUNT == 4, "NetPhase gained or lost a phase");
// Was `SSID_MAX_LEN == 32 && PASS_MAX_LEN == 64, "WiFi credential caps"`. There
// are no credentials any more, so that assertion lost its subject; what these
// two constants still size is this module's ACCESS POINT name buffer and the
// frozen Config padding (core/nt_types.h). Restated in those terms, not
// dropped, so the file still pins what it depends on.
static_assert(SSID_MAX_LEN >= 15, "s_ap_ssid must hold AP_SSID_PREFIX + 4 hex digits");

#endif  // NT_NET_H
