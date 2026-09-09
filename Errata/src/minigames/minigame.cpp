// =============================================================================
//  ERRATA - minigames/minigame.cpp
//  The three-function driver of the contract in minigame.h. PURE: no Arduino,
//  no clock, no I/O. tests/test_minigames.cpp compiles it directly.
// =============================================================================
#include "minigame.h"

static void mg_zero(MgCtx& c)
{
  c.t_ms     = 0;
  c.score    = 0;
  c.note     = 0;
  c.round    = 0;
  c.finished = 0;
  for (uint8_t i = 0; i < MG_STATE_BYTES; ++i) c.state[i] = 0;
}

void mg_begin(MgCtx& c, const MgLogic& g, uint32_t seed)
{
  mg_zero(c);
  // The per-run stream. rng_init() maps seed 0 onto RNG_DEFAULT_SEED, so even
  // a caller that forgets to seed gets a reproducible game rather than a
  // degenerate one.
  rng_init(c.rng, seed);
  if (g.init) g.init(c);
}

bool mg_tick(MgCtx& c, const MgLogic& g)
{
  if (c.finished) return false;

  // t_ms BEFORE step(), so the first step of a run is at MG_STEP_MS and a game
  // that arms something "at t_ms + N" is never woken at its own start instant.
  c.t_ms += MG_STEP_MS;
  if (g.step) g.step(c);

  // The ceiling is enforced here rather than inside each game: spec section 29
  // gives every game 5-15 s, and a game that forgets to end must not be able
  // to strand the player on a screen with no exit.
  if (!c.finished && c.t_ms >= MG_MAX_MS) c.finished = 1u;

  if (g.done && g.done(c)) c.finished = 1u;
  return c.finished == 0u;
}

void mg_press(MgCtx& c, const MgLogic& g, uint8_t side)
{
  if (c.finished) return;            // a late button cannot score
  if (side != MG_SIDE_L && side != MG_SIDE_R) return;
  if (g.press) g.press(c, side);
  if (g.done && g.done(c)) c.finished = 1u;
}

uint16_t mg_score(MgCtx& c, const MgLogic& g)
{
  uint16_t s = g.finish ? g.finish(c) : c.score;
  if (s > MG_SCORE_MAX) s = MG_SCORE_MAX;
  c.score = s;
  return s;
}
