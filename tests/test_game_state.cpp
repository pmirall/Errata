// =============================================================================
//  Pebblebol host tests - test_game_state.cpp
//  persistence/game_state.cpp: the live GameState and the single door to flash.
//
//  It is what stands between a v1 owner's pet and SaveSchema v2, so:
//    * a v1 save migrates and comes back as the SAME animal;
//    * a save/load round trip is lossless for every field v2 carries;
//    * a refused load (LOAD_CORRUPT) writes NOTHING - the one rule the whole
//      P2-C9 pipeline exists to enforce;
//    * the checkpoint in nvs2 brings a rotted Box back when the user asks.
//
//  P2-C10 replaced save_compat.cpp with this module: the simulation runs on
//  PebbleInstance now, so the PetSave map and the "lgpet" companion blob are
//  gone and the tests that covered them with them.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "fakes/kv_mem.h"
#include "fakes/boot_host.h"
#include "persistence/game_state.h"
#include "persistence/save_manager.h"
#include "persistence/legacy_v1.h"
#include "core/crc16.h"

static uint32_t s_ms    = 0;
static uint32_t s_epoch = 1700300000u;
static uint32_t fake_ms(void)    { return s_ms; }
static uint32_t fake_epoch(void) { return s_epoch; }

static void begin(void) {
  kv_mem_reset();
  boot_host_reset();
  s_ms    = 10000;
  s_epoch = 1700300000u;
  save_set_clock(&fake_ms, &fake_epoch);
  gs_set_readonly(false);
}

static size_t load_file(const char* rel, uint8_t* out, size_t cap) {
  char path[512];
  nt_path(rel, path, sizeof path);
  FILE* f = fopen(path, "rb");
  if (!f) { fprintf(stderr, "  cannot open %s\n", path); return 0; }
  const size_t n = fread(out, 1, cap, f);
  fclose(f);
  return n;
}

// Seeds KV_MAIN with the committed v1 blobs, exactly as hardware/kv_nvs.cpp's
// one-shot import leaves them for the migration.
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

TEST(game_state_migrates_a_v1_save_into_slot_zero) {
  begin();
  if (!seed_v1()) { CHECK(false); return; }

  Config cfg;
  const LoadResult r = gs_load(cfg);
  CHECK_EQ((int)r, (int)LOAD_MIGRATED);
  CHECK(gs_have_pebble());
  CHECK(!gs_readonly());

  const PebbleInstance& p = gs_state().pebbles[0];
  CHECK(!pebble_is_empty(p));
  CHECK(p.id != 0u);
  CHECK(p.care[CARE_HUNGER] > 0);
  CHECK_EQ((int)gs_state().box.active_slot, 0);
  // The v1 adult fixture migrates to level 15 (migrate_level_of ADULT).
  CHECK_EQ(p.level, 15);
  // The v1 keys are gone and the Pebble is on flash.
  CHECK(!kv_mem_exists(KV_MAIN, KEY_V1_SAVE));
  CHECK(kv_mem_exists(KV_MAIN, "pb00") || kv_mem_exists(KV_MAIN, "pb01"));
  // The P2-C9 companion blob is not written any more.
  CHECK(!kv_mem_exists(KV_MAIN, "lgpet"));
}

TEST(game_state_round_trip_keeps_every_field_v2_carries) {
  begin();
  if (!seed_v1()) { CHECK(false); return; }

  Config cfg;
  CHECK_EQ((int)gs_load(cfg), (int)LOAD_MIGRATED);

  PebbleInstance& p = gs_state().pebbles[0];
  const uint32_t id = p.id;
  p.care[CARE_HUNGER]      = 61234;
  p.care_rem[CARE_HUNGER]  = -311;
  p.xp                     = 812;
  p.level                  = 16;
  p.minigames_won          = 47;
  p.status                |= PBS_SICK;
  p.last_updated_epoch     = s_epoch;
  s_ms += 2000;
  CHECK(gs_save_active(true));

  Config cfg2;
  CHECK_EQ((int)gs_load(cfg2), (int)LOAD_OK);
  CHECK(gs_have_pebble());
  const PebbleInstance& back = gs_state().pebbles[0];
  CHECK_EQ(back.id, id);
  CHECK_EQ(back.care[CARE_HUNGER], 61234);
  CHECK_EQ((int)back.care_rem[CARE_HUNGER], -311);
  CHECK_EQ((int)back.xp, 812);
  CHECK_EQ((int)back.level, 16);
  CHECK_EQ((int)back.minigames_won, 47);
  CHECK((back.status & PBS_SICK) != 0);
}

TEST(game_state_never_writes_after_a_refused_load) {
  begin();
  if (!seed_v1()) { CHECK(false); return; }

  Config cfg;
  CHECK_EQ((int)gs_load(cfg), (int)LOAD_MIGRATED);
  s_ms += 2000;
  CHECK(gs_save_active(true));

  // Rot both copies of the Box: the load must refuse, and refuse in silence.
  CHECK(kv_mem_corrupt("box0", 6));
  CHECK(kv_mem_corrupt("box1", 6));

  Config cfg2;
  const LoadResult r = gs_load(cfg2);
  CHECK_EQ((int)r, (int)LOAD_CORRUPT);
  CHECK(gs_readonly());
  CHECK(!gs_have_pebble());

  const uint32_t puts_before = kv_mem_puts();
  CHECK(!gs_save_active(true));
  CHECK(!gs_save_slot(0, true));
  CHECK(!gs_save_box());
  CHECK(!gs_save_cfg(cfg));
  gs_touch_lastseen(s_epoch);
  CHECK_EQ(kv_mem_puts(), puts_before);      // not one byte reached flash

  // A factory reset is the only thing that clears it, and it is explicit.
  CHECK(gs_factory_reset());
  CHECK(!gs_readonly());
}

TEST(game_state_config_maps_both_ways) {
  begin();
  // A pet first: save_load_all() reports LOAD_FRESH and reads nothing else when
  // KV_MAIN holds no Box at all, so a config on its own is not a save. On a
  // real unit the first boot writes both, which is what this reproduces.
  if (!seed_v1()) { CHECK(false); return; }
  Config cfg;
  CHECK_EQ((int)gs_load(cfg), (int)LOAD_MIGRATED);

  cfg.brightness     = 96;
  cfg.statusbar_mode = (uint8_t)SBAR_TEXT;
  cfg.flags          = (uint8_t)(CF_MUTE | CF_WEB_ENABLED);
  snprintf(cfg.pet_name, sizeof cfg.pet_name, "Pebo");
  snprintf(cfg.tz, sizeof cfg.tz, "UTC0");
  CHECK(gs_save_cfg(cfg));

  const ConfigV2& v2 = gs_state().cfg;
  CHECK_EQ(v2.brightness, 96);
  CHECK((v2.flags & CFGV2_F_MUTE) != 0);
  CHECK((v2.flags & CFGV2_F_WEB) != 0);
  CHECK((v2.flags & CFGV2_F_BLE) == 0);
  CHECK_EQ((v2.flags & CFGV2_F_SBAR_MASK) >> CFGV2_F_SBAR_SH, (uint16_t)SBAR_TEXT);
  CHECK_STR_EQ(v2.device_name, "Pebo");
  CHECK_STR_EQ(v2.tz, "UTC0");

  Config back;
  CHECK_EQ((int)gs_load(back), (int)LOAD_OK);
  CHECK_EQ(back.brightness, 96);
  CHECK_EQ(back.statusbar_mode, (uint8_t)SBAR_TEXT);
  CHECK((back.flags & CF_MUTE) != 0);
  CHECK((back.flags & CF_WEB_ENABLED) != 0);
  CHECK_STR_EQ(back.pet_name, "Pebo");
  CHECK_STR_EQ(back.tz, "UTC0");

  char tz[CFGV2_TZ_CAP];
  gs_boot_tz(tz, sizeof tz);
  CHECK_STR_EQ(tz, "UTC0");
}

TEST(game_state_gain_ledger_survives_a_write_and_refuses_a_foreign_one) {
  begin();
  uint8_t pts[GS_GAIN_SLOTS];
  uint32_t epoch = 0;
  CHECK(!gs_load_gain(pts, epoch));           // nothing stored yet

  LegacyGainSave g;
  memset(&g, 0, sizeof g);
  g.magic   = (uint16_t)LEGACY_GAIN_MAGIC;
  g.version = (uint8_t)LEGACY_GAIN_VERSION;
  g.slots   = GS_GAIN_SLOTS;
  g.epoch   = 1700200000u;
  g.pts[ST_HUNGER]  = 30;
  g.pts[ST_HYGIENE] = 25;
  g.crc16 = crc16_ccitt(&g, LEGACY_GAINSAVE_CRC_BYTES);
  CHECK(kv_put(KV_MAIN, KEY_GAIN, &g, sizeof g));

  CHECK(gs_load_gain(pts, epoch));
  CHECK_EQ(epoch, 1700200000u);
  CHECK_EQ(pts[ST_HUNGER], 30);
  CHECK_EQ(pts[ST_HYGIENE], 25);

  // A blob written by a build with a different StatId count is refused whole,
  // never reinterpreted byte by byte.
  g.slots = (uint8_t)(GS_GAIN_SLOTS + 1u);
  g.crc16 = crc16_ccitt(&g, LEGACY_GAINSAVE_CRC_BYTES);
  CHECK(kv_put(KV_MAIN, KEY_GAIN, &g, sizeof g));
  CHECK(!gs_load_gain(pts, epoch));
  CHECK_EQ(epoch, 0u);
}

// The XP anti-farm ledger rides the inventory pair (plan P3-C2). What has to
// hold is that a SPEND reaches flash and comes back, and that an epoch which is
// not a wall clock is stored as 0 - the value xp_ledger_restore() refuses,
// which is what stops a never-calibrated device replaying an intact budget.
TEST(game_state_xp_ledger_rides_the_inventory_pair) {
  begin();
  if (!seed_v1()) { CHECK(false); return; }
  uint8_t pts[XP_LEDGER_SLOTS];
  uint32_t epoch = 0;
  Config cfg;
  CHECK_EQ((int)gs_load(cfg), (int)LOAD_MIGRATED);
  CHECK(!gs_load_xp_ledger(pts, epoch));       // a fresh device has no snapshot

  const uint8_t spent[XP_LEDGER_SLOTS] = { 3, 11, 40, 0 };
  CHECK(gs_save_xp_ledger(spent, 1700200000u));
  CHECK(gs_load_xp_ledger(pts, epoch));
  CHECK_EQ(epoch, 1700200000u);
  CHECK_EQ(pts[0], 3);
  CHECK_EQ(pts[1], 11);
  CHECK_EQ(pts[2], 40);

  // Survives a reload from flash, which is the whole point of writing it.
  Config cfg2;
  CHECK_EQ((int)gs_load(cfg2), (int)LOAD_OK);
  CHECK(gs_load_xp_ledger(pts, epoch));
  CHECK_EQ(epoch, 1700200000u);
  CHECK_EQ(pts[2], 40);

  // An uptime counter is not an epoch: stored as 0, and refused on the way back.
  CHECK(gs_save_xp_ledger(spent, 4200u));
  {
    // The read-side guard below refuses a sub-sane epoch however it was stored,
    // so it passes with or without the write-side normalisation -- confirmed by
    // mutating that line and watching the suite stay green. What tells the two
    // implementations apart is the BYTES on flash: normalised they read 0, and
    // unnormalised they read the uptime counter back.
    Inventory a{}, b{};
    const int na = kv_get(KV_MAIN, "inv0", &a, sizeof a);
    const int nb = kv_get(KV_MAIN, "inv1", &b, sizeof b);
    CHECK(na == (int)sizeof a || nb == (int)sizeof b);
    const Inventory& live =
        (na == (int)sizeof a && (nb != (int)sizeof b || a.seq >= b.seq)) ? a : b;
    CHECK_EQ(live.ledger_epoch, 0u);
  }
  CHECK(!gs_load_xp_ledger(pts, epoch));
  CHECK_EQ(epoch, 0u);

  // A read-only session (a save the user has not answered for yet) writes nothing.
  gs_set_readonly(true);
  CHECK(!gs_save_xp_ledger(spent, 1700200000u));
  gs_set_readonly(false);
}

TEST(game_state_device_id_is_drawn_once_and_then_kept) {
  begin();
  if (!seed_v1()) { CHECK(false); return; }

  Config cfg;
  CHECK_EQ((int)gs_load(cfg), (int)LOAD_MIGRATED);
  const uint32_t id = gs_device_id();
  CHECK(id != 0u);

  Config cfg2;
  CHECK_EQ((int)gs_load(cfg2), (int)LOAD_OK);
  CHECK_EQ(gs_device_id(), id);             // spec section 43: once, forever

  // The creator PIN state stays zero until P8 gives it a screen again.
  CHECK_EQ(gs_state().cfg.creator_pin, 0);
  CHECK_EQ(gs_state().cfg.pin_fail_count, 0);
  CHECK_EQ(gs_state().cfg.pin_lock_until, 0u);
}

TEST(game_state_time_calibration_is_persisted_and_read_back) {
  begin();
  if (!seed_v1()) { CHECK(false); return; }
  Config cfg;
  CHECK_EQ((int)gs_load(cfg), (int)LOAD_MIGRATED);

  gs_note_time_cal((uint8_t)CAL_USER, 1700500000u);

  Config cfg2;
  CHECK_EQ((int)gs_load(cfg2), (int)LOAD_OK);
  uint8_t  state = 0;
  uint32_t known = 0;
  gs_boot_cal(state, known);
  CHECK_EQ(state, (uint8_t)CAL_USER);
  CHECK(known >= 1700500000u);
  CHECK_EQ(gs_state().cfg.time_cal_epoch, 1700500000u);
}

TEST(game_state_recovers_from_the_nvs2_checkpoint_when_the_user_asks) {
  begin();
  if (!seed_v1()) { CHECK(false); return; }
  Config cfg;
  CHECK_EQ((int)gs_load(cfg), (int)LOAD_MIGRATED);
  gs_state().pebbles[0].care[CARE_HAPPINESS] = 55000;
  s_ms += 2000;
  CHECK(gs_save_active(true));
  CHECK(save_checkpoint_all());                 // the daily nvs2 copy

  // Now lose KV_MAIN's Box the way bit rot does.
  CHECK(kv_mem_corrupt("box0", 6));
  CHECK(kv_mem_corrupt("box1", 6));
  Config cfg2;
  CHECK_EQ((int)gs_load(cfg2), (int)LOAD_CORRUPT);
  CHECK(gs_readonly());

  Config cfg3;
  CHECK_EQ((int)gs_recover(cfg3), (int)LOAD_RECOVERED_CKPT);
  CHECK(!gs_readonly());
  CHECK(gs_have_pebble());
  CHECK_EQ(gs_state().pebbles[0].care[CARE_HAPPINESS], 55000);

  // The recovered state is on KV_MAIN. The next boot still finds the rotten
  // second copy of the Box pair - commit_all() rewrote the one a reader would
  // not pick - repairs it, and the boot after that is an ordinary one.
  Config cfg4;
  CHECK_EQ((int)gs_load(cfg4), (int)LOAD_RECOVERED_PAIR);
  Config cfg5;
  CHECK_EQ((int)gs_load(cfg5), (int)LOAD_OK);
  CHECK_EQ(gs_state().pebbles[0].care[CARE_HAPPINESS], 55000);
}

TEST(game_state_recover_with_no_checkpoint_writes_nothing) {
  begin();
  if (!seed_v1()) { CHECK(false); return; }
  Config cfg;
  CHECK_EQ((int)gs_load(cfg), (int)LOAD_MIGRATED);
  s_ms += 2000;
  CHECK(gs_save_active(true));
  CHECK(kv_mem_corrupt("box0", 6));
  CHECK(kv_mem_corrupt("box1", 6));

  Config cfg2;
  CHECK_EQ((int)gs_load(cfg2), (int)LOAD_CORRUPT);

  const uint32_t puts_before = kv_mem_puts();
  Config cfg3;
  CHECK_EQ((int)gs_recover(cfg3), (int)LOAD_CORRUPT);
  CHECK(gs_readonly());                         // still nobody's decision but ours
  CHECK_EQ(kv_mem_puts(), puts_before);         // and still not one byte written
}

TEST(game_state_mirrors_the_last_seen_epoch_into_rtc) {
  begin();
  Config cfg;
  CHECK_EQ((int)gs_load(cfg), (int)LOAD_FRESH);
  gs_touch_lastseen(1700400000u);
  CHECK_EQ(boot_host_mirror(), 1700400000u);
  CHECK_EQ(save_last_seen(), 1700400000u);
}

// =============================================================================
//  THE CREATOR PIN STATE (P8-C1)
//
//  ConfigV2 has carried creator_pin, pin_fail_count, pin_lock_until and
//  creator_idle_s since the v2 schema was written and NOTHING had ever read or
//  written them. These cases are the other half of tests/test_creator_gate.cpp:
//  that binary owns the RULES with no store at all, this one owns the ROUND TRIP
//  through the real save_manager and the kv_mem fake. Neither could see the
//  other's half, which is why both exist.
// =============================================================================

TEST(game_state_creator_pin_survives_a_reboot) {
  begin();
  if (!seed_v1()) { CHECK(false); return; }
  Config cfg;
  CHECK_EQ((int)gs_load(cfg), (int)LOAD_MIGRATED);

  // A fresh device has issued nothing. 0 is the sentinel, and it is what makes
  // networking/webui.cpp's "mint on first CREATOR entry" decidable at all.
  uint16_t pin = 0xFFFFu, idle = 0xFFFFu;
  uint8_t  fails = 0xFFu;
  gs_creator_load(pin, fails, idle);
  CHECK_EQ((int)pin, 0);
  CHECK_EQ((int)fails, 0);
  // cfgv2_defaults() already ships D7's 300, so a device made by THIS firmware
  // never sees the zero. A save written before the field existed would, which
  // is what cg_idle_seconds(0) is for - and that half is proved in
  // tests/test_creator_gate.cpp, where a zero can actually be constructed.
  CHECK_EQ((int)idle, (int)CREATOR_IDLE_S_DEFAULT);

  CHECK(gs_creator_store(4242u, 0u, 0u));

  // THE REBOOT. Nothing in RAM survives; the store does.
  Config back;
  CHECK_EQ((int)gs_load(back), (int)LOAD_OK);
  pin = 0; fails = 0xFFu; idle = 0xFFFFu;
  gs_creator_load(pin, fails, idle);
  CHECK_EQ((int)pin, 4242);
  CHECK_EQ((int)fails, 0);
}

// The armed edge is the half that MUST survive: a power cycle that reset the
// failure counter would make the lockout worthless, because an attacker in
// range of a device on a desk can wait for it to be unplugged, and an attacker
// who can unplug it can also read the PIN off the screen. The point is that the
// counter is not free to reset ITSELF.
TEST(game_state_creator_lockout_edge_survives_a_reboot) {
  begin();
  if (!seed_v1()) { CHECK(false); return; }
  Config cfg;
  CHECK_EQ((int)gs_load(cfg), (int)LOAD_MIGRATED);

  CHECK(gs_creator_store(4242u, (uint8_t)CREATOR_PIN_FAIL_MAX, 1700300060u));

  Config back;
  CHECK_EQ((int)gs_load(back), (int)LOAD_OK);
  uint16_t pin = 0, idle = 0;
  uint8_t  fails = 0;
  gs_creator_load(pin, fails, idle);
  CHECK_EQ((int)pin, 4242);
  CHECK_EQ((int)fails, (int)CREATOR_PIN_FAIL_MAX);
  // And the mirror is there for DIAG. It is NOT a deadline: nothing reads it
  // back, because a wall-clock deadline on this device is settable by the phone
  // on the far side of the very gate it guards (networking/creator_gate.h).
  CHECK_EQ((unsigned long)gs_state().cfg.pin_lock_until, 1700300060ul);
}

// THE WEAR PROPERTY, AND IT IS A SECURITY PROPERTY. The only client that can
// drive this path is an unauthenticated HTTP request, so one flash write per
// wrong PIN would be write amplification the rate limiter cannot stop. A store
// that changes nothing must touch nothing.
TEST(game_state_creator_store_writes_nothing_when_nothing_changed) {
  begin();
  if (!seed_v1()) { CHECK(false); return; }
  Config cfg;
  CHECK_EQ((int)gs_load(cfg), (int)LOAD_MIGRATED);

  CHECK(gs_creator_store(4242u, 0u, 0u));
  const uint32_t after_first = kv_mem_puts();

  for (int i = 0; i < 20; ++i) CHECK(gs_creator_store(4242u, 0u, 0u));
  CHECK_EQ((unsigned long)kv_mem_puts(), (unsigned long)after_first);

  // A real edge still writes.
  CHECK(gs_creator_store(4242u, (uint8_t)CREATOR_PIN_FAIL_MAX, 99u));
  CHECK(kv_mem_puts() > after_first);
}

// A refused load leaves the session read-only, and a read-only session writes
// nothing - the rule the whole P2-C9 pipeline exists to enforce. A PIN is not
// an exception to it: minting one over a save the device has been told not to
// touch would be the first write of a session that promised none.
TEST(game_state_creator_store_refuses_a_read_only_session) {
  begin();
  gs_set_readonly(true);
  const uint32_t before = kv_mem_puts();
  CHECK(!gs_creator_store(4242u, 0u, 0u));
  CHECK_EQ((unsigned long)kv_mem_puts(), (unsigned long)before);
}

// SETTINGS and the PIN share one 256 B blob and must not clobber each other:
// cfg_to_v2() writes brightness, tz, the flag bits and the name, and touches
// none of the four creator fields.
TEST(game_state_a_settings_write_does_not_clobber_the_pin) {
  begin();
  if (!seed_v1()) { CHECK(false); return; }
  Config cfg;
  CHECK_EQ((int)gs_load(cfg), (int)LOAD_MIGRATED);
  CHECK(gs_creator_store(4242u, (uint8_t)CREATOR_PIN_FAIL_MAX, 777u));

  cfg.brightness = 44;
  snprintf(cfg.pet_name, sizeof cfg.pet_name, "Roca");
  CHECK(gs_save_cfg(cfg));

  uint16_t pin = 0, idle = 0;
  uint8_t  fails = 0;
  gs_creator_load(pin, fails, idle);
  CHECK_EQ((int)pin, 4242);
  CHECK_EQ((int)fails, (int)CREATOR_PIN_FAIL_MAX);
  CHECK_EQ((unsigned long)gs_state().cfg.pin_lock_until, 777ul);
  CHECK_EQ((int)gs_state().cfg.brightness, 44);
}

// =============================================================================
//  THE BOOT SEAM, PINNED WHERE IT COULD BE
//  Added at the FINAL REVIEW, because tests/fakes/SHADOWS.txt now names these
//  three cases as the anchors for five of the eleven boot_host rows, and
//  tools/check.sh refuses an IMITATION row that points at nothing.
//
//  The other six rows are marked INJECTOR and are NOT imitated - boot_begin()
//  always reports an intact RTC, boot_kind() collapses seven kinds to two - and
//  saying so in the manifest is the honest alternative to a test that would be
//  green about a device property no host can produce. hardware/boot_reason.h is
//  where the real classifier is held instead, host-linkable and swept over all
//  256 esp_reset_reason_t values by tests/test_clock.cpp.
// =============================================================================

// The one pair the suite has always asserted on, and the subtle part is that
// the fake gets it right: a mirror write is NOT visible to the getter within
// the same power cycle. On the device the getter returns s_boot_seen, the
// snapshot boot_begin() took, while the writer touches the live RtcKeep. Had
// the fake collapsed the two into one variable, this test would have been green
// and the device's crash-absence behaviour unproven - app/app.cpp's
// `if (boot_rtc_last_seen() > save_last_seen())` is the line that makes a crash
// reboot report ~0 s of absence instead of up to one 60 s save period.
TEST(the_boot_snapshot_is_not_the_live_mirror) {
  begin();
  CHECK_EQ(boot_host_mirror(), 0u);
  CHECK_EQ(boot_rtc_last_seen(), 0u);

  gs_touch_lastseen(1700400000u);
  CHECK_EQ(boot_host_mirror(), 1700400000u);      // the write landed...
  CHECK_EQ(boot_rtc_last_seen(), 0u);             // ...and this boot cannot see it

  // The zero guard, which both bodies have and which stops an uncalibrated
  // device from mirroring an epoch of 0 over a real one.
  boot_touch_lastseen(0u);
  CHECK_EQ(boot_host_mirror(), 1700400000u);

  // Only a new power cycle publishes it, which the hook models directly.
  boot_host_set_rtc_last_seen(1700400000u);
  CHECK_EQ(boot_rtc_last_seen(), 1700400000u);
}

TEST(the_taint_survives_and_a_rearm_launders_it) {
  begin();
  CHECK(!boot_god_tainted());
  boot_mark_god();
  CHECK(boot_god_tainted());
  CHECK(boot_god_tainted());        // it is a latch, not a one-shot

  // GAME_DESIGN 10.2's ribbon depends on this surviving an ordinary reboot; on
  // the device it lives in RTC fast memory and only a full rearm clears it.
  boot_rearm();
  CHECK(!boot_god_tainted());
}

// THE DIVERGENCE THIS CASE WAS WRITTEN FOR. The shipping boot_rearm() opens
// with nonce_new(), which memsets the whole RtcKeep - so god_taint goes to zero
// - and then sets s_kind = BOOT_FIRST_RUN. The fake did neither: it imitated
// the two effects nothing observes (the boot counter and the epoch) and missed
// both effects that change a verdict. Measured against the real body compiled
// on the host: after mark_god() then rearm(), boot_god_tainted() was fake=1 /
// ship=0 and boot_kind() was fake=SOFT_RESET(3) / ship=FIRST_RUN(0).
//
// gs_factory_reset() is boot_rearm()'s sole caller and is reachable from
// SETTINGS -> "Reset de fabrica" and from bench item G1, so this is what the
// device does after a wipe. Nothing asserted on it, which is what made it the
// P10 shape ONE EDIT EARLY: the first test anybody wrote about post-reset boot
// state would have been written against the fake's answer and been green and
// wrong.
TEST(a_rearm_relabels_the_session_and_launders_the_taint) {
  begin();
  boot_note_save(true);
  CHECK_EQ((int)boot_kind(), (int)BOOT_SOFT_RESET);
  boot_mark_god();
  gs_touch_lastseen(1700400000u);
  CHECK(boot_host_mirror() != 0u);

  boot_rearm();

  CHECK_EQ((int)boot_kind(), (int)BOOT_FIRST_RUN);   // ...not SOFT_RESET
  CHECK(!boot_god_tainted());                        // ...and the taint is gone
  CHECK_EQ(boot_host_mirror(), 0u);
  CHECK_EQ(boot_rtc_last_seen(), 0u);
  CHECK_EQ(boot_count(), 1u);
}

// AND THE FACTORY RESET REALLY GOES THROUGH IT. gs_factory_reset() is the only
// caller in the tree, so if it ever stops calling boot_rearm() the device keeps
// a god-mode taint across a wipe and reports the wrong BootKind for the rest of
// the power cycle - neither of which any other test would notice.
TEST(a_factory_reset_rearms_the_boot_record) {
  begin();
  boot_note_save(true);
  boot_mark_god();
  gs_touch_lastseen(1700400000u);

  CHECK(gs_factory_reset());

  CHECK(!boot_god_tainted());
  CHECK_EQ((int)boot_kind(), (int)BOOT_FIRST_RUN);
  CHECK_EQ(boot_host_mirror(), 0u);
}
