// =============================================================================
//  PEBBLEBOL - ui/dialog.h
//  THE MODAL LAYER: the CONFIRM dialog, the ALERT overlay and the one-line
//  HELP strip, migrated out of ui.cpp by P2-C11c (plan 1.4 `ui/dialog.h/.cpp`).
//
//  These three are NOT screens. They never become sm_current(); they float
//  over whatever screen is up, which is why they have no ScreenDef row and
//  why ui_screen() reports SCR_CONFIRM / SCR_ALERT while one is open. What
//  they share with a migrated screen is everything else: this is a PURE
//  translation unit that draws through gfx.h, so tests/test_screens.cpp
//  renders and drives it on the host.
//
//  INVARIANT 5 (every confirmation starts on NO) and INVARIANT 7 (an alert
//  never steals a press) are implemented here and nowhere else. Invariant 7
//  is the audit's S11 defect (audit section 8.3): the old handle_alert() ran
//  alert_act(), so the first press after the 1.2 s read guard both dismissed
//  the alert AND performed whatever it suggested - a player reaching for the
//  MENU fed the pet instead. The remedy jump is gone; dismissal is all the
//  first gesture may ever do.
//
//  Identifiers and comments: English. User-facing bytes: core/strings_es.h.
// =============================================================================
#ifndef PB_DIALOG_H
#define PB_DIALOG_H

#include <stdint.h>

#include "../core/config.h"
#include "../core/nt_types.h"

// What is on top of the screen right now.
enum UiModal : uint8_t {
  MODAL_NONE = 0,
  MODAL_ALERT,      // the ALERT overlay
  MODAL_CONFIRM,    // the CONFIRM dialog
  MODAL_HELP        // BOTH on a list row, UI_MODAL_HELP_MS
};

// What a CONFIRM is asking about. The dialog layer does not know what any of
// these MEAN: it collects the answer and hands the id to the bound commit
// callback, which is ui.cpp's, because committing needs the simulation, the
// Box and flash.
enum ConfirmId : uint8_t {
  CFM_NONE = 0,
  CFM_QUIT_GAME,
  CFM_MEDICINE,
  CFM_WIPE1,
  CFM_WIPE2,
  // The Box release, spec section 9. Two dialogs for the same reason the wipe
  // has two: it is destructive and there is no undo (invariant B4).
  CFM_BOX_REL1,
  CFM_BOX_REL2,
  // Spec section 18: an evolution is OFFERED, never imposed. Invariant 5 puts
  // the cursor on NO, so a stray press declines - and a decline costs nothing
  // and leaves EVO_STATE_PENDING set, because section 27 forbids punishing the
  // player and a "no" now is not a "no" for ever.
  CFM_EVOLVE
};

typedef void (*DialogCommitFn)(uint8_t confirm_id);
void    dialog_bind_commit(DialogCommitFn fn);

// State.
uint8_t dialog_modal(void);          // UiModal
uint8_t dialog_confirm_id(void);     // ConfirmId, or CFM_NONE
uint8_t dialog_alert_id(void);       // AlertId, or AL_NONE
uint8_t dialog_confirm_yes(void);    // the cursor, for the tests

// Open / close.
void    dialog_open_confirm(uint8_t confirm_id, uint16_t str_id);
void    dialog_open_help(uint16_t str_id);
void    dialog_close(void);
void    dialog_reset(void);          // close AND empty the alert queue

// Queue an alert. Duplicates collapse, UI_ALERT_QUEUE deep, and the queue is
// drained by dialog_service() only when nothing else owns the screen.
void    dialog_alert(uint8_t alert_id);
uint8_t dialog_alert_pending(void);  // how many are waiting

// Per-loop pump. `allow_pop` is the caller's "nothing else owns the screen"
// answer (a running minigame does not get an alert dropped on it). Returns
// true when it raised one, so the caller can request a frame.
bool    dialog_service(uint32_t now_ms, bool allow_pop);

// One gesture. Returns true when the modal layer consumed it, which it always
// does while one is open: that IS invariant 7.
bool    dialog_input(Gesture g);

// One frame, over whatever the screen underneath already drew.
void    dialog_render(void);

#endif  // PB_DIALOG_H
