// =============================================================================
//  PEBBLEBOL - game/pebble.cpp
//  Derived stats. See pebble.h for the formulas, the integer discipline and
//  the creation_seed reading that was rejected.
// =============================================================================
#include "pebble.h"

#include <string.h>

#include "xp.h"    // xp_hp_max(): the ONE hp_max rule in the firmware

// 0..15 -> 0..2. v*3/16 splits the range 0-5 / 6-10 / 11-15. The multiply
// happens before the divide so the truncation is the only rounding step.
uint8_t pebble_genome_var(uint8_t gene_value)
{
  const uint16_t v = (gene_value > 15u) ? 15u : (uint16_t)gene_value;
  const uint8_t out = (uint8_t)((v * (uint16_t)(PEBBLE_GENOME_VAR_MAX + 1)) / 16u);
  return (out > PEBBLE_GENOME_VAR_MAX) ? (uint8_t)PEBBLE_GENOME_VAR_MAX : out;
}

void pebble_genome_vars(const Genome& g, uint8_t out[3])
{
  if (!out) return;
  out[0] = pebble_genome_var(gene_temperament(g));   // atk
  out[1] = pebble_genome_var(gene_hardiness(g));     // def
  out[2] = pebble_genome_var(gene_metabolism(g));    // spd
}

// base + level/3 + gvar, saturating at 255. The saturation is unreachable with
// the shipped tables (10 + 10 + 2 = 22) and exists so a hand-edited content
// pack cannot wrap a stat to a small number and make a creature look weak.
static uint8_t derive_one(uint8_t base, uint8_t level, uint8_t gvar)
{
  const uint16_t v = (uint16_t)base + (uint16_t)(level / 3u) + (uint16_t)gvar;
  return (v > 255u) ? (uint8_t)255u : (uint8_t)v;
}

void pebble_derive_stats(const SpeciesDef& sp, uint8_t level, const Genome& g,
                         PebbleStats& out)
{
  if (level == 0u) level = 1u;
  if (level > (uint8_t)PB_LEVEL_MAX) level = (uint8_t)PB_LEVEL_MAX;

  uint8_t gv[3];
  pebble_genome_vars(g, gv);

  out.hp_max = xp_hp_max(sp.base_hp, level);
  out.atk    = derive_one(sp.base_atk, level, gv[0]);
  out.def    = derive_one(sp.base_def, level, gv[1]);
  out.spd    = derive_one(sp.base_spd, level, gv[2]);
}

bool pebble_stats_of(const PebbleInstance& p, PebbleStats& out)
{
  memset(&out, 0, sizeof out);
  const SpeciesDef* sp = species_get(p.species_id);
  if (sp == nullptr) return false;
  pebble_derive_stats(*sp, p.level, p.genome, out);
  return true;
}

// =============================================================================
//  THE DYNASTY NAME. See pebble.h for what moved out of ui/ui.cpp and what
//  deliberately did not.
// =============================================================================
void pebble_name_syllables(uint32_t lineage_id, uint8_t generation, uint8_t out[2])
{
  if (!out) return;
  // BYTE FOR BYTE ui.cpp's hash. 0x9E3779B9 is the golden-ratio word, 0x85EBCA6B
  // is murmur's, and the last three lines are xorshift-star's finaliser. Nothing
  // here may be "tidied": the answer is a name a player has already seen.
  uint32_t h = lineage_id ^ 0x9E3779B9u;
  h ^= (uint32_t)generation * 0x85EBCA6Bu;
  h ^= h >> 15; h *= 0x2545F491u; h ^= h >> 13;
  out[0] = (uint8_t)(h % (uint32_t)PB_NAME_SYLLABLES);
  out[1] = (uint8_t)((h / (uint32_t)PB_NAME_SYLLABLES) % (uint32_t)PB_NAME_SYLLABLES);
}

// strlen without <string.h>'s size_t, bounded by the u16 everything here uses.
static uint16_t str_len(const char* s)
{
  if (!s) return 0u;
  uint16_t n = 0;
  while (s[n] != '\0') ++n;
  return n;
}

// How many bytes of `s` fit in `room` without splitting a UTF-8 sequence. A
// continuation byte is 10xxxxxx; a cut is legal only where the next byte is NOT
// one. The repertoire is Latin-1 through UTF-8 (core/strings_es.h), so the only
// sequences here are 1 and 2 bytes long - but the rule is written for any
// length, because a rule that assumes two is a rule that breaks on the day
// somebody adds a third.
static uint16_t utf8_fit(const char* s, uint16_t room)
{
  if (!s) return 0u;
  uint16_t n = 0;
  while (n < room && s[n] != '\0') ++n;
  while (n > 0u && ((uint8_t)s[n] & 0xC0u) == 0x80u) --n;   // back off a split
  return n;
}

uint8_t pebble_name_join(const char* a, const char* b, char* out, uint16_t cap)
{
  if (!out || cap == 0u) return 0u;
  out[0] = '\0';
  uint16_t w = 0;
  // cap counts the terminator, so the text budget is cap - 1.
  const uint16_t budget = (uint16_t)(cap - 1u);

  const uint16_t la = str_len(a);
  const uint16_t na = utf8_fit(a, budget);
  for (uint16_t i = 0; i < na; ++i) out[w++] = a[i];
  out[w] = '\0';

  // THE SECOND SYLLABLE IS ONLY REACHED IF THE FIRST FITTED WHOLE. That is what
  // makes a truncated name a PREFIX of the full one: without it, a one-byte
  // budget would drop "Ña" (three bytes, no whole character inside one) and
  // then write the "r" of "rrón", so the player would be shown a name that is
  // not the beginning of their Pebble's name. A prefix is a legible truncation;
  // a different word is a bug that looks like content.
  if (na < la) return (uint8_t)w;

  const uint16_t nb = utf8_fit(b, (uint16_t)(budget - w));
  for (uint16_t i = 0; i < nb; ++i) out[w++] = b[i];
  out[w] = '\0';
  // SATURATING, not truncating. The widest name in the repertoire is 8 bytes so
  // this cannot fire on any caller in the tree, and a silent wrap on the day one
  // does is worse than a wrong-but-bounded count.
  return (uint8_t)((w > 255u) ? 255u : w);
}
