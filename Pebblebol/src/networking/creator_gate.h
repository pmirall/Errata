// =============================================================================
//  PEBBLEBOL - networking/creator_gate.h
//  THE CREATOR PIN AND THE PORTAL IDLE TIMER, AS PURE ARITHMETIC (P8-C1/C2).
//
//  WHY THIS IS ITS OWN MODULE AND NOT SIX LINES INSIDE webui.cpp.
//    tests/Makefile compiles no device module: webui.cpp includes Arduino.h and
//    WebServer.h, so nothing inside it can be driven by a host binary. Every
//    rule that decides whether a request is authorised, when a lockout ends and
//    when the access point goes away is therefore lifted out to here, exactly
//    as P6-C3 lifted pwr_tick_budget() into hardware/power.cpp so a host binary
//    could drive the idle ladder. What is left in webui.cpp is transport:
//    reading a header, answering a status code, writing flash.
//
//  PURE, AND CALLER-OWNED. No Arduino header, no file-scope state - the whole
//  gate is one 16 B struct the caller holds, which is what lets a test run a
//  lockout, a reboot and an idle expiry in one process without a device. It is
//  on tools/check.sh's PURE_NET list and both networking purity gates apply.
//
//  ==========================================================================
//  THE CLOCK QUESTION, ANSWERED RATHER THAN ASSUMED (P8-C1 deliverable 3)
//  ==========================================================================
//  Two deadlines live here and they DO NOT use the same clock.
//
//  1. THE LOCKOUT (lock_until_ms) IS MONOTONIC (hardware/gametime.h gt_mono32)
//     AND IS HELD IN RAM ONLY.
//     It must never be compared against a clock anybody outside the device can
//     move. gt_now() is that clock twice over:
//       * it is UNCALIBRATED by default. With CAL_UNSET it is an ESTIMATE -
//         "last persisted epoch + uptime" - so after a power cut it restarts
//         from save_last_seen(). A deadline written as gt_now()+60 can come
//         back as hours in the future (the owner is locked out of their own
//         device) or as already past (the attacker gets their guesses back).
//       * from P8-C3 the PHONE sets it. POST /api/time calls
//         gt_set_epoch(CAL_PHONE), and the phone is the unauthenticated party
//         on the far side of this very gate. A lockout deadline in wall time
//         would be defeated by pushing the clock forward - which is precisely
//         the shape of the phase-6 forward-clock XP farm, and building its
//         cousin here is the thing this chunk was told not to do.
//     gt_mono32() is immune to both: it is never moved by a calibration and it
//     keeps counting across a light sleep, a deep sleep and a soft reset.
//
//  2. WHAT AN UNCALIBRATED CLOCK DOES TO THE LOCKOUT: NOTHING. That is the
//     point of choosing the monotonic clock. The lockout behaves identically on
//     a device that has never known the date and on one calibrated ten minutes
//     ago, and no clock change - user, phone, or god-mode skew - can shorten or
//     lengthen it by one millisecond.
//
//  3. WHAT A POWER CUT DOES, AND WHY IT IS ACCEPTABLE. gt_mono32() restarts
//     when power is actually removed, so the RAM deadline is lost. What
//     survives is ConfigV2.pin_fail_count, and cg_open() restores it: a gate
//     that comes back with fail_count == CREATOR_PIN_FAIL_MAX is ARMED, so the
//     next wrong PIN locks immediately instead of buying five fresh guesses.
//     The remaining concession - that the 60 s wait itself is skipped once per
//     reboot - is worth nothing to an attacker: power-cycling this device means
//     standing in front of it, and the device DISPLAYS THE PIN ON ITS OWN
//     SCREEN. Anyone who can reboot it can read it.
//
//  4. ConfigV2.pin_lock_until IS A MIRROR, NOT THE AUTHORITY. webui.cpp writes
//     a wall-clock estimate of the deadline there for the DIAG screen, on the
//     same flash write that persists the armed edge, and NOTHING EVER READS IT
//     BACK as a deadline. The field's own comment in save_schema.h says so. If
//     a later chunk wants to read it, it has to argue with this paragraph
//     first.
//
//  5. THE IDLE TIMER (last_seen_ms) IS ALSO MONOTONIC, for a different reason.
//     It answers "how long since a client last talked to us", which is an
//     UPTIME question, and it must not move when the clock is calibrated: a
//     POST /api/time landing mid-session would otherwise jump gt_now() by years
//     and either kill the portal instantly or make it immortal. millis() would
//     also be wrong, though less obviously: hardware/power.cpp's idle ladder
//     can light-sleep the CPU with the radio up, and millis() does not count
//     that time while gt_mono32() does - so millis() would hold the access
//     point open past its D7 budget without anyone asking it to.
//
//  ==========================================================================
//  WHAT THIS GATE IS NOT
//  ==========================================================================
//  It is not a security boundary and no comment here should imply one. With
//  WEB_RATE_REFILL_PER_S 4 and WEB_COST_MUTATE 2 the rate limiter alone allows
//  ~2 guesses/s, so 10,000 PINs fall in ~83 minutes; the 5-failure / 60 s
//  lockout stretches that to ~33 h worst case. Neither number is a
//  cryptographic property. What actually protects the device is that the access
//  point exists ONLY while the CREATOR screen is open, that the idle timer here
//  tears it down after ConfigV2.creator_idle_s (D7, 300 s), and that the PIN is
//  shown on a screen you have to be looking at. Spec section 34 asks for an
//  authorisation gate against the person standing next to you; that is what
//  this is.
//
//  EVERYTHING THAT ARRIVES OVER HTTP IS HOSTILE UNTIL IT IS VALIDATED HERE.
//  The page's own PIN box is a courtesy to the user and never a control: the
//  device has no way to tell a request from the page apart from a request from
//  curl, so it assumes curl.
//
//  All identifiers and comments English.
// =============================================================================
#ifndef PB_CREATOR_GATE_H
#define PB_CREATOR_GATE_H

#include <stdint.h>

#include "../core/config.h"      // CREATOR_PIN_*, CREATOR_IDLE_*, WEB_PIN_MAX

// -----------------------------------------------------------------------------
// What cg_verify() answers. The caller turns these into status codes; the gate
// never writes a response and never touches a socket.
// -----------------------------------------------------------------------------
enum CgVerdict : uint8_t {
  CG_OK = 0,        // the supplied PIN matched the issued one
  CG_BAD_PIN,       // malformed or wrong - the two are ONE answer on purpose
  CG_LOCKED,        // too many failures; cg_lock_left_ms() says how long
  CG_NO_PIN,        // no PIN has been issued, so nothing can match
  CG_VERDICT_COUNT
};

// -----------------------------------------------------------------------------
// The whole gate. 16 B, caller-owned; webui.cpp holds exactly one.
//
// pin           1..WEB_PIN_MAX-1, or 0 for "none issued". ConfigV2.creator_pin
//               uses the same encoding, which is why 0 is never minted.
// idle_s        the EFFECTIVE timeout, already through cg_idle_seconds().
// last_seen_ms  monotonic ms of the last AUTHORISED request. Unauthenticated
//               traffic deliberately does not move it - see cg_verify().
// lock_until_ms monotonic ms, meaningful only while lock_armed. RAM ONLY.
// fail_count    consecutive failures, saturating at CREATOR_PIN_FAIL_MAX.
// lock_armed    a deadline is running.
// open          between cg_open() and cg_close(). A closed gate answers
//               nothing and never expires: with the portal down there is no
//               session to time out and no request to authorise.
// -----------------------------------------------------------------------------
struct CreatorGate {
  uint32_t last_seen_ms;
  uint32_t lock_until_ms;
  uint16_t pin;
  uint16_t idle_s;
  uint8_t  fail_count;
  uint8_t  lock_armed;
  uint8_t  open;
  uint8_t  reserved;        // must be 0
};

// -----------------------------------------------------------------------------
// cg_mint_pin(entropy) -> 1..WEB_PIN_MAX-1, NEVER 0.
//
//   0 IS THE "NO PIN ISSUED" SENTINEL IN ConfigV2 (save_schema.h:255), so a
//   mint that can return it is a bug that only shows up one time in ten
//   thousand: the device would print "PIN: 0000", persist "none issued", and
//   re-roll on the next CREATOR entry - silently invalidating the PIN already
//   on the user's phone. Excluding it costs one addition.
//
//   The caller draws `entropy` from core/rng.h's RNG_MISC stream, which is the
//   stream rng.h documents for "tokens, nonces, PINs, canaries". Passing the
//   draw IN rather than calling rng_below() here is what makes the mint
//   reproducible from a seed in a host test; it also keeps this module free of
//   the global RNG state the purity gate forbids.
// -----------------------------------------------------------------------------
uint16_t cg_mint_pin(uint32_t entropy);

// -----------------------------------------------------------------------------
// cg_idle_seconds(cfg_idle_s) -> the timeout actually used.
//   0                      -> CREATOR_IDLE_S_DEFAULT (D7, 300 s). A ConfigV2
//                             that predates this field, or a fresh device, has
//                             a zero there and MUST NOT mean "shut down
//                             immediately".
//   > CREATOR_IDLE_S_MAX   -> CREATOR_IDLE_S_MAX. This bounds a persisted
//                             number, not user input: nothing over HTTP writes
//                             it in this phase.
// -----------------------------------------------------------------------------
uint16_t cg_idle_seconds(uint16_t cfg_idle_s);

// -----------------------------------------------------------------------------
// cg_open() - the CREATOR screen was entered (or the firmware booted straight
// into it). `pin` and `persisted_fails` come from ConfigV2; `cfg_idle_s` is
// ConfigV2.creator_idle_s raw, and is resolved here.
//
// THE LOCKOUT DEADLINE IS DELIBERATELY NOT RESTORED - there is nothing to
// restore it from that survives the power cut that lost it (see the header
// note 3). fail_count IS restored, so a gate that was locked comes back armed.
// -----------------------------------------------------------------------------
void cg_open(CreatorGate& g, uint16_t pin, uint8_t persisted_fails,
             uint16_t cfg_idle_s, uint32_t now_ms);

// The portal went away. The gate stops authorising and stops expiring.
void cg_close(CreatorGate& g);

// -----------------------------------------------------------------------------
// cg_parse_pin(s, out) -> did `s` name a PIN?
//
//   EXACTLY CREATOR_PIN_DIGITS ASCII digits and a NUL. No sign, no space, no
//   leading zero rule, no fifth character. "0000" parses to 0 and will then
//   fail to match, because 0 is never issued.
//
//   THE LENGTH CHECK IS THE OVERFLOW FIX AND IT IS NOT COSMETIC. The helper
//   this replaces (arg_u32, still readable at `git show
//   aa2b7ee:Pebblebol/webui.cpp`) accumulated into a uint32_t against a
//   0xFFFFFFFF ceiling, so the multiply wrapped mod 2^32 and every decimal
//   congruent to the PIN authenticated AS the PIN - the recorded example is
//   that PIN 3821 was also produced by "4294971117". That helper needed a
//   64-bit accumulator to be correct. Bounding the INPUT to four characters
//   first makes the accumulator width unable to matter at all: the largest
//   value representable is 9999.
// -----------------------------------------------------------------------------
bool cg_parse_pin(const char* s, uint16_t& out);

// -----------------------------------------------------------------------------
// cg_verify(g, supplied, now_ms) -> the verdict, and the ONLY mutator of the
// failure counter.
//
//   * A locked gate refuses EVERYTHING, including the correct PIN, and does not
//     count the attempt: otherwise a lockout could be extended forever by a
//     client that keeps guessing, and the owner who mistyped four times would
//     be punished for waiting.
//   * When the deadline passes the gate allows exactly ONE attempt. fail_count
//     stays at CREATOR_PIN_FAIL_MAX, so a failure re-locks at once. That is the
//     throttle: after the fifth failure the attacker gets one guess per 60 s.
//   * Only CG_OK moves last_seen_ms. UNAUTHENTICATED TRAFFIC CANNOT EXTEND THE
//     PORTAL'S LIFE - without that rule anyone in radio range could hold the
//     access point up indefinitely by fetching one URL every 299 s, and the
//     section 40 "Wi-Fi only when needed" property would belong to whoever was
//     nearest rather than to the owner.
//   * A malformed PIN and a wrong PIN are the same answer for the same reason
//     a login form does not say which half was wrong.
//
//   TIMING. The comparison is one 16-bit compare of two integers, which is
//   data-independent; do NOT "harden" it into a byte loop over the decimal
//   text, which would introduce the very side channel it looks like it removes.
//   cg_parse_pin() does short-circuit on the first non-digit, which leaks the
//   SHAPE of the candidate and never its value.
// -----------------------------------------------------------------------------
CgVerdict cg_verify(CreatorGate& g, const char* supplied, uint32_t now_ms);

// -----------------------------------------------------------------------------
// cg_persist_fails(g) -> what ConfigV2.pin_fail_count should hold: 0 or
// CREATOR_PIN_FAIL_MAX, never anything between.
//
//   THE INTERMEDIATE COUNTS 1..4 LIVE IN RAM ON PURPOSE. Persisting every
//   failure would hand a remote attacker one flash write per guess - write
//   amplification is the one thing a rate-limited HTTP client can still do to
//   this device - while buying almost nothing: losing a count of 1..4 to a
//   power cut costs at most four guesses and requires physically power-cycling
//   a device that shows the PIN on its screen. Persisting only the ARMED edge
//   bounds an entire attack episode to two writes: one when the fifth failure
//   arms the lockout, one when a success clears it.
// -----------------------------------------------------------------------------
uint8_t cg_persist_fails(const CreatorGate& g);

// Milliseconds left on the lockout, 0 when it is not running. Wrap-safe across
// the gt_mono32() 49.7-day rollover, and never reports more than one whole
// lock window even if the deadline was written before a wrap.
uint32_t cg_lock_left_ms(const CreatorGate& g, uint32_t now_ms);

// -----------------------------------------------------------------------------
// cg_idle_expired(g, now_ms) -> the portal has been idle for its whole budget
// and the CREATOR screen should tear the radio down (spec sections 34 and 40).
// False on a closed gate: a portal that is not up cannot time out.
// -----------------------------------------------------------------------------
bool cg_idle_expired(const CreatorGate& g, uint32_t now_ms);

// -----------------------------------------------------------------------------
// Compile-time sanity on the constants this module contracts against.
// -----------------------------------------------------------------------------
static_assert(WEB_PIN_MAX == 10000, "the PIN is exactly four decimal digits");
static_assert(CREATOR_PIN_DIGITS == 4, "cg_parse_pin's fixed length is the overflow fix");
static_assert(CREATOR_PIN_FAIL_MAX > 0 && CREATOR_PIN_FAIL_MAX < 255,
              "fail_count is a saturating uint8_t");
static_assert(CREATOR_IDLE_S_DEFAULT > 0, "0 seconds would tear the portal down at once");
static_assert((uint32_t)CREATOR_IDLE_S_MAX * 1000u < 0x80000000u,
              "the idle budget in ms must stay inside half the gt_mono32 range");
static_assert(CREATOR_PIN_LOCK_MS < 0x80000000u,
              "the lock window in ms must stay inside half the gt_mono32 range");

#endif  // PB_CREATOR_GATE_H
