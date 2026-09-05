// =============================================================================
//  PEBBLEBOL - game/activity.h
//  THE DAILY ACTIVITY SCORE AND WHAT IT PAYS (spec sections 25 and 57, P6-C2).
//
//  Spec section 25 asks for an ABSTRACT activity score rather than a distance
//  the hardware cannot measure, and lists what it may influence: XP, happiness
//  and rare-event chance. This module is that score and those three rewards,
//  and nothing else. Four terms, each capped on its own:
//
//      score = min(carried minutes awake, ACT_CAP_CARRY_MIN) * ACT_PTS_CARRY
//            + min(interactions,          ACT_CAP_INTERACT)  * ACT_PTS_INTERACT
//            + min(distinct net_hash,     ACT_CAP_NETS)      * ACT_PTS_NET
//            + min(peers met,             ACT_CAP_PEERS)     * ACT_PTS_PEER
//
//  PURE MODULE, and clock-free the way game/encounters.cpp is clock-free: the
//  wall clock and how it came to hold its value arrive as an ActClock argument,
//  exactly as game/cooldowns.h takes a CdClock. stdint, data/balance.h and
//  persistence/save_schema.h and NOTHING else - tests/test_activity.cpp links
//  activity.o alone. No Arduino, no heap, no float, no RNG, no I/O.
//
// -----------------------------------------------------------------------------
//  THE ANTI-FARM ARGUMENT, IN THE ORDER THE LAYERS BITE
// -----------------------------------------------------------------------------
//  The ledger in game/xp.h exists because care XP was farmable. An activity
//  score is a strictly easier thing to farm than a care action - the device can
//  be power-cycled, the clock can be typed, and the same access point can be
//  scanned thirty times in a minute - so the score gets five layers, and each
//  one is named here so a reader can check which one is doing the work:
//
//   1. AN UNCALIBRATED CLOCK SCORES ZERO. While cal == CAL_UNSET, gt_now() is
//      "last persisted epoch plus uptime" and restarts near zero every boot, so
//      there is no day to bucket into. Every act_note_*() returns 0 and banks
//      nothing. This is the same withholding game/encounters.cpp applies to the
//      SPECIAL XP burst, reused verbatim rather than re-argued: the one thing
//      that can rate-limit a reward is real time, and while the clock is a
//      guess there is none. An epoch below NT_EPOCH_SANE_MIN is refused too,
//      whatever the caller claims about calibration.
//
//   2. THE DAY COMES FROM THE WALL CLOCK AND ONLY EVER MOVES FORWARD. The day
//      index is now_epoch / ACT_DAY_S - never a RAM counter, so a reboot cannot
//      roll the day - and act_day is monotonic: a clock that reads EARLIER than
//      the stored day does not roll it and does not reset the score. The spent
//      budget stands. Rolling back is therefore worth exactly nothing, which is
//      game/cooldowns.h's "a rollback never shortens a cooldown" applied to a
//      counter that, unlike a deadline, does not get it for free.
//
//   3. THE DAY'S TOTAL IS PERSISTED, IN CooldownTable.act_score. This is the
//      layer that survives a power cycle. Everything a reward is computed from
//      is that one number, and it is capped at ACT_SCORE_MAX - which is exactly
//      the sum of the four capped terms, i.e. exactly what an honest day can
//      reach. See "WHAT A POWER CYCLE STILL BUYS" below for the residual.
//
//   4. PER-TERM CAPS, and a DISTINCT set behind the two terms that repeat. The
//      same access point seen thirty times scores once: act_note_networks()
//      holds an EXACT set of the day's credited net_hash values, and the same
//      for peers. Counting cd_arm() calls instead - the cheap version - would
//      count one access point twelve times a day, which is a farm wearing a
//      diversity score's name.
//
//   5. THE XP LEDGER UNDERNEATH ALL OF IT. Activity XP is paid through
//      XP_SRC_CARRY, which is metered (XP_CAP_CARRY per XP_WIN_CARRY_S) and
//      whose budget a reboot cannot refill (xp_ledger_restore()). Even if every
//      rule above had a bug, the XP is still rate-limited by a bucket that
//      re-seeds to ZERO on an untrusted clock. See "THE FIFTH SLOT" below.
//
// -----------------------------------------------------------------------------
//  WHAT A POWER CYCLE STILL BUYS, SAID PLAINLY
// -----------------------------------------------------------------------------
//  Four bytes of CooldownTable hold the day and the day's total. The four
//  per-term counters, the distinct-network set and the distinct-peer set are
//  per-boot RAM. So a power cycle DOES clear the per-term caps: a player who
//  reboots can walk past the same ten networks again and be credited for them
//  again.
//
//  What that buys is bounded, and the bound is the point. act_score is
//  persisted and capped at ACT_SCORE_MAX, so the rebooting player reaches AT
//  MOST the total an honest player reaches by carrying the device for four
//  hours - sooner, but never higher, and never twice in a day. The trade is the
//  same shape game/cooldowns.h took for the uncalibrated table: a CATASTROPHIC
//  farm (unbounded score) exchanged for a LINEAR one (the honest daily ceiling,
//  reached the tedious way). Persisting the four counters as well would close
//  it, and it would cost the two bytes of CooldownTable.reserved_b plus a write
//  every minute; it is not worth that until something is measured that says so.
//
//  The XP half of the reward does not even have that residual: XP is owed for
//  crossing ACT_XP_STEP_POINTS thresholds of the day's CUMULATIVE, PERSISTED
//  score, so score already banked can never be paid for twice, whatever the
//  per-term counters do.
//
// -----------------------------------------------------------------------------
//  THE FIFTH SLOT THAT DOES NOT EXIST (the phase-5 exit's second debt)
// -----------------------------------------------------------------------------
//  XP_LEDGER_SLOTS is 4 and game/xp.h asserts that the metered XpSources are
//  exactly the first four enumerators, each owning one byte of
//  Inventory.xp_ledger. XP_ACTIVITY would be a fifth, and a fifth is not an
//  enum edit: it makes Inventory.xp_ledger 5 B, moves offsetof(items) from 16
//  to 17 and fails that struct's own assert - or keeps 32 B by taking
//  INVENTORY_SLOTS from 7 to 6, which silently deletes the seventh item stack
//  out of every save in existence. Either way it is an in-place edit of a
//  persisted layout, which is the one thing save_schema.h's rule 5 forbids.
//
//  SO ACTIVITY XP IS NOT A NEW SOURCE. It is paid through XP_SRC_CARRY, which
//  is ALREADY the daily-windowed bucket and whose declared meaning is "time
//  carried awake" - the activity score's own largest term. The choice is made
//  in writing here, as the plan's carried-forward box demands, and the cost is
//  written down with it: activity XP and the passive carry drip now COMPETE for
//  one XP_CAP_CARRY budget, so a heavy-walking day crowds out the drip. That is
//  arguably correct, because the two measure the same thing - but it is a
//  design choice, not a free win, and a day that earns 48 XP from carrying is
//  a day that earns none from networks.
//
//  It is not the unmetered answer XP_SRC_SPECIAL took either, and that is
//  deliberate: an unmetered source is bounded only by the daily cap, and the
//  daily cap is only real if the day survives a restart. P6-C3 makes the device
//  restart itself every ten idle minutes.
// =============================================================================
#ifndef PB_GAME_ACTIVITY_H
#define PB_GAME_ACTIVITY_H

#include <stdint.h>

#include "../data/balance.h"              // ENC_RARE_BONUS_MAX_PM, NT_EPOCH_SANE_MIN
#include "../persistence/save_schema.h"   // CooldownTable, TimeCal

// -----------------------------------------------------------------------------
//  THE TUNING. Spec section 25 gives the four terms and their multipliers and
//  no caps at all; the caps are P6-C2 decisions and live here, next to the
//  formula they tune, for the same reason ENCOUNTER_LEVEL_SPREAD lives in
//  game/encounters.h - so the test and the code cannot disagree about a number
//  that is nowhere in the content pack. The one number that DOES live in
//  data/balance.h is ENC_RARE_BONUS_MAX_PM, because game/encounters.cpp clamps
//  to it and this module scales to it, and a constant with two readers gets one
//  owner.
//
//  THE SHAPE OF THE CAPS. A full day is four hours of carrying, twenty care
//  actions, ten distinct networks and four peers. Carrying is the biggest term
//  because it is the one the spec names the mechanic after, and it is the one a
//  cheat cannot shorten: minutes cost minutes. The network term is the largest
//  per event (10) because walking somewhere new is the behaviour the game is
//  trying to reward and the hardest to fake at scale; peers are worth more
//  still (15) and capped hardest (4), because meeting people is rarer than
//  passing routers.
// -----------------------------------------------------------------------------
#define ACT_CAP_CARRY_MIN     240u   // minutes carried awake that can score in a day
#define ACT_PTS_CARRY           1u
#define ACT_CAP_INTERACT       20u   // care actions that can score in a day
#define ACT_PTS_INTERACT        2u
#define ACT_CAP_NETS           10u   // DISTINCT net_hash values that can score in a day
#define ACT_PTS_NET            10u
#define ACT_CAP_PEERS           4u   // DISTINCT peers that can score in a day
#define ACT_PTS_PEER           15u

// The honest daily maximum, and the clamp act_score is held at. It is written
// as the sum rather than as 440 so that retuning any cap moves it.
#define ACT_SCORE_MAX  ((uint16_t)(ACT_CAP_CARRY_MIN * ACT_PTS_CARRY   + \
                                   ACT_CAP_INTERACT  * ACT_PTS_INTERACT + \
                                   ACT_CAP_NETS      * ACT_PTS_NET      + \
                                   ACT_CAP_PEERS     * ACT_PTS_PEER))

// The day. UTC midnight, not local midnight, and the reason is the layering:
// this module is pure and the only thing that can turn an epoch into a local
// date is hardware/gametime.cpp's localtime_r(). A TZ-local boundary would mean
// either a hardware dependency in a pure module or the TZ string as a fifth
// argument to every call. The cost is that the day rolls at midnight UTC rather
// than at the player's midnight; the anti-farm properties do not depend on
// WHERE the boundary is, only that it comes from the wall clock and moves one
// way, so this is a presentation choice and it is disclosed rather than hidden.
#define ACT_DAY_S           86400UL

// WHAT THE SCORE PAYS.
//   XP        - one point per ACT_XP_STEP_POINTS of the day's CUMULATIVE score,
//               so a full day is ACT_SCORE_MAX / ACT_XP_STEP_POINTS = 44 XP,
//               just under XP_CAP_CARRY (48) - the meter is meant to be close
//               enough to bind on a heavy day and not so tight that an ordinary
//               one is silently thrown away.
//   HAPPINESS - ACT_HAPPY_STEP_MILLI care milli-points per ACT_HAPPY_STEP_POINTS
//               of the day's cumulative score. A full day is 22 steps = 5,500
//               milli = 5.5 % of the happiness bar (PB_CARE_MILLI_MAX 100000):
//               a real nudge for carrying the thing around, far from a
//               substitute for feeding it.
//   RARE      - a permille handed to the NEXT encounter roll, scaled linearly
//               from 0 at score 0 to ENC_RARE_BONUS_MAX_PM at ACT_SCORE_MAX.
#define ACT_XP_STEP_POINTS       10u
#define ACT_HAPPY_STEP_POINTS    20u
#define ACT_HAPPY_STEP_MILLI    250u

// -----------------------------------------------------------------------------
// Everything this module needs from the outside, in one struct - the shape
// game/cooldowns.h's CdClock established.
//   now_epoch - gt_now(). Refused below NT_EPOCH_SANE_MIN whatever `cal` says.
//   cal       - gt_cal_state(). CAL_UNSET scores ZERO, layer 1 above.
// -----------------------------------------------------------------------------
struct ActClock {
  uint32_t now_epoch;
  uint8_t  cal;        // TimeCal
};

// What an act_note_*() call earned, accumulated until the app drains it. The
// notes themselves return only the POINTS, because a screen must not award XP:
// the app drains this once per tick and pays it through the one XP funnel and
// the one happiness path, exactly as sim_take_events() is drained.
struct ActGain {
  uint16_t points;       // added to the day's score by the calls since the drain
  uint16_t xp;           // XP owed, already threshold-counted (never re-earned)
  uint16_t happy_milli;  // care milli-points owed against CARE_HAPPINESS
};

// -----------------------------------------------------------------------------
// act_begin()
//   Clears the PER-BOOT half: the four term counters, the distinct-network set,
//   the distinct-peer set, the carried-seconds remainder, the pending gain and
//   the dirty flag. Call once from setup(), and from a test between cases.
//   NEVER touches the persisted half - act_day and act_score come off flash
//   with the rest of GameState, and clearing them here would hand a farm to
//   anyone who can cause a reset.
// -----------------------------------------------------------------------------
void act_begin(void);

// -----------------------------------------------------------------------------
// act_day_index(now_epoch) -> the day `now_epoch` falls in.
//   Named here so the app, the test and this module cannot each divide by their
//   own constant, exactly as encounter_bucket() is. TOTAL over the whole u32
//   epoch range: the widest possible answer is 49,710, which is why act_day is
//   a u16 and cannot overflow before the epoch itself does.
// -----------------------------------------------------------------------------
uint16_t act_day_index(uint32_t now_epoch);

// -----------------------------------------------------------------------------
//  THE FOUR INPUTS. Each returns the POINTS ADDED to the day's score - 0 when
//  the term is already at its cap, when the day's total is at ACT_SCORE_MAX,
//  when the clock is not trustworthy, or when the input is a repeat. Each may
//  roll the day forward in `t`; poll act_take_dirty() afterwards.
// -----------------------------------------------------------------------------

// awake_s seconds of the active Pebble being carried AWAKE. Whole minutes are
// credited; the remainder is kept in RAM for the next call, so any step size
// gives the same answer over the same elapsed time (the care integrator's
// discipline). The remainder is per-boot and is lost on a reset, which is one
// minute at most and always in the player's disfavour.
uint16_t act_note_carried(CooldownTable& t, uint32_t awake_s, const ActClock& c);

// One care action landed. The caller is ui.cpp's action path, which is already
// rate-limited by sim.h's hourly gain ceiling before it gets here.
uint16_t act_note_interaction(CooldownTable& t, const ActClock& c);

// THE WHOLE SCAN, and it must be the whole scan. `hashes` is the scanner's
// net_hash array and `n` its count; zero hashes are skipped (net_hash 0 is the
// cooldown table's empty-row marker and is never explorable). Only values not
// already credited TODAY score, so the same access point seen thirty times
// scores once.
//
// THE ORDERING TRAP, named because ui/screen_network.cpp walks straight into
// it: hand this the array BEFORE the cooldown walk. Diversity is about what was
// SEEN, not about what was explorable, and hand_off() returns on the first
// off-cooldown network - so calling this from inside that loop would count one
// network out of a dozen and would stop counting entirely once the street was
// on cooldown.
uint16_t act_note_networks(CooldownTable& t, const uint32_t* hashes, uint8_t n,
                           const ActClock& c);

// One peer met, keyed by its device_id. Distinct per day like the networks.
// NO CALLER UNTIL P7-C1, which is when peer discovery exists; the term is
// implemented and tested now because the score is section 25's score and not a
// subset of it, and because a term bolted on later is a term whose caps and
// distinctness nobody re-derives.
uint16_t act_note_peer(CooldownTable& t, uint32_t peer_id, const ActClock& c);

// -----------------------------------------------------------------------------
//  READING IT
// -----------------------------------------------------------------------------
// The day's score as the rewards see it. 0 while the clock is CAL_UNSET or
// insane, and 0 when the wall clock has moved PAST the stored day and nothing
// has been noted in the new one yet. A clock that reads EARLIER than the stored
// day returns the stored score unchanged - the budget stands, which is layer 2.
// Never mutates `t`: this is the query a screen may call on every frame.
uint16_t act_score_today(const CooldownTable& t, const ActClock& c);

// The rare-encounter bonus that score is worth, in permille, for
// EncounterInput.rare_bonus_pm. Monotone non-decreasing, 0 at score 0 and
// exactly ENC_RARE_BONUS_MAX_PM at ACT_SCORE_MAX. Integer and truncating, so
// the curve under-pays rather than over-pays at every point.
uint16_t act_rare_bonus_pm(uint16_t score);

// Drains the rewards owed since the last call and zeroes them. The app pays
// them through app_award_xp(XP_SRC_CARRY) and the happiness path.
ActGain act_take_gain(void);

// True once after any call that CHANGED the persisted half of `t` (act_day or
// act_score), then false until the next one - cd_take_dirty()'s contract, and
// for the same reason: the caller saves on true, and more than one entry point
// can dirty the table.
bool act_take_dirty(void);

// The per-boot counters, for DIAG and for the tests that have to see a cap
// bind rather than infer it from a score.
uint8_t act_carry_min(void);
uint8_t act_interactions(void);
uint8_t act_nets_seen(void);
uint8_t act_peers_seen(void);

#endif  // PB_GAME_ACTIVITY_H
