// =============================================================================
//  NOTTAMAGOCHI - gametime.h
//  The wall clock. NTP bring-up, TZ, virtual (god-mode) skew, and the exact
//  elapsed-time formatter the absence ladder renders {t} with.
//
//  THIS IS THE ONLY MODULE ALLOWED TO CALL time() / localtime_r() /
//  configTzTime() / tzset(). Everybody else asks gt_now().
//
//  Identifiers and comments: English. User-facing prose: strings_es.h.
//  (The unit symbols "d", "h", "min", "s" emitted by gt_format_elapsed are
//   locale-neutral SI-style abbreviations, identical in Spanish and English,
//   and are format scaffolding rather than prose - see gametime.cpp.)
//
//  BRIEF 4 #7 - public interface, exactly:
//    gt_begin, gt_sync_start, gt_is_valid, gt_now, gt_skew_add,
//    gt_format_elapsed, gt_local_tm
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
// gt_sync_start()
//   Kick off (or re-arm) SNTP. Call it once WiFi reports WL_CONNECTED; calling
//   it every loop() is fine and intended - it is idempotent and self-rate-
//   limiting (one attempt per SNTP_GIVEUP_S while in flight, one retry per
//   SNTP_RETRY_S after a failure, no-op once the clock is real and armed).
//   NEVER BLOCKS: it issues configTzTime() and returns. Poll gt_is_valid().
//   lwIP re-polls on its own every SNTP_RESYNC_S once armed.
// -----------------------------------------------------------------------------
void gt_sync_start(void);

// -----------------------------------------------------------------------------
// gt_is_valid()
//   true  -> gt_now() is REAL time from SNTP. Absence math may be trusted and
//            the pet may hold the user responsible.
//   false -> gt_now() is an ESTIMATE (last persisted epoch + uptime). It is
//            monotonic and plausible but arbitrary.
//   THE ABSENCE MECHANIC MUST CHECK THIS FIRST. With false, GAME_DESIGN 5.1
//   says: AbsenceTier = ABS_UNKNOWN with the ABS_LARGA tier as a floor, and
//   retro-apply the true tier if this later flips to true. Never accuse the
//   user on the strength of an estimate.
//   Cheap; this is also the poller that notices SNTP landing, so call it (or
//   gt_now(), which calls it) at least once per loop().
// -----------------------------------------------------------------------------
bool gt_is_valid(void);

// -----------------------------------------------------------------------------
// gt_now()
//   Unix epoch seconds, UTC, plus the accumulated god-mode skew. This is the
//   ONLY time source for every persisted timestamp in PetSave/Config/RtcKeep.
//   Never returns 0 as an error - check gt_is_valid() for trustworthiness.
//   Note it may JUMP FORWARD when SNTP lands. Nothing may derive elapsed game
//   time by differencing it across a tick: PetSave.age_s is accumulated by the
//   sim from sim_step_seconds() precisely so a mid-life jump cannot teleport
//   the pet's age.
// -----------------------------------------------------------------------------
uint32_t gt_now(void);

// -----------------------------------------------------------------------------
// gt_skew_add(delta_s)
//   God mode "time travel": adds delta_s to the virtual skew accumulator.
//   gt_now() == real_epoch + skew. The system clock is never touched, so a
//   later resync cannot fight it and the absence math stays coherent when the
//   speed multiplier goes back to x1 (GAME_DESIGN 12).
//   godmode.cpp owns the running total and must remember it to be able to
//   undo it (there is deliberately no gt_skew_set/gt_skew_get). The total is
//   clamped to +/-0xFFFFFFFF s.
// -----------------------------------------------------------------------------
void gt_skew_add(int64_t delta_s);

// -----------------------------------------------------------------------------
// gt_format_elapsed(seconds, buf, buflen) -> buf
//   The {t} renderer for the whole absence ladder, the memorial and the egg
//   waiting line. GAME_DESIGN 5.3: "t is always rendered with the EXACT
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
// gt_local_tm(out)
//   Fills out with the LOCAL broken-down time of gt_now() (TZ + DST applied).
//   ALWAYS fills the struct - even on an estimated clock - so the caller can
//   render something. Returns gt_is_valid(): false means "this is an estimate,
//   do not print it as a fact".
//   Note: tm_wday / tm_mon are indices; the Spanish weekday and month names
//   for the memorial "{f}, {h}" line live in ui.cpp, not here.
// -----------------------------------------------------------------------------
bool gt_local_tm(struct tm& out);

#endif  // NT_GAMETIME_H
