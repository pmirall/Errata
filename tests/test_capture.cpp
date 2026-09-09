// =============================================================================
//  ERRATA host test - test_capture.cpp
//  CAPTURE (game/capture.h, spec section 23, P5-C4).
//
//  THE HEADLINE CASE IS `a_captured_bug_validates_across_the_whole_roster`,
//  and it is the carried-forward debt: game/validate.h records that
//  box_new_bug() deliberately does NOT call validate_bug(), which left
//  capture as a third minting path with no VR_OK requirement anywhere.
//
//  THE MUTATION THAT PROVES IT CAN FAIL is dropping the genome seal - handing
//  cap_attempt() an unsealed genome turns every row VR_BAD_GENOME. It is not
//  hypothetical: `an_unsealed_genome_is_refused_by_name_before_anything_is_built`
//  drives exactly that input and asserts the named refusal, so the file
//  contains its own mutant.
//
//  RULES, as in tests/test_validate.cpp:
//   (a) every refusal case asserts the EXACT CaptureOutcome, never "not caught";
//   (b) every refusal case carries a POSITIVE CONTROL in the same function -
//       the same call with that one thing corrected must succeed - so the case
//       proves it reached the refusal it names and not an earlier one;
//   (c) a probability case that passes when the roll is ignored is worthless,
//       so `the_roll_decides_and_the_boundary_is_exact` drives the roll either
//       side of the chance by one.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "data/balance.h"
#include "game/box.h"
#include "game/capture.h"
#include "game/genome.h"
#include "game/species.h"
#include "game/validate.h"

static GameState g_gs;

static Genome sealed_genome(uint32_t lineage)
{
  Genome g;
  memset(&g, 0, sizeof g);
  g.lineage_id = lineage ? lineage : 1u;
  g.g0 = 0x1234u; g.g1 = 0x5678u; g.g2 = 0x09ABu;
  g.generation = 1u;
  genome_seal(g);
  return g;
}

// The hand-built genome four shipped test files use: everything but the seal.
// game/validate.h's "measured away" note is about exactly this shape.
static Genome unsealed_genome(void)
{
  Genome g;
  memset(&g, 0, sizeof g);
  g.lineage_id = 0x1234u;
  g.g0 = 0x1111u; g.g1 = 0x2222u; g.g2 = 0x3333u;
  g.generation = 1u;
  return g;                     // no genome_seal(): crc16 stays 0
}

static void fresh_box(void)
{
  memset(&g_gs, 0, sizeof g_gs);
  g_gs.box.magic          = (uint16_t)BOX_MAGIC;
  g_gs.box.schema_version = (uint8_t)SAVE_SCHEMA_VERSION;
  g_gs.box.active_slot    = (uint8_t)BOX_ACTIVE_NONE;
  g_gs.box.next_id_counter = 1u;
  box_bind(g_gs);
}

static EncounterResult wild(uint8_t species, uint8_t level)
{
  EncounterResult r;
  memset(&r, 0, sizeof r);
  r.outcome    = (uint8_t)ENC_OUT_WILD;
  r.species_id = species;
  r.level      = level;
  return r;
}

// =============================================================================
//  1. THE CHANCE
// =============================================================================
TEST(the_chance_is_the_base_the_gap_and_the_item_and_it_clamps_at_both_ends) {
  // Species 1 Paketo is COMMON, so its base is CAPTURE_BASE_PERMILLE[0].
  const SpeciesDef* sp = species_get(1);
  CHECK(sp != nullptr);
  if (!sp) return;
  CHECK_EQ(sp->rarity, SPECIES_RARITY_COMMON);
  CHECK_EQ(cap_chance_permille(1, 10, 10, 0), CAPTURE_BASE_PERMILLE[0]);

  // The level term is ONE-SIDED: above you it costs, below you it is free.
  CHECK_EQ(cap_chance_permille(1, 15, 10, 0),
           (uint16_t)(CAPTURE_BASE_PERMILLE[0] - 5u * CAPTURE_LEVEL_GAP_PERMILLE));
  CHECK_EQ(cap_chance_permille(1, 5, 10, 0), CAPTURE_BASE_PERMILLE[0]);
  CHECK_EQ(cap_chance_permille(1, 1, 30, 0), CAPTURE_BASE_PERMILLE[0]);

  // The item term is value x ITEM_CAPTURE_SCALE, which is the unit the pack
  // stated three different ways until P5-C4 settled it as a number.
  const ItemDef* cebo = item_get(4);
  CHECK(cebo != nullptr);
  if (!cebo) return;
  CHECK_EQ(cebo->klass, (uint8_t)ITEM_KLASS_CAPTURE);
  CHECK_EQ(cap_chance_permille(1, 20, 10, 4),
           (uint16_t)(CAPTURE_BASE_PERMILLE[0] - 10u * CAPTURE_LEVEL_GAP_PERMILLE
                      + (uint16_t)cebo->value * ITEM_CAPTURE_SCALE));

  // BOTH CLAMPS REALLY BITE, and the floor needs a RARE species to reach -
  // measured: a COMMON at the widest legal gap (level 30 wild against a level 1
  // active) is 700 - 29*12 = 352, nowhere near the floor, so a case written
  // with species 1 would have asserted a clamp that never fires. Species 3
  // Rafagon is RARE: 320 - 348 is negative before the clamp.
  const SpeciesDef* rare = species_get(3);
  CHECK(rare != nullptr);
  if (rare) CHECK_EQ(rare->rarity, SPECIES_RARITY_RARE);
  CHECK_EQ(cap_chance_permille(3, 30, 1, 0), (uint16_t)CAPTURE_MIN_PERMILLE);
  CHECK((int)CAPTURE_BASE_PERMILLE[SPECIES_RARITY_RARE]
        - 29 * (int)CAPTURE_LEVEL_GAP_PERMILLE < (int)CAPTURE_MIN_PERMILLE);
  CHECK_EQ(cap_chance_permille(1, 30, 1, 0),
           (uint16_t)(CAPTURE_BASE_PERMILLE[0] - 29u * CAPTURE_LEVEL_GAP_PERMILLE));
  CHECK_EQ(cap_chance_permille(1, 1, 30, 5), (uint16_t)CAPTURE_MAX_PERMILLE);
  // ...and nothing ever leaves the band, over the whole roster and the whole
  // level square, with and without every item in the table.
  for (uint8_t s = 1; s <= SPECIES_TABLE_COUNT; ++s)
    for (uint8_t wl = 1; wl <= (uint8_t)XP_LEVEL_MAX; ++wl)
      for (uint8_t al = 1; al <= (uint8_t)XP_LEVEL_MAX; al = (uint8_t)(al + 7u))
        for (uint8_t it = 0; it <= ITEM_COUNT; ++it) {
          const uint16_t p = cap_chance_permille(s, wl, al, it);
          CHECK(p >= (uint16_t)CAPTURE_MIN_PERMILLE);
          CHECK(p <= (uint16_t)CAPTURE_MAX_PERMILLE);
        }
}

TEST(the_chance_is_monotonic_in_rarity_in_the_level_gap_and_in_the_item) {
  // RARITY: pick one species per band and require the order. A "rarer is
  // harder" claim that only checked two adjacent bands would miss a swap at
  // the far end.
  uint8_t rep[4] = { 0, 0, 0, 0 };
  for (uint8_t s = 1; s <= SPECIES_TABLE_COUNT; ++s) {
    const SpeciesDef* sp = species_get(s);
    if (sp && rep[sp->rarity] == 0u) rep[sp->rarity] = s;
  }
  uint16_t last = 0xFFFFu;
  int bands = 0;
  for (uint8_t r = 0; r < 4u; ++r) {
    if (rep[r] == 0u) continue;                 // the roster has no SPECIAL yet
    const uint16_t p = cap_chance_permille(rep[r], 10, 10, 0);
    CHECK(p <= last);
    last = p;
    bands++;
  }
  CHECK(bands >= 3);                            // COMMON, UNCOMMON, RARE

  // THE LEVEL GAP: never easier as the gap widens, and strictly harder while
  // the clamp is not reached. "Never easier" alone would pass for a term that
  // did nothing at all, which is why the strict half is here too.
  int strict = 0;
  for (uint8_t wl = 1; wl < (uint8_t)XP_LEVEL_MAX; ++wl) {
    const uint16_t a = cap_chance_permille(1, wl, 5, 0);
    const uint16_t b = cap_chance_permille(1, (uint8_t)(wl + 1u), 5, 0);
    CHECK(b <= a);
    if (b < a) strict++;
  }
  CHECK(strict > 0);

  // THE ITEM: a better capture item is never worse, and Jaula Hash (45) really
  // beats Cebo (15).
  const uint16_t none = cap_chance_permille(3, 20, 10, 0);
  const uint16_t weak = cap_chance_permille(3, 20, 10, 4);
  const uint16_t good = cap_chance_permille(3, 20, 10, 5);
  CHECK(weak >= none);
  CHECK(good >= weak);
  CHECK(good > none);
}

TEST(a_bad_item_id_is_never_worth_more_than_no_item_and_a_bad_species_is_the_floor) {
  const uint16_t base = cap_chance_permille(1, 10, 10, 0);
  CHECK_EQ(cap_chance_permille(1, 10, 10, 0), base);
  CHECK_EQ(cap_chance_permille(1, 10, 10, (uint8_t)(ITEM_COUNT + 1u)), base);
  CHECK_EQ(cap_chance_permille(1, 10, 10, 255), base);
  // A NON-CAPTURE item contributes nothing: an XP candy is not a capture chip
  // because both have a `value`.
  for (uint8_t i = 1; i <= ITEM_COUNT; ++i) {
    const ItemDef* it = item_get(i);
    if (!it || it->klass == (uint8_t)ITEM_KLASS_CAPTURE) continue;
    CHECK_EQ(cap_chance_permille(1, 10, 10, i), base);
  }
  // An unknown species is the FLOOR and not zero: the clamp's job is to say
  // that no catch is impossible.
  CHECK_EQ(cap_chance_permille(0, 10, 10, 0), (uint16_t)CAPTURE_MIN_PERMILLE);
  CHECK_EQ(cap_chance_permille(250, 10, 10, 5), (uint16_t)CAPTURE_MIN_PERMILLE);
}

// =============================================================================
//  2. THE ATTEMPT
// =============================================================================
TEST(the_roll_decides_and_the_boundary_is_exact) {
  // A CASE THAT PASSES WHEN THE ROLL IS IGNORED IS WORTHLESS, so the roll is
  // driven one either side of the chance and both answers are named.
  fresh_box();
  const Genome g = sealed_genome(0xC0FFEEu);
  const EncounterResult e = wild(1, 10);
  const uint16_t chance = cap_chance_permille(1, 10, 10, 0);
  CHECK(chance > 0 && chance < 1000);

  CaptureState st; cap_reset(st);
  CaptureReport rep;
  // roll == chance - 1 catches (strictly below).
  CHECK(cap_attempt(st, e, 10, 0, (uint32_t)(chance - 1u), g, 1u, 1000u, rep));
  CHECK_EQ(rep.outcome, (uint8_t)CAP_CAUGHT);
  CHECK_EQ(rep.chance, chance);
  CHECK_EQ(rep.roll, (uint16_t)(chance - 1u));
  CHECK(rep.slot < box_capacity());

  // roll == chance does NOT catch.
  fresh_box();
  cap_reset(st);
  CHECK(!cap_attempt(st, e, 10, 0, (uint32_t)chance, g, 1u, 1000u, rep));
  CHECK_EQ(rep.outcome, (uint8_t)CAP_ESCAPED);
  CHECK_EQ(rep.slot, (uint8_t)BOX_SLOT_NONE);
  CHECK_EQ(box_count(), 0);

  // A raw u32 draw is reduced, so the caller may hand one straight in.
  fresh_box();
  cap_reset(st);
  CHECK(cap_attempt(st, e, 10, 0, 4000u + (uint32_t)(chance - 1u), g, 1u, 1000u, rep));
  CHECK_EQ(rep.outcome, (uint8_t)CAP_CAUGHT);
  CHECK_EQ(rep.roll, (uint16_t)(chance - 1u));
}

TEST(two_failures_and_the_creature_flees_and_a_third_press_buys_nothing) {
  fresh_box();
  const Genome g = sealed_genome(7u);
  const EncounterResult e = wild(1, 10);
  CaptureState st; cap_reset(st);
  CaptureReport rep;

  CHECK(!cap_attempt(st, e, 10, 0, 999u, g, 1u, 1000u, rep));
  CHECK_EQ(rep.outcome, (uint8_t)CAP_ESCAPED);
  CHECK_EQ(rep.attempts_left, (uint8_t)(CAPTURE_MAX_ATTEMPTS - 1));
  CHECK_EQ(st.fled, 0);

  CHECK(!cap_attempt(st, e, 10, 0, 999u, g, 1u, 1000u, rep));
  CHECK_EQ(rep.outcome, (uint8_t)CAP_FLED);
  CHECK_EQ(rep.attempts_left, 0);
  CHECK_EQ(st.fled, 1);

  // A third press is CAP_FLED and NOT a third roll: it does not catch even with
  // a roll that certainly would have.
  const uint16_t chance = cap_chance_permille(1, 10, 10, 0);
  CHECK(!cap_attempt(st, e, 10, 0, (uint32_t)(chance - 1u), g, 1u, 1000u, rep));
  CHECK_EQ(rep.outcome, (uint8_t)CAP_FLED);
  CHECK_EQ(box_count(), 0);

  // POSITIVE CONTROL: the same call on a fresh encounter does catch, so the
  // refusal above was the flee rule and not something earlier.
  cap_reset(st);
  CHECK(cap_attempt(st, e, 10, 0, (uint32_t)(chance - 1u), g, 1u, 1000u, rep));
  CHECK_EQ(rep.outcome, (uint8_t)CAP_CAUGHT);
}

TEST(an_unsealed_genome_is_refused_by_name_before_anything_is_built) {
  // THE MUTATION, AS A CASE. game/validate.h's note says box_new_bug() has
  // never required a sealed genome; measured over the whole roster, the seal is
  // the ONLY input that can make a constructed Bug fail validate_bug().
  // So capture refuses it up front, by name, and files nothing.
  fresh_box();
  const EncounterResult e = wild(1, 10);
  const uint16_t chance = cap_chance_permille(1, 10, 10, 0);
  CaptureState st; cap_reset(st);
  CaptureReport rep;

  CHECK(!cap_attempt(st, e, 10, 0, (uint32_t)(chance - 1u), unsealed_genome(),
                     1u, 1000u, rep));
  CHECK_EQ(rep.outcome, (uint8_t)CAP_BAD_GENOME);
  CHECK_EQ(box_count(), 0);                 // nothing was filed
  CHECK_EQ(st.attempts, 0);                 // and no attempt was spent
  CHECK_EQ(rep.slot, (uint8_t)BOX_SLOT_NONE);

  // POSITIVE CONTROL: seal that same genome and the identical call catches.
  Genome g = unsealed_genome();
  genome_seal(g);
  CHECK(genome_valid(g));
  CHECK(cap_attempt(st, e, 10, 0, (uint32_t)(chance - 1u), g, 1u, 1000u, rep));
  CHECK_EQ(rep.outcome, (uint8_t)CAP_CAUGHT);
  CHECK_EQ(box_count(), 1);
}

TEST(an_encounter_that_is_not_a_wild_one_is_refused_by_name) {
  fresh_box();
  const Genome g = sealed_genome(9u);
  CaptureState st; cap_reset(st);
  CaptureReport rep;

  EncounterResult e;
  memset(&e, 0, sizeof e);
  for (uint8_t o = 0; o < (uint8_t)ENC_OUT_COUNT; ++o) {
    if (o == (uint8_t)ENC_OUT_WILD) continue;
    e.outcome = o;
    e.species_id = 1u;                       // even with a species in the field
    e.level = 10u;
    CHECK(!cap_attempt(st, e, 10, 0, 0u, g, 1u, 1000u, rep));
    CHECK_EQ(rep.outcome, (uint8_t)CAP_NO_ENCOUNTER);
  }
  // A WILD result naming a species the roster does not have is refused too.
  e = wild(250u, 10u);
  CHECK(!cap_attempt(st, e, 10, 0, 0u, g, 1u, 1000u, rep));
  CHECK_EQ(rep.outcome, (uint8_t)CAP_NO_ENCOUNTER);
  e = wild(0u, 10u);
  CHECK(!cap_attempt(st, e, 10, 0, 0u, g, 1u, 1000u, rep));
  CHECK_EQ(rep.outcome, (uint8_t)CAP_NO_ENCOUNTER);

  // POSITIVE CONTROL.
  CHECK(cap_attempt(st, wild(1, 10), 10, 0, 0u, g, 1u, 1000u, rep));
  CHECK_EQ(rep.outcome, (uint8_t)CAP_CAUGHT);
  CHECK_EQ(box_count(), 1);
}

// =============================================================================
//  3. THE DEBT: A CAPTURED BUG PASSES THE VALIDATOR
// =============================================================================
TEST(a_captured_bug_validates_across_the_whole_roster_and_the_clamp_band) {
  // Every species, at every level a wild encounter can produce - which is the
  // whole 1..30 band, because the encounter clamp is symmetric around an active
  // level that itself spans 1..30. This is the only case that would catch a
  // clamp landing outside validate_level_band().
  int checked = 0;
  for (uint8_t s = 1; s <= SPECIES_TABLE_COUNT; ++s) {
    for (uint8_t lv = 1; lv <= (uint8_t)XP_LEVEL_MAX; ++lv) {
      fresh_box();
      CaptureState st; cap_reset(st);
      CaptureReport rep;
      const Genome g = sealed_genome(0x1000u + s);
      CHECK(cap_attempt(st, wild(s, lv), lv, 0, 0u, g, 0xBEEF0000u + s, 1700000000u, rep));
      CHECK_EQ(rep.outcome, (uint8_t)CAP_CAUGHT);
      CHECK_EQ(rep.reject, (uint8_t)VR_OK);
      const BugInstance* p = box_peek(rep.slot);
      CHECK(p != nullptr);
      if (!p) continue;
      // THE ASSERTION THE WHOLE FILE EXISTS FOR, made against the FILED slot
      // and not against a copy the test built.
      CHECK_EQ((uint8_t)validate_bug(*p), (uint8_t)VR_OK);
      CHECK_EQ(p->species_id, s);
      CHECK_EQ(p->level, lv);
      CHECK_EQ(p->origin, (uint8_t)ORIGIN_WILD);
      CHECK(p->id != 0u);
      checked++;
    }
  }
  CHECK_EQ(checked, (int)SPECIES_TABLE_COUNT * (int)XP_LEVEL_MAX);
}

TEST(the_same_roster_sweep_with_an_unsealed_genome_is_refused_every_time) {
  // The mutant, run as a sweep: this is the measurement behind the case above.
  // Drop the seal and every one of the 1,080 rows refuses, by name, with the
  // Box left empty - which is what makes the sweep above a test that can fail.
  int refused = 0;
  const Genome bad = unsealed_genome();
  CHECK(!genome_valid(bad));
  for (uint8_t s = 1; s <= SPECIES_TABLE_COUNT; ++s) {
    for (uint8_t lv = 1; lv <= (uint8_t)XP_LEVEL_MAX; ++lv) {
      fresh_box();
      CaptureState st; cap_reset(st);
      CaptureReport rep;
      CHECK(!cap_attempt(st, wild(s, lv), lv, 0, 0u, bad, 1u, 1700000000u, rep));
      CHECK_EQ(rep.outcome, (uint8_t)CAP_BAD_GENOME);
      CHECK_EQ(box_count(), 0);
      refused++;
    }
  }
  CHECK_EQ(refused, (int)SPECIES_TABLE_COUNT * (int)XP_LEVEL_MAX);
}

TEST(the_first_capture_into_an_empty_box_becomes_the_active_bug) {
  // MEASURED, AND IT IS WHY THE UNDO PATH IS NOT WRITTEN (game/capture.h):
  // box_new_bug() files as it constructs and mask_sync() makes the only
  // Bug in a non-empty Box the active one, so a first capture IS the active
  // slot - and box_release() refuses the active slot outright.
  fresh_box();
  CHECK_EQ(box_active(), (uint8_t)BOX_ACTIVE_NONE);
  CaptureState st; cap_reset(st);
  CaptureReport rep;
  CHECK(cap_attempt(st, wild(1, 5), 5, 0, 0u, sealed_genome(3u), 1u, 1000u, rep));
  CHECK_EQ(box_active(), rep.slot);
  CHECK(!box_release(rep.slot, true));        // refused: it is the active one
  CHECK_EQ(box_count(), 1);

  // A SECOND capture does not become active and does not disturb the first,
  // which is spec section 23's "the encounter must never delete the active
  // Bug" stated as a fact about the code.
  const uint32_t first_id = box_peek(rep.slot)->id;
  CaptureState st2; cap_reset(st2);
  CaptureReport rep2;
  CHECK(cap_attempt(st2, wild(4, 6), 6, 0, 0u, sealed_genome(4u), 2u, 1000u, rep2));
  CHECK(rep2.slot != rep.slot);
  CHECK_EQ(box_active(), rep.slot);
  CHECK_EQ(box_peek(rep.slot)->id, first_id);
  CHECK_EQ(box_count(), 2);
}

TEST(every_captured_bug_has_a_unique_id_and_the_box_stays_valid) {
  fresh_box();
  uint32_t ids[BOX_SLOTS];
  uint8_t n = 0;
  for (uint8_t k = 0; k < (uint8_t)BOX_SLOTS; ++k) {
    CaptureState st; cap_reset(st);
    CaptureReport rep;
    CHECK(cap_attempt(st, wild((uint8_t)(1u + k), (uint8_t)(5u + k)), 8, 0, 0u,
                      sealed_genome(0x2000u + k), 0x900u + k, 1700000000u, rep));
    CHECK_EQ(rep.outcome, (uint8_t)CAP_CAUGHT);
    ids[n++] = box_peek(rep.slot)->id;
  }
  CHECK_EQ(box_count(), (uint8_t)BOX_SLOTS);
  for (uint8_t i = 0; i < n; ++i) {
    CHECK(ids[i] != 0u);
    for (uint8_t j = (uint8_t)(i + 1u); j < n; ++j) CHECK(ids[i] != ids[j]);
  }
  for (uint8_t s = 0; s < (uint8_t)BOX_SLOTS; ++s) {
    const BugInstance* p = box_peek(s);
    CHECK(p != nullptr);
    if (p) CHECK_EQ((uint8_t)validate_bug(*p), (uint8_t)VR_OK);
  }
}
