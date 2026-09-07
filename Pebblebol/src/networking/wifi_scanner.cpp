// =============================================================================
//  PEBBLEBOL - networking/wifi_scanner.cpp
//  The scan job's state machine. See wifi_scanner.h for the seam, the timeout
//  argument and the privacy rule.
//
//  PURE: no Arduino, no radio header, no file-scope mutable state. Every device
//  call goes through the WifiScanDriver the caller passes in.
// =============================================================================
#include "wifi_scanner.h"

// The radio is released exactly once per started run, whatever ends the run.
// Every terminal transition below goes through this function for that reason:
// four exit paths that each remembered to call stop() would be four places to
// forget, and "Wi-Fi shuts down after use" would be a property of whoever
// wrote the last one.
static void release(WifiScanJob& j, const WifiScanDriver& d) {
  if (!j.stopped) {
    j.stopped = 1;
    if (d.stop) {
      d.stop();
    }
  }
}

static void finish(WifiScanJob& j, const WifiScanDriver& d, WifiScanState st) {
  j.state = (uint8_t)st;
  release(j, d);
}

void wifi_scan_reset(WifiScanJob& j) {
  j.state      = (uint8_t)WSCAN_IDLE;
  j.count      = 0;
  j.stopped    = 0;
  j.reserved   = 0;
  j.started_ms = 0;
  for (uint8_t i = 0; i < (uint8_t)WIFI_SCAN_MAX_RESULTS; ++i) {
    j.res[i].net_hash    = 0;
    j.res[i].rssi        = 0;
    j.res[i].category    = 0;
    j.res[i].reserved[0] = 0;
    j.res[i].reserved[1] = 0;
  }
}

uint32_t wifi_scan_elapsed_ms(const WifiScanJob& j, uint32_t now_ms) {
  if (j.state == (uint8_t)WSCAN_IDLE && j.started_ms == 0) {
    return 0;
  }
  return (uint32_t)(now_ms - j.started_ms);      // wrap-safe by construction
}

bool wifi_scan_is_busy(const WifiScanJob& j) {
  return j.state == (uint8_t)WSCAN_RUNNING;
}

bool wifi_scan_start(WifiScanJob& j, const WifiScanDriver& d, uint32_t now_ms) {
  if (j.state == (uint8_t)WSCAN_RUNNING) {
    return false;             // one radio, one scan: a second tap is a no-op
  }
  wifi_scan_reset(j);
  j.started_ms = now_ms;
  j.state      = (uint8_t)WSCAN_RUNNING;
  if (!d.start || !d.start()) {
    // The radio could not be had at all. stop() is still called: the driver may
    // have got half way up before refusing, and this module does not get to
    // assume otherwise.
    finish(j, d, WSCAN_FAILED);
    return false;
  }
  return true;
}

void wifi_scan_service(WifiScanJob& j, const WifiScanDriver& d, uint32_t now_ms) {
  if (j.state != (uint8_t)WSCAN_RUNNING) {
    return;
  }

  const int16_t r = d.poll ? d.poll() : (int16_t)WSCAN_POLL_FAILED;
  if (r >= 0) {
    // A scan that found NOTHING succeeded. The distinction matters: an empty
    // result is a legitimate answer the screen shows as a toast, while a
    // failure is an error state.
    j.count = d.read ? d.read(j.res, (uint8_t)WIFI_SCAN_MAX_RESULTS) : 0;
    finish(j, d, WSCAN_DONE);
    return;
  }
  if (r == (int16_t)WSCAN_POLL_FAILED) {
    finish(j, d, WSCAN_FAILED);
    return;
  }

  // Still running. THE TIMEOUT IS CHECKED AFTER THE POLL, not before: a scan
  // that completed on the very tick the deadline expires has an answer, and
  // throwing it away to report a timeout would be a lie about what happened.
  if ((uint32_t)(now_ms - j.started_ms) >= (uint32_t)WIFI_SCAN_TIMEOUT_MS) {
    finish(j, d, WSCAN_TIMEOUT);
  }
}

void wifi_scan_cancel(WifiScanJob& j, const WifiScanDriver& d) {
  if (j.state == (uint8_t)WSCAN_RUNNING) {
    j.count = 0;              // a cancelled scan keeps nothing
    finish(j, d, WSCAN_CANCELLED);
    return;
  }
  if (j.state == (uint8_t)WSCAN_IDLE && j.started_ms == 0) {
    return;                   // never started: there is nothing to give back
  }
  // A run happened. Release anyway if something skipped it - idempotent by the
  // `stopped` flag - and leave the state alone so a DONE job that is cancelled
  // after the fact still reads DONE.
  release(j, d);
}
