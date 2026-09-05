// =============================================================================
//  PEBBLEBOL - game/inventory.h
//  THE INVENTORY AND WHAT AN ITEM DOES (spec section 24, plan P5-C4).
//
//  Seven kinds of item over the persisted `inv` pair, and one switch with an
//  arm for every ItemKlass - including the two the pack could not describe
//  until this chunk and the fifth class it did not have.
//
// -----------------------------------------------------------------------------
//  THE TABLE OWNED IT, NOT THIS FILE, AND THAT IS THE POINT
// -----------------------------------------------------------------------------
//  data/items_table.h now carries `target` and `param` beside `value`, so a
//  CARE item says WHICH of the five care stats it restores (or ALL) and which
//  status bits it also clears, and a BATTLE_MOD says which stat it buffs and
//  for how many rounds. Before P5-C4 neither was expressible and this consumer
//  could not have been written without guessing - which is why the debt was a
//  PACK change (tools/content/items.json + gen_content.py + a guard) and not a
//  switch statement here.
//
//  THIS HEADER IS THE ONE PLACE THAT SEES BOTH SIDES of the three vocabularies
//  the generated table had to reproduce as bare ordinals, and it asserts them
//  rather than trusting them: CareTarget against persistence/save_schema.h's
//  CareId, ITEM_BSTAT_* against game/battle.h's BattleStat, ITEM_PBS_* against
//  the PBS_* bits. A data header may not include a game or a persistence one,
//  so without these asserts the pack could renumber a care stat and the item
//  that heals hunger would start healing energy with nothing to say so.
//
// -----------------------------------------------------------------------------
//  ITEM 9 (Llave Raiz): WHAT IT DOES, AND WHY THIS ANSWER
// -----------------------------------------------------------------------------
//  It was ITEM_KLASS_CARE with value 0 - "restores nothing" by the table's own
//  unit contract - reachable at about 2.4 % of every HIDDEN scan, and the
//  banner told P5-C4 only what NOT to do with it. Of the plan's three options
//  (give it a use / drop its drop weight to 0 / add the evolution-item class
//  section 24 lacks) the third is taken, because:
//
//    * it is what the item IS. evolution.json makes it the EVOC_ITEM key for
//      species 53 -> 54, and the whole EVOC_ITEM machinery already exists and
//      is already driven by tests (EvoContext.item_id, EVOCTX_ITEM,
//      game/evolution.cpp's arm). Nothing was missing but a shipped rule.
//    * spec section 24 says "INITIAL item classes", so a fifth is an extension
//      rather than a violation, and item_klasses_are_all_populated() - which
//      requires every class to have a row - is satisfied by item 9 itself.
//    * dropping its weight to 0 would need P9 to remember to restore it and
//      nothing would remind them; giving it a care value would make one item
//      mean two things and would delete the information.
//
//  WHAT IT DOES TODAY, SAID PLAINLY: inv_use() routes it to EvoContext.item_id
//  and asks game/evolution.h. At this 36-species roster no shipped rule has
//  cond EVOC_ITEM, so the answer is always "no" and the key is NOT CONSUMED -
//  IU_NO_EFFECT, item still in the bag. That is the honest behaviour for a key
//  with no lock yet, and it is asserted by a named case rather than described
//  here. P9 lands the rule and this code needs no change.
//
// -----------------------------------------------------------------------------
//  BATTLE_MOD IS LOCAL-ONLY, AND THAT IS A DECISION
// -----------------------------------------------------------------------------
//  A BATTLE_MOD is used from a menu BEFORE a fight: it is consumed and ARMED,
//  and ui/screen_battle.cpp applies it to the player's lead combatant after
//  battle_init(). It deliberately does NOT travel:
//    * BattleSetup has no field for it and its `reserved[2]` is asserted zero;
//      writing there would change the REPLAY INPUT and the version-checked
//      setup that two peers agree on.
//    * PebbleInstance has no held-item field either, and putting one there
//      would make the buff persisted AND wire-visible, so validate_pebble()
//      would need a rule and pbw_decode() a VR_WIRE_* decision - an unguarded
//      byte there is a peer handing itself +2 stages.
//  So the buff exists in a LOCAL battle and not in a linked one. Stated here
//  rather than discovered later.
//
// -----------------------------------------------------------------------------
//  PURE MODULE. stdint, the save schema, the content tables, game/xp.h,
//  game/evolution.h, game/corruption.h and game/battle.h (for the one
//  static_assert). No Arduino, no clock of its own, no RNG, no heap, no float,
//  no I/O. tests/test_inventory.cpp compiles it directly.
// =============================================================================
#ifndef PB_GAME_INVENTORY_H
#define PB_GAME_INVENTORY_H

#include <stdint.h>

#include "../data/items_table.h"
#include "../persistence/save_schema.h"
#include "battle.h"                        // BattleStat, for the assert only

// -----------------------------------------------------------------------------
//  THE THREE VOCABULARIES, CHECKED AGAINST THEIR OWNERS
// -----------------------------------------------------------------------------
// CareTarget 1..5 are CareId 0..4. Written out one by one rather than as a
// count, because a reordering keeps the count and moves the meaning.
static_assert((int)CARE_TGT_HUNGER      - 1 == (int)CARE_HUNGER,      "CareTarget/CareId drift");
static_assert((int)CARE_TGT_HAPPINESS   - 1 == (int)CARE_HAPPINESS,   "CareTarget/CareId drift");
static_assert((int)CARE_TGT_HEALTH      - 1 == (int)CARE_HEALTH,      "CareTarget/CareId drift");
static_assert((int)CARE_TGT_CLEANLINESS - 1 == (int)CARE_CLEANLINESS, "CareTarget/CareId drift");
static_assert((int)CARE_TGT_ENERGY      - 1 == (int)CARE_ENERGY,      "CareTarget/CareId drift");
static_assert((int)CARE_TGT_COUNT - 1 == (int)CARE_COUNT,
              "CareTarget has gained or lost a stat against CareId");
static_assert((int)ITEM_BSTAT_ATK == (int)BSTAT_ATK, "ItemDef.target/BattleStat drift");
static_assert((int)ITEM_BSTAT_DEF == (int)BSTAT_DEF, "ItemDef.target/BattleStat drift");
static_assert((int)ITEM_BSTAT_SPD == (int)BSTAT_SPD, "ItemDef.target/BattleStat drift");
static_assert((int)ITEM_PBS_SICK      == (int)PBS_SICK,      "ItemDef.param/PBS_* drift");
static_assert((int)ITEM_PBS_ASLEEP    == (int)PBS_ASLEEP,    "ItemDef.param/PBS_* drift");
static_assert((int)ITEM_PBS_CORRUPTED == (int)PBS_CORRUPTED, "ItemDef.param/PBS_* drift");
static_assert((int)ITEM_PBS_FAINTED   == (int)PBS_FAINTED,   "ItemDef.param/PBS_* drift");

// A CARE item's value is PERCENT OF FULL. care[] is milli-points 0..100000, so
// one percent is this many of them, and the division is exact by construction.
#define CARE_MILLI_PER_PCT  ((int32_t)(PB_CARE_MILLI_MAX / 100L))
static_assert(CARE_MILLI_PER_PCT * 100L == (int32_t)PB_CARE_MILLI_MAX,
              "the care scale is not a whole number of percent, so an item's "
              "value cannot be one");

// -----------------------------------------------------------------------------
//  WHY A USE DID OR DID NOT HAPPEN. Named, for game/validate.h's reason.
// -----------------------------------------------------------------------------
enum ItemUse : uint8_t {
  IU_OK = 0,          // consumed, and `eff` says what it did
  IU_UNKNOWN_ITEM,    // no such row
  IU_NONE_HELD,       // the bag has none
  IU_NO_TARGET,       // the klass needs a Pebble and none was given
  IU_NOT_HERE,        // a CAPTURE item: it is spent by an encounter, not a menu
  IU_NO_EFFECT,       // nothing would change - NOT consumed (see item 9)
  IU_USE_COUNT
};

// What a use did. Every field is 0 unless the klass names it.
struct ItemEffect {
  uint8_t  klass;         // the ItemKlass that ran
  uint16_t xp;            // XP_CANDY: XP awarded through XP_SRC_ITEM
  uint8_t  levels;        // XP_CANDY: levels gained by that award
  uint8_t  care_target;   // CARE: the CareTarget it acted on
  uint8_t  care_stats;    // CARE: how many stats actually moved (ALL -> 5)
  uint8_t  status_cleared;// CARE: the PBS_* bits it turned off
  uint8_t  mod_stat;      // BATTLE_MOD: BattleStat
  uint8_t  mod_stages;    // BATTLE_MOD: stages
  uint8_t  mod_rounds;    // BATTLE_MOD: rounds
  uint8_t  evolved;       // EVOLUTION: 1 when a rule consumed the key
};

// -----------------------------------------------------------------------------
//  THE BAG. `inv` is the caller's, exactly as game/cooldowns.h takes its table:
//  this module holds no inventory of its own, so two host cases cannot leak
//  into each other.
// -----------------------------------------------------------------------------
void    inv_begin(Inventory& inv);                 // magic/version/slots, empty
uint8_t inv_count(const Inventory& inv, uint8_t item_id);
uint8_t inv_slots_used(const Inventory& inv);
uint8_t inv_distinct(const Inventory& inv);        // == inv_slots_used

// Adds `n`. Returns how many were actually added: 0 when the id is unknown,
// when every slot is taken by another kind, or when n is 0. A slot SATURATES at
// 255 rather than wrapping - a bag that silently empties on the 256th pickup is
// the classic version of this bug.
uint8_t inv_add(Inventory& inv, uint8_t item_id, uint8_t n);

// Removes `n`. ALL OR NOTHING: false, and nothing changed, when the bag holds
// fewer than n. An emptied slot is released.
bool    inv_remove(Inventory& inv, uint8_t item_id, uint8_t n);

// -----------------------------------------------------------------------------
// inv_use(inv, item_id, target, now_epoch, cal, eff) -> ItemUse
//
//   Runs the item's klass arm against `target` (which may be nullptr for the
//   klasses that need no Pebble) and consumes ONE on success. Nothing here
//   writes flash; the caller commits.
//
//   `cal` is the TimeCal the corruption deadline needs (game/corruption.h): an
//   item that clears a status works on an uncalibrated device, because the cure
//   must never be the thing that gets stuck.
// -----------------------------------------------------------------------------
uint8_t inv_use(Inventory& inv, uint8_t item_id, PebbleInstance* target,
                uint32_t now_epoch, uint8_t cal, ItemEffect& eff);

// -----------------------------------------------------------------------------
//  THE ARMED BATTLE MODIFIER. Four bytes of globals, and the only state this
//  module owns. It is NOT persisted: an armed buff that survived a power cut
//  would be a stat bonus with no story, and the item is already spent.
// -----------------------------------------------------------------------------
struct InvBattleMod {
  uint8_t item_id;    // 0 = nothing armed
  uint8_t stat;       // BattleStat
  uint8_t stages;     // 1..BUFF_STAGE_MAX
  uint8_t rounds;     // 1..255
};

void inv_mod_clear(void);
bool inv_mod_armed(void);
// Hands out the armed modifier ONCE and disarms it: a buff bought for one fight
// may not be spent on two. False when nothing is armed.
bool inv_mod_take(InvBattleMod& out);

#endif  // PB_GAME_INVENTORY_H
