// =============================================================================
//  Pebblebol host tests - test_qr.cpp
//  The portable half of qr.cpp: version selection by byte-mode capacity at
//  ECC L (17/32/53/78 B for v1..v4), failure modes, and two full symbols
//  checked module-by-module against the Python `qrcode` 8.2 reference.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "qr.h"

static uint8_t s_mod[QR_BUF_BYTES];

static bool encode_n(size_t n, uint8_t& size) {
  static char text[128];
  memset(text, 'A', n);
  text[n] = '\0';
  return qr_encode(text, s_mod, size);
}

TEST(qr_version_is_the_smallest_that_fits_ecc_l_byte_mode) {
  uint8_t size = 0;
  CHECK(encode_n(1, size));   CHECK_EQ(size, 21);      // v1
  CHECK(encode_n(17, size));  CHECK_EQ(size, 21);      // v1 max = 17 B
  CHECK(encode_n(18, size));  CHECK_EQ(size, 25);      // v2
  CHECK(encode_n(32, size));  CHECK_EQ(size, 25);      // v2 max = 32 B
  CHECK(encode_n(33, size));  CHECK_EQ(size, 29);      // v3
  CHECK(encode_n(53, size));  CHECK_EQ(size, 29);      // v3 max = 53 B
  CHECK(encode_n(54, size));  CHECK_EQ(size, 33);      // v4
  CHECK(encode_n(78, size));  CHECK_EQ(size, 33);      // v4 max = 78 B
}

TEST(qr_rejects_oversize_empty_and_null_leaving_the_buffer_alone) {
  memset(s_mod, 0xAA, sizeof s_mod);
  uint8_t size = 99;
  CHECK(!encode_n(79, size));
  CHECK_EQ(size, 0);
  for (size_t i = 0; i < sizeof s_mod; i++) CHECK(s_mod[i] == 0xAA);

  size = 99;
  CHECK(!qr_encode("", s_mod, size));
  CHECK_EQ(size, 0);
  size = 99;
  CHECK(!qr_encode(nullptr, s_mod, size));
  CHECK_EQ(size, 0);
  size = 99;
  CHECK(!qr_encode("x", nullptr, size));
  CHECK_EQ(size, 0);
  for (size_t i = 0; i < sizeof s_mod; i++) CHECK(s_mod[i] == 0xAA);
}

TEST(qr_unused_rows_and_columns_are_zero) {
  uint8_t size = 0;
  CHECK(encode_n(5, size));
  CHECK_EQ(size, 21);
  for (int r = 0; r < QR_MAX_MODULES; r++) {
    for (int c = 0; c < QR_STRIDE_BYTES * 8; c++) {
      if (r >= size || c >= size) CHECK_EQ(QR_MODULE_AT(s_mod, r, c), 0);
    }
  }
}

// Compares the encoded symbol with an ASCII reference ('#' dark, '.' light).
static void check_against(const char* text, const char* const* ref, int n) {
  uint8_t size = 0;
  CHECK(qr_encode(text, s_mod, size));
  CHECK_EQ(size, n);
  if (size != n) return;
  int bad = 0;
  for (int r = 0; r < n; r++) {
    for (int c = 0; c < n; c++) {
      const int want = (ref[r][c] == '#') ? 1 : 0;
      if ((int)QR_MODULE_AT(s_mod, r, c) != want) bad++;
    }
  }
  CHECK_EQ(bad, 0);
}

// Reference symbols from Python qrcode 8.2 (ERROR_CORRECT_L, byte mode forced
// with QRData(..., mode=MODE_8BIT_BYTE), border 0), rendered with the mask
// qr.cpp selects for each payload (v1: mask 7, v2: mask 2). qrcode's automatic
// mask choice differs (0 and 5) because its penalty scoring is not the ISO
// 18004 rule set qr.cpp implements; every mask yields a decodable symbol, so
// pinning the mask makes the vector prove data encoding, Reed-Solomon, module
// placement, masking and the format information bit-for-bit.
static const char* const REF_V1[21] = {
  "#######..#.##.#######",
  "#.....#.##.#..#.....#",
  "#.###.#.##..#.#.###.#",
  "#.###.#..#.#..#.###.#",
  "#.###.#.#...#.#.###.#",
  "#.....#.#..##.#.....#",
  "#######.#.#.#.#######",
  "........#####........",
  "##.#..##.##...###.##.",
  "###.##.##.....##...#.",
  "#####.#####.#..#.##.#",
  "##...#.#...#..####.##",
  ".#..#.##.##.#.#.#.#.#",
  "........##.#.#####.#.",
  "#######.#......###.#.",
  "#.....#..#.###.......",
  "#.###.#..###.###..##.",
  "#.###.#.#..#...#...##",
  "#.###.#..##.#..##...#",
  "#.....#.#.#..####....",
  "#######.#####..#.###.",
};

static const char* const REF_V2[25] = {
  "#######..#...#....#######",
  "#.....#.##.#####..#.....#",
  "#.###.#..###...##.#.###.#",
  "#.###.#.##.#.##...#.###.#",
  "#.###.#...#.##..#.#.###.#",
  "#.....#.#.#...###.#.....#",
  "#######.#.#.#.#.#.#######",
  "............#...#........",
  "#####.#####.###..#.#.#.#.",
  "#.#.##..##...#..#..#...#.",
  "..#...#..#.#####.##..#.##",
  "##..##.#####..#..##.....#",
  ".#.#####.#.#.###.####.###",
  "##.#.#...##.#...#..#.#.#.",
  "#..#..#..##..#.##.####.##",
  "#.#..#.#....#.###..##...#",
  "#.###.####..#########.#..",
  "........##.....##...##...",
  "#######.#####...#.#.#.###",
  "#.....#..###..#.#...##..#",
  "#.###.#.#..##########.#..",
  "#.###.#.#.#.##.#.##.#####",
  "#.###.#.##....#.#....##.#",
  "#.....#.#.##...##.####..#",
  "#######.#.####.....######",
};

TEST(qr_v1_symbol_matches_the_python_reference) {
  check_against("PEBBLEBOL", REF_V1, 21);
}

TEST(qr_v2_symbol_matches_the_python_reference) {
  check_against("https://pebblebol.local/", REF_V2, 25);
}

TEST(qr_symbol_has_finder_and_timing_patterns) {
  uint8_t size = 0;
  CHECK(encode_n(40, size));                            // v3, 29 modules
  CHECK_EQ(size, 29);
  const int n = size;
  // Finder pattern outer ring (7x7) at the three corners.
  static const int corners[3][2] = { {0, 0}, {0, 22}, {22, 0} };
  for (int k = 0; k < 3; k++) {
    const int r0 = corners[k][0], c0 = corners[k][1];
    for (int i = 0; i < 7; i++) {
      CHECK_EQ(QR_MODULE_AT(s_mod, r0, c0 + i), 1);
      CHECK_EQ(QR_MODULE_AT(s_mod, r0 + 6, c0 + i), 1);
      CHECK_EQ(QR_MODULE_AT(s_mod, r0 + i, c0), 1);
      CHECK_EQ(QR_MODULE_AT(s_mod, r0 + i, c0 + 6), 1);
    }
    CHECK_EQ(QR_MODULE_AT(s_mod, r0 + 3, c0 + 3), 1);   // centre
    CHECK_EQ(QR_MODULE_AT(s_mod, r0 + 1, c0 + 1), 0);   // inner light ring
  }
  // Timing patterns alternate along row 6 and column 6.
  for (int i = 8; i < n - 8; i++) {
    CHECK_EQ(QR_MODULE_AT(s_mod, 6, i), (i % 2 == 0) ? 1 : 0);
    CHECK_EQ(QR_MODULE_AT(s_mod, i, 6), (i % 2 == 0) ? 1 : 0);
  }
  // The dark module at (4V + 9, 8).
  CHECK_EQ(QR_MODULE_AT(s_mod, 4 * 3 + 9, 8), 1);
}
