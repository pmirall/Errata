// =============================================================================
//  webui.cpp - the creator server shell.
//
//  The Nottamagochi phone app and every route it consumed are gone. What is
//  left is the part Phase 8 builds the creator API on:
//
//   1. BIND POLICY. The listening socket only opens once net_phase() reports
//      NPH_AP_PORTAL, and only while CF_WEB_ENABLED is set. Binding earlier
//      aborts the firmware inside FreeRTOS (see web_begin()). NPH_STA_UP was
//      the other legal phase until P5-C1 deleted the station: the creator page
//      is served over the device's OWN access point and nothing else.
//   2. ROUTE REGISTRATION EXACTLY ONCE for the lifetime of the firmware.
//   3. The token-bucket rate limiter in front of every handler.
//   4. The captive-portal catch-all.
//
//  P8-C1/C2 ADD THE PIN AND THE PORTAL SESSION, and split them the way this
//  tree splits everything a host binary must be able to drive:
//    - THE RULES are networking/creator_gate.{h,cpp}: pure, caller-owned, no
//      Arduino, driven case by case in tests/test_creator_gate.cpp. Which
//      clock each deadline uses, and why, is argued in that header.
//    - THE PERSISTED HALF is persistence/game_state.h's gs_creator_load /
//      gs_creator_store, driven in tests/test_game_state.cpp against the real
//      save_manager.
//    - WHAT IS LEFT HERE is transport: read a header, answer a status code,
//      ask flash to remember an edge. Nothing in this file decides a rule.
//
//  EVERYTHING ARRIVING OVER HTTP IS HOSTILE UNTIL IT IS VALIDATED ON THIS
//  DEVICE. The page's own checks are a courtesy to the user and never a
//  control: nothing here can tell a request from our page apart from a request
//  from curl, so it assumes curl.
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

#include "../core/config.h"
#include "../core/nt_types.h"
#include "webui.h"
#include "../data/sprites.h"
#include "../ui/render.h"
#include "net.h"
#include "creator_gate.h"                 // the PIN and idle RULES, pure
#include "../core/rng.h"
#include "../hardware/gametime.h"         // gt_mono32() - see creator_gate.h
#include "../persistence/game_state.h"    // gs_creator_load / gs_creator_store
#include "../data/index_html.h"   // EXACTLY ONE TU. See the file header.

#if FEATURE_WEB

// =============================================================================
//  1. STATE
// =============================================================================

static WebServer s_srv(WEB_PORT);

static bool     s_running        = false;
static bool     s_routes_done    = false;
static uint16_t s_port           = WEB_PORT;
// The port web_begin() was last asked for. SETTINGS can switch web access off and
// back on at any time and the entry point calls web_begin() exactly once, so
// web_service() needs to know what to re-open on the rising edge.
static uint16_t s_want_port      = WEB_PORT;

// The live gate: the issued PIN, the failure counter, the lockout deadline and
// the idle clock. 16 B; every rule that reads or writes it is in
// networking/creator_gate.cpp, which a host binary compiles.
static CreatorGate s_gate = { 0, 0, 0, 0, 0, 0, 0, 0 };

// The header keys WebServer is asked to keep. IT DROPS EVERY OTHER HEADER:
// collectAllHeaders() pre-registers only Authorization and If-None-Match, and
// Parsing.cpp discards anything not registered - so header("X-Pin") is the
// empty string on every request until this array is handed to collectHeaders(),
// and the gate would refuse the correct PIN forever. collectAllHeaders() is the
// wrong fix: it allocates a String pair per unknown header, which is unbounded
// heap growth from a hostile header list.
static const char* const HDR_KEYS[] = { "X-Pin" };

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

// Does the user still want the HTTP server? CF_WEB_ENABLED is the SETTINGS "WEB"
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
//  5. SPRITE / MOOD POLICY is ui/pet_view.cpp's since P2-C11c. Nothing in this
//     file consumed it; ui.cpp was the only caller and the rule belongs to the
//     render layer, not to the phone mirror (audit risk 9).
// =============================================================================

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
//  7. THE PIN GATE
//    The rules are creator_gate.cpp's; this is the transport around them.
// =============================================================================

// Persist the ARMED EDGE of the failure counter, and nothing else.
//
// gs_creator_store() is a no-op when all three fields already match, so a
// steady stream of wrong PINs writes flash exactly ONCE (the fifth failure) and
// a success writes it once more (back to 0). Persisting every attempt would
// hand an unauthenticated client one flash write per guess; creator_gate.h
// (cg_persist_fails) argues the trade in full.
//
// pin_lock_until is written on the SAME edge, in wall-clock seconds, purely so
// the DIAG screen has something to show. NOTHING READS IT BACK: gt_now() is an
// estimate on an uncalibrated device and is settable by the phone from P8-C3,
// and a deadline the far end can move is not a deadline.
static void pin_persist_edge(void)
{
  uint16_t pin = 0, idle = 0;
  uint8_t  had = 0;
  gs_creator_load(pin, had, idle);
  const uint8_t want = cg_persist_fails(s_gate);
  if (had == want) return;                       // no edge, no write
  const uint32_t mirror =
      want ? (uint32_t)(gt_now() + (uint32_t)(CREATOR_PIN_LOCK_MS / 1000u)) : 0u;
  (void)gs_creator_store(s_gate.pin, want, mirror);
}

// The gate every mutating route stands behind. Answers the client itself on
// refusal, so a caller is one `if`.
//
// THE PIN ARRIVES IN AN X-Pin HEADER. It used to be a query argument `?k=`
// (git show aa2b7ee:Pebblebol/webui.cpp), which put the secret in the URL and
// therefore in the browser's history and in any Referer the page sends. The
// same cg_verify() takes a plain string, so P8-C3's JSON tokenizer can hand it
// a `"pin"` field from a request body without a second gate existing.
static bool pin_ok(void)
{
  const String supplied = s_srv.header(F("X-Pin"));
  const uint32_t now = gt_mono32();

  switch (cg_verify(s_gate, supplied.c_str(), now)) {
    case CG_OK:
      pin_persist_edge();
      return true;

    case CG_LOCKED: {
      // Say HOW LONG, not how close the guess was. The seconds left are not a
      // secret - the client can measure them - and telling the owner who
      // mistyped is worth more than withholding them from an attacker who
      // already knows.
      const uint32_t left_s = (cg_lock_left_ms(s_gate, now) + 999u) / 1000u;
      snprintf(s_json, sizeof(s_json), "{\"err\":\"locked\",\"s\":%lu}",
               (unsigned long)left_s);
      send_json(403, s_json);
      return false;
    }

    case CG_NO_PIN:
      // No PIN issued, or the portal is not open. Distinguished from a wrong
      // PIN because it is not a guess: there is nothing to guess yet.
      send_err(403, "nopin");
      return false;

    default:
      pin_persist_edge();
      // A malformed PIN and a wrong PIN are ONE answer, for the reason a login
      // form does not say which half was wrong.
      send_err(403, "pin");
      return false;
  }
}

// ---- POST /api/ping ---------------------------------------------------------
//
//  The keep-alive, and spec section 38 names it. It is here in P8-C2 rather
//  than with the other six routes because it is the LIFECYCLE's own endpoint:
//  it is what an authorised page uses to say "I am still here", and cg_verify()
//  moving last_seen_ms on success is the whole idle timer. It is also what
//  gives the PIN gate a caller in the shipping firmware in this chunk instead
//  of leaving pin_ok() as a dead export nobody executes.
static void h_ping(void)
{
  note_request();
  if (!rate_take(WEB_COST_READ)) { send_throttled(); return; }
  // HOSTILE UNTIL PROVEN: a ping carries no body, so a declared one is a
  // malformed request and not something to parse. Refused BEFORE the PIN gate
  // because it costs nothing to answer and the body has already been drained
  // by the raw hook below.
  if (s_srv.clientContentLength() != 0) { send_err(400, "body"); return; }
  if (!pin_ok()) return;                      // pin_ok() has already answered
  send_json(200, "{\"ok\":1}");
}

// The raw/upload hook for the one POST route this chunk registers.
//
// ITS ONLY JOB IS TO EXIST. WebServer::canRaw() (detail/RequestHandlersImpl.h)
// is satisfied by a non-null fourth argument on a non-GET route, and that is
// what routes a request body through the core's fixed HTTP_RAW_BUFLEN buffer
// instead of Parsing.cpp's readBytesWithTimeout(), whose only bound on a
// malloc/realloc growth loop is the attacker's own Content-Length header. Every
// POST route this server ever registers must pass one of these; P8-C3's version
// also counts bytes and answers 413.
//
// IT MUST NEVER TOUCH server.raw(). The SAME function is invoked as the UPLOAD
// hook when the body is multipart (Parsing.cpp's _parseForm path), and there
// _currentRaw is null while WebServer::raw() dereferences it with no check - so
// a POST with a multipart Content-Type would be a null dereference, i.e. a
// crash any client can ask for.
static void h_body_discard(void)
{
}

// =============================================================================
//  8. LIFECYCLE
// =============================================================================

uint16_t web_pin(void)
{
  return s_gate.pin;                 // 0 = none issued. A pure read; see webui.h.
}

void web_portal_open(void)
{
  uint16_t pin = 0, idle_s = 0;
  uint8_t  fails = 0;
  gs_creator_load(pin, fails, idle_s);

  if (pin == 0u || pin >= (uint16_t)WEB_PIN_MAX) {
    // FIRST CREATOR ENTRY. rng_below() draws from RNG_MISC, the stream
    // core/rng.h documents for "tokens, nonces, PINs, canaries"; the one
    // esp_random() of the firmware seeded it in app.cpp. The range handed in is
    // WEB_PIN_MAX-1 so cg_mint_pin()'s +1 is exact rather than biased, and the
    // result is 1..9999 - never the "no PIN issued" sentinel.
    pin   = cg_mint_pin(rng_below(RNG_MISC, (uint32_t)WEB_PIN_MAX - 1u));
    fails = 0u;
    // Persisted BEFORE it is shown. A PIN on the user's phone that the device
    // has forgotten is worse than no PIN at all.
    (void)gs_creator_store(pin, 0u, 0u);
  }

  cg_open(s_gate, pin, fails, idle_s, gt_mono32());
}

void web_portal_close(void)
{
  cg_close(s_gate);
  web_stop();                        // the socket; the radio is ui.cpp's half
}

bool web_portal_idle_expired(void)
{
  return cg_idle_expired(s_gate, gt_mono32());
}

bool     web_running(void)           { return s_running; }
uint16_t web_port(void)              { return s_port; }

bool web_begin(uint16_t port)
{
  s_want_port = port;

  // NO PIN IS ROLLED HERE ANY MORE. web_begin() runs once from app_setup(),
  // long before the user has asked for the creator, and a PIN minted at boot is
  // a PIN written to flash on every fresh device whether or not it is ever
  // wanted. web_portal_open() mints on FIRST CREATOR ENTRY instead (spec 34).

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
    // THE FOUR-ARG on(). The fourth argument is what makes canRaw() true and
    // keeps a hostile Content-Length off the heap - see h_body_discard().
    s_srv.on("/api/ping", HTTP_POST, h_ping, h_body_discard);
    s_srv.onNotFound(h_notfound);

    // Without this the PIN header is invisible: WebServer keeps only the keys
    // it was asked for. Order does not matter - begin() re-initialises the list
    // only while the count is still zero.
    s_srv.collectHeaders((const char**)HDR_KEYS,
                         sizeof(HDR_KEYS) / sizeof(HDR_KEYS[0]));

    // WebServer.h:288 defaults _nullDelay = true, which burns 1 ms inside
    // every idle handleClient() - 20 % of a 20 fps frame budget, for nothing.
    s_srv.enableDelay(false);
  }

  // Routes stay registered either way (they are registered exactly once for the
  // lifetime of the firmware), but the socket only opens when the user wants
  // it. web_service() re-checks every pump, so a later SETTINGS toggle is honoured
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
  // Only NPH_AP_PORTAL guarantees a netif with an address. NPH_SCANNING does
  // NOT: a scan enables the station interface without associating, so there is
  // no address to bind to and the creator page has nothing to be served over.
  // web_service() re-enters web_begin() on every pump while the port is
  // wanted but closed, so the socket opens by itself the moment the radio
  // comes up - no other module has to remember to call us back.
  if (net_phase() != NPH_AP_PORTAL) return true;

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
void     web_portal_open(void)        {}
void     web_portal_close(void)       {}
// FALSE, NOT TRUE, and the difference is a screen the user cannot leave. With
// no server there is no portal to time out, so this timer never fires; what
// gets the user back out of CREATOR in this build is the screen's own
// CREATOR_AP_WAIT_MS, because no access point ever appears either.
bool     web_portal_idle_expired(void) { return false; }

#endif // FEATURE_WEB
