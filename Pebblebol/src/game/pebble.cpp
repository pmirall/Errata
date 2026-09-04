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
