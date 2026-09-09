// =============================================================================
//  ERRATA host test - test_protocol.cpp
//  THE SPEC SECTION 15 CODEC (networking/protocol.h, plan P4-C5).
//
//  WHAT THIS FILE HAS TO PROVE, in the plan's own words: "Decode must be total:
//  every byte string either decodes to a valid message or returns a named
//  PROTO_E_* error. It must never read out of bounds, never trust a length
//  field, and never partially write its output on a reject."
//
//  Four mechanisms, because a sentence is not a proof:
//   (a) TOTALITY is swept, not sampled: every truncation length of every
//       message type, and every single-bit flip at every bit position of every
//       message type.
//   (b) "NEVER PARTIALLY WRITES" is checked by POISONING the output with 0xA5
//       before every decode and requiring it to be ALL ZERO after every reject.
//       A missing memset shows as 0xA5 rather than as nothing.
//   (c) "NEVER READS OUT OF BOUNDS" is checked two ways: the decoded ProtoMsg
//       sits between 0x5A guard bands inside a static arena (the fb_oob()
//       idiom, so the ordinary check build catches an over-WRITE without
//       sanitizers), and the whole binary is run under
//       -fsanitize=address,undefined by hand, which is what catches an
//       over-READ. Both are reported.
//   (d) EVERY ProtoErr and every VR_WIRE_* is reached by a vector whose OWN
//       field is the only defect, with a positive control in the same function.
//
//  TWO CASES ALLOCATE, AND IT IS DELIBERATE RATHER THAN AN OVERSIGHT. This step
//  forbids the heap in the FIRMWARE; the_decoder_reads_nothing_past_the_byte_
//  count_the_transport_gave_it and the fuzzer at the bottom both decode out of a
//  `new uint8_t[k]` block sized to EXACTLY the byte count under test, because
//  that is the only way to put an AddressSanitizer redzone on the first illegal
//  byte - a full-size stack array turns an out-of-bounds read into a read that
//  lands inside the array and no sanitizer ever sees it. Both are host-test-only,
//  both are correctly paired with `delete[]`, and the measurement each one buys
//  is recorded at the case. Said here because "no heap in this step" is the rule
//  and a reader running the obvious grep will find these two lines.
//
//  THE GOLDEN BYTE ARRAYS ARE NOT DECORATION. networking/protocol.h forbids a
//  struct memcpy onto the wire, and a grep cannot express that; a frozen frame
//  and a frozen 48 B record, compared byte for byte and cross-checked against
//  hand-computed field positions in the same case, is what catches an
//  endianness or padding shortcut on the machine it would break on.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "core/crc16.h"
#include "core/rng.h"
#include "data/species_table.h"
#include "game/evolution.h"
#include "game/genome.h"
#include "game/bug.h"
#include "game/validate.h"
#include "game/xp.h"
#include "networking/protocol.h"

#define MV_PLAGA  11u

// =============================================================================
//  FIXTURES
// =============================================================================
static Genome sealed_genome(uint32_t lineage)
{
  Genome g;
  memset(&g, 0, sizeof g);
  g.lineage_id = lineage ? lineage : 1u;
  g.g0 = 0x1234u; g.g1 = 0x5678u; g.g2 = 0x09ABu;
  g.generation = 3u;
  genome_seal(g);
  return g;
}

// A Bug that validate_bug() accepts AND that is legal on the wire (no
// custom species, no custom flag, no status bit the wire forbids).
static void mk_valid(BugInstance& p, uint8_t species, uint8_t level, uint32_t id)
{
  memset(&p, 0, sizeof p);
  p.magic         = (uint16_t)BUG_MAGIC;
  p.layout_ver    = (uint8_t)BUG_LAYOUT_VER;
  p.species_id    = species;
  p.id            = id;
  p.level         = level;
  p.origin        = (uint8_t)ORIGIN_WILD;
  p.custom_sprite = (uint8_t)ER_CUSTOM_SPRITE_NONE;
  p.genome        = sealed_genome(0x0BADF00Du + id);
  const SpeciesDef* sp = species_get(species);
  if (sp == nullptr) return;
  memcpy(p.moves, sp->moves, sizeof p.moves);
  p.hp_cur    = xp_hp_max(sp->base_hp, level);
  p.evo_state = (uint8_t)(sp->stage & (uint8_t)EVO_STATE_STAGE_MASK);
}

#define TEST_SESSION  0x11223344u

// A fully legal message of each type, with distinguishable values in every
// field so a round trip that dropped one would show.
static void mk_msg(ProtoMsg& m, ProtoType t)
{
  proto_msg_init(m, t, (t == PT_HELLO) ? 0u : TEST_SESSION, (uint16_t)(0x1000u + (int)t));
  m.ack = (uint16_t)(0x2000u + (int)t);
  if (PROTO_HAS_ROUND[(int)t]) m.round = 7u;
  switch (t) {
    case PT_HELLO:
      m.p.hello.device_id = 0xA1B2C3D4u; m.p.hello.hello_nonce = 0x55667788u; break;
    case PT_CAPABILITIES:
      m.p.caps.wire_ver    = (uint8_t)BUGW_LAYOUT_VER;
      m.p.caps.team_max    = (uint8_t)BATTLE_TEAM_MAX;
      m.p.caps.level_max   = (uint8_t)ER_LEVEL_MAX;
      m.p.caps.engine_ver  = 2u;
      m.p.caps.hash_ver    = 3u;
      m.p.caps.content_ver = 0x5B4Au;
      m.p.caps.max_payload = PROTO_PAYLOAD_MAX;
      m.p.caps.hash_basis  = 0x0F1E2D3Cu;
      break;
    case PT_SESSION_REQUEST:
      m.p.sreq.nonce_a = 0xCAFEBABEu; m.p.sreq.team_count = 3u;
      // `rules` IS THE OPERATION SINCE P7-C4 and is no longer a reserved byte.
      // A nonzero value here is what proves the round trip carries it.
      m.p.sreq.lvl_lo = 8u; m.p.sreq.lvl_hi = 16u;
      m.p.sreq.rules = 1u; break;      // SOP_TRADE, without dragging session.h in
    case PT_SESSION_ACCEPT:
      m.p.sacc.nonce_b = 0xFEEDFACEu; m.p.sacc.verdict = 0u;
      m.p.sacc.team_count = 3u; m.p.sacc.lvl_lo = 8u; m.p.sacc.lvl_hi = 16u;
      m.p.sacc.op_echo = 1u; break;    // the same operation, echoed
    case PT_TEAM_SUBMIT: {
      m.p.team.count    = (uint8_t)BATTLE_TEAM_MAX;
      m.p.team.team_crc = 0x9E3Au;
      for (uint8_t i = 0; i < (uint8_t)BATTLE_TEAM_MAX; ++i) {
        BugInstance p; mk_valid(p, (uint8_t)(1u + i * 4u), (uint8_t)(10u + i), 0x300u + i);
        pbw_encode(p, m.p.team.rec[i]);
      }
      break;
    }
    case PT_TEAM_VALIDATION:
      m.p.tval.verdict = (uint8_t)VR_BAD_LEVEL; m.p.tval.bad_index = 2u;
      m.p.tval.team_crc_echo = 0x9E3Au; break;
    case PT_BATTLE_STATE:
      m.p.bstate.open_hash = 0x13579BDFu; m.p.bstate.phase = 1u; m.p.bstate.outcome = 0u;
      break;
    case PT_ACTION:
      m.p.action.kind = 1u; m.p.action.index = 2u; m.p.action.open_hash = 0xDEADBEEFu;
      break;
    case PT_ACTION_RESULT:
      m.p.ares.reject = 0u; m.p.ares.kind_echo = 1u; m.p.ares.index_echo = 2u;
      m.p.ares.open_hash = 0xDEADBEEFu; break;
    case PT_ROUND_RESULT:
      m.p.rres.hash_before = 0x01020304u; m.p.rres.hash_after = 0x05060708u;
      m.p.rres.outcome = 1u; break;
    case PT_BATTLE_END:
      m.p.bend.reason = 1u; m.p.bend.outcome = 2u; m.p.bend.detail = 3u;
      m.p.bend.final_hash = 0x0A0B0C0Du; m.p.bend.rounds = 22u; break;
    case PT_GOODBYE:
      m.p.bye.reason = 4u; break;
    case PT_TRADE_OFFER: {
      BugInstance p; mk_valid(p, 5u, 12u, 0x7A1u);
      pbw_encode(p, m.p.toffer.rec);
      break;
    }
    case PT_TRADE_READY:
      m.p.tready.verdict = (uint8_t)VR_OK; m.p.tready.policy = 0u;
      m.p.tready.offer_crc_echo = 0x4C7Bu; break;
    case PT_TRADE_CONFIRM:
      m.p.tconfirm.accept = 1u; m.p.tconfirm.pair_crc = 0xB19Du; break;
    case PT_TRADE_COMMIT:
      m.p.tcommit.pair_crc = 0xB19Du; break;
    case PT_BREED_OFFER: {
      BugInstance p; mk_valid(p, 9u, 14u, 0x8B2u);
      pbw_encode(p, m.p.boffer.rec);
      break;
    }
    case PT_BREED_READY:
      m.p.bready.verdict = (uint8_t)VR_OK; m.p.bready.pair_reject = 0u;
      m.p.bready.offer_crc_echo = 0x5D8Cu; break;
    case PT_BREED_CONFIRM:
      m.p.bconfirm.accept = 1u; m.p.bconfirm.plan_crc = 0xC2AEu; break;
    case PT_BREED_DONE:
      m.p.bdone.reject = 0u; m.p.bdone.plan_crc = 0xC2AEu; break;
    default: break;
  }
}

static size_t encode_ok(const ProtoMsg& m, uint8_t* buf, size_t cap)
{
  size_t n = 0;
  const ProtoErr e = proto_encode(m, buf, cap, n);
  CHECK_EQ((int)e, (int)PE_OK);
  return n;
}

static void reseal(uint8_t* buf, size_t n)
{
  const uint16_t c = crc16_ccitt(buf, n - 2u);
  buf[n - 2] = (uint8_t)(c & 0xFFu);
  buf[n - 1] = (uint8_t)((c >> 8) & 0xFFu);
}

// -----------------------------------------------------------------------------
//  THE GUARDED ARENA. The decoded message sits between 0x5A bands and is
//  poisoned with 0xA5 before every call, so a decoder that over-writes its
//  output or that leaves it partially written is caught in the ORDINARY check
//  build, with no sanitizer.
// -----------------------------------------------------------------------------
#define GUARD_BYTES 64
struct Arena {
  uint8_t  pre[GUARD_BYTES];
  ProtoMsg m;
  uint8_t  post[GUARD_BYTES];
};
static Arena g_arena;

static void arena_arm(void)
{
  memset(g_arena.pre,  0x5A, sizeof g_arena.pre);
  memset(g_arena.post, 0x5A, sizeof g_arena.post);
  memset(&g_arena.m,   0xA5, sizeof g_arena.m);
}
static bool arena_guards_intact(void)
{
  for (size_t i = 0; i < sizeof g_arena.pre; ++i)  if (g_arena.pre[i]  != 0x5A) return false;
  for (size_t i = 0; i < sizeof g_arena.post; ++i) if (g_arena.post[i] != 0x5A) return false;
  return true;
}
static bool arena_msg_is_zero(void)
{
  const uint8_t* b = (const uint8_t*)(const void*)&g_arena.m;
  for (size_t i = 0; i < sizeof g_arena.m; ++i) if (b[i] != 0u) return false;
  return true;
}

// Decode into the guarded arena and assert the two invariants that hold on
// EVERY call, whatever the answer.
static ProtoErr guarded_decode(const uint8_t* buf, size_t n, uint32_t sess)
{
  arena_arm();
  const ProtoErr e = proto_decode(buf, n, sess, g_arena.m);
  CHECK(arena_guards_intact());
  CHECK((int)e < (int)PE_CODEC_COUNT);
  if (e != PE_OK) CHECK(arena_msg_is_zero());
  return e;
}

// =============================================================================
//  1. THE GOLDEN BYTES, AND THE FIELD POSITIONS THEY ARE CROSS-CHECKED AGAINST
// =============================================================================
// RE-RECORDED BY P7-C4, AND ONLY TWO KINDS OF BYTE MOVED: byte 0 (the protocol
// version, 1 -> 2) and the two trailing CRC bytes that cover it. Every field
// position is unchanged, which is the point of appending the four trade types
// after section 15's twelve instead of inserting them among them - and the
// hand-read cross-checks below are what say so rather than this comment.
static const uint8_t GOLDEN_ACTION_FRAME[24] = {
  0x02, 0x08, 0x01, 0x07, 0x44, 0x33, 0x22, 0x11, 0xEF, 0xBE, 0x02, 0x01,
  0x08, 0x00, 0x01, 0x02, 0x00, 0x00, 0xEF, 0xBE, 0xAD, 0xDE, 0x55, 0x4D
};

// THE TRADE FRAME, PINNED THE SAME WAY (P7-C4). The 48 B record inside it is
// GOLDEN_WIRE_BUG below, so this array pins only what is NEW: the type byte,
// the 52-byte length, the four reserved bytes before the record and the offset
// the record sits at. A golden that re-pinned the record would be a second copy
// of a fact the next case already owns.
static const uint8_t GOLDEN_TRADE_OFFER_HEAD[18] = {
  0x02, 0x0D, 0x00, 0x00, 0x44, 0x33, 0x22, 0x11, 0x0D, 0x10,
  0x0D, 0x20, 0x34, 0x00,                       // len 52, little-endian
  0x00, 0x00, 0x00, 0x00                        // the four reserved payload bytes
};

TEST(the_trade_offer_frame_is_pinned_and_carries_the_record_at_offset_four) {
  ProtoMsg m; mk_msg(m, PT_TRADE_OFFER);
  uint8_t buf[PROTO_FRAME_MAX];
  const size_t n = encode_ok(m, buf, sizeof buf);
  CHECK_EQ((int)n, (int)(PROTO_HDR_BYTES + 52u + PROTO_CRC_BYTES));
  for (size_t i = 0; i < sizeof GOLDEN_TRADE_OFFER_HEAD; ++i) {
    if (buf[i] != GOLDEN_TRADE_OFFER_HEAD[i])
      fprintf(stderr, "    byte %u: got 0x%02X want 0x%02X\n",
              (unsigned)i, (unsigned)buf[i], (unsigned)GOLDEN_TRADE_OFFER_HEAD[i]);
    CHECK_EQ((int)buf[i], (int)GOLDEN_TRADE_OFFER_HEAD[i]);
  }
  // The record is at payload offset 4 and is byte-identical to what
  // pbw_encode() produced - no re-framing, no second copy.
  CHECK_EQ(memcmp(buf + PROTO_HDR_BYTES + 4, m.p.toffer.rec, (size_t)BUGW_BYTES), 0);
  // A trade has no round, and the decoder refuses one at step 9.
  CHECK_EQ((int)buf[3], 0);
  CHECK(!PROTO_HAS_ROUND[(int)PT_TRADE_OFFER]);
  // trade_rec_crc() reads the identity out of the record itself rather than out
  // of a field the frame would otherwise have to carry twice.
  CHECK_EQ((int)trade_rec_crc(m.p.toffer.rec),
           (int)((uint16_t)m.p.toffer.rec[BUGW_OFF_CRC] |
                 ((uint16_t)m.p.toffer.rec[BUGW_OFF_CRC + 1] << 8)));
  // And it survives the round trip, which is what a READY echoes.
  CHECK_EQ((int)guarded_decode(buf, n, TEST_SESSION), (int)PE_OK);
  CHECK_EQ((int)trade_rec_crc(g_arena.m.p.toffer.rec),
           (int)trade_rec_crc(m.p.toffer.rec));
}

TEST(the_pair_identity_is_order_dependent_and_that_is_the_whole_point) {
  ProtoMsg a; mk_msg(a, PT_TRADE_OFFER);
  BugInstance other; mk_valid(other, 9u, 7u, 0x7B2u);
  uint8_t rec_b[BUGW_BYTES];
  pbw_encode(other, rec_b);

  const uint16_t ab = trade_pair_crc(a.p.toffer.rec, rec_b);
  const uint16_t ba = trade_pair_crc(rec_b, a.p.toffer.rec);
  CHECK(ab != ba);          // a symmetric identity could not name WHO gives WHAT
  CHECK_EQ((int)trade_pair_crc(a.p.toffer.rec, rec_b), (int)ab);   // and stable

  // One flipped byte in either record changes it: the identity covers both
  // whole records and not just their two CRCs.
  uint8_t poked[BUGW_BYTES];
  memcpy(poked, rec_b, sizeof poked);
  poked[BUGW_OFF_LEVEL] = (uint8_t)(poked[BUGW_OFF_LEVEL] ^ 0x01u);
  CHECK(trade_pair_crc(a.p.toffer.rec, poked) != ab);
}

TEST(one_known_frame_is_pinned_byte_for_byte_and_field_by_field) {
  ProtoMsg m;
  proto_msg_init(m, PT_ACTION, 0x11223344u, 0xBEEFu);
  m.flags = (uint8_t)PF_RETX;
  m.round = 7u;
  m.ack   = 0x0102u;
  m.p.action.kind = 1u; m.p.action.index = 2u; m.p.action.open_hash = 0xDEADBEEFu;

  uint8_t buf[PROTO_FRAME_MAX];
  const size_t n = encode_ok(m, buf, sizeof buf);
  CHECK_EQ((int)n, (int)sizeof GOLDEN_ACTION_FRAME);
  for (size_t i = 0; i < n; ++i) {
    if (buf[i] != GOLDEN_ACTION_FRAME[i])
      fprintf(stderr, "    byte %u: got 0x%02X want 0x%02X\n",
              (unsigned)i, (unsigned)buf[i], (unsigned)GOLDEN_ACTION_FRAME[i]);
    CHECK_EQ((int)buf[i], (int)GOLDEN_ACTION_FRAME[i]);
  }

  // The six things spec section 15 requires of every packet, read out of the
  // frozen bytes BY HAND. This is what makes the array above a layout check and
  // not merely a change detector.
  CHECK_EQ((int)buf[0], (int)PROTO_VERSION);                  // protocol version
  CHECK_EQ((int)buf[1], (int)PT_ACTION);                      // message type
  CHECK_EQ((int)buf[4], 0x44); CHECK_EQ((int)buf[5], 0x33);   // session id, LE
  CHECK_EQ((int)buf[6], 0x22); CHECK_EQ((int)buf[7], 0x11);
  CHECK_EQ((int)buf[8], 0xEF); CHECK_EQ((int)buf[9], 0xBE);   // sequence number, LE
  CHECK_EQ((int)buf[12], 8);   CHECK_EQ((int)buf[13], 0);     // payload length, LE
  const uint16_t crc = crc16_ccitt(buf, n - 2u);              // integrity check
  CHECK_EQ((int)buf[n - 2], (int)(crc & 0xFFu));
  CHECK_EQ((int)buf[n - 1], (int)((crc >> 8) & 0xFFu));
  // The CRC covers the HEADER too, so a flipped type or session is not free.
  CHECK_EQ((int)(n - 2u), (int)(PROTO_HDR_BYTES + 8u));
}

static const uint8_t GOLDEN_WIRE_BUG[48] = {
  0x50, 0x57, 0x01, 0x05, 0xEE, 0xFF, 0xC0, 0x00, 0x20, 0x00, 0x03, 0x00,
  0x0C, 0x00, 0x01, 0x00, 0x01, 0x00, 0x21, 0x1B, 0x01, 0x20, 0x51, 0x4E,
  0x0D, 0xF0, 0xAD, 0x0B, 0x34, 0x12, 0x78, 0x56, 0xAB, 0x09, 0x03, 0x00,
  0xC1, 0x26, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x8C, 0xD2
};

TEST(one_known_wire_bug_is_pinned_byte_for_byte_and_field_by_field) {
  BugInstance p;
  memset(&p, 0, sizeof p);
  p.magic = (uint16_t)BUG_MAGIC; p.layout_ver = (uint8_t)BUG_LAYOUT_VER;
  p.species_id = 5u; p.id = 0x00C0FFEEu; p.level = 12u; p.xp = 3u;
  p.origin = 1u; p.custom_sprite = (uint8_t)ER_CUSTOM_SPRITE_NONE;
  p.status = (uint8_t)PBS_SICK;
  const SpeciesDef* sp = species_get(5);
  memcpy(p.moves, sp->moves, sizeof p.moves);
  p.hp_cur    = xp_hp_max(sp->base_hp, 12u);
  p.evo_state = sp->stage;
  p.genome    = sealed_genome(0x0BADF00Du);
  CHECK_EQ((int)validate_bug(p), (int)VR_OK);

  uint8_t rec[BUGW_BYTES];
  pbw_encode(p, rec);
  for (size_t i = 0; i < (size_t)BUGW_BYTES; ++i) {
    if (rec[i] != GOLDEN_WIRE_BUG[i])
      fprintf(stderr, "    rec byte %u: got 0x%02X want 0x%02X\n",
              (unsigned)i, (unsigned)rec[i], (unsigned)GOLDEN_WIRE_BUG[i]);
    CHECK_EQ((int)rec[i], (int)GOLDEN_WIRE_BUG[i]);
  }
  // Hand-read positions, little-endian by explicit shift and not by memcpy.
  CHECK_EQ((int)rec[BUGW_OFF_MAGIC], (int)(BUGW_MAGIC & 0xFFu));
  CHECK_EQ((int)rec[BUGW_OFF_MAGIC + 1], (int)((BUGW_MAGIC >> 8) & 0xFFu));
  CHECK_EQ((int)rec[BUGW_OFF_SPECIES], 5);
  CHECK_EQ((int)rec[BUGW_OFF_ID], 0xEE);      // 0x00C0FFEE little-endian
  CHECK_EQ((int)rec[BUGW_OFF_ID + 3], 0x00);
  CHECK_EQ((int)rec[BUGW_OFF_LEVEL], 12);
  CHECK_EQ((int)rec[BUGW_OFF_RESERVED0], 0);  // where evo_state would have gone
  for (uint8_t m = 0; m < (uint8_t)ER_MOVE_COUNT; ++m)
    CHECK_EQ((int)rec[BUGW_OFF_MOVES + m], (int)sp->moves[m]);
  const uint16_t c = crc16_ccitt(rec, (size_t)BUGW_CRC_BYTES);
  CHECK_EQ((int)rec[BUGW_OFF_CRC], (int)(c & 0xFFu));
}

// =============================================================================
//  2. ROUND TRIP, EVERY TYPE
// =============================================================================
static void expect_same_payload(const ProtoMsg& a, const ProtoMsg& b)
{
  CHECK_EQ((int)a.version, (int)b.version);
  CHECK_EQ((int)a.type,    (int)b.type);
  CHECK_EQ((int)a.flags,   (int)b.flags);
  CHECK_EQ((int)a.round,   (int)b.round);
  CHECK_EQ((long long)a.session, (long long)b.session);
  CHECK_EQ((int)a.seq,     (int)b.seq);
  CHECK_EQ((int)a.ack,     (int)b.ack);
  switch ((ProtoType)a.type) {
    case PT_HELLO:
      CHECK_EQ((long long)a.p.hello.device_id, (long long)b.p.hello.device_id);
      CHECK_EQ((long long)a.p.hello.hello_nonce, (long long)b.p.hello.hello_nonce);
      break;
    case PT_CAPABILITIES:
      CHECK_EQ((int)a.p.caps.wire_ver, (int)b.p.caps.wire_ver);
      CHECK_EQ((int)a.p.caps.team_max, (int)b.p.caps.team_max);
      CHECK_EQ((int)a.p.caps.level_max, (int)b.p.caps.level_max);
      CHECK_EQ((int)a.p.caps.engine_ver, (int)b.p.caps.engine_ver);
      CHECK_EQ((int)a.p.caps.hash_ver, (int)b.p.caps.hash_ver);
      CHECK_EQ((int)a.p.caps.content_ver, (int)b.p.caps.content_ver);
      CHECK_EQ((int)a.p.caps.max_payload, (int)b.p.caps.max_payload);
      CHECK_EQ((long long)a.p.caps.hash_basis, (long long)b.p.caps.hash_basis);
      break;
    case PT_SESSION_REQUEST:
      CHECK_EQ((long long)a.p.sreq.nonce_a, (long long)b.p.sreq.nonce_a);
      CHECK_EQ((int)a.p.sreq.team_count, (int)b.p.sreq.team_count);
      CHECK_EQ((int)a.p.sreq.lvl_lo, (int)b.p.sreq.lvl_lo);
      CHECK_EQ((int)a.p.sreq.lvl_hi, (int)b.p.sreq.lvl_hi);
      CHECK_EQ((int)a.p.sreq.rules, (int)b.p.sreq.rules);
      break;
    case PT_SESSION_ACCEPT:
      CHECK_EQ((long long)a.p.sacc.nonce_b, (long long)b.p.sacc.nonce_b);
      CHECK_EQ((int)a.p.sacc.verdict, (int)b.p.sacc.verdict);
      CHECK_EQ((int)a.p.sacc.team_count, (int)b.p.sacc.team_count);
      CHECK_EQ((int)a.p.sacc.lvl_lo, (int)b.p.sacc.lvl_lo);
      CHECK_EQ((int)a.p.sacc.lvl_hi, (int)b.p.sacc.lvl_hi);
      break;
    case PT_TEAM_SUBMIT:
      CHECK_EQ((int)a.p.team.count, (int)b.p.team.count);
      CHECK_EQ((int)a.p.team.team_crc, (int)b.p.team.team_crc);
      CHECK_EQ(memcmp(a.p.team.rec, b.p.team.rec, sizeof a.p.team.rec), 0);
      break;
    case PT_TEAM_VALIDATION:
      CHECK_EQ((int)a.p.tval.verdict, (int)b.p.tval.verdict);
      CHECK_EQ((int)a.p.tval.bad_index, (int)b.p.tval.bad_index);
      CHECK_EQ((int)a.p.tval.team_crc_echo, (int)b.p.tval.team_crc_echo);
      break;
    case PT_BATTLE_STATE:
      CHECK_EQ((long long)a.p.bstate.open_hash, (long long)b.p.bstate.open_hash);
      CHECK_EQ((int)a.p.bstate.phase, (int)b.p.bstate.phase);
      CHECK_EQ((int)a.p.bstate.outcome, (int)b.p.bstate.outcome);
      break;
    case PT_ACTION:
      CHECK_EQ((int)a.p.action.kind, (int)b.p.action.kind);
      CHECK_EQ((int)a.p.action.index, (int)b.p.action.index);
      CHECK_EQ((long long)a.p.action.open_hash, (long long)b.p.action.open_hash);
      break;
    case PT_ACTION_RESULT:
      CHECK_EQ((int)a.p.ares.reject, (int)b.p.ares.reject);
      CHECK_EQ((int)a.p.ares.kind_echo, (int)b.p.ares.kind_echo);
      CHECK_EQ((int)a.p.ares.index_echo, (int)b.p.ares.index_echo);
      CHECK_EQ((long long)a.p.ares.open_hash, (long long)b.p.ares.open_hash);
      break;
    case PT_ROUND_RESULT:
      CHECK_EQ((long long)a.p.rres.hash_before, (long long)b.p.rres.hash_before);
      CHECK_EQ((long long)a.p.rres.hash_after, (long long)b.p.rres.hash_after);
      CHECK_EQ((int)a.p.rres.outcome, (int)b.p.rres.outcome);
      break;
    case PT_BATTLE_END:
      CHECK_EQ((int)a.p.bend.reason, (int)b.p.bend.reason);
      CHECK_EQ((int)a.p.bend.outcome, (int)b.p.bend.outcome);
      CHECK_EQ((int)a.p.bend.detail, (int)b.p.bend.detail);
      CHECK_EQ((long long)a.p.bend.final_hash, (long long)b.p.bend.final_hash);
      CHECK_EQ((int)a.p.bend.rounds, (int)b.p.bend.rounds);
      break;
    case PT_GOODBYE:
      CHECK_EQ((int)a.p.bye.reason, (int)b.p.bye.reason);
      break;
    case PT_TRADE_OFFER:
      CHECK_EQ(memcmp(a.p.toffer.rec, b.p.toffer.rec, (size_t)BUGW_BYTES), 0);
      break;
    case PT_TRADE_READY:
      CHECK_EQ((int)a.p.tready.verdict, (int)b.p.tready.verdict);
      CHECK_EQ((int)a.p.tready.policy, (int)b.p.tready.policy);
      CHECK_EQ((int)a.p.tready.offer_crc_echo, (int)b.p.tready.offer_crc_echo);
      break;
    case PT_TRADE_CONFIRM:
      CHECK_EQ((int)a.p.tconfirm.accept, (int)b.p.tconfirm.accept);
      CHECK_EQ((int)a.p.tconfirm.pair_crc, (int)b.p.tconfirm.pair_crc);
      break;
    case PT_TRADE_COMMIT:
      CHECK_EQ((int)a.p.tcommit.pair_crc, (int)b.p.tcommit.pair_crc);
      break;
    case PT_BREED_OFFER:
      CHECK_EQ(memcmp(a.p.boffer.rec, b.p.boffer.rec, (size_t)BUGW_BYTES), 0);
      break;
    case PT_BREED_READY:
      CHECK_EQ((int)a.p.bready.verdict, (int)b.p.bready.verdict);
      CHECK_EQ((int)a.p.bready.pair_reject, (int)b.p.bready.pair_reject);
      CHECK_EQ((int)a.p.bready.offer_crc_echo, (int)b.p.bready.offer_crc_echo);
      break;
    case PT_BREED_CONFIRM:
      CHECK_EQ((int)a.p.bconfirm.accept, (int)b.p.bconfirm.accept);
      CHECK_EQ((int)a.p.bconfirm.plan_crc, (int)b.p.bconfirm.plan_crc);
      break;
    case PT_BREED_DONE:
      CHECK_EQ((int)a.p.bdone.reject, (int)b.p.bdone.reject);
      CHECK_EQ((int)a.p.bdone.plan_crc, (int)b.p.bdone.plan_crc);
      break;
    default: CHECK(false); break;
  }
}

TEST(every_message_type_round_trips_and_re_encodes_byte_for_byte) {
  for (int t = 1; t <= (int)PROTO_TYPE_MAX; ++t) {
    ProtoMsg m; mk_msg(m, (ProtoType)t);
    uint8_t buf[PROTO_FRAME_MAX];
    const size_t n = encode_ok(m, buf, sizeof buf);
    CHECK_EQ((int)n, (int)(PROTO_HDR_BYTES + PROTO_LEN_OF[t] + PROTO_CRC_BYTES));

    const uint32_t sess = (t == (int)PT_HELLO) ? 0u : TEST_SESSION;
    CHECK_EQ((int)guarded_decode(buf, n, sess), (int)PE_OK);
    expect_same_payload(m, g_arena.m);

    // Re-encoding what came back must give the SAME BYTES. This is the half
    // that catches a field the decoder silently dropped.
    uint8_t again[PROTO_FRAME_MAX];
    const size_t n2 = encode_ok(g_arena.m, again, sizeof again);
    CHECK_EQ((int)n2, (int)n);
    CHECK_EQ(memcmp(buf, again, n), 0);
  }
}

TEST(the_payload_cap_is_the_widest_message_and_a_frame_fits_one_datagram) {
  uint16_t widest = 0;
  int widest_type = 0;
  for (int t = 1; t <= (int)PROTO_TYPE_MAX; ++t)
    if (PROTO_LEN_OF[t] > widest) { widest = PROTO_LEN_OF[t]; widest_type = t; }
  CHECK_EQ((int)widest, (int)PROTO_PAYLOAD_MAX);
  CHECK_EQ(widest_type, (int)PT_TEAM_SUBMIT);
  CHECK_EQ((int)PROTO_LEN_OF[PT_TEAM_SUBMIT], (int)(4u + 3u * BUGW_BYTES));
  CHECK_EQ((int)PROTO_FRAME_MAX, (int)(PROTO_HDR_BYTES + PROTO_PAYLOAD_MAX + 2u));
  CHECK((int)PROTO_FRAME_MAX <= 250);
  CHECK_EQ((int)PROTO_FRAME_MIN, (int)(PROTO_HDR_BYTES + 2u));
  CHECK_EQ((int)PROTO_LEN_OF[0], 0);       // type 0 is reserved and never valid
  CHECK_EQ((int)BUGW_BYTES, (int)TR_WIRE_BYTES);
}

// =============================================================================
//  3. TOTALITY - swept, not sampled
// =============================================================================
TEST(truncation_at_every_length_of_every_type_is_refused_by_name) {
  int short_seen = 0, len_seen = 0, other = 0;
  for (int t = 1; t <= (int)PROTO_TYPE_MAX; ++t) {
    ProtoMsg m; mk_msg(m, (ProtoType)t);
    uint8_t buf[PROTO_FRAME_MAX];
    const size_t n = encode_ok(m, buf, sizeof buf);
    const uint32_t sess = (t == (int)PT_HELLO) ? 0u : TEST_SESSION;
    for (size_t k = 0; k < n; ++k) {
      const ProtoErr e = guarded_decode(buf, k, sess);
      CHECK(e != PE_OK);
      if      (e == PE_SHORT) short_seen++;
      else if (e == PE_LEN)   len_seen++;
      else { other++; fprintf(stderr, "    type %d truncated to %u -> %s\n",
                              t, (unsigned)k, proto_err_name(e)); }
    }
    // A zero-length buffer and a null pointer are byte strings too.
    CHECK_EQ((int)guarded_decode(buf, 0, sess), (int)PE_SHORT);
    CHECK_EQ((int)guarded_decode(nullptr, n, sess), (int)PE_SHORT);
  }
  // BOTH buckets are non-empty and NOTHING else appears: a truncation shorter
  // than a header is PE_SHORT, and one that keeps the header but loses payload
  // is PE_LEN, because the length field and the delivered count disagree.
  CHECK_EQ(other, 0);
  CHECK(short_seen > 0);
  CHECK(len_seen > 0);
}

// The truncation sweep above hands a SHORT LENGTH over a FULL-SIZE buffer, so a
// read past `n` is still a read inside the array and no sanitizer would see it.
// This case copies each truncation into a heap block of EXACTLY that many bytes,
// which puts an AddressSanitizer redzone immediately after the last legal byte -
// so under -fsanitize=address a decoder that reads past the delivered count is a
// heap-buffer-overflow rather than a silent success. MEASURED: with the step-1
// floor removed, the ordinary build catches the mutation through
// every_codec_error_is_reachable_and_has_a_distinct_name and reports NO memory
// error, and this case under ASan is what reports the read itself.
TEST(the_decoder_reads_nothing_past_the_byte_count_the_transport_gave_it) {
  int swept = 0;
  for (int t = 1; t <= (int)PROTO_TYPE_MAX; ++t) {
    ProtoMsg m; mk_msg(m, (ProtoType)t);
    uint8_t src[PROTO_FRAME_MAX];
    const size_t n = encode_ok(m, src, sizeof src);
    const uint32_t sess = (t == (int)PT_HELLO) ? 0u : TEST_SESSION;
    for (size_t k = 0; k <= n; ++k) {
      uint8_t* exact = new uint8_t[k ? k : 1u];
      memcpy(exact, src, k);
      const ProtoErr e = guarded_decode(exact, k, sess);
      if (k == n) CHECK_EQ((int)e, (int)PE_OK);
      else        CHECK(e != PE_OK);
      delete[] exact;
      swept++;
    }
  }
  CHECK(swept > 12 * (int)PROTO_FRAME_MIN);
}

// THIS SWEEP DECODES OUT OF A STACK BUFFER AND THAT IS CORRECT, which is worth a
// sentence because it looks like the omission the case above exists to fix. A
// single bit flipped anywhere in the frame either invalidates the trailing CRC
// (step 4) or breaks the length equality (step 3), so NO frame this sweep builds
// ever reaches step 5, let alone the payload read at step 11 - which the
// assertions below state independently, since only PE_CRC, PE_LEN and
// PE_OVERSIZE are permitted answers. A sweep that cannot reach a payload read
// cannot read past the buffer, so an exact-sized arena here would buy nothing.
// MEASURED, because a review suggested otherwise: with step 8
// (len == PROTO_LEN_OF[type]) deleted and every one of these 3,680 flips decoded
// out of a heap block of exactly `n` bytes, AddressSanitizer reports NOTHING.
// The sweep that CAN reach step 11 with a forged type is the fuzzer at the
// bottom of this file, because six of its eleven mutations reseal the CRC - and
// that is where the exact-sized arena lives and where it is measured.
TEST(a_single_bit_flipped_at_every_position_of_every_type_is_refused_by_name) {
  int crc_seen = 0, len_seen = 0, other = 0;
  for (int t = 1; t <= (int)PROTO_TYPE_MAX; ++t) {
    ProtoMsg m; mk_msg(m, (ProtoType)t);
    uint8_t clean[PROTO_FRAME_MAX];
    const size_t n = encode_ok(m, clean, sizeof clean);
    const uint32_t sess = (t == (int)PT_HELLO) ? 0u : TEST_SESSION;
    for (size_t byte = 0; byte < n; ++byte) {
      for (int bit = 0; bit < 8; ++bit) {
        uint8_t buf[PROTO_FRAME_MAX];
        memcpy(buf, clean, n);
        buf[byte] = (uint8_t)(buf[byte] ^ (uint8_t)(1u << bit));
        const ProtoErr e = guarded_decode(buf, n, sess);
        CHECK(e != PE_OK);
        // A CRC-16 detects every single-bit error, so the answer is PE_CRC
        // EVERYWHERE EXCEPT the length field - and there it is not, because
        // steps 2 and 3 of the pinned decode order deliberately run BEFORE the
        // CRC so that `len` is bounded before anything uses it.
        if (byte == 12u || byte == 13u) {
          if (e != PE_LEN && e != PE_OVERSIZE) {
            other++;
            fprintf(stderr, "    type %d len byte %u bit %d -> %s\n",
                    t, (unsigned)byte, bit, proto_err_name(e));
          } else len_seen++;
        } else if (e == PE_CRC) crc_seen++;
        else {
          other++;
          fprintf(stderr, "    type %d byte %u bit %d -> %s\n",
                  t, (unsigned)byte, bit, proto_err_name(e));
        }
      }
    }
  }
  CHECK_EQ(other, 0);
  CHECK_EQ(len_seen, (int)PROTO_TYPE_MAX * 16);
  CHECK(crc_seen > 1000);
}

// =============================================================================
//  4. ONE VECTOR PER ProtoErr, its own field the only defect
// =============================================================================
TEST(an_unsupported_protocol_version_is_refused_by_name) {
  ProtoMsg m; mk_msg(m, PT_HELLO);
  uint8_t buf[PROTO_FRAME_MAX];
  const size_t n = encode_ok(m, buf, sizeof buf);
  CHECK_EQ((int)guarded_decode(buf, n, 0u), (int)PE_OK);          // positive control

  for (int v = 0; v < 256; ++v) {
    if (v == (int)PROTO_VERSION) continue;
    buf[0] = (uint8_t)v; reseal(buf, n);
    CHECK_EQ((int)guarded_decode(buf, n, 0u), (int)PE_VERSION);
  }
  // The ENCODER refuses one too, so a build cannot emit a frame its own peer
  // would name PE_VERSION.
  ProtoMsg bad = m; bad.version = (uint8_t)(PROTO_VERSION + 1u);
  size_t out = 1;
  CHECK_EQ((int)proto_encode(bad, buf, sizeof buf, out), (int)PE_VERSION);
  CHECK_EQ((int)out, 0);
}

TEST(a_type_outside_the_twelve_is_refused_by_name) {
  ProtoMsg m; mk_msg(m, PT_GOODBYE);
  uint8_t buf[PROTO_FRAME_MAX];
  const size_t n = encode_ok(m, buf, sizeof buf);
  CHECK_EQ((int)guarded_decode(buf, n, TEST_SESSION), (int)PE_OK);

  // 0 is reserved, which is what stops an all-zero buffer being a frame.
  buf[1] = 0u; reseal(buf, n);
  CHECK_EQ((int)guarded_decode(buf, n, TEST_SESSION), (int)PE_TYPE);
  for (int t = (int)PT_TYPE_COUNT; t < 256; ++t) {
    buf[1] = (uint8_t)t; reseal(buf, n);
    CHECK_EQ((int)guarded_decode(buf, n, TEST_SESSION), (int)PE_TYPE);
  }
  // The all-zero buffer itself, at every plausible length.
  uint8_t zero[PROTO_FRAME_MAX];
  memset(zero, 0, sizeof zero);
  for (size_t k = PROTO_FRAME_MIN; k <= PROTO_FRAME_MAX; ++k)
    CHECK(guarded_decode(zero, k, 0u) != PE_OK);
}

TEST(a_reserved_flag_bit_is_refused_by_name) {
  ProtoMsg m; mk_msg(m, PT_GOODBYE);
  uint8_t buf[PROTO_FRAME_MAX];
  const size_t n = encode_ok(m, buf, sizeof buf);
  for (int b = 1; b < 8; ++b) {
    buf[2] = (uint8_t)(1u << b); reseal(buf, n);
    CHECK_EQ((int)guarded_decode(buf, n, TEST_SESSION), (int)PE_FLAGS);
  }
  buf[2] = (uint8_t)PF_RETX; reseal(buf, n);
  CHECK_EQ((int)guarded_decode(buf, n, TEST_SESSION), (int)PE_OK);
  CHECK_EQ((int)g_arena.m.flags, (int)PF_RETX);
  buf[2] = 0u; reseal(buf, n);
  CHECK_EQ((int)guarded_decode(buf, n, TEST_SESSION), (int)PE_OK);
}

TEST(a_length_that_is_not_this_types_length_is_refused_by_name) {
  // Two types with DIFFERENT lengths, so the frame is otherwise perfect and the
  // only defect is that the length belongs to another message.
  ProtoMsg m; mk_msg(m, PT_ACTION);              // 8 B payload
  uint8_t buf[PROTO_FRAME_MAX];
  size_t n = encode_ok(m, buf, sizeof buf);

  // A 12 B payload frame relabelled as an ACTION: len is legal, the total is
  // consistent, the CRC is right - and it is still not an ACTION.
  ProtoMsg r; mk_msg(r, PT_ROUND_RESULT);        // 12 B payload
  uint8_t rb[PROTO_FRAME_MAX];
  const size_t rn = encode_ok(r, rb, sizeof rb);
  rb[1] = (uint8_t)PT_ACTION; reseal(rb, rn);
  CHECK_EQ((int)guarded_decode(rb, rn, TEST_SESSION), (int)PE_LEN_FOR_TYPE);

  // And a len field that disagrees with the delivered byte count is PE_LEN,
  // which is a different question and a different code.
  buf[12] = 12u; reseal(buf, n);
  CHECK_EQ((int)guarded_decode(buf, n, TEST_SESSION), (int)PE_LEN);
  buf[12] = 8u; reseal(buf, n);
  CHECK_EQ((int)guarded_decode(buf, n, TEST_SESSION), (int)PE_OK);
}

TEST(an_oversized_frame_or_an_oversized_length_field_is_refused_by_name) {
  ProtoMsg m; mk_msg(m, PT_ACTION);
  uint8_t buf[PROTO_FRAME_MAX + 64];
  const size_t n = encode_ok(m, buf, PROTO_FRAME_MAX);

  // (a) the TRANSPORT delivered more bytes than any frame can be.
  memset(buf + n, 0, sizeof buf - n);
  for (size_t k = PROTO_FRAME_MAX + 1u; k <= PROTO_FRAME_MAX + 64u; ++k)
    CHECK_EQ((int)guarded_decode(buf, k, TEST_SESSION), (int)PE_OVERSIZE);

  // (b) the FRAME claims a payload wider than the protocol has. Step 2 catches
  //     it before the CRC and before any use, which is the whole point of
  //     reading `len` first and trusting it never.
  uint8_t b2[PROTO_FRAME_MAX];
  const size_t n2 = encode_ok(m, b2, sizeof b2);
  b2[12] = 0xFFu; b2[13] = 0xFFu; reseal(b2, n2);
  CHECK_EQ((int)guarded_decode(b2, n2, TEST_SESSION), (int)PE_OVERSIZE);
  b2[12] = (uint8_t)((PROTO_PAYLOAD_MAX + 1u) & 0xFFu);
  b2[13] = (uint8_t)(((PROTO_PAYLOAD_MAX + 1u) >> 8) & 0xFFu);
  reseal(b2, n2);
  CHECK_EQ((int)guarded_decode(b2, n2, TEST_SESSION), (int)PE_OVERSIZE);

  // (c) the encoder refuses a buffer it would overrun rather than writing.
  uint8_t tiny[8];
  size_t out = 1;
  CHECK_EQ((int)proto_encode(m, tiny, sizeof tiny, out), (int)PE_OVERSIZE);
  CHECK_EQ((int)out, 0);
  CHECK_EQ((int)proto_encode(m, nullptr, 999u, out), (int)PE_OVERSIZE);
}

TEST(a_round_where_the_type_forbids_one_is_refused_by_name) {
  for (int t = 1; t <= (int)PROTO_TYPE_MAX; ++t) {
    ProtoMsg m; mk_msg(m, (ProtoType)t);
    uint8_t buf[PROTO_FRAME_MAX];
    const size_t n = encode_ok(m, buf, sizeof buf);
    const uint32_t sess = (t == (int)PT_HELLO) ? 0u : TEST_SESSION;
    CHECK_EQ((int)guarded_decode(buf, n, sess), (int)PE_OK);

    if (PROTO_HAS_ROUND[t]) {
      buf[3] = 0u; reseal(buf, n);
      CHECK_EQ((int)guarded_decode(buf, n, sess), (int)PE_ROUND);
      buf[3] = (uint8_t)(BATTLE_MAX_ROUNDS + 1u); reseal(buf, n);
      CHECK_EQ((int)guarded_decode(buf, n, sess), (int)PE_ROUND);
      buf[3] = (uint8_t)BATTLE_MAX_ROUNDS; reseal(buf, n);
      CHECK_EQ((int)guarded_decode(buf, n, sess), (int)PE_OK);
    } else {
      buf[3] = 1u; reseal(buf, n);
      CHECK_EQ((int)guarded_decode(buf, n, sess), (int)PE_ROUND);
    }
    // The encoder holds the same rule, in the same function.
    ProtoMsg bad = m;
    bad.round = PROTO_HAS_ROUND[t] ? 0u : 1u;
    size_t out = 1;
    CHECK_EQ((int)proto_encode(bad, buf, sizeof buf, out), (int)PE_ROUND);
    CHECK_EQ((int)out, 0);
  }
}

TEST(a_frame_for_another_session_is_refused_by_name) {
  for (int t = 1; t <= (int)PROTO_TYPE_MAX; ++t) {
    ProtoMsg m; mk_msg(m, (ProtoType)t);
    uint8_t buf[PROTO_FRAME_MAX];
    const size_t n = encode_ok(m, buf, sizeof buf);
    if (t == (int)PT_HELLO) {
      // HELLO by definition predates the session, so it must carry 0 and an
      // endpoint accepts it whatever session it is in.
      CHECK_EQ((int)guarded_decode(buf, n, 0u), (int)PE_OK);
      CHECK_EQ((int)guarded_decode(buf, n, TEST_SESSION), (int)PE_OK);
      buf[4] = 1u; reseal(buf, n);
      CHECK_EQ((int)guarded_decode(buf, n, 0u), (int)PE_SESSION);
    } else {
      CHECK_EQ((int)guarded_decode(buf, n, TEST_SESSION), (int)PE_OK);
      CHECK_EQ((int)guarded_decode(buf, n, TEST_SESSION ^ 1u), (int)PE_SESSION);
      // An endpoint with no session refuses all eleven by name, which is what
      // the idle state of the FSM needs.
      CHECK_EQ((int)guarded_decode(buf, n, 0u), (int)PE_SESSION);
    }
  }
}

TEST(a_reserved_payload_byte_that_carries_a_value_is_refused_by_name) {
  // Every type has at least one reserved byte or field; each is poked in turn
  // and the frame is otherwise perfect.
  struct Poke { ProtoType t; uint16_t off; };
  static const Poke POKES[] = {
    { PT_HELLO,           8 }, { PT_HELLO,          11 },
    { PT_CAPABILITIES,    3 },
    // SESSION_REQUEST byte 7 (`rules`) and SESSION_ACCEPT byte 8 (`op_echo`)
    // WERE reserved and became REAL FIELDS in P7-C4. They are off this list
    // rather than deleted from the comment: a reserved byte that acquires a
    // meaning has to lose its guard in the same commit, or the guard fails for
    // a frame that is now perfectly legal.
    { PT_SESSION_REQUEST, 8 }, { PT_SESSION_REQUEST, 11 },
    { PT_SESSION_ACCEPT,  9 }, { PT_SESSION_ACCEPT, 11 },
    { PT_TEAM_SUBMIT,     1 },
    { PT_TEAM_VALIDATION, 4 }, { PT_TEAM_VALIDATION, 7 },
    { PT_BATTLE_STATE,    6 }, { PT_BATTLE_STATE,   11 },
    { PT_ACTION,          2 }, { PT_ACTION,          3 },
    { PT_ACTION_RESULT,   3 },
    { PT_ROUND_RESULT,    9 }, { PT_ROUND_RESULT,   11 },
    { PT_BATTLE_END,      3 }, { PT_BATTLE_END,     10 }, { PT_BATTLE_END, 15 },
    { PT_GOODBYE,         1 }, { PT_GOODBYE,         3 },
    { PT_TRADE_OFFER,     0 }, { PT_TRADE_OFFER,     3 },
    { PT_TRADE_CONFIRM,   1 },
    { PT_TRADE_COMMIT,    0 }, { PT_TRADE_COMMIT,    1 },
    // The breeding's reserved bytes, on the same rule: a byte the encoder
    // leaves zero must be REFUSED when it arrives non-zero, or it is a place a
    // future field can be smuggled into without a version change.
    { PT_BREED_OFFER,     0 }, { PT_BREED_OFFER,     3 },
    { PT_BREED_CONFIRM,   1 },
    { PT_BREED_DONE,      1 }
  };
  for (size_t i = 0; i < sizeof POKES / sizeof POKES[0]; ++i) {
    ProtoMsg m; mk_msg(m, POKES[i].t);
    uint8_t buf[PROTO_FRAME_MAX];
    const size_t n = encode_ok(m, buf, sizeof buf);
    const uint32_t sess = (POKES[i].t == PT_HELLO) ? 0u : TEST_SESSION;
    CHECK_EQ((int)guarded_decode(buf, n, sess), (int)PE_OK);       // control
    buf[PROTO_HDR_BYTES + POKES[i].off] = 0x5Au;
    reseal(buf, n);
    const ProtoErr e = guarded_decode(buf, n, sess);
    if (e != PE_RESERVED)
      fprintf(stderr, "    type %d payload byte %u -> %s\n",
              (int)POKES[i].t, (unsigned)POKES[i].off, proto_err_name(e));
    CHECK_EQ((int)e, (int)PE_RESERVED);
  }
  // PT_TRADE_READY IS THE ONE TYPE WITH NO RESERVED PAYLOAD BYTE, and saying so
  // is better than leaving it silently absent from the list above: all four of
  // its bytes are fields (verdict, policy and a 16-bit echo), which is why
  // networking/protocol.h gives the two refusal codes two bytes instead of
  // collapsing them into one enum.
  {
    ProtoMsg m; mk_msg(m, PT_TRADE_READY);
    uint8_t buf[PROTO_FRAME_MAX];
    const size_t n = encode_ok(m, buf, sizeof buf);
    for (uint16_t off = 0; off < 4u; ++off) {
      uint8_t poke[PROTO_FRAME_MAX];
      memcpy(poke, buf, n);
      poke[PROTO_HDR_BYTES + off] = 0x5Au;
      reseal(poke, n);
      CHECK_EQ((int)guarded_decode(poke, n, TEST_SESSION), (int)PE_OK);
    }
  }
}

TEST(a_team_count_that_cannot_index_the_slots_is_refused_by_name) {
  ProtoMsg m; mk_msg(m, PT_TEAM_SUBMIT);
  uint8_t buf[PROTO_FRAME_MAX];
  const size_t n = encode_ok(m, buf, sizeof buf);
  CHECK_EQ((int)guarded_decode(buf, n, TEST_SESSION), (int)PE_OK);

  for (int c = 0; c < 256; ++c) {
    if (c >= 1 && c <= (int)BATTLE_TEAM_MAX) continue;
    buf[PROTO_HDR_BYTES + 0] = (uint8_t)c; reseal(buf, n);
    CHECK_EQ((int)guarded_decode(buf, n, TEST_SESSION), (int)PE_COUNT_RANGE);
  }
  buf[PROTO_HDR_BYTES + 0] = (uint8_t)BATTLE_TEAM_MAX; reseal(buf, n);
  CHECK_EQ((int)guarded_decode(buf, n, TEST_SESSION), (int)PE_OK);

  // The encoder holds it too.
  ProtoMsg bad = m; bad.p.team.count = 0u;
  size_t out = 1;
  CHECK_EQ((int)proto_encode(bad, buf, sizeof buf, out), (int)PE_COUNT_RANGE);
  CHECK_EQ((int)out, 0);
  bad.p.team.count = (uint8_t)(BATTLE_TEAM_MAX + 1u);
  CHECK_EQ((int)proto_encode(bad, buf, sizeof buf, out), (int)PE_COUNT_RANGE);
}

TEST(a_team_slot_above_the_count_that_is_not_empty_is_refused_by_name) {
  // count is authoritative for how many records are decoded; the zero rule is a
  // CROSS-CHECK, which is what makes a stowaway record in slot 2 a named
  // rejection instead of 48 bytes nobody looked at.
  ProtoMsg m; mk_msg(m, PT_TEAM_SUBMIT);
  m.p.team.count = 1u;
  memset(m.p.team.rec[1], 0, BUGW_BYTES);
  memset(m.p.team.rec[2], 0, BUGW_BYTES);
  uint8_t buf[PROTO_FRAME_MAX];
  const size_t n = encode_ok(m, buf, sizeof buf);
  CHECK_EQ((int)guarded_decode(buf, n, TEST_SESSION), (int)PE_OK);

  for (uint8_t slot = 1; slot < (uint8_t)BATTLE_TEAM_MAX; ++slot) {
    for (uint16_t byte = 0; byte < BUGW_BYTES; byte += 7u) {
      uint8_t b2[PROTO_FRAME_MAX];
      memcpy(b2, buf, n);
      b2[PROTO_HDR_BYTES + 4u + slot * BUGW_BYTES + byte] = 0x01u;
      reseal(b2, n);
      CHECK_EQ((int)guarded_decode(b2, n, TEST_SESSION), (int)PE_TEAM_PAD);
    }
  }
}

TEST(every_codec_error_is_reachable_and_has_a_distinct_name) {
  for (int e = 0; e < (int)PE_CODEC_COUNT; ++e) {
    const char* nm = proto_err_name((ProtoErr)e);
    CHECK(nm != nullptr);
    CHECK(strncmp(nm, "PE_", 3) == 0);
    CHECK(strcmp(nm, "PE_?") != 0);
    for (int q = 0; q < e; ++q) CHECK(strcmp(nm, proto_err_name((ProtoErr)q)) != 0);
  }
  CHECK(strcmp(proto_err_name((ProtoErr)PE_CODEC_COUNT), "PE_?") == 0);
  CHECK(strcmp(proto_err_name((ProtoErr)255), "PE_?") == 0);
  // Reachability is what the cases above establish one at a time; this gathers
  // the same vectors so a code that stops being producible fails HERE too.
  bool seen[PE_CODEC_COUNT];
  memset(seen, 0, sizeof seen);
  ProtoMsg m; mk_msg(m, PT_ACTION);
  uint8_t buf[PROTO_FRAME_MAX];
  const size_t n = encode_ok(m, buf, sizeof buf);
  uint8_t b[PROTO_FRAME_MAX + 8];

  seen[(int)guarded_decode(buf, n, TEST_SESSION)] = true;                    // PE_OK
  seen[(int)guarded_decode(buf, 3u, TEST_SESSION)] = true;                   // PE_SHORT
  memcpy(b, buf, n); memset(b + n, 0, 8);
  seen[(int)guarded_decode(b, PROTO_FRAME_MAX + 8u, TEST_SESSION)] = true;   // PE_OVERSIZE
  memcpy(b, buf, n); b[12] = 12u; reseal(b, n);
  seen[(int)guarded_decode(b, n, TEST_SESSION)] = true;                      // PE_LEN
  memcpy(b, buf, n); b[20] ^= 0x10u;
  seen[(int)guarded_decode(b, n, TEST_SESSION)] = true;                      // PE_CRC
  memcpy(b, buf, n); b[0] = 9u; reseal(b, n);
  seen[(int)guarded_decode(b, n, TEST_SESSION)] = true;                      // PE_VERSION
  memcpy(b, buf, n); b[1] = 0u; reseal(b, n);
  seen[(int)guarded_decode(b, n, TEST_SESSION)] = true;                      // PE_TYPE
  memcpy(b, buf, n); b[2] = 0x80u; reseal(b, n);
  seen[(int)guarded_decode(b, n, TEST_SESSION)] = true;                      // PE_FLAGS
  {
    ProtoMsg r; mk_msg(r, PT_ROUND_RESULT);
    uint8_t rb[PROTO_FRAME_MAX];
    const size_t rn = encode_ok(r, rb, sizeof rb);
    rb[1] = (uint8_t)PT_ACTION; reseal(rb, rn);
    seen[(int)guarded_decode(rb, rn, TEST_SESSION)] = true;                  // PE_LEN_FOR_TYPE
  }
  memcpy(b, buf, n); b[3] = 0u; reseal(b, n);
  seen[(int)guarded_decode(b, n, TEST_SESSION)] = true;                      // PE_ROUND
  seen[(int)guarded_decode(buf, n, 0u)] = true;                              // PE_SESSION
  memcpy(b, buf, n); b[PROTO_HDR_BYTES + 2] = 1u; reseal(b, n);
  seen[(int)guarded_decode(b, n, TEST_SESSION)] = true;                      // PE_RESERVED
  {
    ProtoMsg ts; mk_msg(ts, PT_TEAM_SUBMIT);
    uint8_t tb[PROTO_FRAME_MAX];
    const size_t tn = encode_ok(ts, tb, sizeof tb);
    uint8_t t2[PROTO_FRAME_MAX];
    memcpy(t2, tb, tn); t2[PROTO_HDR_BYTES] = 0u; reseal(t2, tn);
    seen[(int)guarded_decode(t2, tn, TEST_SESSION)] = true;                  // PE_COUNT_RANGE
    memcpy(t2, tb, tn); t2[PROTO_HDR_BYTES] = 1u;
    reseal(t2, tn);
    seen[(int)guarded_decode(t2, tn, TEST_SESSION)] = true;                  // PE_TEAM_PAD
  }
  for (int e = 0; e < (int)PE_CODEC_COUNT; ++e) {
    if (!seen[e]) fprintf(stderr, "    unreachable: %s\n", proto_err_name((ProtoErr)e));
    CHECK(seen[e]);
  }
}

TEST(the_ack_field_is_carried_and_never_acted_on) {
  // protocol.h calls `ack` diagnostic. If any decision were built on it, one of
  // these 256 round trips would answer differently from the others.
  for (int i = 0; i < 256; ++i) {
    ProtoMsg m; mk_msg(m, PT_ROUND_RESULT);
    m.ack = (uint16_t)(i * 257);
    uint8_t buf[PROTO_FRAME_MAX];
    const size_t n = encode_ok(m, buf, sizeof buf);
    CHECK_EQ((int)guarded_decode(buf, n, TEST_SESSION), (int)PE_OK);
    CHECK_EQ((int)g_arena.m.ack, (int)m.ack);
    g_arena.m.ack = m.ack;                       // the only field that differed
    expect_same_payload(m, g_arena.m);
  }
}

// =============================================================================
//  5. THE WIRE BUG - spec section 15's reject list, on the record
// =============================================================================
TEST(a_wire_bug_round_trips_and_the_receiver_derives_what_it_was_not_sent) {
  for (uint8_t s = 1; s <= (uint8_t)SPECIES_TABLE_COUNT; ++s) {
    for (uint8_t lv = 1; lv <= (uint8_t)ER_LEVEL_MAX; lv = (uint8_t)(lv + 6u)) {
      BugInstance p; mk_valid(p, s, lv, 0x400u + s * 32u + lv);
      // Fields the wire deliberately drops, set to values that must NOT survive.
      snprintf(p.nickname, sizeof p.nickname, "NOMBRE");
      p.age_s = 999u; p.battles_won = 7u; p.creation_seed = 0xABCDEF01u;
      for (uint8_t c = 0; c < (uint8_t)ER_CARE_COUNT; ++c) p.care[c] = 12345;
      CHECK_EQ((int)validate_bug(p), (int)VR_OK);

      uint8_t rec[BUGW_BYTES];
      pbw_encode(p, rec);
      BugInstance q;
      memset(&q, 0xA5, sizeof q);
      const VReject r = pbw_decode(rec, q);
      if (r != VR_OK) fprintf(stderr, "    species %u level %u -> %s\n",
                              (unsigned)s, (unsigned)lv, validate_reject_name(r));
      CHECK_EQ((int)r, (int)VR_OK);

      // What crossed.
      CHECK_EQ((int)q.species_id, (int)p.species_id);
      CHECK_EQ((long long)q.id, (long long)p.id);
      CHECK_EQ((int)q.level, (int)p.level);
      CHECK_EQ((int)q.xp, (int)p.xp);
      CHECK_EQ((int)q.hp_cur, (int)p.hp_cur);
      CHECK_EQ((int)q.origin, (int)p.origin);
      CHECK_EQ(memcmp(q.moves, p.moves, sizeof q.moves), 0);
      CHECK_EQ(memcmp(&q.genome, &p.genome, sizeof q.genome), 0);

      // What did NOT, and is the receiver's own instead.
      CHECK_EQ((int)q.nickname[0], 0);
      CHECK_EQ((long long)q.age_s, 0LL);
      CHECK_EQ((int)q.battles_won, 0);
      CHECK_EQ((long long)q.creation_seed, 0LL);
      for (uint8_t c = 0; c < (uint8_t)ER_CARE_COUNT; ++c) CHECK_EQ((long long)q.care[c], 0LL);
      CHECK_EQ((int)q.custom_sprite, (int)ER_CUSTOM_SPRITE_NONE);
      CHECK_EQ((int)q.magic, (int)BUG_MAGIC);
      CHECK_EQ((int)q.layout_ver, (int)BUG_LAYOUT_VER);
      // The save seal belongs to persistence/save_manager.cpp and this layer
      // does not link it, so the blob CRC is left for whoever stores it.
      CHECK_EQ((int)q.crc16, 0);

      // evo_state was DERIVED, not believed: the stage bits come from OUR row.
      CHECK_EQ((int)(q.evo_state & EVO_STATE_STAGE_MASK), (int)species_get(s)->stage);
      CHECK_EQ((int)((q.evo_state & EVO_STATE_PENDING) != 0u),
               (int)(evolution_level_ready(q) != 0u));

      // Re-encoding what came back gives the SAME 48 bytes.
      uint8_t again[BUGW_BYTES];
      pbw_encode(q, again);
      CHECK_EQ(memcmp(rec, again, BUGW_BYTES), 0);
    }
  }
}

TEST(the_evolution_state_is_unrepresentable_on_the_wire_rather_than_checked) {
  // The plan asked for "an evo_state that disagrees with its species row"
  // rejected by the right code. IT CANNOT BE SENT: the byte where it would have
  // lived is reserved and must be zero, and the receiver computes the field
  // from its own tables. This case pins BOTH halves of that claim.
  BugInstance mid; mk_valid(mid, 5, 12, 0x501u);       // species 5 is stage 1
  CHECK_EQ((int)species_get(5)->stage, 1);
  uint8_t rec[BUGW_BYTES];
  pbw_encode(mid, rec);
  CHECK_EQ((int)rec[BUGW_OFF_RESERVED0], 0);               // nothing to lie with

  BugInstance q;
  CHECK_EQ((int)pbw_decode(rec, q), (int)VR_OK);
  CHECK_EQ((int)(q.evo_state & EVO_STATE_STAGE_MASK), 1); // derived, not sent

  // A sender whose own stage bits are WRONG still produces a record the
  // receiver reads correctly, because the field never crosses.
  BugInstance liar = mid;
  liar.evo_state = 0u;                                    // a locally broken Bug
  CHECK_EQ((int)validate_bug(liar), (int)VR_BAD_EVO_STAGE);
  uint8_t rec2[BUGW_BYTES];
  pbw_encode(liar, rec2);
  CHECK_EQ(memcmp(rec, rec2, BUGW_BYTES), 0);              // byte-identical record
  CHECK_EQ((int)pbw_decode(rec2, q), (int)VR_OK);
  CHECK_EQ((int)(q.evo_state & EVO_STATE_STAGE_MASK), 1);

  // And poking that reserved byte is a NAMED wire reject, not a field.
  rec2[BUGW_OFF_RESERVED0] = 0x80u;
  {
    const uint16_t c = crc16_ccitt(rec2, (size_t)BUGW_CRC_BYTES);
    rec2[BUGW_OFF_CRC]     = (uint8_t)(c & 0xFFu);
    rec2[BUGW_OFF_CRC + 1] = (uint8_t)((c >> 8) & 0xFFu);
  }
  CHECK_EQ((int)pbw_decode(rec2, q), (int)VR_WIRE_RESERVED);
}

TEST(every_wire_reject_is_reachable_and_the_output_is_never_partially_written) {
  BugInstance base; mk_valid(base, 1, 10, 0x600u);
  uint8_t clean[BUGW_BYTES];
  pbw_encode(base, clean);

  bool seen[VR_REJECT_COUNT];
  memset(seen, 0, sizeof seen);

  BugInstance out;
  uint8_t rec[BUGW_BYTES];
  #define WIRE_CASE(code, mutate)                                             \
    do {                                                                      \
      memcpy(rec, clean, BUGW_BYTES);                                          \
      { mutate; }                                                             \
      memset(&out, 0xA5, sizeof out);                                         \
      const VReject r_ = pbw_decode(rec, out);                                \
      if (r_ != (code)) fprintf(stderr, "    wanted %s got %s\n",              \
                                validate_reject_name(code),                   \
                                validate_reject_name(r_));                    \
      CHECK_EQ((int)r_, (int)(code));                                         \
      seen[(int)r_] = true;                                                   \
      const uint8_t* ob_ = (const uint8_t*)(const void*)&out;                 \
      for (size_t i_ = 0; i_ < sizeof out; ++i_) CHECK_EQ((int)ob_[i_], 0);   \
    } while (0)
  #define RESEAL_REC()                                                        \
    do { const uint16_t c_ = crc16_ccitt(rec, (size_t)BUGW_CRC_BYTES);         \
         rec[BUGW_OFF_CRC] = (uint8_t)(c_ & 0xFFu);                            \
         rec[BUGW_OFF_CRC + 1] = (uint8_t)((c_ >> 8) & 0xFFu); } while (0)

  // The positive control first: the untouched record decodes.
  CHECK_EQ((int)pbw_decode(clean, out), (int)VR_OK);

  WIRE_CASE(VR_WIRE_MAGIC,   rec[BUGW_OFF_MAGIC] ^= 0x01u; RESEAL_REC());
  WIRE_CASE(VR_WIRE_VERSION, rec[BUGW_OFF_WIRE_VER] = 2u; RESEAL_REC());
  WIRE_CASE(VR_WIRE_CRC,     rec[BUGW_OFF_CRC] ^= 0x01u);
  WIRE_CASE(VR_WIRE_RESERVED, rec[BUGW_OFF_RESERVED0] = 1u; RESEAL_REC());
  WIRE_CASE(VR_WIRE_RESERVED, rec[BUGW_OFF_RESERVED + 7] = 1u; RESEAL_REC());
  // A custom species and a custom flag are ONE code, because in P8 they become
  // one meaning and a silent remap then would be the bug.
  WIRE_CASE(VR_WIRE_CUSTOM_UNRESOLVED, rec[BUGW_OFF_SPECIES] = 200u; RESEAL_REC());
  WIRE_CASE(VR_WIRE_CUSTOM_UNRESOLVED, rec[BUGW_OFF_FLAGS] = PBF_CUSTOM; RESEAL_REC());
  WIRE_CASE(VR_WIRE_CUSTOM_UNRESOLVED,
            rec[BUGW_OFF_FLAGS] = PBF_HAS_CUSTOM_SPRITE; RESEAL_REC());
  // The three status bits the wire refuses, each for its own reason.
  WIRE_CASE(VR_WIRE_STATUS_BITS, rec[BUGW_OFF_STATUS] = PBS_FAINTED; RESEAL_REC());
  WIRE_CASE(VR_WIRE_STATUS_BITS, rec[BUGW_OFF_STATUS] = PBS_CORRUPTED; RESEAL_REC());
  WIRE_CASE(VR_WIRE_STATUS_BITS, rec[BUGW_OFF_STATUS] = PBS_RESERVED_LIGHT; RESEAL_REC());

  // THE SIX SECTION 15 VECTORS THE PLAN NAMES, each rejected by the RIGHT code
  // and each produced by the SHARED validator rather than by a wire rule.
  WIRE_CASE(VR_BAD_LEVEL,  rec[BUGW_OFF_LEVEL] = 31u; RESEAL_REC());
  WIRE_CASE(VR_UNKNOWN_SPECIES,
            rec[BUGW_OFF_SPECIES] = (uint8_t)(SPECIES_TABLE_COUNT + 1u); RESEAL_REC());
  WIRE_CASE(VR_UNKNOWN_MOVE, rec[BUGW_OFF_MOVES + 3] = 0u; RESEAL_REC());
  WIRE_CASE(VR_UNLEARNABLE_MOVESET, rec[BUGW_OFF_MOVES] = MV_PLAGA; RESEAL_REC());
  WIRE_CASE(VR_HP_OVER_MAX,
            rec[BUGW_OFF_HP_CUR] = 0xFFu; rec[BUGW_OFF_HP_CUR + 1] = 0xFFu; RESEAL_REC());
  WIRE_CASE(VR_BAD_GENOME, rec[BUGW_OFF_GENOME + 14] ^= 0x01u; RESEAL_REC());
  // And three more the same call site owns.
  WIRE_CASE(VR_NULL_ID, memset(rec + BUGW_OFF_ID, 0, 4); RESEAL_REC());
  WIRE_CASE(VR_BAD_XP,
            rec[BUGW_OFF_XP] = 0xFFu; rec[BUGW_OFF_XP + 1] = 0xFFu; RESEAL_REC());
  WIRE_CASE(VR_BAD_ORIGIN, rec[BUGW_OFF_ORIGIN] = (uint8_t)ORIGIN_COUNT; RESEAL_REC());
  WIRE_CASE(VR_BAD_TRAIT, rec[BUGW_OFF_TRAIT] = 1u; RESEAL_REC());
  #undef WIRE_CASE
  #undef RESEAL_REC

  // ALL SIX wire codes were produced, and by this function alone
  // (tests/test_validate.cpp asserts validate_bug() never produces one).
  for (int r = (int)VR_WIRE_MAGIC; r <= (int)VR_WIRE_STATUS_BITS; ++r) {
    if (!seen[r]) fprintf(stderr, "    unreachable: %s\n",
                          validate_reject_name((VReject)r));
    CHECK(seen[r]);
  }
}

TEST(a_team_submit_carrying_a_bad_member_is_refused_by_the_right_code) {
  // The plan's TEAM_SUBMIT list, driven end to end: a legal frame whose only
  // defect is inside ONE 48 B record, decoded exactly as the session layer will
  // decode it - straight into a caller-owned BugInstance.
  struct Vec { const char* what; uint16_t off; uint8_t val; VReject want; };
  static const Vec V[] = {
    { "level 31",        BUGW_OFF_LEVEL,     31u, VR_BAD_LEVEL },
    { "unknown species", BUGW_OFF_SPECIES,   99u, VR_UNKNOWN_SPECIES },
    { "three moves",     BUGW_OFF_MOVES + 3,  0u, VR_UNKNOWN_MOVE },
    { "a stolen move",   BUGW_OFF_MOVES,   MV_PLAGA, VR_UNLEARNABLE_MOVESET },
    { "hp over max",     BUGW_OFF_HP_CUR,  0xFFu, VR_HP_OVER_MAX },
    { "a bad genome",    BUGW_OFF_GENOME + 14, 0x00u, VR_BAD_GENOME }
  };
  for (size_t i = 0; i < sizeof V / sizeof V[0]; ++i) {
    ProtoMsg m; mk_msg(m, PT_TEAM_SUBMIT);
    // The defect goes in the SECOND member, so a decoder that stopped at the
    // first would pass this case without ever seeing it.
    m.p.team.rec[1][V[i].off] = V[i].val;
    if (V[i].off == BUGW_OFF_HP_CUR) m.p.team.rec[1][BUGW_OFF_HP_CUR + 1] = 0xFFu;
    const uint16_t c = crc16_ccitt(m.p.team.rec[1], (size_t)BUGW_CRC_BYTES);
    m.p.team.rec[1][BUGW_OFF_CRC]     = (uint8_t)(c & 0xFFu);
    m.p.team.rec[1][BUGW_OFF_CRC + 1] = (uint8_t)((c >> 8) & 0xFFu);

    uint8_t buf[PROTO_FRAME_MAX];
    const size_t n = encode_ok(m, buf, sizeof buf);
    // THE FRAME IS PERFECT. Only the game object inside it is not, which is
    // exactly the split spec section 15 draws between "malformed packets" and
    // "impossible stats".
    CHECK_EQ((int)guarded_decode(buf, n, TEST_SESSION), (int)PE_OK);

    BugInstance team[BATTLE_TEAM_MAX];
    VReject worst = VR_OK;
    uint8_t bad_at = 0xFFu;
    for (uint8_t k = 0; k < g_arena.m.p.team.count; ++k) {
      const VReject r = pbw_decode(g_arena.m.p.team.rec[k], team[k]);
      if (r != VR_OK && worst == VR_OK) { worst = r; bad_at = k; }
    }
    if (worst != V[i].want)
      fprintf(stderr, "    %s -> %s\n", V[i].what, validate_reject_name(worst));
    CHECK_EQ((int)worst, (int)V[i].want);
    CHECK_EQ((int)bad_at, 1);
    // Member 0 was fine, so the rejection is about the member and not the frame.
    CHECK_EQ((int)pbw_decode(g_arena.m.p.team.rec[0], team[0]), (int)VR_OK);
  }
}

// =============================================================================
//  6. THE STRUCTURED FUZZER
//
//  Every frame is a pure function of ONE printable u32 drawn through
//  core/rng.h, so a failure reproduces from the seed alone and nothing here
//  depends on the machine's clock or on malloc.
// =============================================================================
#define FUZZ_ITERS  20000

TEST(a_structured_fuzzer_finds_no_frame_that_is_accepted_and_malformed) {
  Rng rng; rng_init(rng, nt_state().seed ^ 0x50524F54u);

  int accepted = 0, rejected = 0, clean_arm = 0;
  int by_code[PE_CODEC_COUNT];
  memset(by_code, 0, sizeof by_code);

  for (int it = 0; it < FUZZ_ITERS; ++it) {
    const uint32_t draw = rng_next(rng);
    const ProtoType t   = (ProtoType)(1u + (draw % (uint32_t)PROTO_TYPE_MAX));
    ProtoMsg m; mk_msg(m, t);
    m.seq   = (uint16_t)rng_next(rng);
    m.ack   = (uint16_t)rng_next(rng);
    m.flags = (uint8_t)((rng_next(rng) & 1u) ? PF_RETX : 0u);
    if (PROTO_HAS_ROUND[(int)t])
      m.round = (uint8_t)(1u + rng_next_below(rng, (uint32_t)BATTLE_MAX_ROUNDS));

    uint8_t buf[PROTO_FRAME_MAX + 16];
    size_t n = 0;
    CHECK_EQ((int)proto_encode(m, buf, PROTO_FRAME_MAX, n), (int)PE_OK);
    const uint32_t sess = (t == PT_HELLO) ? 0u : TEST_SESSION;

    // EXACTLY ONE mutation per iteration, so the code the decoder returns can
    // be attributed. Kinds 5..10 reseal the CRC, which is what lets the fuzzer
    // reach steps 5 to 11 of the decode order at all - without it every
    // mutation would stop at PE_CRC and the deeper guards would never run.
    const uint32_t kind = rng_next_below(rng, 11u);
    size_t n_use = n;
    switch (kind) {
      case 0: break;                                              // untouched
      case 1: {                                                   // one bit
        const uint32_t b = rng_next_below(rng, (uint32_t)n);
        buf[b] ^= (uint8_t)(1u << (rng_next(rng) & 7u));
        break;
      }
      case 2: n_use = (size_t)rng_next_below(rng, (uint32_t)n); break;   // truncate
      case 3: {                                                   // over-extend
        const uint32_t extra = 1u + rng_next_below(rng, 16u);
        memset(buf + n, (uint8_t)rng_next(rng), extra);
        n_use = n + extra;
        break;
      }
      case 4: {                                                   // len lies
        const uint16_t lie = (uint16_t)rng_next_below(rng, 300u);
        buf[12] = (uint8_t)(lie & 0xFFu); buf[13] = (uint8_t)(lie >> 8);
        break;
      }
      case 5:  buf[1] = (uint8_t)rng_next(rng);  reseal(buf, n); break;  // type
      case 6:  buf[0] = (uint8_t)rng_next(rng);  reseal(buf, n); break;  // version
      case 7:  buf[2] = (uint8_t)rng_next(rng);  reseal(buf, n); break;  // flags
      case 8:  buf[3] = (uint8_t)rng_next(rng);  reseal(buf, n); break;  // round
      case 9:  buf[4] = (uint8_t)rng_next(rng);  reseal(buf, n); break;  // session
      default: {                                                  // a payload byte
        const uint16_t len = PROTO_LEN_OF[(int)t];
        if (len > 0u) {
          const uint32_t o = rng_next_below(rng, (uint32_t)len);
          buf[PROTO_HDR_BYTES + o] = (uint8_t)rng_next(rng);
          reseal(buf, n);
        }
        break;
      }
    }

    // DECODED OUT OF A HEAP BLOCK OF EXACTLY n_use BYTES, and this is the one
    // sweep in the file where that is load-bearing. Kinds 5 to 10 RESEAL the
    // CRC, so a frame with a FORGED TYPE reaches step 11 and the decoder reads
    // the payload of the type the byte now claims - a TEAM_SUBMIT's 148 bytes
    // out of a 20-byte GOODBYE frame, if step 8 were not there to stop it. Over
    // a full-size stack array that read lands inside the array and no sanitizer
    // sees it; over an exact block ASan's redzone sits on the first illegal
    // byte. MEASURED with step 8 (len == PROTO_LEN_OF[type]) deleted: the
    // ordinary build catches the mutation by name through
    // a_length_that_is_not_this_types_length_is_refused_by_name and reports NO
    // memory error at all, and this case under -fsanitize=address reports
    // "heap-buffer-overflow ... READ of size 1 ... in proto_decode
    // protocol.cpp:433" from inside this loop.
    uint8_t* exact = new uint8_t[n_use ? n_use : 1u];
    memcpy(exact, buf, n_use);
    const ProtoErr e = guarded_decode(exact, n_use, sess);
    delete[] exact;
    by_code[(int)e]++;
    if (e == PE_OK) {
      accepted++;
      if (kind == 0) clean_arm++;
      // AN ACCEPTED FRAME MUST RE-ENCODE TO ITSELF. This is what makes "the
      // decoder accepted it" a claim about the bytes rather than about a struct
      // it happened to fill.
      uint8_t again[PROTO_FRAME_MAX];
      size_t n2 = 0;
      CHECK_EQ((int)proto_encode(g_arena.m, again, sizeof again, n2), (int)PE_OK);
      CHECK_EQ((int)n2, (int)n_use);
      if (memcmp(again, buf, n_use) != 0)
        fprintf(stderr, "    iteration %d (kind %u) re-encoded differently\n",
                it, (unsigned)kind);
      CHECK_EQ(memcmp(again, buf, n_use), 0);
    } else {
      rejected++;
    }
  }

  // THE UNTOUCHED ARM IS ACCEPTED IN FULL. Without this a decoder that refused
  // everything would satisfy every assertion above it.
  CHECK(clean_arm > FUZZ_ITERS / 20);
  CHECK_EQ(by_code[(int)PE_OK], accepted);
  CHECK(rejected > FUZZ_ITERS / 4);
  // Every rejection code the fuzzer can structurally reach was reached, so the
  // sweep is not eleven mutations that all land on PE_CRC.
  const int MUST[] = { (int)PE_SHORT, (int)PE_OVERSIZE, (int)PE_LEN, (int)PE_CRC,
                       (int)PE_VERSION, (int)PE_TYPE, (int)PE_FLAGS,
                       (int)PE_ROUND, (int)PE_SESSION, (int)PE_RESERVED };
  for (size_t i = 0; i < sizeof MUST / sizeof MUST[0]; ++i) {
    if (by_code[MUST[i]] == 0)
      fprintf(stderr, "    fuzzer never produced %s\n", proto_err_name((ProtoErr)MUST[i]));
    CHECK(by_code[MUST[i]] > 0);
  }
}

// =============================================================================
//  8. THE ENCODER'S OWN CLAIM
//
//  networking/protocol.h says of proto_encode(): "The encoder applies the SAME
//  type, round and count rules as the decoder, so a message this function
//  accepts is one the decoder accepts", and cites this case by name. THE CASE
//  DID NOT EXIST until the P4-C5 follow-up, and when it was written it FAILED:
//  a HELLO carrying a nonzero session encoded PE_OK and was then refused by
//  proto_decode() step 10 for EVERY expect_session, because HELLO predates the
//  session and must carry 0. That is the class this project keeps finding - a
//  sentence wider than the tree, here with a citation for cover - and the fix
//  was to add the missing ENCODER rule rather than to narrow the sentence.
//
//  IT IS SWEPT AND THEN FUZZED, and the fuzz half is load-bearing: the sweep
//  uses mk_msg()'s own session and would never have produced the one message
//  the rule is about.
// =============================================================================
static void encoder_agrees(const ProtoMsg& m, int* accepted, int* refused)
{
  uint8_t buf[PROTO_FRAME_MAX];
  size_t  n = 0;
  const ProtoErr e = proto_encode(m, buf, sizeof buf, n);
  if (e != PE_OK) {
    CHECK((int)e < (int)PE_CODEC_COUNT);        // a NAMED refusal, always
    CHECK_EQ((int)n, 0);                        // n_out is 0 on every reject
    (*refused)++;
    return;
  }
  (*accepted)++;
  // THE CLAIM. `expect_session` is the session the message names, which is what
  // an endpoint holding that session passes; a HELLO names 0 and so does the
  // endpoint that has no session yet.
  const ProtoErr d = guarded_decode(buf, n, m.session);
  if (d != PE_OK)
    fprintf(stderr, "    encoder emitted type %d session %08lX that its decoder "
                    "refused with %s\n",
            (int)m.type, (unsigned long)m.session, proto_err_name(d));
  CHECK_EQ((int)d, (int)PE_OK);
  if (d != PE_OK) return;
  uint8_t again[PROTO_FRAME_MAX];
  size_t  n2 = 0;
  CHECK_EQ((int)proto_encode(g_arena.m, again, sizeof again, n2), (int)PE_OK);
  CHECK_EQ((int)n2, (int)n);
  CHECK_EQ(memcmp(again, buf, n), 0);
}

TEST(every_frame_the_encoder_emits_its_own_decoder_accepts) {
  int accepted = 0, refused = 0;

  // (a) THE SWEEP: every type, over the header fields a caller can choose.
  static const uint32_t SESSIONS[] = { 0u, 1u, TEST_SESSION, 0xFFFFFFFFu };
  for (int t = 1; t <= (int)PROTO_TYPE_MAX; ++t) {
    for (size_t si = 0; si < sizeof SESSIONS / sizeof SESSIONS[0]; ++si) {
      for (int fl = 0; fl < 2; ++fl) {
        for (int rd = 0; rd <= (int)BATTLE_MAX_ROUNDS; ++rd) {
          ProtoMsg m; mk_msg(m, (ProtoType)t);
          m.session = SESSIONS[si];
          m.flags   = (uint8_t)(fl ? PF_RETX : 0u);
          m.round   = (uint8_t)rd;
          encoder_agrees(m, &accepted, &refused);
        }
      }
    }
  }

  // (b) THE FUZZ: the header is drawn, so the combinations the sweep's own
  //     fixtures never build are reached. One printable seed, as everywhere.
  Rng rng; rng_init(rng, nt_state().seed ^ 0x454E4344u);
  for (int it = 0; it < 40000; ++it) {
    const uint32_t d0 = rng_next(rng);
    const ProtoType t = (ProtoType)(1u + (d0 % (uint32_t)PROTO_TYPE_MAX));
    ProtoMsg m; mk_msg(m, t);
    m.session = rng_next(rng);
    m.seq     = (uint16_t)rng_next(rng);
    m.ack     = (uint16_t)rng_next(rng);
    // DRAWN THROUGH rng_next_below RATHER THAN AS A LOW BYTE. Measured while
    // writing this: `(uint8_t)rng_next(rng)` for `round` produced ZERO zeroes in
    // 40,000 draws on this xorshift32, so the fuzz arm never built the one
    // message the case exists for - a HELLO, whose round MUST be 0 to get past
    // the round rule at all. A low byte is not a uniform small integer.
    m.flags   = (uint8_t)rng_next_below(rng, 8u);
    m.round   = (uint8_t)rng_next_below(rng, 40u);
    if (t == PT_TEAM_SUBMIT) m.p.team.count = (uint8_t)rng_next_below(rng, 6u);
    if (t == PT_SESSION_REQUEST) m.p.sreq.rules = (uint8_t)rng_next_below(rng, 3u);
    encoder_agrees(m, &accepted, &refused);
  }

  // NOT VACUOUS IN EITHER DIRECTION: an encoder that refused everything would
  // satisfy the claim above and prove nothing, and one that accepted everything
  // would mean the sweep never exercised a rule.
  CHECK(accepted > 1000);
  CHECK(refused  > 1000);

  // (c) THE RULE THE CASE WAS WRITTEN FOR, with its positive control. HELLO
  //     carries session 0 BY DEFINITION - proto_decode() step 10 refuses any
  //     other value whatever session the endpoint is in - so the encoder must
  //     refuse it too, by the same name.
  {
    ProtoMsg m; mk_msg(m, PT_HELLO);
    uint8_t buf[PROTO_FRAME_MAX]; size_t n = 0;
    CHECK_EQ((int)m.session, 0);
    CHECK_EQ((int)proto_encode(m, buf, sizeof buf, n), (int)PE_OK);   // control
    m.session = 0x11223344u;
    n = 12345u;
    CHECK_EQ((int)proto_encode(m, buf, sizeof buf, n), (int)PE_SESSION);
    CHECK_EQ((int)n, 0);
  }
}
