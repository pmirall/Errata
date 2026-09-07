// =============================================================================
//  PEBBLEBOL - app/onboarding.cpp
//  See onboarding.h. Pure translation unit.
// =============================================================================
#include "onboarding.h"

#include "../data/species_table.h"

// -----------------------------------------------------------------------------
//  THE TRIO
//
//  Paketo is first and is the one a player who presses nothing keeps: it is
//  SPECIES_ID_STARTER, the species every device shipped with before P10-C4, so
//  declining to choose changes nothing about the device that existed yesterday.
//  The other two are the other two corners of the type chart.
// -----------------------------------------------------------------------------
static constexpr uint8_t kStarters[OB_STARTER_COUNT] = {
  1u,    // Paketo  - TYPE_SIGNAL,  family 1
  16u,   // Buggo   - TYPE_CORRUPT, family 6
  31u    // Daemi   - TYPE_SYSTEM,  family 11
};

// EVERY CLAIM THE PARAGRAPH ABOVE MAKES, CHECKED AGAINST THE GENERATED ROSTER.
// A content edit that renumbers a family, promotes one of these out of stage 0
// or takes away its evolution rule fails the BUILD here rather than shipping a
// starter that cannot grow up.
static_assert(kStarters[0] == (uint8_t)SPECIES_ID_STARTER,
              "the first starter must stay the historical one: a player who "
              "chooses nothing keeps the Pebble every earlier device had");
#define OB_ASSERT_STARTER(i)                                                   \
  static_assert(SPECIES_TABLE[kStarters[i] - 1].id == kStarters[i],            \
                "starter " #i " is not where the contiguous roster says");     \
  static_assert(SPECIES_TABLE[kStarters[i] - 1].stage == 0,                    \
                "starter " #i " is not a stage-0 species");                    \
  static_assert(SPECIES_TABLE[kStarters[i] - 1].rarity == SPECIES_RARITY_COMMON,\
                "starter " #i " is not a common");                             \
  static_assert(SPECIES_TABLE[kStarters[i] - 1].evo_rule != SPECIES_EVO_NONE,  \
                "starter " #i " cannot evolve - a starter has to grow up")
OB_ASSERT_STARTER(0);
OB_ASSERT_STARTER(1);
OB_ASSERT_STARTER(2);
#undef OB_ASSERT_STARTER

// One per corner of the three-cornered chart (data/species_table.h): the choice
// is a real choice and not three coats of paint.
static_assert(SPECIES_TABLE[kStarters[0] - 1].type == TYPE_SIGNAL,  "starter 0 type");
static_assert(SPECIES_TABLE[kStarters[1] - 1].type == TYPE_CORRUPT, "starter 1 type");
static_assert(SPECIES_TABLE[kStarters[2] - 1].type == TYPE_SYSTEM,  "starter 2 type");

uint8_t ob_starter_species(uint8_t index)
{
  return (index < (uint8_t)OB_STARTER_COUNT) ? kStarters[index] : 0u;
}

// -----------------------------------------------------------------------------
//  THE PERSISTED STEP
// -----------------------------------------------------------------------------
uint8_t ob_step(const Config& c)
{
  const uint8_t v = (uint8_t)((c.flags & CF_SETUP_MASK) >> CF_SETUP_SH);
  return (v < (uint8_t)OB_STEP_COUNT) ? v : (uint8_t)OB_DONE;
}

void ob_set_step(Config& c, uint8_t step)
{
  if (step >= (uint8_t)OB_STEP_COUNT) step = (uint8_t)OB_DONE;
  c.flags = (uint8_t)((c.flags & (uint8_t)~CF_SETUP_MASK) |
                      (uint8_t)((step << CF_SETUP_SH) & CF_SETUP_MASK));
}

uint8_t ob_boot_step(bool first_run, bool readonly, const Config& c)
{
  if (readonly) return (uint8_t)OB_DONE;
  // A STORED STEP OUTRANKS THE BOOT KIND, and the order is the point. Asking
  // "is this a first run" first is what the old app.cpp rule did, and it is
  // what a power cut halfway through defeats: the second boot is not a first
  // run, so the two remaining questions were never asked. An interrupted flow
  // resumes whatever hardware/boot.cpp thinks happened.
  const uint8_t stored = ob_step(c);
  if (stored != (uint8_t)OB_DONE) return stored;
  return first_run ? (uint8_t)OB_NAME : (uint8_t)OB_DONE;
}

ScreenId ob_screen_for(uint8_t step)
{
  switch (step) {
    case OB_NAME:    return SCR_SETUP_NAME;
    case OB_TIME:    return SCR_TIME;
    case OB_STARTER: return SCR_SETUP_STARTER;
    default:         return SCR_HOME;
  }
}

uint8_t ob_next(uint8_t step)
{
  // OB_DONE is not a step and has no successor: asking what follows "finished"
  // is a caller error, and answering OB_NAME would restart the wizard.
  if (step == (uint8_t)OB_DONE || step + 1u >= (uint8_t)OB_STEP_COUNT)
    return (uint8_t)OB_DONE;
  return (uint8_t)(step + 1u);
}
