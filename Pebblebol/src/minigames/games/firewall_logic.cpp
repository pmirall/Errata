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
//      c.score == fw_blocked(c) * FW_ROUND_MAX
//
//  written as that product rather than accumulated with +=, so the identity is
//  true by construction. tests/test_minigames.cpp asserts it at every step of
//  every tape, and it is the mirror of buffer's step-count identity. The day
//  someone adds "the shield is close, give a little" here, or "an event
//  happened, add a fixed amount" there, a test fails the same day.
//
//  STANDING STILL SCORES EXACTLY ZERO, structurally rather than statistically:
//  a packet's lane is drawn from the four lanes the shield is NOT in, at the
//  instant of the spawn. Every packet demands at least one press. Under naive
//  uniform spawning an idle run would block about one in five.
//
//  NO PUNISHMENT (spec 27): a leaked packet costs 125 points and nothing else,
//  ctx.note is never written, and a press into a wall is drawn rather than
//  announced - announcing it would fire the toast layer on every press of
//  exactly the player that clause is about.
// =============================================================================
#include "games.h"

#define FW_LANES        5u
#define FW_PACKETS      8u
#define FW_FALL_MAX  1600u      // packet 0 falls for this long
#define FW_FALL_STEP  100u      // each packet is one notch faster
#define FW_GAP_MS     250u      // the outcome pause after every impact
#define FW_ROUND_MAX (MG_SCORE_MAX / FW_PACKETS)   // 125

struct FwState {
  uint16_t spawn_t0;   // ctx.t_ms when the current packet spawned
  uint8_t  lane;       // its lane, 0..FW_LANES-1; never the shield's at spawn
  uint8_t  shield;     // CARRIES OVER between packets - that is the game
  uint8_t  outcome;    // 0 in flight, 1 blocked, 2 leaked; also the once-latch
  uint8_t  blocked;    // the multiplicand in the score identity
  uint8_t  history;    // bit i = packet i was blocked, for the pips
};
static_assert(sizeof(FwState) <= MG_STATE_BYTES, "FwState must fit MgCtx::state");

static_assert(MG_SCORE_MAX % FW_PACKETS == 0u,
              "perfect firewall play must reach exactly MG_SCORE_MAX");
static_assert(FW_PACKETS <= 8u, "FwState::history is a byte mask");
static_assert(FW_FALL_MAX > (FW_PACKETS - 1u) * FW_FALL_STEP,
              "the fall ramp must not underflow on the last packet");
static_assert(FW_FALL_MAX % MG_STEP_MS == 0u && FW_FALL_STEP % MG_STEP_MS == 0u &&
              FW_GAP_MS % MG_STEP_MS == 0u,
              "every firewall boundary must land on a step");
// 2000 + 10000 = 12,000 ms = 480 steps. Best case, worst case and idle case are
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
  // A uniform draw over exactly the four lanes the shield is NOT in.
  const uint8_t pick = (uint8_t)rng_next_below(c.rng, (uint32_t)(FW_LANES - 1u));
  s.lane     = (uint8_t)((pick >= s.shield) ? pick + 1u : pick);
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
      c.score    = (uint16_t)(s.blocked * FW_ROUND_MAX);
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
//  button edges allow so the shield sweeps two adjacent lanes. At spawn the
//  packet is drawn from the four lanes the shield is not in, so it lands in the
//  oscillator's partner lane one time in four, and the single sampled impact
//  catches the sweep on the right side of that pair about half the time: one
//  packet in eight, about 125 of 1000, against 1000 for real play and 0 for
//  standing still. There is no anti-mash penalty, no lockout and no cooldown -
//  scoring 125 instead of 1000 is cost enough, and spec 27 forbids the rest.
//
//  The collapse risk in reverse, worth naming: a future "make it easier" that
//  widens the shield to two lanes or pays for adjacency turns the score from a
//  count of events into a measure of proximity, which is BUFFER. The binary hit
//  and the one-lane shield are load-bearing, and the score identity asserted at
//  every step of every tape is what will catch it.
// -----------------------------------------------------------------------------
