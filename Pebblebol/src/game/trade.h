// =============================================================================
//  PEBBLEBOL - game/trade.h
//  THE TRADE RULES AND THE TRADE JOURNAL (spec section 16, plan P7-C4).
//
//  A trade is the only operation in this firmware that DESTROYS a Pebble on one
//  device while CREATING it on another, so it is the only one where a power cut
//  can leave the world holding two of something or none of it. Everything in
//  this header exists for that one sentence.
//
// -----------------------------------------------------------------------------
//  WHAT IS PROVED, AND WHAT IS ONLY BOUNDED. READ BOTH.
// -----------------------------------------------------------------------------
//  WITHIN ONE DEVICE, PROVED: at EVERY point at which the power can be cut -
//  and tests/test_trade.cpp sweeps every single flash write of the sequence,
//  not three chosen ones - the next boot leaves the Box holding either the
//  outgoing Pebble or the incoming one, never both and never neither, with the
//  Box internally valid. That is what the journal below buys and it is the
//  whole of P7-C4's deliverable.
//
//  ACROSS THE PAIR, NOT PROVED AND NOT PROVABLE: two parties over a lossy link
//  with no third party CANNOT make an exchange atomic. The last message of any
//  handshake is unacknowledged, and whichever side owns the commit decision at
//  that moment can commit while the other does not - initiator first and its
//  Pebble is destroyed while the peer's is duplicated, responder first and it is
//  the mirror image. There is no ordering that closes it. networking/session.h
//  already faces the same wall for battle rewards and answers it the only
//  honest way ("the asymmetry of the two generals still falls on the side of
//  NOT awarding"), and this module does the same three things instead of
//  pretending:
//
//    1. COMMIT SUBSUMES CONFIRM. A device applies when it holds BOTH confirms,
//       and a TRADE_COMMIT frame IMPLIES its sender's confirm - so the peer
//       that missed a CONFIRM still applies on the COMMIT that follows it. The
//       window therefore needs a CONFIRM and a COMMIT to be lost together for a
//       whole nine-rung ladder, not a single frame.
//    2. IT IS MEASURED, NOT ASSERTED. tests/test_trade.cpp runs the pair over
//       transport_loopback.cpp's drop/duplicate/reorder link and REPORTS the
//       count of trials that ended with the two Boxes disagreeing. The number
//       is in the test output.
//    3. THE RENDEZVOUS IS NOT BUILT HERE AND THIS SAYS SO. PendingTrade.peer_id
//       is written on every record precisely so a future chunk can compare two
//       journals at the next LINK with the same peer and let the side that did
//       not apply apply then. BOXF_TRADE_PENDING is the flag that half needs.
//       NEITHER IS USED YET: this chunk gives the flag no writer, because a
//       flag that duplicates a fact the journal already answers at boot is a
//       second source of truth, and a flag nothing can act on is a stuck bit.
//
// -----------------------------------------------------------------------------
//  THE JOURNAL, AND THE ONE GUARANTEE THE WHOLE DESIGN RESTS ON
// -----------------------------------------------------------------------------
//  persistence/kv_store.h:21-23: "a put is atomic per key at the NVS level".
//  A "tr" write therefore lands whole or not at all, and single_write()'s
//  read-back-and-compare turns a FAILED put into a false the caller must
//  honour. Everything below is built on those two sentences and on nothing else.
//
//      W1  tr{SENT,     out_id, peer_id, in_wire = 0}      our offer is out
//      W2  tr{RECEIVED, out_id, peer_id, in_wire = theirs} theirs is here
//      W3  tr{COMMIT,   out_id, peer_id, in_wire = theirs with OUR minted id}
//          -- no Box byte has moved yet --
//      B1  clear the outgoing slot          pair_write pb<sx>
//      B2  add the incoming instance        pair_write pb<sy>
//      B3  the Box header                   slot_mask, next_id_counter, active
//      W4  tr{IDLE}, then save_checkpoint_all()
//
//  B1 BEFORE B2, for two reasons: a full Box (10/10) has no room otherwise, and
//  the transient state then UNDER-counts rather than over-counts, which is the
//  safe direction this tree takes everywhere else.
//
//  WHAT BOOT DOES WITH EACH PHASE:
//    absent / CRC bad / IDLE   nothing. single_load() fails and gs.trade stays
//                              at its defaults.
//    SENT                      ROLL BACK - clear the journal. No Box byte was
//                              ever written and both sides keep their originals.
//    RECEIVED                  ROLL BACK - ditto. The peer's record was
//                              journalled and never filed.
//    COMMIT                    ROLL FORWARD, idempotently: two independent
//                              PRESENCE TESTS, each a no-op when already done.
//
//  THE ID IS MINTED BEFORE THE COMMIT RECORD IS WRITTEN, AND THAT IS WHAT MAKES
//  ROLL-FORWARD IDEMPOTENT. box_mint_id() is hash32(device_id, next_id_counter)
//  and the counter moves, so minting AFTER the record was written would produce
//  a DIFFERENT id on a replayed COMMIT and the resolver could not tell "already
//  done" from "not yet started". Instead the id is minted first, patched into
//  in_wire at PBW_OFF_ID and the 48 B record is resealed, so the journal names
//  the id the incoming Pebble WILL HAVE LOCALLY and both halves of the
//  completion become presence tests. PendingTrade.reserved is 2 bytes and
//  cannot hold a uint32_t, which is why the id goes in the record rather than
//  beside it.
//
//  THE ONE RESIDUAL, WRITTEN DOWN RATHER THAN IMPLIED: a "tr" record that is
//  written cleanly and then ROTS (a flipped bit at rest) fails blob_ok, so
//  single_load() leaves gs.trade at IDLE and a half-applied Box would never be
//  finished. The blob is single-key by design (save_schema.h section 7) and has
//  no seq, so pairing it is not a free change; the window is milliseconds wide.
//
// -----------------------------------------------------------------------------
//  TWO SEAMS, BECAUSE game/ MAY NOT REACH networking/ OR persistence/'s I/O
// -----------------------------------------------------------------------------
//  in_wire[48] is a NETWORKING format (networking/protocol.h's pbw_encode), and
//  networking/protocol.h includes game/validate.h - so game/trade.cpp including
//  it would be a cycle across the layer line. It takes a TradeCodec of two
//  function pointers instead, which networking/trade_link.cpp fills in.
//
//  The WRITE ORDER above is the whole property, so this module cannot hand the
//  caller a list of dirty blobs and hope: it drives the writes itself, through
//  a TradeStore of four function pointers that the app fills in with the REAL
//  persistence/save_manager.cpp. That is what lets tests/test_trade.cpp cut the
//  power at flash write number k for every k the sequence performs.
//
//  PURE MODULE. stdint, string.h, the save schema, game/box.h, game/taint.h and
//  game/validate.h. No Arduino, no networking header, no kv_store, no heap, no
//  float, no clock (now_epoch is a parameter), no RNG.
// =============================================================================
#ifndef PB_GAME_TRADE_H
#define PB_GAME_TRADE_H

#include <stdint.h>

#include "../persistence/save_schema.h"    // PendingTrade, TR_WIRE_BYTES
#include "validate.h"                      // VReject

// -----------------------------------------------------------------------------
//  WHY A TRADE WAS REFUSED. NEVER A BOOL, for game/battle.h's reason.
//
//  ITS OWN ENUM AND NOT A 28th VReject: a policy refusal is not a validity
//  verdict (game/taint.h and game/breeding.h say the same), and every Pebble
//  named by every code below is a perfectly legal object.
//
//  THE ORDER IS THE EVALUATION ORDER and tests/test_trade.cpp pins the exact
//  code for every case.
// -----------------------------------------------------------------------------
enum TradeReject : uint8_t {
  TDR_OK = 0,
  TDR_NO_BOX,            // box_bind() has not run
  TDR_NO_SLOT,           // the slot is out of range or empty
  TDR_ACTIVE,            // THE PEBBLE YOU ARE HOLDING. See the note below.
  TDR_QUARANTINED,       // save_manager.h: "may be shown to its owner. It may
                         // NOT enter a battle or a trade" - P7's obligation,
                         // and this is where it is discharged
  TDR_INVALID,           // validate_pebble() refused our OWN outgoing Pebble
  // THERE IS NO TDR_LAST_PEBBLE AND THIS IS WHERE THAT IS WRITTEN DOWN. "The
  // last Pebble on the device cannot be given away" is a real rule and it is
  // ALREADY HELD, by TDR_ACTIVE plus game/box.h invariant B3: box_active()
  // runs mask_sync(), which repairs an active_slot that points nowhere to the
  // lowest occupied slot, so in a Box with exactly one Pebble that Pebble IS
  // the active one and TDR_ACTIVE is the answer. A separate code for it was
  // written, measured to be unreachable, and removed - a named reject nothing
  // can return is a name, not a rule.
  TDR_SELF,              // the peer's device id is ours: a reflection
  TDR_PEER_INVALID,      // the incoming record did not decode
  TDR_TAINT,             // game/taint.h: a clean unit refuses a tainted one
  TDR_DUPLICATE_ID,      // the incoming Pebble carries an id we already hold
  TDR_PHASE,             // the journal is not in the phase this call needs
  TDR_STORE,             // a flash write did not land: NOTHING was applied
  TDR_LOST,              // a COMMIT record whose in_wire will not decode, with
                         // the outgoing Pebble already gone. Unreachable while
                         // the record is written by this module and the CRC
                         // holds; named because the resolver's input is a
                         // PERSISTED blob and silence would be the wrong answer
  TDR_REJECT_COUNT
};

const char* trade_reject_name(TradeReject r);

// -----------------------------------------------------------------------------
//  WHAT THE BOOT RESOLVER DECIDED. Its own three-valued answer, so a caller can
//  report "a trade was rolled back" without re-deriving the phase byte.
// -----------------------------------------------------------------------------
enum TradeResolution : uint8_t {
  TRS_NONE = 0,          // no journal, or an IDLE one: nothing happened
  TRS_ROLLED_BACK,       // a record below COMMIT: both sides keep their own
  TRS_COMPLETED,         // a COMMIT record: the exchange was finished
  TRS_FAILED,            // the store refused a write; the journal is untouched
                         // and the next boot tries again
  TRS_LOST,              // a COMMIT record whose in_wire will not decode, with
                         // the outgoing Pebble ALREADY GONE. One Pebble has
                         // been lost and no code in this file can bring it
                         // back, so the journal is cleared and the player is
                         // told - which is the only honest thing left. It is
                         // NOT TRS_ROLLED_BACK: nothing was rolled back.
  TRS_COUNT
};

const char* trade_resolution_name(TradeResolution r);

// -----------------------------------------------------------------------------
//  THE WIRE SEAM. networking/trade_link.cpp fills this in over pbw_decode()
//  and pbw_encode()'s own CRC; nothing in game/ knows what the 48 bytes mean.
// -----------------------------------------------------------------------------
struct TradeCodec {
  // The 48 B record -> a complete, VALIDATED PebbleInstance. VR_OK or nothing:
  // `out` is untouched on every reject.
  VReject (*decode)(const uint8_t rec[TR_WIRE_BYTES], PebbleInstance& out);
  // Patches the id field of the record IN PLACE and reseals its own CRC, so the
  // patched record still decodes. This is the idempotence hinge (see banner).
  void    (*set_id)(uint8_t rec[TR_WIRE_BYTES], uint32_t id);
};

// -----------------------------------------------------------------------------
//  THE STORE SEAM. The app fills this in with persistence/save_manager.cpp.
//  Every function returns whether the bytes LANDED (save_manager verifies its
//  own writes by reading them back), because a write that did not land must
//  stop the sequence rather than be assumed.
// -----------------------------------------------------------------------------
struct TradeStore {
  bool (*write_journal)(void* ctx, const PendingTrade& t);
  bool (*write_slot)(void* ctx, uint8_t slot);   // persists gs.pebbles[slot]
  bool (*write_box)(void* ctx);                  // persists gs.box
  void (*checkpoint)(void* ctx);                 // save_checkpoint_all()
  void* ctx;
};

// -----------------------------------------------------------------------------
//  1. MAY I OFFER THIS ONE?
//
//  `quarantine_mask` is persistence/save_manager.h's save_quarantine_mask().
//  It is a PARAMETER and not an include: the RULE is a game rule and belongs
//  here, the DATUM belongs to the load path, and game/ linking the save manager
//  would drag kv_store into every binary that touches a Pebble.
//
//  TDR_ACTIVE IS A REAL PRODUCT RULE AND NOT A SHORTCUT. game/box.h invariant
//  B4 makes box_release() refuse the active slot outright, so trading the
//  Pebble you are holding would mean this module moving box.active_slot and
//  game/sim.cpp's binding out from under a running simulation - at boot, from
//  the resolver, with no screen in front of it. Refusing is one sentence the
//  player can act on ("guarda este Pebble primero"). THE RESOLVER STILL HANDLES
//  IT ANYWAY, because its input is a persisted blob and a blob is not a
//  promise.
// -----------------------------------------------------------------------------
TradeReject trade_offer_check(uint8_t slot, uint16_t quarantine_mask);

// -----------------------------------------------------------------------------
//  2. MAY I ACCEPT THAT ONE?  `incoming` has already been through the codec, so
//  validate_pebble() has run on it; what is left is the POLICY.
// -----------------------------------------------------------------------------
TradeReject trade_accept_check(const PebbleInstance& outgoing,
                               const PebbleInstance& incoming,
                               uint32_t local_device_id, uint32_t peer_device_id);

// -----------------------------------------------------------------------------
//  3. THE JOURNAL. Pure transforms on a PendingTrade; none of them writes.
// -----------------------------------------------------------------------------
void trade_journal_idle(PendingTrade& t);
void trade_journal_sent(PendingTrade& t, uint32_t out_id, uint32_t peer_id);
void trade_journal_received(PendingTrade& t, const uint8_t in_wire[TR_WIRE_BYTES]);

// TRUE when `t` is a record this device wrote and can act on: the magic, the
// version and the phase. It does NOT check the CRC - persistence owns that, and
// by the time a record reaches here single_load() has already refused a rotten
// one.
bool trade_journal_live(const PendingTrade& t);

// -----------------------------------------------------------------------------
//  4. THE TWO ENTRY POINTS
// -----------------------------------------------------------------------------
// THE LIVE PATH. Called once, by the trade driver, when BOTH sides have
// confirmed and the journal is at TRADE_RECEIVED. Mints the local id, patches
// and reseals the record, writes W3, applies B1..B3, writes W4 and checkpoints
// - IN THAT ORDER, which is the property.
//
// On TDR_STORE nothing beyond the write that failed has been attempted, and
// whatever is on flash is a state the resolver below can finish or undo.
TradeReject trade_execute(PendingTrade& t, const TradeCodec& codec,
                          const TradeStore& store, uint32_t now_epoch);

// THE BOOT PATH. Called once, right after save_load_all(), with whatever the
// journal held. Idempotent: running it twice over one COMMIT record changes
// nothing the second time, which is exactly what a replayed power cut is.
TradeResolution trade_resolve(PendingTrade& t, const TradeCodec& codec,
                              const TradeStore& store, uint32_t now_epoch);

// The last reject the two entry points produced, for the caller's log and for
// the tests. TDR_OK when the last call succeeded.
TradeReject trade_last_reject(void);

#endif  // PB_GAME_TRADE_H
