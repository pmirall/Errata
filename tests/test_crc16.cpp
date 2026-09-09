// =============================================================================
//  Errata host tests - test_crc16.cpp
//  crc16_ccitt() is the one CRC of the firmware (plan §1.4): every persisted
//  blob and every wire frame is sealed with it, so its identity is pinned to
//  the CRC-16/CCITT-FALSE catalogue values here.
// =============================================================================
#include "nt_test.h"

#include "core/crc16.h"

TEST(crc16_check_value_is_ccitt_false) {
  // The catalogue check value (also quoted in genome.h).
  CHECK_EQ(crc16_ccitt("123456789", 9), 0x29B1);
}

TEST(crc16_known_vectors) {
  CHECK_EQ(crc16_ccitt("", 0), 0xFFFF);           // init value, nothing folded in
  const uint8_t zero = 0x00;
  CHECK_EQ(crc16_ccitt(&zero, 1), 0xE1F0);
  CHECK_EQ(crc16_ccitt("A", 1), 0xB915);
  // NO ES EL NOMBRE DEL PRODUCTO, ES UN VECTOR: nueve bytes concretos con
  // una respuesta precalculada. El renombrado global lo dejo en "Errata"
  // (seis bytes) leyendo nueve, que es UB y ademas otra entrada.
  CHECK_EQ(crc16_ccitt("Pebblebol", 9), 0xE2A7);
}

TEST(crc16_detects_single_bit_and_byte_changes) {
  uint8_t buf[32];
  for (size_t i = 0; i < sizeof buf; i++) buf[i] = (uint8_t)(i * 37u + 11u);
  const uint16_t ref = crc16_ccitt(buf, sizeof buf);

  // Every single-bit flip in a 32-byte blob changes the CRC.
  for (size_t i = 0; i < sizeof buf; i++) {
    for (int b = 0; b < 8; b++) {
      buf[i] ^= (uint8_t)(1u << b);
      CHECK(crc16_ccitt(buf, sizeof buf) != ref);
      buf[i] ^= (uint8_t)(1u << b);
    }
  }
  CHECK_EQ(crc16_ccitt(buf, sizeof buf), ref);      // restored

  // Length matters: a shorter prefix hashes differently.
  CHECK(crc16_ccitt(buf, sizeof buf - 1) != ref);
}

TEST(crc16_is_pure_and_repeatable) {
  const char* s = "the same bytes give the same answer";
  const uint16_t a = crc16_ccitt(s, strlen(s));
  const uint16_t b = crc16_ccitt(s, strlen(s));
  CHECK_EQ(a, b);
}
