// =============================================================================
//  NOTTAMAGOCHI - ui/render.cpp
//  The single U8G2 instance, the frame scheduler, and every drawing primitive.
//  No network headers here, ever (layering rule, BRIEF section 4).
// =============================================================================
#include "render.h"

#include <Wire.h>
#include <stdio.h>
#include <string.h>

// -----------------------------------------------------------------------------
// THE display object. BRIEF risk #2 - the single most dangerous line in the
// project. Argument order is (rotation, reset, clock = SCL, data = SDA);
// U8x8lib.cpp:1345-1353 then calls Wire.begin(data, clock) in the ESP32 order.
// Dropping the last two arguments silently moves I2C onto GPIO8/GPIO9, which
// are LED_BUILTIN and the BOOT strapping pin.
// File-scope static: nothing outside this file can name it, and there can be
// no second instance fighting over the shared 1024 B framebuffer.
// -----------------------------------------------------------------------------
static RdDisplay s_u8g2(U8G2_R0, /*reset=*/U8X8_PIN_NONE, /*clock=SCL*/PIN_SCL, /*data=SDA*/PIN_SDA);

// -----------------------------------------------------------------------------
// Module state
// -----------------------------------------------------------------------------
static bool     s_display_ok      = false;
static bool     s_display_on      = true;
static bool     s_in_frame        = false;
static uint8_t  s_target_fps      = FPS_NORMAL;
static uint8_t  s_i2c_addr7       = OLED_I2C_ADDR_7BIT;
static uint32_t s_next_frame_ms   = 0;
static uint32_t s_frame_start_us  = 0;
static uint32_t s_last_frame_us   = 0;
static uint32_t s_web_busy_until  = 0;
static bool     s_web_busy_armed  = false;   // millis()==0 rollover safety

// Scratch used by the text helpers. Single-threaded: everything that draws runs
// on the Arduino loop task.
#define RD_SCRATCH  96   // rd_text_fit
#define RD_LINE_BUF 64   // one wrapped line: 26 chars of 5x8 across 128 px
static char s_scratch[RD_SCRATCH];

// 4x4 ordered Bayer matrix. Pixel is on when BAYER[(y&3)*4 + (x&3)] < level.
static const uint8_t RD_BAYER[16] = {
   0,  8,  2, 10,
  12,  4, 14,  6,
   3, 11,  1,  9,
  15,  7, 13,  5
};

// -----------------------------------------------------------------------------
// PANEL REGISTER EFFECTS - state.
//
// Two invariants hold this together and everything else follows from them:
//   1. s_contrast_cur / s_invert_cur / s_offset_cur are what the CONTROLLER is
//      holding, not what we would like it to hold. They are updated only where
//      a byte actually goes out on the wire. That is what makes "write only on
//      change" safe, and what makes the state self-heal after a power save:
//      the controller keeps its registers through 0xAE, so on wake the tracked
//      value is still true and only a genuine difference is resent.
//   2. Every effect has a deadline and decays to its resting value on its own.
//      Nothing here can stay latched because a caller forgot to clear it.
// -----------------------------------------------------------------------------
#define RD_FX_SHAKE_STEP_MS   45u   // just under one 20 fps frame, so consecutive
                                    // frames always land on different wave phases
#define RD_FX_OFFSET_MOD      64u   // 0xD3 is modulo the 64 COM lines

// Contrast: base (resting) + ramp (moves the base) + breathe (modulates it).
static uint8_t  s_contrast_base   = OLED_CONTRAST_DEFAULT;
static uint8_t  s_contrast_cur    = OLED_CONTRAST_DEFAULT;
static uint8_t  s_ramp_from       = OLED_CONTRAST_DEFAULT;
static uint8_t  s_ramp_to         = OLED_CONTRAST_DEFAULT;
static uint32_t s_ramp_t0         = 0;
static uint16_t s_ramp_ms         = 0;
static bool     s_ramp_active     = false;
static uint8_t  s_breathe_amp     = 0;      // 0 = off
static uint16_t s_breathe_period  = 0;

// Flash: 0xA6 / 0xA7.
static uint32_t s_flash_until     = 0;
static bool     s_flash_active    = false;
static bool     s_invert_cur      = false;

// Frame-rate floor asked for by an animating module (rd_hold_fps). Not a panel
// register and not a transient effect: it changes nothing about what is drawn,
// only how often, so it is deliberately NOT cleared by fx_settle_transients().
static uint32_t s_hold_fps_until  = 0;
static uint8_t  s_hold_fps_val    = 0;
static bool     s_hold_fps_armed  = false;

// Shake: 0xD3.
static uint32_t s_shake_t0        = 0;
static uint16_t s_shake_ms        = 0;
static uint8_t  s_shake_amp       = 0;
static bool     s_shake_active    = false;
static uint8_t  s_offset_cur      = 0;

// One sine period in 16 samples, +/-64. Linearly interpolated to 256 sub-steps
// by fx_tick_contrast() so the breathing does not visibly step between samples.
// Sine rather than triangle: a triangle has a corner at each extreme, and the
// eye is far more sensitive to the discontinuity in the rate of change than to
// the brightness itself. Costs 16 bytes of flash and no floating point.
static const int8_t RD_SIN16[16] = {
    0,  24,  45,  59,  64,  59,  45,  24,
    0, -24, -45, -59, -64, -59, -45, -24
};

// =============================================================================
//  Small internal helpers
// =============================================================================

// Length in bytes of the UTF-8 sequence starting with byte c. A stray
// continuation byte counts as 1 so a malformed string can never loop forever.
static inline uint8_t u8_seq_len(uint8_t c) {
  if (c < 0x80)          return 1;
  if ((c & 0xE0) == 0xC0) return 2;
  if ((c & 0xF0) == 0xE0) return 3;
  if ((c & 0xF8) == 0xF0) return 4;
  return 1;
}

// Clip a rectangle to the panel. Returns false when nothing is left.
static bool clip_rect(int16_t& x, int16_t& y, int16_t& w, int16_t& h) {
  if (w <= 0 || h <= 0) return false;
  if (x < 0) { w = (int16_t)(w + x); x = 0; }
  if (y < 0) { h = (int16_t)(h + y); y = 0; }
  if (w <= 0 || h <= 0) return false;
  if (x >= OLED_W || y >= OLED_H) return false;
  if (x + w > OLED_W) w = (int16_t)(OLED_W - x);
  if (y + h > OLED_H) h = (int16_t)(OLED_H - y);
  return (w > 0 && h > 0);
}

// Draw a UTF-8 string at a signed x. Negative x drops whole leading codepoints
// (u8g2 coordinates are unsigned: a negative x would otherwise wrap to ~65500
// and clip the whole string away). The font must already be set.
static uint16_t draw_utf8(int16_t x, int16_t y, const char* s) {
  if (s == NULL || *s == '\0') return 0;
  if (y < 0 || y >= (int16_t)(OLED_H + 16)) return 0;
  while (x < 0 && *s != '\0') {
    char gl[5];
    const uint8_t n = u8_seq_len((uint8_t)*s);
    uint8_t i = 0;
    while (i < n && s[i] != '\0') { gl[i] = s[i]; i++; }
    gl[i] = '\0';
    if (i == 0) break;                       // cannot happen, but never spin
    int16_t gw = (int16_t)s_u8g2.getUTF8Width(gl);
    if (gw <= 0) gw = 1;                     // no glyph in this font: still advance
    x = (int16_t)(x + gw);
    s += i;
  }
  if (*s == '\0' || x >= (int16_t)OLED_W) return 0;
  return s_u8g2.drawUTF8((u8g2_uint_t)x, (u8g2_uint_t)y, s);
}

// Copy s into dst, dropping whole trailing codepoints until it measures no
// more than max_w. The font must already be set. Returns the measured width.
// dst and s may alias (rd_text_wrap trims a word in place).
static uint16_t fit_copy(char* dst, size_t dst_sz, const char* s, int16_t max_w) {
  size_t len = 0;
  if (s != NULL) {
    while (s[len] != '\0' && len + 1 < dst_sz) len++;
    // if the copy was cut short, back off to a codepoint boundary
    if (s[len] != '\0') {
      while (len > 0 && ((uint8_t)s[len] & 0xC0) == 0x80) len--;
    }
    if (dst != s) memmove(dst, s, len);
  }
  dst[len] = '\0';
  if (max_w <= 0) { dst[0] = '\0'; return 0; }
  uint16_t w = s_u8g2.getUTF8Width(dst);
  while (w > (uint16_t)max_w && len > 0) {
    // step back to the start of the last codepoint
    size_t cut = len - 1;
    while (cut > 0 && ((uint8_t)dst[cut] & 0xC0) == 0x80) cut--;
    len = cut;
    dst[len] = '\0';
    w = s_u8g2.getUTF8Width(dst);
  }
  return w;
}

// =============================================================================
//  PANEL REGISTER EFFECTS - internals
//
//  Every deadline test uses the project's rollover-safe (int32_t)(now - t) >= 0
//  form. The one place that cannot be made rollover-proof is the breathing
//  phase, which is now % period: 2^32 is not a whole number of periods, so the
//  breath skips phase once, every 49.7 days. That is a deliberate trade - the
//  alternative is carrying a phase accumulator that has to be ticked even on
//  frames nobody asked for.
// =============================================================================

// The resting contrast right now: the base, or the in-flight ramp's
// interpolated value. Pure - it does not retire a finished ramp, so the public
// entry points can call it to find out where a ramp is without side effects.
static uint8_t fx_base_at(uint32_t now) {
  if (!s_ramp_active)  return s_contrast_base;
  if (s_ramp_ms == 0)  return s_ramp_to;
  const uint32_t el = now - s_ramp_t0;         // unsigned: correct across rollover
  if (el >= (uint32_t)s_ramp_ms) return s_ramp_to;
  const int32_t d = (int32_t)s_ramp_to - (int32_t)s_ramp_from;
  return (uint8_t)((int32_t)s_ramp_from + (d * (int32_t)el) / (int32_t)s_ramp_ms);
}

// Contrast the panel should show this frame, and the one place a finished ramp
// is retired into the base. Integer only, like every other stat path here.
static uint8_t fx_tick_contrast(uint32_t now) {
  int16_t v = (int16_t)fx_base_at(now);

  if (s_ramp_active && (int32_t)(now - (s_ramp_t0 + s_ramp_ms)) >= 0) {
    s_contrast_base = s_ramp_to;   // from here on the base owns the value
    s_ramp_active   = false;
  }

  if (s_breathe_amp != 0 && s_breathe_period != 0) {
    // q is the phase in 0..255. t < period <= 65535, so t * 256 stays well
    // inside uint32 and no 64-bit helper gets linked in.
    const uint32_t t  = now % (uint32_t)s_breathe_period;
    const uint32_t q  = (t * 256u) / (uint32_t)s_breathe_period;
    const uint8_t  i  = (uint8_t)(q >> 4);
    const int16_t  fr = (int16_t)(q & 15u);
    const int16_t  a  = (int16_t)RD_SIN16[i];
    const int16_t  b  = (int16_t)RD_SIN16[(i + 1u) & 15u];
    const int16_t  s  = (int16_t)(a + (((b - a) * fr) / 16));       // -64..64
    v = (int16_t)(v + (((int16_t)s_breathe_amp * s) / 64));
  }

  if (v < 0)   return 0;
  if (v > 255) return 255;
  return (uint8_t)v;
}

// 0xD3 value for this instant, already folded into the controller's 0..63
// range, and the one place a finished shake is retired. 0 == resting.
static uint8_t fx_tick_offset(uint32_t now) {
  if (!s_shake_active) return 0;
  if (s_shake_ms == 0 || (int32_t)(now - (s_shake_t0 + s_shake_ms)) >= 0) {
    s_shake_active = false;
    return 0;
  }
  // el < s_shake_ms <= 65535 is guaranteed by the deadline test above, so the
  // truncation to 16 bits is safe and the maths stays off the 32-bit divider.
  const uint16_t el   = (uint16_t)(now - s_shake_t0);
  const uint16_t left = (uint16_t)(s_shake_ms - el);
  const int16_t  amp  = (int16_t)(((uint32_t)s_shake_amp * left) / s_shake_ms);
  if (amp <= 0) return 0;

  // Four-phase wave rather than a square one: at 20 fps a square wave whose
  // half-period lands near the frame period aliases into a CONSTANT offset,
  // which looks like the screen simply moved. The 0 rungs break that.
  static const int8_t k_wave[4] = { 1, 0, -1, 0 };
  const int16_t v = (int16_t)(amp * (int16_t)k_wave[(el / RD_FX_SHAKE_STEP_MS) & 3u]);
  return (uint8_t)(((uint16_t)((int16_t)RD_FX_OFFSET_MOD + v)) & (RD_FX_OFFSET_MOD - 1u));
}

// Push the register effects to the panel. Called from exactly one place -
// rd_end_frame(), under the same s_display_on && s_display_ok guard as
// sendBuffer() - and it writes a register only when its value changed, so an
// idle screen adds nothing at all to the 1064 bytes of a frame.
// Byte counts below are the command payload; each is its own I2C transaction,
// so add the address and the 0x00 control byte for the real wire cost.
static void fx_apply(uint32_t now) {
  const uint8_t contrast = fx_tick_contrast(now);
  if (contrast != s_contrast_cur) {
    s_u8g2.setContrast(contrast);              // 0x81 + value: 2 bytes
    s_contrast_cur = contrast;
  }

  bool invert = false;
  if (s_flash_active) {
    if ((int32_t)(now - s_flash_until) >= 0) s_flash_active = false;
    else                                     invert = true;
  }
  if (invert != s_invert_cur) {
    s_u8g2.sendF("c", invert ? 0xA7 : 0xA6);   // 1 byte
    s_invert_cur = invert;
  }

  const uint8_t offset = fx_tick_offset(now);
  if (offset != s_offset_cur) {
    s_u8g2.sendF("ca", 0xD3, (int)offset);     // 2 bytes
    s_offset_cur = offset;
  }
}

// Put back the two registers that can make the panel look BROKEN - invert and
// offset - and deliberately leave the contrast state alone.
//
// This is what rd_power(false) wants, and rd_fx_reset() is not: a flash or a
// shake caught mid-flight by the screen going dark must not survive the sleep,
// but the base contrast, a ramp and the breathing are ambient properties of
// the pet, not of this screen, and silently losing them across every blank
// would be a nasty little bug for the caller to find.
// Same guard as everywhere else: with the panel asleep or absent the software
// state is still cleared, the trackers keep telling the truth about the
// controller, and fx_apply() fixes the registers on the first frame after wake.
static void fx_settle_transients(void) {
  s_flash_active = false;
  s_shake_active = false;
  s_shake_amp    = 0;
  if (!(s_display_on && s_display_ok)) return;
  if (s_invert_cur)      { s_u8g2.sendF("c", 0xA6);     s_invert_cur = false; }
  if (s_offset_cur != 0) { s_u8g2.sendF("ca", 0xD3, 0); s_offset_cur = 0; }
}

// =============================================================================
//  LIFECYCLE
// =============================================================================

static uint8_t i2c_probe(uint8_t addr7) {
  Wire.beginTransmission(addr7);
  return Wire.endTransmission();   // 0 == the device acknowledged
}

bool rd_begin(void) {
  // Route B from U8G2_API section 2: bring Wire up ourselves so the bus can be
  // scanned BEFORE u8g2.begin(). U8g2's own Wire.begin() then early-returns
  // ("Bus already started in Master Mode", Wire.cpp:295-299) and our pins and
  // clock survive untouched.
  const bool wire_ok = Wire.begin((int)PIN_SDA, (int)PIN_SCL, (uint32_t)OLED_BUS_CLOCK_HZ);
  if (!wire_ok) Serial.println(F("[rd] Wire.begin() FAILED"));

  // --- boot-time I2C scan (BRIEF risk #2) ----------------------------------
  uint8_t found = 0;
  if      (i2c_probe(OLED_I2C_ADDR_7BIT) == 0) found = OLED_I2C_ADDR_7BIT;
  else if (i2c_probe(0x3D)               == 0) found = 0x3D;

  if (found != 0) {
    s_i2c_addr7  = found;
    s_display_ok = true;
    Serial.printf("[rd] I2C SDA=%d SCL=%d @%luHz - OLED found at 0x%02X\r\n",
                  (int)PIN_SDA, (int)PIN_SCL, (unsigned long)OLED_BUS_CLOCK_HZ, found);
  } else {
    // Nothing at either OLED address. Sweep the whole bus and report every
    // device so a miswired panel is diagnosable without a logic analyser.
    s_display_ok = false;
    Serial.printf("[rd] NO OLED at 0x3C or 0x3D on SDA=%d SCL=%d. Full bus scan:\r\n",
                  (int)PIN_SDA, (int)PIN_SCL);
    uint8_t n = 0;
    for (uint8_t a = 0x08; a <= 0x77; a++) {
      if (i2c_probe(a) == 0) {
        Serial.printf("[rd]   ACK at 0x%02X\r\n", a);
        n++;
      }
    }
    if (n == 0) {
      Serial.println(F("[rd]   bus is silent: check wiring, 3V3 and GND."));
    }
    // Carry on at the default address anyway: a panel that NAKs the probe but
    // works for writes is a real (if rare) clone behaviour, and drawing into
    // the buffer is harmless in any case.
    s_i2c_addr7 = OLED_I2C_ADDR_7BIT;
  }

  s_u8g2.setBusClock(OLED_BUS_CLOCK_HZ);       // must precede begin() to be the default
  s_u8g2.setI2CAddress((uint8_t)(s_i2c_addr7 << 1));   // u8g2 wants the 8-bit form
  s_u8g2.begin();                              // does clearDisplay() + setPowerSave(0)

  s_u8g2.enableUTF8Print();                    // BRIEF 1.5 / risk #5
  s_u8g2.setFont(RD_FONT_BODY);
  s_u8g2.setFontMode(0);                       // solid; literal 0, the U8G2_FONT_MODE_* names do not exist
  s_u8g2.setBitmapMode(0);                     // solid: sprites erase their own background
  s_u8g2.setDrawColor(1);
  s_u8g2.setContrast(OLED_CONTRAST_DEFAULT);
  s_u8g2.clearBuffer();

  s_display_on     = true;
  s_in_frame       = false;
  s_target_fps     = FPS_NORMAL;
  s_next_frame_ms  = millis();
  s_last_frame_us  = 0;
  s_web_busy_armed = false;

  // Register-effect state must match what begin() just left in the controller.
  // The SSD1306/SH1106 init sequence ends with 0xD3 0x00 (no offset) and 0xA6
  // (not inverted), and we sent the default contrast three lines up, so the
  // three trackers below are true statements about the panel, not wishes.
  s_contrast_base  = OLED_CONTRAST_DEFAULT;
  s_contrast_cur   = OLED_CONTRAST_DEFAULT;
  s_ramp_active    = false;
  s_breathe_amp    = 0;
  s_breathe_period = 0;
  s_flash_active   = false;
  s_invert_cur     = false;
  s_shake_active   = false;
  s_shake_amp      = 0;
  s_offset_cur     = 0;
  s_hold_fps_armed = false;

  return s_display_ok;
}

U8G2& rd_u8g2(void) { return s_u8g2; }

void rd_power(bool on) {
  if (on == s_display_on) return;
  // Settle the transients while the panel is still awake and can hear us: a
  // pet that falls asleep mid-flash would otherwise keep 0xA7 latched until
  // the first frame after it wakes. NOT rd_fx_reset() - going dark must not
  // silently cancel the pet's ambient breathing. A no-op when nothing is
  // dirty, which is the common case.
  if (!on) fx_settle_transients();
  s_display_on = on;
  s_u8g2.setPowerSave(on ? 0 : 1);
  if (on) s_next_frame_ms = millis();   // repaint immediately on wake
}

void rd_set_contrast(uint8_t contrast) {
  // This is the user's brightness from apply_config(), so it owns the BASE and
  // wins outright: an explicit setting must not be dragged around by an
  // animation, hence the ramp is cancelled. Breathing is deliberately NOT
  // cancelled - it is ambient, and simply starts modulating the new base.
  // Applied straight away (unchanged from before this module grew effects) so
  // the slider in the web UI still responds on the frame the user let go of it.
  s_contrast_base = contrast;
  s_ramp_active   = false;
  s_u8g2.setContrast(contrast);
  s_contrast_cur  = contrast;
}

// =============================================================================
//  FRAME SCHEDULER
// =============================================================================

void rd_note_web_activity(void) {
  s_web_busy_until = millis() + RENDER_WEB_BUSY_MS;
  s_web_busy_armed = true;
}

bool rd_web_busy(void) {
  if (!s_web_busy_armed) return false;
  if ((int32_t)(millis() - s_web_busy_until) >= 0) {
    s_web_busy_armed = false;
    return false;
  }
  return true;
}

// Same shape as rd_web_busy(): the deadline is tested on the way past, so the
// hold expires the first time anybody asks and needs nobody to clear it.
static bool rd_fps_hold_live(void) {
  if (!s_hold_fps_armed) return false;
  if ((int32_t)(millis() - s_hold_fps_until) >= 0) {
    s_hold_fps_armed = false;
    return false;
  }
  return true;
}

void rd_hold_fps(uint8_t fps, uint16_t ms) {
  if (fps == 0u || ms == 0u) { s_hold_fps_armed = false; return; }
  if (fps > 60u) fps = 60u;
  if (ms > RD_FPS_HOLD_MAX_MS) ms = (uint16_t)RD_FPS_HOLD_MAX_MS;
  s_hold_fps_val   = fps;
  s_hold_fps_until = millis() + ms;
  s_hold_fps_armed = true;
  // Without this the raise would not take effect until the CURRENT period
  // elapsed - i.e. up to 250 ms of the film would still play at 4 fps, which is
  // most of a phase.
  rd_request_frame();
}

uint8_t rd_fps(void) {
  uint8_t fps = s_target_fps;
  if (rd_web_busy() && fps > FPS_LOW) fps = FPS_LOW;   // BRIEF risk #7
  // A film that is on the panel RIGHT NOW outranks that cap, for the bounded
  // window rd_hold_fps() was last renewed for. See the header: the web cap is
  // armed by the very request that starts a web-launched choreography, and by
  // the 700 ms /state poll of any phone that merely has the page open, so
  // without this line the feature the player asked for ("if I feed it, I have
  // to see it eat") plays at ten frames.
  if (rd_fps_hold_live() && fps < s_hold_fps_val) fps = s_hold_fps_val;
  // A live transient outranks every cap above, the web one included. See
  // RD_FX_MIN_FPS in render.h: a flash or a shake is measured in tens of
  // milliseconds but can only be applied AND cleared by rd_end_frame(), so its
  // real resolution is the frame period. At FPS_LOW a 40 ms flash would be held
  // for a whole 250 ms frame and a decaying shake would collapse into one or
  // two steps. Both effects are duration-capped (RD_FX_FLASH_MAX_MS /
  // RD_FX_SHAKE_MAX_MS), so this floor is held over a bounded window.
  if ((s_flash_active || s_shake_active) && fps < RD_FX_MIN_FPS) fps = RD_FX_MIN_FPS;
  if (fps < 1) fps = 1;
  return fps;
}

void rd_set_fps(uint8_t fps) {
  if (fps < 1)  fps = 1;
  if (fps > 60) fps = 60;
  if (fps == s_target_fps) return;
  s_target_fps = fps;
  s_next_frame_ms = millis();   // a rate change takes effect on the next loop()
}

void rd_request_frame(void) {
  s_next_frame_ms = millis();
}

bool rd_begin_frame(void) {
  if (s_in_frame) return true;   // defensive: a caller that forgot rd_end_frame()

  const uint32_t now    = millis();
  const uint32_t period = (uint32_t)(1000u / rd_fps());

  if ((int32_t)(now - s_next_frame_ms) < 0) return false;   // not due yet - skip, never block

  // Advance the deadline by exactly one period so the cadence does not drift,
  // but resynchronise after a long stall instead of firing a burst of catch-up
  // frames (a 2000-step offline catch-up can eat several seconds).
  s_next_frame_ms += period;
  if ((int32_t)(now - s_next_frame_ms) > (int32_t)period) s_next_frame_ms = now + period;

  s_in_frame      = true;
  s_frame_start_us = micros();
  s_u8g2.clearBuffer();
  return true;
}

void rd_end_frame(void) {
  if (!s_in_frame) return;
  s_in_frame = false;

  // A full sendBuffer() is ~1064 bytes * 9 bit-times = ~24 ms at 400 kHz.
  // Skip it entirely while the panel is asleep or absent.
  //
  // The register effects ride in front of it, under exactly the same guard.
  // They go first so that an invert or a shake lands at the START of the 24 ms
  // the frame spends streaming out, not after it; and because they are at most
  // 5 bytes they do not measurably move s_last_frame_us.
  if (s_display_on && s_display_ok) {
    fx_apply(millis());
    s_u8g2.sendBuffer();
  }

  s_last_frame_us = micros() - s_frame_start_us;

  // Overrun guard: if this frame blew the budget, push the next deadline out by
  // the overrun so the scheduler cannot run the loop at 100 % duty cycle.
  if (s_last_frame_us > FRAME_BUDGET_US) {
    uint32_t over_ms = (s_last_frame_us - FRAME_BUDGET_US) / 1000u;
    if (over_ms > 250u) over_ms = 250u;
    s_next_frame_ms += over_ms;
  }
}

uint32_t rd_frame_time_us(void) { return s_last_frame_us; }

// =============================================================================
//  TEXT
// =============================================================================

uint16_t rd_text_width(const uint8_t* font, const char* s) {
  if (s == NULL || *s == '\0') return 0;
  if (font != NULL) s_u8g2.setFont(font);
  return s_u8g2.getUTF8Width(s);
}

uint16_t rd_text(int16_t x, int16_t y, const uint8_t* font, const char* s) {
  if (s == NULL || *s == '\0') return 0;
  if (font != NULL) s_u8g2.setFont(font);
  return draw_utf8(x, y, s);
}

uint16_t rd_text_center(int16_t y, const uint8_t* font, const char* s) {
  return rd_text_center_in(0, OLED_W, y, font, s);
}

uint16_t rd_text_center_in(int16_t x, int16_t w, int16_t y, const uint8_t* font, const char* s) {
  if (s == NULL || *s == '\0') return 0;
  if (font != NULL) s_u8g2.setFont(font);
  int16_t sw = (int16_t)s_u8g2.getUTF8Width(s);
  return draw_utf8((int16_t)(x + ((w - sw) / 2)), y, s);
}

uint16_t rd_text_right(int16_t x_right, int16_t y, const uint8_t* font, const char* s) {
  if (s == NULL || *s == '\0') return 0;
  if (font != NULL) s_u8g2.setFont(font);
  int16_t sw = (int16_t)s_u8g2.getUTF8Width(s);
  return draw_utf8((int16_t)(x_right - sw + 1), y, s);
}

uint16_t rd_text_fit(int16_t x, int16_t y, int16_t max_w, const uint8_t* font, const char* s) {
  if (s == NULL || *s == '\0') return 0;
  if (font != NULL) s_u8g2.setFont(font);
  fit_copy(s_scratch, RD_SCRATCH, s, max_w);
  return draw_utf8(x, y, s_scratch);
}

uint8_t rd_text_wrap(int16_t x, int16_t y, int16_t w, uint8_t line_h,
                     uint8_t max_lines, const uint8_t* font, const char* s) {
  if (s == NULL || *s == '\0' || w <= 0 || max_lines == 0) return 0;
  if (font != NULL) s_u8g2.setFont(font);
  if (line_h == 0) line_h = RD_LINE_BODY;

  char    line[RD_LINE_BUF];
  char    cand[RD_LINE_BUF];
  size_t  line_len = 0;
  uint8_t drawn    = 0;
  int16_t cy       = y;

  line[0] = '\0';
  const char* p = s;

  while (*p != '\0' && drawn < max_lines) {
    while (*p == ' ') p++;              // collapse runs of spaces at a break
    if (*p == '\0') break;

    if (*p == '\n') {                   // forced line break, blank lines kept
      draw_utf8(x, cy, line);
      drawn++;
      cy = (int16_t)(cy + line_h);
      line[0] = '\0';
      line_len = 0;
      p++;
      continue;
    }

    // measure the next word
    const char* wstart = p;
    while (*p != '\0' && *p != ' ' && *p != '\n') p += u8_seq_len((uint8_t)*p);
    size_t wlen = (size_t)(p - wstart);
    if (wlen == 0) break;

    // candidate = line + (space) + word
    size_t need = line_len + (line_len ? 1u : 0u) + wlen;
    if (need < RD_LINE_BUF) {
      memcpy(cand, line, line_len);
      size_t cl = line_len;
      if (cl) cand[cl++] = ' ';
      memcpy(cand + cl, wstart, wlen);
      cl += wlen;
      cand[cl] = '\0';
      if ((int16_t)s_u8g2.getUTF8Width(cand) <= w) {
        memcpy(line, cand, cl + 1);
        line_len = cl;
        continue;
      }
    }

    // does not fit: flush what we have
    if (line_len > 0) {
      draw_utf8(x, cy, line);
      drawn++;
      cy = (int16_t)(cy + line_h);
      line[0] = '\0';
      line_len = 0;
      p = wstart;                        // retry this word on the fresh line
      continue;
    }

    // the word alone is wider than the box: hard-break it on a codepoint edge
    char part[RD_LINE_BUF];
    size_t take = (wlen < RD_LINE_BUF - 1) ? wlen : (RD_LINE_BUF - 1);
    memcpy(part, wstart, take);
    part[take] = '\0';
    fit_copy(part, sizeof(part), part, w);
    size_t used = strlen(part);
    if (used == 0) break;                // box narrower than one glyph: bail out
    draw_utf8(x, cy, part);
    drawn++;
    cy = (int16_t)(cy + line_h);
    p = wstart + used;
  }

  if (line_len > 0 && drawn < max_lines) {
    draw_utf8(x, cy, line);
    drawn++;
  }
  return drawn;
}

// =============================================================================
//  SHADING
// =============================================================================

// rd_dither_rect() is exactly the phase-0 case, and is kept as its own symbol
// only so the ~40 existing call sites do not have to grow an argument.
void rd_dither_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t level) {
  rd_dither_rect_phase(x, y, w, h, level, 0);
}

void rd_dither_rect_phase(int16_t x, int16_t y, int16_t w, int16_t h,
                          uint8_t level, uint8_t phase) {
  if (level == 0) return;
  if (level > RD_DITHER_MAX) level = RD_DITHER_MAX;
  if (!clip_rect(x, y, w, h)) return;

  // phase slides the 4x4 matrix: low 2 bits are the column shift, next 2 the
  // row shift. Only 4 bits are meaningful, and masking here (rather than
  // trusting the caller) is what lets a genome nibble be passed in raw.
  const uint8_t dx = (uint8_t)(phase & 3u);
  const uint8_t dy = (uint8_t)((phase >> 2) & 3u);

  const uint8_t color = s_u8g2.getDrawColor();

  // Fast path: write straight into the full framebuffer. Layout is
  // vertical-top-LSB, byte index = (y>>3)*tile_width*8 + x, bit = 1<<(y&7)
  // (u8g2_ll_hvline.c:100-105). Only valid for the F_ (full buffer) classes.
  uint8_t* buf = s_u8g2.getBufferPtr();
  const uint8_t tw = s_u8g2.getBufferTileWidth();
  const uint8_t th = s_u8g2.getBufferTileHeight();

  if (buf != NULL && th == 8 && (uint16_t)tw * 8u == (uint16_t)OLED_W) {
    // Within one page, (y & 3) == (bit index & 3) because the page start is a
    // multiple of 8, so each column has just one repeating 4-bit pattern.
    //
    // THE PHASE DOES NOT BREAK THAT. The row shift turns row index k into
    // (k + dy) & 3, which is still a function of k & 3 alone, so bit k and bit
    // k+4 of a page byte still select the same matrix row and the nibble is
    // still simply duplicated. Only the row a bit points AT moves; the period
    // stays 4. No restriction, no slow path, same cost as phase 0.
    uint8_t colmask[4];
    for (uint8_t cx = 0; cx < 4; cx++) {
      uint8_t nib = 0;
      for (uint8_t k = 0; k < 4; k++) {
        if (RD_BAYER[(((k + dy) & 3) * 4) + ((cx + dx) & 3)] < level) nib = (uint8_t)(nib | (1u << k));
      }
      colmask[cx] = (uint8_t)(nib | (nib << 4));
    }

    const int16_t y1 = (int16_t)(y + h - 1);
    const int16_t x1 = (int16_t)(x + w - 1);
    for (int16_t p = (int16_t)(y >> 3); p <= (int16_t)(y1 >> 3); p++) {
      const int16_t py = (int16_t)(p << 3);
      const uint8_t lo = (uint8_t)((y  > py)          ? (y  - py) : 0);
      const uint8_t hi = (uint8_t)((y1 < py + 7)      ? (y1 - py) : 7);
      const uint8_t rowmask = (uint8_t)((0xFFu << lo) & (0xFFu >> (7 - hi)));
      uint8_t* row = buf + ((uint16_t)p * (uint16_t)OLED_W);
      for (int16_t px = x; px <= x1; px++) {
        const uint8_t m = (uint8_t)(colmask[px & 3] & rowmask);
        if (m == 0) continue;
        if (color == 0)      row[px] = (uint8_t)(row[px] & (uint8_t)~m);
        else if (color == 2) row[px] = (uint8_t)(row[px] ^ m);
        else                 row[px] = (uint8_t)(row[px] | m);
      }
    }
    return;
  }

  // Portable fallback (never taken on this hardware, kept so the module cannot
  // silently draw nothing if the buffer mode ever changes).
  for (int16_t yy = y; yy < y + h; yy++) {
    for (int16_t xx = x; xx < x + w; xx++) {
      if (RD_BAYER[(((yy + dy) & 3) * 4) + ((xx + dx) & 3)] < level) {
        s_u8g2.drawPixel((u8g2_uint_t)xx, (u8g2_uint_t)yy);
      }
    }
  }
}

void rd_invert_rect(int16_t x, int16_t y, int16_t w, int16_t h) {
  if (!clip_rect(x, y, w, h)) return;
  const uint8_t color = s_u8g2.getDrawColor();
  s_u8g2.setDrawColor(2);   // XOR
  s_u8g2.drawBox((u8g2_uint_t)x, (u8g2_uint_t)y, (u8g2_uint_t)w, (u8g2_uint_t)h);
  s_u8g2.setDrawColor(color);
}

// =============================================================================
//  WIDGETS
// =============================================================================

void rd_bar(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t pct, uint8_t style) {
  // u8g2 coordinates are unsigned: a negative origin wraps to ~65500 and the
  // whole widget silently disappears. Reject it instead.
  if (x < 0 || y < 0) return;
  if (x + w > OLED_W) w = (int16_t)(OLED_W - x);
  if (y + h > OLED_H) h = (int16_t)(OLED_H - y);
  if (w < 3 || h < 3) return;
  if (pct > 100) pct = 100;

  s_u8g2.drawFrame((u8g2_uint_t)x, (u8g2_uint_t)y, (u8g2_uint_t)w, (u8g2_uint_t)h);

  const int16_t iw = (int16_t)(w - 2);
  const int16_t ih = (int16_t)(h - 2);
  if (iw <= 0 || ih <= 0) return;

  // Round to the nearest pixel, but never round a non-zero value down to zero:
  // "almost empty" and "empty" must not look the same.
  int16_t fill = (int16_t)(((int32_t)iw * (int32_t)pct + 50) / 100);
  if (fill == 0 && pct > 0) fill = 1;
  if (fill > iw) fill = iw;
  if (fill <= 0) return;

  const bool hatched = (style == RD_BAR_DITHER) ||
                       (style == RD_BAR_AUTO && pct <= RD_BAR_LOW_PCT);

  if (hatched) {
    // 50 % hatch reads as "weak" even at 3 px tall, and the solid leading edge
    // guarantees a 1 px fill is never swallowed by the dither pattern.
    rd_dither_rect((int16_t)(x + 1), (int16_t)(y + 1), fill, ih, RD_D50);
    s_u8g2.drawVLine((u8g2_uint_t)(x + 1), (u8g2_uint_t)(y + 1), (u8g2_uint_t)ih);
  } else {
    s_u8g2.drawBox((u8g2_uint_t)(x + 1), (u8g2_uint_t)(y + 1),
                   (u8g2_uint_t)fill, (u8g2_uint_t)ih);
  }
}

static uint8_t s_afford_pressed = 0;
// Which halves of the strip actually got a label THIS frame. rd_affordance()
// can legitimately run more than once per frame (a base screen plus a modal),
// so the sides accumulate and the inversion is applied once, by
// rd_affordance_echo(), at the end of the frame.
static uint8_t s_afford_sides   = 0;

void rd_affordance_pressed(uint8_t mask) {
  s_afford_pressed = (uint8_t)(mask & 3u);
  s_afford_sides   = 0;          // pushed once per frame: this IS the frame edge
}

void rd_affordance(const char* left, const char* right) {
  const bool has_l = (left  != NULL && *left  != '\0');
  const bool has_r = (right != NULL && *right != '\0');
  if (!has_l && !has_r) return;

  const int16_t bl = RD_AFFORD_BASELINE;   // 63
  s_u8g2.setFont(RD_FONT_BODY);            // 5x8_tf: Latin-1, fits rows 56..63

  int16_t l_end   = -1;                    // last pixel used on the left
  int16_t r_start = OLED_W;                // first pixel used on the right

  // Right side first: it owns the tail of the strip, the left label yields.
  // Cap it at 60 px (12 glyphs of 5x8) so a long right label can never starve
  // the left one; the longest affordance in strings_es.h is 8 glyphs = 40 px.
  int16_t right_w = 0;
  char    rbuf[40];
  if (has_r) {
    // marker: solid triangle pointing right, rows 58..62, cols 124..127
    s_u8g2.drawTriangle(127, 60, 124, 58, 124, 62);
    right_w = (int16_t)fit_copy(rbuf, sizeof(rbuf), right, 60);
    if (rbuf[0] != '\0') {
      r_start = (int16_t)(RD_AFFORD_TEXT_R - right_w + 1);
      draw_utf8(r_start, bl, rbuf);
    } else {
      r_start = 124;
    }
  }

  if (has_l) {
    // marker: solid triangle pointing left, rows 58..62, cols 0..3
    s_u8g2.drawTriangle(0, 60, 3, 58, 3, 62);
    int16_t avail = (int16_t)(r_start - 4 - RD_AFFORD_TEXT_L);
    char lbuf[40];
    int16_t lw = (int16_t)fit_copy(lbuf, sizeof(lbuf), left, avail);
    if (lbuf[0] != '\0') {
      draw_utf8(RD_AFFORD_TEXT_L, bl, lbuf);
      l_end = (int16_t)(RD_AFFORD_TEXT_L + lw - 1);
    } else {
      l_end = 3;
    }
  }

  // Dotted divider in the gap. Drawn between the labels only, so it can never
  // collide with an accent on a capital (A-acute in ATRAS reaches row 56).
  const int16_t gap_x0 = (int16_t)(l_end + 4);
  const int16_t gap_x1 = (int16_t)(r_start - 4);
  if (has_l && has_r && (gap_x1 - gap_x0) >= 8) {
    for (int16_t gx = gap_x0; gx <= gap_x1; gx += 3) {
      s_u8g2.drawPixel((u8g2_uint_t)gx, (u8g2_uint_t)60);
    }
  }

  // Only RECORD which halves are live. The inversion itself is not done here:
  // see rd_affordance_echo().
  if (has_l) s_afford_sides = (uint8_t)(s_afford_sides | 1u);
  if (has_r) s_afford_sides = (uint8_t)(s_afford_sides | 2u);
}

// Tactile echo: the half under a held button inverts for as long as it is held.
// This is the ONLY feedback in the firmware tied to the physical contact rather
// than to a recognised gesture, so it appears ~25 ms after the press instead of
// after DOUBLE_TAP_WINDOW_MS (280 ms) - the difference between a device that
// answers and one that lags.
//
// It lives OUT here because rd_invert_rect() is a real XOR and rd_affordance()
// is called twice in a frame whenever a modal is open (the base screen draws a
// strip, then draw_confirm() / draw_alert() draws its own): two XORs over the
// same half cancel, and the echo disappeared on every screen that had a modal.
// One call, one inversion, at the end of the frame - idempotent by structure
// rather than by luck.
void rd_affordance_echo(void) {
  const uint8_t sides = (uint8_t)(s_afford_sides & s_afford_pressed);
  s_afford_sides = 0;
  if (sides & 1u) rd_invert_rect(0, RD_AFFORD_Y, OLED_W / 2, AFFORDANCE_BAR_H);
  if (sides & 2u) rd_invert_rect(OLED_W / 2, RD_AFFORD_Y, OLED_W / 2, AFFORDANCE_BAR_H);
}

// =============================================================================
//  PANEL REGISTER EFFECTS - public entry points
//
//  These only ever arm software state. Not one of them touches the bus: the
//  bytes go out in fx_apply(), inside rd_end_frame(), where the panel-asleep
//  and panel-absent guards already live. The single exception is rd_fx_reset(),
//  which is the "unstick the panel" button and therefore has to be able to
//  write immediately.
// =============================================================================

void rd_flash(uint16_t ms) {
  if (ms == 0) { s_flash_active = false; rd_request_frame(); return; }
  // Capped on purpose. rd_flash() is an impact acknowledgement, not a display
  // mode, and an inverted 128x64 panel left on for minutes is both a burn-in
  // risk and, to anyone looking at it, a broken device.
  if (ms > RD_FX_FLASH_MAX_MS) ms = RD_FX_FLASH_MAX_MS;
  s_flash_until  = millis() + ms;
  s_flash_active = true;
  // Without this a 120 ms flash armed at FPS_LOW (4 fps) could expire before
  // any frame ever applied it, and nothing would be seen at all.
  rd_request_frame();
}

void rd_shake(uint8_t amp_px, uint16_t ms) {
  if (amp_px == 0 || ms == 0) { s_shake_active = false; rd_request_frame(); return; }
  if (amp_px > RD_FX_SHAKE_MAX_PX) amp_px = RD_FX_SHAKE_MAX_PX;
  // Capped for rd_flash()'s reason plus one of its own: rd_fps() now holds the
  // frame rate at RD_FX_MIN_FPS for as long as this is armed, and an uncapped
  // uint16 would let a caller pin the panel at 20 fps for 65 seconds with
  // web_service() starving behind it.
  if (ms > RD_FX_SHAKE_MAX_MS) ms = RD_FX_SHAKE_MAX_MS;
  s_shake_amp    = amp_px;
  s_shake_ms     = ms;
  s_shake_t0     = millis();
  s_shake_active = true;
  rd_request_frame();
}

void rd_contrast_ramp(uint8_t target, uint16_t ms) {
  if (ms == 0) {
    s_contrast_base = target;
    s_ramp_active   = false;
    rd_request_frame();
    return;
  }
  // Retarget from where the panel visibly IS, not from where the previous ramp
  // was aiming: restarting a ramp mid-flight must not make the brightness jump.
  const uint32_t now = millis();
  s_ramp_from   = fx_base_at(now);
  s_ramp_to     = target;
  s_ramp_t0     = now;
  s_ramp_ms     = ms;
  s_ramp_active = true;
  rd_request_frame();
}

void rd_breathe(uint8_t amp, uint16_t period_ms) {
  if (amp == 0 || period_ms == 0) {
    s_breathe_amp    = 0;
    s_breathe_period = 0;
    rd_request_frame();          // let the next frame put the base back
    return;
  }
  if (amp > RD_FX_BREATHE_MAX_AMP) amp = RD_FX_BREATHE_MAX_AMP;
  // Below roughly one frame period the sine is sampled once or twice per cycle
  // and aliases into a random flicker instead of a breath.
  if (period_ms < RD_FX_BREATHE_MIN_MS) period_ms = RD_FX_BREATHE_MIN_MS;
  s_breathe_amp    = amp;
  s_breathe_period = period_ms;
}

void rd_fx_reset(void) {
  // A ramp in flight snaps to its target rather than to where it happened to
  // be: the target is the resting level its caller asked for.
  if (s_ramp_active) { s_contrast_base = s_ramp_to; s_ramp_active = false; }
  s_breathe_amp    = 0;
  s_breathe_period = 0;

  // Invert and offset, plus the same panel-asleep / panel-absent guard.
  fx_settle_transients();
  if (!(s_display_on && s_display_ok)) return;

  if (s_contrast_cur != s_contrast_base) {
    s_u8g2.setContrast(s_contrast_base);
    s_contrast_cur = s_contrast_base;
  }
}

// =============================================================================
//  BOOT / FATAL
// =============================================================================

void rd_splash(void) {
  s_u8g2.clearBuffer();
  s_u8g2.setDrawColor(1);

  // Procedural egg (no dependency on sprites.h, which is another module).
  const int16_t ecx = 64, ecy = 22, erx = 10, ery = 14;
  s_u8g2.drawFilledEllipse((u8g2_uint_t)ecx, (u8g2_uint_t)ecy,
                           (u8g2_uint_t)erx, (u8g2_uint_t)ery, U8G2_DRAW_ALL);

  // Speckled shading on the lower half. Colour 0 carves it out of the egg and
  // does nothing to the empty background around it.
  s_u8g2.setDrawColor(0);
  rd_dither_rect((int16_t)(ecx - erx), ecy, (int16_t)(erx * 2 + 1), (int16_t)(ery + 1), RD_D25);

  // Crack: a zigzag scored across the shell, also in colour 0.
  s_u8g2.drawLine(55, 17, 59, 22);
  s_u8g2.drawLine(59, 22, 63, 16);
  s_u8g2.drawLine(63, 16, 67, 22);
  s_u8g2.drawLine(67, 22, 71, 17);
  s_u8g2.setDrawColor(1);

  // Divider + wordmark + version.
  rd_dither_rect(14, 44, 100, 1, RD_D50);
  rd_text_center(55, RD_FONT_HEAD, FW_NAME);
  rd_text_center(63, RD_FONT_TINY, "v" FW_VERSION);

  if (s_display_on && s_display_ok) s_u8g2.sendBuffer();

  // The splash bypassed the scheduler; hand it a clean deadline.
  s_in_frame      = false;
  s_next_frame_ms = millis();
}

// rd_fatal() is gone (plan T10, audit risk 15): a device that halts forever
// because the panel did not answer cannot be fixed by the person holding it.
// The failure is a screen now - ui/screen_error.cpp, ERRK_DISPLAY - which keeps
// the same LED pattern and adds a retry.
