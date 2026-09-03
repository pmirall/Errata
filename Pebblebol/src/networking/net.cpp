// =============================================================================
//  net.cpp - Nottamagochi radio state machine (BRIEF 1.3).
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

// -----------------------------------------------------------------------------
// Module state
// -----------------------------------------------------------------------------
static bool         s_begun          = false;
static RadioMode    s_mode           = RADIO_OFF;
static NetPhase     s_phase          = NPH_OFF;
static NetErr       s_err            = NERR_NONE;

static uint32_t     s_phase_ms       = 0;    // millis() when the phase was entered
static RadioMode    s_pending        = RADIO_OFF;  // NPH_SETTLING target
static uint8_t      s_fails          = 0;    // consecutive association failures
static uint8_t      s_ble_sessions   = 0;    // BLEDevice::init() calls this boot
static bool         s_ap_up          = false;

#if NT_NET_WANT_WIFI
static uint32_t     s_retry_at_ms    = 0;    // next association attempt
static uint32_t     s_link_lost_ms   = 0;    // 0 = link healthy
static uint32_t     s_ip_refresh_ms  = 0;
static bool         s_dns_up         = false;
#endif

static char         s_ip[16]         = "0.0.0.0";
static char         s_ap_ssid[SSID_MAX_LEN + 1];
static char         s_ble_name[16];
static char         s_ssid[SSID_MAX_LEN + 1];
static char         s_pass[PASS_MAX_LEN + 1];

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
  wifi_mode_t m = WiFi.getMode();
  if (m & WIFI_MODE_STA) {
    // (wifioff, eraseap, timeout_ms) - three parameters in 3.1.1.
    WiFi.disconnect(false, false, 100);
  }
  if (m != WIFI_MODE_NULL) {
    // This is the call that genuinely returns the ~50 KB:
    // esp_wifi_stop() + esp_wifi_deinit() + esp_netif_destroy_default_wifi().
    WiFi.mode(WIFI_MODE_NULL);
  }
  clear_ip();
  s_link_lost_ms = 0;
}

static void refresh_sta_ip(void) {
  IPAddress ip = WiFi.localIP();
  set_ip_str(ip[0], ip[1], ip[2], ip[3]);
  s_ip_refresh_ms = millis();
}

static void sta_start(void) {
  WiFi.persistent(false);                 // do not burn NVS on every begin()
  WiFi.setHostname(FW_NAME);              // before the netif exists
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(true);                    // modem sleep; we are battery bound
  WiFi.begin(s_ssid, s_pass[0] ? s_pass : (const char *)NULL);
  clear_ip();
  s_link_lost_ms = 0;
  set_phase(NPH_STA_CONNECTING);
  NET_LOGF("[net] STA connecting to \"%s\" (try %u/%u)\r\n",
           s_ssid, (unsigned)(s_fails + 1), (unsigned)WIFI_MAX_FAILS);
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

// Association attempt failed: back off, or fall back to the portal.
static void sta_failed(uint32_t now) {
  ++s_fails;
  WiFi.disconnect(false, false, 100);
  clear_ip();
  if (s_fails >= WIFI_MAX_FAILS) {
    s_err = NERR_STA_TIMEOUT;
    NET_LOGF("[net] STA gave up after %u attempts\r\n", (unsigned)s_fails);
#if NT_NET_HAVE_PORTAL
    ap_start();
    return;
#else
    s_fails       = 0;
    s_retry_at_ms = now + (WIFI_RETRY_PERIOD_S * 1000UL);
    set_phase(NPH_STA_RETRY_WAIT);
    return;
#endif
  }
  uint32_t backoff = 2000UL << (s_fails - 1);   // 2 s, 4 s, 8 s ...
  if (backoff > 30000UL) {
    backoff = 30000UL;
  }
  s_retry_at_ms = now + backoff;
  set_phase(NPH_STA_RETRY_WAIT);
  NET_LOGF("[net] STA retry in %lu ms\r\n", (unsigned long)backoff);
}

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
  copy_bounded(s_ssid, sizeof(s_ssid), CFG_WIFI_SSID);
  copy_bounded(s_pass, sizeof(s_pass), CFG_WIFI_PASS);
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
  s_fails = 0;
  if (s_ssid[0] == '\0') {
    s_err = NERR_NO_CREDENTIALS;
#if NT_NET_HAVE_PORTAL
    NET_LOGF("[net] no credentials -> provisioning portal\r\n");
    return ap_start();
#else
    NET_LOGF("[net] no credentials and no portal available\r\n");
    set_mode(RADIO_OFF, NPH_OFF, "-> OFF (no creds)");
    return false;
#endif
  }
  set_mode(RADIO_WIFI, NPH_STA_CONNECTING, "-> WIFI");
  sta_start();
  return true;
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
    s_fails   = 0;
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
    return true;   // already on the WiFi track (connecting, up, or portal)
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

    case NPH_STA_CONNECTING:
      if (WiFi.status() == WL_CONNECTED) {
        s_fails = 0;
        s_err   = NERR_NONE;
        refresh_sta_ip();
        set_phase(NPH_STA_UP);
        net_heap_log("STA UP");
        NET_LOGF("[net] STA up: %s rssi=%d\r\n", s_ip, (int)WiFi.RSSI());
      } else if ((uint32_t)(now - s_phase_ms) >= WIFI_CONNECT_TIMEOUT_MS) {
        sta_failed(now);
      }
      break;

    case NPH_STA_RETRY_WAIT:
      if ((int32_t)(now - s_retry_at_ms) >= 0) {
        sta_start();
      }
      break;

    case NPH_STA_UP:
      if (WiFi.status() != WL_CONNECTED) {
        if (s_link_lost_ms == 0) {
          s_link_lost_ms = now;
          NET_LOGF("[net] STA link lost, waiting for auto-reconnect\r\n");
        } else if ((uint32_t)(now - s_link_lost_ms) >= WIFI_CONNECT_TIMEOUT_MS) {
          clear_ip();
          s_link_lost_ms = 0;
          s_fails        = 0;
          sta_start();
        }
      } else {
        if (s_link_lost_ms != 0) {
          s_link_lost_ms = 0;
          refresh_sta_ip();
          NET_LOGF("[net] STA re-associated: %s\r\n", s_ip);
        } else if ((uint32_t)(now - s_ip_refresh_ms) >= 5000UL) {
          refresh_sta_ip();   // DHCP renew can move us
        }
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

bool net_is_sta_up(void) {
#if NT_NET_WANT_WIFI
  return (s_mode == RADIO_WIFI) && (s_phase == NPH_STA_UP) &&
         (WiFi.status() == WL_CONNECTED);
#else
  return false;
#endif
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
    case NERR_NO_CREDENTIALS:  return STR_ERR_NO_WIFI;
    case NERR_STA_TIMEOUT:     return STR_ERR_NO_WIFI;
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

void net_set_credentials(const char *ssid, const char *pass) {
  if (!s_begun) {
    net_begin();
  }
  char new_ssid[SSID_MAX_LEN + 1];
  char new_pass[PASS_MAX_LEN + 1];
  copy_bounded(new_ssid, sizeof(new_ssid), ssid);
  copy_bounded(new_pass, sizeof(new_pass), pass);
  bool changed = (strcmp(new_ssid, s_ssid) != 0) || (strcmp(new_pass, s_pass) != 0);
  copy_bounded(s_ssid, sizeof(s_ssid), new_ssid);
  copy_bounded(s_pass, sizeof(s_pass), new_pass);
  if (!changed) {
    return;
  }
  NET_LOGF("[net] credentials updated (ssid=\"%s\")\r\n", s_ssid);
#if NT_NET_WANT_WIFI
  // Re-associate immediately if we are already on the WiFi track.
  if (s_mode == RADIO_WIFI && s_ssid[0] != '\0') {
    ap_down();
    s_fails = 0;
    set_phase(NPH_STA_CONNECTING);
    sta_start();
  }
#endif
}

bool net_has_credentials(void) {
  return s_ssid[0] != '\0';
}

int8_t net_rssi(void) {
#if NT_NET_WANT_WIFI
  if (s_phase != NPH_STA_UP || WiFi.status() != WL_CONNECTED) {
    return 0;
  }
  int32_t r = WiFi.RSSI();
  if (r > 0) {
    r = 0;
  }
  if (r < -127) {
    r = -127;
  }
  return (int8_t)r;
#else
  return 0;
#endif
}
