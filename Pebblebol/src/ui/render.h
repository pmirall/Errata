// =============================================================================
//  PEBBLEBOL - ui/render.h
//  Owns THE single U8G2 instance, the frame scheduler and every drawing
//  primitive. No other translation unit may construct a U8G2 object: the
//  128x64 full-buffer constructors share one function-local static 1024 B
//  framebuffer (u8g2_d_memory.c:61-71), so a second instance corrupts the
//  first one silently.
//
//  Layering: render never includes WiFi.h / BLEDevice.h / WebServer.h.
//  The web-busy fps drop is pushed in from the entry point via
//  rd_note_web_activity(), not pulled from webui.
//
//  All user-facing text is Spanish and is therefore drawn ONLY through the
//  helpers below, which all use drawUTF8()/getUTF8Width() and _tf fonts.
//  Never call rd_u8g2().drawStr() on a string that came from strings_es.h.
//
//  Identifiers and comments: English. No Spanish literal lives in this module.
// =============================================================================
#ifndef NT_RENDER_H
#define NT_RENDER_H

#include <Arduino.h>
#include <U8g2lib.h>

#include "../core/config.h"
#include "../core/nt_types.h"

// -----------------------------------------------------------------------------
// Display type. Risk #2: the constructor MUST carry the pins,
// otherwise Wire falls back to the variant defaults SDA=8 (LED_BUILTIN) and
// SCL=9 (BOOT strapping pin) and the board looks bricked.
// The SH1106 class has the identical 4-argument signature (U8g2lib.h:2954).
// -----------------------------------------------------------------------------
#if DISPLAY_IS_SH1106
typedef U8G2_SH1106_128X64_NONAME_F_HW_I2C   RdDisplay;
#else
typedef U8G2_SSD1306_128X64_NONAME_F_HW_I2C  RdDisplay;
#endif

// -----------------------------------------------------------------------------
// FONTS - the five verified on the panel and nothing else.
// _tf = 191 glyphs = full Latin-1 -> safe for Spanish (a e i o u accents, n
// tilde, inverted marks). _tr / _tn are ASCII/digits only: legal for numbers,
// IP addresses and hex dumps, NEVER for Spanish prose.
// These are macros so the linker only pulls in the fonts actually referenced.
// -----------------------------------------------------------------------------
#define RD_FONT_BODY    u8g2_font_5x8_tf          // 1715 B, 5x8,  191 gl, asc 6 desc -1
#define RD_FONT_NARR    u8g2_font_6x10_tf         // 2000 B, 6x10, 191 gl, asc 7 desc -2
#define RD_FONT_HEAD    u8g2_font_t0_11b_tf       // 2095 B, 6x11, 191 gl, asc 8 desc -2  (bold)
#define RD_FONT_TINY    u8g2_font_4x6_tr          //  723 B, 4x6,   95 gl, asc 5 desc -1  (ASCII ONLY)
#define RD_FONT_BIGNUM  u8g2_font_logisoso16_tn   //  287 B, 9x19,  18 gl, asc 16 desc -4 (DIGITS ONLY)

// Ascent ('A' height above the baseline) and a comfortable line pitch.
#define RD_ASC_BODY     6
#define RD_ASC_NARR     7
#define RD_ASC_HEAD     8
#define RD_ASC_TINY     5
#define RD_ASC_BIGNUM   16

#define RD_LINE_BODY    8
#define RD_LINE_NARR    10
#define RD_LINE_HEAD    11
#define RD_LINE_TINY    7

// -----------------------------------------------------------------------------
// Dither levels. n/16 of the pixels are touched, distributed by a 4x4 ordered
// Bayer matrix (even, non-directional, tiles seamlessly with anything else).
// rd_dither_rect() honours the CURRENT draw colour:
//   setDrawColor(1) -> paints the pattern   (shading, texture, fog)
//   setDrawColor(0) -> erases the pattern   (fades, dissolves, "ghost" sprites)
//   setDrawColor(2) -> XORs it              (shimmer)
// The 4-step evolution dissolve is levels 4, 8, 12, 16 at colour 0.
// -----------------------------------------------------------------------------
#define RD_D0            0
#define RD_D12           2
#define RD_D25           4
#define RD_D50           8
#define RD_D75          12
#define RD_D88          14
#define RD_D100         16
#define RD_DITHER_MAX   16

// -----------------------------------------------------------------------------
// Stat-bar styles.
// -----------------------------------------------------------------------------
#define RD_BAR_AUTO      0    // solid, but hatched when pct <= RD_BAR_LOW_PCT
#define RD_BAR_SOLID     1
#define RD_BAR_DITHER    2
#define RD_BAR_LOW_PCT  20

// -----------------------------------------------------------------------------
// Affordance strip geometry. Exactly AFFORDANCE_BAR_H (8) rows, 56..63.
// The baseline sits at the last row so that accented capitals (ATRAS, MAS,
// DESPUES, SI) fit inside the strip without spilling into the sprite area.
// -----------------------------------------------------------------------------
#define RD_AFFORD_Y         (OLED_H - AFFORDANCE_BAR_H)   // 56, first row of the strip
#define RD_AFFORD_BASELINE  (OLED_H - 1)                  // 63
#define RD_AFFORD_TEXT_L    5                             // left label x
#define RD_AFFORD_TEXT_R    122                           // right label right edge

static_assert(STATUS_BAR_H + AFFORDANCE_BAR_H < OLED_H, "no room left for the sprite area");
static_assert(SPRITE_AREA_Y + SPRITE_AREA_H == RD_AFFORD_Y, "sprite area must end where the affordance strip starts");
static_assert(OLED_W == 128 && OLED_H == 64, "render.cpp fast paths assume a 128x64 panel");

// =============================================================================
//  LIFECYCLE
// =============================================================================

// Brings up Wire on PIN_SDA/PIN_SCL, scans the I2C bus, selects the OLED
// address (0x3C or 0x3D), starts U8g2, enables UTF-8 printing and applies the
// default contrast. Logs the whole scan to Serial.
// Returns false if nothing acknowledged at 0x3C/0x3D. The caller continues
// headless and shows the ERROR screen (plan T10); drawing after a false return
// is harmless (the buffer is written, the panel just never answers), and
// calling rd_begin() again is how "A: Reintentar" retries the bring-up.
bool  rd_begin(void);

// The one and only U8G2 instance. Use it for primitives this header does not
// wrap (drawXBM, drawDisc, drawLine, setDrawColor, ...). Do NOT call drawStr()
// on Spanish text and do NOT construct another U8G2 anywhere.
U8G2& rd_u8g2(void);

// Panel power. false -> setPowerSave(1); rd_end_frame() then skips sendBuffer()
// so a sleeping pet costs no I2C traffic at all.
void  rd_power(bool on);

// 0..255. Config.brightness maps straight onto this.
void  rd_set_contrast(uint8_t contrast);

// =============================================================================
//  FRAME SCHEDULER
//  Canonical loop body:
//      if (rd_begin_frame()) { ...draw...; rd_end_frame(); }
//  rd_begin_frame() clears the buffer and returns true only when the frame is
//  actually due; otherwise it returns false immediately and NOTHING must be
//  drawn (the buffer still holds the previous frame). It never blocks.
// =============================================================================
bool     rd_begin_frame(void);
void     rd_end_frame(void);

// Requested frame rate, clamped to 1..60. FPS_NORMAL (20) / FPS_LOW (4).
// Takes effect immediately.
void     rd_set_fps(uint8_t fps);

// The rate actually being used right now (the request, capped at FPS_LOW while
// a web client is active).
uint8_t  rd_fps(void);

// Draw the next frame as soon as loop() comes round again - call it on any
// input so a button press never waits out a 250 ms frame period.
void     rd_request_frame(void);

// The entry point calls this whenever the WebServer served a request. For the
// next RENDER_WEB_BUSY_MS the frame rate is capped at FPS_LOW so that
// sendBuffer() (~24 ms at 400 kHz) stops competing with handleClient().
void     rd_note_web_activity(void);
bool     rd_web_busy(void);

// A LIVE ANIMATION OUTRANKS THE WEB CAP, over a bounded window.
//
// rd_set_fps() only moves the REQUEST. rd_fps() then caps that request at
// FPS_LOW for RENDER_WEB_BUSY_MS after every request the WebServer served - and
// the phone's own page polls /state every 700 ms, so the cap is effectively
// permanent for as long as anybody has the page open. The result was that a
// choreography launched from the DEVICE'S OWN BUTTONS ran at 4 fps whenever a
// phone was watching, and one launched from the web ran at 4 fps ALWAYS,
// because the very request that starts the film is itself web activity: a
// 2600 ms meal became ten frames instead of fifty-two, and every phase shorter
// than one 250 ms frame period was never drawn at all.
//
// So: a module that is animating says so, and this floor beats the web cap
// exactly as the flash / shake floor does (RD_FX_MIN_FPS, see rd_fps()). It is
// a floor with a DEADLINE and not a mode - the caller renews it every tick for
// as long as its film lasts, and it lifts itself ms after the last renewal even
// if the caller crashes, forgets, or is deleted. Same self-extinguishing
// contract as rd_shake(): fps is clamped to 1..60, ms to RD_FPS_HOLD_MAX_MS,
// and fps == 0 or ms == 0 cancels on the spot.
//
// It lives HERE, in render, and not in ui.cpp, because ui.cpp can only reach
// rd_set_fps() - which is the value the cap is applied TO, so raising it there
// changes nothing. render never includes a module above it (see the layering
// note at the top of this header), so the animation layer pushes this DOWN
// rather than render reaching up to ask who is animating.
void     rd_hold_fps(uint8_t fps, uint16_t ms);

// Wall time of the last completed frame, including sendBuffer(). Compare it
// against FRAME_BUDGET_US on real hardware.
uint32_t rd_frame_time_us(void);

// =============================================================================
//  TEXT  (UTF-8 only - every one of these routes through drawUTF8)
//  y is the BASELINE, not the top of the glyph.
//  All of them set the font as a side effect and return the advance width in
//  pixels (0 when nothing was drawn).
//  x may be negative: whole leading codepoints are dropped and x advanced, so
//  marquees clip correctly on the left instead of wrapping to x=65500.
// =============================================================================
uint16_t rd_text_width(const uint8_t* font, const char* s);
uint16_t rd_text(int16_t x, int16_t y, const uint8_t* font, const char* s);

// Centred on the 128 px screen.
uint16_t rd_text_center(int16_t y, const uint8_t* font, const char* s);

// Centred inside the box [x, x+w).
uint16_t rd_text_center_in(int16_t x, int16_t w, int16_t y, const uint8_t* font, const char* s);

// Right-aligned: the last pixel of the string lands on x_right.
uint16_t rd_text_right(int16_t x_right, int16_t y, const uint8_t* font, const char* s);

// Left-aligned, truncated on a codepoint boundary so it never exceeds max_w.
uint16_t rd_text_fit(int16_t x, int16_t y, int16_t max_w, const uint8_t* font, const char* s);

// Word-wrapped block, breaking on spaces, hard-breaking words longer than w.
// Returns the number of lines drawn (<= max_lines).
uint8_t  rd_text_wrap(int16_t x, int16_t y, int16_t w, uint8_t line_h,
                      uint8_t max_lines, const uint8_t* font, const char* s);

// =============================================================================
//  SHADING / WIDGETS
// =============================================================================

// level is 0..RD_DITHER_MAX (n/16 of the pixels). Uses the current draw colour.
// Writes straight into the framebuffer: a full-screen fill costs 1024 byte ops.
// Exactly rd_dither_rect_phase(..., 0).
void rd_dither_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t level);

// Ordered dither with a Bayer phase offset, for per-pet coat patterns.
// phase shifts the matrix so two pets with the same level look different:
// its low 4 bits are (dy << 2) | dx, sliding the 4x4 matrix dx columns and dy
// rows. Only those 4 bits matter, so there are 16 distinct phases and phase 16
// is phase 0 again - feed it something stable per pet (a genome nibble), not a
// free-running counter, or the coat will crawl between frames.
// Same cost, same clipping and same draw-colour semantics as rd_dither_rect;
// the phase does not push it off the fast path.
//
// HOW MANY LOOKS YOU ACTUALLY GET, measured over all 16 phases (host test):
//     level  1  3  5  7  9 11 13 15 -> 16 distinct patterns
//     level  2  6 10 14             ->  8
//     level  4 12                   ->  4
//     level  8 (RD_D50)             ->  2
//     level 16                      ->  1 (solid)
// An even level selects a self-similar sub-pattern of the Bayer matrix, and
// "< 8" is exactly the 2x2 checkerboard, which no 4x4 shift can vary. So for
// per-pet coats pick an ODD level (3, 5, 7, 9, 11, 13) and never RD_D50: at
// RD_D50 the phase buys you two looks for the whole species.
void rd_dither_rect_phase(int16_t x, int16_t y, int16_t w, int16_t h,
                          uint8_t level, uint8_t phase);

// XOR a rectangle - menu selection highlight, god-mode status bar.
void rd_invert_rect(int16_t x, int16_t y, int16_t w, int16_t h);

// Stat bar: 1 px frame + fill. Designed to stay legible at h = 5 (3 px of
// fill). pct 0..100. In RD_BAR_AUTO a value at or below RD_BAR_LOW_PCT is
// hatched at 50 % instead of solid, with a solid 1 px leading edge, so "nearly
// empty" is unmistakable even at 3 px tall and even at pct = 1.
void rd_bar(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t pct, uint8_t style = RD_BAR_AUTO);

// The persistent bottom strip: what L and R do on this screen.
//     <| SIG. ..... SEL |>
// Draws a left-pointing marker + left label, a right label + right-pointing
// marker, and a dotted divider in the gap between them. Pass NULL or "" for a
// side that does nothing and neither its marker nor its label is drawn.
// Labels are truncated on a codepoint boundary if they do not fit.
// Costs exactly AFFORDANCE_BAR_H (8) rows: 56..63. Does not clear behind
// itself - the sprite area contractually ends at row 55.
void rd_affordance(const char* left, const char* right);

// Which buttons are PHYSICALLY down right now: bit 0 = left, bit 1 = right.
// Pushed in by the UI once per frame (render never polls input, exactly as the
// web-busy fps cap is pushed in and not pulled). This call is also the frame
// edge for the echo below: it forgets which strip halves the previous frame
// drew.
void rd_affordance_pressed(uint8_t mask);

// Inverts the half of the strip belonging to a pressed button, and ONLY when
// some rd_affordance() call in THIS frame actually gave that half a label - so
// a side that does nothing never lights up. The inversion is an XOR, so it must
// happen exactly once: call this once per frame, after the base screen and any
// modal have both drawn. Calling it a second time in the same frame is a no-op,
// not a double inversion.
void rd_affordance_echo(void);

// =============================================================================
//  PANEL REGISTER EFFECTS
//
//  The scarce resource in this firmware is the I2C bus, not the CPU: one
//  sendBuffer() is 1064 bytes / ~24 ms of the 50 ms frame, while ~4 M CPU
//  cycles go idle. The controller can change HOW it presents the framebuffer
//  without the framebuffer being touched or resent, and each of those changes
//  is one 1-2 byte command - a 3-4 byte I2C transaction once the address and
//  control byte are counted, against sendBuffer's ~1064. Call it 250x cheaper.
//  That is what everything below buys: motion and light for no drawing time.
//
//  Contract, identical for all of them:
//   - They are applied inside rd_end_frame(), immediately before the
//     sendBuffer() that was already there, and a register is written ONLY when
//     its value differs from the previous frame. A still screen costs 0 bytes.
//   - Nothing is sent while the panel is asleep or absent, exactly like
//     sendBuffer() itself. The controller keeps its registers across power
//     save, so the effects re-synchronise on the first frame after
//     rd_power(true) with no visible glitch.
//   - Every effect expires on its own and restores its register. A stuck 0xA7
//     or 0xD3 leaves the device looking permanently broken, so rd_fx_reset()
//     cancels the lot: a crash can never inherit an inverted or shifted panel. rd_power(false) settles the
//     two transients (flash, shake) before the panel goes dark but deliberately
//     leaves the contrast base, a ramp and the breathing alone - those are
//     ambient properties of the pet, not of the screen that was up, and they
//     survive a sleep.
//
//  CONTRAST OWNERSHIP - who wins:
//     rd_set_contrast()  sets the BASE. It is the user's brightness from
//                        apply_config(), so it wins outright: it applies at
//                        once and cancels any ramp in flight.
//     rd_contrast_ramp() MOVES the base, over time, to a new resting value.
//     rd_breathe()       MODULATES around whatever the base currently is and
//                        never changes it.
//  Effective contrast = clamp_0_255(base_now + breathe_offset).
// =============================================================================

// Clamps, so that no caller can leave the panel looking broken.
#define RD_FX_FLASH_MAX_MS      3000    // a "flash" is not a mode; 0xA7 is capped
#define RD_FX_SHAKE_MAX_PX        16    // 0xD3 is 6 bits; beyond this it is nonsense
#define RD_FX_SHAKE_MAX_MS      3000    // a shake is an impact, not a display mode
// Frame-rate floor held while a flash or a shake is live. Both are applied AND
// cleared inside rd_end_frame(), so their real resolution is the frame period.
// The firmware drops to FPS_LOW (4 fps = 250 ms/frame) routinely - asleep,
// energy < 15 %, or any web client inside RENDER_WEB_BUSY_MS - and there a
// 40 ms flash would be held a whole frame and a decaying shake would render as
// one or two steps. Both effects are duration-capped, so this floor costs at
// most a few extra sendBuffer() calls over a bounded window.
#define RD_FX_MIN_FPS             20
// Longest a single rd_hold_fps() call can pin the frame rate. A choreography is
// 2.6 s at worst and renews the hold every tick, so this only ever bounds the
// tail after the LAST renewal; it is capped for rd_shake()'s reason, so that no
// caller can pin the panel at 20 fps with web_service() starving behind it.
#define RD_FPS_HOLD_MAX_MS      3000
#define RD_FX_BREATHE_MAX_AMP     64    // more than this is a strobe, not breathing
#define RD_FX_BREATHE_MIN_MS     100    // below one frame period it just flickers

// Full-screen invert for ms milliseconds (SSD1306 0xA7 / 0xA6). One command
// byte each way, no repaint: the ideal "you hit something" acknowledgement.
// ms == 0 cancels a flash already running. ms is clamped to RD_FX_FLASH_MAX_MS.
// Requests a frame so the flash starts now even at FPS_LOW.
void rd_flash(uint16_t ms);

// Vertical screen shake of +/- amp_px, decaying linearly over ms (SSD1306
// 0xD3). amp_px is clamped to RD_FX_SHAKE_MAX_PX; amp_px == 0 or ms == 0
// cancels. Requests a frame, like rd_flash().
//
// HONEST WARNING: 0xD3 WRAPS. It re-maps COM rows modulo 64, it does not
// scroll a viewport, so a +/-3 px shake makes 3 rows from the bottom of the
// screen reappear at the top. Over ~150 ms of decaying jitter the eye reads
// that as a shake and never resolves the wrapped rows, which is exactly why
// this is usable at all. It is NOT usable for slide transitions, for holding
// a static offset, or for anything slow enough to be looked at: there the
// wrapped band is plainly a bug. Keep ms short and amp_px small.
void rd_shake(uint8_t amp_px, uint16_t ms);

// Ramp the panel contrast to target over ms. ms == 0 applies immediately.
// Moves the BASE (see the ownership note above), so when the ramp finishes the
// base is target and stays there. Retargeting mid-ramp starts from the value
// currently displayed, so it never snaps. rd_set_contrast() cancels it.
void rd_contrast_ramp(uint8_t target, uint16_t ms);

// Ambient contrast breathing around the current base. amp == 0 disables it.
// Interpolated sine, so it survives a slow frame rate without stepping.
// amp is clamped to RD_FX_BREATHE_MAX_AMP and period_ms raised to
// RD_FX_BREATHE_MIN_MS. Does not change the base: rd_set_contrast() and
// rd_contrast_ramp() keep working underneath it.
void rd_breathe(uint8_t amp, uint16_t period_ms);

// Cancel every register effect and put the panel back to its resting state:
// no invert, no offset, no breathing, no ramp (a ramp in flight snaps to its
// target, which is the resting level its caller asked for). Writes only the
// registers that are actually dirty, and only if the panel is on and present -
// with the panel asleep the software state is still cleared and the registers
// are corrected on the first frame after it wakes.
// This one DOES cancel breathing, unlike a sleep: it is the "unstick the
// panel" button, so after calling it, re-arm whatever ambience you wanted.
void rd_fx_reset(void);

// =============================================================================
//  BOOT / FATAL
// =============================================================================

// Boot logo: procedural egg + FW_NAME + FW_VERSION. Draws AND sends, then
// resets the frame scheduler. Safe to call before the pet exists.
void rd_splash(void);

#endif // NT_RENDER_H
