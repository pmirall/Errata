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
