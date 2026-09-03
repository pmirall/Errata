// =============================================================================
//  PEBBLEBOL - dev/godmode.h
//  The debug console. The GOD screen plus the persistent state
//  that makes the rest of the firmware testable in minutes instead of a week:
//  the time-scale multiplier, forced absences, a forced stage,
//  a genome editor with hex dump and paste-in, a live heap panel and a CSV
//  soak log over Serial.
//
//  COMPILED ALWAYS. Entry is undocumented in the product:
//  SCR_STATUS_B + both buttons held GOD_ENTER_HOLD_MS. With GOD_MODE_ENABLED
//  set to 0 every entry point below still links, god_active() is permanently
//  false and nothing of the console reaches flash.
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
//    without touching this file (the ble_social.h pattern). config.h already
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

// Serial paste buffer for "GENOMA / CARGAR". 32 hex chars + slack for spaces
// and a CR/LF; anything longer is discarded and the line restarts.
#ifndef GOD_HEX_LINE_MAX
#define GOD_HEX_LINE_MAX       48
#endif

// -----------------------------------------------------------------------------
// 2. SHAPE CONSTANTS
// -----------------------------------------------------------------------------

// Editable genes in "GENOMA / EDITAR", index-parallel to strings_es.h 34c.
#define GOD_GENE_COUNT         13

// "FIJAR STAT" offers 0 / 25 / 50 / 100.
#define GOD_STATVAL_COUNT      4

// The root list is the seven surviving god commands plus an
// explicit exit row. HOLD_R leaves the SCREEN (god mode stays on, and stays visible); the
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

// Compile-time sanity on the constants this module contracts against.
static_assert(GOD_SCALE_COUNT == 5, "godmode.h: the speed ring is five wide");
static_assert(GOD_SCALE_0 == 1, "godmode.h: index 0 must be real time");
static_assert(GOD_ABSENCE_COUNT == 6, "godmode.h: six forced-absence durations");
static_assert(GOD_CMD_COUNT == 7, "godmode.h: 7 commands after the BLE mating surgery");
static_assert(GOD_BAR_H <= STATUS_BAR_H, "god marker must fit the status bar rows");

#endif // NT_GODMODE_H
