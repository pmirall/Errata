# Pebblebol — decisions log

Every decision that changes hardware mapping, persisted names, transport choice or
language policy gets a row here. Closed decisions record the commit that applied them.
Evidence and defaults come from `PEBBLEBOL_IMPLEMENTATION_PLAN.md` §0.

Status values: **OPEN** (owner must decide), **DEFAULT** (plan default in force, owner
may override), **CLOSED** (decided; commit named).

## Owner decisions (plan §0.2)

| # | Decision | Status | Evidence | Default in force | Blocks | Outcome |
|---|---|---|---|---|---|---|
| **D1** | GPIO map (pin conflict) | **DEFERRED BY OWNER (2026-09-02)** — keep the values the repository already carries; revisit later | `config.h` compiles `PIN_SDA 8, PIN_SCL 9, PIN_BTN_L 10, PIN_BTN_R 2, PIN_LED 5` with the human comments "tu cableado actual del TinyLLM" / "OBLIGATORIO cambiarlo: 8 ya es SDA". README §2, CHANGELOG, `render.h`, `render.cpp` all document the other map (SDA=6, SCL=7, BTN_L=3, BTN_R=4, LED=8) and warn that GPIO2/8/9 are strapping pins. Nothing has ever run on hardware. | **Values stay exactly as committed.** `PB_PINS_CONFIRMED` is NOT defined, so the guard `static_assert`s added in P2-C8 stay dormant. | P2-C0 first flash (hardware track only), P6-C3 deep sleep | Owner defers; consequences recorded below the table. |
| **D2** | Peer-session transport: BLE vs Wi-Fi (ESP-NOW) | **CLOSED — ESP-NOW (2026-09-02, owner)** | The legacy BLE advert carries 19 B/frame and cannot carry the §15 message set; GATT was rejected by the original author for stability; each BLE bring-up burns one of 32 sessions with a claimed ~672 B Bluedroid leak; BLE costs 721,632 B flash / 23,688 B static RAM. ESP-NOW ships in the core (250 B/frame, unicast + send-callback ACK) and needs the same `WIFI_STA` residency the §40 scanner already requires. | ESP-NOW is the transport, behind the §59 `Transport` seam. `FEATURE_BLE 0` in the release build. | P7-C1 | Decided: ESP-NOW. See the consequences below the table. |
| **D3** | Sketch folder rename and product identity in persisted names | DEFAULT | `sketch_aug30b/` → `Pebblebol/`; `NVS_NS "notta"`, AP prefix `NOTTAMAGOCHI-`, mDNS `nottamagochi.local`. No device has ever run this firmware, so renaming orphans nothing real. | Folder renamed in P2-C1; NVS namespace `"pbbl"` in P2-C9 with a one-shot import of a legacy `"notta"` save; AP prefix `PEBBLEBOL-` in P8-C2; mDNS deleted in P2-C5. | P2-C1, P2-C9 | — |
| **D4** | Language of user-facing UI strings | DEFAULT | Spanish `strings_es.h` (439 strings, `StrId` mechanism) vs English. Spec header allows Spanish UI. | Keep Spanish; new BOX/BATTLE/NET/LINK/CREATOR/ERROR/TIME blocks written in Spanish in the same mechanism. An `strings_en.h` twin is a one-file swap later. | P2-C11 | — |
| **D5** | Panel variant SSD1306 vs SH1106 | DEFAULT | `DISPLAY_IS_SH1106` (`config.h:64`); both drivers verified to link. | Keep 0 (SSD1306). Flip only with the panel in front of you (README §3 symptoms). | P2-C0 | — |
| **D6** | Partition table / OTA room (§61) | DEFAULT | `huge_app.csv` = nvs 20 KB, otadata 8 KB, app0 3 MB, spiffs 896 KB unused, coredump 64 KB; no OTA slot. Core 3.1.1 honours a `partitions.csv` in the sketch folder. `initArduino()` erases the whole `nvs` partition on `ESP_ERR_NVS_NO_FREE_PAGES` / `NEW_VERSION_FOUND` before `setup()`. | Custom `Pebblebol/partitions.csv`: `nvs 0x9000 0x5000 · otadata 0xE000 0x2000 · app0 0x10000 0x300000 · nvs2 0x310000 0x10000 · spiffs 0x320000 0xD0000 (reserved) · coredump 0x3F0000 0x10000`. `nvs2` = checkpoint partition. OTA stays a V1 non-goal. | P2-C9d | — |
| **D7** | Creator inactivity grace (§34) | DEFAULT | 120 s vs 300 s. | `ConfigV2.creator_idle_s` default 300, editable in SETTINGS. | P8-C2 | — |
| **D8** | Piezo GPIO (new hardware, V1 baseline §6) | **OPEN — owner confirms when soldering** | A passive ~15 mm piezo joins the V1 BOM. Free, non-strapping GPIOs on this board: 3, 4, 6, 7. GPIO0 is kept for the battery divider (D10). `tone()`/`noTone()` and the LEDC driver are both in the installed core, so no library is needed. | Propose `PIN_PIEZO 3`. The tone engine is written against the macro, so changing it is a one-line edit. | P6-C3 (sleep GPIO states), P10-C2 (tone engine) | — |
| **D9** | Supply architecture for 2×AAA | **OPEN — blocks the >=30-day target** | Alkaline AAA pairs deliver ~2.8 V under load when fresh and sag to ~2.4 V at 80 % discharge, while the core this project builds with arms the brownout detector at level 7 (~3.0 V): verified `CONFIG_ESP_BROWNOUT_DET_LVL 7` in sdkconfig.h. The board's LDO cannot step 3.0 V up to 3.3 V. See `docs/hardware_reconciliation.md` §2. | Recommend a low-quiescent boost converter (~EUR 0.40-0.80, e.g. TPS61023 at ~12 uA quiescent); alternatives are lithium AAA or a third cell. No firmware work depends on which wins. | Hardware bring-up; the battery-life target | — |
| **D10** | Battery sense divider on GPIO0 | OPEN | Spec §26 wants NORMAL/LOW/CRITICAL levels. `PIN_VBAT_ADC 0` is already reserved and GPIO0 is ADC1_CH0, so this needs only two resistors. Without it those levels cannot exist and a flat pack corrupts a save instead of warning. | Two resistors; the firmware side lands with the power states in P6-C3. | P6-C3 | — |

## D1 — consequences of deferring (recorded 2026-09-02)

The owner chose to keep the pin map the repository already carries and revisit it later.
Nothing in the plan changes; these are the facts that follow from that map, so no later
phase re-derives them:

- **Compiled map:** `PIN_SDA 8`, `PIN_SCL 9`, `PIN_BTN_L 10`, `PIN_BTN_R 2`, `PIN_LED 5`.
- **GPIO9 carries SCL.** GPIO9 low at reset selects serial-download mode. I2C idles high
  through its pull-ups, so an ordinary boot should not trip it, but a short or a panel that
  drags SCL low across the power-on edge would make the board look dead while it is only
  waiting to be flashed.
- **GPIO2 carries the right button.** GPIO2 must be high at reset. Holding the right button
  while plugging in USB puts the chip into a different boot combination.
- **GPIO8 is the built-in LED and is now SDA**, and `PIN_LED 5` points at a pin that most
  likely has no LED on it. The blink-forever fatal path is therefore invisible on this
  board. Design consequence: the on-screen `ERROR` state (decision T10, built in P2-C11a)
  is the only diagnostic channel a user can actually see, which raises its priority rather
  than lowering it.
- **Deep sleep (P6-C3).** On the ESP32-C3 only GPIO0..GPIO5 can wake the chip from deep
  sleep. `PIN_BTN_R 2` qualifies; `PIN_BTN_L 10` does not. P6-C3 therefore implements light
  sleep with wake on both buttons, and keeps the deep-sleep path behind
  `PB_PINS_CONFIRMED` so it turns on by itself the day the map is confirmed.
- **Reversal cost stays one commit:** the five `#define`s sit in a single
  `// DECISION D1 PENDING` block in `src/core/config.h`.

## D2 — consequences of choosing ESP-NOW (recorded 2026-09-02)

- **Primary transport is ESP-NOW**, implemented as `networking/transport_espnow.cpp` behind
  the `Transport` interface of spec §59. The game logic never learns which transport
  carried a packet.
- **`FEATURE_BLE 0` in the release build.** The BLE plumbing kept in P2-C7b becomes a
  removal candidate in P7-C1; deleting it recovers 721,632 B of flash and 23,688 B of
  static RAM (measured, audit §14).
- **No stack flip during a link session.** ESP-NOW runs on the same `WIFI_STA` residency the
  §40 scanner already needs, so the single-radio invariant holds without tearing a stack
  down and bringing another up mid-session — which was the weakest point of the BLE path.
- **Same-channel constraint.** ESP-NOW peers must sit on the same Wi-Fi channel. The
  discovery beacon announces the channel and the session pins it to `PB_LINK_CHANNEL`;
  P7-C1 must handle a peer found on another channel by re-tuning before HELLO, not by
  failing.
- **The Bluedroid leak no longer needs measuring** (it was a P7-C1 task only if BLE won).
- **Unchanged by this decision:** the protocol itself. P4-C5 still proves codec, session
  FSM and lockstep battle over an in-process loopback with drop/dup/reorder injection
  before any radio is involved.

### Verified against the installed core before closing (esp32 3.1.1, ESP32-C3)

Checked in `esp_wifi/include/esp_now.h` and `sdkconfig.h` of the toolchain this project
builds with, so P7-C1 starts from facts rather than from the audit's summary:

| Fact | Value | Why it matters |
|---|---|---|
| `ESP_NOW_MAX_DATA_LEN` | 250 B (`= ESP_NOW_MAX_IE_DATA_LEN`) | The §1.4 frame is a 12 B header + payload capped at 200 B, so it fits with 38 B to spare. |
| `ESP_NOW_MAX_TOTAL_PEER_NUM` | 20 | Far above the 8-entry peer table the discovery layer keeps. |
| Receive callback | `esp_now_recv_cb_t(const esp_now_recv_info_t*, const uint8_t*, int)` | `esp_now_recv_info_t.rx_ctrl` is a `wifi_pkt_rx_ctrl_t`, **which carries RSSI** — so the proximity gate the BLE courtship used (RSSI >= -70 dBm) survives unchanged. |
| Send confirmation | `esp_now_register_send_cb` -> `ESP_NOW_SEND_SUCCESS/FAIL` | Gives the per-frame ACK the session FSM needs for its retry/timeout ladder. |
| Prerequisite | `esp_now_init()` after `esp_wifi_start()` | Satisfied by the `WIFI_STA` residency the §40 scanner already brings up; no extra radio state. |
| Broadcast peer | `FF:FF:FF:FF:FF:FF` is a legal peer address | The discovery beacon needs no pairing step. |
| Build config | `CONFIG_ESP_WIFI_ENABLED 1` | ESP-NOW is compiled into the core as shipped; no custom sdkconfig. |
| Arduino wrapper | `libraries/ESP_NOW` exists in the core | Available, but P7-C1 should use the IDF API directly to keep the `Transport` seam free of `String`/callback-object overhead.

## Decisions taken by the plan (plan §0.1, T1-T13)

Recorded here for traceability; each is one commit to reverse.

| # | Decision | Applied in |
|---|---|---|
| T1 | Always-buildable strangler-fig: every commit passes the gate (`tools/check.sh`); removals one subsystem per commit in the flat layout before the `src/` move. | Phase 2 |
| T2 | Reuse over rewrite: file names and `sim_/rd_/gt_/net_/store_` prefixes kept; `render.cpp` and `sim.cpp` stay single TUs; 16 B `Genome` embedded in `PebbleInstance`; milli-point integrator kept. | Phase 2 |
| T3 | ENERGY kept as a fifth, mostly internal care stat. | P2-C7 |
| T4 | Content = generated `constexpr` tables from committed JSON (`tools/content/*.json` → `src/data/*_table.h`), never parsed at runtime. | P4-C1 |
| T5 | Persistence = versioned blob pairs (`<key>0`/`<key>1`, `seq`) + `nvs2` checkpoint partition. | P2-C9 |
| T6 | Radio OFF is the resting state; Wi-Fi requested only by SCAN/LINK/CREATOR screens; SNTP deleted; clock calibrated from the phone or the on-device time screen. | P2-C6 |
| T7 | Local protocol proven on host first over a fault-injecting loopback; lockstep battle with a per-round state hash. | P4-C5 |
| T8 | Input: double-tap retired (tap latency 305 ms → ~25 ms); 5 ms `esp_timer` button sampler; A = TAP_L, B = TAP_R. | P2-C6, P3-C4 |
| T9 | Unknown clock charges zero absence; cooldowns fall back to a per-boot RAM table while uncalibrated. | P2-C6, P5-C2 |
| T10 | `rd_fatal()` replaced by the `ERROR` state in Phase 2. | P2-C11 |
| T11 | Sketch folder renamed `Pebblebol/`, `.ino` → `Pebblebol.ino`. | P2-C1 |
| T12 | UI strings stay Spanish; identifiers, comments and docs English; Spanish docs archived under `docs/legacy/`. | P2-C8, P10-C5 |
| T13 | Roster sprites 24x24, 2 frames, XBM (72 B/frame); creator sprites use the same format. | P9-C3 |

## Measurements to record when hardware exists (P2-C0)

- Real boot free heap vs the author's 179,836 B (`net.cpp:5-7`).
- Real `sendBuffer()` frame time vs the ≈ 24 ms estimate at 400 kHz.
- I2C probe result (0x3C / 0x3D / bus sweep) and panel variant (D5). LED polarity is moot while `PIN_LED 5` points at an unpopulated pin (see D1 consequences).
- USB-CDC port name.
