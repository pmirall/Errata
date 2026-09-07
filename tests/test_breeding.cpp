// =============================================================================
//  Pebblebol host tests - test_breeding.cpp
//  game/breeding.h (plan P7-C5, spec section 17) over the REAL game/genome.cpp,
//  the REAL game/box.cpp and the REAL game/validate.cpp. Nothing is faked: a
//  parent is built by the tree's one constructor and a child is filed by it.
//
//  WHAT EACH GROUP CAN FAIL, because a case that cannot is worse than none:
//    * the compat matrix sweeps all 36 x 36 roster pairs and pins the EXACT
//      BreedReject, so deleting the stage rule (or the group rule, or reordering
//      them) turns it red by name rather than leaving it green with one rule
//      fewer;
//    * the ENVELOPE case is the mutation target for the clamp, and it carries
//      the measured drift of the UNCLAMPED control beside it - so the number
//      that justifies the clamp is in the test output, not in a comment;
//    * the BATTLE ceiling is asserted and the case SAYS IT CANNOT FAIL
//      (game/validate.h rule (b): gvar folds 0..15 to 0..2 by construction).
//      It is here so the claim has an owner, and the case that can fail is the
//      envelope one.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "core/nt_types.h"
#include "core/rng.h"
#include "data/balance.h"
#include "data/species_table.h"
#include "game/box.h"
#include "game/breeding.h"
#include "game/genome.h"
#include "game/pebble.h"
#include "game/species.h"
#include "game/taint.h"
#include "game/validate.h"
#include "persistence/save_schema.h"

#define BRD_EPOCH0  1700000000u

static GameState g_state;

// A bound, empty Box on a device with a real identity.
static void box_fixture(void)
{
  memset(&g_state, 0, sizeof g_state);
  for (uint8_t i = 0; i < (uint8_t)BOX_SLOTS; ++i) {
    g_state.pebbles[i].magic      = (uint16_t)PEBBLE_MAGIC;
    g_state.pebbles[i].layout_ver = (uint8_t)PEBBLE_LAYOUT_VER;
  }
  g_state.box.magic           = (uint16_t)BOX_MAGIC;
  g_state.box.active_slot     = (uint8_t)BOX_ACTIVE_NONE;
  g_state.box.next_id_counter = 1;
  g_state.cfg.device_id       = 0xB0FFE501u;
  box_bind(g_state);
}

// A parent through box_new_pebble(), so it passes validate_pebble() for the
// same reason every other Pebble in the tree does. `g` must already be sealed.
static PebbleInstance* make_parent(uint8_t species_id, uint8_t level, const Genome& g)
{
  const uint8_t slot = box_new_pebble(species_id, level, (uint8_t)ORIGIN_WILD,
                                      g, 0x5EEDu + species_id, BRD_EPOCH0);
  if (slot == (uint8_t)BOX_SLOT_NONE) return nullptr;
  return box_slot(slot);
}

static Genome seeded_genesis(uint32_t seed)
{
  genome_seed(seed);
  return genome_genesis();
}

// -----------------------------------------------------------------------------
//  0. THE UNBOUND BOX. THIS CASE MUST RUN FIRST AND SAYS SO.
//
//  game/box.cpp has no unbind, deliberately - it is bound once at boot and a
//  test-only unbind would be shipping API written for a test. The one moment a
//  process is genuinely unbound is before the first box_bind(), so this case
//  runs there: nt_test.h runs cases in registration order, which is definition
//  order inside this translation unit, so this must stay at the top. The
//  CHECK(!box_bound()) below is what makes the dependency fail loudly instead
//  of silently passing against a bound Box.
// -----------------------------------------------------------------------------
TEST(an_unbound_box_is_a_named_refusal_and_not_a_crash) {
  CHECK(!box_bound());              // if this fires, the case has been moved
  BreedPlan plan;
  memset(&plan, 0, sizeof plan);
  plan.species_id = 1u;
  plan.level      = 1u;
  plan.genome     = seeded_genesis(0xF1u);
  uint8_t slot = 3;
  CHECK_EQ((int)breed_commit(plan, BRD_EPOCH0, slot), (int)BRD_NO_BOX);
  CHECK_EQ((int)slot, (int)BOX_SLOT_NONE);
}

TEST(a_plan_naming_a_species_the_roster_does_not_carry_is_refused_by_name) {
  box_fixture();
  BreedPlan plan;
  memset(&plan, 0, sizeof plan);
  plan.species_id = 250u;                    // past SPECIES_TABLE_COUNT
  plan.level      = 1u;
  plan.genome     = seeded_genesis(0xF2u);
  uint8_t slot = 3;
  CHECK_EQ((int)breed_commit(plan, BRD_EPOCH0, slot), (int)BRD_UNKNOWN_SPECIES);
  CHECK_EQ((int)slot, (int)BOX_SLOT_NONE);
  CHECK_EQ((int)box_count(), 0);
}

// -----------------------------------------------------------------------------
//  1. THE COMPAT MATRIX - ALL 36 x 36 ROSTER PAIRS, EXACT CODES
//
//  A handful of hand-picked pairs is the shape of test this project keeps
//  finding: it passes with the stage rule deleted, because most hand-picked
//  pairs are stage 1 or 2 anyway. The sweep cannot.
// -----------------------------------------------------------------------------
TEST(the_compat_matrix_is_the_whole_roster_and_every_answer_is_named) {
  int ok = 0, stage = 0, group = 0, pairs = 0;
  for (uint8_t ia = 0; ia < (uint8_t)SPECIES_TABLE_COUNT; ++ia) {
    for (uint8_t ib = 0; ib < (uint8_t)SPECIES_TABLE_COUNT; ++ib) {
      const SpeciesDef& sa = SPECIES_TABLE[ia];
      const SpeciesDef& sb = SPECIES_TABLE[ib];

      box_fixture();
      PebbleInstance* a = make_parent(sa.id, 10, seeded_genesis(0x1111u + ia));
      PebbleInstance* b = make_parent(sb.id, 10, seeded_genesis(0x2222u + ib));
      if (a == nullptr || b == nullptr) { CHECK(false); continue; }

      BreedReject want;
      if (sa.stage < 1u || sb.stage < 1u)                       want = BRD_STAGE;
      else if (sa.compat_group == 0u || sb.compat_group == 0u ||
               sa.compat_group != sb.compat_group)              want = BRD_COMPAT_GROUP;
      else                                                      want = BRD_OK;

      const BreedReject got = breed_check(*a, *b);
      if (got != want) {
        fprintf(stderr, "  species %u(st%u,g%u) x %u(st%u,g%u): %s, wanted %s\n",
                (unsigned)sa.id, (unsigned)sa.stage, (unsigned)sa.compat_group,
                (unsigned)sb.id, (unsigned)sb.stage, (unsigned)sb.compat_group,
                breed_reject_name(got), breed_reject_name(want));
      }
      CHECK_EQ((int)got, (int)want);

      // SYMMETRY. The rule reads the two parents the same way round, so the
      // answer must not depend on which one the caller wrote first.
      CHECK_EQ((int)breed_check(*b, *a), (int)want);

      pairs++;
      if (want == BRD_OK) ok++;
      else if (want == BRD_STAGE) stage++;
      else group++;
    }
  }
  printf("     compat matrix: %d pairs, %d OK, %d BRD_STAGE, %d BRD_COMPAT_GROUP\n",
         pairs, ok, stage, group);
  CHECK_EQ(pairs, (int)SPECIES_TABLE_COUNT * (int)SPECIES_TABLE_COUNT);
  CHECK(ok > 0);      // a matrix that refuses everything would also be "exact"
  CHECK(stage > 0);
  CHECK(group > 0);
}

TEST(one_pebble_cannot_breed_with_itself_and_the_code_says_which_rule) {
  box_fixture();
  PebbleInstance* a = make_parent(2, 10, seeded_genesis(0x31u));
  CHECK(a != nullptr);
  CHECK_EQ((int)breed_check(*a, *a), (int)BRD_SAME_UNIT);

  // Two DIFFERENT Pebbles of the SAME species are a perfectly good pair: the
  // rule is about units, not about species.
  PebbleInstance* b = make_parent(2, 10, seeded_genesis(0x32u));
  CHECK(b != nullptr);
  CHECK(a->id != b->id);
  CHECK_EQ((int)breed_check(*a, *b), (int)BRD_OK);
}

TEST(a_parent_the_one_validator_refuses_is_refused_here_by_its_own_name) {
  box_fixture();
  PebbleInstance* a = make_parent(2, 10, seeded_genesis(0x41u));
  PebbleInstance* b = make_parent(2, 10, seeded_genesis(0x42u));
  CHECK(a != nullptr && b != nullptr);
  CHECK_EQ((int)breed_check(*a, *b), (int)BRD_OK);

  // A move the species cannot learn: VR_UNLEARNABLE_MOVESET one layer down.
  const uint8_t keep = b->moves[0];
  b->moves[0] = 11u;
  CHECK(validate_pebble(*b) != VR_OK);
  CHECK_EQ((int)breed_check(*a, *b), (int)BRD_INVALID_PARENT);
  b->moves[0] = keep;
  CHECK_EQ((int)breed_check(*a, *b), (int)BRD_OK);
}

// -----------------------------------------------------------------------------
//  2. THE GOD-TAINT GATE (game/taint.h), THROUGH BREEDING
// -----------------------------------------------------------------------------
TEST(a_clean_dynasty_refuses_a_tainted_parent_and_a_tainted_one_accepts_anything) {
  box_fixture();
  PebbleInstance* clean = make_parent(2, 10, seeded_genesis(0x51u));
  PebbleInstance* dirty = make_parent(5, 10, seeded_genesis(0x52u));
  CHECK(clean != nullptr && dirty != nullptr);
  // Same compat group is needed or the taint would never be reached: 2 and 5
  // are groups 1 and 2, so use two of group 2 instead.
  box_fixture();
  clean = make_parent(5, 10, seeded_genesis(0x53u));
  dirty = make_parent(14, 10, seeded_genesis(0x54u));
  CHECK(clean != nullptr && dirty != nullptr);
  CHECK_EQ((int)breed_check(*clean, *dirty), (int)BRD_OK);

  // MARKER 1: the genome bit, which is what god_enter() sets and what
  // genome_breed() propagates as A | B.
  gene_set_tainted(dirty->genome, 1u);
  CHECK(pb_is_tainted(*dirty));
  CHECK(!pb_is_tainted(*clean));
  CHECK_EQ((int)breed_check(*clean, *dirty), (int)BRD_TAINT);
  CHECK_EQ((int)breed_check(*dirty, *clean), (int)BRD_TAINT);   // symmetric

  // A TAINTED UNIT ACCEPTS ANYTHING: two testers keep a playground.
  gene_set_tainted(clean->genome, 1u);
  CHECK_EQ((int)breed_check(*clean, *dirty), (int)BRD_OK);

  // MARKER 2: the instance FLAG, which persistence/migration.cpp sets from a v1
  // save WITHOUT necessarily touching the genome bit. Reading only the genome
  // is the hole this half names.
  box_fixture();
  clean = make_parent(5, 10, seeded_genesis(0x55u));
  dirty = make_parent(14, 10, seeded_genesis(0x56u));
  CHECK(clean != nullptr && dirty != nullptr);
  CHECK_EQ((int)breed_check(*clean, *dirty), (int)BRD_OK);
  dirty->flags = (uint8_t)(dirty->flags | (uint8_t)PBF_GOD_TAINTED);
  CHECK_EQ((int)gene_tainted(dirty->genome), 0);      // the OTHER marker is clear
  CHECK(pb_is_tainted(*dirty));
  CHECK_EQ((int)breed_check(*clean, *dirty), (int)BRD_TAINT);
}

// -----------------------------------------------------------------------------
//  3. THE CHILD ITSELF
// -----------------------------------------------------------------------------
TEST(the_offspring_is_the_base_stage_of_a_parents_family_and_passes_the_validator) {
  int made = 0, from_a = 0, from_b = 0;
  for (uint32_t seed = 1; seed <= 400u; ++seed) {
    box_fixture();
    // Two DIFFERENT families inside one compat group (1 and 7 are both group 1),
    // so "the family of the parent chosen by the shared seed" is observable.
    PebbleInstance* a = make_parent(2,  12, seeded_genesis(0x6000u + seed));
    PebbleInstance* b = make_parent(20, 14, seeded_genesis(0x7000u + seed));
    CHECK(a != nullptr && b != nullptr);

    BreedPlan plan;
    const BreedReject r = breed_compute(*a, *b, seed, plan);
    CHECK_EQ((int)r, (int)BRD_OK);
    if (r != BRD_OK) break;

    const SpeciesDef* child = species_get(plan.species_id);
    CHECK(child != nullptr);
    if (child == nullptr) break;
    CHECK_EQ((int)child->stage, 0);
    CHECK_EQ((int)child->family, plan.from_b ? 7 : 1);
    CHECK_EQ((int)plan.level, 1);
    CHECK(genome_valid(plan.genome));           // the CRC and the seal
    CHECK((plan.flags & (uint8_t)EF_FROM_MATING) != 0u);

    uint8_t slot = (uint8_t)BOX_SLOT_NONE;
    CHECK_EQ((int)breed_commit(plan, BRD_EPOCH0, slot), (int)BRD_OK);
    CHECK(slot != (uint8_t)BOX_SLOT_NONE);
    const PebbleInstance* c = box_peek(slot);
    CHECK(c != nullptr);
    if (c == nullptr) break;
    CHECK_EQ((int)validate_pebble(*c), (int)VR_OK);
    CHECK_EQ((int)c->origin, (int)ORIGIN_BRED);
    CHECK((c->flags & (uint8_t)PBF_BRED) != 0u);
    CHECK(c->id != 0u && c->id != a->id && c->id != b->id);
    CHECK_EQ((int)c->level, 1);
    // The learnset is the base row's, VERBATIM (breeding.h section 2).
    CHECK_EQ(memcmp(c->moves, child->moves, sizeof c->moves), 0);
    made++;
    if (plan.from_b) from_b++; else from_a++;
  }
  printf("     %d offspring: %d took A's family, %d took B's\n", made, from_a, from_b);
  CHECK_EQ(made, 400);
  CHECK(from_a > 0);      // a seed that never picks either side is not a pick
  CHECK(from_b > 0);
}

TEST(the_generation_counter_climbs_and_saturates_and_the_taint_is_never_cleared) {
  box_fixture();
  Genome ga = seeded_genesis(0x81u);
  Genome gb = seeded_genesis(0x82u);
  ga.generation = 250; genome_seal(ga);
  gb.generation = 253; genome_seal(gb);
  gene_set_tainted(ga, 1u);

  PebbleInstance* a = make_parent(5, 10, ga);
  PebbleInstance* b = make_parent(14, 10, gb);
  CHECK(a != nullptr && b != nullptr);
  // Both parents tainted, so the gate lets the pair through and the CHILD's
  // taint is genome_breed()'s A | B.
  gene_set_tainted(b->genome, 1u);

  BreedPlan plan;
  CHECK_EQ((int)breed_compute(*a, *b, 0xBEEFu, plan), (int)BRD_OK);
  CHECK_EQ((int)plan.genome.generation, 254);
  CHECK_EQ((int)gene_tainted(plan.genome), 1);

  a->genome.generation = 255; genome_seal(a->genome);
  b->genome.generation = 255; genome_seal(b->genome);
  gene_set_tainted(a->genome, 1u);
  gene_set_tainted(b->genome, 1u);
  CHECK_EQ((int)breed_compute(*a, *b, 0xBEEFu, plan), (int)BRD_OK);
  CHECK_EQ((int)plan.genome.generation, 255);       // saturating, never wrapping
}

// -----------------------------------------------------------------------------
//  4. THE BUDGET CEILING
//
//  TWO HALVES, AND ONLY ONE OF THEM CAN FAIL. The first is here so the claim
//  has an owner; the second is the mutation target.
// -----------------------------------------------------------------------------
TEST(the_battle_variation_ceiling_is_structural_and_this_case_says_so) {
  // game/validate.h rule (b): the gene masks make a range check a test that
  // cannot fail, and game/pebble.h folds 0..15 to 0..2. THIS CASE CANNOT FAIL
  // and it is written down as such - the ceiling that CAN be broken is the
  // genesis envelope, and the case below is the one that guards it.
  uint8_t worst[3] = { 0, 0, 0 };
  for (uint32_t seed = 1; seed <= 2000u; ++seed) {
    box_fixture();
    PebbleInstance* a = make_parent(5,  30, seeded_genesis(0x9000u + seed));
    PebbleInstance* b = make_parent(14, 30, seeded_genesis(0xA000u + seed));
    CHECK(a != nullptr && b != nullptr);
    BreedPlan plan;
    if (breed_compute(*a, *b, seed, plan) != BRD_OK) { CHECK(false); break; }
    uint8_t v[3];
    pebble_genome_vars(plan.genome, v);
    for (int i = 0; i < 3; ++i) {
      CHECK(v[i] <= (uint8_t)PEBBLE_GENOME_VAR_MAX);
      if (v[i] > worst[i]) worst[i] = v[i];
    }
  }
  printf("     worst bred genome variation over 2,000 pairs: atk +%u def +%u spd +%u "
         "(ceiling %d, and it is held by the fold, not by breeding.cpp)\n",
         (unsigned)worst[0], (unsigned)worst[1], (unsigned)worst[2],
         (int)PEBBLE_GENOME_VAR_MAX);
}

// The one that CAN fail: delete clamp_to_genesis_envelope()'s call and this
// goes red by name. The unclamped control arm is measured in the same case, so
// the number that justifies the clamp is in the output rather than in a comment.
TEST(ten_thousand_bred_pairs_never_leave_the_genesis_care_envelope) {
  const uint8_t lo = (uint8_t)GENESIS_GENE_MIN, hi = (uint8_t)GENESIS_GENE_MAX;
  uint8_t seen_lo = 15, seen_hi = 0, luck_lo = 7, luck_hi = 0;
  uint16_t app_lo = 0xFFFFu, app_hi = 0, met_lo = 0xFFFFu, met_hi = 0;
  int n = 0;

  Rng r; rng_init(r, 0x51DEu);
  for (int i = 0; i < 10000; ++i) {
    box_fixture();
    // Any legal pair from the whole roster, not one hand-picked species.
    static const uint8_t GRP2[] = { 5, 6, 14, 15, 23, 24 };
    const uint8_t sa = GRP2[rng_next_below(r, (uint32_t)(sizeof GRP2)) ];
    const uint8_t sb = GRP2[rng_next_below(r, (uint32_t)(sizeof GRP2)) ];
    PebbleInstance* a = make_parent(sa, (uint8_t)(1 + rng_next_below(r, 30)),
                                    seeded_genesis(rng_next(r) | 1u));
    PebbleInstance* b = make_parent(sb, (uint8_t)(1 + rng_next_below(r, 30)),
                                    seeded_genesis(rng_next(r) | 1u));
    if (a == nullptr || b == nullptr) { CHECK(false); break; }
    if (a->id == b->id) continue;

    BreedPlan plan;
    if (breed_compute(*a, *b, rng_next(r), plan) != BRD_OK) { CHECK(false); break; }
    const Genome& g = plan.genome;

    const uint8_t gv[5] = { gene_appetite(g), gene_metabolism(g),
                            gene_sociability(g), gene_temperament(g),
                            gene_hardiness(g) };
    for (int k = 0; k < 5; ++k) {
      CHECK(gv[k] >= lo && gv[k] <= hi);
      if (gv[k] < seen_lo) seen_lo = gv[k];
      if (gv[k] > seen_hi) seen_hi = gv[k];
    }
    const uint8_t lk = gene_luck(g);
    CHECK(lk >= (uint8_t)GENESIS_LUCK_MIN && lk <= (uint8_t)GENESIS_LUCK_MAX);
    if (lk < luck_lo) luck_lo = lk;
    if (lk > luck_hi) luck_hi = lk;

    // The multipliers are what the envelope is FOR: they drive care decay and
    // damage taken across the whole 600..1500 per-mille range.
    const uint16_t am = gene_appetite_mult(g), mm = gene_metabolism_mult(g);
    if (am < app_lo) app_lo = am;
    if (am > app_hi) app_hi = am;
    if (mm < met_lo) met_lo = mm;
    if (mm > met_hi) met_hi = mm;
    n++;
  }
  printf("     %d bred children: genes %u..%u (band %u..%u), luck %u..%u, "
         "appetite mult %u..%u, metabolism mult %u..%u\n",
         n, (unsigned)seen_lo, (unsigned)seen_hi, (unsigned)lo, (unsigned)hi,
         (unsigned)luck_lo, (unsigned)luck_hi,
         (unsigned)app_lo, (unsigned)app_hi, (unsigned)met_lo, (unsigned)met_hi);
  CHECK(n > 9000);
}

// THE MEASUREMENT BEHIND THE CLAMP. A dynasty is bred forty deep twice: once
// through breed_compute() (clamped) and once through genome_breed() directly
// (what the game would do without this module). If the unclamped arm never
// left the band the clamp would be a guard nobody can fail, so the case
// ASSERTS that the control arm escapes.
TEST(an_unclamped_dynasty_drifts_out_of_the_band_and_a_clamped_one_does_not) {
  const int GENERATIONS = 40, DYNASTIES = 200;
  int escaped_free = 0, escaped_clamped = 0;
  uint8_t free_lo = 15, free_hi = 0;

  for (int d = 0; d < DYNASTIES; ++d) {
    // --- the control: genome_breed() and nothing else --------------------
    genome_seed(0xD00Du + (uint32_t)d);
    Genome ga = genome_genesis();
    Genome gb = genome_genesis();
    for (int gen = 0; gen < GENERATIONS; ++gen) {
      const Genome child = genome_breed(ga, gb, 0, 0, nullptr);
      gb = ga; ga = child;
      const uint8_t v[5] = { gene_appetite(ga), gene_metabolism(ga),
                             gene_sociability(ga), gene_temperament(ga),
                             gene_hardiness(ga) };
      for (int k = 0; k < 5; ++k) {
        if (v[k] < free_lo) free_lo = v[k];
        if (v[k] > free_hi) free_hi = v[k];
        if (v[k] < (uint8_t)GENESIS_GENE_MIN || v[k] > (uint8_t)GENESIS_GENE_MAX)
          escaped_free++;
      }
    }

    // --- this module: the same walk through breed_compute() ---------------
    genome_seed(0xD00Du + (uint32_t)d);
    Genome ca = genome_genesis();
    Genome cb = genome_genesis();
    for (int gen = 0; gen < GENERATIONS; ++gen) {
      box_fixture();
      PebbleInstance* a = make_parent(5,  20, ca);
      PebbleInstance* b = make_parent(14, 20, cb);
      if (a == nullptr || b == nullptr) { CHECK(false); break; }
      BreedPlan plan;
      if (breed_compute(*a, *b, 0xC0DEu + (uint32_t)(d * 64 + gen), plan) != BRD_OK) {
        CHECK(false); break;
      }
      cb = ca; ca = plan.genome;
      const uint8_t v[5] = { gene_appetite(ca), gene_metabolism(ca),
                             gene_sociability(ca), gene_temperament(ca),
                             gene_hardiness(ca) };
      for (int k = 0; k < 5; ++k) {
        if (v[k] < (uint8_t)GENESIS_GENE_MIN || v[k] > (uint8_t)GENESIS_GENE_MAX)
          escaped_clamped++;
      }
      CHECK(gene_luck(ca) >= (uint8_t)GENESIS_LUCK_MIN);
      CHECK(gene_luck(ca) <= (uint8_t)GENESIS_LUCK_MAX);
    }
  }
  printf("     %d dynasties x %d generations: unclamped left the band %d times "
         "(range %u..%u), clamped %d\n",
         DYNASTIES, GENERATIONS, escaped_free,
         (unsigned)free_lo, (unsigned)free_hi, escaped_clamped);
  CHECK(escaped_free > 0);        // else the clamp would guard nothing
  CHECK_EQ(escaped_clamped, 0);
}

// -----------------------------------------------------------------------------
//  5. DETERMINISM ACROSS TWO DEVICES
//
//  ONE PROCESS, TWO ENDPOINTS, SEQUENTIALLY. breeding.h says why they cannot be
//  interleaved: genome_set_rng() takes a source with no context, so the
//  scripted state is file-scope in breeding.cpp.
// -----------------------------------------------------------------------------
TEST(two_devices_with_the_same_seed_compute_a_byte_identical_child) {
  int differed_by_seed = 0;
  for (uint32_t seed = 1; seed <= 256u; ++seed) {
    box_fixture();
    PebbleInstance* a = make_parent(5,  17, seeded_genesis(0xB100u));
    PebbleInstance* b = make_parent(14, 23, seeded_genesis(0xB200u));
    CHECK(a != nullptr && b != nullptr);

    // Device 1.
    BreedPlan p1;
    CHECK_EQ((int)breed_compute(*a, *b, seed, p1), (int)BRD_OK);

    // Something else draws from RNG_BREEDING in between - a wild capture, a god
    // mode roll - which is exactly what capture.h warns a reseed would break.
    (void)genome_genesis();
    (void)genome_rand();

    // Device 2, same parents, same seed.
    BreedPlan p2;
    CHECK_EQ((int)breed_compute(*a, *b, seed, p2), (int)BRD_OK);

    CHECK_EQ(memcmp(&p1, &p2, sizeof p1), 0);

    BreedPlan p3;
    CHECK_EQ((int)breed_compute(*a, *b, seed ^ 0x5A5A5A5Au, p3), (int)BRD_OK);
    if (memcmp(&p1, &p3, sizeof p1) != 0) differed_by_seed++;
  }
  printf("     256 seeds: %d produced a different child from the same parents\n",
         differed_by_seed);
  // A "deterministic" child that ignores the seed would pass the equality above
  // and fail here.
  CHECK(differed_by_seed > 200);
}

TEST(breeding_never_reseeds_or_consumes_the_shared_breeding_stream) {
  // game/capture.h names the trap: a caller that reached for genome_seed()
  // would change every later breeding outcome in the boot. This pins that
  // breed_compute() does not - the control draw after it must be the draw the
  // stream would have produced with no breeding at all.
  genome_seed(0x1234u);
  uint32_t want[8];
  for (int i = 0; i < 8; ++i) want[i] = genome_rand();

  genome_seed(0x1234u);
  box_fixture();
  PebbleInstance* a = make_parent(5,  10, seeded_genesis(0xC1u));
  PebbleInstance* b = make_parent(14, 10, seeded_genesis(0xC2u));
  CHECK(a != nullptr && b != nullptr);
  genome_seed(0x1234u);                    // make_parent's rolls are not the point
  BreedPlan plan;
  CHECK_EQ((int)breed_compute(*a, *b, 0x777u, plan), (int)BRD_OK);
  for (int i = 0; i < 8; ++i) CHECK_EQ((long long)genome_rand(), (long long)want[i]);
}

TEST(the_two_ends_must_agree_on_which_parent_is_a) {
  // The argument order is part of the cross-device contract (breeding.h). Two
  // devices that order the parents differently compute two different, equally
  // legal children - which is a desync, not a refusal, so the property is
  // stated as a measurement rather than hoped for.
  int same = 0, diff = 0;
  for (uint32_t seed = 1; seed <= 200u; ++seed) {
    box_fixture();
    PebbleInstance* a = make_parent(5,  10, seeded_genesis(0xD100u + seed));
    PebbleInstance* b = make_parent(14, 10, seeded_genesis(0xD200u + seed));
    CHECK(a != nullptr && b != nullptr);
    BreedPlan pab, pba;
    CHECK_EQ((int)breed_compute(*a, *b, seed, pab), (int)BRD_OK);
    CHECK_EQ((int)breed_compute(*b, *a, seed, pba), (int)BRD_OK);
    if (memcmp(&pab, &pba, sizeof pab) == 0) same++; else diff++;
  }
  printf("     200 seeds: A,B and B,A agreed %d times and differed %d\n", same, diff);
  CHECK(diff > 100);
}

// -----------------------------------------------------------------------------
//  6. THE BOX, AND NO AUTO-ACCEPTED EGG (audit risk 5)
// -----------------------------------------------------------------------------
TEST(a_full_box_refuses_the_child_by_name_and_writes_nothing) {
  box_fixture();
  PebbleInstance* a = make_parent(5,  10, seeded_genesis(0xE1u));
  PebbleInstance* b = make_parent(14, 10, seeded_genesis(0xE2u));
  CHECK(a != nullptr && b != nullptr);
  BreedPlan plan;
  CHECK_EQ((int)breed_compute(*a, *b, 0x99u, plan), (int)BRD_OK);

  while (box_count() < box_capacity()) {
    CHECK(make_parent(5, 3, seeded_genesis(0xE300u + box_count())) != nullptr);
  }
  CHECK_EQ((int)box_count(), (int)BOX_SLOTS);

  uint8_t slot = 7;
  CHECK_EQ((int)breed_commit(plan, BRD_EPOCH0, slot), (int)BRD_BOX_FULL);
  CHECK_EQ((int)slot, (int)BOX_SLOT_NONE);
  CHECK_EQ((int)box_count(), (int)BOX_SLOTS);      // nothing was filed

  // COMPUTING A CHILD IS NOT ACCEPTING ONE. breed_compute() alone must never
  // have touched the Box - the plan is shown to the player first, on both
  // devices, and only an A press reaches breed_commit().
  BreedPlan again;
  CHECK_EQ((int)breed_compute(*a, *b, 0x99u, again), (int)BRD_OK);
  CHECK_EQ((int)box_count(), (int)BOX_SLOTS);
  CHECK_EQ(memcmp(&plan, &again, sizeof plan), 0);
}

TEST(every_breed_reject_has_a_name_and_the_table_cannot_drift) {
  for (int r = 0; r < (int)BRD_REJECT_COUNT; ++r) {
    const char* n = breed_reject_name((BreedReject)r);
    CHECK(n != nullptr);
    CHECK(n[0] == 'B' && n[1] == 'R' && n[2] == 'D' && n[3] == '_');
    CHECK(strcmp(n, "BRD_?") != 0);
  }
  CHECK_EQ(strcmp(breed_reject_name((BreedReject)BRD_REJECT_COUNT), "BRD_?"), 0);
  CHECK_EQ(strcmp(breed_reject_name((BreedReject)200), "BRD_?"), 0);
}
