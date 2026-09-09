// =============================================================================
//  ERRATA - minigames/minigame.h
//  THE MINIGAME CONTRACT (spec section 29, plan section 1.4 / P3-C4).
//
//  Spec section 29 ends with the line that shapes this whole directory:
//
//      "All games must be deterministic/testable when supplied a fixed RNG
//       seed."
//
//  That is an acceptance criterion, not a nicety, and TWO rules follow from it.
//
//  1. THE STEP COUNT IS THE CLOCK. A game never reads millis(). MgCtx.t_ms is
//     advanced by exactly MG_STEP_MS per step() by whoever is driving, so the
//     same seed and the same presses produce the same score on a host with no
//     clock at all. The games this replaced all called since(s_g.t0) straight
//     into their scoring, which made a score a function of how busy loop() was.
//
//  2. LOGIC AND DRAWING ARE DIFFERENT TRANSLATION UNITS. games/<name>_logic.cpp
//     is PURE - stdint and core/rng.h, no Arduino, no u8g2, no gfx, no strings -
//     so tests/ compiles it directly. games/<name>_draw.cpp is the only half
//     that draws, through ui/gfx.h and nothing else. A game whose logic cannot
//     be run headless is not finished.
//
//  THE SIX HOOKS are init / press / step / done / finish (all pure, in MgLogic)
//  plus draw (the device half). They are split across two structs only so the
//  pure five can be linked without the sixth; MinigameDef below is the six
//  together, which is what the manager and the screen actually use.
//
//  NO HEAP, ANYWHERE. MgCtx is a fixed-size POD and a game keeps its private
//  state inside MgCtx::state through mg_state<T>(), which static_asserts that
//  T fits. A game needing more than MG_STATE_BYTES should shrink, not allocate.
// =============================================================================
#ifndef ER_MINIGAME_H
#define ER_MINIGAME_H

#include <stddef.h>     // offsetof, for the MgCtx::state alignment guard
#include <stdint.h>

#include "../core/rng.h"
#include "../core/strings_es.h"   // StrId: name_idx, hint_idx and MgCtx::note

// The fixed simulation step. 25 ms is what the games this replaced already
// used, and it is fast enough that a reaction time reads as smooth.
#define MG_STEP_MS        25u

// Spec section 29: every game runs 5 to 15 s. The manager enforces the ceiling
// so a game that never sets `finished` cannot strand the player, and
// tests/test_minigames.cpp asserts every game reaches its own end before it.
#define MG_MAX_MS      15000u
#define MG_MAX_STEPS   (MG_MAX_MS / MG_STEP_MS)

// Every score in the product is per-mille. sim_apply_play_result() and
// xp_minigame_amount() both already speak it, so no game invents a scale.
#define MG_SCORE_MAX    1000u

// The game ids. They lived in nt_types.h as DevGameId until P3-C4a; they
// belong with the contract instead. CANONICAL FOR minigames_won.
//
// THE ORDER IS LOAD-BEARING IN FOUR PLACES and pinned by a static_assert in
// each: POOL[] in manager.cpp, DEFS[] in registry.cpp, the PLAY list rows in
// ui/screen_care.cpp, and the name/hint pair in core/strings_es.h. PING and
// SEQUENCE keep 0 and 1 because minigames_won is persisted against them; the
// P3-C4b four are appended rather than interleaved for the same reason.
enum MgId : uint8_t {
  MG_ID_PING = 0,      // spec 29.1
  MG_ID_SEQUENCE,      // spec 29, "additional candidates"
  MG_ID_PACKET_FLOOD,  // spec 29.2
  MG_ID_FIREWALL,      // spec 29.3
  MG_ID_BUFFER,        // spec 29.4
  MG_ID_DELETE,        // spec 29.5
  MG_ID_COUNT
};

#define MG_STATE_BYTES     32
#define MG_SIDE_L           0u
#define MG_SIDE_R           1u

struct MgCtx {
  Rng      rng;        // per-run, seeded once by mg_begin(): the reproducibility
  uint32_t t_ms;       // MG_STEP_MS * steps taken. NEVER millis().
  uint16_t score;      // per-mille, 0..MG_SCORE_MAX
  uint16_t note;       // a StrId the PRESENTATION layer may toast; 0 = none.
                       // Pure logic cannot toast, so it leaves a note instead.
  uint8_t  round;
  uint8_t  finished;   // set by the game; read by the manager and by done()
  // ALIGNED, not merely sized. mg_state<T>() reinterpret_casts this array to a
  // game's struct, and the alignof() assert below cannot see the OFFSET the
  // array sits at: without the alignas the members before it add up to 14, so
  // a struct holding a uint32_t (PING's arm_ms, packet_flood's marks) would be
  // read at 2 mod 4 - undefined behaviour, and on Xtensa a real alignment
  // exception rather than a slow load. The pad costs nothing: sizeof(MgCtx)
  // stays 48, measured before and after.
  alignas(uint32_t) uint8_t state[MG_STATE_BYTES];
};
static_assert(offsetof(MgCtx, state) % alignof(uint32_t) == 0,
              "MgCtx::state must be 32-bit aligned: mg_state<T>() casts it");

// A game's private state, laid over MgCtx::state. The static_asserts are the
// whole safety of the arrangement.
template <typename T>
inline T& mg_state(MgCtx& c)
{
  static_assert(sizeof(T) <= MG_STATE_BYTES, "minigame state exceeds MG_STATE_BYTES");
  static_assert(alignof(T) <= alignof(uint32_t), "minigame state over-aligned");
  return *reinterpret_cast<T*>(c.state);
}
template <typename T>
inline const T& mg_state(const MgCtx& c)
{
  static_assert(sizeof(T) <= MG_STATE_BYTES, "minigame state exceeds MG_STATE_BYTES");
  return *reinterpret_cast<const T*>(c.state);
}

typedef void     (*MgInitFn)  (MgCtx&);
typedef void     (*MgPressFn) (MgCtx&, uint8_t side);   // MG_SIDE_L / MG_SIDE_R
typedef void     (*MgStepFn)  (MgCtx&);                 // exactly one MG_STEP_MS
typedef bool     (*MgDoneFn)  (const MgCtx&);
typedef uint16_t (*MgFinishFn)(MgCtx&);                 // the final per-mille score
typedef void     (*MgDrawFn)  (const MgCtx&);           // DEVICE half

// The pure five. games/<name>_logic.cpp defines exactly one of these.
struct MgLogic {
  uint8_t    id;
  uint16_t   name_idx;      // StrId, resolved by the presentation layer
  uint16_t   hint_idx;      // StrId
  MgInitFn   init;
  MgPressFn  press;
  MgStepFn   step;
  MgDoneFn   done;
  MgFinishFn finish;
};

// The six. Assembled in a DEVICE translation unit, which is the only place
// that can see a draw function.
struct MinigameDef {
  const MgLogic* logic;
  MgDrawFn       draw;
};

// -----------------------------------------------------------------------------
//  DRIVING A GAME
//  These three are pure and are what the host tests use. The manager wraps them
//  with the sequence, the cooldown and the reporting.
// -----------------------------------------------------------------------------

// Zero the context, seed the run and call init(). One seed in, one game out.
void mg_begin(MgCtx& c, const MgLogic& g, uint32_t seed);

// One fixed step: advances t_ms by MG_STEP_MS and calls step(). Returns false
// once the game is over, so a caller can drive it with `while (mg_tick(...))`.
bool mg_tick(MgCtx& c, const MgLogic& g);

// A press, ignored once the game is over so a late button cannot score.
void mg_press(MgCtx& c, const MgLogic& g, uint8_t side);

// The final score, clamped into 0..MG_SCORE_MAX. Calling it twice is safe and
// returns the same number; it does NOT report anything to the world - that is
// the manager's job, and doing it here is how a result gets counted twice.
uint16_t mg_score(MgCtx& c, const MgLogic& g);

#endif  // ER_MINIGAME_H
