// =============================================================================
//  Pebblebol host tests - test_genome.cpp
//  genome.cpp through its public header: genesis ranges, seal/validate,
//  breeding determinism, the 32-hex round trip and the gene setters.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "game/genome.h"
#include "core/crc16.h"
#include "core/rng.h"

static bool in_range(int v, int lo, int hi) { return v >= lo && v <= hi; }

TEST(genome_genesis_is_valid_and_in_design_ranges) {
  genome_seed(nt_seed());
  int rare = 0;
  for (int i = 0; i < 2000; i++) {
    const Genome g = genome_genesis();
    CHECK(genome_valid(g));
    CHECK(g.lineage_id != 0u);
    CHECK_EQ(g.crc16, crc16_ccitt(&g, GENOME_CRC_BYTES));
    CHECK(gene_species(g) <= 11);                      // hybrids 12..15 only by mating
    CHECK(in_range(gene_appetite(g), 4, 12));
    CHECK(in_range(gene_metabolism(g), 4, 12));
    CHECK(in_range(gene_sociability(g), 4, 12));
    CHECK(in_range(gene_temperament(g), 4, 12));
    CHECK(in_range(gene_hardiness(g), 4, 12));
    CHECK(in_range(gene_luck(g), 2, 6));
    CHECK_EQ(gene_mutations(g), 0);
    CHECK_EQ(gene_tainted(g), 0);
    CHECK(gene_pattern(g) <= 15);
    CHECK(gene_palette(g) <= 7);
    CHECK(gene_body_size(g) <= 7);
    CHECK(gene_ear_horn(g) <= 3);
    rare += gene_rare(g);
  }
  CHECK_NEAR(rare, 60, 45);                             // 3 % of 2000, generous band
}

TEST(genome_genesis_is_deterministic_per_seed) {
  genome_seed(0xBEEFu);
  const Genome a = genome_genesis();
  genome_seed(0xBEEFu);
  const Genome b = genome_genesis();
  CHECK(memcmp(&a, &b, sizeof a) == 0);

  genome_seed(0xBEF0u);
  const Genome c = genome_genesis();
  CHECK(memcmp(&a, &c, sizeof a) != 0);

  // genome_seed() is a wrapper on the RNG_BREEDING stream: seeding the stream
  // directly must give the same roll.
  rng_seed(RNG_BREEDING, 0xBEEFu);
  const Genome d = genome_genesis();
  CHECK(memcmp(&a, &d, sizeof a) == 0);
}

TEST(genome_seal_and_validate) {
  genome_seed(nt_seed());
  Genome g = genome_genesis();
  CHECK(genome_valid(g));

  // Any flipped gene bit breaks the seal until genome_seal() is re-run.
  g.g1 ^= 0x0010u;
  CHECK(!genome_valid(g));
  genome_seal(g);
  CHECK(genome_valid(g));
  CHECK_EQ(g.magic_ver, GENOME_MAGIC_VER);

  // A dynasty id of 0 is invalid by contract, even with a correct CRC.
  Genome z = g;
  z.lineage_id = 0u;
  genome_seal(z);
  CHECK(!genome_valid(z));

  // Wrong signature / protocol version is rejected.
  Genome m = g;
  m.magic_ver ^= 0x0001u;
  CHECK(!genome_valid(m));

  // The CRC covers exactly bytes 0..13 (crc16 excluded).
  CHECK_EQ(genome_crc16(g), crc16_ccitt(&g, 14));
}

TEST(genome_breed_is_deterministic_and_flags_mating) {
  genome_seed(0x1234u);
  const Genome A = genome_genesis();
  const Genome B = genome_genesis();

  uint8_t f1 = 0, f2 = 0;
  genome_seed(0x5555u);
  const Genome c1 = genome_breed(A, B, 0, 0, &f1);
  genome_seed(0x5555u);
  const Genome c2 = genome_breed(A, B, 0, 0, &f2);
  CHECK(memcmp(&c1, &c2, sizeof c1) == 0);
  CHECK_EQ(f1, f2);
  CHECK(genome_valid(c1));
  CHECK((f1 & EF_FROM_MATING) != 0);
  CHECK((f1 & EF_VALID) == 0);                         // storage's bit, never set here
  CHECK(c1.lineage_id != 0u);

  // Ties in cq_hi hand the dynasty to A; a higher B wins it (unless the 6 %
  // founder mutation fired - check it is one of the two, or flagged).
  genome_seed(0x5555u);
  uint8_t f3 = 0;
  const Genome c3 = genome_breed(A, B, 10, 200, &f3);
  CHECK((f3 & EF_NEW_LINEAGE) != 0 || c3.lineage_id == B.lineage_id || c3.lineage_id == A.lineage_id);
  genome_seed(0x5555u);
  const Genome c4 = genome_breed(A, B, 200, 10, nullptr);
  CHECK((f3 & EF_NEW_LINEAGE) != 0 || c4.lineage_id == A.lineage_id);

  // Different seeds do produce different children.
  int differs = 0;
  for (uint32_t s = 1; s <= 8; s++) {
    genome_seed(s);
    const Genome c = genome_breed(A, B);
    CHECK(genome_valid(c));
    if (memcmp(&c, &c1, sizeof c) != 0) differs++;
  }
  CHECK(differs > 0);
}

TEST(genome_hex32_round_trip) {
  genome_seed(nt_seed());
  const Genome g = genome_genesis();

  char hex[33];
  memset(hex, 'x', sizeof hex);
  genome_to_hex32(g, hex);
  CHECK_EQ(strlen(hex), 32);
  for (int i = 0; i < 32; i++) {
    const char c = hex[i];
    CHECK((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'));
  }

  Genome back;
  memset(&back, 0, sizeof back);
  CHECK(genome_from_hex32(hex, back));
  CHECK(memcmp(&g, &back, sizeof g) == 0);

  // Lower case and surrounding whitespace / CRLF are tolerated.
  char loose[40];
  loose[0] = ' ';
  for (int i = 0; i < 32; i++) {
    const char c = hex[i];
    loose[1 + i] = (c >= 'A' && c <= 'F') ? (char)(c - 'A' + 'a') : c;
  }
  loose[33] = '\r'; loose[34] = '\n'; loose[35] = '\0';
  memset(&back, 0, sizeof back);
  CHECK(genome_from_hex32(loose, back));
  CHECK(memcmp(&g, &back, sizeof g) == 0);

  // A mistyped character, a short string or a broken CRC all fail and leave
  // `out` untouched.
  Genome sentinel;
  memset(&sentinel, 0xEE, sizeof sentinel);
  Genome out = sentinel;
  char bad[33];
  memcpy(bad, hex, 33);
  bad[5] = 'G';
  CHECK(!genome_from_hex32(bad, out));
  CHECK(memcmp(&out, &sentinel, sizeof out) == 0);

  memcpy(bad, hex, 33);
  bad[31] = '\0';
  CHECK(!genome_from_hex32(bad, out));
  CHECK(memcmp(&out, &sentinel, sizeof out) == 0);

  memcpy(bad, hex, 33);
  bad[30] = (bad[30] == '0') ? '1' : '0';                // last byte = crc16 high nibble
  CHECK(!genome_from_hex32(bad, out));
  CHECK(memcmp(&out, &sentinel, sizeof out) == 0);

  CHECK(!genome_from_hex32("", out));
  CHECK(!genome_from_hex32(nullptr, out));
}

TEST(genome_setters_mask_and_reseal) {
  genome_seed(nt_seed());
  Genome g = genome_genesis();

  gene_set_luck(g, 7);
  CHECK_EQ(gene_luck(g), 7);
  CHECK(genome_valid(g));
  gene_set_luck(g, 9);                                   // 3-bit gene: masked
  CHECK_EQ(gene_luck(g), 1);
  CHECK(genome_valid(g));

  gene_set_species(g, 13);
  CHECK_EQ(gene_species(g), 13);
  gene_set_tainted(g, 1);
  CHECK_EQ(gene_tainted(g), 1);
  gene_set_mutations(g, 15);
  CHECK_EQ(gene_mutations(g), 15);
  CHECK(genome_valid(g));
}

TEST(genome_inheritance_primitives) {
  CHECK_EQ(genome_reflect(7, 0, 15), 7);
  CHECK_EQ(genome_reflect(16, 0, 15), 14);
  CHECK_EQ(genome_reflect(17, 0, 15), 13);
  CHECK_EQ(genome_reflect(-1, 0, 15), 1);
  CHECK_EQ(genome_reflect(-3, 0, 15), 3);
  CHECK_EQ(genome_reflect(0, 0, 15), 0);
  CHECK_EQ(genome_reflect(15, 0, 15), 15);

  genome_seed(nt_seed());
  bool neg = false, pos = false;
  for (int i = 0; i < 500; i++) {
    const int d = genome_mut_delta();
    CHECK(d != 0);
    CHECK(in_range(d, -3, 3));
    if (d < 0) neg = true; else pos = true;
  }
  CHECK(neg && pos);

  CHECK_EQ(genome_hybrid_species(3, 3), 3);
  for (uint8_t a = 0; a < 12; a++) {
    for (uint8_t b = 0; b < 12; b++) {
      CHECK_EQ(genome_hybrid_species(a, b), genome_hybrid_species(b, a));
      CHECK(genome_hybrid_species(a, b) <= 15);
    }
  }
}

TEST(genome_mating_rules) {
  genome_seed(nt_seed());
  Genome A = genome_genesis();
  Genome B = genome_genesis();

  gene_set_sex(A, 0); gene_set_luck(A, 3);
  gene_set_sex(B, 1); gene_set_luck(B, 3);
  CHECK(genome_can_mate(A, B));
  gene_set_sex(B, 0);
  CHECK(!genome_can_mate(A, B));                         // same sex, ordinary luck
  gene_set_luck(B, 6);
  CHECK(genome_can_mate(A, B));                          // luck >= 6 lifts the rule

  const uint16_t p = genome_mate_success_permille(A, B);
  CHECK(p >= 250 && p <= 900);

  CHECK(!genome_is_inbred(A, B) || A.lineage_id == B.lineage_id);
}

static uint32_t s_scripted_value = 0;
static uint32_t scripted_rng(void) { return s_scripted_value; }

TEST(genome_set_rng_installs_a_scripted_source) {
  s_scripted_value = 0x12345678u;
  genome_set_rng(&scripted_rng);
  CHECK_EQ(genome_rand(), 0x12345678u);
  s_scripted_value = 7u;
  CHECK_EQ(genome_rand(), 7u);

  // nullptr restores the RNG_BREEDING stream.
  genome_set_rng(nullptr);
  rng_seed(RNG_BREEDING, 99u);
  const uint32_t expect = rng_u32(RNG_BREEDING);
  rng_seed(RNG_BREEDING, 99u);
  CHECK_EQ(genome_rand(), expect);
}
