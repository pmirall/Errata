// =============================================================================
//  PEBBLEBOL - game/activity.cpp
//  See activity.h. PURE translation unit.
// =============================================================================
#include "activity.h"

#include <string.h>

#include "../core/config.h"     // NT_EPOCH_SANE_MIN

// -----------------------------------------------------------------------------
//  THE PER-BOOT HALF. Everything below is .bss and everything below is cleared
//  by act_begin(). The PERSISTED half is two fields of the caller's
//  CooldownTable and is never touched here except through bank().
//
//  THE DISTINCT SETS ARE EXACT, AND THE CAP IS WHY THEY CAN BE. A set only ever
//  has to hold as many entries as its term's cap: past ACT_CAP_NETS no further
//  network can score, so an eleventh entry could never change an answer. Ten
//  u32s is 40 B, which is small enough to compare exactly - so this has NO
//  false positives and NO false negatives on the value it is given, and a
//  Bloom filter would be a solution to a problem the cap already removed.
//
//  Measured against the alternatives over 2,000 simulated days x 6 scans per
//  access point (120,000 answers at 10 distinct networks/day, 2.4 M at 200):
//
//    ExactU32<10>   41 B   0 errors            <- this
//    FoldU16<10>    22 B   3 under-counts
//    Bits2<128>     16 B   43 under-counts
//    Bits2<256>     32 B   16 under-counts
//
//  Every approximate form errs in ONE direction only - a new network reading as
//  already-seen, i.e. UNDER-counting - which is structural: a set that only
//  ever adds can say "seen" too often and never "new" too often. That is the
//  safe direction, and it is still an error. 25 B was not worth buying one.
//
//  THE ONE INEXACTNESS THAT REMAINS IS UPSTREAM AND IS NAMED HERE SO IT IS NOT
//  CLAIMED AWAY: net_hash is a 32-bit salted digest of a BSSID
//  (networking/net_classify.h), so two different access points can in principle
//  collide and be credited once between them. tests/test_exploration_hash.cpp
//  measures that hash's avalanche over 4,000 inputs; the failure it would cause
//  here is exactly one lost point, in the player's disfavour.
//
//  FULL MEANS "SEEN" FOR EVERYTHING, and that is not a degradation: when the
//  set is full the term is at its cap, so the honest answer to "is this new?"
//  is "it does not matter" and the honest answer to "does it score?" is no.
// -----------------------------------------------------------------------------
static uint32_t s_net[ACT_CAP_NETS];      // the day's credited net_hash values
static uint32_t s_peer[ACT_CAP_PEERS];    // the day's credited peer device_ids
static uint8_t  s_net_n;
static uint8_t  s_peer_n;

static uint8_t  s_carry_min;      // the four per-term counters, all capped
static uint8_t  s_interact;
static uint8_t  s_nets;
static uint8_t  s_peers;
static uint32_t s_carry_rem_s;    // awake seconds not yet paid out as a minute

static ActGain  s_gain;           // owed to the app, drained by act_take_gain()
static uint8_t  s_dirty;

// Every cap is a u8 counter, so a retune that put one over 255 would silently
// wrap and hand the player an unbounded term. The build stops instead.
static_assert(ACT_CAP_CARRY_MIN <= 255u && ACT_CAP_INTERACT <= 255u &&
              ACT_CAP_NETS <= 255u && ACT_CAP_PEERS <= 255u,
              "an activity term counter is one byte");
// act_day is a u16 and the day index is epoch / ACT_DAY_S; the widest u32 epoch
// gives 49,710, so the field is total over the whole range of the clock.
static_assert(0xFFFFFFFFul / ACT_DAY_S <= 0xFFFFu,
              "CooldownTable.act_day cannot hold every day a u32 epoch can name");
// The rewards are computed by counting thresholds crossed in a u16 score; both
// steps must divide something reachable or a full day pays a fraction nobody
// sees. Neither is load-bearing for correctness - both are integer divisions -
// but a step that does not divide the maximum is a tuning mistake, not a
// design, and it should be visible at build time.
static_assert(ACT_SCORE_MAX % ACT_XP_STEP_POINTS == 0u,
              "the XP step does not divide a full activity day");
static_assert(ACT_SCORE_MAX % ACT_HAPPY_STEP_POINTS == 0u,
              "the happiness step does not divide a full activity day");
// act_happy_for_granted_xp() leans on this: every multiple of the happiness step
// is a multiple of the XP step, so happiness can never be owed by a bank() the
// XP threshold count did not also see. Break it and a happiness payment could
// arrive with granted_xp == 0 behind it and be silently dropped.
static_assert(ACT_HAPPY_STEP_POINTS % ACT_XP_STEP_POINTS == 0u,
              "the happiness step is not a multiple of the XP step - the "
              "happiness would no longer be metered by the XP ledger");

void act_begin(void)
{
  memset(s_net, 0, sizeof s_net);
  memset(s_peer, 0, sizeof s_peer);
  s_net_n = s_peer_n = 0u;
  s_carry_min = s_interact = s_nets = s_peers = 0u;
  s_carry_rem_s = 0u;
  s_gain.points = s_gain.xp = s_gain.happy_milli = 0u;
  s_dirty = 0u;
}

uint16_t act_day_index(uint32_t now_epoch)
{
  return (uint16_t)(now_epoch / (uint32_t)ACT_DAY_S);
}

// -----------------------------------------------------------------------------
//  THE PERSISTED HALF, CHECKED ONCE AT BOOT (P7-C6).
//
//  open_day()'s only roll condition is `day > t.act_day`, and act_day is a u16
//  that can hold 65,535 while the widest day a u32 epoch can name is 49,710.
//  A blob carrying anything above that ceiling therefore names a day the clock
//  can NEVER reach, so the day never rolls again: the score sticks at whatever
//  it held and every later day scores one day's carry cap and nothing more.
//  MEASURED before this clamp: ten simulated years scored 876,000 points from
//  act_day 0 and 240 from act_day 65,535.
//
//  IT IS DENIAL AND NOT A FARM - the holder loses, nobody gains - and the pair
//  CRC covers random corruption, so it takes a hand-edited or foreign blob. One
//  line at load removes it, which is cheaper than reasoning about who could
//  write one.
//
//  NOT IN validate.cpp, DELIBERATELY: that validator's subject is a
//  PebbleInstance and it says in its own header that it takes no policy and no
//  second subject. Widening it to cover the cooldown table is exactly the scope
//  creep game/validate.h argues against.
bool act_adopt(CooldownTable& t)
{
  if (t.act_day <= ACT_DAY_MAX_INDEX) return false;
  t.act_day   = 0u;                // no day recorded: the next real day opens
  t.act_score = 0u;                // and it opens with nothing carried into it
  return true;
}

// -----------------------------------------------------------------------------
//  IS THIS CLOCK WORTH SCORING AGAINST? Layer 1, in one place so no entry point
//  can forget it, and BOTH halves of it: an untrustworthy calibration state and
//  an epoch that is not a date. The second is not redundant - a caller can hand
//  over CAL_USER with a bad epoch (a test, a corrupted config), and an epoch
//  below NT_EPOCH_SANE_MIN divided by a day is a day index near zero, which
//  would collide with "no day recorded".
// -----------------------------------------------------------------------------
static bool clock_ok(const ActClock& c)
{
  if (c.cal == (uint8_t)CAL_UNSET || c.cal >= (uint8_t)CAL_COUNT) return false;
  return c.now_epoch >= (uint32_t)NT_EPOCH_SANE_MIN;
}

// -----------------------------------------------------------------------------
//  THE DAY ROLL. Layer 2, and the whole of it is the comparison operator.
//
//  FORWARD ONLY. `day > t.act_day` opens a new day: the total resets, the
//  per-term counters and both sets are cleared. Anything else - the same day,
//  or a day EARLIER than the one stored - changes nothing at all, so a clock
//  moved backwards (the time screen accepts a CAL_USER rollback of any size,
//  gametime.h) finds the budget exactly where it left it. Returns false when
//  the clock is not worth scoring against, which is the caller's cue to bank
//  nothing.
// -----------------------------------------------------------------------------
static bool open_day(CooldownTable& t, const ActClock& c)
{
  if (!clock_ok(c)) return false;
  const uint16_t day = act_day_index(c.now_epoch);
  if (day > t.act_day) {
    t.act_day   = day;
    t.act_score = 0u;
    memset(s_net, 0, sizeof s_net);
    memset(s_peer, 0, sizeof s_peer);
    s_net_n = s_peer_n = 0u;
    s_carry_min = s_interact = s_nets = s_peers = 0u;
    s_carry_rem_s = 0u;
    s_dirty = 1u;
  }
  return true;
}

// -----------------------------------------------------------------------------
//  BANKING POINTS, AND THE ONLY PLACE A REWARD IS COMPUTED.
//
//  The XP and the happiness are counted as THRESHOLDS CROSSED between the score
//  before and the score after - not as a share of `pts` - and that is what makes
//  a power cycle worthless against them: the before/after pair is read from the
//  PERSISTED total, so score that was already banked can never pay again,
//  however many times the per-term counters are cleared. It is also what makes
//  the reward monotone: `after` only ever rises within a day, so the count of
//  thresholds below it only ever rises.
// -----------------------------------------------------------------------------
static uint16_t bank(CooldownTable& t, uint16_t pts)
{
  if (pts == 0u) return 0u;

  const uint16_t before = t.act_score;
  uint32_t after = (uint32_t)before + (uint32_t)pts;
  if (after > (uint32_t)ACT_SCORE_MAX) after = (uint32_t)ACT_SCORE_MAX;
  if ((uint16_t)after == before) return 0u;        // the day's total is full

  t.act_score = (uint16_t)after;
  s_dirty = 1u;

  const uint16_t added = (uint16_t)(t.act_score - before);
  const uint16_t xp    = (uint16_t)(t.act_score / ACT_XP_STEP_POINTS -
                                    before      / ACT_XP_STEP_POINTS);
  const uint16_t hsteps = (uint16_t)(t.act_score / ACT_HAPPY_STEP_POINTS -
                                     before      / ACT_HAPPY_STEP_POINTS);

  s_gain.points      = (uint16_t)(s_gain.points + added);
  s_gain.xp          = (uint16_t)(s_gain.xp + xp);
  s_gain.happy_milli = (uint16_t)(s_gain.happy_milli +
                                  (uint16_t)(hsteps * ACT_HAPPY_STEP_MILLI));
  return added;
}

// A value already credited today? Exact, linear over at most ACT_CAP_* entries,
// and it adds only when there is room - a full set answers "seen" to everything,
// which is right because the term is at its cap.
static bool seen_or_add(uint32_t* set, uint8_t& n, uint8_t cap, uint32_t v)
{
  for (uint8_t i = 0; i < n; ++i)
    if (set[i] == v) return true;
  if (n >= cap) return true;
  set[n++] = v;
  return false;
}

// -----------------------------------------------------------------------------
//  THE FOUR INPUTS
// -----------------------------------------------------------------------------
uint16_t act_note_carried(CooldownTable& t, uint32_t awake_s, const ActClock& c)
{
  if (!open_day(t, c)) return 0u;
  if (awake_s == 0u) return 0u;
  // A catch-up cannot pay for a day nobody was there, and the cap would swallow
  // it anyway; the clamp is what keeps the remainder inside u32 over a
  // multi-year absence. xp_carry_due() clamps to its window for the same reason.
  if (awake_s > (uint32_t)ACT_DAY_S) awake_s = (uint32_t)ACT_DAY_S;

  s_carry_rem_s += awake_s;
  uint32_t mins = s_carry_rem_s / 60u;
  s_carry_rem_s -= mins * 60u;
  if (mins == 0u) return 0u;

  const uint32_t room = (uint32_t)ACT_CAP_CARRY_MIN - (uint32_t)s_carry_min;
  if (mins > room) mins = room;
  if (mins == 0u) return 0u;

  s_carry_min = (uint8_t)(s_carry_min + mins);
  return bank(t, (uint16_t)(mins * ACT_PTS_CARRY));
}

uint16_t act_note_interaction(CooldownTable& t, const ActClock& c)
{
  if (!open_day(t, c)) return 0u;
  if (s_interact >= (uint8_t)ACT_CAP_INTERACT) return 0u;
  s_interact++;
  return bank(t, (uint16_t)ACT_PTS_INTERACT);
}

uint16_t act_note_networks(CooldownTable& t, const uint32_t* hashes, uint8_t n,
                           const ActClock& c)
{
  if (!open_day(t, c)) return 0u;
  if (hashes == nullptr || n == 0u) return 0u;

  uint16_t pts = 0u;
  for (uint8_t i = 0; i < n; ++i) {
    // net_hash 0 is CooldownRow's empty-row marker and the scanner folds it
    // away (NET_HASH_NEVER_ZERO). It is never explorable, so it is never
    // diversity either - and admitting it would let one padding word fill a
    // slot of the set.
    if (hashes[i] == 0u) continue;
    if (s_nets >= (uint8_t)ACT_CAP_NETS) break;
    if (seen_or_add(s_net, s_net_n, (uint8_t)ACT_CAP_NETS, hashes[i])) continue;
    s_nets++;
    pts = (uint16_t)(pts + bank(t, (uint16_t)ACT_PTS_NET));
  }
  return pts;
}

uint16_t act_note_peer(CooldownTable& t, uint32_t peer_id, const ActClock& c)
{
  if (!open_day(t, c)) return 0u;
  if (peer_id == 0u) return 0u;                 // 0 is "no device id" (gs_device_id)
  if (s_peers >= (uint8_t)ACT_CAP_PEERS) return 0u;
  if (seen_or_add(s_peer, s_peer_n, (uint8_t)ACT_CAP_PEERS, peer_id)) return 0u;
  s_peers++;
  return bank(t, (uint16_t)ACT_PTS_PEER);
}

// -----------------------------------------------------------------------------
//  READING IT
// -----------------------------------------------------------------------------
uint16_t act_score_today(const CooldownTable& t, const ActClock& c)
{
  if (!clock_ok(c)) return 0u;
  // The wall clock has moved past the stored day and nothing has been noted in
  // the new one: today's score really is zero, and saying so here rather than
  // waiting for the next act_note_*() is what stops yesterday's rare bonus from
  // being handed to this morning's first scan.
  if (act_day_index(c.now_epoch) > t.act_day) return 0u;
  // Equal, or the clock reads earlier than the stored day: the stored total is
  // the answer. A rollback therefore neither adds nor removes anything.
  return (t.act_score > (uint16_t)ACT_SCORE_MAX) ? (uint16_t)ACT_SCORE_MAX
                                                 : t.act_score;
}

uint16_t act_rare_bonus_pm(uint16_t score)
{
  if (score >= (uint16_t)ACT_SCORE_MAX) return (uint16_t)ENC_RARE_BONUS_MAX_PM;
  return (uint16_t)(((uint32_t)score * (uint32_t)ENC_RARE_BONUS_MAX_PM) /
                    (uint32_t)ACT_SCORE_MAX);
}

// The happiness half of the reward, scaled to the XP the ledger actually paid.
// See activity.h: this is the only rate limit the happiness has.
uint16_t act_happy_for_granted_xp(const ActGain& g, uint16_t granted_xp)
{
  if (g.happy_milli == 0u || granted_xp == 0u || g.xp == 0u) return 0u;
  if (granted_xp >= g.xp) return g.happy_milli;
  return (uint16_t)(((uint32_t)g.happy_milli * (uint32_t)granted_xp) / (uint32_t)g.xp);
}

ActGain act_take_gain(void)
{
  const ActGain g = s_gain;
  s_gain.points = s_gain.xp = s_gain.happy_milli = 0u;
  return g;
}

bool act_take_dirty(void)
{
  const bool d = (s_dirty != 0u);
  s_dirty = 0u;
  return d;
}

uint8_t act_carry_min(void)    { return s_carry_min; }
uint8_t act_interactions(void) { return s_interact; }
uint8_t act_nets_seen(void)    { return s_nets; }
uint8_t act_peers_seen(void)   { return s_peers; }
