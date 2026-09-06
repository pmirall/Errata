// =============================================================================
//  PEBBLEBOL - networking/creator_server.cpp
//  The spec-38 routes. See creator_server.h for what is trusted, what is not,
//  and why the page's own checks are a courtesy rather than a control.
//
//  NOTHING IN THIS FILE DECIDES A RULE. The cap is creator_body.cpp's, the
//  document shape is creator_parse.cpp's, the game rules are game/validate.cpp's
//  and the PIN is creator_gate.cpp's - four pure modules a host binary drives
//  case by case. What is here is transport, in the order every handler runs it:
//
//      rate limit -> render hint -> PIN -> body verdict -> parse -> validate
//      -> (write) -> answer -> release the body
//
//  BANNED HERE, ON PURPOSE, exactly as in webui.cpp:
//    - the 3-arg send_P  (WebServer.cpp:619 strlen_P()s the blob)
//    - the 1-arg sendContent_P (WebServer.cpp:663, same bug)
//    - String concatenation in a handler: every body is built into a fixed
//      char buffer whose worst case is provable at compile time.
// =============================================================================
#include "creator_server.h"

#if FEATURE_WEB

#include <Arduino.h>
#include <WebServer.h>
#include <uri/UriBraces.h>          // for the Uri base class the catch-all needs

#include <stdio.h>
#include <string.h>

#include "../core/config.h"
#include "../core/rng.h"
#include "../core/version.h"
#include "../data/creator_schema.h"
#include "../data/creator_schema_json.h"
#include "../game/box.h"
#include "../game/genome.h"
#include "../game/species_custom.h"
#include "../game/validate.h"
#include "../hardware/gametime.h"
#include "../persistence/game_state.h"
#include "../persistence/save_manager.h"
#include "creator_body.h"
#include "creator_parse.h"
#include "webui.h"

// =============================================================================
//  1. STATE
// =============================================================================

// THE 2 KB BUFFER THIS CHUNK IS ABOUT. One per firmware, not one per request:
// WebServer serves one client at a time (WebServer.h:3) and there is exactly
// one server object, so a second buffer would be 2 KB of .bss nothing can use.
static CreatorBody s_body;

// The server, bound at cs_register(). webui.cpp owns the object; this module
// only ever needs to read a header and write a response through it.
static WebServer* s_srv = nullptr;

// Response scratch. SEPARATE from webui.cpp's 320 B: sharing it would mean two
// files writing one buffer through calls that can nest (a handler that builds a
// response and then calls send_err on a later failure), which is the shape of
// bug that only shows up in the one path nobody tested.
static char s_out[CS_OUT_BUF];

// =============================================================================
//  2. RESPONSE PRIMITIVES
// =============================================================================

static void send_json(int code, const char* body)
{
  s_srv->sendHeader(F("Cache-Control"), F("no-store"));
  s_srv->send(code, "application/json", body);
}

// {"err":"xxx"} - the same shape webui.cpp answers with, so a client has one
// error format to read and not two.
static void send_err(int code, const char* err)
{
  snprintf(s_out, sizeof(s_out), "{\"err\":\"%s\"}", err);
  send_json(code, s_out);
}

// Copies a device-owned string into a JSON string value, dropping every byte
// that could change the SHAPE of the document.
//
// THIS IS OUTPUT HYGIENE AND IT IS NOT PARANOIA ABOUT OUR OWN CONFIG. The
// device name is typed on the device, but it reaches this function through a
// 256 B blob whose only guarantee is a CRC - and a name carrying a quote would
// produce a response the page's JSON.parse() refuses, which is a bug reported
// as "the creator does not work" long after anyone remembers why.
static void json_copy_name(char* dst, size_t cap, const char* src)
{
  size_t o = 0;
  for (size_t i = 0; src && src[i] != '\0' && i < (size_t)NAME_MAX_LEN; ++i) {
    const uint8_t ch = (uint8_t)src[i];
    if (ch == '"' || ch == '\\') continue;
    if (!creator_name_char_ok(ch)) continue;
    if (o + 1 >= cap) break;
    dst[o++] = (char)ch;
  }
  dst[o] = '\0';
}

// =============================================================================
//  3. THE BODY
// =============================================================================

// True when the request declared a multipart body. THE CHECK EXISTS BECAUSE THE
// SAME FUNCTION IS THE UPLOAD HOOK: Parsing.cpp:163-174 sets isForm for
// "multipart/", which takes the body down _parseForm() instead of the raw path,
// and _parseForm calls the identical _ufn through upload() (Parsing.cpp:500) -
// where _currentRaw is null and WebServer::raw() dereferences it with no check.
// A POST with `Content-Type: multipart/form-data; boundary=b` would therefore
// be a null dereference any client could ask for.
static bool body_is_multipart(void)
{
  const String ct = s_srv->header(F("Content-Type"));
  return ct.startsWith(F("multipart/"));
}

void cs_body_hook(void)
{
  if (s_srv == nullptr) return;

  if (body_is_multipart()) {
    // NOT server.raw(). Reset so this request cannot be handed the PREVIOUS
    // request's bytes, and leave the state at CB_IDLE, which every handler
    // answers 415.
    cb_reset(s_body);
    return;
  }

  const HTTPRaw& raw = s_srv->raw();
  switch (raw.status) {
    case RAW_START:
      cb_start(s_body, s_srv->clientContentLength());
      // THE ONLY WAY OUT OF THE CORE'S READ LOOP. Parsing.cpp:192 loops until
      // totalSize == Content-Length and the handler cannot break it; closing
      // the socket makes the next readBytes() return 0, which the core turns
      // into RAW_ABORTED and a dropped client. It costs the 413 - there is
      // nothing left to write it to - and that trade is why CS_BODY_DRAIN_MAX
      // exists: below it the body is drained so a polite answer is still
      // possible, above it the connection is not worth the seconds.
      if (cb_state(s_body) == CB_ABORT) s_srv->client().stop();
      break;

    case RAW_WRITE:
      cb_write(s_body, raw.buf, (uint32_t)raw.currentSize);
      break;

    case RAW_END:
      cb_end(s_body);
      break;

    case RAW_ABORTED:
    default:
      cb_abort(s_body);
      break;
  }
}

bool cs_body_answered(void)
{
  switch (cb_state(s_body)) {
    case CB_COLLECT:
      if (cb_ready(s_body)) return false;      // the one path that continues
      send_err(400, "short");
      return true;

    case CB_NO_LENGTH:
      // 411, and it is a real distinction rather than pedantry: a chunked body
      // and an absent Content-Length both leave the core's read loop unrun, so
      // the handler is offered a legitimate-looking EMPTY upload. Answering 400
      // would tell the page its JSON was wrong when its framing was.
      send_err(411, "len");
      return true;

    case CB_TOO_LARGE:
      snprintf(s_out, sizeof(s_out), "{\"err\":\"big\",\"max\":%u}",
               (unsigned)CS_BODY_MAX);
      send_json(413, s_out);
      return true;

    case CB_TRUNCATED:
      send_err(400, "short");
      return true;

    case CB_ABORT:
      // The socket is gone. Writing to it would be a no-op at best; say nothing.
      return true;

    case CB_IDLE:
    default:
      // No raw phase ran at all: a multipart POST, which this server has no
      // route for and will not grow one - the creator sends one JSON document.
      send_err(415, "type");
      return true;
  }
}

const uint8_t* cs_body_data(void) { return cb_data(s_body); }
uint16_t       cs_body_len(void)  { return cb_len(s_body); }
void           cs_body_done(void) { cb_reset(s_body); }

// =============================================================================
//  4. THE SHARED PROLOGUE
//
//  Every handler starts the same way and the ORDER is deliberate:
//
//   1. THE RATE LIMIT IS FIRST, and note_request() comes after it. P8-C2
//      recorded this as an open defect: h_root and h_notfound called
//      note_request() before rate_take(), so a client being REFUSED still
//      dropped the renderer to FPS_LOW - a permanent display degradation
//      available to anyone in radio range for the price of a throttled request.
//      A request that is answered 429 now costs the display nothing.
//   2. THE RENDER HINT IS SECOND, before the PIN gate rather than after it. A
//      user mistyping their PIN IS using the device, and the hint only says
//      "somebody is talking to us over the web".
//   3. THE PIN GATE IS THIRD, and it answers the client itself on refusal.
// =============================================================================
static bool prologue(uint8_t cost, bool need_pin)
{
  if (!web_rate_take(cost)) { web_send_throttled(); return false; }
  web_note_request();
  if (need_pin && !web_pin_ok()) return false;    // web_pin_ok() has answered
  return true;
}

// THE PIN FOR A ROUTE THAT CARRIES A BODY, IN TWO HALVES, because the PIN can
// arrive in two places and only one of them is readable before the parse.
//
//   pin_before_body()  the X-Pin header, checked BEFORE a byte of the document
//                      is interpreted. When there is no header it defers.
//   pin_after_body()   the body's "pin" field, checked once the document has
//                      been read. When a header WAS present it is already done.
//
// Exactly one of the two actually gates any given request, and both answer the
// client themselves on refusal.
//
// THE ORDERING IS THE WHOLE ANSWER TO "why is an unauthenticated body parsed at
// all". A client that sends X-Pin is gated before one byte is interpreted. A
// client that does not is parsed first - and that costs a bounded walk over at
// most CS_BODY_MAX bytes by a reader that allocates nothing, cannot recurse and
// has no state, over bytes the core has already pulled off the wire whatever we
// decide. The failure still counts either way, so the lockout still bounds
// guessing at the same rate.
//
// P8-C1 recorded the body-carried PIN as a deviation - "the 'or JSON field'
// half is not wired" - because the only route it owned deliberately carries no
// body. This is that half.
static bool pin_before_body(void)
{
  return !web_pin_present() || web_pin_ok();
}

static bool pin_after_body(uint16_t body_pin)
{
  return web_pin_present() || web_pin_ok_u16(body_pin);
}

// =============================================================================
//  5. HANDLERS
// =============================================================================

// ---- GET /api/schema --------------------------------------------------------
//
//  UNGATED, and creator_server.h argues it: the document is compiled constants,
//  byte-identical on every device running this firmware, and the page needs it
//  before the user has typed anything. It therefore does NOT extend the portal
//  either - cg_verify() is the only thing that moves last_seen_ms.
static void h_schema(void)
{
  if (!prologue(WEB_COST_READ, false)) { cs_body_done(); return; }
  s_srv->sendHeader(F("Cache-Control"), F("no-store"));
  // FOUR-arg send_P. The 3-arg form strlen_P()s the blob (WebServer.cpp:619).
  s_srv->send_P(200, PSTR("application/json"),
                CREATOR_SCHEMA_JSON, CREATOR_SCHEMA_JSON_LEN);
  cs_body_done();
}

// THE WIDEST RESPONSE IN THIS FILE, AND NOW A COMPILE-TIME BOUND RATHER THAN A
// SENTENCE. This file's banner says "every body is built into a fixed char
// buffer whose worst case is provable at compile time"; until the phase-8 exit
// no such proof existed - core/config.h carried a prose estimate ("about 160
// characters") and nothing checked it. The bound matters because it moves with
// something a later phase WILL touch: FW_VERSION is a literal in
// core/version.h, and phase 9's tag and phase 10's both grow it. The failure
// mode is not a crash: snprintf truncates, the page's JSON.parse() refuses the
// fragment, and the bench reports "the creator does not work".
//
// THE ARITHMETIC IS AN OVER-ESTIMATE ON PURPOSE, so it needs no duplicate of
// the fixed text (which would be a second source of truth that drifts). Every
// conversion in this format is at least two characters ("%u", "%s", "%lu"), so
//     worst case  <=  strlen(fmt) + SUM over conversions of (widest - 2) + NUL
// and each `widest` below is the field's own bound, not a guess:
//   v        CREATOR_API_VERSION as unsigned .......  5 (any uint16)
//   fw       FW_VERSION ............................  sizeof - 1
//   content  CONTENT_VERSION as unsigned ...........  5 (uint16)
//   name     json_copy_name() into char[NAME_MAX_LEN+1]  NAME_MAX_LEN
//   box/cs   four uint8 counts .....................  3 each
//   body     CS_BODY_MAX as unsigned ...............  5 (uint16)
//   cal      gt_cal_state() ........................  5 (over-bounded)
//   epoch    uint32 decimal ........................ 10
//   ro       0 or 1 ................................  3 (over-bounded)
static const char CS_STATE_FMT[] =
  "{\"v\":%u,\"fw\":\"%s\",\"content\":%u,\"name\":\"%s\","
  "\"box\":{\"used\":%u,\"free\":%u},"
  "\"cs\":{\"used\":%u,\"free\":%u},"
  "\"body\":%u,\"cal\":%u,\"epoch\":%lu,\"ro\":%u}";

static constexpr size_t CS_STATE_WORST =
    (sizeof(CS_STATE_FMT) - 1u)          // the format, specifiers included
  + (5u - 2u)                            // v
  + ((sizeof(FW_VERSION) - 1u) - 2u)     // fw
  + (5u - 2u)                            // content
  + ((size_t)NAME_MAX_LEN - 2u)          // name
  + 4u * (3u - 2u)                       // box.used/free, cs.used/free
  + (5u - 2u)                            // body
  + (5u - 2u)                            // cal
  + (10u - 3u)                           // epoch, "%lu" is three characters
  + (3u - 2u)                            // ro
  + 1u;                                  // the NUL snprintf always writes

static_assert(CS_STATE_WORST <= (size_t)CS_OUT_BUF,
              "GET /api/state can no longer fit CS_OUT_BUF - a longer "
              "FW_VERSION or a wider field would be TRUNCATED, and a truncated "
              "JSON body is refused by the page with no error the device can "
              "see. Raise CS_OUT_BUF or shorten the response; do not ship it.");

// ---- GET /api/state ---------------------------------------------------------
//
//  PIN-GATED: free slots and the device name are this device's, not the
//  firmware's. Everything here is a COUNT or a VERSION; no Pebble is serialised,
//  because nothing in spec section 38 asks the page to render the Box and a
//  route that could would need a second, wider tokenizer to be worth having.
static void h_state(void)
{
  if (!prologue(WEB_COST_READ, true)) { cs_body_done(); return; }

  char name[NAME_MAX_LEN + 1];
  json_copy_name(name, sizeof name, gs_state().cfg.device_name);

  const uint8_t box_used = box_count();
  const uint8_t box_cap  = box_capacity();
  const uint8_t cs_used  = csp_count();

  snprintf(s_out, sizeof(s_out), CS_STATE_FMT,
           (unsigned)CREATOR_API_VERSION, FW_VERSION, (unsigned)CONTENT_VERSION,
           name,
           (unsigned)box_used, (unsigned)(box_cap - box_used),
           (unsigned)cs_used, (unsigned)(CREATOR_SPECIES_SLOTS - cs_used),
           (unsigned)CS_BODY_MAX, (unsigned)gt_cal_state(),
           (unsigned long)gt_now(), (unsigned)(gs_readonly() ? 1u : 0u));
  send_json(200, s_out);
  cs_body_done();
}

// ---- the shared upload pipeline ---------------------------------------------
//
//  POST /api/validate and POST /api/pebble read the SAME document and apply the
//  SAME rules; the only difference is whether anything is written. They share
//  this function so the two can never drift - a page told "yes" by /api/validate
//  and refused by /api/pebble is the worst outcome this feature has.
//
//  Returns true when `rec` is a definition this device accepts. On false it has
//  already answered the client.
static bool read_and_judge(CustomSpeciesRec& rec, uint16_t& stat_used,
                           uint16_t& attack_used)
{
  uint16_t body_pin = 0u;
  if (!pin_before_body()) return false;          // it has already answered

  const CpErr e = cp_parse_species(cs_body_data(), (uint32_t)cs_body_len(),
                                   rec, body_pin);
  if (!pin_after_body(body_pin)) return false;   // it has already answered

  if (e != CP_OK) {
    // The NAMED code, not "bad request". A page that is told CP_SPRITE_LEN can
    // say which field is wrong; a page that is told 400 can only shrug.
    snprintf(s_out, sizeof(s_out), "{\"err\":\"parse\",\"why\":\"%s\"}",
             cp_err_name(e));
    send_json(400, s_out);
    return false;
  }

  // budget_used is RECOMPUTED, never taken from the document: creator_parse.cpp
  // leaves it zero and validate_custom_species() refuses a disagreement, so a
  // page cannot price its own Pebble.
  creator_cost_of(rec, stat_used, attack_used);
  rec.budget_used = attack_used;

  const VReject r = validate_custom_species(rec);
  if (r != VR_OK) {
    snprintf(s_out, sizeof(s_out), "{\"err\":\"invalid\",\"why\":\"%s\"}",
             validate_reject_name(r));
    send_json(422, s_out);
    return false;
  }
  return true;
}

// ---- POST /api/validate -----------------------------------------------------
//
//  WRITES NOTHING, and it reports the free slots so the page can say "free a
//  Box slot" BEFORE the user has spent five minutes on a sprite. That is
//  game/capture.h's rule applied here: capture.cpp checks the Box before it
//  rolls, "if the Box is full the player must decide", because discovering it
//  afterwards spends the attempt.
static void h_validate(void)
{
  if (!prologue(WEB_COST_READ, false)) { cs_body_done(); return; }
  if (cs_body_answered()) { cs_body_done(); return; }

  CustomSpeciesRec rec;
  uint16_t stat_used = 0u, attack_used = 0u;
  if (!read_and_judge(rec, stat_used, attack_used)) { cs_body_done(); return; }

  const uint8_t box_free = (uint8_t)(box_capacity() - box_count());
  const uint8_t cs_free  = (uint8_t)(CREATOR_SPECIES_SLOTS - csp_count());
  snprintf(s_out, sizeof(s_out),
           "{\"ok\":1,\"pct\":%u,\"stat\":%u,\"atk\":%u,"
           "\"box\":%u,\"cs\":%u}",
           (unsigned)creator_power_pct(stat_used, attack_used),
           (unsigned)stat_used, (unsigned)attack_used,
           (unsigned)box_free, (unsigned)cs_free);
  send_json(200, s_out);
  cs_body_done();
}

// ---- POST /api/pebble -------------------------------------------------------
//
//  THE DEVICE ASKS, IT NEVER OVERWRITES. Both capacities are checked BEFORE a
//  byte is written and both refusals write NOTHING - not the cs record either,
//  because an accepted definition whose Pebble could not be filed would leak a
//  cs slot the user has no way to reclaim.
//
//  THE SLOT IS THE DEVICE'S CHOICE AND THE PAGE IS TOLD WHICH ONE. A
//  page-chosen slot is an attacker-chosen slot, and writing cs4 would silently
//  change the species of a Pebble already in the Box.
static void h_pebble(void)
{
  if (!prologue(WEB_COST_MUTATE, false)) { cs_body_done(); return; }
  if (cs_body_answered()) { cs_body_done(); return; }

  CustomSpeciesRec rec;
  uint16_t stat_used = 0u, attack_used = 0u;
  if (!read_and_judge(rec, stat_used, attack_used)) { cs_body_done(); return; }

  // A READ-ONLY SESSION IS A SAVE THE USER STILL OWNS (audit risk 3): the load
  // refused to touch flash and the pet in RAM is a placeholder. Writing a
  // creator Pebble over that is exactly the silent overwrite gs_set_readonly()
  // exists to stop.
  if (gs_readonly()) { send_err(503, "ro"); cs_body_done(); return; }

  // AN EMPTY BOX IS REFUSED, and the reason is the simulation rather than the
  // Box. Filing the first Pebble moves box_active() from BOX_ACTIVE_NONE to a
  // real slot (invariant B3), and game/sim.cpp is bound to the ACTIVE Pebble by
  // app.cpp's bind_active() - which runs once, at boot. Every screen that can
  // reach CREATOR is downstream of a device that already has its starter, so
  // this is unreachable in the shipping flow; it is here because "unreachable"
  // is a claim about today's navigation and not about the code.
  if (box_count() == 0u) { send_err(409, "nopet"); cs_body_done(); return; }

  const uint8_t cs_slot = csp_free_slot();
  if (cs_slot == (uint8_t)CSP_SLOT_NONE) { send_err(409, "csfull"); cs_body_done(); return; }
  if (box_count() >= box_capacity())     { send_err(409, "boxfull"); cs_body_done(); return; }

  // NOTHING REACHES FLASH UNTIL BOTH HALVES ARE KNOWN GOOD. The registry is
  // RAM, so installing first costs nothing to undo, and it is what lets the
  // creature be built and judged before a single write. The earlier shape wrote
  // the cs record first and would have leaked a slot on any failure after it -
  // an abandoned upload the user has no way to reclaim.
  rec.slot = cs_slot;
  custom_species_seal(rec);                    // magic/version/CRC, save_manager's
  if (!csp_install(rec)) { send_err(500, "install"); cs_body_done(); return; }

  const uint8_t species_id = csp_species_id(cs_slot);
  const uint8_t slot = box_new_pebble(species_id, 1u, (uint8_t)ORIGIN_CREATOR,
                                      genome_genesis(), rng_u32(RNG_MISC),
                                      gt_now());
  PebbleInstance* p = (slot == (uint8_t)BOX_SLOT_NONE) ? nullptr : box_slot(slot);
  if (p == nullptr) {
    csp_forget(cs_slot);
    send_err(500, "box");
    cs_body_done();
    return;
  }

  // The two fields box_new_pebble() cannot know, set at the CONSTRUCTOR's own
  // moment rather than mended afterwards: the constructor takes a species id and
  // a level, and a cs slot is neither. validate_pebble() below is what checks
  // the result, and its VR_BAD_CUSTOM_SPRITE rule holds both directions of the
  // flag/slot agreement.
  p->flags         = (uint8_t)(p->flags | PBF_CUSTOM | PBF_HAS_CUSTOM_SPRITE);
  p->custom_sprite = cs_slot;
  // The creator's name is the CREATURE's name: a custom species has no StrId to
  // point at, so game/species_custom.cpp leaves name_idx empty and the nickname
  // is where the user's twelve characters live. Both fields are the same size
  // and rec.name is NUL-terminated inside it (validate_custom_species checks
  // that by name), so this copies the terminator with the text.
  static_assert(sizeof(((PebbleInstance*)nullptr)->nickname) == CS_NAME_CAP,
                "the nickname and the creator name are no longer the same size: "
                "this copy would truncate or over-read");
  memcpy(p->nickname, rec.name, CS_NAME_CAP);

  // THE SAME ONE VALIDATOR THE WIRE AND THE SAVE PATH RUN (spec section 15).
  // Reaching it with anything but VR_OK is a BUG IN THIS TREE and never a
  // client lie - the definition already passed validate_custom_species() and
  // the registry resolved it - so it is answered 500, the slot is emptied and
  // NOTHING has been written to flash yet.
  const VReject vr = validate_pebble(*p);
  if (vr != VR_OK) {
    pebble_clear(*p);
    (void)box_active();          // invariant B3 re-derives the mask and active
    csp_forget(cs_slot);
    snprintf(s_out, sizeof(s_out), "{\"err\":\"internal\",\"why\":\"%s\"}",
             validate_reject_name(vr));
    send_json(500, s_out);
    cs_body_done();
    return;
  }

  // THE CONTENT BEFORE THE STATE, and the order is the power-cut order: a
  // Pebble on flash whose cs record is not there yet comes back quarantined
  // (VR_UNKNOWN_SPECIES), while a cs record whose Pebble is not there yet is
  // an orphan occupying one of ten slots and nothing worse. The window is one
  // flash write wide and the residue is bounded; freeing an orphaned slot is
  // P8-C4's CREATOR screen action.
  if (!save_custom_species(rec)) {
    pebble_clear(*p);
    (void)box_active();
    csp_forget(cs_slot);
    send_err(500, "flash");
    cs_body_done();
    return;
  }
  (void)gs_save_slot(slot, true);              // also commits the Box header

  snprintf(s_out, sizeof(s_out),
           "{\"ok\":1,\"slot\":%u,\"cs\":%u,\"species\":%u,\"id\":%lu,\"pct\":%u}",
           (unsigned)slot, (unsigned)cs_slot, (unsigned)species_id,
           (unsigned long)p->id,
           (unsigned)creator_power_pct(stat_used, attack_used));
  send_json(200, s_out);
  cs_body_done();
}

// ---- POST /api/time ---------------------------------------------------------
//
//  THE PHONE SETS THE CLOCK, AND EVERY DEADLINE IN THIS FEATURE WAS DESIGNED
//  AROUND THAT (networking/creator_gate.h, "THE CLOCK QUESTION"). The PIN
//  lockout and the portal idle timer are both monotonic, so a client that
//  pushes the clock forward moves neither by one millisecond. What it CAN move
//  is game time - and gt_set_epoch() owns that judgement, including the
//  rollback tolerance that stops a backwards jump.
static void h_time(void)
{
  if (!prologue(WEB_COST_MUTATE, false)) { cs_body_done(); return; }
  if (cs_body_answered()) { cs_body_done(); return; }

  uint32_t epoch = 0u;
  uint16_t body_pin = 0u;
  if (!pin_before_body()) { cs_body_done(); return; }

  const CpErr e = cp_parse_time(cs_body_data(), (uint32_t)cs_body_len(),
                                epoch, body_pin);
  if (!pin_after_body(body_pin)) { cs_body_done(); return; }

  if (e != CP_OK) {
    snprintf(s_out, sizeof(s_out), "{\"err\":\"parse\",\"why\":\"%s\"}",
             cp_err_name(e));
    send_json(400, s_out);
    cs_body_done();
    return;
  }

  const bool took = gt_set_epoch(epoch, CAL_PHONE);
  snprintf(s_out, sizeof(s_out),
           "{\"ok\":%u,\"cal\":%u,\"epoch\":%lu}",
           (unsigned)(took ? 1u : 0u), (unsigned)gt_cal_state(),
           (unsigned long)gt_now());
  // 200 when it landed, 409 when the clock refused it: an unbelievable date is
  // the client's problem to report, not something to swallow. gt_set_epoch()'s
  // own header lists the four reasons it says no.
  send_json(took ? 200 : 409, s_out);
  cs_body_done();
}

// =============================================================================
//  6. THE CATCH-ALL
//
//  A Uri subclass that matches everything, registered LAST so first-match-wins
//  gives every real route priority. UriGlob would have done it too and drags
//  fnmatch() in for a pattern that is one character.
// =============================================================================
namespace {
struct UriAny : public Uri {
  UriAny() : Uri("") {}
  Uri* clone(void) const override { return new UriAny(); }
  bool canHandle(const String&, std::vector<String>&) override { return true; }
};
}  // namespace

// =============================================================================
//  7. REGISTRATION
// =============================================================================
void cs_register(WebServer& srv, WebServer::THandlerFunction notfound)
{
  s_srv = &srv;
  cb_reset(s_body);

  srv.on("/api/schema",   HTTP_GET,  h_schema);
  srv.on("/api/state",    HTTP_GET,  h_state);
  // THE FOUR-ARG on() ON EVERY POST ROUTE. The fourth argument is what makes
  // canRaw() true (detail/RequestHandlersImpl.h:97-103), and canRaw() is what
  // routes the body through the core's fixed 1436 B buffer instead of
  // readBytesWithTimeout()'s malloc growth loop.
  srv.on("/api/validate", HTTP_POST, h_validate, cs_body_hook);
  srv.on("/api/pebble",   HTTP_POST, h_pebble,   cs_body_hook);
  srv.on("/api/time",     HTTP_POST, h_time,     cs_body_hook);

  // LAST, AND THE ORDER IS THE POINT. This is what closes the audit section 12
  // hole for every URI that is not one of the seven: without a registered
  // handler, Parsing.cpp:182's `_currentHandler` is null, the raw path is
  // skipped and `POST /anything` with a huge Content-Length goes straight down
  // the malloc growth loop. onNotFound() cannot do this job - it is consulted
  // at WebServer.cpp:819, after the body has already been read.
  srv.on(UriAny(), HTTP_ANY, notfound, cs_body_hook);
}

#endif  // FEATURE_WEB
