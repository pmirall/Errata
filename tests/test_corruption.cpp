// =============================================================================
//  ERRATA host test - test_corruption.cpp
//  THE EFFECTS HALF OF CORRUPTION (spec section 55, plan P9-C5).
//
//  THE STATUS HALF IS NOT HERE. cor_apply / cor_expire / cor_clear, the 24 h
//  deadline, the boundary on both sides and the uncalibrated-clock refusal are
//  tested in tests/test_encounters.cpp, where P5-C3 produced them, and they are
//  left alone. What this file covers is what P9-C5 attached to that bit:
//
//    1. THE TIMER REACHES THE FIRMWARE. cor_service() is the walk app.cpp's
//       1 Hz tick performs; before this chunk the deadline had no reader at all
//       outside tests/.
//    2. THE GLITCH IS BOUNDED. Not "the row struct looks sensible" - the
//       PIXELS a painter touches are inside the body's ink rectangle, measured
//       against a control render, with a recorder that names the first escape.
//    3. THE BATTLE MODIFIER IS +1/-1 AND DOES NOT STACK - not across rounds,
//       not against Infectar, not across a save/reload, not across two battles.
//    4. THE IDLE ANIMATION CANNOT OUTLIVE THE STATUS.
//    5. EVOC_CORRUPTED UNLOCKS AT MOST TWO FAMILIES and refuses without the
//       flag.
//    6. IT NEVER DESTROYS. A corrupted Bug that is saved, reloaded, traded
//       and bred from is still exactly one Bug with a bounded effect.
//
//  WHAT THIS FILE CANNOT ASSERT, said plainly because the brief asks for it:
//  NOTHING HERE PROVES THE GLITCH LOOKS LIKE A GLITCH. A byte count is not a
//  drawing. The containment below is a real property measured on real pixels,
//  and it is the only visual property a machine can settle; whether the effect
//  READS as corruption is a human judgement, and tests/tools/corrupt_view.cpp
//  is the instrument for it - it renders a real 24x24 species body with and
//  without the noise, at the real ink box, as text a person can look at.
//
//  IT ALSO CANNOT EXECUTE ui/petfx.cpp. That translation unit includes
//  render.h, hence Arduino.h and U8g2lib.h, so no host binary can link it. The
//  geometry and the behaviour-row choice were moved into ui/corrupt_fx.cpp for
//  exactly that reason and are driven here directly; what stays unexecuted is
//  the four-line painting loop in petfx_draw_body() and the one-line call site
//  in app/app.cpp's logic tick, both of which tools/check.sh greps for by name.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "core/nt_types.h"
#include "fakes/gfx_fb.h"
#include "fakes/kv_mem.h"
#include "game/battle.h"
#include "game/box.h"
#include "game/breeding.h"
#include "game/corruption.h"
#include "game/evolution.h"
#include "game/genome.h"
#include "game/inventory.h"
#include "game/validate.h"
#include "game/xp.h"
#include "networking/protocol.h"
#include "persistence/game_state.h"
#include "persistence/save_manager.h"
#include "persistence/save_schema.h"
#include "ui/corrupt_fx.h"
#include "ui/gfx.h"

// =============================================================================
//  FIXTURES
// =============================================================================
static Genome sealed_genome(uint32_t lineage)
{
  Genome g;
  memset(&g, 0, sizeof g);
  g.lineage_id = lineage ? lineage : 1u;
  g.g0 = 0x1234u; g.g1 = 0x5678u; g.g2 = 0x09ABu;
  g.generation = 1u;
  genome_seal(g);                       // stamps magic_ver and the CRC
  return g;
}

// A Bug that validate_bug() ACCEPTS. Every "it never destroys" case in
// this file compares a corrupted record against the validator every load and
// every trade goes through, so the fixture has to start clean or the assertion
// is about the fixture. Same construction as tests/test_validate.cpp's
// mk_valid(), and deliberately not a private variant of it.
static void mk_bug(BugInstance& p, uint8_t species, uint8_t level, uint32_t id)
{
  memset(&p, 0, sizeof p);
  p.magic         = (uint16_t)BUG_MAGIC;
  p.layout_ver    = (uint8_t)BUG_LAYOUT_VER;
  p.species_id    = species;
  p.id            = id;
  p.level         = level;
  p.origin        = (uint8_t)ORIGIN_WILD;
  p.custom_sprite = (uint8_t)ER_CUSTOM_SPRITE_NONE;
  p.genome        = sealed_genome(0xA5A5A500u + id);
  for (uint8_t c = 0; c < (uint8_t)ER_CARE_COUNT; ++c)
    p.care[c] = (int32_t)ER_CARE_MILLI_MAX;
  const SpeciesDef* sp = species_get(species);
  if (sp == nullptr) return;
  memcpy(p.moves, sp->moves, sizeof p.moves);
  p.hp_cur    = xp_hp_max(sp->base_hp, level);
  p.evo_state = (uint8_t)(sp->stage & (uint8_t)EVO_STATE_STAGE_MASK);
  if (evolution_level_ready(p)) p.evo_state |= (uint8_t)EVO_STATE_PENDING;
}

#define T0  1700000000u

// A fake ms/epoch clock for the save manager. Binding a NULL ms clock switches
// OFF save_manager.cpp's whole wear-filter branch, so a fixture that does it is
// driving a save_manager the release artefact does not run - tools/check.sh
// fails the build over it, and the reason is the Bug that was destroyed on
// every clean trade before P7-C6 found it.
static uint32_t s_ms    = 10000u;
static uint32_t s_epoch = T0;
static uint32_t fake_ms(void)    { return s_ms; }
static uint32_t fake_epoch(void) { return s_epoch; }

// A bound, empty Box on a device with a real identity - the same fixture
// tests/test_breeding.cpp uses, because the breeding case below drives the REAL
// breed_commit() and that needs a real Box behind it.
static GameState g_state;
static void box_fixture(void)
{
  memset(&g_state, 0, sizeof g_state);
  for (uint8_t i = 0; i < (uint8_t)BOX_SLOTS; ++i) {
    g_state.bugs[i].magic      = (uint16_t)BUG_MAGIC;
    g_state.bugs[i].layout_ver = (uint8_t)BUG_LAYOUT_VER;
  }
  g_state.box.magic           = (uint16_t)BOX_MAGIC;
  g_state.box.active_slot     = (uint8_t)BOX_ACTIVE_NONE;
  g_state.box.next_id_counter = 1;
  g_state.cfg.device_id       = 0xB0FFE5C5u;
  box_bind(g_state);
}
static Genome seeded_genesis(uint32_t seed)
{
  genome_seed(seed);
  return genome_genesis();
}
static BugInstance* make_parent(uint8_t species_id, uint8_t level, const Genome& g)
{
  const uint8_t slot = box_new_bug(species_id, level, (uint8_t)ORIGIN_WILD,
                                      g, 0x5EEDu + species_id, T0);
  if (slot == (uint8_t)BOX_SLOT_NONE) return nullptr;
  return box_slot(slot);
}

// =============================================================================
//  1. THE TIMER REACHES THE FIRMWARE
//
//  cor_expire() had NO CALLER in Errata/src at 37511d5 - measured by grep,
//  and recorded as the first finding of the survey that scoped this chunk. The
//  24 h deadline was written by the encounter and read by nobody, so the status
//  was permanent until an Antivirus cleared it, while tools/content/verify.py's
//  "corruption clears by timer" check passed over the JSON the whole time.
//
//  cor_service() is the walk app/app.cpp now performs once a second. Testing it
//  here is not the same as testing that app.cpp calls it - app.cpp cannot link
//  on the host - and the gate in tools/check.sh is what covers the call site.
// =============================================================================
TEST(the_timer_walk_expires_every_due_slot_and_leaves_the_rest_alone) {
  BugInstance box[BOX_SLOTS];
  memset(box, 0, sizeof box);

  // Slot 0: due in an hour.   Slot 1: due already.   Slot 2: not corrupted.
  // Slot 3: due already.      Slot 4: empty.         Slot 5: due exactly now.
  mk_bug(box[0], 1, 5, 0x1001u);
  mk_bug(box[1], 2, 5, 0x1002u);
  mk_bug(box[2], 3, 5, 0x1003u);
  mk_bug(box[3], 4, 5, 0x1004u);
  mk_bug(box[5], 6, 5, 0x1006u);

  CHECK(cor_apply(box[0], T0 + 3600u, (uint8_t)CAL_USER));   // due at T0+90000
  CHECK(cor_apply(box[1], T0 - (uint32_t)CORRUPT_DURATION_S - 1u, (uint8_t)CAL_USER));
  CHECK(cor_apply(box[3], T0 - (uint32_t)CORRUPT_DURATION_S - 5u, (uint8_t)CAL_USER));
  CHECK(cor_apply(box[5], T0 - (uint32_t)CORRUPT_DURATION_S, (uint8_t)CAL_USER));

  const uint8_t ended = cor_service(box, (uint8_t)BOX_SLOTS, T0, (uint8_t)CAL_USER);
  // THREE, NOT "SOME". Two overdue plus the one due on this exact second - the
  // boundary cor_expire() defines as `now >= until`.
  CHECK_EQ((int)ended, 3);
  CHECK(cor_is_corrupted(box[0]));                     // an hour still to run
  CHECK(!cor_is_corrupted(box[1]));
  CHECK(!cor_is_corrupted(box[2]));                    // was never corrupted
  CHECK(!cor_is_corrupted(box[3]));
  CHECK(!cor_is_corrupted(box[5]));
  // The deadline goes with the bit; nothing is left to re-fire.
  CHECK_EQ(box[1].corrupt_until_epoch, 0u);
  CHECK_EQ(box[5].corrupt_until_epoch, 0u);
  CHECK(box[0].corrupt_until_epoch > T0);

  // A SECOND PASS ON THE SAME SECOND REPORTS NOTHING, which is what makes the
  // return value usable as "did anything change, do I owe a save".
  CHECK_EQ((int)cor_service(box, (uint8_t)BOX_SLOTS, T0, (uint8_t)CAL_USER), 0);

  // ...and the survivor ends on its own second, not before.
  CHECK_EQ((int)cor_service(box, (uint8_t)BOX_SLOTS, T0 + 90000u - 1u,
                            (uint8_t)CAL_USER), 0);
  CHECK(cor_is_corrupted(box[0]));
  CHECK_EQ((int)cor_service(box, (uint8_t)BOX_SLOTS, T0 + 90000u,
                            (uint8_t)CAL_USER), 1);
  CHECK(!cor_is_corrupted(box[0]));
}

TEST(the_timer_walk_refuses_an_untrustworthy_clock_and_a_null_box) {
  BugInstance box[2];
  memset(box, 0, sizeof box);
  mk_bug(box[0], 1, 5, 0x2001u);
  CHECK(cor_apply(box[0], T0, (uint8_t)CAL_USER));

  // An uptime estimate would expire a real 24 h deadline in seconds. The whole
  // walk refuses, exactly as cor_expire() does one slot at a time.
  CHECK_EQ((int)cor_service(box, 2u, 0xFFFFFFFFu, (uint8_t)CAL_UNSET), 0);
  CHECK(cor_is_corrupted(box[0]));
  CHECK_EQ((int)cor_service(nullptr, 2u, T0 + 999999u, (uint8_t)CAL_USER), 0);
  // POSITIVE CONTROL: the same second with a trustworthy clock does end it, so
  // the two refusals above are refusals and not a walk that never works.
  CHECK_EQ((int)cor_service(box, 2u, T0 + (uint32_t)CORRUPT_DURATION_S,
                            (uint8_t)CAL_USER), 1);
  CHECK(!cor_is_corrupted(box[0]));
}

// =============================================================================
//  2. THE GLITCH IS BOUNDED
//
//  THE INSTRUMENT. A painter that XORs each returned row into the host
//  framebuffer, plus a recorder that watches for a lit pixel outside the ink
//  rectangle. Two independent bounds are checked at once:
//
//    * THE PANEL, by tests/fakes/gfx_fb.cpp's own fb_oob() - the recorder every
//      screen golden already uses. A row that left 0..127 / 0..63 is counted
//      there and named by fb_oob_first().
//    * THE BODY, by cfx_oob() below. This is the one the brief is about: the
//      glitch may not write outside the SPRITE RECT, which is a much smaller
//      box than the panel and is the thing that protects the HUD badge columns,
//      the floor line and the two shadow rows.
//
//  IT IS MEASURED ON PIXELS AND NOT ON THE STRUCT FIELDS. A test that read
//  back row.x and row.w and checked them against the rectangle would be reading
//  the same five numbers the clamp just wrote, which is how a bound comes to be
//  asserted against itself. So the rows are PAINTED, and every pixel that
//  differs from a control render of the same body is required to be inside the
//  rectangle.
// =============================================================================
static const uint8_t CFX_TEST_BAYER[16] = {
  0,  8,  2, 10,
  12, 4, 14,  6,
  3, 11,  1,  9,
  15, 7, 13,  5
};

// Paint one CfxRow the way ui/petfx.cpp does: an XOR dither of `level`/16 with
// the Bayer matrix shifted by `phase`. This mirrors render.cpp:743 (and the
// identical FB_BAYER walk in tests/fakes/gfx_fb.cpp) rather than calling it,
// because rd_dither_rect_phase() lives behind U8g2lib.h and gfx_dither_rect()
// has no phase argument.
//
// THE DUPLICATION IS DELIBERATE AND ITS SCOPE IS SMALL: the PATTERN is
// render.cpp's, already shipped and already exercised by every screen golden,
// and it is reproduced here only so the dump in tests/tools/corrupt_view.cpp
// looks like the panel. The property under test is the GEOMETRY, which is
// shipped code in ui/corrupt_fx.cpp and is not reproduced anywhere.
static void cfx_paint_row(const CfxRow& r)
{
  const uint8_t dx = (uint8_t)(r.phase & 3u);
  const uint8_t dy = (uint8_t)((r.phase >> 2) & 3u);
  gfx_color(GFX_XOR);
  for (int16_t i = 0; i < (int16_t)r.w; ++i) {
    const int16_t px = (int16_t)(r.x + i);
    if (CFX_TEST_BAYER[((((r.y + dy) & 3) * 4) + ((px + dx) & 3))] < r.level)
      gfx_pixel(px, r.y);
  }
  gfx_color(GFX_DRAW);
}

// A stand-in body: a solid block filling the rectangle, so the XOR has ink to
// eat as well as background to light. What is drawn matters only for the
// control diff; the rectangle is what the assertion is about.
static void cfx_paint_body(const CfxRect& ink)
{
  gfx_color(GFX_DRAW);
  gfx_fill(ink.x0, ink.y0, (int16_t)(ink.x1 - ink.x0 + 1),
           (int16_t)(ink.y1 - ink.y0 + 1));
}

// The out-of-bounds recorder for the BODY rectangle. Counts every pixel that
// changed outside it and remembers the first, so a failure names a coordinate
// rather than only a count.
struct CfxOob {
  uint32_t n;
  int      x, y;
};
static CfxOob cfx_diff_outside(const uint8_t base[FB_H][FB_W], const CfxRect& ink)
{
  CfxOob o = { 0u, -1, -1 };
  for (int y = 0; y < FB_H; ++y) {
    for (int x = 0; x < FB_W; ++x) {
      if (fb_get(x, y) == (int)base[y][x]) continue;
      if (x >= ink.x0 && x <= ink.x1 && y >= ink.y0 && y <= ink.y1) continue;
      if (o.n == 0u) { o.x = x; o.y = y; }
      ++o.n;
    }
  }
  return o;
}
static void cfx_snapshot(uint8_t out[FB_H][FB_W])
{
  for (int y = 0; y < FB_H; ++y)
    for (int x = 0; x < FB_W; ++x) out[y][x] = (uint8_t)fb_get(x, y);
}

TEST(the_glitch_never_writes_outside_the_sprite_rect) {
  // The real geometry petfx hands over: a 24x24 body standing on
  // PETFX_FLOOR_Y - 1 = 51, inside the stage columns 14..113. The sweep walks
  // every x the widest body can occupy and both the tallest and the shortest
  // ink boxes the atlas produces, over 4,096 time slots each.
  static uint8_t base[FB_H][FB_W];
  uint32_t painted_total = 0;
  uint32_t slots_on      = 0;

  const int16_t heights[] = { 1, 2, 3, 24, 40 };
  for (size_t hi = 0; hi < sizeof heights / sizeof heights[0]; ++hi) {
    for (int16_t x0 = 14; x0 <= 90; x0 += 19) {
      CfxRect ink;
      ink.x0 = x0;
      ink.x1 = (int16_t)(x0 + 23);
      ink.y1 = 51;
      ink.y0 = (int16_t)(52 - heights[hi]);
      if (ink.y0 < 9) ink.y0 = 9;              // SPRITE_AREA_Y

      for (uint32_t slot = 0; slot < 4096u; ++slot) {
        const uint32_t now = slot * (uint32_t)CFX_GLITCH_SLOT_MS + 17u;
        const uint32_t seed = 0xA5C3F17Bu ^ (uint32_t)(x0 * 2654435761u) ^ slot;
        if (!cfx_glitch_on(now, seed)) continue;
        ++slots_on;

        fb_reset();
        cfx_paint_body(ink);
        cfx_snapshot(base);

        CfxRow rows[CFX_ROWS_MAX];
        const uint8_t n = cfx_rows(ink, now, seed, rows);
        CHECK(n <= (uint8_t)CFX_ROWS_MAX);
        for (uint8_t i = 0; i < n; ++i) cfx_paint_row(rows[i]);
        painted_total += n;

        // THE PANEL BOUND, through the recorder every screen golden uses.
        if (fb_oob() != 0u) {
          fprintf(stderr, "  first panel escape: %s\n", fb_oob_first());
          CHECK_EQ((long long)fb_oob(), 0LL);
          return;
        }
        // THE BODY BOUND. This is the assertion the brief's mutation is aimed
        // at, and it is measured on the pixels that actually changed.
        const CfxOob o = cfx_diff_outside(base, ink);
        if (o.n != 0u) {
          fprintf(stderr,
                  "  glitch escaped the sprite rect: %u px, first at (%d,%d), "
                  "rect x %d..%d y %d..%d, slot %u\n",
                  (unsigned)o.n, o.x, o.y, ink.x0, ink.x1, ink.y0, ink.y1,
                  (unsigned)slot);
          CHECK_EQ((long long)o.n, 0LL);
          return;
        }
      }
    }
  }

  // POSITIVE CONTROLS, or the containment above is satisfied by a glitch that
  // draws nothing at all - which is exactly the shape of the vacuous assertions
  // the phase-8 review found five times.
  CHECK(slots_on > 200u);              // the gate really lit
  CHECK(painted_total >= slots_on);    // every lit slot produced at least a row
}

TEST(the_glitch_actually_changes_pixels_inside_the_rect) {
  // The control for the case above. If cfx_rows() returned rows that painted
  // nothing, every containment assertion in this file would pass.
  static uint8_t base[FB_H][FB_W];
  CfxRect ink = { 40, 28, 63, 51 };
  uint32_t lit_slots = 0, changed_total = 0, min_changed = 0xFFFFFFFFu;

  for (uint32_t slot = 0; slot < 2048u; ++slot) {
    const uint32_t now = slot * (uint32_t)CFX_GLITCH_SLOT_MS;
    if (!cfx_glitch_on(now, 0xBEEF0001u)) continue;
    fb_reset();
    cfx_paint_body(ink);
    cfx_snapshot(base);
    CfxRow rows[CFX_ROWS_MAX];
    const uint8_t n = cfx_rows(ink, now, 0xBEEF0001u, rows);
    CHECK(n >= 1u);
    for (uint8_t i = 0; i < n; ++i) cfx_paint_row(rows[i]);

    uint32_t changed = 0;
    for (int y = ink.y0; y <= ink.y1; ++y)
      for (int x = ink.x0; x <= ink.x1; ++x)
        if (fb_get(x, y) != (int)base[y][x]) ++changed;
    if (changed < min_changed) min_changed = changed;
    changed_total += changed;
    ++lit_slots;
  }
  CHECK(lit_slots > 100u);
  // THE FLOOR IS DERIVED, NOT OBSERVED. The thinnest row cfx_rows() can emit is
  // half the body (12 px on a 24 px ink box), and the sparsest row of the Bayer
  // matrix at level 7 keeps ONE column in four - {15,7,13,5} has only 5 under 7
  // - so 12/4 = 3 pixels is the least a legal glitch can change. Measured
  // minimum over 271 lit slots: exactly 3, which is the floor being reached
  // rather than a number chosen to fit. A mutation that emitted a zero-width
  // row, or none, lands under it.
  CHECK(min_changed >= 3u);
  CHECK(min_changed <= 3u * 24u);
  // ...and typically much more than the floor: three rows of a 24 px body at
  // 7/16 average about 15 changed pixels.
  CHECK_NEAR(changed_total / lit_slots, 15u, 10u);
}

TEST(a_degenerate_or_inverted_rectangle_produces_no_rows_at_all) {
  CfxRow rows[CFX_ROWS_MAX];
  // Inverted on x, on y, and on both. A clamped guess here would be a row
  // drawn at a coordinate the caller never offered.
  const CfxRect bad[] = {
    { 50, 30, 49, 40 }, { 50, 30, 60, 29 }, { 50, 30, 49, 29 },
    { 0, 0, -1, -1 },   { -5, -5, -6, -6 }
  };
  for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i)
    for (uint32_t slot = 0; slot < 64u; ++slot)
      CHECK_EQ((int)cfx_rows(bad[i], slot * 60u, 0x1234u, rows), 0);

  // A ONE-PIXEL rectangle is legal and must produce exactly that pixel's row -
  // the narrowest case the clamp has to get right.
  const CfxRect one = { 64, 32, 64, 32 };
  uint32_t seen = 0;
  for (uint32_t slot = 0; slot < 256u; ++slot) {
    const uint8_t n = cfx_rows(one, slot * 60u, 0x99u, rows);
    CHECK_EQ((int)n, 1);
    for (uint8_t i = 0; i < n; ++i) {
      CHECK_EQ((int)rows[i].x, 64);
      CHECK_EQ((int)rows[i].y, 32);
      CHECK_EQ((int)rows[i].w, 1);
      ++seen;
    }
  }
  CHECK_EQ((long long)seen, 256LL);

  // A NULL destination writes nothing and says so.
  const CfxRect ok = { 10, 10, 40, 40 };
  CHECK_EQ((int)cfx_rows(ok, 600u, 1u, nullptr), 0);
}

TEST(the_glitch_gate_lights_about_one_slot_in_eight_and_is_not_a_constant) {
  // "1 in 8 frames" is the plan's wording. It is measured as a RATE over many
  // slots and many pets rather than pinned to a pattern, because the gate is a
  // hash on purpose - an irregular stutter, not a metronome.
  uint32_t on = 0, total = 0;
  uint32_t any_on = 0, any_off = 0;
  for (uint32_t seed = 1; seed <= 64u; ++seed) {
    uint32_t seed_on = 0;
    for (uint32_t slot = 0; slot < 1024u; ++slot) {
      const uint8_t v = cfx_glitch_on(slot * (uint32_t)CFX_GLITCH_SLOT_MS, seed);
      CHECK(v == 0u || v == 1u);
      seed_on += v;
      ++total;
    }
    on += seed_on;
    if (seed_on > 0u)     ++any_on;
    if (seed_on < 1024u)  ++any_off;
  }
  CHECK_EQ((long long)total, 64LL * 1024LL);
  // 1/8 of 65,536 is 8,192. A 12 % band leaves room for a hash that is not a
  // perfect divider and still fails a gate that lit every slot or none.
  CHECK_NEAR((long long)on, 8192LL, 1000LL);
  CHECK_EQ((long long)any_on, 64LL);    // no pet is permanently clean
  CHECK_EQ((long long)any_off, 64LL);   // and none is permanently glitched

  // EVERY MILLISECOND OF A SLOT AGREES, which is what makes the effect last a
  // whole slot rather than flickering within one frame.
  for (uint32_t slot = 0; slot < 200u; ++slot) {
    const uint32_t base_ms = slot * (uint32_t)CFX_GLITCH_SLOT_MS;
    const uint8_t want = cfx_glitch_on(base_ms, 7u);
    for (uint32_t k = 0; k < (uint32_t)CFX_GLITCH_SLOT_MS; ++k)
      CHECK_EQ((int)cfx_glitch_on(base_ms + k, 7u), (int)want);
    CHECK_EQ(cfx_glitch_slot(base_ms + (uint32_t)CFX_GLITCH_SLOT_MS - 1u), slot);
  }

  // TWO PETS ON ONE DEVICE DO NOT GLITCH TOGETHER. Without the seed term the
  // whole Box would stutter in unison, which reads as a display fault.
  uint32_t differ = 0;
  for (uint32_t slot = 0; slot < 4096u; ++slot) {
    const uint32_t ms = slot * (uint32_t)CFX_GLITCH_SLOT_MS;
    if (cfx_glitch_on(ms, 0x11111111u) != cfx_glitch_on(ms, 0x22222222u)) ++differ;
  }
  CHECK(differ > 600u);
}

TEST(the_glitch_is_deterministic_in_its_arguments) {
  // The same pet on the same slot draws the same noise on every device and on
  // every boot: ui/corrupt_fx.h's whole claim to being pure.
  CfxRow a[CFX_ROWS_MAX], b[CFX_ROWS_MAX];
  const CfxRect ink = { 20, 20, 43, 51 };
  for (uint32_t slot = 0; slot < 512u; ++slot) {
    const uint32_t ms = slot * (uint32_t)CFX_GLITCH_SLOT_MS + 3u;
    const uint8_t n1 = cfx_rows(ink, ms, 0xC0FFEEu, a);
    const uint8_t n2 = cfx_rows(ink, ms + 11u, 0xC0FFEEu, b);   // same slot
    CHECK_EQ((int)n1, (int)n2);
    for (uint8_t i = 0; i < n1; ++i) {
      CHECK_EQ((int)a[i].x, (int)b[i].x);
      CHECK_EQ((int)a[i].y, (int)b[i].y);
      CHECK_EQ((int)a[i].w, (int)b[i].w);
      CHECK_EQ((int)a[i].level, (int)b[i].level);
      CHECK_EQ((int)a[i].phase, (int)b[i].phase);
    }
  }
  // ...and the next slot is a different drawing, or the noise is a still image.
  uint32_t moved = 0;
  for (uint32_t slot = 0; slot < 512u; ++slot) {
    const uint8_t n1 = cfx_rows(ink, slot * 60u, 0xC0FFEEu, a);
    const uint8_t n2 = cfx_rows(ink, (slot + 1u) * 60u, 0xC0FFEEu, b);
    if (n1 != n2 || memcmp(a, b, sizeof a) != 0) ++moved;
  }
  CHECK(moved > 480u);

  // THE LEVEL IS ODD. render.h measures the phase as buying 16 distinct looks
  // at an odd level and 2 at RD_D50; an even level here would make the 16
  // phases collapse and the noise flicker between a pair of fixed patterns.
  for (uint32_t slot = 0; slot < 256u; ++slot) {
    const uint8_t n = cfx_rows(ink, slot * 60u, 5u, a);
    for (uint8_t i = 0; i < n; ++i) {
      CHECK_EQ((int)(a[i].level & 1u), 1);
      CHECK(a[i].level >= 1u && a[i].level <= 16u);
      CHECK(a[i].phase <= 15u);
    }
  }
}

// =============================================================================
//  3. THE BATTLE MODIFIER
// =============================================================================
static BattleReject mk_battle(BattleState& st, const BugInstance& a,
                              const BugInstance& b, uint32_t seed)
{
  BattleSetup s;
  battle_setup_clear(s);
  s.seed     = seed;
  s.count[0] = 1u;
  s.count[1] = 1u;
  s.member[0][0] = a;
  s.member[1][0] = b;
  return battle_init(st, s);
}

TEST(a_corrupted_bug_enters_battle_corrupted_and_a_clean_one_does_not) {
  BugInstance clean, sick;
  mk_bug(clean, 1, 15, 0xC0u);
  mk_bug(sick,  1, 15, 0xC1u);
  CHECK(cor_apply(sick, T0, (uint8_t)CAL_USER));
  CHECK(cor_is_corrupted(sick));

  BattleState st;
  CHECK_EQ((int)mk_battle(st, sick, clean, 0x1234u), (int)BR_OK);
  const BattleCombatant* me  = battle_combatant(st, 0u, 0u);
  const BattleCombatant* foe = battle_combatant(st, 1u, 0u);
  CHECK(me != nullptr && foe != nullptr);

  // THE FIELD, AND THE NUMBER. Before P9-C5 this was 0 on both sides: the
  // engine's +1/-1 existed and nothing ever set the flag that reaches it.
  CHECK_EQ((int)me->corrupt_left, (int)CORRUPT_BATTLE_ROUNDS);
  CHECK_EQ((int)foe->corrupt_left, 0);
  CHECK_EQ((int)CORRUPT_BATTLE_ROUNDS, (int)BATTLE_MAX_ROUNDS);

  // THE EFFECT, MEASURED AGAINST THE CONTROL. Same species, same level, same
  // derivation - so the only difference between the two is the status bit.
  CHECK_EQ((int)battle_stat_eff(*me, (uint8_t)BSTAT_ATK),
           (int)battle_stat_eff(*foe, (uint8_t)BSTAT_ATK) + (int)CORRUPT_BATTLE_ATK_STAGE);
  CHECK_EQ((int)battle_stat_eff(*me, (uint8_t)BSTAT_DEF),
           (int)battle_stat_eff(*foe, (uint8_t)BSTAT_DEF) + (int)CORRUPT_BATTLE_DEF_STAGE);
  // SPD IS UNTOUCHED. Section 55 names two stages, not three, and a silent
  // third would move every speed order in the game.
  CHECK_EQ((int)battle_stat_eff(*me, (uint8_t)BSTAT_SPD),
           (int)battle_stat_eff(*foe, (uint8_t)BSTAT_SPD));
  CHECK_EQ((int)CORRUPT_BATTLE_ATK_STAGE, 1);
  CHECK_EQ((int)CORRUPT_BATTLE_DEF_STAGE, -1);
}

TEST(the_battle_modifier_does_not_stack_across_rounds) {
  // THE BOUND THE BRIEF NAMES. corrupt_left ticks down once per round in step
  // 7, and battle_stat_eff() reads it as a BOOLEAN - so the effect must be the
  // same +1/-1 on round 1 and on round 40, never +2 or +40.
  BugInstance sick, foe;
  mk_bug(sick, 1, 15, 0xD0u);
  mk_bug(foe,  4, 15, 0xD1u);
  CHECK(cor_apply(sick, T0, (uint8_t)CAL_USER));

  BattleState st;
  CHECK_EQ((int)mk_battle(st, sick, foe, 0x5A5Au), (int)BR_OK);

  BattleState ctl;
  BugInstance clean = sick;
  CHECK(cor_clear(clean));
  CHECK_EQ((int)mk_battle(ctl, clean, foe, 0x5A5Au), (int)BR_OK);

  const BattleCombatant* c  = battle_combatant(st, 0u, 0u);
  const BattleCombatant* cc = battle_combatant(ctl, 0u, 0u);
  CHECK(c != nullptr && cc != nullptr);
  const int atk0 = (int)battle_stat_eff(*cc, (uint8_t)BSTAT_ATK);
  const int def0 = (int)battle_stat_eff(*cc, (uint8_t)BSTAT_DEF);

  uint8_t seen_rounds = 0;
  for (uint16_t r = 0; r < 40u; ++r) {
    battle_s7_process_status(st, nullptr);
    c = battle_combatant(st, 0u, 0u);
    CHECK(c != nullptr);
    if (c->corrupt_left == 0u) break;
    ++seen_rounds;
    // ONE STAGE, EVERY ROUND. A `+=` anywhere on this path would show up here
    // as a drift and nowhere else.
    CHECK_EQ((int)battle_stat_eff(*c, (uint8_t)BSTAT_ATK), atk0 + 1);
    CHECK_EQ((int)battle_stat_eff(*c, (uint8_t)BSTAT_DEF), def0 - 1);
  }
  CHECK_EQ((int)seen_rounds, 40);
  // The counter really did move, so the loop above was not 40 passes over a
  // frozen field.
  CHECK_EQ((int)c->corrupt_left, (int)CORRUPT_BATTLE_ROUNDS - 40);
}

TEST(infectar_cannot_stack_on_top_of_a_stored_corruption) {
  // Attack 12 Infectar writes the SAME field (`if (d > corrupt_left)`), which
  // is what makes the two sources one fact. If P9-C5 had added a second flag,
  // an infected corrupted Bug would be at +2 ATK / -2 DEF here.
  BugInstance sick, foe;
  mk_bug(sick, 1, 15, 0xE0u);
  mk_bug(foe,  4, 15, 0xE1u);
  CHECK(cor_apply(sick, T0, (uint8_t)CAL_USER));

  BattleState st;
  CHECK_EQ((int)mk_battle(st, sick, foe, 0x77u), (int)BR_OK);
  // battle_combatant() answers const, and this case has to WRITE the field the
  // engine's ATK_EFF_EFF_CORRUPT arm writes. The cast is confined to this one
  // case and named: what is being modelled is an infecting hit landing, not a
  // test reaching into state it has no business in.
  BattleCombatant* c = const_cast<BattleCombatant*>(battle_combatant(st, 0u, 0u));
  CHECK(c != nullptr);
  if (c == nullptr) return;
  const int atk_before = (int)battle_stat_eff(*c, (uint8_t)BSTAT_ATK);
  const uint8_t left_before = c->corrupt_left;

  // Whatever an infecting hit would set, applied by hand at the field the
  // engine's ATK_EFF_EFF_CORRUPT arm writes: a shorter duration must not
  // shorten a longer one, and no duration may raise the stage twice.
  for (uint8_t d = 1; d <= 8u; ++d) {
    if (d > c->corrupt_left) c->corrupt_left = d;
    CHECK_EQ((int)c->corrupt_left, (int)left_before);
    CHECK_EQ((int)battle_stat_eff(*c, (uint8_t)BSTAT_ATK), atk_before);
  }

  // CLEANSE removes it for the rest of the fight, and the effect goes with it.
  c->corrupt_left = 0u;
  CHECK_EQ((int)battle_stat_eff(*c, (uint8_t)BSTAT_ATK),
           atk_before - (int)CORRUPT_BATTLE_ATK_STAGE);
}

TEST(the_battle_modifier_does_not_stack_across_a_save_or_a_second_battle) {
  // "May not stack across a save" in the only form it can take: the ENGINE
  // never writes back, so a Bug that fights, is stored, is loaded and fights
  // again is in exactly the same state on both occasions.
  BugInstance sick;
  mk_bug(sick, 1, 15, 0xF0u);
  BugInstance foe;
  mk_bug(foe, 4, 15, 0xF1u);
  CHECK(cor_apply(sick, T0, (uint8_t)CAL_USER));
  const BugInstance before = sick;

  BattleState st;
  for (int pass = 0; pass < 3; ++pass) {
    CHECK_EQ((int)mk_battle(st, sick, foe, 0x11u + (uint32_t)pass), (int)BR_OK);
    for (uint16_t r = 0; r < 5u; ++r) battle_s7_process_status(st, nullptr);
    // THE STORED BUG IS UNTOUCHED BY THE FIGHT - byte for byte, deadline
    // included. A battle that could extend or clear the 24 h status would be a
    // second writer of a fact game/corruption.cpp owns.
    CHECK_EQ(memcmp(&sick, &before, sizeof sick), 0);
    const BattleCombatant* c = battle_combatant(st, 0u, 0u);
    CHECK(c != nullptr);
    // ...so every battle starts from the same number, never from the last
    // fight's leftover.
    CHECK_EQ((int)c->corrupt_left, (int)CORRUPT_BATTLE_ROUNDS - 5);
  }

  // A ROUND TRIP THROUGH THE SAVE RECORD keeps the bit and the deadline, and
  // the next battle_init() derives the same number from them.
  BugInstance reloaded;
  memcpy(&reloaded, &sick, sizeof reloaded);
  CHECK_EQ((int)validate_bug(reloaded), (int)VR_OK);
  CHECK(cor_is_corrupted(reloaded));
  CHECK_EQ(reloaded.corrupt_until_epoch, sick.corrupt_until_epoch);
  CHECK_EQ((int)mk_battle(st, reloaded, foe, 0x22u), (int)BR_OK);
  CHECK_EQ((int)battle_combatant(st, 0u, 0u)->corrupt_left,
           (int)CORRUPT_BATTLE_ROUNDS);
}

TEST(a_linked_battle_is_uncorrupted_on_both_sides_and_cannot_desync) {
  // networking/session.cpp's load_own_team() decodes even the LOCAL team
  // through pbw_decode(), so both endpoints' BattleSetups are byte-identical -
  // and BUGW_STATUS_MASK strips PBS_CORRUPTED because it is EVOC_CORRUPTED's
  // input and a forged bit would buy the receiver a free evolution. The
  // consequence of reading the status in battle_init() is therefore that a
  // LINKED battle carries no corruption AT ALL, symmetrically. That is the
  // property that stops this chunk introducing a round-1 desync, and it is
  // pinned here rather than left in a comment.
  BugInstance sick;
  mk_bug(sick, 1, 15, 0x0BADCAFEu);
  CHECK(cor_apply(sick, T0, (uint8_t)CAL_USER));

  uint8_t rec[BUGW_BYTES];
  pbw_encode(sick, rec);
  BugInstance wire;
  CHECK_EQ((int)pbw_decode(rec, wire), (int)VR_OK);
  CHECK(!cor_is_corrupted(wire));                       // the mask did its job
  CHECK_EQ(wire.corrupt_until_epoch, 0u);

  BugInstance foe;
  mk_bug(foe, 4, 15, 0xFEEDu);
  uint8_t frec[BUGW_BYTES];
  pbw_encode(foe, frec);
  BugInstance fwire;
  CHECK_EQ((int)pbw_decode(frec, fwire), (int)VR_OK);

  // BOTH ENDPOINTS: each builds the same setup from the same decoded records.
  BattleState a, b;
  CHECK_EQ((int)mk_battle(a, wire, fwire, 0xABCDu), (int)BR_OK);
  CHECK_EQ((int)mk_battle(b, wire, fwire, 0xABCDu), (int)BR_OK);
  CHECK_EQ((int)battle_combatant(a, 0u, 0u)->corrupt_left, 0);
  CHECK_EQ((int)battle_combatant(b, 0u, 0u)->corrupt_left, 0);
  CHECK_EQ(battle_state_hash(a), battle_state_hash(b));

  // AND THE ROUND-1 BARRIER WOULD HAVE CAUGHT IT: the same fight against the
  // UNDECODED local record hashes differently, which is what a peer that let
  // the bit through would have produced.
  BattleState c;
  CHECK_EQ((int)mk_battle(c, sick, fwire, 0xABCDu), (int)BR_OK);
  CHECK(battle_state_hash(c) != battle_state_hash(a));
}

TEST(the_engine_version_moved_with_what_battle_init_accepts) {
  // game/battle.h: "BATTLE_ENGINE_VER MUST be bumped by any commit that changes
  // what a round does, or what battle_init() will accept". P9-C5 changed the
  // latter, so a setup stamped with the old version must now be refused rather
  // than reproducing a different fight quietly.
  CHECK_EQ((int)BATTLE_ENGINE_VER, 3);
  BugInstance a, b;
  mk_bug(a, 1, 15, 0x1u);
  mk_bug(b, 4, 15, 0x2u);
  BattleSetup s;
  battle_setup_clear(s);
  s.count[0] = 1u; s.count[1] = 1u;
  s.member[0][0] = a; s.member[1][0] = b;
  s.engine_ver = 2u;
  BattleState st;
  CHECK_EQ((int)battle_init(st, s), (int)BR_VERSION_MISMATCH);
}

// =============================================================================
//  4. THE ALTERED IDLE ANIMATION
// =============================================================================
TEST(the_corrupted_behaviour_row_replaces_the_gene_and_never_outlives_it) {
  // "The idle animation may not outlive the status" is the bound. It is a
  // property of ONE expression - ui/petfx.cpp's pf_derive() reads nothing else
  // - so it is asserted over the whole input space rather than sampled.
  for (unsigned t = 0; t < 256u; ++t) {
    const uint8_t row_clean = cfx_temper_index((uint8_t)t, 0u);
    // Every genome temperament keeps its own row while the status is clear...
    if (t < (unsigned)CFX_TEMPER_CORRUPT) CHECK_EQ((int)row_clean, (int)t);
    else                        CHECK_EQ((int)row_clean, (int)CFX_TEMPER_FALLBACK);
    CHECK(row_clean < (uint8_t)CFX_TEMPER_ROWS);
    CHECK(row_clean != (uint8_t)CFX_TEMPER_CORRUPT);   // no gene selects it

    // ...and every one of them is overridden while it is set, whatever else is
    // true of the pet.
    for (unsigned c = 1; c < 256u; ++c) {
      const uint8_t row = cfx_temper_index((uint8_t)t, (uint8_t)c);
      CHECK_EQ((int)row, (int)CFX_TEMPER_CORRUPT);
      CHECK(row < (uint8_t)CFX_TEMPER_ROWS);
    }
  }
  // THE ROW IS ONE PAST THE LAST TEMPERAMENT, which is what ui/petfx.cpp's two
  // static_asserts hold PF_TEMPER to. Add a fifth genome temperament and the
  // BUILD fails there rather than the corrupted pet inheriting its timings.
  CHECK_EQ((int)CFX_TEMPER_CORRUPT, (int)TEMPER_COUNT);
  CHECK_EQ((int)CFX_TEMPER_ROWS, (int)TEMPER_COUNT + 1);
  // The out-of-range fold is TRANQUILO, which is where pf_derive() sent it
  // before the move. petfx.cpp static_asserts the same pair; this is the half
  // that says so in a test rather than only in a build.
  CHECK_EQ((int)CFX_TEMPER_FALLBACK, (int)TEMPER_TRANQUILO);
}

// =============================================================================
//  5. EVO_COND_CORRUPTED
// =============================================================================
TEST(corruption_gates_exactly_two_families_and_refuses_without_the_flag) {
  // Spec section 55 says "a unique evolution in at most 2 families", and
  // tools/content/verify.py asserts 1..2 over the JSON. This asserts it over
  // the EMITTED table, which is what the firmware actually reads.
  uint8_t rows = 0;
  uint8_t fam[EVOLUTION_RULES_COUNT];
  memset(fam, 0, sizeof fam);

  for (uint8_t i = 0; i < (uint8_t)EVOLUTION_RULES_COUNT; ++i) {
    const EvolutionRule* r = evolution_rule_at(i);
    CHECK(r != nullptr);
    if (r->cond != (uint8_t)EVOC_CORRUPTED) continue;
    const SpeciesDef* from = species_get(r->species);
    const SpeciesDef* to   = species_get(r->target);
    CHECK(from != nullptr && to != nullptr);
    // A UNIQUE evolution: it goes somewhere, inside its own family, one stage
    // up. A corruption rule that pointed at another family would take the
    // creature out of its own line.
    CHECK_EQ((int)to->family, (int)from->family);
    CHECK_EQ((int)to->stage, (int)from->stage + 1);
    CHECK(r->target != r->species);
    for (uint8_t k = 0; k < rows; ++k)
      CHECK(fam[k] != from->family);            // one row per family, no more
    fam[rows] = from->family;
    ++rows;
  }
  CHECK(rows >= 1u);
  CHECK(rows <= 2u);
  CHECK_EQ((int)rows, 2);      // both pack rows ship since P9-C3 unclamped the roster

  // THE GATE ITSELF, on every one of them. An unsupplied input REFUSES: a rule
  // that answered "true" for a context nobody filled would evolve a creature on
  // a requirement no code checked.
  for (uint8_t i = 0; i < (uint8_t)EVOLUTION_RULES_COUNT; ++i) {
    const EvolutionRule* r = evolution_rule_at(i);
    if (r == nullptr || r->cond != (uint8_t)EVOC_CORRUPTED) continue;

    BugInstance p;
    mk_bug(p, r->species, r->level, 0x900u + i);

    EvoContext ctx;
    evo_context_clear(ctx);
    CHECK_EQ((int)evolution_ready(p, ctx), 0);           // nothing supplied

    ctx.corrupted = 1u;                                   // supplied but unflagged
    CHECK_EQ((int)evolution_ready(p, ctx), 0);

    ctx.have |= EVOCTX_CORRUPTED;
    ctx.corrupted = 0u;
    CHECK_EQ((int)evolution_ready(p, ctx), 0);           // flagged and false

    ctx.corrupted = 1u;
    CHECK_EQ((int)evolution_level_ready(p), 1);
    CHECK_EQ((int)evolution_ready(p, ctx), 1);           // the one case that passes

    // BELOW THE LEVEL IT STILL REFUSES: corruption is a second condition, not
    // a bypass of the first.
    BugInstance young;
    mk_bug(young, r->species, (uint8_t)(r->level - 1u), 0x910u + i);
    CHECK_EQ((int)evolution_ready(young, ctx), 0);

    // AND IT REALLY EVOLVES, into the species the table names.
    const uint8_t target = r->target;
    CHECK(evolution_apply(p, ctx));
    CHECK_EQ((int)p.species_id, (int)target);
  }
}

TEST(the_corruption_evolution_is_reachable_from_the_status_the_item_clears) {
  // The whole chain in one case: a Bug that catches corruption becomes
  // eligible, an Antivirus takes the eligibility away again, and the deadline
  // does the same thing on its own. This is what "clears by timer OR by a care
  // item" means for the effect that is furthest from the bit.
  const EvolutionRule* rule = nullptr;
  for (uint8_t i = 0; i < (uint8_t)EVOLUTION_RULES_COUNT; ++i) {
    const EvolutionRule* r = evolution_rule_at(i);
    if (r != nullptr && r->cond == (uint8_t)EVOC_CORRUPTED) { rule = r; break; }
  }
  CHECK(rule != nullptr);
  if (rule == nullptr) return;

  BugInstance p;
  mk_bug(p, rule->species, rule->level, 0xA11u);

  EvoContext ctx;
  evo_context_clear(ctx);
  ctx.have |= EVOCTX_CORRUPTED;
  ctx.corrupted = (uint8_t)(cor_is_corrupted(p) ? 1u : 0u);
  CHECK_EQ((int)evolution_ready(p, ctx), 0);

  CHECK(cor_apply(p, T0, (uint8_t)CAL_USER));
  ctx.corrupted = (uint8_t)(cor_is_corrupted(p) ? 1u : 0u);
  CHECK_EQ((int)evolution_ready(p, ctx), 1);

  // THE ITEM ROUTE, through the real inventory effect rather than cor_clear()
  // by hand: item 7 Antivirus is what balance.json's cleared_by_item names.
  Inventory inv;
  inv_begin(inv);
  CHECK_EQ((int)inv_add(inv, 7u, 1u), 1);
  ItemEffect eff;
  memset(&eff, 0, sizeof eff);
  // THE UNCALIBRATED CLOCK IS THE POINT OF THE ARGUMENT: the cure must work on
  // a device that has never been told the date, or the escape hatch is the
  // thing that gets stuck (spec section 47).
  CHECK_EQ((int)inv_use(inv, 7u, &p, 0u, (uint8_t)CAL_UNSET, eff), (int)IU_OK);
  CHECK_EQ((int)(uint8_t)(eff.status_cleared & (uint8_t)PBS_CORRUPTED),
           (int)PBS_CORRUPTED);
  CHECK(!cor_is_corrupted(p));
  CHECK_EQ(p.corrupt_until_epoch, 0u);
  ctx.corrupted = (uint8_t)(cor_is_corrupted(p) ? 1u : 0u);
  CHECK_EQ((int)evolution_ready(p, ctx), 0);

  // THE TIMER ROUTE, on the same Bug, with no item at all.
  CHECK(cor_apply(p, T0, (uint8_t)CAL_USER));
  ctx.corrupted = 1u;
  CHECK_EQ((int)evolution_ready(p, ctx), 1);
  CHECK_EQ((int)cor_service(&p, 1u, T0 + (uint32_t)CORRUPT_DURATION_S,
                            (uint8_t)CAL_USER), 1);
  CHECK(!cor_is_corrupted(p));
  ctx.corrupted = 0u;
  CHECK_EQ((int)evolution_ready(p, ctx), 0);
}

// =============================================================================
//  6. IT NEVER DESTROYS (spec section 55)
//
//  "Corruption is the one mechanic in this game that is SUPPOSED to look like
//  damage, which makes it the one where real damage would hide." So the checks
//  below are about the BUG, not about the effect: every field that is not
//  the status byte and the deadline must be exactly what it was.
// =============================================================================
TEST(corruption_changes_two_fields_and_nothing_else_in_the_record) {
  BugInstance p;
  mk_bug(p, 5, 12, 0xB0B0u);
  p.evolutions = 2u;
  p.care[CARE_HAPPINESS] = 40000;
  const BugInstance before = p;

  CHECK(cor_apply(p, T0, (uint8_t)CAL_USER));

  // Byte-for-byte, with the two fields corruption owns masked out. A memcmp
  // over the whole record is what catches a stray write nobody thought to name.
  BugInstance a = before, b = p;
  a.status = 0u; b.status = 0u;
  a.corrupt_until_epoch = 0u; b.corrupt_until_epoch = 0u;
  CHECK_EQ(memcmp(&a, &b, sizeof a), 0);
  CHECK_EQ((int)(uint8_t)(p.status & (uint8_t)~(uint8_t)PBS_CORRUPTED),
           (int)before.status);
  CHECK_EQ((int)p.hp_cur, (int)before.hp_cur);
  CHECK_EQ((int)p.level, (int)before.level);
  CHECK_EQ((int)p.xp, (int)before.xp);
  CHECK_EQ((int)p.species_id, (int)before.species_id);

  // A CORRUPTED BUG IS STILL A VALID BUG. If it were not, the quarantine
  // in game/validate.cpp would refuse it on the next load and the "signature
  // mechanic" would be a way to lose a creature.
  CHECK_EQ((int)validate_bug(p), (int)VR_OK);

  // ...and the cure puts the record back exactly as it was.
  CHECK(cor_clear(p));
  CHECK_EQ(memcmp(&p, &before, sizeof p), 0);
}

TEST(a_corrupted_bug_survives_a_save_a_trade_and_a_breeding_intact) {
  BugInstance p;
  mk_bug(p, 5, 20, 0x5A1Eu);
  CHECK(cor_apply(p, T0, (uint8_t)CAL_USER));

  // --- SAVED AND RELOADED, THROUGH THE REAL SAVE MANAGER --------------------
  // NOT a memcpy dressed up as a reboot. Both the bit and the deadline are
  // STORED bytes - persistence/save_schema.h carved corrupt_until_epoch out of
  // BugInstance.reserved[12] at P5-C3 with no schema bump - so the thing
  // that could go wrong is precisely that the deadline lands in a byte the
  // writer, the CRC or a migration does not carry. That cannot be seen without
  // driving save_bug_now() and save_load_all() over a real KV store.
  //
  // A LOST DEADLINE IS A DESTROYED BUG IN THE ONLY SENSE SECTION 55 CARES
  // ABOUT: the bit surviving with a zeroed deadline is a corruption that never
  // ends by timer, and the whole "it never destroys" claim rests on it ending.
  kv_mem_reset();
  save_set_clock(&fake_ms, &fake_epoch);
  GameState gs;
  memset(&gs, 0, sizeof gs);
  CHECK_EQ((int)save_load_all(gs), (int)LOAD_FRESH);
  gs.bugs[3] = p;
  bug_seal(gs.bugs[3]);
  // The header has to agree with what is on flash, or save_load_all() HEALS the
  // mask and answers LOAD_RECOVERED_PAIR - a successful recovery, and not the
  // plain read this case wants to be about. app/app.cpp keeps the two in step
  // through game/box.cpp; here it is one bit.
  gs.box.slot_mask   = (uint16_t)(1u << 3);
  gs.box.active_slot = 3u;
  // TWICE, so BOTH copies of the pair carry the deadline. save_bug_now()
  // writes "the inactive copy of the pair" (save_manager.h), so one call leaves
  // the other half empty and the next load answers LOAD_RECOVERED_PAIR - a
  // successful recovery, but not the plain read this case wants to be about.
  CHECK(save_bug_now(3u, gs.bugs[3]));
  CHECK(save_bug_now(3u, gs.bugs[3]));
  CHECK(save_box_header(gs.box));
  CHECK(save_box_header(gs.box));
  // The rest of the save, twice each, for the same reason: a half-written store
  // answers LOAD_RECOVERED_PAIR, which is a successful recovery and not the
  // plain read this case is about.
  for (int w = 0; w < 2; ++w) {
    CHECK(save_config(gs.cfg));
    CHECK(save_inventory(gs.inv));
    CHECK(save_cooldowns(gs.cds));
    CHECK(save_trade_journal(gs.trade));
  }

  GameState back;
  memset(&back, 0, sizeof back);
  CHECK_EQ((int)save_load_all(back), (int)LOAD_OK);
  const BugInstance& stored = back.bugs[3];
  CHECK(cor_is_corrupted(stored));
  CHECK_EQ(stored.corrupt_until_epoch, p.corrupt_until_epoch);
  CHECK_EQ((int)(uint8_t)(stored.status & (uint8_t)PBS_CORRUPTED), (int)PBS_CORRUPTED);
  CHECK_EQ((int)validate_bug(stored), (int)VR_OK);
  CHECK_EQ((int)stored.hp_cur, (int)p.hp_cur);
  CHECK_EQ((int)stored.level, (int)p.level);
  CHECK_EQ(stored.id, p.id);
  // The clock keeps running across the reboot rather than restarting.
  CHECK_EQ(cor_left_s(stored, T0 + 3600u, (uint8_t)CAL_USER),
           (uint32_t)CORRUPT_DURATION_S - 3600u);
  // ...and the reloaded copy still expires on its own second, which is the
  // property a zeroed deadline would silently take away.
  BugInstance rl = stored;
  CHECK_EQ((int)cor_service(&rl, 1u, T0 + (uint32_t)CORRUPT_DURATION_S - 1u,
                            (uint8_t)CAL_USER), 0);
  CHECK_EQ((int)cor_service(&rl, 1u, T0 + (uint32_t)CORRUPT_DURATION_S,
                            (uint8_t)CAL_USER), 1);
  CHECK(!cor_is_corrupted(rl));

  // --- TRADED ---------------------------------------------------------------
  // EXACTLY ONE BUG COMES OUT. The wire strips the status (BUGW_STATUS_MASK)
  // and the receiver's copy is a complete, valid, UNCORRUPTED creature - not a
  // half-transferred one, not a second copy of a corrupted one.
  uint8_t rec[BUGW_BYTES];
  pbw_encode(p, rec);
  BugInstance received;
  CHECK_EQ((int)pbw_decode(rec, received), (int)VR_OK);
  CHECK_EQ((int)validate_bug(received), (int)VR_OK);
  CHECK_EQ((int)received.species_id, (int)p.species_id);
  CHECK_EQ((int)received.level, (int)p.level);
  CHECK_EQ(received.id, p.id);
  CHECK(!cor_is_corrupted(received));
  CHECK_EQ(received.corrupt_until_epoch, 0u);
  // The SENDER is untouched by encoding: a trade that mutated the source would
  // be the "real damage" this case is looking for.
  CHECK(cor_is_corrupted(p));

  // --- BRED FROM ------------------------------------------------------------
  // The child is a NEW Bug and inherits no status: corruption is an acquired
  // condition with a 24 h life, not a heritable trait, and a heritable one
  // would be permanent by the back door. Through the REAL breeding path and a
  // REAL bound Box - a hand-built child would prove nothing about breed_commit.
  box_fixture();
  BugInstance* pa = make_parent(2, 10, seeded_genesis(0x11u));
  BugInstance* pb = make_parent(2, 10, seeded_genesis(0x22u));
  CHECK(pa != nullptr && pb != nullptr);
  if (pa == nullptr || pb == nullptr) return;
  CHECK(cor_apply(*pa, T0, (uint8_t)CAL_USER));
  const BugInstance parent_before = *pa;

  BreedPlan plan;
  CHECK_EQ((int)breed_compute(*pa, *pb, 0xC0FFEEu, plan), (int)BRD_OK);
  uint8_t slot = (uint8_t)BOX_SLOT_NONE;
  CHECK_EQ((int)breed_commit(plan, T0, slot), (int)BRD_OK);
  CHECK(slot != (uint8_t)BOX_SLOT_NONE);
  const BugInstance* child = box_peek(slot);
  CHECK(child != nullptr);
  if (child != nullptr) {
    CHECK_EQ((int)validate_bug(*child), (int)VR_OK);
    CHECK(!cor_is_corrupted(*child));
    CHECK_EQ(child->corrupt_until_epoch, 0u);
    CHECK_EQ((int)(uint8_t)(child->status & (uint8_t)PBS_CORRUPTED), 0);
    CHECK(child->id != pa->id && child->id != pb->id);
  }
  // BOTH PARENTS SURVIVE, and the corrupted one is byte-for-byte what it was:
  // breeding neither cures it, extends it, nor consumes the creature.
  CHECK_EQ(memcmp(pa, &parent_before, sizeof parent_before), 0);
  CHECK(cor_is_corrupted(*pa));
  CHECK_EQ((int)validate_bug(*pa), (int)VR_OK);
  CHECK(!cor_is_corrupted(*pb));
  // THREE BUGS, ONE OF THEM CORRUPTED, AND NO FOURTH. "Still exactly one
  // Bug" measured over the whole Box rather than asserted about the record.
  uint8_t filled = 0, ill = 0;
  for (uint8_t i = 0; i < (uint8_t)BOX_SLOTS; ++i) {
    const BugInstance* q = box_peek(i);
    if (q == nullptr) continue;
    ++filled;
    if (cor_is_corrupted(*q)) ++ill;
  }
  CHECK_EQ((int)filled, 3);
  CHECK_EQ((int)ill, 1);
}

TEST(the_status_bit_is_bounded_and_a_corrupted_bug_is_never_lost) {
  // Spec section 27: the Bug is never lost. Corruption may not be the
  // exception, so the extremes are driven rather than reasoned about.
  BugInstance p;
  mk_bug(p, 5, 30, 0xDEADu);
  p.status = (uint8_t)(PBS_SICK | PBS_ASLEEP);

  // A CLOCK AT THE TOP OF ITS RANGE SATURATES rather than wrapping to a
  // deadline in 1970 - i.e. a status that clears instantly.
  CHECK(cor_apply(p, 0xFFFFFF00u, (uint8_t)CAL_USER));
  CHECK_EQ(p.corrupt_until_epoch, 0xFFFFFFFFu);
  CHECK(cor_is_corrupted(p));
  CHECK_EQ((int)validate_bug(p), (int)VR_OK);
  // The other statuses are untouched: corruption owns one bit.
  CHECK_EQ((int)(uint8_t)(p.status & (uint8_t)PBS_SICK), (int)PBS_SICK);
  CHECK_EQ((int)(uint8_t)(p.status & (uint8_t)PBS_ASLEEP), (int)PBS_ASLEEP);

  // The cure works at the saturated deadline too - the escape hatch may never
  // be the thing that gets stuck (section 47).
  CHECK(cor_clear(p));
  CHECK(!cor_is_corrupted(p));
  CHECK_EQ((int)(uint8_t)(p.status & (uint8_t)PBS_SICK), (int)PBS_SICK);

  // AND HP IS NEVER TOUCHED BY ANY OF IT. Corruption is not damage; it looks
  // like damage. A corrupted Bug at 1 HP is still at 1 HP.
  p.hp_cur = 1u;
  CHECK(cor_apply(p, T0, (uint8_t)CAL_USER));
  CHECK_EQ((int)p.hp_cur, 1);
  CHECK_EQ((int)cor_service(&p, 1u, T0 + (uint32_t)CORRUPT_DURATION_S,
                            (uint8_t)CAL_USER), 1);
  CHECK_EQ((int)p.hp_cur, 1);
  CHECK_EQ((int)validate_bug(p), (int)VR_OK);
}
