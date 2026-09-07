// =============================================================================
//  PEBBLEBOL - ui/pet_art.h
//  THE ONE EXPRESSION THAT TURNS A PEBBLE'S IDENTITY INTO ITS BODY AND ITS NAME
//  (plan P4-C4a, the obligation carried since P3-C3).
//
//  Until this file existed the atlas was keyed on gene_species(genome) - the
//  four-bit species NIBBLE the v1 save format carried - so all 36 species wore
//  one of eight genome bodies, evolving changed the FORM and never the
//  CREATURE, and SpeciesDef.sprite_id was asserted by two guards and drawn by
//  nothing. The key comes from the SPECIES ROW now.
//
//  WHY IT IS ITS OWN HEADER AND NOT PART OF ui/pet_view.h.
//  petfx.cpp and actfx.cpp include pet_view.h, and tools/check.sh forbids them
//  reaching past a PetView into the model. That gate greps for sim.h / genome.h
//  BY FILENAME, so putting data/species_table.h into pet_view.h would slip the
//  whole content layer (and save_schema.h and strings_es.h behind it) into both
//  animators without tripping a thing - reopening exactly the layering
//  inversion pet_view.h exists to close. This header is included by the files
//  that already know what a species is (ui/pet_view.cpp, ui/screen_home.cpp,
//  ui/screen_box.cpp, ui/ui.cpp) and by neither animator.
//
//  Pure header: stdint, the generated species roster, the atlas and the
//  Spanish strings. No Arduino, no render.h, no state.
//
//  Identifiers and comments: English. The only user-facing text is the Spanish
//  species name, which lives in core/strings_es.h where it belongs.
// =============================================================================
#ifndef PB_PET_ART_H
#define PB_PET_ART_H

#include <stdint.h>

#include "../core/strings_es.h"
#include "../data/species_table.h"
#include "../data/sprites.h"

// -----------------------------------------------------------------------------
//  THE ART KEY
//
//  `species_id` is the Box row's; `gene_species` is gene_species(genome), 0..15.
//
//  THE FALLBACK IS THE OLD BEHAVIOUR, BIT FOR BIT, AND THAT IS THE POINT.
//  species_get() answers nullptr for THREE real inputs and this is what each
//  one draws:
//    * 0        - an empty slot, or an egg that has never been filed into the
//                 Box (pet_view_attach() is a no-op without an instance, so the
//                 view keeps the genome key it was filled with).
//    * 200..209 - the creator's cs0..cs9. There is no table row for a custom
//                 species and data/creator_schema.h says so out loud: sprite
//                 dimensions and sprite data are two of the eight inputs spec
//                 section 35 has no numbers for. P8 gives them art; until then
//                 a custom Pebble wears its genome's body rather than a
//                 gravestone.
//    * anything past the roster - a save written by a build with more families
//                 than this one carries.
//  All three fall on gene_species, which is 0..15. P9-C3 REMOVED THE FOLD: the
//  atlas has one 24x24 body per species now and sprite_set_id() clamps rather
//  than folds, so a row-less Pebble draws the body of species (nibble + 1) -
//  one of the first sixteen - instead of one of eight genome bodies. It is
//  still deterministic, still stable for a given genome, and still needs no
//  "unknown species" drawing; what it is not any more is byte-identical to the
//  pre-P4-C4a behaviour, and the roster it lands in is 60 bodies wide.
// -----------------------------------------------------------------------------
inline uint8_t pet_art_key(uint8_t species_id, uint8_t gene_species) {
  const SpeciesDef* sp = species_get(species_id);
  return sp ? sp->sprite_id : gene_species;
}

// The art key as the atlas wants it. P9-C3 COLLAPSED THIS: it used to call
// sprite_design_of(), which folded the key into the pool its life stage drew
// from (`% 8` at BABY, `% 6` at ADULT/SENIOR), and it was the second of two
// stage ladders in the tree - screen_home.cpp went through sprite_form_of()
// instead because the still body also has to draw EGG, CHILD and TEEN. There is
// one ladder now and both call sites are on it: sprite_form_of() answers for
// every stage, so this is a spelling of it that keeps the call sites reading
// the same as they did.
inline uint8_t pet_art_design(uint8_t species_id, uint8_t gene_species,
                              Stage stage) {
  return sprite_form_of(pet_art_key(species_id, gene_species), stage);
}

// -----------------------------------------------------------------------------
//  THE NAME
//
//  The roster carries a Spanish name per species (STR_SPC_NAME_1..N; 60 at
//  P9-C3). This answers
//  nullptr - never a placeholder - for the same three inputs as above, so a
//  caller has to decide what to show instead rather than being handed an empty
//  string it will happily draw. ui.cpp falls back to the dynasty syllables;
//  ui/screen_box.cpp falls back to the genome's species word.
// -----------------------------------------------------------------------------
inline const char* pet_species_name(uint8_t species_id) {
  const SpeciesDef* sp = species_get(species_id);
  if (sp == nullptr) return nullptr;
  const char* nm = S(sp->name_idx);
  // *** AND AN EMPTY NAME IS NOT A NAME. Final review. ***
  // The paragraph above says this answers "nullptr - never a placeholder - for
  // the same three inputs", and one of those three inputs is "a creator custom
  // (200..209)". It did not: game/species_custom.cpp's csp_install() sets
  // name_idx = STR_EMPTY for every custom species ("the name lives on the
  // PebbleInstance's nickname, not in a StrId"), species_get(200) therefore
  // returns a real row, and S(STR_EMPTY) is the EMPTY STRING - which is not
  // nullptr. So the test was a null test where the ladder needed an empty test,
  // rung 2 was taken with a zero-length string, and the dynasty fallback at
  // rung 3 became unreachable for exactly the creature the phone block mints.
  //
  // Measured against the shipping objects: an unnamed device whose active
  // Pebble is a custom species produced "" from ui.cpp's pet_name_stored(), and
  // disc_encode() ACCEPTS that (name_ok() is true for an all-zero field) - so
  // the board went on the air with twelve zero bytes for a name. The far board's
  // LINK row then fell back to S(STR_LK_SEARCH) ("Buscando Pebbles...") and its
  // card header to S(STR_LINK_TITLE) ("ENLACE"), so the device was visible but
  // anonymous and two of them were indistinguishable. HOME's identity line was
  // blank on the near board.
  //
  // Fixed HERE rather than at ui.cpp's call site, because the contract is
  // stated here and ui/screen_box.cpp reads the same answer.
  return (nm != nullptr && nm[0] != '\0') ? nm : nullptr;
}

#endif  // PB_PET_ART_H
