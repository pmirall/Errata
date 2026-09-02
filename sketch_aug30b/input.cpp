// =============================================================================
//  NOTTAMAGOCHI - input.cpp
//  Two-button debounce + gesture recogniser (GAME_DESIGN 8.1).
//
//  DESIGN NOTES (the reasons this file looks the way it does)
//
//  1. No gesture is ever emitted on a press edge except DBL_x. A single press
//     is only classified once it has been released AND DOUBLE_TAP_WINDOW_MS has
//     passed with no second press. That is what makes the chord free of the
//     classic "BOTH is preceded by a spurious TAP" bug: when the second button
//     goes down the first one has not produced anything yet, so there is
//     nothing to retract.
//
//  2. Holds fire exactly ONCE when the threshold is crossed. HOLD_L then keeps
//     repeating every REPEAT_RATE_MS because GAME_DESIGN 8.3 uses it as the
//     fast-scroll of every list. HOLD_R is BACK on every screen and therefore
//     never repeats.
//
//  3. Once an episode has produced its gesture, the remaining edges of that
//     episode are swallowed (state ST_SWALLOW) until BOTH buttons are up. This
//     is what keeps "hold L, then press R" from emitting a phantom TAP_R, and
//     what keeps the second release of a chord from emitting anything.
//
//  4. Every time comparison is written as (uint32_t)(now - then) >= X so the
//     recogniser survives the 49.7-day millis() wrap.
//
//  5. Zero allocation, zero floating point, no Arduino header outside the shim
//     block below, so the whole file compiles on a host compiler for the
//     scripted-waveform test harness.
// =============================================================================
#include "input.h"

// -----------------------------------------------------------------------------
// 0. PLATFORM SHIM
//    On the target this is millis()/digitalRead(). On a host compiler the two
//    hooks below must be provided by the test harness; they let a scripted
//    waveform drive the recogniser with no hardware and no real time.
// -----------------------------------------------------------------------------
#if defined(ARDUINO)
  #include <Arduino.h>
  static inline uint32_t in_now(void)          { return (uint32_t)millis(); }
  static inline int      in_level(uint8_t i)   { return digitalRead(i == INPUT_BTN_L ? PIN_BTN_L : PIN_BTN_R); }
  static inline void     in_pins_begin(void)   { pinMode(PIN_BTN_L, INPUT_PULLUP); pinMode(PIN_BTN_R, INPUT_PULLUP); }
#else
  extern "C" uint32_t nt_input_test_millis(void);   // fake clock, milliseconds
  extern "C" int      nt_input_test_level(int idx); // fake pin level, 0 or 1
  static inline uint32_t in_now(void)          { return nt_input_test_millis(); }
  static inline int      in_level(uint8_t i)   { return nt_input_test_level((int)i); }
  static inline void     in_pins_begin(void)   { }
#endif

// -----------------------------------------------------------------------------
// 1. STATE
// -----------------------------------------------------------------------------
enum InState : uint8_t {
  ST_IDLE = 0,   // nothing down (a tap may still be pending in the window)
  ST_DOWN,       // exactly one button down, not yet classified
  ST_HOLD,       // that button crossed HOLD_MS; HOLD_x already emitted
  ST_CHORD,      // both down, recognised as a chord
  ST_SWALLOW     // episode already resolved: ignore edges until both are up
};

#define IN_QUEUE_LEN 8   // power of two

static uint8_t  s_state;
static uint8_t  s_active;              // index of the button owning ST_DOWN / ST_HOLD

static bool     s_down[INPUT_BTN_N];        // debounced level
static bool     s_raw[INPUT_BTN_N];         // last sampled level (undebounced)
static uint32_t s_raw_change_ms[INPUT_BTN_N];
static uint32_t s_press_ms[INPUT_BTN_N];    // logical press instant (raw edge)

static bool     s_pending_tap;         // a tap is waiting out the double-tap window
static uint8_t  s_pending_idx;
static uint32_t s_pending_since;       // release instant of that tap

static bool     s_long_done;           // LONG_BOTH already emitted for this chord
static uint32_t s_chord_ms;            // instant at which the SECOND button went down
static uint32_t s_repeat_ms;           // last HOLD_L repeat

static uint8_t  s_q[IN_QUEUE_LEN];
static uint8_t  s_q_head;              // next slot to read
static uint8_t  s_q_count;

// -----------------------------------------------------------------------------
// 2. QUEUE
// -----------------------------------------------------------------------------
static void in_emit(Gesture g)
{
  if (s_q_count >= IN_QUEUE_LEN) {
    return;                            // full: drop the newest, keep the order
  }
  s_q[(uint8_t)((s_q_head + s_q_count) & (IN_QUEUE_LEN - 1))] = (uint8_t)g;
  s_q_count++;
}

static Gesture in_pop(void)
{
  if (s_q_count == 0) {
    return GST_NONE;
  }
  Gesture g = (Gesture)s_q[s_q_head];
  s_q_head = (uint8_t)((s_q_head + 1) & (IN_QUEUE_LEN - 1));
  s_q_count--;
  return g;
}

static void in_queue_clear(void)
{
  s_q_head  = 0;
  s_q_count = 0;
}

// -----------------------------------------------------------------------------
// 3. PENDING TAP
// -----------------------------------------------------------------------------
static void in_flush_pending(void)
{
  if (!s_pending_tap) {
    return;
  }
  s_pending_tap = false;
  in_emit(s_pending_idx == INPUT_BTN_L ? GST_TAP_L : GST_TAP_R);
}

// -----------------------------------------------------------------------------
// 4. EDGE HANDLERS
//    t is the instant of the physical edge, not the instant the debouncer
//    accepted it, so every duration below matches the real waveform.
// -----------------------------------------------------------------------------
static void in_on_press(uint8_t i, uint32_t t)
{
  const uint8_t other = (uint8_t)(INPUT_BTN_N - 1 - i);

  s_press_ms[i] = t;

  if (s_down[other]) {
    // --- second button of a two-button episode -------------------------------
    // A chord is only formed when the first button is still unclassified AND
    // the two press edges are within BOTH_SYNC_MS. Anything else (the first
    // button already became a HOLD, or the presses are too far apart to be one
    // deliberate chord) resolves to "swallow": no phantom TAP, no phantom BOTH.
    if (s_state == ST_DOWN) {
      const uint32_t a = s_press_ms[other];
      const uint32_t skew = (t >= a) ? (uint32_t)(t - a) : (uint32_t)(a - t);
      if (skew <= (uint32_t)BOTH_SYNC_MS) {
        s_state     = ST_CHORD;
        s_long_done = false;
        s_chord_ms  = t;               // both are down only from here on
        return;
      }
    }
    s_state = ST_SWALLOW;
    return;
  }

  // --- first (and only) button down -----------------------------------------
  if (s_pending_tap &&
      s_pending_idx == i &&
      (uint32_t)(t - s_pending_since) <= (uint32_t)DOUBLE_TAP_WINDOW_MS) {
    // Second press of a double tap. Emit immediately, then swallow this press:
    // it must not also become a TAP or a HOLD.
    s_pending_tap = false;
    in_emit(i == INPUT_BTN_L ? GST_DBL_L : GST_DBL_R);
    s_state = ST_SWALLOW;
    return;
  }

  // A press of the other button closes any tap still waiting out its window,
  // so the single pending slot is never overwritten and the order is kept.
  in_flush_pending();

  s_active = i;
  s_state  = ST_DOWN;
}

static void in_on_release(uint8_t i, uint32_t t)
{
  const uint8_t other = (uint8_t)(INPUT_BTN_N - 1 - i);

  switch (s_state) {

    case ST_DOWN:
      // Short press: it can still turn into a double tap, so only arm the
      // window here. TAP_x is emitted by in_tick() when the window expires.
      if (s_active == i) {
        s_pending_tap   = true;
        s_pending_idx   = i;
        s_pending_since = t;
      }
      s_state = s_down[other] ? ST_SWALLOW : ST_IDLE;
      break;

    case ST_HOLD:
      // HOLD_x was already delivered on the threshold; the release is silent.
      s_state = s_down[other] ? ST_SWALLOW : ST_IDLE;
      break;

    case ST_CHORD:
      // Released before LONG_BOTH_MS -> BOTH. Released after -> LONG_BOTH was
      // already delivered on the threshold and the release is silent.
      if (!s_long_done) {
        in_emit(GST_BOTH);
      }
      s_state = s_down[other] ? ST_SWALLOW : ST_IDLE;
      break;

    case ST_SWALLOW:
    case ST_IDLE:
    default:
      if (!s_down[INPUT_BTN_L] && !s_down[INPUT_BTN_R]) {
        s_state = ST_IDLE;
      }
      break;
  }
}

// -----------------------------------------------------------------------------
// 5. SAMPLER (debounce)
// -----------------------------------------------------------------------------
static void in_sample(uint32_t now)
{
  for (uint8_t i = 0; i < INPUT_BTN_N; i++) {
    const bool pressed = (in_level(i) == BTN_ACTIVE_LEVEL);

    if (pressed != s_raw[i]) {
      s_raw[i]           = pressed;
      s_raw_change_ms[i] = now;        // restart the stability window
    }
    if (pressed != s_down[i] &&
        (uint32_t)(now - s_raw_change_ms[i]) >= (uint32_t)DEBOUNCE_MS) {
      s_down[i] = pressed;
      if (pressed) {
        in_on_press(i, s_raw_change_ms[i]);
      } else {
        in_on_release(i, s_raw_change_ms[i]);
      }
    }
  }
}

// -----------------------------------------------------------------------------
// 6. TIME-DRIVEN TRANSITIONS
// -----------------------------------------------------------------------------
static void in_tick(uint32_t now)
{
  switch (s_state) {

    case ST_DOWN:
      // The extra s_raw[] test kills the only debounce artefact that matters:
      // a press released a few ms before HOLD_MS would otherwise be promoted to
      // a hold while its release is still inside the debounce window.
      if (s_raw[s_active] &&
          (uint32_t)(now - s_press_ms[s_active]) >= (uint32_t)HOLD_MS) {
        in_emit(s_active == INPUT_BTN_L ? GST_HOLD_L : GST_HOLD_R);
        s_state    = ST_HOLD;
        s_repeat_ms = s_press_ms[s_active] + (uint32_t)REPEAT_START_MS;
      }
      break;

    case ST_HOLD:
      // Fast scroll. HOLD_R is BACK and must never repeat.
      if (s_active == INPUT_BTN_L &&
          (uint32_t)(now - s_repeat_ms) >= (uint32_t)REPEAT_RATE_MS) {
        in_emit(GST_HOLD_L);
        s_repeat_ms = now;
      }
      break;

    case ST_CHORD:
      if (!s_long_done &&
          s_raw[INPUT_BTN_L] && s_raw[INPUT_BTN_R] &&
          (uint32_t)(now - s_chord_ms) >= (uint32_t)LONG_BOTH_MS) {
        in_emit(GST_LONG_BOTH);
        s_long_done = true;
      }
      break;

    default:
      break;
  }

  if (s_pending_tap &&
      (uint32_t)(now - s_pending_since) > (uint32_t)DOUBLE_TAP_WINDOW_MS) {
    in_flush_pending();
  }
}

// -----------------------------------------------------------------------------
// 7. PUBLIC API
// -----------------------------------------------------------------------------
void input_begin(void)
{
  const uint32_t now = in_now();

  in_pins_begin();
  in_queue_clear();

  s_pending_tap = false;
  s_pending_idx = INPUT_BTN_L;
  s_pending_since = now;
  s_long_done   = false;
  s_chord_ms    = now;
  s_repeat_ms   = now;
  s_active      = INPUT_BTN_L;

  bool any_down = false;
  for (uint8_t i = 0; i < INPUT_BTN_N; i++) {
    const bool pressed = (in_level(i) == BTN_ACTIVE_LEVEL);
    s_raw[i]           = pressed;
    s_down[i]          = pressed;      // seeded, so no synthetic press edge
    s_raw_change_ms[i] = now;
    s_press_ms[i]      = now;
    if (pressed) {
      any_down = true;
    }
  }
  // A button held through boot is ignored until it is released.
  s_state = any_down ? ST_SWALLOW : ST_IDLE;
}

Gesture input_poll(void)
{
  const uint32_t now = in_now();
  in_sample(now);
  in_tick(now);
  return in_pop();
}

bool input_raw(uint8_t which)
{
  if (which >= INPUT_BTN_N) {
    return false;
  }
  return s_down[which];
}

uint32_t input_hold_ms(uint8_t which)
{
  if (which >= INPUT_BTN_N || !s_down[which]) {
    return 0;
  }
  return (uint32_t)(in_now() - s_press_ms[which]);
}

void input_flush(void)
{
  in_queue_clear();
  s_pending_tap = false;
  s_state = (s_down[INPUT_BTN_L] || s_down[INPUT_BTN_R]) ? ST_SWALLOW : ST_IDLE;
}
