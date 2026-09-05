// =============================================================================
//  PEBBLEBOL - networking/creator_gate.cpp
//  The creator PIN gate and the portal idle timer. See creator_gate.h for the
//  clock argument, the threat model and what this is deliberately not.
//
//  PURE: <stdint.h> and the constants, nothing else. No file-scope state - the
//  gate is the caller's struct - so tools/check.sh's two networking purity
//  gates both apply to this file.
// =============================================================================
#include "creator_gate.h"

// -----------------------------------------------------------------------------
//  MINT
// -----------------------------------------------------------------------------
uint16_t cg_mint_pin(uint32_t entropy)
{
  // 1 .. WEB_PIN_MAX-1. The modulus is WEB_PIN_MAX-1 and the +1 shifts the
  // range off zero, so the sentinel can never be minted for ANY entropy value,
  // including the 0 and 0xFFFFFFFF a broken source would hand over.
  return (uint16_t)(1u + (entropy % (uint32_t)(WEB_PIN_MAX - 1)));
}

// -----------------------------------------------------------------------------
//  IDLE BUDGET
// -----------------------------------------------------------------------------
uint16_t cg_idle_seconds(uint16_t cfg_idle_s)
{
  if (cfg_idle_s == 0u) return (uint16_t)CREATOR_IDLE_S_DEFAULT;
  if (cfg_idle_s > (uint16_t)CREATOR_IDLE_S_MAX) return (uint16_t)CREATOR_IDLE_S_MAX;
  return cfg_idle_s;
}

// -----------------------------------------------------------------------------
//  LIFECYCLE
// -----------------------------------------------------------------------------
void cg_open(CreatorGate& g, uint16_t pin, uint8_t persisted_fails,
             uint16_t cfg_idle_s, uint32_t now_ms)
{
  g.last_seen_ms  = now_ms;
  g.lock_until_ms = now_ms;
  g.pin           = pin;
  g.idle_s        = cg_idle_seconds(cfg_idle_s);
  g.fail_count    = (persisted_fails > (uint8_t)CREATOR_PIN_FAIL_MAX)
                      ? (uint8_t)CREATOR_PIN_FAIL_MAX : persisted_fails;
  // NOT RESTORED, AND THE HEADER ARGUES IT AT LENGTH: there is no clock that
  // survives the event which lost this deadline. A restored fail_count at the
  // maximum leaves the gate ARMED instead, so the next failure locks at once.
  g.lock_armed    = 0u;
  g.open          = 1u;
  g.reserved      = 0u;
}

void cg_close(CreatorGate& g)
{
  g.open = 0u;
}

// -----------------------------------------------------------------------------
//  PARSE
// -----------------------------------------------------------------------------
bool cg_parse_pin(const char* s, uint16_t& out)
{
  if (s == nullptr) return false;
  uint16_t v = 0u;
  uint8_t  i = 0u;
  for (; i < (uint8_t)CREATOR_PIN_DIGITS; ++i) {
    const char c = s[i];
    if (c < '0' || c > '9') return false;      // also catches the NUL: too short
    v = (uint16_t)(v * 10u + (uint16_t)(c - '0'));
  }
  if (s[i] != '\0') return false;              // too long
  out = v;                                     // 0..9999, cannot overflow
  return true;
}

// -----------------------------------------------------------------------------
//  LOCKOUT
// -----------------------------------------------------------------------------
uint32_t cg_lock_left_ms(const CreatorGate& g, uint32_t now_ms)
{
  if (!g.lock_armed) return 0u;
  // Unsigned difference: correct across the gt_mono32() wrap for any gap under
  // half the range. A deadline already in the past yields a value far larger
  // than one lock window, which is the same answer as "expired".
  const uint32_t left = (uint32_t)(g.lock_until_ms - now_ms);
  if (left == 0u || left > (uint32_t)CREATOR_PIN_LOCK_MS) return 0u;
  return left;
}

// -----------------------------------------------------------------------------
//  VERIFY
// -----------------------------------------------------------------------------
CgVerdict cg_verify(CreatorGate& g, const char* supplied, uint32_t now_ms)
{
  if (!g.open)      return CG_NO_PIN;      // no portal, nothing to authorise
  if (g.pin == 0u)  return CG_NO_PIN;      // none issued: nothing can match

  if (g.lock_armed) {
    if (cg_lock_left_ms(g, now_ms) > 0u) {
      // The attempt is NOT counted. Counting it would let a client extend its
      // own lockout forever, and would punish the owner for waiting.
      return CG_LOCKED;
    }
    // Expired. Exactly one attempt is allowed: fail_count is left at the
    // maximum, so the branch below re-locks on any failure.
    g.lock_armed = 0u;
  }

  uint16_t v = 0u;
  const bool parsed = cg_parse_pin(supplied, v);
  if (!parsed || v != g.pin) {
    if (g.fail_count < (uint8_t)CREATOR_PIN_FAIL_MAX) ++g.fail_count;
    if (g.fail_count >= (uint8_t)CREATOR_PIN_FAIL_MAX) {
      g.lock_armed    = 1u;
      g.lock_until_ms = (uint32_t)(now_ms + (uint32_t)CREATOR_PIN_LOCK_MS);
    }
    return CG_BAD_PIN;
  }

  g.fail_count   = 0u;
  g.lock_armed   = 0u;
  // THE ONLY WRITE OF last_seen_ms IN THE MODULE. See the header: an
  // unauthenticated client must not be able to hold the access point up.
  g.last_seen_ms = now_ms;
  return CG_OK;
}

uint8_t cg_persist_fails(const CreatorGate& g)
{
  return (g.fail_count >= (uint8_t)CREATOR_PIN_FAIL_MAX)
           ? (uint8_t)CREATOR_PIN_FAIL_MAX : (uint8_t)0;
}

// -----------------------------------------------------------------------------
//  IDLE
// -----------------------------------------------------------------------------
bool cg_idle_expired(const CreatorGate& g, uint32_t now_ms)
{
  if (!g.open) return false;
  const uint32_t budget_ms = (uint32_t)g.idle_s * 1000u;
  return (uint32_t)(now_ms - g.last_seen_ms) >= budget_ms;
}
