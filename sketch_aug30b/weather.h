// =============================================================================
//  weather.h - Open-Meteo current conditions + ip-api geolocation, plain HTTP.
//
//  Owns:  the WeatherState singleton, the non-blocking fetch state machine,
//         the WMO -> WeatherGroup table, the integer multipliers derived from
//         GAME_DESIGN 6.1 / 6.2 / 6.4, and the animated on-screen overlay.
//
//  Layering: this header pulls in NOTHING but nt_types.h. U8G2 is forward
//  declared so that sim.cpp / genome.cpp may include weather.h for the
//  multipliers without dragging U8g2lib.h or WiFi.h into their translation
//  units (BRIEF 4, layering rule). Only weather.cpp includes those.
//
//  Units: every multiplier is an integer PER-MILLE value (1000 == x1.00).
//  There is no floating point anywhere in this module.
// =============================================================================
#ifndef WEATHER_H
#define WEATHER_H

#include "nt_types.h"

// Forward declaration only. weather.cpp includes <U8g2lib.h>; consumers that
// merely want the multipliers never see it.
class U8G2;

// -----------------------------------------------------------------------------
// Lifecycle
// -----------------------------------------------------------------------------

// Bind the entry point's live Config, exactly like ui_bind_config() and
// web_bind_config(). Call BEFORE wx_begin().
//
// Without it weather keeps a private Config scratch, reads it from NVS and
// writes it straight back after an ip-api geolocation - behind the back of the
// g_cfg every other module holds a pointer to. NVS then held the auto-detected
// coordinates while g_cfg in RAM still held the empty strings, so the next save
// from S9 SETTINGS or POST /api/cfg silently reverted the location (and an
// unsaved S9 edit was silently reverted in NVS by the weather write). Bound,
// there is exactly one Config object and one writer path (PH3 finding 9).
//
// Passing nullptr restores the standalone scratch behaviour.
void wx_bind_config(Config *cfg);

// Zero the state, seed the coordinates from Config (falling back to the
// CFG_LATITUDE / CFG_LONGITUDE compile-time defaults) and arm the first poll.
// Call AFTER store_begin() and after wx_bind_config(), from setup().
void wx_begin(void);

// Drive the fetch state machine. Call from loop() on every iteration; it
// returns immediately when there is nothing to do and never runs a full
// request to completion in one call. Also maintains WeatherState::age_s and
// demotes the data to WX_UNKNOWN once it is older than WX_STALE_S.
void wx_poll(void);

// -----------------------------------------------------------------------------
// Query
// -----------------------------------------------------------------------------

// The live singleton. Never null, always readable, `valid == 0` means
// "no idea what the weather is" and every multiplier below returns neutral.
const WeatherState &wx_state(void);

// Multiplier (per-mille) on the happiness decay rate.  GAME_DESIGN 6.1 + 6.2,
// with the 6.4 temperament inversion and the "umbrella" rule (bond > 60 in
// the rain) applied. Both arguments are optional: called with no arguments
// this returns the plain table value for a neutral pet.
uint16_t wx_mult_hap(uint8_t temperament = GENE_NEUTRAL, uint8_t bond_pct = 0);

// Multiplier (per-mille) on the energy decay rate. GAME_DESIGN 6.1 + 6.2.
uint16_t wx_mult_en(void);

// Multiplier (per-mille) on the hunger decay rate. Temperature-driven only
// (GAME_DESIGN 6.2); the WMO group has no hunger column.
uint16_t wx_mult_hunger(void);

// Additive sickness probability bonus, PER-MILLE PER HOUR - the same unit as
// SICK_BASE_PPH and friends in config.h. GAME_DESIGN 6.1 `sick` column plus
// the 6.2 heat rows.
uint16_t wx_sick_bonus_pph(void);

// Flat offset added to the displayed mood score (never to a stat).
// GAME_DESIGN 6.1 `mood` column, 6.2 temperature bonus, the >= 40 km/h wind
// penalty and the 6.4 temperament inversion.
int8_t wx_mood_offset(uint8_t temperament = GENE_NEUTRAL);

// -----------------------------------------------------------------------------
// God mode
// -----------------------------------------------------------------------------

// Pin the weather to `group` (a WeatherGroup) and set WeatherState::forced.
// Temperature and wind are parked at neutral values so the group can be tested
// in isolation. Any value >= WX_COUNT (use 0xFF) releases the override and
// schedules an immediate refetch.
void wx_force(uint8_t group);

// -----------------------------------------------------------------------------
// Render
// -----------------------------------------------------------------------------

// The overlay is split in two, and the split is a SCENE-ORDER contract that
// draw_home() depends on (see the composition comment there and PETFX_STAGE_L
// in petfx.h).
//
//   wx_draw_backdrop()  - scenery: the sun and its glare and sparkle, the moon
//                         and stars, clouds, the overcast band, the fog band,
//                         the puddle, settled snow. Drawn BEHIND the body, and
//                         it HAS to be: fx_night() carves its crescent with a
//                         colour-0 drawDisc and fx_fog() runs a colour-2 XOR
//                         over the whole sprite area, so both DESTROY body ink
//                         when they run after it - the moon alone bit a 15 px
//                         disc out of anything standing in columns 97..111.
//   wx_draw_particles() - rain, drizzle, snow, hail, the wind streaks and the
//                         lightning scheduler. Drawn in FRONT of the body: rain
//                         falls between the player and the pet. Every one of
//                         them only ADDS ink, so none can damage the body.
//
// Call backdrop first and particles second, once each per frame. The frame
// clock, the group-change reset and the animation accumulators all live in
// wx_draw_backdrop(); calling particles on its own freezes the weather.
// Both leave the draw colour at 1, touch no font state, and draw nothing when
// the data is invalid or the group is WX_UNKNOWN.
void wx_draw_backdrop(U8G2 &u8g2);
void wx_draw_particles(U8G2 &u8g2);

// Backdrop then particles, back to back. For any caller that has nothing to
// put between them.
void wx_draw_overlay(U8G2 &u8g2);

// True on the single frame a lightning bolt is generated, and only while
// wx_draw_overlay() is actually being called. Reading it CONSUMES it.
//
// fx_lightning() deliberately no longer inverts the framebuffer itself: the UI
// answers this edge with rd_flash() (SSD1306 0xA7, 2 bytes) plus rd_shake()
// (0xD3), which is ~250x cheaper than the full-screen XOR it replaces. Do not
// reinstate the software inversion: the two would cancel each other out.
bool wx_lightning_edge(void);

#endif  // WEATHER_H
