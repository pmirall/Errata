// =============================================================================
//  webui.cpp - the HTTP server the phone talks to.
//
//  Every route below was written against the actual fetch calls inside
//  index_html.h (which is frozen, generated, and 46,663 B). The four places the
//  client and the server MUST agree byte for byte:
//
//   1. /api/state field names. The client reads
//        v t name stage gen age st{hun,hyg,nrg,hap,hea} mood sick poop asleep
//        spr{id,rev} wx{c,t} cd{a,"1","2","3"} busy ip
//      `mood` and `wx.c` are emitted as NUMBERS: the client carries both a
//      numeric table (st[] / ht[]) and a string table (ot[] / rt[]) and picks
//      by typeof. The numeric tables are 1:1 with enum Mood and enum
//      WeatherGroup - ht[] is exactly WX_UNKNOWN..WX_STORM_HAIL in order - so
//      the ordinal is both smaller on the wire and impossible to mistype.
//
//   2. The sprite header is SIX bytes, not seven. The client's bit accessor is
//        m(t,frame,x,y,stride,frameBytes) =
//            t[6 + frame*frameBytes + y*stride + (x>>3)] >> (x&7) & 1
//      -> data starts at offset 6, rows are LSB-first, frame-major. It also
//      checks t[0] === 83 ('S') and bails when length < 6 + N*frameBytes.
//
//   3. The PIN rides as "&k=NNNN" appended to the query string, never a header.
//
//   4. 403 is the ONLY code that opens the client's PIN modal, and it must be
//      403 on the mutating routes specifically - the client deletes its
//      optimistic cooldown and re-prompts.
//
//  BANNED HERE, ON PURPOSE (BRIEF risk 6):
//    - the 3-arg send_P  (WebServer.cpp:619 strlen_P()s the blob)
//    - the 1-arg sendContent_P (WebServer.cpp:663, same bug) - sprite bytes are
//      mostly 0x00, so either would truncate the body at the first row of
//      empty pixels.
//    - String concatenation anywhere on a per-request path. Every response is
//      snprintf'd into a fixed static buffer.
//    - floating point. Weather arrives as deci-degrees and leaves as integer
//      degrees via integer division with explicit negative-half rounding.
//
//  index_html.h is included from THIS TRANSLATION UNIT AND NO OTHER: both
//  INDEX_HTML and INDEX_HTML_LEN have internal linkage, so a second includer
//  would put a second 46 KB copy in .rodata.
// =============================================================================

#include <Arduino.h>
#include <WebServer.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "config.h"
#include "nt_types.h"
#include "webui.h"
#include "sim.h"
#include "sprites.h"
#include "render.h"
#include "net.h"
#include "storage.h"
#include "weather.h"
#include "genome.h"
#include "rng.h"
#include "telegram.h"     // tg_set_mode() when POST /api/cfg changes tg
#include "index_html.h"   // EXACTLY ONE TU. See the file header.

#if FEATURE_WEB

// =============================================================================
//  1. STATE
// =============================================================================

static WebServer s_srv(WEB_PORT);

static bool     s_running        = false;
static bool     s_routes_done    = false;
static uint16_t s_port           = WEB_PORT;
// The port web_begin() was last asked for. S9 can switch web access off and
// back on at any time and the entry point calls web_begin() exactly once, so
// web_service() needs to know what to re-open on the rising edge.
static uint16_t s_want_port      = WEB_PORT;

static uint16_t s_pin            = 0xFFFFu;   // 0xFFFF == not rolled yet
static uint32_t s_last_client_ms = 0;
static uint32_t s_served         = 0;
static uint32_t s_rejected       = 0;

// Token bucket, held in MILLI-tokens so the refill never needs a float and
// never truncates to zero on a fast poll. Capacity WEB_RATE_TOKENS * 1000
// (10,000) fits uint16_t with room to spare - see the static_assert in webui.h.
static uint16_t s_bucket_mtok    = (uint16_t)(WEB_RATE_TOKENS * 1000);
static uint32_t s_bucket_ms      = 0;

// The single global minigame slot.
static uint32_t s_game_tok       = 0;   // 0 == empty
static uint32_t s_game_start_ms  = 0;
static uint16_t s_game_dur_s     = 0;
static uint8_t  s_game_id        = MG_NONE;

// Config: either borrowed from the entry point or privately owned.
static Config*  s_cfg            = nullptr;
static Config   s_cfg_local;
static bool     s_cfg_dirty      = false;

// One slot, latest wins, drained by the entry point once per loop(). A PetSave
// is 128 bytes; keeping a whole one rather than the three fields the
// choreographies happen to need today is deliberate, because the next
// choreography that needs a fourth should not have to change this file.
static uint8_t  s_act_pending    = ACT_NONE;
static PetSave  s_act_before;
static bool     s_cfg_apply_net  = false;   // deferred: credentials changed

// Response scratch. BRIEF 1.6: "static char[320], never String +=".
static char     s_json[WEB_JSON_BUF];
// /api/cfg carries free-form user text (SSID 32 + TZ 39 + name 12 + two
// coordinates) and cannot share the 320 B state buffer.
static char     s_cfgjson[448];

// =============================================================================
//  2. SMALL HELPERS  (no floating point, no String, no dynamic allocation)
// =============================================================================

// Copies src into dst[cap] as a JSON string BODY (no surrounding quotes),
// truncated to `maxout` output bytes. Characters that would need escaping are
// replaced rather than escaped, so the output length is exactly the input
// length: that keeps every response size provable at compile time instead of
// doubling in the worst case. An incomplete trailing UTF-8 sequence is dropped
// so the browser's JSON.parse() never sees a broken code point.
static void json_str_sanitised(char* dst, size_t cap, const char* src, size_t maxout)
{
  if (!cap) return;
  if (!src) { dst[0] = '\0'; return; }

  size_t limit = (cap - 1u < maxout) ? (cap - 1u) : maxout;
  size_t n = 0;
  while (src[n] && n < limit) {
    unsigned char c = (unsigned char)src[n];
    if (c < 0x20u || c == '"' || c == '\\' || c == 0x7Fu) dst[n] = '.';
    else                                                  dst[n] = (char)c;
    n++;
  }
  // Drop a truncated multi-byte sequence: walk back over continuation bytes to
  // the lead byte and check whether its full length still fits.
  if (n > 0 && (unsigned char)dst[n - 1] >= 0x80u) {
    size_t k = n;
    while (k > 0 && ((unsigned char)dst[k - 1] & 0xC0u) == 0x80u) k--;
    if (k > 0) {
      unsigned char lead = (unsigned char)dst[k - 1];
      size_t need = (lead >= 0xF0u) ? 4u : (lead >= 0xE0u) ? 3u : (lead >= 0xC0u) ? 2u : 1u;
      if (k - 1u + need > n) n = k - 1u;
    }
  }
  dst[n] = '\0';
}

// deci-celsius -> celsius, integer, rounded to nearest, away from zero.
static int16_t dc_to_c(int16_t dc)
{
  int32_t v = (dc >= 0) ? ((int32_t)dc + 5) / 10 : ((int32_t)dc - 5) / 10;
  return (int16_t)NT_CLAMP(v, -99, 99);
}

// Non-negative decimal parse with an explicit ceiling. Returns `def` when the
// string is empty or holds anything that is not a digit, and saturates at `hi`.
//
// The accumulator is 64-bit ON PURPOSE. The 32-bit form ("v = v*10 + d; if
// (v > hi) return hi;") cannot detect an overflow when hi == 0xFFFFFFFF, which
// is exactly how pin_ok() calls this: the multiply wrapped mod 2^32 and every
// decimal congruent to the PIN then parsed to the PIN itself (PIN 3821 was also
// produced by "4294971117"). Widening the intermediate is the whole fix; the
// result is bit-identical to the old function for every input that did not wrap
// (verified exhaustively for hi = 0xFF and hi = 0x100, the only other ceilings
// used here). This is a per-request path, not a stat path - no float, and the
// 64-bit multiply is a handful of instructions on a query string of <= 10 digits.
static uint32_t arg_u32(const char* s, uint32_t def, uint32_t hi)
{
  if (!s || !*s) return def;
  uint32_t v = 0;
  for (const char* p = s; *p; ++p) {
    if (*p < '0' || *p > '9') return def;
    const uint64_t t = (uint64_t)v * 10u + (uint64_t)(*p - '0');
    if (t > (uint64_t)hi) return hi;
    v = (uint32_t)t;
  }
  return v;
}

// Exactly 8 lower-case hex digits -> uint32_t. Returns 0 on any deviation,
// which is also the "no token" sentinel, so a malformed token can never match.
static uint32_t parse_tok8(const char* s)
{
  if (!s) return 0;
  uint32_t v = 0;
  uint8_t  n = 0;
  for (; s[n]; ++n) {
    if (n >= 8) return 0;
    char c = s[n];
    uint8_t d;
    if      (c >= '0' && c <= '9') d = (uint8_t)(c - '0');
    else if (c >= 'a' && c <= 'f') d = (uint8_t)(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') d = (uint8_t)(c - 'A' + 10);
    else return 0;
    v = (v << 4) | d;
  }
  return (n == 8) ? v : 0;
}

static uint16_t mg_duration_s(uint8_t id)
{
  switch (id) {
    case MG_SNACK_RUSH:   return MG1_DURATION_S;
    case MG_BUBBLE_SCRUB: return MG2_DURATION_S;
    case MG_LULLABY:      return MG3_DURATION_S;
    default:              return 0;
  }
}

static uint16_t mg_score_max(uint8_t id)
{
  switch (id) {
    case MG_SNACK_RUSH:   return MG1_SCORE_MAX;
    case MG_BUBBLE_SCRUB: return MG2_SCORE_MAX;
    case MG_LULLABY:      return MG3_SCORE_MAX;
    default:              return 0;
  }
}

// =============================================================================
//  3. CONFIG ACCESS
// =============================================================================

static Config* cfg_rw(void) { return s_cfg ? s_cfg : &s_cfg_local; }

const Config* web_cfg(void)      { return cfg_rw(); }
bool  web_cfg_dirty(void)        { return s_cfg_dirty; }
void  web_cfg_clear_dirty(void)  { s_cfg_dirty = false; }

uint8_t web_take_action(PetSave* before)
{
  const uint8_t a = s_act_pending;
  if (a != ACT_NONE && before) *before = s_act_before;
  s_act_pending = ACT_NONE;
  return a;
}

void web_bind_config(Config* cfg)
{
  s_cfg = cfg;
}

// Does the user still want the HTTP server? CF_WEB_ENABLED is the S9 "WEB"
// toggle. It used to feed only wifi_wanted() in the entry point, so with the
// radio kept up for weather or Telegram the listening socket survived the
// toggle and /api/action and /api/cfg went on serving after the user had
// switched web access off (PH3 finding 7).
//
// An UNBOUND module (webui.h "unbound" mode, no entry point) has no user
// preference to consult and must keep working exactly as before.
static bool web_enabled(void)
{
  return (s_cfg == nullptr) || ((s_cfg->flags & CF_WEB_ENABLED) != 0);
}

// =============================================================================
//  4. RATE LIMITER
//    10 tokens, refill 4/s, 1 per read, 2 per mutation, 429 with an EMPTY body
//    when the bucket is dry (BRIEF 6.3). The empty body is deliberate: the
//    client's 429 branch reads t.j && t.j.s, gets null, and falls back to its
//    own 5 s guess - which is exactly the right behaviour for "you are hammering
//    me", as opposed to the cooldown 429 that DOES carry {"err":"cool","s":N}.
// =============================================================================
static bool rate_take(uint8_t cost_tokens)
{
  const uint32_t now = millis();
  uint32_t dt = now - s_bucket_ms;          // unsigned: wrap-safe
  if (dt > 5000u) dt = 5000u;               // also caps the multiply below
  if (dt) {
    s_bucket_ms = now;
    uint32_t t = (uint32_t)s_bucket_mtok + dt * (uint32_t)WEB_RATE_REFILL_PER_S;
    const uint32_t cap = (uint32_t)WEB_RATE_TOKENS * 1000u;
    s_bucket_mtok = (uint16_t)((t > cap) ? cap : t);
  }
  const uint16_t need = (uint16_t)(cost_tokens * 1000u);
  if (s_bucket_mtok < need) return false;
  s_bucket_mtok = (uint16_t)(s_bucket_mtok - need);
  return true;
}

// =============================================================================
//  5. RESPONSE PRIMITIVES
// =============================================================================

static void note_request(void)
{
  s_last_client_ms = millis();
  if (s_last_client_ms == 0) s_last_client_ms = 1;   // 0 means "never"
  rd_note_web_activity();                            // render drops to FPS_LOW
}

static void send_json(int code, const char* body)
{
  s_srv.sendHeader(F("Cache-Control"), F("no-store"));
  s_srv.send(code, "application/json", body);
  if (code >= 400) s_rejected++; else s_served++;
}

// {"err":"xxx"} - the client looks the code up in its own Spanish table.
static void send_err(int code, const char* err)
{
  snprintf(s_json, sizeof(s_json), "{\"err\":\"%s\"}", err);
  send_json(code, s_json);
}

// 429 {"err":"cool","s":N}
static void send_cool(uint16_t secs)
{
  snprintf(s_json, sizeof(s_json), "{\"err\":\"cool\",\"s\":%u}", (unsigned)secs);
  send_json(429, s_json);
}

// 429, empty body. Rate limiter only.
static void send_throttled(void)
{
  s_srv.send(429, "application/json", "");
  s_rejected++;
}

// ActionErr -> (HTTP status, error token). The tokens are the keys of the
// client's `nt` table; anything it does not know falls back to a generic
// "No se ha podido." line, which is honest rather than silent.
static void send_action_err(const ActionResult& r)
{
  switch (r.err) {
    case AERR_COOLDOWN:     send_cool(r.cooldown_s);            return;
    // Sulking after an absence is a timed refusal, so it reads as a cooldown
    // on the phone; the client then greys the button for exactly that long.
    case AERR_SULKING:      send_cool(r.cooldown_s ? r.cooldown_s : 1); return;
    case AERR_FULL:         send_err(409, "full");              return;
    case AERR_TIRED:        send_err(409, "tired");             return;
    case AERR_NOT_SICK:     send_err(409, "nosick");            return;
    case AERR_NOTHING_TODO: send_err(409, "clean");             return;
    case AERR_ASLEEP:       send_err(409, "asleep");            return;
    case AERR_DEAD:         send_err(409, "dead");              return;
    case AERR_IS_EGG:       send_err(409, "egg");               return;
    case AERR_REFUSED:      send_err(409, "refuse");            return;
    case AERR_BAD_ARG:      send_err(400, "arg");               return;
    default:                send_err(409, "arg");               return;
  }
}

// The PIN gate. Returns true when the request may proceed; otherwise it has
// already answered 403 {"err":"pin"}, which is the only thing that makes the
// client show its PIN modal.
static bool pin_ok(void)
{
  if (!s_srv.hasArg("k")) { send_err(403, "pin"); return false; }
  const String k = s_srv.arg("k");
  // web_pin() is 0..9999, so the sentinel 0xFFFFFFFF can never equal a real PIN:
  // a non-numeric "k" returns `def` and an over-long one saturates at `hi`, and
  // both are that same unmatchable value. This is only true because arg_u32()
  // now accumulates in 64 bits - with the old 32-bit accumulator "k" wrapped mod
  // 2^32 and any alias of the PIN (3821 <- 4294971117) authenticated.
  const uint32_t v = arg_u32(k.c_str(), 0xFFFFFFFFu, 0xFFFFFFFFu);
  if (v != (uint32_t)web_pin()) { send_err(403, "pin"); return false; }
  return true;
}

// =============================================================================
//  6. SPRITE / MOOD POLICY  (shared with ui so both panels agree)
// =============================================================================

uint8_t web_pose_of(const PetSave& p)
{
  if (p.stage >= STAGE_DEAD)   return POSE_GHOST;
  if (p.flags & PF_ASLEEP)     return POSE_SLEEP;
  if (p.flags & PF_SICK)       return POSE_SICK;
  return POSE_IDLE;
}

uint8_t web_sprite_id(const PetSave& p)
{
  const Stage st = (p.stage < (uint8_t)STAGE_COUNT) ? (Stage)p.stage : STAGE_EGG;
  // sprite_form_of() is the ONLY correct source of `form` - adult_form is
  // FORM_UNSET (0xFF) for every CHILD and TEEN (sprites.h:1267-1288).
  return sprite_set_id(gene_species(p.genome), (uint8_t)st,
                       sprite_form_of(p, st), web_pose_of(p));
}

uint8_t web_mood_index(uint8_t score)
{
  if (score <= 15) return MOOD_MISERIA;
  if (score <= 35) return MOOD_TRISTE;
  if (score <= 55) return MOOD_NEUTRO;
  if (score <= 75) return MOOD_CONTENTO;
  if (score <= 90) return MOOD_FELIZ;
  return MOOD_EUFORICO;
}

// =============================================================================
//  7. /api/state
//
//  Worst case, measured field by field against the widest legal value of every
//  member (t 7 digits, age 10 digits, name 12 sanitised bytes, every cooldown
//  clamped to 999, wx.t clamped to -99..99, ip 15 chars): 300 bytes. It still
//  cannot overrun, because snprintf returns the length it WANTED and the
//  builder falls back to a shorter form rather than shipping a truncated -
//  and therefore unparseable - JSON document.
// =============================================================================

static bool game_slot_live(void);
static uint16_t game_slot_left_s(void);

static void build_state(char* buf, size_t cap)
{
  const PetSave* p = sim_save();
  const Config*  c = cfg_rw();

  char name[NAME_MAX_LEN * 2 + 1];
  json_str_sanitised(name, sizeof(name), c ? c->pet_name : "", NAME_MAX_LEN);

  const uint32_t up_s  = millis() / 1000u;

  uint8_t  stage  = STAGE_EGG;
  uint8_t  gen    = 0;
  uint32_t age_s  = 0;
  uint8_t  hun = 0, hyg = 0, nrg = 0, hap = 0, hea = 0;
  uint8_t  mood = MOOD_NEUTRO;
  uint8_t  sick = 0, asleep = 0, poop = 0;
  uint8_t  sid = SPR_EGG_IDLE, sw = 0, sh = 0, sn = 1;

  if (p) {
    stage  = (uint8_t)NT_MIN(p->stage, (uint8_t)(STAGE_COUNT - 1));
    gen    = p->genome.generation;
    age_s  = sim_age_s();
    hun    = sim_stat_pct(ST_HUNGER);
    hyg    = sim_stat_pct(ST_HYGIENE);
    nrg    = sim_stat_pct(ST_ENERGY);
    hap    = sim_stat_pct(ST_HAPPINESS);
    hea    = sim_stat_pct(ST_HEALTH);
    mood   = web_mood_index(sim_mood_score());
    sick   = sim_is_sick() ? 1u : 0u;
    asleep = sim_is_asleep() ? 1u : 0u;
    poop   = (uint8_t)NT_MIN(p->poop_count, (uint8_t)POOP_MAX);

    sid = web_sprite_id(*p);
    const SpriteSet set = sprite_set(sid);
    sw = set.w; sh = set.h; sn = set.frames;
  }

  // Weather. WeatherState stores DECI-units (nt_types.h:606) - integer maths
  // only, and a stale/never-fetched reading collapses to WX_UNKNOWN, which is
  // index 0 in the browser's own table ("Sin datos").
  uint8_t  wxc = WX_UNKNOWN;
  int16_t  wxt = 0;
  {
    const WeatherState& w = wx_state();
    if (w.valid) {
      wxc = (uint8_t)NT_MIN(w.group, (uint8_t)(WX_COUNT - 1));
      wxt = dc_to_c(w.temp_dc);
    }
  }

  // Cooldowns. `a` is the floor the client puts under EVERY action button, so
  // it must be the SMALLEST of the six - reporting the largest would grey out
  // buttons that are actually ready. Anything the client lets through that the
  // server still refuses comes back as a 429 carrying the real number.
  uint16_t cd_a = 0xFFFFu;
  {
    static const uint8_t kActs[6] = {
      ACT_FEED_MEAL, ACT_FEED_SNACK, ACT_CLEAN,
      ACT_SLEEP_TOGGLE, ACT_PLAY, ACT_MEDICINE
    };
    for (uint8_t i = 0; i < 6; ++i) {
      const uint16_t cd = sim_action_cooldown_s((ActionId)kActs[i]);
      if (cd < cd_a) cd_a = cd;
    }
    if (cd_a == 0xFFFFu) cd_a = 0;
    if (cd_a > 999u) cd_a = 999u;
  }
  // One shared minigame ledger (sim.h:153) - the same number for all three.
  uint16_t cd_g = sim_minigame_cooldown_s();
  if (cd_g > 999u) cd_g = 999u;

  const uint8_t busy = game_slot_live() ? 1u : 0u;
  const char*   ip   = net_ip();

  int n = snprintf(buf, cap,
    "{\"v\":%u,\"t\":%lu,\"name\":\"%s\",\"stage\":%u,\"gen\":%u,\"age\":%lu,"
    "\"st\":{\"hun\":%u,\"hyg\":%u,\"nrg\":%u,\"hap\":%u,\"hea\":%u},"
    "\"mood\":%u,\"sick\":%u,\"poop\":%u,\"asleep\":%u,"
    "\"spr\":{\"id\":%u,\"w\":%u,\"h\":%u,\"n\":%u,\"rev\":%u},"
    "\"wx\":{\"c\":%u,\"t\":%d},"
    "\"cd\":{\"a\":%u,\"1\":%u,\"2\":%u,\"3\":%u},"
    "\"busy\":%u,\"ip\":\"%s\"}",
    (unsigned)WEB_API_SCHEMA_VER, (unsigned long)up_s, name,
    (unsigned)stage, (unsigned)gen, (unsigned long)age_s,
    (unsigned)hun, (unsigned)hyg, (unsigned)nrg, (unsigned)hap, (unsigned)hea,
    (unsigned)mood, (unsigned)sick, (unsigned)poop, (unsigned)asleep,
    (unsigned)sid, (unsigned)sw, (unsigned)sh, (unsigned)sn, (unsigned)SPRITE_REV,
    (unsigned)wxc, (int)wxt,
    (unsigned)cd_a, (unsigned)cd_g, (unsigned)cd_g, (unsigned)cd_g,
    (unsigned)busy, ip ? ip : "0.0.0.0");

  if (n < 0 || (size_t)n >= cap) {
    // Unreachable with the clamps above, but a truncated JSON body is a page
    // that never updates again. Drop the one optional field and retry; the
    // client falls back to location.hostname for the IP.
    snprintf(buf, cap,
      "{\"v\":%u,\"t\":%lu,\"stage\":%u,\"gen\":%u,\"age\":%lu,"
      "\"st\":{\"hun\":%u,\"hyg\":%u,\"nrg\":%u,\"hap\":%u,\"hea\":%u},"
      "\"mood\":%u,\"sick\":%u,\"poop\":%u,\"asleep\":%u,"
      "\"spr\":{\"id\":%u,\"w\":%u,\"h\":%u,\"n\":%u,\"rev\":%u},"
      "\"wx\":{\"c\":%u,\"t\":%d},"
      "\"cd\":{\"a\":%u,\"1\":%u,\"2\":%u,\"3\":%u},\"busy\":%u}",
      (unsigned)WEB_API_SCHEMA_VER, (unsigned long)up_s,
      (unsigned)stage, (unsigned)gen, (unsigned long)age_s,
      (unsigned)hun, (unsigned)hyg, (unsigned)nrg, (unsigned)hap, (unsigned)hea,
      (unsigned)mood, (unsigned)sick, (unsigned)poop, (unsigned)asleep,
      (unsigned)sid, (unsigned)sw, (unsigned)sh, (unsigned)sn, (unsigned)SPRITE_REV,
      (unsigned)wxc, (int)wxt,
      (unsigned)cd_a, (unsigned)cd_g, (unsigned)cd_g, (unsigned)cd_g,
      (unsigned)busy);
  }
}

static void send_state(void)
{
  build_state(s_json, sizeof(s_json));
  send_json(200, s_json);
}

// =============================================================================
//  8. MINIGAME SLOT
// =============================================================================

static uint32_t game_deadline_ms(void)
{
  return s_game_start_ms
       + (uint32_t)s_game_dur_s * 1000u
       + (uint32_t)MG_TOKEN_GRACE_S * 1000u;
}

static bool game_slot_live(void)
{
  if (s_game_tok == 0) return false;
  if ((int32_t)(millis() - game_deadline_ms()) >= 0) return false;   // wrap-safe
  return true;
}

static uint16_t game_slot_left_s(void)
{
  if (!game_slot_live()) return 0;
  const uint32_t left = game_deadline_ms() - millis();
  return (uint16_t)NT_MIN(left / 1000u + 1u, 65535u);
}

static void game_slot_burn(void)
{
  s_game_tok      = 0;
  s_game_start_ms = 0;
  s_game_dur_s    = 0;
  s_game_id       = MG_NONE;
}

bool     web_game_busy(void)   { return game_slot_live(); }
uint8_t  web_game_id(void)     { return game_slot_live() ? s_game_id : (uint8_t)MG_NONE; }
uint16_t web_game_left_s(void) { return game_slot_left_s(); }
void     web_game_abort(void)  { game_slot_burn(); }

// =============================================================================
//  9. HANDLERS
// =============================================================================

// ---- GET / ------------------------------------------------------------------
static void h_root(void)
{
  note_request();
  if (!rate_take(WEB_COST_READ)) { send_throttled(); return; }
  s_srv.sendHeader(F("Cache-Control"), F("no-cache"));
  // FOUR-arg send_P. The 3-arg form strlen_P()s the blob (WebServer.cpp:619).
  s_srv.send_P(200, PSTR("text/html; charset=utf-8"), INDEX_HTML, INDEX_HTML_LEN);
  s_served++;
}

// ---- GET /api/state ---------------------------------------------------------
static void h_state(void)
{
  note_request();
  if (!rate_take(WEB_COST_READ)) { send_throttled(); return; }
  send_state();
}

// ---- POST /api/action?do=..&k=NNNN -----------------------------------------
static void h_action(void)
{
  note_request();
  if (!rate_take(WEB_COST_MUTATE)) { send_throttled(); return; }
  if (!pin_ok()) return;

  if (!s_srv.hasArg("do")) { send_err(400, "arg"); return; }
  const String d = s_srv.arg("do");
  const char*  s = d.c_str();

  ActionId a = ACT_NONE;
  if      (!strcmp(s, "feed"))  a = ACT_FEED_MEAL;
  else if (!strcmp(s, "snack")) a = ACT_FEED_SNACK;
  else if (!strcmp(s, "clean")) a = ACT_CLEAN;
  else if (!strcmp(s, "med"))   a = ACT_MEDICINE;
  else if (!strcmp(s, "play"))  a = ACT_PLAY;
  else if (!strcmp(s, "sleep")) a = ACT_SLEEP_TOGGLE;
  else { send_err(400, "arg"); return; }

  const PetSave* p0 = sim_save();
  if (!p0) { send_err(409, "dead"); return; }

  // The snapshot goes FIRST, for the same reason ui.cpp's do_action() takes one:
  // sim_apply_action(ACT_CLEAN) zeroes poop_count immediately, so the panel-side
  // choreography has nothing left to dissolve if we wait until afterwards.
  const PetSave before = *p0;

  ActionResult r;
  if (!sim_apply_action(a, r)) { send_action_err(r); return; }

  // Hand the panel something to show. Only a SUCCESSFUL action gets here, which
  // is the same rule the buttons follow.
  s_act_before  = before;
  s_act_pending = (uint8_t)a;

  // Persist immediately, exactly as ui.cpp does after a device action. Without
  // this a web mutation is only covered by the 300 s periodic save, which leaves
  // the persistent gain ledger replayable across a power cycle (~720 pts/h against
  // a 60/h design ceiling). Both paths mutate the same pet; both must persist the
  // same way. Rides the existing store_save() cadence gate, adds no new timer.
  { const PetSave* sp = sim_save(); if (sp) store_save(*sp, true); }

  // One round trip mutates AND refreshes: the client feeds the reply straight
  // into its state consumer and must NOT follow an action with a poll.
  send_state();
}

// ---- POST /api/game/start?id=1|2|3&k=NNNN ----------------------------------
static void h_game_start(void)
{
  note_request();
  if (!rate_take(WEB_COST_MUTATE)) { send_throttled(); return; }
  if (!pin_ok()) return;

  const String ids = s_srv.arg("id");
  const uint32_t id = arg_u32(ids.c_str(), 0, 255);
  if (id < MG_SNACK_RUSH || id >= MG_ID_COUNT) { send_err(400, "arg"); return; }

  // One slot, globally. A second phone is told who is in it and for how long;
  // it keeps polling /api/state and stays a live spectator.
  if (game_slot_live()) {
    snprintf(s_json, sizeof(s_json), "{\"err\":\"busy\",\"s\":%u}",
             (unsigned)game_slot_left_s());
    send_json(409, s_json);
    return;
  }

  const PetSave* p = sim_save();
  if (!p)                        { send_err(409, "dead");   return; }
  if (p->stage == STAGE_DEAD)    { send_err(409, "dead");   return; }
  if (p->stage == STAGE_EGG)     { send_err(409, "egg");    return; }
  if (p->flags & PF_ASLEEP)      { send_err(409, "asleep"); return; }
  if (sim_sulk_left_s() > 0)     { send_cool(sim_sulk_left_s()); return; }
  if (sim_stat_pct(ST_ENERGY) < ACT_PLAY_MIN_ENERGY_PCT) { send_err(409, "tired"); return; }

  const uint16_t cd = sim_minigame_cooldown_s();
  if (cd > 0) { send_cool(cd); return; }

  // 0 is our "empty slot" sentinel, so a zero draw is never used as a token.
  uint32_t tok = rng_u32(RNG_MISC);
  if (tok == 0) tok = 0xA5A5A5A5u;

  s_game_tok      = tok;
  s_game_id       = (uint8_t)id;
  s_game_dur_s    = mg_duration_s((uint8_t)id);
  s_game_start_ms = millis();

  snprintf(s_json, sizeof(s_json),
           "{\"tok\":\"%08lx\",\"dur\":%u,\"exp\":%u}",
           (unsigned long)tok,
           (unsigned)s_game_dur_s,
           (unsigned)(s_game_dur_s + MG_TOKEN_GRACE_S));
  send_json(200, s_json);
}

// ---- POST /api/game?id=..&tok=..&score=..&k=NNNN ---------------------------
//
//  NEVER trust the posted score. The browser proposes an id and a number; the
//  SERVER decides what that is worth. Validation order, and every failure past
//  the PIN burns the token so a score can be submitted exactly once:
//    1. token matches the single outstanding slot
//    2. id matches the id the token was issued for
//    3. elapsed >= dur * MG_MIN_ELAPSED_PERMILLE / 1000   (a bot cannot post a
//       perfect run in two seconds; to farm honestly it must sit out the real
//       duration, at which point it is just playing the game)
//    4. elapsed <= dur + MG_TOKEN_GRACE_S
//    5. score clamped to MGx_SCORE_MAX, then handed to sim_apply_minigame(),
//       which owns the MGx_*_NUM / _CAP fixed-point mapping, the shared
//       cooldown and the hourly-gain ledger.
// ----------------------------------------------------------------------------
static void h_game_submit(void)
{
  note_request();
  if (!rate_take(WEB_COST_MUTATE)) { send_throttled(); return; }
  if (!pin_ok()) return;

  const String toks = s_srv.arg("tok");
  const uint32_t tok = parse_tok8(toks.c_str());
  if (tok == 0 || !game_slot_live() || tok != s_game_tok) {
    game_slot_burn();
    send_err(400, "tok");
    return;
  }

  const String ids = s_srv.arg("id");
  const uint32_t id = arg_u32(ids.c_str(), 0, 255);
  if (id != (uint32_t)s_game_id) { game_slot_burn(); send_err(400, "tok"); return; }

  const uint32_t elapsed_ms = millis() - s_game_start_ms;
  const uint32_t min_ms     = (uint32_t)s_game_dur_s * (uint32_t)MG_MIN_ELAPSED_PERMILLE;
  const uint32_t max_ms     = (uint32_t)s_game_dur_s * 1000u
                            + (uint32_t)MG_TOKEN_GRACE_S * 1000u;
  if (elapsed_ms < min_ms) { game_slot_burn(); send_err(409, "fast");  return; }
  if (elapsed_ms > max_ms) { game_slot_burn(); send_err(410, "stale"); return; }

  const String scs = s_srv.arg("score");
  uint32_t score = arg_u32(scs.c_str(), 0, 65535);
  const uint16_t smax = mg_score_max((uint8_t)id);
  if (score > smax) score = smax;

  const uint8_t mg = (uint8_t)id;
  game_slot_burn();                       // burned before the effect, always

  ActionResult r;
  if (!sim_apply_minigame(mg, (uint16_t)score, r)) { send_action_err(r); return; }

  // Persist immediately, exactly as ui.cpp does after a device action. Without
  // this a web mutation is only covered by the 300 s periodic save, which leaves
  // the persistent gain ledger replayable across a power cycle (~720 pts/h against
  // a 60/h design ceiling). Both paths mutate the same pet; both must persist the
  // same way. Rides the existing store_save() cadence gate, adds no new timer.
  { const PetSave* sp = sim_save(); if (sp) store_save(*sp, true); }

  // {"ok":1,"d":{..},"st":{..}} - `d` carries only the stats that moved, in
  // WHOLE points (sim.cpp:1269 divides the milli-point delta by 1000), keyed
  // exactly as the browser's label table expects.
  //  Length proof, so the cursor arithmetic below can never truncate (a
  //  truncated body is unparseable JSON and the page would freeze on it):
  //      prefix  {"ok":1,"d":{                                     = 13
  //      5 deltas, worst case  ,"hun":-32768                       = 5 * 14 = 70
  //      tail    },"st":{"hun":100,...,"hea":100}}                 = 58
  //                                                          total = 141
  //  against WEB_JSON_BUF (320). The static_assert pins it.
  static_assert(13 + 5 * 14 + 58 < WEB_JSON_BUF, "/api/game reply cannot overrun");

  size_t o = 0;
  int    w = snprintf(s_json, sizeof(s_json), "{\"ok\":1,\"d\":{");
  if (w > 0) o = (size_t)w;

  static const char* const kKeys[5] = { "hun", "hap", "nrg", "hyg", "hea" };
  static const uint8_t     kIdx [5] = { ST_HUNGER, ST_HAPPINESS, ST_ENERGY,
                                        ST_HYGIENE, ST_HEALTH };
  bool first = true;
  for (uint8_t i = 0; i < 5; ++i) {
    const int16_t dv = r.d[kIdx[i]];
    if (!dv) continue;
    w = snprintf(s_json + o, sizeof(s_json) - o, "%s\"%s\":%d",
                 first ? "" : ",", kKeys[i], (int)dv);
    if (w > 0) o += (size_t)w;
    first = false;
  }
  snprintf(s_json + o, sizeof(s_json) - o,
           "},\"st\":{\"hun\":%u,\"hyg\":%u,\"nrg\":%u,\"hap\":%u,\"hea\":%u}}",
           (unsigned)sim_stat_pct(ST_HUNGER),  (unsigned)sim_stat_pct(ST_HYGIENE),
           (unsigned)sim_stat_pct(ST_ENERGY),  (unsigned)sim_stat_pct(ST_HAPPINESS),
           (unsigned)sim_stat_pct(ST_HEALTH));
  send_json(200, s_json);
}

// ---- GET /api/sprites?id=N --------------------------------------------------
//
//  Six-byte header then the whole animation set in one response - one frame per
//  request would be one TCP connection per frame, because Connection: close is
//  unconditional (WebServer.cpp:575).
//
//  setContentLength() + a headers-only send() + sendContent()/sendContent_P()
//  is the only shape that survives: XBM rows are mostly 0x00, so the 1-arg
//  sendContent_P (WebServer.cpp:663, strlen_P) would truncate the body at the
//  first blank pixel row.
// ----------------------------------------------------------------------------
static void h_sprites(void)
{
  note_request();
  if (!rate_take(WEB_COST_READ)) { send_throttled(); return; }

  const String ids = s_srv.arg("id");
  const uint32_t id = arg_u32(ids.c_str(), 0xFFFFu, 0xFFFFu);
  if (id >= SPRITE_SET_COUNT) { send_err(400, "arg"); return; }

  const SpriteSet set = sprite_set((uint8_t)id);
  const uint32_t bpf   = (uint32_t)((set.w + 7u) >> 3) * set.h;
  const uint32_t total = bpf * set.frames;

  const uint8_t hdr[6] = { 'S', 1, set.frames, set.w, set.h, (uint8_t)SPRITE_REV };

  char etag[24];
  snprintf(etag, sizeof(etag), "\"%u-%u\"", (unsigned)id, (unsigned)SPRITE_REV);

  // Immutable: the client only refetches when spr.id or spr.rev changes, and
  // SPRITE_REV is bumped by hand on any art edit (sprites.h:20).
  s_srv.sendHeader(F("Cache-Control"), F("public, max-age=604800, immutable"));
  s_srv.sendHeader(F("ETag"), etag);
  s_srv.setContentLength(6u + total);
  s_srv.send(200, "application/octet-stream", "");     // headers only
  s_srv.sendContent((const char*)hdr, sizeof(hdr));
  s_srv.sendContent_P((PGM_P)set.bits, total);         // TWO-arg. Mandatory.
  s_served++;
}

// ---- GET /api/cfg  /  POST /api/cfg?..&k=NNNN -------------------------------
//
//  The password is NEVER echoed. "pass" in the reply is 0/1: "is one set".
// ----------------------------------------------------------------------------
static void build_cfg(char* buf, size_t cap)
{
  const Config* c = cfg_rw();

  char ssid[SSID_MAX_LEN + 1];
  char tz  [TZ_MAX_LEN + 1];
  char lat [COORD_MAX_LEN + 1];
  char lon [COORD_MAX_LEN + 1];
  char name[NAME_MAX_LEN + 1];

  json_str_sanitised(ssid, sizeof(ssid), c->wifi_ssid, SSID_MAX_LEN);
  json_str_sanitised(tz,   sizeof(tz),   c->tz,        TZ_MAX_LEN);
  json_str_sanitised(lat,  sizeof(lat),  c->lat,       COORD_MAX_LEN);
  json_str_sanitised(lon,  sizeof(lon),  c->lon,       COORD_MAX_LEN);
  json_str_sanitised(name, sizeof(name), c->pet_name,  NAME_MAX_LEN);

  snprintf(buf, cap,
    "{\"v\":%u,\"ssid\":\"%s\",\"pass\":%u,\"name\":\"%s\",\"tz\":\"%s\","
    "\"lat\":\"%s\",\"lon\":\"%s\",\"tg\":%u,\"mute\":%u,\"br\":%u,\"sb\":%u,"
    "\"wx\":%u,\"ble\":%u,\"prov\":%u}",
    (unsigned)WEB_API_SCHEMA_VER, ssid, (unsigned)(c->wifi_pass[0] ? 1u : 0u),
    name, tz, lat, lon,
    (unsigned)c->tg_mode, (unsigned)((c->flags & CF_MUTE) ? 1u : 0u),
    (unsigned)c->brightness, (unsigned)c->statusbar_mode,
    (unsigned)((c->flags & CF_WX_ENABLED)  ? 1u : 0u),
    (unsigned)((c->flags & CF_BLE_ENABLED) ? 1u : 0u),
    (unsigned)((c->flags & CF_PROVISIONED) ? 1u : 0u));
}

// Copies a query argument into a fixed char field, NUL terminating and
// truncating. Returns true when the field actually changed.
static bool cfg_set_str(char* dst, size_t cap, const char* src)
{
  char tmp[PASS_MAX_LEN + 1];
  size_t n = 0;
  const size_t limit = (cap - 1u < sizeof(tmp) - 1u) ? (cap - 1u) : (sizeof(tmp) - 1u);
  while (src && src[n] && n < limit) { tmp[n] = src[n]; n++; }
  tmp[n] = '\0';
  if (!strcmp(dst, tmp)) return false;
  memcpy(dst, tmp, n + 1u);
  return true;
}

static bool coord_valid(const char* s)
{
  if (!s) return false;
  if (!*s) return true;                       // empty clears it
  uint8_t dots = 0, digits = 0;
  for (const char* p = s; *p; ++p) {
    if (*p == '-' && p != s) return false;
    if (*p == '.') { if (++dots > 1) return false; continue; }
    if (*p == '-') continue;
    if (*p < '0' || *p > '9') return false;
    digits++;
  }
  return digits > 0;
}

static void h_cfg(void)
{
  note_request();

  if (s_srv.method() == HTTP_GET) {
    if (!rate_take(WEB_COST_READ)) { send_throttled(); return; }
    build_cfg(s_cfgjson, sizeof(s_cfgjson));
    send_json(200, s_cfgjson);
    return;
  }

  if (!rate_take(WEB_COST_MUTATE)) { send_throttled(); return; }
  if (!pin_ok()) return;

  Config* c = cfg_rw();
  bool changed = false;
  bool creds   = false;

  if (s_srv.hasArg("ssid")) {
    const String v = s_srv.arg("ssid");
    if (cfg_set_str(c->wifi_ssid, sizeof(c->wifi_ssid), v.c_str())) { changed = true; creds = true; }
  }
  if (s_srv.hasArg("pass")) {
    const String v = s_srv.arg("pass");
    if (cfg_set_str(c->wifi_pass, sizeof(c->wifi_pass), v.c_str())) { changed = true; creds = true; }
  }
  if (s_srv.hasArg("name")) {
    const String v = s_srv.arg("name");
    if (cfg_set_str(c->pet_name, sizeof(c->pet_name), v.c_str())) changed = true;
  }
  if (s_srv.hasArg("tz")) {
    const String v = s_srv.arg("tz");
    if (cfg_set_str(c->tz, sizeof(c->tz), v.c_str())) changed = true;
  }
  if (s_srv.hasArg("lat")) {
    const String v = s_srv.arg("lat");
    if (!coord_valid(v.c_str())) { send_err(400, "arg"); return; }
    if (cfg_set_str(c->lat, sizeof(c->lat), v.c_str())) changed = true;
  }
  if (s_srv.hasArg("lon")) {
    const String v = s_srv.arg("lon");
    if (!coord_valid(v.c_str())) { send_err(400, "arg"); return; }
    if (cfg_set_str(c->lon, sizeof(c->lon), v.c_str())) changed = true;
  }
  if (s_srv.hasArg("tg")) {
    const String v = s_srv.arg("tg");
    const uint32_t m = arg_u32(v.c_str(), 0xFFu, 0xFFu);
    if (m >= TG_MODE_COUNT) { send_err(400, "arg"); return; }
    if (c->tg_mode != (uint8_t)m) { c->tg_mode = (uint8_t)m; changed = true; }
    tg_set_mode((TgMode)m);   // no-op stub when FEATURE_TELEGRAM == 0
  }
  if (s_srv.hasArg("mute")) {
    const String v = s_srv.arg("mute");
    const uint32_t m = arg_u32(v.c_str(), 0xFFu, 0xFFu);
    if (m > 1u) { send_err(400, "arg"); return; }
    const uint8_t f = (uint8_t)(m ? (c->flags | CF_MUTE) : (c->flags & (uint8_t)~CF_MUTE));
    if (f != c->flags) { c->flags = f; changed = true; }
  }
  if (s_srv.hasArg("br")) {
    const String v = s_srv.arg("br");
    const uint32_t b = arg_u32(v.c_str(), 0x100u, 0x100u);
    if (b > 255u) { send_err(400, "arg"); return; }
    if (c->brightness != (uint8_t)b) { c->brightness = (uint8_t)b; changed = true; }
  }
  if (s_srv.hasArg("sb")) {
    const String v = s_srv.arg("sb");
    const uint32_t b = arg_u32(v.c_str(), 0xFFu, 0xFFu);
    if (b >= SBAR_COUNT) { send_err(400, "arg"); return; }
    if (c->statusbar_mode != (uint8_t)b) { c->statusbar_mode = (uint8_t)b; changed = true; }
  }

  if (changed) {
    // store_save_cfg() re-seals magic/version/CRC and NUL-terminates every
    // char field IN *c, so nothing above has to - and when *c is the entry
    // point's g_cfg the refreshed crc16 is what makes config_changed() fire,
    // which is the only thing that pushes a new "br" to rd_set_contrast().
    store_save_cfg(*c);
    s_cfg_dirty = true;
    // Applying new credentials restarts the station, which would kill the very
    // socket this response has to travel over. Defer it to web_service().
    if (creds) s_cfg_apply_net = true;
  }

  build_cfg(s_cfgjson, sizeof(s_cfgjson));
  send_json(200, s_cfgjson);
}

// ---- catch-all / captive portal --------------------------------------------
//
//  net.cpp owns the DNSServer (net.h:5-7); this is only the HTTP half. Android
//  and iOS probe a known URL and read the status: a 302 to our own root is what
//  makes the "sign in to network" sheet appear. When the Host header already IS
//  us, the request is a genuine 404.
// ----------------------------------------------------------------------------
static void h_notfound(void)
{
  note_request();
  if (!rate_take(WEB_COST_READ)) { send_throttled(); return; }

  const char* ip = net_ip();
  const String host = s_srv.hostHeader();

  // "0.0.0.0" means the radio has no address yet; redirecting there would send
  // the phone nowhere, so treat every request as genuinely ours and 404.
  const bool have_ip = ip && ip[0] && strcmp(ip, "0.0.0.0") != 0;

  const bool addressed_to_us =
      !have_ip ||
      host.length() == 0 ||
      host.startsWith(ip) ||
      host.startsWith("nottamagochi");

  if (!addressed_to_us) {
    char loc[40];
    snprintf(loc, sizeof(loc), "http://%s/", ip);
    s_srv.sendHeader(F("Location"), loc, true);
    s_srv.sendHeader(F("Cache-Control"), F("no-store"));
    s_srv.send(302, "text/plain", "");
    s_served++;
    return;
  }
  send_err(404, "arg");
}

// =============================================================================
//  10. LIFECYCLE
// =============================================================================

uint16_t web_pin(void)
{
  if (s_pin == 0xFFFFu) s_pin = (uint16_t)rng_below(RNG_MISC, (uint32_t)WEB_PIN_MAX);
  return s_pin;
}

void web_pin_regenerate(void)
{
  s_pin = (uint16_t)rng_below(RNG_MISC, (uint32_t)WEB_PIN_MAX);
}

uint32_t web_client_seen_ms(void) { return s_last_client_ms; }

bool web_client_active(void)
{
  if (!s_last_client_ms) return false;
  return (millis() - s_last_client_ms) < WEB_CLIENT_ACTIVE_MS;
}

uint32_t web_requests_served(void)   { return s_served; }
uint32_t web_requests_rejected(void) { return s_rejected; }
bool     web_running(void)           { return s_running; }
uint16_t web_port(void)              { return s_port; }

bool web_begin(uint16_t port)
{
  s_want_port = port;

  (void)web_pin();                   // roll it once, keep it across restarts

  if (!s_routes_done) {
    s_routes_done = true;

    if (!s_cfg) {                    // unbound: keep a private copy
      if (!store_load_cfg(s_cfg_local)) store_cfg_defaults(s_cfg_local);
    }

    s_bucket_mtok = (uint16_t)(WEB_RATE_TOKENS * 1000);
    s_bucket_ms   = millis();

    // Registered exactly once for the lifetime of the firmware:
    // WebServer::on() appends to a linked list it never prunes, so a
    // stop()/begin() cycle that re-registered would leak a RequestHandler and
    // a std::function per cycle for no behavioural change (the first match
    // wins).
    s_srv.on("/",                HTTP_GET,  h_root);
    s_srv.on("/api/state",       HTTP_GET,  h_state);
    s_srv.on("/api/sprites",     HTTP_GET,  h_sprites);
    s_srv.on("/api/action",      HTTP_POST, h_action);
    s_srv.on("/api/game/start",  HTTP_POST, h_game_start);
    s_srv.on("/api/game",        HTTP_POST, h_game_submit);
    s_srv.on("/api/cfg",         HTTP_GET,  h_cfg);
    s_srv.on("/api/cfg",         HTTP_POST, h_cfg);
    s_srv.onNotFound(h_notfound);

    // WebServer.h:288 defaults _nullDelay = true, which burns 1 ms inside
    // every idle handleClient() - 20 % of a 20 fps frame budget, for nothing.
    s_srv.enableDelay(false);
  }

  // Routes stay registered either way (they are registered exactly once for the
  // lifetime of the firmware), but the socket only opens when the user wants
  // it. web_service() re-checks every pump, so a later S9 toggle is honoured
  // without the entry point calling us again.
  if (!web_enabled()) {
    if (s_running) web_stop();
    return true;
  }

  if (s_running && port == s_port) return true;

  // The listening socket cannot exist before lwIP does. At boot the radio is
  // RADIO_OFF, so NetworkServer::begin() -> lwip_socket() -> netconn_apimsg()
  // -> tcpip_send_msg_wait_sem() -> sys_mutex_lock() takes a mutex that is
  // still NULL, and FreeRTOS aborts the whole firmware with
  //     assert failed: xQueueSemaphoreTake queue.c:1709 (( pxQueue ))
  // Only NPH_STA_UP and NPH_AP_PORTAL guarantee a netif with an address.
  // web_service() re-enters web_begin() on every pump while the port is
  // wanted but closed, so the socket opens by itself the moment the radio
  // comes up - no other module has to remember to call us back.
  {
    const NetPhase ph = net_phase();
    if (ph != NPH_STA_UP && ph != NPH_AP_PORTAL) return true;
  }

  s_srv.begin(port);
  s_port    = port;
  s_running = true;
  return true;
}

void web_service(void)
{
  // CF_WEB_ENABLED is a LIVE switch (PH3 finding 7). Falling edge -> drop the
  // listening socket and burn any outstanding minigame token; rising edge ->
  // re-open on the port web_begin() was originally given. Both are cheap: the
  // check is one flag test per loop() pass and neither edge fires twice.
  const bool want = web_enabled();
  if (want) {
    if (!s_running && s_routes_done) (void)web_begin(s_want_port);
  } else {
    if (s_running) web_stop();
  }

  if (!s_running) return;

  s_srv.handleClient();

  // Deferred side effect of POST /api/cfg. Runs one pump AFTER the response
  // was written, so the phone gets its 200 before the radio is restarted.
  if (s_cfg_apply_net) {
    s_cfg_apply_net = false;
    const Config* c = cfg_rw();
    net_set_credentials(c->wifi_ssid, c->wifi_pass);
  }

  // A token whose window has closed is dead weight: reap it so the next phone
  // is not told "busy" by a game nobody is playing.
  if (s_game_tok != 0 && !game_slot_live()) game_slot_burn();
}

void web_stop(void)
{
  if (!s_running) return;
  game_slot_burn();
  s_srv.close();
  s_running = false;
}

#else  // !FEATURE_WEB ========================================================

bool     web_begin(uint16_t)          { return false; }
void     web_service(void)            {}
void     web_stop(void)               {}
bool     web_running(void)            { return false; }
uint16_t web_port(void)               { return 0; }
uint16_t web_pin(void)                { return 0; }
void     web_pin_regenerate(void)     {}
uint32_t web_client_seen_ms(void)     { return 0; }
bool     web_client_active(void)      { return false; }
uint32_t web_requests_served(void)    { return 0; }
uint32_t web_requests_rejected(void)  { return 0; }
void     web_bind_config(Config*)     {}
const Config* web_cfg(void)           { return nullptr; }
bool     web_cfg_dirty(void)          { return false; }
void     web_cfg_clear_dirty(void)    {}
uint8_t  web_take_action(PetSave*)    { return ACT_NONE; }
bool     web_game_busy(void)          { return false; }
uint8_t  web_game_id(void)            { return MG_NONE; }
uint16_t web_game_left_s(void)        { return 0; }
void     web_game_abort(void)         {}
uint8_t  web_pose_of(const PetSave&)  { return POSE_IDLE; }
uint8_t  web_sprite_id(const PetSave&){ return SPR_EGG_IDLE; }
uint8_t  web_mood_index(uint8_t)      { return MOOD_NEUTRO; }

#endif // FEATURE_WEB
