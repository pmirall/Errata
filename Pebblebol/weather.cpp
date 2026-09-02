// =============================================================================
//  weather.cpp - Open-Meteo + ip-api over plain HTTP, hand-rolled HTTP/1.0.
//
//  Both endpoints are verified 200 over http:// with no redirect (NET_APIS 1
//  and 2), so this module never touches TLS and never competes with Telegram
//  for the 32 KB mbedTLS buffers.
//
//  Why a raw NetworkClient instead of HTTPClient:
//    * HTTPClient::GET() runs the whole request to completion in one call and
//      would stall the 20 fps render loop for the duration of the transfer.
//      Here the response body is drained WX_RX_CHUNK bytes per loop() pass.
//    * We emit "HTTP/1.0" + "Connection: close" on the wire, which is exactly
//      the behaviour HTTPClient::useHTTP10(true) produces (BRIEF 1.7): the
//      server answers unchunked and closes, so there is nothing to de-chunk.
//    * It costs no String allocation and no extra ~2 KB of HTTPClient state.
//
//  The only two blocking points in the whole module are the DNS lookup (once
//  per boot, or after a connect failure - the address is cached) and the TCP
//  connect, capped at WX_TCP_CONNECT_MS. Everything else is incremental.
//
//  All arithmetic is integer. Temperatures are deci-celsius, wind is
//  deci-km/h, multipliers are per-mille. GAME_DESIGN 6 is canonical.
// =============================================================================

#include "weather.h"

#include <Arduino.h>
#include <WiFi.h>
#include <U8g2lib.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// -----------------------------------------------------------------------------
// INTEGRATION POINT - geolocation persistence.
//
// When CFG_LATITUDE / CFG_LONGITUDE are empty the coordinates are auto
// detected once via ip-api and written back into the Config blob so the next
// boot does not have to ask again. That needs storage.h. If storage.cpp ends
// up exposing a different signature, set WX_GEO_PERSIST to 0: the module then
// keeps the detected coordinates in RAM for the current boot only and costs
// exactly one extra ip-api request per power cycle. Nothing else changes.
//
// Which Config object gets written is decided by wx_bind_config() (weather.h).
// Bound, it is the entry point's g_cfg - the same struct ui and webui hold -
// so there is exactly one Config in the firmware. Unbound, this module falls
// back to a private NVS scratch, which is standalone-friendly but diverges
// from whatever the rest of the firmware has in RAM (PH3 finding 9).
//
// Assumed contract (BRIEF 4, row 6):
//     store_load_cfg(Config &)          - fills the struct, return ignored
//     store_save_cfg(Config &)          - re-seals crc16 IN PLACE, writes NVS
// -----------------------------------------------------------------------------
#define WX_GEO_PERSIST 1
#if WX_GEO_PERSIST
#include "storage.h"
#endif

// gt_now() is the only wall-clock read in this file; it feeds the purely
// informational WeatherState::fetched_epoch. age_s is derived from millis()
// so that staleness survives an unset or jumping clock.
#include "gametime.h"

// =============================================================================
// 1. LOCAL TUNING (everything else lives in config.h section 11)
// =============================================================================

// Cap on the single blocking call in the module. config.h's
// HTTP_CONNECT_TIMEOUT_MS (8000) is the Telegram/TLS budget; 8 s inside one
// loop() pass would be an 8 second freeze of the pet, so the plain-HTTP path
// uses a much tighter cap. A TCP connect on a working LAN costs ~40 ms.
#define WX_TCP_CONNECT_MS 2500

// No byte received for this long during a transfer -> give up.
#define WX_RX_TIMEOUT_MS 6000UL

// Bytes drained from the socket per loop() pass. 512 B at 20 fps means a
// typical ~600 B body lands in two frames.
#define WX_RX_CHUNK 512

// First fetch after boot: let WiFi settle first.
#define WX_FIRST_DELAY_MS 4000UL

// Give up on ip-api for a while after this many consecutive failures. The
// free tier is 45 requests/minute per IP and bans abusers for an hour.
#define WX_GEO_MAX_FAILS 3
#define WX_GEO_COOLDOWN_S 3600UL

// Request line + headers. Longest case measured at 288 B.
#define WX_REQ_MAX 384

// =============================================================================
// 2. MODULE STATE
// =============================================================================

enum WxPhase : uint8_t { WXP_IDLE = 0, WXP_RESOLVE, WXP_CONNECT, WXP_SEND, WXP_RECV };

enum WxTarget : uint8_t { WXT_WEATHER = 0, WXT_GEO = 1 };

static WeatherState g_wx;

static char g_lat[COORD_MAX_LEN + 1];
static char g_lon[COORD_MAX_LEN + 1];

static NetworkClient g_client;
static IPAddress g_ip_wx;
static IPAddress g_ip_geo;
static bool g_ip_wx_ok = false;
static bool g_ip_geo_ok = false;

static uint8_t g_phase = WXP_IDLE;
static uint8_t g_target = WXT_WEATHER;
static uint8_t g_fails = 0;      // consecutive weather failures
static uint8_t g_geo_fails = 0;  // consecutive ip-api failures
static bool g_have_ok = false;   // at least one successful weather fetch

static uint32_t g_next_at_ms = 0;   // millis() deadline of the next attempt
static uint32_t g_last_rx_ms = 0;   // millis() of the last byte received
static uint32_t g_last_ok_ms = 0;   // millis() of the last good weather body

// Receive scratch. Shared by both endpoints; the weather body is the larger.
static char g_body[WX_BODY_MAX];
static uint16_t g_body_len = 0;
static uint16_t g_body_cap = 0;
static uint8_t g_rx_stage = 0;  // 0 = status line, 1 = headers, 2 = body
static uint8_t g_crlf = 0;      // "\r\n\r\n" matcher, 0..3
static char g_status_line[24];
static uint8_t g_status_len = 0;
static int16_t g_http_code = 0;

static char g_req[WX_REQ_MAX];

// =============================================================================
// 3. WMO -> WeatherGroup, and the GAME_DESIGN 6.1 / 6.2 tables
// =============================================================================

struct WxRow {
  uint16_t hap;   // per-mille multiplier on happiness decay
  uint16_t en;    // per-mille multiplier on energy decay
  int8_t mood;    // flat offset on the displayed mood score
  uint8_t sick;   // additive sickness chance, per-mille per hour
};

static const WxRow WX_TAB[WX_COUNT] = {
  /* WX_UNKNOWN      */ {1000, 1000, 0, 0},
  /* WX_CLEAR_DAY    */ {850, 1000, 8, 0},
  /* WX_CLEAR_NIGHT  */ {950, 900, 3, 0},
  /* WX_PARTLY       */ {1000, 1000, 0, 0},
  /* WX_CLOUDY       */ {1120, 1050, -5, 5},
  /* WX_FOG          */ {1200, 1000, -8, 10},
  /* WX_DRIZZLE      */ {1150, 1000, -6, 15},
  /* WX_RAIN         */ {1300, 1100, -12, 30},
  /* WX_SNOW         */ {1100, 1250, -4, 25},
  /* WX_SHOWERS      */ {1250, 1100, -10, 25},
  /* WX_SNOW_SHOWERS */ {1150, 1200, -6, 25},
  /* WX_STORM        */ {1450, 1200, -18, 30},
  /* WX_STORM_HAIL   */ {1600, 1300, -25, 40},
};
static_assert(NT_ARRAY_LEN(WX_TAB) == (size_t)WX_COUNT, "WX_TAB must cover every WeatherGroup");

// Representative WMO code per group, used by wx_force() so the god-mode
// override still produces a plausible WeatherState.
static const uint8_t WX_GROUP_CODE[WX_COUNT] = {0, 0, 0, 2, 3, 45, 53, 63, 73, 81, 85, 95, 96};
static_assert(NT_ARRAY_LEN(WX_GROUP_CODE) == (size_t)WX_COUNT, "WX_GROUP_CODE must cover every WeatherGroup");

// GAME_DESIGN 6.2, apparent temperature in deci-celsius. First row whose
// max_dc strictly exceeds app_dc wins.
struct WxTempRow {
  int16_t max_dc;
  uint16_t hap;
  uint16_t en;
  uint16_t hunger;
  int8_t mood;
  uint8_t sick;
};

static const WxTempRow WX_TEMP[] = {
  /* < -2.0 C   */ {-20, 1000, 1350, 1250, 0, 0},
  /* -2 .. 8    */ {80, 1000, 1000, 1150, 0, 0},
  /* 8 .. 24    */ {240, 1000, 1000, 1000, 2, 0},
  /* 24 .. 30   */ {300, 1100, 1150, 1000, 0, 0},
  /* 30 .. 35   */ {350, 1000, 1300, 850, 0, 20},
  /* > 35       */ {32767, 1000, 1300, 850, 0, 40},
};

// Wind at or above 40.0 km/h costs three mood points (GAME_DESIGN 6.2).
#define WX_WIND_HARSH_DKMH 400
#define WX_WIND_MOOD (-3)

static uint8_t wx_group_from_code(uint16_t code, uint8_t is_day) {
  switch (code) {
    case 0: return is_day ? (uint8_t)WX_CLEAR_DAY : (uint8_t)WX_CLEAR_NIGHT;
    case 1:
    case 2: return WX_PARTLY;
    case 3: return WX_CLOUDY;
    case 45:
    case 48: return WX_FOG;
    case 51:
    case 53:
    case 55:
    case 56:
    case 57: return WX_DRIZZLE;
    case 61:
    case 63:
    case 65:
    case 66:
    case 67: return WX_RAIN;
    case 71:
    case 73:
    case 75:
    case 77: return WX_SNOW;
    case 80:
    case 81:
    case 82: return WX_SHOWERS;
    case 85:
    case 86: return WX_SNOW_SHOWERS;
    case 95: return WX_STORM;
    case 96:
    case 99: return WX_STORM_HAIL;
    default: break;
  }
  // Open-Meteo emits nothing outside the 28 codes above (NET_APIS 1), but a
  // future variable or a proxy could. Degrade by WMO 4677 decade instead of
  // pretending we have no data at all.
  if (code >= 95) return WX_STORM;
  if (code >= 80) return WX_SHOWERS;
  if (code >= 70) return WX_SNOW;
  if (code >= 60) return WX_RAIN;
  if (code >= 50) return WX_DRIZZLE;
  if (code >= 40) return WX_FOG;
  if (code <= 3) return WX_PARTLY;
  return WX_UNKNOWN;
}

// =============================================================================
// 4. HELPERS
// =============================================================================

static inline bool wx_sta_up(void) {
  // Reading the link state is not a radio lifecycle operation, so this does
  // not need net.cpp. When RadioMode is RADIO_BLE or RADIO_OFF, WiFi is down
  // and this is false, which is exactly the gate we want: the module can
  // never race a BLE scan.
  return WiFi.status() == WL_CONNECTED;
}

static inline bool wx_active(void) {
  return g_wx.valid != 0 && g_wx.group != (uint8_t)WX_UNKNOWN && g_wx.group < (uint8_t)WX_COUNT;
}

static const WxTempRow &wx_temp_row(void) {
  int16_t t = g_wx.app_dc;
  for (uint8_t i = 0; i < (uint8_t)NT_ARRAY_LEN(WX_TEMP); i++) {
    if (t < WX_TEMP[i].max_dc) return WX_TEMP[i];
  }
  return WX_TEMP[NT_ARRAY_LEN(WX_TEMP) - 1];
}

// A coordinate is text all the way to the query string, so it must be proved
// harmless before it is spliced into a request line.
static bool wx_coord_ok(const char *s) {
  if (s == NULL || s[0] == '\0') return false;
  size_t n = strlen(s);
  if (n > (size_t)COORD_MAX_LEN) return false;
  bool digit = false;
  bool dot = false;
  for (size_t i = 0; i < n; i++) {
    char c = s[i];
    if (c == '-' || c == '+') {
      if (i != 0) return false;
    } else if (c == '.') {
      if (dot) return false;
      dot = true;
    } else if (c >= '0' && c <= '9') {
      digit = true;
    } else {
      return false;
    }
  }
  return digit;
}

static inline bool wx_have_coords(void) {
  return wx_coord_ok(g_lat) && wx_coord_ok(g_lon);
}

// Parse a JSON number that follows `key` (which must include its own quotes
// and the colon), scaled by 10^scale and rounded half away from zero.
// Returns false for a missing key, a null, or a quoted value.
static bool json_scaled(const char *from, const char *key, uint8_t scale, int32_t *out) {
  const char *p = strstr(from, key);
  if (p == NULL) return false;
  p += strlen(key);
  while (*p == ' ' || *p == '\t') p++;

  bool neg = false;
  if (*p == '-') {
    neg = true;
    p++;
  } else if (*p == '+') {
    p++;
  }
  if (*p < '0' || *p > '9') return false;

  int32_t whole = 0;
  uint8_t guard = 0;
  while (*p >= '0' && *p <= '9') {
    if (guard < 7) {
      whole = whole * 10 + (int32_t)(*p - '0');
      guard++;
    }
    p++;
  }

  int32_t frac = 0;
  uint8_t taken = 0;
  int32_t roundup = 0;
  if (*p == '.') {
    p++;
    while (*p >= '0' && *p <= '9') {
      if (taken < scale) {
        frac = frac * 10 + (int32_t)(*p - '0');
        taken++;
      } else if (taken == scale) {
        if (*p >= '5') roundup = 1;
        taken++;
      }
      p++;
    }
  }
  while (taken < scale) {
    frac *= 10;
    taken++;
  }

  int32_t r = whole;
  for (uint8_t i = 0; i < scale; i++) r *= 10;
  r += frac + roundup;
  *out = neg ? -r : r;
  return true;
}

static void wx_clear_data(void) {
  uint8_t forced = g_wx.forced;
  memset(&g_wx, 0, sizeof(g_wx));
  g_wx.forced = forced;
  g_wx.group = (uint8_t)WX_UNKNOWN;
}

static uint32_t wx_backoff_s(uint8_t fails) {
  if (fails <= 1) return 60UL;
  if (fails == 2) return 180UL;
  if (fails == 3) return 600UL;
  return WX_POLL_S;
}

static uint32_t wx_cadence_s(void) {
  // WX_POLL_S +/- WX_POLL_JITTER_S, so a room full of pets does not hit
  // Open-Meteo in lockstep.
  long j = random(0, (long)(2UL * WX_POLL_JITTER_S) + 1);
  return WX_POLL_S + (uint32_t)j - WX_POLL_JITTER_S;
}

static void wx_schedule_in(uint32_t seconds) {
  g_next_at_ms = millis() + seconds * 1000UL;
}

// =============================================================================
// 5. CONFIG GLUE
// =============================================================================

// The entry point's live Config when bound (the intended wiring), nullptr in
// standalone mode. See wx_bind_config() in weather.h for why this exists.
static Config *g_cfg_bound = nullptr;

#if WX_GEO_PERSIST
// Fallback for the UNBOUND case only. One shared scratch copy rather than a
// 256 B stack frame or two separate statics; only ever live for the duration of
// one of the two calls below.
static Config g_cfg_scratch;
#endif

void wx_bind_config(Config *cfg) {
  g_cfg_bound = cfg;
}

static void wx_load_coords_from_cfg(void) {
  // Bound: read the live struct, so a coordinate the user just typed into S9 or
  // posted to /api/cfg is picked up without an NVS round trip, and a location
  // this module auto-detected is never read back stale.
  const Config *src = g_cfg_bound;
#if WX_GEO_PERSIST
  if (src == NULL) {
    Config &cfg = g_cfg_scratch;
    memset(&cfg, 0, sizeof(cfg));
    (void)store_load_cfg(cfg);
    src = &cfg;
  }
#endif
  if (src == NULL) return;
  if (src->magic != NT_CFG_MAGIC || src->version != NT_CFG_VERSION) return;
  if (!wx_coord_ok(src->lat) || !wx_coord_ok(src->lon)) return;
  strncpy(g_lat, src->lat, COORD_MAX_LEN);
  g_lat[COORD_MAX_LEN] = '\0';
  strncpy(g_lon, src->lon, COORD_MAX_LEN);
  g_lon[COORD_MAX_LEN] = '\0';
}

static void wx_store_coords_to_cfg(void) {
#if WX_GEO_PERSIST
  // Bound: write into the ONE Config every module shares, then let
  // store_save_cfg() persist and re-seal it. RAM and NVS therefore agree, so
  // the next save from S9 or POST /api/cfg can no longer revert the detected
  // location, and this write can no longer revert an unsaved S9 edit.
  Config *dst = g_cfg_bound;
  if (dst == NULL) {
    Config &cfg = g_cfg_scratch;
    memset(&cfg, 0, sizeof(cfg));
    (void)store_load_cfg(cfg);
    dst = &cfg;
  }
  if (dst->magic != NT_CFG_MAGIC || dst->version != NT_CFG_VERSION) return;
  strncpy(dst->lat, g_lat, COORD_MAX_LEN);
  dst->lat[COORD_MAX_LEN] = '\0';
  strncpy(dst->lon, g_lon, COORD_MAX_LEN);
  dst->lon[COORD_MAX_LEN] = '\0';
  dst->flags |= CF_GEO_AUTO;
  (void)store_save_cfg(*dst);
#endif
}

// =============================================================================
// 6. RECEIVE / PARSE
// =============================================================================

static void wx_rx_reset(uint16_t cap) {
  g_body_len = 0;
  g_body_cap = (cap > (uint16_t)(WX_BODY_MAX - 1)) ? (uint16_t)(WX_BODY_MAX - 1) : cap;
  g_rx_stage = 0;
  g_crlf = 0;
  g_status_len = 0;
  g_status_line[0] = '\0';
  g_http_code = 0;
  g_body[0] = '\0';
}

static void wx_rx_byte(uint8_t b) {
  if (g_rx_stage < 2) {
    if (g_rx_stage == 0) {
      if (b == '\r' || b == '\n') {
        g_status_line[g_status_len] = '\0';
        // "HTTP/1.1 200 OK" -> the number after the first space.
        const char *sp = strchr(g_status_line, ' ');
        g_http_code = (sp != NULL) ? (int16_t)atoi(sp + 1) : (int16_t)0;
        g_rx_stage = 1;
      } else if (g_status_len < (uint8_t)(sizeof(g_status_line) - 1)) {
        g_status_line[g_status_len++] = (char)b;
      }
    }
    // "\r\n\r\n" matcher, running over the status line too so that a reply
    // with no headers at all is still handled.
    if (b == '\r') {
      g_crlf = (g_crlf == 2) ? 3 : 1;
    } else if (b == '\n') {
      if (g_crlf == 1) {
        g_crlf = 2;
      } else if (g_crlf == 3) {
        g_crlf = 0;
        g_rx_stage = 2;
      } else {
        g_crlf = 0;
      }
    } else {
      g_crlf = 0;
    }
    return;
  }
  if (g_body_len < g_body_cap) g_body[g_body_len++] = (char)b;
}

static bool wx_parse_weather(void) {
  g_body[g_body_len] = '\0';
  if (strstr(g_body, "\"error\":true") != NULL) return false;

  // MUST anchor on "current":{ first: "current_units" carries the same key
  // names and appears earlier in the byte stream (BRIEF 2, row 11).
  const char *cur = strstr(g_body, "\"current\":{");
  if (cur == NULL) return false;

  int32_t v_temp = 0;
  int32_t v_code = 0;
  int32_t v_app = 0;
  int32_t v_day = 1;
  int32_t v_wind = 0;
  int32_t v_prec = 0;

  if (!json_scaled(cur, "\"temperature_2m\":", 1, &v_temp)) return false;
  if (!json_scaled(cur, "\"weather_code\":", 0, &v_code)) return false;
  if (!json_scaled(cur, "\"apparent_temperature\":", 1, &v_app)) v_app = v_temp;
  if (!json_scaled(cur, "\"is_day\":", 0, &v_day)) v_day = 1;
  if (!json_scaled(cur, "\"wind_speed_10m\":", 1, &v_wind)) v_wind = 0;
  if (!json_scaled(cur, "\"precipitation\":", 1, &v_prec)) v_prec = 0;

  if (v_code < 0 || v_code > 99) return false;

  g_wx.temp_dc = (int16_t)NT_CLAMP(v_temp, -900, 900);
  g_wx.app_dc = (int16_t)NT_CLAMP(v_app, -900, 900);
  g_wx.wind_dkmh = (uint16_t)NT_CLAMP(v_wind, 0, 4000);
  g_wx.precip_dmm = (uint16_t)NT_CLAMP(v_prec, 0, 5000);
  g_wx.is_day = (v_day != 0) ? 1 : 0;
  g_wx.code = (uint16_t)v_code;
  g_wx.group = wx_group_from_code(g_wx.code, g_wx.is_day);
  g_wx.valid = 1;
  g_wx.age_s = 0;
  g_wx.fetched_epoch = (uint32_t)gt_now();
  return true;
}

// ip-api /line/ answers bare newline separated text:
//   success \n city \n lat \n lon \n timezone
static bool wx_parse_geo(void) {
  g_body[g_body_len] = '\0';

  const char *field[5] = {NULL, NULL, NULL, NULL, NULL};
  uint8_t n = 0;
  char *p = g_body;
  field[0] = p;
  n = 1;
  while (*p != '\0' && n < 5) {
    if (*p == '\n') {
      *p = '\0';
      field[n++] = p + 1;
    } else if (*p == '\r') {
      *p = '\0';
    }
    p++;
  }
  // Terminate the last field too.
  while (*p != '\0') {
    if (*p == '\n' || *p == '\r') {
      *p = '\0';
      break;
    }
    p++;
  }

  if (field[0] == NULL || strncmp(field[0], "success", 7) != 0) return false;
  if (n < 4 || field[2] == NULL || field[3] == NULL) return false;
  if (!wx_coord_ok(field[2]) || !wx_coord_ok(field[3])) return false;

  strncpy(g_lat, field[2], COORD_MAX_LEN);
  g_lat[COORD_MAX_LEN] = '\0';
  strncpy(g_lon, field[3], COORD_MAX_LEN);
  g_lon[COORD_MAX_LEN] = '\0';
  wx_store_coords_to_cfg();
  return true;
}

// =============================================================================
// 7. THE FETCH STATE MACHINE
// =============================================================================

static void wx_abort(bool ok) {
  g_client.stop();
  g_phase = WXP_IDLE;

  if (g_target == WXT_GEO) {
    if (ok) {
      g_geo_fails = 0;
      wx_schedule_in(2);  // coordinates in hand: go straight for the weather
    } else {
      g_geo_fails++;
      wx_schedule_in(g_geo_fails >= WX_GEO_MAX_FAILS ? WX_GEO_COOLDOWN_S : 120UL);
    }
    return;
  }

  if (ok) {
    g_fails = 0;
    g_have_ok = true;
    g_last_ok_ms = millis();
    wx_schedule_in(wx_cadence_s());
  } else {
    if (g_fails < 250) g_fails++;
    if (g_fails >= WX_MAX_FAILS) {
      // GAME_DESIGN 6.1, last row: three consecutive API failures is one of
      // the two ways into WEATHER_UNKNOWN (the other is data older than 6 h).
      wx_clear_data();
      g_have_ok = false;
      g_ip_wx_ok = false;  // and re-resolve: the CDN address may have moved
    }
    wx_schedule_in(wx_backoff_s(g_fails));
  }
}

static void wx_build_request(void) {
  if (g_target == WXT_GEO) {
    snprintf(g_req, sizeof(g_req),
             "GET %s HTTP/1.0\r\n"
             "Host: %s\r\n"
             "User-Agent: " FW_NAME "/" FW_VERSION "\r\n"
             "Connection: close\r\n"
             "\r\n",
             GEO_PATH, GEO_HOST);
  } else {
    snprintf(g_req, sizeof(g_req),
             "GET %s?latitude=%s&longitude=%s&current=%s&timeformat=unixtime HTTP/1.0\r\n"
             "Host: %s\r\n"
             "User-Agent: " FW_NAME "/" FW_VERSION "\r\n"
             "Connection: close\r\n"
             "\r\n",
             WX_PATH, g_lat, g_lon, WX_FIELDS, WX_HOST);
  }
}

static void wx_service(uint32_t now) {
  // Compile-time constant: with FEATURE_WEATHER 0 the optimiser drops the whole
  // fetch path (and everything it calls) while the source still references it,
  // so no -Wunused-function noise either way.
  if (!FEATURE_WEATHER) return;

  // Losing the link mid-transfer is normal (net.cpp may be switching to BLE).
  if (g_phase != WXP_IDLE && !wx_sta_up()) {
    wx_abort(false);
    return;
  }

  switch (g_phase) {
    case WXP_IDLE: {
      if (!wx_sta_up()) return;
      if ((int32_t)(now - g_next_at_ms) < 0) return;
      if (!wx_have_coords()) {
        if (g_geo_fails >= WX_GEO_MAX_FAILS) {
          wx_schedule_in(WX_GEO_COOLDOWN_S);
          return;
        }
        g_target = WXT_GEO;
      } else {
        // Pick up a coordinate change made from the settings screen or the
        // web UI without needing a reboot. Bound (PH3 #9) this is a read of
        // the live Config and costs nothing; only the unbound/standalone
        // fallback still does one NVS read per half hour.
        wx_load_coords_from_cfg();
        g_target = WXT_WEATHER;
      }
      g_phase = WXP_RESOLVE;
      break;
    }

    case WXP_RESOLVE: {
      bool have = (g_target == WXT_GEO) ? g_ip_geo_ok : g_ip_wx_ok;
      if (!have) {
        IPAddress ip;
        const char *host = (g_target == WXT_GEO) ? GEO_HOST : WX_HOST;
        if (WiFi.hostByName(host, ip) != 1) {
          wx_abort(false);
          return;
        }
        if (g_target == WXT_GEO) {
          g_ip_geo = ip;
          g_ip_geo_ok = true;
        } else {
          g_ip_wx = ip;
          g_ip_wx_ok = true;
        }
      }
      g_phase = WXP_CONNECT;
      break;
    }

    case WXP_CONNECT: {
      IPAddress ip = (g_target == WXT_GEO) ? g_ip_geo : g_ip_wx;
      g_client.stop();
      // The one bounded blocking call in the module. See the file header.
      if (g_client.connect(ip, 80, (int32_t)WX_TCP_CONNECT_MS) != 1) {
        if (g_target == WXT_GEO) {
          g_ip_geo_ok = false;
        } else {
          g_ip_wx_ok = false;
        }
        wx_abort(false);
        return;
      }
      g_client.setNoDelay(true);
      g_phase = WXP_SEND;
      break;
    }

    case WXP_SEND: {
      wx_build_request();
      size_t len = strlen(g_req);
      if (g_client.write((const uint8_t *)g_req, len) != len) {
        wx_abort(false);
        return;
      }
      wx_rx_reset((g_target == WXT_GEO) ? (uint16_t)GEO_BODY_MAX : (uint16_t)WX_BODY_MAX);
      g_last_rx_ms = millis();
      g_phase = WXP_RECV;
      break;
    }

    case WXP_RECV: {
      int budget = WX_RX_CHUNK;
      while (budget > 0) {
        int avail = g_client.available();
        if (avail <= 0) break;
        int c = g_client.read();
        if (c < 0) break;
        wx_rx_byte((uint8_t)c);
        budget--;
      }
      if (budget < WX_RX_CHUNK) g_last_rx_ms = now;

      if (g_client.connected() == 0 && g_client.available() <= 0) {
        bool ok = (g_http_code == 200) && (g_rx_stage == 2) && (g_body_len > 0);
        if (ok) ok = (g_target == WXT_GEO) ? wx_parse_geo() : wx_parse_weather();
        wx_abort(ok);
        return;
      }
      if ((uint32_t)(now - g_last_rx_ms) > WX_RX_TIMEOUT_MS) {
        wx_abort(false);
        return;
      }
      break;
    }

    default:
      g_phase = WXP_IDLE;
      break;
  }
}

// =============================================================================
// 8. PUBLIC: LIFECYCLE AND QUERY
// =============================================================================

void wx_begin(void) {
  memset(&g_wx, 0, sizeof(g_wx));
  g_wx.group = (uint8_t)WX_UNKNOWN;

  memset(g_lat, 0, sizeof(g_lat));
  memset(g_lon, 0, sizeof(g_lon));
  strncpy(g_lat, CFG_LATITUDE, COORD_MAX_LEN);
  strncpy(g_lon, CFG_LONGITUDE, COORD_MAX_LEN);
  g_lat[COORD_MAX_LEN] = '\0';
  g_lon[COORD_MAX_LEN] = '\0';
  if (!wx_coord_ok(g_lat) || !wx_coord_ok(g_lon)) {
    g_lat[0] = '\0';
    g_lon[0] = '\0';
  }
  wx_load_coords_from_cfg();

  g_phase = WXP_IDLE;
  g_target = WXT_WEATHER;
  g_fails = 0;
  g_geo_fails = 0;
  g_have_ok = false;
  g_ip_wx_ok = false;
  g_ip_geo_ok = false;
  g_last_ok_ms = millis();
  g_next_at_ms = millis() + WX_FIRST_DELAY_MS;
}

void wx_poll(void) {
  uint32_t now = millis();

  if (g_wx.forced) {
    // God mode owns the state; do not fetch and do not age it out.
    if (g_phase != WXP_IDLE) {
      g_client.stop();
      g_phase = WXP_IDLE;
    }
    g_wx.age_s = 0;
    return;
  }

  if (g_have_ok) {
    uint32_t elapsed = (uint32_t)(now - g_last_ok_ms) / 1000UL;
    g_wx.age_s = elapsed;
    if (elapsed > WX_STALE_S && g_wx.valid) {
      // Stale data is worse than no data: fall back to STR_WX_NODATA_LINE.
      wx_clear_data();
    }
  } else {
    g_wx.age_s = 0;
  }

  wx_service(now);
}

const WeatherState &wx_state(void) {
  return g_wx;
}

// GAME_DESIGN 6.4: the reason two pets in the same city disagree about rain.
static void wx_temper_adjust(uint8_t group, uint8_t temperament, uint16_t *hap, int16_t *mood) {
  uint8_t v = (uint8_t)(temperament & 0x0Fu);
  bool wet = (group == (uint8_t)WX_FOG || group == (uint8_t)WX_DRIZZLE || group == (uint8_t)WX_RAIN
              || group == (uint8_t)WX_SHOWERS || group == (uint8_t)WX_STORM
              || group == (uint8_t)WX_STORM_HAIL);

  if (v >= 12) {  // GOTICO - it likes the rain
    if (group == (uint8_t)WX_CLEAR_DAY) {
      *hap = 1150;
      *mood = -6;
    } else if (wet) {
      *mood = (int16_t)(-(*mood));
      int32_t d = (int32_t)(*hap) - 1000;
      *hap = (uint16_t)(1000 + (d * 30) / 100);
    }
  } else if (v <= 3) {  // SOLAR
    if (group == (uint8_t)WX_CLEAR_DAY) {
      *hap = 750;
      *mood = 14;
    } else if (group == (uint8_t)WX_RAIN || group == (uint8_t)WX_STORM
               || group == (uint8_t)WX_STORM_HAIL) {
      *hap = (uint16_t)(((uint32_t)(*hap) * 1100UL) / 1000UL);
      *mood = (int16_t)(((int32_t)(*mood) * 125) / 100);
    }
  }
  // 4..11 (TRANQUILO / NERVIOSO): table as written.
}

uint16_t wx_mult_hap(uint8_t temperament, uint8_t bond_pct) {
  if (!wx_active()) return MULT_ONE;
  uint8_t g = g_wx.group;
  uint16_t hap = WX_TAB[g].hap;
  // "si bond > 60 el bicho saca un paraguas" (GAME_DESIGN 6.1, rain row).
  if (g == (uint8_t)WX_RAIN && bond_pct > 60) hap = 1050;
  int16_t mood = WX_TAB[g].mood;
  wx_temper_adjust(g, temperament, &hap, &mood);
  hap = (uint16_t)(((uint32_t)hap * (uint32_t)wx_temp_row().hap) / 1000UL);
  return (uint16_t)NT_CLAMP((int32_t)hap, 400, 2500);
}

uint16_t wx_mult_en(void) {
  if (!wx_active()) return MULT_ONE;
  uint32_t en = (uint32_t)WX_TAB[g_wx.group].en * (uint32_t)wx_temp_row().en / 1000UL;
  return (uint16_t)NT_CLAMP((int32_t)en, 400, 2500);
}

uint16_t wx_mult_hunger(void) {
  if (!wx_active()) return MULT_ONE;
  return (uint16_t)NT_CLAMP((int32_t)wx_temp_row().hunger, 400, 2500);
}

uint16_t wx_sick_bonus_pph(void) {
  if (!wx_active()) return 0;
  uint16_t s = (uint16_t)WX_TAB[g_wx.group].sick + (uint16_t)wx_temp_row().sick;
  return (uint16_t)NT_MIN(s, (uint16_t)200);
}

int8_t wx_mood_offset(uint8_t temperament) {
  if (!wx_active()) return 0;
  uint8_t g = g_wx.group;
  uint16_t hap = WX_TAB[g].hap;  // scratch, wx_temper_adjust writes both
  int16_t mood = WX_TAB[g].mood;
  wx_temper_adjust(g, temperament, &hap, &mood);
  mood = (int16_t)(mood + wx_temp_row().mood);
  if (g_wx.wind_dkmh >= WX_WIND_HARSH_DKMH) mood = (int16_t)(mood + WX_WIND_MOOD);
  return (int8_t)NT_CLAMP((int32_t)mood, -60, 60);
}

void wx_force(uint8_t group) {
  if (group >= (uint8_t)WX_COUNT) {
    g_wx.forced = 0;
    wx_clear_data();
    g_have_ok = false;
    g_next_at_ms = millis();  // refetch the real weather at once
    return;
  }
  g_wx.forced = 1;
  g_wx.group = group;
  g_wx.code = WX_GROUP_CODE[group];
  g_wx.is_day = (group == (uint8_t)WX_CLEAR_NIGHT) ? 0 : 1;
  g_wx.valid = (group == (uint8_t)WX_UNKNOWN) ? 0 : 1;
  g_wx.temp_dc = 180;  // 18.0 C: the neutral band, so the group is isolated
  g_wx.app_dc = 180;
  g_wx.wind_dkmh = 0;
  g_wx.precip_dmm = 0;
  g_wx.age_s = 0;
  g_wx.fetched_epoch = (uint32_t)gt_now();
}

// =============================================================================
// 9. OVERLAY
//
// Everything below is integer, allocation free and bounded. The expensive
// case is the fog checkerboard: ~1500 drawHLine calls, ~1 ms of the 50 ms
// frame budget. Coordinates handed to U8g2 are unsigned (u8g2_uint_t is
// uint16_t), so every value is proved non-negative before the call or routed
// through the clipping helpers.
// =============================================================================

#define WX_FX_Y0 ((uint8_t)SPRITE_AREA_Y)                    // 9
#define WX_FX_H ((uint8_t)SPRITE_AREA_H)                     // 47
#define WX_FX_Y1 ((uint8_t)(SPRITE_AREA_Y + SPRITE_AREA_H))  // 56

// sin(i * 22.5 deg) * 64, rounded.
static const int8_t WX_SIN16[16] = {0, 24, 45, 59, 64, 59, 45, 24, 0, -24, -45, -59, -64, -59, -45, -24};

static uint32_t g_fx_last_ms = 0;
static uint16_t g_fx_dt = 0;      // ms since the previous overlay frame, <= 250
static uint16_t g_fx_phase = 0;   // free running 0..23999 ms
static uint16_t g_fall_q4 = 0;    // vertical particle offset, 1/16 px
static uint16_t g_drift_q4 = 0;   // horizontal drift, 1/16 px
static uint32_t g_snow_ms = 0;    // toward the next accumulated pixel
static uint8_t g_snow_px = 0;     // 0..6 px of settled snow
static int16_t g_flash_on = 0;    // remaining ms of the strike
static int32_t g_flash_next = 0;  // ms until the next lightning strike
static uint8_t g_flash_edge = 0;  // set on the arming frame, cleared when read
static uint8_t g_fx_group = 0xFF;

static inline uint32_t fx_hash(uint32_t i) {
  i = i * 2654435761UL + 0x9E3779B9UL;
  i ^= i >> 15;
  i *= 0x85EBCA6BUL;
  i ^= i >> 13;
  return i;
}

// Advance a 1/16 px accumulator by px_per_s for g_fx_dt ms, wrapping at span.
static inline uint16_t fx_adv(uint16_t acc, uint16_t px_per_s, uint16_t span_px) {
  if (span_px == 0) return 0;
  uint32_t a = (uint32_t)acc + ((uint32_t)g_fx_dt * (uint32_t)px_per_s * 16UL) / 1000UL;
  return (uint16_t)(a % ((uint32_t)span_px * 16UL));
}

static void fx_box_c(U8G2 &u, int16_t x, int16_t y, int16_t w, int16_t h) {
  if (h <= 0 || w <= 0 || y < 0 || y >= OLED_H) return;
  if (y + h > OLED_H) h = OLED_H - y;
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (x >= OLED_W || w <= 0) return;
  if (x + w > OLED_W) w = OLED_W - x;
  u.drawBox((u8g2_uint_t)x, (u8g2_uint_t)y, (u8g2_uint_t)w, (u8g2_uint_t)h);
}

static void fx_tick(void) {
  uint32_t now = millis();
  uint32_t dt = (uint32_t)(now - g_fx_last_ms);
  g_fx_last_ms = now;
  if (dt > 250UL) dt = 250UL;  // after a stall, do not teleport the particles
  g_fx_dt = (uint16_t)dt;
  g_fx_phase = (uint16_t)(((uint32_t)g_fx_phase + dt) % 24000UL);
}

// --- rain / drizzle ----------------------------------------------------------
// slant_pct is the horizontal drift per vertical pixel, in percent
// (27 == 15 degrees). 0 draws vertical drops.
static void fx_rain(U8G2 &u, uint8_t count, uint16_t speed, uint8_t len, uint8_t slant_pct) {
  uint8_t span = (uint8_t)(WX_FX_H - len);
  g_fall_q4 = fx_adv(g_fall_q4, speed, span);
  uint16_t base = (uint16_t)(g_fall_q4 >> 4);
  for (uint8_t i = 0; i < count; i++) {
    uint32_t h = fx_hash(i);
    uint8_t x0 = (uint8_t)(h & 127u);
    uint8_t off = (uint8_t)((h >> 8) % span);
    uint8_t yr = (uint8_t)((base + off) % span);
    uint8_t y = (uint8_t)(WX_FX_Y0 + yr);
    int16_t x = (int16_t)x0 + (int16_t)(((int16_t)yr * (int16_t)slant_pct) / 100);
    x %= OLED_W;
    if (x < 0) x += OLED_W;
    if (slant_pct == 0) {
      u.drawVLine((u8g2_uint_t)x, y, len);
    } else {
      uint8_t x2 = (x < OLED_W - 1) ? (uint8_t)(x + 1) : (uint8_t)x;
      u.drawLine((u8g2_uint_t)x, y, (u8g2_uint_t)x2, (u8g2_uint_t)(y + len));
    }
  }
}

static void fx_puddle(U8G2 &u) {
  u.drawHLine(0, (u8g2_uint_t)(WX_FX_Y1 - 1), OLED_W);
  uint8_t r = (uint8_t)(((uint32_t)(g_fx_phase % 800u) * 9UL) / 800UL);
  static const uint8_t cx[3] = {22, 64, 103};
  for (uint8_t i = 0; i < 3; i++) {
    int16_t a = (int16_t)cx[i] - (int16_t)r;
    int16_t b = (int16_t)cx[i] + (int16_t)r;
    if (a >= 0) u.drawPixel((u8g2_uint_t)a, (u8g2_uint_t)(WX_FX_Y1 - 3));
    if (b < OLED_W) u.drawPixel((u8g2_uint_t)b, (u8g2_uint_t)(WX_FX_Y1 - 3));
  }
}

// --- snow --------------------------------------------------------------------
static void fx_snow(U8G2 &u, uint8_t count, uint16_t speed, int16_t gust) {
  uint8_t span = (uint8_t)(WX_FX_H - 1);
  g_fall_q4 = fx_adv(g_fall_q4, speed, span);
  uint16_t base = (uint16_t)(g_fall_q4 >> 4);
  for (uint8_t i = 0; i < count; i++) {
    uint32_t h = fx_hash(i + 100u);
    uint8_t x0 = (uint8_t)(h & 127u);
    uint8_t off = (uint8_t)((h >> 8) % span);
    uint8_t yr = (uint8_t)((base + off) % span);
    // Sinusoidal sway, amplitude 6 px, 0.4 Hz (2500 ms / 16 steps = 156 ms).
    uint8_t ph = (uint8_t)((((uint32_t)g_fx_phase / 156UL) + i) & 15u);
    int16_t dx = (int16_t)(((int16_t)WX_SIN16[ph] * 6) / 64);
    int16_t x = (int16_t)x0 + dx + gust;
    x %= OLED_W;
    if (x < 0) x += OLED_W;
    u.drawPixel((u8g2_uint_t)x, (u8g2_uint_t)(WX_FX_Y0 + yr));
  }
}

static void fx_snow_ground(U8G2 &u) {
  g_snow_ms += g_fx_dt;
  if (g_snow_ms >= 60000UL) {
    g_snow_ms -= 60000UL;
    if (g_snow_px < 6) g_snow_px++;
  }
  if (g_snow_px) u.drawBox(0, (u8g2_uint_t)(WX_FX_Y1 - g_snow_px), OLED_W, g_snow_px);
}

// --- clear sky ---------------------------------------------------------------
static void fx_sun(U8G2 &u) {
  const int16_t cx = 108;
  const int16_t cy = 24;
  u.drawDisc((u8g2_uint_t)cx, (u8g2_uint_t)cy, 5);
  uint8_t rot = (uint8_t)(((uint32_t)g_fx_phase / 500UL) & 1u);  // one step / 500 ms
  for (uint8_t i = 0; i < 8; i++) {
    uint8_t d = (uint8_t)((i * 2u + rot) & 15u);
    int16_t sy = WX_SIN16[d];
    int16_t sx = WX_SIN16[(d + 4u) & 15u];
    int16_t x1 = cx + (sx * 8) / 64;
    int16_t y1 = cy + (sy * 8) / 64;
    int16_t x2 = cx + (sx * 12) / 64;
    int16_t y2 = cy + (sy * 12) / 64;
    u.drawLine((u8g2_uint_t)x1, (u8g2_uint_t)y1, (u8g2_uint_t)x2, (u8g2_uint_t)y2);
  }
}

static void fx_glare(U8G2 &u) {
  // A 22 px diagonal band of 12.5 % dither sweeping the screen every 6 s.
  int16_t b = (int16_t)(((uint32_t)(g_fx_phase % 6000u) * 220UL) / 6000UL) - 40;
  for (uint8_t y = WX_FX_Y0; y < WX_FX_Y1; y++) {
    int16_t xs = b - (int16_t)y;
    for (int16_t k = 0; k < 22; k++) {
      int16_t x = xs + k;
      if (x < 0 || x >= OLED_W) continue;
      if (((((uint16_t)x * 5u) + ((uint16_t)y * 3u)) & 7u) == 0u) {
        u.drawPixel((u8g2_uint_t)x, y);
      }
    }
  }
}

static void fx_sparkle(U8G2 &u) {
  u.setDrawColor(2);
  for (uint8_t i = 0; i < 3; i++) {
    uint32_t h = fx_hash(i + 400u + ((uint32_t)g_fx_phase / 250UL));
    uint8_t x = (uint8_t)(40u + (h & 47u));
    uint8_t y = (uint8_t)(WX_FX_Y0 + 8u + ((h >> 8) % 28u));
    u.drawPixel(x, y);
  }
  u.setDrawColor(1);
}

static void fx_night(U8G2 &u) {
  for (uint8_t i = 0; i < 12; i++) {
    uint32_t h = fx_hash(i + 300u);
    // The last three twinkle at roughly 2 Hz.
    if (i >= 9 && ((((uint32_t)g_fx_phase / 250UL) + i) & 1u)) continue;
    u.drawPixel((u8g2_uint_t)(h & 127u), (u8g2_uint_t)(WX_FX_Y0 + ((h >> 8) % 26u)));
  }
  u.setDrawColor(1);
  u.drawDisc(108, 22, 7);
  u.setDrawColor(0);
  u.drawDisc(104, 20, 7);  // carve the crescent
  u.setDrawColor(1);
}

// --- clouds ------------------------------------------------------------------
static void fx_cloud(U8G2 &u, int16_t x, int16_t y) {
  fx_box_c(u, x + 6, y, 10, 3);
  fx_box_c(u, x + 2, y + 3, 18, 3);
  fx_box_c(u, x, y + 6, 22, 2);
}

static void fx_clouds(U8G2 &u, uint8_t n, uint16_t speed) {
  g_drift_q4 = fx_adv(g_drift_q4, speed, 176);
  int16_t d = (int16_t)(g_drift_q4 >> 4);
  for (uint8_t i = 0; i < n; i++) {
    int16_t x = (int16_t)(((uint16_t)d + (uint16_t)i * 70u) % 176u) - 24;
    fx_cloud(u, x, (int16_t)(WX_FX_Y0 + 1 + i * 9));
  }
}

static void fx_cloud_band(U8G2 &u) {
  // 10 px overcast band, 25 % dither, drifting at 4 px/s.
  g_drift_q4 = fx_adv(g_drift_q4, 4, OLED_W);
  uint8_t d = (uint8_t)(g_drift_q4 >> 4);
  for (uint8_t y = WX_FX_Y0; y < (uint8_t)(WX_FX_Y0 + 10); y++) {
    for (uint8_t x = (uint8_t)(((uint16_t)y * 2u + d) & 3u); x < OLED_W; x = (uint8_t)(x + 4)) {
      u.drawPixel(x, y);
    }
  }
}

static void fx_fog(U8G2 &u) {
  // 50 % coverage as 2-on / 2-off diagonal runs, XORed over the sprite area
  // and sweeping sideways at 2 px/s: the pet stays barely legible.
  g_drift_q4 = fx_adv(g_drift_q4, 2, OLED_W);
  uint8_t d = (uint8_t)(g_drift_q4 >> 4);
  u.setDrawColor(2);
  for (uint8_t y = WX_FX_Y0; y < WX_FX_Y1; y++) {
    uint8_t x0 = (uint8_t)((4u - (((uint16_t)y + d) & 3u)) & 3u);
    for (uint8_t x = x0; x < OLED_W; x = (uint8_t)(x + 4)) {
      u.drawHLine(x, y, (x <= (uint8_t)(OLED_W - 2)) ? 2 : 1);
    }
  }
  u.setDrawColor(1);
}

// --- storm -------------------------------------------------------------------
// The strike no longer paints anything. It used to XOR the whole 1024 B
// framebuffer for 60 ms; the panel can do the same inversion with one 0xA7
// command byte, and the UI arms it through rd_flash() the moment it sees the
// edge below. Two consequences worth knowing:
//   - the inversion is now frame-rate independent (it is a register, not a
//     redraw), which is why the software timer is only a scheduler here,
//   - it MUST NOT be done in both places. Two inversions cancel out and the
//     storm goes back to having no lightning at all.
static void fx_lightning(U8G2 &u) {
  (void)u;
  if (g_flash_on > 0) {
    g_flash_on = (g_flash_on > (int16_t)g_fx_dt) ? (int16_t)(g_flash_on - (int16_t)g_fx_dt) : 0;
    return;
  }
  g_flash_next -= (int32_t)g_fx_dt;
  if (g_flash_next <= 0) {
    g_flash_on   = 60;
    g_flash_edge = 1;                                 // one frame only
    g_flash_next = 4000L + (int32_t)random(0, 5000);  // every 4-9 s
  }
}

// True EXACTLY once, on the frame a bolt is generated. Consuming it clears it,
// so a caller that stops asking simply misses strikes rather than piling them
// up. Only wx_draw_particles() generates bolts, so this is silent whenever the
// overlay is not being drawn - which is the correct behaviour: no overlay, no
// storm on screen, no reason to shake the panel. Read it AFTER the particles,
// in the same frame; draw_home() does exactly that.
bool wx_lightning_edge(void) {
  const bool e = (g_flash_edge != 0);
  g_flash_edge = 0;
  return e;
}

static void fx_hail(U8G2 &u) {
  for (uint8_t i = 0; i < 6; i++) {
    uint32_t h = fx_hash(i + 200u);
    uint8_t x = (uint8_t)(h & 123u);
    uint16_t per = (uint16_t)(700u + ((h >> 8) % 500u));
    uint16_t t = (uint16_t)(((uint32_t)g_fx_phase + ((h >> 16) & 0xFFFFu)) % per);
    uint16_t half = (uint16_t)(per / 2u);
    uint16_t up = (t < half) ? t : (uint16_t)(per - t);
    int16_t hgt = (int16_t)(((uint32_t)up * (uint32_t)(WX_FX_H - 6)) / (uint32_t)half);
    fx_box_c(u, x, (int16_t)(WX_FX_Y1 - 2 - hgt), 2, 2);
  }
}

static void fx_wind(U8G2 &u) {
  uint8_t d = (uint8_t)(((uint32_t)g_fx_phase / 16UL) % 32UL);
  for (uint8_t i = 0; i < 4; i++) {
    int16_t y = (int16_t)(WX_FX_Y0 + 5 + i * 11);
    for (int16_t x = (int16_t)d - 32 + (int16_t)(i * 8); x < OLED_W; x += 32) {
      fx_box_c(u, x, y, 9, 1);
    }
  }
}

// -----------------------------------------------------------------------------
//  THE SCENE-ORDER SPLIT  (weather.h documents the contract; this is the why)
//
//  Everything below that ERASES lives in the backdrop and everything that only
//  ADDS lives in the particles, because the body is drawn between the two.
//  Three of these effects erase and they were all running AFTER the body:
//    * fx_night()   - a colour-0 drawDisc(104, 20, 7) carves the crescent, and
//                     with it a 15 px disc out of any body in columns 97..111.
//    * fx_fog()     - colour 2 is XOR, over the WHOLE sprite area.
//    * fx_sparkle() - colour 2 again, three pixels, right where the pet stands.
//  fx_glare() only ORs, but it belongs to the same clear-day scenery, so it
//  goes with the sun rather than being split off for no reason.
//
//  fx_tick() and the group-change reset run in the backdrop only: it is the
//  half that is guaranteed to be called first, once per frame.
// -----------------------------------------------------------------------------
void wx_draw_backdrop(U8G2 &u) {
  fx_tick();

  if (!wx_active()) {
    u.setDrawColor(1);
    return;
  }

  uint8_t g = g_wx.group;
  if (g != g_fx_group) {
    g_fx_group = g;
    g_fall_q4 = 0;
    g_drift_q4 = 0;
    g_snow_px = 0;
    g_snow_ms = 0;
    g_flash_on = 0;
    g_flash_edge = 0;
    g_flash_next = 3000;
  }

  switch (g) {
    case WX_CLEAR_DAY:
      fx_sun(u);
      fx_glare(u);
      fx_sparkle(u);
      break;

    case WX_CLEAR_NIGHT:
      fx_night(u);
      break;

    case WX_PARTLY:
      fx_clouds(u, 2, 3);
      break;

    case WX_CLOUDY:
      fx_cloud_band(u);
      break;

    case WX_FOG:
      fx_fog(u);
      break;

    case WX_RAIN:
    case WX_SHOWERS:
    case WX_STORM:
    case WX_STORM_HAIL:
      fx_puddle(u);
      break;

    case WX_SNOW:
    case WX_SNOW_SHOWERS:
      fx_snow_ground(u);
      break;

    default:
      break;
  }

  u.setDrawColor(1);
}

void wx_draw_particles(U8G2 &u) {
  // No fx_tick() here on purpose: g_fx_dt and g_fx_phase were advanced by
  // wx_draw_backdrop() earlier in this same frame, and ticking twice would
  // double every particle's speed.
  if (!wx_active()) {
    u.setDrawColor(1);
    return;
  }

  switch (g_wx.group) {
    case WX_DRIZZLE:
      fx_rain(u, 8, 20, 2, 0);
      break;

    case WX_RAIN:
      fx_rain(u, 24, 45, 3, 27);
      break;

    case WX_SNOW:
      fx_snow(u, 18, 12, 0);
      break;

    case WX_SHOWERS:
      // 4 s of rain, 8 s of respite: the pet leans out during the gaps.
      if ((g_fx_phase % 12000u) < 4000u) {
        fx_rain(u, 24, 45, 3, 27);
      } else {
        g_fall_q4 = fx_adv(g_fall_q4, 45, (uint16_t)(WX_FX_H - 3));
      }
      break;

    case WX_SNOW_SHOWERS: {
      // Lateral gusts, +/- 10 px over a 12 s cycle.
      uint8_t ph = (uint8_t)(((uint32_t)g_fx_phase / 750UL) & 15u);
      int16_t gust = (int16_t)(((int16_t)WX_SIN16[ph] * 10) / 64);
      fx_snow(u, 18, 14, gust);
      break;
    }

    case WX_STORM:
      fx_rain(u, 32, 60, 3, 30);
      fx_lightning(u);
      break;

    case WX_STORM_HAIL:
      fx_rain(u, 32, 60, 3, 30);
      fx_hail(u);
      fx_lightning(u);
      break;

    default:
      break;
  }

  if (g_wx.wind_dkmh >= WX_WIND_HARSH_DKMH) fx_wind(u);

  u.setDrawColor(1);
}

void wx_draw_overlay(U8G2 &u) {
  wx_draw_backdrop(u);
  wx_draw_particles(u);
}
