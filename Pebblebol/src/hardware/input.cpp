// =============================================================================
//  PEBBLEBOL - hardware/input.cpp
//  Two-button debounce + gesture recogniser (GAME_DESIGN 8.1).
//
//  DESIGN NOTES (the reasons this file looks the way it does)
//
//  1. NO GESTURE IS EVER EMITTED ON A PRESS EDGE. A single press is classified
//     the moment it is RELEASED, which is what keeps the chord free of the
//     classic "BOTH is preceded by a spurious TAP" bug: BOTH is decided when
//     the second button goes DOWN, and at that instant the first button has
//     produced nothing yet, so there is nothing to retract.
//
//     Until P3-C4a a release only ARMED a tap, which was then emitted
//     DOUBLE_TAP_WINDOW_MS (280 ms) later if no second press arrived. That
//     bought GST_DBL_L/R and cost 280 ms of latency on every single press in
//     the product - a quarter of a second between the button going up and the
//     screen reacting, on a device whose minigames are scored on reaction
//     time. The double tap is gone (see the commit) and a TAP now lands one
//     debounce interval after release, ~25 ms.
//
//  2. Holds fire exactly ONCE when the threshold is crossed. HOLD_L then keeps
//     repeating every REPEAT_RATE_MS because GAME_DESIGN 8.3 uses it as the
//     fast-scroll of every list. HOLD_R is BACK on almost every screen and
//     therefore never repeats here; the one screen that needs a repeating right
//     button (the CLOCK time-entry screen) times it itself from input_hold_ms().
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
//
//  6. ON THE DEVICE the pins are sampled by a 5 ms esp_timer (INPUT_POLL_MS is
//     finally real), not by loop(). Every level CHANGE is pushed into a 16-deep
//     single-producer/single-consumer ring of {ms, levels} and input_poll()
//     replays it into the FSM below at the instant each edge actually happened.
//     A 300 ms stall in loop() - a flash write, an offline catch-up, the god
//     `stall` command - therefore costs the player nothing: the press is still
//     recognised, with its true duration, on the next poll. The FSM itself is
//     UNCHANGED, and the host path (below, `#else`) still samples inline so the
//     scripted-waveform test drives exactly the code that shipped.
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
  #include "esp_timer.h"
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


static bool     s_edge[INPUT_BTN_N];   // unconsumed press edge, input_pressed_edge()
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
// 4. EDGE HANDLERS
//    t is the instant of the physical edge, not the instant the debouncer
//    accepted it, so every duration below matches the real waveform.
// -----------------------------------------------------------------------------
static void in_on_press(uint8_t i, uint32_t t)
{
  const uint8_t other = (uint8_t)(INPUT_BTN_N - 1 - i);

  s_press_ms[i] = t;
  // BEFORE every early return below. input_pressed_edge() reports the physical
  // press, not the gesture it eventually becomes, so a press that goes on to
  // be swallowed as half a chord must still be visible to a minigame.
  s_edge[i] = true;

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
  // Nothing to reconcile with an earlier press any more: a TAP is emitted at
  // its own release, so by the time this runs the previous episode has already
  // produced its gesture and the queue order is right by construction.
  s_active = i;
  s_state  = ST_DOWN;
}

// `t`, the instant of the physical release edge, is deliberately unnamed: every
// gesture this function can still emit is decided by the STATE, not by when the
// release happened. It was the pending tap that needed the timestamp, to time
// the double-tap window out, and that is gone. The parameter stays because the
// caller replays real edge instants from the timer ring and a future gesture
// (a "flick", a release-velocity anything) would want it back.
static void in_on_release(uint8_t i, uint32_t /* t */)
{
  const uint8_t other = (uint8_t)(INPUT_BTN_N - 1 - i);

  switch (s_state) {

    case ST_DOWN:
      // Short press, and the button is up: that is a TAP and there is nothing
      // left to wait for. HOLD would already have fired from in_tick() at
      // HOLD_MS and moved the state to ST_HOLD, so reaching here means the
      // press really was short.
      if (s_active == i) {
        in_emit(i == INPUT_BTN_L ? GST_TAP_L : GST_TAP_R);
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
//    `levels` is a bitmask of PRESSED buttons, bit i = INPUT_BTN_*. Splitting
//    it out of the pin read is what lets the timer ring replay an old sample at
//    the instant it was taken.
// -----------------------------------------------------------------------------
static void in_apply(uint32_t now, uint8_t levels)
{
  for (uint8_t i = 0; i < INPUT_BTN_N; i++) {
    const bool pressed = ((levels >> i) & 1u) != 0u;

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

// Read both pins right now, as a bitmask.
static uint8_t in_read_levels(void)
{
  uint8_t lv = 0;
  for (uint8_t i = 0; i < INPUT_BTN_N; i++) {
    if (in_level(i) == BTN_ACTIVE_LEVEL) lv = (uint8_t)(lv | (1u << i));
  }
  return lv;
}

static void in_sample(uint32_t now)
{
  in_apply(now, in_read_levels());
}

#if defined(ARDUINO)
// -----------------------------------------------------------------------------
// 5b. TIMER SAMPLER + SPSC EDGE RING (device only)
//     Producer: the esp_timer callback, every INPUT_POLL_MS. Consumer:
//     input_poll(), from loop(). One producer and one consumer means the two
//     indices need no lock - the producer only ever advances the tail, the
//     consumer only ever advances the head - as long as each is written by
//     exactly one side and read as a whole, which a byte is.
//
//     Only CHANGES are queued. A run of identical samples carries no
//     information the FSM can use: what it needs is the instant of each edge,
//     and input_poll() finishes with a live sample at the current time, which
//     is what closes the debounce window of the last queued edge. Sixteen
//     entries is therefore sixteen EDGES, far more than a human generates
//     inside any stall the firmware can survive.
// -----------------------------------------------------------------------------
#define IN_RING_LEN 16                    // power of two
#define IN_RING_MASK (IN_RING_LEN - 1)

struct InSample {
  uint32_t ms;
  uint8_t  levels;
};

static volatile InSample s_ring[IN_RING_LEN];
static volatile uint8_t  s_ring_head = 0;   // consumer cursor
static volatile uint8_t  s_ring_tail = 0;   // producer cursor
static uint8_t           s_ring_last = 0;   // last levels the producer queued
static uint32_t          s_fsm_ms    = 0;   // newest instant already fed to the FSM
static esp_timer_handle_t s_timer    = nullptr;

static void in_timer_cb(void *)
{
  const uint8_t lv = in_read_levels();
  if (lv == s_ring_last) {
    return;                                 // no edge, nothing to record
  }
  s_ring_last = lv;

  const uint8_t tail = s_ring_tail;
  const uint8_t next = (uint8_t)((tail + 1u) & IN_RING_MASK);
  if (next == s_ring_head) {
    return;                                 // full: keep the oldest edges
  }
  s_ring[tail].ms     = (uint32_t)millis();
  s_ring[tail].levels = lv;
  s_ring_tail         = next;               // publish last
}

static bool in_ring_pop(InSample& out)
{
  const uint8_t head = s_ring_head;
  if (head == s_ring_tail) {
    return false;
  }
  out.ms     = s_ring[head].ms;
  out.levels = s_ring[head].levels;
  s_ring_head = (uint8_t)((head + 1u) & IN_RING_MASK);
  return true;
}

static void in_timer_begin(void)
{
  s_ring_head = 0;
  s_ring_tail = 0;
  s_ring_last = in_read_levels();
  s_fsm_ms    = (uint32_t)millis();
  if (s_timer != nullptr) {
    return;                                 // input_begin() is callable twice
  }
  esp_timer_create_args_t a = {};
  a.callback = &in_timer_cb;
  a.name     = "btn";
  if (esp_timer_create(&a, &s_timer) != ESP_OK) {
    s_timer = nullptr;                      // fall back to polling in loop()
    return;
  }
  if (esp_timer_start_periodic(s_timer, (uint64_t)INPUT_POLL_MS * 1000ULL) != ESP_OK) {
    esp_timer_delete(s_timer);
    s_timer = nullptr;
  }
}
#endif  // ARDUINO

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

}

// -----------------------------------------------------------------------------
// 7. PUBLIC API
// -----------------------------------------------------------------------------
void input_begin(void)
{
  const uint32_t now = in_now();

  in_pins_begin();
  in_queue_clear();

  s_edge[INPUT_BTN_L] = false;
  s_edge[INPUT_BTN_R] = false;
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

#if defined(ARDUINO)
  in_timer_begin();
#endif
}

Gesture input_poll(void)
{
#if defined(ARDUINO)
  // Replay every edge the 5 ms sampler caught since the last call, each at its
  // own instant, so a stalled loop() loses no press and no press DURATION.
  // The clamp keeps the instants fed to the FSM monotonic: the live sample at
  // the end of the previous call may have raced ahead of the timer, and every
  // duration in in_tick() is a (uint32_t)(now - then) that would underflow into
  // a phantom HOLD if time ever ran backwards.
  InSample sm;
  while (in_ring_pop(sm)) {
    if ((int32_t)(sm.ms - s_fsm_ms) < 0) sm.ms = s_fsm_ms;
    in_apply(sm.ms, sm.levels);
    in_tick(sm.ms);
    s_fsm_ms = sm.ms;
  }
#endif
  const uint32_t now = in_now();
  in_sample(now);
  in_tick(now);
#if defined(ARDUINO)
  s_fsm_ms = now;
#endif
  return in_pop();
}

bool input_raw(uint8_t which)
{
  if (which >= INPUT_BTN_N) {
    return false;
  }
  return s_down[which];
}

bool input_pressed_edge(uint8_t which)
{
  if (which >= INPUT_BTN_N) return false;
  const bool e = s_edge[which];
  s_edge[which] = false;        // consumed: one physical press, one true
  return e;
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
#if defined(ARDUINO)
  // Drop the queued edges too: they belong to the episode being swallowed.
  s_ring_head = s_ring_tail;
#endif
  in_queue_clear();
  // The unconsumed press edges belong to the episode being swallowed too. A
  // survivor here would hand a minigame a press from before it started.
  s_edge[INPUT_BTN_L] = false;
  s_edge[INPUT_BTN_R] = false;
  s_state = (s_down[INPUT_BTN_L] || s_down[INPUT_BTN_R]) ? ST_SWALLOW : ST_IDLE;
}
