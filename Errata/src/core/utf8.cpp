// =============================================================================
//  ERRATA - core/utf8.cpp
//  See utf8.h. Pure: stdint only.
// =============================================================================
#include "utf8.h"

// A continuation byte is 10xxxxxx and nothing else is.
static inline bool cont(uint8_t b) { return (b & 0xC0u) == 0x80u; }

uint8_t u8_len(const char* s)
{
  if (s == nullptr) return 0u;
  const uint8_t* p = (const uint8_t*)s;
  const uint8_t  b = p[0];
  if (b == 0u)          return 0u;
  if (b < 0x80u)        return 1u;

  uint8_t want;
  if      ((b & 0xE0u) == 0xC0u) want = 2u;
  else if ((b & 0xF0u) == 0xE0u) want = 3u;
  else if ((b & 0xF8u) == 0xF0u) want = 4u;
  else                           return 1u;      // 10xxxxxx or 11111xxx: junk

  // THE HALF EVERY EARLIER COPY LEFT OUT. Reading the lead byte is not enough:
  // the continuation bytes have to BE there, before the terminator, and have to
  // be continuation bytes. Without this a lone 0xF1 at the end of a name makes
  // every caller advance four bytes over a one-byte string.
  for (uint8_t i = 1u; i < want; ++i) {
    if (!cont(p[i])) return 1u;                  // NUL is not a continuation
  }
  return want;
}

uint16_t u8_count(const char* s)
{
  if (s == nullptr) return 0u;
  uint16_t n = 0u;
  for (const char* p = s; *p != '\0'; ) {
    const uint8_t step = u8_len(p);
    p += (step != 0u) ? step : 1u;               // step is never 0 here
    if (n < 0xFFFFu) ++n;
  }
  return n;
}

uint16_t u8_fit(const char* s, uint16_t room)
{
  if (s == nullptr || room == 0u) return 0u;
  uint16_t used = 0u;
  for (const char* p = s; *p != '\0'; ) {
    const uint8_t step = u8_len(p);
    if (step == 0u) break;
    if ((uint32_t)used + (uint32_t)step > (uint32_t)room) break;
    used = (uint16_t)(used + step);
    p += step;
  }
  return used;
}

bool u8_well_formed(const char* s)
{
  if (s == nullptr) return true;
  for (const uint8_t* p = (const uint8_t*)s; *p != 0u; ) {
    const uint8_t step = u8_len((const char*)p);
    if (step == 1u && *p >= 0x80u) return false; // a byte we had to skip blind
    p += step;
  }
  return true;
}

uint16_t u8_from_latin1(char* dst, uint16_t cap, const char* src)
{
  if (dst == nullptr || cap == 0u) return 0u;
  uint16_t o = 0u;
  if (src != nullptr) {
    for (const uint8_t* p = (const uint8_t*)src; *p != 0u; ++p) {
      const uint8_t b    = *p;
      const uint16_t need = (b < 0x80u) ? 1u : 2u;
      // cap counts the terminator, so the text budget is cap - 1. A character
      // is taken whole or the copy stops: a half-written two-byte sequence is
      // exactly the broken lead byte this module exists to prevent.
      if ((uint32_t)o + need + 1u > (uint32_t)cap) break;
      if (b < 0x80u) {
        dst[o++] = (char)b;
      } else {
        dst[o++] = (char)(0xC0u | (b >> 6));
        dst[o++] = (char)(0x80u | (b & 0x3Fu));
      }
    }
  }
  dst[o] = '\0';
  return o;
}

// UTF-8 -> LATIN-1. The inverse of u8_from_latin1(), for DiscBeacon.name, which
// is Latin-1 by wire contract (see the header). u8_len() decides how many bytes
// a character occupies and refuses a lead byte whose continuations are not
// there, so a truncated name stops the copy rather than walking past its own
// terminator. Codepoints above 0xFF have no Latin-1 byte and become '?'.
uint16_t u8_to_latin1(char* dst, uint16_t cap, const char* src)
{
  if (dst == nullptr || cap == 0u) return 0u;
  uint16_t o = 0u;
  if (src != nullptr) {
    const uint8_t* p = (const uint8_t*)src;
    while (*p != 0u) {
      const uint8_t step = u8_len((const char*)p);
      uint32_t cp;
      if (step == 1u)      cp = p[0];
      else if (step == 2u) cp = (uint32_t)(((uint32_t)(p[0] & 0x1Fu) << 6) | (uint32_t)(p[1] & 0x3Fu));
      else                 cp = 0x100u;          // 3- and 4-byte: not Latin-1
      // A lead byte whose continuations are missing comes back as step 1 from
      // u8_len(), so a malformed tail is copied byte by byte rather than
      // walking off the end. That is the same refusal every other reader makes.
      if ((uint32_t)o + 1u + 1u > (uint32_t)cap) break;
      dst[o++] = (char)((cp <= 0xFFu) ? (uint8_t)cp : (uint8_t)'?');
      p += step;
    }
  }
  dst[o] = '\0';
  return o;
}

static uint16_t dst_len(const char* dst, uint16_t cap)
{
  uint16_t n = 0u;
  while (n + 1u < cap && dst[n] != '\0') ++n;
  return n;
}

uint16_t u8_cat(char* dst, uint16_t cap, const char* src)
{
  if (dst == nullptr || cap == 0u) return 0u;
  const uint16_t n = dst_len(dst, cap);
  if (src == nullptr) { dst[n] = '\0'; return n; }
  const uint16_t room = (uint16_t)(cap - 1u - n);
  const uint16_t take = u8_fit(src, room);
  for (uint16_t i = 0; i < take; ++i) dst[n + i] = src[i];
  dst[n + take] = '\0';
  return (uint16_t)(n + take);
}

uint16_t u8_cat_latin1(char* dst, uint16_t cap, const char* src)
{
  if (dst == nullptr || cap == 0u) return 0u;
  const uint16_t n = dst_len(dst, cap);
  const uint16_t wrote = u8_from_latin1(dst + n, (uint16_t)(cap - n), src);
  return (uint16_t)(n + wrote);
}

uint16_t u8_cat_n(char* dst, uint16_t cap, const char* src, uint16_t max_chars)
{
  if (dst == nullptr || cap == 0u) return 0u;
  const uint16_t n = dst_len(dst, cap);
  if (src == nullptr || max_chars == 0u) { dst[n] = '\0'; return n; }
  const uint16_t room = (uint16_t)(cap - 1u - n);
  uint16_t used = 0u, chars = 0u;
  for (const char* p = src; *p != '\0' && chars < max_chars; ) {
    const uint8_t step = u8_len(p);
    if (step == 0u) break;
    if ((uint32_t)used + (uint32_t)step > (uint32_t)room) break;
    for (uint8_t i = 0; i < step; ++i) dst[n + used + i] = p[i];
    used = (uint16_t)(used + step);
    p += step;
    ++chars;
  }
  dst[n + used] = '\0';
  return (uint16_t)(n + used);
}
