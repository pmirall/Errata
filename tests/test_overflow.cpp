// =============================================================================
//  Pebblebol host tests - test_overflow.cpp
//  Plan section 1.7 / risk 24: the integer edges of the time model. Every case
//  here is a place where a u32 wrap or a backwards clock would silently turn
//  into a punishment the player did not earn.
//
//  gametime is linked in its GT_HOST_NEVER_VALID flavour so the estimated-clock
//  path (and with it the millis() wrap extension) is the one under test.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "config.h"
#include "gametime.h"
#include "host_shims.h"
#include "sim.h"
#include "genome.h"

// -----------------------------------------------------------------------------
//  1. Clamped epoch deltas: now < last_seen charges ZERO
// -----------------------------------------------------------------------------
TEST(overflow_epoch_delta_is_zero_when_the_clock_moved_backwards) {
  CHECK_EQ(gt_elapsed_since(1700000000u, 1699999999u), 0);   // one second back
  CHECK_EQ(gt_elapsed_since(1700000000u, 1700000000u), 0);   // exactly equal
  CHECK_EQ(gt_elapsed_since(0xFFFFFFFFu, 0u), 0);            // the full wrap
  CHECK_EQ(gt_elapsed_since(1700000000u, 1u), 0);            // an uptime as "now"
}

TEST(overflow_epoch_delta_spans_the_whole_u32_range) {
  CHECK_EQ(gt_elapsed_since(1700000000u, 1700000001u), 1);
  CHECK_EQ(gt_elapsed_since(0u, 0xFFFFFFFFu), 0xFFFFFFFFu);
  CHECK_EQ(gt_elapsed_since(0xFFFFFFFEu, 0xFFFFFFFFu), 1);
  // 400 days, the ABSENCE_MAX_S ceiling, computed without intermediate overflow.
  CHECK_EQ(gt_elapsed_since(1700000000u, 1700000000u + ABSENCE_MAX_S), ABSENCE_MAX_S);
}

// -----------------------------------------------------------------------------
//  2. The estimated clock across the 2^32 ms millis() wrap
// -----------------------------------------------------------------------------
TEST(overflow_estimated_epoch_is_monotonic_across_the_millis_wrap) {
  gt_test_reset();
  host_reset();
  host_set_ms(0xFFFF0000u);          // 65 536 ms before the rollover
  gt_begin();

  uint32_t prev = gt_now();
  const uint32_t marks[] = { 0xFFFFF000u, 0xFFFFFFFFu, 0x00000001u,
                             0x00010000u, 0x00100000u };
  for (unsigned i = 0; i < sizeof(marks) / sizeof(marks[0]); ++i) {
    host_set_ms(marks[i]);
    const uint32_t now = gt_now();
    CHECK(now >= prev);                                   // never rewinds
    CHECK_EQ(gt_elapsed_since(prev, now), now - prev);    // and never wraps
    prev = now;
  }
  gt_test_reset();
}

// -----------------------------------------------------------------------------
//  3. sim_catch_up_ex: an unknown or nonsensical gap charges ZERO
// -----------------------------------------------------------------------------
#define OVF_EPOCH0  1700000000u

static PetSave g_ovf;

static void ovf_pet(void) {
  genome_seed(0xABCDEF01u);
  sim_seed(0xABCDEF01u);
  memset(&g_ovf, 0, sizeof(g_ovf));
  sim_init(g_ovf);
  sim_new_pet(genome_genesis(), OVF_EPOCH0, 0);
  sim_hatch();

  SimEnv env;
  sim_env_defaults(env);
  env.now_epoch   = OVF_EPOCH0;
  env.clock_valid = 1;
  env.local_hour  = 10;
  env.local_min   = 0;
  env.day_of_year = 100;
  sim_set_env(env);
}

TEST(overflow_unknown_clock_charges_zero_absence) {
  ovf_pet();
  const PetSave before = g_ovf;
  AbsenceReport rep;
  sim_catch_up_ex(6u * 3600u, 0 /* clock unknown */, rep);

  CHECK_EQ(rep.clock_known, 0);
  CHECK_EQ(rep.absence_s, 0);                        // no ABSENCE_LARGA_S floor
  CHECK_EQ(rep.tier, (int)ABS_UNKNOWN);              // the UI still says "unknown"
  CHECK_EQ(rep.sulk_s, 0);                           // and the pet does not sulk
  CHECK((g_ovf.flags & PF_ABS_UNKNOWN) != 0);        // retro-fix armed instead

  // ZERO means zero. Not the decay integration, and not the ladder either:
  // dropping the ABSENCE_LARGA_S floor while still calling apply_tier(ABS_LARGA)
  // would charge -12 bond / -20 happiness / -5 health for a gap nobody measured.
  for (uint8_t i = 0; i < ST_COUNT; ++i) {
    CHECK_EQ(g_ovf.stat[i], before.stat[i]);
    CHECK_EQ(g_ovf.stat_rem[i], before.stat_rem[i]);
  }
  CHECK_EQ(g_ovf.care_miss, before.care_miss);
  CHECK_EQ(g_ovf.dmg_acc[DMG_OTHER], before.dmg_acc[DMG_OTHER]);
  // PF_ABS_UNKNOWN is the only flag that may move (no PF_SCAR, no death).
  CHECK_EQ(g_ovf.flags & (uint16_t)~PF_ABS_UNKNOWN, before.flags);
  // The retro-fix baseline: ABS_NONE, because nothing has been charged yet.
  // Storing ABS_UNKNOWN (6) here would put the baseline above every real tier
  // and sim_absence_retrofix()'s "never better retroactively" test would then
  // refuse the truth forever.
  CHECK_EQ(g_ovf.absence_tier, (int)ABS_NONE);
}

TEST(overflow_retrofix_after_an_unknown_boot_charges_the_whole_tier) {
  ovf_pet();
  const PetSave before = g_ovf;
  AbsenceReport rep;
  sim_catch_up_ex(6u * 3600u, 0 /* clock unknown */, rep);

  // The clock arrives (gt_set_epoch) and the true gap turns out to be 2 h,
  // i.e. ABS_CORTA - LESS than the ABS_LARGA the old floor pretended to. The
  // boot charged nothing, so the whole ABS_CORTA tier is charged now.
  sim_absence_retrofix(2u * 3600u);

  CHECK_EQ(g_ovf.absence_tier, (int)ABS_CORTA);
  CHECK_EQ(g_ovf.stat[ST_BOND],
           before.stat[ST_BOND]      + (int32_t)ABS_CORTA_BOND * 1000);
  CHECK_EQ(g_ovf.stat[ST_HAPPINESS],
           before.stat[ST_HAPPINESS] + (int32_t)ABS_CORTA_HAP  * 1000);
  CHECK_EQ(g_ovf.stat[ST_HEALTH],
           before.stat[ST_HEALTH]    + (int32_t)ABS_CORTA_HEA  * 1000);
  CHECK((g_ovf.flags & PF_ABS_UNKNOWN) == 0);        // and the flag is spent

  // Spent means spent: a second calibration does not charge the gap twice.
  const int32_t bond_after = g_ovf.stat[ST_BOND];
  sim_absence_retrofix(3u * 86400u);
  CHECK_EQ(g_ovf.stat[ST_BOND], bond_after);
}

TEST(overflow_absence_beyond_the_400_day_ceiling_is_unknown) {
  ovf_pet();
  AbsenceReport rep;
  sim_catch_up_ex(ABSENCE_MAX_S + 1u, 1 /* clock known */, rep);

  CHECK_EQ(rep.clock_known, 0);                      // a nonsense clock, not a gap
  CHECK_EQ(rep.absence_s, 0);
  CHECK((g_ovf.flags & PF_ABS_UNKNOWN) != 0);
}

TEST(overflow_absence_exactly_at_the_ceiling_is_charged) {
  ovf_pet();
  AbsenceReport rep;
  sim_catch_up_ex(ABSENCE_MAX_S, 1, rep);

  CHECK_EQ(rep.clock_known, 1);
  CHECK_EQ(rep.absence_s, ABSENCE_MAX_S);
  CHECK((g_ovf.flags & PF_ABS_UNKNOWN) == 0);        // measured, so nothing to fix
}

TEST(overflow_known_zero_absence_does_not_arm_the_retrofix) {
  ovf_pet();
  AbsenceReport rep;
  sim_catch_up_ex(0, 0 /* "unknown", but nothing elapsed */, rep);

  CHECK_EQ(rep.clock_known, 1);                      // a true zero is knowledge
  CHECK_EQ(rep.absence_s, 0);
  CHECK((g_ovf.flags & PF_ABS_UNKNOWN) == 0);
}

// -----------------------------------------------------------------------------
//  4. u32 accumulators saturate rather than wrap
// -----------------------------------------------------------------------------
TEST(overflow_age_saturates_instead_of_rewinding_the_pet) {
  ovf_pet();
  // Park the pet 30 s short of the u32 ceiling, then advance one 60 s sub-step.
  // Plain `age_s += dt` would wrap to 29 - a newborn egg wearing an adult body;
  // sat_add_u32() pins it at the ceiling instead. (Today the natural-death
  // check keeps a pet from ever getting here on its own; P2-C7 removes death
  // and the ceiling becomes the only thing between age_s and a wrap.)
  g_ovf.age_s = 0xFFFFFFFFu - 30u;
  sim_tick(60);
  CHECK_EQ(sim_age_s(), 0xFFFFFFFFu);

  // And it stays there however long the world keeps running.
  for (uint32_t i = 0; i < 100u; ++i) sim_tick(3600);
  CHECK_EQ(sim_age_s(), 0xFFFFFFFFu);
}

TEST(overflow_repeated_catch_up_never_rewinds_the_age) {
  ovf_pet();
  AbsenceReport rep;
  uint32_t prev = sim_age_s();
  for (uint32_t i = 0; i < 8u; ++i) {
    sim_catch_up_ex(ABSENCE_MAX_S, 1, rep);
    const uint32_t now = sim_age_s();
    CHECK(now >= prev);
    prev = now;
  }
}
