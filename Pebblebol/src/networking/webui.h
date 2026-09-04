// =============================================================================
//  webui.h - Pebblebol creator server (HTTP).
//
//  WHAT IS LEFT AND WHY
//    The Nottamagochi phone app is gone: no /api/state, no /api/sprites, no
//    /api/action, no browser minigames, no /api/cfg. What survives is the
//    server SHELL - bind policy, PIN, rate limiter, the root page and the
//    captive-portal catch-all - because Phase 8 grows the creator API back on
//    top of exactly this shell (spec S38: bind only when needed, stop when
//    inactive, minimal endpoints, validate everything).
//
//  ROUTE TABLE
//    GET  /            index_html.h, served with the 4-arg send_P, no-cache
//    *    anything else  captive-portal 302 to http://<ip>/, else 404
//
//  LAYERING
//    webui is integration-layer code: it may include WebServer.h, render.h,
//    net.h and nt_types.h at the same time. Nothing below it may include
//    webui.h expecting network types - this header pulls in NO network header
//    at all (no WiFi.h, no WebServer.h), exactly like net.h, so ui.cpp can call
//    web_pin() for the QR screen without inheriting the WiFi stack.
//
//  OWNERSHIP
//    - webui owns the web PIN. net_url(buf, cap, pin) (net.h) takes it as an
//      argument, so the QR screen cannot be drawn until web_pin() exists.
//    - webui does NOT own the radio. It never calls WiFi.*, never starts the
//      captive DNSServer (net.cpp owns that); it only answers the HTTP half of
//      the captive-portal probe with a redirect.
//    - webui does NOT mutate the pet. No route can: none of them writes.
//
//  All identifiers and comments English.
// =============================================================================
#ifndef NT_WEBUI_H
#define NT_WEBUI_H

#include <stdint.h>
#include <stddef.h>

#include "../core/nt_types.h"   // -> config.h (Config, WEB_* constants)
#include "../game/sim.h"        // SimView

// -----------------------------------------------------------------------------
//  LIFECYCLE
// -----------------------------------------------------------------------------

// Registers every route, rolls the boot PIN (rng_below(RNG_MISC, WEB_PIN_MAX))
// if it has not been rolled yet, calls server.enableDelay(false) (WebServer.h -
// without it handleClient() burns 1 ms of every idle loop()) and starts
// listening on `port`. Idempotent: a second call on the same port is a no-op,
// a call with a different port rebinds. Returns false only when FEATURE_WEB is
// compiled out.
//
// The socket only opens once net_phase() is NPH_AP_PORTAL: binding before lwIP
// exists aborts the firmware inside FreeRTOS, and since P5-C1 the access point
// is the only WiFi phase that has an address at all.
//
// GATED ON CF_WEB_ENABLED when a Config is bound: with the flag clear the
// routes are still registered (once, for the lifetime of the firmware) but no
// socket is opened, and web_running() stays false. The port is remembered, so
// web_service() opens it the moment the user turns the SETTINGS "WEB" toggle back on.
bool     web_begin(uint16_t port = WEB_PORT);

// Pump. Call once per loop(), unconditionally, right after net_service().
// Never blocks: handleClient() returns immediately when no client is queued
// because enableDelay(false) removed the delay(1).
//
// Also owns both edges of CF_WEB_ENABLED. The SETTINGS toggle only fed the entry
// point's radio policy, so whenever anything else kept the radio up the server
// went on answering after the user had switched web access off. Turning the
// flag off here calls web_stop(); turning it back on re-opens the socket on the
// port web_begin() was given.
void     web_service(void);

// Closes the listening socket. The PIN is kept, so a later web_begin() does
// not invalidate a QR code the user already scanned.
void     web_stop(void);

bool     web_running(void);
uint16_t web_port(void);

// -----------------------------------------------------------------------------
//  PIN  (boot PIN, cleartext on the wire, say so in the README)
//    0000..9999. Nothing is PIN-gated yet because nothing mutates yet; spec S34
//    requires the creator API of Phase 8 to be. The QR screen embeds it via
//    net_url(buf, cap, web_pin()).
// -----------------------------------------------------------------------------
uint16_t web_pin(void);

// -----------------------------------------------------------------------------
//  CONFIG BINDING
//    web_bind_config(&g_cfg) before web_begin() lets web_service() honour the
//    SETTINGS "WEB" toggle (CF_WEB_ENABLED) on the entry point's own Config. Unbound,
//    the module keeps serving unconditionally, which is what a standalone
//    build wants.
// -----------------------------------------------------------------------------
void     web_bind_config(Config* cfg);

// -----------------------------------------------------------------------------
//  SPRITE / MOOD POLICY moved to ui/pet_view.cpp by P2-C11c (audit risk 9):
//  "what does this pet look like right now" is a RENDER decision, so
//  pet_pose_of() / pet_mood_index() own it and the phone mirror is one of the
//  two consumers instead of the owner.
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
//  Compile-time sanity on the constants this module contracts against.
// -----------------------------------------------------------------------------
static_assert(WEB_JSON_BUF >= 320, "the shared response buffer is char[320]");
static_assert(WEB_PIN_MAX == 10000, "PIN is exactly 4 decimal digits");
static_assert(WEB_RATE_TOKENS * 1000 <= 65535, "token bucket must fit uint16_t milli-tokens");
static_assert(WEB_COST_MUTATE >= WEB_COST_READ, "a mutation must never be cheaper than a read");

#endif  // NT_WEBUI_H
