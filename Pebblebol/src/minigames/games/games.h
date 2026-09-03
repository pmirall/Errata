// =============================================================================
//  PEBBLEBOL - minigames/games/games.h
//  Every game's PURE logic, declared once.
//
//  It exists for a language reason as much as an organisational one: a
//  namespace-scope `const MgLogic MG_PING = {...}` has INTERNAL linkage in
//  C++, so without a declaration here each definition would be private to its
//  own translation unit and nothing could link to it. Declaring them extern
//  first is what gives them external linkage.
//
//  PURE: it names logic only. The draw halves are paired with these in a
//  device translation unit (minigames/registry.cpp), which is the only place
//  that may see both.
// =============================================================================
#ifndef PB_MG_GAMES_H
#define PB_MG_GAMES_H

#include "../minigame.h"

extern const MgLogic MG_PING;        // spec 29.1
extern const MgLogic MG_SEQUENCE;    // spec 29, "additional candidates"

// --- accessors the draw halves and the tests share, so neither re-derives a
//     rule the logic already owns -------------------------------------------
bool     ping_is_lit(const MgCtx& c);
uint8_t  ping_target(const MgCtx& c);
uint32_t ping_arm_ms(const MgCtx& c);
uint16_t ping_last_ms(const MgCtx& c);
uint32_t ping_window_ms(void);
uint8_t  ping_rounds(void);

int8_t   seq_lit_side(const MgCtx& c);     // -1 while the gap is on
bool     seq_is_showing(const MgCtx& c);
uint8_t  seq_len_of(const MgCtx& c);
uint8_t  seq_pos_of(const MgCtx& c);
uint8_t  seq_levels(void);
uint8_t  seq_symbol_at(const MgCtx& c, uint8_t i);   // scripted tests only

#endif  // PB_MG_GAMES_H
