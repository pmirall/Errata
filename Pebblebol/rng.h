// =============================================================================
//  Pebblebol - rng.h
//  The one random number generator of the firmware (plan §1.4, §1.5).
//  Pure C++: <stdint.h> only, no Arduino header. Compiled on the host by tests/.
//
//  One xorshift32 core (lifted unchanged from the legacy genome.cpp) behind a
//  set of NAMED STREAMS, so that a draw for the care simulation can never
//  perturb a breeding roll and every subsystem is deterministic under a fixed
//  seed. Seed 0 is an absorbing state for xorshift and is remapped to
//  RNG_DEFAULT_SEED, exactly as the legacy genome/sim generators did.
//
//  On the device esp_random() is called EXACTLY ONCE, in the .ino, to feed
//  rng_seed_all(); host tests pass constants (rng_seed_all(0xC0FFEE) or
//  rng_seed(stream, n)). Nothing else in the tree may call esp_random().
// =============================================================================
#ifndef NT_RNG_H
#define NT_RNG_H

#include <stdint.h>

#define RNG_DEFAULT_SEED 0x2545F491u

enum RngStream : uint8_t {
  RNG_CARE = 0,      // sim.cpp: sickness rolls, PUNKI refusals, wish draws
  RNG_ENCOUNTER,     // wild encounters / peer discovery
  RNG_BATTLE,        // battle resolution
  RNG_BREEDING,      // genome genesis, mutation and mating rolls
  RNG_MINIGAME,      // on-device minigame layouts
  RNG_LOOT,          // drops and rewards
  RNG_MISC,          // tokens, nonces, PINs, canaries
  RNG_STREAM_COUNT
};

// A stand-alone generator for a per-battle / per-minigame stream that must be
// reproducible from a single 32-bit value (e.g. shared with a peer).
struct Rng {
  uint32_t s;        // never 0 once seeded through rng_init()
};

void     rng_init(Rng& r, uint32_t seed);              // seed 0 -> RNG_DEFAULT_SEED
uint32_t rng_next(Rng& r);                             // xorshift32 step
uint32_t rng_next_below(Rng& r, uint32_t n);           // uniform in [0, n); n <= 1 -> 0

// Named global streams.
void     rng_seed(RngStream s, uint32_t seed);         // seed 0 -> RNG_DEFAULT_SEED
void     rng_seed_all(uint32_t boot_seed);             // every stream, each distinct
uint32_t rng_u32(RngStream s);
uint32_t rng_below(RngStream s, uint32_t n);           // uniform in [0, n); n <= 1 -> 0
bool     rng_chance_permille(RngStream s, uint16_t p); // true with probability p/1000

#endif  // NT_RNG_H
