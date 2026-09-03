// =============================================================================
//  Pebblebol host tests - test_pet_view.cpp
//  ui/pet_view.cpp is the seam P2-C11c put between the model and the two
//  modules that animate the body. petfx.cpp and actfx.cpp read a PetView and
//  nothing else now, so the things this file checks are exactly the things
//  that used to be read straight out of sim.h and genome.h:
//
//    * the identity petfx seeds its automaton from,
//    * the CareId-indexed percentages (StatId order is NOT the same list),
//    * and the pose / mood ladders that moved here from webui.cpp.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "core/nt_types.h"
#include "data/species_table.h"
#include "data/sprites.h"
#include "game/box.h"
#include "game/evolution.h"
#include "game/sim.h"
#include "game/xp.h"
#include "persistence/save_schema.h"
#include "ui/pet_view.h"

// The identity is the Pebble's own, not the genome's: two Pebbles of the SAME
// species with the SAME genome must still move differently, and the same one
// must move the same way for ever.
TEST(identity_is_the_pebble_not_the_genome) {
  PebbleInstance a;
  memset(&a, 0, sizeof a);
  a.id            = 0x00010203u;
  a.creation_seed = 0x0A0B0C0Du;
  CHECK_EQ(pet_view_identity(a), 0x0A0A0E0Eu);

  PebbleInstance b = a;
  b.id = 0x00010204u;
  CHECK(pet_view_identity(b) != pet_view_identity(a));

  // Stable: nothing in it is derived from the clock or from RAM state.
  CHECK_EQ(pet_view_identity(a), pet_view_identity(a));
}

// pet_view_attach() is what stamps the Box half onto a view already filled
// from the simulation, and a NULL instance (no active slot) must leave the
// genome fallback standing rather than zeroing the identity.
TEST(attach_is_a_no_op_without_a_slot) {
  PetView v;
  memset(&v, 0, sizeof v);
  v.identity = 0xDEADBEEFu;
  v.level    = 1;
  pet_view_attach(v, nullptr);
  CHECK_EQ(v.identity, 0xDEADBEEFu);
  CHECK_EQ(v.level, (uint8_t)1);

  PebbleInstance p;
  memset(&p, 0, sizeof p);
  p.id            = 7;
  p.creation_seed = 9;
  p.species_id    = 1;
  p.level         = 12;
  p.status        = PBS_CORRUPTED;
  snprintf(p.nickname, sizeof p.nickname, "ROCA");
  pet_view_attach(v, &p);
  CHECK_EQ(v.identity, (uint32_t)(7u ^ 9u));
  CHECK_EQ(v.level, (uint8_t)12);
  CHECK_EQ(v.corrupted, (uint8_t)1);
  CHECK(strcmp(v.name, "ROCA") == 0);
}

// care_pct[] is indexed by CareId. StatId puts ENERGY at 2 and HEALTH at 4;
// CareId puts HEALTH at 2 and ENERGY at 4. Reading one with the other's index
// is the bug this mapping exists to make impossible, so the stored path is
// checked against hand-placed values.
TEST(care_percentages_are_care_id_indexed) {
  PebbleInstance p;
  memset(&p, 0, sizeof p);
  p.species_id = 1;
  p.level      = 5;
  p.hp_cur     = 10;
  p.care[CARE_HUNGER]      = PB_CARE_MILLI_MAX;             // 100 %
  p.care[CARE_HAPPINESS]   = PB_CARE_MILLI_MAX / 2;         //  50 %
  p.care[CARE_HEALTH]      = PB_CARE_MILLI_MAX / 4;         //  25 %
  p.care[CARE_CLEANLINESS] = 0;                             //   0 %
  p.care[CARE_ENERGY]      = (PB_CARE_MILLI_MAX * 3) / 4;   //  75 %

  const SpeciesDef* sp = species_get(1);
  CHECK(sp != nullptr);
  if (!sp) return;

  PetView v;
  pet_view_fill(v, p, *sp, POSE_IDLE);
  CHECK_EQ(v.care_pct[CARE_HUNGER],      (uint8_t)100);
  CHECK_EQ(v.care_pct[CARE_HAPPINESS],   (uint8_t)50);
  CHECK_EQ(v.care_pct[CARE_HEALTH],      (uint8_t)25);
  CHECK_EQ(v.care_pct[CARE_CLEANLINESS], (uint8_t)0);
  CHECK_EQ(v.care_pct[CARE_ENERGY],      (uint8_t)75);
  CHECK_EQ(v.present, (uint8_t)1);
  CHECK_EQ(v.level, (uint8_t)5);

  // Out-of-range care must clamp, not wrap: a corrupt record still draws.
  p.care[CARE_HUNGER] = -1;
  p.care[CARE_ENERGY] = PB_CARE_MILLI_MAX * 4;
  pet_view_fill(v, p, *sp, POSE_IDLE);
  CHECK_EQ(v.care_pct[CARE_HUNGER], (uint8_t)0);
  CHECK_EQ(v.care_pct[CARE_ENERGY], (uint8_t)100);

  // hp_pct is derived from the species base and the level (plan 1.5.1) and is
  // capped: a record claiming more HP than its maximum reads as full.
  p.hp_cur = 60000;
  pet_view_fill(v, p, *sp, POSE_IDLE);
  CHECK_EQ(v.hp_pct, (uint8_t)100);
}

// The stage a stored Pebble is drawn at follows its LEVEL, the same rule
// sim_bind() uses. A Pebble in the Box has no simulation to ask.
TEST(stage_follows_the_level) {
  PebbleInstance p;
  memset(&p, 0, sizeof p);
  p.species_id = 1;
  const SpeciesDef* sp = species_get(1);
  CHECK(sp != nullptr);
  if (!sp) return;

  PetView v;
  static const struct { uint8_t level, stage; } kBands[] = {
    { 1, STAGE_BABY }, { 3, STAGE_BABY }, { 4, STAGE_CHILD }, { 9, STAGE_CHILD },
    { 10, STAGE_TEEN }, { 19, STAGE_TEEN }, { 20, STAGE_ADULT }, { 30, STAGE_ADULT }
  };
  for (size_t i = 0; i < sizeof kBands / sizeof kBands[0]; i++) {
    p.level = kBands[i].level;
    pet_view_fill(v, p, *sp, POSE_IDLE);
    CHECK_EQ(v.stage, kBands[i].stage);
  }
}

// The two ladders that moved out of webui.cpp. Sleeping outranks sick, because
// a sleeping pet is drawn with its eyes shut whatever else is wrong with it.
TEST(pose_and_mood_ladders) {
  CHECK_EQ(pet_pose_of(0), (uint8_t)POSE_IDLE);
  CHECK_EQ(pet_pose_of(PF_SICK), (uint8_t)POSE_SICK);
  CHECK_EQ(pet_pose_of(PF_ASLEEP), (uint8_t)POSE_SLEEP);
  CHECK_EQ(pet_pose_of(PF_ASLEEP | PF_SICK), (uint8_t)POSE_SLEEP);

  CHECK_EQ(pet_mood_index(0),   (uint8_t)MOOD_MISERIA);
  CHECK_EQ(pet_mood_index(15),  (uint8_t)MOOD_MISERIA);
  CHECK_EQ(pet_mood_index(16),  (uint8_t)MOOD_TRISTE);
  CHECK_EQ(pet_mood_index(35),  (uint8_t)MOOD_TRISTE);
  CHECK_EQ(pet_mood_index(55),  (uint8_t)MOOD_NEUTRO);
  CHECK_EQ(pet_mood_index(75),  (uint8_t)MOOD_CONTENTO);
  CHECK_EQ(pet_mood_index(90),  (uint8_t)MOOD_FELIZ);
  CHECK_EQ(pet_mood_index(100), (uint8_t)MOOD_EUFORICO);
  // The ladder is monotonic: no score may read as a happier face than a
  // higher one.
  for (uint8_t s = 1; s <= 100; s++)
    CHECK(pet_mood_index(s) >= pet_mood_index((uint8_t)(s - 1u)));
}

// =============================================================================
//  P3-C3: AN EVOLUTION IS A VISIBLE CHANGE
//
//  This is the only place the claim can be checked. ui/ceremony.cpp is a DEVICE
//  translation unit (it drives the panel's flash and shake registers through
//  render.h), so its frames cannot be rendered on the host - but what the
//  ceremony reveals is a PetView, and a PetView is exactly what this file
//  builds. If evolving a Pebble did not move anything petfx reads, the section
//  18 show would be 4.5 seconds of animation ending on the same body.
// =============================================================================
TEST(an_evolution_changes_the_body_the_view_describes) {
  PebbleInstance p;
  memset(&p, 0, sizeof p);
  p.magic      = (uint16_t)PEBBLE_MAGIC;
  p.layout_ver = (uint8_t)PEBBLE_LAYOUT_VER;
  p.species_id = 1;
  p.id         = 0x11223344u;
  p.level      = 8;                       // the level family 1's first rule asks for
  const SpeciesDef* s1 = species_get(p.species_id);
  CHECK(s1 != nullptr);
  if (!s1) return;
  p.hp_cur = xp_hp_max(s1->base_hp, p.level);

  PetView before;
  pet_view_fill(before, p, *s1, POSE_IDLE);

  EvoContext ctx;
  evo_context_clear(ctx);                 // the shipped rules need no input
  CHECK(evolution_apply(p, ctx));

  const SpeciesDef* s2 = species_get(p.species_id);
  CHECK(s2 != nullptr);
  if (!s2) return;
  PetView after;
  pet_view_fill(after, p, *s2, POSE_IDLE);

  // A different species, drawn with a different form: pet_view.cpp feeds
  // evo_state's stage bits to sprite_form_of(), so the stage bit the evolution
  // set is what changes the sprite the ceremony hands back.
  CHECK(after.species_id != before.species_id);
  CHECK(after.form != before.form);
  CHECK_EQ(after.stage, before.stage);    // the LIFE stage follows the level only

  // And the numbers under it: hp_max moved with the species, and a full Pebble
  // came out of the ceremony full rather than hurt.
  CHECK(xp_hp_max(s2->base_hp, p.level) != xp_hp_max(s1->base_hp, p.level));
  CHECK_EQ(after.hp_pct, (uint8_t)100);
  CHECK_EQ(before.hp_pct, (uint8_t)100);
}
