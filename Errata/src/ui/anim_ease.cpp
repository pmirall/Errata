// =============================================================================
//  ERRATA - ui/anim_ease.cpp
//  See anim_ease.h for why this is its own translation unit.
//  PURE: stdint only. No Arduino, no u8g2, no render.h, no gfx.h.
//
//  EVERY FUNCTION HERE IS A LIFT, NOT A REWRITE. ui/actfx.cpp's af_hop,
//  af_lunge, af_lerp, af_pct, AF_BAYER and the two-line dissolve frontier in
//  af_blit() moved here byte for byte; actfx.cpp now calls them. The only
//  change of substance is that ae_dissolve_front() and ae_dissolve_skip() name
//  what af_blit() open-coded, so the frontier can be driven by a test without
//  a framebuffer.
// =============================================================================
#include "anim_ease.h"

// Ordered 4x4 Bayer. Used only to crumble the two-row frontier of a dissolve so
// it reads as something evaporating and not as a window blind coming down.
static const uint8_t AE_BAYER[16] = {
  0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5
};

uint8_t ae_bayer(int16_t x, int16_t y) {
  return AE_BAYER[(((uint8_t)y & 3u) << 2) | ((uint8_t)x & 3u)];
}

int16_t ae_hop(uint32_t t, uint32_t len, uint8_t amp) {
  if (len == 0u) return 0;
  if (t > len)   t = len;
  // WIDENED TO 64 BITS AT P10-C3, AND IT IS A PURE WIDENING: every value the
  // firmware actually passes fits a uint32_t, so this is BIT-IDENTICAL on every
  // reachable input and no film moves by a pixel. What it removes is a silent
  // overflow above it. The numerator peaks at amp*len^2, so 4*amp*t*(len-t)
  // wraps once amp*len^2 passes 2^32 - len above ~23,000 ms at amp 8, ~18,900
  // at amp 12 - and the denominator len*len wraps at len = 65,536, which is
  // reachable because ui/actfx.cpp's s_dur is a uint16_t. Past either point the
  // result was not "clamped" or "wrong by a pixel", it was arbitrary.
  //
  // Nothing in the firmware goes near it: the longest choreography is
  // AF_MEAL_MS at 2,600 ms and the largest amplitude is a handful of pixels, so
  // 4*8*1300*1300 = 54 M against a 4.29 G ceiling. It was found by
  // tests/test_anim.cpp sweeping len up to 65,535 - the first host binary ever
  // to execute this function - and it is fixed rather than documented because
  // the cost is one 64-bit divide on a call made a few times a frame and the
  // alternative is a pure module with a domain nobody checks.
  const uint64_t num = (uint64_t)4u * (uint64_t)amp * (uint64_t)t * (uint64_t)(len - t);
  const uint64_t den = (uint64_t)len * (uint64_t)len;
  return (int16_t)-(int16_t)(uint32_t)(num / den);
}

int16_t ae_lunge(uint32_t ph, uint32_t len, uint8_t amp) {
  if (len == 0u || ph >= len) return 0;
  // THE ONE LINE THIS LIFT ADDED, AND IT FIXES A DOCUMENTED CONTRACT THAT WAS
  // NOT TRUE. The header has claimed "always 0 at ph = 0" since P6-C3, and the
  // degenerate arm below - which exists so a window too short to hold three
  // phases still extends - returned `amp` for EVERY ph including 0. It fires
  // when (len*35)/100 == 0 or the three boundaries collapse, i.e. len <= 4.
  // Nothing in the firmware reaches it: ui/actfx.cpp's only lunge is a bite of
  // (AF_MEAL_EAT_MS - AF_MEAL_LEAN_MS)/AF_MEAL_BITES = 325 ms, or 212 ms for a
  // snack. So this changes no film and makes the invariant callers rely on -
  // repeated beats cannot accumulate drift - actually hold.
  //
  // IT WAS FOUND BY tests/test_anim.cpp, which is the FIRST HOST BINARY EVER TO
  // EXECUTE THIS FUNCTION: it lived as a `static` inside ui/actfx.cpp, a
  // translation unit that includes render.h and therefore compiles nowhere but
  // on the device.
  if (ph == 0u) return 0;
  const uint32_t out  = (len * 35u) / 100u;
  const uint32_t back = (len * 65u) / 100u;
  if (out == 0u || back <= out || len <= back) return (int16_t)amp;
  if (ph < out)  return (int16_t)(((uint32_t)amp * ph) / out);
  if (ph < back) return (int16_t)amp;
  return (int16_t)(((uint32_t)amp * (len - ph)) / (len - back));
}

int16_t ae_lerp(uint32_t t, uint32_t len, int16_t a, int16_t b) {
  if (len == 0u || t >= len) return b;
  return (int16_t)(a + (int16_t)(((int32_t)(b - a) * (int32_t)t) / (int32_t)len));
}

uint8_t ae_pct(uint32_t t, uint32_t t0, uint32_t t1) {
  if (t1 <= t0 || t <= t0) return 0u;
  if (t >= t1)             return 100u;
  return (uint8_t)(((t - t0) * 100u) / (t1 - t0));
}

int16_t ae_dissolve_front(uint8_t dir, uint8_t h, uint8_t pct) {
  if (pct > 100u) pct = 100u;
  const int16_t span = (int16_t)((int16_t)h + 3);
  if (dir == AE_DIS_UP)
    return (int16_t)((int16_t)h - (int16_t)((span * (int16_t)pct) / 100));
  return (int16_t)((int16_t)((span * (int16_t)pct) / 100) - 3);
}

uint8_t ae_dissolve_skip(uint8_t dir, uint8_t row, int16_t front,
                         int16_t sx, int16_t sy) {
  if (dir == AE_DIS_NONE) return 0u;
  uint8_t edge = 0u;
  if (dir == AE_DIS_UP) {
    if ((int16_t)row >= front)     return 1u;          // already gone
    if ((int16_t)row >= front - 2) edge = 1u;          // crumbling
  } else {
    if ((int16_t)row <  front)     return 1u;
    if ((int16_t)row <  front + 2) edge = 1u;
  }
  return (uint8_t)((edge && ae_bayer(sx, sy) >= 8u) ? 1u : 0u);
}

// -----------------------------------------------------------------------------
//  ae_noise. See the header. Three cheap steps - multiply-mix, xor-shift,
//  multiply again - chosen because a single multiply leaves the low bits
//  marching in step, which shows up as scanlines that all move the same way.
// -----------------------------------------------------------------------------
uint8_t ae_noise(uint8_t a, uint8_t b) {
  uint8_t h = (uint8_t)((uint8_t)(a * 37u) + (uint8_t)(b * 97u) + 0x5Au);
  h = (uint8_t)(h ^ (uint8_t)(h >> 3));
  return (uint8_t)((uint8_t)(h * 5u) + 1u);
}
