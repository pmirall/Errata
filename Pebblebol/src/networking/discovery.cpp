// =============================================================================
//  PEBBLEBOL - networking/discovery.cpp
//  The beacon codec and the peer table. See discovery.h for the wire layout,
//  for what a beacon may carry, and for why the sender's hardware identifier
//  is not in this translation unit at all.
//
//  PURE: no Arduino, no radio header, no file-scope mutable state. Every device
//  call goes through the LinkRadioDriver the caller passes in, and every byte
//  lives in the caller's LinkJob.
//
//  INTEGER ONLY. The signal average is an exponential moving average held in
//  QUARTER-dBm so a 1/4 alpha never rounds a slow drift away to nothing - the
//  same arithmetic ble_social.cpp:186-196 uses, restated here rather than
//  shared, because that file is going away and this one is not.
// =============================================================================
#include "discovery.h"

#include <string.h>

#include "../core/crc16.h"

// =============================================================================
//  1. THE CODEC
// =============================================================================

// Little-endian, byte at a time, exactly as networking/protocol.cpp does it:
// no packed struct, no memcpy of a host struct onto the wire, no dependence on
// this compiler's alignment or endianness.
static inline void put_u16(uint8_t* p, uint16_t v)
{
  p[0] = (uint8_t)(v & 0xFFu);
  p[1] = (uint8_t)((v >> 8) & 0xFFu);
}
static inline uint16_t get_u16(const uint8_t* p)
{
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static inline void put_u32(uint8_t* p, uint32_t v)
{
  p[0] = (uint8_t)(v & 0xFFu);
  p[1] = (uint8_t)((v >> 8) & 0xFFu);
  p[2] = (uint8_t)((v >> 16) & 0xFFu);
  p[3] = (uint8_t)((v >> 24) & 0xFFu);
}
static inline uint32_t get_u32(const uint8_t* p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// A NAME ARRIVING FROM THE AIR IS HOSTILE UNTIL PROVEN OTHERWISE, and there are
// two ways for it to be wrong rather than one.
//
//   (a) an unprintable byte. The strings the device draws are Latin-1
//       (core/strings_es.h), so 0xA0..0xFF is legal and carries the accents
//       Spanish needs; 0x00..0x1F, 0x7F and the C1 range 0x80..0x9F are not
//       printable in any of them and a renderer handed one draws rubbish.
//   (b) A BYTE AFTER THE FIRST NUL. This is the half that is easy to leave out
//       and is exactly the leak check.sh:490-509 measured on ScanResult's
//       padding: bytes nobody displays are bytes something can be smuggled
//       through, and twelve of them is two whole hardware addresses. The
//       padding must be zero, and a beacon whose padding is not zero is
//       refused rather than trimmed.
static bool name_ok(const uint8_t* p, uint8_t cap)
{
  uint8_t i = 0;
  while (i < cap && p[i] != 0u) {
    const uint8_t c = p[i];
    if (c < 0x20u || c == 0x7Fu || (c >= 0x80u && c <= 0x9Fu)) return false;
    ++i;
  }
  for (uint8_t j = i; j < cap; ++j) {
    if (p[j] != 0u) return false;              // data hidden in the padding
  }
  return true;
}

DiscErr disc_encode(const DiscBeacon& b, uint8_t* buf, uint16_t cap, uint16_t& n_out)
{
  n_out = 0u;
  if (buf == nullptr || cap < (uint16_t)DISC_BEACON_BYTES) return DE_LEN;
  if (b.ver != (uint8_t)PROTOCOL_VERSION)      return DE_VERSION;
  if (b.device_id == 0u)                       return DE_DEVICE_ID;
  if ((b.caps & (uint16_t)~(uint16_t)DISC_CAP_MASK) != 0u) return DE_CAPS;

  uint8_t nm[NAME_MAX_LEN];
  memset(nm, 0, sizeof nm);
  for (uint8_t i = 0; i < (uint8_t)NAME_MAX_LEN && b.name[i] != '\0'; ++i) {
    nm[i] = (uint8_t)b.name[i];
  }
  // THE ENCODER APPLIES THE DECODER'S RULE. A local name with an unprintable
  // byte in it does not go on the air: the alternative is a device that emits
  // beacons its own decoder refuses, which is the exact defect
  // protocol.h:5 records for HELLO's session field.
  if (!name_ok(nm, (uint8_t)NAME_MAX_LEN)) return DE_NAME;

  buf[0] = (uint8_t)DISC_MAGIC_0;
  buf[1] = (uint8_t)DISC_MAGIC_1;
  buf[2] = (uint8_t)PROTOCOL_VERSION;
  buf[3] = (uint8_t)DISC_KIND_BEACON;
  put_u32(&buf[4], b.device_id);
  put_u16(&buf[8], b.caps);
  memcpy(&buf[10], nm, sizeof nm);
  put_u16(&buf[22], crc16_ccitt(buf, 22));
  n_out = (uint16_t)DISC_BEACON_BYTES;
  return DE_OK;
}

DiscErr disc_decode(const uint8_t* buf, uint16_t n, DiscBeacon& out)
{
  memset(&out, 0, sizeof out);
  if (buf == nullptr) return DE_LEN;
  // AN EQUALITY AND NOT A MINIMUM. The transport reports the exact datagram
  // length (networking/transport.h), so a beacon that is one byte long or one
  // byte over is not a beacon.
  if (n != (uint16_t)DISC_BEACON_BYTES) return DE_LEN;

  if (buf[0] != (uint8_t)DISC_MAGIC_0 || buf[1] != (uint8_t)DISC_MAGIC_1) return DE_MAGIC;
  if (buf[2] != (uint8_t)PROTOCOL_VERSION) return DE_VERSION;
  if (buf[3] != (uint8_t)DISC_KIND_BEACON) return DE_KIND;
  if (get_u16(&buf[22]) != crc16_ccitt(buf, 22)) return DE_CRC;

  const uint32_t id   = get_u32(&buf[4]);
  const uint16_t caps = get_u16(&buf[8]);
  if (id == 0u) return DE_DEVICE_ID;
  if ((caps & (uint16_t)~(uint16_t)DISC_CAP_MASK) != 0u) return DE_CAPS;
  if (!name_ok(&buf[10], (uint8_t)NAME_MAX_LEN)) return DE_NAME;

  out.device_id = id;
  out.caps      = caps;
  out.ver       = buf[2];
  for (uint8_t i = 0; i < (uint8_t)NAME_MAX_LEN; ++i) out.name[i] = (char)buf[10 + i];
  out.name[NAME_MAX_LEN] = '\0';               // ALWAYS terminated on this side
  return DE_OK;
}

// =============================================================================
//  2. THE PEER TABLE
// =============================================================================

static int peer_find(const LinkJob& j, uint32_t device_id)
{
  for (uint8_t i = 0; i < j.count; ++i) {
    if (j.peer[i].device_id == device_id) return (int)i;
  }
  return -1;
}

static void peer_remove(LinkJob& j, uint8_t idx)
{
  if (idx >= j.count) return;
  for (uint8_t i = idx; i + 1u < j.count; ++i) j.peer[i] = j.peer[i + 1u];   // keep order
  memset(&j.peer[j.count - 1u], 0, sizeof j.peer[0]);
  j.count--;
}

// Oldest entry wins the eviction lottery; ties go to the weakest signal. The
// CHOICE is ble_social.cpp:161-174's, and with the same limitation named:
// a QUALIFIED peer can be evicted by a stranger's third beacon. That is not a
// scoring hole - game/activity.cpp keeps a per-DAY set of credited device ids,
// so a peer evicted and rediscovered is not paid twice, and
// `a_peer_evicted_and_rediscovered_is_not_scored_twice` is the case that says
// so - it is a UI churn cost, and P7-C2's list is where it will be felt.
static uint8_t peer_victim(const LinkJob& j, uint32_t now_ms)
{
  uint8_t worst = 0u;
  uint32_t worst_age = 0u;
  for (uint8_t i = 0; i < j.count; ++i) {
    const uint32_t age = (uint32_t)(now_ms - j.peer[i].last_seen_ms);
    if (age > worst_age ||
        (age == worst_age && j.peer[i].rssi < j.peer[worst].rssi)) {
      worst_age = age;
      worst     = i;
    }
  }
  return worst;
}

static void peers_age(LinkJob& j, uint32_t now_ms)
{
  const uint32_t ttl_ms = (uint32_t)LINK_PEER_TTL_S * 1000UL;
  uint8_t i = 0u;
  while (i < j.count) {
    if ((uint32_t)(now_ms - j.peer[i].last_seen_ms) >= ttl_ms) {
      peer_remove(j, i);        // do not advance i: the next entry slid into it
    } else {
      ++i;
    }
  }
}

// One accepted beacon lands in the table. Returns true when THIS call is what
// took the peer over LINK_PEER_HITS_MIN - i.e. exactly once per peer per
// residency, which is what the activity term is allowed to be paid for.
static bool peer_touch(LinkJob& j, const DiscBeacon& b, const DiscRx& rx, uint32_t now_ms)
{
  int idx = peer_find(j, b.device_id);
  if (idx < 0) {
    if (j.count >= (uint8_t)LINK_PEER_CAP) {
      // REMOVE AND APPEND rather than overwrite in place, which is the one
      // place this table deliberately differs from ble_social.cpp's. The index
      // order IS the order P7-C2's list draws in, and overwriting the victim
      // slot would drop a newcomer into the middle of a list the player is
      // reading. peer_remove() shifts the rest down and keeps their order.
      peer_remove(j, peer_victim(j, now_ms));
    }
    idx = (int)j.count++;
    memset(&j.peer[idx], 0, sizeof j.peer[idx]);
    j.peer[idx].device_id = b.device_id;
    j.peer[idx].rssi_q    = (int16_t)((int16_t)rx.rssi * 4);      // seed the EMA
  } else {
    // alpha = 1/4, in quarter-dBm.
    j.peer[idx].rssi_q = (int16_t)(j.peer[idx].rssi_q +
                                   (((int16_t)rx.rssi * 4 - j.peer[idx].rssi_q) / 4));
  }

  DiscPeer& p = j.peer[idx];
  p.caps         = b.caps;
  p.ver          = b.ver;
  p.slot         = rx.slot;
  p.rssi         = (int8_t)(p.rssi_q / 4);
  p.last_seen_ms = now_ms;
  memcpy(p.name, b.name, sizeof p.name);
  p.name[NAME_MAX_LEN] = '\0';
  if (p.hits < 255u) p.hits++;

  if ((p.flags & (uint8_t)DP_QUALIFIED) == 0u && p.hits >= (uint8_t)LINK_PEER_HITS_MIN) {
    p.flags |= (uint8_t)(DP_QUALIFIED | DP_UNREPORTED);
    return true;
  }
  return false;
}

// =============================================================================
//  3. THE JOB
// =============================================================================

// The radio is released exactly once per started run, whatever ends the run -
// wifi_scanner.cpp:16-23's argument, unchanged: four exit paths that each
// remembered to call stop() would be four places to forget.
static void release(LinkJob& j, const LinkRadioDriver& d)
{
  if (!j.stopped) {
    j.stopped = 1u;
    if (d.stop) d.stop();
  }
}

static void finish(LinkJob& j, const LinkRadioDriver& d, LinkState st)
{
  j.state = (uint8_t)st;
  release(j, d);
}

void link_reset(LinkJob& j)
{
  memset(&j, 0, sizeof j);
  j.state = (uint8_t)LS_IDLE;
}

bool link_is_busy(const LinkJob& j) { return j.state == (uint8_t)LS_RUNNING; }

void link_hold(LinkJob& j)
{
  if (j.state != (uint8_t)LS_RUNNING) return;
  j.held = 1u;
}

bool link_is_held(const LinkJob& j) { return j.held != 0u; }

uint32_t link_elapsed_ms(const LinkJob& j, uint32_t now_ms)
{
  if (j.state == (uint8_t)LS_IDLE && j.started_ms == 0u) return 0u;
  return (uint32_t)(now_ms - j.started_ms);        // wrap-safe by construction
}

uint8_t link_peer_count(const LinkJob& j) { return j.count; }

const DiscPeer* link_peer(const LinkJob& j, uint8_t i)
{
  return (i < j.count) ? &j.peer[i] : nullptr;
}

uint8_t link_qualified_count(const LinkJob& j)
{
  uint8_t n = 0u;
  for (uint8_t i = 0; i < j.count; ++i) {
    if ((j.peer[i].flags & (uint8_t)DP_QUALIFIED) != 0u) ++n;
  }
  return n;
}

bool link_start(LinkJob& j, const LinkRadioDriver& d, uint32_t now_ms)
{
  if (j.state == (uint8_t)LS_RUNNING) {
    return false;                  // one radio, one link: a second tap is a no-op
  }
  link_reset(j);
  j.started_ms = now_ms;
  // Set so the FIRST service() beacons immediately rather than 500 ms later:
  // the player who just walked up should be visible on the other device within
  // one frame, not within one beacon period. Unsigned, so the wrap is defined.
  j.beacon_ms  = (uint32_t)(now_ms - (uint32_t)LINK_BEACON_MS);
  j.state      = (uint8_t)LS_RUNNING;
  if (!d.start || !d.start()) {
    // The radio could not be had at all. stop() is still called: the driver may
    // have got half way up before refusing, and this module does not get to
    // assume otherwise (wifi_scanner.cpp:63-69, same rule).
    finish(j, d, LS_FAILED);
    return false;
  }
  return true;
}

void link_service(LinkJob& j, const LinkRadioDriver& d, const DiscBeacon& self,
                  uint32_t now_ms, CooldownTable& cds, const ActClock& aclk)
{
  if (j.state != (uint8_t)LS_RUNNING) return;
  // HELD: the radio is still ours and the browse is not. Every line below is a
  // browse - a beacon, a drain, a TTL sweep, the section 47 ceiling - and a
  // session that has just been agreed over this same stack owns all four of
  // those decisions now (see link_hold()).
  if (j.held != 0u) return;

  // --- our own beacon ------------------------------------------------------
  if ((uint32_t)(now_ms - j.beacon_ms) >= (uint32_t)LINK_BEACON_MS) {
    uint8_t  frame[DISC_BEACON_BYTES];
    uint16_t n = 0u;
    // A beacon this device cannot encode is not put on the air and is not
    // retried faster than the cadence: the clock advances either way, or an
    // unencodable identity would spin the driver at frame rate.
    j.beacon_ms = now_ms;
    if (disc_encode(self, frame, (uint16_t)sizeof frame, n) == DE_OK && d.beacon) {
      if (d.beacon(frame, n)) {
        if (j.beacons_tx < 0xFFFFu) j.beacons_tx++;
      }
    }
  }

  // --- what came back ------------------------------------------------------
  // A BOUNDED DRAIN. The ring the driver empties is filled by the Wi-Fi task,
  // and a room full of beaconing devices must not be able to make one frame
  // arbitrarily long: anything still queued is drained on the next service.
  for (uint8_t k = 0; k < (uint8_t)LINK_DRAIN_PER_SERVICE; ++k) {
    DiscRx rx;
    memset(&rx, 0, sizeof rx);
    const int8_t r = d.poll ? d.poll(&rx) : (int8_t)LINK_POLL_FAILED;
    if (r < 0) {
      // THE RADIO IS GONE. Not a quiet room - the bring-up did not take, or
      // another consumer has the stack. End the job now rather than showing an
      // empty list for the whole of LINK_JOB_TIMEOUT_MS.
      finish(j, d, LS_FAILED);
      return;
    }
    if (r == 0) break;

    DiscBeacon b;
    if (disc_decode(rx.payload, rx.len, b) != DE_OK) {
      if (j.beacons_bad < 0xFFFFu) j.beacons_bad++;
      continue;
    }
    // NOT IN THIS ROOM. The signal floor is applied AFTER the codec so a
    // corrupt frame is counted as corrupt rather than as distant, and it is
    // applied to the RAW reading of this packet rather than to the average:
    // one strong packet is what admits a peer, and the average is what the
    // player then sees.
    if (rx.rssi < (int8_t)LINK_RSSI_MIN) {
      if (j.beacons_bad < 0xFFFFu) j.beacons_bad++;
      continue;
    }
    // NEVER LIST YOURSELF. Two devices with the same persisted id would be a
    // save restored onto two boards, and a device that met itself would pay the
    // activity term for its own beacon.
    if (b.device_id == self.device_id) continue;

    if (j.beacons_rx < 0xFFFFu) j.beacons_rx++;
    if (peer_touch(j, b, rx, now_ms)) {
      // THE ONE PLACE THE ACTIVITY SCORE HEARS ABOUT A PEER, and it is reached
      // exactly once per peer that crossed the hit threshold - never once per
      // beacon. game/activity.cpp then applies its own per-day distinctness and
      // its own ACT_CAP_PEERS ceiling on top, so this is the inner of two
      // gates and neither is load-bearing alone.
      (void)act_note_peer(cds, b.device_id, aclk);
      for (uint8_t i = 0; i < j.count; ++i) {
        if (j.peer[i].device_id == b.device_id) {
          j.peer[i].flags = (uint8_t)(j.peer[i].flags & ~(uint8_t)DP_UNREPORTED);
          break;
        }
      }
      if (j.peers_scored < 0xFFFFu) j.peers_scored++;
    }
  }

  // --- who has gone away ---------------------------------------------------
  peers_age(j, now_ms);

  // --- the ceiling ---------------------------------------------------------
  // CHECKED AFTER THE WORK AND NOT BEFORE, exactly as wifi_scanner.cpp:92-97
  // argues: a beacon that arrived on the very tick the deadline expires is a
  // peer the player can see, and throwing it away to report a timeout would be
  // a lie about what happened.
  if ((uint32_t)(now_ms - j.started_ms) >= (uint32_t)LINK_JOB_TIMEOUT_MS) {
    finish(j, d, LS_TIMEOUT);
  }
}

void link_cancel(LinkJob& j, const LinkRadioDriver& d)
{
  if (j.state == (uint8_t)LS_RUNNING) {
    finish(j, d, LS_CANCELLED);
    // THE PEERS STAY. A cancelled scan keeps nothing because its results are an
    // encounter roll's input and a stale one would be a free encounter; a
    // cancelled link's peer list is a display, it is already TTL-bounded, and
    // P7-C2's "CONEXION PERDIDA / A: Reintentar" needs the name of whoever the
    // player was talking to in order to offer the retry.
    return;
  }
  if (j.state == (uint8_t)LS_IDLE && j.started_ms == 0u) {
    return;                        // never started: there is nothing to give back
  }
  release(j, d);                   // idempotent by `stopped`; state is left alone
}
