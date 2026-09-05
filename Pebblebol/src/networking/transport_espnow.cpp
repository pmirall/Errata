// =============================================================================
//  PEBBLEBOL - networking/transport_espnow.cpp
//  See transport_espnow.h for the seam, the MAC rule, what the callbacks may do
//  and why the send callback is a statistic rather than a return value.
//
//  THE ONLY FILE THAT INCLUDES esp_now.h. tools/check.sh counts esp_now_* call
//  sites under src/ and fails at any that are not in this file.
//
//  NO ARDUINO STRING, NO HEAP, NO FLOAT. millis() is read - hardware/input.cpp
//  reads it from a timer callback too - and nothing else from Arduino.h.
// =============================================================================
#include "transport_espnow.h"

#include "../core/config.h"

#if FEATURE_ESPNOW

#include <Arduino.h>
#include <string.h>
#include <esp_now.h>          // the ONE include; esp_wifi.h is NOT pulled in
#include <esp_err.h>

#include "rxring.h"

// PB_LINK_CHANNEL IS CHECKED HERE AND NOT WHERE IT IS SET, because this is the
// file that knows why: WiFiGeneric.cpp's setChannel() refuses a channel outside
// the country range and the default country is world-safe "01" with schan=1,
// nchan=11. A 12 or 13 would be silently ignored, the two devices would end up
// on whatever channel the last scan left, and every beacon would miss.
static_assert(PB_LINK_CHANNEL >= 1 && PB_LINK_CHANNEL <= 11,
              "PB_LINK_CHANNEL must be inside the world-safe country range "
              "(1..11) or WiFi.setChannel() refuses it and the link is silent");
// A beacon and a session frame must each fit ONE datagram. protocol.h already
// pins the frame; this pins the beacon, and both are far under the 250 B a v1.0
// ESP-NOW device accepts before discarding the packet entirely.
static_assert((uint16_t)DISC_BEACON_BYTES <= (uint16_t)ESP_NOW_MAX_DATA_LEN,
              "a beacon must fit one ESP-NOW datagram");
static_assert((uint16_t)PROTO_FRAME_MAX <= (uint16_t)ESP_NOW_MAX_DATA_LEN,
              "a session frame must fit one ESP-NOW datagram");
static_assert(LINK_PEER_CAP + 2 <= ESP_NOW_MAX_TOTAL_PEER_NUM,
              "the slot table must not be able to ask for more ESP-NOW peer "
              "entries than the driver has");

#define EN_ADDR_LEN  6u

// -----------------------------------------------------------------------------
//  MODULE STATE. File-scope because esp_now_recv_cb_t has no void* - see the
//  header. Everything here is either a ring the pure module owns the code for,
//  or the six-byte addresses that may not leave this file.
// -----------------------------------------------------------------------------

// --- the session ring: unicast frames from the bound peer --------------------
static uint8_t  s_rx_bytes[(size_t)LINK_RX_SLOTS * (size_t)PROTO_FRAME_MAX];
static uint16_t s_rx_lens[LINK_RX_SLOTS];
static RxRing   s_rx;

// --- the beacon ring: broadcasts from anybody -------------------------------
// The address is IN THE RING SLOT and never leaves this translation unit. It is
// here rather than resolved to a slot index inside the callback because slot
// assignment would then be a table written by the Wi-Fi task and read by
// loop(); doing it in espnow_beacon_pop() keeps the slot table single-threaded.
struct BeaconSlot {
  uint8_t addr[EN_ADDR_LEN];
  int8_t  rssi;
  uint8_t len;
  uint8_t payload[DISC_BEACON_BYTES];
};
static_assert(sizeof(BeaconSlot) == 32, "BeaconSlot sizes the beacon ring");

static uint8_t  s_bc_bytes[(size_t)LINK_BEACON_SLOTS * sizeof(BeaconSlot)];
static uint16_t s_bc_lens[LINK_BEACON_SLOTS];
static RxRing   s_bc;

// --- the private address table ----------------------------------------------
static uint8_t  s_slot_addr[LINK_PEER_CAP][EN_ADDR_LEN];
static uint8_t  s_slot_used[LINK_PEER_CAP];      // 0/1
static uint8_t  s_slot_next  = 0;                // round-robin victim

// --- the bound session peer --------------------------------------------------
static uint8_t  s_bound_addr[EN_ADDR_LEN];
static bool     s_bound      = false;

static bool     s_up         = false;
static EspNowStats s_st;
static uint32_t s_ack_ms     = 0;
static bool     s_ever_acked = false;

static const uint8_t EN_BROADCAST[EN_ADDR_LEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static inline bool addr_eq(const uint8_t* a, const uint8_t* b)
{
  return memcmp(a, b, EN_ADDR_LEN) == 0;
}

// =============================================================================
//  THE CALLBACKS. Wi-Fi task. Filter, clamp, copy, publish one index, return.
// =============================================================================

// esp_now.h's IDF 5.x shape: an info STRUCT, not a bare address. Registering
// the IDF 4.x signature against this header is a hard compile error, which is
// the good failure mode - but it means every ESP-NOW snippet older than IDF 5.0
// is wrong for this tree.
static void en_on_recv(const esp_now_recv_info_t* info, const uint8_t* data, int len)
{
  // data_len is SIGNED. Check it before casting, or a negative becomes a very
  // large uint16_t and the length rules below all pass.
  if (info == nullptr || data == nullptr || len <= 0) {
    s_st.rx_malformed++;
    return;
  }
  const uint16_t n = (uint16_t)len;

  // rx_ctrl->rssi is a `signed rssi : 8` bitfield. Clamped rather than trusted,
  // the same way ble_social.cpp:244-245 clamps BLE's int.
  int8_t rssi = -128;
  if (info->rx_ctrl != nullptr) {
    const int r = (int)info->rx_ctrl->rssi;
    rssi = (int8_t)((r < -128) ? -128 : ((r > 127) ? 127 : r));
  }

  const bool bcast = (info->des_addr != nullptr) && addr_eq(info->des_addr, EN_BROADCAST);

  if (bcast) {
    // A BROADCAST IS A BEACON OR IT IS NOTHING. The prefilter is cheap on
    // purpose - one length compare and two bytes - because the 2.4 GHz band is
    // shared and anybody's ESP-NOW traffic lands here. The CRC and every field
    // rule are disc_decode()'s, on the loop task, where they are testable.
    if (n != (uint16_t)DISC_BEACON_BYTES ||
        data[0] != (uint8_t)DISC_MAGIC_0 || data[1] != (uint8_t)DISC_MAGIC_1) {
      return;                        // not ours; not even counted as malformed
    }
    BeaconSlot slot;
    memset(&slot, 0, sizeof slot);
    if (info->src_addr != nullptr) memcpy(slot.addr, info->src_addr, EN_ADDR_LEN);
    slot.rssi = rssi;
    slot.len  = (uint8_t)n;
    memcpy(slot.payload, data, (size_t)n);
    if (!rxring_push(s_bc, (const uint8_t*)&slot, (uint16_t)sizeof slot)) {
      s_st.rx_beacon_overflow++;
      return;
    }
    s_st.rx_beacon++;
    return;
  }

  // UNICAST. Only from the peer we are actually talking to. This is also the
  // cheapest denial-of-service leash available at this layer: session.h:107-112
  // is honest that the codec is not an authenticator, and a frame from a
  // stranger never reaches the ring, let alone the FSM.
  if (!s_bound || info->src_addr == nullptr || !addr_eq(info->src_addr, s_bound_addr)) {
    s_st.rx_wrong_peer++;
    return;
  }
  if (n > (uint16_t)PROTO_FRAME_MAX) {
    s_st.rx_malformed++;             // rxring_push would refuse it anyway
    return;
  }
  if (!rxring_push(s_rx, data, n)) {
    s_st.rx_ring_overflow++;
    return;
  }
  s_st.rx_session++;
}

// A BARE ADDRESS HERE, not the info struct - the asymmetry is easy to get
// wrong. Meaningful for unicast only: a broadcast reports success
// unconditionally and is therefore not counted as an ACK.
static void en_on_sent(const uint8_t* addr, esp_now_send_status_t status)
{
  if (addr != nullptr && addr_eq(addr, EN_BROADCAST)) return;
  if (status == ESP_NOW_SEND_SUCCESS) {
    s_st.tx_ok++;
    s_ack_ms     = millis();
    s_ever_acked = true;
  } else {
    s_st.tx_fail++;
  }
}

// =============================================================================
//  LIFECYCLE. loop() only.
// =============================================================================
bool espnow_begin(void)
{
  if (s_up) return true;

  (void)rxring_init(s_rx, s_rx_bytes, s_rx_lens,
                    (uint8_t)LINK_RX_SLOTS, (uint16_t)PROTO_FRAME_MAX);
  (void)rxring_init(s_bc, s_bc_bytes, s_bc_lens,
                    (uint8_t)LINK_BEACON_SLOTS, (uint16_t)sizeof(BeaconSlot));
  memset(s_slot_addr, 0, sizeof s_slot_addr);
  memset(s_slot_used, 0, sizeof s_slot_used);
  s_slot_next = 0;
  s_bound     = false;
  memset(s_bound_addr, 0, sizeof s_bound_addr);
  memset(&s_st, 0, sizeof s_st);

  if (esp_now_init() != ESP_OK) return false;
  if (esp_now_register_recv_cb(&en_on_recv) != ESP_OK) { (void)esp_now_deinit(); return false; }
  if (esp_now_register_send_cb(&en_on_sent) != ESP_OK) { (void)esp_now_deinit(); return false; }

  // THE BROADCAST PEER IS AN ORDINARY PEER ENTRY and it must exist before a
  // broadcast can be sent: esp_now_send() answers ESP_ERR_ESPNOW_NOT_FOUND for
  // an address with no entry, and only a NULL address means "every peer".
  // channel 0 = "use the current channel the station is on", so the channel is
  // decided in exactly ONE place (net.cpp's WiFi.setChannel) and
  // ESP_ERR_ESPNOW_CHAN is unreachable by construction. encrypt=false is not a
  // choice either: a broadcast peer cannot be encrypted.
  esp_now_peer_info_t p;
  memset(&p, 0, sizeof p);
  memcpy(p.peer_addr, EN_BROADCAST, EN_ADDR_LEN);
  p.channel = 0;
  p.ifidx   = WIFI_IF_STA;
  p.encrypt = false;
  if (esp_now_add_peer(&p) != ESP_OK) { (void)esp_now_deinit(); return false; }

  s_up = true;
  return true;
}

void espnow_end(void)
{
  if (!s_up) {
    // Still clear the transient half: a caller may be tearing down after a
    // failed bring-up, and a half-filled ring must not survive into the next one.
    s_bound = false;
    memset(s_bound_addr, 0, sizeof s_bound_addr);
    return;
  }
  // UNREGISTER FIRST. After these two calls no producer exists, which is what
  // makes the memset of both rings below safe - the one case where storing 0
  // into head AND tail is correct, and it is correct only because the other
  // writer is provably gone (ble_social.cpp:345-350 has the same reset with the
  // opposite conclusion, because its producer was still running).
  (void)esp_now_unregister_recv_cb();
  (void)esp_now_unregister_send_cb();
  if (s_bound) (void)esp_now_del_peer(s_bound_addr);
  (void)esp_now_del_peer(EN_BROADCAST);
  (void)esp_now_deinit();

  s_up    = false;
  s_bound = false;
  memset(s_bound_addr, 0, sizeof s_bound_addr);
  memset(s_slot_addr, 0, sizeof s_slot_addr);
  memset(s_slot_used, 0, sizeof s_slot_used);
  s_slot_next = 0;
  (void)rxring_init(s_rx, s_rx_bytes, s_rx_lens,
                    (uint8_t)LINK_RX_SLOTS, (uint16_t)PROTO_FRAME_MAX);
  (void)rxring_init(s_bc, s_bc_bytes, s_bc_lens,
                    (uint8_t)LINK_BEACON_SLOTS, (uint16_t)sizeof(BeaconSlot));
}

bool espnow_is_up(void) { return s_up; }

bool espnow_broadcast(const uint8_t* frame, uint16_t n)
{
  if (!s_up || frame == nullptr || n == 0u) return false;
  if (n > (uint16_t)ESP_NOW_MAX_DATA_LEN) return false;
  const esp_err_t e = esp_now_send(EN_BROADCAST, frame, (size_t)n);
  if (e != ESP_OK) {
    if (e == ESP_ERR_ESPNOW_NO_MEM) s_st.tx_no_mem++;
    s_st.tx_refused++;
    return false;
  }
  s_st.beacons_tx++;
  return true;
}

// =============================================================================
//  A MAC BECOMES A SLOT. loop() only, which is what keeps the table
//  single-threaded and what stops the callback from ever touching it.
// =============================================================================
static uint8_t slot_for(const uint8_t* addr)
{
  for (uint8_t i = 0; i < (uint8_t)LINK_PEER_CAP; ++i) {
    if (s_slot_used[i] && addr_eq(s_slot_addr[i], addr)) return i;
  }
  for (uint8_t i = 0; i < (uint8_t)LINK_PEER_CAP; ++i) {
    if (!s_slot_used[i]) {
      memcpy(s_slot_addr[i], addr, EN_ADDR_LEN);
      s_slot_used[i] = 1u;
      return i;
    }
  }
  // Full. Round-robin, but NEVER over the bound peer: a session in flight would
  // lose the address it unicasts to, which is the one failure here that costs
  // more than a UI row.
  for (uint8_t k = 0; k < (uint8_t)LINK_PEER_CAP; ++k) {
    const uint8_t i = (uint8_t)((s_slot_next + k) % (uint8_t)LINK_PEER_CAP);
    if (s_bound && addr_eq(s_slot_addr[i], s_bound_addr)) continue;
    memcpy(s_slot_addr[i], addr, EN_ADDR_LEN);
    s_slot_used[i] = 1u;
    s_slot_next    = (uint8_t)((i + 1u) % (uint8_t)LINK_PEER_CAP);
    return i;
  }
  return 0u;                       // every slot is the bound peer: impossible
}

uint8_t espnow_beacon_pop(DiscRx& out)
{
  BeaconSlot slot;
  const uint16_t n = rxring_pop(s_bc, (uint8_t*)&slot, (uint16_t)sizeof slot);
  if (n != (uint16_t)sizeof slot) return 0u;

  memset(&out, 0, sizeof out);
  out.len  = (uint16_t)((slot.len > (uint8_t)DISC_BEACON_BYTES)
                        ? (uint8_t)DISC_BEACON_BYTES : slot.len);
  out.rssi = slot.rssi;
  out.slot = slot_for(slot.addr);
  memcpy(out.payload, slot.payload, sizeof out.payload);
  return 1u;                       // ...and slot.addr dies with this stack frame
}

bool espnow_bind(uint8_t slot)
{
  if (!s_up || slot >= (uint8_t)LINK_PEER_CAP || !s_slot_used[slot]) return false;
  if (s_bound) espnow_unbind();

  esp_now_peer_info_t p;
  memset(&p, 0, sizeof p);
  memcpy(p.peer_addr, s_slot_addr[slot], EN_ADDR_LEN);
  p.channel = 0;                   // follow the station's channel; see begin()
  p.ifidx   = WIFI_IF_STA;
  p.encrypt = false;               // no encrypted peers anywhere in this design
  const esp_err_t e = esp_now_add_peer(&p);
  if (e != ESP_OK && e != ESP_ERR_ESPNOW_EXIST) return false;

  memcpy(s_bound_addr, s_slot_addr[slot], EN_ADDR_LEN);
  s_bound = true;
  // A NEW SESSION DOES NOT INHERIT THE LAST ONE'S FRAMES. Drained from the
  // consumer side (rxring.h), because the Wi-Fi task owns the other cursor.
  rxring_drain(s_rx);
  return true;
}

void espnow_unbind(void)
{
  if (!s_bound) return;
  if (s_up) (void)esp_now_del_peer(s_bound_addr);
  s_bound = false;
  memset(s_bound_addr, 0, sizeof s_bound_addr);
  rxring_drain(s_rx);
}

bool espnow_bound(void) { return s_bound; }

// =============================================================================
//  THE SEAM
// =============================================================================
static bool en_send(void* ctx, const uint8_t* frame, uint16_t n)
{
  (void)ctx;
  if (!s_up || !s_bound || frame == nullptr || n == 0u) {
    s_st.tx_refused++;
    return false;
  }
  const esp_err_t e = esp_now_send(s_bound_addr, frame, (size_t)n);
  if (e == ESP_OK) return true;    // handed to the driver; the ACK is a statistic
  if (e == ESP_ERR_ESPNOW_NO_MEM) s_st.tx_no_mem++;
  s_st.tx_refused++;
  return false;
}

static uint16_t en_recv(void* ctx, uint8_t* buf, uint16_t cap)
{
  (void)ctx;
  return rxring_pop(s_rx, buf, cap);
}

Transport transport_espnow(EspNowPort& port)
{
  port.endpoint = 0u;
  Transport t;
  t.ctx  = &port;
  t.send = &en_send;
  t.recv = &en_recv;
  // PROTO_FRAME_MAX (164) AND NOT ESP_NOW_MAX_DATA_LEN (250). No legal frame
  // exceeds 164, so 164 is the tighter and correct ceiling; 250 would let an
  // encoder bug put a longer frame on air that the far end then names
  // PE_OVERSIZE. transport.h keeps mtu as DATA so a test can still move it.
  t.mtu  = (uint16_t)PROTO_FRAME_MAX;
  return t;
}

const EspNowStats& espnow_stats(void) { return s_st; }
uint32_t espnow_ack_ms(void)          { return s_ack_ms; }
bool     espnow_ever_acked(void)      { return s_ever_acked; }

#else   // FEATURE_ESPNOW

// -----------------------------------------------------------------------------
//  FEATURE_ESPNOW == 0. A DEFINED REFUSAL, not a missing symbol.
//
//  The build that runs this arm has no peer link at all: net.cpp answers
//  NERR_ESPNOW_DISABLED, the discovery job goes straight to LS_FAILED with the
//  radio released, and nothing hangs. discovery.cpp and rxring.cpp are pure and
//  are compiled either way - the codec and the ring are not the radio.
// -----------------------------------------------------------------------------
static EspNowStats s_st_off;

bool espnow_begin(void)                                  { return false; }
void espnow_end(void)                                    { }
bool espnow_is_up(void)                                  { return false; }
bool espnow_broadcast(const uint8_t* f, uint16_t n)      { (void)f; (void)n; return false; }
uint8_t espnow_beacon_pop(DiscRx& out)                   { (void)out; return 0u; }
bool espnow_bind(uint8_t slot)                           { (void)slot; return false; }
void espnow_unbind(void)                                 { }
bool espnow_bound(void)                                  { return false; }
uint32_t espnow_ack_ms(void)                             { return 0u; }
bool espnow_ever_acked(void)                             { return false; }
const EspNowStats& espnow_stats(void)                    { return s_st_off; }

static bool en_send(void* ctx, const uint8_t* frame, uint16_t n)
{ (void)ctx; (void)frame; (void)n; return false; }
static uint16_t en_recv(void* ctx, uint8_t* buf, uint16_t cap)
{ (void)ctx; (void)buf; (void)cap; return 0u; }

Transport transport_espnow(EspNowPort& port)
{
  port.endpoint = 0u;
  Transport t;
  t.ctx  = &port;
  t.send = &en_send;
  t.recv = &en_recv;
  t.mtu  = (uint16_t)PROTO_FRAME_MAX;
  return t;
}

#endif  // FEATURE_ESPNOW
