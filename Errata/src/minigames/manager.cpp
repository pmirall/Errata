// =============================================================================
//  ERRATA - minigames/manager.cpp
//  The sequence state machine. PURE: see the header for why.
// =============================================================================
#include "manager.h"

#include "games/games.h"

// The pool. MgId order, so PLAY's row index IS the game id.
static const MgLogic* const POOL[] = {
  &MG_PING, &MG_SEQUENCE, &MG_PACKET_FLOOD, &MG_FIREWALL, &MG_BUFFER, &MG_DELETE
};
static const uint8_t POOL_N = (uint8_t)(sizeof(POOL) / sizeof(POOL[0]));
static_assert(sizeof(POOL) / sizeof(POOL[0]) == (size_t)MG_ID_COUNT,
              "every MgId must have exactly one MgLogic in the pool");

const MgLogic* mg_logic_by_id(uint8_t id)
{
  return (id < POOL_N) ? POOL[id] : nullptr;
}
uint8_t mg_pool_count(void) { return POOL_N; }

// ---- state ------------------------------------------------------------------
static MgrReportFn s_report = nullptr;
static MgCtx       s_ctx;
static const MgLogic* s_logic = nullptr;

static uint8_t  s_phase     = MGR_IDLE;
static uint8_t  s_index     = 0;      // 0-based position in the sequence
static uint8_t  s_count     = 0;      // how many games this sequence holds
static uint8_t  s_order[MGR_SEQ_LEN];
static uint8_t  s_reported  = 0;      // THE guarantee: 1 once this game reported
static uint16_t s_sum       = 0;      // per-mille, summed over reported games
static uint8_t  s_played    = 0;
static uint32_t s_phase_ms  = 0;
static uint32_t s_step_acc  = 0;      // real ms not yet spent as whole MG_STEPs
static Rng      s_pick;               // the sequence draw, separate from the game

void mgr_bind_report(MgrReportFn fn) { s_report = fn; }

bool           mgr_active(void)      { return s_phase != MGR_IDLE; }
uint8_t        mgr_phase(void)       { return s_phase; }
uint8_t        mgr_index(void)       { return s_index; }
uint8_t        mgr_count(void)       { return s_count; }
uint32_t       mgr_phase_ms(void)    { return s_phase_ms; }
const MgCtx&   mgr_ctx(void)         { return s_ctx; }
const MgLogic* mgr_logic(void)       { return s_logic; }

uint16_t mgr_total_score(void)
{
  return s_played ? (uint16_t)(s_sum / s_played) : 0u;
}

// -----------------------------------------------------------------------------
//  THE ONE REPORT. Every path out of a game goes through here.
//
//  TWO independent things keep it to once, and each is sufficient on its own -
//  measured, not assumed, by mutating them one at a time:
//
//    * the PHASE MACHINE: reporting always moves the phase out of INTRO/RUN,
//      and only INTRO/RUN can report;
//    * s_reported, which latches per game.
//
//  Delete EITHER and the suite still passes; delete BOTH and
//  the_report_count_never_exceeds_the_games_started fails. So this is belt and
//  braces on purpose, not one guard plus a redundant one. It is worth the two
//  bytes because the bug it prevents is the one this module was extracted for:
//  the version in ui.cpp double-counted minigames_won AND the XP ledger the
//  moment a second exit path appeared, and a future phase added here would do
//  it again.
// -----------------------------------------------------------------------------
static void report_current(void)
{
  if (s_reported || s_logic == nullptr) return;
  s_reported = 1u;
  const uint16_t score = mg_score(s_ctx, *s_logic);
  s_sum = (uint16_t)(s_sum + score);
  ++s_played;
  if (s_report) s_report(s_logic->id, score);
}

static void enter(uint8_t phase)
{
  s_phase    = phase;
  s_phase_ms = 0;
}

static void arm_game(uint8_t idx)
{
  s_logic    = mg_logic_by_id(s_order[idx]);
  s_reported = 0u;
  s_step_acc = 0u;
  // Each game of the sequence gets its OWN seed, derived from the sequence
  // stream, so a sequence is reproducible as a whole from one number.
  mg_begin(s_ctx, *s_logic, rng_next(s_pick));
  enter(MGR_INTRO);
}

void mgr_begin(uint8_t first_id, uint32_t seed, uint8_t n)
{
  if (n == 0u) n = 1u;
  if (n > MGR_SEQ_LEN) n = MGR_SEQ_LEN;
  if (mg_logic_by_id(first_id) == nullptr) first_id = MG_ID_PING;

  rng_init(s_pick, seed);
  s_count = n;
  s_index = 0;
  s_sum   = 0;
  s_played = 0;

  // The player chose the first one; the rest are drawn.
  s_order[0] = first_id;
  for (uint8_t i = 1; i < n; ++i) s_order[i] = (uint8_t)rng_next_below(s_pick, POOL_N);

  arm_game(0);
}

void mgr_press(uint8_t side)
{
  switch (s_phase) {
    case MGR_RUN:
      if (s_logic) mg_press(s_ctx, *s_logic, side);
      break;
    case MGR_NEXT:
      // A continues. B is mgr_back().
      if (side == MG_SIDE_L) {
        ++s_index;
        arm_game(s_index);
      }
      break;
    default:
      break;
  }
}

void mgr_back(void)
{
  switch (s_phase) {
    case MGR_INTRO:
    case MGR_RUN:
      // Quitting mid-game is a result of zero, reported like any other.
      report_current();
      enter(MGR_TOTAL);
      break;
    case MGR_NEXT:
      enter(MGR_TOTAL);       // decline the rest of the sequence
      break;
    case MGR_RESULT:
      enter(MGR_TOTAL);
      break;
    default:
      s_phase = MGR_IDLE;
      break;
  }
}

void mgr_abort(void)
{
  if (s_phase == MGR_INTRO || s_phase == MGR_RUN) report_current();
  s_logic = nullptr;
  s_phase = MGR_IDLE;
}

bool mgr_tick(uint32_t dt_ms)
{
  if (s_phase == MGR_IDLE) return false;
  s_phase_ms += dt_ms;

  switch (s_phase) {
    case MGR_INTRO:
      if (s_phase_ms >= MGR_INTRO_MS) enter(MGR_RUN);
      break;

    case MGR_RUN: {
      // WHOLE STEPS ONLY. The chrome runs on real time; the game runs on the
      // step count, which is what makes a score reproducible from a tape.
      s_step_acc += dt_ms;
      while (s_step_acc >= MG_STEP_MS && s_phase == MGR_RUN) {
        s_step_acc -= MG_STEP_MS;
        if (s_logic && !mg_tick(s_ctx, *s_logic)) {
          report_current();
          enter(MGR_RESULT);
        }
      }
      break;
    }

    case MGR_RESULT:
      if (s_phase_ms >= MGR_RESULT_MS) {
        enter((uint8_t)((s_index + 1u < s_count) ? MGR_NEXT : MGR_TOTAL));
      }
      break;

    case MGR_NEXT:
      // Unanswered, the card times out and the sequence stops. Nothing is
      // lost: every game played has already reported.
      if (s_phase_ms >= MGR_NEXT_MS) enter(MGR_TOTAL);
      break;

    case MGR_TOTAL:
      if (s_phase_ms >= MGR_RESULT_MS) { s_logic = nullptr; s_phase = MGR_IDLE; }
      break;

    default:
      break;
  }
  return s_phase != MGR_IDLE;
}
