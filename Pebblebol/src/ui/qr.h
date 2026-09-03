// =============================================================================
//  NOTTAMAGOCHI - ui/qr.h
//  QR Code encoder (versions 1-4, ECC level L, byte mode) + OLED renderer.
//
//  LAYERING
//  --------
//  The ENCODER half of qr.cpp is portable C++: <stdint.h> + <string.h> only.
//  It host-compiles and is validated module-by-module against the Python
//  `qrcode` reference by tests/test_qr.cpp.
//  The RENDERER half of qr.cpp is fenced behind #ifdef ARDUINO and is the only
//  place U8g2 is touched. This header only FORWARD-DECLARES U8G2, so including
//  qr.h never drags U8g2lib.h into a consumer's translation unit.
//
//  Identifiers/comments: English. This file contains no user-facing text.
// =============================================================================
#ifndef NT_QR_H
#define NT_QR_H

#include <stdint.h>

#include "../core/config.h"

// -----------------------------------------------------------------------------
// Module buffer format
// -----------------------------------------------------------------------------
// The symbol is returned bit-packed, one fixed-stride row per module row:
//
//     byte  = modules[row * QR_STRIDE_BYTES + (col >> 3)]
//     bit   = (byte >> (col & 7)) & 1        // 1 = DARK module
//
// LSB = leftmost column inside a byte (same convention as U8g2 XBM rows, so a
// row can be handed to drawXBM unchanged if that is ever wanted).
//
// The caller's buffer MUST be at least QR_BUF_BYTES (165 B) - it is sized for
// the worst case, version 4 (33 x 33), and qr_encode always writes all of it.
// Rows >= size and columns >= size are written as 0.
// -----------------------------------------------------------------------------
#define QR_MODULE_AT(modules, row, col) \
  ((((const uint8_t *)(modules))[(row) * QR_STRIDE_BYTES + ((col) >> 3)] >> ((col) & 7)) & 1u)

static_assert(QR_STRIDE_BYTES * 8 >= QR_MAX_MODULES, "QR_STRIDE_BYTES too small for QR_MAX_MODULES");
static_assert(QR_BUF_BYTES == QR_MAX_MODULES * QR_STRIDE_BYTES, "QR_BUF_BYTES must be QR_MAX_MODULES * QR_STRIDE_BYTES");
static_assert(QR_MIN_VERSION == 1 && QR_MAX_VERSION == 4, "qr.cpp implements versions 1-4 only");
static_assert(QR_MAX_MODULES == 4 * QR_MAX_VERSION + 17, "QR_MAX_MODULES must match QR_MAX_VERSION");

// -----------------------------------------------------------------------------
// Encoder
// -----------------------------------------------------------------------------
// Encodes `text` (NUL-terminated, byte mode, so any 8-bit payload including
// UTF-8 is legal) into the smallest of versions 1..4 at ECC level L that holds
// it. The mask is chosen by scoring all eight FINAL symbols (format information
// already written in) with ISO 18004 penalty rules 1-4 and taking the lowest.
//
// Capacity, ECC L, byte mode:  v1 = 17 B, v2 = 32 B, v3 = 53 B, v4 = 78 B.
//
// modules : out, >= QR_BUF_BYTES bytes, bit-packed as described above.
// size    : out, modules per side (21, 25, 29 or 33). 0 on failure.
// returns : true on success; false if text is null/empty, longer than 78 bytes,
//           or modules is null. On failure the buffer is left untouched.
//
// Side effect: the symbol is also cached inside qr.cpp so that qr_draw() can
// paint the most recently encoded symbol without being handed the buffer again.
// Not reentrant and not thread-safe (single-threaded by design; never call it
// from an ISR or from the BLE BTC task).
// -----------------------------------------------------------------------------
bool qr_encode(const char *text, uint8_t *modules, uint8_t &size);

// -----------------------------------------------------------------------------
// Renderer (firmware only)
// -----------------------------------------------------------------------------
#ifdef ARDUINO

class U8G2;  // forward declaration only - U8g2lib.h stays out of this header

// Draws the symbol cached by the last successful qr_encode() into the current
// U8g2 buffer. Paints a lit (setDrawColor(1)) square of
// (size + 2 * QR_QUIET_MODULES) * px_per_module pixels a side, then clears the
// dark modules out of it (setDrawColor(0)) - on an OLED, lit = light paper,
// unlit = dark module, which is what a scanner expects. Draw color is left at 1.
//
// With the shipped payload (v2, 25 modules), QR_PX_PER_MODULE = 2 and
// QR_QUIET_MODULES = 3 this is exactly the QR_BOX_SIZE = 62 px box the QR
// screen reserves at (QR_BOX_X, QR_BOX_Y) = (0, 1).
//
// x, y must be >= 0 (u8g2 coordinates are unsigned; a negative value wraps).
// px_per_module 0 is treated as 1. No-op if no symbol has been encoded yet.
void qr_draw(U8G2 &u8g2, int x, int y, uint8_t px_per_module);

#endif  // ARDUINO

#endif  // NT_QR_H
