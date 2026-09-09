// =============================================================================
//  ERRATA host tests - test_dex.cpp
//
//  THE WIKI'S BIT ARITHMETIC. Fifteen bytes, two bits per species, sixty
//  species - and every one of those numbers is a place an off-by-one hides
//  where nothing else would notice: a shift that forgets the ids are 1-based
//  wastes the first two bits and drops the last species off the end, and the
//  only symptom is one creature that never records.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "core/nt_types.h"
#include "data/species_table.h"
#include "game/dex.h"

static uint8_t g_bytes[DEX_BYTES];

static void fresh(void)
{
  memset(g_bytes, 0, sizeof g_bytes);
  dex_bind(g_bytes);
  dex_reset();
}

TEST(nothing_is_known_before_anything_happens) {
  fresh();
  CHECK_EQ((int)dex_count_seen(), 0);
  CHECK_EQ((int)dex_count_caught(), 0);
  for (uint8_t id = 1u; id <= (uint8_t)SPECIES_TABLE_COUNT; ++id) {
    CHECK(!dex_seen(id));
    CHECK(!dex_caught(id));
  }
}

// EVERY SPECIES GETS ITS OWN TWO BITS, AND THIS IS THE CASE THAT PROVES IT.
// Marking one and checking that ONE is set is the whole point: a shift that
// collides two species passes any test that only ever marks one thing.
TEST(each_species_owns_two_bits_and_collides_with_no_other) {
  for (uint8_t id = 1u; id <= (uint8_t)SPECIES_TABLE_COUNT; ++id) {
    fresh();
    CHECK(dex_mark_seen(id));
    CHECK(dex_seen(id));
    CHECK(!dex_caught(id));
    CHECK_EQ((int)dex_count_seen(), 1);
    for (uint8_t other = 1u; other <= (uint8_t)SPECIES_TABLE_COUNT; ++other) {
      if (other == id) continue;
      if (dex_seen(other) || dex_caught(other))
        fprintf(stderr, "    marking %u also marked %u\n",
                (unsigned)id, (unsigned)other);
      CHECK(!dex_seen(other));
      CHECK(!dex_caught(other));
    }
  }
}

// THE LAST SPECIES IS THE ONE AN OFF-BY-ONE EATS, so it is checked by name as
// well as by the sweep above - a fifteen-byte array holds 120 bits and species
// 60 is bits 118..119, the very last two.
TEST(the_last_species_in_the_roster_still_fits) {
  fresh();
  const uint8_t last = (uint8_t)SPECIES_TABLE_COUNT;
  CHECK(dex_mark_caught(last));
  CHECK(dex_caught(last));
  CHECK(dex_seen(last));
  CHECK_EQ((int)dex_count_caught(), 1);
}

// CAUGHT IMPLIES SEEN, and it is set by the module rather than by every caller
// remembering. dex.h says why: the alternative is a wiki that lists a creature
// you are holding as one you have never met.
TEST(holding_a_bug_means_you_have_met_it) {
  fresh();
  CHECK(dex_mark_caught(7u));
  CHECK(dex_seen(7u));
  CHECK(dex_caught(7u));
  // ...and the other direction does NOT hold: meeting is not holding.
  CHECK(dex_mark_seen(8u));
  CHECK(dex_seen(8u));
  CHECK(!dex_caught(8u));
}

// THE RETURN VALUE IS "SOMETHING CHANGED", which is what lets a caller tell a
// new discovery from the four hundredth sighting without reading the bit back.
TEST(a_repeat_sighting_reports_that_nothing_moved) {
  fresh();
  CHECK(dex_mark_seen(3u));
  CHECK(!dex_mark_seen(3u));          // already known: no change
  CHECK(dex_mark_caught(3u));         // SEEN -> CAUGHT is a change
  CHECK(!dex_mark_caught(3u));
  CHECK_EQ((int)dex_count_seen(), 1);
  CHECK_EQ((int)dex_count_caught(), 1);
}

// NOTHING OUT OF RANGE TOUCHES THE ARRAY. A dex is written from encounter rolls
// and from peer frames, and neither is a trustworthy source of an index.
TEST(species_zero_and_anything_past_the_roster_are_refused) {
  fresh();
  static const uint8_t kBad[] = { 0u, (uint8_t)(SPECIES_TABLE_COUNT + 1u), 200u, 255u };
  for (uint8_t i = 0; i < (uint8_t)(sizeof kBad / sizeof kBad[0]); ++i) {
    CHECK(!dex_mark_seen(kBad[i]));
    CHECK(!dex_mark_caught(kBad[i]));
    CHECK(!dex_seen(kBad[i]));
    CHECK(!dex_caught(kBad[i]));
  }
  // AND THE BYTES ARE UNTOUCHED, which is the half a "returns false" cannot say.
  for (uint8_t b = 0; b < (uint8_t)DEX_BYTES; ++b) CHECK_EQ((int)g_bytes[b], 0);
}

// UNBOUND IS INERT, not a crash. app.cpp binds on load and a test may not have.
TEST(an_unbound_wiki_answers_no_and_writes_nowhere) {
  fresh();
  dex_unbind();
  CHECK(!dex_mark_seen(5u));
  CHECK(!dex_mark_caught(5u));
  CHECK(!dex_seen(5u));
  CHECK_EQ((int)dex_count_seen(), 0);
  for (uint8_t b = 0; b < (uint8_t)DEX_BYTES; ++b) CHECK_EQ((int)g_bytes[b], 0);
  dex_bind(g_bytes);
}

// THE WHOLE ROSTER FITS, counted rather than assumed.
TEST(the_whole_roster_fits_in_the_fifteen_bytes) {
  fresh();
  for (uint8_t id = 1u; id <= (uint8_t)SPECIES_TABLE_COUNT; ++id)
    CHECK(dex_mark_caught(id));
  CHECK_EQ((int)dex_count_seen(), (int)SPECIES_TABLE_COUNT);
  CHECK_EQ((int)dex_count_caught(), (int)SPECIES_TABLE_COUNT);
  printf("  %d species in %d bytes, %d bits spare\n",
         (int)SPECIES_TABLE_COUNT, (int)DEX_BYTES,
         (int)(DEX_BYTES * 8 - SPECIES_TABLE_COUNT * 2));
}
