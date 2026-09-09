// =============================================================================
//  ERRATA - networking/net_classify.cpp
//  The classifier and the two identity-destroying helpers. See net_classify.h
//  for the ladder, the privacy rule and why this file is pure.
// =============================================================================
#include "net_classify.h"

// -----------------------------------------------------------------------------
// FNV-1a 32. The same function tools/gen_content.py runs over each token to
// build NET_TOKEN_TABLE, so the two must stay byte-identical; the host test
// pins one literal from each side against the other.
// -----------------------------------------------------------------------------
uint32_t net_fnv1a32(const void *data, size_t len) {
  const uint8_t *p = (const uint8_t *)data;
  uint32_t h = 0x811C9DC5u;
  for (size_t i = 0; i < len; ++i) {
    h ^= (uint32_t)p[i];
    h *= 0x01000193u;
  }
  return h;
}

int8_t net_rssi_clamp(int32_t rssi) {
  if (rssi > 0)    return 0;
  if (rssi < -127) return -127;
  return (int8_t)rssi;
}

uint32_t net_scan_salt(uint32_t device_id) {
  // "pbl-scan" - the pre-rename salt, and it STAYS. It seeds every network
  // hash, so changing it would re-roll every cooldown the player has walked
  // for and change which creature each street answers with. A rename must not
  // reset somebody's map.
  // "pbl-scan" then the device id, little-endian. Written as one buffer so the
  // byte order is visible rather than inherited from the host's endianness.
  uint8_t buf[12] = { 'p', 'b', 'l', '-', 's', 'c', 'a', 'n', 0, 0, 0, 0 };
  buf[8]  = (uint8_t)(device_id & 0xFFu);
  buf[9]  = (uint8_t)((device_id >> 8) & 0xFFu);
  buf[10] = (uint8_t)((device_id >> 16) & 0xFFu);
  buf[11] = (uint8_t)((device_id >> 24) & 0xFFu);
  return net_fnv1a32(buf, sizeof(buf));
}

uint32_t net_hash_from_bssid(const uint8_t bssid[6], uint32_t salt) {
  if (!bssid) {
    return NET_HASH_NEVER_ZERO;
  }
  uint8_t buf[10];
  for (uint8_t i = 0; i < 6; ++i) {
    buf[i] = bssid[i];
  }
  buf[6] = (uint8_t)(salt & 0xFFu);
  buf[7] = (uint8_t)((salt >> 8) & 0xFFu);
  buf[8] = (uint8_t)((salt >> 16) & 0xFFu);
  buf[9] = (uint8_t)((salt >> 24) & 0xFFu);
  const uint32_t h = net_fnv1a32(buf, sizeof(buf));
  return h ? h : NET_HASH_NEVER_ZERO;
}

// -----------------------------------------------------------------------------
// Token extraction. NOTHING LEAVES THIS FUNCTION BUT A BITMASK.
// -----------------------------------------------------------------------------
// NET_TOKEN_MAX_LEN is emitted by the generator from the table's own longest
// token, so adding a longer one to networks.json widens this buffer rather than
// making that token silently unmatchable.

static uint8_t class_of_hash(uint32_t h) {
  // NET_TOKEN_TABLE is strictly ascending by hash - net_token_table_is_sorted_
  // and_unique() in the generated header is what makes this search legal.
  uint8_t lo = 0;
  uint8_t hi = NET_TOKEN_ROW_COUNT;          // exclusive
  while (lo < hi) {
    const uint8_t mid = (uint8_t)(lo + (uint8_t)((hi - lo) / 2u));
    const uint32_t v  = NET_TOKEN_TABLE[mid].hash;
    if (v == h)      return NET_TOKEN_TABLE[mid].klass;
    if (v < h)       lo = (uint8_t)(mid + 1u);
    else             hi = mid;
  }
  return 0;
}

static inline bool is_ascii_letter(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

uint8_t net_tokens_of(const char *name, size_t len) {
  if (!name || len == 0) {
    return 0;
  }
  uint8_t  mask = 0;
  char     tok[NET_TOKEN_MAX_LEN];
  uint8_t  n    = 0;         // letters buffered
  bool     over = false;     // this run is longer than any token in the table

  // One pass, and one extra iteration so a run that ends at `len` is flushed by
  // the same code that flushes a run ending at a delimiter. Without it the last
  // token of a name with no trailing delimiter - which is most names - was
  // never looked up.
  for (size_t i = 0; i <= len; ++i) {
    const char c = (i < len) ? name[i] : '\0';
    if (c != '\0' && is_ascii_letter(c)) {
      if (n < NET_TOKEN_MAX_LEN) {
        tok[n++] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
      } else {
        over = true;         // cannot match anything; keep walking to the end
      }
      continue;
    }
    if (n >= (uint8_t)NET_TOKEN_MIN_LEN && !over) {
      mask |= class_of_hash(net_fnv1a32(tok, n));
    }
    n    = 0;
    over = false;
    if (c == '\0' && i < len) {
      break;                 // an embedded NUL ends the name
    }
  }

  // The name is gone by construction - `tok` is a local and `mask` is three
  // bits - but zero the buffer anyway so nothing readable is left on the stack
  // for whatever runs next in this task.
  for (uint8_t i = 0; i < NET_TOKEN_MAX_LEN; ++i) {
    tok[i] = 0;
  }
  return (uint8_t)(mask & (uint8_t)NTOK_ALL);
}

// -----------------------------------------------------------------------------
// The ladder. First match wins; see net_classify.h for why each rung is where
// it is.
// -----------------------------------------------------------------------------
uint8_t net_classify(const NetFacts& f) {
  // 1. A beacon with no name at all. Before auth, because a hidden network
  //    still reports an auth mode and rule 3 or 4 would otherwise take it.
  if (f.hidden) {
    return (uint8_t)NET_CAT_HIDDEN;
  }

  switch (f.auth) {
    // 2. EAP. Essentially never residential.
    case NAUTH_ENTERPRISE:
      return (uint8_t)NET_CAT_BUSINESS;

    // 3. Nothing to join with. A carrier hotspot or a venue is PUBLIC; an
    //    unlocked router is OPEN. Both stay reachable, and neither depends on
    //    the token lists alone.
    case NAUTH_OPEN:
    case NAUTH_OWE:
      if (f.tokens & (uint8_t)(NTOK_VENUE | NTOK_OPERATOR)) {
        return (uint8_t)NET_CAT_PUBLIC;
      }
      return (uint8_t)NET_CAT_OPEN;

    // 4. Locked. The name decides when it can; otherwise distance does.
    case NAUTH_WEP:
    case NAUTH_PSK:
      if (f.tokens & (uint8_t)NTOK_VENUE) {
        return (uint8_t)NET_CAT_PUBLIC;          // a hotel's locked network
      }
      if ((f.tokens & (uint8_t)NTOK_ISP_CPE) && f.rssi >= NET_RSSI_MID) {
        return (uint8_t)NET_CAT_HOME;            // a residential router in range
      }
      if (f.rssi >= NET_RSSI_NEAR) {
        return (uint8_t)NET_CAT_HOME;            // you are standing inside it
      }
      if (f.rssi >= NET_RSSI_MID) {
        return (uint8_t)NET_CAT_BUSINESS;        // you are next to it
      }
      return (uint8_t)NET_CAT_UNKNOWN;           // too far to claim anything

    // 5. THE SINK. NAUTH_OTHER and every byte outside the enum land here.
    default:
      return (uint8_t)NET_CAT_UNKNOWN;
  }
}
