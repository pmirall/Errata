// =============================================================================
//  Errata - crc16.cpp
//  CRC-16/CCITT-FALSE, bitwise. ~30 B of code, no table, no state.
// =============================================================================
#include "crc16.h"

#define CRC16_CCITT_INIT 0xFFFFu
#define CRC16_CCITT_POLY 0x1021u

uint16_t crc16_ccitt(const void* p, size_t n) {
  const uint8_t* b = (const uint8_t*)p;
  uint16_t crc = CRC16_CCITT_INIT;
  for (size_t i = 0; i < n; i++) {
    crc = (uint16_t)(crc ^ ((uint16_t)b[i] << 8));
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (uint16_t)((crc & 0x8000u) ? (uint16_t)((uint16_t)(crc << 1) ^ CRC16_CCITT_POLY)
                                       : (uint16_t)(crc << 1));
    }
  }
  return crc;
}
