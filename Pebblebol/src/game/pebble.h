// =============================================================================
//  PEBBLEBOL - game/pebble.h
//  DERIVED STATS (plan 1.5.1, spec sections 10 and 11).
//
//  NOTHING DERIVED IS EVER STORED. A Pebble carries its species id, its level,
//  its genome and its CURRENT hp; hp_max, atk, def and spd are recomputed here
//  on every read. That is spec section 10, and it is what lets a content
//  rebalance move a creature's numbers without touching a single save.
//
//      hp_max = 10 + 2*base_hp + level                (xp_hp_max, ONE owner)
//      atk    = base_atk + level/3 + genome_var(0..2)
//      def    = base_def + level/3 + genome_var(0..2)
//      spd    = base_spd + level/3 + genome_var(0..2)
//
//  INTEGER ONLY. `level / 3` is truncating division on a fresh input every
//  call - there is no accumulator here and there must not be one, because this
//  is a pure function of (species, level, genome) and not an integration over
//  time. At level 30 the term is +10, so a level-30 stat spans
//  base(1..10) + 10 + gvar(0..2) = 11..22 and every intermediate stays inside
//  uint16_t.
//
//  WHERE THE VARIATION COMES FROM, AND THE READING THAT WAS REJECTED.
//  Plan 1.5.1 names two different sources three lines apart: line 187 calls
//  `creation_seed` the "individual variation source", and line 222 writes
//  "genome variation (0..2)". THE GENOME WINS, for two reasons that are not
//  preference: tools/content/balance.json's BREEDING block states outright that
//  "the only per-instance variation is Genome (0..2 per stat) and it does NOT
//  grow with generation", and the whole win-rate matrix the roster was tuned
//  against was measured at gvar = (1,1,1,1) - the genome's modal roll. The
//  creation_seed reading is cleaner in one respect (battle stats would be
//  provably independent of the care genes and of breeding selection) and is
//  recorded here as REJECTED so the next reader does not re-litigate it from
//  line 187 alone.
//
//      gvar(v) = v * 3 / 16              0..15 -> 0..2, and 0-5/6-10/11-15
//      atk <- gene_temperament   def <- gene_hardiness   spd <- gene_metabolism
//
//  Genesis rolls every gene in [4, 12] (nt_types.h GENESIS_GENE_MIN/MAX), so a
//  fresh Pebble draws 0 / 1 / 2 with probability 2/9, 5/9, 2/9 - symmetric,
//  mode 1. Direction check on the one gene with an existing meaning in the
//  opposite direction: gene_hardiness_mult runs 1300 -> 700 as the gene rises
//  ("lower = tougher" damage multiplier), so a higher hardiness gene IS a
//  tougher creature and a higher DEF. The mapping is monotone the right way.
//
//  TWO GENES ARE NOW DOUBLE-BOOKED and that is a design decision, not an
//  accident: metabolism already scales energy decay and temperament already
//  picks a Temperament class, so a fast-metabolism Pebble both burns energy and
//  moves first. genome_breed() averages numeric genes, so gvar is heritable -
//  but bounded by the parents' range plus mutation and hard-capped at 2, so no
//  escalation across generations is possible.
//
//  PURE MODULE. stdint, the species table, the genome and game/xp.h. No
//  Arduino, no clock, no RNG, no I/O.
// =============================================================================
#ifndef PB_GAME_PEBBLE_H
#define PB_GAME_PEBBLE_H

#include <stdint.h>

#include "../data/species_table.h"
#include "../persistence/save_schema.h"
#include "genome.h"

// The maximum a genome may add to one stat (tools/content/balance.json
// BREEDING.GENOME_VAR_MAX). It does not grow with generation.
#define PEBBLE_GENOME_VAR_MAX   2

struct PebbleStats {
  uint16_t hp_max;
  uint8_t  atk;
  uint8_t  def;
  uint8_t  spd;
};

// 0..15 gene value -> 0..PEBBLE_GENOME_VAR_MAX. Public because tests and the
// creator validator both need the same fold, and because it is the one line of
// this module a reader will want to check by hand.
uint8_t pebble_genome_var(uint8_t gene_value);

// The three variation terms of one genome, in stat order (atk, def, spd).
void pebble_genome_vars(const Genome& g, uint8_t out[3]);

// THE derivation. `level` is clamped into 1..PB_LEVEL_MAX; a level of 0 (an
// un-initialised or migrated Pebble) is treated as 1 rather than producing a
// creature with no stats.
void pebble_derive_stats(const SpeciesDef& sp, uint8_t level, const Genome& g,
                         PebbleStats& out);

// The same derivation straight off a stored Pebble. Returns false and leaves
// `out` zeroed when species_id resolves to no row - which is what a save from a
// newer content pack looks like, and the caller must show that honestly rather
// than invent a maximum.
bool pebble_stats_of(const PebbleInstance& p, PebbleStats& out);

// =============================================================================
//  THE DYNASTY NAME (spec section 54, moved out of ui/ui.cpp by P9-C4)
//
//  The name of a Pebble with no species row is a PURE FUNCTION of
//  (lineage_id, generation) - "a given pet is called the same thing on every
//  device, forever", as ui.cpp put it - and that made it the one shipped
//  algorithm in this tree that NO TEST COULD REACH: ui/ui.cpp includes
//  <Arduino.h>, so no host binary links ui.o, and
//  tests/test_persistence.cpp had to TRANSCRIBE the hash by hand to reason
//  about a migrated pet's name. Two implementations of one algorithm, and
//  changing the constant in ui.cpp failed nothing anywhere.
//
//  WHAT MOVED, AND WHAT DELIBERATELY DID NOT. Only the hash and the two indices
//  are here. The SYLLABLE REPERTOIRE stays in core/strings_es.h and the caller
//  does the lookup, because:
//    * pebble.h's own banner says this is a pure module - stdint, the species
//      table and the genome - and pulling the Spanish string block into game/
//      would put the UI's text repertoire behind a game-layer call. It is legal
//      (tools/check.sh only forbids Arduino.h, u8g2, gfx.h and render.h in
//      src/game) and it is still the wrong side of the line strings_es.h draws.
//    * snprintf() would have come with it. game/ has no stdio today and the
//      layer banner says "no I/O"; pebble_name_join() below is the six lines
//      that replace it and it is strictly better than the %s%s it replaces -
//      see its own comment about UTF-8.
//
//  THE OLD CALLER DID `% 12u` AND SO DID S_SYL_A/B. One bound, two copies. The
//  fold happens exactly once, here, and PB_NAME_SYLLABLES is the number both
//  sides now agree on through core/strings_es.h's own static_asserts.
// =============================================================================
#define PB_NAME_SYLLABLES 12

// The two syllable indices of the dynasty name, each already folded into
// 0..PB_NAME_SYLLABLES-1. out must have 2 elements; a null out is a no-op.
//
// THE HASH IS UNCHANGED, byte for byte, from the one ui.cpp shipped: the
// golden-ratio word, the murmur constant on the generation, and xorshift-star's
// 0x2545F491 mix. A different answer here would silently rename every Pebble on
// every device that ever ran an earlier build, and persistence/migration.cpp
// reasons about that name in prose.
void pebble_name_syllables(uint32_t lineage_id, uint8_t generation, uint8_t out[2]);

// Joins two NUL-terminated UTF-8 syllables into `out`, always NUL-terminating
// inside `cap` bytes, and NEVER SPLITTING A MULTI-BYTE SEQUENCE. Returns the
// number of bytes written, excluding the terminator, SATURATING at 255 (the
// widest name in the repertoire is 8 bytes, so it cannot fire here).
//
// THE UTF-8 RULE IS THE POINT AND IT IS A FIX, not a transcription: ui.cpp used
// snprintf(out, cap, "%s%s", ...), which truncates on a BYTE boundary, and the
// repertoire is not ASCII - core/strings_es.h is UTF-8 with a Latin-1
// restricted repertoire, so "Ña" + "rrón" is 6 glyphs and EIGHT BYTES, and the
// widest name in the repertoire is exactly that one. A buffer sized in
// characters therefore cut a two-byte sequence in half and handed drawUTF8() a
// broken lead byte.
//
// THE RESULT IS ALWAYS A PREFIX OF THE WHOLE NAME, truncated on a CHARACTER
// boundary - which is the rule the plan's section 65 box asks for ("long
// nicknames truncated on codepoint boundaries"). The second syllable is
// therefore only reached when the first fitted whole: otherwise a one-byte
// budget would drop "Ña" and write the "r" of "rrón", showing the player a
// word that is not the beginning of their Pebble's name.
//
// cap == 0 writes nothing at all (there is nowhere to put a terminator); a null
// out writes nothing. Either answers 0.
uint8_t pebble_name_join(const char* a, const char* b, char* out, uint16_t cap);

#endif  // PB_GAME_PEBBLE_H
