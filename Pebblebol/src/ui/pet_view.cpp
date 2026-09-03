// =============================================================================
//  PEBBLEBOL - ui/pet_view.cpp
//  The ONE place that reads the model and decides what the body looks like.
//
//  This file is the seam described in pet_view.h: it includes game/sim.h,
//  game/genome.h, data/sprites.h and data/species_table.h so that petfx.cpp
//  and actfx.cpp no longer have to. Everything above it in the render stack
//  sees a flat PetView and nothing else.
//
//  PURE translation unit: no Arduino.h, no render.h, no NVS, no radio.
//
//  Identifiers and comments: English.
// =============================================================================
#include "pet_view.h"

#include <stdio.h>
#include <string.h>

#include "../data/species_table.h"
#include "../game/xp.h"           // xp_hp_max(): the ONE hp_max formula
#include "../data/sprites.h"
#include "../game/genome.h"
#include "../game/sim.h"

// -----------------------------------------------------------------------------
//  POSE AND MOOD POLICY (ex webui.cpp web_pose_of / web_mood_index)
//
//  Two panels used to answer "what face is this pet wearing": the OLED through
//  ui.cpp and the phone mirror through webui.cpp. The mirror owned the table,
//  which is exactly backwards - the rule is a render decision - so it lives
//  here now and there is only one ladder left to disagree with.
// -----------------------------------------------------------------------------
uint8_t pet_pose_of(uint16_t flags) {
  if (flags & PF_ASLEEP) return POSE_SLEEP;
  if (flags & PF_SICK)   return POSE_SICK;
  return POSE_IDLE;
}

uint8_t pet_mood_index(uint8_t score) {
  if (score <= 15) return MOOD_MISERIA;
  if (score <= 35) return MOOD_TRISTE;
  if (score <= 55) return MOOD_NEUTRO;
  if (score <= 75) return MOOD_CONTENTO;
  if (score <= 90) return MOOD_FELIZ;
  return MOOD_EUFORICO;
}

// -----------------------------------------------------------------------------
//  IDENTITY
//
//  plan 1.4: pebble_identity() == id ^ creation_seed. game/pebble.h does not
//  exist (P2-C9 filed its contents into game/box.cpp and
//  persistence/save_schema.h instead), so the rule lives here, where the only
//  two callers - petfx's automaton seed and the BOX screen - can both see it.
//
//  The genome fallback below is what petfx used to compute for itself
//  (pf_identity): it is what an egg that has never been filed into the Box
//  still has, and dropping it would make every unfiled pet move identically.
// -----------------------------------------------------------------------------
uint32_t pet_view_identity(const PebbleInstance& inst) {
  return inst.id ^ inst.creation_seed;
}

static uint32_t identity_of_genome(const Genome& g) {
  return g.lineage_id
       ^ ((uint32_t)g.g0 << 16)
       ^ (uint32_t)g.g1
       ^ ((uint32_t)g.g2 << 7)
       ^ ((uint32_t)g.generation << 24);
}

// -----------------------------------------------------------------------------
//  THE COSMETIC GENES, DECODED ONCE
// -----------------------------------------------------------------------------
static void fill_genome(PetView& out, const Genome& g, uint8_t minor_form,
                        uint8_t stage) {
  out.gene_species = gene_species(g);
  out.form         = sprite_form_of(g, minor_form, (Stage)stage);
  out.temper       = gene_temper_class(g);
  if (out.temper >= (uint8_t)TEMPER_COUNT) out.temper = (uint8_t)TEMPER_TRANQUILO;
  out.body_size    = gene_body_size(g);
  out.sociability  = gene_sociability(g);
  out.luck         = gene_luck(g);
  out.rare         = gene_rare(g);
  out.mutations    = gene_mutations(g);
  out.pattern      = (uint8_t)(gene_pattern(g) & 0x0Fu);
  out.lineage_bits = (uint8_t)(g.lineage_id & 3u);
}

// -----------------------------------------------------------------------------
//  THE LIVE PATH
// -----------------------------------------------------------------------------
void pet_view_fill_sim(PetView& out, const SimView& p, uint8_t pose) {
  memset(&out, 0, sizeof(out));
  out.present  = 1;
  out.age_s    = p.age_s;
  out.flags    = p.flags;
  out.stage    = p.stage;
  out.pose     = pose;
  out.asleep   = (uint8_t)((p.flags & PF_ASLEEP) != 0u);
  out.sick     = (uint8_t)((p.flags & PF_SICK)   != 0u);
  out.poop_count = p.poop_count;
  out.mood_pct = (uint8_t)sim_mood_score();
  out.mood     = pet_mood_index(out.mood_pct);
  // StatId order and CareId order are NOT the same list (ST_ENERGY is 2,
  // CARE_ENERGY is 4). The view is indexed by CareId, so the mapping happens
  // exactly once, here, instead of being re-guessed at every read site.
  static const uint8_t kStatOfCare[PB_CARE_COUNT] = {
    (uint8_t)ST_HUNGER, (uint8_t)ST_HAPPINESS, (uint8_t)ST_HEALTH,
    (uint8_t)ST_HYGIENE, (uint8_t)ST_ENERGY
  };
  for (uint8_t i = 0; i < PB_CARE_COUNT; ++i)
    out.care_pct[i] = sim_stat_pct((StatId)kStatOfCare[i]);
  out.level    = 1;
  out.identity = identity_of_genome(p.genome);
  fill_genome(out, p.genome, p.minor_form, p.stage);
}

void pet_view_attach(PetView& out, const PebbleInstance* inst) {
  if (!inst) return;
  out.identity   = pet_view_identity(*inst);
  out.species_id = inst->species_id;
  out.level      = inst->level ? inst->level : (uint8_t)1;
  out.corrupted  = (uint8_t)((inst->status & PBS_CORRUPTED) != 0u);
  if (inst->nickname[0] != '\0')
    snprintf(out.name, sizeof(out.name), "%s", inst->nickname);
}

// -----------------------------------------------------------------------------
//  THE STORED PATH (plan 1.4 signature)
//
//  A Pebble that is NOT the active one has no simulation behind it: its truth
//  is entirely in the record, so everything here is read straight off it. The
//  name is the nickname or nothing - the deterministic dynasty name is built
//  from strings_es.h syllables and belongs to the screen layer, which is why
//  ui_pet_name() keeps it.
// -----------------------------------------------------------------------------
void pet_view_fill(PetView& out, const PebbleInstance& inst,
                   const SpeciesDef& sp, uint8_t pose) {
  memset(&out, 0, sizeof(out));
  out.present    = (uint8_t)((inst.species_id != 0u) ? 1u : 0u);
  out.identity   = pet_view_identity(inst);
  out.age_s      = inst.age_s;
  out.species_id = inst.species_id;
  out.level      = inst.level ? inst.level : (uint8_t)1;
  out.pose       = pose;
  out.asleep     = (uint8_t)((inst.status & PBS_ASLEEP)    != 0u);
  out.sick       = (uint8_t)((inst.status & PBS_SICK)      != 0u);
  out.corrupted  = (uint8_t)((inst.status & PBS_CORRUPTED) != 0u);

  // The PF_* word petfx reads is the RAM view's; a stored Pebble only carries
  // the two bits that survive a power cycle, so rebuild exactly those.
  out.flags = (uint16_t)((out.asleep ? PF_ASLEEP : 0u) | (out.sick ? PF_SICK : 0u));

  // Stage is derived from the level, the same rule sim_bind() uses.
  out.stage = (uint8_t)((inst.level >= 20u) ? STAGE_ADULT
                      : (inst.level >= 10u) ? STAGE_TEEN
                      : (inst.level >=  4u) ? STAGE_CHILD
                                            : STAGE_BABY);

  // Derived and never stored (plan 1.5.1). Through xp_hp_max() rather than
  // open-coded: P3-C3 made that function the ONE owner of the formula so a
  // level-up and an evolution rescale identically, and a third copy here would
  // be the one that silently disagrees.
  const uint16_t hp_max = xp_hp_max(sp.base_hp, out.level);
  // CLAMP BEFORE THE CAST. A record claiming far more HP than its maximum -
  // hp_cur 60000 against an hp_max of 23 - divides to 260,869 %, and narrowing
  // THAT to a uint8 first wraps it to 5 %: a "> 100" test after the cast can
  // never see it. P3-C3 found this the moment the starter's base_hp moved.
  uint32_t hp_pct = (hp_max == 0u) ? 0u
                                   : ((uint32_t)inst.hp_cur * 100u) / (uint32_t)hp_max;
  if (hp_pct > 100u) hp_pct = 100u;
  out.hp_pct = (uint8_t)hp_pct;

  for (uint8_t i = 0; i < PB_CARE_COUNT; ++i) {
    int32_t v = inst.care[i];
    if (v < 0) v = 0;
    if (v > PB_CARE_MILLI_MAX) v = PB_CARE_MILLI_MAX;
    out.care_pct[i] = (uint8_t)(v / (PB_CARE_MILLI_MAX / 100L));
  }
  // The care average is the honest mood for a Pebble nothing is simulating.
  {
    uint16_t sum = 0;
    for (uint8_t i = 0; i < PB_CARE_COUNT; ++i) sum = (uint16_t)(sum + out.care_pct[i]);
    out.mood_pct = (uint8_t)(sum / PB_CARE_COUNT);
    out.mood = pet_mood_index(out.mood_pct);
  }

  fill_genome(out, inst.genome, (uint8_t)(inst.evo_state & 0x03u), out.stage);
  if (inst.nickname[0] != '\0')
    snprintf(out.name, sizeof(out.name), "%s", inst.nickname);
}
