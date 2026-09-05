// =============================================================================
//  PEBBLEBOL - networking/trade_link.h
//  THE TRADE HALF OF THE SESSION (spec section 16, plan P7-C4).
//
//  networking/session.cpp owns SS_IDLE..SS_SESSION and SS_CLOSED; this module
//  owns SS_TRADE, exactly as networking/battle_link.cpp owns SS_VERIFY,
//  SS_BATTLE and SS_ENDING. The split is in the same place and for the same
//  reason: everything above is about two devices agreeing to do something, and
//  everything here is about the thing itself.
//
//  THE FIVE WIRE STEPS, AND WHY THE JOURNAL ONLY HAS THREE PHASES:
//
//      OFFER     both sides put their 48 B record on the wire, UNPROMPTED and
//                before either has seen the other's - so no player can choose
//                what to give after seeing what is on offer
//      validate  each side decodes the other's through pbw_decode(), which IS
//                validate_pebble() (protocol.h), and then applies the POLICY
//                its own game layer owns
//      READY     each side reports its two verdicts: a VReject and a TradeReject
//      CONFIRM   each player presses A on their own device. THIS IS THE CONSENT
//                AND IT IS NOT NEGOTIABLE: nothing before it moves a Pebble
//      COMMIT    "I have applied it", and it IMPLIES its sender's CONFIRM
//
//  game/trade.h's PendingTrade collapses those five into three phases because
//  only three of them change what BOOT must do: nothing is at risk until our
//  offer is out (SENT), then until theirs is journalled (RECEIVED), then the
//  exchange is decided (COMMIT). The plan's "fault injection at each of the 5
//  phases" is about the wire steps; the journal's three are the ones a reboot
//  can tell apart.
//
// -----------------------------------------------------------------------------
//  COMMIT SUBSUMES CONFIRM, AND THAT IS THE ONE THING THAT NARROWS THE WINDOW
// -----------------------------------------------------------------------------
//  A device applies when it holds BOTH confirms. If A's CONFIRM is lost, B
//  never applies - so A's COMMIT is read by B as "A confirmed AND A applied",
//  and B applies on it. Losing the exchange now needs a CONFIRM and a COMMIT to
//  be lost together for a whole nine-rung ladder rather than one frame.
//
//  IT DOES NOT MAKE THE EXCHANGE ATOMIC ACROSS THE PAIR AND NOTHING CAN.
//  game/trade.h states the two-generals impossibility, says which side it falls
//  on, and points at the measurement tests/test_trade.cpp reports.
//
// -----------------------------------------------------------------------------
//  IT HOLDS NO STATE OF ITS OWN AND NO GAME AND NO PERSISTENCE
// -----------------------------------------------------------------------------
//  Every byte it reads and writes lives in the caller's Session and TradeLink,
//  so two endpoints run in one host process (tools/check.sh gates the absence
//  of file-scope mutables here as it does for session.cpp and battle_link.cpp).
//
//  AND IT TOUCHES NEITHER THE BOX NOR FLASH. The five things a trade needs from
//  the layers below - judge the peer's record, journal W1, journal W2, run the
//  W3..W4 apply, abandon the journal - arrive as a TradeHooks of function
//  pointers the app fills in with game/trade.cpp and
//  persistence/save_manager.cpp. That is what keeps this file inside
//  check.sh's PURE_NET list, and it is what lets a host test drive the whole
//  five-step exchange with a real codec and a real ladder over a lossy link.
//
//  See networking/session.h for the ladder, the sequence window and the four
//  rules that run before the state table.
// =============================================================================
#ifndef PB_NETWORKING_TRADE_LINK_H
#define PB_NETWORKING_TRADE_LINK_H

#include <stdint.h>

#include "../game/trade.h"        // TradeCodec: this module fills it in
#include "session.h"

// -----------------------------------------------------------------------------
//  WHERE THE EXCHANGE HAS GOT TO. For the screen, and for a test to assert on
//  something better than "the session is still open".
// -----------------------------------------------------------------------------
enum TradeLinkPhase : uint8_t {
  TLP_IDLE = 0,
  TLP_OFFERED,      // our record is out; theirs has not arrived
  TLP_REVIEW,       // both records are on the table; verdicts are in flight
  TLP_ASK_PLAYER,   // both sides said yes to the OBJECTS. Now the people decide
  TLP_WAIT_PEER,    // we confirmed; the peer has not
  TLP_APPLIED,      // the journal ran here: our Box already holds theirs
  TLP_DONE,         // and the peer told us it applied too
  TLP_REFUSED,      // a named refusal; nothing was journalled
  TLP_PHASE_COUNT
};

const char* trade_phase_name(TradeLinkPhase p);

// -----------------------------------------------------------------------------
//  THE WIRE HALF OF game/trade.h's TradeCodec, WHICH IS WHY IT LIVES HERE.
//
//  game/trade.cpp drives the journal and cannot include networking/protocol.h -
//  that header includes game/validate.h, so the include would cross the layer
//  line in the wrong direction. It takes two function pointers instead, and
//  this is the module that owns what the 48 bytes mean: pbw_decode() for the
//  read, and for the write the ONE operation the journal's idempotence rests
//  on - patching PBW_OFF_ID and resealing the record's own CRC over bytes
//  0..PBW_CRC_BYTES-1, so the patched record still decodes.
//
//  Returns a reference to a constant; there is no state behind it.
// -----------------------------------------------------------------------------
const TradeCodec& trade_wire_codec(void);

// -----------------------------------------------------------------------------
//  THE FIVE THINGS THIS MODULE CANNOT DO ITSELF.
//
//  `judge` answers 0 to accept and a game/trade.h TradeReject otherwise. It is
//  a uint8_t and not a TradeReject because networking/ does not include
//  game/trade.h: the code is carried, printed and put on the wire, and never
//  interpreted here.
//
//  The three journal hooks return whether the bytes LANDED. A false is a LOCAL
//  fault (SD_TRADE_STORE), never the peer's, and the session says so.
// -----------------------------------------------------------------------------
struct TradeHooks {
  bool    (*journal_sent)(void* ctx, uint32_t out_id, uint32_t peer_id);   // W1
  uint8_t (*judge)(void* ctx, const uint8_t rec[PBW_BYTES]);
  bool    (*journal_received)(void* ctx, const uint8_t rec[PBW_BYTES]);    // W2
  bool    (*commit)(void* ctx);                                            // W3..W4
  void    (*abort)(void* ctx);                                             // clear
  void*   ctx;
};

// -----------------------------------------------------------------------------
//  THE TRADE, caller-owned exactly as BattleState is. 112 B.
// -----------------------------------------------------------------------------
struct TradeLink {
  TradeHooks hooks;

  uint8_t  out_rec[PBW_BYTES];   // ours, frozen at session_set_trade()
  uint8_t  in_rec[PBW_BYTES];    // theirs, as received
  uint32_t out_id;               // the local id of the Pebble we are giving

  uint16_t in_crc;               // the identity of THEIR offer
  uint16_t pair_crc;             // the identity of THIS trade, canonical order

  uint8_t  have_in;              // an OFFER arrived
  uint8_t  in_verdict;           // OUR VReject on their record
  uint8_t  in_policy;            // OUR TradeReject on their record
  uint8_t  peer_ready;           // their READY arrived
  uint8_t  peer_verdict;         // THEIR VReject on ours
  uint8_t  peer_policy;          // THEIR TradeReject on ours
  uint8_t  local_accept;         // our player pressed A
  uint8_t  peer_accept;          // their CONFIRM (or COMMIT) arrived
  uint8_t  peer_commit;          // their COMMIT arrived
  uint8_t  applied;              // we ran the journal path
  uint8_t  phase;                // TradeLinkPhase
  uint8_t  reack;                // the re-answer leash, as the handshake has
};

// Wires the hooks and the outgoing record. Call it AFTER session_set_trade()
// and BEFORE session_start(); `out_id` is the local Pebble id being given, and
// it is what PendingTrade.out_id records.
void trade_link_init(TradeLink& tl, Session& s, uint32_t out_id,
                     const TradeHooks& hooks);

// SS_TRADE is entered here, from networking/session.cpp's enter_operation().
void trade_link_begin(Session& s);

// One decoded frame, in SS_TRADE.
void trade_link_on_msg(Session& s, const ProtoMsg& m);

// The ladder's regeneration for SS_TRADE. Everything it sends is rebuilt from
// the TradeLink; nothing is stored as bytes.
void trade_link_resend(Session& s);

// TRUE when both records are on the table, both sides validated both, and the
// only thing left is the local player. The screen shows the two Pebbles here.
bool trade_link_wants_consent(const Session& s);

// THE CONSENT. The ONE thing in this module a player does, and nothing moves
// without it. A second call is ignored.
void trade_link_accept(Session& s);

// TRUE only where THIS device applied the exchange AND the peer told us it
// applied too. The trade's twin of session_rewards_authorised(), and it has the
// same shape for the same reason: one expression, in one place.
bool trade_link_completed(const Session& s);

#endif  // PB_NETWORKING_TRADE_LINK_H
