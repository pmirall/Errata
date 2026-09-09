// =============================================================================
//  ERRATA - networking/transport_loopback.cpp
//  See transport.h for the seam and for what this model is and is not.
//
//  NO FILE-SCOPE MUTABLE STATE (tools/check.sh gates it): every byte lives in
//  the caller's LoopbackLink, which is what lets one host process run two
//  endpoints against each other. No heap, no clock, no Arduino, no radio.
//
//  THE FAULTS ARE DRAWN AT SEND TIME EXCEPT REORDERING, WHICH IS DRAWN AT RECV
//  TIME, and the split is not arbitrary: a drop and a duplicate are properties
//  of the CHANNEL, while a reordering is a property of the QUEUE - it only
//  exists when more than one frame is in flight, which is a fact only the
//  receiving side knows.
//
//  DRAW ORDER IS PART OF THE CONTRACT. One Rng serves both directions and the
//  three rolls happen in a fixed order (drop, duplicate, then reorder on the
//  receiving side), so a trial is a pure function of its seed. Changing the
//  order changes every trial's fault pattern - which is a re-recording of the
//  acceptance census, not a bug fix.
// =============================================================================
#include "transport.h"

#include <string.h>

// A permille roll that is TOTAL at both ends: 0 never fires and 1000 always
// does. A `< p` test on rng_next_below(1000) gives exactly that, and the two
// endpoints are indistinguishable to it.
static bool roll_permille(Rng& r, uint16_t p)
{
  if (p == 0u) return false;
  if (p >= 1000u) return true;
  return rng_next_below(r, 1000u) < (uint32_t)p;
}

static void queue_push(LoopbackLink& lk, uint8_t dst, const uint8_t* f, uint16_t n)
{
  LoopbackQueue& q = lk.inbox[dst];
  if (q.count >= (uint8_t)LB_QUEUE_CAP) { lk.stats.overflow++; lk.stats.dropped++; return; }
  memcpy(q.frame[q.count], f, n);
  q.len[q.count] = n;
  q.count++;
}

void loopback_init(LoopbackLink& lk, uint32_t seed, const LoopbackFault& f)
{
  memset(&lk, 0, sizeof lk);
  rng_init(lk.rng, seed);
  lk.fault = f;
}

uint8_t loopback_pending(const LoopbackLink& lk, uint8_t endpoint)
{
  if (endpoint > 1u) return 0u;
  return lk.inbox[endpoint].count;
}

static bool lb_send(void* ctx, const uint8_t* frame, uint16_t n)
{
  LoopbackPort* port = (LoopbackPort*)ctx;
  if (port == nullptr || port->link == nullptr || port->endpoint > 1u) return false;
  LoopbackLink& lk = *port->link;
  if (n == 0u || n > (uint16_t)PROTO_FRAME_MAX) return false;

  lk.stats.sent++;
  if (lk.fault.dead != 0u) { lk.stats.dropped++; return true; }

  // The SCRIPTED fault: one named message type from one endpoint, killed every
  // time. frame[1] is the type byte and networking/protocol.h pins that offset.
  const uint8_t blocked = lk.fault.block_type[port->endpoint];
  if (blocked != 0u && frame[1] == blocked && lk.fault.block_left[port->endpoint] != 0u) {
    if (lk.fault.block_left[port->endpoint] != 0xFFu) lk.fault.block_left[port->endpoint]--;
    lk.stats.blocked++; lk.stats.dropped++; return true;
  }

  if (roll_permille(lk.rng, lk.fault.drop_permille)) { lk.stats.dropped++; return true; }

  const uint8_t dst = (uint8_t)(port->endpoint ^ 1u);
  queue_push(lk, dst, frame, n);
  if (roll_permille(lk.rng, lk.fault.dup_permille)) {
    lk.stats.duplicated++;
    queue_push(lk, dst, frame, n);
  }
  return true;
}

// A SEND THAT WAS DROPPED STILL RETURNS TRUE, and the reason is the seam's:
// the session must not be able to tell a lost frame from a delivered one, or a
// host test over this file would exercise a path the radio does not have.
// ESP-NOW's send callback reports a link-layer failure the same way - too late
// to matter - and transport.h says the session's reaction to both is identical.

static uint16_t lb_recv(void* ctx, uint8_t* buf, uint16_t cap)
{
  LoopbackPort* port = (LoopbackPort*)ctx;
  if (port == nullptr || port->link == nullptr || port->endpoint > 1u) return 0u;
  LoopbackLink& lk = *port->link;
  LoopbackQueue& q = lk.inbox[port->endpoint];
  if (q.count == 0u) return 0u;

  uint8_t take = 0u;
  uint8_t window = lk.fault.reorder_window;
  if (window > q.count) window = q.count;
  if (window > 1u && roll_permille(lk.rng, lk.fault.reorder_permille)) {
    take = (uint8_t)(1u + rng_next_below(lk.rng, (uint32_t)(window - 1u)));
    lk.stats.reordered++;
  }

  const uint16_t n = q.len[take];
  if (n > cap) {
    // Cannot happen with a PROTO_FRAME_MAX buffer, which is what every caller
    // in this tree passes. Counted as a drop rather than truncated, because a
    // truncated frame is a malformed frame wearing a valid header.
    //
    // IT IS ALSO THE ONE PATH WHERE THIS FUNCTION'S 0 IS A LIE. The seam's 0
    // means "nothing waiting", and here the queue still holds frames - a caller
    // that stops polling on 0 would stall behind an oversized one. Nothing in
    // this tree can reach it, but P7's driver will not have that guarantee, so
    // it is written down rather than left to be rediscovered.
    lk.stats.dropped++;
  } else {
    memcpy(buf, q.frame[take], n);
    lk.stats.delivered++;
  }
  for (uint8_t i = take; i + 1u < q.count; ++i) {
    memcpy(q.frame[i], q.frame[i + 1u], q.len[i + 1u]);
    q.len[i] = q.len[i + 1u];
  }
  q.count--;
  return (n > cap) ? 0u : n;
}

Transport transport_loopback(LoopbackPort& port, LoopbackLink& lk, uint8_t endpoint)
{
  port.link     = &lk;
  port.endpoint = (uint8_t)(endpoint & 1u);
  Transport t;
  t.ctx  = &port;
  t.send = &lb_send;
  t.recv = &lb_recv;
  t.mtu  = (uint16_t)PROTO_FRAME_MAX;
  return t;
}
