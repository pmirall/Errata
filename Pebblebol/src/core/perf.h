// =============================================================================
//  PEBBLEBOL - core/perf.h
//  THE PERFORMANCE INSTRUMENT (spec section 46, plan P10-C2).
//
//  WHAT THIS FILE IS FOR, AND WHAT IT REFUSES TO BE.
//
//  Spec section 46 asks for five numbers: a frame under FRAME_BUDGET_US on
//  every screen, a loop() pass under PERF_PASS_BUDGET_US, a per-frame heap
//  delta of zero on HOME / BATTLE / GAME, input-to-render latency inside two
//  frames, and a 24 h soak with a flat heap line. NONE OF THE FIVE CAN BE
//  MEASURED IN THIS REPOSITORY, and the reason is structural rather than
//  temporary:
//
//    * micros(), I2C and U8g2 do not exist on the host, and ui/render.cpp,
//      ui/ui.cpp, app/app.cpp, ui/petfx.cpp, ui/actfx.cpp and ui/ceremony.cpp
//      are compiled by ZERO host binaries (tests/Makefile has no rule for any
//      of them). A host frame number would be a number about a firmware
//      nobody flashes - which is this project's most repeated defect.
//    * the dominant term is invisible AND constant: rd_end_frame()'s own
//      comment puts sendBuffer() at ~1064 bytes x 9 bit-times ~= 24 ms at
//      400 kHz, roughly half the 50 ms budget, charged once per frame however
//      little was drawn. Counting draw calls would measure the SMALL half and
//      call it performance.
//    * the worst loop() pass is not arithmetic at all. It is
//      WebServer::handleClient() -> readBytesWithTimeout() blocking the whole
//      firmware on a peer's TCP trickle (networking/webui.cpp, and the
//      CS_BODY_MAX comment in core/config.h says so outright). WebServer is
//      never linked on the host.
//
//  So THIS MODULE MEASURES NO TIME AND ASSERTS NO TIME. It holds the
//  ARITHMETIC - span, wrap, saturation, worst-of, overrun boundary, the render
//  scheduler's deadline - which is the half that can be wrong here, and
//  tests/test_perf.cpp drives every line of it on the host. The microseconds
//  themselves arrive from ui/render.cpp and app/app.cpp on a board, and are
//  read by a person: docs/bench.md, steps A1 to A5.
//
//  PURE by the same red line ui/petfx_core.cpp and dev/diag_core.cpp carry,
//  and tools/check.sh enforces it: no Arduino.h, no render.h, no gfx.h. The
//  precedent is hardware/power.cpp's pwr_tick_budget(), extracted at P6-C3 so
//  a host binary could drive the ladder's arithmetic without a board.
//
//  ZERO heap, zero float, English identifiers.
//
//  WHERE THE NUMBERS COME OUT.
//    baseline (GOD_MODE_ENABLED 1) - the GD_SYS_PERF console page.
//    release  (GOD_MODE_ENABLED 0) - the `DIAG,perf,` serial line, beside the
//      soak's `DIAG,heap,`, every GOD_HEAP_PERIOD_MS. THE RELEASE ARTEFACT IS
//      THE ONE THAT MATTERS: dev/godmode.cpp's console cannot be opened there
//      at all (god_active() is a hardcoded false, so ui/screen_diag.cpp's
//      update hook sends SCR_DIAG straight home), so a bench step that reads a
//      console page is a bench step against the dev build. Both are wired;
//      docs/bench.md names the variant for every step.
// =============================================================================
#ifndef PB_PERF_H
#define PB_PERF_H

#include <stdint.h>

#include "config.h"
#include "nt_types.h"     // ScreenId / SCR_COUNT - the per-screen index

// -----------------------------------------------------------------------------
//  LIFECYCLE
// -----------------------------------------------------------------------------
// Forget every recorded span.
//
// IT HAS NO FIRMWARE CALLER AND THAT IS DELIBERATE, said here rather than left
// to be found - "a dead export" is the complaint this chunk exists to answer
// about rd_frame_time_us(), so the one it creates is declared. The device
// figure is meant to be ALL-TIME SINCE BOOT: the bench walk in docs/bench.md
// A1 visits nineteen screens and then reads one page, and a reset between
// screens would throw away exactly what it came for. A power cycle is the
// reset. tests/test_perf.cpp is the caller, and because nothing in the
// firmware calls it, --gc-sections drops it from the shipped image.
void perf_reset(void);

// Which screen the frames and passes recorded from here on belong to. app.cpp
// calls it once per pass, immediately before the render; out-of-range ids are
// clamped, because a bad id must not scribble past the array.
void perf_set_screen(uint8_t scr);
uint8_t perf_screen(void);

// -----------------------------------------------------------------------------
//  RECORDING
//
//  Both take the two micros() stamps rather than a pre-computed span, so the
//  WRAP is this module's problem and not the caller's: `end - begin` in
//  unsigned 32-bit is correct across the 71.6 minute micros() period, and a
//  caller that subtracted into an int32_t would report 4.29 billion once an
//  hour. tests/test_perf.cpp drives exactly that pair.
//
//  A span wider than PERF_SANE_MAX_US is longer than one wrap can explain, so
//  it is DISCARDED AND COUNTED (perf_discards()). A silently dropped sample is
//  the phase-7 defect; a counted one is a fact the bench log carries.
// -----------------------------------------------------------------------------
void perf_note_frame(uint32_t begin_us, uint32_t end_us);
void perf_note_pass(uint32_t begin_us, uint32_t end_us);

// -----------------------------------------------------------------------------
//  READING
//
//  The per-screen figure is a MAXIMUM SINCE BOOT, saturating at 65,535 us.
//  A maximum and not the last frame, because a number that flickers twenty
//  times a second cannot be read off a 128x64 panel by a person, and the
//  reader here is a person with a board. uint16 because 65,535 us is above the
//  50,000 us budget, so saturation can never hide an overrun - but it CAN hide
//  how bad a bad frame was, which is what perf_frame_saturated() reports and
//  what docs/bench.md tells the operator to treat as a breach.
// -----------------------------------------------------------------------------
uint16_t perf_frame_max_us(uint8_t scr);
uint8_t  perf_frame_saturated(void);

uint32_t perf_frame_worst_us(void);       // all-time worst frame, full width
uint8_t  perf_frame_worst_screen(void);   // and the screen that owned it
uint32_t perf_pass_worst_us(void);        // all-time worst loop() pass
uint8_t  perf_pass_worst_screen(void);

uint16_t perf_frame_overruns(void);       // frames  > FRAME_BUDGET_US
uint16_t perf_pass_overruns(void);        // passes  > PERF_PASS_BUDGET_US
uint32_t perf_frames(void);
uint32_t perf_passes(void);
uint16_t perf_discards(void);

// -----------------------------------------------------------------------------
//  THE RENDER SCHEDULER'S ARITHMETIC (extracted from ui/render.cpp at P10-C2)
//
//  Both of these shipped inside rd_begin_frame() / rd_end_frame() from 8aff64f
//  (P2-C8) to here WITH NO TEST OF ANY KIND, because render.cpp is compiled by
//  no host binary. They are the second of the two links in "input-to-render
//  latency <= 2 frames": the first is hardware/input.cpp's 5 ms sampler, which
//  tests/test_input.cpp already drives; this is the "not due yet, skip" gate
//  that costs the second frame.
//
//  A CORRECTION TO THE AUDIT WHILE THEY MOVE. PEBBLEBOL_IMPLEMENTATION_AUDIT.md
//  risk 22 calls FRAME_BUDGET_US and rd_frame_time_us() both dead. Half of that
//  has been false for eight phases: FRAME_BUDGET_US drives the live overrun
//  backoff below, which is what stops the loop running at 100 % duty cycle.
//  Only rd_frame_time_us() was dead, and P10-C2 gives it its first caller.
// -----------------------------------------------------------------------------
// The next frame deadline. Advance by exactly one period so the cadence does
// not drift, but RESYNCHRONISE after a long stall rather than firing a burst of
// catch-up frames (a 2,000-step offline catch-up can eat several seconds).
// All three arguments are millisecond counters and may wrap.
uint32_t perf_advance_deadline(uint32_t now_ms, uint32_t next_ms,
                               uint32_t period_ms);

// How far to push the next deadline out after a frame that blew the budget, so
// the renderer cannot run the loop at 100 % duty cycle. 0 when the frame fit.
// The boundary is STRICTLY GREATER, matching perf_note_frame()'s overrun count:
// a frame of exactly FRAME_BUDGET_US is not an overrun in either place, and one
// of them changing alone is what tests/test_perf.cpp's boundary case is for.
uint32_t perf_overrun_backoff_ms(uint32_t frame_us);

#endif  // PB_PERF_H
