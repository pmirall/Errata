// =============================================================================
//  PEBBLEBOL host test - test_content.cpp
//  EVERY PLAN 1.5.2 COMPILE-TIME GUARD, RE-ASSERTED AT RUNTIME (P4-C1).
//
//  The generated headers static_assert all of this, so a bad table is a BUILD
//  failure. This file exists because a static_assert only fires in a build that
//  INCLUDES it: a future table that somehow reaches a compiler which does not
//  evaluate the constexpr, or a header that stops being included anywhere,
//  would take its guard with it silently. Here the same rule is a test that
//  fails the gate.
//
//  It also carries the four things that are not guards:
//    * SPECIES 1 IS PINNED FIELD BY FIELD. P4-C1 obligation 1 says the starter
//      must come out of the generator byte-identical to what P3-C3 froze, and
//      "we diffed it once" is not a gate. The row is written out here so any
//      future regeneration that moves it fails.
//    * evolution_apply() IS DRIVEN THROUGH A FAILING CONDITION (obligation 4).
//      Phase 3 could not: every rule it shipped was EVOC_NONE, so the condition
//      arm was unreachable. This roster ships EVOC_CORRUPTED and
//      EVOC_HAPPINESS_GE, so the arm is reachable for the first time.
//    * THE ORPHAN ATTACKS (obligation 2), pinned where they landed.
//    * THE COMPLETENESS PASS (obligation 3): every enum value the content
//      declares has at least one row, or is named here as deliberately unused.
// =============================================================================
#include "nt_test.h"

#include <string.h>

#include "data/attacks_table.h"
#include "data/creator_schema.h"
#include "data/encounter_table.h"
#include "data/evolution_table.h"
#include "data/items_table.h"
#include "data/species_table.h"
#include "data/sprites.h"
#include "data/sprites_pebbles.h"
#include "core/version.h"
#include "game/evolution.h"
#include "game/species.h"
#include "game/xp.h"

static void make_pebble(PebbleInstance& p, uint8_t species, uint8_t level) {
  memset(&p, 0, sizeof p);
  p.magic      = (uint16_t)PEBBLE_MAGIC;
  p.layout_ver = (uint8_t)PEBBLE_LAYOUT_VER;
  p.species_id = species;
  p.id         = 0x5EED0002u;
  p.level      = level;
  const SpeciesDef* sp = species_get(species);
  p.hp_cur = sp ? xp_hp_max(sp->base_hp, level) : 0u;
}

// -----------------------------------------------------------------------------
//  UTF-8, counted in GLYPHS rather than bytes.
//
//  core/strings_es.h is UTF-8 and the roster is Spanish, so "Rafagon" with an
//  acute is 7 glyphs in 8 bytes and "Nunez" with a tilde is 5 in 6. Every width
//  rule in this product is a rule about GLYPHS - the fonts are fixed-advance, so
//  glyphs are pixels - and strlen() would reject names that draw perfectly well
//  while accepting a 9-glyph name that is 12 bytes. Nothing in the tree counted
//  them until P9-C1.
//
//  It also reports the widest codepoint, because the _tf fonts u8g2 ships carry
//  ASCII + Latin-1 and nothing above: a string with U+2014 draws as a wrong
//  glyph or as nothing, and strings_es.h:9-14 calls that a review blocker.
// -----------------------------------------------------------------------------
static int utf8_glyphs(const char* str, uint32_t* widest_cp) {
  const unsigned char* p = (const unsigned char*)str;
  int n = 0;
  uint32_t widest = 0;
  while (*p) {
    uint32_t cp = 0;
    int len = 1;
    if (*p < 0x80u)            { cp = *p;               len = 1; }
    else if ((*p & 0xE0u) == 0xC0u) { cp = (uint32_t)(*p & 0x1Fu); len = 2; }
    else if ((*p & 0xF0u) == 0xE0u) { cp = (uint32_t)(*p & 0x0Fu); len = 3; }
    else                        { cp = (uint32_t)(*p & 0x07u); len = 4; }
    for (int k = 1; k < len; ++k) cp = (cp << 6) | (uint32_t)(p[k] & 0x3Fu);
    p += len;
    ++n;
    if (cp > widest) widest = cp;
  }
  if (widest_cp) *widest_cp = widest;
  return n;
}

// =============================================================================
//  1. THE STARTER, PINNED (obligation 1)
// =============================================================================
TEST(species_one_is_exactly_the_row_phase_three_froze) {
  const SpeciesDef* s = species_get(1);
  CHECK(s != nullptr);
  if (!s) return;
  CHECK_EQ(s->id, 1);
  CHECK_EQ(s->family, 1);
  CHECK_EQ(s->stage, 0);
  CHECK_EQ(s->type, (uint8_t)TYPE_SIGNAL);
  CHECK_EQ(s->base_hp, 4);
  CHECK_EQ(s->base_atk, 4);
  CHECK_EQ(s->base_def, 4);
  CHECK_EQ(s->base_spd, 4);
  // The learnset a fresh device is built from. tests/golden/care_v2.txt records
  // a species-1 starter, so this is not cosmetic.
  CHECK_EQ(s->moves[0], 1);
  CHECK_EQ(s->moves[1], 6);
  CHECK_EQ(s->moves[2], 7);
  CHECK_EQ(s->moves[3], 27);
  CHECK_EQ(s->evo_rule, 0);
  CHECK_EQ(s->rarity, SPECIES_RARITY_COMMON);
  CHECK_EQ(s->spawn_weight, 190);
  CHECK_EQ(s->compat_group, 1);
  CHECK_EQ(s->category_mask, 23);
  CHECK_EQ(s->sprite_id, 0);
  CHECK_EQ(s->name_idx, (uint16_t)STR_SPC_NAME_1);
  CHECK_EQ(s->flavor_idx, (uint16_t)STR_SPC_FLAV_1);
  CHECK_EQ(s->reserved[0], 0);
  CHECK_EQ(s->reserved[1], 0);
  CHECK_STR_EQ(S(s->name_idx), "Paketo");

  // Its rule is untouched too: level 8, no condition.
  const EvolutionRule* r = evolution_rule_at(s->evo_rule);
  CHECK(r != nullptr);
  if (!r) return;
  CHECK_EQ(r->species, 1);
  CHECK_EQ(r->target, 2);
  CHECK_EQ(r->level, 8);
  CHECK_EQ(r->cond, (uint8_t)EVOC_NONE);
}

// Ids 2 and 3 keep every field they had EXCEPT the learnset, which the content
// pack's final tuning moved and which nothing in the tree depends on (the
// evolution tests use synthetic move ids, the persistence tests use their own,
// and the care golden records stats). Pinned here so the next change is
// deliberate rather than incidental.
TEST(species_two_and_three_keep_their_frozen_columns) {
  const SpeciesDef* s2 = species_get(2);
  const SpeciesDef* s3 = species_get(3);
  CHECK(s2 != nullptr && s3 != nullptr);
  if (!s2 || !s3) return;

  CHECK_EQ(s2->family, 1);   CHECK_EQ(s2->stage, 1);
  CHECK_EQ(s2->base_hp, 6);  CHECK_EQ(s2->base_atk, 6);
  CHECK_EQ(s2->base_def, 5); CHECK_EQ(s2->base_spd, 5);
  CHECK_EQ(s2->rarity, SPECIES_RARITY_COMMON);
  CHECK_EQ(s2->spawn_weight, 120);
  CHECK_EQ(s2->sprite_id, 1);
  CHECK_STR_EQ(S(s2->name_idx), "Fragmar");

  CHECK_EQ(s3->family, 1);   CHECK_EQ(s3->stage, 2);
  CHECK_EQ(s3->base_hp, 7);  CHECK_EQ(s3->base_atk, 7);
  CHECK_EQ(s3->base_def, 7); CHECK_EQ(s3->base_spd, 7);
  CHECK_EQ(s3->rarity, SPECIES_RARITY_RARE);
  CHECK_EQ(s3->spawn_weight, 55);
  CHECK_EQ(s3->sprite_id, 2);
  CHECK_EQ(s3->evo_rule, (uint8_t)SPECIES_EVO_NONE);
  CHECK_STR_EQ(S(s3->name_idx), "Rafag\xc3\xb3n");

  // The pack's tuned learnsets, which P4-C1 adopts.
  const uint8_t want2[PB_MOVE_COUNT] = { 5, 27, 31, 33 };
  const uint8_t want3[PB_MOVE_COUNT] = { 3, 27, 33, 31 };
  for (uint8_t i = 0; i < (uint8_t)PB_MOVE_COUNT; ++i) {
    CHECK_EQ(s2->moves[i], want2[i]);
    CHECK_EQ(s3->moves[i], want3[i]);
  }
}

// =============================================================================
//  2. THE SPECIES GUARDS (plan 1.5.2)
// =============================================================================
TEST(species_rows_are_well_formed_at_runtime) {
  CHECK(SPECIES_TABLE_COUNT >= 1);
  CHECK(SPECIES_TABLE_COUNT <= SPECIES_ID_BUILTIN_MAX);
  CHECK_EQ((int)SPECIES_TABLE_COUNT, (int)SPECIES_FAMILY_COUNT * 3);

  for (uint8_t i = 0; i < SPECIES_TABLE_COUNT; ++i) {
    const SpeciesDef& sp = SPECIES_TABLE[i];
    CHECK_EQ(sp.id, (uint8_t)(i + 1u));          // contiguity
    CHECK(sp.family >= 1 && sp.family <= SPECIES_FAMILY_COUNT);
    CHECK(sp.stage <= 2);
    CHECK(sp.type < (uint8_t)TYPE_COUNT);        // a species is never NEUTRAL
    CHECK(sp.base_hp >= 1 && sp.base_atk >= 1);
    CHECK(sp.base_def >= 1 && sp.base_spd >= 1);
    CHECK(sp.rarity <= SPECIES_RARITY_SPECIAL);
    CHECK(sp.spawn_weight > 0);
    CHECK(sp.compat_group > 0);
    CHECK(sp.category_mask > 0);
    CHECK(sp.name_idx < (uint16_t)STR_COUNT);
    CHECK(sp.flavor_idx < (uint16_t)STR_COUNT);
    CHECK_EQ(sp.reserved[0], 0);
    CHECK_EQ(sp.reserved[1], 0);
    // The strings resolve to something a screen can draw.
    CHECK(S(sp.name_idx)[0] != '\0');
    CHECK(S(sp.flavor_idx)[0] != '\0');
  }
}

// plan 1.5.2's `sprite_id < SPRITE_SET_COUNT`, and - since P4-C4a - the check
// that actually means something: EVERY ROW DRAWS A CREATURE.
//
// THE CASE THIS REPLACES COULD NOT FAIL IN THE WAY THAT MATTERED, and it was
// the eighth of this project's recurring defect. It asserted that
// `SPR_BABY_BLOB + sprite_id` was arithmetically inside SPRITE_SETS - and
// NOTHING EVALUATED THAT SUM TO DRAW ANYTHING. It passed happily while species
// 25..36 would have resolved onto SPR_GHOST, SPR_TOMB and the sleep / sick /
// eat pose sets; Murax would have been drawn as an adult eating. A
// byte-identical copy of it also sat in tests/test_evolution.cpp, so the tree
// carried the same un-failable statement twice.
//
// The sum is still checked, because it is still the forward bound that caps the
// roster at 36 until P10's art pass lands (see game/species.cpp guard 1). What
// is NEW is the second loop: the resolution the firmware runs, at every stage,
// asserted to land on one of the 24 authored creature bodies.
TEST(every_species_row_draws_a_creature_body) {
  static const uint8_t kStages[] = { STAGE_BABY, STAGE_CHILD, STAGE_TEEN,
                                     STAGE_ADULT, STAGE_SENIOR };
  for (uint8_t i = 0; i < SPECIES_TABLE_COUNT; ++i) {
    const SpeciesDef& sp = SPECIES_TABLE[i];
    CHECK(sp.sprite_id < (uint8_t)SPRITE_SET_COUNT);
    CHECK((uint16_t)(SPR_BABY_BLOB + sp.sprite_id) < (uint16_t)SPRITE_SET_COUNT);
    CHECK_EQ(sp.sprite_id, (uint8_t)(sp.id - 1u));

    for (uint8_t s = 0; s < (uint8_t)(sizeof kStages / sizeof kStages[0]); ++s) {
      const Stage st = (Stage)kStages[s];
      const uint8_t id = sprite_set_id((uint8_t)st, sprite_form_of(sp.sprite_id, 0u, st),
                                       (uint8_t)POSE_IDLE);
      CHECK(id >= SPRITE_BODY_FIRST);
      CHECK(id <= SPRITE_BODY_LAST);
      CHECK(id != (uint8_t)SPR_GHOST);
      CHECK(id != (uint8_t)SPR_TOMB);
    }
  }
}

// AND THE REASON THE GUARD ABOVE IS NOT THE SUM. This states, as a number, how
// much of the roster the naive `SPR_BABY_BLOB + sprite_id` resolution would
// mis-draw against TODAY's 38-set atlas: species 25..36, i.e. all of families
// 9 to 12, land on GHOST, TOMB and the ten pose sets.
//
// It is written to FAIL when P10's art pass lands and the atlas becomes one
// body per species - at which point the naive sum becomes correct, this number
// goes to 0, and whoever is holding it should come here and delete both this
// case and sprite_design_of()'s folding.
TEST(the_naive_sprite_sum_would_mis_draw_a_third_of_the_roster) {
  uint8_t not_a_body = 0;
  uint8_t first_bad  = 0;
  for (uint8_t i = 0; i < SPECIES_TABLE_COUNT; ++i) {
    const SpeciesDef& sp = SPECIES_TABLE[i];
    const uint16_t naive = (uint16_t)(SPR_BABY_BLOB + sp.sprite_id);
    if (naive < SPRITE_BODY_FIRST || naive > SPRITE_BODY_LAST) {
      if (!not_a_body) first_bad = sp.id;
      ++not_a_body;
    }
  }
  CHECK_EQ((int)not_a_body, 12);
  CHECK_EQ((int)first_bad, 25);
}

// =============================================================================
//  THE PLAN'S ">= 60 SPECIES", AND WHY IT IS ASSERTED ABOUT THE PACK
//
//  P9-C1's acceptance line asks for ">= 60 species". The shipped table holds 36
//  and will until P9-C3, so a naive `CHECK(SPECIES_TABLE_COUNT >= 60)` would
//  have had to be written as a failing test or not written at all - and
//  tools/content/verify.py's assertion that the PACK holds 60 passes today
//  while 36 ship, which is exactly the gap that lets "the roster is done" and
//  "the roster is 36" both be true statements in the same repository.
//
//  So the requirement is split into the two halves that are each checkable now,
//  both of them from the emitted headers (P9-C1 added SPECIES_PACK_COUNT for
//  this):
//
//    * THE CONTENT IS COMPLETE - the pack defines 60 species in 20 families.
//    * THE SHIP IS CLAMPED BY THE ART, NOT BY THE CONTENT - the roster is
//      exactly as large as the atlas can address, and no larger.
//
//  THE SECOND HALF IS A PIN THAT P9-C3 IS MEANT TO BREAK. When the art pass
//  lands 60 bodies, `SPECIES_TABLE_COUNT == SPRITE_SET_COUNT - SPR_BABY_BLOB`
//  fails, and the correct repair is to raise ROSTER_FAMILIES to 20 and re-point
//  this case at PB_SPRITE_BODY_COUNT - not to delete it. It is the same shape as
//  the_naive_sprite_sum_would_mis_draw_a_third_of_the_roster above.
TEST(the_pack_is_complete_and_the_roster_is_clamped_by_the_atlas) {
  CHECK(SPECIES_PACK_COUNT >= 60);
  CHECK_EQ((int)SPECIES_PACK_COUNT, (int)SPECIES_PACK_FAMILY_COUNT * 3);
  CHECK(SPECIES_TABLE_COUNT <= SPECIES_PACK_COUNT);
  CHECK_EQ((int)SPECIES_TABLE_COUNT, (int)SPECIES_FAMILY_COUNT * 3);

  // The roster may never exceed what the live atlas can draw. Today that is
  // data/sprites.h's: one body per species from SPR_BABY_BLOB up.
  CHECK(SPECIES_TABLE_COUNT <= (int)SPRITE_SET_COUNT - (int)SPR_BABY_BLOB);
  CHECK_EQ((int)SPECIES_TABLE_COUNT, (int)SPRITE_SET_COUNT - (int)SPR_BABY_BLOB);
  CHECK_EQ((int)SPECIES_TABLE_COUNT, 36);

  // And the atlas P9-C3 replaces it with, the moment it has any bodies at all:
  // shipping 60 species against 40 drawn bodies is the failure this forbids.
  CHECK(PB_SPRITE_BODY_COUNT == 0 ||
        PB_SPRITE_BODY_COUNT >= SPECIES_TABLE_COUNT);
}

// plan 1.5.2's `sum(spawn_weight) > 0 per category`, and the reason the table
// is uint16_t: the sums do not fit a byte.
TEST(every_network_category_has_something_to_spawn) {
  uint16_t widest = 0;
  for (uint8_t c = 0; c < (uint8_t)NET_CAT_COUNT; ++c) {
    uint32_t total = 0;
    for (uint8_t r = 0; r < SPECIES_RARITY_COUNT; ++r) {
      total += SPECIES_SPAWN_SUM[c][r];
      if (SPECIES_SPAWN_SUM[c][r] > widest) widest = SPECIES_SPAWN_SUM[c][r];
      // The precomputed table agrees with a walk of the roster.
      uint32_t walk = 0;
      for (uint8_t i = 0; i < SPECIES_TABLE_COUNT; ++i) {
        const SpeciesDef& sp = SPECIES_TABLE[i];
        if ((sp.category_mask & NET_CATEGORY_BIT[c]) && sp.rarity == r)
          walk += sp.spawn_weight;
      }
      CHECK_EQ((uint32_t)SPECIES_SPAWN_SUM[c][r], walk);
      CHECK_EQ((uint32_t)species_spawn_weight_in(c, r, r), walk);
    }
    CHECK(total > 0);
    CHECK(SPECIES_SPAWN_SUM[c][SPECIES_RARITY_COMMON] > 0);
  }
  // If this ever stops holding the table could be narrowed; while it holds, a
  // u8 sum table would silently truncate the range an encounter roll draws on.
  CHECK(widest > 255);
}

TEST(every_family_has_exactly_one_base_stage_and_it_is_reachable) {
  for (uint8_t f = 1; f <= SPECIES_FAMILY_COUNT; ++f) {
    const uint8_t base = species_base_of_family(f);
    CHECK(base != 0);
    const SpeciesDef* sp = species_get(base);
    CHECK(sp != nullptr);
    if (!sp) continue;
    CHECK_EQ(sp->family, f);
    CHECK_EQ(sp->stage, 0);
    // A base stage must be obtainable: nothing may start a family that only
    // a SPECIAL-band encounter could ever produce.
    CHECK(sp->rarity <= SPECIES_RARITY_UNCOMMON);
  }
  CHECK_EQ(species_base_of_family(0), 0);
  CHECK_EQ(species_base_of_family((uint8_t)(SPECIES_FAMILY_COUNT + 1)), 0);
}

// =============================================================================
//  3. THE ATTACK GUARDS, AND THE ONE THE PLAN GOT OFF BY ONE
// =============================================================================
TEST(attack_rows_are_well_formed_at_runtime) {
  CHECK(ATTACK_COUNT >= 1);
  for (uint8_t i = 0; i < ATTACK_COUNT; ++i) {
    const AttackDef& a = ATTACKS_TABLE[i];
    CHECK_EQ(a.id, (uint8_t)(i + 1u));
    CHECK(a.type <= (uint8_t)TYPE_NEUTRAL);
    CHECK(a.category < (uint8_t)ATK_CAT_COUNT);
    CHECK(a.effect < (uint8_t)ATK_EFF_COUNT);
    CHECK(a.power <= 100);
    CHECK(a.accuracy >= 1 && a.accuracy <= 100);
    CHECK(a.anim_id >= 1);
    CHECK(a.name_idx < (uint16_t)STR_COUNT);
    CHECK(S(a.name_idx)[0] != '\0');
    CHECK_EQ(a.reserved[0], 0);
    CHECK_EQ(a.reserved[1], 0);
  }
  CHECK(attack_get(0) == nullptr);
  CHECK(attack_get((uint8_t)(ATTACK_COUNT + 1)) == nullptr);
  // THE BOUNDARY THE PLAN'S LITERAL `moves[i] < ATTACK_COUNT` WOULD REJECT.
  CHECK(attack_get(ATTACK_COUNT) != nullptr);
}

TEST(every_learnset_resolves_and_is_legal) {
  for (uint8_t i = 0; i < SPECIES_TABLE_COUNT; ++i) {
    const SpeciesDef& sp = SPECIES_TABLE[i];
    bool has_damage = false;
    for (uint8_t m = 0; m < (uint8_t)PB_MOVE_COUNT; ++m) {
      CHECK(sp.moves[m] >= 1 && sp.moves[m] <= ATTACK_COUNT);
      const AttackDef* a = attack_get(sp.moves[m]);
      CHECK(a != nullptr);
      if (!a) continue;
      // Own type or NEUTRAL, spec section 13.
      CHECK(a->type == sp.type || a->type == (uint8_t)TYPE_NEUTRAL);
      if (a->power > 0) has_damage = true;
      for (uint8_t n = (uint8_t)(m + 1u); n < (uint8_t)PB_MOVE_COUNT; ++n)
        CHECK(sp.moves[m] != sp.moves[n]);          // four DISTINCT moves
    }
    CHECK(has_damage);
  }
}

TEST(the_type_chart_is_the_three_cornered_cycle) {
  for (uint8_t a = 0; a < (uint8_t)TYPE_COUNT; ++a) {
    CHECK_EQ(TYPE_CHART[a][a], 0);
    for (uint8_t d = 0; d < (uint8_t)TYPE_COUNT; ++d) {
      CHECK(TYPE_CHART[a][d] >= -1 && TYPE_CHART[a][d] <= 1);
      CHECK_EQ(TYPE_CHART[a][d], -TYPE_CHART[d][a]);
    }
  }
  CHECK_EQ(TYPE_CHART[TYPE_SIGNAL][TYPE_CORRUPT], 1);
  CHECK_EQ(TYPE_CHART[TYPE_CORRUPT][TYPE_SYSTEM], 1);
  CHECK_EQ(TYPE_CHART[TYPE_SYSTEM][TYPE_SIGNAL], 1);

  // NEUTRAL never indexes the chart, and an unknown id can only ever cost the
  // attacker - it must never hand out an advantage nobody earned.
  CHECK_EQ(type_mod_of((uint8_t)TYPE_NEUTRAL, (uint8_t)TYPE_SIGNAL), 0);
  CHECK_EQ(type_mod_of((uint8_t)TYPE_SIGNAL, (uint8_t)TYPE_NEUTRAL), 0);
  CHECK_EQ(species_type_mod(0, 1), 0);
  CHECK_EQ(species_type_mod(1, 0), 0);
  CHECK_EQ(species_type_mod((uint8_t)(ATTACK_COUNT + 1), 1), 0);
}

// The roster spans all three types, so the chart is exercised by real content
// and not only by hand-built pairs. A single-type roster would make spec
// section 12 unprovable on anything the player can actually own.
TEST(the_roster_spans_all_three_types_so_the_chart_is_reachable) {
  bool seen[TYPE_COUNT] = { false, false, false };
  for (uint8_t i = 0; i < SPECIES_TABLE_COUNT; ++i) seen[SPECIES_TABLE[i].type] = true;
  for (uint8_t t = 0; t < (uint8_t)TYPE_COUNT; ++t) CHECK(seen[t]);

  // ...and two shipped species really do have a non-zero matchup.
  int nonzero = 0;
  for (uint8_t i = 0; i < SPECIES_TABLE_COUNT; ++i)
    for (uint8_t m = 0; m < (uint8_t)PB_MOVE_COUNT; ++m)
      for (uint8_t j = 0; j < SPECIES_TABLE_COUNT; ++j)
        if (species_type_mod(SPECIES_TABLE[i].moves[m], SPECIES_TABLE[j].id) != 0) nonzero++;
  CHECK(nonzero > 0);
}

// =============================================================================
//  4. OBLIGATION 2 - THE TWO ORPHAN ATTACKS, PINNED WHERE THEY LANDED
// =============================================================================
TEST(the_two_orphan_attacks_are_on_a_learnset) {
  // P3-C3 recorded that attack 26 (Panico) and attack 30 (Cache) were on NO
  // learnset in the pack of the day, i.e. content nobody could ever obtain.
  // The final pack places both. This pins where, so a future retune that
  // orphans them again fails rather than passing quietly.
  int on26 = 0, on30 = 0;
  for (uint8_t i = 0; i < SPECIES_TABLE_COUNT; ++i)
    for (uint8_t m = 0; m < (uint8_t)PB_MOVE_COUNT; ++m) {
      if (SPECIES_TABLE[i].moves[m] == 26) on26++;
      if (SPECIES_TABLE[i].moves[m] == 30) on30++;
    }
  // 26 Panico landed on exactly one species; 30 Cache on two of the shipped
  // twelve families (four across the full 60-species pack: 12, 26, 45, 56).
  CHECK_EQ(on26, 1);      // species 36, Murax   (family 12, SYSTEM,  stage 2)
  CHECK_EQ(on30, 2);      // species 12, Burnix  (family 4,  SIGNAL,  stage 2)
                          // species 26, Voidina (family 9,  CORRUPT, stage 1)
  const SpeciesDef* murax   = species_get(36);
  const SpeciesDef* burnix  = species_get(12);
  const SpeciesDef* voidina = species_get(26);
  CHECK(murax != nullptr && burnix != nullptr && voidina != nullptr);
  if (murax)   CHECK_EQ(murax->moves[1], 26);
  if (burnix)  CHECK_EQ(burnix->moves[1], 30);
  if (voidina) CHECK_EQ(voidina->moves[2], 30);
}

// The general form of the same rule, over the whole shipped table: every
// attack that is NOT on a learnset must be one this roster prefix has not
// reached yet. Anything else is content the player can never obtain.
TEST(every_unreachable_attack_belongs_to_a_family_not_yet_shipped) {
  bool used[64] = { false };
  for (uint8_t i = 0; i < SPECIES_TABLE_COUNT; ++i)
    for (uint8_t m = 0; m < (uint8_t)PB_MOVE_COUNT; ++m)
      used[SPECIES_TABLE[i].moves[m]] = true;

  int unreachable = 0;
  for (uint8_t a = 1; a <= ATTACK_COUNT; ++a) if (!used[a]) unreachable++;
  // 13 (Infeccion, on species 46) and 22 (Firewall, on species 60): both live
  // in families 16 and 20, which the 12-family prefix does not carry. Raising
  // ROSTER_FAMILIES to 20 takes this to 0.
  CHECK_EQ(unreachable, 2);
  CHECK(!used[13]);
  CHECK(!used[22]);
}

// =============================================================================
//  5. OBLIGATION 3 - THE COMPLETENESS PASS
//     Every enum value the content declares has a row, or is named here.
// =============================================================================
TEST(every_attack_category_and_effect_has_at_least_one_row) {
  for (uint8_t c = 0; c < (uint8_t)ATK_CAT_COUNT; ++c) {
    int n = 0;
    for (uint8_t i = 0; i < ATTACK_COUNT; ++i) if (ATTACKS_TABLE[i].category == c) n++;
    CHECK(n > 0);      // all six spec section 13 categories ship
  }
  for (uint8_t e = 0; e < (uint8_t)ATK_EFF_COUNT; ++e) {
    int n = 0;
    for (uint8_t i = 0; i < ATTACK_COUNT; ++i) if (ATTACKS_TABLE[i].effect == e) n++;
    CHECK(n > 0);      // no effect slot exists that nothing uses
  }
}

TEST(every_item_class_and_every_encounter_outcome_has_a_row) {
  for (uint8_t k = 0; k < (uint8_t)ITEM_KLASS_COUNT; ++k) {
    int n = 0;
    for (uint8_t i = 0; i < ITEM_COUNT; ++i) if (ITEMS_TABLE[i].klass == k) n++;
    CHECK(n > 0);
  }
  for (uint8_t o = 0; o < (uint8_t)ENC_OUT_COUNT; ++o) {
    int n = 0;
    for (uint8_t i = 0; i < ENCOUNTER_ROW_COUNT; ++i)
      if (ENCOUNTER_TABLE[i].outcome == o) n++;
    CHECK(n > 0);
  }
}

// THE HOLE THE COMPLETENESS PASS FOUND, NOW ASSERTED AS A CLOSED ONE.
//
// P4-C1 found that spec section 22 names four outcomes, all four have rows, and
// SPECIAL had NO payload table anywhere - no event roster, no ids, no
// per-category weights - while it is 4 to 10 % of every scan. ITEM had the same
// shape of problem and was rescued by ITEM_DROPS; SPECIAL had no equivalent.
//
// P5-C3 FILLED IT (tools/content/specials.json -> SPECIAL_EVENTS[] and
// SPECIAL_DROP_TABLE[]), so the tripwire fired exactly as designed: the case
// pinned CONTENT_VERSION and the pack changed. THE PIN IS DELETED RATHER THAN
// RE-DERIVED, because it was a tripwire for a hole that no longer exists and a
// constant somebody re-derives on every pack edit is a chore, not a guard. What
// replaces it is the same question asked of the thing that now exists: does
// every category's SPECIAL slice resolve to a real event, and does the roster
// behind it have both of the two outcomes the plan promised?
//
// The three generated static_asserts (special_drop_rows_are_well_formed,
// encounter_special_rows_have_an_event, special_kinds_are_all_populated) are
// the compile-time half; this is the runtime half, and it is not a duplicate:
// it walks the PICKER, which a constexpr guard cannot call.
TEST(the_special_outcome_has_a_payload_table_and_every_category_resolves) {
  int special_rows = 0;
  for (uint8_t i = 0; i < ENCOUNTER_ROW_COUNT; ++i)
    if (ENCOUNTER_TABLE[i].outcome == (uint8_t)ENC_OUT_SPECIAL) {
      special_rows++;
      // PER CATEGORY, not summed: a total would let one category fall to a
      // sliver while the others carried it.
      CHECK(ENCOUNTER_TABLE[i].weight >= 4);
      CHECK(ENCOUNTER_TABLE[i].weight <= 10);
    }
  CHECK_EQ(special_rows, (int)NET_CAT_COUNT);      // one per category

  // The roster exists, is contiguous, and every row means something.
  CHECK(SPECIAL_EVENT_COUNT >= 1);
  for (uint8_t i = 0; i < SPECIAL_EVENT_COUNT; ++i) {
    const SpecialEvent& e = SPECIAL_EVENTS[i];
    CHECK_EQ(e.id, (uint8_t)(i + 1u));
    CHECK(e.kind < (uint8_t)SPEV_COUNT);
    CHECK_EQ(e.reserved, 0);
    CHECK(e.name_idx < (uint16_t)STR_COUNT);
    CHECK(S(e.name_idx)[0] != '\0');
    // An XP burst that pays nothing is the item-9 hole in another table.
    if (e.kind == (uint8_t)SPEV_XP_BURST) CHECK(e.value >= 1);
  }
  // BOTH of the two outcomes the plan's P5-C3 bullet promised are reachable,
  // and neither is a kind with no event behind it.
  for (uint8_t k = 0; k < (uint8_t)SPEV_COUNT; ++k) {
    int n = 0;
    for (uint8_t i = 0; i < SPECIAL_EVENT_COUNT; ++i)
      if (SPECIAL_EVENTS[i].kind == k) n++;
    CHECK(n > 0);
  }
  // Every category's weights sum to 100 and name real events.
  for (uint8_t c = 0; c < (uint8_t)NET_CAT_COUNT; ++c) {
    uint32_t sum = 0;
    for (uint8_t i = 0; i < SPECIAL_DROP_ROW_COUNT; ++i)
      if (SPECIAL_DROP_TABLE[i].category == c) {
        sum += SPECIAL_DROP_TABLE[i].weight;
        CHECK(SPECIAL_DROP_TABLE[i].event_id >= 1);
        CHECK(SPECIAL_DROP_TABLE[i].event_id <= SPECIAL_EVENT_COUNT);
        CHECK(SPECIAL_DROP_TABLE[i].weight >= 1);
        CHECK_EQ(SPECIAL_DROP_TABLE[i].reserved, 0);
      }
    CHECK_EQ(sum, (uint32_t)ENCOUNTER_WEIGHT_TOTAL);
  }
}

// A FOURTH HOLE OF THE SAME SHAPE, CLOSED THE WAY P5-C4 CHOSE TO CLOSE IT.
//
// Item 9 Llave Raiz was ITEM_KLASS_CARE with value 0, and items_table.h's own
// unit contract says a CARE item's value is what it restores - so it was a care
// item that restores nothing, reachable at about 2.4 % of every HIDDEN scan.
// The pack folded it onto CARE because spec section 24 names no evolution-item
// class. The old case pinned BOTH halves of the fold and said "give item 9 a
// real value, or ship an ITEM rule, and this case fails".
//
// P5-C4 took the third option instead - ITEM_KLASS_EVOLUTION, the class section
// 24 lacks - so this case is rewritten rather than deleted, and what it now
// pins is the closure and the ONE thing that is still open about it: the key
// has no lock at this roster. game/inventory.h carries the argument for the
// choice; here is the part that can fail.
TEST(the_evolution_key_has_its_own_class_and_no_shipped_rule_spends_it) {
  // No item anywhere is reachable and does nothing. That was the defect, and
  // this is the general form of it rather than a pin on one id.
  for (uint8_t i = 0; i < ITEM_COUNT; ++i) {
    const ItemDef& it = ITEMS_TABLE[i];
    if (it.klass == (uint8_t)ITEM_KLASS_EVOLUTION) continue;   // a key, not a dose
    CHECK(it.value > 0);
  }
  // Item 9 is the key, and it is the only one.
  int keys = 0, last = 0;
  for (uint8_t i = 0; i < ITEM_COUNT; ++i)
    if (ITEMS_TABLE[i].klass == (uint8_t)ITEM_KLASS_EVOLUTION) {
      keys++;
      last = (int)ITEMS_TABLE[i].id;
    }
  CHECK_EQ(keys, 1);
  CHECK_EQ(last, 9);
  // No care item is a key any more: the fold is gone, not merely documented.
  for (uint8_t i = 0; i < ITEM_COUNT; ++i)
    if (ITEMS_TABLE[i].klass == (uint8_t)ITEM_KLASS_CARE)
      CHECK(ITEMS_TABLE[i].value > 0);
  // AND THE HALF THAT IS STILL OPEN, asserted rather than described: no shipped
  // rule can spend the key, because the species 53 -> 54 rule is outside the
  // 36-species prefix. When P9 lands it this fails, and the person who lands it
  // reads game/inventory.h's paragraph and deletes this line.
  for (uint8_t i = 0; i < EVOLUTION_RULES_COUNT; ++i)
    CHECK(EVOLUTION_RULES[i].cond != (uint8_t)EVOC_ITEM);
}

// =============================================================================
//  6. THE ITEM, EVOLUTION AND ENCOUNTER GUARDS
// =============================================================================
TEST(item_rows_are_well_formed_at_runtime) {
  CHECK(ITEM_COUNT >= 1);
  for (uint8_t i = 0; i < ITEM_COUNT; ++i) {
    const ItemDef& it = ITEMS_TABLE[i];
    CHECK_EQ(it.id, (uint8_t)(i + 1u));
    CHECK(it.klass < (uint8_t)ITEM_KLASS_COUNT);
    CHECK(it.rarity <= SPECIES_RARITY_SPECIAL);
    CHECK(it.name_idx < (uint16_t)STR_COUNT);
    CHECK(S(it.name_idx)[0] != '\0');
    // The two bytes that were `reserved[2]` are `target` and `param` since
    // P5-C4, so what used to be "they are zero" is now "they say what the item
    // acts on" - the same question, asked of a column that exists.
    if (it.klass == (uint8_t)ITEM_KLASS_CARE) {
      CHECK(it.target < (uint8_t)CARE_TGT_COUNT);
      CHECK(it.value >= 1 && it.value <= 100);          // percent of a full bar
      CHECK_EQ((uint8_t)(it.param | (uint8_t)ITEM_PBS_MASK), (uint8_t)ITEM_PBS_MASK);
    } else if (it.klass == (uint8_t)ITEM_KLASS_BATTLE_MOD) {
      CHECK(it.target <= (uint8_t)ITEM_BSTAT_SPD);
      CHECK(it.param >= 1);                             // rounds
      CHECK(it.value >= 1);                             // stages
    } else {
      CHECK_EQ(it.target, 0);
      CHECK_EQ(it.param, 0);
    }
  }
  CHECK(item_get(0) == nullptr);
  CHECK(item_get((uint8_t)(ITEM_COUNT + 1)) == nullptr);
  // XP candies scale; nothing else does.
  CHECK_EQ(item_xp_value(1), (uint16_t)(ITEMS_TABLE[0].value * ITEM_XP_CANDY_SCALE));
  for (uint8_t i = 0; i < ITEM_COUNT; ++i)
    if (ITEMS_TABLE[i].klass != (uint8_t)ITEM_KLASS_XP_CANDY)
      CHECK_EQ(item_xp_value(ITEMS_TABLE[i].id), 0);
}

TEST(evolution_rows_are_well_formed_at_runtime) {
  CHECK(EVOLUTION_RULES_COUNT >= 1);
  CHECK(EVOLUTION_RULES_COUNT < (uint8_t)SPECIES_EVO_NONE);
  CHECK_EQ((int)EVOLUTION_RULES_COUNT, (int)SPECIES_FAMILY_COUNT * 2);

  for (uint8_t i = 0; i < EVOLUTION_RULES_COUNT; ++i) {
    const EvolutionRule& r = EVOLUTION_RULES[i];
    const SpeciesDef* s = species_get(r.species);
    const SpeciesDef* t = species_get(r.target);
    CHECK(s != nullptr && t != nullptr);
    if (!s || !t) continue;
    CHECK(r.target != r.species);
    CHECK_EQ(t->family, s->family);
    CHECK_EQ((int)t->stage, (int)s->stage + 1);
    CHECK(r.level >= 1 && r.level <= (uint8_t)PB_LEVEL_MAX);
    CHECK(r.cond < (uint8_t)EVOC_COUNT);
    // The pack's ordering invariant: evo_rule == (family - 1) * 2 + stage.
    CHECK_EQ((int)i, (int)(s->family - 1) * 2 + (int)s->stage);
    for (uint8_t j = (uint8_t)(i + 1u); j < EVOLUTION_RULES_COUNT; ++j)
      CHECK(EVOLUTION_RULES[i].species != EVOLUTION_RULES[j].species);
  }
  for (uint8_t i = 0; i < SPECIES_TABLE_COUNT; ++i) {
    const SpeciesDef& sp = SPECIES_TABLE[i];
    if (sp.evo_rule == SPECIES_EVO_NONE) {
      CHECK_EQ(sp.stage, 2);
      for (uint8_t j = 0; j < EVOLUTION_RULES_COUNT; ++j)
        CHECK(EVOLUTION_RULES[j].species != sp.id);
    } else {
      CHECK(sp.stage < 2);
      CHECK(sp.evo_rule < EVOLUTION_RULES_COUNT);
      CHECK_EQ(EVOLUTION_RULES[sp.evo_rule].species, sp.id);
    }
  }
}

// The generator maps EvoCond BY NAME. If it ever mapped by the pack's ordinal
// instead, the two CORRUPTED families would come out as EVOC_ITEM. This pins
// the mapping against the content, not against the enum.
TEST(the_conditional_rules_carry_the_condition_the_pack_names) {
  const EvolutionRule* r11 = evolution_rule_for(11);   // Artefax -> Burnix
  CHECK(r11 != nullptr);
  if (r11) {
    CHECK_EQ(r11->target, 12);
    CHECK_EQ(r11->level, 20);
    CHECK_EQ(r11->cond, (uint8_t)EVOC_CORRUPTED);
  }
  const EvolutionRule* r32 = evolution_rule_for(32);   // Servik -> Kernon
  CHECK(r32 != nullptr);
  if (r32) {
    CHECK_EQ(r32->target, 33);
    CHECK_EQ(r32->level, 24);
    CHECK_EQ(r32->cond, (uint8_t)EVOC_HAPPINESS_GE);
    CHECK_EQ(r32->cond_value, 70);
  }
  // Exactly two of the twenty-four rules are conditional on this roster.
  int conditional = 0;
  for (uint8_t i = 0; i < EVOLUTION_RULES_COUNT; ++i)
    if (EVOLUTION_RULES[i].cond != (uint8_t)EVOC_NONE) conditional++;
  CHECK_EQ(conditional, 2);
}

TEST(encounter_rows_are_well_formed_at_runtime) {
  for (uint8_t c = 0; c < (uint8_t)NET_CAT_COUNT; ++c) {
    uint32_t sum = 0, nothing = 0;
    for (uint8_t i = 0; i < ENCOUNTER_ROW_COUNT; ++i) {
      const EncounterRow& r = ENCOUNTER_TABLE[i];
      if (r.category != c) continue;
      sum += r.weight;
      if (r.outcome == (uint8_t)ENC_OUT_NOTHING) nothing += r.weight;
    }
    CHECK_EQ(sum, (uint32_t)ENCOUNTER_WEIGHT_TOTAL);
    CHECK(nothing >= ENCOUNTER_NOTHING_MIN_PCT);      // spec section 22
  }
  for (uint8_t i = 0; i < ENCOUNTER_ROW_COUNT; ++i) {
    const EncounterRow& r = ENCOUNTER_TABLE[i];
    CHECK(r.category < (uint8_t)NET_CAT_COUNT);
    CHECK(r.outcome < (uint8_t)ENC_OUT_COUNT);
    CHECK(r.weight > 0);
    CHECK(r.rarity_min <= r.rarity_max);
    CHECK(r.rarity_max <= SPECIES_RARITY_SPECIAL);
    CHECK_EQ(r.reserved[0], 0);
    CHECK_EQ(r.reserved[1], 0);
    CHECK_EQ(r.reserved[2], 0);
  }
}

// EVERY WILD ROW RESOLVES. This is the guard the generator's rarity clamp keeps
// true: a row whose band holds no shipped species is an outcome the picker
// cannot answer, and P5-C3 would have to invent one.
TEST(every_wild_encounter_row_can_actually_produce_a_pebble) {
  for (uint8_t i = 0; i < ENCOUNTER_ROW_COUNT; ++i) {
    const EncounterRow& r = ENCOUNTER_TABLE[i];
    if (r.outcome != (uint8_t)ENC_OUT_WILD) continue;
    const uint16_t w = species_spawn_weight_in(r.category, r.rarity_min, r.rarity_max);
    CHECK(w > 0);
    // ...and the picker really returns a legal species for every roll in range.
    for (uint16_t roll = 0; roll < w; roll += (uint16_t)(w / 7u + 1u)) {
      const uint8_t id = species_pick_by_weight(r.category, r.rarity_min,
                                                r.rarity_max, roll);
      const SpeciesDef* sp = species_get(id);
      CHECK(sp != nullptr);
      if (!sp) continue;
      CHECK(sp->rarity >= r.rarity_min && sp->rarity <= r.rarity_max);
      CHECK((sp->category_mask & NET_CATEGORY_BIT[r.category]) != 0);
    }
  }
}

TEST(every_item_drop_row_resolves_and_the_two_stage_pick_respects_rarity) {
  for (uint8_t c = 0; c < (uint8_t)NET_CAT_COUNT; ++c) {
    uint32_t sum = 0;
    for (uint8_t i = 0; i < ITEM_DROP_ROW_COUNT; ++i)
      if (ITEM_DROP_TABLE[i].category == c) sum += ITEM_DROP_TABLE[i].weight;
    CHECK_EQ(sum, (uint32_t)ENCOUNTER_WEIGHT_TOTAL);
  }
  for (uint8_t i = 0; i < ITEM_DROP_ROW_COUNT; ++i) {
    const ItemDropRow& d = ITEM_DROP_TABLE[i];
    CHECK(d.category < (uint8_t)NET_CAT_COUNT);
    CHECK(item_get(d.item_id) != nullptr);
    CHECK(d.weight > 0);
    CHECK_EQ(d.reserved, 0);
  }
  // The band filter really filters: a common-only band never yields a rare.
  for (uint8_t c = 0; c < (uint8_t)NET_CAT_COUNT; ++c)
    for (uint16_t roll = 0; roll < 100; roll += 7) {
      const uint8_t id = item_pick_drop(c, SPECIES_RARITY_COMMON,
                                        SPECIES_RARITY_COMMON, roll);
      if (id == 0) continue;
      const ItemDef* it = item_get(id);
      CHECK(it != nullptr);
      if (it) CHECK_EQ(it->rarity, SPECIES_RARITY_COMMON);
    }
  // A band with nothing in it returns 0 rather than a wrong item.
  CHECK_EQ(item_pick_drop((uint8_t)NET_CAT_HOME, SPECIES_RARITY_SPECIAL,
                          SPECIES_RARITY_SPECIAL, 0), 0);
  CHECK_EQ(item_pick_drop((uint8_t)NET_CAT_COUNT, 0, 3, 0), 0);
}

// INSTANCE EIGHTEEN OF THIS PROJECT'S RECURRING DEFECT, FOUND INSIDE THE CASE
// THAT LOOKED LIKE THE GUARD, AND MEASURED RATHER THAN ARGUED.
//
// The case above is named "...the_two_stage_pick_respects_rarity" and it checks
// per-category sums, row well-formedness, that a COMMON..COMMON band yields only
// commons and that HOME/SPECIAL..SPECIAL yields 0. It NEVER ONCE pairs an ITEM
// encounter row's OWN band with its OWN category's drop list - which is the
// property the plan says nothing asserts, and the one that decides whether
// encounter_roll() can answer the ITEM outcome at all.
//
// THE MUTATION THAT PROVES THIS CASE CAN FAIL, run before it was written: set
// tools/content/encounters.json's HIDDEN ITEM row band from 1..2 to 3..3 and
// regenerate. The mutated tree ACCEPTS the edit (gen_content.py exits 0,
// --check reports in sync, every static_assert in the OLD headers passed), the
// old case above stays GREEN, and item_pick_drop(HIDDEN, 3, 3, roll) returns 0
// for all 100 rolls - an ITEM outcome nothing can resolve. Only the pack's
// Python gate caught it, and tools/check.sh skips that one with a word when
// python3 is missing.
//
// Two things close it. The compile-time half is the generated
// encounter_item_rows_have_a_drop(), the twin of encounter_wild_rows_have_a_pool()
// that did not exist; this is the runtime half, and it walks the real picker
// over the real band, which a constexpr guard cannot do.
TEST(every_item_encounter_row_can_actually_produce_an_item) {
  int item_rows = 0;
  for (uint8_t i = 0; i < ENCOUNTER_ROW_COUNT; ++i) {
    const EncounterRow& r = ENCOUNTER_TABLE[i];
    if (r.outcome != (uint8_t)ENC_OUT_ITEM) continue;
    item_rows++;

    // The eligible weight of THIS row's band inside THIS row's category.
    uint32_t eligible = 0;
    for (uint8_t d = 0; d < ITEM_DROP_ROW_COUNT; ++d) {
      if (ITEM_DROP_TABLE[d].category != r.category) continue;
      const ItemDef* it = item_get(ITEM_DROP_TABLE[d].item_id);
      CHECK(it != nullptr);
      if (it == nullptr) continue;
      if (it->rarity < r.rarity_min || it->rarity > r.rarity_max) continue;
      eligible += ITEM_DROP_TABLE[d].weight;
    }
    CHECK(eligible > 0);
    if (eligible == 0) continue;

    // ...and the picker really answers, for every roll across that weight, with
    // an item that is IN the band and IN the category's list.
    for (uint32_t roll = 0; roll < eligible; ++roll) {
      const uint8_t id = item_pick_drop(r.category, r.rarity_min, r.rarity_max,
                                        (uint16_t)roll);
      CHECK(id != 0);
      const ItemDef* it = item_get(id);
      CHECK(it != nullptr);
      if (!it) continue;
      CHECK(it->rarity >= r.rarity_min && it->rarity <= r.rarity_max);
      bool listed = false;
      for (uint8_t d = 0; d < ITEM_DROP_ROW_COUNT; ++d)
        if (ITEM_DROP_TABLE[d].category == r.category &&
            ITEM_DROP_TABLE[d].item_id == id) listed = true;
      CHECK(listed);
    }
  }
  CHECK(item_rows >= (int)NET_CAT_COUNT);   // every category has one

  // The SPECIAL twin, for the outcome that had no payload at all until P5-C3.
  int special_rows = 0;
  for (uint8_t i = 0; i < ENCOUNTER_ROW_COUNT; ++i) {
    const EncounterRow& r = ENCOUNTER_TABLE[i];
    if (r.outcome != (uint8_t)ENC_OUT_SPECIAL) continue;
    special_rows++;
    uint32_t eligible = 0;
    for (uint8_t d = 0; d < SPECIAL_DROP_ROW_COUNT; ++d)
      if (SPECIAL_DROP_TABLE[d].category == r.category)
        eligible += SPECIAL_DROP_TABLE[d].weight;
    CHECK(eligible > 0);
  }
  CHECK_EQ(special_rows, (int)NET_CAT_COUNT);
}

// =============================================================================
//  7. OBLIGATION 4 - evolution_apply() DRIVEN THROUGH A FAILING CONDITION
//
//  P3-C3 could not do this: every rule it shipped was EVOC_NONE, so
//  evolution_cond_holds()'s non-trivial arms were unreachable and
//  tests/test_evolution.cpp said so instead of pretending otherwise. Rule 7
//  (Artefax -> Burnix, level 20, EVOC_CORRUPTED) makes them reachable.
// =============================================================================
TEST(apply_refuses_when_the_condition_is_false_and_leaves_the_pebble_alone) {
  PebbleInstance p;
  make_pebble(p, 11, 20);            // Artefax, level 20: the LEVEL gate is met
  const uint16_t hp0 = p.hp_cur;
  const uint8_t  evo0 = p.evo_state;

  EvoContext ctx;
  evo_context_clear(ctx);
  ctx.have      = (uint8_t)(EVOCTX_CORRUPTED | EVOCTX_HAPPINESS |
                            EVOCTX_ITEM | EVOCTX_ACTIVITY);
  ctx.corrupted = 0;                 // supplied, and FALSE
  ctx.happiness = 100;
  ctx.activity  = 100;
  ctx.item_id   = 9;

  CHECK_EQ(evolution_level_ready(p), 1);        // the level half holds...
  CHECK_EQ(evolution_ready(p, ctx), 0);         // ...and the whole rule does not
  CHECK(!evolution_apply(p, ctx));
  CHECK_EQ(p.species_id, 11);                   // nothing moved
  CHECK_EQ(p.hp_cur, hp0);
  CHECK_EQ(p.evo_state, evo0);
  CHECK_EQ(p.evolutions, 0);
}

TEST(apply_refuses_when_the_condition_input_was_never_supplied) {
  // The rule that matters most: an UNSUPPLIED input refuses. A context that
  // says nothing must not be read as "the requirement is met".
  PebbleInstance p;
  make_pebble(p, 11, 20);
  EvoContext empty;
  evo_context_clear(empty);
  CHECK_EQ(evolution_ready(p, empty), 0);
  CHECK(!evolution_apply(p, empty));
  CHECK_EQ(p.species_id, 11);

  // Even a context that supplies every OTHER input still refuses.
  EvoContext wrong;
  evo_context_clear(wrong);
  wrong.have      = (uint8_t)(EVOCTX_HAPPINESS | EVOCTX_ITEM | EVOCTX_ACTIVITY);
  wrong.happiness = 100;
  wrong.activity  = 100;
  wrong.item_id   = 9;
  CHECK_EQ(evolution_ready(p, wrong), 0);
  CHECK(!evolution_apply(p, wrong));
  CHECK_EQ(p.species_id, 11);

  // THE FORM THAT SEPARATES THE BIT FROM THE VALUE, and the reason this case
  // exists at all. Both contexts above leave `corrupted` at 0, so the VALUE
  // check refuses on its own and the have-bit gate is never reached: deleting
  // `if (!(ctx.have & EVOCTX_CORRUPTED)) return 0u;` from evolution.cpp left
  // this whole file at 30/30. A TRUE value with the bit WITHHELD is the only
  // context that tells the two apart - it is what a caller that filled the
  // field but forgot to claim it looks like, and it must still refuse.
  EvoContext lying;
  evo_context_clear(lying);
  lying.have      = 0u;              // nothing is claimed...
  lying.corrupted = 1;               // ...but the field would pass if it were
  CHECK_EQ(evolution_ready(p, lying), 0);
  CHECK(!evolution_apply(p, lying));
  CHECK_EQ(p.species_id, 11);

  // The same shape on the OTHER shipped conditional rule: HAPPINESS_GE 70 with
  // a passing happiness and the bit withheld. Its clear-context cases refuse on
  // 0 < 70 for the same reason, so they do not reach its have-bit either.
  PebbleInstance q;
  make_pebble(q, 32, 24);            // Servik -> Kernon, HAPPINESS_GE 70
  EvoContext lying_happy;
  evo_context_clear(lying_happy);
  lying_happy.have      = 0u;
  lying_happy.happiness = 100;
  CHECK_EQ(evolution_ready(q, lying_happy), 0);
  CHECK(!evolution_apply(q, lying_happy));
  CHECK_EQ(q.species_id, 32);
}

TEST(apply_succeeds_the_moment_the_condition_becomes_true) {
  PebbleInstance p;
  make_pebble(p, 11, 20);
  const uint16_t hp_before = p.hp_cur;

  EvoContext ctx;
  evo_context_clear(ctx);
  ctx.have      = EVOCTX_CORRUPTED;
  ctx.corrupted = 1;

  CHECK_EQ(evolution_ready(p, ctx), 1);
  CHECK(evolution_apply(p, ctx));
  CHECK_EQ(p.species_id, 12);                       // Burnix
  CHECK_EQ((int)(p.evo_state & EVO_STATE_STAGE_MASK), 2);
  CHECK_EQ((int)(p.evo_state & EVO_STATE_PENDING), 0);   // final stage: no re-raise
  CHECK_EQ(p.evolutions, 1);
  // A full Pebble stays full across the base_hp change (4 -> 6).
  const SpeciesDef* to = species_get(12);
  CHECK(to != nullptr);
  if (to) CHECK_EQ(p.hp_cur, xp_hp_max(to->base_hp, 20));
  CHECK(p.hp_cur > hp_before);
}

TEST(the_level_gate_still_binds_when_the_condition_holds) {
  PebbleInstance p;
  make_pebble(p, 11, 19);            // one level short
  EvoContext ctx;
  evo_context_clear(ctx);
  ctx.have = EVOCTX_CORRUPTED;
  ctx.corrupted = 1;
  CHECK_EQ(evolution_level_ready(p), 0);
  CHECK_EQ(evolution_ready(p, ctx), 0);
  CHECK(!evolution_apply(p, ctx));
  CHECK_EQ(p.species_id, 11);
}

TEST(the_happiness_condition_compares_against_the_rules_own_value) {
  PebbleInstance p;
  make_pebble(p, 32, 24);            // Servik -> Kernon, HAPPINESS_GE 70
  EvoContext ctx;
  evo_context_clear(ctx);
  ctx.have = EVOCTX_HAPPINESS;

  ctx.happiness = 69;
  CHECK_EQ(evolution_ready(p, ctx), 0);
  CHECK(!evolution_apply(p, ctx));
  CHECK_EQ(p.species_id, 32);

  ctx.happiness = 70;                // exactly the threshold: >= , not >
  CHECK_EQ(evolution_ready(p, ctx), 1);
  CHECK(evolution_apply(p, ctx));
  CHECK_EQ(p.species_id, 33);
}

// =============================================================================
//  8. CREATOR SCHEMA AND CONTENT_VERSION
// =============================================================================
TEST(every_builtin_row_respects_the_budgets_the_creator_is_held_to) {
  for (uint8_t i = 0; i < SPECIES_TABLE_COUNT; ++i) {
    const SpeciesDef& sp = SPECIES_TABLE[i];
    const uint16_t total = (uint16_t)sp.base_hp + sp.base_atk + sp.base_def + sp.base_spd;
    CHECK_EQ(total, CREATOR_STAT_POINTS_BY_STAGE[sp.stage]);

    uint16_t cost = 0;
    uint8_t  cap  = 0;
    for (uint8_t m = 0; m < (uint8_t)PB_MOVE_COUNT; ++m) {
      const AttackDef* a = attack_get(sp.moves[m]);
      CHECK(a != nullptr);
      if (!a) continue;
      cost += a->budget_cost;
      if (a->power > cap) cap = a->power;
    }
    CHECK(cost <= (uint16_t)(CREATOR_ATTACK_BUDGET_BY_STAGE[sp.stage] +
                             CREATOR_RARITY_BUDGET_BONUS[sp.rarity]));
    CHECK(cap <= CREATOR_POWER_CAP_BY_STAGE[sp.stage]);
  }
  CHECK_EQ(CREATOR_TOTAL_STAT_POINTS, CREATOR_STAT_POINTS_BY_STAGE[1]);
  CHECK(CREATOR_TOTAL_STAT_POINTS < CREATOR_STAT_POINTS_BY_STAGE[2]);
  CHECK_EQ(CREATOR_SPECIES_ID_MIN, (unsigned)(SPECIES_ID_BUILTIN_MAX + 1u));
  CHECK(species_get((uint8_t)CREATOR_SPECIES_ID_MIN) == nullptr);
}

// =============================================================================
//  NAMES THAT FIT, AND GLYPHS THE FONT HAS
//
//  plan P9-C1: "names <= 9 chars at 5x8 width". THE SCOPE OF THIS CASE, said
//  plainly rather than oversold: the emitted names come verbatim out of the JSON
//  through the generator's c_str_literal(), and tools/gen_content.py refuses an
//  over-long or non-Latin-1 string at GENERATE time (width_limit()), so a
//  content author is stopped before this file ever runs. What is left for a
//  runtime case is the part a generate-time guard structurally cannot cover:
//
//    * a static_assert - or a Python guard - only fires where it is run. This is
//      the same argument this file's banner makes about the compile-time table
//      guards, and it is why the runtime copy exists at all.
//    * the guard measures the JSON; this measures THE STRING THE DEVICE DRAWS,
//      after the escaping, the splice into core/strings_es.h and the StrId
//      indirection. An emitter that truncated, double-escaped or mis-indexed
//      would satisfy the generator and fail here.
//    * it covers ALL 576 strings for Latin-1, not just the generated ones. The
//      hand-written half of ES[] is where an em dash or an ellipsis would be
//      pasted in, and strings_es.h:9-14 calls that a review blocker - but until
//      P9-C1 nothing checked it in any build.
//
//  The pixel arithmetic is exact rather than an estimate: u8g2's 5x8 and t0_11b
//  are fixed-advance, so glyphs x advance IS the drawn width.
// =============================================================================
TEST(every_shipped_name_fits_the_box_the_screen_gives_it) {
  const int kAdv5x8   = 5;    // u8g2_font_5x8_tf
  const int kAdvT011b = 6;    // u8g2_font_t0_11b_tf

  for (uint8_t i = 0; i < SPECIES_TABLE_COUNT; ++i) {
    const SpeciesDef& sp = SPECIES_TABLE[i];
    uint32_t cp = 0;
    const int n = utf8_glyphs(S(sp.name_idx), &cp);
    CHECK(n >= 1);
    CHECK(n <= 9);                       // HOME/BOX draw it beside a level
    CHECK(n * kAdv5x8 <= 45);
    CHECK(cp <= 0xFFu);
    const int f = utf8_glyphs(S(sp.flavor_idx), &cp);
    CHECK(f >= 1);
    CHECK(f <= 25);                      // one line of the 128 px panel
    CHECK(f * kAdv5x8 <= 128);
    CHECK(cp <= 0xFFu);
  }
  for (uint8_t i = 0; i < ATTACK_COUNT; ++i) {
    uint32_t cp = 0;
    const int n = utf8_glyphs(S(ATTACKS_TABLE[i].name_idx), &cp);
    CHECK(n >= 1);
    CHECK(n <= 10);                      // four in the battle menu's two columns
    CHECK(n * kAdvT011b <= 64);
    CHECK(cp <= 0xFFu);
  }
  for (uint8_t i = 0; i < ITEM_COUNT; ++i) {
    uint32_t cp = 0;
    const int n = utf8_glyphs(S(ITEMS_TABLE[i].name_idx), &cp);
    CHECK(n >= 1);
    CHECK(n <= 12);                      // the bag list, after a 12 px icon
    CHECK(cp <= 0xFFu);
  }
  // Every string in the product, generated and hand-written alike: the fonts
  // carry ASCII + Latin-1 and nothing else.
  for (uint16_t i = 0; i < (uint16_t)STR_COUNT; ++i) {
    uint32_t cp = 0;
    (void)utf8_glyphs(S(i), &cp);
    CHECK(cp <= 0xFFu);
  }
}

// =============================================================================
//  CONTENT_VERSION
//
//  WHAT THIS CASE CAN AND CANNOT CHECK, because the version it replaces claimed
//  more than it did. P9-C1 measured the old one: `!= 0`, `!= 1`, `<= 0xFFFF`
//  rejects 2 of 65,536 values, and rebuilding this binary with
//  -DCONTENT_VERSION=0x1234, 0xFFFF or 0x0002 passed every time. Its NAME was
//  the plan's requirement - "CONTENT_VERSION changes when the JSON changes" -
//  and its body proved only that the header was not zeroed.
//
//  IT COULD NOT HAVE DONE BETTER AND NEITHER CAN THIS ONE: a C++ test never sees
//  tools/content/*.json, so it cannot compare a hash against its input. That
//  property is now checked where both halves exist - `tools/gen_content.py
//  --selftest`, run by tools/check.sh, which perturbs each pack file ONE AT A
//  TIME and requires the version to move for every one of them, requires it not
//  to move for a `_`-prefixed design note, and requires the roster size to be in
//  it. This case is renamed to what it actually asserts: the value is not one of
//  the two sentinels, and it fits every field it is stamped into.
// =============================================================================
TEST(content_version_is_not_a_sentinel_and_fits_every_field_it_is_stamped_into) {
  CHECK(CONTENT_VERSION != 0);      // what a zeroed save blob carries
  CHECK(CONTENT_VERSION != 1);      // the placeholder before the generator owned it
  // BoxHeader.content_version, BattleSetup.content_ver and the session
  // handshake all carry it as a uint16_t; a wider value would be silently
  // truncated on the way into a save and compare unequal on the way out.
  CHECK(CONTENT_VERSION <= 0xFFFFu);
  CHECK_EQ((unsigned)(uint16_t)CONTENT_VERSION, (unsigned)CONTENT_VERSION);
}

// =============================================================================
//  9. THE LOOKUPS THEMSELVES
// =============================================================================
TEST(species_get_refuses_everything_outside_the_roster) {
  CHECK(species_get(0) == nullptr);
  CHECK(species_get(SPECIES_TABLE_COUNT) != nullptr);
  CHECK(species_get((uint8_t)(SPECIES_TABLE_COUNT + 1)) == nullptr);
  CHECK(species_get(200) == nullptr);      // the creator range, until P8
  CHECK(species_get(255) == nullptr);
  for (uint8_t id = 1; id <= SPECIES_TABLE_COUNT; ++id)
    CHECK_EQ(species_get(id)->id, id);
}

TEST(the_weighted_pick_is_deterministic_and_stays_inside_the_pool) {
  const uint16_t w = species_spawn_weight_in((uint8_t)NET_CAT_HOME,
                                             SPECIES_RARITY_COMMON,
                                             SPECIES_RARITY_COMMON);
  CHECK(w > 0);
  for (uint16_t roll = 0; roll < 300; ++roll) {
    const uint8_t a = species_pick_by_weight((uint8_t)NET_CAT_HOME,
                                             SPECIES_RARITY_COMMON,
                                             SPECIES_RARITY_COMMON, roll);
    const uint8_t b = species_pick_by_weight((uint8_t)NET_CAT_HOME,
                                             SPECIES_RARITY_COMMON,
                                             SPECIES_RARITY_COMMON, roll);
    CHECK_EQ(a, b);                       // same roll, same species, always
    CHECK(a != 0);
    // ...and a roll past the pool wraps rather than falling off the end.
    CHECK(species_pick_by_weight((uint8_t)NET_CAT_HOME, SPECIES_RARITY_COMMON,
                                 SPECIES_RARITY_COMMON, (uint16_t)(roll + w)) == a);
  }
  // An empty pool answers 0, and an invalid category answers 0.
  CHECK_EQ(species_pick_by_weight((uint8_t)NET_CAT_COUNT, 0, 3, 0), 0);
  CHECK_EQ(species_spawn_weight_in((uint8_t)NET_CAT_HOME, 3, 0), 0);
}
