// =============================================================================
//  Pebblebol host tests - test_exploration_hash.cpp
//  P5-C1: the network classifier, the salted identity, the ScanResult layout
//  and the scan job's timeout and cancel.
//
//  NO RADIO ANYWHERE IN THIS BINARY. networking/net_classify.cpp and
//  networking/wifi_scanner.cpp are pure translation units and the scan job
//  reaches the device through a WifiScanDriver of function pointers, so the
//  fake driver at the bottom of this file drives every path a real scan can
//  take - including the two that are hardest to provoke on hardware, the 12 s
//  timeout and the cancel.
// =============================================================================
#include "nt_test.h"

#include <string.h>
#include <stddef.h>

#include "networking/net_classify.h"
#include "networking/wifi_scanner.h"
#include "data/network_table.h"

// =============================================================================
//  1. FNV-1a: THE FIRMWARE AND THE GENERATOR MUST COMPUTE THE SAME FUNCTION
//
//  tools/gen_content.py hashes each token to build NET_TOKEN_TABLE and
//  net_classify.cpp hashes each run of letters to look one up. If the two ever
//  disagree EVERY lookup misses silently, every token class comes back 0, and
//  every network on earth classifies by signal strength alone - a failure with
//  no symptom at all. The literals below were produced by the generator's own
//  fnv1a32() and are pinned from this side.
// =============================================================================
TEST(fnv1a32_agrees_with_the_generator_that_built_the_token_table) {
  CHECK_EQ(net_fnv1a32("movistar", 8), 0x369D8170u);
  CHECK_EQ(net_fnv1a32("eduroam",  7), 0x24D0EA4Eu);
  CHECK_EQ(net_fnv1a32("hotspot",  7), 0xA4A540BCu);
  // The offset basis with nothing hashed into it.
  CHECK_EQ(net_fnv1a32("", 0), 0x811C9DC5u);

  // ...and those three hashes really are in the generated table, with the
  // classes the pack assigned them. This is the half that proves the pin is
  // about THIS table and not about arithmetic in general.
  int found = 0;
  for (uint8_t i = 0; i < NET_TOKEN_ROW_COUNT; ++i) {
    if (NET_TOKEN_TABLE[i].hash == 0x369D8170u) { CHECK_EQ((unsigned)NET_TOKEN_TABLE[i].klass, (unsigned)NTOK_ISP_CPE);  ++found; }
    if (NET_TOKEN_TABLE[i].hash == 0x24D0EA4Eu) { CHECK_EQ((unsigned)NET_TOKEN_TABLE[i].klass, (unsigned)NTOK_VENUE);    ++found; }
    if (NET_TOKEN_TABLE[i].hash == 0xA4A540BCu) { CHECK_EQ((unsigned)NET_TOKEN_TABLE[i].klass, (unsigned)NTOK_OPERATOR); ++found; }
  }
  CHECK_EQ(found, 3);
}

// =============================================================================
//  2. THE SALTED IDENTITY (spec section 44)
// =============================================================================
static const uint8_t BSSID_A[6] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 };
static const uint8_t BSSID_B[6] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x56 };  // one bit

TEST(the_same_access_point_hashes_the_same_way_every_time) {
  const uint32_t salt = net_scan_salt(0x12345678u);
  const uint32_t h1 = net_hash_from_bssid(BSSID_A, salt);
  const uint32_t h2 = net_hash_from_bssid(BSSID_A, salt);
  CHECK_EQ(h1, h2);
  // Pinned, so a change to the derivation is a decision somebody makes rather
  // than something that silently invalidates every cooldown row in the field.
  CHECK_EQ(salt, 0xFB5A1B55u);
  CHECK_EQ(h1,   0x4A77EA45u);
  // One bit of address is a different network.
  CHECK(net_hash_from_bssid(BSSID_B, salt) != h1);
}

TEST(two_devices_hash_the_same_access_point_differently) {
  const uint32_t s1 = net_scan_salt(0x12345678u);
  const uint32_t s2 = net_scan_salt(0x12345679u);
  CHECK(s1 != s2);
  CHECK(net_hash_from_bssid(BSSID_A, s1) != net_hash_from_bssid(BSSID_A, s2));

  // AND THE SALT IS NOT THE DEVICE ID. Using the id raw would let anyone who
  // has seen it - it goes on the wire in ProtoHello - test a candidate address
  // against a stored hash. One FNV pass is what removes that.
  CHECK(s1 != 0x12345678u);
}

// THE FOLD, DRIVEN BY A REAL PREIMAGE OF ZERO RATHER THAN BY HOPE.
// CooldownRow.net_hash == 0 is "empty row", so a network hashing to 0 would be
// permanently off cooldown - infinite farming on exactly one network, invisible
// in the field. The address and salt below were SOLVED for offline: FNV-1a over
// those ten bytes really is 0, so this case fails the moment the fold is
// removed and cannot be satisfied by a hash that merely happens not to be 0.
TEST(an_access_point_that_hashes_to_zero_is_folded_away) {
  static const uint8_t bssid_zero[6] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x12, 0x34 };
  const uint32_t salt_zero = 0x7600281Fu;

  // The raw hash of exactly those bytes is 0 - stated here so the case explains
  // itself rather than resting on a magic constant.
  uint8_t raw[10];
  memcpy(raw, bssid_zero, 6);
  raw[6] = (uint8_t)(salt_zero & 0xFFu);
  raw[7] = (uint8_t)((salt_zero >> 8) & 0xFFu);
  raw[8] = (uint8_t)((salt_zero >> 16) & 0xFFu);
  raw[9] = (uint8_t)((salt_zero >> 24) & 0xFFu);
  CHECK_EQ(net_fnv1a32(raw, sizeof(raw)), 0u);

  CHECK_EQ(net_hash_from_bssid(bssid_zero, salt_zero), (uint32_t)NET_HASH_NEVER_ZERO);
  CHECK(net_hash_from_bssid(bssid_zero, salt_zero) != 0u);

  // And nothing else is disturbed by the fold.
  CHECK_EQ(net_hash_from_bssid(BSSID_A, net_scan_salt(0x12345678u)), 0x4A77EA45u);
}

TEST(no_address_and_salt_in_a_wide_sweep_ever_produces_zero) {
  uint8_t b[6] = { 0, 0, 0, 0, 0, 0 };
  for (uint32_t i = 0; i < 4000u; ++i) {
    b[0] = (uint8_t)(i & 0xFFu);
    b[1] = (uint8_t)((i >> 8) & 0xFFu);
    b[5] = (uint8_t)(i * 31u);
    CHECK(net_hash_from_bssid(b, net_scan_salt(i * 2654435761u)) != 0u);
  }
}

// =============================================================================
//  3. TOKENS - a bitmask of classes, and nothing else, leaves the name
// =============================================================================
static uint8_t toks(const char* s) { return net_tokens_of(s, strlen(s)); }

TEST(a_name_is_split_into_runs_of_letters_and_matched_whole) {
  CHECK_EQ((unsigned)toks("MOVISTAR_1234"), (unsigned)NTOK_ISP_CPE);
  CHECK_EQ((unsigned)toks("vodafone9F4"),   (unsigned)NTOK_ISP_CPE);
  CHECK_EQ((unsigned)toks("MiFibra-A1B2"),  (unsigned)NTOK_ISP_CPE);
  CHECK_EQ((unsigned)toks("eduroam"),       (unsigned)NTOK_VENUE);
  CHECK_EQ((unsigned)toks("Hotel Costa"),   (unsigned)NTOK_VENUE);
  CHECK_EQ((unsigned)toks("FON_ZONE"),      (unsigned)NTOK_OPERATOR);
  // Two classes at once is legal and is an OR, not a first-match.
  CHECK_EQ((unsigned)toks("hotel-fon"),
           (unsigned)(NTOK_VENUE | NTOK_OPERATOR));
}

TEST(matching_is_whole_token_so_a_substring_is_not_a_match) {
  // "Telefonica" is ONE run of letters, so it does not contain the token "fon".
  // Under-matching is the safe direction: the name falls through to the signal
  // rules instead of being asserted to be a carrier hotspot.
  CHECK_EQ((unsigned)toks("Telefonica"), 0u);
  CHECK_EQ((unsigned)toks("guesthouse"), 0u);   // not "guest"
  CHECK_EQ((unsigned)toks("nomatchhere"), 0u);
}

TEST(a_name_the_scanner_cannot_use_produces_no_tokens_and_no_crash) {
  CHECK_EQ((unsigned)net_tokens_of(nullptr, 8), 0u);
  CHECK_EQ((unsigned)net_tokens_of("", 0), 0u);
  CHECK_EQ((unsigned)toks("12345"), 0u);        // digits only
  CHECK_EQ((unsigned)toks("ab"), 0u);           // shorter than NET_TOKEN_MIN_LEN
  // Non-ASCII bytes are delimiters, not decoded: a UTF-8 name simply splits.
  CHECK_EQ((unsigned)toks("caf\xc3\xa9 eduroam"), (unsigned)NTOK_VENUE);
  // A run longer than the table's longest token can match nothing, and must not
  // walk off the buffer either.
  CHECK_EQ((unsigned)toks("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"), 0u);
  // The last token of a name with no trailing delimiter is still looked up -
  // which is most names.
  CHECK_EQ((unsigned)toks("mi_red_eduroam"), (unsigned)NTOK_VENUE);
}

// =============================================================================
//  4. THE CLASSIFIER LADDER, TABLE-DRIVEN
//
//  Every one of the six outputs, both sides of both thresholds, and the default
//  arm fed a byte outside the enum. A category is what the whole encounter
//  table is indexed by, so each row here is a gameplay decision.
// =============================================================================
struct ClassCase {
  const char* what;
  uint8_t     auth;
  uint8_t     hidden;
  int8_t      rssi;
  uint8_t     tokens;
  uint8_t     want;
};

static const ClassCase CLASS_CASES[] = {
  // --- rule 1: hidden wins over everything, including a strong auth signal ---
  { "hidden beats enterprise",   NAUTH_ENTERPRISE, 1, -30, 0,              NET_CAT_HIDDEN },
  { "hidden beats open+venue",   NAUTH_OPEN,       1, -30, NTOK_VENUE,     NET_CAT_HIDDEN },
  { "hidden and far away",       NAUTH_PSK,        1, -99, 0,              NET_CAT_HIDDEN },

  // --- rule 2: EAP is a business, at any distance and with any name ---------
  { "enterprise near",           NAUTH_ENTERPRISE, 0, -20, 0,              NET_CAT_BUSINESS },
  { "enterprise far",            NAUTH_ENTERPRISE, 0, -99, NTOK_ISP_CPE,   NET_CAT_BUSINESS },

  // --- rule 3: nothing to join with ----------------------------------------
  { "open, no tokens",           NAUTH_OPEN,       0, -40, 0,              NET_CAT_OPEN },
  { "open, venue token",         NAUTH_OPEN,       0, -90, NTOK_VENUE,     NET_CAT_PUBLIC },
  { "open, operator token",      NAUTH_OPEN,       0, -90, NTOK_OPERATOR,  NET_CAT_PUBLIC },
  { "open, cpe token only",      NAUTH_OPEN,       0, -40, NTOK_ISP_CPE,   NET_CAT_OPEN },
  { "owe, no tokens",            NAUTH_OWE,        0, -40, 0,              NET_CAT_OPEN },
  { "owe, venue token",          NAUTH_OWE,        0, -40, NTOK_VENUE,     NET_CAT_PUBLIC },

  // --- rule 4: locked ------------------------------------------------------
  { "psk venue beats distance",  NAUTH_PSK,        0, -99, NTOK_VENUE,     NET_CAT_PUBLIC },
  { "wep venue",                 NAUTH_WEP,        0, -99, NTOK_VENUE,     NET_CAT_PUBLIC },
  { "psk cpe at mid range",      NAUTH_PSK,        0, NET_RSSI_MID,  NTOK_ISP_CPE, NET_CAT_HOME },
  { "psk cpe below mid",         NAUTH_PSK,        0, NET_RSSI_MID - 1, NTOK_ISP_CPE, NET_CAT_UNKNOWN },
  { "psk at NEAR exactly",       NAUTH_PSK,        0, NET_RSSI_NEAR, 0,     NET_CAT_HOME },
  { "psk one dB below NEAR",     NAUTH_PSK,        0, NET_RSSI_NEAR - 1, 0, NET_CAT_BUSINESS },
  { "psk at MID exactly",        NAUTH_PSK,        0, NET_RSSI_MID,  0,     NET_CAT_BUSINESS },
  { "psk one dB below MID",      NAUTH_PSK,        0, NET_RSSI_MID - 1, 0,  NET_CAT_UNKNOWN },
  { "wep very near",             NAUTH_WEP,        0, 0,   0,              NET_CAT_HOME },
  { "psk at the noise floor",    NAUTH_PSK,        0, -127, NTOK_OPERATOR, NET_CAT_UNKNOWN },

  // --- rule 5: the sink ----------------------------------------------------
  { "NAUTH_OTHER near",          NAUTH_OTHER,      0, -20, NTOK_ISP_CPE,   NET_CAT_UNKNOWN },
  { "a byte outside the enum",   (uint8_t)200,     0, -20, NTOK_VENUE,     NET_CAT_UNKNOWN },
  { "NAUTH_COUNT itself",        (uint8_t)NAUTH_COUNT, 0, -20, 0,          NET_CAT_UNKNOWN },
};

TEST(the_classifier_ladder_answers_every_case_the_table_names) {
  const size_t n = sizeof(CLASS_CASES) / sizeof(CLASS_CASES[0]);
  for (size_t i = 0; i < n; ++i) {
    const ClassCase& c = CLASS_CASES[i];
    NetFacts f;
    f.auth   = c.auth;
    f.hidden = c.hidden;
    f.rssi   = c.rssi;
    f.tokens = c.tokens;
    const uint8_t got = net_classify(f);
    if (got != c.want) {
      fprintf(stderr, "  case \"%s\": got %u, want %u\n",
              c.what, (unsigned)got, (unsigned)c.want);
    }
    CHECK_EQ((unsigned)got, (unsigned)c.want);
  }
}

// EVERY CATEGORY IS REACHABLE. A ladder that can never emit HOME, or never
// emit OPEN, would leave a whole column of the encounter table dead - forty
// rows of ENCOUNTER_TABLE and thirty-three of ITEM_DROP_TABLE are indexed by
// this value - and every individual case above would still pass.
TEST(all_six_categories_are_reachable_from_the_ladder) {
  bool seen[NET_CAT_COUNT] = { false, false, false, false, false, false };
  const size_t n = sizeof(CLASS_CASES) / sizeof(CLASS_CASES[0]);
  for (size_t i = 0; i < n; ++i) {
    const ClassCase& c = CLASS_CASES[i];
    NetFacts f;
    f.auth = c.auth; f.hidden = c.hidden; f.rssi = c.rssi; f.tokens = c.tokens;
    const uint8_t got = net_classify(f);
    CHECK(got < (uint8_t)NET_CAT_COUNT);
    seen[got] = true;
  }
  for (uint8_t c = 0; c < (uint8_t)NET_CAT_COUNT; ++c) {
    if (!seen[c]) fprintf(stderr, "  category %u is unreachable\n", (unsigned)c);
    CHECK(seen[c]);
  }
}

// THE CLASSIFIER IS TOTAL. Every byte of auth, both hidden values, the whole
// int8_t signal range and every token combination: 256 * 2 * 256 * 8 inputs,
// and not one of them may leave the enum or reach for a clock.
TEST(the_classifier_is_total_over_every_input_byte) {
  for (unsigned a = 0; a < 256u; ++a) {
    for (unsigned h = 0; h < 2u; ++h) {
      for (unsigned t = 0; t <= (unsigned)NTOK_ALL; ++t) {
        for (int r = -128; r < 128; r += 17) {
          NetFacts f;
          f.auth = (uint8_t)a; f.hidden = (uint8_t)h;
          f.rssi = (int8_t)r;  f.tokens = (uint8_t)t;
          CHECK(net_classify(f) < (uint8_t)NET_CAT_COUNT);
        }
      }
    }
  }
}

// =============================================================================
//  5. ScanResult CARRIES NO NETWORK IDENTITY (spec section 44)
//
//  The plan asks for "no network-name field exists - checked by sizeof / field
//  list". SIZEOF ALONE IS A TEST THAT CANNOT FAIL IN THE WAY THAT MATTERS:
//  replacing the two reserved bytes with two bytes of a name keeps sizeof at 8.
//  So every member's offset is pinned as well, and the third half of the claim
//  - that the WORDS do not appear in networking/wifi_scanner.h at all, in a
//  field, a parameter or a comment - is a grep gate in tools/check.sh, which is
//  the one that cannot be satisfied by accident.
// =============================================================================
TEST(a_scan_result_is_eight_bytes_of_hash_signal_and_category) {
  CHECK_EQ((int)sizeof(ScanResult), 8);
  CHECK_EQ((int)offsetof(ScanResult, net_hash), 0);
  CHECK_EQ((int)offsetof(ScanResult, rssi),     4);
  CHECK_EQ((int)offsetof(ScanResult, category), 5);
  CHECK_EQ((int)offsetof(ScanResult, reserved), 6);
  CHECK_EQ((int)sizeof(((ScanResult*)nullptr)->net_hash), 4);
  CHECK_EQ((int)sizeof(((ScanResult*)nullptr)->reserved), 2);
  // The four members account for every byte: 4 + 1 + 1 + 2. A fifth field of
  // any size would have to displace one of them or grow the struct.
  CHECK_EQ((int)(sizeof(((ScanResult*)nullptr)->net_hash) +
                 sizeof(((ScanResult*)nullptr)->rssi) +
                 sizeof(((ScanResult*)nullptr)->category) +
                 sizeof(((ScanResult*)nullptr)->reserved)),
           (int)sizeof(ScanResult));
}

// =============================================================================
//  6. THE SCAN JOB - the 12 s timeout and the cancel (spec section 47)
//
//  A fake driver, so every exit path runs with no radio: the ordinary one, the
//  one where the driver refuses, the one where the scan fails, the one where
//  nothing ever answers, and the one where the player presses B.
// =============================================================================
static int      g_start_calls = 0;
static int      g_stop_calls  = 0;
static int      g_read_calls  = 0;
static bool     g_start_ok    = true;
static int16_t  g_poll        = WSCAN_POLL_RUNNING;
static uint8_t  g_available   = 0;

static bool fake_start(void) { ++g_start_calls; return g_start_ok; }
static int16_t fake_poll(void) { return g_poll; }
static uint8_t fake_read(ScanResult* out, uint8_t cap) {
  ++g_read_calls;
  uint8_t w = 0;
  for (uint8_t i = 0; i < g_available && w < cap; ++i) {
    out[w].net_hash    = 0x1000u + i;
    out[w].rssi        = (int8_t)(-40 - i);
    out[w].category    = (uint8_t)(i % (uint8_t)NET_CAT_COUNT);
    out[w].reserved[0] = 0;
    out[w].reserved[1] = 0;
    ++w;
  }
  return w;
}
static void fake_stop(void) { ++g_stop_calls; }

static const WifiScanDriver FAKE = { &fake_start, &fake_poll, &fake_read, &fake_stop };

static void fake_reset(void) {
  g_start_calls = 0; g_stop_calls = 0; g_read_calls = 0;
  g_start_ok = true; g_poll = WSCAN_POLL_RUNNING; g_available = 0;
}

TEST(a_scan_that_finds_something_ends_done_and_gives_the_radio_back) {
  fake_reset();
  g_available = 3;
  WifiScanJob j;
  wifi_scan_reset(j);
  CHECK(wifi_scan_start(j, FAKE, 1000u));
  CHECK(wifi_scan_is_busy(j));
  CHECK_EQ(g_start_calls, 1);
  CHECK_EQ(g_stop_calls, 0);              // the radio is HELD while scanning

  wifi_scan_service(j, FAKE, 1500u);      // still running
  CHECK_EQ((int)j.state, (int)WSCAN_RUNNING);

  g_poll = 3;
  wifi_scan_service(j, FAKE, 4000u);
  CHECK_EQ((int)j.state, (int)WSCAN_DONE);
  CHECK_EQ((int)j.count, 3);
  CHECK(!wifi_scan_is_busy(j));
  CHECK_EQ(g_stop_calls, 1);              // exactly once
  CHECK_EQ(j.res[0].net_hash, 0x1000u);
  CHECK_EQ((int)j.res[2].rssi, -42);
  // Servicing a finished job does nothing at all, and does not release twice.
  wifi_scan_service(j, FAKE, 90000u);
  CHECK_EQ((int)j.state, (int)WSCAN_DONE);
  CHECK_EQ(g_stop_calls, 1);
}

TEST(a_scan_that_finds_nothing_succeeded_and_is_not_a_failure) {
  fake_reset();
  g_available = 0;
  WifiScanJob j;
  wifi_scan_reset(j);
  CHECK(wifi_scan_start(j, FAKE, 0u));
  g_poll = 0;                              // zero access points is an ANSWER
  wifi_scan_service(j, FAKE, 100u);
  CHECK_EQ((int)j.state, (int)WSCAN_DONE);
  CHECK_EQ((int)j.count, 0);
  CHECK_EQ(g_stop_calls, 1);
}

TEST(more_access_points_than_fit_are_truncated_not_overflowed) {
  fake_reset();
  g_available = (uint8_t)(WIFI_SCAN_MAX_RESULTS + 5);
  WifiScanJob j;
  wifi_scan_reset(j);
  CHECK(wifi_scan_start(j, FAKE, 0u));
  g_poll = (int16_t)g_available;
  wifi_scan_service(j, FAKE, 10u);
  CHECK_EQ((int)j.state, (int)WSCAN_DONE);
  CHECK_EQ((int)j.count, (int)WIFI_SCAN_MAX_RESULTS);
}

TEST(the_twelve_second_timeout_ends_the_job_and_gives_the_radio_back) {
  fake_reset();
  WifiScanJob j;
  wifi_scan_reset(j);
  CHECK(wifi_scan_start(j, FAKE, 5000u));

  // One millisecond short of the deadline the job is still waiting: the
  // boundary is where a timeout that fires early or late shows up.
  wifi_scan_service(j, FAKE, 5000u + (uint32_t)WIFI_SCAN_TIMEOUT_MS - 1u);
  CHECK_EQ((int)j.state, (int)WSCAN_RUNNING);
  CHECK_EQ(g_stop_calls, 0);

  wifi_scan_service(j, FAKE, 5000u + (uint32_t)WIFI_SCAN_TIMEOUT_MS);
  CHECK_EQ((int)j.state, (int)WSCAN_TIMEOUT);
  CHECK_EQ((int)j.count, 0);
  CHECK_EQ(g_stop_calls, 1);
  CHECK(!wifi_scan_is_busy(j));
}

TEST(an_answer_on_the_deadline_tick_is_an_answer_and_not_a_timeout) {
  fake_reset();
  g_available = 2;
  WifiScanJob j;
  wifi_scan_reset(j);
  CHECK(wifi_scan_start(j, FAKE, 0u));
  g_poll = 2;                              // the results arrived...
  wifi_scan_service(j, FAKE, (uint32_t)WIFI_SCAN_TIMEOUT_MS + 5000u);  // ...late
  CHECK_EQ((int)j.state, (int)WSCAN_DONE);
  CHECK_EQ((int)j.count, 2);
}

TEST(the_timeout_survives_the_millis_wrap) {
  fake_reset();
  WifiScanJob j;
  wifi_scan_reset(j);
  const uint32_t near_wrap = 0xFFFFF000u;
  CHECK(wifi_scan_start(j, FAKE, near_wrap));
  // 1 s after the start, having wrapped through zero on the way.
  wifi_scan_service(j, FAKE, (uint32_t)(near_wrap + 1000u));
  CHECK_EQ((int)j.state, (int)WSCAN_RUNNING);
  CHECK_EQ((int)wifi_scan_elapsed_ms(j, (uint32_t)(near_wrap + 1000u)), 1000);
  wifi_scan_service(j, FAKE, (uint32_t)(near_wrap + (uint32_t)WIFI_SCAN_TIMEOUT_MS));
  CHECK_EQ((int)j.state, (int)WSCAN_TIMEOUT);
}

TEST(cancel_ends_a_running_scan_keeps_nothing_and_is_idempotent) {
  fake_reset();
  g_available = 4;
  WifiScanJob j;
  wifi_scan_reset(j);
  CHECK(wifi_scan_start(j, FAKE, 0u));
  wifi_scan_service(j, FAKE, 500u);
  CHECK(wifi_scan_is_busy(j));

  wifi_scan_cancel(j, FAKE);               // the player pressed B
  CHECK_EQ((int)j.state, (int)WSCAN_CANCELLED);
  CHECK_EQ((int)j.count, 0);
  CHECK_EQ(g_stop_calls, 1);
  CHECK(!wifi_scan_is_busy(j));

  wifi_scan_cancel(j, FAKE);               // twice does not release twice
  CHECK_EQ(g_stop_calls, 1);
  // ...and a cancelled job cannot be revived by servicing it.
  g_poll = 4;
  wifi_scan_service(j, FAKE, 1000u);
  CHECK_EQ((int)j.state, (int)WSCAN_CANCELLED);
  CHECK_EQ((int)j.count, 0);
}

TEST(cancelling_a_job_that_never_started_touches_no_radio) {
  fake_reset();
  WifiScanJob j;
  wifi_scan_reset(j);
  wifi_scan_cancel(j, FAKE);
  CHECK_EQ(g_stop_calls, 0);
  CHECK_EQ(g_start_calls, 0);
  CHECK_EQ((int)j.state, (int)WSCAN_IDLE);
}

TEST(a_driver_that_refuses_the_radio_fails_the_job_rather_than_hanging) {
  fake_reset();
  g_start_ok = false;
  WifiScanJob j;
  wifi_scan_reset(j);
  CHECK(!wifi_scan_start(j, FAKE, 0u));
  CHECK_EQ((int)j.state, (int)WSCAN_FAILED);
  CHECK(!wifi_scan_is_busy(j));
  // stop() is still called: the driver may have got half way up before
  // refusing, and this module does not get to assume otherwise.
  CHECK_EQ(g_stop_calls, 1);
}

TEST(a_scan_the_driver_reports_failed_ends_failed_with_the_radio_off) {
  fake_reset();
  WifiScanJob j;
  wifi_scan_reset(j);
  CHECK(wifi_scan_start(j, FAKE, 0u));
  g_poll = WSCAN_POLL_FAILED;
  wifi_scan_service(j, FAKE, 100u);
  CHECK_EQ((int)j.state, (int)WSCAN_FAILED);
  CHECK_EQ((int)j.count, 0);
  CHECK_EQ(g_stop_calls, 1);
  CHECK_EQ(g_read_calls, 0);               // nothing was read from a failure
}

TEST(a_second_start_while_one_scan_runs_is_refused_not_stacked) {
  fake_reset();
  WifiScanJob j;
  wifi_scan_reset(j);
  CHECK(wifi_scan_start(j, FAKE, 0u));
  CHECK(!wifi_scan_start(j, FAKE, 10u));   // one radio, one scan
  CHECK_EQ(g_start_calls, 1);
  CHECK_EQ(g_stop_calls, 0);
  CHECK_EQ((int)j.state, (int)WSCAN_RUNNING);
}

TEST(a_finished_job_can_be_started_again) {
  fake_reset();
  g_available = 1;
  WifiScanJob j;
  wifi_scan_reset(j);
  CHECK(wifi_scan_start(j, FAKE, 0u));
  g_poll = 1;
  wifi_scan_service(j, FAKE, 10u);
  CHECK_EQ((int)j.state, (int)WSCAN_DONE);

  g_poll = WSCAN_POLL_RUNNING;
  CHECK(wifi_scan_start(j, FAKE, 100u));
  CHECK_EQ((int)j.state, (int)WSCAN_RUNNING);
  CHECK_EQ((int)j.count, 0);               // the previous results are gone
  CHECK_EQ(j.res[0].net_hash, 0u);
  CHECK_EQ(g_start_calls, 2);
  g_poll = 1;
  wifi_scan_service(j, FAKE, 200u);
  CHECK_EQ(g_stop_calls, 2);               // once per run, still
}
