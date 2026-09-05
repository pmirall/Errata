// =============================================================================
//  PEBBLEBOL - networking/creator_server.h
//  THE SEVEN SPEC-38 ROUTES, AND THE HOSTILE-INPUT POSTURE THEY STAND ON.
//
//  =========================================================================
//  THIS IS THE FIRST CODE IN THE PRODUCT THAT ACCEPTS INPUT FROM OUTSIDE THE
//  DEVICE. WHAT IS TRUSTED, AND WHAT IS NOT.
//  =========================================================================
//
//  NOT TRUSTED - every one of these is an attacker-controlled value, and each
//  has a named bound in this chunk rather than a habit:
//
//    the request METHOD and PATH      first match wins, and a catch-all is
//                                     registered LAST so no request reaches the
//                                     core's unbounded body reader
//    Content-Length                   an `int` in the core, settable negative;
//                                     networking/creator_body.h decides the
//                                     whole body's fate from it
//    Content-Type                     "multipart/" routes the body down a
//                                     DIFFERENT core path where server.raw() is
//                                     a null dereference; the shared hook
//                                     checks the header before it touches it
//    the BODY BYTES                   length-bounded, never NUL-bounded, read by
//                                     a fixed-schema reader that cannot recurse
//    X-Pin                            four ASCII digits or nothing, and the
//                                     failure counter is the only memory of it
//    the "pin" body field             same gate, same counter
//    the Host header                  only ever compared, never echoed
//    the epoch in POST /api/time      hardware/gametime.h decides; and
//                                     networking/creator_gate.h is written on
//                                     the assumption that this value is hostile,
//                                     which is why no deadline in this feature
//                                     is measured in wall time
//
//  TRUSTED, and only these:
//
//    the CONTENT TABLES               data/*.h are compiled in, and the
//                                     compile-time guards in them have already
//                                     run before main()
//    THE DEVICE'S OWN CONFIG          ConfigV2 survived a CRC and was written by
//                                     this firmware. Not "correct" - bounded
//                                     (cg_open() still clamps a hostile
//                                     persisted fail count)
//    THE FLASH RECORDS, ONLY AFTER validate_custom_species() HAS SEEN THEM.
//                                     A cs* blob that survived a CRC proves
//                                     nothing about its stat total, so the load
//                                     path judges it with the same function
//                                     this server judges an upload with
//
//  =========================================================================
//  THE PAGE'S OWN CHECKS ARE A COURTESY TO THE USER AND NEVER A CONTROL.
//  =========================================================================
//  web/creator/ will draw a budget bar, grey out an over-budget move and refuse
//  an empty name. NONE OF THAT IS A SECURITY PROPERTY AND NONE OF IT IS
//  ASSUMED HERE. This device cannot tell a request from our page apart from a
//  request from curl, so it assumes curl: every rule the page enforces is
//  enforced again here, by game/validate.h, on the bytes that actually arrived.
//  The page's version exists so a user is not refused after five minutes of
//  drawing - that is worth building, and it is worth nothing as a defence.
//
//  The one thing the page and the device must AGREE on is the numbers, and
//  that is what GET /api/schema is for: data/creator_schema_json.h is generated
//  from the same content object as data/creator_schema.h, static_asserted
//  against the compiled constants, and read back by a host test (plan T4).
//
//  =========================================================================
//  THE SEVEN ROUTES (spec section 38)
//  =========================================================================
//    GET  /                index_html.h, 4-arg send_P            webui.cpp
//    POST /api/ping        the keep-alive, PIN-gated              webui.cpp
//    GET  /api/schema      creator_schema_json.h, 4-arg send_P   here, UNGATED
//    GET  /api/state       device state, PIN-gated               here
//    POST /api/validate    judge an upload, WRITES NOTHING       here
//    POST /api/pebble      accept an upload                      here
//    POST /api/time        gt_set_epoch(CAL_PHONE)               here
//    *    anything else    captive-portal 302 / 404 + body drain here + webui.cpp
//
//  WHY /api/schema IS THE ONE UNGATED ROUTE. It serves compiled constants that
//  are byte-identical on every device running this firmware, so it leaks
//  nothing a copy of the binary does not, and the page needs it before the user
//  has typed a PIN. /api/state is gated, because free Box slots and the device
//  name are this device's.
//
//  ONLY A REQUEST THAT PASSED THE PIN GATE EXTENDS THE PORTAL (P8-C2,
//  creator_gate.h): otherwise anyone in radio range could hold the access point
//  up forever by fetching /api/schema every 299 s.
//
//  =========================================================================
//  LAYERING
//  =========================================================================
//  A DEVICE MODULE: it includes WebServer.h and it is on tools/check.sh's
//  IMPURE_NET list. Everything it DECIDES lives in a pure module a host binary
//  drives - networking/creator_body.cpp (the cap), networking/creator_parse.cpp
//  (the shape), game/validate.cpp (the rules), networking/creator_gate.cpp (the
//  PIN) - and what is left here is transport: read a header, hand bytes to a
//  reader, answer a status code, ask flash to remember.
//
//  All identifiers and comments English; Spanish never leaves strings_es.h and
//  no route emits a UI string.
// =============================================================================
#ifndef PB_CREATOR_SERVER_H
#define PB_CREATOR_SERVER_H

#include <stdint.h>

#include "../core/config.h"

#if FEATURE_WEB

#include <WebServer.h>

// -----------------------------------------------------------------------------
// Registers GET /api/schema, GET /api/state, POST /api/validate,
// POST /api/pebble, POST /api/time and - LAST, which is the point - a catch-all
// that gives every unmatched request a real handler.
//
// THE CATCH-ALL IS NOT COSMETIC. WebServer::onNotFound() is not a handler: it
// is consulted at WebServer.cpp:819, long after the body has been parsed, and
// Parsing.cpp:182 requires _currentHandler non-null before it will use the raw
// path. So without a registered catch-all, `POST /anything-else` with
// `Content-Length: 8000000` still walks readBytesWithTimeout()'s malloc growth
// loop - the audit's section 12 hole, closed for the seven named routes and
// wide open for every other URI. `notfound` is what onNotFound() used to be
// handed, and it must be registered through this function so it comes AFTER
// every real route (first match wins, Parsing.cpp:132-136).
//
// Call it exactly once, from webui.cpp's routes-once block, AFTER the routes
// webui.cpp owns.
void cs_register(WebServer& srv, WebServer::THandlerFunction notfound);

// The shared raw/upload hook. EVERY POST ROUTE IN THIS SERVER MUST PASS THIS AS
// THE FOURTH ARGUMENT OF on(), including the ones webui.cpp owns: the fourth
// argument is what makes canRaw() true, and canRaw() is what keeps a hostile
// Content-Length off the heap.
//
// IT IS ALSO INVOKED AS THE UPLOAD HOOK, on the multipart path, where
// server.raw() dereferences a null unique_ptr with no check (WebServer.h:178).
// It checks the collected Content-Type first and returns before touching it.
void cs_body_hook(void);

// Answers the client when the body is unusable and reports whether it did. A
// handler's first line after the PIN gate is `if (cs_body_answered()) return;`.
// The CB_ABORT case answers NOTHING - the socket is already closed - and still
// returns true.
bool cs_body_answered(void);

// The accumulated body. Empty unless the whole declared length arrived inside
// CS_BODY_MAX; see networking/creator_body.h's cb_ready().
const uint8_t* cs_body_data(void);
uint16_t       cs_body_len(void);

// Ends the request: drops whatever was accumulated so the NEXT request cannot
// read this one's bytes. EVERY handler calls it, GET handlers included, because
// the one request shape that never runs a raw phase - a multipart POST - would
// otherwise inherit the previous request's state.
void cs_body_done(void);

#endif  // FEATURE_WEB
#endif  // PB_CREATOR_SERVER_H
