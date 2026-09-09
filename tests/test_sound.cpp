// =============================================================================
//  Errata host tests - test_sound.cpp
//  "THE SOUND SETTING PERSISTS" (plan P10-C2's third bullet, spec section 64).
//
//  WHAT THIS FILE IS, AND WHY IT IS NOT A FOURTH COPY OF SOMETHING.
//
//  The P10-C2 bullet reads "hardware/audio.h + audio_null.cpp (no buzzer) so a
//  future board can add one without game changes; sound setting persists", and
//  it was written as if neither file existed. BOTH LANDED IN P6-C1 UNDER
//  DECISION D8: a seven-effect tone engine behind a two-function-pointer
//  AudioSink, host-tested note by note in tests/test_audio.cpp, with
//  audio_device_sink() already answering nullptr on a boardless build - which
//  IS "a future board can add one without game changes", and which is why
//  audio_null.cpp is deliberately NOT written (see the phase record). Writing
//  a second tone engine would have been the whole bullet.
//
//  AND THE SETTING ALREADY PERSISTED. Every link was tested:
//    * the two toggles              tests/test_screens.cpp (SETTINGS, HOME)
//    * Config <-> ConfigV2          tests/test_game_state.cpp
//    * the blob through NVS         tests/test_persistence.cpp
//    * CF_MUTE inside the engine    tests/test_audio.cpp
//  AND THE JOIN BETWEEN THEM WAS TESTED NOWHERE, because the join is
//  app_audio_muted() in app/app.cpp - a file NO HOST BINARY COMPILES. That is
//  the exact shape of the phase-6 defect (a default in a dev-only function, the
//  shipping build running sim_tick(0) for four phases) and of the phase-9 one
//  (a composite nothing host-linked could draw). A test that re-wrote
//  `(flags & CF_MUTE) != 0` in its own fixture would have asserted a COPY of
//  the line and proved nothing about the artefact.
//
//  So P10-C2 moved the predicate into core/nt_types.h as cfg_sound_muted(),
//  app.cpp calls IT, tools/check.sh gates that call site, and this binary
//  drives the whole chain end to end: a Config with the bit set is sealed and
//  written through THE REAL save_manager into the fake NVS, loaded back through
//  THE REAL gs_load(), and the Config that comes out is handed to THE REAL
//  audio engine through the same predicate the firmware binds. The assertion is
//  that audio_play() REFUSES - not that a flag survived a memcpy.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "core/nt_types.h"
#include "fakes/kv_mem.h"
#include "fakes/boot_host.h"
#include "hardware/audio.h"
#include "persistence/game_state.h"
#include "persistence/legacy_v1.h"
#include "persistence/save_manager.h"

// -----------------------------------------------------------------------------
//  The recording sink, same shape tests/test_audio.cpp uses: a piezo that
//  writes down what it was asked to do instead of making a noise. Without it
//  "nothing was played" would be indistinguishable from "nothing was bound".
// -----------------------------------------------------------------------------
static uint16_t g_on_calls  = 0;
static uint16_t g_off_calls = 0;
static void rec_on(uint16_t)  { ++g_on_calls; }
static void rec_off(void)     { ++g_off_calls; }
static const AudioSink kRec = { rec_on, rec_off };

// THE LIVE CONFIG, exactly as app/app.cpp holds one, and the hook is the SAME
// FUNCTION app_audio_muted() calls - not a re-statement of it.
static Config g_live;
static bool   live_muted(void) { return cfg_sound_muted(g_live); }

static uint32_t s_ms    = 10000;
static uint32_t s_epoch = 1700300000u;
static uint32_t fake_ms(void)    { return s_ms; }
static uint32_t fake_epoch(void) { return s_epoch; }

static size_t load_file(const char* rel, uint8_t* out, size_t cap) {
  char path[512];
  nt_path(rel, path, sizeof path);
  FILE* f = fopen(path, "rb");
  if (!f) { fprintf(stderr, "  cannot open %s\n", path); return 0; }
  const size_t n = fread(out, 1, cap, f);
  fclose(f);
  return n;
}

// A REAL SAVE HAS TO EXIST BEFORE A CONFIG MEANS ANYTHING, and that is a
// property of the pipeline rather than an inconvenience: save_load_all()
// answers LOAD_FRESH and reads nothing else when KV_MAIN holds no Box, so a
// config on its own is not a save. The committed v1 blobs are the shortest
// route to a populated partition and they are what tests/test_game_state.cpp
// uses for the same reason - a first boot on a real unit writes both.
static bool seed_v1(void) {
  uint8_t save_bytes[sizeof(LegacyPetSave)];
  uint8_t cfg_bytes[sizeof(LegacyConfig)];
  if (load_file("fixtures/petsave_v1_adult.bin", save_bytes, sizeof save_bytes)
      != sizeof save_bytes) return false;
  if (load_file("fixtures/config_v1.bin", cfg_bytes, sizeof cfg_bytes)
      != sizeof cfg_bytes) return false;
  return kv_put(KV_MAIN, KEY_V1_SAVE, save_bytes, sizeof save_bytes) &&
         kv_put(KV_MAIN, KEY_V1_CFG, cfg_bytes, sizeof cfg_bytes);
}

static bool begin(void) {
  kv_mem_reset();
  boot_host_reset();
  s_ms    = 10000;
  s_epoch = 1700300000u;
  save_set_clock(&fake_ms, &fake_epoch);
  gs_set_readonly(false);
  if (!seed_v1()) return false;
  memset(&g_live, 0, sizeof g_live);
  if (gs_load(g_live) != LOAD_MIGRATED) return false;
  audio_bind(&kRec, &live_muted);
  audio_begin();
  g_on_calls = g_off_calls = 0;      // audio_begin() idles the pin through the sink
  // The v1 fixture is not guaranteed to arrive unmuted, and every case below
  // starts from "sound is on".
  g_live.flags = (uint8_t)(g_live.flags & (uint8_t)~CF_MUTE);
  return true;
}

// Boot the way app_setup() does: load, then bind the engine to the Config that
// came back. Returns the LoadResult so a case can say what it booted from.
static LoadResult boot(void) {
  memset(&g_live, 0, sizeof g_live);
  const LoadResult r = gs_load(g_live);
  audio_bind(&kRec, &live_muted);
  audio_begin();
  g_on_calls = g_off_calls = 0;
  return r;
}

#define BEGIN() do { if (!begin()) { CHECK(false); return; } } while (0)

// =============================================================================
//  1. THE WHOLE CHAIN, IN ONE CASE
// =============================================================================
TEST(a_mute_saved_to_flash_still_silences_the_piezo_after_a_reboot) {
  BEGIN();

  // A fresh device makes a noise.
  CHECK(!audio_muted());
  CHECK(audio_play(SFX_BEEP));
  audio_service(s_ms + 1u);
  CHECK(g_on_calls > 0);

  // The user turns sound off in SETTINGS. ui/screen_settings.cpp does exactly
  // this - XOR the bit, then ask for a save - and ui.cpp's cfg_persist() is
  // gs_save_cfg().
  g_live.flags = (uint8_t)(g_live.flags ^ CF_MUTE);
  CHECK(gs_save_cfg(g_live));
  CHECK(audio_muted());                       // the live engine, immediately

  // THE REBOOT. Everything in RAM is gone; only what reached the fake NVS
  // survives, and it comes back through the real loader.
  audio_bind(nullptr, nullptr);
  const LoadResult r = boot();
  CHECK_EQ((int)r, (int)LOAD_OK);
  if ((g_live.flags & CF_MUTE) == 0u)
    fprintf(stderr, "  CF_MUTE did not survive the round trip: flags 0x%02X\n",
            (unsigned)g_live.flags);
  CHECK((g_live.flags & CF_MUTE) != 0u);

  // ...AND THE ASSERTION THAT IS THE POINT OF THE FILE. Not "the bit is set":
  // the engine the firmware ships REFUSES the cue, and the pin is never driven.
  CHECK(audio_muted());
  CHECK(!audio_play(SFX_BEEP));
  CHECK_EQ((int)audio_queued(), 0);
  for (uint32_t t = 0; t < 4000u; t += 10u) audio_service(s_ms + t);
  CHECK_EQ((int)g_on_calls, 0);
  CHECK(!audio_busy());
}

// The other direction, because a predicate stuck at `true` would pass the case
// above and silence a device nobody asked to silence.
TEST(sound_left_on_survives_a_reboot_too_and_the_piezo_still_sounds) {
  BEGIN();
  g_live.flags = 0;                            // sound ON
  CHECK(gs_save_cfg(g_live));

  audio_bind(nullptr, nullptr);
  (void)boot();
  CHECK((g_live.flags & CF_MUTE) == 0u);
  CHECK(!audio_muted());
  CHECK(audio_play(SFX_BEEP));
  audio_service(s_ms + 1u);
  CHECK(g_on_calls > 0);
}

// Turning it back ON is a separate write, and a save path that only ever
// recorded the bit and never cleared it would leave a device permanently mute.
TEST(unmuting_is_persisted_as_well_as_muting) {
  BEGIN();
  g_live.flags = CF_MUTE;
  CHECK(gs_save_cfg(g_live));
  audio_bind(nullptr, nullptr);
  (void)boot();
  CHECK(audio_muted());

  g_live.flags = (uint8_t)(g_live.flags & (uint8_t)~CF_MUTE);
  CHECK(gs_save_cfg(g_live));
  audio_bind(nullptr, nullptr);
  (void)boot();
  CHECK(!audio_muted());
  CHECK(audio_play(SFX_BEEP));
}

// CF_MUTE travels in one bit of ConfigV2 next to CF_WEB_ENABLED and the
// brightness. A codec that dropped a NEIGHBOUR would pass every case above.
TEST(the_sound_bit_survives_beside_the_other_settings_it_shares_a_byte_with) {
  BEGIN();
  g_live.flags          = (uint8_t)(CF_MUTE | CF_WEB_ENABLED);
  g_live.brightness     = 96;
  g_live.statusbar_mode = 2;
  memcpy(g_live.pet_name, "Paketo", 7);
  CHECK(gs_save_cfg(g_live));

  audio_bind(nullptr, nullptr);
  (void)boot();
  CHECK((g_live.flags & CF_MUTE) != 0u);
  CHECK((g_live.flags & CF_WEB_ENABLED) != 0u);
  CHECK_EQ((int)g_live.brightness, 96);
  CHECK_EQ((int)g_live.statusbar_mode, 2);
  CHECK(strcmp(g_live.pet_name, "Paketo") == 0);
  CHECK(!audio_play(SFX_BEEP));
}

// =============================================================================
//  2. THE SEAM THE BULLET ASKED FOR, ANSWERED WHERE IT ALREADY EXISTS
//
//  "so a future board can add one without game changes" is a claim about the
//  AudioSink, and the honest way to check it is to run the engine with NO sink
//  at all - which is what audio_device_sink() answers on a build with no
//  buzzer. A board without a piezo must behave like a board with a silent one:
//  the queue runs, the step clock runs, nothing crashes, and no game rule
//  anywhere had to know. That is why there is no audio_null.cpp.
// =============================================================================
TEST(a_board_with_no_buzzer_runs_the_whole_engine_and_changes_no_game_rule) {
  BEGIN();
  audio_bind(nullptr, &live_muted);      // exactly what audio_device_sink()
  audio_begin();                         // answers when there is no board
  CHECK(!audio_muted());
  CHECK(audio_play(SFX_RISE));           // accepted: the queue is real
  audio_service(s_ms);                   // one step: the effect is now in flight
  CHECK(audio_busy());
  for (uint32_t t = 0; t < 4000u; t += 5u) audio_service(s_ms + t);
  CHECK(!audio_busy());                  // and it finished on its own clock
  CHECK_EQ((int)audio_queued(), 0);
  // No sink was ever called, and nothing above needed to know that.
  CHECK_EQ((int)g_on_calls, 0);
  CHECK_EQ((int)g_off_calls, 0);

  // And the persisted setting is still honoured with no board attached, which
  // is the half a null sink could otherwise hide.
  g_live.flags = CF_MUTE;
  CHECK(gs_save_cfg(g_live));
  audio_bind(nullptr, nullptr);
  (void)boot();
  audio_bind(nullptr, &live_muted);
  CHECK(audio_muted());
  CHECK(!audio_play(SFX_BEEP));
}
