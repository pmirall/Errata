// =============================================================================
//  PEBBLEBOL - networking/discovery.h
//  THE BEACON CODEC AND THE PEER TABLE (plan P7-C1, spec sections 42, 43, 44,
//  47).
//
//  WHAT A BEACON MAY CARRY, AND THE LIST IS CLOSED. Spec section 43 allows a
//  device id, a device name and a protocol version; this adds the capability
//  word section 42's menu needs to know which of BATTLE / TRADE / BREED to
//  offer. Section 44 adds the instinct behind it: do not expose personally
//  identifying information, do not store raw network information longer than
//  necessary, prefer ephemeral or hashed identifiers. So the record below is
//  four fields and a checksum and there is no fifth.
//
//  THE DEVICE ID IS ALREADY SPEC-CLEAN AND IS NOT DERIVED FROM ANY HARDWARE
//  IDENTIFIER. persistence/game_state.cpp:189-192 draws it once from
//  rng_u32(RNG_MISC) in a re-roll loop that excludes 0 and persists it, and it
//  is already public on the wire in ProtoHello (networking/protocol.h:340).
//  networking/net_classify.h:110-115 records the same reasoning from the other
//  side: the scan salt is folded rather than using the device id raw, precisely
//  BECAUSE the device id is public.
//
// -----------------------------------------------------------------------------
//  THE HARDWARE ADDRESS IS NOT IN THIS FILE, AND THAT IS ENFORCED
// -----------------------------------------------------------------------------
//  A radio frame carries its sender's hardware address in the 802.11 header
//  whatever this firmware does; we cannot remove it and must not pretend to.
//  What we CAN say, and do:
//
//      the sender's hardware address never enters a structure the game layer
//      can see, is never persisted, is never rendered, and is never folded
//      into anything the game stores. It lives in one private array inside
//      networking/transport_espnow.cpp, and the only thing that crosses into
//      this module is the opaque `slot` byte that indexes it.
//
//  This is net.cpp:750-780's trick with the same shape - a hardware address
//  goes into the read loop, a salted hash comes out, and ScanResult carries
//  nothing that identifies a network - and it is deliberately NOT
//  core/nt_types.h:453-461's shape, where BlePeerInfo's first member is the
//  raw six bytes and ble_social.cpp:243 copies them out of the callback into
//  the table the game reads. That is the shape section 43 rules out.
//
//  tools/check.sh fails the build if this file or its .cpp so much as NAMES
//  that identifier, in a field, a parameter or a comment - the same gate
//  wifi_scanner.h carries for a network's name. THE PROSE COST IS PAID IN
//  networking/transport_espnow.h, which is the file that legitimately handles
//  the address. AND THE GATE'S LIMIT, stated rather than glossed: it catches a
//  RENAME and it cannot see a VALUE. Six bytes smuggled through two uint32_t
//  fields would leave it green, exactly as check.sh:490-509 measured for
//  ScanResult's padding. The tests below pin every field of DiscPeer for that
//  reason, and the struct has no spare bytes to hide six in.
//
// -----------------------------------------------------------------------------
//  PURE. No Arduino.h, no radio header, no file-scope mutable state
// -----------------------------------------------------------------------------
//  Every device call goes through the LinkRadioDriver the caller passes in,
//  exactly as networking/wifi_scanner.h's scan job reaches WiFi.scanNetworks().
//  The job and the table are ONE caller-owned struct, so a host binary drives
//  discovery end to end with no radio: tests/test_discovery.cpp is that binary.
// =============================================================================
#ifndef PB_NETWORKING_DISCOVERY_H
#define PB_NETWORKING_DISCOVERY_H

#include <stdint.h>
#include <stddef.h>

#include "../core/config.h"                // LINK_*, NAME_MAX_LEN, PWR_IDLE_MS
#include "../core/version.h"               // PROTOCOL_VERSION
#include "../game/activity.h"              // act_note_peer, ActClock, CooldownTable

// -----------------------------------------------------------------------------
//  1. THE BEACON ON THE WIRE - 24 BYTES, LITTLE-ENDIAN, FIXED
//
//      0     magic 'P'          framing: ESP-NOW broadcast is a shared band and
//      1     magic 'B'          anybody's datagram can land in the callback
//      2     version           PROTOCOL_VERSION, byte 0 of every session frame
//      3     kind              DISC_KIND_BEACON; there is exactly one kind
//      4..7  device_id         u32, never 0
//      8..9  caps              u16, DISC_CAP_* only
//     10..21 name              12 bytes, NUL-padded, never NUL-terminated here
//     22..23 crc16             over bytes 0..21, crc16_ccitt (core/crc16.h)
//
//  FIXED LENGTH AND AN EXACT-EQUALITY CHECK, for the reason transport.h gives:
//  the transport reports the EXACT byte count of one datagram, so a length is a
//  CHECK and never a trusted input. There is no variable-length field and no
//  reframing anywhere in this codec.
// -----------------------------------------------------------------------------
#define DISC_BEACON_BYTES   24u
#define DISC_MAGIC_0        0x50u        // 'P'
#define DISC_MAGIC_1        0x42u        // 'B'
#define DISC_KIND_BEACON    0x01u

// What this device is willing to do if the player picks it (section 42's menu).
// A RESERVED BIT SET IS A REFUSAL, not a bit to ignore: the same rule
// networking/protocol.h applies to ProtoMsg.flags, and it is what stops a
// future capability from being silently accepted by a build that predates it.
#define DISC_CAP_BATTLE     0x0001u
#define DISC_CAP_TRADE      0x0002u
#define DISC_CAP_BREED      0x0004u
#define DISC_CAP_MASK       0x0007u

// The decoded beacon, and the whole of what one device tells another before a
// session exists. Host struct, not a wire image: the layout above is the byte
// loops in discovery.cpp and nothing here is copied anywhere as bytes.
struct DiscBeacon {
  uint32_t device_id;                    // never 0 (0 means "no device id")
  uint16_t caps;                         // DISC_CAP_* only
  uint8_t  ver;                          // PROTOCOL_VERSION
  char     name[NAME_MAX_LEN + 1];       // ALWAYS NUL-terminated on this side
};

// Why a beacon was refused. A separate enum from ProtoErr on purpose: the
// beacon is not a session frame, it predates the session, and one enum whose
// halves are unreachable from each other's decoder makes a `code < COUNT` sweep
// weaker than it looks (networking/protocol.h says the same of VReject).
enum DiscErr : uint8_t {
  DE_OK = 0,
  DE_LEN,          // not exactly DISC_BEACON_BYTES
  DE_MAGIC,        // somebody else's ESP-NOW traffic
  DE_VERSION,      // a peer speaking a different protocol version
  DE_KIND,
  DE_CRC,
  DE_DEVICE_ID,    // 0, which game/activity.cpp reserves for "no device id"
  DE_CAPS,         // a reserved capability bit was set
  DE_NAME,         // an unprintable byte, or data hidden in the padding
  DE_COUNT
};

// Encodes `b` into `buf`. `n_out` receives DISC_BEACON_BYTES on DE_OK and 0 on
// every reject. It applies the SAME id, capability and name rules as the
// decoder, so a beacon this emits is one disc_decode() accepts - asserted by
// tests/test_discovery.cpp and not assumed.
DiscErr disc_encode(const DiscBeacon& b, uint8_t* buf, uint16_t cap, uint16_t& n_out);

// Decodes exactly `n` bytes. `n` IS THE TRANSPORT'S BYTE COUNT and never a
// field of the beacon. `out` is left zeroed on every reject, so a caller that
// ignores the return value reads a beacon with device_id 0, which every gate
// below already refuses.
DiscErr disc_decode(const uint8_t* buf, uint16_t n, DiscBeacon& out);

// -----------------------------------------------------------------------------
//  2. WHAT THE DRIVER HANDS UP
//
//  ONE RECEIVED BEACON, AND NO SENDER IDENTITY BEYOND AN OPAQUE INDEX. `slot`
//  is the transport's own handle for whoever sent this; this module compares it
//  and stores it and never interprets it. link_peer()'s slot is what
//  P7-C2 hands back to the transport to open a unicast session with.
// -----------------------------------------------------------------------------
struct DiscRx {
  uint8_t  payload[DISC_BEACON_BYTES];
  uint16_t len;                          // what the radio reported, exactly
  int8_t   rssi;                         // clamped to int8_t by the driver
  uint8_t  slot;                         // opaque; < LINK_PEER_CAP
};

// -----------------------------------------------------------------------------
//  3. THE RADIO SEAM. Four calls, and the job makes no others.
//
//    start()   ask for the radio, bring the link up and begin listening. false
//              means it could not be had at all (another stack resident, the
//              feature compiled out); the job goes straight to LS_FAILED.
//    beacon()  put one encoded beacon on the air as a broadcast. A false here
//              is NOT fatal - the radio may be settling - and is counted.
//    poll()    fill `out` with one received beacon and return 1, return 0 when
//              nothing is waiting, and return LINK_POLL_FAILED when the radio
//              is no longer ours at all - a bring-up that did not take, or
//              another consumer that took the stack. WITHOUT THAT THIRD ANSWER
//              a failed bring-up would be indistinguishable from a quiet room
//              and the player would watch an empty list until the section 47
//              ceiling expired 90 s later. Poll-driven and non-blocking, for
//              transport.h's reason: nothing on the Wi-Fi task may call in here.
//    stop()    release the radio. Called EXACTLY ONCE per started job, on every
//              exit path including cancel and timeout - which is what makes
//              "the radio shuts down after use" a property of the
//              state and not of whichever screen happened to drive it.
// -----------------------------------------------------------------------------
#define LINK_POLL_FAILED   (-1)

struct LinkRadioDriver {
  bool   (*start)(void);
  bool   (*beacon)(const uint8_t* frame, uint16_t n);
  int8_t (*poll)(DiscRx* out);
  void   (*stop)(void);
};

// -----------------------------------------------------------------------------
//  4. ONE PEER
//
//  NO SPARE BYTES. Every field is read by something, which is the half of the
//  privacy rule a name gate cannot enforce: there is nowhere in here to hide
//  six bytes of a hardware identifier, and tests/test_discovery.cpp pins the
//  size and every offset so growing a hiding place is a deliberate act.
// -----------------------------------------------------------------------------
#define DP_QUALIFIED    0x01u   // has been heard LINK_PEER_HITS_MIN times
#define DP_UNREPORTED   0x02u   // qualified, and the activity score has not been
                                // told yet. Cleared by the ONE call that tells it.

struct DiscPeer {
  uint32_t device_id;
  uint32_t last_seen_ms;
  int16_t  rssi_q;                     // the EMA, in quarter-dBm
  uint16_t caps;
  char     name[NAME_MAX_LEN + 1];
  int8_t   rssi;                       // rssi_q / 4, what a screen shows
  uint8_t  hits;                       // beacons accepted, saturating at 255
  uint8_t  slot;                       // the transport's opaque handle
  uint8_t  flags;                      // DP_*
  uint8_t  ver;
  uint8_t  reserved;                   // must be 0 - and check.sh counts it
};

// -----------------------------------------------------------------------------
//  5. THE JOB, WHICH IS ALSO THE TABLE
//
//  ONE caller-owned struct, exactly like WifiScanJob: a screen declares one, a
//  test declares one, and this module holds nothing of its own.
//
//  WHY THE JOB HAS A CEILING AT ALL, AND WHY IT IS THIS NUMBER. The power
//  ladder clamps at DIM while a screen is holding a radio job
//  (hardware/power.h's `held`), because IDLE is the rung that drops the radio.
//  The scan is bounded by WIFI_SCAN_TIMEOUT_MS so that clamp always expires; a
//  discovery browse with no ceiling would hold the radio - and the beacon - on
//  a device left face-up on a table, for ever. LINK_JOB_TIMEOUT_MS is therefore
//  STRICTLY LESS than PWR_IDLE_MS, asserted below, so the job always gives the
//  radio back before the rung that wants it arrives.
// -----------------------------------------------------------------------------
enum LinkState : uint8_t {
  LS_IDLE = 0,
  LS_RUNNING,
  LS_TIMEOUT,       // LINK_JOB_TIMEOUT_MS elapsed (spec section 47)
  LS_FAILED,        // the driver could not give us the radio
  LS_CANCELLED,     // the player left, or the ladder navigated away
  LS_COUNT
};

struct LinkJob {
  uint8_t  state;          // LinkState
  uint8_t  stopped;        // the driver's stop() has been called for this run
  uint8_t  count;          // entries in peer[]
  uint8_t  held;           // link_hold(): browsing is over, the radio is not
  uint32_t started_ms;
  uint32_t beacon_ms;      // when the last beacon went out
  uint16_t beacons_tx;
  uint16_t beacons_rx;     // beacons that reached the table
  uint16_t beacons_bad;    // refused by the codec or by the signal floor
  uint16_t peers_scored;   // act_note_peer() calls this run
  DiscPeer peer[LINK_PEER_CAP];
};

// THE CEILING IS BELOW THE RUNG THAT WOULD FIGHT IT. See the note above; this
// is the arithmetic, not a comment about it.
static_assert((uint32_t)LINK_JOB_TIMEOUT_MS < (uint32_t)PWR_IDLE_MS,
              "the discovery job must give the radio back BEFORE the idle rung "
              "wants it, or `held` clamps the power ladder at DIM for as long "
              "as the screen is up (hardware/power.h)");
static_assert(LINK_PEER_CAP >= 2 && LINK_PEER_CAP <= 20,
              "LINK_PEER_CAP sizes DiscPeer[] and must stay inside ESP-NOW's "
              "20-entry peer table (esp_now.h ESP_NOW_MAX_TOTAL_PEER_NUM)");
static_assert(LINK_PEER_HITS_MIN >= 1 && LINK_PEER_HITS_MIN <= 255,
              "the hit threshold must fit DiscPeer.hits");

// Back to LS_IDLE with no peers. Does NOT touch the radio: a job that is still
// running must be cancelled, not reset.
void link_reset(LinkJob& j);

// Kicks the link and starts both clocks. Refuses (returns false, leaving the
// job as it was) when the job is already RUNNING, so a double tap cannot start
// two links over one radio. A driver that refuses leaves the job LS_FAILED with
// stop() already called.
bool link_start(LinkJob& j, const LinkRadioDriver& d, uint32_t now_ms);

// Call once per frame while the job is RUNNING. Never blocks. In one call it:
//   - sends `self` as a broadcast beacon if LINK_BEACON_MS have passed,
//   - drains up to LINK_DRAIN_PER_SERVICE received beacons into the table,
//   - ages out anything unheard for LINK_PEER_TTL_S,
//   - pays act_note_peer() for each peer that CROSSED the hit threshold on
//     this call - once per peer and never once per beacon,
//   - ends the job at LINK_JOB_TIMEOUT_MS.
//
// THE ACTIVITY CALL IS HERE AND NOT IN A SCREEN, and that is plan P7-C1's own
// wording ("discovery.cpp's peer table is where it belongs ... once per peer
// that passes the hit >= 3 / RSSI gate, not once per beacon, or the term
// becomes a count of packets"). game/activity.cpp is pure, the table is pure,
// and the rule is a property of the two together - so it is tested where both
// are in the same binary, which is what P6-C2 learned when the XP farm turned
// out to be cross-module and neither module's own test could see it.
// The POINTS are banked inside game/activity.cpp and drained by app.cpp's one
// funnel; nothing here awards anything.
void link_service(LinkJob& j, const LinkRadioDriver& d, const DiscBeacon& self,
                  uint32_t now_ms, CooldownTable& cds, const ActClock& aclk);

// -----------------------------------------------------------------------------
//  THE BROWSE IS OVER AND THE RADIO IS STILL OURS (P7-C2)
//
//  A screen that has AGREED a session with one of these peers has stopped
//  browsing: no beacon goes out, no beacon is drained, no peer ages out and the
//  section 47 ceiling on the BROWSE stops running. What it must NOT do is put
//  the radio down, because the session it just agreed rides on the same stack -
//  link_cancel() reaches net_request(RADIO_OFF), which passes wifi_down(),
//  which calls espnow_end(), and would take the link with it one frame after
//  the two players consented.
//
//  THE JOB STAYS LS_RUNNING, so link_is_busy() and therefore the power ladder's
//  `held` still know the radio is out, and link_cancel() is still the ONE
//  release path and still exactly-once.
//
//  THE CEILING IS NOT LOST, IT CHANGES OWNER, and that is the whole argument
//  for this call existing. From here the bound is the SESSION's own
//  retransmission ladder - nine rungs of PROTO_RETX_MS, and networking/
//  session.h's rule R4 says nothing a silent peer sends resets it - which is an
//  order of magnitude tighter than LINK_JOB_TIMEOUT_MS. A job HELD with no
//  session behind it is the one shape that would hold the radio for ever, and
//  the caller is what must not create it: ui/screen_link.cpp calls this only on
//  the success path of the function that opens a session, and its own leave
//  hook cancels.
//
//  Refuses (no-op) on a job that is not LS_RUNNING: holding a job that is not
//  holding the radio would make link_is_held() a lie.
void link_hold(LinkJob& j);
bool link_is_held(const LinkJob& j);

// Spec 47's cancellation, wired to B by the screen and reached by the power
// ladder THROUGH NAVIGATION (hardware/power.h: the ladder never names the
// radio). Idempotent, and safe on a job that already finished: the radio is
// released at most once either way.
void link_cancel(LinkJob& j, const LinkRadioDriver& d);

// True only in LS_RUNNING - i.e. exactly while the radio is held. This is the
// power ladder's `held` input for a screen that owns a LinkJob.
bool link_is_busy(const LinkJob& j);

// Monotonic ms since link_start(), wrap-safe. 0 for a job never started.
uint32_t link_elapsed_ms(const LinkJob& j, uint32_t now_ms);

// Peers the table holds, and one of them. INDEX ORDER IS ARRIVAL ORDER and stays
// that way across an eviction and across the TTL sweep, because it is the order
// P7-C2's section 42 list draws in: a table that dropped a newcomer into the
// middle of the list would move a row under the player's thumb.
uint8_t         link_peer_count(const LinkJob& j);
const DiscPeer* link_peer(const LinkJob& j, uint8_t i);

// How many have been heard often enough to be offered to the player. A peer
// below LINK_PEER_HITS_MIN is in the table (so its hits can accumulate) and is
// NOT a peer the section 42 menu may list: one stray packet from a passing
// stranger is not a Pebblebol in the room.
uint8_t link_qualified_count(const LinkJob& j);

#endif  // PB_NETWORKING_DISCOVERY_H
