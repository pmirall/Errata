// =============================================================================
//  Pebblebol host tests - test_cooldowns.cpp
//  P5-C2: encounter cooldowns (spec section 21, plan T9).
//
//  The persisted table, expiry by epoch, LRU eviction over COOLDOWN_SLOTS, a
//  clock that rolls backwards, and the per-boot RAM fallback that keeps an
//  uncalibrated device from farming.
//
//  THE CLOCK IS INJECTED, so every one of those is drivable here with no
//  device: a CdClock carries the wall clock, a monotonic millisecond count and
//  the calibration state, which is what lets this file provoke the two cases
//  hardware makes almost impossible - a never-calibrated unit, and a
//  calibration landing in the middle of a session.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "fakes/kv_mem.h"
#include "game/cooldowns.h"
#include "persistence/save_schema.h"
#include "persistence/save_manager.h"

// A plausible wall clock: 2025-09-16.
#define E0  1758000000u

static uint32_t s_ms    = 0;
static uint32_t s_epoch = E0;
static uint32_t fake_ms(void)    { return s_ms; }
static uint32_t fake_epoch(void) { return s_epoch; }

static CdClock clk(uint32_t epoch, uint32_t ms, TimeCal cal) {
  CdClock c;
  c.now_epoch = epoch;
  c.now_ms    = ms;
  c.cal       = (uint8_t)cal;
  return c;
}

static void begin(CooldownTable& t) {
  kv_mem_reset();
  s_ms    = 10000;
  s_epoch = E0;
  save_set_clock(&fake_ms, &fake_epoch);
  cooldowns_defaults(t);
  cd_begin();
}

static uint8_t rows_used(const CooldownTable& t) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < (uint8_t)COOLDOWN_SLOTS; ++i)
    if (t.rows[i].net_hash != 0) ++n;
  return n;
}

static const CooldownRow* find(const CooldownTable& t, uint32_t h) {
  for (uint8_t i = 0; i < (uint8_t)COOLDOWN_SLOTS; ++i)
    if (t.rows[i].net_hash == h) return &t.rows[i];
  return nullptr;
}

// =============================================================================
//  1. THE CALIBRATED PATH
// =============================================================================
TEST(a_network_nobody_has_explored_is_ready_and_arming_it_makes_it_wait) {
  CooldownTable t; begin(t);
  const CdClock c = clk(E0, 10000u, CAL_USER);

  CHECK(cd_ready(t, 0xAABBCCDDu, c));
  CHECK_EQ((int)rows_used(t), 0);

  CHECK(cd_arm(t, 0xAABBCCDDu, c));
  CHECK(!cd_ready(t, 0xAABBCCDDu, c));
  CHECK_EQ((int)rows_used(t), 1);
  CHECK_EQ((int)t.n, 1);
  CHECK(cd_take_dirty());                       // the caller must save
  CHECK(!cd_take_dirty());                      // ...once

  // A different network is untouched by it.
  CHECK(cd_ready(t, 0x11223344u, c));
}

TEST(a_cooldown_lasts_exactly_the_configured_period_and_not_a_second_less) {
  CooldownTable t; begin(t);
  const uint32_t H = 0xDEADBEEFu;
  CHECK(cd_arm(t, H, clk(E0, 0u, CAL_USER)));

  const CooldownRow* r = find(t, H);
  CHECK(r != nullptr);
  if (r) CHECK_EQ(r->until_epoch, E0 + (uint32_t)ENCOUNTER_COOLDOWN_S);

  // One second short of the deadline: still waiting. This is the boundary a
  // cooldown that is off by one shows up at.
  CHECK(!cd_ready(t, H, clk(E0 + (uint32_t)ENCOUNTER_COOLDOWN_S - 1u, 0u, CAL_USER)));
  CHECK(cd_ready(t, H, clk(E0 + (uint32_t)ENCOUNTER_COOLDOWN_S, 0u, CAL_USER)));
  // Spec section 21 asks for about two hours or more.
  CHECK((uint32_t)ENCOUNTER_COOLDOWN_S >= 7200u);
}

TEST(re_arming_a_known_network_reuses_its_own_row_rather_than_taking_a_new_one) {
  CooldownTable t; begin(t);
  const uint32_t H = 0x01020304u;
  CHECK(cd_arm(t, H, clk(E0, 0u, CAL_USER)));
  CHECK_EQ((int)rows_used(t), 1);
  CHECK(cd_arm(t, H, clk(E0 + 60u, 0u, CAL_USER)));
  CHECK_EQ((int)rows_used(t), 1);
  CHECK(find(t, H) != nullptr);
  if (find(t, H)) CHECK_EQ(find(t, H)->until_epoch, E0 + 60u + (uint32_t)ENCOUNTER_COOLDOWN_S);
}

TEST(hash_zero_is_never_ready_and_can_never_be_armed) {
  CooldownTable t; begin(t);
  const CdClock c = clk(E0, 0u, CAL_USER);
  // 0 is CooldownRow's "empty row" marker. A network that hashed to 0 would sit
  // permanently off cooldown and could be farmed forever, so it is refused at
  // both ends - the scanner's fold is the other half of the same argument.
  CHECK(!cd_ready(t, 0u, c));
  CHECK(!cd_arm(t, 0u, c));
  CHECK_EQ((int)rows_used(t), 0);
  CHECK(!cd_take_dirty());
}

// =============================================================================
//  2. EVICTION
// =============================================================================
// THE DEADLINES ARE OUT OF SLOT ORDER ON PURPOSE, AND THIS IS THE SECOND
// VERSION OF THIS CASE. The first armed COOLDOWN_SLOTS networks one minute
// apart in slot order, so the row that ends soonest WAS slot 0 - and a mutant
// that simply always evicted slot 0 passed it. Measured, not guessed: that
// mutation was applied and the case stayed green. Offsetting the deadlines by
// (i + 7) mod N puts the soonest at slot 25 and the latest at slot 7, so
// "evict slot 0", "evict the last slot" and "evict the least-recently-armed"
// are three different answers and only one of them is right.
#define CD_SKEW(i)  (((uint8_t)(i) + 7u) % (uint8_t)COOLDOWN_SLOTS)

TEST(the_thirty_third_network_evicts_the_one_whose_cooldown_ends_soonest) {
  CooldownTable t; begin(t);
  for (uint8_t i = 0; i < (uint8_t)COOLDOWN_SLOTS; ++i) {
    CHECK(cd_arm(t, 0x1000u + i, clk(E0 + 60u * CD_SKEW(i), 0u, CAL_USER)));
  }
  CHECK_EQ((int)rows_used(t), (int)COOLDOWN_SLOTS);
  CHECK_EQ((int)t.n, (int)COOLDOWN_SLOTS);

  // CD_SKEW(i) == 0 at i == COOLDOWN_SLOTS - 7, which is where the soonest
  // deadline - and therefore the least recently armed network - now lives.
  const uint32_t victim = 0x1000u + (uint32_t)COOLDOWN_SLOTS - 7u;
  CHECK(find(t, victim) != nullptr);
  const uint32_t now = E0 + 60u * COOLDOWN_SLOTS;
  CHECK(cd_arm(t, 0xFEEDu, clk(now, 0u, CAL_USER)));

  CHECK(find(t, victim) == nullptr);             // and only that one went
  CHECK(find(t, 0xFEEDu) != nullptr);
  CHECK(find(t, 0x1000u) != nullptr);            // slot 0 was NOT the victim
  CHECK(find(t, 0x1000u + COOLDOWN_SLOTS - 1u) != nullptr);   // nor the last
  for (uint8_t i = 0; i < (uint8_t)COOLDOWN_SLOTS; ++i) {
    if (0x1000u + i == victim) continue;
    CHECK(find(t, 0x1000u + i) != nullptr);
  }
  CHECK_EQ((int)rows_used(t), (int)COOLDOWN_SLOTS);

  // An evicted network is explorable again. That is the cost of a bounded
  // table and it is asserted rather than assumed.
  CHECK(cd_ready(t, victim, clk(now, 0u, CAL_USER)));
}

TEST(an_expired_row_is_the_one_reused_because_it_is_the_one_that_ends_soonest) {
  CooldownTable t; begin(t);
  for (uint8_t i = 0; i < (uint8_t)COOLDOWN_SLOTS; ++i) {
    CHECK(cd_arm(t, 0x2000u + i, clk(E0 + 3600u * CD_SKEW(i), 0u, CAL_USER)));
  }
  // The smallest deadline is also the first to expire, so ONE rule does both
  // jobs and a second "expired first" pass could only disagree with it. Skewed
  // like the case above so the expired row is not the first slot.
  const uint32_t expired = 0x2000u + (uint32_t)COOLDOWN_SLOTS - 7u;
  const uint32_t later = E0 + 3600u * COOLDOWN_SLOTS + (uint32_t)ENCOUNTER_COOLDOWN_S;
  CHECK(cd_ready(t, expired, clk(later, 0u, CAL_USER)));
  CHECK(cd_arm(t, 0xBEEFu, clk(later, 0u, CAL_USER)));
  CHECK(find(t, expired) == nullptr);
  CHECK(find(t, 0x2000u) != nullptr);
  CHECK(find(t, 0x2000u + COOLDOWN_SLOTS - 1u) != nullptr);
}

// =============================================================================
//  3. THE CLOCK MOVING
// =============================================================================
TEST(a_clock_that_rolls_backwards_never_shortens_a_cooldown) {
  CooldownTable t; begin(t);
  const uint32_t H = 0x5A5A5A5Au;
  CHECK(cd_arm(t, H, clk(E0, 0u, CAL_USER)));
  CHECK(find(t, H) != nullptr);
  const uint32_t until = find(t, H) ? find(t, H)->until_epoch : 0u;

  // A whole day backwards. Only an ABSOLUTE deadline is stored, so this makes
  // "now >= until" MORE false: the wait gets longer, never shorter.
  CHECK(!cd_ready(t, H, clk(E0 - 86400u, 0u, CAL_USER)));
  CHECK(find(t, H) != nullptr);
  if (find(t, H)) CHECK_EQ(find(t, H)->until_epoch, until);   // nothing rewritten

  // Even at the epoch floor.
  CHECK(!cd_ready(t, H, clk(0u, 0u, CAL_USER)));
  // Back to normal, the original deadline still governs.
  CHECK(!cd_ready(t, H, clk(E0 + 10u, 0u, CAL_USER)));
  CHECK(cd_ready(t, H, clk(until, 0u, CAL_USER)));
}

TEST(a_forward_jump_of_a_calibrated_clock_does_expire_a_cooldown_and_that_is_the_asymmetry) {
  CooldownTable t; begin(t);
  const uint32_t H = 0x7777u;
  CHECK(cd_arm(t, H, clk(E0, 0u, CAL_USER)));
  // Nothing can prevent this with absolute deadlines: a legitimate calibration
  // IS a forward jump. What the design protects is the ONE catastrophic forward
  // jump - uptime scale to real epoch on a never-calibrated unit - and it does
  // that by keeping those rows out of this table entirely. See below.
  CHECK(cd_ready(t, H, clk(E0 + 999999u, 0u, CAL_USER)));
}

// =============================================================================
//  4. THE UNCALIBRATED PATH (plan T9) - the farm this exists to close
// =============================================================================
TEST(an_uncalibrated_device_writes_nothing_to_the_persisted_table) {
  CooldownTable t; begin(t);
  // gt_now() on a never-calibrated unit is "last persisted epoch plus uptime",
  // which on a fresh device is a small number that restarts near zero every
  // boot. Writing that as a deadline is the farm.
  const CdClock u = clk(7200u, 5000u, CAL_UNSET);
  CHECK(cd_ready(t, 0xC0FFEEu, u));
  CHECK(cd_arm(t, 0xC0FFEEu, u));
  CHECK(!cd_ready(t, 0xC0FFEEu, u));

  CHECK_EQ((int)rows_used(t), 0);                // NOTHING reached the blob
  CHECK_EQ((int)t.n, 0);
  CHECK(!cd_take_dirty());                       // ...so there is nothing to save
  CHECK_EQ((int)cd_ram_used(), 1);
}

TEST(a_calibration_cannot_free_a_network_armed_while_the_clock_was_unknown) {
  CooldownTable t; begin(t);
  const uint32_t H = 0x1234ABCDu;

  // Armed on an uncalibrated device, 5 s into the boot.
  CHECK(cd_arm(t, H, clk(7200u, 5000u, CAL_UNSET)));

  // The user types the time on the time screen: the clock jumps by about
  // 1.7e9 seconds and gt_cal_state() becomes CAL_USER. THIS IS THE MOMENT the
  // whole design exists for. If the deadline had been written to the persisted
  // table as "7200 + 7200", this one keystroke would step past it - and past
  // the other thirty-one - in a single move.
  const CdClock cal = clk(E0, 6000u, CAL_USER);
  CHECK(!cd_ready(t, H, cal));

  // The row was PROMOTED rather than dropped: it now carries a real deadline
  // holding the time that was actually left.
  const CooldownRow* r = find(t, H);
  CHECK(r != nullptr);
  if (r) CHECK_EQ(r->until_epoch, E0 + (uint32_t)ENCOUNTER_COOLDOWN_S - 1u);  // 1 s elapsed
  CHECK_EQ((int)cd_ram_used(), 0);               // and the RAM row is consumed
  CHECK(cd_take_dirty());                        // the caller must save it

  // ...and it still expires when it should have.
  CHECK(!cd_ready(t, H, clk(E0 + (uint32_t)ENCOUNTER_COOLDOWN_S - 2u, 7000u, CAL_USER)));
  CHECK(cd_ready(t, H, clk(E0 + (uint32_t)ENCOUNTER_COOLDOWN_S, 7000u, CAL_USER)));
}

TEST(a_promoted_row_that_had_already_expired_is_dropped_and_not_re_armed) {
  CooldownTable t; begin(t);
  const uint32_t H = 0x99999999u;
  CHECK(cd_arm(t, H, clk(60u, 1000u, CAL_UNSET)));
  // The calibration lands long after the RAM cooldown ran out.
  const CdClock cal = clk(E0, 1000u + (uint32_t)ENCOUNTER_COOLDOWN_S * 1000u + 1u, CAL_USER);
  CHECK(cd_ready(t, H, cal));                    // it had finished, so it is ready
  CHECK(find(t, H) == nullptr);                  // promoting it would arm a
  CHECK_EQ((int)rows_used(t), 0);                // cooldown that was already over
  CHECK_EQ((int)cd_ram_used(), 0);
}

TEST(a_real_deadline_is_never_read_against_an_uptime_estimate) {
  CooldownTable t; begin(t);
  const uint32_t H = 0x4444u;
  CHECK(cd_arm(t, H, clk(E0, 0u, CAL_USER)));

  // The mirror of the farm, and spec section 47's "must never get stuck
  // permanently ... clock is invalid": a boot that cannot trust its clock must
  // not compare 2025 deadlines against an uptime of forty seconds, or every
  // network reads "not ready" for about fifty-four years.
  CHECK(cd_ready(t, H, clk(40u, 40000u, CAL_UNSET)));
  CHECK(find(t, H) != nullptr);
  if (find(t, H))
    CHECK_EQ(find(t, H)->until_epoch, E0 + (uint32_t)ENCOUNTER_COOLDOWN_S);  // untouched
}

TEST(the_ram_deadline_is_monotonic_so_a_wall_clock_jump_cannot_expire_it) {
  CooldownTable t; begin(t);
  const uint32_t H = 0x2222u;
  CHECK(cd_arm(t, H, clk(100u, 1000u, CAL_UNSET)));
  // The wall clock leaps a decade while the calibration state stays UNSET -
  // which is what god-mode time travel does, and what an estimate reseeded from
  // a stale save does. Uptime moved one second, so the cooldown has one second
  // less to run and no more.
  CHECK(!cd_ready(t, H, clk(400000000u, 2000u, CAL_UNSET)));
  // It expires on the monotonic clock, exactly on time.
  CHECK(!cd_ready(t, H, clk(100u, 1000u + (uint32_t)ENCOUNTER_COOLDOWN_S * 1000u - 1u, CAL_UNSET)));
  CHECK(cd_ready(t, H, clk(100u, 1000u + (uint32_t)ENCOUNTER_COOLDOWN_S * 1000u, CAL_UNSET)));
}

TEST(the_uncalibrated_fallback_is_cleared_by_a_reboot_and_that_limit_is_asserted) {
  CooldownTable t; begin(t);
  const uint32_t H = 0x3333u;
  CHECK(cd_arm(t, H, clk(60u, 1000u, CAL_UNSET)));
  CHECK(!cd_ready(t, H, clk(60u, 2000u, CAL_UNSET)));

  // A power cycle. THIS IS NOT A BUG, IT IS THE LIMIT OF THE DESIGN, and it is
  // named here so nobody discovers it as a surprise: with no trustworthy
  // timestamp there is nothing to persist, so spec section 21's "avoid
  // resetting when battery dies" is unachievable while CAL_UNSET. The trade is
  // a CATASTROPHIC farm (one calibration frees every network) for a LINEAR one
  // (one reboot frees everything, at the cost of a boot).
  cd_begin();
  CHECK_EQ((int)cd_ram_used(), 0);
  CHECK(cd_ready(t, H, clk(60u, 1000u, CAL_UNSET)));
}

// =============================================================================
//  P6-C3: THE CARRIED COOLDOWN DEBT, AND WHY THE LADDER LIGHT-SLEEPS
//
//  The phase-5 exit left this written down: "deep sleep empties the
//  uncalibrated cooldown table, which turns a disclosed limit into a cheap
//  exploit". The case above - the_uncalibrated_fallback_is_cleared_by_a_reboot
//  - is that limit, and it is a LINEAR farm because a reboot costs the player a
//  boot. A DEEP sleep would have made the device perform that reboot by itself,
//  ten idle minutes at a time, with no keystroke at all.
//
//  The discharge is structural rather than clever: hardware/power.h's deepest
//  rung is a LIGHT sleep, so the CPU never resets, cd_begin() never runs again
//  and the RAM table is simply still there. The other half is the clock it is
//  measured in - ui.cpp hands cooldowns.cpp gt_mono32(), the RTC counter, and
//  not millis(), so the deadlines advance through the gap instead of being
//  paused by it. These two cases pin both halves.
// =============================================================================
TEST(an_uncalibrated_cooldown_is_still_armed_on_the_far_side_of_a_sleep) {
  CooldownTable t; begin(t);
  const uint32_t H = 0x51EEu;
  const uint32_t t0 = 1000u;
  CHECK(cd_arm(t, H, clk(60u, t0, CAL_UNSET)));

  // Ten idle minutes with the CPU stopped. Nothing sampled anything; the
  // monotonic clock carried the gap and the table was never cleared.
  const uint32_t after_sleep = t0 + 600000u;
  CHECK(!cd_ready(t, H, clk(60u, after_sleep, CAL_UNSET)));
  CHECK_EQ((int)cd_ram_used(), 1);

  // It expires when it was always going to expire - two hours after it was
  // armed - and the sleep neither shortened nor lengthened that.
  const uint32_t period_ms = (uint32_t)ENCOUNTER_COOLDOWN_S * 1000u;
  CHECK(!cd_ready(t, H, clk(60u, t0 + period_ms - 1u, CAL_UNSET)));
  CHECK(cd_ready(t, H, clk(60u, t0 + period_ms, CAL_UNSET)));
  CHECK_EQ((int)rows_used(t), 0);          // and nothing reached flash
}

TEST(a_full_uncalibrated_table_survives_a_night_of_eight_second_sleep_slices) {
  // The shape the ladder really produces: PWR_SLEEP_SLICE_MS at a time, all
  // night. Every one of the thirty-two rows has to still be armed at dawn, and
  // then expire on its own schedule - which is the difference between "the
  // sleep did nothing to the table" and "the sleep happened not to be noticed".
  CooldownTable t; begin(t);
  const uint32_t t0 = 5000u;
  for (uint8_t i = 0; i < (uint8_t)COOLDOWN_SLOTS; ++i) {
    CHECK(cd_arm(t, 0x7000u + i, clk(60u, t0, CAL_UNSET)));
  }
  CHECK_EQ((int)cd_ram_used(), (int)COOLDOWN_SLOTS);

  // 450 slices of 8 s = one hour, i.e. half of ENCOUNTER_COOLDOWN_S.
  uint32_t ms = t0;
  for (uint32_t i = 0; i < 450u; ++i) {
    ms += (uint32_t)PWR_SLEEP_SLICE_MS;
    CHECK(!cd_ready(t, 0x7000u, clk(60u, ms, CAL_UNSET)));   // polled at every wake
  }
  CHECK_EQ(ms, t0 + 3600000u);
  CHECK_EQ((int)cd_ram_used(), (int)COOLDOWN_SLOTS);
  for (uint8_t i = 0; i < (uint8_t)COOLDOWN_SLOTS; ++i) {
    CHECK(!cd_ready(t, 0x7000u + i, clk(60u, ms, CAL_UNSET)));
  }

  // The second hour, and every row comes free together because they were all
  // armed together. A reboot in the middle would have freed them an hour early
  // and for nothing - that is the exploit this rung refuses to build.
  ms = t0 + (uint32_t)ENCOUNTER_COOLDOWN_S * 1000u;
  for (uint8_t i = 0; i < (uint8_t)COOLDOWN_SLOTS; ++i) {
    CHECK(cd_ready(t, 0x7000u + i, clk(60u, ms, CAL_UNSET)));
  }
}

TEST(the_ram_table_also_evicts_the_row_that_ends_soonest) {
  CooldownTable t; begin(t);
  // Skewed the same way and for the same reason as the persisted case above:
  // the row with the least time left is at slot 25, not slot 0.
  for (uint8_t i = 0; i < (uint8_t)COOLDOWN_SLOTS; ++i) {
    CHECK(cd_arm(t, 0x5000u + i, clk(10u, 1000u + 1000u * CD_SKEW(i), CAL_UNSET)));
  }
  CHECK_EQ((int)cd_ram_used(), (int)COOLDOWN_SLOTS);
  const uint32_t victim = 0x5000u + (uint32_t)COOLDOWN_SLOTS - 7u;
  const uint32_t now_ms = 1000u + 1000u * COOLDOWN_SLOTS;
  CHECK(cd_arm(t, 0x6666u, clk(10u, now_ms, CAL_UNSET)));
  CHECK_EQ((int)cd_ram_used(), (int)COOLDOWN_SLOTS);
  CHECK(cd_ready(t, victim, clk(10u, now_ms, CAL_UNSET)));        // evicted
  CHECK(!cd_ready(t, 0x5000u, clk(10u, now_ms, CAL_UNSET)));      // slot 0 kept
  CHECK(!cd_ready(t, 0x5000u + COOLDOWN_SLOTS - 1u,
                  clk(10u, now_ms, CAL_UNSET)));                  // last slot kept
  CHECK(!cd_ready(t, 0x6666u, clk(10u, now_ms, CAL_UNSET)));      // and the new one
  CHECK_EQ((int)rows_used(t), 0);                                 // still nothing persisted
}

// =============================================================================
//  5. REBOOT SURVIVAL, THROUGH THE REAL PAIR AND THE REAL LOAD PIPELINE
// =============================================================================
TEST(an_armed_cooldown_survives_a_reboot_through_the_cd_pair) {
  CooldownTable t; begin(t);
  const uint32_t H = 0xFACEB00Cu;
  CHECK(cd_arm(t, H, clk(E0, 0u, CAL_USER)));
  CHECK(find(t, H) != nullptr);
  const uint32_t until = find(t, H) ? find(t, H)->until_epoch : 0u;
  CHECK(cd_take_dirty());
  cooldowns_seal(t);
  CHECK(save_cooldowns(t));

  // --- the device is power-cycled: nothing in RAM survives ------------------
  GameState gs;
  memset(&gs, 0, sizeof gs);
  box_defaults(gs.box);
  CHECK(save_box_header(gs.box));                // a Box the loader can read
  cd_begin();

  GameState fresh;
  memset(&fresh, 0, sizeof fresh);
  const LoadResult r = save_load_all(fresh);
  CHECK(r == LOAD_OK || r == LOAD_RECOVERED_PAIR);
  CHECK(cooldowns_blob_ok(fresh.cds));

  const CooldownRow* got = find(fresh.cds, H);
  CHECK(got != nullptr);
  if (got) CHECK_EQ(got->until_epoch, until);
  CHECK_EQ((int)fresh.cds.n, 1);

  // ...and it is still counting down on the far side of the reboot.
  CHECK(!cd_ready(fresh.cds, H, clk(E0 + 60u, 0u, CAL_USER)));
  CHECK(cd_ready(fresh.cds, H, clk(until, 0u, CAL_USER)));
}

TEST(a_full_table_survives_a_reboot_row_for_row) {
  CooldownTable t; begin(t);
  for (uint8_t i = 0; i < (uint8_t)COOLDOWN_SLOTS; ++i) {
    CHECK(cd_arm(t, 0x7000u + i, clk(E0 + i, 0u, CAL_USER)));
  }
  cooldowns_seal(t);
  CHECK(save_cooldowns(t));

  GameState gs;
  memset(&gs, 0, sizeof gs);
  box_defaults(gs.box);
  CHECK(save_box_header(gs.box));

  GameState fresh;
  memset(&fresh, 0, sizeof fresh);
  CHECK(save_load_all(fresh) != LOAD_CORRUPT);
  CHECK_EQ((int)fresh.cds.n, (int)COOLDOWN_SLOTS);
  for (uint8_t i = 0; i < (uint8_t)COOLDOWN_SLOTS; ++i) {
    const CooldownRow* got = find(fresh.cds, 0x7000u + i);
    CHECK(got != nullptr);
    if (got) CHECK_EQ(got->until_epoch, E0 + i + (uint32_t)ENCOUNTER_COOLDOWN_S);
  }
}
