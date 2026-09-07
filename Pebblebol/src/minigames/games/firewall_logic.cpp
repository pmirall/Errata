// =============================================================================
//  PEBBLEBOL - minigames/games/firewall_logic.cpp
//  FIREWALL (spec 29.3): "Move a shield left/right with the two buttons."
//
//  PURE, like every *_logic.cpp here. The step count is the clock.
//
//  DISCRETE AND REACTIVE, which is the whole of what separates it from BUFFER
//  (29.4), the other "hold a position with two buttons" game. Eight packets
//  ARRIVE, one at a time, each down one of five lanes; the shield must be
//  STANDING IN THAT LANE at the single sampled instant of impact. Being one
//  lane away is worth exactly what being at the far wall is worth: nothing.
//  There is no partial credit, no proximity term and no overlap window, and
//  the score is a pure function of an EVENT COUNT:
//
//      c.score == max(0, fw_blocked(c) - FW_LUCK_FLOOR) * FW_ROUND_MAX
//
//  written as that product rather than accumulated with +=, so the identity is
//  true by construction. tests/test_minigames.cpp asserts it at every step of
//  every tape, and it is the mirror of buffer's step-count identity. The day
//  someone adds "the shield is close, give a little" here, or "an event
//  happened, add a fixed amount" there, a test fails the same day.
//
//  STANDING STILL SCORES ZERO IN EXPECTATION, AND THE DRAW IS HONEST. It was
//  neither until this commit: a packet's lane was drawn from the four lanes the
//  shield was NOT in, so the packet dodged the player. That produced the right
//  number - an idle run scored exactly nothing - by making the game cheat, and a
//  player who noticed would be right to stop trusting it.
//
//  THE DRAW IS NOW UNIFORM OVER ALL FIVE LANES and the luck is subtracted
//  instead. An idle run blocks FW_PACKETS / FW_LANES packets by chance, so that
//  many blocks are not paid for:
//
//      score == max(0, blocked - FW_LUCK_FLOOR) * FW_ROUND_MAX
//
//  FW_LUCK_FLOOR IS DERIVED, NOT CHOSEN - it IS that expectation - which is why
//  FW_PACKETS is 10 rather than 8: ten packets over five lanes puts the floor at
//  exactly 2, leaves 8 scoring blocks, and keeps FW_ROUND_MAX at the same 125 a
//  perfect run has always been worth. Pick any other packet count and either the
//  floor stops being the expectation or the perfect run stops being MG_SCORE_MAX.
//
//  WHAT THIS COSTS, SAID PLAINLY: a lucky idle run can still block 4 or 5 and
//  score. The floor removes the MEAN, not the variance. What it buys is that the
//  packet no longer knows where the shield is.
//
//  NO PUNISHMENT (spec 27): a leaked packet costs 125 points and nothing else,
//  ctx.note is never written, and a press into a wall is drawn rather than
//  announced - announcing it would fire the toast layer on every press of
//  exactly the player that clause is about.
// =============================================================================
#include "games.h"

#define FW_LANES        5u
#define FW_PACKETS     10u      // 10 / 5 lanes puts FW_LUCK_FLOOR at exactly 2
#define FW_LUCK_FLOOR  (FW_PACKETS / FW_LANES)      // what chance alone blocks
#define FW_FALL_MAX  1600u      // packet 0 falls for this long
#define FW_FALL_STEP  100u      // each packet is one notch faster
#define FW_GAP_MS     250u      // the outcome pause after every impact
// The PAID blocks are the ones above the luck floor, so the denominator is the
// scoring range and not the packet count. 1000 / (10 - 2) = 125, unchanged.
#define FW_ROUND_MAX (MG_SCORE_MAX / (FW_PACKETS - FW_LUCK_FLOOR))   // 125
static_assert(FW_LUCK_FLOOR * FW_LANES == FW_PACKETS,
              "FW_LUCK_FLOOR must BE the idle expectation, not a number near it");
static_assert((FW_PACKETS - FW_LUCK_FLOOR) * FW_ROUND_MAX == MG_SCORE_MAX,
              "a perfect firewall run must still be worth exactly MG_SCORE_MAX");

struct FwState {
  uint16_t spawn_t0;   // ctx.t_ms when the current packet spawned
  uint8_t  lane;       // its lane, 0..FW_LANES-1; a uniform draw over all five
  uint8_t  shield;     // CARRIES OVER between packets - that is the game
  uint8_t  outcome;    // 0 in flight, 1 blocked, 2 leaked; also the once-latch
  uint8_t  blocked;    // the multiplicand in the score identity
  uint16_t history;    // bit i = packet i was blocked, for the pips
};
static_assert(sizeof(FwState) <= MG_STATE_BYTES, "FwState must fit MgCtx::state");

static_assert(MG_SCORE_MAX % (FW_PACKETS - FW_LUCK_FLOOR) == 0u,
              "perfect firewall play must reach exactly MG_SCORE_MAX");
// Was <= 8 while history was a uint8_t. FW_PACKETS went to 10 so FW_LUCK_FLOOR
// could BE the idle expectation; the mask widened with it rather than the packet
// count being bent back to fit a field.
static_assert(FW_PACKETS <= 16u, "FwState::history is a 16-bit mask");
static_assert(FW_FALL_MAX > (FW_PACKETS - 1u) * FW_FALL_STEP,
              "the fall ramp must not underflow on the last packet");
static_assert(FW_FALL_MAX % MG_STEP_MS == 0u && FW_FALL_STEP % MG_STEP_MS == 0u &&
              FW_GAP_MS % MG_STEP_MS == 0u,
              "every firewall boundary must land on a step");
// sum(1600 - 100i, i=0..9) + 10 gaps of 250 = 11,500 + 2,500 = 14,000 ms =
// 560 steps, against MG_MAX_MS 15,000. Best case, worst case and idle case are
// the same number: a press moves the shield and can neither resolve a packet
// early nor extend one.
static_assert(FW_PACKETS * FW_GAP_MS
              + (FW_FALL_MAX + FW_FALL_MAX - (FW_PACKETS - 1u) * FW_FALL_STEP)
                * FW_PACKETS / 2u < MG_MAX_MS,
              "the firewall run must fit inside the section 29 ceiling");
static_assert(FW_PACKETS * FW_GAP_MS
              + (FW_FALL_MAX + FW_FALL_MAX - (FW_PACKETS - 1u) * FW_FALL_STEP)
                * FW_PACKETS / 2u < UINT16_MAX,
              "FwState::spawn_t0 is 16-bit and must cover the whole run");

// Derived from the round, so both halves ask one function and nothing is
// stored that could disagree with it.
static uint16_t fw_fall_ms(uint8_t round)
{
  return (uint16_t)(FW_FALL_MAX - (uint32_t)round * FW_FALL_STEP);
}

// --- accessors --------------------------------------------------------------
uint8_t fw_lanes(void)               { return (uint8_t)FW_LANES; }
uint8_t fw_packets(void)             { return (uint8_t)FW_PACKETS; }
uint8_t fw_shield(const MgCtx& c)    { return mg_state<FwState>(c).shield; }
uint8_t fw_lane(const MgCtx& c)      { return mg_state<FwState>(c).lane; }
uint8_t fw_outcome(const MgCtx& c)   { return mg_state<FwState>(c).outcome; }
uint8_t fw_blocked(const MgCtx& c)   { return mg_state<FwState>(c).blocked; }

// How far down the packet is, 0..256 fixed point; 256 through the outcome gap.
uint16_t fw_fall_pos(const MgCtx& c)
{
  const FwState& s = mg_state<FwState>(c);
  if (s.outcome != 0u) return 256u;
  const uint16_t fall = fw_fall_ms(c.round);
  const uint16_t el   = (uint16_t)(c.t_ms - s.spawn_t0);
  if (el >= fall) return 256u;
  return (uint16_t)((uint32_t)el * 256u / fall);
}

// 0 = not played yet, 1 = blocked, 2 = leaked.
uint8_t fw_pip(const MgCtx& c, uint8_t i)
{
  const FwState& s = mg_state<FwState>(c);
  if (i >= FW_PACKETS || i > c.round) return 0u;
  if (i == c.round && s.outcome == 0u) return 0u;
  return (s.history & (uint8_t)(1u << i)) ? 1u : 2u;
}

// -----------------------------------------------------------------------------
//  THE GAME
// -----------------------------------------------------------------------------
static void fw_spawn(MgCtx& c)
{
  FwState& s = mg_state<FwState>(c);
  // A UNIFORM DRAW OVER ALL FIVE LANES, INCLUDING THE SHIELD'S. It excluded the
  // shield's lane until this commit, which is the difference between a hard game
  // and a rigged one. The luck that buys is taken back by FW_LUCK_FLOOR at the
  // score, not here - see the banner.
  s.lane     = (uint8_t)rng_next_below(c.rng, (uint32_t)FW_LANES);
  s.outcome  = 0u;
  s.spawn_t0 = (uint16_t)c.t_ms;
}

static void fw_init(MgCtx& c)
{
  FwState& s = mg_state<FwState>(c);
  s.shield = FW_LANES / 2u;        // the middle lane, deterministically
  fw_spawn(c);
}

static void fw_step(MgCtx& c)
{
  FwState& s = mg_state<FwState>(c);
  const uint16_t el   = (uint16_t)(c.t_ms - s.spawn_t0);
  const uint16_t fall = fw_fall_ms(c.round);

  // THE IMPACT, sampled at exactly one step. Binary: standing in the lane, or
  // not. Sweeping through it is worth nothing.
  if (s.outcome == 0u && el >= fall) {
    if (s.shield == s.lane) {
      s.outcome  = 1u;
      s.history  = (uint8_t)(s.history | (uint8_t)(1u << c.round));
      ++s.blocked;
      // The paid blocks only. Written as the product of a clamped difference,
      // never accumulated, so the identity in the banner is true by
      // construction at every step - the property tests/test_minigames.cpp
      // asserts on every tape.
      const uint8_t paid = (s.blocked > (uint8_t)FW_LUCK_FLOOR)
                             ? (uint8_t)(s.blocked - (uint8_t)FW_LUCK_FLOOR) : 0u;
      c.score    = (uint16_t)(paid * FW_ROUND_MAX);
    } else {
      s.outcome = 2u;
    }
    return;
  }

  // THE ROUND END, measured from spawn_t0 so the early return above costs no
  // time and eight rounds cannot drift.
  if (el >= (uint16_t)(fall + FW_GAP_MS)) {
    if (++c.round >= FW_PACKETS) { c.finished = 1u; return; }
    fw_spawn(c);
  }
}

// One press, one lane. input_pressed_edge() has no auto-repeat, so there is no
// hold state to model and none is invented. A press against a wall does
// nothing at all - the prong sitting flush against the panel edge is how the
// player is told, which costs no toast and no round.
static void fw_press(MgCtx& c, uint8_t side)
{
  FwState& s = mg_state<FwState>(c);
  if (side == MG_SIDE_L) { if (s.shield > 0u)             --s.shield; }
  else                   { if (s.shield < FW_LANES - 1u)  ++s.shield; }
}

static bool     fw_done(const MgCtx& c) { return c.round >= FW_PACKETS; }
static uint16_t fw_finish(MgCtx& c)     { return c.score; }

extern const MgLogic MG_FIREWALL = {
  MG_ID_FIREWALL,
  STR_MG_FIREWALL,
  STR_MG_FIREWALL_HINT,
  fw_init,
  fw_press,
  fw_step,
  fw_done,
  fw_finish,
};

// -----------------------------------------------------------------------------
//  THE CHEAPEST CHEAT is oscillating - alternating L and R as fast as the
//  button edges allow so the shield sweeps two adjacent lanes. With the uniform
//  draw a packet lands in one of those two lanes 2 times in 5, and the single
//  sampled impact catches the sweep on the right side of the pair about half the
//  time: one packet in five, so about 2 blocks in 10 - WHICH IS EXACTLY
//  FW_LUCK_FLOOR. The oscillator now scores what standing still scores, nothing,
//  and it gets there through the same subtraction rather than through a rule
//  written against it. Under the old rigged draw it scored 125 of 1000.
//
//  There is still no anti-mash penalty, no lockout and no cooldown: spec 27
//  forbids them and, with the floor doing this work, none is needed.
//
//  The collapse risk in reverse, worth naming: a future "make it easier" that
//  widens the shield to two lanes or pays for adjacency turns the score from a
//  count of events into a measure of proximity, which is BUFFER. The binary hit
//  and the one-lane shield are load-bearing, and the score identity asserted at
//  every step of every tape is what will catch it.
// -----------------------------------------------------------------------------
