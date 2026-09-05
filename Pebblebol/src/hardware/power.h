// =============================================================================
//  PEBBLEBOL - hardware/power.h
//  THE IDLE LADDER (plan P6-C3, spec section 67 "Device sleeps correctly").
//
//      ACTIVE  ->  DIM  ->  IDLE  ->  SLEEP
//        0 s      30 s     2 min    10 min      since the last gesture
//
//  Each rung is a SUPERSET of the one above it, which is what makes the ladder
//  reversible on any gesture with no unwinding to do:
//    DIM    rd_contrast_ramp(PWR_DIM_CONTRAST). Nothing else is told. The panel
//           stays on, the radio is untouched, a scan in flight is untouched.
//    IDLE   + rd_power(false), the frame rate clamped to PWR_IDLE_FPS, the
//           radio released THROUGH THE JOB (see below), and one forced save
//           while a clean save is still cheap.
//    SLEEP  + the CPU is stopped between logic ticks.
//
//  PURE DECISION, DEVICE EFFECTS BEHIND A SEAM. pwr_decide() and pwr_fps() are
//  total, stateless and clock-free: everything they need is pushed in as a
//  PowerInput, so tests/test_power.cpp drives the whole ladder on the host with
//  no display, no radio and no board. The four things the ladder cannot do
//  itself are bound as PowerHooks by app/app.cpp.
//
//  Identifiers and comments English; nothing here is user-facing.
//
// -----------------------------------------------------------------------------
//  WHY THIS SLEEPS LIGHT AND NOT DEEP - THE PINS DECIDE, NOT A PREFERENCE
// -----------------------------------------------------------------------------
//  Plan P6-C3 says deep sleep "only if D1 gives buttons on GPIO0-5, otherwise
//  light sleep with gpio_wakeup_enable". D1 is DEFERRED (docs/decisions.md), so
//  the map is PIN_BTN_L 10 and PIN_BTN_R 2, and the condition resolves NO:
//
//    * The ESP32-C3 has no RTC IO subsystem (SOC_RTCIO_PIN_COUNT is 0). The
//      only pins that can wake it from DEEP sleep are GPIO0..GPIO5, the pads
//      still powered by VDD3P3_RTC - SOC_GPIO_DEEP_SLEEP_WAKE_VALID_GPIO_MASK
//      in the installed core, pinned against PWR_DEEP_WAKE_MASK below by a
//      static_assert in power.cpp so this is a fact and not a memory.
//      esp_deep_sleep_enable_gpio_wakeup() returns ESP_ERR_INVALID_ARG for any
//      other pin, and ext0/ext1 do not exist on this chip.
//    * PIN_BTN_R 2 is inside that set. PIN_BTN_L 10 is not, and no firmware
//      change can put it there.
//    * From LIGHT sleep every GPIO can wake the chip (gpio_wakeup_enable() +
//      esp_sleep_enable_gpio_wakeup()), because the digital pad domain stays
//      powered. Both buttons work.
//
//  SAID PLAINLY, because it is a product fact and not an implementation
//  detail: on this pin map a deep sleep would leave the LEFT BUTTON DEAD. It
//  would also make every wake a reset (ESP_RST_DEEPSLEEP) taken with the
//  player's finger on GPIO2 - a strapping pin, per config.h section 2 - which
//  nothing in this project has ever tested on hardware. Light sleep costs
//  roughly a tenth of the battery-life estimate (docs/decisions.md, D1
//  consequences) and buys back both buttons and the screen the player left.
//
//  THE TRIPWIRE, not a dead code path: if a future D1 ever moves both buttons
//  into the wake domain, PWR_DEEP_WAKE_BOTH_BUTTONS becomes 1 and the
//  static_assert below fails the build with a message naming the choice. A
//  deep-sleep branch compiled out behind that condition would be a path nobody
//  has ever built; a red line puts a human in front of it instead.
// -----------------------------------------------------------------------------
#ifndef PB_POWER_H
#define PB_POWER_H

#include <stdint.h>

#include "../core/config.h"     // PWR_* thresholds, PIN_BTN_L / PIN_BTN_R

// -----------------------------------------------------------------------------
// THE DEEP-SLEEP WAKE DOMAIN OF THIS CHIP, as a compile-time predicate.
//
// The literal is the ESP32-C3's SOC_GPIO_DEEP_SLEEP_WAKE_VALID_GPIO_MASK
// (GPIO0..GPIO5). It is spelled out here rather than included because this
// header is compiled on the host too, where soc_caps.h does not exist;
// power.cpp static_asserts the two against each other on the target, so the
// copy cannot drift from the core.
// -----------------------------------------------------------------------------
#define PWR_DEEP_WAKE_MASK        0x3Fu     // GPIO0..GPIO5

#define PWR_PIN_CAN_DEEP_WAKE(p)  (((p) >= 0) && ((p) < 32) && \
                                   (((PWR_DEEP_WAKE_MASK >> (p)) & 1u) != 0u))

// Plan P6-C3's D1 condition, evaluated rather than remembered.
#define PWR_DEEP_WAKE_BOTH_BUTTONS \
  (PWR_PIN_CAN_DEEP_WAKE(PIN_BTN_L) && PWR_PIN_CAN_DEEP_WAKE(PIN_BTN_R))

static_assert(!PWR_DEEP_WAKE_BOTH_BUTTONS,
              "D1 has moved BOTH buttons into the ESP32-C3 deep-sleep wake "
              "domain (GPIO0..5). PWR_SLEEP light-sleeps only because "
              "PIN_BTN_L could not wake the chip; that reason has expired. "
              "Design the deep-sleep rung, check GPIO strapping at wake on a "
              "real board, and re-read the cooldown note in game/cooldowns.h "
              "before deleting this assert.");

// -----------------------------------------------------------------------------
// The rungs. Ordered: a HIGHER value is a deeper state, and pwr_decide() is
// monotonic in idle_ms, which is what the host cases assert.
// -----------------------------------------------------------------------------
enum PowerState : uint8_t {
  PWR_ACTIVE = 0,
  PWR_DIM,
  PWR_IDLE,
  PWR_SLEEP,
  PWR_STATE_COUNT
};

// -----------------------------------------------------------------------------
// Everything the ladder decides with. Pushed in, so the decision is pure.
//   idle_ms - milliseconds since the last gesture the entry point dispatched.
//             DELIBERATELY NOT sm_idle_ms(), and the reason is a bug that was
//             measured rather than argued: sm_goto() resets the navigation
//             idle clock on every screen change, and the IDLE rung's own
//             release() navigates HOME - so with sm_idle_ms() as the input the
//             ladder reset its own timer at the instant it reached IDLE, went
//             straight back to ACTIVE, turned the panel on again and repeated
//             for ever. app.cpp stamps its own clock next to ui_handle().
//   held    - something must not be interrupted. TODAY that is exactly "a
//             screen is holding a radio job" (wifi_scan_is_busy()), which is
//             plan P6-C3's "a screen holding a radio job counts as activity
//             for the idle timer". It clamps the ladder at DIM rather than at
//             ACTIVE on purpose: dimming the panel is reversible and disturbs
//             nothing, while IDLE is the rung that drops the radio, and that
//             is the one a live job must be able to hold off.
// -----------------------------------------------------------------------------
struct PowerInput {
  uint32_t idle_ms;
  uint8_t  held;        // 0 / 1
};

// -----------------------------------------------------------------------------
// pwr_decide(in) -> PowerState
//   Total, stateless, clock-free. The whole ladder, and the only place the four
//   thresholds are compared.
// -----------------------------------------------------------------------------
uint8_t pwr_decide(const PowerInput& in);

// -----------------------------------------------------------------------------
// pwr_fps(want) -> the frame rate this power state allows
//   `want` is what the screen asked for (ui_fps()). Clamped to PWR_IDLE_FPS
//   from IDLE down. This is a CLAMP and not a hook because app_loop() calls
//   rd_set_fps(ui_fps()) every pass: a rate the ladder merely SET would be
//   overwritten on the next frame, which is the shape of bug this replaces.
// -----------------------------------------------------------------------------
uint8_t pwr_fps(uint8_t want);

// -----------------------------------------------------------------------------
// pwr_slice_ms() -> how long the CPU may be stopped in one go, 0 = not at all
//   0 in ACTIVE and DIM. PWR_IDLE_SLICE_MS in IDLE, PWR_SLEEP_SLICE_MS in
//   SLEEP. The caller shortens it further to the next thing it owes (the 1 Hz
//   logic tick), so the simulation never has a gap to catch up.
// -----------------------------------------------------------------------------
uint32_t pwr_slice_ms(void);

// -----------------------------------------------------------------------------
// THE FOUR THINGS THE LADDER CANNOT DO ITSELF. A pointer to a const struct in
// flash, so this costs one word of globals and not six.
//
//   dim(on)     rd_contrast_ramp() to PWR_DIM_CONTRAST, or back to the user's
//               own brightness. The ladder does not know Config.
//   panel(on)   rd_power().
//   release()   DROP THE RADIO - and READ WHY THIS IS NOT net_request().
//               ui_home() navigates, sm_goto() runs the leaving screen's
//               leave() hook, and network_leave() calls wifi_scan_cancel(),
//               which is the single path that stops the driver and releases
//               the radio exactly once. A bare net_request(RADIO_OFF) from
//               here would take the radio out from under a running
//               WifiScanJob: the job would stay WSCAN_RUNNING with stopped==0,
//               wifi_scan_is_busy() would go on answering true, and the next
//               poll would report FAILED to a player who caused nothing. The
//               job is a file-static inside ui/screen_network.cpp and is
//               unreachable from here BY DESIGN, so navigating is not a
//               workaround, it is the interface. (tools/check.sh gates this
//               file for the radio call; tests/test_power.cpp is the case that
//               shows the job ends CANCELLED and stopped.)
//   persist()   save_touch_lastseen(gt_now()) + gs_save_active(true), once, on
//               the way into IDLE, while a clean save is still cheap and the
//               device is definitely awake to do it.
// -----------------------------------------------------------------------------
struct PowerHooks {
  // dim(on): app.cpp routes this to ui_note_power_dim(), so PWR_DIM_CONTRAST
  // joins the user's brightness and the asleep-pet dim in ui.cpp's ONE contrast
  // owner instead of becoming a fourth writer racing them through
  // rd_contrast_ramp().
  void (*dim)(bool on);
  void (*panel)(bool on);
  void (*release)(void);
  void (*persist)(void);
};

// Bind once from app_setup(), before pwr_begin(). A null pointer, or a null
// member, is simply not called: the ladder still tracks state, which is what
// lets a host test run it with two hooks bound and the rest absent.
void pwr_bind(const PowerHooks* h);

// Back to PWR_ACTIVE with no effects applied and the counters cleared. Call
// once from setup(), and from a test between cases.
void pwr_begin(void);

// -----------------------------------------------------------------------------
// pwr_service(in) -> the state now in force
//   Runs the ladder and applies exactly the transitions it needs. Idempotent
//   inside a rung: the hooks fire on the EDGE, never per loop.
// -----------------------------------------------------------------------------
uint8_t pwr_service(const PowerInput& in);

// The rung currently in force.
uint8_t pwr_state(void);

// -----------------------------------------------------------------------------
// pwr_yield(budget_ms) -> milliseconds the CPU was actually stopped for
//   ON THE TARGET: arms BOTH buttons as light-sleep wake sources
//   (gpio_wakeup_enable + esp_sleep_enable_gpio_wakeup), arms a timer for
//   budget_ms and calls esp_light_sleep_start(). Returns when either fires.
//   budget_ms is clamped to pwr_slice_ms(), so ACTIVE and DIM never sleep.
//   ON THE HOST: records the request and returns 0. Nothing sleeps in a test.
//
//   TWO THINGS A BENCH WILL SEE THAT NO HOST TEST CAN. (1) The board is
//   CDCOnBoot=cdc, so the Serial console rides the USB Serial/JTAG peripheral;
//   a light sleep is very likely to drop that link while the device is plugged
//   in, and a terminal going quiet after ten idle minutes is the ladder
//   working, not a crash. Nothing here special-cases USB - there is no cheap
//   "am I enumerated" query, and inventing one would be a guess. (2) A sleep
//   the driver refuses (ESP_ERR_INVALID_STATE, e.g. a stack still resident)
//   returns 0 and the loop simply carries on; the radio is already off at this
//   rung, so it should not happen, and it is not treated as an error if it does.
//
//   THE CLOCK IS NOT TOLD ANYTHING, and that is deliberate. Light sleep is not
//   a reset: esp_timer is resynchronised from the RTC on the way out, and
//   hardware/gametime.cpp's monotonic base is the RTC counter itself, so
//   millis(), gt_now() and the cooldown table's own millisecond deadlines all
//   come back carrying the gap with no bookkeeping here.
// -----------------------------------------------------------------------------
uint32_t pwr_yield(uint32_t budget_ms);

// -----------------------------------------------------------------------------
// pwr_take_wake()
//   True exactly once after pwr_yield() was ended by a BUTTON rather than by
//   its timer, then false until the next one. The entry point uses it to
//   swallow the gesture that did the waking: the panel was dark, so that press
//   was aimed at the device and not at whatever screen it happens to be on.
// -----------------------------------------------------------------------------
bool pwr_take_wake(void);

// -----------------------------------------------------------------------------
// DIAGNOSTICS (plan P6-C3: "DIAG shows loop iterations/s per power state").
//   pwr_note_loop()   one app_loop() pass, counted against the current rung.
//   pwr_loops_per_s(state) the last completed one-second window's count for
//                     that rung, or the live count when the rung is the
//                     current one and no window has closed yet.
//   Four uint16 counters and a window cursor. The window is driven by the
//   caller's millisecond clock so this module still owns no clock.
// -----------------------------------------------------------------------------
void     pwr_note_loop(uint32_t now_ms);
uint16_t pwr_loops_per_s(uint8_t state);

// Total milliseconds pwr_yield() has stopped the CPU for since pwr_begin().
// Diagnostics and host cases; nothing decides with it.
uint32_t pwr_slept_ms(void);

#endif  // PB_POWER_H
