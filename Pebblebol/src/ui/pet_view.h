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
//  sprites.h into this header. The view carries the atlas COORDINATES instead -
//  stage and form - and petfx resolves the sets it wants.
//
//  WHICH SPECIES A PetView IS OF IS ALREADY DECIDED BY THE TIME petfx SEES ONE
//  (P4-C4a). `form` is the resolved design: pet_view.cpp folds
//  ui/pet_art.h's pet_art_key() - the species row's sprite_id, or the genome
//  nibble when there is no row - into the pool the life stage draws from.
//  species_id and gene_species stay on the view because a screen may want to
//  say WHICH creature it is; petfx never needs to ask, and must not include
//  data/species_table.h to find out.
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
  uint8_t  gene_species;      // 0..15, the genome nibble; the art-key FALLBACK
  uint8_t  stage;             // Stage
  uint8_t  form;              // sprite_form_of(art key, stage):
                              // the resolved design, species-chosen at BABY /
                              // ADULT / SENIOR, care-chosen at CHILD / TEEN
  uint8_t  pose;              // SpritePose the caller wants drawn

  // ---- numbers a screen may show ------------------------------------------
  uint8_t  level;             // 1..30
  // NO hp_pct. It was written by pet_view_fill() alone and read by nothing at
  // all, so when P4-C4a deleted that function the field became a number no
  // code produced and no code consumed. HOME's HP meter comes from
  // PebbleView.hp_cur / hp_max (ui/screen_view.h) through xp_hp_max().
  //
  // NO mood, asleep OR sick EITHER (P4-C6). P4-C4a's sweep deleted hp_pct for
  // exactly one reason - one writer, no reader - and stopped at the first
  // instance. These three were the next three: pet_view_fill_sim() assigned
  // each of them once and nothing in Pebblebol/src or tests/ ever read one.
  // `mood` was the head of a four-link chain that was dead all the way down
  // (PebbleView.mood_face -> sprite_mood_face() -> spr_mood12, all removed in
  // the same commit). `asleep` and `sick` were WORSE than unused: they were a
  // second copy of PF_ASLEEP and PF_SICK, which every real consumer already
  // reads out of `flags` - petfx.cpp:873 and actfx.cpp:595 do exactly that.
  // Two places to be wrong about one fact is the defect; one is the fix.
  uint8_t  mood_pct;          // 0..100 care-quality score; petfx scales motion by it
  uint8_t  care_pct[PB_CARE_COUNT];   // INDEXED BY CareId (save_schema.h), not StatId
  // PBS_CORRUPTED, the section 55 status. IT WAS THE FOURTH INSTANCE OF THE
  // SHAPE THE PARAGRAPH ABOVE DELETED THREE FIELDS FOR - written by
  // pet_view_fill_sim(), asserted once in tests/test_pet_view.cpp and read by
  // nothing in Pebblebol/src - and it survived only because P9-C5 was
  // scheduled. P9-C5 consumed it: ui/petfx.cpp reads it three times, for the
  // behaviour row (pf_derive), for the mid-life re-derive (petfx_service) and
  // for the glitch gate (petfx_draw_body). If a future chunk removes the last
  // of those, this field goes with it rather than joining the list above.
  uint8_t  corrupted;
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
  char     name[PB_NAME_DRAW_CAP];   // UTF-8; the nickname is Latin-1 (core/utf8.h)
};

// -----------------------------------------------------------------------------
//  FILLING ONE
//
//  THERE IS NO pet_view_fill(PetView&, const PebbleInstance&, const SpeciesDef&,
//  uint8_t) ANY MORE. P4-C4a deleted it, and the reason is the one the plan
//  gives for not leaving a tested function the device does not call:
//
//    * it had no caller in Pebblebol/src for three phases - only its own
//      declaration, its definition and the tests that drove it;
//    * its stage ladder was NOT "the same rule sim_bind() uses" its comment
//      claimed. It read >= 20 ADULT, >= 10 TEEN, >= 4 CHILD against
//      sim.cpp stage_of_level's >= 20 SENIOR, >= 15 ADULT, >= 10 TEEN,
//      >= 5 CHILD, and it could never return STAGE_SENIOR at all. A level-20
//      Pebble was a 32x32 senior to the simulation and a 40x40 adult to this
//      function;
//    * and the evolution case built on it "proved" a changed body by feeding
//      `evo_state & 3` in as minor_form, so what moved was SPR_CHILD_GOOD ->
//      SPR_CHILD_POOR: the pet was drawn as badly cared for, not as a new
//      species.
//
//  A stored Pebble needs an art key and a stage, both one expression wide
//  (ui/pet_art.h). Whatever draws a Box card in P5 should reach for those and
//  for the sim's OWN stage rule, not for a second ladder that disagrees with it.
// -----------------------------------------------------------------------------
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
