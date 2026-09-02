// =============================================================================
//  NOTTAMAGOCHI - genome.cpp
//  Implementation of GAME_DESIGN 3 (16-byte field map) and 4 (inheritance).
//  Pure C++: <stdint.h> / <string.h> only. No Arduino, no I/O, no float.
// =============================================================================
#include "genome.h"

#include <string.h>

// =============================================================================
// 0. RANDOMNESS - xorshift32, overridable
// =============================================================================

#define GN_DEFAULT_SEED 0x2545F491u

static uint32_t     s_xs_state = GN_DEFAULT_SEED;
static GenomeRngFn  s_rng      = nullptr;   // nullptr => built-in xorshift32

static uint32_t xorshift32(void) {
  uint32_t x = s_xs_state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  s_xs_state = x;
  return x;
}

void genome_set_rng(GenomeRngFn fn) {
  s_rng = fn;
}

void genome_seed(uint32_t seed) {
  s_xs_state = seed ? seed : GN_DEFAULT_SEED;
}

uint32_t genome_rand(void) {
  return s_rng ? s_rng() : xorshift32();
}

// Uniform in [0, n). Multiply-high instead of modulo: no division, and the
// residual bias is 2^-32 scale, far below anything the game can observe.
static uint32_t rnd_below(uint32_t n) {
  if (n <= 1) return 0;
  return (uint32_t)(((uint64_t)genome_rand() * (uint64_t)n) >> 32);
}

// Every probability in GAME_DESIGN 2.x is a whole percent (2..60), so one
// helper covers the lot. A permille variant existed here and was never called
// (-Wunused-function); if a sub-percent rate is ever specified, add it back as
// `rnd_below(1000) < p` - the multiply-high makes chance_pct(3) and a permille
// chance(30) select exactly the same set of genome_rand() values.
static bool chance_pct(uint32_t p) { return rnd_below(100) < p; }
static bool coin(void)             { return (genome_rand() & 1u) != 0u; }

// =============================================================================
// 1. GENE TABLES
//    Everything below is driven by these two tables so that "for every numeric
//    gene" in the design document maps to a literal loop in the code.
// =============================================================================

// Which 16-bit word a gene lives in.
#define GW_G0 0
#define GW_G1 1
#define GW_G2 2

struct GeneDef {
  uint8_t word;   // GW_G0 / GW_G1 / GW_G2
  uint8_t sh;
  uint8_t mk;
  uint8_t lo;     // inclusive reflect / re-roll lower bound
  uint8_t hi;     // inclusive reflect / re-roll upper bound
};

// Numeric genes (GAME_DESIGN 4.2 "Numeric" class). Order is contractual for
// the death-egg "random numeric gene" draw and for the host test.
enum NumGeneId {
  NG_APPETITE = 0,
  NG_METABOLISM,
  NG_SOCIABILITY,
  NG_TEMPERAMENT,   // last gene that lives in g1
  NG_HARDINESS,
  NG_LUCK,
  NG_COUNT
};
#define NG_G1_COUNT 4   // NG_APPETITE..NG_TEMPERAMENT are the g1 numeric genes

static const GeneDef NUMGENE[NG_COUNT] = {
  { GW_G1, GN_APPETITE_SH, GN_APPETITE_MK, 0, 15 },
  { GW_G1, GN_METAB_SH,    GN_METAB_MK,    0, 15 },
  { GW_G1, GN_SOCIAB_SH,   GN_SOCIAB_MK,   0, 15 },
  { GW_G1, GN_TEMPER_SH,   GN_TEMPER_MK,   0, 15 },
  { GW_G2, GN_HARDY_SH,    GN_HARDY_MK,    0, 15 },
  { GW_G2, GN_LUCK_SH,     GN_LUCK_MK,     0,  7 },   // 3-bit gene
};

// Categorical genes (GAME_DESIGN 4.2 "Categorical" class). species re-rolls
// only over the REAL species [0,11]; 12..15 are reachable exclusively through
// the hybrid table.
enum CatGeneId {
  CG_SPECIES = 0,
  CG_PATTERN,
  CG_PALETTE,
  CG_BODYSIZE,
  CG_EARHORN,
  CG_COUNT
};

static const GeneDef CATGENE[CG_COUNT] = {
  { GW_G0, GN_SPECIES_SH,  GN_SPECIES_MK,  0, 11 },
  { GW_G0, GN_PATTERN_SH,  GN_PATTERN_MK,  0, 15 },
  { GW_G0, GN_PALETTE_SH,  GN_PALETTE_MK,  0,  7 },
  { GW_G0, GN_BODYSIZE_SH, GN_BODYSIZE_MK, 0,  7 },
  { GW_G0, GN_EARHORN_SH,  GN_EARHORN_MK,  0,  3 },
};

static uint16_t gw_read(const Genome& g, uint8_t word) {
  switch (word) {
    case GW_G0: return g.g0;
    case GW_G1: return g.g1;
    default:    return g.g2;
  }
}

static void gw_write(Genome& g, uint8_t word, uint16_t v) {
  switch (word) {
    case GW_G0: g.g0 = v; break;
    case GW_G1: g.g1 = v; break;
    default:    g.g2 = v; break;
  }
}

static uint8_t gd_get(const Genome& g, const GeneDef& d) {
  return (uint8_t)((gw_read(g, d.word) >> d.sh) & d.mk);
}

// Raw write, value masked. Does NOT reseal.
static void gd_put(Genome& g, const GeneDef& d, uint8_t v) {
  uint16_t w = gw_read(g, d.word);
  w = (uint16_t)((w & (uint16_t)~((uint16_t)d.mk << d.sh))
                 | (uint16_t)(((uint16_t)v & d.mk) << d.sh));
  gw_write(g, d.word, w);
}

// Write with reflection into the gene's design range. Does NOT reseal.
static void gd_put_reflected(Genome& g, const GeneDef& d, int v) {
  gd_put(g, d, (uint8_t)genome_reflect(v, d.lo, d.hi));
}

// =============================================================================
// 2. INHERITANCE PRIMITIVES
// =============================================================================

int genome_reflect(int v, int lo, int hi) {
  if (hi <= lo) return lo;
  // Bounded loop: the design's largest delta is 3 and the narrowest gene range
  // is 4 wide, so one or two passes always suffice. The cap makes a pathological
  // caller terminate instead of spinning.
  for (int guard = 0; guard < 8; ++guard) {
    if (v < lo)      v = lo + (lo - v);
    else if (v > hi) v = hi - (v - hi);
    else             return v;
  }
  return (v < lo) ? lo : ((v > hi) ? hi : v);
}

// {-3:5%, -2:15%, -1:30%, +1:30%, +2:15%, +3:5%} - never 0.
int genome_mut_delta(void) {
  uint32_t r = rnd_below(100);
  if (r < 5)  return -3;
  if (r < 20) return -2;
  if (r < 50) return -1;
  if (r < 80) return  1;
  if (r < 95) return  2;
  return 3;
}

bool genome_is_inbred(const Genome& A, const Genome& B) {
  return (A.parent_tag == B.parent_tag) && (A.lineage_id == B.lineage_id);
}

bool genome_can_mate(const Genome& A, const Genome& B) {
  if (gene_sex(A) != gene_sex(B)) return true;
  return (gene_luck(A) >= 6) || (gene_luck(B) >= 6);
}

uint16_t genome_mate_success_permille(const Genome& A, const Genome& B) {
  // (socMultA + socMultB) / 2 * 0.5, clamped [250, 900]  (GAME_DESIGN 4.2)
  int32_t avg = ((int32_t)gene_sociability_mult(A) + (int32_t)gene_sociability_mult(B)) / 2;
  int32_t p   = (avg * 500) / 1000;
  return (uint16_t)NT_CLAMP(p, 250, 900);
}

// =============================================================================
// 3. HYBRID SPECIES TABLE (GAME_DESIGN 4.2 "species special")
//    12x12 over the real species. Read as [min][max]; the array is stored
//    symmetric so either order works and the diagonal is the identity.
//    Values 12..15 (ESPEJO / NUDO / ECO / VACIO) are reachable ONLY here.
//
//      0 BLOB   1 ORUGA  2 PAJARO 3 GATO   4 SETA   5 CACTUS
//      6 PEZ    7 ROBOT  8 FANTASMA 9 CONEJO 10 DRAGON 11 MEDUSA
// =============================================================================
#define HYB_N 12

static const uint8_t HYBRID_TABLE[HYB_N][HYB_N] = {
  /*        0   1   2   3   4   5   6   7   8   9  10  11 */
  /* 0 */ { 0,  1, 11,  0,  4,  4, 11, 13,  8,  0, 14, 11 },
  /* 1 */ { 1,  1,  2,  9,  4,  5,  6, 13, 14,  9, 10, 11 },
  /* 2 */ {11,  2,  2,  3, 12,  5,  6,  7,  8,  9, 10, 14 },
  /* 3 */ { 0,  9,  3,  3, 12,  5,  6,  7,  8,  9, 10, 15 },
  /* 4 */ { 4,  4, 12, 12,  4,  5, 13, 15,  8,  4, 13, 11 },
  /* 5 */ { 4,  5,  5,  5,  5,  5, 13,  7, 15,  9, 10, 12 },
  /* 6 */ {11,  6,  6,  6, 13, 13,  6, 12, 11, 14, 10, 11 },
  /* 7 */ {13, 13,  7,  7, 15,  7, 12,  7, 14, 12, 10, 13 },
  /* 8 */ { 8, 14,  8,  8,  8, 15, 11, 14,  8, 15, 14, 11 },
  /* 9 */ { 0,  9,  9,  9,  4,  9, 14, 12, 15,  9, 10, 12 },
  /*10 */ {14, 10, 10, 10, 13, 10, 10, 10, 14, 10, 10, 15 },
  /*11 */ {11, 11, 14, 15, 11, 12, 11, 13, 11, 12, 15, 11 },
};

uint8_t genome_hybrid_species(uint8_t a, uint8_t b) {
  if (a == b) return (uint8_t)(a & GN_SPECIES_MK);
  if (a >= HYB_N || b >= HYB_N) {
    // One side is already a hybrid-exclusive shape (12..15). Keep it - a
    // recombinant of a recombinant stays exotic.
    return (a >= HYB_N) ? (uint8_t)(a & GN_SPECIES_MK) : (uint8_t)(b & GN_SPECIES_MK);
  }
  uint8_t lo = (a < b) ? a : b;
  uint8_t hi = (a < b) ? b : a;
  return HYBRID_TABLE[lo][hi];
}

// =============================================================================
// 4. INTEGRITY
// =============================================================================

uint16_t genome_crc16(const Genome& g) {
  // CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, MSB-first, no reflection,
  // no final xor. Covers bytes 0..13 (everything except crc16 itself).
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&g);
  uint16_t crc = GENOME_CRC_INIT;
  for (uint8_t i = 0; i < GENOME_CRC_BYTES; ++i) {
    crc ^= (uint16_t)((uint16_t)p[i] << 8);
    for (uint8_t b = 0; b < 8; ++b) {
      crc = (uint16_t)((crc & 0x8000u) ? (uint16_t)((crc << 1) ^ GENOME_CRC_POLY)
                                       : (uint16_t)(crc << 1));
    }
  }
  return crc;
}

void genome_seal(Genome& g) {
  g.magic_ver = (uint16_t)GENOME_MAGIC_VER;
  GN_SET(g.g2, GN_G2RSV_SH, GN_G2RSV_MK, 0);   // reserved must be 0
  g.crc16 = genome_crc16(g);
}

bool genome_valid(const Genome& g) {
  if ((g.magic_ver & GENOME_SIG_MASK) != GENOME_SIG)          return false;
  if ((uint16_t)(g.magic_ver & ~GENOME_SIG_MASK) != GENOME_PROTO_VER) return false;
  if (g.lineage_id == 0u)                                     return false;
  return g.crc16 == genome_crc16(g);
}

// =============================================================================
// 5. GENE ACCESSORS
// =============================================================================

uint8_t gene_species(const Genome& g)      { return GN_GET(g.g0, GN_SPECIES_SH,  GN_SPECIES_MK);  }
uint8_t gene_pattern(const Genome& g)      { return GN_GET(g.g0, GN_PATTERN_SH,  GN_PATTERN_MK);  }
uint8_t gene_palette(const Genome& g)      { return GN_GET(g.g0, GN_PALETTE_SH,  GN_PALETTE_MK);  }
uint8_t gene_body_size(const Genome& g)    { return GN_GET(g.g0, GN_BODYSIZE_SH, GN_BODYSIZE_MK); }
uint8_t gene_ear_horn(const Genome& g)     { return GN_GET(g.g0, GN_EARHORN_SH,  GN_EARHORN_MK);  }
uint8_t gene_appetite(const Genome& g)     { return GN_GET(g.g1, GN_APPETITE_SH, GN_APPETITE_MK); }
uint8_t gene_metabolism(const Genome& g)   { return GN_GET(g.g1, GN_METAB_SH,    GN_METAB_MK);    }
uint8_t gene_sociability(const Genome& g)  { return GN_GET(g.g1, GN_SOCIAB_SH,   GN_SOCIAB_MK);   }
uint8_t gene_temperament(const Genome& g)  { return GN_GET(g.g1, GN_TEMPER_SH,   GN_TEMPER_MK);   }
uint8_t gene_hardiness(const Genome& g)    { return GN_GET(g.g2, GN_HARDY_SH,    GN_HARDY_MK);    }
uint8_t gene_luck(const Genome& g)         { return GN_GET(g.g2, GN_LUCK_SH,     GN_LUCK_MK);     }
uint8_t gene_mutations(const Genome& g)    { return GN_GET(g.g2, GN_MUTCNT_SH,   GN_MUTCNT_MK);   }
uint8_t gene_sex(const Genome& g)          { return GN_GET(g.g2, GN_SEX_SH,      GN_SEX_MK);      }
uint8_t gene_rare(const Genome& g)         { return GN_GET(g.g2, GN_RARE_SH,     GN_RARE_MK);     }
uint8_t gene_tainted(const Genome& g)      { return GN_GET(g.g2, GN_TAINT_SH,    GN_TAINT_MK);    }
uint8_t gene_temper_class(const Genome& g) { return TEMPER_CLASS(gene_temperament(g)); }

#define GN_DEFINE_SETTER(fn, word, sh, mk)                 \
  void fn(Genome& g, uint8_t v) {                          \
    GN_SET(g.word, sh, mk, v);                             \
    genome_seal(g);                                        \
  }

GN_DEFINE_SETTER(gene_set_species,     g0, GN_SPECIES_SH,  GN_SPECIES_MK)
GN_DEFINE_SETTER(gene_set_pattern,     g0, GN_PATTERN_SH,  GN_PATTERN_MK)
GN_DEFINE_SETTER(gene_set_palette,     g0, GN_PALETTE_SH,  GN_PALETTE_MK)
GN_DEFINE_SETTER(gene_set_body_size,   g0, GN_BODYSIZE_SH, GN_BODYSIZE_MK)
GN_DEFINE_SETTER(gene_set_ear_horn,    g0, GN_EARHORN_SH,  GN_EARHORN_MK)
GN_DEFINE_SETTER(gene_set_appetite,    g1, GN_APPETITE_SH, GN_APPETITE_MK)
GN_DEFINE_SETTER(gene_set_metabolism,  g1, GN_METAB_SH,    GN_METAB_MK)
GN_DEFINE_SETTER(gene_set_sociability, g1, GN_SOCIAB_SH,   GN_SOCIAB_MK)
GN_DEFINE_SETTER(gene_set_temperament, g1, GN_TEMPER_SH,   GN_TEMPER_MK)
GN_DEFINE_SETTER(gene_set_hardiness,   g2, GN_HARDY_SH,    GN_HARDY_MK)
GN_DEFINE_SETTER(gene_set_luck,        g2, GN_LUCK_SH,     GN_LUCK_MK)
GN_DEFINE_SETTER(gene_set_mutations,   g2, GN_MUTCNT_SH,   GN_MUTCNT_MK)
GN_DEFINE_SETTER(gene_set_sex,         g2, GN_SEX_SH,      GN_SEX_MK)
GN_DEFINE_SETTER(gene_set_rare,        g2, GN_RARE_SH,     GN_RARE_MK)
GN_DEFINE_SETTER(gene_set_tainted,     g2, GN_TAINT_SH,    GN_TAINT_MK)

#undef GN_DEFINE_SETTER

// =============================================================================
// 6. GENE -> MULTIPLIER MAPS (integer, per-mille)
// =============================================================================

// rare_flag = "+1 to all _mult favourably": one extra gene step toward the
// beneficial side, clamped (NOT reflected - this is a read-time bonus, not a
// mutation, and reflecting would turn a maxed gene into a penalty).
static uint8_t rare_step(const Genome& g, uint8_t v, int dir, uint8_t lo, uint8_t hi) {
  if (!gene_rare(g)) return v;
  int x = (int)v + dir;
  return (uint8_t)NT_CLAMP(x, (int)lo, (int)hi);
}

uint16_t gene_appetite_mult(const Genome& g) {
  uint8_t v = rare_step(g, gene_appetite(g), -1, 0, 15);
  return (uint16_t)(GENE_MULT_BASE + (int32_t)v * GENE_MULT_STEP);
}

uint16_t gene_metabolism_mult(const Genome& g) {
  uint8_t v = rare_step(g, gene_metabolism(g), -1, 0, 15);
  return (uint16_t)(GENE_MULT_BASE + (int32_t)v * GENE_MULT_STEP);
}

uint16_t gene_sociability_mult(const Genome& g) {
  uint8_t v = rare_step(g, gene_sociability(g), +1, 0, 15);
  return (uint16_t)(GENE_SOC_BASE + (int32_t)v * GENE_SOC_STEP);
}

uint16_t gene_hardiness_mult(const Genome& g) {
  // 1300 - v*40: a DAMAGE multiplier, so a higher gene is better.
  uint8_t v = rare_step(g, gene_hardiness(g), +1, 0, 15);
  return (uint16_t)(GENE_HARDY_BASE + (int32_t)v * GENE_HARDY_STEP);
}

uint16_t gene_luck_permille(const Genome& g) {
  uint8_t v = rare_step(g, gene_luck(g), +1, 0, 7);
  return (uint16_t)(20 + (int32_t)v * 15);          // 2% + v*1.5%
}

uint16_t gene_tantrum_permille(const Genome& g) {
  return (uint16_t)((int32_t)gene_temperament(g) * 12);   // v * 1.2%
}

uint16_t gene_mate_success_permille(const Genome& g) {
  uint8_t v = rare_step(g, gene_sociability(g), +1, 0, 15);
  return (uint16_t)(400 + (int32_t)v * 30);         // 40% + v*3%
}

uint16_t gene_weight_ideal_dg(const Genome& g) {
  return (uint16_t)(300 + (int32_t)gene_body_size(g) * 50);
}

// =============================================================================
// 7. GENESIS (GAME_DESIGN 3.1)
// =============================================================================

// The design says numeric genes roll 4..12 out of 0..15 ("gen 0 is never
// extreme"). luck is a 3-bit gene (0..7), so the same proportional band is
// applied to it: 2..6.
#define GENESIS_LUCK_MIN 2
#define GENESIS_LUCK_MAX 6

static uint32_t rnd_nonzero_u32(void) {
  uint32_t v = genome_rand();
  return v ? v : 1u;   // lineage_id == 0 is invalid by contract
}

Genome genome_genesis(void) {
  Genome g;
  memset(&g, 0, sizeof(g));

  g.lineage_id = rnd_nonzero_u32();
  g.generation = 0;
  g.parent_tag = 0;

  // --- morphology (g0) ------------------------------------------------------
  GN_SET(g.g0, GN_SPECIES_SH,  GN_SPECIES_MK,  (uint8_t)rnd_below(GENESIS_SPECIES_MAX + 1));  // [0,7]
  GN_SET(g.g0, GN_PATTERN_SH,  GN_PATTERN_MK,  (uint8_t)rnd_below(PATTERN_COUNT));            // [0,15]
  GN_SET(g.g0, GN_PALETTE_SH,  GN_PALETTE_MK,  (uint8_t)rnd_below(8));
  GN_SET(g.g0, GN_BODYSIZE_SH, GN_BODYSIZE_MK, (uint8_t)rnd_below(8));
  GN_SET(g.g0, GN_EARHORN_SH,  GN_EARHORN_MK,  (uint8_t)rnd_below(4));

  // --- numeric genes: 4..12, luck 2..6 --------------------------------------
  const uint32_t span = (uint32_t)(GENESIS_GENE_MAX - GENESIS_GENE_MIN + 1);   // 9
  for (uint8_t i = 0; i < NG_COUNT; ++i) {
    uint8_t v;
    if (i == NG_LUCK) {
      v = (uint8_t)(GENESIS_LUCK_MIN + rnd_below(GENESIS_LUCK_MAX - GENESIS_LUCK_MIN + 1));
    } else {
      v = (uint8_t)(GENESIS_GENE_MIN + rnd_below(span));
    }
    gd_put(g, NUMGENE[i], v);
  }

  // --- meta (g2) ------------------------------------------------------------
  GN_SET(g.g2, GN_MUTCNT_SH, GN_MUTCNT_MK, 0);
  GN_SET(g.g2, GN_SEX_SH,    GN_SEX_MK,    coin() ? 1u : 0u);
  GN_SET(g.g2, GN_RARE_SH,   GN_RARE_MK,   chance_pct(3) ? 1u : 0u);
  GN_SET(g.g2, GN_TAINT_SH,  GN_TAINT_MK,  0);

  g.parent_tag = (uint8_t)((g.lineage_id ^ (uint32_t)g.g1) & 0xFFu);
  genome_seal(g);
  return g;
}

// =============================================================================
// 8. BLE MATING - TWO PARENTS (GAME_DESIGN 4.2)
//    Averages and mixes: pulls a dynasty back toward the middle and injects
//    foreign categoricals.
// =============================================================================

Genome genome_breed(const Genome& A, const Genome& B,
                    uint8_t cq_hi_a, uint8_t cq_hi_b,
                    uint8_t* out_flags) {
  Genome c;
  memset(&c, 0, sizeof(c));

  uint8_t flags       = EF_FROM_MATING;
  bool    any_mutation = false;

  // --- numeric genes: round((A+B)/2), then p=8% mutate + reflect ------------
  for (uint8_t i = 0; i < NG_COUNT; ++i) {
    const GeneDef& d = NUMGENE[i];
    int a = (int)gd_get(A, d);
    int b = (int)gd_get(B, d);
    int v = (a + b + 1) / 2;                       // round half up, integer
    if (chance_pct(8)) {
      v = genome_reflect(v + genome_mut_delta(), d.lo, d.hi);
      any_mutation = true;
    }
    gd_put_reflected(c, d, v);
  }

  // --- categorical genes: 50/50 crossover, then p=3% uniform re-roll --------
  for (uint8_t i = 0; i < CG_COUNT; ++i) {
    const GeneDef& d = CATGENE[i];
    uint8_t v = coin() ? gd_get(A, d) : gd_get(B, d);
    if (chance_pct(3)) {
      v = (uint8_t)(d.lo + rnd_below((uint32_t)(d.hi - d.lo + 1)));
      any_mutation = true;
    }
    gd_put(c, d, v);
  }

  // --- species special: 12% recombinant when the parents differ -------------
  {
    uint8_t sa = gene_species(A);
    uint8_t sb = gene_species(B);
    if (sa != sb && chance_pct(12)) {
      uint8_t hyb = genome_hybrid_species(sa, sb);
      GN_SET(c.g0, GN_SPECIES_SH, GN_SPECIES_MK, hyb);
      flags |= EF_HYBRID;          // -> QUIMERA eligible (PetSave PF_HYBRID_ELIG)
      any_mutation = true;
    }
  }

  // --- sex: 50/50 -----------------------------------------------------------
  GN_SET(c.g2, GN_SEX_SH, GN_SEX_MK, coin() ? 1u : 0u);

  // --- rare_flag: (A|B) with 50% dominance, plus 2% spontaneous -------------
  {
    uint8_t rare = 0;
    if ((gene_rare(A) | gene_rare(B)) && coin()) rare = 1;
    if (chance_pct(2))                           rare = 1;
    GN_SET(c.g2, GN_RARE_SH, GN_RARE_MK, rare);
  }

  // --- mutation_counter: max(A,B) + (1 if anything mutated), saturate 15 ----
  {
    int mc = (int)NT_MAX(gene_mutations(A), gene_mutations(B));
    if (any_mutation) mc += 1;
    GN_SET(c.g2, GN_MUTCNT_SH, GN_MUTCNT_MK, (uint8_t)NT_MIN(mc, 15));
  }

  // --- god_tainted: A | B, never cleared ------------------------------------
  GN_SET(c.g2, GN_TAINT_SH, GN_TAINT_MK,
         (uint8_t)((gene_tainted(A) | gene_tainted(B)) ? 1u : 0u));

  // --- lineage: from the higher cq_hi parent; 6% founder mutation -----------
  if (chance_pct(6)) {
    c.lineage_id = rnd_nonzero_u32();
    flags |= EF_NEW_LINEAGE;                       // "NUEVA ESTIRPE"
  } else {
    c.lineage_id = (cq_hi_b > cq_hi_a) ? B.lineage_id : A.lineage_id;
    if (c.lineage_id == 0u) c.lineage_id = rnd_nonzero_u32();
  }

  // --- inbreeding: hardiness -2 (reflected), flag for the life-long penalty -
  if (genome_is_inbred(A, B)) {
    const GeneDef& d = NUMGENE[NG_HARDINESS];
    gd_put_reflected(c, d, (int)gd_get(c, d) - 2);
    flags |= EF_INBRED;                            // -> PetSave PF_INBRED
  }

  // --- bookkeeping ----------------------------------------------------------
  {
    int gen = (int)NT_MAX(A.generation, B.generation) + 1;
    c.generation = (uint8_t)NT_MIN(gen, 255);
  }
  c.parent_tag = (uint8_t)((A.lineage_id ^ B.lineage_id
                            ^ (uint32_t)A.g1 ^ (uint32_t)B.g1) & 0xFFu);

  genome_seal(c);
  if (out_flags) *out_flags = flags;
  return c;
}

// =============================================================================
// 9. DEATH-EGG - ONE PARENT, PARTHENOGENESIS (GAME_DESIGN 4.3)
//    Drifts and specialises: pushes a dynasty further into whatever killed it.
// =============================================================================

Genome genome_death_egg(const Genome& parent, uint8_t cause, int16_t cq_at_death) {
  Genome c = parent;                     // step 1: full copy

  // --- step 2: numeric genes, p = 18% each ---------------------------------
  for (uint8_t i = 0; i < NG_COUNT; ++i) {
    const GeneDef& d = NUMGENE[i];
    if (chance_pct(18)) {
      gd_put_reflected(c, d, (int)gd_get(c, d) + genome_mut_delta());
    }
  }

  // --- step 3: morphology churn --------------------------------------------
  if (chance_pct(25)) {                  // pattern: re-roll
    GN_SET(c.g0, GN_PATTERN_SH, GN_PATTERN_MK, (uint8_t)rnd_below(PATTERN_COUNT));
  }
  if (chance_pct(60)) {                  // palette: +-1 reflected
    gd_put_reflected(c, CATGENE[CG_PALETTE],
                     (int)gd_get(c, CATGENE[CG_PALETTE]) + (coin() ? 1 : -1));
  }
  if (chance_pct(20)) {                  // body_size: +-1 reflected
    gd_put_reflected(c, CATGENE[CG_BODYSIZE],
                     (int)gd_get(c, CATGENE[CG_BODYSIZE]) + (coin() ? 1 : -1));
  }
  if (chance_pct(15)) {                  // ear_horn: re-roll
    GN_SET(c.g0, GN_EARHORN_SH, GN_EARHORN_MK, (uint8_t)rnd_below(4));
  }

  // --- step 4: species, p = 6%, +-1 within [0,11] (adjacent only) -----------
  // A hybrid-exclusive species (12..15) is left alone: those shapes exist only
  // through BLE recombination and a parthenogenetic line must not launder one
  // back into the ordinary [0,11] range.
  if (gd_get(c, CATGENE[CG_SPECIES]) <= 11 && chance_pct(6)) {
    gd_put_reflected(c, CATGENE[CG_SPECIES],
                     (int)gd_get(c, CATGENE[CG_SPECIES]) + (coin() ? 1 : -1));
  }

  // --- step 5: directed drift by cause of death ----------------------------
  switch (cause) {
    case DEATH_HUNGER:
      gd_put_reflected(c, NUMGENE[NG_APPETITE],
                       (int)gd_get(c, NUMGENE[NG_APPETITE]) - 2);
      break;
    case DEATH_ILLNESS:
      gd_put_reflected(c, NUMGENE[NG_HARDINESS],
                       (int)gd_get(c, NUMGENE[NG_HARDINESS]) + 2);
      break;
    case DEATH_FILTH:
      gd_put_reflected(c, NUMGENE[NG_METABOLISM],
                       (int)gd_get(c, NUMGENE[NG_METABOLISM]) - 1);
      gd_put_reflected(c, NUMGENE[NG_HARDINESS],
                       (int)gd_get(c, NUMGENE[NG_HARDINESS]) + 1);
      break;
    case DEATH_NEGLECT:
      // The line drifts toward not needing you - and it shows.
      gd_put_reflected(c, NUMGENE[NG_SOCIABILITY],
                       (int)gd_get(c, NUMGENE[NG_SOCIABILITY]) - 3);
      break;
    case DEATH_OLD_AGE:
      if (cq_at_death >= 700) {
        uint8_t i = (uint8_t)rnd_below(NG_COUNT);
        gd_put_reflected(c, NUMGENE[i], (int)gd_get(c, NUMGENE[i]) + 1);
        if (chance_pct(10)) GN_SET(c.g2, GN_RARE_SH, GN_RARE_MK, 1);
      }
      break;
    default:
      break;   // DEATH_NONE / DEATH_SADNESS / DEATH_ACCIDENT: no directed drift
  }

  // --- step 8: forced-novelty guarantee ------------------------------------
  // Steps 2..5 may have produced nothing visible. Every death-egg must be a
  // visibly different creature from its parent, so force one change in the
  // morphology word AND one in the physiology word.
  if (c.g0 == parent.g0 && c.g1 == parent.g1) {
    uint8_t p0 = GN_GET(c.g0, GN_PATTERN_SH, GN_PATTERN_MK);
    uint8_t np;
    do { np = (uint8_t)rnd_below(PATTERN_COUNT); } while (np == p0);
    GN_SET(c.g0, GN_PATTERN_SH, GN_PATTERN_MK, np);

    // Only the four g1 numeric genes, so g1 is guaranteed to move too.
    const GeneDef& d = NUMGENE[rnd_below(NG_G1_COUNT)];
    int v  = (int)gd_get(c, d);
    int nv = genome_reflect(v + (coin() ? 1 : -1), d.lo, d.hi);
    if (nv == v) nv = genome_reflect(v + 1, d.lo, d.hi);   // ranges are >2 wide
    if (nv == v) nv = genome_reflect(v - 1, d.lo, d.hi);
    gd_put(c, d, (uint8_t)nv);
  }

  // --- step 6: mutation_counter += 1 always, saturate 15 -------------------
  {
    int mc = (int)gene_mutations(c) + 1;
    GN_SET(c.g2, GN_MUTCNT_SH, GN_MUTCNT_MK, (uint8_t)NT_MIN(mc, 15));
  }

  // --- step 7: lineage_id preserved (only BLE can break a dynasty) ---------
  c.lineage_id = parent.lineage_id;
  if (c.lineage_id == 0u) c.lineage_id = rnd_nonzero_u32();

  // --- step 9: bookkeeping -------------------------------------------------
  c.generation = (uint8_t)NT_MIN((int)parent.generation + 1, 255);
  c.parent_tag = (uint8_t)((parent.lineage_id ^ (uint32_t)parent.g1) & 0xFFu);

  genome_seal(c);
  return c;
}

// =============================================================================
// 10. HEX SERIALISATION
// =============================================================================

static const char HEX_UP[] = "0123456789ABCDEF";

void genome_to_hex32(const Genome& g, char* out33) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&g);
  for (uint8_t i = 0; i < sizeof(Genome); ++i) {
    out33[i * 2 + 0] = HEX_UP[(p[i] >> 4) & 0x0F];
    out33[i * 2 + 1] = HEX_UP[p[i] & 0x0F];
  }
  out33[sizeof(Genome) * 2] = '\0';
}

static int hex_val(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return -1;
}

static bool is_space(char ch) {
  return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

bool genome_from_hex32(const char* hex, Genome& out) {
  if (!hex) return false;

  while (is_space(*hex)) ++hex;

  uint8_t buf[sizeof(Genome)];
  for (uint8_t i = 0; i < sizeof(Genome); ++i) {
    int hi = hex_val(hex[i * 2 + 0]);
    if (hi < 0) return false;
    int lo = hex_val(hex[i * 2 + 1]);
    if (lo < 0) return false;
    buf[i] = (uint8_t)((hi << 4) | lo);
  }

  const char* tail = hex + sizeof(Genome) * 2;
  while (is_space(*tail)) ++tail;
  if (*tail != '\0') return false;        // trailing garbage

  Genome tmp;
  memcpy(&tmp, buf, sizeof(Genome));
  if (!genome_valid(tmp)) return false;

  out = tmp;
  return true;
}
