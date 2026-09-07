// =============================================================================
//  PEBBLEBOL - networking/rxring.cpp
//  See rxring.h for the cursor convention, the two refusal rules and what a
//  host test here does and does not prove.
//
//  PURE: no Arduino, no radio header, no file-scope mutable state. Every byte
//  lives in the caller's RxRing, which is what lets one host process run two
//  rings against each other.
// =============================================================================
#include "rxring.h"

#include <string.h>

// The mask the whole file wraps with. `slots` is a power of two by
// construction (rxring_init refuses anything else), so this is exact.
static inline uint8_t ring_mask(const RxRing& r) { return (uint8_t)(r.slots - 1u); }

static inline bool ring_bound(const RxRing& r)
{
  return r.slot != nullptr && r.len != nullptr && r.slots >= 2u && r.stride != 0u;
}

bool rxring_init(RxRing& r, uint8_t* storage, uint16_t* lens,
                 uint8_t slots, uint16_t stride)
{
  memset(&r, 0, sizeof r);
  // A power of two in 2..128. `slots` is a uint8_t and the cursors are uint8_t,
  // so 128 is the largest ring whose head/tail cannot alias after a wrap.
  const bool pow2 = (slots >= 2u) && (slots <= 128u) &&
                    ((uint16_t)(slots & (uint8_t)(slots - 1u)) == 0u);
  if (storage == nullptr || lens == nullptr || !pow2 || stride == 0u) {
    return false;                 // a ring that refuses everything, on purpose
  }
  r.slot   = storage;
  r.len    = lens;
  r.stride = stride;
  r.slots  = slots;
  return true;
}

bool rxring_push(RxRing& r, const uint8_t* f, uint16_t n)
{
  if (!ring_bound(r)) return false;
  // A caller bug, not a wire event: neither counter moves. See rxring.h.
  if (f == nullptr || n == 0u) return false;

  if (n > r.stride) {
    __atomic_fetch_add(&r.oversize, (uint16_t)1u, __ATOMIC_RELAXED);
    return false;                 // REFUSED, never truncated
  }

  // The producer owns `tail` and may read it relaxed; `head` belongs to the
  // consumer and is read with ACQUIRE so the slot it has just finished copying
  // out cannot be seen as still occupied.
  const uint8_t tail = __atomic_load_n(&r.tail, __ATOMIC_RELAXED);
  const uint8_t next = (uint8_t)((tail + 1u) & ring_mask(r));
  if (next == __atomic_load_n(&r.head, __ATOMIC_ACQUIRE)) {
    __atomic_fetch_add(&r.overflow, (uint16_t)1u, __ATOMIC_RELAXED);
    return false;                 // full: the NEWEST loses, the oldest is kept
  }

  memcpy(&r.slot[(size_t)tail * (size_t)r.stride], f, (size_t)n);
  r.len[tail] = n;
  // PUBLISH LAST, with release ordering: the payload write above must not be
  // reordered past this store, or the consumer can read a slot the producer has
  // not finished filling.
  __atomic_store_n(&r.tail, next, __ATOMIC_RELEASE);
  return true;
}

uint16_t rxring_pop(RxRing& r, uint8_t* out, uint16_t cap)
{
  if (!ring_bound(r) || out == nullptr || cap == 0u) return 0u;

  // A LOOP AND NOT AN `if`, and that is the whole of rxring.h's second refusal
  // rule: a record too big for the caller's buffer is dropped and the next one
  // is tried, so a 0 from this function means head == tail and can mean nothing
  // else. transport_loopback.cpp:107-118 is the version of this function that
  // could return 0 with frames still queued, written down there for P7.
  for (;;) {
    const uint8_t head = __atomic_load_n(&r.head, __ATOMIC_RELAXED);
    if (head == __atomic_load_n(&r.tail, __ATOMIC_ACQUIRE)) {
      return 0u;                  // empty, and ONLY empty
    }
    const uint16_t n    = r.len[head];
    const uint8_t  next = (uint8_t)((head + 1u) & ring_mask(r));
    const bool     fits = (n != 0u) && (n <= cap) && (n <= r.stride);
    if (fits) {
      memcpy(out, &r.slot[(size_t)head * (size_t)r.stride], (size_t)n);
    } else {
      __atomic_fetch_add(&r.oversize, (uint16_t)1u, __ATOMIC_RELAXED);
    }
    // Free the slot AFTER the copy, with release ordering, so a producer that
    // is about to fill it cannot overwrite bytes still being read out.
    __atomic_store_n(&r.head, next, __ATOMIC_RELEASE);
    if (fits) return n;
  }
}

uint8_t rxring_pending(const RxRing& r)
{
  if (!ring_bound(r)) return 0u;
  const uint8_t head = __atomic_load_n(&r.head, __ATOMIC_ACQUIRE);
  const uint8_t tail = __atomic_load_n(&r.tail, __ATOMIC_ACQUIRE);
  return (uint8_t)((tail - head) & ring_mask(r));
}

void rxring_drain(RxRing& r)
{
  if (!ring_bound(r)) return;
  // head := tail. The consumer moves its OWN cursor and never touches tail.
  __atomic_store_n(&r.head, __atomic_load_n(&r.tail, __ATOMIC_ACQUIRE),
                   __ATOMIC_RELEASE);
}
