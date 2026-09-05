// =============================================================================
//  PEBBLEBOL - game/xp.cpp
//  The XP curve, the level-up carry and the anti-farm ledger. See xp.h.
//
//  ZERO floating point and no 64-bit intermediates: the ledger counts WHOLE XP
//  with a seconds remainder, and every refill step divides its window exactly
//  (static_asserted in balance.h), so the arithmetic below never leaves u32.
// =============================================================================
#include "xp.h"

#include "../core/config.h"            // NT_EPOCH_SANE_MIN
#include "../data/species_table.h"     // species_get(): base_hp, for the hp rescale
#include "evolution.h"                 // the level gate behind EVO_STATE_PENDING

// -----------------------------------------------------------------------------
//  THE METER TABLE. One row per metered source, in XpSource order; a window of
//  0 marks a slot that is reserved but not yet metered. There is no such row
//  left: P4-C4 built the thing that spends XP_SRC_BATTLE and sized its bucket
//  with it (data/balance.h says what the number is and what it costs a save).
// -----------------------------------------------------------------------------
struct XpMeter {
  uint16_t cap;        // whole XP the bucket holds
  uint32_t window_s;   // seconds to refill it from empty; 0 = unmetered
};

static const XpMeter METER[XP_LEDGER_SLOTS] = {
  { (uint16_t)XP_CAP_CARE,     (uint32_t)XP_WIN_CARE_S     },   // XP_SRC_CARE
  { (uint16_t)XP_CAP_MINIGAME, (uint32_t)XP_WIN_MINIGAME_S },   // XP_SRC_MINIGAME
  { (uint16_t)XP_CAP_CARRY,    (uint32_t)XP_WIN_CARRY_S    },   // XP_SRC_CARRY
  { (uint16_t)XP_CAP_BATTLE,   (uint32_t)XP_WIN_BATTLE_S   }    // XP_SRC_BATTLE
};

static XpLedger g_led;

// Seconds of real time that return one point to bucket `i`. The exactness of
// this division is what balance.h static_asserts.
static uint32_t refill_step_s(uint8_t i)
{
  if (i >= (uint8_t)XP_LEDGER_SLOTS) return 0u;
  if (METER[i].window_s == 0u || METER[i].cap == 0u) return 0u;
  return METER[i].window_s / (uint32_t)METER[i].cap;
}

static uint8_t metered(XpSource src)
{
  return (src < (XpSource)XP_LEDGER_SLOTS && refill_step_s((uint8_t)src) != 0u) ? 1u : 0u;
}

// =============================================================================
//  THE CURVE
// =============================================================================
uint16_t xp_for_level(uint8_t level)
{
  if (level == 0u || level >= (uint8_t)XP_LEVEL_MAX) return 0u;
  return XP_TABLE[level];
}

uint16_t xp_care_action_amount(void) { return (uint16_t)XP_CARE_ACTION; }

void xp_hp_rescale(PebbleInstance& p, uint16_t hp_max_before, uint16_t hp_max_after)
{
  if (hp_max_before == 0u) return;
  uint32_t hp = ((uint32_t)p.hp_cur * (uint32_t)hp_max_after) / (uint32_t)hp_max_before;
  if (hp > (uint32_t)hp_max_after) hp = (uint32_t)hp_max_after;
  p.hp_cur = (uint16_t)hp;
}

uint16_t xp_minigame_amount(uint16_t win_permille)
{
  if (win_permille > 1000u) win_permille = 1000u;
  return (uint16_t)(((uint32_t)win_permille * (uint32_t)XP_MINIGAME_NUM)
                    / (uint32_t)XP_MINIGAME_DEN);
}

// Spends what the ledger will allow of `amount` and returns it. An unmetered
// source spends nothing and passes the award through untouched.
static uint16_t meter_take(XpSource src, uint16_t amount)
{
  if (!metered(src)) return amount;
  const uint8_t i = (uint8_t)src;
  if (amount > g_led.left[i]) amount = g_led.left[i];
  g_led.left[i] = (uint16_t)(g_led.left[i] - amount);
  return amount;
}

bool xp_add(PebbleInstance& p, uint16_t amount, XpSource src, uint8_t* levels_gained)
{
  if (levels_gained) *levels_gained = 0u;

  // An empty slot is not a creature: nothing to level, and nothing to meter
  // against either, so the budget is left alone for the Pebble that is real.
  if (p.species_id == 0u || p.id == 0u) return false;
  if (src >= XP_SRC_COUNT) return false;

  if (p.level == 0u) p.level = 1u;
  if (p.level >= (uint8_t)XP_LEVEL_MAX) {
    // The top of the curve. xp stops meaning anything there, so it is pinned at
    // 0 rather than left holding the leftovers of the last level-up.
    p.level = (uint8_t)XP_LEVEL_MAX;
    p.xp    = 0u;
    return false;
  }

  amount = meter_take(src, amount);
  if (amount == 0u) return false;

  const uint8_t level0 = p.level;
  uint32_t acc = (uint32_t)p.xp + (uint32_t)amount;
  uint8_t  lv  = p.level;
  uint8_t  ups = 0u;

  // Multi-level carry-over in ONE call: a big award (an XP candy, a battle, a
  // catch-up's worth of carried time) must not be truncated to a single level.
  while (lv < (uint8_t)XP_LEVEL_MAX) {
    const uint16_t need = XP_TABLE[lv];
    if (need == 0u || acc < (uint32_t)need) break;
    acc -= (uint32_t)need;
    lv++;
    ups++;
  }
  if (lv >= (uint8_t)XP_LEVEL_MAX) {
    lv  = (uint8_t)XP_LEVEL_MAX;      // saturate: no level 31, no wrapped xp
    acc = 0u;
  }
  // Below the cap the loop only stops on acc < XP_TABLE[lv], and every entry is
  // far inside u16, so the store below cannot truncate.
  p.level = lv;
  p.xp    = (uint16_t)acc;

  // EVOLVE_PENDING, on every award rather than only on a level-up: a Pebble
  // that reached the gate before this rule existed, or whose owner declined an
  // offer, must still be able to raise the bit. Only the LEVEL half of the rule
  // is visible from here (see xp.h), and the bit is never CLEARED here either -
  // clearing it belongs to evolution_apply().
  if (evolution_level_ready(p)) p.evo_state |= (uint8_t)EVO_STATE_PENDING;

  if (ups == 0u) return false;

  // A level-up widens the derived hp_max under a creature that is standing
  // still, so hp_cur is rescaled by the same ratio: a level-up is not a heal
  // (it would make levelling mid-battle a free potion) and it must not leave
  // hp_cur above the new maximum either. xp_hp_rescale() is THE rescale, shared
  // with game/evolution.cpp so the two paths can never drift apart.
  const SpeciesDef* sp = species_get(p.species_id);
  if (sp) {
    xp_hp_rescale(p, xp_hp_max(sp->base_hp, level0), xp_hp_max(sp->base_hp, lv));
  }

  if (levels_gained) *levels_gained = ups;
  return true;
}

// =============================================================================
//  THE LEDGER
// =============================================================================
const XpLedger& xp_ledger(void) { return g_led; }

void xp_ledger_reset(uint8_t full)
{
  for (uint8_t i = 0; i < (uint8_t)XP_LEDGER_SLOTS; ++i) {
    g_led.left[i]  = (full && refill_step_s(i) != 0u) ? METER[i].cap : 0u;
    g_led.rem_s[i] = 0u;
  }
  g_led.carry_s = 0u;
}

void xp_ledger_tick(uint32_t dt_s)
{
  if (dt_s == 0u) return;
  for (uint8_t i = 0; i < (uint8_t)XP_LEDGER_SLOTS; ++i) {
    const uint32_t step = refill_step_s(i);
    if (step == 0u) continue;
    if (g_led.left[i] >= METER[i].cap) { g_led.left[i] = METER[i].cap; g_led.rem_s[i] = 0u; continue; }

    // One window refills the whole bucket, so a longer dt is the same answer -
    // and the clamp is what keeps rem_s + dt inside u32 for a catch-up of years.
    uint32_t dt = (dt_s > METER[i].window_s) ? METER[i].window_s : dt_s;
    const uint32_t acc  = g_led.rem_s[i] + dt;
    const uint32_t back = acc / step;
    g_led.rem_s[i] = acc % step;

    uint32_t v = (uint32_t)g_led.left[i] + back;
    if (v >= (uint32_t)METER[i].cap) { v = METER[i].cap; g_led.rem_s[i] = 0u; }
    g_led.left[i] = (uint16_t)v;
  }
}

uint16_t xp_daily_left(const XpLedger& l, XpSource src)
{
  if (src >= (XpSource)XP_LEDGER_SLOTS) return 0xFFFFu;   // unmetered
  if (refill_step_s((uint8_t)src) == 0u) return 0xFFFFu;  // reserved, not yet metered
  return l.left[(uint8_t)src];
}

uint16_t xp_carry_due(uint32_t dt_s)
{
  if (dt_s == 0u) return 0u;
  // A catch-up cannot pay for time nobody carried the device, and the daily cap
  // would swallow it anyway; clamp so carry_s cannot run away inside u32.
  if (dt_s > (uint32_t)XP_WIN_CARRY_S) dt_s = (uint32_t)XP_WIN_CARRY_S;

  g_led.carry_s += dt_s;
  const uint32_t steps = g_led.carry_s / (uint32_t)XP_CARRY_STEP_S;
  g_led.carry_s -= steps * (uint32_t)XP_CARRY_STEP_S;

  uint32_t due = steps * (uint32_t)XP_CARRY_STEP_XP;
  if (due > 0xFFFFu) due = 0xFFFFu;
  return (uint16_t)due;
}

void xp_ledger_snapshot(uint8_t out[XP_LEDGER_SLOTS])
{
  if (!out) return;
  for (uint8_t i = 0; i < (uint8_t)XP_LEDGER_SLOTS; ++i) {
    if (refill_step_s(i) == 0u) { out[i] = 0u; continue; }   // nothing to keep
    // The seconds remainder is deliberately dropped: it is at most one refill
    // step and always in the player's disfavour, so a save/restore round trip
    // can never manufacture a point out of rounding.
    uint16_t v = g_led.left[i];
    if (v > METER[i].cap) v = METER[i].cap;
    out[i] = (uint8_t)((v > 255u) ? 255u : v);
  }
}

// -----------------------------------------------------------------------------
//  THE ELAPSED REFILL IS GONE, AND ITS ABSENCE IS THE ANTI-FARM RULE (P6-C4).
//
//  This used to be  left = min(cap, saved + elapsed / refill_step), with
//  elapsed = now_epoch - saved_epoch clamped to one window. Both epochs are
//  WALL CLOCK, and on this device the wall clock is something the player TYPES:
//  the time screen calls gt_set_epoch(..., CAL_USER) with any value it likes,
//  and the jumped epoch is persisted, so the next boot's "elapsed" is whatever
//  the player decided it was. One window of elapsed refills the whole bucket.
//
//  MEASURED, before the change: 100 rounds of (set the clock forward one day,
//  reboot, run ONE Wi-Fi scan of the SAME ten access points) spent 990 metered
//  XP in ZERO real seconds. An honest player at full tilt earns 37.8 XP per REAL
//  day. Neither half does it alone - 100 reboots with the clock left where it
//  was spend 0, and 100 clock jumps without a reboot spend 0 - so the exploit is
//  exactly this line meeting a reboot.
//
//  There is no way to tell a typed day from a day the device spent switched off,
//  because it was switched off and cannot have watched either. So the refill is
//  not repaired, it is REMOVED: a restore hands back the budget that was true at
//  the last save and not one point more, which is what game/xp.h and
//  app/app.cpp both already said this call did. xp_ledger_tick() then refills it
//  from seconds the device actually watched pass, and those are the only
//  seconds anyone can prove.
//
//  WHAT THE HONEST PLAYER LOSES, said plainly rather than buried: time the
//  device spends switched OFF no longer refills the budget. A player who drained
//  the day's carry cap, switched the device off overnight and switched it on
//  again used to get the whole cap back; now they get back what they had, and it
//  refills at cap/window while the device is on. The meter therefore means "XP
//  per day of device-ON time" rather than "per day of wall time". That is a
//  smaller budget, it is in the player's disfavour in every case, and it is the
//  only direction that cannot be typed.
// -----------------------------------------------------------------------------
uint8_t xp_ledger_restore(const uint8_t* pts, uint8_t n,
                          uint32_t saved_epoch, uint32_t now_epoch)
{
  // Both epochs still have to be real dates. They no longer buy anything, but
  // they are what says the blob was written by a device that knew what time it
  // was - and without that, the safe seed is ZERO rather than the snapshot.
  const uint8_t trust = (pts != 0 && n != 0 &&
                         saved_epoch >= (uint32_t)NT_EPOCH_SANE_MIN &&
                         now_epoch   >= (uint32_t)NT_EPOCH_SANE_MIN) ? 1u : 0u;

  for (uint8_t i = 0; i < (uint8_t)XP_LEDGER_SLOTS; ++i) {
    const uint32_t step = refill_step_s(i);
    g_led.rem_s[i] = 0u;
    if (step == 0u || !trust) { g_led.left[i] = 0u; continue; }

    uint32_t v = (i < n) ? (uint32_t)pts[i] : 0u;
    if (v > (uint32_t)METER[i].cap) v = METER[i].cap;   // an edited blob cannot exceed the cap
    g_led.left[i] = (uint16_t)v;
  }
  g_led.carry_s = 0u;
  return trust;
}
