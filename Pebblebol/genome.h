// =============================================================================
//  NOTTAMAGOCHI - genome.h
//  Pure genetics: CRC, genesis roll, gene accessors, gene->multiplier maps,
//  two-parent breeding, single-parent death-egg drift, inbreeding check,
//  32-hex-char serialisation.
//
//  LAYERING (BRIEF 4): this module includes NO Arduino, WiFi, BLE, WebServer or
//  U8g2 header. It performs no I/O and touches no global game state. It is
//  compiled and unit-tested on the host by tests/test_genome.cpp.
//
//  ZERO FLOATING POINT. Every multiplier is an integer per-mille value (x1000).
//
//  RANDOMNESS: the module never calls esp_random() itself. Every draw comes
//  from the RNG_BREEDING stream of rng.h, which the .ino seeds once at boot
//  through rng_seed_all(); genome_seed() reseeds that stream so every function
//  here is deterministic under a fixed seed (host tests, goldens).
//
//  ENDIANNESS: Genome is serialised (BLE, NVS, hex) in struct memory order.
//  Both the ESP32-C3 (RISC-V) and the host test (x86-64) are little-endian.
// =============================================================================
#ifndef NT_GENOME_H
#define NT_GENOME_H

#include <stdint.h>
#include "nt_types.h"

static_assert(sizeof(Genome) == 16, "genome.h: Genome wire size is contractual");

// -----------------------------------------------------------------------------
// 1. RANDOMNESS
// -----------------------------------------------------------------------------

// Test hook: a scripted 32-bit source, uint32_t (*)(void).
typedef uint32_t (*GenomeRngFn)(void);

// Install a scripted source (host tests that need an exact gene roll). Passing
// nullptr restores the RNG_BREEDING stream. Not thread-safe.
void     genome_set_rng(GenomeRngFn fn);

// Reseed the RNG_BREEDING stream (wrapper on rng_seed()). seed == 0 is
// remapped to RNG_DEFAULT_SEED (0 is an absorbing state for xorshift). Has no
// effect on draws while a scripted source is installed.
void     genome_seed(uint32_t seed);

// Current 32-bit random draw from whichever source is installed.
uint32_t genome_rand(void);

// -----------------------------------------------------------------------------
// 2. INTEGRITY
// -----------------------------------------------------------------------------

// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, no final xor)
// over bytes 0..13 of the struct. Check value for "123456789" is 0x29B1.
uint16_t genome_crc16(const Genome& g);

// Stamp magic_ver, clear the g2 reserved bits and recompute crc16. Every
// constructor below ends with this. Call it after any gene_set_*() burst if
// you used the raw setters (gene_set_*() already reseals).
void     genome_seal(Genome& g);

// True iff: signature bits match GENOME_SIG, proto version matches,
// lineage_id != 0, and crc16 is correct. Reject anything else on the wire.
bool     genome_valid(const Genome& g);

// -----------------------------------------------------------------------------
// 3. CONSTRUCTION  (GAME_DESIGN 3.1 genesis, 4.2 mating, 4.3 death-egg)
// -----------------------------------------------------------------------------

// Generation-0 roll. species in [0,7], numeric genes in [4,12] (luck [2,6] -
// the 3-bit gene is scaled), mutation_counter 0, generation 0, rare_flag 3%,
// god_tainted 0, lineage_id a non-zero random dynasty id.
Genome   genome_genesis(void);

// Two-parent BLE mating. Numeric genes average then mutate at p=8%;
// categoricals cross over 50/50 then re-roll at p=3%; different species have a
// 12% chance of producing a recombinant from the hybrid table; 6% chance of a
// brand-new lineage ("mutacion fundadora").
//   cq_hi_a / cq_hi_b : the cq_hi byte each unit broadcast in its BLE BEACON
//                       frame. lineage_id is inherited from the higher one
//                       (ties -> A). Defaults make genome_breed(A,B) legal.
//   out_flags         : optional. Receives EF_FROM_MATING plus any of
//                       EF_INBRED / EF_NEW_LINEAGE / EF_HYBRID. Never EF_VALID
//                       (that is the storage layer's bit).
Genome   genome_breed(const Genome& A, const Genome& B,
                      uint8_t cq_hi_a = 0, uint8_t cq_hi_b = 0,
                      uint8_t* out_flags = nullptr);

// Single-parent parthenogenesis run when the pet dies. Much higher mutation
// than mating, plus directed drift by cause of death, plus a forced-novelty
// guarantee: g0 and g1 are ALWAYS different from the parent's.
//   cause        : a DeathCause value (uint8_t so a PetSave field passes
//                  straight through).
//   cq_at_death  : care quality at death. Only used by the DEATH_OLD_AGE
//                  branch, which needs cq >= 700 to grant its bonus.
// lineage_id is always preserved: only BLE mating can break a dynasty.
Genome   genome_death_egg(const Genome& parent, uint8_t cause,
                          int16_t cq_at_death = 0);

// -----------------------------------------------------------------------------
// 4. INHERITANCE PRIMITIVES (exposed for god mode and for the host tests)
// -----------------------------------------------------------------------------

// Reflect off the bounds instead of clamping, so dynasties never saturate at
// 0 or 15. Idempotent for values already in range.
int      genome_reflect(int v, int lo, int hi);

// One draw from {-3:5%, -2:15%, -1:30%, +1:30%, +2:15%, +3:5%}. Never 0.
int      genome_mut_delta(void);

// Inbreeding test (GAME_DESIGN 4.2): same parent fingerprint AND same dynasty.
bool     genome_is_inbred(const Genome& A, const Genome& B);

// Mating legality (GAME_DESIGN 3.1): different sex, unless either side has
// luck >= 6. Genetics only - the caller still checks RSSI, energy and stage.
bool     genome_can_mate(const Genome& A, const Genome& B);

// Pair mating success, per-mille (GAME_DESIGN 4.2):
// (socMultA + socMultB)/2 * 0.5, clamped to [250, 900].
uint16_t genome_mate_success_permille(const Genome& A, const Genome& B);

// Recombinant species for a mixed-species pair. Order-independent. Returns a
// value in [0,15]; 12..15 are the mating-exclusive shapes
// (ESPEJO / NUDO / ECO / VACIO). a == b returns a.
uint8_t  genome_hybrid_species(uint8_t a, uint8_t b);

// -----------------------------------------------------------------------------
// 5. GENE ACCESSORS - raw values, exactly as stored (GAME_DESIGN 3.1)
// -----------------------------------------------------------------------------
uint8_t  gene_species(const Genome& g);       // 0..15 (0..11 real, 12..15 hybrid)
uint8_t  gene_pattern(const Genome& g);       // 0..15
uint8_t  gene_palette(const Genome& g);       // 0..7
uint8_t  gene_body_size(const Genome& g);     // 0..7
uint8_t  gene_ear_horn(const Genome& g);      // 0..3
uint8_t  gene_appetite(const Genome& g);      // 0..15
uint8_t  gene_metabolism(const Genome& g);    // 0..15
uint8_t  gene_sociability(const Genome& g);   // 0..15
uint8_t  gene_temperament(const Genome& g);   // 0..15
uint8_t  gene_hardiness(const Genome& g);     // 0..15
uint8_t  gene_luck(const Genome& g);          // 0..7
uint8_t  gene_mutations(const Genome& g);     // 0..15, saturating
uint8_t  gene_sex(const Genome& g);           // 0 = circle, 1 = filled
uint8_t  gene_rare(const Genome& g);          // 0/1
uint8_t  gene_tainted(const Genome& g);       // 0/1, god mode taint, never cleared
uint8_t  gene_temper_class(const Genome& g);  // Temperament: temperament >> 2

// Raw setters for god mode ("GENOMA / EDITAR"). Each one masks the value into
// range and RESEALS the genome (crc stays correct), so a half-edited genome is
// never observable. Do not use these on the live pet outside god mode.
void     gene_set_species(Genome& g, uint8_t v);
void     gene_set_pattern(Genome& g, uint8_t v);
void     gene_set_palette(Genome& g, uint8_t v);
void     gene_set_body_size(Genome& g, uint8_t v);
void     gene_set_ear_horn(Genome& g, uint8_t v);
void     gene_set_appetite(Genome& g, uint8_t v);
void     gene_set_metabolism(Genome& g, uint8_t v);
void     gene_set_sociability(Genome& g, uint8_t v);
void     gene_set_temperament(Genome& g, uint8_t v);
void     gene_set_hardiness(Genome& g, uint8_t v);
void     gene_set_luck(Genome& g, uint8_t v);
void     gene_set_mutations(Genome& g, uint8_t v);
void     gene_set_sex(Genome& g, uint8_t v);
void     gene_set_rare(Genome& g, uint8_t v);
void     gene_set_tainted(Genome& g, uint8_t v);

// -----------------------------------------------------------------------------
// 6. GENE -> MULTIPLIER MAPS - INTEGER, PER-MILLE (x1000). No floating point.
//
//    Base formula (GAME_DESIGN 3.1): mult = 600 + v*60  (config.h
//    GENE_MULT_BASE / GENE_MULT_STEP), i.e. 0.60 + v*0.06.
//
//    rare_flag ("+1 to all _mult favourably", GAME_DESIGN 3.1) is applied here
//    as one extra gene step in the beneficial direction before the formula,
//    clamped to the gene's range:
//      appetite   -1  (lower appetite  = slower hunger decay)
//      metabolism -1  (lower metabolism= slower energy/weight/poop)
//      sociability+1  (higher = less loneliness penalty, better mating)
//      hardiness  +1  (higher gene = LOWER damage multiplier)
//      luck       +1
//    Use the raw gene_*() accessors above if you need the unmodified value.
// -----------------------------------------------------------------------------
uint16_t gene_appetite_mult(const Genome& g);       // 600..1500 (hunger decay x1000)
uint16_t gene_metabolism_mult(const Genome& g);     // 600..1500 (energy/weight x1000)
uint16_t gene_sociability_mult(const Genome& g);    // 700..1300 (x1000)
uint16_t gene_hardiness_mult(const Genome& g);      // 1300..700 (damage x1000, lower=tougher)

uint16_t gene_weight_ideal_dg(const Genome& g);     // 300 + body_size*50, decigrams

// -----------------------------------------------------------------------------
// 7. SERIALISATION - 32 uppercase hex chars, struct memory order
// -----------------------------------------------------------------------------

// Writes exactly 32 hex chars plus a NUL. out must have room for 33 bytes.
void     genome_to_hex32(const Genome& g, char* out33);

// Parses 32 hex chars (upper or lower case). Leading/trailing whitespace and a
// trailing CR/LF are tolerated; nothing else is. Returns true only if the
// parse succeeded AND genome_valid() passes on the result, so a mistyped paste
// in god mode or a corrupt QR/BLE frame cannot install a broken genome.
// `out` is left untouched on failure.
bool     genome_from_hex32(const char* hex, Genome& out);

#endif // NT_GENOME_H
