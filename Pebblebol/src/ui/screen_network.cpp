// =============================================================================
//  PEBBLEBOL - ui/screen_network.cpp
//  See screen_network.h. PURE translation unit.
// =============================================================================
#include "screen_network.h"

#include <stdio.h>
#include <string.h>

#include "../core/strings_es.h"
#include "../data/sprites.h"
#include "../game/box.h"
#include "../game/cooldowns.h"
#include "../game/encounters.h"
#include "../networking/wifi_scanner.h"
#include "gfx.h"
#include "screen.h"
#include "screen_encounter.h"
#include "ui.h"

// THE 136 B. One job, caller-owned by design, and this is the caller.
static WifiScanJob s_job;
static uint8_t     s_phase = (uint8_t)NSP_SCANNING;
static uint8_t     s_seen  = 0;
static uint8_t     s_fresh = 0;
static uint32_t    s_t0    = 0;      // for the spinner only

uint8_t network_screen_phase(void) { return s_phase; }
uint8_t network_screen_seen(void)  { return s_seen; }
uint8_t network_screen_fresh(void) { return s_fresh; }

// The level a wild creature is clamped around. An empty Box - which is only
// reachable if the player released everything - reads as 1 rather than 0, so
// the encounter clamp still has a legal centre.
static uint8_t active_level(void)
{
  const uint8_t slot = box_active();
  if (slot >= (uint8_t)BOX_SLOTS) return 1u;
  const PebbleInstance* p = box_peek(slot);
  return (p != nullptr && p->level >= 1u) ? p->level : 1u;
}

void network_enter(void)
{
  wifi_scan_reset(s_job);
  s_phase = (uint8_t)NSP_SCANNING;
  s_seen  = 0;
  s_fresh = 0;
  uint32_t now_ms = 0;
  ui_explore_clock(nullptr, &now_ms, nullptr);
  s_t0 = now_ms;
  if (!wifi_scan_start(s_job, ui_scan_driver(), now_ms))
    s_phase = (uint8_t)NSP_FAILED;
}

// -----------------------------------------------------------------------------
//  THE HAND-OFF. Called once, on the frame the scan answers.
//
//  ONE ENCOUNTER PER SCAN, not one per access point: the cooldown is what makes
//  exploring a reason to move (spec section 21), and rolling every network in
//  range at once would hand a flat block a dozen encounters. The FIRST network
//  that is off cooldown wins, which is scan order - the driver returns them
//  strongest-first, so it is also the one the player is nearest.
// -----------------------------------------------------------------------------
static void hand_off(uint32_t now_epoch, uint32_t now_ms, uint8_t cal)
{
  CdClock clk;
  clk.now_epoch = now_epoch;
  clk.now_ms    = now_ms;
  clk.cal       = cal;

  s_seen = s_job.count;
  for (uint8_t i = 0; i < s_job.count; ++i) {
    const ScanResult& r = s_job.res[i];
    if (r.net_hash == 0u) continue;               // never explorable, by design
    if (!cd_ready(ui_cooldowns(), r.net_hash, clk)) continue;
    s_fresh++;

    EncounterInput in;
    memset(&in, 0, sizeof in);
    in.net_hash     = r.net_hash;
    in.bucket       = encounter_bucket(now_epoch);
    in.device_seed  = ui_device_seed();
    in.category     = r.category;
    in.rssi         = r.rssi;
    in.active_level = active_level();
    in.progress     = box_count();

    EncounterResult enc;
    if (!encounter_roll(in, enc)) continue;

    // ARM THE COOLDOWN WHETHER OR NOT ANYTHING WAS FOUND. A network that
    // answered NOTHING has been explored; leaving it free would make re-scanning
    // until something turns up the optimal play, which is the farm section 21
    // exists to close.
    (void)cd_arm(ui_cooldowns(), r.net_hash, clk);
    ui_explore_commit();

    if (enc.outcome == (uint8_t)ENC_OUT_NOTHING) {
      s_phase = (uint8_t)NSP_EMPTY;
      ui_toast(STR_NET_NOTHING);
      ui_back();
      return;
    }
    encounter_arm(enc, r.category);
    s_phase = (uint8_t)NSP_HANDOFF;
    ui_push(SCR_ENCOUNTER);
    return;
  }

  // Nothing explorable: every network in range is still on cooldown, or there
  // were none at all. Both are ordinary and neither is an error.
  s_phase = (uint8_t)NSP_EMPTY;
  ui_toast(s_seen ? STR_NET_COOLING : STR_NET_EMPTY);
  ui_back();
}

void network_update(uint32_t now_ms)
{
  if (s_phase != (uint8_t)NSP_SCANNING) return;

  uint32_t now_epoch = 0, ms = now_ms;
  uint8_t  cal = 0;
  ui_explore_clock(&now_epoch, &ms, &cal);

  wifi_scan_service(s_job, ui_scan_driver(), ms);
  switch ((WifiScanState)s_job.state) {
    case WSCAN_RUNNING:
      ui_request_frame();          // the spinner has to keep turning
      break;
    case WSCAN_DONE:
      hand_off(now_epoch, ms, cal);
      break;
    case WSCAN_TIMEOUT:
    case WSCAN_FAILED:
      // Spec section 47: a failed scan is a recoverable state with a message,
      // never a screen that spins for ever. The radio is already off - the job
      // releases it on every exit path.
      s_phase = (uint8_t)NSP_FAILED;
      ui_toast(STR_NET_FAILED);
      ui_back();
      break;
    default:
      break;
  }
}

void network_input(Gesture g)
{
  // SF_OWNS_BACK, so B arrives here rather than as a plain navigation BACK.
  // Cancelling releases the radio; leaving without cancelling would not.
  if (g == (Gesture)GST_TAP_R) {
    wifi_scan_cancel(s_job, ui_scan_driver());
    s_phase = (uint8_t)NSP_CANCELLED;
    ui_back();
    return;
  }
  if (g == (Gesture)GST_BOTH) ui_help(STR_NET_HELP);
}

void network_leave(void)
{
  // THE BACKSTOP, and it is what makes "Wi-Fi shuts down after use" a property
  // of the screen and not of the player's route off it: LONG_BOTH goes HOME
  // without passing through network_input(), and the auto-return could too if
  // this row were ever made SF_STICKY.
  wifi_scan_cancel(s_job, ui_scan_driver());
  wifi_scan_reset(s_job);
}

// -----------------------------------------------------------------------------
//  THE FRAME
// -----------------------------------------------------------------------------
void network_render(void)
{
  gfx_header(S(STR_NET_TITLE), nullptr);

  const SpriteRef r = sprite_mini(MIC_WIFI);
  gfx_xbm((int16_t)((OLED_W - r.w) / 2), (int16_t)(UI_HDR_H + 2), r.w, r.h, r.bits);

  gfx_text_center(GF_NARR, 36, S(STR_NET_SCANNING));

  // A four-phase dot spinner on the elapsed time, so the screen is visibly
  // alive on a scan that takes four seconds. It is drawn from the job's own
  // clock rather than from a frame counter: at FPS_LOW a counter-driven
  // spinner stops when the panel does.
  uint32_t now_ms = 0;
  ui_explore_clock(nullptr, &now_ms, nullptr);
  const uint32_t el = (uint32_t)(now_ms - s_t0);
  const uint8_t  n  = (uint8_t)((el / 250u) % 4u);
  for (uint8_t i = 0; i < 4u; ++i) {
    const int16_t x = (int16_t)(OLED_W / 2 - 12 + i * 8);
    if (i <= n) gfx_fill(x, 45, 4, 4);
    else        gfx_rect(x, 45, 4, 4);
  }

  gfx_affordance(nullptr, S(STR_AF_CANCEL));
}
