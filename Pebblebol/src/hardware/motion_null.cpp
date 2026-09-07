// =============================================================================
//  PEBBLEBOL - hardware/motion_null.cpp
//  THE ONLY IMPLEMENTATION OF hardware/motion.h THERE IS, and it says no.
//
//  This board has no accelerometer and no IMU (audit section 6; the I2C sweep
//  in ui/render.cpp probes for the panel and for nothing else), and spec
//  section 68 r3 forbids inventing a pin to reach one. So both answers here are
//  constants, and the file is two functions long on purpose.
//
//  IT WRITES NOTHING TO `out`. Zeroing the sample would be worse than leaving
//  it alone: "0, 0, 0 milli-g" is a legal reading that means the device is in
//  free fall, and an activity score that averaged it in would be counting a
//  measurement that was never made. Refusing and touching nothing is the only
//  answer that cannot be mistaken for data. tests/test_motion.cpp fills the
//  sample with a pattern and checks every byte of it survives the call.
//
//  This is a PURE translation unit - no Arduino header, no GPIO, no I2C - so
//  tests/Makefile compiles it exactly as the firmware does.
// =============================================================================
#include "motion.h"

bool motion_supported(void) { return false; }

bool motion_poll(MotionSample& out) {
  (void)out;                 // deliberately not written; see the banner
  return false;
}
