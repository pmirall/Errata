// =============================================================================
//  ERRATA - game/cooldowns.cpp
//  See cooldowns.h for the two-table argument, the eviction rule and what the
//  uncalibrated path does and does not close.
// =============================================================================
#include "cooldowns.h"

// -----------------------------------------------------------------------------
// THE PER-BOOT RAM TABLE. COOLDOWN_SLOTS rows of {hash, monotonic deadline},
// the same width as the persisted one so the uncalibrated device is not
// quietly given a smaller or larger memory than the calibrated one.
// -----------------------------------------------------------------------------
struct CdRamRow {
  uint32_t net_hash;    // 0 = empty row, exactly as CooldownRow
  uint32_t until_ms;    // monotonic millisecond deadline
};

static CdRamRow s_ram[COOLDOWN_SLOTS];
static bool     s_dirty = false;

#define CD_PERIOD_MS ((uint32_t)ENCOUNTER_COOLDOWN_S * 1000UL)
static_assert(CD_PERIOD_MS < 0x80000000UL,
              "the RAM cooldown period must stay inside half the millis() wrap, "
              "or the signed comparison below cannot tell past from future");

void cd_begin(void) {
  for (uint8_t i = 0; i < (uint8_t)COOLDOWN_SLOTS; ++i) {
    s_ram[i].net_hash = 0;
    s_ram[i].until_ms = 0;
  }
  s_dirty = false;
}

bool cd_take_dirty(void) {
  const bool d = s_dirty;
  s_dirty = false;
  return d;
}

uint8_t cd_ram_used(void) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < (uint8_t)COOLDOWN_SLOTS; ++i) {
    if (s_ram[i].net_hash != 0) ++n;
  }
  return n;
}

// -----------------------------------------------------------------------------
// Small helpers over the persisted table
// -----------------------------------------------------------------------------
static void recount(CooldownTable& t) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < (uint8_t)COOLDOWN_SLOTS; ++i) {
    if (t.rows[i].net_hash != 0) ++n;
  }
  t.n = n;
}

// The slot a hash belongs in: its own row, else an empty one, else the one with
// the smallest deadline. See cooldowns.h for why that single rule is both the
// LRU and the expired-first policy.
static uint8_t slot_for(CooldownTable& t, uint32_t net_hash) {
  uint8_t empty = (uint8_t)COOLDOWN_SLOTS;
  uint8_t oldest = 0;
  for (uint8_t i = 0; i < (uint8_t)COOLDOWN_SLOTS; ++i) {
    if (t.rows[i].net_hash == net_hash) return i;
    if (t.rows[i].net_hash == 0 && empty == (uint8_t)COOLDOWN_SLOTS) empty = i;
    if (t.rows[i].until_epoch < t.rows[oldest].until_epoch) oldest = i;
  }
  return (empty < (uint8_t)COOLDOWN_SLOTS) ? empty : oldest;
}

static uint8_t ram_slot_for(uint32_t net_hash, uint32_t now_ms) {
  uint8_t empty = (uint8_t)COOLDOWN_SLOTS;
  uint8_t oldest = 0;
  int32_t oldest_left = 0;
  bool have_oldest = false;
  for (uint8_t i = 0; i < (uint8_t)COOLDOWN_SLOTS; ++i) {
    if (s_ram[i].net_hash == net_hash) return i;
    if (s_ram[i].net_hash == 0) {
      if (empty == (uint8_t)COOLDOWN_SLOTS) empty = i;
      continue;
    }
    // Remaining time, wrap-safe: a signed difference of two millisecond stamps
    // is correct across the 49.7-day wrap as long as the gap stays under half
    // the range, which the static_assert above pins.
    const int32_t left = (int32_t)(s_ram[i].until_ms - now_ms);
    if (!have_oldest || left < oldest_left) {
      oldest      = i;
      oldest_left = left;
      have_oldest = true;
    }
  }
  return (empty < (uint8_t)COOLDOWN_SLOTS) ? empty : oldest;
}

// -----------------------------------------------------------------------------
// THE PROMOTION. Called by both entry points, so a caller cannot reach the
// persisted table without it having happened.
// -----------------------------------------------------------------------------
static void promote(CooldownTable& t, const CdClock& c) {
  if (c.cal == (uint8_t)CAL_UNSET) {
    return;                         // still untrustworthy: nothing to promote
  }
  for (uint8_t i = 0; i < (uint8_t)COOLDOWN_SLOTS; ++i) {
    if (s_ram[i].net_hash == 0) continue;
    const int32_t left_ms = (int32_t)(s_ram[i].until_ms - c.now_ms);
    const uint32_t h = s_ram[i].net_hash;
    s_ram[i].net_hash = 0;          // consumed either way: expired rows just go
    s_ram[i].until_ms = 0;
    if (left_ms <= 0) continue;     // already expired - promoting it would be
                                    // arming a cooldown that had finished
    const uint8_t k = slot_for(t, h);
    t.rows[k].net_hash    = h;
    t.rows[k].until_epoch = c.now_epoch + (uint32_t)(left_ms / 1000);
    s_dirty = true;
  }
  recount(t);
}

// -----------------------------------------------------------------------------
// Public
// -----------------------------------------------------------------------------
bool cd_ready(CooldownTable& t, uint32_t net_hash, const CdClock& c) {
  if (net_hash == 0) {
    return false;                   // see cooldowns.h: 0 is the empty marker
  }
  promote(t, c);

  if (c.cal == (uint8_t)CAL_UNSET) {
    for (uint8_t i = 0; i < (uint8_t)COOLDOWN_SLOTS; ++i) {
      if (s_ram[i].net_hash != net_hash) continue;
      return (int32_t)(s_ram[i].until_ms - c.now_ms) <= 0;
    }
    return true;                    // never seen this boot
  }

  for (uint8_t i = 0; i < (uint8_t)COOLDOWN_SLOTS; ++i) {
    if (t.rows[i].net_hash != net_hash) continue;
    // Absolute deadlines only, so a clock that moved BACKWARDS makes this
    // comparison more false and the wait longer. Never shorter.
    return c.now_epoch >= t.rows[i].until_epoch;
  }
  return true;
}

bool cd_arm(CooldownTable& t, uint32_t net_hash, const CdClock& c) {
  if (net_hash == 0) {
    return false;
  }
  promote(t, c);

  if (c.cal == (uint8_t)CAL_UNSET) {
    // NOTHING IS WRITTEN TO FLASH HERE. An uptime-scale deadline in the
    // persisted table is the farm cooldowns.h describes.
    const uint8_t k = ram_slot_for(net_hash, c.now_ms);
    s_ram[k].net_hash = net_hash;
    s_ram[k].until_ms = c.now_ms + CD_PERIOD_MS;
    return true;
  }

  const uint8_t k = slot_for(t, net_hash);
  t.rows[k].net_hash    = net_hash;
  t.rows[k].until_epoch = c.now_epoch + (uint32_t)ENCOUNTER_COOLDOWN_S;
  recount(t);
  s_dirty = true;
  return true;
}
