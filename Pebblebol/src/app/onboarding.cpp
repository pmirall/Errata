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
//  THE ORDER THE QUESTIONS ARE ASKED IN, WHICH IS NOT THE ORDER THEY ARE
//  NUMBERED IN - and this table is the whole reason it can be changed at all.
//
//  It used to be `step + 1`, which welded the ASKING ORDER to the PERSISTED
//  VALUES. Those values live in two bits of Config.flags (onboarding.h), all
//  four are spoken for, and OB_DONE has to stay 0 for every save written before
//  this feature to decode as "already set up" - so reordering by renumbering
//  would have cost a save migration for what is a presentation decision.
//
//  AND THE ORDER CHANGED: PICK, THEN NAME, THEN TIME. Naming the device before
//  the player has met the creature meant typing a name for nothing in
//  particular - and then, two screens later, being shown three bugs one of
//  which is now called that. Choosing first makes the name a name FOR the thing
//  on the panel, which is what the naming screen was always trying to be. The
//  clock stays last because it is the one question with no character in it.
// -----------------------------------------------------------------------------
static constexpr uint8_t kOrder[OB_STEP_COUNT - 1] = {
  (uint8_t)OB_STARTER,   // meet it
  (uint8_t)OB_NAME,      // name it
  (uint8_t)OB_TIME       // and tell it what day it is
};

// EVERY REAL STEP APPEARS EXACTLY ONCE AND OB_DONE APPEARS NOWHERE. A table
// with a repeat is a wizard with a loop in it, and a table that dropped a step
// is a question the player is never asked - both of which compile.
static_assert([]{
  uint8_t seen = 0;
  for (uint8_t i = 0; i < (uint8_t)(OB_STEP_COUNT - 1); ++i) {
    if (kOrder[i] == (uint8_t)OB_DONE) return false;
    if (kOrder[i] >= (uint8_t)OB_STEP_COUNT) return false;
    const uint8_t bit = (uint8_t)(1u << kOrder[i]);
    if (seen & bit) return false;
    seen = (uint8_t)(seen | bit);
  }
  return true;
}(), "the onboarding order repeats a step, drops one, or asks OB_DONE");

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
  return first_run ? (uint8_t)kOrder[0] : (uint8_t)OB_DONE;
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
  // is a caller error, and answering the first question would restart the
  // wizard.
  for (uint8_t i = 0; i + 1u < (uint8_t)(OB_STEP_COUNT - 1); ++i)
    if (step == kOrder[i]) return kOrder[i + 1u];
  return (uint8_t)OB_DONE;
}
