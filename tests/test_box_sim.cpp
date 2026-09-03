// =============================================================================
//  Pebblebol host tests - test_box_sim.cpp
//  game/box_sim.cpp: the ONE place that knows both that the Box stores Pebbles
//  BY VALUE and that game/sim.cpp holds a raw pointer into that array.
//
//  The defect this file exists for: box_swap() exchanges two slots' CONTENTS
//  and carries box.active_slot with the creature, so a swap that touches the
//  active slot leaves sim bound to the OTHER Pebble - the player keeps petting,
//  feeding and saving the creature that moved into the slot they left, with the
//  accumulators of the one they thought they were carrying. Every check below
//  runs against the real box.cpp and the real sim.cpp; nothing here is stubbed.
// =============================================================================
#include "nt_test.h"

#include <stdio.h>
#include <string.h>

#include "core/config.h"
#include "data/species_table.h"
#include "game/box.h"
#include "game/box_sim.h"
#include "game/sim.h"
#include "persistence/save_schema.h"

#define BS_EPOCH0  1700000000u

static GameState g_state;

static Genome zero_genome(void) {
  Genome g;
  memset(&g, 0, sizeof g);
  return g;
}

// A bound Box holding `n` Pebbles, each one a CHILD (level 5) so the stage is
// not EGG, with slot 0 active and the simulation bound to it.
static void fixture(uint8_t n) {
  memset(&g_state, 0, sizeof g_state);
  for (uint8_t i = 0; i < (uint8_t)BOX_SLOTS; ++i) {
    g_state.pebbles[i].magic      = (uint16_t)PEBBLE_MAGIC;
    g_state.pebbles[i].layout_ver = (uint8_t)PEBBLE_LAYOUT_VER;
  }
  g_state.box.magic           = (uint16_t)BOX_MAGIC;
  g_state.box.active_slot     = (uint8_t)BOX_ACTIVE_NONE;
  g_state.box.next_id_counter = 1;
  g_state.cfg.device_id       = 0xB0FFE501u;
  box_bind(g_state);

  for (uint8_t i = 0; i < n; ++i) {
    const uint8_t s = box_new_pebble(SPECIES_ID_STARTER, 5, (uint8_t)ORIGIN_WILD,
                                     zero_genome(), 0x2000u + i, BS_EPOCH0);
    CHECK(s != (uint8_t)BOX_SLOT_NONE);
    PebbleInstance* p = box_slot(s);
    CHECK(p != nullptr);
    snprintf(p->nickname, sizeof p->nickname, "P%u", (unsigned)i);
  }
  CHECK(box_set_active(0));
  sim_bind(*box_slot(0));
}

// -----------------------------------------------------------------------------
//  THE DEFECT. Swapping the slot you are carrying must not change WHO you are
//  carrying - only where that creature is filed.
// -----------------------------------------------------------------------------
TEST(swapping_the_active_slot_keeps_the_simulation_on_the_same_creature) {
  fixture(4);

  const uint32_t carried = box_slot(0)->id;
  const uint32_t stored3 = box_slot(3)->id;
  CHECK(carried != stored3);

  // Two numbers that only exist while this creature is the bound one, set to
  // values no reset would produce: bond lives in sim's RAM (derive_view() puts
  // it back to 50 %) and hunger lives in the PebbleInstance.
  sim_god_set_stat(ST_BOND, 90);
  sim_god_set_stat(ST_HUNGER, 42);
  const int16_t cq_before = sim_view()->cq;

  CHECK(box_sim_swap(0, 3));

  // box.cpp's half: the creature moved and the active INDEX followed it.
  CHECK_EQ((int)box_active(), 3);
  CHECK_EQ((unsigned)box_slot(3)->id, (unsigned)carried);
  CHECK_EQ((unsigned)box_slot(0)->id, (unsigned)stored3);

  // box_sim.cpp's half: sim followed it too. This is the assertion that fails
  // when ui_box_swap() calls box_swap() on its own.
  CHECK(sim_pebble() == box_slot(box_active()));
  CHECK_EQ((unsigned)sim_pebble()->id, (unsigned)carried);

  // A rebind is an address correction, NOT a sim_switch(): not one per-Pebble
  // accumulator may be reset by it.
  CHECK_EQ((int)sim_stat_pct(ST_BOND), 90);
  CHECK_EQ((int)sim_stat_pct(ST_HUNGER), 42);
  CHECK_EQ((int)sim_view()->cq, (int)cq_before);

  // And the simulation now writes into the slot the Box calls active, which is
  // the slot persistence saves.
  sim_god_set_stat(ST_HUNGER, 17);
  CHECK_EQ((int)box_slot(3)->care[0], 17000);
  CHECK_EQ((int)box_slot(0)->care[0], 100000);   // the untouched stored one
}

// The mirror case: the same swap written the other way round has to behave
// identically, because box_swap() itself is symmetric.
TEST(the_swap_is_symmetric_in_its_arguments) {
  fixture(4);
  const uint32_t carried = box_slot(0)->id;

  CHECK(box_sim_swap(3, 0));
  CHECK_EQ((int)box_active(), 3);
  CHECK(sim_pebble() == box_slot(3));
  CHECK_EQ((unsigned)sim_pebble()->id, (unsigned)carried);
}

// -----------------------------------------------------------------------------
//  The cases that must NOT move the binding.
// -----------------------------------------------------------------------------
TEST(a_swap_of_two_stored_slots_leaves_the_binding_alone) {
  fixture(4);
  const PebbleInstance* before = sim_pebble();
  const uint32_t id1 = box_slot(1)->id;
  const uint32_t id2 = box_slot(2)->id;

  CHECK(box_sim_swap(1, 2));
  CHECK_EQ((int)box_active(), 0);
  CHECK(sim_pebble() == before);
  CHECK(sim_pebble() == box_slot(0));
  CHECK_EQ((unsigned)box_slot(1)->id, (unsigned)id2);
  CHECK_EQ((unsigned)box_slot(2)->id, (unsigned)id1);
}

TEST(swapping_a_slot_with_itself_changes_nothing) {
  fixture(4);
  const PebbleInstance* before = sim_pebble();
  const uint32_t id0 = box_slot(0)->id;

  CHECK(box_sim_swap(0, 0));
  CHECK_EQ((int)box_active(), 0);
  CHECK(sim_pebble() == before);
  CHECK_EQ((unsigned)box_slot(0)->id, (unsigned)id0);
}

TEST(a_refused_swap_reports_false_and_touches_nothing) {
  fixture(4);
  const PebbleInstance* before = sim_pebble();

  CHECK(!box_sim_swap(0, (uint8_t)BOX_SLOTS));
  CHECK(!box_sim_swap((uint8_t)BOX_SLOTS, 0));
  CHECK(!box_sim_swap(0, 0xFFu));
  CHECK_EQ((int)box_active(), 0);
  CHECK(sim_pebble() == before);
}

// An empty target slot is still a legal swap: it is how the player moves a
// Pebble up the list. The binding has to follow it there too.
TEST(swapping_the_active_slot_onto_an_empty_one_still_follows_it) {
  fixture(2);
  const uint32_t carried = box_slot(0)->id;
  sim_god_set_stat(ST_BOND, 77);

  CHECK(box_sim_swap(0, 7));
  CHECK_EQ((int)box_active(), 7);
  CHECK(box_occupied(7));
  CHECK(!box_occupied(0));
  CHECK(sim_pebble() == box_slot(7));
  CHECK_EQ((unsigned)sim_pebble()->id, (unsigned)carried);
  CHECK_EQ((int)sim_stat_pct(ST_BOND), 77);
}

// -----------------------------------------------------------------------------
//  sim_rebind() on its own: the guards its contract promises.
// -----------------------------------------------------------------------------
TEST(sim_rebind_is_not_sim_switch) {
  fixture(4);
  sim_god_set_stat(ST_BOND, 90);

  // Rebinding onto the SAME address is a no-op, and rebinding onto another one
  // keeps every accumulator - which is exactly what tells the two apart:
  // sim_switch() below puts bond back to its 50 % default.
  sim_rebind(*box_slot(0));
  CHECK_EQ((int)sim_stat_pct(ST_BOND), 90);

  sim_rebind(*box_slot(1));
  CHECK(sim_pebble() == box_slot(1));
  CHECK_EQ((int)sim_stat_pct(ST_BOND), 90);

  sim_switch(*box_slot(2));
  CHECK(sim_pebble() == box_slot(2));
  CHECK_EQ((int)sim_stat_pct(ST_BOND), 50);
}
