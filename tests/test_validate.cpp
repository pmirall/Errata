// =============================================================================
//  PEBBLEBOL host test - test_validate.cpp
//  THE SHARED VALIDATOR (game/validate.h, spec section 15, plan P4-C5).
//
//  THREE RULES THIS FILE OBEYS, copied from tests/test_battle.cpp because this
//  project has now shipped seventeen assertions that could not fail:
//
//   (a) EVERY rejection case runs on a FULLY LEGAL Pebble built by mk_valid(),
//       in which the case's own field is the ONLY illegal thing. A zeroed
//       PebbleInstance is refused by VR_NULL_ID before it can reach any other
//       guard, so a block written against `!= VR_OK` there would pass with
//       sixteen real rules deleted.
//   (b) EVERY case asserts the EXACT VReject, never `!= VR_OK`.
//   (c) EVERY case carries a POSITIVE CONTROL in the same function: the same
//       Pebble with that one field corrected must return VR_OK. The control is
//       what proves the case reached the guard it names instead of tripping an
//       earlier one.
//
//  The six VR_WIRE_* codes are NOT exercised here: they are produced by
//  networking/protocol.cpp's pbw_decode() and by nothing else, which is a claim
//  this file makes negatively (validate_pebble() never returns one, over every
//  fixture in the file) and tests/test_protocol.cpp makes positively.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "data/attacks_table.h"
#include "data/creator_schema.h"
#include "data/species_table.h"
#include "game/battle.h"
#include "game/box.h"
#include "game/evolution.h"
#include "game/genome.h"
#include "game/pebble.h"
#include "game/species_custom.h"
#include "game/validate.h"
#include "game/xp.h"

// The two attacks game/battle.h's own measurement names. Injecting 11 Plaga
// into a species-1 Paketo took a scripted 1v1 from 0 wins in 200 seeds to 100;
// the ON-TYPE 3 Rafaga took it to 103, which is why the learnset rule cannot be
// narrowed to "an attack of the right type".
#define MV_PLAGA    11u
#define MV_RAFAGA    3u

// =============================================================================
//  FIXTURES
// =============================================================================
static Genome sealed_genome(uint32_t lineage)
{
  Genome g;
  memset(&g, 0, sizeof g);
  g.lineage_id = lineage ? lineage : 1u;
  g.g0 = 0x1234u; g.g1 = 0x5678u; g.g2 = 0x09ABu;
  g.generation = 1u;
  genome_seal(g);                       // stamps magic_ver and the CRC
  return g;
}

// A Pebble on which validate_pebble() returns VR_OK and every case makes
// exactly one thing wrong. Species and level are real roster values.
static void mk_valid(PebbleInstance& p, uint8_t species, uint8_t level, uint32_t id)
{
  memset(&p, 0, sizeof p);
  p.magic         = (uint16_t)PEBBLE_MAGIC;
  p.layout_ver    = (uint8_t)PEBBLE_LAYOUT_VER;
  p.species_id    = species;
  p.id            = id;
  p.level         = level;
  p.origin        = (uint8_t)ORIGIN_WILD;
  p.custom_sprite = (uint8_t)PB_CUSTOM_SPRITE_NONE;
  p.genome        = sealed_genome(0xA5A5A500u + id);
  for (uint8_t i = 0; i < (uint8_t)PB_CARE_COUNT; ++i)
    p.care[i] = (int32_t)PB_CARE_MILLI_MAX;
  const SpeciesDef* sp = species_get(species);
  if (sp == nullptr) return;
  memcpy(p.moves, sp->moves, sizeof p.moves);
  p.hp_cur    = xp_hp_max(sp->base_hp, level);
  p.evo_state = (uint8_t)(sp->stage & (uint8_t)EVO_STATE_STAGE_MASK);
}

// -----------------------------------------------------------------------------
//  THE CREATOR FIXTURES (P8-C3). Attack ids by name, so a case reads as the set
//  it is rather than as four numbers, and so a content edit that renumbers a row
//  fails HERE with a named move instead of somewhere downstream.
// -----------------------------------------------------------------------------
#define MV_PING          1u   // SIGNAL,  power 35, cost 35
#define MV_PULSO         2u   // SIGNAL,  power 55, cost 52
#define MV_AMPLIFICAR    6u   // SIGNAL,  power  0, cost 21
#define MV_ANTENA        7u   // SIGNAL,  power  0, cost 21
#define MV_ECO_DOBLE     8u   // SIGNAL,  power 90, cost 51
#define MV_BYTAZO        9u   // CORRUPT, power 35, cost 35  <- off type for SIGNAL
#define MV_CHOQUE       27u   // NEUTRAL, power 50, cost 50
#define MV_APUESTA      28u   // NEUTRAL, power 100, cost 47 <- over the stage-1 cap
#define MV_DEFRAG       29u   // NEUTRAL, power  0, cost 16
#define MV_OVERCLOCK    32u   // NEUTRAL, power  0, cost 21
#define MV_DEPURAR      34u   // NEUTRAL, power  0, cost 14

// Recompute budget_used after a case has changed the moves, so the ONE thing a
// case makes wrong is the thing it names - otherwise every move edit would also
// trip VR_CS_BUDGET_MISMATCH and the codes would be indistinguishable.
static void cs_price(CustomSpeciesRec& c)
{
  uint16_t stat_used = 0, attack_used = 0;
  creator_cost_of(c, stat_used, attack_used);
  c.budget_used = attack_used;
}

static void cs_moves(CustomSpeciesRec& c, uint8_t a, uint8_t b, uint8_t d, uint8_t e)
{
  c.moves[0] = a; c.moves[1] = b; c.moves[2] = d; c.moves[3] = e;
  cs_price(c);
}

// A definition validate_custom_species() accepts, and it is DELIBERATELY
// APPENDIX C's OWN EXAMPLE: stats 6/5/5/5 = 21 and a legal four-move set costing
// 91, which the spec's POWER bar prices at 72 %. That number is not decoration -
// it is the one worked example the product spec ships, and it is UNREACHABLE at
// a full 22-point stat budget (the cheapest legal set costs 84..86, which prices
// at 73 %), which is how P8-C3 settled that the section 36 stat rule is a BAND.
static void mk_cs(CustomSpeciesRec& c)
{
  memset(&c, 0, sizeof c);
  c.magic   = (uint16_t)CS_MAGIC;
  c.version = (uint8_t)SAVE_SCHEMA_VERSION;
  c.slot    = 0u;
  c.type    = (uint8_t)TYPE_SIGNAL;
  c.base[0] = 6u; c.base[1] = 5u; c.base[2] = 5u; c.base[3] = 5u;   // 21
  memcpy(c.name, "Bicho", 6);
  c.compat_group = 0u;                       // a custom species does not breed
  cs_moves(c, MV_PING, MV_AMPLIFICAR, MV_OVERCLOCK, MV_DEPURAR);    // cost 91
}

static uint16_t hp_max_of(const PebbleInstance& p)
{
  PebbleStats s;
  CHECK(pebble_stats_of(p, s));
  return s.hp_max;
}

// =============================================================================
//  1. THE FULLY LEGAL FIXTURE ITSELF. Without this the whole file is vacuous.
// =============================================================================
TEST(the_fixture_every_case_starts_from_is_actually_valid) {
  uint16_t seen = 0;
  for (uint8_t s = 1; s <= (uint8_t)SPECIES_TABLE_COUNT; ++s) {
    for (uint8_t lv = 1; lv <= (uint8_t)PB_LEVEL_MAX; ++lv) {
      PebbleInstance p;
      mk_valid(p, s, lv, 0x100u + s * 64u + lv);
      const VReject r = validate_pebble(p);
      if (r != VR_OK) fprintf(stderr, "    species %u level %u -> %s\n",
                              (unsigned)s, (unsigned)lv, validate_reject_name(r));
      CHECK_EQ((int)r, (int)VR_OK);
      seen++;
    }
  }
  CHECK_EQ((int)seen, (int)SPECIES_TABLE_COUNT * (int)PB_LEVEL_MAX);
}

// =============================================================================
//  2. ONE CASE PER CONTENT REJECT, each with its positive control
// =============================================================================
TEST(an_id_of_zero_is_refused_by_name) {
  PebbleInstance p; mk_valid(p, 1, 10, 7);
  p.id = 0u;
  CHECK_EQ((int)validate_pebble(p), (int)VR_NULL_ID);
  p.id = 7u;
  CHECK_EQ((int)validate_pebble(p), (int)VR_OK);
}

TEST(a_species_with_no_row_is_refused_by_name) {
  PebbleInstance p; mk_valid(p, 1, 10, 7);
  // 0 is the EMPTY marker and it arrives here through the same guard, on
  // purpose: species_get() answers nullptr for both, so one code covers one
  // question ("does this id resolve") rather than two codes covering one.
  p.species_id = 0u;
  CHECK_EQ((int)validate_pebble(p), (int)VR_UNKNOWN_SPECIES);
  p.species_id = (uint8_t)(SPECIES_TABLE_COUNT + 1u);
  CHECK_EQ((int)validate_pebble(p), (int)VR_UNKNOWN_SPECIES);
  p.species_id = (uint8_t)SPECIES_ID_BUILTIN_MAX;
  CHECK_EQ((int)validate_pebble(p), (int)VR_UNKNOWN_SPECIES);
  p.species_id = 1u;
  CHECK_EQ((int)validate_pebble(p), (int)VR_OK);
}

TEST(an_impossible_level_is_refused_by_name) {
  PebbleInstance p; mk_valid(p, 1, 10, 7);
  p.level = (uint8_t)(PB_LEVEL_MAX + 1u);          // the plan's "level 31"
  CHECK_EQ((int)validate_pebble(p), (int)VR_BAD_LEVEL);
  p.level = 0u;
  CHECK_EQ((int)validate_pebble(p), (int)VR_BAD_LEVEL);
  p.level = 255u;
  CHECK_EQ((int)validate_pebble(p), (int)VR_BAD_LEVEL);
  p.level = 10u;
  CHECK_EQ((int)validate_pebble(p), (int)VR_OK);
}

TEST(xp_at_or_past_what_the_level_costs_is_refused_by_name) {
  PebbleInstance p; mk_valid(p, 1, 10, 7);
  const uint16_t need = xp_for_level(10);
  CHECK(need > 1u);                                // the case would be vacuous at 0
  p.xp = need;
  CHECK_EQ((int)validate_pebble(p), (int)VR_BAD_XP);
  p.xp = (uint16_t)(need - 1u);
  CHECK_EQ((int)validate_pebble(p), (int)VR_OK);

  // The top of the curve is its own arm: xp_for_level() answers 0 there, which
  // means "there is nothing left to buy" and not "any amount is fine".
  PebbleInstance q; mk_valid(q, 1, (uint8_t)PB_LEVEL_MAX, 8);
  CHECK_EQ((int)xp_for_level((uint8_t)PB_LEVEL_MAX), 0);
  q.xp = 1u;
  CHECK_EQ((int)validate_pebble(q), (int)VR_BAD_XP);
  q.xp = 0u;
  CHECK_EQ((int)validate_pebble(q), (int)VR_OK);
}

TEST(a_genome_whose_seal_fails_is_refused_by_name) {
  PebbleInstance p; mk_valid(p, 1, 10, 7);
  const Genome good = p.genome;

  p.genome.crc16 = (uint16_t)(good.crc16 ^ 0x0001u);        // the plan's "bad genome CRC"
  CHECK_EQ((int)validate_pebble(p), (int)VR_BAD_GENOME);
  p.genome = good;
  CHECK_EQ((int)validate_pebble(p), (int)VR_OK);

  p.genome.lineage_id = 0u;                                  // a dynasty of nobody
  CHECK_EQ((int)validate_pebble(p), (int)VR_BAD_GENOME);
  p.genome = good;
  CHECK_EQ((int)validate_pebble(p), (int)VR_OK);

  p.genome.magic_ver = 0u;                                   // wrong signature
  CHECK_EQ((int)validate_pebble(p), (int)VR_BAD_GENOME);
  p.genome = good;
  CHECK_EQ((int)validate_pebble(p), (int)VR_OK);
}

TEST(a_move_id_no_attack_row_answers_is_refused_by_name) {
  PebbleInstance p; mk_valid(p, 1, 10, 7);
  const uint8_t good = p.moves[2];

  p.moves[2] = 0u;                                    // the EMPTY move slot
  CHECK_EQ((int)validate_pebble(p), (int)VR_UNKNOWN_MOVE);
  p.moves[2] = (uint8_t)(ATTACK_COUNT + 1u);          // past the table
  CHECK_EQ((int)validate_pebble(p), (int)VR_UNKNOWN_MOVE);
  p.moves[2] = 255u;
  CHECK_EQ((int)validate_pebble(p), (int)VR_UNKNOWN_MOVE);
  p.moves[2] = good;
  CHECK_EQ((int)validate_pebble(p), (int)VR_OK);
}

// THE DECISIVE PAIR. Both injected moves are REAL attacks, so the previous case
// cannot catch either; the second is the same TYPE as the species, so a rule
// narrowed to "an attack of the right type" would still let it through - which
// is the mutation that proves this rule is not cosmetic.
TEST(a_real_attack_the_species_cannot_learn_is_refused_by_name) {
  PebbleInstance p; mk_valid(p, 1, 10, 7);
  const uint8_t good0 = p.moves[0];
  CHECK(attack_get(MV_PLAGA)  != nullptr);
  CHECK(attack_get(MV_RAFAGA) != nullptr);

  p.moves[0] = MV_PLAGA;                              // off-type, 0/200 -> 100/200
  CHECK_EQ((int)validate_pebble(p), (int)VR_UNLEARNABLE_MOVESET);
  p.moves[0] = MV_RAFAGA;                             // ON-TYPE, 0/200 -> 103/200
  CHECK_EQ((int)validate_pebble(p), (int)VR_UNLEARNABLE_MOVESET);
  CHECK_EQ((int)species_get(1)->type, (int)attack_get(MV_RAFAGA)->type);
  p.moves[0] = good0;
  CHECK_EQ((int)validate_pebble(p), (int)VR_OK);
}

// The other half of the same rule: the walk goes DOWN the family, never up.
TEST(the_learnset_walk_reaches_earlier_stages_and_not_later_ones) {
  const SpeciesDef* base  = species_get(1);           // family 1 stage 0
  const SpeciesDef* final_ = species_get(3);          // family 1 stage 2
  CHECK_EQ((int)base->family, (int)final_->family);
  CHECK_EQ((int)base->stage, 0);
  CHECK_EQ((int)final_->stage, 2);

  PebbleInstance up; mk_valid(up, 3, 20, 11);         // a final stage...
  memcpy(up.moves, base->moves, sizeof up.moves);     // ...keeping its baby moves
  CHECK_EQ((int)validate_pebble(up), (int)VR_OK);

  PebbleInstance down; mk_valid(down, 1, 20, 12);     // a base stage...
  memcpy(down.moves, final_->moves, sizeof down.moves); // ...with the final's moves
  CHECK_EQ((int)validate_pebble(down), (int)VR_UNLEARNABLE_MOVESET);
  memcpy(down.moves, base->moves, sizeof down.moves);
  CHECK_EQ((int)validate_pebble(down), (int)VR_OK);
}

TEST(hp_above_the_derived_maximum_is_refused_by_name) {
  PebbleInstance p; mk_valid(p, 1, 10, 7);
  const uint16_t hm = hp_max_of(p);
  CHECK(hm > 0u);
  p.hp_cur = (uint16_t)(hm + 1u);                     // the plan's "hp > max"
  CHECK_EQ((int)validate_pebble(p), (int)VR_HP_OVER_MAX);
  p.hp_cur = 0xFFFFu;
  CHECK_EQ((int)validate_pebble(p), (int)VR_HP_OVER_MAX);
  p.hp_cur = hm;
  CHECK_EQ((int)validate_pebble(p), (int)VR_OK);
  // hp_cur == 0 is a legitimate STORED state and an illegal BATTLE one, so it
  // is game/battle.h's BR_MEMBER_FAINTED and deliberately not a VReject.
  p.hp_cur = 0u;
  CHECK_EQ((int)validate_pebble(p), (int)VR_OK);
}

TEST(an_evolution_stage_that_disagrees_with_the_species_row_is_refused_by_name) {
  // Species 5 is family 2 stage 1 and species 3 is family 1 stage 2, so both
  // have a stage the zeroed byte would get wrong.
  CHECK_EQ((int)species_get(5)->stage, 1);
  CHECK_EQ((int)species_get(3)->stage, 2);

  PebbleInstance p; mk_valid(p, 5, 10, 7);
  p.evo_state = 0u;                                   // "the plan's evo_state that
  CHECK_EQ((int)validate_pebble(p), (int)VR_BAD_EVO_STAGE);  // disagrees with its row"
  p.evo_state = 2u;
  CHECK_EQ((int)validate_pebble(p), (int)VR_BAD_EVO_STAGE);
  p.evo_state = 1u;
  CHECK_EQ((int)validate_pebble(p), (int)VR_OK);

  PebbleInstance q; mk_valid(q, 3, 10, 8);
  q.evo_state = 1u;
  CHECK_EQ((int)validate_pebble(q), (int)VR_BAD_EVO_STAGE);
  q.evo_state = 2u;
  CHECK_EQ((int)validate_pebble(q), (int)VR_OK);

  // Bits 6:2 are NOT checked, and this case pins that as a DECISION:
  // tests/test_evolution.cpp asserts junk there survives an evolution and
  // game/evolution.cpp:102 preserves it on purpose.
  q.evo_state = (uint8_t)(2u | 0x0Cu);
  CHECK_EQ((int)validate_pebble(q), (int)VR_OK);
}

TEST(a_pending_evolution_without_the_level_gate_is_refused_by_name) {
  const EvolutionRule* rule = evolution_rule_for(1);
  CHECK(rule != nullptr);
  CHECK(rule->level > 1u);

  PebbleInstance p; mk_valid(p, 1, 1, 7);
  CHECK_EQ((int)evolution_level_ready(p), 0);
  p.evo_state |= (uint8_t)EVO_STATE_PENDING;
  CHECK_EQ((int)validate_pebble(p), (int)VR_BAD_EVO_PENDING);
  p.evo_state = (uint8_t)(p.evo_state & (uint8_t)~EVO_STATE_PENDING);
  CHECK_EQ((int)validate_pebble(p), (int)VR_OK);

  // The rule is ONE-DIRECTIONAL and that is not an oversight: box_new_pebble()
  // mints a level directly and never runs xp_add(), so a wild capture above its
  // evolution level is legal with the bit CLEAR.
  PebbleInstance q; mk_valid(q, 1, rule->level, 8);
  CHECK(evolution_level_ready(q) != 0);
  CHECK_EQ((int)validate_pebble(q), (int)VR_OK);
  q.evo_state |= (uint8_t)EVO_STATE_PENDING;
  CHECK_EQ((int)validate_pebble(q), (int)VR_OK);
}

TEST(a_status_or_flag_bit_outside_its_mask_is_refused_by_name) {
  PebbleInstance p; mk_valid(p, 1, 10, 7);

  p.status = 0x20u;                                   // above PBS_RESERVED_LIGHT
  CHECK_EQ((int)validate_pebble(p), (int)VR_BAD_STATUS_BITS);
  p.status = 0x80u;
  CHECK_EQ((int)validate_pebble(p), (int)VR_BAD_STATUS_BITS);
  // Every bit the STORED mask names is accepted, PBS_RESERVED_LIGHT included:
  // a v2 save written before P3-C2b may still carry it and quarantining a
  // Pebble for a field nothing reads would be a repair by another name.
  p.status = (uint8_t)(PBS_SICK | PBS_ASLEEP | PBS_CORRUPTED | PBS_FAINTED |
                       PBS_RESERVED_LIGHT);
  CHECK_EQ((int)validate_pebble(p), (int)VR_OK);
  p.status = 0u;

  p.flags = 0x40u;
  CHECK_EQ((int)validate_pebble(p), (int)VR_BAD_FLAGS_BITS);
  p.flags = 0x80u;
  CHECK_EQ((int)validate_pebble(p), (int)VR_BAD_FLAGS_BITS);
  p.flags = (uint8_t)(PBF_CUSTOM | PBF_TRADED | PBF_BRED | PBF_GOD_TAINTED |
                      PBF_RARE);
  CHECK_EQ((int)validate_pebble(p), (int)VR_OK);
}

TEST(an_origin_a_trait_or_a_sprite_slot_that_cannot_exist_is_refused_by_name) {
  PebbleInstance p; mk_valid(p, 1, 10, 7);

  p.origin = (uint8_t)ORIGIN_COUNT;
  CHECK_EQ((int)validate_pebble(p), (int)VR_BAD_ORIGIN);
  p.origin = (uint8_t)ORIGIN_TRADED;
  CHECK_EQ((int)validate_pebble(p), (int)VR_OK);

  // No traits table exists anywhere in this tree, so a nonzero trait_id is
  // uninterpretable rather than merely unusual.
  p.trait_id = 1u;
  CHECK_EQ((int)validate_pebble(p), (int)VR_BAD_TRAIT);
  p.trait_id = 0u;
  CHECK_EQ((int)validate_pebble(p), (int)VR_OK);

  p.custom_sprite = (uint8_t)CUSTOM_SPECIES_SLOTS;
  CHECK_EQ((int)validate_pebble(p), (int)VR_BAD_CUSTOM_SPRITE);
  p.custom_sprite = (uint8_t)PB_CUSTOM_SPRITE_NONE;
  CHECK_EQ((int)validate_pebble(p), (int)VR_OK);

  // The other direction: a flag claiming a sprite that is not there.
  p.flags |= (uint8_t)PBF_HAS_CUSTOM_SPRITE;
  CHECK_EQ((int)validate_pebble(p), (int)VR_BAD_CUSTOM_SPRITE);
  p.custom_sprite = 0u;
  CHECK_EQ((int)validate_pebble(p), (int)VR_OK);
}

TEST(a_care_value_outside_its_band_is_refused_by_name) {
  PebbleInstance p; mk_valid(p, 1, 10, 7);
  p.care[2] = (int32_t)PB_CARE_MILLI_MAX + 1;
  CHECK_EQ((int)validate_pebble(p), (int)VR_BAD_CARE);
  p.care[2] = -1;
  CHECK_EQ((int)validate_pebble(p), (int)VR_BAD_CARE);
  p.care[2] = 0;
  CHECK_EQ((int)validate_pebble(p), (int)VR_OK);
  p.care[2] = (int32_t)PB_CARE_MILLI_MAX;
  CHECK_EQ((int)validate_pebble(p), (int)VR_OK);
}

// THE ONE RULE THAT IS ABOUT MEMORY SAFETY AND NOT ABOUT GAME RULES.
TEST(a_nickname_with_no_terminator_is_refused_by_name) {
  PebbleInstance p; mk_valid(p, 1, 10, 7);
  for (uint8_t i = 0; i < (uint8_t)PB_NICKNAME_CAP; ++i) p.nickname[i] = 'A';
  CHECK_EQ((int)validate_pebble(p), (int)VR_BAD_NICKNAME);
  p.nickname[PB_NICKNAME_CAP - 1] = '\0';             // the last byte is enough
  CHECK_EQ((int)validate_pebble(p), (int)VR_OK);
  memset(p.nickname, 0, sizeof p.nickname);
  CHECK_EQ((int)validate_pebble(p), (int)VR_OK);
}

// =============================================================================
//  3. THE TEAM AND THE BAND
// =============================================================================
TEST(a_team_size_outside_one_to_three_is_refused_by_name) {
  PebbleInstance m[BATTLE_TEAM_MAX];
  for (uint8_t i = 0; i < (uint8_t)BATTLE_TEAM_MAX; ++i)
    mk_valid(m[i], (uint8_t)(1u + i), 10, 0x40u + i);

  uint8_t bad = 0;
  CHECK_EQ((int)validate_team(m, 0u, bad), (int)VR_TEAM_SIZE);
  CHECK_EQ((int)bad, 0xFF);
  CHECK_EQ((int)validate_team(m, (uint8_t)(BATTLE_TEAM_MAX + 1u), bad), (int)VR_TEAM_SIZE);
  CHECK_EQ((int)validate_team(nullptr, 3u, bad), (int)VR_TEAM_SIZE);
  CHECK_EQ((int)validate_team(m, (uint8_t)BATTLE_TEAM_MAX, bad), (int)VR_OK);
  CHECK_EQ((int)bad, 0xFF);                    // written on the VR_OK path too
}

TEST(a_team_reports_the_offending_member_by_index) {
  PebbleInstance m[BATTLE_TEAM_MAX];
  for (uint8_t i = 0; i < (uint8_t)BATTLE_TEAM_MAX; ++i)
    mk_valid(m[i], (uint8_t)(1u + i), 10, 0x40u + i);

  uint8_t bad = 0xEE;
  m[2].level = (uint8_t)(PB_LEVEL_MAX + 1u);
  CHECK_EQ((int)validate_team(m, 3u, bad), (int)VR_BAD_LEVEL);
  CHECK_EQ((int)bad, 2);
  m[2].level = 10u;
  CHECK_EQ((int)validate_team(m, 3u, bad), (int)VR_OK);

  // A peer sending one Pebble three times is exactly the lie spec section 9
  // refuses; the index reported is the one that made the SET illegal.
  m[1].id = m[0].id;
  CHECK_EQ((int)validate_team(m, 3u, bad), (int)VR_DUPLICATE_ID);
  CHECK_EQ((int)bad, 1);
  m[1].id = 0x41u;
  CHECK_EQ((int)validate_team(m, 3u, bad), (int)VR_OK);
}

TEST(the_agreed_level_band_is_a_session_rule_and_not_a_pebble_rule) {
  PebbleInstance m[BATTLE_TEAM_MAX];
  for (uint8_t i = 0; i < (uint8_t)BATTLE_TEAM_MAX; ++i)
    mk_valid(m[i], (uint8_t)(1u + i), (uint8_t)(8u + i * 4u), 0x50u + i);   // 8, 12, 16

  uint8_t bad = 0xEE;
  CHECK_EQ((int)validate_level_band(m, 3u, 1u, 30u, bad), (int)VR_OK);
  CHECK_EQ((int)bad, 0xFF);
  CHECK_EQ((int)validate_level_band(m, 3u, 8u, 16u, bad), (int)VR_OK);
  CHECK_EQ((int)validate_level_band(m, 3u, 8u, 15u, bad), (int)VR_LEVEL_OUT_OF_BAND);
  CHECK_EQ((int)bad, 2);
  CHECK_EQ((int)validate_level_band(m, 3u, 9u, 16u, bad), (int)VR_LEVEL_OUT_OF_BAND);
  CHECK_EQ((int)bad, 0);
  // The SAME team is valid in one session and out of band in the next, which is
  // why this is not part of validate_pebble().
  for (uint8_t i = 0; i < 3u; ++i) CHECK_EQ((int)validate_pebble(m[i]), (int)VR_OK);
}

// =============================================================================
//  4. EVERY CODE IS REACHABLE, AND EACH HAS EXACTLY ONE OWNER
// =============================================================================
TEST(every_content_and_team_reject_is_reachable_and_no_wire_code_is) {
  bool seen[VR_REJECT_COUNT];
  memset(seen, 0, sizeof seen);

  PebbleInstance p;
  uint8_t bad = 0;
  #define OBSERVE(expr) do { const VReject r_ = (expr); seen[(int)r_] = true; } while (0)

  mk_valid(p, 1, 10, 7); OBSERVE(validate_pebble(p));                  // VR_OK
  mk_valid(p, 1, 10, 7); p.id = 0;              OBSERVE(validate_pebble(p));
  mk_valid(p, 1, 10, 7); p.species_id = 0;      OBSERVE(validate_pebble(p));
  mk_valid(p, 1, 10, 7); p.level = 31;          OBSERVE(validate_pebble(p));
  mk_valid(p, 1, 10, 7); p.xp = 60000;          OBSERVE(validate_pebble(p));
  mk_valid(p, 1, 10, 7); p.genome.crc16 ^= 1;   OBSERVE(validate_pebble(p));
  mk_valid(p, 1, 10, 7); p.moves[0] = 0;        OBSERVE(validate_pebble(p));
  mk_valid(p, 1, 10, 7); p.moves[0] = MV_PLAGA; OBSERVE(validate_pebble(p));
  mk_valid(p, 1, 10, 7); p.hp_cur = 0xFFFF;     OBSERVE(validate_pebble(p));
  mk_valid(p, 5, 10, 7); p.evo_state = 0;       OBSERVE(validate_pebble(p));
  mk_valid(p, 1,  1, 7); p.evo_state |= EVO_STATE_PENDING; OBSERVE(validate_pebble(p));
  mk_valid(p, 1, 10, 7); p.status = 0x20;       OBSERVE(validate_pebble(p));
  mk_valid(p, 1, 10, 7); p.flags = 0x40;        OBSERVE(validate_pebble(p));
  mk_valid(p, 1, 10, 7); p.origin = ORIGIN_COUNT; OBSERVE(validate_pebble(p));
  mk_valid(p, 1, 10, 7); p.trait_id = 1;        OBSERVE(validate_pebble(p));
  mk_valid(p, 1, 10, 7); p.custom_sprite = 99;  OBSERVE(validate_pebble(p));
  mk_valid(p, 1, 10, 7); p.care[0] = -1;        OBSERVE(validate_pebble(p));
  mk_valid(p, 1, 10, 7); memset(p.nickname, 'x', sizeof p.nickname);
                                                OBSERVE(validate_pebble(p));
  {
    PebbleInstance m[BATTLE_TEAM_MAX];
    for (uint8_t i = 0; i < 3u; ++i) mk_valid(m[i], (uint8_t)(1u + i), 10, 0x60u + i);
    OBSERVE(validate_team(m, 0u, bad));
    m[1].id = m[0].id;
    OBSERVE(validate_team(m, 3u, bad));
    m[1].id = 0x61u;
    OBSERVE(validate_level_band(m, 3u, 20u, 30u, bad));
    // BATTLE ELIGIBILITY, the other session-scoped rule. Its POSITIVE control
    // is one line below, because a function that always refused would satisfy
    // the reachability sweep on its own.
    OBSERVE(validate_battle_ready(m, 3u, bad));       // VR_OK: nobody has fainted
    CHECK_EQ(validate_battle_ready(m, 3u, bad), VR_OK);
    m[2].hp_cur = 0u;
    OBSERVE(validate_battle_ready(m, 3u, bad));
    CHECK_EQ(bad, 2);
  }

  // THE FOURTEEN CREATOR CODES (P8-C3). Same rule as every code above: each is
  // produced by the function that owns it, on a fixture in which its own field
  // is the only illegal thing. Section 5 below is where each one gets its
  // positive control; this sweep is what fails when a code is added and no case
  // ever produces it.
  {
    CustomSpeciesRec c;
    mk_cs(c);                                     OBSERVE(validate_custom_species(c));
    mk_cs(c); c.magic ^= 1u;                      OBSERVE(validate_custom_species(c));
    mk_cs(c); c.reserved[0] = 1u;                 OBSERVE(validate_custom_species(c));
    mk_cs(c); c.type = (uint8_t)TYPE_NEUTRAL;     OBSERVE(validate_custom_species(c));
    mk_cs(c); c.base[0] = 11u;                    OBSERVE(validate_custom_species(c));
    mk_cs(c); c.base[0] = (uint8_t)(c.base[0] + 4u); OBSERVE(validate_custom_species(c));
    mk_cs(c); c.moves[0] = 0u;    cs_price(c);    OBSERVE(validate_custom_species(c));
    mk_cs(c); c.moves[1] = c.moves[0]; cs_price(c); OBSERVE(validate_custom_species(c));
    mk_cs(c); c.moves[1] = MV_BYTAZO; cs_price(c); OBSERVE(validate_custom_species(c));
    mk_cs(c); cs_moves(c, MV_AMPLIFICAR, MV_ANTENA, MV_DEFRAG, MV_DEPURAR);
                                                  OBSERVE(validate_custom_species(c));
    mk_cs(c); cs_moves(c, MV_PING, MV_APUESTA, MV_DEFRAG, MV_DEPURAR);
                                                  OBSERVE(validate_custom_species(c));
    mk_cs(c); cs_moves(c, MV_RAFAGA, MV_ECO_DOBLE, MV_PULSO, MV_CHOQUE);
                                                  OBSERVE(validate_custom_species(c));
    mk_cs(c); c.budget_used = (uint16_t)(c.budget_used + 1u);
                                                  OBSERVE(validate_custom_species(c));
    mk_cs(c); c.name[0] = '\0';                   OBSERVE(validate_custom_species(c));
    mk_cs(c); c.compat_group = 1u;                OBSERVE(validate_custom_species(c));
  }
  #undef OBSERVE

  // Every CONTENT, TEAM and SESSION code has been produced by the function that
  // owns it...
  for (int r = (int)VR_NULL_ID; r < (int)VR_REJECT_COUNT; ++r) {
    if (!seen[r]) fprintf(stderr, "    unreachable: %s\n",
                          validate_reject_name((VReject)r));
    CHECK(seen[r]);
  }
  CHECK(seen[(int)VR_OK]);
  // ...and NOT ONE of the six wire codes was produced by any of them. They
  // belong to networking/protocol.cpp's pbw_decode() and to nothing else.
  for (int r = (int)VR_WIRE_MAGIC; r <= (int)VR_WIRE_STATUS_BITS; ++r) {
    if (seen[r]) fprintf(stderr, "    leaked a wire code: %s\n",
                         validate_reject_name((VReject)r));
    CHECK(!seen[r]);
  }
}

TEST(every_reject_has_a_distinct_english_name_and_the_lookup_is_total) {
  for (int r = 0; r < (int)VR_REJECT_COUNT; ++r) {
    const char* n = validate_reject_name((VReject)r);
    CHECK(n != nullptr);
    CHECK(strncmp(n, "VR_", 3) == 0);
    CHECK(strcmp(n, "VR_?") != 0);
    for (int q = 0; q < r; ++q) CHECK(strcmp(n, validate_reject_name((VReject)q)) != 0);
  }
  CHECK(strcmp(validate_reject_name((VReject)VR_REJECT_COUNT), "VR_?") == 0);
  CHECK(strcmp(validate_reject_name((VReject)255), "VR_?") == 0);
}

// =============================================================================
//  5. THE RELATIONSHIP WITH THE ENGINE - the claim game/validate.h makes
// =============================================================================
static void mk_setup_from(BattleSetup& s, const PebbleInstance* team, uint8_t count)
{
  battle_setup_clear(s);
  s.seed     = 0xC0FFEEu;
  s.count[0] = count;
  s.count[1] = 1u;
  for (uint8_t i = 0; i < count; ++i) s.member[0][i] = team[i];
  mk_valid(s.member[1][0], 16, 10, 0xB00Bu);        // a fixed legal opponent
}

// ONE SWEEP, TWO CLAIMS. The whole roster x 5 levels x 8 hostile variants - 60
// species since P9-C3, and the number is not written down here on purpose: the
// loop walks SPECIES_TABLE_COUNT and a comment carrying a literal is a comment
// that goes stale the next time the roster moves. Run once
// against validate_team() alone and once against the whole chain a linked
// battle actually puts a team through. `with_battle_ready` is the ONLY
// difference, so the two cases below are the same measurement asking two
// different questions - and the pair is what shows the exception is exactly one
// code wide and exactly where validate.h says it is.
struct ContainCount { int accepted, br_ok, br_fainted, refused_ready; bool other_seen; };

static void sweep_containment(bool with_battle_ready, ContainCount& c)
{
  memset(&c, 0, sizeof c);
  for (uint8_t s = 1; s <= (uint8_t)SPECIES_TABLE_COUNT; ++s) {
    for (uint8_t lv = 1; lv <= (uint8_t)PB_LEVEL_MAX; lv = (uint8_t)(lv + 7u)) {
      for (uint8_t variant = 0; variant < 8u; ++variant) {
        PebbleInstance m[BATTLE_TEAM_MAX];
        for (uint8_t i = 0; i < 3u; ++i) {
          const uint8_t sp = (uint8_t)(((s + i * 5u - 1u) % SPECIES_TABLE_COUNT) + 1u);
          mk_valid(m[i], sp, lv, 0x1000u + s * 16u + i);
        }
        // Eight hostile variants, so the sweep is not a parade of legal teams.
        switch (variant) {
          case 0: break;                                    // legal
          case 1: m[1].hp_cur = 0u; break;                  // the faint arm
          case 2: m[2].status |= (uint8_t)PBS_FAINTED; break;
          case 3: m[0].moves[1] = MV_PLAGA; break;
          case 4: m[0].level = 31u; break;
          case 5: m[1].id = m[0].id; break;
          case 6: m[2].hp_cur = 0xFFFFu; break;
          case 7: m[0].species_id = 200u; break;            // the custom range
          default: break;
        }
        uint8_t bad = 0;
        const VReject v = validate_team(m, 3u, bad);
        if (v != VR_OK) continue;
        if (with_battle_ready) {
          uint8_t bad2 = 0;
          if (validate_battle_ready(m, 3u, bad2) != VR_OK) { c.refused_ready++; continue; }
        }
        c.accepted++;

        BattleSetup su; mk_setup_from(su, m, 3u);
        BattleState st;
        const BattleReject b = battle_init(st, su);
        if      (b == BR_OK)             c.br_ok++;
        else if (b == BR_MEMBER_FAINTED) c.br_fainted++;
        else {
          c.other_seen = true;
          fprintf(stderr, "    validator accepted, engine said %d\n", (int)b);
        }
      }
    }
  }
}

TEST(a_team_the_validator_accepts_the_engine_refuses_only_for_fainting) {
  ContainCount c;
  sweep_containment(false, c);
  // THE CLAIM about the SHARED BODY: nothing but BR_OK or BR_MEMBER_FAINTED.
  CHECK(!c.other_seen);
  // AND THE SWEEP IS NOT VACUOUS: both outcomes actually happen, so the test
  // cannot pass by accepting nothing and cannot pass by never fainting.
  CHECK(c.accepted > 100);
  CHECK(c.br_ok > 0);
  CHECK(c.br_fainted > 0);
  CHECK_EQ(c.br_ok + c.br_fainted, c.accepted);
}

TEST(a_battle_ready_team_is_one_the_engine_accepts_outright) {
  // THE TIGHTENED CLAIM, which is what networking/battle_link.cpp's link_begin()
  // rests on when it records a BattleReject as SD_INTERNAL. Add
  // validate_battle_ready() to the chain and the ONE exception above disappears:
  // every team that survives is BR_OK, so a BattleReject on the linked path
  // really is a bug in this tree and never a peer capability.
  ContainCount c;
  sweep_containment(true, c);
  CHECK(!c.other_seen);
  CHECK_EQ(c.br_fainted, 0);                 // the exception is gone
  CHECK_EQ(c.br_ok, c.accepted);
  // NOT VACUOUS IN EITHER DIRECTION: the new check must have refused something
  // (or it is a no-op that would pass anyway) and must have let most teams
  // through (or it is a filter that empties the sweep).
  CHECK(c.accepted > 100);
  CHECK(c.refused_ready > 0);
  ContainCount base;
  sweep_containment(false, base);
  CHECK_EQ(c.refused_ready, base.br_fainted);
  CHECK_EQ(c.accepted, base.br_ok);
}

// The one rule this tree implements TWICE, pinned to its twin rather than
// described. game/battle.cpp's moveset_is_learnable() is static, so it is
// reached through battle_init()'s BR_UNLEARNABLE_MOVE.
TEST(the_two_learnset_checkers_agree_on_every_roster_row) {
  int learnable = 0, refused = 0;
  for (uint8_t s = 1; s <= (uint8_t)SPECIES_TABLE_COUNT; ++s) {
    for (uint8_t t = 1; t <= (uint8_t)SPECIES_TABLE_COUNT; ++t) {
      PebbleInstance p; mk_valid(p, s, 20, 0x2000u + s * 64u + t);
      memcpy(p.moves, species_get(t)->moves, sizeof p.moves);

      const bool v_says = (validate_pebble(p) == VR_UNLEARNABLE_MOVESET);

      PebbleInstance one[1] = { p };
      BattleSetup su; mk_setup_from(su, one, 1u);
      BattleState st;
      const bool e_says = (battle_init(st, su) == BR_UNLEARNABLE_MOVE);

      if (v_says != e_says)
        fprintf(stderr, "    species %u with %u's moves: validator %d engine %d\n",
                (unsigned)s, (unsigned)t, (int)v_says, (int)e_says);
      CHECK_EQ((int)v_says, (int)e_says);
      if (v_says) refused++; else learnable++;
    }
  }
  // Neither side of the sweep is empty, so the agreement is not the agreement
  // of two functions that always say the same thing.
  CHECK(refused > 0);
  CHECK(learnable > 0);
  CHECK_EQ(refused + learnable,
           (int)SPECIES_TABLE_COUNT * (int)SPECIES_TABLE_COUNT);
}

// =============================================================================
//  6. THE CONSTRUCTOR. The coupled fix of this commit, and the test that makes
//     reverting it fail by name.
// =============================================================================
static GameState g_box_state;

static void box_fixture(void) {
  memset(&g_box_state, 0, sizeof g_box_state);
  for (uint8_t i = 0; i < (uint8_t)BOX_SLOTS; ++i) {
    g_box_state.pebbles[i].magic      = (uint16_t)PEBBLE_MAGIC;
    g_box_state.pebbles[i].layout_ver = (uint8_t)PEBBLE_LAYOUT_VER;
  }
  g_box_state.box.magic           = (uint16_t)BOX_MAGIC;
  g_box_state.box.active_slot     = (uint8_t)BOX_ACTIVE_NONE;
  g_box_state.box.next_id_counter = 1;
  g_box_state.cfg.device_id       = 0xB0FFE501u;
  box_bind(g_box_state);
}

TEST(a_constructed_pebble_validates) {
  // MEASURED AND RECORDED IN game/validate.h: box_new_pebble() does NOT call
  // the validator, because it has never required a SEALED genome and four
  // shipped test files hand it a zeroed one. What it must do is build a Pebble
  // the validator accepts when it IS handed a real genome - and before P4-C5 it
  // did not, because it never wrote evo_state.
  int stage_nonzero = 0;
  for (uint8_t s = 1; s <= (uint8_t)SPECIES_TABLE_COUNT; ++s) {
    for (uint8_t lv = 1; lv <= 25u; lv = (uint8_t)(lv + 8u)) {
      box_fixture();
      const uint8_t slot = box_new_pebble(s, lv, (uint8_t)ORIGIN_WILD,
                                          sealed_genome(0x1234u + s), 0xC0FFEEu, 1000u);
      CHECK(slot != (uint8_t)BOX_SLOT_NONE);
      const PebbleInstance* p = box_slot(slot);
      CHECK(p != nullptr);
      if (p == nullptr) continue;
      const VReject r = validate_pebble(*p);
      if (r != VR_OK) fprintf(stderr, "    box_new_pebble(%u, %u) -> %s\n",
                              (unsigned)s, (unsigned)lv, validate_reject_name(r));
      CHECK_EQ((int)r, (int)VR_OK);
      CHECK_EQ((int)(p->evo_state & EVO_STATE_STAGE_MASK), (int)species_get(s)->stage);
      if (species_get(s)->stage != 0u) stage_nonzero++;
    }
  }
  // Two thirds of the roster is stage 1 or 2, so deleting the one line in
  // box.cpp cannot pass this case by accident.
  CHECK(stage_nonzero > 0);
}

// =============================================================================
//  6. THE CREATOR DEFINITION (P8-C3, spec sections 35 and 36)
//
//  THE SAME THREE RULES THE REST OF THIS FILE OBEYS: every case starts from
//  mk_cs(), which validate_custom_species() accepts, and makes exactly ONE
//  thing wrong; every case asserts the EXACT VReject; and every case carries a
//  positive control that proves it reached the guard it names.
// =============================================================================

TEST(the_creator_fixture_itself_is_accepted) {
  CustomSpeciesRec c;
  mk_cs(c);
  const VReject r = validate_custom_species(c);
  if (r != VR_OK) fprintf(stderr, "    mk_cs -> %s\n", validate_reject_name(r));
  CHECK_EQ((int)r, (int)VR_OK);
  // Without this the whole section is vacuous: a zeroed record is refused by
  // VR_CS_BAD_HEADER before any other guard can be reached.
  CustomSpeciesRec zero;
  memset(&zero, 0, sizeof zero);
  CHECK_EQ((int)validate_custom_species(zero), (int)VR_CS_BAD_HEADER);
}

TEST(appendix_c_prices_the_spec_example_at_seventy_two_percent) {
  CustomSpeciesRec c;
  mk_cs(c);
  uint16_t stat_used = 0, attack_used = 0;
  creator_cost_of(c, stat_used, attack_used);
  CHECK_EQ((int)stat_used, 21);
  CHECK_EQ((int)attack_used, 91);
  CHECK_EQ((int)creator_power_pct(stat_used, attack_used), 72);

  // THE WHOLE 72 % BAND, AND ITS EDGES, because the spec ships one number and a
  // formula that has to reproduce it on every input near it. balance.json
  // writes it in floats; core arithmetic here is integer, rounding half up.
  CHECK_EQ((int)creator_power_pct(22, 79), 71);
  CHECK_EQ((int)creator_power_pct(22, 80), 72);
  CHECK_EQ((int)creator_power_pct(22, 83), 72);
  CHECK_EQ((int)creator_power_pct(22, 84), 73);
  CHECK_EQ((int)creator_power_pct(21, 88), 72);
  CHECK_EQ((int)creator_power_pct(21, 91), 72);
  CHECK_EQ((int)creator_power_pct(21, 92), 73);
  // A full budget on both axes is 100 %, and nothing can exceed it.
  CHECK_EQ((int)creator_power_pct(CREATOR_TOTAL_STAT_POINTS,
                                 CREATOR_ATTACK_BUDGET), 100);
  CHECK_EQ((int)creator_power_pct(0, 0), 0);
  CHECK_EQ((int)creator_power_pct(60000, 60000), 100);   // saturates, never wraps
}

TEST(the_spec_example_is_unreachable_at_a_full_stat_budget) {
  // THE MEASUREMENT THAT SETTLED THE STAT RULE, run rather than quoted. If the
  // creator required stats == CREATOR_TOTAL_STAT_POINTS, Appendix C's own
  // worked example could not be produced on this device: the cheapest legal
  // four-move set for every type costs more than the 80..83 that 72 % needs.
  uint16_t cheapest[TYPE_COUNT];
  for (uint8_t t = 0; t < (uint8_t)TYPE_COUNT; ++t) cheapest[t] = 0xFFFFu;

  for (uint8_t t = 0; t < (uint8_t)TYPE_COUNT; ++t) {
    for (uint8_t a = 1; a <= ATTACK_COUNT; ++a)
    for (uint8_t b = (uint8_t)(a + 1u); b <= ATTACK_COUNT; ++b)
    for (uint8_t d = (uint8_t)(b + 1u); d <= ATTACK_COUNT; ++d)
    for (uint8_t e = (uint8_t)(d + 1u); e <= ATTACK_COUNT; ++e) {
      CustomSpeciesRec c;
      mk_cs(c);
      c.type = t;
      cs_moves(c, a, b, d, e);
      if (validate_custom_species(c) != VR_OK) continue;
      if (c.budget_used < cheapest[t]) cheapest[t] = c.budget_used;
    }
  }
  for (uint8_t t = 0; t < (uint8_t)TYPE_COUNT; ++t) {
    CHECK(cheapest[t] != 0xFFFFu);                 // every type can build one
    // 72 % at a full 22-point stat budget needs an attack cost of 80..83.
    CHECK(cheapest[t] > 83u);
    CHECK_EQ((int)creator_power_pct(CREATOR_TOTAL_STAT_POINTS, cheapest[t]), 73);
  }
  // And the band's floor is what makes the spec example possible at all.
  CHECK_EQ((int)CREATOR_STAT_POINTS_MIN, (int)CREATOR_STAT_POINTS_BY_STAGE[0]);
  CHECK(CREATOR_STAT_POINTS_MIN < CREATOR_TOTAL_STAT_POINTS);
}

TEST(a_definition_that_is_too_strong_is_refused_on_every_axis) {
  CustomSpeciesRec c;

  // STATS over the band.
  mk_cs(c); c.base[0] = 10u; c.base[1] = 10u;    // 10+10+5+5 = 30
  CHECK_EQ((int)validate_custom_species(c), (int)VR_CS_STAT_BUDGET);
  // ...and under it. A creator that could make deliberately useless trade bait
  // is the other half of the rule, and it is the half a `<=` alone would miss.
  mk_cs(c); c.base[0] = 1u; c.base[1] = 1u; c.base[2] = 1u; c.base[3] = 1u;
  CHECK_EQ((int)validate_custom_species(c), (int)VR_CS_STAT_BUDGET);
  mk_cs(c); c.base[0] = 1u; c.base[1] = 5u; c.base[2] = 5u; c.base[3] = 5u;  // 16
  CHECK_EQ((int)validate_custom_species(c), (int)VR_OK);   // exactly the floor
  mk_cs(c); c.base[0] = 7u; c.base[1] = 5u; c.base[2] = 5u; c.base[3] = 5u;  // 22
  CHECK_EQ((int)validate_custom_species(c), (int)VR_OK);   // exactly the ceiling
  mk_cs(c); c.base[0] = 8u; c.base[1] = 5u; c.base[2] = 5u; c.base[3] = 5u;  // 23
  CHECK_EQ((int)validate_custom_species(c), (int)VR_CS_STAT_BUDGET);

  // A SINGLE STAT out of 1..10, which is a different rule and a different code.
  mk_cs(c); c.base[3] = 0u;
  CHECK_EQ((int)validate_custom_species(c), (int)VR_CS_BAD_STAT);
  mk_cs(c); c.base[3] = 11u;
  CHECK_EQ((int)validate_custom_species(c), (int)VR_CS_BAD_STAT);

  // THE ATTACK BUDGET.
  mk_cs(c); cs_moves(c, MV_RAFAGA, MV_ECO_DOBLE, MV_PULSO, MV_CHOQUE);   // 216
  CHECK(c.budget_used > (uint16_t)CREATOR_ATTACK_BUDGET);
  CHECK_EQ((int)validate_custom_species(c), (int)VR_CS_ATTACK_BUDGET);

  // THE STAGE-1 POWER CAP. Apuesta is power 100 against a cap of 90, and its
  // set is well inside the attack budget - so this case can only be the cap.
  mk_cs(c); cs_moves(c, MV_PING, MV_APUESTA, MV_DEFRAG, MV_DEPURAR);
  CHECK(c.budget_used <= (uint16_t)CREATOR_ATTACK_BUDGET);
  CHECK_EQ((int)validate_custom_species(c), (int)VR_CS_POWER_CAP);
  // Eco Doble is power 90, exactly the cap, and must be ACCEPTED: a cap that
  // refused its own boundary would be a different rule than the one the roster
  // is held to.
  mk_cs(c); cs_moves(c, MV_ECO_DOBLE, MV_AMPLIFICAR, MV_DEFRAG, MV_DEPURAR);
  CHECK_EQ((int)attack_get(MV_ECO_DOBLE)->power, (int)CREATOR_POWER_CAP_BY_STAGE[1]);
  CHECK_EQ((int)validate_custom_species(c), (int)VR_OK);
}

TEST(the_move_rules_are_the_ones_the_shipped_roster_is_held_to) {
  CustomSpeciesRec c;

  mk_cs(c); c.moves[2] = 0u;              cs_price(c);
  CHECK_EQ((int)validate_custom_species(c), (int)VR_CS_UNKNOWN_MOVE);
  mk_cs(c); c.moves[2] = (uint8_t)(ATTACK_COUNT + 1u); cs_price(c);
  CHECK_EQ((int)validate_custom_species(c), (int)VR_CS_UNKNOWN_MOVE);

  mk_cs(c); c.moves[3] = c.moves[0];      cs_price(c);
  CHECK_EQ((int)validate_custom_species(c), (int)VR_CS_MOVE_REPEATED);

  // OFF-TYPE. Bytazo is CORRUPT and the fixture is SIGNAL; the same set on a
  // CORRUPT species is the positive control, which is what proves the rule is
  // "own type or NEUTRAL" and not "this particular move is banned".
  mk_cs(c); c.moves[1] = MV_BYTAZO;       cs_price(c);
  CHECK_EQ((int)validate_custom_species(c), (int)VR_CS_MOVE_OFF_TYPE);
  mk_cs(c); c.type = (uint8_t)TYPE_CORRUPT;
  cs_moves(c, MV_BYTAZO, MV_DEFRAG, MV_OVERCLOCK, MV_DEPURAR);
  CHECK_EQ((int)validate_custom_species(c), (int)VR_OK);

  // FOUR STATUS MOVES CANNOT WIN A BATTLE - attacks_table.h's own rule for the
  // built-in roster, applied to a player's Pebble by the same words.
  mk_cs(c); cs_moves(c, MV_AMPLIFICAR, MV_ANTENA, MV_DEFRAG, MV_DEPURAR);
  CHECK_EQ((int)validate_custom_species(c), (int)VR_CS_NO_DAMAGING_MOVE);
  // One damaging move is enough, and it is the ONLY difference here.
  mk_cs(c); cs_moves(c, MV_PING, MV_ANTENA, MV_DEFRAG, MV_DEPURAR);
  CHECK_EQ((int)validate_custom_species(c), (int)VR_OK);
}

TEST(the_page_may_not_price_its_own_pebble) {
  CustomSpeciesRec c;
  mk_cs(c);
  const uint16_t honest = c.budget_used;
  c.budget_used = 0u;                       // "this Pebble is free"
  CHECK_EQ((int)validate_custom_species(c), (int)VR_CS_BUDGET_MISMATCH);
  c.budget_used = (uint16_t)(honest + 1u);
  CHECK_EQ((int)validate_custom_species(c), (int)VR_CS_BUDGET_MISMATCH);
  c.budget_used = honest;
  CHECK_EQ((int)validate_custom_species(c), (int)VR_OK);
}

TEST(the_name_charset_is_the_one_the_panel_can_draw) {
  CustomSpeciesRec c;

  // EMPTY, and the positive control is one character.
  mk_cs(c); c.name[0] = '\0';
  CHECK_EQ((int)validate_custom_species(c), (int)VR_CS_BAD_NAME);
  mk_cs(c); memset(c.name, 0, sizeof c.name); c.name[0] = 'A';
  CHECK_EQ((int)validate_custom_species(c), (int)VR_OK);

  // UNTERMINATED inside the field. This is a MEMORY SAFETY rule and not a
  // spelling one: the name is handed out as a bare const char* and snprintf'd
  // with "%s", so an unterminated one reads forward into the next field.
  mk_cs(c); memset(c.name, 'x', sizeof c.name);
  CHECK_EQ((int)validate_custom_species(c), (int)VR_CS_BAD_NAME);

  // EXACTLY NAME_MAX_LEN is legal, one more is not.
  mk_cs(c); memset(c.name, 0, sizeof c.name); memset(c.name, 'x', NAME_MAX_LEN);
  CHECK_EQ((int)validate_custom_species(c), (int)VR_OK);

  // THE CHARACTER SET. Latin-1 accents are drawable and must be accepted; a
  // control byte and a codepoint the _tf fonts do not carry must not be.
  mk_cs(c); memset(c.name, 0, sizeof c.name);
  c.name[0] = (char)0xF1; c.name[1] = (char)0xE1; c.name[2] = (char)0xBF;  // n-tilde, a-acute, inverted ?
  CHECK_EQ((int)validate_custom_species(c), (int)VR_OK);
  mk_cs(c); c.name[1] = (char)0x01;
  CHECK_EQ((int)validate_custom_species(c), (int)VR_CS_BAD_NAME);
  mk_cs(c); c.name[1] = (char)0x7F;
  CHECK_EQ((int)validate_custom_species(c), (int)VR_CS_BAD_NAME);
  mk_cs(c); c.name[1] = (char)0x80;                 // a C1 control, not Latin-1 text
  CHECK_EQ((int)validate_custom_species(c), (int)VR_CS_BAD_NAME);

  // A LEADING OR TRAILING SPACE IS REFUSED, NOT TRIMMED. Trimming is mending,
  // and this validator takes its record const so it could not mend if it wanted
  // to; a space in the middle is an ordinary character.
  mk_cs(c); memset(c.name, 0, sizeof c.name); memcpy(c.name, " Bicho", 7);
  CHECK_EQ((int)validate_custom_species(c), (int)VR_CS_BAD_NAME);
  mk_cs(c); memset(c.name, 0, sizeof c.name); memcpy(c.name, "Bicho ", 7);
  CHECK_EQ((int)validate_custom_species(c), (int)VR_CS_BAD_NAME);
  mk_cs(c); memset(c.name, 0, sizeof c.name); memcpy(c.name, "Bi ho", 6);
  CHECK_EQ((int)validate_custom_species(c), (int)VR_OK);

  // The predicate and the validator agree on every byte, because they are the
  // same function: creator_name_char_ok() is what networking/creator_parse.cpp
  // asks of a UTF-8 upload and what this file asks of a flash record.
  for (int ch = 0; ch < 256; ++ch) {
    mk_cs(c); memset(c.name, 0, sizeof c.name);
    c.name[0] = (char)ch;
    const bool ok = validate_custom_species(c) == VR_OK;
    CHECK_EQ((int)ok, (int)(creator_name_char_ok((uint8_t)ch) && ch != ' '));
  }
}

TEST(a_custom_species_never_breeds_and_never_carries_an_evolution) {
  CustomSpeciesRec c;
  mk_cs(c);
  // The record half: any non-zero compat group is refused, so "it does not
  // breed" cannot be undone by setting a field.
  for (uint8_t g = 1; g < 8u; ++g) {
    mk_cs(c); c.compat_group = g;
    CHECK_EQ((int)validate_custom_species(c), (int)VR_CS_BAD_COMPAT);
  }
  // The projection half: no family, no evolution rule, no spawn weight - so
  // game/breeding.cpp has no child species to derive and game/evolution.cpp has
  // nothing to evolve into. All three are structural, not checks.
  mk_cs(c);
  csp_reset();
  CHECK(csp_install(c));
  const SpeciesDef* sp = species_get(csp_species_id(0));
  CHECK(sp != nullptr);
  if (sp) {
    CHECK_EQ((int)sp->family, 0);
    CHECK_EQ((int)sp->evo_rule, (int)SPECIES_EVO_NONE);
    CHECK_EQ((int)sp->spawn_weight, 0);
    CHECK_EQ((int)sp->compat_group, 0);
    CHECK_EQ((int)sp->stage, 1);          // the budget it was measured against
    CHECK_EQ((int)sp->category_mask, 0);  // never a wild encounter
  }
  csp_reset();
}

TEST(a_refused_definition_is_never_installed) {
  CustomSpeciesRec c;
  csp_reset();
  CHECK_EQ((int)csp_count(), 0);
  CHECK(species_get((uint8_t)CREATOR_SPECIES_ID_MIN) == nullptr);

  mk_cs(c); c.base[0] = 11u;                       // one illegal field
  CHECK(!csp_install(c));
  CHECK_EQ((int)csp_count(), 0);
  // AND THE ID STILL DOES NOT RESOLVE, which is the property that matters: a
  // record that survived a CRC and failed the rules must leave the slot EMPTY,
  // so the Pebble pointing at it is quarantined by name at the next scan rather
  // than resolved to a creature with a 30-point stat total.
  CHECK(species_get((uint8_t)CREATOR_SPECIES_ID_MIN) == nullptr);

  mk_cs(c);
  CHECK(csp_install(c));
  CHECK_EQ((int)csp_count(), 1);
  CHECK(species_get((uint8_t)CREATOR_SPECIES_ID_MIN) != nullptr);
  csp_reset();
  CHECK(species_get((uint8_t)CREATOR_SPECIES_ID_MIN) == nullptr);
}

TEST(the_registry_hands_out_ten_slots_and_no_eleventh) {
  csp_reset();
  for (uint8_t i = 0; i < (uint8_t)CREATOR_SPECIES_SLOTS; ++i) {
    CHECK_EQ((int)csp_free_slot(), (int)i);
    CustomSpeciesRec c;
    mk_cs(c);
    c.slot = i;
    CHECK(csp_install(c));
    CHECK_EQ((int)csp_species_id(i), (int)(CREATOR_SPECIES_ID_MIN + i));
    CHECK_EQ((int)csp_slot_of((uint8_t)(CREATOR_SPECIES_ID_MIN + i)), (int)i);
  }
  CHECK_EQ((int)csp_count(), (int)CREATOR_SPECIES_SLOTS);
  CHECK_EQ((int)csp_free_slot(), (int)CSP_SLOT_NONE);
  // One past the range resolves to nothing, and so does a built-in id.
  CHECK(species_get((uint8_t)(CREATOR_SPECIES_ID_MAX + 1u)) == nullptr);
  CHECK(csp_get(1u) == nullptr);
  CHECK_EQ((int)csp_slot_of(1u), (int)CSP_SLOT_NONE);

  csp_forget(3u);
  CHECK_EQ((int)csp_free_slot(), 3);
  CHECK(species_get((uint8_t)(CREATOR_SPECIES_ID_MIN + 3u)) == nullptr);
  csp_reset();
}

TEST(a_creator_pebble_is_an_ordinary_pebble_to_the_one_validator) {
  // THE WHOLE PIPELINE MINUS THE SOCKET, over the REAL registry, the REAL
  // constructor and the REAL validator - which is the only way this claim means
  // anything about the firmware. It is also the defect that was already waiting
  // in the tree: before the registry, species_get(200) answered nullptr, so
  // box_new_pebble() refused outright and a Pebble filed any other way was
  // quarantined with VR_UNKNOWN_SPECIES on the next boot.
  csp_reset();
  box_fixture();
  CustomSpeciesRec c;
  mk_cs(c);

  // Before installation the constructor refuses, by name.
  CHECK_EQ((int)box_new_pebble((uint8_t)CREATOR_SPECIES_ID_MIN, 1u,
                               (uint8_t)ORIGIN_CREATOR, sealed_genome(0x51u),
                               0xC0FFEEu, 1000u),
           (int)BOX_SLOT_NONE);

  CHECK(csp_install(c));
  const uint8_t species_id = csp_species_id(0);
  const uint8_t slot = box_new_pebble(species_id, 1u, (uint8_t)ORIGIN_CREATOR,
                                      sealed_genome(0x51u), 0xC0FFEEu, 1000u);
  CHECK(slot != (uint8_t)BOX_SLOT_NONE);
  PebbleInstance* p = box_slot(slot);
  CHECK(p != nullptr);
  if (p == nullptr) { csp_reset(); return; }

  // The two fields the constructor cannot know, exactly as
  // networking/creator_server.cpp sets them.
  p->flags = (uint8_t)(p->flags | PBF_CUSTOM | PBF_HAS_CUSTOM_SPRITE);
  p->custom_sprite = 0u;
  memset(p->nickname, 0, sizeof p->nickname);
  memcpy(p->nickname, c.name, 6);

  const VReject r = validate_pebble(*p);
  if (r != VR_OK) fprintf(stderr, "    creator Pebble -> %s\n", validate_reject_name(r));
  CHECK_EQ((int)r, (int)VR_OK);
  CHECK_EQ((int)p->origin, (int)ORIGIN_CREATOR);
  CHECK_EQ((int)(p->evo_state & EVO_STATE_STAGE_MASK), 1);

  // THE LEARNSET RULE IS THE ONE THAT WOULD HAVE BROKEN SILENTLY. A custom row
  // has family 0 and no row in SPECIES_TABLE, so the family walk can never match
  // it: without "a species teaches its own learnset" this is
  // VR_UNLEARNABLE_MOVESET one instruction after the Pebble is built.
  CHECK_EQ((int)species_get(species_id)->family, 0);

  // And the record's own moves are what the creature carries - no repair, no
  // substitution.
  for (uint8_t m = 0; m < (uint8_t)PB_MOVE_COUNT; ++m)
    CHECK_EQ((int)p->moves[m], (int)c.moves[m]);

  // Forgetting the record makes the SAME Pebble unknown again, which is what a
  // lost or refused cs* blob looks like on the next boot.
  csp_reset();
  CHECK_EQ((int)validate_pebble(*p), (int)VR_UNKNOWN_SPECIES);
}
