// =============================================================================
//  PEBBLEBOL host test - test_stats.cpp
//  DERIVED STATS (game/pebble.cpp, plan P4-C1).
//
//  What this file has to prove, in the plan's own words: "integer, monotonic in
//  level, bounds". Plus the two things that make those words mean something:
//
//    * NOTHING IS STORED. The same (species, level, genome) always derives the
//      same numbers, and deriving twice never changes a Pebble.
//    * THE GENOME REALLY CONTRIBUTES, and by a bounded amount. A variation term
//      that was silently always zero would pass "monotonic in level" happily.
//
//  Pure: game/pebble.cpp plus the genome. No clock, no I/O.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "data/species_table.h"
#include "game/pebble.h"
#include "game/genome.h"
#include "game/xp.h"

// A genome whose four numeric genes are all `v`. genome_defaults() is not used
// because these tests need to drive the variation genes to their extremes.
static Genome genome_flat(uint8_t v) {
  Genome g;
  memset(&g, 0, sizeof g);
  gene_set_temperament(g, v);
  gene_set_hardiness(g, v);
  gene_set_metabolism(g, v);
  return g;
}

// =============================================================================
//  1. THE FOLD: 0..15 -> 0..2
// =============================================================================
TEST(the_genome_fold_covers_exactly_zero_to_two_and_is_monotonic) {
  uint8_t last = 0;
  int seen[PEBBLE_GENOME_VAR_MAX + 1] = { 0 };
  for (uint8_t v = 0; v <= 15; ++v) {
    const uint8_t f = pebble_genome_var(v);
    CHECK(f <= PEBBLE_GENOME_VAR_MAX);
    CHECK(f >= last);                       // never goes down
    last = f;
    seen[f]++;
  }
  // v*3/16 splits 0-5 / 6-10 / 11-15.
  CHECK_EQ(seen[0], 6);
  CHECK_EQ(seen[1], 5);
  CHECK_EQ(seen[2], 5);
  CHECK_EQ(pebble_genome_var(0), 0);
  CHECK_EQ(pebble_genome_var(5), 0);
  CHECK_EQ(pebble_genome_var(6), 1);
  CHECK_EQ(pebble_genome_var(10), 1);
  CHECK_EQ(pebble_genome_var(11), 2);
  CHECK_EQ(pebble_genome_var(15), 2);
  // A gene wider than four bits still cannot escape the cap.
  CHECK_EQ(pebble_genome_var(200), PEBBLE_GENOME_VAR_MAX);
}

TEST(the_three_variation_genes_are_the_ones_the_header_names) {
  Genome g;
  memset(&g, 0, sizeof g);
  gene_set_temperament(g, 15);      // atk
  gene_set_hardiness(g, 0);         // def
  gene_set_metabolism(g, 8);        // spd
  uint8_t v[3];
  pebble_genome_vars(g, v);
  CHECK_EQ(v[0], 2);
  CHECK_EQ(v[1], 0);
  CHECK_EQ(v[2], 1);
  // ...and they reach the derived stats in that order.
  const SpeciesDef* sp = species_get(1);          // Paketo, 4/4/4/4
  CHECK(sp != nullptr);
  if (!sp) return;
  PebbleStats s;
  pebble_derive_stats(*sp, 1, g, s);
  CHECK_EQ(s.atk, 4 + 0 + 2);
  CHECK_EQ(s.def, 4 + 0 + 0);
  CHECK_EQ(s.spd, 4 + 0 + 1);
}

// =============================================================================
//  2. MONOTONIC IN LEVEL, AND BOUNDED
// =============================================================================
TEST(every_stat_is_non_decreasing_in_level_for_every_species) {
  const Genome g = genome_flat(8);
  for (uint8_t id = 1; id <= SPECIES_TABLE_COUNT; ++id) {
    const SpeciesDef* sp = species_get(id);
    CHECK(sp != nullptr);
    if (!sp) continue;
    PebbleStats prev;
    pebble_derive_stats(*sp, 1, g, prev);
    for (uint8_t lv = 2; lv <= (uint8_t)PB_LEVEL_MAX; ++lv) {
      PebbleStats cur;
      pebble_derive_stats(*sp, lv, g, cur);
      CHECK(cur.hp_max >= prev.hp_max);
      CHECK(cur.atk >= prev.atk);
      CHECK(cur.def >= prev.def);
      CHECK(cur.spd >= prev.spd);
      prev = cur;
    }
  }
}

TEST(the_level_term_is_exactly_level_over_three_and_it_really_rises) {
  const SpeciesDef* sp = species_get(1);
  CHECK(sp != nullptr);
  if (!sp) return;
  const Genome g = genome_flat(0);          // no variation at all
  for (uint8_t lv = 1; lv <= (uint8_t)PB_LEVEL_MAX; ++lv) {
    PebbleStats s;
    pebble_derive_stats(*sp, lv, g, s);
    CHECK_EQ(s.atk, (uint8_t)(sp->base_atk + lv / 3));
    CHECK_EQ(s.def, (uint8_t)(sp->base_def + lv / 3));
    CHECK_EQ(s.spd, (uint8_t)(sp->base_spd + lv / 3));
    CHECK_EQ(s.hp_max, xp_hp_max(sp->base_hp, lv));
  }
  // Level 30 really is +10, so the span the balance matrix was measured over
  // is the span this code produces.
  PebbleStats top;
  pebble_derive_stats(*sp, (uint8_t)PB_LEVEL_MAX, g, top);
  CHECK_EQ(top.atk, (uint8_t)(sp->base_atk + 10));
}

TEST(no_stat_can_leave_the_range_the_balance_matrix_was_measured_over) {
  // base 1..10, +level/3 at most 10, +gvar at most 2 => 3..22 across the whole
  // roster at every level with any genome. The widest effective ATK P4-C2 can
  // ever see is that 22 plus a +2 buff stage, i.e. 24 - the number the damage
  // formula's overflow argument is written against.
  for (uint8_t geneval = 0; geneval <= 15; ++geneval) {
    const Genome g = genome_flat(geneval);
    for (uint8_t id = 1; id <= SPECIES_TABLE_COUNT; ++id) {
      const SpeciesDef* sp = species_get(id);
      if (!sp) continue;
      for (uint8_t lv = 1; lv <= (uint8_t)PB_LEVEL_MAX; ++lv) {
        PebbleStats s;
        pebble_derive_stats(*sp, lv, g, s);
        CHECK(s.atk >= 1 && s.atk <= 22);
        CHECK(s.def >= 1 && s.def <= 22);
        CHECK(s.spd >= 1 && s.spd <= 22);
        CHECK(s.hp_max >= 13 && s.hp_max <= 60);
      }
    }
  }
}

// =============================================================================
//  3. THE GENOME REALLY MOVES THE NUMBERS - and by no more than 2
// =============================================================================
TEST(the_genome_changes_the_stats_and_the_spread_is_exactly_two) {
  const SpeciesDef* sp = species_get(1);
  CHECK(sp != nullptr);
  if (!sp) return;
  PebbleStats lo, hi;
  pebble_derive_stats(*sp, 10, genome_flat(0), lo);
  pebble_derive_stats(*sp, 10, genome_flat(15), hi);
  CHECK_EQ((int)(hi.atk - lo.atk), PEBBLE_GENOME_VAR_MAX);
  CHECK_EQ((int)(hi.def - lo.def), PEBBLE_GENOME_VAR_MAX);
  CHECK_EQ((int)(hi.spd - lo.spd), PEBBLE_GENOME_VAR_MAX);
  // hp_max is NOT a genome stat: it is the one derived number two Pebbles of
  // the same species and level always share, which is what lets xp_hp_rescale()
  // work off species and level alone.
  CHECK_EQ(hi.hp_max, lo.hp_max);
}

TEST(a_genesis_genome_lands_on_the_gvar_the_matrix_was_measured_at) {
  // Genesis rolls every gene in [GENESIS_GENE_MIN, GENESIS_GENE_MAX] = [4, 12],
  // which folds to 0 / 1 / 2 - symmetric, mode 1. The pack's whole win-rate
  // matrix was simulated at gvar = 1 on every stat, so this is the claim that
  // says the shipped derivation matches the tuning.
  int hist[PEBBLE_GENOME_VAR_MAX + 1] = { 0 };
  for (uint8_t v = GENESIS_GENE_MIN; v <= GENESIS_GENE_MAX; ++v)
    hist[pebble_genome_var(v)]++;
  CHECK_EQ(hist[0], 2);      // 4, 5
  CHECK_EQ(hist[1], 5);      // 6..10
  CHECK_EQ(hist[2], 2);      // 11, 12
  CHECK(hist[1] > hist[0] && hist[1] > hist[2]);
}

// =============================================================================
//  4. DERIVED MEANS DERIVED
// =============================================================================
TEST(deriving_twice_gives_the_same_answer_and_never_touches_the_pebble) {
  PebbleInstance p;
  memset(&p, 0, sizeof p);
  p.magic      = (uint16_t)PEBBLE_MAGIC;
  p.layout_ver = (uint8_t)PEBBLE_LAYOUT_VER;
  p.species_id = 5;
  p.level      = 17;
  p.genome     = genome_flat(9);

  PebbleInstance before = p;
  PebbleStats a, b;
  CHECK(pebble_stats_of(p, a));
  CHECK(pebble_stats_of(p, b));
  CHECK_EQ(a.hp_max, b.hp_max);
  CHECK_EQ(a.atk, b.atk);
  CHECK_EQ(a.def, b.def);
  CHECK_EQ(a.spd, b.spd);
  CHECK_EQ(memcmp(&before, &p, sizeof p), 0);
}

TEST(a_level_of_zero_is_read_as_level_one_and_an_overflow_level_clamps) {
  const SpeciesDef* sp = species_get(1);
  CHECK(sp != nullptr);
  if (!sp) return;
  const Genome g = genome_flat(8);
  PebbleStats zero, one, over, top;
  pebble_derive_stats(*sp, 0, g, zero);
  pebble_derive_stats(*sp, 1, g, one);
  pebble_derive_stats(*sp, 255, g, over);
  pebble_derive_stats(*sp, (uint8_t)PB_LEVEL_MAX, g, top);
  CHECK_EQ(zero.hp_max, one.hp_max);
  CHECK_EQ(zero.atk, one.atk);
  CHECK_EQ(over.hp_max, top.hp_max);
  CHECK_EQ(over.atk, top.atk);
}

TEST(an_unresolvable_species_derives_nothing_rather_than_inventing_a_maximum) {
  PebbleInstance p;
  memset(&p, 0, sizeof p);
  p.species_id = 0;
  p.level = 10;
  PebbleStats s;
  CHECK(!pebble_stats_of(p, s));
  CHECK_EQ(s.hp_max, 0);
  CHECK_EQ(s.atk, 0);

  p.species_id = (uint8_t)(SPECIES_TABLE_COUNT + 1);
  CHECK(!pebble_stats_of(p, s));
  CHECK_EQ(s.hp_max, 0);

  p.species_id = 200;                 // the creator range, unresolvable until P8
  CHECK(!pebble_stats_of(p, s));
}

TEST(hp_max_is_the_same_rule_the_xp_module_owns) {
  // There is ONE hp_max formula in the firmware. If pebble.cpp ever grew its
  // own copy, this is where the two would part company.
  const Genome g = genome_flat(12);
  for (uint8_t id = 1; id <= SPECIES_TABLE_COUNT; ++id) {
    const SpeciesDef* sp = species_get(id);
    if (!sp) continue;
    for (uint8_t lv = 1; lv <= (uint8_t)PB_LEVEL_MAX; lv = (uint8_t)(lv + 7u)) {
      PebbleStats s;
      pebble_derive_stats(*sp, lv, g, s);
      CHECK_EQ(s.hp_max, xp_hp_max(sp->base_hp, lv));
    }
  }
}
