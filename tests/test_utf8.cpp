// =============================================================================
//  Pebblebol host tests - test_utf8.cpp
//  core/utf8.cpp: THE codepoint rule, driven for the first time.
//
//  WHY IT IS ITS OWN BINARY. Until P10-C4 this rule existed three times - in
//  ui/render.cpp (device only, compiled by no host binary), in
//  tests/fakes/gfx_fb.cpp (host only, so the firmware never ran it) and in
//  game/pebble.cpp - and NOT ONE of the three was executed against a malformed
//  sequence by anything in this repository. The whole suite puts only ASCII in
//  a name: tests/test_screens.cpp's fixture_maxed uses "ABCDEFGHIJKL", the Box
//  fixture the same, and tests/test_discovery.cpp asserts a high byte survives
//  the wire and never draws it.
// =============================================================================
#include "nt_test.h"

#include <stdlib.h>
#include <string.h>

#include "core/utf8.h"

// The five characters this product's repertoire actually uses beyond ASCII,
// as UTF-8 and as the Latin-1 BYTES a stored name holds them in.
#define U8_NTILDE   "\xC3\xB1"        // U+00F1, two bytes
#define L1_NTILDE   "\xF1"            // the same character, one byte
#define U8_OACUTE   "\xC3\xB3"        // U+00F3
#define L1_OACUTE   "\xF3"

// =============================================================================
//  u8_len(): the half every earlier copy left out
// =============================================================================
TEST(a_lead_byte_is_not_believed_until_its_continuation_bytes_are_there) {
  // ASCII and the terminator.
  CHECK_EQ((int)u8_len(""), 0);
  CHECK_EQ((int)u8_len("A"), 1);
  CHECK_EQ((int)u8_len(nullptr), 0);

  // Well-formed sequences of every length.
  CHECK_EQ((int)u8_len("\xC3\xB1"), 2);
  CHECK_EQ((int)u8_len("\xE2\x82\xAC"), 3);
  CHECK_EQ((int)u8_len("\xF0\x9F\x98\x80"), 4);

  // THE DEFECT. Each of these lead bytes ADVERTISES a length the string does
  // not have. The old rule returned 2/3/4 and every caller stepped that far -
  // over the terminator, out of the buffer.
  CHECK_EQ((int)u8_len("\xC3"), 1);            // 2-byte lead, nothing after it
  CHECK_EQ((int)u8_len("\xE2\x82"), 1);        // 3-byte lead, one continuation
  CHECK_EQ((int)u8_len("\xF0\x9F\x98"), 1);    // 4-byte lead, two
  CHECK_EQ((int)u8_len(L1_NTILDE), 1);         // the byte a nickname really holds
  CHECK_EQ((int)u8_len("\xF1o"), 1);           // "...o" is not a continuation

  // Junk that begins nothing at all.
  CHECK_EQ((int)u8_len("\x80"), 1);            // a stray continuation byte
  CHECK_EQ((int)u8_len("\xFF"), 1);
  CHECK_EQ((int)u8_len("\xF8\x80\x80\x80\x80"), 1);   // 5-byte form: not UTF-8
}

// The property that makes every walk in the tree terminate, stated as a sweep
// over EVERY byte value in both positions rather than as five examples.
TEST(no_byte_pair_can_make_a_walk_step_over_its_own_terminator) {
  for (int b = 1; b < 256; ++b) {
    char one[2] = { (char)b, '\0' };
    const uint8_t n1 = u8_len(one);
    // A one-byte string may only ever be walked one byte.
    if (n1 != 1u) {
      fprintf(stderr, "  lead byte 0x%02X answered %u on a ONE byte string\n",
              (unsigned)b, (unsigned)n1);
    }
    CHECK_EQ((int)n1, 1);

    for (int c = 1; c < 256; ++c) {
      char two[3] = { (char)b, (char)c, '\0' };
      const uint8_t n2 = u8_len(two);
      CHECK(n2 >= 1u && n2 <= 2u);
      // strlen is 2, so a step of n2 always lands inside the buffer.
      CHECK(n2 <= strlen(two));
    }
  }
}

// =============================================================================
//  u8_fit(): truncation on a codepoint boundary
// =============================================================================
TEST(a_truncation_never_leaves_half_a_character_behind) {
  const char* s = "Ni" U8_NTILDE "o";          // 5 bytes, 4 characters
  CHECK_EQ((int)strlen(s), 5);
  CHECK_EQ((int)u8_count(s), 4);

  CHECK_EQ((int)u8_fit(s, 0), 0);
  CHECK_EQ((int)u8_fit(s, 1), 1);              // "N"
  CHECK_EQ((int)u8_fit(s, 2), 2);              // "Ni"
  // THE ONE THAT MATTERS: room for three bytes, and the third byte is the LEAD
  // of a two-byte sequence. The answer must be 2, not 3.
  CHECK_EQ((int)u8_fit(s, 3), 2);
  CHECK_EQ((int)u8_fit(s, 4), 4);              // "Ni" + the whole n-tilde
  CHECK_EQ((int)u8_fit(s, 5), 5);
  CHECK_EQ((int)u8_fit(s, 99), 5);             // never past the terminator

  // Every prefix u8_fit() returns is itself well-formed.
  for (uint16_t room = 0; room <= 8; ++room) {
    char buf[8];
    const uint16_t n = u8_fit(s, room);
    CHECK(n <= 5u);
    memcpy(buf, s, n);
    buf[n] = '\0';
    CHECK(u8_well_formed(buf));
    // and it is a PREFIX: the same rule game/pebble.cpp's joiner depends on.
    CHECK_EQ(memcmp(buf, s, n), 0);
  }
}

// THE UNIFICATION IS NOT A SECOND BUG FIX AND THIS TEST IS THE RECEIPT.
//
// I expected the forward walk to be STRICTER than "back off continuation bytes
// after a blind cut" and wrote a case to show it. The case was wrong: the old
// rule inspects s[n], the byte just PAST the prefix, so on well-formed input it
// already lands on a codepoint boundary every time. So the honest claim is the
// narrow one - u8_len()'s validation is the defect fix; u8_fit() replacing three
// private copies is a UNIFICATION that changes no answer - and this sweep is
// what makes that claim checkable rather than asserted.
//
// Same shape as P10-C3's "correcting the host fake moved ZERO of the 65
// goldens": the value of a receipt is that it could have come out the other way.
TEST(the_shared_rule_answers_exactly_what_the_three_private_copies_did) {
  static const char* kInputs[] = {
    "", "A", "abc", "ABCDEFGHIJKL",
    "Ni" U8_NTILDE "o", "Rafag" U8_OACUTE "n", U8_NTILDE "a" "rr" U8_OACUTE "n",
    U8_NTILDE U8_NTILDE U8_NTILDE U8_NTILDE,
    "\xE2\x82\xAC 12", "\xF0\x9F\x98\x80x",
    "a" U8_NTILDE "b" U8_OACUTE "c"
  };
  uint32_t compared = 0;
  for (size_t i = 0; i < sizeof kInputs / sizeof kInputs[0]; ++i) {
    const char* s = kInputs[i];
    CHECK(u8_well_formed(s));                  // the sweep is over VALID input
    const uint16_t len = (uint16_t)strlen(s);
    for (uint16_t room = 0; room <= len + 3u; ++room) {
      // game/pebble.cpp's utf8_fit(), byte for byte as it stood before P10-C4.
      uint16_t old_n = 0;
      while (old_n < room && s[old_n] != '\0') ++old_n;
      while (old_n > 0u && ((uint8_t)s[old_n] & 0xC0u) == 0x80u) --old_n;

      if (u8_fit(s, room) != old_n) {
        fprintf(stderr, "  \"%s\" room=%u: shared rule %u, old rule %u\n",
                s, (unsigned)room, (unsigned)u8_fit(s, room), (unsigned)old_n);
      }
      CHECK_EQ((int)u8_fit(s, room), (int)old_n);
      ++compared;
    }
  }
  CHECK(compared >= 100u);                     // anti-vacuity on the sweep
}

// Where the two DO part company is malformed input, and the parting is not the
// direction I guessed. A run of stray continuation bytes drives the old rule's
// back-off loop all the way to zero - it throws the whole string away - while
// the shared rule treats each junk byte as one character and returns a prefix.
// Nothing in the tree feeds it such a string; it is recorded because a test
// that only covers the inputs the code sees today is a test of today.
TEST(the_two_rules_part_company_only_on_input_that_is_already_broken) {
  const char* junk = "\x80\x80\x80";
  uint16_t old_n = 2;
  while (old_n > 0u && ((uint8_t)junk[old_n] & 0xC0u) == 0x80u) --old_n;
  CHECK_EQ((int)old_n, 0);                     // the old rule keeps nothing
  CHECK_EQ((int)u8_fit(junk, 2), 2);           // the shared rule keeps a prefix
  CHECK(!u8_well_formed(junk));
}

// =============================================================================
//  u8_from_latin1(): the one crossing between a STORED name and a DRAWN one
// =============================================================================
TEST(a_latin1_name_becomes_utf8_and_is_cut_on_a_character_boundary) {
  char out[32];

  CHECK_EQ((int)u8_from_latin1(out, sizeof out, "ABC"), 3);
  CHECK_EQ(strcmp(out, "ABC"), 0);

  // One Latin-1 byte in, two UTF-8 bytes out, and the same character.
  CHECK_EQ((int)u8_from_latin1(out, sizeof out, "Ni" L1_NTILDE "o"), 5);
  CHECK_EQ(strcmp(out, "Ni" U8_NTILDE "o"), 0);
  CHECK_EQ((int)u8_count(out), 4);
  CHECK(u8_well_formed(out));

  // THE WORST CASE THE SCHEMA ALLOWS: twelve accented characters, which is
  // twelve stored bytes and TWENTY-FOUR drawn ones. A cap sized in stored
  // bytes would cut this in half, character by character.
  const char* wide = L1_NTILDE L1_NTILDE L1_NTILDE L1_NTILDE L1_NTILDE L1_NTILDE
                     L1_NTILDE L1_NTILDE L1_NTILDE L1_NTILDE L1_NTILDE L1_NTILDE;
  CHECK_EQ((int)strlen(wide), 12);
  CHECK_EQ((int)u8_from_latin1(out, sizeof out, wide), 24);
  CHECK(u8_well_formed(out));
  CHECK_EQ((int)u8_count(out), 12);

  // A cap with room for an odd number of bytes must drop the WHOLE character,
  // never write the lead and stop.
  for (uint16_t cap = 1; cap <= 26; ++cap) {
    char small[32];
    memset(small, 0x7F, sizeof small);
    const uint16_t n = u8_from_latin1(small, cap, wide);
    CHECK(n + 1u <= cap);
    CHECK_EQ((int)small[n], 0);                // always terminated
    CHECK_EQ((int)(n % 2u), 0);                // whole two-byte characters only
    CHECK(u8_well_formed(small));
    // nothing was written past the terminator
    CHECK_EQ((int)(uint8_t)small[cap > 0 ? cap : 1], (int)(uint8_t)0x7F);
  }

  // cap 0 writes nothing at all - there is nowhere to put a terminator.
  char guard[2] = { 0x7F, 0x7F };
  CHECK_EQ((int)u8_from_latin1(guard, 0, "A"), 0);
  CHECK_EQ((int)guard[0], 0x7F);

  // A NULL source is an empty string, not a crash.
  CHECK_EQ((int)u8_from_latin1(out, sizeof out, nullptr), 0);
  CHECK_EQ((int)out[0], 0);
}

TEST(the_repertoire_this_product_actually_uses_round_trips) {
  // Every non-ASCII byte core/strings_es.h and game/validate.cpp allow.
  static const uint8_t kRepertoire[] = {
    0xA1, 0xAA, 0xB0, 0xB7, 0xBA, 0xBF,
    0xC1, 0xC9, 0xCD, 0xD1, 0xD3, 0xDA, 0xDC,
    0xE1, 0xE9, 0xED, 0xF1, 0xF3, 0xFA, 0xFC
  };
  for (size_t i = 0; i < sizeof kRepertoire; ++i) {
    char src[2] = { (char)kRepertoire[i], '\0' };
    char dst[8];
    CHECK_EQ((int)u8_from_latin1(dst, sizeof dst, src), 2);
    CHECK(u8_well_formed(dst));
    CHECK_EQ((int)u8_len(dst), 2);
    CHECK_EQ((int)u8_count(dst), 1);
    // and the codepoint really is the Latin-1 one, bit for bit
    const uint32_t cp = (uint32_t)(((uint8_t)dst[0] & 0x1Fu) << 6) |
                        (uint32_t)((uint8_t)dst[1] & 0x3Fu);
    CHECK_EQ((int)cp, (int)kRepertoire[i]);
  }
}

TEST(u8_count_and_u8_well_formed_agree_with_the_length_rule) {
  CHECK_EQ((int)u8_count(""), 0);
  CHECK_EQ((int)u8_count("abc"), 3);
  CHECK_EQ((int)u8_count(U8_NTILDE U8_OACUTE), 2);
  // A malformed byte counts as one character and stops nothing.
  CHECK_EQ((int)u8_count(L1_NTILDE L1_OACUTE), 2);
  CHECK(!u8_well_formed(L1_NTILDE));
  CHECK(u8_well_formed(U8_NTILDE));
  CHECK(u8_well_formed(""));
  CHECK(u8_well_formed(nullptr));
}

// =============================================================================
//  THE HEAP-EXACT WALK - the shape AddressSanitizer can see
//
//  A static array is surrounded by other static arrays, so a one-byte over-read
//  reads a neighbour and nothing complains. tests/test_creator_api.cpp's
//  HeapBody makes the same argument for the HTTP reader: malloc the string at
//  EXACTLY its own length and the over-read becomes a heap error. This binary is
//  in the Makefile's ASAN_SET for that reason, and this is the case that uses it.
// =============================================================================
TEST(walking_a_name_that_ends_in_a_lone_lead_byte_stays_inside_its_allocation) {
  static const char* kEnders[] = {
    "\xC3", "\xE2", "\xF0", "\xF1", "\xFF", "\x80",
    "Ni\xF1", "abc\xF0\x9F", "\xC3\xA1\xC3"
  };
  for (size_t i = 0; i < sizeof kEnders / sizeof kEnders[0]; ++i) {
    const size_t n = strlen(kEnders[i]);
    char* heap = (char*)malloc(n + 1u);        // EXACTLY the string, no slack
    CHECK(heap != nullptr);
    if (!heap) continue;
    memcpy(heap, kEnders[i], n + 1u);
    // Each of these walks the string to its terminator. Before P10-C4 the last
    // step of every one of them jumped 2..4 bytes off the end of this block.
    (void)u8_count(heap);
    (void)u8_fit(heap, (uint16_t)(n + 4u));
    (void)u8_well_formed(heap);
    CHECK_EQ((int)u8_len(heap + n), 0);        // the terminator itself
    free(heap);
  }
}

// =============================================================================
//  P10-C6: THE THREE WRITERS, AT THE BOUNDARY, INTO A HEAP-EXACT DESTINATION
//
//  This file drove the READ path against a malloc sized to the string and
//  never drove the WRITE path against anything. u8_cat(), u8_cat_n() and
//  u8_cat_latin1() are the functions that copy an untrusted peer name or a
//  creator nickname into a fixed UI row, and their whole content is the room
//  arithmetic - `room = cap - 1u - n`, where cap counts the terminator.
//
//  Changing that to `cap - n` in either function is a one-past-the-end WRITE of
//  the NUL, and it survived the entire suite: ALL PASS 58/58, ASAN OK 6/6,
//  GATE OK. The three functions ARE executed at runtime by four screen
//  binaries through their shipped call sites - screen_box.cpp, screen_link.cpp,
//  screen_battle.cpp - so reaching a function is not testing it: every fixture
//  in the tree happens to hand them a destination with slack, so the stray byte
//  landed inside the caller's array and nothing reported it.
//
//  These cases walk every cap from 1 to the source length + 2 with the
//  destination malloc'd to exactly that cap, so the mutation is a
//  heap-buffer-overflow ASAN reports by line. test_utf8 is in ASAN_SET.
// =============================================================================
static void cat_at_every_cap(const char* seed, const char* src, bool latin1,
                             uint16_t max_chars, bool bounded) {
  const size_t need = strlen(seed) + strlen(src) + 2u;
  for (uint16_t cap = 1u; cap <= (uint16_t)need; ++cap) {
    char* d = (char*)malloc(cap);              // EXACTLY cap bytes, no slack
    CHECK(d != nullptr);
    if (!d) return;
    // Seed the destination the way a caller does: snprintf'd, then appended to.
    size_t k = 0;
    while (seed[k] != '\0' && k + 1u < (size_t)cap) { d[k] = seed[k]; ++k; }
    d[k] = '\0';
    uint16_t r;
    if (bounded)      r = u8_cat_n(d, cap, src, max_chars);
    else if (latin1)  r = u8_cat_latin1(d, cap, src);
    else              r = u8_cat(d, cap, src);
    // The contract: the return value IS the new length, the result is always
    // terminated inside the block, and it is always well-formed UTF-8.
    CHECK_EQ((int)r, (int)strlen(d));
    CHECK(r < cap);
    // WELL-FORMEDNESS IS CONDITIONAL ON THE SOURCE, and this assertion was
    // wrong before the code was: the sweep feeds a lone lead byte on purpose
    // (a name off the air can be anything), and an appender copying a
    // malformed source faithfully is correct, not broken. What it may never do
    // is SPLIT a sequence that was whole - which is what this says.
    if (u8_well_formed(seed) && u8_well_formed(src)) CHECK(u8_well_formed(d));
    if (bounded) CHECK(u8_count(d) <= (uint16_t)(u8_count(seed) + max_chars));
    free(d);
  }
}

TEST(the_three_appenders_never_write_past_a_destination_sized_to_the_byte) {
  static const char* kSeeds[] = { "", "1 ", "9 *", "ABCDEFGH" };
  static const char* kUtf8[]  = { "", "A", "ABCDEFGH", "\xC3\x91U",
                                  "\xC3\x81\xC3\x89\xC3\x8D\xC3\x93\xC3\x9A",
                                  "\xC3" };                    // a lone lead
  static const char* kL1[]    = { "", "A", "ABCDEFGH", "\xD1U",
                                  "\xC1\xC9\xCD\xD3\xDA\xDC\xD1\xC1\xC9\xCD\xD3\xDA" };
  for (size_t si = 0; si < sizeof kSeeds / sizeof kSeeds[0]; ++si) {
    for (size_t i = 0; i < sizeof kUtf8 / sizeof kUtf8[0]; ++i) {
      cat_at_every_cap(kSeeds[si], kUtf8[i], false, 0u, false);
      for (uint16_t mc = 0u; mc <= 4u; ++mc)
        cat_at_every_cap(kSeeds[si], kUtf8[i], false, mc, true);
    }
    for (size_t i = 0; i < sizeof kL1 / sizeof kL1[0]; ++i)
      cat_at_every_cap(kSeeds[si], kL1[i], true, 0u, false);
  }
}

// AND THE BOUNDARY ITSELF, NAMED. A cap of exactly seed + src + 1 must take the
// whole source; one byte less must take one character less and never a partial
// sequence. This is the case the mutation `room = cap - n` fails on arithmetic
// alone, without needing a sanitiser to be linked.
TEST(one_byte_of_cap_is_one_byte_of_text_and_the_terminator_is_not_text) {
  char d[16];
  snprintf(d, sizeof d, "%s", "AB");
  CHECK_EQ((int)u8_cat(d, 6u, "CDEFGH"), 5);   // cap 6 -> "ABCDE" + NUL
  CHECK_EQ((int)strlen(d), 5);
  snprintf(d, sizeof d, "%s", "AB");
  CHECK_EQ((int)u8_cat(d, 3u, "CDEFGH"), 2);   // cap 3 -> no room at all
  CHECK_EQ((int)strlen(d), 2);
  // A two-byte character is taken whole or not at all: cap 5 leaves room for
  // two text bytes after "AB", which is exactly one accented character.
  snprintf(d, sizeof d, "%s", "AB");
  CHECK_EQ((int)u8_cat_latin1(d, 5u, "\xD1\xD1"), 4);
  CHECK(u8_well_formed(d));
  snprintf(d, sizeof d, "%s", "AB");
  CHECK_EQ((int)u8_cat_latin1(d, 4u, "\xD1\xD1"), 2);   // no whole char fits
  CHECK(u8_well_formed(d));
}

// THE CROSSING BACK (P10-C6). u8_to_latin1() is what puts a name on the wire;
// see core/utf8.h for why the beacon field is Latin-1 and what went wrong when
// it was handed UTF-8.
TEST(the_crossing_back_is_the_inverse_of_the_crossing_out) {
  for (unsigned b = 1u; b < 256u; ++b) {
    char l1[2]  = { (char)b, '\0' };
    char utf[8];
    char back[8];
    (void)u8_from_latin1(utf, (uint16_t)sizeof utf, l1);
    CHECK(u8_well_formed(utf));
    CHECK_EQ((int)u8_to_latin1(back, (uint16_t)sizeof back, utf), 1);
    CHECK_EQ((int)(unsigned char)back[0], (int)b);      // every byte survives
  }
  // A codepoint with no Latin-1 byte becomes '?' rather than half a sequence.
  char out[8];
  CHECK_EQ((int)u8_to_latin1(out, (uint16_t)sizeof out, "\xE2\x82\xAC"), 1);
  CHECK_EQ((int)out[0], (int)'?');
  // A truncated sequence stops the copy inside the block rather than walking
  // past the terminator - the same refusal u8_len() makes.
  const char* lead = "A\xC3";
  const size_t n = strlen(lead);
  char* heap = (char*)malloc(n + 1u);
  CHECK(heap != nullptr);
  if (heap) {
    memcpy(heap, lead, n + 1u);
    char dst[8];
    (void)u8_to_latin1(dst, (uint16_t)sizeof dst, heap);
    free(heap);
  }
  // cap 1 has room for the terminator and nothing else.
  CHECK_EQ((int)u8_to_latin1(out, 1u, "ABC"), 0);
  CHECK_EQ((int)out[0], 0);
}
