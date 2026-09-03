// =============================================================================
//  PEBBLEBOL - ui/pet_view.h
//  THE BODY'S VIEW (plan section 1.4, audit risk 9).
//
//  petfx and actfx used to include game/sim.h and game/genome.h and read the
//  model directly. That is the layering inversion the audit calls risk 9: the
//  presentation layer knew the simulation's field names, so every change to
//  the model rippled into the animation code and neither could be tested
//  without the other.
//
//  A PetView is what those two modules are allowed to know. It is FLAT, it is
//  ALREADY DERIVED and it is not authority: pet_view.cpp is the ONE place that
//  reads a PebbleInstance or a SimView and decides what the body looks like -
//  which sprite family, which pose, which mood face, how the coat is dithered,
//  how fast the animal walks. Writing to a PetView changes nothing.
//
//  Nothing here includes sim.h, genome.h or sprites.h, which is what lets
//  petfx.cpp be free of all three (plan P2-C11, "petfx.cpp stops including
//  sim.h / genome.h").
//
//  DEVIATION from the 1.4 sketch, deliberately: the sketch carries a
//  `const SpriteSet* body`. petfx needs FOUR sets at once (it clamps the
//  walking body against the widest of IDLE / SLEEP / SICK / EAT, see
//  pf_width_of), so one resolved pointer cannot serve it and would drag
//  sprites.h into this header. The view carries the three atlas COORDINATES
//  instead - gene_species, stage, form - and petfx resolves the sets it wants.
//
//  Identifiers and comments: English. No user-facing text lives here.
// =============================================================================
#ifndef PB_PET_VIEW_H
#define PB_PET_VIEW_H

#include <stdint.h>

#include "../core/config.h"
#include "../core/nt_types.h"
#include "../persistence/save_schema.h"   // PB_CARE_COUNT, PB_NICKNAME_CAP

// The model types pet_view.cpp translates FROM. Declared, never defined here:
// a consumer of this header links against pet_view.cpp and never needs their
// layout, which is the whole point of the seam.
struct PebbleInstance;
struct SpeciesDef;
struct SimView;

struct PetView {
  // ---- identity ------------------------------------------------------------
  // pebble_identity(): the Pebble's `id ^ creation_seed`. petfx seeds its
  // behaviour automaton from this, so the SAME Pebble always moves the same
  // way and two siblings move differently. Falls back to a genome hash when
  // the caller has no Box instance (an egg that has never been filed).
  uint32_t identity;

  uint32_t age_s;
  uint16_t flags;             // PF_* (nt_types.h)

  // ---- the sprite atlas coordinates ---------------------------------------
  uint8_t  species_id;        // the Box's species row, 0 = unknown
  uint8_t  gene_species;      // 0..15, the genome nibble the atlas is keyed on
  uint8_t  stage;             // Stage
  uint8_t  form;              // sprite_form_of(genome, minor_form, stage)
  uint8_t  pose;              // SpritePose the caller wants drawn
  uint8_t  mood;              // Mood, the 12x12 badge index

  // ---- numbers a screen may show ------------------------------------------
  uint8_t  level;             // 1..30
  uint8_t  hp_pct;            // 0..100
  uint8_t  mood_pct;          // 0..100 care-quality score; petfx scales motion by it
  uint8_t  care_pct[PB_CARE_COUNT];   // INDEXED BY CareId (save_schema.h), not StatId
  uint8_t  asleep;
  uint8_t  sick;
  uint8_t  corrupted;         // PBS_CORRUPTED, the Phase 9 status
  uint8_t  poop_count;        // 0..POOP_MAX; actfx dissolves these on ACT_CLEAN

  // ---- the cosmetic genes, ALREADY DECODED --------------------------------
  // petfx maps these to motion and to the coat; the mapping is its policy, the
  // decoding is the genome's, and this is the line between them.
  uint8_t  temper;            // Temperament class, < TEMPER_COUNT
  uint8_t  body_size;         // 0..7
  uint8_t  sociability;       // 0..15
  uint8_t  luck;              // 0..7
  uint8_t  rare;              // 0/1
  uint8_t  mutations;         // 0..15
  uint8_t  pattern;           // 0..15
  uint8_t  lineage_bits;      // the lineage id's low 2 bits: which side it hugs

  uint8_t  present;           // 0 = there is no Pebble at all
  char     name[PB_NICKNAME_CAP];
};

// -----------------------------------------------------------------------------
//  FILLING ONE
// -----------------------------------------------------------------------------
// The plan 1.4 signature: the stored Pebble plus its species row. Fills the
// identity from pebble_identity() and the name from the nickname, and is what
// the BOX screen and Phase 4 will use for a Pebble that is NOT the active one.
void pet_view_fill(PetView& out, const PebbleInstance& inst,
                   const SpeciesDef& sp, uint8_t pose);

// The live path. ui.cpp holds a SimView, not a PebbleInstance: the simulation
// owns the RAM-only half of the body (minor_form, poop, the alert flags) and
// the Box owns the stored half. This fills everything the SimView knows and
// leaves identity/level/name to pet_view_attach().
void pet_view_fill_sim(PetView& out, const SimView& p, uint8_t pose);

// Stamps the Box half onto a view already filled from the simulation. `inst`
// may be NULL (no active slot), in which case the identity stays the genome
// hash and the level stays 1.
void pet_view_attach(PetView& out, const PebbleInstance* inst);

// The identity petfx seeds from: id ^ creation_seed (plan 1.4 pebble_identity).
uint32_t pet_view_identity(const PebbleInstance& inst);

// -----------------------------------------------------------------------------
//  POSE AND MOOD POLICY
//  Moved here from webui.cpp (web_pose_of / web_mood_index, plan P2-C11): the
//  rule "what does this pet look like right now" is a RENDER decision and the
//  web mirror was never the right owner of it.
// -----------------------------------------------------------------------------
uint8_t pet_pose_of(uint16_t flags);          // SpritePose
uint8_t pet_mood_index(uint8_t score_0_100);  // Mood

#endif  // PB_PET_VIEW_H
