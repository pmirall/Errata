// =============================================================================
//  PEBBLEBOL - ui/screen_time.cpp
//  The TIME entry screen, migrated into the screen table by P2-C11b.
//
//  The clock itself is hardware/gametime.cpp's and the persisted baseline is
//  persistence/game_state.cpp's, so both are reached through the ui.h seams
//  (ui_get_clock / ui_set_clock). What lives HERE is the five-field editor:
//  the calendar arithmetic, the wrapping, the day clamp and the auto-repeat.
//
//  PURE translation unit.
// =============================================================================
#include "screen_time.h"

#include <stdio.h>

#include "../core/strings_es.h"
#include "../hardware/input.h"    // INPUT_BTN_R only: a button index, no driver
#include "gfx.h"
#include "screen.h"
#include "ui.h"

static uint16_t s_clk[CLK_FIELDS];      // year, month, day, hour, minute
static uint8_t  s_field  = 0;
static uint32_t s_rep_ms = 0;           // last auto-repeat increment

uint16_t time_field(uint8_t field) { return (field < CLK_FIELDS) ? s_clk[field] : 0u; }
uint8_t  time_cursor(void)         { return s_field; }

static uint8_t days_in_month(uint16_t year, uint16_t month) {
  static const uint8_t kDays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  if (month < 1 || month > 12) return 31;
  if (month == 2) {
    const bool leap = ((year % 4u) == 0u && (year % 100u) != 0u) || ((year % 400u) == 0u);
    return leap ? 29u : 28u;
  }
  return kDays[month - 1];
}

// Keep the day inside the month just selected, so 31 January -> March never
// leaves an impossible 31 February on screen.
static void clamp_day(void) {
  const uint8_t dim = days_in_month(s_clk[CLK_YEAR], s_clk[CLK_MONTH]);
  if (s_clk[CLK_DAY] > dim) s_clk[CLK_DAY] = dim;
  if (s_clk[CLK_DAY] < 1u)  s_clk[CLK_DAY] = 1u;
}

void time_enter(void) {
  uint16_t y = 0;
  uint8_t  mo = 0, d = 0, h = 0, mi = 0;
  if (ui_get_clock(&y, &mo, &d, &h, &mi) && y >= CLK_YEAR_MIN && y <= CLK_YEAR_MAX) {
    s_clk[CLK_YEAR]  = y;
    s_clk[CLK_MONTH] = mo;
    s_clk[CLK_DAY]   = d;
    s_clk[CLK_HOUR]  = h;
    s_clk[CLK_MIN]   = mi;
  } else {
    // Nothing trustworthy to start from: a round, obviously-a-placeholder date.
    s_clk[CLK_YEAR]  = CLK_YEAR_MIN;
    s_clk[CLK_MONTH] = 1;
    s_clk[CLK_DAY]   = 1;
    s_clk[CLK_HOUR]  = 12;
    s_clk[CLK_MIN]   = 0;
  }
  clamp_day();
  s_field  = CLK_YEAR;
  s_rep_ms = 0;
}

static void bump(void) {
  switch (s_field) {
    case CLK_YEAR:
      s_clk[CLK_YEAR] = (s_clk[CLK_YEAR] >= CLK_YEAR_MAX) ? (uint16_t)CLK_YEAR_MIN
                                                          : (uint16_t)(s_clk[CLK_YEAR] + 1);
      clamp_day();
      break;
    case CLK_MONTH:
      s_clk[CLK_MONTH] = (uint16_t)((s_clk[CLK_MONTH] % 12u) + 1u);
      clamp_day();
      break;
    case CLK_DAY: {
      const uint8_t dim = days_in_month(s_clk[CLK_YEAR], s_clk[CLK_MONTH]);
      s_clk[CLK_DAY] = (uint16_t)((s_clk[CLK_DAY] % dim) + 1u);
      break;
    }
    case CLK_HOUR: s_clk[CLK_HOUR] = (uint16_t)((s_clk[CLK_HOUR] + 1u) % 24u); break;
    case CLK_MIN:  s_clk[CLK_MIN]  = (uint16_t)((s_clk[CLK_MIN]  + 1u) % 60u); break;
    default: break;
  }
  ui_note_input();
}

// gt_set_epoch(CAL_USER) is the one source allowed to move the clock backwards,
// so a user correcting a wrong date is never refused.
static void commit(void) {
  if (!ui_set_clock(s_clk[CLK_YEAR], (uint8_t)s_clk[CLK_MONTH], (uint8_t)s_clk[CLK_DAY],
                    (uint8_t)s_clk[CLK_HOUR], (uint8_t)s_clk[CLK_MIN])) {
    ui_toast(STR_CLK_BAD);
    ui_input_flush();      // as on the success path: a held L must not re-commit
    return;
  }
  ui_toast(STR_CLK_SAVED);
  ui_input_flush();        // the release of the confirming hold must not fire below
  ui_back();
}

void time_render(void) {
  gfx_header(S(STR_CLK_TITLE), nullptr);

  static const uint16_t kLabel[CLK_FIELDS] = {
    STR_CLK_YEAR, STR_CLK_MONTH, STR_CLK_DAY, STR_CLK_HOUR, STR_CLK_MIN
  };
  char val[8];
  snprintf(val, sizeof val, (s_field == CLK_YEAR) ? "%u" : "%02u",
           (unsigned)s_clk[s_field]);

  gfx_text_fit(GF_BODY, 2, 22, 124, S(kLabel[s_field]));
  gfx_text(GF_BIG, 2, 42, val);

  // The whole stamp, so the field being edited always has its context.
  char stamp[32];
  snprintf(stamp, sizeof stamp, "%04u-%02u-%02u %02u:%02u",
           (unsigned)s_clk[CLK_YEAR], (unsigned)s_clk[CLK_MONTH],
           (unsigned)s_clk[CLK_DAY],  (unsigned)s_clk[CLK_HOUR],
           (unsigned)s_clk[CLK_MIN]);
  // Both lines sit ABOVE row 56. The legacy screen put the hint on baseline 58,
  // inside the affordance strip invariant 6 says is always drawn, so the two
  // overprinted each other on the panel.
  gfx_text_fit(GF_TINY, 2, 50, 124, stamp);
  gfx_text_fit(GF_TINY, 2, 55, 124, S(STR_CLK_HOLD));

  // No countdown bar: the strip it would draw on belongs to the stamp, and a
  // date half typed in must not be thrown away by a 20 s timeout either - the
  // row is SF_STICKY for exactly that reason.
  gfx_affordance(S(STR_AF_NEXT), S(STR_AF_ADD));
}

// Auto-repeat for the right button. GST_HOLD_R fires once at HOLD_MS and the
// recogniser never repeats it, so the sustained increment is timed here against
// the debounced press instant the recogniser already owns.
void time_update(uint32_t now_ms) {
  if (!ui_btn_down(INPUT_BTN_R)) { s_rep_ms = 0; return; }
  if (ui_btn_hold_ms(INPUT_BTN_R) < (uint32_t)REPEAT_START_MS) return;
  if (s_rep_ms != 0 && (uint32_t)(now_ms - s_rep_ms) < (uint32_t)REPEAT_RATE_MS) return;
  s_rep_ms = now_ms;
  bump();
}

void time_input(Gesture g) {
  switch (g) {
    case GST_TAP_R:
      bump();
      break;
    case GST_HOLD_R:                     // first increment of the auto-repeat
      s_rep_ms = ui_now_ms();
      bump();
      break;
    case GST_TAP_L:
    case GST_HOLD_L:
      // HOLD_L confirms; a tap only moves on. Both are L so the thumb never
      // leaves the button it is already on.
      if (g == GST_HOLD_L) { commit(); return; }
      s_field = (uint8_t)((s_field + 1u) % (uint8_t)CLK_FIELDS);
      break;
    case GST_BOTH:
      ui_back();
      break;
    case GST_LONG_BOTH:
      // Invariant 2. The row is SF_LOCK_INPUT so that HOLD_R can mean "+1"
      // here and nowhere else, which means the global grammar does not run
      // before this handler and HOME has to be honoured by hand.
      ui_goto(SCR_HOME);
      break;
    default:
      break;
  }
}
