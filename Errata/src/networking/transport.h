// =============================================================================
//  ERRATA - networking/transport.h
//  THE TRANSPORT SEAM AND THE FAULT-INJECTING LOOPBACK (plan P4-C5).
//
//  ONE SEAM, TWO IMPLEMENTATIONS, NO #ifdef IN THE SESSION LOGIC. The session
//  and the lockstep see nothing but the struct below: a `send` that takes a
//  whole frame and a `recv` that returns a whole frame or zero. P7's
//  transport_espnow.cpp is the only file in the tree that will include
//  esp_now.h, and it is excluded from tests/Makefile BY FILE SELECTION, which
//  is why networking/session.cpp and networking/battle_link.cpp contain no
//  conditional compilation at all.
//
//  A STRUCT OF FUNCTION POINTERS FROM A FACTORY, not a virtual base: no heap,
//  no RTTI, no vtable in flash, and the choice of transport is a RUNTIME VALUE
//  the caller passes in - which is the property that removes the ifdef. `ctx`
//  is what lets ONE PROCESS RUN TWO ENDPOINTS: there is no file-scope mutable
//  state in transport_loopback.cpp and tools/check.sh gates that.
//
// -----------------------------------------------------------------------------
//  DATAGRAM, NEVER A STREAM, AND THE WHOLE CODEC RESTS ON IT
// -----------------------------------------------------------------------------
//  recv() returns the EXACT byte count of one frame. networking/protocol.h's
//  decode order uses that count as the quantity the frame did not supply, which
//  is what makes the `len` field a CHECK rather than a trusted input. Over a
//  byte stream there is no such count, `len` would have to be believed, and
//  this codec provides no reframing - so a stream transport is out of scope
//  and must not be added behind this seam without a reframing layer of its own.
//  ESP-NOW and the loopback both satisfy it.
//
//  NON-BLOCKING AND POLL-DRIVEN, WITH NO CALLBACK INTO SESSION CODE. An ESP-NOW
//  receive callback runs on the Wi-Fi task; letting it call session_poll()
//  would put the battle engine on another stack. The driver enqueues into a
//  fixed ring and recv() drains it.
//
//  `mtu` IS DATA, NOT A #define, so a host test can set it to 60 and drive the
//  oversize path without recompiling anything. send() returns bool and carries
//  NO RADIO VOCABULARY: the session's only reaction to a refused send is the
//  same as its reaction to a dropped frame - nothing, the retransmission ladder
//  covers both - and that is the property which makes an injected loopback drop
//  and a real radio failure exercise the identical path.
//
//  PURE. stdint plus networking/protocol.h (for PROTO_FRAME_MAX) and core/rng.h
//  (the loopback's reproducible fault stream). No Arduino.h, no esp_now.h, no
//  WiFi.h, no heap, no clock: the loopback's faults are drawn from a SEEDED Rng
//  and never from a timer, so a failing trial reproduces from its seed alone.
// =============================================================================
#ifndef ER_NETWORKING_TRANSPORT_H
#define ER_NETWORKING_TRANSPORT_H

#include <stdint.h>
#include <stddef.h>

#include "../core/rng.h"
#include "protocol.h"

// -----------------------------------------------------------------------------
//  THE SEAM
// -----------------------------------------------------------------------------
struct Transport {
  void*    ctx;                                              // the endpoint's own state
  bool     (*send)(void* ctx, const uint8_t* frame, uint16_t n);
  uint16_t (*recv)(void* ctx, uint8_t* buf, uint16_t cap);   // 0 == nothing waiting
  uint16_t mtu;                                              // the largest frame this link carries
};

// The two wrappers every caller uses. THE MTU RULE LIVES HERE, once, so no
// implementation can forget it and so a test can move the ceiling by writing a
// field. A frame that does not fit is REFUSED, never truncated: a truncated
// frame is a malformed frame with a valid-looking header.
inline bool transport_send(const Transport& t, const uint8_t* frame, uint16_t n)
{
  if (t.send == nullptr || frame == nullptr) return false;
  if (n == 0u || n > t.mtu) return false;
  return t.send(t.ctx, frame, n);
}

inline uint16_t transport_recv(const Transport& t, uint8_t* buf, uint16_t cap)
{
  if (t.recv == nullptr || buf == nullptr || cap == 0u) return 0u;
  return t.recv(t.ctx, buf, cap);
}

// -----------------------------------------------------------------------------
//  THE LOOPBACK - TWO IN-PROCESS ENDPOINTS WITH INJECTED FAULTS
//
//  IT IS A MODEL OF A LOSSY CHANNEL AND NOT A MEASUREMENT OF ONE, and the tests
//  that use it say so: a uniform independent per-frame drop is not what a
//  2.4 GHz room does, and ESP-NOW's own duplicate suppression, ack semantics,
//  real MTU enforcement and callback threading are unobserved until P7. A green
//  fault run here is evidence about the SESSION LOGIC, not about the radio.
// -----------------------------------------------------------------------------
#define LB_QUEUE_CAP  24u      // frames one direction may hold; a send onto a
                               // full queue is dropped and counted as overflow

struct LoopbackFault {
  uint16_t drop_permille;      // per frame, per direction
  uint16_t dup_permille;       // the frame is enqueued twice
  uint16_t reorder_permille;   // recv takes a frame from inside the window
  uint8_t  reorder_window;     // how far back a reordered delivery may reach; 0/1 = in order
  uint8_t  block_type[2];      // a ProtoType endpoint[i] may not send (0 = none).
                               // SCRIPTED, not random: it is how a test kills one
                               // named message and nothing else.
  uint8_t  block_left[2];      // how many of them to kill; 0xFF == every one.
                               // A COUNTED block is what lets a test lose ONE
                               // named frame and then watch the pair recover,
                               // which a permanent one cannot show.
  uint8_t  dead;               // the link is cut in both directions
  uint8_t  reserved[3];
};

struct LoopbackQueue {
  uint8_t  frame[LB_QUEUE_CAP][PROTO_FRAME_MAX];
  uint16_t len[LB_QUEUE_CAP];
  uint8_t  count;
};

struct LoopbackStats {
  uint32_t sent, delivered, dropped, duplicated, reordered, overflow, blocked;
};

// ONE object for the whole link, CALLER-OWNED. Both endpoints draw their faults
// from its single Rng, so a trial is a pure function of one seed and the fault
// pattern is identical every run - which is what makes a failing acceptance
// trial reproducible from the printed seed alone.
struct LoopbackLink {
  LoopbackQueue inbox[2];      // inbox[e] holds what endpoint e will receive
  Rng           rng;
  LoopbackFault fault;
  LoopbackStats stats;
};

// One endpoint's handle. Caller-owned for the same reason as everything else
// here: two endpoints in one process cannot share a file-scope anything.
struct LoopbackPort {
  LoopbackLink* link;
  uint8_t       endpoint;      // 0 or 1
};

void      loopback_init(LoopbackLink& lk, uint32_t seed, const LoopbackFault& f);
Transport transport_loopback(LoopbackPort& port, LoopbackLink& lk, uint8_t endpoint);

// Frames waiting for `endpoint`. For a test that wants to drain a link to
// silence before asserting on it.
uint8_t   loopback_pending(const LoopbackLink& lk, uint8_t endpoint);

#endif  // ER_NETWORKING_TRANSPORT_H
