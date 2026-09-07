// =============================================================================
//  PEBBLEBOL - ui/ui.h
//  The screen state machine S0..S14, the navigation
//  grammar, the 20 s auto-return, the modal + alert layers, the on-device
//  minigames and the hatch ceremony.
//
//  LAYERING
//  --------
//  ui owns NO game state. Everything it shows comes from sim_view() /
//  sim_*() / the discovery job's peer list; everything it changes goes through
//  sim_apply_action(), gs_save_cfg() or net_request().
//  It never includes WiFi.h / esp_now.h / WebServer.h (net.h and discovery.h
//  are deliberately network-header-free) and it never constructs a U8G2: the
//  single instance comes from rd_u8g2().
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

// THE POWER LADDER'S DIM (P6-C3, hardware/power.h). An override on top of the
// user's brightness and the asleep-pet dim, applied by the same
// bright_service() the other two go through, so the contrast base keeps having
// exactly one owner and no two writers race each other through
// rd_contrast_ramp(). ui.cpp takes the LOWER of the two dims when both are on.
void     ui_note_power_dim(bool on);

// Is a screen holding a radio job right now? The power ladder's `held` input:
// while this is true the ladder is clamped at DIM and will not drop the radio.
// It is a query, not a handle - the release itself is a navigation, so it runs
// the owning screen's leave() hook and cancels through wifi_scan_cancel() or
// link_cancel(). BOTH radio screens are folded in here since P7-C2: the NETWORK
// screen's scan and the LINK screen's discovery job and session.
bool     ui_radio_job_busy(void);

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

// RE-ROOT the navigation: empty the back stack and land on `s`. The first-boot
// flow (app/onboarding.h) is what needs it - each setup step REPLACES the one
// before it rather than stacking on it, so B can never walk backwards into a
// question already answered and HOME at the end of the flow is a root and not a
// return. sm_replace_root()'s own doc-comment names exactly this case.
void     ui_replace_root(ScreenId s);

// Ask the frame scheduler for one more frame. A modal that appears between two
// FPS_LOW frames would otherwise wait up to 250 ms to be seen.
void     ui_request_frame(void);

// The live Config the SETTINGS screen edits, or NULL when nothing is bound
// (ui_bind_config()). ui_cfg_changed() stamps and persists it with a toast.
Config*  ui_cfg(void);
void     ui_cfg_changed(void);

// EVERYTHING THE FIRST-BOOT FLOW HAS ANSWERED SO FAR, ONTO FLASH, WITH NO
// TOAST. Two things and not one, and the second is what makes the answers
// survive:
//
//   * the Config, silently - ui_cfg_changed() raises STR_SET_SAVED, and a
//     "guardado" banner landing on top of the NEXT question is the same defect
//     as the boot greeting that used to cover the TIME screen's only
//     instruction (ui/screen_setup.h);
//   * the BOX and the active Pebble - because persistence/save_manager.cpp's
//     load_all_inner() returns LOAD_FRESH the moment the Box pair is missing,
//     BEFORE it ever looks at the config. A first boot that wrote only a config
//     and then lost power would come back with the config DISCARDED and the
//     flow restarted, and the step field would have been a decoration.
void     ui_setup_persist(void);

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
//  CREATOR is the one screen that owns the Wi-Fi access point. It is a pure
//  translation unit, so it cannot call net_request_portal() itself: it asks
//  through these two instead. ui_creator_radio(true) is a REQUEST, not a
//  promise - the access point may still be coming up when it returns, which is
//  why the screen re-reads ui_creator_info() every second rather than caching
//  one answer, and why CREATOR_AP_WAIT_MS exists at all.
//
//  ui_creator_radio(false) IS THE ONE TEARDOWN (P8-C2): the socket, then the
//  radio. Leaving the screen, the D7 idle timeout and the "the access point
//  never came up" exit all reach it, so no path can leave half the portal up.
//  CreatorInfo.idle_expired is how the timeout reaches a pure screen; the
//  decision itself is networking/creator_gate.cpp's, where a host binary drives
//  it.
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
//  THE PEER LINK'S SEAMS (P7-C2/C3)
//
//  ui/screen_link.cpp is a PURE translation unit that owns the discovery job
//  and the Session; what it cannot do for itself is reach the radio and draw a
//  nonce. Four calls for the first and one for the second, and it makes no
//  others. networking/net.cpp is the ONE radio owner on the other side of all
//  of them, exactly as it is for ui_scan_driver().
// -----------------------------------------------------------------------------
struct LinkRadioDriver;
struct Transport;

// The four-function seam networking/discovery.h drives: bring the link up,
// beacon, drain, put the radio back. A host test hands the same screen a fake.
const LinkRadioDriver& ui_link_driver(void);

// The Transport networking/session.h runs over, unicast to the peer bound
// below. It is a REFERENCE to one object because a device has one radio; a host
// test binds a loopback endpoint to it instead.
const Transport& ui_link_transport(void);

// Open / close the unicast peer the transport talks to. The slot is a
// DiscPeer::slot - the transport's own opaque handle - and NOT anything that
// identifies a piece of hardware (networking/discovery.h, spec 43/44).
// THE BIND IS HALF OF THE CONSENT GATE: until it happens the radio delivers
// nothing from that device to this firmware at all.
bool ui_link_bind(uint8_t slot);
void ui_link_unbind(void);

// One draw from RNG_MISC ("tokens, nonces, PINs, canaries"), which is what
// SessionCfg.nonce is and which a pure screen may not reach for itself.
uint32_t ui_link_nonce(void);

// -----------------------------------------------------------------------------
//  THE TRADE'S TWO SEAMS (P7-C4)
//
//  ui/screen_link.cpp owns the trade the same way it owns the battle: it holds
//  the Session and the caller-owned TradeLink, and it drives them. What a PURE
//  screen cannot do is write flash or read the load path's quarantine mask, so
//  those two arrive through here and networking/trade_link.cpp's TradeHooks is
//  the shape they arrive in.
//
//  FORWARD-DECLARED AND RETURNED BY POINTER on purpose: `struct TradeHooks`
//  lives in networking/trade_link.h, which drags networking/session.h and
//  game/battle.h behind it, and ui.h is included by every screen in the tree.
//  A pointer to an incomplete type costs those screens nothing.
//
//  A BUILD MAY ANSWER nullptr, and ui/screen_link.cpp treats that as "this
//  device cannot trade" rather than as a fault.
// -----------------------------------------------------------------------------
struct TradeHooks;
const TradeHooks* ui_trade_hooks(void);

// persistence/save_manager.h's save_quarantine_mask(). game/trade.h takes it as
// a PARAMETER and not an include, because the RULE ("a quarantined Pebble may
// not enter a trade") is a game rule and the DATUM belongs to the load path.
uint16_t ui_trade_quarantine(void);

// -----------------------------------------------------------------------------
//  THE LINKED BATTLE'S SEAMS (P7-C3)
//
//  ui/screen_battle.cpp runs the ENGINE; the SESSION that keeps two engines
//  identical lives on ui/screen_link.cpp - the screen the two players agreed on
//  it from, and the screen that owns the radio. The battle screen reaches it
//  through the calls below rather than by including screen_link.h, so
//  tests/test_battle_screen.cpp still links the battle screen and the things it
//  drives and nothing else.
// -----------------------------------------------------------------------------
// What the SESSION says happened. UI_LKB_WON is answered where and only where
// session_rewards_authorised() is true - both endpoints agreed the outcome AND
// the final hash - so a local engine that won a fight the peer never confirmed
// is UI_LKB_BROKEN and pays nothing. UI_LKB_BROKEN is deliberately NOT
// UI_LKB_LOST: a desync is not a defeat, and printing one as the other is the
// same mistake as calling BO_ABORT a draw.
#define UI_LKB_NONE     0u
#define UI_LKB_RUNNING  1u
#define UI_LKB_WON      2u
#define UI_LKB_LOST     3u
#define UI_LKB_DRAW     4u
#define UI_LKB_BROKEN   5u

uint8_t  ui_link_battle_status(void);
// Which side of the shared BattleSetup is ours. It is the SESSION's answer
// (networking/session.cpp fixes it from the two device ids) and not a constant:
// the linked battle is the first fight in this firmware the player is not
// always side 0 of.
uint8_t  ui_link_battle_side(void);
void     ui_link_battle_pump(uint32_t now_ms);
bool     ui_link_battle_wants_action(void);
void     ui_link_battle_submit(uint8_t kind, uint8_t index);
// Milliseconds before the session's retransmission ladder gives up on a round
// nobody has advanced - the player's real move clock. See the note at
// link_battle_move_ms_left() in ui/screen_link.cpp for why it is nine seconds
// and why neither screen papers over it.
uint32_t ui_link_battle_move_ms_left(uint32_t now_ms);
// SCR_BATTLE IS GONE, WHEREVER IT WENT. ui/screen_battle.cpp's leave() hook is
// the only thing that runs on EVERY route off that screen - B, the result page,
// and LONG_BOTH, which goes straight HOME and never runs the LINK screen's own
// leave() at all - so this is where the session is closed and the radio given
// back. Without it a linked battle abandoned with the HOME gesture would leave
// the radio up, the session unpumped and the power ladder clamped at DIM for
// ever, because ui/screen_link.cpp deliberately does not release when it is
// pushed aside. Idempotent.
void     ui_link_battle_done(void);

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

// THE SAME LADDER, IN THE STORED (LATIN-1) ENCODING. For the one caller that
// does not draw: screen_link.cpp's fill_self(), whose DiscBeacon.name field is
// Latin-1 by wire contract. Handing it the UTF-8 form made disc_encode() answer
// DE_NAME for every accented name the first-boot ring can type, so the device
// emitted no beacon at all - see the note above the implementation.
void     ui_pet_name_latin1(char* out, size_t cap);

// FIRST BOOT: replace the Pebble app/app.cpp minted with one of the chosen
// species, keeping the slot, the active flag, the genome and the creation seed.
//
// IT REFUSES OUTSIDE THE FLOW, and the refusal is not a comment: ui.cpp checks
// the persisted step (app/onboarding.h) and game/box.cpp's box_reroll_starter()
// independently refuses any Pebble that has earned or been named anything. Two
// locks, because this is the one call in the tree that destroys the ACTIVE
// Pebble - box_release() will not, by rule B4.
bool     ui_set_starter(uint8_t species_id);

// hash(lineage_id, generation) -> two Spanish syllables.
// Deterministic across devices; never empty. cap >= 16 recommended.
void     ui_name_for(uint32_t lineage_id, uint8_t generation, char* out, size_t cap);

// =============================================================================
//  MODULE SEAMS ui.cpp CONSUMES (declared by their owners, not here)
//
//  webui.h   uint16_t web_pin(void)          - the 4-digit PIN printed beside
//                                              the symbol. IT IS NOT IN THE QR
//                                              PAYLOAD ANY MORE (spec 39).
//            void     web_portal_open/close  - the CREATOR session
//            bool     web_portal_idle_expired
//                                            - the D7 grace period
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
