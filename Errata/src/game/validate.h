// =============================================================================
//  ERRATA - game/validate.h
//  THE ONE BUG VALIDATOR (spec section 15, plan P4-C5).
//
//  Spec section 15 contains two governing sentences and this module exists for
//  the second one:
//
//      "Never transmit raw unvalidated game objects and trust the peer."
//      "The same validator used for custom Bugs should be used for
//       exchanged Bugs."
//
//  "THE SAME VALIDATOR" MEANS LITERALLY ONE CALL, NOT A SHARED SPIRIT. Every
//  CONSUMER reaches these rules through validate_bug() rather than
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
//  validate_bug() takes a `const BugInstance&`, the same mechanism
//  game/battle.h uses on battle_validate_action(): a repairing validator turns
//  an illegal object into a legal one, which is precisely the injection spec
//  section 15's first sentence forbids. ui/screen_battle.cpp's copy_from_box()
//  (screen_battle.cpp:205) DOES repair - an unknown move becomes the species
//  learnset, hp is clamped to hp_max, PBS_FAINTED is cleared - and that is
//  defensible for a LOCAL Bug the device itself created. It is not
//  defensible for one a peer sent, and the two must never be confused.
//
// -----------------------------------------------------------------------------
//  WHO CALLS IT. THREE PLACES, AND ONLY TWO OF THEM ARE LIVE TODAY.
// -----------------------------------------------------------------------------
//   1. THE WIRE - networking/protocol.cpp's pbw_decode(). LIVE. Every peer
//      Bug is decoded in front of this function and the caller only ever
//      sees an instance that returned VR_OK. Refuse, never repair.
//   2. THE SAVE PATH - persistence/save_manager.cpp's save_load_all(), per
//      occupied slot, after bug_blob_ok() and after migration. LIVE. It
//      QUARANTINES: the slot is flagged with its named VReject and the Box
//      still loads. Refusing the whole Box would brick a device on a content
//      pack change, because VR_UNKNOWN_SPECIES is exactly what an older save
//      legitimately produces.
//   3. THE CREATOR - P8. NOT LIVE, and this header says so rather than
//      claiming a third consumer it does not have. MEASURED at this commit:
//      ui/screen_creator.cpp is 213 lines of QR/portal screen and contains no
//      Bug validation at all (`grep -c "valid" ui/screen_creator.cpp` == 0),
//      networking/webui.cpp registers no upload route, and data/creator_schema.h
//      enforces its budgets at COMPILE TIME over the built-in roster. There is
//      no inline creator rule for this module to absorb; there is a call site
//      for P8 to add.
//
//  A FOURTH ONE WAS CONSIDERED AND MEASURED AWAY: game/box.cpp's
//  box_new_bug(), the tree's only Bug constructor, does NOT call this
//  function. Making the constructor refuse a slot that fails validation reads
//  well and would break four shipped host tests, because box_new_bug() has
//  never required a SEALED genome and tests/test_box.cpp:22,
//  tests/test_box_sim.cpp:50, tests/test_care.cpp:288 and
//  tests/test_screens.cpp:1300 all hand it a zeroed or hand-built one
//  (lineage_id 0 and/or a wrong CRC, i.e. VR_BAD_GENOME). The constructor's
//  contract is therefore unchanged, and what ships instead is the one-line
//  evo_state fix box_new_bug() was missing plus the named test
//  `a_constructed_bug_validates` that drives the constructor with a REAL
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
//  battle eligibility, not Bug validity. It is owned by
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
//       Bug on the save path, where being fainted is ordinary.
//   (d) NO "level >= the evolution rule's level" rule. It is FALSE for wild
//       captures: every one of the 36 roster rows carries a nonzero spawn
//       weight and a wild level is clamped around the active Bug's, so a
//       stage-0 creature above its evolution level is an ordinary catch.
//
// -----------------------------------------------------------------------------
//  WHAT THIS MODULE CANNOT DO, SAID HERE SO NOBODY READS MORE INTO A VR_OK
// -----------------------------------------------------------------------------
//   * A PEER RUNNING MODIFIED FIRMWARE WINS BY PLAYING LEGALLY. Nothing on the
//     wire proves a level was EARNED - BugInstance.xp is XP inside the
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
//   * A BUG'S HISTORY CANNOT BE CHECKED. battles_won, battles_lost,
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
//  game/evolution.h, game/genome.h, game/bug.h and game/xp.h. No Arduino, no
//  heap, no float, no clock, no RNG, no I/O. tools/check.sh greps src/game for
//  the forbidden includes and tests/test_validate.cpp compiles it directly.
// =============================================================================
#ifndef ER_GAME_VALIDATE_H
#define ER_GAME_VALIDATE_H

#include <stdint.h>

#include "../data/balance.h"               // XP_LEVEL_MAX, BATTLE_TEAM_MAX
#include "../data/creator_schema.h"        // the section 35/36 creator budgets
#include "../core/config.h"                // STAT_MILLI_MAX, NAME_MAX_LEN
#include "../persistence/save_schema.h"    // BugInstance, CustomSpeciesRec

// -----------------------------------------------------------------------------
//  TWO RELATIONSHIPS NOTHING ELSE IN THE TREE COMPARES, and this header is the
//  one place that reads both sides of each. Neither is reachable by the balance
//  gate in tools/check.sh: that gate maps balance.json's LEVEL_MAX onto
//  XP_LEVEL_MAX only, so ER_LEVEL_MAX is outside it entirely.
// -----------------------------------------------------------------------------
static_assert((int)ER_LEVEL_MAX == (int)XP_LEVEL_MAX,
              "the persisted level ceiling and the XP curve's ceiling disagree: "
              "a Bug could be stored at a level the curve cannot price");
static_assert((long)ER_CARE_MILLI_MAX == (long)STAT_MILLI_MAX,
              "the persisted care ceiling and the simulator's stat ceiling disagree");

// -----------------------------------------------------------------------------
//  WHY A BUG WAS REFUSED. NEVER A BOOL, for game/battle.h's reason: a test
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
  //     pbw_decode(). A stored Bug can never carry one of these.
  VR_WIRE_MAGIC,             // the 48 B record does not start with BUGW_MAGIC
  VR_WIRE_VERSION,           // wire_ver != BUGW_LAYOUT_VER
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

  // --- CONTENT. Produced ONLY by validate_bug().
  VR_NULL_ID,                // BugInstance.id == 0
  VR_UNKNOWN_SPECIES,        // species_get() answers nullptr (0 included)
  VR_BAD_LEVEL,              // outside 1..ER_LEVEL_MAX
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
  VR_BAD_CARE,               // a care value outside 0..ER_CARE_MILLI_MAX
  VR_BAD_NICKNAME,           // no NUL inside ER_NICKNAME_CAP bytes

  // --- TEAM. Produced ONLY by validate_team().
  VR_TEAM_SIZE,              // count outside 1..BATTLE_TEAM_MAX
  VR_DUPLICATE_ID,           // the same BugInstance.id twice

  // --- SESSION-SCOPED. Produced ONLY by validate_level_band() and
  //     validate_battle_ready(). Both are about a SET in a CONTEXT, never about
  //     whether a Bug is a legal object: the same team is in band in one
  //     session and out of it in the next, and a fainted Bug is a perfectly
  //     legal thing to have STORED.
  VR_LEVEL_OUT_OF_BAND,      // outside the band the two peers agreed
  VR_MEMBER_FAINTED,         // hp_cur == 0 or PBS_FAINTED: legal to store,
                             // illegal to bring to a battle. It is the engine's
                             // BR_MEMBER_FAINTED, named ONE LAYER EARLIER so a
                             // peer that sends one is answered as a peer.

  // --- THE CREATOR DEFINITION (P8-C3). Produced ONLY by
  //     validate_custom_species(), which judges a SPECIES DEFINITION and not a
  //     creature - see the function's own comment for why that is a second
  //     entry point into this module rather than a flag inside the first.
  //
  //     APPENDED AT THE END ON PURPOSE. The values above are read back from
  //     BoxHeader-era quarantine records and named in logs; inserting a code in
  //     the middle would renumber every one after it.
  VR_CS_BAD_HEADER,          // magic, version or slot: not a record this
                             // firmware wrote, or one filed under a slot that
                             // cannot exist
  VR_CS_RESERVED,            // reserved[] carried a value - a field from a
                             // version we do not speak
  VR_CS_BAD_TYPE,            // type >= TYPE_COUNT. A SPECIES may never be
                             // TYPE_NEUTRAL, which shares the value 3 with
                             // TYPE_COUNT (data/species_table.h says why)
  VR_CS_BAD_STAT,            // a base stat outside CREATOR_BASE_STAT_MIN..MAX
  VR_CS_STAT_BUDGET,         // the four stats outside the section 36 BAND -
                             // see data/creator_schema.h for why it is a band
  VR_CS_UNKNOWN_MOVE,        // attack_get() answers nullptr for one of the four
  VR_CS_MOVE_REPEATED,       // the four moves are not distinct
  VR_CS_MOVE_OFF_TYPE,       // a move that is neither the species' own type
                             // nor NEUTRAL
  VR_CS_NO_DAMAGING_MOVE,    // four status moves cannot win a battle
  VR_CS_POWER_CAP,           // a move above CREATOR_POWER_CAP_BY_STAGE[1]
  VR_CS_ATTACK_BUDGET,       // sum(budget_cost) over CREATOR_ATTACK_BUDGET
  VR_CS_BUDGET_MISMATCH,     // budget_used disagrees with the recomputation.
                             // The page may not price its own Bug
  VR_CS_BAD_NAME,            // empty, too long, unterminated, or a character
                             // core/strings_es.h's fonts cannot draw
  VR_CS_BAD_COMPAT,          // compat_group != 0: a custom species has no
                             // family, so it has no child to derive

  VR_REJECT_COUNT
};

// -----------------------------------------------------------------------------
//  THE API. FIVE functions, and no policy parameter anywhere. (This line said
//  "five" when there were four declared below and says five now that there are
//  five; the count is the one below, not the one it inherited.)
// -----------------------------------------------------------------------------

// THE shared body. Every content rule in the tree is here exactly once.
VReject validate_bug(const BugInstance& p);

// `count` members starting at m[0]. Runs validate_bug() on each, then the
// one rule that is about the SET rather than a member: no two share an id
// (spec section 9 - a peer sending one Bug three times is exactly the lie to
// refuse). `bad_index` receives the offending member, or 0xFF when the reject
// belongs to the team as a whole. It is written on EVERY path, VR_OK included,
// so a caller can never read a stale one.
VReject validate_team(const BugInstance* m, uint8_t count, uint8_t& bad_index);

// The agreed level band, inclusive. Separate from validate_team() because it is
// a SESSION rule and not a Bug rule: the same team is valid in one session
// and out of band in the next. lo > hi refuses everything, by construction.
VReject validate_level_band(const BugInstance* m, uint8_t count,
                            uint8_t lo, uint8_t hi, uint8_t& bad_index);

// BATTLE ELIGIBILITY, which is not Bug validity. A stored Bug may be
// fainted; a battling one may not, and game/battle.cpp's battle_init() has
// always said so with BR_MEMBER_FAINTED. Same shape as validate_level_band():
// a rule about a SET in a CONTEXT, outside the shared body, with no policy flag
// anywhere.
//
// IT EXISTS BECAUSE OF A MEASURED MISATTRIBUTION. Before it, hp_cur == 0 was
// the one thing three checkers accepted and the engine refused, so a peer that
// sent a fainted member made the honest device close SE_PROTOCOL / SD_INTERNAL -
// "a bug in game/validate.cpp" - for a lie the peer told, with bad_index 0xFF so
// neither the log nor the peer was told which Bug did it. It is the same
// class as the cross-team duplicate id the session layer already owned, and the
// last instance of it. An honest player whose own team held a fainted Bug
// reached the identical dead end with nobody lying at all.
VReject validate_battle_ready(const BugInstance* m, uint8_t count,
                              uint8_t& bad_index);

// -----------------------------------------------------------------------------
//  THE CREATOR (P8-C3). THIS IS THE THIRD CALL SITE THE HEADER ABOVE HAS BEEN
//  SAYING DOES NOT EXIST YET, AND IT IS NOW LIVE.
//
//  IT IS A SECOND ENTRY POINT AND NOT A FLAG, and the reason is that it judges
//  A DIFFERENT OBJECT. validate_bug() takes a BugInstance - a creature,
//  with a level, an xp, a genome and a health. A creator upload is a
//  CustomSpeciesRec - a species DEFINITION, with base stats and a learnset and
//  no creature anywhere in it. There is no policy parameter that could make one
//  function judge both; there is a pipeline, and it runs in this order:
//
//      cp_parse_species()        the bytes are the shape of a document
//      validate_custom_species() the definition is legal            <- HERE
//      species_get() resolves it through game/species_custom.cpp
//      box_new_bug()          the creature is built from it
//      validate_bug()         the creature is legal
//
//  SO SPEC SECTION 15's "the same validator used for custom Bugs should be
//  used for exchanged Bugs" IS SATISFIED LITERALLY: the CREATURE a creator
//  upload becomes goes through validate_bug(), the same one call the wire
//  and the save path make, with no creator branch inside it. What is here is
//  the rule set that has no other object to be applied to.
//
//  IT IS ALSO THE FLASH GUARD. persistence/save_manager.cpp runs it over every
//  cs* record it reads at boot, so a rotted or hand-written blob leaves its
//  slot empty rather than resolving to a creature with a 400-point stat total.
//  The record arriving over HTTP and the record arriving off flash are judged
//  by the same function, because "it is already stored" is not evidence.
//
//  `budget_used` IS CHECKED, NOT TAKEN. It must equal the recomputation from
//  the moves and the stats: a number the page sends is a number the page can
//  lie about, and this is the one field of the record whose value the device
//  could otherwise inherit from a client.
// -----------------------------------------------------------------------------
VReject validate_custom_species(const CustomSpeciesRec& c);

// -----------------------------------------------------------------------------
//  MAY THIS CHARACTER APPEAR IN A CREATOR NAME?
//
//  `ch` is ONE LATIN-1 BYTE, never a UTF-8 sequence: the conversion happens in
//  networking/creator_parse.cpp, at the point where a codepoint stops being
//  representable, and this is the membership half of that question.
//
//  IT LIVES HERE, IN THE GAME LAYER, BECAUSE TWO CALLERS ASK IT AND ONE OF THEM
//  IS NOT THE PARSER. validate_custom_species() asks it of a record read back
//  from FLASH, which never went through an HTTP request at all. Two copies of
//  an allowed set is exactly the disagreement this project keeps finding, and
//  putting the predicate in networking/ would have inverted the layering as
//  well - game may not include networking, and networking already includes
//  game/validate.h (networking/protocol.cpp does).
//
//  THE SET IS core/strings_es.h's, VERBATIM: printable ASCII plus the accented
//  vowels, u-diaeresis, n-tilde, the two inverted marks, the degree sign, the
//  two ordinals and the middle dot. The reason is not taste - the _tf fonts
//  carry ASCII + Latin-1 and nothing else, so a character outside this set is
//  one the panel draws as a wrong glyph or not at all.
// -----------------------------------------------------------------------------
bool creator_name_char_ok(uint8_t ch);

// -----------------------------------------------------------------------------
//  THE SECTION 36 BUDGET ARITHMETIC, as the integers this device can do.
//
//  tools/content/balance.json's FORMULAS.creator_power_pct writes it in floats:
//      round(100 * (stat_used/TOTAL + attack_used/ATTACK_BUDGET) / 2)
//  and plan rule 1.3 forbids a float in a stat path. The exact integer form,
//  rounding half up, is
//      D   = TOTAL * BUDGET
//      pct = (50 * (BUDGET*S + TOTAL*A) + D/2) / D
//  which agrees with the float sentence on every input in range - tested, not
//  asserted, in tests/test_validate.cpp against Appendix C's own example.
//
//  THE PAGE MUST IMPLEMENT THIS AND NOT THE SENTENCE. Two bars that disagree at
//  one input is a user who is told 72 % and refused at 73 %.
//
//  Total: it saturates at 100 rather than reporting a percentage above it, and
//  it takes its inputs as they are - it is arithmetic, not a rule.
// -----------------------------------------------------------------------------
uint8_t creator_power_pct(uint16_t stat_used, uint16_t attack_used);

// The section 36 costs of a definition, so a caller can price a Bug without
// re-deriving the loop. `stat_used` is the four base stats; `attack_used` is
// the sum of the four budget_cost columns. Both are written on every path,
// including the ones where a move id resolves to nothing (that move costs 0),
// so a caller can never read a stale one.
void creator_cost_of(const CustomSpeciesRec& c,
                     uint16_t& stat_used, uint16_t& attack_used);

// The English name of a code, for the event log and the host tests. NOT a UI
// string: Spanish lives in core/strings_es.h. Total - an out-of-range value
// answers "VR_?" rather than indexing past the table.
const char* validate_reject_name(VReject r);

#endif  // ER_GAME_VALIDATE_H
