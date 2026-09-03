// =============================================================================
//  Pebblebol host tests - test_rng.cpp
//  The named xorshift32 streams of rng.h: determinism per stream, stream
//  independence, rng_below bounds, the seed-0 remap and the exact legacy
//  sequence (the care golden depends on it).
// =============================================================================
#include "nt_test.h"

#include "core/rng.h"

// First outputs of the legacy genome.cpp/sim.cpp xorshift32 from its default
// seed 0x2545F491 (computed independently). If these move, every golden moves.
static const uint32_t LEGACY_DEFAULT_SEQ[4] = {
  0xE124B63Au, 0x8B9A74ABu, 0x64E1B3ACu, 0x00174626u
};

TEST(rng_stream_is_deterministic_under_a_seed) {
  uint32_t a[8], b[8];
  rng_seed(RNG_CARE, 12345u);
  for (int i = 0; i < 8; i++) a[i] = rng_u32(RNG_CARE);
  rng_seed(RNG_CARE, 12345u);
  for (int i = 0; i < 8; i++) b[i] = rng_u32(RNG_CARE);
  for (int i = 0; i < 8; i++) CHECK_EQ(a[i], b[i]);

  // A different seed gives a different first draw.
  rng_seed(RNG_CARE, 12346u);
  CHECK(rng_u32(RNG_CARE) != a[0]);
}

TEST(rng_default_seed_reproduces_the_legacy_sequence) {
  rng_seed(RNG_BREEDING, RNG_DEFAULT_SEED);
  for (int i = 0; i < 4; i++) CHECK_EQ(rng_u32(RNG_BREEDING), LEGACY_DEFAULT_SEQ[i]);

  // Standalone generator: same core, same sequence.
  Rng r;
  rng_init(r, RNG_DEFAULT_SEED);
  for (int i = 0; i < 4; i++) CHECK_EQ(rng_next(r), LEGACY_DEFAULT_SEQ[i]);
}

TEST(rng_seed_zero_is_remapped_to_the_default_seed) {
  rng_seed(RNG_CARE, 0u);
  for (int i = 0; i < 4; i++) CHECK_EQ(rng_u32(RNG_CARE), LEGACY_DEFAULT_SEQ[i]);

  Rng r;
  rng_init(r, 0u);
  CHECK_EQ(r.s, RNG_DEFAULT_SEED);

  // xorshift never leaves a non-zero state: 0 is never produced.
  rng_seed(RNG_MISC, 0u);
  bool saw_zero = false;
  for (int i = 0; i < 5000; i++) if (rng_u32(RNG_MISC) == 0u) saw_zero = true;
  CHECK(!saw_zero);
}

TEST(rng_streams_are_independent) {
  // Drawing from one stream must not move another.
  rng_seed(RNG_CARE, 777u);
  rng_seed(RNG_BREEDING, 999u);
  const uint32_t expect_breeding = rng_u32(RNG_BREEDING);
  rng_seed(RNG_BREEDING, 999u);
  for (int i = 0; i < 100; i++) (void)rng_u32(RNG_CARE);
  CHECK_EQ(rng_u32(RNG_BREEDING), expect_breeding);

  // Two streams given the same seed run the same sequence in lockstep; the
  // whole point of rng_seed_all() is to keep that from happening on the device.
  rng_seed(RNG_CARE, 4242u);
  rng_seed(RNG_LOOT, 4242u);
  CHECK_EQ(rng_u32(RNG_CARE), rng_u32(RNG_LOOT));
}

TEST(rng_seed_all_gives_every_stream_a_distinct_seed) {
  rng_seed_all(nt_seed());
  uint32_t first[RNG_STREAM_COUNT];
  for (int s = 0; s < RNG_STREAM_COUNT; s++) first[s] = rng_u32((RngStream)s);
  for (int i = 0; i < RNG_STREAM_COUNT; i++) {
    for (int j = i + 1; j < RNG_STREAM_COUNT; j++) CHECK(first[i] != first[j]);
  }

  // Reproducible from the boot seed alone.
  rng_seed_all(nt_seed());
  for (int s = 0; s < RNG_STREAM_COUNT; s++) CHECK_EQ(rng_u32((RngStream)s), first[s]);

  // A different boot seed moves every stream.
  rng_seed_all(nt_seed() ^ 0xA5A5A5A5u);
  for (int s = 0; s < RNG_STREAM_COUNT; s++) CHECK(rng_u32((RngStream)s) != first[s]);

  // Boot seed 0 (esp_random() may legitimately return it) still works.
  rng_seed_all(0u);
  for (int s = 0; s < RNG_STREAM_COUNT; s++) CHECK(rng_u32((RngStream)s) != 0u);
}

TEST(rng_below_stays_in_range_and_covers_it) {
  rng_seed(RNG_MINIGAME, nt_seed());
  static const uint32_t N[] = { 2u, 3u, 6u, 7u, 100u, 1000u, 65536u, 0xFFFFFFFFu };
  for (size_t k = 0; k < sizeof N / sizeof N[0]; k++) {
    for (int i = 0; i < 2000; i++) CHECK(rng_below(RNG_MINIGAME, N[k]) < N[k]);
  }

  // Every residue of a small modulus is reachable.
  bool hit[7] = { false, false, false, false, false, false, false };
  for (int i = 0; i < 2000; i++) hit[rng_below(RNG_MINIGAME, 7u)] = true;
  for (int i = 0; i < 7; i++) CHECK(hit[i]);

  // n <= 1 answers 0 without consuming a draw.
  rng_seed(RNG_MINIGAME, 31337u);
  const uint32_t next = rng_u32(RNG_MINIGAME);
  rng_seed(RNG_MINIGAME, 31337u);
  CHECK_EQ(rng_below(RNG_MINIGAME, 0u), 0);
  CHECK_EQ(rng_below(RNG_MINIGAME, 1u), 0);
  CHECK_EQ(rng_u32(RNG_MINIGAME), next);

  // Standalone flavour, same contract.
  Rng r;
  rng_init(r, nt_seed());
  for (int i = 0; i < 2000; i++) CHECK(rng_next_below(r, 13u) < 13u);
  CHECK_EQ(rng_next_below(r, 1u), 0);
}

TEST(rng_chance_permille_edges_and_rate) {
  rng_seed(RNG_ENCOUNTER, nt_seed());
  for (int i = 0; i < 200; i++) CHECK(!rng_chance_permille(RNG_ENCOUNTER, 0));
  for (int i = 0; i < 200; i++) CHECK(rng_chance_permille(RNG_ENCOUNTER, 1000));
  for (int i = 0; i < 200; i++) CHECK(rng_chance_permille(RNG_ENCOUNTER, 1500));

  int hits = 0;
  for (int i = 0; i < 4000; i++) if (rng_chance_permille(RNG_ENCOUNTER, 500)) hits++;
  CHECK_NEAR(hits, 2000, 200);       // 50 % +/- 5 points over 4000 draws

  hits = 0;
  for (int i = 0; i < 4000; i++) if (rng_chance_permille(RNG_ENCOUNTER, 30)) hits++;
  CHECK_NEAR(hits, 120, 60);         // 3 % +/- 1.5 points
}

TEST(rng_out_of_range_stream_does_not_crash) {
  // Defensive: a bad enum value is routed to RNG_MISC instead of indexing
  // past the table.
  rng_seed(RNG_MISC, 5u);
  const uint32_t a = rng_u32(RNG_MISC);
  rng_seed(RNG_MISC, 5u);
  CHECK_EQ(rng_u32((RngStream)250), a);
  rng_seed((RngStream)250, 9u);      // ignored, must not write out of bounds
  CHECK(rng_u32(RNG_MISC) != 0u);
}
