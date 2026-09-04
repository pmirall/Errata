// =============================================================================
//  PEBBLEBOL - networking/protocol.cpp
//  See protocol.h for the frame, the 48 B record, the decode order and the
//  reasons behind each of them.
//
//  EVERY MULTI-BYTE FIELD GOES THROUGH THE FOUR HELPERS BELOW. There is no
//  memcpy of a struct onto the wire anywhere in this file and there must never
//  be one: this is the layer whose host test has to mean something on a
//  different machine.
//
//  NO FILE-SCOPE MUTABLE STATE. tools/check.sh gates it, for the reason
//  game/battle.h gives about battle.cpp: it is what lets one process run two
//  endpoints against each other.
//
//  ZERO floating point, no allocation, no I/O, no Arduino, no clock, no RNG.
// =============================================================================
#include "protocol.h"

#include <string.h>

#include "../core/crc16.h"
#include "../data/species_table.h"    // species_get()
#include "../game/evolution.h"        // EVO_STATE_*, evolution_level_ready()

// -----------------------------------------------------------------------------
//  LITTLE-ENDIAN BY EXPLICIT SHIFT. Not a cast, not a memcpy, not a packed
//  struct: the four functions below are the whole endianness contract of this
//  module and tests/test_protocol.cpp pins their output as golden bytes.
// -----------------------------------------------------------------------------
static inline void put_u16(uint8_t* p, uint16_t v)
{
  p[0] = (uint8_t)(v & 0xFFu);
  p[1] = (uint8_t)((v >> 8) & 0xFFu);
}
static inline void put_u32(uint8_t* p, uint32_t v)
{
  p[0] = (uint8_t)(v & 0xFFu);
  p[1] = (uint8_t)((v >> 8) & 0xFFu);
  p[2] = (uint8_t)((v >> 16) & 0xFFu);
  p[3] = (uint8_t)((v >> 24) & 0xFFu);
}
static inline uint16_t get_u16(const uint8_t* p)
{
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static inline uint32_t get_u32(const uint8_t* p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// True when every byte of a span is zero. Used for every reserved field: a
// reserved byte carrying a value is a field from a version we do not speak, and
// accepting it is how a forward-compatible decoder becomes a lying one.
static bool all_zero(const uint8_t* p, size_t n)
{
  for (size_t i = 0; i < n; ++i) if (p[i] != 0u) return false;
  return true;
}

// =============================================================================
//  THE WIRE PEBBLE
// =============================================================================
static void genome_put(uint8_t* p, const Genome& g)
{
  put_u16(p + 0,  g.magic_ver);
  put_u32(p + 2,  g.lineage_id);
  put_u16(p + 6,  g.g0);
  put_u16(p + 8,  g.g1);
  put_u16(p + 10, g.g2);
  p[12] = g.generation;
  p[13] = g.parent_tag;
  put_u16(p + 14, g.crc16);
}

static void genome_get(const uint8_t* p, Genome& g)
{
  g.magic_ver  = get_u16(p + 0);
  g.lineage_id = get_u32(p + 2);
  g.g0         = get_u16(p + 6);
  g.g1         = get_u16(p + 8);
  g.g2         = get_u16(p + 10);
  g.generation = p[12];
  g.parent_tag = p[13];
  g.crc16      = get_u16(p + 14);
}
static_assert(sizeof(Genome) == 16, "the wire genome is 16 bytes, field by field");

void pbw_encode(const PebbleInstance& p, uint8_t rec[PBW_BYTES])
{
  memset(rec, 0, (size_t)PBW_BYTES);          // every reserved byte, once
  put_u16(rec + PBW_OFF_MAGIC, (uint16_t)PBW_MAGIC);
  rec[PBW_OFF_WIRE_VER] = (uint8_t)PBW_LAYOUT_VER;
  rec[PBW_OFF_SPECIES]  = p.species_id;
  put_u32(rec + PBW_OFF_ID, p.id);
  put_u16(rec + PBW_OFF_HP_CUR, p.hp_cur);
  put_u16(rec + PBW_OFF_XP, p.xp);
  rec[PBW_OFF_LEVEL]  = p.level;
  // PBW_OFF_RESERVED0 stays 0: it is where evo_state would have gone, and the
  // receiver derives that field rather than believing it.
  rec[PBW_OFF_STATUS] = (uint8_t)(p.status & (uint8_t)PBW_STATUS_MASK);
  rec[PBW_OFF_FLAGS]  = p.flags;
  rec[PBW_OFF_ORIGIN] = p.origin;
  rec[PBW_OFF_TRAIT]  = p.trait_id;
  for (uint8_t m = 0; m < (uint8_t)PB_MOVE_COUNT; ++m)
    rec[PBW_OFF_MOVES + m] = p.moves[m];
  genome_put(rec + PBW_OFF_GENOME, p.genome);
  put_u16(rec + PBW_OFF_CRC, crc16_ccitt(rec, (size_t)PBW_CRC_BYTES));
}

VReject pbw_decode(const uint8_t rec[PBW_BYTES], PebbleInstance& out)
{
  memset(&out, 0, sizeof out);

  // --- THE SEAL AND THE WIRE-ONLY RULES, in order. All six are this decoder's
  //     and none of them can be produced by validate_pebble().
  if (get_u16(rec + PBW_OFF_MAGIC) != (uint16_t)PBW_MAGIC) return VR_WIRE_MAGIC;
  if (rec[PBW_OFF_WIRE_VER] != (uint8_t)PBW_LAYOUT_VER)    return VR_WIRE_VERSION;
  if (get_u16(rec + PBW_OFF_CRC) != crc16_ccitt(rec, (size_t)PBW_CRC_BYTES))
    return VR_WIRE_CRC;
  if (rec[PBW_OFF_RESERVED0] != 0u ||
      !all_zero(rec + PBW_OFF_RESERVED, 8u))
    return VR_WIRE_RESERVED;

  const uint8_t species = rec[PBW_OFF_SPECIES];
  const uint8_t flags   = rec[PBW_OFF_FLAGS];
  // A custom species id, or either custom flag, means a cs* record this device
  // does not hold. Its OWN code because its MEANING changes in P8 - and a
  // silent remap onto VR_UNKNOWN_SPECIES then would be exactly the kind of
  // quiet reinterpretation this project keeps finding.
  if (species > (uint8_t)SPECIES_ID_BUILTIN_MAX ||
      (flags & (uint8_t)(PBF_CUSTOM | PBF_HAS_CUSTOM_SPRITE)) != 0u)
    return VR_WIRE_CUSTOM_UNRESOLVED;
  if ((uint8_t)(rec[PBW_OFF_STATUS] & (uint8_t)~PBW_STATUS_MASK) != 0u)
    return VR_WIRE_STATUS_BITS;

  // --- BUILD A COMPLETE LOCAL INSTANCE. Never `out`: a caller must not be able
  //     to observe a half-trusted Pebble, not even transiently.
  PebbleInstance loc;
  memset(&loc, 0, sizeof loc);
  loc.magic         = (uint16_t)PEBBLE_MAGIC;
  loc.layout_ver    = (uint8_t)PEBBLE_LAYOUT_VER;
  loc.species_id    = species;
  loc.id            = get_u32(rec + PBW_OFF_ID);
  loc.hp_cur        = get_u16(rec + PBW_OFF_HP_CUR);
  loc.xp            = get_u16(rec + PBW_OFF_XP);
  loc.level         = rec[PBW_OFF_LEVEL];
  loc.status        = rec[PBW_OFF_STATUS];
  loc.flags         = flags;
  loc.origin        = rec[PBW_OFF_ORIGIN];
  loc.trait_id      = rec[PBW_OFF_TRAIT];
  for (uint8_t m = 0; m < (uint8_t)PB_MOVE_COUNT; ++m)
    loc.moves[m] = rec[PBW_OFF_MOVES + m];
  genome_get(rec + PBW_OFF_GENOME, loc.genome);
  // The receiver's defaults for the fields nobody sent. custom_sprite is the
  // one that is not simply zero: 0 is a real cs slot, so it gets the same
  // "there is none" value persistence/save_manager.cpp's pebble_clear() writes.
  loc.custom_sprite = (uint8_t)PB_CUSTOM_SPRITE_NONE;
  // nickname stays empty (a NUL at index 0), which is what makes it terminated.
  // care[] stays 0 and the counters and epochs stay 0: see protocol.h.

  // evo_state, DERIVED FROM OUR OWN TABLES rather than believed. An unknown
  // species leaves it 0 and validate_pebble() names the real problem one line
  // below.
  {
    const SpeciesDef* sp = species_get(loc.species_id);
    uint8_t evo = 0u;
    if (sp != nullptr) {
      evo = (uint8_t)(sp->stage & (uint8_t)EVO_STATE_STAGE_MASK);
      if (evolution_level_ready(loc) != 0u) evo |= (uint8_t)EVO_STATE_PENDING;
    }
    loc.evo_state = evo;
  }

  // --- THE SAME VALIDATOR (spec section 15). One call, no policy flag.
  const VReject r = validate_pebble(loc);
  if (r != VR_OK) return r;                    // `out` is already zeroed

  out = loc;
  return VR_OK;
}

// =============================================================================
//  THE FRAME
// =============================================================================
static const char* const PE_NAMES[] = {
  "PE_OK", "PE_SHORT", "PE_OVERSIZE", "PE_LEN", "PE_CRC", "PE_VERSION",
  "PE_TYPE", "PE_FLAGS", "PE_LEN_FOR_TYPE", "PE_ROUND", "PE_SESSION",
  "PE_RESERVED", "PE_COUNT_RANGE", "PE_TEAM_PAD"
};
static_assert(sizeof(PE_NAMES) / sizeof(PE_NAMES[0]) == (size_t)PE_CODEC_COUNT,
              "a ProtoErr was added without its name");

const char* proto_err_name(ProtoErr e)
{
  if ((uint8_t)e >= (uint8_t)PE_CODEC_COUNT) return "PE_?";
  return PE_NAMES[(uint8_t)e];
}

void proto_msg_init(ProtoMsg& m, ProtoType t, uint32_t session, uint16_t seq)
{
  memset(&m, 0, sizeof m);
  m.version = (uint8_t)PROTO_VERSION;
  m.type    = (uint8_t)t;
  m.session = session;
  m.seq     = seq;
}

// The header rule shared by both directions. Kept in ONE function so an encoder
// that would emit a frame its own decoder refuses cannot exist.
static ProtoErr header_rules_ok(uint8_t type, uint8_t flags, uint8_t round)
{
  if (type == 0u || type > (uint8_t)PROTO_TYPE_MAX)         return PE_TYPE;
  if ((uint8_t)(flags & (uint8_t)~PF_MASK) != 0u)           return PE_FLAGS;
  if (PROTO_HAS_ROUND[type]) {
    if (round < 1u || round > (uint8_t)BATTLE_MAX_ROUNDS)   return PE_ROUND;
  } else {
    if (round != 0u)                                        return PE_ROUND;
  }
  return PE_OK;
}

// -----------------------------------------------------------------------------
//  ENCODE
// -----------------------------------------------------------------------------
ProtoErr proto_encode(const ProtoMsg& m, uint8_t* buf, size_t cap, size_t& n_out)
{
  n_out = 0;
  if (buf == nullptr) return PE_OVERSIZE;
  if (m.version != (uint8_t)PROTO_VERSION) return PE_VERSION;

  const ProtoErr hr = header_rules_ok(m.type, m.flags, m.round);
  if (hr != PE_OK) return hr;

  const uint16_t len = PROTO_LEN_OF[m.type];
  const size_t   n   = (size_t)PROTO_HDR_BYTES + len + PROTO_CRC_BYTES;
  if (n > cap) return PE_OVERSIZE;

  // THE ONE HEADER RULE header_rules_ok() CANNOT HOLD, because it is about a
  // field the decoder only reaches at step 10 and only against a session the
  // encoder is not told. HELLO carries session 0 BY DEFINITION - it predates
  // the session - so proto_decode() refuses any other value whatever
  // `expect_session` it is given, and a HELLO the encoder let through with a
  // nonzero session would be undeliverable to every endpoint alive. It was let
  // through until the P4-C5 follow-up: a review's encoder fuzzer found 198 such
  // frames in 2,000,000 random messages, and the header cited a case for the
  // claim that did not exist. The case exists now
  // (`every_frame_the_encoder_emits_its_own_decoder_accepts`); deleting this
  // line again makes it report 35 undeliverable frames and fail by name.
  if (m.type == (uint8_t)PT_HELLO && m.session != 0u) return PE_SESSION;

  // The two payload rules the encoder must also hold, or it could produce a
  // frame its own decoder names PE_COUNT_RANGE / PE_RESERVED.
  if (m.type == (uint8_t)PT_TEAM_SUBMIT &&
      (m.p.team.count < 1u || m.p.team.count > (uint8_t)BATTLE_TEAM_MAX))
    return PE_COUNT_RANGE;
  if (m.type == (uint8_t)PT_SESSION_REQUEST && m.p.sreq.rules != 0u)
    return PE_RESERVED;

  memset(buf, 0, n);                     // every reserved byte of every payload
  buf[0] = m.version;
  buf[1] = m.type;
  buf[2] = m.flags;
  buf[3] = m.round;
  put_u32(buf + 4, m.session);
  put_u16(buf + 8, m.seq);
  put_u16(buf + 10, m.ack);
  put_u16(buf + 12, len);

  uint8_t* q = buf + PROTO_HDR_BYTES;
  switch ((ProtoType)m.type) {
    case PT_HELLO:
      put_u32(q + 0, m.p.hello.device_id);
      put_u32(q + 4, m.p.hello.hello_nonce);
      break;                                        // q[8..11] reserved
    case PT_CAPABILITIES:
      q[0] = m.p.caps.wire_ver;
      q[1] = m.p.caps.team_max;
      q[2] = m.p.caps.level_max;
      put_u16(q + 4,  m.p.caps.engine_ver);
      put_u16(q + 6,  m.p.caps.hash_ver);
      put_u16(q + 8,  m.p.caps.content_ver);
      put_u16(q + 10, m.p.caps.max_payload);
      put_u32(q + 12, m.p.caps.hash_basis);
      break;
    case PT_SESSION_REQUEST:
      put_u32(q + 0, m.p.sreq.nonce_a);
      q[4] = m.p.sreq.team_count;
      q[5] = m.p.sreq.lvl_lo;
      q[6] = m.p.sreq.lvl_hi;
      q[7] = m.p.sreq.rules;
      break;
    case PT_SESSION_ACCEPT:
      put_u32(q + 0, m.p.sacc.nonce_b);
      q[4] = m.p.sacc.verdict;
      q[5] = m.p.sacc.team_count;
      q[6] = m.p.sacc.lvl_lo;
      q[7] = m.p.sacc.lvl_hi;
      break;
    case PT_TEAM_SUBMIT:
      q[0] = m.p.team.count;
      put_u16(q + 2, m.p.team.team_crc);
      // Slots at or above `count` stay ZERO. `count` is authoritative for how
      // many records the receiver decodes; the zero rule is a cross-check, not
      // a second source of truth.
      for (uint8_t i = 0; i < m.p.team.count; ++i)
        memcpy(q + 4 + (size_t)i * PBW_BYTES, m.p.team.rec[i], (size_t)PBW_BYTES);
      break;
    case PT_TEAM_VALIDATION:
      q[0] = m.p.tval.verdict;
      q[1] = m.p.tval.bad_index;
      put_u16(q + 2, m.p.tval.team_crc_echo);
      break;
    case PT_BATTLE_STATE:
      put_u32(q + 0, m.p.bstate.open_hash);
      q[4] = m.p.bstate.phase;
      q[5] = m.p.bstate.outcome;
      break;
    case PT_ACTION:
      q[0] = m.p.action.kind;
      q[1] = m.p.action.index;
      put_u32(q + 4, m.p.action.open_hash);
      break;
    case PT_ACTION_RESULT:
      q[0] = m.p.ares.reject;
      q[1] = m.p.ares.kind_echo;
      q[2] = m.p.ares.index_echo;
      put_u32(q + 4, m.p.ares.open_hash);
      break;
    case PT_ROUND_RESULT:
      put_u32(q + 0, m.p.rres.hash_before);
      put_u32(q + 4, m.p.rres.hash_after);
      q[8] = m.p.rres.outcome;
      break;
    case PT_BATTLE_END:
      q[0] = m.p.bend.reason;
      q[1] = m.p.bend.outcome;
      q[2] = m.p.bend.detail;
      put_u32(q + 4, m.p.bend.final_hash);
      put_u16(q + 8, m.p.bend.rounds);
      break;
    case PT_GOODBYE:
      q[0] = m.p.bye.reason;
      break;
    default:
      return PE_TYPE;         // unreachable: header_rules_ok() already refused
  }

  put_u16(buf + n - PROTO_CRC_BYTES, crc16_ccitt(buf, n - PROTO_CRC_BYTES));
  n_out = n;
  return PE_OK;
}

// -----------------------------------------------------------------------------
//  DECODE. The step numbers are protocol.h's and the order is contractual.
// -----------------------------------------------------------------------------
ProtoErr proto_decode(const uint8_t* buf, size_t n, uint32_t expect_session,
                      ProtoMsg& out)
{
  memset(&out, 0, sizeof out);            // nothing partial ever survives here
  if (buf == nullptr) return PE_SHORT;

  // 1. THE TRANSPORT'S byte count, never a frame field.
  if (n < PROTO_FRAME_MIN) return PE_SHORT;
  if (n > PROTO_FRAME_MAX) return PE_OVERSIZE;

  // 2. The one field read before the CRC. Bounded against a compile-time
  //    constant before any use, and never used as an index.
  const uint16_t len = get_u16(buf + 12);
  if (len > PROTO_PAYLOAD_MAX) return PE_OVERSIZE;

  // 3. EXACTLY, not >=: trailing bytes are a covert channel.
  if ((size_t)PROTO_HDR_BYTES + len + PROTO_CRC_BYTES != n) return PE_LEN;

  // 4. One call over exactly the bytes received, header included.
  if (crc16_ccitt(buf, n - PROTO_CRC_BYTES) != get_u16(buf + n - PROTO_CRC_BYTES))
    return PE_CRC;

  // 5.
  if (buf[0] != (uint8_t)PROTO_VERSION) return PE_VERSION;

  // 6, 7, 9 - the shared header rule.
  const uint8_t type  = buf[1];
  const uint8_t flags = buf[2];
  const uint8_t round = buf[3];
  {
    const ProtoErr hr = header_rules_ok(type, flags, round);
    // Step 8 sits between the type check and the round check in the pinned
    // order, so it is applied here rather than inside header_rules_ok(): the
    // type must be known before its length can be looked up.
    if (hr == PE_TYPE || hr == PE_FLAGS) return hr;
    if (len != PROTO_LEN_OF[type]) return PE_LEN_FOR_TYPE;   // 8
    if (hr != PE_OK) return hr;                              // 9 (PE_ROUND)
  }

  // 10. Two named cases and no third branch. HELLO by definition predates the
  //     session; an endpoint that has none passes 0 and refuses the other
  //     eleven types by name.
  const uint32_t session = get_u32(buf + 4);
  if (type == (uint8_t)PT_HELLO) { if (session != 0u) return PE_SESSION; }
  else if (session != expect_session)                 return PE_SESSION;

  // 11. The payload. Only now is a single payload byte read.
  const uint8_t* q = buf + PROTO_HDR_BYTES;
  ProtoMsg m;
  memset(&m, 0, sizeof m);
  m.version = buf[0];
  m.type    = type;
  m.flags   = flags;
  m.round   = round;
  m.session = session;
  m.seq     = get_u16(buf + 8);
  m.ack     = get_u16(buf + 10);

  switch ((ProtoType)type) {
    case PT_HELLO:
      if (!all_zero(q + 8, 4)) return PE_RESERVED;
      m.p.hello.device_id   = get_u32(q + 0);
      m.p.hello.hello_nonce = get_u32(q + 4);
      break;
    case PT_CAPABILITIES:
      if (q[3] != 0u) return PE_RESERVED;
      m.p.caps.wire_ver    = q[0];
      m.p.caps.team_max    = q[1];
      m.p.caps.level_max   = q[2];
      m.p.caps.engine_ver  = get_u16(q + 4);
      m.p.caps.hash_ver    = get_u16(q + 6);
      m.p.caps.content_ver = get_u16(q + 8);
      m.p.caps.max_payload = get_u16(q + 10);
      m.p.caps.hash_basis  = get_u32(q + 12);
      break;
    case PT_SESSION_REQUEST:
      // `rules` has exactly one defined value today, so a nonzero one is a rule
      // set we do not speak - the same argument as a reserved byte.
      if (q[7] != 0u || !all_zero(q + 8, 4)) return PE_RESERVED;
      m.p.sreq.nonce_a    = get_u32(q + 0);
      m.p.sreq.team_count = q[4];
      m.p.sreq.lvl_lo     = q[5];
      m.p.sreq.lvl_hi     = q[6];
      m.p.sreq.rules      = q[7];
      break;
    case PT_SESSION_ACCEPT:
      if (!all_zero(q + 8, 4)) return PE_RESERVED;
      m.p.sacc.nonce_b    = get_u32(q + 0);
      m.p.sacc.verdict    = q[4];
      m.p.sacc.team_count = q[5];
      m.p.sacc.lvl_lo     = q[6];
      m.p.sacc.lvl_hi     = q[7];
      break;
    case PT_TEAM_SUBMIT: {
      if (q[1] != 0u) return PE_RESERVED;
      const uint8_t count = q[0];
      // AN INDEXING PRECONDITION, not a game rule: `count` decides how many 48 B
      // slots are read, so it is bounded HERE and not left to
      // validate_team()'s VR_TEAM_SIZE, which answers a different question about
      // a different object.
      if (count < 1u || count > (uint8_t)BATTLE_TEAM_MAX) return PE_COUNT_RANGE;
      for (uint8_t i = count; i < (uint8_t)BATTLE_TEAM_MAX; ++i)
        if (!all_zero(q + 4 + (size_t)i * PBW_BYTES, (size_t)PBW_BYTES))
          return PE_TEAM_PAD;
      m.p.team.count    = count;
      m.p.team.team_crc = get_u16(q + 2);
      // THE RECORDS STAY RAW. pbw_decode() is the caller's to run, straight into
      // whatever PebbleInstance it means to fill - see protocol.h.
      memcpy(m.p.team.rec, q + 4, (size_t)BATTLE_TEAM_MAX * PBW_BYTES);
      break;
    }
    case PT_TEAM_VALIDATION:
      if (!all_zero(q + 4, 4)) return PE_RESERVED;
      m.p.tval.verdict       = q[0];
      m.p.tval.bad_index     = q[1];
      m.p.tval.team_crc_echo = get_u16(q + 2);
      break;
    case PT_BATTLE_STATE:
      if (!all_zero(q + 6, 6)) return PE_RESERVED;
      m.p.bstate.open_hash = get_u32(q + 0);
      m.p.bstate.phase     = q[4];
      m.p.bstate.outcome   = q[5];
      break;
    case PT_ACTION:
      if (!all_zero(q + 2, 2)) return PE_RESERVED;
      m.p.action.kind      = q[0];
      m.p.action.index     = q[1];
      m.p.action.open_hash = get_u32(q + 4);
      break;
    case PT_ACTION_RESULT:
      if (q[3] != 0u) return PE_RESERVED;
      m.p.ares.reject     = q[0];
      m.p.ares.kind_echo  = q[1];
      m.p.ares.index_echo = q[2];
      m.p.ares.open_hash  = get_u32(q + 4);
      break;
    case PT_ROUND_RESULT:
      if (!all_zero(q + 9, 3)) return PE_RESERVED;
      m.p.rres.hash_before = get_u32(q + 0);
      m.p.rres.hash_after  = get_u32(q + 4);
      m.p.rres.outcome     = q[8];
      break;
    case PT_BATTLE_END:
      if (q[3] != 0u || !all_zero(q + 10, 6)) return PE_RESERVED;
      m.p.bend.reason     = q[0];
      m.p.bend.outcome    = q[1];
      m.p.bend.detail     = q[2];
      m.p.bend.final_hash = get_u32(q + 4);
      m.p.bend.rounds     = get_u16(q + 8);
      break;
    case PT_GOODBYE:
      if (!all_zero(q + 1, 3)) return PE_RESERVED;
      m.p.bye.reason = q[0];
      break;
    default:
      return PE_TYPE;         // unreachable: step 6 already refused
  }

  out = m;
  return PE_OK;
}
