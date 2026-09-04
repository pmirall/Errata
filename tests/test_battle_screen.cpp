// =============================================================================
//  Pebblebol host tests - test_battle_screen.cpp
//  THE BATTLE SCREEN'S BEHAVIOUR (plan P4-C4). The PIXELS are
//  tests/test_screens.cpp's five goldens; this file is everything else, and it
//  drives the REAL screen over the REAL engine and the REAL AI - ui/
//  screen_battle.cpp is a pure translation unit, so nothing here is a mock of
//  anything.
//
//  THE FOUR PROMISES UNDER TEST, in the order the plan makes them:
//    1. the screen never offers - and never submits - an action the engine
//       would refuse, in any mode, in any state, for any seed;
//    2. the log ring is sized against the WORST round, not the typical one;
//    3. one battle reports its result exactly once whatever route is taken out
//       of the screen, and an abandoned battle pays nothing;
//    4. the Box is never written, so an interrupted battle cannot corrupt it.
//
//  Plus the two things P4-C4 lifted out of ui/petfx.cpp: the horizontal flip
//  (which nothing in this repository compiled, let alone executed, until it
//  moved) and the species-keyed 24x24 combat body.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "core/nt_types.h"
#include "core/strings_es.h"
#include "data/sprites.h"
#include "fakes/gfx_fb.h"
#include "game/battle.h"
#include "game/box.h"
#include "game/genome.h"
#include "ui/battle_renderer.h"
#include "ui/screen_battle.h"
#include "ui/ui.h"
#include "ui/xbm_mirror.h"

// =============================================================================
//  THE SEAMS. Nine of them, which is the whole of what a pure battle screen
//  cannot do for itself.
// =============================================================================
static uint32_t g_now      = 100000u;
static uint16_t g_toast    = STR_EMPTY;
static uint16_t g_help     = STR_EMPTY;
static int      g_backs    = 0;
static int      g_wiggles  = 0;
static int      g_results  = 0;
static uint8_t  g_res_entry = 0xFF;
static uint8_t  g_res_won  = 0xFF;
static uint8_t  g_hold_fps = 0;
static int      g_holds    = 0;
static int      g_flashes  = 0;
static int      g_shakes   = 0;

uint32_t ui_now_ms(void)   { return g_now; }
uint32_t ui_idle_ms(void)  { return 0; }
void ui_toast(uint16_t id) { g_toast = id; }
void ui_help(uint16_t id)  { g_help = id; }
void ui_back(void)         { ++g_backs; }
void ui_wiggle(void)       { ++g_wiggles; }
void ui_battle_result(uint8_t entry, uint8_t won) {
  ++g_results; g_res_entry = entry; g_res_won = won;
}
void ui_hold_fps(uint8_t f, uint16_t) { g_hold_fps = f; ++g_holds; }
void ui_flash(uint16_t)               { ++g_flashes; }
void ui_shake(uint8_t, uint16_t)      { ++g_shakes; }

static void seams_reset(void) {
  g_now = 100000u; g_toast = STR_EMPTY; g_help = STR_EMPTY;
  g_backs = 0; g_wiggles = 0; g_results = 0;
  g_res_entry = 0xFF; g_res_won = 0xFF;
  g_hold_fps = 0; g_holds = 0; g_flashes = 0; g_shakes = 0;
}

// =============================================================================
//  A REAL BOX, exactly as tests/test_screens.cpp builds one: box_new_pebble()
//  rather than a hand-drawn mock, so the movesets, the ids and the levels are
//  the ones the firmware would actually hand the engine.
// =============================================================================
static GameState g_gs;

static void box_fixture(uint8_t occupied) {
  memset(&g_gs, 0, sizeof g_gs);
  box_bind(g_gs);
  Genome gen;
  memset(&gen, 0, sizeof gen);
  gen.magic_ver  = GENOME_MAGIC_VER;
  gen.lineage_id = 0x0BADF00Du;
  gen.g0 = 0x1234u; gen.g1 = 0x5678u; gen.g2 = 0x9ABCu;
  gen.generation = 3;
  for (uint8_t i = 0; i < occupied; ++i) {
    const uint8_t slot = box_new_pebble((uint8_t)(1u + i * 4u), (uint8_t)(6u + i * 2u),
                                        ORIGIN_STARTER, gen, 0xC0FFEEu + i, 1000u);
    CHECK(slot != BOX_SLOT_NONE);
    PebbleInstance* p = box_slot(slot);
    if (!p) continue;
    for (uint8_t c = 0; c < PB_CARE_COUNT; ++c) p->care[c] = PB_CARE_MILLI_MAX;
    if (i == 1u) snprintf(p->nickname, sizeof p->nickname, "ABCDEFGHIJKL");
  }
  if (occupied) CHECK(box_set_active(0));
}

// =============================================================================
//  THE DRIVER. Every gesture below is one a player could make; there is no
//  back door into the screen's state.
// =============================================================================

// PICK: choose `n` occupied slots and press LISTO. The ring only stops on
// occupied slots and on the LISTO row, so walking it is how a slot is reached.
static void pick_team(uint8_t n) {
  for (uint8_t i = 0; i < n; ++i) {
    battle_input(GST_HOLD_R);                 // toggle the slot under the cursor
    battle_input(GST_TAP_L);                  // and step to the next legal row
  }
  while (battle_screen_cursor() < (uint8_t)BOX_SLOTS) battle_input(GST_TAP_L);
  battle_input(GST_HOLD_R);                   // LISTO
}

// Walk the whole ring of the current mode and require the engine to accept
// every row it stops on. THIS IS PROMISE 1. Leaves the cursor where it found it.
static void sweep_ring(int& stops, int& blocked_seen) {
  const uint8_t start = battle_screen_cursor();
  for (uint8_t k = 0; k < 12u; ++k) {
    CHECK_EQ(battle_screen_cursor_reject(), (uint8_t)BR_OK);
    ++stops;
    if (battle_screen_blocked_rows() > 0u) ++blocked_seen;
    battle_input(GST_TAP_L);
    if (battle_screen_cursor() == start) break;
  }
  while (battle_screen_cursor() != start) battle_input(GST_TAP_L);
}

struct RunStats {
  int      rounds;
  int      stops;
  int      blocked_seen;
  int      hit_beats;
  int      faint_beats;
  uint16_t widest_round;      // events logged in the busiest single round
  uint8_t  outcome;
};

// One whole battle, from the pick list to the result screen, choosing whatever
// the ring's first legal row is every round.
static void run_battle(uint32_t seed, uint8_t entry, RunStats& out) {
  memset(&out, 0, sizeof out);
  battle_arm(entry, seed);
  battle_enter();
  if (entry == BT_ENTRY_PRACTICE) {
    CHECK_EQ(battle_screen_mode(), (uint8_t)BTM_PICK);
    pick_team((uint8_t)BATTLE_TEAM_MAX);
  }
  CHECK_EQ(battle_screen_mode(), (uint8_t)BTM_INTRO);
  battle_input(GST_HOLD_R);                   // skip the stare-down

  for (int guard = 0; guard < 4000; ++guard) {
    const uint8_t m = battle_screen_mode();
    if (m == BTM_RESULT) break;
    if (m == BTM_MENU || m == BTM_SWITCH) {
      sweep_ring(out.stops, out.blocked_seen);
      ++out.rounds;
      battle_input(GST_HOLD_R);
      continue;
    }
    if (m == BTM_RESOLVE) {
      // Step the transcript one beat at a time rather than skipping it: the
      // beats are what the goldens are of and what the flash and the shake
      // hang off.
      const uint8_t k = battle_screen_event();
      if (k == RLE_HIT)   ++out.hit_beats;
      if (k == RLE_FAINT) ++out.faint_beats;
      if (battle_screen_round() > out.widest_round) out.widest_round = battle_screen_round();
      battle_input(GST_TAP_L);
      continue;
    }
    break;                                     // no other mode is reachable here
  }
  out.outcome = battle_screen_outcome();
}

// =============================================================================
//  1. THE PROMISE
// =============================================================================
TEST(the_screen_never_offers_an_action_the_engine_would_refuse) {
  int total_stops = 0, total_blocked = 0, decided = 0;
  for (uint32_t s = 0; s < 64u; ++s) {
    seams_reset();
    box_fixture(3);
    RunStats r;
    run_battle(0x1000u + s * 0x9E3779B9u, BT_ENTRY_PRACTICE, r);
    total_stops   += r.stops;
    total_blocked += r.blocked_seen;
    if (r.outcome != (uint8_t)BO_UNDECIDED) ++decided;
    // Nothing this screen handed the engine was refused, ever.
    CHECK_EQ(battle_screen_reject(), (uint8_t)BR_OK);
    CHECK(battle_screen_submits() > 0);
    battle_leave();
  }
  // The sweep has to have MET the situation it is about, or it proved nothing:
  // 64 battles must include menus where the engine refuses at least one row
  // (a move on cooldown, an empty bench slot, the active one, the closed way
  // out of a forced replacement).
  CHECK(total_blocked > 0);
  CHECK(total_stops > 1000);
  CHECK_EQ(decided, 64);
  printf("  64 practice battles: %d cursor stops, %d of them in a menu with a "
         "refused row, all %d decided\n", total_stops, total_blocked, decided);
}

// The one row the ring deliberately CLOSES rather than steps over: the way out
// of the switch list while the engine says a replacement is owed. B there is a
// dead end and answers with the wiggle, not by leaving a fight half-played.
TEST(a_forced_replacement_has_no_way_out_but_a_replacement) {
  seams_reset();
  box_fixture(3);
  battle_arm(BT_ENTRY_PRACTICE, 0x51DE0001u);
  battle_enter();
  pick_team((uint8_t)BATTLE_TEAM_MAX);
  battle_input(GST_HOLD_R);

  int forced = 0, dead_ends = 0;
  for (int guard = 0; guard < 4000 && battle_screen_mode() != BTM_RESULT; ++guard) {
    if (battle_screen_mode() == BTM_SWITCH) {
      // The engine put us here, not the player: a forced replacement is the
      // only way BTM_SWITCH is entered without pressing CAMBIAR.
      ++forced;
      // TRY to reach the "Volver" row. A full lap of the list never gets there
      // while a replacement is owed - and the lap is BOUNDED, because an
      // unbounded one would HANG rather than fail if the guard were removed,
      // and a test that hangs on the mutation it is meant to catch is worse
      // than one that misses it.
      bool reached_the_way_out = false;
      for (uint8_t k = 0; k < (uint8_t)(BATTLE_TEAM_MAX + 2u); ++k) {
        if (battle_screen_cursor() >= (uint8_t)BATTLE_TEAM_MAX) reached_the_way_out = true;
        CHECK_EQ(battle_screen_cursor_reject(), (uint8_t)BR_OK);
        battle_input(GST_TAP_L);
      }
      CHECK(!reached_the_way_out);
      // And B there is a dead end that SAYS so, rather than abandoning a fight
      // that is one press from continuing.
      const int w = g_wiggles;
      const uint8_t before = battle_screen_mode();
      battle_input(GST_TAP_R);
      CHECK_EQ(battle_screen_mode(), before);
      CHECK_EQ(g_wiggles, w + 1);
      CHECK_EQ(g_backs, 0);
      ++dead_ends;
    }
    battle_input(GST_HOLD_R);
  }
  CHECK(forced > 0);
  CHECK_EQ(dead_ends, forced);
  printf("  %d forced replacements, none with a way out but a replacement\n", forced);
  battle_leave();
}

// =============================================================================
//  2. THE RING IS SIZED AGAINST THE WORST ROUND
// =============================================================================
TEST(the_transcript_ring_never_drops_an_event) {
  uint16_t worst_round = 0;
  for (uint32_t s = 0; s < 64u; ++s) {
    seams_reset();
    box_fixture(3);
    RunStats r;
    run_battle(0x2000u + s * 0x85EBCA6Bu, BT_ENTRY_PRACTICE, r);
    // A dropped event is a hole in a transcript the state hash cannot see.
    CHECK_EQ(battle_screen_dropped(), 0u);
    if (r.widest_round > worst_round) worst_round = r.widest_round;
    battle_leave();
  }
  printf("  64 battles, no event dropped; longest battle %u rounds\n",
         (unsigned)worst_round);
}

// =============================================================================
//  3. ONE REPORT PATH
// =============================================================================
TEST(one_battle_reports_its_result_exactly_once) {
  // (a) fought to the end and then left.
  seams_reset();
  box_fixture(3);
  RunStats r;
  run_battle(0x3000u, BT_ENTRY_PRACTICE, r);
  CHECK_EQ(battle_screen_mode(), (uint8_t)BTM_RESULT);
  CHECK_EQ(g_results, 1);
  CHECK_EQ(battle_screen_reports(), 1u);
  CHECK_EQ(g_res_entry, (uint8_t)BT_ENTRY_PRACTICE);
  CHECK_EQ(g_res_won, (uint8_t)(r.outcome == (uint8_t)BO_WIN_A ? 1u : 0u));
  battle_leave();                       // the second exit must not report again
  CHECK_EQ(g_results, 1);
  battle_leave();
  CHECK_EQ(g_results, 1);

  // (b) ABANDONED at the menu. Reported once, as a loss, so nothing is paid.
  seams_reset();
  box_fixture(3);
  battle_arm(BT_ENTRY_PRACTICE, 0x3001u);
  battle_enter();
  pick_team((uint8_t)BATTLE_TEAM_MAX);
  battle_input(GST_HOLD_R);             // INTRO -> MENU
  CHECK_EQ(battle_screen_mode(), (uint8_t)BTM_MENU);
  battle_input(GST_HOLD_R);             // one round, so the fight really started
  battle_leave();
  CHECK_EQ(g_results, 1);
  CHECK_EQ(g_res_won, 0u);

  // (c) never started at all: the pick list, abandoned. Still exactly one
  // report, and still a loss - there is nothing to pay for.
  seams_reset();
  box_fixture(3);
  battle_arm(BT_ENTRY_PRACTICE, 0x3002u);
  battle_enter();
  battle_leave();
  CHECK_EQ(g_results, 1);
  CHECK_EQ(g_res_won, 0u);
}

// The DIAG entry reports the entry it was ARMED with, which is the one byte
// ui.cpp's ui_battle_result() branches on to decide that a diagnostic pays
// nothing. (ui.cpp is not host-linkable; this is the half of that rule a host
// test can hold.)
TEST(the_diag_entry_reports_itself_as_a_diagnostic) {
  seams_reset();
  box_fixture(0);                       // a device that has never filled its Box
  RunStats r;
  run_battle((uint32_t)BT_DIAG_SEED, BT_ENTRY_DIAG, r);
  CHECK_EQ(battle_screen_mode(), (uint8_t)BTM_RESULT);
  CHECK_EQ(g_res_entry, (uint8_t)BT_ENTRY_DIAG);
  CHECK_EQ(g_results, 1);
  // A synthetic team, so the empty Box did not stop it.
  CHECK(r.rounds > 0);
  battle_leave();
}

// Spec section 49's "deterministic RNG seed": the same seed replays the same
// battle, event for event, on the same build.
TEST(the_diag_seed_replays_the_same_battle) {
  RunStats a, b;
  seams_reset(); box_fixture(0); run_battle((uint32_t)BT_DIAG_SEED, BT_ENTRY_DIAG, a);
  const uint16_t rounds_a = battle_screen_round();
  battle_leave();
  seams_reset(); box_fixture(0); run_battle((uint32_t)BT_DIAG_SEED, BT_ENTRY_DIAG, b);
  const uint16_t rounds_b = battle_screen_round();
  battle_leave();
  CHECK_EQ(a.outcome, b.outcome);
  CHECK_EQ(a.rounds, b.rounds);
  CHECK_EQ(a.hit_beats, b.hit_beats);
  CHECK_EQ(a.faint_beats, b.faint_beats);
  CHECK_EQ(rounds_a, rounds_b);
  // And a NEIGHBOURING seed does not, or the case above would pass on a screen
  // that ignored the seed entirely.
  RunStats c;
  seams_reset(); box_fixture(0);
  run_battle((uint32_t)BT_DIAG_SEED + 1u, BT_ENTRY_DIAG, c);
  battle_leave();
  CHECK(c.rounds != a.rounds || c.hit_beats != a.hit_beats ||
        c.outcome != a.outcome);
}

// =============================================================================
//  4. THE BOX IS NEVER WRITTEN
// =============================================================================
TEST(a_battle_leaves_the_box_byte_identical) {
  seams_reset();
  box_fixture(3);
  static GameState before;
  memcpy(&before, &g_gs, sizeof before);

  RunStats r;
  run_battle(0x4000u, BT_ENTRY_PRACTICE, r);
  battle_leave();
  CHECK_EQ(memcmp(&before, &g_gs, sizeof before), 0);

  // And an INTERRUPTED one, which is the case the plan names: the minigame
  // manager's bug was on the abandon path, not the finish path.
  memcpy(&before, &g_gs, sizeof before);
  battle_arm(BT_ENTRY_PRACTICE, 0x4001u);
  battle_enter();
  pick_team((uint8_t)BATTLE_TEAM_MAX);
  battle_input(GST_HOLD_R);
  for (int i = 0; i < 6; ++i) battle_input(GST_HOLD_R);
  battle_leave();
  CHECK_EQ(memcmp(&before, &g_gs, sizeof before), 0);

  // The fight really did hurt somebody - otherwise "unchanged" would be a
  // statement about a battle that never happened.
  CHECK(r.hit_beats > 0);
}

// A stored Pebble at 1 HP can still practise, because the team is a COPY and
// the copy is healed. The Box's own hp_cur is what must not move.
TEST(a_practice_team_is_a_healed_copy_and_the_stored_one_is_not_touched) {
  seams_reset();
  box_fixture(3);
  PebbleInstance* p = box_slot(0);
  CHECK(p != nullptr);
  if (!p) return;
  p->hp_cur = 1u;
  p->status = (uint8_t)(p->status | PBS_FAINTED);
  const uint16_t stored_hp = p->hp_cur;

  RunStats r;
  run_battle(0x4100u, BT_ENTRY_PRACTICE, r);
  // battle_init() refuses a fainted member by name; reaching a result at all is
  // the proof that the copy was healed.
  CHECK_EQ(battle_screen_reject(), (uint8_t)BR_OK);
  CHECK_EQ(battle_screen_mode(), (uint8_t)BTM_RESULT);
  CHECK_EQ(box_slot(0)->hp_cur, stored_hp);
  CHECK((box_slot(0)->status & PBS_FAINTED) != 0u);
  battle_leave();
}

// =============================================================================
//  THE TRANSCRIPT SHOWS THE ROUND, NOT ITS END STATE
// =============================================================================
TEST(the_playback_draws_the_hp_of_the_beat_and_not_of_the_round_end) {
  // A whole round is resolved before its first frame is drawn. If the playback
  // read hp_cur it would show the round's FINAL numbers under every one of its
  // events, and a hit would land on a bar that had already fallen. Replaying
  // RLE_HP / RLE_DOT up to the current beat is what stops that - so the HP at a
  // round's FIRST beat must be strictly greater than at its last, in every
  // round that dealt damage.
  int moved = 0, rounds = 0;
  for (uint32_t seed = 0; seed < 8u; ++seed) {
    seams_reset();
    box_fixture(3);
    battle_arm(BT_ENTRY_PRACTICE, 0x5000u + seed * 0x2545F491u);
    battle_enter();
    pick_team((uint8_t)BATTLE_TEAM_MAX);
    battle_input(GST_HOLD_R);                  // INTRO -> MENU

    for (int guard = 0; guard < 4000 && battle_screen_mode() != BTM_RESULT; ++guard) {
      if (battle_screen_mode() != BTM_RESOLVE) { battle_input(GST_HOLD_R); continue; }
      ++rounds;
      const uint16_t first0 = battle_screen_hp_shown(0);
      const uint16_t first1 = battle_screen_hp_shown(1);
      while (battle_screen_mode() == BTM_RESOLVE) battle_input(GST_TAP_L);
      // Out of RESOLVE the query answers the state's own hp_cur, which is the
      // round's settled value - the thing the playback must NOT have been
      // showing all along.
      const uint16_t last0 = battle_screen_hp_shown(0);
      const uint16_t last1 = battle_screen_hp_shown(1);
      if (first0 > last0 || first1 > last1) ++moved;
    }
    battle_leave();
  }
  CHECK(rounds > 0);
  CHECK(moved > 0);
  printf("  %d of %d replayed rounds opened on more HP than they closed on\n",
         moved, rounds);
}

// The two frame-level effects are EVENTS, not pixels: one shake per landed hit
// and one flash per faint, raised when the beat is entered and not once per
// drawn frame.
TEST(a_hit_shakes_once_and_a_faint_flashes_once) {
  seams_reset();
  box_fixture(3);
  RunStats r;
  run_battle(0x6000u, BT_ENTRY_PRACTICE, r);
  CHECK(r.hit_beats > 0);
  CHECK(r.faint_beats > 0);
  CHECK_EQ(g_shakes, r.hit_beats);
  CHECK_EQ(g_flashes, r.faint_beats);
  battle_leave();
}

// 20 fps is HELD, not requested: rd_fps() caps a bare request at FPS_LOW while
// a web client is live, and the hold is what beats that cap. It is renewed
// every update() so it dies on its own the moment this screen stops running.
TEST(the_screen_holds_twenty_fps_from_its_update_hook) {
  seams_reset();
  box_fixture(3);
  battle_arm(BT_ENTRY_PRACTICE, 0x7000u);
  battle_enter();
  CHECK_EQ(g_holds, 0);
  battle_update(g_now);
  CHECK_EQ(g_holds, 1);
  CHECK_EQ(g_hold_fps, (uint8_t)FPS_NORMAL);
  battle_update(g_now);
  CHECK_EQ(g_holds, 2);                 // renewed, every loop
  battle_leave();
}

// The stare-down ends on its own clock as well as on a press, so a player who
// puts the device down still gets to choose a move.
TEST(the_intro_ends_on_its_own_clock) {
  seams_reset();
  box_fixture(3);
  battle_arm(BT_ENTRY_PRACTICE, 0x7100u);
  battle_enter();
  pick_team((uint8_t)BATTLE_TEAM_MAX);
  CHECK_EQ(battle_screen_mode(), (uint8_t)BTM_INTRO);
  battle_update(g_now);
  CHECK_EQ(battle_screen_mode(), (uint8_t)BTM_INTRO);
  g_now += 5000u;
  battle_update(g_now);
  CHECK_EQ(battle_screen_mode(), (uint8_t)BTM_MENU);
  battle_leave();
}

// =============================================================================
//  THE PICK LIST
// =============================================================================
TEST(the_pick_list_takes_three_occupied_slots_and_no_more) {
  seams_reset();
  box_fixture(5);
  battle_arm(BT_ENTRY_PRACTICE, 0x8000u);
  battle_enter();
  CHECK_EQ(battle_screen_mode(), (uint8_t)BTM_PICK);
  CHECK_EQ(battle_screen_picked(), 0u);

  // Four presses on four different occupied slots; the fourth is refused.
  for (uint8_t i = 0; i < 4u; ++i) { battle_input(GST_HOLD_R); battle_input(GST_TAP_L); }
  CHECK_EQ(battle_screen_picked(), (uint8_t)BATTLE_TEAM_MAX);
  CHECK_EQ(g_toast, (uint16_t)STR_BT_FULL_TEAM);

  // A second press on a chosen slot takes it back out.
  while (battle_screen_cursor() != 0u) battle_input(GST_TAP_L);
  battle_input(GST_HOLD_R);
  CHECK_EQ(battle_screen_picked(), 2u);
  battle_leave();
}

TEST(an_empty_box_cannot_start_a_practice_battle) {
  seams_reset();
  box_fixture(0);
  battle_arm(BT_ENTRY_PRACTICE, 0x8100u);
  battle_enter();
  CHECK_EQ(battle_screen_mode(), (uint8_t)BTM_PICK);
  CHECK_EQ(g_toast, (uint16_t)STR_BT_NO_TEAM);
  // The ring stops only on the LISTO row, and LISTO with nothing chosen says so
  // rather than starting a battle with an empty side.
  CHECK_EQ(battle_screen_cursor(), (uint8_t)BOX_SLOTS);
  battle_input(GST_HOLD_R);
  CHECK_EQ(battle_screen_mode(), (uint8_t)BTM_PICK);
  CHECK_EQ(g_toast, (uint16_t)STR_BT_NO_TEAM);
  battle_leave();
}

// A battle can be fought with ONE Pebble. Spec section 67 asks for a 3-Pebble
// team, not for three to be compulsory.
TEST(one_pebble_is_a_legal_team) {
  seams_reset();
  box_fixture(3);
  battle_arm(BT_ENTRY_PRACTICE, 0x8200u);
  battle_enter();
  pick_team(1u);
  CHECK_EQ(battle_screen_mode(), (uint8_t)BTM_INTRO);
  battle_input(GST_HOLD_R);
  CHECK_EQ(battle_screen_mode(), (uint8_t)BTM_MENU);
  // With one Pebble there is nothing to switch to, so the CAMBIAR row is one
  // of the rows the ring steps over.
  CHECK(battle_screen_blocked_rows() > 0u);
  for (uint8_t k = 0; k < 8u; ++k) {
    CHECK_EQ(battle_screen_cursor_reject(), (uint8_t)BR_OK);
    CHECK(battle_screen_cursor() < (uint8_t)PB_MOVE_COUNT);
    battle_input(GST_TAP_L);
  }
  battle_leave();
}

// =============================================================================
//  THE FLIP - ui/xbm_mirror.h, out of ui/petfx.cpp's PIXEL CORE.
//
//  Nothing in this repository ever executed it: the harness petfx.cpp names,
//  scratchpad/petfx/mkharness.py, is not here.
// =============================================================================
static void fill_pattern(uint8_t* buf, uint8_t w, uint8_t h, uint32_t seed) {
  const uint8_t stride = xbm_stride(w);
  memset(buf, 0, (size_t)stride * h);
  uint32_t x = seed | 1u;
  for (uint8_t y = 0; y < h; ++y) {
    for (uint8_t c = 0; c < w; ++c) {
      x ^= x << 13; x ^= x >> 17; x ^= x << 5;
      if (x & 1u) buf[(size_t)y * stride + (c >> 3)] |= (uint8_t)(1u << (c & 7u));
    }
  }
}

static int bit_at(const uint8_t* buf, uint8_t w, uint8_t x, uint8_t y) {
  const uint8_t stride = xbm_stride(w);
  return (buf[(size_t)y * stride + (x >> 3)] >> (x & 7u)) & 1;
}

TEST(the_flip_is_a_flip_at_every_width_the_atlas_uses) {
  static uint8_t a[XBM_MIRROR_MAX_W * 5], b[XBM_MIRROR_MAX_W * 5],
                 c[XBM_MIRROR_MAX_W * 5];
  const uint8_t widths[] = { 8, 16, 24, 28, 32, 40 };
  int pixels = 0;
  for (uint8_t i = 0; i < sizeof widths / sizeof widths[0]; ++i) {
    const uint8_t w = widths[i];
    const uint8_t h = w;
    fill_pattern(a, w, h, 0xA5A50000u + w);
    xbm_mirror_frame(a, b, w, h);
    // Every pixel is where the flip says it is - the direct statement, not a
    // round trip, because mirroring twice is the identity even for a routine
    // that shifts the padding the wrong way at 28 px.
    for (uint8_t y = 0; y < h; ++y)
      for (uint8_t x = 0; x < w; ++x) {
        CHECK_EQ(bit_at(b, w, x, y), bit_at(a, w, (uint8_t)(w - 1u - x), y));
        ++pixels;
      }
    // And the round trip, which catches a lost bit rather than a moved one.
    xbm_mirror_frame(b, c, w, h);
    CHECK_EQ(memcmp(a, c, (size_t)xbm_stride(w) * h), 0);
  }
  printf("  %d mirrored pixels checked across 6 widths\n", pixels);
}

TEST(the_flip_refuses_a_frame_wider_than_the_atlas) {
  static uint8_t a[64 * 8], b[64 * 8];
  memset(a, 0xAB, sizeof a);
  memset(b, 0x11, sizeof b);
  xbm_mirror_frame(a, b, 48, 4);          // 48 > XBM_MIRROR_MAX_W
  for (size_t i = 0; i < sizeof b; ++i) CHECK_EQ(b[i], 0x11u);
  xbm_mirror_frame(nullptr, b, 24, 4);
  for (size_t i = 0; i < sizeof b; ++i) CHECK_EQ(b[i], 0x11u);
}

// =============================================================================
//  THE 24x24 COMBAT BODY
// =============================================================================
TEST(the_species_chooses_the_combat_body) {
  // Every roster row draws a CREATURE at battle size - never an egg, a pose set
  // or one of the two retired bodies.
  int per_set[SPRITE_SET_COUNT];
  memset(per_set, 0, sizeof per_set);
  for (uint8_t id = 1; id <= (uint8_t)SPECIES_TABLE_COUNT; ++id) {
    const SpeciesDef* sp = species_get(id);
    CHECK(sp != nullptr);
    if (!sp) continue;
    const uint8_t set = br_body_set_id(sp->sprite_id);
    CHECK(set >= SPRITE_BODY_FIRST);
    CHECK(set <= SPRITE_BODY_LAST);
    // 24x24, which is what the field geometry is laid out against.
    CHECK_EQ(sprite_set(set).w, (uint8_t)BR_BODY_W);
    CHECK_EQ(sprite_set(set).h, (uint8_t)BR_BODY_H);
    ++per_set[set];
  }
  int n = 0, worst = 0;
  for (int i = 0; i < SPRITE_SET_COUNT; ++i) {
    if (per_set[i]) ++n;
    if (per_set[i] > worst) worst = per_set[i];
  }
  // COUNTING THE DISTINCT BODIES IS NOT ENOUGH, and the first draft of this
  // case did exactly that and could not fail: sprite_set_id() CLAMPS an
  // out-of-pool form to 0, so dropping sprite_design_of()'s fold sends species
  // 9..36 onto SPR_BABY_BLOB and still leaves eight distinct bodies - the eight
  // that species 1..8 reach on their own. What the fold is FOR is the
  // distribution, so that is what is asserted: 36 species over 8 authored
  // bodies is at most ceil(36/8) = 5 species per body, and the unfolded
  // resolution puts 29 of them on one.
  CHECK_EQ(n, SPRITE_BABY_BODIES);
  CHECK(worst <= ((int)SPECIES_TABLE_COUNT + SPRITE_BABY_BODIES - 1) / SPRITE_BABY_BODIES);
  printf("  %d species resolve onto %d distinct 24x24 combat bodies, "
         "at most %d of them sharing one\n",
         (int)SPECIES_TABLE_COUNT, n, worst);
}

TEST(the_two_combatants_face_each_other) {
  // The foe is drawn mirrored and the player is not, so the same art key gives
  // two different pictures. Drawn into the real framebuffer, at the real
  // positions, and compared column by column.
  fb_reset();
  br_draw_body(0, 0, 3u, 0, false, false, false);
  static uint8_t plain[BR_BODY_H][BR_BODY_W];
  for (int y = 0; y < BR_BODY_H; ++y)
    for (int x = 0; x < BR_BODY_W; ++x) plain[y][x] = (uint8_t)fb_get(x, y);

  fb_reset();
  br_draw_body(0, 0, 3u, 0, true, false, false);
  int same = 0, mirrored = 0;
  for (int y = 0; y < BR_BODY_H; ++y)
    for (int x = 0; x < BR_BODY_W; ++x) {
      if ((uint8_t)fb_get(x, y) == plain[y][BR_BODY_W - 1 - x]) ++mirrored;
      if ((uint8_t)fb_get(x, y) == plain[y][x]) ++same;
    }
  CHECK_EQ(mirrored, BR_BODY_W * BR_BODY_H);
  // And the body is not symmetric, so "mirrored" is a claim with content.
  CHECK(same < BR_BODY_W * BR_BODY_H);
  CHECK_EQ(fb_oob(), 0u);
}
