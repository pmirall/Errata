// =============================================================================
//  NOTTAMAGOCHI - petfx.h
//  "The creature layer": everything that turns a centred 2-frame sprite into
//  something that looks alive. Owns the pet's X position, its facing, the
//  ground it stands on, its shadow, its blink and the cosmetic genes that had
//  no pixels of their own until now (pattern, body_size, rare, mutations).
//
//  Why a separate module: ui.cpp decides WHAT is on screen (which screen,
//  which pose, which emote). petfx decides HOW the body sits in the world.
//  Mixing the two is what froze the pet in the middle of the panel for the
//  whole project so far.
//
//  Layering: petfx includes render/sprites/genome/sim and nothing else. It
//  never touches NVS, never talks to the network and never blocks.
//
//  Identifiers and comments: English. No user-facing text lives here at all -
//  this module draws pixels, never glyphs, so it needs no strings_es.h.
// =============================================================================
#ifndef NT_PETFX_H
#define NT_PETFX_H

#include <stdint.h>

#include "config.h"
#include "nt_types.h"

// -----------------------------------------------------------------------------
// THE GROUND LINE.
//
// Everything in this module hangs off one number. The body is placed so that
// the LAST ROW OF ITS INK lands on PETFX_FLOOR_Y - 1, not so that the sprite
// BOX is centred in the band: the art has empty margins (spr_adult_bolota is a
// 40x40 box whose ink only spans rows 1..36) and centring the box makes every
// species float at a different height above nothing.
//
// 52 is the largest value that still leaves the two shadow rows (53, 54) and
// one spare row (55) inside the sprite band, which contractually ends where
// the affordance strip starts at row 56. It also guarantees the top of the
// tallest body stays inside the band: a 40 px sprite whose ink reaches its
// last row lands at y = 52 - 40 = 12, still below SPRITE_AREA_Y (9).
// -----------------------------------------------------------------------------
#define PETFX_FLOOR_Y        52
#define PETFX_SHADOW_Y       (PETFX_FLOOR_Y + 1)

static_assert(PETFX_FLOOR_Y + 3 <= OLED_H - AFFORDANCE_BAR_H,
              "floor + shadow must stay inside the sprite band");
static_assert(PETFX_FLOOR_Y - 40 >= SPRITE_AREA_Y,
              "the tallest body must still start inside the sprite band");

// -----------------------------------------------------------------------------
// THE STAGE.  Read this before moving anything horizontally.
//
// The sprite band is 128 columns wide but the ACTOR only owns the middle of it.
// Columns 0..PETFX_STAGE_L-1 and PETFX_STAGE_R+1..OLED_W-1 are HUD: the left
// band is reserved at UI_HUD_L_X (columns 2..13) and draw_home() puts an opaque
// 12x12 mood face at UI_HUD_R_X (columns 115..126). The badge is drawn AFTER
// the body and must stay readable at all times. config.h owns those numbers;
// the assert below is what ties this stage to them.
//
// A 1-bit framebuffer cannot deliver "the badge is always legible" AND "the body
// is never damaged" while the two share pixels: whoever draws second wins and
// destroys the other. So they do not share pixels. The body is CONFINED, once,
// here, and every clamp in petfx.cpp derives from these two numbers - there is
// no second, softer keep-out anywhere. That is the whole contract, and it
// replaces a round of pairwise patches that could not have converged.
//
// The cost is small and honest. The widest body is 40 px, so an adult keeps x
// in 14..74: 61 columns of stage, against the 0 it had while it was nailed to
// sprite_center_x(). Emotes hang OFF the body, so they are the one thing that
// still has business outside the stage - and ui.cpp's emote_x() now treats a
// destination inside the HUD columns as "does not fit" and reflects the emote
// to the other side of the body, because a badge drawn on top of an emote
// deletes it exactly as thoroughly as the panel edge used to.
// -----------------------------------------------------------------------------
#define PETFX_STAGE_L        14
#define PETFX_STAGE_R        113

static_assert(PETFX_STAGE_L >= 0 && PETFX_STAGE_R < OLED_W,
              "the stage must fit on the panel");
static_assert(PETFX_STAGE_R + 1 - PETFX_STAGE_L >= 40,
              "the stage must be at least one adult body wide");

// The two asserts above only check the stage against itself and against the
// panel, which is why four rounds of review kept finding the same bug in new
// clothes: nothing in the build knew where the badges actually were. This one
// does. Move a badge, widen one to 14 px or add a third and the compiler stops
// you here instead of the bench stopping you later.
static_assert(PETFX_STAGE_L >= UI_HUD_L_END && PETFX_STAGE_R < UI_HUD_R_BEGIN,
              "the stage overlaps the HUD badge columns - see UI_HUD_* in config.h");

// =============================================================================
//  LIFECYCLE
// =============================================================================

// One-time init. Safe to call before a pet exists; petfx_service() calls it
// itself if the caller forgot.
void petfx_begin(void);

// Reseed the automaton from this pet's identity. Call on boot, on hatch and
// whenever the pet changes. Seeding from lineage_id ^ genome makes
// the SAME pet always move the same way and siblings move differently, so
// motion becomes a visible, heritable trait.
void petfx_reset(const PetSave& p);

// Advance the behaviour automaton. Call once per loop() from ui_service().
// Cheap (a few hundred cycles) and never blocks; it is time-based, so calling
// it at an irregular rate is fine, and a stall longer than 250 ms is clamped
// so the pet walks instead of teleporting.
void petfx_service(const PetSave& p, uint32_t now_ms);

// =============================================================================
//  DRAWING  (inside a rd_begin_frame() / rd_end_frame() pair)
// =============================================================================

// The dotted floor line the pet stands on. Draw before the body.
void petfx_draw_floor(void);

// Shadow + (mirrored) body + coat pattern + blink + squash. dy is an extra
// vertical nudge the caller wants applied (the "nope" wiggle).
// `pose` is a SpritePose, `frame` the caller's 2-frame animation phase.
//
// dx is the HORIZONTAL nudge, and it exists because vertical does not fit.
// A choreography that wants the animal to lunge at something has nowhere to go
// up (the band is 47 rows and an adult is 40) and nowhere to go down (the ink
// already stands on PETFX_FLOOR_Y - 1, and petfx_draw_body()'s own clear box is
// clamped to that row, so a downward nudge only moves the TOP edge). Sideways
// it has the whole width of the stage. So a bite is a 3 px lunge towards the
// bowl and back, and this is where it is applied - to x, before the SAME stage
// clamp everything else in this file goes through, so a lunge cannot push the
// body into the HUD columns however hard the caller pushes.
//
// It defaults to 0 so the ceremonies, which have no lunge, are unchanged.
void petfx_draw_body(const PetSave& p, uint8_t pose, uint8_t frame, int16_t dy,
                     int16_t dx = 0);

// Where the body actually landed this frame, so the caller can hang emotes
// off it. Valid after petfx_draw_body().
int16_t petfx_body_x(void);
int16_t petfx_body_y(void);
uint8_t petfx_body_w(void);
uint8_t petfx_body_h(void);

// =============================================================================
//  NUDGES FROM THE OUTSIDE
// =============================================================================

// A wall bump, a refused action: recoil away from where it was looking
// and hold still for ms. Overrides whatever the automaton was doing.
void petfx_startle(uint16_t ms);

// The player pressed a button: turn and look. Also resets the boredom timer,
// so call it on EVERY input, not only on the ones that reach the pet.
void petfx_attention(void);

// Face something at screen x (a food icon, a poop, the other pet in a BLE
// meeting). Enters a short TURN if that means turning round.
void petfx_face_point(int16_t x);

// Ceremonies (evolution, hatch, the birthday card) pin the pet in the centre
// so the choreography can rely on its position. 1 = pinned, 0 = free.
void petfx_freeze(uint8_t on);

// SUSPEND WITHOUT CENTRING. An ACTION choreography (actfx.cpp: eating, being
// washed, being medicated) needs the pet to stay still WHERE IT IS, because the
// bowl slides in and parks against the body - so the body must not move, and it
// must certainly not teleport to the middle of the panel first.
//
//   hold   - the automaton stops picking new states and stops moving x. The pet
//            keeps its position, keeps blinking, keeps obeying
//            petfx_face_point() and keeps whatever bob the caller passes as dy.
//            It is the PF_ASLEEP branch of the automaton reused verbatim,
//            minus being asleep: same suspension, same eyelids, no recentring.
//   freeze - all of that PLUS the body is pinned to sprite_center_x(). Right
//            for a ceremony, wrong for an action.
//
// Independent flags, so a hold taken during an evolution cannot cancel the
// freeze the evolution needs. Cleared by petfx_begin(); actfx_cancel() is what
// releases it in normal running, and actfx_service() bounds it in time so it
// cannot survive its choreography even if every explicit cancel were removed.
void petfx_hold(uint8_t on);

// The INK box of the body as petfx_draw_body() last drew it, in absolute screen
// coordinates, inclusive on all four sides. This is NOT the sprite box that
// petfx_body_x/y/w/h report: the art has empty margins (spr_adult_bolota is a
// 40x40 box whose ink only spans rows 1..36), so a prop parked against the
// SPRITE box would sit in a visible gap beside the animal. Any pointer may be
// null. Valid after petfx_draw_body(); before the first one it reports the
// resting box of a 40 px adult.
void petfx_body_ink(int16_t* x0, int16_t* y0, int16_t* x1, int16_t* y1);

// THE INK COLUMNS THE BODY WOULD HAVE IN `pose`, WITHOUT DRAWING IT.
//
// Why this is not a curiosity. A choreography parks a prop flush against the
// ink box on its first frame and then FREEZES it, because a bowl that shuffled
// sideways every UI_ANIM_FRAME_MS would read as a bug. But the pose changes
// halfway through the film - POSE_IDLE while the bowl slides in, POSE_EAT once
// the animal leans over it - and sprite_set_id() maps POSE_EAT onto one shared
// per-band set whose ink is WIDER than the idle it replaces in fifteen of the
// sixteen species: 1-3 px for an adult, and 9-12 px for a senior, whose idle
// body is 32 px and whose SPR_EAT_ADULT is 40. petfx_draw_body() then clears
// that new, wider ink box in colour 0 every frame and punches a rectangular
// hole out of the prop parked in it - measured at up to 43 of a bowl's 63
// pixels. So the prop has to be parked against the WIDEST wall the film can
// produce, and only this module can say where that is.
//
// The answer is deliberately generous, and covers ALL of:
//   * both animation frames (24 of the 38 sets differ between frame 0 and 1);
//   * both facings, because the pet turns to look at its dinner mid-film and a
//     mirrored ink span is not the span it was authored with;
//   * the 1 px dilation copy a fat or squashing body draws beside itself.
// Erring wide costs a column or two of gap; erring narrow costs a hole in the
// prop, which is the defect this exists to close.
//
// Valid after the first petfx_draw_body(): it answers for the pet that was
// drawn, at the x it is standing on now. Before that it reports the live ink
// box, which is the honest "I do not know yet".
void petfx_pose_ink_x(uint8_t pose, int16_t* x0, int16_t* x1);

// COMPRESS THE BODY AGAINST THE FLOOR for ms, then let it go.
//
// This is the squash petfx already draws on a landing - sprite row 0 dropped,
// the remaining rows keeping their screen position, one column of dilation -
// exposed so that a choreography can use it as an EXPRESSION. A yawn and a
// waking stretch are compressions: the animal presses into the ground and comes
// back. They are NOT lifts. The body does not leave PETFX_FLOOR_Y - 1 except in
// an explicit hop, which is the whole contract the floor line exists to state,
// and a "yawn" implemented as a 4 px lift renders as a sleeping pet levitating
// away from its own shadow.
//
// It is a separate latch from the landing squash on purpose: petfx_service()
// zeroes that one on every suspended tick (a choreography holds the automaton),
// so the two would fight once per loop(). Time-bounded like every other nudge
// here - the caller renews it while it wants it and it releases itself ms after
// the last call, so a forgotten compression cannot leave a permanently squat
// pet. ms == 0 releases it now.
void petfx_squash(uint16_t ms);

// Register the props the walk must not walk into: `n` boxes, each `w` px wide,
// left edges in xs[], all standing on the floor line. ui.cpp feeds it the live
// poops. Pass n = 0 to clear.
//
// A poop and a body are both solid silhouettes on a 1-bit panel, so a body
// standing on one fuses with it into a single blob and the coat dither then
// hatches the poop as if it were fur. The fix is behavioural, not a drawing
// order: the pet treats a prop as a wall, walks up to it, stops and turns.
// A pet that is ALREADY overlapping one (the poop appeared underneath it, or
// the list changed) is never teleported out - it walks out and respects the
// prop from the moment it is clear.
//
// The caller keeps ownership of xs[]; petfx copies what it needs.
void petfx_set_obstacles(const int16_t* xs, uint8_t n, uint8_t w);

#endif  // NT_PETFX_H
