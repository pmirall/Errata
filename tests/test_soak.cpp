// =============================================================================
//  Pebblebol host tests - test_soak.cpp
//  THE SOAK THAT CAN BE RUN HERE (P10-C2, spec section 46 "no memory leaks").
//
//  READ THE SCOPE FIRST. Spec section 46 asks for a 24 h soak with a flat heap
//  line. THAT SOAK NEEDS A BOARD AND THIS FILE IS NOT IT. A 24-hour host run
//  would exercise game/**, persistence/** and networking/{protocol,session}
//  against fakes, with NO Wi-Fi stack, NO WebServer, NO U8g2 buffer, NO ESP-NOW
//  transport and NO NVS wear - which is to say it would link none of the four
//  subsystems where a leak would actually live, and produce a green line and a
//  claim about a device. That is the phase-7 defect (a fifteen-point power-cut
//  sweep run against a save_manager the release artefact does not execute,
//  because the fixture bound a null clock) with a bigger number attached.
//  docs/bench.md step A5 is the real soak: a release board, 24 hours, the
//  `DIAG,heap,` line the shipping artefact already prints every 60 s.
//
//  WHAT THIS FILE *IS*, AND IT IS WORTH HAVING. The pure layers are supposed to
//  allocate NOTHING AT ALL - tools/check.sh already forbids new/malloc inside
//  src/game/** by grep - and "nothing" is a property a long run can check
//  exactly rather than approximately. So: a whole simulated day of the real
//  loop, second by second, through the real sim, the real Box, the real
//  cooldown table, the real corruption deadline and the real save_manager into
//  a fake NVS partition, with a global operator new counter armed for the
//  entire run. The assertions are that the count is ZERO, that the Pebble is
//  still valid at the end, and that what came back off "flash" is what went in.
//
//  THE LIMIT, SAID OUT LOUD: this proves the pure half leaks nothing. It says
//  nothing whatever about the device, and it is not evidence for section 46.
// =============================================================================
#include "nt_test.h"

#include <stdlib.h>
#include <string.h>

#include "core/config.h"
#include "data/species_table.h"
#include "fakes/alloc_count.h"
#include "fakes/boot_host.h"
#include "fakes/kv_mem.h"
#include "game/box.h"
#include "game/corruption.h"
#include "game/cooldowns.h"
#include "game/sim.h"
#include "core/rng.h"
#include "game/genome.h"
#include "game/validate.h"
#include "persistence/save_manager.h"
#include "persistence/save_schema.h"

// -----------------------------------------------------------------------------
//  THE FIXTURE. A real GameState, a real Box, a real bound simulation, and a
//  real save_manager with a REAL millisecond clock - never save_set_clock(
//  nullptr, ...), which switches the wear filter off and is exactly the fixture
//  that let a Pebble be destroyed on every clean trade for three phases.
//  tools/check.sh greps for it.
// -----------------------------------------------------------------------------
#define SOAK_EPOCH0   1700000000u
#define SOAK_SECONDS  86400u          // one whole simulated day, second by second
#define SOAK_SAVE_S   60u             // the firmware's own periodic write cadence

static GameState g_gs;
static uint32_t  g_ms    = 10000u;
static uint32_t  g_epoch = SOAK_EPOCH0;
static uint32_t  soak_ms(void)    { return g_ms; }
static uint32_t  soak_epoch(void) { return g_epoch; }

static void fixture(void) {
  kv_mem_reset();
  boot_host_reset();
  g_ms    = 10000u;
  g_epoch = SOAK_EPOCH0;
  save_set_clock(&soak_ms, &soak_epoch);
  rng_seed_all(0xC0FFEEu);

  memset(&g_gs, 0, sizeof g_gs);
  for (uint8_t i = 0; i < (uint8_t)BOX_SLOTS; ++i) {
    g_gs.pebbles[i].magic      = (uint16_t)PEBBLE_MAGIC;
    g_gs.pebbles[i].layout_ver = (uint8_t)PEBBLE_LAYOUT_VER;
  }
  g_gs.box.magic           = (uint16_t)BOX_MAGIC;
  g_gs.box.active_slot     = (uint8_t)BOX_ACTIVE_NONE;
  g_gs.box.next_id_counter = 1;
  g_gs.box.schema_version  = (uint8_t)SAVE_SCHEMA_VERSION;
  g_gs.cfg.device_id       = 0x50A4B011u;
  box_bind(g_gs);
  save_bind(g_gs);

  // Three creatures, so the corruption walk and the Box have something to walk.
  // REAL SEALED GENOMES, not a memset: validate_pebble() is one of this file's
  // two end-of-run assertions and a zeroed genome fails it as VR_BAD_GENOME,
  // which would make "still valid after a day" a statement about the fixture.
  for (uint8_t i = 0; i < 3u; ++i) {
    const uint8_t s = box_new_pebble(SPECIES_ID_STARTER, (uint8_t)(5u + i),
                                     (uint8_t)ORIGIN_WILD, genome_genesis(),
                                     0x3000u + i, SOAK_EPOCH0);
    CHECK(s != (uint8_t)BOX_SLOT_NONE);
  }
  CHECK(box_set_active(0));
  sim_bind(*box_slot(0));
  CHECK(save_config(g_gs.cfg));
  CHECK(save_box_header(g_gs.box));
  for (uint8_t i = 0; i < 3u; ++i) CHECK(save_pebble_now(i, g_gs.pebbles[i]));
}

// One second of the firmware's stage 2, with the same inputs app.cpp assembles.
static void one_second(uint32_t sec)
{
  SimEnv env;
  sim_env_defaults(env);
  env.now_epoch   = g_epoch;
  env.day_of_year = (uint16_t)((sec / 86400u) % 366u);
  env.local_hour  = (uint8_t)((sec / 3600u) % 24u);
  env.local_min   = (uint8_t)((sec / 60u) % 60u);
  env.clock_valid = 1u;
  sim_set_env(env);
  sim_tick(1u);
  (void)sim_take_events();
}

// =============================================================================
//  THE RUN
// =============================================================================
TEST(a_whole_simulated_day_of_the_pure_loop_allocates_nothing) {
  fixture();

  CooldownTable cds;
  memset(&cds, 0, sizeof cds);

  // ARMED ONLY NOW: the fixture above is setup, and setup is allowed to do
  // whatever it likes. What may not allocate is the LOOP.
  alloc_count_reset();
  alloc_count_arm(true);

  uint32_t saves = 0;
  for (uint32_t sec = 0; sec < SOAK_SECONDS; ++sec) {
    g_epoch += 1u;
    g_ms    += 1000u;
    one_second(sec);

    // The two per-second walks app.cpp's logic_tick() also drives.
    (void)cor_service(g_gs.pebbles, (uint8_t)BOX_SLOTS, g_epoch, (uint8_t)CAL_USER);
    CdClock cc; cc.now_epoch = g_epoch; cc.now_ms = g_ms; cc.cal = (uint8_t)CAL_USER;
    (void)cd_ready(cds, 0xA1B2C3D4u, cc);

    // And the periodic write, through the REAL save_manager and the REAL wear
    // filter, into the fake partition. A soak that never wrote would be a soak
    // of the half of the firmware that cannot wear flash out.
    if ((sec % SOAK_SAVE_S) == 0u) {
      if (save_pebble(0, g_gs.pebbles[0], false)) ++saves;
      save_service();
    }
  }

  const uint32_t loop_allocs = alloc_count();

  // THE INSTRUMENT HAS TO BE LIVE, and this is not a formality. Found by the
  // mutation run: alloc_count_arm(false) makes every assertion below pass for
  // ever while measuring nothing - a counter that reads 0 because it is off is
  // indistinguishable from one that reads 0 because the code is clean, and this
  // repository has shipped that exact shape before. One deliberate allocation,
  // inside the armed window, must be SEEN.
  alloc_count_probe();
  const uint32_t probe_allocs = alloc_count() - loop_allocs;
  alloc_count_arm(false);

  if (probe_allocs != 1u)
    fprintf(stderr, "  the allocation counter is not live (%u for one probe) - "
                    "every zero below would be meaningless\n",
            (unsigned)probe_allocs);
  CHECK_EQ(probe_allocs, 1u);

  if (loop_allocs != 0u)
    fprintf(stderr, "  %u allocation(s) in %u simulated seconds - the pure "
                    "layers are supposed to have no heap at all\n",
            (unsigned)loop_allocs, (unsigned)SOAK_SECONDS);
  CHECK_EQ(loop_allocs, 0u);

  // The run has to have DONE something, or "it allocated nothing" is a
  // statement about a loop that did not run. This is the anti-vacuity clause.
  CHECK(saves > 0);
  const PebbleInstance* p = box_peek(0);
  CHECK(p != nullptr);
  if (p) {
    CHECK(p->age_s >= SOAK_SECONDS - 2u);   // a whole day really passed
    CHECK_EQ((int)validate_pebble(*p), (int)VR_OK);
  }
}

// The same day, and then the device is switched off and on. A leak is one
// failure mode; a save that stopped being loadable after a day of writes is the
// other, and the wear filter is what makes the two interact.
TEST(what_a_days_worth_of_writes_left_on_flash_still_loads_and_still_validates) {
  fixture();

  CooldownTable cds;
  memset(&cds, 0, sizeof cds);
  for (uint32_t sec = 0; sec < SOAK_SECONDS; ++sec) {
    g_epoch += 1u;
    g_ms    += 1000u;
    one_second(sec);
    CdClock cc; cc.now_epoch = g_epoch; cc.now_ms = g_ms; cc.cal = (uint8_t)CAL_USER;
    (void)cd_ready(cds, 0xA1B2C3D4u, cc);
    if ((sec % SOAK_SAVE_S) == 0u) { (void)save_pebble(0, g_gs.pebbles[0], false); save_service(); }
  }
  CHECK(save_pebble_now(0, g_gs.pebbles[0]));
  CHECK(save_box_header(g_gs.box));
  const uint32_t id0  = g_gs.pebbles[0].id;
  const uint32_t age0 = g_gs.pebbles[0].age_s;

  // THE REBOOT: a brand-new GameState, filled from nothing but what reached the
  // fake partition, through the loader the firmware runs.
  static GameState back;
  memset(&back, 0, sizeof back);
  const LoadResult r = save_load_all(back);
  if (r != LOAD_OK) fprintf(stderr, "  a day of writes reloaded as %d\n", (int)r);
  CHECK_EQ((int)r, (int)LOAD_OK);

  CHECK_EQ((int)back.box.active_slot, 0);
  CHECK_EQ(back.pebbles[0].id, id0);
  CHECK_EQ(back.pebbles[0].age_s, age0);
  for (uint8_t i = 0; i < 3u; ++i)
    CHECK_EQ((int)validate_pebble(back.pebbles[i]), (int)VR_OK);
  // Nothing was quarantined by a day of ordinary living.
  CHECK_EQ((int)save_quarantine_mask(), 0);

  // And the fixture is put back, so a later case does not inherit save_bind()
  // pointing at a GameState this one is done with.
  save_bind(g_gs);
}
