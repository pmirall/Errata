// =============================================================================
//  ERRATA - dev/diag_core.h
//  THE ARITHMETIC HALF OF THE DIAGNOSTICS SURFACE (plan P10-C1, spec 49 + 66).
//
//  WHY THIS FILE EXISTS AT ALL, said first because it is the only reason it is
//  a separate translation unit:
//
//    dev/godmode.cpp includes <Arduino.h> at line 15 and ui/render.h below it,
//    so NO HOST BINARY IN THIS REPOSITORY COMPILES IT. `grep godmode tests/`
//    returns nothing and tests/Makefile has no dev/ rule. Every line put in
//    that file is untested BY CONSTRUCTION - including its GOD_MODE_ENABLED 0
//    stubs, which is worse, because a stub that returned garbage would still
//    print ALL PASS and MATRIX OK.
//
//    That is the phase-9 defect exactly: pf_build_lids() sat inside
//    ui/petfx.cpp behind a banner claiming it was testable, no host binary
//    compiled it, and the blink it composites drew over four species' bodies
//    while every byte check in the tree passed. The repair was
//    ui/petfx_core.{h,cpp}. The P6 precedent is pwr_tick_budget() moved into
//    hardware/power.cpp. This is the same move for the console: the parsing,
//    the range checks, the taint decision, the field formatting and the
//    execution of every command that only touches PURE modules live HERE, and
//    tests/test_diag.cpp drives all of it.
//
//    What is left in dev/godmode.cpp is a SHELL: read a serial line, hand it to
//    diag_parse(), hand the parse to diag_exec(), print the sink, and do the
//    four things that genuinely need a device (Serial, the radio, the screen
//    stack, flash). The phase log says so; the shell itself is still covered by
//    no host test and pretending otherwise is what this banner refuses to do.
//
// -----------------------------------------------------------------------------
//  THE TAINT RULE IS PART OF THE INTERFACE, NOT A DETAIL OF ONE COMMAND
// -----------------------------------------------------------------------------
//  game/taint.h holds the rule ("a clean unit refuses a tainted one") and reads
//  TWO markers: the genome bit GN_TAINT_SH and BugInstance.flags's
//  PBF_GOD_TAINTED. Everything that reaches the live pet through a sim_god_*()
//  hook self-taints, because game/sim.cpp sets PF_GOD_TAINTED and status_sync()
//  mirrors it onto the instance. NOTHING THAT REACHES THE BOX DOES:
//  box_new_bug() memsets the record and takes the genome as given, so a
//  `spawn` written the obvious way MINTS A CLEAN BUG INSIDE GOD MODE, which
//  then passes taint_gate_ok() and can be traded into an honest player's
//  dynasty. That is the hole the gate exists to close, and it is one line of
//  omission away at all times.
//
//  So the table below carries DCF_TAINTS as data, diag_exec() applies
//  diag_taint() from that flag rather than from a hand-written call per arm,
//  and tests/test_diag.cpp walks the table: every command marked DCF_TAINTS is
//  executed on a clean state and the result must be REFUSED BY taint_gate_ok().
//  A command added without the flag fails there by name.
//
//  give_item is deliberately NOT marked, and the deliberateness is the point:
//  Inventory is device-wide, carries no genome and no flags, and items do not
//  cross a trade. There is nothing to taint. Minting a potion is a cheat; it is
//  not a DYNASTY cheat, and the gate has no opinion about consumables. Said out
//  loud here rather than left as a silent gap in the table.
//
// -----------------------------------------------------------------------------
//  THE RELEASE SPLIT (spec 66's closing line, spec 67 "Diagnostics available")
// -----------------------------------------------------------------------------
//  GOD_MODE_ENABLED is 0 in the shipping artefact. Before this phase that meant
//  the release build had NO screen, NO command and NO serial input at all: one
//  `DIAG,heap,` line a minute and six boot lines were the whole of the
//  product's diagnostics. §66 says "do not ship DANGEROUS debug commands", and
//  it distinguishes a dangerous command from reading a version number.
//
//  So the table splits on DCF_ALWAYS, and the invariant is mechanical:
//
//      DCF_ALWAYS  =>  NOT DCF_MUTATES
//
//  static_asserted at the bottom of diag_core.cpp AND walked by
//  tests/test_diag.cpp. Marking `spawn` DCF_ALWAYS fails both by name. The
//  always-on set is help / info / show_save / stall: three read-only and one
//  bounded busy-wait that P10-C2's latency measurement needs on a board a
//  release image was flashed to.
//
// -----------------------------------------------------------------------------
//  PURITY. stdint, stddef, stdio (snprintf, as hardware/gametime.cpp uses it)
//  and the game/persistence headers. NO Arduino.h, NO render.h, NO Serial.
//  tools/check.sh holds that as a red line, the same one ui/petfx_core.cpp has.
//  NO file-scope mutable state: every call takes what it needs.
// =============================================================================
#ifndef ER_DIAG_CORE_H
#define ER_DIAG_CORE_H

#include <stdint.h>
#include <stddef.h>

#include "../persistence/save_schema.h"   // GameState, BugInstance, BoxHeader

// =============================================================================
//  1. THE OUTPUT SINK
//     A caller-owned char buffer with a length and a truncation flag. The pure
//     layer never prints; the shell prints what this collected. `truncated` is
//     a REPORTED fact rather than a silent clip, so a test can assert the bound
//     and an operator can see that a line was cut.
// =============================================================================
struct DiagOut {
  char*    buf;
  uint16_t cap;        // including the NUL
  uint16_t len;        // bytes used, excluding the NUL
  uint8_t  truncated;  // 1 once anything did not fit
};

void diag_out_init(DiagOut& o, char* buf, uint16_t cap);
void diag_puts(DiagOut& o, const char* s);
void diag_putc(DiagOut& o, char c);
void diag_putu(DiagOut& o, uint32_t v);
void diag_puti(DiagOut& o, int32_t v);
void diag_puthex(DiagOut& o, uint32_t v, uint8_t digits);
void diag_nl(DiagOut& o);

// =============================================================================
//  2. THE COMMANDS (spec 66's twelve, plus spec 49's six actions and the three
//     read-only ones the release artefact needs)
// =============================================================================
enum DiagCmdId : uint8_t {
  // --- the always-compiled read-only surface -------------------------------
  DGC_HELP = 0,
  DGC_INFO,             // the spec 49 field list
  DGC_SHOW_SAVE,        // spec 66
  DGC_STALL,            // the P10-C2 latency instrument, bounded
  // --- spec 66, the mutating twelve ----------------------------------------
  DGC_GIVE_ITEM,
  DGC_SPAWN,
  DGC_SET_LEVEL,
  DGC_SET_TIME,
  DGC_SET_ACTIVITY,
  DGC_HEAL,
  DGC_FILL_BOX,
  DGC_CLEAR_BOX,
  DGC_START_BATTLE,
  DGC_START_CREATOR,
  DGC_SCAN_WIFI,
  // --- spec 49's actions ---------------------------------------------------
  DGC_TEST_ENCOUNTER,
  DGC_TEST_EVOLUTION,
  DGC_TEST_MINIGAME,
  DGC_TEST_SAVE_LOAD,
  DGC_SEED,             // deterministic RNG seed
  DGC_COUNT,
  DGC_UNKNOWN = 0xFFu
};

// Command flags. See the banner: DCF_ALWAYS and DCF_MUTATES are exclusive by
// construction and DCF_TAINTS is what makes the cheat honest.
#define DCF_ALWAYS    0x01u   // compiled into the GOD_MODE_ENABLED 0 artefact
#define DCF_MUTATES   0x02u   // changes game state, the clock or the radio
#define DCF_TAINTS    0x04u   // ...a BugInstance, so it must mark it (taint.h)
#define DCF_DEFERS    0x08u   // the pure layer cannot run it; the shell must

struct DiagCmdSpec {
  const char* name;
  uint8_t     id;
  uint8_t     argc_min;
  uint8_t     argc_max;
  uint8_t     flags;
  const char* usage;      // ASCII, operator-facing scaffolding, never a UI string
};

// DGC_COUNT entries, index == id (static_asserted in the .cpp).
const DiagCmdSpec* diag_cmd_table(void);
const DiagCmdSpec* diag_cmd_spec(uint8_t id);
const DiagCmdSpec* diag_cmd_find(const char* name);   // exact, case-sensitive

// =============================================================================
//  3. THE PARSER
//     One line -> a command id and up to DIAG_ARG_MAX unsigned arguments.
//     Decimal, or 0x-prefixed hex. Every refusal is NAMED: a console that
//     answers "?" to a typo and to an out-of-range number teaches nothing.
// =============================================================================
enum DiagParseErr : uint8_t {
  DGP_OK = 0,
  DGP_EMPTY,        // blank line: not an error, nothing to run
  DGP_UNKNOWN,      // no such command
  DGP_TOO_FEW,      // fewer arguments than argc_min
  DGP_TOO_MANY,     // more than argc_max
  DGP_BAD_NUMBER,   // a token that is not a number
  DGP_OVERFLOW,     // a number wider than uint32_t
  DGP_ERR_COUNT
};

#define DIAG_ARG_MAX  2

struct DiagParse {
  uint8_t  cmd;                 // DiagCmdId, DGC_UNKNOWN on failure
  uint8_t  err;                 // DiagParseErr
  uint8_t  argc;
  uint32_t arg[DIAG_ARG_MAX];
};

void diag_parse(const char* line, DiagParse& out);
const char* diag_parse_err_str(uint8_t err);   // ASCII

// =============================================================================
//  4. THE TAINT RULE
// =============================================================================
// Sets BOTH markers game/taint.h reads. gene_set_tainted() reseals the genome,
// so the CRC agrees; BugInstance.crc16 is sealed by persistence on the way
// to flash, exactly as every other mutation in the tree relies on.
void diag_taint(BugInstance& p);

// From the table, so the test can walk it and a new command cannot slip past.
uint8_t diag_cmd_taints(uint8_t id);

// =============================================================================
//  5. EXECUTION
// =============================================================================
enum DiagResult : uint8_t {
  DGR_OK = 0,
  DGR_DEFER,          // DCF_DEFERS: the shell owns it (radio, screens, flash)
  DGR_ERR_PARSE,      // p.err said so; nothing ran
  DGR_ERR_NOT_HERE,   // compiled out, or wrong build
  DGR_ERR_NO_PET,     // there is no active Bug
  DGR_ERR_RANGE,      // an argument named something that does not exist
  DGR_ERR_FULL,       // the Box or the bag is full
  DGR_ERR_REFUSED,    // the owning module said no (box_release on the active slot)
  DGR_RESULT_COUNT
};

// What the shell must persist after a successful call. The pure layer writes no
// flash - persistence decides when a mutated blob lands (game/box.h's rule) -
// so it says what it touched and the shell commits.
#define DGCOMMIT_BOX      0x01u   // BoxHeader (slot mask, active slot, counters)
#define DGCOMMIT_ACTIVE   0x02u   // the active BugInstance
#define DGCOMMIT_SLOTS    0x04u   // every occupied slot
#define DGCOMMIT_INV      0x08u   // Inventory
#define DGCOMMIT_CDS      0x10u   // CooldownTable (the activity score lives there)

// Everything diag_exec() needs from the outside, in one struct - the shape
// game/activity.h's ActClock and game/cooldowns.h's CdClock established.
struct DiagCtx {
  GameState* gs;            // the live state; nullptr makes every arm a no-op
  uint32_t   now_epoch;     // gt_now()
  uint8_t    cal;           // TimeCal, gt_cal_state()
  uint8_t    have_sim;      // 1 when sim_bind() has run on the active Bug
};

// Runs `p` and appends its human-readable answer to `out`. Returns a
// DiagResult; `commit` is filled with the DGCOMMIT_* bits the caller must
// persist (0 on every failure and on every read-only command).
uint8_t diag_exec(const DiagParse& p, const DiagCtx& ctx, DiagOut& out,
                  uint8_t& commit);

// ONE help line. Returns false when command `i` is not listed in a build with
// this `dev` flag (or when i is out of range), leaving `out` untouched. The
// device shell walks this with a small stack buffer rather than collecting the
// whole text: twenty usage lines do not fit in a buffer the loop task can
// afford, and a truncated help is a help that lies about what the build can do.
// `i == DGC_COUNT` is the trailing note the release build prints; every other
// index is a command.
bool diag_help_line(uint8_t i, bool dev, DiagOut& out);

// The whole help text in one sink, for a caller that has room for it (the host
// test does). Exactly diag_help_line() over every index, so the two cannot
// disagree about what a build advertises.
void diag_help(bool dev, DiagOut& out);

// =============================================================================
//  6. THE SPEC 49 FIELD LIST
//     Twelve fields, in the spec's own order. The shell fills DiagFields from
//     device accessors (ESP.getFreeHeap(), net_*(), gt_*(), kv_*()); the pure
//     layer decides what each field SAYS. That split is what makes "Battery:
//     n/a" and "BLE: none" testable claims rather than string literals nobody
//     reads.
// =============================================================================
enum DiagFieldId : uint8_t {
  DGD_FIRMWARE = 0,
  DGD_BUILD,
  DGD_FREE_RAM,
  DGD_BATTERY,
  DGD_WIFI,
  DGD_BLE,
  DGD_CLOCK,
  DGD_SAVE_VER,
  DGD_BUGS,
  DGD_LAST_SCAN,
  DGD_LAST_ERR,
  DGD_PROTOCOL,
  DGD_FIELD_COUNT
};

struct DiagFields {
  // heap
  uint32_t free_heap, min_heap, max_alloc;
  // clock
  uint32_t now_epoch, last_seen;
  uint8_t  cal_state, clock_valid;
  int32_t  skew_s;
  // radio
  uint8_t  net_mode, net_phase, net_err, ap_up;
  // save
  uint8_t  save_schema;       // the LOADED BoxHeader.schema_version, not the macro
  uint8_t  save_migrated;
  uint16_t quarantine_mask;
  uint8_t  load_result;       // LoadResult from boot, carried instead of dropped
  uint16_t content_version;
  // box
  uint8_t  bugs, box_cap;
  // the last scan (ui/screen_network.h)
  uint8_t  scan_valid, scan_seen, scan_fresh, scan_phase;
  // flash
  uint8_t  kv_healthy, kv_error;
  // uint16_t SINCE THE FINAL REVIEW, matching hardware/kv_nvs.h's own type.
  // It was uint8_t here and uint16_t at the source, so `info`'s section-49 Last
  // error line printed the count MODULO 256 - and it is the only numeric
  // evidence the product offers about flash misbehaving. On a board with no
  // usable nvs2 every save_checkpoint_all() raises it by exactly 12, so after a
  // boot and 30 checkpoints the true count is 361 and the line read "wfails=105";
  // every further 256 failures the reading passes through small numbers again,
  // so a board with hundreds of failed writes could print a value
  // indistinguishable from a healthy one. God mode's STORE page prints the full
  // uint16 from the same counter, so the two disagreed and the operator had no
  // way to know which to believe. Two neighbours in this struct are already
  // uint16_t, so there is no new alignment cost.
  uint16_t kv_write_fails;
  // uptime, for the operator reading a log
  uint32_t uptime_s;
};

void diag_fields_clear(DiagFields& f);

// One "Name: value" line, no newline. Total over 0..DGD_FIELD_COUNT-1.
void diag_fmt_field(uint8_t id, const DiagFields& f, DiagOut& o);

// All twelve, one per line, each terminated. This is what `info` prints.
void diag_fmt_all(const DiagFields& f, DiagOut& o);

// The field's spec-49 name, for a caller that wants to lay them out itself
// (the console's own SYS/INFO page does). Never nullptr.
const char* diag_field_name(uint8_t id);

// -----------------------------------------------------------------------------
//  SCREEN NAMES (P10-C6)
//
//  The performance receipt prints the screen that owned the worst frame and the
//  worst loop pass AS A RAW ScreenId INTEGER, and nothing on the device or in
//  docs/bench.md turned that back into a screen. docs/bench.md A1's whole
//  method is "walk the screens, then read which one owned the worst frame" -
//  and the operator got `s17`. Worse, the enum is not stable across phases:
//  P10-C4 inserted SCR_SETUP_NAME and SCR_SETUP_STARTER IN THE MIDDLE, so any
//  mapping written down from an older log is wrong, and a capture committed to
//  docs/bench/ was not interpretable a phase later.
//
//  Here rather than in dev/godmode.cpp because this file is pure, host-linked
//  and driven by tests/test_diag.cpp - the table is index-parallel to ScreenId
//  with a static_assert on its length, so a screen added without a name fails
//  the BUILD, the same shape kAudit[] uses for the section 63 audit.
const char* diag_screen_name(uint8_t scr);

#endif  // ER_DIAG_CORE_H
