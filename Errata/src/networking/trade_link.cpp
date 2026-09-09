// =============================================================================
//  ERRATA - networking/trade_link.cpp
//  See trade_link.h for the five wire steps, the COMMIT-subsumes-CONFIRM rule
//  and why this file touches neither the Box nor flash.
//
//  NO FILE-SCOPE MUTABLE STATE, for session.cpp's reason: it is what lets one
//  host process run two endpoints against each other. tools/check.sh gates it.
// =============================================================================
#include "trade_link.h"

#include <string.h>

#include "../core/crc16.h"

// -----------------------------------------------------------------------------
//  THE CODEC game/trade.cpp DRIVES THE JOURNAL THROUGH (trade_link.h)
// -----------------------------------------------------------------------------
static VReject tw_decode(const uint8_t rec[TR_WIRE_BYTES], BugInstance& out)
{
  return pbw_decode(rec, out);
}

// THE IDEMPOTENCE HINGE, and it is four lines. The id is written little-endian
// by explicit shifts - never a memcpy of a uint32_t - for protocol.cpp's reason:
// this is the layer whose host test has to mean something on another machine.
// The CRC is then recomputed over exactly the span BUGW_CRC_BYTES names, so
// pbw_decode() accepts the patched record and VR_WIRE_CRC cannot be the answer
// to a patch this function made.
static void tw_set_id(uint8_t rec[TR_WIRE_BYTES], uint32_t id)
{
  rec[BUGW_OFF_ID + 0] = (uint8_t)(id & 0xFFu);
  rec[BUGW_OFF_ID + 1] = (uint8_t)((id >> 8) & 0xFFu);
  rec[BUGW_OFF_ID + 2] = (uint8_t)((id >> 16) & 0xFFu);
  rec[BUGW_OFF_ID + 3] = (uint8_t)((id >> 24) & 0xFFu);
  const uint16_t crc = crc16_ccitt(rec, (size_t)BUGW_CRC_BYTES);
  rec[BUGW_OFF_CRC + 0] = (uint8_t)(crc & 0xFFu);
  rec[BUGW_OFF_CRC + 1] = (uint8_t)((crc >> 8) & 0xFFu);
}

static const TradeCodec TRADE_WIRE_CODEC = { &tw_decode, &tw_set_id };

const TradeCodec& trade_wire_codec(void) { return TRADE_WIRE_CODEC; }

static const char* const TLP_NAMES[] = {
  "TLP_IDLE", "TLP_OFFERED", "TLP_REVIEW", "TLP_ASK_PLAYER", "TLP_WAIT_PEER",
  "TLP_APPLIED", "TLP_DONE", "TLP_REFUSED"
};
static_assert(sizeof(TLP_NAMES) / sizeof(TLP_NAMES[0]) == (size_t)TLP_PHASE_COUNT,
              "a TradeLinkPhase was added without its name");

const char* trade_phase_name(TradeLinkPhase p)
{
  if ((uint8_t)p >= (uint8_t)TLP_PHASE_COUNT) return "TLP_?";
  return TLP_NAMES[(uint8_t)p];
}

// The re-answer leash, the same heuristic and the same number the handshake
// uses: a READY answered by a READY can bounce between two endpoints that are
// both past that step, and 16 bounds it.
#define TRADE_MAX_REACK 16u

// -----------------------------------------------------------------------------
//  WIRING
// -----------------------------------------------------------------------------
void trade_link_init(TradeLink& tl, Session& s, uint32_t out_id,
                     const TradeHooks& hooks)
{
  memset(&tl, 0, sizeof tl);
  tl.hooks  = hooks;
  tl.out_id = out_id;
  tl.phase  = (uint8_t)TLP_IDLE;
  // The record the session froze at session_set_trade(). Copied rather than
  // aliased so this module reads ONE source of truth even if a caller were to
  // re-freeze the session's.
  memcpy(tl.out_rec, s.my_rec[0], (size_t)BUGW_BYTES);
  s.tl = &tl;
}

// -----------------------------------------------------------------------------
//  SENDING. Everything is regenerated from the TradeLink, so the ladder costs
//  no buffer - the same rule battle_link.cpp follows.
// -----------------------------------------------------------------------------
static void send_offer(Session& s, bool retx)
{
  if (s.tl == nullptr) return;
  ProtoMsg m; session_prepare(s, m, PT_TRADE_OFFER, 0u);
  if (retx) m.flags = (uint8_t)PF_RETX;
  memcpy(m.p.toffer.rec, s.tl->out_rec, (size_t)BUGW_BYTES);
  session_emit(s, m);
}

static void send_ready(Session& s, bool retx)
{
  if (s.tl == nullptr) return;
  ProtoMsg m; session_prepare(s, m, PT_TRADE_READY, 0u);
  if (retx) m.flags = (uint8_t)PF_RETX;
  m.p.tready.verdict        = s.tl->in_verdict;
  m.p.tready.policy         = s.tl->in_policy;
  m.p.tready.offer_crc_echo = s.tl->in_crc;
  session_emit(s, m);
}

static void send_confirm(Session& s, bool retx)
{
  if (s.tl == nullptr) return;
  ProtoMsg m; session_prepare(s, m, PT_TRADE_CONFIRM, 0u);
  if (retx) m.flags = (uint8_t)PF_RETX;
  m.p.tconfirm.accept   = 1u;
  m.p.tconfirm.pair_crc = s.tl->pair_crc;
  session_emit(s, m);
}

static void send_commit(Session& s, bool retx)
{
  if (s.tl == nullptr) return;
  ProtoMsg m; session_prepare(s, m, PT_TRADE_COMMIT, 0u);
  if (retx) m.flags = (uint8_t)PF_RETX;
  m.p.tcommit.pair_crc = s.tl->pair_crc;
  session_emit(s, m);
}

// -----------------------------------------------------------------------------
//  THE PAIR IDENTITY. Canonical order: the INITIATOR's record first, on both
//  devices, so the two compute the same number for the same exchange.
// -----------------------------------------------------------------------------
static void fix_pair_crc(Session& s)
{
  TradeLink& tl = *s.tl;
  const bool me_first = (s.role == (uint8_t)SR_INITIATOR);
  tl.pair_crc = me_first ? trade_pair_crc(tl.out_rec, tl.in_rec)
                         : trade_pair_crc(tl.in_rec, tl.out_rec);
}

// -----------------------------------------------------------------------------
//  ENTERING
// -----------------------------------------------------------------------------
void trade_link_begin(Session& s)
{
  if (s.tl == nullptr) { session_close(s, SE_PROTOCOL, (uint8_t)SD_INTERNAL); return; }
  // W1: OUR OFFER IS OUT. The journal is written BEFORE the frame, because the
  // record has to name what we are giving from the moment anyone else could
  // start acting on it. A store that refuses is a LOCAL fault and the trade
  // stops here with nothing on the wire.
  if (s.tl->hooks.journal_sent != nullptr &&
      !s.tl->hooks.journal_sent(s.tl->hooks.ctx, s.tl->out_id, s.peer_device_id)) {
    session_close(s, SE_PROTOCOL, (uint8_t)SD_TRADE_STORE);
    return;
  }
  s.tl->phase = (uint8_t)TLP_OFFERED;
  send_offer(s, false);
  s.waiting_for = (uint8_t)PT_TRADE_OFFER;
  session_goto(s, SS_TRADE);
  session_progress(s);
}

// -----------------------------------------------------------------------------
//  THE STEPS
// -----------------------------------------------------------------------------
static void refuse(Session& s, SessionEndReason r, uint8_t detail)
{
  if (s.tl != nullptr) {
    s.tl->phase = (uint8_t)TLP_REFUSED;
    if (s.tl->hooks.abort != nullptr) s.tl->hooks.abort(s.tl->hooks.ctx);
  }
  session_close(s, r, detail);
}

// Both halves of the review are in and both said yes: the OBJECTS are agreed
// and the only thing left is the two people.
//
// IT IS CALLED FROM BOTH HANDLERS AND THAT IS NOT BELT AND BRACES. It was in
// on_ready() alone, and a peer whose READY arrived BEFORE its OFFER - which a
// plain drop produces as readily as a reorder, because the two frames are
// re-sent on different rungs - left this endpoint parked in TLP_REVIEW with
// both halves already in hand, waiting out its whole ladder for a frame that
// had already been delivered. MEASURED over the loopback: 74 of 200 trials at
// 10 % drop, 183 of 200 at 30 %. It is the same shape as the handshake hole
// networking/session.h has the measurement for, and the same answer: decide
// from STATE, never from the arrival that happened to be last.
static void maybe_ask_player(Session& s)
{
  TradeLink& tl = *s.tl;
  if (tl.have_in == 0u || tl.peer_ready == 0u) return;
  if (tl.in_verdict != (uint8_t)VR_OK || tl.in_policy != 0u) return;
  if (tl.peer_verdict != (uint8_t)VR_OK || tl.peer_policy != 0u) return;
  if (tl.phase == (uint8_t)TLP_REVIEW || tl.phase == (uint8_t)TLP_OFFERED) {
    tl.phase      = (uint8_t)TLP_ASK_PLAYER;
    s.waiting_for = (uint8_t)PT_TRADE_CONFIRM;
  }
}

static void on_offer(Session& s, const ProtoMsg& m)
{
  TradeLink& tl = *s.tl;
  const uint16_t crc = trade_rec_crc(m.p.toffer.rec);

  if (tl.have_in != 0u) {
    // A SECOND OFFER. Different bytes is "the peer changed what it is giving"
    // and it is a TERMINATION, exactly as SD_TEAM_CHANGED is for a team - the
    // consent a player is about to give names one specific pair of Bugs.
    if (crc != tl.in_crc ||
        memcmp(tl.in_rec, m.p.toffer.rec, (size_t)BUGW_BYTES) != 0) {
      refuse(s, SE_PROTOCOL, (uint8_t)SD_TEAM_CHANGED);
      return;
    }
    // A benign retransmission: ANSWER it from state and advance nothing (R4).
    if (s.end.rx_stale < 0xFFFFu) s.end.rx_stale++;
    session_note(s, LEK_REACK, (uint8_t)PT_TRADE_OFFER, 0u, 0u, m.seq);
    if (tl.reack < (uint8_t)TRADE_MAX_REACK) { tl.reack++; send_ready(s, true); }
    return;
  }

  memcpy(tl.in_rec, m.p.toffer.rec, (size_t)BUGW_BYTES);
  tl.have_in = 1u;
  tl.in_crc  = crc;
  fix_pair_crc(s);

  // THE ONE VALIDATOR, on the wire path, exactly where protocol.h says it runs:
  // pbw_decode() IS validate_bug() in front of a decode, and its answer is
  // the VReject half of our READY.
  BugInstance in;
  const VReject v = pbw_decode(tl.in_rec, in);
  tl.in_verdict = (uint8_t)v;
  // THE POLICY HALF, which is the game layer's and not ours: the taint gate, a
  // duplicate id, a peer that is us. A legal object this device will not take.
  tl.in_policy = (v == VR_OK && tl.hooks.judge != nullptr)
                     ? tl.hooks.judge(tl.hooks.ctx, tl.in_rec)
                     : 0u;

  if (tl.in_verdict == (uint8_t)VR_OK && tl.in_policy == 0u) {
    // W2: THEIRS IS HERE AND OURS IS NOT GONE. Journalled before the READY, so
    // a cut between the two leaves a record boot rolls back rather than a
    // promise nobody wrote down.
    if (tl.hooks.journal_received != nullptr &&
        !tl.hooks.journal_received(tl.hooks.ctx, tl.in_rec)) {
      send_ready(s, false);                 // tell the peer, then stop
      refuse(s, SE_PROTOCOL, (uint8_t)SD_TRADE_STORE);
      return;
    }
  }

  send_ready(s, false);
  session_progress(s);

  if (tl.in_verdict != (uint8_t)VR_OK) {
    refuse(s, SE_REJECTED, tl.in_verdict);
    return;
  }
  if (tl.in_policy != 0u) {
    refuse(s, SE_REJECTED, (uint8_t)SD_TRADE_REFUSED);
    return;
  }
  tl.phase      = (uint8_t)TLP_REVIEW;
  s.waiting_for = (uint8_t)PT_TRADE_READY;
  maybe_ask_player(s);          // their READY may already be in hand
}

static void on_ready(Session& s, const ProtoMsg& m)
{
  TradeLink& tl = *s.tl;
  if (m.p.tready.offer_crc_echo != trade_rec_crc(tl.out_rec)) {
    // A verdict about a record that is not the one we sent.
    if (s.end.rx_wrong_state < 0xFFFFu) s.end.rx_wrong_state++;
    session_note(s, LEK_WRONG_STATE, m.type, 0u, 0u, m.seq);
    return;
  }
  if (tl.peer_ready != 0u) {              // a duplicate the window let through
    if (s.end.rx_stale < 0xFFFFu) s.end.rx_stale++;
    return;
  }
  tl.peer_ready   = 1u;
  tl.peer_verdict = m.p.tready.verdict;
  tl.peer_policy  = m.p.tready.policy;
  session_progress(s);

  if (tl.peer_verdict != (uint8_t)VR_OK) {
    refuse(s, SE_REJECTED, tl.peer_verdict);
    return;
  }
  if (tl.peer_policy != 0u) {
    refuse(s, SE_REJECTED, (uint8_t)SD_TRADE_REFUSED);
    return;
  }
  maybe_ask_player(s);
}

// The one place a Bug moves. Reached only with BOTH confirms in hand.
static void run_commit(Session& s)
{
  TradeLink& tl = *s.tl;
  if (tl.applied != 0u) return;                       // idempotent, like the boot path
  if (tl.local_accept == 0u || tl.peer_accept == 0u) return;

  // W3 -> B1 -> B2 -> B3 -> W4, and every byte of that order belongs to
  // game/trade.cpp. This module does not know what a Box is.
  if (tl.hooks.commit == nullptr || !tl.hooks.commit(tl.hooks.ctx)) {
    refuse(s, SE_PROTOCOL, (uint8_t)SD_TRADE_STORE);
    return;
  }
  tl.applied    = 1u;
  tl.phase      = (uint8_t)TLP_APPLIED;
  s.waiting_for = (uint8_t)PT_TRADE_COMMIT;
  send_commit(s, false);
  session_progress(s);

  if (tl.peer_commit != 0u) {
    tl.phase = (uint8_t)TLP_DONE;
    session_close(s, SE_DONE, (uint8_t)SD_NONE);
  }
}

static void on_confirm(Session& s, const ProtoMsg& m)
{
  TradeLink& tl = *s.tl;
  if (tl.have_in == 0u || m.p.tconfirm.pair_crc != tl.pair_crc) {
    // A CONFIRM for a pair that is not the one on this table. This is the
    // reason trade_pair_crc() is order-dependent: a symmetric identity could
    // not tell "A gives X for Y" from "A gives Y for X".
    if (s.end.rx_wrong_state < 0xFFFFu) s.end.rx_wrong_state++;
    session_note(s, LEK_WRONG_STATE, m.type, 0u, 0u, m.seq);
    return;
  }
  if (m.p.tconfirm.accept == 0u) {        // the peer's player said no
    refuse(s, SE_REJECTED, (uint8_t)SD_TRADE_REFUSED);
    return;
  }
  if (tl.peer_accept != 0u) {
    if (s.end.rx_stale < 0xFFFFu) s.end.rx_stale++;
    if (tl.reack < (uint8_t)TRADE_MAX_REACK && tl.local_accept != 0u) {
      tl.reack++;
      if (tl.applied != 0u) send_commit(s, true); else send_confirm(s, true);
    }
    return;
  }
  tl.peer_accept = 1u;
  session_progress(s);
  run_commit(s);
}

static void on_commit(Session& s, const ProtoMsg& m)
{
  TradeLink& tl = *s.tl;
  if (tl.have_in == 0u || m.p.tcommit.pair_crc != tl.pair_crc) {
    if (s.end.rx_wrong_state < 0xFFFFu) s.end.rx_wrong_state++;
    session_note(s, LEK_WRONG_STATE, m.type, 0u, 0u, m.seq);
    return;
  }
  if (tl.peer_commit != 0u) {
    if (s.end.rx_stale < 0xFFFFu) s.end.rx_stale++;
    if (tl.applied != 0u && tl.reack < (uint8_t)TRADE_MAX_REACK) {
      tl.reack++; send_commit(s, true);
    }
    return;
  }
  // COMMIT SUBSUMES CONFIRM (trade_link.h). A peer that applied has necessarily
  // confirmed, so a lost CONFIRM does not strand this side.
  tl.peer_commit = 1u;
  tl.peer_accept = 1u;
  session_progress(s);

  if (tl.applied != 0u) {
    tl.phase = (uint8_t)TLP_DONE;
    session_close(s, SE_DONE, (uint8_t)SD_NONE);
    return;
  }
  // The peer has applied and we have not. If our player has already said yes we
  // apply now; if they have not, the peer committed against a consent this
  // device never gave - which is a protocol violation, not a race, because a
  // CONFIRM is the only thing that can produce a COMMIT.
  if (tl.local_accept == 0u) {
    refuse(s, SE_PROTOCOL, (uint8_t)SD_TRADE_REFUSED);
    return;
  }
  // run_commit() closes SE_DONE itself once it has applied and peer_commit is
  // set, which it is by the two lines above. A second close here would be a
  // second rule for one fact, and two rules for one fact is what this file's
  // neighbours keep finding.
  run_commit(s);
}

void trade_link_on_msg(Session& s, const ProtoMsg& m)
{
  if (s.tl == nullptr) { session_close(s, SE_PROTOCOL, (uint8_t)SD_INTERNAL); return; }
  switch ((ProtoType)m.type) {
    case PT_TRADE_OFFER:   on_offer(s, m);   break;
    case PT_TRADE_READY:   on_ready(s, m);   break;
    case PT_TRADE_CONFIRM: on_confirm(s, m); break;
    case PT_TRADE_COMMIT:  on_commit(s, m);  break;
    default:
      // R3: legal but not expected here. The handshake's own re-answer runs
      // first, because a CAPABILITIES that overtook its HELLO is the peer being
      // behind rather than the peer being wrong (session.h has the measurement).
      if (session_reanswer_earlier_phase(s, m)) return;
      if (s.end.rx_wrong_state < 0xFFFFu) s.end.rx_wrong_state++;
      session_note(s, LEK_WRONG_STATE, m.type, 0u, 0u, m.seq);
      break;
  }
}

// -----------------------------------------------------------------------------
//  THE LADDER. What this endpoint OWES, rebuilt from state.
// -----------------------------------------------------------------------------
// WHAT THIS ENDPOINT OWES IS DECIDED BY WHAT THE PEER HAS ACKNOWLEDGED, NOT BY
// WHAT PHASE WE ARE IN, AND THAT DISTINCTION WAS MEASURED. The first version of
// this function sent one frame - whatever the LOCAL phase said was newest - and
// a peer that lost our OFFER was then stranded for ever: we would climb the
// whole ladder re-sending a CONFIRM to a device still waiting for a record it
// never received. Over the loopback at 10 % drop that cost 74 of 200 trials;
// with the rule below it costs 3. It is the same "answer from state" shape
// networking/session.h has the measurement for, applied to three obligations:
//
//   * the OFFER is owed until the peer has sent a READY, because a READY is
//     only ever sent by a device that has our record;
//   * the READY is owed until the peer has CONFIRMED, because a CONFIRM is only
//     ever sent by a device that has our verdict;
//   * and then exactly one of CONFIRM or COMMIT, whichever we have reached.
//
// So the rung costs three frames at the start and one at the end, and never
// re-sends something the peer has already proved it holds.
void trade_link_resend(Session& s)
{
  if (s.tl == nullptr) return;
  TradeLink& tl = *s.tl;
  if (tl.peer_ready == 0u)                        send_offer(s, true);
  if (tl.have_in != 0u && tl.peer_accept == 0u)   send_ready(s, true);
  if (tl.applied != 0u)                           send_commit(s, true);
  else if (tl.local_accept != 0u)                 send_confirm(s, true);
}

// -----------------------------------------------------------------------------
//  THE PLAYER
// -----------------------------------------------------------------------------
bool trade_link_wants_consent(const Session& s)
{
  if (s.tl == nullptr || s.state != (uint8_t)SS_TRADE) return false;
  return s.tl->phase == (uint8_t)TLP_ASK_PLAYER && s.tl->local_accept == 0u;
}

void trade_link_accept(Session& s)
{
  if (!trade_link_wants_consent(s)) return;
  TradeLink& tl = *s.tl;
  tl.local_accept = 1u;
  tl.phase        = (uint8_t)TLP_WAIT_PEER;
  s.waiting_for   = (uint8_t)PT_TRADE_COMMIT;
  send_confirm(s, false);
  session_progress(s);
  run_commit(s);
}

bool trade_link_completed(const Session& s)
{
  return s.tl != nullptr && s.tl->applied != 0u && s.tl->peer_commit != 0u;
}
