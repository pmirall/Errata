// =============================================================================
//  PEBBLEBOL - ui/screen_link.h
//  LINK (spec section 6 SCR_LINK, spec section 42's flow, spec section 47's
//  timeouts). Plan P7-C2, and the screen that drives P7-C3's linked battle.
//
//  WHAT THIS SCREEN IS. It is the one place in the firmware that owns a peer
//  link: the discovery job (networking/discovery.h) that finds another
//  Pebblebol in the room, the consent that turns "found" into "agreed", and the
//  Session (networking/session.h) that carries whatever the two players agreed
//  to do. It replaces a placeholder that said "Enlace - Fase 7" - a screen that
//  printed an internal plan phase number at a player - and that string is gone
//  from core/strings_es.h with STR_SO_LINK_SOON beside it.
//
// -----------------------------------------------------------------------------
//  CONSENT IS NOT IMPLIED BY PROXIMITY, AND IT IS ENFORCED TWICE
// -----------------------------------------------------------------------------
//  Spec section 42 asks that SESSION_REQUEST/SESSION_ACCEPT need A on BOTH
//  devices. Here that is not a dialog in front of an automatic handshake: it is
//  the ONLY thing that starts one. Until the local player has chosen a peer,
//  chosen an operation and pressed A:
//
//    * no Session exists on this device (session_init() has not been called),
//    * nothing drains the transport, so a peer's HELLO is not even looked at,
//    * and the radio has no unicast peer bound, so on the device that HELLO was
//      never delivered to this firmware at all - networking/transport_espnow.cpp
//      counts it as rx_wrong_peer inside the receive callback and drops it.
//
//  The second of those is the one a host binary can see and the third is the
//  one the radio enforces; they are independent, and BOTH have to be removed
//  for a device to be dragged into a session it never agreed to.
//
//  THE HANDSHAKE IS SYMMETRIC, WHICH IS WHY "A ON BOTH" IS STRUCTURAL RATHER
//  THAN POLITE. networking/session.h: "Both endpoints may call session_start()
//  - the ROLES are decided by device id, not by who spoke first". So each
//  device sends HELLO when ITS player presses A, and a device whose player
//  never pressed it answers nothing. The one that did press it climbs its
//  nine-rung ladder and closes SE_LOST, which is what the player sees as
//  "CONEXIÓN PERDIDA / A: Reintentar / B: Salir" (spec section 47). There is no
//  path in which one device's button opens a session on the other's.
//
//  WHAT A BEACON MAY CARRY IS SETTLED ELSEWHERE AND IS NOT WIDENED HERE. The
//  operation the player picked is NOT announced (spec 43/44: a device id, a
//  name, a version, capabilities and nothing else). Two devices that pick
//  different operations discover it in the session, not on the air.
//
// -----------------------------------------------------------------------------
//  SIX MODES, ONE SCREEN, AND EVERY WAIT HAS A CEILING
// -----------------------------------------------------------------------------
//      LKM_BROWSE   the peer list. The radio is up, a beacon goes out every
//                   LINK_BEACON_MS, and LINK_JOB_TIMEOUT_MS ends the browse.
//      LKM_CARD     "¡PEBBLEBOL ENCONTRADO!" and section 42's four rows:
//                   COMBATE / INTERCAMBIO / CRIAR / CANCELAR.
//      LKM_WAIT     this player has consented. Peer name, operation and the
//                   session's own state, on both devices, until the peer
//                   consents too or the ladder gives up.
//      LKM_HANDOFF  SCR_BATTLE is on top and this screen is pumping the
//                   session underneath it. The radio stays ours.
//      LKM_LOST     spec section 47's recoverable failure: A retries the same
//                   peer, B leaves.
//      LKM_ENDED    the operation finished. One neutral line and no reward
//                   decision - the reward decision belongs to the session, and
//                   ui/screen_battle.cpp asks for it by name.
//
//  THE RADIO IS RELEASED ON EVERY EXIT PATH, and by the same mechanism the
//  NETWORK screen uses: link_cancel() is the one call that stops the driver,
//  it is idempotent, and link_leave() calls it. The one exception is the
//  hand-off to SCR_BATTLE, which is a push and therefore runs leave() on the
//  way - see s_handoff in the .cpp, and the ladder that bounds it.
//
//  THREE CEILINGS, AND WHICH ONE ACTUALLY BITES. This row is NOT SF_STICKY, so
//  invariant 3's twenty silent seconds return the player to HOME and run
//  link_leave() - and that is STRICTER than LINK_JOB_TIMEOUT_MS (90 s), so in
//  the product a browse nobody is touching is ended by the auto-return and the
//  discovery job's own §47 ceiling is a BACKSTOP rather than the usual path.
//  It is kept, tested and worth keeping: it is what bounds a browse the player
//  IS touching (every tap restarts the auto-return and none of them restarts
//  the job's clock), and it is the only ceiling left if this row is ever made
//  SF_STICKY. The third is the SESSION's nine-rung ladder, which is what bounds
//  everything from the moment two players consent - including a linked battle,
//  which runs on SCR_BATTLE and IS SF_STICKY.
//
//  PURE translation unit: gfx.h, the pure networking and game modules, and the
//  ui.h seams. No Arduino.h, no radio header, no render.h - so
//  tests/test_link_screen.cpp drives the whole of it, against a fake
//  LinkRadioDriver and a loopback Transport, with a REAL peer Session on the
//  other end of the loopback.
// =============================================================================
#ifndef PB_SCREEN_LINK_H
#define PB_SCREEN_LINK_H

#include <stdint.h>

#include "../core/nt_types.h"

// Spec section 42's menu, in the order it lists them. LOP_CANCEL is a row and
// not an absence: a card with no way off it is a screen with a dead end.
enum LinkOp : uint8_t {
  LOP_BATTLE = 0,
  LOP_TRADE,
  LOP_BREED,
  LOP_CANCEL,
  LOP_COUNT
};

enum LinkScreenMode : uint8_t {
  LKM_BROWSE = 0,
  LKM_CARD,
  LKM_WAIT,
  LKM_HANDOFF,
  LKM_LOST,
  LKM_ENDED,
  LKM_MODE_COUNT
};

// The screen-table hooks.
void link_enter(void);
void link_update(uint32_t now_ms);
void link_render(void);
void link_input(Gesture g);
void link_leave(void);

// -----------------------------------------------------------------------------
//  THE BOX'S ENTRY POINT (spec section 9 "initiate breeding; initiate trade")
//
//  ui/screen_box.cpp calls this and then pushes SCR_LINK. It PRE-SELECTS the
//  operation and remembers which Box slot the player was looking at; it does
//  NOT consent to anything and it does not touch the radio. The arming is
//  consumed by the next link_enter() and cleared there, exactly as
//  battle_arm() is consumed by battle_enter().
// -----------------------------------------------------------------------------
void link_arm_intent(uint8_t op, uint8_t box_slot);

// -----------------------------------------------------------------------------
//  WHAT THE POWER LADDER AND THE TESTS READ
// -----------------------------------------------------------------------------
// The ladder's `held` input, ORed into ui_radio_job_busy(). It is the discovery
// JOB's own flag and nothing else, and link_hold() is what makes that
// sufficient: a session only ever exists over a job the hold left LS_RUNNING,
// so there is no state in which a session is live and the job is not. See the
// note at its definition for the mutation that established it.
bool     link_screen_busy(void);

uint8_t  link_screen_mode(void);        // LinkScreenMode
uint8_t  link_screen_cursor(void);      // the row inside the current mode
uint8_t  link_screen_op(void);          // LinkOp, once one is chosen
uint8_t  link_screen_peers(void);       // peers the player may pick from
uint8_t  link_screen_session_state(void);   // SessionState
uint8_t  link_screen_end_reason(void);      // SessionEndReason of the last one
uint8_t  link_screen_consents(void);    // how many times A opened a session
const char* link_screen_peer_name(void);    // never NULL

// -----------------------------------------------------------------------------
//  THE TRADE'S SEAM (P7-C4)
//
//  A trade uses the SAME Session, the SAME handshake, the SAME ladder and the
//  SAME consent gate as a battle; what differs is the operation agreed in
//  SS_SESSION and the driver that runs after it (networking/trade_link.cpp).
//  There is NO new screen mode: the review happens inside LKM_WAIT, which
//  already shows the peer name, the operation and the state, and which gains
//  the two Pebbles and an "A: aceptar" affordance the moment both sides have
//  validated both records. A mode whose only difference is two lines of text is
//  a mode nobody can reason about.
//
//  THE PLAYER HAS ABOUT NINE SECONDS TO ANSWER THE REVIEW, AND THAT IS THE
//  SESSION'S LADDER RATHER THAN THIS SCREEN'S. Once both READYs are in, this
//  endpoint owes a CONFIRM; rule R4 says only PROGRESS resets the ladder, and a
//  peer that has already confirmed sends nothing new, so a review nobody answers
//  closes SE_LOST after PROTO_RETX_MAX * PROTO_RETX_MS. It is the SAME
//  constraint P7-C3 measured for a linked battle round (9,950 ms through the
//  real screen) and it has the SAME remedy and the same cost: the two constants
//  in networking/session.h, which lengthen every other wait in the protocol
//  with them. Nothing is lost when it expires - the journal rolls back and both
//  players keep what they had - which is why it is recorded here rather than
//  worked around.
//
//  ui/ui.cpp reads the two below to answer ui_trade_hooks()'s judge: the RULE
//  is game/trade.cpp's and the two facts it needs are this screen's.
// -----------------------------------------------------------------------------
uint8_t  link_trade_slot(void);        // the Box slot being offered, or BOX_SLOT_NONE
uint32_t link_trade_peer_id(void);     // the peer's device id, or 0
uint8_t  link_trade_phase(void);       // TradeLinkPhase, TLP_IDLE when there is none
bool     link_trade_wants_consent(void);   // both records validated; the player decides

// -----------------------------------------------------------------------------
//  THE LINKED BATTLE'S SEAM (P7-C3)
//
//  ui/screen_battle.cpp runs the ENGINE; the SESSION that keeps two engines
//  identical lives here, because this is the screen the two players agreed on
//  it from and the screen that owns the radio. The battle screen reaches these
//  through ui.h (ui_link_*), NOT by including this header, so
//  tests/test_battle_screen.cpp still links the battle screen and the things it
//  drives and nothing else.
// -----------------------------------------------------------------------------
// UI_LKB_* in ui.h. The reward gate is link_battle_status(): it answers
// UI_LKB_WON only where the SESSION authorised rewards, never where the local
// engine merely won.
uint8_t  link_battle_status(void);
uint8_t  link_battle_side(void);
void     link_battle_pump(uint32_t now_ms);
bool     link_battle_wants_action(void);
void     link_battle_submit(uint8_t kind, uint8_t index);
// Milliseconds left before the session's own retransmission ladder gives up on
// a round nobody has advanced. 0 when there is no live linked battle. This is
// the player's real move clock and it is the SESSION's, not this screen's -
// see the note on it in the .cpp.
uint32_t link_battle_move_ms_left(uint32_t now_ms);
// The battle screen is gone. Closes the session, releases the radio and leaves
// this screen showing what happened. Idempotent; see ui.h for why the BATTLE
// screen and not this one is what calls it.
void     link_battle_done(void);

#endif  // PB_SCREEN_LINK_H
