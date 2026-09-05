// =============================================================================
//  PEBBLEBOL - networking/protocol.h
//  THE SPEC SECTION 15 FRAME AND THE 48 B WIRE PEBBLE (plan P4-C5).
//
//  Spec section 15 says every packet must contain a protocol version, a message
//  type, a session identifier, a payload length, an integrity check and a
//  sequence number where appropriate. All six are below, at fixed offsets, and
//  the message set is section 15's twelve names in section 15's order.
//
//  core/version.h already reserves byte 0 of every radio frame for this header;
//  PROTO_VERSION is static_asserted equal to PROTOCOL_VERSION so the two can
//  never drift.
//
// -----------------------------------------------------------------------------
//  EVERY MULTI-BYTE FIELD IS WRITTEN AND READ BYTE BY BYTE WITH EXPLICIT SHIFTS
// -----------------------------------------------------------------------------
//  Never a struct memcpy, and the contrast is deliberate: game/battle.h's
//  battle_state_hash() IS a memory-image hash and is documented as valid only
//  between little-endian builds of the same struct. This codec is the layer
//  that must not inherit that, because a host test over a memcpy codec proves
//  nothing about the device. tests/test_protocol.cpp pins one known frame and
//  one known 48 B record as GOLDEN BYTE ARRAYS, which is what catches an
//  endianness or padding shortcut directly - a grep could not.
//
// -----------------------------------------------------------------------------
//  THE CRC IS LAST, NOT IN THE HEADER, AND THE REASON IS MECHANICAL
// -----------------------------------------------------------------------------
//  core/crc16.h's crc16_ccitt(p, n) is ONE-SHOT with no continuation seed. A
//  header-embedded CRC splits the covered span into two runs and would need
//  either a scratch copy of the frame or a new core API. Trailing gives one
//  call over exactly the bytes received, puts the HEADER INSIDE the span - an
//  attacker cannot flip `type` or `session` for free - and matches this tree's
//  own convention exactly: PebbleInstance.crc16 at 126 over 0..125,
//  PendingTrade at 62 over 0..61, Genome at 14..15 over 0..13.
//
//  AND IT IS AN ERROR DETECTOR, NOT AN AUTHENTICATOR. crc16_ccitt is the same
//  function that seals the save blobs; any peer computes it over any bytes. It
//  defends against a flipped bit on the air and against nothing else. The
//  session id travels in clear in every frame, so it is a demultiplexing key
//  and not a secret.
//
// -----------------------------------------------------------------------------
//  PROTO_PAYLOAD_MAX IS DERIVED, NOT CHOSEN
// -----------------------------------------------------------------------------
//  It is the maximum of PROTO_LEN_OF[], computed at compile time. No message in
//  this protocol has a variable length, so a cap picked by hand at (say) 200
//  would reserve 52 bytes of attack surface for messages that do not exist.
//  Deriving it means the cap can never be looser than the protocol needs, and
//  the ESP-NOW ceiling is asserted against the derived number rather than hoped
//  for.
//
// -----------------------------------------------------------------------------
//  NOT FORWARD COMPATIBLE, AND IT DOES NOT PRETEND TO BE
// -----------------------------------------------------------------------------
//  Any type outside 1..16, any version other than PROTO_VERSION, any length
//  other than its type's exact length and any nonzero reserved byte is REFUSED.
//  There is no reserved-field forward channel and no downgrade path: a version
//  mismatch is a refusal, not a negotiation.
//
//  P7-C4 ADDED FOUR TYPES AND BUMPED THE VERSION TO 2, AND THE SENTENCE THAT
//  USED TO SIT HERE WAS WRONG. It said "P7's trade messages will exceed
//  PROTO_PAYLOAD_MAX and the answer must be a VERSION BUMP": a TRADE_OFFER is
//  one 48 B record plus four bytes = 52, comfortably under the 148 TEAM_SUBMIT
//  already costs, and PROTO_PAYLOAD_MAX did not move. The real constraint was
//  the CLOSED TYPE SPACE, not the length cap - the answer was the same bump for
//  a different reason, and the reason is worth having right.
//
// -----------------------------------------------------------------------------
//  WHERE THE LINE BETWEEN THIS LAYER AND THE SESSION LAYER IS DRAWN
// -----------------------------------------------------------------------------
//  This codec owns FRAMING (bounds, length, CRC), SHAPE (the exact payload
//  length of each type, every reserved byte zero) and the one INDEXING
//  PRECONDITION a payload carries (TEAM_SUBMIT.count, because slots are read
//  with it). It deliberately does NOT range-check semantic fields such as
//  TEAM_VALIDATION.bad_index or BATTLE_END.reason: those are the session FSM's
//  to interpret, they have no memory-safety consequence here, and a check with
//  no consequence is a guard nobody can make fail.
//
// -----------------------------------------------------------------------------
//  PURE MODULE. stdint, core/crc16.h, core/version.h, the save schema, the
//  content tables and game/validate.h. NO Arduino.h, no esp_now.h, no WiFi.h,
//  no renderer, no heap, no float, no clock, no RNG, no file-scope mutable in
//  protocol.cpp - which is what lets one process run two endpoints.
//  tools/check.sh gates all of that by FILENAME, because networking/ble_social.cpp
//  legitimately includes Arduino.h and the BLE headers.
// =============================================================================
#ifndef PB_NETWORKING_PROTOCOL_H
#define PB_NETWORKING_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#include "../core/version.h"               // PROTOCOL_VERSION
#include "../data/balance.h"               // BATTLE_TEAM_MAX, BATTLE_MAX_ROUNDS
#include "../game/validate.h"              // VReject
#include "../persistence/save_schema.h"    // PebbleInstance, TR_WIRE_BYTES

// -----------------------------------------------------------------------------
//  1. THE FRAME
// -----------------------------------------------------------------------------
#define PROTO_VERSION       2u
static_assert((int)PROTO_VERSION == (int)PROTOCOL_VERSION,
              "networking/protocol.h and core/version.h disagree about byte 0 of "
              "every radio frame");

#define PROTO_HDR_BYTES     14u
#define PROTO_CRC_BYTES      2u

// ProtoMsg.flags. PF_RETX says "this is a retransmission" and is DIAGNOSTIC
// ONLY: nothing in this codec or in the session layer may branch on it. Every
// other bit is reserved and must be zero on ingest.
#define PF_RETX             0x01u
#define PF_MASK             0x01u

// Spec section 15's twelve names, in spec section 15's order. 0 IS RESERVED AND
// NEVER VALID, which is what stops an all-zero buffer from being a frame.
enum ProtoType : uint8_t {
  PT_HELLO = 1,
  PT_CAPABILITIES,
  PT_SESSION_REQUEST,
  PT_SESSION_ACCEPT,
  PT_TEAM_SUBMIT,
  PT_TEAM_VALIDATION,
  PT_BATTLE_STATE,
  PT_ACTION,
  PT_ACTION_RESULT,
  PT_ROUND_RESULT,
  PT_BATTLE_END,
  PT_GOODBYE,
  // --- P7-C4, THE TRADE (spec section 16). Appended after section 15's twelve
  //     rather than inserted among them, so every existing type keeps its
  //     number and the golden frames of the first twelve move only by their
  //     version byte.
  PT_TRADE_OFFER,         // 13  the 48 B wire Pebble this device is giving
  PT_TRADE_READY,         // 14  "I decoded and validated yours": a VReject
  PT_TRADE_CONFIRM,       // 15  "my player pressed A on this exact pair"
  PT_TRADE_COMMIT,        // 16  "I have applied it" - and it IMPLIES my CONFIRM
  PT_TYPE_COUNT           // one past the last legal type, never a type itself
};
#define PROTO_TYPE_MAX  ((uint8_t)(PT_TYPE_COUNT - 1))

// The 48 B wire Pebble - see section 3 below for why 48 and not 64.
#define PBW_BYTES  48u

// THE EXACT PAYLOAD LENGTH OF EVERY TYPE, indexed by ProtoType. Index 0 is the
// reserved type and carries 0 so a lookup is total. Nothing here is variable
// length, which is what makes the decoder's length rule an equality.
inline constexpr uint16_t PROTO_LEN_OF[(size_t)PT_TYPE_COUNT] = {
  0,                       // 0  reserved, never a type
  12,                      // 1  HELLO
  16,                      // 2  CAPABILITIES
  12,                      // 3  SESSION_REQUEST
  12,                      // 4  SESSION_ACCEPT
  4 + 3 * PBW_BYTES,       // 5  TEAM_SUBMIT   (148)
  8,                       // 6  TEAM_VALIDATION
  12,                      // 7  BATTLE_STATE
  8,                       // 8  ACTION
  8,                       // 9  ACTION_RESULT
  12,                      // 10 ROUND_RESULT
  16,                      // 11 BATTLE_END
  4,                       // 12 GOODBYE
  4 + PBW_BYTES,           // 13 TRADE_OFFER   (52)
  4,                       // 14 TRADE_READY
  4,                       // 15 TRADE_CONFIRM
  4                        // 16 TRADE_COMMIT
};

// Which types carry a ROUND in the header. Every other type must send 0 there:
// a per-type rule in the same table as the length, so the two cannot drift.
inline constexpr bool PROTO_HAS_ROUND[(size_t)PT_TYPE_COUNT] = {
  false, false, false, false, false, false, false,
  true,                    // 7  BATTLE_STATE
  true,                    // 8  ACTION
  true,                    // 9  ACTION_RESULT
  true,                    // 10 ROUND_RESULT
  false, false,
  // A trade has no rounds. The four below must send 0 there, exactly as the
  // handshake types do, and proto_decode() refuses any other value at step 9.
  false, false, false, false
};

// DERIVED, never chosen. See the banner.
constexpr uint16_t proto_widest_payload(void)
{
  uint16_t m = 0;
  for (size_t t = 1; t < (size_t)PT_TYPE_COUNT; ++t)
    if (PROTO_LEN_OF[t] > m) m = PROTO_LEN_OF[t];
  return m;
}
inline constexpr uint16_t PROTO_PAYLOAD_MAX = proto_widest_payload();
static_assert(PROTO_PAYLOAD_MAX == 148,
              "PROTO_PAYLOAD_MAX is the widest message and TEAM_SUBMIT is it");

// A frame with an empty payload. No type has length 0, so step 8 of the decode
// order refuses one anyway - the floor exists so step 1 can stay free of any
// knowledge of types.
inline constexpr size_t PROTO_FRAME_MIN = (size_t)PROTO_HDR_BYTES + PROTO_CRC_BYTES;
inline constexpr size_t PROTO_FRAME_MAX =
    (size_t)PROTO_HDR_BYTES + PROTO_PAYLOAD_MAX + PROTO_CRC_BYTES;
static_assert(PROTO_FRAME_MAX <= 250,
              "a frame must fit one ESP-NOW datagram (250 B) - P7 depends on it");

// -----------------------------------------------------------------------------
//  2. WHY A FRAME WAS REFUSED
//
//  A SEPARATE ENUM FROM VReject ON PURPOSE: one enum in which half the values
//  are unreachable from the function under test makes a `code < COUNT` sweep
//  weaker than it looks.
// -----------------------------------------------------------------------------
enum ProtoErr : uint8_t {
  PE_OK = 0,
  PE_SHORT,          // the transport delivered fewer than PROTO_FRAME_MIN bytes
  PE_OVERSIZE,       // more than PROTO_FRAME_MAX, or len > PROTO_PAYLOAD_MAX
  PE_LEN,            // hdr + len + crc != the delivered byte count, EXACTLY
  PE_CRC,
  PE_VERSION,
  PE_TYPE,           // 0, or above PROTO_TYPE_MAX
  PE_FLAGS,          // a reserved flag bit was set
  PE_LEN_FOR_TYPE,   // a legal length, but not THIS type's length
  PE_ROUND,          // a round where the type forbids one, or out of 1..60
  PE_SESSION,        // not our session; or nonzero on HELLO, which predates one
  PE_RESERVED,       // a reserved payload field carried a value
  PE_COUNT_RANGE,    // TEAM_SUBMIT.count outside 1..BATTLE_TEAM_MAX
  PE_TEAM_PAD,       // a TEAM_SUBMIT slot at or above count was not all zero
  PE_CODEC_COUNT
};

const char* proto_err_name(ProtoErr e);

// -----------------------------------------------------------------------------
//  3. THE WIRE PEBBLE - 48 BYTES, AND 64 IS NOT AVAILABLE
//
//  TR_WIRE_BYTES is 48 and it lives inside PendingTrade, a PERSISTED 64 B blob
//  pinned by four static_asserts (save_schema.h:397-400). save_schema.h's own
//  rule is that a persisted layout is never edited in place, so a 64 B wire
//  record would either not fit the trade journal or would force a
//  SAVE_SCHEMA_VERSION bump plus a migration for a blob whose whole purpose is
//  surviving a power cut. Spec section 15's "the same validator" sentence makes
//  the battle record and the trade record ONE object; choosing anything but 48
//  now means two wire forms across time, which is what that sentence exists to
//  prevent. The assert below is the enforcement.
//
//  WHAT IS NOT TRANSMITTED, AND WHY IT IS A DELETION RATHER THAN A BUDGET CUT:
//    * nickname[13]. The one deliberate FEATURE loss, and the reason is memory
//      safety: ui/screen_battle.cpp:156 hands it out as a bare `const char*` and
//      ui/pet_view.cpp:150 snprintf's "%s" from it - both cap the OUTPUT at 13
//      and read the SOURCE to NUL. Nothing in src/ writes a nickname today, so
//      a peer would be the first producer of an unterminated one: thirteen
//      attacker-controlled bytes reading forward into the next member. Not
//      transmitting it DELETES the class instead of guarding it, and after this
//      every wire field is a bounded integer - there is no string parsing on
//      the ingest path at all. It also does not fit: 9 spare bytes against 13.
//    * evo_state. It is EXACTLY derivable, not merely mostly: the stage bits are
//      species_get(id)->stage and the PENDING bit is evolution_level_ready(),
//      a pure function of species_id and level. The decoder computes both from
//      the RECEIVER's tables, so spec section 15's "invalid evolution state" is
//      satisfied by INEXPRESSIBILITY - a peer cannot lie about a field it
//      cannot send. ONE BEHAVIOURAL CONSEQUENCE, STATED: a Pebble may arrive
//      with PENDING set where the sender had it clear, because the receiver's
//      own rules decided so. Always clearing it instead would trap a level-30
//      Pebble that has never evolved, since game/xp.cpp only raises the bit on
//      a level-up.
//    * care[5], care_rem[5], the four epochs, all five lifetime counters,
//      creation_seed, custom_sprite and the save bookkeeping (magic,
//      layout_ver, seq, crc16, reserved[12]). None of them reaches BattleState.
//      The fields the peer did not send are not unknown; they are the
//      RECEIVER'S.
//
//  P7 IS PRE-COMMITTED BY THIS AND CANNOT UNDO IT CHEAPLY: a traded Pebble will
//  arrive unnamed (the species name speaks instead), at zero care, zero age and
//  zero counters, with evo_state recomputed locally, and with PBF_CUSTOM /
//  PBF_HAS_CUSTOM_SPRITE refused. The record is full at 48 B with nine spare
//  bytes and a nickname needs thirteen, so reversing any of it needs a SEPARATE
//  MESSAGE, not a wider record.
// -----------------------------------------------------------------------------
static_assert((int)PBW_BYTES == (int)TR_WIRE_BYTES,
              "the wire Pebble must fit the frozen PendingTrade.in_wire");

// NOT PEBBLE_MAGIC: a 48 B wire record and a 128 B save blob must not answer to
// one magic, or a record pasted into the wrong slot would look plausible.
#define PBW_MAGIC        0x5750u    // bytes 'P','W' little-endian
// Its OWN number. Not PROTOCOL_VERSION (the frame owns that) and not
// SAVE_SCHEMA_VERSION (the blob owns that): the record's shape can move without
// either.
#define PBW_LAYOUT_VER   1u

#define PBW_OFF_MAGIC      0u   // 2
#define PBW_OFF_WIRE_VER   2u   // 1
#define PBW_OFF_SPECIES    3u   // 1
#define PBW_OFF_ID         4u   // 4
#define PBW_OFF_HP_CUR     8u   // 2
#define PBW_OFF_XP        10u   // 2
#define PBW_OFF_LEVEL     12u   // 1
#define PBW_OFF_RESERVED0 13u   // 1   where evo_state would have gone
#define PBW_OFF_STATUS    14u   // 1
#define PBW_OFF_FLAGS     15u   // 1
#define PBW_OFF_ORIGIN    16u   // 1
#define PBW_OFF_TRAIT     17u   // 1
#define PBW_OFF_MOVES     18u   // 4
#define PBW_OFF_GENOME    22u   // 16, field by field, never a struct copy
#define PBW_OFF_RESERVED  38u   // 8
#define PBW_OFF_CRC       46u   // 2, over bytes 0..45
#define PBW_CRC_BYTES     46u
static_assert(PBW_OFF_CRC + 2u == PBW_BYTES, "the wire Pebble's CRC is not last");
static_assert(PBW_CRC_BYTES == PBW_BYTES - 2u, "the wire Pebble's CRC span drifted");

// The status bits the WIRE may carry. Narrower than the stored mask on purpose,
// and the narrowing is owned HERE rather than by a flag inside validate_pebble():
//   * PBS_FAINTED and hp_cur == 0 are a legitimate STORED state and an illegal
//     one to hand a battle.
//   * PBS_CORRUPTED is EVOC_CORRUPTED's input, so a forged bit would hand the
//     receiver a free evolution condition on P7's trade path.
//   * PBS_RESERVED_LIGHT is a dead bit an old save may carry and a peer may not.
#define PBW_STATUS_MASK  ((uint8_t)(PBS_SICK | PBS_ASLEEP))

// Writes the 48 B record. `p` is assumed already valid - this is the SEND side
// and a device only ever encodes its own Pebbles. Total: it writes every one of
// the 48 bytes on every call, so no caller can leak a stale byte.
void pbw_encode(const PebbleInstance& p, uint8_t rec[PBW_BYTES]);

// Reads one. THE DECODER PRODUCES A COMPLETE PebbleInstance, NOT A WIRE OBJECT:
// it fills a LOCAL, derives evo_state from the receiver's own tables, stamps
// PEBBLE_MAGIC/PEBBLE_LAYOUT_VER, and runs validate_pebble() - then copies to
// `out` ONLY on VR_OK, memsetting `out` on every reject. There is therefore no
// window in which a caller holds a half-trusted instance.
//
// `out.crc16` IS LEFT ZERO ON PURPOSE. The save seal belongs to
// persistence/save_manager.cpp's pebble_seal() and this module does not link the
// persistence layer; whoever stores a received Pebble seals it there. The wire
// record's own CRC is this layer's integrity check and it has already been
// applied by the time out is written.
//
// THE validate_pebble() CALL IS REDUNDANT ON THE BATTLE PATH AND MUST STAY.
// Measured: removing it leaves tests/test_session.cpp entirely green, because
// networking/session.cpp re-runs validate_team() over the decoded members and
// catches the same vectors one layer up (test_protocol goes red, which is the
// point of having both). It is not redundant for a caller that decodes ONE
// record without a team around it - P7's trade path is exactly that - so the
// duplication is defence in depth on one path and the only check on another.
VReject pbw_decode(const uint8_t rec[PBW_BYTES], PebbleInstance& out);

// -----------------------------------------------------------------------------
//  4. THE TWELVE PAYLOADS, DECODED
//
//  Host structs, not wire images: the wire layout is the byte loops in
//  protocol.cpp and nothing here is memcpy'd anywhere.
//
//  TEAM_SUBMIT KEEPS ITS THREE RECORDS RAW. Decoding them here would put three
//  128 B PebbleInstances inside every ProtoMsg; instead the session layer calls
//  pbw_decode() straight into its own BattleSetup.member[peer_side][i], so there
//  is never a second source of truth for a hashed team and never a second 780 B
//  BattleSetup.
// -----------------------------------------------------------------------------
struct ProtoHello        { uint32_t device_id; uint32_t hello_nonce; };
struct ProtoCaps         { uint16_t engine_ver, hash_ver, content_ver, max_payload;
                           uint32_t hash_basis;
                           uint8_t  wire_ver, team_max, level_max; };
struct ProtoSessionReq   { uint32_t nonce_a; uint8_t team_count, lvl_lo, lvl_hi, rules; };
// `op_echo` (P7-C4) is the responder's answer to ProtoSessionReq.rules: the
// operation it will actually run. On an accept it equals what was asked; on a
// refusal it is the responder's OWN operation, which is what lets the initiator
// name SD_OP instead of reading a refusal out of a VReject field that has no
// code for "we are here to do different things".
struct ProtoSessionAcc   { uint32_t nonce_b;
                           uint8_t  verdict, team_count, lvl_lo, lvl_hi, op_echo; };
struct ProtoTeamSubmit   { uint16_t team_crc; uint8_t count;
                           uint8_t  rec[BATTLE_TEAM_MAX][PBW_BYTES]; };
struct ProtoTeamValid    { uint16_t team_crc_echo; uint8_t verdict, bad_index; };
struct ProtoBattleState  { uint32_t open_hash; uint8_t phase, outcome; };
struct ProtoAction       { uint32_t open_hash; uint8_t kind, index; };
struct ProtoActionResult { uint32_t open_hash; uint8_t reject, kind_echo, index_echo; };
// ProtoRoundResult.outcome IS CARRIED AND DELIBERATELY NEVER COMPARED, the same
// way `ack` is. It is the sender's BattleOutcome for the round, and
// battle_state_hash() covers the whole 212 B state INCLUDING st.outcome - so
// hash_after already decides the same fact, and a comparison of this byte could
// never fail while the hashes matched. It is a diagnostic in a wire trace, and
// adding a guard for it would be a guard nobody can make fail. Flipping it in
// flight is measurably inert: both sides still reach SE_DONE with equal outcome,
// equal 212 B state and both paid.
struct ProtoRoundResult  { uint32_t hash_before, hash_after; uint8_t outcome; };
struct ProtoBattleEnd    { uint32_t final_hash; uint16_t rounds;
                           uint8_t  reason, outcome, detail; };
struct ProtoGoodbye      { uint8_t reason; };
// --- P7-C4. THE OFFER KEEPS ITS RECORD RAW, for TEAM_SUBMIT's reason: the
//     caller decodes it straight into the PebbleInstance it means to fill, so
//     there is never a second source of truth for a journalled record.
//
//     THERE IS NO SEPARATE offer_crc FIELD AND THERE MUST NOT BE. The 48 B
//     record already carries its own CRC at PBW_OFF_CRC over its own bytes, so
//     trade_rec_crc() below reads the offer's identity out of the record rather
//     than transmitting a second copy of it that could disagree.
struct ProtoTradeOffer   { uint8_t  rec[PBW_BYTES]; };
// TWO REFUSAL BYTES AND NOT ONE, because they are two different enums and
// game/taint.h argues the distinction: `verdict` is a VReject (the codec and
// the one validator refused the RECORD) and `policy` is a TradeReject (the
// record is a legal Pebble and this device will not take it - a taint, a
// duplicate id, a peer that is us). Collapsing them would put a policy flag
// inside the validator's own enum by the back door.
struct ProtoTradeReady   { uint16_t offer_crc_echo; uint8_t verdict, policy; };
struct ProtoTradeConfirm { uint16_t pair_crc; uint8_t accept; };
struct ProtoTradeCommit  { uint16_t pair_crc; };

// The identity of one offer: the record's OWN trailing CRC, read in the codec's
// own little-endian convention. Used to echo "the offer I validated" in READY
// without a second field on the wire.
uint16_t trade_rec_crc(const uint8_t rec[PBW_BYTES]);

// The identity of one TRADE: crc16 over the two 48 B records in the CANONICAL
// order (the initiator's first), so both devices compute the same number for
// the same exchange and a CONFIRM cannot be replayed onto a different pair.
uint16_t trade_pair_crc(const uint8_t initiator_rec[PBW_BYTES],
                        const uint8_t responder_rec[PBW_BYTES]);

// One decoded frame. The header fields are section 15's six plus the two
// diagnostics.
//
// `ack` IS DIAGNOSTIC ONLY. It is carried and logged and NEVER compared or
// branched on, and tests/test_protocol.cpp fuzzes it to arbitrary values across
// a whole round trip to prove nothing depends on it.
//
// `seq` IS A PER-SENDER DE-DUPLICATION NUMBER, ALLOCATED FRESH ON EVERY FRAME -
// RETRANSMISSIONS INCLUDED - AND IT IS NOT THE REPLAY DEFENCE. This header said
// the opposite until P4-C5b ("allocated once and reused verbatim on every
// retransmission"), and the two rules cannot both hold: the plan asks for a
// window that DROPS DUPLICATES, and a retransmission that reused its seq would
// be indistinguishable from a transport duplicate, so dropping duplicates would
// discard exactly the frames the retransmission ladder depends on. Message
// identity is carried by CONTENT instead, which networking/session.cpp needs
// anyway because a radio can duplicate a frame the sender never repeated.
// PF_RETX marks a retransmission for the log and is still never branched on.
struct ProtoMsg {
  uint8_t  version;      // PROTO_VERSION
  uint8_t  type;         // ProtoType
  uint8_t  flags;        // PF_*
  uint8_t  round;        // 1..BATTLE_MAX_ROUNDS for the four in-battle types, else 0
  uint32_t session;      // 0 on HELLO, the session id on the other eleven
  uint16_t seq;
  uint16_t ack;          // diagnostic
  union {
    ProtoHello        hello;
    ProtoCaps         caps;
    ProtoSessionReq   sreq;
    ProtoSessionAcc   sacc;
    ProtoTeamSubmit   team;
    ProtoTeamValid    tval;
    ProtoBattleState  bstate;
    ProtoAction       action;
    ProtoActionResult ares;
    ProtoRoundResult  rres;
    ProtoBattleEnd    bend;
    ProtoGoodbye      bye;
    ProtoTradeOffer   toffer;
    ProtoTradeReady   tready;
    ProtoTradeConfirm tconfirm;
    ProtoTradeCommit  tcommit;
  } p;
};

// Zeroes the whole message and stamps the version, the type, the session and
// the seq. Every encoder path starts here, so a caller cannot forget the
// version field and cannot leave a reserved payload byte holding a stale value.
void proto_msg_init(ProtoMsg& m, ProtoType t, uint32_t session, uint16_t seq);

// -----------------------------------------------------------------------------
//  5. THE CODEC
// -----------------------------------------------------------------------------
// Encodes `m` into `buf`. `cap` is the caller's buffer size; `n_out` receives
// the frame length on PE_OK and 0 on every reject. The encoder applies the SAME
// type, flag, round, count, reserved and HELLO-session rules as the decoder, so
// a frame this function emits is one proto_decode() accepts when it is given
// the session the frame names. That is ASSERTED and not assumed, by a sweep
// over the twelve types and a header fuzz
// (`every_frame_the_encoder_emits_its_own_decoder_accepts`) - and the case is
// there because the claim was FALSE when it was first written: a HELLO with a
// nonzero session encoded and could then be decoded by nobody.
ProtoErr proto_encode(const ProtoMsg& m, uint8_t* buf, size_t cap, size_t& n_out);

// Decodes `n` bytes. `n` IS THE TRANSPORT'S BYTE COUNT AND NEVER A FRAME FIELD:
// the whole design rests on the transport reporting an exact count, which
// ESP-NOW and the loopback both do and a byte stream does not. Over a stream
// `len` would become a trusted input and this codec provides no reframing.
//
// `expect_session` is the session this endpoint is in, or 0 when it is in none.
// HELLO is the one type that must carry session 0 (it predates the session);
// every other type must match `expect_session`, so an endpoint with no session
// refuses all eleven of them by name.
//
// TOTAL: every byte string either decodes to a message this header can name or
// returns a named ProtoErr. It never reads outside buf[0..n-1], never trusts
// `len`, and NEVER PARTIALLY WRITES `out` - `out` is memset to zero on every
// reject, which tests/test_protocol.cpp proves by poisoning it with 0xA5 first.
//
// THE DECODE ORDER IS PINNED AND EACH STEP USES ONLY FIELDS ALREADY PROVED:
//   1  n within [PROTO_FRAME_MIN, PROTO_FRAME_MAX]        PE_SHORT / PE_OVERSIZE
//   2  len at 12..13, len <= PROTO_PAYLOAD_MAX            PE_OVERSIZE
//   3  PROTO_HDR_BYTES + len + 2 == n, EXACTLY            PE_LEN
//   4  the trailing CRC over bytes 0..n-3                 PE_CRC
//   5  version                                            PE_VERSION
//   6  1 <= type <= 12                                    PE_TYPE
//   7  no reserved flag bit                               PE_FLAGS
//   8  len == PROTO_LEN_OF[type]                          PE_LEN_FOR_TYPE
//   9  the header round obeys PROTO_HAS_ROUND[type]       PE_ROUND
//  10  session, HELLO excepted                            PE_SESSION
//  11  the payload's own reserved bytes and TEAM_SUBMIT's
//      count/padding                     PE_RESERVED / PE_COUNT_RANGE / PE_TEAM_PAD
//
// Step 2 is the ONE field read before the CRC. It is bounded against a
// compile-time constant before use and is never used as an index. Step 3 is an
// equality and not `>=` on purpose: trailing bytes are a covert channel, and
// cross-checking a frame field against a quantity the frame did not supply is
// what makes `len` a check rather than a trusted input.
ProtoErr proto_decode(const uint8_t* buf, size_t n, uint32_t expect_session,
                      ProtoMsg& out);

#endif  // PB_NETWORKING_PROTOCOL_H
