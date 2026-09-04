// =============================================================================
//  Pebblebol host tests - test_pet_view.cpp
//  ui/pet_view.cpp is the seam P2-C11c put between the model and the two
//  modules that animate the body. petfx.cpp and actfx.cpp read a PetView and
//  nothing else now, so the things this file checks are exactly the things
//  that used to be read straight out of sim.h and genome.h:
//
//    * the identity petfx seeds its automaton from,
//    * the CareId-indexed percentages (StatId order is NOT the same list),
//    * the pose / mood ladders that moved here from webui.cpp,
//    * and, since P4-C4a, WHICH CREATURE THE VIEW DESCRIBES.
//
//  THE P4-C4a CASES ARE THE POINT OF THIS FILE NOW. For three phases the atlas
//  was keyed on gene_species(genome) - the four-bit nibble the v1 save carried -
//  so all 36 species wore one of eight genome bodies and an evolution moved the
//  FORM and never the CREATURE. PetView.species_id was written and read by
//  nothing. Every case below that names a species drives the LIVE path,
//  pet_view_fill_sim() + pet_view_attach(), which is the pair ui.cpp:201-203
//  actually calls - not a function the firmware never runs.
//
//  WHAT WENT AWAY WITH THAT CHANGE. pet_view_fill(PetView&, const
//  PebbleInstance&, const SpeciesDef&, uint8_t) is deleted, and the three cases
//  that drove it are replaced rather than removed:
//    * its care-percentage case tested a STRAIGHT COPY of inst.care[] and was
//      cited as proof of the CareId/StatId mapping, which lives in the OTHER
//      fill. It drives the live one now, through five distinct god-set stats.
//    * its stage case pinned a ladder that disagreed with sim.cpp's on three of
//      four thresholds and could never return STAGE_SENIOR. The live view takes
//      its stage from the simulation, which is the only ladder left.
//    * its evolution case checked `after.form != before.form` where `form` came
//      from `evo_state & 3` fed in as minor_form - so the "changed body" it
//      proved was SPR_CHILD_GOOD -> SPR_CHILD_POOR, the pet drawn as badly
//      cared for. every_evolution_reaches_the_body below checks the resolved
//      SET ID at the stages a species actually chooses.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "core/nt_types.h"
#include "data/evolution_table.h"
#include "data/species_table.h"
#include "data/sprites.h"
#include "game/box.h"
#include "game/evolution.h"
#include "game/genome.h"
#include "game/sim.h"
#include "game/xp.h"
#include "persistence/save_schema.h"
#include "ui/pet_art.h"
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
// genome fallback standing rather than zeroing the identity - OR the body.
TEST(attach_is_a_no_op_without_a_slot) {
  PetView v;
  memset(&v, 0, sizeof v);
  v.identity     = 0xDEADBEEFu;
  v.level        = 1;
  v.stage        = STAGE_ADULT;
  v.gene_species = 4;
  v.form         = sprite_form_of(4u, 0u, STAGE_ADULT);
  const uint8_t form_before = v.form;
  pet_view_attach(v, nullptr);
  CHECK_EQ(v.identity, 0xDEADBEEFu);
  CHECK_EQ(v.level, (uint8_t)1);
  // The early return IS the no-Box fallback: species_id stays 0 and the body
  // stays the one the genome chose.
  CHECK_EQ(v.species_id, (uint8_t)0);
  CHECK_EQ(v.form, form_before);

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

// =============================================================================
//  THE LIVE FILL
// =============================================================================

// A bound simulation, so sim_stat_pct() answers something a test chose.
static PebbleInstance g_live;

static void live_pet(void) {
  genome_seed(0x5EED0C7Au);
  sim_seed(0x5EED0C7Au);
  memset(&g_live, 0, sizeof g_live);
  sim_bind(g_live);
  sim_new_pet(genome_genesis(), 1700000000u, 0);
  sim_hatch();
}

// care_pct[] is indexed by CareId. StatId puts ENERGY at 2 and HEALTH at 4;
// CareId puts HEALTH at 2 and ENERGY at 4. Reading one with the other's index
// is the bug this mapping exists to make impossible - and the mapping lives in
// pet_view_fill_sim(), which is the fill the firmware runs, so that is the one
// driven here.
//
// THE FIVE VALUES ARE PAIRWISE DISTINCT ON PURPOSE and the test says so out
// loud: with the five stats equal, ANY permutation of kStatOfCare[] passes.
TEST(care_percentages_are_care_id_indexed) {
  live_pet();
  sim_god_set_stat(ST_HUNGER,    91);
  sim_god_set_stat(ST_HAPPINESS, 72);
  sim_god_set_stat(ST_HEALTH,    53);
  sim_god_set_stat(ST_HYGIENE,   34);
  sim_god_set_stat(ST_ENERGY,    15);

  const SimView* sv = sim_view();
  CHECK(sv != nullptr);
  if (!sv) return;

  PetView v;
  pet_view_fill_sim(v, *sv, POSE_IDLE);
  CHECK_EQ(v.care_pct[CARE_HUNGER],      (uint8_t)91);
  CHECK_EQ(v.care_pct[CARE_HAPPINESS],   (uint8_t)72);
  CHECK_EQ(v.care_pct[CARE_HEALTH],      (uint8_t)53);
  CHECK_EQ(v.care_pct[CARE_CLEANLINESS], (uint8_t)34);
  CHECK_EQ(v.care_pct[CARE_ENERGY],      (uint8_t)15);
  // No two of them are the same number, which is what makes the five checks
  // above able to fail one at a time.
  for (uint8_t i = 0; i < PB_CARE_COUNT; ++i)
    for (uint8_t j = (uint8_t)(i + 1u); j < PB_CARE_COUNT; ++j)
      CHECK(v.care_pct[i] != v.care_pct[j]);

  CHECK_EQ(v.present, (uint8_t)1);
  // The live view takes its stage from the simulation and derives none of its
  // own: there is exactly one stage ladder in the tree now (sim.cpp).
  CHECK_EQ(v.stage, sv->stage);
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
//  P4-C4a: THE SPECIES REACHES THE BODY
//
//  These are the cases the carried obligation is discharged by, and every one
//  of them was made to fail before it was written down. The mutation each one
//  catches is named above it.
// =============================================================================

// One SimView, built by hand, so a case can say exactly which genome and which
// life stage it means. The live fill needs nothing else from the simulation
// except the care percentages, which these cases do not look at.
static void sim_view_of(SimView& sv, uint16_t g0, uint8_t stage,
                        uint8_t minor_form) {
  memset(&sv, 0, sizeof sv);
  sv.genome.magic_ver  = GENOME_MAGIC_VER;
  sv.genome.lineage_id = 0x0BADF00Du;
  sv.genome.g0         = g0;
  sv.genome.g1         = 0x5678u;
  sv.genome.g2         = 0x9ABCu;
  sv.genome.generation = 3;
  sv.stage      = stage;
  sv.minor_form = minor_form;
}

// The whole live path in one call: fill from a SimView, then stamp a Box row
// on it, exactly as ui.cpp's body_view() does.
static void live_view(PetView& out, uint16_t g0, uint8_t stage,
                      uint8_t species_id) {
  SimView sv;
  sim_view_of(sv, g0, stage, 0u);
  PebbleInstance inst;
  memset(&inst, 0, sizeof inst);
  inst.id            = 0x11223344u;
  inst.creation_seed = 0x55667788u;
  inst.species_id    = species_id;
  inst.level         = 10;
  pet_view_fill_sim(out, sv, POSE_IDLE);
  pet_view_attach(out, &inst);
}

// The set id petfx would draw for a view, through the same two functions
// petfx_draw_body() uses.
static uint8_t drawn_set(const PetView& v, uint8_t pose) {
  return sprite_set_id(v.stage, v.form, pose);
}

// -----------------------------------------------------------------------------
//  MUTATION THIS CATCHES: make the resolution ignore species_id - restore
//  `out.form = sprite_form_of(out.gene_species, ...)` in pet_view_attach(), or
//  delete the apply_species_design() call, or make pet_art_key() return
//  gene_species unconditionally. All three make the two views below identical.
// -----------------------------------------------------------------------------
TEST(the_species_and_not_the_genome_chooses_the_body) {
  // ONE genome. Two species. The genome nibble is 4 either way, so anything
  // that still reads it draws the same creature twice.
  const uint16_t kG0 = 0x1234u;

  static const uint8_t kStages[] = { STAGE_BABY, STAGE_ADULT, STAGE_SENIOR };
  for (uint8_t s = 0; s < (uint8_t)(sizeof kStages / sizeof kStages[0]); ++s) {
    PetView a, b;
    live_view(a, kG0, kStages[s], 1);   // Paketo,  sprite_id 0
    live_view(b, kG0, kStages[s], 3);   // Rafagon, sprite_id 2
    CHECK_EQ(a.gene_species, b.gene_species);
    CHECK(a.species_id != b.species_id);
    CHECK(a.form != b.form);
    CHECK(drawn_set(a, POSE_IDLE) != drawn_set(b, POSE_IDLE));
    // And both are creature bodies, not furniture.
    CHECK(drawn_set(a, POSE_IDLE) >= SPRITE_BODY_FIRST);
    CHECK(drawn_set(a, POSE_IDLE) <= SPRITE_BODY_LAST);
    CHECK(drawn_set(b, POSE_IDLE) >= SPRITE_BODY_FIRST);
    CHECK(drawn_set(b, POSE_IDLE) <= SPRITE_BODY_LAST);
  }

  // CHILD and TEEN are deliberately NOT species-keyed: those two designs carry
  // the care quality sim.cpp froze into minor_form, and the atlas authors no
  // species art at either stage. Stating it here is what stops a later change
  // from silently overwriting care quality with a species and calling it a fix.
  for (uint8_t st = STAGE_CHILD; st <= STAGE_TEEN; ++st) {
    PetView a, b;
    live_view(a, kG0, st, 1);
    live_view(b, kG0, st, 3);
    CHECK_EQ(a.form, b.form);
    CHECK_EQ(drawn_set(a, POSE_IDLE), drawn_set(b, POSE_IDLE));
  }
}

// -----------------------------------------------------------------------------
//  MUTATIONS THIS CATCHES, MEASURED: pet_art_key() ignoring species_id
//  (73 failed checks), SPRITE_BABY_BODIES 8 -> 1 (25), SPRITE_ADULT_BODIES
//  6 -> 1 (49), and restoring the naive SPR_BABY_BLOB + sprite_id resolution
//  (41).
//
//  AND THE ONE IT DOES NOT, WHICH THE FIRST DRAFT OF THIS COMMENT CLAIMED IT
//  DID. Shrinking the pool to 3 (or the adult pool to 2) does NOT break it, and
//  saying so was a sentence wider than the tree: EVERY shipped rule is a SINGLE
//  step between CONSECUTIVE art keys (sprite_id == id - 1, and a family's three
//  stages are three consecutive ids), so `k % P != (k+1) % P` holds for every
//  pool size except 1. Measured: with SPRITE_BABY_BODIES at 3 this case passes
//  and `a_pebble_with_no_species_row_draws_what_it_always_did` is what catches
//  it, at 3,549 checks. The pool size is guarded; it is just not guarded here.
//
//  It walks EVERY rule in EVOLUTION_RULES, not a sample, and it walks the LIVE
//  path - so it also fails if pet_view_attach() stops re-deriving the form.
// -----------------------------------------------------------------------------
TEST(every_evolution_reaches_the_body) {
  const uint8_t n = (uint8_t)(sizeof EVOLUTION_RULES / sizeof EVOLUTION_RULES[0]);
  CHECK(n > 0);
  uint8_t moved = 0;
  for (uint8_t r = 0; r < n; ++r) {
    const EvolutionRule& rule = EVOLUTION_RULES[r];
    static const uint8_t kStages[] = { STAGE_BABY, STAGE_ADULT, STAGE_SENIOR };
    for (uint8_t s = 0; s < (uint8_t)(sizeof kStages / sizeof kStages[0]); ++s) {
      PetView from, to;
      live_view(from, 0x1234u, kStages[s], rule.species);
      live_view(to,   0x1234u, kStages[s], rule.target);
      if (drawn_set(from, POSE_IDLE) != drawn_set(to, POSE_IDLE)) ++moved;
      CHECK(drawn_set(from, POSE_IDLE) != drawn_set(to, POSE_IDLE));
    }
  }
  // Every rule, at all three species-keyed stages. Written as a number so that
  // a rule quietly dropped from the table fails here too.
  CHECK_EQ((int)moved, (int)n * 3);
}

// -----------------------------------------------------------------------------
//  THE FALLBACK IS BIT-IDENTICAL TO THE OLD BEHAVIOUR.
//
//  A Pebble with no species row - id 0, the creator's 200..209, or anything
//  past the roster - must render EXACTLY as it did before P4-C4a, because that
//  is what makes this change safe for an unfiled egg and for P8's customs. The
//  pre-change arithmetic is written out below rather than described.
//
//  MUTATION THIS CATCHES: `% SPRITE_BABY_BODIES` -> `% 6` in
//  sprite_design_of(), or pet_art_key() answering 0 instead of gene_species for
//  an unresolvable id.
// -----------------------------------------------------------------------------
static uint8_t legacy_form_of(uint8_t gene, uint8_t minor_form, uint8_t stage) {
  if (stage >= STAGE_ADULT) return (uint8_t)(gene % SPRITE_ADULT_BODIES);
  if (stage == STAGE_TEEN)  return (uint8_t)(minor_form >> 4);
  if (stage == STAGE_CHILD) return (uint8_t)(minor_form & 0x0Fu);
  return 0;
}

static uint8_t legacy_set_id(uint8_t gene, uint8_t stage, uint8_t form,
                             uint8_t pose) {
  if (stage == STAGE_EGG)  return SPR_EGG_IDLE;
  if (pose == POSE_SLEEP) {
    switch (stage) {
      case STAGE_BABY:  return SPR_SLEEP_BABY;
      case STAGE_CHILD: return SPR_SLEEP_CHILD;
      case STAGE_TEEN:  return SPR_SLEEP_TEEN;
      default:          return SPR_SLEEP_ADULT;
    }
  }
  if (pose == POSE_SICK && stage >= STAGE_CHILD) {
    switch (stage) {
      case STAGE_CHILD: return SPR_SICK_CHILD;
      case STAGE_TEEN:  return SPR_SICK_TEEN;
      default:          return SPR_SICK_ADULT;
    }
  }
  if (pose == POSE_EAT && stage >= STAGE_CHILD) {
    switch (stage) {
      case STAGE_CHILD: return SPR_EAT_CHILD;
      case STAGE_TEEN:  return SPR_EAT_TEEN;
      default:          return SPR_EAT_ADULT;
    }
  }
  switch (stage) {
    case STAGE_BABY:   return (uint8_t)(SPR_BABY_BLOB + (gene & 0x07u));
    case STAGE_CHILD:  return (uint8_t)(SPR_CHILD_GOOD + (form <= 1u ? form : 0u));
    case STAGE_TEEN:   return (uint8_t)(SPR_TEEN_GOOD  + (form <= 1u ? form : 0u));
    case STAGE_ADULT:  return (uint8_t)(SPR_ADULT_BOLOTA
                                       + (form < SPRITE_ADULT_BODIES ? form : 0u));
    default:           return (uint8_t)(SPR_SENIOR_BOLOTA
                                       + (form < SPRITE_ADULT_BODIES ? form : 0u));
  }
}

TEST(a_pebble_with_no_species_row_draws_what_it_always_did) {
  // 0, the whole creator range, one id past the roster and the top of the byte.
  static const uint8_t kNoRow[] = { 0, 200, 201, 202, 203, 204, 205, 206, 207,
                                    208, 209, (uint8_t)(SPECIES_TABLE_COUNT + 1u),
                                    255 };
  for (uint8_t k = 0; k < (uint8_t)(sizeof kNoRow / sizeof kNoRow[0]); ++k) {
    CHECK(species_get(kNoRow[k]) == nullptr);
    for (uint8_t gene = 0; gene < 16u; ++gene) {
      // The key IS the nibble when there is no row.
      CHECK_EQ(pet_art_key(kNoRow[k], gene), gene);
      // minor_form only reaches CHILD and TEEN, and only as two nibbles: the
      // in-range pair, the out-of-range values both clamps have to fold, and
      // the two extremes. A full 0..255 sweep is 1.2 M checks that say nothing
      // these seven do not.
      static const uint8_t kMinor[] = { 0x00, 0x01, 0x10, 0x11, 0x2F, 0xF0, 0xFF };
      for (uint8_t stage = 0; stage < 6u; ++stage) {
        for (uint8_t m = 0; m < (uint8_t)(sizeof kMinor / sizeof kMinor[0]); ++m) {
          const uint8_t mf = kMinor[m];
          for (uint8_t pose = 0; pose < (uint8_t)POSE_COUNT; ++pose) {
            const uint8_t key  = pet_art_key(kNoRow[k], gene);
            const uint8_t form = sprite_form_of(key, mf, (Stage)stage);
            CHECK_EQ(sprite_set_id(stage, form, pose),
                     legacy_set_id(gene, stage,
                                   legacy_form_of(gene, mf, stage), pose));
          }
        }
      }
    }
  }
}

// -----------------------------------------------------------------------------
//  THE NAME. The roster carries all 36 Spanish names; pet_species_name() hands
//  back nullptr - never "" - for the three no-row inputs, so a caller has to
//  choose a fallback instead of drawing an empty header.
//
//  MUTATION THIS CATCHES: pet_species_name() answering S(STR_EMPTY) rather than
//  nullptr for an unresolvable id, which would blank HOME's name line.
// -----------------------------------------------------------------------------
TEST(the_species_name_is_the_rosters_own) {
  for (uint8_t id = 1; id <= SPECIES_TABLE_COUNT; ++id) {
    const char* n = pet_species_name(id);
    CHECK(n != nullptr);
    if (!n) continue;
    CHECK(n[0] != '\0');
    CHECK(n == S(SPECIES_TABLE[id - 1u].name_idx));
  }
  // The starter and its two evolutions are three DIFFERENT words: an evolution
  // has to change the name as well as the pixels.
  CHECK(strcmp(pet_species_name(1), pet_species_name(2)) != 0);
  CHECK(strcmp(pet_species_name(2), pet_species_name(3)) != 0);

  CHECK(pet_species_name(0) == nullptr);
  CHECK(pet_species_name(200) == nullptr);
  CHECK(pet_species_name(209) == nullptr);
  CHECK(pet_species_name((uint8_t)(SPECIES_TABLE_COUNT + 1u)) == nullptr);
}

// -----------------------------------------------------------------------------
//  AND THE MODEL HALF, kept from the case this file used to carry: an evolution
//  really does move species_id and the evo_state stage bits, and the numbers
//  under the body move with it. What is new is that it now checks the body the
//  LIVE view would draw, through the fill pair ui.cpp calls.
// -----------------------------------------------------------------------------
TEST(an_evolution_changes_the_creature_the_live_view_draws) {
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

  SimView sv;
  sim_view_of(sv, 0x1234u, STAGE_ADULT, 0u);

  PetView before;
  pet_view_fill_sim(before, sv, POSE_IDLE);
  pet_view_attach(before, &p);

  EvoContext ctx;
  evo_context_clear(ctx);                 // the shipped rules need no input
  CHECK(evolution_apply(p, ctx));

  const SpeciesDef* s2 = species_get(p.species_id);
  CHECK(s2 != nullptr);
  if (!s2) return;
  PetView after;
  pet_view_fill_sim(after, sv, POSE_IDLE);
  pet_view_attach(after, &p);

  CHECK(after.species_id != before.species_id);
  CHECK(after.form != before.form);
  CHECK(drawn_set(after, POSE_IDLE) != drawn_set(before, POSE_IDLE));
  // The LIFE stage is the simulation's and an evolution does not move it: what
  // changed is the creature standing at that stage.
  CHECK_EQ(after.stage, before.stage);
  CHECK_EQ(after.gene_species, before.gene_species);

  // And the numbers under it: hp_max moved with the species, and a full Pebble
  // came out of the ceremony full rather than hurt.
  CHECK(xp_hp_max(s2->base_hp, p.level) != xp_hp_max(s1->base_hp, p.level));
  CHECK_EQ(p.hp_cur, xp_hp_max(s2->base_hp, p.level));
}
