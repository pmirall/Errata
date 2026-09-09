// =============================================================================
//  ERRATA - data/attacks_table.h
//
//  GENERATED FILE. Do not edit: tools/gen_content.py rewrites it from
//  tools/content/*.json. `tools/gen_content.py --check` fails the gate if
//  this file and the JSON have drifted apart.
//
//  THE ATTACK TABLE and TYPE_CHART (plan 1.5.2, spec sections 12 and 13).
//
//  An attack is DATA. Spec section 13: "the combat engine must resolve
//  attacks through data, not hard-coded species-specific logic", so every
//  field a round needs is a column here and P4-C2 reads nothing else.
//
//  This header also carries the SPECIES <-> ATTACK cross guard, because it
//  is the first file that has seen both tables.
// =============================================================================

#ifndef ER_ATTACKS_TABLE_H
#define ER_ATTACKS_TABLE_H

#include <stdint.h>
#include <stddef.h>

#include "species_table.h"        // BugType, TYPE_CHART dimension, the roster
#include "../core/strings_es.h"   // StrId: name_idx

// Attack categories (spec section 13). All six ship.
enum AttackCategory : uint8_t {
  ATK_CAT_DAMAGE,
  ATK_CAT_DEFENSIVE,
  ATK_CAT_OFF_BUFF,
  ATK_CAT_SPEED,
  ATK_CAT_PROTECT,
  ATK_CAT_RISK,
  ATK_CAT_COUNT
};

// What an attack DOES besides damage. One slot per shipped effect.
enum AttackEffect : uint8_t {
  ATK_EFF_NONE,
  ATK_EFF_BUFF_ATK,
  ATK_EFF_BUFF_DEF,
  ATK_EFF_BUFF_SPD,
  ATK_EFF_DEBUFF_ATK,
  ATK_EFF_DEBUFF_DEF,
  ATK_EFF_DEBUFF_SPD,
  ATK_EFF_PROTECT_HALF,
  ATK_EFF_HEAL_PCT,
  ATK_EFF_DRAIN_PCT,
  ATK_EFF_RECOIL_PCT,
  ATK_EFF_SELF_STUN,
  ATK_EFF_CLEANSE,
  ATK_EFF_SELF_DEBUFF_DEF,
  ATK_EFF_DOT,
  ATK_EFF_EFF_CORRUPT,
  ATK_EFF_COUNT
};

struct AttackDef {          // 16 B, plan 1.5.2
  uint8_t  id;                // 1..ATTACK_COUNT, contiguous == index + 1
  uint8_t  type;              // BugType, TYPE_NEUTRAL allowed
  uint8_t  category;          // AttackCategory
  uint8_t  power;             // 0..100, 0 for a pure status move
  uint8_t  accuracy;          // 0..100
  int8_t   priority;          // resolved BEFORE effective speed (spec 14 step 2)
  uint8_t  effect;            // AttackEffect
  uint8_t  effect_value;      // magnitude; unit depends on effect
  uint8_t  effect_duration;   // rounds; 0 = instant
  uint8_t  cooldown;          // rounds unavailable after use
  uint8_t  anim_id;           // index into the 14 animation films (P10-C3)
  uint8_t  budget_cost;       // spec section 36 creator pricing
  uint16_t name_idx;          // StrId of the Spanish attack name
  uint8_t  reserved[2];       // must be 0
};
static_assert(sizeof(AttackDef) == 16, "AttackDef layout drifted");

inline constexpr AttackDef ATTACKS_TABLE[] = {
  //  id type          category          pow acc pri effect                  val dur cd anim cost  name
  {  1, TYPE_SIGNAL,  ATK_CAT_DAMAGE,    35, 100,  0, ATK_EFF_NONE,            0,  0,  0,  1,  35, STR_ATK_NAME_1,   { 0, 0 } },   // Ping
  {  2, TYPE_SIGNAL,  ATK_CAT_DAMAGE,    55,  95,  0, ATK_EFF_NONE,            0,  0,  0,  1,  52, STR_ATK_NAME_2,   { 0, 0 } },   // Pulso
  {  3, TYPE_SIGNAL,  ATK_CAT_DAMAGE,    75,  85,  0, ATK_EFF_NONE,            0,  0,  0,  2,  63, STR_ATK_NAME_3,   { 0, 0 } },   // Ráfaga
  {  4, TYPE_SIGNAL,  ATK_CAT_DAMAGE,    40, 100,  1, ATK_EFF_NONE,            0,  0,  0,  3,  52, STR_ATK_NAME_4,   { 0, 0 } },   // Adelanto
  {  5, TYPE_SIGNAL,  ATK_CAT_DAMAGE,    35,  90,  0, ATK_EFF_DEBUFF_SPD,      1,  3,  0,  4,  54, STR_ATK_NAME_5,   { 0, 0 } },   // Interferir
  {  6, TYPE_SIGNAL,  ATK_CAT_OFF_BUFF,   0, 100,  0, ATK_EFF_BUFF_ATK,        1,  3,  0, 12,  21, STR_ATK_NAME_6,   { 0, 0 } },   // Amplificar
  {  7, TYPE_SIGNAL,  ATK_CAT_DEFENSIVE,   0, 100,  0, ATK_EFF_BUFF_DEF,        1,  3,  0, 11,  21, STR_ATK_NAME_7,   { 0, 0 } },   // Antena
  {  8, TYPE_SIGNAL,  ATK_CAT_RISK,      90,  75,  0, ATK_EFF_RECOIL_PCT,     25,  0,  0,  2,  51, STR_ATK_NAME_8,   { 0, 0 } },   // Eco Doble
  {  9, TYPE_CORRUPT, ATK_CAT_DAMAGE,    35, 100,  0, ATK_EFF_NONE,            0,  0,  0,  5,  35, STR_ATK_NAME_9,   { 0, 0 } },   // Bytazo
  { 10, TYPE_CORRUPT, ATK_CAT_DAMAGE,    55,  95,  0, ATK_EFF_NONE,            0,  0,  0,  5,  52, STR_ATK_NAME_10,  { 0, 0 } },   // Mordisco
  { 11, TYPE_CORRUPT, ATK_CAT_DAMAGE,    75,  85,  0, ATK_EFF_NONE,            0,  0,  0,  6,  63, STR_ATK_NAME_11,  { 0, 0 } },   // Plaga
  { 12, TYPE_CORRUPT, ATK_CAT_DAMAGE,    30,  90,  0, ATK_EFF_EFF_CORRUPT,    35,  3,  3,  8,  49, STR_ATK_NAME_12,  { 0, 0 } },   // Infectar
  { 13, TYPE_CORRUPT, ATK_CAT_DAMAGE,    20, 100,  0, ATK_EFF_DOT,             5,  3,  0,  6,  50, STR_ATK_NAME_13,  { 0, 0 } },   // Infección
  { 14, TYPE_CORRUPT, ATK_CAT_DAMAGE,    50,  90,  0, ATK_EFF_DRAIN_PCT,      50,  0,  0,  7,  65, STR_ATK_NAME_14,  { 0, 0 } },   // Devorar
  { 15, TYPE_CORRUPT, ATK_CAT_SPEED,      0,  90,  0, ATK_EFF_DEBUFF_DEF,      1,  3,  0,  8,  23, STR_ATK_NAME_15,  { 0, 0 } },   // Corromper
  { 16, TYPE_CORRUPT, ATK_CAT_OFF_BUFF,   0, 100,  0, ATK_EFF_BUFF_ATK,        2,  2,  3, 12,  20, STR_ATK_NAME_16,  { 0, 0 } },   // Frenesí
  { 17, TYPE_CORRUPT, ATK_CAT_RISK,      85,  80,  0, ATK_EFF_SELF_DEBUFF_DEF,  1,  3,  0,  6,  50, STR_ATK_NAME_17,  { 0, 0 } },   // Gusano
  { 18, TYPE_SYSTEM,  ATK_CAT_DAMAGE,    35, 100,  0, ATK_EFF_NONE,            0,  0,  0,  9,  35, STR_ATK_NAME_18,  { 0, 0 } },   // Escaneo
  { 19, TYPE_SYSTEM,  ATK_CAT_DAMAGE,    55,  95,  0, ATK_EFF_NONE,            0,  0,  0,  9,  52, STR_ATK_NAME_19,  { 0, 0 } },   // Núcleo
  { 20, TYPE_SYSTEM,  ATK_CAT_DAMAGE,    75,  85,  0, ATK_EFF_NONE,            0,  0,  0, 10,  63, STR_ATK_NAME_20,  { 0, 0 } },   // Sobrecarga
  { 21, TYPE_SYSTEM,  ATK_CAT_DAMAGE,    30,  90,  0, ATK_EFF_DEBUFF_ATK,      1,  3,  2,  9,  42, STR_ATK_NAME_21,  { 0, 0 } },   // Bloqueo
  { 22, TYPE_SYSTEM,  ATK_CAT_PROTECT,    0, 100,  2, ATK_EFF_PROTECT_HALF,    0,  1,  3, 11,  34, STR_ATK_NAME_22,  { 0, 0 } },   // Firewall
  { 23, TYPE_SYSTEM,  ATK_CAT_DEFENSIVE,   0, 100,  0, ATK_EFF_BUFF_DEF,        1,  5,  4, 11,  19, STR_ATK_NAME_23,  { 0, 0 } },   // Cifrado
  { 24, TYPE_SYSTEM,  ATK_CAT_OFF_BUFF,   0, 100,  0, ATK_EFF_BUFF_ATK,        1,  5,  4, 12,  19, STR_ATK_NAME_24,  { 0, 0 } },   // Permisos
  { 25, TYPE_SYSTEM,  ATK_CAT_SPEED,      0, 100,  0, ATK_EFF_BUFF_SPD,        1,  3,  0, 12,  21, STR_ATK_NAME_25,  { 0, 0 } },   // Reinicio
  { 26, TYPE_SYSTEM,  ATK_CAT_RISK,      95,  70, -1, ATK_EFF_SELF_STUN,       1,  1,  2, 10,  36, STR_ATK_NAME_26,  { 0, 0 } },   // Pánico
  { 27, TYPE_NEUTRAL, ATK_CAT_DAMAGE,    50, 100,  0, ATK_EFF_NONE,            0,  0,  0,  1,  50, STR_ATK_NAME_27,  { 0, 0 } },   // Choque
  { 28, TYPE_NEUTRAL, ATK_CAT_RISK,     100,  55,  0, ATK_EFF_NONE,            0,  0,  2, 14,  47, STR_ATK_NAME_28,  { 0, 0 } },   // Apuesta
  { 29, TYPE_NEUTRAL, ATK_CAT_DEFENSIVE,   0, 100,  0, ATK_EFF_BUFF_DEF,        1,  2,  0, 11,  16, STR_ATK_NAME_29,  { 0, 0 } },   // Defrag
  { 30, TYPE_NEUTRAL, ATK_CAT_PROTECT,    0, 100,  2, ATK_EFF_PROTECT_HALF,    0,  1,  2, 11,  38, STR_ATK_NAME_30,  { 0, 0 } },   // Caché
  { 31, TYPE_NEUTRAL, ATK_CAT_PROTECT,    0, 100,  2, ATK_EFF_PROTECT_HALF,    0,  2,  4, 11,  44, STR_ATK_NAME_31,  { 0, 0 } },   // Sandbox
  { 32, TYPE_NEUTRAL, ATK_CAT_SPEED,      0, 100,  0, ATK_EFF_BUFF_SPD,        1,  3,  0, 12,  21, STR_ATK_NAME_32,  { 0, 0 } },   // Overclock
  { 33, TYPE_NEUTRAL, ATK_CAT_DEFENSIVE,   0, 100,  0, ATK_EFF_HEAL_PCT,       25,  0,  4, 13,  21, STR_ATK_NAME_33,  { 0, 0 } },   // Backup
  { 34, TYPE_NEUTRAL, ATK_CAT_SPEED,      0, 100,  1, ATK_EFF_CLEANSE,         0,  0,  0, 13,  14, STR_ATK_NAME_34,  { 0, 0 } },   // Depurar
};

inline constexpr uint8_t ATTACK_COUNT =
    (uint8_t)(sizeof(ATTACKS_TABLE) / sizeof(ATTACKS_TABLE[0]));

// TYPE_CHART[attacker][defender], +1 advantage / 0 neutral / -1 disadvantage
// (spec section 12). A NEUTRAL attack never indexes this table: its modifier is
// 0 unconditionally, which is why the array is TYPE_COUNT and not
// TYPE_ATTACK_COUNT wide.
inline constexpr int8_t TYPE_CHART[TYPE_COUNT][TYPE_COUNT] = {
  {  0,  1, -1 },   // TYPE_SIGNAL attacking
  { -1,  0,  1 },   // TYPE_CORRUPT attacking
  {  1, -1,  0 },   // TYPE_SYSTEM attacking
};

// The chart is the SIGNAL > CORRUPT > SYSTEM > SIGNAL cycle and nothing else:
// no self-advantage, and every pair is antisymmetric.
constexpr bool type_chart_is_a_cycle(void) {
  for (uint8_t a = 0; a < (uint8_t)TYPE_COUNT; ++a) {
    if (TYPE_CHART[a][a] != 0) return false;
    for (uint8_t d = 0; d < (uint8_t)TYPE_COUNT; ++d) {
      if (TYPE_CHART[a][d] < -1 || TYPE_CHART[a][d] > 1) return false;
      if (TYPE_CHART[a][d] != -TYPE_CHART[d][a]) return false;
    }
  }
  return true;
}

constexpr bool attack_rows_are_well_formed(void) {
  for (uint8_t i = 0; i < (uint8_t)(sizeof(ATTACKS_TABLE) / sizeof(ATTACKS_TABLE[0])); ++i) {
    const AttackDef& a = ATTACKS_TABLE[i];
    if (a.id != (uint8_t)(i + 1u))                    return false;
    if (a.type > (uint8_t)TYPE_NEUTRAL)               return false;
    if (a.category >= (uint8_t)ATK_CAT_COUNT)         return false;
    if (a.effect >= (uint8_t)ATK_EFF_COUNT)           return false;
    if (a.power > 100u)                               return false;
    if (a.accuracy == 0u || a.accuracy > 100u)        return false;
    if (a.anim_id == 0u)                              return false;
    if (a.name_idx >= (uint16_t)STR_COUNT)            return false;
    if (a.reserved[0] != 0u || a.reserved[1] != 0u)   return false;
  }
  return true;
}

// plan 1.5.2: "every moves[i] < ATTACK_COUNT". THE PLAN'S LITERAL FORM IS OFF
// BY ONE and would reject the last attack in the table: attack ids are 1-based
// (ATTACKS_TABLE[id - 1]), so the true bound is 1 <= id <= ATTACK_COUNT. This
// is the guard species_table.h could not carry until an attack table existed;
// its own comment said so and said P4-C1 would add it without moving a number.
constexpr bool species_learnsets_resolve(void) {
  for (uint8_t i = 0; i < SPECIES_TABLE_COUNT; ++i) {
    const SpeciesDef& sp = SPECIES_TABLE[i];
    for (uint8_t m = 0; m < (uint8_t)ER_MOVE_COUNT; ++m) {
      if (sp.moves[m] < 1u || sp.moves[m] > ATTACK_COUNT) return false;
      for (uint8_t n = (uint8_t)(m + 1u); n < (uint8_t)ER_MOVE_COUNT; ++n)
        if (sp.moves[m] == sp.moves[n]) return false;          // 4 DISTINCT moves
    }
  }
  return true;
}

// Spec section 13's learnset rule, as the content pack states it: every move a
// species knows is its own type or NEUTRAL, and at least one of the four does
// damage. A learnset of four status moves cannot win a battle.
constexpr bool species_learnsets_are_legal(void) {
  for (uint8_t i = 0; i < SPECIES_TABLE_COUNT; ++i) {
    const SpeciesDef& sp = SPECIES_TABLE[i];
    bool has_damage = false;
    for (uint8_t m = 0; m < (uint8_t)ER_MOVE_COUNT; ++m) {
      // species_learnsets_resolve() above already rejects an id outside
      // 1..ATTACK_COUNT by name, and its static_assert is declared first. The
      // bound is repeated here anyway because THIS function INDEXES the table:
      // a constexpr read past the end is not a false return, it is an
      // "array subscript ... outside the bounds" error with no sentence in it,
      // and it would be what an operator saw if that guard were ever deleted.
      if (sp.moves[m] < 1u || sp.moves[m] > ATTACK_COUNT) return false;
      const AttackDef& a = ATTACKS_TABLE[sp.moves[m] - 1u];
      if (a.type != sp.type && a.type != (uint8_t)TYPE_NEUTRAL) return false;
      if (a.power > 0u) has_damage = true;
    }
    if (!has_damage) return false;
  }
  return true;
}

static_assert(ATTACK_COUNT >= 1, "the attack table is empty");
static_assert(type_chart_is_a_cycle(),
              "TYPE_CHART is not the antisymmetric SIGNAL>CORRUPT>SYSTEM>SIGNAL cycle");
static_assert(attack_rows_are_well_formed(),
              "an attack row has a bad id, type, category, effect, accuracy or string index");
static_assert(species_learnsets_resolve(),
              "a species learnset holds an unknown or repeated attack id");
static_assert(species_learnsets_are_legal(),
              "a learnset holds an off-type attack, has no damaging move, or holds "
              "an id outside 1..ATTACK_COUNT (which the guard above names first)");

// Resolves an attack id. 0 is the empty move slot of BugInstance.moves and
// returns nullptr, exactly like an id past the table.
inline const AttackDef* attack_get(uint8_t id) {
  if (id < 1u || id > ATTACK_COUNT) return nullptr;
  return &ATTACKS_TABLE[id - 1u];
}

// The type modifier of `atk_type` against `def_type`: -1, 0 or +1. NEUTRAL and
// anything out of range answer 0 rather than indexing the chart.
inline int8_t type_mod_of(uint8_t atk_type, uint8_t def_type) {
  if (atk_type >= (uint8_t)TYPE_COUNT || def_type >= (uint8_t)TYPE_COUNT) return 0;
  return TYPE_CHART[atk_type][def_type];
}

#endif // ER_ATTACKS_TABLE_H
