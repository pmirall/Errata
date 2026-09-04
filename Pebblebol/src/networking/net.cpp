// =============================================================================
//  net.cpp - Nottamagochi radio state machine (BRIEF 1.3).
//
//  SCAN-ONLY WI-FI (P5-C1). This module has no station path: no credentials,
//  no association call, no retry backoff, no link-loss re-association. What
//  Wi-Fi does here is listen (NPH_SCANNING, a PASSIVE scan that sends nothing)
//  and serve the creator's own access point (NPH_AP_PORTAL). tools/check.sh
//  counts association call sites under src/ and fails the build at anything but
//  zero, so "the device never joins a network" is a fact about the tree rather
//  than a promise - which it was until P5-C1, held up by an empty SSID string.
//
//  Strict time-multiplex: exactly one radio stack resident at a time.
//  Both-resident was rejected on the measured heap arithmetic
//  (179,836 - 70,000 Bluedroid - 50,000 WiFi leaves too little for the HTTP
//  server's working set, below the fragmentation floor).
//
//  millis() note: the layering rule forbids reading millis() for GAME logic.
//  Association timeouts and retry backoff are wall-clock plumbing, not game
//  logic, and must not be affected by god-mode time skew - so this module uses
//  millis() directly and never calls gt_now().
// =============================================================================
#include "net.h"
#include "wifi_scanner.h"      // ScanResult, WifiScanDriver
#include "net_classify.h"      // NetFacts, the ladder, the salted hash

// net.h only forward-declares StrId (it must not pull the string table into
// every one of its consumers). This TU is one of the few that really indexes
// ES[], via net_last_err_str(), so it includes the table itself.
#include "../core/strings_es.h"

#include <Arduino.h>
#include <string.h>
#include <stdio.h>
#include <esp_mac.h>

// WiFi is only linked in when something actually consumes it.
#define NT_NET_WANT_WIFI (FEATURE_WEB)
// The captive provisioning portal needs an HTTP server to be worth starting.
#define NT_NET_HAVE_PORTAL (NT_NET_WANT_WIFI && FEATURE_WEB)

#if NT_NET_WANT_WIFI
#include <WiFi.h>
#include <DNSServer.h>
#endif

#if FEATURE_BLE
#include <BLEDevice.h>
#endif

// -----------------------------------------------------------------------------
// Logging. Serial is USB CDC (HWCDC) - never block on it, never gate boot on it.
// HWCDC::write drops silently when the host has not enumerated.
// -----------------------------------------------------------------------------
#define NET_LOGF(...) do { Serial.printf(__VA_ARGS__); } while (0)

// The NPH_SCANNING backstop (net_service()). STRICTLY LATER than the scan job's
// own WIFI_SCAN_TIMEOUT_MS so the two clocks can never disagree about what
// happened: the job always answers first, and this only catches a job nobody is
// pumping. One settle window plus a second of slack is the margin.
#define NET_SCAN_BACKSTOP_MS ((uint32_t)WIFI_SCAN_TIMEOUT_MS + RADIO_SETTLE_MS + 1000UL)

// -----------------------------------------------------------------------------
// Module state
// -----------------------------------------------------------------------------
static bool         s_begun          = false;
static RadioMode    s_mode           = RADIO_OFF;
static NetPhase     s_phase          = NPH_OFF;
static NetErr       s_err            = NERR_NONE;

static uint32_t     s_phase_ms       = 0;    // millis() when the phase was entered
static RadioMode    s_pending        = RADIO_OFF;  // NPH_SETTLING target
static uint8_t      s_ble_sessions   = 0;    // BLEDevice::init() calls this boot
static bool         s_ap_up          = false;
static bool         s_want_scan      = false;  // the next WiFi bring-up is a scan
static uint32_t     s_scan_salt      = 0;    // net_scan_salt_set(gs_device_id())

#if NT_NET_WANT_WIFI
static bool         s_dns_up         = false;
#endif

static char         s_ip[16]         = "0.0.0.0";
static char         s_ap_ssid[SSID_MAX_LEN + 1];
static char         s_ble_name[16];

static NetHeapStats s_heap;

#if NT_NET_HAVE_PORTAL
// DNSServer is non-copyable (DNSServer.h:88-89) - hold it as a file-scope
// object, never by value in a container.
static DNSServer    s_dns;
#endif

// -----------------------------------------------------------------------------
// Small helpers
// -----------------------------------------------------------------------------
static void copy_bounded(char *dst, size_t cap, const char *src) {
  if (!dst || cap == 0) {
    return;
  }
  if (!src) {
    dst[0] = '\0';
    return;
  }
  size_t n = 0;
  while (src[n] != '\0' && n < cap - 1) {
    dst[n] = src[n];
    ++n;
  }
  dst[n] = '\0';
}

#if NT_NET_WANT_WIFI
static void set_ip_str(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  snprintf(s_ip, sizeof(s_ip), "%u.%u.%u.%u",
           (unsigned)a, (unsigned)b, (unsigned)c, (unsigned)d);
}
#endif

static void clear_ip(void) {
  copy_bounded(s_ip, sizeof(s_ip), "0.0.0.0");
}

void net_heap_log(const char *tag) {
  s_heap.free_b      = ESP.getFreeHeap();
  s_heap.min_free_b  = ESP.getMinFreeHeap();
  s_heap.max_alloc_b = ESP.getMaxAllocHeap();
  s_heap.at_ms       = millis();
  NET_LOGF("[net] %s mode=%u phase=%u free=%lu min=%lu maxalloc=%lu\r\n",
           tag ? tag : "-",
           (unsigned)s_mode, (unsigned)s_phase,
           (unsigned long)s_heap.free_b,
           (unsigned long)s_heap.min_free_b,
           (unsigned long)s_heap.max_alloc_b);
}

static void set_phase(NetPhase p) {
  s_phase    = p;
  s_phase_ms = millis();
}

// Every RadioMode transition logs a heap census (BRIEF open question 5).
static void set_mode(RadioMode m, NetPhase p, const char *tag) {
  s_heap.from_mode = (uint8_t)s_mode;
  s_heap.to_mode   = (uint8_t)m;
  s_mode           = m;
  set_phase(p);
  net_heap_log(tag);
}

// A radio stack was just torn down: give the driver / controller a moment
// before bringing the other one up. That moment used to be delay(250), a
// quarter of a second in which no frame was drawn, no button was drained and
// the 1 Hz tick slipped. It is now a PHASE: park in NPH_SETTLING with the
// requested target remembered, return to the caller, and let net_service()
// finish the bring-up once RADIO_SETTLE_MS have passed.
// Only a build that can host BOTH stacks ever has something to settle between;
// with NT_NET_WANT_WIFI off there is nothing to tear down before BLE comes up.
#if NT_NET_WANT_WIFI
static void begin_settle(RadioMode target) {
  s_pending = target;
  set_mode(RADIO_OFF, NPH_SETTLING, "-> SETTLING");
}
#endif

// -----------------------------------------------------------------------------
// Identity: AP SSID and BLE device name, derived from the station MAC.
// esp_read_mac() works before WiFi or Bluedroid have been started.
// -----------------------------------------------------------------------------
static void build_identity(void) {
  uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
    uint64_t efuse = ESP.getEfuseMac();
    for (int i = 0; i < 6; ++i) {
      mac[i] = (uint8_t)((efuse >> (8 * i)) & 0xFF);
    }
  }
  snprintf(s_ap_ssid, sizeof(s_ap_ssid), "%s%02X%02X",
           AP_SSID_PREFIX, (unsigned)mac[4], (unsigned)mac[5]);
  snprintf(s_ble_name, sizeof(s_ble_name), "NT-%02X%02X",
           (unsigned)mac[4], (unsigned)mac[5]);
}

// =============================================================================
//  BLE lifecycle - the ONLY place BLEDevice::init/deinit are called.
// =============================================================================
static void ble_down(void) {
#if FEATURE_BLE
  if (!BLEDevice::getInitialized()) {
    return;
  }
  // Order matters: quiesce the radio activity, drop the result map, then
  // deinit. BLEDevice::deinit(true) is FORBIDDEN - BLEDevice.cpp:622-641 only
  // clears `initialized` in the else branch, so a later init() silently
  // no-ops at the if (!initialized) guard. One-way trip.
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  if (adv) {
    adv->stop();
  }
  BLEScan *scan = BLEDevice::getScan();
  if (scan) {
    scan->stop();
    scan->clearResults();
  }
  BLEDevice::deinit(false);
  NET_LOGF("[net] BLE down (session %u/%u)\r\n",
           (unsigned)s_ble_sessions, (unsigned)BLE_SESSION_CAP);
#endif
}

static inline bool ble_up(void) {
#if FEATURE_BLE
  if (BLEDevice::getInitialized()) {
    return true;
  }
  // deinit(false)/init() cycling leaks ~672 B per cycle (scan map, m_pScan,
  // m_pClient, m_bleAdvertising are never freed). Fail deterministically
  // instead of dying of a mystery OOM 200 cycles later.
  if (s_ble_sessions >= BLE_SESSION_CAP) {
    s_err = NERR_BLE_SESSION_CAP;
    NET_LOGF("[net] BLE refused: session cap %u reached\r\n",
             (unsigned)BLE_SESSION_CAP);
    return false;
  }
  BLEDevice::init(String(s_ble_name));
  if (!BLEDevice::getInitialized()) {
    s_err = NERR_BLE_INIT_FAILED;
    NET_LOGF("[net] BLEDevice::init() failed\r\n");
    return false;
  }
  ++s_ble_sessions;
  return true;
#else
  s_err = NERR_BLE_DISABLED;
  return false;
#endif
}

static inline bool ble_resident(void) {
#if FEATURE_BLE
  return BLEDevice::getInitialized();
#else
  return false;
#endif
}

// =============================================================================
//  WiFi lifecycle - the ONLY place WiFi.mode() is called.
// =============================================================================
#if NT_NET_WANT_WIFI

static void ap_down(void) {
#if NT_NET_HAVE_PORTAL
  if (s_dns_up) {
    s_dns.stop();
    s_dns_up = false;
  }
#endif
  if (s_ap_up) {
    WiFi.softAPdisconnect(false);
    s_ap_up = false;
  }
}

static void wifi_down(void) {
  ap_down();
  // FREE THE SCAN ARRAY HERE, not only where a scan is read. WiFiScan::
  // _scanDone() heap-allocates one wifi_ap_record_t per access point and only
  // scanDelete() or the NEXT scan releases it - so any route to RADIO_OFF out
  // from under a running or finished scan (leaving the screen, god mode, a
  // low-power state) leaked the array. This is the one place every teardown
  // passes through, which is why it belongs here.
  WiFi.scanDelete();
  wifi_mode_t m = WiFi.getMode();
  if (m & WIFI_MODE_STA) {
    // (wifioff, eraseap, timeout_ms) - three parameters in 3.1.1. The scan
    // enables STA (a pure mode change) and never associates, so this is a
    // driver-state tidy-up rather than a disconnection.
    WiFi.disconnect(false, false, 100);
  }
  if (m != WIFI_MODE_NULL) {
    // This is the call that genuinely returns the ~50 KB:
    // esp_wifi_stop() + esp_wifi_deinit() + esp_netif_destroy_default_wifi().
    WiFi.mode(WIFI_MODE_NULL);
  }
  clear_ip();
}

// =============================================================================
//  THE PASSIVE SCAN (P5-C1, spec sections 20, 40, 44)
//
//  WiFiScan::scanNetworks() calls WiFi.enableSTA(true) and NOTHING else that
//  touches association - verified against the installed core (esp32 3.1.1,
//  WiFiScan.cpp:75): enableSTA is a mode change, esp_wifi_connect() is never
//  reached. net_begin()'s WiFi.persistent(false) also puts the IDF in
//  WIFI_STORAGE_RAM, so it holds no saved credentials to try on its own.
// =============================================================================
static bool scan_begin(void) {
  WiFi.persistent(false);
  WiFi.setSleep(true);                    // modem sleep; we are battery bound
  // async, show_hidden, PASSIVE, ms per channel. Passive is the privacy half of
  // spec section 44: the device listens for beacons and never sends a probe
  // request, so it broadcasts nothing about itself while it explores.
  // show_hidden is on because a beacon with no name is a CATEGORY (HIDDEN),
  // not noise.
  const int16_t r = WiFi.scanNetworks(true, true, true,
                                      (uint32_t)WIFI_SCAN_DWELL_MS);
  if (r != WIFI_SCAN_RUNNING) {
    s_err = NERR_SCAN_FAILED;
    NET_LOGF("[net] scan start FAILED (%d)\r\n", (int)r);
    WiFi.scanDelete();
    WiFi.mode(WIFI_MODE_NULL);
    clear_ip();
    set_mode(RADIO_OFF, NPH_OFF, "SCAN FAIL");
    return false;
  }
  set_mode(RADIO_WIFI, NPH_SCANNING, "-> SCAN");
  return true;
}

#if NT_NET_HAVE_PORTAL
static bool ap_start(void) {
  if (WiFi.getMode() & WIFI_MODE_STA) {
    WiFi.disconnect(false, false, 100);
  }
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  // nullptr, NOT "" - an empty String passphrase with the default
  // WIFI_AUTH_WPA2_PSK fails and the AP never appears.
  if (!WiFi.softAP(s_ap_ssid, (const char *)NULL)) {
    s_err   = NERR_AP_FAILED;
    s_ap_up = false;
    NET_LOGF("[net] softAP(\"%s\") FAILED\r\n", s_ap_ssid);
    WiFi.mode(WIFI_MODE_NULL);
    clear_ip();
    set_mode(RADIO_OFF, NPH_OFF, "AP FAIL");
    return false;
  }
  s_ap_up = true;
  if (!WiFi.softAPConfig(IPAddress(AP_IP_A, AP_IP_B, AP_IP_C, AP_IP_D),
                         IPAddress(AP_IP_A, AP_IP_B, AP_IP_C, AP_IP_D),
                         IPAddress(255, 255, 255, 0))) {
    NET_LOGF("[net] softAPConfig failed, keeping the driver default IP\r\n");
  }
  {
    IPAddress ip = WiFi.softAPIP();
    set_ip_str(ip[0], ip[1], ip[2], ip[3]);
  }
  // DNSServer::start() returns false when WIFI_AP is not already up or
  // softAPIP() is 0.0.0.0 (DNSServer.cpp:20-39) - start it AFTER the AP and
  // CHECK the return. Never call processNextRequest(): it is an empty stub in
  // 3.x (DNSServer.h:96), the responder is AsyncUDP driven.
  s_dns.setTTL(60);
  s_dns_up = s_dns.start();
  if (!s_dns_up) {
    s_err = NERR_DNS_FAILED;
    NET_LOGF("[net] captive DNS start FAILED (AP still reachable at %s)\r\n", s_ip);
  }
  set_mode(RADIO_WIFI, NPH_AP_PORTAL, "AP PORTAL");
  NET_LOGF("[net] AP \"%s\" at %s, dns=%u\r\n",
           s_ap_ssid, s_ip, (unsigned)s_dns_up);
  return true;
}
#endif  // NT_NET_HAVE_PORTAL

#endif  // NT_NET_WANT_WIFI

// =============================================================================
//  Public API
// =============================================================================
void net_begin(void) {
  if (s_begun) {
    return;
  }
  s_begun = true;
  memset(&s_heap, 0, sizeof(s_heap));
  build_identity();
  clear_ip();
#if NT_NET_WANT_WIFI
  WiFi.persistent(false);
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.mode(WIFI_MODE_NULL);
  }
#endif
  s_mode  = RADIO_OFF;
  s_phase = NPH_OFF;
  s_err   = NERR_NONE;
  net_heap_log("BEGIN");
  NET_LOGF("[net] ap=\"%s\" ble=\"%s\" wifi=%u ble_feature=%u\r\n",
           s_ap_ssid, s_ble_name,
           (unsigned)(NT_NET_WANT_WIFI), (unsigned)(FEATURE_BLE));
}

RadioMode net_mode(void) {
  return s_mode;
}

NetPhase net_phase(void) {
  return s_phase;
}

NetErr net_last_err(void) {
  return s_err;
}

// Second half of a deferred bring-up: everything net_request() would have done
// after settle(). Called either straight from net_request() (nothing had to be
// torn down) or from net_service() when the settle timer expires.
static bool bring_up(RadioMode want) {
  s_pending = RADIO_OFF;

  if (want == RADIO_BLE) {
#if !FEATURE_BLE
    s_err = NERR_BLE_DISABLED;
    set_mode(RADIO_OFF, NPH_OFF, "-> OFF (no ble)");
    return false;
#else
    if (!ble_up()) {
      set_mode(RADIO_OFF, NPH_OFF, "-> OFF (ble fail)");
      return false;
    }
    set_mode(RADIO_BLE, NPH_BLE_UP, "-> BLE");
    return true;
#endif
  }

#if !NT_NET_WANT_WIFI
  s_err = NERR_WIFI_DISABLED;
  set_mode(RADIO_OFF, NPH_OFF, "-> OFF (no wifi)");
  return false;
#else
  // TWO INTENTS ON ONE STACK, and the caller does not get to confuse them: the
  // scanner sets s_want_scan through net_scan_driver()'s start(), everything
  // else lands on the creator's access point. There is no third branch, because
  // there is no station.
  if (s_want_scan) {
    return scan_begin();
  }
#if NT_NET_HAVE_PORTAL
  return ap_start();
#else
  s_err = NERR_WIFI_DISABLED;
  NET_LOGF("[net] no portal available in this build\r\n");
  set_mode(RADIO_OFF, NPH_OFF, "-> OFF (no portal)");
  return false;
#endif
#endif
}

bool net_request(RadioMode want) {
  if (!s_begun) {
    net_begin();
  }
  if ((uint8_t)want >= (uint8_t)RADIO_COUNT) {
    s_err = NERR_BAD_ARG;
    return false;
  }
  s_err = NERR_NONE;

  // ---------------------------------------------------------------- OFF ----
  if (want == RADIO_OFF) {
    ble_down();
#if NT_NET_WANT_WIFI
    wifi_down();
#endif
    s_want_scan = false;            // a scan intent does not survive a power-down
    s_pending = RADIO_OFF;          // cancels a settle that was in flight
    if (s_mode != RADIO_OFF || s_phase != NPH_OFF) {
      set_mode(RADIO_OFF, NPH_OFF, "-> OFF");
    }
    return true;
  }

  // A settle window is already running: the stack that was resident is down
  // and NOTHING may come up until it expires (BRIEF 1.3 - that is the whole
  // point of the window). Retarget it and keep waiting; net_service() brings
  // up whichever stack was asked for last. Falling through to the branches
  // below would test "did I have to tear anything down?", find the answer is
  // no - because the teardown already happened - and bring the new stack up
  // right on top of it.
  if (s_phase == NPH_SETTLING) {
    // The session cap is checked before anything is torn down everywhere else;
    // here nothing is torn down at all, so check it before accepting a target
    // that ble_up() would only refuse RADIO_SETTLE_MS later.
    if (want == RADIO_BLE && !ble_resident() && s_ble_sessions >= BLE_SESSION_CAP) {
      s_err = NERR_BLE_SESSION_CAP;
      return false;
    }
    s_pending = want;
    return true;                    // finished by net_service()
  }

  // ---------------------------------------------------------------- BLE ----
  if (want == RADIO_BLE) {
#if !FEATURE_BLE
    s_err = NERR_BLE_DISABLED;
    return false;
#else
    if (s_mode == RADIO_BLE && ble_resident()) {
      return true;
    }
    // Refuse before tearing anything down, so a capped request is a no-op.
    if (!ble_resident() && s_ble_sessions >= BLE_SESSION_CAP) {
      s_err = NERR_BLE_SESSION_CAP;
      return false;
    }
#if NT_NET_WANT_WIFI
    const bool tore_down = (WiFi.getMode() != WIFI_MODE_NULL);
    wifi_down();
    if (tore_down) {
      begin_settle(RADIO_BLE);
      return true;                  // finished by net_service()
    }
#endif
    return bring_up(RADIO_BLE);
#endif
  }

  // --------------------------------------------------------------- WIFI ----
#if !NT_NET_WANT_WIFI
  s_err = NERR_WIFI_DISABLED;
  return false;
#else
  {
    const bool tore_down = ble_resident();
    ble_down();
    // Binding precondition (BRIEF 1.3): Bluedroid must be gone before WiFi.
    if (ble_resident()) {
      s_err = NERR_BUSY;
      NET_LOGF("[net] refusing WIFI: Bluedroid still initialised\r\n");
      return false;
    }
    if (tore_down) {
      begin_settle(RADIO_WIFI);
      return true;                  // finished by net_service()
    }
  }
  if (s_mode == RADIO_WIFI && s_phase != NPH_OFF) {
    return true;   // already on the WiFi track (scanning or portal)
  }
  return bring_up(RADIO_WIFI);
#endif  // NT_NET_WANT_WIFI
}

void net_service(void) {
  if (!s_begun) {
    return;
  }
  const uint32_t now = millis();

  // The settle timer that replaced delay(RADIO_SETTLE_MS). One stack is down,
  // the other is not up yet, and the radio is genuinely OFF meanwhile.
  if (s_phase == NPH_SETTLING) {
    if ((uint32_t)(now - s_phase_ms) < (uint32_t)RADIO_SETTLE_MS) {
      return;
    }
    (void)bring_up(s_pending);
    return;
  }

#if FEATURE_BLE
  if (s_mode == RADIO_BLE && !BLEDevice::getInitialized()) {
    // Somebody deinitialised behind our back, or init silently failed.
    set_mode(RADIO_OFF, NPH_OFF, "BLE vanished");
    return;
  }
#endif

#if NT_NET_WANT_WIFI
  switch (s_phase) {

    // A BACKSTOP, AND IT IS DELIBERATELY LATER THAN THE JOB'S OWN CLOCK.
    // networking/wifi_scanner.h owns the section 47 timeout at
    // WIFI_SCAN_TIMEOUT_MS and releases the radio through the driver's stop().
    // This one only fires when NOBODY IS PUMPING THE JOB AT ALL - a screen torn
    // down mid-scan, a caller that forgot to service it - and because its
    // deadline is strictly later it can never pre-empt the job's answer or
    // disagree with it about what happened.
    case NPH_SCANNING:
      if ((uint32_t)(now - s_phase_ms) >= NET_SCAN_BACKSTOP_MS) {
        s_err = NERR_SCAN_FAILED;
        NET_LOGF("[net] scan backstop fired - nobody serviced the job\r\n");
        wifi_down();
        s_want_scan = false;
        set_mode(RADIO_OFF, NPH_OFF, "SCAN BACKSTOP");
      }
      break;

    // NPH_AP_PORTAL has no timer of its own. The portal is not a fallback the
    // firmware retries out of any more (plan section 2 row G4): the screen that
    // asked for RADIO_WIFI owns it, and leaving that screen releases it.

    default:
      break;
  }
#endif  // NT_NET_WANT_WIFI
}

const char *net_ip(void) {
  return s_ip;
}

bool net_is_ap_up(void) {
  return s_ap_up;
}

StrId net_last_err_str(void) {
  switch (s_err) {
    case NERR_NONE:            return STR_EMPTY;
    case NERR_BUSY:            return STR_ERR_BUSY;
    case NERR_BLE_SESSION_CAP: return STR_SO_CAP;
    case NERR_BLE_INIT_FAILED: return STR_ERR_MEM;
    case NERR_SCAN_FAILED:     return STR_ERR_NO_WIFI;
    case NERR_WIFI_DISABLED:   return STR_ERR_NO_WIFI;
    case NERR_BLE_DISABLED:    return STR_ERR_NO_NET;
    case NERR_AP_FAILED:       return STR_ERR_NO_NET;
    case NERR_DNS_FAILED:      return STR_ERR_NO_NET;
    case NERR_BAD_ARG:         return STR_ERR_NO_NET;
    default:                   return STR_ERR_NO_NET;
  }
}

const char *net_ap_ssid(void) {
  if (!s_begun) {
    net_begin();
  }
  return s_ap_ssid;
}

size_t net_url(char *out, size_t cap, uint16_t pin) {
  if (!out || cap == 0) {
    return 0;
  }
  out[0] = '\0';
  if (s_ip[0] == '\0' || strcmp(s_ip, "0.0.0.0") == 0) {
    return 0;
  }
  // BRIEF 1.4: IP only. A hostname form would be 33 B and would force QR
  // version 3. Worst case here is
  // "http://255.255.255.255/?k=9999" = 30 B, inside the 32 B v2-L budget.
  int n = snprintf(out, cap, "http://%s/?k=%04u",
                   s_ip, (unsigned)(pin % (unsigned)WEB_PIN_MAX));
  if (n <= 0 || (size_t)n >= cap) {
    out[0] = '\0';
    return 0;
  }
  return (size_t)n;
}

const NetHeapStats &net_heap_last(void) {
  return s_heap;
}

uint8_t net_ble_sessions_used(void) {
  return s_ble_sessions;
}

uint8_t net_ble_sessions_left(void) {
  return (s_ble_sessions >= BLE_SESSION_CAP)
             ? 0
             : (uint8_t)(BLE_SESSION_CAP - s_ble_sessions);
}

// =============================================================================
//  THE SCANNER'S RADIO DRIVER (P5-C1)
//
//  The four calls networking/wifi_scanner.cpp makes, and the ONE loop in the
//  firmware where a beacon's name and hardware address exist. Both die inside
//  scan_read(): what comes out is a salted 32-bit hash and a category ordinal
//  (spec section 44). Nothing here returns an Arduino String - WiFi.SSID(i)
//  would heap-allocate one per access point AND would be a name escaping by
//  accident - so the records are read straight out of the driver's array.
// =============================================================================
void net_scan_salt_set(uint32_t device_id) {
  s_scan_salt = net_scan_salt(device_id);
}

#if NT_NET_WANT_WIFI
// The one mapping from the IDF's auth modes onto the project enum the pure
// classifier speaks. NAUTH_OTHER is the default arm, so WIFI_AUTH_DPP and every
// constant the core gains next classify rather than fall off the end.
static uint8_t auth_of(wifi_auth_mode_t m) {
  switch (m) {
    case WIFI_AUTH_OPEN:                    return (uint8_t)NAUTH_OPEN;
    case WIFI_AUTH_WEP:                     return (uint8_t)NAUTH_WEP;
    case WIFI_AUTH_WPA_PSK:
    case WIFI_AUTH_WPA2_PSK:
    case WIFI_AUTH_WPA_WPA2_PSK:
    case WIFI_AUTH_WPA3_PSK:
    case WIFI_AUTH_WPA2_WPA3_PSK:
    case WIFI_AUTH_WAPI_PSK:
    case WIFI_AUTH_WPA3_EXT_PSK:
    case WIFI_AUTH_WPA3_EXT_PSK_MIXED_MODE: return (uint8_t)NAUTH_PSK;
    // WIFI_AUTH_WPA2_ENTERPRISE is an ALIAS of WIFI_AUTH_ENTERPRISE in this
    // core, so naming both here would be a duplicate case label.
    case WIFI_AUTH_ENTERPRISE:
    case WIFI_AUTH_WPA3_ENT_192:            return (uint8_t)NAUTH_ENTERPRISE;
    case WIFI_AUTH_OWE:                     return (uint8_t)NAUTH_OWE;
    default:                                return (uint8_t)NAUTH_OTHER;
  }
}
// The classifier is a pure translation unit and cannot see wifi_auth_mode_t, so
// this is the file that pins the two together.
static_assert((int)WIFI_AUTH_OPEN == 0, "WIFI_AUTH_OPEN is no longer 0");
static_assert((int)WIFI_AUTH_WPA2_ENTERPRISE == (int)WIFI_AUTH_ENTERPRISE,
              "WIFI_AUTH_WPA2_ENTERPRISE stopped aliasing WIFI_AUTH_ENTERPRISE - "
              "auth_of() needs a case for it");
static_assert((int)WIFI_AUTH_MAX > (int)WIFI_AUTH_DPP,
              "the auth enum shrank under auth_of()");
static_assert((int)NAUTH_COUNT == 6, "NetAuth gained or lost a value");

static bool scan_start(void) {
  if (!s_begun) {
    net_begin();
  }
  // ONE RADIO, ONE OWNER. The creator portal is the other WiFi intent; refuse
  // rather than silently hand back a scan that will never run.
  if (s_mode == RADIO_WIFI && s_phase == NPH_AP_PORTAL) {
    s_err = NERR_BUSY;
    return false;
  }
  s_want_scan = true;
  if (!net_request(RADIO_WIFI)) {
    s_want_scan = false;
    return false;
  }
  return true;
}

static int16_t scan_poll(void) {
  if (s_phase == NPH_SETTLING) {
    return (int16_t)WSCAN_POLL_RUNNING;      // the radio is still coming up
  }
  if (s_mode != RADIO_WIFI || s_phase != NPH_SCANNING) {
    return (int16_t)WSCAN_POLL_FAILED;       // bring-up failed, or somebody
  }                                          // else took the radio
  const int16_t r = WiFi.scanComplete();
  if (r == WIFI_SCAN_RUNNING) {
    return (int16_t)WSCAN_POLL_RUNNING;
  }
  if (r < 0) {
    // WIFI_SCAN_FAILED covers both "timed out" and "was never triggered" - the
    // core cannot tell them apart, which is exactly why wifi_scanner.h keeps
    // its own clock instead of trusting this value to arrive.
    s_err = NERR_SCAN_FAILED;
    return (int16_t)WSCAN_POLL_FAILED;
  }
  return r;
}

static uint8_t scan_read(ScanResult *out, uint8_t cap) {
  if (!out || cap == 0) {
    WiFi.scanDelete();
    return 0;
  }
  const int16_t n = WiFi.scanComplete();
  uint8_t w = 0;
  for (int16_t i = 0; i < n && w < cap; ++i) {
    const wifi_ap_record_t *rec =
        (const wifi_ap_record_t *)WiFi.getScanInfoByIndex((int)i);
    if (!rec) {
      continue;
    }
    // --- the only place a name and an address exist -----------------------
    size_t name_len = 0;
    while (name_len < sizeof(rec->ssid) && rec->ssid[name_len] != 0) {
      ++name_len;
    }
    NetFacts f;
    f.auth   = auth_of(rec->authmode);
    f.hidden = (name_len == 0) ? 1u : 0u;
    f.rssi   = net_rssi_clamp((int32_t)rec->rssi);
    f.tokens = f.hidden ? 0u
                        : net_tokens_of((const char *)rec->ssid, name_len);
    out[w].net_hash    = net_hash_from_bssid(rec->bssid, s_scan_salt);
    // --- and here they are gone -------------------------------------------
    out[w].rssi        = f.rssi;
    out[w].category    = net_classify(f);
    out[w].reserved[0] = 0;
    out[w].reserved[1] = 0;
    ++w;
  }
  WiFi.scanDelete();
  NET_LOGF("[net] scan read %u of %d\r\n", (unsigned)w, (int)n);
  return w;
}

static void scan_stop(void) {
  s_want_scan = false;
  // Always: this is the call that makes spec section 40's "Wi-Fi shuts down
  // after use" true, and wifi_scanner.cpp guarantees it happens exactly once
  // per run on every exit path - done, failed, timed out or cancelled.
  (void)net_request(RADIO_OFF);
}

static const WifiScanDriver s_scan_driver = {
  &scan_start, &scan_poll, &scan_read, &scan_stop
};

#else   // NT_NET_WANT_WIFI

// No WiFi consumer in this variant: the job refuses at start() and reports
// WSCAN_FAILED, which is a defined outcome rather than a screen that hangs.
static bool    scan_start(void) { s_err = NERR_WIFI_DISABLED; return false; }
static int16_t scan_poll(void)  { return (int16_t)WSCAN_POLL_FAILED; }
static uint8_t scan_read(ScanResult *out, uint8_t cap) { (void)out; (void)cap; return 0; }
static void    scan_stop(void)  { (void)net_request(RADIO_OFF); }

static const WifiScanDriver s_scan_driver = {
  &scan_start, &scan_poll, &scan_read, &scan_stop
};

#endif  // NT_NET_WANT_WIFI

const WifiScanDriver &net_scan_driver(void) {
  return s_scan_driver;
}
