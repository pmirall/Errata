// =============================================================================
//  PEBBLEBOL - data/network_table.h
//
//  GENERATED FILE. Do not edit: tools/gen_content.py rewrites it from
//  tools/content/*.json. `tools/gen_content.py --check` fails the gate if
//  this file and the JSON have drifted apart.
//
//  THE NETWORK CLASSIFIER'S TABLE (spec sections 20, 40, 44). P5-C1.
//
//  Six abstract categories, the auth modes a passive scan can tell apart,
//  two RSSI bands and a sorted token table. networking/net_classify.cpp is
//  the one consumer; it is a PURE translation unit, so nothing here may
//  name a radio type - networking/net.cpp maps wifi_auth_mode_t onto
//  NetAuth and static_asserts that mapping against the real constants.
//
//  NO NAME AND NO HARDWARE ADDRESS SURVIVES A LOOKUP. The scanner splits a
//  beacon's name into runs of ASCII letters inside its own loop, hashes
//  each run of at least NET_TOKEN_MIN_LEN letters, binary-searches this
//  table and ORs the classes. What leaves that loop is a 3-bit bitmask.
//
//  The category ENCODING lives here rather than in encounter_table.h
//  because it is the classifier's OUTPUT and the encounter table's INDEX,
//  and a value with two owners is a value that drifts. encounter_table.h
//  includes this header and keeps the count cross-check against the
//  species roster, which is the one thing this header cannot see.
// =============================================================================

#ifndef PB_NETWORK_TABLE_H
#define PB_NETWORK_TABLE_H

#include <stdint.h>
#include <stddef.h>

// Network categories (spec section 20). ORDINALS index EncounterRow.category;
// BITS are what SpeciesDef.category_mask holds. Two encodings, one set.
enum NetCategory : uint8_t {
  NET_CAT_UNKNOWN = 0,
  NET_CAT_HOME = 1,
  NET_CAT_PUBLIC = 2,
  NET_CAT_BUSINESS = 3,
  NET_CAT_OPEN = 4,
  NET_CAT_HIDDEN = 5,
  NET_CAT_COUNT
};

inline constexpr uint8_t NET_CATEGORY_BIT[NET_CAT_COUNT] = {
  1, 2, 4, 8, 16, 32   // UNKNOWN HOME PUBLIC BUSINESS OPEN HIDDEN
};

// What a beacon says about its own security, as the classifier sees it.
// A PROJECT enum: net_classify.cpp may not include a radio header, so
// net.cpp owns the one mapping from wifi_auth_mode_t and asserts it.
// NAUTH_OTHER is LAST on purpose - it is the sink every constant the
// core gains next lands on, so a new IDF value moves a category and
// never produces an unhandled case.
enum NetAuth : uint8_t {
  NAUTH_OPEN = 0,
  NAUTH_WEP = 1,
  NAUTH_PSK = 2,
  NAUTH_ENTERPRISE = 3,
  NAUTH_OWE = 4,
  NAUTH_OTHER = 5,
  NAUTH_COUNT
};

// Token CLASSES - a bitmask, never text.
#define NTOK_VENUE      1u
#define NTOK_ISP_CPE    2u
#define NTOK_OPERATOR   4u
#define NTOK_ALL         7u
#define NET_TOKEN_MIN_LEN 3u
// The longest token in the table below. net_classify.cpp sizes its
// stack buffer from this, so adding a longer token to networks.json
// widens the buffer instead of silently becoming unmatchable.
#define NET_TOKEN_MAX_LEN 12u

// The two signal bands. Stronger than NEAR is 'you are inside it';
// between NEAR and MID is 'you are next to it'; weaker than MID is a
// distant beacon nothing can be claimed about.
#define NET_RSSI_NEAR  (-55)
#define NET_RSSI_MID   (-75)

struct NetTokenRow {        // 8 B
  uint32_t hash;            // FNV-1a 32 of the lowercase ASCII token
  uint8_t  klass;           // exactly one NTOK_* bit
  uint8_t  reserved[3];     // must be 0
};
static_assert(sizeof(NetTokenRow) == 8, "NetTokenRow layout drifted");

// SORTED BY HASH: net_classify.cpp binary-searches this, and the guard
// below is what makes that search legal.
inline constexpr NetTokenRow NET_TOKEN_TABLE[] = {
  { 0x02D9D6C5u, NTOK_VENUE,   { 0, 0, 0 } },   // invitado
  { 0x03ED4ADEu, NTOK_OPERATOR, { 0, 0, 0 } },   // telekom
  { 0x06CD526Bu, NTOK_ISP_CPE, { 0, 0, 0 } },   // netgear
  { 0x083EAE32u, NTOK_ISP_CPE, { 0, 0, 0 } },   // avatel
  { 0x19E64798u, NTOK_VENUE,   { 0, 0, 0 } },   // visitante
  { 0x1C5A0ED3u, NTOK_VENUE,   { 0, 0, 0 } },   // lobby
  { 0x1D126DD1u, NTOK_OPERATOR, { 0, 0, 0 } },   // attwifi
  { 0x1DB58FE8u, NTOK_VENUE,   { 0, 0, 0 } },   // cine
  { 0x1E27237Eu, NTOK_OPERATOR, { 0, 0, 0 } },   // freewifi
  { 0x21F378B2u, NTOK_ISP_CPE, { 0, 0, 0 } },   // linksys
  { 0x24D0EA4Eu, NTOK_VENUE,   { 0, 0, 0 } },   // eduroam
  { 0x2500AC46u, NTOK_ISP_CPE, { 0, 0, 0 } },   // yoigo
  { 0x2C2D8A80u, NTOK_VENUE,   { 0, 0, 0 } },   // guests
  { 0x2D21BBF3u, NTOK_ISP_CPE, { 0, 0, 0 } },   // technicolor
  { 0x2E0178E7u, NTOK_ISP_CPE, { 0, 0, 0 } },   // pepephone
  { 0x32DED856u, NTOK_ISP_CPE, { 0, 0, 0 } },   // finetwork
  { 0x32ED0082u, NTOK_VENUE,   { 0, 0, 0 } },   // invitados
  { 0x369D8170u, NTOK_ISP_CPE, { 0, 0, 0 } },   // movistar
  { 0x376DE257u, NTOK_OPERATOR, { 0, 0, 0 } },   // spectrumwifi
  { 0x3810BAB5u, NTOK_ISP_CPE, { 0, 0, 0 } },   // mifibra
  { 0x39A30099u, NTOK_OPERATOR, { 0, 0, 0 } },   // xfinitywifi
  { 0x3F2E9A66u, NTOK_ISP_CPE, { 0, 0, 0 } },   // digi
  { 0x41E90D4Du, NTOK_VENUE,   { 0, 0, 0 } },   // cafeteria
  { 0x42FE96E5u, NTOK_ISP_CPE, { 0, 0, 0 } },   // zyxel
  { 0x4436EF92u, NTOK_VENUE,   { 0, 0, 0 } },   // hostal
  { 0x45B473EBu, NTOK_ISP_CPE, { 0, 0, 0 } },   // orange
  { 0x47F0E877u, NTOK_ISP_CPE, { 0, 0, 0 } },   // fritzbox
  { 0x4AA182F5u, NTOK_VENUE,   { 0, 0, 0 } },   // colegio
  { 0x4F9CD7B3u, NTOK_VENUE,   { 0, 0, 0 } },   // hospital
  { 0x54941F2Eu, NTOK_ISP_CPE, { 0, 0, 0 } },   // telecable
  { 0x572B5823u, NTOK_VENUE,   { 0, 0, 0 } },   // escuela
  { 0x5D0C6EB9u, NTOK_ISP_CPE, { 0, 0, 0 } },   // comtrend
  { 0x61172B3Fu, NTOK_VENUE,   { 0, 0, 0 } },   // ayuntamiento
  { 0x642C9496u, NTOK_VENUE,   { 0, 0, 0 } },   // hostel
  { 0x6710AA2Fu, NTOK_OPERATOR, { 0, 0, 0 } },   // optimumwifi
  { 0x6EEF45E9u, NTOK_VENUE,   { 0, 0, 0 } },   // gratis
  { 0x73A697ECu, NTOK_VENUE,   { 0, 0, 0 } },   // museo
  { 0x7A0BF8A9u, NTOK_VENUE,   { 0, 0, 0 } },   // restaurante
  { 0x7A885D16u, NTOK_ISP_CPE, { 0, 0, 0 } },   // rcable
  { 0x7D8C66DDu, NTOK_VENUE,   { 0, 0, 0 } },   // hotel
  { 0x7FBB85B4u, NTOK_VENUE,   { 0, 0, 0 } },   // terraza
  { 0x804515B0u, NTOK_ISP_CPE, { 0, 0, 0 } },   // arris
  { 0x839C50B7u, NTOK_VENUE,   { 0, 0, 0 } },   // estacion
  { 0x83DE1647u, NTOK_ISP_CPE, { 0, 0, 0 } },   // vodafone
  { 0x87F85125u, NTOK_ISP_CPE, { 0, 0, 0 } },   // jazztel
  { 0x8A7BBB50u, NTOK_ISP_CPE, { 0, 0, 0 } },   // livebox
  { 0x922A3880u, NTOK_VENUE,   { 0, 0, 0 } },   // campus
  { 0x95390495u, NTOK_VENUE,   { 0, 0, 0 } },   // recepcion
  { 0x96972CB0u, NTOK_VENUE,   { 0, 0, 0 } },   // airport
  { 0x99B3EEDBu, NTOK_VENUE,   { 0, 0, 0 } },   // free
  { 0x99F40AB1u, NTOK_VENUE,   { 0, 0, 0 } },   // terminal
  { 0x9B5FCE76u, NTOK_OPERATOR, { 0, 0, 0 } },   // fonera
  { 0x9DB3C79Du, NTOK_OPERATOR, { 0, 0, 0 } },   // swisscom
  { 0xA0DBEB49u, NTOK_VENUE,   { 0, 0, 0 } },   // aeropuerto
  { 0xA37F8044u, NTOK_ISP_CPE, { 0, 0, 0 } },   // askey
  { 0xA4A540BCu, NTOK_OPERATOR, { 0, 0, 0 } },   // hotspot
  { 0xA57DAD3Du, NTOK_ISP_CPE, { 0, 0, 0 } },   // sagemcom
  { 0xA82D0F87u, NTOK_ISP_CPE, { 0, 0, 0 } },   // masmovil
  { 0xA8F37D44u, NTOK_OPERATOR, { 0, 0, 0 } },   // fon
  { 0xAC4948F7u, NTOK_ISP_CPE, { 0, 0, 0 } },   // dlink
  { 0xB08336F1u, NTOK_VENUE,   { 0, 0, 0 } },   // visitantes
  { 0xB264CE20u, NTOK_VENUE,   { 0, 0, 0 } },   // teatro
  { 0xB7098E48u, NTOK_VENUE,   { 0, 0, 0 } },   // cafe
  { 0xBE5CDBF3u, NTOK_VENUE,   { 0, 0, 0 } },   // guest
  { 0xC244AFD6u, NTOK_VENUE,   { 0, 0, 0 } },   // instituto
  { 0xC42729ADu, NTOK_ISP_CPE, { 0, 0, 0 } },   // ono
  { 0xC52B4B38u, NTOK_ISP_CPE, { 0, 0, 0 } },   // lowi
  { 0xC720BEF4u, NTOK_ISP_CPE, { 0, 0, 0 } },   // mitrastar
  { 0xCAAA22F7u, NTOK_ISP_CPE, { 0, 0, 0 } },   // adamo
  { 0xCC909380u, NTOK_VENUE,   { 0, 0, 0 } },   // public
  { 0xD1EC9481u, NTOK_OPERATOR, { 0, 0, 0 } },   // fonwifi
  { 0xD424CAFDu, NTOK_ISP_CPE, { 0, 0, 0 } },   // tplink
  { 0xD447715Eu, NTOK_VENUE,   { 0, 0, 0 } },   // apartamentos
  { 0xDE8EC1F9u, NTOK_VENUE,   { 0, 0, 0 } },   // biblioteca
  { 0xDFFFCE2Cu, NTOK_VENUE,   { 0, 0, 0 } },   // clinica
  { 0xE83DC49Cu, NTOK_OPERATOR, { 0, 0, 0 } },   // wifionice
  { 0xE898CB33u, NTOK_VENUE,   { 0, 0, 0 } },   // publica
  { 0xED1002C9u, NTOK_ISP_CPE, { 0, 0, 0 } },   // wlan
  { 0xED5F73D5u, NTOK_VENUE,   { 0, 0, 0 } },   // centrosalud
  { 0xEE2C0690u, NTOK_OPERATOR, { 0, 0, 0 } },   // wifispot
  { 0xF0B08A1Fu, NTOK_VENUE,   { 0, 0, 0 } },   // universidad
  { 0xF1200A47u, NTOK_OPERATOR, { 0, 0, 0 } },   // boingo
  { 0xF25FBD03u, NTOK_VENUE,   { 0, 0, 0 } },   // renfe
  { 0xF519492Fu, NTOK_OPERATOR, { 0, 0, 0 } },   // wayport
  { 0xF64E022Du, NTOK_ISP_CPE, { 0, 0, 0 } },   // euskaltel
  { 0xF698E13Du, NTOK_VENUE,   { 0, 0, 0 } },   // publico
  { 0xFAC23644u, NTOK_VENUE,   { 0, 0, 0 } },   // camping
  { 0xFE44BA12u, NTOK_ISP_CPE, { 0, 0, 0 } },   // parlem
};

inline constexpr uint8_t NET_TOKEN_ROW_COUNT =
    (uint8_t)(sizeof(NET_TOKEN_TABLE) / sizeof(NET_TOKEN_TABLE[0]));

// --- generator-emitted compile-time guards -----------------------------------
constexpr bool net_category_bits_are_one_bit_each_and_distinct(void) {
  uint32_t seen = 0;
  for (uint8_t i = 0; i < (uint8_t)NET_CAT_COUNT; ++i) {
    const uint32_t b = NET_CATEGORY_BIT[i];
    if (b == 0u || (b & (b - 1u)) != 0u) return false;
    if (seen & b)                        return false;
    seen |= b;
  }
  return true;
}

// STRICTLY ascending, which proves sortedness AND uniqueness in one walk. The
// binary search in net_classify.cpp is only correct on a sorted table, and a
// duplicate hash would give one token the other's class depending on where the
// search happened to land.
constexpr bool net_token_table_is_sorted_and_unique(void) {
  for (uint8_t i = 1; i < NET_TOKEN_ROW_COUNT; ++i)
    if (NET_TOKEN_TABLE[i].hash <= NET_TOKEN_TABLE[i - 1].hash) return false;
  return true;
}

constexpr bool net_token_rows_are_well_formed(void) {
  for (uint8_t i = 0; i < NET_TOKEN_ROW_COUNT; ++i) {
    const NetTokenRow& r = NET_TOKEN_TABLE[i];
    if (r.klass == 0u)                          return false;
    if ((r.klass & (uint8_t)(r.klass - 1u)) != 0u) return false;  // one bit
    if ((r.klass & ~(uint8_t)NTOK_ALL) != 0u)   return false;
    if (r.reserved[0] || r.reserved[1] || r.reserved[2]) return false;
  }
  return true;
}

// EVERY CLASS IS REACHABLE. A class no token carries is a branch of the ladder
// no scan can ever take - the shape of dead rule this project keeps finding.
constexpr bool net_every_token_class_is_populated(void) {
  uint8_t seen = 0;
  for (uint8_t i = 0; i < NET_TOKEN_ROW_COUNT; ++i) seen |= NET_TOKEN_TABLE[i].klass;
  return seen == (uint8_t)NTOK_ALL;
}

// The HOME band is [NET_RSSI_NEAR, 0] and the BUSINESS band is
// [NET_RSSI_MID, NET_RSSI_NEAR). Swap the two constants and BUSINESS is empty
// while HOME swallows everything down to the noise floor.
constexpr bool net_rssi_bands_are_ordered(void) {
  return NET_RSSI_MID < NET_RSSI_NEAR &&
         NET_RSSI_MID >= -127 && NET_RSSI_NEAR <= 0;
}

static_assert(NET_TOKEN_ROW_COUNT >= 1, "the network token table is empty");
static_assert(net_category_bits_are_one_bit_each_and_distinct(),
              "two network categories share a category_mask bit");
static_assert(net_token_table_is_sorted_and_unique(),
              "NET_TOKEN_TABLE is not strictly ascending by hash - the binary search is invalid");
static_assert(net_token_rows_are_well_formed(),
              "a network token row has no class, more than one class, or a dirty reserved byte");
static_assert(net_every_token_class_is_populated(),
              "a network token class has no tokens - the classifier branch that reads it is dead");
static_assert(net_rssi_bands_are_ordered(),
              "NET_RSSI_NEAR / NET_RSSI_MID are swapped or out of int8_t range");

#endif // PB_NETWORK_TABLE_H
