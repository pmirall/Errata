// =============================================================================
//  ERRATA - networking/rxring.h
//  THE SINGLE-PRODUCER / SINGLE-CONSUMER RING THE RADIO CALLBACK POSTS INTO
//  (plan P7-C1).
//
//  WHY IT IS ITS OWN MODULE, AND PURE. networking/transport.h already decided
//  that "an ESP-NOW receive callback runs on the Wi-Fi task; letting it call
//  session_poll() would put the battle engine on another stack. The driver
//  enqueues into a fixed ring and recv() drains it." The ring is therefore the
//  one piece of the driver that carries a real invariant, and it is the one
//  piece a host binary can drive - so it lives here, CALLER-OWNED and with no
//  file-scope state, and networking/transport_espnow.cpp holds the instances.
//  The instances have to be file-scope there because esp_now_recv_cb_t has no
//  user-context argument at all (there is no `void*` in the signature): a
//  device has one radio and a singleton is the truth. The MECHANISM does not
//  have to be a singleton with it, and this is where that split is made.
//
//  THE STORAGE IS THE CALLER'S. Both the byte array and the length array are
//  passed in, which is what lets one host process run two rings against each
//  other - the same property that makes transport_loopback.cpp testable - and
//  what lets ONE implementation serve two rings of different shapes: the
//  session ring carries PROTO_FRAME_MAX frames and the beacon ring carries
//  32 B records, and neither needs its own copy of this code.
//
// -----------------------------------------------------------------------------
//  THE CURSOR CONVENTION, PICKED DELIBERATELY BECAUSE THE TREE HAS TWO
// -----------------------------------------------------------------------------
//  hardware/input.cpp's timer ring advances TAIL from the producer and HEAD
//  from the consumer; networking/ble_social.cpp's BTC ring did the opposite.
//  A third ring inheriting a mixed convention is how one of them ends up read
//  the wrong way round, so this file states its choice once - and states it in
//  its own terms rather than by reference, which is why P8-C0 deleting
//  ble_social.cpp costs this banner nothing. Every citation of that file below
//  is `git show ed9b099:Pebblebol/src/networking/ble_social.cpp`.
//
//      tail  is advanced by the PRODUCER and by nothing else (the Wi-Fi task)
//      head  is advanced by the CONSUMER and by nothing else (loop())
//      empty is head == tail;  full is ((tail + 1) & mask) == head
//
//  i.e. input.cpp's. rxring_drain() empties the ring by moving HEAD up to
//  tail - the consumer moving its OWN cursor - for exactly the reason
//  ble_social.cpp:345-350 recorded: a reset that stores 0 into both races a
//  producer that is mid-publish and can leave the ring holding stale records.
//
//  AND IT USES ACQUIRE/RELEASE, NOT `volatile`. input.cpp gets away with plain
//  volatile because its producer is an esp_timer callback that does not preempt
//  loop() in the way that matters. THE WI-FI TASK IS A REAL FreeRTOS TASK and
//  can preempt loop() in the middle of a pop, so the payload copy must not be
//  reordered past the index publish. ble_social.cpp:236-247 was the precedent
//  that got this right and this file copied it.
//
//  WHAT NO HOST TEST HERE PROVES, SAID PLAINLY: a host binary is
//  single-threaded, so nothing below is evidence about real preemption or
//  about the Wi-Fi task. The SPSC claim rests on the discipline above and on
//  the two precedents, and it is falsifiable only on the bench. What the tests
//  do prove is every rule that is a property of the data structure: refusal on
//  a full ring, refusal of an oversize record, wrap-around, and the one
//  invariant the seam depends on (below).
//
// -----------------------------------------------------------------------------
//  TWO REFUSAL RULES, AND THE SECOND ONE CLOSES A HOLE THE LOOPBACK LEFT FOR P7
// -----------------------------------------------------------------------------
//  ON A FULL RING THE NEWEST RECORD LOSES AND THE OLDEST IS KEPT. Both existing
//  rings in this tree did that (input.cpp:304-305, ble_social.cpp:238-241), so
//  the tree keeps one rule - but the reason that actually decides it is the
//  third: networking/session.h's retransmission is BY REGENERATION FROM STATE,
//  so the peer re-sends what it still owes and the newest frame costs at most
//  one 1 s rung, while the OLDEST frame may be the one that discharged an
//  obligation the peer now considers settled and will never repeat.
//
//  AN OVERSIZE RECORD IS REFUSED AT PUSH, NEVER TRUNCATED, and that is what
//  makes rxring_pop()'s 0 mean "empty" and nothing else.
//  transport_loopback.cpp:107-118 wrote the hole down for P7 to inherit: its
//  recv() returns 0 for a frame that does not fit the caller's buffer WHILE THE
//  QUEUE STILL HOLDS FRAMES, and networking/session.cpp:880-886 drains with
//  `for(;;) { n = recv; if (n == 0) break; }` - so a caller would stall behind
//  one oversized datagram. Here the ring refuses `n > stride` at push time AND
//  rxring_pop() SKIPS (never stops at) a record that will not fit the caller's
//  buffer, so 0 is returned only when head == tail. Both halves are pinned by
//  named cases in tests/test_link_transport.cpp.
//
//  PURE. stdint only. No Arduino.h, no esp_now.h, no heap, no clock.
// =============================================================================
#ifndef ER_NETWORKING_RXRING_H
#define ER_NETWORKING_RXRING_H

#include <stdint.h>
#include <stddef.h>

// -----------------------------------------------------------------------------
//  THE RING. Caller-owned, storage included.
//
//  `slots` must be a POWER OF TWO so the wrap is a mask and the producer never
//  divides; rxring_init() refuses anything else rather than wrapping wrongly.
//  `stride` is the largest record the ring will accept - the transport passes
//  PROTO_FRAME_MAX for the session ring, so the mtu rule and the ring's ceiling
//  are the same number arrived at from two directions.
// -----------------------------------------------------------------------------
struct RxRing {
  uint8_t*  slot;        // caller's storage, slots * stride bytes
  uint16_t* len;         // caller's storage, slots entries
  uint16_t  stride;      // bytes per slot; a longer record is refused
  uint8_t   slots;       // power of two, 2..128
  uint8_t   head;        // CONSUMER cursor - loop(), and nothing else
  uint8_t   tail;        // PRODUCER cursor - the Wi-Fi task, and nothing else
  uint16_t  overflow;    // records refused because the ring was full
  uint16_t  oversize;    // records refused (or skipped) for not fitting
};

// Binds the caller's storage and empties the ring. Returns false - and leaves a
// ring that refuses everything - for a null pointer, a `slots` that is not a
// power of two in 2..128, or a zero stride. A ring that cannot hold a record is
// better than a ring that wraps with a modulo nobody checked.
bool rxring_init(RxRing& r, uint8_t* storage, uint16_t* lens,
                 uint8_t slots, uint16_t stride);

// PRODUCER SIDE. Copies n bytes into the ring and publishes the index.
// Returns false and counts the reason when the ring is full (overflow) or the
// record is longer than one slot (oversize). A null pointer or n == 0 is a
// CALLER BUG rather than a wire event: it returns false and counts NOTHING, so
// neither counter can be inflated by a broken caller into looking like traffic.
bool rxring_push(RxRing& r, const uint8_t* f, uint16_t n);

// CONSUMER SIDE. Copies one record out and returns its exact byte count.
// 0 MEANS THE RING IS EMPTY AND NOTHING ELSE - a record that does not fit `cap`
// is skipped and counted, never returned as a 0 with frames still queued.
uint16_t rxring_pop(RxRing& r, uint8_t* out, uint16_t cap);

// Records waiting. For a test, and for the DIAG screen.
uint8_t rxring_pending(const RxRing& r);

// Empties the ring FROM THE CONSUMER SIDE (head := tail). Never touches tail:
// the producer owns it, and storing 0 into both is the race ble_social.cpp
// recorded (see the banner). Safe to call while a producer is running; safe to call when there is
// no producer at all.
void rxring_drain(RxRing& r);

#endif  // ER_NETWORKING_RXRING_H
