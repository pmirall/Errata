// =============================================================================
//  Errata host tests - test_box.cpp
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
    g_state.bugs[i].magic      = (uint16_t)BUG_MAGIC;
    g_state.bugs[i].layout_ver = (uint8_t)BUG_LAYOUT_VER;
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
    if (box_new_bug(SPECIES_ID_STARTER, 1, (uint8_t)ORIGIN_WILD,
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

  CHECK_EQ((int)box_new_bug(SPECIES_ID_STARTER, 1, (uint8_t)ORIGIN_WILD,
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
    const BugInstance* a = box_peek(i);
    CHECK(a != nullptr);
    CHECK(a->id != 0u);
    for (uint8_t j = (uint8_t)(i + 1u); j < (uint8_t)BOX_SLOTS; ++j) {
      CHECK(a->id != box_peek(j)->id);
    }
  }

  // A released slot's id must not be handed straight back out.
  const uint32_t gone = box_peek(4)->id;
  CHECK(box_release(4, true));
  CHECK(box_new_bug(SPECIES_ID_STARTER, 1, (uint8_t)ORIGIN_WILD,
                       zero_genome(), 99, BOX_EPOCH0) == 4);
  CHECK(box_peek(4)->id != gone);
}

// An id that arrives from outside (a capture or a trade) is kept; a colliding
// one is re-minted rather than silently duplicating a Bug.
TEST(box_add_keeps_a_foreign_id_but_refuses_a_duplicate) {
  box_fixture();
  fill(1);
  const uint32_t mine = box_peek(0)->id;

  BugInstance incoming;
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
TEST(box_swap_moves_the_bugs_and_the_active_index) {
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

  BugInstance* p = box_slot(1);
  CHECK(p != nullptr);
  p->level = 7;
  p->xp    = 1234;
  p->age_s = 99999;
  for (uint8_t i = 0; i < (uint8_t)ER_CARE_COUNT; ++i) {
    p->care[i]     = 10000;                  // 10 %
    p->care_rem[i] = 0;
  }
  const uint32_t id_before  = p->id;
  const uint32_t last_before = p->last_updated_epoch;

  box_recover(1, 3600u);                     // one hour
  for (uint8_t i = 0; i < (uint8_t)ER_CARE_COUNT; ++i) {
    CHECK_EQ(p->care[i], 10000 + (int32_t)BOX_RECOVER_MPH);
  }
  CHECK_EQ(p->last_updated_epoch, last_before + 3600u);

  box_recover(1, 400u * 86400u);             // a year in the Box
  for (uint8_t i = 0; i < (uint8_t)ER_CARE_COUNT; ++i) {
    CHECK_EQ(p->care[i], (int32_t)ER_CARE_MILLI_MAX);   // capped, never above
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
    BugInstance* q = box_slot(s);
    for (uint8_t i = 0; i < (uint8_t)ER_CARE_COUNT; ++i) {
      q->care[i] = 20000; q->care_rem[i] = 0;
    }
  }
  box_recover(1, 3600u);
  for (uint16_t k = 0; k < 60; ++k) box_recover(2, 60u);

  for (uint8_t i = 0; i < (uint8_t)ER_CARE_COUNT; ++i) {
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
    BugInstance* q = box_slot(s);
    for (uint8_t i = 0; i < (uint8_t)ER_CARE_COUNT; ++i) {
      q->care[i] = 0; q->care_rem[i] = 0;
    }
  }

  CHECK_EQ((int)box_recover_all(BOX_EPOCH0 + 7u * 86400u), 2);
  CHECK_EQ(box_peek(0)->care[0], 0);                            // untouched
  CHECK_EQ(box_peek(1)->care[0], (int32_t)ER_CARE_MILLI_MAX);   // a stored week
  CHECK_EQ(box_peek(2)->care[0], (int32_t)ER_CARE_MILLI_MAX);
  CHECK_EQ(box_peek(0)->last_updated_epoch, BOX_EPOCH0);

  // A clock that went backwards charges nothing rather than something negative.
  CHECK_EQ((int)box_recover_all(BOX_EPOCH0), 0);
}

// -----------------------------------------------------------------------------
//  The first-boot starter (plan P2-C10)
// -----------------------------------------------------------------------------
TEST(box_first_boot_starter_lands_in_slot_zero) {
  box_fixture();
  const uint8_t slot = box_new_bug(SPECIES_ID_STARTER, 1,
                                      (uint8_t)ORIGIN_STARTER, zero_genome(),
                                      0xC0FFEEu, BOX_EPOCH0);
  CHECK_EQ((int)slot, 0);
  CHECK(box_set_active(0));

  const BugInstance* p = box_peek(0);
  CHECK_EQ((int)p->species_id, (int)SPECIES_ID_STARTER);
  CHECK_EQ((int)p->level, 1);
  CHECK_EQ((int)p->origin, (int)ORIGIN_STARTER);
  CHECK_EQ((int)p->xp, 0);
  CHECK(p->id != 0u);
  CHECK_EQ(p->birth_epoch, BOX_EPOCH0);
  CHECK_EQ(p->last_updated_epoch, BOX_EPOCH0);
  for (uint8_t i = 0; i < (uint8_t)ER_CARE_COUNT; ++i) {
    CHECK_EQ(p->care[i], (int32_t)ER_CARE_MILLI_MAX);
  }
  CHECK_EQ((int)box_count(), 1);
  CHECK_EQ((int)box_active(), 0);
}

// =============================================================================
//  THE FIRST-BOOT STARTER SWAP (P10-C4)
//
//  box_release() refuses the active slot by rule B4 and is right to. The
//  starter choice has to replace exactly that Bug - the one app/app.cpp
//  minted at boot before the player had been asked anything - so it needs its
//  own door, and the lock on that door is about the BUG and not about when
//  the call happens: a clock-based rule ("within a minute of boot") fails the
//  moment somebody thinks for two minutes about a name, which is exactly the
//  accessibility failure spec section 65 is written against.
// =============================================================================
static uint8_t starter_slot(uint8_t species) {
  const uint8_t slot = box_new_bug(species, 1, (uint8_t)ORIGIN_STARTER,
                                      zero_genome(), 0xC0FFEEu, BOX_EPOCH0);
  CHECK(slot != (uint8_t)BOX_SLOT_NONE);
  CHECK(box_set_active(slot));
  return slot;
}

TEST(the_starter_swap_replaces_the_active_bug_and_keeps_what_it_should) {
  box_fixture();
  const uint8_t slot = starter_slot(1u);
  const BugInstance before = *box_peek(slot);

  CHECK(box_reroll_starter(slot, 16u, BOX_EPOCH0 + 90u));
  const BugInstance* after = box_peek(slot);
  CHECK(after != nullptr);
  if (!after) return;

  CHECK_EQ((int)after->species_id, 16);
  CHECK_EQ((int)box_active(), (int)slot);          // still the one being carried
  CHECK_EQ((int)box_count(), 1);                   // and still one Bug

  // THE GENOME AND THE CREATION SEED TRAVEL. They are what this device rolled
  // for this player at this boot; choosing a species must not silently re-roll
  // everything else about the creature.
  CHECK_EQ(memcmp(&after->genome, &before.genome, sizeof before.genome), 0);
  CHECK_EQ((int)after->creation_seed, (int)before.creation_seed);
  CHECK_EQ((int)after->birth_epoch,   (int)before.birth_epoch);

  // And it really is a NEW creature otherwise: the learnset and the derived HP
  // are the new species', not the old one's.
  CHECK_EQ((int)after->level, 1);
  CHECK(memcmp(after->moves, SPECIES_TABLE[15].moves, sizeof after->moves) == 0);
  CHECK(after->hp_cur > 0u);
}

TEST(the_starter_swap_refuses_any_bug_that_has_done_anything) {
  // Each case is a thing the PLAYER did. A Bug that has done none of them is
  // worth exactly what the next one would be; one that has done any of them is
  // not replaceable by a menu choice.
  struct Case { const char* what; void (*taint)(BugInstance&); };
  static const Case kCases[] = {
    { "level",         [](BugInstance& p){ p.level = 2; } },
    { "xp",            [](BugInstance& p){ p.xp = 1; } },
    { "a battle won",  [](BugInstance& p){ p.battles_won = 1; } },
    { "a battle lost", [](BugInstance& p){ p.battles_lost = 1; } },
    { "a minigame",    [](BugInstance& p){ p.minigames_won = 1; } },
    { "an evolution",  [](BugInstance& p){ p.evolutions = 1; } },
    { "a trade",       [](BugInstance& p){ p.trades = 1; } },
    { "a nickname",    [](BugInstance& p){ p.nickname[0] = 'A'; p.nickname[1] = '\0'; } },
    { "a wild origin", [](BugInstance& p){ p.origin = (uint8_t)ORIGIN_WILD; } },
  };
  for (size_t i = 0; i < sizeof kCases / sizeof kCases[0]; ++i) {
    box_fixture();
    const uint8_t slot = starter_slot(1u);
    kCases[i].taint(*box_slot(slot));
    if (box_reroll_starter(slot, 16u, BOX_EPOCH0 + 90u))
      fprintf(stderr, "  a Bug with %s was replaced by the starter swap\n",
              kCases[i].what);
    CHECK(!box_reroll_starter(slot, 16u, BOX_EPOCH0 + 90u));
    CHECK_EQ((int)box_peek(slot)->species_id, 1);   // and it is untouched
  }

  // ANTI-VACUITY: with none of the taints applied, the same call succeeds -
  // otherwise the nine refusals above would prove nothing about the rule.
  box_fixture();
  const uint8_t slot = starter_slot(1u);
  CHECK(box_reroll_starter(slot, 16u, BOX_EPOCH0 + 90u));

  // TIME IS DELIBERATELY NOT ONE OF THE LOCKS. age_s is the one field that
  // moves on its own; putting it in the list would make the rule "be quick".
  box_fixture();
  const uint8_t s2 = starter_slot(1u);
  box_slot(s2)->age_s = 3600u * 24u;               // a whole day of thinking
  CHECK(box_reroll_starter(s2, 31u, BOX_EPOCH0 + 86400u));
  CHECK_EQ((int)box_peek(s2)->species_id, 31);
}

TEST(the_starter_swap_refuses_an_empty_slot_and_an_unknown_species) {
  box_fixture();
  const uint8_t slot = starter_slot(1u);
  CHECK(!box_reroll_starter((uint8_t)(slot + 1u), 16u, BOX_EPOCH0));   // empty
  CHECK(!box_reroll_starter((uint8_t)BOX_SLOTS, 16u, BOX_EPOCH0));     // no slot
  CHECK(!box_reroll_starter(slot, 0u, BOX_EPOCH0));                    // no species
  CHECK(!box_reroll_starter(slot, 250u, BOX_EPOCH0));                  // past the roster
  CHECK_EQ((int)box_peek(slot)->species_id, 1);
}
