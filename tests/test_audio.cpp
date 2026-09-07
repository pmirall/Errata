// =============================================================================
//  Pebblebol host tests - test_audio.cpp
//  THE TONE ENGINE (P6-C1; hardware/audio.{h,cpp}, hardware spec 18 and 19).
//
//  hardware/audio.cpp reaches the piezo through a two-function AudioSink, so
//  every one of these cases binds a sink that RECORDS instead of sounding and
//  then asserts the emitted sequence note by note - the queue, the drop, the
//  step clock, both halves of CF_MUTE and "the pin is idle between effects" are
//  all decided on the host, with no board and no LEDC. That is the same split
//  minigames/games/*_logic.cpp has against *_draw.cpp, and it is why these
//  cases can fail at all.
//
//  WHAT THE RECORDER STORES. One entry per sink call: the frequency for
//  tone_on, and 0 for tone_off. So a whole effect reads as a little sequence
//  ending in 0, and "every effect ends with the pin idle" is something an
//  assertion can see rather than something a comment claims.
//
//  THE STEP TABLE'S NUMBERS ARE NOT COPIED HERE. Two lists of the same numbers
//  is this repository's recurring defect; what is pinned instead is the SHAPE
//  of each effect (a rise ascends, a fall is the rise reversed, a double beep
//  has a real silence between two equal notes) and the section 18 duration BAND
//  each one has to stay inside. A retune is free to move a note; it is not free
//  to turn a rise into a fall or to walk an effect out of its band.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "hardware/audio.h"

// -----------------------------------------------------------------------------
//  0. THE RECORDING SINK
// -----------------------------------------------------------------------------
#define REC_MAX 128

static uint16_t g_rec[REC_MAX];
static uint16_t g_rec_n    = 0;
static uint16_t g_on_calls = 0;
static uint16_t g_off_calls = 0;
static bool     g_muted    = false;

static void rec_on(uint16_t hz) {
  ++g_on_calls;
  if (g_rec_n < REC_MAX) g_rec[g_rec_n++] = hz;
}
static void rec_off(void) {
  ++g_off_calls;
  if (g_rec_n < REC_MAX) g_rec[g_rec_n++] = 0u;
}
static const AudioSink kRec = { rec_on, rec_off };

static bool muted_hook(void) { return g_muted; }

static void rec_clear(void) { g_rec_n = 0; g_on_calls = 0; g_off_calls = 0; }

// Bind the recorder, reset the engine, then clear the log: audio_begin() idles
// the pin THROUGH the bound sink, so clearing afterwards is what keeps every
// case from starting with a stray tone_off in its sequence.
static void fresh(bool muted) {
  g_muted = muted;
  audio_bind(&kRec, &muted_hook);
  audio_begin();
  rec_clear();
}

// Service once per millisecond for `ms` milliseconds, starting at *t. This is
// far faster than the firmware's loop and is deliberately so: it makes the step
// boundaries exact, so a duration assertion is an equality and not a window.
static void pump(uint32_t* t, uint32_t ms) {
  for (uint32_t i = 0; i < ms; ++i) { audio_service(*t); ++(*t); }
}

// Play one effect alone and leave its whole sequence in g_rec[].
static void record_alone(uint8_t sfx) {
  fresh(false);
  CHECK(audio_play(sfx));
  uint32_t t = 0;
  pump(&t, (uint32_t)sfx_duration_ms(sfx) + 8u);
  CHECK(!audio_busy());
}

// The last entry of the recorded sequence, or a sentinel that is not a legal
// emission when nothing was recorded at all. EVERY read of g_rec[] goes through
// this or through a bounds-checked index: a CHECK that fails does NOT stop the
// case, so an assertion followed by a bare g_rec[g_rec_n - 1] turns a mutation
// this file is meant to NAME into a segfault. Measured - removing the tone_off
// from end_effect() crashed the binary before this existed, which reports as
// "exit -11" and as no named test at all.
#define REC_NONE 0xFFFFu
static uint16_t rec_at_end(uint16_t back) {
  return (g_rec_n > back) ? g_rec[g_rec_n - 1u - back] : REC_NONE;
}

// The notes of the sequence currently in g_rec[], trailing tone_off dropped.
static uint16_t notes_of(uint16_t* out, uint16_t cap) {
  uint16_t n = 0;
  for (uint16_t i = 0; i + 1u < g_rec_n && n < cap; ++i) out[n++] = g_rec[i];
  return n;
}

// -----------------------------------------------------------------------------
//  1. THE EMITTED SEQUENCE
// -----------------------------------------------------------------------------

// The property the plan bullet states in words - "noTone() on every effect end"
// - as something the recorder can see. It also refuses an effect whose LAST
// step is a silence: the engine emits a tone_off at the end regardless, so a
// trailing 0 Hz step would be a note that changes nothing, and the table would
// have grown a step nobody could hear.
TEST(every_effect_ends_with_one_tone_off_after_a_real_note) {
  for (uint8_t sfx = (uint8_t)SFX_NONE + 1u; sfx < (uint8_t)SFX_COUNT; ++sfx) {
    record_alone(sfx);
    CHECK(g_rec_n >= 2u);
    CHECK_EQ((int)rec_at_end(0), 0);               // ends idle
    CHECK(rec_at_end(1) != 0u);                    // ...right after a real note
    // One tone_off per silence step, plus exactly one for the end.
    uint16_t silences = 0;
    for (uint16_t i = 0; i + 1u < g_rec_n; ++i) if (g_rec[i] == 0u) ++silences;
    CHECK_EQ((int)g_off_calls, (int)(silences + 1u));
  }
}

TEST(beep_is_one_note_and_chirp_is_two_that_rise) {
  uint16_t n[REC_MAX] = { 0 };
  record_alone(SFX_BEEP);
  CHECK_EQ((int)notes_of(n, REC_MAX), 1);

  record_alone(SFX_CHIRP);
  CHECK_EQ((int)notes_of(n, REC_MAX), 2);
  CHECK(n[1] > n[0]);
}

// A buzz is a buzz because it is the lowest thing the vocabulary can say. If a
// retune lifts it above any other effect's lowest note it has stopped being a
// buzz whatever the enumerator is still called.
TEST(buzz_is_one_note_and_the_lowest_in_the_whole_vocabulary) {
  uint16_t n[REC_MAX] = { 0 };
  record_alone(SFX_BUZZ);
  CHECK_EQ((int)notes_of(n, REC_MAX), 1);
  const uint16_t buzz = n[0];

  for (uint8_t sfx = (uint8_t)SFX_NONE + 1u; sfx < (uint8_t)SFX_COUNT; ++sfx) {
    if (sfx == (uint8_t)SFX_BUZZ) continue;
    record_alone(sfx);
    const uint16_t cnt = notes_of(n, REC_MAX);
    for (uint16_t i = 0; i < cnt; ++i) if (n[i] != 0u) CHECK(n[i] > buzz);
  }
}

// The pair that is easiest to swap in the table and hardest to notice on a
// device nobody has heard yet: they are the same ladder walked in opposite
// directions, so each is also the other's check.
TEST(rise_ascends_fall_descends_and_each_is_the_other_reversed) {
  uint16_t up[REC_MAX] = { 0 }, down[REC_MAX] = { 0 };
  record_alone(SFX_RISE);
  const uint16_t nu = notes_of(up, REC_MAX);
  record_alone(SFX_FALL);
  const uint16_t nd = notes_of(down, REC_MAX);

  CHECK(nu >= 3u);
  CHECK_EQ((int)nu, (int)nd);
  for (uint16_t i = 0; i + 1u < nu; ++i) CHECK(up[i + 1u]   > up[i]);
  for (uint16_t i = 0; i + 1u < nd; ++i) CHECK(down[i + 1u] < down[i]);
  for (uint16_t i = 0; i < nu; ++i)      CHECK_EQ((int)down[i], (int)up[nu - 1u - i]);
}

// Two equal notes with a REAL silence between them - not one long note, and not
// two notes back to back. Dropping the middle step is the mutation this case
// exists for, and it is invisible to a duration check because the note either
// side would simply be longer.
TEST(double_beep_is_two_equal_notes_around_a_silence) {
  record_alone(SFX_DOUBLE_BEEP);
  CHECK_EQ((int)g_rec_n, 4);
  CHECK(g_rec[0] != 0u);
  CHECK_EQ((int)g_rec[1], 0);
  CHECK_EQ((int)g_rec[2], (int)g_rec[0]);
  CHECK_EQ((int)g_rec[3], 0);
}

// A glitch has to jump. A monotone ladder with the right total duration would
// pass every other case in this file.
TEST(glitch_alternates_direction_on_every_note) {
  uint16_t n[REC_MAX] = { 0 };
  record_alone(SFX_GLITCH);
  const uint16_t cnt = notes_of(n, REC_MAX);
  CHECK(cnt >= 4u);
  for (uint16_t i = 0; i + 2u < cnt; ++i) {
    const bool a = n[i + 1u] > n[i];
    const bool b = n[i + 2u] > n[i + 1u];
    CHECK(a != b);
  }
}

// -----------------------------------------------------------------------------
//  2. THE STEP CLOCK
// -----------------------------------------------------------------------------

// The engine has no clock of its own, so "how long is an effect" is answerable
// only by driving it. sfx_duration_ms() is the table's own sum; this is what
// the machine actually does with it.
TEST(an_effect_ends_exactly_its_table_duration_after_it_starts) {
  for (uint8_t sfx = (uint8_t)SFX_NONE + 1u; sfx < (uint8_t)SFX_COUNT; ++sfx) {
    fresh(false);
    CHECK(audio_play(sfx));
    uint32_t t = 0;
    audio_service(t);                    // t = 0: the effect starts here
    CHECK(audio_busy());
    const uint16_t want = sfx_duration_ms(sfx);
    CHECK(want > 0u);
    for (uint32_t i = 1; i < (uint32_t)want; ++i) {
      audio_service(i);
      CHECK(audio_busy());               // not one millisecond early
    }
    audio_service((uint32_t)want);
    CHECK(!audio_busy());                // and not one late
    (void)t;
  }
}

// A caller that stalls must STRETCH an effect, never skip a note: on a piezo a
// "caught up" step is a note nobody hears, which is the same argument
// ui/screen_battle.cpp's enter_beat() makes about battle beats.
//
// THE PER-CALL ASSERTION IS THE HALF THAT MATTERS AND IT WAS MISSING. Written
// first with only the sequence comparison below, this case PASSED a mutation
// that replaced the one-step-per-call rule with a catch-up `while` loop over an
// accumulated step clock - because that mutant emits exactly the same notes in
// exactly the same order, all inside the first call. The sequence check can see
// a skipped note and cannot see a stretch, and the case is named for both. One
// emission per service call, whatever the gap, is what says the rest.
TEST(a_stalled_caller_stretches_an_effect_and_skips_no_note) {
  uint16_t want[REC_MAX] = { 0 };
  record_alone(SFX_GLITCH);
  const uint16_t wn = g_rec_n;
  memcpy(want, g_rec, sizeof(uint16_t) * wn);

  fresh(false);
  CHECK(audio_play(SFX_GLITCH));
  uint32_t t = 0;
  for (uint16_t i = 0; i < wn; ++i) {
    audio_service(t);
    t += 10000u;                         // a ten second stall between calls
    CHECK_EQ((int)g_rec_n, (int)(i + 1u));
  }
  CHECK_EQ((int)g_rec_n, (int)wn);
  for (uint16_t i = 0; i < wn && i < g_rec_n; ++i) CHECK_EQ((int)g_rec[i], (int)want[i]);
  CHECK(!audio_busy());
}

// Every comparison in audio.cpp is (uint32_t)(now - then) >= x, so the 49.7-day
// millis() wrap must be invisible.
//
// THE STARTING OFFSET IS LOAD-BEARING AND THE FIRST VERSION HAD IT WRONG.
// Written as `2^32 - 40` against a 40 ms first step, this case PASSED a mutation
// that rewrote the guard as `now < then + ms` - because at that offset the
// deadline lands exactly on 2^32 - 1 and `now` has wrapped by the time the
// comparison could disagree, so the naive form and the wrap-safe form give the
// same answer at every step. The offset has to be SMALLER than the first step,
// so the deadline wraps while `now` has not: at 2^32 - 20 the mutant computes a
// deadline of 19, finds `now` (0xFFFFFFEC) is not less than it, and ends a 40 ms
// note after one millisecond.
TEST(the_step_clock_survives_the_millis_wrap) {
  fresh(false);
  CHECK(audio_play(SFX_RISE));
  const uint16_t want = sfx_duration_ms(SFX_RISE);
  uint32_t t = 0xFFFFFFFFu - 20u;
  audio_service(t);
  CHECK(audio_busy());
  for (uint16_t i = 1; i < want; ++i) { audio_service(t + i); CHECK(audio_busy()); }
  audio_service(t + want);
  CHECK(!audio_busy());
  CHECK_EQ((int)rec_at_end(0), 0);
}

// -----------------------------------------------------------------------------
//  3. THE QUEUE (hardware spec 18: "use a short event queue")
// -----------------------------------------------------------------------------

TEST(the_queue_holds_audio_queue_len_and_refuses_the_newest_beyond_it) {
  fresh(false);
  for (uint8_t i = 0; i < (uint8_t)AUDIO_QUEUE_LEN; ++i) {
    CHECK(audio_play(SFX_BEEP));
    CHECK_EQ((int)audio_queued(), (int)(i + 1u));
  }
  CHECK(!audio_play(SFX_BEEP));                       // the fifth is dropped
  CHECK_EQ((int)audio_queued(), (int)AUDIO_QUEUE_LEN);
  CHECK_EQ((int)g_rec_n, 0);                          // and nothing sounded yet
}

// FIFO, and every queued effect is played WHOLE. The reference is each effect
// recorded on its own, concatenated - so a queue that dropped the oldest, or
// re-ordered, or truncated an effect when the next one arrived, fails here.
TEST(a_full_queue_plays_every_effect_whole_and_in_order) {
  const uint8_t plan[4] = { SFX_BEEP, SFX_CHIRP, SFX_BUZZ, SFX_RISE };
  static_assert((int)AUDIO_QUEUE_LEN == 4, "this case is written for a queue of four");

  uint16_t want[REC_MAX] = { 0 };
  uint16_t wn = 0;
  uint32_t total = 0;
  for (uint8_t i = 0; i < 4u; ++i) {
    record_alone(plan[i]);
    for (uint16_t j = 0; j < g_rec_n; ++j) want[wn++] = g_rec[j];
    total += (uint32_t)sfx_duration_ms(plan[i]);
  }

  fresh(false);
  for (uint8_t i = 0; i < 4u; ++i) CHECK(audio_play(plan[i]));
  CHECK(!audio_play(SFX_GLITCH));                     // the fifth, refused
  uint32_t t = 0;
  pump(&t, total + 64u);
  CHECK(!audio_busy());
  CHECK_EQ((int)audio_queued(), 0);
  CHECK_EQ((int)g_rec_n, (int)wn);
  for (uint16_t i = 0; i < wn && i < g_rec_n; ++i) CHECK_EQ((int)g_rec[i], (int)want[i]);
}

// The reason the NEWEST is what gets dropped rather than the oldest: an effect
// that has started has to be able to finish, and a queue that slides would let
// a burst of events keep truncating whatever the player is currently hearing.
TEST(a_burst_of_plays_cannot_truncate_the_effect_in_flight) {
  fresh(false);
  CHECK(audio_play(SFX_RISE));
  uint32_t t = 0;
  audio_service(t);                                   // RISE is now in flight
  CHECK(g_rec_n > 0u);
  const uint16_t first = (g_rec_n > 0u) ? g_rec[0] : REC_NONE;
  for (uint8_t i = 0; i < 20u; ++i) (void)audio_play(SFX_BUZZ);
  CHECK_EQ((int)audio_queued(), (int)AUDIO_QUEUE_LEN);

  uint16_t want[REC_MAX] = { 0 };
  record_alone(SFX_RISE);
  const uint16_t wn = g_rec_n;
  CHECK(wn > 0u);
  memcpy(want, g_rec, sizeof(uint16_t) * wn);
  CHECK_EQ((int)(wn > 0u ? want[0] : REC_NONE), (int)first);

  fresh(false);
  CHECK(audio_play(SFX_RISE));
  t = 0;
  audio_service(t++);
  for (uint8_t i = 0; i < 20u; ++i) (void)audio_play(SFX_BUZZ);
  pump(&t, (uint32_t)sfx_duration_ms(SFX_RISE) + 4u);
  // The RISE's own sequence comes out at the FRONT, untouched and whole. The
  // log is longer than wn here on purpose: by the time the rise has finished,
  // one of the twenty buzzes behind it has already started, which is the other
  // half of the promise.
  CHECK(g_rec_n > wn);
  for (uint16_t i = 0; i < wn && i < g_rec_n; ++i) CHECK_EQ((int)g_rec[i], (int)want[i]);
}

TEST(sfx_none_and_anything_out_of_range_are_refused) {
  fresh(false);
  CHECK(!audio_play((uint8_t)SFX_NONE));
  CHECK(!audio_play((uint8_t)SFX_COUNT));
  CHECK(!audio_play(200u));
  CHECK_EQ((int)audio_queued(), 0);
  CHECK_EQ((int)sfx_duration_ms((uint8_t)SFX_NONE), 0);
  CHECK_EQ((int)sfx_duration_ms((uint8_t)SFX_COUNT), 0);
  CHECK_EQ((int)sfx_duration_ms(200u), 0);
}

TEST(audio_stop_ends_the_effect_idles_the_pin_and_empties_the_queue) {
  fresh(false);
  for (uint8_t i = 0; i < 3u; ++i) CHECK(audio_play(SFX_RISE));
  uint32_t t = 0;
  pump(&t, 45u);                                      // mid-effect, mid-queue
  CHECK(audio_busy());
  const uint16_t before = g_off_calls;

  audio_stop();
  CHECK(!audio_busy());
  CHECK_EQ((int)audio_queued(), 0);
  CHECK_EQ((int)g_off_calls, (int)(before + 1u));     // the pin was idled once
  CHECK_EQ((int)rec_at_end(0), 0);

  const uint16_t n = g_rec_n;
  pump(&t, 2000u);
  CHECK_EQ((int)g_rec_n, (int)n);                     // and nothing came back
  audio_stop();                                       // idempotent
  CHECK_EQ((int)g_rec_n, (int)n);
}

// -----------------------------------------------------------------------------
//  4. CF_MUTE - both halves
// -----------------------------------------------------------------------------

TEST(a_play_made_while_muted_is_refused_and_never_reaches_the_queue) {
  fresh(true);
  CHECK(audio_muted());
  for (uint8_t sfx = (uint8_t)SFX_NONE + 1u; sfx < (uint8_t)SFX_COUNT; ++sfx) {
    CHECK(!audio_play(sfx));
  }
  CHECK_EQ((int)audio_queued(), 0);
  uint32_t t = 0;
  pump(&t, 3000u);
  CHECK(!audio_busy());
  CHECK_EQ((int)g_rec_n, 0);
  CHECK_EQ((int)g_on_calls, 0);
}

// The half that is easy to get wrong: refusing at the door but letting a note
// that is already sounding run to its end. A player who reaches for the mute
// toggle wants silence now, and on a device with one speaker "now" is the next
// service call.
TEST(mute_arriving_mid_effect_silences_the_pin_on_the_next_service_call) {
  fresh(false);
  CHECK(audio_play(SFX_RISE));
  CHECK(audio_play(SFX_GLITCH));
  uint32_t t = 0;
  pump(&t, 50u);
  CHECK(audio_busy());
  CHECK(g_on_calls > 0u);
  const uint16_t on_before  = g_on_calls;
  const uint16_t off_before = g_off_calls;

  g_muted = true;
  audio_service(t++);
  CHECK(!audio_busy());
  CHECK_EQ((int)audio_queued(), 0);                   // what was waiting is gone
  CHECK_EQ((int)g_off_calls, (int)(off_before + 1u));
  CHECK_EQ((int)rec_at_end(0), 0);

  pump(&t, 3000u);
  CHECK_EQ((int)g_on_calls, (int)on_before);          // and stays silent
}

// Un-muting must not hand the player a burst of everything they silenced. This
// is the case that separates "refused" from "deferred", and it is the whole
// reason audio_play() drops rather than queues.
TEST(unmuting_never_replays_what_was_refused_while_muted) {
  fresh(true);
  for (uint8_t i = 0; i < 8u; ++i) (void)audio_play(SFX_CHIRP);
  g_muted = false;
  CHECK(!audio_muted());
  uint32_t t = 0;
  pump(&t, 2000u);
  CHECK_EQ((int)g_rec_n, 0);
  CHECK(!audio_busy());
  CHECK(audio_play(SFX_CHIRP));                       // but the next one plays
  pump(&t, (uint32_t)sfx_duration_ms(SFX_CHIRP) + 4u);
  CHECK(g_rec_n > 0u);
}

// -----------------------------------------------------------------------------
//  5. BINDING
// -----------------------------------------------------------------------------

// The null checks inside emit_on/emit_off are not decoration: a host build
// answers nullptr from audio_device_sink(), so the firmware's own binding line
// is the only thing between the engine and a null call. The queue and the clock
// must run identically with nothing bound.
TEST(an_unbound_sink_still_runs_the_queue_and_the_step_clock) {
  audio_bind(nullptr, nullptr);
  audio_begin();
  CHECK(!audio_muted());
  CHECK(audio_play(SFX_FALL));
  const uint16_t want = sfx_duration_ms(SFX_FALL);
  for (uint32_t i = 0; i < (uint32_t)want; ++i) { audio_service(i); CHECK(audio_busy()); }
  audio_service((uint32_t)want);
  CHECK(!audio_busy());
}

TEST(the_host_build_has_no_device_sink_to_bind) {
  // If this ever answers non-null on the host, hardware/audio.cpp has grown a
  // GPIO the tests are pretending to drive.
  CHECK(audio_device_sink() == nullptr);
}

// Re-binding while a note sounds has to stop it on the sink that is playing it:
// the new sink cannot silence a pin it never drove.
TEST(rebinding_stops_the_effect_on_the_sink_that_was_playing_it) {
  fresh(false);
  CHECK(audio_play(SFX_BUZZ));
  CHECK(audio_play(SFX_BUZZ));
  uint32_t t = 0;
  audio_service(t);
  CHECK(audio_busy());
  const uint16_t off_before = g_off_calls;

  audio_bind(&kRec, &muted_hook);
  CHECK_EQ((int)g_off_calls, (int)(off_before + 1u));
  CHECK(!audio_busy());
  CHECK_EQ((int)audio_queued(), 0);
}

// -----------------------------------------------------------------------------
//  6. THE SECTION 18 DURATION BANDS
//
//  Spec section 18 gives four bands by EVENT (UI click 30-80 ms, damage 50-150,
//  capture 150-300, evolution 500-1500) and hardware/audio.h maps each word of
//  the vocabulary into one of them without repeating the numbers. This is where
//  the mapping lives, and it is what a retune has to keep true.
//
//  No single effect is in the evolution band on purpose: ui/ceremony.cpp arms
//  one cue per phase edge across 4,480 ms, so the evolution sound is a sequence
//  of this vocabulary rather than one note the queue would have to hold.
// -----------------------------------------------------------------------------
struct SfxBand { uint8_t sfx; uint16_t lo; uint16_t hi; const char* band; };

static const SfxBand kBands[] = {
  { SFX_BEEP,         30u,  80u, "UI click" },
  { SFX_CHIRP,        30u,  80u, "UI click" },
  { SFX_BUZZ,         50u, 150u, "damage"   },
  { SFX_DOUBLE_BEEP,  50u, 150u, "damage"   },
  { SFX_GLITCH,       50u, 150u, "damage"   },
  { SFX_RISE,        150u, 300u, "capture"  },
  { SFX_FALL,        150u, 300u, "capture"  },
  // TICK IS DELIBERATELY BELOW THE UI-CLICK BAND. Spec 18's 30-80 ms is for a
  // click that CONFIRMS; a tick marks a keystroke or a cursor and fires dozens
  // of times in a row, so it has its own band and the band is the point.
  { SFX_TICK,          5u,  20u, "keystroke" },
  // FANFARE is above the capture band on purpose: the held final note is what
  // separates "arriving" from "going up", and a hold that fits inside SFX_RISE's
  // ceiling is not a hold.
  { SFX_FANFARE,     250u, 450u, "arrival"  },
};

TEST(every_effect_is_inside_the_spec_18_band_it_was_written_for) {
  const uint16_t n = (uint16_t)(sizeof(kBands) / sizeof(kBands[0]));
  // EVERY effect but SFX_NONE, and no extras. The count is derived from the
  // enum so adding one without a band fails here rather than shipping unbanded.
  CHECK_EQ((int)n, (int)SFX_COUNT - 1);
  bool seen[SFX_COUNT] = { false };
  for (uint16_t i = 0; i < n; ++i) {
    const uint16_t d = sfx_duration_ms(kBands[i].sfx);
    CHECK(d >= kBands[i].lo);
    CHECK(d <= kBands[i].hi);
    CHECK(!seen[kBands[i].sfx]);
    seen[kBands[i].sfx] = true;
  }
  for (uint8_t s = (uint8_t)SFX_NONE + 1u; s < (uint8_t)SFX_COUNT; ++s) CHECK(seen[s]);
}
