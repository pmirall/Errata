// =============================================================================
//  tests/tools/balance_matrix.cpp - THE AI-vs-AI WIN-RATE MATRIX (P9-C4).
//
//  Every ordered pair of the 60-species roster, at ONE equal level, fought
//  1,000 times by game/battle_ai.cpp against itself over game/battle.cpp, and
//  printed as a table a person reads. It is the instrument the plan's
//  "flag > 65/35 splits and any type < 40 % aggregate" line asks for.
//
//  NOT part of `make check`: it prints a report and answers a human, which is
//  what tests/tools/ means. `make -C tests balancetool` builds it.
//
// -----------------------------------------------------------------------------
//  WHAT THIS MEASURES, AND - MORE IMPORTANTLY - WHAT IT DOES NOT
// -----------------------------------------------------------------------------
//  IT MEASURES THE GREEDY-vs-GREEDY METAGAME AND NOTHING WIDER. game/battle_ai.h
//  calls its chooser "deliberately not more than" a greedy ranker, and
//  battle_ai.cpp scores every power-0 move at 0 - so PROTECT_HALF, the three
//  BUFF_* moves, CLEANSE and the DOT are chosen ONLY when every damaging move
//  is on cooldown. A species whose kit is built around a status move therefore
//  reads as weak HERE for a reason that belongs to the AI and not to the roster.
//  Before moving a number because of a cell in this table, check whether the
//  loser's kit is one this chooser can even use.
//
//  IT IS ALSO NOT tools/content/verify.py's MATRIX. That one is a PYTHON matrix
//  over tools/content/sim_engine.py, and game/battle.h names five ways that
//  simulator diverges from this engine (priority, stage durations, the
//  cross-multiplied timeout, ties, forced replacement). The two numbers are
//  about two different games and must not be quoted as one.
//
//  AND IT IS 1v1. The plan's box says 1v1 and so does this file; a 3v3 sweep
//  costs about four times as much and measures the SWITCH rule as much as the
//  roster (game/battle_ai.h records that rule 3 cannot fire at all for a
//  mono-type team, which every shipped family is).
//
// -----------------------------------------------------------------------------
//  THE THREE THINGS THE SHAPE OF THIS PROBLEM INVITES YOU TO GET WRONG
// -----------------------------------------------------------------------------
//   1. SEEDING THE TWO AIs TOGETHER. game/battle_ai.h records that
//      rng_next_below(r, 2) is one xorshift32 step, so two AI seeds that differ
//      only in their low bits break every two-way tie identically.
//      battle_ai_init() mixes the side in, which fixes it - but a driver that
//      derives all three seeds from one counter by XOR is leaning on that fix
//      rather than testing with it. THE THREE SEEDS HERE COME FROM THREE
//      DIFFERENT MIXES of (a, b, k): see seed3().
//
//   2. NOT SWEEPING BOTH ORDERS. Side 0 is not side 1: it is the side whose
//      priority tie is broken first (battle_s3_determine_order), and a driver
//      bug that favours one side would be invisible in a half-grid. Both
//      (a,b) and (b,a) are fought, and --self-check requires the ALL-BATTLES
//      side-0 share to sit inside SYMMETRY_BAND_PM of 500 permille.
//
//   3. FLAGGING EVERY CROSS-STAGE CELL. The roster is 20 families x 3 stages
//      with base-stat totals of exactly 16 / 22 / 28 (tools/content/verify.py
//      asserts the zero variance), so a stage-0 body losing 90/10 to a stage-2
//      one at the same level is the DESIGN and not a defect. The 65/35 flag is
//      therefore applied to SAME-STAGE cells, which is the population the pack's
//      own balance claim is about; cross-stage cells get their own, different
//      check - the later stage must win - and a cross-stage cell where the
//      EARLIER stage wins is flagged, because that one really is a defect.
//
// -----------------------------------------------------------------------------
//  THE FIXTURE
// -----------------------------------------------------------------------------
//  Both fighters carry a genome whose every numeric gene is 8, i.e.
//  bug_genome_var() == 1 on all three stats. That is the MODAL genesis roll
//  and the point game/bug.h says the pack's whole win-rate matrix was
//  measured at ("gvar = (1,1,1,1)"). A zeroed genome would also be even-handed
//  but would be a different point on the curve.
//
//  Each side gets its species' own four-move learnset, straight out of
//  SPECIES_TABLE, so battle_init()'s BR_UNLEARNABLE_MOVE guard is satisfied by
//  the content rather than by a fixture.
//
//  All identifiers and comments English.
// =============================================================================
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "data/balance.h"
#include "data/species_table.h"
#include "game/battle.h"
#include "game/battle_ai.h"
#include "game/genome.h"
#include "game/bug.h"
#include "game/xp.h"

// The defaults. --level and --seeds move them; the grid never shrinks.
#define BM_LEVEL_DEFAULT    15u
#define BM_SEEDS_DEFAULT    1000u

// The two thresholds the plan names, in permille so no float is needed.
#define BM_LOPSIDED_PM      650u    // "> 65/35"
#define BM_TYPE_FLOOR_PM    400u    // "any type < 40 % aggregate"

// The side-0 share the whole sweep must land inside once both orders are
// fought. 500 +- 15 permille: a 3.6 M-battle sweep whose side-0 share sits
// outside that is a driver bug, not roster noise.
#define BM_SYMMETRY_BAND_PM 15u

#define BM_TYPES 4   // SIGNAL, CORRUPT, SYSTEM, NEUTRAL (data/attacks_table.h)

static const char* const TYPE_NAME[BM_TYPES] = { "SIGNAL", "CORRUPT", "SYSTEM", "NEUTRAL" };

// -----------------------------------------------------------------------------
//  THREE SEEDS FROM ONE CELL, BY THREE DIFFERENT MIXES.
//  murmur3's fmix32 finaliser, entered with three different constant salts, so
//  the battle stream and the two AI streams cannot be near-neighbours of one
//  another for any (a, b, k). See note 1 in the banner.
// -----------------------------------------------------------------------------
static uint32_t fmix32(uint32_t h)
{
  h ^= h >> 16; h *= 0x85EBCA6Bu;
  h ^= h >> 13; h *= 0xC2B2AE35u;
  h ^= h >> 16;
  return h;
}

static uint32_t seed3(uint8_t a, uint8_t b, uint32_t k, uint32_t salt)
{
  uint32_t h = salt;
  h = fmix32(h ^ ((uint32_t)a * 0x9E3779B9u));
  h = fmix32(h ^ ((uint32_t)b * 0x85EBCA6Bu));
  h = fmix32(h ^ (k * 0xC2B2AE35u));
  return h ? h : 1u;   // rng_init() remaps 0 anyway; be explicit
}

// -----------------------------------------------------------------------------
//  ONE FIGHTER
// -----------------------------------------------------------------------------
static Genome g_modal;    // every numeric gene 8 -> gvar (1,1,1)

static void build_modal_genome(void)
{
  memset(&g_modal, 0, sizeof g_modal);
  g_modal.magic_ver  = (uint16_t)GENOME_MAGIC_VER;
  g_modal.lineage_id = 0x0BA5EU;       // any non-zero dynasty
  gene_set_metabolism(g_modal, 8u);    // -> spd variation 1
  gene_set_temperament(g_modal, 8u);   // -> atk variation 1
  gene_set_hardiness(g_modal, 8u);     // -> def variation 1
  genome_seal(g_modal);
}

static void mk_member(BugInstance& p, uint8_t species, uint8_t level, uint32_t id)
{
  memset(&p, 0, sizeof p);
  p.magic      = (uint16_t)BUG_MAGIC;
  p.layout_ver = (uint8_t)BUG_LAYOUT_VER;
  p.species_id = species;
  p.id         = id;
  p.level      = level;
  p.genome     = g_modal;
  const SpeciesDef* sp = species_get(species);
  if (sp == nullptr) return;
  for (uint8_t m = 0; m < (uint8_t)ER_MOVE_COUNT; ++m) p.moves[m] = sp->moves[m];
  BugStats st;
  bug_derive_stats(*sp, level, p.genome, st);
  p.hp_cur = st.hp_max;
}

// -----------------------------------------------------------------------------
//  ONE BATTLE. Returns the outcome; *rounds receives how long it took.
//  Nothing here is allowed to write BattleState except the engine: the two AIs
//  see it through the const queries only, which is the property game/battle_ai.h
//  is built around.
// -----------------------------------------------------------------------------
static BattleOutcome one_battle(uint8_t a, uint8_t b, uint8_t level, uint32_t k,
                                uint16_t* rounds)
{
  BattleSetup s;
  battle_setup_clear(s);
  s.seed     = seed3(a, b, k, 0x5A17B00Du);
  s.count[0] = 1u;
  s.count[1] = 1u;
  mk_member(s.member[0][0], a, level, 0x1000u + k);
  mk_member(s.member[1][0], b, level, 0x2000u + k);

  BattleState st;
  if (battle_init(st, s) != BR_OK) { if (rounds) *rounds = 0u; return BO_ABORT; }

  BattleAi ai0, ai1;
  battle_ai_init(ai0, 0u, seed3(a, b, k, 0xA1A1A1A1u));
  battle_ai_init(ai1, 1u, seed3(a, b, k, 0xB2B2B2B2u));

  uint16_t r = 0;
  for (;;) {
    const BattleAction act0 = battle_ai_choose(ai0, st);
    const BattleAction act1 = battle_ai_choose(ai1, st);
    // BACT_NONE means the side has no legal action at all; the engine refuses
    // it by name, so submitting it is safe and the round still resolves.
    (void)battle_submit_action(st, 0u, act0);
    (void)battle_submit_action(st, 1u, act1);
    const BattleStepResult sr = battle_step_round(st, nullptr);
    if (sr == BS_BATTLE_OVER) break;
    if (sr == BS_NEED_ACTIONS) break;   // neither side could act: a stuck state
    if (++r > (uint16_t)BATTLE_MAX_ROUNDS + 4u) break;
  }
  if (rounds) *rounds = st.round;
  return (BattleOutcome)st.outcome;
}

// -----------------------------------------------------------------------------
//  THE GRID
// -----------------------------------------------------------------------------
#define BM_MAX 64   // SPECIES_TABLE_COUNT today is 60; the arrays are static

static uint32_t g_win[BM_MAX][BM_MAX];    // [a][b] = battles a won as SIDE 0
static uint32_t g_los[BM_MAX][BM_MAX];
static uint32_t g_drw[BM_MAX][BM_MAX];

static uint8_t sp_stage(uint8_t id) { const SpeciesDef* s = species_get(id); return s ? s->stage : 0u; }
static uint8_t sp_type(uint8_t id)  { const SpeciesDef* s = species_get(id); return s ? s->type  : 0u; }
static uint8_t sp_rarity(uint8_t id){ const SpeciesDef* s = species_get(id); return s ? s->rarity: 0u; }

// permille of `num` out of `den`, rounded to nearest, 0 for an empty sample.
static uint32_t pm(uint32_t num, uint32_t den)
{
  if (den == 0u) return 0u;
  return (uint32_t)((2u * 1000u * (uint64_t)num + den) / (2u * (uint64_t)den));
}

int main(int argc, char** argv)
{
  uint8_t  level = (uint8_t)BM_LEVEL_DEFAULT;
  uint32_t seeds = (uint32_t)BM_SEEDS_DEFAULT;
  int      want_cells = 0;      // --cells prints every same-stage cell
  int      self_check = 0;

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--level") == 0 && i + 1 < argc) level = (uint8_t)atoi(argv[++i]);
    else if (strcmp(argv[i], "--seeds") == 0 && i + 1 < argc) seeds = (uint32_t)strtoul(argv[++i], nullptr, 10);
    else if (strcmp(argv[i], "--cells") == 0) want_cells = 1;
    else if (strcmp(argv[i], "--self-check") == 0) self_check = 1;
    else {
      fprintf(stderr, "usage: balance_matrix [--level N] [--seeds N] [--cells] [--self-check]\n");
      return 2;
    }
  }
  if (level < 1u || level > (uint8_t)ER_LEVEL_MAX) { fprintf(stderr, "level out of range\n"); return 2; }
  if (seeds == 0u) { fprintf(stderr, "seeds must be >= 1\n"); return 2; }

  build_modal_genome();

  const uint8_t n = (uint8_t)SPECIES_TABLE_COUNT;
  if (n > (uint8_t)BM_MAX) { fprintf(stderr, "roster larger than BM_MAX\n"); return 2; }

  printf("BALANCE MATRIX  roster=%u  level=%u  seeds/ordered-pair=%u  "
         "battles=%llu  engine=%u content=%04X\n",
         (unsigned)n, (unsigned)level, (unsigned)seeds,
         (unsigned long long)((uint64_t)n * n * seeds),
         (unsigned)BATTLE_ENGINE_VER, (unsigned)CONTENT_VERSION);
  printf("1v1, both sides driven by game/battle_ai.cpp. Genome: every numeric "
         "gene 8 (gvar 1,1,1).\n\n");

  const clock_t t0 = clock();

  memset(g_win, 0, sizeof g_win);
  memset(g_los, 0, sizeof g_los);
  memset(g_drw, 0, sizeof g_drw);

  uint64_t rounds_total = 0, battles_total = 0, aborts = 0;

  for (uint8_t a = 1; a <= n; ++a) {
    for (uint8_t b = 1; b <= n; ++b) {
      for (uint32_t k = 0; k < seeds; ++k) {
        uint16_t rr = 0;
        const BattleOutcome o = one_battle(a, b, level, k, &rr);
        rounds_total += rr;
        ++battles_total;
        if (o == BO_WIN_A)      ++g_win[a - 1][b - 1];
        else if (o == BO_WIN_B) ++g_los[a - 1][b - 1];
        else if (o == BO_DRAW)  ++g_drw[a - 1][b - 1];
        else                    ++aborts;
      }
    }
  }

  const double secs = (double)(clock() - t0) / (double)CLOCKS_PER_SEC;

  // ---------------------------------------------------------------------------
  //  SELF-CHECK 1: SIDE SYMMETRY over the whole sweep.
  //  Every ordered pair is fought in both orders, so the side-0 win share over
  //  ALL battles is 500 permille unless the driver favours a side.
  // ---------------------------------------------------------------------------
  uint64_t all_s0 = 0, all_s1 = 0, all_dr = 0;
  for (uint8_t a = 0; a < n; ++a)
    for (uint8_t b = 0; b < n; ++b) {
      all_s0 += g_win[a][b]; all_s1 += g_los[a][b]; all_dr += g_drw[a][b];
    }
  (void)all_dr;
  const uint64_t decided_all = all_s0 + all_s1;
  const uint32_t side0_share_pm =
      decided_all ? (uint32_t)((2000u * all_s0 + decided_all) / (2u * decided_all)) : 0u;

  printf("SWEEP: %llu battles in %.2f s (%.2f us/battle), mean %.2f rounds, "
         "%llu aborts\n",
         (unsigned long long)battles_total, secs,
         battles_total ? 1e6 * secs / (double)battles_total : 0.0,
         battles_total ? (double)rounds_total / (double)battles_total : 0.0,
         (unsigned long long)aborts);
  printf("SIDE SYMMETRY: side-0 took %u permille of the %llu decided battles "
         "(band 500 +- %u)\n\n",
         side0_share_pm, (unsigned long long)decided_all,
         (unsigned)BM_SYMMETRY_BAND_PM);

  // ---------------------------------------------------------------------------
  //  THE UNORDERED CELLS.
  //  Cell {a,b} pools both orders: a's wins are g_win[a][b] + g_los[b][a].
  //
  //  THREE DIFFERENT QUESTIONS COME OUT OF THE SAME GRID AND THEY ARE NOT ONE
  //  QUESTION, which is the mistake the first draft of this file made:
  //
  //   (A) IS ONE CELL LOPSIDED?  Almost all of them are, and that is the type
  //       triangle working. A x5/4 multiplier on every hit against a x4/5 one
  //       is a 56 % damage difference, and two fighters with the same stats and
  //       a type edge between them SHOULD end 80/20. Counting lopsided cells is
  //       therefore a MEASUREMENT, not a defect list, and it is printed as a
  //       count with the worst few named rather than as 380 lines nobody reads.
  //       --cells prints them all.
  //
  //   (B) IS ONE SPECIES LOPSIDED AGAINST ITS WHOLE STAGE?  THIS is the number a
  //       JSON edit can move, and it is what tools/content/verify.py asserts
  //       over its Python model ("stage-1 spread is tight", "mean win rate rises
  //       with rarity"). A species outside 35-65 % against every same-stage
  //       species is over- or under-tuned; a species inside it is fine no matter
  //       how many individual cells it wins 90/10.
  //
  //   (C) IS AN EVOLUTION AN UPGRADE?  Only answerable WITHIN A FAMILY. Across
  //       families a stage-1 body with a type edge beating a stage-2 one is the
  //       triangle again and not a defect - so the hard check is
  //       family F stage k vs family F stage k+1, and the cross-family
  //       cross-stage upsets are printed separately with their type modifier so
  //       a reader can see which explanation applies.
  // ---------------------------------------------------------------------------
  uint32_t lop_same = 0, same_cells = 0, cross_cells = 0;
  uint32_t worst_off = 0, worst_a = 0, worst_b = 0, worst_pm = 500u;

  for (uint8_t ia = 0; ia < n; ++ia) {
    for (uint8_t ib = (uint8_t)(ia + 1u); ib < n; ++ib) {
      const uint8_t a = (uint8_t)(ia + 1u), b = (uint8_t)(ib + 1u);
      const uint32_t aw = g_win[ia][ib] + g_los[ib][ia];
      const uint32_t bw = g_los[ia][ib] + g_win[ib][ia];
      const uint32_t dec = aw + bw;
      if (dec == 0u) continue;
      const uint32_t apm = pm(aw, dec);
      if (sp_stage(a) == sp_stage(b)) {
        ++same_cells;
        const uint32_t off = (apm > 500u) ? (apm - 500u) : (500u - apm);
        if (off > worst_off) { worst_off = off; worst_a = a; worst_b = b; worst_pm = apm; }
        if (apm >= BM_LOPSIDED_PM || apm <= 1000u - BM_LOPSIDED_PM) ++lop_same;
      } else {
        ++cross_cells;
      }
    }
  }
  printf("=== (A) SAME-STAGE CELLS OVER %u/%u ===\n",
         (unsigned)(BM_LOPSIDED_PM / 10u), (unsigned)(100u - BM_LOPSIDED_PM / 10u));
  printf("  %u of %u (%u.%u %%). Worst cell: %u vs %u at %u/%u.\n",
         lop_same, same_cells, pm(lop_same, same_cells) / 10u, pm(lop_same, same_cells) % 10u,
         worst_a, worst_b, worst_pm / 10u, (1000u - worst_pm) / 10u);
  printf("  A LOPSIDED CELL IS NOT BY ITSELF A DEFECT - see the banner. The\n"
         "  actionable number is (B) below.\n\n");

  // ---------------------------------------------------------------------------
  //  (B) PER-SPECIES AGGREGATE AGAINST ITS OWN STAGE.
  // ---------------------------------------------------------------------------
  uint32_t sp_pm[BM_MAX];
  uint32_t sp_n[BM_MAX];
  uint32_t sp_out = 0;
  for (uint8_t ia = 0; ia < n; ++ia) {
    const uint8_t a = (uint8_t)(ia + 1u);
    uint64_t w = 0, l = 0;
    for (uint8_t ib = 0; ib < n; ++ib) {
      const uint8_t b = (uint8_t)(ib + 1u);
      if (a == b || sp_stage(b) != sp_stage(a)) continue;
      w += g_win[ia][ib] + g_los[ib][ia];
      l += g_los[ia][ib] + g_win[ib][ia];
    }
    sp_n[ia]  = (uint32_t)(w + l);
    sp_pm[ia] = (w + l) ? (uint32_t)((2000u * w + (w + l)) / (2u * (w + l))) : 500u;
  }
  printf("=== (B) EACH SPECIES AGAINST ITS OWN STAGE (the number a JSON edit moves) ===\n");
  printf("  outside %u-%u %%:\n", (unsigned)(100u - BM_LOPSIDED_PM / 10u),
         (unsigned)(BM_LOPSIDED_PM / 10u));
  for (uint8_t ia = 0; ia < n; ++ia) {
    const uint8_t a = (uint8_t)(ia + 1u);
    if (sp_pm[ia] < BM_LOPSIDED_PM && sp_pm[ia] > 1000u - BM_LOPSIDED_PM) continue;
    ++sp_out;
    const SpeciesDef* s = species_get(a);
    printf("    id %-3u fam %-3u st%u %-8s rarity %u  hp%2u atk%2u def%2u spd%2u  "
           "total %2u   %u.%u %%  (n=%u)\n",
           a, s ? s->family : 0u, sp_stage(a), TYPE_NAME[sp_type(a) % BM_TYPES],
           sp_rarity(a),
           s ? s->base_hp : 0u, s ? s->base_atk : 0u, s ? s->base_def : 0u,
           s ? s->base_spd : 0u,
           s ? (unsigned)(s->base_hp + s->base_atk + s->base_def + s->base_spd) : 0u,
           sp_pm[ia] / 10u, sp_pm[ia] % 10u, sp_n[ia]);
  }
  if (sp_out == 0u) printf("    (none)\n");
  printf("  %u of %u species are outside the band.\n\n", sp_out, n);

  // ---------------------------------------------------------------------------
  //  (C) IS AN EVOLUTION AN UPGRADE? Within a family, at equal level.
  // ---------------------------------------------------------------------------
  printf("=== (C) WITHIN-FAMILY: DOES THE LATER STAGE WIN AT EQUAL LEVEL? ===\n");
  uint32_t evo_backwards = 0, evo_cells = 0;
  for (uint8_t ia = 0; ia < n; ++ia) {
    for (uint8_t ib = 0; ib < n; ++ib) {
      const uint8_t a = (uint8_t)(ia + 1u), b = (uint8_t)(ib + 1u);
      const SpeciesDef* sa = species_get(a);
      const SpeciesDef* sb = species_get(b);
      if (!sa || !sb || sa->family != sb->family || sa->stage >= sb->stage) continue;
      ++evo_cells;
      const uint32_t aw = g_win[ia][ib] + g_los[ib][ia];
      const uint32_t bw = g_los[ia][ib] + g_win[ib][ia];
      if (aw + bw == 0u) continue;
      const uint32_t bpm = pm(bw, aw + bw);       // the LATER stage's share
      if (bpm < 500u) {
        ++evo_backwards;
        printf("    fam %u: id %u (st%u) LOSES to id %u (st%u) - %u/%u\n",
               sa->family, b, sb->stage, a, sa->stage, bpm / 10u, (1000u - bpm) / 10u);
      }
    }
  }
  if (evo_backwards == 0u)
    printf("    every one of the %u within-family cross-stage cells goes to the later stage\n",
           evo_cells);
  printf("\n");

  printf("=== (C') CROSS-FAMILY CROSS-STAGE UPSETS (informational) ===\n");
  printf("  A stage-1 body beating a stage-2 body of ANOTHER family is the type\n"
         "  triangle, not a defect; the modifier is printed so a reader can tell.\n");
  uint32_t xf_upsets = 0;
  for (uint8_t ia = 0; ia < n; ++ia) {
    for (uint8_t ib = (uint8_t)(ia + 1u); ib < n; ++ib) {
      const uint8_t a = (uint8_t)(ia + 1u), b = (uint8_t)(ib + 1u);
      const SpeciesDef* sa = species_get(a);
      const SpeciesDef* sb = species_get(b);
      if (!sa || !sb || sa->family == sb->family || sa->stage == sb->stage) continue;
      const uint32_t aw = g_win[ia][ib] + g_los[ib][ia];
      const uint32_t bw = g_los[ia][ib] + g_win[ib][ia];
      if (aw + bw == 0u) continue;
      const uint32_t apm = pm(aw, aw + bw);
      const uint8_t later_is_a = (uint8_t)(sa->stage > sb->stage);
      const uint32_t later_pm = later_is_a ? apm : (1000u - apm);
      if (later_pm >= 500u) continue;
      ++xf_upsets;
      const uint8_t lo = later_is_a ? b : a, hi = later_is_a ? a : b;
      printf("    id %u (st%u %s) beats id %u (st%u %s) %u/%u   type_mod(lo->hi)=%+d\n",
             lo, sp_stage(lo), TYPE_NAME[sp_type(lo) % BM_TYPES],
             hi, sp_stage(hi), TYPE_NAME[sp_type(hi) % BM_TYPES],
             (1000u - later_pm) / 10u, later_pm / 10u,
             (int)type_mod_of(sp_type(lo), sp_type(hi)));
    }
  }
  printf("    %u of %u cross-family cross-stage cells\n\n", xf_upsets, cross_cells);

  // ---------------------------------------------------------------------------
  //  (D) PER-TYPE AGGREGATE.
  //  Over CROSS-TYPE battles only: a mirror-type cell is 50 % by construction
  //  and pooling it in drags every type toward the middle, which would make the
  //  40 % floor unreachable and the check unfailable.
  // ---------------------------------------------------------------------------
  uint64_t tw[BM_TYPES], tl[BM_TYPES];
  memset(tw, 0, sizeof tw); memset(tl, 0, sizeof tl);
  // ...and the same split restricted to SAME-STAGE battles, which is the
  // population the pack tuned; the all-stages figure is dominated by whichever
  // type happens to sit on more stage-2 bodies.
  uint64_t sw[BM_TYPES], sl[BM_TYPES];
  memset(sw, 0, sizeof sw); memset(sl, 0, sizeof sl);
  // The type EDGE itself: how often the side holding type_mod +1 wins.
  uint64_t edge_w = 0, edge_l = 0;
  for (uint8_t ia = 0; ia < n; ++ia) {
    for (uint8_t ib = 0; ib < n; ++ib) {
      const uint8_t a = (uint8_t)(ia + 1u), b = (uint8_t)(ib + 1u);
      const uint8_t ta = sp_type(a) % BM_TYPES, tb = sp_type(b) % BM_TYPES;
      if (ta == tb) continue;
      tw[ta] += g_win[ia][ib]; tl[ta] += g_los[ia][ib];
      tw[tb] += g_los[ia][ib]; tl[tb] += g_win[ia][ib];
      if (sp_stage(a) != sp_stage(b)) continue;
      sw[ta] += g_win[ia][ib]; sl[ta] += g_los[ia][ib];
      sw[tb] += g_los[ia][ib]; sl[tb] += g_win[ia][ib];
      if (type_mod_of(sp_type(a), sp_type(b)) > 0) {
        edge_w += g_win[ia][ib]; edge_l += g_los[ia][ib];
      }
    }
  }
  printf("=== (D) PER-TYPE AGGREGATE (cross-type battles only) ===\n");
  printf("  %-8s  %-22s  %-22s\n", "type", "all stages", "same stage only");
  uint32_t types_under = 0;
  for (uint8_t t = 0; t < BM_TYPES; ++t) {
    const uint64_t dec = tw[t] + tl[t];
    if (dec == 0u) {
      printf("  %-8s  no cross-type battles (no species carries it)\n", TYPE_NAME[t]);
      continue;
    }
    const uint32_t p  = (uint32_t)((2000u * tw[t] + dec) / (2u * dec));
    const uint64_t sd = sw[t] + sl[t];
    const uint32_t sp = sd ? (uint32_t)((2000u * sw[t] + sd) / (2u * sd)) : 500u;
    const int under = (p < BM_TYPE_FLOOR_PM) || (sd && sp < BM_TYPE_FLOOR_PM);
    if (under) ++types_under;
    printf("  %-8s  %u.%u %% of %-12llu  %u.%u %% of %-12llu%s\n",
           TYPE_NAME[t], p / 10u, p % 10u, (unsigned long long)dec,
           sp / 10u, sp % 10u, (unsigned long long)sd,
           under ? "  << UNDER 40 %" : "");
  }
  {
    const uint64_t ed = edge_w + edge_l;
    const uint32_t ep = ed ? (uint32_t)((2000u * edge_w + ed) / (2u * ed)) : 500u;
    printf("  THE EDGE ITSELF: the side holding type_mod +1 wins %u.%u %% of the "
           "%llu decided same-stage cross-type battles\n"
           "  (spec 12 wants this to MATTER without DECIDING; the pack's own "
           "Python proxy asserts 54-65 %%)\n",
           ep / 10u, ep % 10u, (unsigned long long)ed);
  }
  printf("\n");

  // ---------------------------------------------------------------------------
  //  MEAN WIN RATE BY RARITY, WITHIN A STAGE.
  //  tools/content/verify.py asserts this over the Python model; it is printed
  //  here over the real engine so the two claims can be compared by eye.
  // ---------------------------------------------------------------------------
  printf("=== MEAN SAME-STAGE WIN RATE BY RARITY ===\n");
  for (uint8_t stg = 0; stg < 3u; ++stg) {
    printf("  stage %u:", stg);
    for (uint8_t rar = 0; rar < 4u; ++rar) {
      uint64_t w = 0, l = 0;
      for (uint8_t ia = 0; ia < n; ++ia) {
        const uint8_t a = (uint8_t)(ia + 1u);
        if (sp_stage(a) != stg || sp_rarity(a) != rar) continue;
        for (uint8_t ib = 0; ib < n; ++ib) {
          const uint8_t b = (uint8_t)(ib + 1u);
          if (a == b || sp_stage(b) != stg) continue;
          w += g_win[ia][ib] + g_los[ib][ia];
          l += g_los[ia][ib] + g_win[ib][ia];
        }
      }
      if (w + l == 0u) { printf("  r%u n/a", rar); continue; }
      const uint32_t p = (uint32_t)((2000u * w + (w + l)) / (2u * (w + l)));
      printf("  r%u %u.%u%%", rar, p / 10u, p % 10u);
    }
    printf("\n");
  }
  printf("\n");

  if (want_cells) {
    printf("=== EVERY SAME-STAGE CELL ===\n");
    for (uint8_t ia = 0; ia < n; ++ia)
      for (uint8_t ib = (uint8_t)(ia + 1u); ib < n; ++ib) {
        const uint8_t a = (uint8_t)(ia + 1u), b = (uint8_t)(ib + 1u);
        if (sp_stage(a) != sp_stage(b)) continue;
        const uint32_t aw = g_win[ia][ib] + g_los[ib][ia];
        const uint32_t bw = g_los[ia][ib] + g_win[ib][ia];
        if (aw + bw == 0u) continue;
        printf("  %3u vs %3u  %3u/%-3u\n", a, b, pm(aw, aw + bw) / 10u,
               (1000u - pm(aw, aw + bw)) / 10u);
      }
    printf("\n");
  }

  // ---------------------------------------------------------------------------
  //  THE EXIT CODE. Only --self-check turns findings into a failure: the plain
  //  run is a REPORT, and a report that exits non-zero cannot be piped into a
  //  review without somebody adding `|| true` and losing the signal for good.
  // ---------------------------------------------------------------------------
  if (self_check) {
    int bad = 0;
    const uint32_t off = (side0_share_pm > 500u) ? (side0_share_pm - 500u)
                                                 : (500u - side0_share_pm);
    if (off > (uint32_t)BM_SYMMETRY_BAND_PM) {
      printf("SELF-CHECK FAIL: side-0 share %u permille is outside 500 +- %u - "
             "the driver favours a side\n", side0_share_pm, (unsigned)BM_SYMMETRY_BAND_PM);
      ++bad;
    }
    if (aborts != 0u) {
      printf("SELF-CHECK FAIL: %llu battles ended BO_ABORT or failed battle_init()\n",
             (unsigned long long)aborts);
      ++bad;
    }
    if (types_under != 0u) {
      printf("SELF-CHECK FAIL: %u type(s) under %u %% aggregate\n",
             types_under, (unsigned)(BM_TYPE_FLOOR_PM / 10u));
      ++bad;
    }
    if (evo_backwards != 0u) {
      printf("SELF-CHECK FAIL: %u within-family cross-stage cell(s) go to the "
             "EARLIER stage - an evolution that is not an upgrade\n",
             evo_backwards);
      ++bad;
    }
    if (bad) return 1;
    printf("SELF-CHECK OK: symmetric, no aborts, every type at or over %u %% "
           "(all stages AND same stage), every within-family evolution an "
           "upgrade, and %u of %u species inside the %u-%u %% band against their "
           "own stage.\n",
           (unsigned)(BM_TYPE_FLOOR_PM / 10u), (unsigned)(n - sp_out), n,
           (unsigned)(100u - BM_LOPSIDED_PM / 10u), (unsigned)(BM_LOPSIDED_PM / 10u));
  }
  return 0;
}
