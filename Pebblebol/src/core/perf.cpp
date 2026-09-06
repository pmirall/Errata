// =============================================================================
//  PEBBLEBOL - core/perf.cpp
//  See core/perf.h. Arithmetic only; this file measures nothing by itself.
// =============================================================================
#include "perf.h"

// -----------------------------------------------------------------------------
//  STATE. ~84 bytes of .bss, present in EVERY variant including release - which
//  is the whole difference from the per-frame heap probe in dev/godmode.cpp,
//  whose accessors are hardcoded `return 0` when GOD_MODE_ENABLED is 0 and
//  which only accumulates while the console is engaged (and engaging it taints
//  the pet). A measurement the shipping artefact cannot take is not a
//  measurement of the product.
// -----------------------------------------------------------------------------
static uint16_t s_frame_max[SCR_COUNT];   // per screen, all-time, saturating
static uint8_t  s_frame_sat   = 0;        // any screen hit the uint16 ceiling
static uint32_t s_frame_worst = 0;        // all-time worst, full width
static uint8_t  s_frame_wscr  = 0;
static uint32_t s_pass_worst  = 0;
static uint8_t  s_pass_wscr   = 0;
static uint16_t s_frame_over  = 0;
static uint16_t s_pass_over   = 0;
static uint32_t s_frames      = 0;
static uint32_t s_passes      = 0;
static uint16_t s_discards    = 0;
// NOT NAMED s_screen, AND THAT IS NOT A STYLE CHOICE. tools/check.sh's oldest
// grep gate refuses `s_screen =` anywhere but app/state_machine.cpp, because
// the screen state machine owns navigation - and it fired on the first draft of
// this file. The gate is right and was not weakened: this variable is not
// navigation, it is which screen the next span is BILLED to, so it takes its
// own name.
static uint8_t  s_scr      = 0;

static inline void bump16(uint16_t& v) { if (v < 0xFFFFu) ++v; }

void perf_reset(void)
{
  for (uint8_t i = 0; i < (uint8_t)SCR_COUNT; ++i) s_frame_max[i] = 0;
  s_frame_sat = 0;
  s_frame_worst = 0; s_frame_wscr = 0;
  s_pass_worst  = 0; s_pass_wscr  = 0;
  s_frame_over = 0; s_pass_over = 0;
  s_frames = 0; s_passes = 0;
  s_discards = 0;
  // s_scr is NOT cleared: a reset asked for from the console must not make
  // the next frame arrive on a screen the device is not showing.
}

void perf_set_screen(uint8_t scr)
{
  s_scr = (scr < (uint8_t)SCR_COUNT) ? scr : (uint8_t)(SCR_COUNT - 1);
}

uint8_t perf_screen(void) { return s_scr; }

// The one place a span is computed. Unsigned throughout, so a stamp pair that
// straddles the micros() wrap gives the true elapsed time.
static bool span_of(uint32_t begin_us, uint32_t end_us, uint32_t& out)
{
  const uint32_t d = (uint32_t)(end_us - begin_us);
  if (d > (uint32_t)PERF_SANE_MAX_US) { bump16(s_discards); return false; }
  out = d;
  return true;
}

void perf_note_frame(uint32_t begin_us, uint32_t end_us)
{
  uint32_t d;
  if (!span_of(begin_us, end_us, d)) return;

  ++s_frames;
  if (d > (uint32_t)FRAME_BUDGET_US) bump16(s_frame_over);

  if (d >= 0xFFFFu) { s_frame_max[s_scr] = 0xFFFFu; s_frame_sat = 1u; }
  else if ((uint16_t)d > s_frame_max[s_scr]) s_frame_max[s_scr] = (uint16_t)d;

  if (d > s_frame_worst) { s_frame_worst = d; s_frame_wscr = s_scr; }
}

void perf_note_pass(uint32_t begin_us, uint32_t end_us)
{
  uint32_t d;
  if (!span_of(begin_us, end_us, d)) return;

  ++s_passes;
  if (d > (uint32_t)PERF_PASS_BUDGET_US) bump16(s_pass_over);
  if (d > s_pass_worst) { s_pass_worst = d; s_pass_wscr = s_scr; }
}

uint16_t perf_frame_max_us(uint8_t scr)
{
  return (scr < (uint8_t)SCR_COUNT) ? s_frame_max[scr] : 0u;
}
uint8_t  perf_frame_saturated(void)   { return s_frame_sat; }
uint32_t perf_frame_worst_us(void)    { return s_frame_worst; }
uint8_t  perf_frame_worst_screen(void){ return s_frame_wscr; }
uint32_t perf_pass_worst_us(void)     { return s_pass_worst; }
uint8_t  perf_pass_worst_screen(void) { return s_pass_wscr; }
uint16_t perf_frame_overruns(void)    { return s_frame_over; }
uint16_t perf_pass_overruns(void)     { return s_pass_over; }
uint32_t perf_frames(void)            { return s_frames; }
uint32_t perf_passes(void)            { return s_passes; }
uint16_t perf_discards(void)          { return s_discards; }

// -----------------------------------------------------------------------------
//  THE RENDER SCHEDULER'S ARITHMETIC
// -----------------------------------------------------------------------------
uint32_t perf_advance_deadline(uint32_t now_ms, uint32_t next_ms,
                               uint32_t period_ms)
{
  uint32_t n = (uint32_t)(next_ms + period_ms);
  // More than one whole period late means the scheduler was stalled, not slow:
  // resynchronise instead of firing every frame that was missed.
  if ((int32_t)(now_ms - n) > (int32_t)period_ms) n = (uint32_t)(now_ms + period_ms);
  return n;
}

uint32_t perf_overrun_backoff_ms(uint32_t frame_us)
{
  if (frame_us <= (uint32_t)FRAME_BUDGET_US) return 0u;
  uint32_t over_ms = (uint32_t)(frame_us - (uint32_t)FRAME_BUDGET_US) / 1000u;
  if (over_ms > (uint32_t)FRAME_BACKOFF_MAX_MS) over_ms = (uint32_t)FRAME_BACKOFF_MAX_MS;
  return over_ms;
}
