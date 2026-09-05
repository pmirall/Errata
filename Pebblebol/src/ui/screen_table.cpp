// =============================================================================
//  PEBBLEBOL - ui/screen_table.cpp
//  THE table. One row per ScreenId, in enum order, and the enum is now the
//  spec section 6 state set exactly (P2-C11d renumbered it).
//
//  IT IS COMPLETE. Every row names all five hooks; there is no such thing any
//  more as an id the dispatcher has to fall through on. The strangler that
//  P2-C11a started - a render hook as the "migrated" marker, ui.cpp's three
//  switches for the rest - is finished: the switches are gone, GAME reaches
//  its still-in-ui.cpp implementation through five one-line seams (P3-C4 turns
//  those into a pure screen module), and the eight section 6 states that have
//  no implementation at all carry the ui/screen_soon.cpp placeholder, which is
//  a real screen with a real BACK rather than a hole.
//
//  P2-C11a migrated BOOT, LOAD_SAVE and ERROR; P2-C11b added HOME, MENU, the
//  two CARE lists, both PEBBLE pages, SETTINGS and TIME; P2-C11c added LINK,
//  EVOLUTION, DIAG and CREATOR and moved the CONFIRM / ALERT overlays into
//  ui/dialog.cpp; P2-C11d added BOX and the placeholders.
//
//  PURE translation unit: it names hooks, it does not draw.
// =============================================================================
#include "screen.h"

#include "screen_battle.h"
#include "screen_boot.h"
#include "screen_box.h"
#include "screen_care.h"
#include "screen_creator.h"
#include "screen_diag.h"
#include "screen_encounter.h"
#include "screen_error.h"
#include "screen_evolution.h"
#include "screen_home.h"
#include "screen_link.h"
#include "screen_menu.h"
#include "screen_network.h"
#include "screen_settings.h"
#include "screen_soon.h"
#include "screen_status.h"
#include "screen_time.h"
#include "ui.h"                 // the five GAME seams, until P3-C4

// Positional initialisers, in this order:
//   enter, update, render, input, leave, fps, flags
const ScreenDef SCREENS[SCR_COUNT] = {
  // --- BOOT / LOAD_SAVE -----------------------------------------------------
  // Two frames the save pipeline shows while it works. SF_LOCK_INPUT keeps the
  // navigation grammar off them and SF_STICKY keeps the 20 s auto-return off a
  // screen that is only ever up for as long as the read takes.
  { nop_enter, nop_update, boot_render, nop_input, nop_leave, 0,
    SF_STICKY | SF_LOCK_INPUT },                                  // SCR_BOOT
  { nop_enter, nop_update, load_save_render, nop_input, nop_leave, 0,
    SF_STICKY | SF_LOCK_INPUT },                                  // SCR_LOAD_SAVE

  // --- HOME -----------------------------------------------------------------
  // Where the auto-return goes, so it never times out. Its leave hook ends the
  // action choreography and releases the petfx hold.
  { nop_enter, nop_update, home_render, home_input, home_leave, 0,
    SF_STICKY },                                                  // SCR_HOME

  { menu_enter, nop_update, menu_render, menu_input, nop_leave, 0, 0 },
                                                                  // SCR_MENU
  // CARE owns B since P5-C4: its bag is a second mode one level below the
  // navigation stack, exactly as the BOX walks its own modes, so B closes the
  // bag first and only then the screen.
  { care_enter, nop_update, care_render, care_input, care_leave, 0,
    SF_OWNS_BACK },                                               // SCR_CARE
  { play_enter, nop_update, play_render, play_input, nop_leave, 0, 0 },
                                                                  // SCR_PLAY

  // --- GAME -----------------------------------------------------------------
  // Still implemented inside ui.cpp (the three minigames, their scoring and
  // their pause dialog); P3-C4 lifts it into ui/screen_game.cpp. Until then
  // the row is five forwarders, which is what lets the dispatcher stop having
  // a special case for it. SF_OWNS_BACK because a tap must not abandon a game
  // in progress - handle_game() reads B itself - and SF_STICKY because a
  // reflex game scored in milliseconds may not be interrupted by invariant 3.
  { ui_game_enter, ui_game_update, ui_game_render, ui_game_input, ui_game_leave,
    0, SF_STICKY | SF_OWNS_BACK },                                // SCR_GAME

  // --- BOX ------------------------------------------------------------------
  // SF_OWNS_BACK: B walks back through the screen's four modes one at a time
  // and only leaves the Box from the slot list.
  { box_enter, box_update, box_render, box_input, box_leave, 0,
    SF_OWNS_BACK },                                               // SCR_BOX

  { status_a_enter, nop_update, status_a_render, status_input, nop_leave, 0, 0 },
                                                                  // SCR_STATUS
  { status_b_enter, nop_update, status_b_render, status_input, nop_leave, 0, 0 },
                                                                  // SCR_STATUS_B

  // NETWORK (P5-C3). SF_OWNS_BACK because B means CANCEL THE SCAN here, and a
  // scan holds the radio: turning B into a plain BACK would leave the screen
  // and the radio behind it. Its leave hook cancels anyway, so there is no
  // route off this screen that leaves Wi-Fi up (spec sections 40 and 47).
  { network_enter, network_update, network_render, network_input, network_leave,
    0, SF_OWNS_BACK },                                            // SCR_NETWORK
  // LINK is a placeholder with a screen of its own (P2-C11c). The BLE peer
  // browser that used to live here owned RADIO_BLE on entry; nothing does now.
  { nop_enter, nop_update, link_render, link_input, nop_leave, 0, 0 },
                                                                  // SCR_LINK
  // CREATOR: its enter/leave hooks are the radio - request on the way in,
  // release on the way out, which is the whole "radio OFF by default" policy
  // for the Wi-Fi station (plan section 2 row G4).
  { creator_enter, creator_update, creator_render, creator_input, creator_leave,
    0, 0 },                                                       // SCR_CREATOR
  // SETTINGS owns B because its "Acerca de" page is one level below the
  // navigation stack: B closes the page, and only then the screen.
  { settings_enter, nop_update, settings_render, settings_input, nop_leave, 0,
    SF_OWNS_BACK },                                               // SCR_SETTINGS
  // TIME owns every gesture (SF_LOCK_INPUT), because B means "+1" here and
  // BACK everywhere else, and it never times out (SF_STICKY), because throwing
  // away a half-entered date after 20 s of thinking would be wrong. Its update
  // hook drives the right button's auto-repeat.
  { time_enter, time_update, time_render, time_input, nop_leave, 0,
    SF_STICKY | SF_LOCK_INPUT },                                  // SCR_TIME

  // --- the two OVERLAYS -----------------------------------------------------
  // CONFIRM and ALERT float over whatever screen is up and never become
  // sm_current(): ui/dialog.cpp owns them and ui_screen() is what reports
  // them. Their rows are unreachable by construction and exist so that the
  // table has no hole - see ui/screen_soon.h.
  { nop_enter, nop_update, soon_generic, soon_input, nop_leave, 0, 0 },
                                                                  // SCR_CONFIRM
  { nop_enter, nop_update, soon_generic, soon_input, nop_leave, 0, 0 },
                                                                  // SCR_ALERT

  // --- the section 6 transient states that are not built yet ----------------
  // ENCOUNTER and CAPTURE (P5-C3, P5-C4), one module, two rows. Neither is
  // SF_STICKY: a transient the player walked away from should time out to HOME
  // like any other screen, and the encounter has already been paid for (the
  // item is in the bag, the cooldown is armed) by the time it is drawn.
  { encounter_enter, nop_update, encounter_render, encounter_input,
    encounter_leave, 0, 0 },                                      // SCR_ENCOUNTER
  // CAPTURE owns B because it walks back to the encounter one level at a time,
  // exactly as the BOX walks its modes.
  { capture_enter, nop_update, capture_render, capture_input, capture_leave, 0,
    SF_OWNS_BACK },                                               // SCR_CAPTURE
  // BATTLE (P4-C4). SF_OWNS_BACK: B walks its ladder one level at a time -
  // SWITCH back to MENU, RESOLVE to the end of the round - and only the top
  // mode leaves the screen, exactly as the BOX does. SF_STICKY because
  // invariant 3 dropping the player on HOME twenty seconds into a fight would
  // abandon it. NOT SF_OWNS_FRAME: the toast layer is how this screen says
  // "ahora no puedes" and how ui.cpp says a win paid XP, and an owns-frame
  // screen is handed neither.
  { battle_enter, battle_update, battle_render, battle_input, battle_leave, 0,
    SF_STICKY | SF_OWNS_BACK },                                   // SCR_BATTLE
  { nop_enter, nop_update, soon_trade,       soon_input, nop_leave, 0, 0 },
                                                                  // SCR_TRADE
  { nop_enter, nop_update, soon_breed,       soon_input, nop_leave, 0, 0 },
                                                                  // SCR_BREED

  // EVOLUTION. SF_STICKY because an incubating egg is a place to stand, and
  // SF_OWNS_BACK because the rub ALTERNATES the two buttons: B is half of the
  // gesture that hatches the egg, so the router may not spend it on BACK.
  // LONG_BOTH still leaves. The ceremony's "no chrome, no buttons" is DYNAMIC
  // and belongs to the ceremony: ui_input_locked() drops every gesture while
  // one runs, and ui_draw() treats ceremony_active() as an owns-frame.
  { evo_enter, nop_update, evo_render, evo_input, nop_leave, 0,
    SF_STICKY | SF_OWNS_BACK },                                   // SCR_EVOLUTION

  { nop_enter, nop_update, soon_item_reward, soon_input, nop_leave, 0, 0 },
                                                                  // SCR_ITEM_REWARD

  // ERROR holds the device on an unanswered question, so it owns every gesture
  // - and FPS_LOW, because nothing on it moves and the blinking LED is driven
  // by its update hook, not by the frame rate.
  { err_enter, err_update, err_render, err_input, err_leave, FPS_LOW,
    SF_STICKY | SF_LOCK_INPUT },                                  // SCR_ERROR

  { nop_enter, nop_update, soon_sleep,       soon_input, nop_leave, 0, 0 },
                                                                  // SCR_SLEEP

  // DIAG. All three of the old flags: no auto-return off a console somebody is
  // typing into, no global grammar over its own, and it draws its own frame.
  { nop_enter, diag_update, diag_render, diag_input, nop_leave, 0,
    SF_STICKY | SF_LOCK_INPUT | SF_OWNS_FRAME },                  // SCR_DIAG
};

// If the enum is renumbered again, these fire and the rows must move with it.
static_assert((int)SCR_COUNT == 27, "screen table: rows and ScreenId drifted apart");
static_assert((int)SCR_BOOT  ==  0, "screen table: BOOT is the first state");
static_assert((int)SCR_DIAG  == 26, "screen table: DIAG is the last state");
