// =============================================================================
//  ERRATA - minigames/manager.h
//  THE SEQUENCE (plan P3-C4): three games in a row, a card between them, and
//  ONE report per game.
//
//  PURE, deliberately. The obvious place for this is ui.cpp, next to the
//  drawing - and that is exactly why it is not there. The property that
//  matters most here is "finish() is called exactly once per game, whether it
//  completed or was abandoned", and the version this replaces got it right
//  only by accident: ui_game_leave() scored an abandoned game as a loss and
//  guarded re-entry with a phase check, so any second exit path would have
//  reported twice and double-counted both minigames_won and the XP ledger.
//  A pure state machine is one tests/test_minigames.cpp can actually prove
//  that about.
//
//  The report goes out through a bound function, so this file knows nothing
//  about sim.cpp, the XP ledger or flash.
// =============================================================================
#ifndef ER_MG_MANAGER_H
#define ER_MG_MANAGER_H

#include "minigame.h"

#define MGR_SEQ_LEN        3u      // games per sequence (plan P3-C4)
#define MGR_INTRO_MS    1600u      // "READY" then "GO"
#define MGR_GO_MS       1000u      // when READY becomes GO inside the intro
#define MGR_RESULT_MS   2200u
#define MGR_NEXT_MS     1200u      // the "next?" card (plan P3-C4)

enum MgrPhase : uint8_t {
  MGR_IDLE = 0,
  MGR_INTRO,     // the game is armed but not running
  MGR_RUN,
  MGR_RESULT,    // this game's score
  MGR_NEXT,      // "next?" - A continues, B stops
  MGR_TOTAL      // the sequence total, then done
};

// Called EXACTLY ONCE per game played, with that game's final per-mille score.
// The app binds sim_apply_play_result() + app_award_xp() + the save behind it.
typedef void (*MgrReportFn)(uint8_t game_id, uint16_t score);
void mgr_bind_report(MgrReportFn fn);

// Start a sequence. `first_id` is the game the player chose from PLAY; the
// rest of the sequence is drawn from the pool with the seed, so the same seed
// gives the same three games. n is clamped to 1..MGR_SEQ_LEN.
void mgr_begin(uint8_t first_id, uint32_t seed, uint8_t n);

// Drive it. dt_ms is real elapsed time for the CHROME only; the game itself
// is stepped in whole MG_STEP_MS units, so its score never depends on the
// frame rate. Returns false once the sequence is over.
bool mgr_tick(uint32_t dt_ms);

void mgr_press(uint8_t side);     // a button, meaningful in MGR_RUN and MGR_NEXT
void mgr_back(void);              // B: quit the run, or decline the next game

// Abandon everything, reporting the running game once if there is one. This is
// the screen-left path, and the one the double-report bug lived on.
void mgr_abort(void);

bool           mgr_active(void);
uint8_t        mgr_phase(void);
uint8_t        mgr_index(void);          // which game of the sequence, 0-based
uint8_t        mgr_count(void);          // how many are in this sequence
uint16_t       mgr_total_score(void);    // per-mille, averaged over those played
uint32_t       mgr_phase_ms(void);       // time spent in the current phase
const MgCtx&   mgr_ctx(void);
const MgLogic* mgr_logic(void);          // nullptr when idle

// The pool, for the sequence draw and for the PLAY screen.
const MgLogic* mg_logic_by_id(uint8_t id);
uint8_t        mg_pool_count(void);

#endif  // ER_MG_MANAGER_H
