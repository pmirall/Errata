// =============================================================================
//  tests/test_minigames.cpp - the minigame contract (P3-C4a, spec section 29)
//
//  Spec 29's closing line is the acceptance criterion these tests exist for:
//  "All games must be deterministic/testable when supplied a fixed RNG seed."
//  Every case below runs the PURE logic with no clock, no Arduino and no
//  screen - which is the whole reason games/*_logic.cpp is a separate
//  translation unit from games/*_draw.cpp.
// =============================================================================
#include "nt_test.h"

#include "minigames/minigame.h"
#include "minigames/games/games.h"
#include "minigames/manager.h"

static const MgLogic* const ALL[] = {
  &MG_PING, &MG_SEQUENCE, &MG_PACKET_FLOOD, &MG_FIREWALL, &MG_BUFFER, &MG_DELETE
};
static const uint8_t ALL_N = (uint8_t)(sizeof(ALL) / sizeof(ALL[0]));
static_assert(sizeof(ALL) / sizeof(ALL[0]) == (size_t)MG_ID_COUNT,
              "every MgId must be under the contract tests, not just the pool");

// -----------------------------------------------------------------------------
//  A TAPE. The unit of reproducibility: a seed plus a scripted policy.
//  `policy` is called once per step and returns -1 for "no press this step",
//  or MG_SIDE_L / MG_SIDE_R.
// -----------------------------------------------------------------------------
typedef int8_t (*TapeFn)(const MgCtx&, uint32_t step);

// An invariant checked after EVERY press and EVERY step of a tape, not merely
// at the end. The two score-shape identities (firewall's event count, buffer's
// step count) are only worth anything if they are checked where they could
// first break, which is mid-run.
typedef bool (*InvFn)(const MgCtx&);

static uint16_t run_tape(const MgLogic& g, uint32_t seed, TapeFn policy,
                         uint32_t* steps_out = nullptr, InvFn inv = nullptr,
                         uint32_t* inv_fail = nullptr)
{
  MgCtx c;
  mg_begin(c, g, seed);
  uint32_t steps = 0;
  if (inv && !inv(c) && inv_fail) ++*inv_fail;
  while (!c.finished && steps < MG_MAX_STEPS + 4u) {
    const int8_t side = policy ? policy(c, steps) : (int8_t)-1;
    if (side >= 0) mg_press(c, g, (uint8_t)side);
    if (inv && !inv(c) && inv_fail) ++*inv_fail;
    if (!c.finished) mg_tick(c, g);
    if (inv && !inv(c) && inv_fail) ++*inv_fail;
    ++steps;
  }
  if (steps_out) *steps_out = steps;
  return mg_score(c, g);
}

// The LADDER a run climbs: every distinct value c.score passes through, in
// order, with the times stripped out. Comparing final scores alone is not
// enough - a time bonus on a run that already reaches the ceiling is invisible
// after mg_score() clamps, which is exactly how a "faster pays" term could be
// added to this game and go unnoticed.
static uint8_t score_ladder(const MgLogic& g, uint32_t seed, TapeFn policy,
                            uint16_t* out, uint8_t max_n)
{
  MgCtx c;
  mg_begin(c, g, seed);
  uint8_t  n = 0;
  uint16_t prev = 0xFFFFu;
  uint32_t steps = 0;
  while (!c.finished && steps < MG_MAX_STEPS + 4u) {
    const int8_t side = policy ? policy(c, steps) : (int8_t)-1;
    if (side >= 0) mg_press(c, g, (uint8_t)side);
    if (c.score != prev && n < max_n) { out[n++] = c.score; prev = c.score; }
    if (!c.finished) mg_tick(c, g);
    if (c.score != prev && n < max_n) { out[n++] = c.score; prev = c.score; }
    ++steps;
  }
  return n;
}

// Policies -------------------------------------------------------------------
static int8_t tape_idle(const MgCtx&, uint32_t)  { return -1; }

// PING: press the lit side the moment it lights.
static int8_t tape_ping_perfect(const MgCtx& c, uint32_t)
{
  return ping_is_lit(c) ? (int8_t)ping_target(c) : (int8_t)-1;
}
// PING: press immediately, always too early.
static int8_t tape_ping_early(const MgCtx& c, uint32_t)
{
  return ping_is_lit(c) ? (int8_t)-1 : (int8_t)MG_SIDE_L;
}
// SEQUENCE: reproduce what was shown, one symbol per step once playback ends.
static int8_t tape_seq_perfect(const MgCtx& c, uint32_t)
{
  if (seq_is_showing(c)) return -1;
  // The tape may read the sequence: this is a scripted PLAYER, and what is
  // under test is the engine's reproducibility, not the player's memory.
  extern uint8_t seq_symbol_at(const MgCtx&, uint8_t);
  return (int8_t)seq_symbol_at(c, seq_pos_of(c));
}
static int8_t tape_seq_wrong(const MgCtx& c, uint32_t)
{
  if (seq_is_showing(c)) return -1;
  extern uint8_t seq_symbol_at(const MgCtx&, uint8_t);
  return (int8_t)(1u - seq_symbol_at(c, seq_pos_of(c)));
}
// SEQUENCE, PLAYED AS SLOWLY AS THE RULES ALLOW: correct every time, but on the
// LAST legal step of every answer deadline. This is the tape the ceiling case
// needs, because sequence is the one game whose run length a PLAYER extends -
// every correct press buys another SEQ_ANSWER_MS - so an idle run (which ends
// at the first unanswered deadline, 2.1 s in) is nowhere near its worst case.
static int8_t tape_seq_stretch(const MgCtx& c, uint32_t)
{
  if (seq_is_showing(c)) return -1;
  // One more step and seq_step() would end the run on the deadline: press now.
  if (c.t_ms + MG_STEP_MS < seq_deadline_ms(c)) return -1;
  return (int8_t)seq_symbol_at(c, seq_pos_of(c));
}

// GENERIC tapes, which is what the contract cases below run over every game.
// They used to be PING's, applied to every game - which meant SEQUENCE was
// driven by ping_is_lit() reading SeqState's bytes as a PingState: a
// deterministic tape, but an accidental one, and one that reads a union of two
// unrelated structs. A mash is a mash on any game and needs no such trick.
static int8_t tape_mash_l  (const MgCtx&, uint32_t)          { return MG_SIDE_L; }
static int8_t tape_mash_r  (const MgCtx&, uint32_t)          { return MG_SIDE_R; }
static int8_t tape_mash_alt(const MgCtx&, uint32_t step)     { return (int8_t)(step & 1u); }
static int8_t tape_mash_rl (const MgCtx&, uint32_t step)     { return (int8_t)(1u - (step & 1u)); }

// --- PACKET FLOOD -----------------------------------------------------------
// THE SORT, played from what the PLAYER can see and nothing else: the digit on
// the head packet, and the digit on each mouth. It never asks the game which
// side is right, so a game that stopped putting the answer on the mouths would
// fail this tape rather than quietly keep passing.
static int8_t tape_pf_sort(const MgCtx& c, uint32_t)
{
  if (pf_queue_len(c) == 0u) return -1;
  const uint8_t d = pf_digit_at(c, 0);
  if (pf_mouth_digit(c, MG_SIDE_L) == d) return MG_SIDE_L;
  if (pf_mouth_digit(c, MG_SIDE_R) == d) return MG_SIDE_R;
  return -1;                               // unreachable: the digits are distinct
}
// The same sort, played on the last step before each packet expires.
static int8_t tape_pf_sort_late(const MgCtx& c, uint32_t step)
{
  if (pf_life_left_ms(c) > MG_STEP_MS) return -1;
  return tape_pf_sort(c, step);
}
static int8_t tape_pf_wrong(const MgCtx& c, uint32_t)
{
  const uint8_t w = pf_want_side(c);
  return (w == PF_NO_HEAD) ? (int8_t)-1 : (int8_t)(1u - w);
}

// --- FIREWALL ---------------------------------------------------------------
static int8_t tape_fw_perfect(const MgCtx& c, uint32_t)
{
  const uint8_t sh = fw_shield(c), ln = fw_lane(c);
  if (sh < ln) return MG_SIDE_R;
  if (sh > ln) return MG_SIDE_L;
  return -1;
}
// Handicapped: never moves before the packet is halfway down, and sits out one
// whole packet. That is what "competent" means here - not perfect.
static int8_t tape_fw_competent(const MgCtx& c, uint32_t step)
{
  if (c.round == 3u) return -1;
  if (fw_fall_pos(c) < 128u) return -1;
  return tape_fw_perfect(c, step);
}
// ADJACENT, deliberately: parks one lane away from the packet, every time.
static int8_t tape_fw_adjacent(const MgCtx& c, uint32_t)
{
  const uint8_t ln   = fw_lane(c);
  const uint8_t want = (uint8_t)((ln + 1u < fw_lanes()) ? ln + 1u : ln - 1u);
  const uint8_t sh   = fw_shield(c);
  if (sh < want) return MG_SIDE_R;
  if (sh > want) return MG_SIDE_L;
  return -1;
}

// CHASES the packet all the way down and then steps OFF the lane just before
// impact. It is in the right place for almost the whole fall and in the wrong
// place at the one instant that is sampled - which is the difference between
// "be there when it lands" and "pass through at some point".
static int8_t tape_fw_leave_early(const MgCtx& c, uint32_t step)
{
  const uint8_t sh = fw_shield(c), ln = fw_lane(c);
  if (fw_fall_pos(c) >= 200u) {
    if (sh != ln) return -1;                       // already clear of it
    return (uint8_t)(ln + 1u) < fw_lanes() ? (int8_t)MG_SIDE_R : (int8_t)MG_SIDE_L;
  }
  return tape_fw_perfect(c, step);
}

// --- BUFFER -----------------------------------------------------------------
// A pursuit controller: aim the cursor's velocity at the zone's, plus a term
// for the position error. It reads the Q4 internals, exactly as tape_seq_perfect
// reads the sequence it is reproducing - what is under test is the engine, not
// a simulated thumb.
static int8_t tape_buf_track(const MgCtx& c, uint32_t)
{
  const int16_t err  = (int16_t)(buf_zone_q4(c) - buf_cursor_q4(c));
  const int16_t want = (int16_t)(buf_zone_vel_q4(c) + err / 4);
  const int16_t v    = buf_vel_q4(c);
  const int16_t half = (int16_t)(buf_kick_q4() / 2);
  if (v + half < want) return MG_SIDE_R;
  if (v - half > want) return MG_SIDE_L;
  return -1;
}
// THE PARKER: steer once to the middle of the band and then hold still, letting
// the zone come to you. The strategy the shrink is aimed at.
static int8_t tape_buf_park(const MgCtx& c, uint32_t)
{
  const int16_t mid  = (int16_t)((buf_track_px() / 2) * 16);
  const int16_t err  = (int16_t)(mid - buf_cursor_q4(c));
  const int16_t want = (int16_t)(err / 4);
  const int16_t v    = buf_vel_q4(c);
  const int16_t half = (int16_t)(buf_kick_q4() / 2);
  if (v + half < want) return MG_SIDE_R;
  if (v - half > want) return MG_SIDE_L;
  return -1;
}

// THE CAMPER: latch where the zone IS at t = 0 and hold that column for the
// whole run. It is the parker with the best possible guess - the strategy a
// player would actually find if the zone did not move - and it is the tape that
// states spec 29.4's "MOVING safe zone" as a score: if the zone ever stopped
// drifting, this would be a perfect run.
static int16_t s_buf_camp_q4 = 0;
static int8_t tape_buf_camp(const MgCtx& c, uint32_t step)
{
  if (step == 0u) s_buf_camp_q4 = buf_zone_q4(c);     // where it started
  const int16_t err  = (int16_t)(s_buf_camp_q4 - buf_cursor_q4(c));
  const int16_t want = (int16_t)(err / 4);
  const int16_t v    = buf_vel_q4(c);
  const int16_t half = (int16_t)(buf_kick_q4() / 2);
  if (v + half < want) return MG_SIDE_R;
  if (v - half > want) return MG_SIDE_L;
  return -1;
}

// --- DELETE -----------------------------------------------------------------
// Walk to the corrupted block with the shortest fuse and purge it. `late` holds
// the press until the fuse is nearly out, which is the whole point of the pair.
static int8_t del_triage(const MgCtx& c, bool late)
{
  uint8_t  best = 0xFFu;
  uint16_t best_life = 0xFFFFu;
  for (uint8_t i = 0; i < del_slots(); ++i) {
    if (del_kind_at(c, i) != DEL_KIND_CORRUPT) continue;
    const uint16_t l = del_life_left_ms(c, i);
    if (l < best_life) { best_life = l; best = i; }
  }
  if (best == 0xFFu) return -1;
  if (del_cursor(c) != best) return MG_SIDE_L;           // walk
  if (late && best_life > 100u) return -1;               // wait for the fuse
  return MG_SIDE_R;                                      // purge
}
static int8_t tape_del_now (const MgCtx& c, uint32_t) { return del_triage(c, false); }
static int8_t tape_del_late(const MgCtx& c, uint32_t) { return del_triage(c, true); }
// NEVER WALKS: purges only what happens to land under the caret it started on.
static int8_t tape_del_nowalk(const MgCtx& c, uint32_t)
{
  return (del_kind_at(c, del_cursor(c)) == DEL_KIND_CORRUPT) ? (int8_t)MG_SIDE_R
                                                             : (int8_t)-1;
}
// GREEDY: walks to the nearest block of EITHER kind and purges it. It has the
// selection dimension - it chooses a target and travels to it - and lacks only
// the reading, so it isolates "which block" from "walk at all".
static int8_t tape_del_greedy(const MgCtx& c, uint32_t)
{
  uint8_t  best = 0xFFu;
  uint16_t best_life = 0xFFFFu;
  for (uint8_t i = 0; i < del_slots(); ++i) {
    if (del_kind_at(c, i) == DEL_KIND_EMPTY) continue;
    const uint16_t l = del_life_left_ms(c, i);
    if (l < best_life) { best_life = l; best = i; }
  }
  if (best == 0xFFu) return -1;
  return (del_cursor(c) != best) ? (int8_t)MG_SIDE_L : (int8_t)MG_SIDE_R;
}
// PING'S WINNING REFLEX: press the moment anything appears, anywhere.
static int8_t tape_del_reflex(const MgCtx& c, uint32_t)
{
  for (uint8_t i = 0; i < del_slots(); ++i)
    if (del_kind_at(c, i) != DEL_KIND_EMPTY) return MG_SIDE_R;
  return -1;
}
// The blind sweep, started once the board has something on it: walk the ring
// and purge every fourth step without looking.
static int8_t tape_del_sweep(const MgCtx&, uint32_t step)
{
  if (step < 20u) return -1;
  return (step % 4u == 0u) ? (int8_t)MG_SIDE_R : (int8_t)MG_SIDE_L;
}

// What a seed actually laid out, as one number: an idle run's whole visible
// state, folded step by step. It is a per-game signature written once, so
// a_different_seed_gives_a_different_game can ask the question of every game
// without asking it about the score - which an idle run leaves at 0 for all six.
static uint32_t layout_sig(const MgLogic& g, uint32_t seed)
{
  MgCtx c;
  mg_begin(c, g, seed);
  uint32_t h = 2166136261u;
  do {
    for (uint8_t i = 0; i < MG_STATE_BYTES; ++i)
      h = (uint32_t)((h ^ c.state[i]) * 16777619u);
  } while (mg_tick(c, g));
  return h;
}

// -----------------------------------------------------------------------------
//  THE CONTRACT, over every game
// -----------------------------------------------------------------------------
static TapeFn kGeneric[] = { tape_idle, tape_mash_l, tape_mash_r, tape_mash_alt,
                             tape_mash_rl };
static const uint8_t kGenericN = (uint8_t)(sizeof(kGeneric) / sizeof(kGeneric[0]));

TEST(every_game_is_reproducible_from_a_seed_and_a_tape) {
  for (uint8_t i = 0; i < ALL_N; ++i) {
    for (uint32_t seed = 1; seed <= 8u; ++seed) {
      for (uint8_t t = 0; t < kGenericN; ++t) {
        // The same seed and the same tape, twice: byte-identical scores.
        const uint16_t a = run_tape(*ALL[i], seed, kGeneric[t]);
        const uint16_t b = run_tape(*ALL[i], seed, kGeneric[t]);
        CHECK_EQ(a, b);
      }
    }
  }
}

// THE LONGEST TAPE PER GAME. For five of the six the idle tape is the worst
// case - nobody presses anything, so the game must run itself out - but that is
// NOT true of SEQUENCE, and saying it was is how this case came to test the one
// game a player can stretch with the one tape that cannot stretch it: an idle
// sequence ends at 2,100 ms, while pressing correctly on the last legal step
// every time runs 14,100 ms, 6.7x longer and 900 ms from the ceiling instead of
// 12,900. Every game is driven by BOTH, and by every generic tape besides.
static TapeFn kStretch[] = { tape_idle, tape_seq_stretch, tape_idle, tape_idle,
                             tape_idle, tape_idle };
static_assert(sizeof(kStretch) / sizeof(kStretch[0]) == (size_t)MG_ID_COUNT,
              "one longest-run tape per MgId, in MgId order");

TEST(every_game_finishes_inside_the_fifteen_second_ceiling) {
  for (uint8_t i = 0; i < ALL_N; ++i) {
    for (uint32_t seed = 1; seed <= 32u; ++seed) {
      // MG_MAX_STEPS is the manager's backstop, and reaching it would mean the
      // game never ends on its own - assert the game beats it, not merely that
      // the backstop works.
      for (uint8_t t = 0; t <= kGenericN; ++t) {
        uint32_t steps = 0;
        (void)run_tape(*ALL[i], seed, (t < kGenericN) ? kGeneric[t]
                                                      : kStretch[i], &steps);
        CHECK(steps < MG_MAX_STEPS);
        CHECK((steps * MG_STEP_MS) < MG_MAX_MS);
      }
    }
  }
}

TEST(the_stretch_tape_really_does_stretch_the_one_game_that_can_be_stretched) {
  // Guarding the guard: if tape_seq_stretch ever stopped holding the deadline
  // open, the case above would go back to measuring an idle run without saying
  // so. Measured: 84 steps idle, 564 stretched (2,100 ms against 14,100).
  for (uint32_t seed = 1; seed <= 8u; ++seed) {
    uint32_t idle_steps = 0, long_steps = 0;
    (void)run_tape(MG_SEQUENCE, seed, tape_idle,         &idle_steps);
    (void)run_tape(MG_SEQUENCE, seed, tape_seq_stretch,  &long_steps);
    CHECK(long_steps > idle_steps * 4u);
    // ... and the stretched run is still a WON run: it is slow, not wrong.
    CHECK_EQ(run_tape(MG_SEQUENCE, seed, tape_seq_stretch), MG_SCORE_MAX);
  }
}

TEST(every_game_scores_inside_zero_to_a_thousand) {
  for (uint8_t i = 0; i < ALL_N; ++i) {
    for (uint32_t seed = 1; seed <= 16u; ++seed) {
      for (uint8_t t = 0; t < kGenericN; ++t) {
        const uint16_t s = run_tape(*ALL[i], seed, kGeneric[t]);
        CHECK(s <= MG_SCORE_MAX);
      }
    }
  }
}

TEST(every_game_wears_the_name_and_the_hint_of_its_own_id) {
  // THE THIRD LINK IN THE ROW-ORDER CHAIN, and the only one nothing held. The
  // PLAY rows are folded against MgId by a static_assert in screen_care.cpp and
  // their LABELS are held by the play_list golden - but the pair a RUN draws
  // (ui.cpp takes the header from mgr_logic()->name_idx and the intro hint from
  // ->hint_idx) is chosen inside each *_logic.cpp, where a copy-paste can hand
  // FIREWALL buffer's name and hint and nothing at all notices: the build is
  // clean, every golden matches, and on the device the player picks CORTAFUEGOS
  // off the list and is shown a card titled BUFFER, for every seed, silently.
  // Both string blocks are contiguous and in MgId order (static_asserts in
  // core/strings_es.h), so the pairing is one subtraction.
  CHECK_EQ(mg_pool_count(), (uint8_t)MG_ID_COUNT);
  for (uint8_t i = 0; i < ALL_N; ++i) {
    const MgLogic* g = mg_logic_by_id(i);          // the POOL the device reads
    CHECK(g != nullptr);
    if (g == nullptr) continue;
    CHECK(g == ALL[i]);                            // ... is the list these cases drive
    CHECK_EQ(g->id, i);
    CHECK_EQ(g->name_idx, (uint16_t)(STR_MG_PING + i));
    CHECK_EQ(g->hint_idx, (uint16_t)(STR_MG_PING_HINT + i));
  }
}

TEST(a_different_seed_gives_a_different_game) {
  // Not a different SCORE necessarily - an idle run scores 0 whatever the
  // seed - so compare the thing the seed actually drives: the layout.
  MgCtx a, b;
  mg_begin(a, MG_PING, 1u);
  mg_begin(b, MG_PING, 2u);
  bool differs = (ping_arm_ms(a) != ping_arm_ms(b)) || (ping_target(a) != ping_target(b));
  CHECK(differs);

  // The same, per game. Against the WHOLE run's layout rather than its first
  // draw: two seeds sharing an opening lane or an opening zone position is
  // ordinary (five lanes and thirty-one start columns), and a case that
  // compares only the first number is a case that fails on an unlucky pair
  // rather than on a broken game. layout_sig() folds every step of an idle run.
  for (uint8_t i = 0; i < ALL_N; ++i) {
    CHECK(layout_sig(*ALL[i], 1u) != layout_sig(*ALL[i], 2u));
    CHECK(layout_sig(*ALL[i], 7u) != layout_sig(*ALL[i], 8u));
    // ... and the same seed twice is the same layout, which is the other half.
    CHECK_EQ(layout_sig(*ALL[i], 3u), layout_sig(*ALL[i], 3u));
  }
}

// -----------------------------------------------------------------------------
//  PING
// -----------------------------------------------------------------------------
TEST(ping_perfect_play_scores_the_maximum_and_idle_play_scores_nothing) {
  for (uint32_t seed = 1; seed <= 8u; ++seed) {
    const uint16_t perfect = run_tape(MG_PING, seed, tape_ping_perfect);
    const uint16_t idle    = run_tape(MG_PING, seed, tape_idle);
    CHECK_EQ(idle, 0u);
    // Pressing on the first lit step cannot be beaten, and the five rounds
    // are worth 200 each.
    CHECK(perfect > 900u);
  }
}

TEST(ping_punishes_an_early_press_with_a_lost_round_and_nothing_worse) {
  // "Mash the button" must not win. It also must not cost a stat - spec 27
  // forbids punishment - so all it does is spend the round at zero.
  for (uint32_t seed = 1; seed <= 8u; ++seed) {
    CHECK_EQ(run_tape(MG_PING, seed, tape_ping_early), 0u);
  }
}

TEST(ping_leaves_a_note_the_draw_layer_can_toast_but_never_toasts_itself) {
  MgCtx c;
  mg_begin(c, MG_PING, 3u);
  CHECK_EQ(c.note, 0u);
  mg_press(c, MG_PING, MG_SIDE_L);              // instantly: before the light
  CHECK_EQ(c.note, (uint16_t)STR_GM_TOOSOON);
}

// -----------------------------------------------------------------------------
//  SEQUENCE
// -----------------------------------------------------------------------------
TEST(sequence_perfect_play_walks_the_whole_ladder) {
  for (uint32_t seed = 1; seed <= 8u; ++seed) {
    CHECK_EQ(run_tape(MG_SEQUENCE, seed, tape_seq_perfect), MG_SCORE_MAX);
  }
}

TEST(sequence_gives_partial_credit_rather_than_zero_for_a_wrong_symbol) {
  // Wrong on the very first symbol: round 0 of 3, so zero - but the game ends
  // rather than continuing, and a later mistake keeps what was earned.
  for (uint32_t seed = 1; seed <= 4u; ++seed) {
    CHECK_EQ(run_tape(MG_SEQUENCE, seed, tape_seq_wrong), 0u);
  }
}

TEST(sequence_ignores_presses_while_it_is_still_showing_the_sequence) {
  MgCtx c;
  mg_begin(c, MG_SEQUENCE, 5u);
  CHECK(seq_is_showing(c));
  const uint8_t pos0 = seq_pos_of(c);
  mg_press(c, MG_SEQUENCE, MG_SIDE_L);
  mg_press(c, MG_SEQUENCE, MG_SIDE_R);
  CHECK_EQ(seq_pos_of(c), pos0);                // nothing advanced
  CHECK_EQ(c.finished, 0u);                     // and nothing ended
}

// =============================================================================
//  PACKET FLOOD (29.2). The one rule that makes it itself: THE CORRECT CHANNEL
//  IS READ OFF THE PACKET. Not a reaction test - see the late tape below.
// =============================================================================
TEST(packet_flood_is_a_sort_and_the_visible_digits_are_the_whole_rule) {
  for (uint32_t seed = 1; seed <= 16u; ++seed) {
    // tape_pf_sort never asks which side is correct: it matches the head
    // packet's digit against the two mouth digits, which is exactly what the
    // player can see. If the address on a packet ever stopped deciding where it
    // goes, this is the case that would fail.
    CHECK_EQ(run_tape(MG_PACKET_FLOOD, seed, tape_pf_sort), MG_SCORE_MAX);
    CHECK_EQ(run_tape(MG_PACKET_FLOOD, seed, tape_pf_wrong), 0u);
    CHECK_EQ(run_tape(MG_PACKET_FLOOD, seed, tape_idle),     0u);
  }
}

TEST(packet_flood_the_head_packets_address_is_on_the_mouth_it_must_go_to) {
  // The same statement as a direct invariant rather than through a score: at
  // every step at which there is something to route, the digit on the packet
  // equals the digit on the mouth pf_press() will accept - through the reroute
  // and out the other side.
  for (uint32_t seed = 1; seed <= 8u; ++seed) {
    MgCtx c;
    mg_begin(c, MG_PACKET_FLOOD, seed);
    uint32_t seen = 0;
    while (mg_tick(c, MG_PACKET_FLOOD)) {
      const uint8_t want = pf_want_side(c);
      if (want == PF_NO_HEAD) continue;
      CHECK_EQ(pf_mouth_digit(c, want), pf_digit_at(c, 0));
      // ... and the OTHER mouth is a different digit, so the sort has an answer
      // rather than two.
      CHECK(pf_mouth_digit(c, (uint8_t)(1u - want)) != pf_digit_at(c, 0));
      ++seen;
    }
    CHECK(seen > 100u);
  }
}

TEST(packet_flood_scores_a_late_but_correct_run_exactly_like_an_early_one) {
  // THE SHAPE TEST that keeps this from being PING. tape_pf_sort_late waits
  // until the last step before each packet expires; PING's score is a function
  // of milliseconds and could not pass this.
  for (uint32_t seed = 1; seed <= 16u; ++seed) {
    const uint16_t now  = run_tape(MG_PACKET_FLOOD, seed, tape_pf_sort);
    const uint16_t late = run_tape(MG_PACKET_FLOOD, seed, tape_pf_sort_late);
    CHECK_EQ(now, late);
    CHECK_EQ(late, MG_SCORE_MAX);

    // ... and not merely at the end. Both runs climb the SAME ladder of 18
    // values, one of them a second and a half behind the other. Comparing only
    // the finals would miss a speed bonus entirely, because mg_score() clamps
    // it away on a run that already reaches the ceiling.
    uint16_t la[24], lb[24];
    const uint8_t na = score_ladder(MG_PACKET_FLOOD, seed, tape_pf_sort,      la, 24u);
    const uint8_t nb = score_ladder(MG_PACKET_FLOOD, seed, tape_pf_sort_late, lb, 24u);
    CHECK_EQ(na, nb);
    CHECK_EQ(na, (uint8_t)(pf_packets() + 1u));
    for (uint8_t i = 0; i < na && i < nb; ++i) CHECK_EQ(la[i], lb[i]);
  }
}

TEST(packet_flood_the_reroute_swaps_the_mouths_and_not_the_addresses) {
  // Cut the reroute and this game is PING with a digit on it, so it gets a case
  // of its own: the two mouths exchange digits exactly once, mid-run, while the
  // packet at a given index keeps the address it was born with.
  MgCtx c;
  mg_begin(c, MG_PACKET_FLOOD, 6u);
  const uint8_t before_l = pf_mouth_digit(c, MG_SIDE_L);
  const uint8_t before_r = pf_mouth_digit(c, MG_SIDE_R);
  uint8_t swaps = 0;
  uint8_t prev  = before_l;
  bool    note_seen = false;
  while (mg_tick(c, MG_PACKET_FLOOD)) {
    const uint8_t now_l = pf_mouth_digit(c, MG_SIDE_L);
    if (now_l != prev) { ++swaps; prev = now_l; }
    if (c.note == STR_GM_REROUTE) { note_seen = true; const_cast<MgCtx&>(c).note = 0u; }
  }
  CHECK_EQ(swaps, 1u);                       // exactly once, never back again
  CHECK(note_seen);                          // and it is announced
  CHECK_EQ(pf_mouth_digit(c, MG_SIDE_L), before_r);
  CHECK_EQ(pf_mouth_digit(c, MG_SIDE_R), before_l);
}

static bool inv_pf_every_packet_resolves_once(const MgCtx& c)
{
  return (uint16_t)(pf_good(c) + pf_bad(c) + pf_lost(c)) == (uint16_t)pf_head(c);
}

TEST(packet_flood_resolves_every_packet_exactly_once_routed_lost_or_wrong) {
  // The head only ever advances, and it advances exactly once per packet - by a
  // press or by an expiry, never both and never neither. Checked after every
  // press and every step, because the expiry loop and the press path share
  // pf_resolve() and a second call from either would show up here first.
  static TapeFn kTapes[] = { tape_idle, tape_pf_sort, tape_pf_sort_late,
                             tape_pf_wrong, tape_mash_alt };
  for (uint8_t t = 0; t < 5u; ++t) {
    for (uint32_t seed = 1; seed <= 8u; ++seed) {
      uint32_t fails = 0;
      (void)run_tape(MG_PACKET_FLOOD, seed, kTapes[t], nullptr,
                     inv_pf_every_packet_resolves_once, &fails);
      CHECK_EQ(fails, 0u);
    }
  }
  // ... and at the end every packet has been accounted for.
  MgCtx c;
  mg_begin(c, MG_PACKET_FLOOD, 9u);
  while (mg_tick(c, MG_PACKET_FLOOD)) { }
  CHECK_EQ((uint16_t)(pf_good(c) + pf_bad(c) + pf_lost(c)), (uint16_t)pf_packets());
  CHECK_EQ(pf_lost(c), pf_packets());        // nobody pressed anything
}

TEST(packet_flood_ends_at_the_same_instant_however_it_is_played) {
  // The schedule is a function of the packet index, so no press can move the
  // end of the run - only retire a packet before its expiry.
  uint32_t idle_steps = 0, sort_steps = 0;
  for (uint32_t seed = 1; seed <= 8u; ++seed) {
    (void)run_tape(MG_PACKET_FLOOD, seed, tape_idle,    &idle_steps);
    (void)run_tape(MG_PACKET_FLOOD, seed, tape_pf_sort, &sort_steps);
    CHECK_EQ(idle_steps, 467u);              // 11,675 ms, the hard end
    CHECK(sort_steps <= idle_steps);         // playing can only shorten it
    CHECK((idle_steps * MG_STEP_MS) < MG_MAX_MS);
  }
}

TEST(packet_flood_makes_a_one_button_mash_worth_almost_nothing) {
  // A misroute costs 3 where a route pays 2, so a fair guess is worth -0.5 a
  // packet and 18 of them are worth a clamped ZERO. This is a DISTRIBUTION, and
  // the design says so: a mash needs 15 of 18 lucky bits to clear the 500 at
  // which minigames_won increments, which is about 1 run in 265. Asserting
  // "< 500 on this seed" would be asserting a coin flip; assert the shape of
  // the distribution instead, over enough seeds to see it.
  const uint32_t kSeeds = 2048u;
  uint32_t sum = 0, over = 0;
  for (uint32_t seed = 1; seed <= kSeeds; ++seed) {
    const uint16_t v = run_tape(MG_PACKET_FLOOD, seed, tape_mash_l);
    sum += v;
    if (v >= 500u) ++over;
  }
  CHECK((sum / kSeeds) < 100u);              // measured 32 of 1000
  CHECK((over * 100u) < kSeeds);             // measured 0.32%, cap 1%
}

// =============================================================================
//  FIREWALL (29.3). The one rule that makes it itself: THE HIT IS DISCRETE, and
//  the score is a count of events. The mirror of buffer, below.
// =============================================================================
static bool inv_fw_score_is_a_count(const MgCtx& c)
{
  return c.score == (uint16_t)(fw_blocked(c) * (MG_SCORE_MAX / 8u));
}

TEST(firewall_perfect_play_is_exactly_the_maximum_and_idle_play_exactly_zero) {
  for (uint32_t seed = 1; seed <= 16u; ++seed) {
    CHECK_EQ(run_tape(MG_FIREWALL, seed, tape_fw_perfect), MG_SCORE_MAX);
    // EQUALITY, not an inequality: a packet's lane is drawn from the four lanes
    // the shield is NOT in, so a motionless shield blocks nothing structurally
    // rather than merely unluckily.
    CHECK_EQ(run_tape(MG_FIREWALL, seed, tape_idle), 0u);
    // Competent play - late, and one packet sat out - still clears the 500 that
    // increments minigames_won.
    CHECK(run_tape(MG_FIREWALL, seed, tape_fw_competent) >= 500u);
  }
}

TEST(firewall_scores_a_count_of_events_and_nothing_else) {
  // THE ANTI-COLLAPSE ASSERTION, checked after every press and every step: the
  // score is fw_blocked() * 125 and can be nothing else. Add partial credit for
  // being one lane away and this game becomes BUFFER; this is what fails first.
  static TapeFn kTapes[] = { tape_idle, tape_fw_perfect, tape_fw_competent,
                             tape_fw_adjacent, tape_mash_alt, tape_mash_l };
  for (uint8_t t = 0; t < 6u; ++t) {
    for (uint32_t seed = 1; seed <= 8u; ++seed) {
      uint32_t fails = 0;
      (void)run_tape(MG_FIREWALL, seed, kTapes[t], nullptr,
                     inv_fw_score_is_a_count, &fails);
      CHECK_EQ(fails, 0u);
    }
  }
}

TEST(firewall_pays_nothing_at_all_for_being_one_lane_away) {
  // The other half of "discrete": a shield parked ADJACENT to every packet -
  // as close as it is possible to be without being there - scores exactly what
  // a shield at the far wall scores.
  for (uint32_t seed = 1; seed <= 16u; ++seed) {
    CHECK_EQ(run_tape(MG_FIREWALL, seed, tape_fw_adjacent), 0u);
  }
}

TEST(firewall_samples_one_instant_so_passing_through_the_lane_is_worth_nothing) {
  // The SAME distinction in time rather than in space, and the one the adjacent
  // tape cannot make: tape_fw_leave_early sits in the correct lane for four
  // fifths of every fall and steps out just before the packet lands. An overlap
  // window - "you were there at some point" - would pay it everything. One
  // sampled instant pays it nothing, which is what makes stopping the skill.
  for (uint32_t seed = 1; seed <= 16u; ++seed) {
    CHECK_EQ(run_tape(MG_FIREWALL, seed, tape_fw_leave_early), 0u);
  }
}

TEST(firewall_makes_mashing_worth_less_than_playing) {
  // Sweeping through a lane is worth nothing: the impact is one sampled step,
  // not an overlap window. A one-sided mash walks into a wall and blocks the one
  // packet drawn into the lane it is stuck in.
  for (uint32_t seed = 1; seed <= 16u; ++seed) {
    CHECK(run_tape(MG_FIREWALL, seed, tape_mash_alt) < 500u);
    CHECK(run_tape(MG_FIREWALL, seed, tape_mash_rl)  < 500u);
    CHECK(run_tape(MG_FIREWALL, seed, tape_mash_l)   < 500u);
  }
}

// =============================================================================
//  BUFFER (29.4). The one rule that makes it itself: THE SCORE IS TIME SPENT
//  INSIDE. No events, no rounds, no arrivals. The mirror of firewall, above.
// =============================================================================
static bool inv_buf_score_is_time(const MgCtx& c)
{
  return c.score == (uint16_t)((uint32_t)buf_inside_steps(c) * MG_SCORE_MAX /
                               buf_scored_steps());
}

TEST(buffer_scores_time_inside_and_nothing_else) {
  // THE ANTI-COLLAPSE ASSERTION, checked after every press and every step. Add
  // "the player just entered the zone, give them something" and this game
  // becomes FIREWALL; this is what fails first.
  static TapeFn kTapes[] = { tape_idle, tape_buf_track, tape_buf_park,
                             tape_mash_alt, tape_mash_r };
  for (uint8_t t = 0; t < 5u; ++t) {
    for (uint32_t seed = 1; seed <= 8u; ++seed) {
      uint32_t fails = 0;
      (void)run_tape(MG_BUFFER, seed, kTapes[t], nullptr,
                     inv_buf_score_is_time, &fails);
      CHECK_EQ(fails, 0u);
    }
  }
}

TEST(buffer_never_moves_the_score_by_an_event_sized_amount) {
  // The other half of "continuous": the score creeps by 1000/340 = 2 or 3 per
  // step and never jumps. A per-event award of any size a player would notice
  // shows up here even if it happens to keep the identity above true.
  //
  // IT HAS TO BE DRIVEN BY A TAPE THAT SCORES, and the first version of this
  // case was not: it pressed nothing, and an idle run never enters the zone at
  // all (the walls absorb the cursor, and the band the zone wanders excludes
  // both walls by static_assert). Its score was therefore 0 at every single
  // step, `worst` was 0 by construction and CHECK(worst <= 3) was CHECK(0 <= 3)
  // - an assertion that could not fail whatever this game did with a press.
  // Measured on the shipped code: worst = 3 under either tape below and 0 with
  // no presses; under a mutant paying 5 steps for ENTERING the zone it is 15,
  // and 117 at 40. The CHECK(worst >= 2) is what keeps the tape honest, so a
  // future edit cannot quietly turn this back into a tautology.
  static TapeFn kTapes[] = { tape_buf_track, tape_buf_park };
  for (uint8_t t = 0; t < 2u; ++t) {
    for (uint32_t seed = 1; seed <= 8u; ++seed) {
      MgCtx c;
      mg_begin(c, MG_BUFFER, seed);
      uint16_t prev = 0, worst = 0;
      uint32_t steps = 0;
      while (!c.finished && steps < MG_MAX_STEPS + 4u) {
        const int8_t side = kTapes[t](c, steps);
        if (side >= 0) mg_press(c, MG_BUFFER, (uint8_t)side);
        if (!c.finished) mg_tick(c, MG_BUFFER);
        const uint16_t d = (uint16_t)(c.score - prev);
        if (d > worst) worst = d;
        prev = c.score;
        ++steps;
      }
      CHECK(worst >= 2u);            // the run scored: the bound below is live
      CHECK(worst <= 3u);            // ... and it creeps, one step at a time
    }
  }
}

TEST(buffer_tracking_beats_parking_and_idle_play_scores_exactly_nothing) {
  uint32_t park_sum = 0;
  const uint32_t kSeeds = 32u;
  for (uint32_t seed = 1; seed <= kSeeds; ++seed) {
    const uint16_t track = run_tape(MG_BUFFER, seed, tape_buf_track);
    const uint16_t park  = run_tape(MG_BUFFER, seed, tape_buf_park);
    // A continuous-pursuit ceiling is not provable for every seed the way an
    // event count is, so this is >= 950 rather than == 1000. (Measured: 1000
    // for every seed in this range.)
    CHECK(track >= 950u);
    CHECK(park < track);            // for EVERY seed, not on average
    // The zone can never touch a wall (static_assert in buffer_logic.cpp), and
    // an untouched cursor never leaves one.
    CHECK_EQ(run_tape(MG_BUFFER, seed, tape_idle), 0u);
    park_sum += park;
  }
  // Parking is bounded by 2*hw/band, which is about 280 of 1000 over the run -
  // under the 500 at which minigames_won increments. It clears 500 on a few
  // seeds in 256, which is why this is a mean and the per-seed assertion above
  // is against tracking rather than against a constant.
  CHECK((park_sum / kSeeds) < 400u);
}

TEST(buffer_presses_are_acceleration_and_not_position) {
  // One press is an impulse, not a move: the cursor keeps travelling for about
  // half a second afterwards and then stops on its own. A firewall-shaped
  // press - "the cursor is now here" - would move it exactly once.
  MgCtx c;
  mg_begin(c, MG_BUFFER, 5u);
  CHECK_EQ(buf_cursor_q4(c), 0);
  mg_press(c, MG_BUFFER, MG_SIDE_R);
  uint8_t moved = 0;
  for (uint8_t i = 0; i < 40u; ++i) {
    const int16_t before = buf_cursor_q4(c);
    mg_tick(c, MG_BUFFER);
    if (buf_cursor_q4(c) != before) ++moved;
  }
  CHECK(moved >= 8u);                        // it glides
  CHECK_EQ(buf_vel_q4(c), 0);                // and the friction stops it
  CHECK(buf_cursor_q4(c) > 0);
}

TEST(buffer_the_safe_zone_never_stops_moving) {
  // Spec 29.4 is "keep a cursor inside a MOVING safe zone", and until this case
  // existed nothing in the suite said so: freezing the zone (s.zc + 0 in
  // buf_step) left all 45 cases green and was caught only by a pixel golden,
  // which a legitimate `make -C tests screens` re-record would have erased.
  // The zone must travel further than its own opening width (2 * 12 px), so a
  // frozen or barely-crawling zone fails here rather than in a bitmap.
  // Measured over these seeds: 32 px at the least, 72 at the most; 0 frozen.
  for (uint32_t seed = 1; seed <= 32u; ++seed) {
    MgCtx c;
    mg_begin(c, MG_BUFFER, seed);
    int16_t lo = buf_zone_q4(c), hi = lo;
    while (mg_tick(c, MG_BUFFER)) {
      const int16_t z = buf_zone_q4(c);
      if (z < lo) lo = z;
      if (z > hi) hi = z;
    }
    const int16_t travel_px = (int16_t)((hi - lo) / 16);
    CHECK(travel_px >= 24);
  }
}

TEST(buffer_camping_where_the_zone_started_loses_to_following_it) {
  // The same rule as a SCORE, which is the half that cannot be re-recorded: a
  // player who steers once to the zone's opening position and holds there must
  // do strictly worse than one who keeps chasing it, for every seed. With the
  // zone frozen this tape is a perfect run on every seed, which is exactly the
  // collapse - "keep a cursor inside a moving zone" would have become "put the
  // cursor in the right place once".
  uint32_t camp_sum = 0;
  const uint32_t kSeeds = 32u;
  for (uint32_t seed = 1; seed <= kSeeds; ++seed) {
    const uint16_t track = run_tape(MG_BUFFER, seed, tape_buf_track);
    const uint16_t camp  = run_tape(MG_BUFFER, seed, tape_buf_camp);
    CHECK(camp < track);                   // for EVERY seed, not on average
    camp_sum += camp;
  }
  // Measured: mean 183 over these seeds against a tracker's 1000, and 1000 flat
  // if the zone stops moving.
  CHECK((camp_sum / kSeeds) < 400u);
}

// =============================================================================
//  DELETE (29.5). The one rule that makes it itself: THE PLAYER CHOOSES WHICH
//  BLOCK. A rationed, indiscriminate purge over simultaneous typed targets.
// =============================================================================
TEST(delete_forces_a_choice_because_two_corrupted_blocks_share_the_board) {
  // The proof in delete_logic.cpp's header, as a measurement: corrupted blocks
  // co-exist unless every pair of their indices is 3 apart, which cannot fit.
  // So every run, for every seed, has an instant with two of them up at once
  // and a caret that can only be in one place.
  for (uint32_t seed = 1; seed <= 32u; ++seed) {
    MgCtx c;
    mg_begin(c, MG_DELETE, seed);
    uint8_t most = 0;
    while (mg_tick(c, MG_DELETE)) {
      uint8_t n = 0;
      for (uint8_t i = 0; i < del_slots(); ++i)
        if (del_kind_at(c, i) == DEL_KIND_CORRUPT) ++n;
      if (n > most) most = n;
    }
    CHECK(most >= 2u);
  }
}

TEST(delete_cannot_reach_the_ceiling_without_choosing_where_to_go) {
  // A player who never walks purges only what happens to land under the caret
  // they started on. Every one of their shots is correct - so this is not about
  // accuracy - and they still cannot clear the board.
  for (uint32_t seed = 1; seed <= 32u; ++seed) {
    const uint16_t walk   = run_tape(MG_DELETE, seed, tape_del_now);
    const uint16_t nowalk = run_tape(MG_DELETE, seed, tape_del_nowalk);
    CHECK_EQ(walk, MG_SCORE_MAX);
    CHECK(nowalk < walk);
    CHECK(nowalk <= MG_SCORE_MAX / 2u);      // measured 0..500, mean about 250
    CHECK_EQ(run_tape(MG_DELETE, seed, tape_idle), 0u);
  }
}

TEST(delete_scores_the_same_purged_on_sight_or_on_the_last_step_of_the_fuse) {
  // THE SHAPE TEST that keeps this from being PING: the score is a count of
  // targets and does not know what time it is. PING's is a function of
  // milliseconds and would fail this on its first round.
  for (uint32_t seed = 1; seed <= 32u; ++seed) {
    uint32_t now_steps = 0, late_steps = 0;
    const uint16_t now  = run_tape(MG_DELETE, seed, tape_del_now,  &now_steps);
    const uint16_t late = run_tape(MG_DELETE, seed, tape_del_late, &late_steps);
    CHECK_EQ(now, late);
    CHECK_EQ(late, MG_SCORE_MAX);
    CHECK(late_steps > now_steps);           // it really was played later

    // ... and not merely at the end: the two runs climb the SAME ladder,
    // 0/166/333/500/666/833/1000, one of them several seconds behind the other.
    uint16_t la[16], lb[16];
    const uint8_t na = score_ladder(MG_DELETE, seed, tape_del_now,  la, 16u);
    const uint8_t nb = score_ladder(MG_DELETE, seed, tape_del_late, lb, 16u);
    CHECK_EQ(na, nb);
    CHECK_EQ(na, (uint8_t)(del_bad_total() + 1u));
    for (uint8_t i = 0; i < na && i < nb; ++i) CHECK_EQ(la[i], lb[i]);
  }
}

TEST(delete_pays_only_for_the_corrupted_and_a_healthy_block_is_not_a_target) {
  // A charge spent on a healthy block buys NOTHING - it clears the block, costs
  // the charge and leaves the score where it was. This is the rule that makes
  // "which one" a question at all, so it is checked directly rather than only
  // through a tape: a purge that counted any block would leave every score-based
  // case in this file passing, because the tapes that walk already pick the
  // corrupted ones and the tapes that do not walk barely reach anything.
  for (uint32_t seed = 1; seed <= 8u; ++seed) {
    MgCtx c;
    mg_begin(c, MG_DELETE, seed);
    uint8_t slot = 0xFFu;
    while (slot == 0xFFu && mg_tick(c, MG_DELETE)) {
      for (uint8_t i = 0; i < del_slots(); ++i)
        if (del_kind_at(c, i) == DEL_KIND_HEALTHY) { slot = i; break; }
    }
    CHECK(slot != 0xFFu);
    // Walking is free and costs no time, so the caret can be put on it here.
    while (del_cursor(c) != slot) mg_press(c, MG_DELETE, MG_SIDE_L);
    const uint8_t  charges = del_charges(c);
    const uint8_t  cleared = del_cleared(c);
    const uint16_t score   = c.score;
    mg_press(c, MG_DELETE, MG_SIDE_R);
    CHECK_EQ(del_cleared(c), cleared);                  // it was not a target
    CHECK_EQ(c.score, score);                           // and it paid nothing
    CHECK_EQ(del_charges(c), (uint8_t)(charges - 1u));  // but it cost a charge
    CHECK_EQ(del_kind_at(c, slot), (uint8_t)DEL_KIND_EMPTY);  // the purge is blind
  }
}

TEST(delete_makes_reading_the_board_worth_more_than_merely_walking_it) {
  // The same shape as a tape: tape_del_greedy chooses a target and travels to
  // it, exactly as tape_del_now does, and differs ONLY in not reading which
  // kind it is. It must score strictly less, for every seed.
  for (uint32_t seed = 1; seed <= 32u; ++seed) {
    const uint16_t read   = run_tape(MG_DELETE, seed, tape_del_now);
    const uint16_t greedy = run_tape(MG_DELETE, seed, tape_del_greedy);
    CHECK_EQ(read, MG_SCORE_MAX);
    CHECK(greedy < read);
  }
}

TEST(delete_gives_a_rationed_purge_so_a_blind_sweep_buys_almost_nothing) {
  for (uint32_t seed = 1; seed <= 32u; ++seed) {
    // Seven charges, spent without looking: about one kill.
    CHECK(run_tape(MG_DELETE, seed, tape_del_sweep) < 500u);
    // And PING's winning reflex - press the instant anything appears - is worth
    // less still, because the caret is almost never already there.
    CHECK(run_tape(MG_DELETE, seed, tape_del_reflex) < 500u);
  }
}

TEST(delete_leaves_the_wasted_charge_note_once_per_run_and_not_once_per_waste) {
  // An unlatched note buries the board under seven toasts in three seconds, and
  // lands them on exactly the player it is trying to teach.
  MgCtx c;
  mg_begin(c, MG_DELETE, 3u);
  CHECK_EQ(c.note, 0u);
  uint8_t notes = 0;
  for (uint8_t i = 0; i < 5u && !c.finished; ++i) {
    mg_press(c, MG_DELETE, MG_SIDE_R);       // nothing has spawned yet: all waste
    if (c.note == STR_GM_WASTED) { ++notes; c.note = 0u; }
  }
  CHECK_EQ(notes, 1u);
  CHECK_EQ(c.score, 0u);                     // and a wasted charge costs no points
}

TEST(delete_spends_a_charge_on_every_purge_and_ends_when_they_run_out) {
  MgCtx c;
  mg_begin(c, MG_DELETE, 4u);
  CHECK_EQ(del_charges(c), del_charges_max());
  for (uint8_t i = 0; i < del_charges_max(); ++i) {
    CHECK_EQ(c.finished, 0u);
    mg_press(c, MG_DELETE, MG_SIDE_R);
  }
  CHECK_EQ(del_charges(c), 0u);
  CHECK_EQ(c.finished, 1u);
  // Walking is free and unlimited, which is why it is on the other button.
  mg_begin(c, MG_DELETE, 4u);
  for (uint8_t i = 0; i < 50u; ++i) mg_press(c, MG_DELETE, MG_SIDE_L);
  CHECK_EQ(del_charges(c), del_charges_max());
  CHECK_EQ(c.finished, 0u);
}

// -----------------------------------------------------------------------------
//  THE DRIVER
// -----------------------------------------------------------------------------
TEST(a_press_after_the_end_can_never_score) {
  MgCtx c;
  mg_begin(c, MG_PING, 7u);
  while (mg_tick(c, MG_PING)) { }
  const uint16_t s = mg_score(c, MG_PING);
  for (uint8_t i = 0; i < 20u; ++i) {
    mg_press(c, MG_PING, MG_SIDE_L);
    mg_press(c, MG_PING, MG_SIDE_R);
  }
  CHECK_EQ(mg_score(c, MG_PING), s);
}

TEST(mg_score_is_idempotent_so_a_double_read_cannot_double_report) {
  MgCtx c;
  mg_begin(c, MG_PING, 11u);
  while (mg_tick(c, MG_PING)) { }
  const uint16_t a = mg_score(c, MG_PING);
  CHECK_EQ(mg_score(c, MG_PING), a);
  CHECK_EQ(mg_score(c, MG_PING), a);
}

TEST(the_clock_is_the_step_count_and_nothing_else) {
  MgCtx c;
  mg_begin(c, MG_PING, 13u);
  CHECK_EQ(c.t_ms, 0u);
  mg_tick(c, MG_PING);
  CHECK_EQ(c.t_ms, MG_STEP_MS);
  mg_tick(c, MG_PING);
  CHECK_EQ(c.t_ms, 2u * MG_STEP_MS);
}

// =============================================================================
//  THE MANAGER
//
//  The property these exist for: finish() is reported EXACTLY ONCE per game.
//  The code this replaced got there by accident - ui_game_leave() scored an
//  abandoned game as a loss and re-entry was guarded by a phase check - so any
//  second exit path would have double-counted minigames_won AND the XP ledger.
// =============================================================================
static uint8_t  g_reports;
static uint8_t  g_last_id;
static uint16_t g_last_score;
static uint16_t g_report_sum;

static void spy_report(uint8_t id, uint16_t score)
{
  ++g_reports;
  g_last_id    = id;
  g_last_score = score;
  g_report_sum = (uint16_t)(g_report_sum + score);
}

static void spy_reset(void)
{
  g_reports = 0; g_last_id = 0xFF; g_last_score = 0xFFFF; g_report_sum = 0;
  mgr_bind_report(spy_report);
}

// Drive the manager in 25 ms slices until it stops or the guard trips.
static uint32_t mgr_drain(uint32_t max_ms = 200000u)
{
  uint32_t t = 0;
  while (mgr_tick(MG_STEP_MS) && t < max_ms) t += MG_STEP_MS;
  return t;
}

TEST(the_manager_reports_each_game_exactly_once_when_played_to_the_end) {
  spy_reset();
  mgr_begin(MG_ID_PING, 42u, MGR_SEQ_LEN);
  // Nobody presses: every game runs itself out, and the "next?" card times out
  // after the FIRST game, so exactly one game is played and reported once.
  mgr_drain();
  CHECK_EQ(g_reports, 1u);
  CHECK(!mgr_active());
}

TEST(the_manager_reports_a_game_abandoned_mid_run_exactly_once) {
  // THE BUG THIS TEST EXISTS FOR. Leave the screen while a game is running.
  spy_reset();
  mgr_begin(MG_ID_PING, 7u, MGR_SEQ_LEN);
  for (uint8_t i = 0; i < 100u; ++i) mgr_tick(MG_STEP_MS);   // into MGR_RUN
  CHECK_EQ(mgr_phase(), (uint8_t)MGR_RUN);
  CHECK_EQ(g_reports, 0u);
  mgr_abort();
  CHECK_EQ(g_reports, 1u);
  // ... and every further exit path must add nothing.
  mgr_abort();
  mgr_back();
  mgr_drain();
  CHECK_EQ(g_reports, 1u);
}

TEST(quitting_with_B_reports_once_and_never_again) {
  spy_reset();
  mgr_begin(MG_ID_PING, 9u, MGR_SEQ_LEN);
  for (uint8_t i = 0; i < 100u; ++i) mgr_tick(MG_STEP_MS);
  mgr_back();                       // B during the run: quit
  CHECK_EQ(g_reports, 1u);
  mgr_back();
  mgr_abort();
  mgr_drain();
  CHECK_EQ(g_reports, 1u);
}

TEST(a_full_sequence_reports_once_per_game_and_averages_them) {
  spy_reset();
  mgr_begin(MG_ID_PING, 5u, MGR_SEQ_LEN);
  uint32_t guard = 0;
  while (mgr_active() && guard < 400000u) {
    // Answer the "next?" card with A so the whole sequence is played.
    if (mgr_phase() == (uint8_t)MGR_NEXT) mgr_press(MG_SIDE_L);
    mgr_tick(MG_STEP_MS);
    guard += MG_STEP_MS;
  }
  CHECK_EQ(g_reports, MGR_SEQ_LEN);
  CHECK_EQ(mgr_count(), MGR_SEQ_LEN);
}

TEST(the_manager_never_reports_a_game_it_did_not_start) {
  spy_reset();
  mgr_abort();                      // idle
  mgr_back();
  mgr_drain();
  CHECK_EQ(g_reports, 0u);
}

TEST(a_sequence_is_reproducible_from_its_seed) {
  uint8_t ids_a[MGR_SEQ_LEN] = {0}, ids_b[MGR_SEQ_LEN] = {0};
  for (uint8_t pass = 0; pass < 2u; ++pass) {
    spy_reset();
    mgr_begin(MG_ID_PING, 12345u, MGR_SEQ_LEN);
    uint8_t seen = 0; uint32_t guard = 0;
    while (mgr_active() && guard < 400000u) {
      if (mgr_logic() && seen < MGR_SEQ_LEN) {
        const uint8_t id = mgr_logic()->id;
        if (seen == 0 || (pass ? ids_b : ids_a)[seen - 1] != id ||
            mgr_index() == seen) {
          if (mgr_index() == seen) (pass ? ids_b : ids_a)[seen++] = id;
        }
      }
      if (mgr_phase() == (uint8_t)MGR_NEXT) mgr_press(MG_SIDE_L);
      mgr_tick(MG_STEP_MS);
      guard += MG_STEP_MS;
    }
  }
  for (uint8_t i = 0; i < MGR_SEQ_LEN; ++i) CHECK_EQ(ids_a[i], ids_b[i]);
}

TEST(the_chrome_runs_on_real_time_but_the_game_runs_on_whole_steps) {
  // Drive with a ragged frame time. The score must not change with it, which
  // is the whole reason mgr_tick() accumulates into whole MG_STEP_MS units.
  uint16_t scores[2] = {0, 0};
  const uint32_t kSlice[2] = { MG_STEP_MS, 37u };   // 25 ms vs a ragged 37 ms
  for (uint8_t v = 0; v < 2u; ++v) {
    spy_reset();
    mgr_begin(MG_ID_PING, 99u, 1u);
    uint32_t guard = 0;
    while (mgr_active() && guard < 400000u) { mgr_tick(kSlice[v]); guard += kSlice[v]; }
    scores[v] = g_report_sum;
  }
  CHECK_EQ(scores[0], scores[1]);
}

// The invariant, fuzzed over interleavings of every exit path rather than the
// three the tests above walk by hand. This is what would catch a phase added
// to the manager that reports without leaving RUN - the shape of the original
// double-count bug. Deterministic: the "randomness" is a fixed LCG.
//
// TWO THINGS THIS TEST NEEDED BEFORE IT COULD CATCH ANYTHING, both found by
// mutating the manager and watching it stay green:
//
//  * RAGGED FRAME TIMES. Driving a flat MG_STEP_MS means mgr_tick()'s inner
//    step loop runs exactly once per call, so the loop's exit condition is
//    never exercised. A real frame is 20-50 ms, and one after a flash write
//    can be 300, which runs the loop a dozen times.
//  * RESTARTING. back/abort are two of the eight actions, so the manager went
//    idle within a few steps and the remaining ~595 iterations of each trial
//    tested nothing at all.
TEST(the_report_count_never_exceeds_the_games_started) {
  for (uint32_t trial = 1; trial <= 200u; ++trial) {
    spy_reset();
    mgr_begin(MG_ID_PING, trial, MGR_SEQ_LEN);
    uint16_t started = 1u;            // mgr_begin() arms the first one
    uint8_t  last_index = 0u;

    uint32_t r = trial * 2654435761u;
    for (uint16_t k = 0; k < 600u; ++k) {
      r = r * 1103515245u + 12345u;
      switch ((r >> 16) % 8u) {
        case 0: mgr_back();            break;
        case 1: mgr_abort();           break;
        case 2: mgr_press(MG_SIDE_L);  break;
        case 3: mgr_press(MG_SIDE_R);  break;
        default: mgr_tick((uint32_t)(5u + ((r >> 8) % 320u))); break;
      }
      if (mgr_active() && mgr_index() != last_index) { ++started; last_index = mgr_index(); }

      // THE INVARIANT: a game reports at most once, so the running total can
      // never outrun the number of games that were armed.
      CHECK(g_reports <= started);

      // Keep a live sequence under the fuzz. Without this the manager is idle
      // for almost the whole trial and the exit paths are never re-entered.
      if (!mgr_active()) {
        mgr_begin((uint8_t)(r % MG_ID_COUNT), r ^ trial, MGR_SEQ_LEN);
        ++started;
        last_index = 0u;
      }
    }
    CHECK(g_reports <= started);
  }
}
