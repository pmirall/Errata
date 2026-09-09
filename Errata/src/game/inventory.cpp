// =============================================================================
//  ERRATA - game/inventory.cpp
//  See inventory.h. PURE translation unit.
// =============================================================================
#include "inventory.h"

#include <string.h>

#include "corruption.h"
#include "evolution.h"
#include "species.h"
#include "xp.h"

// THE ONE PIECE OF STATE IN THIS MODULE, and it is four bytes. inventory.h says
// why it is not persisted and why the buff never reaches the wire.
static InvBattleMod s_mod = { 0, 0, 0, 0 };

// -----------------------------------------------------------------------------
//  THE BAG
// -----------------------------------------------------------------------------
void inv_begin(Inventory& inv)
{
  memset(&inv, 0, sizeof inv);
  inv.magic   = (uint16_t)INV_MAGIC;
  inv.version = (uint8_t)SAVE_SCHEMA_VERSION;
  inv.slots   = (uint8_t)INVENTORY_SLOTS;
  // NOT sealed: the CRC belongs to persistence/save_manager.cpp, which is the
  // one place that knows what a blob's checksum spans. A caller that means to
  // write this to flash goes through save_inventory().
}

static InvSlot* find_slot(Inventory& inv, uint8_t item_id)
{
  if (item_id == 0u) return nullptr;
  for (uint8_t i = 0; i < (uint8_t)INVENTORY_SLOTS; ++i)
    if (inv.items[i].item_id == item_id) return &inv.items[i];
  return nullptr;
}

uint8_t inv_count(const Inventory& inv, uint8_t item_id)
{
  if (item_id == 0u) return 0u;
  for (uint8_t i = 0; i < (uint8_t)INVENTORY_SLOTS; ++i)
    if (inv.items[i].item_id == item_id) return inv.items[i].count;
  return 0u;
}

uint8_t inv_slots_used(const Inventory& inv)
{
  uint8_t n = 0;
  for (uint8_t i = 0; i < (uint8_t)INVENTORY_SLOTS; ++i)
    if (inv.items[i].item_id != 0u && inv.items[i].count != 0u) ++n;
  return n;
}

uint8_t inv_distinct(const Inventory& inv) { return inv_slots_used(inv); }

uint8_t inv_add(Inventory& inv, uint8_t item_id, uint8_t n)
{
  if (n == 0u) return 0u;
  if (item_get(item_id) == nullptr) return 0u;    // a bag never holds a non-item

  InvSlot* s = find_slot(inv, item_id);
  if (s == nullptr) {
    for (uint8_t i = 0; i < (uint8_t)INVENTORY_SLOTS; ++i) {
      if (inv.items[i].item_id == 0u || inv.items[i].count == 0u) {
        s = &inv.items[i];
        s->item_id = item_id;
        s->count   = 0u;
        break;
      }
    }
  }
  if (s == nullptr) return 0u;                    // seven kinds is the cap

  // SATURATE, NEVER WRAP. A bag that empties on the 256th pickup is the classic
  // shape of this bug, and it is silent.
  const uint16_t room = (uint16_t)(255u - (uint16_t)s->count);
  const uint8_t  add  = (n <= room) ? n : (uint8_t)room;
  s->count = (uint8_t)(s->count + add);
  return add;
}

bool inv_remove(Inventory& inv, uint8_t item_id, uint8_t n)
{
  if (n == 0u) return false;
  InvSlot* s = find_slot(inv, item_id);
  // ALL OR NOTHING: an underflow changes nothing at all, so a caller that
  // ignored the return value cannot have half-spent something.
  if (s == nullptr || s->count < n) return false;
  s->count = (uint8_t)(s->count - n);
  if (s->count == 0u) s->item_id = 0u;            // release the slot
  return true;
}

// -----------------------------------------------------------------------------
//  THE KLASS ARMS
// -----------------------------------------------------------------------------
static uint8_t use_xp_candy(uint8_t item_id, BugInstance& p, ItemEffect& eff)
{
  const uint16_t amount = item_xp_value(item_id);
  if (amount == 0u) return (uint8_t)IU_NO_EFFECT;
  // "Did anything happen" is asked of the BUG and not of xp_add()'s return
  // value, which reports a LEVEL and not an award: a candy that adds XP without
  // levelling is a use, and one spent at the top of the curve is not.
  const uint8_t  lv0 = p.level;
  const uint16_t xp0 = p.xp;
  uint8_t levels = 0;
  (void)xp_add(p, amount, XP_SRC_ITEM, &levels);
  if (p.level == lv0 && p.xp == xp0) return (uint8_t)IU_NO_EFFECT;
  eff.xp     = amount;
  eff.levels = levels;
  return (uint8_t)IU_OK;
}

static uint8_t use_care(const ItemDef& it, BugInstance& p, uint32_t now_epoch,
                        uint8_t cal, ItemEffect& eff)
{
  eff.care_target = it.target;
  const int32_t add = (int32_t)it.value * CARE_MILLI_PER_PCT;

  uint8_t moved = 0;
  for (uint8_t i = 0; i < (uint8_t)ER_CARE_COUNT; ++i) {
    if (it.target != (uint8_t)CARE_TGT_ALL && (uint8_t)(it.target - 1u) != i) continue;
    if (p.care[i] >= (int32_t)ER_CARE_MILLI_MAX) continue;   // already full
    int32_t v = p.care[i] + add;
    if (v > (int32_t)ER_CARE_MILLI_MAX) v = (int32_t)ER_CARE_MILLI_MAX;
    p.care[i] = v;
    ++moved;
  }
  eff.care_stats = moved;

  // The status half. PBS_CORRUPTED goes through game/corruption.h so the
  // DEADLINE goes with the bit - clearing the bit here and leaving
  // corrupt_until_epoch set would leave a timer with nothing to expire.
  uint8_t cleared = 0;
  if (it.param != 0u) {
    if ((it.param & (uint8_t)PBS_CORRUPTED) != 0u && cor_clear(p))
      cleared |= (uint8_t)PBS_CORRUPTED;
    const uint8_t rest = (uint8_t)(it.param & (uint8_t)~(uint8_t)PBS_CORRUPTED);
    const uint8_t hit  = (uint8_t)(p.status & rest);
    p.status = (uint8_t)(p.status & (uint8_t)~rest);
    cleared |= hit;
  }
  eff.status_cleared = cleared;
  (void)now_epoch;
  (void)cal;

  // A well Bug at full health does not waste the item.
  return (moved != 0u || cleared != 0u) ? (uint8_t)IU_OK : (uint8_t)IU_NO_EFFECT;
}

static uint8_t use_battle_mod(const ItemDef& it, ItemEffect& eff)
{
  s_mod.item_id = it.id;
  s_mod.stat    = it.target;
  s_mod.stages  = it.value;
  s_mod.rounds  = it.param;
  eff.mod_stat   = it.target;
  eff.mod_stages = it.value;
  eff.mod_rounds = it.param;
  // Arming over an already-armed modifier REPLACES it: two buffs is a stacking
  // rule nobody stated, and BattleCombatant.stage clamps to BUFF_STAGE_MAX
  // anyway, so the second item would silently buy nothing.
  return (uint8_t)IU_OK;
}

static uint8_t use_evolution(const ItemDef& it, BugInstance& p, ItemEffect& eff)
{
  // THE KEY. Everything this needs already existed (EvoContext.item_id,
  // EVOCTX_ITEM, game/evolution.cpp's EVOC_ITEM arm); what was missing was a
  // class for the item and a consumer to ask the question.
  //
  // A KEY OPENS THE LOCK IT IS CUT FOR AND NOT EVERY DOOR, and that check has
  // to be HERE rather than left to evolution_ready(). MEASURED: without it, a
  // level-30 species-1 Paketo - whose rule is level 8, cond EVOC_NONE - answers
  // "ready" for any context at all, so the key evolved a creature it has
  // nothing to do with and was consumed doing it. evolution_ready() is right to
  // say yes; its question is "may this creature evolve", not "is this item what
  // does it".
  const EvolutionRule* rule = evolution_rule_for(p.species_id);
  if (rule == nullptr) return (uint8_t)IU_NO_EFFECT;
  if (rule->cond != (uint8_t)EVOC_ITEM) return (uint8_t)IU_NO_EFFECT;
  if (rule->cond_value != (uint16_t)it.id) return (uint8_t)IU_NO_EFFECT;

  EvoContext ctx;
  evo_context_clear(ctx);
  ctx.have    = (uint8_t)(EVOCTX_ITEM | EVOCTX_CORRUPTED);
  ctx.item_id = it.id;
  ctx.corrupted = (uint8_t)((p.status & (uint8_t)PBS_CORRUPTED) != 0u);
  if (!evolution_ready(p, ctx)) return (uint8_t)IU_NO_EFFECT;   // key not spent
  if (!evolution_apply(p, ctx)) return (uint8_t)IU_NO_EFFECT;
  eff.evolved = 1u;
  return (uint8_t)IU_OK;
}

uint8_t inv_use(Inventory& inv, uint8_t item_id, BugInstance* target,
                uint32_t now_epoch, uint8_t cal, ItemEffect& eff)
{
  memset(&eff, 0, sizeof eff);

  const ItemDef* it = item_get(item_id);
  if (it == nullptr) return (uint8_t)IU_UNKNOWN_ITEM;
  eff.klass = it->klass;
  if (inv_count(inv, item_id) == 0u) return (uint8_t)IU_NONE_HELD;

  const bool needs_bug = (it->klass == (uint8_t)ITEM_KLASS_XP_CANDY ||
                             it->klass == (uint8_t)ITEM_KLASS_CARE ||
                             it->klass == (uint8_t)ITEM_KLASS_EVOLUTION);
  if (needs_bug && (target == nullptr || bug_is_empty(*target)))
    return (uint8_t)IU_NO_TARGET;

  uint8_t r;
  switch ((ItemKlass)it->klass) {
    case ITEM_KLASS_XP_CANDY:   r = use_xp_candy(item_id, *target, eff); break;
    case ITEM_KLASS_CARE:       r = use_care(*it, *target, now_epoch, cal, eff); break;
    case ITEM_KLASS_BATTLE_MOD: r = use_battle_mod(*it, eff); break;
    case ITEM_KLASS_EVOLUTION:  r = use_evolution(*it, *target, eff); break;
    case ITEM_KLASS_CAPTURE:
      // Spent by an encounter (game/capture.h), never from a menu - and NOT
      // consumed here, because a capture chip thrown away from the item list
      // would be the one item the player can destroy by accident.
      r = (uint8_t)IU_NOT_HERE;
      break;
    default:
      r = (uint8_t)IU_UNKNOWN_ITEM;
      break;
  }
  // CONSUMED ONLY ON A REAL EFFECT. IU_NO_EFFECT keeps the item: an evolution
  // key with no lock, a candy at level 30 and a bandage on a well creature all
  // stay in the bag.
  if (r == (uint8_t)IU_OK) (void)inv_remove(inv, item_id, 1u);
  return r;
}

// -----------------------------------------------------------------------------
//  THE ARMED MODIFIER
// -----------------------------------------------------------------------------
void inv_mod_clear(void) { s_mod.item_id = 0u; s_mod.stat = 0u; s_mod.stages = 0u; s_mod.rounds = 0u; }
bool inv_mod_armed(void) { return s_mod.item_id != 0u; }

bool inv_mod_take(InvBattleMod& out)
{
  if (s_mod.item_id == 0u) { memset(&out, 0, sizeof out); return false; }
  out = s_mod;
  inv_mod_clear();
  return true;
}
