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

// -----------------------------------------------------------------------------
//  THE METER TABLE. One row per metered source, in XpSource order; a window of
//  0 marks a slot that is reserved but not yet metered (XP_SRC_BATTLE, until
//  P4-C4 exists to spend it).
// -----------------------------------------------------------------------------
struct XpMeter {
  uint16_t cap;        // whole XP the bucket holds
  uint32_t window_s;   // seconds to refill it from empty; 0 = unmetered
};

static const XpMeter METER[XP_LEDGER_SLOTS] = {
  { (uint16_t)XP_CAP_CARE,     (uint32_t)XP_WIN_CARE_S     },   // XP_SRC_CARE
  { (uint16_t)XP_CAP_MINIGAME, (uint32_t)XP_WIN_MINIGAME_S },   // XP_SRC_MINIGAME
  { (uint16_t)XP_CAP_CARRY,    (uint32_t)XP_WIN_CARRY_S    },   // XP_SRC_CARRY
  { 0,                         0                           }    // XP_SRC_BATTLE
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

  if (ups == 0u) return false;

  // hp_max = 10 + 2*base_hp + level (plan 1.5.1), derived and never stored, so
  // a level-up widens the bar under a creature that is standing still. Rescale
  // hp_cur by the same ratio: a level-up is not a heal (it would make levelling
  // mid-battle a free potion) and it must not leave hp_cur above the new max
  // either. Integer, truncating: the fraction lost is under one HP and always
  // in the honest direction.
  const SpeciesDef* sp = species_get(p.species_id);
  if (sp) {
    const uint32_t max0 = 10u + 2u * (uint32_t)sp->base_hp + (uint32_t)level0;
    const uint32_t max1 = 10u + 2u * (uint32_t)sp->base_hp + (uint32_t)lv;
    if (max0 > 0u) {
      uint32_t hp = ((uint32_t)p.hp_cur * max1) / max0;
      if (hp > max1) hp = max1;
      p.hp_cur = (uint16_t)hp;
    }
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

uint8_t xp_ledger_restore(const uint8_t* pts, uint8_t n,
                          uint32_t saved_epoch, uint32_t now_epoch)
{
  const uint8_t trust = (pts != 0 && n != 0 &&
                         saved_epoch >= (uint32_t)NT_EPOCH_SANE_MIN &&
                         now_epoch   >= (uint32_t)NT_EPOCH_SANE_MIN) ? 1u : 0u;

  uint32_t elapsed = 0u;
  if (trust && now_epoch > saved_epoch) elapsed = now_epoch - saved_epoch;

  for (uint8_t i = 0; i < (uint8_t)XP_LEDGER_SLOTS; ++i) {
    const uint32_t step = refill_step_s(i);
    g_led.rem_s[i] = 0u;
    if (step == 0u || !trust) { g_led.left[i] = 0u; continue; }

    uint32_t el = (elapsed > METER[i].window_s) ? METER[i].window_s : elapsed;
    uint32_t v  = (i < n) ? (uint32_t)pts[i] : 0u;
    if (v > (uint32_t)METER[i].cap) v = METER[i].cap;   // an edited blob cannot exceed the cap
    v += el / step;                                     // truncating: under-reports, never over
    if (v > (uint32_t)METER[i].cap) v = METER[i].cap;
    g_led.left[i] = (uint16_t)v;
  }
  g_led.carry_s = 0u;
  return trust;
}
