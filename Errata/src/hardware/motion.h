// =============================================================================
//  ERRATA - hardware/motion.h
//  THE MOTION CAPABILITY (spec section 4 "capability detection where
//  appropriate", spec section 25, audit section 6, spec section 68 r3). P6-C1.
//
//  THERE IS NO IMU ON THIS BOARD AND THIS HEADER DOES NOT PRETEND OTHERWISE.
//  The audit's inventory is unambiguous - "no accelerometer/IMU wired, no
//  driver, no I2C address probed for one (only the OLED sweep)" - and spec 68
//  r3 forbids inventing a pin to reach one. So the shipped implementation is
//  hardware/motion_null.cpp, which answers false and writes nothing.
//
//  WHY THE INTERFACE EXISTS AT ALL, given that it has no hardware behind it.
//  Spec section 25 makes Bugs grow "through carrying" and lists an
//  accelerometer, an IMU, movement detection and ELAPSED TIME as equally
//  legitimate sensors for the abstract activity score, with an explicit
//  instruction not to claim a precision the hardware cannot deliver. P6-C2's
//  score is therefore built on time, interactions, network diversity and peers
//  - none of which is motion - and this header is the seam a later board
//  revision drops a real driver into without touching one line of game code.
//  The audit says exactly that, and names this file when it says it.
//
//  A CAPABILITY THAT ANSWERS FALSE IS NOT A STUB. The distinction the spec
//  draws in section 4 is between a feature that is absent and a feature that is
//  claimed and broken: a caller that asks motion_supported() first can degrade
//  honestly, and a caller that does not ask still gets a false from
//  motion_poll() and an untouched sample rather than a plausible zero it might
//  average into a score. tests/test_motion.cpp is what holds both of those.
//
//  Host-compilable: stdint only. Identifiers and comments are English.
// =============================================================================
#ifndef ER_MOTION_H
#define ER_MOTION_H

#include <stdint.h>

// -----------------------------------------------------------------------------
//  ONE READING. 8 B, integer, device axes, milli-g (1000 = one gravity), which
//  is the unit every hobby IMU part on this bus reports in after its own
//  scaling - so a future driver converts once and the game never sees a raw
//  register or a float. `t_ms` is the caller's millisecond clock at the moment
//  the sample was taken, on the same time base gametime.h uses.
//
//  It is DECLARED here and produced by nobody today. That is the point: the
//  layout is settled before a driver exists, so adding one is not also a
//  redesign of what an activity score is allowed to read.
// -----------------------------------------------------------------------------
struct MotionSample {
  int16_t  ax;      // 0
  int16_t  ay;      // 2
  int16_t  az;      // 4
  uint16_t seq;     // 6  sample counter, wraps; a gap means samples were lost
  uint32_t t_ms;    // 8
};
static_assert(sizeof(MotionSample) == 12, "MotionSample layout drifted");

// Is there a motion sensor at all? Answered by the linked implementation, not
// by a compile-time flag, so a board revision that probes an I2C address at
// boot can answer true without every caller changing shape.
bool motion_supported(void);

// One reading. Returns false and leaves `out` UNTOUCHED when there is nothing
// to read - which, with motion_null.cpp linked, is always. A caller that
// ignores the return value must therefore be handed whatever it initialised
// `out` with, and never a fabricated stillness.
bool motion_poll(MotionSample& out);

#endif  // ER_MOTION_H
