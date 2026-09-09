// =============================================================================
//  ERRATA - networking/creator_body.h
//  THE RAW HTTP BODY CAP, AS PURE ARITHMETIC (P8-C3).
//
//  This is the module the audit's section 12 line "Body limits: none" is about,
//  and it exists as its own file for the reason creator_gate.cpp does:
//  networking/creator_server.cpp includes Arduino.h and WebServer.h, so nothing
//  inside it can be driven by a host binary - and "an oversize body is refused"
//  is exactly the claim that must be driven, byte by byte, without a socket.
//
//  ==========================================================================
//  WHAT THE ARDUINO CORE DOES, READ FROM THE CORE AND NOT ASSUMED
//  ==========================================================================
//  Pinned core: esp32 3.1.1, libraries/WebServer/src/Parsing.cpp.
//
//  WITHOUT a raw handler a POST body goes through readBytesWithTimeout()
//  (Parsing.cpp:44-74), called as
//      readBytesWithTimeout(client, _clientContentLength, ..., 5000)
//  and `maxLength` there is THE CLIENT'S OWN Content-Length HEADER. It is the
//  only bound on a malloc/realloc growth loop, the timeout is per CHUNK rather
//  than per request (a byte every 4 s holds it open forever), and the body is
//  then stored a SECOND time as a String argument named "plain". Meanwhile
//  handleClient() is called from loop() and WebServer serves one client at a
//  time, so all of that blocks the whole firmware: no render, no simulation.
//
//  WITH a raw handler - the fourth argument of on(uri, method, fn, ufn), which
//  is what makes canRaw() true (detail/RequestHandlersImpl.h:97-103) - the core
//  reads into its own fixed HTTP_RAW_BUFLEN buffer (1436 B, heap, freed per
//  request at WebServer.cpp:484) and hands it to the handler in chunks. That is
//  the whole reason for the 4-arg overload; this module is what catches those
//  chunks.
//
//  THREE THINGS THE HANDLER CANNOT DO, AND THE SHAPE THAT FOLLOWS FROM THEM:
//   1. RAW_START CANNOT ABORT THE READ. Parsing.cpp:189 calls the handler and
//      line 192 enters the loop unconditionally. So an oversize body is
//      DRAINED, not refused: cb_start() records the verdict, cb_write() throws
//      the bytes away, and the ordinary handler answers 413 afterwards.
//   2. THE HANDLER CANNOT SEND FROM INSIDE THE LOOP. The response belongs to
//      fn(), which the core runs after the body (WebServer.cpp:808-833).
//   3. THE ONLY WAY OUT OF THE LOOP IS CLOSING THE SOCKET, which makes the next
//      readBytes() return 0 -> RAW_ABORTED -> the client is dropped with NO
//      response at all. That is why there are two bands: drain-and-413 up to
//      CS_BODY_DRAIN_MAX, hang up above it. A client declaring 100 MB would
//      otherwise hold loop() for as long as it kept trickling bytes.
//
//  ==========================================================================
//  EVERY BYTE HERE IS HOSTILE UNTIL PROVEN OTHERWISE
//  ==========================================================================
//  Content-Length is `int` in the core (WebServer.h:307), set from
//  headerValue.toInt(), so it can be NEGATIVE - and "Content-Length: -1" makes
//  the core's `totalSize < (size_t)_clientContentLength` comparison promote to
//  SIZE_MAX and read until the client goes quiet. cb_start() takes the core's
//  `int` verbatim and treats anything below zero as CB_ABORT, because a
//  negative declared length is not a length.
//
//  A Content-Length that LIES is bounded in both directions: fewer bytes than
//  declared ends CB_TRUNCATED (the core would already have returned false, but
//  this module must not depend on that), and more bytes than declared - which
//  the core cannot produce today but a future core could - is CB_TOO_LARGE
//  rather than a write past the buffer.
//
//  THE BUFFER IS THE CALLER'S. No file-scope state, exactly as creator_gate.h:
//  the whole accumulator is one struct creator_server.cpp holds one of, which
//  is what lets tests/test_creator_api.cpp run a hundred hostile bodies through
//  it in one process with no device. It is on tools/check.sh's PURE_NET and
//  PURE_NET_CPP lists and both networking purity gates apply.
//
//  All identifiers and comments English.
// =============================================================================
#ifndef ER_CREATOR_BODY_H
#define ER_CREATOR_BODY_H

#include <stdint.h>

#include "../core/config.h"      // CS_BODY_MAX, CS_BODY_DRAIN_MAX

// -----------------------------------------------------------------------------
// The verdict on one request body. It is decided at RAW_START from the DECLARED
// length and can only get worse as bytes arrive: a body never becomes
// acceptable after it was refused.
// -----------------------------------------------------------------------------
enum CbState : uint8_t {
  CB_IDLE = 0,     // no body has been started on this request
  CB_COLLECT,      // within the cap; bytes are being kept
  CB_NO_LENGTH,    // Content-Length absent, zero, or chunked -> 411
  CB_TOO_LARGE,    // declared or actual length over CS_BODY_MAX -> drain, 413
  CB_ABORT,        // negative or over CS_BODY_DRAIN_MAX -> close the socket
  CB_TRUNCATED,    // fewer bytes arrived than were declared -> 400
  CB_STATE_COUNT
};

// -----------------------------------------------------------------------------
// The accumulator. 2,060 B, and CS_BODY_MAX of that is the point of the chunk.
//
// declared  the client's Content-Length, clamped at 0 for anything negative.
// seen      how many body bytes the core has actually handed over. It counts
//           DRAINED bytes too, which is what makes "the cap fired" observable.
// len       how many of them are in buf. Never above CS_BODY_MAX, ever.
// state     CbState.
//
// THE WORST LEGITIMATE BODY IS 384 B, and the arithmetic is here so the cap has
// a reason rather than a round number:
//     {"v":1,"pin":"1234","name":"<=12","type":N,"base":[N,N,N,N],
//      "moves":[NN,NN,NN,NN],"sprite":["<144 hex>","<144 hex>"]}
//   syntax and keys          ~ 90 B
//   name                       12 B
//   type + base + moves      ~ 20 B
//   two sprite frames         288 B
//                            ------
//                            ~ 410 B worst case with every number at 3 digits
// CS_BODY_MAX 2048 is five times that. See core/config.h for what it costs.
// -----------------------------------------------------------------------------
struct CreatorBody {
  uint32_t declared;
  uint32_t seen;
  uint16_t len;
  uint8_t  state;
  uint8_t  reserved;               // must be 0
  uint8_t  buf[CS_BODY_MAX];
};

// Back to CB_IDLE with nothing kept. Called on every request, before anything
// else, so one request can never read the previous request's bytes.
void cb_reset(CreatorBody& b);

// RAW_START. `content_length` is WebServer::clientContentLength() verbatim,
// including its ability to be negative. Decides the verdict for the whole body.
void cb_start(CreatorBody& b, int content_length);

// RAW_WRITE. `n` bytes from the core's HTTP_RAW_BUFLEN buffer. Bounded on every
// path: it never writes past buf and never trusts `declared`.
void cb_write(CreatorBody& b, const uint8_t* data, uint32_t n);

// RAW_END. Turns "the client declared more than it sent" into CB_TRUNCATED.
// NOT called on RAW_ABORTED - the core returns before delivering RAW_END
// (Parsing.cpp:199) - so cb_abort() is the other end of the request.
void cb_end(CreatorBody& b);

// The core delivered RAW_ABORTED, or the transport died. Nothing kept is usable.
void cb_abort(CreatorBody& b);

// True when the body is complete, inside the cap and non-empty: the ONLY state
// in which the bytes may be parsed.
bool cb_ready(const CreatorBody& b);

// The bytes, and how many. Never NUL-terminated: the parser is length-bounded
// on purpose, because a body may legally contain a NUL and a NUL-terminated
// reader would then stop early and validate a PREFIX of what arrived.
const uint8_t* cb_data(const CreatorBody& b);
uint16_t       cb_len(const CreatorBody& b);
CbState        cb_state(const CreatorBody& b);

// The English name of a state.
//
// NO FIRMWARE CALLER, AND THAT IS DELIBERATE RATHER THAN AN OVERSIGHT: it is
// absent from the release AND the baseline .elf (--gc-sections drops it),
// because creator_server.cpp maps CbState to a status code with its own switch
// and never prints a name. It exists for tests/test_creator_api.cpp and, more
// usefully, for the static_assert beside CB_NAMES in the .cpp - which is what
// actually stops a CbState being added without a name, and which DOES run in
// every build. Named here so a reader does not go looking for the call site.
const char* cb_state_name(CbState s);

// -----------------------------------------------------------------------------
//  Compile-time sanity on the constants this module contracts against.
// -----------------------------------------------------------------------------
static_assert(CS_BODY_MAX >= 512, "the cap must clear the largest legitimate upload");
static_assert(CS_BODY_MAX <= 65535, "CreatorBody.len is a uint16_t");
static_assert(CS_BODY_DRAIN_MAX >= CS_BODY_MAX,
              "the drain band must be at least the accept band, or a body could be "
              "hung up on and answered 413 at the same time");

#endif  // ER_CREATOR_BODY_H
