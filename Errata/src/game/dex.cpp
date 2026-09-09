// =============================================================================
//  ERRATA - game/dex.cpp
//  See dex.h. PURE translation unit.
// =============================================================================
#include "dex.h"

#include <string.h>

static uint8_t* s_bytes = nullptr;

// The two bits of one species. id 1 is bits 0..1, id 2 is bits 2..3, and so on:
// the id is 1-based so the shift is (id - 1), and getting that wrong wastes two
// bits at the front and drops the last species off the end.
static inline bool slot_of(uint8_t species_id, uint16_t& byte, uint8_t& shift)
{
  if (species_id == 0u || species_id > (uint8_t)SPECIES_TABLE_COUNT) return false;
  const uint16_t bit = (uint16_t)((species_id - 1u) * DEX_BITS_PER_SPECIES);
  byte  = (uint16_t)(bit >> 3);
  shift = (uint8_t)(bit & 7u);
  return byte < (uint16_t)DEX_BYTES;
}

#define DEX_B_SEEN    0x1u
#define DEX_B_CAUGHT  0x2u

void dex_bind(uint8_t* bytes) { s_bytes = bytes; }
void dex_unbind(void)         { s_bytes = nullptr; }

void dex_reset(void)
{
  if (s_bytes != nullptr) memset(s_bytes, 0, (size_t)DEX_BYTES);
}

static bool set_bits(uint8_t species_id, uint8_t bits)
{
  uint16_t byte = 0; uint8_t shift = 0;
  if (s_bytes == nullptr || !slot_of(species_id, byte, shift)) return false;
  const uint8_t before = s_bytes[byte];
  s_bytes[byte] = (uint8_t)(before | (uint8_t)(bits << shift));
  return s_bytes[byte] != before;
}

static bool get_bit(uint8_t species_id, uint8_t bit)
{
  uint16_t byte = 0; uint8_t shift = 0;
  if (s_bytes == nullptr || !slot_of(species_id, byte, shift)) return false;
  return (s_bytes[byte] & (uint8_t)(bit << shift)) != 0u;
}

bool dex_mark_seen(uint8_t species_id)   { return set_bits(species_id, DEX_B_SEEN); }

bool dex_mark_caught(uint8_t species_id)
{
  // BOTH BITS, ALWAYS. dex.h says why: a caller that sets only CAUGHT leaves a
  // creature you are holding listed as one you have never met.
  return set_bits(species_id, (uint8_t)(DEX_B_CAUGHT | DEX_B_SEEN));
}

bool dex_seen(uint8_t species_id)   { return get_bit(species_id, DEX_B_SEEN); }
bool dex_caught(uint8_t species_id) { return get_bit(species_id, DEX_B_CAUGHT); }

static uint8_t count(uint8_t bit)
{
  uint8_t n = 0;
  for (uint8_t id = 1u; id <= (uint8_t)SPECIES_TABLE_COUNT; ++id)
    if (get_bit(id, bit)) ++n;
  return n;
}

uint8_t dex_count_seen(void)   { return count(DEX_B_SEEN); }
uint8_t dex_count_caught(void) { return count(DEX_B_CAUGHT); }
