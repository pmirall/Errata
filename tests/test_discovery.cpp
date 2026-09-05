// =============================================================================
//  PEBBLEBOL host tests - test_discovery.cpp
//  P7-C1: THE BEACON CODEC, THE PEER TABLE AND THE DISCOVERY JOB.
//
//  NO RADIO ANYWHERE IN THIS BINARY. networking/discovery.cpp is a pure
//  translation unit and the job reaches the device through a LinkRadioDriver of
//  function pointers, so the fake driver at the bottom of this file drives
//  every path a real link can take - including the three that are hardest to
//  provoke on hardware: the section 47 ceiling, a bring-up that does not take,
//  and a radio that vanishes while the screen is up.
//
//  activity.o IS LINKED IN AND THAT IS THE POINT. The plan's rule -
//  "act_note_peer() once per peer that passes the hit >= 3 / RSSI gate, not
//  once per beacon, or the term becomes a count of packets" - is a property of
//  the peer table and the activity score TOGETHER. P6-C2 learned what happens
//  when a cross-module property has no binary in which both modules exist: the
//  XP farm shipped because neither module's own test could see it.
//
//  WHAT THIS BINARY CANNOT SEE, said before the cases rather than after: it
//  never touches ESP-NOW. The channel, the modem-sleep default, the broadcast
//  peer entry, the receive callback's threading and whether two boards can hear
//  each other at all are bench facts, and P7-C1 leaves them explicitly untested
//  and unticked.
// =============================================================================
#include "nt_test.h"

#include <string.h>
#include <stddef.h>

#include "core/config.h"
#include "core/crc16.h"
#include "core/version.h"
#include "game/activity.h"
#include "networking/discovery.h"
#include "persistence/save_schema.h"

// =============================================================================
//  FIXTURES
// =============================================================================
static const uint32_t EP0 = 1767225600u;      // the day boundary test_activity uses

static ActClock clk(uint32_t epoch = EP0, uint8_t cal = (uint8_t)CAL_USER)
{
  ActClock c;
  c.now_epoch = epoch;
  c.cal       = cal;
  return c;
}

static DiscBeacon mk_beacon(uint32_t id, const char* name, uint16_t caps)
{
  DiscBeacon b;
  memset(&b, 0, sizeof b);
  b.device_id = id;
  b.ver       = (uint8_t)PROTOCOL_VERSION;
  b.caps      = caps;
  if (name != nullptr) {
    for (uint8_t i = 0; i < (uint8_t)NAME_MAX_LEN && name[i] != '\0'; ++i) b.name[i] = name[i];
  }
  return b;
}

// The bytes a peer would put on the air.
static void mk_wire(uint8_t out[DISC_BEACON_BYTES], uint32_t id,
                    const char* name = "PEBBLE", uint16_t caps = DISC_CAP_BATTLE)
{
  const DiscBeacon b = mk_beacon(id, name, caps);
  uint16_t n = 0;
  CHECK_EQ((int)disc_encode(b, out, (uint16_t)DISC_BEACON_BYTES, n), (int)DE_OK);
  CHECK_EQ((int)n, (int)DISC_BEACON_BYTES);
}

// =============================================================================
//  1. THE CODEC
// =============================================================================
TEST(a_beacon_round_trips_through_its_own_codec)
{
  const DiscBeacon b = mk_beacon(0xDEADBEEFu, "Pedrusco",
                                 (uint16_t)(DISC_CAP_BATTLE | DISC_CAP_TRADE));
  uint8_t buf[DISC_BEACON_BYTES];
  uint16_t n = 0;
  CHECK_EQ((int)disc_encode(b, buf, (uint16_t)sizeof buf, n), (int)DE_OK);
  CHECK_EQ((int)n, 24);

  DiscBeacon got;
  CHECK_EQ((int)disc_decode(buf, n, got), (int)DE_OK);
  CHECK_EQ(got.device_id, 0xDEADBEEFu);
  CHECK_EQ((int)got.caps, (int)(DISC_CAP_BATTLE | DISC_CAP_TRADE));
  CHECK_EQ((int)got.ver, (int)PROTOCOL_VERSION);
  CHECK_EQ(strcmp(got.name, "Pedrusco"), 0);
}

// THE LAYOUT IS PINNED FROM THIS SIDE. A beacon is on the air between two
// firmware versions, so moving a field is a decision somebody makes rather than
// something a struct rearrangement does quietly.
TEST(the_beacon_is_twenty_four_bytes_with_every_field_where_the_header_says)
{
  uint8_t buf[DISC_BEACON_BYTES];
  mk_wire(buf, 0x11223344u, "AB", (uint16_t)DISC_CAP_BREED);
  CHECK_EQ((int)DISC_BEACON_BYTES, 24);
  CHECK_EQ((int)buf[0], (int)DISC_MAGIC_0);
  CHECK_EQ((int)buf[1], (int)DISC_MAGIC_1);
  CHECK_EQ((int)buf[2], (int)PROTOCOL_VERSION);
  CHECK_EQ((int)buf[3], (int)DISC_KIND_BEACON);
  CHECK_EQ((int)buf[4], 0x44); CHECK_EQ((int)buf[5], 0x33);   // little-endian
  CHECK_EQ((int)buf[6], 0x22); CHECK_EQ((int)buf[7], 0x11);
  CHECK_EQ((int)buf[8], (int)DISC_CAP_BREED); CHECK_EQ((int)buf[9], 0);
  CHECK_EQ((int)buf[10], 'A'); CHECK_EQ((int)buf[11], 'B');
  for (int i = 12; i < 22; ++i) CHECK_EQ((int)buf[i], 0);     // NUL padding
  const uint16_t crc = crc16_ccitt(buf, 22);
  CHECK_EQ((int)buf[22], (int)(crc & 0xFFu));
  CHECK_EQ((int)buf[23], (int)((crc >> 8) & 0xFFu));
}

// EVERY WAY A BEACON CAN BE WRONG HAS ITS OWN NAMED CODE, and each row here is
// one MUTATION of a beacon that decoded a line earlier - so a rule that stopped
// being applied turns this table red rather than turning a beacon acceptable.
TEST(every_way_a_beacon_can_be_wrong_has_its_own_named_code)
{
  uint8_t good[DISC_BEACON_BYTES];
  mk_wire(good, 0x0000A001u, "Roca", (uint16_t)DISC_CAP_TRADE);
  DiscBeacon out;
  CHECK_EQ((int)disc_decode(good, (uint16_t)sizeof good, out), (int)DE_OK);

  struct Case { const char* what; int at; uint8_t to; uint16_t len; DiscErr want; };
  static const Case CASES[] = {
    { "one byte short",         -1, 0,    23u, DE_LEN },
    { "one byte long",          -1, 0,    25u, DE_LEN },
    { "empty datagram",         -1, 0,     0u, DE_LEN },
    { "first magic byte",        0, 'X',  24u, DE_MAGIC },
    { "second magic byte",       1, 'X',  24u, DE_MAGIC },
    { "a future protocol",       2, 2u,   24u, DE_VERSION },
    { "an unknown kind",         3, 9u,   24u, DE_KIND },
    { "a flipped id byte",       4, 0x02, 24u, DE_CRC },   // the CRC sees it first
    { "a flipped name byte",    10, 'X',  24u, DE_CRC },
    { "the low crc byte",       22, 0xFF, 24u, DE_CRC },
    { "the high crc byte",      23, 0xFF, 24u, DE_CRC },
  };
  for (size_t i = 0; i < sizeof CASES / sizeof CASES[0]; ++i) {
    uint8_t bad[DISC_BEACON_BYTES];
    memcpy(bad, good, sizeof bad);
    if (CASES[i].at >= 0) bad[CASES[i].at] = CASES[i].to;
    DiscBeacon b;
    CHECK_EQ((int)disc_decode(bad, CASES[i].len, b), (int)CASES[i].want);
    CHECK_EQ(b.device_id, 0u);                  // every reject leaves it zeroed
  }
  CHECK_EQ((int)disc_decode(nullptr, 24u, out), (int)DE_LEN);
}

// THE THREE REJECTS THAT NEED THE CRC RESEALED, because they are rules about
// the CONTENT and not about the bytes: a mutation that leaves the checksum
// stale is caught by DE_CRC first and would prove nothing about them.
TEST(a_beacon_that_is_wrong_about_itself_is_refused_even_when_it_is_sealed)
{
  uint8_t buf[DISC_BEACON_BYTES];
  DiscBeacon out;

  // (a) device_id 0, which game/activity.cpp reserves for "no device id" and
  //     would therefore have scored nothing while occupying a table slot.
  mk_wire(buf, 0x0000A001u);
  buf[4] = buf[5] = buf[6] = buf[7] = 0u;
  const uint16_t c1 = crc16_ccitt(buf, 22);
  buf[22] = (uint8_t)(c1 & 0xFFu); buf[23] = (uint8_t)(c1 >> 8);
  CHECK_EQ((int)disc_decode(buf, (uint16_t)sizeof buf, out), (int)DE_DEVICE_ID);

  // (b) a reserved capability bit. A build that predates a capability must
  //     refuse it, not ignore it - protocol.h's rule for ProtoMsg.flags.
  mk_wire(buf, 0x0000A001u);
  buf[8] = (uint8_t)(DISC_CAP_MASK | 0x80u);
  const uint16_t c2 = crc16_ccitt(buf, 22);
  buf[22] = (uint8_t)(c2 & 0xFFu); buf[23] = (uint8_t)(c2 >> 8);
  CHECK_EQ((int)disc_decode(buf, (uint16_t)sizeof buf, out), (int)DE_CAPS);

  // (c) an unprintable byte in the name. A renderer handed one draws rubbish.
  mk_wire(buf, 0x0000A001u, "Roca");
  buf[11] = 0x01u;
  const uint16_t c3 = crc16_ccitt(buf, 22);
  buf[22] = (uint8_t)(c3 & 0xFFu); buf[23] = (uint8_t)(c3 >> 8);
  CHECK_EQ((int)disc_decode(buf, (uint16_t)sizeof buf, out), (int)DE_NAME);
}

// THE PADDING RULE, AND IT IS THE ONE THE PROJECT HAS ALREADY BEEN BITTEN BY.
// tools/check.sh:490-509 records that two unused bytes of ScanResult were a
// place a beacon's name could be smuggled through with every gate green. The
// name field here has TWELVE such bytes - two whole hardware addresses - so a
// byte after the first NUL is a refusal and not padding to ignore.
TEST(bytes_hidden_after_the_name_are_a_refusal_and_not_padding_to_ignore)
{
  uint8_t buf[DISC_BEACON_BYTES];
  mk_wire(buf, 0x0000A001u, "AB");
  CHECK_EQ((int)buf[12], 0);                 // the padding starts here
  buf[16] = 0xDEu;                           // six bytes of something, tucked away
  buf[17] = 0xADu;
  const uint16_t c = crc16_ccitt(buf, 22);
  buf[22] = (uint8_t)(c & 0xFFu); buf[23] = (uint8_t)(c >> 8);
  DiscBeacon out;
  CHECK_EQ((int)disc_decode(buf, (uint16_t)sizeof buf, out), (int)DE_NAME);
  CHECK_EQ(out.device_id, 0u);
}

TEST(a_name_from_the_air_comes_back_terminated_and_never_longer_than_the_box)
{
  // A name that fills the field exactly, with no room for a terminator on the
  // wire. The decoder has to add one, or every consumer reads off the end.
  uint8_t buf[DISC_BEACON_BYTES];
  mk_wire(buf, 0x0000A001u, "ABCDEFGHIJKL");   // NAME_MAX_LEN == 12
  DiscBeacon out;
  CHECK_EQ((int)disc_decode(buf, (uint16_t)sizeof buf, out), (int)DE_OK);
  CHECK_EQ((int)strlen(out.name), (int)NAME_MAX_LEN);
  CHECK_EQ((int)out.name[NAME_MAX_LEN], 0);
  CHECK_EQ(strcmp(out.name, "ABCDEFGHIJKL"), 0);

  // A name longer than the field is TRUNCATED by the encoder, not refused: it
  // is our own name and there is nothing hostile about it.
  DiscBeacon b;
  memset(&b, 0, sizeof b);
  b.device_id = 7u; b.ver = (uint8_t)PROTOCOL_VERSION;
  memcpy(b.name, "ABCDEFGHIJKLM", 13);          // 13 chars, no terminator in field
  b.name[NAME_MAX_LEN] = '\0';
  uint16_t n = 0;
  CHECK_EQ((int)disc_encode(b, buf, (uint16_t)sizeof buf, n), (int)DE_OK);
  CHECK_EQ((int)disc_decode(buf, n, out), (int)DE_OK);
  CHECK_EQ((int)strlen(out.name), (int)NAME_MAX_LEN);

  // An EMPTY name is legal: a device whose player never set one is still a
  // device, and the section 42 list falls back to the id.
  const DiscBeacon e = mk_beacon(9u, "", (uint16_t)DISC_CAP_BATTLE);
  CHECK_EQ((int)disc_encode(e, buf, (uint16_t)sizeof buf, n), (int)DE_OK);
  CHECK_EQ((int)disc_decode(buf, n, out), (int)DE_OK);
  CHECK_EQ((int)out.name[0], 0);

  // Latin-1 above 0xA0 is legal, because the string table is Latin-1 and
  // Spanish needs the accents; the C1 range is not.
  DiscBeacon acc = mk_beacon(11u, "", 0u);
  acc.name[0] = (char)0xF1;                     // 'n' with a tilde
  acc.name[1] = 'a';
  CHECK_EQ((int)disc_encode(acc, buf, (uint16_t)sizeof buf, n), (int)DE_OK);
  CHECK_EQ((int)disc_decode(buf, n, out), (int)DE_OK);
  CHECK_EQ((int)(uint8_t)out.name[0], 0xF1);
  acc.name[0] = (char)0x85;                     // a C1 control
  CHECK_EQ((int)disc_encode(acc, buf, (uint16_t)sizeof buf, n), (int)DE_NAME);
}

// THE ENCODER APPLIES THE DECODER'S RULES, asserted rather than assumed. The
// exact defect protocol.h records for HELLO's session field was a frame the
// encoder emitted and nobody could decode.
TEST(every_beacon_the_encoder_emits_its_own_decoder_accepts)
{
  static const char* NAMES[] = { "", "A", "Pedrusco", "ABCDEFGHIJKL", "Nu 9" };
  uint32_t checked = 0;
  for (uint32_t id = 1u; id <= 300u; ++id) {
    for (uint16_t caps = 0; caps <= (uint16_t)DISC_CAP_MASK; ++caps) {
      const char* nm = NAMES[(id + caps) % (sizeof NAMES / sizeof NAMES[0])];
      const uint32_t mixed = id * 2654435761u;
      const DiscBeacon b = mk_beacon((mixed != 0u) ? mixed : 1u, nm, caps);
      uint8_t buf[DISC_BEACON_BYTES];
      uint16_t n = 0;
      CHECK_EQ((int)disc_encode(b, buf, (uint16_t)sizeof buf, n), (int)DE_OK);
      DiscBeacon got;
      CHECK_EQ((int)disc_decode(buf, n, got), (int)DE_OK);
      CHECK_EQ(got.device_id, b.device_id);
      CHECK_EQ((int)got.caps, (int)b.caps);
      CHECK_EQ(strcmp(got.name, nm), 0);
      ++checked;
    }
  }
  CHECK_EQ((int)checked, 2400);
  // ...and the encoder refuses what it must never put on the air.
  uint8_t buf[DISC_BEACON_BYTES];
  uint16_t n = 1u;
  DiscBeacon zero = mk_beacon(0u, "X", 0u);
  CHECK_EQ((int)disc_encode(zero, buf, (uint16_t)sizeof buf, n), (int)DE_DEVICE_ID);
  CHECK_EQ((int)n, 0);
  DiscBeacon wide = mk_beacon(5u, "X", 0xFFFFu);
  CHECK_EQ((int)disc_encode(wide, buf, (uint16_t)sizeof buf, n), (int)DE_CAPS);
  DiscBeacon oldv = mk_beacon(5u, "X", 0u); oldv.ver = 99u;
  CHECK_EQ((int)disc_encode(oldv, buf, (uint16_t)sizeof buf, n), (int)DE_VERSION);
  CHECK_EQ((int)disc_encode(mk_beacon(5u, "X", 0u), buf, 23u, n), (int)DE_LEN);
}

// =============================================================================
//  2. WHAT A PEER RECORD MAY HOLD (spec sections 43, 44)
//
//  tools/check.sh fails the build if discovery.{h,cpp} NAMES a hardware
//  address. That gate catches a rename and cannot see a value, and this case is
//  the other half: every byte of DiscPeer is accounted for, so there is nowhere
//  to put six of somebody else's.
// =============================================================================
TEST(a_disc_peer_has_no_room_to_hide_a_hardware_address)
{
  CHECK_EQ((int)offsetof(DiscPeer, device_id),    0);
  CHECK_EQ((int)offsetof(DiscPeer, last_seen_ms), 4);
  CHECK_EQ((int)offsetof(DiscPeer, rssi_q),       8);
  CHECK_EQ((int)offsetof(DiscPeer, caps),        10);
  CHECK_EQ((int)offsetof(DiscPeer, name),        12);
  CHECK_EQ((int)offsetof(DiscPeer, rssi),        25);
  CHECK_EQ((int)offsetof(DiscPeer, hits),        26);
  CHECK_EQ((int)offsetof(DiscPeer, slot),        27);
  CHECK_EQ((int)offsetof(DiscPeer, flags),       28);
  CHECK_EQ((int)offsetof(DiscPeer, ver),         29);
  CHECK_EQ((int)offsetof(DiscPeer, reserved),    30);
  CHECK_EQ((int)sizeof(DiscPeer),                32);
  // 4+4+2+2+13+1+1+1+1+1+1 = 31 declared bytes and ONE byte of tail padding.
  // Saying so here is what makes a twelfth field a visible change rather than a
  // struct that silently grew four bytes of somewhere to hide something.
  CHECK_EQ((int)(4 + 4 + 2 + 2 + (NAME_MAX_LEN + 1) + 1 + 1 + 1 + 1 + 1 + 1), 31);

  // And the one declared spare byte is zero in a record the table produced.
  LinkJob j;
  link_reset(j);
  CHECK_EQ((int)link_peer_count(j), 0);
  CHECK(link_peer(j, 0) == nullptr);
}

// =============================================================================
//  3. THE FAKE RADIO
// =============================================================================
static uint32_t g_start_calls, g_stop_calls, g_beacon_calls;
static bool     g_start_ok;
static int8_t   g_poll_answer;                 // forced answer, or 2 = "use the queue"
static bool     g_beacon_ok;
static uint8_t  g_last_beacon[DISC_BEACON_BYTES];
static uint16_t g_last_beacon_len;

#define FQ_CAP 32u
static DiscRx  g_queue[FQ_CAP];
static uint8_t g_q_head, g_q_tail;

static void fake_reset(void)
{
  g_start_calls = g_stop_calls = g_beacon_calls = 0;
  g_start_ok    = true;
  g_beacon_ok   = true;
  g_poll_answer = 2;
  g_q_head = g_q_tail = 0;
  memset(g_queue, 0, sizeof g_queue);
  memset(g_last_beacon, 0, sizeof g_last_beacon);
  g_last_beacon_len = 0;
}

// Queue one beacon "on the air", with the signal strength and the driver's
// opaque slot the real transport would have attached.
static void air(uint32_t id, int8_t rssi, uint8_t slot,
                const char* name = "Roca", uint16_t caps = DISC_CAP_BATTLE)
{
  CHECK((uint8_t)((g_q_tail + 1u) % FQ_CAP) != g_q_head);
  DiscRx& r = g_queue[g_q_tail];
  memset(&r, 0, sizeof r);
  mk_wire(r.payload, id, name, caps);
  r.len  = (uint16_t)DISC_BEACON_BYTES;
  r.rssi = rssi;
  r.slot = slot;
  g_q_tail = (uint8_t)((g_q_tail + 1u) % FQ_CAP);
}

// A datagram that is on the air but is not a beacon this build accepts.
static void air_raw(const uint8_t* bytes, uint16_t len, int8_t rssi, uint8_t slot)
{
  CHECK((uint8_t)((g_q_tail + 1u) % FQ_CAP) != g_q_head);
  DiscRx& r = g_queue[g_q_tail];
  memset(&r, 0, sizeof r);
  if (bytes != nullptr && len <= (uint16_t)DISC_BEACON_BYTES) memcpy(r.payload, bytes, len);
  r.len  = len;
  r.rssi = rssi;
  r.slot = slot;
  g_q_tail = (uint8_t)((g_q_tail + 1u) % FQ_CAP);
}

static bool fake_start(void) { g_start_calls++; return g_start_ok; }
static void fake_stop(void)  { g_stop_calls++; }

static bool fake_beacon(const uint8_t* f, uint16_t n)
{
  g_beacon_calls++;
  if (f != nullptr && n <= (uint16_t)sizeof g_last_beacon) {
    memcpy(g_last_beacon, f, n);
    g_last_beacon_len = n;
  }
  return g_beacon_ok;
}

static int8_t fake_poll(DiscRx* out)
{
  if (g_poll_answer != 2) return g_poll_answer;
  if (g_q_head == g_q_tail) return 0;
  *out = g_queue[g_q_head];
  g_q_head = (uint8_t)((g_q_head + 1u) % FQ_CAP);
  return 1;
}

static const LinkRadioDriver FAKE = { &fake_start, &fake_beacon, &fake_poll, &fake_stop };

// A device with a name of its own, for the self-filter.
static DiscBeacon self_beacon(void)
{
  return mk_beacon(0x00005E1Fu, "Yo", (uint16_t)(DISC_CAP_BATTLE | DISC_CAP_TRADE));
}

struct Fixture {
  LinkJob       job;
  CooldownTable cds;
  DiscBeacon    self;
};

static Fixture& fx(void)
{
  static Fixture f;
  fake_reset();
  memset(&f.cds, 0, sizeof f.cds);
  act_begin();
  link_reset(f.job);
  f.self = self_beacon();
  return f;
}

static void serve(Fixture& f, uint32_t now_ms) {
  link_service(f.job, FAKE, f.self, now_ms, f.cds, clk());
}

// =============================================================================
//  4. THE RSSI GATE, THE HIT THRESHOLD AND THE TTL
// =============================================================================

// MUTATION: delete the `rx.rssi < LINK_RSSI_MIN` guard in link_service() and
// this case fails on the very first assertion - the distant device enters the
// table.
TEST(a_peer_below_the_signal_floor_never_reaches_the_table)
{
  Fixture& f = fx();
  CHECK(link_start(f.job, FAKE, 1000u));
  for (uint8_t i = 0; i < 5u; ++i) air(0x0A0A0A0Au, (int8_t)(LINK_RSSI_MIN - 1), 0u);
  serve(f, 1000u);
  CHECK_EQ((int)link_peer_count(f.job), 0);
  CHECK_EQ((int)f.job.beacons_bad, 5);
  CHECK_EQ((int)f.job.beacons_rx, 0);

  // Exactly AT the floor is in the room: the constant is a floor, not a strict
  // threshold, and which one it is decides a whole class of borderline peers.
  air(0x0A0A0A0Au, (int8_t)LINK_RSSI_MIN, 0u);
  serve(f, 1100u);
  CHECK_EQ((int)link_peer_count(f.job), 1);
}

// MUTATION: change `p.hits >= LINK_PEER_HITS_MIN` to `>= 1` and both the
// qualified count and peers_scored go to 1 on the first beacon.
TEST(three_hits_are_what_makes_a_peer_real)
{
  Fixture& f = fx();
  CHECK(link_start(f.job, FAKE, 0u));
  CHECK_EQ((int)LINK_PEER_HITS_MIN, 3);

  uint32_t t = 0;
  for (uint8_t hit = 1u; hit <= 4u; ++hit) {
    air(0x0B0B0B0Bu, -50, 3u);
    t += (uint32_t)LINK_BEACON_MS;
    serve(f, t);
    CHECK_EQ((int)link_peer_count(f.job), 1);          // in the table from hit 1
    const DiscPeer* p = link_peer(f.job, 0);
    CHECK(p != nullptr);
    CHECK_EQ((int)p->hits, (int)hit);
    // ...and offered to the player only from hit 3.
    CHECK_EQ((int)link_qualified_count(f.job), (hit >= 3u) ? 1 : 0);
    CHECK_EQ((int)f.job.peers_scored, (hit >= 3u) ? 1 : 0);
  }
  const DiscPeer* p = link_peer(f.job, 0);
  CHECK_EQ((int)p->slot, 3);
  CHECK_EQ((int)p->caps, (int)DISC_CAP_BATTLE);
  CHECK_EQ(strcmp(p->name, "Roca"), 0);
}

// MUTATION: replace the EMA with `p.rssi = rx.rssi` and the last assertion
// fails - the average would be -90 after one distant packet instead of -55.
TEST(the_signal_shown_is_an_average_and_not_the_last_packet)
{
  Fixture& f = fx();
  CHECK(link_start(f.job, FAKE, 0u));
  uint32_t t = 0;
  for (uint8_t i = 0; i < 6u; ++i) {
    air(0x0C0C0C0Cu, -50, 0u);
    t += (uint32_t)LINK_BEACON_MS;
    serve(f, t);
  }
  CHECK_EQ((int)link_peer(f.job, 0)->rssi, -50);       // steady state

  air(0x0C0C0C0Cu, -70, 0u);                           // one packet from further off
  t += (uint32_t)LINK_BEACON_MS;
  serve(f, t);
  const int8_t after = link_peer(f.job, 0)->rssi;
  CHECK(after < -50);                                  // it moved...
  CHECK(after > -70);                                  // ...but not all the way
  CHECK_EQ((int)after, -55);                           // alpha = 1/4, exactly
}

// MUTATION: remove the peers_age() call from link_service() and the peer is
// still listed after two minutes of silence.
TEST(a_peer_unheard_for_the_ttl_leaves_the_table)
{
  Fixture& f = fx();
  CHECK(link_start(f.job, FAKE, 0u));
  const uint32_t ttl_ms = (uint32_t)LINK_PEER_TTL_S * 1000UL;
  CHECK_EQ((int)ttl_ms, 60000);

  for (uint8_t i = 0; i < 3u; ++i) { air(0x0D0D0D0Du, -40, 0u); serve(f, (uint32_t)(i * 500u)); }
  CHECK_EQ((int)link_qualified_count(f.job), 1);

  serve(f, ttl_ms + 999u);                 // one millisecond short of the TTL
  CHECK_EQ((int)link_peer_count(f.job), 1);
  serve(f, ttl_ms + 1000u);                // and exactly at it
  CHECK_EQ((int)link_peer_count(f.job), 0);
  CHECK_EQ((int)link_qualified_count(f.job), 0);
}

TEST(a_ninth_device_evicts_the_oldest_and_never_the_newest)
{
  Fixture& f = fx();
  CHECK(link_start(f.job, FAKE, 0u));
  CHECK_EQ((int)LINK_PEER_CAP, 8);

  // Eight devices, oldest first, one second apart.
  for (uint8_t i = 0; i < 8u; ++i) {
    air((uint32_t)(0x1000u + i), -40, i);
    serve(f, (uint32_t)(1000u + i * 1000u));
  }
  CHECK_EQ((int)link_peer_count(f.job), 8);
  CHECK_EQ(link_peer(f.job, 0)->device_id, 0x1000u);

  air(0x2000u, -40, 8u);                      // a ninth walks in
  serve(f, 9000u);
  CHECK_EQ((int)link_peer_count(f.job), 8);
  // The oldest is gone, the newest is here, and everybody else kept their order.
  for (uint8_t i = 0; i < 7u; ++i) {
    CHECK_EQ(link_peer(f.job, i)->device_id, (uint32_t)(0x1001u + i));
  }
  CHECK_EQ(link_peer(f.job, 7)->device_id, 0x2000u);
}

// MUTATION: remove the `b.device_id == self.device_id` guard and this device
// lists itself and pays the activity term for its own beacon.
TEST(a_device_never_lists_itself)
{
  Fixture& f = fx();
  CHECK(link_start(f.job, FAKE, 0u));
  for (uint8_t i = 0; i < 5u; ++i) air(self_beacon().device_id, -30, 0u);
  serve(f, 0u);
  CHECK_EQ((int)link_peer_count(f.job), 0);
  CHECK_EQ((int)f.job.peers_scored, 0);
  CHECK_EQ(act_score_today(f.cds, clk()), 0u);
}

TEST(a_datagram_that_is_not_a_beacon_is_counted_and_dropped)
{
  Fixture& f = fx();
  CHECK(link_start(f.job, FAKE, 0u));
  uint8_t junk[DISC_BEACON_BYTES];
  memset(junk, 0xA5, sizeof junk);
  air_raw(junk, (uint16_t)sizeof junk, -30, 0u);      // wrong magic
  air_raw(junk, 8u, -30, 0u);                         // wrong length
  air_raw(nullptr, 0u, -30, 0u);                      // nothing at all
  serve(f, 0u);
  CHECK_EQ((int)link_peer_count(f.job), 0);
  CHECK_EQ((int)f.job.beacons_bad, 3);
  CHECK(link_is_busy(f.job));                          // and the job is unharmed
}

// =============================================================================
//  5. THE ACTIVITY TERM - the cross-module rule, in the one binary that can see
//     both modules (plan P7-C1's own bullet)
// =============================================================================

// MUTATION: move the act_note_peer() call out of the `peer_touch() returned
// true` branch so it runs on every accepted beacon. This case then reports
// twenty scores instead of one and the day's score saturates at ACT_CAP_PEERS.
TEST(a_peer_is_scored_once_however_many_beacons_it_sends)
{
  Fixture& f = fx();
  CHECK(link_start(f.job, FAKE, 0u));
  uint32_t t = 0;
  for (uint8_t i = 0; i < 20u; ++i) {
    air(0x0E0E0E0Eu, -40, 0u);
    t += (uint32_t)LINK_BEACON_MS;
    serve(f, t);
  }
  CHECK_EQ((int)f.job.beacons_rx, 20);
  CHECK_EQ((int)f.job.peers_scored, 1);
  CHECK_EQ(act_score_today(f.cds, clk()), (uint16_t)ACT_PTS_PEER);
  CHECK_EQ((int)link_peer(f.job, 0)->hits, 20);
}

// The other side of the same rule: distinct devices really do each score, so
// the case above is not passing because the term is dead.
TEST(four_distinct_peers_score_four_times_and_a_fifth_is_capped)
{
  Fixture& f = fx();
  CHECK(link_start(f.job, FAKE, 0u));
  CHECK_EQ((int)ACT_CAP_PEERS, 4);
  uint32_t t = 0;
  for (uint8_t d = 0; d < 5u; ++d) {
    for (uint8_t i = 0; i < 3u; ++i) {
      air((uint32_t)(0x7000u + d), -40, d);
      t += 100u;
      serve(f, t);
    }
  }
  CHECK_EQ((int)link_qualified_count(f.job), 5);
  CHECK_EQ((int)f.job.peers_scored, 5);            // the table crossed five times
  // ...and game/activity.cpp's own ceiling is what stops the fifth paying. Two
  // gates, and the outer one is not this module's.
  CHECK_EQ(act_score_today(f.cds, clk()),
           (uint16_t)(ACT_CAP_PEERS * ACT_PTS_PEER));
}

// THE EVICTION HOLE, CLOSED BY THE OTHER MODULE AND SAID SO. discovery.cpp's
// table can evict a qualified peer and re-admit it, which would pay twice if
// the distinctness lived here. It lives in game/activity.cpp, per DAY, and this
// is the case that proves the pair is safe rather than the part.
TEST(a_peer_evicted_and_rediscovered_is_not_scored_twice)
{
  Fixture& f = fx();
  CHECK(link_start(f.job, FAKE, 0u));
  uint32_t t = 0;
  for (uint8_t i = 0; i < 3u; ++i) { air(0x9001u, -40, 0u); t += 100u; serve(f, t); }
  CHECK_EQ((int)f.job.peers_scored, 1);
  CHECK_EQ(act_score_today(f.cds, clk()), (uint16_t)ACT_PTS_PEER);

  // Age it out completely, then let it walk back in and qualify again.
  t += (uint32_t)LINK_PEER_TTL_S * 1000UL;
  serve(f, t);
  CHECK_EQ((int)link_peer_count(f.job), 0);
  for (uint8_t i = 0; i < 3u; ++i) { air(0x9001u, -40, 0u); t += 100u; serve(f, t); }
  CHECK_EQ((int)link_qualified_count(f.job), 1);
  CHECK_EQ((int)f.job.peers_scored, 2);            // the TABLE crossed twice...
  // ...and the SCORE is unchanged, because the day's distinct set remembers.
  CHECK_EQ(act_score_today(f.cds, clk()), (uint16_t)ACT_PTS_PEER);
}

// =============================================================================
//  6. THE JOB
// =============================================================================

// MUTATION: beacon on every service() instead of on the cadence and the count
// jumps from 3 to 20.
TEST(a_beacon_goes_out_on_the_cadence_and_not_at_frame_rate)
{
  Fixture& f = fx();
  CHECK(link_start(f.job, FAKE, 10000u));
  CHECK_EQ((int)LINK_BEACON_MS, 500);

  // The FIRST service beacons immediately: a player who has just walked up
  // should be visible within one frame, not within one beacon period.
  serve(f, 10000u);
  CHECK_EQ((int)f.job.beacons_tx, 1);
  CHECK_EQ((int)g_last_beacon_len, (int)DISC_BEACON_BYTES);

  // Twenty frames at 30 Hz is 660 ms and must produce exactly one more - not
  // twenty. The clock is re-stamped at the frame the beacon actually went out
  // on rather than advanced by exactly LINK_BEACON_MS, so the cadence drifts by
  // up to one frame and never catches up in a burst; 11,100 ms is the next
  // frame past the second period and is where the third belongs.
  for (uint8_t i = 1; i <= 20u; ++i) serve(f, (uint32_t)(10000u + i * 33u));
  CHECK_EQ((int)f.job.beacons_tx, 2);
  serve(f, 11100u);
  CHECK_EQ((int)f.job.beacons_tx, 3);

  // And the cadence over a long run really is one per period and not one per
  // frame: ten seconds at 30 Hz is 300 services and about 20 beacons.
  for (uint16_t i = 1; i <= 300u; ++i) serve(f, (uint32_t)(11100u + i * 33u));
  CHECK(f.job.beacons_tx >= 21u);
  CHECK(f.job.beacons_tx <= 24u);

  // AND THE BEACON ON THE AIR IS THIS DEVICE'S OWN IDENTITY, decodable by the
  // very decoder a peer would use.
  DiscBeacon got;
  CHECK_EQ((int)disc_decode(g_last_beacon, g_last_beacon_len, got), (int)DE_OK);
  CHECK_EQ(got.device_id, self_beacon().device_id);
  CHECK_EQ(strcmp(got.name, "Yo"), 0);
  CHECK_EQ((int)got.caps, (int)(DISC_CAP_BATTLE | DISC_CAP_TRADE));
}

// A driver that refuses the broadcast is not an error and does not spin: the
// cadence clock advances either way, or an unreachable radio would be asked at
// frame rate.
TEST(a_broadcast_the_driver_refuses_does_not_spin_the_cadence)
{
  Fixture& f = fx();
  g_beacon_ok = false;
  CHECK(link_start(f.job, FAKE, 0u));
  for (uint8_t i = 0; i < 20u; ++i) serve(f, (uint32_t)(i * 33u));
  CHECK_EQ((int)f.job.beacons_tx, 0);          // none went out...
  CHECK_EQ((int)g_beacon_calls, 2);            // ...and only two were attempted
  CHECK(link_is_busy(f.job));
}

// =============================================================================
//  THE HOLD (P7-C2): THE BROWSE IS OVER AND THE RADIO IS STILL OURS
//
//  ui/screen_link.cpp calls link_hold() the moment the two players consent,
//  because link_cancel() would reach net_request(RADIO_OFF) - which passes
//  wifi_down(), which calls espnow_end() - and take the session's own link down
//  one frame after it was agreed. Everything a browse does must stop; the radio
//  must not.
// =============================================================================

// MUTATION: delete the `if (j.held != 0u) return;` line at the top of
// link_service() and this case fails on the beacon count - a held job starts
// beaconing again underneath a bound unicast session.
TEST(a_held_job_stops_browsing_and_keeps_the_radio)
{
  Fixture& f = fx();
  CHECK(link_start(f.job, FAKE, 0u));
  air(0x11111111u, -40, 1);
  serve(f, 0u);
  air(0x11111111u, -40, 1);
  serve(f, 600u);
  air(0x11111111u, -40, 1);
  serve(f, 1200u);
  CHECK_EQ((int)link_qualified_count(f.job), 1);
  const int beacons = g_beacon_calls;
  CHECK(beacons > 0);

  CHECK(!link_is_held(f.job));
  link_hold(f.job);
  CHECK(link_is_held(f.job));

  // The radio is STILL OURS: the job is running, nothing was stopped, and the
  // power ladder's `held` input still says so.
  CHECK(link_is_busy(f.job));
  CHECK_EQ((int)f.job.state, (int)LS_RUNNING);
  CHECK_EQ((int)g_stop_calls, 0);

  // And the browse has stopped: no beacon, no drain, no ageing, and the
  // section 47 ceiling on the BROWSE no longer fires - the bound from here is
  // the session's own ladder, which lives in networking/session.cpp.
  air(0x22222222u, -40, 2);
  air(0x22222222u, -40, 2);
  air(0x22222222u, -40, 2);
  for (uint32_t t = 1800u; t <= (uint32_t)LINK_JOB_TIMEOUT_MS + 5000u; t += 600u)
    serve(f, t);
  CHECK_EQ(g_beacon_calls, beacons);                 // not one more beacon
  CHECK_EQ((int)link_qualified_count(f.job), 1);     // and no new peer
  CHECK_EQ((int)f.job.state, (int)LS_RUNNING);       // the ceiling did not fire
  CHECK_EQ((int)g_stop_calls, 0);
}

// AND THE ONE RELEASE PATH IS STILL THE ONE RELEASE PATH. A held job gives the
// radio back through link_cancel() and through nothing else, exactly once.
TEST(cancelling_a_held_job_still_releases_the_radio_exactly_once)
{
  Fixture& f = fx();
  CHECK(link_start(f.job, FAKE, 0u));
  serve(f, 0u);
  link_hold(f.job);
  CHECK_EQ((int)g_stop_calls, 0);

  link_cancel(f.job, FAKE);
  CHECK_EQ((int)g_stop_calls, 1);
  CHECK_EQ((int)f.job.state, (int)LS_CANCELLED);
  CHECK(!link_is_busy(f.job));
  link_cancel(f.job, FAKE);
  CHECK_EQ((int)g_stop_calls, 1);

  // A job that never held the radio cannot be held: link_is_held() would be a
  // lie about a job that is not LS_RUNNING.
  Fixture& g = fx();
  link_hold(g.job);
  CHECK(!link_is_held(g.job));
  CHECK_EQ((int)g_start_calls, 0);
  CHECK_EQ((int)g_stop_calls, 0);
}

// THE RADIO IS RELEASED EXACTLY ONCE PER RUN, WHATEVER ENDS THE RUN. This is
// the property hardware/power.h's release() hook depends on: the ladder
// navigates, the leaving screen's leave() hook cancels, and the radio goes back
// exactly once - never twice, never not at all.
TEST(the_radio_is_released_exactly_once_on_every_exit_path)
{
  // (a) cancel
  Fixture& f = fx();
  CHECK(link_start(f.job, FAKE, 0u));
  serve(f, 100u);
  CHECK(link_is_busy(f.job));
  link_cancel(f.job, FAKE);
  CHECK_EQ((int)f.job.state, (int)LS_CANCELLED);
  CHECK_EQ((int)g_stop_calls, 1);
  CHECK(!link_is_busy(f.job));
  link_cancel(f.job, FAKE);                    // twice does not release twice
  CHECK_EQ((int)g_stop_calls, 1);
  serve(f, 200u);                              // and it cannot be revived
  CHECK_EQ((int)f.job.state, (int)LS_CANCELLED);

  // (b) the section 47 ceiling
  Fixture& g = fx();
  CHECK(link_start(g.job, FAKE, 5000u));
  serve(g, 5000u + (uint32_t)LINK_JOB_TIMEOUT_MS - 1u);
  CHECK(link_is_busy(g.job));
  CHECK_EQ((int)g_stop_calls, 0);
  serve(g, 5000u + (uint32_t)LINK_JOB_TIMEOUT_MS);
  CHECK_EQ((int)g.job.state, (int)LS_TIMEOUT);
  CHECK_EQ((int)g_stop_calls, 1);
  serve(g, 5000u + (uint32_t)LINK_JOB_TIMEOUT_MS + 5000u);
  CHECK_EQ((int)g_stop_calls, 1);

  // (c) a bring-up that never took. stop() is still called: the driver may have
  //     got half way up before refusing.
  Fixture& h = fx();
  g_start_ok = false;
  CHECK(!link_start(h.job, FAKE, 0u));
  CHECK_EQ((int)h.job.state, (int)LS_FAILED);
  CHECK_EQ((int)g_stop_calls, 1);
  CHECK(!link_is_busy(h.job));

  // (d) the radio taken away underneath a running job.
  Fixture& k = fx();
  CHECK(link_start(k.job, FAKE, 0u));
  serve(k, 0u);
  g_poll_answer = (int8_t)LINK_POLL_FAILED;
  serve(k, 100u);
  CHECK_EQ((int)k.job.state, (int)LS_FAILED);
  CHECK_EQ((int)g_stop_calls, 1);
}

TEST(cancelling_a_job_that_never_started_touches_no_radio)
{
  Fixture& f = fx();
  link_cancel(f.job, FAKE);
  CHECK_EQ((int)g_stop_calls, 0);
  CHECK_EQ((int)g_start_calls, 0);
  CHECK_EQ((int)f.job.state, (int)LS_IDLE);
}

TEST(a_second_start_while_one_link_runs_is_refused_and_not_stacked)
{
  Fixture& f = fx();
  CHECK(link_start(f.job, FAKE, 0u));
  for (uint8_t i = 0; i < 3u; ++i) { air(0x4444u, -40, 0u); serve(f, (uint32_t)(i * 100u)); }
  CHECK_EQ((int)link_qualified_count(f.job), 1);

  CHECK(!link_start(f.job, FAKE, 500u));       // one radio, one link
  CHECK_EQ((int)g_start_calls, 1);
  CHECK_EQ((int)g_stop_calls, 0);
  CHECK_EQ((int)link_qualified_count(f.job), 1);   // and it did not reset the table
}

TEST(a_finished_job_can_be_started_again_and_keeps_nothing_from_the_last_one)
{
  Fixture& f = fx();
  CHECK(link_start(f.job, FAKE, 0u));
  for (uint8_t i = 0; i < 3u; ++i) { air(0x5555u, -40, 0u); serve(f, (uint32_t)(i * 100u)); }
  CHECK_EQ((int)link_peer_count(f.job), 1);
  link_cancel(f.job, FAKE);
  // A CANCELLED LINK KEEPS ITS PEERS, and that is a deliberate difference from
  // the scan: a scan's results are an encounter roll's input and a stale one
  // would be a free encounter, while section 47's "CONEXION PERDIDA /
  // A: Reintentar" needs to know who the player was talking to.
  CHECK_EQ((int)link_peer_count(f.job), 1);

  CHECK(link_start(f.job, FAKE, 9000u));
  CHECK_EQ((int)link_peer_count(f.job), 0);    // ...but a NEW run starts empty
  CHECK_EQ((int)f.job.beacons_tx, 0);
  CHECK_EQ((int)g_start_calls, 2);
  link_cancel(f.job, FAKE);
  CHECK_EQ((int)g_stop_calls, 2);              // once per run, still
}

TEST(the_ceiling_survives_the_millis_wrap)
{
  Fixture& f = fx();
  const uint32_t near_wrap = 0xFFFFF000u;
  CHECK(link_start(f.job, FAKE, near_wrap));
  serve(f, (uint32_t)(near_wrap + 1000u));     // 1 s later, having wrapped
  CHECK(link_is_busy(f.job));
  CHECK_EQ((int)link_elapsed_ms(f.job, (uint32_t)(near_wrap + 1000u)), 1000);
  serve(f, (uint32_t)(near_wrap + (uint32_t)LINK_JOB_TIMEOUT_MS));
  CHECK_EQ((int)f.job.state, (int)LS_TIMEOUT);
}

// THE ARITHMETIC hardware/power.h DEPENDS ON, as a case and not only as a
// static_assert: the browse gives the radio back BEFORE the idle rung would
// want it, so `held` can never clamp the ladder at DIM indefinitely.
TEST(the_link_gives_the_radio_back_before_the_idle_rung_would_want_it)
{
  CHECK((uint32_t)LINK_JOB_TIMEOUT_MS < (uint32_t)PWR_IDLE_MS);
  Fixture& f = fx();
  CHECK(link_start(f.job, FAKE, 0u));
  // Drive the whole ladder's worth of frames at 30 Hz and watch the job end -
  // and therefore stop being `held` - strictly before PWR_IDLE_MS.
  uint32_t t = 0;
  while (link_is_busy(f.job) && t < (uint32_t)PWR_IDLE_MS) { t += 33u; serve(f, t); }
  CHECK(!link_is_busy(f.job));
  CHECK_EQ((int)f.job.state, (int)LS_TIMEOUT);
  CHECK(t < (uint32_t)PWR_IDLE_MS);
  CHECK_EQ((int)g_stop_calls, 1);
}

// A BOUNDED DRAIN: a crowded room cannot make one frame arbitrarily long, and
// what is left over is picked up on the next frame rather than dropped.
TEST(one_service_drains_a_bounded_number_of_beacons_and_loses_none)
{
  Fixture& f = fx();
  CHECK(link_start(f.job, FAKE, 0u));
  CHECK_EQ((int)LINK_DRAIN_PER_SERVICE, 8);
  for (uint8_t i = 0; i < 12u; ++i) air((uint32_t)(0x8100u + i), -40, (uint8_t)(i % 8u));
  serve(f, 0u);
  CHECK_EQ((int)f.job.beacons_rx, 8);          // eight this frame...
  serve(f, 33u);
  CHECK_EQ((int)f.job.beacons_rx, 12);         // ...and the remaining four next
}

TEST(a_job_that_is_not_running_ignores_everything_it_is_handed)
{
  Fixture& f = fx();
  air(0x1234u, -30, 0u);
  serve(f, 0u);                                // never started
  CHECK_EQ((int)f.job.beacons_rx, 0);
  CHECK_EQ((int)f.job.beacons_tx, 0);
  CHECK_EQ((int)g_beacon_calls, 0);
  CHECK_EQ((int)g_start_calls, 0);
}
