// =============================================================================
//  webui.cpp - the creator server shell.
//
//  The Nottamagochi phone app and every route it consumed are gone. What is
//  left is the part Phase 8 builds the creator API on:
//
//   1. BIND POLICY. The listening socket only opens once net_phase() reports
//      NPH_STA_UP or NPH_AP_PORTAL, and only while CF_WEB_ENABLED is set.
//      Binding earlier aborts the firmware inside FreeRTOS (see web_begin()).
//   2. ROUTE REGISTRATION EXACTLY ONCE for the lifetime of the firmware.
//   3. The token-bucket rate limiter in front of every handler.
//   4. The captive-portal catch-all.
//
//  BANNED HERE, ON PURPOSE:
//    - the 3-arg send_P  (WebServer.cpp:619 strlen_P()s the blob)
//    - the 1-arg sendContent_P (WebServer.cpp:663, same bug)
//    - String concatenation in a handler: every body is built into a static
//      char buffer whose worst case is provable at compile time.
//
//  All identifiers and comments English; the placeholder page lives in
//  index_html.h, which this is the ONLY translation unit to include.
// =============================================================================

#include <Arduino.h>
#include <WebServer.h>

#include <string.h>
#include <stdio.h>

#include "config.h"
#include "nt_types.h"
#include "webui.h"
#include "sprites.h"
#include "render.h"
#include "net.h"
#include "rng.h"
#include "index_html.h"   // EXACTLY ONE TU. See the file header.

#if FEATURE_WEB

// =============================================================================
//  1. STATE
// =============================================================================

static WebServer s_srv(WEB_PORT);

static bool     s_running        = false;
static bool     s_routes_done    = false;
static uint16_t s_port           = WEB_PORT;
// The port web_begin() was last asked for. S9 can switch web access off and
// back on at any time and the entry point calls web_begin() exactly once, so
// web_service() needs to know what to re-open on the rising edge.
static uint16_t s_want_port      = WEB_PORT;

static uint16_t s_pin            = 0xFFFFu;   // 0xFFFF == not rolled yet

// Token bucket, held in MILLI-tokens so the refill never needs a float and
// never truncates to zero on a fast poll. Capacity WEB_RATE_TOKENS * 1000
// (10,000) fits uint16_t with room to spare - see the static_assert in webui.h.
static uint16_t s_bucket_mtok    = (uint16_t)(WEB_RATE_TOKENS * 1000);
static uint32_t s_bucket_ms      = 0;

// Borrowed from the entry point; null in a standalone build.
static Config*  s_cfg            = nullptr;

// Response scratch: static char[320], never String +=.
static char     s_json[WEB_JSON_BUF];

// =============================================================================
//  2. CONFIG ACCESS
// =============================================================================

void web_bind_config(Config* cfg)
{
  s_cfg = cfg;
}

// Does the user still want the HTTP server? CF_WEB_ENABLED is the S9 "WEB"
// toggle. It used to feed only the entry point's radio policy, so whenever
// anything else kept the radio up the listening socket survived the toggle and
// the server went on answering after the user had switched web access off.
//
// An UNBOUND module (no entry point) has no user preference to consult and must
// keep working exactly as before.
static bool web_enabled(void)
{
  return (s_cfg == nullptr) || ((s_cfg->flags & CF_WEB_ENABLED) != 0);
}

// =============================================================================
//  3. RATE LIMITER
//    10 tokens, refill 4/s, 1 per read, 2 per mutation, 429 with an EMPTY body
//    when the bucket is dry. The empty body is deliberate: it says "you are
//    hammering me" and carries no state a client could act on.
// =============================================================================
static bool rate_take(uint8_t cost_tokens)
{
  const uint32_t now = millis();
  uint32_t dt = now - s_bucket_ms;          // unsigned: wrap-safe
  if (dt > 5000u) dt = 5000u;               // also caps the multiply below
  if (dt) {
    s_bucket_ms = now;
    uint32_t t = (uint32_t)s_bucket_mtok + dt * (uint32_t)WEB_RATE_REFILL_PER_S;
    const uint32_t cap = (uint32_t)WEB_RATE_TOKENS * 1000u;
    s_bucket_mtok = (uint16_t)((t > cap) ? cap : t);
  }
  const uint16_t need = (uint16_t)(cost_tokens * 1000u);
  if (s_bucket_mtok < need) return false;
  s_bucket_mtok = (uint16_t)(s_bucket_mtok - need);
  return true;
}

// =============================================================================
//  4. RESPONSE PRIMITIVES
// =============================================================================

static void note_request(void)
{
  rd_note_web_activity();                   // render drops to FPS_LOW
}

static void send_json(int code, const char* body)
{
  s_srv.sendHeader(F("Cache-Control"), F("no-store"));
  s_srv.send(code, "application/json", body);
}

// {"err":"xxx"}
static void send_err(int code, const char* err)
{
  snprintf(s_json, sizeof(s_json), "{\"err\":\"%s\"}", err);
  send_json(code, s_json);
}

// 429, empty body. Rate limiter only.
static void send_throttled(void)
{
  s_srv.send(429, "application/json", "");
}

// =============================================================================
//  5. SPRITE / MOOD POLICY  (shared with ui so both panels agree)
// =============================================================================

uint8_t web_pose_of(const PetSave& p)
{
  if (p.stage >= STAGE_DEAD)   return POSE_GHOST;
  if (p.flags & PF_ASLEEP)     return POSE_SLEEP;
  if (p.flags & PF_SICK)       return POSE_SICK;
  return POSE_IDLE;
}

uint8_t web_mood_index(uint8_t score)
{
  if (score <= 15) return MOOD_MISERIA;
  if (score <= 35) return MOOD_TRISTE;
  if (score <= 55) return MOOD_NEUTRO;
  if (score <= 75) return MOOD_CONTENTO;
  if (score <= 90) return MOOD_FELIZ;
  return MOOD_EUFORICO;
}

// =============================================================================
//  6. HANDLERS
// =============================================================================

// ---- GET / ------------------------------------------------------------------
static void h_root(void)
{
  note_request();
  if (!rate_take(WEB_COST_READ)) { send_throttled(); return; }
  s_srv.sendHeader(F("Cache-Control"), F("no-cache"));
  // FOUR-arg send_P. The 3-arg form strlen_P()s the blob (WebServer.cpp:619).
  s_srv.send_P(200, PSTR("text/html; charset=utf-8"), INDEX_HTML, INDEX_HTML_LEN);
}

// ---- catch-all / captive portal --------------------------------------------
//
//  net.cpp owns the DNSServer (net.h:5-7); this is only the HTTP half. Android
//  and iOS probe a known URL and read the status: a 302 to our own root is what
//  makes the "sign in to network" sheet appear. When the Host header already IS
//  us, the request is a genuine 404.
// ----------------------------------------------------------------------------
static void h_notfound(void)
{
  note_request();
  if (!rate_take(WEB_COST_READ)) { send_throttled(); return; }

  const char* ip = net_ip();
  const String host = s_srv.hostHeader();

  // "0.0.0.0" means the radio has no address yet; redirecting there would send
  // the phone nowhere, so treat every request as genuinely ours and 404.
  const bool have_ip = ip && ip[0] && strcmp(ip, "0.0.0.0") != 0;

  const bool addressed_to_us =
      !have_ip || host.length() == 0 || host.startsWith(ip);

  if (!addressed_to_us) {
    char loc[40];
    snprintf(loc, sizeof(loc), "http://%s/", ip);
    s_srv.sendHeader(F("Location"), loc, true);
    s_srv.sendHeader(F("Cache-Control"), F("no-store"));
    s_srv.send(302, "text/plain", "");
    return;
  }
  send_err(404, "arg");
}

// =============================================================================
//  7. LIFECYCLE
// =============================================================================

uint16_t web_pin(void)
{
  if (s_pin == 0xFFFFu) s_pin = (uint16_t)rng_below(RNG_MISC, (uint32_t)WEB_PIN_MAX);
  return s_pin;
}

bool     web_running(void)           { return s_running; }
uint16_t web_port(void)              { return s_port; }

bool web_begin(uint16_t port)
{
  s_want_port = port;

  (void)web_pin();                   // roll it once, keep it across restarts

  if (!s_routes_done) {
    s_routes_done = true;

    s_bucket_mtok = (uint16_t)(WEB_RATE_TOKENS * 1000);
    s_bucket_ms   = millis();

    // Registered exactly once for the lifetime of the firmware:
    // WebServer::on() appends to a linked list it never prunes, so a
    // stop()/begin() cycle that re-registered would leak a RequestHandler and
    // a std::function per cycle for no behavioural change (the first match
    // wins).
    s_srv.on("/", HTTP_GET, h_root);
    s_srv.onNotFound(h_notfound);

    // WebServer.h:288 defaults _nullDelay = true, which burns 1 ms inside
    // every idle handleClient() - 20 % of a 20 fps frame budget, for nothing.
    s_srv.enableDelay(false);
  }

  // Routes stay registered either way (they are registered exactly once for the
  // lifetime of the firmware), but the socket only opens when the user wants
  // it. web_service() re-checks every pump, so a later S9 toggle is honoured
  // without the entry point calling us again.
  if (!web_enabled()) {
    if (s_running) web_stop();
    return true;
  }

  if (s_running && port == s_port) return true;

  // The listening socket cannot exist before lwIP does. At boot the radio is
  // RADIO_OFF, so NetworkServer::begin() -> lwip_socket() -> netconn_apimsg()
  // -> tcpip_send_msg_wait_sem() -> sys_mutex_lock() takes a mutex that is
  // still NULL, and FreeRTOS aborts the whole firmware with
  //     assert failed: xQueueSemaphoreTake queue.c:1709 (( pxQueue ))
  // Only NPH_STA_UP and NPH_AP_PORTAL guarantee a netif with an address.
  // web_service() re-enters web_begin() on every pump while the port is
  // wanted but closed, so the socket opens by itself the moment the radio
  // comes up - no other module has to remember to call us back.
  {
    const NetPhase ph = net_phase();
    if (ph != NPH_STA_UP && ph != NPH_AP_PORTAL) return true;
  }

  s_srv.begin(port);
  s_port    = port;
  s_running = true;
  return true;
}

void web_service(void)
{
  // CF_WEB_ENABLED is a LIVE switch. Falling edge -> drop the listening socket;
  // rising edge -> re-open on the port web_begin() was originally given. Both
  // are cheap: the check is one flag test per loop() pass and neither edge
  // fires twice.
  const bool want = web_enabled();
  if (want) {
    if (!s_running && s_routes_done) (void)web_begin(s_want_port);
  } else {
    if (s_running) web_stop();
  }

  if (!s_running) return;

  s_srv.handleClient();
}

void web_stop(void)
{
  if (!s_running) return;
  s_srv.close();
  s_running = false;
}

#else  // !FEATURE_WEB ========================================================

bool     web_begin(uint16_t)          { return false; }
void     web_service(void)            {}
void     web_stop(void)               {}
bool     web_running(void)            { return false; }
uint16_t web_port(void)               { return 0; }
uint16_t web_pin(void)                { return 0; }
void     web_bind_config(Config*)     {}
uint8_t  web_pose_of(const PetSave&)  { return POSE_IDLE; }
uint8_t  web_mood_index(uint8_t)      { return MOOD_NEUTRO; }

#endif // FEATURE_WEB
