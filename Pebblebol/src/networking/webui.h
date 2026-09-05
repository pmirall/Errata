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
//    POST /api/ping    the keep-alive. PIN-gated, and the ONLY thing that
//                      extends the portal's life (P8-C2). One of spec section
//                      38's seven; the other six are P8-C3's.
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
//    - webui owns the RUNTIME half of the creator PIN: it loads or mints it at
//      web_portal_open() and holds the live CreatorGate. The RULES are
//      networking/creator_gate.h's (pure, host-tested) and the PERSISTED half
//      is persistence/game_state.h's (gs_creator_load / gs_creator_store).
//      The PIN NO LONGER TRAVELS IN THE QR: net_url() emits "http://<ip>/" and
//      the user types the four digits the device shows into an X-Pin header
//      (spec section 39).
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

// Registers every route, registers the request headers the PIN gate needs
// (WebServer drops every header that was not asked for - see webui.cpp), calls
// server.enableDelay(false) (WebServer.h -
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

// -----------------------------------------------------------------------------
//  THE PORTAL SESSION (P8-C2)
//
//  web_begin() is BOOT wiring; these three are the CREATOR screen's session.
//  ui_creator_radio() calls open on the way in and close on the way out, and
//  the screen polls idle_expired through CreatorInfo once a second.
//
//  web_portal_open()
//    Loads ConfigV2's creator_pin / pin_fail_count / creator_idle_s and arms
//    the gate. MINTS AND PERSISTS THE PIN ON FIRST CREATOR ENTRY, before the
//    screen can display it, so a device that loses power between showing a PIN
//    and being asked for it still knows the number on the user's phone.
//    Idempotent: entering CREATOR again re-arms the idle timer and keeps the
//    PIN, which is what makes a QR already scanned stay valid.
//
//  web_portal_close()
//    Stops the listening socket and disarms the gate. It does NOT touch the
//    radio: ui_creator_radio(false) owns that half, so there is exactly one
//    teardown and leaving the screen and timing out run the same one.
//
//  web_portal_idle_expired()
//    True once ConfigV2.creator_idle_s (D7, default 300) has passed with no
//    AUTHORISED request. False whenever the portal is not open, and false in a
//    FEATURE_WEB=0 build - where the screen's CREATOR_AP_WAIT_MS timeout is
//    what gets the user back out, since no access point ever appears.
// -----------------------------------------------------------------------------
void     web_portal_open(void);
void     web_portal_close(void);
bool     web_portal_idle_expired(void);

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
//  PIN
//    THE ISSUED PIN, 1..9999, or 0 FOR "NONE ISSUED YET" - the same encoding
//    ConfigV2.creator_pin uses, and 0 is never minted so the sentinel is
//    unambiguous. A PURE READ since P8-C1: it no longer rolls anything, because
//    a getter that mints is a getter whose value depends on who asked first.
//    web_portal_open() is the one mint site.
//
//    It is PERSISTED, so it is the same number across reboots until a factory
//    reset - which is what lets the user keep a scanned QR and a written-down
//    PIN. It is cleartext on the wire (this is an HTTP server on a soft AP);
//    creator_gate.h says exactly what that is and is not worth.
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
