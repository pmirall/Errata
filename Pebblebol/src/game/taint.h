// =============================================================================
//  PEBBLEBOL - game/taint.h
//  THE GOD-TAINT GATE, BACK IN THE GAME LAYER (plan P7-C5, spec sections 16/17).
//
//  P2-C7b deleted peer_block_reason() along with the rest of the BLE mating
//  protocol, and the rule went with it: a god-tainted unit must not pollute a
//  real dynasty. The MARKERS survived in the transport - ble_social.cpp built
//  BLE_BF_DEBUG into the beacon and stored it into BlePeerInfo.peer_flags - and
//  nothing in the tree read either of them for a gameplay decision until this
//  header. Correct for a transport-only module; wrong as a permanent state,
//  because a taint that nothing enforces is a taint that spreads.
//
//  AND THE TRANSPORT THAT CARRIED THEM IS GONE (P8-C0 deleted BLE; the file is
//  at `git show ed9b099:Pebblebol/src/networking/ble_social.cpp`). THAT CHANGES
//  NOTHING HERE, WHICH IS THE POINT OF THE PARAGRAPH ABOVE: the two markers
//  this header reads are the GENOME BIT and the PebbleInstance FLAG, both of
//  which live on flash and on the 48 B wire record. ESP-NOW's DiscPeer carries
//  no taint marker at all - it carries DISC_CAP_* and nothing else - so a peer's
//  beacon never was and still is not where this rule gets its answer.
//
// -----------------------------------------------------------------------------
//  WHERE IT IS NOT
// -----------------------------------------------------------------------------
//   * NOT in validate_pebble(). game/validate.h says twice that there is no
//     policy parameter, and PBF_GOD_TAINTED is INSIDE VLD_FLAGS_MASK on
//     purpose: a tainted Pebble is a perfectly legal OBJECT. A rule here would
//     quarantine every tainted Pebble on the save path, i.e. brick the device
//     of the god-mode tester who made it.
//   * NOT in the transport. That is exactly what P2-C7b removed, and the reason
//     it removed it is still right: a radio module has no business holding a
//     game rule.
//   * NOT a new VReject. A policy refusal is not a validity verdict, and a
//     28th VReject would put a policy flag inside the one validator by the back
//     door. The refusal belongs to the trade and breeding FSMs' own reason
//     enums (TradeReject, BreedReject), which is where it is.
//
// -----------------------------------------------------------------------------
//  TWO MARKERS, AND READING ONE OF THEM IS A HOLE WITH A NAME
// -----------------------------------------------------------------------------
//   1. Genome g2 bit GN_TAINT_SH. Set by dev/godmode.cpp's god_enter() through
//      sim_god_set_genome(), propagated by genome_breed() as A | B and NEVER
//      cleared (game/genome.cpp), carried by the 16 B genome onto flash and
//      onto the 48 B wire record at PBW_OFF_GENOME. This is the DURABLE truth.
//   2. PebbleInstance.flags bit PBF_GOD_TAINTED. Set by game/sim.cpp as a
//      mirror of the genome bit when a Pebble is bound, and INDEPENDENTLY by
//      persistence/migration.cpp from the v1 LV1_PF_GOD_TAINTED - a v1 save can
//      therefore carry the flag with the genome bit clear.
//
//  They are set by different writers on different paths, so pb_is_tainted()
//  reads BOTH. Reading only the genome misses every migrated v1 unit; reading
//  only the flag misses a Pebble god mode touched before sim bound it.
//
// -----------------------------------------------------------------------------
//  THE RULE, AND WHAT IT IS NOT
// -----------------------------------------------------------------------------
//  A CLEAN UNIT REFUSES A TAINTED ONE; A TAINTED UNIT ACCEPTS ANYTHING. Both
//  sides evaluate it on their OWN incoming record, so the refusal is symmetric
//  with no negotiation and no extra wire field, and two testers keep a
//  playground with each other.
//
//  IT IS HONOUR-BASED AND THIS HEADER SAYS SO RATHER THAN IMPLYING OTHERWISE. A
//  peer running modified firmware clears the bit and this gate cannot tell -
//  the same sentence game/validate.h writes about a forged genome resealed with
//  a correct CRC. It protects an honest player's dynasty from an honest
//  tester's device. It is not a security control.
//
//  PURE, HEADER-ONLY. stdint, the save schema and game/genome.h. No .cpp,
//  because both functions are one expression and a translation unit for them
//  would be a file with nothing in it.
// =============================================================================
#ifndef PB_GAME_TAINT_H
#define PB_GAME_TAINT_H

#include <stdint.h>

#include "../persistence/save_schema.h"   // PebbleInstance, PBF_GOD_TAINTED
#include "genome.h"                       // gene_tainted()

// True when EITHER marker is set. See the banner for why both are read.
inline bool pb_is_tainted(const PebbleInstance& p)
{
  return gene_tainted(p.genome) != 0u ||
         (uint8_t)(p.flags & (uint8_t)PBF_GOD_TAINTED) != 0u;
}

// The gate itself, as ONE expression with ONE meaning, shared verbatim by
// game/trade.cpp and game/breeding.cpp. `local` is a Pebble this device owns;
// `incoming` is the one it is being asked to take in (a traded record, or the
// other parent of a breeding). True means the exchange may proceed.
inline bool taint_gate_ok(const PebbleInstance& local, const PebbleInstance& incoming)
{
  return !pb_is_tainted(incoming) || pb_is_tainted(local);
}

#endif  // PB_GAME_TAINT_H
