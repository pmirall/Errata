// =============================================================================
//  ERRATA - minigames/games/sequence_logic.cpp
//  SEQUENCE (spec 29, "additional candidates"): "Remember and reproduce a short
//  A/B sequence." Ported from the MEMORIA game that lived in ui.cpp.
//
//  PURE, like every *_logic.cpp here. The step count is the clock.
//
//  IT IS DELIBERATELY SHORTER THAN THE GAME IT REPLACES. MEMORIA ran 6 levels
//  of 3..8 steps; the plan shortens it to 3..5, and this ships 3 levels of
//  3, 4 and 5. That is a CONTENT change, not a port bug. The arithmetic is why:
//  playback costs SEQ_FLASH_MS + SEQ_GAP_MS per symbol, so the old worst case
//  was 6 levels averaging 5.5 symbols = about 18 s of playback ALONE, before
//  the player pressed anything - over the 15 s ceiling spec 29 sets. Three
//  levels of 3+4+5 symbols cost 12 * 550 = 6.6 s of playback and leave the rest
//  of the budget to the player.
// =============================================================================
#include "games.h"

#define SEQ_LEVELS        3u
#define SEQ_FIRST_LEN     3u       // levels are 3, 4, 5 symbols
#define SEQ_MAX_LEN       5u
#define SEQ_FLASH_MS    300u
#define SEQ_GAP_MS      150u
#define SEQ_SLOT_MS     (SEQ_FLASH_MS + SEQ_GAP_MS)

// THE ANSWER DEADLINE, and why it exists. Without it this game never ends on
// its own: playback finishes and it waits for a press for ever, so a player
// who puts the device down sits on the GAME screen until the manager's
// MG_MAX_MS backstop fires. The game this was ported from had exactly that
// hole. The clock is per SYMBOL and is pushed forward by every correct press,
// so it bounds the run without hurrying someone who is actually playing.
//
// The budget has to close: the whole run is 12 symbols (3 + 4 + 5), so
// 12 * SEQ_SLOT_MS of playback plus 12 * SEQ_ANSWER_MS of thinking must stay
// under MG_MAX_MS. 12 * 450 + 12 * 750 = 14,400 ms. The static_assert below
// is what keeps a future tweak honest.
#define SEQ_ANSWER_MS   750u

struct SeqState {
  uint32_t show_t0;             // ctx.t_ms when playback of this level began
  uint32_t deadline;            // ctx.t_ms by which the next symbol must arrive
  uint8_t  seq[SEQ_MAX_LEN];
  uint8_t  len;
  uint8_t  play_idx;            // how many symbols have been shown
  uint8_t  pos;                 // how many the player has matched
};
static_assert(SEQ_FIRST_LEN + SEQ_LEVELS - 1u == SEQ_MAX_LEN,
              "the level ladder must end exactly at SEQ_MAX_LEN");

// Total symbols across the ladder: 3 + 4 + 5.
#define SEQ_TOTAL_SYMBOLS ((SEQ_FIRST_LEN + SEQ_MAX_LEN) * SEQ_LEVELS / 2u)
static_assert(SEQ_TOTAL_SYMBOLS * (SEQ_SLOT_MS + SEQ_ANSWER_MS) < MG_MAX_MS,
              "the worst-case sequence run must fit inside the section 29 ceiling");

// Read by the draw half; neither side re-derives the playback rule.
uint8_t  seq_len_of(const MgCtx& c)     { return mg_state<SeqState>(c).len; }
uint8_t  seq_pos_of(const MgCtx& c)     { return mg_state<SeqState>(c).pos; }
bool     seq_is_showing(const MgCtx& c) { const SeqState& s = mg_state<SeqState>(c);
                                          return s.play_idx < s.len; }
uint8_t  seq_levels(void)               { return SEQ_LEVELS; }

// The symbol at one position of the current level. Exposed for the SCRIPTED
// PLAYER in tests/test_minigames.cpp: what is under test is the engine's
// reproducibility, not a simulated human's memory, so the tape is allowed to
// read the sequence it is supposed to reproduce. Nothing on the device calls it.
uint8_t seq_symbol_at(const MgCtx& c, uint8_t i)
{
  const SeqState& s = mg_state<SeqState>(c);
  return (i < s.len) ? s.seq[i] : 0u;
}

// THE ANSWER DEADLINE this level is running against, in ctx.t_ms. Meaningful
// only once playback has ended (seq_is_showing() == false); before that it is
// whatever the previous level left, or 0 on the first. Exposed for the same
// reason seq_symbol_at() is: a SCRIPTED PLAYER in tests/test_minigames.cpp
// presses on the last legal step to stretch the run to its true maximum, and it
// must not re-derive SEQ_ANSWER_MS to know when that is. Nothing on the device
// calls it.
uint32_t seq_deadline_ms(const MgCtx& c) { return mg_state<SeqState>(c).deadline; }

// Which side is lit right now, or -1 while the gap between symbols is on.
int8_t seq_lit_side(const MgCtx& c)
{
  const SeqState& s = mg_state<SeqState>(c);
  if (s.play_idx >= s.len) return -1;
  const uint32_t el = c.t_ms - s.show_t0;
  if ((el % SEQ_SLOT_MS) >= SEQ_FLASH_MS) return -1;      // the gap
  return (int8_t)s.seq[s.play_idx];
}

static void seq_new_level(MgCtx& c)
{
  SeqState& s = mg_state<SeqState>(c);
  s.len = (uint8_t)(SEQ_FIRST_LEN + c.round);
  if (s.len > SEQ_MAX_LEN) s.len = SEQ_MAX_LEN;
  for (uint8_t i = 0; i < s.len; ++i) s.seq[i] = (uint8_t)(rng_next(c.rng) & 1u);
  s.play_idx = 0u;
  s.pos      = 0u;
  s.show_t0  = c.t_ms;
}

static void seq_init(MgCtx& c) { seq_new_level(c); }

// What the run is worth if it ends now: one whole level, one whole share.
static void seq_end_with_credit(MgCtx& c)
{
  c.score    = (uint16_t)((uint32_t)c.round * MG_SCORE_MAX / SEQ_LEVELS);
  c.finished = 1u;
}

static void seq_step(MgCtx& c)
{
  SeqState& s = mg_state<SeqState>(c);
  if (s.play_idx < s.len) {
    const uint32_t idx = (c.t_ms - s.show_t0) / SEQ_SLOT_MS;
    s.play_idx = (idx > s.len) ? s.len : (uint8_t)idx;
    // Playback has just ended: the answer clock starts here, not at the start
    // of the level, so watching never eats into thinking.
    if (s.play_idx >= s.len) s.deadline = c.t_ms + SEQ_ANSWER_MS;
    return;
  }
  if (c.t_ms >= s.deadline) seq_end_with_credit(c);        // ran out of thinking
}

static void seq_press(MgCtx& c, uint8_t side)
{
  SeqState& s = mg_state<SeqState>(c);
  if (s.play_idx < s.len) return;                         // still showing: ignored

  if (side != s.seq[s.pos]) {
    // A wrong symbol ends the game with what the player DID reach. Partial
    // credit, not zero: spec 27 forbids punishment, and finishing two levels
    // out of three is worth more than finishing none.
    seq_end_with_credit(c);
    return;
  }
  s.deadline = c.t_ms + SEQ_ANSWER_MS;      // a correct press buys the next one
  if (++s.pos >= s.len) {
    if (++c.round >= SEQ_LEVELS) {
      c.score    = MG_SCORE_MAX;                          // the whole ladder
      c.finished = 1u;
      return;
    }
    seq_new_level(c);
  }
}

// The score is written at the moment the run ends (both endings above), so
// done() is just "has that happened" and finish() has nothing left to compute.
static bool     seq_done(const MgCtx& c) { return c.finished != 0u; }
static uint16_t seq_finish(MgCtx& c)     { return c.score; }

extern const MgLogic MG_SEQUENCE = {
  MG_ID_SEQUENCE,
  STR_MG_SEQ,
  STR_MG_SEQ_HINT,
  seq_init,
  seq_press,
  seq_step,
  seq_done,
  seq_finish,
};
