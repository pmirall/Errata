// =============================================================================
//  ERRATA - game/battle.h
//  THE BATTLE ENGINE (spec section 14, plan P4-C2).
//
//  A battle is 3 Bugs a side, one active each, resolved one whole ROUND at a
//  time through the nine numbered steps spec section 14 lists. The nine are
//  nine functions with those numbers in their names, called once each in order
//  by battle_step_round(), so a reviewer can diff the driver against the spec
//  line by line:
//
//     1 validate action     battle_s1_validate_action
//     2 determine priority  battle_s2_priority
//     3 effective speed     battle_s3_determine_order
//     4 resolve first       battle_s4_resolve_first
//     5 resolve second      battle_s5_resolve_second
//     6 process fainting    battle_s6_process_fainting
//     7 process status      battle_s7_process_status
//     8 end of round        battle_s8_end_of_round
//     9 check victory       battle_s9_check_victory
//
//  PURE MODULE. stdint, core/rng.h, the generated content tables, the save
//  schema and game/bug.h. NO Arduino.h, no gfx.h, no u8g2, no render.h; no
//  heap, no std:: container, no floating point, no clock, no I/O, and NO
//  FILE-SCOPE MUTABLE VARIABLE in battle.cpp - which is what lets P4-C5's
//  loopback run two engines in one process. tests/ compiles it directly and
//  tools/check.sh greps src/game for the forbidden includes.
//
// -----------------------------------------------------------------------------
//  DETERMINISM, AND WHAT IT COSTS
// -----------------------------------------------------------------------------
//  Every random draw comes from `BattleState.rng`, a per-battle Rng seeded from
//  ONE u32 (BattleSetup.seed). The module never touches a named RNG stream and
//  never calls esp_random: a named stream's cursor is shared with the rest of
//  the firmware, so a care roll on one device and not the other would silently
//  desynchronise two peers that are otherwise playing the same battle. The
//  draw budget is fixed and small, and it is the determinism contract:
//
//     step 3   1 draw   IFF priority ties AND effective speed ties
//     step 4   0 draws if the actor is stunned or switching; else 1 accuracy
//              draw, plus 1 damage draw IFF it hit and power > 0
//     step 5   the same 0-2, or 0 when the corpse rule skipped the action
//     steps 1, 2, 6, 7, 8, 9   ZERO
//
//  There is NO coin flip anywhere in this module - not at the round cap, not on
//  a double KO. data/balance.h refused one for the round cap because it makes a
//  replay depend on the stream position; a double KO is the same situation and
//  gets the same cross-multiplied rule, not a second rule.
//
//  Step 7 draws nothing IN PARTICULAR: DOT and EFF_CORRUPT are deterministic in
//  the tuned simulator (tools/content/sim_engine.py end_of_round / take_turn),
//  so there is deliberately NO passive infection roll here. That leaves two
//  battle constants with no consumer, and they are named rather than left
//  implicitly absent: CORRUPT_BATTLE_INFECT_PERMILLE (data/balance.h) and
//  attack 12 Infectar's effect_value 35. Adding a draw the tuning never saw is
//  the cheapest possible way to desynchronise two firmware versions.
//
//  REPLAY NEVER RUNS THE AI. P4-C3's game/battle_ai.cpp gets its OWN Rng and
//  its draws must never touch st.rng: an AI that reaches into the battle stream
//  moves the cursor on the device that is choosing and not on the one that is
//  replaying, and the logs stop reproducing. tools/check.sh greps
//  src/game/battle* for the named-stream calls; the AI's own stream is its
//  business, the battle stream is not.
//
// -----------------------------------------------------------------------------
//  WHAT AN INVALID ACTION DOES: NOTHING
// -----------------------------------------------------------------------------
//  A BattleAction is two untrusted bytes, i.e. 65,536 bit patterns, and every
//  one of them maps either to a legal action or to a NAMED refusal. The engine
//  NEVER CLAMPS a peer-supplied value: clamping turns an illegal action into a
//  legal one, which is precisely "injecting illegal data" (spec section 67).
//
//  The guarantee is "never writes", not "writes then restores", and four
//  mechanisms hold it up - the first two enforced by the compiler:
//    1. battle_validate_action() takes a CONST BattleState&. Because Rng lives
//       inside BattleState, const also freezes the random cursor: rng_next()
//       will not bind to a const member, so "stash the attempt", "decrement the
//       cooldown you just checked" and "roll for the tie you are about to need"
//       DO NOT COMPILE.
//    2. battle_submit_action() has exactly one write site, unreachable on a
//       reject: it returns before the two bytes it may ever write.
//    3. The range checks come first and NO ARRAY IS INDEXED until they pass.
//    4. The reason is a named enum, never a bool. A test asserting `!= BR_OK`
//       still passes when the WRONG guard fires and still passes after the
//       guard it names is deleted, which is the exact shape of the defect
//       P4-C1 shipped; so every code is distinguishable and every test in
//       tests/test_battle.cpp asserts the exact one AND carries a positive
//       control that returns BR_OK on the same fixture.
//
// -----------------------------------------------------------------------------
//  NAMED DIVERGENCES FROM tools/content/sim_engine.py
// -----------------------------------------------------------------------------
//  A reader WILL diff this engine against the Python simulator the roster was
//  tuned with, so the five places they disagree are written down here:
//
//    * PRIORITY. AttackDef.priority is honoured here (spec section 14 step 2);
//      battle_3v3() ignores it entirely and orders on speed alone.
//    * BUFF DURATION. Stages expire here on stage_left; in the simulator
//      end_of_round() never touches f.st, so every stage is permanent.
//    * THE TIMEOUT. Cross-multiplied here, per data/balance.h; the simulator
//      sums raw HP.
//    * TIES. An honest BO_DRAW here; the simulator flips a coin.
//    * FORCED REPLACEMENT. It costs the turn here (see below); the simulator
//      replaces a fainted fighter free at the end of the round.
//
//  THE CONSEQUENCE, STATED RATHER THAN IMPLIED: the win-rate matrix the roster
//  was tuned against is a measured claim about the PYTHON MODEL, not about this
//  file, and nobody may quote those percentages as if they had been measured
//  here. Re-running the simulator with priority, stage durations and the
//  cross-multiplied timeout is the honest fix and it may move the roster.
//
//  FORCED REPLACEMENT COSTS THE TURN, and the reason is not aesthetic:
//  P4-C5's lockstep exchanges exactly ONE ACTION per side per round and spec
//  section 15's message set has no replacement message, so a free-replacement
//  sub-phase would be a step 0 that neither section 14 nor section 15 has. The
//  cost is real and is recorded here: up to two extra lost turns per 3v3
//  against the tuning.
//
//  SWITCHING RESOLVES FIRST (BATTLE_SWITCH_PRIORITY). Spec section 14 says
//  "switching costs the turn" and says nothing about WHEN it resolves. A switch
//  that resolved second would make "costs the turn" meaningless, because the
//  outgoing Bug would eat the hit anyway. A reasonable reader could pick the
//  opposite, and two peers that shipped different answers would desynchronise
//  from round 1 - so it is a decision, written beside its constant.
// =============================================================================
#ifndef ER_GAME_BATTLE_H
#define ER_GAME_BATTLE_H

#include <stdint.h>
#include <stddef.h>

#include "../core/rng.h"
#include "../data/attacks_table.h"
#include "../data/balance.h"
#include "../data/content_version.h"
#include "../data/species_table.h"
#include "../persistence/save_schema.h"

// -----------------------------------------------------------------------------
//  VERSIONS
//
//  ALL THREE - BATTLE_ENGINE_VER, BATTLE_HASH_VERSION and CONTENT_VERSION - are
//  mixed into the FNV BASIS of battle_state_hash() rather than stored as state
//  bytes, so a peer running different rules or a different content pack can
//  never produce a matching hash - "we are playing different games" surfaces as
//  a round-1 desync - at a cost of zero bytes of state.
//
//  BATTLE_ENGINE_VER MUST be bumped by any commit that changes what a round
//  does, or what battle_init() will accept; CONTENT_VERSION is generated and
//  moves on its own; BATTLE_HASH_VERSION belongs to the HASH FUNCTION and moves
//  only when the mixing itself changes.
//
//  THE P4-C2/C3 FOLLOW-UP FIXED THIS BLOCK AND THE CODE UNDER IT. Until then the
//  paragraph above promised the round-1 guarantee for BATTLE_ENGINE_VER and the
//  basis carried BATTLE_HASH_VERSION instead, so bumping the engine version - the
//  very thing the next sentence orders a maintainer to do - moved no hash at all.
//  Not an argument: at commit 9997ed4, BATTLE_ENGINE_VER appeared in battle.cpp
//  exactly three times, and all three were the setup-carried version check
//  (battle_setup_clear, battle_init, battle_replay). It reached the hash
//  nowhere, so EVERY state hashed the same at either version.
//  battle_hash_basis() below is the parameterised form, and
//  tests/test_battle.cpp requires a neighbouring value of EACH of the three to
//  change it - a claim a host test can check without recompiling the engine
//  three times.
// -----------------------------------------------------------------------------
#define BATTLE_ENGINE_VER   3u    // P9-C5: battle_init() now carries the STORED
                                  // PBS_CORRUPTED into BattleCombatant.corrupt_left
#define BATTLE_HASH_VERSION 2u    // the basis mixing changed with it

// CORRUPTION ENTERS A FIGHT THROUGH THE BugInstance THE CALLER ALREADY
// SUPPLIES, NOT THROUGH A NEW BattleSetup FIELD, AND THAT IS A DECISION.
//
// The obvious cut is a `uint8_t corrupt_rounds[2][BATTLE_TEAM_MAX]` in
// BattleSetup, filled by whoever holds the clock. It was rejected for three
// reasons, in order of weight:
//
//   1. IT WOULD BE A SECOND SOURCE OF TRUTH FOR ONE FACT. BattleSetup already
//      carries the whole BugInstance, `status` included. A parallel array
//      the caller fills from that same byte is a copy that can disagree with
//      it, and battle_init() would have no way to tell which one was right.
//
//   2. THE ENGINE STILL READS NO CLOCK. The paragraph below about there being
//      "no clock argument, no callback, no frame, no millis()" is intact: what
//      is read is a STATUS BIT, and `corrupt_until_epoch` - the seconds-shaped
//      half - is ignored here exactly as it always was. Whoever holds the clock
//      is still the one who decides whether that bit is still true, and since
//      P9-C5 app/app.cpp's logic tick does it once a second through
//      cor_service().
//
//   3. BattleSetup IS THE REPLAY INPUT AND THE VERSION-CHECKED RECORD TWO PEERS
//      AGREE ON. Growing it would move sizeof(BattleSetup), re-derive the
//      padding proof above it and change what a recorded setup means - for a
//      byte that is already inside it.
//
// THE CONSEQUENCE, STATED RATHER THAN DISCOVERED: A LINKED BATTLE IS NEVER
// CORRUPTED, ON EITHER SIDE. networking/session.cpp's load_own_team() decodes
// even the LOCAL team through pbw_decode(), which is what makes the two
// endpoints' BattleSetups byte-identical - and networking/protocol.h's
// BUGW_STATUS_MASK deliberately refuses PBS_CORRUPTED on the wire, because it is
// EVOC_CORRUPTED's input and a forged bit would hand the receiver a free
// evolution condition. So both peers see the bit stripped, both derive
// corrupt_left == 0, and the lockstep hash agrees at round 1. A practice battle
// built straight off the Box (ui/screen_battle.cpp) does carry it. That is the
// same shape as the armed BATTLE_MOD in game/inventory.h - local, not linked -
// and tests/test_corruption.cpp pins the symmetry rather than leaving it to a
// reader to notice.
static_assert((int)CORRUPT_BATTLE_ROUNDS == (int)BATTLE_MAX_ROUNDS,
              "a stored corruption must last exactly as long as a battle can");

// A switch outruns every attack in the pack. The constexpr guard below fails
// the BUILD if a content pack ever ships a move at or above this priority,
// rather than letting a switch quietly stop being first.
#define BATTLE_SWITCH_PRIORITY  6

// RLE_STAGE carries a signed stage in an unsigned field. The bias is a
// constant, not a cast, so a transcript reader has one rule to apply.
#define BATTLE_STAGE_BIAS   128

// -----------------------------------------------------------------------------
//  ACTIONS
// -----------------------------------------------------------------------------
// BACT_, not ACT_: core/nt_types.h already owns ACT_NONE / ACT_FEED_MEAL / ...
// for the CARE actions, tree-wide, and a battle action is a different thing.
enum BattleActKind : uint8_t {
  BACT_NONE = 0,          // "has not submitted this round"; also an illegal action
  BACT_ATTACK = 1,        // index = move slot 0..ER_MOVE_COUNT-1
  BACT_SWITCH = 2,        // index = team slot 0..BATTLE_TEAM_MAX-1
  BACT_KIND_COUNT
};

// The whole wire payload of a P4-C5 ACTION message: two untrusted bytes.
//
// WHAT THE ENGINE CANNOT ANSWER ABOUT ONE, said here so P4-C5 does not assume
// it is covered: battle_validate_action() refuses a side above 1 by name, but
// nothing in this file can tell whether the PEER that sent an action is the one
// entitled to play that side. There is no identity in a BattleAction and there
// is deliberately none in BattleState either. Binding a connection to a side is
// the transport's job.
struct BattleAction {
  uint8_t kind;
  uint8_t index;
};
static_assert(sizeof(BattleAction) == 2, "BattleAction is the 2-byte wire action");

// Why an action, or an init, was refused. NEVER a bool: see the header banner.
enum BattleReject : uint8_t {
  BR_OK = 0,
  // --- per-action -----------------------------------------------------------
  BR_NOT_RUNNING,          // phase != BP_RUNNING, or the battle is already over
  BR_BAD_SIDE,             // side > 1
  BR_BAD_KIND,             // BACT_NONE, or >= BACT_KIND_COUNT
  BR_BAD_INDEX,            // index outside the range this kind allows
  BR_ALREADY_SUBMITTED,    // the FIRST submission of a round wins
  BR_MUST_SWITCH,          // the active fainted and a live bench member exists
  BR_ACTOR_FAINTED,        // the active fainted and NOTHING is left to switch to
  BR_EMPTY_ACTIVE,         // the active slot holds no Bug
  BR_UNKNOWN_MOVE,         // moves[index] == 0, or attack_get() answers nullptr
  BR_MOVE_ON_COOLDOWN,
  BR_SWITCH_TO_SELF,
  BR_SWITCH_TO_EMPTY,
  BR_SWITCH_TO_FAINTED,
  // --- init-time ------------------------------------------------------------
  BR_TEAM_SIZE,            // count outside 1..BATTLE_TEAM_MAX
  BR_NULL_MEMBER,          // a declared slot holds no Bug (species_id or id 0)
  BR_UNKNOWN_SPECIES,      // species_get() answers nullptr
  BR_BAD_LEVEL,            // outside 1..ER_LEVEL_MAX
  BR_ILLEGAL_MOVESET,      // one of the four moves fails attack_get()
  BR_UNLEARNABLE_MOVE,     // the four moves are real attacks, but no species in
                           // this one's family at this stage or below teaches
                           // exactly them - see battle.cpp's moveset_is_learnable
  BR_HP_OVER_MAX,          // hp_cur above the DERIVED hp_max
  BR_MEMBER_FAINTED,       // hp_cur == 0, or PBS_FAINTED
  BR_DUPLICATE_ID,         // the same BugInstance.id twice across the six
  BR_VERSION_MISMATCH,     // BattleSetup names another engine or content pack
  BR_LOG_INCOMPLETE,       // a replay source dropped events, or lacks an action
  BR_REJECT_COUNT
};

// -----------------------------------------------------------------------------
//  STATE
//
//  Fixed size, heap-free, POINTER-FREE and PADDING-FREE. The last two are not
//  preferences: a pointer differs between two processes, so two identical
//  battles would hash differently; and a padding hole is indeterminate, so the
//  raw-byte hash would go non-deterministic SILENTLY and a desync detector that
//  cries wolf gets switched off. The static_asserts under each struct compare
//  sizeof against the SUM OF THE MEMBERS, which is the no-padding proof, and
//  battle_init() memsets the whole object representation before writing a
//  field, which is what makes the untouched bytes defined.
// -----------------------------------------------------------------------------
enum BattleStat : uint8_t { BSTAT_ATK = 0, BSTAT_DEF, BSTAT_SPD, BSTAT_COUNT };

#define BCF_PRESENT  0x01u
#define BCF_FAINTED  0x02u

struct BattleCombatant {          // 32 B
  uint16_t hp_max;                //  0  from bug_derive_stats -> xp_hp_max
  uint16_t hp_cur;                //  2  0 == fainted
  uint8_t  species_id;            //  4  identity and the log; never indexed in a round
  uint8_t  level;                 //  5  frozen
  uint8_t  type;                  //  6  BugType, frozen: type_mod_of() reads THIS,
                                  //     so no species lookup happens in the hot path
  uint8_t  atk;                   //  7  frozen derived stat
  uint8_t  def;                   //  8
  uint8_t  spd;                   //  9
  uint8_t  flags;                 // 10  BCF_*
  uint8_t  corrupt_left;          // 11  rounds; > 0 IS "corrupted" - one fact, one
                                  //     representation, so there is no status bit
  uint8_t  moves[ER_MOVE_COUNT];  // 12  AttackId, each resolved at init
  uint8_t  cooldown[ER_MOVE_COUNT]; // 16  rounds unavailable, per move slot
  int8_t   stage[BSTAT_COUNT];    // 20  already clamped to BUFF_STAGE_MIN..MAX
  uint8_t  stage_left[BSTAT_COUNT]; // 23  rounds; 0 implies stage[k] == 0
  uint8_t  protect_left;          // 26
  uint8_t  dot_value;             // 27  damage per round of the single DOT slot
  uint8_t  dot_left;              // 28
  uint8_t  stun_left;             // 29
  uint8_t  type_edge_left;        // 30  TYPE_MOD_MAX_HITS budget, PER COMBATANT
  uint8_t  reserved;              // 31  must be 0
};
static_assert(sizeof(BattleCombatant) == 32, "BattleCombatant layout drifted");
static_assert(sizeof(BattleCombatant) ==
                  2 + 2 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 +
                  ER_MOVE_COUNT + ER_MOVE_COUNT +
                  (int)BSTAT_COUNT + (int)BSTAT_COUNT + 1 + 1 + 1 + 1 + 1 + 1,
              "BattleCombatant has a padding hole: the raw-byte hash would be "
              "non-deterministic and the memcmp rejection proof would be unsound");
static_assert(offsetof(BattleCombatant, moves) == 12, "BattleCombatant.moves moved");
static_assert(offsetof(BattleCombatant, stage) == 20, "BattleCombatant.stage moved");
static_assert(offsetof(BattleCombatant, type_edge_left) == 30,
              "BattleCombatant.type_edge_left moved");

struct BattleSide {                            // 100 B
  BattleCombatant team[BATTLE_TEAM_MAX];       //  0
  uint8_t         active;                      // 96  0..BATTLE_TEAM_MAX-1
  uint8_t         pending_kind;                // 97  BACT_NONE == not submitted
  uint8_t         pending_index;               // 98
  uint8_t         reserved;                    // 99  must be 0
};
// No alive_count, no owes_switch, no per-side type-edge counter: all three are
// DERIVABLE, and a stored copy of a derivable fact is a second source of truth
// the hash would then have to defend.
static_assert(sizeof(BattleSide) == BATTLE_TEAM_MAX * sizeof(BattleCombatant) + 4,
              "BattleSide has a padding hole");
static_assert(sizeof(BattleSide) == 100, "BattleSide layout drifted");

enum BattlePhase : uint8_t { BP_INIT = 0, BP_RUNNING = 1 };

// "Over" is NOT a phase: it is outcome != BO_UNDECIDED, so the two cannot
// disagree with each other.
enum BattleOutcome : uint8_t {
  BO_UNDECIDED = 0,
  BO_WIN_A,
  BO_WIN_B,
  BO_DRAW,
  BO_ABORT        // step 1 found the state moved under a submitted action;
                  // P4-C5 maps this to BATTLE_END(DESYNC)
};

struct BattleState {              // 212 B
  BattleSide side[2];             //   0
  Rng        rng;                 // 200  THE per-battle stream. Inside the state
                                  //      is what makes the cursor hashable AND
                                  //      what stops a const validator drawing.
  uint16_t   round;               // 204  1-based; the round being resolved NOW
  uint8_t    phase;               // 206  BattlePhase
  uint8_t    outcome;             // 207  BattleOutcome
  uint8_t    reserved[4];         // 208  must be 0
};
static_assert(sizeof(BattleState) == 2 * sizeof(BattleSide) + sizeof(Rng) + 8,
              "BattleState has a padding hole");
static_assert(sizeof(BattleState) == 212, "BattleState layout drifted");
static_assert(offsetof(BattleState, side[1]) == 100, "BattleState.side[1] moved");
static_assert(offsetof(BattleState, rng) == 200, "BattleState.rng moved");
static_assert(offsetof(BattleState, round) == 204, "BattleState.round moved");
static_assert(offsetof(BattleState, outcome) == 207, "BattleState.outcome moved");

// -----------------------------------------------------------------------------
//  THE REPRODUCIBLE INPUT
//
//  A replay re-runs bug_derive_stats() from the STORED Bugs rather than
//  copying their derived numbers, because a derivation bug that is copied
//  cannot be reproduced. engine_ver / content_ver are checked by name so a
//  setup recorded against other rules refuses instead of reproducing a
//  different bug quietly.
// -----------------------------------------------------------------------------
struct BattleSetup {
  uint32_t       seed;                              //  0
  uint16_t       engine_ver;                        //  4
  uint16_t       content_ver;                       //  6
  uint8_t        count[2];                          //  8  1..BATTLE_TEAM_MAX each
  uint8_t        reserved[2];                       // 10  must be 0
  BugInstance member[2][BATTLE_TEAM_MAX];        // 12
};
static_assert(sizeof(BattleSetup) ==
                  12 + 2 * BATTLE_TEAM_MAX * sizeof(BugInstance),
              "BattleSetup has a padding hole");

// Stamps this build's engine and content versions and zeroes everything else.
void battle_setup_clear(BattleSetup& s);

// -----------------------------------------------------------------------------
//  THE LOG
//
//  A CALLER-PROVIDED ring, deliberately OUTSIDE BattleState. The device hands
//  it a 32-entry static array; a host golden run hands it thousands; neither
//  needs the other's constant, and because the log is never hashed those two
//  builds do not disagree every round while playing identically.
//
//  THE HONEST CONSEQUENCE, stated rather than hidden: a log-writing bug is
//  invisible to P4-C5's lockstep hash. So tests/test_battle.cpp asserts log
//  contents directly and tests/test_battle_golden.cpp diffs a recorded
//  transcript. The hash does not stand in for either.
//
//  `dropped` SATURATES rather than wrapping: a ring that overflows silently
//  produces a log you cannot debug from, and spec section 14 asks for exactly
//  the opposite.
// -----------------------------------------------------------------------------
enum BattleLogEvent : uint8_t {
  RLE_NONE = 0,
  RLE_ROUND_BEGIN,   // v = state hash BEFORE the round
  RLE_ACTION,        // side, slot = active, a = BattleActKind, b = action index
  RLE_ORDER,         // a = the side that moves first, b = its effective speed
  RLE_SWITCH,        // side, slot = INCOMING slot, b = its hp_cur
  RLE_SKIPPED,       // side, a = BattleSkipReason
  RLE_MISS,          // side, slot, a = move slot
  RLE_HIT,           // side/slot = ATTACKER, a = move slot, b = damage dealt
  RLE_TYPE_EDGE,     // side/slot = ATTACKER, a = 1 advantage / 2 disadvantage,
                     //   b = type_edge_left AFTER the spend. Pushed just before
                     //   the RLE_HIT it belongs to, so a reader sees the reason
                     //   and then the number. IT EXISTS BECAUSE THE CAP WAS
                     //   INVISIBLE: TYPE_MOD_MAX_HITS is 1 and nothing told the
                     //   player which hit got it or that it was now gone, so the
                     //   most consequential rule in the fight was one the player
                     //   could not see. The cap is unchanged; only its reporting
                     //   is new.
  RLE_HP,            // side, slot, b = hp_cur AFTER
  RLE_STAGE,         // side, slot, a = BattleStat, b = stage AFTER + BATTLE_STAGE_BIAS
  RLE_PROTECT,       // side, slot, b = protect_left after
  RLE_DOT,           // side, slot, a = damage taken, b = hp_cur after
  RLE_CORRUPT,       // side, slot, b = corrupt_left after
  RLE_STUN,          // side, slot, b = stun_left after
  RLE_CLEANSE,       // side, slot
  RLE_FAINT,         // side, slot
  RLE_ROUND_END,     // v = state hash AFTER the round
  RLE_BATTLE_END,    // a = BattleOutcome
  RLE_COUNT
};

enum BattleSkipReason : uint8_t {
  BSK_STUNNED = 0,
  BSK_ACTOR_FAINTED,
  BSK_TARGET_FAINTED,
  BSK_COUNT
};

// Every event carries the RESULTING value (hp after, stage after, damage
// dealt), never a delta: it costs nothing and it makes a transcript readable
// and diffable without replaying it in your head.
struct BattleEvent {   // 12 B
  uint8_t  kind;       // BattleLogEvent
  uint8_t  side;       // 0/1, or 0xFF when the event belongs to no side
  uint8_t  slot;       // team slot
  uint8_t  a;          // kind-specific small field
  uint16_t b;          // kind-specific RESULTING value
  uint16_t round;
  uint32_t v;          // the 32-bit state hash on a round boundary, else 0
};
static_assert(sizeof(BattleEvent) == 12, "BattleEvent layout drifted");
static_assert(sizeof(BattleEvent) == 1 + 1 + 1 + 1 + 2 + 2 + 4,
              "BattleEvent has a padding hole");

struct BattleLog {
  BattleEvent* ev;
  uint16_t     cap;
  uint16_t     head;      // next write slot
  uint16_t     count;     // entries held, <= cap
  uint16_t     dropped;   // saturating; non-zero means the transcript has holes
};

void   battle_log_init(BattleLog& l, BattleEvent* buf, uint16_t cap);
// The i-th OLDEST event held, or nullptr past the end.
const BattleEvent* battle_log_at(const BattleLog& l, uint16_t i);

// -----------------------------------------------------------------------------
//  LIFECYCLE
// -----------------------------------------------------------------------------
// Builds a battle from two stored teams. Validates the whole setup - the engine
// must be safe when called by anything and must not assume its caller checked.
// ON ANY REJECTION the state is memset back to zero and phase stays BP_INIT, so
// a failed init is bit-defined too.
//
// WHAT IT CHECKS, named rather than summarised as "everything", because the
// P4-C2/C3 review found the summary hiding a hole: the team size, each member's
// species, level, hp against the DERIVED hp_max, faint status, the ids being
// distinct across all six, the engine and content versions - and, since that
// review, that each member's four moves are a moveset its species could
// actually have (spec section 67: a peer must not be able to hand its Bug a
// move the content never gave it). Measured before the fix: injecting attack 11
// Plaga onto a species-1 Paketo took a scripted 1v1 from 0 wins in 200 seeds to
// 100, and the on-type attack 3 Rafaga to 103. What it does NOT check is
// anything about the OWNERSHIP of a team: whether the peer that sent it may play
// side 0 or side 1 is a transport question and P4-C5's to answer, and the
// BattleAction comment above says the same thing about a single action.
BattleReject battle_init(BattleState& st, const BattleSetup& setup);

// Refuses or records one side's action for this round. The ONLY function that
// writes an untrusted value, and it writes exactly two bytes at two fixed
// offsets, after the only early return.
BattleReject battle_submit_action(BattleState& st, uint8_t side, BattleAction act);

// Turn/phase bookkeeping plus battle_action_legal_now(). CONST: see the banner.
BattleReject battle_validate_action(const BattleState& st, uint8_t side, BattleAction act);

// The actor/target/move rules, with exactly one owner. Step 1 re-runs THIS
// against the state as it stands at the top of the round.
BattleReject battle_action_legal_now(const BattleState& st, uint8_t side, BattleAction act);

enum BattleStepResult : uint8_t {
  BS_NEED_ACTIONS = 0,   // state bit-identical: nothing was written
  BS_ROUND_DONE,
  BS_BATTLE_OVER
};

// ONE CALL IS ONE WHOLE ROUND. No clock argument, no callback, no frame, no
// millis(): every duration in this engine is counted in ROUNDS. The one
// seconds-shaped constant in the domain, CORRUPT_DURATION_S, belongs to the
// out-of-battle timer, so a battle REPORTS an infection and the caller stamps
// the clock.
BattleStepResult battle_step_round(BattleState& st, BattleLog* log);

// -----------------------------------------------------------------------------
//  THE NINE STEPS (spec section 14). Public so a host test can call any one in
//  isolation and hash before and after.
//
//  Steps 1, 2 and 9 are CONST queries. Steps 4 and 5 take the side that acts,
//  because the turn order is a per-round transient and storing it in
//  BattleState would put a derivable fact into the hashed state.
// -----------------------------------------------------------------------------
bool          battle_s1_validate_action(const BattleState& st);
int8_t        battle_s2_priority(const BattleState& st, uint8_t side);
uint8_t       battle_s3_determine_order(BattleState& st, int8_t pri_a, int8_t pri_b);
void          battle_s4_resolve_first(BattleState& st, uint8_t side, BattleLog* log);
void          battle_s5_resolve_second(BattleState& st, uint8_t side, BattleLog* log);
void          battle_s6_process_fainting(BattleState& st, BattleLog* log);
void          battle_s7_process_status(BattleState& st, BattleLog* log);
void          battle_s8_end_of_round(BattleState& st, BattleLog* log);
BattleOutcome battle_s9_check_victory(const BattleState& st);

// -----------------------------------------------------------------------------
//  CONST QUERIES - also exactly the surface P4-C3's AI needs, so it never
//  requires privileged access to the state it is choosing for.
// -----------------------------------------------------------------------------
const BattleCombatant* battle_combatant(const BattleState& st, uint8_t side, uint8_t slot);
const BattleCombatant* battle_active(const BattleState& st, uint8_t side);

// max(STAT_EFF_MIN, base + clamp(stage) + corruption). Computed SIGNED in
// int16_t throughout: the stat is uint8_t, a stage is negative, and an unsigned
// subtraction there wraps to 65,535 and looks like an invincible Bug
// (data/balance.h names the failure). The corruption term rides OUTSIDE the
// +-2 clamp - measured from sim_engine.py Fighter.eff(), so a corrupted,
// +2-buffed attacker reaching an effective +3 is intended, not a clamp bug.
uint16_t battle_stat_eff(const BattleCombatant& c, uint8_t stat);
uint16_t battle_effective_speed(const BattleCombatant& c);
bool     battle_move_ready(const BattleCombatant& c, uint8_t slot);
bool     battle_side_must_switch(const BattleState& st, uint8_t side);
uint8_t  battle_alive_count(const BattleState& st, uint8_t side);

// -----------------------------------------------------------------------------
//  THE TWO PURE HALVES OF A HIT (added by P4-C3)
//
//  Both are CONST, side-effect free and DRAWLESS. They were factored out of
//  resolve_one()/compute_damage() for exactly one reason: game/battle_ai.cpp has
//  to predict what a move would do, and a chooser carrying its own copy of the
//  damage formula is a SECOND IMPLEMENTATION that drifts from this one the first
//  time a constant in data/balance.h moves. The engine calls them with the type
//  modifier it has DECIDED AND SPENT; the AI calls them with the modifier it
//  PREDICTS. Neither function touches the type-edge budget, because only the
//  engine may spend it.
//
//  Factoring them changed no behaviour: tests/golden/battle_v1.txt is a 22-round
//  transcript recorded before this commit and is asserted event by event after
//  it, so a refactor that moved a single number would fail it.
// -----------------------------------------------------------------------------

// The damage a hit deals BEFORE the roll and BEFORE protection, for an
// already-decided type modifier m in -1..+1 (data/balance.h publishes the
// formula). An m outside that range is treated as neutral rather than indexing
// TYPE_MUL_NUM past its end - defence on a public function, not a clamp of any
// wire value: type_mod_of() cannot produce one and no caller in this tree does.
uint16_t battle_damage_pre_roll(const BattleCombatant& u, const BattleCombatant& f,
                                const AttackDef& a, int8_t m);

// Accuracy after evasion and the ACCURACY_MIN floor, in 0..ACCURACY_ROLL_SPAN.
// A power-0 move rolls against RAW accuracy with no evasion term, which is the
// engine's rule and not a simplification.
uint8_t  battle_accuracy_eff(const BattleCombatant& u, const BattleCombatant& f,
                             const AttackDef& a);

// -----------------------------------------------------------------------------
//  THE HASH
//
//  FNV-1a 32 over ALL sizeof(BattleState) raw bytes - the whole object
//  representation - starting from a basis into which BATTLE_ENGINE_VER,
//  BATTLE_HASH_VERSION and CONTENT_VERSION have been mixed BYTE BY BYTE through
//  the same FNV step, and not XORed in. The difference is not cosmetic: an XOR
//  fold collides, and (BATTLE_HASH_VERSION 1, CONTENT_VERSION 0x5B4A) and
//  (3, 0x5B48) both give 0x5B4B - two different builds satisfying one version
//  guard. Mixing costs twelve FNV steps once per hash.
//
//  COVERED, without a judgement call: both teams' three slots INCLUDING the
//  benched and the fainted ones, every duration, every cooldown, every stage,
//  the type-edge budget, the active slot, the pending action, the round, the
//  phase, the outcome - and rng.s, which is what gives it teeth: a divergence
//  in the NUMBER OF DRAWS TAKEN moves the cursor before it has changed a single
//  visible number, so it is caught on the round it happens rather than three
//  rounds later when it finally alters a damage roll. Hashing the whole struct
//  also means a field added next year is covered automatically instead of being
//  forgotten in a longhand list.
//
//  EXCLUDED: the BattleLog in its entirety, structurally - it is not in the
//  struct. Nothing derived, because nothing derived is stored. No pointers,
//  because there are none.
//
//  WHAT IT CANNOT SEE, and this one is worth knowing before leaning on it as a
//  desync detector: the ORDER of steps 6 and 7. Spec section 14 numbers fainting
//  before status, and swapping the two calls in battle_step_round() leaves every
//  round hash and the final hash BYTE-IDENTICAL across a whole battle - measured
//  - because tick_status() does not consult BCF_FAINTED, so a corpse ticks the
//  same whether it was flagged before or after. Two peers that shipped opposite
//  orders would agree on every hash they exchanged. tests/golden/battle_v1.txt
//  (which watches the LOG, where the order does show) and tools/check.sh's third
//  battle gate are the only things holding that boundary, and both say so.
//
//  STATED LIMITS, because P4-C5 puts this value on the wire: it is a 32-bit
//  NON-CRYPTOGRAPHIC hash and a MEMORY-IMAGE hash, valid between two
//  little-endian builds of the same struct (ESP32-C3 to ESP32-C3, and
//  ESP32-C3 to the x86-64 host tests). Neither limit weakens the security
//  story: this is a divergence detector between two COOPERATING engines, and
//  the defence against a LYING peer is the validator above, which never accepts
//  a peer's state at all - only its two bytes of intent.
// -----------------------------------------------------------------------------
uint32_t battle_state_hash(const BattleState& st);

// The basis battle_state_hash() starts from, PARAMETERISED. Public for exactly
// one reason: a host test cannot recompile the engine with a different
// BATTLE_ENGINE_VER, so the only way to prove that a version word actually
// reaches the hash is to ask for the basis of a neighbouring version and require
// a different answer. tests/test_battle.cpp does that for all three, and also
// rebuilds battle_state_hash() longhand from this function so that dropping a
// version word at the CALL SITE fails too.
uint32_t battle_hash_basis(uint32_t engine_ver, uint32_t hash_ver, uint32_t content_ver);

// -----------------------------------------------------------------------------
//  REPLAY
//
//  Re-runs a battle from its setup and the ACTIONS recorded in a log. It does
//  NOT re-apply the log's effects - that would be a second implementation of
//  the state transition, and the one thing worse than no replay is a replay
//  that agrees with itself. P4-C5 may earn the entry-level reconstruction; this
//  is the cheap and honest version.
//
//  first_bad_round is the LOCALISATION LADDER: a mismatch on a round's
//  hash_before says the round started already diverged; a match there followed
//  by a mismatch on hash_after says the divergence is inside that round.
// -----------------------------------------------------------------------------
struct BattleReplayReport {
  uint16_t rounds;            // rounds re-run
  uint16_t first_bad_round;   // 0 == every recorded hash matched
  uint32_t final_hash;
  uint8_t  outcome;           // BattleOutcome
  uint8_t  reserved[3];
};

BattleReject battle_replay(const BattleSetup& setup, const BattleLog& src,
                           BattleState& out, BattleLog* log,
                           BattleReplayReport& rep);

// -----------------------------------------------------------------------------
//  CONTENT GUARDS. A rule this engine relies on that the content could break.
// -----------------------------------------------------------------------------
constexpr bool battle_switch_outruns_every_attack(void) {
  for (uint8_t i = 0; i < ATTACK_COUNT; ++i)
    if (ATTACKS_TABLE[i].priority >= (int8_t)BATTLE_SWITCH_PRIORITY) return false;
  return true;
}
static_assert(battle_switch_outruns_every_attack(),
              "a content pack ships a move at or above BATTLE_SWITCH_PRIORITY, so a "
              "switch would no longer resolve first and 'switching costs the turn' "
              "would stop meaning what game/battle.h says it means");

// ONE DOT slot per combatant is EXACT, not an approximation, only while at most
// one attack row carries ATK_EFF_DOT. Ship a second and the single slot starts
// refreshing where the tuned simulator stacked - so it fails the build instead.
constexpr uint8_t battle_dot_attack_count(void) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < ATTACK_COUNT; ++i)
    if (ATTACKS_TABLE[i].effect == (uint8_t)ATK_EFF_DOT) ++n;
  return n;
}
static_assert(battle_dot_attack_count() <= 1,
              "more than one ATK_EFF_DOT attack ships: BattleCombatant's single DOT "
              "slot would refresh where sim_engine.py stacks");

// THERE IS NO "PASS" ACTION, so a side must always have something legal to do.
// The only way it could not is a learnset in which every move carries a
// cooldown; the shipped pack has none, and this fails the build if one ever
// arrives rather than letting a battle deadlock into BO_ABORT on the device.
constexpr bool battle_every_learnset_has_an_always_ready_move(void) {
  for (uint8_t i = 0; i < SPECIES_TABLE_COUNT; ++i) {
    bool ready = false;
    for (uint8_t m = 0; m < (uint8_t)ER_MOVE_COUNT; ++m) {
      const uint8_t id = SPECIES_TABLE[i].moves[m];
      // Bound first: attacks_table.h's own guard is declared before this one,
      // but THIS function indexes the table and a constexpr read past the end
      // is an error with no sentence in it rather than a false return.
      if (id < 1u || id > ATTACK_COUNT) return false;
      if (ATTACKS_TABLE[id - 1u].cooldown == 0u) ready = true;
    }
    if (!ready) return false;
  }
  return true;
}
static_assert(battle_every_learnset_has_an_always_ready_move(),
              "a learnset has a cooldown on all four moves, so its Bug could "
              "have no legal action - and this engine has no pass action");

static_assert((uint8_t)TYPE_MOD_MAX_HITS <= 255,
              "type_edge_left is one byte");
static_assert(BATTLE_TEAM_MAX <= 3, "BattleSide.active is 0..2 and the log's slot is a byte");

#endif  // ER_GAME_BATTLE_H
