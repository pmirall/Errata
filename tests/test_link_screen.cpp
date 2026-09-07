// =============================================================================
//  Pebblebol host tests - test_link_screen.cpp
//  THE LINK SCREEN, ITS CONSENT GATE AND THE BATTLE OVER THE LINK
//  (plan P7-C2 and P7-C3, spec sections 42 and 47).
//
//  ONE SCREEN AND ONE REAL PEER, IN ONE PROCESS. A screen is a file-scope
//  singleton, so two LINK screens cannot coexist in one binary - but a Session
//  is CALLER-OWNED (networking/session.h says why, and tools/check.sh gates the
//  absence of file-scope mutables in the pure networking modules), so the far
//  end of every case below is a REAL Session with a REAL BattleState and the
//  REAL game/battle_ai.cpp driving it, over the other end of a REAL
//  transport_loopback.cpp.
//
//  WHAT IS FAKE, so a green run is not over-read: the RADIO (a LinkRadioDriver
//  of function pointers and a queue of beacons, tests/fakes/link_fake.h) and the
//  ui.h seams. The beacon codec, the peer table, the session FSM, the lockstep,
//  the battle engine, the validator and the Box are the shipping modules.
//  NOTHING HERE PROVES ANYTHING ABOUT ESP-NOW: no Wi-Fi task, no real channel,
//  no real duplicate, no callback threading. That is a bench item and it is
//  left unticked.
//
// -----------------------------------------------------------------------------
//  THE TWO GATES THIS FILE EXISTS FOR
// -----------------------------------------------------------------------------
//  THE CONSENT GATE. A session that was never accepted must not battle. Three
//  cases hold it from three directions: a peer already shouting HELLO into an
//  un-consented screen changes nothing; one device pressing A alone ends as
//  CONEXIÓN PERDIDA and never pushes a battle; and the radio is bound by the A
//  press and by nothing else.
//
//  THE REWARD GATE. A DESYNC must not pay. ui/screen_battle.cpp's won_now()
//  reads ui_link_battle_status(), which answers UI_LKB_WON only where
//  networking/session.h's session_rewards_authorised() is true - so a
//  divergence, a dead link and a walk-out all report won == 0 and leave the Box
//  byte-identical.
// =============================================================================
#include "nt_test.h"

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/nt_types.h"
#include "core/strings_es.h"
#include "fakes/gfx_fb.h"
#include "fakes/link_fake.h"
#include "game/activity.h"
#include "data/balance.h"      // XP_LEVEL_MAX: the band both devices offer
#include "game/battle.h"
#include "game/battle_ai.h"
#include "game/box.h"
#include "game/cooldowns.h"
#include "game/evolution.h"
#include "game/genome.h"
#include "game/validate.h"
#include "game/pebble.h"
#include "game/species.h"
#include "networking/battle_link.h"
#include "networking/discovery.h"
#include "networking/session.h"
#include "networking/trade_link.h"
#include "game/trade.h"
#include "networking/protocol.h"
#include "networking/transport.h"
#include "ui/screen.h"
#include "ui/screen_battle.h"
#include "ui/screen_link.h"
#include "ui/ui.h"

// =============================================================================
//  THE ui.h SEAMS THE FAKE RADIO DOES NOT COVER
// =============================================================================
static uint32_t g_now     = 100000u;
static uint32_t g_idle    = 0;
static uint16_t g_toast   = STR_EMPTY;
static uint16_t g_help    = STR_EMPTY;
static int      g_backs   = 0;
static int      g_wiggles = 0;
static int      g_results = 0;
static uint8_t  g_res_entry = 0xFF;
static uint8_t  g_res_won   = 0xFF;
static int      g_commits = 0;
static uint32_t g_dev_id  = 0x11110000u;
static CooldownTable g_cds;
static uint8_t  g_epoch_cal = (uint8_t)CAL_USER;
static uint32_t g_epoch     = 1700000000u;

// The mini navigation machine. app/state_machine.cpp's sm_goto() runs the
// LEAVING screen's leave() hook and then the arriving screen's enter(), and
// sm_push() stacks - this models exactly that and no more, because the
// s_handoff guard in ui/screen_link.cpp is a statement ABOUT that ordering and
// a harness that skipped the leave would not test it.
static uint8_t g_screen = (uint8_t)SCR_LINK;
static uint8_t g_stack[4];
static uint8_t g_sp = 0;
static int     g_pushes = 0;
static uint8_t g_last_push = 0xFF;

static void screen_leave(uint8_t s) {
  if (s == (uint8_t)SCR_LINK)   link_leave();
  if (s == (uint8_t)SCR_BATTLE) battle_leave();
}
static void screen_enter(uint8_t s) {
  if (s == (uint8_t)SCR_LINK)   link_enter();
  if (s == (uint8_t)SCR_BATTLE) battle_enter();
}
static void nav_goto(uint8_t s) {
  if (s != g_screen) screen_leave(g_screen);
  g_screen = s;
  screen_enter(s);
}

uint32_t ui_now_ms(void)  { return g_now; }
uint32_t ui_idle_ms(void) { return g_idle; }
void ui_toast(uint16_t id){ g_toast = id; }
void ui_help(uint16_t id) { g_help = id; }
void ui_wiggle(void)      { ++g_wiggles; }
void ui_push(ScreenId s)  {
  ++g_pushes;
  g_last_push = (uint8_t)s;
  if (g_sp < 4u) g_stack[g_sp++] = g_screen;
  nav_goto((uint8_t)s);
}
void ui_back(void) {
  ++g_backs;
  nav_goto(g_sp ? g_stack[--g_sp] : (uint8_t)SCR_HOME);
}
void ui_battle_result(uint8_t entry, uint8_t won) {
  ++g_results; g_res_entry = entry; g_res_won = won;
}
void ui_hold_fps(uint8_t, uint16_t) {}
void ui_flash(uint16_t)             {}
void ui_shake(uint8_t, uint16_t)    {}
void ui_explore_clock(uint32_t* e, uint32_t* ms, uint8_t* cal) {
  if (e)   *e   = g_epoch;
  if (ms)  *ms  = g_now;
  if (cal) *cal = g_epoch_cal;
}
uint32_t ui_device_seed(void)     { return g_dev_id; }
CooldownTable& ui_cooldowns(void) { return g_cds; }
void ui_explore_commit(uint8_t)   { ++g_commits; }
// The spinner request. The screen asks for one more frame while it is
// browsing; there is no frame scheduler here to ask.
void ui_request_frame(void)       {}

// =============================================================================
//  A REAL BOX, built the way tests/test_battle_screen.cpp builds one:
//  box_new_pebble() rather than a hand-drawn mock, so the movesets, the ids and
//  the levels are the ones the firmware would hand the wire.
// =============================================================================
static GameState g_gs;
// The level the Box fixture is built at. A case that has to KNOW which side
// wins sets it; everything else leaves it where two evenly matched teams meet.
static uint8_t g_box_level = 6u;

static void box_fixture(uint8_t occupied) {
  memset(&g_gs, 0, sizeof g_gs);
  box_bind(g_gs);
  Genome gen;
  memset(&gen, 0, sizeof gen);
  gen.magic_ver  = GENOME_MAGIC_VER;
  gen.lineage_id = 0x0BADF00Du;
  gen.g0 = 0x1234u; gen.g1 = 0x5678u; gen.g2 = 0x9ABCu;
  gen.generation = 3;
  // SEALED. box_new_pebble() takes the genome as given and the shared validator
  // refuses an unsealed one (VR_BAD_GENOME) - which every earlier screen test
  // got away with because none of them put a Pebble on a wire.
  genome_seal(gen);
  for (uint8_t i = 0; i < occupied; ++i) {
    const uint8_t slot = box_new_pebble((uint8_t)(1u + i * 4u),
                                        (uint8_t)(g_box_level + i * 2u),
                                        ORIGIN_STARTER, gen, 0x51DE0001u + i, 1000u);
    CHECK(slot != BOX_SLOT_NONE);
    PebbleInstance* p = box_slot(slot);
    if (!p) continue;
    for (uint8_t c = 0; c < PB_CARE_COUNT; ++c) p->care[c] = PB_CARE_MILLI_MAX;
  }
  if (occupied) CHECK(box_set_active(0));
}

// A CRC over the whole Box, so "the Box is untouched" is a byte statement and
// not an inspection of the fields somebody thought to look at.
static uint32_t box_fingerprint(void) {
  const uint8_t* p = (const uint8_t*)&g_gs;
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < sizeof g_gs; ++i) { h ^= p[i]; h *= 16777619u; }
  return h;
}

// =============================================================================
//  THE PEER. A real Session with its own everything, on loopback endpoint 1.
// =============================================================================
static LoopbackLink  g_lk;
static LoopbackPort  g_port0, g_port1;
static Transport     g_tp0, g_tp1;

static Session       g_peer;
static BattleSetup   g_peer_setup;
static BattleState   g_peer_st;
static BattleAi      g_peer_ai;
static BattleEvent   g_peer_ring[64];
static BattleLog     g_peer_log;
static uint32_t      g_peer_id = 0x22220000u;
static bool          g_peer_live = false;
static bool          g_peer_ai_ready = false;
// (nothing here: the peer is tampered with through the LINK, not through its
// own state - see a_win_the_peer_never_confirmed_pays_nothing.)
static int           g_peer_moves = 0;

// A synthetic member at a real roster species with ITS OWN learnset, which is
// the only moveset battle_init() accepts without an argument.
static void make_member(PebbleInstance& p, uint8_t species_id, uint8_t level,
                        uint32_t id) {
  memset(&p, 0, sizeof p);
  const SpeciesDef* sp = species_get(species_id);
  CHECK(sp != nullptr);
  if (!sp) return;
  p.magic            = PEBBLE_MAGIC;
  p.layout_ver       = PEBBLE_LAYOUT_VER;
  p.species_id       = species_id;
  p.id               = id;
  p.level            = level;
  // A LINEAGE IS REQUIRED, not decorative: game/genome.cpp's genome_valid()
  // refuses lineage_id 0, and the peer's team goes through the shared validator
  // on BOTH devices - which is the whole point of the wire path.
  p.genome.lineage_id = 0xBEEF0000u + id;
  p.genome.generation = 2;
  gene_set_temperament(p.genome, 8);
  gene_set_hardiness(p.genome, 8);
  gene_set_metabolism(p.genome, 8);
  genome_seal(p.genome);
  memcpy(p.moves, sp->moves, sizeof p.moves);
  // The stage bits are a PURE FUNCTION of the species row and game/validate.cpp
  // checks them against it rather than repairing them from it (VR_BAD_EVO_STAGE).
  p.evo_state = (uint8_t)(sp->stage & (uint8_t)EVO_STATE_STAGE_MASK);
  PebbleStats st;
  pebble_derive_stats(*sp, p.level, p.genome, st);
  p.hp_cur = st.hp_max;
  for (uint8_t c = 0; c < PB_CARE_COUNT; ++c) p.care[c] = PB_CARE_MILLI_MAX;
}

static void peer_init(void) {
  memset(&g_peer, 0, sizeof g_peer);
  memset(&g_peer_setup, 0, sizeof g_peer_setup);
  memset(&g_peer_st, 0, sizeof g_peer_st);
  battle_log_init(g_peer_log, g_peer_ring, 64u);

  SessionCfg cfg;
  memset(&cfg, 0, sizeof cfg);
  cfg.tp        = &g_tp1;
  cfg.setup     = &g_peer_setup;
  cfg.st        = &g_peer_st;
  cfg.blog      = &g_peer_log;
  cfg.llog      = nullptr;
  cfg.device_id = g_peer_id;
  cfg.nonce     = 0x5EED0002u;
  // THE SAME BAND THE FIRMWARE OFFERS, and it has to be: session.cpp's
  // band_acceptable() requires the REQUESTED band to sit inside the responder's
  // own, so a peer with a wider band refuses a Pebblebol's SESSION_REQUEST by
  // name. A second real device runs this same build and this same constant;
  // 0xFF here would have been a peer no Pebblebol can talk to.
  cfg.lvl_lo    = 1u;
  cfg.lvl_hi    = (uint8_t)XP_LEVEL_MAX;
  session_init(g_peer, cfg);

  PebbleInstance team[BATTLE_TEAM_MAX];
  for (uint8_t i = 0; i < (uint8_t)BATTLE_TEAM_MAX; ++i)
    make_member(team[i], (uint8_t)(2u + i * 3u), (uint8_t)(6u + i * 2u),
                0x7EE00001u + i);
  uint8_t bad = 0xFFu;
  CHECK_EQ((int)session_set_team(g_peer, team, (uint8_t)BATTLE_TEAM_MAX, bad), (int)VR_OK);
  // THE AI'S SIDE IS NOT KNOWN YET. networking/session.cpp fixes the two sides
  // from the two device ids at HELLO, so an AI seeded here with a guess would
  // choose moves for the wrong half of the field and its own engine would
  // refuse them (SE_PROTOCOL / SD_INTERNAL, measured). It is initialised on the
  // first round it is asked for a move, when g_peer.side is real.
  g_peer_ai_ready = false;
  g_peer_moves = 0;
}

// -----------------------------------------------------------------------------
//  THE PEER, AS A TRADER (P7-C4)
//
//  The SAME Session, the SAME codec and the REAL networking/trade_link.cpp -
//  only its journal and its Box are a model, because game/box.cpp is a
//  file-scope singleton and the ONE real Box in this process belongs to the
//  screen under test. That is the same split tests/test_trade.cpp makes and for
//  the same reason.
// -----------------------------------------------------------------------------
static TradeLink g_peer_tl;
static int       g_peer_trade_commits = 0;
static int       g_peer_trade_aborts  = 0;
static uint8_t   g_peer_judge = 0;          // what the peer says about OUR record

static bool    ptr_sent(void* c, uint32_t a, uint32_t b) { (void)c;(void)a;(void)b; return true; }
static uint8_t ptr_judge(void* c, const uint8_t r[48])   { (void)c;(void)r; return g_peer_judge; }
static bool    ptr_recv(void* c, const uint8_t r[48])    { (void)c;(void)r; return true; }
static bool    ptr_commit(void* c) { (void)c; ++g_peer_trade_commits; return true; }
static void    ptr_abort(void* c)  { (void)c; ++g_peer_trade_aborts; }
static const TradeHooks g_peer_hooks = {
  &ptr_sent, &ptr_judge, &ptr_recv, &ptr_commit, &ptr_abort, nullptr
};

// Re-opens the peer as a TRADE session offering one Pebble of its own.
static void peer_init_trade(uint32_t pebble_id) {
  memset(&g_peer, 0, sizeof g_peer);
  memset(&g_peer_tl, 0, sizeof g_peer_tl);
  g_peer_trade_commits = 0;
  g_peer_trade_aborts  = 0;
  g_peer_judge = 0;

  SessionCfg cfg;
  memset(&cfg, 0, sizeof cfg);
  cfg.tp        = &g_tp1;
  cfg.tl        = &g_peer_tl;
  cfg.op        = (uint8_t)SOP_TRADE;
  cfg.device_id = g_peer_id;
  cfg.nonce     = 0x5EED0002u;
  cfg.lvl_lo    = 1u;
  cfg.lvl_hi    = (uint8_t)XP_LEVEL_MAX;
  session_init(g_peer, cfg);

  PebbleInstance mine;
  make_member(mine, 7u, 11u, pebble_id);
  CHECK_EQ((int)session_set_trade(g_peer, mine), (int)VR_OK);
  trade_link_init(g_peer_tl, g_peer, mine.id, g_peer_hooks);
  g_peer_ai_ready = false;
  g_peer_moves = 0;
}

// THE PEER'S CONSENT. Nothing above this line puts a frame on the wire; this
// is the far device's player pressing A.
static void peer_consent(void) {
  g_peer_live = true;
  session_start(g_peer, g_now);
}

// Set false to model a peer whose player never presses the SECOND A - the one
// that agrees to these two Pebbles rather than to this peer.
static bool g_peer_accepts_trade = true;

static void peer_step(void) {
  if (!g_peer_live) return;
  session_poll(g_peer, g_now);
  if (g_peer_accepts_trade && trade_link_wants_consent(g_peer)) {
    trade_link_accept(g_peer);
  }
  if (link_wants_action(g_peer)) {
    if (!g_peer_ai_ready) {
      battle_ai_init(g_peer_ai, g_peer.side, 0xA11CE001u);
      g_peer_ai_ready = true;
    }
    const BattleAction a = battle_ai_choose(g_peer_ai, g_peer_st);
    if (a.kind != (uint8_t)BACT_NONE) { link_local_action(g_peer, a); ++g_peer_moves; }
  }
}

// =============================================================================
//  THE HARNESS
// =============================================================================
static void harness_reset(uint32_t my_id, uint32_t peer_id) {
  // A CLEAN DEVICE, not a clean test. Both screens are file-scope singletons, so
  // the previous case's leave hooks have to run or its session, its radio and
  // its "here is what happened" page survive into this one - which is exactly
  // the state a player reaches by walking away and coming back, and exactly the
  // state that made the LINK screen open on a summary instead of a browse.
  screen_leave((uint8_t)SCR_BATTLE);
  screen_leave((uint8_t)SCR_LINK);
  g_now = 100000u;
  g_idle = 0;
  g_toast = STR_EMPTY;
  g_help  = STR_EMPTY;
  g_backs = g_wiggles = g_results = 0;
  g_res_entry = 0xFF;
  g_res_won   = 0xFF;
  g_commits = 0;
  g_pushes  = 0;
  g_last_push = 0xFF;
  g_screen  = (uint8_t)SCR_LINK;
  g_sp = 0;
  memset(g_stack, 0, sizeof g_stack);
  memset(&g_cds, 0, sizeof g_cds);
  cd_begin();
  act_begin();
  g_dev_id  = my_id;
  g_peer_id = peer_id;

  LoopbackFault f;
  memset(&f, 0, sizeof f);
  loopback_init(g_lk, 0xC0FFEEu, f);
  g_tp0 = transport_loopback(g_port0, g_lk, 0u);
  g_tp1 = transport_loopback(g_port1, g_lk, 1u);

  lf_reset();
  lf_bind_transport(&g_tp0);
  lf_set_pet_name("BOLOTA");
  g_peer_live = false;
  g_peer_accepts_trade = true;
  box_fixture(3u);
  peer_init();
  nav_goto((uint8_t)SCR_LINK);
}

// Three beacons is what crosses LINK_PEER_HITS_MIN (networking/discovery.h), so
// this is the shortest honest way to put a peer on the list.
static void see_peer(uint16_t caps, int8_t rssi, uint8_t slot) {
  for (uint8_t i = 0; i < (uint8_t)LINK_PEER_HITS_MIN; ++i) {
    lf_push_beacon(g_peer_id, "ROCOSO", caps, rssi, slot);
    g_now += (uint32_t)LINK_BEACON_MS + 1u;
    link_update(g_now);
  }
}

// The local player: open the card on peer 0 and press A on the row `op`.
static void consent_to(uint8_t op) {
  link_input(GST_TAP_R);                            // peer -> card
  CHECK_EQ(link_screen_mode(), (uint8_t)LKM_CARD);
  while (link_screen_op() != op) link_input(GST_TAP_L);
  link_input(GST_TAP_R);
}

// One frame of the whole device: the peer's loop, then the current screen's.
static void tick(uint32_t dt) {
  g_now += dt;
  peer_step();
  if (g_screen == (uint8_t)SCR_LINK)        link_update(g_now);
  else if (g_screen == (uint8_t)SCR_BATTLE) battle_update(g_now);
}

// The local player at the battle screen: skip the stare-down, take the first
// legal row every round, skip the transcript. Every one of these is a gesture a
// player could make; there is no back door into the screen.
static void play(uint32_t dt, int max_ticks) {
  for (int i = 0; i < max_ticks; ++i) {
    tick(dt);
    if (g_screen != (uint8_t)SCR_BATTLE) continue;
    switch (battle_screen_mode()) {
      case BTM_INTRO:   battle_input(GST_TAP_R); break;
      case BTM_MENU:
      case BTM_SWITCH:  battle_input(GST_TAP_R); break;
      case BTM_RESOLVE: battle_input(GST_TAP_L);  break;
      case BTM_RESULT:  return;
      default: break;
    }
  }
}

// =============================================================================
//  1. THE CONSENT GATE
// =============================================================================

// A PEER SHOUTING INTO A SCREEN THAT HAS NOT CONSENTED CHANGES NOTHING. The
// far device's player has pressed A and this one's has not: its HELLO is on the
// wire, it climbs its whole ladder, and this screen neither answers it nor even
// looks. There is no Session here to answer with - session_init() has not been
// called - and on the device the radio would not have delivered the frame at
// all, because nothing is bound.
// =============================================================================
//  P10-C6: THE NAME ON THE AIR
//
//  The beacon field is LATIN-1 by contract and P10-C4 made ui_pet_name() emit
//  UTF-8. fill_self() fed one to the other, so every Latin-1 accent (0xC0..0xDF
//  - exactly the uppercase set screen_setup.cpp's naming ring can type) became
//  0xC3 plus a byte in 0x80..0x9F, which name_ok() refuses. disc_encode()
//  answered DE_NAME, link_service() dropped the frame, and a device with an
//  accented name emitted NO BEACON AT ALL: invisible to every peer, and so the
//  link could not be offered from either side. Nothing reported it -
//  beacons_tx stayed 0 by design when a beacon cannot be encoded.
//
//  Nothing here could see it, because tests/fakes/link_fake.cpp stood in for
//  ui_pet_name() with the pre-P10-C4 body. The fake now matches the shipping
//  file, so this case drives the REAL fill_self() into the REAL disc_encode().
// =============================================================================
TEST(a_beacon_reaches_the_air_whatever_the_owner_named_the_device) {
  // Latin-1, the way Config.pet_name and screen_setup.cpp's ring store it.
  const char* kNames[6] = {
    "PACO",              // ASCII, the control
    "\xD1U",             // N-tilde
    "\xC1LEX",           // A-acute
    "SOF\xCD" "A",       // I-acute
    "JOS\xC9",           // E-acute
    "\xC1\xC9\xCD\xD3\xDA\xDC\xD1\xC1\xC9\xCD\xD3\xDA"   // twelve, all accented
  };
  for (uint8_t k = 0; k < 6u; ++k) {
    harness_reset(0x11111111u, 0x22222222u);
    lf_set_pet_name(kNames[k]);
    const int before = lf_beacons_tx();
    for (uint8_t i = 0; i < 40u; ++i) tick(100u);
    // ANTI-VACUITY FIRST: the control must actually put beacons up, or every
    // comparison below is a statement about a loop that did not run.
    const int sent = lf_beacons_tx() - before;
    if (k == 0u) CHECK(sent > 0);
    CHECK(sent > 0);
  }
}

// The stored form is what goes on the wire and the drawn form is what goes on
// the panel. Asserting only "a beacon went out" would pass with the two halves
// swapped everywhere, so this names WHICH bytes each one produces.
TEST(the_name_seam_answers_latin1_for_the_wire_and_utf8_for_the_panel) {
  harness_reset(0x11111111u, 0x22222222u);
  lf_set_pet_name("\xD1U");
  char wire[16];
  char drawn[16];
  ui_pet_name_latin1(wire, sizeof wire);
  ui_pet_name(drawn, sizeof drawn);
  CHECK_EQ((int)(uint8_t)wire[0], 0xD1);         // one stored byte
  CHECK_EQ((int)(uint8_t)wire[1], (int)'U');
  CHECK_EQ((int)strlen(wire), 2);
  CHECK_EQ((int)(uint8_t)drawn[0], 0xC3);        // two drawn bytes
  CHECK_EQ((int)(uint8_t)drawn[1], 0x91);
  CHECK_EQ((int)strlen(drawn), 3);
  // And the wire form is one disc_encode() accepts, which is the property the
  // whole thing exists for.
  DiscBeacon b;
  memset(&b, 0, sizeof b);
  b.device_id = 0x11111111u;
  b.caps      = (uint16_t)DISC_CAP_BATTLE;
  b.ver       = (uint8_t)PROTOCOL_VERSION;
  ui_pet_name_latin1(b.name, sizeof b.name);
  uint8_t buf[64];
  uint16_t n = 0u;
  CHECK_EQ((int)disc_encode(b, buf, (uint16_t)sizeof buf, n), (int)DE_OK);
  CHECK(n > 0u);
}

TEST(a_peer_in_the_room_does_not_start_a_session_on_its_own) {
  harness_reset(0x11110000u, 0x22220000u);
  see_peer((uint16_t)DISC_CAP_BATTLE, -40, 0u);
  CHECK_EQ(link_screen_peers(), (uint8_t)1);

  peer_consent();
  for (int i = 0; i < 400; ++i) tick(100u);

  CHECK_EQ(link_screen_consents(), (uint8_t)0);
  CHECK_EQ(link_screen_session_state(), (uint8_t)SS_IDLE);
  CHECK_EQ(lf_binds(), 0);
  CHECK_EQ(g_pushes, 0);
  CHECK_EQ(g_results, 0);
  // The peer gave up on its own ladder, by name, having been answered by
  // nobody. That is the far device's "CONEXIÓN PERDIDA".
  CHECK(session_closed(g_peer));
  CHECK_EQ((int)session_end(g_peer).reason, (int)SE_LOST);
  // And this screen is still just browsing.
  CHECK_EQ(link_screen_mode(), (uint8_t)LKM_BROWSE);
}

// THE A PRESS IS WHAT BINDS THE RADIO, and it is the only thing that does.
TEST(the_a_press_is_the_only_thing_that_opens_a_session_or_binds_a_peer) {
  harness_reset(0x11110000u, 0x22220000u);
  see_peer((uint16_t)DISC_CAP_BATTLE, -40, 5u);
  CHECK_EQ(lf_binds(), 0);

  link_input(GST_TAP_R);                     // opening the CARD binds nothing
  CHECK_EQ(link_screen_mode(), (uint8_t)LKM_CARD);
  CHECK_EQ(lf_binds(), 0);
  CHECK_EQ(link_screen_session_state(), (uint8_t)SS_IDLE);

  link_input(GST_TAP_L);                      // walking the rows binds nothing
  link_input(GST_TAP_L);
  link_input(GST_TAP_L);
  CHECK_EQ(lf_binds(), 0);

  while (link_screen_op() != (uint8_t)LOP_BATTLE) link_input(GST_TAP_L);
  link_input(GST_TAP_R);
  CHECK_EQ(link_screen_consents(), (uint8_t)1);
  CHECK_EQ(lf_binds(), 1);
  CHECK_EQ(lf_bound_slot(), (uint8_t)5);      // the peer's own opaque handle
  CHECK_EQ(link_screen_mode(), (uint8_t)LKM_WAIT);
  CHECK_EQ(link_screen_session_state(), (uint8_t)SS_HELLO);
}

// A SESSION THAT WAS NEVER ACCEPTED MUST NOT BATTLE. One device pressed A and
// the other never did; nine rungs later this is spec section 47's recoverable
// failure and not a fight.
TEST(a_session_that_was_never_accepted_never_reaches_a_battle) {
  harness_reset(0x11110000u, 0x22220000u);
  see_peer((uint16_t)DISC_CAP_BATTLE, -40, 0u);
  consent_to((uint8_t)LOP_BATTLE);

  // The peer is in the room and its player never touched a button.
  for (int i = 0; i < 200; ++i) tick(100u);

  CHECK_EQ(link_screen_mode(), (uint8_t)LKM_LOST);
  CHECK_EQ(link_screen_end_reason(), (uint8_t)SE_LOST);
  CHECK_EQ(g_pushes, 0);                       // no SCR_BATTLE, ever
  CHECK_EQ(g_results, 0);                      // and nothing to report
  CHECK_EQ(lf_stops(), 1);                     // the radio went back exactly once
  CHECK_EQ(lf_unbinds(), 1);
  CHECK(!link_screen_busy());
}

// AND THE LADDER IS THE CEILING, MEASURED RATHER THAN ASSUMED. Spec section 47
// asks that every wait end; this is the number it ends at.
TEST(the_wait_for_a_peer_ends_at_the_ladder_and_not_a_second_later) {
  harness_reset(0x11110000u, 0x22220000u);
  see_peer((uint16_t)DISC_CAP_BATTLE, -40, 0u);
  const uint32_t t0 = g_now;
  consent_to((uint8_t)LOP_BATTLE);

  uint32_t died = 0;
  for (int i = 0; i < 400 && died == 0u; ++i) {
    tick(100u);
    if (link_screen_mode() == (uint8_t)LKM_LOST) died = (uint32_t)(g_now - t0);
  }
  CHECK(died != 0u);
  const uint32_t ladder = (uint32_t)PROTO_RETX_MS * (uint32_t)PROTO_RETX_MAX;
  printf("  a wait with no answer ends after %u ms (ladder %u ms)\n",
         (unsigned)died, (unsigned)ladder);
  CHECK(died >= ladder);
  CHECK(died <= ladder + 1500u);
}

// SPEC SECTION 47'S TWO ANSWERS, and they are the two the screen prints.
TEST(a_lost_connection_offers_retry_and_exit_and_both_do_what_they_say) {
  harness_reset(0x11110000u, 0x22220000u);
  see_peer((uint16_t)DISC_CAP_BATTLE, -40, 0u);
  consent_to((uint8_t)LOP_BATTLE);
  for (int i = 0; i < 200; ++i) tick(100u);
  CHECK_EQ(link_screen_mode(), (uint8_t)LKM_LOST);

  // A retries: a fresh browse, and the radio is taken again.
  const int stops_before = lf_stops();
  link_input(GST_TAP_R);
  CHECK_EQ(link_screen_mode(), (uint8_t)LKM_BROWSE);
  CHECK_EQ(lf_starts(), 2);
  CHECK_EQ(lf_stops(), stops_before);          // it was already released
  CHECK(link_screen_busy());

  // B leaves, and the radio goes with it.
  link_input(GST_HOLD_R);
  CHECK_EQ(g_backs, 1);
  CHECK_EQ(g_screen, (uint8_t)SCR_HOME);
  CHECK(!link_screen_busy());
  CHECK_EQ(lf_stops(), 2);
}

// =============================================================================
//  2. THE CARD (spec section 42)
// =============================================================================

// An operation the PEER never claimed is refused BY NAME rather than opened and
// then failed nine seconds later on the wire.
TEST(an_operation_the_peer_cannot_do_is_refused_by_name_and_opens_nothing) {
  harness_reset(0x11110000u, 0x22220000u);
  see_peer((uint16_t)DISC_CAP_BATTLE, -40, 0u);   // battle only
  link_input(GST_TAP_R);
  while (link_screen_op() != (uint8_t)LOP_TRADE) link_input(GST_TAP_L);
  g_toast = STR_EMPTY;
  link_input(GST_TAP_R);
  CHECK_EQ(g_toast, STR_LK_NO_CAP);
  CHECK_EQ(lf_binds(), 0);
  CHECK_EQ(link_screen_mode(), (uint8_t)LKM_CARD);

  // And CANCELAR is a row, not an absence: it walks back to the list.
  while (link_screen_op() != (uint8_t)LOP_CANCEL) link_input(GST_TAP_L);
  link_input(GST_TAP_R);
  CHECK_EQ(link_screen_mode(), (uint8_t)LKM_BROWSE);
  CHECK_EQ(g_backs, 0);
}

// A CAPABILITY THIS BUILD HAS NO PROTOCOL FOR STILL GETS THE HONEST ANSWER, and
// after P7-C4 that is CRIAR and no longer INTERCAMBIO. game/breeding.cpp is
// complete and tested; no wire driver carries a breeding, and the row says so
// rather than opening a session that could only ever end in a timeout.
// P10-C6: DRIVEN AGAINST A PEER A REAL BOARD CAN BE, WHICH IS THE WHOLE POINT.
// This case used to fabricate a peer advertising DISC_CAP_BREED - a beacon no
// artefact can produce, since LK_SELF_CAPS is BATTLE|TRADE - so it reached a
// branch the release build could never take, while the sentence every actual
// player got ("El otro no puede eso", this device blaming the other player's
// device for a feature neither has) was asserted nowhere. The refusal is
// unconditional now: the reason breeding does not run is local and symmetric.
TEST(a_capability_this_build_has_no_protocol_for_says_so_and_opens_nothing) {
  // Both peers, the one a board really is and the impossible one, must answer
  // the same way - otherwise the message is about the peer again.
  const uint16_t kCaps[2] = {
    (uint16_t)(DISC_CAP_BATTLE | DISC_CAP_TRADE),                    // a real board
    (uint16_t)(DISC_CAP_BATTLE | DISC_CAP_TRADE | DISC_CAP_BREED)    // a future one
  };
  for (uint8_t c = 0; c < 2u; ++c) {
    harness_reset(0x11110000u, 0x22220000u);
    see_peer(kCaps[c], -40, 0u);
    link_input(GST_TAP_R);
    while (link_screen_op() != (uint8_t)LOP_BREED) link_input(GST_TAP_L);
    g_toast = STR_EMPTY;
    link_input(GST_TAP_R);
    CHECK_EQ(g_toast, STR_UI_SOON);
    CHECK(g_toast != STR_LK_NO_CAP);
    CHECK_EQ(lf_binds(), 0);
    CHECK_EQ(link_screen_session_state(), (uint8_t)SS_IDLE);
    CHECK_EQ(link_screen_mode(), (uint8_t)LKM_CARD);
  }
  // ANTI-VACUITY: STR_LK_NO_CAP is still the answer where it is TRUE - a peer
  // that really cannot trade. Without this the case above would pass with the
  // capability check deleted from the whole screen.
  harness_reset(0x11110000u, 0x22220000u);
  see_peer((uint16_t)DISC_CAP_BATTLE, -40, 0u);
  link_input(GST_TAP_R);
  while (link_screen_op() != (uint8_t)LOP_TRADE) link_input(GST_TAP_L);
  g_toast = STR_EMPTY;
  link_input(GST_TAP_R);
  CHECK_EQ(g_toast, STR_LK_NO_CAP);
}

// =============================================================================
//  THE TRADE, THROUGH THE SCREEN (P7-C4)
//
//  The whole five-step exchange, driven by two gestures a player could make and
//  no back door. The journal here is a MODEL (tests/fakes/link_fake.h says
//  exactly what is fake and what is not); the atomicity of the five flash
//  writes is tests/test_trade.cpp's subject. What THIS binary proves is that
//  the SCREEN offers the right Pebble, refuses the ones the rules refuse, asks
//  the player before anything moves, and that the real Box really swaps.
// =============================================================================
static uint32_t offered_id(void) {
  const uint8_t sl = link_trade_slot();
  const PebbleInstance* p = (sl < (uint8_t)BOX_SLOTS) ? box_peek(sl) : nullptr;
  return p ? p->id : 0u;
}

static uint8_t traded_in_box(void) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < (uint8_t)BOX_SLOTS; ++i) {
    const PebbleInstance* p = box_peek(i);
    if (p && (p->flags & (uint8_t)PBF_TRADED) != 0u) n++;
  }
  return n;
}

TEST(two_players_who_both_press_a_swap_one_pebble_each_and_the_box_says_so) {
  harness_reset(0x11110000u, 0x22220000u);
  peer_init_trade(0x7EE0BEEFu);
  see_peer((uint16_t)(DISC_CAP_BATTLE | DISC_CAP_TRADE), -40, 0u);

  consent_to((uint8_t)LOP_TRADE);
  CHECK_EQ(link_screen_mode(), (uint8_t)LKM_WAIT);
  CHECK_EQ(lf_binds(), 1);
  CHECK_EQ(link_screen_op(), (uint8_t)LOP_TRADE);
  // NOT THE ACTIVE ONE. game/trade.h's TDR_ACTIVE is why, and box_fixture()
  // makes slot 0 active.
  CHECK(link_trade_slot() != box_active());
  const uint32_t out_id = offered_id();
  CHECK(out_id != 0u);

  peer_consent();
  for (int i = 0; i < 400 && !link_trade_wants_consent(); ++i) tick(50u);
  CHECK(link_trade_wants_consent());
  CHECK_EQ((int)link_trade_phase(), (int)TLP_ASK_PLAYER);

  // BOTH OFFERS ARE ON THE TABLE AND BOTH WERE VALIDATED ON BOTH SIDES, AND
  // NOTHING HAS MOVED. This is the state the second A exists for.
  CHECK_EQ((int)box_count(), 3);
  CHECK(box_id_in_use(out_id));
  CHECK_EQ((int)traded_in_box(), 0);
  CHECK_EQ(lf_trade_commits(), 0);
  CHECK_EQ(g_peer_trade_commits, 0);
  CHECK_EQ((int)lf_trade_phase(), (int)TRADE_RECEIVED);   // W1 and W2 are down
  CHECK_EQ((long long)lf_trade_out_id(), (long long)out_id);
  // The frame the player is looking at draws inside the panel.
  fb_reset(); link_render(); CHECK_EQ(fb_oob(), 0u);

  link_input(GST_TAP_R);                       // THE SECOND CONSENT
  for (int i = 0; i < 400 && link_screen_mode() == (uint8_t)LKM_WAIT; ++i) tick(50u);

  CHECK_EQ(lf_trade_commits(), 1);
  CHECK_EQ(g_peer_trade_commits, 1);
  CHECK_EQ((int)link_screen_end_reason(), (int)SE_DONE);
  CHECK_EQ(link_screen_mode(), (uint8_t)LKM_ENDED);
  // THE BOX SWAPPED, and it is the REAL game/box.cpp.
  CHECK_EQ((int)box_count(), 3);
  CHECK(!box_id_in_use(out_id));
  CHECK_EQ((int)traded_in_box(), 1);
  CHECK_EQ((int)lf_trade_phase(), (int)TRADE_IDLE);       // W4 cleared it
  // AND THE RADIO IS BACK. A trade that ends leaves nothing bound.
  CHECK_EQ(lf_unbinds(), 1);
  CHECK(!lf_bound());
  fb_reset(); link_render(); CHECK_EQ(fb_oob(), 0u);
}

TEST(a_trade_the_local_player_never_accepts_moves_nothing_and_clears_its_journal) {
  harness_reset(0x11110000u, 0x22220000u);
  peer_init_trade(0x7EE0CAFEu);
  see_peer((uint16_t)(DISC_CAP_BATTLE | DISC_CAP_TRADE), -40, 0u);
  consent_to((uint8_t)LOP_TRADE);
  const uint32_t out_id = offered_id();
  const uint32_t before = box_fingerprint();

  peer_consent();
  for (int i = 0; i < 400 && !link_trade_wants_consent(); ++i) tick(50u);
  CHECK(link_trade_wants_consent());

  // The player looks at it and never presses A. Both ladders run out.
  for (int i = 0; i < 800 && link_screen_mode() == (uint8_t)LKM_WAIT; ++i) tick(200u);
  CHECK_EQ(link_screen_mode(), (uint8_t)LKM_LOST);
  CHECK_EQ((int)link_screen_end_reason(), (int)SE_LOST);
  CHECK_EQ(lf_trade_commits(), 0);
  CHECK_EQ(g_peer_trade_commits, 0);
  CHECK(box_id_in_use(out_id));
  CHECK_EQ((long long)box_fingerprint(), (long long)before);      // byte-identical
  // THE JOURNAL IS CLEARED HERE AND NOT LEFT FOR THE NEXT BOOT. The boot
  // resolver stays the backstop for the case this cannot reach - the power cut.
  CHECK_EQ((int)lf_trade_phase(), (int)TRADE_IDLE);
  CHECK(lf_trade_aborts() > 0);
  CHECK_EQ(lf_unbinds(), 1);
}

TEST(the_pebble_the_player_is_holding_is_never_the_one_put_on_the_wire) {
  harness_reset(0x11110000u, 0x22220000u);
  peer_init_trade(0x7EE0D00Du);
  see_peer((uint16_t)(DISC_CAP_BATTLE | DISC_CAP_TRADE), -40, 0u);

  // The BOX armed the ACTIVE slot. game/trade.cpp refuses it, and the refusal
  // is a sentence the player can act on rather than a silent substitution of a
  // different Pebble - which is the failure mode the comment at pick_trade_slot()
  // names.
  link_leave();
  link_arm_intent((uint8_t)LOP_TRADE, box_active());
  link_enter();
  see_peer((uint16_t)(DISC_CAP_BATTLE | DISC_CAP_TRADE), -40, 0u);
  g_toast = STR_EMPTY;
  consent_to((uint8_t)LOP_TRADE);
  CHECK_EQ(g_toast, STR_LK_TR_ACTIVE);
  CHECK_EQ(lf_binds(), 0);
  CHECK_EQ(link_screen_mode(), (uint8_t)LKM_CARD);
  CHECK_EQ((int)link_trade_slot(), (int)BOX_SLOT_NONE);

  // A QUARANTINED SLOT IS REFUSED TOO, and by its own name. save_manager.h:
  // "A quarantined Pebble may be shown to its owner. It may NOT enter a battle
  // or a trade" - P7's obligation, discharged where the offer is made.
  link_leave();
  link_arm_intent((uint8_t)LOP_TRADE, 1u);
  link_enter();
  see_peer((uint16_t)(DISC_CAP_BATTLE | DISC_CAP_TRADE), -40, 0u);
  lf_set_quarantine((uint16_t)(1u << 1));
  g_toast = STR_EMPTY;
  consent_to((uint8_t)LOP_TRADE);
  CHECK_EQ(g_toast, STR_LK_TR_QUARANTINED);
  CHECK_EQ(lf_binds(), 0);
}

TEST(a_peer_that_refuses_our_pebble_is_answered_by_name_and_nothing_moves) {
  harness_reset(0x11110000u, 0x22220000u);
  peer_init_trade(0x7EE0FEEDu);
  g_peer_judge = (uint8_t)TDR_TAINT;        // the far device's policy says no
  see_peer((uint16_t)(DISC_CAP_BATTLE | DISC_CAP_TRADE), -40, 0u);
  consent_to((uint8_t)LOP_TRADE);
  const uint32_t before = box_fingerprint();
  peer_consent();
  for (int i = 0; i < 800 && link_screen_mode() == (uint8_t)LKM_WAIT; ++i) tick(50u);

  CHECK_EQ(link_screen_mode(), (uint8_t)LKM_LOST);
  CHECK_EQ((int)link_screen_end_reason(), (int)SE_REJECTED);
  CHECK_EQ(lf_trade_commits(), 0);
  CHECK_EQ((long long)box_fingerprint(), (long long)before);
  CHECK_EQ((int)lf_trade_phase(), (int)TRADE_IDLE);
  CHECK_EQ(lf_unbinds(), 1);
}

// THE BOX'S ENTRY POINT: an operation and a Pebble, pre-selected, consenting to
// nothing. The armed slot leads the team the session then offers.
TEST(the_box_pre_selects_an_operation_and_a_pebble_and_consents_to_nothing) {
  harness_reset(0x11110000u, 0x22220000u);
  link_leave();
  link_arm_intent((uint8_t)LOP_BREED, 2u);
  link_enter();
  CHECK_EQ(link_screen_op(), (uint8_t)LOP_BREED);
  CHECK_EQ(link_screen_mode(), (uint8_t)LKM_BROWSE);
  CHECK_EQ(lf_binds(), 0);
  CHECK_EQ(link_screen_session_state(), (uint8_t)SS_IDLE);

  // The arming is consumed: a second entry starts on COMBATE again.
  link_leave();
  link_enter();
  CHECK_EQ(link_screen_op(), (uint8_t)LOP_BATTLE);
}

// =============================================================================
//  3. THE PEER TABLE AND THE ACTIVITY SCORE (the P6 carried debt, discharged)
// =============================================================================

// act_note_peer()'s FIRST DRIVER IN THE FIRMWARE. The rule it must obey - once
// per peer that crosses the hit threshold, never once per beacon - lives in
// networking/discovery.cpp and is tested there; what this case adds is that
// something now REACHES it, and that the day it dirtied is persisted.
TEST(a_peer_met_over_the_link_is_scored_once_and_the_day_is_saved) {
  harness_reset(0x11110000u, 0x22220000u);
  CHECK_EQ(act_peers_seen(), (uint8_t)0);

  see_peer((uint16_t)DISC_CAP_BATTLE, -40, 0u);
  CHECK_EQ(act_peers_seen(), (uint8_t)1);
  CHECK(g_commits >= 1);

  // Thirty more beacons from the SAME device are the same Pebblebol.
  const int commits = g_commits;
  for (int i = 0; i < 30; ++i) {
    lf_push_beacon(g_peer_id, "ROCOSO", (uint16_t)DISC_CAP_BATTLE, -40, 0u);
    g_now += (uint32_t)LINK_BEACON_MS + 1u;
    link_update(g_now);
  }
  CHECK_EQ(act_peers_seen(), (uint8_t)1);
  CHECK_EQ(g_commits, commits);
}

// A device below the signal floor is not in the room, and one heard once is not
// a Pebblebol - so neither reaches the list the player may pick from.
TEST(only_a_peer_heard_often_enough_and_loudly_enough_can_be_picked) {
  harness_reset(0x11110000u, 0x22220000u);

  // Two devices that are NOT Pebblebols in the room: one too far away for even
  // its first packet to count, one heard exactly once. Both are in the table
  // (their hits have to be able to accumulate) and NEITHER may be offered.
  lf_push_beacon(0x33330000u, "LEJOS", (uint16_t)DISC_CAP_BATTLE, -95, 1u);
  lf_push_beacon(0x44440000u, "UNAVEZ", (uint16_t)DISC_CAP_BATTLE, -40, 2u);
  g_now += (uint32_t)LINK_BEACON_MS + 1u;
  link_update(g_now);
  CHECK_EQ(link_screen_peers(), (uint8_t)0);

  // The real one arrives AFTER them, so it is not index 0 of the table - which
  // is the whole point of this case. The list walks QUALIFIED peers, so row 0
  // must be the real one, and consenting to row 0 must bind ITS slot.
  see_peer((uint16_t)DISC_CAP_BATTLE, -40, 6u);
  CHECK_EQ(link_screen_peers(), (uint8_t)1);
  CHECK_EQ(strcmp(link_screen_peer_name(), ""), 0);   // nothing agreed yet

  // THE RING WALKS THE OFFERED PEERS AND THE WAY OUT, AND NOTHING ELSE. One
  // qualified peer plus "Volver" is two rows, and A wraps between exactly those
  // two - the two unqualified devices are in the table and are not on the list.
  CHECK_EQ(link_screen_cursor(), (uint8_t)0);
  link_input(GST_TAP_L);
  CHECK_EQ(link_screen_cursor(), (uint8_t)1);         // the "Volver" row
  link_input(GST_TAP_L);
  CHECK_EQ(link_screen_cursor(), (uint8_t)0);         // and back to the peer

  consent_to((uint8_t)LOP_BATTLE);
  CHECK_EQ(link_screen_consents(), (uint8_t)1);
  CHECK_EQ(lf_bound_slot(), (uint8_t)6);             // the qualified peer's own
  CHECK_EQ(strcmp(link_screen_peer_name(), "ROCOSO"), 0);
}

// =============================================================================
//  4. THE BATTLE OVER THE LINK (P7-C3)
// =============================================================================

// Run to the point where both devices have consented and the battle exists.
static void both_consent(void) {
  see_peer((uint16_t)DISC_CAP_BATTLE, -40, 0u);
  peer_consent();
  consent_to((uint8_t)LOP_BATTLE);
  for (int i = 0; i < 200 && g_screen != (uint8_t)SCR_BATTLE; ++i) tick(50u);
}

// TWO DEVICES THAT BOTH PRESS A FIGHT ONE BATTLE, AND IT IS REPORTED ONCE.
TEST(two_devices_that_both_consent_fight_one_battle_reported_exactly_once) {
  harness_reset(0x11110000u, 0x22220000u);
  both_consent();
  CHECK_EQ(g_screen, (uint8_t)SCR_BATTLE);
  CHECK_EQ(g_last_push, (uint8_t)SCR_BATTLE);
  CHECK_EQ(link_screen_mode(), (uint8_t)LKM_HANDOFF);
  // The BattleState was built by the session, not by this screen: both devices
  // are in the same round of the same battle before a single frame is drawn.
  CHECK_EQ((int)g_peer_st.phase, (int)BP_RUNNING);

  // Watch the transcript's own label as the fight runs. open_round_log() is
  // what sets it, and a linked battle whose log is never re-opened would replay
  // round 1 for ever - the ring only overflows in a long fight, so the LABEL is
  // what catches it in a short one.
  uint16_t widest_round = 0;
  for (int i = 0; i < 4000 && battle_screen_mode() != BTM_RESULT; ++i) {
    tick(50u);
    if (g_screen != (uint8_t)SCR_BATTLE) break;
    const uint8_t m = battle_screen_mode();
    if (m == BTM_RESOLVE && battle_screen_round() > widest_round)
      widest_round = battle_screen_round();
    if (m == BTM_INTRO || m == BTM_MENU || m == BTM_SWITCH) battle_input(GST_TAP_R);
    else if (m == BTM_RESOLVE) battle_input(GST_TAP_L);
    // The screen's own side never drifts from the session's, on any frame.
    CHECK_EQ(battle_screen_side(), ui_link_battle_side());
  }
  CHECK_EQ(battle_screen_mode(), (uint8_t)BTM_RESULT);
  CHECK(widest_round > 1u);
  printf("  the transcript was labelled up to round %u\n", (unsigned)widest_round);
  CHECK_EQ(g_results, 1);
  CHECK_EQ(g_res_entry, (uint8_t)BT_ENTRY_LINK);
  CHECK(g_peer_moves > 0);                      // the peer really played
  // ONE ROUND'S TRANSCRIPT AT A TIME. The ring holds 48 events and is re-opened
  // immediately before every local submit; a linked round whose log was never
  // re-opened would accumulate across the whole battle and start dropping.
  CHECK_EQ(battle_screen_dropped(), (uint16_t)0);

  // THE VERDICT AGREED, so the reward gate opened - and it opened for exactly
  // one of the two sides.
  CHECK_EQ((int)ui_link_battle_status() == (int)UI_LKB_RUNNING, false);
  const uint8_t st = ui_link_battle_status();
  CHECK(st == (uint8_t)UI_LKB_WON || st == (uint8_t)UI_LKB_LOST ||
        st == (uint8_t)UI_LKB_DRAW);
  CHECK_EQ(g_res_won, (uint8_t)(st == (uint8_t)UI_LKB_WON ? 1u : 0u));
  printf("  a linked battle finished %s after %d peer moves\n",
         st == (uint8_t)UI_LKB_WON ? "won" : (st == (uint8_t)UI_LKB_LOST ? "lost" : "drawn"),
         g_peer_moves);

  // Leaving the result page hands the link back and the radio with it.
  battle_input(GST_TAP_R);
  CHECK_EQ(g_screen, (uint8_t)SCR_LINK);
  CHECK_EQ(link_screen_mode(), (uint8_t)LKM_ENDED);
  CHECK_EQ(g_results, 1);                       // still once
  CHECK(!link_screen_busy());
  CHECK_EQ(lf_stops(), 1);
  CHECK_EQ(lf_unbinds(), 1);
}

// THE DEVICE WITH THE HIGHER ID PLAYS SIDE ONE AND STILL SEES ITS OWN TEAM.
// networking/session.cpp fixes the sides from the two device ids, so on one of
// every pair of boards the player is side 1 - and every "side 0 is us" in
// ui/screen_battle.cpp would have shown the peer's creatures as the player's
// and paid the wrong half of the fight.
TEST(the_higher_device_id_plays_side_one_and_still_sees_its_own_team) {
  harness_reset(0x99990000u, 0x22220000u);     // ours is the HIGHER id
  both_consent();
  CHECK_EQ(g_screen, (uint8_t)SCR_BATTLE);
  CHECK_EQ(ui_link_battle_side(), (uint8_t)1);
  CHECK_EQ((int)g_peer.side, 0);
  // AND THE BATTLE SCREEN IS DRAWING FOR THAT SIDE. This is the assertion the
  // rendered frame cannot make for itself: hard-coding s_me back to 0 puts the
  // PEER's team where the player's belongs, on one board of every pair, and
  // every other check in this file still passed with it done.
  CHECK_EQ(battle_screen_side(), (uint8_t)1);

  // The lead the menu is about is OUR lead: its species is one of the Box's,
  // never one of the peer's synthetic roster.
  const BattleState* st = battle_link_state();
  CHECK(st != nullptr);
  const uint8_t my_species = st->side[1].team[st->side[1].active].species_id;
  bool from_box = false;
  for (uint8_t i = 0; i < (uint8_t)BOX_SLOTS; ++i) {
    const PebbleInstance* p = box_peek(i);
    if (p && p->species_id == my_species) from_box = true;
  }
  CHECK(from_box);

  play(50u, 4000);
  CHECK_EQ(g_results, 1);
  const uint8_t s = ui_link_battle_status();
  CHECK_EQ(g_res_won, (uint8_t)(s == (uint8_t)UI_LKB_WON ? 1u : 0u));
}

// =============================================================================
//  5. THE REWARD GATE
// =============================================================================

// A DESYNC PAYS NOTHING AND LEAVES THE BOX UNTOUCHED. The peer's engine is
// moved out from under it mid-battle, which is exactly what a divergence looks
// like from here - networking/battle_link.cpp says a genuine divergence and a
// lying peer are indistinguishable, and neither may pay.
TEST(a_desync_pays_nothing_and_leaves_both_boxes_untouched) {
  harness_reset(0x11110000u, 0x22220000u);
  both_consent();
  CHECK_EQ(g_screen, (uint8_t)SCR_BATTLE);
  const uint32_t fp = box_fingerprint();

  // One byte of the peer's 212 B state, changed by nobody's rules.
  g_peer_st.side[0].team[0].hp_cur =
      (uint16_t)(g_peer_st.side[0].team[0].hp_cur - 1u);

  play(50u, 4000);

  CHECK_EQ((int)ui_link_battle_status(), (int)UI_LKB_BROKEN);
  CHECK_EQ(g_results, 1);
  CHECK_EQ(g_res_won, (uint8_t)0);              // THE GATE
  CHECK_EQ(box_fingerprint(), fp);              // and the Box is byte-identical
  CHECK(session_closed(g_peer));
  // Named, on at least one of the two ends, as what it is.
  const uint8_t r = link_screen_end_reason();
  CHECK(r == (uint8_t)SE_DESYNC || r == (uint8_t)SE_PROTOCOL ||
        (int)session_end(g_peer).reason == (int)SE_DESYNC);
  printf("  a mid-battle divergence ended as reason %u / peer %u, paid %u\n",
         (unsigned)r, (unsigned)session_end(g_peer).reason, (unsigned)g_res_won);
}

// A WIN CONFIRMED A RUNG LATE IS STILL PAID, and this is the OTHER half of the
// reward gate - the half that is an UNDER-award rather than an over-award.
//
// The engine finishes the fight several beats before the BATTLE_END exchange
// that authorises the reward, and the transcript is still playing while it
// happens. Reporting on the ENGINE'S finish would fire report_once() with the
// session still RUNNING, which is `won == 0`, one shot, spent - and the win the
// peer confirms a second later would never be paid. Blocking exactly ONE of the
// peer's BATTLE_END frames opens that window wide enough to see: its
// retransmission arrives a ladder rung later, long after the playback ended.
TEST(a_win_confirmed_a_rung_late_is_still_paid) {
  g_box_level = 22u;                          // so the local engine WILL win
  harness_reset(0x11110000u, 0x22220000u);
  // ONE frame killed by name, not the type for ever: transport.h's counted
  // block is what lets a case lose a single message and watch the pair recover.
  g_lk.fault.block_type[1] = (uint8_t)PT_BATTLE_END;
  g_lk.fault.block_left[1] = 1u;
  both_consent();
  CHECK_EQ(g_screen, (uint8_t)SCR_BATTLE);

  play(50u, 6000);
  // The playback is over and the session has NOT agreed yet.
  CHECK_EQ(battle_screen_mode(), (uint8_t)BTM_RESULT);
  const uint8_t mine = (ui_link_battle_side() == 0u) ? (uint8_t)BO_WIN_A : (uint8_t)BO_WIN_B;
  CHECK_EQ(battle_screen_outcome(), mine);
  const int reports_at_playback_end = g_results;

  for (int i = 0; i < 600 && ui_link_battle_status() == (uint8_t)UI_LKB_RUNNING; ++i)
    tick(50u);
  g_box_level = 6u;

  CHECK_EQ((int)ui_link_battle_status(), (int)UI_LKB_WON);
  CHECK_EQ(g_results, 1);
  CHECK_EQ(g_res_won, (uint8_t)1);             // PAID, one rung late
  printf("  the win was confirmed late; %d report(s) had fired at playback end\n",
         reports_at_playback_end);
}

// A LINK THAT DIES MID-BATTLE PAYS NOTHING EITHER, and it is not called a
// defeat: SE_LOST is a radio and not an opponent.
TEST(a_link_that_dies_mid_battle_pays_nothing_and_is_not_a_defeat) {
  harness_reset(0x11110000u, 0x22220000u);
  both_consent();
  const uint32_t fp = box_fingerprint();

  // Two rounds of a real fight, and then the room is cut in half.
  for (int i = 0; i < 40 && g_screen == (uint8_t)SCR_BATTLE; ++i) {
    tick(50u);
    if (battle_screen_mode() == BTM_INTRO)   battle_input(GST_TAP_R);
    if (battle_screen_mode() == BTM_MENU)    battle_input(GST_TAP_R);
    if (battle_screen_mode() == BTM_RESOLVE) battle_input(GST_TAP_L);
  }
  g_lk.fault.dead = 1u;
  play(50u, 4000);

  CHECK_EQ((int)ui_link_battle_status(), (int)UI_LKB_BROKEN);
  CHECK_EQ(g_results, 1);
  CHECK_EQ(g_res_won, (uint8_t)0);
  CHECK_EQ(box_fingerprint(), fp);
}

// A PLAYER WHO WALKS OUT OF A LINKED BATTLE IS PAID NOTHING, and the radio goes
// with them however they left. The HOME gesture is the interesting route: it
// throws the whole back stack away and runs only the CURRENT screen's leave, so
// the LINK screen underneath is never asked - which is why battle_leave() is
// the hook that hands the link back.
TEST(going_home_from_a_linked_battle_pays_nothing_and_gives_the_radio_back) {
  harness_reset(0x11110000u, 0x22220000u);
  both_consent();
  CHECK_EQ(g_screen, (uint8_t)SCR_BATTLE);
  CHECK(link_screen_busy());
  const uint32_t fp = box_fingerprint();

  // sm_home(): the stack is discarded and only SCR_BATTLE's leave() runs.
  screen_leave((uint8_t)SCR_BATTLE);
  g_screen = (uint8_t)SCR_HOME;
  g_sp = 0;

  CHECK_EQ(g_results, 1);
  CHECK_EQ(g_res_won, (uint8_t)0);
  CHECK_EQ(box_fingerprint(), fp);
  CHECK(!link_screen_busy());
  CHECK_EQ(lf_stops(), 1);
  CHECK_EQ(lf_unbinds(), 1);
  CHECK_EQ((int)link_screen_end_reason(), (int)SE_LOCAL_CANCEL);
}

// THE MOVE CLOCK IS THE SESSION'S, AND IT IS NINE SECONDS. MEASURED rather than
// asserted from the constant: a round neither device advances is closed by the
// retransmission ladder, because nothing a waiting peer sends is PROGRESS
// (networking/session.h rule R4). A player who thinks longer than that loses
// the link and is paid nothing - which is a real constraint on this design and
// is written into the plan rather than papered over.
TEST(a_player_who_thinks_longer_than_the_ladder_loses_the_link_and_is_paid_nothing) {
  harness_reset(0x11110000u, 0x22220000u);
  both_consent();
  CHECK_EQ(g_screen, (uint8_t)SCR_BATTLE);

  // Get to the menu, which is where the lockstep is waiting for us.
  for (int i = 0; i < 200 && battle_screen_mode() != BTM_MENU; ++i) {
    tick(50u);
    if (battle_screen_mode() == BTM_INTRO) battle_input(GST_TAP_R);
  }
  CHECK_EQ(battle_screen_mode(), (uint8_t)BTM_MENU);
  CHECK(ui_link_battle_wants_action());

  const uint32_t shown = ui_link_battle_move_ms_left(g_now);
  const uint32_t t0 = g_now;
  uint32_t died = 0;
  for (int i = 0; i < 600 && died == 0u; ++i) {
    tick(50u);                                   // and the player presses nothing
    if (ui_link_battle_status() != (uint8_t)UI_LKB_RUNNING) died = (uint32_t)(g_now - t0);
  }
  CHECK(died != 0u);
  printf("  a round nobody advances dies after %u ms; the screen showed %u ms\n",
         (unsigned)died, (unsigned)shown);
  const uint32_t ladder = (uint32_t)PROTO_RETX_MS * (uint32_t)PROTO_RETX_MAX;
  CHECK(died <= ladder + 1500u);
  // The screen's own countdown is honest about it: within one rung.
  CHECK(shown <= ladder);
  CHECK(shown + (uint32_t)PROTO_RETX_MS >= died);

  play(50u, 200);
  CHECK_EQ(g_results, 1);
  CHECK_EQ(g_res_won, (uint8_t)0);
}

// A WIN THE PEER NEVER CONFIRMED PAYS NOTHING. This is the sharpest form of the
// reward gate: the local engine really did win - `battle_screen_outcome()` says
// so - and the two BATTLE_ENDs carried different final hashes, so
// networking/battle_link.cpp's compare_end() closed SE_DESYNC without ever
// setting `paid`. A screen that reported the ENGINE'S verdict instead of the
// SESSION'S would hand out XP_BATTLE_WIN here, every time, to whichever device
// asked first.
TEST(a_win_the_peer_never_confirmed_pays_nothing) {
  g_box_level = 22u;                 // this device's team is far stronger, so
  harness_reset(0x11110000u, 0x22220000u);   // the local engine WILL win
  // THE PEER'S BATTLE_END NEVER ARRIVES. transport.h's scripted block kills one
  // NAMED message and nothing else, which is the cleanest way to reach the one
  // state the reward gate is really about: this engine has finished and won,
  // our own BATTLE_END is on the wire, and the confirmation that would set
  // `paid` is the frame that is missing.
  g_lk.fault.block_type[1] = (uint8_t)PT_BATTLE_END;
  g_lk.fault.block_left[1] = 0xFFu;
  both_consent();
  CHECK_EQ(g_screen, (uint8_t)SCR_BATTLE);
  const uint32_t fp = box_fingerprint();

  play(50u, 6000);
  // AND THEN THE WAIT. The screen lands on the result page with the outcome
  // decided and the session still in SS_ENDING, which is precisely the window
  // in which reporting would be wrong: nothing has authorised anything yet.
  // Nine rungs later the ladder gives up and the answer is BROKEN.
  for (int i = 0; i < 600 && ui_link_battle_status() == (uint8_t)UI_LKB_RUNNING; ++i)
    tick(50u);
  g_box_level = 6u;

  // The local engine finished, and it finished OUR way.
  const uint8_t mine = (ui_link_battle_side() == 0u) ? (uint8_t)BO_WIN_A : (uint8_t)BO_WIN_B;
  CHECK_EQ(battle_screen_outcome(), mine);

  // And nothing was paid for it, because nobody agreed it.
  CHECK_EQ((int)ui_link_battle_status(), (int)UI_LKB_BROKEN);
  CHECK_EQ(g_results, 1);
  CHECK_EQ(g_res_won, (uint8_t)0);
  CHECK_EQ(box_fingerprint(), fp);
  printf("  the local engine won outcome %u and the session paid %u\n",
         (unsigned)battle_screen_outcome(), (unsigned)g_res_won);
}

// =============================================================================
//  6. THE RADIO, ON EVERY EXIT PATH
// =============================================================================
TEST(the_radio_is_released_exactly_once_on_every_way_off_this_screen) {
  // (a) B from the browse.
  harness_reset(0x11110000u, 0x22220000u);
  CHECK_EQ(lf_starts(), 1);
  link_input(GST_HOLD_R);
  CHECK_EQ(lf_stops(), 1);
  CHECK(!link_screen_busy());

  // (b) B from the wait, which also tells the peer rather than leaving it to
  //     sit through its own ladder.
  harness_reset(0x11110000u, 0x22220000u);
  see_peer((uint16_t)DISC_CAP_BATTLE, -40, 0u);
  consent_to((uint8_t)LOP_BATTLE);
  link_input(GST_HOLD_R);
  CHECK_EQ(lf_stops(), 1);
  CHECK_EQ(lf_unbinds(), 1);
  CHECK(!link_screen_busy());
  CHECK_EQ(g_toast, STR_LK_CANCELLED);

  // (c) the auto-return / LONG_BOTH out of the browse: leave() and nothing else.
  harness_reset(0x11110000u, 0x22220000u);
  link_leave();
  CHECK_EQ(lf_stops(), 1);
  link_leave();                                  // idempotent
  CHECK_EQ(lf_stops(), 1);

  // (d) the browse's own section 47 ceiling.
  harness_reset(0x11110000u, 0x22220000u);
  g_now += (uint32_t)LINK_JOB_TIMEOUT_MS + 1u;
  link_update(g_now);
  CHECK_EQ(link_screen_mode(), (uint8_t)LKM_LOST);
  CHECK_EQ(lf_stops(), 1);
  CHECK(!link_screen_busy());

  // (e) a radio that refuses to come up at all is a named failure, not a
  //     spinner, and it is not counted as a release that never happened.
  harness_reset(0x11110000u, 0x22220000u);
  link_leave();
  lf_set_start_ok(false);
  link_enter();
  CHECK_EQ(link_screen_mode(), (uint8_t)LKM_LOST);
  CHECK_EQ(g_toast, STR_LK_RADIO_ERR);
  CHECK(!link_screen_busy());
}

// THE BROWSE STOPS AND THE RADIO DOES NOT. link_hold() is what separates those
// two, and calling link_cancel() in its place would put the stack down one
// frame after the two players agreed to use it.
TEST(consenting_stops_the_browse_and_keeps_the_radio) {
  harness_reset(0x11110000u, 0x22220000u);
  see_peer((uint16_t)DISC_CAP_BATTLE, -40, 0u);
  const int tx_before = lf_beacons_tx();
  consent_to((uint8_t)LOP_BATTLE);

  CHECK_EQ(lf_stops(), 0);                       // the radio is still ours
  CHECK(link_screen_busy());
  for (int i = 0; i < 40; ++i) tick(100u);       // four seconds of frames
  CHECK_EQ(lf_beacons_tx(), tx_before);          // and not one more beacon
}

// EVERY FRAME OF THIS SCREEN DRAWS INSIDE THE PANEL. The goldens live in
// tests/test_screens.cpp; this is the out-of-bounds half, over the modes that
// need a live session to reach and which no golden can therefore hold still.
TEST(every_link_mode_draws_inside_the_panel) {
  harness_reset(0x11110000u, 0x22220000u);
  fb_reset(); link_render(); CHECK_EQ(fb_oob(), 0u);       // browse, empty

  see_peer((uint16_t)DISC_CAP_BATTLE, -40, 0u);
  fb_reset(); link_render(); CHECK_EQ(fb_oob(), 0u);       // browse, one peer
  link_input(GST_TAP_R);
  fb_reset(); link_render(); CHECK_EQ(fb_oob(), 0u);       // the card

  peer_consent();
  link_input(GST_TAP_R);                                   // consent
  fb_reset(); link_render(); CHECK_EQ(fb_oob(), 0u);       // the wait

  for (int i = 0; i < 200 && g_screen != (uint8_t)SCR_BATTLE; ++i) {
    tick(50u);
    if (g_screen == (uint8_t)SCR_LINK) { fb_reset(); link_render(); CHECK_EQ(fb_oob(), 0u); }
  }
  CHECK_EQ(g_screen, (uint8_t)SCR_BATTLE);
  fb_reset(); battle_render(); CHECK_EQ(fb_oob(), 0u);     // the linked field

  for (int i = 0; i < 400 && battle_screen_mode() != BTM_RESULT; ++i) {
    tick(50u);
    if (battle_screen_mode() == BTM_INTRO)   battle_input(GST_TAP_R);
    if (battle_screen_mode() == BTM_MENU)    battle_input(GST_TAP_R);
    if (battle_screen_mode() == BTM_RESOLVE) battle_input(GST_TAP_L);
    fb_reset(); battle_render(); CHECK_EQ(fb_oob(), 0u);
  }
  battle_input(GST_TAP_R);
  CHECK_EQ(g_screen, (uint8_t)SCR_LINK);
  fb_reset(); link_render(); CHECK_EQ(fb_oob(), 0u);       // the ended page
  // ...AND EVERY ONE OF THEM DREW TEXT THE PANEL CAN ACTUALLY SHOW.
  // Added at the FINAL REVIEW. The two recorders below were bought with shipped
  // defects - fb_bad_utf8() with P10-C4's split multi-byte sequence,
  // fb_no_glyph() with P10-C6's five accented strings drawn in a 95-glyph
  // ASCII-only face - and until now they were asserted in tests/test_screens.cpp
  // ALONE. Three of the four binaries that link the fake and draw never read
  // them, and THIS is the one that covers ui/screen_link.cpp, whose header row
  // and whose peer rows put a live NAME on the panel. Names are the exact input
  // class that produced the P10-C6 beacon defect.
  //
  // They are lifetime totals across every render above, so this is one
  // assertion for the whole sweep.
  if (fb_no_glyph() != 0u) fprintf(stderr, "  %s\n", fb_no_glyph_first());
  CHECK_EQ(fb_no_glyph(), 0u);
  if (fb_bad_utf8() != 0u) fprintf(stderr, "  %s\n", fb_bad_utf8_first());
  CHECK_EQ(fb_bad_utf8(), 0u);
}

// =============================================================================
//  7. NO FRAME OF A LINKED BATTLE ALLOCATES
//
//  ui/screen_battle.h's no-per-frame-heap promise, over the ONE mode
//  tests/test_battle_screen.cpp cannot reach: BTM_WAIT, which only exists while
//  a peer is choosing. The session, the codec and the lockstep are in this
//  binary too, so the count below covers a whole linked battle's frames and not
//  only the screen's.
//
//  The counter is PROVED TO FIRE before its zero is trusted, exactly as
//  tests/test_battle_screen.cpp does it - a heap gate that cannot fail is the
//  defect this project keeps finding.
// =============================================================================
extern "C" void* __libc_malloc(size_t);
extern "C" void* __libc_calloc(size_t, size_t);
extern "C" void* __libc_realloc(void*, size_t);
extern "C" void  __libc_free(void*);

static long g_allocs = 0;
static int  g_watch  = 0;

extern "C" void* malloc(size_t n)           { if (g_watch) ++g_allocs; return __libc_malloc(n); }
extern "C" void* calloc(size_t n, size_t m) { if (g_watch) ++g_allocs; return __libc_calloc(n, m); }
extern "C" void* realloc(void* p, size_t n) { if (g_watch) ++g_allocs; return __libc_realloc(p, n); }
extern "C" void  free(void* p)              { __libc_free(p); }

void* operator new(size_t n) {
  if (g_watch) ++g_allocs;
  void* p = __libc_malloc(n ? n : 1u);
  if (!p) throw std::bad_alloc();
  return p;
}
void* operator new[](size_t n) { return operator new(n); }
void  operator delete(void* p) noexcept           { __libc_free(p); }
void  operator delete[](void* p) noexcept         { __libc_free(p); }
void  operator delete(void* p, size_t) noexcept   { __libc_free(p); }
void  operator delete[](void* p, size_t) noexcept { __libc_free(p); }

TEST(no_frame_of_a_linked_battle_allocates_and_the_waiting_mode_is_drawn) {
  // (0) THE COUNTER WORKS.
  g_allocs = 0; g_watch = 1;
  { volatile int* leak = new int(7); delete leak; }
  { char* c = (char*)malloc(32); free(c); }
  g_watch = 0;
  CHECK(g_allocs >= 2);

  harness_reset(0x11110000u, 0x22220000u);
  both_consent();
  CHECK_EQ(g_screen, (uint8_t)SCR_BATTLE);

  int frames = 0;
  int seen[BTM_MODE_COUNT];
  memset(seen, 0, sizeof seen);

  g_allocs = 0;
  g_watch = 1;
  for (int i = 0; i < 4000 && g_screen == (uint8_t)SCR_BATTLE; ++i) {
    const uint8_t m = battle_screen_mode();
    ++seen[m];
    tick(50u);
    fb_reset();
    battle_render();
    ++frames;
    if (m == BTM_INTRO)   battle_input(GST_TAP_R);
    if (m == BTM_MENU)    battle_input(GST_TAP_R);
    if (m == BTM_SWITCH)  battle_input(GST_TAP_R);
    if (m == BTM_RESOLVE) battle_input(GST_TAP_L);
    if (m == BTM_RESULT && seen[BTM_RESULT] > 4) break;
  }
  g_watch = 0;

  CHECK(frames > 40);
  CHECK(seen[BTM_WAIT] > 0);          // the mode the single-device suite cannot reach
  CHECK(seen[BTM_RESOLVE] > 0);
  CHECK(seen[BTM_RESULT] > 0);
  CHECK_EQ(seen[BTM_PICK], 0);        // a linked entry never opens the pick list
  CHECK_EQ(g_allocs, 0L);
  printf("  %d linked frames, %d of them waiting on the peer, %ld allocations\n",
         frames, seen[BTM_WAIT], g_allocs);
}

// =============================================================================
//  THE TWO REFUSALS THAT HAD NEVER EXECUTED
//  Added at the FINAL REVIEW, with the injectors tests/fakes/link_fake.h grew
//  for them. Both are documented behaviour of networking/transport_espnow.cpp
//  that the fake could not produce, so both arms were dead code in every one of
//  the 59 host binaries.
// =============================================================================

// ui/screen_link.cpp: `if (!ui_link_bind(p->slot)) { ui_toast(STR_LK_RADIO_ERR);
// return false; }` - the branch the player hits when the A press lands on a peer
// the ESP-NOW peer table will not take. espnow_bind() refuses on four separate
// conditions; the fake answered true for any slot including 0xFF, so the arm
// had never run. MEASURED before the injector existed: deleting the whole
// refusal survived 122,581 checks across the three binaries that link
// screen_link.o, and the only STR_LK_RADIO_ERR assertion in the suite sits under
// lf_set_start_ok(false) and therefore covers a DIFFERENT producer.
TEST(a_consent_the_radio_refuses_leaves_the_screen_usable) {
  harness_reset(0x11110000u, 0x22220000u);
  see_peer((uint16_t)DISC_CAP_BATTLE, -40, 0u);
  g_toast = 0;

  lf_set_bind_ok(false);
  const int unbinds_before = lf_unbinds();
  consent_to((uint8_t)LOP_BATTLE);

  // The player is TOLD, and the refusal is the one the radio produced.
  CHECK_EQ(g_toast, STR_LK_RADIO_ERR);
  // Nothing was bound, so nothing needs unbinding: a refusal that then tore
  // down a session it never made would be worse than the refusal.
  CHECK_EQ(lf_unbinds(), unbinds_before);
  // And the screen is still a screen. This is the whole point of the arm: a
  // peer whose slot the transport will not take must not strand the player.
  CHECK(link_screen_mode() == (uint8_t)LKM_CARD ||
        link_screen_mode() == (uint8_t)LKM_BROWSE);
  fb_reset(); link_render(); CHECK_EQ(fb_oob(), 0u);
  link_input(GST_HOLD_L);                       // and the way out still works
  fb_reset(); link_render(); CHECK_EQ(fb_oob(), 0u);

  // With the radio willing, the same gesture consents - so the case above is a
  // statement about the refusal and not about the gesture.
  harness_reset(0x11110000u, 0x22220000u);
  see_peer((uint16_t)DISC_CAP_BATTLE, -40, 0u);
  lf_set_bind_ok(true);
  consent_to((uint8_t)LOP_BATTLE);
  CHECK(lf_bound());
}

// A RADIO THAT REFUSES EVERY BROADCAST IS INDISTINGUISHABLE FROM AN EMPTY ROOM,
// and that is by design in networking/discovery.cpp - a false from beacon()
// means "do not count it" and nothing else. This case records the consequence
// rather than objecting to it: the job still runs its full ceiling and ends
// LS_TIMEOUT, and the ONLY thing in the whole LinkJob that differs is
// beacons_tx, which no ui/ file reads. On a board that is a device nobody can
// see reporting "no peers" about a room it never spoke to; the instrument is
// DIAG,link's beacons_tx column, and README section 2 now says to read it.
TEST(a_radio_that_refuses_every_broadcast_looks_exactly_like_an_empty_room) {
  harness_reset(0x11110000u, 0x22220000u);
  lf_set_beacon_ok(false);
  for (int i = 0; i < 40; ++i) tick(100u);
  const int tx_deaf   = lf_beacons_tx();
  const uint8_t m_deaf = link_screen_mode();

  harness_reset(0x11110000u, 0x22220000u);
  lf_set_beacon_ok(true);
  for (int i = 0; i < 40; ++i) tick(100u);
  const int tx_live   = lf_beacons_tx();

  CHECK_EQ(tx_deaf, 0);                 // nothing went out...
  CHECK(tx_live > 0);                   // ...where it should have
  CHECK_EQ((int)m_deaf, (int)link_screen_mode());   // and the screen agrees
}
