// =============================================================================
//  PEBBLEBOL - ui/screen_battle.cpp
//  See screen_battle.h. PURE translation unit.
// =============================================================================
#include "screen_battle.h"

#include <stdio.h>
#include <string.h>

#include "../core/strings_es.h"
#include "../game/battle.h"
#include "../game/battle_ai.h"
#include "../game/box.h"
#include "../game/genome.h"
#include "../game/pebble.h"
#include "battle_renderer.h"
#include "gfx.h"
#include "pet_art.h"
#include "../game/inventory.h"   // the armed BATTLE_MOD (P5-C4)
#include "../hardware/audio.h"    // one cue per beat (P6-C1); pure, no GPIO
#include "screen.h"
#include "ui.h"

// -----------------------------------------------------------------------------
//  SHAPE
// -----------------------------------------------------------------------------
// The event ring holds ONE ROUND. It is re-initialised before every
// battle_step_round(), which is why it does not have to be sized against a
// whole battle and why the RESOLVE playback never has to filter on ev->round -
// every entry in it belongs to the round being shown. `dropped` is still read
// back and surfaced (battle_screen_dropped), because a transcript with holes in
// it is a transcript the hash cannot see is wrong.
//
// 48 is not a guess: tests/test_battle_screen.cpp plays 64 seeded battles to
// their end through this screen and asserts dropped == 0 the whole way, and it
// reports the widest round it saw.
#define BT_LOG_CAP        48u

#define BT_EV_MS         600u    // one transcript beat
#define BT_INTRO_MS     1600u    // the stare-down, skippable
#define BT_FPS_HOLD_MS   400u    // renewed every update(); see battle_update()

// A BEAT MUST OUTLAST THE WORST FRAME PERIOD, or the playback enters beats and
// replaces them without ever drawing one. battle_update() advances at most ONE
// beat per call (see the `if`, deliberately not a `while`, in battle_update()),
// so the relationship is exact: with BT_EV_MS at or below the frame period,
// every frame retires a beat and the ones in between are never shown.
// ui/render.h's rd_fps() never answers below FPS_LOW, so the frame period tops
// out at 1000/FPS_LOW ms and today's margin is 600/250 = 2.4x. MEASURED at the
// boundary: 293/293 beats drawn at 20 fps, at 4 fps and at 2 fps; at 1 fps only
// 176/293. Nothing else in the tree states this, so a retune of BT_EV_MS below
// a quarter of a second would silently start dropping transcript with no test
// failing - which is what this line is here to stop.
static_assert(BT_EV_MS > (1000u / (unsigned)FPS_LOW),
              "a transcript beat must last longer than the slowest frame, or "
              "beats are entered and replaced without ever being drawn");

#define BT_MENU_ROWS     ((uint8_t)(PB_MOVE_COUNT + 1u))       // 4 attacks + CAMBIAR
#define BT_MENU_SWITCH   ((uint8_t)PB_MOVE_COUNT)              // that last row
#define BT_SWITCH_ROWS   ((uint8_t)(BATTLE_TEAM_MAX + 1u))     // slots + "Volver"
#define BT_PICK_ROWS     ((uint8_t)(BOX_SLOTS + 1u))           // slots + "LISTO"

#define BT_ROW_CAP        20
#define BT_VAL_CAP         7
#define BT_MSG_CAP        28

static_assert(BT_MENU_ROWS == 5u, "four attacks and one CAMBIAR row");
static_assert(BATTLE_TEAM_MAX == 3, "the pick list and the pip track are three wide");

// -----------------------------------------------------------------------------
//  STATE. Every byte of it is here, on purpose: see the no-per-frame-heap note
//  in screen_battle.h.
// -----------------------------------------------------------------------------
static BattleSetup s_setup;                 // 780 B; kept, not discarded - it is
                                            // where the player's nicknames live
                                            // and it is the exact object P4-C5's
                                            // battle_replay() takes.
static BattleState s_st;                    // 212 B
static BattleAi    s_ai;                    //   8 B
static BattleEvent s_ring[BT_LOG_CAP];      // 576 B
static BattleLog   s_log;

static uint8_t  s_mode    = BTM_PICK;
static uint8_t  s_entry   = BT_ENTRY_PRACTICE;
// WHICH HALF OF THE SETUP IS THE PLAYER'S. 0 for every single-device entry, and
// the SESSION's answer for a linked one (networking/session.cpp fixes it from
// the two device ids). Every `0u` that used to mean "us" and every `1u` that
// used to mean "the foe" in this file is now s_me / s_foe(): a linked battle is
// the first fight in this firmware the player is not always side 0 of, and a
// hard-coded 0 would have shown the peer's team as the player's on one of the
// two devices and paid the wrong side.
static uint8_t  s_me      = 0;
static uint32_t s_seed    = 0;
static uint8_t  s_armed   = 0;
static uint8_t  s_live    = 0;              // battle_init() succeeded
static uint8_t  s_reported = 0;             // THE one report path's guard
static uint8_t  s_reports = 0;              // how often it actually fired
static uint8_t  s_submits = 0;              // actions handed to the engine
static uint8_t  s_reject  = (uint8_t)BR_OK;
static uint8_t  s_cur     = 0;
static uint8_t  s_pick[BATTLE_TEAM_MAX];
static uint8_t  s_pick_n  = 0;
static uint8_t  s_art[2][BATTLE_TEAM_MAX];
static uint16_t s_hp0[2][BATTLE_TEAM_MAX];  // hp at the top of the shown round
static uint16_t s_round_shown = 0;
static uint16_t s_ev      = 0;              // playback cursor into s_log
static uint8_t  s_ev_live = 0;              // is s_ev pointing at a real beat?
static uint32_t s_ev_ms   = 0;
static uint32_t s_mode_ms = 0;
static uint16_t s_dropped = 0;
static char     s_msg[BT_MSG_CAP];

static uint8_t s_foe(void) { return (uint8_t)(s_me ^ 1u); }

// A linked battle's opposing actions come off the wire, so nothing in this file
// may call the AI, build a foe or step a round for one.
static bool linked(void) { return s_entry == (uint8_t)BT_ENTRY_LINK; }

uint8_t  battle_screen_mode(void)    { return s_mode; }
uint8_t  battle_screen_side(void)    { return s_me; }
uint8_t  battle_screen_cursor(void)  { return s_cur; }
uint8_t  battle_screen_reject(void)  { return s_reject; }
uint8_t  battle_screen_outcome(void) { return s_st.outcome; }

int8_t battle_screen_lead_stage(uint8_t stat) {
  if (!s_live || stat >= (uint8_t)BSTAT_COUNT) return 0;
  return s_st.side[s_me].team[s_st.side[s_me].active].stage[stat];
}
uint8_t battle_screen_lead_stage_left(uint8_t stat) {
  if (!s_live || stat >= (uint8_t)BSTAT_COUNT) return 0u;
  return s_st.side[s_me].team[s_st.side[s_me].active].stage_left[stat];
}
uint16_t battle_screen_round(void)   { return s_round_shown; }
uint16_t battle_screen_dropped(void) { return s_dropped; }
uint8_t  battle_screen_picked(void)  { return s_pick_n; }
uint8_t  battle_screen_submits(void) { return s_submits; }
uint8_t  battle_screen_reports(void) { return s_reports; }

uint8_t battle_screen_event(void) {
  if (!s_ev_live || s_ev >= s_log.count) return (uint8_t)RLE_NONE;
  const BattleEvent* e = battle_log_at(s_log, s_ev);
  return e ? e->kind : (uint8_t)RLE_NONE;
}

// -----------------------------------------------------------------------------
//  ONE REPORT PATH (minigames/manager.cpp's mgr_abort(), in miniature).
//
//  Reached from exactly two places - the transition into BTM_RESULT and
//  battle_leave() - and idempotent, so a fight that is won and then walked away
//  from awards its XP once, and a fight abandoned at round 3 awards none. This
//  is the defect the minigame manager shipped with: two exits, each with its
//  own phase check, each able to report.
//
//  THE GUARD IS PER SCREEN VISIT, NOT PER BATTLE, and the difference is worth
//  stating precisely because the tests are named after the battle. battle_enter()
//  clears s_reported, so every visit to SCR_BATTLE reports exactly once - even a
//  visit in which no battle ever started, such as entering the pick list on an
//  empty Box and pressing B: that reports (PRACTICE, won = 0), which ui.cpp
//  drops on its `if (!won) return;`. Harmless, and one battle still cannot be
//  reported twice, because a battle cannot outlive the visit that started it -
//  there is exactly one nav_push(SCR_BATTLE) in the tree and battle_arm()
//  always precedes it.
// -----------------------------------------------------------------------------
static void report_once(uint8_t won) {
  if (s_reported) return;
  s_reported = 1;
  ++s_reports;
  ui_battle_result(s_entry, won);
}

// -----------------------------------------------------------------------------
//  NAMES
//
//  BattleCombatant carries no name - deliberately; game/battle.h keeps nothing
//  it does not resolve a round with. The setup is still here, so the player's
//  nickname is one lookup away and the foe falls back to the roster's own
//  Spanish species name (ui/pet_art.h).
// -----------------------------------------------------------------------------
static const char* combatant_name(uint8_t side, uint8_t slot) {
  if (side > 1u || slot >= (uint8_t)BATTLE_TEAM_MAX) return "";
  const PebbleInstance& m = s_setup.member[side][slot];
  if (m.nickname[0] != '\0') return m.nickname;
  const char* sp = pet_species_name(m.species_id);
  return sp ? sp : "";
}

// -----------------------------------------------------------------------------
//  BUILDING THE TWO TEAMS
// -----------------------------------------------------------------------------

// A deterministic integer hash. The foe roster must be reproducible from the
// seed alone - that is what makes a practice battle replayable from a bug
// report - and drawing it from a named RNG stream would make it depend on every
// unrelated roll the firmware took first.
static uint32_t bt_mix(uint32_t x) {
  x ^= x >> 16; x *= 0x7FEB352Du;
  x ^= x >> 15; x *= 0x846CA68Bu;
  x ^= x >> 16;
  return x;
}

// A synthetic member: a real roster species at a real level with ITS OWN
// learnset, which is the only moveset battle_init() will accept without an
// argument (BR_UNLEARNABLE_MOVE).
static void make_member(PebbleInstance& p, uint8_t species_id, uint8_t level,
                        uint32_t id) {
  memset(&p, 0, sizeof p);
  const SpeciesDef* sp = species_get(species_id);
  if (sp == nullptr) return;          // stays species_id 0 -> BR_NULL_MEMBER, by name
  p.magic            = PEBBLE_MAGIC;
  p.layout_ver       = PEBBLE_LAYOUT_VER;
  p.species_id       = species_id;
  p.id               = id;
  p.level            = level ? level : 1u;
  p.genome.magic_ver = GENOME_MAGIC_VER;
  // The three genes pebble_derive_stats() reads, at their modal value: 8 folds
  // to +1 through pebble_genome_var(), which is the (1,1,1) roll the whole
  // roster win-rate matrix in tools/content was measured at.
  gene_set_temperament(p.genome, 8);
  gene_set_hardiness(p.genome, 8);
  gene_set_metabolism(p.genome, 8);
  memcpy(p.moves, sp->moves, sizeof p.moves);
  PebbleStats st;
  pebble_derive_stats(*sp, p.level, p.genome, st);
  p.hp_cur = st.hp_max;
}

// THE THREE OBJECTS THE SESSION BORROWS (P7-C3). See screen_battle.h: a
// Session holds no BattleSetup and no BattleState of its own, so the linked
// path uses THESE - the same 780 B setup, the same 212 B state and the same
// 576 B transcript ring the practice path uses, and never a second copy.
BattleSetup* battle_link_setup(void) { return &s_setup; }
BattleState* battle_link_state(void) { return &s_st; }
BattleLog*   battle_link_log(void)   { return &s_log; }

// One of the player's Pebbles, COPIED out of the Box. Nothing here writes back,
// which is the whole of the "an interrupted battle cannot corrupt the Box"
// argument.
bool battle_copy_from_box(PebbleInstance& out, uint8_t slot) {
  const PebbleInstance* src = box_peek(slot);
  if (src == nullptr) return false;
  out = *src;
  const SpeciesDef* sp = species_get(out.species_id);
  if (sp == nullptr) return false;
  if (out.level == 0u) out.level = 1u;
  // A stored Pebble from a v1 save can carry moves[] = {0,0,0,0}
  // (persistence/migration.cpp says so). One unknown id and the whole moveset
  // falls back to the species learnset rather than the fight being refused.
  for (uint8_t m = 0; m < (uint8_t)PB_MOVE_COUNT; ++m) {
    if (attack_get(out.moves[m]) == nullptr) {
      memcpy(out.moves, sp->moves, sizeof out.moves);
      break;
    }
  }
  // A PRACTICE BOUT IS FOUGHT AT FULL HEALTH, on the copy. The alternative -
  // carrying hp_cur in - means a Pebble at 1 HP cannot practise at all, and
  // means the fight would have to write the damage back to be honest about it.
  PebbleStats st;
  pebble_derive_stats(*sp, out.level, out.genome, st);
  out.hp_cur = st.hp_max;
  out.status = (uint8_t)(out.status & (uint8_t)~(unsigned)PBS_FAINTED);
  return true;
}

// Ids must be distinct across all six members (BR_DUPLICATE_ID). The Box's own
// ids are whatever box_new_pebble() minted, so the synthetic ones are checked
// against them rather than assumed to be out of the way.
static uint32_t free_id(const BattleSetup& s, uint32_t want) {
  for (uint8_t guard = 0; guard < 64u; ++guard) {
    bool clash = false;
    for (uint8_t side = 0; side < 2u && !clash; ++side)
      for (uint8_t i = 0; i < (uint8_t)BATTLE_TEAM_MAX; ++i)
        if (s.member[side][i].id == want) { clash = true; break; }
    if (!clash) return want;
    ++want;
  }
  return want;
}

// The opponent. Species folded out of the seed, levels matched to the player's
// lead so a practice bout is neither a walkover nor a wall.
static void build_foe(BattleSetup& s, uint8_t n, uint8_t level, uint32_t seed) {
  if (n == 0u) n = 1u;
  s.count[1] = n;
  for (uint8_t i = 0; i < n; ++i) {
    const uint32_t h  = bt_mix(seed + 0x9E3779B9u * (uint32_t)(i + 1u));
    const uint8_t  sp = (uint8_t)(1u + (h % (uint32_t)SPECIES_TABLE_COUNT));
    make_member(s.member[1][i], sp, level, free_id(s, 0xFF000001u + i));
  }
}

static void resolve_art(void) {
  memset(s_art, 0, sizeof s_art);
  for (uint8_t side = 0; side < 2u; ++side) {
    for (uint8_t i = 0; i < (uint8_t)BATTLE_TEAM_MAX; ++i) {
      const BattleCombatant* c = battle_combatant(s_st, side, i);
      if (c == nullptr || (c->flags & BCF_PRESENT) == 0u) continue;
      // The gene_species fallback is unreachable here and that is worth saying:
      // battle_init() refuses BR_UNKNOWN_SPECIES, so every combatant on this
      // field HAS a roster row and pet_art_key() answers from it. The art is
      // resolved ONCE, outside every frame loop.
      s_art[side][i] = pet_art_key(c->species_id, 0u);
    }
  }
}

static void set_mode(uint8_t m) {
  s_mode    = m;
  s_mode_ms = ui_now_ms();
  gfx_list_reset();
}

// -----------------------------------------------------------------------------
//  LEGALITY. Every menu row asks the ENGINE whether it is playable; nothing in
//  this file re-implements a rule about cooldowns, faints or empty slots.
// -----------------------------------------------------------------------------
static bool attack_legal(uint8_t slot) {
  const BattleAction a = { (uint8_t)BACT_ATTACK, slot };
  return battle_validate_action(s_st, s_me, a) == BR_OK;
}

static bool switch_legal(uint8_t slot) {
  const BattleAction a = { (uint8_t)BACT_SWITCH, slot };
  return battle_validate_action(s_st, s_me, a) == BR_OK;
}

static bool any_switch_legal(void) {
  for (uint8_t i = 0; i < (uint8_t)BATTLE_TEAM_MAX; ++i)
    if (switch_legal(i)) return true;
  return false;
}

static bool menu_row_legal(uint8_t r) {
  if (r < (uint8_t)PB_MOVE_COUNT) return attack_legal(r);
  return any_switch_legal();
}

static bool switch_row_legal(uint8_t r) {
  if (r < (uint8_t)BATTLE_TEAM_MAX) return switch_legal(r);
  // The way out of the switch list is closed exactly while the engine says a
  // replacement is owed, so the ladder can never park the player on a screen
  // whose only legal answer is one they just declined.
  return !battle_side_must_switch(s_st, s_me);
}

static bool pick_row_legal(uint8_t r) {
  if (r < (uint8_t)BOX_SLOTS) return box_occupied(r);
  return true;                                   // the LISTO row is always there
}

// The ring, over LEGAL rows only. A cursor that cannot come to rest on an
// illegal row is the first half of "never submit something the engine refuses";
// submit() is the second.
static uint8_t ring_legal(uint8_t cur, uint8_t n, bool (*ok)(uint8_t)) {
  for (uint8_t k = 1u; k <= n; ++k) {
    const uint8_t c = (uint8_t)((cur + k) % n);
    if (ok(c)) return c;
  }
  return cur;
}

static uint8_t first_legal(uint8_t n, bool (*ok)(uint8_t)) {
  for (uint8_t c = 0; c < n; ++c) if (ok(c)) return c;
  return 0;
}

// -----------------------------------------------------------------------------
//  THE TRANSCRIPT
// -----------------------------------------------------------------------------

// Which log entries get a beat of their own. RLE_ROUND_BEGIN / _END carry
// hashes, RLE_ACTION and RLE_ORDER carry bookkeeping and RLE_HP is the
// resulting number the beat before it already announced: showing any of them
// would spend a second of the player's time on something with no picture.
static bool ev_is_beat(uint8_t kind) {
  switch (kind) {
    case RLE_SWITCH: case RLE_SKIPPED: case RLE_MISS:  case RLE_HIT:
    case RLE_STAGE:  case RLE_PROTECT: case RLE_DOT:   case RLE_CORRUPT:
    case RLE_STUN:   case RLE_CLEANSE: case RLE_FAINT: case RLE_BATTLE_END:
      return true;
    default:
      return false;
  }
}

static const char* stat_label(uint8_t stat) {
  switch (stat) {
    case BSTAT_ATK: return S(STR_BT_ATK);
    case BSTAT_DEF: return S(STR_BT_DEF);
    default:        return S(STR_BT_SPD);
  }
}

// BO_ABORT IS NOT A DRAW, and it used to print as one. game/battle.h defines it
// as "step 1 found the state moved under a submitted action" - a dropped or
// stale action, which P4-C5 maps to BATTLE_END(DESYNC) - and it arrived here
// through the `default` arm, so the one outcome that means something went WRONG
// was the one the panel called Empate. It is unreachable from this screen today
// (submit() is the only producer of side 0's action and battle_ai_choose() the
// only producer of side 1's, and both validate against the same unchanged
// state) but the whole point of naming it is that a failure should not arrive
// wearing an ordinary result's clothes.
//
// AND IT IS RELATIVE TO s_me SINCE P7-C3. BO_WIN_A is "side 0 won", which is
// the PLAYER only on a device the session made side 0; on the other device the
// same byte means the peer won, and printing "¡GANASTE!" there would be the
// same battle reported two different ways on two panels.
static const char* outcome_word(uint8_t outcome) {
  // A LINKED BATTLE THE SESSION DID NOT AUTHORISE IS NEITHER A WIN NOR A LOSS.
  // A desync, a lost radio or a peer that disagreed about the final hash all
  // leave this engine holding an outcome nobody agreed, and blaming the player
  // for a radio is the same class of mistake as calling BO_ABORT a draw.
  if (linked() && ui_link_battle_status() == UI_LKB_BROKEN) return S(STR_LK_BROKEN);
  const uint8_t win_me  = (s_me == 0u) ? (uint8_t)BO_WIN_A : (uint8_t)BO_WIN_B;
  const uint8_t win_foe = (s_me == 0u) ? (uint8_t)BO_WIN_B : (uint8_t)BO_WIN_A;
  if (outcome == win_me)  return S(STR_BT_WIN);
  if (outcome == win_foe) return S(STR_BT_LOSE);
  if (outcome == (uint8_t)BO_ABORT) return S(STR_BT_ABORT);
  return S(STR_BT_DRAW);
}

// The engine's own verdict as the byte ui_battle_result() takes. ONE
// expression, because the two places that report - the transition into
// BTM_RESULT and battle_leave() - disagreeing about who won is exactly the
// defect this file's report_once() exists to make impossible.
static uint8_t won_now(void) {
  // THE REWARD GATE FOR A LINKED BATTLE IS THE SESSION'S AND NOT THIS ENGINE'S.
  // ui_link_battle_status() answers UI_LKB_WON only where
  // session_rewards_authorised() is true - both endpoints agreed the outcome
  // AND the final hash - so SE_DESYNC and SE_LOST pay nothing however the local
  // engine happened to end. Reading s_st.outcome here instead would pay a win
  // the peer never confirmed, which is exactly the hole networking/session.h's
  // `paid` flag exists to close.
  if (linked()) return (uint8_t)(ui_link_battle_status() == UI_LKB_WON ? 1u : 0u);
  const uint8_t mine = (s_me == 0u) ? (uint8_t)BO_WIN_A : (uint8_t)BO_WIN_B;
  return (uint8_t)(s_st.outcome == mine ? 1u : 0u);
}

// One line for the beat at s_ev. Built once per beat, not once per frame.
static void build_message(void) {
  s_msg[0] = '\0';
  if (!s_ev_live || s_ev >= s_log.count) return;
  const BattleEvent* e = battle_log_at(s_log, s_ev);
  if (e == nullptr) return;
  const char* who = (e->side <= 1u) ? combatant_name(e->side, e->slot) : "";

  switch (e->kind) {
    case RLE_SWITCH:
      snprintf(s_msg, sizeof s_msg, "%s %s", who, S(STR_BT_ENTER));
      break;
    case RLE_MISS:
      snprintf(s_msg, sizeof s_msg, "%s %s", who, S(STR_BT_MISS));
      break;
    case RLE_HIT: {
      // The MOVE's own name, out of the attacker's own slot: an attack the
      // content pack renamed renames itself here.
      const BattleCombatant* c = battle_combatant(s_st, e->side, e->slot);
      const AttackDef* a = (c && e->a < (uint8_t)PB_MOVE_COUNT)
                             ? attack_get(c->moves[e->a]) : nullptr;
      snprintf(s_msg, sizeof s_msg, "%s -%u",
               a ? S(a->name_idx) : who, (unsigned)e->b);
      break;
    }
    case RLE_STAGE: {
      const int stage = (int)e->b - (int)BATTLE_STAGE_BIAS;
      snprintf(s_msg, sizeof s_msg, "%s %s%+d", who, stat_label(e->a), stage);
      break;
    }
    case RLE_PROTECT: snprintf(s_msg, sizeof s_msg, "%s %s", who, S(STR_BT_PROTECT)); break;
    case RLE_DOT:     snprintf(s_msg, sizeof s_msg, "%s %s -%u", who, S(STR_BT_DOT),
                               (unsigned)e->a); break;
    case RLE_CORRUPT: snprintf(s_msg, sizeof s_msg, "%s %s", who, S(STR_BT_CORRUPT)); break;
    case RLE_STUN:    snprintf(s_msg, sizeof s_msg, "%s %s", who, S(STR_BT_STUN)); break;
    case RLE_CLEANSE: snprintf(s_msg, sizeof s_msg, "%s %s", who, S(STR_BT_CLEANSE)); break;
    case RLE_SKIPPED: snprintf(s_msg, sizeof s_msg, "%s %s", who, S(STR_BT_SKIP)); break;
    case RLE_FAINT:   snprintf(s_msg, sizeof s_msg, "%s %s", who, S(STR_BT_FAINT)); break;
    case RLE_BATTLE_END: snprintf(s_msg, sizeof s_msg, "%s", outcome_word(e->a)); break;
    default: break;
  }
}

static void end_playback(void);

// Move to the next beat, or finish the round. The two frame-level effects live
// here rather than in render(): they are events, not pixels, and calling
// rd_shake() once per frame would pin the panel shaking for the whole beat.
static void enter_beat(uint16_t i) {
  s_ev      = i;
  s_ev_live = 1;
  // THIS LINE IS LOAD-BEARING AND READS LIKE AN ORDINARY TIMESTAMP. It is a
  // RESET TO NOW, not an accumulation (`s_ev_ms += BT_EV_MS`), and that is what
  // makes time-based catch-up structurally impossible: a frame that arrives
  // late shows its beat for a full BT_EV_MS from the moment it was entered
  // rather than immediately owing the playback the beats the gap swallowed. So
  // the rendered SEQUENCE of beats is identical at 20 fps and at 4 fps and only
  // the wall-clock length of the transcript changes. MEASURED: turning
  // battle_update()'s `if` into a catch-up `while` is completely INERT while
  // this line resets; the two together are what would diverge.
  s_ev_ms   = ui_now_ms();
  build_message();
  const uint8_t k = battle_screen_event();
  // The two frame effects and their two cues are armed together, on the same
  // edge and for the same reason: a beat is entered exactly once.
  if (k == RLE_HIT)   { ui_shake(1u, 120u); audio_play(SFX_BUZZ); }
  if (k == RLE_FAINT) { ui_flash(140u);     audio_play(SFX_FALL); }
}

static void advance_playback(void) {
  uint16_t i = s_ev_live ? (uint16_t)(s_ev + 1u) : 0u;
  for (; i < s_log.count; ++i) {
    const BattleEvent* e = battle_log_at(s_log, i);
    if (e && ev_is_beat(e->kind)) { enter_beat(i); return; }
  }
  end_playback();
}

// HP as it stood AT THIS BEAT, not as it stands now. The whole round has
// already been resolved by the time the first beat is drawn, so reading
// hp_cur straight off the state would show the final number under every event
// and a hit would land on a bar that had already fallen. RLE_HP / RLE_DOT carry
// the resulting value, so replaying them up to the NEXT beat (which is what
// includes the RLE_HP that belongs to the current one) reconstructs it exactly.
static uint16_t hp_at_beat(uint8_t side, uint8_t slot) {
  uint16_t hp = (side <= 1u && slot < (uint8_t)BATTLE_TEAM_MAX)
                  ? s_hp0[side][slot] : 0u;
  if (!s_ev_live) return hp;
  for (uint16_t i = 0; i < s_log.count; ++i) {
    const BattleEvent* e = battle_log_at(s_log, i);
    if (e == nullptr) continue;
    if (i > s_ev && ev_is_beat(e->kind)) break;
    if (e->side != side || e->slot != slot) continue;
    if (e->kind == RLE_HP || e->kind == RLE_DOT) hp = e->b;
  }
  return hp;
}

// -----------------------------------------------------------------------------
//  THE ROUND
// -----------------------------------------------------------------------------
// EVERYTHING A ROUND'S TRANSCRIPT NEEDS BEFORE THE ROUND RESOLVES, in one
// place because both paths need all three and a linked round is resolved
// somewhere this file cannot see. The HP snapshot is what hp_at_beat() replays
// forward from; s_round_shown is the round being resolved and NOT st.round
// afterwards (battle_s8_end_of_round() increments it, which game/battle.h warns
// about in as many words); and the ring holds ONE round, which is why it is
// re-opened rather than appended to.
static void open_round_log(void) {
  for (uint8_t side = 0; side < 2u; ++side)
    for (uint8_t i = 0; i < (uint8_t)BATTLE_TEAM_MAX; ++i)
      s_hp0[side][i] = s_st.side[side].team[i].hp_cur;
  s_round_shown = s_st.round;
  battle_log_init(s_log, s_ring, (uint16_t)BT_LOG_CAP);
}

static void step_round(void) {
  open_round_log();
  // BS_NEED_ACTIONS means the engine wrote NOTHING - game/battle.h calls the
  // state bit-identical - so there is no transcript to play and RESOLVE would
  // be a mode with an empty ring under it. Going straight back to the menu is
  // the only honest answer: the round did not happen, and the player's next
  // press starts it again. Unreachable today (submit() only calls this after
  // both sides' actions came back BR_OK) and cheaper than a mode that draws a
  // round that was never resolved.
  const uint8_t step = (uint8_t)battle_step_round(s_st, &s_log);
  if (s_log.dropped > s_dropped) s_dropped = s_log.dropped;
  if (step == (uint8_t)BS_NEED_ACTIONS) { end_playback(); return; }

  s_ev_live = 0;
  set_mode(BTM_RESOLVE);
  advance_playback();
}

// THE ONLY function that reaches battle_submit_action(). It re-validates even
// though the cursor cannot rest on an illegal row: the two halves of the
// promise are independent, and this one also covers a submission arriving from
// somewhere the ring never walked.
//
// AND THE FIRST VALIDATE IS EXACTLY ONE WIGGLE WIDE - said here because the
// obvious reading of the paragraph above is that deleting it would let an
// illegal action through, and it would not. battle_submit_action() performs
// the SAME full validation internally and the line below checks its return, so
// a refusal still happens either way; what this call adds is the ui_wiggle()
// beside the toast. MEASURED: with this line deleted the whole battle-screen
// suite stays green and the only observable difference is that the wiggle stops
// firing. It is kept because a refusal the player cannot feel is a refusal they
// will press again, not because the engine needs it.
static void submit(BattleAction act) {
  s_reject = (uint8_t)battle_validate_action(s_st, s_me, act);
  if (s_reject != (uint8_t)BR_OK) { ui_toast(STR_BT_ILLEGAL); ui_wiggle(); return; }

  // THE LINKED PATH SUBMITS THROUGH THE LOCKSTEP AND NEVER DIRECTLY. The engine
  // is written by networking/battle_link.cpp's link_local_action(), which also
  // puts the action on the wire and re-tries the resolution; calling
  // battle_submit_action() here as well would write the pending twice and the
  // second write is what the engine calls BR_ALREADY_SUBMITTED.
  if (linked()) {
    // THE LOCKSTEP DECIDES WHEN A MOVE IS WANTED, not the menu. It answers
    // false while the agreement barrier is up and while this side already has a
    // pending action, and a submission into either of those would be silently
    // dropped by networking/battle_link.cpp - a button that does nothing, which
    // is indistinguishable from a broken one.
    if (!ui_link_battle_wants_action()) { ui_toast(STR_BT_ILLEGAL); ui_wiggle(); return; }
    open_round_log();
    ++s_submits;
    ui_link_battle_submit(act.kind, act.index);
    set_mode(BTM_WAIT);
    return;
  }

  s_reject = (uint8_t)battle_submit_action(s_st, s_me, act);
  if (s_reject != (uint8_t)BR_OK) { ui_toast(STR_BT_ILLEGAL); return; }
  ++s_submits;

  // game/battle_ai.h: battle_ai_choose() answers { BACT_NONE, 0 } when the side
  // has NOTHING legal, and the validator refuses that value by name, so it must
  // never be forwarded. In a running battle the only two states that produce it
  // are "already over" and "already submitted", neither of which is reachable
  // one line after the player's own submission - this is defence in depth and
  // is not covered by a test because nothing can drive it.
  const BattleAction foe = battle_ai_choose(s_ai, s_st);
  if (foe.kind == (uint8_t)BACT_NONE) { end_playback(); return; }

  // THE FOE'S SUBMISSION IS NOT DISCARDED. It was `(void)`-ed, and the failure
  // mode that hides is silent and misleading rather than loud: a refused
  // submission leaves side 1 carrying BACT_NONE into battle_s1_validate_action()
  // at the top of the round, which turns the whole battle into BO_ABORT - and
  // BO_ABORT used to print through outcome_word()'s default arm as "Empate". A
  // dropped action would have reached the player as a DRAW and as nothing else.
  // Recording it in s_reject makes it visible to battle_screen_reject(), which
  // every case in tests/test_battle_screen.cpp already asserts is BR_OK, so an
  // AI that ever drifts away from the validator fails a NAMED test instead of
  // quietly ending battles in a word that is not true.
  s_reject = (uint8_t)battle_submit_action(s_st, s_foe(), foe);

  step_round();
}

static void to_menu(void) {
  // A side that owes a replacement has no legal attack at all, so the menu
  // would be a list of five things none of which can be chosen. The engine is
  // asked, not guessed at.
  if (battle_side_must_switch(s_st, s_me)) {
    s_cur = first_legal(BT_SWITCH_ROWS, switch_row_legal);
    set_mode(BTM_SWITCH);
    return;
  }
  s_cur = first_legal(BT_MENU_ROWS, menu_row_legal);
  set_mode(BTM_MENU);
}

static void end_playback(void) {
  s_ev_live = 0;
  s_msg[0]  = '\0';
  if (linked()) {
    // THE ROUND HAS BEEN SHOWN. The ring holds one round and the lockstep will
    // not write another until we submit again, so emptying it here is what
    // stops the next frame replaying the transcript it just finished.
    battle_log_init(s_log, s_ring, (uint16_t)BT_LOG_CAP);
    const uint8_t stt = ui_link_battle_status();
    if (stt != (uint8_t)UI_LKB_RUNNING) { report_once(won_now()); set_mode(BTM_RESULT); return; }
    // THE ENGINE MAY HAVE FINISHED BEFORE THE SESSION AGREED, and this is the
    // half-second in which reporting would be wrong: the local outcome is
    // settled and the BATTLE_END exchange that authorises a reward has not
    // happened yet. So the screen lands on the result page and battle_update()
    // reports when ui_link_battle_status() stops answering RUNNING - which it
    // will, in one direction or the other, because the ladder ends every
    // session.
    if (s_st.outcome != (uint8_t)BO_UNDECIDED) { set_mode(BTM_RESULT); return; }
    if (ui_link_battle_wants_action()) { to_menu(); return; }
    set_mode(BTM_WAIT);
    return;
  }
  if (s_st.outcome != (uint8_t)BO_UNDECIDED) {
    report_once(won_now());
    set_mode(BTM_RESULT);
    return;
  }
  to_menu();
}

// -----------------------------------------------------------------------------
//  STARTING
// -----------------------------------------------------------------------------
static uint8_t start_battle(void) {
  battle_setup_clear(s_setup);
  s_setup.seed = s_seed;

  uint8_t lead = 1u;
  if (s_entry == BT_ENTRY_DIAG) {
    // SYNTHETIC ON BOTH SIDES, so test_battle runs on a device that has never
    // filled its Box - which is exactly the device a bug report comes from.
    // Three consecutive families at a level in the middle of the curve.
    lead = 10u;
    s_setup.count[0] = (uint8_t)BATTLE_TEAM_MAX;
    for (uint8_t i = 0; i < (uint8_t)BATTLE_TEAM_MAX; ++i)
      make_member(s_setup.member[0][i], (uint8_t)(1u + i * 3u), lead,
                  free_id(s_setup, 0xFE000001u + i));
  } else {
    if (s_pick_n == 0u) return (uint8_t)BR_TEAM_SIZE;
    uint8_t n = 0;
    for (uint8_t i = 0; i < s_pick_n; ++i)
      if (battle_copy_from_box(s_setup.member[0][n], s_pick[i])) ++n;
    if (n == 0u) return (uint8_t)BR_TEAM_SIZE;
    s_setup.count[0] = n;
    lead = s_setup.member[0][0].level;
  }

  build_foe(s_setup, s_setup.count[0], lead, s_seed);

  const uint8_t r = (uint8_t)battle_init(s_st, s_setup);
  if (r != (uint8_t)BR_OK) return r;

  // THE ARMED BATTLE MODIFIER (P5-C4). An ITEM_KLASS_BATTLE_MOD used from a
  // menu before the fight is consumed there and armed in game/inventory.cpp;
  // this is where it lands, on the player's lead combatant, AFTER battle_init()
  // has built the state.
  //
  // LOCAL-ONLY, AND DELIBERATELY. It is applied to BattleState and NEVER to
  // BattleSetup: the setup is the REPLAY INPUT and the version-checked record
  // two peers agree on, and its reserved[2] is asserted zero. So the buff
  // exists in a practice battle and not in a linked one - stated in
  // game/inventory.h rather than discovered later.
  //
  // A DIAG battle takes NO modifier: the console builds a synthetic team on a
  // device that may have never filled its Box, and a diagnostic whose numbers
  // depend on the player's bag is a diagnostic nobody can compare. That also
  // keeps tests/golden/battle_v1.txt exactly what it was - nothing is armed in
  // a golden run either.
  InvBattleMod mod;
  if (s_entry == BT_ENTRY_PRACTICE && inv_mod_take(mod)) {
    BattleCombatant& lead_c = s_st.side[s_me].team[s_st.side[s_me].active];
    if (mod.stat < (uint8_t)BSTAT_COUNT) {
      int16_t st_v = (int16_t)((int16_t)lead_c.stage[mod.stat] + (int16_t)mod.stages);
      if (st_v > (int16_t)BUFF_STAGE_MAX) st_v = (int16_t)BUFF_STAGE_MAX;
      if (st_v < (int16_t)BUFF_STAGE_MIN) st_v = (int16_t)BUFF_STAGE_MIN;
      lead_c.stage[mod.stat]      = (int8_t)st_v;
      lead_c.stage_left[mod.stat] = mod.rounds;
    }
  }

  // The AI's own stream. game/battle_ai.h's two-stream argument is that the
  // chooser must never move the battle's cursor, so it is seeded from a value
  // derived from the same seed and never from BattleState.rng.
  battle_ai_init(s_ai, 1u, s_seed ^ 0xA5A5A5A5u);
  battle_log_init(s_log, s_ring, (uint16_t)BT_LOG_CAP);
  resolve_art();

  s_live     = 1;
  s_reported = 0;
  s_submits  = 0;
  s_dropped  = 0;
  s_ev_live  = 0;
  s_msg[0]   = '\0';
  s_round_shown = s_st.round;
  set_mode(BTM_INTRO);
  return (uint8_t)BR_OK;
}

// -----------------------------------------------------------------------------
//  THE TABLE HOOKS
// -----------------------------------------------------------------------------
void battle_arm(uint8_t entry, uint32_t seed) {
  s_entry = (entry == BT_ENTRY_DIAG) ? (uint8_t)BT_ENTRY_DIAG
                                     : (uint8_t)BT_ENTRY_PRACTICE;
  s_seed  = seed;
  s_armed = 1;
}

// NO SEED, BECAUSE THERE IS NO LOCAL SEED. networking/battle_link.cpp's
// link_begin() derived it from the session id, both nonces and both team CRCs
// and has ALREADY run battle_init() on the state this screen lent it - so a
// linked entry adopts a battle rather than starting one, and drawing a seed
// here would be a number nothing reads.
void battle_arm_link(uint8_t my_side) {
  s_entry = (uint8_t)BT_ENTRY_LINK;
  s_me    = (uint8_t)(my_side & 1u);
  s_seed  = 0;
  s_armed = 1;
}

// RE-ENTERING WITHOUT LEAVING WOULD DROP A REPORT, and the reason it cannot is
// worth writing down rather than relying on: app/state_machine.cpp's sm_goto()
// skips leave() when the target IS the current screen but always runs enter(),
// so an sm_push(SCR_BATTLE) onto a battle that has already reported would clear
// s_reported and s_reports below and the visit would end having reported once
// in total instead of twice - the minigame manager's bug in mirror image. The
// tree is safe because there is exactly ONE nav_push(SCR_BATTLE) (ui.cpp's
// ui_start_battle) and battle_arm() always precedes it, so a second entry point
// added later must either go through ui_start_battle() or re-open this.
void battle_enter(void) {
  const uint8_t was_link = (uint8_t)(s_armed && s_entry == (uint8_t)BT_ENTRY_LINK ? 1u : 0u);
  s_live     = 0;
  s_reported = 0;
  s_reports  = 0;
  s_submits  = 0;
  s_reject   = (uint8_t)BR_OK;
  s_pick_n   = 0;
  s_dropped  = 0;
  s_ev_live  = 0;
  s_msg[0]   = '\0';
  memset(s_pick, 0, sizeof s_pick);
  // A LINKED ENTRY MAY NOT WIPE THE STATE, and this is the one line where the
  // difference is fatal rather than cosmetic: networking/battle_link.cpp ran
  // battle_init() on this exact object before the push, both endpoints hashed
  // it, and zeroing it here would put this device into round 1 of a battle the
  // peer is already holding - a desync manufactured by the screen. The log is
  // re-opened either way: it holds one round and the first one has not run.
  if (!was_link) memset(&s_st, 0, sizeof s_st);
  battle_log_init(s_log, s_ring, (uint16_t)BT_LOG_CAP);
  // A mode that is NOT the pick list, before either branch below decides. A
  // stale BTM_PICK left over from a previous entry would draw the Box list for
  // a DIAG battle whose init had just been refused - the one state render()
  // checks before it checks s_live.
  set_mode(BTM_INTRO);

  if (!s_armed) {
    // Reached by navigating to SCR_BATTLE without going through
    // ui_start_battle() - an old back-stack entry, or a test driving the row.
    // A fixed seed rather than a refusal: the screen still has to draw.
    s_entry = (uint8_t)BT_ENTRY_PRACTICE;
    s_seed  = (uint32_t)BT_DIAG_SEED;
  }
  s_armed = 0;
  if (!was_link) s_me = 0u;      // every single-device entry is side 0

  if (was_link) {
    // ADOPT, do not start. The battle already exists on both devices; all this
    // screen owes it is a picture, a menu and one action per round.
    s_live        = 1;
    s_round_shown = s_st.round;
    resolve_art();
    set_mode(BTM_INTRO);
    return;
  }

  if (s_entry == BT_ENTRY_DIAG) {
    const uint8_t r = start_battle();
    if (r != (uint8_t)BR_OK) { s_reject = r; ui_toast(STR_BT_START_ERR); }
    return;
  }

  s_cur = first_legal(BT_PICK_ROWS, pick_row_legal);
  set_mode(BTM_PICK);
  if (box_count() == 0u) ui_toast(STR_BT_NO_TEAM);
}

// -----------------------------------------------------------------------------
//  ONE FRAME OF A LINKED BATTLE
//
//  The order of the four questions below is the whole of it, and it is chosen
//  so that nothing the player is owed is skipped by something that happened
//  later on the wire:
//    1. pump the session, always, in every mode - including BTM_RESULT, where
//       the BATTLE_END exchange that authorises the reward is still running;
//    2. show a round that has resolved, even if the session has since ended -
//       a transcript the player paid for is not thrown away by a desync that
//       arrived after it;
//    3. let a playback in progress finish before anything else is decided;
//    4. and only then report, because report_once() is one shot and won_now()
//       is only true once the session has authorised it.
// -----------------------------------------------------------------------------
static void link_frame(uint32_t now_ms) {
  ui_link_battle_pump(now_ms);
  if (s_log.dropped > s_dropped) s_dropped = s_log.dropped;

  if (s_mode == BTM_INTRO) {
    if ((uint32_t)(now_ms - s_mode_ms) < BT_INTRO_MS) return;
    set_mode(BTM_WAIT);
  }

  if (s_mode == BTM_WAIT && s_log.count > 0u) {
    s_ev_live = 0;
    set_mode(BTM_RESOLVE);
    advance_playback();
    return;
  }

  if (s_mode == BTM_RESOLVE) {
    if (s_ev_live && (uint32_t)(now_ms - s_ev_ms) >= BT_EV_MS) advance_playback();
    return;
  }

  const uint8_t stt = ui_link_battle_status();
  if (stt != (uint8_t)UI_LKB_RUNNING) {
    report_once(won_now());
    if (s_mode != BTM_RESULT) set_mode(BTM_RESULT);
    return;
  }
  if (s_mode == BTM_WAIT && ui_link_battle_wants_action()) to_menu();
}

void battle_update(uint32_t now_ms) {
  // 20 fps, renewed every loop. rd_set_fps() only moves the REQUEST and
  // rd_fps() caps that at FPS_LOW while a web client is live (ui/render.h:161),
  // so a screen whose whole job is a moving transcript has to hold the rate the
  // way actfx does: a short, self-extinguishing hold that dies within
  // BT_FPS_HOLD_MS of the last update() this screen runs.
  ui_hold_fps((uint8_t)FPS_NORMAL, (uint16_t)BT_FPS_HOLD_MS);

  // THE GUARD render() AND input() ALREADY CARRY, and update() was the one hook
  // without it. A DIAG entry whose battle_init() is refused sits at BTM_INTRO
  // with s_live 0: the INTRO timeout below then ran to_menu() on a dead battle
  // and left s_mode at BTM_MENU while render() was drawing the start-error page
  // and battle_screen_mode() was answering BTM_MENU. Not exploitable - B still
  // leaves - but a screen whose reported mode disagrees with its picture is a
  // screen no test can be written against. The fps hold stays ABOVE this line:
  // the pick list is a live screen with s_live still 0.
  if (!s_live) return;

  if (linked()) { link_frame(now_ms); return; }

  if (s_mode == BTM_INTRO && (uint32_t)(now_ms - s_mode_ms) >= BT_INTRO_MS) {
    to_menu();
    return;
  }
  if (s_mode == BTM_RESOLVE && s_ev_live &&
      (uint32_t)(now_ms - s_ev_ms) >= BT_EV_MS) {
    advance_playback();
  }
}

void battle_leave(void) {
  // Leaving by ANY route - B, LONG_BOTH, a push, the auto-return that SF_STICKY
  // keeps off this screen anyway - reports exactly once.
  //
  // AND IT REPORTS THE ENGINE'S OWN VERDICT, WHICH IS THE FIX FOR A REAL
  // UNDER-AWARD. This said report_once(0u): a flat loss, on the argument that
  // "a battle abandoned before it ended pays nothing". The argument is right
  // and the code did not implement it, because a battle is DECIDED inside
  // battle_step_round() while its victory transcript is still playing - the
  // outcome is BO_WIN_A for the four beats it takes to show the last blow, the
  // faint and the BATTLE_END line, roughly 2.4 s. LONG_BOTH (the global HOME
  // invariant; SCR_BATTLE carries no SF_LOCK_INPUT) or a hatch ceremony
  // arriving inside that window reported the WIN as a LOSS and paid nothing,
  // while B in the identical state ran end_playback() and paid. Same battle,
  // same state, different exit, different reward. MEASURED before the fix: 35
  // of 64 seeded practice battles sit decided-but-unreported in that window.
  //
  // An abandoned battle is still BO_UNDECIDED, so won_now() is 0 for it and the
  // original intent is unchanged: walking out of a fight in progress earns
  // nothing.
  report_once(won_now());
  // AND THE LINK GOES BACK, ON EVERY ROUTE OUT OF THIS SCREEN. ui/screen_link.cpp
  // deliberately does NOT release the radio when it is pushed aside (that push
  // is not a departure), so this is the ONLY hook that runs on every exit -
  // including LONG_BOTH, which goes straight HOME and never runs the LINK
  // screen's leave() at all. Without this line a linked battle abandoned with
  // the HOME gesture would leave the radio up, the session unpumped and the
  // power ladder clamped at DIM for ever. It is called AFTER report_once(),
  // because the reward gate reads the session it is about to close.
  if (linked()) ui_link_battle_done();
  s_live  = 0;
  s_armed = 0;
}

// -----------------------------------------------------------------------------
//  RENDER
// -----------------------------------------------------------------------------
static uint8_t hp_pct(uint16_t cur, uint16_t max) {
  if (max == 0u) return 0u;
  if (cur >= max) return 100u;
  return (uint8_t)(((uint32_t)cur * 100u) / (uint32_t)max);
}

static void fill_art(BattleCombatantArt& out, uint8_t side, bool at_beat) {
  memset(&out, 0, sizeof out);
  const uint8_t slot = s_st.side[side].active;
  const BattleCombatant* c = battle_combatant(s_st, side, slot);
  out.name  = combatant_name(side, slot);
  out.team  = (uint8_t)BATTLE_TEAM_MAX;
  out.alive = battle_alive_count(s_st, side);
  if (c == nullptr) return;
  out.art_key = (slot < (uint8_t)BATTLE_TEAM_MAX) ? s_art[side][slot] : 0u;
  out.level   = c->level;
  const uint16_t hp = at_beat ? hp_at_beat(side, slot) : c->hp_cur;
  out.hp_pct  = hp_pct(hp, c->hp_max);
  out.fainted = (uint8_t)((hp == 0u) ? 1u : 0u);

  // The impact frame. Only the combatant the CURRENT beat is about, and only
  // for the two events that are a blow landing.
  if (at_beat && s_ev_live && s_ev < s_log.count) {
    const BattleEvent* e = battle_log_at(s_log, s_ev);
    if (e && e->kind == RLE_HIT && e->side != side) out.struck = 1u;
    if (e && e->kind == RLE_DOT && e->side == side && e->slot == slot) out.struck = 1u;
  }
}

// The two-frame idle phase, shared by both bodies so they breathe together.
static uint8_t body_frame(void) {
  return (uint8_t)((ui_now_ms() / UI_ANIM_FRAME_MS) & 1u);
}

static void draw_field(bool at_beat, const char* message) {
  BattleCombatantArt foe, you;
  fill_art(foe, s_foe(), at_beat);
  fill_art(you, s_me, at_beat);
  br_draw_field(foe, you, body_frame(), message);
}

// "RONDA n" over "<your HP>|<foe HP>". Both numbers ride in the header tag
// because the content band belongs to the list widget in MENU and to the field
// everywhere else, and a player choosing a move has to be able to see how much
// health is left on each side without changing mode to find out.
//
// TWO THINGS IT HAS TO GET RIGHT AND WHICH ARE EASY TO GET WRONG.
//  * WHICH ROUND. battle_s8_end_of_round() increments st.round, so after a step
//    st.round is the NEXT one. The MENU is about that next round and the
//    transcript is about the one just resolved, so they read different fields -
//    printing st.round under a transcript would number every round n+1.
//  * WHICH HP. During the playback the bars show the beat's HP (hp_at_beat), so
//    the tag has to as well; the settled value beside a mid-round bar is two
//    numbers for one fact.
static void draw_chrome(void) {
  char title[16];
  char tag[10];
  const uint16_t round = (s_mode == BTM_MENU || s_mode == BTM_SWITCH)
                           ? s_st.round : s_round_shown;
  // THE MOVE CLOCK, ON A LINKED BATTLE ONLY, AND IT IS THE SESSION'S. A round
  // neither device advances is closed by the retransmission ladder after
  // PROTO_RETX_MAX rungs, so the player really does have a deadline; showing it
  // is the honest alternative to letting it arrive as a broken link. See
  // link_battle_move_ms_left() in ui/screen_link.cpp for why neither screen
  // papers over it.
  const uint32_t left = (linked() && (s_mode == BTM_MENU || s_mode == BTM_SWITCH))
                          ? ui_link_battle_move_ms_left(ui_now_ms()) : 0u;
  // Both fields are clamped where they are FORMATTED and not where they are
  // read: a round is a byte and the ladder is nine seconds, so neither can
  // really reach these bounds - but a title buffer that depends on that is a
  // buffer nobody can check by looking at this line.
  const unsigned r_txt = (unsigned)((round ? round : 1u) % 1000u);
  const unsigned s_txt = (unsigned)(((left + 999u) / 1000u) % 100u);
  if (left != 0u)
    snprintf(title, sizeof title, "%s %u %us", S(STR_BT_ROUND), r_txt, s_txt);
  else
    snprintf(title, sizeof title, "%s %u", S(STR_BT_ROUND), r_txt);
  const BattleCombatant* a = battle_active(s_st, s_me);
  const BattleCombatant* b = battle_active(s_st, s_foe());
  const bool beat = (s_mode == BTM_RESOLVE);
  snprintf(tag, sizeof tag, "%u|%u",
           (unsigned)(a ? hp_pct(beat ? hp_at_beat(s_me, s_st.side[s_me].active) : a->hp_cur,
                                 a->hp_max) : 0u),
           (unsigned)(b ? hp_pct(beat ? hp_at_beat(s_foe(), s_st.side[s_foe()].active) : b->hp_cur,
                                 b->hp_max) : 0u));
  gfx_header(title, tag);
}

static void draw_pick(void) {
  static char rows[BT_PICK_ROWS][BT_ROW_CAP];
  static char vals[BT_PICK_ROWS][BT_VAL_CAP];
  const char* items[BT_PICK_ROWS];
  const char* values[BT_PICK_ROWS];

  for (uint8_t i = 0; i < (uint8_t)BOX_SLOTS; ++i) {
    const PebbleInstance* p = box_peek(i);
    if (p == nullptr) {
      snprintf(rows[i], BT_ROW_CAP, "%u %s", (unsigned)(i + 1u), S(STR_BOX_EMPTY));
      vals[i][0] = '\0';
    } else {
      const char* sp = pet_species_name(p->species_id);
      snprintf(rows[i], BT_ROW_CAP, "%u %s", (unsigned)(i + 1u),
               (p->nickname[0] != '\0') ? p->nickname
                                        : (sp ? sp : S_SPECIES(gene_species(p->genome))));
      bool chosen = false;
      for (uint8_t k = 0; k < s_pick_n; ++k) if (s_pick[k] == i) chosen = true;
      if (chosen) snprintf(vals[i], BT_VAL_CAP, "*%s%u", S(STR_ST_LEVEL), (unsigned)p->level);
      else        snprintf(vals[i], BT_VAL_CAP, "%s%u",  S(STR_ST_LEVEL), (unsigned)p->level);
    }
    items[i]  = rows[i];
    values[i] = vals[i];
  }
  snprintf(rows[BOX_SLOTS], BT_ROW_CAP, "%s", S(STR_BT_READY));
  snprintf(vals[BOX_SLOTS], BT_VAL_CAP, "%u/%u", (unsigned)s_pick_n,
           (unsigned)BATTLE_TEAM_MAX);
  items[BOX_SLOTS]  = rows[BOX_SLOTS];
  values[BOX_SLOTS] = vals[BOX_SLOTS];

  gfx_header(S(STR_BT_PICK), nullptr);
  gfx_list(items, BT_PICK_ROWS, s_cur, values, ui_now_ms());
  gfx_affordance(S(STR_AF_NEXT), S(STR_AF_BACK_SEL));
}

static void draw_menu(void) {
  static char rows[BT_MENU_ROWS][BT_ROW_CAP];
  static char vals[BT_MENU_ROWS][BT_VAL_CAP];
  const char* items[BT_MENU_ROWS];
  const char* values[BT_MENU_ROWS];

  const BattleCombatant* me = battle_active(s_st, s_me);
  for (uint8_t i = 0; i < (uint8_t)PB_MOVE_COUNT; ++i) {
    const AttackDef* a = me ? attack_get(me->moves[i]) : nullptr;
    snprintf(rows[i], BT_ROW_CAP, "%s", a ? S(a->name_idx) : "-");
    // A move nobody can pick still SAYS why: the cooldown that is running, not
    // an empty row the ring silently steps over.
    if (me && !battle_move_ready(*me, i))
      snprintf(vals[i], BT_VAL_CAP, "x%u", (unsigned)me->cooldown[i]);
    else if (a)
      snprintf(vals[i], BT_VAL_CAP, "%u", (unsigned)a->power);
    else
      vals[i][0] = '\0';
    items[i]  = rows[i];
    values[i] = vals[i];
  }
  snprintf(rows[BT_MENU_SWITCH], BT_ROW_CAP, "%s", S(STR_BT_SWITCH));
  snprintf(vals[BT_MENU_SWITCH], BT_VAL_CAP, "%u", (unsigned)battle_alive_count(s_st, s_me));
  items[BT_MENU_SWITCH]  = rows[BT_MENU_SWITCH];
  values[BT_MENU_SWITCH] = vals[BT_MENU_SWITCH];

  draw_chrome();
  gfx_list(items, BT_MENU_ROWS, s_cur, values, ui_now_ms());
  gfx_affordance(S(STR_AF_NEXT), S(STR_AF_BACK_SEL));
}

static void draw_switch(void) {
  static char rows[BT_SWITCH_ROWS][BT_ROW_CAP];
  static char vals[BT_SWITCH_ROWS][BT_VAL_CAP];
  const char* items[BT_SWITCH_ROWS];
  const char* values[BT_SWITCH_ROWS];

  for (uint8_t i = 0; i < (uint8_t)BATTLE_TEAM_MAX; ++i) {
    const BattleCombatant* c = battle_combatant(s_st, s_me, i);
    if (c == nullptr || (c->flags & BCF_PRESENT) == 0u) {
      snprintf(rows[i], BT_ROW_CAP, "%s", S(STR_BOX_EMPTY));
      vals[i][0] = '\0';
    } else {
      snprintf(rows[i], BT_ROW_CAP, "%s%s",
               (i == s_st.side[s_me].active) ? S(STR_BOX_ACTIVE) : "",
               combatant_name(s_me, i));
      snprintf(vals[i], BT_VAL_CAP, "%u%%", (unsigned)hp_pct(c->hp_cur, c->hp_max));
    }
    items[i]  = rows[i];
    values[i] = vals[i];
  }
  snprintf(rows[BATTLE_TEAM_MAX], BT_ROW_CAP, "%s", S(STR_ITEM_BACK));
  vals[BATTLE_TEAM_MAX][0] = '\0';
  items[BATTLE_TEAM_MAX]  = rows[BATTLE_TEAM_MAX];
  values[BATTLE_TEAM_MAX] = vals[BATTLE_TEAM_MAX];

  draw_chrome();
  gfx_list(items, BT_SWITCH_ROWS, s_cur, values, ui_now_ms());
  gfx_affordance(S(STR_AF_NEXT), S(STR_AF_BACK_SEL));
}

void battle_render(void) {
  if (s_mode == BTM_PICK)   { draw_pick();   return; }
  if (!s_live) {
    // battle_init() refused and said why. The screen shows the refusal rather
    // than an empty field: a state with no picture is indistinguishable from a
    // crash.
    gfx_header(S(STR_BT_TITLE), nullptr);
    gfx_text_fit(GF_BODY, 2, (int16_t)(UI_CONTENT_Y + 14), OLED_W - 4,
                 S(STR_BT_START_ERR));
    gfx_affordance(nullptr, S(STR_AF_BACK));
    return;
  }
  if (s_mode == BTM_MENU)   { draw_menu();   return; }
  if (s_mode == BTM_SWITCH) { draw_switch(); return; }

  draw_chrome();
  switch (s_mode) {
    case BTM_INTRO:
      draw_field(false, S(STR_BT_VS));
      gfx_affordance(nullptr, S(STR_AF_OK));
      break;
    case BTM_WAIT:
      // The peer is choosing. The field is drawn settled (not at a beat): there
      // is no transcript in flight and the bars must show where the round
      // actually stands.
      draw_field(false, S(STR_BT_WAIT_PEER));
      gfx_affordance(nullptr, S(STR_AF_CANCEL));
      break;
    case BTM_RESULT:
      draw_field(false, outcome_word(s_st.outcome));
      gfx_affordance(nullptr, S(STR_AF_OK));
      break;
    default:
      draw_field(true, s_msg);
      gfx_affordance(S(STR_AF_FWD), S(STR_AF_SEL));
      break;
  }
}

// -----------------------------------------------------------------------------
//  INPUT. Spec section 7: A steps, HOLD_R chooses, B walks back one level,
//  BOTH is help. SF_OWNS_BACK is what lets B mean "up one mode" here.
// -----------------------------------------------------------------------------
static void toggle_pick(uint8_t slot) {
  for (uint8_t i = 0; i < s_pick_n; ++i) {
    if (s_pick[i] != slot) continue;
    for (uint8_t k = (uint8_t)(i + 1u); k < s_pick_n; ++k) s_pick[k - 1u] = s_pick[k];
    --s_pick_n;
    return;
  }
  if (s_pick_n >= (uint8_t)BATTLE_TEAM_MAX) { ui_toast(STR_BT_FULL_TEAM); return; }
  if (!box_occupied(slot)) { ui_toast(STR_BOX_EMPTY); return; }
  s_pick[s_pick_n++] = slot;
}

static void pick_input(Gesture g) {
  switch (g) {
    case GST_TAP_L:
    case GST_HOLD_L: s_cur = ring_legal(s_cur, BT_PICK_ROWS, pick_row_legal); break;
    case GST_HOLD_R:
      if (s_cur < (uint8_t)BOX_SLOTS) { toggle_pick(s_cur); break; }
      if (s_pick_n == 0u) { ui_toast(STR_BT_NO_TEAM); break; }
      {
        const uint8_t r = start_battle();
        if (r != (uint8_t)BR_OK) { s_reject = r; ui_toast(STR_BT_START_ERR); }
      }
      break;
    case GST_TAP_R: ui_back(); break;
    default: break;
  }
}

void battle_input(Gesture g) {
  if (g == GST_BOTH) { ui_help(STR_BT_HELP); return; }
  if (s_mode == BTM_PICK) { pick_input(g); return; }
  if (!s_live) { if (g == GST_TAP_R || g == GST_HOLD_R) ui_back(); return; }

  switch (s_mode) {
    case BTM_INTRO:
      if (g == GST_TAP_R)          ui_back();
      else if (g == GST_NONE)      break;
      // Skipping the stare-down on a linked battle does not open the menu: the
      // lockstep says when a move is wanted, and until it does the honest
      // picture is "waiting".
      else if (linked())           set_mode(BTM_WAIT);
      else                         to_menu();
      break;

    case BTM_WAIT:
      // One thing to say here and it is "stop". battle_leave() reports whatever
      // the session has authorised, which mid-battle is nothing.
      if (g == GST_TAP_R) ui_back();
      break;

    case BTM_MENU:
      switch (g) {
        case GST_TAP_L:
        case GST_HOLD_L: s_cur = ring_legal(s_cur, BT_MENU_ROWS, menu_row_legal); break;
        case GST_HOLD_R:
          if (s_cur == BT_MENU_SWITCH) {
            s_cur = first_legal(BT_SWITCH_ROWS, switch_row_legal);
            set_mode(BTM_SWITCH);
          } else {
            const BattleAction a = { (uint8_t)BACT_ATTACK, s_cur };
            submit(a);
          }
          break;
        // The top of the ladder for a running battle: B abandons it. Nothing is
        // written and nothing is awarded - report_once(0) in battle_leave().
        case GST_TAP_R: ui_back(); break;
        default: break;
      }
      break;

    case BTM_SWITCH:
      switch (g) {
        case GST_TAP_L:
        case GST_HOLD_L: s_cur = ring_legal(s_cur, BT_SWITCH_ROWS, switch_row_legal); break;
        case GST_HOLD_R:
          if (s_cur >= (uint8_t)BATTLE_TEAM_MAX) { to_menu(); break; }
          {
            const BattleAction a = { (uint8_t)BACT_SWITCH, s_cur };
            submit(a);
          }
          break;
        case GST_TAP_R:
          // One level up the ladder - unless the engine says a replacement is
          // owed, in which case there is nothing above this list to go to.
          if (battle_side_must_switch(s_st, s_me)) ui_wiggle();
          else                                   to_menu();
          break;
        default: break;
      }
      break;

    case BTM_RESOLVE:
      if (g == GST_TAP_L || g == GST_HOLD_L) advance_playback();
      else if (g == GST_HOLD_R || g == GST_TAP_R) end_playback();
      break;

    case BTM_RESULT:
      if (g == GST_TAP_R || g == GST_HOLD_R) ui_back();
      break;

    default: break;
  }
}

// -----------------------------------------------------------------------------
//  THE THREE QUERIES THE SWEEP READS (screen_battle.h). Defined here, at the
//  bottom, because two of them are the input handler's own predicates asked
//  about the cursor rather than about a chosen row - so they cannot drift from
//  what the ring and submit() actually do without this file changing.
// -----------------------------------------------------------------------------
uint8_t battle_screen_cursor_reject(void) {
  if (!s_live) return (uint8_t)BR_OK;
  if (s_mode == BTM_MENU) {
    if (s_cur < (uint8_t)PB_MOVE_COUNT) {
      const BattleAction a = { (uint8_t)BACT_ATTACK, s_cur };
      return (uint8_t)battle_validate_action(s_st, s_me, a);
    }
    return (uint8_t)(any_switch_legal() ? BR_OK : BR_BAD_INDEX);
  }
  if (s_mode == BTM_SWITCH) {
    if (s_cur < (uint8_t)BATTLE_TEAM_MAX) {
      const BattleAction a = { (uint8_t)BACT_SWITCH, s_cur };
      return (uint8_t)battle_validate_action(s_st, s_me, a);
    }
    return (uint8_t)(battle_side_must_switch(s_st, s_me) ? BR_MUST_SWITCH : BR_OK);
  }
  return (uint8_t)BR_OK;
}

uint8_t battle_screen_blocked_rows(void) {
  if (!s_live) return 0u;
  uint8_t n = 0;
  if (s_mode == BTM_MENU) {
    for (uint8_t r = 0; r < BT_MENU_ROWS; ++r) if (!menu_row_legal(r)) ++n;
  } else if (s_mode == BTM_SWITCH) {
    for (uint8_t r = 0; r < BT_SWITCH_ROWS; ++r) if (!switch_row_legal(r)) ++n;
  }
  return n;
}

uint16_t battle_screen_hp_shown(uint8_t side) {
  if (side > 1u) return 0u;
  const uint8_t slot = s_st.side[side].active;
  return (s_mode == BTM_RESOLVE) ? hp_at_beat(side, slot)
                                 : s_st.side[side].team[slot].hp_cur;
}
