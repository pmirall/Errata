// =============================================================================
//  Pebblebol host tests - test_care_golden.cpp
//  Pins the care trajectory of the legacy simulation (plan P2-C2, §4.2).
//
//  Fixed seeds, a fixed SimEnv and a scripted six hours: a meal every hour,
//  CLEAN at 2 h, PLAY at 3 h. Every simulated minute the test hashes the six
//  stats in StatId order plus the legacy PF_* word and appends one line; the
//  whole transcript must match tests/golden/care_v2.txt byte for byte.
//
//  P2-C10 moved the sim onto PebbleInstance. The hash is re-derived THROUGH the
//  field map (stat[] -> care[] under CareId, ST_BOND -> the sim's RAM copy,
//  flags -> the SimView word), so the bytes hashed are the same bytes in the
//  same order and the golden file did NOT change. This is not a retune.
//
//  P3-C2b RE-RECORDED IT, AND THE CARE NUMBERS DID NOT MOVE. That commit
//  deleted the light mechanic, so PF_LIGHT_ON (bit 0x0004) - which
//  sim_new_pet() used to set and which is part of the hashed flags word -
//  is gone from every line. Verified before recording by hashing the six
//  stats WITHOUT the flags word on both sides of the change: the transcripts
//  were byte-identical, events and action results included. The scripted six
//  hours run 10:00 -> 16:00, which is broad daylight on day 100 under the new
//  daylight window too, so nothing here ever sleeps and no rate changed.
//
//  P3-C1 RE-RECORDED IT. That commit moved care onto the hours scale of spec
//  section 27 (hunger -12.000 -> -4.200 milli/h, happiness -8.000 -> -3.000,
//  energy awake -9.000 -> -6.000, per-poop cleanliness -6.000 -> -1.000, one
//  gated health bleed instead of four instant ones) and deleted the age-based
//  STAGE_MULT_* multipliers, so every hashed minute after t=000 moved. Only a
//  `retune:` commit - or one that changes what is HASHED, as P3-C2b did - may
//  regenerate it:  make -C tests golden  (or ./bin/test_care_golden --record).
//
//  P3-C5 RENAMED IT, AND NOT ONE TRAJECTORY LINE MOVED. It was
//  test_sim_golden.cpp over golden/sim_v1.txt, where the `v1` meant the
//  legacy v1 SIMULATION this file was first recorded from. Two retunes later
//  that is no longer what it pins - P3-C1 moved care onto the hours scale -
//  so the plan's own name for it applies: test_care_golden.cpp over
//  golden/care_v2.txt, the second care model. The rename touched the file's
//  HEADER LINE and nothing else; every hashed minute is byte-identical, which
//  is exactly what the diff and this test passing together prove.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "game/sim.h"
#include "game/genome.h"

#define GOLDEN_REL_PATH   "golden/care_v2.txt"
#define GOLDEN_SEED       0xC0FFEEu
#define GOLDEN_EPOCH0     1700000000u     // any sane epoch (>= NT_EPOCH_SANE_MIN)
#define GOLDEN_MINUTES    360u            // 6 h
#define GOLDEN_LINE_MAX   64
#define GOLDEN_TEXT_MAX   ((GOLDEN_MINUTES + 2u) * GOLDEN_LINE_MAX)

// FNV-1a, 32-bit. Cheap, portable, good enough to notice a single flipped bit.
static uint32_t fnv1a(uint32_t h, const void* p, size_t n) {
  const uint8_t* b = (const uint8_t*)p;
  for (size_t i = 0; i < n; i++) {
    h ^= b[i];
    h *= 16777619u;
  }
  return h;
}

static uint32_t hash_pet(void) {
  int32_t stat[ST_COUNT];
  for (uint8_t i = 0; i < (uint8_t)ST_COUNT; ++i) {
    stat[i] = sim_stat_milli((StatId)i);          // the v1 stat[] order, exactly
  }
  const uint16_t flags = sim_view()->flags;
  uint32_t h = 2166136261u;
  h = fnv1a(h, stat, sizeof(stat));
  h = fnv1a(h, &flags, sizeof(flags));
  return h;
}

// Appends one transcript line. `act` is 0 when no action was attempted.
static void emit(char* text, size_t cap, uint32_t minute,
                 uint32_t ev, uint8_t act, const ActionResult* r) {
  char line[GOLDEN_LINE_MAX];
  if (act == 0) {
    snprintf(line, sizeof line, "t=%03u h=%08X ev=%08X act=-\n",
             (unsigned)minute, (unsigned)hash_pet(), (unsigned)ev);
  } else {
    snprintf(line, sizeof line, "t=%03u h=%08X ev=%08X act=%u:%u/%u\n",
             (unsigned)minute, (unsigned)hash_pet(), (unsigned)ev,
             (unsigned)act, (unsigned)r->ok, (unsigned)r->err);
  }
  const size_t used = strlen(text);
  if (used + strlen(line) + 1 <= cap) strcat(text, line);
}

// Runs the script and renders the transcript into `text`.
static void golden_run(char* text, size_t cap) {
  text[0] = '\0';

  genome_seed(GOLDEN_SEED);
  const Genome g = genome_genesis();
  sim_seed(GOLDEN_SEED);

  static PebbleInstance save;
  memset(&save, 0, sizeof(save));
  sim_bind(save);
  sim_new_pet(g, GOLDEN_EPOCH0, 0);
  sim_hatch();

  SimEnv env;
  sim_env_defaults(env);
  env.now_epoch   = GOLDEN_EPOCH0;
  env.day_of_year = 100;
  env.local_hour  = 10;               // 10:00 -> 16:00, clear of the sleep window
  env.local_min   = 0;
  env.clock_valid = 1;
  sim_set_env(env);

  char hex[33];
  genome_to_hex32(g, hex);
  char head[GOLDEN_LINE_MAX];
  snprintf(head, sizeof head, "care_v2 seed=%08X genome=%s\n", (unsigned)GOLDEN_SEED, hex);
  strcat(text, head);

  emit(text, cap, 0, sim_take_events(), 0, nullptr);

  for (uint32_t m = 1; m <= GOLDEN_MINUTES; m++) {
    sim_tick(60);
    env.now_epoch += 60;
    if (++env.local_min >= 60) { env.local_min = 0; env.local_hour++; }
    sim_set_env(env);

    uint8_t act = 0;
    if (m % 60u == 0u)  act = ACT_FEED_MEAL;   // hourly meal
    else if (m == 121u) act = ACT_CLEAN;       // 2 h
    else if (m == 181u) act = ACT_PLAY;        // 3 h

    ActionResult r;
    memset(&r, 0, sizeof(r));
    if (act != 0) (void)sim_apply_action((ActionId)act, r);

    emit(text, cap, m, sim_take_events(), act, act ? &r : nullptr);
  }
}

static char s_text[GOLDEN_TEXT_MAX];
static char s_file[GOLDEN_TEXT_MAX];

TEST(care_golden_matches_recorded_trajectory) {
  golden_run(s_text, sizeof s_text);
  CHECK(strlen(s_text) > 0);

  char path[512];
  nt_path(GOLDEN_REL_PATH, path, sizeof path);

  if (nt_flag("--record")) {
    FILE* f = fopen(path, "wb");
    CHECK(f != nullptr);
    if (f) {
      fputs(s_text, f);
      fclose(f);
      printf("  recorded %s (%zu bytes)\n", path, strlen(s_text));
    }
    return;
  }

  FILE* f = fopen(path, "rb");
  CHECK(f != nullptr);
  if (!f) {
    fprintf(stderr, "  missing %s - record it with --record\n", path);
    return;
  }
  const size_t n = fread(s_file, 1, sizeof s_file - 1, f);
  fclose(f);
  s_file[n] = '\0';

  CHECK_EQ(n, strlen(s_text));

  // Line-by-line so a mismatch names the simulated minute it first appears at.
  const char* a = s_text;
  const char* b = s_file;
  int line = 1;
  bool same = true;
  while (*a || *b) {
    const char* ea = strchr(a, '\n');
    const char* eb = strchr(b, '\n');
    const size_t la = ea ? (size_t)(ea - a) : strlen(a);
    const size_t lb = eb ? (size_t)(eb - b) : strlen(b);
    if (la != lb || memcmp(a, b, la) != 0) {
      fprintf(stderr, "  golden mismatch at line %d\n    now:    %.*s\n    golden: %.*s\n",
              line, (int)la, a, (int)lb, b);
      same = false;
      break;
    }
    a = ea ? ea + 1 : a + la;
    b = eb ? eb + 1 : b + lb;
    line++;
  }
  CHECK(same);
}

// The transcript is a pure function of the seeds: two runs in one process
// must agree, which is what makes the file above a meaningful pin.
TEST(care_golden_is_deterministic) {
  static char again[GOLDEN_TEXT_MAX];
  golden_run(s_text, sizeof s_text);
  golden_run(again, sizeof again);
  CHECK(strcmp(s_text, again) == 0);
  // Something happened in six hours: the pet is no longer a fresh hatchling.
  CHECK(strstr(s_text, "t=360") != nullptr);
}
