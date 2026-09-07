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
#include "core/utf8.h"
#include "ui/pet_view.h"
#include "data/attacks_table.h"    // ATTACK_COUNT: the move-set search walks it
#include "game/species_custom.h"   // the registry a drawn body comes out of
#include "game/validate.h"         // creator_cost_of / validate_custom_species

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
  v.form         = sprite_form_of(4u, STAGE_ADULT);
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

  // A NICKNAME IS STORED AS RAW LATIN-1 AND DRAWN AS UTF-8, and this attach is
  // the one crossing between the two (core/utf8.h). Before P10-C4 it was an
  // snprintf("%s") that copied the byte straight through, so a Pebble called
  // "Ninon" with an n-tilde handed drawUTF8() a lone 0xF1 - which on the device
  // opens a four-byte decoder state and swallows the character after it, and on
  // the host walked past the end of the buffer.
  //
  // The WIDEST case is the one that matters: twelve accented characters are
  // twelve stored bytes and TWENTY-FOUR drawn ones, so a name buffer sized in
  // stored bytes cut such a name in half.
  static const char kL1[] = "\xD1\xC1\xC9\xCD\xD3\xDA\xDC\xD1\xC1\xC9\xCD\xD3";
  memcpy(p.nickname, kL1, sizeof kL1);
  CHECK_EQ((int)strlen(p.nickname), 12);
  pet_view_attach(v, &p);
  CHECK_EQ((int)strlen(v.name), 24);              // every character survived
  CHECK(u8_well_formed(v.name));
  CHECK_EQ((int)u8_count(v.name), 12);
  // and it is the SAME twelve characters, in order.
  for (uint8_t i = 0; i < 12u; ++i) {
    const uint32_t cp = (uint32_t)(((uint8_t)v.name[i * 2] & 0x1Fu) << 6) |
                        (uint32_t)((uint8_t)v.name[i * 2 + 1] & 0x3Fu);
    CHECK_EQ((int)cp, (int)(uint8_t)kL1[i]);
  }
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
//
// AND IT IS A RE-STATEMENT, NOT THE THING ITSELF - said here because every
// case below leans on it. ui/petfx.cpp includes render.h, so it is a
// DEVICE-ONLY translation unit that no host binary compiles; this line is a
// copy of petfx.cpp's own two-line resolution (`sprite_set_id(p.stage, p.form,
// pose)`), and a change made INSIDE petfx would be caught by no host test. The
// exposure is small and bounded by things the gate does hold: tools/check.sh
// forbids petfx.cpp from including sim.h or genome.h, so the only body source
// it has is PetView.form, and the goldens in tests/test_screens.cpp cover the
// still body that screen_home.cpp draws through the same expression. What is
// NOT covered is petfx's animation path, and putting it under test needs a
// host fake for render.h, which is a phase of its own.
static uint8_t drawn_set(const PetView& v, uint8_t pose) {
  return sprite_set_id(v.stage, v.form, pose);
}

// WHICH LIFE STAGE A LEVEL IS - ASKED, NOT RESTATED. sim.cpp's stage_of_level()
// is static and there is no accessor for it, and writing the four thresholds
// out here would be a second copy of the ladder that could drift from the one
// the firmware runs - which is exactly the defect that got pet_view_fill()
// deleted. So this binds a Pebble at the level and reads the stage the
// SIMULATION derived for it. The bound instance is file-static because sim_bind
// keeps a pointer to it.
static PebbleInstance g_ladder;
static uint8_t stage_at_level(uint8_t level) {
  memset(&g_ladder, 0, sizeof g_ladder);
  g_ladder.magic      = (uint16_t)PEBBLE_MAGIC;
  g_ladder.layout_ver = (uint8_t)PEBBLE_LAYOUT_VER;
  g_ladder.species_id = 1;
  g_ladder.id         = 0x0A0B0C0Du;
  g_ladder.level      = level;
  for (uint8_t c = 0; c < PB_CARE_COUNT; ++c) g_ladder.care[c] = PB_CARE_MILLI_MAX;
  sim_bind(g_ladder);
  const SimView* sv = sim_view();
  return sv ? sv->stage : (uint8_t)STAGE_EGG;
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

  // CHILD AND TEEN ARE SPECIES-KEYED NOW, AND THE INVERSION IS THE POINT.
  //
  // This block asserted the OPPOSITE until P9-C3: `CHECK_EQ(a.form, b.form)`
  // and the same drawn set, because the atlas authored two designs at each of
  // those stages and they carried the CARE QUALITY sim.cpp froze into
  // minor_form - not the species. The comment said "stating it here is what
  // stops a later change from silently overwriting care quality with a species
  // and calling it a fix".
  //
  // P9-C3 IS THAT CHANGE, AND IT IS NOT CALLING IT A FIX. The care-quality
  // bodies are deleted, with the rest of the 38-set legacy atlas, because one
  // 24x24 body per species has nowhere to put a second variant: keeping it
  // would have meant 60 more drawings. data/sprites.h's LOOKUP banner records
  // the deletion and says what a screen should do instead (draw care quality
  // with the renderer - a sweat emote, a dulled dither - not with a second
  // atlas). What is asserted here is the rule that replaced it, at the same two
  // stages, so the change is pinned in both directions rather than removed.
  for (uint8_t st = STAGE_CHILD; st <= STAGE_TEEN; ++st) {
    PetView a, b;
    live_view(a, kG0, st, 1);
    live_view(b, kG0, st, 3);
    CHECK(a.form != b.form);
    CHECK(drawn_set(a, POSE_IDLE) != drawn_set(b, POSE_IDLE));
  }

  // AND minor_form NO LONGER REACHES THE ATLAS AT ALL. THAT IS ENFORCED BY THE
  // COMPILER, NOT BY THIS FILE, and the 1,536-check loop that used to stand here
  // claiming otherwise was DELETED AT P9-C6 because it could not fail: both
  // sides of its CHECK_EQ were the same constexpr call with the same arguments
  // (`sprite_set_id(st, sprite_form_of(11u, st), POSE_IDLE)` against a `base`
  // computed from that same expression), and its loop variable was discarded on
  // the next line with `(void)mf`. It asserted f(x) == f(x), 1,536 times, under
  // a comment that promised a sweep over minor_form's whole range.
  //
  // P9-C3 removed the minor_form parameter from sprite_form_of() outright, so
  // "something re-reads minor_form for art" is now a BUILD ERROR at every call
  // site rather than a test failure - which is the stronger guarantee, and the
  // reason there is nothing left to run here.
}

// -----------------------------------------------------------------------------
//  MUTATIONS THIS CATCHES. pet_art_key() ignoring species_id; sprite_form_of()
//  answering a constant; apply_species_design() dropping a stage.
//
//  THE THREE THE OLD COMMENT MEASURED ARE GONE WITH THE THING THEY MUTATED.
//  They were SPRITE_BABY_BODIES 8 -> 1 (25 failed checks), SPRITE_ADULT_BODIES
//  6 -> 1 (49) and "restore the naive SPR_BABY_BLOB + sprite_id resolution"
//  (41). P9-C3 deleted both pools and MADE the naive resolution the real one,
//  so there is no fold left to shrink. The observation the old comment carried
//  is still true and still worth keeping: a fold of 3 would NOT break this case,
//  because every shipped rule is a SINGLE step between CONSECUTIVE art keys
//  (sprite_id == id - 1, and a family's three stages are three consecutive ids),
//  so `k % P != (k+1) % P` holds for every P except 1.
//
//  It walks EVERY rule in EVOLUTION_RULES, not a sample, and it walks the LIVE
//  path - so it also fails if pet_view_attach() stops re-deriving the form.
//
//  AND IT IS ABOUT THREE STAGES, NOT ABOUT THE MOMENT A RULE FIRES. The three
//  it walks are the three the species keys, and 12 of these 24 rules fire at a
//  LEVEL that is not any of them - every family's FIRST evolution is at level
//  8, 10 or 12, which sim.cpp's ladder calls CHILD or TEEN. So this case says
//  "wherever a species-keyed body is drawn, these two species draw different
//  ones"; it deliberately does NOT say "the player sees the body change when
//  they confirm the evolution". The case that walks each rule at the stage it
//  really fires at - and states, as a number, which half sees a changed body
//  there and which half sees only a changed NAME - is
//  every_rule_measured_at_the_level_it_actually_fires_at, at the bottom of this
//  file.
// -----------------------------------------------------------------------------
TEST(every_evolution_reaches_the_body) {
  const uint8_t n = (uint8_t)(sizeof EVOLUTION_RULES / sizeof EVOLUTION_RULES[0]);
  CHECK(n > 0);
  uint8_t moved = 0;
  for (uint8_t r = 0; r < n; ++r) {
    const EvolutionRule& rule = EVOLUTION_RULES[r];
    // FIVE STAGES SINCE P9-C3, where this was three. CHILD and TEEN were
    // excluded because their two bodies carried care quality rather than the
    // species; they are species-keyed now like every other stage.
    static const uint8_t kStages[] = { STAGE_BABY, STAGE_CHILD, STAGE_TEEN,
                                       STAGE_ADULT, STAGE_SENIOR };
    for (uint8_t s = 0; s < (uint8_t)(sizeof kStages / sizeof kStages[0]); ++s) {
      PetView from, to;
      live_view(from, 0x1234u, kStages[s], rule.species);
      live_view(to,   0x1234u, kStages[s], rule.target);
      if (drawn_set(from, POSE_IDLE) != drawn_set(to, POSE_IDLE)) ++moved;
      CHECK(drawn_set(from, POSE_IDLE) != drawn_set(to, POSE_IDLE));
    }
  }
  // Every rule, at all five stages. Written as a number so that a rule quietly
  // dropped from the table fails here too.
  CHECK_EQ((int)moved, (int)n * 5);
}

// -----------------------------------------------------------------------------
//  THE FALLBACK FOR A PEBBLE WITH NO SPECIES ROW.
//
//  A Pebble with no row - id 0, the creator's 200..209, or anything past the
//  roster - falls on the genome's species nibble. This case carried a
//  byte-for-byte FROZEN COPY of the pre-P4-C4a resolution (legacy_set_id(),
//  38 sets, four sleep bodies, three sick, three eat, `gene & 7` at BABY and
//  `% 6` at ADULT) and asserted the live lookup agreed with it everywhere.
//
//  P9-C3 DELETED THE ATLAS THAT ORACLE DESCRIBED, so the oracle is REWRITTEN,
//  not deleted - which is what SURVEY ONE asked for and what stops this from
//  becoming a case that tests nothing. What is frozen now is the resolution
//  this chunk introduced, written out as arithmetic rather than as a call to
//  the function under test:
//
//      EGG                     -> PBSPR_EGG_IDLE, at every pose
//      POSE_SLEEP, any stage   -> PBSPR_SLEEP
//      POSE_SICK,  any stage   -> PBSPR_SICK
//      anything else           -> PB_SPRITE_BODY_FIRST + nibble
//
//  and, separately, that minor_form changes nothing - the property the old
//  oracle spent two of its branches on and that P9-C3 removed.
//
//  MUTATIONS THIS CATCHES: pet_art_key() answering 0 instead of gene_species
//  for an unresolvable id; sprite_set_id() losing its `+ form`; the sleep or
//  sick branch falling through to the body; POSE_EAT growing a set of its own;
//  and any re-introduced fold `% P` with P <= 15.
// -----------------------------------------------------------------------------
static uint8_t expected_set_id(uint8_t gene, uint8_t stage, uint8_t pose) {
  if (stage == STAGE_EGG)          return (uint8_t)PBSPR_EGG_IDLE;
  if (pose  == POSE_SLEEP)         return (uint8_t)PBSPR_SLEEP;
  if (pose  == POSE_SICK)          return (uint8_t)PBSPR_SICK;
  return (uint8_t)(PB_SPRITE_BODY_FIRST
                   + (gene < (uint8_t)PB_SPRITE_BODY_COUNT ? gene : 0u));
}

TEST(a_pebble_with_no_species_row_draws_its_genome_nibbles_body) {
  // 0, the whole creator range, one id past the roster and the top of the byte.
  static const uint8_t kNoRow[] = { 0, 200, 201, 202, 203, 204, 205, 206, 207,
                                    208, 209, (uint8_t)(SPECIES_TABLE_COUNT + 1u),
                                    255 };
  for (uint8_t k = 0; k < (uint8_t)(sizeof kNoRow / sizeof kNoRow[0]); ++k) {
    CHECK(species_get(kNoRow[k]) == nullptr);
    for (uint8_t gene = 0; gene < 16u; ++gene) {
      // The key IS the nibble when there is no row.
      CHECK_EQ(pet_art_key(kNoRow[k], gene), gene);
      for (uint8_t stage = 0; stage < 6u; ++stage) {
        for (uint8_t pose = 0; pose < (uint8_t)POSE_COUNT; ++pose) {
          const uint8_t key  = pet_art_key(kNoRow[k], gene);
          const uint8_t form = sprite_form_of(key, (Stage)stage);
          CHECK_EQ(sprite_set_id(stage, form, pose),
                   expected_set_id(gene, stage, pose));
        }
      }
      // Sixteen nibbles, sixteen DIFFERENT bodies. The old atlas folded them
      // onto eight at BABY and six at ADULT, so half of them collided; this is
      // the half of the change a player would see on an unfiled egg.
      for (uint8_t other = 0; other < 16u; ++other) {
        if (other == gene) continue;
        CHECK(sprite_set_id((uint8_t)STAGE_ADULT,
                            sprite_form_of(gene, STAGE_ADULT),
                            (uint8_t)POSE_IDLE)
              != sprite_set_id((uint8_t)STAGE_ADULT,
                               sprite_form_of(other, STAGE_ADULT),
                               (uint8_t)POSE_IDLE));
      }
    }
  }
}

// -----------------------------------------------------------------------------
//  THE NAME. The roster carries a Spanish name per species (60 since P9-C3);
//  pet_species_name() hands
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
  // LEVEL 15, not 8, AND THE FIX IS THE POINT. This case used to set level 8 -
  // "the level family 1's first rule asks for" - and then draw the pet at
  // STAGE_ADULT, which sim.cpp's ladder says a level-8 pet cannot be: it is
  // CHILD until 10 and TEEN until 15. So the case proved a body change at a
  // stage its own Pebble could not have been standing at. Paketo -> Fragmar is
  // still the rule under test (evolution_apply() only asks that the level is at
  // or above the rule's minimum), and 15 is a level at which the drawn body and
  // the life stage agree with each other.
  p.level      = 15;
  const SpeciesDef* s1 = species_get(p.species_id);
  CHECK(s1 != nullptr);
  if (!s1) return;
  p.hp_cur = xp_hp_max(s1->base_hp, p.level);

  SimView sv;
  sim_view_of(sv, 0x1234u, stage_at_level(p.level), 0u);
  CHECK_EQ(sv.stage, (uint8_t)STAGE_ADULT);

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

// =============================================================================
//  P4-C4 FOLLOW-UP: EACH RULE MEASURED AT THE LEVEL IT ACTUALLY FIRES AT.
//
//  THE HOLE THIS CLOSES IS IN A SENTENCE, NOT IN THE CODE. P4-C4a's own tests
//  drive all 24 rules at BABY, ADULT and SENIOR - the three stages the species
//  keys - and the commit then said the obligation was closed. But sim.cpp's
//  ladder is >=20 SENIOR / >=15 ADULT / >=10 TEEN / >=5 CHILD, and TWELVE of
//  the 24 shipped rules have a minimum level of 8, 10 or 12: every family's
//  FIRST evolution, the starter's Paketo -> Fragmar among them. Those twelve
//  fire at CHILD or TEEN, which are the two stages apply_species_design()
//  deliberately does NOT touch - so at the moment the player confirms one of
//  them the drawn body is bit-identical and only the NAME and hp_max move. The
//  three stages the other case walks are three stages those twelve rules can
//  never occur at, so it cannot notice.
//
//  P9-C3 CLOSED IT, and this case is what records that rather than being
//  deleted with the policy it measured. The old policy was: the atlas authors
//  exactly two CHILD designs and two TEEN designs, both carrying the care
//  quality sim.cpp froze into minor_form, and folding 36 species onto two
//  designs would say almost nothing about the species while destroying the one
//  thing those designs did say. That was the right call against THAT atlas.
//  There is one 24x24 body per species now, the care-quality bodies are gone
//  (data/sprites.h's LOOKUP banner records the deletion), apply_species_design()
//  no longer skips CHILD and TEEN, and every rule moves the drawn body at the
//  level it actually fires at - the starter's Paketo -> Fragmar at level 8
//  included, for the first time in the project.
//
//  THE COUNTS STAY. `only_the_name` is asserted to be ZERO now rather than 12,
//  and the CHILD/TEEN population is still counted separately, because a content
//  pack that moves a rule across a stage threshold is still the thing that
//  makes the sentence in CHANGELOG.md and the plan wrong.
//
//  MUTATIONS THIS CATCHES: apply_species_design() skipping any stage again (the
//  CHECK(moved) arm fails, naming nothing else); sprite_form_of() answering a
//  constant; a content pack moving a rule's level across a stage threshold (the
//  two counts).
// =============================================================================
TEST(every_rule_changes_the_body_at_the_level_it_actually_fires_at) {
  // The ladder itself, at the five anchors, so a reader can check the mapping
  // below without opening sim.cpp - and so a retuned ladder fails HERE, with a
  // sentence, rather than only in the counts.
  CHECK_EQ(stage_at_level(1),  (uint8_t)STAGE_BABY);
  CHECK_EQ(stage_at_level(5),  (uint8_t)STAGE_CHILD);
  CHECK_EQ(stage_at_level(10), (uint8_t)STAGE_TEEN);
  CHECK_EQ(stage_at_level(15), (uint8_t)STAGE_ADULT);
  CHECK_EQ(stage_at_level(20), (uint8_t)STAGE_SENIOR);

  const uint8_t n = (uint8_t)EVOLUTION_RULES_COUNT;
  int only_the_name = 0, body_too = 0, reaches_the_body_at_adult = 0;

  for (uint8_t r = 0; r < n; ++r) {
    const EvolutionRule& rule = EVOLUTION_RULES[r];
    const uint8_t st = stage_at_level(rule.level);

    PetView from, to;
    live_view(from, 0x1234u, st, rule.species);
    live_view(to,   0x1234u, st, rule.target);
    const bool moved = drawn_set(from, POSE_IDLE) != drawn_set(to, POSE_IDLE);

    // THE NAME MOVES FOR EVERY RULE, AT EVERY STAGE, and that is what makes the
    // twelve below a narrower claim rather than an empty one: the player who
    // evolves at level 8 does see the creature they now have, in words.
    const char* a = pet_species_name(rule.species);
    const char* b = pet_species_name(rule.target);
    CHECK(a != nullptr);
    CHECK(b != nullptr);
    if (a && b) CHECK(strcmp(a, b) != 0);

    // EVERY rule, at the level it really fires at, moves the drawn body. The
    // arm below used to be `CHECK(!moved)` for the CHILD/TEEN population.
    CHECK(moved);
    CHECK(from.form != to.form);
    if (!moved) ++only_the_name;
    ++body_too;

    if (st == (uint8_t)STAGE_CHILD || st == (uint8_t)STAGE_TEEN) {
      // Counted so the stage split is still visible, and checked at ADULT too -
      // the body has to keep moving after the pet grows past the level the rule
      // fired at, which is a different statement from the one above.
      PetView fa, ta;
      live_view(fa, 0x1234u, (uint8_t)STAGE_ADULT, rule.species);
      live_view(ta, 0x1234u, (uint8_t)STAGE_ADULT, rule.target);
      CHECK(drawn_set(fa, POSE_IDLE) != drawn_set(ta, POSE_IDLE));
      ++reaches_the_body_at_adult;
    }
  }

  // THE NUMBERS CHANGELOG.md AND THE PLAN QUOTE. A content pack that moves a
  // rule across a stage threshold fails here, which is what makes the sentence
  // in those files a checked one instead of a remembered one.
  CHECK_EQ(body_too, (int)n);
  CHECK_EQ(only_the_name, 0);
  // Half the roster's rules fire below ADULT - every family's FIRST evolution,
  // at level 8, 10 or 12 - and those are precisely the twenty this project drew
  // no body for until P9-C3.
  CHECK_EQ(reaches_the_body_at_adult, 20);
  printf("  %d of %d rules change the drawn body at the level they fire at; "
         "%d of them fire below ADULT\n",
         body_too, (int)n, reaches_the_body_at_adult);
}


// =============================================================================
//  THE CREATOR'S BODY ENTERS THE VIEW HERE, AND NOWHERE ELSE (P10-C4b)
//
//  ui/petfx.cpp draws the ANIMATED body on HOME - the one the device actually
//  shows - and its banner forbids it from knowing what a species is. So it does
//  not ask the registry; it reads PetView.custom_bits, which apply_species_
//  design() fills. That single assignment is the whole device path: null it and
//  a drawn Pebble wears the atlas body on hardware while every golden in the
//  suite, which goes through the STILL path, stays green.
//
//  ui/petfx.cpp is compiled by no host binary, so this case cannot reach the
//  drawing. What it CAN do is hold the pointer that gets handed to it.
// =============================================================================
static void mk_custom_view_rec(CustomSpeciesRec& c, uint8_t slot, uint8_t seed)
{
  memset(&c, 0, sizeof c);
  c.magic   = (uint16_t)CS_MAGIC;
  c.version = (uint8_t)SAVE_SCHEMA_VERSION;
  c.slot    = slot;
  c.type    = (uint8_t)TYPE_SIGNAL;
  c.base[0] = 6u; c.base[1] = 5u; c.base[2] = 5u; c.base[3] = 5u;
  memcpy(c.name, "Bicho", 6);
  bool legal = false;
  for (uint8_t a = 1u; a <= (uint8_t)ATTACK_COUNT && !legal; ++a)
    for (uint8_t b = (uint8_t)(a + 1u); b <= (uint8_t)ATTACK_COUNT && !legal; ++b)
      for (uint8_t d = (uint8_t)(b + 1u); d <= (uint8_t)ATTACK_COUNT && !legal; ++d)
        for (uint8_t e = (uint8_t)(d + 1u); e <= (uint8_t)ATTACK_COUNT && !legal; ++e) {
          c.moves[0] = a; c.moves[1] = b; c.moves[2] = d; c.moves[3] = e;
          uint16_t su = 0, au = 0;
          creator_cost_of(c, su, au);
          c.budget_used = au;
          legal = (validate_custom_species(c) == (uint8_t)VR_OK);
        }
  CHECK(legal);
  for (uint8_t f = 0; f < (uint8_t)CS_SPRITE_FRAMES; ++f)
    for (uint8_t i = 0; i < (uint8_t)CS_SPRITE_BYTES; ++i)
      c.sprite[f][i] = (uint8_t)(0x55u ^ (uint8_t)(i * 7u + f * 33u + seed));
}

TEST(the_drawn_view_carries_the_creators_own_pixels) {
  csp_reset();
  CustomSpeciesRec c;
  mk_custom_view_rec(c, 0u, 0x4Du);
  CHECK(csp_install(c));
  const uint8_t id = csp_species_id(0);
  CHECK(id != 0u);

  PebbleInstance p;
  memset(&p, 0, sizeof p);
  p.species_id = id;
  p.level      = 5u;

  PetView v;
  memset(&v, 0, sizeof v);
  v.stage      = (uint8_t)STAGE_ADULT;
  v.species_id = id;
  pet_view_attach(v, &p);
  CHECK(v.custom_bits != nullptr);
  CHECK_EQ(memcmp(v.custom_bits, c.sprite[0], (size_t)CS_SPRITE_BYTES), 0);
  // FRAME 1 FOLLOWS FRAME 0 IN MEMORY, which is the layout ui/pet_view.h
  // promises and the one ui/petfx.cpp indexes by. A registry that stored the
  // two frames apart would satisfy every check above and break the animation.
  CHECK_EQ(memcmp(v.custom_bits + CS_SPRITE_BYTES, c.sprite[1],
                  (size_t)CS_SPRITE_BYTES), 0);

  // AND A ROSTER SPECIES CARRIES NOTHING, so the field is a statement about
  // creator Pebbles and not a pointer everybody now has.
  PebbleInstance r;
  memset(&r, 0, sizeof r);
  r.species_id = 3u;
  r.level      = 5u;
  PetView rv;
  memset(&rv, 0, sizeof rv);
  rv.stage      = (uint8_t)STAGE_ADULT;
  rv.species_id = 3u;
  pet_view_attach(rv, &r);
  CHECK(rv.custom_bits == nullptr);

  // An EGG never wears anybody's drawing (ui/pet_art.h says why), and the view
  // is where that starts: petfx reads this field before it looks at the stage.
  PetView ev;
  memset(&ev, 0, sizeof ev);
  ev.stage      = (uint8_t)STAGE_EGG;
  ev.species_id = id;
  pet_view_attach(ev, &p);
  CHECK(ev.custom_bits == nullptr);

  csp_reset();
}
