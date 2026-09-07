// =============================================================================
//  Pebblebol host tests - test_anim.cpp
//  ui/anim_ease.{h,cpp}: THE MOTION MATHS (P10-C3).
//
//  WHY THIS BINARY EXISTS. Every film in this firmware is built out of five
//  pieces of integer arithmetic - a parabola, a lunge, a lerp, a percentage of
//  a window, and an ordered Bayer test that crumbles the frontier of a
//  dissolve. They lived as `static` functions inside ui/actfx.cpp, which
//  includes render.h and therefore Arduino.h, SO NO HOST BINARY HAD EVER
//  COMPILED OR EXECUTED ONE OF THEM. Seven films have shipped on top of them
//  since P6-C3 and ui/screen_encounter.cpp's two new ones ride the same code.
//
//  THE PROPERTIES ARE THE ONES THE CALLERS ACTUALLY DEPEND ON, and the file
//  says which caller depends on which:
//    * TOTAL - a zero-length window, a t past the end and an inverted range all
//      answer something defined rather than dividing by zero. actfx_begin()
//      hands ae_pct() windows built from #defines that a retune can invert.
//    * EXACTLY ZERO AT BOTH ENDS for ae_hop() and ae_lunge(). This is what
//      stops a film that is interrupted and restarted from accumulating drift,
//      and it is the property ui/actfx.cpp's own comment claims in prose.
//    * ae_lunge() REACHES FULL EXTENSION AND HOLDS IT. That is the whole reason
//      it exists rather than a second parabola: actfx.cpp:305 records that at
//      20 fps a parabola gives a bite that never quite arrives.
//    * THE DISSOLVE FRONTIER COVERS BOTH ENDS. pct = 0 must mean ALL of the
//      sprite and pct = 100 exactly NONE of it, whatever its height - which is
//      why the front travels three rows past each end.
//
//  Identifiers and comments: English.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "ui/anim_ease.h"

// -----------------------------------------------------------------------------
//  THE PARABOLA
// -----------------------------------------------------------------------------
TEST(the_hop_is_zero_at_both_ends_and_peaks_at_its_amplitude) {
  static const uint32_t kLens[] = { 1u, 2u, 7u, 60u, 420u, 2600u, 65535u };
  for (unsigned i = 0; i < sizeof(kLens) / sizeof(kLens[0]); ++i) {
    const uint32_t len = kLens[i];
    for (uint8_t amp = 1; amp <= 12u; ++amp) {
      // Zero at both ends and BEYOND the end: a film that is serviced one tick
      // late must land, not overshoot.
      CHECK_EQ(ae_hop(0u, len, amp), 0);
      CHECK_EQ(ae_hop(len, len, amp), 0);
      CHECK_EQ(ae_hop(len + 1u, len, amp), 0);
      CHECK_EQ(ae_hop(len * 4u + 1u, len, amp), 0);

      // Negative throughout, because up is -y, and never past the amplitude.
      int16_t peak = 0;
      for (uint32_t t = 0; t <= len; ++t) {
        const int16_t v = ae_hop(t, len, amp);
        CHECK(v <= 0);
        CHECK(v >= -(int16_t)amp);
        if (v < peak) peak = v;
      }
      // THE PEAK IS EXACT ON AN EVEN WINDOW: 4*a*(L/2)*(L/2)/L^2 is a, with no
      // truncation anywhere. That is the sharp statement; "it rises at all" is
      // NOT true for every window, and the reason is worth recording rather
      // than asserting around. On a SHORT window integer truncation eats the
      // whole parabola - ae_hop(1, 3, 1) is 4*1*1*2/9 = 0 - so a hop of one
      // pixel over three milliseconds never leaves the ground. Nothing in the
      // firmware hops over a window shorter than AF_PET_HOP_MS (1,150 ms), so
      // this is a property of the arithmetic and not a defect; it is here so
      // that a future film with a very short beat finds it written down.
      if ((len % 2u) == 0u && len >= 2u)
        CHECK_EQ(ae_hop(len / 2u, len, amp), -(int16_t)amp);
      // It rises at all once the window is long enough to survive the
      // truncation: amp*len^2/4 must exceed len^2/4, i.e. amp >= 1 and len
      // large enough that 4*amp*(len/2)^2 / len^2 rounds to at least 1. On an
      // even window that is exact, so this holds from len 2 upward.
      if ((len % 2u) == 0u && len >= 2u) CHECK(peak <= -1);
    }
  }
  // THE WINDOW ui/actfx.cpp's s_dur CAN ACTUALLY HOLD, swept to its uint16_t
  // ceiling. Before P10-C3 the numerator wrapped above amp*len^2 = 2^32 and the
  // DENOMINATOR wrapped at len = 65,536, so a long window did not degrade - it
  // answered an arbitrary number. Widening the multiply fixed it; this is what
  // says so.
  for (uint32_t len = 60000u; len <= 65535u; len += 1111u)
    for (uint8_t amp = 1; amp <= 12u; ++amp) {
      CHECK_EQ(ae_hop(0u, len, amp), 0);
      CHECK_EQ(ae_hop(len, len, amp), 0);
      CHECK(ae_hop(len / 2u, len, amp) <= -(int16_t)(amp - 1u));
      CHECK(ae_hop(len / 2u, len, amp) >= -(int16_t)amp);
    }

  // A zero-length window is answered, not divided by.
  CHECK_EQ(ae_hop(0u, 0u, 8u), 0);
  CHECK_EQ(ae_hop(99u, 0u, 8u), 0);
  // ...and so is a zero amplitude.
  for (uint32_t t = 0; t <= 100u; ++t) CHECK_EQ(ae_hop(t, 100u, 0u), 0);
}

TEST(the_hop_rises_then_falls_and_never_doubles_back) {
  const uint32_t len = 600u;
  int16_t prev = 0;
  uint32_t turned = 0;
  for (uint32_t t = 1; t <= len; ++t) {
    const int16_t v = ae_hop(t, len, 10u);
    if (v > prev) ++turned;                    // started coming down
    prev = v;
  }
  CHECK(turned > 0);                           // it does come down
  // ONE turning point, not several: a jitter here is a hop that stutters.
  int flips = 0;
  int dir = 0;
  prev = 0;
  for (uint32_t t = 1; t <= len; ++t) {
    const int16_t v = ae_hop(t, len, 10u);
    const int d = (v < prev) ? -1 : (v > prev ? 1 : dir);
    if (dir != 0 && d != 0 && d != dir) ++flips;
    if (d != 0) dir = d;
    prev = v;
  }
  CHECK_EQ(flips, 1);
}

// -----------------------------------------------------------------------------
//  THE LUNGE - out, hold, back
// -----------------------------------------------------------------------------
TEST(the_lunge_reaches_full_extension_holds_it_and_comes_back_to_zero) {
  static const uint32_t kLens[] = { 1u, 3u, 10u, 100u, 650u, 5000u };
  for (unsigned i = 0; i < sizeof(kLens) / sizeof(kLens[0]); ++i) {
    const uint32_t len = kLens[i];
    const uint8_t amp = 9u;
    // 0 AT ph == 0 FOR EVERY WINDOW, len = 1 included. This was NOT true before
    // P10-C3: the degenerate arm that keeps a too-short window extending
    // answered `amp` at every phase, contradicting the contract its own header
    // stated. No film reaches a window that short, and the guard is one line.
    CHECK_EQ(ae_lunge(0u, len, amp), 0);
    CHECK_EQ(ae_lunge(len, len, amp), 0);
    CHECK_EQ(ae_lunge(len + 50u, len, amp), 0);

    int16_t top = 0;
    uint32_t at_top = 0;
    for (uint32_t ph = 0; ph < len; ++ph) {
      const int16_t v = ae_lunge(ph, len, amp);
      CHECK(v >= 0);
      CHECK(v <= (int16_t)amp);
      if (v > top) { top = v; at_top = 0; }
      if (v == (int16_t)amp) ++at_top;
    }
    // IT ARRIVES - from len 2 upward. At len 1 the only phase is ph = 0, which
    // the P10-C3 guard answers 0, so a one-millisecond lunge never extends.
    if (len >= 2u) CHECK_EQ((int)top, (int)amp);
    // ...and HOLDS, which is the whole difference from a parabola. Below five
    // steps the 35/30/35 split has nowhere to put a hold and the function
    // answers `amp` flat, which is the degenerate case it documents.
    if (len >= 10u) CHECK(at_top >= 2u);
  }
  CHECK_EQ(ae_lunge(0u, 0u, 5u), 0);
  for (uint32_t ph = 0; ph < 40u; ++ph) CHECK_EQ(ae_lunge(ph, 40u, 0u), 0);
}

// -----------------------------------------------------------------------------
//  THE LERP AND THE WINDOW
// -----------------------------------------------------------------------------
TEST(the_lerp_starts_at_a_and_lands_exactly_on_b) {
  CHECK_EQ(ae_lerp(0u, 100u, 10, 50), 10);
  CHECK_EQ(ae_lerp(100u, 100u, 10, 50), 50);
  CHECK_EQ(ae_lerp(101u, 100u, 10, 50), 50);
  CHECK_EQ(ae_lerp(1000u, 100u, 10, 50), 50);
  CHECK_EQ(ae_lerp(50u, 100u, 10, 50), 30);
  // BACKWARDS, which is how ui/screen_encounter.cpp closes its brackets and how
  // the item film climbs: b below a must fall and land, not wrap.
  CHECK_EQ(ae_lerp(0u, 100u, 43, 26), 43);
  CHECK_EQ(ae_lerp(100u, 100u, 43, 26), 26);
  // 35 AND NOT 34: C integer division truncates toward zero, so a DOWNWARD
  // lerp lags a rounded one by up to a pixel in the middle of its travel.
  // Pinned rather than corrected - see the note in ui/anim_ease.h.
  CHECK_EQ(ae_lerp(50u, 100u, 43, 26), 35);
  // Monotone in both directions.
  int16_t prev = ae_lerp(0u, 520u, 43, 26);
  for (uint32_t t = 1; t <= 520u; ++t) {
    const int16_t v = ae_lerp(t, 520u, 43, 26);
    CHECK(v <= prev);
    prev = v;
  }
  // A zero-length window answers the DESTINATION, which is what a caller with
  // a retuned-to-zero beat needs: the film ends where it was going.
  CHECK_EQ(ae_lerp(0u, 0u, 10, 50), 50);
  // Negative endpoints, because a dy is signed.
  CHECK_EQ(ae_lerp(0u, 10u, -8, 8), -8);
  CHECK_EQ(ae_lerp(10u, 10u, -8, 8), 8);
  CHECK_EQ(ae_lerp(5u, 10u, -8, 8), 0);
}

TEST(the_window_saturates_at_both_ends_and_refuses_an_inverted_one) {
  CHECK_EQ(ae_pct(0u, 0u, 100u), 0u);
  CHECK_EQ(ae_pct(100u, 0u, 100u), 100u);
  CHECK_EQ(ae_pct(200u, 0u, 100u), 100u);
  CHECK_EQ(ae_pct(50u, 0u, 100u), 50u);
  // Before the window opens.
  CHECK_EQ(ae_pct(10u, 20u, 40u), 0u);
  CHECK_EQ(ae_pct(20u, 20u, 40u), 0u);
  CHECK_EQ(ae_pct(30u, 20u, 40u), 50u);
  CHECK_EQ(ae_pct(40u, 20u, 40u), 100u);
  // AN INVERTED OR EMPTY WINDOW. A retune that swaps two cumulative boundaries
  // must not divide by zero or wrap to 255 - it must answer 0 and leave the
  // beat un-started.
  CHECK_EQ(ae_pct(30u, 40u, 20u), 0u);
  CHECK_EQ(ae_pct(30u, 30u, 30u), 0u);
  // Never above 100, over a wide sweep.
  for (uint32_t t = 0; t < 3000u; t += 7u) CHECK(ae_pct(t, 500u, 1500u) <= 100u);
}

// -----------------------------------------------------------------------------
//  THE DISSOLVE
// -----------------------------------------------------------------------------
// pct = 100 IS EXACT IN BOTH DIRECTIONS. pct = 0 IS EXACT IN ONLY ONE, and this
// is the case that found it. ui/actfx.cpp's comment claimed the frontier
// "travels three rows past each end, so pct = 0 and pct = 100 mean exactly
// 'all of it' and 'none of it'". For AE_DIS_DOWN both halves are true. For
// AE_DIS_UP the frontier starts AT row h rather than three past it, so the
// trailing two-row crumble band already covers the sprite's bottom two rows on
// the very first frame. Recorded as behaviour, not fixed - ui/anim_ease.h says
// why - and this test is what stops it from being rediscovered as a surprise.
TEST(a_dissolve_at_a_hundred_per_cent_draws_nothing_in_either_direction) {
  for (uint8_t h = 1; h <= 40u; ++h) {
    for (int d = 0; d < 2; ++d) {
      const uint8_t dir = (uint8_t)(d ? AE_DIS_UP : AE_DIS_DOWN);
      const int16_t f100 = ae_dissolve_front(dir, h, 100u);
      for (uint8_t row = 0; row < h; ++row)
        for (int16_t x = 0; x < 4; ++x)
          for (int16_t y = 0; y < 4; ++y)
            if (!ae_dissolve_skip(dir, row, f100, x, y))
              nt_fail_at(__FILE__, __LINE__, "pct 100 drew a pixel");
    }
  }
}

TEST(a_downward_dissolve_starts_solid_and_an_upward_one_starts_two_rows_short) {
  for (uint8_t h = 4; h <= 40u; ++h) {
    // DOWN: nothing is skipped at pct 0, anywhere.
    const int16_t d0 = ae_dissolve_front(AE_DIS_DOWN, h, 0u);
    for (uint8_t row = 0; row < h; ++row)
      for (int16_t x = 0; x < 4; ++x)
        for (int16_t y = 0; y < 4; ++y)
          if (ae_dissolve_skip(AE_DIS_DOWN, row, d0, x, y))
            nt_fail_at(__FILE__, __LINE__, "a downward dissolve skipped at pct 0");

    // UP: every row ABOVE the last two is solid...
    const int16_t u0 = ae_dissolve_front(AE_DIS_UP, h, 0u);
    for (uint8_t row = 0; row + 2u < (uint16_t)h; ++row)
      for (int16_t x = 0; x < 4; ++x)
        for (int16_t y = 0; y < 4; ++y)
          if (ae_dissolve_skip(AE_DIS_UP, row, u0, x, y))
            nt_fail_at(__FILE__, __LINE__, "an upward dissolve skipped above its band");

    // ...and the last two are already crumbling, about half of each. That is
    // the asymmetry, asserted rather than assumed away.
    int drawn = 0, total = 0;
    for (uint8_t row = (uint8_t)(h - 2u); row < h; ++row)
      for (int16_t x = 0; x < 4; ++x)
        for (int16_t y = 0; y < 4; ++y) {
          ++total;
          if (!ae_dissolve_skip(AE_DIS_UP, row, u0, x, y)) ++drawn;
        }
    CHECK_EQ(drawn * 2, total);
  }
}

TEST(a_dissolve_eats_the_sprite_from_the_end_its_direction_names) {
  const uint8_t h = 24u;
  // UP eats from the BOTTOM row upwards; DOWN from the TOP row downwards. This
  // is the distinction ui/screen_encounter.cpp got wrong in its first draft -
  // every body in the atlas stands on the bottom of its box, so the wrong
  // direction takes the whole creature away in the first fifth of the beat.
  const int16_t up   = ae_dissolve_front(AE_DIS_UP,   h, 50u);
  const int16_t down = ae_dissolve_front(AE_DIS_DOWN, h, 50u);
  // Halfway: UP has eaten the lower half, DOWN the upper half.
  CHECK_EQ(ae_dissolve_skip(AE_DIS_UP, 0u, up, 0, 0), 0u);          // top survives
  CHECK_EQ(ae_dissolve_skip(AE_DIS_UP, (uint8_t)(h - 1u), up, 0, 0), 1u);
  CHECK_EQ(ae_dissolve_skip(AE_DIS_DOWN, 0u, down, 0, 0), 1u);      // top gone
  CHECK_EQ(ae_dissolve_skip(AE_DIS_DOWN, (uint8_t)(h - 1u), down, 0, 0), 0u);

  // The frontier is MONOTONE in pct in both directions: a dissolve that
  // un-eats a row reads as a flicker.
  int16_t prev_up = ae_dissolve_front(AE_DIS_UP, h, 0u);
  int16_t prev_dn = ae_dissolve_front(AE_DIS_DOWN, h, 0u);
  for (uint8_t pct = 1; pct <= 100u; ++pct) {
    const int16_t u = ae_dissolve_front(AE_DIS_UP, h, pct);
    const int16_t d = ae_dissolve_front(AE_DIS_DOWN, h, pct);
    CHECK(u <= prev_up);
    CHECK(d >= prev_dn);
    prev_up = u; prev_dn = d;
  }

  // AE_DIS_NONE skips nothing, ever - it is the "draw it solid" arm every film
  // uses before its dissolve starts.
  //
  // P10-C6: SWEPT OVER ALL SIXTEEN DITHER PHASES, and the previous form pinned
  // (3,5) only. ae_bayer(3,5) is 7, one below the >= 8 threshold, so deleting
  // the `dir == AE_DIS_NONE` guard changed nothing here: AE_DIS_NONE is 0 and
  // AE_DIS_DOWN is 2, so without the guard NONE falls into the DOWN arm with
  // front 0, marks rows 0 and 1 as the crumbling band and drops half their
  // pixels on every frame a film draws "solid". The matrix is a set of sixteen
  // phases and the case sampled the one that could not see it.
  for (uint8_t row = 0; row < h; ++row)
    for (uint8_t px = 0; px < 4u; ++px)
      for (uint8_t py = 0; py < 4u; ++py)
        CHECK_EQ(ae_dissolve_skip(AE_DIS_NONE, row, 0, px, py), 0u);
}

TEST(the_crumbling_frontier_is_two_rows_deep_and_about_half_of_it) {
  const uint8_t h = 24u;
  // A row well inside the surviving part is drawn whole; a row ON the frontier
  // loses about half its pixels to the Bayer test. That is what makes a
  // dissolve read as evaporation instead of as a window blind.
  const int16_t front = ae_dissolve_front(AE_DIS_DOWN, h, 50u);
  int edge_drawn = 0, edge_total = 0, deep_drawn = 0, deep_total = 0;
  for (int16_t x = 0; x < 24; ++x) {
    for (int16_t y = 0; y < 24; ++y) {
      const uint8_t edge_row = (uint8_t)(front + 1);
      const uint8_t deep_row = (uint8_t)(front + 8);
      if (deep_row >= h) continue;
      ++edge_total; ++deep_total;
      if (!ae_dissolve_skip(AE_DIS_DOWN, edge_row, front, x, y)) ++edge_drawn;
      if (!ae_dissolve_skip(AE_DIS_DOWN, deep_row, front, x, y)) ++deep_drawn;
    }
  }
  CHECK_EQ(deep_drawn, deep_total);                       // solid behind the front
  CHECK(edge_drawn * 100 < edge_total * 60);              // and crumbling on it
  CHECK(edge_drawn * 100 > edge_total * 40);
}

TEST(the_bayer_matrix_is_a_permutation_of_the_sixteen_levels) {
  int seen[16];
  memset(seen, 0, sizeof seen);
  for (int16_t y = 0; y < 4; ++y)
    for (int16_t x = 0; x < 4; ++x) {
      const uint8_t v = ae_bayer(x, y);
      CHECK(v < 16u);
      ++seen[v];
    }
  for (int i = 0; i < 16; ++i) CHECK_EQ(seen[i], 1);
  // It TILES: the matrix repeats every four pixels in both axes, which is what
  // makes the crumble stable under a prop that is also sliding.
  for (int16_t y = -8; y < 8; ++y)
    for (int16_t x = -8; x < 8; ++x)
      CHECK_EQ(ae_bayer(x, y), ae_bayer((int16_t)(x + 4), (int16_t)(y + 4)));
}
