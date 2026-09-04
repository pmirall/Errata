// =============================================================================
//  PEBBLEBOL - game/validate.cpp
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
#include "pebble.h"                   // pebble_derive_stats(), PebbleStats
#include "xp.h"                       // xp_for_level()

// The full set of status bits a STORED Pebble may carry. PBS_RESERVED_LIGHT is
// in it on purpose: P3-C2b deleted the light mechanic but a v2 save written
// before that commit may still carry the bit, and refusing it would quarantine
// a Pebble for a field nothing reads. The narrower WIRE mask lives in
// networking/protocol.cpp, which is the layer that owns it.
#define VLD_STATUS_MASK  ((uint8_t)(PBS_SICK | PBS_ASLEEP | PBS_CORRUPTED | \
                                    PBS_FAINTED | PBS_RESERVED_LIGHT))

#define VLD_FLAGS_MASK   ((uint8_t)(PBF_CUSTOM | PBF_TRADED | PBF_BRED | \
                                    PBF_GOD_TAINTED | PBF_RARE | \
                                    PBF_HAS_CUSTOM_SPRITE))

// -----------------------------------------------------------------------------
//  THE LEARNSET WALK
//
//  Four real attacks is not four attacks THIS Pebble could know. A moveset is
//  legal when it is the VERBATIM, IN-ORDER learnset of some species in the same
//  family at this one's stage or below - which is what lets an evolved creature
//  keep the moves it grew up with and stops a peer handing its Pebble a move
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
  for (uint8_t i = 0; i < (uint8_t)SPECIES_TABLE_COUNT; ++i) {
    const SpeciesDef& cand = SPECIES_TABLE[i];
    if (cand.family != sp.family || cand.stage > sp.stage) continue;
    bool same = true;
    for (uint8_t m = 0; m < (uint8_t)PB_MOVE_COUNT; ++m)
      if (moves[m] != cand.moves[m]) { same = false; break; }
    if (same) return true;
  }
  return false;
}

// -----------------------------------------------------------------------------
//  validate_pebble - the shared body
// -----------------------------------------------------------------------------
VReject validate_pebble(const PebbleInstance& p)
{
  // IDENTITY. species_id 0 is caught by species_get() one line down rather than
  // by a second null rule, so each code keeps exactly one meaning.
  if (p.id == 0u) return VR_NULL_ID;

  const SpeciesDef* sp = species_get(p.species_id);
  if (sp == nullptr) return VR_UNKNOWN_SPECIES;

  // LEVEL. "Impossible levels" (spec section 15) can only ever mean this: see
  // validate.h on why nothing here can prove a level was earned.
  if (p.level < 1u || p.level > (uint8_t)PB_LEVEL_MAX) return VR_BAD_LEVEL;

  // XP. PebbleInstance.xp is XP INSIDE the current level, so it must be below
  // what this level costs to leave. xp_for_level() answers 0 at the top of the
  // curve, which is "there is nothing left to buy" and not "any amount is fine"
  // - so the two arms are separate and a level-30 Pebble must carry xp == 0,
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
  for (uint8_t m = 0; m < (uint8_t)PB_MOVE_COUNT; ++m)
    if (attack_get(p.moves[m]) == nullptr) return VR_UNKNOWN_MOVE;
  if (!moveset_is_learnable(*sp, p.moves)) return VR_UNLEARNABLE_MOVESET;

  // "IMPOSSIBLE STATS" (spec section 15). Nothing derived is stored, so the
  // only stat a Pebble carries is its CURRENT hp - and it is measured against
  // the DERIVED maximum through pebble_derive_stats(), the same call
  // game/battle.cpp's setup_member_ok() makes, never against xp_hp_max()
  // directly. They give the same number today (the genome does not enter
  // hp_max); calling the same function is what stops them diverging if it ever
  // gains a genome term.
  {
    PebbleStats stats;
    pebble_derive_stats(*sp, p.level, p.genome, stats);
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
  // clear is an ordinary wild capture, since game/box.cpp's box_new_pebble()
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
  if (p.custom_sprite != (uint8_t)PB_CUSTOM_SPRITE_NONE &&
      p.custom_sprite >= (uint8_t)CUSTOM_SPECIES_SLOTS)
    return VR_BAD_CUSTOM_SPRITE;
  if ((p.flags & (uint8_t)PBF_HAS_CUSTOM_SPRITE) != 0u &&
      p.custom_sprite == (uint8_t)PB_CUSTOM_SPRITE_NONE)
    return VR_BAD_CUSTOM_SPRITE;

  // CARE. Milli-points, and the ceiling is the one both headers name (pinned to
  // each other by the static_assert in validate.h). care_rem is an integrator
  // remainder with no independent bound and is deliberately not checked.
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i)
    if (p.care[i] < 0 || p.care[i] > (int32_t)PB_CARE_MILLI_MAX) return VR_BAD_CARE;

  // THE ONLY STRING A PebbleInstance CARRIES, and the only rule that is about
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
    for (uint8_t i = 0; i < (uint8_t)PB_NICKNAME_CAP; ++i)
      if (p.nickname[i] == '\0') { terminated = true; break; }
    if (!terminated) return VR_BAD_NICKNAME;
  }

  return VR_OK;
}

// -----------------------------------------------------------------------------
//  validate_team
// -----------------------------------------------------------------------------
VReject validate_team(const PebbleInstance* m, uint8_t count, uint8_t& bad_index)
{
  bad_index = 0xFFu;                       // written on EVERY path, VR_OK too
  if (m == nullptr) return VR_TEAM_SIZE;
  if (count < 1u || count > (uint8_t)BATTLE_TEAM_MAX) return VR_TEAM_SIZE;

  for (uint8_t i = 0; i < count; ++i) {
    const VReject r = validate_pebble(m[i]);
    if (r != VR_OK) { bad_index = i; return r; }
  }
  // The set rule, after every member is known good: a peer sending one Pebble
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
VReject validate_level_band(const PebbleInstance* m, uint8_t count,
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
//  validate_battle_ready - battle eligibility, which is not Pebble validity.
//  See validate.h: this is game/battle.cpp's BR_MEMBER_FAINTED, named one layer
//  earlier so a peer that sends a fainted member is answered as a peer instead
//  of the engine refusing a team three checkers had already accepted.
// -----------------------------------------------------------------------------
VReject validate_battle_ready(const PebbleInstance* m, uint8_t count,
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
  "VR_LEVEL_OUT_OF_BAND", "VR_MEMBER_FAINTED"
};
static_assert(sizeof(VR_NAMES) / sizeof(VR_NAMES[0]) == (size_t)VR_REJECT_COUNT,
              "a VReject was added without its name: the event log would print "
              "the wrong reason for every code after it");

const char* validate_reject_name(VReject r)
{
  if ((uint8_t)r >= (uint8_t)VR_REJECT_COUNT) return "VR_?";
  return VR_NAMES[(uint8_t)r];
}
