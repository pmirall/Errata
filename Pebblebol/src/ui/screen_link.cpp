// =============================================================================
//  PEBBLEBOL - ui/screen_link.cpp
//  See screen_link.h. PURE translation unit.
// =============================================================================
#include "screen_link.h"

#include <stdio.h>
#include <string.h>

#include "../core/strings_es.h"
#include "../core/utf8.h"
#include "../data/sprites.h"
#include "../data/balance.h"       // XP_LEVEL_MAX: the top of the level band
#include "../game/box.h"
#include "../networking/battle_link.h"
#include "../networking/discovery.h"
#include "../networking/session.h"
#include "../networking/trade_link.h"   // the P7-C4 trade driver
#include "../game/trade.h"                // trade_offer_check()
#include "../data/species_table.h"        // the two species names on the review line
#include "gfx.h"
#include "screen.h"
#include "screen_battle.h"     // the BattleSetup / BattleState / BattleLog the
                               // session borrows, and the one team builder
#include "ui.h"

// -----------------------------------------------------------------------------
//  SHAPE
// -----------------------------------------------------------------------------
// 20 -> PB_NAME_DRAW_CAP at P10-C4: a peer name is up to NAME_MAX_LEN Latin-1
// characters and therefore up to twice that many UTF-8 bytes once drawn.
#define LK_ROW_CAP        ((uint8_t)PB_NAME_DRAW_CAP)
#define LK_VAL_CAP         8
// The browse list: LINK_PEER_CAP peers plus a "Volver" row.
#define LK_LIST_ROWS      ((uint8_t)(LINK_PEER_CAP + 1u))
// THE PEER NAME IS TRANSCODED ON ARRIVAL, so this cap is the DRAW one.
// networking/discovery.cpp accepts a peer name as raw LATIN-1 bytes off the air
// (name_ok() admits 0xF1 and its neighbours one byte at a time) and every draw
// on this screen goes through drawUTF8(), so twelve stored bytes can be
// twenty-four drawn ones. core/utf8.h holds the crossing.
// The trade review line: nine CHARACTERS a side (core/utf8.h cuts on a
// codepoint boundary, so the cap is stated in the bytes nine characters can
// take) plus " > " plus the terminator.
#define LK_TRADE_HALF_CHARS ((uint16_t)9)
#define LK_TRADE_LINE_CAP   ((uint8_t)(2 * (LK_TRADE_HALF_CHARS * 2) + 3 + 1))

#define LK_NAME_CAP       ((uint8_t)PB_NAME_DRAW_CAP)

// -----------------------------------------------------------------------------
//  STATE. All of it here, on purpose: the 280 B LinkJob is caller-owned exactly
//  as the NETWORK screen's 136 B WifiScanJob is, and the Session is caller-owned
//  for the reason networking/session.h gives - one host process runs two
//  endpoints, and a module that held its own could not.
//
//  WHAT IS NOT HERE: a BattleSetup and a BattleState. networking/session.h says
//  in as many words that the linked path "borrows the one ui/screen_battle.cpp
//  already owns at file scope", so there is never a second 780 B setup and
//  never a second source of truth for a hashed team.
//
//  AND NO LinkLog. It is optional (session.h: "may be nullptr") and it is 8 B
//  per entry of transport telemetry the player never sees; SessionEnd's 40 B
//  record already answers "why did it end" without a rerun, which is what this
//  screen shows. If a bench ever needs the ring, it is one static array away
//  and the argument for adding it is a measurement, not a habit.
// -----------------------------------------------------------------------------
static LinkJob  s_job;
static Session  s_sess;
// THE TRADE (P7-C4). 112 B, caller-owned exactly as the LinkJob and the Session
// are, and for the same reason networking/trade_link.h gives: two endpoints
// share one host process and a module that held its own could not.
static TradeLink s_tl;
static uint8_t   s_trade_slot = (uint8_t)BOX_SLOT_NONE;

static uint8_t  s_mode      = LKM_BROWSE;
static uint8_t  s_cur       = 0;
static uint8_t  s_op        = (uint8_t)LOP_BATTLE;
// THE PEER'S NAME AND NOTHING ELSE. Its slot, its device id and its capability
// word all live in the LinkJob's own table and in the Session, and a second
// copy here would be a second thing to keep true; the NAME is copied because
// §47's "CONEXIÓN PERDIDA" has to be able to say who the player was talking to
// AFTER the table that held them has been reset.
static char     s_peer_name[LK_NAME_CAP];
static uint8_t  s_live      = 0;          // a Session has been initialised
static uint8_t  s_handoff   = 0;          // SCR_BATTLE is on top of us
static uint8_t  s_consents  = 0;          // times A opened a session this visit
static uint8_t  s_end_reason = (uint8_t)SE_NONE;
static uint8_t  s_armed_op  = 0xFFu;      // link_arm_intent()
static uint8_t  s_armed_slot = 0xFFu;
static uint8_t  s_intent_slot = 0xFFu;    // the Box slot the BOX pre-selected
static uint8_t  s_show_end  = 0;          // the battle screen handed the link back
static uint32_t s_now       = 0;

uint8_t  link_screen_mode(void)    { return s_mode; }
uint8_t  link_screen_cursor(void)  { return s_cur; }
uint8_t  link_screen_op(void)      { return s_op; }
uint8_t  link_screen_peers(void)   { return link_qualified_count(s_job); }
uint8_t  link_screen_consents(void){ return s_consents; }
uint8_t  link_screen_end_reason(void) { return s_end_reason; }
const char* link_screen_peer_name(void) { return s_peer_name; }

uint8_t  link_trade_slot(void)     { return s_trade_slot; }
uint32_t link_trade_peer_id(void)  { return s_live ? s_sess.peer_device_id : 0u; }
uint8_t  link_trade_phase(void)    { return s_live ? s_tl.phase : (uint8_t)TLP_IDLE; }
bool     link_trade_wants_consent(void) {
  return s_live && s_op == (uint8_t)LOP_TRADE && trade_link_wants_consent(s_sess);
}

uint8_t link_screen_session_state(void) {
  return s_live ? (uint8_t)session_state(s_sess) : (uint8_t)SS_IDLE;
}

// The power ladder's `held`. hardware/power.h clamps the ladder at DIM while
// this is true, and the release itself is a NAVIGATION, so it runs link_leave()
// and therefore link_cancel().
//
// THE JOB'S OWN FLAG IS THE WHOLE ANSWER, AND link_hold() IS WHY. This read
// `link_is_busy(s_job) || (s_live && !session_closed(s_sess))` for one
// afternoon, and the second half was UNREACHABLE: a session only ever exists
// over a job that link_hold() left LS_RUNNING, so the OR could not answer true
// where the left side answered false. A mutation deleting the second half
// failed nothing, which is exactly the "sentence wider than the tree" this
// project keeps finding, so the sentence is narrowed instead of decorated.
//
// IT IS ALSO A DEPENDENCY, stated: if the consent path ever calls
// link_cancel() where it now calls link_hold(), this goes false while a session
// is live and the ladder starts dropping a radio somebody is using.
// `consenting_stops_the_browse_and_keeps_the_radio` is the case that fails if
// it does.
bool link_screen_busy(void) { return link_is_busy(s_job); }

// -----------------------------------------------------------------------------
//  THE BEACON THIS DEVICE SENDS
//
//  Four fields and a checksum, and networking/discovery.h is where the list is
//  closed (spec sections 43/44). The NAME is the player's own pet name and not
//  anything derived from the hardware - net.cpp's access-point name is built
//  from two bytes of the station address and those two bytes stay out of the
//  one payload section 43 governs.
//
//  THE CAPABILITIES ARE WHAT THIS BUILD CAN ACTUALLY DO, AND THE WORD "CAN" IS
//  LOAD-BEARING. DISC_CAP_BATTLE has been claimed since P7-C3; DISC_CAP_TRADE
//  joins it in P7-C4, because from this commit an A on the INTERCAMBIO row
//  really opens a trade session. DISC_CAP_BREED IS STILL NOT CLAIMED:
//  game/breeding.cpp is complete and tested and NO WIRE DRIVER CARRIES A
//  BREEDING (P7-C5's breed_link.cpp is not built), so claiming it would offer a
//  player an operation whose only possible outcome is a nine-second timeout.
//  A peer running this firmware offers exactly the operations that exist.
//
//  A reserved bit is a refusal on the far side (discovery.h), which is what
//  stops a future capability being silently accepted by a build that predates
//  it - and it is also what makes THIS line the one that has to move when
//  breed_link.cpp lands, rather than something noticing on its own.
// -----------------------------------------------------------------------------
#define LK_SELF_CAPS  ((uint16_t)(DISC_CAP_BATTLE | DISC_CAP_TRADE))

static void fill_self(DiscBeacon& b) {
  memset(&b, 0, sizeof b);
  b.device_id = ui_device_seed();
  b.caps      = (uint16_t)LK_SELF_CAPS;
  b.ver       = (uint8_t)PROTOCOL_VERSION;
  ui_pet_name(b.name, sizeof b.name);
}

// -----------------------------------------------------------------------------
//  THE PEER LIST
//
//  A peer the player may pick is a QUALIFIED one - LINK_PEER_HITS_MIN beacons
//  above the signal floor - because one stray packet from a passing stranger is
//  not a Pebblebol in the room (networking/discovery.h). The unqualified ones
//  stay in the table so their hits can accumulate and are simply not drawn.
// -----------------------------------------------------------------------------
static const DiscPeer* nth_qualified(uint8_t n) {
  uint8_t k = 0;
  for (uint8_t i = 0; i < link_peer_count(s_job); ++i) {
    const DiscPeer* p = link_peer(s_job, i);
    if (p == nullptr || (p->flags & DP_QUALIFIED) == 0u) continue;
    if (k == n) return p;
    ++k;
  }
  return nullptr;
}

// -----------------------------------------------------------------------------
//  THE RADIO
// -----------------------------------------------------------------------------
static void start_browse(void) {
  link_reset(s_job);
  s_mode = LKM_BROWSE;
  s_cur  = 0;
  gfx_list_reset();
  if (!link_start(s_job, ui_link_driver(), s_now)) {
    s_mode = LKM_LOST;
    s_end_reason = (uint8_t)SE_NONE;
    ui_toast(STR_LK_RADIO_ERR);
  }
}

// THE ONE TEARDOWN. Idempotent on both halves, and it is what makes "the radio
// shuts down after use" a property of the STATE rather than of the route the
// player took out of it: link_cancel() calls the driver's stop() exactly once
// per started job (networking/discovery.h) and stop() reaches
// net_request(RADIO_OFF), which passes wifi_down(), which calls espnow_end().
static void release_all(void) {
  // A TRADE THAT NEVER REACHED ITS COMMIT LEAVES A JOURNAL BEHIND, and leaving
  // it there would make the NEXT boot roll back a trade the player already
  // walked away from - correct, but a whole power cycle later and with a toast
  // nobody asked for. The abort hook clears it now; the boot resolver stays the
  // backstop for the case this line cannot reach, which is the power cut.
  if (s_live && s_op == (uint8_t)LOP_TRADE && s_tl.applied == 0u &&
      s_tl.hooks.abort != nullptr) {
    s_tl.hooks.abort(s_tl.hooks.ctx);
  }
  if (s_live && !session_closed(s_sess)) session_cancel(s_sess, s_now);
  s_live = 0;
  s_trade_slot = (uint8_t)BOX_SLOT_NONE;
  ui_link_unbind();
  // link_cancel() KEEPS THE PEER LIST on purpose (networking/discovery.cpp says
  // why): "CONEXIÓN PERDIDA / A: Reintentar" needs to know who the player was
  // talking to, and start_browse() is what clears it when they retry.
  link_cancel(s_job, ui_link_driver());
}

// -----------------------------------------------------------------------------
//  THE TEAM THIS DEVICE OFFERS
//
//  Up to BATTLE_TEAM_MAX Box slots, the ACTIVE one first so the Pebble the
//  player is carrying leads, then slot order. A slot the BOX pre-selected
//  (link_arm_intent) leads instead, which is what "pre-selects that Pebble"
//  means on this side of the push.
//
//  IT GOES THROUGH ui/screen_battle.cpp's OWN COPY, and that is deliberate
//  rather than convenient: battle_copy_from_box() is the repair the PRACTICE
//  path already applies to THIS DEVICE'S OWN Box - a v1 save's empty moveset
//  falls back to the species learnset, hp comes up to full, a stale FAINTED bit
//  is cleared - and doing it here means the object that goes on the wire is the
//  one both devices then validate. It is NOT the thing networking/battle_link.cpp
//  forbids: that rule is about mending what a PEER sent, and nothing here
//  touches a peer's bytes. session_set_team() then runs game/validate.cpp over
//  the result and its verdict is final; a Pebble this device cannot make legal
//  is refused by name and never offered.
// -----------------------------------------------------------------------------
static uint8_t build_team(PebbleInstance* out, uint8_t cap) {
  uint8_t n = 0;
  const uint8_t lead = (s_intent_slot < (uint8_t)BOX_SLOTS) ? s_intent_slot
                                                            : box_active();
  if (lead < (uint8_t)BOX_SLOTS && n < cap && battle_copy_from_box(out[n], lead)) ++n;
  for (uint8_t i = 0; i < (uint8_t)BOX_SLOTS && n < cap; ++i) {
    if (i == lead) continue;
    if (battle_copy_from_box(out[n], i)) ++n;
  }
  return n;
}

// -----------------------------------------------------------------------------
//  CONSENT. THE ONLY FUNCTION IN THIS FILE THAT OPENS A SESSION.
//
//  It is reached from exactly one place - the A press on the card's operation
//  row - and nothing else in this translation unit calls session_init(),
//  session_start() or session_poll() outside the guard below. That is the
//  half of "consent is not implied by proximity" a host binary can see; the
//  other half is the radio, which delivers a unicast frame only from the peer
//  this device has BOUND, and it binds here and nowhere else.
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
//  THE ONE PEBBLE THIS DEVICE OFFERS IN A TRADE
//
//  The BOX's pre-selection leads (link_arm_intent); otherwise the first slot
//  game/trade.cpp will accept. The RULES are game/trade.cpp's - the active
//  Pebble is not for sale, a quarantined one may not enter a trade, an object
//  the one validator refuses may not be offered - and this function does not
//  re-derive one of them: it asks, and it reports the refusal BY NAME so a
//  player who armed a slot the rules refuse is told which rule.
// -----------------------------------------------------------------------------
static uint8_t pick_trade_slot(uint8_t& why) {
  const uint16_t q = ui_trade_quarantine();
  if (s_intent_slot < (uint8_t)BOX_SLOTS) {
    why = (uint8_t)trade_offer_check(s_intent_slot, q);
    if (why == (uint8_t)TDR_OK) return s_intent_slot;
    // A slot the BOX armed and the rules refuse is NOT quietly replaced with a
    // different Pebble: the player chose that one.
    return (uint8_t)BOX_SLOT_NONE;
  }
  // KEEP THE MOST INFORMATIVE REFUSAL, and the order matters because slot 0 is
  // usually the ACTIVE one: "guarda ese Pebble primero" is the right answer for
  // a player whose only other Pebbles are fine, and the WRONG one for a player
  // whose other Pebbles are all quarantined. So an empty slot's reason is
  // beaten by any other, and TDR_ACTIVE is beaten by anything that is not it.
  why = (uint8_t)TDR_NO_SLOT;
  for (uint8_t i = 0; i < (uint8_t)BOX_SLOTS; ++i) {
    const uint8_t r = (uint8_t)trade_offer_check(i, q);
    if (r == (uint8_t)TDR_OK) { why = r; return i; }
    if (why == (uint8_t)TDR_NO_SLOT) why = r;
    else if (why == (uint8_t)TDR_ACTIVE && r != (uint8_t)TDR_NO_SLOT) why = r;
  }
  return (uint8_t)BOX_SLOT_NONE;
}

static bool open_session(void) {
  const DiscPeer* p = nth_qualified(s_cur);
  if (p == nullptr) { ui_toast(STR_LK_NOBODY); return false; }

  const bool trading = (s_op == (uint8_t)LOP_TRADE);
  PebbleInstance team[BATTLE_TEAM_MAX];
  uint8_t n = 0;
  uint8_t offer_slot = (uint8_t)BOX_SLOT_NONE;

  if (trading) {
    if (ui_trade_hooks() == nullptr) { ui_toast(STR_UI_SOON); return false; }
    uint8_t why = (uint8_t)TDR_NO_SLOT;
    offer_slot = pick_trade_slot(why);
    if (offer_slot >= (uint8_t)BOX_SLOTS) {
      ui_toast((why == (uint8_t)TDR_ACTIVE)      ? STR_LK_TR_ACTIVE
             : (why == (uint8_t)TDR_QUARANTINED) ? STR_LK_TR_QUARANTINED
                                                 : STR_LK_NO_TEAM);
      return false;
    }
  } else {
    n = build_team(team, (uint8_t)BATTLE_TEAM_MAX);
    if (n == 0u) { ui_toast(STR_LK_NO_TEAM); return false; }
  }

  if (!ui_link_bind(p->slot)) { ui_toast(STR_LK_RADIO_ERR); return false; }

  (void)u8_from_latin1(s_peer_name, (uint16_t)sizeof s_peer_name, p->name);

  SessionCfg cfg;
  memset(&cfg, 0, sizeof cfg);
  cfg.tp        = &ui_link_transport();
  cfg.setup     = battle_link_setup();
  cfg.st        = battle_link_state();
  cfg.blog      = battle_link_log();
  cfg.llog      = nullptr;
  cfg.device_id = ui_device_seed();
  cfg.nonce     = ui_link_nonce();
  // THE WHOLE BAND, and it is not a placeholder. A linked battle between two
  // friends in the same room is not matchmaking: refusing a level 3 Pebble a
  // fight with a level 20 one is a rule the players can apply themselves, and
  // the band exists so a FUTURE mode can narrow it (session.h's SD_BAND is
  // reachable the moment anything does).
  //
  // AND IT MUST BE THE SAME NUMBER ON BOTH DEVICES, which is why it is
  // XP_LEVEL_MAX and not a literal. networking/session.cpp's band_acceptable()
  // requires the REQUESTED band to sit INSIDE the responder's own, so a device
  // offering a wider band is refused by name (SE_REJECTED, with the VReject as
  // the detail) - and the refusal is asymmetric, so it would show up on one of
  // the two boards and not the other. Measured, when the test's fake peer still
  // offered 0..255 against this screen's 1..30: the pairing worked in one
  // direction and was rejected in the other, purely on which device the session
  // made the initiator. A shared constant is what makes that impossible.
  cfg.lvl_lo    = 1u;
  cfg.lvl_hi    = (uint8_t)XP_LEVEL_MAX;
  // THE OPERATION, AGREED IN THE HANDSHAKE (networking/session.h). It is the
  // last cheap moment for two devices to discover they came here to do
  // different things: the beacon does not carry it (spec 43/44 lists what a
  // beacon may say and an intention is not on the list).
  cfg.op        = trading ? (uint8_t)SOP_TRADE : (uint8_t)SOP_BATTLE;
  cfg.tl        = trading ? &s_tl : nullptr;
  session_init(s_sess, cfg);

  if (trading) {
    const PebbleInstance* mine = box_peek(offer_slot);
    if (mine == nullptr) { ui_link_unbind(); ui_toast(STR_LK_NO_TEAM); return false; }
    // session_set_trade() runs the ONE validator and NOT validate_battle_ready():
    // a fainted Pebble is a legal thing to have stored and therefore a legal
    // thing to give away (networking/session.h says so at the declaration).
    if (session_set_trade(s_sess, *mine) != VR_OK) {
      ui_link_unbind();
      ui_toast(STR_LK_TEAM_BAD);
      return false;
    }
    trade_link_init(s_tl, s_sess, mine->id, *ui_trade_hooks());
    s_trade_slot = offer_slot;
  } else {
    uint8_t bad = 0xFFu;
    const VReject v = session_set_team(s_sess, team, n, bad);
    if (v != VR_OK) {
      ui_link_unbind();
      ui_toast(STR_LK_TEAM_BAD);
      return false;
    }
  }

  s_live = 1;
  ++s_consents;
  s_end_reason = (uint8_t)SE_NONE;
  session_start(s_sess, s_now);
  return true;
}

// -----------------------------------------------------------------------------
//  THE HAND-OFF TO SCR_BATTLE
//
//  The session reaches SS_VERIFY only after networking/battle_link.cpp's
//  link_begin() has run battle_init() on the BattleState this screen borrowed,
//  so by the time we push, the battle already exists and battle_enter() adopts
//  it rather than building one.
//
//  s_handoff IS WHAT KEEPS link_leave() FROM TEARING THE LINK DOWN ON THE WAY.
//  app/state_machine.cpp's sm_push() runs the LEAVING screen's leave() hook, so
//  a push from here is indistinguishable from the player walking out unless the
//  screen says which it is. The flag is cleared by the next link_enter(), which
//  is the frame the player comes BACK to this screen on.
// -----------------------------------------------------------------------------
static void hand_off_to_battle(void) {
  s_handoff = 1;
  s_mode    = LKM_HANDOFF;
  battle_arm_link(link_battle_side());
  ui_push(SCR_BATTLE);
}

// -----------------------------------------------------------------------------
//  THE SESSION PUMP
//
//  ONE GUARD, and it is the consent gate: nothing below runs - not even a
//  transport drain - until open_session() has been through.
// -----------------------------------------------------------------------------
static void pump(uint32_t now_ms) {
  s_now = now_ms;
  if (!s_live) return;
  if (session_closed(s_sess)) return;
  session_poll(s_sess, now_ms);
}

static void note_end(void) {
  if (!s_live || !session_closed(s_sess)) return;
  s_end_reason = (uint8_t)session_end(s_sess).reason;
}

// -----------------------------------------------------------------------------
//  THE HOOKS
// -----------------------------------------------------------------------------
void link_enter(void) {
  uint32_t now_ms = 0;
  ui_explore_clock(nullptr, &now_ms, nullptr);
  s_now = now_ms;

  // COMING BACK FROM THE BATTLE, not arriving. link_battle_done() has already
  // closed the session and given the radio back - it runs in the battle
  // screen's leave() hook, which app/state_machine.cpp calls immediately before
  // this - so all that is left is to say what happened. No new browse is
  // started: the player asked to go back, not to look for somebody else.
  if (s_show_end) {
    s_show_end = 0;
    s_mode     = (uint8_t)LKM_ENDED;
    s_cur      = 0;
    return;
  }

  s_op          = (s_armed_op < (uint8_t)LOP_CANCEL) ? s_armed_op : (uint8_t)LOP_BATTLE;
  s_intent_slot = s_armed_slot;
  s_armed_op    = 0xFFu;
  s_armed_slot  = 0xFFu;
  s_live        = 0;
  s_consents    = 0;
  s_end_reason  = (uint8_t)SE_NONE;
  s_peer_name[0] = '\0';
  start_browse();
}

void link_update(uint32_t now_ms) {
  s_now = now_ms;

  // The browse. It is running only while nobody has consented: the moment a
  // session opens, the radio's job changes from "who is here" to "talk to
  // this one", and a beacon going out underneath a bound unicast link would be
  // noise nobody reads.
  if (s_mode == (uint8_t)LKM_BROWSE || s_mode == (uint8_t)LKM_CARD) {
    uint32_t epoch = 0, ms = now_ms;
    uint8_t  cal = 0;
    ui_explore_clock(&epoch, &ms, &cal);
    s_now = ms;

    DiscBeacon self;
    fill_self(self);
    ActClock aclk;
    aclk.now_epoch = epoch;
    aclk.cal       = cal;

    const uint16_t scored_before = s_job.peers_scored;
    // THE FIRST CALLER act_note_peer() HAS EVER HAD, and the call is inside
    // link_service() rather than here: game/activity.cpp is paid once per peer
    // that CROSSES the hit threshold and never once per beacon, and that rule
    // is a property of the peer table, so it lives with the table
    // (networking/discovery.h). This is the screen that finally drives it.
    link_service(s_job, ui_link_driver(), self, ms, ui_cooldowns(), aclk);
    if (s_job.peers_scored != scored_before) ui_explore_commit();

    ui_request_frame();     // the signal bars and the peer count move

    switch ((LinkState)s_job.state) {
      case LS_RUNNING:
        break;
      case LS_TIMEOUT:
      case LS_FAILED:
      case LS_CANCELLED:
        // Spec section 47: a browse that ended is a recoverable state with a
        // message and a way on, never a screen that spins for ever. The radio
        // is already off - the job releases it on every exit path.
        s_mode = (uint8_t)LKM_LOST;
        break;
      default:
        break;
    }
    return;
  }

  if (s_mode == (uint8_t)LKM_WAIT) {
    const uint8_t was_phase = s_tl.phase;
    pump(now_ms);
    if (session_closed(s_sess)) {
      note_end();
      // A TRADE THAT FINISHED IS NOT A LOST LINK. The battle path never reaches
      // this line - it hands off to SCR_BATTLE long before the session closes -
      // so the single LKM_LOST here was true of every case that could get to
      // it. It stopped being true the moment a trade could END on this screen,
      // and printing "CONEXI�N PERDIDA" over a completed exchange would be the
      // same mistake as printing "has perdido" over a desync.
      s_mode = (s_end_reason == (uint8_t)SE_DONE) ? (uint8_t)LKM_ENDED
                                                  : (uint8_t)LKM_LOST;
      release_all();
      return;
    }
    // The review became available, or the player's consent moved the trade on:
    // both change what this frame says, and neither is driven by a button.
    if (s_op == (uint8_t)LOP_TRADE && s_tl.phase != was_phase) ui_request_frame();
    // SS_VERIFY is the first state in which the BattleState exists, so it is
    // the earliest honest moment to show the fight.
    //
    // THE `s_op == LOP_BATTLE` GUARD IS LOAD-BEARING AND WAS NOT THERE BEFORE
    // P7-C4. SS_TRADE is numbered 8 - after SS_ENDING, for the reason
    // networking/session.h gives - so it satisfies `>= SS_VERIFY &&
    // < SS_CLOSED` exactly as a battle state does, and without the guard a
    // trade would push SCR_BATTLE onto a BattleState nobody ever built.
    if (s_op == (uint8_t)LOP_BATTLE &&
        session_state(s_sess) >= SS_VERIFY && session_state(s_sess) < SS_CLOSED) {
      hand_off_to_battle();
    }
    return;
  }

  if (s_mode == (uint8_t)LKM_HANDOFF) {
    // SCR_BATTLE is on top and pumping through link_battle_pump(); nothing to
    // do here. Reached only if a frame is serviced with this screen current,
    // which sm_goto() makes impossible - kept because a mode with no branch is
    // a mode nobody can reason about.
    return;
  }
}

// -----------------------------------------------------------------------------
//  THE BATTLE SCREEN HANDING THE LINK BACK
//
//  It is the battle screen that calls this and not this screen's own leave
//  hook, and the reason is app/state_machine.cpp: sm_home() (LONG_BOTH) throws
//  the whole back stack away and runs ONLY the current screen's leave, so the
//  LINK screen underneath would never hear about it. battle_leave() runs on
//  every route off SCR_BATTLE - that is the same property its one-report path
//  already depends on - so this is the hook that always fires.
// -----------------------------------------------------------------------------
void link_battle_done(void) {
  s_handoff = 0;
  if (s_live) {
    // A battle walked out of mid-round is a LOCAL CANCEL and the peer is told
    // so: session_cancel() sends GOODBYE, which is what stops the other device
    // sitting through its whole ladder for a player who has left the room.
    if (!session_closed(s_sess)) session_cancel(s_sess, s_now);
    s_end_reason = (uint8_t)session_end(s_sess).reason;
  }
  release_all();
  s_mode     = (uint8_t)LKM_ENDED;
  s_show_end = 1;
}

void link_leave(void) {
  // THE BACKSTOP, and it is what makes "the radio shuts down after use" a
  // property of the screen and not of the player's route off it: LONG_BOTH goes
  // HOME without passing through link_input(), and the auto-return could too.
  // The ONE exception is the push to SCR_BATTLE, which is not a departure.
  if (s_handoff) return;
  release_all();
  // AND THE END PAGE IS NOT KEPT FOR A LATER VISIT. s_show_end is a hand-off
  // from battle_leave() to the very next link_enter(); a player who walked past
  // this screen instead has nothing owed to them, and leaving the flag standing
  // would open the NEXT visit on a summary of a session that is long gone
  // instead of on a browse.
  s_show_end = 0;
  s_mode = (uint8_t)LKM_BROWSE;
}

// -----------------------------------------------------------------------------
//  RENDER
// -----------------------------------------------------------------------------
static bool op_offered(uint8_t op);
static bool op_offered_row(uint8_t op) { return op_offered(op); }

static const char* op_label(uint8_t op) {
  switch (op) {
    case LOP_BATTLE: return S(STR_LK_OP_BATTLE);
    case LOP_TRADE:  return S(STR_LK_OP_TRADE);
    case LOP_BREED:  return S(STR_LK_OP_BREED);
    default:         return S(STR_LK_OP_CANCEL);
  }
}

// The session's own state, in the player's words. A switch and not an indexed
// block: SessionState is networking's enum and a parallel string block would be
// a second place for the two to drift.
static const char* state_word(void) {
  if (!s_live) return S(STR_LK_BOTH_A);
  switch (session_state(s_sess)) {
    case SS_IDLE:
    case SS_HELLO:   return S(STR_LK_ST_HELLO);
    case SS_CAPS:    return S(STR_LK_ST_CAPS);
    case SS_SESSION: return S(STR_LK_ST_AGREE);
    case SS_TEAM:    return S(STR_LK_ST_TEAM);
    default:         return S(STR_LK_ST_READY);
  }
}

static void draw_browse(void) {
  static char rows[LK_LIST_ROWS][LK_ROW_CAP];
  static char vals[LK_LIST_ROWS][LK_VAL_CAP];
  const char* items[LK_LIST_ROWS];
  const char* values[LK_LIST_ROWS];

  const uint8_t n = link_qualified_count(s_job);
  uint8_t r = 0;
  for (; r < n && r < (uint8_t)LINK_PEER_CAP; ++r) {
    const DiscPeer* p = nth_qualified(r);
    // The peer name arrives as raw LATIN-1 off the air (networking/discovery.cpp
    // name_ok() admits the high bytes one at a time) and this list draws with
    // drawUTF8(), so it is transcoded here and cut on a CHARACTER boundary.
    rows[r][0] = '\0';
    if (p && p->name[0]) (void)u8_cat_latin1(rows[r], (uint16_t)LK_ROW_CAP, p->name);
    else                 (void)u8_cat(rows[r], (uint16_t)LK_ROW_CAP, S(STR_LK_SEARCH));
    // The EMA in whole dBm, which is what discovery.h keeps for a screen to
    // show. The last packet's value would jump a row under the player's thumb.
    snprintf(vals[r], LK_VAL_CAP, "%d", p ? (int)p->rssi : 0);
    items[r]  = rows[r];
    values[r] = vals[r];
  }
  snprintf(rows[r], LK_ROW_CAP, "%s", S(STR_ITEM_BACK));
  vals[r][0] = '\0';
  items[r]  = rows[r];
  values[r] = vals[r];
  const uint8_t total = (uint8_t)(r + 1u);

  char tag[8];
  snprintf(tag, sizeof tag, "%u", (unsigned)n);
  gfx_header(S(STR_LINK_TITLE), tag);
  if (n == 0u) {
    gfx_text_center(GF_BODY, (int16_t)(UI_HDR_H + 14), S(STR_LK_SEARCH));
    gfx_text_center(GF_BODY, (int16_t)(UI_HDR_H + 26), S(STR_LK_NOBODY));
  } else {
    gfx_list(items, total, s_cur, values, ui_now_ms());
  }
  gfx_countdown(ui_idle_ms());
  gfx_affordance(S(STR_AF_NEXT), S(STR_AF_BACK_SEL));
}

// SPEC SECTION 42'S CARD, AND IT IS DRAWN BY HAND RATHER THAN WITH gfx_list().
// The shared list widget starts at UI_HDR_H and spends 11 px on each of four
// rows, which is the whole 44 px content band - so a banner above it would land
// ON the first row. Four rows at the 8 px body pitch leave exactly enough room
// for the one line section 42 asks for by name, and the section 7 grammar (A
// steps, HOLD_R chooses) is unchanged: this is a layout, not a second widget.
#define LK_CARD_TOP    ((int16_t)(UI_HDR_H + 10))    // 21
#define LK_CARD_PITCH  ((int16_t)8)

static void draw_card(void) {
  const DiscPeer* p = nth_qualified(s_cur);
  char tag[8];
  snprintf(tag, sizeof tag, "%d", p ? (int)p->rssi : 0);
  char nm[PB_NAME_DRAW_CAP];
  nm[0] = '\0';
  if (p && p->name[0]) (void)u8_from_latin1(nm, (uint16_t)sizeof nm, p->name);
  gfx_header(nm[0] ? nm : S(STR_LINK_TITLE), tag);

  gfx_text_center(GF_BODY, (int16_t)(UI_HDR_H + 7), S(STR_LK_FOUND));

  for (uint8_t i = 0; i < (uint8_t)LOP_COUNT; ++i) {
    const int16_t y = (int16_t)(LK_CARD_TOP + (int16_t)i * LK_CARD_PITCH);
    gfx_text(GF_BODY, 4, (int16_t)(y + 6), op_label(i));
    // An operation the PEER cannot do is drawn and marked rather than hidden:
    // a row that vanishes is a row the player thinks they mis-remembered.
    if (!op_offered_row(i)) gfx_text_right(GF_BODY, OLED_W - 4, (int16_t)(y + 6), "-");
    if (i == s_op) gfx_invert_rect(0, y, OLED_W, (int16_t)(LK_CARD_PITCH - 1));
  }

  gfx_countdown(ui_idle_ms());
  gfx_affordance(S(STR_AF_NEXT), S(STR_AF_BACK_SEL));
}

// Both devices show the peer NAME, the OPERATION and the STATE - spec section
// 42 asks for exactly those three and this is the frame that carries them.
static void draw_wait(void) {
  gfx_header(S(STR_LK_WAIT), nullptr);
  const SpriteRef r = sprite_mini(MIC_BLE);
  gfx_xbm((int16_t)((OLED_W - r.w) / 2), (int16_t)(UI_HDR_H + 1), r.w, r.h, r.bits);
  gfx_text_center(GF_BODY, (int16_t)(UI_HDR_H + 20),
                  s_peer_name[0] ? s_peer_name : S(STR_LK_SEARCH));
  gfx_text_center(GF_BODY, (int16_t)(UI_HDR_H + 29), op_label(s_op));
  // THE TRADE'S REVIEW LIVES IN THIS FRAME AND NOT IN A SIXTH MODE. Once both
  // devices have decoded and validated both records there is exactly one thing
  // left to say - what goes and what comes - and it replaces the state word,
  // which by then says nothing the player did not already know.
  if (link_trade_wants_consent()) {
    // 9 + 3 + 9 = 21 CHARACTERS against GF_BODY's 25-character line, so two
    // long species names cannot push the line off the panel. The separator is
    // ">" and not an arrow glyph: core/strings_es.h forbids arrows outright,
    // because the _tf fonts carry ASCII + Latin-1 and nothing else.
    //
    // IT USED TO BE "%.9s", AND "%.Ns" IS A BYTE PRECISION WHILE THE COMMENT
    // ABOVE REASONS IN CHARACTERS. Five of the sixty species names are
    // multi-byte - Rafagon, Estatic, Jamron, Plagon, Gateon - so the day a
    // content edit takes one past nine BYTES the cut lands inside a sequence
    // and drawUTF8() gets a broken lead. Nothing today reaches it (the longest
    // is eight bytes) and there is no static_assert on species-name byte
    // length, which is exactly why it is fixed with a codepoint-safe append
    // rather than left as a comment that is right about the wrong unit.
    char line[LK_TRADE_LINE_CAP];
    const SpeciesDef* give = species_get(s_tl.out_rec[PBW_OFF_SPECIES]);
    const SpeciesDef* get  = species_get(s_tl.in_rec[PBW_OFF_SPECIES]);
    line[0] = '\0';
    (void)u8_cat_n(line, (uint16_t)sizeof line,
                   give ? S(give->name_idx) : "?", LK_TRADE_HALF_CHARS);
    (void)u8_cat(line, (uint16_t)sizeof line, " > ");
    (void)u8_cat_n(line, (uint16_t)sizeof line,
                   get ? S(get->name_idx) : "?", LK_TRADE_HALF_CHARS);
    gfx_text_center(GF_BODY, (int16_t)(UI_HDR_H + 38), line);
  } else {
    gfx_text_center(GF_BODY, (int16_t)(UI_HDR_H + 38), state_word());
  }
  // INVARIANT 3 APPLIES HERE AND THE BAR IS HONEST ABOUT IT: this row is not
  // SF_STICKY, so twenty silent seconds return the player to HOME. It can never
  // cut a handshake short, and the arithmetic is why rather than the intention:
  // the session's whole ladder is PROTO_RETX_MAX * PROTO_RETX_MS = 9 s, so a
  // peer that is coming has answered, and one that is not has already been
  // named CONEXIÓN PERDIDA, long before the auto-return arrives.
  gfx_countdown(ui_idle_ms());
  // A IS OFFERED ONLY WHERE IT MEANS SOMETHING. Before the review there is
  // nothing for the player to agree to and a second A would be a second consent
  // for a session that already has ours.
  gfx_affordance(link_trade_wants_consent() ? S(STR_AF_OK) : nullptr,
                 S(STR_AF_CANCEL));
}

// Spec section 47's wording, on the screen, with both ways on.
static void draw_lost(void) {
  gfx_header(S(STR_LINK_TITLE), nullptr);
  gfx_text_center(GF_NARR, (int16_t)(UI_HDR_H + 12), S(STR_LK_LOST));
  gfx_text_center(GF_BODY, (int16_t)(UI_HDR_H + 26), S(STR_LK_RETRY));
  gfx_text_center(GF_BODY, (int16_t)(UI_HDR_H + 36), S(STR_LK_EXIT));
  gfx_countdown(ui_idle_ms());
  gfx_affordance(S(STR_AF_OK), S(STR_AF_BACK));
}

static void draw_ended(void) {
  gfx_header(S(STR_LINK_TITLE), nullptr);
  gfx_text_center(GF_BODY, (int16_t)(UI_HDR_H + 14),
                  s_peer_name[0] ? s_peer_name : S(STR_LK_SEARCH));
  gfx_text_center(GF_BODY, (int16_t)(UI_HDR_H + 26),
                  (s_end_reason != (uint8_t)SE_DONE)   ? S(STR_LK_BROKEN)
                  : (s_op == (uint8_t)LOP_TRADE)       ? S(STR_TR_RESOLVED)
                                                       : S(STR_LK_ENDED));
  gfx_countdown(ui_idle_ms());
  gfx_affordance(nullptr, S(STR_AF_BACK));
}

void link_render(void) {
  switch (s_mode) {
    case LKM_CARD:    draw_card();  break;
    case LKM_WAIT:
    case LKM_HANDOFF: draw_wait();  break;
    case LKM_LOST:    draw_lost();  break;
    case LKM_ENDED:   draw_ended(); break;
    default:          draw_browse(); break;
  }
}

// -----------------------------------------------------------------------------
//  INPUT. Spec section 7: A steps, HOLD_R chooses, B walks back one level,
//  BOTH is help. SF_OWNS_BACK, because B here means CANCEL THE LINK and a link
//  holds the radio: turning B into a plain BACK would leave the screen and the
//  radio behind it.
// -----------------------------------------------------------------------------
static uint8_t ring(uint8_t cur, uint8_t n) {
  return n ? (uint8_t)((cur + 1u) % n) : 0u;
}

static void choose_peer(void) {
  const uint8_t n = link_qualified_count(s_job);
  if (s_cur >= n) { ui_back(); return; }        // the "Volver" row
  s_mode = LKM_CARD;
  gfx_list_reset();
}

// The card's rows are section 42's four, and TRADE / BREED are offered only
// where the PEER says it can do them: a menu that offers an operation the other
// device has never heard of is a menu whose only possible answer is an error
// nine seconds later.
static bool op_offered(uint8_t op) {
  const DiscPeer* p = nth_qualified(s_cur);
  switch (op) {
    case LOP_BATTLE: return p && (p->caps & (uint16_t)DISC_CAP_BATTLE) != 0u;
    case LOP_TRADE:  return p && (p->caps & (uint16_t)DISC_CAP_TRADE)  != 0u;
    case LOP_BREED:  return p && (p->caps & (uint16_t)DISC_CAP_BREED)  != 0u;
    default:         return true;
  }
}

static void choose_op(void) {
  switch (s_op) {
    case LOP_CANCEL:
      s_mode = LKM_BROWSE;
      gfx_list_reset();
      return;
    case LOP_BREED:
      // CRIAR IS STILL AN ENTRY POINT AND NOT AN OPERATION. game/breeding.cpp
      // is complete and tested, and no wire driver carries a breeding: P7-C5's
      // breed_link.cpp is not built, and the plan says so in the one place that
      // should. Nothing is opened and no radio state changes.
      ui_toast(op_offered(s_op) ? STR_UI_SOON : STR_LK_NO_CAP);
      return;
    default:
      break;
  }
  if (!op_offered(s_op)) { ui_toast(STR_LK_NO_CAP); return; }
  // CONSENT. The browse stops here and THE RADIO DOES NOT: link_hold() is what
  // separates those two, and calling link_cancel() instead would put the stack
  // down one frame after the two players agreed to use it.
  if (!open_session()) return;
  link_hold(s_job);
  s_mode = LKM_WAIT;
}

void link_input(Gesture g) {
  if (g == GST_BOTH) { ui_help(STR_LK_HELP); return; }

  switch (s_mode) {
    case LKM_BROWSE:
      switch (g) {
        case GST_TAP_L:
        case GST_HOLD_L:
          s_cur = ring(s_cur, (uint8_t)(link_qualified_count(s_job) + 1u));
          break;
        case GST_HOLD_R: choose_peer(); break;
        case GST_TAP_R:  ui_back();     break;
        default: break;
      }
      break;

    case LKM_CARD:
      switch (g) {
        case GST_TAP_L:
        case GST_HOLD_L: s_op = ring(s_op, (uint8_t)LOP_COUNT); break;
        case GST_HOLD_R: choose_op(); break;
        case GST_TAP_R:  s_mode = LKM_BROWSE; gfx_list_reset(); break;
        default: break;
      }
      break;

    case LKM_WAIT:
      // THE SECOND CONSENT OF A TRADE, and it is a different question from the
      // first. The A on the card agreed to TALK TO THIS PEER; this one agrees
      // to THESE TWO PEBBLES, which neither player could see until both records
      // were on the table. Nothing moves without it: networking/trade_link.cpp
      // applies only where it holds BOTH confirms.
      if (g == GST_HOLD_R && link_trade_wants_consent()) {
        trade_link_accept(s_sess);
        ui_request_frame();
        break;
      }
      // Otherwise the only thing to say here is "stop". A second A on a session
      // that already has our consent is not a second consent.
      if (g == GST_TAP_R || g == GST_HOLD_R) {
        release_all();
        s_end_reason = (uint8_t)SE_LOCAL_CANCEL;
        ui_toast(STR_LK_CANCELLED);
        ui_back();
      }
      break;

    case LKM_LOST:
      // Spec section 47's two answers, and they are the two the screen prints.
      if (g == GST_HOLD_R || g == GST_TAP_L || g == GST_HOLD_L) {
        release_all();
        start_browse();
      } else if (g == GST_TAP_R) {
        ui_back();
      }
      break;

    case LKM_ENDED:
      if (g == GST_TAP_R || g == GST_HOLD_R) ui_back();
      break;

    default: break;
  }
}

// -----------------------------------------------------------------------------
//  THE BOX'S ENTRY POINT
// -----------------------------------------------------------------------------
void link_arm_intent(uint8_t op, uint8_t box_slot) {
  s_armed_op   = (op < (uint8_t)LOP_CANCEL) ? op : (uint8_t)LOP_BATTLE;
  s_armed_slot = box_slot;
}

// =============================================================================
//  THE LINKED BATTLE (P7-C3)
// =============================================================================

// -----------------------------------------------------------------------------
//  THE REWARD GATE, IN ONE EXPRESSION AND IN ONE PLACE.
//
//  UI_LKB_WON is answered where and only where the SESSION authorised rewards:
//  networking/session.h's session_rewards_authorised() is true exactly when
//  networking/battle_link.cpp's compare_end() found that the peer's BATTLE_END
//  carried the SAME outcome AND the SAME final hash as ours. The local engine
//  having reached BO_WIN_A is NOT that: a desync, a lost link and a peer that
//  disagreed all leave the local engine holding an outcome, and none of them
//  may pay.
//
//  SE_DESYNC IS NOT A LOSS EITHER, and the difference is why UI_LKB_BROKEN
//  exists as a fourth answer: ui/screen_battle.cpp prints a neutral line for it
//  rather than "has perdido", because blaming the player for a radio is the
//  same class of mistake as calling BO_ABORT a draw (game/battle.h).
// -----------------------------------------------------------------------------
uint8_t link_battle_status(void) {
  if (!s_live) return (uint8_t)UI_LKB_NONE;
  if (session_rewards_authorised(s_sess)) {
    const uint8_t out = s_sess.final_outcome;
    const uint8_t mine = (s_sess.side == 0u) ? (uint8_t)BO_WIN_A : (uint8_t)BO_WIN_B;
    const uint8_t theirs = (s_sess.side == 0u) ? (uint8_t)BO_WIN_B : (uint8_t)BO_WIN_A;
    if (out == mine)   return (uint8_t)UI_LKB_WON;
    if (out == theirs) return (uint8_t)UI_LKB_LOST;
    return (uint8_t)UI_LKB_DRAW;
  }
  if (session_closed(s_sess)) return (uint8_t)UI_LKB_BROKEN;
  return (uint8_t)UI_LKB_RUNNING;
}

uint8_t link_battle_side(void) { return s_live ? s_sess.side : 0u; }

void link_battle_pump(uint32_t now_ms) {
  pump(now_ms);
  note_end();
}

bool link_battle_wants_action(void) {
  if (!s_live || session_closed(s_sess)) return false;
  return link_wants_action(s_sess);
}

void link_battle_submit(uint8_t kind, uint8_t index) {
  if (!link_battle_wants_action()) return;
  BattleAction a;
  a.kind  = kind;
  a.index = index;
  link_local_action(s_sess, a);
}

// -----------------------------------------------------------------------------
//  THE MOVE CLOCK, AND IT IS THE SESSION'S RATHER THAN THIS SCREEN'S
//
//  MEASURED AND WRITTEN DOWN RATHER THAN DISCOVERED ON A BENCH: a round that
//  neither device advances is closed by the retransmission ladder after
//  PROTO_RETX_MAX * PROTO_RETX_MS = 9,000 ms, because nothing a waiting peer
//  sends is PROGRESS (networking/session.h rule R4, and battle_link.cpp's
//  on_battle_state() counts a probe as stale). So each player really does have
//  nine seconds from the last frame that advanced the round in which to choose,
//  and a slower one is SE_LOST and paid nothing.
//
//  THIS SCREEN DOES NOT PAPER OVER IT. Substituting a move on a timeout is
//  forbidden in principle (battle_link.cpp: the two engines would disagree on
//  the substitute's legality), and stretching the ladder is a change to the
//  session's own measured tuning and not a UI chunk's to make. What the screen
//  does instead is SHOW the clock, so the deadline is visible rather than
//  arriving as a broken link. The remedy, if play says nine seconds is too
//  short, is PROTO_RETX_MAX / PROTO_RETX_MS in networking/session.h, and it is
//  written into the plan.
// -----------------------------------------------------------------------------
uint32_t link_battle_move_ms_left(uint32_t now_ms) {
  if (!s_live || session_closed(s_sess)) return 0u;
  if (s_sess.tries >= (uint8_t)PROTO_RETX_MAX) return 0u;
  const uint32_t rungs = (uint32_t)((uint8_t)PROTO_RETX_MAX - s_sess.tries);
  const int32_t  to_due = (int32_t)(s_sess.due_ms - now_ms);
  const uint32_t head = (to_due > 0) ? (uint32_t)to_due : 0u;
  return head + (rungs - 1u) * (uint32_t)PROTO_RETX_MS;
}
