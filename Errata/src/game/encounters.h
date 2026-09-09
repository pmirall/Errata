// =============================================================================
//  ERRATA - game/encounters.h
//  THE ENCOUNTER ROLL (spec sections 20 and 22, plan P5-C3).
//
//  One scanned access point in, one of {WILD, ITEM, SPECIAL, NOTHING} out, off
//  data/encounter_table.h's per-category weights - and, for the three outcomes
//  that need one, a resolved payload: a species and a level, an item id, or a
//  special event.
//
// -----------------------------------------------------------------------------
//  DETERMINISM, AND WHY THIS MODULE DRAWS FROM NO NAMED STREAM
// -----------------------------------------------------------------------------
//  Spec section 20: "wild encounter generation must be deterministic from a
//  seed where practical", and it lists the inputs - network identity, category,
//  signal strength, time bucket, device random seed, player progress. Every one
//  of those is a field of EncounterInput below, and the roll is a PURE FUNCTION
//  of them: encounter_roll() is total, has no state, reads no clock and calls
//  no global RNG.
//
//  THE PLAN'S BULLET SAYS `RNG_ENCOUNTER` AND THIS IS A DEVIATION FROM IT, on
//  purpose and with a reason that can be checked. Drawing the outcome from the
//  shared RNG_ENCOUNTER stream would make two scans of the same access point in
//  the same six-hour bucket answer DIFFERENTLY - which is the opposite of what
//  section 20 asks for - and it would make the stream POSITION part of the
//  answer, so a host test could pin a seed and still not pin an outcome. The
//  encounter is seeded instead from a mix of its own inputs (core/rng.h's Rng
//  is initialised locally, exactly as game/battle.cpp seeds BattleState.rng
//  from one u32), and RNG_ENCOUNTER stays what it always was: the stream the
//  CAPTURE roll draws from (game/capture.h), where a re-roll must be a real one.
//
//  The consequence, stated because it is a design choice a player can feel:
//  rescanning one network inside its bucket shows the SAME encounter. The two
//  hour ENCOUNTER_COOLDOWN_S normally hides that; a cooldown cleared by a
//  reboot on an uncalibrated device does not, and the honest answer there is
//  that the player sees the same wild Bug again rather than a fresh roll.
//
// -----------------------------------------------------------------------------
//  THE SPECIAL PAYLOAD (the debt this chunk discharges)
// -----------------------------------------------------------------------------
//  SPECIAL was 4-10 % of every scan with NOTHING behind it: no roster, no ids,
//  no weights. tools/content/specials.json is that table now, emitted as
//  SPECIAL_EVENTS[] and SPECIAL_DROP_TABLE[] beside the item drops, guarded at
//  compile time by encounter_special_rows_have_an_event() and picked by the
//  same two-stage walk. Two kinds and no third invented here:
//
//    SPEV_XP_BURST    pays SPECIAL_XP_SCALE * value experience...
//    SPEV_CORRUPTION  sets PBS_CORRUPTED on the active Bug for 24 h
//                     (game/corruption.h owns the status and the deadline)
//
//  ...AND THE XP BURST IS WITHHELD ON AN UNCALIBRATED DEVICE. That choice is
//  P5-C3's to make in writing, and this is the writing. game/xp.h's
//  XP_SRC_CAPTURE and XP_SRC_ITEM are unmetered because "a capture consumes an
//  encounter, an item consumes the item"; a SPECIAL burst consumes NEITHER, so
//  the only thing between it and a farm is the two-hour cooldown - and
//  game/cooldowns.h says plainly that while gt_cal_state() is CAL_UNSET the
//  cooldown table is per-boot, so a reboot frees every network. Metering it
//  properly is not available: Inventory.xp_ledger is four persisted bytes and
//  the static_assert in xp.h pins the metered sources to exactly those four, so
//  a fifth would be a save-schema change for an award nobody can spend. The
//  cheap, honest answer is to pay nothing when the clock cannot be trusted -
//  the event still fires, the screen still shows it, and XP_SRC_SPECIAL is what
//  it would have been paid through. encounter_special_xp() is that rule, in one
//  place, drivable from a host test.
//
// -----------------------------------------------------------------------------
//  PURE MODULE. stdint, core/rng.h, the generated content tables, data/balance.h
//  and game/species.h. No Arduino, no clock, no heap, no float, no I/O.
//  tests/test_encounters.cpp compiles it directly.
// =============================================================================
#ifndef ER_GAME_ENCOUNTERS_H
#define ER_GAME_ENCOUNTERS_H

#include <stdint.h>

#include "../data/balance.h"              // ENCOUNTER_BUCKET_S, XP_LEVEL_MAX
#include "../data/encounter_table.h"      // ENCOUNTER_TABLE, SPECIAL_EVENTS
#include "../persistence/save_schema.h"   // TimeCal

// How far either side of the active Bug's level a wild one may be found.
// Spec section 22 gives no number; plan P5-C3 says clamp(active +/- 2, 1..30),
// and this is that 2 with a name so the test and the code cannot disagree.
#define ENCOUNTER_LEVEL_SPREAD  2

// -----------------------------------------------------------------------------
//  THE INPUT. Section 20's list, minus the two it cannot use.
//
//  `cooldown state` is NOT here: whether a network may be explored at all is
//  game/cooldowns.h's question and it is asked BEFORE this one, so folding it
//  in would give one fact two owners. `day` is the bucket.
//
//  net_hash is the salted, per-device identity networking/net_classify.h
//  produces - never an address, never a name (spec section 44).
// -----------------------------------------------------------------------------
struct EncounterInput {
  uint32_t net_hash;      // the scan's abstract identity; 0 is refused
  uint32_t bucket;        // now_epoch / ENCOUNTER_BUCKET_S - see encounter_bucket()
  uint32_t device_seed;   // gs_device_id(): stable per device, so two units
                          // standing side by side do not see the same creature
  uint8_t  category;      // NetCategory ordinal, < NET_CAT_COUNT
  int8_t   rssi;          // dBm; part of the seed, never part of the weights
  uint8_t  active_level;  // the level the wild one is clamped around, 1..30
  uint8_t  progress;      // Bugs filed, 0..BOX_SLOTS. Seed input only, for
                          // now: nothing in the pack tunes an outcome by it, so
                          // it does not silently become a difficulty curve.
  // P6-C2. THE FIRST INPUT THAT MOVES A WEIGHT RATHER THAN THE SEED, and the
  // difference is the whole reason it is a separate field and a separate
  // stage. `progress` above is documented as tuning NOTHING: it stirs the mix
  // and a membership assertion can see it. This one changes WHICH ROW pays, so
  // only a DISTRIBUTION can see it, and tests/test_encounters.cpp measures one.
  //
  // Permille, 0..ENC_RARE_BONUS_MAX_PM (data/balance.h), clamped by the roll so
  // a caller cannot exceed the cap by passing a bigger number. ZERO IS THE
  // EXACT IDENTITY - measured, 120,000 rolls per category byte-identical to the
  // roll before this field existed - which is what lets every phase-5
  // distribution case keep asserting the numbers it was written against.
  //
  // IT IS DELIBERATELY NOT FOLDED INTO encounter_seed(). Folding it in would
  // make a bigger bonus RESHUFFLE rather than IMPROVE, so no test could assert
  // monotonicity - and monotonicity is the only property that makes this a
  // bonus rather than a stirring stick. It would also break the promise at the
  // top of this header that rescanning one network inside its bucket shows the
  // same encounter, since the bonus moves with the player's day.
  uint16_t rare_bonus_pm;
};

// THE ASSERT THAT MAKES THE NEXT FIELD IMPOSSIBLE TO FORGET, and it is here
// because its absence was MEASURED: an eighth field added to this struct and
// left unread passed tests/test_encounters.cpp 22/22, because
// every_declared_input_reaches_the_answer enumerates the fields BY HAND. It
// cannot see a field nobody told it about. This assert can: adding one changes
// the size, the build stops, and whoever adds it is standing in front of the
// case list. Update BOTH together or neither.
static_assert(sizeof(EncounterInput) == 20, "EncounterInput gained or lost a field - "
              "add it to every_declared_input_reaches_the_answer's perturbation "
              "list in tests/test_encounters.cpp before changing this number");

// -----------------------------------------------------------------------------
//  THE ANSWER. 8 B, and every field is 0 unless the outcome names it, so a
//  caller that reads the wrong one gets nothing rather than a stale value.
// -----------------------------------------------------------------------------
struct EncounterResult {
  uint8_t  outcome;       // EncounterOutcome. Always < ENC_OUT_COUNT.
  uint8_t  species_id;    // WILD
  uint8_t  level;         // WILD, 1..XP_LEVEL_MAX
  uint8_t  item_id;       // ITEM
  uint8_t  event_id;      // SPECIAL, into SPECIAL_EVENTS
  uint8_t  event_kind;    // SPECIAL, SpecialKind
  uint16_t event_value;   // SPECIAL, the event row's raw value
};
static_assert(sizeof(EncounterResult) == 8, "EncounterResult layout drifted");

// -----------------------------------------------------------------------------
// encounter_bucket(now_epoch) -> the six-hour bucket `now_epoch` falls in.
//   Named here so the screen, the test and the roll cannot each divide by their
//   own constant.
// -----------------------------------------------------------------------------
uint32_t encounter_bucket(uint32_t now_epoch);

// -----------------------------------------------------------------------------
// encounter_roll(in, out) -> was a roll made?
//
//   false, with `out` cleared to NOTHING, for net_hash 0 or a category outside
//   the table: both are caller bugs and neither may hand back a creature.
//
//   THE RARE-BONUS PROMOTION (P6-C2). When the row that won is a WILD row and
//   in.rare_bonus_pm is non-zero, one further independent draw promotes that row
//   to the SAME CATEGORY's next rarer WILD row - the one with the smallest
//   rarity_min strictly above this row's. The walk is bounded and total: with no
//   rarer row in the category the original row stands, so the promotion is the
//   identity at the top band and the answer is always a resolved row of the same
//   category. The OUTCOME never changes (a WILD row is promoted to a WILD row),
//   which is what keeps the WILD/ITEM/SPECIAL/NOTHING split - and section 22's
//   15 % NOTHING floor - invariant under any bonus.
//
//   Otherwise `out` is the resolved encounter. WILD always names a species the
//   category can spawn inside the row's rarity band (the generated
//   encounter_wild_rows_have_a_pool() is why that cannot fail), ITEM always
//   names an item inside the row's band (encounter_item_rows_have_a_drop(), new
//   in this chunk), and SPECIAL always names an event
//   (encounter_special_rows_have_an_event(), also new). An outcome that could
//   not be resolved would be a table bug rather than a runtime one, and the
//   three static_asserts are what make it a build failure instead.
// -----------------------------------------------------------------------------
bool encounter_roll(const EncounterInput& in, EncounterResult& out);

// -----------------------------------------------------------------------------
// encounter_special_xp(r, cal) -> XP the SPECIAL event pays, 0 for anything
//   else and 0 while `cal` is CAL_UNSET. See the banner: the withholding is the
//   whole anti-farm argument for an unmetered source, and it lives in one
//   function so a screen cannot forget it.
// -----------------------------------------------------------------------------
uint16_t encounter_special_xp(const EncounterResult& r, uint8_t cal);

// The event row behind a SPECIAL result, or nullptr. Screens need its name.
const SpecialEvent* encounter_event_of(const EncounterResult& r);

#endif  // ER_GAME_ENCOUNTERS_H
