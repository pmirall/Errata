// =============================================================================
//  ERRATA - dev/godmode.h
//  The debug console. The GOD screen plus the persistent state
//  that makes the rest of the firmware testable in minutes instead of a week:
//  the time-scale multiplier, forced absences, a forced stage,
//  a genome editor with hex dump and paste-in, a live heap panel and a CSV
//  soak log over Serial.
//
//  COMPILED ALWAYS. Entry is undocumented in the product:
//  SCR_STATUS_B + both buttons held GOD_ENTER_HOLD_MS. With GOD_MODE_ENABLED
//  set to 0 every entry point below still links and god_active() is permanently
//  false.
//
//  WHAT THE SHIPPING ARTEFACT ACTUALLY HAS (P10-C1, spec section 67
//  "Diagnostics available"). tools/build_matrix.sh's `release` variant is
//  GOD_MODE_ENABLED=0, and until this phase that meant SCR_DIAG was
//  unreachable (god_entry_progress() returns 0, always, and ui.cpp:1658 is the
//  only navigation to it in the tree), no serial reader ran at all, and the
//  whole of the product's diagnostics was ONE `DIAG,heap,` line a minute plus
//  six boot lines. The comment on the bench console even claimed a stall was
//  "reachable from a plain serial terminal on a freshly flashed board", which
//  was true of the dev build and false of the artefact - the phase-7 shape,
//  where a sweep runs against a build the release does not execute.
//
//  So the module now splits, and dev/diag_core.h's DCF_ALWAYS flag is where the
//  split is written down. ALWAYS COMPILED, release included:
//      the heap trend line, `help`, `info` (the spec 49 field list),
//      `show_save` and `stall <ms>` - three reads and one bounded busy-wait.
//  GOD_MODE_ENABLED ONLY: the screen, every gesture, and every command that
//  MUTATES anything (spec section 66's twelve and spec section 49's actions).
//  The invariant DCF_ALWAYS => NOT DCF_MUTATES is static_asserted in
//  dev/diag_core.cpp and walked by tests/test_diag.cpp, so "dangerous debug
//  commands do not ship in the normal release UI" is a checked property rather
//  than a promise in a comment.
//
//  HONESTY RULES this module enforces, not just implements:
//   - The system clock is NEVER touched. Time travel is gt_skew_add() only.
//   - Acceleration is NEVER a stat hack: it goes through sim_set_time_scale(),
//     so every simulated hour runs the real decay, poop and sickness paths at
//     the real cadence.
//   - Entering sets genome.god_tainted permanently, on the living pet and on
//     everything it will ever produce. There is no way back.
//   - While it is active the top GOD_BAR_H rows of EVERY screen are inverted
//     and read "GOD xN". It is impossible to leave it on by accident.
//
//  LAYERING: no WiFi.h / BLEDevice.h / WebServer.h here. The heap panel reads
//  net.h's accessors, which pull in no network header. Only render.h brings in
//  U8g2, which this module needs because it draws.
//
//  Identifiers and comments: English. Every user-facing string: strings_es.h.
// =============================================================================
#ifndef NT_GODMODE_H
#define NT_GODMODE_H

#include <stdint.h>

#include "../core/config.h"
#include "../core/nt_types.h"

// -----------------------------------------------------------------------------
// 1. TUNABLES THAT ARE NOT (YET) IN config.h
//    Every one is #ifndef-guarded so it can be lifted into config.h verbatim
//    without touching this file (the pattern ble_social.h set, P8-C0). config.h already
//    owns GOD_MODE_ENABLED, GOD_ENTER_HOLD_MS, GOD_SCALE_0..4,
//    GOD_SCALE_COUNT, GOD_ABSENCE_COUNT, GOD_CMD_COUNT and GOD_BAR_H.
// -----------------------------------------------------------------------------

// Soak-log cadence. One CSV line per this many REAL milliseconds, on top of the
// line emitted after every state change. At GOD_SCALE_4 (x3600) this is one
// sample per ~2.8 simulated hours, which is the right resolution for a decay
// curve. These are wall-clock diagnostics, not game logic: reading millis()
// here does not breach the layering rule, which governs game time.
#ifndef GOD_DUMP_PERIOD_MS
#define GOD_DUMP_PERIOD_MS     10000UL
#endif

// Heap-trend cadence (plan P2-C12). One "DIAG,heap," line per this many REAL
// milliseconds, printed on EVERY screen whether or not god mode is on, and in
// the GOD_MODE_ENABLED == 0 build too - it is the phase-exit baseline for the
// bench soak ("flat line on HOME with radio OFF"), so it must survive the
// release configuration and must not require entering the console.
#ifndef GOD_HEAP_PERIOD_MS
#define GOD_HEAP_PERIOD_MS     60000UL
#endif

// How long an on-screen confirmation line stays up.
#ifndef GOD_TOAST_MS
#define GOD_TOAST_MS           1600UL
#endif

// THE ONE SERIAL LINE BUFFER (P10-C1). It backs the whole command layer AND the
// "GENOMA / CARGAR" genome paste, which is why it must clear 32 hex chars plus
// slack for spaces and a CR/LF. It replaced GOD_HEX_LINE_MAX (48) and a second
// 24-byte buffer for the bench console; those were two Serial.read() loops that
// each had to be written not to fight the other, and both sat below the
// GOD_MODE_ENABLED guard, so a release board never read a byte.
//
// An overlong line RESTARTS rather than truncating: a clipped
// "give_item 3<junk>spawn 1" must not become a spawn.
#ifndef GOD_LINE_MAX
#define GOD_LINE_MAX           56
#endif

// -----------------------------------------------------------------------------
// 2. SHAPE CONSTANTS
// -----------------------------------------------------------------------------

// Editable genes in "GENOMA / EDITAR", index-parallel to strings_es.h 34c.
#define GOD_GENE_COUNT         13

// "FIJAR STAT" offers 0 / 25 / 50 / 100.
#define GOD_STATVAL_COUNT      4

// The root list is the EIGHT surviving god commands plus an explicit exit row.
// (It said "seven" from P2-C7b until P10-C1; P4-C4's test_battle made it eight
// and GOD_CMD_COUNT has been 8 in config.h since. The static_assert at the
// bottom of this header is what actually holds the number - this line is prose
// and prose drifts, which is exactly what it did.) HOLD_R leaves the SCREEN (god mode stays on, and stays visible); the
// exit row leaves the MODE.
#define GOD_MENU_ROWS          (GOD_CMD_COUNT + 1)

// -----------------------------------------------------------------------------
// 3. WHAT god_handle() WANTS THE CALLER TO DO NEXT
//    god mode owns its own screen; these are the only things it cannot do for
//    itself because they belong to the screen state machine in ui.cpp.
// -----------------------------------------------------------------------------
enum GodEvt : uint8_t {
  GOD_EVT_NONE = 0,   // handled internally, stay on SCR_DIAG
  GOD_EVT_LEAVE,      // leave SCR_DIAG for SCR_HOME. god_active() may still be 1.
  GOD_EVT_WIPED,      // NVS erased and a fresh gen-0 egg installed: reload cfg
                      // and go to SCR_EVOLUTION
  GOD_EVT_BATTLE,     // test_battle (spec section 49): PUSH SCR_BATTLE on top of
                      // the console, which the caller must arm with the fixed
                      // BT_DIAG_SEED. The console stays underneath, so B walks
                      // back into it.
  // P10-C1. Spec section 66 asks for start_creator, scan_wifi and (through
  // section 49's action list) test_minigame. All three are NAVIGATION, which is
  // the one thing this module has never been able to do for itself, so they
  // join test_battle rather than growing a second mechanism. They arrive from a
  // TYPED LINE rather than a gesture, so god_handle() cannot return them - see
  // god_take_evt() below.
  GOD_EVT_CREATOR,    // start_creator: push SCR_CREATOR
  GOD_EVT_SCAN,       // scan_wifi: push SCR_NETWORK, which owns the scan job
  GOD_EVT_MINIGAME,   // test_minigame <n>: ui_start_minigame(arg)
  GOD_EVT_COUNT
};

// =============================================================================
//  4. MANDATORY PUBLIC INTERFACE
// =============================================================================

// True while god mode is ON. This SURVIVES leaving SCR_DIAG: the whole point of
// the time scale is watching an accelerated life on the normal screens. Whoever
// draws a frame must therefore call god_draw_marker() whenever this is true.
bool     god_active(void);

// Feed one gesture while SCR_DIAG is the current screen. Returns what the caller
// must do next. Never call this when god_active() is false.
GodEvt   god_handle(Gesture g);

// Draw the console. Call between rd_begin_frame() and rd_end_frame(), instead
// of the normal screen. Draws its own affordance strip and its own marker bar.
void     god_draw(void);

// Simulated seconds per logic tick: 1, 6, 60, 360 or 3600. Always 1 when god
// mode is off. This is a MIRROR of the sim time scale; the authority is sim,
// because sim_step_seconds() is what the tick actually reads.
uint32_t god_time_scale(void);

// Emit one CSV line of the full pet state on Serial, right now. The first
// eleven columns are exactly the soak-log format
//     GOD,<epoch>,<stage>,<hun>,<hap>,<ene>,<hyg>,<hea>,<cq>,<gen>,<genome>
// and everything after them is appended, so a parser that indexes the first
// eleven columns keeps working. A "GOD#," header line naming every column is
// printed on entry to god mode.
void     god_dump_line(void);

// =============================================================================
//  5. ADDITIVE EXTENSIONS
//     Nothing above changes. Without these the module cannot be entered, cannot
//     be pumped, and cannot keep its promise of being unmistakable.
// =============================================================================

// Reset every console variable and force the time scale back to x1, and arm the
// heap trend so the first god_service() emits the t=0 sample. Call once from
// setup(), AFTER kv_begin() and sim_init(). Never enters god mode.
void     god_begin(void);

// Pump. Call once per loop(), on every screen, whether or not god mode is on:
// it drives the soak log, the Serial genome paste and the GOD_HEAP_PERIOD_MS
// heap-trend line. Cheap and non-blocking.
void     god_service(void);

// The undocumented entry gesture. Call every loop() with the id of the screen
// currently on display. Returns 0..100 = how much of GOD_ENTER_HOLD_MS both
// buttons have been held for on SCR_STATUS_B; the caller draws that as the
// "..." fill bar (STR_UI_GOD_HOLD). At 100 god mode has ALREADY been entered
// and input_flush() has already been called, so the caller only has to switch
// to SCR_DIAG. Returns 0 on every other screen.
//
// The SAME hold re-opens the console when god mode is already on, which is the
// way back after GOD_EVT_LEAVE: the only off switch is the console's exit row,
// so a user watching an accelerated life on SCR_HOME must be able to return.
//
// NOTE for the caller: while this returns non-zero, SWALLOW GST_LONG_BOTH.
// Both buttons held past LONG_BOTH_MS (1500 ms) would otherwise send the user
// HOME at 30 % of the way through the 5 s hold and the gesture would be
// unreachable.
uint8_t  god_entry_progress(uint8_t screen_id);

// Enter / leave god mode explicitly. god_enter() taints the genome forever,
// marks the RTC and prints the CSV header. god_exit() puts the time scale back
// to x1 and forces a save.
void     god_enter(void);
void     god_exit(void);

// The unmistakable marker: the top GOD_BAR_H rows inverted, reading "GOD xN"
// plus the live free heap. Draws nothing when god mode is off. EVERY screen
// must call this last, after its own status bar, so the state can never hide.
void     god_draw_marker(void);

// Above x1 the sleep window is frozen at the local hour
// captured when the scale was raised, otherwise the pet flickers in and out of
// its sleep window many times per second. Returns true and fills hour/minute
// when the caller must override SimEnv.local_hour / .local_min before
// sim_set_env(); false means "use the real local time".
bool     god_freeze_clock(uint8_t& hour, uint8_t& minute);

// Whether the periodic soak log is armed (GST_BOTH on the root list toggles it).
bool     god_dump_enabled(void);

// -----------------------------------------------------------------------------
//  THE PER-FRAME HEAP PROBE (plan P4-C4: "ESP.getFreeHeap() delta 0 per frame
//  in DIAG").
//
//  Sampled inside god_draw_marker(), which every RENDERED frame calls last and
//  which does nothing while god mode is off - so the measurement is per FRAME
//  rather than per loop(), and it costs nothing in the shipped configuration.
//  Two numbers, because one would not be enough to believe: how many frames
//  since god_enter() moved the free heap at all, and the widest single move.
//  A screen that allocates once per frame shows a rising count; a screen that
//  allocates once, at entry, shows 1 - which is why the count is not a bool.
//  The SYS / HEAP page prints both.
// -----------------------------------------------------------------------------
uint16_t god_frame_heap_moves(void);   // frames whose free heap differed from the last
int32_t  god_frame_heap_worst(void);   // the widest delta seen, in bytes

// -----------------------------------------------------------------------------
//  P10-C1: THE SERIAL COMMAND LAYER'S ONE OUTPUT CHANNEL
//
//  god_handle() returns what a GESTURE asked for. A typed line arrives from
//  god_service(), which returns void and is called from app/app.cpp's loop -
//  nowhere near the screen stack. So a line that asks for navigation LATCHES an
//  event here and ui.cpp drains it once per frame through the SAME switch that
//  handles god_handle()'s result. One latch deep: a second event before the
//  first is drained replaces it, because a queue of console navigations is a
//  queue of surprises.
//
//  Returns GOD_EVT_NONE and leaves `arg` alone when there is nothing pending.
//  `arg` carries GOD_EVT_MINIGAME's index and is 0 for every other event.
// -----------------------------------------------------------------------------
GodEvt  god_take_evt(uint8_t& arg);

// -----------------------------------------------------------------------------
//  P10-C1: THE BOOT LOAD RESULT, RECORDED INSTEAD OF DROPPED
//
//  app/app.cpp took the boot LoadResult as a LOCAL, printed it once inside one
//  Serial.printf and let it go out of scope. Spec section 49 asks for a "Last
//  error" field and the single most useful thing that field can say - "this
//  boot came back LOAD_RECOVERED_PAIR" - was therefore unreachable from any
//  screen and from any command, for ever, on every build.
//
//  ONE BYTE, AND IT LIVES ABOVE THE GOD_MODE_ENABLED GUARD ON PURPOSE. That is
//  the whole structural lesson of the phase-6 defect: what the SHIPPING artefact
//  needs must not sit inside the console's #if. The release build calls
//  god_note_load() from the same line of app.cpp and `info` prints it.
// -----------------------------------------------------------------------------
void    god_note_load(uint8_t load_result);   // a LoadResult, from app_setup()
uint8_t god_load_result(void);

// Compile-time sanity on the constants this module contracts against.
static_assert(GOD_SCALE_COUNT == 5, "godmode.h: the speed ring is five wide");
static_assert(GOD_SCALE_0 == 1, "godmode.h: index 0 must be real time");
static_assert(GOD_ABSENCE_COUNT == 6, "godmode.h: six forced-absence durations");
static_assert(GOD_CMD_COUNT == 8,
              "godmode.h: 8 commands - the 7 that survived the BLE mating surgery "
              "plus P4-C4's test_battle. NOT a weakened guard: it still pins the "
              "root list length that GOD_MENU_ROWS and open_command() are written against");
static_assert(GOD_BAR_H <= STATUS_BAR_H, "god marker must fit the status bar rows");

#endif // NT_GODMODE_H
