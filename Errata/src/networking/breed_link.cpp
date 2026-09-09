// =============================================================================
//  ERRATA - networking/breed_link.cpp
//  See breed_link.h. PURE translation unit.
// =============================================================================
#include "breed_link.h"

#include <string.h>

#include "../core/crc16.h"
#include "../game/box.h"        // BOX_SLOT_NONE
#include "../game/validate.h"

// -----------------------------------------------------------------------------
//  NAMES
// -----------------------------------------------------------------------------
// The re-answer leash. Its own constant rather than trade_link.cpp's, which is
// a static in another translation unit: two modules sharing a number by
// coincidence is how one of them silently changes when the other is tuned.
#define BREED_MAX_REACK 16u

static const char* const BLP_NAMES[] = {
  "BLP_IDLE", "BLP_OFFERED", "BLP_REVIEW", "BLP_ASK_PLAYER",
  "BLP_WAIT_PEER", "BLP_FILED", "BLP_DONE", "BLP_REFUSED"
};
static_assert(sizeof(BLP_NAMES) / sizeof(BLP_NAMES[0]) == (size_t)BLP_PHASE_COUNT,
              "a BreedLinkPhase was added without a name");

const char* breed_phase_name(BreedLinkPhase p)
{
  return (p < BLP_PHASE_COUNT) ? BLP_NAMES[(uint8_t)p] : "?";
}

// -----------------------------------------------------------------------------
//  THE PLAN'S IDENTITY, FIELD BY FIELD AND NEVER OVER THE STRUCT.
//
//  crc16_ccitt((const uint8_t*)&plan, sizeof plan) is the obvious line and it
//  is WRONG: BreedPlan has padding, padding is not initialised by an aggregate
//  assignment, and two devices that computed a byte-identical child would then
//  disagree about its CRC because of bytes neither of them ever wrote. It
//  would fail rarely and look like a radio fault.
//
//  So the fields are serialised in a fixed order into a fixed buffer. The
//  static_assert below is what stops a field being ADDED to BreedPlan without
//  being added here - the failure mode otherwise is a plan whose changed part
//  is not covered by the number that is supposed to identify it.
// -----------------------------------------------------------------------------
static uint16_t plan_crc_of(const BreedPlan& p)
{
  uint8_t b[4 + sizeof(Genome) + 4];
  uint8_t* q = b;
  q[0] = (uint8_t)(p.seed & 0xFFu);
  q[1] = (uint8_t)((p.seed >> 8) & 0xFFu);
  q[2] = (uint8_t)((p.seed >> 16) & 0xFFu);
  q[3] = (uint8_t)((p.seed >> 24) & 0xFFu);
  q += 4;
  memcpy(q, &p.genome, sizeof(Genome));      // sealed and fully written by breed_compute()
  q += sizeof(Genome);
  q[0] = p.species_id;
  q[1] = p.level;
  q[2] = p.flags;
  q[3] = p.from_b;
  return crc16_ccitt(b, sizeof b);
}
static_assert(sizeof(BreedPlan) ==
                  sizeof(uint32_t) + sizeof(Genome) + 4 * sizeof(uint8_t) +
                      (sizeof(BreedPlan) - sizeof(uint32_t) - sizeof(Genome) -
                       4 * sizeof(uint8_t)),
              "trivially true; the real guard is the field list below");
static_assert(offsetof(BreedPlan, seed) == 0 &&
              offsetof(BreedPlan, species_id) > offsetof(BreedPlan, genome) &&
              sizeof(BreedPlan) <= 4 + sizeof(Genome) + 8,
              "BreedPlan grew a field: plan_crc_of() covers seed, genome, "
              "species_id, level, flags and from_b BY NAME, so a seventh member "
              "would be invisible to the number that identifies a child on the "
              "wire - two devices could agree on a CRC over two different plans");

// The identity of one parent record: its OWN trailing CRC. protocol.h declares
// this under the trade's name because the trade needed it first; there is
// nothing trade-specific in it - it reads BUGW_OFF_CRC out of a 48 B record -
// and a second function with a different name over the same two bytes would be
// a second place for the two to disagree.
static inline uint16_t rec_crc(const uint8_t rec[BUGW_BYTES])
{
  return trade_rec_crc(rec);
}

// -----------------------------------------------------------------------------
//  WIRING
// -----------------------------------------------------------------------------
void breed_link_init(BreedLink& bl, Session& s, const BreedHooks& hooks)
{
  memset(&bl, 0, sizeof bl);
  bl.hooks = hooks;
  bl.slot  = (uint8_t)BOX_SLOT_NONE;
  bl.phase = (uint8_t)BLP_IDLE;
  s.bl     = &bl;
}

// -----------------------------------------------------------------------------
//  THE FOUR SENDS. Every one is rebuilt from the BreedLink, so the ladder's
//  regeneration and the first transmission cannot drift.
// -----------------------------------------------------------------------------
static void send_offer(Session& s, bool retx)
{
  if (s.bl == nullptr) return;
  ProtoMsg m; session_prepare(s, m, PT_BREED_OFFER, 0u);
  if (retx) m.flags = (uint8_t)PF_RETX;
  memcpy(m.p.boffer.rec, s.my_rec[0], (size_t)BUGW_BYTES);
  session_emit(s, m);
}

static void send_ready(Session& s, bool retx)
{
  if (s.bl == nullptr) return;
  ProtoMsg m; session_prepare(s, m, PT_BREED_READY, 0u);
  if (retx) m.flags = (uint8_t)PF_RETX;
  m.p.bready.verdict        = s.bl->in_verdict;
  m.p.bready.pair_reject    = s.bl->pair_reject;
  m.p.bready.offer_crc_echo = s.bl->in_crc;
  session_emit(s, m);
}

static void send_confirm(Session& s, bool retx)
{
  if (s.bl == nullptr) return;
  ProtoMsg m; session_prepare(s, m, PT_BREED_CONFIRM, 0u);
  if (retx) m.flags = (uint8_t)PF_RETX;
  m.p.bconfirm.accept   = 1u;
  m.p.bconfirm.plan_crc = s.bl->plan_crc;
  session_emit(s, m);
}

static void send_done(Session& s, bool retx)
{
  if (s.bl == nullptr) return;
  ProtoMsg m; session_prepare(s, m, PT_BREED_DONE, 0u);
  if (retx) m.flags = (uint8_t)PF_RETX;
  m.p.bdone.plan_crc = s.bl->plan_crc;
  m.p.bdone.reject   = s.bl->commit_reject;
  session_emit(s, m);
}

// -----------------------------------------------------------------------------
//  ENTERING. Nothing is journalled and nothing can fail locally here: a
//  breeding writes nothing until both people have said yes, which is the whole
//  difference from networking/trade_link.cpp's W1.
// -----------------------------------------------------------------------------
void breed_link_begin(Session& s)
{
  if (s.bl == nullptr) { session_close(s, SE_PROTOCOL, (uint8_t)SD_INTERNAL); return; }
  s.bl->phase = (uint8_t)BLP_OFFERED;
  send_offer(s, false);
  s.waiting_for = (uint8_t)PT_BREED_OFFER;
  session_goto(s, SS_BREED);
  session_progress(s);
}

static void refuse(Session& s, SessionEndReason r, uint8_t detail)
{
  if (s.bl != nullptr) {
    s.bl->phase = (uint8_t)BLP_REFUSED;
    if (s.bl->hooks.abort != nullptr) s.bl->hooks.abort(s.bl->hooks.ctx);
  }
  session_close(s, r, detail);
}

// -----------------------------------------------------------------------------
//  THE CHILD. Computed once, on both devices, from the same five inputs.
//
//  THE ARGUMENT ORDER IS THE CONTRACT game/breeding.h SPELLS OUT: `a` is the
//  INITIATOR's parent on BOTH ends. Ordering by "mine, then theirs" gives each
//  device a different, equally valid child, and nothing downstream would notice
//  until two players compared two screens.
// -----------------------------------------------------------------------------
static void compute_plan(Session& s)
{
  BreedLink& bl = *s.bl;
  if (bl.have_plan != 0u || bl.have_in == 0u) return;

  BugInstance mine, theirs;
  if (pbw_decode(s.my_rec[0], mine) != VR_OK) return;
  if (pbw_decode(bl.in_rec,   theirs) != VR_OK) return;

  const bool init_side = (s.role == (uint8_t)SR_INITIATOR);
  const uint16_t my_crc = rec_crc(s.my_rec[0]);
  const uint32_t seed = session_derive_seed(
      s.id,
      init_side ? s.nonce_local : s.nonce_peer,
      init_side ? s.nonce_peer  : s.nonce_local,
      init_side ? my_crc        : bl.in_crc,
      init_side ? bl.in_crc     : my_crc);

  const BreedReject br = init_side ? breed_compute(mine, theirs, seed, bl.plan)
                                   : breed_compute(theirs, mine, seed, bl.plan);
  if (br != BRD_OK) { bl.pair_reject = (uint8_t)br; return; }
  bl.have_plan = 1u;
  bl.plan_crc  = plan_crc_of(bl.plan);
}

// Both halves of the review are in and both said yes. Called from BOTH handlers
// for the reason networking/trade_link.cpp measured and wrote down: a READY that
// overtakes its own OFFER is ordinary on a lossy medium, and an endpoint that
// decides from "whichever arrived last" parks until its ladder runs out.
static void maybe_ask_player(Session& s)
{
  BreedLink& bl = *s.bl;
  if (bl.have_in == 0u || bl.peer_ready == 0u) return;
  if (bl.in_verdict != (uint8_t)VR_OK || bl.pair_reject != 0u) return;
  if (bl.peer_verdict != (uint8_t)VR_OK || bl.peer_pair_reject != 0u) return;
  if (bl.have_plan == 0u) return;
  if (bl.phase == (uint8_t)BLP_REVIEW || bl.phase == (uint8_t)BLP_OFFERED) {
    bl.phase      = (uint8_t)BLP_ASK_PLAYER;
    s.waiting_for = (uint8_t)PT_BREED_CONFIRM;
  }
}

static void on_offer(Session& s, const ProtoMsg& m)
{
  BreedLink& bl = *s.bl;
  const uint16_t crc = rec_crc(m.p.boffer.rec);

  if (bl.have_in != 0u) {
    // A SECOND OFFER. Different bytes means the peer changed which Bug it is
    // breeding, and the consent a player is about to give names one specific
    // pair - so it terminates, exactly as the trade's second offer does.
    if (crc != bl.in_crc) { refuse(s, SE_PROTOCOL, (uint8_t)SD_TEAM_CHANGED); return; }
    if (s.end.rx_stale < 0xFFFFu) s.end.rx_stale++;
    if (bl.reack < (uint8_t)BREED_MAX_REACK) { bl.reack++; send_ready(s, true); }
    return;
  }

  memcpy(bl.in_rec, m.p.boffer.rec, (size_t)BUGW_BYTES);
  bl.have_in = 1u;
  bl.in_crc  = crc;

  // ONE VALIDATOR ON THE RECORD, then the PAIR rule on top of it. The two are
  // separate enums and separate bytes on the wire; game/taint.h argues why.
  BugInstance theirs;
  bl.in_verdict = (uint8_t)pbw_decode(bl.in_rec, theirs);
  if (bl.in_verdict == (uint8_t)VR_OK) {
    BugInstance mine;
    if (pbw_decode(s.my_rec[0], mine) == VR_OK) {
      const bool init_side = (s.role == (uint8_t)SR_INITIATOR);
      bl.pair_reject = (uint8_t)(init_side ? breed_check(mine, theirs)
                                           : breed_check(theirs, mine));
    } else {
      bl.pair_reject = (uint8_t)BRD_INVALID_PARENT;
    }
  }
  if (bl.phase == (uint8_t)BLP_OFFERED) bl.phase = (uint8_t)BLP_REVIEW;

  if (bl.in_verdict == (uint8_t)VR_OK && bl.pair_reject == 0u) compute_plan(s);

  send_ready(s, false);
  s.waiting_for = (uint8_t)PT_BREED_READY;
  session_progress(s);

  if (bl.in_verdict != (uint8_t)VR_OK) { refuse(s, SE_REJECTED, bl.in_verdict); return; }
  if (bl.pair_reject != 0u) { refuse(s, SE_REJECTED, (uint8_t)SD_BREED_REFUSED); return; }
  maybe_ask_player(s);
}

static void on_ready(Session& s, const ProtoMsg& m)
{
  BreedLink& bl = *s.bl;
  if (m.p.bready.offer_crc_echo != rec_crc(s.my_rec[0])) {
    if (s.end.rx_wrong_state < 0xFFFFu) s.end.rx_wrong_state++;
    session_note(s, LEK_WRONG_STATE, m.type, 0u, 0u, m.seq);
    return;
  }
  if (bl.peer_ready != 0u) {
    if (s.end.rx_stale < 0xFFFFu) s.end.rx_stale++;
    return;
  }
  bl.peer_ready        = 1u;
  bl.peer_verdict      = m.p.bready.verdict;
  bl.peer_pair_reject  = m.p.bready.pair_reject;
  session_progress(s);

  if (bl.peer_verdict != (uint8_t)VR_OK) { refuse(s, SE_REJECTED, bl.peer_verdict); return; }
  if (bl.peer_pair_reject != 0u) {
    refuse(s, SE_REJECTED, (uint8_t)SD_BREED_REFUSED);
    return;
  }
  compute_plan(s);
  maybe_ask_player(s);
}

// -----------------------------------------------------------------------------
//  FILING. Reached only with BOTH confirms in hand.
//
//  A FULL BOX IS NOT A SESSION FAILURE, and that is the deliberate asymmetry
//  breed_link.h section 4 argues for: the pair was agreed, nothing is lost, and
//  denying the other player the child they said yes to because of this Box's
//  housekeeping would be punishing them for it. The reason travels in DONE so
//  the screen can be honest about which half happened.
// -----------------------------------------------------------------------------
static void run_file(Session& s)
{
  BreedLink& bl = *s.bl;
  if (bl.filed != 0u) return;                        // idempotent
  if (bl.local_accept == 0u || bl.peer_accept == 0u) return;
  if (bl.have_plan == 0u) { refuse(s, SE_PROTOCOL, (uint8_t)SD_INTERNAL); return; }

  uint8_t slot = (uint8_t)BOX_SLOT_NONE;
  bl.commit_reject = (bl.hooks.commit != nullptr)
                       ? bl.hooks.commit(bl.hooks.ctx, bl.plan, slot)
                       : (uint8_t)BRD_NO_BOX;
  if (bl.commit_reject == (uint8_t)BRD_OK) {
    bl.slot = slot;
    if (bl.hooks.save != nullptr && !bl.hooks.save(bl.hooks.ctx)) {
      // The child is in RAM and did not reach flash. That IS a local fault
      // worth ending on: the player would otherwise be shown a creature that
      // the next boot has never heard of.
      refuse(s, SE_PROTOCOL, (uint8_t)SD_BREED_STORE);
      return;
    }
  }

  bl.filed      = 1u;
  bl.phase      = (uint8_t)BLP_FILED;
  s.waiting_for = (uint8_t)PT_BREED_DONE;
  send_done(s, false);
  session_progress(s);

  if (bl.peer_done != 0u) {
    bl.phase = (uint8_t)BLP_DONE;
    session_close(s, SE_DONE, (uint8_t)SD_NONE);
  }
}

static void on_confirm(Session& s, const ProtoMsg& m)
{
  BreedLink& bl = *s.bl;
  if (bl.have_plan == 0u || m.p.bconfirm.plan_crc != bl.plan_crc) {
    // A CONFIRM naming a child that is not the one on this table. THIS IS THE
    // DESYNC CHECK breed_link.h section 2 exists for: if the two ends ordered
    // the parents differently, or derived a different seed, they computed two
    // different children and this is where it is caught - before either is
    // filed, rather than by two players comparing two screens afterwards.
    bl.peer_plan_crc = m.p.bconfirm.plan_crc;
    if (bl.have_plan != 0u) { refuse(s, SE_PROTOCOL, (uint8_t)SD_BREED_PLAN); return; }
    if (s.end.rx_wrong_state < 0xFFFFu) s.end.rx_wrong_state++;
    session_note(s, LEK_WRONG_STATE, m.type, 0u, 0u, m.seq);
    return;
  }
  bl.peer_plan_crc = m.p.bconfirm.plan_crc;
  if (m.p.bconfirm.accept == 0u) {          // their player said no
    refuse(s, SE_REJECTED, (uint8_t)SD_BREED_REFUSED);
    return;
  }
  if (bl.peer_accept != 0u) {
    if (s.end.rx_stale < 0xFFFFu) s.end.rx_stale++;
    if (bl.reack < (uint8_t)BREED_MAX_REACK && bl.local_accept != 0u) {
      bl.reack++;
      if (bl.filed != 0u) send_done(s, true); else send_confirm(s, true);
    }
    return;
  }
  bl.peer_accept = 1u;
  session_progress(s);
  run_file(s);
}

static void on_done(Session& s, const ProtoMsg& m)
{
  BreedLink& bl = *s.bl;
  if (bl.have_plan == 0u || m.p.bdone.plan_crc != bl.plan_crc) {
    if (s.end.rx_wrong_state < 0xFFFFu) s.end.rx_wrong_state++;
    session_note(s, LEK_WRONG_STATE, m.type, 0u, 0u, m.seq);
    return;
  }
  if (bl.peer_done != 0u) {
    if (s.end.rx_stale < 0xFFFFu) s.end.rx_stale++;
    if (bl.reack < (uint8_t)BREED_MAX_REACK && bl.filed != 0u) {
      bl.reack++; send_done(s, true);
    }
    return;
  }
  // A DONE IMPLIES THE CONFIRM IT FOLLOWS, exactly as TRADE_COMMIT does: a peer
  // that filed has by definition consented, and requiring the CONFIRM to have
  // arrived first would deadlock on the one frame the medium dropped.
  bl.peer_done        = 1u;
  bl.peer_accept      = 1u;
  bl.peer_commit_reject = m.p.bdone.reject;
  session_progress(s);

  run_file(s);
  if (bl.filed != 0u && bl.phase != (uint8_t)BLP_DONE) {
    bl.phase = (uint8_t)BLP_DONE;
    session_close(s, SE_DONE, (uint8_t)SD_NONE);
  }
}

// -----------------------------------------------------------------------------
//  THE SEAMS session.cpp CALLS
// -----------------------------------------------------------------------------
void breed_link_on_msg(Session& s, const ProtoMsg& m)
{
  if (s.bl == nullptr) { session_close(s, SE_PROTOCOL, (uint8_t)SD_INTERNAL); return; }
  switch ((ProtoType)m.type) {
    case PT_BREED_OFFER:   on_offer(s, m);   break;
    case PT_BREED_READY:   on_ready(s, m);   break;
    case PT_BREED_CONFIRM: on_confirm(s, m); break;
    case PT_BREED_DONE:    on_done(s, m);    break;
    default:
      if (s.end.rx_wrong_state < 0xFFFFu) s.end.rx_wrong_state++;
      session_note(s, LEK_WRONG_STATE, m.type, 0u, 0u, m.seq);
      break;
  }
}

void breed_link_resend(Session& s)
{
  if (s.bl == nullptr) return;
  const BreedLink& bl = *s.bl;
  if (bl.filed != 0u)                          { send_done(s, true);    return; }
  if (bl.local_accept != 0u)                   { send_confirm(s, true); return; }
  if (bl.have_in != 0u)                        { send_ready(s, true);   return; }
  send_offer(s, true);
}

bool breed_link_wants_consent(const Session& s)
{
  return s.bl != nullptr && s.bl->phase == (uint8_t)BLP_ASK_PLAYER &&
         s.bl->local_accept == 0u;
}

void breed_link_accept(Session& s)
{
  if (!breed_link_wants_consent(s)) return;    // a second call is ignored
  BreedLink& bl = *s.bl;
  bl.local_accept = 1u;
  bl.phase        = (uint8_t)BLP_WAIT_PEER;
  send_confirm(s, false);
  s.waiting_for   = (uint8_t)PT_BREED_CONFIRM;
  session_progress(s);
  run_file(s);
}

const BreedPlan* breed_link_plan(const Session& s)
{
  if (s.bl == nullptr || s.bl->have_plan == 0u) return nullptr;
  return &s.bl->plan;
}

bool breed_link_completed(const Session& s)
{
  return s.bl != nullptr && s.bl->filed != 0u && s.bl->peer_done != 0u;
}
