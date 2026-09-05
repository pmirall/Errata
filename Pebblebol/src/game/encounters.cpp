// =============================================================================
//  PEBBLEBOL - game/encounters.cpp
//  See encounters.h. PURE translation unit.
// =============================================================================
#include "encounters.h"

#include <string.h>

#include "../core/rng.h"
#include "species.h"

// -----------------------------------------------------------------------------
//  THE SEED. FNV-1a 32 over the input fields, in a fixed order.
//
//  Not a home-made shuffle: the same mix core/crc16 and networking/
//  net_classify.cpp already use, so there is one hash function in the firmware
//  and its avalanche is already measured (tests/test_exploration_hash.cpp puts
//  4,000 inputs through it). Every field of EncounterInput goes in, so changing
//  ANY of them - a different network, a different six hours, a different device,
//  a different signal, a different Box - gives a different encounter, and
//  repeating all of them gives the same one.
//
//  The four u32s are folded little-endian byte by byte rather than as words, so
//  the answer does not depend on the machine that computes it: a host test and
//  the device must agree or the test proves nothing about the device.
// -----------------------------------------------------------------------------
static inline uint32_t fnv_u8(uint32_t h, uint8_t b)
{
  h ^= (uint32_t)b;
  return (uint32_t)(h * 0x01000193u);
}

static inline uint32_t fnv_u32(uint32_t h, uint32_t v)
{
  h = fnv_u8(h, (uint8_t)(v & 0xFFu));
  h = fnv_u8(h, (uint8_t)((v >> 8) & 0xFFu));
  h = fnv_u8(h, (uint8_t)((v >> 16) & 0xFFu));
  return fnv_u8(h, (uint8_t)((v >> 24) & 0xFFu));
}

static uint32_t encounter_seed(const EncounterInput& in)
{
  uint32_t h = 0x811C9DC5u;
  h = fnv_u32(h, in.net_hash);
  h = fnv_u32(h, in.bucket);
  h = fnv_u32(h, in.device_seed);
  h = fnv_u8(h, in.category);
  h = fnv_u8(h, (uint8_t)in.rssi);      // two's complement, both ends the same
  h = fnv_u8(h, in.active_level);
  h = fnv_u8(h, in.progress);
  // rare_bonus_pm is DELIBERATELY ABSENT from the seed. See encounters.h: it
  // must improve the answer, not reshuffle it, and folding it in here would
  // also make one network's encounter change inside its own six-hour bucket
  // whenever the player's activity score moved. It is consumed by its own
  // stage, below, exactly once.
  return h;
}

// -----------------------------------------------------------------------------
//  ONE DRAW PER STAGE, AND EACH STAGE IS INDEPENDENT OF THE OTHERS.
//
//  THIS IS A FIX, NOT A FLOURISH, AND IT WAS MEASURED. The first version took
//  successive steps of ONE xorshift32 - pick_row(rng_next), then
//  special_pick_event(rng_next) - which looks obviously fine and is not: the
//  SPECIAL branch is only reached when the FIRST draw lands in a 4-to-10 wide
//  band mod 100, and the second draw is a deterministic function of the first,
//  so conditioning on that band leaves the second draw's low bits badly
//  structured. Measured over 8,000 scans per category: HIDDEN's four special
//  events, weighted 20/30/20/30, came out 53/32/0/15 percent - event 3 was
//  UNREACHABLE at two of the six categories, and the membership-only test
//  passed anyway because event 1 is in every category's list.
//
//  So every stage gets its OWN seed: the encounter seed folded with a one-byte
//  tag through the same FNV-1a this file already uses, then one xorshift step.
//  The stages are no longer successive states of one generator, so conditioning
//  on the outcome says nothing about the payload.
// -----------------------------------------------------------------------------
enum EncStage : uint8_t {
  ENC_STAGE_OUTCOME = 0x01,
  ENC_STAGE_SPECIES = 0x02,
  ENC_STAGE_LEVEL   = 0x03,
  ENC_STAGE_ITEM    = 0x04,
  ENC_STAGE_EVENT   = 0x05,
  ENC_STAGE_RARE    = 0x06     // P6-C2, and it obeys the same rule as the rest
};

static uint32_t stage_draw(uint32_t seed, uint8_t tag)
{
  uint32_t h = 0x811C9DC5u;
  h = fnv_u32(h, seed);
  h = fnv_u8(h, tag);
  Rng r;
  rng_init(r, h);
  return rng_next(r);
}

uint32_t encounter_bucket(uint32_t now_epoch)
{
  return (uint32_t)(now_epoch / (uint32_t)ENCOUNTER_BUCKET_S);
}

// -----------------------------------------------------------------------------
//  THE OUTCOME ROW. One walk over the category's rows against a 0..99 draw.
//
//  The weights sum to exactly 100 by a generated static_assert, so there is no
//  normalisation step here to get wrong - which is the reason the pack states
//  that invariant rather than merely satisfying it.
// -----------------------------------------------------------------------------
static const EncounterRow* pick_row(uint8_t category, uint32_t roll)
{
  const uint32_t target = roll % (uint32_t)ENCOUNTER_WEIGHT_TOTAL;
  uint32_t acc = 0;
  for (uint8_t i = 0; i < ENCOUNTER_ROW_COUNT; ++i) {
    const EncounterRow& r = ENCOUNTER_TABLE[i];
    if (r.category != category) continue;
    acc += r.weight;
    if (acc > target) return &r;
  }
  // Unreachable while the weights sum to 100 for every category, which is a
  // static_assert. Returning null rather than the last row means a table that
  // somehow did not would show up as NOTHING and not as a silently biased pick.
  return nullptr;
}

// The SPECIAL half of the two-stage pick. Byte for byte the shape of
// game/species.cpp's item_pick_drop(), and deliberately so: the two outcomes
// that need a second stage are resolved the same way, so neither can drift into
// a rule the other one does not have.
static uint8_t special_pick_event(uint8_t category, uint32_t roll)
{
  if (category >= (uint8_t)NET_CAT_COUNT) return 0u;
  uint32_t total = 0;
  for (uint8_t i = 0; i < SPECIAL_DROP_ROW_COUNT; ++i)
    if (SPECIAL_DROP_TABLE[i].category == category)
      total += SPECIAL_DROP_TABLE[i].weight;
  if (total == 0u) return 0u;

  const uint32_t target = roll % total;
  uint32_t acc = 0;
  for (uint8_t i = 0; i < SPECIAL_DROP_ROW_COUNT; ++i) {
    if (SPECIAL_DROP_TABLE[i].category != category) continue;
    acc += SPECIAL_DROP_TABLE[i].weight;
    if (acc > target) return SPECIAL_DROP_TABLE[i].event_id;
  }
  return 0u;
}

// -----------------------------------------------------------------------------
//  THE RARE-BONUS PROMOTION. Same category, same outcome, next rarity band up.
//
//  TOTAL BY CONSTRUCTION: the walk visits every row once, only ever accepts a
//  WILD row of the SAME category whose rarity_min is strictly greater, and
//  returns the row it was given when there is none. So the answer is never null,
//  never a different category and never a different outcome - which is what
//  makes a_wild_species_always_matches_its_rows_category_mask_and_rarity_band
//  hold under any bonus without that case needing to know this function exists.
//
//  "SMALLEST rarity_min strictly above" and not "the rarest row in the
//  category": a promotion is one step, so a full activity day moves common ->
//  uncommon -> rare through repeated rolls rather than jumping the whole ladder
//  in one, and the shipped table's duplicate top-band rows (PUBLIC, BUSINESS,
//  OPEN and HIDDEN each carry two rows at rarity 2) are already the identity.
// -----------------------------------------------------------------------------
static const EncounterRow* rarer_wild_row(const EncounterRow* row)
{
  const EncounterRow* best = nullptr;
  for (uint8_t i = 0; i < ENCOUNTER_ROW_COUNT; ++i) {
    const EncounterRow& r = ENCOUNTER_TABLE[i];
    if (r.category != row->category) continue;
    if (r.outcome  != (uint8_t)ENC_OUT_WILD) continue;
    if (r.rarity_min <= row->rarity_min) continue;
    if (best == nullptr || r.rarity_min < best->rarity_min) best = &r;
  }
  return (best != nullptr) ? best : row;
}

// clamp(active +/- ENCOUNTER_LEVEL_SPREAD, 1..XP_LEVEL_MAX).
//
// THE CLAMP IS AT BOTH ENDS AND THE SPREAD IS SYMMETRIC, which is what keeps
// every wild level inside validate_pebble()'s band: a level-1 active Pebble
// cannot produce a level -1 wild one and a level-30 one cannot produce a 32.
// tests/test_capture.cpp asserts VR_OK over the whole roster crossed with this
// band, which is the only thing that would catch a clamp landing outside it.
static uint8_t wild_level(uint8_t active_level, uint32_t roll)
{
  int32_t base = (int32_t)active_level;
  if (base < 1) base = 1;
  if (base > (int32_t)XP_LEVEL_MAX) base = (int32_t)XP_LEVEL_MAX;
  const int32_t span = 2 * (int32_t)ENCOUNTER_LEVEL_SPREAD + 1;
  int32_t lv = base - (int32_t)ENCOUNTER_LEVEL_SPREAD + (int32_t)(roll % (uint32_t)span);
  if (lv < 1) lv = 1;
  if (lv > (int32_t)XP_LEVEL_MAX) lv = (int32_t)XP_LEVEL_MAX;
  return (uint8_t)lv;
}

bool encounter_roll(const EncounterInput& in, EncounterResult& out)
{
  memset(&out, 0, sizeof out);
  out.outcome = (uint8_t)ENC_OUT_NOTHING;

  // net_hash 0 is the Cooldown table's "empty row" marker and the scanner folds
  // it away (NET_HASH_NEVER_ZERO); a category outside the table has no rows at
  // all. Both are caller bugs, and neither may produce a creature.
  if (in.net_hash == 0u) return false;
  if (in.category >= (uint8_t)NET_CAT_COUNT) return false;

  const uint32_t seed = encounter_seed(in);
  const EncounterRow* row = pick_row(in.category, stage_draw(seed, ENC_STAGE_OUTCOME));
  if (row == nullptr) return true;              // NOTHING, and the table is wrong

  // THE ACTIVITY BONUS (P6-C2). Its own stage, its own seed, consumed after the
  // outcome is decided and only for a WILD row - so it can never turn a NOTHING
  // into a creature, and the outcome split is the same at every permille. The
  // clamp is here rather than at the caller because a pure function must not
  // trust its input: a screen that passed 60000 gets ENC_RARE_BONUS_MAX_PM.
  if (row->outcome == (uint8_t)ENC_OUT_WILD && in.rare_bonus_pm != 0u) {
    const uint32_t pm = (in.rare_bonus_pm > (uint16_t)ENC_RARE_BONUS_MAX_PM)
                            ? (uint32_t)ENC_RARE_BONUS_MAX_PM
                            : (uint32_t)in.rare_bonus_pm;
    if ((stage_draw(seed, ENC_STAGE_RARE) % 1000u) < pm) row = rarer_wild_row(row);
  }

  out.outcome = row->outcome;
  switch ((EncounterOutcome)row->outcome) {
    case ENC_OUT_WILD: {
      const uint16_t pool = species_spawn_weight_in(row->category, row->rarity_min,
                                                    row->rarity_max);
      // Guarded at compile time by encounter_wild_rows_have_a_pool(). If it
      // ever were empty the honest answer is NOTHING, never a species 0 the
      // caller would look up and get nullptr for.
      if (pool == 0u) { out.outcome = (uint8_t)ENC_OUT_NOTHING; break; }
      out.species_id = species_pick_by_weight(row->category, row->rarity_min,
                                              row->rarity_max,
                                              (uint16_t)(stage_draw(seed, ENC_STAGE_SPECIES) & 0xFFFFu));
      if (out.species_id == 0u) { out.outcome = (uint8_t)ENC_OUT_NOTHING; break; }
      out.level = wild_level(in.active_level, stage_draw(seed, ENC_STAGE_LEVEL));
      break;
    }
    case ENC_OUT_ITEM: {
      out.item_id = item_pick_drop(row->category, row->rarity_min, row->rarity_max,
                                   (uint16_t)(stage_draw(seed, ENC_STAGE_ITEM) & 0xFFFFu));
      // Guarded at compile time by encounter_item_rows_have_a_drop(), which did
      // not exist before this chunk - the ITEM outcome's resolvability was
      // checked only by the pack's Python gate, and tools/check.sh skips that
      // one with a word when python3 is missing.
      if (out.item_id == 0u) out.outcome = (uint8_t)ENC_OUT_NOTHING;
      break;
    }
    case ENC_OUT_SPECIAL: {
      out.event_id = special_pick_event(row->category, stage_draw(seed, ENC_STAGE_EVENT));
      const SpecialEvent* ev = (out.event_id >= 1u && out.event_id <= SPECIAL_EVENT_COUNT)
                                   ? &SPECIAL_EVENTS[out.event_id - 1u]
                                   : nullptr;
      if (ev == nullptr) { out.outcome = (uint8_t)ENC_OUT_NOTHING; out.event_id = 0u; break; }
      out.event_kind  = ev->kind;
      out.event_value = ev->value;
      break;
    }
    case ENC_OUT_NOTHING:
    default:
      break;
  }
  return true;
}

uint16_t encounter_special_xp(const EncounterResult& r, uint8_t cal)
{
  if (r.outcome != (uint8_t)ENC_OUT_SPECIAL) return 0u;
  if (r.event_kind != (uint8_t)SPEV_XP_BURST) return 0u;
  // THE WITHHOLDING. See encounters.h: this source is unmetered, and the only
  // thing that makes an unmetered source farm-proof is the two-hour cooldown,
  // which is a per-boot RAM table while the clock is CAL_UNSET.
  if (cal == (uint8_t)CAL_UNSET) return 0u;
  return (uint16_t)(r.event_value * (uint16_t)SPECIAL_XP_SCALE);
}

const SpecialEvent* encounter_event_of(const EncounterResult& r)
{
  if (r.outcome != (uint8_t)ENC_OUT_SPECIAL) return nullptr;
  if (r.event_id < 1u || r.event_id > SPECIAL_EVENT_COUNT) return nullptr;
  return &SPECIAL_EVENTS[r.event_id - 1u];
}
