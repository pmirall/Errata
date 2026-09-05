// =============================================================================
//  PEBBLEBOL - networking/creator_body.cpp
//  The raw-body cap. See creator_body.h for what the Arduino core does, what a
//  raw handler cannot do about it, and why there are two refusal bands.
//
//  PURE: <stdint.h>, <string.h> and the constants. No file-scope state - the
//  accumulator is the caller's struct - so both of tools/check.sh's networking
//  purity gates apply to this file.
// =============================================================================
#include "creator_body.h"

#include <string.h>

void cb_reset(CreatorBody& b)
{
  b.declared = 0u;
  b.seen     = 0u;
  b.len      = 0u;
  b.state    = (uint8_t)CB_IDLE;
  b.reserved = 0u;
  // buf is deliberately NOT cleared: 2 KB of memset per request buys nothing
  // (cb_data() is only readable through cb_len(), which is 0 here) and would be
  // paid on every captive-portal probe. The one place it would matter is a
  // parser that reads past len, and that is the thing creator_parse.cpp is
  // written and fuzzed not to do.
}

void cb_start(CreatorBody& b, int content_length)
{
  cb_reset(b);

  // A NEGATIVE Content-Length is not a short body, it is a hostile one: the
  // core compares `size_t < int` at Parsing.cpp:192, so -1 promotes to SIZE_MAX
  // and the read loop runs until the client goes quiet. Hang up.
  if (content_length < 0) { b.state = (uint8_t)CB_ABORT; return; }

  const uint32_t n = (uint32_t)content_length;
  b.declared = n;

  // Zero means no Content-Length header, or a chunked body: the core sets
  // _clientContentLength = 0 in both cases (Parsing.cpp:112 and :175-177), so
  // the read loop never runs and the handler is handed RAW_START/RAW_END with
  // no bytes at all. That looks EXACTLY like a legitimate empty upload, which
  // is how a validator ends up being handed an empty Pebble and told a client
  // sent it. It is answered 411, never parsed.
  if (n == 0u)                     { b.state = (uint8_t)CB_NO_LENGTH;  return; }

  // Above the drain band there is no polite answer: draining 100 MB at one TCP
  // segment per read would hold loop() for as long as the client kept talking.
  if (n > (uint32_t)CS_BODY_DRAIN_MAX) { b.state = (uint8_t)CB_ABORT; return; }

  // Inside the drain band: the bytes are pulled off the wire and thrown away so
  // the connection stays in a state where a 413 can be written back to it.
  if (n > (uint32_t)CS_BODY_MAX)   { b.state = (uint8_t)CB_TOO_LARGE; return; }

  b.state = (uint8_t)CB_COLLECT;
}

void cb_write(CreatorBody& b, const uint8_t* data, uint32_t n)
{
  // seen counts DRAINED bytes as well as kept ones. It is what a test asserts
  // to prove the cap fired instead of the parser having refused later.
  b.seen += n;

  if (b.state != (uint8_t)CB_COLLECT) return;   // draining, or already refused
  if (data == nullptr || n == 0u)     return;

  // THE BOUND IS ON THE BUFFER, NEVER ON `declared`. A core that handed over
  // more bytes than the client declared - which this one does not, and which a
  // later one must not be able to turn into a write past the end - lands here
  // as CB_TOO_LARGE and not as a memcpy that runs off the array.
  const uint32_t room = (uint32_t)CS_BODY_MAX - (uint32_t)b.len;
  if (n > room) {
    b.state = (uint8_t)CB_TOO_LARGE;
    b.len   = 0u;                               // nothing partial is parseable
    return;
  }

  memcpy(&b.buf[b.len], data, (size_t)n);
  b.len = (uint16_t)(b.len + n);
}

void cb_end(CreatorBody& b)
{
  if (b.state != (uint8_t)CB_COLLECT) return;
  // The client declared more than it sent. The core returns false on this path
  // before the handler ever runs, so this state is unreachable through
  // WebServer today - it is here because the module must not DEPEND on that,
  // and because tests/test_creator_api.cpp drives it directly.
  if (b.seen < b.declared) { b.state = (uint8_t)CB_TRUNCATED; b.len = 0u; }
}

void cb_abort(CreatorBody& b)
{
  b.state = (uint8_t)CB_ABORT;
  b.len   = 0u;
}

bool cb_ready(const CreatorBody& b)
{
  return b.state == (uint8_t)CB_COLLECT && b.len > 0u &&
         (uint32_t)b.len == b.declared && b.seen == b.declared;
}

const uint8_t* cb_data(const CreatorBody& b) { return b.buf; }
uint16_t       cb_len(const CreatorBody& b)  { return cb_ready(b) ? b.len : (uint16_t)0u; }
CbState        cb_state(const CreatorBody& b){ return (CbState)b.state; }

static const char* const CB_NAMES[] = {
  "CB_IDLE", "CB_COLLECT", "CB_NO_LENGTH", "CB_TOO_LARGE", "CB_ABORT", "CB_TRUNCATED"
};
static_assert(sizeof(CB_NAMES) / sizeof(CB_NAMES[0]) == (size_t)CB_STATE_COUNT,
              "a CbState was added without its name");

const char* cb_state_name(CbState s)
{
  if ((uint8_t)s >= (uint8_t)CB_STATE_COUNT) return "CB_?";
  return CB_NAMES[(uint8_t)s];
}
