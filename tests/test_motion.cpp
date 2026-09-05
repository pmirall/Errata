// =============================================================================
//  Pebblebol host tests - test_motion.cpp
//  THE MOTION CAPABILITY (P6-C1; hardware/motion.h + hardware/motion_null.cpp).
//
//  There is no IMU on this board (audit section 6) and spec section 68 r3
//  forbids inventing a pin to reach one, so the shipped implementation answers
//  "unsupported" - and THAT is the thing worth a test. An interface whose only
//  implementation says no is one edit away from an interface that says yes by
//  accident, and spec section 4's whole point about capability detection is
//  that a caller must be able to tell absent from broken.
//
//  Three cases, and each one fails to a different mutation: flipping
//  motion_supported() to true, filling the sample with a plausible stillness
//  instead of refusing, and moving a field in the struct a future driver will
//  have to fill.
// =============================================================================
#include "nt_test.h"

#include <stddef.h>
#include <string.h>

#include "hardware/motion.h"

// -----------------------------------------------------------------------------
//  1. The capability
// -----------------------------------------------------------------------------
TEST(motion_reports_unsupported_because_this_board_has_no_imu) {
  CHECK(!motion_supported());
}

// -----------------------------------------------------------------------------
//  2. A refusal writes NOTHING
//
//  Zeroing the sample would be worse than leaving it alone: {0, 0, 0} milli-g
//  is a legal reading that means free fall, and an activity score (spec section
//  25, P6-C2) that averaged it in would be counting a measurement nobody made.
//  So the sample is filled with a pattern first and every byte of it has to
//  survive the call - including the padding, which is why this is a memcmp over
//  the whole object and not a field-by-field comparison.
// -----------------------------------------------------------------------------
TEST(a_refused_poll_leaves_every_byte_of_the_sample_alone) {
  MotionSample s;
  memset(&s, 0xA5, sizeof(s));
  MotionSample before;
  memcpy(&before, &s, sizeof(s));

  CHECK(!motion_poll(s));
  CHECK_EQ(memcmp(&s, &before, sizeof(s)), 0);

  // And again from a different pattern, so a call that happened to write 0xA5
  // would not slip through.
  memset(&s, 0x00, sizeof(s));
  memcpy(&before, &s, sizeof(s));
  CHECK(!motion_poll(s));
  CHECK_EQ(memcmp(&s, &before, sizeof(s)), 0);
}

// -----------------------------------------------------------------------------
//  3. The layout is settled before there is a driver
//
//  The point of declaring MotionSample now is that adding a real sensor later
//  is a driver and not also a redesign of what the game may read. Offsets, not
//  only sizeof: a size check alone cannot see two int16_t fields swapped.
// -----------------------------------------------------------------------------
TEST(the_motion_sample_layout_is_pinned_by_offset) {
  CHECK_EQ((int)sizeof(MotionSample), 12);
  CHECK_EQ((int)offsetof(MotionSample, ax),   0);
  CHECK_EQ((int)offsetof(MotionSample, ay),   2);
  CHECK_EQ((int)offsetof(MotionSample, az),   4);
  CHECK_EQ((int)offsetof(MotionSample, seq),  6);
  CHECK_EQ((int)offsetof(MotionSample, t_ms), 8);
}
