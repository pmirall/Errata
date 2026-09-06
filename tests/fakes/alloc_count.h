// =============================================================================
//  Pebblebol host tests - fakes/alloc_count.h
//  THE ALLOCATION COUNTER (P10-C2, spec section 46 "per-frame heap delta 0"
//  and "no memory leaks").
//
//  WHAT IT IS. A replacement global operator new / new[] / delete / delete[]
//  that counts. tests/test_screens.cpp arms it around one screen render and
//  requires zero; tests/test_soak.cpp arms it around a whole simulated day and
//  requires zero. Those are the two honest host halves of section 46's heap
//  thresholds: the device figure is ESP.getFreeHeap() sampled either side of a
//  frame and it has no host equivalent, but "the code allocates nothing at all"
//  is exactly the property that makes the device figure zero, and it is exact
//  rather than approximate.
//
//  WHY IT IS ITS OWN TRANSLATION UNIT rather than twenty lines in each test.
//  Two reasons, both found by trying the other way:
//    1. In the same TU as its caller, -O1 elides a new/delete pair outright
//       (N3664) and the probe below measures nothing - the counter would then
//       be a counter of nothing, passing for ever.
//    2. -Wmismatched-new-delete reasons across the inlined pair and fails the
//       build under -Werror, differently in different binaries.
//  Across a TU boundary the calls are opaque, so the probe is real and the
//  overrides are ordinary.
//
//  WHAT IT SEES: every operator new/new[] in code the binary compiles. WHAT IT
//  DOES NOT: a raw malloc() inside libstdc++, and - the larger half - the four
//  subsystems that actually allocate on the device (the Wi-Fi stack,
//  WebServer, U8g2's buffer, the ESP-NOW transport), none of which is linked
//  into any host binary. docs/bench.md steps A3 and A5 are where those are
//  watched, on a board, by a person.
// =============================================================================
#ifndef NT_ALLOC_COUNT_H
#define NT_ALLOC_COUNT_H

#include <stdint.h>

// Count from here, or stop counting. Setup is allowed to allocate; what is
// under test is the loop.
void     alloc_count_arm(bool on);

// How many allocations have been counted while armed.
uint32_t alloc_count(void);
void     alloc_count_reset(void);

// ONE deliberate allocation, so a case can prove the counter is LIVE before it
// believes a zero. A counter that reads 0 because it is switched off is
// indistinguishable from one that reads 0 because the code is clean, and this
// repository has shipped that shape before.
//
// INLINE, IN THE HEADER, AND THE POINTER ESCAPES THROUGH A GLOBAL. Both halves
// are load-bearing and both were found by trying the simple form:
//   * inline here, so the new/delete pair is analysed in the CALLER's
//     translation unit, where the replacement operators below are only
//     declarations - in alloc_count.cpp they are definitions and
//     -Wmismatched-new-delete fails the build under -Werror;
//   * the pointer escapes, so -O1 cannot elide the pair under N3664 and leave
//     the self-check measuring nothing, which is what the first version did.
extern void* g_alloc_probe_sink;
inline void alloc_count_probe(void) {
  char* p = new char[8];
  p[0] = 0;
  g_alloc_probe_sink = p;
  delete[] p;
}

#endif  // NT_ALLOC_COUNT_H
