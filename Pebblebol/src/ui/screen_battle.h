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
//  NO PER-FRAME HEAP, and it is a design property rather than a measurement:
//  the 212 B BattleState, the 8 B BattleAi, the BattleSetup, the event ring and
//  every string buffer are file-scope statics in screen_battle.cpp. Nothing in
//  this screen's update or render path allocates, because there is nothing left
//  for it to allocate. dev/godmode.cpp measures the ESP.getFreeHeap() delta per
//  drawn frame and the SYS/HEAP page shows it.
//
//  THE BOX IS READ-ONLY TO THIS SCREEN. A practice team is a COPY taken through
//  box_peek(); the fight is fought at full health on that copy and no damage,
//  no faint and no status ever travels back. So "a battle interrupted by
//  leaving the screen must not corrupt the Box" is structural: there is no
//  write to corrupt it with.
//
//  ONE REPORT PATH, copied from minigames/manager.cpp's mgr_abort(). Leaving by
//  ANY route - B, LONG_BOTH, a push, the result screen - runs report_once(),
//  which is idempotent. That is the fix for the exact defect the minigame
//  manager had: two exits, two chances to count one result twice.
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
#define BT_ENTRY_PRACTICE   0u
#define BT_ENTRY_DIAG       1u

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
  BTM_MODE_COUNT
};

// Arm the NEXT entry into SCR_BATTLE. ui.cpp calls this and then pushes the
// screen, because the seed comes from a named RNG stream and a pure screen may
// not draw from one. Calling it does not start anything: battle_enter() does.
void battle_arm(uint8_t entry, uint32_t seed);

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

// The HP the RESOLVE playback is drawing for `side` right now, which is the HP
// as it stood at the beat being shown and NOT the state's current value: a
// whole round is resolved before its first frame is drawn, so reading hp_cur
// would put the round's final numbers under its opening event.
uint16_t battle_screen_hp_shown(uint8_t side);

#endif  // PB_SCREEN_BATTLE_H
