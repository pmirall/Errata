// =============================================================================
//  ERRATA - networking/breed_link.h
//
//  TWO BUGS, ONE PAIR, TWO CHILDREN. The wire half of spec section 17.
//  PURE translation unit.
//
//  THIS FILE WAS PLANNED AT P7-C5 AND NOT WRITTEN, AND THE TREE SAID SO FOR
//  THREE PHASES rather than pretending otherwise: game/breeding.cpp was
//  complete and swept over every roster pair, DISC_CAP_BREED existed and was
//  NOT claimed, and the LINK card's CRIAR row answered "Aun no esta listo" on
//  purpose. README.md listed two acceptance boxes as unrunnable for that
//  reason. This is that module.
//
// -----------------------------------------------------------------------------
//  1. WHY IT IS NOT THE TRADE, ALTHOUGH IT LOOKS LIKE IT
//
//  networking/trade_link.h drives a two-phase journal because a trade can
//  DESTROY value: a Bug leaves one Box and must arrive in the other, and a
//  power cut between those two facts either duplicates it or vaporises it. That
//  is what PendingTrade and the W1..W4 hooks exist for.
//
//  A BREEDING CREATES. Each device keeps its own parent and gains a child of
//  its own; nothing leaves anywhere. So there is no journal here, no
//  PendingTrade, no resume-at-boot, and no hooks to write flash in a particular
//  order - and putting one in "for symmetry" would be ceremony around a risk
//  that does not exist.
//
//  WHAT IS SHARED IS THE SHAPE OF THE CONVERSATION, because the shape is not
//  about atomicity: offer, judge, ask the two people, act. That is the same
//  four beats whether the thing being agreed is an exchange or a child.
//
// -----------------------------------------------------------------------------
//  2. THE ONE THING BOTH DEVICES MUST COMPUTE IDENTICALLY
//
//  game/breeding.h's breed_compute() is deterministic in (a, b, shared_seed)
//  and its banner is explicit about the trap: `a` is the INITIATOR's parent and
//  `b` the RESPONDER's ON BOTH DEVICES. Ordering them by "mine, then theirs"
//  produces a different, equally valid child on each end - two players staring
//  at two different creatures, both correct, with nothing detecting it.
//
//  So this module orders by SessionRole and never by locality, and the seed is
//  session_derive_seed() over the session id, both nonces and the two PARENT
//  CRCs - the same derivation the battle uses over the two TEAM crcs, for the
//  same reason: both ends hold all five inputs and neither chose them alone.
//
//  THE PLAN IS THEN CHECKED AGAINST THE PEER'S, not assumed to match. Each side
//  puts the CRC of its computed BreedPlan in its CONFIRM. If the two disagree
//  the session closes SE_PROTOCOL / SD_HASH rather than filing two different
//  children, which is the same instinct as the battle's round-hash barrier.
//
// -----------------------------------------------------------------------------
//  3. CONSENT IS LOCAL, SYMMETRIC AND NOT INFERRED
//
//  game/breeding.h names it as audit risk 5: THERE IS NO AUTO-ACCEPTED EGG.
//  Both players press A on their own device, on a pair they can both see, and
//  nothing is filed until both CONFIRMs are in. One refusal ends it for both -
//  a child neither person agreed to is worse than no child.
//
// -----------------------------------------------------------------------------
//  4. WHAT IS DELIBERATELY ASYMMETRIC, AND WHY THAT IS NOT A BUG
//
//  Once both people have consented, EACH DEVICE FILES ITS OWN CHILD LOCALLY and
//  the two commits are independent. If one Box is full, that owner gets
//  BRD_BOX_FULL and the other still gets their child.
//
//  That is a deliberate departure from the trade, where partial application is
//  the whole disaster. Here nothing is lost or duplicated by it: the pair
//  agreed, and one player's housekeeping is not a reason to deny the other the
//  thing they both said yes to. The peer is TOLD (BREED_DONE carries the
//  reason) so the screen can say "your side filed it, theirs could not" instead
//  of silently claiming a success that only half happened.
// =============================================================================
#ifndef ER_NETWORKING_BREED_LINK_H
#define ER_NETWORKING_BREED_LINK_H

#include <stdint.h>

#include "../game/breeding.h"     // BreedPlan, BreedReject
#include "session.h"

// -----------------------------------------------------------------------------
//  WHERE THE PAIRING HAS GOT TO. For the screen, and so a test can assert on
//  something better than "the session is still open".
// -----------------------------------------------------------------------------
enum BreedLinkPhase : uint8_t {
  BLP_IDLE = 0,
  BLP_OFFERED,      // our parent is out; theirs has not arrived
  BLP_REVIEW,       // both parents are on the table; verdicts are in flight
  BLP_ASK_PLAYER,   // the pair is legal on both sides. Now the people decide
  BLP_WAIT_PEER,    // we confirmed; the peer has not
  BLP_FILED,        // our child is in our Box (or BRD_BOX_FULL said why not)
  BLP_DONE,         // and the peer told us how its own commit went
  BLP_REFUSED,      // a named refusal; nothing was filed anywhere
  BLP_PHASE_COUNT
};

const char* breed_phase_name(BreedLinkPhase p);

// -----------------------------------------------------------------------------
//  THE THREE THINGS THIS MODULE CANNOT DO ITSELF.
//
//  It is three and not the trade's five because there is no journal to drive.
//
//  `commit` files the plan and answers a game/breeding.h BreedReject as a
//  uint8_t - carried, printed and put on the wire, never interpreted here.
//  networking/ does not include game/breeding.h for its MEANING, only for
//  BreedPlan's layout.
//
//  `save` flushes the Box the commit wrote. Separate from `commit` because
//  breed_commit() performs no I/O by contract, so somebody above has to.
// -----------------------------------------------------------------------------
struct BreedHooks {
  uint8_t (*commit)(void* ctx, const BreedPlan& plan, uint8_t& slot_out);
  bool    (*save)(void* ctx);
  void    (*abort)(void* ctx);
  void*   ctx;
};

// -----------------------------------------------------------------------------
//  THE BREEDING, caller-owned exactly as TradeLink and BattleState are.
// -----------------------------------------------------------------------------
struct BreedLink {
  BreedHooks hooks;

  uint8_t  in_rec[BUGW_BYTES];   // their parent, as received
  BreedPlan plan;                // the child both ends computed

  uint16_t in_crc;               // the identity of THEIR parent
  uint16_t plan_crc;             // the identity of THIS child, both ends
  uint16_t peer_plan_crc;        // what they say they computed

  uint8_t  have_in;              // an OFFER arrived
  uint8_t  have_plan;            // breed_compute() ran and succeeded
  uint8_t  in_verdict;           // OUR VReject on their record
  uint8_t  pair_reject;          // OUR BreedReject on the pair
  uint8_t  peer_ready;           // their READY arrived
  uint8_t  peer_verdict;         // THEIR VReject on ours
  uint8_t  peer_pair_reject;     // THEIR BreedReject on the pair
  uint8_t  local_accept;         // our player pressed A
  uint8_t  peer_accept;          // their CONFIRM (or DONE) arrived
  uint8_t  peer_done;            // their DONE arrived
  uint8_t  filed;                // we ran breed_commit()
  uint8_t  commit_reject;        // OUR BreedReject from breed_commit()
  uint8_t  peer_commit_reject;   // THEIRS, so the screen can be honest
  uint8_t  slot;                 // where our child landed; BOX_SLOT_NONE if not
  uint8_t  phase;                // BreedLinkPhase
  uint8_t  reack;                // the re-answer leash, as the handshake has
};

// Wires the hooks. Call it AFTER session_set_breed() and BEFORE session_start().
void breed_link_init(BreedLink& bl, Session& s, const BreedHooks& hooks);

// SS_BREED is entered here, from networking/session.cpp's enter_operation().
void breed_link_begin(Session& s);

// One decoded frame, in SS_BREED.
void breed_link_on_msg(Session& s, const ProtoMsg& m);

// The ladder's regeneration for SS_BREED. Everything it sends is rebuilt from
// the BreedLink; nothing is stored as bytes.
void breed_link_resend(Session& s);

// TRUE when both parents are on the table, both sides judged the pair legal,
// and the only thing left is the local player. The screen shows the child here.
bool breed_link_wants_consent(const Session& s);

// THE CONSENT. The ONE thing in this module a player does, and nothing is filed
// without it. A second call is ignored.
void breed_link_accept(Session& s);

// The child both ends agreed on, or nullptr before there is one. The screen
// draws it; nothing outside may write it.
const BreedPlan* breed_link_plan(const Session& s);

// TRUE only where THIS device filed a child AND the peer told us how its own
// commit went. The breeding's twin of trade_link_completed(), and the same
// shape for the same reason: one expression, in one place.
bool breed_link_completed(const Session& s);

#endif  // ER_NETWORKING_BREED_LINK_H
