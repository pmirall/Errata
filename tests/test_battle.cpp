// =============================================================================
//  PEBBLEBOL host test - test_battle.cpp
//  EVERY spec section 50 BATTLE case, over the real game/battle.cpp (P4-C2).
//
//    type advantage / damage / speed order / switching / fainting / buffs /
//    protection / victory / invalid moves
//
//  THREE RULES THIS FILE OBEYS, because this project has already shipped SIX
//  assertions that could not fail:
//
//   (a) EVERY invalid-action case runs on a FULLY FORMED LEGAL BATTLE built by
//       battle_init(), in which the case's own field is the ONLY illegal thing.
//       A zeroed BattleState has active == 0, flags == 0, hp_cur == 0 and
//       moves[] == 0, so on that fixture every action is refused by
//       BR_EMPTY_ACTIVE long before it can reach BR_UNKNOWN_MOVE,
//       BR_MOVE_ON_COOLDOWN, BR_SWITCH_TO_EMPTY or BR_SWITCH_TO_FAINTED - and a
//       block written against `!= BR_OK` there would pass with four real guards
//       deleted. That is the exact shape of the defect P4-C1 shipped.
//   (b) EVERY case asserts the EXACT BattleReject code, never `!= BR_OK`.
//   (c) EVERY case carries a POSITIVE CONTROL in the same test function: the
//       same action with that one field corrected must return BR_OK **and must
//       change the bytes**. The control is what proves the case reached the
//       guard it names instead of tripping an earlier one; the "must change the
//       bytes" half is what proves the engine is not simply refusing
//       everything.
//
//  The rejection proof is BYTES, not reading: `memcmp` over the whole struct,
//  which is sound here because BattleState has no padding (static_asserted as
//  sum-of-members in battle.h, and re-checked at runtime below) and because
//  battle_init() zeroes the entire object representation. Both the memcmp and
//  the hash are asserted: the memcmp is the stronger one, and the pair failing
//  together versus only the memcmp failing says instantly whether a missed
//  write landed in a byte the hash would have covered.
//
//  CONTROLLED POSITIONS. Several cases need an exact matchup that the roster
//  does not happen to contain (equal speeds, a chosen defence, one specific
//  move). They build a legal battle first and then OVERWRITE the combatant
//  fields, which is honest for a plain struct and is why the derivation itself
//  gets its own case (init_derives_every_stat_through_the_one_owner).
// =============================================================================
#include "nt_test.h"

#include <string.h>
#include <type_traits>

#include "game/battle.h"
#include "game/pebble.h"
#include "game/xp.h"

// -----------------------------------------------------------------------------
//  SEEDS WITH KNOWN DRAWS (measured from core/rng.cpp, not assumed)
//    seed 1     -> rng_next_below(100) = 0,  then rng_next_below(3) = 0
//    seed 22    -> 0,  then 1
//    seed 43    -> 0,  then 2
//    seed 15872 -> 99, i.e. a MISS for any move whose accuracy is <= 99
//  A first draw of 0 hits at any accuracy, including the ACCURACY_MIN floor,
//  so a case that seeds 1 is asserting damage and never accuracy.
// -----------------------------------------------------------------------------
#define SEED_HIT_ROLL0   1u
#define SEED_HIT_ROLL1   22u
#define SEED_HIT_ROLL2   43u
#define SEED_MISS_85     15872u

// A neutral, no-effect, no-cooldown, accuracy-100 damage move: the control move
// for every case that is not about a particular effect.
#define MV_CHOQUE   27u    // NEUTRAL power 50 acc 100
#define MV_PING      1u    // SIGNAL  power 35 acc 100
#define MV_RAFAGA    3u    // SIGNAL  power 75 acc  85
#define MV_AMPLIFICAR 6u   // SIGNAL  power  0 acc 100 BUFF_ATK +1 for 3
#define MV_ANTENA    7u    // SIGNAL  power  0 acc 100 BUFF_DEF +1 for 3
#define MV_FIREWALL 22u    // SYSTEM  power  0 acc 100 PROTECT_HALF 1 round, pri +2, cd 3
#define MV_SANDBOX  31u    // NEUTRAL power  0 acc 100 PROTECT_HALF 2 rounds, pri +2, cd 4
#define MV_INFECCION 13u   // CORRUPT power 20 acc 100 DOT 5 for 3
#define MV_INFECTAR 12u    // CORRUPT power 30 acc  90 EFF_CORRUPT 3 rounds, cd 3
#define MV_ADELANTO  4u    // SIGNAL  power 40 acc 100 priority +1
#define MV_PANICO   26u    // SYSTEM  power 95 acc  70 priority -1 SELF_STUN 1, cd 2
#define MV_DEPURAR  34u    // NEUTRAL power  0 acc 100 CLEANSE, priority +1
#define MV_BLOQUEO  21u    // SYSTEM  power 30 acc  90 DEBUFF_ATK 1 for 3, cd 2

// =============================================================================
//  FIXTURES
// =============================================================================
static void mk_member(PebbleInstance& p, uint8_t species, uint8_t level, uint32_t id)
{
  memset(&p, 0, sizeof p);
  p.magic      = (uint16_t)PEBBLE_MAGIC;
  p.layout_ver = (uint8_t)PEBBLE_LAYOUT_VER;
  p.species_id = species;
  p.id         = id;
  p.level      = level;
  const SpeciesDef* sp = species_get(species);
  if (sp == nullptr) return;
  for (uint8_t m = 0; m < (uint8_t)PB_MOVE_COUNT; ++m) p.moves[m] = sp->moves[m];
  p.hp_cur = xp_hp_max(sp->base_hp, level);
}

// A legal 3v3 at level 10 from real roster rows: SIGNAL vs CORRUPT, so the type
// chart is live in both directions.
static void mk_setup(BattleSetup& s, uint32_t seed,
                     const uint8_t* a, uint8_t na, const uint8_t* b, uint8_t nb,
                     uint8_t level)
{
  battle_setup_clear(s);
  s.seed     = seed;
  s.count[0] = na;
  s.count[1] = nb;
  for (uint8_t i = 0; i < na; ++i) mk_member(s.member[0][i], a[i], level, 0x1000u + i);
  for (uint8_t i = 0; i < nb; ++i) mk_member(s.member[1][i], b[i], level, 0x2000u + i);
}

static const uint8_t TEAM_A[3] = {  1,  4, 13 };   // SIGNAL
static const uint8_t TEAM_B[3] = { 16, 19, 22 };   // CORRUPT

// THE fixture every rejection case starts from: a fully formed, legal, running
// battle in which nothing is illegal until the case makes exactly one thing so.
static void fixture(BattleState& st, uint32_t seed = 0xC0FFEEu, uint8_t level = 10u)
{
  BattleSetup s;
  mk_setup(s, seed, TEAM_A, 3u, TEAM_B, 3u, level);
  const BattleReject r = battle_init(st, s);
  CHECK_EQ(r, BR_OK);
}

// Overwrites one combatant with a chosen matchup. Used only by the cases whose
// subject is arithmetic, never by a rejection case.
static void arm(BattleCombatant& c, uint8_t type, uint8_t atk, uint8_t def, uint8_t spd,
                uint16_t hp, uint8_t m0, uint8_t m1, uint8_t m2, uint8_t m3)
{
  c.flags      = BCF_PRESENT;
  c.type       = type;
  c.atk        = atk;
  c.def        = def;
  c.spd        = spd;
  c.hp_max     = hp;
  c.hp_cur     = hp;
  c.moves[0]   = m0; c.moves[1] = m1; c.moves[2] = m2; c.moves[3] = m3;
  for (uint8_t i = 0; i < (uint8_t)PB_MOVE_COUNT; ++i) c.cooldown[i] = 0u;
  for (uint8_t k = 0; k < (uint8_t)BSTAT_COUNT; ++k) { c.stage[k] = 0; c.stage_left[k] = 0u; }
  c.protect_left = 0u; c.dot_value = 0u; c.dot_left = 0u; c.stun_left = 0u;
  c.corrupt_left = 0u; c.type_edge_left = (uint8_t)TYPE_MOD_MAX_HITS;
}

// How many rng_next() steps separate two cursor values. Used to assert the draw
// BUDGET, not just the final cursor.
static uint32_t draws_from(uint32_t from_s, uint32_t to_s)
{
  Rng r; r.s = from_s;
  for (uint32_t n = 0; n <= 64u; ++n) {
    if (r.s == to_s) return n;
    (void)rng_next(r);
  }
  return 0xFFFFFFFFu;
}

static BattleAction ACT(uint8_t kind, uint8_t index)
{
  BattleAction a; a.kind = kind; a.index = index; return a;
}

// Submits `act` for `side`, requires the EXACT code, and proves nothing moved.
static void refuses(BattleState& st, uint8_t side, BattleAction act, BattleReject want)
{
  BattleState before;
  memcpy(&before, &st, sizeof before);
  const uint32_t h0 = battle_state_hash(st);
  CHECK_EQ(battle_submit_action(st, side, act), want);
  CHECK(memcmp(&before, &st, sizeof st) == 0);
  CHECK_EQ(battle_state_hash(st), h0);
}

// The positive control: the corrected action is accepted AND moves the bytes.
static void accepts(BattleState& st, uint8_t side, BattleAction act)
{
  BattleState before;
  memcpy(&before, &st, sizeof before);
  CHECK_EQ(battle_submit_action(st, side, act), BR_OK);
  CHECK(memcmp(&before, &st, sizeof st) != 0);
}

// Runs one whole round with both actions given.
static BattleStepResult round_with(BattleState& st, BattleAction a, BattleAction b,
                                   BattleLog* log)
{
  CHECK_EQ(battle_submit_action(st, 0u, a), BR_OK);
  CHECK_EQ(battle_submit_action(st, 1u, b), BR_OK);
  return battle_step_round(st, log);
}

// =============================================================================
//  0. LAYOUT AND HASH DISCIPLINE
//     The raw-byte hash and the memcmp rejection proof rest on ONE shared
//     invariant - no padding plus a full zero fill - so it is checked here
//     before anything leans on it.
// =============================================================================
TEST(the_battle_structs_have_no_padding_and_a_unique_object_representation) {
  CHECK_EQ(sizeof(BattleCombatant), 32);
  CHECK_EQ(sizeof(BattleSide), 100);
  CHECK_EQ(sizeof(BattleState), 212);
  CHECK_EQ(sizeof(BattleEvent), 12);
  // Kept in the TEST and not in battle.h so the firmware header never pulls in
  // <type_traits>: the sum-of-members static_asserts in battle.h are the proof
  // that ships, and this is the same property stated the other way round.
  CHECK(std::has_unique_object_representations_v<BattleCombatant>);
  CHECK(std::has_unique_object_representations_v<BattleSide>);
  CHECK(std::has_unique_object_representations_v<BattleState>);
  CHECK(std::has_unique_object_representations_v<BattleEvent>);
}

TEST(the_hash_covers_every_single_byte_of_the_state) {
  BattleState st;
  fixture(st);
  const uint32_t h0 = battle_state_hash(st);
  // FNV-1a's step is a bijection of the accumulator, so a change to ANY byte
  // must reach the final value. This walks all 212 of them: it is the coverage
  // proof a longhand field-by-field hash could never give.
  uint8_t* p = (uint8_t*)&st;
  int misses = 0;
  for (size_t i = 0; i < sizeof st; ++i) {
    p[i] = (uint8_t)(p[i] ^ 0x01u);
    if (battle_state_hash(st) == h0) misses++;
    p[i] = (uint8_t)(p[i] ^ 0x01u);
  }
  CHECK_EQ(misses, 0);
  CHECK_EQ(battle_state_hash(st), h0);
}

TEST(the_hash_moves_when_only_the_random_cursor_has) {
  BattleState st;
  fixture(st);
  const uint32_t h0 = battle_state_hash(st);
  (void)rng_next(st.rng);          // a draw that changed nothing else
  CHECK(battle_state_hash(st) != h0);
}

// THE VERSION GUARANTEE battle.h SELLS, checked instead of asserted. The header
// promises that a peer on different RULES, a different HASH MIXING or a
// different CONTENT PACK can never produce a matching hash. Until the P4-C2/C3
// follow-up that promise was false for the first of the three: the basis carried
// BATTLE_HASH_VERSION and CONTENT_VERSION, BATTLE_ENGINE_VER reached nothing,
// and bumping it - the one thing battle.h orders a maintainer to do for a rules
// change - left every hash byte-identical.
//
// A host test cannot recompile the engine three times with three different
// macros, which is exactly why battle_hash_basis() is parameterised. The two
// halves below are what make this non-vacuous TOGETHER: the first says each
// version word reaches the BASIS, the second says the basis reaches the HASH
// with all three words in it - so deleting a word from the call site fails even
// though battle_hash_basis() itself still mixes it.
TEST(each_of_the_three_versions_reaches_the_hash_and_no_two_pairs_collide) {
  const uint32_t E = (uint32_t)BATTLE_ENGINE_VER;
  const uint32_t H = (uint32_t)BATTLE_HASH_VERSION;
  const uint32_t C = (uint32_t)CONTENT_VERSION;
  const uint32_t b = battle_hash_basis(E, H, C);

  // Each word alone, moved by one: the engine version is the arm that was
  // broken, and the other two are the controls beside it.
  CHECK(battle_hash_basis(E + 1u, H, C) != b);
  CHECK(battle_hash_basis(E, H + 1u, C) != b);
  CHECK(battle_hash_basis(E, H, C ^ 1u) != b);
  CHECK_EQ(battle_hash_basis(E, H, C), b);          // and it is a pure function

  // THE COLLISION THE OLD XOR FOLD HAD. `basis ^ hash_ver ^ content_ver` maps
  // (1, 0x5B4A) and (3, 0x5B48) to the same 0x5B4B, so two builds that disagree
  // satisfied one guard. Mixing the bytes through the FNV step separates them.
  CHECK(battle_hash_basis(E, 1u, 0x5B4Au) != battle_hash_basis(E, 3u, 0x5B48u));
  CHECK(battle_hash_basis(1u, 2u, 3u) != battle_hash_basis(3u, 2u, 1u));   // order matters

  // AND THE CALL SITE: battle_state_hash() is FNV-1a over the whole object
  // representation starting from THAT basis, rebuilt here longhand. Drop a
  // version from the basis call in battle.cpp and this is what fails.
  BattleState st;
  fixture(st);
  uint32_t want = b;
  const uint8_t* q = (const uint8_t*)&st;
  for (size_t i = 0; i < sizeof st; ++i) { want ^= q[i]; want *= 16777619u; }
  CHECK_EQ(battle_state_hash(st), want);
}

// =============================================================================
//  1. INIT - the engine must be safe when called by anything
// =============================================================================
TEST(init_derives_every_stat_through_the_one_owner) {
  BattleState st;
  fixture(st, 0xC0FFEEu, 17u);
  for (uint8_t s = 0; s < 2u; ++s) {
    for (uint8_t i = 0; i < 3u; ++i) {
      const uint8_t id = (s == 0u) ? TEAM_A[i] : TEAM_B[i];
      const SpeciesDef* sp = species_get(id);
      CHECK(sp != nullptr);
      if (!sp) continue;
      Genome g; memset(&g, 0, sizeof g);
      PebbleStats want;
      pebble_derive_stats(*sp, 17u, g, want);
      const BattleCombatant* c = battle_combatant(st, s, i);
      CHECK(c != nullptr);
      if (!c) continue;
      CHECK_EQ(c->hp_max, want.hp_max);
      CHECK_EQ(c->hp_max, xp_hp_max(sp->base_hp, 17u));   // the ONE hp rule
      CHECK_EQ(c->atk, want.atk);
      CHECK_EQ(c->def, want.def);
      CHECK_EQ(c->spd, want.spd);
      CHECK_EQ(c->type, sp->type);
      CHECK_EQ(c->hp_cur, c->hp_max);
      CHECK_EQ(c->flags, BCF_PRESENT);
      CHECK_EQ(c->type_edge_left, TYPE_MOD_MAX_HITS);
    }
  }
  CHECK_EQ(st.phase, BP_RUNNING);
  CHECK_EQ(st.outcome, BO_UNDECIDED);
  CHECK_EQ(st.round, 1);
}

// A failed init is bit-defined too: the state is zero and the phase is BP_INIT.
static void init_refuses(BattleSetup& s, BattleReject want)
{
  BattleState st;
  memset(&st, 0xA5, sizeof st);          // poison, so a missing memset shows
  CHECK_EQ(battle_init(st, s), want);
  BattleState zero;
  memset(&zero, 0, sizeof zero);
  CHECK(memcmp(&zero, &st, sizeof st) == 0);
  CHECK_EQ(st.phase, BP_INIT);
}

TEST(init_refuses_every_illegal_team_by_name) {
  BattleSetup s;
  BattleState st;

  // POSITIVE CONTROL FIRST: without it every case below could be satisfied by
  // an init that refuses everything.
  mk_setup(s, 7u, TEAM_A, 3u, TEAM_B, 3u, 10u);
  CHECK_EQ(battle_init(st, s), BR_OK);

  mk_setup(s, 7u, TEAM_A, 3u, TEAM_B, 3u, 10u); s.count[0] = 0u;
  init_refuses(s, BR_TEAM_SIZE);
  mk_setup(s, 7u, TEAM_A, 3u, TEAM_B, 3u, 10u); s.count[1] = (uint8_t)(BATTLE_TEAM_MAX + 1u);
  init_refuses(s, BR_TEAM_SIZE);

  mk_setup(s, 7u, TEAM_A, 3u, TEAM_B, 3u, 10u); s.member[0][1].species_id = 0u;
  init_refuses(s, BR_NULL_MEMBER);
  mk_setup(s, 7u, TEAM_A, 3u, TEAM_B, 3u, 10u); s.member[1][2].id = 0u;
  init_refuses(s, BR_NULL_MEMBER);

  mk_setup(s, 7u, TEAM_A, 3u, TEAM_B, 3u, 10u); s.member[0][0].species_id = 250u;
  init_refuses(s, BR_UNKNOWN_SPECIES);

  mk_setup(s, 7u, TEAM_A, 3u, TEAM_B, 3u, 10u); s.member[0][0].level = 0u;
  init_refuses(s, BR_BAD_LEVEL);
  mk_setup(s, 7u, TEAM_A, 3u, TEAM_B, 3u, 10u); s.member[1][0].level = (uint8_t)(PB_LEVEL_MAX + 1u);
  init_refuses(s, BR_BAD_LEVEL);

  mk_setup(s, 7u, TEAM_A, 3u, TEAM_B, 3u, 10u); s.member[0][2].moves[3] = 0u;
  init_refuses(s, BR_ILLEGAL_MOVESET);
  mk_setup(s, 7u, TEAM_A, 3u, TEAM_B, 3u, 10u); s.member[0][2].moves[1] = (uint8_t)(ATTACK_COUNT + 1u);
  init_refuses(s, BR_ILLEGAL_MOVESET);

  mk_setup(s, 7u, TEAM_A, 3u, TEAM_B, 3u, 10u);
  s.member[1][1].hp_cur = (uint16_t)(s.member[1][1].hp_cur + 1u);
  init_refuses(s, BR_HP_OVER_MAX);

  mk_setup(s, 7u, TEAM_A, 3u, TEAM_B, 3u, 10u); s.member[0][1].hp_cur = 0u;
  init_refuses(s, BR_MEMBER_FAINTED);
  mk_setup(s, 7u, TEAM_A, 3u, TEAM_B, 3u, 10u); s.member[0][1].status |= PBS_FAINTED;
  init_refuses(s, BR_MEMBER_FAINTED);

  // A peer sending one Pebble three times (spec section 9), inside one team...
  mk_setup(s, 7u, TEAM_A, 3u, TEAM_B, 3u, 10u); s.member[0][2].id = s.member[0][0].id;
  init_refuses(s, BR_DUPLICATE_ID);
  // ...and across the two teams, which the naive "walk my own team" loop misses.
  mk_setup(s, 7u, TEAM_A, 3u, TEAM_B, 3u, 10u); s.member[1][0].id = s.member[0][1].id;
  init_refuses(s, BR_DUPLICATE_ID);

  mk_setup(s, 7u, TEAM_A, 3u, TEAM_B, 3u, 10u); s.engine_ver = (uint16_t)(BATTLE_ENGINE_VER + 1u);
  init_refuses(s, BR_VERSION_MISMATCH);
  mk_setup(s, 7u, TEAM_A, 3u, TEAM_B, 3u, 10u); s.content_ver = (uint16_t)(CONTENT_VERSION ^ 1u);
  init_refuses(s, BR_VERSION_MISMATCH);

  // AND THE CONTROL AGAIN, after all of it.
  mk_setup(s, 7u, TEAM_A, 3u, TEAM_B, 3u, 10u);
  CHECK_EQ(battle_init(st, s), BR_OK);
}

// Copies one species' learnset verbatim onto a setup member, so a case can say
// exactly whose four moves that member is carrying.
static void give_learnset(PebbleInstance& p, uint8_t from_species)
{
  const SpeciesDef* sp = species_get(from_species);
  CHECK(sp != nullptr);
  if (sp == nullptr) return;
  for (uint8_t m = 0; m < (uint8_t)PB_MOVE_COUNT; ++m) p.moves[m] = sp->moves[m];
}

// SPEC SECTION 67, and the hole the P4-C2/C3 review found: until that follow-up
// battle_init() checked only that the four move ids RESOLVED, so a peer could
// hand its Pebble any of the 34 attacks. The engine measured the cost itself - a
// species-1 Paketo that wins 0 of 200 scripted 1v1 seeds against species 17 wins
// 100 with attack 11 Plaga written into slot 0, and 103 with the ON-TYPE attack
// 3 Rafaga - so this is a cheat that decides fights, and a type-only rule would
// have missed the bigger half of it.
//
// The rule under test: moves[] must be the VERBATIM learnset of some species in
// the same family at a stage no higher than this one's. Every arm below moves
// exactly one of those three words - the moves, the family, the stage - and the
// order arm pins the "verbatim" that the other three lean on.
TEST(init_refuses_a_moveset_no_species_in_this_family_could_teach) {
  BattleSetup s;
  BattleState st;

  // POSITIVE CONTROL FIRST. member[0][0] is species 1: family 1, stage 0,
  // learnset {1,6,7,27}, which is what mk_member() gives it.
  mk_setup(s, 7u, TEAM_A, 3u, TEAM_B, 3u, 10u);
  CHECK_EQ(battle_init(st, s), BR_OK);

  // OFF-TYPE AND OFF-LEARNSET: attack 11 Plaga is CORRUPT power 75.
  mk_setup(s, 7u, TEAM_A, 3u, TEAM_B, 3u, 10u); s.member[0][0].moves[0] = 11u;
  init_refuses(s, BR_UNLEARNABLE_MOVE);

  // ON-TYPE AND OFF-LEARNSET: attack 3 Rafaga is SIGNAL, exactly like species 1,
  // and species 1 still cannot learn it. This is the arm a "the move must match
  // the species type" rule would let through, and it is the STRONGER cheat.
  mk_setup(s, 7u, TEAM_A, 3u, TEAM_B, 3u, 10u); s.member[0][0].moves[0] = 3u;
  init_refuses(s, BR_UNLEARNABLE_MOVE);

  // A REAL LEARNSET, BUT ANOTHER FAMILY'S. Species 3 is family 1 stage 2 and
  // species 6 is family 2 stage 2, so the STAGE matches and only the family
  // moves: a rule that forgot the family would accept this.
  mk_setup(s, 7u, TEAM_A, 3u, TEAM_B, 3u, 10u);
  s.member[0][0].species_id = 3u;
  s.member[0][0].hp_cur     = xp_hp_max(species_get(3u)->base_hp, 10u);
  give_learnset(s.member[0][0], 6u);
  init_refuses(s, BR_UNLEARNABLE_MOVE);

  // A REAL LEARNSET FROM THE RIGHT FAMILY, BUT FROM AHEAD OF IT. Species 1 is
  // stage 0 and species 2 is stage 1 of the same family: moves travel FORWARD
  // through an evolution and never backward, so a stage-0 Pebble holding its
  // own evolution's kit is a lie. A rule that dropped `stage <=` accepts this.
  mk_setup(s, 7u, TEAM_A, 3u, TEAM_B, 3u, 10u);
  give_learnset(s.member[0][0], 2u);
  init_refuses(s, BR_UNLEARNABLE_MOVE);

  // ITS OWN FOUR MOVES IN ANOTHER ORDER. No writer in the tree can produce this
  // - game/box.cpp and persistence/migration.cpp both memcpy a learnset whole -
  // so "verbatim" is the closure and this is refused. Pinned because it is the
  // one arm a future move-reordering feature would have to come back and relax.
  mk_setup(s, 7u, TEAM_A, 3u, TEAM_B, 3u, 10u);
  s.member[0][0].moves[0] = 6u; s.member[0][0].moves[1] = 1u;   // {1,6,..} -> {6,1,..}
  init_refuses(s, BR_UNLEARNABLE_MOVE);

  // THE OTHER SIDE IS CHECKED TOO, and a middle slot as well as slot 0.
  mk_setup(s, 7u, TEAM_A, 3u, TEAM_B, 3u, 10u); s.member[1][2].moves[2] = 27u;
  init_refuses(s, BR_UNLEARNABLE_MOVE);

  // THE ARM THAT MAKES THE RULE MORE THAN "moves must equal MY learnset", and
  // the reason a per-species membership test is wrong: an evolved Pebble keeps
  // the kit it grew up with. game/evolution.cpp deliberately leaves moves[]
  // alone, so a species-2 Fragmar legitimately carries species 1's {1,6,7,27},
  // which is NOT species 2's own {5,27,31,33}. This MUST be accepted.
  mk_setup(s, 7u, TEAM_A, 3u, TEAM_B, 3u, 10u);
  s.member[0][0].species_id = 2u;
  s.member[0][0].hp_cur     = xp_hp_max(species_get(2u)->base_hp, 10u);
  give_learnset(s.member[0][0], 1u);
  CHECK_EQ(battle_init(st, s), BR_OK);

  // ...and so must the same Pebble one stage further on, still carrying the
  // base stage's kit: species 3 is stage 2 of that same family.
  mk_setup(s, 7u, TEAM_A, 3u, TEAM_B, 3u, 10u);
  s.member[0][0].species_id = 3u;
  s.member[0][0].hp_cur     = xp_hp_max(species_get(3u)->base_hp, 10u);
  give_learnset(s.member[0][0], 1u);
  CHECK_EQ(battle_init(st, s), BR_OK);

  // AND THE CONTROL AGAIN, after all of it.
  mk_setup(s, 7u, TEAM_A, 3u, TEAM_B, 3u, 10u);
  CHECK_EQ(battle_init(st, s), BR_OK);
}

TEST(a_one_pebble_team_is_legal_and_a_short_team_leaves_the_rest_absent) {
  BattleSetup s;
  BattleState st;
  mk_setup(s, 7u, TEAM_A, 1u, TEAM_B, 2u, 10u);
  CHECK_EQ(battle_init(st, s), BR_OK);
  CHECK_EQ(battle_alive_count(st, 0u), 1);
  CHECK_EQ(battle_alive_count(st, 1u), 2);
  CHECK_EQ(battle_combatant(st, 0u, 1u)->flags, 0);
  CHECK_EQ(battle_combatant(st, 1u, 2u)->flags, 0);
}

// =============================================================================
//  2. INVALID ACTIONS (spec section 50 "invalid moves").
//     Exact code, bytes untouched, positive control that moves the bytes.
// =============================================================================
TEST(an_unknown_move_is_refused_for_an_empty_slot_and_for_an_id_past_the_table) {
  BattleState st;
  fixture(st);
  // The empty move slot. moves[] is 0 only where a learnset left it 0, so the
  // case has to make it so - and it is the ONLY illegal thing here.
  st.side[0].team[0].moves[2] = 0u;
  refuses(st, 0u, ACT(BACT_ATTACK, 2u), BR_UNKNOWN_MOVE);
  // The id past the table, which is a DIFFERENT guard on the same line.
  st.side[0].team[0].moves[3] = (uint8_t)(ATTACK_COUNT + 7u);
  refuses(st, 0u, ACT(BACT_ATTACK, 3u), BR_UNKNOWN_MOVE);
  // POSITIVE CONTROL: slot 0 is untouched and legal.
  accepts(st, 0u, ACT(BACT_ATTACK, 0u));
}

TEST(a_move_on_cooldown_is_refused) {
  BattleState st;
  fixture(st);
  st.side[1].team[0].cooldown[1] = 2u;
  refuses(st, 1u, ACT(BACT_ATTACK, 1u), BR_MOVE_ON_COOLDOWN);
  // POSITIVE CONTROL: the same move once the cooldown is gone.
  st.side[1].team[0].cooldown[1] = 0u;
  accepts(st, 1u, ACT(BACT_ATTACK, 1u));
}

TEST(switching_to_the_active_slot_is_refused) {
  BattleState st;
  fixture(st);
  refuses(st, 0u, ACT(BACT_SWITCH, st.side[0].active), BR_SWITCH_TO_SELF);
  accepts(st, 0u, ACT(BACT_SWITCH, 1u));
}

TEST(switching_to_an_empty_slot_is_refused) {
  BattleSetup s;
  BattleState st;
  mk_setup(s, 5u, TEAM_A, 2u, TEAM_B, 3u, 10u);   // slot 2 of side A holds nobody
  CHECK_EQ(battle_init(st, s), BR_OK);
  refuses(st, 0u, ACT(BACT_SWITCH, 2u), BR_SWITCH_TO_EMPTY);
  accepts(st, 0u, ACT(BACT_SWITCH, 1u));
}

TEST(switching_to_a_fainted_slot_is_refused) {
  BattleState st;
  fixture(st);
  st.side[0].team[2].hp_cur = 0u;
  battle_s6_process_fainting(st, nullptr);
  CHECK_EQ(st.side[0].team[2].flags & BCF_FAINTED, BCF_FAINTED);
  refuses(st, 0u, ACT(BACT_SWITCH, 2u), BR_SWITCH_TO_FAINTED);
  accepts(st, 0u, ACT(BACT_SWITCH, 1u));
}

TEST(acting_for_a_fainted_pebble_is_refused_and_only_a_switch_is_left) {
  BattleState st;
  fixture(st);
  st.side[0].team[0].hp_cur = 0u;
  battle_s6_process_fainting(st, nullptr);
  CHECK(battle_side_must_switch(st, 0u));
  refuses(st, 0u, ACT(BACT_ATTACK, 0u), BR_MUST_SWITCH);
  refuses(st, 0u, ACT(BACT_ATTACK, 3u), BR_MUST_SWITCH);
  // The other side is untouched and may still attack: the rule is per side.
  accepts(st, 1u, ACT(BACT_ATTACK, 0u));
  // POSITIVE CONTROL for the fainted side: the forced switch is accepted.
  accepts(st, 0u, ACT(BACT_SWITCH, 1u));
}

TEST(a_fainted_active_with_nothing_to_switch_to_is_a_named_refusal) {
  // CONSTRUCTED BY HAND, and it says so: battle_init() never produces a side
  // whose whole team is down, and step 9 ends the battle the moment one is. The
  // guard exists for a state arriving over the wire, which is defence in depth,
  // not a path the local engine can reach.
  BattleState st;
  fixture(st);
  for (uint8_t i = 0; i < 3u; ++i) st.side[0].team[i].hp_cur = 0u;
  battle_s6_process_fainting(st, nullptr);
  CHECK(!battle_side_must_switch(st, 0u));
  refuses(st, 0u, ACT(BACT_ATTACK, 0u), BR_ACTOR_FAINTED);
  refuses(st, 0u, ACT(BACT_SWITCH, 1u), BR_ACTOR_FAINTED);
  // POSITIVE CONTROL: the OTHER side, on the same state, is still fine.
  accepts(st, 1u, ACT(BACT_ATTACK, 0u));
}

TEST(an_empty_active_slot_is_a_named_refusal) {
  // ALSO CONSTRUCTED BY HAND. battle_init() never leaves the active slot empty,
  // so this guard is for wire-supplied state as well.
  BattleState st;
  fixture(st);
  st.side[1].active = 2u;
  st.side[1].team[2].flags = 0u;
  refuses(st, 1u, ACT(BACT_ATTACK, 0u), BR_EMPTY_ACTIVE);
  refuses(st, 1u, ACT(BACT_SWITCH, 0u), BR_EMPTY_ACTIVE);
  st.side[1].team[2].flags = BCF_PRESENT;
  accepts(st, 1u, ACT(BACT_ATTACK, 0u));
}

TEST(a_second_action_in_one_round_is_refused_and_the_first_one_stands) {
  BattleState st;
  fixture(st);
  accepts(st, 0u, ACT(BACT_ATTACK, 1u));
  refuses(st, 0u, ACT(BACT_ATTACK, 2u), BR_ALREADY_SUBMITTED);
  refuses(st, 0u, ACT(BACT_SWITCH, 1u), BR_ALREADY_SUBMITTED);
  CHECK_EQ(st.side[0].pending_kind, BACT_ATTACK);
  CHECK_EQ(st.side[0].pending_index, 1);
  // POSITIVE CONTROL: the other side has not submitted, so it still may.
  accepts(st, 1u, ACT(BACT_ATTACK, 0u));
}

TEST(acting_before_init_or_after_the_battle_is_over_is_refused) {
  BattleState st;
  memset(&st, 0, sizeof st);                 // BP_INIT
  refuses(st, 0u, ACT(BACT_ATTACK, 0u), BR_NOT_RUNNING);

  fixture(st);
  accepts(st, 0u, ACT(BACT_ATTACK, 0u));     // control: running, so it works
  st.side[0].pending_kind = (uint8_t)BACT_NONE;
  st.outcome = (uint8_t)BO_WIN_A;
  refuses(st, 0u, ACT(BACT_ATTACK, 0u), BR_NOT_RUNNING);
  st.outcome = (uint8_t)BO_UNDECIDED;
  accepts(st, 0u, ACT(BACT_ATTACK, 0u));
}

TEST(garbage_bytes_are_refused_by_the_guard_that_names_them) {
  BattleState st;
  fixture(st);
  refuses(st, 200u, ACT(BACT_ATTACK, 0u), BR_BAD_SIDE);
  refuses(st, 2u,   ACT(BACT_ATTACK, 0u), BR_BAD_SIDE);
  refuses(st, 0u,   ACT(200u, 0u),        BR_BAD_KIND);
  refuses(st, 0u,   ACT(BACT_NONE, 0u),   BR_BAD_KIND);
  refuses(st, 0u,   ACT(BACT_KIND_COUNT, 0u), BR_BAD_KIND);
  refuses(st, 0u,   ACT(BACT_ATTACK, (uint8_t)PB_MOVE_COUNT), BR_BAD_INDEX);
  refuses(st, 0u,   ACT(BACT_ATTACK, 200u), BR_BAD_INDEX);
  refuses(st, 0u,   ACT(BACT_SWITCH, (uint8_t)BATTLE_TEAM_MAX), BR_BAD_INDEX);
  refuses(st, 0u,   ACT(BACT_SWITCH, 200u), BR_BAD_INDEX);
  accepts(st, 0u,   ACT(BACT_ATTACK, 0u));
}

TEST(every_one_of_the_sixty_five_thousand_action_bit_patterns_is_refused_or_legal) {
  // The action is two untrusted bytes. This sweeps ALL of them on a real
  // battle, on both sides, and requires that the state does not move for any
  // refusal - which is also the proof that the const validator never draws.
  BattleState st;
  fixture(st);
  BattleState before;
  memcpy(&before, &st, sizeof before);
  const uint32_t h0 = battle_state_hash(st);

  long accepted = 0, refused = 0;
  int bad_code = 0;
  for (unsigned side = 0; side < 4u; ++side) {
    for (unsigned k = 0; k < 256u; ++k) {
      for (unsigned idx = 0; idx < 256u; ++idx) {
        const BattleReject r =
            battle_validate_action(st, (uint8_t)side, ACT((uint8_t)k, (uint8_t)idx));
        if (r >= BR_REJECT_COUNT) bad_code++;
        if (r == BR_OK) accepted++; else refused++;
      }
    }
  }
  CHECK_EQ(bad_code, 0);
  // Exactly the legal ones: side 0 and 1, ATTACK x 4 slots + SWITCH to the two
  // benched slots = 6 per side.
  CHECK_EQ(accepted, 12);
  CHECK_EQ(refused, 4L * 256L * 256L - 12L);
  CHECK(memcmp(&before, &st, sizeof st) == 0);
  CHECK_EQ(battle_state_hash(st), h0);
}

TEST(a_round_without_both_actions_leaves_the_state_bit_identical) {
  BattleState st;
  fixture(st);
  BattleState before;
  memcpy(&before, &st, sizeof before);
  CHECK_EQ(battle_step_round(st, nullptr), BS_NEED_ACTIONS);
  CHECK(memcmp(&before, &st, sizeof st) == 0);

  accepts(st, 0u, ACT(BACT_ATTACK, 0u));
  memcpy(&before, &st, sizeof before);
  CHECK_EQ(battle_step_round(st, nullptr), BS_NEED_ACTIONS);   // still only one
  CHECK(memcmp(&before, &st, sizeof st) == 0);
}

TEST(a_state_that_moved_between_submit_and_resolve_aborts_the_battle) {
  // REACHABLE FOR A REAL REASON: P4-C5 validates a peer's action against the
  // LOCAL state, and a desynced peer produces exactly an action that was legal
  // there and is illegal here. battle_step_round() must refuse to resolve it.
  BattleState st;
  fixture(st);
  CHECK_EQ(battle_submit_action(st, 0u, ACT(BACT_ATTACK, 1u)), BR_OK);
  CHECK_EQ(battle_submit_action(st, 1u, ACT(BACT_ATTACK, 0u)), BR_OK);
  st.side[0].team[0].cooldown[1] = 3u;             // the state moved underneath
  CHECK_EQ(battle_step_round(st, nullptr), BS_BATTLE_OVER);
  CHECK_EQ(st.outcome, BO_ABORT);
  CHECK_EQ(st.round, 1);                            // and the round did not run
}

// =============================================================================
//  3. THE ROUND
// =============================================================================
// Drives ONE step in isolation. Writing the two pending bytes directly is what
// battle_submit_action() would have written; it is done here so a case can call
// step 4 several times over one fixture without running whole rounds.
static void set_pending(BattleState& st, uint8_t side, uint8_t kind, uint8_t index)
{
  st.side[side].pending_kind  = kind;
  st.side[side].pending_index = index;
}

// The controlled duel used by every arithmetic case: equal defences, a speed
// gap wide enough that no evasion penalty applies to side 0's attacks, and a
// move list chosen per case.
static void duel(BattleState& st, uint8_t a_type, uint8_t b_type,
                 uint8_t atk = 10u, uint8_t def = 5u, uint16_t hp = 400u)
{
  fixture(st);
  arm(st.side[0].team[0], a_type, atk, def, 9u, hp,
      MV_PING, MV_CHOQUE, MV_RAFAGA, MV_AMPLIFICAR);
  arm(st.side[1].team[0], b_type, atk, def, 9u, hp,
      MV_PING, MV_CHOQUE, MV_RAFAGA, MV_AMPLIFICAR);
}

TEST(damage_is_the_published_integer_formula_and_the_roll_lands_after_the_type_multiply) {
  BattleState st;
  duel(st, TYPE_SIGNAL, TYPE_CORRUPT);
  const uint16_t hp0 = st.side[1].team[0].hp_cur;

  // NEUTRAL move: raw = 50*10 / (5*7) = 500/35 = 14, m = 0, roll 0.
  rng_init(st.rng, SEED_HIT_ROLL0);
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 1u);           // MV_CHOQUE
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK_EQ(st.side[1].team[0].hp_cur, hp0 - 14);

  // SIGNAL move into CORRUPT: raw = 35*10/35 = 10, x5/4 = 12, roll 0.
  st.side[1].team[0].hp_cur = hp0;
  st.side[0].team[0].type_edge_left = (uint8_t)TYPE_MOD_MAX_HITS;
  rng_init(st.rng, SEED_HIT_ROLL0);
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 0u);           // MV_PING
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK_EQ(st.side[1].team[0].hp_cur, hp0 - 12);

  // THE ROLL IS ADDED AFTER THE MULTIPLY, and roll 2 is what separates the two
  // readings: 12 + 2 = 14, whereas multiplying (10 + 2) would give 15.
  st.side[1].team[0].hp_cur = hp0;
  st.side[0].team[0].type_edge_left = (uint8_t)TYPE_MOD_MAX_HITS;
  rng_init(st.rng, SEED_HIT_ROLL2);
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 0u);
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK_EQ(st.side[1].team[0].hp_cur, hp0 - 14);
}

TEST(damage_never_falls_below_one_and_never_wraps_past_zero) {
  BattleState st;
  duel(st, TYPE_SIGNAL, TYPE_SIGNAL, 1u, 200u, 3u);   // atk 1 against def 200
  rng_init(st.rng, SEED_HIT_ROLL0);
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 1u);
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK_EQ(st.side[1].team[0].hp_cur, 2);             // exactly DMG_MIN

  // THE SECOND FLOOR, WHICH THIS CASE USED TO MISS. data/balance.h publishes two
  // DMG_MIN floors - one on the raw term and one AFTER the type multiply - and
  // the case above reaches only a NEUTRAL matchup, where TYPE_MUL is 1/1 and the
  // two are indistinguishable. So each floor was masked by the other: deleting
  // EITHER one alone left the whole suite green, and only deleting both was ever
  // caught. A DISADVANTAGE is what separates them: raw 1 times 4/5 is 0 in
  // integers, and without the post-multiply floor a disadvantaged minimum hit
  // deals literally nothing. Asserted on the shared function directly...
  duel(st, TYPE_SIGNAL, TYPE_SYSTEM, 1u, 200u, 400u);
  const BattleCombatant& du = st.side[0].team[0];
  const BattleCombatant& df = st.side[1].team[0];
  const AttackDef* ping = attack_get(MV_PING);
  CHECK(ping != nullptr);
  if (ping != nullptr) {
    CHECK_EQ(battle_damage_pre_roll(du, df, *ping, 0), 1);     // the neutral reading
    CHECK_EQ(battle_damage_pre_roll(du, df, *ping, -1), 1);    // and the disadvantaged one
  }
  // ...and end to end, where MV_PING is SIGNAL into a SYSTEM defender, so the
  // engine decides the -1 itself and spends the type edge on it.
  const uint16_t hp_d = st.side[1].team[0].hp_cur;
  rng_init(st.rng, SEED_HIT_ROLL0);
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 0u);
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK_EQ(st.side[0].team[0].type_edge_left, 0);     // the -1 really was applied
  CHECK_EQ(hp_d - st.side[1].team[0].hp_cur, 1);

  st.side[1].team[0].hp_cur = 1u;
  duel(st, TYPE_SIGNAL, TYPE_CORRUPT, 30u, 1u, 4u);   // a hit far bigger than hp
  rng_init(st.rng, SEED_HIT_ROLL2);
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 1u);
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK_EQ(st.side[1].team[0].hp_cur, 0);             // saturating, not wrapped
}

TEST(type_advantage_is_five_quarters_and_disadvantage_four_fifths) {
  BattleState st;
  duel(st, TYPE_SIGNAL, TYPE_CORRUPT);
  const uint16_t hp0 = st.side[1].team[0].hp_cur;
  rng_init(st.rng, SEED_HIT_ROLL0);
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 0u);
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK_EQ(hp0 - st.side[1].team[0].hp_cur, 12);      // raw 10 x 5/4

  duel(st, TYPE_SIGNAL, TYPE_SYSTEM);
  rng_init(st.rng, SEED_HIT_ROLL0);
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 0u);
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK_EQ(hp0 - st.side[1].team[0].hp_cur, 8);       // raw 10 x 4/5

  // A NEUTRAL-typed attack never indexes the chart, whatever it is aimed at.
  duel(st, TYPE_SIGNAL, TYPE_CORRUPT);
  rng_init(st.rng, SEED_HIT_ROLL0);
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 1u);      // MV_CHOQUE is NEUTRAL
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK_EQ(st.side[0].team[0].type_edge_left, TYPE_MOD_MAX_HITS);   // nothing spent
}

TEST(the_type_edge_is_capped_per_hit_and_the_cap_zeroes_a_disadvantage_too) {
  BattleState st;
  duel(st, TYPE_SIGNAL, TYPE_CORRUPT);
  const uint16_t hp0 = st.side[1].team[0].hp_cur;

  // Hit 1 at x5/4 and hit 2 at x1, asserted SEPARATELY and by exact damage: a
  // case that only checked "advantaged total > neutral total" would pass with
  // the cap broken.
  rng_init(st.rng, SEED_HIT_ROLL0);
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 0u);
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK_EQ(hp0 - st.side[1].team[0].hp_cur, 12);
  CHECK_EQ(st.side[0].team[0].type_edge_left, 0);

  const uint16_t hp1 = st.side[1].team[0].hp_cur;
  rng_init(st.rng, SEED_HIT_ROLL0);
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 0u);
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK_EQ(hp1 - st.side[1].team[0].hp_cur, 10);      // the edge is gone: x1

  // The cap zeroes a DISADVANTAGE as well, so the second hit does MORE damage.
  duel(st, TYPE_SIGNAL, TYPE_SYSTEM);
  const uint16_t hp2 = st.side[1].team[0].hp_cur;
  rng_init(st.rng, SEED_HIT_ROLL0);
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 0u);
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK_EQ(hp2 - st.side[1].team[0].hp_cur, 8);
  const uint16_t hp3 = st.side[1].team[0].hp_cur;
  rng_init(st.rng, SEED_HIT_ROLL0);
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 0u);
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK_EQ(hp3 - st.side[1].team[0].hp_cur, 10);
}

TEST(a_miss_and_a_power_zero_move_do_not_spend_the_type_edge) {
  BattleState st;
  duel(st, TYPE_SIGNAL, TYPE_CORRUPT);

  // A MISS: MV_RAFAGA is accuracy 85 and the seeded accuracy roll is 99.
  rng_init(st.rng, SEED_MISS_85);
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 2u);
  const uint16_t hp0 = st.side[1].team[0].hp_cur;
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK_EQ(st.side[1].team[0].hp_cur, hp0);                        // it missed
  CHECK_EQ(st.side[0].team[0].type_edge_left, TYPE_MOD_MAX_HITS);  // and paid nothing

  // A POWER-0 advantaged-type move (MV_AMPLIFICAR is SIGNAL) spends nothing.
  rng_init(st.rng, SEED_HIT_ROLL0);
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 3u);
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK_EQ(st.side[0].team[0].stage[BSTAT_ATK], 1);                // it landed
  CHECK_EQ(st.side[0].team[0].type_edge_left, TYPE_MOD_MAX_HITS);

  // ...and the edge really was still there: the next damaging hit is x5/4.
  const uint16_t hp1 = st.side[1].team[0].hp_cur;
  st.side[0].team[0].stage[BSTAT_ATK]      = 0;     // undo the buff so the
  st.side[0].team[0].stage_left[BSTAT_ATK] = 0u;    // arithmetic stays raw 10
  rng_init(st.rng, SEED_HIT_ROLL0);
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 0u);
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK_EQ(hp1 - st.side[1].team[0].hp_cur, 12);
}

TEST(the_type_edge_belongs_to_the_combatant_and_survives_a_round_trip_to_the_bench) {
  BattleState st;
  duel(st, TYPE_SIGNAL, TYPE_CORRUPT);
  arm(st.side[0].team[1], TYPE_SIGNAL, 10u, 5u, 9u, 400u,
      MV_PING, MV_CHOQUE, MV_RAFAGA, MV_AMPLIFICAR);

  rng_init(st.rng, SEED_HIT_ROLL0);
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 0u);
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK_EQ(st.side[0].team[0].type_edge_left, 0);
  CHECK_EQ(st.side[0].team[1].type_edge_left, TYPE_MOD_MAX_HITS);   // the TEAMMATE keeps its own

  set_pending(st, 0u, (uint8_t)BACT_SWITCH, 1u);
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK_EQ(st.side[0].active, 1);
  set_pending(st, 0u, (uint8_t)BACT_SWITCH, 0u);
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK_EQ(st.side[0].active, 0);
  CHECK_EQ(st.side[0].team[0].type_edge_left, 0);                   // still spent
  CHECK_EQ(st.side[0].team[1].type_edge_left, TYPE_MOD_MAX_HITS);
}

TEST(protection_halves_after_the_roll_and_never_below_one) {
  BattleState st;
  duel(st, TYPE_SIGNAL, TYPE_CORRUPT);
  const uint16_t hp0 = st.side[1].team[0].hp_cur;

  // roll 2: applying PROTECT_DIVISOR after the roll gives (14 + 2) / 2 = 8,
  // before it gives 7 + 2 = 9. The two readings differ, which is the point.
  st.side[1].team[0].protect_left = 1u;
  rng_init(st.rng, SEED_HIT_ROLL2);
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 1u);
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK_EQ(hp0 - st.side[1].team[0].hp_cur, 8);

  // Unprotected, the identical hit costs the full 16.
  st.side[1].team[0].hp_cur      = hp0;
  st.side[1].team[0].protect_left = 0u;
  rng_init(st.rng, SEED_HIT_ROLL2);
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 1u);
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK_EQ(hp0 - st.side[1].team[0].hp_cur, 16);

  // "NEVER BELOW ONE" - the half of this test's own NAME that had no case at
  // all until the P4-C2/C3 review, so the DMG_MIN floor inside the protection
  // branch could be deleted with the entire suite still green. atk 1 into def
  // 200 gives raw 1, the NEUTRAL move keeps it at 1, roll 0 leaves 1 to halve,
  // and 1 / PROTECT_DIVISOR is 0 in integers: the floor is the only thing
  // between a protected Pebble and a hit that costs nothing.
  duel(st, TYPE_SIGNAL, TYPE_SIGNAL, 1u, 200u, 400u);
  st.side[1].team[0].protect_left = 1u;
  const uint16_t hp_min = st.side[1].team[0].hp_cur;
  rng_init(st.rng, SEED_HIT_ROLL0);
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 1u);
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK_EQ(hp_min - st.side[1].team[0].hp_cur, 1);
}

TEST(protection_covers_the_round_it_was_raised_and_is_gone_the_next) {
  BattleState st;
  fixture(st);
  // Side 0 raises the shield in round 1 and does something else harmless in
  // round 2; BOTH of its moves are power 0, so the two rounds consume the same
  // draws in the same order and re-seeding makes the incoming hits comparable.
  arm(st.side[0].team[0], TYPE_SYSTEM, 10u, 5u, 9u, 400u,
      MV_FIREWALL, MV_AMPLIFICAR, MV_PING, MV_ANTENA);
  arm(st.side[1].team[0], TYPE_CORRUPT, 10u, 5u, 4u, 400u,
      MV_CHOQUE, MV_PING, MV_RAFAGA, MV_AMPLIFICAR);

  // MV_FIREWALL is priority +2, so it resolves before the attack it blunts.
  const uint16_t hp0 = st.side[0].team[0].hp_cur;
  rng_init(st.rng, SEED_HIT_ROLL0);
  CHECK_EQ(round_with(st, ACT(BACT_ATTACK, 0u), ACT(BACT_ATTACK, 0u), nullptr),
           BS_ROUND_DONE);
  const uint16_t d_shielded = (uint16_t)(hp0 - st.side[0].team[0].hp_cur);
  CHECK_EQ(st.side[0].team[0].protect_left, 0);       // expired in step 7

  // THE OTHER BOUNDARY: the very next round, same seed and same draw order, the
  // identical hit costs the full amount.
  const uint16_t hp1 = st.side[0].team[0].hp_cur;
  rng_init(st.rng, SEED_HIT_ROLL0);
  CHECK_EQ(round_with(st, ACT(BACT_ATTACK, 1u), ACT(BACT_ATTACK, 0u), nullptr),
           BS_ROUND_DONE);
  const uint16_t d_bare = (uint16_t)(hp1 - st.side[0].team[0].hp_cur);
  // raw = 50*10 / (5*7) = 14, and seed 1's THIRD draw (the two accuracy rolls
  // come first) is 1, so the bare hit is 15. AN ODD TOTAL IS WHAT MAKES THIS
  // CASE SHARP: halving after the roll gives 15/2 = 7, halving before it gives
  // 7 + 1 = 8, so the two readings of PROTECT_DIVISOR disagree here.
  CHECK_EQ(d_bare, 15);
  CHECK_EQ(d_shielded, 7);
  CHECK_EQ(d_shielded, d_bare / PROTECT_DIVISOR);
}

TEST(a_buff_holds_on_its_last_active_round_and_is_gone_on_the_first_inactive_one) {
  BattleState st;
  fixture(st);
  // Side 1 only ever uses MV_AMPLIFICAR on ITSELF, so it never damages side 0
  // and never changes side 0's damage output: only the buff duration is on
  // trial. Side 0's slots: 0 buff, 1 a harmless DEF buff, 2 the damaging probe.
  arm(st.side[0].team[0], TYPE_SIGNAL, 10u, 5u, 9u, 400u,
      MV_AMPLIFICAR, MV_ANTENA, MV_CHOQUE, MV_PING);
  arm(st.side[1].team[0], TYPE_CORRUPT, 10u, 5u, 4u, 400u,
      MV_AMPLIFICAR, MV_ANTENA, MV_CHOQUE, MV_PING);
  const uint16_t base = battle_stat_eff(st.side[0].team[0], (uint8_t)BSTAT_ATK);
  CHECK_EQ(base, 10);

  rng_init(st.rng, SEED_HIT_ROLL0);
  round_with(st, ACT(BACT_ATTACK, 0u), ACT(BACT_ATTACK, 0u), nullptr);   // 1: buff
  CHECK_EQ(battle_stat_eff(st.side[0].team[0], (uint8_t)BSTAT_ATK), base + 1);
  CHECK_EQ(st.side[0].team[0].stage_left[BSTAT_ATK], 2);

  round_with(st, ACT(BACT_ATTACK, 1u), ACT(BACT_ATTACK, 0u), nullptr);   // 2
  CHECK_EQ(battle_stat_eff(st.side[0].team[0], (uint8_t)BSTAT_ATK), base + 1);
  CHECK_EQ(st.side[0].team[0].stage_left[BSTAT_ATK], 1);

  // ROUND 3 IS THE LAST ROUND THE BUFF ACTS IN, and the claim is made in
  // DAMAGE, not in a field read: atk 11 gives 50*11/(5*7) = 15.
  const uint16_t hp_a = st.side[1].team[0].hp_cur;
  rng_init(st.rng, SEED_HIT_ROLL0);
  round_with(st, ACT(BACT_ATTACK, 2u), ACT(BACT_ATTACK, 0u), nullptr);   // 3
  CHECK_EQ(hp_a - st.side[1].team[0].hp_cur, 15);
  CHECK_EQ(st.side[0].team[0].stage_left[BSTAT_ATK], 0);
  CHECK_EQ(st.side[0].team[0].stage[BSTAT_ATK], 0);
  CHECK_EQ(battle_stat_eff(st.side[0].team[0], (uint8_t)BSTAT_ATK), base);

  // ROUND 4 IS THE FIRST ROUND IT DOES NOT: the identical hit, from the
  // identical seed and the identical draw order, is 50*10/(5*7) = 14.
  const uint16_t hp_b = st.side[1].team[0].hp_cur;
  rng_init(st.rng, SEED_HIT_ROLL0);
  round_with(st, ACT(BACT_ATTACK, 2u), ACT(BACT_ATTACK, 0u), nullptr);   // 4
  CHECK_EQ(hp_b - st.side[1].team[0].hp_cur, 14);
}

TEST(a_buff_stage_clamps_at_two_and_a_debuff_can_never_wrap_the_stat) {
  BattleState st;
  // BASE 10, NOT 3, AND THE DIFFERENCE IS THE WHOLE DEBUFF ARM. At base 3,
  // 3 + BUFF_STAGE_MIN is 1, which is also STAT_EFF_MIN - so the assertion
  // below was satisfied by the LATER floor and the clamp it names was never
  // reached: deleting the BUFF_STAGE_MIN half of battle_stat_eff's clamp left
  // ALL PASS 27/27, while deleting the BUFF_STAGE_MAX half on the next line was
  // caught. At base 10 the clamp answers 8 and the floor would answer 1.
  duel(st, TYPE_SIGNAL, TYPE_CORRUPT, 10u, 10u);
  BattleCombatant& c = st.side[0].team[0];
  c.stage[BSTAT_ATK] = 5;    c.stage_left[BSTAT_ATK] = 3u;   // past the clamp
  CHECK_EQ(battle_stat_eff(c, (uint8_t)BSTAT_ATK), 10 + BUFF_STAGE_MAX);
  c.stage[BSTAT_ATK] = -9;
  CHECK_EQ(battle_stat_eff(c, (uint8_t)BSTAT_ATK), 10 + BUFF_STAGE_MIN);
  CHECK(10 + BUFF_STAGE_MIN > STAT_EFF_MIN);   // the two readings really do differ
  // The unsigned-wrap failure data/balance.h names: a stat of 1 with a -2 stage
  // must be STAT_EFF_MIN, never 65,535.
  c.atk = 1u;
  CHECK_EQ(battle_stat_eff(c, (uint8_t)BSTAT_ATK), STAT_EFF_MIN);
}

TEST(corruption_rides_outside_the_stage_clamp_and_expires_on_its_own_counter) {
  BattleState st;
  duel(st, TYPE_SIGNAL, TYPE_CORRUPT, 10u, 10u);
  BattleCombatant& c = st.side[0].team[0];
  c.stage[BSTAT_ATK] = (int8_t)BUFF_STAGE_MAX;   c.stage_left[BSTAT_ATK] = 5u;
  c.stage[BSTAT_DEF] = (int8_t)BUFF_STAGE_MIN;   c.stage_left[BSTAT_DEF] = 5u;
  CHECK_EQ(battle_stat_eff(c, (uint8_t)BSTAT_ATK), 10 + BUFF_STAGE_MAX);
  CHECK_EQ(battle_stat_eff(c, (uint8_t)BSTAT_DEF), 10 + BUFF_STAGE_MIN);
  c.corrupt_left = 3u;
  // +3 and -3: the corruption term is applied AFTER the +-2 clamp, which is
  // what sim_engine.py's Fighter.eff() does and is not a clamp bug.
  CHECK_EQ(battle_stat_eff(c, (uint8_t)BSTAT_ATK), 10 + BUFF_STAGE_MAX + CORRUPT_BATTLE_ATK_STAGE);
  CHECK_EQ(battle_stat_eff(c, (uint8_t)BSTAT_DEF), 10 + BUFF_STAGE_MIN + CORRUPT_BATTLE_DEF_STAGE);
  c.corrupt_left = 0u;
  CHECK_EQ(battle_stat_eff(c, (uint8_t)BSTAT_ATK), 10 + BUFF_STAGE_MAX);
}

TEST(a_dot_ticks_on_every_status_step_and_stops_on_the_round_after_its_last) {
  BattleState st;
  duel(st, TYPE_SIGNAL, TYPE_CORRUPT);
  BattleCombatant& c = st.side[1].team[0];
  c.dot_value = 5u;
  c.dot_left  = 2u;
  const uint16_t hp0 = c.hp_cur;
  battle_s7_process_status(st, nullptr);
  CHECK_EQ(c.hp_cur, hp0 - 5);
  CHECK_EQ(c.dot_left, 1);
  battle_s7_process_status(st, nullptr);
  CHECK_EQ(c.hp_cur, hp0 - 10);
  CHECK_EQ(c.dot_left, 0);
  CHECK_EQ(c.dot_value, 0);
  battle_s7_process_status(st, nullptr);          // THE OTHER BOUNDARY
  CHECK_EQ(c.hp_cur, hp0 - 10);
}

TEST(a_dot_kills_on_the_status_tick_and_the_second_faint_pass_is_what_sees_it) {
  BattleSetup s;
  BattleState st;
  mk_setup(s, 3u, TEAM_A, 3u, TEAM_B, 1u, 10u);          // side B has ONE Pebble
  CHECK_EQ(battle_init(st, s), BR_OK);
  arm(st.side[0].team[0], TYPE_SIGNAL, 10u, 5u, 9u, 400u,
      MV_AMPLIFICAR, MV_ANTENA, MV_CHOQUE, MV_PING);
  arm(st.side[1].team[0], TYPE_CORRUPT, 10u, 5u, 4u, 400u,
      MV_AMPLIFICAR, MV_ANTENA, MV_CHOQUE, MV_PING);
  st.side[1].team[0].hp_cur   = 5u;                       // exactly the DOT
  st.side[1].team[0].dot_value = 5u;
  st.side[1].team[0].dot_left  = 3u;

  BattleEvent buf[64];
  BattleLog log;
  battle_log_init(log, buf, 64u);
  rng_init(st.rng, SEED_HIT_ROLL0);
  // Both sides use a power-0 move, so nothing but the DOT can do the killing.
  CHECK_EQ(round_with(st, ACT(BACT_ATTACK, 0u), ACT(BACT_ATTACK, 0u), &log), BS_BATTLE_OVER);

  CHECK_EQ(st.side[1].team[0].hp_cur, 0);
  CHECK_EQ(st.side[1].team[0].flags & BCF_FAINTED, BCF_FAINTED);   // step 8's SECOND s6 call
  CHECK_EQ(st.outcome, BO_WIN_A);
  int faints = 0;
  for (uint16_t i = 0; i < log.count; ++i) {
    const BattleEvent* e = battle_log_at(log, i);
    if (e && e->kind == (uint8_t)RLE_FAINT && e->side == 1u) faints++;
  }
  CHECK_EQ(faints, 1);
}

TEST(step_six_is_idempotent_which_is_the_property_step_eight_leans_on) {
  BattleState st;
  fixture(st);
  st.side[0].team[1].hp_cur = 0u;
  battle_s6_process_fainting(st, nullptr);
  const uint32_t h = battle_state_hash(st);
  battle_s6_process_fainting(st, nullptr);
  CHECK_EQ(battle_state_hash(st), h);
  battle_s6_process_fainting(st, nullptr);
  CHECK_EQ(battle_state_hash(st), h);
}

TEST(a_faster_pebble_acts_first_and_a_tie_is_broken_by_the_battle_rng) {
  BattleState st;
  duel(st, TYPE_SIGNAL, TYPE_CORRUPT);
  st.side[0].team[0].spd = 9u;
  st.side[1].team[0].spd = 4u;
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 1u);
  set_pending(st, 1u, (uint8_t)BACT_ATTACK, 1u);
  uint32_t cursor = st.rng.s;
  CHECK_EQ(battle_s3_determine_order(st, 0, 0), 0);
  CHECK_EQ(st.rng.s, cursor);                 // NO DRAW when speeds differ

  st.side[0].team[0].spd = 3u;
  CHECK_EQ(battle_s3_determine_order(st, 0, 0), 1);
  CHECK_EQ(st.rng.s, cursor);

  // A tie, and only a tie, costs one draw - and the draw decides.
  st.side[0].team[0].spd = 4u;
  rng_init(st.rng, 1u);                        // rng_next_below(2) == 0
  cursor = st.rng.s;
  CHECK_EQ(battle_s3_determine_order(st, 0, 0), 0);
  CHECK(st.rng.s != cursor);
  rng_init(st.rng, 8192u);                     // rng_next_below(2) == 1
  CHECK_EQ(battle_s3_determine_order(st, 0, 0), 1);

  // A stage moves effective speed, so it moves the order too.
  rng_init(st.rng, 1u);
  st.side[0].team[0].stage[BSTAT_SPD]      = 1;
  st.side[0].team[0].stage_left[BSTAT_SPD] = 3u;
  cursor = st.rng.s;
  CHECK_EQ(battle_s3_determine_order(st, 0, 0), 0);
  CHECK_EQ(st.rng.s, cursor);                  // no longer a tie: no draw
}

TEST(priority_beats_effective_speed_and_a_switch_outruns_every_attack) {
  BattleState st;
  duel(st, TYPE_SIGNAL, TYPE_CORRUPT);
  arm(st.side[1].team[0], TYPE_CORRUPT, 10u, 5u, 1u, 400u,
      MV_ADELANTO, MV_CHOQUE, MV_PING, MV_AMPLIFICAR);
  st.side[0].team[0].spd = 20u;

  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 1u);          // MV_CHOQUE, priority 0
  set_pending(st, 1u, (uint8_t)BACT_ATTACK, 0u);          // MV_ADELANTO, priority +1
  CHECK_EQ(battle_s2_priority(st, 0u), 0);
  CHECK_EQ(battle_s2_priority(st, 1u), 1);
  CHECK_EQ(battle_s3_determine_order(st, battle_s2_priority(st, 0u),
                                         battle_s2_priority(st, 1u)), 1);

  // The switch, which is the whole reason "switching costs the turn" means what
  // battle.h says it means.
  set_pending(st, 1u, (uint8_t)BACT_SWITCH, 1u);
  CHECK_EQ(battle_s2_priority(st, 1u), BATTLE_SWITCH_PRIORITY);
  CHECK_EQ(battle_s3_determine_order(st, battle_s2_priority(st, 0u),
                                         battle_s2_priority(st, 1u)), 1);
}

TEST(switching_costs_the_turn_and_the_incoming_pebble_is_the_one_that_is_hit) {
  BattleState st;
  duel(st, TYPE_SIGNAL, TYPE_CORRUPT);
  arm(st.side[1].team[1], TYPE_CORRUPT, 10u, 5u, 4u, 400u,
      MV_PING, MV_CHOQUE, MV_RAFAGA, MV_AMPLIFICAR);
  const uint16_t out_hp = st.side[1].team[0].hp_cur;
  const uint16_t in_hp  = st.side[1].team[1].hp_cur;
  const uint16_t a_hp   = st.side[0].team[0].hp_cur;

  rng_init(st.rng, SEED_HIT_ROLL0);
  CHECK_EQ(round_with(st, ACT(BACT_ATTACK, 1u), ACT(BACT_SWITCH, 1u), nullptr),
           BS_ROUND_DONE);

  CHECK_EQ(st.side[1].active, 1);
  CHECK_EQ(st.side[1].team[0].hp_cur, out_hp);          // the one that left is untouched
  CHECK_EQ(in_hp - st.side[1].team[1].hp_cur, 14);      // the one that arrived took it
  CHECK_EQ(st.side[0].team[0].hp_cur, a_hp);            // and the switcher dealt NOTHING
}

TEST(a_switch_clears_the_position_and_keeps_what_rides_the_creature) {
  BattleState st;
  duel(st, TYPE_SIGNAL, TYPE_CORRUPT);
  arm(st.side[0].team[1], TYPE_SIGNAL, 10u, 5u, 9u, 400u,
      MV_PING, MV_CHOQUE, MV_RAFAGA, MV_AMPLIFICAR);
  BattleCombatant& c = st.side[0].team[0];
  c.stage[BSTAT_ATK] = 2;  c.stage_left[BSTAT_ATK] = 3u;
  c.protect_left = 2u;
  c.stun_left    = 1u;
  c.dot_value    = 5u;  c.dot_left = 3u;
  c.corrupt_left = 2u;
  c.cooldown[2]  = 3u;
  c.type_edge_left = 0u;

  set_pending(st, 0u, (uint8_t)BACT_SWITCH, 1u);
  battle_s4_resolve_first(st, 0u, nullptr);
  // Belongs to the POSITION - cleared.
  CHECK_EQ(c.stage[BSTAT_ATK], 0);
  CHECK_EQ(c.stage_left[BSTAT_ATK], 0);
  CHECK_EQ(c.protect_left, 0);
  CHECK_EQ(c.stun_left, 0);
  // Rides the CREATURE - kept, because clearing it would make a switch a free
  // cure, a free cooldown refresh and a free second type edge.
  CHECK_EQ(c.dot_left, 3);
  CHECK_EQ(c.corrupt_left, 2);
  CHECK_EQ(c.cooldown[2], 3);
  CHECK_EQ(c.type_edge_left, 0);
}

TEST(a_pebble_that_walks_in_arrives_with_a_clean_position_of_its_own) {
  // THE OTHER HALF, and it needs its own case: the switch clears the position
  // on the way OUT and again on the way IN, and until this existed the outgoing
  // clear covered for the incoming one - a mutation that deleted the incoming
  // clear left every case green.
  BattleState st;
  duel(st, TYPE_SIGNAL, TYPE_CORRUPT);
  arm(st.side[0].team[1], TYPE_SIGNAL, 10u, 5u, 9u, 400u,
      MV_PING, MV_CHOQUE, MV_RAFAGA, MV_AMPLIFICAR);
  // A bench member carrying position state it should never bring on with it.
  BattleCombatant& in = st.side[0].team[1];
  in.stage[BSTAT_ATK] = 2;  in.stage_left[BSTAT_ATK] = 4u;
  in.stage[BSTAT_SPD] = -2; in.stage_left[BSTAT_SPD] = 4u;
  in.protect_left = 3u;
  in.stun_left    = 2u;
  in.dot_value    = 5u; in.dot_left = 2u;
  in.cooldown[2]  = 3u;

  set_pending(st, 0u, (uint8_t)BACT_SWITCH, 1u);
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK_EQ(st.side[0].active, 1);
  CHECK_EQ(in.stage[BSTAT_ATK], 0);
  CHECK_EQ(in.stage_left[BSTAT_ATK], 0);
  CHECK_EQ(in.stage[BSTAT_SPD], 0);
  CHECK_EQ(in.stage_left[BSTAT_SPD], 0);
  CHECK_EQ(in.protect_left, 0);
  CHECK_EQ(in.stun_left, 0);
  CHECK_EQ(battle_stat_eff(in, (uint8_t)BSTAT_ATK), 10);   // and it is really gone
  // ...while what rides the creature came with it.
  CHECK_EQ(in.dot_left, 2);
  CHECK_EQ(in.cooldown[2], 3);
}

TEST(fainting_forces_the_replacement_and_the_replacement_costs_the_turn) {
  BattleState st;
  duel(st, TYPE_SIGNAL, TYPE_CORRUPT);
  arm(st.side[1].team[1], TYPE_CORRUPT, 10u, 5u, 4u, 400u,
      MV_PING, MV_CHOQUE, MV_RAFAGA, MV_AMPLIFICAR);
  st.side[1].team[0].hp_cur = 3u;                    // one hit from gone
  st.side[0].team[0].spd = 20u;                      // and side 0 strikes first

  BattleEvent buf[128];
  BattleLog log;
  battle_log_init(log, buf, 128u);
  rng_init(st.rng, SEED_HIT_ROLL0);
  CHECK_EQ(round_with(st, ACT(BACT_ATTACK, 1u), ACT(BACT_ATTACK, 1u), &log), BS_ROUND_DONE);
  CHECK_EQ(st.side[1].team[0].hp_cur, 0);
  CHECK_EQ(st.side[1].team[0].flags & BCF_FAINTED, BCF_FAINTED);
  CHECK(battle_side_must_switch(st, 1u));
  CHECK_EQ(st.side[1].active, 0);                    // NO free replacement happened

  // Next round the fainted side may do exactly one thing.
  refuses(st, 1u, ACT(BACT_ATTACK, 1u), BR_MUST_SWITCH);
  const uint16_t a_hp  = st.side[0].team[0].hp_cur;
  const uint16_t in_hp = st.side[1].team[1].hp_cur;
  rng_init(st.rng, SEED_HIT_ROLL0);
  CHECK_EQ(round_with(st, ACT(BACT_ATTACK, 1u), ACT(BACT_SWITCH, 1u), &log), BS_ROUND_DONE);
  CHECK_EQ(st.side[1].active, 1);
  CHECK_EQ(in_hp - st.side[1].team[1].hp_cur, 14);   // the replacement ate the hit
  CHECK_EQ(st.side[0].team[0].hp_cur, a_hp);         // and answered with nothing
}

// =============================================================================
//  THE TYPE EDGE IS NOW VISIBLE, AND THIS IS WHAT MAKES THAT CLAIM CHECKABLE.
//  TYPE_MOD_MAX_HITS is 1 and it decides cross-type duels (measured: uncapped,
//  the off-diagonal cells win 69-78 % of the time; capped, 40-63 %). Until
//  RLE_TYPE_EDGE the log said nothing about which hit got it or that it was
//  spent, so the most consequential rule in a fight was one no player could see.
//  The rule did not change; only its reporting did, which is exactly what this
//  case pins - the EVENT COUNT, not the damage.
// =============================================================================
TEST(the_type_edge_is_announced_exactly_once_per_combatant_and_never_again) {
  BattleState st;
  duel(st, TYPE_SIGNAL, TYPE_CORRUPT);      // a real cross-type relation
  st.side[0].team[0].hp_cur = 400u;         // nobody dies inside the sample
  st.side[1].team[0].hp_cur = 400u;

  BattleEvent buf[256];
  BattleLog log;
  battle_log_init(log, buf, 256u);

  // Eight rounds of the same attacking move on both sides. The relation holds
  // every round; the ANNOUNCEMENT must not.
  for (int r = 0; r < 8; ++r) {
    rng_init(st.rng, SEED_HIT_ROLL0 + (uint32_t)r);
    // SLOT 0 is the damaging on-type move on both sides - slot 1 has no type
    // relation, which is what the first draft of this case got wrong and why
    // it measured zero edges over eight rounds.
    (void)round_with(st, ACT(BACT_ATTACK, 0u), ACT(BACT_ATTACK, 0u), &log);
  }

  int edges[2] = { 0, 0 };
  int hits = 0;
  int edge_before_its_hit = 0;
  for (uint16_t i = 0; i < log.count; ++i) {
    const BattleEvent* e = battle_log_at(log, i);
    if (!e) continue;
    if (e->kind == (uint8_t)RLE_HIT) hits++;
    if (e->kind != (uint8_t)RLE_TYPE_EDGE) continue;
    CHECK(e->side <= 1u);
    edges[e->side]++;
    CHECK(e->a == 1u || e->a == 2u);        // advantage or disadvantage, never 0
    CHECK_EQ((int)e->b, 0);                 // nothing left: the cap is 1
    // It belongs to the hit that follows it, so the very next event for the
    // same side is that RLE_HIT. Order is the whole reason it is pushed first.
    const BattleEvent* nx = battle_log_at(log, (uint16_t)(i + 1u));
    if (nx && nx->kind == (uint8_t)RLE_HIT && nx->side == e->side) edge_before_its_hit++;
  }
  CHECK(hits >= 8);                         // the sample really did land hits

  // THE MODIFIER IS THE ATTACK'S TYPE AGAINST THE DEFENDER'S, NOT THE
  // ATTACKER'S, and this case pins that because it is the easy thing to get
  // wrong - two drafts of it did. duel() arms BOTH sides with the same four
  // moves, so slot 0 is MV_PING on both, and MV_PING is one type:
  //    side 0: PING's type into CORRUPT -> a relation, one announcement
  //    side 1: PING's type into SIGNAL  -> none, and nothing is announced
  // Read out of the tables rather than asserted from memory, so a content
  // change that retypes MV_PING fails here instead of quietly making the case
  // measure nothing.
  const AttackDef* ping = attack_get(MV_PING);
  CHECK(ping != nullptr);
  const int8_t m_vs_corrupt = type_mod_of(ping->type, (uint8_t)TYPE_CORRUPT);
  const int8_t m_vs_signal  = type_mod_of(ping->type, (uint8_t)TYPE_SIGNAL);
  CHECK(m_vs_corrupt != 0);                 // side 0 really has an edge to spend
  CHECK_EQ((int)m_vs_signal, 0);            // side 1 really has none

  // ONE announcement for the side that has a relation, over eight rounds in
  // which that relation held every single time. That is the cap being visible.
  CHECK_EQ(edges[0], 1);
  // ...and NONE for the neutral side: a matchup with no edge must not announce
  // one. No false positives is half of what makes the line worth reading.
  CHECK_EQ(edges[1], 0);
  CHECK_EQ(edge_before_its_hit, 1);         // and it preceded its own hit
}

TEST(the_second_action_is_skipped_with_a_reason_when_the_first_one_emptied_the_field) {
  BattleState st;
  duel(st, TYPE_SIGNAL, TYPE_CORRUPT);
  st.side[1].team[0].hp_cur = 3u;
  st.side[0].team[0].spd = 20u;

  BattleEvent buf[128];
  BattleLog log;
  battle_log_init(log, buf, 128u);
  const uint16_t a_hp = st.side[0].team[0].hp_cur;
  rng_init(st.rng, SEED_HIT_ROLL0);
  round_with(st, ACT(BACT_ATTACK, 1u), ACT(BACT_ATTACK, 1u), &log);
  CHECK_EQ(st.side[0].team[0].hp_cur, a_hp);         // the corpse did not answer
  int skipped = 0;
  for (uint16_t i = 0; i < log.count; ++i) {
    const BattleEvent* e = battle_log_at(log, i);
    if (e && e->kind == (uint8_t)RLE_SKIPPED && e->a == (uint8_t)BSK_ACTOR_FAINTED) skipped++;
  }
  CHECK_EQ(skipped, 1);                              // skipped, and SAID SO
}

TEST(a_cooldown_blocks_exactly_the_rounds_the_content_asks_for) {
  BattleState st;
  fixture(st);
  // MV_SANDBOX: cooldown 4. Both sides hold harmless moves so nothing dies.
  arm(st.side[0].team[0], TYPE_SIGNAL, 10u, 5u, 9u, 400u,
      MV_SANDBOX, MV_AMPLIFICAR, MV_ANTENA, MV_PING);
  arm(st.side[1].team[0], TYPE_CORRUPT, 10u, 5u, 4u, 400u,
      MV_AMPLIFICAR, MV_ANTENA, MV_CHOQUE, MV_PING);
  const AttackDef* sb = attack_get(MV_SANDBOX);
  CHECK(sb != nullptr);
  CHECK_EQ(sb->cooldown, 4);

  rng_init(st.rng, SEED_HIT_ROLL0);
  round_with(st, ACT(BACT_ATTACK, 0u), ACT(BACT_ATTACK, 0u), nullptr);
  CHECK_EQ(st.side[0].team[0].cooldown[0], 4);       // armed 4+1, ticked once
  for (int r = 0; r < 4; ++r) {
    CHECK_EQ(battle_validate_action(st, 0u, ACT(BACT_ATTACK, 0u)), BR_MOVE_ON_COOLDOWN);
    round_with(st, ACT(BACT_ATTACK, 1u), ACT(BACT_ATTACK, 0u), nullptr);
  }
  // THE OTHER BOUNDARY: the round after the last blocked one, it is back.
  CHECK_EQ(st.side[0].team[0].cooldown[0], 0);
  CHECK_EQ(battle_validate_action(st, 0u, ACT(BACT_ATTACK, 0u)), BR_OK);
}

TEST(a_miss_still_pays_the_cooldown) {
  BattleState st;
  fixture(st);
  arm(st.side[0].team[0], TYPE_SYSTEM, 10u, 5u, 9u, 400u,
      MV_PANICO, MV_CHOQUE, MV_PING, MV_AMPLIFICAR);
  arm(st.side[1].team[0], TYPE_CORRUPT, 10u, 5u, 4u, 400u,
      MV_AMPLIFICAR, MV_ANTENA, MV_CHOQUE, MV_PING);
  const uint16_t hp0 = st.side[1].team[0].hp_cur;
  rng_init(st.rng, SEED_MISS_85);                    // roll 99 against accuracy 70
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 0u);
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK_EQ(st.side[1].team[0].hp_cur, hp0);          // it missed
  CHECK_EQ(st.side[0].team[0].cooldown[0], 3);       // and still paid 2 + 1
  CHECK_EQ(st.side[0].team[0].stun_left, 0);         // and its effect did NOT apply
}

TEST(a_stun_is_spent_by_the_turn_it_prevents_and_the_turn_after_is_free) {
  BattleState st;
  fixture(st);
  arm(st.side[0].team[0], TYPE_SYSTEM, 10u, 5u, 9u, 400u,
      MV_PANICO, MV_CHOQUE, MV_PING, MV_AMPLIFICAR);
  arm(st.side[1].team[0], TYPE_CORRUPT, 10u, 5u, 4u, 400u,
      MV_AMPLIFICAR, MV_ANTENA, MV_CHOQUE, MV_PING);

  rng_init(st.rng, SEED_HIT_ROLL0);
  round_with(st, ACT(BACT_ATTACK, 0u), ACT(BACT_ATTACK, 0u), nullptr);
  CHECK_EQ(st.side[0].team[0].stun_left, 1);         // armed, and NOT ticked by step 7

  const uint16_t hp0 = st.side[1].team[0].hp_cur;
  rng_init(st.rng, SEED_HIT_ROLL0);
  round_with(st, ACT(BACT_ATTACK, 1u), ACT(BACT_ATTACK, 0u), nullptr);
  CHECK_EQ(st.side[1].team[0].hp_cur, hp0);          // the stunned turn did nothing
  CHECK_EQ(st.side[0].team[0].stun_left, 0);         // and the skip IS the tick

  rng_init(st.rng, SEED_HIT_ROLL0);
  round_with(st, ACT(BACT_ATTACK, 1u), ACT(BACT_ATTACK, 0u), nullptr);
  CHECK_EQ(hp0 - st.side[1].team[0].hp_cur, 14);     // free again
}

TEST(corruption_lands_for_its_stated_duration_and_then_lets_go) {
  BattleState st;
  fixture(st);
  arm(st.side[0].team[0], TYPE_CORRUPT, 10u, 5u, 9u, 400u,
      MV_INFECTAR, MV_AMPLIFICAR, MV_ANTENA, MV_PING);
  arm(st.side[1].team[0], TYPE_SIGNAL, 10u, 5u, 4u, 400u,
      MV_AMPLIFICAR, MV_ANTENA, MV_CHOQUE, MV_PING);

  rng_init(st.rng, SEED_HIT_ROLL0);
  round_with(st, ACT(BACT_ATTACK, 0u), ACT(BACT_ATTACK, 0u), nullptr);
  CHECK_EQ(st.side[1].team[0].corrupt_left, 2);      // set to 3, ticked once
  round_with(st, ACT(BACT_ATTACK, 1u), ACT(BACT_ATTACK, 0u), nullptr);
  CHECK_EQ(st.side[1].team[0].corrupt_left, 1);
  round_with(st, ACT(BACT_ATTACK, 1u), ACT(BACT_ATTACK, 0u), nullptr);
  CHECK_EQ(st.side[1].team[0].corrupt_left, 0);      // THE BOUNDARY
  round_with(st, ACT(BACT_ATTACK, 1u), ACT(BACT_ATTACK, 0u), nullptr);
  CHECK_EQ(st.side[1].team[0].corrupt_left, 0);
}

TEST(cleanse_clears_the_negative_stages_the_dot_and_the_corruption_and_nothing_else) {
  BattleState st;
  fixture(st);
  arm(st.side[0].team[0], TYPE_SIGNAL, 10u, 5u, 9u, 400u,
      MV_DEPURAR, MV_CHOQUE, MV_PING, MV_AMPLIFICAR);
  arm(st.side[1].team[0], TYPE_CORRUPT, 10u, 5u, 4u, 400u,
      MV_AMPLIFICAR, MV_ANTENA, MV_CHOQUE, MV_PING);
  BattleCombatant& c = st.side[0].team[0];
  c.stage[BSTAT_ATK] = -2; c.stage_left[BSTAT_ATK] = 3u;
  c.stage[BSTAT_DEF] =  2; c.stage_left[BSTAT_DEF] = 3u;   // a GOOD one, kept
  c.dot_value = 5u; c.dot_left = 3u;
  c.corrupt_left = 3u;
  c.cooldown[1] = 2u;

  rng_init(st.rng, SEED_HIT_ROLL0);
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 0u);
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK_EQ(c.stage[BSTAT_ATK], 0);
  CHECK_EQ(c.stage_left[BSTAT_ATK], 0);
  CHECK_EQ(c.stage[BSTAT_DEF], 2);                   // a buff is not a curse
  CHECK_EQ(c.dot_left, 0);
  CHECK_EQ(c.dot_value, 0);
  CHECK_EQ(c.corrupt_left, 0);
  CHECK_EQ(c.cooldown[1], 2);                        // and it is not a refresh
}

// =============================================================================
//  4. VICTORY
// =============================================================================
TEST(a_side_with_nothing_left_standing_loses_and_two_of_them_draw) {
  BattleState st;
  fixture(st);
  CHECK_EQ(battle_s9_check_victory(st), BO_UNDECIDED);

  for (uint8_t i = 0; i < 3u; ++i) st.side[1].team[i].hp_cur = 0u;
  battle_s6_process_fainting(st, nullptr);
  CHECK_EQ(battle_s9_check_victory(st), BO_WIN_A);

  fixture(st);
  for (uint8_t i = 0; i < 3u; ++i) st.side[0].team[i].hp_cur = 0u;
  battle_s6_process_fainting(st, nullptr);
  CHECK_EQ(battle_s9_check_victory(st), BO_WIN_B);

  // A DOUBLE KO IS A DRAW, and it costs no draw from the battle stream: there
  // is no coin flip in this engine.
  fixture(st);
  for (uint8_t i = 0; i < 3u; ++i) {
    st.side[0].team[i].hp_cur = 0u;
    st.side[1].team[i].hp_cur = 0u;
  }
  battle_s6_process_fainting(st, nullptr);
  const uint32_t cursor = st.rng.s;
  CHECK_EQ(battle_s9_check_victory(st), BO_DRAW);
  CHECK_EQ(st.rng.s, cursor);
}

TEST(the_round_cap_is_broken_by_the_hp_fraction_and_not_by_raw_hp) {
  BattleSetup s;
  BattleState st;
  mk_setup(s, 9u, TEAM_A, 1u, TEAM_B, 1u, 10u);
  CHECK_EQ(battle_init(st, s), BR_OK);
  st.round = (uint16_t)(BATTLE_MAX_ROUNDS);
  CHECK_EQ(battle_s9_check_victory(st), BO_UNDECIDED);      // not yet
  st.round = (uint16_t)(BATTLE_MAX_ROUNDS + 1u);

  // A has MORE raw HP and a WORSE fraction: 10/100 against 5/40. The simulator
  // sums raw HP and would give this to A; data/balance.h cross-multiplies and
  // gives it to B, which is the divergence battle.h names.
  st.side[0].team[0].hp_max = 100u; st.side[0].team[0].hp_cur = 10u;
  st.side[1].team[0].hp_max =  40u; st.side[1].team[0].hp_cur =  5u;
  const uint32_t cursor = st.rng.s;
  CHECK_EQ(battle_s9_check_victory(st), BO_WIN_B);
  CHECK_EQ(st.rng.s, cursor);                                // and no coin flip

  st.side[1].team[0].hp_max = 50u;                           // 10/100 == 5/50
  CHECK_EQ(battle_s9_check_victory(st), BO_DRAW);
  CHECK_EQ(st.rng.s, cursor);

  st.side[1].team[0].hp_max = 60u;                           // 10/100 > 5/60
  CHECK_EQ(battle_s9_check_victory(st), BO_WIN_A);

  // A fainted member contributes 0 hp and its FULL maximum, which is what
  // penalises the side that lost Pebbles.
  mk_setup(s, 9u, TEAM_A, 2u, TEAM_B, 1u, 10u);
  CHECK_EQ(battle_init(st, s), BR_OK);
  st.round = (uint16_t)(BATTLE_MAX_ROUNDS + 1u);
  st.side[0].team[0].hp_max = 100u; st.side[0].team[0].hp_cur = 100u;
  st.side[0].team[1].hp_max = 100u; st.side[0].team[1].hp_cur = 0u;
  battle_s6_process_fainting(st, nullptr);
  st.side[1].team[0].hp_max = 100u; st.side[1].team[0].hp_cur = 60u;
  CHECK_EQ(battle_s9_check_victory(st), BO_WIN_B);           // 100/200 < 60/100
}

TEST(a_battle_that_neither_side_can_win_still_terminates_at_the_cap) {
  BattleState st;
  fixture(st);
  // Two power-0 movesets: nothing can ever deal damage, so only the cap ends it.
  arm(st.side[0].team[0], TYPE_SIGNAL, 10u, 5u, 9u, 400u,
      MV_AMPLIFICAR, MV_ANTENA, MV_AMPLIFICAR, MV_ANTENA);
  arm(st.side[1].team[0], TYPE_CORRUPT, 10u, 5u, 4u, 400u,
      MV_AMPLIFICAR, MV_ANTENA, MV_AMPLIFICAR, MV_ANTENA);
  for (uint8_t i = 1; i < 3u; ++i) {
    st.side[0].team[i].flags = 0u;
    st.side[1].team[i].flags = 0u;
  }
  int rounds = 0;
  while (battle_step_round(st, nullptr) != BS_BATTLE_OVER) {
    CHECK_EQ(battle_submit_action(st, 0u, ACT(BACT_ATTACK, 0u)), BR_OK);
    CHECK_EQ(battle_submit_action(st, 1u, ACT(BACT_ATTACK, 1u)), BR_OK);
    if (++rounds > BATTLE_MAX_ROUNDS + 4) break;
  }
  CHECK_EQ(st.round, BATTLE_MAX_ROUNDS + 1);
  CHECK(st.outcome == (uint8_t)BO_DRAW || st.outcome == (uint8_t)BO_WIN_A ||
        st.outcome == (uint8_t)BO_WIN_B);
  CHECK(st.outcome != (uint8_t)BO_UNDECIDED);
}

// =============================================================================
//  5. THE LOG - which the hash structurally cannot see, so it is asserted here
// =============================================================================
TEST(the_log_records_the_round_in_the_order_the_nine_steps_ran) {
  BattleState st;
  duel(st, TYPE_SIGNAL, TYPE_CORRUPT);
  st.side[0].team[0].spd = 20u;
  BattleEvent buf[64];
  BattleLog log;
  battle_log_init(log, buf, 64u);
  rng_init(st.rng, SEED_HIT_ROLL0);
  round_with(st, ACT(BACT_ATTACK, 1u), ACT(BACT_ATTACK, 1u), &log);

  CHECK_EQ(log.dropped, 0);
  CHECK(log.count >= 8);
  const BattleEvent* e0 = battle_log_at(log, 0u);
  CHECK(e0 != nullptr);
  if (!e0) return;
  CHECK_EQ(e0->kind, RLE_ROUND_BEGIN);
  CHECK_EQ(e0->round, 1);
  CHECK(e0->v != 0u);
  const BattleEvent* e1 = battle_log_at(log, 1u);
  CHECK_EQ(e1->kind, RLE_ACTION);
  CHECK_EQ(e1->side, 0);
  CHECK_EQ(e1->a, BACT_ATTACK);
  CHECK_EQ(e1->b, 1);
  const BattleEvent* last = battle_log_at(log, (uint16_t)(log.count - 1u));
  CHECK_EQ(last->kind, RLE_ROUND_END);
  CHECK_EQ(last->round, 1);

  // The HIT carries the resulting damage and the HP its resulting value, not a
  // delta - which is what makes a transcript readable without replaying it.
  int hits = 0;
  for (uint16_t i = 0; i < log.count; ++i) {
    const BattleEvent* e = battle_log_at(log, i);
    if (e->kind == (uint8_t)RLE_HIT) {
      hits++;
      CHECK_EQ(e->b, 14);
    }
    if (e->kind == (uint8_t)RLE_HP) CHECK(e->b <= 400u);
  }
  CHECK_EQ(hits, 2);                      // both sides landed one
}

TEST(the_log_ring_saturates_its_dropped_counter_instead_of_wrapping_it) {
  BattleEvent buf[4];
  BattleLog log;
  battle_log_init(log, buf, 4u);
  CHECK_EQ(log.count, 0);
  CHECK_EQ(log.dropped, 0);

  BattleState st;
  duel(st, TYPE_SIGNAL, TYPE_CORRUPT);
  st.side[0].team[0].hp_max = 4000u; st.side[0].team[0].hp_cur = 4000u;
  st.side[1].team[0].hp_max = 4000u; st.side[1].team[0].hp_cur = 4000u;
  rng_init(st.rng, SEED_HIT_ROLL0);
  for (int r = 0; r < 6; ++r) {
    if (battle_step_round(st, &log) == BS_BATTLE_OVER) break;
    CHECK_EQ(battle_submit_action(st, 0u, ACT(BACT_ATTACK, 1u)), BR_OK);
    CHECK_EQ(battle_submit_action(st, 1u, ACT(BACT_ATTACK, 1u)), BR_OK);
  }
  CHECK_EQ(log.count, 4);                 // full
  CHECK(log.dropped > 0);                 // and it says so
  // Oldest-first reading still works over a wrapped ring.
  for (uint16_t i = 0; i < 4u; ++i) CHECK(battle_log_at(log, i) != nullptr);
  CHECK(battle_log_at(log, 4u) == nullptr);
}

TEST(a_null_log_is_not_a_back_channel_into_the_rules) {
  // The same battle twice, once logging and once not: the two must agree byte
  // for byte, which is what proves no rule branches on log != nullptr.
  BattleState a, b;
  duel(a, TYPE_SIGNAL, TYPE_CORRUPT);
  duel(b, TYPE_SIGNAL, TYPE_CORRUPT);
  BattleEvent buf[512];
  BattleLog log;
  battle_log_init(log, buf, 512u);
  for (int r = 0; r < 8; ++r) {
    const uint8_t slot = (uint8_t)(r & 1);
    CHECK_EQ(battle_submit_action(a, 0u, ACT(BACT_ATTACK, slot)), BR_OK);
    CHECK_EQ(battle_submit_action(a, 1u, ACT(BACT_ATTACK, slot)), BR_OK);
    CHECK_EQ(battle_submit_action(b, 0u, ACT(BACT_ATTACK, slot)), BR_OK);
    CHECK_EQ(battle_submit_action(b, 1u, ACT(BACT_ATTACK, slot)), BR_OK);
    const BattleStepResult ra = battle_step_round(a, &log);
    const BattleStepResult rb = battle_step_round(b, nullptr);
    CHECK_EQ(ra, rb);
    CHECK_EQ(battle_state_hash(a), battle_state_hash(b));
    CHECK(memcmp(&a, &b, sizeof a) == 0);
    if (ra == BS_BATTLE_OVER) break;
  }
}

// =============================================================================
//  6. ACCURACY, EVASION AND THE THREE HP EFFECTS
//     (added because a rule with no case is a rule with no gate)
// =============================================================================
#define SEED_ROLL_35   5632u    // first rng_next_below(100) == 35
#define SEED_ROLL_45   7168u    // first rng_next_below(100) == 45
#define SEED_ROLL_80   12800u   // first rng_next_below(100) == 80

TEST(a_faster_defender_is_harder_to_hit_and_the_gap_stops_at_its_cap) {
  BattleState st;
  duel(st, TYPE_SIGNAL, TYPE_CORRUPT);
  const uint16_t hp0 = st.side[1].team[0].hp_cur;

  // No speed gap: MV_CHOQUE is accuracy 100 and the roll is 80, so it lands.
  st.side[0].team[0].spd = 5u;
  st.side[1].team[0].spd = 5u;
  rng_init(st.rng, SEED_ROLL_80);
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 1u);
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK(st.side[1].team[0].hp_cur < hp0);

  // A ten-point speed gap costs EVASION_PER_SPD * 10 = 20 points of accuracy,
  // so the identical roll now misses.
  st.side[1].team[0].hp_cur = hp0;
  st.side[1].team[0].spd = 15u;
  rng_init(st.rng, SEED_ROLL_80);
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 1u);
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK_EQ(st.side[1].team[0].hp_cur, hp0);

  // AND THE GAP IS CAPPED: a defender 90 points faster is no harder to hit than
  // one 10 points faster. THE ROLL HAS TO SIT BETWEEN THE TWO READINGS or the
  // case proves nothing - capped gives accuracy 100 - 20 = 80, uncapped would
  // give 100 - 180 floored to ACCURACY_MIN 40, so a roll of 45 lands under the
  // clamp and misses without it. A roll of 35 would hit under BOTH and was the
  // first version of this line; the mutation sweep caught that.
  st.side[1].team[0].hp_cur = hp0;
  st.side[1].team[0].spd = 95u;
  CHECK(100 - EVASION_PER_SPD * 90 < ACCURACY_MIN);
  rng_init(st.rng, SEED_ROLL_45);
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 1u);
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK(st.side[1].team[0].hp_cur < hp0);

  // A POWER-0 MOVE ROLLS AGAINST RAW ACCURACY: no evasion, however wide the
  // gap - and it still costs exactly one draw.
  st.side[0].team[0].stage[BSTAT_ATK] = 0;
  st.side[0].team[0].stage_left[BSTAT_ATK] = 0u;
  rng_init(st.rng, SEED_ROLL_80);
  const uint32_t cursor = st.rng.s;
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 3u);     // MV_AMPLIFICAR, accuracy 100
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK_EQ(st.side[0].team[0].stage[BSTAT_ATK], 1);  // it landed
  CHECK(st.rng.s != cursor);
  CHECK_EQ(draws_from(cursor, st.rng.s), 1);         // exactly one, no damage roll
}

TEST(accuracy_never_falls_below_its_floor_however_fast_the_defender_is) {
  BattleState st;
  fixture(st);
  // MV_APUESTA is accuracy 55. A ten-point gap would take it to 35, which is
  // below ACCURACY_MIN, so the floor holds it at 40 and a roll of 35 LANDS.
  arm(st.side[0].team[0], TYPE_SIGNAL, 10u, 5u, 5u, 400u,
      28u, MV_CHOQUE, MV_PING, MV_AMPLIFICAR);
  arm(st.side[1].team[0], TYPE_CORRUPT, 10u, 5u, 15u, 400u,
      MV_CHOQUE, MV_PING, MV_RAFAGA, MV_AMPLIFICAR);
  CHECK_EQ(attack_get(28u)->accuracy, 55);
  CHECK(55 - EVASION_PER_SPD * EVASION_MAX_SPD_GAP < ACCURACY_MIN);

  const uint16_t hp0 = st.side[1].team[0].hp_cur;
  rng_init(st.rng, SEED_ROLL_35);
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 0u);
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK(st.side[1].team[0].hp_cur < hp0);            // the floor saved it

  // ...and just above the floor it still misses, so the floor is a floor and
  // not "always hit".
  st.side[1].team[0].hp_cur = hp0;
  st.side[0].team[0].cooldown[0] = 0u;
  rng_init(st.rng, SEED_ROLL_80);
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 0u);
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK_EQ(st.side[1].team[0].hp_cur, hp0);
}

TEST(heal_drain_and_recoil_move_hp_by_the_percentages_the_content_publishes) {
  BattleState st;
  fixture(st);
  // MV_BACKUP 33: HEAL_PCT 25 of hp_max, power 0. MV_DEVORAR 14: DRAIN_PCT 50
  // of the damage DEALT, power 50. MV_ECO_DOBLE 8: RECOIL_PCT 25, power 90.
  arm(st.side[0].team[0], TYPE_CORRUPT, 10u, 5u, 9u, 200u,
      33u, 14u, 8u, MV_CHOQUE);
  arm(st.side[1].team[0], TYPE_SYSTEM, 10u, 5u, 4u, 200u,
      MV_CHOQUE, MV_PING, MV_RAFAGA, MV_AMPLIFICAR);

  // HEAL: 25 % of 200 is 50, and it never overheals.
  st.side[0].team[0].hp_cur = 100u;
  rng_init(st.rng, SEED_HIT_ROLL0);
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 0u);
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK_EQ(st.side[0].team[0].hp_cur, 150);
  st.side[0].team[0].hp_cur = 190u;
  st.side[0].team[0].cooldown[0] = 0u;
  rng_init(st.rng, SEED_HIT_ROLL0);
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 0u);
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK_EQ(st.side[0].team[0].hp_cur, 200);          // capped at hp_max

  // DRAIN: CORRUPT into SYSTEM is a type ADVANTAGE, raw = 50*10/(5*7) = 14,
  // x5/4 = 17, roll 0; half of 17 is 8.
  st.side[0].team[0].hp_cur = 100u;
  const uint16_t f0 = st.side[1].team[0].hp_cur;
  rng_init(st.rng, SEED_HIT_ROLL0);
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 1u);
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK_EQ(f0 - st.side[1].team[0].hp_cur, 17);
  CHECK_EQ(st.side[0].team[0].hp_cur, 100 + 8);

  // RECOIL: the edge is spent now, so raw = 90*10/(5*7) = 25 at x1, roll 0;
  // a quarter of 25 is 6, taken off the USER.
  const uint16_t u0 = st.side[0].team[0].hp_cur;
  const uint16_t f1 = st.side[1].team[0].hp_cur;
  rng_init(st.rng, SEED_HIT_ROLL0);
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 2u);
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK_EQ(f1 - st.side[1].team[0].hp_cur, 25);
  CHECK_EQ(u0 - st.side[0].team[0].hp_cur, 6);
}

// =============================================================================
//  7. THE FOUR RULES THE MUTATION SWEEP FOUND UNGUARDED
//     Each of these landed here because a deliberate break of it left the whole
//     suite green. They are not extra colour; they are the gaps that pass found.
// =============================================================================
#define MV_ECO_DOBLE  8u    // SIGNAL  power 90 acc 75 RECOIL_PCT 25
#define MV_GUSANO    17u    // CORRUPT power 85 acc 80 SELF_DEBUFF_DEF 1 for 3

TEST(a_recoil_kill_leaves_the_second_action_with_a_corpse_to_skip) {
  // The OTHER arm of step 5's corpse rule: the first mover is dead and the
  // second mover is fine, so it is the TARGET that is missing. Deleting only
  // that arm left every case green until this one existed.
  BattleState st;
  fixture(st);
  arm(st.side[0].team[0], TYPE_SIGNAL, 10u, 5u, 20u, 400u,
      MV_ECO_DOBLE, MV_CHOQUE, MV_PING, MV_AMPLIFICAR);
  arm(st.side[1].team[0], TYPE_CORRUPT, 10u, 5u, 4u, 400u,
      MV_CHOQUE, MV_PING, MV_RAFAGA, MV_AMPLIFICAR);
  st.side[0].team[0].hp_cur = 1u;               // one point of recoil is fatal

  BattleEvent buf[128];
  BattleLog log;
  battle_log_init(log, buf, 128u);
  const uint16_t b_hp = st.side[1].team[0].hp_cur;
  rng_init(st.rng, SEED_HIT_ROLL0);
  round_with(st, ACT(BACT_ATTACK, 0u), ACT(BACT_ATTACK, 0u), &log);

  CHECK_EQ(st.side[0].team[0].hp_cur, 0);       // killed by its own recoil
  CHECK(st.side[1].team[0].hp_cur < b_hp);      // the blow still landed
  int target_skips = 0;
  for (uint16_t i = 0; i < log.count; ++i) {
    const BattleEvent* e = battle_log_at(log, i);
    if (e && e->kind == (uint8_t)RLE_SKIPPED &&
        e->a == (uint8_t)BSK_TARGET_FAINTED && e->side == 1u) target_skips++;
  }
  CHECK_EQ(target_skips, 1);
}

TEST(a_dot_refreshes_its_single_slot_and_never_stacks) {
  BattleState st;
  fixture(st);
  // No SHIPPED learnset can reach attack 13 (its only species is 46, outside
  // the 36-species prefix - test_content.cpp pins that), so the move is placed
  // here by hand. The RULE still ships, and battle.h static_asserts that only
  // one DOT row exists, which is what makes one slot exact.
  arm(st.side[0].team[0], TYPE_CORRUPT, 10u, 5u, 9u, 400u,
      MV_INFECCION, MV_CHOQUE, MV_PING, MV_AMPLIFICAR);
  arm(st.side[1].team[0], TYPE_SIGNAL, 10u, 5u, 4u, 400u,
      MV_CHOQUE, MV_PING, MV_RAFAGA, MV_AMPLIFICAR);
  const AttackDef* dot = attack_get(MV_INFECCION);
  CHECK(dot != nullptr);
  CHECK_EQ(dot->effect, ATK_EFF_DOT);
  CHECK_EQ(dot->effect_value, 5);
  CHECK_EQ(dot->effect_duration, 3);

  rng_init(st.rng, SEED_HIT_ROLL0);
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 0u);
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK_EQ(st.side[1].team[0].dot_value, 5);
  CHECK_EQ(st.side[1].team[0].dot_left, 3);

  st.side[1].team[0].dot_left = 1u;             // partly spent
  rng_init(st.rng, SEED_HIT_ROLL0);
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 0u);
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK_EQ(st.side[1].team[0].dot_value, 5);    // NOT 10: it refreshes
  CHECK_EQ(st.side[1].team[0].dot_left, 3);
}

TEST(corruption_takes_the_longer_of_the_two_durations_and_never_shortens_itself) {
  BattleState st;
  fixture(st);
  arm(st.side[0].team[0], TYPE_CORRUPT, 10u, 5u, 9u, 400u,
      MV_INFECTAR, MV_CHOQUE, MV_PING, MV_AMPLIFICAR);
  arm(st.side[1].team[0], TYPE_SIGNAL, 10u, 5u, 4u, 400u,
      MV_CHOQUE, MV_PING, MV_RAFAGA, MV_AMPLIFICAR);

  st.side[1].team[0].corrupt_left = 5u;         // longer than the move's 3
  rng_init(st.rng, SEED_HIT_ROLL0);
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 0u);
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK_EQ(st.side[1].team[0].corrupt_left, 5); // NOT 3: a re-infection cannot cure

  st.side[1].team[0].corrupt_left = 1u;         // shorter than the move's 3
  st.side[0].team[0].cooldown[0] = 0u;
  rng_init(st.rng, SEED_HIT_ROLL0);
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 0u);
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK_EQ(st.side[1].team[0].corrupt_left, 3);
}

TEST(a_debuff_lands_on_the_defender_and_a_self_debuff_on_the_user) {
  BattleState st;
  fixture(st);
  arm(st.side[0].team[0], TYPE_SYSTEM, 10u, 5u, 9u, 400u,
      MV_BLOQUEO, MV_GUSANO, MV_CHOQUE, MV_AMPLIFICAR);
  arm(st.side[1].team[0], TYPE_CORRUPT, 10u, 5u, 4u, 400u,
      MV_CHOQUE, MV_PING, MV_RAFAGA, MV_AMPLIFICAR);

  rng_init(st.rng, SEED_HIT_ROLL0);
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 0u);       // DEBUFF_ATK on the FOE
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK_EQ(st.side[1].team[0].stage[BSTAT_ATK], -1);
  CHECK_EQ(st.side[1].team[0].stage_left[BSTAT_ATK], 3);
  CHECK_EQ(st.side[0].team[0].stage[BSTAT_ATK], 0);    // and NOT on the user
  CHECK_EQ(battle_stat_eff(st.side[1].team[0], (uint8_t)BSTAT_ATK), 9);

  rng_init(st.rng, SEED_HIT_ROLL0);
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 1u);       // SELF_DEBUFF_DEF on the USER
  battle_s4_resolve_first(st, 0u, nullptr);
  CHECK_EQ(st.side[0].team[0].stage[BSTAT_DEF], -1);
  CHECK_EQ(st.side[1].team[0].stage[BSTAT_DEF], 0);
  CHECK_EQ(battle_stat_eff(st.side[0].team[0], (uint8_t)BSTAT_DEF), 4);
}

TEST(a_third_buff_cannot_push_the_stored_stage_past_the_clamp) {
  // The STORED stage is hashed state that P4-C5 compares between peers, so it
  // has to be clamped where it is written and not only where it is read. Until
  // this case existed, dropping the clamp inside set_stage() left every test
  // green: battle_stat_eff() clamps again on the way out and hid it.
  BattleState st;
  fixture(st);
  arm(st.side[0].team[0], TYPE_SIGNAL, 10u, 5u, 9u, 400u,
      MV_AMPLIFICAR, MV_BLOQUEO, MV_CHOQUE, MV_PING);
  arm(st.side[1].team[0], TYPE_CORRUPT, 10u, 5u, 4u, 400u,
      MV_CHOQUE, MV_PING, MV_RAFAGA, MV_AMPLIFICAR);
  BattleCombatant& c = st.side[0].team[0];

  for (int i = 0; i < 3; ++i) {
    rng_init(st.rng, SEED_HIT_ROLL0);
    set_pending(st, 0u, (uint8_t)BACT_ATTACK, 0u);
    battle_s4_resolve_first(st, 0u, nullptr);
  }
  CHECK_EQ(c.stage[BSTAT_ATK], BUFF_STAGE_MAX);            // stored, not merely read
  CHECK_EQ(battle_stat_eff(c, (uint8_t)BSTAT_ATK), 10 + BUFF_STAGE_MAX);

  // And the same on the way down: three debuffs on the foe stop at the floor.
  BattleCombatant& f = st.side[1].team[0];
  for (int i = 0; i < 3; ++i) {
    f.stage[BSTAT_ATK] = (int8_t)(f.stage[BSTAT_ATK]);
    rng_init(st.rng, SEED_HIT_ROLL0);
    c.cooldown[1] = 0u;
    set_pending(st, 0u, (uint8_t)BACT_ATTACK, 1u);         // MV_BLOQUEO, DEBUFF_ATK
    battle_s4_resolve_first(st, 0u, nullptr);
  }
  CHECK_EQ(f.stage[BSTAT_ATK], BUFF_STAGE_MIN);
  CHECK_EQ(battle_stat_eff(f, (uint8_t)BSTAT_ATK), 10 + BUFF_STAGE_MIN);
}

// =============================================================================
//  8. THE THREE PUBLIC QUERIES THAT WERE ONLY EVER REACHED THROUGH A CALLER
//
//  The P4-C2/C3 review's single-line deletion sweep found guards that survive
//  deletion NOT because they are unreachable, but because every test that
//  exercised them went in through a caller carrying its own copy of the same
//  check. game/battle.h publishes all three of these as callable on their own -
//  P4-C5 calls battle_action_legal_now() directly, and battle_ai.cpp asks
//  battle_move_ready() before it scores anything - so they are tested here as
//  what they are, rather than labelled untested.
// =============================================================================
TEST(battle_action_legal_now_refuses_a_bad_side_without_help_from_its_callers) {
  BattleState st;
  fixture(st);
  // battle_validate_action() checks the side too and shadowed this one on every
  // path the suite used, so deleting it was green. Called directly, it is the
  // only thing between `side` and st.side[side].
  // Without the guard both of these index st.side[side] on a two-element array,
  // which is undefined behaviour rather than a wrong answer: the observed
  // symptom is a wrong reject code for 2 and a crash for 255. Both fail, which
  // is all a guard against a peer-supplied byte can be asked to demonstrate.
  CHECK_EQ(battle_action_legal_now(st, 2u, ACT(BACT_ATTACK, 0u)), BR_BAD_SIDE);
  CHECK_EQ(battle_action_legal_now(st, 255u, ACT(BACT_SWITCH, 1u)), BR_BAD_SIDE);
  // The positive controls, on both sides, so a function that refused everything
  // could not satisfy the two above.
  CHECK_EQ(battle_action_legal_now(st, 0u, ACT(BACT_ATTACK, 0u)), BR_OK);
  CHECK_EQ(battle_action_legal_now(st, 1u, ACT(BACT_ATTACK, 0u)), BR_OK);
  CHECK_EQ(battle_action_legal_now(st, 1u, ACT(BACT_SWITCH, 1u)), BR_OK);
  // And the range order: a bad side is answered BEFORE a bad kind, so nothing
  // is indexed on the way to the second complaint.
  CHECK_EQ(battle_action_legal_now(st, 2u, ACT(BACT_NONE, 0u)), BR_BAD_SIDE);
}

TEST(battle_move_ready_answers_for_itself_and_each_of_its_guards_is_reachable) {
  BattleState st;
  duel(st, TYPE_SIGNAL, TYPE_CORRUPT);
  BattleCombatant& c = st.side[0].team[0];

  CHECK(battle_move_ready(c, 0u));                       // the positive control
  CHECK(battle_move_ready(c, 3u));

  // THE SLOT BOUND, and it needs arranging or it cannot fail. Slot 4 reads the
  // byte just past moves[3], which is cooldown[0]; that byte is 0 in any normal
  // fixture, so `moves[slot] == 0` on the next line answers for the missing
  // bound and deleting it stays green. Park a REAL attack id there and slot 4
  // resolves to a real move whose slot-4 "cooldown" (stage[0]) is 0 - so
  // without the bound this call answers true.
  // Said through offsetof rather than by indexing moves[4] here: the point is
  // that the FUNCTION must not make that read, and a test that makes it itself
  // is undefined behaviour of its own (UBSAN says so out loud).
  CHECK_EQ(offsetof(BattleCombatant, cooldown), offsetof(BattleCombatant, moves) + PB_MOVE_COUNT);
  CHECK_EQ(offsetof(BattleCombatant, stage), offsetof(BattleCombatant, cooldown) + PB_MOVE_COUNT);
  c.cooldown[0] = MV_CHOQUE;      // == moves[4] to anything that ignores the bound
  CHECK_EQ(c.stage[0], 0);        // == cooldown[4], so the move would read as READY
  CHECK(!battle_move_ready(c, (uint8_t)PB_MOVE_COUNT));
  c.cooldown[0] = 0u;
  CHECK(battle_move_ready(c, 0u));                        // and slot 0 is free again

  const uint8_t keep = c.moves[2];
  c.moves[2] = 0u;                                       // the empty slot
  CHECK(!battle_move_ready(c, 2u));
  c.moves[2] = (uint8_t)(ATTACK_COUNT + 1u);             // an id past the table
  CHECK(!battle_move_ready(c, 2u));
  c.moves[2] = keep;
  CHECK(battle_move_ready(c, 2u));

  c.cooldown[2] = 1u;                                    // and the cooldown
  CHECK(!battle_move_ready(c, 2u));
  c.cooldown[2] = 0u;
  CHECK(battle_move_ready(c, 2u));
}

TEST(the_drivers_two_early_outs_are_two_guards_and_not_one) {
  // They mutually masked: phase and outcome are set together when a battle
  // ends, so deleting EITHER alone left the suite green. Each arm below reaches
  // a state where exactly one of them is the thing that answers.
  //
  // ARM 1 - a state that was never initialised. phase is BP_INIT and outcome is
  // BO_UNDECIDED, so only the PHASE guard can refuse it. Without that guard the
  // driver falls through to the pending check and answers BS_NEED_ACTIONS,
  // which invites a caller to keep feeding a battle that does not exist.
  BattleState st;
  memset(&st, 0, sizeof st);
  BattleState before;
  memcpy(&before, &st, sizeof before);
  CHECK_EQ(battle_step_round(st, nullptr), BS_BATTLE_OVER);
  CHECK(memcmp(&before, &st, sizeof st) == 0);

  // ARM 2 - a battle that ENDED. phase is still BP_RUNNING (the engine has no
  // "over" phase on purpose: game/battle.h says outcome is the single fact), so
  // only the OUTCOME guard can refuse it. The pending actions are written
  // directly, because battle_submit_action would refuse them by name - which is
  // exactly why the driver may not rely on the submitter having been asked.
  fixture(st);
  st.side[1].team[0].hp_cur = 0u;
  st.side[1].team[1].hp_cur = 0u;
  st.side[1].team[2].hp_cur = 0u;
  round_with(st, ACT(BACT_ATTACK, 0u), ACT(BACT_ATTACK, 0u), nullptr);
  CHECK_EQ(st.phase, BP_RUNNING);
  CHECK_EQ(st.outcome, BO_WIN_A);
  set_pending(st, 0u, (uint8_t)BACT_ATTACK, 0u);
  set_pending(st, 1u, (uint8_t)BACT_ATTACK, 0u);
  memcpy(&before, &st, sizeof before);
  CHECK_EQ(battle_step_round(st, nullptr), BS_BATTLE_OVER);
  CHECK(memcmp(&before, &st, sizeof st) == 0);
}
