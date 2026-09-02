// =============================================================================
//  NOTTAMAGOCHI - actfx.h
//  "The action layer": the choreography of every single thing the player can
//  DO to the pet. Feeding it, washing it, medicating it, stroking it, telling
//  it off, playing with it, putting it to bed, hitting the light.
//
//  WHY IT IS ITS OWN MODULE. ui.cpp is 139 KB and already owns sixteen screens,
//  three minigames, two ceremonies, a modal layer and an alert queue; petfx.cpp
//  owns the body's idle life and must keep owning only that. A choreography is
//  neither: it is a short, scripted, INTERRUPTIBLE film that borrows the body,
//  the floor and a handful of emote sprites for one and a half to two and a
//  half seconds. Giving it its own file keeps the seam explicit - ui decides
//  WHICH action happened, petfx decides where the body is, actfx decides what
//  the moment looks like.
//
//  LAYERING. actfx includes config / nt_types / sprites / render / petfx and
//  nothing else. It never touches sim (it is handed everything it needs to
//  know about the pet), never touches NVS, never talks to a radio, never
//  blocks and never allocates.
//
//  THE TWO DRAWING LAYERS, and why there are exactly two. draw_home()'s
//  composition order is a contract (see the long comment there):
//
//      floor -> poops -> BODY -> emotes -> HUD badge ->
//      banner / toast -> affordance strip
//
//  A choreography has things that belong on the GROUND, in front of the
//  scenery but BEHIND the animal (the bowl it eats from, the bubbles that sweep
//  past its feet, the ball it watches, the crumbs it drops), and things that
//  belong ON the animal (the pill, the germ, the hearts, the sparks). Those are
//  two different slots in that order, so this module has two entry points and
//  ui.cpp calls them in the two places they belong. There is deliberately no
//  third: anything that wanted to be drawn after the HUD badge would be
//  writing in the HUD's columns, and this module cannot do that (see below).
//
//    actfx_draw_props()  goes with the poops, BEFORE the body. Whatever it puts
//                        under the body is erased by petfx_draw_body()'s ink-box
//                        clear, which is the correct reading: the animal passes
//                        in front of its own props.
//    actfx_draw_over()   goes with the emotes, AFTER the body. Every pixel it
//                        writes is ADDITIVE - it ORs, it never clears, and it
//                        never blits opaquely - so it is arithmetically
//                        incapable of taking ink off the body. That is the one
//                        promise this module has to keep, and it is kept by
//                        construction rather than by care: there is exactly one
//                        pixel writer in actfx.cpp and it only ever sets.
//
//  THE STAGE. Every prop is clipped to columns PETFX_STAGE_L..PETFX_STAGE_R and
//  to rows SPRITE_AREA_Y..RD_AFFORD_Y-1, in the same single pixel writer, so
//  "nothing of mine reaches the HUD badges, the status bar or the affordance
//  strip" is likewise a property of the module. The documented exception is
//  ACT_CLEAN's poop layer - the dissolving replicas, the bubbles that wash them
//  and the sparks that mark where they stood. All three are clipped to the
//  PANEL rather than to the stage, because the poops themselves live outside
//  the stage on purpose (draw_poop() puts two of them at x = 2 and x = 114) and
//  a cleaning animation that stopped at the stage edge would wash three quarters
//  of the mess. They all live in rows 43..55, which no HUD badge reaches - the
//  badges are 12x12 at row SPRITE_AREA_Y+1, i.e. rows 10..21 - and both badges
//  are still stamped opaquely after this layer. See actfx.cpp.
//
//  Identifiers and comments: English. This module draws pixels, never glyphs,
//  so it needs no strings_es.h and adds no user-facing text at all.
// =============================================================================
#ifndef NT_ACTFX_H
#define NT_ACTFX_H

#include <stdint.h>

#include "config.h"
#include "nt_types.h"

// -----------------------------------------------------------------------------
//  LIFECYCLE
// -----------------------------------------------------------------------------

// Start the choreography for `action` (an ActionId). Cancels whatever was
// running - actions do not queue, the newest one wins, because a queue would
// let a player who taps four times watch eight seconds of film they cannot
// interrupt.
//
// `before` is the pet AS IT WAS BEFORE sim_apply_action() ran, and it is not a
// nicety: sim_apply_action(ACT_CLEAN) sets poop_count to 0 on the spot, so a
// copy taken afterwards has nothing left to dissolve. Every caller has to take
// the copy first. Only a SUCCESSFUL action may be announced here; a rejected
// one (cooldown, full, asleep, sulking) gets its toast and no film.
void    actfx_begin(uint8_t action, const PetSave& before);

// Advance the clock. MUST be called once per loop() from ui_service(), above
// every early return in it and regardless of which screen is up. That is what
// bounds the petfx_hold() this module takes: even if every explicit
// actfx_cancel() in the firmware were deleted, the hold would still be released
// within one choreography (2.6 s worst case) because the timer keeps running.
void    actfx_service(uint32_t now_ms);

// Non-zero while a choreography is running. ui.cpp uses it to pin the frame
// rate at FPS_NORMAL and to decide whether an action is worth sending the
// player back to HOME for.
uint8_t actfx_active(void);

// Give up now: release petfx_hold() and forget the film. Call on every exit
// from HOME, on death, on hatch, on entering god mode and at ui_begin().
void    actfx_cancel(void);

// -----------------------------------------------------------------------------
//  WHAT THE BODY DOES WHILE THE FILM RUNS
// -----------------------------------------------------------------------------

// The pose the body should be drawn in: POSE_EAT while it is chewing, POSE_SICK
// while the germ is still on it. Returns `fallback` (what ui.cpp would have
// used) whenever the choreography has no opinion, which is most of the time.
uint8_t actfx_pose(uint8_t fallback);

// WHILE THIS IS NON-ZERO, THE BODY'S OFFSETS BELONG TO THIS MODULE ALONE.
// draw_home() must then use actfx_body_dy() / actfx_body_dx() as they are and
// must NOT add its idle bob to them.
//
// It is not tidiness. The idle bob is +/-1 px on a 220 ms cycle and it never
// stops; the accent of a bite is 1 px. Summed, the gesture the player pressed a
// button for is smaller than the noise it is added to, and the pet reads as
// bobbing exactly as it was before - which is what the first version of this
// module actually did, measured frame by frame on the framebuffer. One owner of
// the offset at a time; while a film is running, that owner is this file.
uint8_t actfx_owns_body(void);

// Extra vertical nudge for the body this frame: the lean over the bowl, the dip
// of each bite, the press of a yawn, the satisfied hop. Small (-5..+2), always
// 0 when no choreography is running, and the WHOLE vertical story while one is
// (see actfx_owns_body()).
int16_t actfx_body_dy(void);

// Extra HORIZONTAL nudge, and the reason the bite is visible at all.
//
// There is no room to bite downwards. The body's ink already stands on
// PETFX_FLOOR_Y - 1 and petfx_draw_body() clamps its own clear box to that row,
// so a downward dip only moves the TOP edge of the silhouette; and there is no
// room upwards either, because the band is 47 rows and an adult is 40. What
// there IS room for is sideways: the stage is 100 columns wide. So a bite is a
// LUNGE - the whole animal drives 3 px at the bowl and comes back - which is
// both visible and what an animal eating actually looks like.
//
// Positive is right. Always 0 when no choreography is running. draw_home()
// hands it to petfx_draw_body(), which applies it inside the SAME stage clamp
// as everything else, so a lunge can never reach the HUD columns.
int16_t actfx_body_dx(void);

// -----------------------------------------------------------------------------
//  DRAWING  (inside a rd_begin_frame() / rd_end_frame() pair)
// -----------------------------------------------------------------------------

// Ground layer. Call from draw_home() immediately after draw_poop(), i.e.
// BEFORE petfx_draw_body().
void    actfx_draw_props(void);

// Emote layer. Call from draw_pet_body() at the very end, i.e. AFTER
// petfx_draw_body() and after ui.cpp's own emotes, and BEFORE the HUD badges.
//
// It also samples the body box petfx_draw_body() just produced, which is why it
// must be called even on frames where it has nothing to draw: actfx_draw_props()
// parks its props against that sample and stays blank until it exists.
void    actfx_draw_over(void);

// -----------------------------------------------------------------------------
//  BINDING
// -----------------------------------------------------------------------------

// Where the poops stand. ui.cpp owns that table (kPoopX) because it is also
// what petfx_set_obstacles() is fed; this module only needs to know it so
// ACT_CLEAN can dissolve poops that no longer exist in the model. Call once
// from ui_begin(). The caller keeps ownership of xs[] - it must outlive the UI,
// which a file-scope const table does.
void    actfx_bind_poop_layout(const int16_t* xs, uint8_t n);

#endif  // NT_ACTFX_H
