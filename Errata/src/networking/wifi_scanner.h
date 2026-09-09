// =============================================================================
//  ERRATA - networking/wifi_scanner.h
//  THE SCAN JOB (spec sections 20, 40, 44, 47). P5-C1.
//
//  One passive Wi-Fi scan, from "ask for the radio" to "the radio is off
//  again", as a caller-owned state machine with a hard software timeout and a
//  cancel. The radio itself is never touched from here: every device call goes
//  through the WifiScanDriver seam, which networking/net.cpp fills in and a
//  host test fills in with a fake.
//
//  PURE TRANSLATION UNIT, and the job struct is the CALLER's. Two consequences
//  worth stating: the whole job runs on the host with no radio (which is what
//  makes the timeout and the cancel testable at all), and it costs zero bytes
//  of globals until a screen declares one.
//
//  WHAT THE RESULT MAY CONTAIN - THE PRIVACY RULE (spec 44). ScanResult below
//  carries a salted hash, a signal strength and an abstract category. It
//  carries no network name, no hardware address, no channel and no auth mode:
//  those exist only as locals inside the driver's own read loop, and
//  networking/net_classify.h is the file allowed to touch them.
//  tools/check.sh greps THIS header for both of the words it must not contain,
//  so the rule is a fact about the file rather than a promise in a comment -
//  which is why the words themselves are spelled out in net_classify.h and not
//  here.
//
//  Identifiers and comments are English; nothing here is user-facing.
// =============================================================================
#ifndef ER_WIFI_SCANNER_H
#define ER_WIFI_SCANNER_H

#include <stdint.h>
#include <stddef.h>

#include "../core/config.h"       // WIFI_SCAN_TIMEOUT_MS, WIFI_SCAN_MAX_RESULTS
#include "net_classify.h"         // NetCategory, and the identity rule

// -----------------------------------------------------------------------------
// ONE ACCESS POINT, AS THE GAME IS ALLOWED TO SEE IT. 8 B.
//
// The layout is pinned by offset and not only by size, because a size-only
// check cannot fail in the way that matters: renaming the padding pair into two
// bytes of a network's name keeps sizeof at 8. tests/test_exploration_hash.cpp
// pins every member's offset and the grep gate covers the words.
//
// WHAT THOSE TWO CATCH, STATED AT THE WIDTH THAT WAS MEASURED (phase-5 exit).
// Between them they catch a RENAME, from two directions: rename the pair here
// and the gate's grep bites, while the test stops compiling because its offset
// case names the member. (An earlier note claimed the layout case would PASS a
// renamed pair with eight green checks. It cannot: the case names the member, so
// a header-only rename is a compile error, and only a rename applied everywhere
// would reach the assertion - which was run too, and then the suite passes 24/24
// with 69,702 checks. The architectural point stands - sizeof and offsetof
// genuinely cannot tell one two-byte member from another of the same width - but
// the mechanism there is the compiler, not the assertion.)
//
// NEITHER CAN SEE A VALUE. Assigning the pair two bytes of a beacon name needs no
// rename at all, and net.cpp - which is where a scan is read - is on check.sh's
// IMPURE list and is never compiled by tests/Makefile, so no host case can reach
// that assignment. Measured: the leak left GATE OK and ALL PASS 37/37. Gate 3 in
// tools/check.sh is the third layer that closes it, by requiring every assignment
// to the pair under src/networking to be a literal zero.
// -----------------------------------------------------------------------------
struct ScanResult {
  uint32_t net_hash;     // 0  salted, never 0 - net_classify.h derives it
  int8_t   rssi;         // 4  dBm, clamped to [-127, 0] - the bars a screen draws
  uint8_t  category;     // 5  NetCategory ordinal, < NET_CAT_COUNT
  uint8_t  reserved[2];  // 6  must be 0
};
static_assert(sizeof(ScanResult) == 8, "ScanResult layout drifted");

// -----------------------------------------------------------------------------
// Where the job is. IDLE and the four terminal states are all "not busy"; only
// RUNNING holds the radio.
// -----------------------------------------------------------------------------
enum WifiScanState : uint8_t {
  WSCAN_IDLE = 0,      // never started, or read and reset
  WSCAN_RUNNING,       // the radio is coming up, or the scan is in flight
  WSCAN_DONE,          // results are in job.res[0 .. job.count)
  WSCAN_FAILED,        // the driver refused the radio, or the scan failed
  WSCAN_TIMEOUT,       // WIFI_SCAN_TIMEOUT_MS elapsed with no answer
  WSCAN_CANCELLED,     // the player pressed B
  WSCAN_COUNT
};

// What a driver's poll() returns instead of a count.
#define WSCAN_POLL_RUNNING  (-1)
#define WSCAN_POLL_FAILED   (-2)

// -----------------------------------------------------------------------------
// THE RADIO SEAM. Four calls, and the job makes no others.
//
//   start()  ask for the radio and kick an asynchronous passive scan. false
//            means it could not be had at all (another stack resident, feature
//            compiled out); the job goes straight to WSCAN_FAILED.
//   poll()   WSCAN_POLL_RUNNING while the radio settles or the scan is in
//            flight, WSCAN_POLL_FAILED when the driver knows it failed, and
//            otherwise the number of access points found (0 is a legal answer:
//            a scan that found nothing SUCCEEDED).
//   read()   fill up to `cap` results and release the driver's own buffer.
//            Returns how many were written.
//   stop()   free anything still held and put the radio back to OFF. Called
//            EXACTLY ONCE per started job, on every exit path including cancel
//            and timeout - which is what makes spec section 40's "Wi-Fi shuts
//            down after use" a property of the machine rather than of the
//            screen that happened to drive it.
// -----------------------------------------------------------------------------
struct WifiScanDriver {
  bool    (*start)(void);
  int16_t (*poll)(void);
  uint8_t (*read)(ScanResult *out, uint8_t cap);
  void    (*stop)(void);
};

// -----------------------------------------------------------------------------
// The job. Caller-owned: a screen declares one, a test declares one, and this
// module holds no state of its own.
// -----------------------------------------------------------------------------
struct WifiScanJob {
  uint8_t    state;        // WifiScanState
  uint8_t    count;        // results in res[]
  uint8_t    stopped;      // the driver's stop() has been called for this run
  uint8_t    reserved;     // must be 0
  uint32_t   started_ms;   // monotonic ms at wifi_scan_start()
  ScanResult res[WIFI_SCAN_MAX_RESULTS];
};
static_assert(sizeof(WifiScanJob) == 8 + 8 * WIFI_SCAN_MAX_RESULTS,
              "WifiScanJob grew a field - check what it costs in globals first");

// -----------------------------------------------------------------------------
// wifi_scan_reset(j)
//   Back to WSCAN_IDLE with no results. Does NOT touch the radio: a job that
//   is still running must be cancelled, not reset.
// -----------------------------------------------------------------------------
void wifi_scan_reset(WifiScanJob& j);

// -----------------------------------------------------------------------------
// wifi_scan_start(j, d, now_ms) -> started
//   Kicks the scan and starts the timeout clock. Refuses (returns false,
//   leaving the job as it was) when the job is already RUNNING, so a double tap
//   cannot start two scans over one radio. A driver that refuses leaves the job
//   in WSCAN_FAILED, with stop() already called.
// -----------------------------------------------------------------------------
bool wifi_scan_start(WifiScanJob& j, const WifiScanDriver& d, uint32_t now_ms);

// -----------------------------------------------------------------------------
// wifi_scan_service(j, d, now_ms)
//   Call once per frame while the job is RUNNING. Never blocks.
//
//   THE 12 s TIMEOUT IS LOAD-BEARING, NOT BELT AND BRACES (spec 47). The
//   Arduino core's own scan timeout is 60,000 ms - five times this budget - and
//   its scanComplete() reports the same failure value for "timed out" and "was
//   never triggered", so a poll cannot tell them apart. Without this clock a
//   scan that never comes back leaves the screen spinning and the radio on for
//   a minute. A passive scan at WIFI_SCAN_DWELL_MS per channel over 13 channels
//   is about 3.9 s, so 12 s is a real ceiling and not a formality.
//
//   Timing is wrap-safe: elapsed is computed as an unsigned difference, so a
//   job that spans the 49.7-day millis() wrap is not instantly timed out.
// -----------------------------------------------------------------------------
void wifi_scan_service(WifiScanJob& j, const WifiScanDriver& d, uint32_t now_ms);

// -----------------------------------------------------------------------------
// wifi_scan_cancel(j, d)
//   Spec 47's cancellation, wired to B by the screen. Idempotent, and safe on a
//   job that already finished: the radio is released at most once either way.
//   A cancelled job keeps no results.
// -----------------------------------------------------------------------------
void wifi_scan_cancel(WifiScanJob& j, const WifiScanDriver& d);

// True only in WSCAN_RUNNING - i.e. exactly while the radio is held.
bool wifi_scan_is_busy(const WifiScanJob& j);

// Monotonic ms since wifi_scan_start(), wrap-safe. 0 for a job never started.
uint32_t wifi_scan_elapsed_ms(const WifiScanJob& j, uint32_t now_ms);

#endif  // ER_WIFI_SCANNER_H
