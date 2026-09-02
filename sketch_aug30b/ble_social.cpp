// =============================================================================
//  NOTTAMAGOCHI - ble_social.cpp
//  Connectionless BLE social layer: identity beacon, passive scan, 3-frame
//  mating handshake and contagion by proximity.
//
//  ARCHITECTURE NOTES THAT ARE LOAD-BEARING
//  ----------------------------------------
//  * Advertisement-only. No BLEServer, no BLEClient, no GATT, and the
//    advertisement type is ADV_TYPE_NONCONN_IND so nobody can even try to
//    connect to a device that has no services.
//  * onResult() runs on the Bluedroid BTC task. It does exactly three things:
//    filter by company id and length, copy 29 bytes into a lock-free ring,
//    publish the head. No U8g2, no I2C, no Preferences, no Serial, no BLE API.
//    s_q_head is written by that task and by NOTHING else - not even to reset
//    it. reset_transient() empties the ring with tail := head for exactly that
//    reason (PH3 finding 10).
//  * Every BLEScan bug listed in BRIEF 7 #8 is defused here:
//      - getDevice()/getResults() are NEVER called (they deref begin() with no
//        bounds check), so the getCount()>0 guard is moot by construction.
//      - erase() is NEVER called (it derefs find() without an end() compare).
//      - stop() always precedes start(), because start() takes a binary
//        semaphore with portMAX_DELAY and re-entry hangs forever.
//      - setAdvertisedDeviceCallbacks(&cb, true, true): wantDuplicates=true is
//        what stops m_scanResults growing without bound, and it also gives us
//        a fresh RSSI on every packet, which is what proximity needs.
//  * LIFECYCLE SPLIT. BRIEF 4 makes net.cpp "the only module allowed to call
//    BLEDevice::init/deinit", and net.cpp already does exactly that and owns
//    the BLE_SESSION_CAP counter. So ble_begin() does NOT init the stack: it
//    rides on a stack net_request(RADIO_BLE) already brought up, and refuses
//    if it is not there. ble_end() quiesces advertising and scanning and drops
//    the callback; the ~70 KB is genuinely returned by net_request(RADIO_OFF)
//    -> BLEDevice::deinit(false). deinit(true) is forbidden everywhere: it
//    leaves `initialized == true` and a later init() silently no-ops.
//    ble_end() ALWAYS calls BLEScan::stop() first, because the scan-end
//    semaphore lives in the BLEScan object, which deinit() never frees - a
//    deinit with the semaphore still taken would hang the next start().
//  * Integer only. No float touches any game value in this file.
// =============================================================================

#include "ble_social.h"

#if FEATURE_BLE

#include <Arduino.h>
#include <string.h>
#include <esp_random.h>
#include <esp_bt_device.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertising.h>
#include <BLEAdvertisedDevice.h>

#include "strings_es.h"

// -----------------------------------------------------------------------------
// 0. WIRE FORMAT (BRIEF 6.2) - 22 bytes of Manufacturer Specific Data
//    AD budget: Flags(3) + MSD(2 + 22) = 27 of the 31 available bytes.
//    addData() silently DROPS an oversized AD structure, hence the static
//    assert rather than a runtime check.
// -----------------------------------------------------------------------------
NT_PACK_PUSH
struct NT_PACKED BleFrame {
  uint16_t company_id;   // 0..1   BLE_COMPANY_ID (0xFFFF, SIG reserved)
  uint8_t  frame_type;   // 2      BLE_FRAME_BEACON / _MATE_OFFER / _MATE_ACK
  union {
    struct NT_PACKED {
      Genome  genome;    // 3..18
      uint8_t stage;     // 19
      uint8_t cq_hi;     // 20
      uint8_t flags;     // 21   BLE_BF_*
    } beacon;
    struct NT_PACKED {
      Genome  child;     // 3..18
      uint8_t target3[3];// 19..21  low 3 bytes of the courted unit's MAC
    } offer;
    struct NT_PACKED {
      uint16_t child_crc;  // 3..4
      uint8_t  target3[3]; // 5..7  low 3 bytes of the initiator's MAC
      uint8_t  pad[14];    // 8..21
    } ack;
    uint8_t raw[19];
  } u;
};
NT_PACK_POP

static_assert(sizeof(BleFrame) == BLE_MSD_LEN, "BleFrame must be exactly BLE_MSD_LEN bytes");
static_assert(sizeof(BleFrame) == 22, "BRIEF 6.2 fixes the MSD payload at 22 bytes");
static_assert(offsetof(BleFrame, u) == 3, "BleFrame payload must start at byte 3");
static_assert(3 + 2 + BLE_MSD_LEN <= 31, "Flags + MSD must fit ESP_BLE_ADV_DATA_LEN_MAX");

// The GAP device name is set by net.cpp at BLEDevice::init() time. It never
// goes on air from here: our ADV carries no name and the scan response is off.

// -----------------------------------------------------------------------------
// 1. MODULE STATE
// -----------------------------------------------------------------------------

// --- local identity ----------------------------------------------------------
static bool     s_up            = false;
static uint8_t  s_sessions      = 0;      // BLEDevice::init() cycles this boot
static uint8_t  s_self_mac[6]   = {0, 0, 0, 0, 0, 0};
static Genome   s_self_genome;
static bool     s_self_valid    = false;
static uint8_t  s_self_stage    = STAGE_EGG;
static uint8_t  s_self_cq_hi    = 0;
static uint8_t  s_self_energy   = 0;      // 0..100
static uint8_t  s_self_flags    = 0;      // BLE_SELF_*

// --- advertising -------------------------------------------------------------
enum AdvMode : uint8_t { ADV_MODE_NONE = 0, ADV_MODE_BEACON, ADV_MODE_OFFER, ADV_MODE_ACK };
static uint8_t  s_adv_mode = ADV_MODE_NONE;
static BleFrame s_adv_frame;              // exactly what is on air right now

// --- scanning ----------------------------------------------------------------
static bool     s_scan_running    = false;
static bool     s_scan_fast       = false;
static uint32_t s_scan_started_ms = 0;
static volatile bool s_scan_done  = false;   // set on the BTC task

// --- BTC -> loop ring buffer (single producer, single consumer) --------------
struct RawEvt {
  uint8_t mac[6];
  int8_t  rssi;
  uint8_t msd[BLE_MSD_LEN];
};
static RawEvt   s_q[BLE_QUEUE_CAP];
static uint16_t s_q_head = 0;   // written by the BTC task only
static uint16_t s_q_tail = 0;   // written by loop() only
static uint16_t s_q_drop = 0;   // diagnostics only

// --- peer table --------------------------------------------------------------
struct PeerPriv {
  int16_t  rssi_acc;        // RSSI EMA in quarter-dBm (4 * dBm)
  uint16_t hits;            // beacons received, saturating
  uint32_t exposure_ms;     // contagion dwell time
  uint32_t last_infect_ms;  // re-arm anchor
  uint8_t  infect_armed;    // 1 = a roll already happened, re-arm pending
};
static BlePeerInfo s_peers[BLE_PEER_CAP];
static PeerPriv    s_priv[BLE_PEER_CAP];
static uint8_t     s_peer_n = 0;

// --- mating state machine ----------------------------------------------------
static uint8_t  s_mate_state      = BLE_MATE_IDLE;
static uint32_t s_mate_t0         = 0;
static uint8_t  s_mate_peer_mac[6] = {0, 0, 0, 0, 0, 0};
static bool     s_mate_peer_known = false;   // full 6-byte MAC resolved
static uint8_t  s_mate_mac3[3]    = {0, 0, 0};
static Genome   s_mate_child;
static uint16_t s_mate_child_crc  = 0;

static bool     s_cd_active   = false;
static uint32_t s_cd_start_ms = 0;
static uint32_t s_cd_len_ms   = 0;

static BleMateEvent s_result;
static bool     s_result_valid = false;
static uint32_t s_result_ms    = 0;

// --- contagion ---------------------------------------------------------------
static bool     s_contagion = false;

// --- service bookkeeping -----------------------------------------------------
static uint32_t s_last_service_ms = 0;

// -----------------------------------------------------------------------------
// 2. SMALL PURE HELPERS
// -----------------------------------------------------------------------------

// CRC-16/CCITT-FALSE, poly 0x1021, init 0xFFFF. Deliberately a private copy:
// this file must not depend on genome.cpp being linked, and a duplicated
// 10-line CRC is cheaper than a cross-module dependency on the wire path.
static uint16_t nt_crc16(const uint8_t *p, uint8_t n) {
  uint16_t c = GENOME_CRC_INIT;
  for (uint8_t i = 0; i < n; ++i) {
    c ^= (uint16_t)((uint16_t)p[i] << 8);
    for (uint8_t b = 0; b < 8; ++b) {
      c = (c & 0x8000u) ? (uint16_t)(((uint16_t)(c << 1)) ^ GENOME_CRC_POLY) : (uint16_t)(c << 1);
    }
  }
  return c;
}

// A genome arriving from the air is hostile until proven otherwise.
static bool genome_wire_ok(const Genome &g) {
  if ((uint16_t)(g.magic_ver & GENOME_SIG_MASK) != (uint16_t)GENOME_SIG) return false;
  if ((uint16_t)(g.magic_ver & 0x000Fu) != (uint16_t)GENOME_PROTO_VER) return false;
  if (g.lineage_id == 0) return false;               // 0 is reserved for "invalid"
  uint8_t tmp[16];
  memcpy(tmp, &g, sizeof(tmp));                      // packed -> aligned copy
  return nt_crc16(tmp, GENOME_CRC_BYTES) == g.crc16;
}

static inline bool elapsed_since(uint32_t t0, uint32_t span_ms) {
  return (uint32_t)(millis() - t0) >= span_ms;       // rollover safe
}

static void cooldown_arm(uint32_t seconds) {
  s_cd_active   = true;
  s_cd_start_ms = millis();
  // 24 h = 86,400,000 ms, comfortably inside uint32_t (max ~49.7 days).
  s_cd_len_ms   = seconds * 1000UL;
}

static bool cooldown_live(void) {
  if (!s_cd_active) return false;
  if ((uint32_t)(millis() - s_cd_start_ms) >= s_cd_len_ms) {
    s_cd_active = false;
    return false;
  }
  return true;
}

// -----------------------------------------------------------------------------
// 3. PEER TABLE
// -----------------------------------------------------------------------------

static int peer_find(const uint8_t mac[6]) {
  for (uint8_t i = 0; i < s_peer_n; ++i) {
    if (memcmp(s_peers[i].mac, mac, 6) == 0) return (int)i;
  }
  return -1;
}

static int peer_find_mac3(const uint8_t mac3[3]) {
  int best = -1;
  for (uint8_t i = 0; i < s_peer_n; ++i) {
    if (memcmp(s_peers[i].mac + 3, mac3, 3) != 0) continue;
    // Three bytes can collide. Break the tie on signal strength: the loudest
    // match is the one physically in front of the user.
    if (best < 0 || s_peers[i].rssi > s_peers[best].rssi) best = (int)i;
  }
  return best;
}

static void peer_remove(uint8_t idx) {
  if (idx >= s_peer_n) return;
  for (uint8_t i = idx; i + 1 < s_peer_n; ++i) {   // shift down, preserve order
    s_peers[i] = s_peers[i + 1];
    s_priv[i]  = s_priv[i + 1];
  }
  s_peer_n--;
}

// Oldest entry wins the eviction lottery; ties go to the weakest signal.
static uint8_t peer_victim(void) {
  uint8_t worst = 0;
  uint32_t now = millis();
  uint32_t worst_age = 0;
  for (uint8_t i = 0; i < s_peer_n; ++i) {
    uint32_t age = (uint32_t)(now - s_peers[i].last_seen_ms);
    if (age > worst_age || (age == worst_age && s_peers[i].rssi < s_peers[worst].rssi)) {
      worst_age = age;
      worst = i;
    }
  }
  return worst;
}

static bool peer_fresh(uint8_t idx) {
  return (uint32_t)(millis() - s_peers[idx].last_seen_ms) < BLE_PEER_FRESH_MS;
}

// A peer only becomes courtable after several packets from close range: one
// stray reflection off a wall is not "you two are standing next to each other".
static bool peer_solid(uint8_t idx) {
  return s_priv[idx].hits >= BLE_PEER_MIN_HITS
      && s_peers[idx].rssi >= BLE_RSSI_MIN
      && peer_fresh(idx);
}

static void peer_touch(const RawEvt &e, const BleFrame &f) {
  int idx = peer_find(e.mac);
  if (idx < 0) {
    if (s_peer_n < BLE_PEER_CAP) {
      idx = (int)s_peer_n++;
    } else {
      idx = (int)peer_victim();
    }
    memset(&s_peers[idx], 0, sizeof(s_peers[idx]));
    memset(&s_priv[idx], 0, sizeof(s_priv[idx]));
    memcpy(s_peers[idx].mac, e.mac, 6);
    s_priv[idx].rssi_acc = (int16_t)((int16_t)e.rssi * 4);   // seed the EMA
  } else {
    // EMA with alpha = 1/4, held in quarter-dBm so the division never rounds
    // a slow drift away to nothing.
    s_priv[idx].rssi_acc = (int16_t)(s_priv[idx].rssi_acc
                          + (((int16_t)e.rssi * 4 - s_priv[idx].rssi_acc) / 4));
  }

  s_peers[idx].genome       = f.u.beacon.genome;
  s_peers[idx].stage        = f.u.beacon.stage;
  s_peers[idx].cq_hi        = f.u.beacon.cq_hi;
  s_peers[idx].peer_flags   = f.u.beacon.flags;
  s_peers[idx].rssi         = (int8_t)(s_priv[idx].rssi_acc / 4);
  s_peers[idx].last_seen_ms = millis();
  if (s_priv[idx].hits < 0xFFFFu) s_priv[idx].hits++;
}

static void peers_age(void) {
  uint32_t now = millis();
  uint8_t i = 0;
  while (i < s_peer_n) {
    if ((uint32_t)(now - s_peers[i].last_seen_ms) >= (uint32_t)BLE_PEER_TTL_S * 1000UL) {
      peer_remove(i);          // do not advance i: the next entry slid into it
    } else {
      ++i;
    }
  }
}

// -----------------------------------------------------------------------------
// 4. RADIO PRIMITIVES - the only place BLE APIs are touched, always from loop()
// -----------------------------------------------------------------------------

// Anonymous namespace: the vtable and typeinfo stay internal to this TU, so
// no other module can ever collide with them at link time.
namespace {

class NtScanCallbacks : public BLEAdvertisedDeviceCallbacks {
  // Runs on the Bluedroid BTC task. Touch NOTHING but the ring buffer.
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    if (!advertisedDevice.haveManufacturerData()) return;
    String md = advertisedDevice.getManufacturerData();   // real copy, binary safe
    if (md.length() != (unsigned int)BLE_MSD_LEN) return;
    const uint8_t *p = (const uint8_t *)md.c_str();
    // Company id is little-endian on the wire.
    if (p[0] != (uint8_t)(BLE_COMPANY_ID & 0xFFu)) return;
    if (p[1] != (uint8_t)((BLE_COMPANY_ID >> 8) & 0xFFu)) return;
    if (p[2] != BLE_FRAME_BEACON && p[2] != BLE_FRAME_MATE_OFFER && p[2] != BLE_FRAME_MATE_ACK) return;

    uint16_t head = __atomic_load_n(&s_q_head, __ATOMIC_RELAXED);
    uint16_t next = (uint16_t)((head + 1u) % (uint16_t)BLE_QUEUE_CAP);
    if (next == __atomic_load_n(&s_q_tail, __ATOMIC_ACQUIRE)) {
      __atomic_fetch_add(&s_q_drop, 1u, __ATOMIC_RELAXED);
      return;                                              // full: newest loses
    }
    BLEAddress addr = advertisedDevice.getAddress();       // named: getNative()
    memcpy(s_q[head].mac, *addr.getNative(), 6);           // points into addr
    int r = advertisedDevice.getRSSI();
    s_q[head].rssi = (int8_t)NT_CLAMP(r, -128, 127);
    memcpy(s_q[head].msd, p, BLE_MSD_LEN);
    __atomic_store_n(&s_q_head, next, __ATOMIC_RELEASE);   // publish
  }
};

}  // anonymous namespace

static NtScanCallbacks s_cb;

// Also on the BTC task. One flag, nothing else.
static void on_scan_complete(BLEScanResults results) {
  (void)results;
  s_scan_done = true;
}

static void adv_apply(const BleFrame &f, bool fast) {
  BLEAdvertising *a = BLEDevice::getAdvertising();
  if (a == nullptr) return;
  a->stop();                                  // always stop before re-keying
  BLEAdvertisementData ad;
  ad.setFlags(0x06);                          // LE General Disc | BR/EDR not supported
  // Length-based String ctor: the genome is full of 0x00 and must survive.
  ad.setManufacturerData(String((const uint8_t *)&f, (unsigned int)BLE_MSD_LEN));
  a->setScanResponse(false);                  // no name on air, no auto scan-rsp
  a->setAdvertisementType(ADV_TYPE_NONCONN_IND);   // we have no GATT to offer
  // RAW 0.625 ms units here. BLEScan::setInterval below is MILLISECONDS.
  a->setMinInterval(fast ? (uint16_t)BLE_ADV_FAST_MIN_RAW : (uint16_t)BLE_ADV_MIN_RAW);
  a->setMaxInterval(fast ? (uint16_t)BLE_ADV_FAST_MAX_RAW : (uint16_t)BLE_ADV_MAX_RAW);
  a->setAdvertisementData(ad);                // esp_ble_gap_config_adv_data_raw
  a->start();
  s_adv_frame = f;
}

static void adv_stop(void) {
  BLEAdvertising *a = BLEDevice::getAdvertising();
  if (a != nullptr) a->stop();
  s_adv_mode = ADV_MODE_NONE;
}

static void build_beacon(BleFrame &f) {
  memset(&f, 0, sizeof(f));
  f.company_id      = (uint16_t)BLE_COMPANY_ID;
  f.frame_type      = (uint8_t)BLE_FRAME_BEACON;
  f.u.beacon.genome = s_self_genome;
  f.u.beacon.stage  = s_self_stage;
  f.u.beacon.cq_hi  = s_self_cq_hi;
  uint8_t bf = 0;
  if (s_self_flags & BLE_SELF_GOD)     bf |= BLE_BF_DEBUG;
  if (s_self_flags & BLE_SELF_SEEKING) bf |= BLE_BF_SEEKING;
  if (s_self_flags & BLE_SELF_SICK)    bf |= BLE_BF_SICK;
  f.u.beacon.flags = bf;
}

// Re-key the beacon only when its bytes actually changed: every call to
// adv_apply() stops and restarts the advertiser, which is not free.
static void beacon_refresh(void) {
  if (!s_up || !s_self_valid) return;
  BleFrame f;
  build_beacon(f);
  if (s_adv_mode == ADV_MODE_BEACON && memcmp(&f, &s_adv_frame, sizeof(f)) == 0) return;
  adv_apply(f, false);
  s_adv_mode = ADV_MODE_BEACON;
}

static void scan_restart(bool fast) {
  BLEScan *s = BLEDevice::getScan();
  if (s == nullptr) return;
  s->stop();                        // MANDATORY: start() re-entry blocks forever
  s->setActiveScan(false);          // passive: we never send SCAN_REQ
  s->setInterval(fast ? (uint16_t)BLE_SCAN_FAST_INTERVAL_MS : (uint16_t)BLE_SCAN_INTERVAL_MS);
  s->setWindow(fast ? (uint16_t)BLE_SCAN_FAST_WINDOW_MS : (uint16_t)BLE_SCAN_WINDOW_MS);
  // wantDuplicates=true keeps m_scanResults empty AND gives a fresh RSSI per
  // packet; shouldParse=true is what populates getManufacturerData().
  s->setAdvertisedDeviceCallbacks(&s_cb, true, true);
  s_scan_done       = false;
  s_scan_fast       = fast;
  s_scan_started_ms = millis();
  s_scan_running    = s->start((uint32_t)BLE_SCAN_DURATION_S, on_scan_complete, false);
}

// -----------------------------------------------------------------------------
// 5. RESULT LATCH
// -----------------------------------------------------------------------------

static void latch(uint8_t ok, uint8_t role, const uint8_t mac[6], uint8_t peer_stage,
                  const Genome *child, uint16_t str_id) {
  memset(&s_result, 0, sizeof(s_result));
  s_result.ok         = ok;
  s_result.role       = role;
  s_result.peer_stage = peer_stage;
  s_result.str_id     = str_id;
  if (mac   != nullptr) memcpy(s_result.peer_mac, mac, 6);
  if (child != nullptr) s_result.child = *child;
  s_result_valid = true;
  s_result_ms    = millis();
}

// A refusal that is our own fault (tired, too young, on cooldown). No penalty
// cooldown: the user simply cannot try right now.
static void fail_local(uint16_t str_id) {
  latch(0, BLE_ROLE_INITIATOR, nullptr, (uint8_t)STAGE_COUNT, nullptr, str_id);
  s_mate_state = BLE_MATE_FAIL;
}

// A genuine rejection: 45 min before this unit may court again (GAME_DESIGN 4.2).
static void fail_courted(uint16_t str_id, const uint8_t mac[6], uint8_t peer_stage) {
  latch(0, BLE_ROLE_INITIATOR, mac, peer_stage, nullptr, str_id);
  s_mate_state = BLE_MATE_FAIL;
  cooldown_arm(BLE_MATE_FAIL_COOLDOWN_S);
}

// -----------------------------------------------------------------------------
// 6. ELIGIBILITY
// -----------------------------------------------------------------------------

// 0 = clear. Otherwise the StrId explaining the refusal in Spanish.
static uint16_t self_block_reason(void) {
  if (!s_up)                                    return (uint16_t)STR_ERR_BUSY;
  if (s_sessions >= BLE_SESSION_CAP)            return (uint16_t)STR_SO_CAP;
  if (!s_self_valid)                            return (uint16_t)STR_ERR_GENOME;
  if (cooldown_live())                          return (uint16_t)STR_SO_COOLDOWN;
  if (s_self_flags & BLE_SELF_MATE_LOCK)        return (uint16_t)STR_SO_COOLDOWN;
  if (s_self_stage < (uint8_t)STAGE_TEEN)       return (uint16_t)STR_SO_YOUNG;
  if (s_self_stage >= (uint8_t)STAGE_DEAD)      return (uint16_t)STR_AERR_DEAD;
  if (s_self_energy < BLE_MATE_MIN_ENERGY_PCT)  return (uint16_t)STR_SO_TIRED;
  if (!(s_self_flags & BLE_SELF_SEEKING))       return (uint16_t)STR_SO_LOSE;
  return 0;
}

// The peer must be a real, close, courtable, non-tainted, teen-or-older pet
// that is itself sitting on the SOCIAL screen.
static uint16_t peer_block_reason(uint8_t idx) {
  const BlePeerInfo &p = s_peers[idx];
  if (!peer_solid(idx))                                          return (uint16_t)STR_SO_FAR;
  if (p.stage < (uint8_t)STAGE_TEEN)                             return (uint16_t)STR_SO_YOUNG;
  if (p.stage >= (uint8_t)STAGE_DEAD)                            return (uint16_t)STR_SO_LOSE;
  if (!(p.peer_flags & BLE_BF_SEEKING))                          return (uint16_t)STR_SO_LOSE;
  if (!genome_wire_ok(p.genome))                                 return (uint16_t)STR_ERR_GENOME;
  // A god-mode unit must not pollute a real dynasty. Tainted talks to tainted.
  if ((p.peer_flags & BLE_BF_DEBUG) && !(s_self_flags & BLE_SELF_GOD))
    return (uint16_t)STR_ERR_GENOME;
  return 0;
}

// -----------------------------------------------------------------------------
// 7. FRAME HANDLERS - all of these run in loop() context
// -----------------------------------------------------------------------------

static void handle_beacon(const RawEvt &e, const BleFrame &f) {
  if (e.rssi < BLE_RSSI_MIN) return;                 // not in this room
  if (!genome_wire_ok(f.u.beacon.genome)) return;    // corrupt or foreign
  if (memcmp(e.mac, s_self_mac, 6) == 0) return;     // never court yourself
  peer_touch(e, f);
}

static void handle_offer(const RawEvt &e, const BleFrame &f) {
  if (memcmp(f.u.offer.target3, s_self_mac + 3, 3) != 0) return;   // not for us

  // An offer is re-advertised for many seconds. Answer it exactly once.
  if (s_mate_state == BLE_MATE_ACKING) return;
  if (s_mate_state != BLE_MATE_IDLE)   return;       // busy, or a result is pending

  if (e.rssi < BLE_RSSI_MIN)               return;
  if (self_block_reason() != 0)            return;   // silence is the refusal
  if (!genome_wire_ok(f.u.offer.child))    return;

  int idx = peer_find(e.mac);
  if (idx < 0)                             return;   // never heard them beacon
  if (peer_block_reason((uint8_t)idx) != 0) return;

  // Accept. The child was bred by the initiator and travels whole, so both
  // units end up with byte-identical eggs; we only verify the CRC.
  memcpy(s_mate_peer_mac, e.mac, 6);
  s_mate_peer_known = true;
  memcpy(s_mate_mac3, e.mac + 3, 3);
  s_mate_child      = f.u.offer.child;
  s_mate_child_crc  = f.u.offer.child.crc16;

  BleFrame a;
  memset(&a, 0, sizeof(a));
  a.company_id       = (uint16_t)BLE_COMPANY_ID;
  a.frame_type       = (uint8_t)BLE_FRAME_MATE_ACK;
  a.u.ack.child_crc  = s_mate_child_crc;
  memcpy(a.u.ack.target3, e.mac + 3, 3);
  adv_apply(a, true);
  s_adv_mode   = ADV_MODE_ACK;
  s_mate_state = BLE_MATE_ACKING;
  s_mate_t0    = millis();

  cooldown_arm(BLE_MATE_COOLDOWN_S);
  latch(1, BLE_ROLE_RESPONDER, e.mac, s_peers[idx].stage, &s_mate_child, (uint16_t)STR_SO_EGG_MADE);
}

static void handle_ack(const RawEvt &e, const BleFrame &f) {
  if (s_mate_state != BLE_MATE_OFFERING) return;
  if (memcmp(f.u.ack.target3, s_self_mac + 3, 3) != 0) return;     // not for us
  if (s_mate_peer_known) {
    if (memcmp(e.mac, s_mate_peer_mac, 6) != 0) return;            // wrong unit
  } else {
    if (memcmp(e.mac + 3, s_mate_mac3, 3) != 0) return;
  }
  if (f.u.ack.child_crc != s_mate_child_crc) return;               // wrong egg

  int idx = peer_find(e.mac);
  uint8_t pstage = (idx >= 0) ? s_peers[idx].stage : (uint8_t)STAGE_COUNT;

  cooldown_arm(BLE_MATE_COOLDOWN_S);
  latch(1, BLE_ROLE_INITIATOR, e.mac, pstage, &s_mate_child, (uint16_t)STR_SO_EGG_MADE);
  s_mate_state = BLE_MATE_OK;
  beacon_refresh();                                // straight back to idle rate
}

static void dispatch(const RawEvt &e) {
  BleFrame f;
  memcpy(&f, e.msd, sizeof(f));                    // aligned working copy
  switch (f.frame_type) {
    case BLE_FRAME_BEACON:     handle_beacon(e, f); break;
    case BLE_FRAME_MATE_OFFER: handle_offer(e, f);  break;
    case BLE_FRAME_MATE_ACK:   handle_ack(e, f);    break;
    default: break;
  }
}

// -----------------------------------------------------------------------------
// 8. CONTAGION BY PROXIMITY
//    A sick pet radiates. Catching it needs sustained closeness, not a
//    drive-by: BLE_CONTAGION_EXPOSURE_S of continuous contact above
//    BLE_CONTAGION_RSSI, then one roll. Walking away drains the meter.
// -----------------------------------------------------------------------------
static void contagion_service(uint32_t dt_ms) {
  uint32_t now = millis();
  const uint32_t need_ms = (uint32_t)BLE_CONTAGION_EXPOSURE_S * 1000UL;

  for (uint8_t i = 0; i < s_peer_n; ++i) {
    PeerPriv &pv = s_priv[i];

    if (pv.infect_armed) {
      if ((uint32_t)(now - pv.last_infect_ms) >= (uint32_t)BLE_CONTAGION_REARM_S * 1000UL) {
        pv.infect_armed = 0;
      }
      pv.exposure_ms = 0;
      continue;
    }

    const bool contagious = (s_peers[i].peer_flags & BLE_BF_SICK) != 0
                         && s_peers[i].rssi >= BLE_CONTAGION_RSSI
                         && peer_fresh(i);

    // Already sick: nothing to catch. Still age the meter so it is clean when
    // the pet recovers.
    if (!contagious || (s_self_flags & BLE_SELF_SICK) || s_self_stage < (uint8_t)STAGE_BABY
        || s_self_stage >= (uint8_t)STAGE_DEAD) {
      pv.exposure_ms = (pv.exposure_ms > dt_ms) ? (pv.exposure_ms - dt_ms) : 0;
      continue;
    }

    pv.exposure_ms += dt_ms;
    if (pv.exposure_ms >= need_ms) {
      pv.exposure_ms    = 0;
      pv.infect_armed   = 1;
      pv.last_infect_ms = now;
      if ((uint8_t)(esp_random() % 100u) < (uint8_t)BLE_CONTAGION_P_PCT) {
        s_contagion = true;
      }
    }
  }
}

// -----------------------------------------------------------------------------
// 9. PUBLIC INTERFACE
// -----------------------------------------------------------------------------

static void reset_transient(void) {
  memset(s_peers, 0, sizeof(s_peers));
  memset(s_priv, 0, sizeof(s_priv));
  s_peer_n = 0;
  // Empty the ring WITHOUT touching s_q_head. This runs on the loop task, which
  // is the consumer and therefore owns s_q_tail and nothing else; s_q_head
  // belongs to the Bluedroid BTC task. Storing 0 into both (as this did) races
  // ble_end(): if onResult() is past its full-check at that instant it publishes
  // s_q_head = next computed from the PRE-reset head, and the ring comes back
  // holding up to BLE_QUEUE_CAP-1 stale RawEvts - a phantom peer on the next
  // session. Assigning tail := head keeps the single-writer invariant intact,
  // so the worst case is one advertisement that was mid-publish being dropped.
  __atomic_store_n(&s_q_tail,
                   __atomic_load_n(&s_q_head, __ATOMIC_ACQUIRE),
                   __ATOMIC_RELEASE);
  s_mate_state      = BLE_MATE_IDLE;
  s_mate_peer_known = false;
  memset(s_mate_peer_mac, 0, sizeof(s_mate_peer_mac));
  memset(s_mate_mac3, 0, sizeof(s_mate_mac3));
  s_mate_child_crc  = 0;
  s_adv_mode        = ADV_MODE_NONE;
  memset(&s_adv_frame, 0, sizeof(s_adv_frame));
  s_scan_running    = false;
  s_scan_fast       = false;
  s_scan_done       = false;
  // A successful result survives a begin/end cycle: an egg must never be lost
  // just because the user left the SOCIAL screen before the UI read it.
  // A failure is only a message, and a stale one would be confusing.
  if (s_result_valid && s_result.ok == 0) s_result_valid = false;
}

bool ble_begin(void) {
  if (s_up) return true;
  // Second line of defence only: net.cpp owns the authoritative counter
  // (net_ble_sessions_used()). Both refuse at the same number.
  if (s_sessions >= BLE_SESSION_CAP) return false;   // UI must show STR_SO_CAP

  // net.cpp is the only module allowed to call BLEDevice::init/deinit
  // (BRIEF 4). Ask for RADIO_BLE before calling this.
  if (!BLEDevice::getInitialized()) return false;

  // BLEDevice::init() sets `initialized = true` BEFORE btStart(), so
  // getInitialized() is true even when the controller failed to start. The
  // base address is only published once the controller is actually enabled,
  // so this is the honest liveness probe.
  const uint8_t *bda = esp_bt_dev_get_address();
  if (bda == nullptr) return false;

  s_sessions++;
  memcpy(s_self_mac, bda, 6);

  // 0 dBm instead of the +3 dBm default: 3 dB less reach shrinks the physical
  // mating radius without hurting same-room discovery, and it is 3 dB the
  // battery keeps. Combined with the -70 dBm gate this is "same room", and
  // with the -60 dBm contagion gate it is "same table".
  BLEDevice::setPower(ESP_PWR_LVL_N0, ESP_BLE_PWR_TYPE_ADV);

  reset_transient();
  s_last_service_ms = millis();
  s_up = true;

  scan_restart(false);
  if (s_self_valid) beacon_refresh();
  return true;
}

void ble_end(void) {
  // Quiesce whether or not we think we are up: this must leave the stack in a
  // state where net.cpp's BLEDevice::deinit(false) is safe, and where the next
  // BLEScan::start() cannot deadlock.
  if (BLEDevice::getInitialized()) {
    BLEScan *s = BLEDevice::getScan();
    if (s != nullptr) {
      // stop() also gives the scan-end semaphore. That object survives
      // deinit(), so skipping this would hang the next start() forever.
      s->stop();
      s->setAdvertisedDeviceCallbacks(nullptr, true, true);  // no stale callback
      s->clearResults();                                     // never erase()
    }
    adv_stop();
  }
  // The stack itself is torn down by net_request(RADIO_OFF) -> ble_down() ->
  // BLEDevice::deinit(false), which is where the ~70 KB comes back. Calling
  // deinit here as well would desynchronise net.cpp's radio state machine and
  // its session counter, and deinit(true) is forbidden outright: it leaves
  // `initialized == true` so a later init() silently no-ops forever.
  s_up = false;
  reset_transient();
}

bool ble_is_up(void) {
  return s_up;
}

uint8_t ble_session_count(void) {
  return s_sessions;
}

void ble_advertise_beacon(const Genome &g, uint8_t stage, uint8_t cq_hi) {
  s_self_genome = g;
  s_self_stage  = stage;
  s_self_cq_hi  = cq_hi;
  s_self_valid  = genome_wire_ok(g);
  if (!s_up) return;
  if (s_mate_state == BLE_MATE_OFFERING || s_mate_state == BLE_MATE_ACKING) return;
  beacon_refresh();
}

void ble_set_self(uint8_t energy_pct, uint8_t self_flags) {
  s_self_energy = (uint8_t)NT_CLAMP((int)energy_pct, 0, 100);
  s_self_flags  = self_flags;
  if (!s_up) return;
  if (s_mate_state == BLE_MATE_OFFERING || s_mate_state == BLE_MATE_ACKING) return;
  beacon_refresh();
}

void ble_advertise_offer(const Genome &child, const uint8_t mac3[3]) {
  if (mac3 == nullptr) { fail_local((uint16_t)STR_AERR_BAD_ARG); return; }

  uint16_t why = self_block_reason();
  if (why != 0) { fail_local(why); return; }
  if (s_mate_state != BLE_MATE_IDLE) { fail_local((uint16_t)STR_ERR_BUSY); return; }

  int idx = peer_find_mac3(mac3);
  if (idx < 0) { fail_local((uint16_t)STR_SO_FAR); return; }

  const BlePeerInfo &p = s_peers[idx];
  why = peer_block_reason((uint8_t)idx);
  if (why != 0) { fail_local(why); return; }

  // The egg itself is bred by the caller (genome.cpp owns the genetics); we
  // only refuse to put a corrupt one on the air.
  if (!genome_wire_ok(child)) { fail_local((uint16_t)STR_ERR_GENOME); return; }

  const uint8_t sex_a  = GN_GET(s_self_genome.g2, GN_SEX_SH, GN_SEX_MK);
  const uint8_t sex_b  = GN_GET(p.genome.g2,      GN_SEX_SH, GN_SEX_MK);
  const uint8_t luck_a = GN_GET(s_self_genome.g2, GN_LUCK_SH, GN_LUCK_MK);
  const uint8_t luck_b = GN_GET(p.genome.g2,      GN_LUCK_SH, GN_LUCK_MK);
  // GAME_DESIGN 3.1: same sex needs luck >= 6 on at least one side.
  if (sex_a == sex_b && NT_MAX(luck_a, luck_b) < 6) {
    fail_courted((uint16_t)STR_SO_LOSE, p.mac, p.stage);
    return;
  }

  // GAME_DESIGN 4.2 success probability, in pure integers:
  //   sociability_mult x1000 = 700 + 40*v
  //   p = avg(multA, multB) * 0.5
  //     = ((700+40a) + (700+40b)) / 2 / 2  = 350 + 10*(a+b)   [x1000]
  //   p_pct = p/10 = 35 + a + b            -> 35..65 %, then clamped 25..90 %.
  const uint8_t soc_a = GN_GET(s_self_genome.g1, GN_SOCIAB_SH, GN_SOCIAB_MK);
  const uint8_t soc_b = GN_GET(p.genome.g1,      GN_SOCIAB_SH, GN_SOCIAB_MK);
  const int32_t p_pct = NT_CLAMP((int32_t)35 + (int32_t)soc_a + (int32_t)soc_b,
                                 (int32_t)BLE_MATE_P_MIN_PCT, (int32_t)BLE_MATE_P_MAX_PCT);
  if ((int32_t)(esp_random() % 100u) >= p_pct) {
    fail_courted((uint16_t)STR_SO_LOSE, p.mac, p.stage);
    return;
  }

  memcpy(s_mate_peer_mac, p.mac, 6);
  s_mate_peer_known = true;
  memcpy(s_mate_mac3, mac3, 3);
  s_mate_child      = child;
  s_mate_child_crc  = child.crc16;

  BleFrame f;
  memset(&f, 0, sizeof(f));
  f.company_id   = (uint16_t)BLE_COMPANY_ID;
  f.frame_type   = (uint8_t)BLE_FRAME_MATE_OFFER;
  f.u.offer.child = child;
  memcpy(f.u.offer.target3, mac3, 3);
  adv_apply(f, true);
  s_adv_mode   = ADV_MODE_OFFER;
  s_mate_state = BLE_MATE_OFFERING;
  s_mate_t0    = millis();
}

uint8_t ble_mate_state(void) {
  return s_mate_state;
}

bool ble_mate_take(BleMateEvent &ev) {
  if (!s_result_valid) return false;
  ev = s_result;
  s_result_valid = false;
  if (s_mate_state == BLE_MATE_OK || s_mate_state == BLE_MATE_FAIL) {
    s_mate_state = BLE_MATE_IDLE;
    if (s_up) beacon_refresh();
  }
  return true;
}

uint32_t ble_mate_cooldown_left_s(void) {
  if (!cooldown_live()) return 0;
  uint32_t gone = (uint32_t)(millis() - s_cd_start_ms);
  return (s_cd_len_ms - gone + 999UL) / 1000UL;
}

bool ble_take_contagion(void) {
  if (!s_contagion) return false;
  s_contagion = false;
  return true;
}

uint8_t ble_peer_count(void) {
  return s_peer_n;
}

const BlePeerInfo *ble_peer(uint8_t i) {
  if (i >= s_peer_n) return nullptr;
  return &s_peers[i];
}

void ble_scan_service(void) {
  if (!s_up) return;

  const uint32_t now = millis();
  uint32_t dt = (uint32_t)(now - s_last_service_ms);
  if (dt > 5000UL) dt = 5000UL;      // a long stall must not bank exposure
  s_last_service_ms = now;

  // --- 1. drain the BTC-task ring ------------------------------------------
  uint16_t tail = __atomic_load_n(&s_q_tail, __ATOMIC_RELAXED);
  for (uint8_t guard = 0; guard < (uint8_t)BLE_QUEUE_CAP; ++guard) {
    if (tail == __atomic_load_n(&s_q_head, __ATOMIC_ACQUIRE)) break;
    RawEvt e = s_q[tail];                              // copy out, then release
    tail = (uint16_t)((tail + 1u) % (uint16_t)BLE_QUEUE_CAP);
    __atomic_store_n(&s_q_tail, tail, __ATOMIC_RELEASE);
    dispatch(e);
  }

  // --- 2. age the peer table -----------------------------------------------
  peers_age();

  // --- 3. contagion ---------------------------------------------------------
  contagion_service(dt);

  // --- 4. handshake timeouts ------------------------------------------------
  if (s_mate_state == BLE_MATE_OFFERING && elapsed_since(s_mate_t0, BLE_OFFER_TIMEOUT_MS)) {
    // Nobody answered: either they are out of range, asleep, on cooldown or
    // simply not on the SOCIAL screen. Same outcome, 45 min to think about it.
    uint8_t pstage = (uint8_t)STAGE_COUNT;
    int idx = peer_find(s_mate_peer_mac);
    if (idx >= 0) pstage = s_peers[idx].stage;
    fail_courted((uint16_t)STR_SO_LOSE, s_mate_peer_mac, pstage);
    beacon_refresh();
  } else if (s_mate_state == BLE_MATE_ACKING && elapsed_since(s_mate_t0, BLE_ACK_WINDOW_MS)) {
    // The ACK has been repeated long enough to be heard many times over. The
    // egg was latched when we accepted, so this only takes the radio back.
    s_mate_state = s_result_valid ? (uint8_t)BLE_MATE_OK : (uint8_t)BLE_MATE_IDLE;
    beacon_refresh();
  } else if ((s_mate_state == BLE_MATE_OK || s_mate_state == BLE_MATE_FAIL)
             && !s_result_valid) {
    s_mate_state = BLE_MATE_IDLE;
  }

  // A result nobody ever read must not wedge the machine forever.
  if (s_result_valid && elapsed_since(s_result_ms, BLE_RESULT_TTL_MS)
      && s_mate_state != BLE_MATE_ACKING) {
    s_result_valid = false;
    if (s_mate_state == BLE_MATE_OK || s_mate_state == BLE_MATE_FAIL) {
      s_mate_state = BLE_MATE_IDLE;
    }
  }

  // --- 5. scan duty cycle ---------------------------------------------------
  const bool want_fast = (s_mate_state == BLE_MATE_OFFERING || s_mate_state == BLE_MATE_ACKING);
  const bool stalled   = elapsed_since(s_scan_started_ms,
                                       (uint32_t)BLE_SCAN_DURATION_S * 1000UL + BLE_SCAN_WATCHDOG_MS);
  if (want_fast != s_scan_fast || s_scan_done || !s_scan_running || stalled) {
    scan_restart(want_fast);
  }

  // --- 6. keep the beacon honest -------------------------------------------
  if (s_mate_state == BLE_MATE_IDLE && s_adv_mode != ADV_MODE_BEACON) {
    beacon_refresh();
  }
}

// -----------------------------------------------------------------------------
// 10. GOD MODE HOOKS - command 10 "BLE FALSO"
//     These run in loop() context, exactly like ble_scan_service(), so they
//     bypass the ring buffer (which is strictly single-producer) and feed the
//     frame handlers directly.
// -----------------------------------------------------------------------------
#if GOD_MODE_ENABLED

static const uint8_t GOD_PEER_MAC[6] = { 0x02, 0x4E, 0x54, 0xFA, 0x11, 0x50 };

bool ble_debug_inject_beacon(const Genome &g, uint8_t stage, uint8_t cq_hi,
                             uint8_t peer_flags, int8_t rssi) {
  if (!s_up) return false;
  RawEvt e;
  memcpy(e.mac, GOD_PEER_MAC, 6);
  e.rssi = rssi;
  BleFrame f;
  memset(&f, 0, sizeof(f));
  f.company_id      = (uint16_t)BLE_COMPANY_ID;
  f.frame_type      = (uint8_t)BLE_FRAME_BEACON;
  f.u.beacon.genome = g;
  f.u.beacon.stage  = stage;
  f.u.beacon.cq_hi  = cq_hi;
  f.u.beacon.flags  = (uint8_t)(peer_flags | BLE_BF_DEBUG);
  memcpy(e.msd, &f, sizeof(f));
  // One injection is one packet; call it BLE_PEER_MIN_HITS times to make the
  // synthetic peer courtable, exactly like a real one.
  dispatch(e);
  return true;
}

bool ble_debug_inject_ack(void) {
  if (!s_up || s_mate_state != BLE_MATE_OFFERING) return false;
  if (memcmp(s_mate_peer_mac, GOD_PEER_MAC, 6) != 0) return false;
  RawEvt e;
  memcpy(e.mac, GOD_PEER_MAC, 6);
  e.rssi = -40;
  BleFrame f;
  memset(&f, 0, sizeof(f));
  f.company_id      = (uint16_t)BLE_COMPANY_ID;
  f.frame_type      = (uint8_t)BLE_FRAME_MATE_ACK;
  f.u.ack.child_crc = s_mate_child_crc;
  memcpy(f.u.ack.target3, s_self_mac + 3, 3);
  memcpy(e.msd, &f, sizeof(f));
  dispatch(e);
  return true;
}

#endif // GOD_MODE_ENABLED

#else  // ---------------------------------------------------------------------
// FEATURE_BLE == 0. The API still links so no consumer needs an #if, but
// nothing of Bluedroid's 777 KB of flash is dragged into the image.
// -----------------------------------------------------------------------------

#include <string.h>

bool ble_begin(void) {
  return false;
}
void ble_end(void) {}
void ble_advertise_beacon(const Genome &g, uint8_t stage, uint8_t cq_hi) {
  (void)g;
  (void)stage;
  (void)cq_hi;
}
void ble_advertise_offer(const Genome &child, const uint8_t mac3[3]) {
  (void)child;
  (void)mac3;
}
void ble_scan_service(void) {}
uint8_t ble_peer_count(void) {
  return 0;
}
const BlePeerInfo *ble_peer(uint8_t i) {
  (void)i;
  return nullptr;
}
void ble_set_self(uint8_t energy_pct, uint8_t self_flags) {
  (void)energy_pct;
  (void)self_flags;
}
uint8_t ble_mate_state(void) {
  return (uint8_t)BLE_MATE_IDLE;
}
bool ble_mate_take(BleMateEvent &ev) {
  (void)ev;
  return false;
}
uint32_t ble_mate_cooldown_left_s(void) {
  return 0;
}
bool ble_take_contagion(void) {
  return false;
}
uint8_t ble_session_count(void) {
  return 0;
}
bool ble_is_up(void) {
  return false;
}

#if GOD_MODE_ENABLED
bool ble_debug_inject_beacon(const Genome &g, uint8_t stage, uint8_t cq_hi,
                             uint8_t peer_flags, int8_t rssi) {
  (void)g;
  (void)stage;
  (void)cq_hi;
  (void)peer_flags;
  (void)rssi;
  return false;
}
bool ble_debug_inject_ack(void) {
  return false;
}
#endif

#endif // FEATURE_BLE
