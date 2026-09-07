// =============================================================================
//  PEBBLEBOL host tests - test_creator_gate.cpp (P8-C1/C2)
//
//  THE PIN STATE MACHINE AND THE IDLE TIMER, DRIVEN DIRECTLY.
//
//  ONE OBJECT AND NO OTHERS is the point of this binary's link line
//  (tests/Makefile): networking/creator_gate.cpp reaches no clock, no store, no
//  RNG stream and no socket - every one of those arrives as an argument - so if
//  this ever needs a second object, something device-shaped has leaked into the
//  rules.
//
//  WHERE THIS FIXTURE DIFFERS FROM app_setup()'s OWN WIRING, stated here
//  because a fixture that differs in silence is testing a firmware nobody
//  flashes (the P7 defect the phase-8 preamble records):
//
//   1. THE CLOCK. The firmware passes gt_mono32() at every call site
//      (networking/webui.cpp); these cases pass a uint32_t the test advances by
//      hand. That is the SAME quantity - monotonic milliseconds - and passing
//      it in is exactly what makes a 60 s lockout and a 300 s idle timeout
//      executable in microseconds. What it cannot prove is that webui.cpp calls
//      gt_mono32() and not millis(); that is a one-line reading of
//      networking/webui.cpp, and gametime.h's own tests
//      (tests/test_clock_device.cpp) are what prove gt_mono32() behaves.
//
//   2. THE PERSISTED HALF. cg_open()'s `persisted_fails` really comes from
//      ConfigV2.pin_fail_count through gs_creator_load(); here it is an
//      argument. The round trip through flash is tested where it lives, in
//      tests/test_game_state.cpp, against the real save_manager and the kv_mem
//      fake - not mocked here.
//
//   3. THERE IS NO SOCKET. cg_verify() never sees an HTTP request; webui.cpp
//      hands it the X-Pin header value as a plain string. A test that a bad
//      header reaches the gate at all is only possible on the device, and
//      P8-C5's tools/creator_smoke.sh is where it belongs.
// =============================================================================
#include "nt_test.h"

#include "networking/creator_gate.h"

// -----------------------------------------------------------------------------
//  Helpers
// -----------------------------------------------------------------------------
static const uint32_t T0 = 1000u;        // an arbitrary non-zero "now"

static CreatorGate fresh(uint16_t pin = 4242u, uint8_t fails = 0u,
                         uint16_t idle_s = 0u, uint32_t now = T0) {
  CreatorGate g;
  memset(&g, 0xAA, sizeof g);            // poison: cg_open must set every field
  cg_open(g, pin, fails, idle_s, now);
  return g;
}

static void pin_text(uint16_t v, char* out) { snprintf(out, 8, "%04u", (unsigned)v); }

// Drive `n` wrong guesses at `now`, returning the last verdict.
static CgVerdict miss(CreatorGate& g, int n, uint32_t now) {
  CgVerdict v = CG_OK;
  for (int i = 0; i < n; ++i) v = cg_verify(g, "0001", now);
  return v;
}

// =============================================================================
//  1. THE MINT
// =============================================================================

// 0 is the "no PIN issued" sentinel in ConfigV2. A mint that can return it
// prints "PIN: 0000", persists "none issued", and re-rolls on the next CREATOR
// entry - silently invalidating the PIN already on the user's phone. One time
// in ten thousand is exactly the failure rate nobody reproduces.
TEST(mint_never_returns_the_no_pin_sentinel) {
  // Every residue class of the modulus, plus the values a broken entropy source
  // would hand over.
  for (uint32_t e = 0; e < 20000u; ++e) {
    const uint16_t p = cg_mint_pin(e);
    CHECK(p != 0u);
    CHECK(p < (uint16_t)WEB_PIN_MAX);
  }
  CHECK(cg_mint_pin(0u) != 0u);
  CHECK(cg_mint_pin(0xFFFFFFFFu) != 0u);
  CHECK(cg_mint_pin(0x80000000u) != 0u);
  CHECK(cg_mint_pin((uint32_t)(WEB_PIN_MAX - 1)) != 0u);
}

// The whole 1..9999 range must be reachable, or the PIN is shorter than it
// looks. rng_below(RNG_MISC, WEB_PIN_MAX - 1) is what the firmware feeds it, so
// the input range walked here is the input range the device produces.
TEST(mint_covers_the_whole_range_exactly_once) {
  bool seen[WEB_PIN_MAX];
  memset(seen, 0, sizeof seen);
  for (uint32_t e = 0; e < (uint32_t)(WEB_PIN_MAX - 1); ++e) {
    const uint16_t p = cg_mint_pin(e);
    CHECK(p >= 1u && p <= (uint16_t)(WEB_PIN_MAX - 1));
    CHECK(!seen[p]);                     // uniform: no residue collides
    seen[p] = true;
  }
  CHECK(!seen[0]);
  for (uint16_t p = 1; p < (uint16_t)WEB_PIN_MAX; ++p) CHECK(seen[p]);
}

// =============================================================================
//  2. THE PARSER  (the authentication bypass this replaces)
// =============================================================================

TEST(parse_accepts_exactly_four_digits) {
  uint16_t v = 0xFFFFu;
  CHECK(cg_parse_pin("0000", v)); CHECK_EQ((int)v, 0);
  CHECK(cg_parse_pin("0001", v)); CHECK_EQ((int)v, 1);
  CHECK(cg_parse_pin("4242", v)); CHECK_EQ((int)v, 4242);
  CHECK(cg_parse_pin("9999", v)); CHECK_EQ((int)v, 9999);
}

TEST(parse_rejects_everything_that_is_not_four_digits) {
  uint16_t v = 0u;
  const char* bad[] = {
    "", "1", "12", "123", "12345", "00000",
    " 123", "123 ", "12 4", "+123", "-123", "12.4", "abcd", "12ab", "ab12",
    "0x12", "1234\n", "1234 ", "\t123",
    "\xd9\xa1\xd9\xa2\xd9\xa3\xd9\xa4"   /* UTF-8 Arabic-Indic digits: not ASCII, not a PIN */
  };
  for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
    CHECK(!cg_parse_pin(bad[i], v));
  }
  CHECK(!cg_parse_pin(nullptr, v));
}

// THE RECORDED BYPASS. The helper this replaces (git show
// aa2b7ee:Pebblebol/webui.cpp, arg_u32) accumulated into a uint32_t against a
// 0xFFFFFFFF ceiling, so the multiply wrapped mod 2^32 and PIN 3821 was also
// produced by "4294971117". Bounding the INPUT length first makes the
// accumulator width unable to matter: nothing longer than four characters is
// ever converted at all.
TEST(parse_refuses_the_wrapping_decimals_that_used_to_authenticate) {
  uint16_t v = 0u;
  CHECK(!cg_parse_pin("4294971117", v));         // the recorded example, PIN 3821
  // Every decimal congruent to a live PIN mod 2^32, for the first few classes.
  for (uint32_t pin = 0; pin < 10u; ++pin) {
    for (uint32_t k = 1; k <= 3u; ++k) {
      char buf[24];
      const unsigned long long cong = (unsigned long long)pin + (unsigned long long)k * 4294967296ull;
      snprintf(buf, sizeof buf, "%llu", cong);
      CHECK(!cg_parse_pin(buf, v));
    }
  }
}

// =============================================================================
//  3. VERIFY, FAIL, LOCK, EXPIRE
// =============================================================================

TEST(the_issued_pin_is_accepted_and_clears_the_counter) {
  CreatorGate g = fresh(4242u);
  char ok[8]; pin_text(4242u, ok);
  CHECK_EQ((int)miss(g, 3, T0), (int)CG_BAD_PIN);
  CHECK_EQ((int)g.fail_count, 3);
  CHECK_EQ((int)cg_verify(g, ok, T0), (int)CG_OK);
  CHECK_EQ((int)g.fail_count, 0);
  CHECK_EQ((int)g.lock_armed, 0);
}

TEST(a_gate_with_no_pin_issued_refuses_everything) {
  CreatorGate g = fresh(0u);
  CHECK_EQ((int)cg_verify(g, "0000", T0), (int)CG_NO_PIN);
  CHECK_EQ((int)cg_verify(g, "1234", T0), (int)CG_NO_PIN);
  CHECK_EQ((int)g.fail_count, 0);              // not a guess: nothing to guess
}

TEST(a_closed_gate_authorises_nothing) {
  CreatorGate g = fresh(4242u);
  cg_close(g);
  CHECK_EQ((int)cg_verify(g, "4242", T0), (int)CG_NO_PIN);
}

TEST(five_failures_arm_a_sixty_second_lockout) {
  CreatorGate g = fresh(4242u);
  for (int i = 1; i <= (int)CREATOR_PIN_FAIL_MAX - 1; ++i) {
    CHECK_EQ((int)cg_verify(g, "0001", T0), (int)CG_BAD_PIN);
    CHECK_EQ((int)g.lock_armed, 0);            // not yet
    CHECK_EQ((int)g.fail_count, i);
  }
  CHECK_EQ((int)cg_verify(g, "0001", T0), (int)CG_BAD_PIN);   // the fifth
  CHECK_EQ((int)g.lock_armed, 1);
  CHECK_EQ((unsigned long)cg_lock_left_ms(g, T0), (unsigned long)CREATOR_PIN_LOCK_MS);
}

// A locked gate refuses the CORRECT PIN too. Anything else would make the
// lockout a formality: an attacker who happened to guess right on the sixth try
// would be let in by the very mechanism meant to stop them.
TEST(a_locked_gate_refuses_even_the_correct_pin) {
  CreatorGate g = fresh(4242u);
  (void)miss(g, CREATOR_PIN_FAIL_MAX, T0);
  CHECK_EQ((int)cg_verify(g, "4242", T0 + 1000u), (int)CG_LOCKED);
  CHECK_EQ((int)cg_verify(g, "4242", T0 + 59999u), (int)CG_LOCKED);
}

// And it does not COUNT the refused attempts, or a client could extend its own
// lockout forever - and the owner who mistyped five times would be punished for
// waiting patiently.
TEST(attempts_made_while_locked_do_not_extend_the_lockout) {
  CreatorGate g = fresh(4242u);
  (void)miss(g, CREATOR_PIN_FAIL_MAX, T0);
  const uint32_t deadline = g.lock_until_ms;
  for (uint32_t t = T0 + 1000u; t < T0 + 60000u; t += 1000u) {
    CHECK_EQ((int)cg_verify(g, "0001", t), (int)CG_LOCKED);
  }
  CHECK_EQ((unsigned long)g.lock_until_ms, (unsigned long)deadline);
}

TEST(the_lockout_expires_after_exactly_sixty_seconds) {
  CreatorGate g = fresh(4242u);
  (void)miss(g, CREATOR_PIN_FAIL_MAX, T0);
  CHECK_EQ((int)cg_verify(g, "0001", T0 + CREATOR_PIN_LOCK_MS - 1u), (int)CG_LOCKED);
  // At the deadline the correct PIN gets in again.
  CHECK_EQ((int)cg_verify(g, "4242", T0 + CREATOR_PIN_LOCK_MS), (int)CG_OK);
  CHECK_EQ((int)g.fail_count, 0);
  CHECK_EQ((int)g.lock_armed, 0);
}

// THE THROTTLE. After the fifth failure the counter stays at the maximum, so an
// expired lockout buys exactly ONE guess and a wrong one re-locks at once. That
// is what turns ~83 minutes of guessing into ~33 hours.
TEST(an_expired_lockout_grants_exactly_one_guess) {
  CreatorGate g = fresh(4242u);
  (void)miss(g, CREATOR_PIN_FAIL_MAX, T0);
  const uint32_t t1 = T0 + CREATOR_PIN_LOCK_MS;
  CHECK_EQ((int)cg_verify(g, "0001", t1), (int)CG_BAD_PIN);   // the one guess
  CHECK_EQ((int)g.lock_armed, 1);                             // and it re-locked
  CHECK_EQ((unsigned long)cg_lock_left_ms(g, t1), (unsigned long)CREATOR_PIN_LOCK_MS);
  CHECK_EQ((int)cg_verify(g, "0001", t1 + 1u), (int)CG_LOCKED);
}

TEST(lock_left_is_zero_when_nothing_is_armed_and_bounded_when_it_is) {
  CreatorGate g = fresh(4242u);
  CHECK_EQ((unsigned long)cg_lock_left_ms(g, T0), 0ul);
  (void)miss(g, CREATOR_PIN_FAIL_MAX, T0);
  for (uint32_t t = T0; t <= T0 + CREATOR_PIN_LOCK_MS; t += 997u) {
    const uint32_t left = cg_lock_left_ms(g, t);
    CHECK(left <= (uint32_t)CREATOR_PIN_LOCK_MS);
  }
  CHECK_EQ((unsigned long)cg_lock_left_ms(g, T0 + CREATOR_PIN_LOCK_MS), 0ul);
  CHECK_EQ((unsigned long)cg_lock_left_ms(g, T0 + CREATOR_PIN_LOCK_MS + 5000u), 0ul);
}

// gt_mono32() wraps every 49.7 days exactly as millis() does. A lockout armed
// three seconds before the wrap must still expire three seconds after it.
TEST(the_lockout_survives_the_monotonic_clock_wrap) {
  const uint32_t near_wrap = 0xFFFFFFFFu - 3000u;
  CreatorGate g = fresh(4242u, 0u, 0u, near_wrap);
  (void)miss(g, CREATOR_PIN_FAIL_MAX, near_wrap);
  CHECK_EQ((int)cg_verify(g, "4242", (uint32_t)(near_wrap + 30000u)), (int)CG_LOCKED);
  CHECK_EQ((int)cg_verify(g, "4242", (uint32_t)(near_wrap + CREATOR_PIN_LOCK_MS)), (int)CG_OK);
}

// =============================================================================
//  4. REBOOT MID-LOCKOUT  (the clock question, executed)
// =============================================================================

// A power cut zeroes gt_mono32() and takes the RAM deadline with it. What
// survives is ConfigV2.pin_fail_count, and it must come back ARMED: the next
// wrong PIN locks immediately instead of buying five fresh guesses.
TEST(reboot_mid_lockout_does_not_grant_five_fresh_guesses) {
  CreatorGate before = fresh(4242u);
  (void)miss(before, CREATOR_PIN_FAIL_MAX, T0);
  const uint8_t persisted = cg_persist_fails(before);
  CHECK_EQ((int)persisted, (int)CREATOR_PIN_FAIL_MAX);

  // The reboot: a new gate, the monotonic clock back at zero, the persisted
  // counter restored.
  CreatorGate after = fresh(4242u, persisted, 0u, 0u);
  CHECK_EQ((int)after.fail_count, (int)CREATOR_PIN_FAIL_MAX);
  CHECK_EQ((int)after.lock_armed, 0);            // no deadline could survive
  CHECK_EQ((int)cg_verify(after, "0001", 0u), (int)CG_BAD_PIN);   // ONE guess
  CHECK_EQ((int)after.lock_armed, 1);                             // then locked
  CHECK_EQ((int)cg_verify(after, "0001", 5u), (int)CG_LOCKED);
}

// And the concession is bounded: rebooting skips the remaining 60 s wait once.
// That is worth nothing to an attacker, because power-cycling this device means
// standing in front of a screen that DISPLAYS THE PIN - which is why the
// correct PIN is still accepted after the reboot.
TEST(reboot_mid_lockout_still_lets_the_owner_in) {
  CreatorGate after = fresh(4242u, CREATOR_PIN_FAIL_MAX, 0u, 0u);
  CHECK_EQ((int)cg_verify(after, "4242", 0u), (int)CG_OK);
  CHECK_EQ((int)after.fail_count, 0);
  CHECK_EQ((int)cg_persist_fails(after), 0);
}

// The counter is restored, not invented: a saturated or corrupt persisted value
// cannot push fail_count past the maximum, and a clean one comes back clean.
TEST(cg_open_clamps_a_hostile_persisted_fail_count) {
  CreatorGate g = fresh(4242u, 200u, 0u, T0);
  CHECK_EQ((int)g.fail_count, (int)CREATOR_PIN_FAIL_MAX);
  CreatorGate h = fresh(4242u, 0u, 0u, T0);
  CHECK_EQ((int)h.fail_count, 0);
  CHECK_EQ((int)cg_verify(h, "0001", T0), (int)CG_BAD_PIN);
  CHECK_EQ((int)h.lock_armed, 0);                // one failure is not five
}

// =============================================================================
//  5. WHAT REACHES FLASH
// =============================================================================

// Only the ARMED EDGE is persisted, never the running count. Every intermediate
// failure that reached flash would be one write an unauthenticated client can
// ask for; see creator_gate.h (cg_persist_fails).
TEST(only_the_armed_edge_is_persisted) {
  CreatorGate g = fresh(4242u);
  for (int i = 1; i < (int)CREATOR_PIN_FAIL_MAX; ++i) {
    (void)cg_verify(g, "0001", T0);
    CHECK_EQ((int)g.fail_count, i);
    CHECK_EQ((int)cg_persist_fails(g), 0);       // nothing to write yet
  }
  (void)cg_verify(g, "0001", T0);                // the fifth
  CHECK_EQ((int)cg_persist_fails(g), (int)CREATOR_PIN_FAIL_MAX);
  (void)cg_verify(g, "4242", T0 + CREATOR_PIN_LOCK_MS);
  CHECK_EQ((int)cg_persist_fails(g), 0);         // and once more, back to 0
}

// =============================================================================
//  6. THE IDLE TIMER  (spec 34/40, decision D7)
// =============================================================================

// 0 in ConfigV2 means "never set" - a fresh device, or a save older than the
// field. Reading it as a literal zero-second budget would tear the access point
// down the instant it came up, and the user would never see a page.
TEST(a_zero_idle_config_means_the_default_not_an_instant_shutdown) {
  CHECK_EQ((int)cg_idle_seconds(0u), (int)CREATOR_IDLE_S_DEFAULT);
  CreatorGate g = fresh(4242u, 0u, /*idle_s=*/0u, T0);
  CHECK_EQ((int)g.idle_s, (int)CREATOR_IDLE_S_DEFAULT);
  CHECK(!cg_idle_expired(g, T0));
  CHECK(!cg_idle_expired(g, T0 + 1u));
  CHECK(!cg_idle_expired(g, T0 + (uint32_t)CREATOR_IDLE_S_DEFAULT * 1000u - 1u));
  CHECK(cg_idle_expired(g, T0 + (uint32_t)CREATOR_IDLE_S_DEFAULT * 1000u));
}

TEST(the_idle_budget_is_clamped_and_honoured) {
  CHECK_EQ((int)cg_idle_seconds(1u), 1);
  CHECK_EQ((int)cg_idle_seconds(300u), 300);
  CHECK_EQ((int)cg_idle_seconds((uint16_t)CREATOR_IDLE_S_MAX), (int)CREATOR_IDLE_S_MAX);
  CHECK_EQ((int)cg_idle_seconds(65535u), (int)CREATOR_IDLE_S_MAX);

  CreatorGate g = fresh(4242u, 0u, 120u, T0);
  CHECK(!cg_idle_expired(g, T0 + 119999u));
  CHECK(cg_idle_expired(g, T0 + 120000u));
}

// THE RULE THAT KEEPS THE ACCESS POINT THE OWNER'S. Only an AUTHORISED request
// moves the clock. Without this, anyone in radio range could hold the portal up
// forever by failing the PIN once every 299 s, and "Wi-Fi only when needed"
// (spec 40) would belong to whoever was nearest.
TEST(unauthenticated_traffic_does_not_extend_the_portal) {
  CreatorGate g = fresh(4242u, 0u, 60u, T0);
  // Four wrong PINs spread across the whole budget; the fifth would lock.
  for (uint32_t t = T0 + 10000u; t <= T0 + 40000u; t += 10000u) {
    CHECK_EQ((int)cg_verify(g, "0001", t), (int)CG_BAD_PIN);
  }
  CHECK(cg_idle_expired(g, T0 + 60000u));        // expired on schedule
}

TEST(an_authorised_request_extends_the_portal) {
  CreatorGate g = fresh(4242u, 0u, 60u, T0);
  CHECK_EQ((int)cg_verify(g, "4242", T0 + 50000u), (int)CG_OK);
  CHECK(!cg_idle_expired(g, T0 + 60000u));       // the ping moved the deadline
  CHECK(cg_idle_expired(g, T0 + 110000u));
}

// A locked-out client cannot keep the portal alive either, correct PIN or not.
TEST(a_locked_client_cannot_hold_the_portal_open) {
  CreatorGate g = fresh(4242u, 0u, 60u, T0);
  (void)miss(g, CREATOR_PIN_FAIL_MAX, T0 + 1000u);
  for (uint32_t t = T0 + 2000u; t < T0 + 60000u; t += 5000u) {
    CHECK_EQ((int)cg_verify(g, "4242", t), (int)CG_LOCKED);
  }
  CHECK(cg_idle_expired(g, T0 + 60000u));
}

// A portal that is not up cannot time out: cg_idle_expired() is polled every
// second by the CREATOR screen, including on a device whose access point never
// came up, and a `true` there would bounce the user out of a screen they just
// opened before the radio had a chance.
TEST(a_closed_gate_never_expires) {
  CreatorGate g = fresh(4242u, 0u, 60u, T0);
  cg_close(g);
  CHECK(!cg_idle_expired(g, T0 + 10u * 60000u));
}

TEST(the_idle_timer_survives_the_monotonic_clock_wrap) {
  const uint32_t near_wrap = 0xFFFFFFFFu - 10000u;
  CreatorGate g = fresh(4242u, 0u, 60u, near_wrap);
  CHECK(!cg_idle_expired(g, (uint32_t)(near_wrap + 59999u)));
  CHECK(cg_idle_expired(g, (uint32_t)(near_wrap + 60000u)));
}
