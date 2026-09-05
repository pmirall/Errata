// =============================================================================
//  PEBBLEBOL - hardware/power.cpp
//  See power.h for the ladder, the four hooks and the pin argument that makes
//  this light sleep rather than deep sleep.
//
//  The whole file is host-compilable: everything that needs ESP-IDF sits behind
//  #if defined(ARDUINO), so tests/test_power.cpp drives every transition, the
//  radio release and the frame-rate clamp with no board.
//
//  No heap, no float, no String, no blocking call except the sleep itself -
//  which is the point of it.
// =============================================================================
#include "power.h"

#if defined(ARDUINO)
  #include <Arduino.h>
  #include "esp_sleep.h"
  #include "driver/gpio.h"
  #include "soc/soc_caps.h"

  // THE COPY IS PINNED TO THE CORE'S OWN MASK. power.h spells GPIO0..GPIO5 out
  // as a literal because the host build has no soc_caps.h; this is what stops
  // that literal from becoming a memory of what the chip used to be.
  static_assert((uint64_t)PWR_DEEP_WAKE_MASK ==
                (uint64_t)SOC_GPIO_DEEP_SLEEP_WAKE_VALID_GPIO_MASK,
                "power.h's PWR_DEEP_WAKE_MASK has drifted from the core's "
                "SOC_GPIO_DEEP_SLEEP_WAKE_VALID_GPIO_MASK");
  static_assert(SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP == 1,
                "this chip has no GPIO deep-sleep wakeup at all - power.h's "
                "argument needs rewriting, not relaxing");
#endif

// ---- module state (20 B of .bss) --------------------------------------------
static const PowerHooks* s_hooks     = nullptr;
static uint8_t  s_state    = (uint8_t)PWR_ACTIVE;
static uint8_t  s_woke     = 0;        // pwr_yield() ended on a button
static uint32_t s_slept_ms = 0;

// The DIAG window: one counter per rung, plus the cursor of the second they are
// being filled for. uint16 is enough - a 1 ms loop is 1000/s and the fastest
// this firmware has ever run a frame is FPS_NORMAL.
static uint16_t s_loops_now[PWR_STATE_COUNT];
static uint16_t s_loops_last[PWR_STATE_COUNT];
static uint32_t s_window_ms    = 0;
// s_window_ms alone cannot say "no window has started yet": millisecond zero is
// a legal reading, and using 0 as the sentinel made the first window restart on
// every call until the clock left 0 - which billed 101 passes to a window that
// held 100. Found by tests/test_power.cpp's loop-counter case, not by reading.
static uint8_t  s_window_armed = 0;

// =============================================================================
//  THE PURE HALF
// =============================================================================
uint8_t pwr_decide(const PowerInput& in)
{
  uint8_t s = (uint8_t)PWR_ACTIVE;
  if      (in.idle_ms >= (uint32_t)PWR_SLEEP_MS) s = (uint8_t)PWR_SLEEP;
  else if (in.idle_ms >= (uint32_t)PWR_IDLE_MS)  s = (uint8_t)PWR_IDLE;
  else if (in.idle_ms >= (uint32_t)PWR_DIM_MS)   s = (uint8_t)PWR_DIM;

  // A held radio job clamps the ladder at DIM. See power.h: it holds off the
  // rungs that DROP the radio, and deliberately not the one that only dims.
  if (in.held && s > (uint8_t)PWR_DIM) s = (uint8_t)PWR_DIM;
  return s;
}

uint8_t pwr_fps(uint8_t want)
{
  if (s_state >= (uint8_t)PWR_IDLE && want > (uint8_t)PWR_IDLE_FPS) {
    return (uint8_t)PWR_IDLE_FPS;
  }
  return want;
}

uint32_t pwr_slice_ms(void)
{
  switch (s_state) {
    case (uint8_t)PWR_IDLE:  return (uint32_t)PWR_IDLE_SLICE_MS;
    case (uint8_t)PWR_SLEEP: return (uint32_t)PWR_SLEEP_SLICE_MS;
    default:                 return 0u;      // ACTIVE and DIM never stop the CPU
  }
}

// =============================================================================
//  LIFECYCLE
// =============================================================================
void pwr_bind(const PowerHooks* h) { s_hooks = h; }

static uint16_t s_tick_stalls;   // defined with pwr_tick_budget(); per-boot
static uint32_t s_tick_lost_s;

void pwr_begin(void)
{
  s_state     = (uint8_t)PWR_ACTIVE;
  s_woke      = 0;
  s_slept_ms  = 0;
  s_tick_stalls = 0;             // per-boot, like every counter on this page
  s_tick_lost_s = 0;
  s_window_ms    = 0;
  s_window_armed = 0;
  for (uint8_t i = 0; i < (uint8_t)PWR_STATE_COUNT; ++i) {
    s_loops_now[i]  = 0;
    s_loops_last[i] = 0;
  }
}

uint8_t pwr_state(void) { return s_state; }

// =============================================================================
//  THE TRANSITIONS
//  Every hook fires on an EDGE. The ladder is a superset chain, so going down
//  applies what the new rung adds and going up undoes it, in the mirror order:
//  the panel comes back before the contrast does, so the first frame after a
//  wake is drawn at the brightness the user chose and not at 40.
// =============================================================================
static void hook_dim(bool on)     { if (s_hooks && s_hooks->dim)     s_hooks->dim(on); }
static void hook_panel(bool on)   { if (s_hooks && s_hooks->panel)   s_hooks->panel(on); }
static void hook_release(void)    { if (s_hooks && s_hooks->release) s_hooks->release(); }
static void hook_persist(void)    { if (s_hooks && s_hooks->persist) s_hooks->persist(); }

uint8_t pwr_service(const PowerInput& in)
{
  const uint8_t want = pwr_decide(in);
  if (want == s_state) return s_state;

  if (want > s_state) {
    // Going DOWN the ladder, one rung at a time so no step is skipped when the
    // loop was late (a long save, an offline catch-up) and the ladder jumps
    // two rungs in one pass.
    while (s_state < want) {
      switch (s_state + 1) {
        case (uint8_t)PWR_DIM:
          hook_dim(true);
          break;
        case (uint8_t)PWR_IDLE:
          // ORDER MATTERS. The radio goes first: hook_release() navigates, the
          // leaving screen's leave() hook cancels the scan job, and only then
          // is it true that nothing is mid-transaction. The save is last, so
          // it records a device that has already let go of the radio.
          hook_release();
          hook_panel(false);
          hook_persist();
          break;
        case (uint8_t)PWR_SLEEP:
          // Nothing to apply: SLEEP adds only that pwr_slice_ms() becomes
          // non-trivial, which the entry point reads on its next pass.
          break;
        default:
          break;
      }
      ++s_state;
    }
  } else {
    while (s_state > want) {
      switch (s_state) {
        case (uint8_t)PWR_SLEEP:
          break;
        case (uint8_t)PWR_IDLE:
          hook_panel(true);
          break;
        case (uint8_t)PWR_DIM:
          hook_dim(false);
          break;
        default:
          break;
      }
      --s_state;
    }
  }
  return s_state;
}

// =============================================================================
//  THE SLEEP
// =============================================================================
uint32_t pwr_yield(uint32_t budget_ms)
{
  const uint32_t slice = pwr_slice_ms();
  if (slice == 0u || budget_ms == 0u) return 0u;
  if (budget_ms > slice) budget_ms = slice;

#if defined(ARDUINO)
  // Both buttons, every time. They are INPUT_PULLUP (hardware/input.cpp), so
  // pressed reads LOW and the wake is on the level rather than on an edge -
  // which is why a press cannot be missed however long the slice is, and why
  // this beats polling with delay() at every rung below DIM.
  //
  // gpio_wakeup_enable() carries no pin-mask restriction: in LIGHT sleep the
  // digital pad domain stays powered and all 22 GPIOs can wake the chip. This
  // is the exact line that would have to become
  // esp_deep_sleep_enable_gpio_wakeup(BIT(PIN_BTN_R), ...) - one button - if
  // this were a deep sleep. See power.h.
  (void)gpio_wakeup_enable((gpio_num_t)PIN_BTN_L, GPIO_INTR_LOW_LEVEL);
  (void)gpio_wakeup_enable((gpio_num_t)PIN_BTN_R, GPIO_INTR_LOW_LEVEL);
  (void)esp_sleep_enable_gpio_wakeup();
  (void)esp_sleep_enable_timer_wakeup((uint64_t)budget_ms * 1000ULL);

  const uint32_t t0 = (uint32_t)millis();
  const esp_err_t rc = esp_light_sleep_start();
  const uint32_t dt = (uint32_t)millis() - t0;

  // A button held down when we tried to sleep makes the level wake fire at
  // once; that is a wake, not a failure, and the caller must treat it as one.
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO) s_woke = 1;

  // Disarm the pins again. Leaving a level-triggered wake armed while awake is
  // harmless for sleep but it is state this module would then be sharing with
  // hardware/input.cpp, which owns those pins.
  (void)gpio_wakeup_disable((gpio_num_t)PIN_BTN_L);
  (void)gpio_wakeup_disable((gpio_num_t)PIN_BTN_R);
  (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);

  if (rc != ESP_OK) return 0u;          // refused: the caller just loops again
  s_slept_ms += dt;
  return dt;
#else
  // Host: record the request and return 0. A test asserts on what the ladder
  // ASKED for; nothing here may actually stop.
  s_slept_ms += budget_ms;
  return 0u;
#endif
}

bool pwr_take_wake(void)
{
  const bool w = (s_woke != 0u);
  s_woke = 0;
  return w;
}

uint32_t pwr_slept_ms(void) { return s_slept_ms; }

// =============================================================================
//  THE TICK BUDGET, AND THE STALL IT NOW COUNTS (P7-C6)
//  See power.h for why this arithmetic lives here rather than in app_loop().
// =============================================================================
uint32_t pwr_tick_budget(uint32_t now_ms, uint32_t& tick_ms)
{
  // Wrap-safe: the subtraction is unsigned and the anchor is only ever moved
  // forward by what this function returns.
  const uint32_t owed = (uint32_t)(now_ms - tick_ms) / 1000UL;
  if (owed == 0u) return 0u;

  if (owed > (uint32_t)NT_TICK_MAX_OWED_S) {
    // A STALL, NOT A SLEEP. Resynchronise and charge one second - which is what
    // this has always done - but say so, because a device that silently
    // discards minutes is indistinguishable from one that never stalls.
    const uint32_t lost = owed - 1u;
    if (s_tick_stalls < 0xFFFFu) s_tick_stalls++;
    s_tick_lost_s = (s_tick_lost_s > 0xFFFFFFFFul - lost) ? 0xFFFFFFFFul
                                                          : s_tick_lost_s + lost;
    tick_ms = now_ms;
    return 1u;
  }
  tick_ms += owed * 1000UL;
  return owed;
}

uint16_t pwr_tick_stalls(void) { return s_tick_stalls; }
uint32_t pwr_tick_lost_s(void) { return s_tick_lost_s; }

// =============================================================================
//  DIAGNOSTICS
// =============================================================================
void pwr_note_loop(uint32_t now_ms)
{
  if (!s_window_armed) {
    s_window_ms    = now_ms;
    s_window_armed = 1;
  }

  // Close the window every whole second. A window longer than one second (the
  // device slept through it) is reported as-is rather than scaled: "loops per
  // second" of a rung that ran for eight seconds and looped once is 1, and
  // scaling it to 0.125 would need a float to say so.
  if ((uint32_t)(now_ms - s_window_ms) >= 1000u) {
    for (uint8_t i = 0; i < (uint8_t)PWR_STATE_COUNT; ++i) {
      s_loops_last[i] = s_loops_now[i];
      s_loops_now[i]  = 0;
    }
    s_window_ms = now_ms;
  }
  if (s_state < (uint8_t)PWR_STATE_COUNT && s_loops_now[s_state] < 0xFFFFu) {
    s_loops_now[s_state]++;
  }
}

uint16_t pwr_loops_per_s(uint8_t state)
{
  if (state >= (uint8_t)PWR_STATE_COUNT) return 0;
  // The rung we are in has a partial window; the others have none, so their
  // last completed one is all there is to show.
  if (state == s_state && s_loops_last[state] == 0u) return s_loops_now[state];
  return s_loops_last[state];
}
