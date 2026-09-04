// =============================================================================
//  PEBBLEBOL - networking/battle_link.h
//  THE LOCKSTEP HALF OF THE SESSION (spec section 15, plan P4-C5).
//
//  networking/session.cpp owns SS_IDLE..SS_TEAM and SS_CLOSED; this module owns
//  SS_VERIFY, SS_BATTLE and SS_ENDING. The split is where the subject changes:
//  everything above is about two devices agreeing to play, and everything here
//  is about two ENGINES staying identical.
//
//  IT HOLDS NO STATE OF ITS OWN. Every byte it reads and writes lives in the
//  caller's Session, BattleState and BattleSetup, so two endpoints run in one
//  process (tools/check.sh gates the absence of file-scope mutables) and so
//  there is never a second source of truth for a hashed object.
//
//  See networking/session.h for the ladder, the sequence window and the four
//  rules that run before the state table; see docs/protocol.md for the state
//  table itself, the three invariants and the LIMITS.
// =============================================================================
#ifndef PB_NETWORKING_BATTLE_LINK_H
#define PB_NETWORKING_BATTLE_LINK_H

#include <stdint.h>

#include "session.h"

// Both teams are in the setup and both verdicts are VR_OK: seed the battle,
// run battle_init() and raise the round-1 agreement barrier. On any refusal it
// closes the session itself.
void link_begin(Session& s);

// One decoded frame, in SS_VERIFY, SS_BATTLE or SS_ENDING.
void link_on_msg(Session& s, const ProtoMsg& m);

// The ladder's regeneration for those three states. Everything it sends is
// rebuilt from Session and BattleState; nothing is stored as bytes.
void link_resend(Session& s);

// The lockstep wants one local action for the round it is on. FALSE while the
// agreement barrier is up: nothing advances unmatched.
bool link_wants_action(const Session& s);
void link_local_action(Session& s, BattleAction a);

#endif  // PB_NETWORKING_BATTLE_LINK_H
