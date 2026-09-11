// =============================================================================
//  ERRATA - hardware/audio.h
//  THE TONE ENGINE (hardware spec sections 18 and 19, decision D8). P6-C1.
//
//  The V1 baseline adds a passive piezoelectric sounder. This is the whole of
//  what the firmware may ask it for: seven synthesized effects, a four-deep
//  event queue, and a pin that is electrically quiet the rest of the time.
//
//  WHAT THIS FILE IS NOT. It is not a mixer, not a sequencer with tracks and
//  not a player of stored samples: spec section 19 chose synthesis precisely so
//  there is no file storage, no decoder and no amplifier, and section 6 calls
//  the piezo "an event-output device, not a general audio speaker".
//
//  NON-BLOCKING BY CONSTRUCTION. audio_play() only writes a byte into a queue;
//  audio_service() advances at most ONE step per call and returns. Nothing here
//  ever delays, so a 200 ms effect costs the loop nothing but the calls it
//  takes to walk it. (The Arduino core's own tone() would have been the other
//  way round - see the cost note on the device sink in audio.cpp.)
//
//  THE SEAM, and why there is one. Everything above the two function pointers
//  below is arithmetic over a table: the queue, the step clock, the mute rule
//  and the "pin idle between effects" rule are all decided here, on the host,
//  with no radio, no LEDC and no board - the way minigames/games/*_logic.cpp is
//  decided apart from *_draw.cpp. tests/test_audio.cpp binds a sink that
//  RECORDS every call and asserts the emitted sequence note by note. The device
//  sink lives at the bottom of audio.cpp behind `#if defined(ARDUINO)`, exactly
//  as hardware/input.cpp keeps its digitalRead() behind the same guard.
//
//  CF_MUTE IS HONOURED IN TWO PLACES, because it has two meanings. A play
//  requested while muted is never queued (silence costs nothing later), and
//  mute arriving while an effect is in flight stops it on the next service
//  call rather than at the end of the note. Both are named cases in the test.
//
//  Identifiers and comments are English; nothing here is user-facing.
// =============================================================================
#ifndef ER_AUDIO_H
#define ER_AUDIO_H

#include <stdint.h>

#include "../core/config.h"   // AUDIO_QUEUE_LEN, PIN_PIEZO (device half only)
#include "../core/nt_types.h" // SoundVol - the level the hook answers with

// -----------------------------------------------------------------------------
//  THE VOCABULARY - hardware spec section 19, all seven of them and no eighth.
//
//  Each is a short list of {frequency, milliseconds} steps in flash; a step of
//  0 Hz is a deliberate silence inside an effect (DOUBLE_BEEP has one) and is
//  emitted as tone_off, not as a note nobody can hear.
//
//  DURATIONS. Spec section 18 gives four bands by EVENT - UI click 30-80 ms,
//  damage 50-150, capture 150-300, evolution 500-1500 - and this table maps
//  each word of the vocabulary into one of them. Which word sits in which band
//  is a design choice, so it is written down where a change to a step can break
//  it: tests/test_audio.cpp holds the band per effect and fails the day a
//  retune walks one out of its band. The numbers are NOT repeated here, because
//  two lists of the same numbers is the defect this repository keeps finding.
//
//  The evolution band is the one no single effect reaches, and that is
//  deliberate: the ceremony is 4,480 ms long and ui/ceremony.cpp arms one cue
//  per phase edge, so the evolution sound is a SEQUENCE of this vocabulary
//  rather than one long note the queue would have to hold.
// -----------------------------------------------------------------------------
enum SfxId : uint8_t {
  SFX_NONE = 0,      // "nothing is playing"; never queued, never emitted
  SFX_BEEP,          // one short square note - the UI click
  SFX_CHIRP,         // two very short rising notes - something arrived
  SFX_BUZZ,          // low and long - refused, or hit
  SFX_RISE,          // five ascending steps - something got better
  SFX_FALL,          // five descending steps - something got worse
  SFX_DOUBLE_BEEP,   // note, silence, note - confirmed
  SFX_GLITCH,        // six alternating extremes - corruption, error
  // ADDED FOR THE FIRST-BOOT INTRO, AND BOTH EARN THEIR FLASH SOMEWHERE ELSE.
  // A piezo is one square-wave voice with no envelope, so the only things that
  // separate two effects are pitch, duration and silence - which is why these
  // two are a very short click and a held resolution rather than two melodies.
  SFX_TICK,          // one 12 ms click - a keystroke, a cursor, a countdown
  SFX_FANFARE,       // four rising notes and a HELD fifth - you got something
  SFX_COUNT
};

// -----------------------------------------------------------------------------
//  THE OUTPUT SEAM. Two calls, and the engine makes no others.
//
//    tone_on(hz)  drive the piezo at hz. hz is never 0 here.
//    tone_off()   stop driving it and leave the GPIO in its idle state. This
//                 is the "noTone() on every effect end" of the plan bullet and
//                 of spec section 18's "stop the tone immediately after the
//                 sound effect"; it must be safe to call when nothing sounds.
//
//  Every effect ENDS with a tone_off, whatever ended it - the last step, a
//  cancel, or mute arriving mid-note - so the pin is idle between effects by
//  construction rather than by a caller remembering.
// -----------------------------------------------------------------------------
struct AudioSink {
  // duty_pct is 1..50, the fraction of the period the pin is driven high, and
  // it is how loud the note is. See THE VOLUME below.
  void (*tone_on)(uint16_t hz, uint8_t duty_pct);
  void (*tone_off)(void);
};

// -----------------------------------------------------------------------------
//  THE VOLUME, AND WHY A ONE-VOICE PIEZO HAS ONE AT ALL (P10-C9).
//
//  A passive piezo has no amplifier and no envelope: the pin is high or it is
//  low, at one supply voltage. What CAN be varied is how much of each period it
//  spends high. ledcWriteTone() drives a 50 % square wave, which is the loudest
//  thing this circuit can do; narrowing the pulse delivers less energy per
//  cycle into a capacitive sounder and it gets quieter. That is the whole
//  mechanism, and it is why three levels and not sixteen: the relationship is
//  compressive and nobody can hear a hundred steps of it.
//
//  IT IS NOT A LINEAR VOLUME CONTROL and this header will not pretend it is.
//  The pitch does not change - the timer frequency is untouched - but the
//  timbre does, because a narrow pulse is harmonically richer than a square.
//  Low will sound thinner as well as quieter.
//
//  NOT ONE OF THE NUMBERS BELOW HAS BEEN HEARD. No piezo is fitted to the board
//  this firmware has run on, so kVolDuty in audio.cpp is arithmetic that a host
//  test proves MONOTONIC and nothing has proved AUDIBLE. docs/bench.md carries
//  the item. Expect to retune the middle one.
//
//  MUTE IS STILL SEPARATE. audio_play() drops while muted whatever the volume
//  is, and a volume of SND_VOL_LOW is never silence - the row that shows the
//  player one ring of four is ui/screen_settings.cpp's, not this file's.
// -----------------------------------------------------------------------------

// The level hook answers SoundVol (core/nt_types.h): 0 = loudest. Bound the
// same way and for the same reason as the mute question - the value lives in a
// Config this file must not include. nullptr reads as SND_VOL_HIGH.
void    audio_bind_volume(uint8_t (*vol)(void));

// The duty the NEXT note will be emitted at, 1..50. For tests and for DIAG;
// nothing in the firmware needs to ask.
uint8_t audio_duty(void);

// -----------------------------------------------------------------------------
//  LIFECYCLE
// -----------------------------------------------------------------------------

// Set the sink and the mute question. THE ONE WAY to set either, so there is
// no second copy of the mute flag to go stale and no order in which begin and
// bind disagree. `sink` may be nullptr, which runs the queue and the step clock
// with no output at all; `muted` may be nullptr, which reads as "never muted".
// The firmware binds a hook that reads CF_MUTE off the live Config, so toggling
// sound in SETTINGS takes effect on the next cue.
void audio_bind(const AudioSink* sink, bool (*muted)(void));

// The LEDC sink that drives PIN_PIEZO, or nullptr on a host build - which is
// what keeps the GPIO out of the engine and out of every caller. app.cpp binds
// it; a test never sees it.
const AudioSink* audio_device_sink(void);

// Clear the queue, forget any effect in flight and put the pin in its idle
// state through whatever is bound. Call it AFTER audio_bind(), the way
// input_begin() follows the pin map: on the device that is what makes the
// sounder electrically quiet from the first millisecond of setup().
void audio_begin(void);

// -----------------------------------------------------------------------------
//  PLAYING
// -----------------------------------------------------------------------------

// Queue one effect. Returns false and queues NOTHING when muted, when `sfx` is
// out of range, and when the queue is full - the newest is what gets dropped,
// so an effect that has already started always finishes.
bool audio_play(uint8_t sfx);

// Advance the engine. Non-blocking: at most one step per call, so a caller that
// stalls stretches an effect rather than skipping notes nobody would hear.
// Call it once per loop; it is cheap and it is a no-op while idle.
void audio_service(uint32_t now_ms);

// Stop everything now: the queue is emptied, any effect in flight ends and the
// pin goes idle. Idempotent. This is what a power state change calls.
void audio_stop(void);

// -----------------------------------------------------------------------------
//  QUERIES (the firmware uses the first; the rest exist for tests and DIAG)
// -----------------------------------------------------------------------------
bool     audio_busy(void);      // an effect is in flight right now
uint8_t  audio_queued(void);    // effects waiting behind it, 0 .. AUDIO_QUEUE_LEN
bool     audio_muted(void);     // what the bound hook answers, or false

// Total length of one effect, milliseconds, summed over its steps. 0 for
// SFX_NONE and for anything out of range.
uint16_t sfx_duration_ms(uint8_t sfx);

#endif  // ER_AUDIO_H
