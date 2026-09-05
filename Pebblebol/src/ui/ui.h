// =============================================================================
//  PEBBLEBOL - ui/ui.h
//  The screen state machine S0..S14, the navigation
//  grammar, the 20 s auto-return, the modal + alert layers, the on-device
//  minigames and the hatch ceremony.
//
//  LAYERING
//  --------
//  ui owns NO game state. Everything it shows comes from sim_view() /
//  sim_*() / ble_peer(); everything it changes goes through
//  sim_apply_action(), gs_save_cfg() or net_request().
//  It never includes WiFi.h / BLEDevice.h / WebServer.h (net.h and
//  ble_social.h are deliberately network-header-free) and it never constructs
//  a U8G2: the single instance comes from rd_u8g2().
//
//  TIME
//  ----
//  ui reads millis() for PRESENTATION only - animation phase, modal lifetimes,
//  the hatch ceremony clock, hold progress bars. No game quantity is ever
//  derived from it; game time is sim_step_seconds() and gt_now(), as the
//  layering rule requires.
//
//  Identifiers and comments: English. Every user-facing byte: strings_es.h.
// =============================================================================
#ifndef NT_UI_H
#define NT_UI_H

#include <stdint.h>
#include <stddef.h>

#include "../core/config.h"
#include "../core/nt_types.h"
// P5-C3: the exploration seams below hand out references to two persisted
// blobs and a Genome by value. Types only - ui.h still names no hardware.
#include "../persistence/save_schema.h"

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
//  MANDATORY PUBLIC INTERFACE
// =============================================================================

// Reset the state machine to S0 HOME (or straight into the EGG screen when the
// loaded save is still an egg). Call once from setup(), AFTER kv_begin(),
// gt_begin(), sim_init() and rd_begin().
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
// as the ALERT overlay as soon as no higher-priority staging is running.
void     ui_alert(AlertId a);

// The screen the user is looking at. Reports SCR_ALERT / SCR_CONFIRM while
// one of those overlays is up, otherwise the base screen.
ScreenId ui_screen(void);

// =============================================================================
//  ADDITIVE EXTENSIONS
//  Nothing above changes shape. These exist because the six mandated entry
//  points cannot express a per-loop pump, and because a UI with no writable
//  Config cannot implement the SETTINGS screen.
// =============================================================================

// Per-loop pump. Runs auto-return, modal lifetimes, the hatch ceremony clock,
// the minigame loops, the god-entry hold-progress detector and the BLE service
// while SOCIAL is open. Call once per loop(), unconditionally - it is
// independent of the frame scheduler, so timing stays correct at 1 fps.
void     ui_service(void);

// Bind the live Config the settings screen edits and persists. REQUIRED: with
// no binding the SETTINGS screen renders read-only and every toggle answers
// STR_ERR_BUSY. The pointer must outlive the UI (app.cpp's own Config).
void     ui_bind_config(Config* cfg);

// The entry point's apply_config() routes the user brightness through here
// instead of calling rd_set_contrast() directly. render.h:264 makes
// rd_set_contrast() cancel any ramp in flight, so an unrelated Config write
// (a mute toggle, a captive-portal write) would otherwise snap the sleeping panel
// back to full brightness and leave it there until the pet woke up.
void     ui_note_brightness(uint8_t contrast);

// Drain one sim_take_events() bitmask into the UI: evolution freeze, hatch,
// alerts, poop/sick toasts.
// Call every logic tick with the value sim_take_events() returned.
void     ui_note_events(uint32_t sim_events);

// Hand over the AbsenceReport that sim_catch_up_ex() produced at boot. The UI
// turns it into the absence line with the EXACT elapsed time substituted for
// {t} and shows it
// over HOME for five seconds. rep.clock_known == 0 picks the unknown-clock
// wording instead, because nothing was charged for that gap.
void     ui_note_absence(const AbsenceReport& rep);

// Transient one-line notice above the affordance strip, UI_TOAST_MS long.
void     ui_toast(uint16_t str_id);

// -----------------------------------------------------------------------------
// The save pipeline's face (P2-C9c).
//
// ui_boot_screen() draws ONE frame of SCR_BOOT or SCR_LOAD_SAVE and may be
// called before ui_begin(): neither reads the pet or the config, which is the
// point - they are what is on screen while the load decides.
//
// ui_note_load() takes the LoadResult (persistence/save_manager.h) and turns it
// into either a toast or the SAVE ERROR screen. It NEVER wipes anything: the
// two refused outcomes leave the session read-only until the user chooses.
//
// ui_bind_recover() binds the "Recuperar" action. It lives in the entry point
// because recovery has to rebind the simulation to the pet that comes back,
// which ui.cpp may not do. Returning false means "there was no copy" and
// nothing was written.
// -----------------------------------------------------------------------------
// ui_bind_recover(), ui_note_load() and the ERROR screen itself live in
// ui/screen_error.cpp since P2-C11a: the screen is in the table, and the table
// rows are pure translation units. The declarations stay here because the
// entry point calls them and knows only ui.h.
typedef bool (*UiRecoverFn)(void);
void     ui_bind_recover(UiRecoverFn fn);
void     ui_note_load(uint8_t load_result);
void     ui_boot_screen(ScreenId s);

// Open the two-step "factory reset" confirmation. The ERROR screen's B button
// asks for it; the dialogs, and the reset itself, are ui.cpp's modal layer.
void     ui_confirm_wipe(void);

// The nvs2 checkpoint came back and the simulation is bound to the pet it
// carried: re-prime the body, stop animating stats towards stale values and
// land on HOME. Called by the ERROR screen after a successful recovery.
void     ui_note_recovered(void);

// -----------------------------------------------------------------------------
//  NAVIGATION SEAMS (P2-C11a)
//
//  app/state_machine.cpp owns the current screen, the back stack and the two
//  navigation clocks; the four calls below are the parts of a screen change
//  that are still ui.cpp's business. They are called in this order and from
//  nowhere else:
//
//      ui_nav_reset()       close the modal, cut the shared interpolators
//      ui_nav_arrived(to)   entry dissolve, frame rate, frame request
//
//  The two ui_nav_leave/ui_nav_enter forwarders that used to sit either side of
//  them went with P2-C11d: the table has a row for every state now, so there is
//  no screen left whose enter and leave live in ui.cpp.
// -----------------------------------------------------------------------------
void     ui_nav_reset(void);
void     ui_nav_arrived(uint8_t to);

// -----------------------------------------------------------------------------
//  THE GAME SEAMS (P2-C11d, retired by P3-C4)
//
//  SCR_GAME's five hooks. The three minigames, their scoring and their pause
//  dialog are still inside ui.cpp - lifting them out is P3-C4 - but the screen
//  table may not have a hole for them, so the row points at these forwarders
//  instead of at a special case in the dispatcher. When P3-C4 lands
//  ui/screen_game.cpp these five disappear and the row points straight at it.
// -----------------------------------------------------------------------------
void     ui_game_enter(void);
void     ui_game_update(uint32_t now_ms);
void     ui_game_render(void);
void     ui_game_input(Gesture g);
void     ui_game_leave(void);

// -----------------------------------------------------------------------------
//  THE BOX SEAMS (P2-C11d)
//
//  ui/screen_box.cpp READS game/box.h directly (pure queries over the live
//  GameState) but may not write: activating a slot changes what the simulation
//  is bound to, and every mutation has to reach flash. Both are ui.cpp's.
// -----------------------------------------------------------------------------

// box_set_active(slot) + sim_switch() + save. A no-op for an empty slot or for
// the slot that is already active.
void     ui_box_activate(uint8_t slot);

// The "nope" wiggle: a gesture that means nothing on this screen shakes the
// body for UI_WIGGLE_MS instead of doing nothing at all. HOME's B secondary is
// the only dead end left in the grammar.
void     ui_wiggle(void);

// box_swap(a, b) + save. Display order is the player's to arrange (spec 9).
void     ui_box_swap(uint8_t a, uint8_t b);

// Open the FIRST of the two confirmations that stand in front of a release.
// Nothing is destroyed until both have been answered YES (invariant B4, spec
// section 9 "release/discard if explicitly supported").
void     ui_box_release(uint8_t slot);


// -----------------------------------------------------------------------------
//  SCREEN SEAMS (P2-C11b)
//
//  A migrated screen is a PURE translation unit: gfx.h, strings, and pure data
//  headers. Everything it needs that is NOT pure - the wall clock, the
//  navigation machine, the modal layer, the simulation, the panel contrast -
//  reaches it through the calls below, which ui.cpp implements in one line
//  each and tests/test_screens.cpp records. Same pattern the ERROR screen
//  already uses for ui_toast() / ui_goto() / ui_confirm_wipe().
// -----------------------------------------------------------------------------

// The PRESENTATION clock (millis) and the time since the last gesture. The
// second one is what gfx_countdown() draws invariant 3's drain bar from.
uint32_t ui_now_ms(void);
uint32_t ui_idle_ms(void);

// Navigation, forwarded to app/state_machine.cpp. ui_note_input() restarts the
// auto-return without moving.
void     ui_push(ScreenId s);
void     ui_back(void);
void     ui_home(void);
void     ui_note_input(void);

// Ask the frame scheduler for one more frame. A modal that appears between two
// FPS_LOW frames would otherwise wait up to 250 ms to be seen.
void     ui_request_frame(void);

// The live Config the SETTINGS screen edits, or NULL when nothing is bound
// (ui_bind_config()). ui_cfg_changed() stamps and persists it with a toast.
Config*  ui_cfg(void);
void     ui_cfg_changed(void);

// The user's brightness choice, through ui.cpp's arbiter rather than straight
// to the panel: while the pet is asleep the dim override still wins and the new
// setting takes effect on waking.
void     ui_apply_brightness(uint8_t contrast);

// sim_apply_action() plus the toast, the choreography and the save.
// ui_act_and_show() additionally sends the player HOME to watch a film that
// actually started (BRIEF D); both return whether the action was accepted.
// No screen calls ui_do_action() since P3-C2b took the light switch away - it
// was the one action with no film - but it stays as the seam for an action
// that must NOT move the player, and the screens' tests still drive it.
bool     ui_do_action(uint8_t action);
bool     ui_act_and_show(uint8_t action);

// The MENU's "do that again": replays the last accepted action, or explains
// that there is not one yet.

// The modal layer. ui_help() is the one-line hint BOTH opens on a list row;
// ui_confirm_medicine() opens the confirmation the medicine costs.
void     ui_help(uint16_t str_id);
void     ui_confirm_medicine(void);

// -----------------------------------------------------------------------------
//  THE CREATOR SCREEN'S RADIO SEAM (P2-C11c)
//
//  CREATOR is the one screen that owns the Wi-Fi station. It is a pure
//  translation unit, so it cannot call net_request() itself: it asks through
//  these two instead. ui_creator_radio(true) is a REQUEST, not a promise - the
//  station may still be settling when it returns, which is why the screen
//  re-reads ui_creator_info() every second rather than caching one answer.
// -----------------------------------------------------------------------------
struct CreatorInfo;
void     ui_creator_info(CreatorInfo& out);
void     ui_creator_radio(bool on);

// The EVOLUTION screen's ten alternating taps landed: hatch the egg now. The
// simulation, the save and the ceremony are all ui.cpp's, so the screen only
// says that it happened.
void     ui_request_hatch(void);

// Start minigame `idx` (the PLAY list order), refusing with a toast when the
// cooldown or the energy floor says no. The GAME screen itself is still
// ui.cpp's until P3-C4.
void     ui_start_minigame(uint8_t idx);

// -----------------------------------------------------------------------------
//  THE BATTLE SCREEN'S SEAMS (P4-C4)
//
//  ui/screen_battle.cpp is a pure translation unit and owns the whole fight -
//  the 212 B BattleState, the AI, the log ring and every decision in it. What
//  it cannot do for itself is exactly three things, and each gets one line
//  here rather than a hole in the layering.
// -----------------------------------------------------------------------------

// Arm and open a battle. `entry` is BT_ENTRY_PRACTICE (the PLAY row) or
// BT_ENTRY_DIAG (the console's test_battle). ui.cpp draws the seed - practice
// from rng_u32(RNG_BATTLE), the diagnostic from the fixed BT_DIAG_SEED spec
// section 49's "deterministic RNG seed" asks for - and then pushes SCR_BATTLE,
// because a named RNG stream and the navigation machine are both outside a pure
// screen's reach.
void     ui_start_battle(uint8_t entry);

// THE ONE PATH A BATTLE RESULT CAN TAKE, and it is called exactly once per
// VISIT to SCR_BATTLE however the player leaves the screen (ui/
// screen_battle.cpp's report_once(), which is minigames/manager.cpp's
// mgr_abort() in miniature - and per visit rather than per battle, which is
// only a difference for a visit in which no battle started: that one reports
// won = 0 and is dropped on the line below).
//
// `won` IS THE ENGINE'S OUTCOME, not "did the player sit through the result
// screen": a battle is decided while its victory transcript is still playing,
// and a leave inside that window has to pay the same as a leave after it.
//
// A practice win awards XP_BATTLE_WIN through app_award_xp() - metered by
// game/xp.h's ledger like every other source - and persists. A DIAG battle
// awards nothing: entering god mode already taints the genome, and a
// diagnostic that pays XP is a cheat.
//
// WHO IS PAID: THE ACTIVE PEBBLE, which is not necessarily one that fought.
// app_award_xp() pays box_slot(box_active()), and the pick list only requires
// box_occupied(), so a player who fields slots 2-4 while slot 1 is active earns
// the XP and the level-up hp rescale on the Pebble that stayed home. That is
// consistent with every other source in the game (all of them pay the active
// pet) and it is stated here rather than left to be discovered, because "a
// practice win awards XP_BATTLE_WIN" does not say to whom. Per-combatant XP
// needs a per-Pebble ledger and belongs with P4-C5's real battles.
void     ui_battle_result(uint8_t entry, uint8_t won);

// -----------------------------------------------------------------------------
//  THE EXPLORATION SEAMS (P5-C3/C4)
//
//  ui/screen_network.cpp and ui/screen_encounter.cpp are pure translation
//  units - they run whole on the host, which is the only way the scan timeout,
//  the cancel and the capture roll get a test at all. What they cannot do for
//  themselves is exactly six things, and each gets one line here rather than a
//  hole in the layering.
// -----------------------------------------------------------------------------

// THE RADIO. networking/net.cpp fills in the four-function WifiScanDriver seam
// and is the ONLY route from the game to a scan; a host test hands the same
// screen a fake driver through this call.
struct WifiScanDriver;
const WifiScanDriver& ui_scan_driver(void);

// THE CLOCK, all three facts at once and in the shape game/cooldowns.h takes
// them: the wall clock, a monotonic millisecond count and HOW the clock came to
// hold its value. The third is what decides whether a cooldown is persisted or
// per-boot, and whether a SPECIAL XP burst is paid at all.
void     ui_explore_clock(uint32_t* now_epoch, uint32_t* now_ms, uint8_t* cal);

// gs_device_id(): stable per device, persisted, never 0. It salts the encounter
// seed so two units standing side by side do not see the same creature.
uint32_t ui_device_seed(void);

// One draw from RNG_ENCOUNTER. The capture roll is REAL randomness and must be
// - re-entering an encounter and getting the same failure again is not a game -
// which is why it is drawn here and not folded into the deterministic
// encounter seed.
uint32_t ui_explore_roll(void);

// The two persisted blobs the exploration path reads and writes. References
// into the one live GameState; ui_explore_commit() is what puts a changed
// cooldown table, a changed bag or a changed Pebble on flash, and it polls
// cd_take_dirty() rather than saving after every arm (game/cooldowns.h says
// why: cd_ready() can dirty the table too).
CooldownTable& ui_cooldowns(void);
Inventory&     ui_inventory(void);
void           ui_explore_commit(void);

// A fresh sealed genome for a captured Pebble. It draws through RNG_BREEDING
// (game/genome.cpp), which is a named global stream and therefore out of a pure
// screen's reach - and the trap worth naming: a caller that wanted a
// deterministic capture must NOT reseed that stream, because every later
// breeding roll in the boot would move with it.
Genome   ui_fresh_genome(void);

// XP from an exploration source, through app_award_xp() so the ledger and the
// level-up choreography are the same ones every other source uses.
void     ui_award_xp(uint16_t amount, uint8_t src);

// The Pebble the player is carrying, or NULL. A screen that only wants to point
// an item at it should not have to bind the Box to do so.
PebbleInstance* ui_active_pebble(void);

// -----------------------------------------------------------------------------
//  THE THREE render.h EFFECTS A PURE SCREEN CANNOT REACH (P4-C4)
//
//  ui_hold_fps() is the one that is load-bearing rather than decorative:
//  rd_set_fps() only moves the REQUEST and rd_fps() caps that request at
//  FPS_LOW for RENDER_WEB_BUSY_MS after every web hit, so a screen animating a
//  transcript has to hold the rate the way actfx does. The hold is
//  SELF-EXTINGUISHING (capped at RD_FPS_HOLD_MAX_MS) and must be renewed from
//  the screen's update hook, which is what makes it impossible to leave the
//  panel pinned at 20 fps by forgetting to release it.
// -----------------------------------------------------------------------------
void     ui_hold_fps(uint8_t fps, uint16_t ms);
void     ui_flash(uint16_t ms);
void     ui_shake(uint8_t amp_px, uint16_t ms);

// The undocumented god-mode entry hold, 0..100, painted by the PEBBLE screen's
// genome page. 0 whenever no hold is in progress, which is almost always.
uint8_t  ui_god_progress(void);

// The "Acerca de" page's five diagnostic lines (IP, heap, PIN, NVS error,
// age). Every one of them is a device fact, so ui.cpp fills them in and the
// SETTINGS screen only lays them out.
#define UI_INFO_LINES  5
#define UI_INFO_CAP   40
void     ui_info_lines(char lines[UI_INFO_LINES][UI_INFO_CAP]);

// The TIME screen commits through here: gt_set_epoch(CAL_USER) plus the
// persisted baseline rewrite. Returns false when the stamp is not a real
// instant, in which case nothing moved.
bool     ui_set_clock(uint16_t year, uint8_t month, uint8_t day,
                      uint8_t hour, uint8_t minute);
// What the clock currently believes, for the screen to start editing from.
// Returns false when there is nothing trustworthy to start from.
bool     ui_get_clock(uint16_t* year, uint8_t* month, uint8_t* day,
                      uint8_t* hour, uint8_t* minute);
// Drop whatever the recogniser is holding, so the release of a confirming hold
// cannot fire again on the screen underneath.
void     ui_input_flush(void);
// Is that button physically down, and for how long? The TIME screen drives its
// own auto-repeat from this (the recogniser deliberately never repeats HOLD_R).
bool     ui_btn_down(uint8_t btn);
uint32_t ui_btn_hold_ms(uint8_t btn);

// True while the hatch ceremony owns the buttons. The ceremony is
// unskippable, so every gesture is dropped for its duration.
// The entry point must keep pumping input_poll() anyway - ui_handle() drops
// the gestures itself - but this lets the LED / buzzer stay quiet too.
bool     ui_input_locked(void);

// Frame rate this screen wants right now. ui_goto() already applies it via
// rd_set_fps(); this is exposed so the entry point can arbitrate with the
// web-busy cap if it wants to.
uint8_t  ui_fps(void);

// The living pet's display name, in order: Config.pet_name when the user set
// one, then the SPECIES the active Pebble is (the roster's Spanish name), then
// the deterministic dynasty name for a Pebble with no species row. Always
// NUL-terminates. P4-C4a put the species in the middle: the dynasty name says
// the same word before and after an evolution, so on its own it could never
// tell the player their creature had become something else.
void     ui_pet_name(char* out, size_t cap);

// hash(lineage_id, generation) -> two Spanish syllables.
// Deterministic across devices; never empty. cap >= 16 recommended.
void     ui_name_for(uint32_t lineage_id, uint8_t generation, char* out, size_t cap);

// =============================================================================
//  MODULE SEAMS ui.cpp CONSUMES (declared by their owners, not here)
//
//  webui.h   uint16_t web_pin(void)          - the QR payload and the
//                                              4-digit PIN printed beside it.
//  godmode.h bool     god_active(void)
//            GodEvt   god_handle(Gesture)    - returns what ui must do next
//            void     god_draw(void)         - owns the whole GOD frame
//            uint8_t  god_entry_progress(id) - the undocumented STATUS_B hold
//            void     god_draw_marker(void)  - the "GOD xN" bar, every screen
//
//  ui.cpp includes webui.h and godmode.h directly; neither pulls in a network
//  header, so the layering rule holds. There are deliberately NO weak
//  stubs here: both modules ship in the same sketch folder, and a weak
//  fallback would only hide a signature drift that must be a hard error.
//
//  The ENTRY POINT still owns god_begin() / god_service() / god_freeze_clock()
//  and web_begin() / web_service() / web_bind_config(); ui calls none of them.
// =============================================================================

#endif  // NT_UI_H
