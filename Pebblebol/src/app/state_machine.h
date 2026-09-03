// =============================================================================
//  PEBBLEBOL - app/state_machine.h
//  THE NAVIGATION STATE MACHINE (plan P2-C11, section 1.4).
//
//  It owns the current screen, the five-deep back stack and the two clocks the
//  navigation grammar runs on (time on this screen, time since the last
//  gesture). ui.cpp used to own all of that as file-scope statics; moving it
//  here is what lets the screen table dispatch and what makes the plan's exit
//  gate - `grep -cE "s_screen\s*=" src/` == 0 outside this file - mean
//  something.
//
//  THE TABLE IS COMPLETE (P2-C11d). Every ScreenId has a row with all five
//  hooks, so sm_handle/sm_service/sm_draw dispatch unconditionally and there is
//  no legacy switch left to fall through to. The presentation work that is
//  still ui.cpp's - closing the modal, cutting the shared interpolators, the
//  entry dissolve and the frame rate - is reached through the two ui_nav_*
//  seams in ui.h.
//
//  Identifiers and comments: English.
// =============================================================================
#ifndef NT_STATE_MACHINE_H
#define NT_STATE_MACHINE_H

#include <stdint.h>

#include "../core/nt_types.h"
#include "../ui/screen.h"

// Reset to HOME with an empty stack. Called from ui_begin().
void     sm_begin(void);

// Force a screen. Runs leave(old) / enter(new), cuts the interpolators, resets
// both clocks and applies the screen's frame rate. Every other mover funnels
// through this one, exactly as ui_goto() used to.
void     sm_goto(ScreenId s);

// Remember where we are (UI_STACK_DEPTH deep; once full the NEWEST push is
// dropped and the existing trail is kept, which is what nav_push always did) and go to s.
void     sm_push(ScreenId s);

// Back to whatever sm_push() remembered, or HOME when the stack is empty.
void     sm_back(void);

// HOME with the stack cleared.
void     sm_home(void);

// Clear the stack and go to s. For the three paths that used to assign
// s_screen by hand (the god-mode entry, the hatch ceremony, a factory reset):
// they are re-rooting the navigation, not pushing onto it.
void     sm_replace_root(ScreenId s);

// The screen the machine is on. ui_screen() reports the modal layer on top of
// it; this is the base.
ScreenId sm_current(void);

// The row for the current screen. Never NULL for an id inside the enum.
const ScreenDef* sm_def(void);

// Per-loop pump: runs the current screen's update hook and the 20 s
// auto-return (invariant 3), which is skipped for SF_STICKY screens and while
// sm_block_autoreturn(true) is in force. Returns true when it navigated, so
// the caller can stop touching a screen that is already gone.
bool     sm_service(uint32_t now_ms);

// Feed one gesture to the current screen. app/input_router.cpp calls this
// AFTER the section 7 global grammar has had its say. False only for an id
// outside the enum.
bool     sm_handle(Gesture g);

// Draw the current screen. False only for an id outside the enum.
bool     sm_draw(void);

// "The user just did something": restarts the auto-return countdown.
void     sm_note_input(void);

// Milliseconds since the last sm_note_input() / screen change, and since the
// current screen was entered.
uint32_t sm_idle_ms(void);
uint32_t sm_screen_ms(void);

// Invariant 3 does not apply to this screen.
bool     sm_is_sticky(void);

// Freeze the auto-return while a modal owns the screen: the countdown that is
// running belongs to the screen underneath, and it must neither fire nor
// restart while a question is on top of it.
void     sm_block_autoreturn(bool blocked);

#endif  // NT_STATE_MACHINE_H
