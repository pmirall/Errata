// =============================================================================
//  PEBBLEBOL - game/battle_ai.cpp
//  The greedy local opponent. See game/battle_ai.h for the never-invalid
//  contract, the two-stream argument and the five stated limits.
//
//  NO FILE-SCOPE MUTABLE VARIABLE, for the same reason game/battle.cpp has none:
//  P4-C5's loopback runs two of these in one process and a single static would
//  couple them.
// =============================================================================
#include "battle_ai.h"

#include <string.h>

// =============================================================================
//  SETUP
// =============================================================================
// THE AVALANCHE, and it is not decoration. rng_next_below(r, 2) - the draw a
// two-way tie makes - is the TOP BIT of one xorshift32 step, and one step moves
// a difference in the seed's low bits no higher than bit 21: bit i goes to i+13,
// the >> 17 cannot lift it, and the << 5 lands it at i+18. So two AI seeds that
// differ only in their low nibble - exactly what a caller derived from a counter
// hands over - break EVERY two-way tie the same way, and the AI seed stops
// reaching the choice at all.
//
// This was not reasoned out in advance. tests/test_battle_ai.cpp asked sixteen
// consecutive seeds for sixteen fights and got ONE action sequence back; the
// case is still there and still requires two.
//
// murmur3's fmix32, which is what core/rng.cpp's rng_seed_all() uses on the boot
// seed for the same reason. It is copied rather than shared because rng.cpp
// keeps it static and this file must not widen that header's surface.
static uint32_t ai_seed_mix(uint32_t h)
{
  h ^= h >> 16;
  h *= 0x85EBCA6Bu;
  h ^= h >> 13;
  h *= 0xC2B2AE35u;
  h ^= h >> 16;
  return h;
}

void battle_ai_init(BattleAi& ai, uint8_t side, uint32_t seed)
{
  memset(&ai, 0, sizeof ai);
  // NOT clamped. A side of 2 is a caller's bug, and turning it into 0 would make
  // the AI choose for the wrong team - which is a far worse answer than choosing
  // nothing. battle_ai_choose() refuses it before it indexes anything.
  ai.side = side;
  // The side is mixed in first, so two AIs built from ONE number do not run the
  // same sequence; the golden-ratio odd word is the one core/rng.cpp uses for
  // that job. rng_init() maps a 0 result to RNG_DEFAULT_SEED, so no seed can
  // produce a dead stream.
  rng_init(ai.rng, ai_seed_mix(seed + 0x9E3779B9u * (uint32_t)((uint32_t)side + 1u)));
}

// =============================================================================
//  THE RANKING
// =============================================================================

// Expected damage of one hit in HALF-POINTS (game/battle_ai.h explains the
// halves). The damage FORMULA is not here: battle_damage_pre_roll() is the
// engine's own, so this file cannot drift from data/balance.h.
static uint32_t expected_damage_half(const BattleCombatant& u, const BattleCombatant& f,
                                     const AttackDef& a)
{
  if (a.power == 0u) return 0u;      // a status move: see the LIMITS note in the header

  int8_t m = type_mod_of(a.type, f.type);
  // THE TYPE EDGE IS A BUDGET, not a permanent property of the matchup
  // (data/balance.h TYPE_MOD_MAX_HITS, spent per attacking combatant). An
  // estimate that ignored the budget would keep paying for an advantage this
  // attacker has already spent, and would keep fearing a disadvantage it can no
  // longer suffer. Reading the counter is not spending it - only the engine
  // spends, inside compute_damage().
  if (u.type_edge_left == 0u) m = 0;

  uint32_t h = 2u * (uint32_t)battle_damage_pre_roll(u, f, a, m)
             + (uint32_t)((uint32_t)DMG_RNG_SPAN - 1u);

  if (f.protect_left > 0u) {
    // The engine halves AFTER the roll and floors at DMG_MIN, so its expected
    // damage is the mean of DMG_RNG_SPAN separately-halved values; this halves
    // the mean once. The GAP is small and conservative - at most 2/6 of a point,
    // measured over the whole roster - but it does NOT always preserve the
    // ranking, and the first version of this comment claimed it did. When two
    // candidates straddle the integer halving the order can flip: 757 of the
    // 233,280 plain-roster protected move pairs, 0.32 %, worst case 5.9 % of the
    // better move's expected damage. game/battle_ai.h's LIMITS list carries the
    // measurement and the reason it is accepted rather than removed.
    h /= (uint32_t)PROTECT_DIVISOR;
    // NOT A FLOOR: h is 2 * pre_roll + (DMG_RNG_SPAN - 1) and pre_roll is itself
    // floored at DMG_MIN, so h is at least 2 * DMG_MIN + DMG_RNG_SPAN - 1 and
    // h / PROTECT_DIVISOR cannot fall below 2 * DMG_MIN under any value
    // data/balance.h can hold. The line below was DEAD, not merely untested -
    // deleting it changed nothing anywhere - and a floor that reads as though it
    // does something is worse than no floor. It is gone; this comment is what
    // replaces it, and a future PROTECT_DIVISOR larger than DMG_RNG_SPAN + 1
    // would be the change that needs it back.
  }
  return h;
}

uint32_t battle_ai_move_score(const BattleState& st, uint8_t side, uint8_t move_slot)
{
  // Range first, then index - game/battle.cpp's rule, kept here for the same
  // reason: these are the exact bytes a peer's action carries in P4-C5.
  //
  // THE SLOT HALF IS TESTED AND THE SIDE HALF IS NOT, and the difference is
  // stated rather than left for a reader to assume. Deleting the slot bound
  // makes move_slot 4 read the byte after moves[3] and score a move that is not
  // there; tests/test_battle_ai.cpp arms that byte and fails. Deleting the side
  // bound changes NOTHING, because battle_active() below range-checks the side
  // itself and nothing is indexed before it - the mutation sweep confirmed it
  // stays green. It is kept because "range first" is the rule this module's
  // never-clamp contract rests on and a reader must see it locally, but nothing
  // here claims it is tested.
  if (side > 1u || move_slot >= (uint8_t)PB_MOVE_COUNT) return 0u;

  const BattleCombatant* u = battle_active(st, side);
  const BattleCombatant* f = battle_active(st, (uint8_t)(side ^ 1u));
  // An absent combatant is not scoreable. It is unreachable from a battle this
  // engine ran, and it is defence in depth for the wire-supplied state P4-C5
  // will hand in - the same reason battle.h keeps BR_EMPTY_ACTIVE.
  if (u == nullptr || f == nullptr) return 0u;
  if ((u->flags & BCF_PRESENT) == 0u || (f->flags & BCF_PRESENT) == 0u) return 0u;

  const AttackDef* a = attack_get(u->moves[move_slot]);
  if (a == nullptr) return 0u;

  // Expected damage weighted by the chance of landing it, and the u32 has room
  // for it several thousand times over. TWO DIFFERENT NUMBERS, and the first
  // version of this comment printed the second while calling it the first:
  //   * the widest the SHIPPED ROSTER actually reaches is 34,170 - species 12 at
  //     level 6 against species 16 with attack 3, pre_roll 200 - measured by
  //     sweeping 36 attackers x 36 defenders x levels 1..30 x every ATK and DEF
  //     stage x corruption on either side x the type edge held and spent;
  //   * the arithmetic CEILING is 894 half-points * 100 = 89,400, from
  //     battle_damage_pre_roll()'s own bound of 446 (atk_eff 25, def_eff 1,
  //     power 100, times 5/4).
  // The ceiling is what makes the type safe; the measured figure is what the
  // game does. Neither is close to 4,294,967,295.
  return expected_damage_half(*u, *f, *a) * (uint32_t)battle_accuracy_eff(*u, *f, *a);
}

uint32_t battle_ai_switch_score(const BattleState& st, uint8_t side, uint8_t team_slot)
{
  const BattleCombatant* c = battle_combatant(st, side, team_slot);
  if (c == nullptr || (c->flags & BCF_PRESENT) == 0u) return 0u;

  // A bench member is worth its TYPE against the foe first and its HEALTH
  // second. type_mod_of() answers -1..+1, so the term is 0..2.
  uint32_t mod_term = 1u;      // 1 == neutral, which is also "no information"
  const BattleCombatant* f = battle_active(st, (uint8_t)(side ^ 1u));
  if (f != nullptr && (f->flags & BCF_PRESENT) != 0u) {
    mod_term = (uint32_t)((int32_t)type_mod_of(c->type, f->type) + 1);
  }

  const uint32_t hp_pct = (c->hp_max > 0u)
                        ? ((uint32_t)c->hp_cur * 100u / (uint32_t)c->hp_max)
                        : 0u;
  return mod_term * BATTLE_AI_SWITCH_TYPE_WEIGHT + hp_pct;
}

bool battle_ai_wants_to_switch(const BattleState& st, uint8_t side)
{
  // Provably redundant, on the same terms as the side half of move_score's
  // check above: battle_active() answers nullptr for any side above 1 and the
  // nullptr test below returns false. Nothing claims this line is tested.
  if (side > 1u) return false;

  const BattleCombatant* u = battle_active(st, side);
  const BattleCombatant* f = battle_active(st, (uint8_t)(side ^ 1u));
  if (u == nullptr || f == nullptr) return false;
  if ((u->flags & BCF_PRESENT) == 0u || (f->flags & BCF_PRESENT) == 0u) return false;
  if (u->hp_max == 0u) return false;

  // "HP < 25 %", MULTIPLIED OUT: hp_cur / hp_max < PCT / 100 with no division,
  // so a rounding step cannot move the boundary. The comparison is strict, so a
  // Pebble at exactly the threshold stands and fights, and
  // tests/test_battle_ai.cpp pins both sides of it.
  if ((uint32_t)u->hp_cur * 100u >=
      (uint32_t)u->hp_max * (uint32_t)BATTLE_AI_SWITCH_HP_PCT) return false;

  // ...AND a benched Pebble with a STRICTLY better type. One number, not two:
  // TYPE_CHART is antisymmetric (attacks_table.h static_asserts it), so the
  // defensive reading ranks the bench identically.
  const int8_t here = type_mod_of(u->type, f->type);
  for (uint8_t i = 0; i < (uint8_t)BATTLE_TEAM_MAX; ++i) {
    BattleAction act;
    act.kind  = (uint8_t)BACT_SWITCH;
    act.index = i;
    // LEGALITY IS THE VALIDATOR'S ANSWER, here as everywhere in this file: this
    // one call skips the active slot, the empty slots and the fainted ones
    // without a second copy of those three rules.
    if (battle_validate_action(st, side, act) != BR_OK) continue;
    if (type_mod_of(st.side[side].team[i].type, f->type) > here) return true;
  }
  return false;
}

// =============================================================================
//  THE DECISION
// =============================================================================
BattleAction battle_ai_choose(BattleAi& ai, const BattleState& st)
{
  BattleAction none;
  none.kind  = (uint8_t)BACT_NONE;
  none.index = 0u;

  // The range check comes FIRST and nothing is indexed until it passes. It is
  // provably redundant too - battle_validate_action() refuses every candidate
  // with BR_BAD_SIDE and the empty pool below answers BACT_NONE anyway, which
  // the mutation sweep confirmed - so it earns its place on two other grounds
  // and not on being tested: it makes the never-clamp rule visible at the top of
  // the one function callers use, and it skips seven validator calls that could
  // only ever fail. What IS tested, and what fails a named case, is CLAMPING a
  // bad side to 0: choosing for the wrong team is far worse than choosing
  // nothing.
  if (ai.side > 1u) return none;

  BattleAction cand[BATTLE_AI_CAND_MAX];
  uint32_t     score[BATTLE_AI_CAND_MAX];
  uint8_t      n = 0u;

  // (1) THE LEGAL ATTACKS. Every candidate goes through the engine's validator
  //     and an illegal one is not scored, not ranked and not reachable - so the
  //     highest-scoring move in the world is skipped when it is on cooldown.
  for (uint8_t i = 0; i < (uint8_t)PB_MOVE_COUNT; ++i) {
    BattleAction a;
    a.kind  = (uint8_t)BACT_ATTACK;
    a.index = i;
    if (battle_validate_action(st, ai.side, a) != BR_OK) continue;
    cand[n]  = a;
    score[n] = battle_ai_move_score(st, ai.side, i);
    ++n;
  }
  const uint8_t n_atk = n;

  // (2) THE LEGAL SWITCHES, after them, so [0, n_atk) and [n_atk, n) are the two
  //     pools and neither needs a flag to tell them apart.
  for (uint8_t i = 0; i < (uint8_t)BATTLE_TEAM_MAX; ++i) {
    BattleAction a;
    a.kind  = (uint8_t)BACT_SWITCH;
    a.index = i;
    if (battle_validate_action(st, ai.side, a) != BR_OK) continue;
    cand[n]  = a;
    score[n] = battle_ai_switch_score(st, ai.side, i);
    ++n;
  }

  // NOTHING IS LEGAL. The battle is over, this side has already submitted, or -
  // only from wire-supplied state - its active slot is empty or its whole team
  // is down. BACT_NONE is refused by name, so forwarding it injects nothing.
  if (n == 0u) return none;

  uint8_t lo, hi;
  if (n_atk == 0u) {
    // A FORCED REPLACEMENT LANDS HERE AND IS NOT A SPECIAL CASE: when the active
    // has fainted the validator refuses every attack with BR_MUST_SWITCH, so the
    // attack pool is empty on its own and the switch pool is what is left.
    lo = 0u;  hi = n;
  } else if (battle_ai_wants_to_switch(st, ai.side)) {
    // ONE CONDITION, NOT TWO. This used to read `n > n_atk && wants...`, and the
    // mutation sweep proved that the first half COULD NOT FAIL: rule 3 answers
    // true only when some slot came back BR_OK from the very same validator, on
    // the very same state, in the very same round - and those are exactly the
    // slots the loop above put in the switch pool. A guard that cannot fail is
    // this project's recurring defect, so it is gone rather than kept for
    // comfort. If rule 3 ever stops consulting the validator, THIS is the line
    // that has to come back.
    lo = n_atk; hi = n;
  } else {
    lo = 0u;  hi = n_atk;
  }

  uint32_t best  = 0u;
  uint8_t  ties  = 0u;
  for (uint8_t i = lo; i < hi; ++i) {
    if (ties == 0u || score[i] > best) { best = score[i]; ties = 1u; }
    else if (score[i] == best)         { ++ties; }
  }

  // THE ONLY DRAW IN THIS MODULE, and it comes from the AI's own stream. It
  // costs nothing when there is nothing to break: rng_next_below(r, 1) returns 0
  // without stepping the generator (core/rng.cpp), so an unambiguous choice
  // leaves the cursor exactly where it was.
  uint32_t pick = rng_next_below(ai.rng, (uint32_t)ties);
  for (uint8_t i = lo; i < hi; ++i) {
    if (score[i] != best) continue;
    if (pick == 0u) return cand[i];
    --pick;
  }

  // Unreachable: `best` and `ties` were both measured over [lo, hi), so the loop
  // above always finds the pick-th of them. Returning a candidate rather than
  // BACT_NONE keeps the never-invalid contract true even if that ever changes.
  return cand[lo];
}
