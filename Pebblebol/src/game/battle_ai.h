// =============================================================================
//  PEBBLEBOL - game/battle_ai.h
//  THE LOCAL OPPONENT (plan P4-C3).
//
//  One function answers one question: given a battle as it stands, what does
//  this side do this round? It is a GREEDY chooser - it ranks what it can do
//  right now and takes the best - and it is deliberately not more than that.
//
// -----------------------------------------------------------------------------
//  THE PROPERTY THAT MATTERS MOST: IT CANNOT PRODUCE AN INVALID ACTION
// -----------------------------------------------------------------------------
//  In P4-C5 the SAME engine takes actions from a network peer, so the validator
//  and the action producer had better agree about what is legal. This AI is the
//  only local proof that they do, and the proof is structural rather than
//  careful: THE AI DOES NOT DECIDE LEGALITY AT ALL. It enumerates the
//  PB_MOVE_COUNT + BATTLE_TEAM_MAX actions that exist, hands each one to
//  battle_validate_action(), and may only ever return one that came back BR_OK.
//  There is no second copy of "is this move on cooldown", "is that slot empty",
//  "has this side already submitted" or "is the battle over" anywhere in
//  game/battle_ai.cpp, so a rule the engine tightens tomorrow tightens here for
//  free and the two cannot drift apart.
//
//  THE ONE EXCEPTION, stated rather than hidden: when a side has NO legal action
//  at all, battle_ai_choose() returns { BACT_NONE, 0 }. BACT_NONE is itself
//  refused by name (BR_BAD_KIND), so a caller that forwards it blindly still
//  cannot inject anything - and tests/test_battle_ai.cpp pairs every BACT_NONE
//  answer with a brute-force sweep of all 65,536 action bit patterns proving
//  that not one of them was legal either. Without that pairing, an AI that
//  always answered BACT_NONE would satisfy "never invalid" vacuously.
//
// -----------------------------------------------------------------------------
//  TWO RANDOM STREAMS, AND WHY THEY CANNOT INTERFERE
// -----------------------------------------------------------------------------
//  The AI draws (it breaks ties), and every one of its draws comes from
//  BattleAi.rng - never from BattleState.rng and never from a named global
//  stream. Four mechanisms hold that up, and the first three are enforced by
//  something other than care:
//
//    1. BattleAi is a SEPARATE OBJECT. Its Rng is not inside BattleState, so it
//       is not in battle_state_hash()'s bytes, not on P4-C5's wire and not in a
//       replay: two peers may run completely different AIs, or none, and still
//       agree round by round.
//    2. battle_ai_choose() takes a CONST BattleState&. rng_next() will not bind
//       to a const member, so "just one draw from the battle stream" DOES NOT
//       COMPILE. It is the same lever game/battle.h's validator uses.
//    3. NO FUNCTION IN THIS MODULE TAKES A MUTABLE BattleState. That is stronger
//       than (2) - the AI cannot write ANY byte of the hashed state, not merely
//       the cursor - and tools/check.sh greps for it, so a helper added next
//       year cannot quietly take one.
//    4. tools/check.sh's second battle gate already covers src/game/battle*,
//       which includes this file, so battle_ai.cpp may not call rng_u32(),
//       rng_below(), rng_seed() or esp_random() either - RNG_BATTLE included.
//
//  AND THE MEASUREMENT, because the four arguments above are still only
//  arguments: tests/test_battle_ai.cpp runs a full AI-vs-AI battle, records its
//  log, and replays it through battle_replay() - which never runs an AI. If a
//  single AI draw had come out of the battle stream, the replay's cursor would
//  sit one step behind and the round hash would not match. The same test asserts
//  that the AI really did draw, so the check is not passing on an AI that never
//  used its stream.
//
//  THE SEED. battle_ai_init() mixes the side into the seed, so two AIs built
//  from ONE number do not run the same sequence. Passing the BATTLE's seed is
//  safe and expected: BattleAi.rng is a different object, and nothing can make
//  one cursor move the other.
//
// -----------------------------------------------------------------------------
//  WHAT IT DECIDES, IN ORDER
// -----------------------------------------------------------------------------
//    1. Collect every legal attack and every legal switch (the validator says
//       which). Nothing is legal -> BACT_NONE.
//    2. NO LEGAL ATTACK -> switch. This is where a forced replacement lands, and
//       it is NOT a special case in this file: when the active has fainted the
//       validator refuses every attack with BR_MUST_SWITCH, so the branch falls
//       out of step 1 with no second copy of the rule.
//    3. HP below BATTLE_AI_SWITCH_HP_PCT AND a benched Pebble whose type is
//       STRICTLY better against the foe's active -> switch to the best of them.
//    4. Otherwise attack with the highest battle_ai_move_score().
//    5. Ties at any of those are broken by ONE draw from the AI's own stream.
//
//  A BETTER TYPE IS ONE NUMBER, NOT TWO. type_mod_of(mine, theirs) is enough,
//  and the defensive reading type_mod_of(theirs, mine) adds nothing: TYPE_CHART
//  is antisymmetric and data/attacks_table.h static_asserts that it is, so the
//  two always disagree by exactly a sign and rank the bench identically.
//
// -----------------------------------------------------------------------------
//  WHAT IT IS NOT, MEASURED RATHER THAN APOLOGISED FOR
// -----------------------------------------------------------------------------
//  This is a practice opponent for P4-C4, not a solver, and four limits are real
//  and tested rather than merely admitted:
//
//    * IT NEVER USES A STATUS MOVE while a damaging one is legal. A power-0 move
//      scores 0 by construction, so buffs, protection, cleanse and the DOT are
//      chosen only when every legal move scores 0. Every learnset carries a
//      damaging move (attacks_table.h static_asserts it), so in practice that
//      means "only when the damaging moves are on cooldown".
//    * IT DOES NOT LOOK AHEAD. No opponent model, no turn-order reasoning, no
//      "this kills me next round". Greedy expected damage this round is the
//      whole ranking, exactly as the plan specifies.
//    * ITS DAMAGE FIGURE IS AN ESTIMATE, not a simulation: it is the engine's
//      own battle_damage_pre_roll() plus the mean of the damage roll, halved if
//      the defender is protected. It cannot know the accuracy or damage roll,
//      because those draws have not happened.
//    * IT VALUES A TYPE EDGE ONLY WHILE THE ATTACKER STILL HAS BUDGET FOR ONE
//      (data/balance.h TYPE_MOD_MAX_HITS). That is not sophistication, it is the
//      minimum needed to stop it paying for an advantage it has already spent.
//
//  NO WIN-RATE FIGURE ANYWHERE APPLIES TO THIS CHOOSER. game/battle.h already
//  records that the roster's 40-63 % matrix is a claim about
//  tools/content/sim_engine.py and not about battle.cpp; the gap is wider here,
//  because that simulator HAS NO AI AT ALL - battle_3v3() scripts both sides.
//  Nothing has measured how a greedy chooser does against the tuned roster, and
//  nobody may quote a number as though something had.
//
//  PURE MODULE, on the same terms as game/battle.cpp: stdint, string.h,
//  core/rng.h and game/battle.h. No Arduino.h, no gfx, no heap, no std::
//  container, no floating point, no clock, no I/O and no file-scope mutable
//  variable - so P4-C5's loopback can run two of these in one process.
//
//  WHAT IT COSTS, MEASURED RATHER THAN ESTIMATED. arduino-cli compiles this file
//  under --warnings all (so its static_asserts fire in the firmware build), and
//  riscv32-esp-elf-size puts battle_ai.cpp.o at 1,148 B of .text, 0 .data,
//  0 .bss. riscv32-esp-elf-nm then finds ZERO battle_ai_* symbols in the linked
//  ELF, because nothing calls it yet and --gc-sections drops it: flash is
//  byte-identical to P4-C2's 1,896,094 on all seven variants. 1,148 B is what
//  P4-C4 starts paying, on top of game/battle.cpp's 8,533 B, against 503,906 B
//  of headroom. BattleAi is 8 B of caller-owned RAM per side.
// =============================================================================
#ifndef PB_GAME_BATTLE_AI_H
#define PB_GAME_BATTLE_AI_H

#include <stdint.h>

#include "../core/rng.h"
#include "battle.h"

// Bumped by any commit that changes which action this chooser returns. It is
// NOT part of battle_state_hash()'s basis and must never become part of it: two
// peers running different AI versions, or one running none at all, still play
// the same battle, because only the resulting two bytes ever cross the wire.
#define BATTLE_AI_VER 1u

// The weight of the type term in a switch score. It must exceed the widest
// health term (a percentage, so 100) or a nearly-dead Pebble with a good type
// could be outranked by a healthy one with a bad one, and rule 3 above would
// stop meaning what it says.
#define BATTLE_AI_SWITCH_TYPE_WEIGHT 128u
static_assert(BATTLE_AI_SWITCH_TYPE_WEIGHT > 100u,
              "the type term must strictly dominate the health term in a switch score");

// Every action that can be considered in one round: four move slots plus the
// team slots. Sized from the constants, so a bigger team cannot overflow it.
#define BATTLE_AI_CAND_MAX ((uint8_t)PB_MOVE_COUNT + (uint8_t)BATTLE_TEAM_MAX)

// The chooser's whole state. Padding-free like everything else in this corner
// of the tree, though for tidiness rather than necessity: it is NOT hashed and
// NOT on the wire, which is the point (see mechanism 1 above).
struct BattleAi {
  Rng     rng;            // 0  ITS OWN stream, and the only one this module has
  uint8_t side;           // 4  0 or 1; anything else makes every choice BACT_NONE
  uint8_t reserved[3];    // 5  must be 0
};
static_assert(sizeof(BattleAi) == sizeof(Rng) + 4, "BattleAi has a padding hole");
static_assert(sizeof(BattleAi) == 8, "BattleAi layout drifted");

// Zeroes the object, stores `side` AS GIVEN (an out-of-range side is not
// clamped - it makes every subsequent choice BACT_NONE, which is the same
// never-clamp rule game/battle.h's validator follows) and seeds the stream from
// `seed` mixed with the side.
void battle_ai_init(BattleAi& ai, uint8_t side, uint32_t seed);

// The one decision. Returns an action battle_validate_action() accepts, or
// { BACT_NONE, 0 } if and only if the side has no legal action at all.
// Draws at most once, from ai.rng.
BattleAction battle_ai_choose(BattleAi& ai, const BattleState& st);

// -----------------------------------------------------------------------------
//  THE RANKING, EXPOSED
//
//  Not for the firmware - battle_ai_choose() is the whole interface P4-C4 needs
//  - but so a test can assert the ORDER a choice was made in, instead of
//  inferring the ranking from which action came back. Inferring it is how a test
//  that cannot fail gets written: one choice is consistent with many rankings.
//  All three are const, drawless and side-effect free.
// -----------------------------------------------------------------------------

// Expected damage in HALF-POINTS times effective accuracy (0..100). Zero for a
// status move, an unknown move, an out-of-range slot or an absent combatant.
// Half-points because the damage roll is uniform over 0..DMG_RNG_SPAN-1 and its
// mean is (SPAN-1)/2, which is not an integer for an even span: doubling makes
// that mean EXACT in integer arithmetic instead of truncating it away.
uint32_t battle_ai_move_score(const BattleState& st, uint8_t side, uint8_t move_slot);

// (type_mod + 1) * BATTLE_AI_SWITCH_TYPE_WEIGHT + hp percent. Type first,
// health second, and the weight above is what keeps that order.
uint32_t battle_ai_switch_score(const BattleState& st, uint8_t side, uint8_t team_slot);

// Rule 3 on its own: HP strictly below BATTLE_AI_SWITCH_HP_PCT AND a LEGAL
// switch target whose type is strictly better against the foe's active. False
// for a forced replacement, which rule 2 handles and which never asks.
bool battle_ai_wants_to_switch(const BattleState& st, uint8_t side);

#endif  // PB_GAME_BATTLE_AI_H
