// =============================================================================
//  Pebblebol host tests - test_box_persist.cpp
//
//  THE SEAM BETWEEN game/box.cpp AND persistence/game_state.cpp.
//
//  game/box.h states the contract in its own words: "the Box does no I/O of its
//  own - persistence decides when a mutated slot reaches flash". Until this
//  file existed, NO host binary linked those two translation units together, so
//  neither half of that sentence was ever held to account: test_box proved the
//  RAM mutation, test_game_state proved the flash round trip, and the question
//  the player actually cares about - "does the creature I just caught survive a
//  power cut" - was asked by nothing.
//
//  It shipped a defect. ui/screen_encounter.cpp's throw_once() minted a
//  captured Pebble through box_new_pebble() (RAM only, by design) and then
//  committed with ui_explore_commit(), which writes cooldowns, inventory and
//  gs_save_active() - and gs_save_active() only ever writes the slot
//  box.active_slot names. A capture lands in first_free(), which is the active
//  slot only when the Box was empty. Every creature caught after the first was
//  gone at the next brown-out.
//
//  So the shape of every case here is the same and it is deliberately blunt:
//  mutate the Box the way a screen does, run the commit that screen runs,
//  service the writer, then REBOOT into a fresh GameState and ask whether the
//  animal is still there.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "fakes/kv_mem.h"
#include "fakes/boot_host.h"
#include "game/box.h"
#include "data/encounter_table.h"   // ENC_OUT_WILD
#include "game/capture.h"
#include "game/genome.h"
#include "game/encounters.h"
#include "persistence/game_state.h"
#include "persistence/save_manager.h"

static uint32_t s_ms    = 0;
static uint32_t s_epoch = 1700300000u;
static uint32_t fake_ms(void)    { return s_ms; }
static uint32_t fake_epoch(void) { return s_epoch; }

// A REAL ms CLOCK, deliberately. save_manager.cpp's wear filter (SAVE_MIN_GAP_MS)
// is a branch the release artefact runs, and the phase-7 defect was a fixture
// that switched it off by binding a null millisecond clock. Every write below
// therefore advances s_ms past the gap the way a device does.
//
// THE PROSE IS WORDED AROUND THE GATE ON PURPOSE. tools/check.sh's P7-C6 rule
// greps tests/ for that call with a null first argument and does NOT strip
// comments first, so a file that spelled the forbidden call out - even to
// explain why it is forbidden - would fail the build. Named here rather than
// worked around silently: a gate that a file documenting its own rule cannot
// satisfy is a gate that discourages writing the rule down, in a tree whose
// whole style is rules written down next to the code.
static void begin(void) {
  kv_mem_reset();
  boot_host_reset();
  s_ms    = 10000;
  s_epoch = 1700300000u;
  save_set_clock(&fake_ms, &fake_epoch);
  gs_set_readonly(false);
}

static void settle(void) {
  s_ms += 5000;            // clear of SAVE_MIN_GAP_MS
  save_service();
  s_ms += 5000;
  save_service();
}

// The first boot: an empty save, one starter minted into slot 0, active.
static void boot_with_starter(void) {
  begin();
  Config cfg;
  memset(&cfg, 0, sizeof cfg);
  CHECK_EQ((int)gs_load(cfg), (int)LOAD_FRESH);
  box_bind(gs_state());
  const uint8_t slot = box_new_pebble(1u, 3u, (uint8_t)ORIGIN_STARTER,
                                      Genome(), 0xC0FFEEu, s_epoch);
  CHECK_EQ((int)slot, 0);
  CHECK(box_set_active(0u));
  CHECK(gs_save_active(true));
  settle();
}

// Tears the world down and brings it back the way a power cut does: a fresh
// GameState, a fresh load from the same store.
static LoadResult reboot(void) {
  Config cfg;
  memset(&cfg, 0, sizeof cfg);
  const LoadResult r = gs_load(cfg);
  box_bind(gs_state());
  return r;
}

// =============================================================================
//  ANTI-VACUITY. If the starter itself does not survive, every case below is a
//  statement about a store that was never written.
// =============================================================================
TEST(the_starter_survives_a_reboot) {
  boot_with_starter();
  const uint32_t id = box_peek(0u)->id;
  CHECK(id != 0u);

  CHECK_EQ((int)reboot(), (int)LOAD_OK);
  CHECK(box_occupied(0u));
  CHECK_EQ((int)box_peek(0u)->species_id, 1);
  CHECK_EQ((unsigned long)box_peek(0u)->id, (unsigned long)id);
  CHECK_EQ((int)box_count(), 1);
}

// =============================================================================
//  THE CAPTURE. This is the case that was failing: a second Pebble minted into
//  a NON-ACTIVE slot and committed the way ui/screen_encounter.cpp commits.
// =============================================================================
TEST(a_captured_pebble_is_on_flash_before_the_next_power_cut) {
  boot_with_starter();

  const uint8_t slot = box_new_pebble(2u, 5u, (uint8_t)ORIGIN_WILD,
                                      Genome(), 0xBEEF01u, s_epoch);
  CHECK_EQ((int)slot, 1);                 // first_free(), not the active slot
  CHECK(slot != box_active());
  const uint32_t id = box_peek(slot)->id;

  // Exactly what the capture path commits: the slot that changed, then the
  // Box header, then the writer's own service pass.
  CHECK(gs_save_slot(slot, true));
  CHECK(gs_save_box());
  settle();

  CHECK_EQ((int)reboot(), (int)LOAD_OK);
  CHECK(box_occupied(slot));
  CHECK_EQ((int)box_peek(slot)->species_id, 2);
  CHECK_EQ((unsigned long)box_peek(slot)->id, (unsigned long)id);
  CHECK_EQ((int)box_count(), 2);
}

// The same thing through the real cap_attempt(), so the constructor under test
// is the one the firmware calls rather than a hand-rolled stand-in.
TEST(a_real_cap_attempt_files_a_pebble_that_survives) {
  boot_with_starter();

  // A wild encounter, minted the way ui/screen_encounter.cpp mints one.
  EncounterResult enc;
  memset(&enc, 0, sizeof enc);
  enc.outcome    = (uint8_t)ENC_OUT_WILD;
  enc.species_id = 3u;
  enc.level      = 4u;

  CaptureReport rep;
  uint8_t caught_slot = (uint8_t)BOX_SLOT_NONE;
  // The roll is random; sweep seeds until one lands, which also proves the
  // report's slot field is the one that carries the answer. cap_attempt() takes
  // the CaptureState by REFERENCE and counts failed attempts in it, so it is
  // reset each round - otherwise the creature flees at CAPTURE_MAX_ATTEMPTS and
  // every later seed is refused rather than rolled.
  for (uint32_t seed = 1u; seed < 4000u && caught_slot == (uint8_t)BOX_SLOT_NONE; ++seed) {
    CaptureState st;
    cap_reset(st);
    // genome_genesis(), NOT a default-constructed Genome: capture.cpp answers
    // CAP_BAD_GENOME *before* it rolls, so a zeroed genome refuses every seed
    // and the sweep would look like a 0 % capture rate rather than a bad input.
    const Genome g = genome_genesis();
    if (cap_attempt(st, enc, 5u, 0u, seed, g, 0xA5A5u + seed, s_epoch, rep)
        && rep.outcome == (uint8_t)CAP_CAUGHT) {
      caught_slot = rep.slot;
    }
  }
  CHECK(caught_slot != (uint8_t)BOX_SLOT_NONE);
  CHECK(caught_slot != box_active());
  const uint32_t id = box_peek(caught_slot)->id;

  CHECK(gs_save_slot(caught_slot, true));
  CHECK(gs_save_box());
  settle();

  CHECK_EQ((int)reboot(), (int)LOAD_OK);
  CHECK(box_occupied(caught_slot));
  CHECK_EQ((unsigned long)box_peek(caught_slot)->id, (unsigned long)id);
}

// =============================================================================
//  THE SIBLING MUTATIONS. These were already correct; they are here so the
//  seam has a row per Box door rather than a row for the one that broke.
// =============================================================================
TEST(a_release_removes_the_pebble_from_flash_too) {
  boot_with_starter();
  const uint8_t slot = box_new_pebble(2u, 5u, (uint8_t)ORIGIN_WILD,
                                      Genome(), 0xBEEF02u, s_epoch);
  CHECK(gs_save_slot(slot, true));
  CHECK(gs_save_box());
  settle();

  CHECK(box_release(slot, true));
  CHECK(gs_save_slot(slot, true));
  CHECK(gs_save_box());
  settle();

  CHECK_EQ((int)reboot(), (int)LOAD_OK);
  CHECK(!box_occupied(slot));
  CHECK_EQ((int)box_count(), 1);
}

TEST(activating_the_other_slot_survives_a_reboot) {
  boot_with_starter();
  const uint8_t slot = box_new_pebble(2u, 5u, (uint8_t)ORIGIN_WILD,
                                      Genome(), 0xBEEF03u, s_epoch);
  CHECK(gs_save_slot(slot, true));
  settle();

  CHECK(box_set_active(slot));
  CHECK(gs_save_box());
  settle();

  CHECK_EQ((int)reboot(), (int)LOAD_OK);
  CHECK_EQ((int)box_active(), (int)slot);
  CHECK_EQ((int)box_count(), 2);
}

TEST(a_swap_moves_both_slots_on_flash) {
  boot_with_starter();
  const uint8_t slot = box_new_pebble(2u, 5u, (uint8_t)ORIGIN_WILD,
                                      Genome(), 0xBEEF04u, s_epoch);
  CHECK(gs_save_slot(slot, true));
  settle();
  const uint32_t id0 = box_peek(0u)->id;
  const uint32_t id1 = box_peek(slot)->id;

  CHECK(box_swap(0u, slot));
  CHECK(gs_save_slot(0u, true));
  CHECK(gs_save_slot(slot, true));
  CHECK(gs_save_box());
  settle();

  CHECK_EQ((int)reboot(), (int)LOAD_OK);
  CHECK_EQ((unsigned long)box_peek(0u)->id, (unsigned long)id1);
  CHECK_EQ((unsigned long)box_peek(slot)->id, (unsigned long)id0);
}

// =============================================================================
//  THE CONTRACT ITSELF. gs_save_active() writes the slot box.active_slot names
//  and NOTHING ELSE - that is what game_state.cpp's own comment says, and it is
//  the sentence the capture defect walked past. Pinned here so the next person
//  who reaches for gs_save_active() as a general-purpose flush is told no.
// =============================================================================
TEST(gs_save_active_does_not_write_the_other_slots) {
  boot_with_starter();
  const uint8_t slot = box_new_pebble(2u, 5u, (uint8_t)ORIGIN_WILD,
                                      Genome(), 0xBEEF05u, s_epoch);
  CHECK_EQ((int)slot, 1);
  CHECK(slot != box_active());

  CHECK(gs_save_active(true));    // ALL the persistence the old commit ran
  settle();

  // THE SAVE IS LEFT INCONSISTENT, AND THIS IS WORSE THAN "THE CREATURE IS
  // LOST". gs_save_active() writes the active slot's blob AND THEN mirrors the
  // Box header through gs_save_box() - and mask_sync() had already put the new
  // slot into box.slot_mask when box_new_pebble() filed it. So flash ends up
  // with a header claiming a slot whose blob was never written, and the next
  // boot does not come back clean: it reports LOAD_RECOVERED_PAIR, having
  // served the older copy of something to reconcile the two.
  const int lr = (int)reboot();
  CHECK(lr != (int)LOAD_OK);
  CHECK_EQ(lr, (int)LOAD_RECOVERED_PAIR);
  // And the creature is gone either way, which is what the player sees.
  CHECK(!box_occupied(slot));
  CHECK_EQ((int)box_count(), 1);
}

// =============================================================================
//  A FACTORY RESET LEAVES A PEBBLE THAT IS ACTUALLY IN THE BOX
//  Added at the FINAL REVIEW. It is the SAME CLASS as the capture defect above,
//  one door along, and it had been standing since the reset was written.
//
//  Both wipe paths - ui/ui.cpp's CFM_WIPE2 ("Reset de fabrica", the button an
//  owner presses) and dev/godmode.cpp's run_wipe() ("BORRAR TODO") - did:
//
//      gs_factory_reset();          // memsets the Box: slot_mask 0, active 255
//      Genome g = genome_genesis();
//      sim_new_pet(g, gt_now(), 0); // writes an egg into the BOUND instance
//      if (pet()) gs_save_active(true);            // ...and the bool is dropped
//
//  sim_new_pet() is not a Box constructor - it mints no slot - so gs_save_active()
//  returned false at its `act >= BOX_SLOTS` guard and the verdict was discarded.
//  Measured against these same objects before the fix: a fully playable creature
//  with box_count() 0, active_slot 255, gs_readonly() false, and EVERY SAVE FROM
//  THAT MOMENT A SILENT NO-OP - so the next power cut lost everything since the
//  reset and re-ran the first-boot wizard, while `show_save` reported
//  "slots=0x0000 active=255 count=0/10" under a live pet on the panel.
//
//  Neither wipe path is compiled by any host binary (ui/ui.cpp and
//  dev/godmode.cpp are both in the never-compiled set), so this case pins the
//  SEQUENCE they must both use, and tools/check.sh gates that each body uses it.
//  game/box.h calls box_new_pebble() "THE TREE'S ONE CONSTRUCTOR"; this is what
//  goes wrong when a caller goes round it.
// =============================================================================
TEST(a_factory_reset_leaves_a_starter_the_save_manager_can_actually_write) {
  boot_with_starter();
  // Earn something, so a lost reset is distinguishable from a lost boot.
  box_slot(0u)->level = 7u;
  CHECK(gs_save_active(true));
  settle();

  // --- the wipe, exactly as both shipping call sites now do it -------------
  CHECK(gs_factory_reset());
  CHECK_EQ((int)box_count(), 0);                    // the Box really is empty
  CHECK(box_active() >= (uint8_t)BOX_SLOTS);        // ...and has no active slot

  // THE DEFECT, DIRECTLY: with no slot minted, the save the old code relied on
  // cannot land. If this ever starts returning true the guard has moved and the
  // rest of this case stops meaning anything.
  CHECK(!gs_save_active(true));

  const uint8_t sl = box_new_pebble(1u, 1u, (uint8_t)ORIGIN_STARTER,
                                    genome_genesis(), 0xC0DE01u, s_epoch);
  CHECK(sl != (uint8_t)BOX_SLOT_NONE);
  CHECK(box_set_active(sl));
  CHECK(box_slot(sl) != nullptr);

  // Now it can, and the verdict must be checked rather than discarded.
  CHECK(gs_save_active(true));
  CHECK(gs_save_box());
  settle();

  // --- and it is there after the power cut ---------------------------------
  CHECK_EQ((int)reboot(), (int)LOAD_OK);
  CHECK_EQ((int)box_count(), 1);
  CHECK(box_occupied(sl));
  CHECK_EQ((int)box_active(), (int)sl);
  CHECK_EQ((int)box_peek(sl)->species_id, 1);
  CHECK_EQ((int)box_peek(sl)->level, 1);            // the wiped one, not the level 7
}
