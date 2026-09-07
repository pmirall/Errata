// =============================================================================
//  PEBBLEBOL - networking/creator_parse.h
//  THE FIXED-SCHEMA REQUEST BODY READER (P8-C3, spec section 38).
//
//  THERE IS NO JSON LIBRARY IN THIS FIRMWARE AND THIS IS NOT ONE. It reads
//  EXACTLY the two documents the creator page is allowed to send and refuses
//  everything else with a named code. That is a deliberate inversion of how a
//  parser is usually written: a general reader accepts a document and then asks
//  a validator what is in it, so every field the validator forgot is a field
//  the device accepted. Here the SHAPE is the schema - a key that is not one of
//  the six is CP_UNKNOWN_KEY at the character it appears, and a value of the
//  wrong kind is CP_BAD_TYPE before a byte of it is interpreted.
//
//  ==========================================================================
//  THERE IS NO RECURSION AND THERE IS NO DEPTH
//  ==========================================================================
//  "Deep nesting" is a whole class of parser bug that this module cannot have,
//  by construction rather than by a depth counter: every value is read by a
//  reader written for its own field, and NONE of them can read an object or an
//  array that was not expected there. `{` where a number belongs is CP_BAD_TYPE
//  at that character. So a body of ten thousand open braces is one error and a
//  constant amount of stack, and tests/test_creator_api.cpp asserts exactly
//  that rather than trusting this paragraph.
//
//  ==========================================================================
//  LENGTH-BOUNDED, NEVER NUL-BOUNDED
//  ==========================================================================
//  Every read takes (body, len) and every cursor is compared against len before
//  it is dereferenced. The body may legally contain a NUL - a client can send
//  one - and a NUL-terminated reader would silently validate a PREFIX of what
//  arrived and hand the rest to nobody. A NUL inside a string is CP_NUL; a NUL
//  between tokens is CP_SYNTAX. Neither is ever a terminator.
//
//  ==========================================================================
//  DUPLICATE KEYS ARE AN ERROR, NOT A LAST-WINS
//  ==========================================================================
//  {"type":0,"type":1} is CP_DUP_KEY. Last-wins parsers are how a document
//  passes one reader's validation and means something else to the next one, and
//  this device has two readers of the same document (this one and the page's).
//
//  ==========================================================================
//  WHAT THE ACCEPTED DOCUMENTS ARE
//  ==========================================================================
//  1. THE UPLOAD, for POST /api/validate and POST /api/pebble:
//
//       {"v":1,"pin":"1234","name":"Bicho","type":0,
//        "base":[5,6,6,5],"moves":[1,6,7,27],
//        "sprite":["<144 hex>","<144 hex>"]}
//
//     "pin" is OPTIONAL and everything else is required, in any order, once
//     each. "v" must equal CREATOR_API_VERSION. The sprite frames are exactly
//     2 * CS_SPRITE_BYTES hex characters each - a 24x24 XBM at 3 B per row -
//     and the palette rule of spec section 35 is satisfied by construction: XBM
//     is one bit per pixel, so there is no palette to bound.
//
//     THE SLOT IS NOT IN THE DOCUMENT AND MUST NEVER BE. A page-chosen cs slot
//     is an attacker-chosen one, and writing cs4 would silently change the
//     species of a Pebble already in the Box. The DEVICE picks the slot and
//     tells the page which one it used.
//
//     NEITHER IS budget_used. It is RECOMPUTED on the device from the moves and
//     the stats; a number the page sends is a number the page can lie about.
//
//  2. THE CLOCK, for POST /api/time:
//
//       {"v":1,"pin":"1234","epoch":1767225600}
//
//     A separate reader because `epoch` is a 32-bit value and every number in
//     the upload is bounded at 16 bits. One reader with a widened accumulator
//     would have relaxed the upload's bounds to serve the clock's.
//
//  PURE MODULE. stdint, string.h, the save schema and the generated content
//  headers. No Arduino, no heap, no float, no clock, no RNG, no I/O, and no
//  file-scope state at all: the output is the caller's struct. It is on
//  tools/check.sh's PURE_NET and PURE_NET_CPP lists.
//
//  IT DECIDES NO GAME RULE. Whether four moves are legal, whether the stats are
//  inside the section 36 budget and whether the name is a name at all are
//  game/validate.h's; this file only decides whether the bytes are the shape of
//  a document. The name is the one place the two meet, and the seam is drawn so
//  that no rule is duplicated: the UTF-8 -> Latin-1 CONVERSION happens here,
//  because that is where a codepoint stops being representable, and the
//  question "is this Latin-1 byte one the panel can draw" is asked of
//  game/validate.h's creator_name_char_ok() - the same predicate
//  validate_custom_species() asks of a record read back from flash.
//
//  All identifiers and comments English.
// =============================================================================
#ifndef PB_CREATOR_PARSE_H
#define PB_CREATOR_PARSE_H

#include <stdint.h>

#include "../persistence/save_schema.h"   // CustomSpeciesRec, CS_* capacities

// -----------------------------------------------------------------------------
// Why a body was refused. NEVER a bool: a test written against `!= CP_OK` still
// passes when the wrong guard fires, which is the same argument game/battle.h
// and game/validate.h make for their reject enums.
// -----------------------------------------------------------------------------
enum CpErr : uint8_t {
  CP_OK = 0,
  CP_EMPTY,          // no bytes at all
  CP_SYNTAX,         // not the shape of a document: a byte where none belongs
  CP_TRAILING,       // bytes after the closing brace
  CP_UNKNOWN_KEY,    // a key the schema does not have
  CP_DUP_KEY,        // the same key twice
  CP_MISSING_KEY,    // a required key absent
  CP_BAD_TYPE,       // the right key, the wrong kind of value
  CP_NUMBER,         // not a canonical unsigned decimal, or out of range
  CP_ARRAY_LEN,      // an array that is not exactly the length the schema fixes
  CP_STRING_LEN,     // a string longer than its field can hold
  CP_NUL,            // an embedded NUL inside a string
  CP_UTF8,           // not valid UTF-8, or a codepoint outside Latin-1
  CP_NAME_CHAR,      // a character the device's font cannot draw
  CP_SPRITE_LEN,     // a sprite frame that is not exactly 2*CS_SPRITE_BYTES chars
  CP_SPRITE_HEX,     // a non-hex character inside a sprite frame
  CP_VERSION,        // "v" is not CREATOR_API_VERSION
  CP_ERR_COUNT
};

// The English name of a code, for the response body, the event log and the host
// tests. TOTAL: an out-of-range value answers "CP_?" rather than indexing past
// the table.
const char* cp_err_name(CpErr e);

// -----------------------------------------------------------------------------
// cp_parse_species() - the upload.
//
//  `out` is FULLY WRITTEN on CP_OK and left in a defined, refusable state on
//  every other path (it is zeroed first, so a caller that ignores the return
//  value gets a record validate_custom_species() refuses rather than a
//  half-filled one). magic and version are stamped; slot, budget_used and
//  reserved[] are left ZERO on purpose - the device fills the first two and the
//  third must stay zero.
//
//  `pin_out` receives the body's "pin" field, or 0 when it was absent. 0 is the
//  ConfigV2 "no PIN issued" sentinel and is never a live PIN, so "absent" and
//  "wrong" cannot be confused. THE HEADER PIN WINS WHERE BOTH ARE PRESENT; see
//  creator_server.h for the ordering and why it is that way round.
// -----------------------------------------------------------------------------
CpErr cp_parse_species(const uint8_t* body, uint32_t len,
                       CustomSpeciesRec& out, uint16_t& pin_out);

// -----------------------------------------------------------------------------
// cp_parse_time() - the clock. `epoch_out` receives the UTC epoch the phone
// claims; hardware/gametime.h's gt_set_epoch() decides whether to believe it,
// and this reader deliberately does no sanity check of its own - a second
// opinion about what a plausible date is would be a second rule that can drift
// from the first.
// -----------------------------------------------------------------------------
CpErr cp_parse_time(const uint8_t* body, uint32_t len,
                    uint32_t& epoch_out, uint16_t& pin_out);

// -----------------------------------------------------------------------------
//  Compile-time sanity on the shapes this module contracts against.
// -----------------------------------------------------------------------------
static_assert(CS_SPRITE_FRAMES == 2, "the sprite array is exactly two frames");
static_assert(CS_BASE_COUNT == 4, "base[] is exactly hp, atk, def, spd");
static_assert(CS_NAME_CAP == 13, "the name field is 12 characters and a NUL");

#endif  // PB_CREATOR_PARSE_H
