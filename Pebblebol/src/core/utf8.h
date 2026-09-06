// =============================================================================
//  PEBBLEBOL - core/utf8.h
//  THE CODEPOINT RULE. One implementation, three consumers, host-tested.
//
//  WHY THIS FILE EXISTS (P10-C4, spec section 65 "long nicknames truncated on
//  codepoint boundaries"). Before it there were THREE copies of "how long is
//  the sequence starting here" and they did not agree:
//
//    ui/render.cpp        u8_seq_len()  - the device's draw and wrap path
//    tests/fakes/gfx_fb.cpp seq_len()   - the host's, which every golden uses
//    game/pebble.cpp      utf8_fit()    - the name joiner
//
//  All three read the LEAD byte and trusted it. `p += 4` on a lead byte whose
//  continuation bytes are not there walks straight past the NUL terminator:
//  AddressSanitizer caught it in the host fake (heap-buffer-overflow, READ of
//  size 1, one byte past a 5-byte "Ni\xF1o"), and ui/render.cpp's
//  rd_text_wrap() has the identical `p += u8_seq_len(*p)` step over a buffer
//  the firmware owns.
//
//  AND THE INPUT THAT REACHES IT IS NOT HYPOTHETICAL. A nickname is stored as
//  RAW LATIN-1 - networking/creator_parse.cpp's cp_name_from_utf8() says so in
//  its own banner ("UTF-8 in, Latin-1 out"), game/validate.cpp's
//  creator_name_char_ok() admits 0xF1 as a bare byte, and
//  networking/discovery.cpp accepts the same range off the air - while
//  core/strings_es.h and the species names are UTF-8. Both then go to
//  drawUTF8(). So "Nino" with an n-tilde is one 0xF1 byte where the roster's
//  "Rafagon" is two, and the decoder that reads them is the same one.
//
//  THE SEAM IS THEREFORE EXPLICIT NOW: stored names are LATIN-1, drawn text is
//  UTF-8, and u8_from_latin1() is the only crossing. Nothing draws a stored
//  name directly any more.
//
//  PURE. stdint and nothing else: no Arduino, no gfx, no strings. Linked into
//  the firmware AND every host binary that draws, and gated onto the
//  Arduino-free red line by tools/check.sh.
// =============================================================================
#ifndef PB_UTF8_H
#define PB_UTF8_H

#include <stdint.h>

// The length in BYTES of the UTF-8 sequence beginning at s, VALIDATED.
//
//   0  s is NULL or points at the terminator.
//   1  a plain ASCII byte, OR any byte that does not begin a well-formed
//      sequence - a stray continuation byte, an over-long lead, or a lead whose
//      continuation bytes are missing or are not continuation bytes.
//   2..4 a well-formed sequence, every continuation byte present before the
//      terminator and every one of them 10xxxxxx.
//
// The "1" answer is what makes every walk in this project terminate: advancing
// by one byte over a malformed lead can never step over the NUL.
uint8_t u8_len(const char* s);

// Codepoints in s, counting each malformed byte as one. Never reads past NUL.
uint16_t u8_count(const char* s);

// How many BYTES of s fit in `room` bytes, cut on a codepoint boundary. A
// sequence is taken whole or not at all, so the answer is always a prefix that
// is itself well-formed wherever s was.
uint16_t u8_fit(const char* s, uint16_t room);

// True when s is entirely well-formed UTF-8 (no stray continuation bytes, no
// truncated sequence). Used by the tests and by the name gate; not a hot path.
bool u8_well_formed(const char* s);

// LATIN-1 IN, UTF-8 OUT. Every byte of src is one codepoint: < 0x80 copies as
// itself, >= 0x80 becomes the two-byte sequence for the same codepoint. Stops
// on the last WHOLE character that fits in cap (which counts the terminator),
// so the result is never a split sequence and is always NUL-terminated.
// Returns the number of bytes written, not counting the terminator.
//
// cap == 0 writes nothing at all (there is nowhere to put a terminator).
uint16_t u8_from_latin1(char* dst, uint16_t cap, const char* src);

// APPEND, on a codepoint boundary. dst must already be NUL-terminated; cap
// counts the terminator. Both return the new total length in bytes.
//
// THESE EXIST BECAUSE EVERY LIST ROW IN THIS UI IS BUILT WITH snprintf("%s"),
// WHICH CUTS ON A BYTE. A Box row is "10 *NAME" inside BOX_ROW_CAP, a battle
// pick row the same, and a name is the only part of either that can be wide -
// so the byte cut lands inside the name and, once a name can hold a two-byte
// character, inside a sequence.
uint16_t u8_cat(char* dst, uint16_t cap, const char* src);          // src UTF-8
uint16_t u8_cat_latin1(char* dst, uint16_t cap, const char* src);   // src Latin-1

// The same, bounded by CHARACTERS as well as by bytes. A layout budget written
// in glyphs ("nine a side against a 25-character line") has to be enforced in
// glyphs: "%.9s" is a BYTE precision and the two stop agreeing the moment a
// name holds a two-byte character.
uint16_t u8_cat_n(char* dst, uint16_t cap, const char* src, uint16_t max_chars);

#endif  // PB_UTF8_H
