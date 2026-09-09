// =============================================================================
//  tests/tools/creator_decode.cpp - THE DEVICE END OF THE BROWSER TEST (P8-C4).
//
//  Reads ONE creator upload document on stdin and prints what THIS FIRMWARE
//  makes of it: the reader's code, the validator's code, and every field of the
//  CustomSpeciesRec that came out, including both decoded sprite frames.
//
//  WHY IT EXISTS. tools/page_test.mjs drives the shipped page in a real
//  headless browser and captures the body it POSTs. Nothing in JavaScript can
//  say whether those bytes mean what the device thinks they mean - in
//  particular whether the page's XBM bit order is the device's - so the bytes
//  are handed to THE REAL networking/creator_parse.cpp and THE REAL
//  game/validate.cpp, linked here with no reimplementation of either.
//
//  WHERE IT DIFFERS FROM app_setup(), STATED RATHER THAN LEFT TO BE FOUND
//  (this project's recurring defect is the fixture that tests a firmware
//  nobody flashes):
//
//   1. THERE IS NO SOCKET AND NO WebServer. The body arrives on stdin already
//      whole, so nothing here exercises networking/creator_body.cpp's cap or
//      the core's RAW_* sequence. tests/test_creator_api.cpp drives those byte
//      by byte; this binary starts where they finish.
//
//   2. slot AND budget_used ARE FILLED THE WAY networking/creator_server.cpp
//      FILLS THEM, and the two lines below are copied from read_and_judge():
//      the parser deliberately leaves both zero, the DEVICE picks the slot and
//      RECOMPUTES the cost, and a validator run without those two steps would
//      answer VR_CS_BUDGET_MISMATCH on a perfectly good document. Slot 0 is
//      used because the free-slot search is game/species_custom.cpp's and it
//      needs a registry this binary has no reason to build.
//
//   3. NOTHING IS WRITTEN. No flash, no Box, no registry. Whether an accepted
//      definition can be FILED is tests/test_creator_api.cpp section 7 and
//      tests/test_persistence.cpp; this binary answers only "is the document
//      the device's own reader and validator accept".
//
//  Built by `make -C tests pagetool`, not by `make check`: it takes input from
//  outside the tree, so it is a simulator in the sense tests/tools/ means.
//
//  All identifiers and comments English.
// =============================================================================
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "game/validate.h"
#include "networking/creator_parse.h"
#include "persistence/save_schema.h"

static void put_hex(const char* key, const uint8_t* p, uint32_t n)
{
  printf("%s=", key);
  for (uint32_t i = 0; i < n; ++i) printf("%02x", p[i]);
  printf("\n");
}

// -----------------------------------------------------------------------------
//  --pct : the section 36 percentage, for every "S A" pair on stdin.
//
//  The page implements creator_power_pct()'s arithmetic and tools/page_test.mjs
//  compares the two ACROSS THE WHOLE INPUT DOMAIN rather than at the one input
//  a drawn Bug happens to produce. That is not thoroughness for its own
//  sake: the first version of that check compared a single pair, and replacing
//  the page's integer form with balance.json's FLOAT sentence left it green -
//  the two agree on all 14,837 pairs, so one input can never tell them apart.
//  A wrong CONSTANT or a missing saturation is what this mode can see.
// -----------------------------------------------------------------------------
static int pct_mode(void)
{
  unsigned s = 0u, a = 0u;
  while (scanf("%u %u", &s, &a) == 2)
    printf("%u\n", (unsigned)creator_power_pct((uint16_t)s, (uint16_t)a));
  return 0;
}

int main(int argc, char** argv)
{
  if (argc > 1 && strcmp(argv[1], "--pct") == 0) return pct_mode();

  std::vector<uint8_t> body;
  int ch;
  while ((ch = getchar()) != EOF) body.push_back((uint8_t)ch);

  CustomSpeciesRec rec;
  uint16_t pin = 0u;
  const CpErr e = cp_parse_species(body.data(), (uint32_t)body.size(), rec, pin);

  printf("bytes=%u\n", (unsigned)body.size());
  printf("cp=%s\n", cp_err_name(e));
  printf("pin=%u\n", (unsigned)pin);
  if (e != CP_OK) { printf("vr=-\n"); return 0; }

  // The two lines networking/creator_server.cpp's read_and_judge() runs before
  // it calls the validator. See note 2 in the banner.
  uint16_t stat_used = 0u, attack_used = 0u;
  creator_cost_of(rec, stat_used, attack_used);
  rec.budget_used = attack_used;

  const VReject r = validate_custom_species(rec);
  printf("vr=%s\n", validate_reject_name(r));
  printf("name=%s\n", rec.name);
  put_hex("name_bytes", (const uint8_t*)rec.name, (uint32_t)strlen(rec.name));
  printf("type=%u\n", (unsigned)rec.type);
  printf("base=%u,%u,%u,%u\n", (unsigned)rec.base[0], (unsigned)rec.base[1],
         (unsigned)rec.base[2], (unsigned)rec.base[3]);
  printf("moves=%u,%u,%u,%u\n", (unsigned)rec.moves[0], (unsigned)rec.moves[1],
         (unsigned)rec.moves[2], (unsigned)rec.moves[3]);
  printf("compat=%u\n", (unsigned)rec.compat_group);
  printf("stat=%u\n", (unsigned)stat_used);
  printf("atk=%u\n", (unsigned)attack_used);
  printf("pct=%u\n", (unsigned)creator_power_pct(stat_used, attack_used));
  for (uint8_t f = 0; f < (uint8_t)CS_SPRITE_FRAMES; ++f) {
    char key[16];
    snprintf(key, sizeof key, "sprite%u", (unsigned)f);
    put_hex(key, rec.sprite[f], (uint32_t)CS_SPRITE_BYTES);
  }
  return 0;
}
