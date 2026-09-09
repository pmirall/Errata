// =============================================================================
//  Errata host tests - fakes/link_fake.h
//  THE RADIO ui/screen_link.cpp REACHES THROUGH ui.h, AND NOTHING ELSE.
//
//  networking/net.cpp is a device module tests/Makefile never compiles, so the
//  six ui.h seams the LINK screen uses - a LinkRadioDriver, a Transport, the
//  unicast bind, the unbind, a nonce and the pet name that goes in the beacon -
//  are filled in here. Two binaries share it (tests/test_screens.cpp and
//  tests/test_link_screen.cpp) so the fake radio has ONE definition and cannot
//  drift between them.
//
//  WHAT IS FAKE AND WHAT IS NOT, stated so a green run is not over-read: the
//  RADIO is fake (a struct of function pointers and a queue of beacons the test
//  writes) and the pet name is fake. The BEACON CODEC, the peer table, the
//  session FSM, the lockstep, the battle engine and the validator are all the
//  real modules. Nothing here models ESP-NOW, the Wi-Fi task, a real channel or
//  a real duplicate; a green run is evidence about the SCREEN and the SESSION,
//  never about the radio (networking/transport.h says the same of the loopback).
// =============================================================================
#ifndef ER_TESTS_LINK_FAKE_H
#define ER_TESTS_LINK_FAKE_H

#include <stdint.h>

struct Transport;

// Everything back to its power-on state, including the queued beacons.
void lf_reset(void);

// Whether the driver's start() succeeds, and whether poll() answers
// LINK_POLL_FAILED (the "the radio was taken away" third answer).
void lf_set_start_ok(bool ok);
void lf_set_poll_fail(bool fail);

// ...AND THE OTHER TWO DRIVER CALLS, added at the FINAL REVIEW because their
// absence made two documented refusals DEAD CODE in all 59 binaries.
//
// lf_set_bind_ok(false)   : ui_link_bind() answers false, which is what
//   networking/transport_espnow.cpp's espnow_bind() does on !s_up, on
//   slot >= LINK_PEER_CAP, when the ESP-NOW slot table has no address for that
//   handle, and when esp_now_add_peer() fails. ui/screen_link.cpp's
//   STR_LK_RADIO_ERR arm for a failed consent had never executed: deleting the
//   whole arm survived 122,581 checks across the three binaries that link
//   screen_link.o.
// lf_set_beacon_ok(false) : the driver's beacon() answers false, which
//   espnow_broadcast() does on !s_up, on an oversize frame, and on any
//   esp_now_send() error. networking/discovery.cpp handles that by NOT
//   incrementing beacons_tx and doing nothing else, BY DESIGN - so at the job
//   level a radio refusing every broadcast is bit-for-bit indistinguishable
//   from an empty room, and beacons_tx is read by no ui/ file at all. That is
//   the P10 invisible-device outcome reached from the transmit side.
void lf_set_bind_ok(bool ok);
void lf_set_beacon_ok(bool ok);

// Queue one beacon for the next poll()s to hand up. `caps` is a DISC_CAP_* mask
// and `slot` is the transport's opaque handle - the fake is what invents it,
// exactly as networking/transport_espnow.cpp does on the device.
void lf_push_beacon(uint32_t device_id, const char* name, uint16_t caps,
                    int8_t rssi, uint8_t slot);
// The same beacon `n` times, which is how a peer crosses LINK_PEER_HITS_MIN.
void lf_push_beacons(uint32_t device_id, const char* name, uint16_t caps,
                     int8_t rssi, uint8_t slot, uint8_t n);
// A datagram that is NOT a beacon: `len` bytes of `byte`. For the "somebody
// else's traffic" path.
void lf_push_raw(const uint8_t* bytes, uint16_t len, int8_t rssi, uint8_t slot);

// What the screen did with the radio.
int     lf_starts(void);
int     lf_stops(void);
int     lf_beacons_tx(void);      // broadcasts handed to the driver
int     lf_binds(void);
int     lf_unbinds(void);
uint8_t lf_bound_slot(void);      // 0xFF when nothing is bound
bool    lf_bound(void);

// What ui_link_transport() hands the screen. NULL until a test binds one, and
// a screen given a NULL transport gets one whose send() and recv() refuse -
// which is a defined refusal and not a crash.
void lf_bind_transport(const Transport* t);

// What ui_link_nonce() answers and what ui_pet_name() writes.
void lf_set_nonce(uint32_t n);
void lf_set_pet_name(const char* name);

// -----------------------------------------------------------------------------
//  THE TRADE'S TWO ui.h SEAMS (P7-C4), AND WHAT IS REAL IN THEM
//
//  The HOOKS are the real ones in shape and in policy: `judge` runs the real
//  game/trade.cpp's trade_accept_check() over the real Box, and `commit` runs
//  the real trade_execute() - the same W3/B1/B2/B3/W4 order the device runs -
//  over a TradeStore whose writes always succeed and reach no flash. What is
//  FAKE is the flash, and only the flash: neither binary that uses this fake
//  links persistence/save_manager.cpp or tests/fakes/kv_mem.cpp, and the
//  atomicity of those five writes is tests/test_trade.cpp's subject, not this
//  one's. Here the subject is the SCREEN: that it offers the right Bug,
//  refuses the ones the rules refuse, asks the player before anything moves,
//  and that the Box really swaps when both players say yes.
// -----------------------------------------------------------------------------
void     lf_trade_reset(void);               // also run by lf_reset()
void     lf_set_quarantine(uint16_t mask);   // what ui_trade_quarantine() answers
uint8_t  lf_trade_phase(void);               // the journal phase the fake holds
int      lf_trade_commits(void);             // times the commit hook ran and won
int      lf_trade_aborts(void);
uint32_t lf_trade_out_id(void);              // what W1 recorded

#endif  // ER_TESTS_LINK_FAKE_H
