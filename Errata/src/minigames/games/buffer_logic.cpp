// =============================================================================
//  ERRATA - minigames/games/buffer_logic.cpp
//  BUFFER (spec 29.4): "Keep a cursor inside a moving safe zone."
//
//  PURE, like every *_logic.cpp here. The step count is the clock - and here
//  it is also the SCORE, which is the point.
//
//  CONTINUOUS, which is the whole of what separates it from FIREWALL (29.3),
//  the other "hold a position with two buttons" game. There are no rounds, no
//  arrivals, no targets and no discrete events of any kind: one 400-step run,
//  of which 340 steps are scored, and the run is worth the FRACTION OF THAT
//  TIME spent inside a zone that drifts. The score is a pure function of a
//  STEP COUNT:
//
//      c.score == buf_inside_steps(c) * MG_SCORE_MAX / BUF_SCORE_STEPS
//
//  assigned unconditionally on every scored step - never with a += anywhere in
//  this file - so the identity holds at every step and tests/test_minigames.cpp
//  asserts exactly that. It is the mirror of firewall's event-count identity.
//  Take the clock away and there is nothing left to score; take it away from
//  firewall and you still have the hits.
//
//  AND A PRESS IS ACCELERATION, NOT POSITION. press() adds an impulse to a
//  velocity that then decays to nothing over about a dozen steps, and does
//  NOTHING else: it never scores, never ends anything, never touches t_ms. You
//  place a firewall shield; you fly a buffer cursor, and the first correction
//  overshoots.
// =============================================================================
#include "games.h"

// The track, in Q4 fixed point: 1 px = 16 units, so a position is 0..1920 and
// fits an int16_t with room to spare.
#define BUF_Q            16
#define BUF_TRACK_PX    120
#define BUF_TRACK16     (BUF_TRACK_PX * BUF_Q)     // 1920

#define BUF_KICK         12      // 0.75 px/step added by one press
#define BUF_VMAX         40      // 2.5 px/step: four times the zone's top speed

#define BUF_BAND_LO      24      // px: the zone centre never goes below this
#define BUF_BAND_HI      96      // px: ... nor above this
#define BUF_BAND_LO16   (BUF_BAND_LO * BUF_Q)
#define BUF_BAND_HI16   (BUF_BAND_HI * BUF_Q)

#define BUF_HW_MAX       12      // px: the zone's opening half-width
#define BUF_HW_MIN        8      // px: ... and its closing one
#define BUF_HW_MAX16    (BUF_HW_MAX * BUF_Q)       // 192
#define BUF_HW_MIN16    (BUF_HW_MIN * BUF_Q)       // 128

#define BUF_ZV_MIN        5      // zone speed, Q4/step: 0.31 px/step
#define BUF_ZV_SPAN       6      // ... up to 0.63 px/step
#define BUF_TURN_MIN     40      // steps between reversals: 1.0 s
#define BUF_TURN_SPAN    48      // ... up to 2.2 s

#define BUF_TOTAL_MS  10000u
#define BUF_LEAD_MS    1500u     // the approach, not scored
#define BUF_TOTAL_STEPS (BUF_TOTAL_MS / MG_STEP_MS)   // 400
#define BUF_LEAD_STEPS  (BUF_LEAD_MS  / MG_STEP_MS)   // 60
#define BUF_SCORE_STEPS (BUF_TOTAL_STEPS - BUF_LEAD_STEPS)   // 340

struct BufState {
  int16_t  cur;       // cursor position, Q4, 0..BUF_TRACK16
  int16_t  vel;       // cursor velocity, Q4/step, clamped to +-BUF_VMAX
  int16_t  zc;        // safe-zone centre, Q4, clamped to the band
  uint16_t inside;    // scored steps spent inside. THE score, and its only input
  int8_t   zv;        // zone velocity, Q4/step, magnitude BUF_ZV_MIN..
  uint8_t  turn_in;   // steps until the zone re-picks zv; always >= 1
};
static_assert(sizeof(BufState) <= MG_STATE_BYTES, "BufState must fit MgCtx::state");

static_assert(BUF_LEAD_STEPS + BUF_SCORE_STEPS == BUF_TOTAL_STEPS,
              "the scored window must be the run minus the lead-in");
static_assert(BUF_TOTAL_STEPS * MG_STEP_MS < MG_MAX_MS,
              "the buffer run must fit inside the section 29 ceiling");
static_assert(BUF_TOTAL_MS % MG_STEP_MS == 0u && BUF_LEAD_MS % MG_STEP_MS == 0u,
              "both buffer boundaries must land on a step");
// The idle-zero proof, as a build error: the zone can never touch either wall,
// so a cursor pinned at a wall is never inside it, for any seed.
static_assert(BUF_BAND_LO > BUF_HW_MAX && BUF_BAND_HI + BUF_HW_MAX < BUF_TRACK_PX,
              "the zone must never reach a wall, or idle play scores");
static_assert(BUF_VMAX > BUF_ZV_MIN + BUF_ZV_SPAN,
              "the cursor must be able to out-run the zone");

// NEITHER the step index NOR the scored-step count is stored: both are derived
// from c.t_ms, which is the clock. The shrink and the score read this, and so
// does the draw half, so the zone can never render at a width the logic is not
// scoring against.
static uint16_t buf_scored(const MgCtx& c)
{
  return (c.t_ms > BUF_LEAD_MS)
         ? (uint16_t)((c.t_ms - BUF_LEAD_MS) / MG_STEP_MS) : 0u;
}

// The zone narrows from 24 px wide to 16 px across the run. That is the whole
// difficulty arc; the zone's SPEED never changes.
static int16_t buf_hw16(const MgCtx& c)
{
  return (int16_t)(BUF_HW_MAX16 -
                   (int32_t)(BUF_HW_MAX16 - BUF_HW_MIN16) * buf_scored(c) /
                   BUF_SCORE_STEPS);
}

static int16_t buf_abs16(int16_t v) { return (int16_t)(v < 0 ? -v : v); }

// --- accessors --------------------------------------------------------------
uint8_t  buf_track_px(void)              { return (uint8_t)BUF_TRACK_PX; }
uint16_t buf_scored_steps(void)          { return (uint16_t)BUF_SCORE_STEPS; }
uint16_t buf_inside_steps(const MgCtx& c){ return mg_state<BufState>(c).inside; }
bool     buf_is_live(const MgCtx& c)     { return c.t_ms > BUF_LEAD_MS; }

uint8_t buf_cursor_px(const MgCtx& c)
{
  return (uint8_t)(mg_state<BufState>(c).cur / BUF_Q);
}
uint8_t buf_zone_lo_px(const MgCtx& c)
{
  const int16_t lo = (int16_t)(mg_state<BufState>(c).zc - buf_hw16(c));
  return (uint8_t)((lo > 0) ? lo / BUF_Q : 0);
}
uint8_t buf_zone_hi_px(const MgCtx& c)
{
  const int16_t hi = (int16_t)(mg_state<BufState>(c).zc + buf_hw16(c));
  return (uint8_t)((hi < BUF_TRACK16) ? hi / BUF_Q : BUF_TRACK_PX);
}
bool buf_is_inside(const MgCtx& c)
{
  const BufState& s = mg_state<BufState>(c);
  return buf_abs16((int16_t)(s.zc - s.cur)) <= buf_hw16(c);
}

// Q4 internals, for the scripted tapes in tests/test_minigames.cpp only. What
// is under test is the engine, not a simulated human's thumb, so a tape is
// allowed to read the state it is steering - exactly as seq_symbol_at() is.
int16_t buf_cursor_q4(const MgCtx& c)   { return mg_state<BufState>(c).cur; }
int16_t buf_vel_q4(const MgCtx& c)      { return mg_state<BufState>(c).vel; }
int16_t buf_zone_q4(const MgCtx& c)     { return mg_state<BufState>(c).zc; }
int16_t buf_zone_vel_q4(const MgCtx& c) { return mg_state<BufState>(c).zv; }
int16_t buf_kick_q4(void)               { return (int16_t)BUF_KICK; }

// -----------------------------------------------------------------------------
//  THE GAME
// -----------------------------------------------------------------------------
static void buf_pick_turn(MgCtx& c)
{
  BufState& s = mg_state<BufState>(c);
  const int16_t m = (int16_t)(BUF_ZV_MIN + rng_next_below(c.rng, BUF_ZV_SPAN));
  s.zv      = (int8_t)((rng_next(c.rng) & 1u) ? m : -m);
  s.turn_in = (uint8_t)(BUF_TURN_MIN + rng_next_below(c.rng, BUF_TURN_SPAN));
}

static void buf_init(MgCtx& c)
{
  BufState& s = mg_state<BufState>(c);
  // The cursor always starts at the left wall with no velocity, and the zone
  // always starts 30..60 px away. The seed changes the CHASE, never the
  // handicap - so "run it again until the start is easy" is not a strategy.
  s.cur = 0;
  s.vel = 0;
  s.zc  = (int16_t)((30u + rng_next_below(c.rng, 31u)) * BUF_Q);
  buf_pick_turn(c);
}

// THE WHOLE PRESS FUNCTION, and the structural reason this cannot be firewall.
static void buf_press(MgCtx& c, uint8_t side)
{
  BufState& s = mg_state<BufState>(c);
  s.vel = (int16_t)(s.vel + ((side == MG_SIDE_L) ? -BUF_KICK : BUF_KICK));
  if (s.vel >  BUF_VMAX) s.vel =  BUF_VMAX;
  if (s.vel < -BUF_VMAX) s.vel = -BUF_VMAX;
}

static void buf_step(MgCtx& c)
{
  BufState& s = mg_state<BufState>(c);

  // 1. Friction. Integer division truncates toward zero, so it is
  //    sign-symmetric and the velocity reaches EXACTLY 0 rather than crawling
  //    at 1. Measured, not estimated: one tap from rest is 11,10,9...1,0 - a
  //    4 px glide over 12 steps, 300 ms - which is what makes this a steering
  //    task rather than a placement one. Holding a direction is what reaches
  //    BUF_VMAX, and stopping there takes the other button.
  s.vel = (int16_t)(((int32_t)s.vel * 15) / 16);

  // 2. Travel. The walls ABSORB rather than bounce - a bounce would let idle
  //    play drift into scoring range.
  s.cur = (int16_t)(s.cur + s.vel);
  if (s.cur < 0)           { s.cur = 0;           s.vel = 0; }
  if (s.cur > BUF_TRACK16) { s.cur = BUF_TRACK16; s.vel = 0; }

  // 3. The zone. It never stops and it never reaches a wall.
  s.zc = (int16_t)(s.zc + s.zv);
  if (--s.turn_in == 0u) buf_pick_turn(c);
  if (s.zc < BUF_BAND_LO16 || s.zc > BUF_BAND_HI16) {
    s.zc      = (int16_t)((s.zc < BUF_BAND_LO16) ? BUF_BAND_LO16 : BUF_BAND_HI16);
    s.zv      = (int8_t)(-s.zv);
    s.turn_in = (uint8_t)(BUF_TURN_MIN + rng_next_below(c.rng, BUF_TURN_SPAN));
  }

  // 4/5. The shrink is derived, and the score is REWRITTEN every scored step
  //      from `inside` - whether or not the cursor is inside. No += anywhere.
  if (c.t_ms > BUF_LEAD_MS) {
    if (buf_abs16((int16_t)(s.zc - s.cur)) <= buf_hw16(c)) ++s.inside;
    c.score = (uint16_t)((uint32_t)s.inside * MG_SCORE_MAX / BUF_SCORE_STEPS);
  }

  // 6. The only line that ends the run, and nothing a player does is in it.
  if (c.t_ms >= BUF_TOTAL_MS) c.finished = 1u;
}

static bool     buf_done(const MgCtx& c) { return c.finished != 0u; }
static uint16_t buf_finish(MgCtx& c)     { return c.score; }

extern const MgLogic MG_BUFFER = {
  MG_ID_BUFFER,
  STR_MG_BUFFER,
  STR_MG_BUFFER_HINT,
  buf_init,
  buf_press,
  buf_step,
  buf_done,
  buf_finish,
};

// -----------------------------------------------------------------------------
//  THE CHEAPEST CHEAT is mashing both buttons, betting that input rate stands
//  in for aim. The impulses are equal and opposite, so it nets almost nothing
//  and drifts the cursor into a wall the band excludes; a one-sided mash
//  saturates at BUF_VMAX and pins against the other wall, which the band
//  excludes too.
//
//  The real one is PARKING in the middle of the band and letting the zone come
//  to you. It is bounded by geometry rather than by a rule: the zone centre
//  wanders a 72 px band and is within a half-width of a fixed column about
//  2*hw/72 of the time - 33% at the opening 12 px, 22% at the closing 8 px, so
//  roughly 280 of 1000 over the run. That is under the 500 at which
//  minigames_won increments, and far under the 700-850 that steering gets.
//
//  Note WHICH knob the shrink turns, because it is the whole tuning argument:
//  widening the closing zone helps the player who is TRACKING, whose error is
//  bounded by their control authority, and barely helps the parker, whose error
//  is bounded by the band. The shrink is also monotone, so parking is worth
//  least exactly where most of the scored steps are. Spec 27 is respected
//  throughout: parking is a low score, never a penalty and never a message -
//  ctx.note is never written by this game.
// -----------------------------------------------------------------------------
