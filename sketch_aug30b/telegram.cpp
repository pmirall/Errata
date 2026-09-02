// =============================================================================
//  telegram.cpp - Nottamagochi guilt channel. The ONLY TLS consumer.
//
//  Layout of this file:
//    PART A - pure logic. No Arduino, no radio, no wall clock. Host-testable
//             with -DNT_TG_PURE_TEST (see TOOLING.md 2/4): percent-encoding,
//             the template renderer, the civil-date maths, the message pool
//             tables and the anti-spam predicates.
//    PART B - the queue, the guilt ladder, the quiet-hours digest and the
//             HTTPS transport. Compiled only for the target.
//
//  Contracts honoured here:
//    * BRIEF 1.3  every TLS attempt is gated on RADIO_WIFI + no BLE +
//                 ESP.getMaxAllocHeap() >= TLS_MIN_MAXALLOC_HEAP. Fail -> queue.
//    * BRIEF 1.7  Telegram is HTTPS-only; WiFiClientSecure + setInsecure().
//    * RISK 3     client and http share one scope so both destructors run;
//                 client.stop() on every path; free heap logged before/after
//                 and the cumulative drift is logged with it.
//    * RISK 4     setHandshakeTimeout() is SECONDS, the HTTPClient timeouts are
//                 MILLISECONDS. The unit is written at every call site.
//    * Rule 4     zero floating point anywhere in this file.
//    * Rule 7     no String concatenation; snprintf into fixed static buffers.
//    * A3         every user-facing string comes from strings_es.h.
// =============================================================================

#include "telegram.h"
#include "strings_es.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

// =============================================================================
//  PART A - PURE LOGIC (host-testable)
// =============================================================================

// -----------------------------------------------------------------------------
//  A.1 Percent-encoding
// -----------------------------------------------------------------------------

static const char TG_HEX[16] = { '0','1','2','3','4','5','6','7',
                                 '8','9','A','B','C','D','E','F' };

// RFC 3986 unreserved. Everything else is escaped, including '+' and ':' - the
// bot token's colon lives in the PATH and is appended raw, never through here.
static inline bool tg_is_unreserved(unsigned char c)
{
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
         (c >= '0' && c <= '9') ||
         c == '-' || c == '_' || c == '.' || c == '~';
}

// Length in bytes of the UTF-8 sequence starting at p, clamped so we never run
// past the terminating NUL. A stray or truncated sequence counts as 1 byte.
static size_t tg_utf8_seq_len(const unsigned char* p)
{
  const unsigned char c = p[0];
  size_t want;
  if (c < 0x80u)                 want = 1;
  else if ((c & 0xE0u) == 0xC0u) want = 2;
  else if ((c & 0xF0u) == 0xE0u) want = 3;
  else if ((c & 0xF8u) == 0xF0u) want = 4;
  else                           want = 1;
  for (size_t i = 1; i < want; ++i) {
    if ((p[i] & 0xC0u) != 0x80u) return 1;
  }
  return want;
}

size_t tg_url_encode(const char* in, char* out, size_t out_cap)
{
  if (out == 0 || out_cap == 0) return 0;
  out[0] = '\0';
  if (in == 0) return 0;

  size_t o = 0;
  for (const unsigned char* p = (const unsigned char*)in; *p != 0; ++p) {
    const unsigned char c = *p;
    if (tg_is_unreserved(c)) {
      if (o + 1 >= out_cap) { out[0] = '\0'; return 0; }
      out[o++] = (char)c;
    } else {
      if (o + 3 >= out_cap) { out[0] = '\0'; return 0; }
      out[o++] = '%';
      out[o++] = TG_HEX[(c >> 4) & 0x0Fu];
      out[o++] = TG_HEX[c & 0x0Fu];
    }
  }
  out[o] = '\0';
  return o;
}

// Same encoding, but instead of failing on overflow it stops at the last
// COMPLETE UTF-8 codepoint that fits. Used for the URL, where half of an
// encoded multi-byte character would be worse than a slightly short message.
static size_t tg_url_encode_fit(const char* in, char* out, size_t out_cap)
{
  if (out == 0 || out_cap == 0) return 0;
  out[0] = '\0';
  if (in == 0) return 0;

  size_t o = 0;
  const unsigned char* p = (const unsigned char*)in;
  while (*p != 0) {
    const size_t clen = tg_utf8_seq_len(p);
    size_t cost = 0;
    for (size_t i = 0; i < clen; ++i) cost += tg_is_unreserved(p[i]) ? 1u : 3u;
    if (o + cost + 1 > out_cap) break;               // stop on a codepoint edge
    for (size_t i = 0; i < clen; ++i) {
      const unsigned char c = p[i];
      if (tg_is_unreserved(c)) {
        out[o++] = (char)c;
      } else {
        out[o++] = '%';
        out[o++] = TG_HEX[(c >> 4) & 0x0Fu];
        out[o++] = TG_HEX[c & 0x0Fu];
      }
    }
    p += clen;
  }
  out[o] = '\0';
  return o;
}

// -----------------------------------------------------------------------------
//  A.2 Civil calendar from an epoch, integer only.
//      T14 prints the DEATH timestamp, which is not "now", and only
//      gametime.cpp may touch the wall clock (BRIEF 4 layering). So we take the
//      current UTC->local offset from gametime once per pass and shift by it.
//      Howard Hinnant's days_from_civil / civil_from_days; exact for 1970-2099.
//      A DST boundary between the death and now can shift the printed hour by
//      one - acceptable, and the alternative is a wall-clock call from here.
// -----------------------------------------------------------------------------

struct TgCivil { int y, mo, d, hh, mi, ss; };

static void tg_civil_from_epoch(uint32_t local_epoch, TgCivil* c)
{
  const uint32_t days = local_epoch / 86400u;
  const uint32_t secs = local_epoch % 86400u;
  c->hh = (int)(secs / 3600u);
  c->mi = (int)((secs / 60u) % 60u);
  c->ss = (int)(secs % 60u);

  const int32_t  z   = (int32_t)days + 719468;        // shift era to 0000-03-01
  const int32_t  era = (z >= 0 ? z : z - 146096) / 146097;
  const uint32_t doe = (uint32_t)(z - era * 146097);                 // 0..146096
  const uint32_t yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
  const int32_t  yy  = (int32_t)yoe + era * 400;
  const uint32_t doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
  const uint32_t mp  = (5u * doy + 2u) / 153u;
  const uint32_t dd  = doy - (153u * mp + 2u) / 5u + 1u;
  const int      mm  = (int)mp + ((mp < 10u) ? 3 : -9);

  c->mo = mm;
  c->d  = (int)dd;
  c->y  = (int)(yy + ((mm <= 2) ? 1 : 0));
}

static int32_t tg_days_from_civil(int y, int m, int d)
{
  y -= (m <= 2) ? 1 : 0;
  const int32_t  era = (int32_t)((y >= 0 ? y : y - 399) / 400);
  const uint32_t yoe = (uint32_t)(y - (int)era * 400);                 // 0..399
  const uint32_t doy = (uint32_t)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
  const uint32_t doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
  return era * 146097 + (int32_t)doe - 719468;
}

// -----------------------------------------------------------------------------
//  A.3 Care grade from CQ (0..1000). GAME_DESIGN 9.3 stores a grade in the
//      ancestor record but never fixes the thresholds; these are ours. See the
//      hand-off notes - this wants to move to sim/ so the lineage screen, the
//      ancestor record and this message all agree on the same letter.
// -----------------------------------------------------------------------------
static CareGrade tg_grade_from_cq(int16_t cq)
{
  if (cq >= 850) return GRADE_A;
  if (cq >= 700) return GRADE_B;
  if (cq >= 550) return GRADE_C;
  if (cq >= 400) return GRADE_D;
  if (cq >= 250) return GRADE_E;
  return GRADE_F;
}

// -----------------------------------------------------------------------------
//  A.4 Template renderer.
//      GAME_DESIGN 7.2 placeholders: {t} {n} {f} {h} {c} {g} {l}
//      plus {forma} {dias} {comidas} {juegos} {fallos} {grado} in P01/P05.
//      An unknown token is emitted literally rather than swallowed, so a typo
//      in the string table shows up on the phone instead of disappearing.
// -----------------------------------------------------------------------------

struct TgSubst {
  char t[28];        // "2 d 7 h 41 min"
  char n[20];        // pet name - or a line count, in the digest string
  char f[16];        // "04/03/2026" - 16, not 11: GCC cannot prove tm_year is
                     // 4-digit, so 12 trips -Wformat-truncation at the snprintf
  char h[8];         // "04:12"
  char g[6];         // generation
  char l[8];         // lineage, 4 hex digits
  char dias[8];
  char comidas[8];
  char juegos[8];
  char fallos[8];
  const char* c;     // death cause, from S_CAUSE()
  const char* forma; // adult form,  from S_FORM()
  const char* grado; // care grade,  from S_GRADE()
};

static void tg_subst_clear(TgSubst* s)
{
  memset(s, 0, sizeof(*s));
  s->c     = S(STR_CAUSE_NONE);
  s->forma = S(STR_STAGE_BABY);
  s->grado = S(STR_GRADE_C);
  s->t[0] = '?';
  s->n[0] = '?';
  s->f[0] = '?';
  s->h[0] = '?';
  s->g[0] = '0';
  s->l[0] = '0';
  s->dias[0]    = '0';
  s->comidas[0] = '0';
  s->juegos[0]  = '0';
  s->fallos[0]  = '0';
}

static const char* tg_subst_lookup(const TgSubst* s, const char* key, size_t klen)
{
  #define TG_KEY_IS(lit) (klen == (sizeof(lit) - 1) && memcmp(key, lit, klen) == 0)
  if (TG_KEY_IS("t"))       return s->t;
  if (TG_KEY_IS("n"))       return s->n;
  if (TG_KEY_IS("f"))       return s->f;
  if (TG_KEY_IS("h"))       return s->h;
  if (TG_KEY_IS("c"))       return s->c;
  if (TG_KEY_IS("g"))       return s->g;
  if (TG_KEY_IS("l"))       return s->l;
  if (TG_KEY_IS("forma"))   return s->forma;
  if (TG_KEY_IS("dias"))    return s->dias;
  if (TG_KEY_IS("comidas")) return s->comidas;
  if (TG_KEY_IS("juegos"))  return s->juegos;
  if (TG_KEY_IS("fallos"))  return s->fallos;
  if (TG_KEY_IS("grado"))   return s->grado;
  #undef TG_KEY_IS
  return 0;
}

// Returns the number of bytes written, excluding the NUL. Always terminates.
static size_t tg_render(const char* tpl, const TgSubst* sb, char* out, size_t cap)
{
  if (out == 0 || cap == 0) return 0;
  out[0] = '\0';
  if (tpl == 0 || sb == 0) return 0;

  size_t o = 0;
  const char* p = tpl;
  while (*p != '\0' && o + 1 < cap) {
    if (*p != '{') { out[o++] = *p++; continue; }

    const char* e = strchr(p + 1, '}');
    const size_t klen = (e != 0) ? (size_t)(e - (p + 1)) : 0;
    const char* v = (e != 0 && klen > 0 && klen <= 8)
                    ? tg_subst_lookup(sb, p + 1, klen) : 0;
    if (v == 0) { out[o++] = *p++; continue; }     // not a token: emit the '{'

    while (*v != '\0' && o + 1 < cap) out[o++] = *v++;
    p = e + 1;
  }
  out[o] = '\0';
  return o;
}

// -----------------------------------------------------------------------------
//  A.5 The message pool: escalation level and trigger class, indexed by MsgId.
//      Levels are GAME_DESIGN 7.2 verbatim. The bonus pool (P01..P05) sits
//      outside the guilt ladder and carries level 0.
// -----------------------------------------------------------------------------

enum TgTrigger : uint8_t {
  TRG_NONE = 0,
  TRG_SILENCE,   // T01 T06 T08 T10 T12 - the absence ladder
  TRG_HUNGER,    // T02
  TRG_POOP,      // T03
  TRG_SAD,       // T04
  TRG_ENERGY,    // T05
  TRG_SICK,      // T07
  TRG_HEALTH,    // T11 T13
  TRG_WEATHER,   // T09
  TRG_EVENT,     // T14 T15 P01..P05
  TRG_COUNT
};

static const uint8_t TG_LEVEL[MSG_COUNT] = {
  /* MSG_NONE */ 0,
  /* T01..T05 */ 0, 1, 1, 2, 2,
  /* T06..T10 */ 2, 3, 3, 3, 4,
  /* T11..T15 */ 4, 5, 5, 6, 6,
  /* P01..P05 */ 0, 0, 0, 0, 0
};

static const uint8_t TG_TRG[MSG_COUNT] = {
  /* MSG_NONE */ TRG_NONE,
  /* T01..T05 */ TRG_SILENCE, TRG_HUNGER,  TRG_POOP,    TRG_SAD,     TRG_ENERGY,
  /* T06..T10 */ TRG_SILENCE, TRG_SICK,    TRG_SILENCE, TRG_WEATHER, TRG_SILENCE,
  /* T11..T15 */ TRG_HEALTH,  TRG_SILENCE, TRG_HEALTH,  TRG_EVENT,   TRG_EVENT,
  /* P01..P05 */ TRG_EVENT,   TRG_EVENT,   TRG_EVENT,   TRG_EVENT,   TRG_EVENT
};

static_assert(NT_ARRAY_LEN(TG_LEVEL) == (size_t)MSG_COUNT, "TG_LEVEL vs MsgId");
static_assert(NT_ARRAY_LEN(TG_TRG)   == (size_t)MSG_COUNT, "TG_TRG vs MsgId");

// State-derived triggers (GAME_DESIGN 7.2). T09 needs the weather module and
// T14/P01..P05 are events, so they read false here and arrive via tg_queue().
static bool tg_trigger_true(MsgId id, const PetSave* p, uint32_t now, uint32_t silence_s)
{
  if (p == 0) return false;

  const bool dead = (p->flags & PF_DEAD) != 0;

  // The egg-waiting reminder is the only line a dead pet still sends.
  if (id == MSG_T15) {
    return dead && p->death_epoch != 0 && now > p->death_epoch &&
           (now - p->death_epoch) >= 86400u;
  }
  if (dead || p->stage == STAGE_EGG) return false;

  switch (id) {
    case MSG_T01: return silence_s >= 3600u;
    case MSG_T02: return NT_PCT(p->stat[ST_HUNGER])    < 30;
    case MSG_T03: return p->poop_count >= 3;
    case MSG_T04: return NT_PCT(p->stat[ST_HAPPINESS]) < 25;
    case MSG_T05: return NT_PCT(p->stat[ST_ENERGY])    < 20 &&
                         (p->flags & PF_LIGHT_ON) != 0;
    case MSG_T06: return silence_s >=  6u * 3600u;
    case MSG_T07: return (p->flags & PF_SICK) != 0;
    case MSG_T08: return silence_s >= 12u * 3600u;
    case MSG_T10: return silence_s >= 24u * 3600u;
    case MSG_T11: return NT_PCT(p->stat[ST_HEALTH])    < 30;
    case MSG_T12: return silence_s >= 48u * 3600u;
    case MSG_T13: return NT_PCT(p->stat[ST_HEALTH])    < 12;
    default:      return false;
  }
}

// -----------------------------------------------------------------------------
//  A.6 Anti-spam predicates (GAME_DESIGN 7.3). Pure, so the host harness can
//      drive them without a clock.
// -----------------------------------------------------------------------------

// 23:30 -> 08:00 local, expressed in minutes past local midnight.
static bool tg_is_quiet_minute(int minute_of_day)
{
  return (minute_of_day >= TG_QUIET_START_MIN) || (minute_of_day < TG_QUIET_END_MIN);
}

static uint32_t tg_retry_delay_s(uint8_t tries)
{
  switch (tries) {
    case 0:  return TG_RETRY_1_S;
    case 1:  return TG_RETRY_2_S;
    default: return TG_RETRY_3_S;
  }
}

// Elapsed seconds with underflow protection: an epoch in the future reads 0.
static inline uint32_t tg_since(uint32_t now, uint32_t then)
{
  return (then != 0 && now > then) ? (now - then) : 0u;
}

// How long the pet has been ignored. A pet that has never been touched at all
// counts its silence from the hatch, otherwise last_interact_epoch == 0 would
// read as "interacted just now" and the whole ladder would stay mute forever.
static inline uint32_t tg_silence_s(const PetSave* p, uint32_t now)
{
  if (p == 0) return 0;
  const uint32_t ref = (p->last_interact_epoch != 0) ? p->last_interact_epoch
                                                     : p->birth_epoch;
  return tg_since(now, ref);
}


#if !defined(NT_TG_PURE_TEST)
// =============================================================================
//  PART B - TARGET ONLY
// =============================================================================

#include <Arduino.h>
#include <time.h>

#if FEATURE_TELEGRAM

#include <WiFi.h>
#include <WiFiClientSecure.h>   // typedef of NetworkClientSecure (BRIEF 3.4)
#include <HTTPClient.h>

#include "gametime.h"           // gt_now / gt_is_valid / gt_local_tm / gt_format_elapsed
#include "net.h"                // net_mode() - the RadioMode gate (BRIEF 1.3)

// {t} is written by gt_format_elapsed(), which needs GT_ELAPSED_BUF bytes.
static_assert(sizeof(((TgSubst*)0)->t) >= GT_ELAPSED_BUF,
              "TgSubst.t is too small for gt_format_elapsed()");

// -----------------------------------------------------------------------------
//  B.0 Integration seam. Weak, so this module links and runs on its own; a
//      strong definition anywhere in the sketch takes over. Prototypes are
//      documented at the bottom of telegram.h.
// -----------------------------------------------------------------------------
#if defined(__GNUC__)
#  define NT_WEAK __attribute__((weak))
#else
#  define NT_WEAK
#endif

NT_WEAK const PetSave* nt_pet_view(void) { return 0; }
NT_WEAK const Config*  nt_cfg_view(void) { return 0; }

// -----------------------------------------------------------------------------
//  B.1 Module-local constants. These would belong in config.h section 11; they
//      live here so the Foundation header is not touched (see the hand-off).
// -----------------------------------------------------------------------------
static const uint32_t TG_IDLE_BEFORE_SEND_S = 20;  // no button press for 20 s
                                                   // before a blocking send
static const uint32_t TG_TRIGGER_SCAN_S     = 30;  // guilt scan cadence
static const uint32_t TG_ATTEMPT_GAP_S      = 10;  // floor between TLS attempts
static const uint8_t  TG_DIGEST_MAX_TRIES   = 3;
static const int8_t   TG_GUILT_MAX          = 6;

#ifndef TG_LOG_ENABLED
#define TG_LOG_ENABLED 1
#endif
#if TG_LOG_ENABLED
#  define TG_LOG(...) do { Serial.printf(__VA_ARGS__); } while (0)
#else
#  define TG_LOG(...) do { } while (0)
#endif

// -----------------------------------------------------------------------------
//  B.2 Queue and ledger
// -----------------------------------------------------------------------------
#define TGE_USED   0x01u
#define TGE_HELD   0x02u   // parked by quiet hours; may only leave via the digest

struct TgEntry {
  uint32_t queued_epoch;    // 0 = queued before the clock was trusted
  uint32_t next_try_epoch;  // 0 = ready now
  uint8_t  id;              // MsgId
  uint8_t  prio;            // Priority
  uint8_t  tries;           // 0..2, dropped at 3
  uint8_t  flags;           // TGE_*
};
static_assert(sizeof(TgEntry) == 12, "TgEntry grew; check the RAM budget");

static TgEntry  s_q[TG_QUEUE_CAP];

// All epochs are game time (gt_now()).
static uint32_t s_id_last_sent[MSG_COUNT];
static uint32_t s_trg_last_sent[TRG_COUNT];
static uint8_t  s_trg_last_level[TRG_COUNT];
static uint32_t s_last_send_epoch;
static uint32_t s_last_attempt_epoch;
static uint32_t s_last_scan_epoch;
static uint32_t s_last_interact_seen;
static uint16_t s_sent_today;          // P1 + P2 only
static int16_t  s_yday = -1;           // tm_yday that s_sent_today counts
static int8_t   s_guilt;               // 0..6, GAME_DESIGN 7.1
static uint8_t  s_mode = TG_ON;
static uint8_t  s_digest_tries;
static bool     s_begun;
static bool     s_auth_dead;           // 401/403/404 seen: stop hammering
static int32_t  s_heap_drift;          // cumulative free-heap delta across sends

// Big buffers live in BSS, never on the loop stack: the mbedTLS handshake needs
// several KB of the 8 KB loop-task stack all to itself.
static char s_text[TG_TEXT_MAX];
static char s_url[TG_URL_MAX];

// -----------------------------------------------------------------------------
//  B.3 Queue helpers
// -----------------------------------------------------------------------------

static void tg_slot_free(int i) { memset(&s_q[i], 0, sizeof(s_q[i])); }

static void tg_queue_clear(void) { memset(s_q, 0, sizeof(s_q)); }

static void tg_queue_drop_p2(void)
{
  for (size_t i = 0; i < NT_ARRAY_LEN(s_q); ++i) {
    if ((s_q[i].flags & TGE_USED) && s_q[i].prio == PRIO_P2) tg_slot_free((int)i);
  }
}

static int tg_find_id(MsgId id)
{
  for (size_t i = 0; i < NT_ARRAY_LEN(s_q); ++i) {
    if ((s_q[i].flags & TGE_USED) && s_q[i].id == (uint8_t)id) return (int)i;
  }
  return -1;
}

static int tg_free_slot(void)
{
  for (size_t i = 0; i < NT_ARRAY_LEN(s_q); ++i) {
    if ((s_q[i].flags & TGE_USED) == 0) return (int)i;
  }
  return -1;
}

// Evict the oldest entry of strictly lower priority (higher Priority value).
static int tg_evict_for(Priority prio)
{
  int worst = -1;
  for (size_t i = 0; i < NT_ARRAY_LEN(s_q); ++i) {
    if ((s_q[i].flags & TGE_USED) == 0) continue;
    if (s_q[i].prio <= (uint8_t)prio) continue;
    if (worst < 0 || s_q[i].prio > s_q[worst].prio ||
        (s_q[i].prio == s_q[worst].prio &&
         s_q[i].queued_epoch < s_q[worst].queued_epoch)) {
      worst = (int)i;
    }
  }
  if (worst >= 0) tg_slot_free(worst);
  return worst;
}

static bool tg_mode_allows(Priority prio)
{
  if (s_mode == TG_OFF) return false;
  if (s_mode == TG_ONLY_SEVERE) return (prio == PRIO_P0);
  return true;
}

// A bot token is <digits>:<35 chars>. Anything else would corrupt the request
// line, so refuse rather than post a message to a stranger's endpoint.
static bool tg_token_valid(const char* tok)
{
  if (tok == 0) return false;
  const size_t n = strnlen(tok, TG_TOKEN_MAX_LEN + 1);
  if (n < 20 || n > TG_TOKEN_MAX_LEN) return false;
  size_t colons = 0, colon_at = 0;
  for (size_t i = 0; i < n; ++i) {
    const char c = tok[i];
    if (c == ':') { ++colons; colon_at = i; continue; }
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == '_' || c == '-';
    if (!ok) return false;
  }
  if (colons != 1 || colon_at == 0 || colon_at + 1 >= n) return false;
  for (size_t i = 0; i < colon_at; ++i) {
    if (tok[i] < '0' || tok[i] > '9') return false;
  }
  return true;
}

// chat_id is a decimal integer, negative for groups (-1001234567890).
static bool tg_chat_valid(const char* chat)
{
  if (chat == 0) return false;
  const size_t n = strnlen(chat, TG_CHAT_MAX_LEN + 1);
  if (n < 1 || n > TG_CHAT_MAX_LEN) return false;
  size_t i = (chat[0] == '-') ? 1 : 0;
  if (i >= n) return false;
  for (; i < n; ++i) {
    if (chat[i] < '0' || chat[i] > '9') return false;
  }
  return true;
}

// -----------------------------------------------------------------------------
//  B.4 Substitution builder. Every value is read from the live PetSave and
//      Config at SEND time, not at queue time, so a message that waited out the
//      night still tells the truth when it finally goes.
// -----------------------------------------------------------------------------
static void tg_build_subst(MsgId id, const PetSave* p, const Config* cfg,
                           uint32_t now, int32_t local_offset_s, TgSubst* s)
{
  tg_subst_clear(s);

  // {n} - the name. Config.pet_name is the single source of truth: whoever
  // hatches the pet writes the genome-derived name there (GAME_DESIGN 9.3).
  const char* name = (cfg != 0 && cfg->pet_name[0] != '\0') ? cfg->pet_name
                                                            : CFG_PET_NAME;
  if (name[0] == '\0') name = S(STR_APP_NAME);
  snprintf(s->n, sizeof(s->n), "%s", name);

  if (p == 0) return;

  const uint32_t silence_s = tg_silence_s(p, now);

  // {t} - the exact elapsed time. GAME_DESIGN 5.3: never rounded, the precision
  // is the joke. T14 prints the lifespan, T15 how long the egg has been waiting,
  // everything else how long the silence has lasted.
  uint32_t t_s = silence_s;
  if (id == MSG_T14) {
    t_s = (p->death_epoch > p->birth_epoch) ? (p->death_epoch - p->birth_epoch)
                                            : p->age_s;
  } else if (id == MSG_T15) {
    t_s = tg_since(now, p->death_epoch);
  }
  gt_format_elapsed(t_s, s->t, sizeof(s->t));

  // {f} {h} - the moment the message is ABOUT: the death for T14, now for the
  // rest. Numeric on purpose: strings_es.h carries no weekday or month names.
  const uint32_t stamp = (id == MSG_T14 && p->death_epoch != 0) ? p->death_epoch
                                                                : now;
  TgCivil cv;
  tg_civil_from_epoch((uint32_t)((int64_t)stamp + (int64_t)local_offset_s), &cv);
  // Bound every field before formatting. TgCivil carries plain ints, so GCC
  // must assume cv.y spans the whole int range: with a raw "%04d" it computes a
  // worst case of 30 bytes and raises -Wformat-truncation whatever size f has
  // (f[16] is not enough either - measured). The modulo makes each directive
  // provably fixed-width, so the write is 10 bytes + NUL, always. A uint32_t
  // epoch cannot reach year 10000 anyway, so this never truncates a real date.
  snprintf(s->f, sizeof(s->f), "%02u/%02u/%04u",
           (unsigned)cv.d  % 100u,
           (unsigned)cv.mo % 100u,
           (unsigned)cv.y  % 10000u);
  snprintf(s->h, sizeof(s->h), "%02d:%02d", cv.hh, cv.mi);

  // {c} {g} {l}
  s->c = S_CAUSE((p->death_cause < DEATH_COUNT) ? p->death_cause
                                                : (uint8_t)DEATH_NONE);
  snprintf(s->g, sizeof(s->g), "%u", (unsigned)p->genome.generation);
  snprintf(s->l, sizeof(s->l), "%04X", (unsigned)(p->genome.lineage_id & 0xFFFFu));

  // {forma} - the adult form once it exists, the life stage before that.
  s->forma = (p->adult_form < FORM_COUNT)
             ? S_FORM(p->adult_form)
             : S_STAGE((p->stage < STAGE_COUNT) ? p->stage : (uint8_t)STAGE_BABY);

  // {dias} {comidas} {juegos} {fallos} {grado} - the weekly summary, P05.
  snprintf(s->dias,    sizeof(s->dias),    "%lu",
           (unsigned long)(p->age_s / 86400u));
  snprintf(s->comidas, sizeof(s->comidas), "%u", (unsigned)p->snacks_total);
  snprintf(s->juegos,  sizeof(s->juegos),  "%u", (unsigned)p->minigames_won);
  snprintf(s->fallos,  sizeof(s->fallos),  "%u", (unsigned)p->care_miss);
  s->grado = S_GRADE(tg_grade_from_cq(p->cq));
}

// -----------------------------------------------------------------------------
//  B.5 HTTPS transport.
//      Returns  1 = delivered, 0 = retry later, -1 = permanent, drop it.
//
//      This call BLOCKS for roughly 1-3 s: one TLS 1.2 handshake against
//      api.telegram.org plus one GET. tg_service() only reaches it when the
//      player has not touched a button for TG_IDLE_BEFORE_SEND_S, or when the
//      message is P0 - and P0 means death or hatch, where the device is sitting
//      on the memorial / egg screen, which is deliberately static. The hitch is
//      invisible in both cases; there is no non-blocking HTTPS client in 3.1.1.
// -----------------------------------------------------------------------------
static int tg_transmit(void)
{
  const uint32_t heap_before = ESP.getFreeHeap();
  int  code = -1;
  bool ok   = false;

  {   // one scope for both objects: RISK 3, both destructors must run
    WiFiClientSecure client;
    client.setInsecure();                                 // no CA, no clock need
    client.setHandshakeTimeout(TLS_HANDSHAKE_TIMEOUT_S);  // SECONDS (x1000 inside)
    client.setTimeout(8);                                 // SECONDS (NetworkClient)

    HTTPClient http;
    http.setReuse(false);
    http.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);      // MILLISECONDS
    http.setTimeout(HTTP_READ_TIMEOUT_MS);                // MILLISECONDS (uint16_t)
    http.useHTTP10(true);                                 // Connection: close

    if (http.begin(client, String(s_url))) {
      code = http.GET();
      if (code == 200) {
        // Telegram replies are well under 1 KB. Parse rule per NET_APIS 3:
        // strstr for "ok":true and nothing else.
        String body = http.getString();
        ok = (body.indexOf("\"ok\":true") >= 0);
      }
    }
    http.end();
    client.stop();                             // every path, success or failure
  }

  const uint32_t heap_after = ESP.getFreeHeap();
  s_heap_drift += (int32_t)heap_after - (int32_t)heap_before;
  TG_LOG("[tg] http=%d ok=%d heap %lu->%lu drift=%ld\n",
         code, (int)ok,
         (unsigned long)heap_before, (unsigned long)heap_after,
         (long)s_heap_drift);

  if (ok) return 1;
  if (code == 401 || code == 403 || code == 404) {
    // Bad token, bot blocked by the user, or a bad route. Retrying never helps.
    s_auth_dead = true;
    TG_LOG("[tg] %s\n", S(STR_ERR_TG));
    return -1;
  }
  return 0;
}

// Build https://api.telegram.org/bot<TOKEN>/sendMessage?... into s_url.
// The token's ':' is a legal path character and must NOT be percent-encoded.
static bool tg_build_url(const Config* cfg, const char* text, bool silent)
{
  s_url[0] = '\0';
  if (cfg == 0) return false;
  if (!tg_token_valid(cfg->tg_token) || !tg_chat_valid(cfg->tg_chat)) {
    TG_LOG("[tg] %s\n", S(STR_ERR_TG));
    return false;
  }

  const int n = snprintf(s_url, sizeof(s_url),
                         "https://%s/bot%s/sendMessage?chat_id=%s"
                         "&disable_web_page_preview=true%s&text=",
                         TG_HOST, cfg->tg_token, cfg->tg_chat,
                         silent ? "&disable_notification=true" : "");
  if (n <= 0 || (size_t)n >= sizeof(s_url)) { s_url[0] = '\0'; return false; }

  const size_t enc = tg_url_encode_fit(text, s_url + n, sizeof(s_url) - (size_t)n);
  if (enc == 0) { s_url[0] = '\0'; return false; }
  return true;
}

// -----------------------------------------------------------------------------
//  B.6 The TLS gate. BRIEF 1.3, evaluated immediately before every attempt.
// -----------------------------------------------------------------------------
static bool tg_radio_ready(void)
{
  // net.cpp owns the one-stack-at-a-time rule: RADIO_WIFI already implies
  // Bluedroid is deinitialised, which is why this file never includes BLEDevice.h.
  if (net_mode() != RADIO_WIFI) return false;
  if (WiFi.status() != WL_CONNECTED) return false;
  // Never softAP + HTTPS at the same time (BRIEF 1.3 / RISK 3).
  if (WiFi.getMode() != WIFI_MODE_STA) return false;
  // The two 16 KB mbedTLS record buffers must come out of one contiguous block.
  if (ESP.getMaxAllocHeap() < (uint32_t)TLS_MIN_MAXALLOC_HEAP) return false;
  return true;
}

// -----------------------------------------------------------------------------
//  B.7 The guilt ladder (GAME_DESIGN 7.1).
//      Eligibility is (level <= guilt + 1): the ladder may always climb exactly
//      one rung, so it can never deadlock on a pet whose only problem is
//      silence, and it can never jump straight to the nastiest pool. Firing a
//      P2 raises guilt by one, capped at 6; any interaction resets it to 0.
// -----------------------------------------------------------------------------
static void tg_scan_triggers(const PetSave* p, uint32_t now)
{
  if (p == 0) return;
  if (s_mode != TG_ON) return;             // SOLO GRAVES carries P0 only

  // One guilt message in flight at a time; a strictly nastier one replaces it.
  int pending = -1;
  for (size_t i = 0; i < NT_ARRAY_LEN(s_q); ++i) {
    if ((s_q[i].flags & TGE_USED) && s_q[i].prio == PRIO_P2) { pending = (int)i; break; }
  }
  const int pending_level = (pending >= 0) ? (int)TG_LEVEL[s_q[pending].id] : -1;

  const uint32_t silence_s = tg_silence_s(p, now);

  int best = -1, best_level = -1;
  for (int id = MSG_T01; id <= MSG_T15; ++id) {
    if (!tg_trigger_true((MsgId)id, p, now, silence_s)) continue;

    const int level = (int)TG_LEVEL[id];
    if (level > (int)s_guilt + 1) continue;                    // ladder gate

    // 48 h same-id cooldown: one guilt line never fires twice for one episode.
    if (s_id_last_sent[id] != 0 &&
        tg_since(now, s_id_last_sent[id]) < TG_SAME_ID_COOLDOWN_S) continue;

    // 6 h same-trigger-class cooldown, bypassed only by a strictly nastier
    // message of that class - escalation is the whole point of the ladder.
    const uint8_t trg = TG_TRG[id];
    if (s_trg_last_sent[trg] != 0 &&
        tg_since(now, s_trg_last_sent[trg]) < TG_TRIGGER_COOLDOWN_S &&
        level <= (int)s_trg_last_level[trg]) continue;

    if (level > best_level) { best_level = level; best = id; }
  }

  if (best < 0) return;
  if (pending >= 0) {
    if (best_level <= pending_level) return;                   // keep what we have
    tg_slot_free(pending);
  }
  if (tg_queue((MsgId)best, PRIO_P2)) {
    TG_LOG("[tg] guilt T%02d lvl=%d guilt=%d\n", best, best_level, (int)s_guilt);
  }
}

// -----------------------------------------------------------------------------
//  B.8 Send one queued entry
// -----------------------------------------------------------------------------
static void tg_dispatch(int slot, const PetSave* p, const Config* cfg,
                        uint32_t now, int32_t local_offset_s, bool quiet)
{
  const MsgId    id   = (MsgId)s_q[slot].id;
  const Priority pr   = (Priority)s_q[slot].prio;
  const uint8_t  trg  = TG_TRG[id];

  TgSubst sb;
  tg_build_subst(id, p, cfg, now, local_offset_s, &sb);
  tg_render(S_MSG(id), &sb, s_text, sizeof(s_text));

  if (!tg_build_url(cfg, s_text, quiet)) {
    tg_slot_free(slot);                        // unsendable; retrying cannot fix
    return;
  }

  s_last_attempt_epoch = now;
  const int r = tg_transmit();

  if (r == 1) {
    s_id_last_sent[id]    = now;
    s_trg_last_sent[trg]  = now;
    s_trg_last_level[trg] = TG_LEVEL[id];
    s_last_send_epoch     = now;
    if (pr != PRIO_P0) s_sent_today++;
    if (pr == PRIO_P2 && s_guilt < TG_GUILT_MAX) s_guilt++;
    tg_slot_free(slot);
    return;
  }

  if (r < 0) { tg_slot_free(slot); return; }

  // Retry at 1 / 5 / 25 min, then drop (GAME_DESIGN 7.3).
  if (++s_q[slot].tries >= 3) tg_slot_free(slot);
  else s_q[slot].next_try_epoch = now + tg_retry_delay_s(s_q[slot].tries);
}

// -----------------------------------------------------------------------------
//  B.9 The 08:05 digest. Everything parked overnight collapses into ONE line.
//      A single parked message is released as itself: "Resumen de la noche: 1
//      lineas" would be worse than the message it is hiding.
// -----------------------------------------------------------------------------
static void tg_flush_digest(const Config* cfg, uint32_t now)
{
  int held = 0, only = -1;
  for (size_t i = 0; i < NT_ARRAY_LEN(s_q); ++i) {
    if ((s_q[i].flags & TGE_USED) && (s_q[i].flags & TGE_HELD)) { ++held; only = (int)i; }
  }
  if (held == 0) return;

  if (held == 1) {
    s_q[only].flags = (uint8_t)(s_q[only].flags & ~TGE_HELD);
    return;                                    // the normal path sends it next
  }

  TgSubst sb;
  tg_subst_clear(&sb);
  snprintf(sb.n, sizeof(sb.n), "%d", held);    // {n} is a COUNT in this string
  tg_render(S(STR_TG_DIGEST), &sb, s_text, sizeof(s_text));

  const bool built = tg_build_url(cfg, s_text, false);
  int r = -1;
  if (built) {
    s_last_attempt_epoch = now;
    r = tg_transmit();
  }

  if (r == 0 && ++s_digest_tries < TG_DIGEST_MAX_TRIES) return;   // try again later

  // Delivered, permanently refused, or out of tries: either way the night ends.
  for (size_t i = 0; i < NT_ARRAY_LEN(s_q); ++i) {
    if ((s_q[i].flags & TGE_USED) && (s_q[i].flags & TGE_HELD)) tg_slot_free((int)i);
  }
  s_digest_tries = 0;
  if (r == 1) { s_last_send_epoch = now; s_sent_today++; }
}

// =============================================================================
//  B.10 PUBLIC API
// =============================================================================

void tg_begin(void)
{
  tg_queue_clear();
  memset(s_id_last_sent,   0, sizeof(s_id_last_sent));
  memset(s_trg_last_sent,  0, sizeof(s_trg_last_sent));
  memset(s_trg_last_level, 0, sizeof(s_trg_last_level));
  s_last_send_epoch    = 0;
  s_last_attempt_epoch = 0;
  s_last_scan_epoch    = 0;
  s_sent_today         = 0;
  s_yday               = -1;
  s_digest_tries       = 0;
  s_auth_dead          = false;
  s_heap_drift         = 0;
  s_text[0]            = '\0';
  s_url[0]             = '\0';

  const Config* cfg = nt_cfg_view();
  s_mode = (cfg != 0 && cfg->tg_mode < TG_MODE_COUNT) ? cfg->tg_mode
                                                      : (uint8_t)TG_ON;

  const PetSave* p     = nt_pet_view();
  s_guilt              = (p != 0) ? (int8_t)NT_CLAMP((int)p->guilt_level, 0, (int)TG_GUILT_MAX) : 0;
  s_last_interact_seen = (p != 0) ? p->last_interact_epoch : 0;

  s_begun = true;
  TG_LOG("[tg] begin mode=%u guilt=%d\n", (unsigned)s_mode, (int)s_guilt);
}

void tg_set_mode(TgMode mode)
{
  if (mode >= TG_MODE_COUNT) return;
  s_mode = (uint8_t)mode;
  if (s_mode == TG_OFF)              tg_queue_clear();
  else if (s_mode == TG_ONLY_SEVERE) tg_queue_drop_p2();
  s_auth_dead = false;                     // give a re-provisioned token a shot
  TG_LOG("[tg] mode=%u\n", (unsigned)s_mode);
}

bool tg_queue(MsgId id, Priority prio)
{
  if (!s_begun) return false;
  if (id <= MSG_NONE || id >= MSG_COUNT) return false;
  if (prio >= PRIO_COUNT) return false;
  if (!tg_mode_allows(prio)) return false;

  const bool     clock_ok = gt_is_valid();
  const uint32_t now      = clock_ok ? (uint32_t)gt_now() : 0u;

  if (tg_find_id(id) >= 0) return false;                  // already pending

  // 48 h same-id cooldown. P0 (death, hatch) is never suppressed.
  if (prio != PRIO_P0 && clock_ok && s_id_last_sent[id] != 0 &&
      tg_since(now, s_id_last_sent[id]) < TG_SAME_ID_COOLDOWN_S) {
    return false;
  }

  int slot = tg_free_slot();
  if (slot < 0) slot = tg_evict_for(prio);
  if (slot < 0) return false;

  s_q[slot].queued_epoch   = now;         // 0 while the clock is not trusted yet
  s_q[slot].next_try_epoch = 0;
  s_q[slot].id             = (uint8_t)id;
  s_q[slot].prio           = (uint8_t)prio;
  s_q[slot].tries          = 0;
  s_q[slot].flags          = TGE_USED;
  return true;
}

void tg_service(void)
{
  if (!s_begun) return;
  if (s_mode == TG_OFF) { tg_queue_clear(); return; }

  // No trusted clock means no quiet hours, no daily cap and a lying {t}. Hold.
  if (!gt_is_valid()) return;
  const uint32_t now = (uint32_t)gt_now();

  struct tm lt;
  memset(&lt, 0, sizeof(lt));
  gt_local_tm(lt);
  if (lt.tm_year < 70) return;                  // gametime disagrees with itself

  const int  minute_of_day = lt.tm_hour * 60 + lt.tm_min;
  const bool quiet         = tg_is_quiet_minute(minute_of_day);

  // The local UTC offset, derived once per pass, so T14 can print a timestamp
  // in the past without this module ever calling localtime() (BRIEF 4 layering).
  const int64_t local_now =
      (int64_t)tg_days_from_civil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday) * 86400 +
      (int64_t)lt.tm_hour * 3600 + (int64_t)lt.tm_min * 60 + (int64_t)lt.tm_sec;
  const int32_t local_offset_s = (int32_t)(local_now - (int64_t)now);

  const PetSave* p   = nt_pet_view();
  const Config*  cfg = nt_cfg_view();

  // --- daily rollover, local midnight ---------------------------------------
  if (s_yday != (int16_t)lt.tm_yday) {
    s_yday         = (int16_t)lt.tm_yday;
    s_sent_today   = 0;
    s_digest_tries = 0;
  }

  // --- interaction reset (GAME_DESIGN 7.3) ----------------------------------
  // Any meaningful interaction: guilt back to 0, the pending guilt line dropped,
  // and NO "you came back" message. Silence is the reward.
  if (p != 0 && p->last_interact_epoch != s_last_interact_seen) {
    s_last_interact_seen = p->last_interact_epoch;
    if (s_guilt != 0) TG_LOG("[tg] guilt reset\n");
    s_guilt = 0;
    tg_queue_drop_p2();
  }

  // --- clock fix-up + stale P2 ----------------------------------------------
  for (size_t i = 0; i < NT_ARRAY_LEN(s_q); ++i) {
    if ((s_q[i].flags & TGE_USED) == 0) continue;
    if (s_q[i].queued_epoch == 0) s_q[i].queued_epoch = now;   // the clock landed
    if (s_q[i].prio == PRIO_P2 && !quiet && (s_q[i].flags & TGE_HELD) == 0 &&
        tg_since(now, s_q[i].queued_epoch) > TG_DROP_P2_OLDER_S) {
      tg_slot_free((int)i);                    // too old to still be true
    }
  }

  // --- guilt trigger scan ----------------------------------------------------
  if (s_last_scan_epoch == 0 || tg_since(now, s_last_scan_epoch) >= TG_TRIGGER_SCAN_S) {
    s_last_scan_epoch = now;
    tg_scan_triggers(p, now);
  }

  if (s_auth_dead) return;                     // bad token / bot blocked

  // --- pick the next sendable entry -----------------------------------------
  int pick = -1, held = 0;
  for (size_t i = 0; i < NT_ARRAY_LEN(s_q); ++i) {
    if ((s_q[i].flags & TGE_USED) == 0) continue;
    if (s_q[i].flags & TGE_HELD) { ++held; continue; }   // parked: the digest owns it
    if (!tg_mode_allows((Priority)s_q[i].prio)) continue;
    if (s_q[i].next_try_epoch != 0 && now < s_q[i].next_try_epoch) continue;

    if (s_q[i].prio != PRIO_P0) {
      if (quiet) { s_q[i].flags |= TGE_HELD; ++held; continue; }
      if (s_sent_today >= TG_MAX_PER_DAY) continue;
      if (s_last_send_epoch != 0 &&
          tg_since(now, s_last_send_epoch) < TG_MIN_GAP_S) continue;
    }

    if (pick < 0 || s_q[i].prio < s_q[pick].prio ||
        (s_q[i].prio == s_q[pick].prio &&
         s_q[i].queued_epoch < s_q[pick].queued_epoch)) {
      pick = (int)i;
    }
  }

  const bool digest_due = (held > 0 && !quiet && minute_of_day >= TG_DIGEST_MIN);
  if (pick < 0 && !digest_due) return;

  // --- transport gates -------------------------------------------------------
  if (s_last_attempt_epoch != 0 &&
      tg_since(now, s_last_attempt_epoch) < TG_ATTEMPT_GAP_S) return;
  if (!tg_radio_ready()) return;               // BRIEF 1.3: queue, do not try

  // A send blocks for 1-3 s. Only do it while nobody is touching the device -
  // unless it is P0, where the screen is a frozen memorial or egg anyway.
  const bool p0 = (pick >= 0 && s_q[pick].prio == PRIO_P0);
  if (!p0 && p != 0 && p->last_interact_epoch != 0 &&
      tg_since(now, p->last_interact_epoch) < TG_IDLE_BEFORE_SEND_S) return;

  if (pick >= 0) tg_dispatch(pick, p, cfg, now, local_offset_s, quiet);
  else           tg_flush_digest(cfg, now);
}

#else  // FEATURE_TELEGRAM == 0
// -----------------------------------------------------------------------------
//  Feature compiled out: the API stays, the TLS stack never links.
// -----------------------------------------------------------------------------
void tg_begin(void)                     { }
bool tg_queue(MsgId id, Priority prio)  { (void)id; (void)prio; return false; }
void tg_service(void)                   { }
void tg_set_mode(TgMode mode)           { (void)mode; }

#endif // FEATURE_TELEGRAM
#endif // !NT_TG_PURE_TEST
