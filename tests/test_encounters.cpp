// =============================================================================
//  PEBBLEBOL host test - test_encounters.cpp
//  THE ENCOUNTER ROLL (game/encounters.h, spec sections 20 and 22, P5-C3).
//
//  THE RULES THIS FILE OBEYS, copied from tests/test_validate.cpp because this
//  project has now shipped NINETEEN assertions that could not fail:
//
//   (a) A WEIGHT TEST THAT PASSES AGAINST SHUFFLED WEIGHTS IS WORTHLESS. Every
//       distribution case below is anchored to the TABLE's own numbers - the
//       expected share is read out of ENCOUNTER_TABLE at run time and the
//       tolerance is a band around it - so swapping two categories' weights, or
//       moving one, moves the expectation with the reality and the case still
//       has to hold. `the_measured_shares_track_the_table_and_not_each_other`
//       is the one that makes that non-trivial: it also asserts the categories
//       DIFFER from each other, so a picker that ignored the category entirely
//       and returned one fixed distribution fails.
//   (b) EVERY case that can name a specific outcome names it, never "not
//       NOTHING".
//   (c) The determinism cases assert BOTH halves: the same inputs give the same
//       answer AND each input actually reaches the answer, one field at a time.
//       A seed that ignored `bucket` would pass the first half alone.
//
//  MUTATIONS RUN AGAINST THIS FILE are listed in the commit message; each one
//  is named there with the case it turns red.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "data/balance.h"
#include "data/encounter_table.h"
#include "game/corruption.h"
#include "game/encounters.h"
#include "game/species.h"

// A plausible scan of one access point. Every field is set, so a case that
// wants to vary one varies exactly one.
static EncounterInput mk_in(uint8_t category)
{
  EncounterInput in;
  memset(&in, 0, sizeof in);
  in.net_hash     = 0xA17E3C55u;
  in.bucket       = 12345u;
  in.device_seed  = 0x0BADC0DEu;
  in.category     = category;
  in.rssi         = -60;
  in.active_level = 10;
  in.progress     = 3;
  return in;
}

// The table's own weight for one outcome in one category, as a percentage.
// Read at run time on purpose: see rule (a).
static uint32_t table_pct(uint8_t category, uint8_t outcome)
{
  uint32_t w = 0;
  for (uint8_t i = 0; i < ENCOUNTER_ROW_COUNT; ++i)
    if (ENCOUNTER_TABLE[i].category == category &&
        ENCOUNTER_TABLE[i].outcome == outcome)
      w += ENCOUNTER_TABLE[i].weight;
  return w;
}

// N rolls over one category, varying ONLY the network identity, tallied by
// outcome. Varying the hash is what a walk past N access points looks like.
static void tally(uint8_t category, uint32_t n, uint32_t out[ENC_OUT_COUNT])
{
  for (uint8_t o = 0; o < (uint8_t)ENC_OUT_COUNT; ++o) out[o] = 0;
  EncounterInput in = mk_in(category);
  for (uint32_t i = 0; i < n; ++i) {
    in.net_hash = 0x1000u + i * 2654435761u;   // Knuth's multiplier: spread, not random
    if (in.net_hash == 0u) in.net_hash = 1u;
    EncounterResult r;
    CHECK(encounter_roll(in, r));
    CHECK(r.outcome < (uint8_t)ENC_OUT_COUNT);
    out[r.outcome]++;
  }
}

// The same N rolls, but tallied by the RARITY BAND of the wild creature rather
// than by outcome. P6-C2's rare bonus moves weight INSIDE the wild outcome, so
// it is invisible to tally() above by construction - a distribution over bands
// is the only thing that can see it at all, which is exactly what the plan's
// carried-forward box warned about ("a bonus that moves WEIGHTS ... needs a
// case that can see a distribution, not just a membership").
static void tally_band(uint8_t category, uint16_t bonus_pm, uint32_t n,
                       uint32_t band[4], uint32_t out[ENC_OUT_COUNT])
{
  for (uint8_t b = 0; b < 4u; ++b) band[b] = 0;
  for (uint8_t o = 0; o < (uint8_t)ENC_OUT_COUNT; ++o) out[o] = 0;
  EncounterInput in = mk_in(category);
  in.rare_bonus_pm = bonus_pm;
  for (uint32_t i = 0; i < n; ++i) {
    in.net_hash = 0x1000u + i * 2654435761u;
    if (in.net_hash == 0u) in.net_hash = 1u;
    EncounterResult r;
    CHECK(encounter_roll(in, r));
    out[r.outcome]++;
    if (r.outcome != (uint8_t)ENC_OUT_WILD) continue;
    const SpeciesDef* sp = species_get(r.species_id);
    CHECK(sp != nullptr);
    if (sp && sp->rarity < 4u) band[sp->rarity]++;
  }
}

// =============================================================================
//  1. THE CONTRACT: TOTALITY AND REFUSAL
// =============================================================================
TEST(a_roll_is_total_over_every_category_and_always_names_a_resolved_outcome) {
  for (uint8_t c = 0; c < (uint8_t)NET_CAT_COUNT; ++c) {
    EncounterInput in = mk_in(c);
    for (uint32_t k = 0; k < 500u; ++k) {
      in.net_hash = 1u + k * 2246822519u;
      in.bucket   = k;
      in.rssi     = (int8_t)(-100 + (int)(k % 90u));
      in.active_level = (uint8_t)(1u + (k % 30u));
      in.progress = (uint8_t)(k % 11u);
      EncounterResult r;
      CHECK(encounter_roll(in, r));
      CHECK(r.outcome < (uint8_t)ENC_OUT_COUNT);
      // EVERY outcome carries exactly the payload it names and nothing else.
      switch ((EncounterOutcome)r.outcome) {
        case ENC_OUT_WILD:
          CHECK(species_get(r.species_id) != nullptr);
          CHECK(r.level >= 1 && r.level <= (uint8_t)XP_LEVEL_MAX);
          CHECK_EQ(r.item_id, 0);
          CHECK_EQ(r.event_id, 0);
          break;
        case ENC_OUT_ITEM:
          CHECK(item_get(r.item_id) != nullptr);
          CHECK_EQ(r.species_id, 0);
          CHECK_EQ(r.level, 0);
          CHECK_EQ(r.event_id, 0);
          break;
        case ENC_OUT_SPECIAL:
          CHECK(r.event_id >= 1 && r.event_id <= SPECIAL_EVENT_COUNT);
          CHECK(r.event_kind < (uint8_t)SPEV_COUNT);
          CHECK(encounter_event_of(r) != nullptr);
          CHECK_EQ(r.species_id, 0);
          CHECK_EQ(r.item_id, 0);
          break;
        case ENC_OUT_NOTHING:
          CHECK_EQ(r.species_id, 0);
          CHECK_EQ(r.item_id, 0);
          CHECK_EQ(r.event_id, 0);
          break;
        default:
          CHECK(false);
          break;
      }
    }
  }
}

TEST(a_zero_hash_and_a_bad_category_are_refused_and_yield_nothing) {
  // net_hash 0 is CooldownRow's "empty row" marker: a network that hashed to 0
  // would be permanently off cooldown, which is why the scanner folds it away
  // and why this refuses rather than rolling.
  EncounterInput in = mk_in((uint8_t)NET_CAT_HOME);
  in.net_hash = 0u;
  EncounterResult r;
  memset(&r, 0xAA, sizeof r);
  CHECK(!encounter_roll(in, r));
  CHECK_EQ(r.outcome, (uint8_t)ENC_OUT_NOTHING);
  CHECK_EQ(r.species_id, 0);
  CHECK_EQ(r.item_id, 0);
  CHECK_EQ(r.event_id, 0);

  in = mk_in((uint8_t)NET_CAT_COUNT);        // one past the last real category
  memset(&r, 0xAA, sizeof r);
  CHECK(!encounter_roll(in, r));
  CHECK_EQ(r.outcome, (uint8_t)ENC_OUT_NOTHING);
  CHECK_EQ(r.species_id, 0);

  in = mk_in(200u);
  CHECK(!encounter_roll(in, r));
  CHECK_EQ(r.outcome, (uint8_t)ENC_OUT_NOTHING);
}

// =============================================================================
//  2. DETERMINISM, BOTH HALVES
// =============================================================================
TEST(the_same_scan_in_the_same_bucket_gives_the_same_encounter) {
  for (uint8_t c = 0; c < (uint8_t)NET_CAT_COUNT; ++c) {
    EncounterInput in = mk_in(c);
    for (uint32_t k = 0; k < 64u; ++k) {
      in.net_hash = 1u + k * 40503u;
      EncounterResult a, b;
      CHECK(encounter_roll(in, a));
      CHECK(encounter_roll(in, b));
      CHECK_EQ(memcmp(&a, &b, sizeof a), 0);
    }
  }
}

// AND THE OTHER HALF, WHICH IS THE ONE THAT CAN FAIL: every declared input
// really reaches the answer. A seed that folded in only net_hash would pass the
// case above with five of the six fields dead.
TEST(every_declared_input_reaches_the_answer) {
  const EncounterInput base = mk_in((uint8_t)NET_CAT_PUBLIC);
  EncounterResult r0;
  CHECK(encounter_roll(base, r0));

  // For each field: perturb it across a range and require the ANSWER to differ
  // at least once. "At least once" and not "always", because two different
  // inputs may legitimately land on the same outcome - the claim is that the
  // field is wired in, not that the hash is injective.
  int moved_hash = 0, moved_bucket = 0, moved_dev = 0;
  int moved_rssi = 0, moved_level = 0, moved_prog = 0, moved_cat = 0;
  int moved_bonus = 0;
  for (uint32_t k = 1; k <= 64u; ++k) {
    EncounterInput in = base; EncounterResult r;
    in.net_hash = base.net_hash + k;
    CHECK(encounter_roll(in, r)); if (memcmp(&r, &r0, sizeof r) != 0) moved_hash++;

    in = base; in.bucket = base.bucket + k;
    CHECK(encounter_roll(in, r)); if (memcmp(&r, &r0, sizeof r) != 0) moved_bucket++;

    in = base; in.device_seed = base.device_seed + k;
    CHECK(encounter_roll(in, r)); if (memcmp(&r, &r0, sizeof r) != 0) moved_dev++;

    in = base; in.rssi = (int8_t)(-100 + (int)(k % 90u));
    CHECK(encounter_roll(in, r)); if (memcmp(&r, &r0, sizeof r) != 0) moved_rssi++;

    in = base; in.active_level = (uint8_t)(1u + (k % 30u));
    CHECK(encounter_roll(in, r)); if (memcmp(&r, &r0, sizeof r) != 0) moved_level++;

    in = base; in.progress = (uint8_t)(k % 11u);
    CHECK(encounter_roll(in, r)); if (memcmp(&r, &r0, sizeof r) != 0) moved_prog++;

    in = base; in.category = (uint8_t)(k % (uint32_t)NET_CAT_COUNT);
    CHECK(encounter_roll(in, r)); if (memcmp(&r, &r0, sizeof r) != 0) moved_cat++;

    // THE EIGHTH FIELD (P6-C2), and it is here because its absence was
    // MEASURED: an eighth field declared and left unread passed this file
    // 22/22, because the list above is written out by hand and cannot see a
    // field nobody adds to it. The sizeof static_assert in game/encounters.h
    // is what forces the next person to this line; this perturbation is what
    // proves the field is wired once they get here.
    //
    // IT IS COMPARED PAIRWISE AND NOT AGAINST r0, and the difference is not
    // cosmetic. Every field above is folded into encounter_seed(), so moving it
    // moves the whole draw and one fixed base is enough to see it. This one is
    // deliberately NOT in the seed (see game/encounters.h): it is a THRESHOLD
    // on one independent draw, so for any single fixed input the draw either
    // clears the threshold or never does, whatever the permille. Sweeping the
    // bonus against a fixed base therefore proves nothing about three quarters
    // of the seeds - measured: with the sweep written that way this check read
    // `FAIL CHECK(moved_bonus > 0)` on a correctly wired field. Comparing the
    // SAME input with the bonus off and on, over the same 64 networks the other
    // fields are perturbed across, is the claim that actually holds: turning
    // the bonus on changes the answer for at least one scan.
    in = base; in.net_hash = base.net_hash + k;
    EncounterResult off, on;
    CHECK(encounter_roll(in, off));
    in.rare_bonus_pm = (uint16_t)ENC_RARE_BONUS_MAX_PM;
    CHECK(encounter_roll(in, on));
    if (memcmp(&off, &on, sizeof on) != 0) moved_bonus++;
  }
  CHECK(moved_hash > 0);
  CHECK(moved_bucket > 0);
  CHECK(moved_dev > 0);
  CHECK(moved_rssi > 0);
  CHECK(moved_level > 0);
  CHECK(moved_prog > 0);
  CHECK(moved_cat > 0);
  CHECK(moved_bonus > 0);
}

TEST(two_devices_standing_side_by_side_do_not_see_the_same_creature) {
  // The same access point, the same six hours, two device ids. Section 20 lists
  // "device random seed" as an input for exactly this reason.
  EncounterInput a = mk_in((uint8_t)NET_CAT_OPEN);
  EncounterInput b = a;
  b.device_seed = a.device_seed ^ 0xFFFFFFFFu;
  int differ = 0;
  for (uint32_t k = 0; k < 200u; ++k) {
    a.net_hash = b.net_hash = 1u + k * 2654435761u;
    EncounterResult ra, rb;
    CHECK(encounter_roll(a, ra));
    CHECK(encounter_roll(b, rb));
    if (memcmp(&ra, &rb, sizeof ra) != 0) differ++;
  }
  // Not "always differ" - two devices will sometimes agree - but the two must
  // not be the same sequence. Under any sane mix this is well over half.
  CHECK(differ > 120);
}

TEST(the_six_hour_bucket_is_the_one_in_balance_h_and_nothing_else) {
  CHECK_EQ(encounter_bucket(0u), 0u);
  CHECK_EQ(encounter_bucket((uint32_t)ENCOUNTER_BUCKET_S - 1u), 0u);
  CHECK_EQ(encounter_bucket((uint32_t)ENCOUNTER_BUCKET_S), 1u);
  CHECK_EQ(encounter_bucket((uint32_t)ENCOUNTER_BUCKET_S * 4u), 4u);
  // Four buckets to the day, which is what "6 h" means and what the plan asks
  // for. Asserted as arithmetic rather than as a comment.
  CHECK_EQ((uint32_t)ENCOUNTER_BUCKET_S * 4u, 86400u);
  // A whole bucket is longer than a cooldown, so a network coming off cooldown
  // inside its own bucket cannot replay the same encounter forever.
  CHECK((uint32_t)ENCOUNTER_BUCKET_S >= (uint32_t)ENCOUNTER_COOLDOWN_S);
}

// =============================================================================
//  3. THE WEIGHTS - MEASURED AGAINST THE TABLE, NOT AGAINST A LITERAL
// =============================================================================
TEST(ten_thousand_rolls_per_category_track_the_tables_own_weights) {
  for (uint8_t c = 0; c < (uint8_t)NET_CAT_COUNT; ++c) {
    uint32_t got[ENC_OUT_COUNT];
    tally(c, 10000u, got);
    for (uint8_t o = 0; o < (uint8_t)ENC_OUT_COUNT; ++o) {
      const uint32_t want = table_pct(c, o) * 100u;      // per 10,000
      // +/- 2.5 percentage points of the table's own share. Wide enough that a
      // correct picker never flakes, narrow enough that swapping any two rows
      // in a category - the smallest gap in the table is 4 vs 10 points - is
      // outside it.
      const uint32_t tol = 250u;
      CHECK(got[o] + tol >= want);
      CHECK(got[o] <= want + tol);
    }
  }
}

// THE CASE THAT MAKES THE ONE ABOVE NON-TRIVIAL. A picker that ignored the
// category and returned one fixed distribution would satisfy every band above
// only if all six categories had the same weights - they do not, and this
// asserts the difference is really observed rather than assumed.
TEST(the_measured_shares_track_the_table_and_not_each_other) {
  uint32_t home[ENC_OUT_COUNT], hidden[ENC_OUT_COUNT];
  tally((uint8_t)NET_CAT_HOME, 10000u, home);
  tally((uint8_t)NET_CAT_HIDDEN, 10000u, hidden);
  // HIDDEN's SPECIAL slice is 10 % against HOME's 4 %, and its WILD slice is
  // the heaviest on the roster: the two categories must not measure alike.
  CHECK(hidden[ENC_OUT_SPECIAL] > home[ENC_OUT_SPECIAL] + 300u);
  CHECK(home[ENC_OUT_NOTHING] > hidden[ENC_OUT_NOTHING] + 100u);
  // ...and the direction of each difference is the table's, read at run time.
  CHECK(table_pct((uint8_t)NET_CAT_HIDDEN, (uint8_t)ENC_OUT_SPECIAL) >
        table_pct((uint8_t)NET_CAT_HOME, (uint8_t)ENC_OUT_SPECIAL));
  CHECK(table_pct((uint8_t)NET_CAT_HOME, (uint8_t)ENC_OUT_NOTHING) >
        table_pct((uint8_t)NET_CAT_HIDDEN, (uint8_t)ENC_OUT_NOTHING));
}

TEST(nothing_really_happens_and_it_happens_at_least_fifteen_percent_of_the_time) {
  // Spec section 22: exploring must be able to come back empty, or the device
  // is a slot machine. The floor is the pack's own ENCOUNTER_NOTHING_MIN_PCT.
  for (uint8_t c = 0; c < (uint8_t)NET_CAT_COUNT; ++c) {
    uint32_t got[ENC_OUT_COUNT];
    tally(c, 10000u, got);
    CHECK(got[ENC_OUT_NOTHING] >= (uint32_t)ENCOUNTER_NOTHING_MIN_PCT * 100u - 250u);
    // ...and all four outcomes are reachable in every category, so no arm of
    // the switch in encounters.cpp is dead.
    for (uint8_t o = 0; o < (uint8_t)ENC_OUT_COUNT; ++o) CHECK(got[o] > 0);
  }
}

// =============================================================================
//  3b. THE RARE-ENCOUNTER BONUS (P6-C2). Four claims, and each one is the
//      thing a different wrong implementation would break:
//        - zero is the exact identity          (a bonus folded into the seed)
//        - the outcome split never moves       (a promotion that ate NOTHING)
//        - the rare share rises monotonically  (a bonus that reshuffles)
//        - the common band survives the max    (a max permille of 1000)
//      Every phase-5 case above runs at bonus 0, so without these four the
//      entire weight-moving half of encounter_roll() would be untested by
//      construction - this project's signature defect, re-armed.
// =============================================================================
TEST(a_zero_bonus_reproduces_the_tables_own_wild_band_weights_exactly) {
  // "Zero is the identity" made checkable without a copy of the old function to
  // diff against: at bonus 0 the measured share of each rarity band must be the
  // TABLE's own share for that band, read at run time. A bonus folded into
  // encounter_seed() would still pass tally()'s outcome check and would fail
  // here, because folding changes which species each seed lands on.
  for (uint8_t c = 0; c < (uint8_t)NET_CAT_COUNT; ++c) {
    uint32_t band[4], out[ENC_OUT_COUNT];
    tally_band(c, 0u, 20000u, band, out);
    for (uint8_t b = 0; b < 3u; ++b) {
      uint32_t want = 0;
      for (uint8_t i = 0; i < ENCOUNTER_ROW_COUNT; ++i) {
        const EncounterRow& row = ENCOUNTER_TABLE[i];
        if (row.category == c && row.outcome == (uint8_t)ENC_OUT_WILD &&
            row.rarity_min == b)
          want += row.weight;
      }
      want *= 200u;                                   // per 20,000
      CHECK(band[b] + 500u >= want);
      CHECK(band[b] <= want + 500u);
    }
  }
}

TEST(the_outcome_split_is_identical_at_every_bonus_and_nothing_eats_nothing) {
  // EXACT equality, not a tolerance band: a promotion only ever replaces a WILD
  // row with another WILD row of the same category, and it draws from its own
  // independent stage, so the outcome tally must be the SAME NUMBERS at every
  // permille. This is what makes section 22's 15 % NOTHING floor structurally
  // safe under any activity score rather than merely measured at one.
  const uint16_t pm[4] = { 0u, (uint16_t)(ENC_RARE_BONUS_MAX_PM / 4u),
                           (uint16_t)(ENC_RARE_BONUS_MAX_PM / 2u),
                           (uint16_t)ENC_RARE_BONUS_MAX_PM };
  for (uint8_t c = 0; c < (uint8_t)NET_CAT_COUNT; ++c) {
    uint32_t base_band[4], base_out[ENC_OUT_COUNT];
    tally_band(c, pm[0], 20000u, base_band, base_out);
    CHECK(base_out[ENC_OUT_NOTHING] > 0u);
    for (uint8_t j = 1; j < 4u; ++j) {
      uint32_t band[4], out[ENC_OUT_COUNT];
      tally_band(c, pm[j], 20000u, band, out);
      for (uint8_t o = 0; o < (uint8_t)ENC_OUT_COUNT; ++o) CHECK_EQ(out[o], base_out[o]);
      // ...and the wild total is invariant too, so the bonus really is moving
      // weight INSIDE wild and not creating creatures.
      CHECK_EQ(band[0] + band[1] + band[2] + band[3], base_band[0] + base_band[1] +
               base_band[2] + base_band[3]);
    }
  }
}

TEST(the_rare_share_rises_with_the_bonus_and_the_common_band_never_empties) {
  const uint16_t pm[4] = { 0u, (uint16_t)(ENC_RARE_BONUS_MAX_PM / 4u),
                           (uint16_t)(ENC_RARE_BONUS_MAX_PM / 2u),
                           (uint16_t)ENC_RARE_BONUS_MAX_PM };
  for (uint8_t c = 0; c < (uint8_t)NET_CAT_COUNT; ++c) {
    uint32_t rare[4] = { 0, 0, 0, 0 }, common[4] = { 0, 0, 0, 0 };
    for (uint8_t j = 0; j < 4u; ++j) {
      uint32_t band[4], out[ENC_OUT_COUNT];
      tally_band(c, pm[j], 20000u, band, out);
      rare[j]   = band[2];
      common[j] = band[0];
      CHECK(common[j] > 0u);
    }
    // THE REASON ENC_RARE_BONUS_MAX_PM IS 250 AND NOT 900-ODD, and the check
    // has to be a SHARE rather than "> 0" to be worth anything: at permille
    // 1000 the common band empties completely, and even at 999 about one roll
    // in a thousand survives - so a non-zero count would pass a cap that has
    // effectively deleted the ordinary creature from the game. Measured: with
    // ENC_RARE_BONUS_MAX_PM edited to 999 the `common[j] > 0` form still passed
    // every category. The claim that bites is that a full activity day may not
    // even HALVE the chance of meeting an ordinary creature. At 250 the common
    // band keeps three quarters of its weight, so this holds with margin; it
    // fails for any cap above 500. Exactly 1000 is refused at build time by the
    // static_assert next to the constant in data/balance.h.
    CHECK(common[3] * 2u >= common[0]);
    // Monotone non-decreasing in the bonus, and strictly higher at the top than
    // at zero. "Non-decreasing" between neighbours because a quarter-step is
    // within sampling noise; "strictly" end to end because the whole point of
    // the field is that a full activity day is worth something.
    for (uint8_t j = 1; j < 4u; ++j) CHECK(rare[j] + 150u >= rare[j - 1]);
    CHECK(rare[3] > rare[0]);
    CHECK(common[3] < common[0]);
  }
}

TEST(a_bonus_above_the_cap_is_clamped_by_the_roll_and_not_by_the_caller) {
  // A pure function may not trust its input. Every value at or above the cap
  // must produce EXACTLY the cap's distribution - roll for roll, not merely
  // within a band - so a screen that computed a permille wrong, or a corrupted
  // activity blob, cannot buy a rarer creature than a full honest day does.
  for (uint8_t c = 0; c < (uint8_t)NET_CAT_COUNT; ++c) {
    EncounterInput a = mk_in(c), b = mk_in(c), d = mk_in(c);
    a.rare_bonus_pm = (uint16_t)ENC_RARE_BONUS_MAX_PM;
    b.rare_bonus_pm = (uint16_t)(ENC_RARE_BONUS_MAX_PM + 1u);
    d.rare_bonus_pm = 0xFFFFu;
    for (uint32_t k = 0; k < 3000u; ++k) {
      a.net_hash = b.net_hash = d.net_hash = 1u + k * 2654435761u;
      EncounterResult ra, rb, rd;
      CHECK(encounter_roll(a, ra));
      CHECK(encounter_roll(b, rb));
      CHECK(encounter_roll(d, rd));
      CHECK_EQ(memcmp(&ra, &rb, sizeof ra), 0);
      CHECK_EQ(memcmp(&ra, &rd, sizeof ra), 0);
    }
  }
}

TEST(the_roll_is_still_total_and_still_deterministic_under_a_bonus) {
  // The two contract cases of section 1 and 2, re-run with the bonus turned on:
  // a new stage that could return null, or that read anything outside the input
  // struct, would show up here and nowhere else.
  for (uint8_t c = 0; c < (uint8_t)NET_CAT_COUNT; ++c) {
    EncounterInput in = mk_in(c);
    in.rare_bonus_pm = (uint16_t)ENC_RARE_BONUS_MAX_PM;
    for (uint32_t k = 0; k < 2000u; ++k) {
      in.net_hash     = 1u + k * 2246822519u;
      in.bucket       = k;
      in.rssi         = (int8_t)(-100 + (int)(k % 90u));
      in.active_level = (uint8_t)(1u + (k % 30u));
      in.progress     = (uint8_t)(k % 11u);
      EncounterResult r, again;
      CHECK(encounter_roll(in, r));
      CHECK(r.outcome < (uint8_t)ENC_OUT_COUNT);
      if (r.outcome == (uint8_t)ENC_OUT_WILD) {
        CHECK(r.species_id != 0u);
        CHECK(r.level >= 1u && r.level <= (uint8_t)XP_LEVEL_MAX);
        const SpeciesDef* sp = species_get(r.species_id);
        CHECK(sp != nullptr);
        // A PROMOTED ROW IS STILL THIS CATEGORY'S ROW. rarer_wild_row() only
        // ever accepts a row with the same category ordinal, so the mask must
        // hold at the maximum bonus exactly as it does at zero.
        if (sp) CHECK((sp->category_mask & NET_CATEGORY_BIT[c]) != 0);
      }
      if (r.outcome == (uint8_t)ENC_OUT_ITEM) CHECK(r.item_id != 0u);
      if (r.outcome == (uint8_t)ENC_OUT_SPECIAL) CHECK(r.event_id != 0u);
      // Stateless and repeatable: the bonus arrives in the struct, so the same
      // struct twice is the same answer.
      CHECK(encounter_roll(in, again));
      CHECK_EQ(memcmp(&r, &again, sizeof r), 0);
    }
  }
}

// =============================================================================
//  4. THE PAYLOADS
// =============================================================================
TEST(a_wild_species_always_matches_its_rows_category_mask_and_rarity_band) {
  // The claim is not "the species is legal" but "the species is one THIS
  // category and THIS band could produce" - which is what a picker that lost
  // the mask would break, while still returning a real species.
  for (uint8_t c = 0; c < (uint8_t)NET_CAT_COUNT; ++c) {
    EncounterInput in = mk_in(c);
    int wilds = 0;
    for (uint32_t k = 0; k < 4000u; ++k) {
      in.net_hash = 1u + k * 2654435761u;
      EncounterResult r;
      CHECK(encounter_roll(in, r));
      if (r.outcome != (uint8_t)ENC_OUT_WILD) continue;
      wilds++;
      const SpeciesDef* sp = species_get(r.species_id);
      CHECK(sp != nullptr);
      if (!sp) continue;
      CHECK((sp->category_mask & NET_CATEGORY_BIT[c]) != 0);
      // and its rarity is one of the bands this category's WILD rows draw from
      bool in_a_band = false;
      for (uint8_t i = 0; i < ENCOUNTER_ROW_COUNT; ++i) {
        const EncounterRow& row = ENCOUNTER_TABLE[i];
        if (row.category != c || row.outcome != (uint8_t)ENC_OUT_WILD) continue;
        if (sp->rarity >= row.rarity_min && sp->rarity <= row.rarity_max) in_a_band = true;
      }
      CHECK(in_a_band);
    }
    CHECK(wilds > 0);
  }
}

TEST(a_wild_level_is_the_active_level_plus_or_minus_two_and_clamped_to_the_band) {
  // Both ends of the clamp, and the spread itself. The clamp is what keeps
  // every capture inside validate_pebble()'s level band.
  for (uint8_t lv = 1; lv <= (uint8_t)XP_LEVEL_MAX; ++lv) {
    EncounterInput in = mk_in((uint8_t)NET_CAT_HOME);
    in.active_level = lv;
    int lo_seen = 0, hi_seen = 0, count = 0;
    for (uint32_t k = 0; k < 3000u; ++k) {
      in.net_hash = 1u + k * 2654435761u;
      EncounterResult r;
      CHECK(encounter_roll(in, r));
      if (r.outcome != (uint8_t)ENC_OUT_WILD) continue;
      count++;
      CHECK(r.level >= 1);
      CHECK(r.level <= (uint8_t)XP_LEVEL_MAX);
      const int lo = (lv > ENCOUNTER_LEVEL_SPREAD) ? (int)lv - ENCOUNTER_LEVEL_SPREAD : 1;
      const int hi = ((int)lv + ENCOUNTER_LEVEL_SPREAD > (int)XP_LEVEL_MAX)
                         ? (int)XP_LEVEL_MAX : (int)lv + ENCOUNTER_LEVEL_SPREAD;
      CHECK((int)r.level >= lo);
      CHECK((int)r.level <= hi);
      if ((int)r.level == lo) lo_seen++;
      if ((int)r.level == hi) hi_seen++;
    }
    CHECK(count > 0);
    // Both ends of the band are actually produced, so the spread is real and
    // not a constant dressed as a range.
    CHECK(lo_seen > 0);
    CHECK(hi_seen > 0);
  }
}

TEST(an_item_encounter_always_names_an_item_its_own_row_could_drop) {
  for (uint8_t c = 0; c < (uint8_t)NET_CAT_COUNT; ++c) {
    EncounterInput in = mk_in(c);
    int items = 0;
    uint32_t seen[16];
    for (uint8_t e = 0; e < 16u; ++e) seen[e] = 0;
    CHECK(ITEM_COUNT < 16);                 // the tally above is fixed-width
    for (uint32_t k = 0; k < 4000u; ++k) {
      in.net_hash = 1u + k * 2654435761u;
      EncounterResult r;
      CHECK(encounter_roll(in, r));
      if (r.outcome != (uint8_t)ENC_OUT_ITEM) continue;
      items++;
      const ItemDef* it = item_get(r.item_id);
      CHECK(it != nullptr);
      if (!it) continue;
      // It is in THIS category's drop list...
      bool listed = false;
      for (uint8_t d = 0; d < ITEM_DROP_ROW_COUNT; ++d)
        if (ITEM_DROP_TABLE[d].category == c && ITEM_DROP_TABLE[d].item_id == r.item_id)
          listed = true;
      CHECK(listed);
      // ...and inside one of this category's ITEM rows' rarity bands.
      bool banded = false;
      for (uint8_t i = 0; i < ENCOUNTER_ROW_COUNT; ++i) {
        const EncounterRow& row = ENCOUNTER_TABLE[i];
        if (row.category != c || row.outcome != (uint8_t)ENC_OUT_ITEM) continue;
        if (it->rarity >= row.rarity_min && it->rarity <= row.rarity_max) banded = true;
      }
      CHECK(banded);
      seen[r.item_id]++;
    }
    CHECK(items > 0);

    // AND THE DISTRIBUTION, for the reason the SPECIAL case below spells out:
    // a membership-only check passed a picker that had gone blind to the
    // category, because the commonest drop is in every category's list. An item
    // this category cannot drop must appear NEVER, and the ones it can must
    // appear in the table's own proportions.
    uint32_t eligible = 0;
    for (uint8_t d = 0; d < ITEM_DROP_ROW_COUNT; ++d) {
      if (ITEM_DROP_TABLE[d].category != c) continue;
      const ItemDef* it = item_get(ITEM_DROP_TABLE[d].item_id);
      if (it && it->rarity <= 2u) eligible += ITEM_DROP_TABLE[d].weight;
    }
    for (uint8_t id = 1; id <= ITEM_COUNT; ++id) {
      uint32_t w = 0;
      for (uint8_t d = 0; d < ITEM_DROP_ROW_COUNT; ++d)
        if (ITEM_DROP_TABLE[d].category == c && ITEM_DROP_TABLE[d].item_id == id)
          w = ITEM_DROP_TABLE[d].weight;
      if (w == 0u) { CHECK_EQ(seen[id], 0u); continue; }
      // Only items inside SOME ITEM row's band can appear; the ones outside
      // every band legitimately do not, and the guard above is what promises
      // the pool is never empty.
      const ItemDef* def = item_get(id);
      bool reachable = false;
      for (uint8_t i = 0; i < ENCOUNTER_ROW_COUNT; ++i) {
        const EncounterRow& row = ENCOUNTER_TABLE[i];
        if (row.category != c || row.outcome != (uint8_t)ENC_OUT_ITEM) continue;
        if (def && def->rarity >= row.rarity_min && def->rarity <= row.rarity_max)
          reachable = true;
      }
      if (!reachable) { CHECK_EQ(seen[id], 0u); continue; }
      CHECK(seen[id] > 0u);
      const uint32_t want = ((uint32_t)items * w) / (eligible ? eligible : 100u);
      const uint32_t tol  = ((uint32_t)items * 10u) / 100u + 6u;
      CHECK(seen[id] + tol >= want);
      CHECK(seen[id] <= want + tol);
    }
  }
}

TEST(a_special_encounter_always_names_an_event_its_own_category_can_produce) {
  for (uint8_t c = 0; c < (uint8_t)NET_CAT_COUNT; ++c) {
    EncounterInput in = mk_in(c);
    int specials = 0;
    uint32_t seen[16];
    for (uint8_t e = 0; e < 16u; ++e) seen[e] = 0;
    CHECK(SPECIAL_EVENT_COUNT < 16);        // the tally above is fixed-width
    for (uint32_t k = 0; k < 8000u; ++k) {
      in.net_hash = 1u + k * 2654435761u;
      EncounterResult r;
      CHECK(encounter_roll(in, r));
      if (r.outcome != (uint8_t)ENC_OUT_SPECIAL) continue;
      specials++;
      const SpecialEvent* ev = encounter_event_of(r);
      CHECK(ev != nullptr);
      if (!ev) continue;
      CHECK_EQ(ev->id, r.event_id);
      CHECK_EQ(ev->kind, r.event_kind);
      CHECK_EQ((uint16_t)ev->value, r.event_value);
      bool listed = false;
      for (uint8_t d = 0; d < SPECIAL_DROP_ROW_COUNT; ++d)
        if (SPECIAL_DROP_TABLE[d].category == c &&
            SPECIAL_DROP_TABLE[d].event_id == r.event_id) listed = true;
      CHECK(listed);
      seen[r.event_id]++;
    }
    CHECK(specials > 0);

    // INSTANCE TWENTY OF THIS PROJECT'S RECURRING DEFECT, FOUND IN MY OWN TEST
    // AND MEASURED RATHER THAN REASONED ABOUT. The membership check above is
    // NOT ENOUGH: mutation M14 - dropping the category filter from the SECOND
    // loop of special_pick_event(), so the weight total is per category and the
    // walk is over the whole table - PASSED it, because event 1 is in all six
    // category lists and the mutant kept landing on event 1. What it cannot
    // survive is the DISTRIBUTION: an event this category does not carry must
    // never appear, and the ones it does carry must appear in the proportions
    // the table states.
    for (uint8_t e = 1; e <= SPECIAL_EVENT_COUNT; ++e) {
      uint32_t w = 0;
      for (uint8_t d = 0; d < SPECIAL_DROP_ROW_COUNT; ++d)
        if (SPECIAL_DROP_TABLE[d].category == c && SPECIAL_DROP_TABLE[d].event_id == e)
          w = SPECIAL_DROP_TABLE[d].weight;
      if (w == 0u) {
        CHECK_EQ(seen[e], 0u);            // never, not "rarely"
      } else {
        CHECK(seen[e] > 0u);
        // Within 8 percentage points of the table's share of the specials this
        // category produced. Wide, because the sample is only the SPECIAL slice
        // of 8,000 rolls; narrow enough that the smallest gap in the table
        // (10 vs 20 points) is outside it.
        const uint32_t want = ((uint32_t)specials * w) / 100u;
        const uint32_t tol  = ((uint32_t)specials * 8u) / 100u + 4u;
        CHECK(seen[e] + tol >= want);
        CHECK(seen[e] <= want + tol);
      }
    }
  }
}

TEST(both_special_kinds_are_reachable_from_every_category) {
  // The plan's P5-C3 bullet promises two SPECIAL outcomes: an XP burst and a
  // corruption event. This is the case that says both really happen, everywhere
  // - a payload table with one reachable kind would be the old hole in a new
  // shape.
  for (uint8_t c = 0; c < (uint8_t)NET_CAT_COUNT; ++c) {
    EncounterInput in = mk_in(c);
    int burst = 0, corrupt = 0;
    for (uint32_t k = 0; k < 20000u; ++k) {
      in.net_hash = 1u + k * 2654435761u;
      EncounterResult r;
      CHECK(encounter_roll(in, r));
      if (r.outcome != (uint8_t)ENC_OUT_SPECIAL) continue;
      if (r.event_kind == (uint8_t)SPEV_XP_BURST)   burst++;
      if (r.event_kind == (uint8_t)SPEV_CORRUPTION) corrupt++;
    }
    CHECK(burst > 0);
    CHECK(corrupt > 0);
  }
}

// =============================================================================
//  5. THE UNMETERED XP SOURCE AND THE CLOCK IT DEPENDS ON
// =============================================================================
TEST(an_xp_burst_pays_the_events_value_times_the_scale_and_nothing_else_does) {
  EncounterResult r;
  memset(&r, 0, sizeof r);
  r.outcome     = (uint8_t)ENC_OUT_SPECIAL;
  r.event_id    = 1u;
  r.event_kind  = (uint8_t)SPEV_XP_BURST;
  r.event_value = 5u;
  CHECK_EQ(encounter_special_xp(r, (uint8_t)CAL_USER),
           (uint16_t)(5u * (uint16_t)SPECIAL_XP_SCALE));

  // The corruption event pays no XP: it is not a second XP source wearing a
  // different name.
  r.event_kind = (uint8_t)SPEV_CORRUPTION;
  CHECK_EQ(encounter_special_xp(r, (uint8_t)CAL_USER), 0);

  // Nor does any other outcome, whatever junk is left in the event fields.
  r.event_kind = (uint8_t)SPEV_XP_BURST;
  for (uint8_t o = 0; o < (uint8_t)ENC_OUT_COUNT; ++o) {
    if (o == (uint8_t)ENC_OUT_SPECIAL) continue;
    r.outcome = o;
    CHECK_EQ(encounter_special_xp(r, (uint8_t)CAL_USER), 0);
  }
}

TEST(an_uncalibrated_device_is_paid_no_special_xp_and_the_event_still_fires) {
  // THE CHOICE P5-C3 HAD TO MAKE IN WRITING, asserted here so it cannot be
  // quietly reversed: XP_SRC_CAPTURE and XP_SRC_ITEM are unmetered because a
  // capture consumes an encounter and an item consumes the item; a SPECIAL
  // burst consumes NEITHER, and the only thing behind it is a two-hour cooldown
  // that game/cooldowns.h says is a per-boot RAM table while the clock is
  // CAL_UNSET. So the burst pays nothing there.
  EncounterResult r;
  memset(&r, 0, sizeof r);
  r.outcome     = (uint8_t)ENC_OUT_SPECIAL;
  r.event_id    = 3u;
  r.event_kind  = (uint8_t)SPEV_XP_BURST;
  r.event_value = 25u;
  CHECK_EQ(encounter_special_xp(r, (uint8_t)CAL_UNSET), 0);
  // ...and every trustworthy calibration state pays.
  CHECK(encounter_special_xp(r, (uint8_t)CAL_ESTIMATED) > 0);
  CHECK(encounter_special_xp(r, (uint8_t)CAL_USER) > 0);
  CHECK(encounter_special_xp(r, (uint8_t)CAL_PHONE) > 0);

  // AND THE EVENT ITSELF IS NOT SUPPRESSED. Withholding the award is the rule;
  // hiding the encounter would be a different (and worse) one, because the roll
  // has to stay a pure function of its inputs.
  EncounterInput in = mk_in((uint8_t)NET_CAT_HIDDEN);
  int specials = 0;
  for (uint32_t k = 0; k < 4000u; ++k) {
    in.net_hash = 1u + k * 2654435761u;
    EncounterResult e;
    CHECK(encounter_roll(in, e));
    if (e.outcome == (uint8_t)ENC_OUT_SPECIAL) specials++;
  }
  CHECK(specials > 0);
}

TEST(encounter_event_of_refuses_everything_that_is_not_a_real_special) {
  EncounterResult r;
  memset(&r, 0, sizeof r);
  r.outcome = (uint8_t)ENC_OUT_SPECIAL;
  r.event_id = 0u;
  CHECK(encounter_event_of(r) == nullptr);
  r.event_id = (uint8_t)(SPECIAL_EVENT_COUNT + 1u);
  CHECK(encounter_event_of(r) == nullptr);
  r.event_id = 1u;
  CHECK(encounter_event_of(r) != nullptr);
  r.outcome = (uint8_t)ENC_OUT_WILD;
  CHECK(encounter_event_of(r) == nullptr);
}

// =============================================================================
//  6. THE CORRUPTION EVENT'S STATUS AND ITS DEADLINE
//
//  The SPECIAL outcome's second kind. P5-C3 is the FIRST code in the tree that
//  can set PBS_CORRUPTED at all - measured: no writer anywhere in src/ before
//  this chunk - so the status, its 24 h deadline and the clock rule that guards
//  it are tested here, where they are produced. P9-C5 grows game/corruption.cpp
//  with the EFFECTS and keeps these.
// =============================================================================
static void mk_pebble(PebbleInstance& p, uint8_t species, uint8_t level)
{
  memset(&p, 0, sizeof p);
  p.magic      = (uint16_t)PEBBLE_MAGIC;
  p.layout_ver = (uint8_t)PEBBLE_LAYOUT_VER;
  p.species_id = species;
  p.id         = 0x5EED0003u;
  p.level      = level;
}

TEST(corrupting_a_pebble_arms_a_twenty_four_hour_deadline_and_it_expires_on_time) {
  PebbleInstance p;
  mk_pebble(p, 1, 5);
  const uint32_t t0 = 1700000000u;
  CHECK(!cor_is_corrupted(p));

  CHECK(cor_apply(p, t0, (uint8_t)CAL_USER));
  CHECK(cor_is_corrupted(p));
  CHECK_EQ(p.corrupt_until_epoch, t0 + (uint32_t)CORRUPT_DURATION_S);
  CHECK_EQ(cor_left_s(p, t0, (uint8_t)CAL_USER), (uint32_t)CORRUPT_DURATION_S);

  // THE BOUNDARY, BOTH SIDES. One second before it is due, nothing happens.
  CHECK(!cor_expire(p, t0 + (uint32_t)CORRUPT_DURATION_S - 1u, (uint8_t)CAL_USER));
  CHECK(cor_is_corrupted(p));
  CHECK_EQ(cor_left_s(p, t0 + (uint32_t)CORRUPT_DURATION_S - 1u, (uint8_t)CAL_USER), 1u);
  // Exactly on the tick, it ends - and takes the deadline with it.
  CHECK(cor_expire(p, t0 + (uint32_t)CORRUPT_DURATION_S, (uint8_t)CAL_USER));
  CHECK(!cor_is_corrupted(p));
  CHECK_EQ(p.corrupt_until_epoch, 0u);
  // ...and expiring again is a no-op rather than a second event.
  CHECK(!cor_expire(p, t0 + (uint32_t)CORRUPT_DURATION_S + 1u, (uint8_t)CAL_USER));
  // The duration really is a day, which is what spec section 55 asks for.
  CHECK_EQ((uint32_t)CORRUPT_DURATION_S, 86400u);
}

TEST(an_uncalibrated_clock_can_neither_arm_nor_expire_a_corruption) {
  // game/cooldowns.h met the same problem and answered it with a per-boot RAM
  // table; corruption answers it by not arming at all, because a 24 h effect
  // has nowhere to live on a device with no idea what 24 h is.
  PebbleInstance p;
  mk_pebble(p, 1, 5);
  CHECK(!cor_apply(p, 3600u, (uint8_t)CAL_UNSET));
  CHECK(!cor_is_corrupted(p));
  CHECK_EQ(p.corrupt_until_epoch, 0u);

  // POSITIVE CONTROL: the same call with a trustworthy clock arms it.
  CHECK(cor_apply(p, 3600u, (uint8_t)CAL_ESTIMATED));
  CHECK(cor_is_corrupted(p));

  // And an uptime estimate may not EXPIRE a real deadline either: a boot after
  // one was armed reads a small number, and expiring on it would clear a 24 h
  // status in seconds.
  p.corrupt_until_epoch = 1700000000u + (uint32_t)CORRUPT_DURATION_S;
  CHECK(!cor_expire(p, 99999999u, (uint8_t)CAL_UNSET));
  CHECK(cor_is_corrupted(p));
  CHECK_EQ(cor_left_s(p, 99999999u, (uint8_t)CAL_UNSET), 0u);  // and reports nothing
}

TEST(re_corrupting_refreshes_the_deadline_and_never_shortens_it) {
  PebbleInstance p;
  mk_pebble(p, 1, 5);
  const uint32_t t0 = 1700000000u;
  CHECK(cor_apply(p, t0, (uint8_t)CAL_USER));
  const uint32_t first = p.corrupt_until_epoch;

  // Later: the deadline moves OUT.
  CHECK(cor_apply(p, t0 + 3600u, (uint8_t)CAL_USER));
  CHECK(p.corrupt_until_epoch > first);
  const uint32_t second = p.corrupt_until_epoch;

  // A caller handing back an OLDER clock must not shorten it - one fact, one
  // representation, refreshed rather than stacked (the DOT rule from
  // game/battle.cpp).
  CHECK(cor_apply(p, t0 - 7200u, (uint8_t)CAL_USER));
  CHECK_EQ(p.corrupt_until_epoch, second);
  CHECK(cor_is_corrupted(p));
}

TEST(the_cure_needs_no_clock_and_a_corrupted_pebble_with_no_deadline_is_freed) {
  PebbleInstance p;
  mk_pebble(p, 1, 5);
  CHECK(cor_apply(p, 1700000000u, (uint8_t)CAL_USER));
  // The item route works on an uncalibrated device: the cure must never be the
  // thing that gets stuck (spec section 47).
  CHECK(cor_clear(p));
  CHECK(!cor_is_corrupted(p));
  CHECK_EQ(p.corrupt_until_epoch, 0u);
  CHECK(!cor_clear(p));                        // idempotent, and says so

  // A save carrying the bit with NO deadline cannot be produced by this tree -
  // P5-C3 always writes both - but if one ever arrives it is freed rather than
  // left permanently ill.
  p.status |= (uint8_t)PBS_CORRUPTED;
  p.corrupt_until_epoch = 0u;
  CHECK(cor_expire(p, 1700000000u, (uint8_t)CAL_USER));
  CHECK(!cor_is_corrupted(p));
}

TEST(an_empty_slot_is_never_ill) {
  PebbleInstance p;
  memset(&p, 0, sizeof p);
  CHECK(!cor_apply(p, 1700000000u, (uint8_t)CAL_USER));
  CHECK(!cor_is_corrupted(p));
  CHECK(!cor_clear(p));
  CHECK(!cor_expire(p, 1700000000u, (uint8_t)CAL_USER));
  CHECK_EQ(p.corrupt_until_epoch, 0u);
}
