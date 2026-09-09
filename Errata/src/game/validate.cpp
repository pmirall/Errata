// =============================================================================
//  ERRATA - game/validate.cpp
//  See validate.h for the contract, the three call sites and the four rules
//  that are deliberately absent.
//
//  THE SHAPE OF EVERY RULE BELOW IS THE SAME: read one field, compare it
//  against a bound that comes from a table or a header constant, return a NAMED
//  code. Nothing is clamped, nothing is written, nothing is assumed about the
//  caller having checked first.
//
//  ZERO floating point, no allocation, no I/O, no Arduino, no clock, no RNG.
// =============================================================================
#include "validate.h"

#include <string.h>

#include "../data/attacks_table.h"    // attack_get()
#include "../data/species_table.h"    // species_get(), SpeciesDef
#include "evolution.h"                // EVO_STATE_*, evolution_level_ready()
#include "genome.h"                   // genome_valid()
#include "bug.h"                   // bug_derive_stats(), BugStats
#include "xp.h"                       // xp_for_level()

// The full set of status bits a STORED Bug may carry. PBS_RESERVED_LIGHT is
// in it on purpose: P3-C2b deleted the light mechanic but a v2 save written
// before that commit may still carry the bit, and refusing it would quarantine
// a Bug for a field nothing reads. The narrower WIRE mask lives in
// networking/protocol.cpp, which is the layer that owns it.
#define VLD_STATUS_MASK  ((uint8_t)(PBS_SICK | PBS_ASLEEP | PBS_CORRUPTED | \
                                    PBS_FAINTED | PBS_RESERVED_LIGHT))

#define VLD_FLAGS_MASK   ((uint8_t)(PBF_CUSTOM | PBF_TRADED | PBF_BRED | \
                                    PBF_GOD_TAINTED | PBF_RARE | \
                                    PBF_HAS_CUSTOM_SPRITE))

// -----------------------------------------------------------------------------
//  THE LEARNSET WALK
//
//  Four real attacks is not four attacks THIS Bug could know. A moveset is
//  legal when it is the VERBATIM, IN-ORDER learnset of some species in the same
//  family at this one's stage or below - which is what lets an evolved creature
//  keep the moves it grew up with and stops a peer handing its Bug a move
//  the content never gave it.
//
//  MEASURED, and the numbers are game/battle.h's: injecting attack 11 Plaga
//  onto a species-1 Paketo took a scripted 1v1 from 0 wins in 200 seeds to 100,
//  and the ON-TYPE attack 3 Rafaga to 103. The second number is why the rule
//  cannot be narrowed to "an attack of the right type".
//
//  This is a SECOND implementation of game/battle.cpp's static
//  moveset_is_learnable(), deliberately (validate.h explains) and pinned to it
//  by a named test rather than by this comment.
// -----------------------------------------------------------------------------
static bool moveset_is_learnable(const SpeciesDef& sp, const uint8_t* moves)
{
  // A SPECIES TEACHES ITS OWN LEARNSET. For a built-in row this changes
  // NOTHING - the walk below finds the row itself, since a species is always in
  // its own family at its own stage - and tests/test_validate.cpp's
  // `the_two_learnset_checkers_agree_on_every_roster_row` is what holds that.
  // For a CREATOR species it is the whole rule: a custom row has family 0 and
  // no row in SPECIES_TABLE, so the walk can never match it, and without this
  // line every creator Bug would be VR_UNLEARNABLE_MOVESET one instruction
  // after being built. THE ALTERNATIVE WAS AN `if (custom)` INSIDE THE
  // VALIDATOR, which is the branch data/species_table.h has spent four phases
  // saying no validator would ever need.
  {
    bool same = true;
    for (uint8_t m = 0; m < (uint8_t)ER_MOVE_COUNT; ++m)
      if (moves[m] != sp.moves[m]) { same = false; break; }
    if (same) return true;
  }
  for (uint8_t i = 0; i < (uint8_t)SPECIES_TABLE_COUNT; ++i) {
    const SpeciesDef& cand = SPECIES_TABLE[i];
    if (cand.family != sp.family || cand.stage > sp.stage) continue;
    bool same = true;
    for (uint8_t m = 0; m < (uint8_t)ER_MOVE_COUNT; ++m)
      if (moves[m] != cand.moves[m]) { same = false; break; }
    if (same) return true;
  }
  return false;
}

// -----------------------------------------------------------------------------
//  validate_bug - the shared body
// -----------------------------------------------------------------------------
VReject validate_bug(const BugInstance& p)
{
  // IDENTITY. species_id 0 is caught by species_get() one line down rather than
  // by a second null rule, so each code keeps exactly one meaning.
  if (p.id == 0u) return VR_NULL_ID;

  const SpeciesDef* sp = species_get(p.species_id);
  if (sp == nullptr) return VR_UNKNOWN_SPECIES;

  // LEVEL. "Impossible levels" (spec section 15) can only ever mean this: see
  // validate.h on why nothing here can prove a level was earned.
  if (p.level < 1u || p.level > (uint8_t)ER_LEVEL_MAX) return VR_BAD_LEVEL;

  // XP. BugInstance.xp is XP INSIDE the current level, so it must be below
  // what this level costs to leave. xp_for_level() answers 0 at the top of the
  // curve, which is "there is nothing left to buy" and not "any amount is fine"
  // - so the two arms are separate and a level-30 Bug must carry xp == 0,
  // exactly as game/xp.cpp's xp_add() pins it.
  {
    const uint16_t need = xp_for_level(p.level);
    if (need == 0u) { if (p.xp != 0u) return VR_BAD_XP; }
    else if (p.xp >= need)            return VR_BAD_XP;
  }

  // THE SEAL. Lineage and trade integrity, and NOTHING ELSE - validate.h says
  // why this is not a stat-forging defence.
  if (!genome_valid(p.genome)) return VR_BAD_GENOME;

  // MOVES. "Illegal moves" (spec section 15), in two halves that are two codes:
  // an id no attack row answers, and four real attacks in an impossible set.
  for (uint8_t m = 0; m < (uint8_t)ER_MOVE_COUNT; ++m)
    if (attack_get(p.moves[m]) == nullptr) return VR_UNKNOWN_MOVE;
  if (!moveset_is_learnable(*sp, p.moves)) return VR_UNLEARNABLE_MOVESET;

  // "IMPOSSIBLE STATS" (spec section 15). Nothing derived is stored, so the
  // only stat a Bug carries is its CURRENT hp - and it is measured against
  // the DERIVED maximum through bug_derive_stats(), the same call
  // game/battle.cpp's setup_member_ok() makes, never against xp_hp_max()
  // directly. They give the same number today (the genome does not enter
  // hp_max); calling the same function is what stops them diverging if it ever
  // gains a genome term.
  {
    BugStats stats;
    bug_derive_stats(*sp, p.level, p.genome, stats);
    if (p.hp_cur > stats.hp_max) return VR_HP_OVER_MAX;
  }

  // "INVALID EVOLUTION STATE" (spec section 15), in the two halves the byte
  // actually has. The stage bits are a pure function of the species row, so
  // they are checked against it and never repaired from it. Bits 6:2 are NOT
  // checked - validate.h rule (a).
  if ((uint8_t)(p.evo_state & (uint8_t)EVO_STATE_STAGE_MASK) != sp->stage)
    return VR_BAD_EVO_STAGE;

  // PENDING means "the LEVEL requirement is met", never "it will evolve"
  // (game/evolution.h). The rule is therefore ONE-DIRECTIONAL: the bit set
  // without the level gate is impossible, but the level gate met with the bit
  // clear is an ordinary wild capture, since game/box.cpp's box_new_bug()
  // mints a level directly and never runs xp_add().
  if ((p.evo_state & (uint8_t)EVO_STATE_PENDING) != 0u &&
      evolution_level_ready(p) == 0u)
    return VR_BAD_EVO_PENDING;

  // THE BIT MASKS. A bit outside the mask is a field from a version we do not
  // speak; accepting it is how a forward-compatible validator becomes a lying
  // one.
  if ((uint8_t)(p.status & (uint8_t)~VLD_STATUS_MASK) != 0u) return VR_BAD_STATUS_BITS;
  if ((uint8_t)(p.flags  & (uint8_t)~VLD_FLAGS_MASK)  != 0u) return VR_BAD_FLAGS_BITS;

  if (p.origin >= (uint8_t)ORIGIN_COUNT) return VR_BAD_ORIGIN;

  // No traits table exists anywhere in this tree, so a nonzero trait_id is
  // UNINTERPRETABLE rather than merely unusual.
  if (p.trait_id != 0u) return VR_BAD_TRAIT;

  // The sprite slot and the flag that claims one must agree in both directions.
  if (p.custom_sprite != (uint8_t)ER_CUSTOM_SPRITE_NONE &&
      p.custom_sprite >= (uint8_t)CUSTOM_SPECIES_SLOTS)
    return VR_BAD_CUSTOM_SPRITE;
  if ((p.flags & (uint8_t)PBF_HAS_CUSTOM_SPRITE) != 0u &&
      p.custom_sprite == (uint8_t)ER_CUSTOM_SPRITE_NONE)
    return VR_BAD_CUSTOM_SPRITE;

  // CARE. Milli-points, and the ceiling is the one both headers name (pinned to
  // each other by the static_assert in validate.h). care_rem is an integrator
  // remainder with no independent bound and is deliberately not checked.
  for (uint8_t i = 0; i < (uint8_t)ER_CARE_COUNT; ++i)
    if (p.care[i] < 0 || p.care[i] > (int32_t)ER_CARE_MILLI_MAX) return VR_BAD_CARE;

  // THE ONLY STRING A BugInstance CARRIES, and the only rule that is about
  // memory safety rather than game rules: ui/screen_battle.cpp:156 hands the
  // nickname out as a bare `const char*` and ui/pet_view.cpp:150 snprintf's
  // "%s" from it. Both cap the OUTPUT at 13 and read the SOURCE to NUL, so an
  // unterminated nickname reads forward into the next field. The ONE nickname
  // writer in src/ is persistence/migration.cpp:221-226, whose loop is bounded
  // by `o + 1 < sizeof p.nickname` and always stores the NUL, so a peer would be
  // the first producer of an UNTERMINATED one. (This said "nothing in src/
  // writes a nickname today" until the P4-C5 follow-up, which was wider than the
  // tree; the load-bearing half survives the correction.)
  {
    bool terminated = false;
    for (uint8_t i = 0; i < (uint8_t)ER_NICKNAME_CAP; ++i)
      if (p.nickname[i] == '\0') { terminated = true; break; }
    if (!terminated) return VR_BAD_NICKNAME;
  }

  return VR_OK;
}

// -----------------------------------------------------------------------------
//  validate_team
// -----------------------------------------------------------------------------
VReject validate_team(const BugInstance* m, uint8_t count, uint8_t& bad_index)
{
  bad_index = 0xFFu;                       // written on EVERY path, VR_OK too
  if (m == nullptr) return VR_TEAM_SIZE;
  if (count < 1u || count > (uint8_t)BATTLE_TEAM_MAX) return VR_TEAM_SIZE;

  for (uint8_t i = 0; i < count; ++i) {
    const VReject r = validate_bug(m[i]);
    if (r != VR_OK) { bad_index = i; return r; }
  }
  // The set rule, after every member is known good: a peer sending one Bug
  // three times is exactly the lie spec section 9 refuses. The index reported
  // is the SECOND occurrence, i.e. the one that made the set illegal.
  for (uint8_t i = 1; i < count; ++i)
    for (uint8_t j = 0; j < i; ++j)
      if (m[i].id == m[j].id) { bad_index = i; return VR_DUPLICATE_ID; }

  return VR_OK;
}

// -----------------------------------------------------------------------------
//  validate_level_band
// -----------------------------------------------------------------------------
VReject validate_level_band(const BugInstance* m, uint8_t count,
                            uint8_t lo, uint8_t hi, uint8_t& bad_index)
{
  bad_index = 0xFFu;
  if (m == nullptr) return VR_TEAM_SIZE;
  if (count < 1u || count > (uint8_t)BATTLE_TEAM_MAX) return VR_TEAM_SIZE;

  for (uint8_t i = 0; i < count; ++i) {
    if (m[i].level < lo || m[i].level > hi) { bad_index = i; return VR_LEVEL_OUT_OF_BAND; }
  }
  return VR_OK;
}

// -----------------------------------------------------------------------------
//  validate_battle_ready - battle eligibility, which is not Bug validity.
//  See validate.h: this is game/battle.cpp's BR_MEMBER_FAINTED, named one layer
//  earlier so a peer that sends a fainted member is answered as a peer instead
//  of the engine refusing a team three checkers had already accepted.
// -----------------------------------------------------------------------------
VReject validate_battle_ready(const BugInstance* m, uint8_t count,
                              uint8_t& bad_index)
{
  bad_index = 0xFFu;
  if (m == nullptr) return VR_TEAM_SIZE;
  if (count < 1u || count > (uint8_t)BATTLE_TEAM_MAX) return VR_TEAM_SIZE;

  for (uint8_t i = 0; i < count; ++i) {
    // BOTH halves, and the same two game/battle.cpp's setup_member_ok() reads.
    // A PEER cannot send PBS_FAINTED - networking/protocol.cpp masks it off and
    // VR_WIRE_STATUS_BITS refuses a forged one - but the LOCAL team comes
    // straight out of the Box, where the bit is legitimate and ordinary, so
    // checking only hp_cur would be a rule narrower than the engine's on
    // exactly the caller that has no adversary in it.
    if (m[i].hp_cur == 0u || (m[i].status & (uint8_t)PBS_FAINTED) != 0u) {
      bad_index = i;
      return VR_MEMBER_FAINTED;
    }
  }
  return VR_OK;
}

// =============================================================================
//  THE CREATOR DEFINITION (P8-C3). See validate.h for why this is a second
//  entry point and not a policy flag, and for the pipeline it sits in.
//
//  EVERYTHING IT READS ARRIVED FROM OUTSIDE THE DEVICE - over HTTP from a page
//  this firmware cannot tell apart from curl, or off a flash blob that survived
//  a CRC and proves nothing else. The page's own budget bar is a COURTESY TO
//  THE USER AND NEVER A CONTROL: it exists so the user is not refused after
//  five minutes of drawing, and this function is what actually decides.
// =============================================================================

bool creator_name_char_ok(uint8_t ch)
{
  if (ch >= 0x20u && ch <= 0x7Eu) return true;       // printable ASCII
  switch (ch) {
    case 0xA1u:                                      // inverted !
    case 0xAAu:                                      // feminine ordinal
    case 0xB0u:                                      // degree sign
    case 0xB7u:                                      // middle dot
    case 0xBAu:                                      // masculine ordinal
    case 0xBFu:                                      // inverted ?
    case 0xC1u: case 0xC9u: case 0xCDu: case 0xD3u: case 0xDAu:   // A E I O U acute
    case 0xD1u:                                      // N tilde
    case 0xDCu:                                      // U diaeresis
    case 0xE1u: case 0xE9u: case 0xEDu: case 0xF3u: case 0xFAu:   // a e i o u acute
    case 0xF1u:                                      // n tilde
    case 0xFCu:                                      // u diaeresis
      return true;
    default:
      return false;
  }
}

void creator_cost_of(const CustomSpeciesRec& c,
                     uint16_t& stat_used, uint16_t& attack_used)
{
  uint16_t stats = 0u;
  for (uint8_t i = 0; i < (uint8_t)CS_BASE_COUNT; ++i)
    stats = (uint16_t)(stats + c.base[i]);

  uint16_t cost = 0u;
  for (uint8_t m = 0; m < (uint8_t)ER_MOVE_COUNT; ++m) {
    const AttackDef* a = attack_get(c.moves[m]);
    // An unresolvable move costs 0 rather than skipping the write: the caller
    // gets a number on every path and validate_custom_species() is what names
    // the move as the reason.
    if (a != nullptr) cost = (uint16_t)(cost + a->budget_cost);
  }

  stat_used   = stats;
  attack_used = cost;
}

uint8_t creator_power_pct(uint16_t stat_used, uint16_t attack_used)
{
  // The integer form of balance.json's FORMULAS.creator_power_pct, rounding
  // half up. Widest intermediate: 50 * (185*22 + 22*185) + 2035 = 409,035,
  // which is why the accumulator is 32-bit and no wider.
  const uint32_t D = (uint32_t)CREATOR_TOTAL_STAT_POINTS *
                     (uint32_t)CREATOR_ATTACK_BUDGET;
  const uint32_t n = 50u * ((uint32_t)CREATOR_ATTACK_BUDGET * (uint32_t)stat_used +
                            (uint32_t)CREATOR_TOTAL_STAT_POINTS * (uint32_t)attack_used);
  const uint32_t pct = (n + D / 2u) / D;
  return (pct > 100u) ? (uint8_t)100u : (uint8_t)pct;
}

VReject validate_custom_species(const CustomSpeciesRec& c)
{
  // THE RECORD'S OWN IDENTITY FIRST. A blob whose magic or schema version is
  // not ours is not a creator species that happens to be wrong - it is not a
  // creator species at all, and reading its fields would be reading a different
  // struct. The CRC is persistence/save_manager.cpp's half and has already run
  // by the time a record read from flash arrives here.
  if (c.magic != (uint16_t)CS_MAGIC) return VR_CS_BAD_HEADER;
  if (c.version != (uint8_t)SAVE_SCHEMA_VERSION) return VR_CS_BAD_HEADER;
  if (c.slot >= (uint8_t)CUSTOM_SPECIES_SLOTS) return VR_CS_BAD_HEADER;

  // The reserve is a field from a version we do not speak. Accepting a value
  // there is how a forward-compatible validator becomes a lying one - the same
  // rule the wire decoder holds with VR_WIRE_RESERVED.
  for (uint8_t i = 0; i < (uint8_t)sizeof c.reserved; ++i)
    if (c.reserved[i] != 0u) return VR_CS_RESERVED;

  // TYPE. A SPECIES may never be TYPE_NEUTRAL, which shares the value 3 with
  // TYPE_COUNT: data/species_table.h states that at length, and it is why this
  // reads `>= TYPE_COUNT` where the ATTACK guard reads `> TYPE_NEUTRAL`.
  if (c.type >= (uint8_t)TYPE_COUNT) return VR_CS_BAD_TYPE;

  // STATS, per field and then as a total.
  for (uint8_t i = 0; i < (uint8_t)CS_BASE_COUNT; ++i) {
    if (c.base[i] < (uint8_t)CREATOR_BASE_STAT_MIN ||
        c.base[i] > (uint8_t)CREATOR_BASE_STAT_MAX) return VR_CS_BAD_STAT;
  }

  uint16_t stat_used = 0u, attack_used = 0u;
  creator_cost_of(c, stat_used, attack_used);

  // THE SECTION 36 BAND. Not equality: data/creator_schema.h carries the
  // argument, and the short version is that Appendix C's own worked example is
  // unreachable at a full stat budget.
  if (stat_used < (uint16_t)CREATOR_STAT_POINTS_MIN ||
      stat_used > (uint16_t)CREATOR_TOTAL_STAT_POINTS) return VR_CS_STAT_BUDGET;

  // MOVES. Four halves, four codes, in the order attacks_table.h's own
  // species_learnsets_are_legal() holds every built-in row to - so a custom
  // species is judged by the rule the roster is judged by, and not by a second
  // one written for the creator.
  for (uint8_t m = 0; m < (uint8_t)ER_MOVE_COUNT; ++m)
    if (attack_get(c.moves[m]) == nullptr) return VR_CS_UNKNOWN_MOVE;

  for (uint8_t m = 0; m < (uint8_t)ER_MOVE_COUNT; ++m)
    for (uint8_t n2 = (uint8_t)(m + 1u); n2 < (uint8_t)ER_MOVE_COUNT; ++n2)
      if (c.moves[m] == c.moves[n2]) return VR_CS_MOVE_REPEATED;

  bool    has_damage = false;
  uint8_t max_power  = 0u;
  for (uint8_t m = 0; m < (uint8_t)ER_MOVE_COUNT; ++m) {
    const AttackDef& a = *attack_get(c.moves[m]);
    if (a.type != c.type && a.type != (uint8_t)TYPE_NEUTRAL) return VR_CS_MOVE_OFF_TYPE;
    if (a.power > 0u) has_damage = true;
    if (a.power > max_power) max_power = a.power;
  }
  if (!has_damage) return VR_CS_NO_DAMAGING_MOVE;

  // A custom Bug is a STAGE-1 creature (game/species_custom.cpp projects it
  // as one), so it is held to the stage-1 power cap and the stage-1 budget -
  // never to the stage-2 numbers, which is spec section 68 r17.
  if (max_power > CREATOR_POWER_CAP_BY_STAGE[1]) return VR_CS_POWER_CAP;
  if (attack_used > (uint16_t)CREATOR_ATTACK_BUDGET) return VR_CS_ATTACK_BUDGET;

  // THE ONE FIELD A CLIENT COULD OTHERWISE SET. budget_used is checked against
  // the recomputation rather than trusted, because a page that prices its own
  // Bug is a page that can price it at zero.
  if (c.budget_used != attack_used) return VR_CS_BUDGET_MISMATCH;

  // THE NAME. Terminated inside the field, non-empty, inside NAME_MAX_LEN, and
  // every character one the panel can draw. The termination rule is the same
  // memory-safety rule validate_bug() holds on the nickname: the name is
  // handed out as a bare const char* and snprintf'd with "%s".
  {
    uint8_t len = 0u;
    bool terminated = false;
    for (uint8_t i = 0; i < (uint8_t)CS_NAME_CAP; ++i) {
      if (c.name[i] == '\0') { terminated = true; break; }
      len++;
    }
    if (!terminated) return VR_CS_BAD_NAME;
    if (len == 0u || len > (uint8_t)NAME_MAX_LEN) return VR_CS_BAD_NAME;
    for (uint8_t i = 0; i < len; ++i)
      if (!creator_name_char_ok((uint8_t)c.name[i])) return VR_CS_BAD_NAME;
    // A leading or trailing space is refused rather than trimmed: trimming is a
    // repair, and this file cannot repair - its argument is const.
    if (c.name[0] == ' ' || c.name[len - 1u] == ' ') return VR_CS_BAD_NAME;
  }

  // BREEDING. A custom species has no family, so game/breeding.cpp has no child
  // species to derive from a pairing; compat_group 0 is the value that module
  // already refuses by name (BRD_COMPAT_GROUP), and pinning it here is what
  // makes "a custom Bug does not breed" a rule with a test instead of a
  // consequence somebody could undo by setting a group.
  if (c.compat_group != 0u) return VR_CS_BAD_COMPAT;

  return VR_OK;
}

// -----------------------------------------------------------------------------
//  validate_reject_name - English, for the event log. TOTAL.
// -----------------------------------------------------------------------------
static const char* const VR_NAMES[] = {
  "VR_OK",
  "VR_WIRE_MAGIC", "VR_WIRE_VERSION", "VR_WIRE_CRC", "VR_WIRE_RESERVED",
  "VR_WIRE_CUSTOM_UNRESOLVED", "VR_WIRE_STATUS_BITS",
  "VR_NULL_ID", "VR_UNKNOWN_SPECIES", "VR_BAD_LEVEL", "VR_BAD_XP",
  "VR_BAD_GENOME", "VR_UNKNOWN_MOVE", "VR_UNLEARNABLE_MOVESET", "VR_HP_OVER_MAX",
  "VR_BAD_EVO_STAGE", "VR_BAD_EVO_PENDING", "VR_BAD_STATUS_BITS",
  "VR_BAD_FLAGS_BITS", "VR_BAD_ORIGIN", "VR_BAD_TRAIT", "VR_BAD_CUSTOM_SPRITE",
  "VR_BAD_CARE", "VR_BAD_NICKNAME",
  "VR_TEAM_SIZE", "VR_DUPLICATE_ID",
  "VR_LEVEL_OUT_OF_BAND", "VR_MEMBER_FAINTED",
  "VR_CS_BAD_HEADER", "VR_CS_RESERVED", "VR_CS_BAD_TYPE", "VR_CS_BAD_STAT",
  "VR_CS_STAT_BUDGET", "VR_CS_UNKNOWN_MOVE", "VR_CS_MOVE_REPEATED",
  "VR_CS_MOVE_OFF_TYPE", "VR_CS_NO_DAMAGING_MOVE", "VR_CS_POWER_CAP",
  "VR_CS_ATTACK_BUDGET", "VR_CS_BUDGET_MISMATCH", "VR_CS_BAD_NAME",
  "VR_CS_BAD_COMPAT"
};
static_assert(sizeof(VR_NAMES) / sizeof(VR_NAMES[0]) == (size_t)VR_REJECT_COUNT,
              "a VReject was added without its name: the event log would print "
              "the wrong reason for every code after it");

const char* validate_reject_name(VReject r)
{
  if ((uint8_t)r >= (uint8_t)VR_REJECT_COUNT) return "VR_?";
  return VR_NAMES[(uint8_t)r];
}
