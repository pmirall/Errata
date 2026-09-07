// =============================================================================
//  PEBBLEBOL - dev/godmode.cpp
//  The debug console (GAME_DESIGN 10). See godmode.h for the contract.
//
//  Everything the console mutates goes through the owning module's own API:
//  sim_god_*() for the pet, gt_skew_add() for time, gs_*() for NVS. This
//  file mutates NOTHING directly - which is exactly why a test run through it
//  exercises the real code paths.
//
//  ZERO floating point. Every number rendered here is an integer formatted
//  with snprintf into a fixed stack buffer. No String, no heap.
// =============================================================================
#include "godmode.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

// THE PURE HALF. Everything with a right answer - the command table, the
// parser, the range checks, the taint decision, the field formatting and the
// execution of every command that only touches pure modules - is in
// dev/diag_core.cpp, because THIS file includes <Arduino.h> two lines above and
// therefore no host binary in this repository compiles it. See diag_core.h's
// banner: it is the same move as ui/petfx_core.cpp (P9-C6) and
// hardware/power.cpp's pwr_tick_budget() (P6-C3).
#include "diag_core.h"

// The always-compiled surface reads these. They are all already linked into the
// release artefact - this adds the READERS, not the modules.
#include "../core/version.h"
#include "../game/box.h"
#include "../hardware/boot.h"
#include "../hardware/gametime.h"
#include "../hardware/kv_nvs.h"
#include "../networking/net.h"
#include "../networking/transport_espnow.h"  // P10-C6: espnow_stats(), which had no reader
#include "../persistence/game_state.h"
#include "../persistence/save_manager.h"
#include "../ui/screen_network.h"    // network_screen_seen/fresh/phase
#include "../core/perf.h"              // P10-C2: the frame / pass instrument

// =============================================================================
//  0. HEAP TREND (plan P2-C12)
//  The phase-exit heap baseline. One Serial line every GOD_HEAP_PERIOD_MS with
//  the free heap and the low-water mark since boot, emitted on EVERY screen,
//  with god mode OFF, and in the GOD_MODE_ENABLED == 0 build as well - the
//  bench soak it exists for is "one hour on HOME with the radio OFF, look for a
//  flat line", which is precisely the configuration where nothing else prints.
//  This block therefore sits OUTSIDE the console's own #if.
//
//  Format, stable and greppable:  DIAG,heap,<uptime_s>,<free_b>,<min_free_b>
//  ESP.getMinFreeHeap() is the IDF's own low-water mark since boot, so a leak
//  shows up as a falling min even when the instantaneous free heap looks calm.
// =============================================================================
static uint32_t s_heap_last_ms = 0;

static void heap_trend_begin(void)
{
  // Back-date the timer by one full period so the first god_service() prints
  // the t=0 sample instead of leaving the trend without an origin.
  s_heap_last_ms = (uint32_t)millis() - (uint32_t)GOD_HEAP_PERIOD_MS;
  Serial.printf("DIAG#,heap,uptime_s,free_b,min_free_b\r\n");
  Serial.printf("DIAG#,perf,uptime_s,scr,frames,frame_max_us,frame_worst_us,"
                "frame_worst_scr,frame_over,frame_sat,passes,pass_worst_us,"
                "pass_worst_scr,pass_over,discards\r\n");
  Serial.printf("DIAG#,link,uptime_s,rx_session,rx_beacon,rx_wrong_peer,"
                "rx_malformed,rx_ring_ovf,rx_beacon_ovf,tx_ok,tx_fail,"
                "tx_refused,tx_no_mem,beacons_tx\r\n");
  // THE ScreenId MAP, ONCE, SO A CAPTURE IS SELF-DESCRIBING (P10-C6). The perf
  // row's scr / frame_worst_scr / pass_worst_scr columns are raw enum values,
  // docs/bench.md A1's method is "walk the screens, then read which one owned
  // the worst frame", and nothing turned s17 back into a screen. The enum is
  // not stable across phases either - P10-C4 inserted two setup screens IN THE
  // MIDDLE - so a mapping written down from an older log is wrong rather than
  // merely old. Emitting it into the capture itself is the only version that
  // cannot rot: 29 short names, printed once at boot, in the same file the
  // numbers are in.
  Serial.printf("DIAG#,screens");
  for (uint8_t i = 0; i < (uint8_t)SCR_COUNT; ++i) {
    Serial.printf(",%u=%s", (unsigned)i, diag_screen_name(i));
  }
  Serial.printf("\r\n");
}

// P10-C6. THE ESP-NOW COUNTERS, WHICH HAD NO READER OF ANY KIND: espnow_stats()
// was declared, defined, and called by no screen, no page, no serial line and
// no test. docs/bench.md B2 - the consent gate on the air, and the precondition
// for every other two-board item - names `rx_wrong_peer` rising on the silent
// board as its acceptance instrument, and there was no way to read it. That is
// the dead-export shape this phase has now recorded three times.
//
// SILENT UNTIL THERE IS SOMETHING TO SAY. A board that has never brought the
// radio up prints nothing here, so the 24 h soak capture (docs/bench.md C5,
// radio off) is not padded with a row of zeros every minute.
static void link_trend_service(uint32_t now_ms)
{
  const EspNowStats& s = espnow_stats();
  const uint32_t any = s.rx_session | s.rx_beacon | s.rx_wrong_peer |
                       s.rx_malformed | s.tx_ok | s.tx_fail | s.beacons_tx;
  if (any == 0u) return;
  Serial.printf("DIAG,link,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu\r\n",
                (unsigned long)(now_ms / 1000UL),
                (unsigned long)s.rx_session,   (unsigned long)s.rx_beacon,
                (unsigned long)s.rx_wrong_peer,(unsigned long)s.rx_malformed,
                (unsigned long)s.rx_ring_overflow,
                (unsigned long)s.rx_beacon_overflow,
                (unsigned long)s.tx_ok,        (unsigned long)s.tx_fail,
                (unsigned long)s.tx_refused,   (unsigned long)s.tx_no_mem,
                (unsigned long)s.beacons_tx);
}

// P10-C2. THE PERFORMANCE RECEIPT, AND IT IS A SEPARATE LINE ON PURPOSE.
// `DIAG,heap,` is the 24 h soak's own format and has been stable since P2-C12;
// appending columns to it would break any parser somebody already has for a
// capture, and this line has a different lifetime anyway (the soak wants one
// number an hour, a screen walk wants a row per screen). The two share the
// GOD_HEAP_PERIOD_MS tick because they are read together.
//
// THIS IS THE ONLY PLACE ANY OF SPEC SECTION 46'S TIME NUMBERS REACHES A HUMAN
// ON A RELEASE BOARD. dev/godmode.cpp's console cannot be opened when
// GOD_MODE_ENABLED is 0 - god_active() is a hardcoded `false`, so
// ui/screen_diag.cpp's update hook sends SCR_DIAG straight home - so a bench
// step that reads a console page is a bench step against the dev build, which
// is the phase-6/phase-7 shape. This block is above the guard for that reason,
// exactly as the heap line beside it is.
static void perf_trend_service(uint32_t now_ms)
{
  const uint8_t scr = perf_screen();
  Serial.printf("DIAG,perf,%lu,%u,%lu,%u,%lu,%u,%u,%u,%lu,%lu,%u,%u,%u\r\n",
                (unsigned long)(now_ms / 1000UL),
                (unsigned)scr,
                (unsigned long)perf_frames(),
                (unsigned)perf_frame_max_us(scr),
                (unsigned long)perf_frame_worst_us(),
                (unsigned)perf_frame_worst_screen(),
                (unsigned)perf_frame_overruns(),
                (unsigned)perf_frame_saturated(),
                (unsigned long)perf_passes(),
                (unsigned long)perf_pass_worst_us(),
                (unsigned)perf_pass_worst_screen(),
                (unsigned)perf_pass_overruns(),
                (unsigned)perf_discards());
}

static void heap_trend_service(void)
{
  const uint32_t now = (uint32_t)millis();
  if ((uint32_t)(now - s_heap_last_ms) < (uint32_t)GOD_HEAP_PERIOD_MS) return;
  s_heap_last_ms = now;
  Serial.printf("DIAG,heap,%lu,%lu,%lu\r\n",
                (unsigned long)(now / 1000UL),
                (unsigned long)ESP.getFreeHeap(),
                (unsigned long)ESP.getMinFreeHeap());
  perf_trend_service(now);
  link_trend_service(now);
}

// =============================================================================
//  0b. THE BOOT LOAD RESULT (P10-C1, godmode.h)
//      One byte, above the guard, because the SHIPPING build's `info` prints it
//      and app/app.cpp's `load` was a local it dropped on the floor.
// =============================================================================
static uint8_t s_load_result = 0;   // a LoadResult; LOAD_OK until told otherwise

void god_note_load(uint8_t load_result) { s_load_result = load_result; }
uint8_t god_load_result(void)           { return s_load_result; }

// =============================================================================
//  0c. THE ALWAYS-COMPILED DIAGNOSTICS SURFACE (P10-C1)
//
//  ONE serial line reader for the whole module. Before this phase there were
//  TWO - hex_paste_service() and cmd_service() - each calling Serial.read() and
//  each having to remember not to fight the other, and BOTH were below the
//  GOD_MODE_ENABLED guard, so a release board was deaf. Now the reader is here,
//  it assembles a line, and it offers that line to the console first (which
//  owns the genome paste and every mutating command) and to the read-only
//  surface second.
//
//  WHAT SHIPS: help / info / show_save / stall. dev/diag_core.h's DCF_ALWAYS
//  flag is the authority and the static_assert next to it forbids a mutating
//  command from carrying it.
// =============================================================================
// The bounded busy-wait. It exists to be able to PROVE the timer-driven button
// sampler (input.cpp note 6) on real hardware: a press made during the stall
// must still be recognised, with its true duration, on the poll that follows.
// P10-C2's "input-to-render latency <= 2 frames measured with the DIAG stall
// command" is a measurement on a RELEASE board, so this is above the guard.
// Well under the 5 s task watchdog.
#define GOD_STALL_MAX_MS   3000UL
#define GOD_STALL_DEF_MS   500UL

static void shell_do_stall(uint32_t ms)
{
  if (ms > GOD_STALL_MAX_MS) ms = GOD_STALL_MAX_MS;
  Serial.printf("[diag] stall %lu ms begin\r\n", (unsigned long)ms);
  const uint32_t t0 = millis();
  while ((uint32_t)(millis() - t0) < ms) {
    // Deliberately busy. yield() here would defeat the whole point.
  }
  Serial.printf("[diag] stall end after %lu ms\r\n", (unsigned long)(millis() - t0));
}

// Defined in BOTH arms of the #if below: the console's own line handler when it
// is compiled in, and an inert `false` when it is not. Returns true when the
// console consumed the line.
static bool    shell_dev_line(const char* line);
// The console's accumulated forced-absence skew, 0 in the release build.
static int32_t shell_skew_s(void);

// Fill spec section 49's inputs from the device. EVERY line here is a read of
// an accessor that already existed and that the console had never joined up.
static void shell_fill_fields(DiagFields& f)
{
  diag_fields_clear(f);

  f.free_heap  = (uint32_t)ESP.getFreeHeap();
  f.min_heap   = (uint32_t)ESP.getMinFreeHeap();
  f.max_alloc  = (uint32_t)ESP.getMaxAllocHeap();

  f.now_epoch   = gt_now();
  f.last_seen   = save_last_seen();
  f.cal_state   = (uint8_t)gt_cal_state();
  f.clock_valid = gt_is_valid() ? 1u : 0u;
  f.skew_s      = shell_skew_s();

  f.net_mode  = (uint8_t)net_mode();
  f.net_phase = (uint8_t)net_phase();
  f.net_err   = (uint8_t)net_last_err();
  f.ap_up     = net_is_ap_up() ? 1u : 0u;

  const GameState& gs = gs_state();
  f.save_schema     = gs.box.schema_version;
  f.content_version = gs.box.content_version;
  f.save_migrated   = save_was_migrated() ? 1u : 0u;
  f.quarantine_mask = save_quarantine_mask();
  f.load_result     = god_load_result();

  f.pebbles = box_count();
  f.box_cap = box_capacity();

  // ui/screen_network.h says in its own header that these exist "for tests and
  // for the DIAG console", and the console had never read one of them.
  f.scan_seen  = network_screen_seen();
  f.scan_fresh = network_screen_fresh();
  f.scan_phase = network_screen_phase();
  f.scan_valid = (uint8_t)((f.scan_seen || f.scan_fresh ||
                            f.scan_phase != (uint8_t)NSP_SCANNING) ? 1u : 0u);

  f.kv_healthy     = kv_healthy(KV_MAIN) ? 1u : 0u;
  f.kv_error       = (uint8_t)kv_error();
  f.kv_write_fails = (uint8_t)kv_write_fails();

  f.uptime_s = (uint32_t)(millis() / 1000UL);
}

// spec section 49's field list, one field per print so the stack buffer stays
// small and nothing is ever truncated.
static void shell_print_info(void)
{
  DiagFields f;
  shell_fill_fields(f);
  char b[144];
  for (uint8_t i = 0; i < (uint8_t)DGD_FIELD_COUNT; ++i) {
    DiagOut o;
    diag_out_init(o, b, (uint16_t)sizeof(b));
    diag_fmt_field(i, f, o);
    Serial.print(b);
    if (o.truncated) Serial.print(F("..."));
    Serial.print(F("\r\n"));
  }
}

// spec section 66's show_save. Read-only, so it ships.
static void shell_print_save(void)
{
  const GameState& gs = gs_state();
  const BoxHeader& b  = gs.box;
  Serial.printf("save schema=%u proto=%u content=0x%04X seq=%lu\r\n",
                (unsigned)b.schema_version, (unsigned)b.protocol_version,
                (unsigned)b.content_version, (unsigned long)b.seq);
  Serial.printf("save slots=0x%04X active=%u count=%u/%u next_id=%lu\r\n",
                (unsigned)b.slot_mask, (unsigned)b.active_slot,
                (unsigned)box_count(), (unsigned)box_capacity(),
                (unsigned long)b.next_id_counter);
  Serial.printf("save epoch=%lu flags=0x%02X quarantine=0x%04X migrated=%u\r\n",
                (unsigned long)b.saved_epoch, (unsigned)b.flags,
                (unsigned)save_quarantine_mask(),
                (unsigned)(save_was_migrated() ? 1u : 0u));
  // THE TWO DEAD COUNTERS, NAMED RATHER THAN QUIETLY PRINTED AS ZERO.
  // BoxHeader.captures and .battles carry the comment "lifetime counters, spec
  // section 49" and NOTHING IN THE TREE EVER INCREMENTS EITHER: their only
  // reader is the device-id hash at persistence/game_state.cpp:47. They ship at
  // zero, so the line says so instead of implying a measurement.
  Serial.printf("save captures=%u battles=%u (no writer in the tree; they ship at 0)\r\n",
                (unsigned)b.captures, (unsigned)b.battles);
  for (uint8_t s = 0; s < (uint8_t)BOX_SLOTS; ++s) {
    if (!box_occupied(s)) continue;
    const PebbleInstance* p = box_peek(s);
    if (!p) continue;
    Serial.printf("  slot%u sp=%u lv=%u id=%08lX q=%u\r\n",
                  (unsigned)s, (unsigned)p->species_id, (unsigned)p->level,
                  (unsigned long)p->id,
                  (unsigned)save_quarantine_reason(s));
  }
}

// The read-only half. Runs in EVERY build; the dev half gets first refusal.
static void shell_always_line(const char* line)
{
  DiagParse pr;
  diag_parse(line, pr);
  if (pr.err == DGP_EMPTY) return;

  const bool dev = (GOD_MODE_ENABLED != 0);

  if (pr.err != DGP_OK) {
    // A command the release build knows about but cannot run is NOT "no such
    // command": say which it is and why, or the operator retypes it.
    const DiagCmdSpec* sp = diag_cmd_spec(pr.cmd);
    if (pr.err == DGP_UNKNOWN) Serial.printf("[diag] no such command; try help\r\n");
    else Serial.printf("[diag] %s: %s\r\n", sp ? sp->name : "?",
                       diag_parse_err_str(pr.err));
    return;
  }

  const DiagCmdSpec* sp = diag_cmd_spec(pr.cmd);
  if (!sp) return;

  switch (pr.cmd) {
    case DGC_HELP: {
      // Line by line, with a small buffer. The whole text is ~1.2 kB and the
      // loop task's stack is not the place for it - a truncated help is a help
      // that lies about what this build can do.
      Serial.print(F("[diag] commands:\r\n"));
      char b[96];
      for (uint8_t i = 0; i <= (uint8_t)DGC_COUNT; ++i) {
        DiagOut o;
        diag_out_init(o, b, (uint16_t)sizeof(b));
        if (!diag_help_line(i, dev, o)) continue;
        Serial.print(b);
        if (o.truncated) Serial.print(F("..."));
        Serial.print(F("\r\n"));
      }
      return;
    }
    case DGC_INFO:      shell_print_info(); return;
    case DGC_SHOW_SAVE: shell_print_save(); return;
    case DGC_STALL:
      shell_do_stall((pr.argc >= 1) ? pr.arg[0] : GOD_STALL_DEF_MS);
      return;
    default: break;
  }

  // Anything else is a mutating command. In the dev build shell_dev_line() has
  // already had it and returned true, so reaching here means this build cannot
  // run it - and it says which flag decides that rather than shrugging.
  Serial.printf("[diag] %s needs GOD_MODE_ENABLED 1 (not in this build)\r\n",
                sp->name);
}

static char    s_line[GOD_LINE_MAX + 1];
static uint8_t s_linelen = 0;

static void shell_line_reset(void)
{
  s_linelen = 0;
  s_line[0] = '\0';
}

static void shell_line_service(void)
{
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c < 0) break;
    if (c == '\n' || c == '\r') {
      if (s_linelen > 0) {
        s_line[s_linelen] = '\0';
        // The console first: it owns the genome paste and every mutating
        // command. shell_dev_line() is inert in the release build.
        if (!shell_dev_line(s_line)) shell_always_line(s_line);
      }
      shell_line_reset();
      continue;
    }
    // An overlong line RESTARTS rather than truncating into a different
    // command: "give_item 3<junk...>spawn 1" must not become a spawn.
    if (s_linelen >= (uint8_t)GOD_LINE_MAX) { shell_line_reset(); continue; }
    s_line[s_linelen++] = (char)c;
    s_line[s_linelen]   = '\0';
  }
}

#if GOD_MODE_ENABLED

#include <time.h>

#include "../ui/render.h"        // the only U8G2 owner; never construct another one
#include "../core/strings_es.h"
#include "../game/sim.h"
#include "../game/genome.h"
#include "../persistence/game_state.h"
#include "../hardware/boot.h"
#include "../hardware/kv_nvs.h"
#include "../hardware/gametime.h"
#include "../hardware/input.h"
#include "../hardware/power.h"      // the P6-C3 ladder, for the ENERGIA page
#include "../networking/net.h"

// The gene-name block in strings_es.h 34c must stay index-parallel to GENES[].
static_assert((int)STR_GN_RARE - (int)STR_GN_SPECIES + 1 == GOD_GENE_COUNT,
              "strings_es.h 34c drifted from GOD_GENE_COUNT");

#define GOD_LOGF(...) do { Serial.printf(__VA_ARGS__); } while (0)

// =============================================================================
//  1. LAYOUT
//     Rows 0..GOD_BAR_H-1  : the inverted marker bar (drawn last, always wins)
//     Rows GOD_BAR_H..55   : content
//     Rows 56..63          : the affordance strip (rd_affordance owns it)
// =============================================================================
#define GD_ROW_H        9
#define GD_ROOT_Y0      10
#define GD_ROOT_ROWS    5
#define GD_SUB_Y0       19
#define GD_SUB_ROWS     4
#define GD_TITLE_BASE   16
#define GD_SEP_Y        17
#define GD_SCROLL_X     125

// =============================================================================
//  2. CONSOLE SCREENS
// =============================================================================
enum GodScreen : uint8_t {
  GSC_MENU = 0,   // the 8 commands + the exit row
  GSC_SPEED,      // 1  VELOCIDAD
  GSC_ABSENCE,    // 2  SALTAR AUSENCIA
  GSC_STATPICK,   // 3  FIJAR STAT
  GSC_STATVAL,    // 3  the value ring for the pick above
  GSC_STAGE,      // 4  FORZAR ETAPA
  GSC_GENOME,     // 5  GENOMA root
  GSC_GENE,       // 5  GENOMA / EDITAR
  GSC_HEX,        // 5  GENOMA / VOLCAR + CARGAR
  GSC_SYS,        // 6  RELOJ + the heap / radio / storage panels
  GSC_CONFIRM,    // shared modal, cursor defaults to NO
  GSC_COUNT
};

// What a GSC_CONFIRM "YES" commits to.
enum GodConfirm : uint8_t {
  GCF_NONE = 0,
  GCF_WIPE1,
  GCF_WIPE2
};

// GSC_HEX has two jobs.
enum GodHexMode : uint8_t { GHX_DUMP = 0, GHX_LOAD };

// =============================================================================
//  3. STATIC TABLES
// =============================================================================
static const uint32_t GD_SCALES[GOD_SCALE_COUNT] = {
  (uint32_t)GOD_SCALE_0, (uint32_t)GOD_SCALE_1, (uint32_t)GOD_SCALE_2,
  (uint32_t)GOD_SCALE_3, (uint32_t)GOD_SCALE_4
};

// GAME_DESIGN 10.1 command 2: 1 h, 6 h, 24 h, 72 h, 168 h, 720 h.
static const uint32_t GD_ABSENCES[GOD_ABSENCE_COUNT] = {
  3600UL, 21600UL, 86400UL, 259200UL, 604800UL, 2592000UL
};

static const uint8_t GD_STATVALS[GOD_STATVAL_COUNT] = { 0, 25, 50, 100 };

// The root list. Seven commands, then the explicit exit row.
static constexpr uint16_t GD_MENU_STR[GOD_MENU_ROWS] = {
  (uint16_t)STR_GOD_SPEED,    (uint16_t)STR_GOD_ABSENCE, (uint16_t)STR_GOD_SETSTAT,
  (uint16_t)STR_GOD_STAGE,    (uint16_t)STR_GOD_GENOME,
  (uint16_t)STR_GOD_CLOCK,    (uint16_t)STR_GOD_WIPE,
  (uint16_t)STR_BT_TEST,      (uint16_t)STR_AF_QUIT
};

// The two root rows draw_menu() prints a live value next to.
enum : uint8_t { GD_ROW_SPEED = 0, GD_ROW_CLOCK = 5 };
static_assert(GD_MENU_STR[GD_ROW_SPEED] == (uint16_t)STR_GOD_SPEED,
              "GD_ROW_SPEED no longer names the speed row");
static_assert(GD_MENU_STR[GD_ROW_CLOCK] == (uint16_t)STR_GOD_CLOCK,
              "GD_ROW_CLOCK no longer names the clock row");
// P4-C4's test_battle (spec section 49). open_command() switches on the ROW
// NUMBER with no lookup table, exactly as ui/screen_care.cpp's PLAY list does,
// so a row inserted in the wrong place would open somebody else's sub-screen.
enum : uint8_t { GD_ROW_BATTLE = 7, GD_ROW_QUIT = 8 };
static_assert(GD_MENU_STR[GD_ROW_BATTLE] == (uint16_t)STR_BT_TEST,
              "GD_ROW_BATTLE no longer names the test_battle row");
static_assert(GD_MENU_STR[GD_ROW_QUIT] == (uint16_t)STR_AF_QUIT,
              "the exit row must stay last");
static_assert(GD_ROW_QUIT + 1 == GOD_MENU_ROWS, "the root list grew without its rows");

// The gene editor. Index-parallel to strings_es.h block 34c.
struct GodGene {
  uint16_t label;                        // StrId
  uint8_t  vmax;                         // inclusive
  uint8_t  (*get)(const Genome&);
  void     (*set)(Genome&, uint8_t);
};
static const GodGene GD_GENES[GOD_GENE_COUNT] = {
  { (uint16_t)STR_GN_SPECIES,  15, gene_species,     gene_set_species     },
  { (uint16_t)STR_GN_PATTERN,  15, gene_pattern,     gene_set_pattern     },
  { (uint16_t)STR_GN_PALETTE,   7, gene_palette,     gene_set_palette     },
  { (uint16_t)STR_GN_BODYSIZE,  7, gene_body_size,   gene_set_body_size   },
  { (uint16_t)STR_GN_EARHORN,   3, gene_ear_horn,    gene_set_ear_horn    },
  { (uint16_t)STR_GN_APPETITE, 15, gene_appetite,    gene_set_appetite    },
  { (uint16_t)STR_GN_METAB,    15, gene_metabolism,  gene_set_metabolism  },
  { (uint16_t)STR_GN_SOCIAB,   15, gene_sociability, gene_set_sociability },
  { (uint16_t)STR_GN_TEMPER,   15, gene_temperament, gene_set_temperament },
  { (uint16_t)STR_GN_HARDY,    15, gene_hardiness,   gene_set_hardiness   },
  { (uint16_t)STR_GN_LUCK,      7, gene_luck,        gene_set_luck        },
  { (uint16_t)STR_GN_SEX,       1, gene_sex,         gene_set_sex         },
  { (uint16_t)STR_GN_RARE,      1, gene_rare,        gene_set_rare        }
};

// GSC_GENOME rows.
#define GD_GEN_RANDOM   0
#define GD_GEN_EDIT     1
#define GD_GEN_DUMP     2
#define GD_GEN_LOAD     3
#define GD_GEN_ROWS     4

// GSC_SYS pages. GD_SYS_PET is the watch panel for an accelerated run: at
// GOD_SCALE_4 a whole life goes past in minutes and the operator needs to see
// the stats move without leaving the console.
#define GD_SYS_CLOCK    0
#define GD_SYS_PET      1
#define GD_SYS_HEAP     2
#define GD_SYS_RADIO    3
#define GD_SYS_STORE    4
// P6-C3: "DIAG shows loop iterations/s per power state". The acceptance number
// this page exists to read is the plan's own - loop rate in IDLE <= 10/s - and
// it is the one figure that says whether the ladder is really stopping the CPU
// or only turning the panel off.
#define GD_SYS_POWER    5
// P10-C1: spec section 49's FIELD LIST, on the panel rather than only on the
// serial line, because a bench operator with two boards and no laptop is the
// person this console is for.
#define GD_SYS_INFO     6
// P10-C2: spec section 46's frame and loop() numbers. BASELINE ONLY, and that
// is the point of the DIAG,perf serial line beside it - this page does not
// exist in the artefact that ships, because the console it lives in cannot be
// opened there at all.
#define GD_SYS_PERF     7
#define GD_SYS_PAGES    8

// =============================================================================
//  4. MODULE STATE
// =============================================================================
static bool     s_active      = false;
static uint8_t  s_console_page      = GSC_MENU;
static uint8_t  s_menu_cur    = 0;        // root list cursor, survives sub-screens
static uint8_t  s_sub_cur     = 0;        // cursor inside the current sub-screen
static uint8_t  s_scale_idx   = 0;
static uint8_t  s_pick        = 0;        // GSC_STATVAL: which stat/condition
static uint8_t  s_gene_idx    = 0;
static uint8_t  s_sys_page    = 0;
static uint8_t  s_hex_mode    = GHX_DUMP;
static uint8_t  s_confirm_act = GCF_NONE;
static bool     s_confirm_yes = false;    // GAME_DESIGN 8.2: defaults to NO
static uint16_t s_confirm_str = (uint16_t)STR_EMPTY;

// The per-frame heap probe (godmode.h). Sampled from god_draw_marker(), which
// is the LAST thing every rendered frame does and which returns immediately
// while god mode is off - so it measures frames, costs nothing when the console
// is not in use, and needs no hook in ui.cpp or app.cpp to exist.
static uint16_t s_fh_moves = 0;
static int32_t  s_fh_worst = 0;
static uint32_t s_fh_last  = 0;
static bool     s_fh_armed = false;

static void frame_heap_sample(void)
{
  const uint32_t now = (uint32_t)ESP.getFreeHeap();
  if (!s_fh_armed) { s_fh_armed = true; s_fh_last = now; return; }
  const int32_t d = (int32_t)(now - s_fh_last);
  s_fh_last = now;
  if (d == 0) return;
  if (s_fh_moves < 0xFFFFu) ++s_fh_moves;
  if (d > s_fh_worst || -d > s_fh_worst) s_fh_worst = (d < 0) ? -d : d;
}

uint16_t god_frame_heap_moves(void) { return s_fh_moves; }
int32_t  god_frame_heap_worst(void) { return s_fh_worst; }


static uint16_t s_toast_str   = (uint16_t)STR_EMPTY;
static uint32_t s_toast_until = 0;

static bool     s_dump_on     = true;
static uint32_t s_dump_last   = 0;

static bool     s_frozen      = false;
static uint8_t  s_frozen_h    = 0;
static uint8_t  s_frozen_m    = 0;

static int32_t  s_skew_total  = 0;        // what god mode has handed gt_skew_add

// Absence result panel.
static AbsenceReport s_abs_rep;
static bool     s_abs_done    = false;

// The last dumped/loaded genome, 32 hex + NUL, for the GSC_HEX panel.
// THE TWO SERIAL BUFFERS THAT USED TO LIVE HERE ARE GONE (P10-C1): s_hex[49]
// and s_cmd[24] each backed their own Serial.read() loop, both below the
// GOD_MODE_ENABLED guard - so a release board was deaf and the two readers had
// to be written not to fight each other. One s_line[57] above the guard
// replaces both, which is 16 bytes of globals back and one reader instead of
// two.
static char     s_hex_show[33];

// =============================================================================
//  5. SMALL HELPERS
// =============================================================================
static inline const SimView* pet(void) { return sim_view(); }

static void toast(uint16_t str_id)
{
  s_toast_str   = str_id;
  s_toast_until = millis() + GOD_TOAST_MS;
}

static void list_step(uint8_t& cur, uint8_t count, int8_t d)
{
  if (count == 0) { cur = 0; return; }
  int v = (int)cur + (int)d;
  while (v < 0)            v += (int)count;
  while (v >= (int)count)  v -= (int)count;
  cur = (uint8_t)v;
}

// Top row of a scrolling window that keeps `cur` roughly centred.
static uint8_t win_top(uint8_t cur, uint8_t count, uint8_t rows)
{
  if (count <= rows) return 0;
  int top = (int)cur - (int)(rows / 2);
  if (top < 0) top = 0;
  if (top > (int)count - (int)rows) top = (int)count - (int)rows;
  return (uint8_t)top;
}

// One CSV line after every state change, plus a forced save (GAME_DESIGN 10.2).
static void changed(void)
{
  const SimView* p = pet();
  if (p) gs_save_active(true);
  god_dump_line();
}

static void open_confirm(uint16_t question, uint8_t act)
{
  s_confirm_str = question;
  s_confirm_act = act;
  s_confirm_yes = false;          // NO, always
  s_console_page      = GSC_CONFIRM;
}

// -----------------------------------------------------------------------------
// Time scale. The ONLY way acceleration reaches the game is sim_step_seconds(),
// which returns whatever sim_set_time_scale() was last given.
// -----------------------------------------------------------------------------
static void set_scale(uint8_t idx)
{
  if (idx >= GOD_SCALE_COUNT) idx = 0;
  s_scale_idx = idx;
  const uint32_t sc = GD_SCALES[idx];
  sim_set_time_scale(sc);

  if (sc > 1u) {
    if (!s_frozen) {
      // GAME_DESIGN 10.2: freeze the sleep window at the hour we left reality.
      const SimEnv& e = sim_env();
      s_frozen_h = e.local_hour;
      s_frozen_m = e.local_min;
      s_frozen   = true;
    }
  } else {
    s_frozen = false;
  }
  GOD_LOGF("[god] scale=x%lu frozen=%u %02u:%02u\n", (unsigned long)sc,
           (unsigned)s_frozen, (unsigned)s_frozen_h, (unsigned)s_frozen_m);
}

// -----------------------------------------------------------------------------
// Push a SimEnv that matches the clock right now. Used before the forced
// absence, which needs sim to agree with gametime about what "now" is.
// -----------------------------------------------------------------------------
static void push_env_now(void)
{
  SimEnv e = sim_env();
  struct tm lt;
  memset(&lt, 0, sizeof(lt));
  const bool ok = gt_local_tm(lt);

  e.now_epoch   = gt_now();
  e.local_hour  = (uint8_t)((lt.tm_hour >= 0 && lt.tm_hour < 24) ? lt.tm_hour : 0);
  e.local_min   = (uint8_t)((lt.tm_min  >= 0 && lt.tm_min  < 60) ? lt.tm_min  : 0);
  e.day_of_year = (uint16_t)((lt.tm_yday >= 0 && lt.tm_yday < 366) ? lt.tm_yday : 0);
  e.clock_valid = ok ? 1u : 0u;
  sim_set_env(e);
}

// -----------------------------------------------------------------------------
// Command 2: inject a real absence. The clock genuinely moves forward (skew,
// never the system clock) and sim runs the GAME_DESIGN 5.2 offline path.
// -----------------------------------------------------------------------------
static void run_absence(uint32_t secs)
{
  if (!pet()) { toast((uint16_t)STR_GOD_NOPET); return; }

  gt_skew_add((int64_t)secs);
  if (s_skew_total <= (int32_t)(0x7FFFFFFF - (int32_t)secs)) s_skew_total += (int32_t)secs;

  push_env_now();

  memset(&s_abs_rep, 0, sizeof(s_abs_rep));
  sim_catch_up_ex(secs, 1u, s_abs_rep);
  s_abs_done = true;

  gs_touch_lastseen(gt_now());
  changed();
  GOD_LOGF("[god] absence %lus known=%u steps=%u\n",
           (unsigned long)secs, (unsigned)s_abs_rep.clock_known,
           (unsigned)s_abs_rep.steps);
}

// -----------------------------------------------------------------------------
// Command 8 (BORRAR TODO): factory reset, then a brand new gen-0 egg so the
// caller never sees a firmware with no pet in it.
// -----------------------------------------------------------------------------
static bool run_wipe(void)
{
  const bool ok = gs_factory_reset();
  Genome g = genome_genesis();
  sim_new_pet(g, gt_now(), 0);
  // GAME_DESIGN 10.2: everything god mode produces carries the taint, and this
  // egg was produced inside god mode. A factory reset resets the save, not the
  // honesty of the dynasty ribbon.
  sim_god_set_genome(g);
  const SimView* p = pet();
  if (p) gs_save_active(true);
  GOD_LOGF("[god] wipe ok=%u\n", (unsigned)ok);
  return ok;
}

// -----------------------------------------------------------------------------
// Command 7: install a genome on the living pet. sim_god_set_genome() reseals
// it and forces the taint bit, so nothing here can produce a clean genome.
// -----------------------------------------------------------------------------
static void install_genome(const Genome& g)
{
  sim_god_set_genome(g);
  const SimView* p = pet();
  if (p) genome_to_hex32(p->genome, s_hex_show);
  changed();
  toast((uint16_t)STR_GOD_DONE);
}

// =============================================================================
//  7. THE SERIAL COMMAND LAYER (P10-C1, spec sections 49 and 66)
//
//  ONE entry point, called from the single reader above the GOD_MODE_ENABLED
//  guard. It returns true when it consumed the line, false to let the
//  always-compiled read-only surface have it.
//
//  Everything with a right answer already happened in dev/diag_core.cpp before
//  this function sees a command id: the parse, the range checks, the taint, the
//  Box and bag mutations. What is left here is the four things a pure module
//  cannot do - move the clock, push a screen, start the radio, write flash -
//  and the phase log says plainly that THIS function is covered by no host
//  test, because dev/godmode.cpp includes Arduino.h and no host binary
//  compiles it.
// =============================================================================

// The one-deep navigation latch (godmode.h). A typed line cannot return a
// GodEvt the way a gesture can, so it leaves one here and ui.cpp drains it.
static uint8_t s_pending_evt = (uint8_t)GOD_EVT_NONE;
static uint8_t s_pending_arg = 0;

static void latch_evt(GodEvt e, uint8_t arg)
{
  s_pending_evt = (uint8_t)e;
  s_pending_arg = arg;
}

GodEvt god_take_evt(uint8_t& arg)
{
  const GodEvt e = (GodEvt)s_pending_evt;
  if (e == GOD_EVT_NONE) return GOD_EVT_NONE;
  arg = s_pending_arg;
  s_pending_evt = (uint8_t)GOD_EVT_NONE;
  s_pending_arg = 0;
  return e;
}

static int32_t shell_skew_s(void) { return s_skew_total; }

// Print a sink. It lives BELOW the guard because shell_dev_line() is its only
// caller: the always-compiled surface prints line by line with its own small
// buffer, so a release build that carried this would carry a function nobody
// calls - which -Wunused-function says out loud, and which is the honest
// signal that a piece of the console leaked into the artefact.
static void shell_print(const DiagOut& o)
{
  if (!o.buf) return;
  Serial.print(o.buf);
  if (o.truncated) Serial.print(F("...[truncated]\r\n"));
}

// Persist what diag_exec() said it touched. The pure layer writes no flash -
// game/box.h's rule is that persistence decides when a mutated blob lands - so
// it reports and this commits.
static void shell_commit(uint8_t commit)
{
  if (commit & DGCOMMIT_SLOTS) {
    for (uint8_t sl = 0; sl < (uint8_t)BOX_SLOTS; ++sl) {
      if (box_occupied(sl)) (void)gs_save_slot(sl, true);
    }
  }
  if (commit & DGCOMMIT_ACTIVE) (void)gs_save_active(true);
  if (commit & DGCOMMIT_BOX)    (void)gs_save_box();
  if (commit & DGCOMMIT_INV)    (void)save_inventory(gs_state().inv);
  if (commit & DGCOMMIT_CDS)    (void)save_cooldowns(gs_state().cds);
}

// -----------------------------------------------------------------------------
// set_time <epoch>. IT MOVES THE SKEW, NEVER THE SYSTEM CLOCK, and the reason
// is written at god_exit() thirty lines from here: every timestamp the console
// wrote into SimView is in skewed time, so a gt_set_epoch() that jumped the
// wall clock could leave last_seen_epoch in the FUTURE and underflow the next
// absence computation. This is the same door SALTAR AUSENCIA uses.
//
// A BACKWARD set_time is refused rather than quietly clamped, for the same
// reason: there is no honest way to un-live an hour the simulation has already
// charged for.
// -----------------------------------------------------------------------------
static void run_set_time(uint32_t target, DiagOut& o)
{
  const uint32_t now = gt_now();
  if (target <= now) {
    diag_puts(o, "set_time refused: ");
    diag_putu(o, target);
    diag_puts(o, " is not after now=");
    diag_putu(o, now);
    diag_puts(o, " (the skew only moves forward; see god_exit())\n");
    return;
  }
  const uint32_t delta = (uint32_t)(target - now);
  run_absence(delta);
  diag_puts(o, "set_time +");
  diag_putu(o, delta);
  diag_puts(o, "s -> now=");
  diag_putu(o, gt_now());
  diag_puts(o, " steps=");
  diag_putu(o, (uint32_t)s_abs_rep.steps);
  diag_nl(o);
}

// -----------------------------------------------------------------------------
// test_save_load (spec section 49). Three checks, and the middle one is the
// point: sealing a blob and finding it valid proves nothing on its own, because
// a seal that always said "ok" would pass too. So the command CORRUPTS its own
// copy by one byte and requires pebble_blob_ok() to refuse it. The mutation is
// built into the command instead of being something a person remembers to do.
//
// It does NOT read the blob back off flash. save_restore_checkpoint() is the
// only read path and it REBINDS save_manager to whatever GameState it is handed
// (save_manager.cpp:611-621), so a read-back into a scratch would leave the
// live state unbound. Said here rather than implied by a missing line.
// -----------------------------------------------------------------------------
static void run_save_load(DiagOut& o)
{
  const SimView* p = pet();
  if (!p) { diag_puts(o, "test_save_load: no pet\n"); return; }
  const uint8_t slot = box_active();
  const PebbleInstance* live = (slot < (uint8_t)BOX_SLOTS) ? box_peek(slot) : nullptr;
  if (!live) { diag_puts(o, "test_save_load: no active slot\n"); return; }

  PebbleInstance copy = *live;
  pebble_seal(copy);
  const bool sealed_ok = pebble_blob_ok(copy);

  PebbleInstance bad = copy;
  bad.level = (uint8_t)(bad.level ^ 0x01u);       // one bit, anywhere under the CRC
  const bool corrupt_refused = !pebble_blob_ok(bad);

  const uint16_t fails_before = kv_write_fails();
  const bool wrote  = gs_save_active(true);
  const bool landed = save_pebble_landed();
  const uint16_t fails_after = kv_write_fails();

  diag_puts(o, "test_save_load seal=");     diag_putu(o, sealed_ok ? 1u : 0u);
  diag_puts(o, " corrupt_refused=");        diag_putu(o, corrupt_refused ? 1u : 0u);
  diag_puts(o, " wrote=");                  diag_putu(o, wrote ? 1u : 0u);
  diag_puts(o, " landed=");                 diag_putu(o, landed ? 1u : 0u);
  diag_puts(o, " wfails ");                 diag_putu(o, fails_before);
  diag_puts(o, "->");                       diag_putu(o, fails_after);
  diag_puts(o, " nvs=");                    diag_puts(o, kv_healthy(KV_MAIN) ? "OK" : "BAD");
  diag_nl(o);
}

// The commands the pure layer handed back. Each one needs a device.
static void run_deferred(const DiagParse& pr, DiagOut& o)
{
  switch (pr.cmd) {
    case DGC_SET_TIME:
      run_set_time(pr.arg[0], o);
      return;

    case DGC_START_BATTLE:
      latch_evt(GOD_EVT_BATTLE, 0);
      diag_puts(o, "start_battle: pushing the battle screen on BT_DIAG_SEED\n");
      return;

    case DGC_START_CREATOR:
      latch_evt(GOD_EVT_CREATOR, 0);
      diag_puts(o, "start_creator: pushing the creator portal\n");
      return;

    case DGC_SCAN_WIFI:
      // The scan JOB is a ui/screen_network.cpp file-static and the radio has
      // exactly one owner, so the console asks for the screen rather than
      // starting a second scan behind its back.
      latch_evt(GOD_EVT_SCAN, 0);
      diag_puts(o, "scan_wifi: opening the network screen, which owns the job\n");
      return;

    case DGC_TEST_MINIGAME: {
      const uint8_t idx = (pr.argc >= 1) ? (uint8_t)pr.arg[0] : 0u;
      latch_evt(GOD_EVT_MINIGAME, idx);
      diag_puts(o, "test_minigame ");
      diag_putu(o, idx);
      diag_nl(o);
      return;
    }

    case DGC_TEST_SAVE_LOAD:
      run_save_load(o);
      return;

    default:
      // DGC_HELP / DGC_INFO / DGC_SHOW_SAVE / DGC_STALL are DCF_ALWAYS and were
      // filtered out before this function; reaching here is a table drift.
      diag_puts(o, "not runnable here\n");
      return;
  }
}

static bool shell_dev_line(const char* line)
{
  // 1. THE GENOME PASTE OWNS THE LINE while GENOMA / CARGAR is open. It was a
  //    second Serial.read() loop until P10-C1; now it is a line handler like
  //    everything else, and it strips the spaces the old reader skipped.
  if (s_console_page == GSC_HEX && s_hex_mode == GHX_LOAD) {
    char hex[33];
    uint8_t n = 0;
    for (const char* c = line; *c; ++c) {
      if (*c == ' ' || *c == '\t') continue;
      if (n >= 32u) { n = 33u; break; }               // too long: refuse, do not clip
      hex[n++] = *c;
    }
    if (n <= 32u) {
      hex[n] = '\0';
      Genome g;
      if (genome_from_hex32(hex, g)) {
        install_genome(g);
        s_console_page = GSC_GENOME;
        return true;
      }
    }
    toast((uint16_t)STR_ERR_GENOME);
    return true;
  }

  // 2. Everything else goes through the pure parser.
  DiagParse pr;
  diag_parse(line, pr);
  if (pr.err != DGP_OK) return false;                 // the always-surface names it
  const DiagCmdSpec* sp = diag_cmd_spec(pr.cmd);
  if (!sp) return false;
  if (sp->flags & DCF_ALWAYS) return false;           // help / info / show_save / stall

  // A MUTATING TYPED COMMAND *IS* ENTERING GOD MODE, and it says so on the
  // panel. Without this line a bench operator could `spawn 1` over a cable with
  // the console never opened, and the top GOD_BAR_H rows - the thing godmode.h
  // promises makes the state impossible to leave on by accident - would stay
  // down. god_enter() is idempotent, marks the RTC nonce, prints the CSV header
  // and taints the LIVING pet; the per-command diag_taint() marks what the
  // command itself creates or edits, which is a different and narrower claim.
  //
  // The read-only surface is deliberately NOT behind this: reading a version
  // number is not a cheat, and making it one would mean a release board and a
  // dev board answered `info` differently for no reason.
  if (sp->flags & DCF_MUTATES) god_enter();

  DiagCtx ctx;
  ctx.gs        = &gs_state();
  ctx.now_epoch = gt_now();
  ctx.cal       = (uint8_t)gt_cal_state();
  ctx.have_sim  = (pet() != nullptr) ? 1u : 0u;

  char b[224];
  DiagOut o;
  diag_out_init(o, b, (uint16_t)sizeof(b));
  uint8_t commit = 0;
  const uint8_t r = diag_exec(pr, ctx, o, commit);

  if (r == (uint8_t)DGR_DEFER) {
    run_deferred(pr, o);
    shell_print(o);
    return true;
  }

  shell_print(o);
  if (r == (uint8_t)DGR_OK) {
    shell_commit(commit);
    // One CSV line after every state change, exactly as a gesture-driven
    // command produces: a soak log must not have holes where the operator
    // typed instead of pressed.
    if (commit) god_dump_line();
  } else if (r == (uint8_t)DGR_ERR_NO_PET) {
    Serial.printf("[diag] %s: no active Pebble\r\n", sp->name);
  }
  return true;
}

// =============================================================================
//  8. THE SOAK LOG
// =============================================================================
static void dump_header(void)
{
  Serial.println(F(
    "GOD#,epoch,stage,hun,hap,ene,hyg,hea,cq,gen,genome,"
    "age_s,bond,poop,sick,asleep,minor,"
    "alert,scale,clock_ok,skew_s,"
    "heap_free,heap_min,heap_maxalloc,radio,phase"));
}

void god_dump_line(void)
{
  const SimView* p = pet();
  if (!p) { Serial.println(F("GOD,0,,,,,,,,,")); return; }

  char hex[33];
  genome_to_hex32(p->genome, hex);

  // Columns 1..11 are the GAME_DESIGN 10.2 format, byte for byte.
  Serial.printf("GOD,%lu,%u,%u,%u,%u,%u,%u,%d,%u,%s",
                (unsigned long)sim_now(),
                (unsigned)p->stage,
                (unsigned)sim_stat_pct(ST_HUNGER),
                (unsigned)sim_stat_pct(ST_HAPPINESS),
                (unsigned)sim_stat_pct(ST_ENERGY),
                (unsigned)sim_stat_pct(ST_HYGIENE),
                (unsigned)sim_stat_pct(ST_HEALTH),
                (int)p->cq,
                (unsigned)p->genome.generation,
                hex);

  // Everything below is APPENDED, so an eleven-column parser keeps working.
  Serial.printf(",%lu,%u,%u,%u,%u,%u",
                (unsigned long)sim_age_s(),
                (unsigned)sim_stat_pct(ST_BOND),
                (unsigned)p->poop_count,
                (unsigned)sim_is_sick(),
                (unsigned)sim_is_asleep(),
                (unsigned)p->minor_form);

  Serial.printf(",%u,%lu,%u,%ld,%lu,%lu,%lu,%u,%u\n",
                (unsigned)sim_alert(),
                (unsigned long)god_time_scale(),
                (unsigned)(gt_is_valid() ? 1u : 0u),
                (long)s_skew_total,
                (unsigned long)ESP.getFreeHeap(),
                (unsigned long)ESP.getMinFreeHeap(),
                (unsigned long)ESP.getMaxAllocHeap(),
                (unsigned)net_mode(),
                (unsigned)net_phase());
}

// =============================================================================
//  9. LIFECYCLE
// =============================================================================
void god_begin(void)
{
  s_active      = false;
  s_console_page      = GSC_MENU;
  s_menu_cur    = 0;
  s_sub_cur     = 0;
  s_scale_idx   = 0;
  s_pick        = 0;
  s_gene_idx    = 0;
  s_sys_page    = 0;
  s_hex_mode    = GHX_DUMP;
  s_confirm_act = GCF_NONE;
  s_confirm_yes = false;
  s_toast_str   = (uint16_t)STR_EMPTY;
  s_toast_until = 0;
  s_dump_on     = true;
  s_dump_last   = millis();
  s_frozen      = false;
  s_skew_total  = 0;
  s_abs_done    = false;
  s_hex_show[0] = '\0';
  s_pending_evt = (uint8_t)GOD_EVT_NONE;
  s_pending_arg = 0;

  s_fh_moves    = 0;
  s_fh_worst    = 0;
  s_fh_armed    = false;

  sim_set_time_scale(1u);
  heap_trend_begin();
  shell_line_reset();

  if (boot_god_tainted()) {
    // The RTC nonce survived a soft reset that happened inside god mode. Do NOT
    // silently resume acceleration - say so and start at x1.
    GOD_LOGF("[god] previous boot was tainted\n");
  }
}

void god_enter(void)
{
  if (s_active) return;
  s_active   = true;
  s_console_page   = GSC_MENU;
  s_menu_cur = 0;
  s_sub_cur  = 0;
  s_abs_done = false;
  s_dump_last = millis();

  boot_mark_god();
  set_scale(0);                                  // acceleration is opt-in

  // The taint is permanent, on the living pet and on everything it produces.
  const SimView* p = pet();
  if (p) {
    Genome g = p->genome;
    gene_set_tainted(g, 1);
    sim_god_set_genome(g);
    genome_to_hex32(g, s_hex_show);
  }

  GOD_LOGF("[god] ENTER (tainted forever)\n");
  dump_header();
  changed();
  toast((uint16_t)STR_GOD_TAINTED);
}

void god_exit(void)
{
  if (!s_active) return;

  // The accumulated skew is deliberately NOT undone. gametime.h offers the
  // undo, but every timestamp the forced absences wrote into SimView is in
  // skewed time; rewinding gt_now() would leave last_seen_epoch in the future
  // and the next absence computation would underflow. The skew is reported on
  // the RELOJ panel and in the CSV instead, so a soak log stays interpretable.
  // (It is RAM-only: a reboot does drop it, and the pet then looks like it was
  //  saved in the future until the next real absence catches up. Known, and the
  //  reason the wipe command exists.)
  set_scale(0);
  s_frozen = false;
  s_active   = false;
  s_console_page   = GSC_MENU;

  const SimView* p = pet();
  if (p) gs_save_active(true);
  god_dump_line();
  GOD_LOGF("[god] EXIT %s\n", S(STR_GOD_EXIT));
}

bool     god_active(void)       { return s_active; }
bool     god_dump_enabled(void) { return s_dump_on; }
uint32_t god_time_scale(void)   { return s_active ? GD_SCALES[s_scale_idx] : 1u; }

bool god_freeze_clock(uint8_t& hour, uint8_t& minute)
{
  if (!s_active || !s_frozen) return false;
  hour   = s_frozen_h;
  minute = s_frozen_m;
  return true;
}

uint8_t god_entry_progress(uint8_t screen_id)
{
  if (screen_id != (uint8_t)SCR_STATUS_B) return 0;

  const uint32_t l = input_hold_ms(INPUT_BTN_L);
  const uint32_t r = input_hold_ms(INPUT_BTN_R);
  if (l == 0 || r == 0) return 0;

  const uint32_t held = (l < r) ? l : r;
  if (held >= (uint32_t)GOD_ENTER_HOLD_MS) {
    // The same hold RE-OPENS the console when god mode is already on. Without
    // this the user who left SCR_DIAG with GOD_EVT_LEAVE to watch an accelerated
    // life would be stranded: the only off switch is the console's exit row.
    if (!s_active) god_enter();
    else           s_console_page = GSC_MENU;
    input_flush();          // the eventual release must not fire on SCR_DIAG
    return 100;
  }
  return (uint8_t)((held * 100UL) / (uint32_t)GOD_ENTER_HOLD_MS);
}

void god_service(void)
{
  // The serial console is not gated on god mode being ON - a stall and a field
  // list have to be reachable from a plain terminal on a freshly flashed board.
  // UNTIL P10-C1 THAT SENTENCE WAS TRUE OF THIS BUILD AND FALSE OF THE ONE THAT
  // SHIPS, because the reader it described sat below the #if: a release board
  // never called Serial.available() at all. The reader now lives above the
  // guard and BOTH god_service() bodies call it, which is the shape
  // heap_trend_service() has had since P2-C12 and the shape the phase-6 defect
  // taught (a default that lived in a dev-only function).
  shell_line_service();
  heap_trend_service();

  if (!s_active) return;

  const uint32_t now = millis();

  if (s_dump_on && (uint32_t)(now - s_dump_last) >= (uint32_t)GOD_DUMP_PERIOD_MS) {
    s_dump_last = now;
    god_dump_line();
  }
}

// =============================================================================
//  10. INPUT
// =============================================================================

// Commit a GSC_CONFIRM "YES". Returns what ui must do next.
static GodEvt commit_confirm(void)
{
  const uint8_t act = s_confirm_act;
  s_confirm_act = GCF_NONE;

  switch (act) {
    case GCF_WIPE1:
      open_confirm((uint16_t)STR_CF_WIPE2, GCF_WIPE2);
      return GOD_EVT_NONE;

    case GCF_WIPE2: {
      const bool ok = run_wipe();
      s_console_page = GSC_MENU;
      toast((uint16_t)(ok ? STR_GOD_DONE : STR_ERR_NVS));
      return GOD_EVT_WIPED;
    }
    default:
      s_console_page = GSC_MENU;
      return GOD_EVT_NONE;
  }
}

// The root list: open the sub-screen for the selected command.
static GodEvt open_command(uint8_t row)
{
  s_sub_cur  = 0;
  s_abs_done = false;

  switch (row) {
    case  0: s_console_page = GSC_SPEED;    s_sub_cur = s_scale_idx; break;
    case  1: s_console_page = GSC_ABSENCE;  break;
    case  2: s_console_page = GSC_STATPICK; break;
    case  3: s_console_page = GSC_STAGE;    break;
    case  4: s_console_page = GSC_GENOME;   break;
    case  5: s_console_page = GSC_SYS;      s_sys_page = GD_SYS_CLOCK; break;
    case  6: open_confirm((uint16_t)STR_CF_WIPE, GCF_WIPE1); break;
    // test_battle. The console cannot navigate - ui.cpp owns that - so this is
    // the whole of what it does: say so and let the caller push SCR_BATTLE on
    // top. god mode stays ON and stays visible underneath.
    case GD_ROW_BATTLE: return GOD_EVT_BATTLE;
    default:
      god_exit();
      return GOD_EVT_LEAVE;
  }
  return GOD_EVT_NONE;
}

// How many rows the current sub-screen has.
static uint8_t sub_count(void)
{
  switch (s_console_page) {
    case GSC_SPEED:    return (uint8_t)GOD_SCALE_COUNT;
    case GSC_ABSENCE:  return (uint8_t)GOD_ABSENCE_COUNT;
    case GSC_STATPICK: return (uint8_t)ST_COUNT;
    case GSC_STATVAL:
      return (uint8_t)GOD_STATVAL_COUNT;
    case GSC_STAGE:    return (uint8_t)STAGE_COUNT;
    case GSC_GENOME:   return GD_GEN_ROWS;
    case GSC_GENE:     return (uint8_t)GOD_GENE_COUNT;
    default:           return 0;
  }
}

// TAP_R / select inside a sub-screen.
static GodEvt select_sub(void)
{
  switch (s_console_page) {
    case GSC_SPEED:
      set_scale(s_sub_cur);
      toast((uint16_t)STR_GOD_DONE);
      return GOD_EVT_NONE;

    case GSC_ABSENCE:
      run_absence(GD_ABSENCES[s_sub_cur % GOD_ABSENCE_COUNT]);
      return GOD_EVT_NONE;

    case GSC_STATPICK:
      s_pick    = s_sub_cur;
      s_sub_cur = 0;
      s_console_page  = GSC_STATVAL;
      return GOD_EVT_NONE;

    case GSC_STATVAL:
      sim_god_set_stat((StatId)s_pick, GD_STATVALS[s_sub_cur % GOD_STATVAL_COUNT]);
      changed();
      toast((uint16_t)STR_GOD_DONE);
      return GOD_EVT_NONE;

    case GSC_STAGE:
      sim_god_set_stage(s_sub_cur);
      changed();
      toast((uint16_t)STR_GOD_DONE);
      return GOD_EVT_NONE;

    case GSC_GENOME:
      switch (s_sub_cur) {
        case GD_GEN_RANDOM: {
          Genome g = genome_genesis();
          install_genome(g);
          break;
        }
        case GD_GEN_EDIT:
          s_gene_idx = 0;
          s_console_page   = GSC_GENE;
          break;
        case GD_GEN_DUMP: {
          const SimView* p = pet();
          if (p) {
            genome_to_hex32(p->genome, s_hex_show);
            GOD_LOGF("GENOME,%s\n", s_hex_show);
          }
          s_hex_mode = GHX_DUMP;
          s_console_page   = GSC_HEX;
          break;
        }
        default:
          s_hex_mode = GHX_LOAD;
          shell_line_reset();          // the shared reader owns the buffer now
          s_console_page   = GSC_HEX;
          GOD_LOGF("[god] %s\n", S(STR_GOD_PASTE));
          break;
      }
      return GOD_EVT_NONE;

    case GSC_GENE: {
      const SimView* p = pet();
      if (!p) { toast((uint16_t)STR_GOD_NOPET); return GOD_EVT_NONE; }
      const GodGene& gg = GD_GENES[s_gene_idx % GOD_GENE_COUNT];
      Genome g = p->genome;
      uint8_t v = gg.get(g);
      v = (uint8_t)((v >= gg.vmax) ? 0u : (v + 1u));
      gg.set(g, v);
      install_genome(g);
      return GOD_EVT_NONE;
    }

    case GSC_HEX:
      if (s_hex_mode == GHX_DUMP && s_hex_show[0]) GOD_LOGF("GENOME,%s\n", s_hex_show);
      return GOD_EVT_NONE;

    default:
      return GOD_EVT_NONE;
  }
}

GodEvt god_handle(Gesture g)
{
  if (!s_active) return GOD_EVT_NONE;

  // GAME_DESIGN 8.2 invariant 2: HOME from anywhere. God mode STAYS ON - the
  // marker bar guarantees the user knows - and the exit row turns it off.
  if (g == GST_LONG_BOTH) { s_console_page = GSC_MENU; return GOD_EVT_LEAVE; }

  // ---- the shared confirm modal -------------------------------------------
  if (s_console_page == GSC_CONFIRM) {
    switch (g) {
      case GST_TAP_L:  s_confirm_yes = !s_confirm_yes;      return GOD_EVT_NONE;
      case GST_TAP_R:  if (s_confirm_yes) return commit_confirm();
                       s_confirm_act = GCF_NONE; s_console_page = GSC_MENU;
                       return GOD_EVT_NONE;
      case GST_HOLD_R: s_confirm_act = GCF_NONE; s_console_page = GSC_MENU;
                       return GOD_EVT_NONE;
      default:         return GOD_EVT_NONE;
    }
  }

  // ---- the root list -------------------------------------------------------
  if (s_console_page == GSC_MENU) {
    switch (g) {
      case GST_TAP_L:
      case GST_HOLD_L: list_step(s_menu_cur, (uint8_t)GOD_MENU_ROWS, +1); return GOD_EVT_NONE;
      case GST_TAP_R:  return open_command(s_menu_cur);
      case GST_BOTH:   s_dump_on = !s_dump_on;
                       toast((uint16_t)(s_dump_on ? STR_GOD_DUMP_ON : STR_GOD_DUMP_OFF));
                       if (s_dump_on) god_dump_line();
                       return GOD_EVT_NONE;
      case GST_HOLD_R: return GOD_EVT_LEAVE;      // leave the screen, not the mode
      default:         return GOD_EVT_NONE;
    }
  }

  // ---- every sub-screen ----------------------------------------------------
  switch (g) {
    case GST_HOLD_R:
      if (s_console_page == GSC_STATVAL) { s_console_page = GSC_STATPICK; s_sub_cur = s_pick; }
      else if (s_console_page == GSC_GENE || s_console_page == GSC_HEX) { s_console_page = GSC_GENOME; s_sub_cur = 0; }
      else                       { s_console_page = GSC_MENU; }
      return GOD_EVT_NONE;

    case GST_TAP_L:
    case GST_HOLD_L:
      if (s_console_page == GSC_SYS)      { s_sys_page = (uint8_t)((s_sys_page + 1u) % GD_SYS_PAGES); }
      else if (s_console_page == GSC_GENE){ list_step(s_gene_idx, (uint8_t)GOD_GENE_COUNT, +1); }
      else                          { list_step(s_sub_cur, sub_count(), +1); }
      return GOD_EVT_NONE;

    // The gene editor's DECREMENT lived on GST_DBL_R and went with the double
    // tap (P3-C4a). TAP_R still increments and it WRAPS at vmax, so every value
    // is still reachable - at worst vmax presses instead of one. This is the
    // cheapest of the shortcuts the double tap took with it: the console is a
    // dev tool behind a five-second hold.
    case GST_TAP_R:
      return select_sub();

    default:
      return GOD_EVT_NONE;
  }
}

// =============================================================================
//  11. DRAWING
// =============================================================================
static void draw_row(int16_t y0, uint8_t row, bool sel, const char* label, const char* value)
{
  const int16_t top  = (int16_t)(y0 + (int16_t)row * GD_ROW_H);
  const int16_t base = (int16_t)(top + 7);
  if (label) rd_text_fit(3, base, 96, RD_FONT_BODY, label);
  if (value && value[0]) rd_text_right(OLED_W - 5, base, RD_FONT_BODY, value);
  if (sel) rd_invert_rect(0, top, OLED_W, GD_ROW_H);   // XOR last: black on white
}

static void draw_scrollbar(int16_t y0, uint8_t rows, uint8_t count, uint8_t top)
{
  if (count <= rows) return;
  const int16_t h = (int16_t)(rows * GD_ROW_H);
  U8G2& u = rd_u8g2();
  u.setDrawColor(1);
  int16_t th = (int16_t)((int32_t)h * rows / count);
  if (th < 3) th = 3;
  const int16_t ty = (int16_t)(y0 + (int32_t)(h - th) * top / (count - rows));
  u.drawBox(GD_SCROLL_X, ty, 2, th);
}

static void draw_title(const char* s)
{
  rd_text_fit(2, GD_TITLE_BASE, OLED_W - 4, RD_FONT_BODY, s);
  U8G2& u = rd_u8g2();
  u.setDrawColor(1);
  u.drawHLine(0, GD_SEP_Y, OLED_W);
}

static void draw_toast(void)
{
  if (s_toast_str == (uint16_t)STR_EMPTY) return;
  if ((int32_t)(millis() - s_toast_until) >= 0) { s_toast_str = (uint16_t)STR_EMPTY; return; }

  U8G2& u = rd_u8g2();
  u.setDrawColor(0);
  u.drawBox(0, 44, OLED_W, 11);
  u.setDrawColor(1);
  u.drawFrame(0, 44, OLED_W, 11);
  rd_text_fit(3, 52, OLED_W - 6, RD_FONT_BODY, S(s_toast_str));
}

void god_draw_marker(void)
{
  if (!s_active) return;
  frame_heap_sample();

  U8G2& u = rd_u8g2();
  u.setDrawColor(1);
  u.drawBox(0, 0, OLED_W, GOD_BAR_H);
  u.setDrawColor(0);                       // solid font mode -> black on white

  char b[24];
  snprintf(b, sizeof(b), "GOD x%lu", (unsigned long)god_time_scale());
  rd_text(2, GOD_BAR_H - 2, RD_FONT_BODY, b);

  // ASCII only, so the _tr tiny font is legal here (BRIEF 1.5).
  snprintf(b, sizeof(b), "%luk%s", (unsigned long)(ESP.getFreeHeap() >> 10),
           s_frozen ? " F" : "");
  rd_text_right(OLED_W - 2, GOD_BAR_H - 2, RD_FONT_TINY, b);

  u.setDrawColor(1);
}

// -----------------------------------------------------------------------------
// Per-screen bodies
// -----------------------------------------------------------------------------
static void draw_menu(void)
{
  const uint8_t top = win_top(s_menu_cur, (uint8_t)GOD_MENU_ROWS, GD_ROOT_ROWS);
  char val[16];

  for (uint8_t r = 0; r < GD_ROOT_ROWS; ++r) {
    const uint8_t i = (uint8_t)(top + r);
    if (i >= (uint8_t)GOD_MENU_ROWS) break;
    val[0] = '\0';
    // Only two rows carry a value readout. The indices are pinned above, so a
    // later renumbering of GD_MENU_STR breaks the build instead of quietly
    // printing the readout next to the wrong command.
    switch (i) {
      case GD_ROW_SPEED:
        snprintf(val, sizeof(val), "x%lu", (unsigned long)god_time_scale());
        break;
      case GD_ROW_CLOCK:
        snprintf(val, sizeof(val), "%s", gt_is_valid() ? "OK" : "??");
        break;
      default: break;
    }
    draw_row(GD_ROOT_Y0, r, (i == s_menu_cur), S(GD_MENU_STR[i]), val);
  }
  draw_scrollbar(GD_ROOT_Y0, GD_ROOT_ROWS, (uint8_t)GOD_MENU_ROWS, top);
  rd_affordance(S(STR_AF_NEXT), S(STR_AF_SEL));
}

static void draw_simple_list(const char* title, uint8_t count,
                             void (*label)(uint8_t, char*, size_t))
{
  draw_title(title);
  const uint8_t top = win_top(s_sub_cur, count, GD_SUB_ROWS);
  char b[26];
  for (uint8_t r = 0; r < GD_SUB_ROWS; ++r) {
    const uint8_t i = (uint8_t)(top + r);
    if (i >= count) break;
    b[0] = '\0';
    label(i, b, sizeof(b));
    draw_row(GD_SUB_Y0, r, (i == s_sub_cur), b, 0);
  }
  draw_scrollbar(GD_SUB_Y0, GD_SUB_ROWS, count, top);
  rd_affordance(S(STR_AF_NEXT), S(STR_AF_SEL));
}

static void lbl_speed(uint8_t i, char* b, size_t n)
{
  snprintf(b, n, "x%lu", (unsigned long)GD_SCALES[i % GOD_SCALE_COUNT]);
}
static void lbl_absence(uint8_t i, char* b, size_t n)
{
  char t[GT_ELAPSED_BUF];
  gt_format_elapsed(GD_ABSENCES[i % GOD_ABSENCE_COUNT], t, sizeof(t));
  snprintf(b, n, "%s", t);
}
static void lbl_statpick(uint8_t i, char* b, size_t n) { snprintf(b, n, "%s", S_STAT(i)); }
static void lbl_statval(uint8_t i, char* b, size_t n)
{
  snprintf(b, n, "%u %%", (unsigned)GD_STATVALS[i % GOD_STATVAL_COUNT]);
}
static void lbl_stage(uint8_t i, char* b, size_t n)   { snprintf(b, n, "%s", S_STAGE(i)); }
static void lbl_genome(uint8_t i, char* b, size_t n)
{
  static const uint16_t rows[GD_GEN_ROWS] = {
    (uint16_t)STR_GOD_RANDOM, (uint16_t)STR_GOD_EDIT,
    (uint16_t)STR_GOD_DUMP,   (uint16_t)STR_GOD_LOAD
  };
  snprintf(b, n, "%s", S(rows[i % GD_GEN_ROWS]));
}
static void draw_absence(void)
{
  if (!s_abs_done) { draw_simple_list(S(STR_GOD_ABSENCE), (uint8_t)GOD_ABSENCE_COUNT, lbl_absence); return; }

  draw_title(S(STR_GOD_ABSENCE));
  char t[GT_ELAPSED_BUF];
  char b[30];
  gt_format_elapsed(s_abs_rep.absence_s, t, sizeof(t));
  rd_text(3, 27, RD_FONT_BODY, t);
  snprintf(b, sizeof(b), "n=%u clock=%u", (unsigned)s_abs_rep.steps,
           (unsigned)s_abs_rep.clock_known);
  rd_text(3, 45, RD_FONT_TINY, b);
  rd_affordance(0, S(STR_AF_BACK));
}

static void draw_gene(void)
{
  const SimView* p = pet();
  draw_title(S(STR_GOD_GENOME));
  if (!p) { rd_text_fit(3, 32, OLED_W - 6, RD_FONT_BODY, S(STR_GOD_NOPET)); rd_affordance(0, S(STR_AF_BACK)); return; }

  const uint8_t top = win_top(s_gene_idx, (uint8_t)GOD_GENE_COUNT, GD_SUB_ROWS);
  char v[8];
  for (uint8_t r = 0; r < GD_SUB_ROWS; ++r) {
    const uint8_t i = (uint8_t)(top + r);
    if (i >= (uint8_t)GOD_GENE_COUNT) break;
    snprintf(v, sizeof(v), "%u", (unsigned)GD_GENES[i].get(p->genome));
    draw_row(GD_SUB_Y0, r, (i == s_gene_idx), S(GD_GENES[i].label), v);
  }
  draw_scrollbar(GD_SUB_Y0, GD_SUB_ROWS, (uint8_t)GOD_GENE_COUNT, top);
  rd_affordance(S(STR_AF_NEXT), S(STR_AF_MORE));
}

static void draw_hex(void)
{
  draw_title(S(s_hex_mode == GHX_DUMP ? STR_GOD_DUMP : STR_GOD_LOAD));

  char half[17];
  if (s_hex_show[0]) {
    memcpy(half, s_hex_show, 16); half[16] = '\0';
    rd_text_center_in(0, OLED_W, 28, RD_FONT_TINY, half);
    memcpy(half, s_hex_show + 16, 16); half[16] = '\0';
    rd_text_center_in(0, OLED_W, 36, RD_FONT_TINY, half);
  }

  if (s_hex_mode == GHX_LOAD) {
    rd_text_fit(2, 46, OLED_W - 4, RD_FONT_BODY, S(STR_GOD_PASTE));
    // The line the shared reader has accumulated so far. It was s_hex[] until
    // P10-C1 collapsed the two serial buffers into one.
    rd_text_fit(2, 54, OLED_W - 4, RD_FONT_TINY, s_line);
    rd_affordance(0, S(STR_AF_BACK));
  } else {
    rd_affordance(0, S(STR_AF_SEL));
  }
}

static void draw_sys(void)
{
  // 34 until P10-C1; the spec 49 field lines dev/diag_core.cpp formats are
  // longer than that and rd_text_fit() clips the PIXELS, not the bytes.
  char b[64];

  switch (s_sys_page) {
    case GD_SYS_CLOCK: {
      draw_title(S(STR_GOD_CLOCK));
      struct tm lt;
      memset(&lt, 0, sizeof(lt));
      (void)gt_local_tm(lt);      // always fills lt; the verdict is gt_cal_state()
      // Bounded operands: struct tm carries plain ints, so an unbounded %d (or
      // a signed modulo, whose result is -99..99 and becomes huge on the cast
      // to unsigned) makes GCC assume ten digits per field and fire
      // -Wformat-truncation. Cast FIRST, then take the modulo, so every
      // directive is provably 2-4 digits. Lossless for any real date.
      snprintf(b, sizeof(b), "%04u-%02u-%02u %02u:%02u:%02u",
               (unsigned)(lt.tm_year + 1900) % 10000u,
               (unsigned)(lt.tm_mon + 1)     % 100u,
               (unsigned)lt.tm_mday          % 100u,
               (unsigned)lt.tm_hour          % 100u,
               (unsigned)lt.tm_min           % 100u,
               (unsigned)lt.tm_sec           % 100u);
      rd_text(2, 27, RD_FONT_TINY, b);
      // Calibration state, not "did SNTP land": UNSET / EST(imated) / USER /
      // PHONE, in TimeCal order.
      static const char* const kCal[CAL_COUNT] = { "UNSET", "EST", "USER", "PHONE" };
      snprintf(b, sizeof(b), "now=%lu %s", (unsigned long)gt_now(),
               kCal[(uint8_t)gt_cal_state() < (uint8_t)CAL_COUNT
                      ? (uint8_t)gt_cal_state() : 0u]);
      rd_text(2, 35, RD_FONT_TINY, b);
      snprintf(b, sizeof(b), "seen=%lu", (unsigned long)save_last_seen());
      rd_text(2, 43, RD_FONT_TINY, b);
      snprintf(b, sizeof(b), "skew=%lds frz=%02u:%02u", (long)s_skew_total,
               (unsigned)s_frozen_h, (unsigned)s_frozen_m);
      rd_text(2, 51, RD_FONT_TINY, b);
      break;
    }
    case GD_SYS_PET: {
      draw_title(S(STR_MENU_STATUS));
      const SimView* p = pet();
      if (!p) { rd_text_fit(2, 32, OLED_W - 4, RD_FONT_BODY, S(STR_GOD_NOPET)); break; }
      snprintf(b, sizeof(b), "%u/%u/%u/%u/%u hp",
               (unsigned)sim_stat_pct(ST_HUNGER),   (unsigned)sim_stat_pct(ST_HAPPINESS),
               (unsigned)sim_stat_pct(ST_ENERGY),   (unsigned)sim_stat_pct(ST_HYGIENE),
               (unsigned)sim_stat_pct(ST_HEALTH));
      rd_text(2, 27, RD_FONT_TINY, b);
      char el[GT_ELAPSED_BUF];
      gt_format_elapsed(sim_age_s(), el, sizeof(el));
      snprintf(b, sizeof(b), "%u %s", (unsigned)p->stage, el);
      rd_text(2, 35, RD_FONT_TINY, b);
      snprintf(b, sizeof(b), "cq%d p%u s%u a%u", (int)p->cq,
               (unsigned)p->poop_count, (unsigned)sim_is_sick(),
               (unsigned)sim_alert());
      rd_text(2, 43, RD_FONT_TINY, b);
      rd_text_fit(2, 52, OLED_W - 4, RD_FONT_BODY, S_STAGE(p->stage % STAGE_COUNT));
      break;
    }
    case GD_SYS_HEAP: {
      draw_title(S(STR_GOD_HEAP));
      snprintf(b, sizeof(b), "free   %lu", (unsigned long)ESP.getFreeHeap());
      rd_text(2, 27, RD_FONT_TINY, b);
      snprintf(b, sizeof(b), "min    %lu", (unsigned long)ESP.getMinFreeHeap());
      rd_text(2, 35, RD_FONT_TINY, b);
      snprintf(b, sizeof(b), "maxblk %lu", (unsigned long)ESP.getMaxAllocHeap());
      rd_text(2, 43, RD_FONT_TINY, b);
      const NetHeapStats& h = net_heap_last();
      snprintf(b, sizeof(b), "%u>%u f%lu m%lu", (unsigned)h.from_mode, (unsigned)h.to_mode,
               (unsigned long)h.free_b, (unsigned long)h.max_alloc_b);
      rd_text(2, 51, RD_FONT_TINY, b);
      // THE PER-FRAME DELTA (plan P4-C4). "dF 0/0" is what a screen that
      // allocates nothing per frame reads - open test_battle, play a few rounds
      // and come back to this page to check it.
      snprintf(b, sizeof(b), "dF %u/%ld", (unsigned)god_frame_heap_moves(),
               (long)god_frame_heap_worst());
      rd_text_right(OLED_W - 2, 43, RD_FONT_TINY, b);
      break;
    }
    case GD_SYS_POWER: {
      draw_title(S(STR_GOD_POWER));
      // ACT/DIM/IDL/SLP, and the rung in force is bracketed. The four labels
      // are ASCII scaffolding for an operator, not user-facing prose.
      static const char* const kRung[PWR_STATE_COUNT] = { "ACT", "DIM", "IDL", "SLP" };
      const uint8_t st = pwr_state();
      snprintf(b, sizeof(b), "state  %s%s%s",
               st < (uint8_t)PWR_STATE_COUNT ? "[" : "",
               st < (uint8_t)PWR_STATE_COUNT ? kRung[st] : "?",
               st < (uint8_t)PWR_STATE_COUNT ? "]" : "");
      rd_text(2, 27, RD_FONT_TINY, b);
      // Loops per second per rung. A rung nobody has been in this run reads 0,
      // which is the honest answer and not a measurement of 0/s.
      snprintf(b, sizeof(b), "%u %u %u %u /s",
               (unsigned)pwr_loops_per_s((uint8_t)PWR_ACTIVE),
               (unsigned)pwr_loops_per_s((uint8_t)PWR_DIM),
               (unsigned)pwr_loops_per_s((uint8_t)PWR_IDLE),
               (unsigned)pwr_loops_per_s((uint8_t)PWR_SLEEP));
      rd_text(2, 35, RD_FONT_TINY, b);
      snprintf(b, sizeof(b), "slept  %lus", (unsigned long)(pwr_slept_ms() / 1000UL));
      rd_text(2, 43, RD_FONT_TINY, b);
      // THE DROPPED-TICK COUNTER (P7-C6). A gap wider than NT_TICK_MAX_OWED_S
      // is a stall, not a sleep: it is resynchronised away and one second is
      // charged. Both halves are here because "how often" without "how much"
      // cannot tell a hiccup from a device that stops for minutes. A healthy
      // board reads 0 0 for its whole run - the ladder cannot make one
      // (PWR_SLEEP_SLICE_MS 8,000 < 16,000, static_asserted in app.cpp).
      snprintf(b, sizeof(b), "stall  %u x %lus", (unsigned)pwr_tick_stalls(),
               (unsigned long)pwr_tick_lost_s());
      rd_text(2, 51, RD_FONT_TINY, b);
      // The pin fact this whole design turns on, on the screen rather than only
      // in a header: LIGHT sleep, both buttons, because PIN_BTN_L cannot wake
      // the chip from deep sleep on this map.
      snprintf(b, sizeof(b), "wake L%u R%u light",
               (unsigned)PIN_BTN_L, (unsigned)PIN_BTN_R);
      rd_text(2, 59, RD_FONT_TINY, b);
      break;
    }
    case GD_SYS_RADIO: {
      draw_title(S(STR_GOD_RADIO));
      snprintf(b, sizeof(b), "mode=%u phase=%u err=%u", (unsigned)net_mode(),
               (unsigned)net_phase(), (unsigned)net_last_err());
      rd_text(2, 27, RD_FONT_TINY, b);
      snprintf(b, sizeof(b), "ip %s", net_ip());
      rd_text(2, 35, RD_FONT_TINY, b);
      // No rssi line: net_rssi() was station-only and went with the station
      // (P5-C1). Per-access-point signal strength lives in ScanResult.rssi,
      // which the NETWORK screen reads, not here.
      // Was `ble n/32`, the BLE init/deinit session counter, until P8-C0
      // deleted BLE. The row it replaces is the one a soak log wants here:
      // whether the peer link is actually up.
      snprintf(b, sizeof(b), "espnow %s",
               (net_phase() == NPH_LINK) ? "up" : "-");
      rd_text(2, 43, RD_FONT_TINY, b);
      break;
    }
    // NAMED, not `default:`. Until P10-C1 the STORE page was served by the
    // default arm, so adding a seventh page drew STORE under a new title and
    // nothing said so. The default arm below is now the compile-time-unreachable
    // one and it SAYS which page is missing instead of impersonating another.
    case GD_SYS_STORE: {
      draw_title(S(STR_GOD_STORE));
      snprintf(b, sizeof(b), "nvs %s err=%02X", kv_healthy(KV_MAIN) ? "OK" : "BAD",
               (unsigned)kv_error());
      rd_text(2, 27, RD_FONT_TINY, b);
      snprintf(b, sizeof(b), "boot=%u rst=%u n=%lu", (unsigned)boot_kind(),
               (unsigned)boot_reset_reason(), (unsigned long)boot_count());
      rd_text(2, 35, RD_FONT_TINY, b);
      snprintf(b, sizeof(b), "fails=%u rtc=%u",
               (unsigned)kv_write_fails(), (unsigned)(boot_rtc_intact() ? 1u : 0u));
      rd_text(2, 43, RD_FONT_TINY, b);
      snprintf(b, sizeof(b), "%s  %s", FW_VERSION, s_dump_on ? "LOG" : "---");
      rd_text(2, 51, RD_FONT_TINY, b);
      break;
    }
    // -----------------------------------------------------------------------
    // SPEC SECTION 49, THE FIELD LIST. Four rows of 128 px cannot hold twelve
    // fields, so this page carries the FIVE that had no reader anywhere before
    // this phase - build, save version, Pebble count, last scan, protocol -
    // and the serial `info` command prints all twelve. The formatting is
    // dev/diag_core.cpp's and tests/test_diag.cpp drives it; this arm only
    // places the strings.
    // -----------------------------------------------------------------------
    case GD_SYS_INFO: {
      draw_title(S(STR_GOD_INFO));
      DiagFields f;
      shell_fill_fields(f);
      DiagOut o;

      diag_out_init(o, b, (uint16_t)sizeof(b));
      diag_fmt_field((uint8_t)DGD_BUILD, f, o);
      rd_text_fit(2, 27, OLED_W - 4, RD_FONT_TINY, b);

      diag_out_init(o, b, (uint16_t)sizeof(b));
      diag_fmt_field((uint8_t)DGD_SAVE_VER, f, o);
      rd_text_fit(2, 35, OLED_W - 4, RD_FONT_TINY, b);

      diag_out_init(o, b, (uint16_t)sizeof(b));
      diag_fmt_field((uint8_t)DGD_LAST_SCAN, f, o);
      rd_text_fit(2, 43, OLED_W - 4, RD_FONT_TINY, b);

      snprintf(b, sizeof(b), "pebbles %u/%u  proto %u  batt n/a",
               (unsigned)f.pebbles, (unsigned)f.box_cap,
               (unsigned)PROTOCOL_VERSION);
      rd_text_fit(2, 51, OLED_W - 4, RD_FONT_TINY, b);
      break;
    }
    case GD_SYS_PERF: {
      // P10-C2. THE FOUR NUMBERS SPEC SECTION 46 ASKS FOR THAT A DEVICE CAN
      // ACTUALLY PRODUCE, and not one of them is checked by any host test or by
      // tools/check.sh: no host binary compiles ui/render.cpp or app/app.cpp,
      // micros() does not exist there, and the ~24 ms sendBuffer() that
      // dominates a frame is a property of a bus and a panel that are absent.
      // The acceptance procedure is docs/bench.md, steps A1 and A2.
      draw_title(S(STR_GOD_PERF));
      const uint8_t scr = perf_screen();
      // This screen: the live frame through rd_frame_time_us() - which had no
      // reader anywhere in the tree until this line - and the maximum this
      // screen has ever reached, which is the figure the bench walk reads.
      snprintf(b, sizeof(b), "f now %lu max %u%s",
               (unsigned long)rd_frame_time_us(),
               (unsigned)perf_frame_max_us(scr),
               perf_frame_saturated() ? "!" : "");
      rd_text(2, 27, RD_FONT_TINY, b);
      snprintf(b, sizeof(b), "f worst %lu s%u over %u",
               (unsigned long)perf_frame_worst_us(),
               (unsigned)perf_frame_worst_screen(),
               (unsigned)perf_frame_overruns());
      rd_text(2, 35, RD_FONT_TINY, b);
      // The pass is the WORK of one app_loop(), stamped above stage 7's yield.
      // A deliberate nap is not a slow pass and is not counted here.
      snprintf(b, sizeof(b), "p worst %lu s%u over %u",
               (unsigned long)perf_pass_worst_us(),
               (unsigned)perf_pass_worst_screen(),
               (unsigned)perf_pass_overruns());
      rd_text(2, 43, RD_FONT_TINY, b);
      snprintf(b, sizeof(b), "n %lu/%lu drop %u",
               (unsigned long)perf_frames(), (unsigned long)perf_passes(),
               (unsigned)perf_discards());
      rd_text(2, 51, RD_FONT_TINY, b);
      break;
    }
    default:
      // Unreachable while GD_SYS_PAGES matches the arms above. It says the page
      // number rather than drawing somebody else's panel.
      draw_title(S(STR_GOD_INFO));
      snprintf(b, sizeof(b), "no page %u", (unsigned)s_sys_page);
      rd_text(2, 32, RD_FONT_BODY, b);
      break;
  }
  rd_affordance(S(STR_AF_NEXT), 0);
}

static void draw_confirm(void)
{
  U8G2& u = rd_u8g2();
  u.setDrawColor(1);
  u.drawFrame(2, 12, OLED_W - 4, 40);
  rd_text_wrap(6, 22, OLED_W - 12, 9, 2, RD_FONT_BODY, S(s_confirm_str));

  const int16_t yes_x = 20, no_x = 74, box_w = 34, box_y = 40, box_h = 11;
  rd_text_center_in(yes_x, box_w, box_y + 8, RD_FONT_BODY, S(STR_YES));
  rd_text_center_in(no_x,  box_w, box_y + 8, RD_FONT_BODY, S(STR_NO));
  rd_invert_rect(s_confirm_yes ? yes_x : no_x, box_y, box_w, box_h);

  rd_affordance(S(STR_AF_NEXT), S(STR_AF_OK));
}

void god_draw(void)
{
  if (!s_active) return;

  switch (s_console_page) {
    case GSC_MENU:     draw_menu(); break;
    case GSC_SPEED:    draw_simple_list(S(STR_GOD_SPEED),   (uint8_t)GOD_SCALE_COUNT,  lbl_speed);    break;
    case GSC_ABSENCE:  draw_absence(); break;
    case GSC_STATPICK: draw_simple_list(S(STR_GOD_SETSTAT), (uint8_t)ST_COUNT,         lbl_statpick); break;
    case GSC_STATVAL:  draw_simple_list(S(STR_GOD_SETSTAT), sub_count(),               lbl_statval);  break;
    case GSC_STAGE:    draw_simple_list(S(STR_GOD_STAGE),   (uint8_t)STAGE_COUNT,      lbl_stage);    break;
    case GSC_GENOME:   draw_simple_list(S(STR_GOD_GENOME),  GD_GEN_ROWS,               lbl_genome);   break;
    case GSC_GENE:     draw_gene(); break;
    case GSC_HEX:      draw_hex(); break;
    case GSC_SYS:      draw_sys(); break;
    case GSC_CONFIRM:  draw_confirm(); break;
    default:           draw_menu(); break;
  }

  god_draw_marker();     // always last: nothing may cover the top nine rows
  draw_toast();
}

#else  // ---------------------------------------------------------------------
// GOD_MODE_ENABLED == 0. The API still links so no caller needs an #if, and
// none of the console reaches flash.
// -----------------------------------------------------------------------------
bool     god_active(void)                       { return false; }
GodEvt   god_handle(Gesture g)                  { (void)g; return GOD_EVT_NONE; }
void     god_draw(void)                         { }
uint32_t god_time_scale(void)                   { return 1u; }
void     god_dump_line(void)                    { }
// BOTH of these call the same always-compiled helpers the GOD_MODE_ENABLED 1
// bodies do. That is the whole structural lesson of the phase-6 defect, where
// sim_set_time_scale()'s only callers were below the #if and the shipping build
// ran sim_tick(0) for four phases: what the release artefact must still DO goes
// above the guard, and both bodies call out to it.
void     god_begin(void)                        { heap_trend_begin(); shell_line_reset(); }
void     god_service(void)                      { shell_line_service(); heap_trend_service(); }
uint8_t  god_entry_progress(uint8_t screen_id)  { (void)screen_id; return 0; }
void     god_enter(void)                        { }
void     god_exit(void)                         { }
void     god_draw_marker(void)                  { }
bool     god_freeze_clock(uint8_t& h, uint8_t& m) { (void)h; (void)m; return false; }
bool     god_dump_enabled(void)                 { return false; }
uint16_t god_frame_heap_moves(void)             { return 0; }
int32_t  god_frame_heap_worst(void)             { return 0; }
// P10-C1. There is no console to raise a navigation event and no forced
// absence to accumulate a skew, so both are inert - INERT, not a lie: a stub
// that answered anything else would make a caller act on a console that is not
// there. (god_note_load()/god_load_result() are NOT stubbed: they live above
// the guard because the SHIPPING build's `info` prints the boot LoadResult.)
GodEvt   god_take_evt(uint8_t& arg)              { (void)arg; return GOD_EVT_NONE; }

// The two functions the always-compiled reader forward-declares. This build has
// no console, so no typed line is ever consumed by one and the read-only
// surface above gets every line.
static bool    shell_dev_line(const char* line)  { (void)line; return false; }
static int32_t shell_skew_s(void)                { return 0; }

#endif // GOD_MODE_ENABLED
