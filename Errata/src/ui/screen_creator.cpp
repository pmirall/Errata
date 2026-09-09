// =============================================================================
//  ERRATA - ui/screen_creator.cpp
//  CREATOR, migrated by P2-C11c. PURE translation unit.
// =============================================================================
#include "screen_creator.h"

#include <stdio.h>
#include <string.h>

#include "../core/strings_es.h"
#include "../data/sprites.h"
#include "gfx.h"
#include "qr.h"
#include "qr_paint.h"
#include "screen.h"
#include "ui.h"

// The right-hand text column. The symbol box owns columns 0..61.
#define CR_COL_X   (QR_BOX_SIZE + 3)              // 65
#define CR_COL_W   (OLED_W - CR_COL_X - 1)        // 62

// How long one symbol stays up while the provisioning AP alternates the two,
// and how long a manual tap pins the choice.
// CR_ALTERNATE_MS and CR_MANUAL_MS were here: five seconds per symbol and a
// ten-second pin after a manual flip. There is one symbol now, so there is
// nothing to alternate and nothing to pin.
#define CR_REBUILD_MS    1000UL

// The spec section 34 stack, as BASELINES in the right-hand column. Named
// because the two that matter are load-bearing and a magic number in the middle
// of a render function is where a layout silently walks off a 64-row panel:
//   CR_ROW_PIN_VAL is a GF_BIG baseline and that face has a 16 px ascent, so it
//   occupies rows CR_ROW_PIN_VAL-15 .. CR_ROW_PIN_VAL; and
//   CR_ROW_HINT + 1 must stay above UI_AFFORD_Y or the last line lands in the
//   affordance strip.
// The static_asserts below are what hold both, so the panel is checked at
// compile time and not only by tests/golden/screens/creator_portal.pbm.
#define CR_ROW_SCAN         7
#define CR_ROW_WHAT        15
#define CR_ROW_PIN_LBL     24
#define CR_ROW_PIN_VAL     44
#define CR_ROW_HINT        53
#define CR_ROW_WAIT        26      // the "not up yet" branch: one block, no stack

static_assert(CR_ROW_PIN_VAL - GFX_ASC_BIG + 1 > CR_ROW_PIN_LBL,
              "the big PIN digits overlap the PIN label");
static_assert(CR_ROW_HINT - GFX_ASC_BODY + 1 > CR_ROW_PIN_VAL,
              "the hint line overlaps the big PIN digits");
static_assert(CR_ROW_HINT < UI_AFFORD_Y,
              "the hint line lands in the affordance strip");
static_assert(CR_ROW_SCAN - GFX_ASC_BODY + 1 >= 0, "the headline is off the top of the panel");
static_assert(CR_ROW_WAIT + 2 * GFX_LINE_BODY < UI_AFFORD_Y,
              "the three wrapped \"Conectando...\" lines reach the affordance strip");

static uint8_t  s_mod[QR_BUF_BYTES];
static uint8_t  s_size     = 0;
static uint32_t s_build_ms = 0;
static uint32_t s_open_ms  = 0;
static char     s_key[CREATOR_TEXT_MAX]; // the payload the cached symbol encodes

// THE EXACT BYTES THE SYMBOL ON SCREEN ENCODES. Exported for the same reason
// creator_variant() is - a host test has no scanner - but it buys something
// creator_variant() cannot: spec section 39 says the QR carries no secret, and
// the only way to hold that is to read the payload back and look. The gate in
// tools/check.sh greps src/networking for a formatted query parameter, which
// catches any URL under src/networking growing a "?k=" and CANNOT see this
// file at all: a
// PIN appended HERE, in the one function that decides what is encoded, would
// pass every check in the tree. tests/test_screens.cpp's
// creator_payload_carries_no_pin_for_any_pin is what closes that, and this is
// the seam it reads through. Empty when nothing has been encoded.
const char* creator_payload(void) { return s_key; }

// -----------------------------------------------------------------------------
//  THE PAYLOAD
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
//  ONE SYMBOL, AND IT IS THE ONE THAT JOINS THE NETWORK.
//
//  This screen used to show two in turn: "join this network" for five seconds,
//  then "open the page" for five, with A to flip between them. That is one
//  symbol too many, and the owner said so from the bench: JOINING THE NETWORK
//  IS ALREADY THE WHOLE JOURNEY. networking/net.cpp runs a DNSServer over the
//  access point and networking/webui.cpp answers the catch-all, so a phone that
//  joins fires its own captive-portal probe and opens the page by itself. The
//  second symbol pointed at a page the first symbol had already opened.
//
//  What a rotating symbol cost, which is the part that does not show up in a
//  screenshot: a person holding a phone at a QR has to notice that the picture
//  under the camera changed while they were aiming at it, and the two symbols
//  are indistinguishable at a glance. Half the time you scan the wrong one, and
//  the wrong one is a URL for a network you have not joined yet - so it fails,
//  and it fails in a way that looks like the device is broken rather than like
//  you were early.
// -----------------------------------------------------------------------------
static void build(const CreatorInfo& in) {
  char text[CREATOR_TEXT_MAX];
  text[0] = '\0';

  if (in.ap_up) {
    // OPEN NETWORK, AND P8-C2 DECIDED THAT RATHER THAN INHERITING IT.
    //
    // "WIFI:S:ERRATA-A1B2;;" is 23 B and fits QR version 2 (25 modules ->
    // 25*2 + 2*3*2 = 62 px at 2 px/module, exactly QR_BOX_SIZE). Every addition
    // breaks it: "T:nopass" alone makes it 35 B, which forces version 3 -> 29
    // modules -> 70 px, and 70 does not fit in 64 rows.
    //
    // THE PLAN ASKED FOR A GENERATED ConfigV2.ap_pass AND A WPA ACCESS POINT.
    // The arithmetic refuses it, for every passphrase length and every SSID
    // this product may use:
    //     "WIFI:T:WPA;S:" 13 + SSID 14 + ";P:" 3 + pass + ";;" 2
    //   = 32 B of fixed text before a single passphrase character, against a
    //     32 B version-2-L byte budget (ui/qr.cpp: data_cw - 2).
    //   WPA2-PSK's own minimum passphrase is 8 characters, so the shortest
    //   legal payload is 40 B -> version 3 -> 29 modules -> 70 px on a 64-row
    //   panel. Dropping the "-XXXX" suffix does not save it (35 B), and
    //   dropping the "ERRATA-" prefix would break decision D3.
    // Turning WPA on anyway would have produced TWO silent failures no host
    // test can see: draw_symbol() clamps px to 1 and paints a 35 px symbol at
    // one pixel per module on a 0.96" panel, and snprintf() here truncates the
    // payload to CREATOR_TEXT_MAX-1 = 39 characters without reporting it, so
    // the symbol would encode a valid-looking WIFI: string with a mangled
    // passphrase.
    // So the access point stays OPEN and the PIN stays the authorisation
    // layer, which is what spec section 39 asks for in as many words ("Do not
    // put secrets into the QR beyond what is necessary. The PIN remains the
    // user-facing authorization layer"). What makes that safe is not the
    // network: it is that the portal exists only while this screen is open,
    // that it dies after ConfigV2.creator_idle_s, and that the PIN gate locks
    // for 60 s after five failures - which bounds an in-range attacker to
    // about nine guesses per portal session out of 10,000.
    // Reopening this needs a bench call (a redesigned CREATOR layout, or the
    // join symbol dropped for on-screen text), not a code change.
    snprintf(text, sizeof(text), "WIFI:S:%s;;", in.ssid);
  }

  if (text[0] == '\0') { s_size = 0; s_key[0] = '\0'; return; }
  if (strcmp(text, s_key) == 0) return;              // already cached

  uint8_t size = 0;
  if (qr_encode(text, s_mod, size)) {
    s_size = size;
    snprintf(s_key, sizeof(s_key), "%s", text);
  } else {
    s_size   = 0;
    s_key[0] = '\0';
  }
}

// -----------------------------------------------------------------------------
//  HOOKS
// -----------------------------------------------------------------------------
// THE RADIO HOLD. Set while this screen owns the access point and cleared by
// the ONE teardown, so it can never outlive the portal it speaks for. See the
// long note in screen_creator.h: without it the ladder took the AP down at
// 120 s and D7's 300 s timeout could never be reached, let alone observed.
static uint8_t s_holding = 0;

bool creator_screen_busy(void) { return s_holding != 0u; }

void creator_enter(void) {
  // The screen that wants the station is the screen that asks for it; there is
  // no radio policy in the entry point any more (plan section 2 row G4). It is
  // released again in creator_leave().
  ui_creator_radio(true);
  s_holding = 1u;                     // the ladder is clamped at DIM from here

  CreatorInfo in;
  ui_creator_info(in);
  s_key[0]   = '\0';
  s_size     = 0;
  s_build_ms = ui_now_ms();
  s_open_ms  = s_build_ms;
  build(in);
}

void creator_leave(void) {
  // Radio OFF by default: this screen is the only owner of the station, so
  // leaving it gives back the ~50 KB of heap and the largest current draw on
  // the board instead of holding the radio powered until the next reboot.
  ui_creator_radio(false);
  s_holding = 0u;                     // ...and released here, once, with it
  s_size   = 0;
  s_key[0] = '\0';
}

void creator_update(uint32_t now_ms) {
  if ((uint32_t)(now_ms - s_build_ms) < CR_REBUILD_MS) return;
  s_build_ms = now_ms;

  CreatorInfo in;
  ui_creator_info(in);

  // ---- THE TWO EXITS (P8-C2) ------------------------------------------------
  // This screen is SF_STICKY, so invariant 3's 20 s auto-return does not apply
  // and these are the only ways out other than a B press. Both call ui_back(),
  // which runs creator_leave() - the ONE teardown - so a timeout and a button
  // leave the device in the same state.
  //
  // 1. The access point never came up. spec section 47: every radio wait has an
  //    exit. net_request_portal() can fail outright (NERR_AP_FAILED, or a scan
  //    holding the radio), and without this the user would sit on "Conectando"
  //    with the radio drawing current until they pressed a button.
  // 2. The portal has been idle for ConfigV2.creator_idle_s (decision D7,
  //    default 300 s). "Idle" means no request that PASSED THE PIN GATE:
  //    networking/creator_gate.cpp is deliberate about that, because otherwise
  //    anyone in radio range could hold the access point up forever by fetching
  //    one unauthenticated URL every 299 s.
  if (!in.ap_up) {
    if ((uint32_t)(now_ms - s_open_ms) >= CREATOR_AP_WAIT_MS) { ui_back(); return; }
  } else if (in.idle_expired) {
    ui_back();
    return;
  }

  // The alternation was here. build() answers one payload now, and it caches on
  // the payload itself, so a symbol that has not changed is not re-encoded.
  build(in);
}

// THIS SCREEN OWNS NO BUTTON NOW. A flipped between the two symbols; there is
// one symbol, so A has nothing to mean here and does nothing rather than doing
// something invented to keep it busy. B is BACK and the router owns it.
//
// The `void` cast is not laziness: the hook is in the screen table and the
// table's signature is fixed.
void creator_input(Gesture g) { (void)g; }

// -----------------------------------------------------------------------------
//  DRAWING
//
//  qr_draw() is fenced behind #ifdef ARDUINO because it speaks u8g2 directly,
//  so the symbol is painted here through gfx.h instead - same composition
//  (lit paper, cleared dark modules, horizontal runs merged) and one less
//  thing that only exists on the target.
// -----------------------------------------------------------------------------
// The painter moved to ui/qr_paint.cpp when the MANUAL screen needed the same
// picture. What is left here is the box this screen reserves for it.
static void draw_symbol(void) {
  qrp_paint(s_mod, s_size, QR_BOX_X, QR_BOX_Y, QR_BOX_SIZE);
}

void creator_render(void) {
  CreatorInfo in;
  ui_creator_info(in);

  draw_symbol();

  const int16_t rx = CR_COL_X;
  const int16_t rw = CR_COL_W;

  // ---- THE SPEC SECTION 34 SCREEN -------------------------------------------
  //
  //      SCAN ME / [QR CODE] / PIN: 1234 / Scan with your phone
  //
  // and, in the same breath, "do not clutter this screen with unrelated UI".
  // The four lines are stacked in the 62 px column beside the symbol because
  // the symbol is 62 px tall on a 64-row panel and there is nowhere else for
  // them to go; the ORDER and the CONTENT are the spec's.
  //
  // WHAT WAS REMOVED TO GET THERE, because a removal is the part of a layout
  // change nobody can see afterwards:
  //   * "Creador - Fase 8" (STR_CREATOR_PHASE, now deleted). It said the page
  //     this screen points at did not exist yet. It does.
  //   * "Conectate a la red:" above the SSID. The line under ESCANEAME is the
  //     SSID or the address, and which one it is says which symbol is up.
  //   * The IP printed alongside the SSID at all times. Both were on screen
  //     together while the symbol could only be one of them, so the pair
  //     contradicted the picture every five seconds.
  //
  // THE PIN IS DRAWN HERE AND IS NOT IN THE SYMBOL (spec section 39). A QR is
  // photographed, forwarded and posted; a four-digit number a person reads off
  // a screen they are standing in front of is the authorisation layer. build()
  // above encodes the join string and nothing else, and
  // tests/test_screens.cpp's creator_payload_carries_no_pin_for_any_pin holds
  // that for all 9,999 PINs cg_mint_pin() can produce.
  if (in.ap_up) {
    gfx_text_fit(GF_BODY, rx, CR_ROW_SCAN, rw, S(STR_CREATOR_SCAN));

    // WHAT THE SYMBOL DOES, in the words of the thing it encodes: the network
    // it joins. It used to be the SSID or the address depending on which symbol
    // was up; there is one symbol, so there is one caption.
    //
    // IT EARNS ITS ROW BY BEING THE FALLBACK. If the camera will not read the
    // code - a scratched panel, a phone that refuses WIFI: payloads - this line
    // is how the owner joins by hand from the phone's own Wi-Fi list, and the
    // captive portal then opens the page exactly as it would have. GF_TINY is
    // the ASCII-only 4x6 and the SSID is ASCII by construction (AP_SSID_PREFIX
    // plus hex), which is the one thing that font is for.
    gfx_text_fit(GF_TINY, rx, CR_ROW_WHAT, rw, in.ssid);

    gfx_text_fit(GF_BODY, rx, CR_ROW_PIN_LBL, rw, S(STR_WEB_PIN));

    // THE DIGITS AT 9x19 SO THEY CAN BE READ AT ARM'S LENGTH, which is the
    // posture this screen is used in: the device is on the table and the phone
    // is in your hands. GF_BIG is logisoso16_tn - DIGITS ONLY, 18 glyphs - so
    // the sentinel is NOT drawn in it: web_pin() answers 0 for "none issued
    // yet" and "----" in a digits-only face is four missing glyphs. The body
    // font draws the sentinel instead, exactly as ui_info_lines() does on the
    // DIAG screen, and for the same reason: four zeros would be a screen
    // stating a PIN that does not exist.
    if (in.pin == 0u) {
      gfx_text_fit(GF_BODY, rx, CR_ROW_PIN_VAL, rw, "----");
    } else {
      char pin[CREATOR_PIN_DIGITS + 1];
      snprintf(pin, sizeof(pin), "%04u", (unsigned)(in.pin % (unsigned)WEB_PIN_MAX));
      gfx_text_fit(GF_BIG, rx, CR_ROW_PIN_VAL, rw, pin);
    }

    gfx_text_fit(GF_BODY, rx, CR_ROW_HINT, rw, S(STR_CREATOR_WITH_PHONE));
  } else {
    // The station branch was here: address, PIN label, big PIN. It is gone with
    // the station itself (P5-C1). Two states remain - the access point is up,
    // or it is not yet - and both are reachable. There is nothing to scan
    // before it comes up, so this branch says what it is doing and nothing
    // else; CREATOR_AP_WAIT_MS is what stops it saying it forever.
    gfx_text_fit(GF_BODY, rx, CR_ROW_SCAN, rw, S(STR_CREATOR_TITLE));
    gfx_text_wrap(GF_BODY, rx, CR_ROW_WAIT, rw, GFX_LINE_BODY, 3, S(STR_WEB_CONNECTING));
  }

  // Invariant 6, relocated: the symbol box owns the affordance rows, so the
  // hint lives in the right column's bottom strip instead.
  {
    const SpriteRef l = sprite_mini(MIC_ARROW_L);
    gfx_xbm(rx, UI_AFFORD_Y, l.w, l.h, l.bits);
    gfx_text_fit(GF_BODY, (int16_t)(rx + 10), (int16_t)(UI_AFFORD_Y + GFX_ASC_BODY),
                 40, S(STR_AF_BACK));
    // The right-hand arrow was here and it advertised the flip. An affordance
    // for a button that does nothing is worse than no affordance: it is a
    // promise the screen cannot keep.
  }
}
