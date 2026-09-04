// =============================================================================
//  PEBBLEBOL - ui/pet_view.cpp
//  The ONE place that reads the model and decides what the body looks like.
//
//  This file is the seam described in pet_view.h: it includes game/sim.h,
//  game/genome.h, data/sprites.h and (through ui/pet_art.h) the species roster
//  so that petfx.cpp and actfx.cpp no longer have to. Everything above it in
//  the render stack sees a flat PetView and nothing else.
//
//  PURE translation unit: no Arduino.h, no render.h, no NVS, no radio.
//
//  Identifiers and comments: English.
// =============================================================================
#include "pet_view.h"

#include <stdio.h>
#include <string.h>

#include "../data/sprites.h"
#include "../game/genome.h"
#include "../game/sim.h"
#include "pet_art.h"              // pet_art_key(): species row -> atlas key

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
  // The genome nibble is the art key's FALLBACK, and this is the only place it
  // is used as the key itself: pet_view_attach() overwrites `form` a moment
  // later for any Pebble the Box has a species row for. A view that never gets
  // an attach - an egg nothing has filed - keeps exactly the body it always had.
  out.form         = sprite_form_of(out.gene_species, minor_form, (Stage)stage);
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

// -----------------------------------------------------------------------------
//  THE SPECIES REACHES THE BODY (P4-C4a).
//
//  ORDER MATTERS AND IT IS WHY THIS IS NOT IN fill_genome(). ui.cpp fills a
//  view in two calls - pet_view_fill_sim() then pet_view_attach() - and
//  species_id only arrives in the second one. fill_genome() therefore derives
//  `form` from the genome, and this re-derives it from the species row the
//  instant there is one.
//
//  ONLY THE STAGES WHOSE DESIGN IS THE ART KEY ARE TOUCHED. CHILD and TEEN
//  forms are the care-quality variant sim.cpp froze into minor_form when the
//  pet grew; the view does not carry minor_form, and it does not need to,
//  because an evolution does not move those two designs at all.
// -----------------------------------------------------------------------------
static void apply_species_design(PetView& out) {
  const Stage st = (Stage)out.stage;
  if (st == STAGE_EGG || st == STAGE_CHILD || st == STAGE_TEEN) return;
  out.form = pet_art_design(out.species_id, out.gene_species, st);
}

void pet_view_attach(PetView& out, const PebbleInstance* inst) {
  if (!inst) return;
  out.identity   = pet_view_identity(*inst);
  out.species_id = inst->species_id;
  apply_species_design(out);
  out.level      = inst->level ? inst->level : (uint8_t)1;
  out.corrupted  = (uint8_t)((inst->status & PBS_CORRUPTED) != 0u);
  if (inst->nickname[0] != '\0')
    snprintf(out.name, sizeof(out.name), "%s", inst->nickname);
}
