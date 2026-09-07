// =============================================================================
//  PEBBLEBOL - dev/diag_core.cpp
//  See diag_core.h for the contract and for why this file is not inside
//  dev/godmode.cpp.
//
//  ZERO file-scope mutable state and ZERO heap: everything is a caller-owned
//  buffer or a caller-owned GameState, exactly as game/inventory.h takes its
//  bag and game/cooldowns.h takes its table. Two host cases cannot leak into
//  each other and the module costs the release build nothing it does not use.
// =============================================================================
#include "diag_core.h"

#include <stdio.h>
#include <string.h>

#include "../core/version.h"
#include "../core/rng.h"
#include "../data/items_table.h"
#include "../data/species_table.h"
#include "../game/activity.h"
#include "../game/box.h"
#include "../game/encounters.h"
#include "../game/evolution.h"
#include "../game/genome.h"
#include "../game/inventory.h"
#include "../game/sim.h"
#include "../game/taint.h"
#include "../game/xp.h"

// =============================================================================
//  1. THE SINK
// =============================================================================
void diag_out_init(DiagOut& o, char* buf, uint16_t cap)
{
  o.buf = buf;
  o.cap = cap;
  o.len = 0;
  o.truncated = 0;
  if (buf && cap) buf[0] = '\0';
}

void diag_putc(DiagOut& o, char c)
{
  if (!o.buf || o.cap == 0) { o.truncated = 1u; return; }
  if ((uint16_t)(o.len + 1u) >= o.cap) { o.truncated = 1u; return; }
  o.buf[o.len++] = c;
  o.buf[o.len]   = '\0';
}

void diag_puts(DiagOut& o, const char* s)
{
  if (!s) return;
  while (*s) diag_putc(o, *s++);
}

void diag_putu(DiagOut& o, uint32_t v)
{
  char b[11];
  uint8_t n = 0;
  do { b[n++] = (char)('0' + (v % 10u)); v /= 10u; } while (v && n < sizeof(b));
  while (n) diag_putc(o, b[--n]);
}

void diag_puti(DiagOut& o, int32_t v)
{
  if (v < 0) { diag_putc(o, '-'); diag_putu(o, (uint32_t)(-(int64_t)v)); return; }
  diag_putu(o, (uint32_t)v);
}

void diag_puthex(DiagOut& o, uint32_t v, uint8_t digits)
{
  static const char kHex[] = "0123456789ABCDEF";
  if (digits == 0 || digits > 8) digits = 8;
  for (uint8_t i = digits; i > 0; --i) {
    diag_putc(o, kHex[(v >> (uint8_t)((i - 1u) * 4u)) & 0x0Fu]);
  }
}

void diag_nl(DiagOut& o) { diag_putc(o, '\n'); }

// =============================================================================
//  2. THE COMMAND TABLE
//     Order MUST match DiagCmdId; the static_assert block at the bottom of this
//     file and diag_cmd_spec()'s own check both hold it.
// =============================================================================
static constexpr DiagCmdSpec kCmds[DGC_COUNT] = {
  // --- always compiled: read-only, and the bounded stall -------------------
  // All three are DCF_DEFERS as well as DCF_ALWAYS, and that is a statement
  // about knowledge rather than about capability: only the SHELL knows which
  // build it is (so only it can say what `help` should list), only the shell can
  // read ESP.getFreeHeap() and net_phase() (so only it can fill DiagFields), and
  // only the shell holds the live GameState's BoxHeader. The pure layer owns
  // what each of them SAYS - diag_help_line(), diag_fmt_field() - and
  // tests/test_diag.cpp drives that half.
  { "help",           DGC_HELP,           0, 0, DCF_ALWAYS | DCF_DEFERS,
    "help - what this build can run" },
  { "info",           DGC_INFO,           0, 0, DCF_ALWAYS | DCF_DEFERS,
    "info - the spec 49 field list" },
  { "show_save",      DGC_SHOW_SAVE,      0, 0, DCF_ALWAYS | DCF_DEFERS,
    "show_save - BoxHeader, slot mask, quarantine mask" },
  { "stall",          DGC_STALL,          0, 1, DCF_ALWAYS | DCF_DEFERS,
    "stall [ms] - busy-wait, for the input latency measurement" },

  // --- spec 66, the mutating twelve ----------------------------------------
  // give_item is NOT DCF_TAINTS. The banner in diag_core.h says why, at length:
  // an Inventory carries no genome and no flags and does not cross a trade.
  { "give_item",      DGC_GIVE_ITEM,      1, 2, DCF_MUTATES,
    "give_item <id> [n] - add n of item id to the bag" },
  { "spawn",          DGC_SPAWN,          1, 2, DCF_MUTATES | DCF_TAINTS,
    "spawn <species> [level] - file a new Pebble in the Box" },
  { "set_level",      DGC_SET_LEVEL,      1, 1, DCF_MUTATES | DCF_TAINTS,
    "set_level <n> - set the active Pebble's level, 1..30" },
  { "set_time",       DGC_SET_TIME,       1, 1, DCF_MUTATES | DCF_DEFERS,
    "set_time <epoch> - move game time by skew, never the system clock" },
  // DCF_TAINTS, and the reason is not obvious enough to leave unwritten: this
  // arm touches no PebbleInstance itself. What it does is note interactions
  // that app/app.cpp will LATER pay as XP_SRC_CARRY onto the active Pebble
  // through the one funnel. The creature therefore ends up carrying XP nobody
  // earned, which is a dynasty cheat however indirectly it arrived - so the
  // Pebble is marked here, where the decision is made.
  { "set_activity",   DGC_SET_ACTIVITY,   1, 1, DCF_MUTATES | DCF_TAINTS,
    "set_activity <n> - record n interactions; the score is DERIVED" },
  { "heal",           DGC_HEAL,           0, 0, DCF_MUTATES | DCF_TAINTS,
    "heal - every care stat to 100 %, sickness cleared" },
  { "fill_box",       DGC_FILL_BOX,       0, 1, DCF_MUTATES | DCF_TAINTS,
    "fill_box [level] - fill every free slot" },
  { "clear_box",      DGC_CLEAR_BOX,      0, 0, DCF_MUTATES,
    "clear_box - release every STORED slot; the active one is refused" },
  { "start_battle",   DGC_START_BATTLE,   0, 0, DCF_MUTATES | DCF_DEFERS,
    "start_battle - push the battle screen on the fixed diag seed" },
  { "start_creator",  DGC_START_CREATOR,  0, 0, DCF_MUTATES | DCF_DEFERS,
    "start_creator - push the creator portal screen" },
  { "scan_wifi",      DGC_SCAN_WIFI,      0, 0, DCF_MUTATES | DCF_DEFERS,
    "scan_wifi - open the network screen and start a scan" },

  // --- spec 49's actions ---------------------------------------------------
  // test_encounter ROLLS and PRINTS; it does not navigate. The roll is the part
  // that has a right answer, and it is the part a bench operator cannot see any
  // other way.
  { "test_encounter", DGC_TEST_ENCOUNTER, 0, 1, 0,
    "test_encounter [net_hash] - roll one encounter and print it" },
  { "test_evolution", DGC_TEST_EVOLUTION, 0, 0, DCF_MUTATES | DCF_TAINTS,
    "test_evolution - force the active Pebble's evolution rule" },
  { "test_minigame",  DGC_TEST_MINIGAME,  0, 1, DCF_MUTATES | DCF_DEFERS,
    "test_minigame [n] - open minigame n" },
  { "test_save_load", DGC_TEST_SAVE_LOAD, 0, 0, DCF_MUTATES | DCF_DEFERS,
    "test_save_load - seal, verify, corrupt-and-verify, then force a write" },
  { "seed",           DGC_SEED,           1, 1, DCF_MUTATES,
    "seed <n> - reseed every rng.h stream, for a reproducible run" }
};

const DiagCmdSpec* diag_cmd_table(void) { return kCmds; }

const DiagCmdSpec* diag_cmd_spec(uint8_t id)
{
  if (id >= (uint8_t)DGC_COUNT) return nullptr;
  return &kCmds[id];
}

const DiagCmdSpec* diag_cmd_find(const char* name)
{
  if (!name || !*name) return nullptr;
  for (uint8_t i = 0; i < (uint8_t)DGC_COUNT; ++i) {
    if (strcmp(kCmds[i].name, name) == 0) return &kCmds[i];
  }
  return nullptr;
}

uint8_t diag_cmd_taints(uint8_t id)
{
  const DiagCmdSpec* s = diag_cmd_spec(id);
  return (s && (s->flags & DCF_TAINTS)) ? 1u : 0u;
}

// =============================================================================
//  3. THE PARSER
// =============================================================================
#define DIAG_TOK_MAX  16          // the longest name is "test_encounter" (14)

static bool is_space(char c) { return c == ' ' || c == '\t'; }

// One unsigned number, decimal or 0x hex. Total: reports WHY rather than
// returning a plausible 0, which is the difference between a console that
// teaches and one that lies.
static uint8_t parse_u32(const char* s, uint32_t& out)
{
  out = 0;
  if (!s || !*s) return DGP_BAD_NUMBER;

  uint32_t base = 10u;
  if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16u; s += 2; }
  if (!*s) return DGP_BAD_NUMBER;

  uint32_t v = 0;
  for (; *s; ++s) {
    uint32_t d;
    if      (*s >= '0' && *s <= '9') d = (uint32_t)(*s - '0');
    else if (base == 16u && *s >= 'a' && *s <= 'f') d = (uint32_t)(*s - 'a' + 10);
    else if (base == 16u && *s >= 'A' && *s <= 'F') d = (uint32_t)(*s - 'A' + 10);
    else return DGP_BAD_NUMBER;
    // THE SECOND HALF OF THE BASE CHECK, and it is NOT redundant even though
    // the branches above already restrict `d`: MEASURED AT THE P10-C1 EXIT,
    // dropping either one of the two alone leaves `seed 1f` still refused, and
    // dropping BOTH makes it parse as 31. Two guards for one property is what
    // stops a one-line edit to the branch conditions from silently turning a
    // typo into a number.
    if (d >= base) return DGP_BAD_NUMBER;
    if (v > (0xFFFFFFFFu - d) / base) return DGP_OVERFLOW;
    v = v * base + d;
  }
  out = v;
  return DGP_OK;
}

void diag_parse(const char* line, DiagParse& out)
{
  out.cmd  = (uint8_t)DGC_UNKNOWN;
  out.err  = DGP_EMPTY;
  out.argc = 0;
  for (uint8_t i = 0; i < DIAG_ARG_MAX; ++i) out.arg[i] = 0;
  if (!line) return;

  const char* p = line;
  while (is_space(*p)) ++p;
  if (!*p) return;                       // blank line: DGP_EMPTY, not an error

  // --- the name ------------------------------------------------------------
  char tok[DIAG_TOK_MAX + 1];
  uint8_t n = 0;
  while (*p && !is_space(*p)) {
    if (n < DIAG_TOK_MAX) tok[n++] = *p;
    else { out.err = DGP_UNKNOWN; return; }   // longer than any name: unknown
    ++p;
  }
  tok[n] = '\0';

  const DiagCmdSpec* spec = diag_cmd_find(tok);
  if (!spec) { out.err = DGP_UNKNOWN; return; }
  out.cmd = spec->id;

  // --- the arguments -------------------------------------------------------
  while (*p) {
    while (is_space(*p)) ++p;
    if (!*p) break;
    char num[12];
    uint8_t m = 0;
    while (*p && !is_space(*p)) {
      if (m < (uint8_t)(sizeof(num) - 1u)) num[m++] = *p;
      else { out.err = DGP_OVERFLOW; return; }
      ++p;
    }
    num[m] = '\0';
    if (out.argc >= DIAG_ARG_MAX || out.argc >= spec->argc_max) {
      out.err = DGP_TOO_MANY;
      return;
    }
    uint32_t v = 0;
    const uint8_t e = parse_u32(num, v);
    if (e != DGP_OK) { out.err = e; return; }
    out.arg[out.argc++] = v;
  }

  out.err = (out.argc < spec->argc_min) ? (uint8_t)DGP_TOO_FEW : (uint8_t)DGP_OK;
}

const char* diag_parse_err_str(uint8_t err)
{
  switch (err) {
    case DGP_OK:         return "ok";
    case DGP_EMPTY:      return "empty";
    case DGP_UNKNOWN:    return "no such command";
    case DGP_TOO_FEW:    return "missing argument";
    case DGP_TOO_MANY:   return "too many arguments";
    case DGP_BAD_NUMBER: return "not a number";
    case DGP_OVERFLOW:   return "number too large";
    default:             return "?";
  }
}

// =============================================================================
//  4. THE TAINT RULE
// =============================================================================
void diag_taint(PebbleInstance& p)
{
  // BOTH markers, for the reason game/taint.h gives at length: they are set by
  // different writers on different paths and pb_is_tainted() reads both.
  gene_set_tainted(p.genome, 1);                                  // reseals
  p.flags = (uint8_t)(p.flags | (uint8_t)PBF_GOD_TAINTED);
}

// =============================================================================
//  5. EXECUTION
// =============================================================================
static PebbleInstance* active_pebble(const DiagCtx& ctx)
{
  if (!ctx.gs) return nullptr;
  const uint8_t s = box_active();
  if (s >= (uint8_t)BOX_SLOTS) return nullptr;
  return box_slot(s);
}

// A genome for a Pebble minted inside the console. genome_genesis() CLEARS the
// taint bit (game/genome.cpp), which is exactly why every mint below runs
// diag_taint() afterwards rather than trusting the constructor.
static Genome minted_genome(void)
{
  return genome_genesis();
}

// Files one Pebble and marks it. THE ONE PLACE a spawn is written, so
// fill_box cannot drift from spawn.
static uint8_t mint_one(const DiagCtx& ctx, uint8_t species, uint8_t level)
{
  const Genome g = minted_genome();
  const uint8_t slot = box_new_pebble(species, level, (uint8_t)ORIGIN_WILD, g,
                                      rng_u32(RNG_MISC), ctx.now_epoch);
  if (slot >= (uint8_t)BOX_SLOTS) return (uint8_t)BOX_SLOT_NONE;
  PebbleInstance* p = box_slot(slot);
  if (p) diag_taint(*p);
  return slot;
}

static void say_slot(DiagOut& o, const char* what, uint8_t slot,
                     const PebbleInstance& p)
{
  diag_puts(o, what);
  diag_puts(o, " slot=");   diag_putu(o, slot);
  diag_puts(o, " sp=");     diag_putu(o, p.species_id);
  diag_puts(o, " lv=");     diag_putu(o, p.level);
  diag_puts(o, " id=");     diag_puthex(o, p.id, 8);
  diag_puts(o, " tainted=");diag_putu(o, pb_is_tainted(p) ? 1u : 0u);
  diag_nl(o);
}

uint8_t diag_exec(const DiagParse& p, const DiagCtx& ctx, DiagOut& out,
                  uint8_t& commit)
{
  commit = 0;
  if (p.err != DGP_OK) {
    diag_puts(out, diag_parse_err_str(p.err));
    diag_nl(out);
    return (uint8_t)DGR_ERR_PARSE;
  }
  const DiagCmdSpec* spec = diag_cmd_spec(p.cmd);
  if (!spec) { diag_puts(out, "no such command\n"); return (uint8_t)DGR_ERR_PARSE; }

  // Everything the pure layer cannot do: Serial timing, the radio, the screen
  // stack, flash. Named as a DEFER rather than half-done here.
  if (spec->flags & DCF_DEFERS) return (uint8_t)DGR_DEFER;

  switch (p.cmd) {

    // ---- give_item <id> [n] ------------------------------------------------
    case DGC_GIVE_ITEM: {
      if (!ctx.gs) return (uint8_t)DGR_ERR_NO_PET;
      const uint32_t id = p.arg[0];
      const uint32_t n  = (p.argc >= 2) ? p.arg[1] : 1u;
      if (id < 1u || id > (uint32_t)ITEM_COUNT || n == 0u || n > 255u) {
        diag_puts(out, "item 1..");
        diag_putu(out, (uint32_t)ITEM_COUNT);
        diag_puts(out, ", n 1..255\n");
        return (uint8_t)DGR_ERR_RANGE;
      }
      const uint8_t got = inv_add(ctx.gs->inv, (uint8_t)id, (uint8_t)n);
      if (got == 0u) { diag_puts(out, "bag full\n"); return (uint8_t)DGR_ERR_FULL; }
      diag_puts(out, "give_item id=");  diag_putu(out, id);
      diag_puts(out, " +");             diag_putu(out, got);
      diag_puts(out, " held=");         diag_putu(out, inv_count(ctx.gs->inv, (uint8_t)id));
      // NOT DCF_TAINTS, deliberately, and the line is careful about WHAT is
      // untainted: an Inventory carries no marker to set. The LIVING PET is
      // still tainted on a device, because dev/godmode.cpp enters god mode on
      // any mutating typed command and god_enter() taints it. Two different
      // claims, and conflating them is how "no taint" would become a lie.
      diag_puts(out, " (a bag carries no taint marker; the pet is tainted by god mode itself)\n");
      commit = DGCOMMIT_INV;
      return (uint8_t)DGR_OK;
    }

    // ---- spawn <species> [level] ------------------------------------------
    case DGC_SPAWN: {
      if (!ctx.gs) return (uint8_t)DGR_ERR_NO_PET;
      const uint32_t sp = p.arg[0];
      const uint32_t lv = (p.argc >= 2) ? p.arg[1] : 1u;
      if (sp > 255u || !species_get((uint8_t)sp)) {
        diag_puts(out, "no such species\n");
        return (uint8_t)DGR_ERR_RANGE;
      }
      if (lv < 1u || lv > (uint32_t)PB_LEVEL_MAX) {
        diag_puts(out, "level 1..");
        diag_putu(out, (uint32_t)PB_LEVEL_MAX);
        diag_nl(out);
        return (uint8_t)DGR_ERR_RANGE;
      }
      const uint8_t slot = mint_one(ctx, (uint8_t)sp, (uint8_t)lv);
      if (slot >= (uint8_t)BOX_SLOTS) {
        diag_puts(out, "box full\n");
        return (uint8_t)DGR_ERR_FULL;
      }
      say_slot(out, "spawn", slot, *box_slot(slot));
      commit = DGCOMMIT_BOX | DGCOMMIT_SLOTS;
      return (uint8_t)DGR_OK;
    }

    // ---- set_level <n> -----------------------------------------------------
    case DGC_SET_LEVEL: {
      PebbleInstance* a = active_pebble(ctx);
      if (!a) return (uint8_t)DGR_ERR_NO_PET;
      const uint32_t lv = p.arg[0];
      if (lv < 1u || lv > (uint32_t)PB_LEVEL_MAX) {
        diag_puts(out, "level 1..");
        diag_putu(out, (uint32_t)PB_LEVEL_MAX);
        diag_nl(out);
        return (uint8_t)DGR_ERR_RANGE;
      }
      // The hp rescale is xp.h's, not a fourth copy of the formula: a level set
      // by hand with hp_cur left where it was is a Pebble whose own numbers
      // disagree, which is what game/validate.cpp refuses.
      const SpeciesDef* sd = species_get(a->species_id);
      const uint16_t before = sd ? xp_hp_max(sd->base_hp, a->level) : 0u;
      a->level = (uint8_t)lv;
      const uint16_t after  = sd ? xp_hp_max(sd->base_hp, a->level) : 0u;
      if (sd) xp_hp_rescale(*a, before, after);
      // THE LOAD-BEARING LINE. Level lives on the instance and there is no
      // sim_god_set_level(), so nothing else in this arm would mark it.
      diag_taint(*a);
      say_slot(out, "set_level", box_active(), *a);
      commit = DGCOMMIT_ACTIVE;
      return (uint8_t)DGR_OK;
    }

    // ---- set_activity <n> --------------------------------------------------
    case DGC_SET_ACTIVITY: {
      PebbleInstance* a = active_pebble(ctx);
      if (!ctx.gs || !a) return (uint8_t)DGR_ERR_NO_PET;
      const uint32_t n = p.arg[0];
      if (n > 255u) { diag_puts(out, "n 0..255\n"); return (uint8_t)DGR_ERR_RANGE; }
      // THE SPEC ASKS FOR A SETTER AND THERE IS NOT ONE. game/activity.h derives
      // the score from CooldownTable through four capped terms; a setter would
      // be a fifth writer to a persisted farm-control field. So this RECORDS n
      // interactions and reports what the score actually became - which
      // saturates at the term cap, and saying so is the honest answer.
      // THE GAIN IS NOT DRAINED HERE. act_take_gain() has exactly one call site
      // in the firmware - app/app.cpp's app_pay_activity() - and tools/check.sh
      // holds that as a red line, because the funnel is what puts the XP ledger
      // decrease on flash. Draining it in the console would pay a reward outside
      // the funnel with no place left to look for it. So the notes are left for
      // the app to pay through the normal capped path, and the Pebble that will
      // receive that XP is marked instead.
      ActClock c; c.now_epoch = ctx.now_epoch; c.cal = ctx.cal;
      for (uint32_t i = 0; i < n; ++i) (void)act_note_interaction(ctx.gs->cds, c);
      diag_taint(*a);
      diag_puts(out, "set_activity notes=");
      diag_putu(out, n);
      diag_puts(out, " score=");
      diag_putu(out, (uint32_t)act_score_today(ctx.gs->cds, c));
      diag_puts(out, " (derived, not set)\n");
      commit = DGCOMMIT_CDS | DGCOMMIT_ACTIVE;
      return (uint8_t)DGR_OK;
    }

    // ---- heal ---------------------------------------------------------------
    case DGC_HEAL: {
      PebbleInstance* a = active_pebble(ctx);
      if (!a || !ctx.have_sim) return (uint8_t)DGR_ERR_NO_PET;
      for (uint8_t s = 0; s < (uint8_t)ST_COUNT; ++s) {
        sim_god_set_stat((StatId)s, 100u);
      }
      // sim_god_set_sick() was declared in game/sim.h's GOD MODE HOOKS block,
      // implemented in sim.cpp, and called by NO console row - its only caller
      // in the whole tree was tests/test_care.cpp. Spec 66's `heal` is the
      // command that was always supposed to. Same shape as rd_frame_time_us().
      sim_god_set_sick(0u);
      // Belt and braces: the sim hooks taint through PF_GOD_TAINTED and
      // status_sync(), but this arm must not depend on that staying true.
      diag_taint(*a);
      diag_puts(out, "heal hun=");  diag_putu(out, sim_stat_pct(ST_HUNGER));
      diag_puts(out, " hea=");      diag_putu(out, sim_stat_pct(ST_HEALTH));
      diag_puts(out, " sick=");     diag_putu(out, sim_is_sick());
      diag_puts(out, " tainted=");  diag_putu(out, pb_is_tainted(*a) ? 1u : 0u);
      diag_nl(out);
      commit = DGCOMMIT_ACTIVE;
      return (uint8_t)DGR_OK;
    }

    // ---- fill_box [level] --------------------------------------------------
    case DGC_FILL_BOX: {
      if (!ctx.gs) return (uint8_t)DGR_ERR_NO_PET;
      const uint32_t lv = (p.argc >= 1) ? p.arg[0] : 1u;
      if (lv < 1u || lv > (uint32_t)PB_LEVEL_MAX) {
        diag_puts(out, "level 1..");
        diag_putu(out, (uint32_t)PB_LEVEL_MAX);
        diag_nl(out);
        return (uint8_t)DGR_ERR_RANGE;
      }
      uint8_t made = 0;
      // The species walks the roster so a filled Box is a useful fixture rather
      // than ten copies of one creature.
      for (uint8_t i = 0; i < (uint8_t)BOX_SLOTS; ++i) {
        const uint8_t sp = (uint8_t)(1u + (i % (uint8_t)SPECIES_TABLE_COUNT));
        if (mint_one(ctx, sp, (uint8_t)lv) >= (uint8_t)BOX_SLOTS) break;
        ++made;
      }
      diag_puts(out, "fill_box made="); diag_putu(out, made);
      diag_puts(out, " count=");        diag_putu(out, box_count());
      diag_puts(out, "/");              diag_putu(out, box_capacity());
      diag_nl(out);
      if (made == 0u) return (uint8_t)DGR_ERR_FULL;
      commit = DGCOMMIT_BOX | DGCOMMIT_SLOTS;
      return (uint8_t)DGR_OK;
    }

    // ---- clear_box ----------------------------------------------------------
    case DGC_CLEAR_BOX: {
      if (!ctx.gs) return (uint8_t)DGR_ERR_NO_PET;
      const uint8_t act = box_active();
      uint8_t freed = 0;
      for (uint8_t s = 0; s < (uint8_t)BOX_SLOTS; ++s) {
        if (!box_occupied(s)) continue;
        if (box_release(s, true)) ++freed;
      }
      // box.h invariant B4: release refuses the ACTIVE slot outright. So
      // clear_box CANNOT empty the Box, and it says so instead of looking
      // broken to whoever counts the slots afterwards.
      diag_puts(out, "clear_box freed=");  diag_putu(out, freed);
      diag_puts(out, " left=");            diag_putu(out, box_count());
      if (act < (uint8_t)BOX_SLOTS) {
        diag_puts(out, " (slot ");  diag_putu(out, act);
        diag_puts(out, " is active and box.h B4 refuses it)");
      }
      diag_nl(out);
      commit = DGCOMMIT_BOX | DGCOMMIT_SLOTS;
      return (uint8_t)DGR_OK;
    }

    // ---- test_encounter [net_hash] -----------------------------------------
    case DGC_TEST_ENCOUNTER: {
      if (!ctx.gs) return (uint8_t)DGR_ERR_NO_PET;
      EncounterInput in;
      memset(&in, 0, sizeof in);
      in.net_hash     = (p.argc >= 1 && p.arg[0] != 0u) ? p.arg[0] : 0xD1A6C0DEu;
      in.bucket       = encounter_bucket(ctx.now_epoch);
      in.device_seed  = ctx.gs->box.next_id_counter;
      in.category     = 0u;
      in.rssi         = -60;
      const PebbleInstance* a = active_pebble(ctx);
      in.active_level = (a && a->level) ? a->level : 1u;
      in.progress     = box_count();
      in.rare_bonus_pm = 0u;

      EncounterResult r;
      memset(&r, 0, sizeof r);
      if (!encounter_roll(in, r)) {
        diag_puts(out, "test_encounter refused (net_hash 0 or bad input)\n");
        return (uint8_t)DGR_ERR_RANGE;
      }
      diag_puts(out, "test_encounter out=");  diag_putu(out, r.outcome);
      diag_puts(out, " sp=");                 diag_putu(out, r.species_id);
      diag_puts(out, " lv=");                 diag_putu(out, r.level);
      diag_puts(out, " item=");               diag_putu(out, r.item_id);
      diag_puts(out, " ev=");                 diag_putu(out, r.event_id);
      diag_puts(out, " bucket=");             diag_putu(out, in.bucket);
      diag_nl(out);
      // Read-only: it rolls and prints. Nothing is filed, so nothing is tainted
      // and nothing is committed.
      return (uint8_t)DGR_OK;
    }

    // ---- test_evolution -----------------------------------------------------
    case DGC_TEST_EVOLUTION: {
      PebbleInstance* a = active_pebble(ctx);
      if (!a) return (uint8_t)DGR_ERR_NO_PET;
      const EvolutionRule* rule = evolution_rule_for(a->species_id);
      if (!rule) {
        diag_puts(out, "test_evolution: species ");
        diag_putu(out, a->species_id);
        diag_puts(out, " is final-stage, no rule\n");
        return (uint8_t)DGR_ERR_RANGE;
      }
      // The console FORCES the inputs a rule needs rather than editing the
      // Pebble past its rule: evolution_apply() re-checks evolution_ready() and
      // is the only writer (game/evolution.h), so a forced context is the only
      // honest way in.
      const uint8_t was_level = a->level;
      const uint8_t was_sp    = a->species_id;
      // The rule's own level, not PB_LEVEL_MAX: the point is to exercise the
      // rule, not to hand the operator a level-30 creature as a side effect.
      if (a->level < rule->level) a->level = rule->level;
      EvoContext c;
      evo_context_clear(c);
      c.have      = (uint8_t)(EVOCTX_HAPPINESS | EVOCTX_CORRUPTED |
                              EVOCTX_ITEM | EVOCTX_ACTIVITY);
      c.happiness = 100u;
      c.corrupted = (uint8_t)((a->status & (uint8_t)PBS_CORRUPTED) ? 1u : 0u);
      // EVOC_ITEM's key is the rule's own cond_value, so the forced context
      // presents exactly the key this rule asks for and no other.
      c.item_id   = (uint8_t)rule->cond_value;
      c.activity  = 100u;
      const bool ok = evolution_apply(*a, c);
      // Tainted whether or not the rule fired: the level was edited by hand
      // either way, and a Pebble this arm touched is not a clean one.
      diag_taint(*a);
      diag_puts(out, "test_evolution ");
      diag_puts(out, ok ? "applied " : "refused ");
      diag_putu(out, was_sp);  diag_puts(out, "->");
      diag_putu(out, a->species_id);
      diag_puts(out, " lv ");  diag_putu(out, was_level);
      diag_puts(out, "->");    diag_putu(out, a->level);
      diag_nl(out);
      commit = DGCOMMIT_ACTIVE;
      return ok ? (uint8_t)DGR_OK : (uint8_t)DGR_ERR_REFUSED;
    }

    // ---- seed <n> -----------------------------------------------------------
    case DGC_SEED: {
      // spec 49's "deterministic RNG seed". rng_seed_all()'s only caller in the
      // tree was app.cpp's one esp_random(); this is the second, and it is what
      // makes a bench run reproducible from a number in a bug report.
      rng_seed_all(p.arg[0]);
      diag_puts(out, "seed ");
      diag_puthex(out, p.arg[0], 8);
      diag_puts(out, " -> every rng.h stream\n");
      return (uint8_t)DGR_OK;
    }

    default:
      diag_puts(out, "not runnable here\n");
      return (uint8_t)DGR_ERR_NOT_HERE;
  }
}

bool diag_help_line(uint8_t i, bool dev, DiagOut& out)
{
  if (i == (uint8_t)DGC_COUNT) {
    // THE HONEST LINE. spec 67 "Diagnostics available" is a claim about the
    // artefact that ships, and the artefact that ships is GOD_MODE_ENABLED 0.
    if (dev) return false;
    diag_puts(out, "  (GOD_MODE_ENABLED 0: the mutating half is not in this build)");
    return true;
  }
  if (i > (uint8_t)DGC_COUNT) return false;
  const DiagCmdSpec& s = kCmds[i];
  if (!dev && !(s.flags & DCF_ALWAYS)) return false;
  diag_puts(out, "  ");
  diag_puts(out, s.usage);
  return true;
}

void diag_help(bool dev, DiagOut& out)
{
  diag_puts(out, "commands:\n");
  for (uint8_t i = 0; i <= (uint8_t)DGC_COUNT; ++i) {
    const uint16_t mark = out.len;
    if (!diag_help_line(i, dev, out)) { out.len = mark; continue; }
    diag_nl(out);
  }
}

// =============================================================================
//  6. THE SPEC 49 FIELD LIST
// =============================================================================
static const char* const kFieldName[DGD_FIELD_COUNT] = {
  "Firmware", "Build", "Free RAM", "Battery", "Wi-Fi", "BLE",
  "Clock", "Save version", "Pebble count", "Last scan", "Last error",
  "Protocol version"
};

const char* diag_field_name(uint8_t id)
{
  return (id < (uint8_t)DGD_FIELD_COUNT) ? kFieldName[id] : "?";
}

// -----------------------------------------------------------------------------
//  SCREEN NAMES (P10-C6). See dev/diag_core.h for why: the perf receipt printed
//  a raw ScreenId and the enum has been renumbered mid-list within this phase.
//  Index-parallel to ScreenId, with a static_assert on its length, so a screen
//  added to the enum without a name here fails the BUILD by name.
// -----------------------------------------------------------------------------
static const char* const kScreenName[] = {
  "BOOT", "LOAD_SAVE", "HOME", "MENU", "CARE", "PLAY", "GAME", "BOX",
  "STATUS", "STATUS_B", "NETWORK", "LINK", "CREATOR", "SETTINGS", "TIME",
  "SETUP_NAME", "SETUP_STARTER", "CONFIRM", "ALERT", "ENCOUNTER", "CAPTURE",
  "BATTLE", "TRADE", "BREED", "EVOLUTION", "ITEM_REWARD", "ERROR", "SLEEP",
  "DIAG"
};
static_assert(sizeof(kScreenName) / sizeof(kScreenName[0]) == (size_t)SCR_COUNT,
              "spec section 46's performance receipt names the screen that owned "
              "the worst frame. A ScreenId was added to the enum without a name "
              "here, so a bench capture would print a number nothing can map - "
              "and this enum has already been renumbered mid-list once (P10-C4 "
              "inserted SCR_SETUP_NAME and SCR_SETUP_STARTER), which is what "
              "makes an out-of-date mapping worse than no mapping at all.");

const char* diag_screen_name(uint8_t scr)
{
  return (scr < (uint8_t)SCR_COUNT) ? kScreenName[scr] : "?";
}

void diag_fields_clear(DiagFields& f)
{
  memset(&f, 0, sizeof f);
  f.box_cap = (uint8_t)BOX_SLOTS;
}

static void fmt_cal(DiagOut& o, uint8_t cal)
{
  static const char* const kCal[] = { "UNSET", "EST", "USER", "PHONE" };
  diag_puts(o, (cal < 4u) ? kCal[cal] : "?");
}

void diag_fmt_field(uint8_t id, const DiagFields& f, DiagOut& o)
{
  diag_puts(o, diag_field_name(id));
  diag_puts(o, ": ");

  switch (id) {
    case DGD_FIRMWARE:
      diag_puts(o, FW_VERSION);
      break;

    case DGD_BUILD:
      // NEW IN P10-C1. Nothing in src/ or tools/ carried a build stamp before
      // this phase; the field was simply absent from the product.
      diag_puts(o, FW_BUILD_STAMP);
      break;

    case DGD_FREE_RAM:
      diag_putu(o, f.free_heap);
      diag_puts(o, " free / ");
      diag_putu(o, f.min_heap);
      diag_puts(o, " min / ");
      diag_putu(o, f.max_alloc);
      diag_puts(o, " maxblk B");
      // The 59 kB of STATIC globals the size gate polices is not readable at
      // run time and never will be. Said, so nobody looks for it here.
      diag_puts(o, " (heap only; statics are a link-time number)");
      break;

    case DGD_BATTERY:
      // n/a, AND WHY. There is no PIN_BATT, no divider fitted, no ADC read
      // anywhere in the tree; core/config.h reserves GPIO0 for one under D10
      // and PB_PINS_CONFIRMED is never defined. Printing a fabricated
      // percentage would be the single most misleading thing this console
      // could do.
      diag_puts(o, "n/a (no divider fitted; D10 open, PB_PINS_CONFIRMED unset)");
      break;

    case DGD_WIFI:
      diag_puts(o, "mode=");   diag_putu(o, f.net_mode);
      diag_puts(o, " phase="); diag_putu(o, f.net_phase);
      diag_puts(o, " err=");   diag_putu(o, f.net_err);
      diag_puts(o, " ap=");    diag_puts(o, f.ap_up ? "up" : "-");
      break;

    case DGD_BLE:
      // Not "off": GONE. P8-C0 deleted the stack, so there is no state to read
      // and inventing a field for one would be a lie with a version number.
      diag_puts(o, "none (removed in P8-C0, decision D2)");
      break;

    case DGD_CLOCK:
      diag_putu(o, f.now_epoch);
      diag_puts(o, " cal=");   fmt_cal(o, f.cal_state);
      diag_puts(o, " valid="); diag_putu(o, f.clock_valid ? 1u : 0u);
      diag_puts(o, " seen=");  diag_putu(o, f.last_seen);
      diag_puts(o, " skew=");  diag_puti(o, f.skew_s);
      diag_puts(o, "s");
      break;

    case DGD_SAVE_VER:
      // THREE numbers, because they answer three questions: what this firmware
      // writes, what the save on flash actually said, and whether the migration
      // ran. Only the first was ever visible.
      diag_puts(o, "fw=");      diag_putu(o, (uint32_t)SAVE_SCHEMA_VERSION);
      diag_puts(o, " onflash="); diag_putu(o, f.save_schema);
      diag_puts(o, " migrated="); diag_putu(o, f.save_migrated ? 1u : 0u);
      diag_puts(o, " content=0x"); diag_puthex(o, f.content_version, 4);
      break;

    case DGD_PEBBLES:
      diag_putu(o, f.pebbles);
      diag_puts(o, "/");
      diag_putu(o, f.box_cap ? f.box_cap : (uint8_t)BOX_SLOTS);
      break;

    case DGD_LAST_SCAN:
      if (!f.scan_valid) {
        // ui/screen_network.h's counters are ui-layer file statics that die at
        // reboot; there is no timestamp and never was one.
        diag_puts(o, "none this boot");
      } else {
        diag_putu(o, f.scan_seen);
        diag_puts(o, " seen / ");
        diag_putu(o, f.scan_fresh);
        diag_puts(o, " fresh, phase=");
        diag_putu(o, f.scan_phase);
      }
      break;

    case DGD_LAST_ERR:
      // FOUR unjoined sources joined here for the first time. LoadResult in
      // particular was a LOCAL in app.cpp printed once at boot and dropped.
      diag_puts(o, "load=");   diag_putu(o, f.load_result);
      diag_puts(o, " nvs=");   diag_puts(o, f.kv_healthy ? "OK" : "BAD");
      diag_puts(o, " kverr=0x"); diag_puthex(o, f.kv_error, 2);
      diag_puts(o, " wfails="); diag_putu(o, f.kv_write_fails);
      diag_puts(o, " radio=");  diag_putu(o, f.net_err);
      diag_puts(o, " quar=0x"); diag_puthex(o, f.quarantine_mask, 4);
      break;

    case DGD_PROTOCOL:
      diag_putu(o, (uint32_t)PROTOCOL_VERSION);
      break;

    default:
      diag_puts(o, "?");
      break;
  }
}

void diag_fmt_all(const DiagFields& f, DiagOut& o)
{
  for (uint8_t i = 0; i < (uint8_t)DGD_FIELD_COUNT; ++i) {
    diag_fmt_field(i, f, o);
    diag_nl(o);
  }
}

// =============================================================================
//  7. THE COMPILE-TIME GUARDS
//     The release split is an invariant, not a convention, so it is checked
//     where it cannot be forgotten. tests/test_diag.cpp walks the same rule at
//     run time; this catches it before a binary exists.
// =============================================================================
static constexpr bool table_is_indexed_by_id(void)
{
  for (uint8_t i = 0; i < (uint8_t)DGC_COUNT; ++i) {
    if (kCmds[i].id != i) return false;
  }
  return true;
}
static_assert(table_is_indexed_by_id(),
              "diag_core.cpp: kCmds is no longer index-parallel to DiagCmdId - "
              "diag_cmd_spec() indexes it directly");

static constexpr bool always_means_read_only(void)
{
  for (uint8_t i = 0; i < (uint8_t)DGC_COUNT; ++i) {
    if ((kCmds[i].flags & DCF_ALWAYS) && (kCmds[i].flags & DCF_MUTATES)) return false;
  }
  return true;
}
static_assert(always_means_read_only(),
              "a DCF_ALWAYS command mutates state - it would ship in the "
              "GOD_MODE_ENABLED 0 artefact, which spec 66's closing line "
              "forbids for dangerous commands (diag_core.h, the release split)");

static constexpr bool argc_fits(void)
{
  for (uint8_t i = 0; i < (uint8_t)DGC_COUNT; ++i) {
    if (kCmds[i].argc_max > (uint8_t)DIAG_ARG_MAX) return false;
    if (kCmds[i].argc_min > kCmds[i].argc_max)     return false;
  }
  return true;
}
static_assert(argc_fits(), "a command wants more arguments than DiagParse holds");

static_assert((int)DGD_FIELD_COUNT == 12,
              "spec section 49 lists twelve fields; the table must carry all of them");
