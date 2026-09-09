// =============================================================================
//  ERRATA - networking/battle_link.cpp
//  THE LOCKSTEP. See battle_link.h for the split, session.h for the ladder and
//  docs/protocol.md for the state table, the invariants and the LIMITS.
//
// -----------------------------------------------------------------------------
//  THE ONE STRUCTURAL FACT EVERYTHING RESTS ON
// -----------------------------------------------------------------------------
//  game/battle.cpp's battle_step_round() returns BS_NEED_ACTIONS WITH THE STATE
//  BIT-IDENTICAL when either pending_kind is BACT_NONE, and nothing above that
//  line writes. So ATTEMPTING to resolve is a free, idempotent no-op, and this
//  module never schedules resolution: it simply tries after every accepted
//  ACTION, every local submit and every barrier that comes down.
//
// -----------------------------------------------------------------------------
//  THREE HASHES, THREE DIFFERENT QUESTIONS - AND NONE OF THEM CATCHES A LIAR
// -----------------------------------------------------------------------------
//   ACTION.open_hash        we entered this round already disagreeing (or this
//                           is a stale or replayed ACTION)
//   ROUND_RESULT.hash_before  we agreed on the state and I received a different
//                           ACTION than you sent - the engine takes this hash
//                           AFTER both pendings are written, so it already
//                           commits to the action PAIR
//   ROUND_RESULT.hash_after we agreed on the inputs and disagree on the RULES
//
//  Twelve bytes per round per side. It buys NOTHING against a peer that lies:
//  a liar forges all three just as easily, and a forged ROUND_RESULT is exactly
//  indistinguishable from a genuine divergence - no third party, no signature,
//  no shared secret. What the protocol does guarantee is that neither outcome
//  pays anybody: SE_DESYNC awards no XP, writes no Box and commits nothing, on
//  BOTH sides. A lying peer's guaranteed power is DENIAL. It cannot move our
//  state, because it never supplies state - only two bytes of intent and a
//  digest we compare against our own.
//
//  AND THE HASH IS BLIND TO THE STEP-6 / STEP-7 ORDER. game/battle.h records
//  the measurement: swapping battle_s6_process_fainting() with
//  battle_s7_process_status() leaves every round hash and the final hash
//  byte-identical, because tick_status() never reads BCF_FAINTED. Two peers
//  shipping opposite orders would agree on every hash they exchange and produce
//  different transcripts, and CAPABILITIES cannot see it because both report
//  the same BATTLE_ENGINE_VER. Only tests/golden/battle_v1.txt and
//  tools/check.sh's third battle gate hold that boundary, and both are LOCAL.
//  Nothing here may claim the hash covers "the whole round".
//
// -----------------------------------------------------------------------------
//  NEVER SUBSTITUTE A MISSING ACTION. Not on a timeout, not ever, and it is
//  impossible in principle rather than merely undesirable: there is no pass
//  action (game/battle.h's battle_every_learnset_has_an_always_ready_move()
//  exists precisely because there is none), and even a fully deterministic rule
//  such as "timeout implies attack slot 0" fails, because the timed-out peer
//  may have slot 0 on cooldown - so the two engines would compute different
//  legality and desynchronise ON THE SUBSTITUTE. Nothing needs unwinding:
//  battle_step_round() never ran, and a round is the engine's atomic unit.
//
//  A WIRE TEAM ENTERS THE ENGINE THROUGH THE VALIDATOR AND IS NEVER MENDED.
//  ui/screen_battle.cpp:205's copy_from_box() rewrites an unknown move to the
//  species learnset, clamps hp and clears PBS_FAINTED, and that is defensible
//  for a Bug THIS device created. It is not defensible for one a peer sent,
//  and this module does the opposite: game/validate.cpp refuses, by name, and
//  the frame goes back as TEAM_VALIDATION.
// =============================================================================
#include "battle_link.h"

#include <string.h>

// -----------------------------------------------------------------------------
//  SMALL HELPERS
// -----------------------------------------------------------------------------
static uint8_t peer_side_of(const Session& s) { return (uint8_t)(s.side ^ 1u); }

static bool battle_live(const Session& s)
{
  return s.st != nullptr && s.st->phase == (uint8_t)BP_RUNNING &&
         s.st->outcome == (uint8_t)BO_UNDECIDED;
}

// THE RE-ACK LEASH. Answering a retransmission is what keeps an honest peer
// alive on a lossy link; it is also a small amplification vector, so it is
// capped PER ROUND and the cap is named as the heuristic it is.
static bool reack_allowed(Session& s, uint8_t round)
{
  if (s.reack_round != round) { s.reack_round = round; s.reack_count = 0u; }
  if (s.reack_count >= (uint8_t)SESSION_MAX_REACK_PER_ROUND) return false;
  s.reack_count++;
  return true;
}

static void count_stale(Session& s, const ProtoMsg& m)
{
  if (s.end.rx_stale < 0xFFFFu) s.end.rx_stale++;
  session_note(s, LEK_STALE, m.type, 0u, m.round, m.seq);
}

static void count_wrong(Session& s, const ProtoMsg& m)
{
  // A peer still driving an earlier PHASE gets the same treatment as one still
  // driving an earlier ROUND: it is answered from state, never dropped.
  if (session_reanswer_earlier_phase(s, m)) return;
  if (s.end.rx_wrong_state < 0xFFFFu) s.end.rx_wrong_state++;
  session_note(s, LEK_WRONG_STATE, m.type, 0u, m.round, m.seq);
}

// -----------------------------------------------------------------------------
//  THE FRAMES THIS MODULE REGENERATES. Every one is rebuilt from Session and
//  BattleState; not one is stored as bytes.
// -----------------------------------------------------------------------------
// `round` MUST BE THE ROUND s.open_hash BELONGS TO, which in practice is always
// st->round (and 1 in SS_VERIFY, where they are the same). The open hash is a
// per-round value and only one of them is ever held, so naming any other round
// here would put a hash on the wire under a label it does not answer to - the
// same mistake the ACTION path made before my_act_hash existed. The bound below
// is a defence on a public shape, not a clamp of any wire value.
static void send_battle_state(Session& s, uint8_t round, bool retx)
{
  if (round < 1u || round > (uint8_t)BATTLE_MAX_ROUNDS) return;
  ProtoMsg m; session_prepare(s, m, PT_BATTLE_STATE, round);
  if (retx) m.flags = (uint8_t)PF_RETX;
  m.p.bstate.open_hash = s.open_hash;
  m.p.bstate.phase     = (s.st != nullptr) ? s.st->phase : 0u;
  m.p.bstate.outcome   = (s.st != nullptr) ? s.st->outcome : 0u;
  session_emit(s, m);
}

static void send_action(Session& s, bool retx)
{
  if (s.my_act_round == 0u) return;
  ProtoMsg m; session_prepare(s, m, PT_ACTION, s.my_act_round);
  if (retx) m.flags = (uint8_t)PF_RETX;
  m.p.action.kind      = s.my_act_kind;
  m.p.action.index     = s.my_act_index;
  // THE HASH OF THAT ACTION'S OWN ROUND, never of the round we happen to be on
  // now. A retransmission for round N-1 stamped with round N's open hash is a
  // frame the peer must refuse - measured before this field existed: the 10 %
  // drop arm reported 263 SD_OPEN_HASH desyncs in 500 trials on a link that
  // never corrupted a byte.
  m.p.action.open_hash = s.my_act_hash;
  session_emit(s, m);
}

static void send_action_result(Session& s, bool retx)
{
  if (s.ar_round == 0u) return;
  ProtoMsg m; session_prepare(s, m, PT_ACTION_RESULT, s.ar_round);
  if (retx) m.flags = (uint8_t)PF_RETX;
  m.p.ares.reject     = s.ar_reject;
  m.p.ares.kind_echo  = s.ar_kind;
  m.p.ares.index_echo = s.ar_index;
  m.p.ares.open_hash  = s.ar_hash;
  session_emit(s, m);
}

static void send_round_result(Session& s, bool retx)
{
  if (s.rr_round == 0u) return;
  ProtoMsg m; session_prepare(s, m, PT_ROUND_RESULT, s.rr_round);
  if (retx) m.flags = (uint8_t)PF_RETX;
  m.p.rres.hash_before = s.rr_before;
  m.p.rres.hash_after  = s.rr_after;
  m.p.rres.outcome     = s.rr_outcome;
  session_emit(s, m);
}

static void send_battle_end(Session& s, uint8_t reason, uint8_t detail, bool retx)
{
  ProtoMsg m; session_prepare(s, m, PT_BATTLE_END, 0u);
  if (retx) m.flags = (uint8_t)PF_RETX;
  m.p.bend.reason     = reason;
  m.p.bend.outcome    = s.final_outcome;
  m.p.bend.detail     = detail;
  m.p.bend.final_hash = s.final_hash;
  m.p.bend.rounds     = (s.st != nullptr) ? s.st->round : 0u;
  session_emit(s, m);
}

// A desync is always announced before it is recorded, so the peer names the
// same round we do instead of waiting out its own ladder.
static void desync(Session& s, uint8_t detail, uint32_t peer_hash)
{
  s.end.peer_hash = peer_hash;
  if (s.st != nullptr) s.final_hash = battle_state_hash(*s.st);
  send_battle_end(s, (uint8_t)SE_DESYNC, detail, false);
  session_close(s, SE_DESYNC, detail);
}

// EVERYTHING WE HOLD FOR ROUND `round`, for a peer that is behind. This is the
// pull that heals the case the design's first two drafts both got wrong: a peer
// retransmitting a round we already resolved must be ANSWERED from state, never
// dropped as stale, or an honest pair strands itself after a single lost frame.
static void reanswer_round(Session& s, uint8_t round)
{
  if (!reack_allowed(s, round)) return;
  session_note(s, LEK_REACK, 0u, 0u, round, 0u);
  if (s.ar_round == round) send_action_result(s, true);
  if (s.my_act_round == round) send_action(s, true);
  if (s.rr_round == round) send_round_result(s, true);
}

// -----------------------------------------------------------------------------
//  RESOLUTION
// -----------------------------------------------------------------------------
static void enter_ending(Session& s);

static void try_resolve(Session& s)
{
  if (s.barrier != 0u) return;                      // nothing advances unmatched
  if (s.st == nullptr) return;
  if (s.st->side[0].pending_kind == (uint8_t)BACT_NONE ||
      s.st->side[1].pending_kind == (uint8_t)BACT_NONE) return;

  const uint8_t  n      = (uint8_t)s.st->round;
  const uint32_t before = battle_state_hash(*s.st);
  const BattleStepResult r = battle_step_round(*s.st, s.blog);
  const uint32_t after  = battle_state_hash(*s.st);

  s.rr_round   = n;
  s.rr_before  = before;
  s.rr_after   = after;
  s.rr_outcome = s.st->outcome;
  send_round_result(s, false);
  s.barrier = 1u;
  s.final_hash    = after;
  s.final_outcome = s.st->outcome;

  if (s.st->outcome == (uint8_t)BO_ABORT) {
    // The engine itself found the state moved under a submitted action. It is a
    // DESYNC and not a loss, and it pays nobody.
    send_battle_end(s, (uint8_t)SE_DESYNC, (uint8_t)SD_BO_ABORT, false);
    session_close(s, SE_DESYNC, (uint8_t)SD_BO_ABORT);
    return;
  }
  if (r == BS_BATTLE_OVER) { enter_ending(s); return; }

  // st->round is n+1 and battle_s8_end_of_round() has cleared both pendings, so
  // `after` IS the next round's open hash by construction - the one 212 B
  // object both peers demonstrably agree on.
  s.open_hash = after;
  s.waiting_for = (uint8_t)PT_ROUND_RESULT;
  session_progress(s);

  // THE PEER MAY HAVE RESOLVED THIS ROUND BEFORE WE DID AND TOLD US ALREADY.
  // Reordering delivers its ROUND_RESULT(N) ahead of the ACTION(N) we still
  // needed, and the frame is not repeated - so a barrier that only ever looks
  // FORWARD never comes down. Measured before this block existed: a link that
  // dropped nothing and only reordered ended 472 of 500 trials in SE_LOST.
  if (s.peer_rr_round == n) {
    if (s.peer_rr_before != s.rr_before) { desync(s, (uint8_t)SD_INPUTS, s.peer_rr_before); return; }
    if (s.peer_rr_after  != s.rr_after)  { desync(s, (uint8_t)SD_RULES,  s.peer_rr_after);  return; }
    s.barrier = 0u;
    s.waiting_for = (uint8_t)PT_ACTION;
  }
}

static void send_goodbye_done(Session& s)
{
  ProtoMsg g; session_prepare(s, g, PT_GOODBYE, 0u);
  g.p.bye.reason = (uint8_t)SE_DONE;
  session_emit(s, g);
}

// AGREEING IS NOT LEAVING. Rewards commit the moment BOTH the outcome and the
// final hash matched - and only then - but the endpoint STAYS in SS_ENDING
// until the peer's GOODBYE, so that a peer whose own BATTLE_END was lost can
// still pull ours from state.
//
// MEASURED, WHICH IS WHY IT IS WRITTEN THIS WAY: closing here immediately paid
// one side and not the other in 88 of 500 trials at 10 % drop and in 18 of 500
// on a link that only REORDERED - because the peer's GOODBYE could arrive
// before its BATTLE_END, or its BATTLE_END could be lost once with nobody left
// to repeat it. Waiting makes the asymmetry need NINE consecutive losses in one
// direction instead of one.
static void compare_end(Session& s)
{
  if (s.paid != 0u) return;
  if (s.peer_end_outcome != s.final_outcome || s.peer_end_hash != s.final_hash) {
    s.end.peer_hash = s.peer_end_hash;
    session_close(s, SE_DESYNC, (uint8_t)SD_END_DISAGREE);
    return;
  }
  s.paid = 1u;
  s.waiting_for = (uint8_t)PT_GOODBYE;
  send_goodbye_done(s);
  session_progress(s);
}

static void enter_ending(Session& s)
{
  send_battle_end(s, (uint8_t)SE_DONE, (uint8_t)SD_NONE, false);
  s.waiting_for = (uint8_t)PT_BATTLE_END;
  session_goto(s, SS_ENDING);
  session_progress(s);
  // The peer may have finished first and told us while we were still resolving.
  if (s.peer_end_seen != 0u) compare_end(s);
}

// -----------------------------------------------------------------------------
//  LIFECYCLE
// -----------------------------------------------------------------------------
void link_begin(Session& s)
{
  if (s.setup == nullptr || s.st == nullptr) {
    session_close(s, SE_PROTOCOL, (uint8_t)SD_INTERNAL);
    return;
  }
  const bool init_side = (s.role == (uint8_t)SR_INITIATOR);
  s.setup->seed = session_derive_seed(
      s.id,
      init_side ? s.nonce_local : s.nonce_peer,
      init_side ? s.nonce_peer  : s.nonce_local,
      init_side ? s.team_crc_local : s.team_crc_peer,
      init_side ? s.team_crc_peer  : s.team_crc_local);

  const BattleReject br = battle_init(*s.st, *s.setup);
  if (br != BR_OK) {
    // game/validate.h's containment claim has teeth, and the claim is about the
    // WHOLE CHAIN both teams have already been through: validate_team() (every
    // content rule and the within-team ids), the cross-team id check in
    // session.cpp's on_team_submit(), validate_level_band() and
    // validate_battle_ready(). After those there is no BattleReject a peer can
    // still cause, so this is a BUG IN THIS TREE and the peer is not blamed.
    //
    // THIS COMMENT USED TO CLAIM THAT OF validate_team() ALONE, AND IT WAS
    // FALSE FOR ONE CODE: BR_MEMBER_FAINTED, which game/validate.h declared an
    // exception in the same commit. A peer sending hp_cur == 0 landed here and
    // was recorded as SD_INTERNAL. That is why validate_battle_ready() exists.
    s.final_outcome = 0u;
    s.final_hash    = 0u;
    send_battle_end(s, (uint8_t)SE_PROTOCOL, (uint8_t)SD_INTERNAL, false);
    session_close(s, SE_PROTOCOL, (uint8_t)SD_INTERNAL);
    return;
  }
  s.open_hash = battle_state_hash(*s.st);
  s.barrier   = 0u;
  s.waiting_for = (uint8_t)PT_BATTLE_STATE;
  send_battle_state(s, 1u, false);
  session_goto(s, SS_VERIFY);
  session_progress(s);
}

bool link_wants_action(const Session& s)
{
  if (s.state != (uint8_t)SS_BATTLE) return false;
  if (s.barrier != 0u) return false;
  if (!battle_live(s)) return false;
  return s.st->side[s.side].pending_kind == (uint8_t)BACT_NONE;
}

void link_local_action(Session& s, BattleAction a)
{
  if (!link_wants_action(s)) return;
  const BattleReject r = battle_submit_action(*s.st, s.side, a);
  if (r != BR_OK) {
    // OUR OWN chooser produced an action this engine refuses. That is a local
    // fault; nothing is sent to the peer that blames it.
    session_close(s, SE_PROTOCOL, (uint8_t)SD_INTERNAL);
    return;
  }
  s.my_act_round = (uint8_t)s.st->round;
  s.my_act_kind  = a.kind;
  s.my_act_index = a.index;
  s.my_act_hash  = s.open_hash;
  send_action(s, false);
  s.waiting_for = (uint8_t)PT_ACTION_RESULT;
  session_progress(s);
  try_resolve(s);
}

// -----------------------------------------------------------------------------
//  INBOUND
// -----------------------------------------------------------------------------
static void on_action(Session& s, const ProtoMsg& m)
{
  const uint8_t cur = (uint8_t)s.st->round;
  if (m.round == cur) {
    if (m.p.action.open_hash != s.open_hash) {
      // The peer thinks this round starts from a different 212 B state. There
      // is no ACTION_RESULT for this: BattleReject has no code meaning "we
      // disagree about the state", and inventing one would put a transport
      // concept inside the engine. The state itself goes back instead.
      send_battle_state(s, cur, false);
      desync(s, (uint8_t)SD_OPEN_HASH, m.p.action.open_hash);
      return;
    }
    const uint8_t ps = peer_side_of(s);
    if (s.st->side[ps].pending_kind != (uint8_t)BACT_NONE) {
      if (s.st->side[ps].pending_kind == m.p.action.kind &&
          s.st->side[ps].pending_index == m.p.action.index) {
        count_stale(s, m);
        if (reack_allowed(s, cur)) send_action_result(s, true);
      } else {
        // The accepted bytes are already in st.side[ps].pending_*, so catching
        // this costs nothing.
        session_close(s, SE_PROTOCOL, (uint8_t)SD_ACTION_CHANGED);
      }
      return;
    }
    BattleAction a; a.kind = m.p.action.kind; a.index = m.p.action.index;
    const BattleReject r = battle_submit_action(*s.st, ps, a);
    s.ar_round  = cur;
    s.ar_reject = (uint8_t)r;
    s.ar_kind   = a.kind;
    s.ar_index  = a.index;
    s.ar_hash   = s.open_hash;
    send_action_result(s, false);
    if (r != BR_OK) {
      // The engine refused it with the state BIT-IDENTICAL (game/battle.h), so
      // nothing needs unwinding. The peer has been told by name; both ends
      // terminate rather than looping on an action neither can legalise.
      session_close(s, SE_PROTOCOL, (uint8_t)SD_ACTION_REJECT);
      return;
    }
    session_progress(s);
    try_resolve(s);
    return;
  }
  if ((uint16_t)m.round + 1u == (uint16_t)cur) {
    // ANSWERED, NEVER SUBMITTED: round m.round only resolved BECAUSE this
    // action was accepted, so replying from state is sound - and it is what
    // stops a peer retransmitting a resolved round from dying at its ladder.
    count_stale(s, m);
    reanswer_round(s, m.round);
    return;
  }
  if (m.round > cur) {
    // INV-1 says the peer can be at most one round ahead. Old frames in flight
    // are ordinary on a reordering link; a FUTURE one is not.
    session_close(s, SE_PROTOCOL, (uint8_t)SD_ROUND_GAP);
    return;
  }
  count_stale(s, m);
}

static void on_action_result(Session& s, const ProtoMsg& m)
{
  if (m.p.ares.reject != (uint8_t)BR_OK) {
    // NEVER RETRIED, and an ACTION_RESULT is never answered by an
    // ACTION_RESULT: that is an attacker-drivable ping-pong.
    session_close(s, SE_PROTOCOL, (uint8_t)SD_ACTION_REJECT);
    return;
  }
  if (m.round != s.my_act_round) { count_stale(s, m); return; }
  // ONLY THE FIRST ONE IS PROGRESS (R4). The second ACTION_RESULT for a round is
  // a re-acknowledgement - which is precisely what reanswer_round() regenerates
  // for an honest peer - and a re-acknowledgement leaves the ladder where it
  // was. Every other handler in this file already guards its repeat; this one
  // did not, so a peer that said nothing else and replayed one legal frame at a
  // spacing IT chose held the session open forever with tx_retx stuck at 0.
  // A round number is used once per battle, so equality is the whole test.
  if (s.ar_ack_round == m.round) { count_stale(s, m); return; }
  s.ar_ack_round = m.round;
  s.waiting_for = (uint8_t)PT_ACTION;
  session_progress(s);
}

static void on_round_result(Session& s, const ProtoMsg& m)
{
  const uint8_t cur = (uint8_t)s.st->round;

  if (s.peer_rr_round == m.round) {
    if (s.peer_rr_before != m.p.rres.hash_before ||
        s.peer_rr_after  != m.p.rres.hash_after) {
      desync(s, (uint8_t)SD_RESTATED, m.p.rres.hash_after);
      return;
    }
    count_stale(s, m);
    if (s.rr_round == m.round && reack_allowed(s, m.round)) send_round_result(s, true);
    return;
  }

  if (s.barrier != 0u && s.rr_round == m.round) {
    s.peer_rr_round  = m.round;
    s.peer_rr_before = m.p.rres.hash_before;
    s.peer_rr_after  = m.p.rres.hash_after;
    if (m.p.rres.hash_before != s.rr_before) {
      desync(s, (uint8_t)SD_INPUTS, m.p.rres.hash_before);
      return;
    }
    if (m.p.rres.hash_after != s.rr_after) {
      desync(s, (uint8_t)SD_RULES, m.p.rres.hash_after);
      return;
    }
    s.barrier = 0u;
    s.waiting_for = (uint8_t)PT_ACTION;
    session_progress(s);
    try_resolve(s);            // the peer's next ACTION may already be pending
    return;
  }

  if (m.round == cur) {
    // The peer resolved a round we have not: we are missing its ACTION. Our own
    // is the pull that makes it regenerate one.
    s.peer_rr_round  = m.round;
    s.peer_rr_before = m.p.rres.hash_before;
    s.peer_rr_after  = m.p.rres.hash_after;
    count_stale(s, m);
    if (reack_allowed(s, cur)) { send_action(s, true); send_battle_state(s, cur, true); }
    return;
  }
  if (m.round > cur) { session_close(s, SE_PROTOCOL, (uint8_t)SD_ROUND_GAP); return; }
  count_stale(s, m);
  reanswer_round(s, m.round);
}

static void on_battle_state(Session& s, const ProtoMsg& m)
{
  const uint8_t cur = (uint8_t)s.st->round;
  if (m.round == cur) {
    if (m.p.bstate.open_hash != s.open_hash) {
      // The probe turns a nine-second silence into an early NAMED desync.
      desync(s, (uint8_t)SD_PROBE, m.p.bstate.open_hash);
      return;
    }
    count_stale(s, m);
    return;
  }
  if ((uint16_t)m.round + 1u == (uint16_t)cur) { count_stale(s, m); reanswer_round(s, m.round); return; }
  if (m.round == (uint8_t)(cur + 1u)) {
    // The peer is one ahead: it has resolved our round and we are missing its
    // ACTION. Pull it with our own.
    count_stale(s, m);
    if (reack_allowed(s, cur)) send_action(s, true);
    return;
  }
  session_close(s, SE_PROTOCOL, (uint8_t)SD_ROUND_GAP);
}

static void on_battle_end(Session& s, const ProtoMsg& m)
{
  if (m.p.bend.reason == (uint8_t)SE_DESYNC) {
    s.end.peer_hash = m.p.bend.final_hash;
    session_close(s, SE_DESYNC, m.p.bend.detail);
    return;
  }
  if (m.p.bend.reason != (uint8_t)SE_DONE) {
    session_close(s, SE_LOST, (uint8_t)SD_PEER_GOODBYE);
    return;
  }
  if (s.peer_end_seen != 0u && s.state == (uint8_t)SS_ENDING) {
    // A repeat: the peer has not seen ours. ANSWER IT from state.
    count_stale(s, m);
    if (reack_allowed(s, 0u)) {
      send_battle_end(s, (uint8_t)SE_DONE, (uint8_t)SD_NONE, true);
      if (s.paid != 0u) send_goodbye_done(s);
    }
    return;
  }
  s.peer_end_seen    = 1u;
  s.peer_end_outcome = m.p.bend.outcome;
  s.peer_end_hash    = m.p.bend.final_hash;
  if (s.state == (uint8_t)SS_ENDING) { compare_end(s); return; }
  // Still resolving: the peer finished a round before us, which is legal. It is
  // recorded and compared when we get there, and the ladder is NOT reset -
  // this is not the frame we are waiting for.
  count_stale(s, m);
}

void link_on_msg(Session& s, const ProtoMsg& m)
{
  if (s.st == nullptr) { count_wrong(s, m); return; }
  const ProtoType t = (ProtoType)m.type;

  if (t == PT_GOODBYE) {
    if (s.state == (uint8_t)SS_ENDING) {
      // The farewell only ENDS a session we already agreed on. A GOODBYE that
      // overtook the BATTLE_END it follows proves nothing about the battle, so
      // it is counted and the ladder is left to do its work rather than being
      // read as a loss.
      if (s.paid != 0u) { session_close(s, SE_DONE, (uint8_t)SD_NONE); return; }
      count_stale(s, m);
      return;
    }
    session_close(s, SE_LOST, (uint8_t)SD_PEER_GOODBYE);
    return;
  }
  if (t == PT_BATTLE_END) { on_battle_end(s, m); return; }

  if (s.state == (uint8_t)SS_VERIFY) {
    if (t == PT_BATTLE_STATE && m.round == 1u) {
      if (m.p.bstate.open_hash != s.open_hash) {
        // Two engines that disagree before a single action has been chosen are
        // playing different games; no round is ever resolved.
        desync(s, (uint8_t)SD_SETUP, m.p.bstate.open_hash);
        return;
      }
      s.waiting_for = (uint8_t)PT_ACTION;
      session_goto(s, SS_BATTLE);
      session_progress(s);
      return;
    }
    if (t == PT_ACTION && m.round == 1u) {
      // The peer has already matched our round-1 hash - it would not be sending
      // an ACTION otherwise - so the barrier is lifted and the action is handled
      // by the ordinary rules.
      session_goto(s, SS_BATTLE);
      session_progress(s);
      on_action(s, m);
      return;
    }
    count_wrong(s, m);
    return;
  }

  if (s.state == (uint8_t)SS_BATTLE) {
    switch (t) {
      case PT_ACTION:        on_action(s, m); break;
      case PT_ACTION_RESULT: on_action_result(s, m); break;
      case PT_ROUND_RESULT:  on_round_result(s, m); break;
      case PT_BATTLE_STATE:  on_battle_state(s, m); break;
      default:               count_wrong(s, m); break;
    }
    return;
  }

  // SS_ENDING. The three in-battle types are ANSWERED from state rather than
  // dropped: a peer whose last round is incomplete needs exactly the frames we
  // still hold, and dropping them here would strand it at its ladder after we
  // had already finished.
  switch (t) {
    case PT_ACTION:
    case PT_ROUND_RESULT:
    case PT_BATTLE_STATE:
      count_stale(s, m);
      reanswer_round(s, m.round);
      break;
    default:
      count_wrong(s, m);
      break;
  }
}

// -----------------------------------------------------------------------------
//  THE LADDER'S REGENERATION FOR THE THREE LOCKSTEP STATES
// -----------------------------------------------------------------------------
void link_resend(Session& s)
{
  if (s.st == nullptr) return;
  if (s.state == (uint8_t)SS_VERIFY) { send_battle_state(s, 1u, true); return; }
  if (s.state == (uint8_t)SS_ENDING) {
    send_battle_end(s, (uint8_t)SE_DONE, (uint8_t)SD_NONE, true);
    if (s.paid != 0u) send_goodbye_done(s);
    return;
  }

  // EXACTLY ONE FRAME PER RUNG, and each of the three is both what we owe and
  // the pull the peer needs.
  //
  // THE AGREED DESIGN ALSO SENT A BATTLE_STATE ALONGSIDE, from rung
  // PROTO_PROBE_AT onward, to turn a nine-second silence into an early NAMED
  // desync. IT IS DELETED AND THE CONSTANT WITH IT, because no case could be
  // written that it makes pass: in the first arm the outstanding ACTION already
  // carries this round's open hash and a mismatch is SD_OPEN_HASH before any
  // probe; in the second the outstanding ROUND_RESULT already carries both of
  // that round's hashes; and in the third a BATTLE_STATE is what we send anyway.
  // A branch whose claim no test can hold is the defect this project keeps
  // finding, so it is narrowed away rather than kept with a comment.
  const uint8_t cur = (uint8_t)s.st->round;
  if (s.barrier != 0u) {
    send_round_result(s, true);                 // the peer has not matched it yet
  } else if (s.st->side[s.side].pending_kind != (uint8_t)BACT_NONE) {
    send_action(s, true);                       // the peer has not accepted it yet
  } else {
    send_battle_state(s, cur, true);            // we owe nothing: ask what it holds
  }
}
