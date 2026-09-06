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
#include "core/strings_es.h"   // S_SYL_A/B: the repertoire the NAME cases look up

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

// =============================================================================
//  THE DYNASTY NAME (P9-C4 moved it here out of ui/ui.cpp)
//
//  IT HAD NO TEST AND COULD NOT HAVE ONE. ui/ui.cpp includes <Arduino.h>, so no
//  host binary links ui.o, and the consequence was visible in
//  tests/test_persistence.cpp, which TRANSCRIBED the hash by hand to reason
//  about a migrated pet's name - a second implementation of a shipped algorithm
//  living inside a test. Changing 0x2545F491 in ui.cpp failed nothing anywhere.
//
//  These four cases are survey-two's list, and each is written so it can fail:
//  a pinned word, generation reaching the answer, BOTH indices exercised across
//  their whole range, and the cap bound aimed at the UTF-8 over-run that the
//  snprintf() this replaces really had.
// =============================================================================

// The lookup ui.cpp now does, so a case can compare whole words rather than
// indices. Exactly ui_name_for()'s two lines, minus the Arduino.
static void name_of(uint32_t lineage, uint8_t gen, char* out, uint16_t cap) {
  uint8_t syl[2];
  pebble_name_syllables(lineage, gen, syl);
  (void)pebble_name_join(S_SYL_A(syl[0]), S_SYL_B(syl[1]), out, cap);
}

TEST(the_dynasty_name_is_a_pinned_word_and_not_merely_a_string) {
  // (1) THE PIN. These are the words the shipped hash produces; they are what a
  // player has already seen on a device, so they may not move. A mutation of
  // any constant in pebble_name_syllables() fails HERE, by name.
  char n[32];

  // Derived independently from the published rule rather than transcribed from
  // this build's output: h = lineage ^ 0x9E3779B9, h ^= gen * 0x85EBCA6B,
  // h ^= h>>15, h *= 0x2545F491, h ^= h>>13, then A[h % 12] and B[(h/12) % 12]
  // over strings_es.h's two twelve-entry rows.
  name_of(0u, 0u, n, sizeof n);
  CHECK_STR_EQ(n, "Lazo");
  name_of(1u, 0u, n, sizeof n);
  CHECK_STR_EQ(n, "Guri");
  name_of(0xDEADBEEFu, 3u, n, sizeof n);
  CHECK_STR_EQ(n, "Kezo");
  // (2) IT IS A FUNCTION, not a fresh roll: the same input twice is the same
  // word. Without this the pin above could pass on a generator with state.
  char again[32];
  name_of(0xDEADBEEFu, 3u, again, sizeof again);
  CHECK_STR_EQ(again, n);

  name_of(7u, 1u, n, sizeof n);
  CHECK_STR_EQ(n, "Masco");

  // (3) THE INDICES ARE WHAT THE WORD IS BUILT FROM, asserted separately so a
  // broken JOIN and a broken HASH cannot be confused for each other.
  uint8_t syl[2];
  pebble_name_syllables(0xDEADBEEFu, 3u, syl);
  CHECK(syl[0] < PB_NAME_SYLLABLES);
  CHECK(syl[1] < PB_NAME_SYLLABLES);
  CHECK_STR_EQ(S_SYL_A(syl[0]), "Ke");
  CHECK_STR_EQ(S_SYL_B(syl[1]), "zo");
}

TEST(the_generation_really_reaches_the_dynasty_name) {
  // Deleting the `generation` term entirely still passes a "returns a non-empty
  // string" test, so this asserts the thing that would catch it: over a spread
  // of lineages, consecutive generations must produce different words far more
  // often than not. (They CAN collide - 144 words and 256 generations - so the
  // assertion is on the rate, with the exact count printed by a failure.)
  int differ = 0, total = 0;
  for (uint32_t lin = 1u; lin < 200u; ++lin) {
    for (uint8_t g = 0; g < 8u; ++g) {
      char a[32], b[32];
      name_of(lin, g, a, sizeof a);
      name_of(lin, (uint8_t)(g + 1u), b, sizeof b);
      ++total;
      if (strcmp(a, b) != 0) ++differ;
    }
  }
  CHECK_EQ(total, 199 * 8);
  // 143/144 of pairs differ if the term reaches the hash; 0 if it does not.
  CHECK(differ > (total * 9) / 10);
}

TEST(both_syllable_indices_are_exercised_over_their_whole_range) {
  // h % 12 and (h / 12) % 12 are CORRELATED in a way a "returns something" test
  // cannot see: a mix that collapsed the high bits would leave B constant while
  // A still looked healthy. Both must reach all twelve.
  bool seen_a[PB_NAME_SYLLABLES] = { false };
  bool seen_b[PB_NAME_SYLLABLES] = { false };
  for (uint32_t lin = 0u; lin < 4000u; ++lin) {
    uint8_t syl[2];
    pebble_name_syllables(lin, 0u, syl);
    CHECK(syl[0] < PB_NAME_SYLLABLES);
    CHECK(syl[1] < PB_NAME_SYLLABLES);
    seen_a[syl[0]] = true;
    seen_b[syl[1]] = true;
  }
  int na = 0, nb = 0;
  for (int i = 0; i < PB_NAME_SYLLABLES; ++i) { if (seen_a[i]) ++na; if (seen_b[i]) ++nb; }
  CHECK_EQ(na, (int)PB_NAME_SYLLABLES);
  CHECK_EQ(nb, (int)PB_NAME_SYLLABLES);

  // And every syllable the indices can name is a real, non-empty string, so the
  // fold above cannot be quietly pointing off the end of the block.
  for (int i = 0; i < PB_NAME_SYLLABLES; ++i) {
    CHECK(S_SYL_A(i)[0] != '\0');
    CHECK(S_SYL_B(i)[0] != '\0');
  }
}

// Is `n` bytes of `s` a complete sequence of UTF-8 characters? The property
// pebble_name_join() owes is exactly this - not "the last byte is not a
// continuation byte", which is false of every legal two-byte character.
static bool utf8_well_formed(const char* s, uint16_t n) {
  uint16_t i = 0;
  while (i < n) {
    const uint8_t c = (uint8_t)s[i];
    uint16_t len;
    if      ((c & 0x80u) == 0x00u) len = 1u;
    else if ((c & 0xE0u) == 0xC0u) len = 2u;
    else if ((c & 0xF0u) == 0xE0u) len = 3u;
    else if ((c & 0xF8u) == 0xF0u) len = 4u;
    else return false;                       // a continuation byte where a lead
                                             // byte belongs: a split
    if (i + len > n) return false;           // the sequence runs past the end
    for (uint16_t k = 1u; k < len; ++k)
      if (((uint8_t)s[i + k] & 0xC0u) != 0x80u) return false;
    i = (uint16_t)(i + len);
  }
  return true;
}

TEST(the_name_cap_never_splits_a_utf8_sequence) {
  // THE BUG THIS REPLACES. ui.cpp used snprintf(out, cap, "%s%s", ...), which
  // truncates on a BYTE boundary. "Ña" + "rrón" is four glyphs and NINE bytes
  // in the UTF-8 core/strings_es.h ships, so a buffer sized in characters cut a
  // two-byte sequence in half. This aims straight at that word.
  const char* A = "Ña";     // 3 bytes: C3 91 61
  const char* B = "rrón";   // 5 bytes: 72 72 C3 B3 6E
  char buf[32];

  // Every cap from 0 to past the whole word: the result is always
  // NUL-terminated, always inside cap, and always WELL-FORMED UTF-8. The last
  // property is the one that matters and it is not "does not end on a
  // continuation byte" - the last byte of a legal two-byte character IS a
  // continuation byte - so it is checked by walking the sequences.
  for (uint16_t cap = 0; cap <= 12u; ++cap) {
    memset(buf, 0x7F, sizeof buf);
    const uint8_t w = pebble_name_join(A, B, buf, cap);
    if (cap == 0u) { CHECK_EQ((int)w, 0); CHECK_EQ((int)(uint8_t)buf[0], 0x7F); continue; }
    CHECK(w + 1u <= cap);                        // fits, terminator included
    CHECK_EQ((int)buf[w], 0);                    // and is terminated
    for (uint8_t i = 0; i < w; ++i) CHECK((uint8_t)buf[i] != 0x7Fu);  // no gaps
    CHECK(utf8_well_formed(buf, w));
    // ...and it is a PREFIX of the whole name, never a different word.
    CHECK(memcmp(buf, "\xC3\x91" "arr\xC3\xB3" "n", w) == 0);
  }

  // The named cases, spelled out so a regression says which one moved. "Ñ" is
  // itself a complete two-byte character, so a two-byte budget legitimately
  // yields it; what may never happen is half of one.
  CHECK_EQ((int)pebble_name_join(A, B, buf, 1u), 0);   CHECK_STR_EQ(buf, "");
  CHECK_EQ((int)pebble_name_join(A, B, buf, 2u), 0);   CHECK_STR_EQ(buf, "");
  CHECK_EQ((int)pebble_name_join(A, B, buf, 3u), 2);   CHECK_STR_EQ(buf, "Ñ");
  CHECK_EQ((int)pebble_name_join(A, B, buf, 4u), 3);   CHECK_STR_EQ(buf, "Ña");
  CHECK_EQ((int)pebble_name_join(A, B, buf, 7u), 5);   CHECK_STR_EQ(buf, "Ñarr");
  CHECK_EQ((int)pebble_name_join(A, B, buf, 8u), 7);   CHECK_STR_EQ(buf, "Ñarró");
  CHECK_EQ((int)pebble_name_join(A, B, buf, 9u), 8);   CHECK_STR_EQ(buf, "Ñarrón");
  CHECK_EQ((int)pebble_name_join(A, B, buf, 32u), 8);  CHECK_STR_EQ(buf, "Ñarrón");

  // THE PREFIX RULE AT ITS EDGE. A budget too small for the first syllable's
  // first character must write NOTHING, never the second syllable's first
  // character - which is what the join did before the `na < la` guard and what
  // would silently show a player the wrong word.
  CHECK_EQ((int)pebble_name_join(A, B, buf, 2u), 0);
  CHECK_EQ((int)(uint8_t)buf[0], 0u);

  // THE CONTROL. A join that DID split would have to be caught, so the checker
  // above is aimed at a deliberately broken string first: without this, an
  // always-true utf8_well_formed() would make every line above vacuous.
  char broken[4] = { (char)0xC3, (char)0x91, (char)0xC3, '\0' };   // "Ñ" + a lone lead
  CHECK(!utf8_well_formed(broken, 3u));
  CHECK(utf8_well_formed(broken, 2u));

  // A null out and a null syllable are both survivable.
  CHECK_EQ((int)pebble_name_join(A, B, nullptr, 32u), 0);
  // A NULL first syllable is an empty one, which FITS WHOLE, so the second is
  // still reached: the prefix rule is about truncation, not about absence.
  CHECK_EQ((int)pebble_name_join(nullptr, B, buf, 32u), 5); CHECK_STR_EQ(buf, "rrón");
  CHECK_EQ((int)pebble_name_join(A, nullptr, buf, 32u), 3); CHECK_STR_EQ(buf, "Ña");

  // AND THE WHOLE POINT: the widest real name still fits the buffer the UI
  // hands it. Nothing in the repertoire is wider than "Ña" + "rrón".
  uint16_t widest = 0;
  for (int a = 0; a < PB_NAME_SYLLABLES; ++a)
    for (int b = 0; b < PB_NAME_SYLLABLES; ++b) {
      const uint16_t n = (uint16_t)(strlen(S_SYL_A(a)) + strlen(S_SYL_B(b)));
      if (n > widest) widest = n;
    }
  CHECK_EQ((int)widest, 8);
}
