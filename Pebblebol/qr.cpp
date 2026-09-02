// =============================================================================
//  NOTTAMAGOCHI - qr.cpp
//  QR Code encoder (v1-v4, ECC L, byte mode) + OLED renderer.
//
//  PART 1 (lines below, up to the "#ifdef ARDUINO" fence) is PORTABLE C++:
//         <stdint.h>/<string.h> only, zero floating point, no dynamic memory.
//         It is compiled on the host by tests/test_qr.cpp and diffed
//         module-by-module against Python `qrcode` 8.x.
//  PART 2 (after the fence) is the U8g2 renderer and only exists on-target.
//
//  Algorithm source: research/QR_SPEC.md (validated 96/96 against qrcode 8.2).
//  Everything here is (row, col), 0-indexed, origin top-left. n = 4*V + 17.
// =============================================================================
#include "qr.h"

#include <string.h>

// =============================================================================
//  PART 1 - PORTABLE ENCODER
// =============================================================================

// -----------------------------------------------------------------------------
// GF(256), primitive polynomial 0x11D, alpha = 2 (the QR convention).
// Built at COMPILE time into a constexpr object so both tables land in .rodata
// (flash on the C3) and cost zero SRAM. 768 B of flash.
// -----------------------------------------------------------------------------
namespace {

struct QrGfTables {
  uint8_t gexp[512];
  uint8_t glog[256];

  constexpr QrGfTables() : gexp(), glog() {
    int x = 1;
    for (int i = 0; i < 255; i++) {
      gexp[i] = (uint8_t)x;
      glog[x] = (uint8_t)i;
      x <<= 1;
      if (x & 0x100) x ^= QR_GF_POLY;
    }
    // Doubled exp table: gf_mul can add two logs (max 254 + 254) with no % 255.
    for (int i = 255; i < 512; i++) gexp[i] = gexp[i - 255];
  }
};

constexpr QrGfTables QR_GF = QrGfTables();

// Compile-time smoke tests against the values verified in QR_SPEC.md 4.
static_assert(QR_GF.gexp[0] == 1 && QR_GF.gexp[8] == 29 && QR_GF.gexp[15] == 38, "GF exp table");
static_assert(QR_GF.glog[1] == 0 && QR_GF.glog[3] == 25 && QR_GF.glog[16] == 4, "GF log table");
static_assert(QR_GF.gexp[300] == QR_GF.gexp[45], "GF exp table must wrap at 255");

inline uint8_t gf_mul(uint8_t a, uint8_t b) {
  if (a == 0 || b == 0) return 0;
  return QR_GF.gexp[(int)QR_GF.glog[a] + (int)QR_GF.glog[b]];
}

// -----------------------------------------------------------------------------
// Version / block structure table, ECC level L only (QR_SPEC.md 3).
// No v1-v4 L symbol uses group 2, but the general interleaver is written anyway.
// -----------------------------------------------------------------------------
struct QrVerInfo {
  uint8_t size;         // modules per side
  uint8_t total_cw;     // total codewords (data + EC)
  uint8_t data_cw;      // data codewords
  uint8_t ec_per_blk;   // EC codewords per block
  uint8_t g1_blocks;    // group 1 block count
  uint8_t g1_data;      // data codewords per group 1 block
  uint8_t g2_blocks;    // group 2 block count
  uint8_t g2_data;      // data codewords per group 2 block
  uint8_t align_c;      // second alignment centre (first is always 6); 0 = none
};

const QrVerInfo QR_VER[4] = {
    // size total data ec  g1n g1d  g2n g2d  align
    {21, 26, 19, 7, 1, 19, 0, 0, 0},   // v1-L : 17 payload bytes
    {25, 44, 34, 10, 1, 34, 0, 0, 18}, // v2-L : 32 payload bytes
    {29, 70, 55, 15, 1, 55, 0, 0, 22}, // v3-L : 53 payload bytes
    {33, 100, 80, 20, 1, 80, 0, 0, 26} // v4-L : 78 payload bytes
};

enum {
  QR_MAX_TOTAL_CW = 100,  // v4
  QR_MAX_DATA_CW = 80,    // v4
  QR_MAX_EC_CW = 20,      // v4
  QR_MAX_BLOCKS = 4       // headroom for the general interleaver
};

// Format information, ECC level L (eccBits = 01), masks 0..7 (QR_SPEC.md 8).
const uint16_t QR_FORMAT_L[8] = {0x77C4, 0x72F3, 0x7DAA, 0x789D,
                                 0x662F, 0x6318, 0x6C41, 0x6976};

// -----------------------------------------------------------------------------
// Bit-packed matrix accessors. Row stride is fixed at QR_STRIDE_BYTES for every
// version so a v1 symbol and a v4 symbol share one buffer layout.
// -----------------------------------------------------------------------------
inline int mod_get(const uint8_t *m, int r, int c) {
  return (m[r * QR_STRIDE_BYTES + (c >> 3)] >> (c & 7)) & 1;
}

inline void mod_set(uint8_t *m, int r, int c, int v) {
  uint8_t *p = &m[r * QR_STRIDE_BYTES + (c >> 3)];
  const uint8_t bit = (uint8_t)(1u << (c & 7));
  if (v) {
    *p = (uint8_t)(*p | bit);
  } else {
    *p = (uint8_t)(*p & (uint8_t)~bit);
  }
}

// -----------------------------------------------------------------------------
// Reed-Solomon
// -----------------------------------------------------------------------------

// g(x) = prod (x - alpha^i), i = 0..n-1. Highest degree first, g[0] == 1.
void gen_poly(int n, uint8_t *g) {
  memset(g, 0, (size_t)n + 1);
  g[0] = 1;
  int len = 1;
  for (int i = 0; i < n; i++) {
    g[len] = 0;
    for (int j = len; j > 0; j--)  // walk DOWN, in place
      g[j] ^= gf_mul(g[j - 1], QR_GF.gexp[i]);
    len++;
  }
}

// Remainder of data(x) * x^nec divided by g(x) -> the nec EC codewords.
void rs_encode(const uint8_t *data, int dlen, int nec, uint8_t *ec) {
  uint8_t g[QR_MAX_EC_CW + 1];
  gen_poly(nec, g);
  memset(ec, 0, (size_t)nec);
  for (int i = 0; i < dlen; i++) {
    const uint8_t factor = (uint8_t)(data[i] ^ ec[0]);
    memmove(ec, ec + 1, (size_t)nec - 1);
    ec[nec - 1] = 0;
    if (factor) {
      for (int j = 0; j < nec; j++) ec[j] ^= gf_mul(g[j + 1], factor);
    }
  }
}

// -----------------------------------------------------------------------------
// Bit stream assembly + block interleaving (QR_SPEC.md 2 and 3).
// Writes exactly vi.total_cw bytes into out[].
// -----------------------------------------------------------------------------
void build_stream(const uint8_t *payload, int len, const QrVerInfo &vi, uint8_t *out) {
  uint8_t dcw[QR_MAX_DATA_CW];
  memset(dcw, 0, sizeof(dcw));

  int bitpos = 0;
  // Header: mode indicator 0100 (byte mode) + 8-bit character count (v1-v9).
  // Written MSB-first, like every bit in the stream.
  const uint32_t header = (uint32_t)((0x4u << 8) | (uint32_t)(len & 0xFF));
  for (int k = 11; k >= 0; k--) {
    if ((header >> k) & 1u) dcw[bitpos >> 3] |= (uint8_t)(0x80u >> (bitpos & 7));
    bitpos++;
  }
  for (int i = 0; i < len; i++) {
    const uint32_t v = payload[i];
    for (int k = 7; k >= 0; k--) {
      if ((v >> k) & 1u) dcw[bitpos >> 3] |= (uint8_t)(0x80u >> (bitpos & 7));
      bitpos++;
    }
  }

  // Terminator: up to 4 zero bits, fewer if the stream is already near capacity.
  const int cap_bits = (int)vi.data_cw * 8;
  int term = cap_bits - bitpos;
  if (term > 4) term = 4;
  bitpos += term;  // dcw is zero-initialised, so the bits are already 0

  // Pad to the next byte boundary, then alternate 0xEC / 0x11 (0xEC first).
  bitpos = (bitpos + 7) & ~7;
  int idx = bitpos >> 3;
  for (int k = 0; idx < (int)vi.data_cw; k++) dcw[idx++] = (k & 1) ? 0x11 : 0xEC;

  // Split into blocks, compute EC per block.
  uint8_t blk_off[QR_MAX_BLOCKS];
  uint8_t blk_len[QR_MAX_BLOCKS];
  uint8_t ec[QR_MAX_BLOCKS][QR_MAX_EC_CW];
  int nblocks = 0;
  int p = 0;
  for (int b = 0; b < (int)vi.g1_blocks; b++) {
    blk_off[nblocks] = (uint8_t)p;
    blk_len[nblocks] = vi.g1_data;
    p += vi.g1_data;
    nblocks++;
  }
  for (int b = 0; b < (int)vi.g2_blocks; b++) {
    blk_off[nblocks] = (uint8_t)p;
    blk_len[nblocks] = vi.g2_data;
    p += vi.g2_data;
    nblocks++;
  }
  for (int b = 0; b < nblocks; b++)
    rs_encode(&dcw[blk_off[b]], blk_len[b], vi.ec_per_blk, ec[b]);

  // Interleave: data column-wise across blocks, then EC column-wise.
  int maxlen = 0;
  for (int b = 0; b < nblocks; b++)
    if (blk_len[b] > maxlen) maxlen = blk_len[b];
  int o = 0;
  for (int i = 0; i < maxlen; i++) {
    for (int b = 0; b < nblocks; b++) {
      if (i < (int)blk_len[b]) out[o++] = dcw[blk_off[b] + i];
    }
  }
  for (int i = 0; i < (int)vi.ec_per_blk; i++) {
    for (int b = 0; b < nblocks; b++) out[o++] = ec[b][i];
  }
}

// -----------------------------------------------------------------------------
// Function patterns (QR_SPEC.md 5).
// mod[] receives the module colours, fn[] the "this module is a function or
// reserved module" bitmap. Both buffers are QR_BUF_BYTES and fully cleared, so
// the unused rows/columns of a sub-v4 symbol are deterministic zeros.
// -----------------------------------------------------------------------------
void place_finder(int n, int r0, int c0, uint8_t *mod, uint8_t *fn) {
  // One -1..7 double loop draws the 7x7 finder AND its 1-module separator.
  for (int r = -1; r <= 7; r++) {
    for (int c = -1; c <= 7; c++) {
      const int rr = r0 + r;
      const int cc = c0 + c;
      if (rr < 0 || rr >= n || cc < 0 || cc >= n) continue;
      const int dark = ((r >= 0 && r <= 6) && (c == 0 || c == 6)) ||
                       ((c >= 0 && c <= 6) && (r == 0 || r == 6)) ||
                       (r >= 2 && r <= 4 && c >= 2 && c <= 4);
      mod_set(mod, rr, cc, dark);
      mod_set(fn, rr, cc, 1);
    }
  }
}

void build_function(int n, uint8_t align_c, uint8_t *mod, uint8_t *fn) {
  memset(mod, 0, QR_BUF_BYTES);
  memset(fn, 0, QR_BUF_BYTES);

  place_finder(n, 0, 0, mod, fn);
  place_finder(n, 0, n - 7, mod, fn);
  place_finder(n, n - 7, 0, mod, fn);

  // Timing patterns: row 6 and column 6, only between the separators.
  for (int i = 8; i <= n - 9; i++) {
    mod_set(mod, i, 6, (i % 2) == 0);
    mod_set(fn, i, 6, 1);
    mod_set(mod, 6, i, (i % 2) == 0);
    mod_set(fn, 6, i, 1);
  }

  // Alignment patterns: Cartesian product of the centre list, omitting any
  // centre whose module is already occupied (that check drops exactly the
  // finder collisions - for v2-v4 only the bottom-right pattern survives).
  if (align_c) {
    const int centres[2] = {6, (int)align_c};
    for (int a = 0; a < 2; a++) {
      for (int b = 0; b < 2; b++) {
        const int r0 = centres[a];
        const int c0 = centres[b];
        if (mod_get(fn, r0, c0)) continue;
        for (int r = -2; r <= 2; r++) {
          for (int c = -2; c <= 2; c++) {
            const int dark = (r == -2 || r == 2 || c == -2 || c == 2 || (r == 0 && c == 0));
            mod_set(mod, r0 + r, c0 + c, dark);
            mod_set(fn, r0 + r, c0 + c, 1);
          }
        }
      }
    }
  }

  // Reserve the two format-information areas (written light for now) and the
  // dark module. No version-information block: that is V >= 7 only.
  for (int i = 0; i <= 8; i++) {
    if (!mod_get(fn, 8, i)) {
      mod_set(mod, 8, i, 0);
      mod_set(fn, 8, i, 1);
    }
    if (!mod_get(fn, i, 8)) {
      mod_set(mod, i, 8, 0);
      mod_set(fn, i, 8, 1);
    }
  }
  for (int i = 0; i <= 7; i++) {
    if (!mod_get(fn, 8, n - 1 - i)) {
      mod_set(mod, 8, n - 1 - i, 0);
      mod_set(fn, 8, n - 1 - i, 1);
    }
    if (!mod_get(fn, n - 1 - i, 8)) {
      mod_set(mod, n - 1 - i, 8, 0);
      mod_set(fn, n - 1 - i, 8, 1);
    }
  }
  mod_set(mod, n - 8, 8, 1);  // dark module, always
  mod_set(fn, n - 8, 8, 1);
}

// -----------------------------------------------------------------------------
// Mask patterns (QR_SPEC.md 7). i = row, j = column. True -> invert.
// Note the classic traps: mask 1 is ROW based, mask 2 is COLUMN based, mask 5
// has no outer % 2 while mask 6 does.
// -----------------------------------------------------------------------------
inline int mask_bit(uint8_t m, int i, int j) {
  switch (m) {
    case 0: return ((i + j) % 2) == 0;
    case 1: return (i % 2) == 0;
    case 2: return (j % 3) == 0;
    case 3: return ((i + j) % 3) == 0;
    case 4: return (((i / 2) + (j / 3)) % 2) == 0;
    case 5: return (((i * j) % 2) + ((i * j) % 3)) == 0;
    case 6: return ((((i * j) % 2) + ((i * j) % 3)) % 2) == 0;
    default: return ((((i + j) % 2) + ((i * j) % 3)) % 2) == 0;
  }
}

// -----------------------------------------------------------------------------
// Zigzag data placement (QR_SPEC.md 6). Bits past the end of the stream (the 7
// remainder bits of v2-v4) are written as 0 and masked like any other bit.
// -----------------------------------------------------------------------------
void place_data(int n, const uint8_t *fn, const uint8_t *stream, int total_bits,
                uint8_t mask, uint8_t *mod) {
  int bi = 0;
  int up = 1;
  int col = n - 1;
  while (col > 0) {
    if (col == 6) col = 5;  // skip the vertical timing column entirely
    for (int k = 0; k < n; k++) {
      const int r = up ? (n - 1 - k) : k;
      for (int t = 0; t < 2; t++) {  // right column of the pair first
        const int c = col - t;
        if (mod_get(fn, r, c)) continue;
        int bit = 0;
        if (bi < total_bits) bit = (stream[bi >> 3] >> (7 - (bi & 7))) & 1;
        bi++;
        if (mask_bit(mask, r, c)) bit ^= 1;
        mod_set(mod, r, c, bit);
      }
    }
    up = !up;
    col -= 2;
  }
}

// -----------------------------------------------------------------------------
// Format information, both copies (QR_SPEC.md 5). i = 0 is the LSB of F.
// The two discontinuities skip the timing patterns.
// -----------------------------------------------------------------------------
void place_format(int n, uint16_t f, uint8_t *mod) {
  for (int i = 0; i < 15; i++) {
    const int b = (f >> i) & 1;
    const int r1 = (i < 6) ? i : ((i < 8) ? (i + 1) : (n - 15 + i));
    mod_set(mod, r1, 8, b);
    const int c2 = (i < 8) ? (n - 1 - i) : ((i == 8) ? 7 : (14 - i));
    mod_set(mod, 8, c2, b);
  }
  mod_set(mod, n - 8, 8, 1);  // dark module last, per QR_SPEC.md 5
}

// -----------------------------------------------------------------------------
// ISO 18004 penalty rules 1-4, scored over the FINAL symbol.
// Pure integer arithmetic - no floating point anywhere on this path.
// -----------------------------------------------------------------------------
int32_t penalty(int n, const uint8_t *mod) {
  int32_t score = 0;

  // Rule 1: runs of >= 5 identical modules in a row or column, +(L - 2) each.
  for (int r = 0; r < n; r++) {
    int prev = mod_get(mod, r, 0);
    int run = 1;
    for (int c = 1; c < n; c++) {
      const int v = mod_get(mod, r, c);
      if (v == prev) {
        run++;
      } else {
        if (run >= 5) score += run - 2;
        prev = v;
        run = 1;
      }
    }
    if (run >= 5) score += run - 2;
  }
  for (int c = 0; c < n; c++) {
    int prev = mod_get(mod, 0, c);
    int run = 1;
    for (int r = 1; r < n; r++) {
      const int v = mod_get(mod, r, c);
      if (v == prev) {
        run++;
      } else {
        if (run >= 5) score += run - 2;
        prev = v;
        run = 1;
      }
    }
    if (run >= 5) score += run - 2;
  }

  // Rule 2: every uniform 2x2 block, +3. Overlapping blocks each count.
  for (int r = 0; r < n - 1; r++) {
    for (int c = 0; c < n - 1; c++) {
      const int v = mod_get(mod, r, c);
      if (mod_get(mod, r, c + 1) == v && mod_get(mod, r + 1, c) == v &&
          mod_get(mod, r + 1, c + 1) == v)
        score += 3;
    }
  }

  // Rule 3: +40 per 1:1:3:1:1 finder-like run with 4 light modules on one side,
  // i.e. 10111010000 or 00001011101, scanned in rows and columns.
  // Scanning is in-matrix only - we do not extend into a virtual quiet zone
  // (BRIEF 8.8; both readings of the standard yield scannable symbols).
  {
    static const uint8_t P1[11] = {1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0};
    static const uint8_t P2[11] = {0, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1};
    for (int r = 0; r < n; r++) {
      for (int c = 0; c + 10 < n; c++) {
        int m1 = 1, m2 = 1;
        for (int k = 0; k < 11; k++) {
          const int v = mod_get(mod, r, c + k);
          if (v != (int)P1[k]) m1 = 0;
          if (v != (int)P2[k]) m2 = 0;
          if (!m1 && !m2) break;
        }
        if (m1 || m2) score += 40;
      }
    }
    for (int c = 0; c < n; c++) {
      for (int r = 0; r + 10 < n; r++) {
        int m1 = 1, m2 = 1;
        for (int k = 0; k < 11; k++) {
          const int v = mod_get(mod, r + k, c);
          if (v != (int)P1[k]) m1 = 0;
          if (v != (int)P2[k]) m2 = 0;
          if (!m1 && !m2) break;
        }
        if (m1 || m2) score += 40;
      }
    }
  }

  // Rule 4: dark-module ratio. ISO: take the previous and next multiple of five
  // of the dark percentage, subtract 50, /5, take the smaller, x10. Written
  // with integers only: lo = floor(pct/5), hi = ceil(pct/5), then
  // min(|lo - 10|, |hi - 10|) * 10.
  {
    int dark = 0;
    for (int r = 0; r < n; r++)
      for (int c = 0; c < n; c++) dark += mod_get(mod, r, c);
    const int total = n * n;
    const int p20 = dark * 20;  // dark * 100 / 5, numerator only
    const int lo = p20 / total;
    const int hi = (p20 + total - 1) / total;
    int a = lo - 10;
    if (a < 0) a = -a;
    int b = hi - 10;
    if (b < 0) b = -b;
    score += (int32_t)((a < b ? a : b) * 10);
  }

  return score;
}

}  // namespace

// Symbol cached for qr_draw(). 166 B of SRAM; the GF tables cost none.
static uint8_t s_modules[QR_BUF_BYTES];
static uint8_t s_size = 0;

bool qr_encode(const char *text, uint8_t *modules, uint8_t &size) {
  size = 0;
  if (text == 0 || modules == 0) return false;

  const size_t len = strlen(text);
  if (len == 0) return false;

  // Smallest version 1..4 at ECC L whose byte-mode capacity (data_cw - 2, the
  // 12-bit header rounded up to a whole codeword) holds the payload.
  int vidx = -1;
  for (int v = QR_MIN_VERSION; v <= QR_MAX_VERSION; v++) {
    if (len <= (size_t)(QR_VER[v - 1].data_cw - 2)) {
      vidx = v - 1;
      break;
    }
  }
  if (vidx < 0) return false;

  const QrVerInfo &vi = QR_VER[vidx];
  const int n = (int)vi.size;

  uint8_t stream[QR_MAX_TOTAL_CW];
  build_stream((const uint8_t *)text, (int)len, vi, stream);
  const int total_bits = (int)vi.total_cw * 8;

  uint8_t fmod[QR_BUF_BYTES];
  uint8_t fbits[QR_BUF_BYTES];
  build_function(n, vi.align_c, fmod, fbits);

  // Mask selection: build all eight FINAL symbols (format info written in),
  // score them with the ISO penalty rules, keep the lowest. The caller's buffer
  // doubles as the scratch matrix so no ninth 165 B buffer is needed.
  uint8_t best_mask = 0;
  int32_t best_score = 0;
  for (uint8_t m = 0; m < 8; m++) {
    memcpy(modules, fmod, QR_BUF_BYTES);
    place_data(n, fbits, stream, total_bits, m, modules);
    place_format(n, QR_FORMAT_L[m], modules);
    const int32_t s = penalty(n, modules);
    if (m == 0 || s < best_score) {
      best_score = s;
      best_mask = m;
    }
  }

  memcpy(modules, fmod, QR_BUF_BYTES);
  place_data(n, fbits, stream, total_bits, best_mask, modules);
  place_format(n, QR_FORMAT_L[best_mask], modules);

  memcpy(s_modules, modules, QR_BUF_BYTES);
  s_size = (uint8_t)n;
  size = (uint8_t)n;
  return true;
}

// =============================================================================
//  PART 2 - RENDERER (firmware only; U8g2 lives strictly behind this fence)
// =============================================================================
#ifdef ARDUINO

#include <Arduino.h>
#include <U8g2lib.h>

void qr_draw(U8G2 &u8g2, int x, int y, uint8_t px_per_module) {
  if (s_size == 0) return;
  if (px_per_module == 0) px_per_module = 1;

  const int n = (int)s_size;
  const int q = QR_QUIET_MODULES;
  const int px = (int)px_per_module;
  const int box = (n + 2 * q) * px;

  // Light paper first: on an OLED a lit pixel is white, so the whole symbol
  // area including the quiet zone is drawn set, and dark modules are cleared.
  u8g2.setDrawColor(1);
  u8g2.drawBox((u8g2_uint_t)x, (u8g2_uint_t)y, (u8g2_uint_t)box, (u8g2_uint_t)box);

  u8g2.setDrawColor(0);
  for (int r = 0; r < n; r++) {
    const uint8_t *row = &s_modules[r * QR_STRIDE_BYTES];
    int c = 0;
    while (c < n) {
      if (!((row[c >> 3] >> (c & 7)) & 1)) {
        c++;
        continue;
      }
      // Merge horizontal runs of dark modules into one drawBox - roughly a 3x
      // cut in u8g2 call count for a typical symbol.
      int run = 1;
      while (c + run < n && ((row[(c + run) >> 3] >> ((c + run) & 7)) & 1)) run++;
      u8g2.drawBox((u8g2_uint_t)(x + (q + c) * px), (u8g2_uint_t)(y + (q + r) * px),
                   (u8g2_uint_t)(run * px), (u8g2_uint_t)px);
      c += run;
    }
  }

  u8g2.setDrawColor(1);
}

#endif  // ARDUINO
