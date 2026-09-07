// =============================================================================
//  PEBBLEBOL host test - test_creator_api.cpp
//  THE RAW-BODY CAP AND THE FIXED-SCHEMA READER (P8-C3, spec section 38,
//  audit section 12 "Body limits: none").
//
//  =========================================================================
//  WHERE THIS FIXTURE DIFFERS FROM app_setup()'s OWN WIRING, STATED HERE
//  BECAUSE A FIXTURE THAT DIFFERS SILENTLY IS TESTING A FIRMWARE NOBODY FLASHES
//  =========================================================================
//  FOUR DIFFERENCES, and each names what covers the other half:
//
//   1. THERE IS NO SOCKET AND NO WebServer. tests/Makefile compiles no device
//      module: networking/creator_server.cpp includes Arduino.h and WebServer.h.
//      So the RAW_START / RAW_WRITE / RAW_END sequence is reproduced HERE, from
//      the pinned core (esp32 3.1.1, libraries/WebServer/src/Parsing.cpp:182-205)
//      rather than delivered by it - including HTTP_RAW_BUFLEN, which is 1436
//      and is the chunk size feed_body() below uses. What no host binary can
//      see is that creator_server.cpp really passes the fourth argument of on()
//      on every POST route; that is a one-line reading of its cs_register().
//
//   2. THE HTTP STATUS CODES ARE NOT ASSERTED HERE, the STATES are. The map
//      from state to code is creator_server.cpp's cs_body_answered(), one
//      switch with one line per state: CB_NO_LENGTH -> 411, CB_TOO_LARGE ->
//      413, CB_TRUNCATED / not-ready -> 400, CB_ABORT -> no answer at all
//      (the socket is gone), CB_IDLE -> 415. tools/creator_smoke.sh (P8-C5) is
//      where a real curl sees the numbers.
//
//   3. THE CLOCK, THE STORE AND THE RATE LIMITER ARE ABSENT because nothing in
//      this binary needs them: the cap and the reader take no clock, allocate
//      nothing and hold no state between requests. tests/test_creator_gate.cpp
//      drives the PIN's clocks and tests/test_game_state.cpp drives the store.
//
//   4. THE END-TO-END CASE COPIES THE NAME ITS OWN WAY (added at the phase-8
//      exit; the list above said THREE and there were four). h_pebble() does
//      `memcpy(p->nickname, rec.name, CS_NAME_CAP)` - a fixed 13 bytes, under
//      a static_assert that the two fields are the same size - and the case
//      below does memset+memcpy of strlen(rec.name). The results are identical
//      for every name validate_custom_species() accepts, because it refuses an
//      unterminated one by name (VR_CS_BAD_NAME), so this is equivalent TODAY
//      and stops being equivalent the moment either field size or the NUL rule
//      moves. It is written here rather than left to a reader diffing the two
//      functions, because this banner is what a reader trusts instead.
//
//  WHAT IS REAL HERE: game/validate.cpp, game/species_custom.cpp and
//  game/box.cpp are the shipping objects, so the end-to-end case files a REAL
//  Pebble through the tree's ONE constructor and runs the tree's ONE validator
//  on it.
//
//  =========================================================================
//  EVERY FUZZ BODY IS HEAP-ALLOCATED AT EXACTLY ITS OWN LENGTH
//  =========================================================================
//  and freed straight after, so a read one byte past the end is a heap overflow
//  a sanitiser can see rather than a read into the rest of a static array that
//  nothing would ever notice. SINCE THE PHASE-8 EXIT THAT IS NOT A MANUAL RUN
//  ANY MORE: `make -C tests asan` builds this binary and three others with
//  -fsanitize=address and runs them, and tools/check.sh runs it on every
//  commit. It had to become a gate stage because it was the ONLY instrument
//  the three "never over-reads" case names below have, and it was never run:
//  a one-byte over-read planted in cp_skip_ws() printed ALL PASS 49/49 on the
//  plain build and heap-buffer-overflow under the sanitiser.
//
//  -fsanitize=undefined is NOT in that target and the reason is pre-existing:
//  it makes evolution_table.h:112's null check non-constant and fails a
//  static_assert in a generated header. tests/Makefile records the subset that
//  does work.
// =============================================================================
#include "nt_test.h"

#include <stdlib.h>
#include <string.h>

#include "core/rng.h"
#include "core/strings_es.h"
#include "data/attacks_table.h"
#include "data/creator_schema.h"
#include "data/creator_schema_json.h"
#include "data/species_table.h"
#include "ui/pet_art.h"      // pet_species_name(): the roster name a custom species has NOT got
#include "game/box.h"
#include "game/genome.h"
#include "game/species_custom.h"
#include "game/validate.h"
#include "networking/creator_body.h"
#include "networking/creator_parse.h"

// The core's own chunk size, WebServer.h:62-64. Not overridden anywhere in this
// build: tools/build.sh passes no -D to the library.
#define CORE_RAW_BUFLEN   1436

// Two offsets into VALID_BODY, so a case can replace the FRONT of the document
// without duplicating a key it also wants to keep. Both are checked against the
// bytes they claim to point at in the first case below, because an offset that
// silently drifted would turn every case using it into a CP_DUP_KEY that reads
// like a parser bug.
#define BODY_AFTER_V      7u     // -> "name":"Bicho",...
#define BODY_AFTER_NAME  22u     // -> "type":0,...

// A body the schema accepts. Kept as one string so every hostile case below is
// a NAMED single edit of it rather than a hand-typed document that might be
// wrong for a second reason.
static const char* const VALID_BODY =
  "{\"v\":1,\"name\":\"Bicho\",\"type\":0,\"base\":[6,5,5,5],"
  "\"moves\":[1,6,32,34],\"sprite\":["
  "\"000000000000000000000000000000000000000000000000"
  "000000000000000000000000000000000000000000000000"
  "0000000000000000000000000000000000000000000000ff\","
  "\"ffffffffffffffffffffffffffffffffffffffffffffffff"
  "000000000000000000000000000000000000000000000000"
  "000000000000000000000000000000000000000000000000\"]}";

// =============================================================================
//  THE CORE'S READ LOOP, REPRODUCED
//
//  Parsing.cpp:182-205 verbatim in shape: RAW_START once, then
//  ceil(len / HTTP_RAW_BUFLEN) RAW_WRITE deliveries of at most HTTP_RAW_BUFLEN
//  bytes, then RAW_END. `declared` is the CLIENT'S Content-Length and is a
//  separate argument from the bytes actually sent, because the whole point of
//  several cases below is that the two disagree.
// =============================================================================
static void feed_body(CreatorBody& b, const uint8_t* data, uint32_t len,
                      int declared)
{
  cb_start(b, declared);
  // The core reads min(declared - totalSize, HTTP_RAW_BUFLEN) each time and
  // stops when totalSize reaches declared, so a client that sends MORE than it
  // declared simply never has the excess read. The loop below delivers what the
  // caller says was sent, bounded the same way, so a test can drive both.
  uint32_t sent = 0;
  while (sent < len) {
    uint32_t n = len - sent;
    if (n > (uint32_t)CORE_RAW_BUFLEN) n = (uint32_t)CORE_RAW_BUFLEN;
    cb_write(b, data + sent, n);
    sent += n;
  }
  cb_end(b);
}

// A body on the heap at exactly its own length: a read past the end is then a
// heap overflow rather than a read into the rest of a static array.
struct HeapBody {
  uint8_t* p;
  uint32_t n;
  explicit HeapBody(const char* s) : p(nullptr), n((uint32_t)strlen(s)) {
    p = (uint8_t*)malloc(n ? n : 1);
    memcpy(p, s, n);
  }
  HeapBody(const uint8_t* s, uint32_t len) : p(nullptr), n(len) {
    p = (uint8_t*)malloc(n ? n : 1);
    if (n) memcpy(p, s, n);
  }
  ~HeapBody() { free(p); }
  HeapBody(const HeapBody&) = delete;
  HeapBody& operator=(const HeapBody&) = delete;
};

// Parse one body straight out of the heap, with no accumulator in the way.
static CpErr parse_str(const char* s, CustomSpeciesRec& out, uint16_t& pin)
{
  HeapBody hb(s);
  return cp_parse_species(hb.p, hb.n, out, pin);
}

static CpErr parse_bytes(const uint8_t* s, uint32_t n,
                         CustomSpeciesRec& out, uint16_t& pin)
{
  HeapBody hb(s, n);
  return cp_parse_species(hb.p, hb.n, out, pin);
}

// =============================================================================
//  1. THE CAP. THIS IS THE POINT OF THE CHUNK.
// =============================================================================

TEST(a_body_inside_the_cap_arrives_whole) {
  CreatorBody b;
  HeapBody hb(VALID_BODY);
  feed_body(b, hb.p, hb.n, (int)hb.n);
  CHECK_EQ((int)cb_state(b), (int)CB_COLLECT);
  CHECK(cb_ready(b));
  CHECK_EQ((int)cb_len(b), (int)hb.n);
  CHECK_EQ(memcmp(cb_data(b), hb.p, hb.n), 0);
  // Without this the case would pass on a body the accumulator silently
  // truncated to zero.
  CHECK(hb.n > 300u);
  // The two offsets every later case slices the document at.
  CHECK_EQ(strncmp(VALID_BODY + BODY_AFTER_V, "\"name\"", 6), 0);
  CHECK_EQ(strncmp(VALID_BODY + BODY_AFTER_NAME, "\"type\"", 6), 0);
}

TEST(a_body_of_exactly_the_cap_is_accepted) {
  CreatorBody b;
  uint8_t* big = (uint8_t*)malloc(CS_BODY_MAX);
  memset(big, 'x', CS_BODY_MAX);
  feed_body(b, big, (uint32_t)CS_BODY_MAX, CS_BODY_MAX);
  CHECK_EQ((int)cb_state(b), (int)CB_COLLECT);
  CHECK(cb_ready(b));
  CHECK_EQ((int)cb_len(b), (int)CS_BODY_MAX);
  // It spans two of the core's chunks, so the boundary arithmetic is exercised
  // and not merely the one-chunk case.
  CHECK(CS_BODY_MAX > CORE_RAW_BUFLEN);
  free(big);
}

TEST(a_body_one_byte_over_the_cap_is_refused_and_nothing_is_kept) {
  // THE MUTATION TARGET OF THIS CHUNK, AND THE FIRST VERSION OF THIS CASE COULD
  // NOT FAIL FOR THE REASON IT NAMES. It fed the whole 2049 B body and asserted
  // the end state - which is CB_TOO_LARGE either way, because cb_write()'s
  // second, defensive bound catches the overflow one chunk later. Deleting the
  // cap check from cb_start() left it GREEN. So the assertion is now the thing
  // the cap actually claims: THE DECLARED LENGTH DECIDES THE WHOLE BODY BEFORE
  // ONE BYTE ARRIVES.
  CreatorBody b;
  const uint32_t n = (uint32_t)CS_BODY_MAX + 1u;

  cb_start(b, (int)n);
  CHECK_EQ((int)cb_state(b), (int)CB_TOO_LARGE);   // <- delete the cap, lose this
  CHECK_EQ((int)b.len, 0);
  CHECK(!cb_ready(b));

  // ...and it stays decided as the core delivers. With the cap gone the first
  // 1436 B chunk WOULD have been copied, so this second assertion is what
  // separates "refused at RAW_START" from "refused when the buffer filled".
  uint8_t* big = (uint8_t*)malloc(n);
  memset(big, 'x', n);
  cb_write(b, big, (uint32_t)CORE_RAW_BUFLEN);
  CHECK_EQ((int)cb_state(b), (int)CB_TOO_LARGE);
  CHECK_EQ((int)b.len, 0);
  cb_write(b, big + CORE_RAW_BUFLEN, n - (uint32_t)CORE_RAW_BUFLEN);
  cb_end(b);

  CHECK_EQ((int)cb_state(b), (int)CB_TOO_LARGE);
  CHECK(!cb_ready(b));
  CHECK_EQ((int)cb_len(b), 0);              // nothing partial is parseable
  // AND THE BYTES WERE STILL DRAINED: the handler cannot stop the core's read
  // loop, so "refused" must mean "read and thrown away", not "not read".
  CHECK_EQ((int)b.seen, (int)n);
  free(big);

  // The boundary is exact in both directions.
  cb_start(b, CS_BODY_MAX);
  CHECK_EQ((int)cb_state(b), (int)CB_COLLECT);
  cb_start(b, CS_BODY_MAX + 1);
  CHECK_EQ((int)cb_state(b), (int)CB_TOO_LARGE);
}

TEST(the_write_bound_is_the_buffer_and_never_the_declared_length) {
  // cb_write()'s second bound is DEFENCE IN DEPTH and today it is unreachable
  // through cb_start(), which never admits a declared length above the cap - so
  // it can only be tested by driving the struct directly, which the header
  // explicitly allows (the accumulator is the caller's).
  //
  // IT MATTERS BECAUSE THE FAILURE IS NOT A WRONG ANSWER, IT IS A MEMCPY PAST A
  // 2 KB ARRAY. Bound this on `declared` instead and a core that handed over
  // more bytes than the cap - a future core, a changed HTTP_RAW_BUFLEN, a
  // mistake in cb_start() - writes off the end. Under AddressSanitizer this
  // case is a heap-buffer-overflow; without it, it is silent.
  CreatorBody b;
  cb_reset(b);
  b.state    = (uint8_t)CB_COLLECT;
  b.declared = 0xFFFFFFFFu;                  // a length no cb_start() would set

  const uint32_t n = (uint32_t)CS_BODY_MAX + 952u;
  uint8_t* big = (uint8_t*)malloc(n);
  memset(big, 'x', n);
  cb_write(b, big, n);
  CHECK_EQ((int)cb_state(b), (int)CB_TOO_LARGE);
  CHECK_EQ((int)b.len, 0);
  free(big);

  // And one byte at a time up to the very last: the last legal write fills the
  // buffer exactly and the next one is refused rather than written.
  cb_reset(b);
  b.state    = (uint8_t)CB_COLLECT;
  b.declared = 0xFFFFFFFFu;
  uint8_t one = 'y';
  for (uint32_t i = 0; i < (uint32_t)CS_BODY_MAX; ++i) cb_write(b, &one, 1u);
  CHECK_EQ((int)b.len, (int)CS_BODY_MAX);
  CHECK_EQ((int)cb_state(b), (int)CB_COLLECT);
  cb_write(b, &one, 1u);
  CHECK_EQ((int)cb_state(b), (int)CB_TOO_LARGE);
  CHECK_EQ((int)b.len, 0);
}

TEST(a_body_over_the_drain_band_hangs_up_instead_of_answering) {
  CreatorBody b;
  cb_start(b, CS_BODY_DRAIN_MAX);
  CHECK_EQ((int)cb_state(b), (int)CB_TOO_LARGE);      // still politely refusable
  cb_start(b, CS_BODY_DRAIN_MAX + 1);
  CHECK_EQ((int)cb_state(b), (int)CB_ABORT);          // creator_server stops the socket
  CHECK(!cb_ready(b));
  CHECK_EQ((int)cb_len(b), 0);
}

TEST(a_negative_content_length_is_hostile_and_not_a_short_body) {
  // "Content-Length: -1" makes the core's `size_t < int` comparison promote to
  // SIZE_MAX and read until the client goes quiet (Parsing.cpp:192). There is
  // no polite answer to that; the socket is closed.
  CreatorBody b;
  cb_start(b, -1);
  CHECK_EQ((int)cb_state(b), (int)CB_ABORT);
  cb_start(b, -2147483647 - 1);
  CHECK_EQ((int)cb_state(b), (int)CB_ABORT);
}

TEST(an_absent_or_chunked_content_length_is_never_an_empty_upload) {
  // The core sets _clientContentLength to 0 for BOTH, so the read loop never
  // runs and the handler is offered a legitimate-looking empty body. That is
  // exactly how a validator ends up being handed an empty Pebble and told a
  // client sent it, so it is CB_NO_LENGTH and creator_server answers 411.
  CreatorBody b;
  feed_body(b, (const uint8_t*)"", 0u, 0);
  CHECK_EQ((int)cb_state(b), (int)CB_NO_LENGTH);
  CHECK(!cb_ready(b));
  CHECK_EQ((int)cb_len(b), 0);
}

TEST(a_content_length_that_lies_is_bounded_in_both_directions) {
  CreatorBody b;
  HeapBody hb(VALID_BODY);

  // DECLARED MORE THAN SENT. The core returns false before the handler runs, so
  // this state is unreachable through WebServer today - it is asserted because
  // the module must not DEPEND on that.
  feed_body(b, hb.p, hb.n, (int)(hb.n + 10u));
  CHECK_EQ((int)cb_state(b), (int)CB_TRUNCATED);
  CHECK(!cb_ready(b));
  CHECK_EQ((int)cb_len(b), 0);

  // DECLARED LESS THAN SENT. cb_ready() requires len == declared == seen, so a
  // body that over-delivers is refused rather than parsed as a prefix.
  feed_body(b, hb.p, hb.n, (int)(hb.n - 10u));
  CHECK(!cb_ready(b));
  CHECK_EQ((int)cb_len(b), 0);

  // AND THE BUFFER IS THE BOUND, NOT `declared`. A core that handed over more
  // than the cap after declaring less must land in CB_TOO_LARGE and never in a
  // memcpy past the array.
  const uint32_t n = (uint32_t)CS_BODY_MAX + 64u;
  uint8_t* big = (uint8_t*)malloc(n);
  memset(big, 'x', n);
  cb_start(b, 16);                       // "sixteen bytes, honest"
  CHECK_EQ((int)cb_state(b), (int)CB_COLLECT);
  cb_write(b, big, n);                   // ...then 2112
  CHECK_EQ((int)cb_state(b), (int)CB_TOO_LARGE);
  CHECK_EQ((int)cb_len(b), 0);
  free(big);
}

TEST(an_aborted_transfer_keeps_nothing) {
  CreatorBody b;
  HeapBody hb(VALID_BODY);
  cb_start(b, (int)hb.n);
  cb_write(b, hb.p, 100u);
  cb_abort(b);                            // the core's RAW_ABORTED
  CHECK_EQ((int)cb_state(b), (int)CB_ABORT);
  CHECK(!cb_ready(b));
  CHECK_EQ((int)cb_len(b), 0);
}

TEST(one_request_can_never_read_the_previous_requests_bytes) {
  // creator_server.cpp calls cs_body_done() at the end of EVERY handler, GET
  // handlers included, because the one request shape that never runs a raw
  // phase - a multipart POST - would otherwise inherit whatever was left.
  CreatorBody b;
  HeapBody hb(VALID_BODY);
  feed_body(b, hb.p, hb.n, (int)hb.n);
  CHECK(cb_ready(b));

  cb_reset(b);                            // the end of the handler
  CHECK_EQ((int)cb_state(b), (int)CB_IDLE);
  CHECK(!cb_ready(b));
  CHECK_EQ((int)cb_len(b), 0);
  // CB_IDLE is what creator_server answers 415: no raw phase ran at all.
}

TEST(every_body_state_has_a_distinct_english_name_and_the_lookup_is_total) {
  for (int a = 0; a < (int)CB_STATE_COUNT; ++a) {
    CHECK(cb_state_name((CbState)a) != nullptr);
    for (int b2 = a + 1; b2 < (int)CB_STATE_COUNT; ++b2)
      CHECK(strcmp(cb_state_name((CbState)a), cb_state_name((CbState)b2)) != 0);
  }
  CHECK_STR_EQ(cb_state_name((CbState)CB_STATE_COUNT), "CB_?");
  CHECK_STR_EQ(cb_state_name((CbState)200), "CB_?");
}

// =============================================================================
//  2. THE READER: THE DOCUMENT IT ACCEPTS
// =============================================================================

TEST(the_valid_document_parses_into_exactly_what_it_says) {
  CustomSpeciesRec c;
  uint16_t pin = 0xFFFFu;
  CHECK_EQ((int)parse_str(VALID_BODY, c, pin), (int)CP_OK);
  CHECK_EQ((int)pin, 0);                       // absent, not wrong
  CHECK_EQ((int)c.magic, (int)CS_MAGIC);
  CHECK_EQ((int)c.version, (int)SAVE_SCHEMA_VERSION);
  CHECK_EQ((int)c.slot, 0);                    // THE DEVICE picks it, not the page
  CHECK_EQ((int)c.budget_used, 0);             // THE DEVICE prices it, not the page
  CHECK_EQ((int)c.compat_group, 0);
  CHECK_STR_EQ(c.name, "Bicho");
  CHECK_EQ((int)c.type, (int)TYPE_SIGNAL);
  CHECK_EQ((int)c.base[0], 6); CHECK_EQ((int)c.base[1], 5);
  CHECK_EQ((int)c.base[2], 5); CHECK_EQ((int)c.base[3], 5);
  CHECK_EQ((int)c.moves[0], 1); CHECK_EQ((int)c.moves[1], 6);
  CHECK_EQ((int)c.moves[2], 32); CHECK_EQ((int)c.moves[3], 34);
  // The sprite hex really decoded, in both frames and at both ends.
  CHECK_EQ((int)c.sprite[0][CS_SPRITE_BYTES - 1], 0xFF);
  CHECK_EQ((int)c.sprite[0][0], 0x00);
  CHECK_EQ((int)c.sprite[1][0], 0xFF);
  CHECK_EQ((int)c.sprite[1][CS_SPRITE_BYTES - 1], 0x00);
  for (uint8_t i = 0; i < (uint8_t)sizeof c.reserved; ++i)
    CHECK_EQ((int)c.reserved[i], 0);
}

TEST(the_pin_may_ride_in_the_body_and_is_parsed_by_the_one_pin_parser) {
  CustomSpeciesRec c;
  uint16_t pin = 0;
  char buf[1024];

  snprintf(buf, sizeof buf, "{\"v\":1,\"pin\":\"0421\",%s", VALID_BODY + BODY_AFTER_V);
  CHECK_EQ((int)parse_str(buf, c, pin), (int)CP_OK);
  CHECK_EQ((int)pin, 421);

  // A PIN that is not four digits is not an error - it is a PIN that will fail,
  // and the gate is what says so. A malformed and a wrong PIN are ONE answer.
  snprintf(buf, sizeof buf, "{\"v\":1,\"pin\":\"42\",%s", VALID_BODY + BODY_AFTER_V);
  CHECK_EQ((int)parse_str(buf, c, pin), (int)CP_OK);
  CHECK_EQ((int)pin, 0);
  snprintf(buf, sizeof buf, "{\"v\":1,\"pin\":\"04211\",%s", VALID_BODY + BODY_AFTER_V);
  CHECK_EQ((int)parse_str(buf, c, pin), (int)CP_OK);
  CHECK_EQ((int)pin, 0);
  // THE RECORDED BYPASS. "4294971117" is congruent to 3821 mod 2^32 and
  // authenticated AS 3821 through the deleted arg_u32; four-digit bounding is
  // what makes it unable to exist.
  snprintf(buf, sizeof buf, "{\"v\":1,\"pin\":\"4294971117\",%s", VALID_BODY + BODY_AFTER_V);
  CHECK_EQ((int)parse_str(buf, c, pin), (int)CP_OK);
  CHECK_EQ((int)pin, 0);
  // A NUMBER is not a PIN: "0421" and 421 are different PINs and only one of
  // them survives a JSON number.
  snprintf(buf, sizeof buf, "{\"v\":1,\"pin\":421,%s", VALID_BODY + BODY_AFTER_V);
  CHECK_EQ((int)parse_str(buf, c, pin), (int)CP_BAD_TYPE);
}

// =============================================================================
//  3. THE READER: EVERY WAY A DOCUMENT CAN BE WRONG, EACH WITH ITS OWN CODE
// =============================================================================

TEST(a_body_that_is_not_a_document_is_refused_by_name) {
  CustomSpeciesRec c;
  uint16_t pin = 0;

  CHECK_EQ((int)parse_str("", c, pin), (int)CP_EMPTY);
  CHECK_EQ((int)cp_parse_species(nullptr, 0u, c, pin), (int)CP_EMPTY);
  CHECK_EQ((int)parse_str("[]", c, pin), (int)CP_SYNTAX);
  CHECK_EQ((int)parse_str("null", c, pin), (int)CP_SYNTAX);
  CHECK_EQ((int)parse_str("{", c, pin), (int)CP_SYNTAX);
  CHECK_EQ((int)parse_str("{}", c, pin), (int)CP_MISSING_KEY);
  CHECK_EQ((int)parse_str("{\"v\"", c, pin), (int)CP_SYNTAX);
  CHECK_EQ((int)parse_str("{\"v\":", c, pin), (int)CP_SYNTAX);
  CHECK_EQ((int)parse_str("{\"v\":1", c, pin), (int)CP_SYNTAX);
  CHECK_EQ((int)parse_str("{1:2}", c, pin), (int)CP_SYNTAX);
  CHECK_EQ((int)parse_str("{\"v\":1}", c, pin), (int)CP_MISSING_KEY);
}

// -----------------------------------------------------------------------------
//  EVERY REQUIRED KEY, ONE AT A TIME.
//
//  ADDED AT THE PHASE-8 EXIT, AND THE CASE ABOVE IS WHY. It drives
//  CP_MISSING_KEY with "{}" and "{\"v\":1}" - documents missing EVERY key, or
//  all but one - so the `required` mask was only ever asserted IN AGGREGATE.
//  Whichever single key was dropped from it, CPK_V or CPK_NAME still fired and
//  the case stayed green. MEASURED: with CPK_TYPE and CPK_SPRITE both removed
//  from creator_parse.cpp's mask, `make -C tests check` printed ALL PASS 49/49,
//  and
//    printf '{"v":1,"name":"Bicho","base":[6,5,5,5],"moves":[1,6,32,34]}'
//      piped into tests/bin/creator_decode
//  answered cp=CP_OK vr=VR_OK type=0 with both sprite frames all zero: a
//  type-defaulted, entirely blank creature accepted by POST /api/pebble.
//
//  TWO OF THE SIX HAVE NO DOWNSTREAM GUARD AT ALL, which is what makes the mask
//  load-bearing rather than belt-and-braces: a zeroed `type` is 0 = TYPE_SIGNAL,
//  a legal value, and game/validate.cpp's validate_custom_species() never
//  inspects the sprite bytes (spec section 35 asks for dimensions, data size and
//  palette - all structural here, since the record's sprite is a fixed array).
//  The mask is the only thing standing there.
//
//  THE SURGERY IS CHECKED BEFORE IT IS TRUSTED. A helper that produced garbage
//  would give CP_MISSING_KEY for the wrong reason and this case would pass
//  while proving nothing - so for every key the removed pair is put BACK at the
//  front of the reduced document and the result must parse CP_OK again. That
//  round trip is what makes the refusal attributable to the missing key.
// -----------------------------------------------------------------------------

// Removes the top-level "key": <value> pair from `src`. Depth- and
// string-aware, so removing "sprite" does not stop at a comma inside its array.
// Writes the reduced document to `out` and the removed pair (without its
// separator) to `pair`. Returns false if the key is not top-level in `src`.
static bool body_without(char* out, size_t out_cap, char* pair, size_t pair_cap,
                         const char* src, const char* key)
{
  char needle[32];
  snprintf(needle, sizeof needle, "\"%s\":", key);
  const char* at = strstr(src, needle);
  if (at == nullptr) return false;

  const size_t start = (size_t)(at - src);          // the key's opening quote
  size_t i = start + strlen(needle);
  int    depth = 0;
  bool   in_str = false;
  for (; src[i] != '\0'; ++i) {
    const char ch = src[i];
    if (in_str) { if (ch == '"') in_str = false; continue; }
    if (ch == '"') { in_str = true; continue; }
    if (ch == '[' || ch == '{') { depth++; continue; }
    if (ch == ']' || ch == '}') {
      if (depth == 0) break;                        // the document's own '}'
      depth--; continue;
    }
    if (ch == ',' && depth == 0) break;
  }
  if (src[i] == '\0') return false;

  const size_t end = i;                             // ',' or the closing '}'
  if (end - start >= pair_cap) return false;
  memcpy(pair, src + start, end - start);
  pair[end - start] = '\0';

  // Drop the separator with the pair: the comma AFTER it when there is one, and
  // otherwise the comma BEFORE it (the pair was last in the object).
  size_t cut_from = start, cut_to = end;
  if (src[end] == ',') cut_to = end + 1;
  else                 cut_from = (start > 0 && src[start - 1] == ',') ? start - 1 : start;

  const size_t len = strlen(src);
  if (cut_from + (len - cut_to) + 1 > out_cap) return false;
  memcpy(out, src, cut_from);
  memcpy(out + cut_from, src + cut_to, len - cut_to);
  out[cut_from + (len - cut_to)] = '\0';
  return true;
}

TEST(every_required_key_is_required_on_its_own) {
  static const char* const KEYS[] = { "v", "name", "type", "base", "moves", "sprite" };

  CustomSpeciesRec c;
  uint16_t pin = 0;
  char reduced[2048], pair[512], restored[2048];

  for (size_t k = 0; k < sizeof KEYS / sizeof KEYS[0]; ++k) {
    CHECK(body_without(reduced, sizeof reduced, pair, sizeof pair,
                       VALID_BODY, KEYS[k]));
    // The surgery removed something, and something that names this key.
    CHECK(strlen(reduced) < strlen(VALID_BODY));
    CHECK(strstr(pair, KEYS[k]) != nullptr);
    CHECK(strstr(reduced, pair) == nullptr);

    // 1. WITHOUT IT: refused, by name.
    CHECK_EQ((int)parse_str(reduced, c, pin), (int)CP_MISSING_KEY);

    // 2. WITH IT BACK: accepted. This is what proves (1) is about the missing
    //    key and not about a document the helper mangled.
    const size_t np = strlen(pair), nr = strlen(reduced);
    CHECK(2u + np + nr <= sizeof restored);      // "{" + pair + "," + reduced+1
    restored[0] = '{';
    memcpy(restored + 1, pair, np);
    restored[1 + np] = ',';
    memcpy(restored + 2 + np, reduced + 1, nr);  // nr - 1 bytes plus the NUL
    CHECK_EQ((int)parse_str(restored, c, pin), (int)CP_OK);
  }
}

TEST(trailing_bytes_after_the_document_are_refused_rather_than_ignored) {
  CustomSpeciesRec c;
  uint16_t pin = 0;
  char buf[2048];
  snprintf(buf, sizeof buf, "%s ", VALID_BODY);
  CHECK_EQ((int)parse_str(buf, c, pin), (int)CP_OK);        // trailing whitespace is fine
  snprintf(buf, sizeof buf, "%s{}", VALID_BODY);
  CHECK_EQ((int)parse_str(buf, c, pin), (int)CP_TRAILING);
  snprintf(buf, sizeof buf, "%sx", VALID_BODY);
  CHECK_EQ((int)parse_str(buf, c, pin), (int)CP_TRAILING);
}

TEST(an_unknown_key_is_refused_and_a_duplicate_key_is_never_last_wins) {
  CustomSpeciesRec c;
  uint16_t pin = 0;
  char buf[2048];

  snprintf(buf, sizeof buf, "{\"v\":1,\"slot\":4,%s", VALID_BODY + BODY_AFTER_V);
  CHECK_EQ((int)parse_str(buf, c, pin), (int)CP_UNKNOWN_KEY);
  snprintf(buf, sizeof buf, "{\"v\":1,\"budget_used\":0,%s", VALID_BODY + BODY_AFTER_V);
  CHECK_EQ((int)parse_str(buf, c, pin), (int)CP_UNKNOWN_KEY);
  // A key longer than any the schema has is refused before it is compared,
  // which is what stops a 2 KB key being copied anywhere.
  snprintf(buf, sizeof buf, "{\"v\":1,\"aaaaaaaaaaaaaaaaaaaa\":0,%s", VALID_BODY + BODY_AFTER_V);
  CHECK_EQ((int)parse_str(buf, c, pin), (int)CP_UNKNOWN_KEY);

  // DUPLICATES. A last-wins reader is how a document passes one validator and
  // means something else to the next one.
  snprintf(buf, sizeof buf, "{\"v\":1,\"type\":1,%s", VALID_BODY + BODY_AFTER_V);
  CHECK_EQ((int)parse_str(buf, c, pin), (int)CP_DUP_KEY);
  snprintf(buf, sizeof buf, "{\"v\":1,\"v\":1,%s", VALID_BODY + BODY_AFTER_V);
  CHECK_EQ((int)parse_str(buf, c, pin), (int)CP_DUP_KEY);
}

TEST(a_value_of_the_wrong_kind_is_refused_before_it_is_interpreted) {
  CustomSpeciesRec c;
  uint16_t pin = 0;

  CHECK_EQ((int)parse_str("{\"v\":\"1\"}", c, pin), (int)CP_BAD_TYPE);
  CHECK_EQ((int)parse_str("{\"v\":true}", c, pin), (int)CP_BAD_TYPE);
  CHECK_EQ((int)parse_str("{\"v\":null}", c, pin), (int)CP_BAD_TYPE);
  CHECK_EQ((int)parse_str("{\"v\":{}}", c, pin), (int)CP_BAD_TYPE);
  CHECK_EQ((int)parse_str("{\"v\":[]}", c, pin), (int)CP_BAD_TYPE);
  CHECK_EQ((int)parse_str("{\"v\":-1}", c, pin), (int)CP_BAD_TYPE);
  CHECK_EQ((int)parse_str("{\"v\":1.5}", c, pin), (int)CP_SYNTAX);
  CHECK_EQ((int)parse_str("{\"name\":5}", c, pin), (int)CP_BAD_TYPE);
  CHECK_EQ((int)parse_str("{\"base\":5}", c, pin), (int)CP_BAD_TYPE);
  CHECK_EQ((int)parse_str("{\"sprite\":\"ab\"}", c, pin), (int)CP_BAD_TYPE);

  // CANONICAL DECIMALS ONLY: no leading zero, no more digits than the field
  // can hold. Bounding the digit COUNT before the accumulator can wrap is what
  // makes the arg_u32 overflow class unable to exist.
  CHECK_EQ((int)parse_str("{\"v\":01}", c, pin), (int)CP_NUMBER);
  CHECK_EQ((int)parse_str("{\"v\":000001}", c, pin), (int)CP_NUMBER);
  CHECK_EQ((int)parse_str("{\"v\":999999}", c, pin), (int)CP_NUMBER);
  CHECK_EQ((int)parse_str("{\"v\":4294971117}", c, pin), (int)CP_NUMBER);

  // THE WRAP CLASS ITSELF, ONE FIELD UP FROM THE RECORDED ONE. The deleted
  // arg_u32 accumulated into a uint32_t, so "4294971117" wrapped to 3821 and
  // authenticated as the PIN. This reader accumulates into a uint64_t, which
  // moves the wrap rather than removing it: 2^64 + 1 = 18446744073709551617 is
  // congruent to 1, so WITHOUT the digit-count bound it would be read as
  // version 1 and ACCEPTED. The ceiling check cannot catch it - the wrapped
  // value is inside every ceiling. Bounding the DIGIT COUNT before the multiply
  // is the only guard that can, which is why it is not cosmetic.
  CHECK_EQ((int)parse_str("{\"v\":18446744073709551617}", c, pin), (int)CP_NUMBER);
  CHECK_EQ((int)parse_str("{\"v\":18446744073709551616}", c, pin), (int)CP_NUMBER);
  CHECK_EQ((int)parse_str("{\"v\":2}", c, pin), (int)CP_VERSION);
  CHECK_EQ((int)parse_str("{\"v\":0}", c, pin), (int)CP_VERSION);
}

TEST(an_array_that_is_not_exactly_its_fixed_length_is_refused) {
  CustomSpeciesRec c;
  uint16_t pin = 0;
  CHECK_EQ((int)parse_str("{\"base\":[]}", c, pin), (int)CP_ARRAY_LEN);
  CHECK_EQ((int)parse_str("{\"base\":[1,2,3]}", c, pin), (int)CP_ARRAY_LEN);
  CHECK_EQ((int)parse_str("{\"base\":[1,2,3,4,5]}", c, pin), (int)CP_ARRAY_LEN);
  CHECK_EQ((int)parse_str("{\"moves\":[]}", c, pin), (int)CP_ARRAY_LEN);
  CHECK_EQ((int)parse_str("{\"moves\":[1,2,3,4,5]}", c, pin), (int)CP_ARRAY_LEN);
  CHECK_EQ((int)parse_str("{\"sprite\":[]}", c, pin), (int)CP_ARRAY_LEN);
}

TEST(a_sprite_frame_is_exactly_the_bytes_the_record_can_hold) {
  CustomSpeciesRec c;
  uint16_t pin = 0;
  char buf[4096];
  char frame[512];

  // One character short, one long, and one non-hex character - three codes'
  // worth of ways to be wrong, two of which are the same code by design.
  memset(frame, '0', sizeof frame);
  for (int delta = -1; delta <= 1; ++delta) {
    const int len = (int)(CS_SPRITE_BYTES * 2) + delta;
    snprintf(buf, sizeof buf,
             "{\"v\":1,\"name\":\"A\",\"type\":0,\"base\":[6,5,5,5],"
             "\"moves\":[1,6,32,34],\"sprite\":[\"%.*s\",\"%.*s\"]}",
             len, frame, (int)(CS_SPRITE_BYTES * 2), frame);
    const CpErr e = parse_str(buf, c, pin);
    if (delta == 0) CHECK_EQ((int)e, (int)CP_OK);
    else            CHECK_EQ((int)e, (int)CP_SPRITE_LEN);
  }

  memset(frame, '0', sizeof frame);
  frame[7] = 'g';
  snprintf(buf, sizeof buf,
           "{\"v\":1,\"name\":\"A\",\"type\":0,\"base\":[6,5,5,5],"
           "\"moves\":[1,6,32,34],\"sprite\":[\"%.*s\",\"%.*s\"]}",
           (int)(CS_SPRITE_BYTES * 2), frame, (int)(CS_SPRITE_BYTES * 2), frame);
  CHECK_EQ((int)parse_str(buf, c, pin), (int)CP_SPRITE_HEX);

  // Both cases of hex are accepted; case is not a security property.
  memset(frame, 'A', sizeof frame);
  snprintf(buf, sizeof buf,
           "{\"v\":1,\"name\":\"A\",\"type\":0,\"base\":[6,5,5,5],"
           "\"moves\":[1,6,32,34],\"sprite\":[\"%.*s\",\"%.*s\"]}",
           (int)(CS_SPRITE_BYTES * 2), frame, (int)(CS_SPRITE_BYTES * 2), frame);
  CHECK_EQ((int)parse_str(buf, c, pin), (int)CP_OK);
  CHECK_EQ((int)c.sprite[0][0], 0xAA);
}

TEST(the_name_is_utf8_in_and_latin1_out_and_says_so_when_it_cannot_convert) {
  CustomSpeciesRec c;
  uint16_t pin = 0;
  char buf[2048];
  #define NAMEBODY(lit) do { \
      snprintf(buf, sizeof buf, "{\"v\":1,\"name\":%s,%s", lit, \
               VALID_BODY + BODY_AFTER_NAME); \
    } while (0)

  // Two-byte UTF-8 inside Latin-1: n-tilde (C3 B1) and a-acute (C3 A1).
  NAMEBODY("\"Ni\xC3\xB1o\"");
  CHECK_EQ((int)parse_str(buf, c, pin), (int)CP_OK);
  CHECK_EQ((int)(uint8_t)c.name[2], 0xF1);
  CHECK_EQ((int)strlen(c.name), 4);

  // Three-byte UTF-8 is outside Latin-1 by definition (an em dash here).
  NAMEBODY("\"a\xE2\x80\x94\x62\"");
  CHECK_EQ((int)parse_str(buf, c, pin), (int)CP_UTF8);
  // A lone continuation byte, a truncated sequence and an overlong encoding.
  NAMEBODY("\"a\xB1\x62\"");
  CHECK_EQ((int)parse_str(buf, c, pin), (int)CP_UTF8);
  NAMEBODY("\"a\xC3\"");
  CHECK_EQ((int)parse_str(buf, c, pin), (int)CP_UTF8);
  NAMEBODY("\"\xC0\xAF\"");
  CHECK_EQ((int)parse_str(buf, c, pin), (int)CP_UTF8);
  // A Latin-1 codepoint the _tf fonts carry no glyph for.
  NAMEBODY("\"a\xC2\xA9\"");                    // (c) sign, U+00A9
  CHECK_EQ((int)parse_str(buf, c, pin), (int)CP_NAME_CHAR);

  // LENGTH IS IN LATIN-1 CHARACTERS, NOT UTF-8 BYTES. Twelve n-tildes are 24
  // bytes and a legal name; thirteen are not.
  char n12[64]; char n13[64];
  int o = 0; for (int i = 0; i < 12; ++i) { n12[o++] = (char)0xC3; n12[o++] = (char)0xB1; }
  n12[o] = '\0';
  o = 0; for (int i = 0; i < 13; ++i) { n13[o++] = (char)0xC3; n13[o++] = (char)0xB1; }
  n13[o] = '\0';
  snprintf(buf, sizeof buf, "{\"v\":1,\"name\":\"%s\",%s", n12,
           VALID_BODY + BODY_AFTER_NAME);
  CHECK_EQ((int)parse_str(buf, c, pin), (int)CP_OK);
  CHECK_EQ((int)strlen(c.name), 12);
  snprintf(buf, sizeof buf, "{\"v\":1,\"name\":\"%s\",%s", n13,
           VALID_BODY + BODY_AFTER_NAME);
  CHECK_EQ((int)parse_str(buf, c, pin), (int)CP_STRING_LEN);

  NAMEBODY("\"\"");
  CHECK_EQ((int)parse_str(buf, c, pin), (int)CP_STRING_LEN);   // a Pebble needs a name
  NAMEBODY("\" Bi\"");
  CHECK_EQ((int)parse_str(buf, c, pin), (int)CP_NAME_CHAR);    // refused, not trimmed
  NAMEBODY("\"Bi \"");
  CHECK_EQ((int)parse_str(buf, c, pin), (int)CP_NAME_CHAR);
  #undef NAMEBODY
}

TEST(a_string_may_not_carry_an_escape_a_control_byte_or_a_nul) {
  CustomSpeciesRec c;
  uint16_t pin = 0;
  char buf[2048];

  snprintf(buf, sizeof buf, "{\"v\":1,\"name\":\"a\\\\u0041\",%s",
           VALID_BODY + BODY_AFTER_NAME);
  CHECK_EQ((int)parse_str(buf, c, pin), (int)CP_SYNTAX);
  snprintf(buf, sizeof buf, "{\"v\":1,\"name\":\"a\tb\",%s",
           VALID_BODY + BODY_AFTER_NAME);
  CHECK_EQ((int)parse_str(buf, c, pin), (int)CP_SYNTAX);

  // AN EMBEDDED NUL IS NOT A TERMINATOR. A NUL-bounded reader would stop here
  // and validate a PREFIX of what arrived, handing the rest to nobody.
  {
    const char lit[] = "{\"v\":1,\"name\":\"a\0b\",\"type\":0}";
    const uint32_t n = (uint32_t)(sizeof lit - 1);
    CHECK_EQ((int)parse_bytes((const uint8_t*)lit, n, c, pin), (int)CP_NUL);
    // ...and the same bytes with the NUL taken as a terminator would have been
    // a perfectly ordinary "unterminated document" instead.
    CHECK(strlen(lit) < n);
  }
  // A NUL BETWEEN TOKENS is a syntax error, not an early end.
  {
    const char lit[] = "{\"v\":1\0,\"type\":0}";
    const uint32_t n = (uint32_t)(sizeof lit - 1);
    CHECK_EQ((int)parse_bytes((const uint8_t*)lit, n, c, pin), (int)CP_SYNTAX);
  }
}

TEST(deep_nesting_is_one_error_and_a_constant_amount_of_stack) {
  // THERE IS NO RECURSION IN THIS READER AND THERE IS NO DEPTH COUNTER EITHER.
  // Every value is read by a reader written for its own field and none of them
  // can read an object or an array that was not expected there, so ten thousand
  // open braces is a single CP_BAD_TYPE at the first one.
  CustomSpeciesRec c;
  uint16_t pin = 0;
  for (uint32_t depth = 1; depth <= 10000u; depth *= 10u) {
    const uint32_t n = 8u + depth;
    uint8_t* body = (uint8_t*)malloc(n);
    memcpy(body, "{\"v\":", 5);
    memset(body + 5, '{', depth);
    body[5 + depth] = '}';
    body[6 + depth] = '}';
    body[7 + depth] = '}';
    CHECK_EQ((int)cp_parse_species(body, n, c, pin), (int)CP_BAD_TYPE);
    free(body);
  }
  // The same shape with brackets, and with the nesting inside a field that
  // really is an array - which is where a naive reader would recurse.
  for (uint32_t depth = 1; depth <= 10000u; depth *= 10u) {
    const uint32_t n = 12u + depth;
    uint8_t* body = (uint8_t*)malloc(n);
    memcpy(body, "{\"base\":[", 9);
    memset(body + 9, '[', depth);
    body[9 + depth] = ']';
    body[10 + depth] = ']';
    body[11 + depth] = '}';
    const CpErr e = cp_parse_species(body, n, c, pin);
    CHECK(e == CP_BAD_TYPE || e == CP_ARRAY_LEN);
    free(body);
  }
}

// =============================================================================
//  4. FUZZ. IT MUST ERROR, NEVER CRASH, NEVER READ PAST THE BUFFER.
// =============================================================================

TEST(every_truncation_of_a_valid_document_errors_and_never_over_reads) {
  // Every prefix of a legal body, on the heap at exactly its own length. This
  // is the case a NUL-bounded reader fails: the truncated body has no
  // terminator at all.
  CustomSpeciesRec c;
  uint16_t pin = 0;
  const uint32_t full = (uint32_t)strlen(VALID_BODY);
  uint32_t parsed_ok = 0;
  for (uint32_t n = 0; n < full; ++n) {
    const CpErr e = parse_bytes((const uint8_t*)VALID_BODY, n, c, pin);
    if (e == CP_OK) parsed_ok++;
    CHECK(e != CP_OK);
  }
  CHECK_EQ((int)parsed_ok, 0);
  CHECK_EQ((int)parse_bytes((const uint8_t*)VALID_BODY, full, c, pin), (int)CP_OK);
}

TEST(byte_flip_fuzz_over_a_valid_document_never_produces_a_bad_record) {
  // Every single-byte substitution at every position, for a spread of byte
  // values: 256 x 400-odd bodies. THE INVARIANT IS NOT "it refuses" - some
  // flips produce a different LEGAL document - it is that CP_OK is only ever
  // returned for a record the validator then judges on its own terms, and that
  // no input reaches a state where the reader reads past the buffer.
  CustomSpeciesRec c;
  uint16_t pin = 0;
  const uint32_t full = (uint32_t)strlen(VALID_BODY);
  uint32_t accepted = 0, refused = 0;

  for (uint32_t pos = 0; pos < full; ++pos) {
    for (int v = 0; v < 256; v += 17) {
      uint8_t* body = (uint8_t*)malloc(full);
      memcpy(body, VALID_BODY, full);
      body[pos] = (uint8_t)v;
      const CpErr e = cp_parse_species(body, full, c, pin);
      if (e == CP_OK) {
        accepted++;
        // A document the reader accepts is still only a DEFINITION: the game
        // rules are the validator's, and it must always answer with a NAMED
        // code rather than crashing on a record the reader let through.
        const VReject r = validate_custom_species(c);
        CHECK((int)r >= 0 && (int)r < (int)VR_REJECT_COUNT);
      } else {
        refused++;
        CHECK((int)e > 0 && (int)e < (int)CP_ERR_COUNT);
      }
      free(body);
    }
  }
  // Both arms were exercised - without this the case would pass if the reader
  // refused everything, which is this project's recurring defect.
  CHECK(accepted > 0);
  CHECK(refused > 100u);
}

TEST(random_bodies_error_and_never_crash) {
  // Seeded, so a failure reproduces from the seed alone (plan section 4.2).
  CustomSpeciesRec c;
  uint16_t pin = 0;
  Rng r;
  rng_init(r, nt_seed() ^ 0x8BADF00Du);
  uint32_t ok_count = 0;

  for (uint32_t trial = 0; trial < 4000u; ++trial) {
    const uint32_t n = 1u + (rng_next(r) % 300u);
    uint8_t* body = (uint8_t*)malloc(n);
    for (uint32_t i = 0; i < n; ++i) {
      const uint32_t k = rng_next(r) % 8u;
      // A mix of raw bytes and structural characters, so the reader is driven
      // through its own branches rather than only through "not a document".
      static const char punct[] = "{}[]\":,0123456789vname sprite";
      body[i] = (k < 3u) ? (uint8_t)(rng_next(r) & 0xFFu)
                         : (uint8_t)punct[rng_next(r) % (sizeof punct - 1u)];
    }
    const CpErr e = cp_parse_species(body, n, c, pin);
    CHECK((int)e < (int)CP_ERR_COUNT);
    if (e == CP_OK) {
      ok_count++;
      const VReject vr = validate_custom_species(c);
      CHECK((int)vr < (int)VR_REJECT_COUNT);
    }
    free(body);
  }
  (void)ok_count;
}

TEST(a_hostile_body_of_every_length_up_to_the_cap_is_bounded) {
  // The cap and the reader together, at the sizes where an off-by-one would
  // live: the chunk boundary, the cap itself, and one byte either side.
  CustomSpeciesRec c;
  uint16_t pin = 0;
  const uint32_t sizes[] = {
    1u, 2u,
    (uint32_t)CORE_RAW_BUFLEN - 1u, (uint32_t)CORE_RAW_BUFLEN,
    (uint32_t)CORE_RAW_BUFLEN + 1u,
    (uint32_t)CS_BODY_MAX - 1u, (uint32_t)CS_BODY_MAX,
  };
  for (uint32_t s = 0; s < sizeof sizes / sizeof sizes[0]; ++s) {
    const uint32_t n = sizes[s];
    uint8_t* body = (uint8_t*)malloc(n);
    memset(body, '{', n);
    CreatorBody b;
    feed_body(b, body, n, (int)n);
    CHECK(cb_ready(b));
    CHECK_EQ((int)cb_len(b), (int)n);
    const CpErr e = cp_parse_species(cb_data(b), cb_len(b), c, pin);
    CHECK(e != CP_OK);
    free(body);
  }
}

// =============================================================================
//  5. THE CLOCK DOCUMENT
// =============================================================================

TEST(the_time_document_reads_a_thirty_two_bit_epoch_and_nothing_wider) {
  uint32_t epoch = 0; uint16_t pin = 0;
  #define TIME(s) cp_parse_time((const uint8_t*)(s), (uint32_t)strlen(s), epoch, pin)

  CHECK_EQ((int)TIME("{\"v\":1,\"epoch\":1767225600}"), (int)CP_OK);
  CHECK_EQ((long)epoch, 1767225600L);
  CHECK_EQ((int)pin, 0);

  CHECK_EQ((int)TIME("{\"v\":1,\"pin\":\"0421\",\"epoch\":1}"), (int)CP_OK);
  CHECK_EQ((int)pin, 421);
  CHECK_EQ((long)epoch, 1L);

  // The full 32-bit range is readable, and one past it is not - the widest
  // number in the upload document is 16 bits, which is why this is a separate
  // reader rather than one with a widened accumulator.
  CHECK_EQ((int)TIME("{\"v\":1,\"epoch\":4294967295}"), (int)CP_OK);
  CHECK_EQ((unsigned long)epoch, 4294967295UL);
  CHECK_EQ((int)TIME("{\"v\":1,\"epoch\":4294967296}"), (int)CP_NUMBER);
  CHECK_EQ((int)TIME("{\"v\":1,\"epoch\":99999999999}"), (int)CP_NUMBER);
  // The same wrap, against the wider field: 2^64 + 1767225600 is congruent to a
  // perfectly plausible epoch, and only the ten-digit bound refuses it.
  CHECK_EQ((int)TIME("{\"v\":1,\"epoch\":18446744075476777216}"), (int)CP_NUMBER);
  CHECK_EQ((int)TIME("{\"v\":1,\"epoch\":-1}"), (int)CP_BAD_TYPE);

  CHECK_EQ((int)TIME("{\"v\":1}"), (int)CP_MISSING_KEY);
  CHECK_EQ((int)TIME("{\"epoch\":1}"), (int)CP_MISSING_KEY);
  CHECK_EQ((int)TIME("{\"v\":2,\"epoch\":1}"), (int)CP_VERSION);
  CHECK_EQ((int)TIME("{\"v\":1,\"name\":\"x\",\"epoch\":1}"), (int)CP_UNKNOWN_KEY);
  CHECK_EQ((int)TIME("{\"v\":1,\"epoch\":1,\"epoch\":2}"), (int)CP_DUP_KEY);
  CHECK_EQ((int)TIME("{\"v\":1,\"epoch\":1}x"), (int)CP_TRAILING);
  CHECK_EQ((int)TIME(""), (int)CP_EMPTY);
  #undef TIME

  // And the same truncation sweep the upload gets.
  const char* full = "{\"v\":1,\"pin\":\"0421\",\"epoch\":1767225600}";
  for (uint32_t n = 0; n < (uint32_t)strlen(full); ++n) {
    HeapBody hb((const uint8_t*)full, n);
    CHECK(cp_parse_time(hb.p, hb.n, epoch, pin) != CP_OK);
  }
}

TEST(every_reader_code_has_a_distinct_english_name_and_the_lookup_is_total) {
  for (int a = 0; a < (int)CP_ERR_COUNT; ++a) {
    CHECK(cp_err_name((CpErr)a) != nullptr);
    for (int b = a + 1; b < (int)CP_ERR_COUNT; ++b)
      CHECK(strcmp(cp_err_name((CpErr)a), cp_err_name((CpErr)b)) != 0);
  }
  CHECK_STR_EQ(cp_err_name((CpErr)CP_ERR_COUNT), "CP_?");
  CHECK_STR_EQ(cp_err_name((CpErr)200), "CP_?");
}

// =============================================================================
//  6. THE SERVED SCHEMA AGREES WITH THE COMPILED ONE (plan T4)
//
//  data/creator_schema_json.h's static_asserts pin its DEFINES to the compiled
//  constants; nothing but reading the document itself can pin the TEXT, and two
//  emitters in one generator can agree with each other's defines and disagree
//  with the bytes they emit.
// =============================================================================

static bool json_has(const char* needle)
{
  return strstr(CREATOR_SCHEMA_JSON, needle) != nullptr;
}

TEST(the_served_schema_document_carries_the_compiled_numbers) {
  char buf[256];

  snprintf(buf, sizeof buf, "\"v\":%d", (int)CREATOR_API_VERSION);
  CHECK(json_has(buf));
  snprintf(buf, sizeof buf, "\"types\":%d", (int)TYPE_COUNT);
  CHECK(json_has(buf));
  snprintf(buf, sizeof buf, "\"stat\":{\"min\":%d,\"max\":%d,\"lo\":%d,\"hi\":%d}",
           (int)CREATOR_STAT_POINTS_MIN, (int)CREATOR_TOTAL_STAT_POINTS,
           (int)CREATOR_BASE_STAT_MIN, (int)CREATOR_BASE_STAT_MAX);
  CHECK(json_has(buf));
  snprintf(buf, sizeof buf, "\"atkbudget\":%d", (int)CREATOR_ATTACK_BUDGET);
  CHECK(json_has(buf));
  snprintf(buf, sizeof buf, "\"powcap\":%d", (int)CREATOR_POWER_CAP_BY_STAGE[1]);
  CHECK(json_has(buf));
  snprintf(buf, sizeof buf, "\"moves\":%d", (int)CREATOR_MOVE_COUNT);
  CHECK(json_has(buf));
  snprintf(buf, sizeof buf, "\"sprite\":{\"w\":%d,\"h\":%d,\"f\":%d,\"bytes\":%d}",
           (int)CS_SPRITE_W, (int)CS_SPRITE_H, (int)CS_SPRITE_FRAMES,
           (int)CS_SPRITE_BYTES);
  CHECK(json_has(buf));
  snprintf(buf, sizeof buf, "\"name\":{\"max\":%d}", (int)NAME_MAX_LEN);
  CHECK(json_has(buf));
  snprintf(buf, sizeof buf, "\"id\":{\"min\":%d,\"max\":%d,\"slots\":%d}",
           (int)CREATOR_SPECIES_ID_MIN, (int)CREATOR_SPECIES_ID_MAX,
           (int)CREATOR_SPECIES_SLOTS);
  CHECK(json_has(buf));

  // EVERY ATTACK ROW, positionally, against the compiled table. This is the
  // half that would catch a generator whose defines are right and whose rows
  // are stale - the page prices a Pebble from a[4] and the device prices it
  // from AttackDef.budget_cost, and a disagreement there is a user who is shown
  // 68 % and refused at 101 %.
  for (uint8_t i = 0; i < ATTACK_COUNT; ++i) {
    const AttackDef& a = ATTACKS_TABLE[i];
    snprintf(buf, sizeof buf, "[%d,%d,%d,%d,%d]",
             (int)a.id, (int)a.type, (int)a.power, (int)a.accuracy,
             (int)a.budget_cost);
    if (!json_has(buf)) fprintf(stderr, "    missing attack row %s\n", buf);
    CHECK(json_has(buf));
  }
  // And no row for an attack that does not exist.
  snprintf(buf, sizeof buf, "[%d,", (int)(ATTACK_COUNT + 1));
  CHECK(!json_has(buf));

  CHECK_EQ((int)CREATOR_SCHEMA_JSON_LEN, (int)strlen(CREATOR_SCHEMA_JSON));
  CHECK(CREATOR_SCHEMA_JSON_LEN < (size_t)WEB_HTML_MAX);
}

// -----------------------------------------------------------------------------
//  THE NAMES THE PAGE DRAWS (P8-C4).
//
//  The ATTACKS screen lists 34 attacks by name, and the only alternative to
//  serving them was 34 Spanish strings typed into web/creator/app.js - a second
//  source of truth no gate can see drift in, which is the whole thing this
//  document exists to prevent. So they are served, and this is the case that
//  checks the two ends against each other.
//
//  THE COMPARISON IS BY CODEPOINT, IN BOTH DIRECTIONS OF AN ENCODING CHANGE.
//  core/strings_es.h holds "R\u00e1faga" as UTF-8 bytes; the served document
//  holds it as the JSON escape \u00e1, because the document must stay printable
//  ASCII. Comparing the two as byte strings would fail on every accented name
//  while both are perfectly correct, so each side is decoded to codepoints and
//  those are what is compared.
// -----------------------------------------------------------------------------

// One UTF-8 sequence out of a C string. Returns the codepoint and advances.
// Bounded by the NUL; the input is a compiled literal, not a network buffer.
static uint32_t utf8_next(const char*& p)
{
  const uint8_t b = (uint8_t)*p++;
  if (b < 0x80u) return b;
  if ((b & 0xE0u) == 0xC0u) {
    const uint8_t b2 = (uint8_t)*p++;
    return (uint32_t)((b & 0x1Fu) << 6) | (uint32_t)(b2 & 0x3Fu);
  }
  if ((b & 0xF0u) == 0xE0u) {
    const uint8_t b2 = (uint8_t)*p++, b3 = (uint8_t)*p++;
    return (uint32_t)((b & 0x0Fu) << 12) | (uint32_t)((b2 & 0x3Fu) << 6) |
           (uint32_t)(b3 & 0x3Fu);
  }
  return 0xFFFDu;
}

// The nth JSON string of the array that starts at `key`, decoded to codepoints.
// Deliberately a small hand reader rather than a JSON parser: a parser here
// would be a second implementation of the thing under test.
static bool json_array_string(const char* key, int index, uint32_t* out,
                              int cap, int& out_len)
{
  const char* p = strstr(CREATOR_SCHEMA_JSON, key);
  out_len = 0;
  if (!p) return false;
  p += strlen(key);
  for (int k = 0; k < index; ++k) {
    p = strchr(p, ',');                 // one comma between entries
    if (!p) return false;
    ++p;
  }
  while (*p && *p != '"') {
    if (*p == ']') return false;
    ++p;
  }
  if (*p != '"') return false;
  ++p;
  while (*p && *p != '"') {
    if (out_len >= cap) return false;
    if (p[0] == '\\' && p[1] == 'u') {
      unsigned v = 0u;
      if (sscanf(p + 2, "%4x", &v) != 1) return false;
      out[out_len++] = (uint32_t)v;
      p += 6;
    } else {
      out[out_len++] = (uint32_t)(uint8_t)*p++;
    }
  }
  return *p == '"';
}

TEST(the_served_schema_names_every_attack_the_page_can_offer) {
  // ONE NAME PER ROW, IN THE TABLE'S ORDER. The page pairs an[k] with atk[k]
  // positionally rather than by id, so a shifted array would label every attack
  // with its neighbour's name and nothing else in the tree would notice.
  for (uint8_t i = 0; i < ATTACK_COUNT; ++i) {
    uint32_t served[64];
    int n = 0;
    if (!json_array_string("\"an\":[", (int)i, served, 64, n)) {
      fprintf(stderr, "    no served name at index %d\n", (int)i);
      CHECK(false);
      continue;
    }
    const char* want = ES[ATTACKS_TABLE[i].name_idx];
    const char* w = want;
    int k = 0;
    bool same = true;
    while (*w) {
      const uint32_t cp = utf8_next(w);
      if (k >= n || served[k] != cp) { same = false; break; }
      ++k;
    }
    if (same && k != n) same = false;
    if (!same)
      fprintf(stderr, "    attack %d: served name is not %s\n",
              (int)ATTACKS_TABLE[i].id, want);
    CHECK(same);
  }

  // And no thirty-fifth name for an attack that does not exist.
  uint32_t extra[64];
  int n = 0;
  CHECK(!json_array_string("\"an\":[", (int)ATTACK_COUNT, extra, 64, n));

  // THE TYPE NAMES, whose INDEX is the type ordinal the rows carry. NEUTRAL is
  // last at index TYPE_COUNT, which is the relation the page's own pool filter
  // reads (`a[1] === M.type || a[1] === S.types`) and the relation
  // game/validate.cpp relies on when it refuses a SPECIES of type >= TYPE_COUNT
  // while accepting an ATTACK of type TYPE_NEUTRAL.
  static const char* const TN[] = { "SIGNAL", "CORRUPT", "SYSTEM", "NEUTRAL" };
  for (int i = 0; i < 4; ++i) {
    uint32_t got[16];
    int m = 0;
    CHECK(json_array_string("\"tn\":[", i, got, 16, m));
    CHECK_EQ(m, (int)strlen(TN[i]));
    bool same = (m == (int)strlen(TN[i]));
    for (int k = 0; same && k < m; ++k) same = (got[k] == (uint32_t)TN[i][k]);
    CHECK(same);
  }
  CHECK_EQ((int)TYPE_NEUTRAL, 3);
  CHECK_EQ((int)TYPE_COUNT, 3);
}

// =============================================================================
//  7. END TO END, MINUS THE SOCKET
// =============================================================================

static GameState g_box_state;

static void box_fixture(void)
{
  memset(&g_box_state, 0, sizeof g_box_state);
  for (uint8_t i = 0; i < (uint8_t)BOX_SLOTS; ++i) {
    g_box_state.pebbles[i].magic      = (uint16_t)PEBBLE_MAGIC;
    g_box_state.pebbles[i].layout_ver = (uint8_t)PEBBLE_LAYOUT_VER;
  }
  g_box_state.box.magic           = (uint16_t)BOX_MAGIC;
  g_box_state.box.active_slot     = (uint8_t)BOX_ACTIVE_NONE;
  g_box_state.box.next_id_counter = 1;
  g_box_state.cfg.device_id       = 0xB0FFE503u;
  box_bind(g_box_state);
}

TEST(an_uploaded_document_becomes_a_pebble_the_one_validator_accepts) {
  // THE WHOLE PIPELINE creator_server.cpp runs, with the socket replaced by
  // feed_body() and nothing else replaced at all: the same accumulator, the
  // same reader, the same pricing, the same validator, the same registry and
  // the same Box constructor the firmware calls.
  csp_reset();
  box_fixture();
  rng_seed_all(0xC0FFEEu);

  CreatorBody b;
  HeapBody hb(VALID_BODY);
  feed_body(b, hb.p, hb.n, (int)hb.n);
  CHECK(cb_ready(b));

  CustomSpeciesRec rec;
  uint16_t pin = 0;
  CHECK_EQ((int)cp_parse_species(cb_data(b), cb_len(b), rec, pin), (int)CP_OK);

  uint16_t stat_used = 0, attack_used = 0;
  creator_cost_of(rec, stat_used, attack_used);
  rec.budget_used = attack_used;
  rec.slot = csp_free_slot();
  CHECK_EQ((int)rec.slot, 0);
  CHECK_EQ((int)validate_custom_species(rec), (int)VR_OK);
  CHECK_EQ((int)creator_power_pct(stat_used, attack_used), 72);   // Appendix C

  CHECK(csp_install(rec));
  const uint8_t species_id = csp_species_id(rec.slot);
  const uint8_t slot = box_new_pebble(species_id, 1u, (uint8_t)ORIGIN_CREATOR,
                                      genome_genesis(), 0xC0FFEEu, 1700000000u);
  CHECK(slot != (uint8_t)BOX_SLOT_NONE);
  PebbleInstance* p = box_slot(slot);
  CHECK(p != nullptr);
  if (p == nullptr) { csp_reset(); return; }
  p->flags = (uint8_t)(p->flags | PBF_CUSTOM | PBF_HAS_CUSTOM_SPRITE);
  p->custom_sprite = rec.slot;
  memset(p->nickname, 0, sizeof p->nickname);
  memcpy(p->nickname, rec.name, strlen(rec.name));

  const VReject r = validate_pebble(*p);
  if (r != VR_OK) fprintf(stderr, "    pipeline -> %s\n", validate_reject_name(r));
  CHECK_EQ((int)r, (int)VR_OK);
  CHECK_EQ((int)p->origin, (int)ORIGIN_CREATOR);
  CHECK_STR_EQ(p->nickname, "Bicho");
  CHECK_EQ((int)p->species_id, (int)CREATOR_SPECIES_ID_MIN);

  // *** AND THE ROSTER STILL HAS NO NAME FOR IT. Final review. ***
  // ui/pet_art.h promises pet_species_name() answers "nullptr - never a
  // placeholder" for three inputs, one of which is "a creator custom
  // (200..209)". With a row actually INSTALLED that was false: csp_install()
  // sets name_idx = STR_EMPTY ("the name lives on the PebbleInstance's
  // nickname, not in a StrId"), species_get(200) therefore returns a real row,
  // and S(STR_EMPTY) is the EMPTY STRING - which is not nullptr.
  //
  // tests/test_pet_view.cpp already asserted `pet_species_name(200) == nullptr`
  // and passed, because it asserts it with NO custom species installed, i.e. in
  // the one state where the defect cannot occur. This is the same claim in the
  // state that reaches it, and it belongs here because this is the only place
  // in the suite that gets a real record past the validator.
  //
  // What it cost: ui/ui.cpp's name ladder took rung 2 with a zero-length
  // string, so the dynasty fallback at rung 3 was unreachable for exactly the
  // creature the phone block mints. An unnamed device carrying one produced ""
  // for its own name; disc_encode() ACCEPTS that (name_ok() is true for an
  // all-zero field), so the board beaconed twelve zero bytes, the far board's
  // LINK row fell back to "Buscando Pebbles..." and its card header to
  // "ENLACE", and two such devices were indistinguishable on the air.
  CHECK(species_get(CREATOR_SPECIES_ID_MIN) != nullptr);   // the row IS there
  if (pet_species_name(CREATOR_SPECIES_ID_MIN) != nullptr)
    fprintf(stderr, "    pet_species_name(%u) = \"%s\", not nullptr: ui.cpp's "
                    "name ladder takes rung 2 with an empty string and the "
                    "device beacons no name at all\n",
            (unsigned)CREATOR_SPECIES_ID_MIN,
            pet_species_name(CREATOR_SPECIES_ID_MIN));
  CHECK(pet_species_name(CREATOR_SPECIES_ID_MIN) == nullptr);

  csp_reset();
}

TEST(a_full_box_and_a_full_registry_are_asked_about_before_anything_is_written) {
  // game/capture.h's rule, applied here: the Box is checked BEFORE the attempt
  // is spent, "if the Box is full the player must decide". The server answers
  // 409 and writes NOTHING - not the cs record either, or an abandoned upload
  // would leak a cs slot the user has no way to reclaim.
  csp_reset();
  box_fixture();

  CustomSpeciesRec rec;
  uint16_t pin = 0;
  CHECK_EQ((int)parse_str(VALID_BODY, rec, pin), (int)CP_OK);
  uint16_t s = 0, a = 0;
  creator_cost_of(rec, s, a);
  rec.budget_used = a;

  // Fill the Box with built-ins, then confirm the constructor refuses.
  for (uint8_t i = 0; i < (uint8_t)BOX_SLOTS; ++i) {
    Genome g; memset(&g, 0, sizeof g);
    g.lineage_id = 0x100u + i; g.generation = 1u; genome_seal(g);
    CHECK(box_new_pebble(1u, 5u, (uint8_t)ORIGIN_WILD, g, 1u + i, 1000u)
          != (uint8_t)BOX_SLOT_NONE);
  }
  CHECK_EQ((int)box_count(), (int)BOX_SLOTS);
  rec.slot = csp_free_slot();
  CHECK(csp_install(rec));
  CHECK_EQ((int)box_new_pebble(csp_species_id(rec.slot), 1u,
                               (uint8_t)ORIGIN_CREATOR, genome_genesis(),
                               1u, 1000u),
           (int)BOX_SLOT_NONE);
  // The Box is byte-identical to before the refused attempt: nothing was
  // consumed and nothing was overwritten.
  CHECK_EQ((int)box_count(), (int)BOX_SLOTS);

  // The registry's own full case.
  csp_reset();
  for (uint8_t i = 0; i < (uint8_t)CREATOR_SPECIES_SLOTS; ++i) {
    rec.slot = i;
    CHECK(csp_install(rec));
  }
  CHECK_EQ((int)csp_free_slot(), (int)CSP_SLOT_NONE);
  csp_reset();
}
