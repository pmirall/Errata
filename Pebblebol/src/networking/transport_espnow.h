// =============================================================================
//  PEBBLEBOL - networking/transport_espnow.h
//  THE ESP-NOW DRIVER BEHIND networking/transport.h (plan P7-C1, decision D2).
//
//  THIS IS THE ONLY FILE IN THE TREE THAT INCLUDES esp_now.h, and tools/check.sh
//  gates that. transport.h promised it in so many words at P4-C5 - "P7's
//  transport_espnow.cpp is the only file in the tree that will include
//  esp_now.h, and it is excluded from tests/Makefile BY FILE SELECTION, which
//  is why networking/session.cpp and networking/battle_link.cpp contain no
//  conditional compilation at all" - and this chunk is where that is either
//  kept or discovered to be false. It was kept: session.cpp and battle_link.cpp
//  are BYTE-IDENTICAL across P7-C1, and check.sh now fails the build if either
//  of them grows a single preprocessor conditional.
//
// -----------------------------------------------------------------------------
//  WHY THIS FILE HAS FILE-SCOPE MUTABLE STATE, WHICH THE PURE MODULES MAY NOT
// -----------------------------------------------------------------------------
//  esp_now_recv_cb_t and esp_now_send_cb_t HAVE NO USER-CONTEXT ARGUMENT. There
//  is no `void*` in either signature, so the sink a callback posts into is
//  forced to be reachable from file scope. That is not a style choice, it is
//  the API - and a device has one radio, so a singleton is the truth. This file
//  is therefore on tools/check.sh's IMPURE_NET list, beside net.cpp and
//  webui.cpp, which is the declared list of device modules.
//
//  THE MECHANISM IS NOT A SINGLETON WITH IT. The ring is networking/rxring.h,
//  which is pure and caller-owned and driven by a host binary; only the two
//  INSTANCES live here. The peer table is networking/discovery.h, likewise.
//  What is left in this file is exactly the part no host binary can reach: the
//  radio calls and the callback context.
//
// -----------------------------------------------------------------------------
//  THE MAC ADDRESS, AND THE RULE STATED IN FULL BECAUSE THIS IS THE FILE THAT
//  MAY SAY IT
// -----------------------------------------------------------------------------
//  Every 802.11 frame carries its sender's MAC address in the header, and
//  esp_now.h hands it to us at esp_now_info->src_addr. We cannot remove it and
//  will not pretend to. What this firmware does is refuse to AMPLIFY it:
//
//    * the MAC of a peer lives in ONE private array in transport_espnow.cpp,
//      s_slot_addr[LINK_PEER_CAP][6], plus the six bytes of a bound session
//      peer. Nothing else in the tree holds one.
//    * what crosses into networking/discovery.h - and therefore into the game,
//      the UI and anything persisted - is a SLOT INDEX. A DiscPeer has no
//      address field and check.sh fails the build if discovery.{h,cpp} so much
//      as names one.
//    * it is never persisted, never rendered, never hashed into anything the
//      game stores, and never used to derive the beacon's name. net.cpp's
//      AP SSID is built from two bytes of the station MAC; the beacon name is
//      the player's own pet name, so those two bytes stay out of the one
//      payload spec section 43 governs.
//    * MAC randomisation (esp_wifi_set_mac) is available and is deliberately
//      NOT used: it would break peer stability across a reboot and buys little,
//      because the address is on air either way.
//
//  esp_now.h:111 says the recv info struct and all three of its pointers are
//  valid ONLY inside the callback, so none of them is retained: the six bytes
//  are COPIED into a ring slot and the pointer is dropped at the return.
//
// -----------------------------------------------------------------------------
//  WHAT THE RECEIVE CALLBACK MAY DO, AND IT IS A SHORT LIST
// -----------------------------------------------------------------------------
//  It runs on the Wi-Fi task. ble_social.cpp's banner states the rule for the
//  BTC task and this file inherits it verbatim: filter, clamp, copy, publish
//  one index, return. NO U8g2, no I2C, no Preferences, no Serial (net.cpp:52-54
//  already says never block on HWCDC), no allocation, nothing that touches game
//  or UI state, and - the one that is specific to this radio - NO esp_now_*
//  CALL OF ANY KIND, because esp_now_add_peer() or esp_now_send() from inside
//  the receive callback re-enters the Wi-Fi task's own machinery. An intent is
//  posted and acted on from loop().
//
// -----------------------------------------------------------------------------
//  THE SEND CALLBACK IS A STATISTIC AND NOT A RETURN VALUE
// -----------------------------------------------------------------------------
//  esp_now_send() is asynchronous; the 802.11 link-layer ACK arrives later, in
//  a callback with a different shape (a bare MAC, not the info struct). It is
//  meaningful for UNICAST ONLY - a broadcast reports success unconditionally
//  and carries no information at all.
//
//  IT MUST NOT BE PLUMBED BACK INTO Transport::send()'s bool. transport.h:36-40
//  and transport_loopback.cpp:86-90 both say the session's reaction to a
//  refused send and to a dropped frame is identical, and that is the property
//  which makes an injected loopback drop and a real radio failure exercise the
//  same path. Wiring the ACK back would create a path the loopback cannot
//  exercise. So send() returns true for ESP_OK - "handed to the driver" - and
//  the ACK becomes espnow_stats().tx_ok / tx_fail and espnow_ack_ms(), which is
//  what P7-C2's LINK screen shows as "the peer is still there".
// =============================================================================
#ifndef PB_NETWORKING_TRANSPORT_ESPNOW_H
#define PB_NETWORKING_TRANSPORT_ESPNOW_H

#include <stdint.h>

#include "discovery.h"     // DiscRx, LINK_* - pure, no radio header
#include "transport.h"     // Transport, PROTO_FRAME_MAX - pure

// -----------------------------------------------------------------------------
//  WHAT THE DIAG SCREEN SHOWS, AND THE HONEST SENTENCE BESIDE IT
//
//  A session frame lost to ring overflow is INDISTINGUISHABLE to session.cpp
//  from a frame the radio dropped - which is transport.h:36-40's whole point,
//  and therefore the reason a counter is the only way anyone will ever know it
//  happened. rx_ring_overflow being nonzero after a battle is the evidence that
//  LINK_RX_SLOTS is too small; nothing else in the system can report it.
// -----------------------------------------------------------------------------
struct EspNowStats {
  uint32_t rx_session;        // unicast frames accepted from the bound peer
  uint32_t rx_beacon;         // broadcasts that passed the length/magic prefilter
  uint32_t rx_wrong_peer;     // unicast from somebody we are not talking to
  uint32_t rx_malformed;      // len <= 0, or longer than one frame can be
  uint32_t rx_ring_overflow;  // session ring was full
  uint32_t rx_beacon_overflow;// beacon ring was full
  uint32_t tx_ok;             // unicast sends the peer acknowledged
  uint32_t tx_fail;           // unicast sends the peer did not acknowledge
  uint32_t tx_refused;        // esp_now_send() returned something but ESP_OK
  uint32_t tx_no_mem;         // ...and specifically ESP_ERR_ESPNOW_NO_MEM
  uint32_t beacons_tx;        // broadcasts handed to the driver
};

// Bring ESP-NOW up on a station that networking/net.cpp has ALREADY started and
// put on PB_LINK_CHANNEL. Registers both callbacks and adds the broadcast peer
// (which is an ordinary peer entry with a FF:FF:FF:FF:FF:FF address; a send to
// an address with no peer entry is ESP_ERR_ESPNOW_NOT_FOUND). Idempotent.
//
// ORDERING IS NOT NEGOTIABLE: esp_wifi_start() first, esp_now_init() second.
// net.cpp is the only module allowed to do the first half, so this function is
// called from there and from nowhere else.
bool espnow_begin(void);

// Unregister, drop every peer, deinit, and empty both rings. Idempotent, and
// safe to call when ESP-NOW was never up. Reached from net.cpp's wifi_down(),
// which is the ONE place every teardown passes through - so ESP-NOW cannot
// still be initialised over a Wi-Fi driver that has been stopped, whatever
// route took the radio down.
void espnow_end(void);

bool espnow_is_up(void);

// One broadcast beacon. Returns false when ESP-NOW is down or the driver
// refused; a broadcast has no ACK, so a true here means "handed to the driver"
// and nothing more.
bool espnow_broadcast(const uint8_t* frame, uint16_t n);

// Drain ONE received beacon. Returns 1 when `out` was filled, 0 when the ring
// is empty. THIS IS WHERE A MAC BECOMES A SLOT: the address the callback copied
// is looked up in (or added to) the private slot table here, on the loop task,
// and only the index leaves.
uint8_t espnow_beacon_pop(DiscRx& out);

// Open a unicast peer on the device that owns `slot` (a DiscPeer::slot handed
// out by the call above). Adds the ESP-NOW peer entry, so the table holds two
// entries at a time - broadcast plus the bound peer - and never a mirror of the
// discovery table. From loop() only, never from a callback.
bool espnow_bind(uint8_t slot);
void espnow_unbind(void);
bool espnow_bound(void);

// The Transport the session runs over. `port` is caller-owned for the same
// reason LoopbackPort is, even though this driver can only ever have one:
// the seam's ctx is the field that removes the need to widen transport.h,
// and a factory that returned a pointer to its own static would be a second,
// quieter singleton.
struct EspNowPort { uint8_t endpoint; };
Transport transport_espnow(EspNowPort& port);

const EspNowStats& espnow_stats(void);

// millis() at the last unicast send-callback ACK, and whether one has ever
// arrived. This is the "ACK after HELLO" evidence: after the first unicast the
// LINK screen can say whether anything at all is listening, which the session's
// nine-second ladder cannot tell it any sooner.
uint32_t espnow_ack_ms(void);
bool     espnow_ever_acked(void);

#endif  // PB_NETWORKING_TRANSPORT_ESPNOW_H
