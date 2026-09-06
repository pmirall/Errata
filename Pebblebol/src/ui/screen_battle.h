// =============================================================================
//  PEBBLEBOL - ui/screen_battle.h
//  THE BATTLE SCREEN (spec section 14, plan P4-C4).
//
//  The only place on the device where game/battle.cpp and game/battle_ai.cpp
//  are actually run. Six modes, one screen:
//
//      PICK    the Box, up to BATTLE_TEAM_MAX slots           (practice only)
//      INTRO   both creatures on the field
//      MENU    four attacks + CAMBIAR, on the shared list widget
//      SWITCH  which bench slot walks in
//      RESOLVE the round's transcript, played back one event at a time
//      RESULT  won / lost / drawn
//
//  B WALKS THAT LADDER (SF_OWNS_BACK), exactly as the BOX does: SWITCH goes
//  back to MENU, RESOLVE skips to the end of the round, and only the top mode
//  leaves the screen. SF_STICKY as well: invariant 3 dropping the player on
//  HOME twenty seconds into a fight would abandon it.
//
//  THE PROMISE THIS SCREEN MAKES, and the one that is tested hardest: IT NEVER
//  SUBMITS AN ACTION THE ENGINE WOULD REFUSE. The cursor cannot even come to
//  rest on an illegal row - the ring steps over them - and the one function
//  that reaches battle_submit_action() re-asks battle_validate_action() first.
//  The engine refuses safely and by name, but a menu that offers a move on
//  cooldown is a UI bug, and game/battle_ai.cpp already proves an action
//  producer and the validator can agree.
//
//  NO PER-FRAME HEAP, and it is now MEASURED as well as designed for. The
//  design half: the 212 B BattleState, the 8 B BattleAi, the BattleSetup, the
//  event ring and every string buffer are file-scope statics in
//  screen_battle.cpp, so there is nothing left in update() or render() to
//  allocate. The measurement half: tests/test_battle_screen.cpp intercepts
//  operator new / new[] / malloc / calloc / realloc and drives thousands of
//  real update+render frames across all six modes, asserting the count stays at
//  zero - and proves the counter can fire before it trusts the zero.
//  dev/godmode.cpp carries the same question to the device: it samples
//  ESP.getFreeHeap() per drawn frame and the SYS/HEAP page prints it, which is
//  the only half of this that has never run on hardware.
//
//  UPDATE RUNS AT LOOP RATE, NOT FRAME RATE. battle_update() is called from
//  ui_service() - app.cpp's step 3, every loop iteration - and not from the
//  render path, which is why the BT_FPS_HOLD_MS hold can never lapse: it is
//  renewed far more often than the frames it is holding the rate for. It is
//  also why the beat clock is compared against ui_now_ms() rather than counted
//  in frames.
//
//  THE BOX IS READ-ONLY TO THIS SCREEN - to THIS SCREEN, which is narrower than
//  "a battle never writes the Box" and is the true sentence. A practice team is
//  a COPY taken through box_peek(); the fight is fought at full health on that
//  copy and no damage, no faint and no status ever travels back, so "a battle
//  interrupted by leaving the screen must not corrupt the Box" is structural:
//  this file has no write to corrupt it with. A battle that is WON does reach
//  the Box, one hop away and outside this screen: ui_battle_result() calls
//  app_award_xp(), which writes the ACTIVE Pebble's level, xp and hp_cur and
//  flushes. That is the ordinary XP path every other source takes.
//
//  ONE REPORT PATH, copied from minigames/manager.cpp's mgr_abort(). Leaving by
//  ANY route - B, LONG_BOTH, a push, the result screen - runs report_once(),
//  which is idempotent, and reports the ENGINE'S outcome rather than a flat
//  loss: a battle is decided while its victory transcript is still playing, so
//  a leave inside that window used to pay nothing for a fight the player had
//  already won. That is the fix for the exact defect the minigame manager had -
//  two exits, two chances to count one result - plus the one it did not have.
//
//  THERE IS NO CONFIRM ON QUIT, and that is the one place this lifecycle
//  differs from the minigame's (handle_game() opens CFM_QUIT_GAME before
//  discarding a run). It is deliberate rather than forgotten: a confirm would
//  have to be an OVERLAY, because leaving the screen is what reports, so a
//  dialog pushed as a screen would report the battle it was asking about. The
//  overlay is reachable - dialog_open_confirm() does not navigate - but it puts
//  ui/dialog.o on a link line kept deliberately minimal, and with the report
//  above fixed a stray B can no longer throw away a fight that was already won:
//  it abandons an UNDECIDED one, which is what the minigame's dialog protects
//  against and what P5 can add here if play says it should.
//
//  PURE translation unit: gfx.h, the pure game modules, the content tables and
//  the ui.h seams. No render.h, no Arduino.h, no U8G2 - which is what lets
//  tests/test_screens.cpp render all six modes at the real 128x64.
// =============================================================================
#ifndef PB_SCREEN_BATTLE_H
#define PB_SCREEN_BATTLE_H

#include <stdint.h>

#include "../core/nt_types.h"

// Who asked for the battle. It decides the SEED, the TEAMS and the REWARD, and
// those three are the whole difference between the two entries.
//
//   PRACTICE - MENU / PLAY / "COMBATE DE PRACTICA". A fresh RNG_BATTLE seed,
//              the player's own Box, and XP_BATTLE_WIN on a win through
//              app_award_xp(). The only single-device battle entry in V1: there
//              are no wild battles (spec section 68 rule 18).
//   DIAG     - the developer console's test_battle (spec section 49). A FIXED
//              seed so a bug reproduces from a log, a SYNTHETIC team so it runs
//              on a device with one starter, and NO reward - a diagnostic that
//              pays XP is a cheat.
//   LINK     - P7-C3. TWO DEVICES, ONE BATTLE. The seed is not drawn here at
//              all: networking/battle_link.cpp derives it from the session id,
//              both nonces and both team CRCs, so the two engines start from
//              one number neither device chose alone. The teams are the two
//              Boxes, each side's own half validated by game/validate.cpp on
//              BOTH devices. THE REWARD IS THE SESSION'S TO AUTHORISE AND NOT
//              THIS SCREEN'S TO INFER: ui_link_battle_status() answers
//              UI_LKB_WON only where session_rewards_authorised() is true, so a
//              desync and a lost link pay nothing and print a neutral line.
//              There is no AI on this entry - the opposing actions come off the
//              wire - and no pick list: the team was frozen when the two
//              players consented, one screen earlier.
#define BT_ENTRY_PRACTICE   0u
#define BT_ENTRY_DIAG       1u
#define BT_ENTRY_LINK       2u

// The seed spec section 49's "deterministic RNG seed" pins. Stated here rather
// than hidden in a .cpp because reproducing a report means quoting it.
#define BT_DIAG_SEED        0x0B47713Eu

// The screen's own state, for the tests.
enum BattleScreenMode : uint8_t {
  BTM_PICK = 0,
  BTM_INTRO,
  BTM_MENU,
  BTM_SWITCH,
  BTM_RESOLVE,
  BTM_RESULT,
  // P7-C3 only: the lockstep has our move and is waiting for the peer's. It is
  // a MODE and not a spinner over BTM_MENU because the menu must not accept a
  // second action for a round the engine has already been given one for.
  BTM_WAIT,
  BTM_MODE_COUNT
};

// Arm the NEXT entry into SCR_BATTLE. ui.cpp calls this and then pushes the
// screen, because the seed comes from a named RNG stream and a pure screen may
// not draw from one. Calling it does not start anything: battle_enter() does.
void battle_arm(uint8_t entry, uint32_t seed);

// -----------------------------------------------------------------------------
//  THE LINKED ENTRY (P7-C3)
//
//  ui/screen_link.cpp calls battle_arm_link() and then pushes the screen. There
//  is no seed argument because there is no local seed, and there is a SIDE
//  argument because the session decides which half of the shared BattleSetup is
//  ours (networking/session.cpp fixes it from the two device ids, lower id
//  first). Every "side 0 is the player" in this file became that byte.
// -----------------------------------------------------------------------------
void battle_arm_link(uint8_t my_side);

// -----------------------------------------------------------------------------
//  THE THREE OBJECTS THE SESSION BORROWS, AND WHY THEY LIVE HERE
//
//  networking/session.h says it in as many words: a Session "holds NO
//  BattleState and NO BattleSetup of its own ... the linked path borrows the
//  one ui/screen_battle.cpp already owns at file scope". These three accessors
//  are that sentence made callable. They exist so there is never a SECOND 780 B
//  BattleSetup and never a second source of truth for a hashed team - which is
//  the whole reason the session takes them by pointer.
//
//  THE LOG IS THE SESSION'S TOO. networking/battle_link.cpp's try_resolve()
//  passes s.blog straight to battle_step_round(), so the transcript this screen
//  plays back IS the one the lockstep produced. Keeping it to ONE ROUND's worth
//  is this file's own job and is not exported: submit() re-opens the ring
//  immediately before handing the action to the lockstep, which is the last
//  moment before a resolution can happen (a round needs BOTH pendings, and ours
//  is the one being submitted).
//
//  A caller that is not the LINK screen has no business with any of these.
// -----------------------------------------------------------------------------
struct BattleSetup;
struct BattleState;
struct BattleLog;
BattleSetup* battle_link_setup(void);
BattleState* battle_link_state(void);
BattleLog*   battle_link_log(void);

// The ONE copy-out-of-the-Box this firmware has, shared with ui/screen_link.cpp
// so the team that goes on the wire is built by the same rule the practice team
// is: full health on a copy, a v1 save's empty moveset falling back to the
// species learnset, a stale FAINTED bit cleared, and the Box itself untouched.
// It repairs THIS DEVICE'S OWN Pebble before it is offered; it is not, and must
// never become, a repair of anything a peer sent (networking/battle_link.cpp).
struct PebbleInstance;
bool battle_copy_from_box(PebbleInstance& out, uint8_t slot);

// The screen-table hooks.
void battle_enter(void);
void battle_update(uint32_t now_ms);
void battle_render(void);
void battle_input(Gesture g);
void battle_leave(void);

// -----------------------------------------------------------------------------
//  WHAT THE TESTS READ. Every one of these is a plain query over the state the
//  hooks above already keep; none of them exists only for a test.
// -----------------------------------------------------------------------------
uint8_t  battle_screen_mode(void);      // BattleScreenMode
// WHICH HALF OF THE SETUP THIS SCREEN IS DRAWING AND SUBMITTING FOR. 0 for
// every single-device entry; for a linked one it is the SESSION's answer, and
// it is a plain query over the byte the hooks already keep. It exists because
// the two answers are indistinguishable in a rendered frame until you know
// which team is whose, and a mutation that hard-codes it back to 0 failed
// nothing without it.
uint8_t  battle_screen_side(void);
uint8_t  battle_screen_cursor(void);    // the row inside the current mode
uint8_t  battle_screen_event(void);     // the BattleLogEvent being played back
uint8_t  battle_screen_reject(void);    // the last BattleReject this screen saw
uint8_t  battle_screen_outcome(void);   // BattleOutcome
uint16_t battle_screen_round(void);     // the round the transcript is about
uint16_t battle_screen_dropped(void);   // log-ring overflow; MUST stay 0
uint8_t  battle_screen_picked(void);    // how many Box slots are chosen
uint8_t  battle_screen_submits(void);   // actions this screen has handed the engine
uint8_t  battle_screen_reports(void);   // how many times the result was reported

// THE ENGINE'S OWN VERDICT ON THE ROW THE CURSOR IS POINTING AT RIGHT NOW, as a
// BattleReject. This is the promise at the top of this header expressed as a
// number a test can sweep: BR_OK on every row the ring can come to rest on, in
// every mode, in every state, for every seed. It is not a test-only hook - it
// is battle_validate_action() asked about the cursor, which is exactly what the
// input handler asks before it submits.
uint8_t  battle_screen_cursor_reject(void);

// How many of the current mode's rows the engine would refuse. A menu where
// this is non-zero is a menu the ring is stepping over rows in, which is the
// situation the promise is about - a test that never reaches one has proved
// nothing.
uint8_t  battle_screen_blocked_rows(void);

// The player's LEAD combatant's stage on `stat` and the rounds it has left.
// P5-C4's armed ITEM_KLASS_BATTLE_MOD is the only thing in the firmware that
// can set one BEFORE the first round, and these two are how a host case sees
// that it landed. Both answer 0 for a stat out of range or a battle that never
// started, so a caller cannot read a stale byte.
int8_t   battle_screen_lead_stage(uint8_t stat);
uint8_t  battle_screen_lead_stage_left(uint8_t stat);

// The HP the RESOLVE playback is drawing for `side` right now, which is the HP
// as it stood at the beat being shown and NOT the state's current value: a
// whole round is resolved before its first frame is drawn, so reading hp_cur
// would put the round's final numbers under its opening event.
uint16_t battle_screen_hp_shown(uint8_t side);

// P10-C3: 1 while the transcript is on the RLE_PROTECT beat that named `side`.
// The RENDERER's own answer, out of the same fill_art() br_draw_field() is
// handed - not a re-derivation. 0 outside BTM_RESOLVE.
uint8_t  battle_screen_guard(uint8_t side);

#endif  // PB_SCREEN_BATTLE_H
