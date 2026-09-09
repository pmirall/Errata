// =============================================================================
//  ERRATA - networking/creator_parse.cpp
//  The fixed-schema body reader. See creator_parse.h for the two documents it
//  accepts, why there is no recursion, and why every read is length-bounded.
//
//  PURE: stdint, string.h, the save schema, the creator schema and the PIN
//  parser. No file-scope mutable state, so both networking purity gates apply.
//
//  THE SHAPE OF EVERY READER BELOW IS THE SAME: check the cursor against the
//  length BEFORE dereferencing it, consume exactly what the schema says is
//  there, and return a NAMED code the moment the bytes stop matching. Nothing
//  is skipped, nothing is guessed, nothing is mended.
// =============================================================================
#include "creator_parse.h"

#include <string.h>

#include "../core/version.h"              // CREATOR_API_VERSION
#include "../game/validate.h"             // creator_name_char_ok - ONE charset
#include "creator_gate.h"                 // cg_parse_pin - ONE PIN parser

// The sprite payload: a 24x24 XBM frame is CS_SPRITE_BYTES bytes, so its hex
// form is exactly twice that. Derived, never typed: a sprite that grows moves
// this number with it.
#define CP_SPRITE_HEX_CHARS   ((uint32_t)CS_SPRITE_BYTES * 2u)

// Longest string this module will read into a local at all. The sprite frame is
// the biggest legitimate one; anything above it is refused before a byte is
// copied, which is what keeps the stack cost of a hostile body constant.
#define CP_STR_CAP            (CP_SPRITE_HEX_CHARS + 1u)

// The name's UTF-8 form. Twelve Latin-1 characters is at most 24 bytes of
// UTF-8, and the cap is a little above that so an over-long name is a NAMED
// refusal rather than a truncation that looks like a shorter name.
#define CP_NAME_UTF8_CAP      32u

// -----------------------------------------------------------------------------
//  THE CURSOR. `i` is only ever advanced past a byte that has been read, and
//  every read checks `i < n` first. There is no lookahead that can run off the
//  end and no "peek the next byte" that returns a value for a byte that is not
//  there - a byte that is not there is CP_SYNTAX, not a zero.
// -----------------------------------------------------------------------------
struct CpCur {
  const uint8_t* p;
  uint32_t       n;
  uint32_t       i;
};

static bool cp_take(CpCur& c, uint8_t& out)
{
  if (c.i >= c.n) return false;
  out = c.p[c.i++];
  return true;
}

static void cp_skip_ws(CpCur& c)
{
  // The four JSON whitespace bytes and nothing else. A vertical tab or a form
  // feed between tokens is CP_SYNTAX, because a reader that is liberal about
  // what separates tokens is a reader two implementations can disagree about.
  while (c.i < c.n) {
    const uint8_t b = c.p[c.i];
    if (b == ' ' || b == '\t' || b == '\r' || b == '\n') { c.i++; continue; }
    break;
  }
}

static bool cp_expect(CpCur& c, uint8_t want)
{
  cp_skip_ws(c);
  if (c.i >= c.n || c.p[c.i] != want) return false;
  c.i++;
  return true;
}

// -----------------------------------------------------------------------------
//  STRINGS. No escapes at all, and that is a decision rather than an omission:
//  the only escapes a page could need are \" and \\ inside a name, and a name
//  containing a quote or a backslash is refused by the character set two
//  functions down anyway. Supporting \uXXXX would mean a second UTF-16 decoder
//  beside the UTF-8 one below, for characters the font cannot draw.
// -----------------------------------------------------------------------------
static CpErr cp_read_string(CpCur& c, uint8_t* dst, uint32_t cap, uint32_t& out_len)
{
  out_len = 0u;
  if (!cp_expect(c, '"')) return CP_BAD_TYPE;
  bool over = false;
  for (;;) {
    uint8_t b;
    if (!cp_take(c, b)) return CP_SYNTAX;        // ran off the end unterminated
    if (b == '"')  return over ? CP_STRING_LEN : CP_OK;
    if (b == 0u)   return CP_NUL;                // never a terminator here
    if (b == '\\') return CP_SYNTAX;             // no escapes, see above
    if (b < 0x20u) return CP_SYNTAX;             // raw control byte
    // AN OVER-LONG STRING IS CONSUMED TO ITS CLOSING QUOTE AND THEN REFUSED,
    // never abandoned mid-string. Returning early here left the cursor INSIDE
    // the string for the one caller that tolerates CP_STRING_LEN (cp_read_pin,
    // for which an unusable PIN is not a malformed document) - so the next
    // token read was the tail of the value, and a body carrying a long "pin"
    // was refused as CP_SYNTAX instead of being read and gated. Bounded by the
    // body length either way: cp_take() is the only way forward and it stops at
    // the end.
    if (out_len >= cap) { over = true; continue; }
    dst[out_len++] = b;
  }
}

// -----------------------------------------------------------------------------
//  NUMBERS. CANONICAL UNSIGNED DECIMAL ONLY: "0", or 1-9 followed by digits.
//  No sign, no leading zero, no fraction, no exponent.
//
//  THE LEADING-ZERO RULE IS NOT PEDANTRY. The helper this file replaces
//  (arg_u32, still readable at `git show aa2b7ee:Pebblebol/webui.cpp`)
//  accumulated into a uint32_t against a 0xFFFFFFFF ceiling, so the multiply
//  wrapped and every decimal congruent to the PIN authenticated AS the PIN -
//  "4294971117" for 3821. Bounding the DIGIT COUNT before the accumulator can
//  wrap is what makes that class unable to exist: the widest number this file
//  reads is ten digits into a uint64_t, compared against the field's ceiling
//  before it is narrowed.
// -----------------------------------------------------------------------------
static CpErr cp_read_number(CpCur& c, uint32_t max_digits, uint64_t ceiling,
                            uint32_t& out)
{
  cp_skip_ws(c);
  out = 0u;
  if (c.i >= c.n) return CP_SYNTAX;
  const uint8_t first = c.p[c.i];
  // ANYTHING THAT IS NOT A DIGIT IS THE WRONG KIND OF VALUE, and that includes
  // an object, an array, a string, true, false and null. Only a value that
  // LOOKS like a number and is not one - a leading zero, too many digits, a
  // value over the field's ceiling - is CP_NUMBER, so the two codes stay
  // distinguishable in a test.
  if (first < '0' || first > '9') return CP_BAD_TYPE;

  uint64_t v      = 0u;
  uint32_t digits = 0u;
  while (c.i < c.n) {
    const uint8_t b = c.p[c.i];
    if (b < '0' || b > '9') break;
    if (digits >= max_digits) return CP_NUMBER;      // bounded BEFORE the multiply
    v = v * 10u + (uint64_t)(b - '0');
    digits++;
    c.i++;
  }
  if (digits == 0u) return CP_NUMBER;
  if (digits > 1u && first == '0') return CP_NUMBER; // "007" is not a number here
  if (v > ceiling) return CP_NUMBER;
  out = (uint32_t)v;
  return CP_OK;
}

static CpErr cp_read_u16(CpCur& c, uint32_t& out)
{
  return cp_read_number(c, 5u, 65535u, out);
}

// -----------------------------------------------------------------------------
//  THE NAME. UTF-8 in, Latin-1 out, and the conversion is where the character
//  set is enforced because it is the point at which a character stops being
//  representable. The device's fonts are _tf (ASCII + Latin-1); a page that
//  offers an emoji keyboard gets a named refusal rather than a mangled glyph.
// -----------------------------------------------------------------------------
static CpErr cp_name_from_utf8(const uint8_t* s, uint32_t n, char* out_name)
{
  uint32_t i = 0u, o = 0u;
  while (i < n) {
    const uint8_t b = s[i];
    uint32_t cp;
    if (b < 0x80u) {
      cp = b;
      i += 1u;
    } else if ((b & 0xE0u) == 0xC0u) {
      if (i + 1u >= n) return CP_UTF8;               // truncated sequence
      const uint8_t b2 = s[i + 1u];
      if ((b2 & 0xC0u) != 0x80u) return CP_UTF8;     // not a continuation byte
      cp = (uint32_t)((b & 0x1Fu) << 6) | (uint32_t)(b2 & 0x3Fu);
      if (cp < 0x80u) return CP_UTF8;                // overlong encoding
      i += 2u;
    } else {
      // Three- and four-byte sequences encode codepoints above 0x7FF, which are
      // outside Latin-1 by definition; a lone continuation byte lands here too.
      return CP_UTF8;
    }
    if (cp > 0xFFu) return CP_UTF8;
    if (!creator_name_char_ok((uint8_t)cp)) return CP_NAME_CHAR;
    if (o >= (uint32_t)NAME_MAX_LEN) return CP_STRING_LEN;
    out_name[o++] = (char)cp;
  }
  if (o == 0u) return CP_STRING_LEN;                 // a Bug needs a name
  // A leading or trailing space is refused rather than trimmed. Trimming is
  // MENDING, which is the thing spec section 15's first sentence is written
  // against (game/validate.h argues it at length); it would also make two names
  // the user typed differently compare equal on a device that never said so.
  if (out_name[0] == ' ' || out_name[o - 1u] == ' ') return CP_NAME_CHAR;
  out_name[o] = '\0';
  return CP_OK;
}

// -----------------------------------------------------------------------------
//  THE SPRITE FRAME. Exactly CP_SPRITE_HEX_CHARS characters, both cases
//  accepted (hex case is not a security property; the shipped arg parser this
//  replaces accepted both too), decoded into CS_SPRITE_BYTES.
// -----------------------------------------------------------------------------
static bool cp_hex_nibble(uint8_t ch, uint8_t& out)
{
  if (ch >= '0' && ch <= '9') { out = (uint8_t)(ch - '0');        return true; }
  if (ch >= 'a' && ch <= 'f') { out = (uint8_t)(ch - 'a' + 10u);  return true; }
  if (ch >= 'A' && ch <= 'F') { out = (uint8_t)(ch - 'A' + 10u);  return true; }
  return false;
}

static CpErr cp_read_sprite_frame(CpCur& c, uint8_t* frame)
{
  uint8_t  hex[CP_STR_CAP];
  uint32_t hex_len = 0u;
  const CpErr e = cp_read_string(c, hex, CP_SPRITE_HEX_CHARS, hex_len);
  if (e == CP_STRING_LEN) return CP_SPRITE_LEN;      // longer than a frame
  if (e != CP_OK) return e;
  if (hex_len != CP_SPRITE_HEX_CHARS) return CP_SPRITE_LEN;

  for (uint32_t k = 0u; k < (uint32_t)CS_SPRITE_BYTES; ++k) {
    uint8_t hi, lo;
    if (!cp_hex_nibble(hex[k * 2u], hi) || !cp_hex_nibble(hex[k * 2u + 1u], lo))
      return CP_SPRITE_HEX;
    frame[k] = (uint8_t)((hi << 4) | lo);
  }
  return CP_OK;
}

// -----------------------------------------------------------------------------
//  KEYS. Read into a small fixed buffer and compared whole; a key longer than
//  the longest one the schema has is CP_UNKNOWN_KEY before it is compared,
//  which is what stops a 2 KB key from being copied anywhere.
// -----------------------------------------------------------------------------
#define CP_KEY_CAP  8u

static CpErr cp_read_key(CpCur& c, char* key, uint32_t& key_len)
{
  uint8_t raw[CP_KEY_CAP];
  const CpErr e = cp_read_string(c, raw, CP_KEY_CAP, key_len);
  if (e == CP_STRING_LEN) return CP_UNKNOWN_KEY;   // longer than any key we have
  if (e == CP_BAD_TYPE)   return CP_SYNTAX;        // not a string at all
  if (e != CP_OK) return e;
  for (uint32_t k = 0u; k < key_len; ++k) key[k] = (char)raw[k];
  key[key_len] = '\0';
  return CP_OK;
}

static bool cp_key_is(const char* key, const char* want)
{
  return strcmp(key, want) == 0;
}

// The "pin" value. A STRING, never a number: "0123" and 123 are different PINs
// and only one of them survives a JSON number. A value cg_parse_pin() refuses
// leaves pin_out at 0 - the ConfigV2 "none issued" sentinel, which is never a
// live PIN - so a malformed PIN and a wrong PIN reach the gate as one answer,
// exactly as creator_gate.h requires.
static CpErr cp_read_pin(CpCur& c, uint16_t& pin_out)
{
  uint8_t  raw[CP_KEY_CAP];
  uint32_t len = 0u;
  const CpErr e = cp_read_string(c, raw, CP_KEY_CAP, len);
  if (e == CP_STRING_LEN) { pin_out = 0u; return CP_OK; }   // too long: not a PIN
  if (e != CP_OK) return e;
  char text[CP_KEY_CAP + 1u];
  for (uint32_t k = 0u; k < len; ++k) text[k] = (char)raw[k];
  text[len] = '\0';
  uint16_t v = 0u;
  pin_out = cg_parse_pin(text, v) ? v : (uint16_t)0u;
  return CP_OK;
}

// -----------------------------------------------------------------------------
//  THE UPLOAD
// -----------------------------------------------------------------------------
#define CPK_V       0x01u
#define CPK_PIN     0x02u
#define CPK_NAME    0x04u
#define CPK_TYPE    0x08u
#define CPK_BASE    0x10u
#define CPK_MOVES   0x20u
#define CPK_SPRITE  0x40u
#define CPK_EPOCH   0x80u

CpErr cp_parse_species(const uint8_t* body, uint32_t len,
                       CustomSpeciesRec& out, uint16_t& pin_out)
{
  memset(&out, 0, sizeof out);
  pin_out = 0u;
  if (body == nullptr || len == 0u) return CP_EMPTY;

  CpCur c = { body, len, 0u };
  uint8_t seen = 0u;

  if (!cp_expect(c, '{')) return CP_SYNTAX;

  // The empty object is a missing-key error rather than a syntax one, because
  // {} is a well-formed document that says nothing.
  cp_skip_ws(c);
  if (c.i < c.n && c.p[c.i] == '}') { c.i++; return CP_MISSING_KEY; }

  for (;;) {
    char     key[CP_KEY_CAP + 1u];
    uint32_t key_len = 0u;
    CpErr    e = cp_read_key(c, key, key_len);
    if (e != CP_OK) return e;
    if (!cp_expect(c, ':')) return CP_SYNTAX;

    uint8_t bit = 0u;
    if      (cp_key_is(key, "v"))      bit = CPK_V;
    else if (cp_key_is(key, "pin"))    bit = CPK_PIN;
    else if (cp_key_is(key, "name"))   bit = CPK_NAME;
    else if (cp_key_is(key, "type"))   bit = CPK_TYPE;
    else if (cp_key_is(key, "base"))   bit = CPK_BASE;
    else if (cp_key_is(key, "moves"))  bit = CPK_MOVES;
    else if (cp_key_is(key, "sprite")) bit = CPK_SPRITE;
    else return CP_UNKNOWN_KEY;

    if (seen & bit) return CP_DUP_KEY;      // never last-wins; see the header
    seen = (uint8_t)(seen | bit);

    uint32_t num = 0u;
    switch (bit) {
      case CPK_V:
        e = cp_read_u16(c, num);
        if (e != CP_OK) return e;
        if (num != (uint32_t)CREATOR_API_VERSION) return CP_VERSION;
        break;

      case CPK_PIN:
        e = cp_read_pin(c, pin_out);
        if (e != CP_OK) return e;
        break;

      case CPK_NAME: {
        uint8_t  raw[CP_NAME_UTF8_CAP];
        uint32_t raw_len = 0u;
        e = cp_read_string(c, raw, CP_NAME_UTF8_CAP, raw_len);
        if (e != CP_OK) return e;
        e = cp_name_from_utf8(raw, raw_len, out.name);
        if (e != CP_OK) return e;
        break;
      }

      case CPK_TYPE:
        e = cp_read_u16(c, num);
        if (e != CP_OK) return e;
        if (num > 255u) return CP_NUMBER;   // the field is a uint8_t; the game
        out.type = (uint8_t)num;            // rule is validate_custom_species's
        break;

      case CPK_BASE:
        if (!cp_expect(c, '[')) return CP_BAD_TYPE;
        cp_skip_ws(c);
        if (c.i < c.n && c.p[c.i] == ']') return CP_ARRAY_LEN;   // []
        for (uint8_t k = 0u; k < (uint8_t)CS_BASE_COUNT; ++k) {
          if (k && !cp_expect(c, ',')) return CP_ARRAY_LEN;
          e = cp_read_u16(c, num);
          if (e != CP_OK) return e;
          if (num > 255u) return CP_NUMBER;
          out.base[k] = (uint8_t)num;
        }
        if (!cp_expect(c, ']')) return CP_ARRAY_LEN;   // a fifth element lands here
        break;

      case CPK_MOVES:
        if (!cp_expect(c, '[')) return CP_BAD_TYPE;
        cp_skip_ws(c);
        if (c.i < c.n && c.p[c.i] == ']') return CP_ARRAY_LEN;
        for (uint8_t k = 0u; k < (uint8_t)ER_MOVE_COUNT; ++k) {
          if (k && !cp_expect(c, ',')) return CP_ARRAY_LEN;
          e = cp_read_u16(c, num);
          if (e != CP_OK) return e;
          if (num > 255u) return CP_NUMBER;
          out.moves[k] = (uint8_t)num;
        }
        if (!cp_expect(c, ']')) return CP_ARRAY_LEN;
        break;

      case CPK_SPRITE:
        if (!cp_expect(c, '[')) return CP_BAD_TYPE;
        cp_skip_ws(c);
        if (c.i < c.n && c.p[c.i] == ']') return CP_ARRAY_LEN;
        for (uint8_t f = 0u; f < (uint8_t)CS_SPRITE_FRAMES; ++f) {
          if (f && !cp_expect(c, ',')) return CP_ARRAY_LEN;
          e = cp_read_sprite_frame(c, out.sprite[f]);
          if (e != CP_OK) return e;
        }
        if (!cp_expect(c, ']')) return CP_ARRAY_LEN;
        break;

      default:
        return CP_UNKNOWN_KEY;              // unreachable; no silent fallthrough
    }

    cp_skip_ws(c);
    if (c.i >= c.n) return CP_SYNTAX;       // unterminated object
    if (c.p[c.i] == ',') { c.i++; continue; }
    if (c.p[c.i] == '}') { c.i++; break; }
    return CP_SYNTAX;
  }

  // Nothing may follow the document. A second object, or a byte of padding, is
  // a refusal and not something to ignore: "ignore the rest" is how two readers
  // of the same bytes come to different conclusions.
  cp_skip_ws(c);
  if (c.i != c.n) return CP_TRAILING;

  const uint8_t required = (uint8_t)(CPK_V | CPK_NAME | CPK_TYPE | CPK_BASE |
                                     CPK_MOVES | CPK_SPRITE);
  if ((seen & required) != required) return CP_MISSING_KEY;

  // Stamped here so a record that reaches flash is a record this firmware
  // wrote. slot, budget_used and reserved[] stay ZERO: the device fills the
  // first two and the third must never carry anything.
  out.magic   = (uint16_t)CS_MAGIC;
  out.version = (uint8_t)SAVE_SCHEMA_VERSION;
  return CP_OK;
}

// -----------------------------------------------------------------------------
//  THE CLOCK
// -----------------------------------------------------------------------------
CpErr cp_parse_time(const uint8_t* body, uint32_t len,
                    uint32_t& epoch_out, uint16_t& pin_out)
{
  epoch_out = 0u;
  pin_out   = 0u;
  if (body == nullptr || len == 0u) return CP_EMPTY;

  CpCur c = { body, len, 0u };
  uint8_t seen = 0u;

  if (!cp_expect(c, '{')) return CP_SYNTAX;
  cp_skip_ws(c);
  if (c.i < c.n && c.p[c.i] == '}') { c.i++; return CP_MISSING_KEY; }

  for (;;) {
    char     key[CP_KEY_CAP + 1u];
    uint32_t key_len = 0u;
    CpErr    e = cp_read_key(c, key, key_len);
    if (e != CP_OK) return e;
    if (!cp_expect(c, ':')) return CP_SYNTAX;

    uint8_t bit = 0u;
    if      (cp_key_is(key, "v"))     bit = CPK_V;
    else if (cp_key_is(key, "pin"))   bit = CPK_PIN;
    else if (cp_key_is(key, "epoch")) bit = CPK_EPOCH;
    else return CP_UNKNOWN_KEY;

    if (seen & bit) return CP_DUP_KEY;
    seen = (uint8_t)(seen | bit);

    uint32_t num = 0u;
    switch (bit) {
      case CPK_V:
        e = cp_read_u16(c, num);
        if (e != CP_OK) return e;
        if (num != (uint32_t)CREATOR_API_VERSION) return CP_VERSION;
        break;
      case CPK_PIN:
        e = cp_read_pin(c, pin_out);
        if (e != CP_OK) return e;
        break;
      case CPK_EPOCH:
        // Ten digits into a uint64_t, compared against the uint32_t ceiling
        // before it is narrowed. gt_set_epoch() decides whether the VALUE is
        // believable; this reader only decides whether it is a number.
        e = cp_read_number(c, 10u, 0xFFFFFFFFull, num);
        if (e != CP_OK) return e;
        epoch_out = num;
        break;
      default:
        return CP_UNKNOWN_KEY;
    }

    cp_skip_ws(c);
    if (c.i >= c.n) return CP_SYNTAX;
    if (c.p[c.i] == ',') { c.i++; continue; }
    if (c.p[c.i] == '}') { c.i++; break; }
    return CP_SYNTAX;
  }

  cp_skip_ws(c);
  if (c.i != c.n) return CP_TRAILING;

  const uint8_t required = (uint8_t)(CPK_V | CPK_EPOCH);
  if ((seen & required) != required) return CP_MISSING_KEY;
  return CP_OK;
}

// -----------------------------------------------------------------------------
//  NAMES
// -----------------------------------------------------------------------------
static const char* const CP_NAMES[] = {
  "CP_OK", "CP_EMPTY", "CP_SYNTAX", "CP_TRAILING", "CP_UNKNOWN_KEY", "CP_DUP_KEY",
  "CP_MISSING_KEY", "CP_BAD_TYPE", "CP_NUMBER", "CP_ARRAY_LEN", "CP_STRING_LEN",
  "CP_NUL", "CP_UTF8", "CP_NAME_CHAR", "CP_SPRITE_LEN", "CP_SPRITE_HEX",
  "CP_VERSION"
};
static_assert(sizeof(CP_NAMES) / sizeof(CP_NAMES[0]) == (size_t)CP_ERR_COUNT,
              "a CpErr was added without its name: the response would carry the "
              "wrong reason for every code after it");

const char* cp_err_name(CpErr e)
{
  if ((uint8_t)e >= (uint8_t)CP_ERR_COUNT) return "CP_?";
  return CP_NAMES[(uint8_t)e];
}
