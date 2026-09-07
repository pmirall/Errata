// =============================================================================
//  PEBBLEBOL - game/corruption.cpp
//  See corruption.h. PURE translation unit.
// =============================================================================
#include "corruption.h"

// A slot with no creature in it cannot be ill. pebble_is_empty() is the Box's
// own emptiness test (persistence/save_schema.h), used here rather than a
// second one: two definitions of "empty" is the disagreement this project keeps
// finding.
static inline bool usable(const PebbleInstance& p)
{
  return !pebble_is_empty(p);
}

// The one place the calibration rule is written. CAL_UNSET means gt_now() is
// "some epoch plus uptime" on a unit that has never been told the date; every
// other TimeCal means a wall clock somebody or something asserted.
static inline bool clock_ok(uint8_t cal)
{
  return cal != (uint8_t)CAL_UNSET;
}

bool cor_apply(PebbleInstance& p, uint32_t now_epoch, uint8_t cal)
{
  if (!usable(p) || !clock_ok(cal)) return false;
  // Saturating rather than wrapping. now_epoch + 86400 cannot overflow a u32
  // from any real clock, but a god-mode jump to the top of the range would wrap
  // to a deadline in 1970 - i.e. a status that clears instantly - and "cannot
  // happen" is how that kind of bug ships.
  const uint32_t until = (now_epoch > 0xFFFFFFFFu - (uint32_t)CORRUPT_DURATION_S)
                             ? 0xFFFFFFFFu
                             : (uint32_t)(now_epoch + (uint32_t)CORRUPT_DURATION_S);
  // REFRESH, NEVER STACK: a second corruption on an already corrupted Pebble
  // moves the deadline out and does not shorten it. Taking the max is what
  // makes that true even if a caller hands back an older clock.
  if ((p.status & (uint8_t)PBS_CORRUPTED) != 0u && p.corrupt_until_epoch > until)
    return true;
  p.status |= (uint8_t)PBS_CORRUPTED;
  p.corrupt_until_epoch = until;
  return true;
}

bool cor_expire(PebbleInstance& p, uint32_t now_epoch, uint8_t cal)
{
  if (!usable(p) || !clock_ok(cal)) return false;
  if ((p.status & (uint8_t)PBS_CORRUPTED) == 0u) return false;
  // A corrupted Pebble with NO deadline is what a save written by a firmware
  // that could set the bit and not time it would look like. Nothing in this
  // tree can produce one - P5-C3 is the first writer and it always writes both
  // - but if one ever arrives it is cleared here rather than left permanently
  // ill, which is spec section 47's rule about never getting stuck.
  if (p.corrupt_until_epoch == 0u || now_epoch >= p.corrupt_until_epoch) {
    p.status &= (uint8_t)~(uint8_t)PBS_CORRUPTED;
    p.corrupt_until_epoch = 0u;
    return true;
  }
  return false;
}

bool cor_clear(PebbleInstance& p)
{
  if (!usable(p)) return false;
  const bool was = (p.status & (uint8_t)PBS_CORRUPTED) != 0u;
  p.status &= (uint8_t)~(uint8_t)PBS_CORRUPTED;
  p.corrupt_until_epoch = 0u;
  return was;
}

uint8_t cor_service(PebbleInstance* slots, uint8_t n, uint32_t now_epoch, uint8_t cal)
{
  if (slots == nullptr || !clock_ok(cal)) return 0u;
  uint8_t ended = 0;
  for (uint8_t i = 0; i < n; ++i) {
    // cor_expire() is the ONE rule, called once per slot. Re-deciding "is it
    // due" here would be a second copy of the boundary condition, and the
    // boundary is exactly what tests/test_encounters.cpp pins on both sides.
    if (cor_expire(slots[i], now_epoch, cal) && ended < 255u) ++ended;
  }
  return ended;
}

bool cor_is_corrupted(const PebbleInstance& p)
{
  return usable(p) && (p.status & (uint8_t)PBS_CORRUPTED) != 0u;
}

uint32_t cor_left_s(const PebbleInstance& p, uint32_t now_epoch, uint8_t cal)
{
  if (!cor_is_corrupted(p) || !clock_ok(cal)) return 0u;
  if (p.corrupt_until_epoch <= now_epoch) return 0u;
  return (uint32_t)(p.corrupt_until_epoch - now_epoch);
}
