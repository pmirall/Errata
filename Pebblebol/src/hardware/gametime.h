// =============================================================================
//  PEBBLEBOL - hardware/gametime.h
//  The wall clock. Calibration, TZ, virtual (god-mode) skew, and the exact
//  elapsed-time formatter the absence line renders {t} with.
//
//  THIS IS THE ONLY MODULE ALLOWED TO CALL time() / localtime_r() /
//  settimeofday() / tzset(). Everybody else asks gt_now().
//
//  Identifiers and comments: English. User-facing prose: strings_es.h.
//  (The unit symbols "d", "h", "min", "s" emitted by gt_format_elapsed are
//   locale-neutral SI-style abbreviations, identical in Spanish and English,
//   and are format scaffolding rather than prose - see gametime.cpp.)
//
//  Public interface, exactly:
//    gt_begin, gt_set_epoch, gt_cal_state, gt_is_valid, gt_now, gt_skew_add,
//    gt_elapsed_since, gt_format_elapsed, gt_local_tm
//
//  There is NO SNTP and no radio in this module (plan section 2 row G4): the
//  clock is calibrated by whoever knows the time - the on-device time screen
//  (CAL_USER) or, from P8, the creator API (CAL_PHONE) - and the RTC timer
//  carries it across resets and deep sleep from there.
// =============================================================================
#ifndef NT_GAMETIME_H
#define NT_GAMETIME_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>       // struct tm

// -----------------------------------------------------------------------------
// Minimum buffer size for gt_format_elapsed(). The widest possible output is
// the uint32_t ceiling, 4294967295 s = "49710 d 6 h 28 min" (19 chars + NUL).
// Always size the caller's buffer with this macro; never guess.
// -----------------------------------------------------------------------------
#define GT_ELAPSED_BUF 24

// -----------------------------------------------------------------------------
// How the clock came to hold the value it holds (plan section 1.7).
//   CAL_UNSET     nothing has ever calibrated it. gt_now() is an ESTIMATE
//                 (last persisted epoch + uptime) and absence charges ZERO.
//   CAL_ESTIMATED the RTC timer already read a sane epoch at boot, i.e. a
//                 previous run's calibration survived the reset.
//   CAL_USER      a human typed it on the device time screen.
//   CAL_PHONE     a paired phone pushed it (POST /api/time, from P8).
// The ordering is deliberate: everything above CAL_UNSET is trustworthy enough
// to charge an absence with.
// -----------------------------------------------------------------------------
enum TimeCal : uint8_t {
  CAL_UNSET = 0,
  CAL_ESTIMATED,
  CAL_USER,
  CAL_PHONE,
  CAL_COUNT
};

// Rollback guard (plan section 1.7): a calibration that moves the clock more
// than this far BACKWARDS is refused unless it came from CAL_USER, who is
// looking at the device and meant it.
#define GT_ROLLBACK_TOLERANCE_S 300u

// -----------------------------------------------------------------------------
// gt_begin()
//   Call once from setup(), AFTER store_begin() and BEFORE anything asks for a
//   timestamp. Never blocks, never touches the radio, never writes anything.
//   - Installs the POSIX TZ string (the persisted Config.tz when storage has
//     one, otherwise CFG_TZ_STRING) via setenv+tzset, so local time is already
//     correct on a device that will never see the internet.
//   - Seeds the ESTIMATED clock from store_last_seen().
//   The store_begin() ordering is a one-way dependency: storage.cpp never calls
//   into gametime, it takes epochs as arguments. Calling gt_begin() first is
//   not fatal - the estimate simply starts at the epoch, and gt_is_valid()
//   already reports that as untrustworthy.
// -----------------------------------------------------------------------------
void gt_begin(void);

// -----------------------------------------------------------------------------
// gt_set_epoch(epoch, src) -> accepted
//   THE ONLY WAY the clock ever becomes real. Sets the system clock (and with
//   it the RTC timer, which survives a soft reset and deep sleep) to `epoch`
//   UTC seconds and records `src` as the calibration state.
//   Refused, returning false and changing nothing, when:
//     * epoch < NT_EPOCH_SANE_MIN (2017-01-01) - that is an uptime, not a date;
//     * src is CAL_UNSET or out of range;
//     * the clock is already calibrated and this moves it more than
//       GT_ROLLBACK_TOLERANCE_S (5 min) BACKWARDS, unless src == CAL_USER.
//   Never blocks. The god-mode skew is untouched, so time travel survives a
//   calibration exactly as it survived an SNTP fix.
//   Accepting flips gt_is_valid() to true, which is what arms the absence
//   retro-fix in the entry point.
// -----------------------------------------------------------------------------
bool gt_set_epoch(uint32_t epoch, TimeCal src);

// -----------------------------------------------------------------------------
// gt_epoch_from_local(year, month, day, hour, minute)
//   The inverse of gt_local_tm(): turns a LOCAL wall-clock date - year 4-digit,
//   month 1..12, day 1..31, hour 0..23, minute 0..59, seconds implicitly 0 -
//   into the UTC epoch gt_set_epoch() wants, honouring the installed TZ and its
//   DST rule. Returns 0 when the fields do not name a representable date, which
//   gt_set_epoch() then refuses on its own.
//   This exists so the on-device time screen never has to touch mktime(): this
//   module stays the only one allowed to call the C time functions.
// -----------------------------------------------------------------------------
uint32_t gt_epoch_from_local(int year, uint8_t month, uint8_t day,
                             uint8_t hour, uint8_t minute);

// -----------------------------------------------------------------------------
// gt_cal_state()
//   How the clock got its value. CAL_UNSET means "no trustworthy time": the
//   absence mechanic must charge zero (plan section 1.7).
// -----------------------------------------------------------------------------
TimeCal gt_cal_state(void);

// -----------------------------------------------------------------------------
// gt_is_valid()
//   true  -> gt_now() is REAL time: gt_cal_state() != CAL_UNSET.
//   false -> gt_now() is an ESTIMATE (last persisted epoch + uptime). It is
//            monotonic and plausible but arbitrary.
//   THE ABSENCE MECHANIC MUST CHECK THIS FIRST. With false the absence is
//   unknown and charges ZERO (plan section 1.7); the truth is retro-applied if
//   this later flips to true. Never accuse the user on the strength of an
//   estimate.
//   Cheap; this is also the poller that adopts a surviving RTC value as
//   CAL_ESTIMATED, so call it (or gt_now(), which calls it) at least once per
//   loop().
// -----------------------------------------------------------------------------
bool gt_is_valid(void);

// -----------------------------------------------------------------------------
// gt_now()
//   Unix epoch seconds, UTC, plus the accumulated god-mode skew. This is the
//   ONLY time source for every persisted timestamp in PetSave/Config/RtcKeep.
//   Never returns 0 as an error - check gt_is_valid() for trustworthiness.
//   Note it may JUMP FORWARD when gt_set_epoch() lands. Nothing may derive elapsed game
//   time by differencing it across a tick: PetSave.age_s is accumulated by the
//   sim from sim_step_seconds() precisely so a mid-life jump cannot teleport
//   the pet's age.
// -----------------------------------------------------------------------------
uint32_t gt_now(void);

// -----------------------------------------------------------------------------
// gt_skew_add(delta_s)
//   God mode "time travel": adds delta_s to the virtual skew accumulator.
//   gt_now() == real_epoch + skew. The system clock is never touched, so a
//   later calibration cannot fight it and the absence math stays coherent when the
//   speed multiplier goes back to x1.
//   godmode.cpp owns the running total and must remember it to be able to
//   undo it (there is deliberately no gt_skew_set/gt_skew_get). The total is
//   clamped to +/-0xFFFFFFFF s.
// -----------------------------------------------------------------------------
void gt_skew_add(int64_t delta_s);

// -----------------------------------------------------------------------------
// gt_format_elapsed(seconds, buf, buflen) -> buf
//   The {t} renderer for the absence line and the egg
//   waiting line. {t} is always rendered with the EXACT
//   elapsed time - the precision is the joke; never round."
//     0..59 s        "45 s"
//     1..59 min      "12 min"
//     1..23 h        "3 h 12 min"
//     >= 1 d         "2 d 7 h 41 min"
//   No zero padding, no rounding, no "casi una semana": the sub-unit is
//   truncated, never rounded up. Always NUL-terminates when buflen >= 1.
//   buf must be at least GT_ELAPSED_BUF bytes. Returns buf (or "" if buf is
//   null / buflen is 0) so it can be dropped straight into a snprintf arg.
//   Pure: no clock access, host-testable, zero floating point.
// -----------------------------------------------------------------------------
const char* gt_format_elapsed(uint32_t seconds, char* buf, size_t buflen);

// -----------------------------------------------------------------------------
// gt_elapsed_since(then, now)
//   The clamped epoch delta of plan section 1.7: 0 whenever `now` is not
//   strictly after `then`, so a clock that moved backwards - a rollback, a
//   never-calibrated baseline, a save from the future - charges NOTHING
//   instead of wrapping into 136 years. Pure, host-tested
//   (tests/test_overflow.cpp); this is the only subtraction of two epochs
//   allowed outside sim.cpp.
// -----------------------------------------------------------------------------
uint32_t gt_elapsed_since(uint32_t then, uint32_t now);

// -----------------------------------------------------------------------------
// gt_local_tm(out)
//   Fills out with the LOCAL broken-down time of gt_now() (TZ + DST applied).
//   ALWAYS fills the struct - even on an estimated clock - so the caller can
//   render something. Returns gt_is_valid(): false means "this is an estimate,
//   do not print it as a fact".
//   Note: tm_wday / tm_mon are indices; the Spanish weekday and month names
//   live in strings_es.h, not here.
// -----------------------------------------------------------------------------
bool gt_local_tm(struct tm& out);

#if !defined(ARDUINO)
// -----------------------------------------------------------------------------
// gt_test_reset()
//   HOST TEST HOOK ONLY - ARDUINO is always defined by the Arduino build, so
//   this cannot exist in shipped firmware. Forgets the calibration, the
//   estimate base and the skew so a test can drive gt_begin() again from a
//   clean state.
// -----------------------------------------------------------------------------
void gt_test_reset(void);
#endif

#endif  // NT_GAMETIME_H
