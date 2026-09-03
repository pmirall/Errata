// =============================================================================
//  NOTTAMAGOCHI - ble_social.cpp
//  Connectionless BLE discovery: identity beacon plus passive scan.
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
//  * Every BLEScan bug listed in the shipped-library audit is defused here:
//      - getDevice()/getResults() are NEVER called (they deref begin() with no
//        bounds check), so the getCount()>0 guard is moot by construction.
//      - erase() is NEVER called (it derefs find() without an end() compare).
//      - stop() always precedes start(), because start() takes a binary
//        semaphore with portMAX_DELAY and re-entry hangs forever.
//      - setAdvertisedDeviceCallbacks(&cb, true, true): wantDuplicates=true is
//        what stops m_scanResults growing without bound, and it also gives us
//        a fresh RSSI on every packet, which is what proximity needs.
//  * LIFECYCLE SPLIT. net.cpp is "the only module allowed to call
//    BLEDevice::init/deinit", and it already does exactly that and owns
//    the BLE_SESSION_CAP counter. So ble_begin() does NOT init the stack: it
//    rides on a stack net_request(RADIO_BLE) already brought up, and refuses
//    if it is not there. ble_end() quiesces advertising and scanning and drops
//    the callback; the ~70 KB is genuinely returned by net_request(RADIO_OFF)
//    -> BLEDevice::deinit(false). deinit(true) is forbidden everywhere: it
//    leaves `initialized == true` and a later init() silently no-ops.
//    ble_end() ALWAYS calls BLEScan::stop() first, because the scan-end
//    semaphore lives in the BLEScan object, which deinit() never frees - a
//    deinit with the semaphore still taken would hang the next start().
//  * TRANSPORT ONLY. No game rule lives in this file. The peer table is raw
//    discovery data; what the game does with a peer is decided one layer up.
//  * Integer only. No float touches any game value in this file.
// =============================================================================

#include "ble_social.h"

#if FEATURE_BLE

#include <Arduino.h>
#include <string.h>
#include "crc16.h"
#include <esp_bt_device.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertising.h>
#include <BLEAdvertisedDevice.h>

// -----------------------------------------------------------------------------
// 0. WIRE FORMAT - 22 bytes of Manufacturer Specific Data
//    AD budget: Flags(3) + MSD(2 + 22) = 27 of the 31 available bytes.
//    addData() silently DROPS an oversized AD structure, hence the static
//    assert rather than a runtime check.
// -----------------------------------------------------------------------------
NT_PACK_PUSH
struct NT_PACKED BleFrame {
  uint16_t company_id;   // 0..1   BLE_COMPANY_ID (0xFFFF, SIG reserved)
  uint8_t  frame_type;   // 2      BLE_FRAME_BEACON (the only one on air)
  Genome   genome;       // 3..18
  uint8_t  stage;        // 19
  uint8_t  cq_hi;        // 20
  uint8_t  flags;        // 21     BLE_BF_*
};
NT_PACK_POP

static_assert(sizeof(BleFrame) == BLE_MSD_LEN, "BleFrame must be exactly BLE_MSD_LEN bytes");
static_assert(sizeof(BleFrame) == 22, "the MSD payload is fixed at 22 bytes");
static_assert(offsetof(BleFrame, genome) == 3, "BleFrame payload must start at byte 3");
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
static uint8_t  s_self_flags    = 0;      // BLE_SELF_*

// --- advertising -------------------------------------------------------------
enum AdvMode : uint8_t { ADV_MODE_NONE = 0, ADV_MODE_BEACON };
static uint8_t  s_adv_mode = ADV_MODE_NONE;
static BleFrame s_adv_frame;              // exactly what is on air right now

// --- scanning ----------------------------------------------------------------
static bool     s_scan_running    = false;
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
};
static BlePeerInfo s_peers[BLE_PEER_CAP];
static PeerPriv    s_priv[BLE_PEER_CAP];
static uint8_t     s_peer_n = 0;

// -----------------------------------------------------------------------------
// 2. SMALL PURE HELPERS
// -----------------------------------------------------------------------------

// A genome arriving from the air is hostile until proven otherwise. The CRC is
// crc16_ccitt() from crc16.h, the same function genome.cpp seals with.
static bool genome_wire_ok(const Genome &g) {
  if ((uint16_t)(g.magic_ver & GENOME_SIG_MASK) != (uint16_t)GENOME_SIG) return false;
  if ((uint16_t)(g.magic_ver & 0x000Fu) != (uint16_t)GENOME_PROTO_VER) return false;
  if (g.lineage_id == 0) return false;               // 0 is reserved for "invalid"
  uint8_t tmp[16];
  memcpy(tmp, &g, sizeof(tmp));                      // packed -> aligned copy
  return crc16_ccitt(tmp, GENOME_CRC_BYTES) == g.crc16;
}

static inline bool elapsed_since(uint32_t t0, uint32_t span_ms) {
  return (uint32_t)(millis() - t0) >= span_ms;       // rollover safe
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

  s_peers[idx].genome       = f.genome;
  s_peers[idx].stage        = f.stage;
  s_peers[idx].cq_hi        = f.cq_hi;
  s_peers[idx].peer_flags   = f.flags;
  s_peers[idx].rssi         = (int8_t)(s_priv[idx].rssi_acc / 4);
  s_peers[idx].last_seen_ms = millis();
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
    if (p[2] != BLE_FRAME_BEACON) return;

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

static void adv_apply(const BleFrame &f) {
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
  a->setMinInterval((uint16_t)BLE_ADV_MIN_RAW);
  a->setMaxInterval((uint16_t)BLE_ADV_MAX_RAW);
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
  f.company_id = (uint16_t)BLE_COMPANY_ID;
  f.frame_type = (uint8_t)BLE_FRAME_BEACON;
  f.genome     = s_self_genome;
  f.stage      = s_self_stage;
  f.cq_hi      = s_self_cq_hi;
  uint8_t bf = 0;
  if (s_self_flags & BLE_SELF_GOD)     bf |= BLE_BF_DEBUG;
  if (s_self_flags & BLE_SELF_SEEKING) bf |= BLE_BF_SEEKING;
  f.flags = bf;
}

// Re-key the beacon only when its bytes actually changed: every call to
// adv_apply() stops and restarts the advertiser, which is not free.
static void beacon_refresh(void) {
  if (!s_up || !s_self_valid) return;
  BleFrame f;
  build_beacon(f);
  if (s_adv_mode == ADV_MODE_BEACON && memcmp(&f, &s_adv_frame, sizeof(f)) == 0) return;
  adv_apply(f);
  s_adv_mode = ADV_MODE_BEACON;
}

static void scan_restart(void) {
  BLEScan *s = BLEDevice::getScan();
  if (s == nullptr) return;
  s->stop();                        // MANDATORY: start() re-entry blocks forever
  s->setActiveScan(false);          // passive: we never send SCAN_REQ
  s->setInterval((uint16_t)BLE_SCAN_INTERVAL_MS);
  s->setWindow((uint16_t)BLE_SCAN_WINDOW_MS);
  // wantDuplicates=true keeps m_scanResults empty AND gives a fresh RSSI per
  // packet; shouldParse=true is what populates getManufacturerData().
  s->setAdvertisedDeviceCallbacks(&s_cb, true, true);
  s_scan_done       = false;
  s_scan_started_ms = millis();
  s_scan_running    = s->start((uint32_t)BLE_SCAN_DURATION_S, on_scan_complete, false);
}

// -----------------------------------------------------------------------------
// 5. FRAME HANDLING - runs in loop() context
// -----------------------------------------------------------------------------

static void handle_beacon(const RawEvt &e, const BleFrame &f) {
  if (e.rssi < BLE_RSSI_MIN) return;                 // not in this room
  if (!genome_wire_ok(f.genome)) return;             // corrupt or foreign
  if (memcmp(e.mac, s_self_mac, 6) == 0) return;     // never list yourself
  peer_touch(e, f);
}

static void dispatch(const RawEvt &e) {
  BleFrame f;
  memcpy(&f, e.msd, sizeof(f));                    // aligned working copy
  if (f.frame_type == BLE_FRAME_BEACON) handle_beacon(e, f);
}

// -----------------------------------------------------------------------------
// 6. PUBLIC INTERFACE
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
  s_adv_mode        = ADV_MODE_NONE;
  memset(&s_adv_frame, 0, sizeof(s_adv_frame));
  s_scan_running    = false;
  s_scan_done       = false;
}

bool ble_begin(void) {
  if (s_up) return true;
  // Second line of defence only: net.cpp owns the authoritative counter
  // (net_ble_sessions_used()). Both refuse at the same number.
  if (s_sessions >= BLE_SESSION_CAP) return false;   // UI must show STR_SO_CAP

  // net.cpp is the only module allowed to call BLEDevice::init/deinit.
  // Ask for RADIO_BLE before calling this.
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
  // discovery radius without hurting same-room discovery, and it is 3 dB the
  // battery keeps. Combined with the -70 dBm gate this is "same room".
  BLEDevice::setPower(ESP_PWR_LVL_N0, ESP_BLE_PWR_TYPE_ADV);

  reset_transient();
  s_up = true;

  scan_restart();
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
  beacon_refresh();
}

void ble_set_self(uint8_t self_flags) {
  s_self_flags = self_flags;
  if (!s_up) return;
  beacon_refresh();
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

  // --- 3. keep the scanner alive -------------------------------------------
  const bool stalled = elapsed_since(s_scan_started_ms,
                                     (uint32_t)BLE_SCAN_DURATION_S * 1000UL + BLE_SCAN_WATCHDOG_MS);
  if (s_scan_done || !s_scan_running || stalled) {
    scan_restart();
  }

  // --- 4. keep the beacon honest -------------------------------------------
  if (s_adv_mode != ADV_MODE_BEACON) beacon_refresh();
}

#else  // ---------------------------------------------------------------------
// FEATURE_BLE == 0. The API still links so no consumer needs an #if, but
// nothing of Bluedroid's 777 KB of flash is dragged into the image.
// -----------------------------------------------------------------------------

bool ble_begin(void) {
  return false;
}
void ble_end(void) {}
void ble_advertise_beacon(const Genome &g, uint8_t stage, uint8_t cq_hi) {
  (void)g;
  (void)stage;
  (void)cq_hi;
}
void ble_scan_service(void) {}
uint8_t ble_peer_count(void) {
  return 0;
}
const BlePeerInfo *ble_peer(uint8_t i) {
  (void)i;
  return nullptr;
}
void ble_set_self(uint8_t self_flags) {
  (void)self_flags;
}
uint8_t ble_session_count(void) {
  return 0;
}
bool ble_is_up(void) {
  return false;
}

#endif // FEATURE_BLE
