// =============================================================================
//  ERRATA - hardware/audio.cpp
//  The tone engine. See audio.h for the contract; this file is the table, the
//  queue and the step clock, plus the device sink at the bottom.
//
//  DESIGN NOTES (the reasons this file looks the way it does)
//
//  1. THE WHOLE ENGINE IS HOST-COMPILABLE. Nothing above the `#if
//     defined(ARDUINO)` block at the end includes an Arduino header or names a
//     GPIO, so tests/test_audio.cpp drives every path - the queue, the drop,
//     both halves of the mute rule and the note-by-note emission of all seven
//     effects - with no board. hardware/input.cpp has kept its platform half
//     behind the same guard since P1.
//
//  2. ONE STEP PER SERVICE CALL, and the step clock is RESET TO NOW rather than
//     accumulated. A call that arrives late plays its step for the full
//     duration instead of owing the engine the notes the gap swallowed. The
//     alternative catches up by emitting several steps in one call, which on a
//     piezo means emitting notes that are never heard - the same argument
//     ui/screen_battle.cpp's enter_beat() makes about battle beats, and the
//     same conclusion.
//
//  3. EVERY TIME COMPARISON IS (uint32_t)(now - then) >= X, so the engine
//     survives the 49.7-day millis() wrap. There is no other clock here: the
//     caller owns the milliseconds.
//
//  4. NO HEAP, NO FLOAT, NO BLOCKING. The step tables are `static const` and
//     live in flash; the mutable state below is 20-odd bytes of .bss. Globals
//     are the scarce axis on this device (docs/budget.md), which is also why
//     the queue is four bytes and not a ring of structs.
// =============================================================================
#include "audio.h"

// -----------------------------------------------------------------------------
//  1. THE TABLE
//
//  One flat array of steps plus a {first, count} index per effect, rather than
//  seven arrays: it is one relocation instead of seven and it makes the total
//  step count something a static_assert can hold.
//
//  A step of 0 Hz is a SILENCE inside the effect - emitted as tone_off, which
//  is why DOUBLE_BEEP reads as two notes and not as one wobble. NO EFFECT ENDS
//  ON A SILENCE: the engine emits tone_off at the end of every effect anyway,
//  so a trailing silence step would be a note that does nothing, and
//  tests/test_audio.cpp refuses one by name.
// -----------------------------------------------------------------------------
struct SfxStep {
  uint16_t hz;   // 0 = silence
  uint16_t ms;   // > 0
};

static const SfxStep kSteps[] = {
  // SFX_BEEP - the UI click. One note, high enough to cut through a pocket.
  { 2000,  60 },
  // SFX_CHIRP - two very short notes, the second higher. "Something arrived."
  { 2600,  25 }, { 3300,  25 },
  // SFX_BUZZ - low, long, and deliberately ugly. "No", and "that hurt".
  {  180, 140 },
  // SFX_RISE - five ascending steps. Capture, level-up, the reveal.
  {  880,  40 }, { 1175,  40 }, { 1568,  40 }, { 2093,  40 }, { 2637,  40 },
  // SFX_FALL - the same ladder walked down. Fainting, losing something.
  { 2637,  40 }, { 2093,  40 }, { 1568,  40 }, { 1175,  40 }, {  880,  40 },
  // SFX_DOUBLE_BEEP - note, silence, note. Confirmed, named, done.
  { 2200,  45 }, {    0,  45 }, { 2200,  45 },
  // SFX_GLITCH - six alternating extremes. Corruption, and errors.
  { 3100,  25 }, {  420,  25 }, { 2600,  25 },
  {  520,  25 }, { 3400,  25 }, {  330,  25 },
  // SFX_TICK - one click, as short as the step clock can express. Deliberately
  // ABOVE SFX_BEEP and a fifth of its length: a keystroke repeated forty times
  // must not sound like forty confirmations.
  { 3600,  12 },
  // SFX_FANFARE - four rising notes and a fifth held three times as long. The
  // hold is the whole difference from SFX_RISE, which is a flat five-step
  // slide: a slide says "going up", a resolution says "arrived".
  { 1046,  45 }, { 1318,  45 }, { 1568,  45 }, { 2093,  45 }, { 2637, 140 },
};

// Index by SfxId. SFX_NONE owns no steps, which is what makes it unplayable
// without a special case anywhere below.
static constexpr uint8_t kFirst[SFX_COUNT] = { 0,  0,  1,  3,  4,  9, 14, 17, 23, 24 };
static constexpr uint8_t kCount[SFX_COUNT] = { 0,  1,  2,  1,  5,  5,  3,  6,  1,  5 };

static_assert(sizeof(kSteps) / sizeof(kSteps[0]) == 29,
              "the step table and the {first,count} index have drifted apart");
static_assert(kFirst[SFX_FANFARE] + kCount[SFX_FANFARE]
                == (uint8_t)(sizeof(kSteps) / sizeof(kSteps[0])),
              "the last effect does not end at the end of the step table");
// EVERY effect owns a contiguous, non-overlapping run, checked here rather than
// by reading the two rows above and trusting them - adding SFX_TICK and
// SFX_FANFARE meant editing three literals in three places, which is exactly
// the shape that goes wrong quietly.
static_assert([]{
  uint8_t at = 0;
  for (uint8_t i = 1; i < (uint8_t)SFX_COUNT; ++i) {
    if (kFirst[i] != at) return false;
    at = (uint8_t)(at + kCount[i]);
  }
  return at == (uint8_t)(sizeof(kSteps) / sizeof(kSteps[0]));
}(), "the effects do not tile the step table exactly once each");

// -----------------------------------------------------------------------------
//  2. STATE. Everything mutable in this file is here, and it is all .bss.
// -----------------------------------------------------------------------------
static const AudioSink* s_sink     = nullptr;
static bool           (*s_muted)(void) = nullptr;

static uint8_t  s_q[AUDIO_QUEUE_LEN];  // SfxId, oldest at s_qhead
static uint8_t  s_qhead   = 0;
static uint8_t  s_qcount  = 0;

static uint8_t  s_cur     = SFX_NONE;  // the effect in flight
static uint8_t  s_step    = 0;         // which of its steps is sounding
static uint32_t s_step_ms = 0;         // when that step started
static bool     s_driven  = false;     // the pin is being driven right now

// -----------------------------------------------------------------------------
//  3. EMISSION. The only two places the sink is touched.
// -----------------------------------------------------------------------------
static inline void emit_off(void) {
  s_driven = false;
  if (s_sink && s_sink->tone_off) s_sink->tone_off();
}

static inline void emit_step(const SfxStep& st) {
  if (st.hz == 0u) { emit_off(); return; }
  s_driven = true;
  if (s_sink && s_sink->tone_on) s_sink->tone_on(st.hz);
}

// End whatever is in flight, with the pin idle. Called on the natural end, on
// audio_stop() and when mute arrives mid-note, so "tone_off on every effect
// end" is one line and not three.
static void end_effect(void) {
  s_cur  = SFX_NONE;
  s_step = 0;
  emit_off();
}

// -----------------------------------------------------------------------------
//  4. LIFECYCLE
// -----------------------------------------------------------------------------
void audio_begin(void) {
  s_qhead  = 0;
  s_qcount = 0;
  s_cur    = SFX_NONE;
  s_step   = 0;
  s_step_ms = 0;
  // Idle the pin UNCONDITIONALLY - emit_off() does not ask whether anything
  // was sounding. On the device this is what makes the piezo electrically quiet
  // from setup() onwards (spec section 18); with no sink bound it is a no-op.
  emit_off();
}

void audio_bind(const AudioSink* sink, bool (*muted)(void)) {
  // Anything in flight belongs to the OLD sink; stop it there before the new
  // one takes over, or the old sink is left driving a pin nobody will silence.
  if (s_cur != SFX_NONE || s_driven) end_effect();
  s_qhead  = 0;
  s_qcount = 0;
  s_sink   = sink;
  s_muted  = muted;
}

// -----------------------------------------------------------------------------
//  5. PLAYING
// -----------------------------------------------------------------------------
bool audio_muted(void) { return s_muted ? s_muted() : false; }

bool audio_play(uint8_t sfx) {
  if (sfx == (uint8_t)SFX_NONE || sfx >= (uint8_t)SFX_COUNT) return false;
  // CF_MUTE, first half: a play made while muted is not deferred, it is
  // DROPPED. Queueing it would hand the player a burst of everything they
  // silenced the moment they turned the sound back on.
  if (audio_muted()) return false;
  if (s_qcount >= (uint8_t)AUDIO_QUEUE_LEN) return false;
  s_q[(uint8_t)((s_qhead + s_qcount) % (uint8_t)AUDIO_QUEUE_LEN)] = sfx;
  ++s_qcount;
  return true;
}

void audio_stop(void) {
  s_qhead  = 0;
  s_qcount = 0;
  if (s_cur != SFX_NONE || s_driven) end_effect();
}

void audio_service(uint32_t now_ms) {
  // CF_MUTE, second half: mute arriving mid-effect silences the pin on this
  // call, not at the end of the note.
  if (audio_muted()) { audio_stop(); return; }

  if (s_cur == SFX_NONE) {
    if (s_qcount == 0u) return;
    s_cur    = s_q[s_qhead];
    s_qhead  = (uint8_t)((s_qhead + 1u) % (uint8_t)AUDIO_QUEUE_LEN);
    --s_qcount;
    s_step   = 0;
    s_step_ms = now_ms;
    emit_step(kSteps[kFirst[s_cur]]);
    return;
  }

  const SfxStep& cur = kSteps[kFirst[s_cur] + s_step];
  if ((uint32_t)(now_ms - s_step_ms) < (uint32_t)cur.ms) return;

  const uint8_t next = (uint8_t)(s_step + 1u);
  if (next >= kCount[s_cur]) { end_effect(); return; }
  s_step    = next;
  s_step_ms = now_ms;                       // reset to now, see design note 2
  emit_step(kSteps[kFirst[s_cur] + s_step]);
}

// -----------------------------------------------------------------------------
//  6. QUERIES
// -----------------------------------------------------------------------------
bool    audio_busy(void)   { return s_cur != (uint8_t)SFX_NONE; }
uint8_t audio_queued(void) { return s_qcount; }

uint16_t sfx_duration_ms(uint8_t sfx) {
  if (sfx == (uint8_t)SFX_NONE || sfx >= (uint8_t)SFX_COUNT) return 0u;
  uint16_t total = 0u;
  for (uint8_t i = 0; i < kCount[sfx]; ++i) total = (uint16_t)(total + kSteps[kFirst[sfx] + i].ms);
  return total;
}

// -----------------------------------------------------------------------------
//  7. THE DEVICE SINK. Nothing above this line knows a GPIO exists.
//
//  LEDC, NOT THE CORE'S tone(). Both are in the installed core and the plan
//  bullet allows either, so this is a choice with a reason. Arduino-ESP32
//  3.1.1's Tone.cpp lazily creates a FreeRTOS task with a 3,500 WORD stack
//  (14,000 B) and a 128-entry x 16 B message queue (2,048 B) the first time
//  tone() is called, and its noTone() calls xQueueReset(), which discards a
//  start that has been queued but not yet run. Sixteen kilobytes of heap and a
//  priority-10 task to click a piezo is not a trade this device can make, and
//  the dropped-start race is exactly the kind of defect that would only ever
//  show up on hardware. ledcAttach / ledcWriteTone / ledcDetach cost one LEDC
//  channel while a note sounds and nothing at all between effects.
//
//  Read from the core source at
//  ~/.arduino15/packages/esp32/hardware/esp32/3.1.1/cores/esp32/Tone.cpp; NOT
//  measured on hardware, because nothing in this repository has ever run on a
//  board.
//
//  BETWEEN EFFECTS THE CHANNEL IS RELEASED, not merely set to zero duty. Spec
//  section 18 asks for "GPIO inactive except during sound playback" and
//  "avoid continuous PWM", and the section 66 checklist has a line for it
//  ("piezo output is inactive between effects"). Detach then drive the pin low,
//  so the sounder sits across 0 V rather than across a stopped timer.
// -----------------------------------------------------------------------------
#if defined(ARDUINO)
#include <Arduino.h>

static bool s_attached = false;

static void dev_tone_off(void) {
  if (s_attached) {
    ledcWriteTone(PIN_PIEZO, 0);   // duty 0 first: the pin stops switching
    ledcDetach(PIN_PIEZO);
    s_attached = false;
  }
  pinMode(PIN_PIEZO, OUTPUT);
  digitalWrite(PIN_PIEZO, LOW);
}

static void dev_tone_on(uint16_t hz) {
  if (hz == 0u) { dev_tone_off(); return; }
  if (!s_attached) {
    if (!ledcAttach(PIN_PIEZO, hz, AUDIO_PWM_BITS)) return;
    s_attached = true;
  }
  // Sets the timer to hz and the duty to half the period: a square wave, which
  // is all a passive piezo wants.
  ledcWriteTone(PIN_PIEZO, hz);
}

static const AudioSink kDeviceSink = { dev_tone_on, dev_tone_off };
const AudioSink* audio_device_sink(void) { return &kDeviceSink; }

#else   // !ARDUINO

// A HOST BUILD HAS NO PIEZO AND SAYS SO. Answering nullptr rather than a
// do-nothing sink is deliberate: a test that forgets to bind its recorder gets
// silence it can see in the assertions, not a sink that swallowed everything.
const AudioSink* audio_device_sink(void) { return nullptr; }

#endif  // ARDUINO
