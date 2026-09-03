// =============================================================================
//  PEBBLEBOL - minigames/games/ping_logic.cpp
//  PING (spec 29.1): "Press A when a moving indicator enters the target zone."
//  Ported from the REFLEJOS game that lived in ui.cpp.
//
//  PURE. stdint, rng.h, minigame.h. No Arduino, no gfx, no strings-by-value:
//  the "too soon" message is left in ctx.note as a StrId for the draw layer,
//  because the game this replaced called ui_toast() from inside its scoring.
//
//  THE DEAD TIME IS THE GAME. Each round waits a random PING_ARM_MIN_MS..
//  PING_ARM_MAX_MS before lighting a side. Without it "hold both buttons down"
//  wins every round, which is why the port keeps it rather than simplifying.
//
//  ONE TIMING NUMBER CHANGED, deliberately. The original armed at
//  700 + rng_below(1500) ms, i.e. up to 2199 ms, and gave a 900 ms window:
//  5 * (2199 + 900) = 15,495 ms, which BREAKS the 15 s ceiling spec 29 sets
//  (and MG_MAX_MS would have cut the last round off mid-play). The range is now
//  600..1999, so the worst case is 5 * (1999 + 900) = 14,495 ms.
// =============================================================================
#include "games.h"

#define PING_ROUNDS        5u
#define PING_ARM_MIN_MS  600u
#define PING_ARM_SPAN_MS 1400u     // max dead time = MIN + SPAN - 1 = 1999 ms
#define PING_WINDOW_MS   900u

// Per round: a perfect (0 ms) reaction is worth PING_ROUND_MAX, a reaction at
// the very edge of the window is worth 0. Five rounds -> exactly MG_SCORE_MAX.
#define PING_ROUND_MAX  (MG_SCORE_MAX / PING_ROUNDS)

struct PingState {
  uint32_t arm_ms;      // dead time of THIS round
  uint32_t round_t0;    // ctx.t_ms when the round started
  uint16_t last_ms;     // reaction time of the last round; 0 = missed or early
  uint8_t  target;      // MG_SIDE_L / MG_SIDE_R
  uint8_t  lit_seen;    // the light has been up at least one step this round
};

// Exposed for the draw half and for the tests, so neither re-derives the rule.
uint32_t ping_arm_ms(const MgCtx& c)  { return mg_state<PingState>(c).arm_ms; }
uint16_t ping_last_ms(const MgCtx& c) { return mg_state<PingState>(c).last_ms; }
uint8_t  ping_target(const MgCtx& c)  { return mg_state<PingState>(c).target; }

bool ping_is_lit(const MgCtx& c)
{
  const PingState& s = mg_state<PingState>(c);
  const uint32_t el = c.t_ms - s.round_t0;
  return el >= s.arm_ms && el <= s.arm_ms + PING_WINDOW_MS;
}

uint32_t ping_window_ms(void) { return PING_WINDOW_MS; }
uint8_t  ping_rounds(void)    { return PING_ROUNDS; }

static void ping_arm(MgCtx& c)
{
  PingState& s = mg_state<PingState>(c);
  s.target   = (uint8_t)(rng_next(c.rng) & 1u);
  s.arm_ms   = PING_ARM_MIN_MS + rng_next_below(c.rng, PING_ARM_SPAN_MS);
  s.round_t0 = c.t_ms;
  s.lit_seen = 0u;
}

static void ping_init(MgCtx& c)
{
  PingState& s = mg_state<PingState>(c);
  s.last_ms = 0u;
  ping_arm(c);
}

// A round ends, one way or another, and the game ends after PING_ROUNDS of them.
static void ping_next_round(MgCtx& c)
{
  if (++c.round >= PING_ROUNDS) { c.finished = 1u; return; }
  ping_arm(c);
}

static void ping_step(MgCtx& c)
{
  PingState& s = mg_state<PingState>(c);
  const uint32_t el = c.t_ms - s.round_t0;
  if (el >= s.arm_ms) s.lit_seen = 1u;
  if (el > s.arm_ms + PING_WINDOW_MS) {
    s.last_ms = 0u;                       // the window closed untouched
    ping_next_round(c);
  }
}

static void ping_press(MgCtx& c, uint8_t side)
{
  PingState& s = mg_state<PingState>(c);
  const uint32_t el = c.t_ms - s.round_t0;

  if (el < s.arm_ms) {
    // Too early. No score, no penalty beyond the round being spent: spec 27
    // forbids punishment, and losing the round is cost enough.
    c.note    = STR_GM_TOOSOON;
    s.last_ms = 0u;
  } else {
    const uint32_t rt = el - s.arm_ms;
    if (side == s.target && rt <= PING_WINDOW_MS) {
      s.last_ms = (uint16_t)rt;
      c.score   = (uint16_t)(c.score +
                  (PING_WINDOW_MS - rt) * PING_ROUND_MAX / PING_WINDOW_MS);
    } else {
      s.last_ms = 0u;                     // wrong side, or the window had closed
    }
  }
  ping_next_round(c);
}

static bool ping_done(const MgCtx& c) { return c.round >= PING_ROUNDS; }

static uint16_t ping_finish(MgCtx& c) { return c.score; }

extern const MgLogic MG_PING = {
  MG_ID_PING,
  STR_MG_PING,
  STR_MG_PING_HINT,
  ping_init,
  ping_press,
  ping_step,
  ping_done,
  ping_finish,
};
