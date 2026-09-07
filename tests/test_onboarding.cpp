// =============================================================================
//  Pebblebol host tests - test_onboarding.cpp
//  app/onboarding.cpp: WHICH first-boot question a boot resumes at.
//
//  THE PROPERTY THAT NEEDS A REAL SAVE PIPELINE, AND WHY IT IS THIS BINARY.
//  The step is two bits of Config.flags, and Config is not what reaches flash:
//  persistence/game_state.cpp maps it to and from ConfigV2, save_manager.cpp
//  writes the pair, and gs_load() reads it back. A test that set the bits in a
//  struct and read them out of the same struct would prove the shift and say
//  NOTHING about whether the answer survives a power cut - which is the entire
//  requirement. That is the phase-7 defect exactly: a fifteen-point power-cut
//  sweep against a save_manager the release artefact does not execute.
//
//  So every case here runs the REAL gs_save_cfg() into the fake NVS and the
//  REAL gs_load() back out.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "app/onboarding.h"
#include "data/species_table.h"
#include "fakes/kv_mem.h"
#include "fakes/boot_host.h"
#include "persistence/game_state.h"
#include "persistence/save_manager.h"

static uint32_t s_ms    = 0;
static uint32_t s_epoch = 1700300000u;
static uint32_t fake_ms(void)    { return s_ms; }
static uint32_t fake_epoch(void) { return s_epoch; }

static void begin(void) {
  kv_mem_reset();
  boot_host_reset();
  s_ms    = 10000;
  s_epoch = 1700300000u;
  save_set_clock(&fake_ms, &fake_epoch);
  gs_set_readonly(false);
}

// =============================================================================
//  1. THE STEP FIELD
// =============================================================================
TEST(an_unset_field_means_finished_and_that_is_what_protects_every_old_save) {
  Config c;
  gs_cfg_defaults(c);
  // A Config that has never heard of this feature - which is every save written
  // before P10-C4, and gs_cfg_defaults()'s own memset.
  CHECK_EQ((int)ob_step(c), (int)OB_DONE);
  CHECK(!ob_active(ob_step(c)));
  // AND THE BOOT RULE AGREES: a device with a save is asked nothing.
  CHECK_EQ((int)ob_boot_step(false, false, c), (int)OB_DONE);
}

TEST(the_step_survives_every_other_flag_in_the_byte) {
  Config c;
  gs_cfg_defaults(c);
  for (uint8_t step = 0; step < (uint8_t)OB_STEP_COUNT; ++step) {
    for (uint16_t other = 0; other < 64u; ++other) {
      c.flags = (uint8_t)(other & 0x3Fu);        // every bit below the field
      ob_set_step(c, step);
      CHECK_EQ((int)ob_step(c), (int)step);
      CHECK_EQ((int)(c.flags & 0x3Fu), (int)(other & 0x3Fu));
    }
  }
  // An out-of-range step is refused rather than stored: three quarters of the
  // values the two bits can hold are legal, and the fourth would decode as a
  // screen that does not exist.
  ob_set_step(c, 99u);
  CHECK_EQ((int)ob_step(c), (int)OB_DONE);
}

TEST(the_flow_is_name_then_time_then_starter_and_then_it_is_over) {
  CHECK_EQ((int)ob_next(OB_NAME),    (int)OB_TIME);
  CHECK_EQ((int)ob_next(OB_TIME),    (int)OB_STARTER);
  CHECK_EQ((int)ob_next(OB_STARTER), (int)OB_DONE);
  // "What follows finished" must not be "start again".
  CHECK_EQ((int)ob_next(OB_DONE),    (int)OB_DONE);

  CHECK(ob_screen_for(OB_NAME)    == SCR_SETUP_NAME);
  CHECK(ob_screen_for(OB_TIME)    == SCR_TIME);     // the P2-C6 screen, reused
  CHECK(ob_screen_for(OB_STARTER) == SCR_SETUP_STARTER);
  CHECK(ob_screen_for(OB_DONE)    == SCR_HOME);
  CHECK(ob_screen_for(99u)        == SCR_HOME);
}

// =============================================================================
//  2. THE BOOT RULE
// =============================================================================
TEST(a_fresh_device_is_asked_and_a_device_with_a_save_is_not) {
  Config c;
  gs_cfg_defaults(c);

  // FIRST RUN: asked, whatever the (default) field says.
  CHECK_EQ((int)ob_boot_step(true, false, c), (int)OB_NAME);

  // NOT a first run and nothing recorded: never asked. This is the case that
  // protects a device somebody has been playing for months.
  CHECK_EQ((int)ob_boot_step(false, false, c), (int)OB_DONE);

  // READ-ONLY beats both. A session that cannot save cannot keep one answer,
  // so asking three questions would be three answers thrown away on top of the
  // save error the user is about to be shown.
  ob_set_step(c, OB_TIME);
  CHECK_EQ((int)ob_boot_step(false, true, c), (int)OB_DONE);
  CHECK_EQ((int)ob_boot_step(true,  true, c), (int)OB_DONE);
}

// THE ORDER OF THE TWO RULES, NAMED - and this case exists because the
// mutation that reverses them did NOT fire without it.
//
// Reverting ob_boot_step() to "ask the boot kind first" left every other case
// in this file green, because they all pass first_run = false. The two rules
// only disagree when a save EXISTS and hardware/boot.cpp still calls the boot a
// first run - and that is reachable: boot_note_save() takes gs_have_pebble(),
// so a Box that came back with no Pebble in it (a slot write that did not land)
// makes the verdict BOOT_FIRST_RUN while the CONFIG, and every answer already
// typed into it, survived intact. Under the old order the player is asked their
// name again with their name already stored.
TEST(a_stored_step_outranks_the_boot_kind_and_that_is_the_whole_ordering) {
  Config c;
  gs_cfg_defaults(c);
  const char nm[] = "PACO";
  memcpy(c.pet_name, nm, sizeof nm);
  ob_set_step(c, OB_STARTER);

  // first_run TRUE and a stored step: resume where the player was, do not
  // restart the flow on top of answers that are already on the device.
  CHECK_EQ((int)ob_boot_step(true, false, c), (int)OB_STARTER);
  // ...and with nothing stored, a first run still starts at the beginning.
  ob_set_step(c, OB_DONE);
  CHECK_EQ((int)ob_boot_step(true, false, c), (int)OB_NAME);
  // read-only still beats both, whatever is stored.
  ob_set_step(c, OB_STARTER);
  CHECK_EQ((int)ob_boot_step(true, true, c), (int)OB_DONE);
}

// =============================================================================
//  3. THE POWER CUT - the requirement, driven through the real save pipeline
// =============================================================================
TEST(a_power_cut_halfway_through_setup_resumes_at_the_step_it_reached) {
  for (uint8_t step = (uint8_t)OB_NAME; step <= (uint8_t)OB_STARTER; ++step) {
    begin();

    // --- boot 1: a genuinely fresh device -----------------------------------
    Config c;
    LoadResult r = gs_load(c);
    CHECK_EQ((int)r, (int)LOAD_FRESH);
    CHECK_EQ((int)ob_boot_step(true, false, c), (int)OB_NAME);

    // The player answers up to `step`, and app/app.cpp stamps the step it is
    // standing on before each question. The last write is the one a power cut
    // catches.
    ob_set_step(c, step);
    if (step > (uint8_t)OB_NAME) {
      // ...and whatever had been answered so far is in the same blob.
      const char nm[] = "PACO";
      memcpy(c.pet_name, nm, sizeof nm);
    }
    // BOTH HALVES, which is what ui_setup_persist() writes and why it writes
    // two things. persistence/save_manager.cpp's load_all_inner() answers
    // LOAD_FRESH the moment the Box pair is missing and returns BEFORE it looks
    // at the config, so a config written on its own is a config thrown away.
    // The case below this one drives exactly that.
    CHECK(gs_save_cfg(c));
    CHECK(gs_save_box());

    // --- the power cut. Nothing else is written; the RAM copy is gone. ------
    Config after;
    memset(&after, 0xAA, sizeof after);

    // --- boot 2: there IS a save now, so this is NOT a first run ------------
    // The old rule - `boot == BOOT_FIRST_RUN` - answered "ask nothing" here,
    // and that is the whole defect: the remaining questions were never asked
    // and the answers already given decided nothing.
    r = gs_load(after);
    CHECK_EQ((int)r, (int)LOAD_OK);
    CHECK_EQ((int)ob_boot_step(false, false, after), (int)step);
    CHECK(ob_active(ob_boot_step(false, false, after)));
    if (step > (uint8_t)OB_NAME) CHECK_EQ(memcmp(after.pet_name, "PACO", 5), 0);

    // --- and finishing it makes it stay finished ----------------------------
    ob_set_step(after, OB_DONE);
    CHECK(gs_save_cfg(after));
    Config third;
    CHECK_EQ((int)gs_load(third), (int)LOAD_OK);
    CHECK_EQ((int)ob_boot_step(false, false, third), (int)OB_DONE);
    if (step > (uint8_t)OB_NAME) CHECK_EQ(memcmp(third.pet_name, "PACO", 5), 0);
  }
}

// The other half of the same claim, and it is the one a mutation can hide
// behind: the step has to travel through ConfigV2's OWN flag word. Writing it
// into Config and reading it out of Config would pass with the mapping deleted.
TEST(the_step_travels_through_the_v2_flag_word_and_not_just_through_ram) {
  begin();
  Config c;
  CHECK_EQ((int)gs_load(c), (int)LOAD_FRESH);
  ob_set_step(c, OB_STARTER);
  c.flags |= CF_MUTE;                    // a neighbour in the same byte
  CHECK(gs_save_cfg(c));
  CHECK(gs_save_box());

  // Read the stored blob back as BYTES and find the field where the schema says
  // it is, rather than trusting the loader that wrote it.
  uint8_t blob[sizeof(ConfigV2)];
  size_t  n = sizeof blob;
  CHECK(kv_get(KV_MAIN, "cfg0", blob, n) || kv_get(KV_MAIN, "cfg1", blob, n));
  const ConfigV2* v2 = (const ConfigV2*)(const void*)blob;
  CHECK_EQ((int)((v2->flags & CFGV2_F_SETUP_MASK) >> CFGV2_F_SETUP_SH),
           (int)OB_STARTER);
  CHECK((v2->flags & CFGV2_F_MUTE) != 0u);

  Config back;
  CHECK_EQ((int)gs_load(back), (int)LOAD_OK);
  CHECK_EQ((int)ob_step(back), (int)OB_STARTER);
  CHECK((back.flags & CF_MUTE) != 0u);
}

// =============================================================================
//  4. THE TRIO
// =============================================================================
TEST(the_three_starters_are_three_real_and_different_creatures) {
  CHECK_EQ((int)OB_STARTER_COUNT, 3);
  uint8_t seen[OB_STARTER_COUNT];
  for (uint8_t i = 0; i < (uint8_t)OB_STARTER_COUNT; ++i) {
    const uint8_t sp = ob_starter_species(i);
    seen[i] = sp;
    CHECK(sp >= (uint8_t)SPECIES_ID_MIN);
    CHECK(sp <= (uint8_t)SPECIES_TABLE_COUNT);
    const SpeciesDef& d = SPECIES_TABLE[sp - 1];
    CHECK_EQ((int)d.id, (int)sp);
    CHECK_EQ((int)d.stage, 0);                       // a starter is a baby
    CHECK(d.evo_rule != (uint8_t)SPECIES_EVO_NONE);  // and it grows up
    for (uint8_t k = 0; k < i; ++k) CHECK(seen[k] != sp);
  }
  // One per corner of the three-cornered type chart, so the choice is a choice.
  CHECK_EQ((int)SPECIES_TABLE[ob_starter_species(0) - 1].type, (int)TYPE_SIGNAL);
  CHECK_EQ((int)SPECIES_TABLE[ob_starter_species(1) - 1].type, (int)TYPE_CORRUPT);
  CHECK_EQ((int)SPECIES_TABLE[ob_starter_species(2) - 1].type, (int)TYPE_SYSTEM);
  // A PLAYER WHO CHOOSES NOTHING KEEPS THE PEBBLE EVERY EARLIER DEVICE HAD.
  CHECK_EQ((int)ob_starter_species(0), (int)SPECIES_ID_STARTER);
  // Out of range answers 0, which box_new_pebble() and box_reroll_starter()
  // both refuse, rather than answering species 1 and quietly minting one.
  CHECK_EQ((int)ob_starter_species((uint8_t)OB_STARTER_COUNT), 0);
  CHECK_EQ((int)ob_starter_species(0xFF), 0);
}

// THE OTHER SIDE OF THE SAME COIN, and it is the storage telling the truth
// rather than a hole. If the power goes before ANYTHING reached flash - no
// config pair, no Box pair - the next boot is a genuinely fresh device: there
// is no name to keep and no step to resume, and the flow starts over. Pinned
// because "resumes where it left off" and "starts over when nothing was
// written" are two different promises and only one of them is about a bug.
TEST(a_power_cut_before_anything_reached_flash_starts_over) {
  begin();
  Config c;
  CHECK_EQ((int)gs_load(c), (int)LOAD_FRESH);
  CHECK_EQ((int)ob_boot_step(true, false, c), (int)OB_NAME);

  // The player types a name; nothing is persisted; the power goes.
  const char nm[] = "PACO";
  memcpy(c.pet_name, nm, sizeof nm);
  ob_set_step(c, OB_TIME);

  Config after;
  CHECK_EQ((int)gs_load(after), (int)LOAD_FRESH);
  CHECK_EQ((int)after.pet_name[0], 0);            // the name is gone with it
  CHECK_EQ((int)ob_boot_step(true, false, after), (int)OB_NAME);
}

// AND A CONFIG SAVED WITH NO BOX BESIDE IT IS THE SAME THING, which is the fact
// ui.h's ui_setup_persist() exists for and which nothing in this repository had
// ever stated. It is a property of persistence/save_manager.cpp, not of the
// flow, and it would silently undo the whole feature if the Box write were
// dropped from ui_setup_persist().
TEST(a_config_written_with_no_box_beside_it_is_discarded_by_the_loader) {
  begin();
  Config c;
  CHECK_EQ((int)gs_load(c), (int)LOAD_FRESH);
  ob_set_step(c, OB_STARTER);
  const char nm[] = "PACO";
  memcpy(c.pet_name, nm, sizeof nm);
  CHECK(gs_save_cfg(c));                          // the config alone

  Config after;
  CHECK_EQ((int)gs_load(after), (int)LOAD_FRESH); // no Box: nothing loadable
  CHECK_EQ((int)ob_step(after), (int)OB_DONE);    // the step did not come back
  CHECK_EQ((int)after.pet_name[0], 0);            // nor did the name

  // With the Box written too, the same config survives.
  begin();
  CHECK_EQ((int)gs_load(c), (int)LOAD_FRESH);
  ob_set_step(c, OB_STARTER);
  memcpy(c.pet_name, nm, sizeof nm);
  CHECK(gs_save_cfg(c));
  CHECK(gs_save_box());
  CHECK_EQ((int)gs_load(after), (int)LOAD_OK);
  CHECK_EQ((int)ob_step(after), (int)OB_STARTER);
  CHECK_EQ(memcmp(after.pet_name, "PACO", 5), 0);
}
