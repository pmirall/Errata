// =============================================================================
//  Pebblebol host tests - test_power.cpp
//  P6-C3: the idle ladder (hardware/power.h) and the carried radio debt.
//
//  THE WHOLE LADDER RUNS HERE WITH NO BOARD, which is the reason power.cpp is
//  written the way it is: pwr_decide() and pwr_fps() are pure functions of a
//  pushed-in PowerInput, and the four things the ladder can DO to the device
//  are function pointers. So this file drives ten idle minutes in a few
//  microseconds, watches which hooks fired in which order, and - the case this
//  file exists for - holds a REAL WifiScanJob against a fake driver while the
//  ladder drops the radio, to show the job ends CANCELLED and released rather
//  than left running behind its own back.
//
//  What it deliberately does NOT test: esp_light_sleep_start(). pwr_yield()
//  compiles to "record the request and return 0" off the target, so the cases
//  below assert what the ladder ASKED for and never that anything slept. The
//  sleep itself has never run on hardware and this file does not pretend it
//  has.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "hardware/power.h"
#include "networking/wifi_scanner.h"

// -----------------------------------------------------------------------------
//  THE FAKE DEVICE. Each hook records that it fired and what with, in order, so
//  a case can assert "the radio went first, then the panel, then the save"
//  rather than only "all three happened".
// -----------------------------------------------------------------------------
#define FX_CAP 32
static char     s_log[FX_CAP][8];
static uint8_t  s_n      = 0;
static uint8_t  s_dim    = 0;      // last value passed to dim()
static uint8_t  s_panel  = 1;      // last value passed to panel()
static uint8_t  s_saves  = 0;

static void fx_note(const char* what) {
  if (s_n >= FX_CAP) return;
  snprintf(s_log[s_n], sizeof(s_log[0]), "%s", what);
  ++s_n;
}
static bool fx_seq(uint8_t i, const char* what) {
  return (i < s_n) && (strcmp(s_log[i], what) == 0);
}

static void h_dim(bool on)   { s_dim = on ? 1u : 0u; fx_note(on ? "dim+" : "dim-"); }
static void h_panel(bool on) { s_panel = on ? 1u : 0u; fx_note(on ? "pan+" : "pan-"); }
static void h_persist(void)  { ++s_saves; fx_note("save"); }

// -----------------------------------------------------------------------------
//  THE RADIO, TWO WAYS. `s_job` is a real WifiScanJob driven by a fake driver,
//  exactly as ui/screen_network.cpp owns one; `h_release_through_job` is what
//  app.cpp's hook really does (navigate -> leave() -> wifi_scan_cancel()), and
//  `h_release_bare` is the shortcut hardware/power.h forbids, kept here so one
//  case can show the difference instead of describing it.
// -----------------------------------------------------------------------------
static WifiScanJob s_job;
static uint8_t     s_drv_started = 0;
static uint8_t     s_drv_stopped = 0;
static int16_t     s_drv_poll    = WSCAN_POLL_RUNNING;
static uint8_t     s_bare_off    = 0;

static bool    d_start(void) { ++s_drv_started; return true; }
static int16_t d_poll(void)  { return s_drv_poll; }
static uint8_t d_read(ScanResult* out, uint8_t cap) { (void)out; (void)cap; return 0; }
static void    d_stop(void)  { ++s_drv_stopped; }
static const WifiScanDriver kDrv = { &d_start, &d_poll, &d_read, &d_stop };

static void h_release_through_job(void) {
  fx_note("radio");
  wifi_scan_cancel(s_job, kDrv);      // the ONE path; idempotent
  wifi_scan_reset(s_job);
}
static void h_release_bare(void) {
  fx_note("radio");
  s_bare_off = 1;                     // "net_request(RADIO_OFF)", and nothing else
}

static const PowerHooks kThroughJob = { &h_dim, &h_panel, &h_release_through_job, &h_persist };
static const PowerHooks kBare       = { &h_dim, &h_panel, &h_release_bare,        &h_persist };

static void fresh(const PowerHooks* h) {
  memset(s_log, 0, sizeof s_log);
  s_n = 0; s_dim = 0; s_panel = 1; s_saves = 0;
  s_drv_started = 0; s_drv_stopped = 0; s_drv_poll = WSCAN_POLL_RUNNING;
  s_bare_off = 0;
  wifi_scan_reset(s_job);
  pwr_bind(h);
  pwr_begin();
}

static uint8_t step(uint32_t idle_ms, bool held) {
  PowerInput in;
  in.idle_ms = idle_ms;
  in.held    = held ? 1u : 0u;
  return pwr_service(in);
}

// =============================================================================
//  1. THE PURE DECISION
// =============================================================================
TEST(the_ladder_has_one_rung_per_threshold_and_the_boundaries_are_inclusive) {
  PowerInput in; in.held = 0;
  in.idle_ms = 0;                        CHECK_EQ(pwr_decide(in), (int)PWR_ACTIVE);
  in.idle_ms = PWR_DIM_MS - 1;           CHECK_EQ(pwr_decide(in), (int)PWR_ACTIVE);
  in.idle_ms = PWR_DIM_MS;               CHECK_EQ(pwr_decide(in), (int)PWR_DIM);
  in.idle_ms = PWR_IDLE_MS - 1;          CHECK_EQ(pwr_decide(in), (int)PWR_DIM);
  in.idle_ms = PWR_IDLE_MS;              CHECK_EQ(pwr_decide(in), (int)PWR_IDLE);
  in.idle_ms = PWR_SLEEP_MS - 1;         CHECK_EQ(pwr_decide(in), (int)PWR_IDLE);
  in.idle_ms = PWR_SLEEP_MS;             CHECK_EQ(pwr_decide(in), (int)PWR_SLEEP);
  in.idle_ms = 0xFFFFFFFFu;              CHECK_EQ(pwr_decide(in), (int)PWR_SLEEP);
}

TEST(the_decision_is_total_and_monotonic_over_a_whole_idle_hour) {
  // Every second of an hour, and the answer may only ever go DOWN the ladder.
  // This is what makes "each rung is a superset of the one above" a fact:
  // a non-monotonic map would let the panel come back on without a gesture.
  PowerInput in; in.held = 0;
  uint8_t prev = (uint8_t)PWR_ACTIVE;
  uint8_t seen[PWR_STATE_COUNT] = { 0, 0, 0, 0 };
  for (uint32_t t = 0; t <= 3600000u; t += 1000u) {
    in.idle_ms = t;
    const uint8_t s = pwr_decide(in);
    CHECK(s < (uint8_t)PWR_STATE_COUNT);
    CHECK(s >= prev);
    prev = s;
    seen[s] = 1;
  }
  for (uint8_t i = 0; i < (uint8_t)PWR_STATE_COUNT; ++i) CHECK(seen[i] == 1);
}

TEST(a_held_radio_job_clamps_the_ladder_at_dim_and_never_lower) {
  // Plan P6-C3: "a screen holding a radio job counts as activity for the idle
  // timer". Clamped at DIM and not at ACTIVE on purpose - dimming disturbs
  // nothing, and IDLE is the rung that drops the radio.
  PowerInput in; in.held = 1;
  in.idle_ms = 0;                CHECK_EQ(pwr_decide(in), (int)PWR_ACTIVE);
  in.idle_ms = PWR_DIM_MS;       CHECK_EQ(pwr_decide(in), (int)PWR_DIM);
  in.idle_ms = PWR_IDLE_MS;      CHECK_EQ(pwr_decide(in), (int)PWR_DIM);
  in.idle_ms = PWR_SLEEP_MS;     CHECK_EQ(pwr_decide(in), (int)PWR_DIM);
  in.idle_ms = 0xFFFFFFFFu;      CHECK_EQ(pwr_decide(in), (int)PWR_DIM);
}

// =============================================================================
//  2. THE TRANSITIONS
// =============================================================================
TEST(walking_down_the_ladder_fires_each_hook_once_and_in_the_documented_order) {
  fresh(&kThroughJob);
  CHECK_EQ(step(0, false), (int)PWR_ACTIVE);
  CHECK_EQ(s_n, 0);                              // ACTIVE applies nothing at all

  CHECK_EQ(step(PWR_DIM_MS, false), (int)PWR_DIM);
  CHECK_EQ(s_n, 1);
  CHECK(fx_seq(0, "dim+"));
  CHECK_EQ(s_dim, 1);
  CHECK_EQ(s_panel, 1);                          // DIM leaves the panel alone
  CHECK_EQ(s_saves, 0);

  // Staying inside a rung applies nothing: the hooks are edges, not per-loop.
  CHECK_EQ(step(PWR_DIM_MS + 5000u, false), (int)PWR_DIM);
  CHECK_EQ(s_n, 1);

  CHECK_EQ(step(PWR_IDLE_MS, false), (int)PWR_IDLE);
  CHECK_EQ(s_n, 4);
  CHECK(fx_seq(1, "radio"));                     // the radio goes FIRST
  CHECK(fx_seq(2, "pan-"));
  CHECK(fx_seq(3, "save"));                      // and the save records a device
  CHECK_EQ(s_panel, 0);                          // that has already let go
  CHECK_EQ(s_saves, 1);

  CHECK_EQ(step(PWR_SLEEP_MS, false), (int)PWR_SLEEP);
  CHECK_EQ(s_n, 4);                              // SLEEP adds no effect of its own
}

TEST(no_rung_is_skipped_when_the_loop_arrives_late) {
  // A long save, an offline catch-up or a slow frame can leave app_loop() two
  // rungs behind. Every hook must still fire, in the same order, or a device
  // could reach SLEEP with the radio still held and the panel still lit.
  fresh(&kThroughJob);
  CHECK_EQ(step(PWR_SLEEP_MS + 60000u, false), (int)PWR_SLEEP);
  CHECK_EQ(s_n, 4);
  CHECK(fx_seq(0, "dim+"));
  CHECK(fx_seq(1, "radio"));
  CHECK(fx_seq(2, "pan-"));
  CHECK(fx_seq(3, "save"));
  CHECK_EQ(s_panel, 0);
  CHECK_EQ(s_saves, 1);
}

TEST(climbing_back_up_restores_the_panel_before_the_contrast) {
  // The mirror order matters to the eye: the panel has to come back at the
  // brightness the user chose, not at 40 and then brighten.
  fresh(&kThroughJob);
  (void)step(PWR_SLEEP_MS, false);
  s_n = 0;                                       // only watch the way up
  CHECK_EQ(step(0, false), (int)PWR_ACTIVE);
  CHECK_EQ(s_n, 2);
  CHECK(fx_seq(0, "pan+"));
  CHECK(fx_seq(1, "dim-"));
  CHECK_EQ(s_panel, 1);
  CHECK_EQ(s_dim, 0);
  CHECK_EQ(s_saves, 1);                          // the way up saves nothing more
}

TEST(a_second_descent_releases_the_radio_again_because_a_screen_may_have_taken_it) {
  fresh(&kThroughJob);
  (void)step(PWR_IDLE_MS, false);
  (void)step(0, false);
  s_n = 0;
  (void)step(PWR_IDLE_MS, false);
  CHECK(fx_seq(0, "dim+"));
  CHECK(fx_seq(1, "radio"));
  CHECK_EQ(s_saves, 2);
}

// =============================================================================
//  3. THE CARRIED RADIO DEBT (plan P6-C3)
// =============================================================================
TEST(a_running_scan_job_ends_cancelled_and_released_when_idle_drops_the_radio) {
  fresh(&kThroughJob);

  // A scan is in flight, exactly as the NETWORK screen leaves it.
  CHECK(wifi_scan_start(s_job, kDrv, 1000u));
  CHECK_EQ(s_job.state, (int)WSCAN_RUNNING);
  CHECK(wifi_scan_is_busy(s_job));
  CHECK_EQ(s_drv_started, 1);
  CHECK_EQ(s_drv_stopped, 0);

  // The ladder reaches IDLE. Its release hook navigates; the leaving screen's
  // leave() hook is what calls wifi_scan_cancel().
  CHECK_EQ(step(PWR_IDLE_MS, false), (int)PWR_IDLE);

  CHECK_EQ(s_drv_stopped, 1);                    // the driver was stopped ONCE
  CHECK(!wifi_scan_is_busy(s_job));              // and the job knows it
  CHECK_EQ(s_job.state, (int)WSCAN_IDLE);        // reset after the cancel
  CHECK_EQ(s_job.count, 0);

  // Idempotent: a second pass through the same path must not stop it twice.
  (void)step(0, false);
  (void)step(PWR_IDLE_MS, false);
  CHECK_EQ(s_drv_stopped, 1);
}

TEST(a_bare_radio_shutdown_would_leave_the_job_running_behind_its_own_back) {
  // THE COUNTER-CASE, and it is here as the shape of the defect rather than as
  // a wish. `kBare` is the shortcut hardware/power.h forbids - the moral
  // equivalent of net_request(RADIO_OFF) from the power path. Everything below
  // is what the NETWORK screen would then see: a job that still believes it
  // holds a radio that is gone, so wifi_scan_is_busy() goes on answering true
  // to the very ladder that is trying to sleep, the driver's stop() never runs
  // (and with it neither does wifi_down()'s WiFi.scanDelete()), and the next
  // poll reports a failure the player did not cause.
  fresh(&kBare);
  CHECK(wifi_scan_start(s_job, kDrv, 1000u));
  CHECK_EQ(step(PWR_IDLE_MS, false), (int)PWR_IDLE);

  CHECK_EQ(s_bare_off, 1);                       // the radio was "turned off"
  CHECK_EQ(s_drv_stopped, 0);                    // and nobody told the job
  CHECK(wifi_scan_is_busy(s_job));
  CHECK_EQ(s_job.state, (int)WSCAN_RUNNING);
  CHECK_EQ(s_job.stopped, 0);

  // Left alone it does not recover: it times out and reports an error.
  wifi_scan_service(s_job, kDrv, 1000u + WIFI_SCAN_TIMEOUT_MS);
  CHECK_EQ(s_job.state, (int)WSCAN_TIMEOUT);

  // Clean up so the shared job does not leak into the next case.
  wifi_scan_cancel(s_job, kDrv);
  wifi_scan_reset(s_job);
}

TEST(a_busy_job_holds_the_ladder_off_idle_so_the_scan_is_never_cut_mid_flight) {
  // The other half of the plan's rule. Without it a twelve-second scan started
  // just under two minutes of idle would be cancelled by the ladder rather than
  // finished by the screen.
  fresh(&kThroughJob);
  CHECK(wifi_scan_start(s_job, kDrv, 1000u));

  CHECK_EQ(step(PWR_IDLE_MS, true), (int)PWR_DIM);
  CHECK_EQ(s_drv_stopped, 0);
  CHECK(wifi_scan_is_busy(s_job));
  CHECK_EQ(s_panel, 1);                          // the panel is still on
  CHECK_EQ(s_saves, 0);

  CHECK_EQ(step(PWR_SLEEP_MS, true), (int)PWR_DIM);
  CHECK_EQ(s_drv_stopped, 0);

  // The scan answers; the screen reads it; the hold goes away; NOW the ladder
  // may drop the radio.
  s_drv_poll = 0;                                // a scan that found nothing
  wifi_scan_service(s_job, kDrv, 2000u);
  CHECK_EQ(s_job.state, (int)WSCAN_DONE);
  CHECK(!wifi_scan_is_busy(s_job));
  CHECK_EQ(s_drv_stopped, 1);                    // the job released it itself

  CHECK_EQ(step(PWR_IDLE_MS, false), (int)PWR_IDLE);
  CHECK_EQ(s_drv_stopped, 1);                    // and the cancel did not double it
}

// =============================================================================
//  4. THE FRAME RATE AND THE SLICE
// =============================================================================
TEST(the_frame_rate_is_clamped_from_idle_down_and_is_never_raised) {
  fresh(&kThroughJob);
  CHECK_EQ(pwr_fps(20), 20);
  CHECK_EQ(pwr_fps(4), 4);
  CHECK_EQ(pwr_fps(1), 1);

  (void)step(PWR_DIM_MS, false);
  CHECK_EQ(pwr_fps(20), 20);                     // DIM does not touch the rate

  (void)step(PWR_IDLE_MS, false);
  CHECK_EQ(pwr_fps(20), (int)PWR_IDLE_FPS);
  CHECK_EQ(pwr_fps(4),  (int)PWR_IDLE_FPS);
  CHECK_EQ(pwr_fps(1),  1);                      // a clamp, never a floor

  (void)step(PWR_SLEEP_MS, false);
  CHECK_EQ(pwr_fps(20), (int)PWR_IDLE_FPS);

  (void)step(0, false);
  CHECK_EQ(pwr_fps(20), 20);
}

TEST(active_and_dim_never_stop_the_cpu_and_the_two_lower_rungs_have_their_slices) {
  fresh(&kThroughJob);
  CHECK_EQ(pwr_slice_ms(), 0);
  CHECK_EQ(pwr_yield(1000), 0);
  CHECK_EQ(pwr_slept_ms(), 0);                   // asked for nothing, slept nothing

  (void)step(PWR_DIM_MS, false);
  CHECK_EQ(pwr_slice_ms(), 0);
  CHECK_EQ(pwr_yield(1000), 0);
  CHECK_EQ(pwr_slept_ms(), 0);

  (void)step(PWR_IDLE_MS, false);
  CHECK_EQ(pwr_slice_ms(), (long long)PWR_IDLE_SLICE_MS);
  (void)pwr_yield(PWR_IDLE_SLICE_MS);
  CHECK_EQ(pwr_slept_ms(), (long long)PWR_IDLE_SLICE_MS);

  // The slice is a CEILING: a bigger budget is cut down to it, which is what
  // keeps an IDLE device ticking at 1 Hz.
  (void)pwr_yield(60000u);
  CHECK_EQ(pwr_slept_ms(), (long long)(2u * PWR_IDLE_SLICE_MS));

  (void)step(PWR_SLEEP_MS, false);
  CHECK_EQ(pwr_slice_ms(), (long long)PWR_SLEEP_SLICE_MS);
  (void)pwr_yield(60000u);
  CHECK_EQ(pwr_slept_ms(),
           (long long)(2u * PWR_IDLE_SLICE_MS + PWR_SLEEP_SLICE_MS));

  // A zero budget sleeps for zero however deep the rung is.
  CHECK_EQ(pwr_yield(0), 0);
  CHECK_EQ(pwr_slept_ms(),
           (long long)(2u * PWR_IDLE_SLICE_MS + PWR_SLEEP_SLICE_MS));
}

TEST(a_sleep_slice_fits_inside_the_tick_schedulers_catch_up_bound) {
  // The two numbers that must not drift: app.cpp charges up to
  // NT_TICK_MAX_OWED_S whole seconds in one logic_tick() and resynchronises
  // past anything longer, so a slice wider than that bound would be seconds of
  // real time the pet never receives. app.cpp static_asserts it too; this is
  // the half that fails with a name rather than with a compile error.
  CHECK(PWR_SLEEP_SLICE_MS >= PWR_IDLE_SLICE_MS);
  CHECK(PWR_SLEEP_SLICE_MS / 1000u < (uint32_t)NT_TICK_MAX_OWED_S);
  CHECK(PWR_IDLE_SLICE_MS / 1000u < (uint32_t)NT_TICK_MAX_OWED_S);
}

// =============================================================================
//  5. DIAG
// =============================================================================
TEST(the_loop_counter_bills_each_pass_to_the_rung_it_ran_in) {
  fresh(&kThroughJob);
  uint32_t t = 0;

  // 100 passes inside one second, all ACTIVE.
  for (int i = 0; i < 100; ++i) pwr_note_loop(t);
  CHECK_EQ(pwr_loops_per_s((uint8_t)PWR_ACTIVE), 100);
  CHECK_EQ(pwr_loops_per_s((uint8_t)PWR_IDLE), 0);

  // Close that window, drop to IDLE, and loop twice a second there.
  t += 1000u; pwr_note_loop(t);
  (void)step(PWR_IDLE_MS, false);
  for (int i = 0; i < 2; ++i) pwr_note_loop(t);
  t += 1000u; pwr_note_loop(t);                  // closes the IDLE window
  CHECK_EQ(pwr_loops_per_s((uint8_t)PWR_IDLE), 2);
  CHECK_EQ(pwr_loops_per_s((uint8_t)PWR_ACTIVE), 1);   // the one pass that closed it

  CHECK_EQ(pwr_loops_per_s((uint8_t)PWR_STATE_COUNT), 0);   // out of range
  CHECK_EQ(pwr_loops_per_s(200), 0);
}

// =============================================================================
//  6. THE PIN FACT THE WHOLE DESIGN RESTS ON
// =============================================================================
TEST(only_the_right_button_could_ever_wake_this_board_from_deep_sleep) {
  // hardware/power.h's argument, as an assertion rather than a paragraph. The
  // mask itself is pinned to the core's own
  // SOC_GPIO_DEEP_SLEEP_WAKE_VALID_GPIO_MASK by a static_assert in power.cpp,
  // which only the firmware build can make; this half checks what the pin map
  // then means.
  CHECK(PWR_PIN_CAN_DEEP_WAKE(0));
  CHECK(PWR_PIN_CAN_DEEP_WAKE(5));
  CHECK(!PWR_PIN_CAN_DEEP_WAKE(6));
  CHECK(!PWR_PIN_CAN_DEEP_WAKE(21));

  CHECK(PWR_PIN_CAN_DEEP_WAKE(PIN_BTN_R));       // GPIO2, inside the RTC domain
  CHECK(!PWR_PIN_CAN_DEEP_WAKE(PIN_BTN_L));      // GPIO10, outside it

  // Therefore: no two-button deep sleep on this map, and PWR_SLEEP light
  // sleeps. The static_assert in power.h is what fails the build the day this
  // stops being true.
  CHECK(!PWR_DEEP_WAKE_BOTH_BUTTONS);
}

// =============================================================================
//  THE TICK BUDGET, AND THE STALL IT COUNTS (P7-C6)
//
//  app_loop() charges the pet WHOLE SECONDS of real time and clamps a gap wider
//  than NT_TICK_MAX_OWED_S back to one: a stall is not elapsed game time. That
//  clamp was silent for four phases - a device stalling for a minute an hour
//  looked exactly like a healthy one. The arithmetic lives here now so a host
//  binary can drive it, and BOTH HALVES ARE ASSERTED: that the counter moved,
//  and that exactly one second was charged. A counter with no second half is a
//  guard nobody can fail.
// =============================================================================
TEST(a_gap_wider_than_the_bound_charges_one_second_and_says_it_lost_the_rest) {
  fresh(&kThroughJob);
  CHECK_EQ((int)pwr_tick_stalls(), 0);
  CHECK_EQ(pwr_tick_lost_s(), 0u);

  // --- an ordinary second: charged, counted as no stall
  uint32_t anchor = 0u;
  CHECK_EQ(pwr_tick_budget(1000u, anchor), 1u);
  CHECK_EQ(anchor, 1000u);
  CHECK_EQ((int)pwr_tick_stalls(), 0);

  // --- a partial second: nothing due, and the anchor does not move
  CHECK_EQ(pwr_tick_budget(1999u, anchor), 0u);
  CHECK_EQ(anchor, 1000u);

  // --- THE WHOLE SLEEP SLICE IS REAL TIME AND IS CHARGED IN FULL. This is the
  //     case the bound was raised for at P6-C3, and it must not be a stall.
  CHECK((uint32_t)PWR_SLEEP_SLICE_MS < (uint32_t)NT_TICK_MAX_OWED_S * 1000UL);
  anchor = 0u;
  CHECK_EQ(pwr_tick_budget((uint32_t)PWR_SLEEP_SLICE_MS, anchor),
           (uint32_t)PWR_SLEEP_SLICE_MS / 1000UL);
  CHECK_EQ((int)pwr_tick_stalls(), 0);
  CHECK_EQ(pwr_tick_lost_s(), 0u);

  // --- the last second that is still a sleep, not a stall
  anchor = 0u;
  CHECK_EQ(pwr_tick_budget((uint32_t)NT_TICK_MAX_OWED_S * 1000UL, anchor),
           (uint32_t)NT_TICK_MAX_OWED_S);
  CHECK_EQ((int)pwr_tick_stalls(), 0);

  // --- ONE SECOND MORE IS A STALL: one charged, the rest counted and thrown.
  anchor = 0u;
  const uint32_t gap_s = (uint32_t)NT_TICK_MAX_OWED_S + 1u;
  CHECK_EQ(pwr_tick_budget(gap_s * 1000UL, anchor), 1u);      // exactly one
  CHECK_EQ(anchor, gap_s * 1000UL);                          // resynchronised
  CHECK_EQ((int)pwr_tick_stalls(), 1);
  CHECK_EQ(pwr_tick_lost_s(), gap_s - 1u);

  // --- a 1,800 s gap the loop never observed: one second charged, 1,799 named
  anchor = 0u;
  CHECK_EQ(pwr_tick_budget(1800u * 1000UL, anchor), 1u);
  CHECK_EQ((int)pwr_tick_stalls(), 2);
  CHECK_EQ(pwr_tick_lost_s(), (gap_s - 1u) + 1799u);

  // --- AND IT SATURATES RATHER THAN WRAPS, because a counter that wraps reads
  //     as healthy exactly when it has the most to say.
  for (uint32_t k = 0; k < 70000u; ++k) {
    anchor = 0u;
    (void)pwr_tick_budget(20u * 1000UL, anchor);
  }
  CHECK_EQ((int)pwr_tick_stalls(), 0xFFFF);
  CHECK(pwr_tick_lost_s() > 0u);

  // --- pwr_begin() clears both, like every other counter on the ENERGIA page
  fresh(&kThroughJob);
  CHECK_EQ((int)pwr_tick_stalls(), 0);
  CHECK_EQ(pwr_tick_lost_s(), 0u);
}

TEST(the_tick_budget_survives_the_millis_wrap) {
  fresh(&kThroughJob);
  // 500 ms before the wrap, asked again 1,500 ms later: one whole second is due
  // and the arithmetic is unsigned, so the wrap is not an event.
  uint32_t anchor = 0xFFFFFFFFul - 500u;
  CHECK_EQ(pwr_tick_budget(anchor + 1500u, anchor), 1u);
  CHECK_EQ((int)pwr_tick_stalls(), 0);
}
