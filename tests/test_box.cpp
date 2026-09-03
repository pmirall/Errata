// =============================================================================
//  Pebblebol host tests - test_box.cpp
//  The five Box invariants of game/box.h (plan P2-C10, spec section 9):
//    B1 capacity 10          B2 unique non-zero ids      B3 exactly one active
//    B4 release is explicit and never the active slot
//    B5 recovery caps at 100 % and never touches xp / level
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "core/config.h"
#include "data/balance.h"
#include "game/box.h"
#include "data/species_table.h"
#include "persistence/save_schema.h"

#define BOX_EPOCH0  1700000000u

static GameState g_state;

static Genome zero_genome(void) {
  Genome g;
  memset(&g, 0, sizeof g);
  return g;
}

// A bound, empty Box on a device with a real identity.
static void box_fixture(void) {
  memset(&g_state, 0, sizeof g_state);
  for (uint8_t i = 0; i < (uint8_t)BOX_SLOTS; ++i) {
    g_state.pebbles[i].magic      = (uint16_t)PEBBLE_MAGIC;
    g_state.pebbles[i].layout_ver = (uint8_t)PEBBLE_LAYOUT_VER;
  }
  g_state.box.magic          = (uint16_t)BOX_MAGIC;
  g_state.box.active_slot    = (uint8_t)BOX_ACTIVE_NONE;
  g_state.box.next_id_counter = 1;
  g_state.cfg.device_id      = 0xB0FFE501u;
  box_bind(g_state);
}

static uint8_t fill(uint8_t n) {
  uint8_t made = 0;
  for (uint8_t i = 0; i < n; ++i) {
    if (box_new_pebble(SPECIES_ID_STARTER, 1, (uint8_t)ORIGIN_WILD,
                       zero_genome(), 0x1000u + i,
                       BOX_EPOCH0) != (uint8_t)BOX_SLOT_NONE) made++;
  }
  return made;
}

// -----------------------------------------------------------------------------
//  B1 - ten slots and no more
// -----------------------------------------------------------------------------
TEST(box_capacity_is_ten_and_the_eleventh_is_refused) {
  box_fixture();
  CHECK_EQ((int)box_capacity(), (int)BOX_SLOTS);
  CHECK_EQ((int)box_count(), 0);
  CHECK_EQ((int)box_active(), (int)BOX_ACTIVE_NONE);

  CHECK_EQ((int)fill((uint8_t)BOX_SLOTS), (int)BOX_SLOTS);
  CHECK_EQ((int)box_count(), (int)BOX_SLOTS);

  CHECK_EQ((int)box_new_pebble(SPECIES_ID_STARTER, 1, (uint8_t)ORIGIN_WILD,
                               zero_genome(), 7, BOX_EPOCH0),
           (int)BOX_SLOT_NONE);
  CHECK_EQ((int)box_count(), (int)BOX_SLOTS);
}

// -----------------------------------------------------------------------------
//  B2 - every id is non-zero and unique, and stays so across a release/refill
// -----------------------------------------------------------------------------
TEST(box_ids_are_non_zero_and_unique) {
  box_fixture();
  fill((uint8_t)BOX_SLOTS);

  for (uint8_t i = 0; i < (uint8_t)BOX_SLOTS; ++i) {
    const PebbleInstance* a = box_peek(i);
    CHECK(a != nullptr);
    CHECK(a->id != 0u);
    for (uint8_t j = (uint8_t)(i + 1u); j < (uint8_t)BOX_SLOTS; ++j) {
      CHECK(a->id != box_peek(j)->id);
    }
  }

  // A released slot's id must not be handed straight back out.
  const uint32_t gone = box_peek(4)->id;
  CHECK(box_release(4, true));
  CHECK(box_new_pebble(SPECIES_ID_STARTER, 1, (uint8_t)ORIGIN_WILD,
                       zero_genome(), 99, BOX_EPOCH0) == 4);
  CHECK(box_peek(4)->id != gone);
}

// An id that arrives from outside (a capture or a trade) is kept; a colliding
// one is re-minted rather than silently duplicating a Pebble.
TEST(box_add_keeps_a_foreign_id_but_refuses_a_duplicate) {
  box_fixture();
  fill(1);
  const uint32_t mine = box_peek(0)->id;

  PebbleInstance incoming;
  memset(&incoming, 0, sizeof incoming);
  incoming.species_id = SPECIES_ID_STARTER;
  incoming.level      = 3;
  incoming.id         = 0xDEADBEEFu;
  CHECK_EQ((int)box_add(incoming), 1);
  CHECK_EQ(box_peek(1)->id, 0xDEADBEEFu);

  incoming.id = mine;                       // the same id twice
  const uint8_t slot = box_add(incoming);
  CHECK_EQ((int)slot, 2);
  CHECK(box_peek(2)->id != mine);
  CHECK(box_peek(2)->id != 0u);
}

// -----------------------------------------------------------------------------
//  B3 - exactly one active slot
// -----------------------------------------------------------------------------
TEST(box_has_exactly_one_active_slot) {
  box_fixture();
  CHECK_EQ((int)box_active(), (int)BOX_ACTIVE_NONE);   // empty: none

  fill(3);
  const uint8_t a = box_active();
  CHECK(a < (uint8_t)BOX_SLOTS);
  CHECK(box_occupied(a));

  CHECK(box_set_active(2));
  CHECK_EQ((int)box_active(), 2);

  CHECK(!box_set_active(5));                           // empty slot: refused
  CHECK(!box_set_active((uint8_t)BOX_SLOTS));          // out of range: refused
  CHECK_EQ((int)box_active(), 2);

  // A header that points at an empty slot is repaired, never believed.
  g_state.box.active_slot = 9;
  const uint8_t fixed = box_active();
  CHECK(fixed < (uint8_t)BOX_SLOTS);
  CHECK(box_occupied(fixed));
}

// -----------------------------------------------------------------------------
//  Swap carries the active index with the creature
// -----------------------------------------------------------------------------
TEST(box_swap_moves_the_pebbles_and_the_active_index) {
  box_fixture();
  fill(3);
  CHECK(box_set_active(0));

  const uint32_t id0 = box_peek(0)->id;
  const uint32_t id2 = box_peek(2)->id;

  CHECK(box_swap(0, 2));
  CHECK_EQ(box_peek(0)->id, id2);
  CHECK_EQ(box_peek(2)->id, id0);
  CHECK_EQ((int)box_active(), 2);            // still the same creature

  CHECK(box_swap(2, 2));                     // a no-op is still a success
  CHECK_EQ((int)box_active(), 2);
  CHECK(!box_swap(0, (uint8_t)BOX_SLOTS));   // out of range
}

// -----------------------------------------------------------------------------
//  B4 - release is explicit, and never the active slot
// -----------------------------------------------------------------------------
TEST(box_release_is_explicit_and_spares_the_active_slot) {
  box_fixture();
  fill(3);
  CHECK(box_set_active(1));

  CHECK(!box_release(2, false));             // no confirmation: refused
  CHECK(box_occupied(2));

  CHECK(!box_release(1, true));              // the active one: refused outright
  CHECK(box_occupied(1));
  CHECK_EQ((int)box_active(), 1);

  CHECK(box_release(2, true));
  CHECK(!box_occupied(2));
  CHECK_EQ((int)box_count(), 2);

  CHECK(!box_release(2, true));              // already empty
  CHECK(!box_release((uint8_t)BOX_SLOTS, true));
}

// -----------------------------------------------------------------------------
//  B5 - recovery caps at 100 % and touches nothing else
// -----------------------------------------------------------------------------
TEST(box_recover_caps_at_full_and_leaves_xp_and_level_alone) {
  box_fixture();
  fill(2);
  CHECK(box_set_active(0));

  PebbleInstance* p = box_slot(1);
  CHECK(p != nullptr);
  p->level = 7;
  p->xp    = 1234;
  p->age_s = 99999;
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) {
    p->care[i]     = 10000;                  // 10 %
    p->care_rem[i] = 0;
  }
  const uint32_t id_before  = p->id;
  const uint32_t last_before = p->last_updated_epoch;

  box_recover(1, 3600u);                     // one hour
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) {
    CHECK_EQ(p->care[i], 10000 + (int32_t)BOX_RECOVER_MPH);
  }
  CHECK_EQ(p->last_updated_epoch, last_before + 3600u);

  box_recover(1, 400u * 86400u);             // a year in the Box
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) {
    CHECK_EQ(p->care[i], (int32_t)PB_CARE_MILLI_MAX);   // capped, never above
    CHECK_EQ((int)p->care_rem[i], 0);
  }

  // Nothing but care and the timestamp moved.
  CHECK_EQ((int)p->level, 7);
  CHECK_EQ((int)p->xp, 1234);
  CHECK_EQ(p->age_s, 99999u);
  CHECK_EQ(p->id, id_before);
}

// The chunk size must not change the answer: the remainder is carried.
TEST(box_recover_is_chunk_independent) {
  box_fixture();
  fill(3);
  CHECK(box_set_active(0));
  for (uint8_t s = 1; s <= 2; ++s) {
    PebbleInstance* q = box_slot(s);
    for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) {
      q->care[i] = 20000; q->care_rem[i] = 0;
    }
  }
  box_recover(1, 3600u);
  for (uint16_t k = 0; k < 60; ++k) box_recover(2, 60u);

  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) {
    CHECK_EQ(box_peek(1)->care[i], box_peek(2)->care[i]);
  }
  CHECK_EQ(box_peek(1)->last_updated_epoch, box_peek(2)->last_updated_epoch);
}

// The boot sweep: every stored slot is brought up to now, the active one is not
// (sim_catch_up_ex owns it).
TEST(box_recover_all_skips_the_active_slot) {
  box_fixture();
  fill(3);
  CHECK(box_set_active(0));
  for (uint8_t s = 0; s < 3; ++s) {
    PebbleInstance* q = box_slot(s);
    for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) {
      q->care[i] = 0; q->care_rem[i] = 0;
    }
  }

  CHECK_EQ((int)box_recover_all(BOX_EPOCH0 + 7u * 86400u), 2);
  CHECK_EQ(box_peek(0)->care[0], 0);                            // untouched
  CHECK_EQ(box_peek(1)->care[0], (int32_t)PB_CARE_MILLI_MAX);   // a stored week
  CHECK_EQ(box_peek(2)->care[0], (int32_t)PB_CARE_MILLI_MAX);
  CHECK_EQ(box_peek(0)->last_updated_epoch, BOX_EPOCH0);

  // A clock that went backwards charges nothing rather than something negative.
  CHECK_EQ((int)box_recover_all(BOX_EPOCH0), 0);
}

// -----------------------------------------------------------------------------
//  The first-boot starter (plan P2-C10)
// -----------------------------------------------------------------------------
TEST(box_first_boot_starter_lands_in_slot_zero) {
  box_fixture();
  const uint8_t slot = box_new_pebble(SPECIES_ID_STARTER, 1,
                                      (uint8_t)ORIGIN_STARTER, zero_genome(),
                                      0xC0FFEEu, BOX_EPOCH0);
  CHECK_EQ((int)slot, 0);
  CHECK(box_set_active(0));

  const PebbleInstance* p = box_peek(0);
  CHECK_EQ((int)p->species_id, (int)SPECIES_ID_STARTER);
  CHECK_EQ((int)p->level, 1);
  CHECK_EQ((int)p->origin, (int)ORIGIN_STARTER);
  CHECK_EQ((int)p->xp, 0);
  CHECK(p->id != 0u);
  CHECK_EQ(p->birth_epoch, BOX_EPOCH0);
  CHECK_EQ(p->last_updated_epoch, BOX_EPOCH0);
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) {
    CHECK_EQ(p->care[i], (int32_t)PB_CARE_MILLI_MAX);
  }
  CHECK_EQ((int)box_count(), 1);
  CHECK_EQ((int)box_active(), 0);
}
