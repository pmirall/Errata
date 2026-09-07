// =============================================================================
//  PEBBLEBOL host test - test_inventory.cpp
//  THE BAG AND WHAT AN ITEM DOES (game/inventory.h, spec section 24, P5-C4).
//
//  Two halves. The first is the bag - add, use, underflow, the seven-slot cap,
//  the saturation - and the second is the one the carried-forward debt is
//  about: every ItemKlass has an arm that DOES something, and this file drives
//  all five and asserts the effect on the Pebble rather than the return value.
//
//  THE ONE THAT WOULD OTHERWISE NOT BE ABLE TO FAIL: a CARE case that only
//  asserted "IU_OK" would pass against an arm that consumed the item and
//  changed nothing. Every arm below asserts the Pebble BEFORE and AFTER, and
//  every refusal asserts that the item is STILL IN THE BAG.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "data/items_table.h"
#include "game/corruption.h"
#include "game/evolution.h"
#include "game/inventory.h"
#include "game/species.h"
#include "game/xp.h"

static Inventory g_inv;

static void fresh_bag(void)
{
  inv_begin(g_inv);
  inv_mod_clear();
  CHECK_EQ(inv_slots_used(g_inv), 0);
}

static void mk_pebble(PebbleInstance& p, uint8_t species, uint8_t level)
{
  memset(&p, 0, sizeof p);
  p.magic      = (uint16_t)PEBBLE_MAGIC;
  p.layout_ver = (uint8_t)PEBBLE_LAYOUT_VER;
  p.species_id = species;
  p.id         = 0x5EED0001u;
  p.level      = level;
  const SpeciesDef* sp = species_get(species);
  p.hp_cur = sp ? xp_hp_max(sp->base_hp, level) : 0u;
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) p.care[i] = PB_CARE_MILLI_MAX;
}

// The id of the first item of a klass, so a case names a klass and not a
// literal that a content edit could quietly retarget.
static uint8_t first_of(uint8_t klass)
{
  for (uint8_t i = 0; i < ITEM_COUNT; ++i)
    if (ITEMS_TABLE[i].klass == klass) return ITEMS_TABLE[i].id;
  return 0u;
}

// =============================================================================
//  1. THE BAG
// =============================================================================
TEST(add_and_remove_are_exact_and_an_underflow_changes_nothing) {
  fresh_bag();
  CHECK_EQ(inv_add(g_inv, 1, 3), 3);
  CHECK_EQ(inv_count(g_inv, 1), 3);
  CHECK_EQ(inv_slots_used(g_inv), 1);
  CHECK_EQ(inv_add(g_inv, 1, 2), 2);           // stacks in the same slot
  CHECK_EQ(inv_count(g_inv, 1), 5);
  CHECK_EQ(inv_slots_used(g_inv), 1);

  CHECK(inv_remove(g_inv, 1, 2));
  CHECK_EQ(inv_count(g_inv, 1), 3);

  // ALL OR NOTHING. An underflow must not half-spend.
  CHECK(!inv_remove(g_inv, 1, 4));
  CHECK_EQ(inv_count(g_inv, 1), 3);
  CHECK(!inv_remove(g_inv, 1, 255));
  CHECK_EQ(inv_count(g_inv, 1), 3);
  CHECK(!inv_remove(g_inv, 2, 1));             // none held at all
  CHECK_EQ(inv_count(g_inv, 2), 0);
  CHECK(!inv_remove(g_inv, 1, 0));             // removing zero is not a use
  CHECK_EQ(inv_count(g_inv, 1), 3);

  // Emptying releases the slot, so seven KINDS is the cap and not seven
  // lifetime pickups.
  CHECK(inv_remove(g_inv, 1, 3));
  CHECK_EQ(inv_count(g_inv, 1), 0);
  CHECK_EQ(inv_slots_used(g_inv), 0);
}

TEST(a_bag_never_holds_a_non_item_and_a_slot_saturates_rather_than_wrapping) {
  fresh_bag();
  CHECK_EQ(inv_add(g_inv, 0, 1), 0);
  CHECK_EQ(inv_add(g_inv, (uint8_t)(ITEM_COUNT + 1u), 1), 0);
  CHECK_EQ(inv_add(g_inv, 255, 1), 0);
  CHECK_EQ(inv_add(g_inv, 1, 0), 0);
  CHECK_EQ(inv_slots_used(g_inv), 0);

  // SATURATION, NOT WRAP. A bag that empties on the 256th pickup is the classic
  // silent version of this bug, so the case drives it past 255 twice.
  CHECK_EQ(inv_add(g_inv, 1, 255), 255);
  CHECK_EQ(inv_count(g_inv, 1), 255);
  CHECK_EQ(inv_add(g_inv, 1, 1), 0);           // no room, nothing added
  CHECK_EQ(inv_count(g_inv, 1), 255);
  CHECK_EQ(inv_add(g_inv, 1, 200), 0);
  CHECK_EQ(inv_count(g_inv, 1), 255);
}

TEST(seven_kinds_is_the_cap_and_the_eighth_is_refused_without_disturbing_the_bag) {
  fresh_bag();
  CHECK(ITEM_COUNT > (uint8_t)INVENTORY_SLOTS);   // there are more kinds than slots
  for (uint8_t i = 1; i <= (uint8_t)INVENTORY_SLOTS; ++i)
    CHECK_EQ(inv_add(g_inv, i, 2), 2);
  CHECK_EQ(inv_slots_used(g_inv), (uint8_t)INVENTORY_SLOTS);

  const uint8_t extra = (uint8_t)(INVENTORY_SLOTS + 1u);
  CHECK_EQ(inv_add(g_inv, extra, 1), 0);
  CHECK_EQ(inv_count(g_inv, extra), 0);
  for (uint8_t i = 1; i <= (uint8_t)INVENTORY_SLOTS; ++i)
    CHECK_EQ(inv_count(g_inv, i), 2);           // nothing was evicted

  // A kind ALREADY held still stacks even with every slot taken.
  CHECK_EQ(inv_add(g_inv, 1, 3), 3);
  CHECK_EQ(inv_count(g_inv, 1), 5);

  // ...and freeing a slot makes room for the eighth kind.
  CHECK(inv_remove(g_inv, 2, 2));
  CHECK_EQ(inv_add(g_inv, extra, 1), 1);
  CHECK_EQ(inv_count(g_inv, extra), 1);
}

// =============================================================================
//  2. THE FIVE ARMS
// =============================================================================
TEST(an_xp_candy_really_pays_xp_and_is_consumed) {
  fresh_bag();
  const uint8_t id = first_of((uint8_t)ITEM_KLASS_XP_CANDY);
  CHECK(id != 0);
  CHECK_EQ(inv_add(g_inv, id, 2), 2);

  PebbleInstance p;
  mk_pebble(p, 1, 3);
  const uint8_t  lv0 = p.level;
  const uint16_t xp0 = p.xp;

  ItemEffect eff;
  CHECK_EQ(inv_use(g_inv, id, &p, 1000u, (uint8_t)CAL_USER, eff), (uint8_t)IU_OK);
  CHECK_EQ(eff.klass, (uint8_t)ITEM_KLASS_XP_CANDY);
  CHECK_EQ(eff.xp, item_xp_value(id));
  CHECK(eff.xp > 0);
  // THE PEBBLE MOVED - the claim, rather than the return value.
  CHECK(p.level > lv0 || p.xp > xp0);
  CHECK_EQ(inv_count(g_inv, id), 1);           // exactly one consumed
}

TEST(a_candy_at_the_top_of_the_curve_is_not_consumed) {
  fresh_bag();
  const uint8_t id = first_of((uint8_t)ITEM_KLASS_XP_CANDY);
  CHECK_EQ(inv_add(g_inv, id, 1), 1);

  PebbleInstance p;
  mk_pebble(p, 1, (uint8_t)XP_LEVEL_MAX);
  p.xp = 0;
  PebbleInstance before = p;

  ItemEffect eff;
  CHECK_EQ(inv_use(g_inv, id, &p, 1000u, (uint8_t)CAL_USER, eff), (uint8_t)IU_NO_EFFECT);
  CHECK_EQ(memcmp(&before, &p, sizeof p), 0);  // the creature is untouched
  CHECK_EQ(inv_count(g_inv, id), 1);           // and the item is still there
}

TEST(a_care_item_restores_the_stat_its_own_row_names_and_no_other) {
  // THE DEBT, ASSERTED: before P5-C4 the table said "care points restored" and
  // named no stat, so this case could not have been written at all.
  fresh_bag();
  // Parche is CARE_TGT_ALL; Antivirus is CARE_TGT_HEALTH plus a status mask.
  const ItemDef* all = nullptr;
  const ItemDef* one = nullptr;
  for (uint8_t i = 0; i < ITEM_COUNT; ++i) {
    const ItemDef& it = ITEMS_TABLE[i];
    if (it.klass != (uint8_t)ITEM_KLASS_CARE) continue;
    if (it.target == (uint8_t)CARE_TGT_ALL) all = &it;
    else if (one == nullptr)                one = &it;
  }
  CHECK(all != nullptr);
  CHECK(one != nullptr);
  if (!all || !one) return;

  // THE SINGLE-STAT ITEM MOVES EXACTLY ONE BAR.
  CHECK_EQ(inv_add(g_inv, one->id, 1), 1);
  PebbleInstance p;
  mk_pebble(p, 1, 5);
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) p.care[i] = 0;
  ItemEffect eff;
  CHECK_EQ(inv_use(g_inv, one->id, &p, 1000u, (uint8_t)CAL_USER, eff), (uint8_t)IU_OK);
  CHECK_EQ(eff.care_target, one->target);
  CHECK_EQ(eff.care_stats, 1);
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) {
    if (i == (uint8_t)(one->target - 1u)) {
      // value is PERCENT OF FULL - the unit the pack now states as a number.
      CHECK_EQ(p.care[i], (int32_t)one->value * CARE_MILLI_PER_PCT);
    } else {
      CHECK_EQ(p.care[i], 0);                 // and NO other bar moved
    }
  }

  // THE ALL ITEM MOVES FIVE.
  fresh_bag();
  CHECK_EQ(inv_add(g_inv, all->id, 1), 1);
  mk_pebble(p, 1, 5);
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) p.care[i] = 0;
  CHECK_EQ(inv_use(g_inv, all->id, &p, 1000u, (uint8_t)CAL_USER, eff), (uint8_t)IU_OK);
  CHECK_EQ(eff.care_target, (uint8_t)CARE_TGT_ALL);
  CHECK_EQ(eff.care_stats, (uint8_t)PB_CARE_COUNT);
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i)
    CHECK_EQ(p.care[i], (int32_t)all->value * CARE_MILLI_PER_PCT);
}

TEST(a_care_item_never_overfills_and_is_not_consumed_on_a_well_pebble) {
  fresh_bag();
  const uint8_t id = first_of((uint8_t)ITEM_KLASS_CARE);
  CHECK(id != 0);
  const ItemDef* it = item_get(id);
  CHECK(it != nullptr);
  if (!it) return;
  CHECK_EQ(inv_add(g_inv, id, 2), 2);

  PebbleInstance p;
  mk_pebble(p, 1, 5);                          // every bar already full
  PebbleInstance before = p;
  ItemEffect eff;
  CHECK_EQ(inv_use(g_inv, id, &p, 1000u, (uint8_t)CAL_USER, eff), (uint8_t)IU_NO_EFFECT);
  CHECK_EQ(memcmp(&before, &p, sizeof p), 0);
  CHECK_EQ(inv_count(g_inv, id), 2);           // NOT consumed

  // Nearly full: it works, and it clamps rather than overflowing.
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) p.care[i] = PB_CARE_MILLI_MAX - 1;
  CHECK_EQ(inv_use(g_inv, id, &p, 1000u, (uint8_t)CAL_USER, eff), (uint8_t)IU_OK);
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i)
    CHECK_EQ(p.care[i], (int32_t)PB_CARE_MILLI_MAX);
  CHECK_EQ(inv_count(g_inv, id), 1);
}

TEST(the_cure_item_clears_the_status_bits_its_own_column_names_and_the_deadline_with_them) {
  fresh_bag();
  // The item balance.json's CORRUPTION.cleared_by_item points at, found through
  // its OWN column rather than by id, so a content edit moves the case with it.
  const ItemDef* cure = nullptr;
  for (uint8_t i = 0; i < ITEM_COUNT; ++i)
    if (ITEMS_TABLE[i].klass == (uint8_t)ITEM_KLASS_CARE &&
        (ITEMS_TABLE[i].param & (uint8_t)PBS_CORRUPTED) != 0u)
      cure = &ITEMS_TABLE[i];
  CHECK(cure != nullptr);
  if (!cure) return;
  CHECK_EQ(inv_add(g_inv, cure->id, 1), 1);

  PebbleInstance p;
  mk_pebble(p, 1, 5);
  CHECK(cor_apply(p, 1700000000u, (uint8_t)CAL_USER));
  CHECK(cor_is_corrupted(p));
  CHECK(p.corrupt_until_epoch != 0u);
  p.status |= (uint8_t)PBS_SICK;
  p.care[CARE_HEALTH] = 0;

  ItemEffect eff;
  CHECK_EQ(inv_use(g_inv, cure->id, &p, 1700000100u, (uint8_t)CAL_USER, eff),
           (uint8_t)IU_OK);
  CHECK(!cor_is_corrupted(p));
  // THE DEADLINE GOES WITH THE BIT. Clearing one and leaving the other is a
  // timer with nothing to expire, which is why the arm goes through
  // game/corruption.h rather than masking the byte here.
  CHECK_EQ(p.corrupt_until_epoch, 0u);
  CHECK_EQ((uint8_t)(p.status & (uint8_t)PBS_SICK), 0);
  CHECK_EQ((uint8_t)(eff.status_cleared & (uint8_t)PBS_CORRUPTED), (uint8_t)PBS_CORRUPTED);
  CHECK_EQ((uint8_t)(eff.status_cleared & (uint8_t)PBS_SICK), (uint8_t)PBS_SICK);
  CHECK(p.care[CARE_HEALTH] > 0);
  CHECK_EQ(inv_count(g_inv, cure->id), 0);
}

// EVERY battle modifier, not just the first. INSTANCE TWENTY-ONE OF THIS
// PROJECT'S RECURRING DEFECT, FOUND IN THIS FILE AND MEASURED: the first
// version drove only first_of(BATTLE_MOD), which is Turbo Chip - and its target
// is ITEM_BSTAT_ATK, ordinal ZERO. Mutation M30 (arm the modifier with a
// hard-coded stat 0 instead of the table's) PASSED it, because the one row it
// drove agreed with the constant by accident. The loop is what makes the
// `target` column load-bearing: Escudo RAM's DEF is ordinal 1.
TEST(every_battle_modifier_is_consumed_armed_and_handed_out_exactly_once) {
  int mods = 0;
  for (uint8_t i = 0; i < ITEM_COUNT; ++i) {
    const ItemDef& it = ITEMS_TABLE[i];
    if (it.klass != (uint8_t)ITEM_KLASS_BATTLE_MOD) continue;
    mods++;
    fresh_bag();
    CHECK_EQ(inv_add(g_inv, it.id, 1), 1);
    CHECK(!inv_mod_armed());

    ItemEffect eff;
    // It needs no Pebble: the buff is armed for the next fight.
    CHECK_EQ(inv_use(g_inv, it.id, nullptr, 1000u, (uint8_t)CAL_USER, eff),
             (uint8_t)IU_OK);
    CHECK_EQ(inv_count(g_inv, it.id), 0);
    CHECK(inv_mod_armed());
    // THE STAT AND THE DURATION COME FROM THE TABLE - the second half of the
    // debt, and the reason this loops.
    CHECK_EQ(eff.mod_stat, it.target);
    CHECK_EQ(eff.mod_stages, it.value);
    CHECK_EQ(eff.mod_rounds, it.param);
    CHECK(eff.mod_rounds >= 1);

    InvBattleMod m;
    CHECK(inv_mod_take(m));
    CHECK_EQ(m.item_id, it.id);
    CHECK_EQ(m.stat, it.target);
    CHECK_EQ(m.stages, it.value);
    CHECK_EQ(m.rounds, it.param);
    // EXACTLY ONCE: a buff bought for one fight may not be spent on two.
    CHECK(!inv_mod_armed());
    InvBattleMod again;
    CHECK(!inv_mod_take(again));
    CHECK_EQ(again.item_id, 0);
  }
  CHECK(mods >= 2);
}

TEST(the_two_battle_modifiers_buff_different_stats) {
  // A table in which both mods named the same stat would satisfy every case
  // above; this is what makes the target column load-bearing.
  uint8_t stats[4] = { 0, 0, 0, 0 };
  int mods = 0;
  for (uint8_t i = 0; i < ITEM_COUNT; ++i)
    if (ITEMS_TABLE[i].klass == (uint8_t)ITEM_KLASS_BATTLE_MOD) {
      CHECK(ITEMS_TABLE[i].target <= (uint8_t)ITEM_BSTAT_SPD);
      stats[ITEMS_TABLE[i].target]++;
      mods++;
    }
  CHECK(mods >= 2);
  int distinct = 0;
  for (uint8_t i = 0; i < 4u; ++i) if (stats[i]) distinct++;
  CHECK(distinct >= 2);
}

TEST(the_evolution_key_is_offered_to_the_rules_and_never_spent_when_none_bites) {
  // ITEM 9's ANSWER, ASSERTED. The key must never be consumed by a creature
  // whose rule is not ITS lock - a key that vanished into a lock that does not
  // exist is the "reachable item that does nothing" the plan forbids, one step
  // worse.
  //
  // THE ROSTER FACT THIS CASE RESTS ON CHANGED AT P9-C3. It used to be "no
  // shipped rule has cond EVOC_ITEM", because the only such rule (species
  // 53 -> 54, Cifrax -> Ransora) was outside the 36-species prefix. Raising
  // ROSTER_FAMILIES to 20 landed it, so the emptiness is gone and the case is
  // rewritten around the creature it was always really about: a species-1
  // Paketo, whose own rule is cond EVOC_NONE, and for whom the key is not a
  // key. The full path - key used, creature evolves, item consumed - is now
  // reachable and is driven by the case below.
  fresh_bag();
  const uint8_t id = first_of((uint8_t)ITEM_KLASS_EVOLUTION);
  CHECK(id != 0);
  CHECK_EQ(inv_add(g_inv, id, 1), 1);

  int item_rules = 0;
  for (uint8_t i = 0; i < EVOLUTION_RULES_COUNT; ++i)
    if (EVOLUTION_RULES[i].cond == (uint8_t)EVOC_ITEM) item_rules++;
  CHECK_EQ(item_rules, 1);          // exactly one lock in the whole roster

  PebbleInstance p;
  mk_pebble(p, 1, (uint8_t)XP_LEVEL_MAX);      // well past every evolution level
  PebbleInstance before = p;
  ItemEffect eff;
  CHECK_EQ(inv_use(g_inv, id, &p, 1000u, (uint8_t)CAL_USER, eff), (uint8_t)IU_NO_EFFECT);
  CHECK_EQ(eff.klass, (uint8_t)ITEM_KLASS_EVOLUTION);
  CHECK_EQ(eff.evolved, 0);
  CHECK_EQ(memcmp(&before, &p, sizeof p), 0);
  CHECK_EQ(inv_count(g_inv, id), 1);           // STILL IN THE BAG

  // AND IT IS NOT A CARE ITEM. The old fold made it one; nothing may read it
  // that way now, whatever the creature's state.
  CHECK_EQ(item_get(id)->klass, (uint8_t)ITEM_KLASS_EVOLUTION);
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) p.care[i] = 0;
  CHECK_EQ(inv_use(g_inv, id, &p, 1000u, (uint8_t)CAL_USER, eff), (uint8_t)IU_NO_EFFECT);
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i) CHECK_EQ(p.care[i], 0);
}

TEST(the_key_is_cut_for_one_lock_and_the_condition_arm_agrees) {
  // THE OTHER DIRECTION, which the case above cannot reach and which a reader
  // would otherwise have to take on trust: the EVOC_ITEM machinery really does
  // answer yes for THIS key and no for anything else.
  //
  // MEASURED WHILE WRITING IT, and it changed the code: without the "the rule
  // must be EVOC_ITEM and name this item" check in inv_use(), a level-30
  // species-1 Paketo - whose own rule is level 8, cond EVOC_NONE - was
  // "ready", so the key evolved a creature it has nothing to do with and was
  // consumed doing it. evolution_ready() was right; the question it answers is
  // "may this creature evolve", not "is this item what does it".
  const uint8_t id = first_of((uint8_t)ITEM_KLASS_EVOLUTION);
  CHECK(id != 0);

  EvolutionRule r;
  memset(&r, 0, sizeof r);
  r.species = 1u; r.target = 2u; r.level = 1u;
  r.cond = (uint8_t)EVOC_ITEM; r.cond_value = (uint16_t)id;

  EvoContext ctx;
  evo_context_clear(ctx);
  ctx.have = (uint8_t)EVOCTX_ITEM;
  ctx.item_id = id;
  CHECK(evolution_cond_holds(r, ctx));

  // A DIFFERENT item does not open it.
  ctx.item_id = (uint8_t)(id == 1u ? 2u : 1u);
  CHECK(!evolution_cond_holds(r, ctx));

  // Nor does "some item, unspecified": a context that never supplied the field
  // is UNKNOWN and refuses (game/evolution.h's `have` rule).
  evo_context_clear(ctx);
  ctx.item_id = id;                          // set, but EVOCTX_ITEM not raised
  CHECK(!evolution_cond_holds(r, ctx));

  // THE FULL PATH, WHICH THIS CASE COULD NOT DRIVE BEFORE P9-C3. It said: "the
  // full path - key used, creature evolves, item consumed - CANNOT be driven at
  // this roster, because no shipped species points at an EVOC_ITEM rule. This
  // is the seam: land the species 53 -> 54 rule and inv_use() needs no change."
  // The rule landed with families 13..20 and inv_use() needed no change; here
  // is the path, driven end to end on the SHIPPED table rather than on the
  // hand-built rule above.
  const EvolutionRule* real = evolution_rule_for(53);   // Cifrax -> Ransora
  CHECK(real != nullptr);
  if (real) {
    CHECK_EQ(real->cond, (uint8_t)EVOC_ITEM);
    CHECK_EQ((int)real->cond_value, (int)id);
    fresh_bag();
    CHECK_EQ(inv_add(g_inv, id, 2), 2);
    PebbleInstance c;
    mk_pebble(c, 53, real->level);
    ItemEffect eff;
    CHECK_EQ(inv_use(g_inv, id, &c, 2000u, (uint8_t)CAL_USER, eff), (uint8_t)IU_OK);
    CHECK_EQ(eff.klass, (uint8_t)ITEM_KLASS_EVOLUTION);
    CHECK_EQ(eff.evolved, 1);
    CHECK_EQ(c.species_id, real->target);        // it really is a Ransora now
    CHECK_EQ(inv_count(g_inv, id), 1);           // and the key was SPENT

    // One below the rule's level and the same key does nothing and stays.
    fresh_bag();
    CHECK_EQ(inv_add(g_inv, id, 1), 1);
    PebbleInstance young;
    mk_pebble(young, 53, (uint8_t)(real->level - 1u));
    CHECK_EQ(inv_use(g_inv, id, &young, 2000u, (uint8_t)CAL_USER, eff),
             (uint8_t)IU_NO_EFFECT);
    CHECK_EQ(young.species_id, 53);
    CHECK_EQ(inv_count(g_inv, id), 1);
  }
}

TEST(a_capture_item_is_never_spent_from_a_menu) {
  fresh_bag();
  const uint8_t id = first_of((uint8_t)ITEM_KLASS_CAPTURE);
  CHECK(id != 0);
  CHECK_EQ(inv_add(g_inv, id, 2), 2);
  PebbleInstance p;
  mk_pebble(p, 1, 5);
  ItemEffect eff;
  CHECK_EQ(inv_use(g_inv, id, &p, 1000u, (uint8_t)CAL_USER, eff), (uint8_t)IU_NOT_HERE);
  // The one item a player could otherwise destroy by accident stays in the bag.
  CHECK_EQ(inv_count(g_inv, id), 2);
}

// =============================================================================
//  3. THE REFUSALS, EACH BY NAME AND EACH WITH A CONTROL
// =============================================================================
TEST(every_refusal_is_named_and_leaves_the_bag_alone) {
  fresh_bag();
  PebbleInstance p;
  mk_pebble(p, 1, 5);
  ItemEffect eff;

  // UNKNOWN ITEM
  CHECK_EQ(inv_use(g_inv, 0, &p, 0u, (uint8_t)CAL_USER, eff), (uint8_t)IU_UNKNOWN_ITEM);
  CHECK_EQ(inv_use(g_inv, (uint8_t)(ITEM_COUNT + 1u), &p, 0u, (uint8_t)CAL_USER, eff),
           (uint8_t)IU_UNKNOWN_ITEM);

  // NONE HELD - a real item the bag does not have.
  const uint8_t candy = first_of((uint8_t)ITEM_KLASS_XP_CANDY);
  CHECK_EQ(inv_use(g_inv, candy, &p, 0u, (uint8_t)CAL_USER, eff), (uint8_t)IU_NONE_HELD);

  // NO TARGET - held, but nothing to use it on.
  CHECK_EQ(inv_add(g_inv, candy, 1), 1);
  CHECK_EQ(inv_use(g_inv, candy, nullptr, 0u, (uint8_t)CAL_USER, eff), (uint8_t)IU_NO_TARGET);
  CHECK_EQ(inv_count(g_inv, candy), 1);
  PebbleInstance empty;
  memset(&empty, 0, sizeof empty);
  CHECK_EQ(inv_use(g_inv, candy, &empty, 0u, (uint8_t)CAL_USER, eff), (uint8_t)IU_NO_TARGET);
  CHECK_EQ(inv_count(g_inv, candy), 1);

  // POSITIVE CONTROL: the same item, the same bag, a real Pebble.
  mk_pebble(p, 1, 3);
  CHECK_EQ(inv_use(g_inv, candy, &p, 0u, (uint8_t)CAL_USER, eff), (uint8_t)IU_OK);
  CHECK_EQ(inv_count(g_inv, candy), 0);
}

TEST(every_shipped_item_has_an_arm_that_answers) {
  // COMPLETENESS: no item in the table falls through to a default that does
  // nothing, and no klass answers IU_UNKNOWN_ITEM for a real row.
  for (uint8_t i = 0; i < ITEM_COUNT; ++i) {
    const ItemDef& it = ITEMS_TABLE[i];
    fresh_bag();
    CHECK_EQ(inv_add(g_inv, it.id, 1), 1);
    PebbleInstance p;
    mk_pebble(p, 1, 4);
    for (uint8_t c = 0; c < (uint8_t)PB_CARE_COUNT; ++c) p.care[c] = 0;
    ItemEffect eff;
    const uint8_t r = inv_use(g_inv, it.id, &p, 1700000000u, (uint8_t)CAL_USER, eff);
    CHECK(r < (uint8_t)IU_USE_COUNT);
    CHECK(r != (uint8_t)IU_UNKNOWN_ITEM);
    CHECK(r != (uint8_t)IU_NONE_HELD);
    CHECK(r != (uint8_t)IU_NO_TARGET);
    CHECK_EQ(eff.klass, it.klass);
    // Consumed exactly when it worked.
    CHECK_EQ(inv_count(g_inv, it.id), (r == (uint8_t)IU_OK) ? 0 : 1);
  }
}
