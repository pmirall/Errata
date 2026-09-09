// =============================================================================
//  ERRATA - ui/corrupt_fx.cpp
//  See corrupt_fx.h. PURE translation unit: stdint and nothing else.
// =============================================================================
#include "corrupt_fx.h"

// The same murmur-style finaliser ui/petfx.cpp uses for its own automaton. It
// is duplicated here on purpose rather than exported from petfx.cpp: petfx.cpp
// cannot be linked on the host, so importing it would drag Arduino.h into every
// binary that wants to check a bound. Four lines, no state, no allocation.
static inline uint32_t cfx_mix(uint32_t x)
{
  x ^= x >> 16; x *= 0x7FEB352Du;
  x ^= x >> 15; x *= 0x846CA68Bu;
  x ^= x >> 16;
  return x;
}

uint8_t cfx_temper_index(uint8_t temper, uint8_t corrupted)
{
  if (corrupted != 0u) return (uint8_t)CFX_TEMPER_CORRUPT;
  // pf_derive() did exactly this before the move ("if (s_temper >= TEMPER_COUNT)
  // s_temper = TEMPER_TRANQUILO"), and the rule has to survive it or a garbage
  // genome nibble indexes past the table. The target is named as a constant
  // rather than importing core/nt_types.h, which this pure file has no other
  // use for; ui/petfx.cpp static_asserts the constant against the enum.
  if (temper >= (uint8_t)CFX_TEMPER_CORRUPT) return (uint8_t)CFX_TEMPER_FALLBACK;
  return temper;
}

uint32_t cfx_glitch_slot(uint32_t now_ms)
{
  return now_ms / (uint32_t)CFX_GLITCH_SLOT_MS;
}

uint8_t cfx_glitch_on(uint32_t now_ms, uint32_t seed)
{
  const uint32_t h = cfx_mix(cfx_glitch_slot(now_ms) ^ (seed * 0x9E3779B9u));
  // The high bits, not the low ones. cfx_mix()'s last step is `x ^= x >> 16`,
  // so bit 0 of the result is bit 0 XOR bit 16 of the pre-final value; taking
  // the top three bits instead costs nothing and keeps the gate uncorrelated
  // with the row hashes below, which are drawn from the same slot.
  return (uint8_t)(((h >> 29) % (uint32_t)CFX_GLITCH_ONE_IN) == 0u);
}

uint8_t cfx_rows(const CfxRect& ink, uint32_t now_ms, uint32_t seed, CfxRow* out)
{
  if (out == nullptr) return 0u;

  // A degenerate or inverted rectangle is not clamped into something plausible:
  // there is no row that is inside it, so there are no rows. Answering with a
  // guess is how a bound stops being a bound.
  if (ink.x1 < ink.x0 || ink.y1 < ink.y0) return 0u;

  const int32_t span_x = (int32_t)ink.x1 - (int32_t)ink.x0 + 1;   // >= 1
  const int32_t span_y = (int32_t)ink.y1 - (int32_t)ink.y0 + 1;   // >= 1

  const uint32_t slot = cfx_glitch_slot(now_ms);
  const uint32_t h0   = cfx_mix(slot ^ (seed * 0x85EBCA6Bu));

  // 1..CFX_ROWS_MAX rows. A body two pixels tall cannot carry three separate
  // rows without repeating one, so the count is capped by the height too - a
  // repeated row would XOR twice and cancel itself, which is a glitch that
  // draws nothing on the narrowest bodies.
  uint32_t n = 1u + (h0 % (uint32_t)CFX_ROWS_MAX);
  if ((int32_t)n > span_y) n = (uint32_t)span_y;

  uint8_t written = 0;
  for (uint32_t i = 0; i < n; ++i) {
    const uint32_t h = cfx_mix(h0 + 0x9E3779B9u * (i + 1u));

    // --- the row -------------------------------------------------------------
    // Distinct rows: the i-th row is drawn from its own stripe of the body, so
    // two rows of one slot can never land on the same y and cancel.
    const int32_t lo = ink.y0 + (int32_t)((uint32_t)i * (uint32_t)span_y / n);
    const int32_t hi = ink.y0 + (int32_t)(((uint32_t)i + 1u) * (uint32_t)span_y / n) - 1;
    const int32_t stripe = (hi >= lo) ? (hi - lo + 1) : 1;
    int32_t y = lo + (int32_t)((h >> 8) % (uint32_t)stripe);

    // --- the span ------------------------------------------------------------
    // Between half the body and all of it, so the noise reads as torn scan
    // lines rather than as a uniform wash. `w` is at least 1 for any rectangle
    // this function accepted.
    const int32_t half = (span_x + 1) / 2;                     // >= 1
    int32_t w = half + (int32_t)((h >> 3) % (uint32_t)(span_x - half + 1));
    int32_t x = ink.x0 + (int32_t)((h >> 19) % (uint32_t)(span_x - w + 1));

    // --- THE CLAMP -----------------------------------------------------------
    // The arithmetic above already lands inside the rectangle; this is the
    // second line, and it is here because the FIRST line is a page of modular
    // arithmetic that a later edit can get wrong without any test noticing.
    // Everything that leaves this function goes through these six lines, so
    // "the glitch may not write outside the sprite rect" is one place to read
    // and one place to break.
    if (y < ink.y0) y = ink.y0;
    if (y > ink.y1) y = ink.y1;
    if (x < ink.x0) x = ink.x0;
    if (x > ink.x1) x = ink.x1;
    if (x + w - 1 > (int32_t)ink.x1) w = (int32_t)ink.x1 - x + 1;
    if (w < 1) w = 1;
    // A row wider than a u8 cannot happen on a 128 px panel and cannot happen
    // on a 40 px body, but `w` is a uint8_t in the struct and a silent
    // truncation would produce a row NARROWER than the clamp just proved -
    // which is safe - or, if the field ever widens, one that is not. Refuse
    // instead: an unrepresentable row is dropped, never approximated.
    if (w > 255) continue;

    out[written].x     = (int16_t)x;
    out[written].y     = (int16_t)y;
    out[written].w     = (uint8_t)w;
    // ODD LEVEL, DELIBERATELY. render.h measures how many distinct looks the
    // phase argument buys: 16 at odd levels, 2 at RD_D50 (8). A glitch whose
    // 16 phases collapse to 2 is a flicker between two fixed patterns.
    out[written].level = 7u;
    out[written].phase = (uint8_t)((h >> 24) & 0x0Fu);
    ++written;
  }
  return written;
}
