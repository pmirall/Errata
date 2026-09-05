// =============================================================================
//  PEBBLEBOL - networking/session.h
//  THE SESSION FSM (spec section 15, plan P4-C5).
//
//  HELLO -> CAPABILITIES -> SESSION_REQUEST/SESSION_ACCEPT (which is where the
//  SHARED SEED is agreed) -> TEAM_SUBMIT/TEAM_VALIDATION -> the lockstep battle
//  (networking/battle_link.cpp) -> BATTLE_END -> GOODBYE. Nine states, one of
//  them terminal, and a fixed 40 B record written on the way into it that says
//  why.
//
//  THE TRANSPORT IS A PARAMETER AND now_ms IS A PARAMETER. There is no clock
//  call and no radio call anywhere in session.cpp or battle_link.cpp, so the
//  whole nine-second retransmission ladder runs in zero wall-clock seconds in a
//  host test and the identical code runs over ESP-NOW in P7 with no #ifdef.
//
// -----------------------------------------------------------------------------
//  FOUR RULES APPLIED BEFORE THE STATE TABLE, WHICH IS WHAT KEEPS IT SMALL
// -----------------------------------------------------------------------------
//   R1  A frame networking/protocol.cpp refused NEVER REACHES THE FSM AND NEVER
//       TOUCHES A TIMER. Otherwise a malformed flood keeps a dead session alive
//       for as long as the attacker keeps typing.
//   R2  A frame for another session is dropped before the FSM (HELLO excepted -
//       it predates the session by definition). proto_decode() does this itself,
//       given the session id this endpoint holds.
//   R3  A message that is legal but not expected in this state is DROPPED AND
//       COUNTED, never aborted on. A peer whose reply crossed ours on the wire
//       is not an attacker.
//   R4  ONLY PROGRESS TOUCHES THE TIMER. A duplicate, a re-acknowledgement, a
//       stale frame and a wrong-state frame all leave the ladder exactly where
//       it was, so the ladder is the only liveness bound in the design and no
//       separate "wrong state tolerance" constant is needed. EVERY HANDLER HAS
//       TO GUARD ITS OWN REPEAT FOR THIS TO BE TRUE, and one did not:
//       on_action_result() called session_progress() for every ACTION_RESULT
//       matching our outstanding ACTION, so a peer replaying ONE legal frame
//       held the session open for as long as it kept typing (measured: 1,019
//       injections, seventeen hours of virtual time, tx_retx stuck at 0). It is
//       now leashed by s.ar_ack_round and pinned by
//       `one_legal_frame_replayed_forever_cannot_hold_the_session_open`, which
//       replays a BATTLE_STATE in the same shape as its control.
//
//  FIVE anti-exhaustion caps remain and every one is NAMED AS A HEURISTIC
//  rather than derived: SESSION_MAX_RX, SESSION_MAX_TX,
//  SESSION_MAX_REACK_PER_ROUND, SESSION_MAX_HANDSHAKE_REACK (in session.cpp,
//  beside the rule it leashes) and SESSION_SEQ_MAX_JUMP. They come from the
//  worst honest case plus slack, not from an adversary, because there is no
//  radio in this step. They bound how long ONE hostile session can hold the
//  link and they do nothing about a peer that opens another immediately;
//  discovery-level rate limiting belongs to P7.
//
// -----------------------------------------------------------------------------
//  THE LADDER, AND WHY IT IS 9 x 1 s RATHER THAN THE PLAN'S 3 x 3 s
// -----------------------------------------------------------------------------
//  Same nine-second deadline, spent as nine attempts instead of three. MEASURED
//  on the same 500 seeds per arm, changing only these two constants:
//
//      arm                          3 x 3000        9 x 1000
//      10 % drop                    483 / 500       500 / 500
//      10 % drop + dup + reorder    482 / 500       500 / 500
//      30 % drop, 20 % reorder      108 / 500       471 / 500
//
//  (The harsh arm was 474 before the P4-C5 follow-up closed the ACTION_RESULT
//  hole in R4 below. Three trials of five hundred were completing on a ladder
//  that a re-acknowledgement had wrongly reset, and they are not a loss worth
//  keeping: the same reset is what let a peer hold a session open forever.)
//
//  THE ESTIMATE THAT CHOSE NINE WAS PESSIMISTIC AND THE MEASUREMENT SAYS SO.
//  The design reasoned that an obligation needs its frame out AND the clearing
//  reply back - about 0.81 per attempt - so three attempts leave 6.9e-3 per
//  obligation against roughly 240 of them in a 3v3, and therefore that MOST
//  battles would abort at three rungs. They do not: 3.4 % do. The reason is the
//  answer-from-state rules below - a peer that is behind PULLS what it needs
//  from the peer that is ahead, so two ladders and several pull paths cooperate
//  on every gap and the independent-obligation arithmetic does not hold. Nine
//  is still the right number, and what justifies it is the table and not the
//  estimate.
//
//  RETRANSMISSION IS BY REGENERATION FROM STATE. Nothing stores a transmitted
//  frame: the session re-encodes what its own state says it owes. That is what
//  makes a retransmission free of a tx buffer, and it is why the RE-ACK rules
//  below cost nothing either.
//
// -----------------------------------------------------------------------------
//  THE SEQUENCE NUMBER IS A DE-DUPLICATION WINDOW AND NOT THE REPLAY DEFENCE
// -----------------------------------------------------------------------------
//  Every frame this endpoint sends gets a FRESH seq, retransmissions included
//  (they carry PF_RETX, which is diagnostic and is never branched on). So a
//  repeated seq means exactly one thing - the TRANSPORT delivered the same
//  frame twice - and dropping it is free and correct. A 32-frame sliding window
//  also names the two other things a lossy link does: a seq above the expected
//  one is a GAP (something was lost or overtaken) and a seq below the window is
//  STALE. All three are counted in SessionEnd.
//
//  THIS IS A DEVIATION FROM THE AGREED DESIGN, WHICH SAID A RETRANSMISSION
//  REUSES ITS seq VERBATIM, and the reason is that the two rules cannot both
//  hold: if a retransmission reuses its seq, the de-duplication window cannot
//  tell a transport duplicate from a retransmission, and dropping duplicates
//  would then discard exactly the retransmissions the ladder depends on. The
//  plan asks for a window that drops duplicates, so the seq is allocated fresh
//  and message IDENTITY is carried by CONTENT instead - which the FSM needs
//  anyway, because a radio can duplicate a frame the sender never repeated.
//
//  WHAT IS NOT A DEFENCE, said plainly: the seq is not a replay defence and the
//  CRC is not an authenticator. The session id travels in clear in every frame,
//  so it is a demultiplexing key. On P7's shared medium an off-path device that
//  reads one frame can inject well-formed ones; its power is bounded to DENIAL
//  (an injected ACTION for a round already submitted terminates the session by
//  name rather than being applied) but it is real, and there is no fix at this
//  layer without a key exchange that spec section 15's message set does not
//  contain.
//
// -----------------------------------------------------------------------------
//  PURE MODULE. stdint, string.h, the codec, the battle engine, the validator
//  and core/crc16.h. NO Arduino.h, no esp_now.h, no WiFi.h, no renderer, no
//  heap, no float, no clock, no RNG draw, and NO FILE-SCOPE MUTABLE STATE in
//  session.cpp or battle_link.cpp - which is what lets one host process run two
//  endpoints against each other. tools/check.sh gates all of it by filename.
// =============================================================================
#ifndef PB_NETWORKING_SESSION_H
#define PB_NETWORKING_SESSION_H

#include <stdint.h>
#include <stddef.h>

#include "../game/battle.h"
#include "../game/validate.h"
#include "protocol.h"
#include "transport.h"

// -----------------------------------------------------------------------------
//  CONSTANTS
// -----------------------------------------------------------------------------
#define PROTO_RETX_MS        1000u   // one rung of the ladder
#define PROTO_RETX_MAX          9u   // rungs before SE_LOST; 9 x 1 s = the plan's 9 s
#define SESSION_MAX_RX       1024u   // accepted frames, per session - a heuristic
#define SESSION_MAX_TX       4096u   // sent frames, per session - also what makes
                                     // the 16-bit seq unable to wrap
#define SESSION_MAX_REACK_PER_ROUND 8u   // the re-ack rule is a small amplifier;
                                         // this is its per-round leash
// HOW FAR AHEAD OF WHAT WE HAVE ACCEPTED A SEQUENCE NUMBER MAY BE. A frame
// beyond it is refused WITHOUT MOVING THE WINDOW, and the rule exists because
// of a hole a host test found: the window slides forward to whatever seq it is
// shown, so ONE INJECTED FRAME carrying seq 0x7000 pushed it past every number
// the honest peer would ever send and every real frame afterwards was dropped
// as stale - a permanent deafness bought with a single packet an off-path
// device can forge, since the session id travels in clear. An honest jump is
// the count of consecutive frames we missed: nine ladder rungs at up to three
// frames each, plus what is in flight, is under forty.
//
// IT BOUNDS THE BASELINE TOO, and that half was missing until the P4-C5
// follow-up. The rule above is about a jump from an ESTABLISHED last; the FIRST
// frame an endpoint accepted set that last to whatever it claimed, so the same
// one-packet deafness was still available BEFORE the peer had spoken - through
// a HELLO, which carries session 0 by definition and so needs no session id at
// all. An honest peer's first frame is seq 1, and the furthest it can get
// before it hears from us is its own ladder, which is under ten.

#define SESSION_SEQ_MAX_JUMP 64u

static_assert((uint32_t)SESSION_MAX_TX < 0xFFFFu,
              "the sequence window assumes a session cannot wrap a 16-bit seq");

// -----------------------------------------------------------------------------
//  STATES. One terminal state; WHY it ended is SessionEnd.reason.
// -----------------------------------------------------------------------------
enum SessionState : uint8_t {
  SS_IDLE = 0,   // no session; only HELLO is meaningful
  SS_HELLO,      // our HELLO is out, waiting for the peer's
  SS_CAPS,       // versions on the wire, waiting for the peer's
  SS_SESSION,    // SESSION_REQUEST / SESSION_ACCEPT: the band, the seed AND the
                 // OPERATION (SessionOp) - the last cheap moment to disagree
  SS_TEAM,       // TEAM_SUBMIT / TEAM_VALIDATION: the validator runs on BOTH sides
  SS_VERIFY,     // the round-1 agreement barrier: one open hash each
  SS_BATTLE,     // the lockstep (networking/battle_link.cpp)
  SS_ENDING,     // BATTLE_END exchanged; rewards commit HERE and nowhere else
  // P7-C4. APPENDED AFTER SS_ENDING AND NOT INSERTED NEXT TO SS_TEAM, although
  // that is where it belongs in the story: three places compare this enum with
  // `<` (`state < SS_SESSION`, `state < SS_TEAM`), so inserting a value in the
  // middle would silently change what "an earlier phase" means. The state table
  // in docs/protocol.md carries the reading order; this enum carries the
  // numbering, and the two are allowed to differ as long as one of them says so.
  SS_TRADE,      // TRADE_OFFER / READY / CONFIRM / COMMIT (networking/trade_link.cpp)
  SS_CLOSED,     // terminal
  SS_STATE_COUNT
};

// WHAT THE TWO DEVICES AGREED TO DO. Carried in ProtoSessionReq.rules - a byte
// that was reserved until P7-C4 - and echoed in ProtoSessionAcc.op_echo, so a
// mismatch is named (SD_OP) at the last moment before a Pebble is on the wire
// rather than discovered by one side sending a frame the other cannot place.
//
// A DEVICE RUNS ONE OPERATION PER SESSION. There is no mode switch inside a
// live session and there must not be: the consent the player gave on the LINK
// card (ui/screen_link.h) was consent to THIS operation.
enum SessionOp : uint8_t {
  SOP_BATTLE = 0,
  SOP_TRADE,
  SOP_COUNT
};

enum SessionRole : uint8_t { SR_UNSET = 0, SR_INITIATOR, SR_RESPONDER };

// WHY IT ENDED. SE_LOST IS NEVER SE_DESYNC: calling a lost radio a desync
// blames an engine for a transport.
enum SessionEndReason : uint8_t {
  SE_NONE = 0,
  SE_DONE,           // both sides agreed the outcome AND the final hash
  SE_LOST,           // the ladder expired; the obligation and round are recorded
  SE_DESYNC,         // two engines disagreed, or a peer lied about a hash - which
                     // are INDISTINGUISHABLE and the code does not pretend otherwise
  SE_REJECTED,       // a team or a band was refused; detail is the VReject
  SE_PROTOCOL,       // the peer did something the state table forbids
  SE_INCOMPATIBLE,   // CAPABILITIES disagreed; detail is the word
  SE_LOCAL_CANCEL,
  SE_REASON_COUNT
};

// SessionEnd.detail, READ ACCORDING TO reason: a SessionDetail for SE_PROTOCOL,
// SE_DESYNC and SE_LOST; a VReject for SE_REJECTED; a SessionCapWord for
// SE_INCOMPATIBLE. One byte, three readings, and the reason says which - the
// alternative is three bytes that are zero almost always.
enum SessionDetail : uint8_t {
  SD_NONE = 0,
  SD_SELF,             // the peer's device id is ours: a reflection, not a peer
  SD_BASIS_SKEW,       // every version word matched and the hash basis did not
  SD_TEAM_CHANGED,     // a second TEAM_SUBMIT with a different team_crc
  SD_ACTION_CHANGED,   // a second ACTION for one round with different bytes
  SD_OPEN_HASH,        // an ACTION whose open hash is not ours
  SD_INPUTS,           // ROUND_RESULT.hash_before differs: different action pair
  SD_RULES,            // hash_before agreed and hash_after did not: different rules
  SD_RESTATED,         // a second ROUND_RESULT for one round with other hashes
  SD_PROBE,            // a BATTLE_STATE for our round with another hash
  SD_ROUND_GAP,        // a frame more than one round away: INV-1 says impossible
  SD_SETUP,            // the round-1 open hashes disagreed
  SD_END_DISAGREE,     // BATTLE_END outcome or final hash disagreed
  SD_PEER_GOODBYE,     // the peer left before the operation finished
  SD_INTERNAL,         // battle_init() refused a team validate_team() accepted:
                       // a bug in game/validate.cpp, NEVER a peer capability
  SD_BO_ABORT,         // the engine itself aborted the round
  SD_ACTION_REJECT,    // the peer refused our ACTION by name
  SD_RX_BUDGET,        // SESSION_MAX_RX
  SD_TX_BUDGET,        // SESSION_MAX_TX
  SD_BAND,             // the level band was refused
  SD_VERDICT,          // SESSION_ACCEPT carried a nonzero verdict
  SD_OP,               // the two devices are here to do different things
  SD_TRADE_REFUSED,    // the peer's TradeReject on our offer: a legal Pebble it
                       // will not take. NOT a VReject, for game/taint.h's reason
  SD_TRADE_STORE,      // OUR OWN flash refused a write inside the trade journal.
                       // A local fault, named as one rather than blamed on the
                       // peer, and the journal is what finishes it at the next
                       // boot
  SD_DETAIL_COUNT
};

// Which CAPABILITIES word disagreed. Named one by one so an incompatible pair
// tells a player something better than "incompatible".
enum SessionCapWord : uint8_t {
  SCW_NONE = 0, SCW_WIRE_VER, SCW_TEAM_MAX, SCW_LEVEL_MAX,
  SCW_ENGINE_VER, SCW_HASH_VER, SCW_CONTENT_VER, SCW_PAYLOAD_MAX, SCW_HASH_BASIS,
  SCW_COUNT
};

// -----------------------------------------------------------------------------
//  THE ABORT RECORD - THE DELIVERABLE OF EVERY TERMINAL
//
//  Written on the way into SS_CLOSED and never after. It is what makes every
//  terminal answerable WITHOUT A RERUN: SE_LOST shows tx_retx == PROTO_RETX_MAX
//  with peer_hash 0 and names the frame and round it died on; SE_DESYNC shows
//  both hashes side by side; SE_REJECTED shows a VReject and a member index.
// -----------------------------------------------------------------------------
struct SessionEnd {
  uint32_t session;        //  0
  uint32_t local_hash;     //  4  our battle state hash at the end, or 0
  uint32_t peer_hash;      //  8  the peer's, when it sent one
  uint16_t rx_ok;          // 12  frames the FSM accepted
  uint16_t rx_dup;         // 14  the sequence window named them duplicates
  uint16_t rx_stale;       // 16  a round behind, or below the sequence window
  uint16_t rx_gap;         // 18  a seq above the expected one: something was lost
  uint16_t rx_wrong_state; // 20  legal, unexpected here (R3)
  uint16_t rx_reject;      // 22  the codec refused it (R1)
  uint16_t tx_frames;      // 24
  uint16_t tx_retx;        // 26  ladder rungs climbed on the LAST obligation
  uint16_t seq;            // 28  the seq of the last frame we sent
  uint16_t round;          // 30  the battle round at the end, or 0
  uint8_t  reason;         // 32  SessionEndReason
  uint8_t  state;          // 33  the SessionState it died in
  uint8_t  role;           // 34  SessionRole
  uint8_t  detail;         // 35  read per `reason` - see SessionDetail
  uint8_t  waiting_for;    // 36  the ProtoType we were owed, or 0
  uint8_t  outcome;        // 37  BattleOutcome, when there was a battle
  uint8_t  bad_index;      // 38  the refused team member, or 0xFF
  uint8_t  reserved;       // 39
};
static_assert(sizeof(SessionEnd) == 40, "SessionEnd layout drifted");
static_assert(sizeof(SessionEnd) == 4 * 3 + 2 * 9 + 1 * 6 + 4,
              "SessionEnd has a padding hole");

// -----------------------------------------------------------------------------
//  THE EVENT RING - caller-owned, exactly like BattleLog and for the same
//  reason: the device hands it 16 entries, a host test hands it thousands, and
//  neither needs the other's constant. `dropped` SATURATES.
// -----------------------------------------------------------------------------
enum LinkEventKind : uint8_t {
  LEK_NONE = 0, LEK_TX, LEK_RX, LEK_CODEC_REJECT, LEK_DUP, LEK_STALE,
  LEK_WRONG_STATE, LEK_STATE, LEK_REACK, LEK_RETX, LEK_END, LEK_KIND_COUNT
};

struct LinkEvent {     // 8 B
  uint8_t  kind;       // LinkEventKind
  uint8_t  type;       // ProtoType, or a SessionState for LEK_STATE
  uint8_t  state;      // SessionState at the time
  uint8_t  code;       // ProtoErr / VReject / SessionDetail, per kind
  uint16_t round;
  uint16_t seq;
};
static_assert(sizeof(LinkEvent) == 8, "LinkEvent layout drifted");
static_assert(sizeof(LinkEvent) == 1 + 1 + 1 + 1 + 2 + 2, "LinkEvent has a padding hole");

struct LinkLog {
  LinkEvent* ev;
  uint16_t   cap, head, count, dropped;
};
void             link_log_init(LinkLog& l, LinkEvent* buf, uint16_t cap);
const LinkEvent* link_log_at(const LinkLog& l, uint16_t i);

// -----------------------------------------------------------------------------
//  THE SESSION
//
//  It holds NO BattleState and NO BattleSetup of its own: both are caller-owned
//  and passed by pointer, and the peer's team is decoded STRAIGHT INTO
//  setup->member[peer_side][i]. So there is never a second source of truth for
//  a hashed team and never a second 780 B BattleSetup - the linked path borrows
//  the one ui/screen_battle.cpp already owns at file scope.
// -----------------------------------------------------------------------------
struct TradeLink;           // networking/trade_link.h, caller-owned like the rest

struct SessionCfg {
  const Transport* tp;
  BattleSetup*     setup;
  BattleState*     st;
  BattleLog*       blog;      // may be nullptr
  LinkLog*         llog;      // may be nullptr
  TradeLink*       tl;        // may be nullptr; REQUIRED when op is SOP_TRADE
  uint32_t         device_id; // the tie-break that fixes the roles
  uint32_t         nonce;     // from RNG_MISC ("tokens, nonces, PINs, canaries")
  uint8_t          op;        // SessionOp, agreed with the peer in SS_SESSION
  uint8_t          lvl_lo;    // the level band this device will accept, inclusive
  uint8_t          lvl_hi;
};

struct Session {
  // --- wiring, never owned here
  const Transport* tp;
  BattleSetup*     setup;
  BattleState*     st;
  BattleLog*       blog;
  LinkLog*         llog;
  TradeLink*       tl;

  // --- identity
  uint32_t device_id, peer_device_id;
  uint32_t id;                 // the session id, 0 until both HELLOs are in
  uint32_t nonce_local, nonce_peer;
  uint32_t hello_nonce_initiator;

  // --- role and state
  uint8_t  role, side, state, lvl_lo, lvl_hi;
  uint8_t  op;                 // SessionOp: what this session is FOR

  // --- the sequence window (see the banner)
  uint16_t tx_seq, rx_seq_last;
  uint32_t rx_seq_bits;
  uint8_t  rx_seq_seen;

  // --- the ladder
  uint32_t now_ms, due_ms;
  uint8_t  tries;
  uint8_t  waiting_for;        // the ProtoType we are owed, for the record

  // --- our team, frozen at session_set_team() and never re-read from the Box
  uint8_t  my_rec[BATTLE_TEAM_MAX][PBW_BYTES];
  uint8_t  my_count;
  uint16_t team_crc_local, team_crc_peer;
  uint8_t  team_seen, verdict_seen;
  uint8_t  verdict_local, verdict_peer, bad_index_local;

  // --- the lockstep
  uint32_t open_hash;          // this round's hash with both pendings clear
  uint8_t  barrier;            // 1 == we resolved a round the peer has not matched
  uint8_t  my_act_round, my_act_kind, my_act_index;   // regenerated on demand
  uint32_t my_act_hash;        // the open hash OF THAT ROUND, not of the current
                               // one: a retransmitted ACTION must carry the hash
                               // the peer will compare it against
  uint8_t  ar_round, ar_reject, ar_kind, ar_index;    // our last ACTION_RESULT
  uint32_t ar_hash;
  uint8_t  ar_ack_round;       // the last round whose ACTION_RESULT we accepted
                               // AS PROGRESS. Without it R4 was false for this
                               // one type: a peer replaying a single legal
                               // ACTION_RESULT reset the ladder every time and
                               // held the session open indefinitely.
  uint8_t  rr_round, rr_outcome;                      // our last ROUND_RESULT
  uint32_t rr_before, rr_after;
  uint8_t  peer_rr_round;                             // the peer's last, so a
  uint32_t peer_rr_before, peer_rr_after;             // RESTATED round is nameable
  uint8_t  reack_round, reack_count, hs_reack;
  uint32_t final_hash;
  uint8_t  final_outcome, peer_end_seen, peer_end_outcome;
  uint32_t peer_end_hash;
  uint8_t  paid;               // rewards authorised: SE_DONE and nothing else

  // --- budgets and the record
  uint16_t rx_budget, tx_budget;
  SessionEnd end;
};

// -----------------------------------------------------------------------------
//  API
// -----------------------------------------------------------------------------
void session_init(Session& s, const SessionCfg& cfg);

// FREEZES THE ONE PEBBLE THIS DEVICE IS OFFERING IN A TRADE. The trade twin of
// session_set_team(), and a SEPARATE function rather than a count of one,
// because validate_battle_ready() must NOT run here: hp_cur == 0 / PBS_FAINTED
// is a legal thing to have stored and therefore a legal thing to trade, and
// game/validate.h says exactly that about why the rule lives outside the shared
// body. Everything else is identical - the Pebble is encoded to the 48 B record
// and the record is the only copy the session ever reads again.
VReject session_set_trade(Session& s, const PebbleInstance& p);

// Freezes the local team: it is ENCODED to the wire here and decoded back
// through pbw_decode() when the battle is built, so the local team enters the
// engine through the SAME validator the peer's does and the two endpoints'
// BattleSetups are BYTE-IDENTICAL. Returns the validator's verdict; anything
// but VR_OK means this device may not offer this team.
VReject session_set_team(Session& s, const PebbleInstance* m, uint8_t count,
                         uint8_t& bad_index);

// Sends HELLO. Both endpoints may call it - the ROLES are decided by device id,
// not by who spoke first - and an endpoint that never calls it still answers a
// peer's HELLO from SS_IDLE.
void session_start(Session& s, uint32_t now_ms);

// Drains the transport, runs the FSM, then runs the ladder. Call it as often as
// the caller likes; `now_ms` is monotonic milliseconds from anywhere.
void session_poll(Session& s, uint32_t now_ms);

// The lockstep needs one local action for this round. The caller chooses it
// (game/battle_ai.cpp on the device); the session never chooses.
bool session_wants_action(const Session& s);
void session_submit_action(Session& s, BattleAction a, uint32_t now_ms);

// Ends the session politely. GOODBYE is sent when there is a session to leave.
void session_cancel(Session& s, uint32_t now_ms);

inline SessionState      session_state(const Session& s) { return (SessionState)s.state; }
inline bool              session_closed(const Session& s) { return s.state == (uint8_t)SS_CLOSED; }
inline const SessionEnd& session_end(const Session& s) { return s.end; }

// TRUE ONLY WHERE SessionEnd.reason IS SE_DONE, and that is STRUCTURAL rather
// than a habit of the callers: session_close() is the one place that decides a
// terminal, and a session that has authorised rewards closes SE_DONE there. It
// was NOT structural until the P4-C5 follow-up - on_battle_end() took the
// peer's own reason byte, so a peer that had already made us pay could then
// choose our label, and an exhausted rx budget could do it with no peer at all.
// The asymmetry of the two generals still falls on the side of NOT awarding -
// see the LIMITS section of docs/protocol.md.
//
// THE FLAG IS SET BY OUR OWN COMPARISON AND NEVER BY THE PEER'S WORD: our
// engine finished the battle, and the peer's BATTLE_END carried the same
// outcome AND the same final hash as ours.
//
// NO PRODUCT CODE READS THIS YET. Paying XP and writing the Box is P7's, the
// same way the radio is; nothing under app/, ui/ or persistence/ calls into
// this module at this commit.
inline bool session_rewards_authorised(const Session& s) { return s.paid != 0u; }

const char* session_state_name(SessionState st);
const char* session_reason_name(SessionEndReason r);
const char* session_detail_name(SessionDetail d);

// The two derivations, public so a host test can pin them and so both ends can
// be shown to compute them identically. FNV-1a 32 over the words, in order.
uint32_t session_derive_id(uint32_t id_lo, uint32_t id_hi, uint32_t hello_nonce);
uint32_t session_derive_seed(uint32_t session, uint32_t nonce_a, uint32_t nonce_b,
                             uint16_t crc_a, uint16_t crc_b);
uint16_t session_team_crc(const uint8_t rec[BATTLE_TEAM_MAX][PBW_BYTES]);

// -----------------------------------------------------------------------------
//  INTERNALS SHARED WITH networking/battle_link.cpp. They are declared here
//  rather than duplicated because the lockstep sends and closes exactly as the
//  handshake does; nothing outside these two translation units should call them.
// -----------------------------------------------------------------------------
void session_prepare(Session& s, ProtoMsg& m, ProtoType t, uint8_t round);
bool session_emit(Session& s, ProtoMsg& m);
void session_close(Session& s, SessionEndReason r, uint8_t detail);
void session_progress(Session& s);          // R4: the ONLY thing that resets the ladder
void session_note(Session& s, LinkEventKind k, uint8_t type, uint8_t code,
                  uint16_t round, uint16_t seq);
void session_goto(Session& s, SessionState to);

// A MESSAGE FROM AN EARLIER PHASE IS ANSWERED WITH THE REPLY WE ALREADY GAVE,
// regenerated from state, and it advances nothing. Returns true when it handled
// the message. WITHOUT THIS AN HONEST PAIR STRANDS ITSELF ON ONE LOST FRAME:
// measured before it existed, a link that only REORDERED - dropping nothing at
// all - ended 472 of 500 acceptance trials in SE_LOST, because a CAPABILITIES
// that overtook its HELLO was dropped as out-of-state and the peer then waited
// out its whole ladder for a frame that had already been delivered and
// discarded. It is the handshake half of the same rule battle_link.cpp applies
// to a round the peer is still driving.
bool session_reanswer_earlier_phase(Session& s, const ProtoMsg& m);

#endif  // PB_NETWORKING_SESSION_H
