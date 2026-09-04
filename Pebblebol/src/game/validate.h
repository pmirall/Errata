// =============================================================================
//  PEBBLEBOL - game/validate.h
//  THE ONE PEBBLE VALIDATOR (spec section 15, plan P4-C5).
//
//  Spec section 15 contains two governing sentences and this module exists for
//  the second one:
//
//      "Never transmit raw unvalidated game objects and trust the peer."
//      "The same validator used for custom Pebbles should be used for
//       exchanged Pebbles."
//
//  "THE SAME VALIDATOR" MEANS LITERALLY ONE CALL, NOT A SHARED SPIRIT. Every
//  CONSUMER reaches these rules through validate_pebble() rather than
//  re-deriving them. There is deliberately NO policy or scope parameter: a rule
//  that is meaningful only for a wire record belongs to the wire DECODER
//  (networking/protocol.cpp), not to a flag inside a shared function, and the
//  VR_WIRE_* codes below are produced by that decoder alone.
//
//  ONE RULE IS DELIBERATELY IMPLEMENTED TWICE AND THIS HEADER SAYS SO RATHER
//  THAN IMPLYING OTHERWISE: the family/stage learnset walk exists here AND as
//  game/battle.cpp's static moveset_is_learnable(). That is the defence in
//  depth argued for below, not an oversight - and because two checkers with
//  DIFFERENT bounds is the disagreement this project keeps finding, the two are
//  pinned to each other by a test that can fail
//  (`the_two_learnset_checkers_agree_on_every_roster_row`), not by this comment.
//
// -----------------------------------------------------------------------------
//  CONST IN. A VALIDATOR THAT CANNOT WRITE CANNOT REPAIR.
// -----------------------------------------------------------------------------
//  validate_pebble() takes a `const PebbleInstance&`, the same mechanism
//  game/battle.h uses on battle_validate_action(): a repairing validator turns
//  an illegal object into a legal one, which is precisely the injection spec
//  section 15's first sentence forbids. ui/screen_battle.cpp's copy_from_box()
//  (screen_battle.cpp:205) DOES repair - an unknown move becomes the species
//  learnset, hp is clamped to hp_max, PBS_FAINTED is cleared - and that is
//  defensible for a LOCAL Pebble the device itself created. It is not
//  defensible for one a peer sent, and the two must never be confused.
//
// -----------------------------------------------------------------------------
//  WHO CALLS IT. THREE PLACES, AND ONLY TWO OF THEM ARE LIVE TODAY.
// -----------------------------------------------------------------------------
//   1. THE WIRE - networking/protocol.cpp's pbw_decode(). LIVE. Every peer
//      Pebble is decoded in front of this function and the caller only ever
//      sees an instance that returned VR_OK. Refuse, never repair.
//   2. THE SAVE PATH - persistence/save_manager.cpp's save_load_all(), per
//      occupied slot, after pebble_blob_ok() and after migration. LIVE. It
//      QUARANTINES: the slot is flagged with its named VReject and the Box
//      still loads. Refusing the whole Box would brick a device on a content
//      pack change, because VR_UNKNOWN_SPECIES is exactly what an older save
//      legitimately produces.
//   3. THE CREATOR - P8. NOT LIVE, and this header says so rather than
//      claiming a third consumer it does not have. MEASURED at this commit:
//      ui/screen_creator.cpp is 213 lines of QR/portal screen and contains no
//      Pebble validation at all (`grep -c "valid" ui/screen_creator.cpp` == 0),
//      networking/webui.cpp registers no upload route, and data/creator_schema.h
//      enforces its budgets at COMPILE TIME over the built-in roster. There is
//      no inline creator rule for this module to absorb; there is a call site
//      for P8 to add.
//
//  A FOURTH ONE WAS CONSIDERED AND MEASURED AWAY: game/box.cpp's
//  box_new_pebble(), the tree's only Pebble constructor, does NOT call this
//  function. Making the constructor refuse a slot that fails validation reads
//  well and would break four shipped host tests, because box_new_pebble() has
//  never required a SEALED genome and tests/test_box.cpp:22,
//  tests/test_box_sim.cpp:50, tests/test_care.cpp:288 and
//  tests/test_screens.cpp:1300 all hand it a zeroed or hand-built one
//  (lineage_id 0 and/or a wrong CRC, i.e. VR_BAD_GENOME). The constructor's
//  contract is therefore unchanged, and what ships instead is the one-line
//  evo_state fix box_new_pebble() was missing plus the named test
//  `a_constructed_pebble_validates` that drives the constructor with a REAL
//  sealed genome and asserts VR_OK.
//
// -----------------------------------------------------------------------------
//  THE RELATIONSHIP WITH battle_init(), WHICH IS NOT A REFACTOR
// -----------------------------------------------------------------------------
//  game/battle.cpp keeps its own setup_member_ok(). Two independent checkers
//  with the SAME bounds is defence in depth; two with DIFFERENT bounds is the
//  disagreement this project keeps finding - so the relationship is asserted by
//  two tests that can fail rather than by this sentence:
//
//      validate_team(...) == VR_OK  =>  battle_init(...) is BR_OK or
//                                       BR_MEMBER_FAINTED and nothing else
//                                       (`a_team_the_validator_accepts_the_engine_
//                                         refuses_only_for_fainting`)
//
//      validate_team(...) == VR_OK AND validate_battle_ready(...) == VR_OK
//                                   =>  battle_init(...) == BR_OK
//                                       (`a_battle_ready_team_is_one_the_engine_
//                                         accepts_outright`)
//
//  hp_cur == 0 / PBS_FAINTED is the one engine code the SHARED BODY does not
//  own, because it is a legitimate STORED state and an illegal BATTLE state -
//  battle eligibility, not Pebble validity. It is owned by
//  validate_battle_ready() one function down instead, which is what keeps the
//  shared body free of a scope flag AND stops the engine being the first thing
//  in the tree to notice. BR_VERSION_MISMATCH is structurally unreachable on
//  the wire path: battle_setup_clear() stamps the LOCAL engine and content
//  versions and a peer sends records, never a BattleSetup - which is why the
//  CAPABILITIES message carries those words and refuses before a team is ever
//  sent. BR_DUPLICATE_ID spans BOTH teams, which no single-team function can
//  see, so networking/session.cpp owns the cross-team half and answers
//  VR_DUPLICATE_ID.
//
//  The consequence has teeth: once a caller has run validate_team(), the
//  cross-team id check and validate_battle_ready(), a BattleReject is a BUG IN
//  THIS TREE and never a peer capability, and the linked battle driver reports
//  it as an internal fault rather than blaming the peer. Before
//  validate_battle_ready() existed that sentence was FALSE for exactly one
//  code, and the driver's comment asserted it anyway.
//
// -----------------------------------------------------------------------------
//  FOUR RULES DELIBERATELY NOT WRITTEN, EACH WITH ITS REASON
// -----------------------------------------------------------------------------
//   (a) NO reserved-evolution-bits rule. tests/test_evolution.cpp:203-218
//       asserts that junk in evo_state bits 6:2 SURVIVES an evolution and
//       game/evolution.cpp:102 preserves them on purpose. A rule here would
//       contradict shipped behaviour. On the wire the whole byte is
//       unrepresentable, which is stronger than a rule.
//   (b) NO genome GENE-RANGE check. GN_TEMPER_MK, GN_HARDY_MK and GN_METAB_MK
//       are all 0x0F, so a gene is <= 15 BY CONSTRUCTION and genome variance is
//       0..2 whatever a peer sends. A guard there is a test that cannot fail.
//   (c) NO faint rule IN THE SHARED BODY - it is a battle rule and it lives in
//       validate_battle_ready(). Putting it here would quarantine a fainted
//       Pebble on the save path, where being fainted is ordinary.
//   (d) NO "level >= the evolution rule's level" rule. It is FALSE for wild
//       captures: every one of the 36 roster rows carries a nonzero spawn
//       weight and a wild level is clamped around the active Pebble's, so a
//       stage-0 creature above its evolution level is an ordinary catch.
//
// -----------------------------------------------------------------------------
//  WHAT THIS MODULE CANNOT DO, SAID HERE SO NOBODY READS MORE INTO A VR_OK
// -----------------------------------------------------------------------------
//   * A PEER RUNNING MODIFIED FIRMWARE WINS BY PLAYING LEGALLY. Nothing on the
//     wire proves a level was EARNED - PebbleInstance.xp is XP inside the
//     CURRENT level, never a cumulative total, so there is no running figure to
//     check against. "Impossible level" can only ever mean "outside 1..30".
//     VR_LEVEL_OUT_OF_BAND is a MATCHMAKING control, not a security guarantee:
//     it holds only because both sides agreed the band, and a peer may decline
//     a narrow band or send a level-12 team it never raised inside one.
//   * A FORGED GENOME IS LEGAL AND VALIDATING IT BUYS NOTHING AGAINST THAT.
//     genome_valid() checks the signature, the proto version, lineage_id != 0
//     and the CRC - and a forgery resealed with a correct CRC passes all four.
//     The genome is validated for lineage and trade integrity and for nothing
//     else. It is NOT a stat-forging defence. AND THE STAKES ARE NOT SMALL:
//     rule (b) below says variance is 0..2 by construction, which is a bound on
//     the RANGE and not a reassurance about the outcome. Measured - level 10
//     mirror match, same species and moves, AI on both sides, 200 seeds per
//     configuration - the maximum genome beats the minimum one 594 times in
//     600, symmetric across sides.
//   * A PEBBLE'S HISTORY CANNOT BE CHECKED. battles_won, battles_lost,
//     minigames_won, evolutions, trades, age_s, lifetime_active_s and every
//     epoch are checked against NOTHING here, because no rule exists to check
//     them against. There is no VReject for them and inventing one would be a
//     guard with no rule behind it.
//   * HALF OF THIS FILE HAS NO SECOND LINE. The rules battle_init() also holds
//     (species, level, moves, learnability, hp) are caught twice. The rules
//     that exist only here - the genome seal, the xp curve, the status and flag
//     masks, the evolution state, the origin, the trait, the care band and the
//     nickname termination - are guarded by nothing but their own tests. That
//     is where a hole would live.
//
// -----------------------------------------------------------------------------
//  PURE MODULE. stdint, the save schema, the generated content tables,
//  game/evolution.h, game/genome.h, game/pebble.h and game/xp.h. No Arduino, no
//  heap, no float, no clock, no RNG, no I/O. tools/check.sh greps src/game for
//  the forbidden includes and tests/test_validate.cpp compiles it directly.
// =============================================================================
#ifndef PB_GAME_VALIDATE_H
#define PB_GAME_VALIDATE_H

#include <stdint.h>

#include "../data/balance.h"               // XP_LEVEL_MAX, BATTLE_TEAM_MAX
#include "../core/config.h"                // STAT_MILLI_MAX
#include "../persistence/save_schema.h"    // PebbleInstance and its masks

// -----------------------------------------------------------------------------
//  TWO RELATIONSHIPS NOTHING ELSE IN THE TREE COMPARES, and this header is the
//  one place that reads both sides of each. Neither is reachable by the balance
//  gate in tools/check.sh: that gate maps balance.json's LEVEL_MAX onto
//  XP_LEVEL_MAX only, so PB_LEVEL_MAX is outside it entirely.
// -----------------------------------------------------------------------------
static_assert((int)PB_LEVEL_MAX == (int)XP_LEVEL_MAX,
              "the persisted level ceiling and the XP curve's ceiling disagree: "
              "a Pebble could be stored at a level the curve cannot price");
static_assert((long)PB_CARE_MILLI_MAX == (long)STAT_MILLI_MAX,
              "the persisted care ceiling and the simulator's stat ceiling disagree");

// -----------------------------------------------------------------------------
//  WHY A PEBBLE WAS REFUSED. NEVER A BOOL, for game/battle.h's reason: a test
//  written against `!= VR_OK` still passes when the WRONG guard fires and still
//  passes after the guard it names is deleted.
//
//  THE ORDER IS THE EVALUATION ORDER. The first failing reason is the answer,
//  so reordering this enum's rules in validate.cpp is a behaviour change and
//  tests/test_validate.cpp pins the exact code for every case.
// -----------------------------------------------------------------------------
enum VReject : uint8_t {
  VR_OK = 0,

  // --- SEAL AND WIRE-ONLY. Produced ONLY by networking/protocol.cpp's
  //     pbw_decode(). A stored Pebble can never carry one of these.
  VR_WIRE_MAGIC,             // the 48 B record does not start with PBW_MAGIC
  VR_WIRE_VERSION,           // wire_ver != PBW_LAYOUT_VER
  VR_WIRE_CRC,               // the record's own trailing CRC-16 failed
  VR_WIRE_RESERVED,          // reserved0 or reserved[8] carried a value
  VR_WIRE_CUSTOM_UNRESOLVED, // a custom species id, or PBF_CUSTOM /
                             // PBF_HAS_CUSTOM_SPRITE: they imply a cs* record
                             // the receiver does not hold. Its OWN code because
                             // its MEANING changes in P8 and it must never
                             // become a silent remap.
  VR_WIRE_STATUS_BITS,       // a status bit the wire may not carry. PBS_CORRUPTED
                             // is EVOC_CORRUPTED's input, so a forged bit would
                             // hand the receiver a free evolution condition.

  // --- CONTENT. Produced ONLY by validate_pebble().
  VR_NULL_ID,                // PebbleInstance.id == 0
  VR_UNKNOWN_SPECIES,        // species_get() answers nullptr (0 included)
  VR_BAD_LEVEL,              // outside 1..PB_LEVEL_MAX
  VR_BAD_XP,                 // xp >= what this level costs to leave, or != 0 at
                             // the top of the curve
  VR_BAD_GENOME,             // genome_valid() refused the seal
  VR_UNKNOWN_MOVE,           // attack_get() answers nullptr for one of the four
  VR_UNLEARNABLE_MOVESET,    // four real attacks, but no species in this one's
                             // family at this stage or below teaches exactly
                             // them, in order
  VR_HP_OVER_MAX,            // hp_cur above the DERIVED hp_max
  VR_BAD_EVO_STAGE,          // evo_state's stage bits disagree with the row
  VR_BAD_EVO_PENDING,        // EVO_STATE_PENDING set where the level gate is not
  VR_BAD_STATUS_BITS,        // a status bit outside the full stored mask
  VR_BAD_FLAGS_BITS,         // a flags bit outside the full stored mask
  VR_BAD_ORIGIN,             // origin >= ORIGIN_COUNT
  VR_BAD_TRAIT,              // trait_id != 0: no traits table exists anywhere in
                             // the tree, so a nonzero value is uninterpretable
  VR_BAD_CUSTOM_SPRITE,      // a cs slot that cannot exist, or the flag and the
                             // slot disagreeing
  VR_BAD_CARE,               // a care value outside 0..PB_CARE_MILLI_MAX
  VR_BAD_NICKNAME,           // no NUL inside PB_NICKNAME_CAP bytes

  // --- TEAM. Produced ONLY by validate_team().
  VR_TEAM_SIZE,              // count outside 1..BATTLE_TEAM_MAX
  VR_DUPLICATE_ID,           // the same PebbleInstance.id twice

  // --- SESSION-SCOPED. Produced ONLY by validate_level_band() and
  //     validate_battle_ready(). Both are about a SET in a CONTEXT, never about
  //     whether a Pebble is a legal object: the same team is in band in one
  //     session and out of it in the next, and a fainted Pebble is a perfectly
  //     legal thing to have STORED.
  VR_LEVEL_OUT_OF_BAND,      // outside the band the two peers agreed
  VR_MEMBER_FAINTED,         // hp_cur == 0 or PBS_FAINTED: legal to store,
                             // illegal to bring to a battle. It is the engine's
                             // BR_MEMBER_FAINTED, named ONE LAYER EARLIER so a
                             // peer that sends one is answered as a peer.

  VR_REJECT_COUNT
};

// -----------------------------------------------------------------------------
//  THE API. FIVE functions, and no policy parameter anywhere. (This line said
//  "five" when there were four declared below and says five now that there are
//  five; the count is the one below, not the one it inherited.)
// -----------------------------------------------------------------------------

// THE shared body. Every content rule in the tree is here exactly once.
VReject validate_pebble(const PebbleInstance& p);

// `count` members starting at m[0]. Runs validate_pebble() on each, then the
// one rule that is about the SET rather than a member: no two share an id
// (spec section 9 - a peer sending one Pebble three times is exactly the lie to
// refuse). `bad_index` receives the offending member, or 0xFF when the reject
// belongs to the team as a whole. It is written on EVERY path, VR_OK included,
// so a caller can never read a stale one.
VReject validate_team(const PebbleInstance* m, uint8_t count, uint8_t& bad_index);

// The agreed level band, inclusive. Separate from validate_team() because it is
// a SESSION rule and not a Pebble rule: the same team is valid in one session
// and out of band in the next. lo > hi refuses everything, by construction.
VReject validate_level_band(const PebbleInstance* m, uint8_t count,
                            uint8_t lo, uint8_t hi, uint8_t& bad_index);

// BATTLE ELIGIBILITY, which is not Pebble validity. A stored Pebble may be
// fainted; a battling one may not, and game/battle.cpp's battle_init() has
// always said so with BR_MEMBER_FAINTED. Same shape as validate_level_band():
// a rule about a SET in a CONTEXT, outside the shared body, with no policy flag
// anywhere.
//
// IT EXISTS BECAUSE OF A MEASURED MISATTRIBUTION. Before it, hp_cur == 0 was
// the one thing three checkers accepted and the engine refused, so a peer that
// sent a fainted member made the honest device close SE_PROTOCOL / SD_INTERNAL -
// "a bug in game/validate.cpp" - for a lie the peer told, with bad_index 0xFF so
// neither the log nor the peer was told which Pebble did it. It is the same
// class as the cross-team duplicate id the session layer already owned, and the
// last instance of it. An honest player whose own team held a fainted Pebble
// reached the identical dead end with nobody lying at all.
VReject validate_battle_ready(const PebbleInstance* m, uint8_t count,
                              uint8_t& bad_index);

// The English name of a code, for the event log and the host tests. NOT a UI
// string: Spanish lives in core/strings_es.h. Total - an out-of-range value
// answers "VR_?" rather than indexing past the table.
const char* validate_reject_name(VReject r);

#endif  // PB_GAME_VALIDATE_H
