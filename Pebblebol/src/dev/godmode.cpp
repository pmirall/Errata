// =============================================================================
//  NOTTAMAGOCHI - dev/godmode.cpp
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

#if GOD_MODE_ENABLED

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
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
  (uint16_t)STR_GOD_CLOCK,    (uint16_t)STR_GOD_WIPE,    (uint16_t)STR_AF_QUIT
};

// The two root rows draw_menu() prints a live value next to.
enum : uint8_t { GD_ROW_SPEED = 0, GD_ROW_CLOCK = 5 };
static_assert(GD_MENU_STR[GD_ROW_SPEED] == (uint16_t)STR_GOD_SPEED,
              "GD_ROW_SPEED no longer names the speed row");
static_assert(GD_MENU_STR[GD_ROW_CLOCK] == (uint16_t)STR_GOD_CLOCK,
              "GD_ROW_CLOCK no longer names the clock row");

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
#define GD_SYS_PAGES    5

// =============================================================================
//  4. MODULE STATE
// =============================================================================
static bool     s_active      = false;
static uint8_t  s_screen      = GSC_MENU;
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

// Serial genome paste.
static char     s_hex[GOD_HEX_LINE_MAX + 1];
static uint8_t  s_hexlen      = 0;
static char     s_hex_show[33];           // last dumped/loaded genome, 32 + NUL

// Serial bench console (the `stall` command).
static char     s_cmd[24];
static uint8_t  s_cmdlen      = 0;

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
  s_screen      = GSC_CONFIRM;
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
//  7. SERIAL GENOME PASTE (command 5, GENOMA / CARGAR)
// =============================================================================
static void hex_paste_service(void)
{
  if (!(s_screen == GSC_HEX && s_hex_mode == GHX_LOAD)) return;

  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c < 0) break;

    if (c == '\n' || c == '\r') {
      if (s_hexlen == 0) continue;
      s_hex[s_hexlen] = '\0';
      Genome g;
      if (genome_from_hex32(s_hex, g)) {
        install_genome(g);
        s_screen = GSC_GENOME;
      } else {
        toast((uint16_t)STR_ERR_GENOME);
      }
      s_hexlen = 0;
      s_hex[0] = '\0';
      continue;
    }
    if (c == ' ' || c == '\t') continue;
    if (s_hexlen >= GOD_HEX_LINE_MAX) { s_hexlen = 0; s_hex[0] = '\0'; }   // restart
    s_hex[s_hexlen++] = (char)c;
    s_hex[s_hexlen]   = '\0';
  }
}

// =============================================================================
//  7b. SERIAL BENCH CONSOLE
//      One command, `stall <ms>`, and it exists to be able to PROVE the
//      timer-driven button sampler (input.cpp note 6) on real hardware: it
//      busy-waits inside loop() for the requested number of milliseconds, so a
//      press made during the stall must still be recognised - with its true
//      duration - on the poll that follows. Without a way to create the stall
//      on demand, the ring is untestable outside a debugger.
//      The stall is bounded well under the 5 s task watchdog. This console runs
//      whether or not the god screen is open, but never while the genome paste
//      owns the serial line.
// =============================================================================
#define GOD_STALL_MAX_MS 3000UL

static void do_stall(uint32_t ms)
{
  if (ms > GOD_STALL_MAX_MS) ms = GOD_STALL_MAX_MS;
  GOD_LOGF("[god] stall %lu ms begin\r\n", (unsigned long)ms);
  const uint32_t t0 = millis();
  while ((uint32_t)(millis() - t0) < ms) {
    // Deliberately busy. yield() here would defeat the whole point.
  }
  GOD_LOGF("[god] stall end after %lu ms\r\n", (unsigned long)(millis() - t0));
}

static void run_cmd(const char* line)
{
  if (strncmp(line, "stall", 5) == 0) {
    const char* a = line + 5;
    while (*a == ' ' || *a == '\t') ++a;
    uint32_t ms = 0;
    while (*a >= '0' && *a <= '9') {
      ms = ms * 10u + (uint32_t)(*a - '0');
      if (ms > GOD_STALL_MAX_MS) { ms = GOD_STALL_MAX_MS; break; }
      ++a;
    }
    do_stall(ms);
    return;
  }
  GOD_LOGF("[god] commands: stall <ms>\r\n");
}

static void cmd_service(void)
{
  // The genome paste owns the line while it is open; never fight it.
  if (s_screen == GSC_HEX && s_hex_mode == GHX_LOAD) return;

  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c < 0) break;
    if (c == '\n' || c == '\r') {
      if (s_cmdlen > 0) {
        s_cmd[s_cmdlen] = '\0';
        run_cmd(s_cmd);
      }
      s_cmdlen = 0;
      s_cmd[0] = '\0';
      continue;
    }
    if (s_cmdlen + 1u >= sizeof(s_cmd)) { s_cmdlen = 0; }   // overlong: restart
    s_cmd[s_cmdlen++] = (char)c;
  }
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
  s_screen      = GSC_MENU;
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
  s_hexlen      = 0;
  s_hex[0]      = '\0';
  s_hex_show[0] = '\0';

  sim_set_time_scale(1u);

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
  s_screen   = GSC_MENU;
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
  s_screen   = GSC_MENU;

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
    // this the user who left SCR_GOD with GOD_EVT_LEAVE to watch an accelerated
    // life would be stranded: the only off switch is the console's exit row.
    if (!s_active) god_enter();
    else           s_screen = GSC_MENU;
    input_flush();          // the eventual release must not fire on SCR_GOD
    return 100;
  }
  return (uint8_t)((held * 100UL) / (uint32_t)GOD_ENTER_HOLD_MS);
}

void god_service(void)
{
  // The bench console is not gated on god mode being ON: a stall has to be
  // reachable from a plain serial terminal on a freshly flashed board.
  cmd_service();

  if (!s_active) return;

  hex_paste_service();

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
      s_screen = GSC_MENU;
      toast((uint16_t)(ok ? STR_GOD_DONE : STR_ERR_NVS));
      return GOD_EVT_WIPED;
    }
    default:
      s_screen = GSC_MENU;
      return GOD_EVT_NONE;
  }
}

// The root list: open the sub-screen for the selected command.
static GodEvt open_command(uint8_t row)
{
  s_sub_cur  = 0;
  s_abs_done = false;

  switch (row) {
    case  0: s_screen = GSC_SPEED;    s_sub_cur = s_scale_idx; break;
    case  1: s_screen = GSC_ABSENCE;  break;
    case  2: s_screen = GSC_STATPICK; break;
    case  3: s_screen = GSC_STAGE;    break;
    case  4: s_screen = GSC_GENOME;   break;
    case  5: s_screen = GSC_SYS;      s_sys_page = GD_SYS_CLOCK; break;
    case  6: open_confirm((uint16_t)STR_CF_WIPE, GCF_WIPE1); break;
    default:
      god_exit();
      return GOD_EVT_LEAVE;
  }
  return GOD_EVT_NONE;
}

// How many rows the current sub-screen has.
static uint8_t sub_count(void)
{
  switch (s_screen) {
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
  switch (s_screen) {
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
      s_screen  = GSC_STATVAL;
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
          s_screen   = GSC_GENE;
          break;
        case GD_GEN_DUMP: {
          const SimView* p = pet();
          if (p) {
            genome_to_hex32(p->genome, s_hex_show);
            GOD_LOGF("GENOME,%s\n", s_hex_show);
          }
          s_hex_mode = GHX_DUMP;
          s_screen   = GSC_HEX;
          break;
        }
        default:
          s_hex_mode = GHX_LOAD;
          s_hexlen   = 0;
          s_hex[0]   = '\0';
          s_screen   = GSC_HEX;
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
  if (g == GST_LONG_BOTH) { s_screen = GSC_MENU; return GOD_EVT_LEAVE; }

  // ---- the shared confirm modal -------------------------------------------
  if (s_screen == GSC_CONFIRM) {
    switch (g) {
      case GST_TAP_L:  s_confirm_yes = !s_confirm_yes;      return GOD_EVT_NONE;
      case GST_TAP_R:  if (s_confirm_yes) return commit_confirm();
                       s_confirm_act = GCF_NONE; s_screen = GSC_MENU;
                       return GOD_EVT_NONE;
      case GST_HOLD_R: s_confirm_act = GCF_NONE; s_screen = GSC_MENU;
                       return GOD_EVT_NONE;
      default:         return GOD_EVT_NONE;
    }
  }

  // ---- the root list -------------------------------------------------------
  if (s_screen == GSC_MENU) {
    switch (g) {
      case GST_TAP_L:
      case GST_HOLD_L: list_step(s_menu_cur, (uint8_t)GOD_MENU_ROWS, +1); return GOD_EVT_NONE;
      case GST_TAP_R:  return open_command(s_menu_cur);
      case GST_DBL_L:  s_menu_cur = 0;                                    return GOD_EVT_NONE;
      case GST_DBL_R:  s_menu_cur = (uint8_t)(GOD_MENU_ROWS - 1);         return GOD_EVT_NONE;
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
      if (s_screen == GSC_STATVAL) { s_screen = GSC_STATPICK; s_sub_cur = s_pick; }
      else if (s_screen == GSC_GENE || s_screen == GSC_HEX) { s_screen = GSC_GENOME; s_sub_cur = 0; }
      else                       { s_screen = GSC_MENU; }
      return GOD_EVT_NONE;

    case GST_TAP_L:
    case GST_HOLD_L:
      if (s_screen == GSC_SYS)      { s_sys_page = (uint8_t)((s_sys_page + 1u) % GD_SYS_PAGES); }
      else if (s_screen == GSC_GENE){ list_step(s_gene_idx, (uint8_t)GOD_GENE_COUNT, +1); }
      else                          { list_step(s_sub_cur, sub_count(), +1); }
      return GOD_EVT_NONE;

    case GST_DBL_L:
      if (s_screen == GSC_GENE) s_gene_idx = 0; else s_sub_cur = 0;
      return GOD_EVT_NONE;

    case GST_DBL_R:
      // On the gene editor this is the decrement; everywhere else it is the
      // vertical-list "jump to last item" of GAME_DESIGN 8.3.
      if (s_screen == GSC_GENE) {
        const SimView* p = pet();
        if (p) {
          const GodGene& gg = GD_GENES[s_gene_idx % GOD_GENE_COUNT];
          Genome gn = p->genome;
          uint8_t v = gg.get(gn);
          v = (uint8_t)((v == 0) ? gg.vmax : (v - 1u));
          gg.set(gn, v);
          install_genome(gn);
        }
      } else {
        const uint8_t n = sub_count();
        s_sub_cur = (uint8_t)((n > 0) ? (n - 1) : 0);
      }
      return GOD_EVT_NONE;

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
    rd_text_fit(2, 54, OLED_W - 4, RD_FONT_TINY, s_hex);
    rd_affordance(0, S(STR_AF_BACK));
  } else {
    rd_affordance(0, S(STR_AF_SEL));
  }
}

static void draw_sys(void)
{
  char b[34];

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
      break;
    }
    case GD_SYS_RADIO: {
      draw_title(S(STR_GOD_RADIO));
      snprintf(b, sizeof(b), "mode=%u phase=%u err=%u", (unsigned)net_mode(),
               (unsigned)net_phase(), (unsigned)net_last_err());
      rd_text(2, 27, RD_FONT_TINY, b);
      snprintf(b, sizeof(b), "ip %s", net_ip());
      rd_text(2, 35, RD_FONT_TINY, b);
      snprintf(b, sizeof(b), "rssi %d  ble %u/%u", (int)net_rssi(),
               (unsigned)net_ble_sessions_used(), (unsigned)BLE_SESSION_CAP);
      rd_text(2, 43, RD_FONT_TINY, b);
      break;
    }
    default: {
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

  switch (s_screen) {
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
void     god_begin(void)                        { }
void     god_service(void)                      { }
uint8_t  god_entry_progress(uint8_t screen_id)  { (void)screen_id; return 0; }
void     god_enter(void)                        { }
void     god_exit(void)                         { }
void     god_draw_marker(void)                  { }
bool     god_freeze_clock(uint8_t& h, uint8_t& m) { (void)h; (void)m; return false; }
bool     god_dump_enabled(void)                 { return false; }

#endif // GOD_MODE_ENABLED
