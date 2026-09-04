// =============================================================================
//  PEBBLEBOL - networking/session.cpp
//  The handshake half of the FSM. See networking/session.h for the four rules,
//  the ladder and the sequence window, and docs/protocol.md for the state table.
//
//  NO FILE-SCOPE MUTABLE STATE, no heap, no clock, no RNG draw, no Arduino, no
//  radio. Every send goes through session_emit() and every terminal through
//  session_close(), so there is exactly one place that counts a frame and
//  exactly one place that writes the abort record.
// =============================================================================
#include "session.h"

#include <string.h>

#include "../core/crc16.h"
#include "../data/content_version.h"
#include "battle_link.h"

// -----------------------------------------------------------------------------
//  THE EVENT RING - the BattleLog idiom, including the saturating `dropped`.
// -----------------------------------------------------------------------------
void link_log_init(LinkLog& l, LinkEvent* buf, uint16_t cap)
{
  l.ev = buf; l.cap = (buf != nullptr) ? cap : 0u;
  l.head = 0u; l.count = 0u; l.dropped = 0u;
}

const LinkEvent* link_log_at(const LinkLog& l, uint16_t i)
{
  if (l.ev == nullptr || i >= l.count) return nullptr;
  const uint16_t oldest = (uint16_t)((l.head + l.cap - l.count) % l.cap);
  return &l.ev[(uint16_t)((oldest + i) % l.cap)];
}

void session_note(Session& s, LinkEventKind k, uint8_t type, uint8_t code,
                  uint16_t round, uint16_t seq)
{
  LinkLog* l = s.llog;
  if (l == nullptr || l->ev == nullptr || l->cap == 0u) return;
  LinkEvent& e = l->ev[l->head];
  e.kind = (uint8_t)k; e.type = type; e.state = s.state; e.code = code;
  e.round = round; e.seq = seq;
  l->head = (uint16_t)((l->head + 1u) % l->cap);
  if (l->count < l->cap) l->count++;
  else if (l->dropped < 0xFFFFu) l->dropped++;
}

// -----------------------------------------------------------------------------
//  THE TWO DERIVATIONS. FNV-1a 32 over the words in a FIXED ORDER, byte by
//  byte: both endpoints must compute the same number from the same inputs
//  without one extra round trip, and a fold that depended on argument order
//  would give the two sides different answers.
// -----------------------------------------------------------------------------
static uint32_t fnv_u32(uint32_t h, uint32_t v)
{
  for (uint8_t i = 0; i < 4u; ++i) {
    h ^= (uint32_t)((v >> (8u * i)) & 0xFFu);
    h *= 16777619u;
  }
  return h;
}

uint32_t session_derive_id(uint32_t id_lo, uint32_t id_hi, uint32_t hello_nonce)
{
  // The caller passes min() and max(), so both endpoints feed the same order.
  uint32_t h = 2166136261u;
  h = fnv_u32(h, id_lo);
  h = fnv_u32(h, id_hi);
  h = fnv_u32(h, hello_nonce);
  return (h == 0u) ? 1u : h;      // 0 is "no session" on the wire
}

uint32_t session_derive_seed(uint32_t session, uint32_t nonce_a, uint32_t nonce_b,
                             uint16_t crc_a, uint16_t crc_b)
{
  // BOTH nonces AND BOTH team CRCs, so neither side fixes the seed alone. It
  // does NOT stop a modified responder that delays its TEAM_SUBMIT until the
  // initiator's has arrived and then grinds its own team - see the LIMITS
  // section of docs/protocol.md, and note that no commit-reveal built on this
  // tree's hashes (CRC-16 and FNV-1a-32, neither one-way) would fix it.
  uint32_t h = 2166136261u;
  h = fnv_u32(h, session);
  h = fnv_u32(h, nonce_a);
  h = fnv_u32(h, nonce_b);
  h = fnv_u32(h, (uint32_t)crc_a | ((uint32_t)crc_b << 16));
  return h;
}

uint16_t session_team_crc(const uint8_t rec[BATTLE_TEAM_MAX][PBW_BYTES])
{
  // Over the WHOLE slot area exactly as TEAM_SUBMIT carries it, padding
  // included - so the number both sides compare is a function of the bytes on
  // the wire and not of a second serialisation.
  return crc16_ccitt(&rec[0][0], (size_t)BATTLE_TEAM_MAX * PBW_BYTES);
}

// -----------------------------------------------------------------------------
//  NAMES (English, for the event log and the host tests; core/strings_es.h owns
//  the Spanish a player sees).
// -----------------------------------------------------------------------------
static const char* const SS_NAMES[] = {
  "SS_IDLE", "SS_HELLO", "SS_CAPS", "SS_SESSION", "SS_TEAM", "SS_VERIFY",
  "SS_BATTLE", "SS_ENDING", "SS_CLOSED"
};
static_assert(sizeof(SS_NAMES) / sizeof(SS_NAMES[0]) == (size_t)SS_STATE_COUNT,
              "a SessionState was added without its name");

static const char* const SE_NAMES[] = {
  "SE_NONE", "SE_DONE", "SE_LOST", "SE_DESYNC", "SE_REJECTED", "SE_PROTOCOL",
  "SE_INCOMPATIBLE", "SE_LOCAL_CANCEL"
};
static_assert(sizeof(SE_NAMES) / sizeof(SE_NAMES[0]) == (size_t)SE_REASON_COUNT,
              "a SessionEndReason was added without its name");

static const char* const SD_NAMES[] = {
  "SD_NONE", "SD_SELF", "SD_BASIS_SKEW", "SD_TEAM_CHANGED", "SD_ACTION_CHANGED",
  "SD_OPEN_HASH", "SD_INPUTS", "SD_RULES", "SD_RESTATED", "SD_PROBE",
  "SD_ROUND_GAP", "SD_SETUP", "SD_END_DISAGREE", "SD_PEER_GOODBYE",
  "SD_INTERNAL", "SD_BO_ABORT", "SD_ACTION_REJECT", "SD_RX_BUDGET",
  "SD_TX_BUDGET", "SD_BAND", "SD_VERDICT"
};
static_assert(sizeof(SD_NAMES) / sizeof(SD_NAMES[0]) == (size_t)SD_DETAIL_COUNT,
              "a SessionDetail was added without its name");

const char* session_state_name(SessionState st)
{ return ((uint8_t)st < (uint8_t)SS_STATE_COUNT) ? SS_NAMES[(uint8_t)st] : "SS_?"; }
const char* session_reason_name(SessionEndReason r)
{ return ((uint8_t)r < (uint8_t)SE_REASON_COUNT) ? SE_NAMES[(uint8_t)r] : "SE_?"; }
const char* session_detail_name(SessionDetail d)
{ return ((uint8_t)d < (uint8_t)SD_DETAIL_COUNT) ? SD_NAMES[(uint8_t)d] : "SD_?"; }

// -----------------------------------------------------------------------------
//  SENDING, CLOSING AND THE LADDER
// -----------------------------------------------------------------------------
void session_prepare(Session& s, ProtoMsg& m, ProtoType t, uint8_t round)
{
  // A FRESH seq on every frame, retransmissions included - see the sequence
  // window paragraph in session.h.
  if (s.tx_seq < 0xFFFFu) s.tx_seq++;
  proto_msg_init(m, t, (t == PT_HELLO) ? 0u : s.id, s.tx_seq);
  m.round = round;
}

bool session_emit(Session& s, ProtoMsg& m)
{
  if (s.state == (uint8_t)SS_CLOSED || s.tp == nullptr) return false;
  if (s.tx_budget == 0u) { session_close(s, SE_PROTOCOL, (uint8_t)SD_TX_BUDGET); return false; }
  s.tx_budget--;

  uint8_t  buf[PROTO_FRAME_MAX];
  size_t   n  = 0;
  const ProtoErr e = proto_encode(m, buf, sizeof buf, n);
  if (e != PE_OK) {
    // The encoder applies the same rules as the decoder, so this is a bug in
    // this file and never a peer capability - it is reported as internal.
    session_note(s, LEK_CODEC_REJECT, m.type, (uint8_t)e, m.round, m.seq);
    session_close(s, SE_PROTOCOL, (uint8_t)SD_INTERNAL);
    return false;
  }
  if (s.end.tx_frames < 0xFFFFu) s.end.tx_frames++;
  s.end.seq = m.seq;
  session_note(s, (m.flags & PF_RETX) ? LEK_RETX : LEK_TX, m.type, 0u, m.round, m.seq);
  // A refused send is indistinguishable from a dropped frame ON PURPOSE: the
  // ladder covers both, which is what makes a loopback drop and a radio
  // failure exercise the identical path (networking/transport.h).
  (void)transport_send(*s.tp, buf, (uint16_t)n);
  return true;
}

void session_goto(Session& s, SessionState to)
{
  s.state = (uint8_t)to;
  session_note(s, LEK_STATE, (uint8_t)to, 0u, s.st ? s.st->round : 0u, 0u);
}

// R4: THE ONLY THING THAT RESETS THE LADDER. A duplicate, a re-acknowledgement,
// a stale frame and a wrong-state frame all leave it where it was.
void session_progress(Session& s)
{
  s.tries  = 0u;
  s.due_ms = s.now_ms + PROTO_RETX_MS;
}

void session_close(Session& s, SessionEndReason r, uint8_t detail)
{
  if (s.state == (uint8_t)SS_CLOSED) return;      // the first terminal wins

  // AGREEING IS FINAL. The ONE place that makes session_rewards_authorised()
  // and SessionEnd.reason agree, so the header's "true only after SE_DONE" is a
  // structural property of this function rather than a habit of its callers.
  //
  // s.paid is set in battle_link.cpp's compare_end() and NOWHERE ELSE: OUR
  // comparison of the peer's outcome AND final hash against our own, after our
  // own engine has finished the battle. Nothing that happens afterwards can
  // un-decide that - the peer has already agreed, and a later BATTLE_END saying
  // SE_DESYNC contradicts what it said itself. What was reachable before this
  // line: a peer that had made us pay could then CHOOSE our terminal label
  // (SE_DESYNC or SE_LOST from its own reason byte), and a flood after the
  // agreement could close us SE_PROTOCOL / SD_RX_BUDGET while paid. In every
  // case the battle was over, agreed and correctly paid, and only the label was
  // wrong - so the label is what is corrected. Refusing to pay instead would
  // hand an attacker a free denial for the price of one frame.
  //
  // The superseded reason is not recorded: SessionEnd has one reason field, and
  // what happened afterwards is in the counters (rx_reject, rx_stale, rx_ok).
  if (s.paid != 0u) { r = SE_DONE; detail = (uint8_t)SD_NONE; }

  s.end.reason  = (uint8_t)r;
  s.end.state   = s.state;
  s.end.role    = s.role;
  s.end.detail  = detail;
  s.end.session = s.id;
  s.end.tx_retx = s.tries;
  s.end.waiting_for = s.waiting_for;
  if (s.st != nullptr && s.st->phase == (uint8_t)BP_RUNNING) {
    s.end.local_hash = battle_state_hash(*s.st);
    s.end.round      = s.st->round;
    s.end.outcome    = s.st->outcome;
  }
  session_note(s, LEK_END, (uint8_t)r, detail, s.end.round, s.end.seq);
  s.state = (uint8_t)SS_CLOSED;
}

// -----------------------------------------------------------------------------
//  THE SEQUENCE WINDOW. Three answers, all three counted by name.
// -----------------------------------------------------------------------------
enum SeqVerdict : uint8_t { SQ_NEW = 0, SQ_DUP, SQ_STALE };

static SeqVerdict seq_check(Session& s, uint16_t seq)
{
  if (s.rx_seq_seen == 0u) {
    // THE BASELINE IS BOUNDED BY THE SAME RULE, from an implicit last of 0.
    // Without this line the bound below covered only jumps from an ESTABLISHED
    // last, and the very first frame set that last to whatever it claimed - so
    // the one-packet deafness was still available BEFORE the peer had spoken,
    // through a HELLO, which needs no session id at all. An honest peer's first
    // frame is seq 1 and the furthest it can get before it hears from us is its
    // own ladder, which is under ten.
    if (seq > (uint16_t)SESSION_SEQ_MAX_JUMP) return SQ_STALE;
    s.rx_seq_seen = 1u; s.rx_seq_last = seq; s.rx_seq_bits = 0u;
    return SQ_NEW;
  }
  if (seq == s.rx_seq_last) return SQ_DUP;
  if (seq > s.rx_seq_last) {
    const uint16_t adv = (uint16_t)(seq - s.rx_seq_last);
    // A jump no honest peer can make does not move the window. See session.h:
    // without this one forged frame deafens the endpoint permanently.
    if (adv > (uint16_t)SESSION_SEQ_MAX_JUMP) return SQ_STALE;
    if (adv > 1u && s.end.rx_gap < 0xFFFFu) s.end.rx_gap++;   // lost or overtaken
    if (adv >= 32u) s.rx_seq_bits = 0u;
    else s.rx_seq_bits = (uint32_t)((s.rx_seq_bits << adv) | (1u << (adv - 1u)));
    s.rx_seq_last = seq;
    return SQ_NEW;
  }
  const uint16_t back = (uint16_t)(s.rx_seq_last - seq);
  if (back > 32u) return SQ_STALE;
  const uint32_t bit = (uint32_t)1u << (back - 1u);
  if ((s.rx_seq_bits & bit) != 0u) return SQ_DUP;
  s.rx_seq_bits |= bit;
  return SQ_NEW;                              // an out-of-order fill, not a repeat
}

// -----------------------------------------------------------------------------
//  CAPABILITIES
// -----------------------------------------------------------------------------
static void caps_fill(ProtoCaps& c)
{
  c.wire_ver    = (uint8_t)PBW_LAYOUT_VER;
  c.team_max    = (uint8_t)BATTLE_TEAM_MAX;
  c.level_max   = (uint8_t)PB_LEVEL_MAX;
  c.engine_ver  = (uint16_t)BATTLE_ENGINE_VER;
  c.hash_ver    = (uint16_t)BATTLE_HASH_VERSION;
  c.content_ver = (uint16_t)CONTENT_VERSION;
  c.max_payload = (uint16_t)PROTO_PAYLOAD_MAX;
  // THE BASIS ALONGSIDE THE WORDS, and the pair catches two different things
  // for free: the words catch a nameable content or engine skew, and WORDS
  // EQUAL BUT BASIS DIFFERENT catches a build where a version word failed to
  // reach the hash - which is exactly the bug the P4-C2/C3 follow-up fixed and
  // which nothing else in this protocol can see.
  c.hash_basis  = battle_hash_basis((uint32_t)BATTLE_ENGINE_VER,
                                    (uint32_t)BATTLE_HASH_VERSION,
                                    (uint32_t)CONTENT_VERSION);
}

static SessionCapWord caps_disagreement(const ProtoCaps& them)
{
  ProtoCaps us; caps_fill(us);
  if (them.wire_ver    != us.wire_ver)    return SCW_WIRE_VER;
  if (them.team_max    != us.team_max)    return SCW_TEAM_MAX;
  if (them.level_max   != us.level_max)   return SCW_LEVEL_MAX;
  if (them.engine_ver  != us.engine_ver)  return SCW_ENGINE_VER;
  if (them.hash_ver    != us.hash_ver)    return SCW_HASH_VER;
  if (them.content_ver != us.content_ver) return SCW_CONTENT_VER;
  if (them.max_payload != us.max_payload) return SCW_PAYLOAD_MAX;
  if (them.hash_basis  != us.hash_basis)  return SCW_HASH_BASIS;
  return SCW_NONE;
}

// -----------------------------------------------------------------------------
//  LIFECYCLE
// -----------------------------------------------------------------------------
void session_init(Session& s, const SessionCfg& cfg)
{
  memset(&s, 0, sizeof s);
  s.tp     = cfg.tp;
  s.setup  = cfg.setup;
  s.st     = cfg.st;
  s.blog   = cfg.blog;
  s.llog   = cfg.llog;
  s.device_id   = cfg.device_id;
  s.nonce_local = cfg.nonce;
  s.lvl_lo = cfg.lvl_lo;
  s.lvl_hi = cfg.lvl_hi;
  s.role   = (uint8_t)SR_UNSET;
  s.state  = (uint8_t)SS_IDLE;
  s.rx_budget = (uint16_t)SESSION_MAX_RX;
  s.tx_budget = (uint16_t)SESSION_MAX_TX;
  s.end.bad_index = 0xFFu;
  if (s.setup != nullptr) battle_setup_clear(*s.setup);
  if (s.st != nullptr) memset(s.st, 0, sizeof *s.st);
}

VReject session_set_team(Session& s, const PebbleInstance* m, uint8_t count,
                         uint8_t& bad_index)
{
  bad_index = 0xFFu;
  memset(s.my_rec, 0, sizeof s.my_rec);
  s.my_count = 0u;
  const VReject r = validate_team(m, count, bad_index);
  if (r != VR_OK) return r;
  // BATTLE ELIGIBILITY, CHECKED ON OUR OWN TEAM BEFORE A FRAME IS SENT. It is
  // not Pebble validity (game/validate.h), so it cannot live in the shared body
  // - and without it a player whose own team held a fainted Pebble got VR_OK
  // here and then watched BOTH endpoints close SE_PROTOCOL / SD_INTERNAL with
  // nobody lying at all. The wire path refuses where ui/screen_battle.cpp's
  // copy_from_box() mends, so the answer is a named refusal and not a heal.
  const VReject b = validate_battle_ready(m, count, bad_index);
  if (b != VR_OK) return b;
  for (uint8_t i = 0; i < count; ++i) pbw_encode(m[i], s.my_rec[i]);
  s.my_count = count;
  s.team_crc_local = session_team_crc(s.my_rec);
  return VR_OK;
}

static void send_hello(Session& s)
{
  ProtoMsg m; session_prepare(s, m, PT_HELLO, 0u);
  m.p.hello.device_id   = s.device_id;
  m.p.hello.hello_nonce = s.nonce_local;
  session_emit(s, m);
}

static void send_caps(Session& s)
{
  ProtoMsg m; session_prepare(s, m, PT_CAPABILITIES, 0u);
  caps_fill(m.p.caps);
  session_emit(s, m);
}

static void send_request(Session& s)
{
  ProtoMsg m; session_prepare(s, m, PT_SESSION_REQUEST, 0u);
  m.p.sreq.nonce_a    = s.nonce_local;
  m.p.sreq.team_count = s.my_count;
  m.p.sreq.lvl_lo     = s.lvl_lo;
  m.p.sreq.lvl_hi     = s.lvl_hi;
  session_emit(s, m);
}

// Decodes our OWN records into the setup through pbw_decode(), which is what
// makes the two endpoints' BattleSetups byte-identical: the local team enters
// the engine through the same decoder and the same validator the peer's does.
static bool load_own_team(Session& s)
{
  if (s.setup == nullptr) return false;
  for (uint8_t i = 0; i < s.my_count; ++i) {
    if (pbw_decode(s.my_rec[i], s.setup->member[s.side][i]) != VR_OK) return false;
  }
  s.setup->count[s.side] = s.my_count;
  return true;
}

static void send_team(Session& s)
{
  ProtoMsg m; session_prepare(s, m, PT_TEAM_SUBMIT, 0u);
  m.p.team.count    = s.my_count;
  m.p.team.team_crc = s.team_crc_local;
  memcpy(m.p.team.rec, s.my_rec, sizeof s.my_rec);
  session_emit(s, m);
}

static void send_validation(Session& s)
{
  ProtoMsg m; session_prepare(s, m, PT_TEAM_VALIDATION, 0u);
  m.p.tval.verdict       = s.verdict_local;
  m.p.tval.bad_index     = s.bad_index_local;
  m.p.tval.team_crc_echo = s.team_crc_peer;
  session_emit(s, m);
}

static void send_goodbye(Session& s, uint8_t reason)
{
  if (s.id == 0u) return;             // there is no session to leave yet
  ProtoMsg m; session_prepare(s, m, PT_GOODBYE, 0u);
  m.p.bye.reason = reason;
  session_emit(s, m);
}

void session_start(Session& s, uint32_t now_ms)
{
  if (s.state != (uint8_t)SS_IDLE) return;
  s.now_ms = now_ms;
  send_hello(s);
  s.waiting_for = (uint8_t)PT_HELLO;
  session_goto(s, SS_HELLO);
  session_progress(s);
}

void session_cancel(Session& s, uint32_t now_ms)
{
  if (s.state == (uint8_t)SS_CLOSED) return;
  s.now_ms = now_ms;
  send_goodbye(s, (uint8_t)SE_LOCAL_CANCEL);
  session_close(s, SE_LOCAL_CANCEL, (uint8_t)SD_NONE);
}

// -----------------------------------------------------------------------------
//  THE HANDSHAKE STATES
// -----------------------------------------------------------------------------
static void bind_roles(Session& s, const ProtoHello& h)
{
  s.peer_device_id = h.device_id;
  s.role = (s.device_id < h.device_id) ? (uint8_t)SR_INITIATOR : (uint8_t)SR_RESPONDER;
  s.side = (s.role == (uint8_t)SR_INITIATOR) ? 0u : 1u;
  const uint32_t lo = (s.device_id < h.device_id) ? s.device_id : h.device_id;
  const uint32_t hi = (s.device_id < h.device_id) ? h.device_id : s.device_id;
  s.hello_nonce_initiator =
      (s.role == (uint8_t)SR_INITIATOR) ? s.nonce_local : h.hello_nonce;
  s.id = session_derive_id(lo, hi, s.hello_nonce_initiator);
}

static void begin_battle_if_ready(Session& s)
{
  if (s.team_seen == 0u || s.verdict_seen == 0u) return;
  if (s.verdict_local != (uint8_t)VR_OK) return;
  link_begin(s);
}

static void on_team_submit(Session& s, const ProtoMsg& m)
{
  if (s.team_seen != 0u) {
    if (m.p.team.team_crc != s.team_crc_peer) {
      session_close(s, SE_PROTOCOL, (uint8_t)SD_TEAM_CHANGED);
      return;
    }
    // A benign retransmission: ANSWER it from state and advance nothing.
    if (s.end.rx_stale < 0xFFFFu) s.end.rx_stale++;
    session_note(s, LEK_REACK, (uint8_t)PT_TEAM_SUBMIT, 0u, 0u, m.seq);
    send_validation(s);
    return;
  }

  const uint8_t peer_side = (uint8_t)(s.side ^ 1u);
  PebbleInstance them[BATTLE_TEAM_MAX];
  memset(them, 0, sizeof them);
  VReject v = VR_OK;
  uint8_t bad = 0xFFu;
  for (uint8_t i = 0; i < m.p.team.count && v == VR_OK; ++i) {
    v = pbw_decode(m.p.team.rec[i], them[i]);
    if (v != VR_OK) bad = i;
  }
  if (v == VR_OK) v = validate_team(them, m.p.team.count, bad);
  if (v == VR_OK) v = validate_level_band(them, m.p.team.count, s.lvl_lo, s.lvl_hi, bad);
  // BATTLE ELIGIBILITY. hp_cur == 0 was the last thing validate_pebble(),
  // validate_team() and pbw_decode() all accepted and battle_init() refused, so
  // a peer that sent a fainted member reached the engine and came back as
  // SD_INTERNAL - this device recording a bug in game/validate.cpp, with
  // bad_index 0xFF, for a lie the peer told. It is a peer capability, so it is
  // named as one and the peer is told which member.
  if (v == VR_OK) v = validate_battle_ready(them, m.p.team.count, bad);
  // THE ONE RULE THAT SPANS BOTH TEAMS, and it has to live here because
  // validate_team() is about ONE team by construction. game/battle.cpp's
  // battle_init() refuses BR_DUPLICATE_ID across ALL SIX members, so a peer
  // that echoes one of our ids back at us would otherwise reach the engine and
  // be reported as SD_INTERNAL - this device blaming itself for a lie the peer
  // told. It is a peer capability, so it is named as one and the peer is told.
  if (v == VR_OK && s.setup != nullptr) {
    for (uint8_t i = 0; i < m.p.team.count && v == VR_OK; ++i) {
      for (uint8_t j = 0; j < s.my_count; ++j) {
        if (them[i].id == s.setup->member[s.side][j].id) {
          v = VR_DUPLICATE_ID; bad = i; break;
        }
      }
    }
  }

  s.team_crc_peer   = m.p.team.team_crc;
  s.verdict_local   = (uint8_t)v;
  s.bad_index_local = bad;
  s.team_seen       = 1u;
  send_validation(s);

  if (v != VR_OK) {
    s.end.bad_index = bad;
    send_goodbye(s, (uint8_t)SE_REJECTED);
    session_close(s, SE_REJECTED, (uint8_t)v);
    return;
  }
  if (s.setup != nullptr) {
    for (uint8_t i = 0; i < m.p.team.count; ++i) s.setup->member[peer_side][i] = them[i];
    s.setup->count[peer_side] = m.p.team.count;
  }
  session_progress(s);
  begin_battle_if_ready(s);
}

static void on_hello(Session& s, const ProtoMsg& m)
{
  if (m.p.hello.device_id == s.device_id) {
    // A peer that echoes our HELLO verbatim makes us our own peer, and with the
    // lower id playing side A the seed mix would be symmetric, so the
    // reflection would be invisible from here on.
    session_close(s, SE_PROTOCOL, (uint8_t)SD_SELF);
    return;
  }
  bind_roles(s, m.p.hello);
  if (s.state == (uint8_t)SS_IDLE) send_hello(s);
  send_caps(s);
  s.waiting_for = (uint8_t)PT_CAPABILITIES;
  session_goto(s, SS_CAPS);
  session_progress(s);
}

static void on_caps(Session& s, const ProtoMsg& m)
{
  const SessionCapWord w = caps_disagreement(m.p.caps);
  if (w == SCW_HASH_BASIS) {
    // Every named word matched and the basis did not: a version word is not
    // reaching the hash on one of the two builds.
    session_close(s, SE_PROTOCOL, (uint8_t)SD_BASIS_SKEW);
    return;
  }
  if (w != SCW_NONE) {
    send_goodbye(s, (uint8_t)SE_INCOMPATIBLE);
    session_close(s, SE_INCOMPATIBLE, (uint8_t)w);
    return;
  }
  if (s.role == (uint8_t)SR_INITIATOR) {
    send_request(s);
    s.waiting_for = (uint8_t)PT_SESSION_ACCEPT;
  } else {
    s.waiting_for = (uint8_t)PT_SESSION_REQUEST;
  }
  session_goto(s, SS_SESSION);
  session_progress(s);
}

static bool band_acceptable(const Session& s, uint8_t lo, uint8_t hi)
{
  if (lo == 0u || lo > hi) return false;
  return (lo >= s.lvl_lo) && (hi <= s.lvl_hi);
}

static void enter_team(Session& s)
{
  if (!load_own_team(s)) { session_close(s, SE_PROTOCOL, (uint8_t)SD_INTERNAL); return; }
  send_team(s);
  s.waiting_for = (uint8_t)PT_TEAM_VALIDATION;
  session_goto(s, SS_TEAM);
  session_progress(s);
}

static void on_session_request(Session& s, const ProtoMsg& m)
{
  ProtoMsg r; session_prepare(s, r, PT_SESSION_ACCEPT, 0u);
  r.p.sacc.nonce_b    = s.nonce_local;
  r.p.sacc.team_count = s.my_count;
  r.p.sacc.lvl_lo     = m.p.sreq.lvl_lo;
  r.p.sacc.lvl_hi     = m.p.sreq.lvl_hi;
  if (!band_acceptable(s, m.p.sreq.lvl_lo, m.p.sreq.lvl_hi)) {
    r.p.sacc.verdict = (uint8_t)VR_LEVEL_OUT_OF_BAND;
    session_emit(s, r);
    send_goodbye(s, (uint8_t)SE_REJECTED);
    session_close(s, SE_REJECTED, (uint8_t)SD_BAND);
    return;
  }
  r.p.sacc.verdict = 0u;
  session_emit(s, r);
  s.nonce_peer = m.p.sreq.nonce_a;
  s.lvl_lo = m.p.sreq.lvl_lo;          // the AGREED band, from here on
  s.lvl_hi = m.p.sreq.lvl_hi;
  // The responder submits its team UNPROMPTED, before it has seen the
  // initiator's: an honest implementation therefore CANNOT counter-pick, which
  // is the exact boundary of that claim - see docs/protocol.md.
  enter_team(s);
}

static void on_session_accept(Session& s, const ProtoMsg& m)
{
  if (m.p.sacc.verdict != 0u) {
    session_close(s, SE_REJECTED, (uint8_t)m.p.sacc.verdict);
    return;
  }
  if (m.p.sacc.lvl_lo != s.lvl_lo || m.p.sacc.lvl_hi != s.lvl_hi) {
    session_close(s, SE_PROTOCOL, (uint8_t)SD_BAND);
    return;
  }
  s.nonce_peer = m.p.sacc.nonce_b;
  enter_team(s);
}

static void on_team_validation(Session& s, const ProtoMsg& m)
{
  if (m.p.tval.team_crc_echo != s.team_crc_local) {
    if (s.end.rx_wrong_state < 0xFFFFu) s.end.rx_wrong_state++;
    session_note(s, LEK_WRONG_STATE, m.type, 0u, m.round, m.seq);
    return;                                   // a verdict about another team
  }
  if (m.p.tval.verdict != (uint8_t)VR_OK) {
    s.end.bad_index = m.p.tval.bad_index;
    send_goodbye(s, (uint8_t)SE_REJECTED);
    session_close(s, SE_REJECTED, m.p.tval.verdict);
    return;
  }
  if (s.verdict_seen != 0u) {                 // a duplicate the window let through
    if (s.end.rx_stale < 0xFFFFu) s.end.rx_stale++;
    return;
  }
  s.verdict_seen = 1u;
  session_progress(s);
  begin_battle_if_ready(s);
}

// -----------------------------------------------------------------------------
//  ANSWERING A PEER THAT IS BEHIND. See session.h for the measurement that made
//  this mandatory rather than an optimisation.
//
//  It regenerates the reply we already gave and ADVANCES NOTHING - no state, no
//  timer, no counter but the leash. The leash is a heuristic like every other
//  anti-exhaustion number here: a HELLO answered by a HELLO can bounce a few
//  times between two endpoints that are both past that phase, and 16 bounds it.
// -----------------------------------------------------------------------------
#define SESSION_MAX_HANDSHAKE_REACK 16u

bool session_reanswer_earlier_phase(Session& s, const ProtoMsg& m)
{
  // TEAM_SUBMIT is checked BEFORE the leash: "the peer changed its team" is a
  // termination, not an answer, and a leash must not be able to silence it.
  if (m.type == (uint8_t)PT_TEAM_SUBMIT && s.team_seen != 0u &&
      m.p.team.team_crc != s.team_crc_peer) {
    session_close(s, SE_PROTOCOL, (uint8_t)SD_TEAM_CHANGED);
    return true;
  }
  if (s.hs_reack >= (uint8_t)SESSION_MAX_HANDSHAKE_REACK) return false;

  switch ((ProtoType)m.type) {
    case PT_HELLO:
      if (s.id == 0u) return false;
      s.hs_reack++;
      session_note(s, LEK_REACK, m.type, 0u, 0u, m.seq);
      send_hello(s);
      return true;
    case PT_CAPABILITIES:
      if (s.state < (uint8_t)SS_SESSION) return false;
      s.hs_reack++;
      session_note(s, LEK_REACK, m.type, 0u, 0u, m.seq);
      send_caps(s);
      return true;
    case PT_SESSION_REQUEST: {
      if (s.role != (uint8_t)SR_RESPONDER || s.state < (uint8_t)SS_TEAM) return false;
      s.hs_reack++;
      session_note(s, LEK_REACK, m.type, 0u, 0u, m.seq);
      ProtoMsg r; session_prepare(s, r, PT_SESSION_ACCEPT, 0u);
      r.flags = (uint8_t)PF_RETX;
      r.p.sacc.nonce_b = s.nonce_local; r.p.sacc.team_count = s.my_count;
      r.p.sacc.verdict = 0u;
      r.p.sacc.lvl_lo = s.lvl_lo; r.p.sacc.lvl_hi = s.lvl_hi;
      session_emit(s, r);
      return true;
    }
    case PT_TEAM_SUBMIT:
      if (s.team_seen == 0u) return false;
      s.hs_reack++;
      session_note(s, LEK_REACK, m.type, 0u, 0u, m.seq);
      send_validation(s);
      return true;
    default:
      return false;
  }
}

// -----------------------------------------------------------------------------
//  DISPATCH. R3 lives here: a legal message that is not expected in this state
//  is dropped AND COUNTED, and it never advances anything.
// -----------------------------------------------------------------------------
static void wrong_state(Session& s, const ProtoMsg& m)
{
  if (session_reanswer_earlier_phase(s, m)) return;
  if (s.end.rx_wrong_state < 0xFFFFu) s.end.rx_wrong_state++;
  session_note(s, LEK_WRONG_STATE, m.type, 0u, m.round, m.seq);
}

static void dispatch(Session& s, const ProtoMsg& m)
{
  const ProtoType t = (ProtoType)m.type;

  // The universal rule, applied in every state that HAS a session: the peer has
  // left. It is SE_LOST and never SE_DESYNC - a peer that walks away is a
  // transport event, not an engine disagreement.
  //
  // FOUR STATES ARE EXCLUDED, FOR TWO DIFFERENT REASONS. SS_IDLE has no session
  // to leave, so the frame is ordinary junk. SS_VERIFY, SS_BATTLE and SS_ENDING
  // are excluded because a BATTLE_END there carries a REASON, and reading "the
  // peer says we diverged" as "the peer walked away" would file a desync under
  // the one code that exists to mean the opposite.
  if ((t == PT_GOODBYE || t == PT_BATTLE_END) &&
      s.state != (uint8_t)SS_IDLE && s.state != (uint8_t)SS_VERIFY &&
      s.state != (uint8_t)SS_ENDING && s.state != (uint8_t)SS_BATTLE) {
    session_close(s, SE_LOST, (uint8_t)SD_PEER_GOODBYE);
    return;
  }

  switch ((SessionState)s.state) {
    case SS_IDLE:
      if (t == PT_HELLO) on_hello(s, m); else wrong_state(s, m);
      break;
    case SS_HELLO:
      if (t == PT_HELLO) on_hello(s, m); else wrong_state(s, m);
      break;
    case SS_CAPS:
      if (t == PT_CAPABILITIES) on_caps(s, m);
      else wrong_state(s, m);      // a repeated HELLO is ANSWERED there rather
                                   // than dropped: the peer that sent it is
                                   // missing ours, and the session id is
                                   // already fixed so nothing is re-derived
      break;
    case SS_SESSION:
      if (t == PT_SESSION_REQUEST && s.role == (uint8_t)SR_RESPONDER)
        on_session_request(s, m);
      else if (t == PT_SESSION_ACCEPT && s.role == (uint8_t)SR_INITIATOR)
        on_session_accept(s, m);
      else
        wrong_state(s, m);         // both-initiator glare is forbidden by the
                                   // device-id role rule: a modified or
                                   // reflected peer, not a race
      break;
    case SS_TEAM:
      if (t == PT_TEAM_SUBMIT) on_team_submit(s, m);
      else if (t == PT_TEAM_VALIDATION) on_team_validation(s, m);
      else wrong_state(s, m);
      break;
    case SS_VERIFY:
    case SS_BATTLE:
    case SS_ENDING:
      link_on_msg(s, m);
      break;
    case SS_CLOSED:
    default:
      break;                       // terminal: everything is dropped
  }
}

// -----------------------------------------------------------------------------
//  THE LADDER'S REGENERATION. Everything below is rebuilt from state; no frame
//  is ever stored as bytes.
// -----------------------------------------------------------------------------
static void resend(Session& s)
{
  switch ((SessionState)s.state) {
    case SS_HELLO: {
      ProtoMsg m; session_prepare(s, m, PT_HELLO, 0u); m.flags = (uint8_t)PF_RETX;
      m.p.hello.device_id = s.device_id; m.p.hello.hello_nonce = s.nonce_local;
      session_emit(s, m);
      break;
    }
    case SS_CAPS: {
      ProtoMsg m; session_prepare(s, m, PT_CAPABILITIES, 0u); m.flags = (uint8_t)PF_RETX;
      caps_fill(m.p.caps);
      session_emit(s, m);
      break;
    }
    case SS_SESSION:
      // ONLY THE INITIATOR HAS SOMETHING TO SAY HERE. The responder is waiting
      // for a SESSION_REQUEST it cannot ask for; its ladder still runs, which
      // is what turns a peer that vanished in this state into a NAMED SE_LOST
      // rather than a wait with no end.
      if (s.role == (uint8_t)SR_INITIATOR) {
        ProtoMsg m; session_prepare(s, m, PT_SESSION_REQUEST, 0u);
        m.flags = (uint8_t)PF_RETX;
        m.p.sreq.nonce_a = s.nonce_local; m.p.sreq.team_count = s.my_count;
        m.p.sreq.lvl_lo = s.lvl_lo; m.p.sreq.lvl_hi = s.lvl_hi;
        session_emit(s, m);
      }
      break;
    case SS_TEAM: {
      ProtoMsg m; session_prepare(s, m, PT_TEAM_SUBMIT, 0u); m.flags = (uint8_t)PF_RETX;
      m.p.team.count = s.my_count; m.p.team.team_crc = s.team_crc_local;
      memcpy(m.p.team.rec, s.my_rec, sizeof s.my_rec);
      session_emit(s, m);
      if (s.team_seen != 0u) {
        ProtoMsg v; session_prepare(s, v, PT_TEAM_VALIDATION, 0u);
        v.flags = (uint8_t)PF_RETX;
        v.p.tval.verdict = s.verdict_local; v.p.tval.bad_index = s.bad_index_local;
        v.p.tval.team_crc_echo = s.team_crc_peer;
        session_emit(s, v);
      }
      break;
    }
    case SS_VERIFY:
    case SS_BATTLE:
    case SS_ENDING:
      link_resend(s);
      break;
    default:
      break;
  }
}

static void ladder(Session& s)
{
  if (s.state == (uint8_t)SS_IDLE || s.state == (uint8_t)SS_CLOSED) return;
  if ((int32_t)(s.now_ms - s.due_ms) < 0) return;
  if (s.tries >= (uint8_t)PROTO_RETX_MAX) {
    // A session that already AGREED - both endpoints' outcome and final hash
    // matched - has nothing left to lose: the peer's GOODBYE is a courtesy and
    // its absence is not a disagreement. That rule USED TO BE WRITTEN HERE, as
    // a `paid ? SE_DONE : SE_LOST` fork; it now lives in session_close(),
    // which is the only place that can hold it for the terminals this function
    // is not the one to reach (a relabelling BATTLE_END, an exhausted budget).
    session_close(s, SE_LOST, (uint8_t)SD_NONE);
    return;
  }
  s.tries++;
  s.due_ms = s.now_ms + PROTO_RETX_MS;
  resend(s);
}

// -----------------------------------------------------------------------------
//  ONE FRAME, FROM THE TRANSPORT TO THE FSM
// -----------------------------------------------------------------------------
static void on_frame(Session& s, const uint8_t* buf, uint16_t n)
{
  ProtoMsg m;
  // R2 comes free: proto_decode() is told which session we are in, so every
  // type but HELLO is refused by name when it names another one.
  const ProtoErr e = proto_decode(buf, (size_t)n, s.id, m);
  if (e != PE_OK) {
    // R1: a refused frame never reaches the FSM and NEVER TOUCHES A TIMER.
    if (s.end.rx_reject < 0xFFFFu) s.end.rx_reject++;
    session_note(s, LEK_CODEC_REJECT, (n >= 2u) ? buf[1] : 0u, (uint8_t)e, 0u, 0u);
    return;
  }
  if (m.seq == 0u) {                       // never allocated by session_prepare()
    if (s.end.rx_reject < 0xFFFFu) s.end.rx_reject++;
    return;
  }
  const SeqVerdict q = seq_check(s, m.seq);
  if (q == SQ_DUP) {
    if (s.end.rx_dup < 0xFFFFu) s.end.rx_dup++;
    session_note(s, LEK_DUP, m.type, 0u, m.round, m.seq);
    return;                                // the transport spoke twice; we do not
  }
  if (q == SQ_STALE) {
    if (s.end.rx_stale < 0xFFFFu) s.end.rx_stale++;
    session_note(s, LEK_STALE, m.type, 0u, m.round, m.seq);
    return;
  }
  if (s.rx_budget == 0u) { session_close(s, SE_PROTOCOL, (uint8_t)SD_RX_BUDGET); return; }
  s.rx_budget--;
  if (s.end.rx_ok < 0xFFFFu) s.end.rx_ok++;
  session_note(s, LEK_RX, m.type, 0u, m.round, m.seq);
  dispatch(s, m);
}

void session_poll(Session& s, uint32_t now_ms)
{
  s.now_ms = now_ms;
  if (s.state == (uint8_t)SS_CLOSED || s.tp == nullptr) return;

  uint8_t buf[PROTO_FRAME_MAX];
  for (;;) {
    const uint16_t n = transport_recv(*s.tp, buf, (uint16_t)sizeof buf);
    if (n == 0u) break;
    on_frame(s, buf, n);
    if (s.state == (uint8_t)SS_CLOSED) return;
  }
  ladder(s);
}

bool session_wants_action(const Session& s)
{
  return (s.state == (uint8_t)SS_BATTLE) && link_wants_action(s);
}

void session_submit_action(Session& s, BattleAction a, uint32_t now_ms)
{
  s.now_ms = now_ms;
  if (!session_wants_action(s)) return;
  link_local_action(s, a);
}
