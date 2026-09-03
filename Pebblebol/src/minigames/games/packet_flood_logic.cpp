// =============================================================================
//  PEBBLEBOL - minigames/games/packet_flood_logic.cpp
//  PACKET FLOOD (spec 29.2): "Press A/B to redirect packets into the correct
//  channel."
//
//  PURE. stdint, rng.h, minigame.h, strings_es.h for the StrId it leaves in
//  ctx.note. The step count is the clock; nothing here reads millis().
//
//  IT IS A SORT, NOT A REACTION TEST, and that is the whole reason it is not
//  PING with different art. Nothing on a packet says "left" or "right": the
//  packet carries an ADDRESS - one digit - and that digit is printed on one of
//  the two MOUTHS. To act you must look away from the packet, find its digit,
//  and come back. PING lights the half you must press; here the answer is
//  never where the stimulus is.
//
//  THE REROUTE IS LOAD-BEARING. When packet PF_SWAP_AT spawns the two mouths
//  EXCHANGE SIDES: the addresses do not move, the doors do. Everything the
//  player burned in during the first half is deliberately made wrong, and the
//  only recovery is to go back to reading. Cut it and this game IS ping with a
//  digit on it - it is not flavour and it is not a difficulty knob.
//
//  MISROUTING COSTS MORE THAN ROUTING PAYS (3 against 2), which is not a
//  punishment (spec 27 forbids those) but the thing that makes the only real
//  decision real: a fair guess is worth 2*(1/2) - 3*(1/2) = -0.5 a packet, so
//  LETTING AN UNREADABLE PACKET FALL IS STRICTLY BETTER THAN GUESSING. The
//  floor is zero; a run can be worth nothing, never less than nothing.
// =============================================================================
#include "games.h"

#define PF_PACKETS       18u     // see the anti-mash note at the bottom
#define PF_GAP0_MS      750u     // gap before packet 1
#define PF_GAP_DEC_MS    25u     // each gap is one step shorter than the last
#define PF_GAP_MIN_MS   500u     // ... down to this floor, from i = 10 on
#define PF_LIFE_MS     1800u     // a packet's life at the router, untouched
#define PF_SWAP_AT        9u     // the packet whose spawn exchanges the mouths
#define PF_SWAP_STEPS    24u     // 600 ms of both-mouths-inverted highlight
#define PF_FLASH_STEPS    6u     // 150 ms of outcome highlight
#define PF_W_GOOD         2u     // a routed packet pays this
#define PF_W_BAD          3u     // a misrouted one costs this
#define PF_SCORE_DEN     (PF_W_GOOD * PF_PACKETS)   // 36: perfect play, exactly

// The lane the draw half reserves. DERIVED, not chosen: spawns are at least
// PF_GAP_MIN_MS apart and a lifetime spans a half-open window, so the peak
// live count is floor(1800/500) + 1 = 4 - head plus two drawn backlog slots
// plus at most a "+1". Every packet the player can act on next is therefore on
// screen WITH ITS DIGIT rather than hidden behind a counter, which is why the
// gap floor is 500 and not lower.
#define PF_MAX_ALIVE      4u

// Five visually unconfusable digits. 3/8, 6/5 and 5/9 are not separable at
// GF_HEAD on a 1-bit panel, so this is a legibility decision rather than
// decoration: an address the player cannot read is not a sort, it is a guess.
static const uint8_t PF_DIGITS[] = { 1u, 2u, 4u, 7u, 9u };
#define PF_DIGIT_N  ((uint8_t)(sizeof(PF_DIGITS) / sizeof(PF_DIGITS[0])))

struct PfState {
  uint32_t marks;       // bit i = the PORT (0/1) packet i is addressed to
  uint8_t  digit[2];    // the two port digits; digit[0] belongs to port 0
  uint8_t  spawned;     // packets that have entered the lane, 0..PF_PACKETS
  uint8_t  good;        // routed correctly
  uint8_t  bad;         // routed into the wrong mouth
  uint8_t  lost;        // expired at the router untouched
  uint8_t  swapped;     // 1 once the mouths have exchanged sides
  uint8_t  swap_steps;  // steps of the reroute highlight left
  uint8_t  last;        // PF_LAST_* - what happened to the packet that resolved
  uint8_t  last_side;   // which mouth took it (meaningless when _LOST)
  uint8_t  last_steps;  // steps of that outcome highlight left
};
static_assert(sizeof(PfState) <= MG_STATE_BYTES, "PfState must fit MgCtx::state");

// THE HEAD INDEX IS MgCtx.round, not a field here: it is game-private (nothing
// in manager.cpp or ui/ reads it), which is what lets pf_done() be the same
// one-liner as ping_done() and keeps one counter where two could disagree.

// THE SCHEDULE IS A PURE FUNCTION OF THE INDEX. No timestamps live in state,
// and no play can move the end of the run: presses only retire packets earlier
// than their expiry.
static constexpr uint32_t pf_gap_ms(uint8_t i)
{
  return (PF_GAP0_MS - PF_GAP_MIN_MS > (uint32_t)i * PF_GAP_DEC_MS)
         ? PF_GAP0_MS - (uint32_t)i * PF_GAP_DEC_MS
         : PF_GAP_MIN_MS;
}
static constexpr uint32_t pf_spawn_ms(uint8_t n)
{
  return n ? pf_spawn_ms((uint8_t)(n - 1u)) + pf_gap_ms((uint8_t)(n - 1u)) : 0u;
}

// 6375 + 3500 + 1800 = 11,675 ms = 467 steps, with nobody pressing anything.
// That is the figure, not a bound - the fastest possible run is 9,875 ms.
static_assert(pf_spawn_ms(PF_PACKETS - 1u) + PF_LIFE_MS < MG_MAX_MS,
              "the packet_flood schedule must end inside the section 29 ceiling");
static_assert(PF_LIFE_MS / PF_GAP_MIN_MS + 1u <= PF_MAX_ALIVE,
              "the backlog must fit the lane the draw half reserves");
static_assert(pf_spawn_ms(PF_PACKETS - 1u) % MG_STEP_MS == 0u &&
              PF_LIFE_MS % MG_STEP_MS == 0u,
              "every packet_flood event must land on a step boundary");
static_assert(PF_SWAP_AT > 0u && PF_SWAP_AT < PF_PACKETS,
              "the reroute must happen inside the run");

// --- accessors: the draw half and the tapes re-derive nothing ----------------
uint8_t  pf_packets(void)                 { return (uint8_t)PF_PACKETS; }
uint16_t pf_life_ms(void)                 { return (uint16_t)PF_LIFE_MS; }
uint8_t  pf_head(const MgCtx& c)          { return c.round; }
uint8_t  pf_last(const MgCtx& c)          { return mg_state<PfState>(c).last; }
uint8_t  pf_last_side(const MgCtx& c)     { return mg_state<PfState>(c).last_side; }
bool     pf_swap_flash(const MgCtx& c)    { return mg_state<PfState>(c).swap_steps != 0u; }
uint8_t  pf_good(const MgCtx& c)          { return mg_state<PfState>(c).good; }
uint8_t  pf_bad(const MgCtx& c)           { return mg_state<PfState>(c).bad; }
uint8_t  pf_lost(const MgCtx& c)          { return mg_state<PfState>(c).lost; }

uint8_t pf_queue_len(const MgCtx& c)
{
  const PfState& s = mg_state<PfState>(c);
  return (s.spawned > c.round) ? (uint8_t)(s.spawned - c.round) : 0u;
}

// The address of the packet `slot` places back from the router (0 = the head).
uint8_t pf_digit_at(const MgCtx& c, uint8_t slot)
{
  const PfState& s = mg_state<PfState>(c);
  const uint8_t idx = (uint8_t)(c.round + slot);
  if (idx >= s.spawned) return 0u;
  return s.digit[(s.marks >> idx) & 1u];
}

// The digit currently printed on the mouth at `side`. Honours the reroute:
// want = port ^ swapped, so the mouth at `side` is showing port side ^ swapped.
uint8_t pf_mouth_digit(const MgCtx& c, uint8_t side)
{
  const PfState& s = mg_state<PfState>(c);
  return s.digit[(side ^ s.swapped) & 1u];
}

uint16_t pf_life_left_ms(const MgCtx& c)
{
  if (pf_queue_len(c) == 0u) return 0u;
  const uint32_t die = pf_spawn_ms(c.round) + PF_LIFE_MS;
  return (die > c.t_ms) ? (uint16_t)(die - c.t_ms) : 0u;
}

// The correct mouth for the CURRENT head, or PF_NO_HEAD when the lane is
// empty. It reads the SAME two facts pf_press() reads, in the same order, so
// the hook and the rule cannot drift: an index-derived form disagrees with the
// rule for any packet still backlogged when the mouths swap.
uint8_t pf_want_side(const MgCtx& c)
{
  const PfState& s = mg_state<PfState>(c);
  if (s.spawned <= c.round) return PF_NO_HEAD;
  return (uint8_t)(((s.marks >> c.round) & 1u) ^ s.swapped);
}

// -----------------------------------------------------------------------------
//  THE GAME
// -----------------------------------------------------------------------------
static void pf_init(MgCtx& c)
{
  PfState& s = mg_state<PfState>(c);
  // Two port digits, drawn WITHOUT replacement so the two mouths can never
  // show the same address.
  const uint8_t i = (uint8_t)rng_next_below(c.rng, PF_DIGIT_N);
  uint8_t       j = (uint8_t)rng_next_below(c.rng, (uint32_t)(PF_DIGIT_N - 1u));
  if (j >= i) ++j;
  s.digit[0] = PF_DIGITS[i];
  s.digit[1] = PF_DIGITS[j];
  // ONE draw for the whole run: bit i is packet i's port. That is what makes a
  // run trivially reproducible from a seed.
  s.marks = rng_next(c.rng);
  s.last  = PF_LAST_NONE;
}

// Shared by a press and by an expiry: one packet leaves the router.
static void pf_resolve(MgCtx& c)
{
  PfState& s = mg_state<PfState>(c);
  ++c.round;
  const int16_t raw = (int16_t)((int16_t)(PF_W_GOOD * s.good) -
                                (int16_t)(PF_W_BAD  * s.bad));
  // Rewritten every time rather than accumulated, so the manager's live score
  // tag is always exactly this expression and a misroute can take back the
  // credit it cost - without ever going below zero (spec 27).
  c.score = (raw > 0) ? (uint16_t)((uint32_t)raw * MG_SCORE_MAX / PF_SCORE_DEN)
                      : 0u;
  if (c.round >= PF_PACKETS) c.finished = 1u;
}

static void pf_step(MgCtx& c)
{
  PfState& s = mg_state<PfState>(c);
  if (s.last_steps && --s.last_steps == 0u) s.last = PF_LAST_NONE;
  if (s.swap_steps) --s.swap_steps;

  // SPAWN. Monotone in the index, so it normally runs zero or one iteration.
  while (s.spawned < PF_PACKETS && c.t_ms >= pf_spawn_ms(s.spawned)) {
    if (s.spawned == PF_SWAP_AT) {
      s.swapped    = 1u;
      s.swap_steps = PF_SWAP_STEPS;
      c.note       = STR_GM_REROUTE;   // ui.cpp toasts it; pure logic cannot
    }
    ++s.spawned;
  }

  // EXPIRE. Lifetimes are constant, so expiries are ordered exactly as spawns
  // are and the head only ever advances.
  while (c.round < s.spawned && c.t_ms >= pf_spawn_ms(c.round) + PF_LIFE_MS) {
    ++s.lost;
    s.last       = PF_LAST_LOST;
    s.last_steps = PF_FLASH_STEPS;
    pf_resolve(c);
    if (c.finished) return;
  }
}

static void pf_press(MgCtx& c, uint8_t side)
{
  PfState& s = mg_state<PfState>(c);
  // An empty lane swallows the press: it cannot be banked against a packet
  // that has not arrived, so a fast masher and a slow one score the same.
  if (s.spawned <= c.round) return;

  const uint8_t port = (uint8_t)((s.marks >> c.round) & 1u);
  const uint8_t want = (uint8_t)(port ^ s.swapped);
  if (side == want) { ++s.good; s.last = PF_LAST_GOOD; }
  else              { ++s.bad;  s.last = PF_LAST_BAD;  }
  s.last_side  = side;
  s.last_steps = PF_FLASH_STEPS;
  pf_resolve(c);
}

static bool     pf_done(const MgCtx& c) { return c.round >= PF_PACKETS; }
static uint16_t pf_finish(MgCtx& c)     { return c.score; }

extern const MgLogic MG_PACKET_FLOOD = {
  MG_ID_PACKET_FLOOD,
  STR_MG_FLOOD,
  STR_MG_FLOOD_HINT,
  pf_init,
  pf_press,
  pf_step,
  pf_done,
  pf_finish,
};

// -----------------------------------------------------------------------------
//  WHY PF_PACKETS IS 18, which is the anti-mash argument and not a taste.
//
//  The cheapest cheat is to hold ONE button: every packet is dispatched to that
//  mouth on the step it spawns and the marks are fair coin flips. The weights
//  make that worth E[raw] = 2*9 - 3*9 = -9, i.e. a score of ZERO, the same as
//  putting the device down. Over the whole distribution E[score] is about 32 of
//  1000 (measured, over 4096 seeds). Reaching ANYTHING at all needs 11 of 18
//  lucky bits (24.0% of the mark words), reaching the 500 that increments
//  minigames_won needs 15 (0.377% predicted, 0.3% measured - one run in about
//  300), and 1000 needs all 18 (one in 262,144).
//
//  Those numbers are the reason the run is long rather than short: at 12
//  packets a mash clears 500 about 3.8% of the time, which a player would find.
//  If this game ever needs shortening, widen the gaps - do not drop packets.
//
//  The honest residual, stated rather than hidden: a seed whose 18 mark bits
//  are all equal makes one held button a perfect run. That is 2 words in 2^18,
//  and it is deliberately NOT special-cased - forcing balance into the mark
//  word would make the second half of a run partly predictable from the first,
//  which is a worse exploit than the one it fixes, and it would cost the single
//  rng_next() that makes a run reproducible from a seed.
// -----------------------------------------------------------------------------
