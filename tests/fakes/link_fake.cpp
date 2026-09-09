// =============================================================================
//  Errata host tests - fakes/link_fake.cpp
//  See link_fake.h.
// =============================================================================
#include "link_fake.h"

#include <stdio.h>
#include <string.h>

#include "game/box.h"
#include "game/trade.h"
#include "networking/discovery.h"
#include "networking/protocol.h"
#include "game/breeding.h"
#include "networking/breed_link.h"
#include "networking/trade_link.h"
#include "networking/transport.h"
#include "core/utf8.h"
#include "ui/screen_link.h"
#include "ui/ui.h"

// -----------------------------------------------------------------------------
//  THE QUEUE THE FAKE RADIO HANDS UP
// -----------------------------------------------------------------------------
#define LF_QUEUE  64u

struct LfRx {
  uint8_t  payload[DISC_BEACON_BYTES];
  uint16_t len;
  int8_t   rssi;
  uint8_t  slot;
};

static LfRx    g_q[LF_QUEUE];
static uint8_t g_head = 0, g_count = 0;

static bool    g_start_ok  = true;
static bool    g_poll_fail = false;
static bool    g_bind_ok   = true;   // ui_link_bind(): espnow_bind refuses four ways
static bool    g_beacon_ok = true;   // driver beacon(): espnow_broadcast refuses four ways
static int     g_starts = 0, g_stops = 0, g_tx = 0;
static int     g_binds = 0, g_unbinds = 0;
static uint8_t g_slot = 0xFFu;
static bool    g_bound = false;
static const Transport* g_tp = nullptr;
static uint32_t g_nonce = 0x1234ABCDu;
static char     g_name[NAME_MAX_LEN + 1] = "BOLOTA";

void lf_trade_reset(void);

void lf_reset(void) {
  memset(g_q, 0, sizeof g_q);
  g_head = g_count = 0;
  g_start_ok = true;
  g_poll_fail = false;
  g_bind_ok = true;
  g_beacon_ok = true;
  g_starts = g_stops = g_tx = 0;
  g_binds = g_unbinds = 0;
  g_slot = 0xFFu;
  g_bound = false;
  g_tp = nullptr;
  g_nonce = 0x1234ABCDu;
  snprintf(g_name, sizeof g_name, "BOLOTA");
  lf_trade_reset();
}

void lf_set_start_ok(bool ok)   { g_start_ok = ok; }
void lf_set_poll_fail(bool f)   { g_poll_fail = f; }
void lf_set_bind_ok(bool ok)    { g_bind_ok = ok; }
void lf_set_beacon_ok(bool ok)  { g_beacon_ok = ok; }
void lf_set_nonce(uint32_t n)   { g_nonce = n; }
void lf_set_pet_name(const char* name) { snprintf(g_name, sizeof g_name, "%s", name); }

int     lf_starts(void)     { return g_starts; }
int     lf_stops(void)      { return g_stops; }
int     lf_beacons_tx(void) { return g_tx; }
int     lf_binds(void)      { return g_binds; }
int     lf_unbinds(void)    { return g_unbinds; }
uint8_t lf_bound_slot(void) { return g_slot; }
bool    lf_bound(void)      { return g_bound; }
void    lf_bind_transport(const Transport* t) { g_tp = t; }

static void push(const uint8_t* bytes, uint16_t len, int8_t rssi, uint8_t slot) {
  if (g_count >= (uint8_t)LF_QUEUE) return;
  const uint8_t i = (uint8_t)((g_head + g_count) % (uint8_t)LF_QUEUE);
  memset(&g_q[i], 0, sizeof g_q[i]);
  const uint16_t n = (len <= (uint16_t)DISC_BEACON_BYTES) ? len : (uint16_t)DISC_BEACON_BYTES;
  memcpy(g_q[i].payload, bytes, n);
  g_q[i].len  = len;
  g_q[i].rssi = rssi;
  g_q[i].slot = slot;
  ++g_count;
}

void lf_push_beacon(uint32_t device_id, const char* name, uint16_t caps,
                    int8_t rssi, uint8_t slot) {
  DiscBeacon b;
  memset(&b, 0, sizeof b);
  b.device_id = device_id;
  b.caps      = caps;
  b.ver       = (uint8_t)PROTOCOL_VERSION;
  snprintf(b.name, sizeof b.name, "%s", name ? name : "");
  uint8_t  frame[DISC_BEACON_BYTES];
  uint16_t n = 0;
  if (disc_encode(b, frame, (uint16_t)sizeof frame, n) != DE_OK) return;
  push(frame, n, rssi, slot);
}

void lf_push_beacons(uint32_t device_id, const char* name, uint16_t caps,
                     int8_t rssi, uint8_t slot, uint8_t n) {
  for (uint8_t i = 0; i < n; ++i)
    lf_push_beacon(device_id, name, caps, rssi, slot);
}

void lf_push_raw(const uint8_t* bytes, uint16_t len, int8_t rssi, uint8_t slot) {
  push(bytes, len, rssi, slot);
}

// -----------------------------------------------------------------------------
//  THE FOUR-CALL DRIVER networking/discovery.h takes
// -----------------------------------------------------------------------------
static bool fk_start(void) { ++g_starts; return g_start_ok; }

static bool fk_beacon(const uint8_t* frame, uint16_t n) {
  (void)frame;
  (void)n;
  if (!g_beacon_ok) return false;
  ++g_tx;
  return true;
}

static int8_t fk_poll(DiscRx* out) {
  if (out == nullptr) return (int8_t)LINK_POLL_FAILED;
  if (g_poll_fail)    return (int8_t)LINK_POLL_FAILED;
  if (g_count == 0u)  return 0;
  const LfRx& r = g_q[g_head];
  memcpy(out->payload, r.payload, sizeof out->payload);
  out->len  = r.len;
  out->rssi = r.rssi;
  out->slot = r.slot;
  g_head = (uint8_t)((g_head + 1u) % (uint8_t)LF_QUEUE);
  --g_count;
  return 1;
}

static void fk_stop(void) { ++g_stops; }

static const LinkRadioDriver g_drv = { fk_start, fk_beacon, fk_poll, fk_stop };

// -----------------------------------------------------------------------------
//  A TRANSPORT THAT REFUSES, for a test that has bound none. It is a DEFINED
//  refusal - transport.h's own send() and recv() wrappers already answer false
//  and 0 for a null function pointer - and not a crash, which is what a screen
//  handed no radio has to survive.
// -----------------------------------------------------------------------------
static bool     dead_send(void*, const uint8_t*, uint16_t) { return false; }
static uint16_t dead_recv(void*, uint8_t*, uint16_t)       { return 0u; }
static Transport g_dead = { nullptr, dead_send, dead_recv, (uint16_t)PROTO_FRAME_MAX };

// -----------------------------------------------------------------------------
//  THE ui.h SEAMS
// -----------------------------------------------------------------------------
const LinkRadioDriver& ui_link_driver(void) { return g_drv; }

const Transport& ui_link_transport(void) { return g_tp ? *g_tp : g_dead; }

bool ui_link_bind(uint8_t slot) {
  // THE FOURTH FAULT INJECTOR, ADDED AT THE FINAL REVIEW. This answered true
  // unconditionally, for any slot including 0xFF, while the shipping
  // espnow_bind() refuses on FOUR separate conditions - !s_up, slot >=
  // LINK_PEER_CAP, no address in the ESP-NOW slot table, and esp_now_add_peer()
  // returning anything but OK/EXIST. So ui/screen_link.cpp's
  // `if (!ui_link_bind(p->slot)) { ui_toast(STR_LK_RADIO_ERR); return false; }`
  // was DEAD CODE in all 59 binaries: measured by deleting the whole refusal
  // arm, which survived 122,581 checks across the three binaries that link
  // screen_link.o. The fake was already a declared fault injector for start()
  // and poll(); this is the same legitimate shape for the third driver call.
  ++g_binds;
  if (!g_bind_ok) return false;
  g_slot  = slot;
  g_bound = true;
  return true;
}

void ui_link_unbind(void) {
  if (g_bound) ++g_unbinds;
  g_bound = false;
  g_slot  = 0xFFu;
}

uint32_t ui_link_nonce(void) { return g_nonce; }

// THE TWO HALVES OF THE NAME SEAM, MATCHING ui/ui.cpp EXACTLY (P10-C6).
//
// This fake stood in for ui_pet_name() with a plain snprintf - the PRE-P10-C4
// body - long after the shipping one started emitting UTF-8. Every discovery
// and link test therefore drove a function the firmware no longer had, and the
// live defect it hid (an accented device name is refused by disc_encode() and
// the board emits no beacon at all) was invisible to all 58 binaries.
//
// g_name holds the STORED form, Latin-1, the way Config.pet_name does.
void ui_pet_name_latin1(char* out, size_t cap) {
  if (out == nullptr || cap == 0u) return;
  snprintf(out, cap, "%s", g_name);
}

void ui_pet_name(char* out, size_t cap) {
  if (out == nullptr || cap == 0u) return;
  (void)u8_from_latin1(out, (uint16_t)((cap > 0xFFFFu) ? 0xFFFFu : cap), g_name);
}

// The battle screen's six, forwarded to the real ui/screen_link.cpp. Both
// binaries that use this fake link that screen, so these are the REAL session
// and never a stub of one - which is the whole point: the reward gate under
// test is link_battle_status().
uint8_t  ui_link_battle_status(void)          { return link_battle_status(); }
uint8_t  ui_link_battle_side(void)            { return link_battle_side(); }
void     ui_link_battle_pump(uint32_t now_ms) { link_battle_pump(now_ms); }
bool     ui_link_battle_wants_action(void)    { return link_battle_wants_action(); }
void     ui_link_battle_submit(uint8_t k, uint8_t i) { link_battle_submit(k, i); }
uint32_t ui_link_battle_move_ms_left(uint32_t now_ms) {
  return link_battle_move_ms_left(now_ms);
}
void     ui_link_battle_done(void)            { link_battle_done(); }

// -----------------------------------------------------------------------------
//  THE TRADE'S SEAMS. See link_fake.h for exactly what is fake here (the flash,
//  and only the flash).
// -----------------------------------------------------------------------------
static PendingTrade g_journal;
static uint16_t     g_quarantine = 0;
static int          g_commits    = 0;
static int          g_aborts     = 0;

static bool lf_store_journal(void* ctx, const PendingTrade& t)
{
  (void)ctx; g_journal = t; return true;
}
static bool lf_store_slot(void* ctx, uint8_t slot) { (void)ctx; (void)slot; return true; }
static bool lf_store_box(void* ctx)                { (void)ctx; return true; }
static void lf_store_ckpt(void* ctx)               { (void)ctx; }

static bool lf_journal_sent(void* ctx, uint32_t out_id, uint32_t peer_id)
{
  (void)ctx;
  trade_journal_sent(g_journal, out_id, peer_id);
  return true;
}
static uint8_t lf_judge(void* ctx, const uint8_t rec[48])
{
  (void)ctx;
  BugInstance in;
  if (pbw_decode(rec, in) != VR_OK) return (uint8_t)TDR_PEER_INVALID;
  const BugInstance* mine = box_peek(link_trade_slot());
  if (mine == nullptr) return (uint8_t)TDR_NO_SLOT;
  return (uint8_t)trade_accept_check(*mine, in, 0xA0A0A0A0u, link_trade_peer_id());
}
static bool lf_journal_received(void* ctx, const uint8_t rec[48])
{
  (void)ctx;
  trade_journal_received(g_journal, rec);
  return true;
}
static bool lf_commit(void* ctx)
{
  (void)ctx;
  TradeStore st;
  st.write_journal = &lf_store_journal;
  st.write_slot    = &lf_store_slot;
  st.write_box     = &lf_store_box;
  st.checkpoint    = &lf_store_ckpt;
  st.ctx           = nullptr;
  PendingTrade t = g_journal;
  const TradeReject r = trade_execute(t, trade_wire_codec(), st, 1700000000u);
  g_journal = t;
  if (r == TDR_OK) ++g_commits;
  return r == TDR_OK;
}
static void lf_abort(void* ctx)
{
  (void)ctx; ++g_aborts; trade_journal_idle(g_journal);
}

static const TradeHooks g_trade_hooks = {
  &lf_journal_sent, &lf_judge, &lf_journal_received, &lf_commit, &lf_abort, nullptr
};

const TradeHooks* ui_trade_hooks(void)  { return &g_trade_hooks; }

// THE BREEDING'S TWO, AND THE COMMIT IS THE REAL ONE. game/breeding.cpp's
// breed_commit() writes into whatever Box box_bind() bound, which in a screen
// test is the test's own - so a case that drives CRIAR to the end really does
// gain a Bug, and "the child reached the Box" is checkable rather than assumed.
// Only the FLUSH is faked, because a screen binary has no NVS.
static uint32_t g_breed_commits = 0;
static uint32_t g_breed_saves   = 0;
static uint8_t  g_breed_force   = (uint8_t)BRD_OK;

static uint8_t lf_breed_commit(void* ctx, const BreedPlan& plan, uint8_t& slot_out)
{
  (void)ctx;
  if (g_breed_force != (uint8_t)BRD_OK) return g_breed_force;
  const uint8_t r = (uint8_t)breed_commit(plan, 1700000000u, slot_out);
  if (r == (uint8_t)BRD_OK) ++g_breed_commits;
  return r;
}
static bool lf_breed_save(void* ctx)  { (void)ctx; ++g_breed_saves; return true; }
static void lf_breed_abort(void* ctx) { (void)ctx; }

static const BreedHooks g_breed_hooks = {
  &lf_breed_commit, &lf_breed_save, &lf_breed_abort, nullptr
};

const BreedHooks* ui_breed_hooks(void)  { return &g_breed_hooks; }

uint32_t lf_breed_commits(void) { return g_breed_commits; }
uint32_t lf_breed_saves(void)   { return g_breed_saves; }
void     lf_breed_force(uint8_t r) { g_breed_force = r; }
void     lf_breed_reset(void) {
  g_breed_commits = 0; g_breed_saves = 0; g_breed_force = (uint8_t)BRD_OK;
}
uint16_t ui_trade_quarantine(void)      { return g_quarantine; }

void lf_trade_reset(void)
{
  trade_journal_idle(g_journal);
  g_quarantine = 0;
  g_commits = 0;
  g_aborts  = 0;
}

void     lf_set_quarantine(uint16_t m)  { g_quarantine = m; }
uint8_t  lf_trade_phase(void)           { return g_journal.phase; }
int      lf_trade_commits(void)         { return g_commits; }
int      lf_trade_aborts(void)          { return g_aborts; }
uint32_t lf_trade_out_id(void)          { return g_journal.out_id; }
