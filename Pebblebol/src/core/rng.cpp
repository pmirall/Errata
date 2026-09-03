// =============================================================================
//  Pebblebol - rng.cpp
//  xorshift32 streams. The step function is byte-for-byte the legacy
//  genome.cpp / sim.cpp generator, so a stream seeded with the same value
//  reproduces the pre-refactor sequence (tests/golden/sim_v1.txt depends on it).
// =============================================================================
#include "rng.h"

static Rng s_stream[RNG_STREAM_COUNT];

// Streams that were never seeded still work (from RNG_DEFAULT_SEED) so a host
// test that forgets rng_seed_all() is deterministic rather than broken.
static inline Rng& stream(RngStream s) {
  Rng& r = s_stream[(s < RNG_STREAM_COUNT) ? s : RNG_MISC];
  if (r.s == 0u) r.s = RNG_DEFAULT_SEED;
  return r;
}

void rng_init(Rng& r, uint32_t seed) {
  r.s = seed ? seed : RNG_DEFAULT_SEED;
}

uint32_t rng_next(Rng& r) {
  uint32_t x = r.s;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  r.s = x;
  return x;
}

// Multiply-high instead of modulo: no division, and the residual bias is
// 2^-32 scale, far below anything the game can observe.
uint32_t rng_next_below(Rng& r, uint32_t n) {
  if (n <= 1u) return 0u;
  return (uint32_t)(((uint64_t)rng_next(r) * (uint64_t)n) >> 32);
}

void rng_seed(RngStream s, uint32_t seed) {
  if (s >= RNG_STREAM_COUNT) return;
  rng_init(s_stream[s], seed);
}

// Derives one distinct seed per stream from the boot seed with a 32-bit
// integer finaliser (murmur3 fmix32), so identical boot seeds give identical
// streams and no two streams ever run the same sequence in lockstep.
static uint32_t fmix32(uint32_t h) {
  h ^= h >> 16;
  h *= 0x85EBCA6Bu;
  h ^= h >> 13;
  h *= 0xC2B2AE35u;
  h ^= h >> 16;
  return h;
}

void rng_seed_all(uint32_t boot_seed) {
  for (uint8_t i = 0; i < (uint8_t)RNG_STREAM_COUNT; i++) {
    rng_init(s_stream[i], fmix32(boot_seed + 0x9E3779B9u * (uint32_t)(i + 1u)));
  }
}

uint32_t rng_u32(RngStream s) {
  return rng_next(stream(s));
}

uint32_t rng_below(RngStream s, uint32_t n) {
  return rng_next_below(stream(s), n);
}

bool rng_chance_permille(RngStream s, uint16_t p) {
  if (p == 0u) return false;
  if (p >= 1000u) return true;
  return rng_below(s, 1000u) < (uint32_t)p;
}
