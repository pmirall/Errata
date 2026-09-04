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

#endif  // PB_GAME_PEBBLE_H
