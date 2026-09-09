// =============================================================================
//  ERRATA - minigames/games/delete_logic.cpp
//  DELETE (spec 29.5): "Select and remove appearing corrupted blocks."
//
//  PURE, like every *_logic.cpp here. The step count is the clock.
//
//  A TRIAGE GAME, NOT A REACTION GAME, which is the whole of what separates it
//  from PING. Five slots. Blocks appear on a fixed schedule, sit there rotting
//  on a visible fuse and vanish on their own; some are healthy and some are
//  corrupted. One button WALKS a caret along the slots, the other spends one of
//  seven irreversible PURGE charges on whatever the caret is over.
//
//  THERE IS ALWAYS MORE THAN ONE TARGET, and that is a proof rather than a
//  hope. Blocks j and k co-exist iff DEL_SPAWN_MS*(k-j) < DEL_LIFE_MS, i.e.
//  k-j <= 2. For no two of the DEL_BAD corrupted indices to contest each other
//  every pairwise gap would have to be >= 3, which needs a span of
//  3*(DEL_BAD-1) = 15 across indices 0..11 - a span of 11. Impossible, so EVERY
//  run, for EVERY seed, has at least one instant with two corrupted blocks on
//  the board and a caret that can only be in one place. The static_assert below
//  is that sentence as a build error - and it DERIVES the 3 from the fuse and
//  the spawn period (DEL_MIN_APART) rather than spelling it, because a hardcoded
//  3 keeps passing when the fuse is shortened, which is precisely the edit that
//  kills the property it claims to be guarding.
//
//  AND THE SCORE DOES NOT KNOW WHAT TIME IT IS. It is a count of correct
//  targets, completely indifferent to how fast you got there as long as you got
//  there inside the fuse: a run purged on sight and a run purged on the last
//  step before each fuse expires score IDENTICALLY, which is a test PING could
//  not pass on its first round.
//
//  NOTHING IS EVER SUBTRACTED (spec 27). A wasted charge costs an OPPORTUNITY,
//  never points already banked and never a stat, and the one message this game
//  leaves is latched to fire once per run.
// =============================================================================
#include "games.h"

#define DEL_SLOTS       5u
#define DEL_BLOCKS     12u      // scheduled blocks in a run
#define DEL_BAD         6u      // how many of them are corrupted
#define DEL_CHARGES     7u      // purges available: one spare over DEL_BAD
#define DEL_FIRST_MS  400u
#define DEL_SPAWN_MS  800u
#define DEL_LIFE_MS  2400u
#define DEL_RUN_MS  11600u      // 400 + 11*800 + 2400

// The three slot states live in games.h as DEL_KIND_*, because the draw half
// reads them through del_kind_at() and neither side may name a bare number.
#define DEL_EMPTY    DEL_KIND_EMPTY
#define DEL_HEALTHY  DEL_KIND_HEALTHY
#define DEL_CORRUPT  DEL_KIND_CORRUPT

struct DelState {
  uint16_t die_ms[DEL_SLOTS];   // ctx.t_ms at which that slot's block vanishes
  uint16_t next_ms;             // the next scheduled spawn; never recomputed
  uint16_t bad_mask;            // bit i = scheduled block i is corrupted
  uint8_t  kind[DEL_SLOTS];     // DEL_EMPTY / _HEALTHY / _CORRUPT
  uint8_t  cursor;              // the caret slot, 0..DEL_SLOTS-1
  uint8_t  spawned;             // blocks that have appeared; index into bad_mask
  uint8_t  charges;             // purges left
  uint8_t  cleared;             // corrupted blocks purged. THE score
  uint8_t  warned;              // STR_GM_WASTED has already fired this run
};
static_assert(sizeof(DelState) <= MG_STATE_BYTES, "DelState must fit MgCtx::state");

static_assert(DEL_FIRST_MS + (DEL_BLOCKS - 1u) * DEL_SPAWN_MS + DEL_LIFE_MS
              == DEL_RUN_MS, "DEL_RUN_MS must be the schedule's own end");
static_assert(DEL_RUN_MS < MG_MAX_MS,
              "the delete schedule must end inside the section 29 ceiling");
// At most (LIFE + SPAWN - 1)/SPAWN = 3 blocks are alive when a spawn happens,
// so a free slot is guaranteed and there is no deferral path to lengthen a run.
static_assert((DEL_LIFE_MS + DEL_SPAWN_MS - 1u) / DEL_SPAWN_MS <= DEL_SLOTS - 1u,
              "a delete spawn must always find a free slot");
// The ladder is 0 / 166 / 333 / 500 / 666 / 833 / 1000. Its fourth rung landing
// EXACTLY on 500 is what makes "more than half the corruption" and "a win" the
// same statement: sim_apply_play_result() increments the persisted
// minigames_won at >= 500 and the result card flips there too.
static_assert(DEL_BAD % 2u == 0u &&
              (DEL_BAD / 2u) * MG_SCORE_MAX / DEL_BAD == MG_SCORE_MAX / 2u,
              "clearing half the corruption must land exactly on the win line");
static_assert(DEL_CHARGES > DEL_BAD,
              "one fumbled press must not put the ceiling out of reach");
// THE CHOICE PROOF, as a build error. See the header comment. DEL_MIN_APART is
// the smallest index gap at which two blocks do NOT overlap: block j is alive
// over [j*SPAWN, j*SPAWN + LIFE), so j and k miss each other iff
// (k-j)*SPAWN >= LIFE. At 2400/800 it is 3; shorten the fuse to 700 and it
// falls to 1, and the assertion fails instead of quietly guarding nothing.
#define DEL_MIN_APART  ((DEL_LIFE_MS - 1u) / DEL_SPAWN_MS + 1u)
static_assert(DEL_MIN_APART * (DEL_BAD - 1u) > DEL_BLOCKS - 1u,
              "two corrupted blocks must be forced to contest the caret");
static_assert(DEL_FIRST_MS % MG_STEP_MS == 0u && DEL_SPAWN_MS % MG_STEP_MS == 0u &&
              DEL_LIFE_MS % MG_STEP_MS == 0u,
              "every delete event must land on a step boundary");
static_assert(DEL_BLOCKS <= 16u, "DelState::bad_mask is a 16-bit mask");

// --- accessors --------------------------------------------------------------
uint8_t  del_slots(void)                { return (uint8_t)DEL_SLOTS; }
uint8_t  del_charges_max(void)          { return (uint8_t)DEL_CHARGES; }
uint8_t  del_bad_total(void)            { return (uint8_t)DEL_BAD; }
uint8_t  del_cursor(const MgCtx& c)     { return mg_state<DelState>(c).cursor; }
uint8_t  del_charges(const MgCtx& c)    { return mg_state<DelState>(c).charges; }
uint8_t  del_cleared(const MgCtx& c)    { return mg_state<DelState>(c).cleared; }

uint8_t del_kind_at(const MgCtx& c, uint8_t slot)
{
  const DelState& s = mg_state<DelState>(c);
  return (slot < DEL_SLOTS) ? s.kind[slot] : (uint8_t)DEL_EMPTY;
}

// How much fuse that slot has left, 0..100. The one thing that makes triage
// possible when two corrupted blocks overlap: kill the short bar first.
uint8_t del_life_pct(const MgCtx& c, uint8_t slot)
{
  const DelState& s = mg_state<DelState>(c);
  if (slot >= DEL_SLOTS || s.kind[slot] == DEL_EMPTY) return 0u;
  if (s.die_ms[slot] <= c.t_ms) return 0u;
  const uint32_t left = (uint32_t)(s.die_ms[slot] - c.t_ms);
  return (uint8_t)((left >= DEL_LIFE_MS) ? 100u : (left * 100u / DEL_LIFE_MS));
}

// Milliseconds of fuse left in that slot; 0 when it is empty. For the tapes.
uint16_t del_life_left_ms(const MgCtx& c, uint8_t slot)
{
  const DelState& s = mg_state<DelState>(c);
  if (slot >= DEL_SLOTS || s.kind[slot] == DEL_EMPTY) return 0u;
  return (s.die_ms[slot] > c.t_ms) ? (uint16_t)(s.die_ms[slot] - c.t_ms) : 0u;
}

// -----------------------------------------------------------------------------
//  THE GAME
// -----------------------------------------------------------------------------
static void del_init(MgCtx& c)
{
  DelState& s = mg_state<DelState>(c);
  s.charges = DEL_CHARGES;
  s.next_ms = (uint16_t)DEL_FIRST_MS;

  // A uniform DEL_BAD-1 subset of the first DEL_BLOCKS-1 indices by selection
  // sampling, plus the LAST scheduled block forced corrupt - so the ceiling can
  // never be reached before t = 9200 ms and a well-played run cannot end early.
  uint8_t need = (uint8_t)(DEL_BAD - 1u);
  for (uint8_t i = 0; i < DEL_BLOCKS - 1u && need; ++i) {
    const uint32_t left = (uint32_t)(DEL_BLOCKS - 1u - i);
    if (rng_next_below(c.rng, left) < need) {
      s.bad_mask = (uint16_t)(s.bad_mask | (uint16_t)(1u << i));
      --need;
    }
  }
  s.bad_mask = (uint16_t)(s.bad_mask | (uint16_t)(1u << (DEL_BLOCKS - 1u)));
}

static void del_step(MgCtx& c)
{
  DelState& s = mg_state<DelState>(c);

  // 1. EXPIRE, before the spawn, which is what guarantees a free slot. A
  //    corrupted block that expires is simply gone: no points, no penalty and
  //    no note.
  for (uint8_t i = 0; i < DEL_SLOTS; ++i) {
    if (s.kind[i] != DEL_EMPTY && c.t_ms >= s.die_ms[i]) s.kind[i] = DEL_EMPTY;
  }

  // 2. SPAWN, off the absolute schedule - next_ms += DEL_SPAWN_MS, never
  //    t_ms + something - so the cadence cannot drift and nothing the player
  //    does can push it later.
  if (s.spawned < DEL_BLOCKS && c.t_ms >= s.next_ms) {
    uint8_t n_free = 0;
    for (uint8_t i = 0; i < DEL_SLOTS; ++i) if (s.kind[i] == DEL_EMPTY) ++n_free;
    uint8_t k = (uint8_t)rng_next_below(c.rng, n_free);
    for (uint8_t i = 0; i < DEL_SLOTS; ++i) {
      if (s.kind[i] != DEL_EMPTY) continue;
      if (k--) continue;
      s.kind[i]   = ((s.bad_mask >> s.spawned) & 1u) ? DEL_CORRUPT : DEL_HEALTHY;
      s.die_ms[i] = (uint16_t)(c.t_ms + DEL_LIFE_MS);
      break;
    }
    ++s.spawned;
    s.next_ms = (uint16_t)(s.next_ms + DEL_SPAWN_MS);
  }

  if (c.t_ms >= DEL_RUN_MS) c.finished = 1u;
}

static void del_press(MgCtx& c, uint8_t side)
{
  DelState& s = mg_state<DelState>(c);

  if (side == MG_SIDE_L) {
    // WALK. Free, unlimited, wrapping - and the device's own list grammar:
    // every menu in the firmware steps a ring with A and acts with B.
    s.cursor = (uint8_t)((s.cursor + 1u) % DEL_SLOTS);
    return;
  }

  // PURGE. It always costs a charge, whatever is under the caret: the purge is
  // INDISCRIMINATE, and that is the game. (charges is never 0 here - the run
  // ends on the press that spends the last one, and mg_press() refuses after
  // that.)
  --s.charges;
  if (s.kind[s.cursor] == DEL_CORRUPT) {
    s.kind[s.cursor] = DEL_EMPTY;
    ++s.cleared;
    c.score = (uint16_t)((uint32_t)s.cleared * MG_SCORE_MAX / DEL_BAD);
  } else {
    s.kind[s.cursor] = DEL_EMPTY;
    // LATCHED. A blind sweeper spends every charge inside about three seconds,
    // so an unlatched note buries the board under seven toasts in three
    // seconds - and lands them on exactly the player it is trying to teach.
    // One toast is the lesson; the charge pips going hollow are the readout.
    if (!s.warned) { s.warned = 1u; c.note = STR_GM_WASTED; }
  }
  if (s.charges == 0u || s.cleared >= DEL_BAD) c.finished = 1u;
}

static bool     del_done(const MgCtx& c) { return c.finished != 0u; }
static uint16_t del_finish(MgCtx& c)     { return c.score; }

extern const MgLogic MG_DELETE = {
  MG_ID_DELETE,
  STR_MG_DELETE,
  STR_MG_DELETE_HINT,
  del_init,
  del_press,
  del_step,
  del_done,
  del_finish,
};

// -----------------------------------------------------------------------------
//  THE CHEAPEST CHEAT is the blind sweep: alternate L and R without looking,
//  walking the ring and purging every slot the caret lands on. It is the exact
//  strategy that would break this game if a purge were free.
//
//  What stops it is arithmetic, not a rule. Every R costs a charge and there
//  are seven for the whole run. A shot scores only if that slot holds a
//  corrupted block at that instant, and corrupted slot-occupancy over the run is
//  DEL_BAD * DEL_LIFE_MS / (DEL_RUN_MS * DEL_SLOTS) = 24.8%, so seven blind
//  shots are worth about 290 of 1000 - and less in practice, because the sweeper
//  spends all seven inside the first three seconds while only three or four
//  blocks have spawned, and the run then ends on charges == 0 and denies them
//  the rest of the board.
//
//  Camping on one slot and purging only when it turns corrupted is the other
//  half: every shot lands, but only the corrupted blocks that happen to spawn in
//  that one slot are ever reachable - E[6/5] = 1.2 kills, about 200. The
//  selection dimension is load-bearing, not decorative.
//
//  The residual, stated honestly: an expert who reads the board hits 1000 on
//  most runs. That is intended - this is a judgement test, not a dexterity test,
//  and the ceiling should be reachable by someone paying attention. If the
//  competent case ever flattens to 1000 for every seed, the honest lever is
//  another target, not a shorter fuse.
// -----------------------------------------------------------------------------
