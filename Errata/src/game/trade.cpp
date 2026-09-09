// =============================================================================
//  ERRATA - game/trade.cpp
//  See trade.h for the journal, the write order, the idempotence hinge and the
//  two things this module proves against the two things it only bounds.
//
//  THE ONE FILE-SCOPE MUTABLE IS s_last_reject, a diagnostic. Nothing branches
//  on it, and the two entry points write it on every path so a caller can never
//  read a stale one.
// =============================================================================
#include "trade.h"

#include <string.h>

#include "box.h"
#include "taint.h"

// -----------------------------------------------------------------------------
//  NAMES
// -----------------------------------------------------------------------------
static const char* const TDR_NAMES[] = {
  "TDR_OK", "TDR_NO_BOX", "TDR_NO_SLOT", "TDR_ACTIVE", "TDR_QUARANTINED",
  "TDR_INVALID", "TDR_SELF", "TDR_PEER_INVALID", "TDR_TAINT",
  "TDR_DUPLICATE_ID", "TDR_PHASE", "TDR_STORE", "TDR_LOST"
};
static_assert(sizeof(TDR_NAMES) / sizeof(TDR_NAMES[0]) == (size_t)TDR_REJECT_COUNT,
              "a TradeReject was added without its name");

const char* trade_reject_name(TradeReject r)
{
  if ((uint8_t)r >= (uint8_t)TDR_REJECT_COUNT) return "TDR_?";
  return TDR_NAMES[(uint8_t)r];
}

static const char* const TRS_NAMES[] = {
  "TRS_NONE", "TRS_ROLLED_BACK", "TRS_COMPLETED", "TRS_FAILED", "TRS_LOST"
};
static_assert(sizeof(TRS_NAMES) / sizeof(TRS_NAMES[0]) == (size_t)TRS_COUNT,
              "a TradeResolution was added without its name");

const char* trade_resolution_name(TradeResolution r)
{
  if ((uint8_t)r >= (uint8_t)TRS_COUNT) return "TRS_?";
  return TRS_NAMES[(uint8_t)r];
}

static TradeReject s_last_reject = TDR_OK;
TradeReject trade_last_reject(void) { return s_last_reject; }

static TradeReject fail(TradeReject r) { s_last_reject = r; return r; }

// -----------------------------------------------------------------------------
//  1. MAY I OFFER THIS ONE?
// -----------------------------------------------------------------------------
TradeReject trade_offer_check(uint8_t slot, uint16_t quarantine_mask)
{
  if (!box_bound()) return fail(TDR_NO_BOX);
  if (slot >= (uint8_t)BOX_SLOTS) return fail(TDR_NO_SLOT);
  const BugInstance* p = box_peek(slot);
  if (p == nullptr) return fail(TDR_NO_SLOT);
  if (slot == box_active()) return fail(TDR_ACTIVE);
  if ((quarantine_mask & (uint16_t)(1u << slot)) != 0u) return fail(TDR_QUARANTINED);
  // THE LAST BUG ON THE DEVICE IS ALREADY PROTECTED, by the line above and
  // by game/box.h invariant B3 - see the TradeReject enum. A `box_count() <= 1`
  // check here was written and then deleted: it could not fire.
  if (validate_bug(*p) != VR_OK) return fail(TDR_INVALID);
  return fail(TDR_OK);
}

// -----------------------------------------------------------------------------
//  2. MAY I ACCEPT THAT ONE?
// -----------------------------------------------------------------------------
TradeReject trade_accept_check(const BugInstance& outgoing,
                               const BugInstance& incoming,
                               uint32_t local_device_id, uint32_t peer_device_id)
{
  if (!box_bound()) return fail(TDR_NO_BOX);
  // A device that hears its own beacon back is not a peer. networking/session.h
  // already refuses this at SD_SELF; it is repeated here because game/trade.cpp
  // is also the boot path's rule holder and a journal names a peer_id.
  if (peer_device_id == 0u || peer_device_id == local_device_id)
    return fail(TDR_SELF);
  if (incoming.id == 0u) return fail(TDR_PEER_INVALID);
  if (validate_bug(incoming) != VR_OK) return fail(TDR_PEER_INVALID);
  // THE GOD-TAINT GATE (game/taint.h). Evaluated on OUR outgoing Bug against
  // THEIR incoming one, on both devices independently, so the refusal is
  // symmetric with no negotiation and no extra wire field.
  if (!taint_gate_ok(outgoing, incoming)) return fail(TDR_TAINT);
  // A peer that sends a Bug whose id we already hold would break invariant
  // B2 if box_add() took it as given. It does not - box_add() re-mints a
  // colliding id - but a silent re-mint on the wire path is a peer's lie being
  // repaired instead of refused, which is exactly what spec section 15's first
  // sentence is about. THE COMMIT PATH MINTS A FRESH ID ANYWAY; this refuses
  // the lie before it is journalled.
  if (box_id_in_use(incoming.id)) return fail(TDR_DUPLICATE_ID);
  return fail(TDR_OK);
}

// -----------------------------------------------------------------------------
//  3. THE JOURNAL
// -----------------------------------------------------------------------------
void trade_journal_idle(PendingTrade& t)
{
  memset(&t, 0, sizeof t);
  t.magic   = (uint16_t)TR_MAGIC;
  t.version = (uint8_t)SAVE_SCHEMA_VERSION;
  t.phase   = (uint8_t)TRADE_IDLE;
}

void trade_journal_sent(PendingTrade& t, uint32_t out_id, uint32_t peer_id)
{
  trade_journal_idle(t);
  t.phase   = (uint8_t)TRADE_SENT;
  t.out_id  = out_id;
  t.peer_id = peer_id;
}

void trade_journal_received(PendingTrade& t, const uint8_t in_wire[TR_WIRE_BYTES])
{
  t.phase = (uint8_t)TRADE_RECEIVED;
  memcpy(t.in_wire, in_wire, (size_t)TR_WIRE_BYTES);
}

bool trade_journal_live(const PendingTrade& t)
{
  if (t.magic != (uint16_t)TR_MAGIC) return false;
  if (t.version != (uint8_t)SAVE_SCHEMA_VERSION) return false;
  return t.phase != (uint8_t)TRADE_IDLE && t.phase <= (uint8_t)TRADE_COMMIT;
}

// -----------------------------------------------------------------------------
//  THE SHARED APPLY. Two INDEPENDENT PRESENCE TESTS, each a no-op when already
//  done, which is what makes a replayed COMMIT free. trade_execute() and
//  trade_resolve() both end here, so there is exactly one implementation of
//  "what a committed trade does to the Box".
//
//  THE COMMIT RECORD IS ALREADY ON FLASH WHEN THIS RUNS. Every early return
//  below therefore leaves a state the NEXT boot can finish, never one it cannot
//  name.
// -----------------------------------------------------------------------------
static TradeReject apply_committed(PendingTrade& t, const TradeCodec& codec,
                                   const TradeStore& store)
{
  BugInstance in;
  memset(&in, 0, sizeof in);
  const bool decoded = (codec.decode != nullptr) &&
                       (codec.decode(t.in_wire, in) == VR_OK);

  // --- B1: the outgoing Bug leaves ---------------------------------------
  // A PRESENCE TEST, not a step: on a replay the slot is already gone.
  uint8_t out_slot = (uint8_t)BOX_SLOT_NONE;
  for (uint8_t s = 0; s < (uint8_t)BOX_SLOTS; ++s) {
    const BugInstance* p = box_peek(s);
    if (p != nullptr && p->id == t.out_id) { out_slot = s; break; }
  }

  if (!decoded) {
    // The record will not decode. If the outgoing Bug is still here, nothing
    // has been destroyed and the honest answer is to undo; if it is already
    // gone, one Bug has been lost and no code in this file can bring it back.
    // Unreachable while this module writes the record and the 64 B CRC holds -
    // named because the resolver's input is a PERSISTED blob.
    const TradeReject why = (out_slot == (uint8_t)BOX_SLOT_NONE) ? TDR_LOST
                                                                 : TDR_PEER_INVALID;
    trade_journal_idle(t);
    if (store.write_journal != nullptr) (void)store.write_journal(store.ctx, t);
    return fail(why);
  }

  if (out_slot != (uint8_t)BOX_SLOT_NONE) {
    // box_release() refuses the ACTIVE slot by invariant B4, so the trade path
    // owns moving it first. trade_offer_check() refuses to offer the active
    // Bug at all, so this is DEFENSIVE - the input here is a persisted blob
    // and a blob is not a promise. The incoming Bug becomes active below.
    if (box_active() == out_slot) {
      for (uint8_t s = 0; s < (uint8_t)BOX_SLOTS; ++s) {
        if (s != out_slot && box_occupied(s)) { (void)box_set_active(s); break; }
      }
      if (box_active() == out_slot) return fail(TDR_ACTIVE);   // it was the only one
    }
    if (!box_release(out_slot, true)) return fail(TDR_ACTIVE);
    if (store.write_slot != nullptr && !store.write_slot(store.ctx, out_slot))
      return fail(TDR_STORE);
  }

  // --- B2: the incoming Bug arrives --------------------------------------
  // The second presence test. The id in the record is the one WE minted before
  // W3 was written, so "already here" is answerable without guessing.
  if (!box_id_in_use(in.id)) {
    in.flags  = (uint8_t)(in.flags | (uint8_t)PBF_TRADED);
    in.origin = (uint8_t)ORIGIN_TRADED;
    if (in.trades < 0xFFu) in.trades++;
    const uint8_t slot = box_add(in);
    // A FULL BOX WITH THE OUTGOING BUG ALREADY GONE. B1 frees a slot on
    // every path that reaches here, so this is the resolver's defensive answer
    // to a PERSISTED blob rather than a state this module can produce - and
    // TDR_NO_SLOT is the honest word for it. It is deliberately NOT TDR_STORE:
    // nothing failed to write.
    if (slot == (uint8_t)BOX_SLOT_NONE) return fail(TDR_NO_SLOT);
    // box_add() takes the id as given when it is unused, which is the whole
    // point of minting it before W3. If it ever re-minted, the record would
    // stop naming what is in the Box and the next replay would add a second
    // copy - so this is asserted at runtime rather than trusted.
    const BugInstance* filed = box_peek(slot);
    if (filed == nullptr || filed->id != in.id) return fail(TDR_DUPLICATE_ID);
    if (store.write_slot != nullptr && !store.write_slot(store.ctx, slot))
      return fail(TDR_STORE);
  }

  // --- B3: the header -------------------------------------------------------
  // Invariant B3: exactly one slot is active whenever the Box is non-empty. It
  // can only have been broken by the defensive branch above.
  if (box_count() > 0u && box_active() >= (uint8_t)BOX_SLOTS) {
    for (uint8_t s = 0; s < (uint8_t)BOX_SLOTS; ++s) {
      if (box_occupied(s)) { (void)box_set_active(s); break; }
    }
  }
  if (store.write_box != nullptr && !store.write_box(store.ctx))
    return fail(TDR_STORE);

  // --- W4 -------------------------------------------------------------------
  trade_journal_idle(t);
  if (store.write_journal != nullptr && !store.write_journal(store.ctx, t))
    return fail(TDR_STORE);
  if (store.checkpoint != nullptr) store.checkpoint(store.ctx);
  return fail(TDR_OK);
}

// -----------------------------------------------------------------------------
//  4a. THE LIVE PATH
// -----------------------------------------------------------------------------
TradeReject trade_execute(PendingTrade& t, const TradeCodec& codec,
                          const TradeStore& store, uint32_t now_epoch)
{
  (void)now_epoch;
  if (!box_bound()) return fail(TDR_NO_BOX);
  if (t.magic != (uint16_t)TR_MAGIC || t.phase != (uint8_t)TRADE_RECEIVED)
    return fail(TDR_PHASE);
  if (codec.decode == nullptr || codec.set_id == nullptr) return fail(TDR_PEER_INVALID);

  // The record must still decode BEFORE anything is minted or written: this is
  // the last point at which refusing costs nothing.
  BugInstance probe;
  if (codec.decode(t.in_wire, probe) != VR_OK) return fail(TDR_PEER_INVALID);

  uint8_t out_slot = (uint8_t)BOX_SLOT_NONE;
  for (uint8_t s = 0; s < (uint8_t)BOX_SLOTS; ++s) {
    const BugInstance* p = box_peek(s);
    if (p != nullptr && p->id == t.out_id) { out_slot = s; break; }
  }
  if (out_slot == (uint8_t)BOX_SLOT_NONE) return fail(TDR_NO_SLOT);
  if (out_slot == box_active()) return fail(TDR_ACTIVE);

  // THE IDEMPOTENCE HINGE (trade.h). Mint FIRST, patch the record, reseal it,
  // and only then write the COMMIT phase - so the journal names the id the
  // incoming Bug will have here and a replay is two presence tests.
  const uint32_t fresh = box_mint_id();
  if (fresh == 0u) return fail(TDR_NO_BOX);
  codec.set_id(t.in_wire, fresh);
  BugInstance patched;
  if (codec.decode(t.in_wire, patched) != VR_OK) return fail(TDR_PEER_INVALID);
  if (patched.id != fresh) return fail(TDR_PEER_INVALID);   // the reseal failed

  // W3. NO BOX BYTE HAS MOVED YET. A cut here leaves a COMMIT record over an
  // untouched Box, which the resolver finishes; a FAILED put leaves the older
  // record, which the resolver rolls back.
  t.phase = (uint8_t)TRADE_COMMIT;
  if (store.write_journal == nullptr || !store.write_journal(store.ctx, t))
    return fail(TDR_STORE);

  return apply_committed(t, codec, store);
}

// -----------------------------------------------------------------------------
//  4b. THE BOOT PATH
// -----------------------------------------------------------------------------
TradeResolution trade_resolve(PendingTrade& t, const TradeCodec& codec,
                              const TradeStore& store, uint32_t now_epoch)
{
  (void)now_epoch;
  s_last_reject = TDR_OK;
  if (!box_bound()) { s_last_reject = TDR_NO_BOX; return TRS_FAILED; }
  if (!trade_journal_live(t)) return TRS_NONE;

  if (t.phase != (uint8_t)TRADE_COMMIT) {
    // SENT or RECEIVED: no Box byte was ever written, so rolling back IS
    // clearing the journal. Both sides keep their originals (spec section 16).
    trade_journal_idle(t);
    if (store.write_journal != nullptr && !store.write_journal(store.ctx, t)) {
      s_last_reject = TDR_STORE;
      return TRS_FAILED;                 // the record survives; try again next boot
    }
    return TRS_ROLLED_BACK;
  }

  const TradeReject r = apply_committed(t, codec, store);
  if (r == TDR_OK)           return TRS_COMPLETED;
  if (r == TDR_PEER_INVALID) return TRS_ROLLED_BACK;   // ours was still here
  if (r == TDR_LOST)         return TRS_LOST;          // and it was not
  return TRS_FAILED;
}
