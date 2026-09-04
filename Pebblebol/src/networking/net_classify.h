// =============================================================================
//  PEBBLEBOL - networking/net_classify.h
//  THE NETWORK CLASSIFIER (spec sections 20, 40, 44). P5-C1.
//
//  What a passive Wi-Fi scan can see about a beacon, turned into one of the six
//  abstract NetCategory values the encounter table is indexed by - plus the two
//  pure helpers that destroy the identifying information on the way.
//
//  PURE TRANSLATION UNIT. No Arduino, no WiFi.h, no esp_wifi, no file-scope
//  mutable state, no heap, no float. That is not tidiness: net.cpp is a device
//  module tests/Makefile never compiles, so a classifier written there could
//  not be driven by a host test at all, and "a category is what the whole
//  encounter table is indexed by" would rest on nothing. Everything here runs
//  on the host with no radio, driven by tests/test_exploration_hash.cpp.
//
//  THE PRIVACY RULE, STATED HERE BECAUSE THIS IS THE FILE THAT HANDLES THE RAW
//  MATERIAL (spec 44). A beacon's SSID and its BSSID enter these functions and
//  do not leave them:
//    * net_tokens_of() reads a name and returns a 3-bit class bitmask;
//    * net_hash_from_bssid() reads six bytes of hardware address and returns a
//      salted 32-bit hash.
//  Neither is stored, neither is a member of any struct, and networking/
//  wifi_scanner.h - which owns the struct results are handed out in - names
//  neither concept. tools/check.sh greps that header for both words.
//
//  All identifiers and comments are English; nothing here is user-facing.
// =============================================================================
#ifndef PB_NET_CLASSIFY_H
#define PB_NET_CLASSIFY_H

#include <stdint.h>
#include <stddef.h>

#include "../data/network_table.h"   // NetCategory, NetAuth, NTOK_*, NET_RSSI_*

// -----------------------------------------------------------------------------
// The classifier's WHOLE input. Built inside the scanner's per-access-point
// loop, passed by value to net_classify(), and destroyed with the loop
// iteration: it is never persisted, never put on the wire and never a member of
// a result.
//
// Four facts, and the reason each one is here rather than something richer:
//   auth    - the beacon says this about itself. The only high-confidence
//             signal that needs no interpretation apart from `hidden`.
//   hidden  - the beacon carried no name at all. Checked FIRST in the ladder,
//             because a hidden network still reports an auth mode and would
//             otherwise be classified by it.
//   rssi    - clamped to [-127, 0]. The clamp is load-bearing, not decoration:
//             the IDF's own header says RSSI "can be slightly positive" for a
//             very strong signal, and an unclamped positive value would satisfy
//             every >= in the ladder at once.
//   tokens  - NTOK_* bitmask from the name, computed by net_tokens_of() inside
//             the same loop. A bitmask of CLASSES, never text.
// -----------------------------------------------------------------------------
struct NetFacts {
  uint8_t auth;      // NetAuth
  uint8_t hidden;    // 1 when the beacon carried no name
  int8_t  rssi;      // dBm, clamped to [-127, 0]
  uint8_t tokens;    // NTOK_* bitmask
};
static_assert(sizeof(NetFacts) == 4, "NetFacts layout drifted");

// -----------------------------------------------------------------------------
// net_classify(f) -> NetCategory ordinal, always < NET_CAT_COUNT
//
// TOTAL AND DETERMINISTIC: no clock, no RNG, no state. First match wins.
//
//   1. hidden                        -> HIDDEN
//   2. ENTERPRISE                    -> BUSINESS      (EAP is not residential)
//   3. OPEN or OWE:
//        VENUE or OPERATOR token     -> PUBLIC
//        otherwise                   -> OPEN
//   4. WEP or PSK:
//        VENUE token                 -> PUBLIC        (a hotel's locked network)
//        ISP_CPE token and >= MID    -> HOME          (a residential router)
//        rssi >= NET_RSSI_NEAR       -> HOME          (you are inside it)
//        rssi >= NET_RSSI_MID        -> BUSINESS      (you are next to it)
//        otherwise                   -> UNKNOWN
//   5. anything else                 -> UNKNOWN
//
// Rule 5 is the switch's `default` arm, not a value some specific input
// reaches: wifi_auth_mode_t gained WPA3_EXT_PSK and DPP in the installed core
// and will gain more, and every one of them arrives here as NAUTH_OTHER.
// UNKNOWN is the right sink - it is spec section 20's own first example, it has
// the largest species pool on the roster and it already has six encounter rows.
// -----------------------------------------------------------------------------
uint8_t net_classify(const NetFacts& f);

// -----------------------------------------------------------------------------
// net_tokens_of(name, len) -> NTOK_* bitmask
//
// Splits `name` into maximal runs of ASCII letters, lowercases each run into a
// small stack buffer, hashes runs of at least NET_TOKEN_MIN_LEN letters with
// FNV-1a 32, binary-searches NET_TOKEN_TABLE and ORs the classes. Every byte
// that is not an ASCII letter is a delimiter, so a UTF-8 name simply splits at
// its non-ASCII bytes rather than being decoded.
//
// WHOLE-TOKEN AND DELIBERATELY CONSERVATIVE: "MOVISTAR_1234" and "vodafone9F4"
// both match, "Telefonica" does NOT match "fon". It under-matches rather than
// over-matches, which is the safe direction - an unmatched name falls through
// to the signal-strength rules instead of being asserted to be something.
//
// `name` is read and not retained. len is a byte count; a NUL inside it ends
// the walk. Returns 0 for a null or empty name.
// -----------------------------------------------------------------------------
uint8_t net_tokens_of(const char *name, size_t len);

// -----------------------------------------------------------------------------
// THE SALTED IDENTITY (spec 44). Two steps, both pure.
//
// net_scan_salt(device_id) = FNV-1a 32 of "pbl-scan" followed by device_id in
//   little-endian. WHY NOT device_id RAW: gs_device_id() goes on the wire in
//   ProtoHello, so salting with it directly would let anyone holding a device
//   id test a candidate hardware address against a stored net_hash. One FNV
//   pass costs nothing and removes the question.
//
// net_hash_from_bssid(bssid, salt) = FNV-1a 32 of the six address bytes
//   followed by the salt in little-endian, with 0 folded to 'PBLN'.
//
//   WHY 0 IS FOLDED AWAY: CooldownRow.net_hash == 0 means "empty row"
//   (persistence/save_schema.h). A network that hashed to 0 would be
//   permanently off cooldown - infinite farming, on exactly one network, and
//   invisible in the field. The tree already has this convention twice
//   (migrated_id(), device_id generation).
//
//   WHY THE ADDRESS AND NOT THE ADDRESS PLUS THE NAME, which is a deliberate
//   departure from the plan bullet's wording: a hidden network reports an empty
//   name on one scan and may reveal it on the next, and a router that is
//   renamed keeps its address - so folding the name in would give the SAME
//   access point TWO identities and silently reset its cooldown. 48 bits is
//   already more entropy than a 32-bit output can use, so the name adds
//   instability and no information. The cost, stated plainly: an access point
//   that rotates its address (a phone hotspot with address randomisation) reads
//   as a new network on each scan and no cooldown can close that. No observable
//   identity fixes it.
//
//   COLLISIONS: 32 rows of 32-bit hashes collide with probability about
//   1.2e-7. A collision reads as one network wrongly on cooldown - a NOTHING
//   outcome, not corruption.
//
//   A FACTORY WIPE REGENERATES device_id, so every armed cooldown goes stale at
//   once. That is correct - a wiped device is a new device - and it is written
//   down here because it looks like a bug when nobody has said it.
// -----------------------------------------------------------------------------
#define NET_HASH_NEVER_ZERO 0x50424C4Eu   // 'PBLN', the folded-away zero

uint32_t net_scan_salt(uint32_t device_id);
uint32_t net_hash_from_bssid(const uint8_t bssid[6], uint32_t salt);

// The FNV-1a 32 this module and tools/gen_content.py both compute. Exposed so a
// host test can pin one against the other: if the generator and the firmware
// ever disagree, every token lookup misses silently and every network on earth
// classifies by signal strength alone.
uint32_t net_fnv1a32(const void *data, size_t len);

// [-127, 0]. The one clamp, so the scanner and the classifier cannot disagree.
int8_t   net_rssi_clamp(int32_t rssi);

#endif  // PB_NET_CLASSIFY_H
