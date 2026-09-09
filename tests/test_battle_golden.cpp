// =============================================================================
//  ERRATA host test - test_battle_golden.cpp
//  A FULL SCRIPTED 3v3, RECORDED EVENT BY EVENT (plan P4-C2).
//
//  WHY THIS FILE EXISTS, and it is not the same reason as test_battle_replay:
//
//   1. THE ROUND HASH CANNOT SEE A SHARED OFF-BY-ONE. If both engines expire a
//      buff one round early, the lockstep hashes agree perfectly every round
//      and the battle is still wrong. Only a recorded transcript catches that.
//   2. THE ROUND HASH CANNOT SEE THE LOG AT ALL. battle_state_hash() covers
//      BattleState, and the BattleLog is deliberately outside it (game/battle.h
//      says why), so a log-writing bug is structurally invisible to P4-C5's
//      desync detector. This file is the thing that watches the log.
//
//  Every line is one BattleEvent with its RESULTING values, plus the state hash
//  at both ends of every round. Re-record ONLY from a `retune:` commit - or one
//  that deliberately changes what a round does - and read the diff:
//      make -C tests golden      (or ./bin/test_battle_golden --record)
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "game/battle.h"
#include "game/xp.h"

#define GOLDEN_REL_PATH   "golden/battle_v1.txt"
#define GOLDEN_SEED       0x2468ACE0u
#define GOLDEN_LEVEL      14u
#define GOLDEN_EVENTS     8192
#define GOLDEN_TEXT_MAX   ((size_t)220000)

// Chosen so the recording covers effects, not only damage: species 14 knows
// Depurar (CLEANSE) and species 22 knows Infectar (EFF_CORRUPT). It CANNOT
// cover ATK_EFF_DOT, and that is a fact about the content rather than about
// this file: the only DOT attack is 13 Infeccion, whose one learnset belongs to
// species 46, outside the shipped 36-species prefix - test_content.cpp already
// pins that by name. The DOT rules are covered by test_battle.cpp instead.
static const uint8_t G_A[3] = {  9, 14,  6 };   // SIGNAL
static const uint8_t G_B[3] = { 22, 27, 36 };   // CORRUPT, CORRUPT, SYSTEM

static const char* const EV_NAME[RLE_COUNT] = {
  "NONE", "ROUND_BEGIN", "ACTION", "ORDER", "SWITCH", "SKIPPED", "MISS", "HIT", "TYPE_EDGE",
  "HP", "STAGE", "PROTECT", "DOT", "CORRUPT", "STUN", "CLEANSE", "FAINT",
  "ROUND_END", "BATTLE_END"
};

static void mk_member(BugInstance& p, uint8_t species, uint8_t level, uint32_t id)
{
  memset(&p, 0, sizeof p);
  p.magic      = (uint16_t)BUG_MAGIC;
  p.layout_ver = (uint8_t)BUG_LAYOUT_VER;
  p.species_id = species;
  p.id         = id;
  p.level      = level;
  const SpeciesDef* sp = species_get(species);
  if (sp == nullptr) return;
  for (uint8_t m = 0; m < (uint8_t)ER_MOVE_COUNT; ++m) p.moves[m] = sp->moves[m];
  p.hp_cur = xp_hp_max(sp->base_hp, level);
}

static void mk_setup(BattleSetup& s)
{
  battle_setup_clear(s);
  s.seed     = GOLDEN_SEED;
  s.count[0] = 3u;
  s.count[1] = 3u;
  for (uint8_t i = 0; i < 3u; ++i) {
    mk_member(s.member[0][i], G_A[i], (uint8_t)GOLDEN_LEVEL, 0x0A00u + i);
    mk_member(s.member[1][i], G_B[i], (uint8_t)GOLDEN_LEVEL, 0x0B00u + i);
  }
}

// The same shape of pure, AI-free script test_battle_replay.cpp uses: it walks
// the move slots so buffs, cooldowns, protection, DOTs and forced switches all
// appear in the recording.
static BattleAction script(const BattleState& st, uint8_t side)
{
  BattleAction a; a.kind = (uint8_t)BACT_NONE; a.index = 0u;
  const BattleCombatant* u = battle_active(st, side);
  if (u == nullptr) return a;
  if (battle_side_must_switch(st, side)) {
    for (uint8_t i = 0; i < (uint8_t)BATTLE_TEAM_MAX; ++i) {
      const BattleCombatant* c = battle_combatant(st, side, i);
      if (i != st.side[side].active && c != nullptr &&
          (c->flags & BCF_PRESENT) != 0u && (c->flags & BCF_FAINTED) == 0u &&
          c->hp_cur > 0u) { a.kind = (uint8_t)BACT_SWITCH; a.index = i; return a; }
    }
  }
  // One deliberate voluntary switch, so the recording covers "switching costs
  // the turn" and not only the forced replacement.
  if (st.round == 5u && side == 0u) {
    const BattleCombatant* c = battle_combatant(st, side, 1u);
    if (st.side[side].active != 1u && c != nullptr && c->hp_cur > 0u) {
      a.kind = (uint8_t)BACT_SWITCH; a.index = 1u; return a;
    }
  }
  for (uint8_t k = 0; k < (uint8_t)ER_MOVE_COUNT; ++k) {
    const uint8_t slot = (uint8_t)((st.round * 3u + side + k) % (uint16_t)ER_MOVE_COUNT);
    if (battle_move_ready(*u, slot)) { a.kind = (uint8_t)BACT_ATTACK; a.index = slot; return a; }
  }
  a.kind = (uint8_t)BACT_ATTACK; a.index = 0u;
  return a;
}

static void append(char* text, size_t cap, const char* line)
{
  const size_t used = strlen(text);
  if (used + strlen(line) + 1u <= cap) strcat(text, line);
}

static void golden_run(char* text, size_t cap)
{
  text[0] = '\0';
  char line[192];

  BattleSetup s;
  mk_setup(s);
  BattleState st;
  const BattleReject r = battle_init(st, s);
  CHECK_EQ(r, BR_OK);

  snprintf(line, sizeof line, "seed=%08X engine=%u content=%04X level=%u\n",
           (unsigned)GOLDEN_SEED, (unsigned)BATTLE_ENGINE_VER,
           (unsigned)CONTENT_VERSION, (unsigned)GOLDEN_LEVEL);
  append(text, cap, line);
  for (uint8_t sd = 0; sd < 2u; ++sd) {
    for (uint8_t i = 0; i < 3u; ++i) {
      const BattleCombatant* c = battle_combatant(st, sd, i);
      snprintf(line, sizeof line,
               "team s=%u i=%u spc=%u lvl=%u ty=%u hp=%u atk=%u def=%u spd=%u "
               "mv=%u,%u,%u,%u\n",
               sd, i, c->species_id, c->level, c->type, c->hp_max,
               c->atk, c->def, c->spd,
               c->moves[0], c->moves[1], c->moves[2], c->moves[3]);
      append(text, cap, line);
    }
  }

  static BattleEvent buf[GOLDEN_EVENTS];
  BattleLog log;
  battle_log_init(log, buf, (uint16_t)GOLDEN_EVENTS);

  int guard = 0;
  BattleStepResult res = BS_ROUND_DONE;
  while (res != BS_BATTLE_OVER && guard++ <= BATTLE_MAX_ROUNDS + 1) {
    const BattleAction a = script(st, 0u);
    const BattleAction b = script(st, 1u);
    CHECK_EQ(battle_submit_action(st, 0u, a), BR_OK);
    CHECK_EQ(battle_submit_action(st, 1u, b), BR_OK);
    res = battle_step_round(st, &log);
  }
  CHECK_EQ(log.dropped, 0);          // a holed transcript is not a transcript

  for (uint16_t i = 0; i < log.count; ++i) {
    const BattleEvent* e = battle_log_at(log, i);
    if (e == nullptr) break;
    const char* nm = (e->kind < (uint8_t)RLE_COUNT) ? EV_NAME[e->kind] : "?";
    if (e->kind == (uint8_t)RLE_ROUND_BEGIN || e->kind == (uint8_t)RLE_ROUND_END) {
      snprintf(line, sizeof line, "r=%03u %-11s h=%08X\n",
               (unsigned)e->round, nm, (unsigned)e->v);
    } else {
      snprintf(line, sizeof line, "r=%03u %-11s s=%u i=%u a=%u b=%u\n",
               (unsigned)e->round, nm, (unsigned)e->side, (unsigned)e->slot,
               (unsigned)e->a, (unsigned)e->b);
    }
    append(text, cap, line);
  }

  snprintf(line, sizeof line, "final outcome=%u round=%u hash=%08X rng=%08X\n",
           (unsigned)st.outcome, (unsigned)st.round,
           (unsigned)battle_state_hash(st), (unsigned)st.rng.s);
  append(text, cap, line);
}

static char s_text[GOLDEN_TEXT_MAX];
static char s_file[GOLDEN_TEXT_MAX];

TEST(the_scripted_battle_matches_the_recorded_transcript_byte_for_byte) {
  char path[512];
  nt_path(GOLDEN_REL_PATH, path, sizeof path);
  golden_run(s_text, sizeof s_text);

  if (nt_flag("--record")) {
    FILE* f = fopen(path, "wb");
    CHECK(f != nullptr);
    if (f) {
      fputs(s_text, f);
      fclose(f);
      printf("  recorded %s (%zu bytes)\n", path, strlen(s_text));
    }
    return;
  }

  FILE* f = fopen(path, "rb");
  CHECK(f != nullptr);
  if (!f) {
    fprintf(stderr, "  missing %s - record it with --record\n", path);
    return;
  }
  const size_t n = fread(s_file, 1, sizeof s_file - 1, f);
  fclose(f);
  s_file[n] = '\0';
  CHECK_EQ(n, strlen(s_text));

  const char* a = s_text;
  const char* b = s_file;
  int ln = 1;
  bool same = true;
  while (*a || *b) {
    const char* ea = strchr(a, '\n');
    const char* eb = strchr(b, '\n');
    const size_t la = ea ? (size_t)(ea - a) : strlen(a);
    const size_t lb = eb ? (size_t)(eb - b) : strlen(b);
    if (la != lb || memcmp(a, b, la) != 0) {
      fprintf(stderr, "  golden mismatch at line %d\n    now:    %.*s\n    golden: %.*s\n",
              ln, (int)la, a, (int)lb, b);
      same = false;
      break;
    }
    a = ea ? ea + 1 : a + la;
    b = eb ? eb + 1 : b + lb;
    ln++;
  }
  CHECK(same);
}

TEST(the_transcript_is_a_pure_function_of_the_seed_and_covers_what_it_claims_to) {
  static char again[GOLDEN_TEXT_MAX];
  golden_run(s_text, sizeof s_text);
  golden_run(again, sizeof again);
  CHECK(strcmp(s_text, again) == 0);
  // A recording that never saw a faint, a switch, a buff or a protection would
  // be a transcript of nothing in particular, so it names what it actually
  // covers. THE TWO IT DOES NOT: ATK_EFF_DOT, which no shipped learnset can
  // reach (see the banner), and ATK_EFF_HEAL_PCT / DRAIN_PCT, which appear as
  // plain HP lines. Those rules are covered by test_battle.cpp.
  CHECK(strstr(s_text, " FAINT ") != nullptr);
  CHECK(strstr(s_text, " SWITCH ") != nullptr);
  CHECK(strstr(s_text, " STAGE ") != nullptr);
  CHECK(strstr(s_text, " HIT ") != nullptr);
  CHECK(strstr(s_text, " MISS ") != nullptr);
  CHECK(strstr(s_text, " PROTECT ") != nullptr);
  CHECK(strstr(s_text, " STUN ") != nullptr);
  CHECK(strstr(s_text, " CORRUPT ") != nullptr);
  CHECK(strstr(s_text, " CLEANSE ") != nullptr);
  CHECK(strstr(s_text, " SKIPPED ") != nullptr);
  CHECK(strstr(s_text, "BATTLE_END") != nullptr);
}
