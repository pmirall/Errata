// =============================================================================
//  webui.h - Nottamagochi HTTP server (BRIEF 1.6 / 4 row 17 / 6.3).
//
//  The phone talks to this and nothing else. The client is index_html.h, which
//  is already written and frozen; every route, query parameter, JSON key and
//  binary offset below is a byte-for-byte match against the fetch calls in that
//  blob. Changing any of them without regenerating the blob breaks the page
//  silently.
//
//  LAYERING
//    webui is integration-layer code: it is allowed to include WebServer.h,
//    sim.h, sprites.h, render.h, net.h and storage.h at the same time. Nothing
//    below it may include webui.h expecting network types - this header pulls
//    in NO network header at all (no WiFi.h, no WebServer.h), exactly like
//    net.h, so ui.cpp and godmode.cpp can call web_pin() for the S15 QR screen
//    without inheriting the WiFi stack.
//
//  OWNERSHIP
//    - webui owns the web PIN. net_url(buf, cap, pin) (net.h:136) takes it as
//      an argument, so the QR screen cannot be drawn until web_pin() exists.
//    - webui does NOT own the radio. It never calls WiFi.*, never starts the
//      captive DNSServer (net.cpp owns that, net.h:5-7); it only answers the
//      HTTP half of the captive-portal probe with a redirect.
//    - webui does NOT mutate PetSave. Every state change goes through
//      sim_apply_action() / sim_apply_minigame(), which own the clamps, the
//      cooldowns and the hourly-gain ledger.
//
//  TRUST MODEL
//    The browser is hostile by assumption. It posts an id and a score; the
//    SERVER decides the deltas (MG*_NUM / MG*_CAP in config.h), enforces
//    MG_COOLDOWN_S, enforces MG_MIN_ELAPSED_PERMILLE against a token it issued
//    itself, and burns the token on every outcome so a score cannot be
//    replayed. A token-bucket limiter sits in front of every handler.
//
//  All identifiers and comments English; every user-facing string the phone
//  shows lives inside index_html.h (Spanish, already written). This module
//  emits no prose - only JSON keys and enum ordinals.
// =============================================================================
#ifndef NT_WEBUI_H
#define NT_WEBUI_H

#include <stdint.h>
#include <stddef.h>

#include "nt_types.h"   // -> config.h (Config, PetSave, WEB_* constants)

// -----------------------------------------------------------------------------
//  ROUTE TABLE (the contract index_html.h is coded against)
//
//   GET  /                      the blob, 4-arg send_P, no-cache
//   GET  /api/state             ~300 B JSON, static char[320]           read
//   POST /api/action?do=..&k=   full state object back                  mutate
//   POST /api/game/start?id=..&k=   {"tok","dur","exp"}                 mutate
//   POST /api/game?id=..&tok=..&score=..&k=..                           mutate
//                               {"ok":1,"d":{..},"st":{..}}
//   GET  /api/sprites?id=N      {'S',1,N,W,H,rev} + N*((W+7)>>3)*H B    read
//   GET  /api/cfg               settings snapshot (never the password)  read
//   POST /api/cfg?..&k=NNNN     partial update, then the snapshot       mutate
//   *    anything else          captive-portal 302 to http://<ip>/, else 404
//
//  Errors (BRIEF 6.3): 429 {"err":"cool","s":N} - and a BARE 429 with an empty
//  body when the rate limiter refuses; 409 {"err":"full"|"tired"|"busy"|...};
//  403 {"err":"pin"}; 400 {"err":"arg"|"tok"}; 410 {"err":"stale"}.
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
//  LIFECYCLE
// -----------------------------------------------------------------------------

// Registers every route, rolls the boot PIN (esp_random() % WEB_PIN_MAX) if it
// has not been rolled yet, calls server.enableDelay(false) (WebServer.h:219 -
// without it handleClient() burns 1 ms of every idle loop()) and starts
// listening on `port`. Idempotent: a second call on the same port is a no-op,
// a call with a different port rebinds. Returns false only when FEATURE_WEB is
// compiled out.
//
// Safe to call before the station has an IP - listening on 0.0.0.0 costs
// nothing and the socket survives the DHCP lease arriving.
//
// GATED ON CF_WEB_ENABLED when a Config is bound: with the flag clear the
// routes are still registered (once, for the lifetime of the firmware) but no
// socket is opened, and web_running() stays false. The port is remembered, so
// web_service() opens it the moment the user turns S9 "WEB" back on.
bool     web_begin(uint16_t port = WEB_PORT);

// Pump. Call once per loop(), unconditionally, right after net_service().
// Never blocks: handleClient() returns immediately when no client is queued
// because enableDelay(false) removed the delay(1). Also applies the deferred
// side effects of a POST /api/cfg (credential change), which are held back
// until after the response has gone out so the request that changed the SSID
// is not killed by its own success.
//
// Also owns both edges of CF_WEB_ENABLED. The S9 toggle only fed the entry
// point's wifi_wanted(), so whenever the radio was kept up for Telegram the
// server went on answering /api/action and /api/cfg after the user had
// switched web access off. Turning the flag off here calls web_stop();
// turning it back on re-opens the socket on the port web_begin() was given.
void     web_service(void);

// Closes the listening socket. The PIN is kept, so a later web_begin() does
// not invalidate a QR code the user already scanned. Any outstanding minigame
// token is burned.
void     web_stop(void);

bool     web_running(void);
uint16_t web_port(void);

// -----------------------------------------------------------------------------
//  PIN  (BRIEF 1.6: boot PIN, cleartext on the wire, say so in the README)
//    0000..9999. Required on POST /api/action, /api/game/start, /api/game and
//    /api/cfg. GET /api/state and GET /api/sprites stay open - a neighbour
//    seeing the pet's hunger level is not a threat model.
//    The S15 QR screen embeds it via net_url(buf, cap, web_pin()).
// -----------------------------------------------------------------------------
uint16_t web_pin(void);

// Rolls a new PIN. Every phone that had the old one is logged out; the QR
// screen must be redrawn. God mode uses this; nothing else should.
void     web_pin_regenerate(void);

// -----------------------------------------------------------------------------
//  ACTIVITY  (render.cpp drops to FPS_LOW while a phone is talking to us)
// -----------------------------------------------------------------------------

// millis() of the last request served, 0 if no phone has ever connected.
// Compare against WEB_CLIENT_ACTIVE_MS.
uint32_t web_client_seen_ms(void);

// True while the last request is younger than WEB_CLIENT_ACTIVE_MS. This is
// the value the S0 status bar shows as the "phone" icon.
bool     web_client_active(void);

uint32_t web_requests_served(void);    // total 2xx/3xx responses
uint32_t web_requests_rejected(void);  // total 4xx (rate limit, PIN, args)

// -----------------------------------------------------------------------------
//  CONFIG BINDING
//
//  GET/POST /api/cfg need a Config to read and write. Two modes:
//
//   bound   - web_bind_config(&g_cfg) before web_begin(). webui reads and
//             writes THAT object, so the entry point, the S9 settings screen
//             and the browser all see the same struct. This is the intended
//             wiring.
//   unbound - webui keeps a private copy, loaded from NVS at web_begin() (or
//             store_cfg_defaults() when NVS has nothing). The module still
//             works standalone; the entry point picks changes up by polling
//             web_cfg_dirty() and copying web_cfg().
//
//  Either way a successful POST persists through store_save_cfg(), which takes
//  a non-const Config& and re-seals magic/version/CRC IN THAT STRUCT. In the
//  bound case that is the entry point's g_cfg, so the refreshed crc16 is what
//  makes the .ino's config_changed() fire and re-apply the settings that live
//  outside Config (OLED contrast above all).
// -----------------------------------------------------------------------------
void          web_bind_config(Config* cfg);
const Config* web_cfg(void);
bool          web_cfg_dirty(void);        // a POST /api/cfg landed
void          web_cfg_clear_dirty(void);

// The last action a POST /api/action APPLIED, as an ActionId; ACT_NONE (0) when
// there is none. Reading it CLEARS it, exactly like web_cfg_dirty() +
// web_cfg_clear_dirty() collapsed into one call, and only ever reports actions
// that sim_apply_action() accepted - a rejected one already answered the phone
// with its error and has nothing for the panel to show.
//
// `before` (may be null) receives the pet AS IT WAS BEFORE the action landed.
// That is not a nicety: ACT_CLEAN sets poop_count to 0 on the spot, so the
// device-side choreography has nothing left to dissolve unless the snapshot was
// taken first, and ACT_SLEEP_TOGGLE cannot tell a yawn from a stretch without
// the previous PF_ASLEEP.
//
// WHY IT EXISTS: webui.cpp calls sim_apply_action() directly and does NOT go
// through ui.cpp's do_action(), so feeding the pet from the phone used to move
// seven bars and animate nothing whatsoever on the OLED. The entry point drains
// this once per loop() and hands it to ui_note_web_action(), the same way it
// already drains web_cfg_dirty().
uint8_t       web_take_action(PetSave* before);

// -----------------------------------------------------------------------------
//  MINIGAME SLOT
//    Exactly ONE token exists globally. A second phone is not locked out: it
//    keeps polling /api/state, sees "busy":1, dims its game cards and can
//    still use the action rail. That is the designed degradation.
// -----------------------------------------------------------------------------
bool     web_game_busy(void);      // a token is outstanding and not yet expired
uint8_t  web_game_id(void);        // MinigameId of that token, MG_NONE if none
uint16_t web_game_left_s(void);    // seconds until the token expires, 0 if none
void     web_game_abort(void);     // burn it (god mode / web_stop())

// -----------------------------------------------------------------------------
//  SPRITE MIRROR
//    ui should call these for the OLED too, so the phone and the panel never
//    show two different bodies. web_pose_of() is the whole pose policy in one
//    place; web_sprite_id() folds in sprite_form_of() (sprites.h:1284), which
//    is the ONLY correct source of the `form` argument - reading adult_form
//    for a CHILD or a TEEN yields FORM_UNSET (0xFF) and renders "descuidado"
//    for the pet's entire minor life.
// -----------------------------------------------------------------------------
uint8_t  web_pose_of(const PetSave& p);      // SpritePose
uint8_t  web_sprite_id(const PetSave& p);    // SpriteSetId

// 0..100 mood score -> enum Mood ordinal, using the nt_types.h:110-115 bands.
// The browser indexes its own Spanish table with this number.
uint8_t  web_mood_index(uint8_t score_0_100);

// -----------------------------------------------------------------------------
//  Compile-time sanity on the constants this module contracts against.
// -----------------------------------------------------------------------------
static_assert(WEB_JSON_BUF >= 320, "BRIEF 1.6: /api/state builds into char[320]");
static_assert(WEB_PIN_MAX == 10000, "PIN is exactly 4 decimal digits");
static_assert(WEB_RATE_TOKENS * 1000 <= 65535, "token bucket must fit uint16_t milli-tokens");
static_assert(WEB_COST_MUTATE >= WEB_COST_READ, "a mutation must never be cheaper than a read");
static_assert(MG_COUNT_WEB == 3, "index_html.h ships exactly three browser minigames");
static_assert(MG_MIN_ELAPSED_PERMILLE > 0 && MG_MIN_ELAPSED_PERMILLE <= 1000,
              "minimum-elapsed gate must be a permille of the real duration");

#endif  // NT_WEBUI_H
