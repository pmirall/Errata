// =============================================================================
//  PEBBLEBOL - game/battle.cpp
//  Spec section 14's nine steps. See game/battle.h for the determinism
//  contract, the rejection contract, the draw budget and the five named
//  divergences from tools/content/sim_engine.py.
//
//  THERE IS NO FILE-SCOPE MUTABLE VARIABLE IN THIS FILE and there must not be
//  one: P4-C5's loopback runs two engines in one process, and a single static
//  would couple them into one battle that agrees with itself.
// =============================================================================
#include "battle.h"

#include <string.h>

#include "pebble.h"     // pebble_derive_stats(): the ONE stat derivation
#include "xp.h"         // xp_hp_max() reaches it; battle.cpp holds NO stat formula

// =============================================================================
//  THE LOG
// =============================================================================
void battle_log_init(BattleLog& l, BattleEvent* buf, uint16_t cap)
{
  l.ev      = buf;
  l.cap     = (buf != nullptr) ? cap : 0u;
  l.head    = 0u;
  l.count   = 0u;
  l.dropped = 0u;
}

// The one write site. Its first line is the whole nullable-log contract: the
// rules must not be able to branch on whether anybody is listening.
static void log_push(BattleLog* log, uint8_t kind, uint8_t side, uint8_t slot,
                     uint8_t a, uint16_t b, uint16_t round, uint32_t v)
{
  if (log == nullptr || log->ev == nullptr || log->cap == 0u) return;
  BattleEvent& e = log->ev[log->head];
  e.kind  = kind;
  e.side  = side;
  e.slot  = slot;
  e.a     = a;
  e.b     = b;
  e.round = round;
  e.v     = v;
  log->head = (uint16_t)((log->head + 1u) % log->cap);
  if (log->count < log->cap) {
    log->count = (uint16_t)(log->count + 1u);
  } else if (log->dropped < 0xFFFFu) {
    // Saturates rather than wraps: a ring that overflowed 65,537 times must not
    // report that it dropped one event.
    log->dropped = (uint16_t)(log->dropped + 1u);
  }
}

const BattleEvent* battle_log_at(const BattleLog& l, uint16_t i)
{
  if (l.ev == nullptr || l.cap == 0u || i >= l.count) return nullptr;
  const uint16_t oldest = (uint16_t)((l.head + l.cap - l.count) % l.cap);
  return &l.ev[(uint16_t)((oldest + i) % l.cap)];
}

// =============================================================================
//  THE HASH
// =============================================================================
// One FNV-1a step. Named so the basis and the state walk are provably the same
// arithmetic and cannot drift into two mixings.
static inline uint32_t fnv1a_byte(uint32_t h, uint8_t b)
{
  h ^= (uint32_t)b;
  h *= 16777619u;
  return h;
}

uint32_t battle_hash_basis(uint32_t engine_ver, uint32_t hash_ver, uint32_t content_ver)
{
  // THE THREE VERSIONS RIDE IN THE BASIS, not in the state: a peer on other
  // rules, another hash mixing or another content pack can never match, at a
  // cost of zero state bytes.
  //
  // MIXED, NOT XORED. `basis ^ hash_ver ^ content_ver` was the P4-C2 form and it
  // collides: (1, 0x5B4A) and (3, 0x5B48) both fold to 0x5B4B, so two builds
  // that disagree satisfy one guard. Four bytes of each word through the FNV
  // step has no such pair, and the order is fixed here so two peers agree.
  uint32_t h = 2166136261u;
  const uint32_t w[3] = { engine_ver, hash_ver, content_ver };
  for (uint8_t i = 0; i < 3u; ++i)
    for (uint8_t k = 0; k < 4u; ++k)
      h = fnv1a_byte(h, (uint8_t)((w[i] >> (8u * k)) & 0xFFu));
  return h;
}

uint32_t battle_state_hash(const BattleState& st)
{
  uint32_t h = battle_hash_basis((uint32_t)BATTLE_ENGINE_VER,
                                 (uint32_t)BATTLE_HASH_VERSION,
                                 (uint32_t)CONTENT_VERSION);
  const uint8_t* p = (const uint8_t*)&st;
  for (size_t i = 0; i < sizeof(BattleState); ++i) h = fnv1a_byte(h, p[i]);
  return h;
}

// =============================================================================
//  CONST QUERIES
// =============================================================================
const BattleCombatant* battle_combatant(const BattleState& st, uint8_t side, uint8_t slot)
{
  if (side > 1u || slot >= (uint8_t)BATTLE_TEAM_MAX) return nullptr;
  return &st.side[side].team[slot];
}

const BattleCombatant* battle_active(const BattleState& st, uint8_t side)
{
  if (side > 1u) return nullptr;
  return battle_combatant(st, side, st.side[side].active);
}

// hp_cur > 0 IS part of "alive", and not only the flag. Between step 4 and
// step 6 the two disagree on purpose - that window is exactly the corpse rule
// step 5 exists for - and a definition that read the flag alone would let a
// Pebble that was just killed answer the blow that killed it.
static inline bool combatant_alive(const BattleCombatant& c)
{
  return (c.flags & BCF_PRESENT) != 0u && (c.flags & BCF_FAINTED) == 0u && c.hp_cur > 0u;
}

uint16_t battle_stat_eff(const BattleCombatant& c, uint8_t stat)
{
  if (stat >= (uint8_t)BSTAT_COUNT) return (uint16_t)STAT_EFF_MIN;

  int16_t base;
  switch (stat) {
    case (uint8_t)BSTAT_ATK: base = (int16_t)c.atk; break;
    case (uint8_t)BSTAT_DEF: base = (int16_t)c.def; break;
    default:                 base = (int16_t)c.spd; break;
  }

  int16_t s = (int16_t)c.stage[stat];
  if (s < (int16_t)BUFF_STAGE_MIN) s = (int16_t)BUFF_STAGE_MIN;
  if (s > (int16_t)BUFF_STAGE_MAX) s = (int16_t)BUFF_STAGE_MAX;

  // Corruption rides OUTSIDE the clamp - measured from sim_engine.py's
  // Fighter.eff(), where a corrupted +2-buffed attacker reaches +3.
  if (c.corrupt_left > 0u) {
    if (stat == (uint8_t)BSTAT_ATK) s = (int16_t)(s + (int16_t)CORRUPT_BATTLE_ATK_STAGE);
    if (stat == (uint8_t)BSTAT_DEF) s = (int16_t)(s + (int16_t)CORRUPT_BATTLE_DEF_STAGE);
  }

  int16_t v = (int16_t)(base + s);
  if (v < (int16_t)STAT_EFF_MIN) v = (int16_t)STAT_EFF_MIN;
  return (uint16_t)v;
}

uint16_t battle_effective_speed(const BattleCombatant& c)
{
  return battle_stat_eff(c, (uint8_t)BSTAT_SPD);
}

bool battle_move_ready(const BattleCombatant& c, uint8_t slot)
{
  if (slot >= (uint8_t)PB_MOVE_COUNT) return false;
  if (c.moves[slot] == 0u) return false;
  if (attack_get(c.moves[slot]) == nullptr) return false;
  return c.cooldown[slot] == 0u;
}

uint8_t battle_alive_count(const BattleState& st, uint8_t side)
{
  if (side > 1u) return 0u;
  uint8_t n = 0u;
  for (uint8_t i = 0; i < (uint8_t)BATTLE_TEAM_MAX; ++i)
    if (combatant_alive(st.side[side].team[i])) ++n;
  return n;
}

bool battle_side_must_switch(const BattleState& st, uint8_t side)
{
  if (side > 1u) return false;
  const BattleSide& s = st.side[side];
  if (s.active >= (uint8_t)BATTLE_TEAM_MAX) return false;
  if (combatant_alive(s.team[s.active])) return false;
  for (uint8_t i = 0; i < (uint8_t)BATTLE_TEAM_MAX; ++i)
    if (i != s.active && combatant_alive(s.team[i])) return true;
  return false;
}

// =============================================================================
//  VALIDATION (spec section 14 step 1). NEVER CLAMPS.
// =============================================================================
BattleReject battle_action_legal_now(const BattleState& st, uint8_t side, BattleAction act)
{
  // (1) THE THREE RANGE CHECKS COME FIRST AND NO ARRAY IS INDEXED UNTIL THEY
  //     ALL PASS. `side`, `kind` and `index` are peer-supplied bytes.
  if (side > 1u) return BR_BAD_SIDE;
  if (act.kind == (uint8_t)BACT_NONE || act.kind >= (uint8_t)BACT_KIND_COUNT) return BR_BAD_KIND;
  if (act.kind == (uint8_t)BACT_ATTACK && act.index >= (uint8_t)PB_MOVE_COUNT) return BR_BAD_INDEX;
  if (act.kind == (uint8_t)BACT_SWITCH && act.index >= (uint8_t)BATTLE_TEAM_MAX) return BR_BAD_INDEX;

  const BattleSide& me = st.side[side];

  // (2) The actor. `active` is state, not wire input, but a state handed in
  //     over the wire is exactly what P4-C5 must survive.
  if (me.active >= (uint8_t)BATTLE_TEAM_MAX) return BR_EMPTY_ACTIVE;
  const BattleCombatant& u = me.team[me.active];
  if ((u.flags & BCF_PRESENT) == 0u) return BR_EMPTY_ACTIVE;

  // (3) A fainted active may do exactly one thing, and only if it can.
  if ((u.flags & BCF_FAINTED) != 0u) {
    if (!battle_side_must_switch(st, side)) return BR_ACTOR_FAINTED;
    if (act.kind != (uint8_t)BACT_SWITCH)    return BR_MUST_SWITCH;
  }

  if (act.kind == (uint8_t)BACT_ATTACK) {
    // ONE guard, not two. 0 is PebbleInstance's empty move slot and
    // attack_get() already answers nullptr for it, exactly as it does for an id
    // past the table - so a separate `moves[index] == 0` test could not fail,
    // and the mutation sweep proved it: deleting it left every case green.
    if (attack_get(u.moves[act.index]) == nullptr)       return BR_UNKNOWN_MOVE;
    if (u.cooldown[act.index] > 0u)                      return BR_MOVE_ON_COOLDOWN;
    return BR_OK;
  }

  // BACT_SWITCH
  if (act.index == me.active) return BR_SWITCH_TO_SELF;
  const BattleCombatant& t = me.team[act.index];
  if ((t.flags & BCF_PRESENT) == 0u) return BR_SWITCH_TO_EMPTY;
  if ((t.flags & BCF_FAINTED) != 0u) return BR_SWITCH_TO_FAINTED;
  return BR_OK;
}

BattleReject battle_validate_action(const BattleState& st, uint8_t side, BattleAction act)
{
  if (st.phase != (uint8_t)BP_RUNNING) return BR_NOT_RUNNING;
  if (st.outcome != (uint8_t)BO_UNDECIDED) return BR_NOT_RUNNING;
  if (side > 1u) return BR_BAD_SIDE;
  // THE FIRST SUBMISSION OF A ROUND WINS. A peer that can resubmit can probe
  // the engine, and it makes lockstep depend on message ordering.
  if (st.side[side].pending_kind != (uint8_t)BACT_NONE) return BR_ALREADY_SUBMITTED;
  return battle_action_legal_now(st, side, act);
}

BattleReject battle_submit_action(BattleState& st, uint8_t side, BattleAction act)
{
  const BattleReject r = battle_validate_action(st, side, act);
  if (r != BR_OK) return r;               // the ONLY return before any write
  st.side[side].pending_kind  = act.kind; // the ONLY two writes in this function
  st.side[side].pending_index = act.index;
  return BR_OK;
}

// =============================================================================
//  INIT
// =============================================================================
void battle_setup_clear(BattleSetup& s)
{
  memset(&s, 0, sizeof s);
  s.engine_ver  = (uint16_t)BATTLE_ENGINE_VER;
  s.content_ver = (uint16_t)CONTENT_VERSION;
}

// COULD THIS SPECIES ACTUALLY HAVE THIS MOVESET? (spec section 67, added by the
// P4-C2/C3 follow-up.) Until then battle_init() checked only that each of the
// four ids resolved through attack_get(), so a peer-supplied team could hand any
// species any of the 34 attacks - including the off-type ones
// data/attacks_table.h's species_learnsets_are_legal() declares impossible.
//
// IT IS DECISIVE AND NOT COSMETIC, measured here rather than assumed: a species-1
// Paketo scripted against a species-17 Exploid, both level 10, 1v1, walking their
// move slots, wins 0 of 200 seeds honestly. Write attack 11 Plaga (CORRUPT,
// power 75) over its slot 0 and it wins 100 of 200. Write the ON-TYPE attack 3
// Rafaga (SIGNAL, power 75) there instead and it wins 103 of 200 - so a rule that
// only checked the move's TYPE would have stopped nothing at all.
//
// THE RULE, and it is the closure of the only writers in the tree rather than a
// guess at intent: game/box.cpp memcpy's sp->moves at creation and
// persistence/migration.cpp memcpy's msp->moves at migration - those two are the
// ONLY places a PebbleInstance.moves[] is ever written - and game/evolution.cpp
// deliberately leaves moves[] alone while moving species_id one stage forward
// inside the same family (data/evolution_table.h static_asserts that a rule
// never crosses a family and never skips a stage). So a legitimate moves[] is
// the VERBATIM learnset, in order, of some species in this one's family at a
// stage no higher than this one's.
//
// NOT strict per-species membership, which was the tempting wrong answer: a
// Paketo (species 1, learnset {1,6,7,27}) that has become a Fragmar still
// carries Paketo's four moves, and species 2's own learnset is {5,27,31,33} - so
// a per-species rule would refuse every evolved Pebble in the box.
static bool moveset_is_learnable(const SpeciesDef& sp, const uint8_t* moves)
{
  // A SPECIES TEACHES ITS OWN LEARNSET (P8-C3). Identical to the line
  // game/validate.cpp's copy carries, and the two are pinned to each other by
  // tests/test_validate.cpp's `the_two_learnset_checkers_agree_on_every_roster_row`
  // - two checkers with the SAME bounds is defence in depth, two with DIFFERENT
  // bounds is the disagreement this project keeps finding. For a built-in row
  // this changes nothing (the walk finds the row itself); for a CREATOR species
  // it is the whole rule, because a custom row has family 0 and no row in
  // SPECIES_TABLE for the walk to match.
  {
    bool same = true;
    for (uint8_t m = 0; m < (uint8_t)PB_MOVE_COUNT; ++m)
      if (moves[m] != sp.moves[m]) { same = false; break; }
    if (same) return true;
  }
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

// Validates one setup slot and hands back BOTH the species row and the derived
// stats, so battle_init() derives once. game/pebble.cpp owns the derivation and
// game/xp.h owns hp_max: this file holds no stat formula at all.
static BattleReject setup_member_ok(const PebbleInstance& p,
                                    const SpeciesDef*& sp_out, PebbleStats& stats_out)
{
  sp_out = nullptr;
  memset(&stats_out, 0, sizeof stats_out);
  if (p.species_id == 0u || p.id == 0u) return BR_NULL_MEMBER;
  const SpeciesDef* sp = species_get(p.species_id);
  if (sp == nullptr) return BR_UNKNOWN_SPECIES;
  if (p.level < 1u || p.level > (uint8_t)PB_LEVEL_MAX) return BR_BAD_LEVEL;
  for (uint8_t m = 0; m < (uint8_t)PB_MOVE_COUNT; ++m)
    if (attack_get(p.moves[m]) == nullptr) return BR_ILLEGAL_MOVESET;
  // Four real attacks is not four attacks THIS Pebble could know.
  if (!moveset_is_learnable(*sp, p.moves)) return BR_UNLEARNABLE_MOVE;

  PebbleStats stats;
  pebble_derive_stats(*sp, p.level, p.genome, stats);
  if (p.hp_cur > stats.hp_max) return BR_HP_OVER_MAX;
  if (p.hp_cur == 0u || (p.status & PBS_FAINTED) != 0u) return BR_MEMBER_FAINTED;
  sp_out    = sp;
  stats_out = stats;
  return BR_OK;
}

BattleReject battle_init(BattleState& st, const BattleSetup& setup)
{
  // memset FIRST and memset again on refusal: the whole object representation,
  // padding included, must be defined before the hash reads it and before a
  // rejection test memcmps it.
  memset(&st, 0, sizeof st);

  if (setup.engine_ver != (uint16_t)BATTLE_ENGINE_VER ||
      setup.content_ver != (uint16_t)CONTENT_VERSION) {
    return BR_VERSION_MISMATCH;
  }

  for (uint8_t s = 0; s < 2u; ++s) {
    if (setup.count[s] < 1u || setup.count[s] > (uint8_t)BATTLE_TEAM_MAX) {
      memset(&st, 0, sizeof st);
      return BR_TEAM_SIZE;
    }
  }

  for (uint8_t s = 0; s < 2u; ++s) {
    for (uint8_t i = 0; i < setup.count[s]; ++i) {
      const PebbleInstance& p = setup.member[s][i];
      const SpeciesDef* sp = nullptr;
      PebbleStats stats;
      const BattleReject r = setup_member_ok(p, sp, stats);
      if (r != BR_OK) { memset(&st, 0, sizeof st); return r; }

      // A peer sending one Pebble three times is exactly the lie to refuse
      // (spec section 9). The ids are compared and then FORGOTTEN: the engine
      // holds rules state and nothing else, so P4-C4 keeps its own box_slot[]
      // beside it for write-back.
      for (uint8_t s2 = 0; s2 <= s; ++s2) {
        const uint8_t upper = (s2 == s) ? i : setup.count[s2];
        for (uint8_t j = 0; j < upper; ++j) {
          if (setup.member[s2][j].id == p.id) { memset(&st, 0, sizeof st); return BR_DUPLICATE_ID; }
        }
      }

      BattleCombatant& c = st.side[s].team[i];
      c.hp_max         = stats.hp_max;
      c.hp_cur         = p.hp_cur;
      c.species_id     = p.species_id;
      c.level          = p.level;
      c.type           = sp->type;
      c.atk            = stats.atk;
      c.def            = stats.def;
      c.spd            = stats.spd;
      c.flags          = BCF_PRESENT;
      c.type_edge_left = (uint8_t)TYPE_MOD_MAX_HITS;
      // THE STORED CORRUPTION ENTERS THE FIGHT HERE, AND THIS IS THE ONLY LINE
      // THAT WAS MISSING (P9-C5). battle_stat_eff()'s +1 ATK / -1 DEF has read
      // corrupt_left since P4-C2 and nothing ever set it from the Pebble, so a
      // creature that was corrupted out of battle walked into one cured.
      //
      // ONE ASSIGNMENT, NOT AN ADDITION, and that is the no-stacking rule in
      // its cheapest form: re-initialising the same battle twice, or entering a
      // second battle, writes the same value rather than accumulating one.
      // Everything downstream reads `corrupt_left > 0` as a boolean, so the
      // stat effect is +1/-1 however this field got here.
      //
      // corrupt_until_epoch IS NOT READ. The engine counts rounds and holds no
      // clock; whether the deadline has passed is app/app.cpp's question and it
      // answers it once a second through cor_service().
      c.corrupt_left   = ((p.status & (uint8_t)PBS_CORRUPTED) != 0u)
                             ? (uint8_t)CORRUPT_BATTLE_ROUNDS : 0u;
      for (uint8_t m = 0; m < (uint8_t)PB_MOVE_COUNT; ++m) c.moves[m] = p.moves[m];
      // hp_max/atk/def/spd are written HERE and never again: level and genome
      // cannot move mid-battle, so the cache is provably constant.
    }
    st.side[s].active        = 0u;
    st.side[s].pending_kind  = (uint8_t)BACT_NONE;
    st.side[s].pending_index = 0u;
  }

  rng_init(st.rng, setup.seed);
  st.round   = 1u;      // the round about to be resolved, 1-based
  st.phase   = (uint8_t)BP_RUNNING;
  st.outcome = (uint8_t)BO_UNDECIDED;
  return BR_OK;
}

// =============================================================================
//  STEP 1 - validate action
//
//  A failure HERE is not a rejection, it is an INVARIANT VIOLATION: the state
//  moved between submit and resolve, which is exactly what a desynced peer's
//  action looks like in P4-C5. The driver turns it into BO_ABORT.
// =============================================================================
bool battle_s1_validate_action(const BattleState& st)
{
  for (uint8_t s = 0; s < 2u; ++s) {
    BattleAction a;
    a.kind  = st.side[s].pending_kind;
    a.index = st.side[s].pending_index;
    if (battle_action_legal_now(st, s, a) != BR_OK) return false;
  }
  return true;
}

// =============================================================================
//  STEP 2 - determine priority
// =============================================================================
int8_t battle_s2_priority(const BattleState& st, uint8_t side)
{
  if (side > 1u) return 0;
  const BattleSide& me = st.side[side];
  if (me.pending_kind == (uint8_t)BACT_SWITCH) return (int8_t)BATTLE_SWITCH_PRIORITY;
  if (me.pending_kind != (uint8_t)BACT_ATTACK) return 0;
  if (me.active >= (uint8_t)BATTLE_TEAM_MAX) return 0;
  if (me.pending_index >= (uint8_t)PB_MOVE_COUNT) return 0;
  const AttackDef* a = attack_get(me.team[me.active].moves[me.pending_index]);
  return (a != nullptr) ? a->priority : (int8_t)0;
}

// RANGE FIRST, THEN INDEX - APPLIED TO THE ACTIVE SLOT TOO, which four of the
// nine steps did not do until the P4-C2/C3 review. BattleState.side[s].active is
// a byte like any other; battle.h advertises the nine steps as individually
// callable so a host test can hash before and after; and P4-C5 hands in state
// this engine did not build. A state carrying active == 3 therefore reached
// st.side[s].team[3].
//
// MEASURED, NOT ARGUED: with side[1].active = 3, battle_s3_determine_order()
// under -fsanitize=address is a stack-buffer-overflow READ past the end of a
// 212-byte BattleState, reported inside battle_stat_eff(); battle_s7's status
// tick reaches the same slot.
//
// NOT REACHABLE THROUGH battle_step_round(), and this comment does not pretend
// otherwise: step 1 re-validates both pending actions against the state as it
// stands, battle_action_legal_now() refuses an out-of-range active by name with
// BR_EMPTY_ACTIVE, and the driver turns that into BO_ABORT before step 3 runs.
//
// NO TEST CLAIMS THESE GUARDS, and they are labelled rather than left looking
// proven - the treatment this file gives its DMG_MIN floor. A normal build
// cannot tell the difference, because what they prevent is undefined behaviour
// and not a wrong answer; the sanitizer run above is the whole evidence.
static inline bool active_in_range(const BattleSide& s)
{
  return s.active < (uint8_t)BATTLE_TEAM_MAX;
}

// =============================================================================
//  STEP 3 - determine effective speed, and therefore the order.
//  THE ONLY DRAW OUTSIDE steps 4 and 5, and it happens only on a double tie.
// =============================================================================
uint8_t battle_s3_determine_order(BattleState& st, int8_t pri_a, int8_t pri_b)
{
  if (pri_a > pri_b) return 0u;
  if (pri_b > pri_a) return 1u;

  // Side 0 first is an arbitrary but DEFINED answer, and no draw is taken: a
  // state with no active fighter has no speeds to compare and must not spend a
  // number from the shared stream deciding nothing.
  if (!active_in_range(st.side[0]) || !active_in_range(st.side[1])) return 0u;

  const BattleCombatant& a = st.side[0].team[st.side[0].active];
  const BattleCombatant& b = st.side[1].team[st.side[1].active];
  const uint16_t sa = battle_effective_speed(a);
  const uint16_t sb = battle_effective_speed(b);
  if (sa > sb) return 0u;
  if (sb > sa) return 1u;
  return (uint8_t)rng_next_below(st.rng, 2u);
}

// =============================================================================
//  RESOLUTION - one rule, shared by steps 4 and 5
// =============================================================================
static void hp_take(BattleCombatant& c, uint16_t dmg)
{
  c.hp_cur = (uint16_t)((dmg >= c.hp_cur) ? 0u : (c.hp_cur - dmg));   // saturating
}

static void hp_give(BattleCombatant& c, uint16_t heal)
{
  const uint32_t v = (uint32_t)c.hp_cur + (uint32_t)heal;
  c.hp_cur = (uint16_t)((v > c.hp_max) ? c.hp_max : v);
}

static void set_stage(BattleCombatant& c, uint8_t k, int16_t delta, uint8_t dur,
                      uint8_t side, uint8_t slot, uint16_t round, BattleLog* log)
{
  if (k >= (uint8_t)BSTAT_COUNT) return;
  int16_t s = (int16_t)((int16_t)c.stage[k] + delta);
  if (s < (int16_t)BUFF_STAGE_MIN) s = (int16_t)BUFF_STAGE_MIN;
  if (s > (int16_t)BUFF_STAGE_MAX) s = (int16_t)BUFF_STAGE_MAX;
  c.stage[k] = (int8_t)s;
  // A 0-duration stage would be a permanent one, which is the simulator's
  // behaviour and NOT this engine's; the pack ships none, and one round is the
  // honest floor.
  c.stage_left[k] = (dur > 0u) ? dur : 1u;
  log_push(log, (uint8_t)RLE_STAGE, side, slot, k,
           (uint16_t)((int16_t)BATTLE_STAGE_BIAS + s), round, 0u);
}

// THE ACCURACY RULE, and the ONE copy of it in the tree (data/balance.h names
// the constants). SPD buys evasion, capped at EVASION_MAX_SPD_GAP and floored
// at ACCURACY_MIN; a power-0 utility move is not evaded at all.
uint8_t battle_accuracy_eff(const BattleCombatant& u, const BattleCombatant& f,
                            const AttackDef& a)
{
  int16_t acc = (int16_t)a.accuracy;
  if (a.power > 0u) {
    const int16_t gap_raw = (int16_t)((int16_t)battle_effective_speed(f) -
                                      (int16_t)battle_effective_speed(u));
    int16_t gap = (gap_raw > 0) ? gap_raw : (int16_t)0;
    if (gap > (int16_t)EVASION_MAX_SPD_GAP) gap = (int16_t)EVASION_MAX_SPD_GAP;
    acc = (int16_t)(acc - (int16_t)((int16_t)EVASION_PER_SPD * gap));
    if (acc < (int16_t)ACCURACY_MIN) acc = (int16_t)ACCURACY_MIN;
  }
  // Both ends fit a byte without a clamp: attack_rows_are_well_formed() caps
  // accuracy at 100 and ACCURACY_MIN is the floor, so no value here is lost.
  return (uint8_t)acc;
}

// THE DAMAGE RULE, in the order the code reads it, and the ONE copy of it in
// the tree: game/battle_ai.cpp predicts a hit by calling this same function.
// uint32_t intermediates, no float, no 64-bit. Widest reachable value:
// power 100 * atk_eff 25 = 2,500, raw <= 357 and raw * 5 <= 1,785 (measured
// against the shipped tables).
uint16_t battle_damage_pre_roll(const BattleCombatant& u, const BattleCombatant& f,
                                const AttackDef& a, int8_t m)
{
  const uint32_t atk_eff = battle_stat_eff(u, (uint8_t)BSTAT_ATK);
  const uint32_t def_eff = battle_stat_eff(f, (uint8_t)BSTAT_DEF);
  // def_eff >= STAT_EFF_MIN == 1 and BATTLE_K > 0 is static_asserted, so the
  // divisor cannot be zero.
  uint32_t raw = ((uint32_t)a.power * atk_eff) / (def_eff * (uint32_t)BATTLE_K);
  // data/balance.h publishes BOTH floors, and under the shipped TYPE_MUL table
  // this one is UNOBSERVABLE: every multiplier maps 0 to 0, so the second floor
  // below already answers DMG_MIN wherever this one would have. The mutation
  // sweep confirmed it - deleting this line changes no result and no test can
  // tell. It stays because it is the published formula and because a future
  // multiplier could separate them; nothing here claims it is tested.
  //
  // THE SECOND FLOOR IS A DIFFERENT MATTER, and the P4-C2/C3 review found this
  // comment being read as though it covered both. It is OBSERVABLE and it is
  // tested: raw 1 times the 4/5 disadvantage multiplier is 0 in integers, so
  // without it a disadvantaged minimum hit deals literally nothing.
  // damage_never_falls_below_one_and_never_wraps_past_zero asserts exactly that,
  // because until then every case reached only a NEUTRAL matchup where TYPE_MUL
  // is 1/1 and the two floors were indistinguishable - so each was masked by the
  // other and deleting either alone was green.
  if (raw < (uint32_t)DMG_MIN) raw = (uint32_t)DMG_MIN;

  // Defence on a PUBLIC function that indexes a three-entry table. type_mod_of()
  // answers -1..+1 and neither caller in this tree can produce anything else, so
  // this is not a clamp of an untrusted value - it is the difference between a
  // wrong number and an undefined read if a future caller gets it wrong.
  if (m < -1 || m > 1) m = 0;

  uint32_t dmg = (raw * (uint32_t)TYPE_MUL_NUM[m + 1]) / (uint32_t)TYPE_MUL_DEN[m + 1];
  if (dmg < (uint32_t)DMG_MIN) dmg = (uint32_t)DMG_MIN;
  return (uint16_t)dmg;
}

// `spent` reports the modifier this call ACTUALLY applied - 0 when there was no
// type relation or when the cap had already been used up. An out-parameter
// rather than a second call to type_mod_of() at the call site: the cap is spent
// here, so only here knows whether it was.
static uint16_t compute_damage(BattleState& st, BattleCombatant& u,
                               const BattleCombatant& f, const AttackDef& a,
                               int8_t* spent)
{
  if (spent != nullptr) *spent = 0;
  int8_t m = type_mod_of(a.type, f.type);
  if (m != 0) {
    // The cap zeroes a DISADVANTAGE as well as an advantage (sim_engine.py
    // take_turn: `if m != 0`), it is spent PER ATTACKING COMBATANT, and it is
    // spent only here - after the accuracy roll and only for power > 0 - so a
    // miss does not spend it and a utility move does not spend it. THE SPEND IS
    // THE ENGINE'S ALONE: battle_damage_pre_roll() only reads the modifier it is
    // handed, which is what lets the AI predict a hit without paying for one.
    if (u.type_edge_left == 0u) m = 0;
    else {
      u.type_edge_left = (uint8_t)(u.type_edge_left - 1u);
      if (spent != nullptr) *spent = m;
    }
  }
  uint32_t dmg = (uint32_t)battle_damage_pre_roll(u, f, a, m);

  dmg += rng_next_below(st.rng, (uint32_t)DMG_RNG_SPAN);

  // AFTER the roll, matching the simulator: halving BEFORE it would let the
  // roll add up to 2 back on top, and a protected Pebble could take more than
  // half of what it would otherwise have taken.
  if (f.protect_left > 0u) {
    dmg = dmg / (uint32_t)PROTECT_DIVISOR;
    if (dmg < (uint32_t)DMG_MIN) dmg = (uint32_t)DMG_MIN;
  }
  return (uint16_t)dmg;
}

static void apply_effect(BattleState& st, uint8_t side, const AttackDef& a,
                         uint16_t dealt, BattleLog* log)
{
  BattleSide& me  = st.side[side];
  BattleSide& you = st.side[side ^ 1u];
  BattleCombatant& u = me.team[me.active];
  BattleCombatant& f = you.team[you.active];
  const uint8_t us = me.active;
  const uint8_t fs = you.active;
  const uint8_t osd = (uint8_t)(side ^ 1u);
  const uint16_t rd = st.round;
  const uint8_t  v  = a.effect_value;
  const uint8_t  d  = a.effect_duration;

  switch (a.effect) {
    case (uint8_t)ATK_EFF_NONE: break;
    case (uint8_t)ATK_EFF_BUFF_ATK:   set_stage(u, (uint8_t)BSTAT_ATK, (int16_t)v,  d, side, us, rd, log); break;
    case (uint8_t)ATK_EFF_BUFF_DEF:   set_stage(u, (uint8_t)BSTAT_DEF, (int16_t)v,  d, side, us, rd, log); break;
    case (uint8_t)ATK_EFF_BUFF_SPD:   set_stage(u, (uint8_t)BSTAT_SPD, (int16_t)v,  d, side, us, rd, log); break;
    case (uint8_t)ATK_EFF_DEBUFF_ATK: set_stage(f, (uint8_t)BSTAT_ATK, (int16_t)-v, d, osd,  fs, rd, log); break;
    case (uint8_t)ATK_EFF_DEBUFF_DEF: set_stage(f, (uint8_t)BSTAT_DEF, (int16_t)-v, d, osd,  fs, rd, log); break;
    case (uint8_t)ATK_EFF_DEBUFF_SPD: set_stage(f, (uint8_t)BSTAT_SPD, (int16_t)-v, d, osd,  fs, rd, log); break;
    case (uint8_t)ATK_EFF_SELF_DEBUFF_DEF:
      set_stage(u, (uint8_t)BSTAT_DEF, (int16_t)-v, d, side, us, rd, log); break;
    case (uint8_t)ATK_EFF_PROTECT_HALF:
      u.protect_left = (d > 0u) ? d : 1u;
      log_push(log, (uint8_t)RLE_PROTECT, side, us, 0u, u.protect_left, rd, 0u);
      break;
    case (uint8_t)ATK_EFF_HEAL_PCT:
      hp_give(u, (uint16_t)(((uint32_t)u.hp_max * (uint32_t)v) / 100u));
      log_push(log, (uint8_t)RLE_HP, side, us, 0u, u.hp_cur, rd, 0u);
      break;
    case (uint8_t)ATK_EFF_DRAIN_PCT:
      hp_give(u, (uint16_t)(((uint32_t)dealt * (uint32_t)v) / 100u));
      log_push(log, (uint8_t)RLE_HP, side, us, 0u, u.hp_cur, rd, 0u);
      break;
    case (uint8_t)ATK_EFF_RECOIL_PCT: {
      uint32_t r = ((uint32_t)dealt * (uint32_t)v) / 100u;
      if (r < 1u) r = 1u;                       // recoil always costs something
      hp_take(u, (uint16_t)r);
      log_push(log, (uint8_t)RLE_HP, side, us, 0u, u.hp_cur, rd, 0u);
      break;
    }
    case (uint8_t)ATK_EFF_SELF_STUN:
      // The ONE duration the status tick does not own: it is armed at the end
      // of the user's own turn, so a step-7 decrement in the same round would
      // expire it before it ever skipped anything. It is spent by the action it
      // prevents (sim_engine.py take_turn), and battle.h records the divergence.
      u.stun_left = (d > 0u) ? d : 1u;
      log_push(log, (uint8_t)RLE_STUN, side, us, 0u, u.stun_left, rd, 0u);
      break;
    case (uint8_t)ATK_EFF_DOT:
      f.dot_value = v;                          // refreshes, never stacks
      f.dot_left  = (d > 0u) ? d : 1u;
      log_push(log, (uint8_t)RLE_DOT, osd, fs, 0u, f.hp_cur, rd, 0u);
      break;
    case (uint8_t)ATK_EFF_EFF_CORRUPT:
      if (d > f.corrupt_left) f.corrupt_left = d;
      log_push(log, (uint8_t)RLE_CORRUPT, osd, fs, 0u, f.corrupt_left, rd, 0u);
      break;
    case (uint8_t)ATK_EFF_CLEANSE:
      for (uint8_t k = 0; k < (uint8_t)BSTAT_COUNT; ++k) {
        if (u.stage[k] < 0) { u.stage[k] = 0; u.stage_left[k] = 0u; }
      }
      u.dot_value    = 0u;
      u.dot_left     = 0u;
      u.corrupt_left = 0u;
      log_push(log, (uint8_t)RLE_CLEANSE, side, us, 0u, 0u, rd, 0u);
      break;
    default: break;
  }
}

// Adding an effect to the content without handling it here must FAIL THE BUILD
// rather than be silently ignored.
static_assert((int)ATK_EFF_COUNT == 16,
              "AttackEffect grew: the switch in battle.cpp::apply_effect handles 16 "
              "slots and a new one would resolve as 'nothing happened'");

// Switching costs the turn. The incoming fighter's stages, protection and stun
// belong to the POSITION and are cleared; its DOT, corruption, cooldowns and
// type-edge budget ride the CREATURE and are kept, because clearing them would
// make a switch a free cure, a free cooldown refresh and a free second edge.
static void resolve_switch(BattleState& st, uint8_t side, uint8_t to, BattleLog* log)
{
  BattleSide& me = st.side[side];
  BattleCombatant& out = me.team[me.active];
  for (uint8_t k = 0; k < (uint8_t)BSTAT_COUNT; ++k) { out.stage[k] = 0; out.stage_left[k] = 0u; }
  out.protect_left = 0u;
  out.stun_left    = 0u;

  me.active = to;

  BattleCombatant& in = me.team[to];
  for (uint8_t k = 0; k < (uint8_t)BSTAT_COUNT; ++k) { in.stage[k] = 0; in.stage_left[k] = 0u; }
  in.protect_left = 0u;
  in.stun_left    = 0u;

  log_push(log, (uint8_t)RLE_SWITCH, side, to, 0u, in.hp_cur, st.round, 0u);
}

static void resolve_one(BattleState& st, uint8_t side, BattleLog* log)
{
  BattleSide& me  = st.side[side];
  BattleSide& you = st.side[side ^ 1u];
  // See active_in_range() above: unreachable through the driver, untested, and
  // here because this function indexes three caller-supplied slots.
  if (!active_in_range(me) || !active_in_range(you)) return;

  if (me.pending_kind == (uint8_t)BACT_SWITCH) {
    if (me.pending_index >= (uint8_t)BATTLE_TEAM_MAX) return;
    resolve_switch(st, side, me.pending_index, log);
    return;                                        // 0 draws
  }

  BattleCombatant& u = me.team[me.active];
  if (u.stun_left > 0u) {
    u.stun_left = (uint8_t)(u.stun_left - 1u);     // the skipped turn IS the tick
    log_push(log, (uint8_t)RLE_SKIPPED, side, me.active, (uint8_t)BSK_STUNNED,
             u.stun_left, st.round, 0u);
    return;                                        // 0 draws
  }

  const AttackDef* a = attack_get(u.moves[me.pending_index]);
  if (a == nullptr) return;                        // step 1 already refused this
  BattleCombatant& f = you.team[you.active];

  // Armed BEFORE the accuracy roll, so a miss still costs the cooldown, and
  // +1 because step 7 decrements it in this same round (sim_engine.py).
  if (a->cooldown > 0u) {
    const uint16_t cd = (uint16_t)((uint16_t)a->cooldown + 1u);
    u.cooldown[me.pending_index] = (uint8_t)((cd > 255u) ? 255u : cd);
  }

  // A power-0 utility move rolls against RAW accuracy with no evasion - and it
  // STILL ROLLS, which is why every non-stunned, non-switch action costs
  // exactly one accuracy draw.
  const uint32_t acc  = (uint32_t)battle_accuracy_eff(u, f, *a);
  const uint32_t roll = rng_next_below(st.rng, (uint32_t)ACCURACY_ROLL_SPAN);
  if (roll >= acc) {
    log_push(log, (uint8_t)RLE_MISS, side, me.active, me.pending_index, 0u, st.round, 0u);
    return;
  }

  uint16_t dealt = 0u;
  if (a->power > 0u) {
    int8_t edge = 0;
    dealt = compute_damage(st, u, f, *a, &edge);
    hp_take(f, dealt);
    // BEFORE the hit, so a transcript reads "the weakness was exploited" and
    // then the number it produced. b is what is LEFT, which with
    // TYPE_MOD_MAX_HITS at 1 is always 0 - the field is there so the day the cap
    // moves the log says so without this line changing.
    if (edge != 0) {
      log_push(log, (uint8_t)RLE_TYPE_EDGE, side, me.active,
               (uint8_t)((edge > 0) ? 1u : 2u), (uint16_t)u.type_edge_left,
               st.round, 0u);
    }
    log_push(log, (uint8_t)RLE_HIT, side, me.active, me.pending_index, dealt, st.round, 0u);
    log_push(log, (uint8_t)RLE_HP, (uint8_t)(side ^ 1u), you.active, 0u, f.hp_cur, st.round, 0u);
  } else {
    log_push(log, (uint8_t)RLE_HIT, side, me.active, me.pending_index, 0u, st.round, 0u);
  }
  apply_effect(st, side, *a, dealt, log);
}

// =============================================================================
//  STEPS 4 AND 5
//
//  Both delegate to resolve_one() so there is ONE resolution rule. They differ
//  only in step 5's corpse precondition, which is precisely why spec section 14
//  numbers them apart.
//
//  STEP 4 NEEDS NO CORPSE CHECK, and the argument is a static_assert rather
//  than a hope: at the top of a round, step 1 has already forced a side whose
//  active is fainted to submit BACT_SWITCH, and a switch resolves at
//  BATTLE_SWITCH_PRIORITY, which battle_switch_outruns_every_attack() proves is
//  above every move in the pack. So the first action is either that switch, or
//  an action by a side whose active is alive against a side whose active is
//  alive.
// =============================================================================
void battle_s4_resolve_first(BattleState& st, uint8_t side, BattleLog* log)
{
  if (side > 1u) return;
  resolve_one(st, side, log);
}

void battle_s5_resolve_second(BattleState& st, uint8_t side, BattleLog* log)
{
  if (side > 1u) return;
  const BattleSide& me  = st.side[side];
  const BattleSide& you = st.side[side ^ 1u];
  if (!active_in_range(me) || !active_in_range(you)) return;   // active_in_range()

  // sim_engine.py's `if not (u.alive and f.alive): break`, expressed as a rule
  // and LOGGED rather than silently dropped. It covers both "the first mover
  // killed the second" and "the first mover killed itself with recoil".
  if (!combatant_alive(me.team[me.active])) {
    log_push(log, (uint8_t)RLE_SKIPPED, side, me.active, (uint8_t)BSK_ACTOR_FAINTED,
             0u, st.round, 0u);
    return;
  }
  if (!combatant_alive(you.team[you.active])) {
    log_push(log, (uint8_t)RLE_SKIPPED, side, me.active, (uint8_t)BSK_TARGET_FAINTED,
             0u, st.round, 0u);
    return;
  }
  resolve_one(st, side, log);
}

// =============================================================================
//  STEP 6 - process fainting. IDEMPOTENT, which is the property step 8 leans on.
// =============================================================================
void battle_s6_process_fainting(BattleState& st, BattleLog* log)
{
  for (uint8_t s = 0; s < 2u; ++s) {
    for (uint8_t i = 0; i < (uint8_t)BATTLE_TEAM_MAX; ++i) {
      BattleCombatant& c = st.side[s].team[i];
      if ((c.flags & BCF_PRESENT) == 0u) continue;
      if ((c.flags & BCF_FAINTED) != 0u) continue;
      if (c.hp_cur != 0u) continue;
      c.flags = (uint8_t)(c.flags | BCF_FAINTED);
      log_push(log, (uint8_t)RLE_FAINT, s, i, 0u, 0u, st.round, 0u);
    }
  }
}

// =============================================================================
//  STEP 7 - process status effects.
//
//  DOT damage plus every duration decrement, on the TWO ACTIVE fighters only
//  (sim_engine.py calls end_of_round(A); end_of_round(B)). A benched Pebble's
//  cooldowns therefore do not tick - they ride the creature, not the clock.
//  ZERO DRAWS: DOT and corruption are deterministic in the tuned simulator, and
//  battle.h records why no infection roll exists here.
// =============================================================================
static void tick_status(BattleState& st, uint8_t side, BattleLog* log)
{
  BattleSide& me = st.side[side];
  if (!active_in_range(me)) return;                             // active_in_range()
  BattleCombatant& c = me.team[me.active];

  if (c.dot_left > 0u) {
    hp_take(c, c.dot_value);
    log_push(log, (uint8_t)RLE_DOT, side, me.active, c.dot_value, c.hp_cur, st.round, 0u);
    c.dot_left = (uint8_t)(c.dot_left - 1u);
    if (c.dot_left == 0u) c.dot_value = 0u;
  }
  if (c.corrupt_left > 0u) {
    c.corrupt_left = (uint8_t)(c.corrupt_left - 1u);
    log_push(log, (uint8_t)RLE_CORRUPT, side, me.active, 0u, c.corrupt_left, st.round, 0u);
  }
  if (c.protect_left > 0u) {
    c.protect_left = (uint8_t)(c.protect_left - 1u);
    log_push(log, (uint8_t)RLE_PROTECT, side, me.active, 0u, c.protect_left, st.round, 0u);
  }
  for (uint8_t k = 0; k < (uint8_t)BSTAT_COUNT; ++k) {
    if (c.stage_left[k] == 0u) continue;
    c.stage_left[k] = (uint8_t)(c.stage_left[k] - 1u);
    if (c.stage_left[k] == 0u) {
      c.stage[k] = 0;
      log_push(log, (uint8_t)RLE_STAGE, side, me.active, k,
               (uint16_t)BATTLE_STAGE_BIAS, st.round, 0u);
    }
  }
  for (uint8_t i = 0; i < (uint8_t)PB_MOVE_COUNT; ++i) {
    if (c.cooldown[i] > 0u) c.cooldown[i] = (uint8_t)(c.cooldown[i] - 1u);
  }
}

void battle_s7_process_status(BattleState& st, BattleLog* log)
{
  tick_status(st, 0u, log);
  tick_status(st, 1u, log);
}

// =============================================================================
//  STEP 8 - determine end of round.
//
//  THE SECOND CALL TO STEP 6 IS LOAD-BEARING AND IS NOT REDUNDANT. Spec section
//  14 numbers fainting (6) BEFORE status (7), and step 7's DOT is the only thing
//  in the status tick that deals damage - so without this line a Pebble killed
//  by poison sits at 0 HP and is NEVER MARKED FAINTED: the BCF_FAINTED flag that
//  P4-C5 compares between peers goes missing from the hashed state, and the
//  transcript loses its FAINT event.
//
//  IT DOES NOT COST THE VICTORY, and the first version of this comment said it
//  did - a sentence wider than the tree, corrected by the P4-C2/C3 review.
//  combatant_alive() requires hp_cur > 0 as well as the flag, so
//  battle_alive_count() and step 9 already count a 0-HP unflagged Pebble as
//  dead. Deleting this line fails exactly two checks of
//  a_dot_kills_on_the_status_tick_and_the_second_faint_pass_is_what_sees_it -
//  the flag and the single FAINT event - and the outcome check beside them still
//  passes. Do not delete it as duplicated work; do not defend it with the wrong
//  reason either.
// =============================================================================
void battle_s8_end_of_round(BattleState& st, BattleLog* log)
{
  battle_s6_process_fainting(st, log);
  st.side[0].pending_kind  = (uint8_t)BACT_NONE;
  st.side[0].pending_index = 0u;
  st.side[1].pending_kind  = (uint8_t)BACT_NONE;
  st.side[1].pending_index = 0u;
  if (st.round < 0xFFFFu) st.round = (uint16_t)(st.round + 1u);
}

// =============================================================================
//  STEP 9 - check victory. CONST: it answers a question, it does not decide.
// =============================================================================
BattleOutcome battle_s9_check_victory(const BattleState& st)
{
  const uint8_t alive_a = battle_alive_count(st, 0u);
  const uint8_t alive_b = battle_alive_count(st, 1u);
  if (alive_a == 0u && alive_b == 0u) return BO_DRAW;
  if (alive_a == 0u) return BO_WIN_B;
  if (alive_b == 0u) return BO_WIN_A;

  if (st.round > (uint16_t)BATTLE_MAX_ROUNDS) {
    // Remaining HP as a FRACTION of each side's maximum, cross-multiplied so no
    // float and no ratio is needed (data/balance.h). Fainted members contribute
    // 0 HP and their full hp_max, which correctly penalises the side that lost
    // Pebbles. An exact tie is an honest DRAW - there is no coin flip in this
    // engine, at the cap or on a double KO.
    uint32_t hp_a = 0u, max_a = 0u, hp_b = 0u, max_b = 0u;
    for (uint8_t i = 0; i < (uint8_t)BATTLE_TEAM_MAX; ++i) {
      const BattleCombatant& a = st.side[0].team[i];
      const BattleCombatant& b = st.side[1].team[i];
      if ((a.flags & BCF_PRESENT) != 0u) { hp_a += a.hp_cur; max_a += a.hp_max; }
      if ((b.flags & BCF_PRESENT) != 0u) { hp_b += b.hp_cur; max_b += b.hp_max; }
    }
    // Widest reachable product with the shipped roster: base_hp 9 gives
    // hp_max 58 at level 30, so a three-Pebble side sums to at most 174 and the
    // product to 30,276. uint32_t holds far more, so a hand-edited content pack
    // cannot overflow it either.
    const uint32_t fa = hp_a * max_b;
    const uint32_t fb = hp_b * max_a;
    if (fa > fb) return BO_WIN_A;
    if (fb > fa) return BO_WIN_B;
    return BO_DRAW;
  }
  return BO_UNDECIDED;
}

// =============================================================================
//  THE DRIVER. Its body is spec section 14's nine steps, in order, and nothing
//  else. tools/check.sh greps the digits back out of it.
// =============================================================================
BattleStepResult battle_step_round(BattleState& st, BattleLog* log)
{
  if (st.phase != (uint8_t)BP_RUNNING) return BS_BATTLE_OVER;
  if (st.outcome != (uint8_t)BO_UNDECIDED) return BS_BATTLE_OVER;
  // Bit-identical exit: nothing above this line writes, and nothing below it
  // runs until both sides have submitted.
  if (st.side[0].pending_kind == (uint8_t)BACT_NONE ||
      st.side[1].pending_kind == (uint8_t)BACT_NONE) return BS_NEED_ACTIONS;

  log_push(log, (uint8_t)RLE_ROUND_BEGIN, 0xFFu, 0xFFu, 0u, 0u, st.round,
           battle_state_hash(st));
  for (uint8_t s = 0; s < 2u; ++s) {
    log_push(log, (uint8_t)RLE_ACTION, s, st.side[s].active, st.side[s].pending_kind,
             st.side[s].pending_index, st.round, 0u);
  }

  if (!battle_s1_validate_action(st)) {
    st.outcome = (uint8_t)BO_ABORT;
    log_push(log, (uint8_t)RLE_BATTLE_END, 0xFFu, 0xFFu, (uint8_t)BO_ABORT, 0u, st.round, 0u);
    return BS_BATTLE_OVER;
  }
  const int8_t  pri_a = battle_s2_priority(st, 0u);
  const int8_t  pri_b = battle_s2_priority(st, 1u);
  const uint8_t first = battle_s3_determine_order(st, pri_a, pri_b);
  log_push(log, (uint8_t)RLE_ORDER, first, st.side[first].active, first,
           battle_effective_speed(st.side[first].team[st.side[first].active]),
           st.round, 0u);
  battle_s4_resolve_first(st, first, log);
  battle_s5_resolve_second(st, (uint8_t)(first ^ 1u), log);
  battle_s6_process_fainting(st, log);
  battle_s7_process_status(st, log);
  battle_s8_end_of_round(st, log);
  st.outcome = (uint8_t)battle_s9_check_victory(st);

  log_push(log, (uint8_t)RLE_ROUND_END, 0xFFu, 0xFFu, 0u, 0u,
           (uint16_t)(st.round - 1u), battle_state_hash(st));
  if (st.outcome != (uint8_t)BO_UNDECIDED) {
    log_push(log, (uint8_t)RLE_BATTLE_END, 0xFFu, 0xFFu, st.outcome, 0u,
             (uint16_t)(st.round - 1u), 0u);
    return BS_BATTLE_OVER;
  }
  return BS_ROUND_DONE;
}

// =============================================================================
//  REPLAY
// =============================================================================
BattleReject battle_replay(const BattleSetup& setup, const BattleLog& src,
                           BattleState& out, BattleLog* log,
                           BattleReplayReport& rep)
{
  memset(&rep, 0, sizeof rep);
  memset(&out, 0, sizeof out);

  if (setup.engine_ver != (uint16_t)BATTLE_ENGINE_VER ||
      setup.content_ver != (uint16_t)CONTENT_VERSION) return BR_VERSION_MISMATCH;
  // A ring that dropped events is not a transcript, and replaying from one
  // would silently reproduce a DIFFERENT battle.
  if (src.dropped != 0u) return BR_LOG_INCOMPLETE;

  const BattleReject r = battle_init(out, setup);
  if (r != BR_OK) return r;

  uint16_t i = 0u;
  while (out.outcome == (uint8_t)BO_UNDECIDED && out.phase == (uint8_t)BP_RUNNING) {
    const uint16_t round = out.round;

    // Collect this round's two recorded actions and the recorded hashes.
    BattleAction act[2];
    act[0].kind = (uint8_t)BACT_NONE; act[0].index = 0u;
    act[1].kind = (uint8_t)BACT_NONE; act[1].index = 0u;
    uint32_t rec_before = 0u, rec_after = 0u;
    bool have_before = false, have_after = false;
    bool ended = false;

    for (; i < src.count; ++i) {
      const BattleEvent* e = battle_log_at(src, i);
      if (e == nullptr) break;
      if (e->round != round) { if (have_after) break; else continue; }
      if (e->kind == (uint8_t)RLE_ROUND_BEGIN) { rec_before = e->v; have_before = true; }
      else if (e->kind == (uint8_t)RLE_ACTION && e->side < 2u) {
        act[e->side].kind  = e->a;
        act[e->side].index = (uint8_t)e->b;
      } else if (e->kind == (uint8_t)RLE_ROUND_END) { rec_after = e->v; have_after = true; }
      else if (e->kind == (uint8_t)RLE_BATTLE_END) { ended = true; }
    }

    if (act[0].kind == (uint8_t)BACT_NONE || act[1].kind == (uint8_t)BACT_NONE) {
      // The source stops here. That is only legal if it recorded the end.
      if (!ended && !have_after) { rep.final_hash = battle_state_hash(out);
                                   rep.outcome = out.outcome; return BR_LOG_INCOMPLETE; }
      break;
    }

    if (battle_submit_action(out, 0u, act[0]) != BR_OK ||
        battle_submit_action(out, 1u, act[1]) != BR_OK) {
      rep.final_hash = battle_state_hash(out);
      rep.outcome    = out.outcome;
      rep.rounds     = (uint16_t)(round - 1u);
      return BR_LOG_INCOMPLETE;
    }
    // The recorded hash_before was taken AFTER both actions were pending (the
    // driver logs RLE_ROUND_BEGIN once it has them), so the comparison happens
    // here and not before the submits.
    if (have_before && rep.first_bad_round == 0u &&
        battle_state_hash(out) != rec_before) {
      rep.first_bad_round = round;   // the round STARTED diverged
    }
    (void)battle_step_round(out, log);
    rep.rounds = round;
    if (have_after && rep.first_bad_round == 0u &&
        battle_state_hash(out) != rec_after) {
      rep.first_bad_round = round;   // the divergence is INSIDE this round
    }
  }

  rep.final_hash = battle_state_hash(out);
  rep.outcome    = out.outcome;
  return BR_OK;
}
