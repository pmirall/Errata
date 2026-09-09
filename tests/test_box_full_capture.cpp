// =============================================================================
//  ERRATA host test - test_box_full_capture.cpp
//  THE FULL-BOX BRANCH OF CAPTURE (spec section 23, section 68 r14, P5-C4).
//
//  Spec section 23: "if the Box is full, the player must decide whether to
//  release/replace; never silently discard", and "the encounter must never
//  delete the active Bug".
//
//  The plan asks that BOTH branches leave ten valid Bugs, and that is what
//  this file measures rather than asserts: after the refusal, and after the
//  release-then-retry, the Box holds ten Bugs and every one of them returns
//  VR_OK. Its own binary, linking what it drives and nothing else, for the
//  reason tests/test_battle_screen.cpp has its own.
//
//  THE ONE THAT COULD NOT FAIL AND IS WRITTEN SO IT CAN: a case that only
//  checked "the capture was refused" would pass against a capture that rolled,
//  failed and reported CAP_ESCAPED. So every refusal here asserts CAP_BOX_FULL
//  BY NAME, asserts that NO ATTEMPT WAS SPENT (st.attempts stays 0 - the item
//  and the attempt must survive a refusal the player did not cause), and
//  asserts the Box is byte-identical afterwards.
// =============================================================================
#include "nt_test.h"

#include <string.h>

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
  g.g0 = 0x2244u; g.g1 = 0x6688u; g.g2 = 0x0ACEu;
  g.generation = 1u;
  genome_seal(g);
  return g;
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

// Ten captured Bugs, filed by the real capture path so the fixture is the
// thing under test rather than a hand-built Box.
static void fill_box(void)
{
  memset(&g_gs, 0, sizeof g_gs);
  g_gs.box.magic           = (uint16_t)BOX_MAGIC;
  g_gs.box.schema_version  = (uint8_t)SAVE_SCHEMA_VERSION;
  g_gs.box.active_slot     = (uint8_t)BOX_ACTIVE_NONE;
  g_gs.box.next_id_counter = 1u;
  box_bind(g_gs);
  for (uint8_t k = 0; k < (uint8_t)BOX_SLOTS; ++k) {
    CaptureState st; cap_reset(st);
    CaptureReport rep;
    CHECK(cap_attempt(st, wild((uint8_t)(1u + k), (uint8_t)(6u + k)), 8, 0, 0u,
                      sealed_genome(0x4000u + k), 0xAB00u + k, 1700000000u, rep));
    CHECK_EQ(rep.outcome, (uint8_t)CAP_CAUGHT);
  }
  CHECK_EQ(box_count(), (uint8_t)BOX_SLOTS);
}

static void check_ten_valid(void)
{
  CHECK_EQ(box_count(), (uint8_t)BOX_SLOTS);
  int seen = 0;
  for (uint8_t s = 0; s < (uint8_t)BOX_SLOTS; ++s) {
    const BugInstance* p = box_peek(s);
    CHECK(p != nullptr);
    if (!p) continue;
    CHECK_EQ((uint8_t)validate_bug(*p), (uint8_t)VR_OK);
    CHECK(p->id != 0u);
    seen++;
  }
  CHECK_EQ(seen, (int)BOX_SLOTS);
  // Exactly one active, which is the Box's invariant B3 and the thing a
  // refusal-and-retry path could quietly break.
  CHECK(box_active() < (uint8_t)BOX_SLOTS);
}

// =============================================================================
//  BRANCH 1: THE REFUSAL. The player is asked; nothing is discarded.
// =============================================================================
TEST(a_full_box_refuses_by_name_spends_no_attempt_and_changes_nothing) {
  fill_box();
  // A byte-for-byte snapshot: the strongest form of "nothing was discarded".
  GameState before;
  memcpy(&before, &g_gs, sizeof before);
  const uint8_t active_before = box_active();

  CaptureState st; cap_reset(st);
  CaptureReport rep;
  const uint16_t chance = cap_chance_permille(1, 8, 8, 0);
  // A roll that WOULD have caught: the refusal must come from the Box being
  // full and not from a lost roll, which is why the roll is the winning one.
  CHECK(!cap_attempt(st, wild(1, 8), 8, 0, (uint32_t)(chance - 1u),
                     sealed_genome(99u), 5u, 1700000000u, rep));
  CHECK_EQ(rep.outcome, (uint8_t)CAP_BOX_FULL);
  CHECK_EQ(rep.slot, (uint8_t)BOX_SLOT_NONE);
  // THE ATTEMPT IS NOT SPENT. A player who cannot store a creature has not
  // failed to catch it, and the capture item they were about to use is theirs.
  CHECK_EQ(st.attempts, 0);
  CHECK_EQ(st.fled, 0);
  CHECK_EQ(rep.attempts_left, (uint8_t)CAPTURE_MAX_ATTEMPTS);
  // Nothing moved at all.
  CHECK_EQ(memcmp(&before, &g_gs, sizeof before), 0);
  CHECK_EQ(box_active(), active_before);
  check_ten_valid();

  // ...and it stays refused however many times the button is pressed.
  for (int i = 0; i < 5; ++i) {
    CHECK(!cap_attempt(st, wild(1, 8), 8, 0, (uint32_t)(chance - 1u),
                       sealed_genome(99u), 5u, 1700000000u, rep));
    CHECK_EQ(rep.outcome, (uint8_t)CAP_BOX_FULL);
  }
  CHECK_EQ(memcmp(&before, &g_gs, sizeof before), 0);
  check_ten_valid();
}

// =============================================================================
//  BRANCH 2: THE PLAYER FREES A SLOT AND THE SAME ENCOUNTER LANDS.
// =============================================================================
TEST(releasing_a_stored_slot_lets_the_same_encounter_land_and_leaves_ten_valid) {
  fill_box();
  const uint8_t active = box_active();
  // Pick a slot that is NOT the active one - releasing the active one is
  // refused by invariant B4, which is the next case.
  uint8_t victim = (uint8_t)((active == 0u) ? 1u : 0u);
  const uint32_t victim_id = box_peek(victim)->id;

  CHECK(box_release(victim, true));
  CHECK_EQ(box_count(), (uint8_t)(BOX_SLOTS - 1));
  CHECK_EQ(box_active(), active);            // the active Bug is untouched

  CaptureState st; cap_reset(st);
  CaptureReport rep;
  const uint16_t chance = cap_chance_permille(2, 9, 8, 0);
  CHECK(cap_attempt(st, wild(2, 9), 8, 0, (uint32_t)(chance - 1u),
                    sealed_genome(0x5150u), 77u, 1700000000u, rep));
  CHECK_EQ(rep.outcome, (uint8_t)CAP_CAUGHT);
  CHECK_EQ(rep.reject, (uint8_t)VR_OK);
  // BOTH BRANCHES LEAVE TEN VALID BUGS - the plan's own wording.
  check_ten_valid();
  // The new one is genuinely new, and the released one is genuinely gone.
  CHECK(box_peek(rep.slot)->id != victim_id);
  for (uint8_t s = 0; s < (uint8_t)BOX_SLOTS; ++s)
    CHECK(box_peek(s)->id != victim_id);
  // The active Bug is STILL the one the player was carrying (section 23).
  CHECK_EQ(box_active(), active);
}

TEST(the_active_bug_can_never_be_the_slot_a_capture_frees) {
  fill_box();
  const uint8_t active = box_active();
  const uint32_t active_id = box_peek(active)->id;

  // B4: releasing the active slot is refused, confirmed or not. So the "free a
  // slot" flow can never take the creature the player is carrying, which is the
  // half of section 23 that is a SAFETY rule rather than a UI one.
  CHECK(!box_release(active, true));
  CHECK(!box_release(active, false));
  CHECK_EQ(box_count(), (uint8_t)BOX_SLOTS);
  CHECK_EQ(box_peek(active)->id, active_id);

  // And a refused capture on top of that leaves it where it was.
  CaptureState st; cap_reset(st);
  CaptureReport rep;
  CHECK(!cap_attempt(st, wild(1, 8), 8, 0, 0u, sealed_genome(1u), 1u, 1700000000u, rep));
  CHECK_EQ(rep.outcome, (uint8_t)CAP_BOX_FULL);
  CHECK_EQ(box_active(), active);
  CHECK_EQ(box_peek(active)->id, active_id);
  check_ten_valid();
}

TEST(a_full_box_is_refused_before_the_roll_so_a_capture_item_is_never_wasted) {
  // The ORDER matters and nothing else asserts it: if the roll came first, a
  // player at a full Box would lose an attempt (and, at the screen layer, the
  // capture chip that was armed) on a catch that could never land. Measured by
  // driving a LOSING roll: even that must not advance the attempt counter,
  // which it would if the roll ran first.
  fill_box();
  CaptureState st; cap_reset(st);
  CaptureReport rep;
  CHECK(!cap_attempt(st, wild(1, 8), 8, 5, 999u, sealed_genome(2u), 1u, 1700000000u, rep));
  CHECK_EQ(rep.outcome, (uint8_t)CAP_BOX_FULL);
  CHECK_EQ(st.attempts, 0);
  CHECK_EQ(rep.chance, 0);        // no chance was even computed
  CHECK_EQ(rep.roll, 0);

  // POSITIVE CONTROL: free a slot and the identical losing roll DOES spend an
  // attempt, so the case above measured the order and not a dead code path.
  const uint8_t active = box_active();
  const uint8_t victim = (uint8_t)((active == 0u) ? 1u : 0u);
  CHECK(box_release(victim, true));
  CHECK(!cap_attempt(st, wild(1, 8), 8, 5, 999u, sealed_genome(2u), 1u, 1700000000u, rep));
  CHECK_EQ(rep.outcome, (uint8_t)CAP_ESCAPED);
  CHECK_EQ(st.attempts, 1);
  CHECK(rep.chance > 0);
}
