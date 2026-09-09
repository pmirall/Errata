// =============================================================================
//  ERRATA - networking/creator_server.h
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
//  NOT BOUNDED BY THIS FILE, AND SAID PLAINLY BECAUSE THE TABLE ABOVE READS AS
//  COMPREHENSIVE AND IS NOT (recorded at the phase-8 exit):
//
//    the REQUEST LINE, every HEADER LINE and the URL are read by the pinned
//    Arduino core BEFORE any handler here runs - Parsing.cpp:78,149,247,
//    `client.readStringUntil('\r')` into heap-growing Strings with NO length
//    bound. The only limit is a PER-BYTE 5 s timeout, and Stream::timedRead
//    resets its deadline on every byte received, so a client trickling one
//    byte every four seconds grows a String without limit and, worse, blocks
//    inside handleClient() - which app_loop() calls, so the renderer, the 1 Hz
//    tick, input and the creator's own idle teardown all stop with it. There
//    is no watchdog rescue: the loop task WDT is off in this core by default
//    and this tree never arms it (see app/app.h). It is core-inherent,
//    pre-existing, unauthenticated, and bounded only by the attacker choosing
//    to stop or the owner power-cycling. THE BODY CAP DOES NOT COVER THIS -
//    CS_BODY_MAX is a bound on the BODY, and saying otherwise is the sentence
//    wider than the tree that this project keeps writing. The window exists
//    only while the CREATOR screen is open and the attacker is in radio range.
//    The available mitigations - a shorter client read timeout, or arming the
//    loop WDT so a stall is a bounded reset - are a decision for a phase that
//    can bench a reset, not a comment.
//
//    AVAILABILITY OF THE GATE ITSELF. networking/creator_gate.h's lockout is
//    one global CreatorGate and an absent X-Pin counts as a failure, so five
//    unauthenticated requests a minute from anyone in radio range keep the
//    OWNER refused for the whole lock window while the idle timer keeps
//    running - i.e. the idle teardown becomes the attacker's tool rather than
//    the owner's protection. It is a deliberate trade (see that header's WHAT
//    THIS GATE IS NOT), not an oversight, and it is written here so the trade
//    is read rather than rediscovered.
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
//  WITH EXACTLY ONE EXCEPTION, NAMED HERE SO THE SENTENCE ABOVE STAYS TRUE.
//  web/creator/app.js refuses an EMPTY sprite frame ("Algun fotograma del
//  sprite esta vacio") and gates its own SIGUIENTE button on it, and this
//  device accepts one: game/validate.cpp never inspects the sprite BYTES, and
//  it should not, because spec section 35 asks for sprite DIMENSIONS, DATA SIZE
//  and PALETTE - all three structural here, since CustomSpeciesRec.sprite is a
//  fixed array and networking/creator_parse.cpp refuses anything that is not
//  exactly it. A blank creature is legal; it is just not one anybody wants to
//  have drawn by accident. So that check is the page being KIND, and it is the
//  one page rule with no device twin. The direction matters: the page is
//  NARROWER than the device there, never wider, and a page rule that were
//  wider would be a rule nobody enforces.
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
//    POST /api/bug      accept an upload                      here
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
#ifndef ER_CREATOR_SERVER_H
#define ER_CREATOR_SERVER_H

#include <stdint.h>

#include "../core/config.h"

#if FEATURE_WEB

#include <WebServer.h>

// -----------------------------------------------------------------------------
// Registers GET /api/schema, GET /api/state, POST /api/validate,
// POST /api/bug, POST /api/time and - LAST, which is the point - a catch-all
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
#endif  // ER_CREATOR_SERVER_H
