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
| **D3** | Sketch folder rename and product identity in persisted names | DEFAULT | `sketch_aug30b/` → `Pebblebol/`; `NVS_NS "notta"`, AP prefix `NOTTAMAGOCHI-`, mDNS `nottamagochi.local`. No device has ever run this firmware, so renaming orphans nothing real. | Folder renamed in P2-C1; NVS namespace `"pbbl"` in P2-C9 with a one-shot import of a legacy `"notta"` save; AP prefix `PEBBLEBOL-` in P8-C2; mDNS deleted in P2-C5. | P2-C1, P2-C9 | **CLOSED (P2-C9b, 2026-09-03).** Namespace is `"pbbl"` (`hardware/kv_nvs.h`, `PB_NVS_NAMESPACE`). `kv_begin()` performs a ONE-SHOT import: the raw v1 blobs (`save`, `cfg`, `gl`, `t`) are copied out of `"notta"` under their old key names into `"pbbl"`, where `persistence/migration.cpp` finds them, and `"notta"` is then cleared so a later factory reset cannot resurrect a deleted pet. It runs only when `"pbbl"` holds neither a v2 Box nor an already-imported v1 save, so it can never overwrite live state, and it is idempotent across a power cut in the middle. Verified by `tests/test_compat.cpp` (`compat_migrates_a_v1_save_into_the_live_pet`) and `tests/test_persistence.cpp` (the v1 fixtures). AP prefix and mDNS are unaffected (mDNS was deleted in P2-C5). |
| **D4** | Language of user-facing UI strings | DEFAULT | Spanish `strings_es.h` (439 strings, `StrId` mechanism) vs English. Spec header allows Spanish UI. | Keep Spanish; new BOX/BATTLE/NET/LINK/CREATOR/ERROR/TIME blocks written in Spanish in the same mechanism. An `strings_en.h` twin is a one-file swap later. | P2-C11 | — |
| **D5** | Panel variant SSD1306 vs SH1106 | DEFAULT | `DISPLAY_IS_SH1106` (`config.h:64`); both drivers verified to link. | Keep 0 (SSD1306). Flip only with the panel in front of you (README §3 symptoms). | P2-C0 | — |
| **D6** | Partition table / OTA room (§61) | DEFAULT | `huge_app.csv` = nvs 20 KB, otadata 8 KB, app0 3 MB, spiffs 896 KB unused, coredump 64 KB; no OTA slot. Core 3.1.1 honours a `partitions.csv` in the sketch folder. `initArduino()` erases the whole `nvs` partition on `ESP_ERR_NVS_NO_FREE_PAGES` / `NEW_VERSION_FOUND` before `setup()`. | Custom `Pebblebol/partitions.csv`: `nvs 0x9000 0x5000 · otadata 0xE000 0x2000 · app0 0x10000 0x300000 · nvs2 0x310000 0x10000 · spiffs 0x320000 0xD0000 (reserved) · coredump 0x3F0000 0x10000`. `nvs2` = checkpoint partition. OTA stays a V1 non-goal. | P2-C9d | **CLOSED (P2-C9d, 2026-09-03).** `Pebblebol/partitions.csv` committed exactly as proposed: `nvs 0x9000 0x5000 · otadata 0xE000 0x2000 · app0 0x10000 0x300000 · nvs2 0x310000 0x10000 · spiffs 0x320000 0xD0000 · coredump 0x3F0000 0x10000`, filling 4 MB with no gap. **`PartitionScheme=huge_app` STAYS in the FQBN**, contrary to the plan's wording, and the reason is worth recording: arduino-cli copies a sketch-local `partitions.csv` into the build directory and esptool flashes THAT — so the table in force is ours either way — but `upload.maximum_size`, the ceiling the compile is checked against, comes from the board menu. Dropping the option left the check at the default scheme's 1,310,720 B and failed a 1.87 MB build that fits `app0` perfectly. `huge_app`'s ceiling is 3,145,728 B, which is exactly `app0` in our CSV, so the two agree. `tools/build.sh` now gates both facts: the reported app maximum must equal 3,145,728, and the partition table about to be flashed must contain `nvs2`. |
| **D7** | Creator inactivity grace (§34) | DEFAULT | 120 s vs 300 s. | `ConfigV2.creator_idle_s` default 300, editable in SETTINGS. | P8-C2 | — |
| **D8** | Piezo GPIO (new hardware, V1 baseline §6) | **OPEN — owner confirms when soldering** | A passive ~15 mm piezo joins the V1 BOM. Free, non-strapping GPIOs on this board: 3, 4, 6, 7. GPIO0 is kept for the battery divider (D10). `tone()`/`noTone()` and the LEDC driver are both in the installed core, so no library is needed. | Propose `PIN_PIEZO 3`. The tone engine is written against the macro, so changing it is a one-line edit. | P6-C3 (sleep GPIO states), P10-C2 (tone engine) | — |
| **D9** | Supply architecture for 2×AAA | **CLOSED — 3.3 V boost converter (2026-09-03, owner)** | Alkaline AAA pairs sag from ~2.8 V loaded to ~2.4 V at 80 % discharge, while the core arms brownout at level 7 (~3.0 V, verified `CONFIG_ESP_BROWNOUT_DET_LVL 7`), and the board's LDO cannot step 3.0 V up. A boost module removes the whole problem: the 3.3 V rail stays flat across the discharge curve and ~90 % of cell capacity becomes usable. | Boost module fitted, feeding 3.3 V. The board's always-on power LED is being desoldered (it cost ~48 mAh/day, more than the rest of the device combined). | — | Decided: boost. Two follow-ups it creates are tracked as D11 and D12. |
| **D10** | Battery sense divider on GPIO0 | OPEN | Spec §26 wants NORMAL/LOW/CRITICAL levels. `PIN_VBAT_ADC 0` is already reserved and GPIO0 is ADC1_CH0, so this needs only two resistors. Without it those levels cannot exist and a flat pack corrupts a save instead of warning. | Two resistors; the firmware side lands with the power states in P6-C3. | P6-C3 | — |
| **D11** | Boost module quiescent current | **OPEN — now the single biggest factor in battery life** | With the voltage window solved and the power LED gone, the dominant idle load is whatever the boost module draws doing nothing. Cheap PFM modules range from ~20 µA to ~2 mA, a 100× spread that decides the runtime outright. Budget from ~680 mAh of usable energy at 3.3 V and the spec's 60 min/day profile: 25 µA idle → ~26 days · 200 µA → ~23 days · 1 mA → ~14 days · 2 mA → ~9 days. | Measure it: multimeter in series with the cells, ESP32 in deep sleep, OLED off, radios off. Anything above ~200 µA and the ≥30-day target needs a different module, not firmware work. | Battery-life target | — |
| **D12** | Bulk capacitor on the boost output | OPEN | The ESP32-C3 pulls ~350 mA in Wi-Fi TX. Drawn through a boost from 2.4 V cells that have ~0.3 Ω internal resistance, that transient can collapse the rail and trip exactly the reset the hardware checklist §30 asks about ("Wi-Fi scan does not cause resets"). | A 100–470 µF electrolytic across the boost output, for a few cents. Firmware already helps: the scanner is specified `passive=true` (plan P5-C1), so it listens rather than sending probe requests, which is the cheap half of a scan. | P5-C1 bench test | — |
| **D13** | Light ON by default, and what it does to a hands-off player | **CLOSED — delete the light (2026-09-03, owner)** | `sim_new_pet()` set `PF_LIGHT_ON`, and `sleep_machine()` auto-slept only when it was night AND the light was off. A player who never found the light toggle therefore owned a Pebble that **never slept**: energy pinned at 0 after 16.7 h, the 2 h zero-dwell grace started, and health bled to the 10 % floor at about 64 h. Spec-compliant (section 27: inconveniently unhappy, never destroyed) but the WORST case reachable, and reached by doing nothing — the opposite of what "fun even if you ignore it for hours" is meant to feel like. Measured during P3-C1's retune. | — (superseded) | P3-C5's soak criterion | **The light mechanic is deleted, not defaulted (P3-C2b).** The owner's judgement was that the switch "doesn't add anything": neither of the two one-line fixes was taken. Sleep now follows an approximated daylight table and a player who insists can wake the creature. See the consequences below the table. |

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
| T2 note | The `store_*` prefix of T2 did not survive P2-C9b: `storage.cpp` was two unrelated modules in one file and split into `hardware/kv_nvs.cpp` (`kv_*`), `hardware/boot.cpp` (`boot_*`) and the save policy in `persistence/save_manager.cpp` (`save_*`). | P2-C9b |
| T6 | Radio OFF is the resting state; Wi-Fi requested only by SCAN/LINK/CREATOR screens; SNTP deleted; clock calibrated from the phone or the on-device time screen. | P2-C6 |
| T7 | Local protocol proven on host first over a fault-injecting loopback; lockstep battle with a per-round state hash. | P4-C5 |
| T8 | Input: double-tap retired (tap latency 305 ms → ~25 ms); 5 ms `esp_timer` button sampler; A = TAP_L, B = TAP_R. | P2-C6, P3-C4 |
| T9 | Unknown clock charges zero absence; cooldowns fall back to a per-boot RAM table while uncalibrated. | P2-C6, P5-C2 |
| T10 | `rd_fatal()` replaced by the `ERROR` state in Phase 2. | P2-C11 |
| T11 | Sketch folder renamed `Pebblebol/`, `.ino` → `Pebblebol.ino`. | P2-C1 |
| T12 | UI strings stay Spanish; identifiers, comments and docs English; Spanish docs archived under `docs/legacy/`. | P2-C8, P10-C5 |
| T13 | Roster sprites 24x24, 2 frames, XBM (72 B/frame); creator sprites use the same format. | P9-C3 |

## Measured, not estimated (running record)

| What | Measurement | Consequence |
|---|---|---|
| Worst-case offline catch-up (`sim_catch_up_ex`, 400 days) | **8.4 ms** on the host at `-O1`; **~421 ms** projected on a 160 MHz ESP32-C3 using the plan's 50x factor | Comfortably under the 1 s threshold and 12x under the 5 s Task WDT, so catch-up does **not** need to be made resumable. Plan risk 25 closed by measurement (P2-C10, 2026-09-03). |

## Phase-2 exit (P2-C12, 2026-09-03)

**Variant matrix.** `tools/build_matrix.sh` compiles all seven feature variants with
`--warnings all` and fails on any warning pointing into the sketch. Result at the
`v0.2.0-core` tag, every variant at **0 project warnings**:

| Variant | Overrides | Flash (B) | Static RAM (B) |
|---|---|---|---|
| baseline | — | 1,881,376 | 70,276 |
| no-ble | `FEATURE_BLE=0` | 1,168,978 | 46,780 |
| no-web | `FEATURE_WEB=0` | 1,251,972 | 49,332 |
| no-god | `GOD_MODE_ENABLED=0` | 1,869,150 | 70,116 |
| sh1106 | `DISPLAY_IS_SH1106=1` | 1,881,376 | 70,276 |
| all-off | every `FEATURE_*`=0 + `GOD_MODE_ENABLED=0` | 480,766 | 22,024 |
| **release** | `GOD_MODE_ENABLED=0 FEATURE_BLE=0` (D2) | **1,156,866** | **46,620** |

Baseline flash is down from the 2,105,548 B measured on the source commit `b53cfe4`
(−224,172 B) and the release configuration is 1,156,866 B, i.e. 37 % of the 3,145,728 B
`app0` slot. `sh1106` compiles to the same size as `baseline` because U8g2's two
`_F_HW_I2C` classes differ only in which init sequence is linked and those are the same
length; the variant still proves the alternate driver builds and links (D5).

**D3 outcome.** Closed in P2-C9b and recorded in the table above: the persisted namespace
is `"pbbl"`, `kv_begin()` performs the one-shot import of a legacy `"notta"` save, the
folder rename landed in P2-C1 and mDNS was deleted in P2-C5. Nothing about D3 is left open
at the phase boundary; the AP prefix (`PEBBLEBOL-`) is the only remaining piece and it is
scheduled for P8-C2, where the AP itself is built.

**Heap trend instrumentation.** `dev/godmode.cpp` now prints one line on Serial every
`GOD_HEAP_PERIOD_MS` (60,000 ms):

```
DIAG#,heap,uptime_s,free_b,min_free_b
DIAG,heap,<uptime_s>,<ESP.getFreeHeap()>,<ESP.getMinFreeHeap()>
```

It is deliberately outside the console's `#if GOD_MODE_ENABLED`: the soak it exists for is
"one hour on HOME with the radio OFF", which is the configuration where god mode is off and
the release build is what is flashed, so the line must appear there too. `god_begin()`
back-dates the timer by one period so the first `god_service()` emits the t=0 origin
sample. Cost: 262 B of flash and 8 B of static RAM in the release variant.

**Soak NOT run — no hardware.** The 1 h heap soak the plan asks for cannot be executed:
this project has still never run on a physical board (no ESP32-C3, no panel, no cells), and
free heap on the host has no relationship to the IDF allocator's. There is no honest way to
substitute a host measurement here the way P2-C10 substituted one for the catch-up budget,
because the quantity under test *is* the device allocator. The Phase-2 exit is therefore
recorded as: instrumentation in place and building in every variant, measurement pending
first flash. The soak is listed below with the other first-hardware measurements.

## Measurements to record when hardware exists (P2-C0)

- **1 h heap soak (P2-C12).** Boot to HOME, radio OFF, god mode OFF, leave it for an hour
  and capture the `DIAG,heap,` lines. Pass = `free_b` flat within allocator noise and
  `min_free_b` reaching a floor early and then not falling. A `min_free_b` that keeps
  sliding is a leak; the per-frame render path and the 1 Hz save policy are the first two
  suspects.
- Real boot free heap vs the author's 179,836 B (`net.cpp:5-7`).
- Real `sendBuffer()` frame time vs the ≈ 24 ms estimate at 400 kHz.
- I2C probe result (0x3C / 0x3D / bus sweep) and panel variant (D5). LED polarity is moot while `PIN_LED 5` points at an unpopulated pin (see D1 consequences).
- USB-CDC port name.

## D13 — consequences of deleting the light (recorded 2026-09-03)

- **There is no light any more.** `PF_LIGHT_ON`, `ACT_LIGHT_TOGGLE`, `MULT_LIGHT_ON_SLEEP`,
  the CARE row, the SETTINGS row and the five `STR_*_LIGHT` strings are gone. Bit 0x0004
  of the live flag word and bit 0x10 of `PebbleInstance.status` (`PBS_RESERVED_LIGHT`) are
  **reserved**: never reused, never written, and no other bit moved — the 128 B layout is
  pinned by `offsetof` asserts and the save schema is versioned. The v1 migration DROPS the
  old light bit rather than carrying it into a v2 save.
- **Sleep follows the sun, approximately and openly.** `data/balance.h` §5 holds twelve
  sunrise/sunset pairs in minutes from local midnight, interpolated by day of year in
  `game/daylight.cpp`. Bedtime is sunset + 90 min; morning is sunrise. In mid-January that
  is 19:30 → 08:35 and in mid-July 23:10 → 07:00.
- **It is an approximation and the header says so.** A true sunrise needs a latitude and
  there is none in this firmware — it went with the weather module, and spec §44 is explicit
  that the Wi-Fi scanner is a sensor, not a geolocator. The table describes ~40° N
  (peninsular Spain, where the default `CFG_TZ_STRING` points) in LOCAL OFFICIAL time: the
  TZ string has already applied daylight saving, so nothing downstream may apply it twice.
  A player at another latitude sees a drift. That cost was accepted with the mechanic.
- **Insistence wakes it, and costs nothing.** The first gesture against a sleeping pebble
  does not act; three inside ten seconds wake it and the third one then lands. The counter
  decays, so taps hours apart never accumulate. Five quiet minutes put it back to sleep
  while the window is still open. No happiness or health is charged for waking — §27 forbids
  punishing the player, and the energy that drains while awake is cost enough.
- **One rule changed at the boundary, deliberately.** "Wake as soon as energy is full" is
  now a DAYTIME rule only. Energy refills in 5 h and a winter night is 13 h long, so at
  night the old rule woke the creature at about 00:30 and the window put it straight back —
  a `SIM_EV_SLEEP`/`SIM_EV_WAKE` pair every substep until dawn. Nothing else about the decay
  inside the window moved: P3-C1's rates are untouched.
- **P3-C5's soak criterion is unblocked for energy** and still cannot hold for hunger and
  happiness, which empty in about a day of total neglect by design. The P3-C5 bullet now
  says so.

## D14 — the species roster stops at family 1 (recorded 2026-09-03, P3-C3)

- **What shipped.** `data/species_table.h` grew from one placeholder row to the THREE REAL
  rows of family 1 — Paketo (id 1, base stage), Fragmar (2, mid), Rafagón (3, final) — copied
  verbatim from the verified content pack, plus `data/evolution_table.h` with the two rules
  that join them (`{1,2,8}` and `{2,3,18}`, both `EVOC_NONE`).
- **Why it stops there.** The roster's contiguity guard (`id == index + 1`) makes it
  **all-or-nothing in whole family blocks**: there is no way to ship family 4 without also
  shipping families 2 and 3. And family 4 is the first one that carries a real evolution
  CONDITION, so "add one family with a condition in it" is in fact "pull P4-C1's entire
  12-species roster, its attack table and its generator forward into P3-C3". The conditions
  are covered by `tests/test_evolution.cpp` instead, which drives all five `EvoCond` kinds
  through hand-built rules — including the case no shipped rule can reach, an input the
  build cannot supply, which must REFUSE.
- **The rows are final, not placeholder.** They are the ids, stats, moves and strings the
  content pack already holds, so P4-C1's `tools/gen_content.py` will emit them byte for byte
  and nothing recorded against ids 1..3 in Phase 3 has to be re-recorded. Two fields inside
  them are still placeholders and say so in the header: `moves` names attack ids that no
  table resolves until P4-C1 (deliberately — they are the FINAL ids, so P4-C1 adds the table
  and the cross-reference guard without touching these rows), and `sprite_id` is placeholder
  art until the P10 art pass.
- **Consequence: the starter's base_hp moved from 5 to 4**, so a fresh Pebble's derived
  `hp_max` at level 1 is 19 rather than 21. Everything reads it through `species_get()`, so
  no test hard-codes it — but it did expose a real defect in `ui/pet_view.cpp`, where the
  `> 100` clamp on `hp_pct` ran AFTER the narrowing cast and a corrupt `hp_cur` of 60,000
  wrapped to 5 % instead of clamping to 100 %. Fixed in the same commit.
- **Consequence: `persistence/migration.cpp`'s legacy family map is now visibly wrong** and
  is commented as such. It maps the eight v1 families onto ids 1..8; ids 2 and 3 are now the
  mid and final STAGES of family 1 rather than other families, and 4..8 still resolve to
  nothing. P4-C1 must land every legacy family on the BASE-stage species of its family. Not
  fixed here: with only one family in the roster there is no correct answer to move it to.
- **Correction to the P3-C3 commit message.** It says "Six strings added to
  `core/strings_es.h`, appended so no existing id moves". That is wrong on both counts:
  **seven** strings were added, and one of them — `STR_CF_EVOLVE`, the confirmation
  question — went in mid-enum after `STR_CF_WIPE2` so it would sit with the other
  confirmations, which shifted every `StrId` from `STR_DG_REFLEX` to `STR_PHASE_10` by one.
  It is harmless: `ES[]` moved in lockstep, and no `StrId` is persisted, sent on the wire or
  written into a golden (checked against `save_schema.h`, `protocol.h` and `tests/golden/`).
  But a future reader must not act on "no existing id moves" — it is not a property this
  file has. The rule that DOES hold is the weaker one: a `StrId` is a compile-time index and
  nothing outside the firmware image may store it.

- **Section 18's fourth item, sound, is NOT here.** The plan sequences `hardware/audio.{h,cpp}`
  in P6, and the ceremony's phase edges in `ui/ceremony.cpp` are where those calls slot in
  (one per phase transition, armed exactly once, next to `rd_flash()` / `rd_shake()`). No
  tone engine was invented in P3.

## Phase-3 exit (P3-C5, 2026-09-03)

**Variant matrix.** `tools/build_matrix.sh` compiles all seven feature variants with
`--warnings all` and fails on any warning pointing into the sketch. Result at the
`v0.3.0-pet` tag, every variant at **0 project warnings**:

| Variant | Overrides | Flash (B) | Static RAM (B) | Δ flash vs 0.2.0-core |
|---|---|---|---|---|
| baseline | — | 1,893,072 | 70,348 | +11,696 |
| no-ble | `FEATURE_BLE=0` | 1,180,684 | 46,868 | +11,706 |
| no-web | `FEATURE_WEB=0` | 1,263,616 | 49,420 | +11,644 |
| no-god | `GOD_MODE_ENABLED=0` | 1,881,054 | 70,204 | +11,904 |
| sh1106 | `DISPLAY_IS_SH1106=1` | 1,893,072 | 70,348 | +11,696 |
| all-off | every `FEATURE_*`=0 + `GOD_MODE_ENABLED=0` | 492,610 | 22,112 | +11,844 |
| **release** | `GOD_MODE_ENABLED=0 FEATURE_BLE=0` (D2) | **1,168,772** | **46,708** | +11,906 |

A whole phase — XP and levels, the daylight sleep machine, data-driven evolution, the
minigame framework and six games — cost **11,696 B of flash and 72 B of static RAM** on the
baseline. Caps are `GATE_FLASH_MAX` 2,400,000 and `GATE_GLOBALS_MAX` 90,000 (`config.h`), so
the baseline sits at 79 % of the flash cap and the release build at 37 % of the 3,145,728 B
`app0` slot, where it was in phase 2.

**Host suite.** 21 binaries, **327 tests, 350,157 checks** (phase 2 shipped 17 / 205 /
185,482). Phase 3 added 4 binaries, 122 tests and 164,675 checks.

### The soak criterion, restated and measured

The phase-3 exit criterion the plan carried in was *"no stat pinned at 0 for more than 6
simulated hours of neglect"*. Before D13 closed that was unreachable — a pebble whose light
nobody switched off never slept, and energy pinned at 0 for ever. It is reachable now, but
only for the one stat the simulation refills by itself, so it is restated:

> **Fourteen simulated days of total neglect** — a hatched pebble, a trustworthy clock, and
> not one action for a fortnight, recording the longest CONTINUOUS run each stat spends at 0.
> **ENERGY**, the one core stat the simulation restores on its own, **is never at 0 for more
> than 6 continuous simulated hours, and is back above 90 % at every sunrise.**
> **HUNGER, HAPPINESS and CLEANLINESS** are at 0 for most of the fortnight, and are meant to
> be: they are the ones only the player can refill, and refilling them is what the player is
> for. **HEALTH** is what the floor protects — it never reaches 0 at all, and never falls
> below `HEALTH_FLOOR_PCT`.

It is asserted, not merely benched:
`tests/test_care.cpp` §16 `care_a_fortnight_of_neglect_only_pins_the_stats_the_player_owns`
runs the whole fortnight (20,160 minutes) in about a millisecond, so it is a gate rather
than something somebody has to remember to drive on a device that has never been flashed.

Measured on that fixture (seed `0x5EED0C7A`, 10:00, day 100, 336 h):

| Stat | First reaches 0 | **Longest run at 0** | Total at 0 | Ends at |
|---|---|---|---|---|
| hunger | 30.8 h | **305.2 h** | 305.2 h | 0 % |
| happiness | 27.5 h | **160.2 h** | 296.8 h | 40 % |
| **energy** | 34.7 h | **2.1 h** | 24.6 h | 79 % |
| cleanliness | 24.7 h | **311.4 h** | 311.4 h | 0 % |
| health | never | **0.0 h** | 0.0 h | 10 % |

14 wake-ups in 14 nights, every one of them above 90 % energy (99 % each time). The pebble
is asleep for 124.6 h of the 336, sick for 329.9 h, and sits at the 10 % health floor for
267.4 h — inconveniently unhappy, exactly as spec §27 asks, and still alive.

Robustness, measured outside the suite over **480 fortnights** (40 genesis genomes × the 12
month anchors):

| Stat | min | mean | max | runs ever at 0 | runs with a run > 6 h |
|---|---|---|---|---|---|
| hunger | 292.48 h | 307.12 h | 313.08 h | 480/480 | 480/480 |
| happiness | 155.85 h | 158.98 h | 160.47 h | 480/480 | 480/480 |
| **energy** | 0.00 h | 0.49 h | **3.87 h** | 125/480 | **0/480** |
| cleanliness | 307.57 h | 310.20 h | 312.55 h | 480/480 | 480/480 |
| **health** | 0.00 h | 0.00 h | **0.00 h** | **0/480** | 0/480 |

Hard worst case: forcing the metabolism gene to its maximum 15 (×1.50) — which genesis
cannot roll, it clamps to 4..12 — over all 366 start days gives energy a longest run of
**5.40 h**, still under 6 (worst 14-day start measured at day 151, which straddles the
solstice). The closed form agrees: the longest awake window is 16.50 h — a 7.50 h midsummer
night — minus 100000/(6000×1.50) = 11.11 h of energy, i.e. 5.39 h. The
6 h number is not arbitrary — it is where the night stops being long enough to pay for the
day, so `CARE_ENERGY_ASLEEP_MPH` dropped much below 13,300, `SLEEP_AFTER_DUSK_MIN` pushed
past ~3 h, or `CARE_DECAY_MPH[CARE_ENERGY]` raised all break it.

Two things the criterion could not honestly leave out:

- **CLEANLINESS.** Restating this as "hunger and happiness" and stopping, as the plan's own
  draft did, is factually wrong: cleanliness is the WORST of the five (311.4 h) and the
  FIRST to bottom out (24.7 h, before hunger's 30.8 h), because every poop adds
  `CARE_HYGIENE_POOP_MPH` on top of the base rate.
- **Happiness does not stay empty, and no player is involved.** `events_step()` pays
  `EVENT_VISITA_HAPPINESS` at 72 h of age and `EVENT_BIRTHDAY_HAPPY` every 168 h, which is
  why its longest run is ~160 h rather than ~300 and why it finishes the fortnight at 40 %.
  Hunger and cleanliness genuinely do stay empty.

**Scoped to a valid clock on purpose.** With `clock_valid = 0` there is no night —
`daylight_is_night()` is not asked without a trustworthy clock — the pebble never sleeps,
and energy sits at 0 for **322.78 h of the same 336**, with 0.00 h asleep. That is D13
reproduced exactly, on a device that has never had SNTP and learns the date from a human.
It is the documented cost of the mechanic, not a defect in `game/sim.cpp`, but a criterion
that does not say "with a valid clock" is simply false of the shipped simulation.

### A real integrator defect, found by the chunking test the plan asked for

The plan asked for one thing here: add 1 s chunking to
`care_one_hour_of_catch_up_is_the_same_however_it_is_chunked`, because the three cases it
had (3600, 60×60, 6×600) are the identical sub-step sequence and cannot disagree. The hour
does match on all 128 bytes at every chunk size, and the test says so and says why. One hour
later it does not, and the cause was not rounding:

**`poop_step()` scaled its advance by `MULT_SLEEP` (×0.35) and truncated it every sub-step
with no carry.** `(1 * 350) / 1000` is 0 — and **1 s is the step the live device runs**
(`app/app.cpp` → `sim_step_seconds()`, which is 1 outside god mode). So a pebble asleep on
real hardware never advanced its poop timer at all and **could not poop overnight, ever**,
while an offline catch-up over the same night (60 s sub-steps, an exact 21) produced two.
Same night, same pebble, two different models — and `poop_step()`'s own comment, "an 8 h
night produces 5 poops… sleeping the pet before bed is a real strategy", was accidentally
absolute on-device: sleeping the pet was not a discount, it was total immunity.

Measured poops over 8 h from 23:00, by the step size the sim is driven at:

| `sim_tick(n)` | 3600 | 600 | 60 | 30 | 20 | 10 | 5 | 2 | 1 |
|---|---|---|---|---|---|---|---|---|---|
| before | 2 | 2 | 2 | 2 | 2 | 2 | 1 | 0 | **0** |
| after | 2 | 2 | 2 | 2 | 2 | 2 | 2 | 2 | **2** |

`poop_step()` carries its remainder now, the way `accum()` always has. `stage_step()`'s
`g.acc_stage = 0` became `-=` for the same reason — the same leak whenever `dt` does not
divide the period, harmless at dt ∈ {1, 60} but the same class of bug.

**Nothing at dt = 60 moved**, because 60 × 350 / 1000 is exact, so `golden/care_v2.txt` was
NOT re-recorded and no existing test changed. Cost: 32 B of flash, 0 B of static RAM.

What remains is a bound, not a bug, and the test says that plainly: a rate CHANGE — a poop
arriving, the loneliness multiplier turning on — is evaluated on the `SIM_SUBSTEP_S` grid,
so a finer step charges the new rate up to one sub-step early. The budget is one sub-step of
the largest rate change in the model, **25 milli**; measured 9 milli over 2 awake hours
(1 poop), 25 over 4 (3 poops), 11 over an 8 h night (2 poops at ×0.35 plus the loneliness
edge). A displayed percent is 1,000 milli. `SIM_SUBSTEP_S` moved to `game/sim.h` so a test
can state that bound. Mutation-checked: reverting the carry fails 6 checks across the two
new cases.

### Two overstated claims in the phase-3 audit trail, corrected

- **P3-C3's "`test_pet_view` proves the body the view describes really changes" is not true
  of the shipping build**, and the box has been split so that the false half is now an OPEN
  item. `pet_view_fill()` — the only path that feeds `evo_state`'s stage bits to
  `sprite_form_of()`, and the one the test drives — **has no caller in `Pebblebol/src`**. The
  firmware fills the active pet through `pet_view_fill_sim()`, whose `form` comes from the
  genome, the minor form and the life stage, none of which an evolution moves;
  `sprite_lookup_pose()` never sees `species_id`, `SpeciesDef.sprite_id` is read by nothing,
  and `STR_SPC_NAME_1..3` are displayed nowhere. A player who confirms an evolution today
  watches 4.5 s of ceremony and gets back the same body, the same name and a different
  `hp_max`. The MODEL half of P3-C3 is genuinely complete; §18's visual transformation is
  not, and it is carried to P4-C1 as a sixth obligation, where the roster grows to twelve
  species that would otherwise all wear their genome's body.
- **P3-C4's "snapshots for each game" was true of four of six.** `ping_draw.cpp` and
  `sequence_draw.cpp` were compiled only by the firmware build and never rendered on the
  host. Both objects are in `MG_FRAME_OBJS` now with four goldens — `mg_ping_wait` /
  `mg_ping_lit`, `mg_sequence_show` / `mg_sequence_answer` — so all six draw halves render at
  128×64 with `fb_oob() == 0`, and the claim is true as written. The re-record produced only
  those four files; every existing golden came back byte-identical.
- **P3-C4's "DELETE's choice proof now DERIVES its '3 indices apart' from
  `DEL_LIFE_MS`/`DEL_SPAWN_MS`" describes something that is not in the tree.** The test names
  neither macro; it measures the maximum simultaneous corrupt count over 32 seeds and asserts
  `>= 2`. That proves the property directly rather than deriving the schedule that implies
  it — stronger than the description, but not the description. Corrected in place.

### Still not run on hardware

Unchanged from the phase-2 exit and worth repeating at a tag: **this firmware has never run
on a physical board.** Every number above is a host measurement or a compile result. The
plan's god-mode ×3600 soak (`dev/godmode.cpp`, the DIAG CSV) stays listed with the other
first-flash measurements; the criterion it was meant to check now lives in the suite, where
it runs on every gate instead of once, by hand, on a board nobody has.
