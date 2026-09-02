// =============================================================================
//  NOTTAMAGOCHI - ui.h
//  The screen state machine S0..S15 (GAME_DESIGN 8.2 / 8.3), the navigation
//  grammar, the 20 s auto-return, the modal + alert layers, the on-device
//  minigames, the death staging and the inheritance hand-off.
//
//  LAYERING
//  --------
//  ui owns NO game state. Everything it shows comes from sim_save() /
//  sim_*() / ble_peer() / store_ancestor(); everything it changes
//  goes through sim_apply_action(), store_save_cfg() or net_request().
//  It never includes WiFi.h / BLEDevice.h / WebServer.h (net.h and
//  ble_social.h are deliberately network-header-free) and it never constructs
//  a U8G2: the single instance comes from rd_u8g2().
//
//  TIME
//  ----
//  ui reads millis() for PRESENTATION only - animation phase, modal lifetimes,
//  the death staging clock, hold progress bars. No game quantity is ever
//  derived from it; game time is sim_step_seconds() and gt_now(), as the
//  BRIEF 4 layering rule requires.
//
//  Identifiers and comments: English. Every user-facing byte: strings_es.h.
// =============================================================================
#ifndef NT_UI_H
#define NT_UI_H

#include <stdint.h>
#include <stddef.h>

#include "config.h"
#include "nt_types.h"

// -----------------------------------------------------------------------------
//  Geometry the module contracts against (render.h owns the strip constants).
// -----------------------------------------------------------------------------
#define UI_BODY_Y        SPRITE_AREA_Y                 //  9
#define UI_BODY_H        SPRITE_AREA_H                 // 47
#define UI_BODY_BOTTOM   (SPRITE_AREA_Y + SPRITE_AREA_H - 1)   // 55

#define UI_LIST_ROWS     4          // visible rows in every vertical list
#define UI_LIST_PITCH    11         // 4 * 11 = 44 <= UI_BODY_H
#define UI_STACK_DEPTH   5          // navigation back-stack
#define UI_ALERT_QUEUE   8

// Idle body animation. Two-frame art, so this is the half-period.
#define UI_ANIM_FRAME_MS   420UL
#define UI_MIMO_MS         1400UL   // heart particles after DBL_R
#define UI_WIGGLE_MS       350UL    // the "nope" wiggle on a dead-end gesture
// UI_EAT_POSE_MS is gone on purpose: the 2.5 s static POSE_EAT hold after a
// meal is now the eating WINDOW of the ACT_FEED_* choreography (actfx.cpp), so
// the pose and the bowl on the floor cannot disagree about when a meal ends.

// Interface motion. All PRESENTATION timings: no sim quantity reads them, and
// every one degrades to "instant" when a frame arrives late.
#define UI_RING_MS          140UL   // menu carousel settle after one step
#define UI_RING_STEP_PX     24      // MUST match the d*24 pitch in draw_menu()
#define UI_LIST_SLIDE_MS    120UL   // list highlight travel between two rows
#define UI_TRANS_MS         150UL   // 3-step dither dissolve on screen entry
#define UI_STAT_STEP_MS      50UL   // one frame at FPS_NORMAL
#define UI_STAT_RATE_PCT      4     // points per step -> 80 %/s, full bar in 1.25 s
#define UI_STAT_SNAP_GAP_MS 1000UL  // a gap this long means "we were away": snap
#define UI_SLEEP_DIM_MS    2000     // contrast ramp down when the pet falls asleep
#define UI_WAKE_RAMP_MS    1200     // and back up when it wakes

// =============================================================================
//  MANDATORY PUBLIC INTERFACE (BRIEF 4, row 11)
// =============================================================================

// Reset the state machine to S0 HOME (or straight into the death staging /
// egg screen when the loaded save is already dead / still an egg). Call once
// from setup(), AFTER store_begin(), gt_begin(), sim_init() and rd_begin().
void     ui_begin(void);

// Feed the recogniser output. Exactly one gesture per call; GST_NONE is a
// legal no-op. Never blocks, never draws.
void     ui_handle(Gesture g);

// Draw the current screen into the U8g2 buffer. Call ONLY between a true
// rd_begin_frame() and rd_end_frame().
void     ui_draw(void);

// Force a screen. Clears any modal, resets the auto-return timer and runs the
// target's enter hook (radio requests, list cursors, QR encode, ...).
void     ui_goto(ScreenId s);

// Raise an alert. Queued (UI_ALERT_QUEUE deep, duplicates collapse) and shown
// as the S11 overlay as soon as no higher-priority staging is running.
void     ui_alert(AlertId a);

// The screen the user is looking at. Reports SCR_ALERT / SCR_CONFIRM while
// one of those overlays is up, otherwise the base screen.
ScreenId ui_screen(void);

// =============================================================================
//  ADDITIVE EXTENSIONS
//  Nothing above changes shape. These exist because the six mandated entry
//  points cannot express a per-loop pump, and because a UI with no writable
//  Config cannot implement S9 SETTINGS.
// =============================================================================

// Per-loop pump. Runs auto-return, modal lifetimes, the death staging clock,
// the minigame loops, the hold-progress detectors (god entry, burial) and the
// BLE service while S8 SOCIAL is open. Call once per loop(), unconditionally -
// it is independent of the frame scheduler, so timing stays correct at 1 fps.
void     ui_service(void);

// Bind the live Config the settings screen edits and persists. REQUIRED: with
// no binding S9 SETTINGS renders read-only and every toggle answers
// STR_ERR_BUSY. The pointer must outlive the UI (the .ino's own Config).
void     ui_bind_config(Config* cfg);

// The entry point's apply_config() routes the user brightness through here
// instead of calling rd_set_contrast() directly. render.h:264 makes
// rd_set_contrast() cancel any ramp in flight, so an unrelated Config write
// (a mute toggle, a POST /api/cfg) would otherwise snap the sleeping panel
// back to full brightness and leave it there until the pet woke up.
void     ui_note_brightness(uint8_t contrast);

// Drain one sim_take_events() bitmask into the UI: death staging, evolution
// freeze, hatch, alerts, poop/sick toasts.
// Call every logic tick with the value sim_take_events() returned.
void     ui_note_events(uint32_t sim_events);

// An action the WEB applied. webui.cpp calls sim_apply_action() directly, so
// without this hook feeding the pet from the phone moved seven bars and
// animated nothing at all on the panel. The entry point drains
// web_take_action() once per loop() and hands the result here; `before` is the
// pet as it was BEFORE the action landed, which ACT_CLEAN needs because
// poop_count is already 0 by the time anyone can look.
//
// The UI navigates to HOME so the choreography can be seen, EXCEPT while a
// ceremony, the memorial, god mode or a running minigame owns the panel - a
// remote button press does not outrank those. The sim change stands either way;
// only the film is dropped.
void     ui_note_web_action(uint8_t action, const PetSave& before);

// Hand over the AbsenceReport that sim_catch_up_ex() produced at boot. The UI
// turns it into the tier line with the EXACT elapsed time substituted for {t}
// (GAME_DESIGN 5.3: "never round - the precision is the joke") and shows it
// over HOME for five seconds. rep.died == 1 starts the death staging instead.
void     ui_note_absence(const AbsenceReport& rep);

// Transient one-line notice above the affordance strip, UI_TOAST_MS long.
void     ui_toast(uint16_t str_id);

// True while the death staging owns the buttons (GAME_DESIGN 9.2 steps 2-6).
// The entry point must keep pumping input_poll() anyway - ui_handle() drops
// the gestures itself - but this lets the LED / buzzer stay quiet too.
bool     ui_input_locked(void);

// Frame rate this screen wants right now. ui_goto() already applies it via
// rd_set_fps(); this is exposed so the entry point can arbitrate with the
// web-busy cap if it wants to.
uint8_t  ui_fps(void);

// The living pet's display name: Config.pet_name when the user set one,
// otherwise the deterministic dynasty name. Always NUL-terminates.
void     ui_pet_name(char* out, size_t cap);

// GAME_DESIGN 9.3: hash(lineage_id, generation) -> two Spanish syllables.
// Deterministic across devices; never empty. cap >= 16 recommended.
void     ui_name_for(uint32_t lineage_id, uint8_t generation, char* out, size_t cap);

// =============================================================================
//  MODULE SEAMS ui.cpp CONSUMES (declared by their owners, not here)
//
//  webui.h   uint16_t web_pin(void)          - the S15 QR payload and the
//                                              4-digit PIN printed beside it.
//  godmode.h bool     god_active(void)
//            GodEvt   god_handle(Gesture)    - returns what ui must do next
//            void     god_draw(void)         - owns the whole S14 frame
//            uint8_t  god_entry_progress(id) - the undocumented S6 hold
//            void     god_draw_marker(void)  - the "GOD xN" bar, every screen
//
//  ui.cpp includes webui.h and godmode.h directly; neither pulls in a network
//  header, so the BRIEF 4 layering rule holds. There are deliberately NO weak
//  stubs here: both modules ship in the same sketch folder, and a weak
//  fallback would only hide a signature drift that must be a hard error.
//
//  The ENTRY POINT still owns god_begin() / god_service() / god_freeze_clock()
//  and web_begin() / web_service() / web_bind_config(); ui calls none of them.
// =============================================================================

#endif  // NT_UI_H
