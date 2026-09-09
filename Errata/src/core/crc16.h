// =============================================================================
//  Errata - crc16.h
//  The one CRC of the firmware (plan §1.4). Pure C++: <stdint.h>/<stddef.h>
//  only, no Arduino header, no state. Compiled on the host by tests/.
//
//  CRC-16/CCITT-FALSE: polynomial 0x1021, init 0xFFFF, MSB-first, no
//  reflection, no final xor. Check value: crc16_ccitt("123456789", 9) == 0x29B1.
//
//  Guards Genome (bytes 0..13), PetSave, Config, GainSave and the BLE frames.
// =============================================================================
#ifndef NT_CRC16_H
#define NT_CRC16_H

#include <stdint.h>
#include <stddef.h>

uint16_t crc16_ccitt(const void* p, size_t n);

#endif  // NT_CRC16_H
