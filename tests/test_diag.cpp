// =============================================================================
//  Pebblebol host tests - test_diag.cpp
//  dev/diag_core.cpp: the arithmetic half of the diagnostics surface (P10-C1,
//  spec sections 49 and 66).
//
//  WHY THIS BINARY EXISTS. dev/godmode.cpp includes <Arduino.h>, so no host
//  binary in this repository compiles it: every line in that file - INCLUDING
//  its GOD_MODE_ENABLED 0 stubs - is untested by construction, and a stub that
//  returned garbage would still print ALL PASS and MATRIX OK. That is the
//  phase-9 defect exactly (pf_build_lids() drawing over four species' bodies
//  inside ui/petfx.cpp while every byte check passed), and the repair is the
//  same one: move everything with a right answer into a pure translation unit
//  and drive it from here.
//
//  THE CASE THIS FILE IS REALLY FOR is the taint. game/sim.cpp's god hooks set
//  PF_GOD_TAINTED themselves; game/box.cpp's constructor does not, and it
//  memsets flags to 0 and takes the genome as given. So a `spawn` written the
//  obvious way mints a CLEAN Pebble inside god mode, which passes
//  taint_gate_ok() and can be traded into an honest player's dynasty. The
//  checks below do not assert "a flag is set": they hand the minted Pebble to
//  taint_gate_ok() against a clean local one and require the REFUSAL, which is
//  the thing game/trade.cpp:87 and game/breeding.cpp:84 actually ask.
// =============================================================================
#include "nt_test.h"

#include <stdio.h>
#include <string.h>

#include "core/config.h"
#include "core/rng.h"
#include "data/items_table.h"
#include "data/species_table.h"
#include "dev/diag_core.h"
#include "game/box.h"
#include "game/genome.h"
#include "game/inventory.h"
#include "game/sim.h"
#include "game/taint.h"
#include "persistence/save_schema.h"

#define DG_EPOCH0   1700000000u

static GameState g_state;
static char      g_buf[1024];

static Genome clean_genome(void) {
  Genome g;
  memset(&g, 0, sizeof g);
  return g;
}

// A bound Box with `n` Pebbles, slot 0 active and the simulation bound to it.
// Every one of them is CLEAN: no genome taint bit, no PBF_GOD_TAINTED. That is
// the precondition every taint case below depends on, so it is asserted here
// rather than assumed.
static void fixture(uint8_t n) {
  rng_seed_all(0xC0FFEEu);
  memset(&g_state, 0, sizeof g_state);
  for (uint8_t i = 0; i < (uint8_t)BOX_SLOTS; ++i) {
    g_state.pebbles[i].magic      = (uint16_t)PEBBLE_MAGIC;
    g_state.pebbles[i].layout_ver = (uint8_t)PEBBLE_LAYOUT_VER;
  }
  g_state.box.magic           = (uint16_t)BOX_MAGIC;
  g_state.box.schema_version  = (uint8_t)SAVE_SCHEMA_VERSION;
  g_state.box.protocol_version= (uint8_t)PROTOCOL_VERSION;
  g_state.box.active_slot     = (uint8_t)BOX_ACTIVE_NONE;
  g_state.box.next_id_counter = 1;
  g_state.cfg.device_id       = 0xB0FFE501u;
  inv_begin(g_state.inv);
  box_bind(g_state);

  for (uint8_t i = 0; i < n; ++i) {
    const uint8_t s = box_new_pebble(SPECIES_ID_STARTER, 5, (uint8_t)ORIGIN_WILD,
                                     clean_genome(), 0x3000u + i, DG_EPOCH0);
    CHECK(s != (uint8_t)BOX_SLOT_NONE);
    CHECK(!pb_is_tainted(*box_slot(s)));
  }
  if (n) {
    CHECK(box_set_active(0));
    sim_bind(*box_slot(0));
  }
}

static DiagCtx ctx(void) {
  DiagCtx c;
  c.gs        = &g_state;
  c.now_epoch = DG_EPOCH0;
  c.cal       = (uint8_t)CAL_USER;
  c.have_sim  = 1u;
  return c;
}

// Parse + run one line. Returns the DiagResult; the text lands in g_buf.
static uint8_t run(const char* line, uint8_t* commit_out = nullptr) {
  DiagParse p;
  diag_parse(line, p);
  DiagOut o;
  diag_out_init(o, g_buf, (uint16_t)sizeof(g_buf));
  uint8_t commit = 0;
  const uint8_t r = diag_exec(p, ctx(), o, commit);
  if (commit_out) *commit_out = commit;
  return r;
}

static bool has(const char* needle) { return strstr(g_buf, needle) != nullptr; }

// A clean Pebble that is NOT in the Box: the honest player's dynasty, for the
// gate to refuse against.
static PebbleInstance clean_stranger(void) {
  PebbleInstance p;
  memset(&p, 0, sizeof p);
  p.magic      = (uint16_t)PEBBLE_MAGIC;
  p.layout_ver = (uint8_t)PEBBLE_LAYOUT_VER;
  p.species_id = (uint8_t)SPECIES_ID_STARTER;
  p.id         = 0xABCD1234u;
  p.level      = 5;
  return p;
}

// =============================================================================
//  1. THE TAINT RULE - the reason this file exists
// =============================================================================

// spawn goes through box_new_pebble(), which memsets flags to 0 and takes the
// genome AS GIVEN, and genome_genesis() CLEARS the taint bit. So without
// diag_taint() this Pebble is clean and tradeable. Delete the diag_taint() call
// from mint_one() in diag_core.cpp and this fails by name.
TEST(spawn_mints_a_pebble_an_honest_dynasty_refuses) {
  fixture(1);
  const uint8_t before = box_count();
  CHECK_EQ(run("spawn 1 7"), (uint8_t)DGR_OK);
  CHECK_EQ((int)box_count(), (int)before + 1);

  // Find the one that was just filed.
  const PebbleInstance* spawned = nullptr;
  for (uint8_t s = 0; s < (uint8_t)BOX_SLOTS; ++s) {
    if (!box_occupied(s)) continue;
    const PebbleInstance* p = box_peek(s);
    if (p && p->level == 7) spawned = p;
  }
  CHECK(spawned != nullptr);

  // BOTH markers, because game/taint.h reads both and a writer that set only
  // one leaves a hole for either a migrated v1 save or an unbound Pebble.
  CHECK(gene_tainted(spawned->genome) != 0u);
  CHECK((spawned->flags & (uint8_t)PBF_GOD_TAINTED) != 0u);

  // THE ACTUAL QUESTION game/trade.cpp:87 and game/breeding.cpp:84 ask.
  const PebbleInstance honest = clean_stranger();
  CHECK(!pb_is_tainted(honest));
  CHECK(!taint_gate_ok(honest, *spawned));   // a clean unit REFUSES it
  CHECK(taint_gate_ok(*spawned, honest));    // a tainted one accepts anything
}

TEST(fill_box_taints_every_slot_it_mints) {
  fixture(1);
  uint8_t commit = 0;
  CHECK_EQ(run("fill_box 3", &commit), (uint8_t)DGR_OK);
  CHECK_EQ((int)box_count(), (int)BOX_SLOTS);
  CHECK((commit & DGCOMMIT_SLOTS) != 0u);

  const PebbleInstance honest = clean_stranger();
  uint8_t minted = 0;
  for (uint8_t s = 0; s < (uint8_t)BOX_SLOTS; ++s) {
    const PebbleInstance* p = box_peek(s);
    CHECK(p != nullptr);
    if (p->level != 3) continue;             // the fixture's own level-5 slot
    ++minted;
    CHECK(!taint_gate_ok(honest, *p));
  }
  CHECK(minted >= 9);
}

// Level lives on the PebbleInstance and there is no sim_god_set_level(), so
// nothing but the explicit diag_taint() in this arm marks it.
TEST(set_level_taints_the_pebble_it_edits) {
  fixture(1);
  const PebbleInstance honest = clean_stranger();
  CHECK(taint_gate_ok(honest, *box_peek(0)));     // clean before

  CHECK_EQ(run("set_level 30"), (uint8_t)DGR_OK);
  CHECK_EQ((int)box_peek(0)->level, 30);
  CHECK(!taint_gate_ok(honest, *box_peek(0)));    // refused after
}

// heal is the first caller sim_god_set_sick() has ever had outside a test:
// game/sim.h declared it in the GOD MODE HOOKS block, sim.cpp implemented it,
// and no console row called it - the same shape as rd_frame_time_us().
TEST(heal_clears_sickness_and_taints_through_the_sim_hook) {
  fixture(1);
  sim_god_set_sick(1);
  CHECK_EQ((int)sim_is_sick(), 1);
  // sim_god_set_sick() taints on its own, so re-arm a clean fixture to make the
  // heal arm answer for itself.
  fixture(1);
  const PebbleInstance honest = clean_stranger();
  CHECK(taint_gate_ok(honest, *box_peek(0)));

  CHECK_EQ(run("heal"), (uint8_t)DGR_OK);
  CHECK_EQ((int)sim_is_sick(), 0);
  CHECK_EQ((int)sim_stat_pct(ST_HEALTH), 100);
  CHECK_EQ((int)sim_stat_pct(ST_HUNGER), 100);
  CHECK(!taint_gate_ok(honest, *box_peek(0)));
}

// THE TABLE-DRIVEN ONE, and it is the case that catches the NEXT command
// somebody adds. Every DCF_TAINTS row is executed on a fresh clean fixture and
// the Box must come back holding at least one Pebble a clean dynasty refuses.
// A minting command added without the flag fails HERE by name, not in a review.
TEST(every_command_marked_DCF_TAINTS_produces_a_refused_pebble) {
  const DiagCmdSpec* t = diag_cmd_table();
  const PebbleInstance honest = clean_stranger();
  uint8_t checked = 0;

  for (uint8_t i = 0; i < (uint8_t)DGC_COUNT; ++i) {
    if (!(t[i].flags & DCF_TAINTS)) continue;
    CHECK(diag_cmd_taints(t[i].id) == 1u);

    fixture(1);
    // Arguments the arm needs, taken from the table's own minimum.
    char line[32];
    if      (t[i].id == DGC_SPAWN)     snprintf(line, sizeof line, "spawn 1 4");
    else if (t[i].id == DGC_SET_LEVEL) snprintf(line, sizeof line, "set_level 12");
    else if (t[i].id == DGC_FILL_BOX)  snprintf(line, sizeof line, "fill_box 2");
    else if (t[i].id == DGC_SET_ACTIVITY) snprintf(line, sizeof line, "set_activity 3");
    else                               snprintf(line, sizeof line, "%s", t[i].name);

    const uint8_t r = run(line);
    // test_evolution's rule may legitimately refuse (the starter's rule needs a
    // level the arm does supply, but a roster change could move it), and the
    // taint is applied either way because the level was edited by hand.
    CHECK(r == (uint8_t)DGR_OK || r == (uint8_t)DGR_ERR_REFUSED);

    bool any_refused = false;
    for (uint8_t s = 0; s < (uint8_t)BOX_SLOTS; ++s) {
      const PebbleInstance* p = box_peek(s);
      if (p && !taint_gate_ok(honest, *p)) any_refused = true;
    }
    CHECK(any_refused);
    ++checked;
  }
  // The table must actually have some, or this case passes over an empty loop -
  // the shape of "an assertion that merely can fail".
  CHECK(checked >= 5);
}

// The deliberate NON-taint, written down as a case so it reads as a decision
// rather than an omission. An Inventory carries no genome and no flags and does
// not cross a trade, so there is no marker for this command to set.
//
// IT IS A CLAIM ABOUT THE COMMAND, NOT ABOUT A DEVICE. dev/godmode.cpp calls
// god_enter() on any mutating typed line, and that taints the living pet
// whatever was typed - so on a board a give_item session still leaves a marked
// creature. This case drives the pure layer, which is where the command's own
// decision lives.
TEST(give_item_changes_the_bag_and_taints_nothing_itself) {
  fixture(1);
  const PebbleInstance honest = clean_stranger();
  uint8_t commit = 0;
  CHECK_EQ(run("give_item 1 5", &commit), (uint8_t)DGR_OK);
  CHECK_EQ((int)inv_count(g_state.inv, 1), 5);
  CHECK_EQ((int)commit, (int)DGCOMMIT_INV);
  CHECK(taint_gate_ok(honest, *box_peek(0)));
  CHECK(diag_cmd_taints((uint8_t)DGC_GIVE_ITEM) == 0u);
  CHECK(has("no taint marker"));
}

TEST(diag_taint_sets_both_markers_and_either_one_alone_is_a_hole) {
  PebbleInstance p = clean_stranger();
  const PebbleInstance honest = clean_stranger();
  CHECK(taint_gate_ok(honest, p));

  // The genome bit alone: what a v1 save cannot carry.
  PebbleInstance a = p;
  gene_set_tainted(a.genome, 1);
  CHECK(!taint_gate_ok(honest, a));

  // The flag alone: what persistence/migration.cpp produces from a v1 save.
  PebbleInstance b = p;
  b.flags = (uint8_t)(b.flags | (uint8_t)PBF_GOD_TAINTED);
  CHECK(!taint_gate_ok(honest, b));

  diag_taint(p);
  CHECK(gene_tainted(p.genome) != 0u);
  CHECK((p.flags & (uint8_t)PBF_GOD_TAINTED) != 0u);
  CHECK(!taint_gate_ok(honest, p));
}

// =============================================================================
//  2. THE RELEASE SPLIT - spec 66's closing line, as a checked property
// =============================================================================

// GOD_MODE_ENABLED is 0 in the shipping artefact, so what carries DCF_ALWAYS is
// what a player's board can be made to do over a serial cable. Marking `spawn`
// DCF_ALWAYS fails here by name (and fails the static_assert in diag_core.cpp).
TEST(every_always_compiled_command_is_read_only) {
  const DiagCmdSpec* t = diag_cmd_table();
  uint8_t always = 0;
  for (uint8_t i = 0; i < (uint8_t)DGC_COUNT; ++i) {
    if (!(t[i].flags & DCF_ALWAYS)) continue;
    ++always;
    CHECK((t[i].flags & DCF_MUTATES) == 0u);
    CHECK((t[i].flags & DCF_TAINTS)  == 0u);
  }
  // help / info / show_save / stall, and nothing else has been slipped in.
  CHECK_EQ((int)always, 4);
}

// The release build's help must not advertise commands it cannot run, and it
// must say WHY the rest is missing rather than pretending the list is complete.
TEST(release_help_lists_only_the_read_only_surface_and_says_so) {
  DiagOut o;
  diag_out_init(o, g_buf, (uint16_t)sizeof(g_buf));
  diag_help(false, o);
  CHECK(has("help"));
  CHECK(has("info"));
  CHECK(has("show_save"));
  CHECK(has("stall"));
  CHECK(!has("spawn"));
  CHECK(!has("fill_box"));
  CHECK(has("GOD_MODE_ENABLED 0"));

  // MEASURED: the dev build's whole help text is over 1 kB, which is why the
  // device shell prints it one line at a time. The 512-byte stack buffer the
  // first version of the shell used would have clipped it in half on the board
  // and nowhere else.
  static char big[2048];
  DiagOut ob;
  diag_out_init(ob, big, (uint16_t)sizeof(big));
  diag_help(true, ob);
  CHECK(ob.truncated == 0u);
  CHECK(ob.len > 1024);
  CHECK(strstr(big, "spawn") != nullptr);
  CHECK(strstr(big, "give_item") != nullptr);
  CHECK(strstr(big, "scan_wifi") != nullptr);
  CHECK(strstr(big, "GOD_MODE_ENABLED 0") == nullptr);

  // The device shell prints one line at a time with a 96 B buffer, so the two
  // paths have to agree about what a build advertises AND every line has to fit
  // in that buffer. A usage string that outgrew it would be silently clipped on
  // the board and nowhere else.
  for (int di = 0; di < 2; ++di) {
    const bool d = (di != 0);
    uint8_t listed = 0;
    for (uint8_t i = 0; i <= (uint8_t)DGC_COUNT; ++i) {
      char line[96];
      DiagOut l;
      diag_out_init(l, line, (uint16_t)sizeof(line));
      if (!diag_help_line(i, d, l)) continue;
      ++listed;
      CHECK(l.truncated == 0u);
      CHECK(l.len > 2);
    }
    CHECK_EQ((int)listed, d ? (int)DGC_COUNT : 5);   // 4 commands + the note
  }
  // Out of range is a refusal, not a blank line.
  {
    char line[32];
    DiagOut l;
    diag_out_init(l, line, (uint16_t)sizeof(line));
    CHECK(!diag_help_line((uint8_t)(DGC_COUNT + 1u), true, l));
    CHECK_EQ((int)l.len, 0);
  }
}

TEST(the_table_carries_all_twelve_of_spec_66s_commands) {
  static const char* const k66[] = {
    "give_item", "spawn", "set_level", "set_time", "set_activity", "heal",
    "fill_box", "clear_box", "start_battle", "start_creator", "scan_wifi",
    "show_save"
  };
  for (unsigned i = 0; i < sizeof k66 / sizeof k66[0]; ++i) {
    const DiagCmdSpec* s = diag_cmd_find(k66[i]);
    CHECK(s != nullptr);
    CHECK(s->usage != nullptr);
  }
  // And spec section 49's six actions. test_battle is start_battle's twin -
  // it was already a menu row - so the serial name is the section 66 one.
  static const char* const k49[] = {
    "test_encounter", "test_evolution", "start_battle", "test_minigame",
    "test_save_load", "seed"
  };
  for (unsigned i = 0; i < sizeof k49 / sizeof k49[0]; ++i) {
    CHECK(diag_cmd_find(k49[i]) != nullptr);
  }
}

// =============================================================================
//  3. THE PARSER
// =============================================================================
TEST(the_parser_names_every_refusal) {
  DiagParse p;

  diag_parse("", p);              CHECK_EQ((int)p.err, (int)DGP_EMPTY);
  diag_parse("   \t ", p);        CHECK_EQ((int)p.err, (int)DGP_EMPTY);
  diag_parse("nosuch", p);        CHECK_EQ((int)p.err, (int)DGP_UNKNOWN);
  diag_parse("spawn", p);         CHECK_EQ((int)p.err, (int)DGP_TOO_FEW);
  diag_parse("heal 1", p);        CHECK_EQ((int)p.err, (int)DGP_TOO_MANY);
  diag_parse("spawn abc", p);     CHECK_EQ((int)p.err, (int)DGP_BAD_NUMBER);
  diag_parse("spawn 4294967296", p);
  CHECK_EQ((int)p.err, (int)DGP_OVERFLOW);
  // A name longer than any command reads as unknown, not as a buffer accident.
  diag_parse("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa 1", p);
  CHECK_EQ((int)p.err, (int)DGP_UNKNOWN);

  diag_parse("  spawn   12   30  ", p);
  CHECK_EQ((int)p.err, (int)DGP_OK);
  CHECK_EQ((int)p.cmd, (int)DGC_SPAWN);
  CHECK_EQ((int)p.argc, 2);
  CHECK_EQ((int)p.arg[0], 12);
  CHECK_EQ((int)p.arg[1], 30);

  diag_parse("seed 0xDEADBEEF", p);
  CHECK_EQ((int)p.err, (int)DGP_OK);
  CHECK(p.arg[0] == 0xDEADBEEFu);

  diag_parse("seed 0xffffffff", p);
  CHECK_EQ((int)p.err, (int)DGP_OK);
  CHECK(p.arg[0] == 0xFFFFFFFFu);

  // 0x with no digits is not the number 0.
  diag_parse("seed 0x", p);       CHECK_EQ((int)p.err, (int)DGP_BAD_NUMBER);
  // A hex digit in a decimal token is refused, not silently truncated to 1.
  diag_parse("seed 1f", p);       CHECK_EQ((int)p.err, (int)DGP_BAD_NUMBER);
}

TEST(a_bad_parse_runs_nothing_and_commits_nothing) {
  fixture(1);
  const uint8_t before = box_count();
  uint8_t commit = 0xFF;
  CHECK_EQ(run("spawn abc", &commit), (uint8_t)DGR_ERR_PARSE);
  CHECK_EQ((int)commit, 0);
  CHECK_EQ((int)box_count(), (int)before);
  CHECK(has("not a number"));
}

TEST(out_of_range_arguments_are_named_and_change_nothing) {
  fixture(1);
  const uint8_t before = box_count();

  CHECK_EQ(run("spawn 250"), (uint8_t)DGR_ERR_RANGE);
  CHECK(has("no such species"));
  CHECK_EQ((int)box_count(), (int)before);

  CHECK_EQ(run("spawn 1 31"), (uint8_t)DGR_ERR_RANGE);
  CHECK(has("level 1..30"));
  CHECK_EQ((int)box_count(), (int)before);

  CHECK_EQ(run("set_level 0"), (uint8_t)DGR_ERR_RANGE);
  CHECK_EQ((int)box_peek(0)->level, 5);

  CHECK_EQ(run("give_item 0"), (uint8_t)DGR_ERR_RANGE);
  CHECK_EQ(run("give_item 250"), (uint8_t)DGR_ERR_RANGE);
  CHECK_EQ((int)inv_slots_used(g_state.inv), 0);
}

// =============================================================================
//  4. THE COMMANDS THAT ARE NOT ABOUT THE TAINT
// =============================================================================

// box.h invariant B4: release refuses the ACTIVE slot. So clear_box cannot
// empty the Box, and the answer says so instead of looking broken to whoever
// counts the slots afterwards.
TEST(clear_box_leaves_the_active_slot_and_says_why) {
  fixture(1);
  CHECK_EQ(run("fill_box 2"), (uint8_t)DGR_OK);
  CHECK_EQ((int)box_count(), (int)BOX_SLOTS);

  CHECK_EQ(run("clear_box"), (uint8_t)DGR_OK);
  CHECK_EQ((int)box_count(), 1);
  CHECK_EQ((int)box_active(), 0);
  CHECK(has("is active"));
  CHECK(has("freed=9"));
}

TEST(spawn_fills_the_box_and_then_refuses_with_a_named_reason) {
  fixture(1);
  for (uint8_t i = 1; i < (uint8_t)BOX_SLOTS; ++i) {
    CHECK_EQ(run("spawn 1"), (uint8_t)DGR_OK);
  }
  CHECK_EQ((int)box_count(), (int)BOX_SLOTS);
  CHECK_EQ(run("spawn 1"), (uint8_t)DGR_ERR_FULL);
  CHECK(has("box full"));
  CHECK_EQ((int)box_count(), (int)BOX_SLOTS);
}

// spec section 66 asks for a SETTER and game/activity.h has none: the score is
// derived from four capped terms in the CooldownTable. The command records
// interactions and reports what the score actually became.
TEST(set_activity_records_interactions_and_reports_the_derived_score) {
  fixture(1);
  const PebbleInstance honest = clean_stranger();
  uint8_t commit = 0;
  CHECK_EQ(run("set_activity 3", &commit), (uint8_t)DGR_OK);
  CHECK_EQ((int)commit, (int)(DGCOMMIT_CDS | DGCOMMIT_ACTIVE));
  CHECK(has("derived, not set"));
  CHECK(has("notes=3"));
  // The XP those notes will pay lands on the active Pebble through
  // app_pay_activity(), so the creature is marked here rather than being left
  // clean while carrying XP nobody earned.
  CHECK(!taint_gate_ok(honest, *box_peek(0)));

  CHECK_EQ(run("set_activity 256"), (uint8_t)DGR_ERR_RANGE);
}

TEST(seed_reseeds_every_stream_so_a_bench_run_reproduces) {
  fixture(1);
  CHECK_EQ(run("seed 0x1234"), (uint8_t)DGR_OK);
  const uint32_t a0 = rng_u32(RNG_ENCOUNTER);
  const uint32_t a1 = rng_u32(RNG_BATTLE);
  CHECK_EQ(run("seed 0x1234"), (uint8_t)DGR_OK);
  CHECK(rng_u32(RNG_ENCOUNTER) == a0);
  CHECK(rng_u32(RNG_BATTLE) == a1);

  CHECK_EQ(run("seed 0x9999"), (uint8_t)DGR_OK);
  CHECK(rng_u32(RNG_ENCOUNTER) != a0);
}

TEST(test_encounter_rolls_and_prints_without_filing_anything) {
  fixture(1);
  const uint8_t before = box_count();
  uint8_t commit = 0xFF;
  CHECK_EQ(run("test_encounter 0x1111", &commit), (uint8_t)DGR_OK);
  CHECK_EQ((int)commit, 0);                    // read-only: nothing to persist
  CHECK_EQ((int)box_count(), (int)before);
  CHECK(has("test_encounter out="));
  CHECK(has("bucket="));

  // The same net_hash in the same bucket gives the same creature - the promise
  // game/encounters.h makes at the top of its own header.
  char first[sizeof g_buf];
  memcpy(first, g_buf, sizeof first);
  CHECK_EQ(run("test_encounter 0x1111"), (uint8_t)DGR_OK);
  CHECK(strcmp(first, g_buf) == 0);
}

TEST(the_deferred_commands_run_nothing_in_the_pure_layer) {
  fixture(1);
  static const char* const kDefer[] = {
    "set_time 1800000000", "start_battle", "start_creator", "scan_wifi",
    "test_minigame 1", "test_save_load", "stall 10",
    // The three read-only ones are deferred too: only the shell knows which
    // build it is, and only the shell can read a heap figure or a BoxHeader.
    "help", "info", "show_save"
  };
  for (unsigned i = 0; i < sizeof kDefer / sizeof kDefer[0]; ++i) {
    uint8_t commit = 0xFF;
    const uint8_t before = box_count();
    CHECK_EQ(run(kDefer[i], &commit), (uint8_t)DGR_DEFER);
    CHECK_EQ((int)commit, 0);
    CHECK_EQ((int)box_count(), (int)before);
  }
}

TEST(a_command_that_needs_a_pet_refuses_when_the_box_is_empty) {
  fixture(0);
  DiagCtx c = ctx();
  c.have_sim = 0u;
  DiagParse p;
  diag_parse("set_level 9", p);
  DiagOut o;
  diag_out_init(o, g_buf, (uint16_t)sizeof(g_buf));
  uint8_t commit = 0xFF;
  CHECK_EQ(diag_exec(p, c, o, commit), (uint8_t)DGR_ERR_NO_PET);
  CHECK_EQ((int)commit, 0);

  diag_parse("heal", p);
  diag_out_init(o, g_buf, (uint16_t)sizeof(g_buf));
  CHECK_EQ(diag_exec(p, c, o, commit), (uint8_t)DGR_ERR_NO_PET);
}

// =============================================================================
//  5. THE SPEC 49 FIELD LIST
// =============================================================================
static DiagFields sample_fields(void) {
  DiagFields f;
  diag_fields_clear(f);
  f.free_heap = 123456; f.min_heap = 100000; f.max_alloc = 65536;
  f.now_epoch = DG_EPOCH0; f.last_seen = DG_EPOCH0 - 60;
  f.cal_state = (uint8_t)CAL_PHONE; f.clock_valid = 1; f.skew_s = -42;
  f.net_mode = 2; f.net_phase = 3; f.net_err = 0; f.ap_up = 1;
  f.save_schema = 2; f.save_migrated = 1; f.quarantine_mask = 0x0005u;
  f.load_result = 3; f.content_version = 0x5B4Au;
  f.pebbles = 4;
  f.scan_valid = 1; f.scan_seen = 7; f.scan_fresh = 2; f.scan_phase = 1;
  f.kv_healthy = 1; f.kv_error = 0x11u; f.kv_write_fails = 0;
  f.uptime_s = 3600;
  return f;
}

TEST(all_twelve_spec_49_fields_format_and_none_is_empty) {
  const DiagFields f = sample_fields();
  CHECK_EQ((int)DGD_FIELD_COUNT, 12);
  for (uint8_t i = 0; i < (uint8_t)DGD_FIELD_COUNT; ++i) {
    DiagOut o;
    diag_out_init(o, g_buf, (uint16_t)sizeof(g_buf));
    diag_fmt_field(i, f, o);
    CHECK(o.truncated == 0u);
    // "Name: " plus something. A field that formatted to its own label and
    // nothing else would be a blank line with a title.
    const size_t name_len = strlen(diag_field_name(i));
    CHECK(o.len > (uint16_t)(name_len + 2));
    CHECK(strncmp(g_buf, diag_field_name(i), name_len) == 0);
    CHECK(!has("?"));
  }
}

// The two fields whose honest answer is "there is nothing here". Printing a
// fabricated battery percentage would be the single most misleading thing this
// console could do, so the absence is asserted rather than assumed.
TEST(battery_and_ble_report_absence_with_a_reason) {
  const DiagFields f = sample_fields();
  DiagOut o;

  diag_out_init(o, g_buf, (uint16_t)sizeof(g_buf));
  diag_fmt_field((uint8_t)DGD_BATTERY, f, o);
  CHECK(has("n/a"));
  CHECK(has("PB_PINS_CONFIRMED"));

  diag_out_init(o, g_buf, (uint16_t)sizeof(g_buf));
  diag_fmt_field((uint8_t)DGD_BLE, f, o);
  CHECK(has("none"));
  CHECK(has("P8-C0"));
}

TEST(save_version_reports_the_firmwares_and_the_flashs_and_the_migration) {
  DiagFields f = sample_fields();
  f.save_schema = 1;                 // a v1 save that was migrated on this boot
  DiagOut o;
  diag_out_init(o, g_buf, (uint16_t)sizeof(g_buf));
  diag_fmt_field((uint8_t)DGD_SAVE_VER, f, o);
  // "fw=2" WAS A LITERAL HERE UNTIL P10-C5 BUMPED THE SCHEMA, and it failed by
  // name, which is what it was for. It is derived now because this is a
  // FORMATTER test - the three questions the line answers and the shape of the
  // answer - while the value of SAVE_SCHEMA_VERSION itself is pinned where it
  // belongs, in tests/test_persistence.cpp's schema_sizes_are_the_wire_sizes.
  // The two numbers below stay literal: they come from the fields, not the
  // build, and a formatter that printed the firmware's version for all three
  // would still pass a test that derived all three.
  char want[16];
  snprintf(want, sizeof want, "fw=%u", (unsigned)SAVE_SCHEMA_VERSION);
  CHECK(has(want));
  CHECK(has("onflash=1"));
  CHECK(has("migrated=1"));
}

TEST(last_error_joins_the_four_sources_that_were_never_joined) {
  const DiagFields f = sample_fields();
  DiagOut o;
  diag_out_init(o, g_buf, (uint16_t)sizeof(g_buf));
  diag_fmt_field((uint8_t)DGD_LAST_ERR, f, o);
  CHECK(has("load=3"));              // the boot LoadResult app.cpp used to drop
  CHECK(has("kverr=0x11"));
  CHECK(has("quar=0x0005"));
  CHECK(has("radio="));
}

TEST(last_scan_says_none_this_boot_rather_than_zero_networks) {
  DiagFields f = sample_fields();
  f.scan_valid = 0; f.scan_seen = 0; f.scan_fresh = 0;
  DiagOut o;
  diag_out_init(o, g_buf, (uint16_t)sizeof(g_buf));
  diag_fmt_field((uint8_t)DGD_LAST_SCAN, f, o);
  CHECK(has("none this boot"));
  CHECK(!has("0 seen"));
}

TEST(the_field_list_prints_twelve_lines_in_the_specs_order) {
  const DiagFields f = sample_fields();
  DiagOut o;
  diag_out_init(o, g_buf, (uint16_t)sizeof(g_buf));
  diag_fmt_all(f, o);
  CHECK(o.truncated == 0u);
  uint8_t lines = 0;
  for (uint16_t i = 0; i < o.len; ++i) if (g_buf[i] == '\n') ++lines;
  CHECK_EQ((int)lines, 12);
  // The order is the spec's, so a reader can diff two boards line by line.
  const char* fw = strstr(g_buf, "Firmware:");
  const char* pv = strstr(g_buf, "Protocol version:");
  CHECK(fw != nullptr && pv != nullptr && fw < pv);
}

// =============================================================================
//  6. THE SINK - it is what bounds every string above
// =============================================================================
TEST(the_sink_truncates_at_the_cap_and_reports_it) {
  char small[8];
  DiagOut o;
  diag_out_init(o, small, (uint16_t)sizeof(small));
  diag_puts(o, "abcdefghijklmnop");
  CHECK_EQ((int)o.len, 7);
  CHECK(o.truncated == 1u);
  CHECK(small[7] == '\0');
  CHECK(strcmp(small, "abcdefg") == 0);

  // A zero-cap sink must not write anywhere at all.
  DiagOut z;
  diag_out_init(z, nullptr, 0);
  diag_puts(z, "x");
  diag_putu(z, 12345);
  CHECK(z.truncated == 1u);
  CHECK_EQ((int)z.len, 0);
}

TEST(the_sink_formats_numbers_the_way_a_log_reader_expects) {
  DiagOut o;
  diag_out_init(o, g_buf, (uint16_t)sizeof(g_buf));
  diag_putu(o, 0);        diag_putc(o, ' ');
  diag_putu(o, 4294967295u); diag_putc(o, ' ');
  diag_puti(o, -2147483647 - 1); diag_putc(o, ' ');
  diag_puthex(o, 0xABCDu, 4);
  CHECK(strcmp(g_buf, "0 4294967295 -2147483648 ABCD") == 0);
}
