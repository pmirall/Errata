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

extern const MgLogic MG_PING;          // spec 29.1
extern const MgLogic MG_SEQUENCE;      // spec 29, "additional candidates"
extern const MgLogic MG_PACKET_FLOOD;  // spec 29.2
extern const MgLogic MG_FIREWALL;      // spec 29.3
extern const MgLogic MG_BUFFER;        // spec 29.4
extern const MgLogic MG_DELETE;        // spec 29.5

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
uint32_t seq_deadline_ms(const MgCtx& c);           // scripted tests only

// --- PACKET FLOOD (29.2) -----------------------------------------------------
// pf_last(): which of these happened to the packet that resolved most recently.
#define PF_LAST_NONE   0u
#define PF_LAST_GOOD   1u
#define PF_LAST_BAD    2u
#define PF_LAST_LOST   3u
// pf_want_side() when the lane is empty. Not a side.
#define PF_NO_HEAD     0xFFu

uint8_t  pf_packets(void);
uint16_t pf_life_ms(void);
uint8_t  pf_head(const MgCtx& c);                    // == MgCtx.round
uint8_t  pf_queue_len(const MgCtx& c);
uint8_t  pf_digit_at(const MgCtx& c, uint8_t slot);  // slot 0 = the head
uint8_t  pf_mouth_digit(const MgCtx& c, uint8_t side);
uint16_t pf_life_left_ms(const MgCtx& c);
uint8_t  pf_last(const MgCtx& c);
uint8_t  pf_last_side(const MgCtx& c);
bool     pf_swap_flash(const MgCtx& c);
// The correct mouth for the CURRENT head, or PF_NO_HEAD. It reads exactly what
// press() reads, so a hook and the rule it mirrors cannot drift apart.
uint8_t  pf_want_side(const MgCtx& c);
uint8_t  pf_good(const MgCtx& c);                    // scripted tests only
uint8_t  pf_bad(const MgCtx& c);                     // scripted tests only
uint8_t  pf_lost(const MgCtx& c);                    // scripted tests only

// --- FIREWALL (29.3) ---------------------------------------------------------
uint8_t  fw_lanes(void);
uint8_t  fw_packets(void);
uint8_t  fw_shield(const MgCtx& c);
uint8_t  fw_lane(const MgCtx& c);
uint8_t  fw_outcome(const MgCtx& c);                 // 0 flight, 1 blocked, 2 leaked
uint16_t fw_fall_pos(const MgCtx& c);                // 0..256, 256 in the gap
uint8_t  fw_pip(const MgCtx& c, uint8_t i);          // 0 unplayed, 1 blocked, 2 leaked
uint8_t  fw_blocked(const MgCtx& c);                 // the score's only input

// --- BUFFER (29.4) -----------------------------------------------------------
uint8_t  buf_track_px(void);
uint16_t buf_scored_steps(void);
uint16_t buf_inside_steps(const MgCtx& c);           // the score's only input
bool     buf_is_live(const MgCtx& c);                // the lead-in is over
uint8_t  buf_cursor_px(const MgCtx& c);
uint8_t  buf_zone_lo_px(const MgCtx& c);
uint8_t  buf_zone_hi_px(const MgCtx& c);
bool     buf_is_inside(const MgCtx& c);
// Q4 internals, for the scripted tapes only - nothing on the device calls them.
int16_t  buf_cursor_q4(const MgCtx& c);
int16_t  buf_vel_q4(const MgCtx& c);
int16_t  buf_zone_q4(const MgCtx& c);
int16_t  buf_zone_vel_q4(const MgCtx& c);
int16_t  buf_kick_q4(void);

// --- DELETE (29.5) -----------------------------------------------------------
#define DEL_KIND_EMPTY    0u
#define DEL_KIND_HEALTHY  1u
#define DEL_KIND_CORRUPT  2u

uint8_t  del_slots(void);
uint8_t  del_charges_max(void);
uint8_t  del_bad_total(void);
uint8_t  del_cursor(const MgCtx& c);
uint8_t  del_charges(const MgCtx& c);
uint8_t  del_cleared(const MgCtx& c);                // the score's only input
uint8_t  del_kind_at(const MgCtx& c, uint8_t slot);
uint8_t  del_life_pct(const MgCtx& c, uint8_t slot);
uint16_t del_life_left_ms(const MgCtx& c, uint8_t slot);   // scripted tests only

#endif  // PB_MG_GAMES_H
