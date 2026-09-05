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
| **D2** | Peer-session transport: BLE vs Wi-Fi (ESP-NOW) | **CLOSED — ESP-NOW (2026-09-02, owner). BUILT IN PHASE 7 AND UNPROVEN ON HARDWARE: the BLE deletion the decision authorises is still pending a two-board bench test — see "Phase-7 exit" at the end of this file.** | The legacy BLE advert carries 19 B/frame and cannot carry the §15 message set; GATT was rejected by the original author for stability; each BLE bring-up burns one of 32 sessions with a claimed ~672 B Bluedroid leak; BLE costs 721,632 B flash / 23,688 B static RAM **as measured in phase 1 — re-measured at the phase-4 exit as 712,466 B / 23,504 B (baseline minus `no-ble`); the phase-1 pair is kept because it is what the owner decided on, and the correction is worked through under "D2 — consequences"**. ESP-NOW ships in the core (250 B/frame, unicast + send-callback ACK) and needs the same `WIFI_STA` residency the §40 scanner already requires. | ESP-NOW is the transport, behind the §59 `Transport` seam. `FEATURE_BLE 0` in the release build. | P7-C1 | Decided: ESP-NOW. See the consequences below the table. |
| **D3** | Sketch folder rename and product identity in persisted names | DEFAULT | `sketch_aug30b/` → `Pebblebol/`; `NVS_NS "notta"`, AP prefix `NOTTAMAGOCHI-`, mDNS `nottamagochi.local`. No device has ever run this firmware, so renaming orphans nothing real. | Folder renamed in P2-C1; NVS namespace `"pbbl"` in P2-C9 with a one-shot import of a legacy `"notta"` save; AP prefix `PEBBLEBOL-` in P8-C2; mDNS deleted in P2-C5. | P2-C1, P2-C9 | **CLOSED (P2-C9b, 2026-09-03).** Namespace is `"pbbl"` (`hardware/kv_nvs.h`, `PB_NVS_NAMESPACE`). `kv_begin()` performs a ONE-SHOT import: the raw v1 blobs (`save`, `cfg`, `gl`, `t`) are copied out of `"notta"` under their old key names into `"pbbl"`, where `persistence/migration.cpp` finds them, and `"notta"` is then cleared so a later factory reset cannot resurrect a deleted pet. It runs only when `"pbbl"` holds neither a v2 Box nor an already-imported v1 save, so it can never overwrite live state, and it is idempotent across a power cut in the middle. Verified by `tests/test_compat.cpp` (`compat_migrates_a_v1_save_into_the_live_pet`) and `tests/test_persistence.cpp` (the v1 fixtures). AP prefix and mDNS are unaffected (mDNS was deleted in P2-C5). |
| **D4** | Language of user-facing UI strings | DEFAULT | Spanish `strings_es.h` (439 strings, `StrId` mechanism) vs English. Spec header allows Spanish UI. | Keep Spanish; new BOX/BATTLE/NET/LINK/CREATOR/ERROR/TIME blocks written in Spanish in the same mechanism. An `strings_en.h` twin is a one-file swap later. | P2-C11 | — |
| **D5** | Panel variant SSD1306 vs SH1106 | DEFAULT | `DISPLAY_IS_SH1106` (`config.h:64`); both drivers verified to link. | Keep 0 (SSD1306). Flip only with the panel in front of you (README §3 symptoms). | P2-C0 | — |
| **D6** | Partition table / OTA room (§61) | DEFAULT | `huge_app.csv` = nvs 20 KB, otadata 8 KB, app0 3 MB, spiffs 896 KB unused, coredump 64 KB; no OTA slot. Core 3.1.1 honours a `partitions.csv` in the sketch folder. `initArduino()` erases the whole `nvs` partition on `ESP_ERR_NVS_NO_FREE_PAGES` / `NEW_VERSION_FOUND` before `setup()`. | Custom `Pebblebol/partitions.csv`: `nvs 0x9000 0x5000 · otadata 0xE000 0x2000 · app0 0x10000 0x300000 · nvs2 0x310000 0x10000 · spiffs 0x320000 0xD0000 (reserved) · coredump 0x3F0000 0x10000`. `nvs2` = checkpoint partition. OTA stays a V1 non-goal. | P2-C9d | **CLOSED (P2-C9d, 2026-09-03).** `Pebblebol/partitions.csv` committed exactly as proposed: `nvs 0x9000 0x5000 · otadata 0xE000 0x2000 · app0 0x10000 0x300000 · nvs2 0x310000 0x10000 · spiffs 0x320000 0xD0000 · coredump 0x3F0000 0x10000`, filling 4 MB with no gap. **`PartitionScheme=huge_app` STAYS in the FQBN**, contrary to the plan's wording, and the reason is worth recording: arduino-cli copies a sketch-local `partitions.csv` into the build directory and esptool flashes THAT — so the table in force is ours either way — but `upload.maximum_size`, the ceiling the compile is checked against, comes from the board menu. Dropping the option left the check at the default scheme's 1,310,720 B and failed a 1.87 MB build that fits `app0` perfectly. `huge_app`'s ceiling is 3,145,728 B, which is exactly `app0` in our CSV, so the two agree. `tools/build.sh` now gates both facts: the reported app maximum must equal 3,145,728, and the partition table about to be flashed must contain `nvs2`. |
| **D7** | Creator inactivity grace (§34) | DEFAULT | 120 s vs 300 s. | `ConfigV2.creator_idle_s` default 300, editable in SETTINGS. | P8-C2 | — |
| **D8** | Piezo GPIO (new hardware, V1 baseline §6) | **STILL OPEN — owner confirms when soldering** | A passive ~15 mm piezo joins the V1 BOM. Free, non-strapping GPIOs on this board: 3, 4, 6, 7. GPIO0 is kept for the battery divider (D10). `tone()`/`noTone()` and the LEDC driver are both in the installed core, so no library is needed. | `PIN_PIEZO 3` is now **committed as the proposal** in `core/config.h` §2 (P6-C1). The tone engine is written against the macro, so changing it is a one-line edit, and `tools/check.sh` fails any other file that defines a `PIN_` macro. | P6-C3 (sleep GPIO states); the tone engine landed EARLY, in **P6-C1**, not P10-C2 | **The engine shipped against an open decision — see "D8 — the tone engine landed…" below.** The pin itself is unconfirmed and nothing has been heard. |
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
- **Deep sleep (P6-C3) — BUILT, AND THE ANSWER IS LIGHT SLEEP ON BOTH BUTTONS.** On the
  ESP32-C3 only GPIO0..GPIO5 can wake the chip from deep sleep. `PIN_BTN_R 2` qualifies;
  `PIN_BTN_L 10` does not. P6-C3 therefore implements light sleep with wake on both
  buttons. Verified against the installed core rather than from memory:
  `SOC_GPIO_DEEP_SLEEP_WAKE_VALID_GPIO_MASK` is `BIT0|…|BIT5` and `SOC_RTCIO_PIN_COUNT` is
  0, so `esp_deep_sleep_enable_gpio_wakeup()` returns `ESP_ERR_INVALID_ARG` for anything
  outside those six pads and ext0/ext1 are not compiled for this chip at all. From *light*
  sleep the digital pad domain stays powered and `gpio_wakeup_enable()` accepts every GPIO,
  which is why both buttons work there and only there.

  **Three consequences the owner should hear in these words.**
  1. **A deep sleep on this map would leave the left button dead.** Not slow, not
     unreliable — physically incapable of waking the device. That is a product decision
     dressed as a pin decision, and it is the reason the ladder stops at light sleep.
  2. **It would also make every wake a reset taken with a finger on GPIO2**, which
     `config.h` §2 lists as a strapping pin ("never wire a button to them" — and
     `PIN_BTN_R` is 2, which is why the dormant `PB_PINS_CONFIRMED` assert would fail
     today). Whether the C3 re-samples GPIO2 into a different boot mode at a
     deep-sleep wake could not be established from any installed header, so it is an
     **unresolved bench risk**, not an asserted failure. Deep sleep on this map must not be
     trusted until somebody checks it on a board.
  3. **The battery cost of choosing light is small next to D11.** On D11's own published
     model (680 mAh usable, 60 min/day active) light sleep instead of deep costs roughly
     **2.7 days out of ~26** at a 20 µA boost module and **0.8 out of ~14** at a mediocre
     one — while D11's own unmeasured 100× spread swings the same answer from 26 days to 9.
     *The boost module nobody has measured matters about ten times more than deep-vs-light.*
     These are arithmetic on D11's four published points, not measurements; ~5 µA deep
     sleep is this repo's own citation and the ~130 µA light-sleep figure is a vendor
     number that appears nowhere in this repo and is the weakest input.

  **The earlier wording of this bullet said the deep-sleep path would be kept "behind
  `PB_PINS_CONFIRMED` so it turns on by itself the day the map is confirmed". P6-C3 did
  not do that, deliberately, and this records why.** `PB_PINS_CONFIRMED` is not the
  condition — a *confirmed* map with `PIN_BTN_L 10` still cannot deep-sleep on two buttons,
  and a confirmed map with `PIN_BTN_R 2` fails the strapping assert. The real condition is
  "are both buttons inside the wake mask", so `hardware/power.h` computes exactly that as
  `PWR_DEEP_WAKE_BOTH_BUTTONS` and carries a `static_assert(!PWR_DEEP_WAKE_BOTH_BUTTONS)`
  that **fails the build**, with a message naming the choice, the day the pin map changes.
  A compiled-out deep-sleep branch would have been a path nobody had ever built, switching
  itself on unattended; a red line puts a human in front of it. Measured: editing
  `PIN_BTN_L` to 4 stops the build with that message.
- **Battery sense (D10) is still not in the firmware after P6-C3**, and the P6-C3 row of
  the D10 line above should be read with that. `PIN_VBAT_ADC 0` points at a divider that is
  not populated, so a NORMAL/LOW/CRITICAL reading would be a made-up number on a floating
  pin. The power ladder needs no battery input to work — it runs on idle time — so nothing
  was invented to fill the gap.
- **WHAT THE DEFERRAL COST PHASE 6, IN ONE PLACE (recorded at the phase-6 exit, P6-C4).**
  Nothing else in the tree states it as a product fact, so it is stated here:

  | | GPIO | wakes from LIGHT sleep | wakes from DEEP sleep |
  |---|---|---|---|
  | `PIN_BTN_L` (left / DOWN) | 10 | **yes** | **NO — outside the C3's GPIO0..5 wake domain** |
  | `PIN_BTN_R` (right / A) | 2 | **yes** | yes, but 2 is a strapping pin and every deep wake is a reset |

  So the deepest rung phase 6 could build is a **light sleep**, and that is a direct,
  product-visible consequence of the deferral rather than a tuning choice. What it costs is
  ~2.7 days of ~26 on D11's own model (the arithmetic is three bullets up) and roughly
  **9.6 KB of flash** for ESP-IDF's `esp_sleep` machinery, which a deep-sleep rung would
  have spent anyway. What it BUYS is that both buttons work: the alternative on this map is
  a device the left button cannot wake. **Nothing has run on hardware**, so the wake itself
  — that `esp_light_sleep_start()` really returns on a `GPIO_INTR_LOW_LEVEL` from GPIO10
  and GPIO2 with the buttons' internal pull-ups — is still a bench item.

- **D8 (piezo GPIO) IS STILL OPEN AFTER PHASE 6, and `PIN_PIEZO 3` is a PROPOSAL.** P6-C1
  shipped the tone engine against the macro, so the pin is a one-line change; `PIN_PIEZO`
  is defined once, in `core/config.h`, and `tools/check.sh` fails any `#define PIN_` that
  appears anywhere else. `PB_PINS_CONFIRMED` is still not defined anywhere in the tree, so
  the guard `static_assert`s stay dormant. **No tone has ever been heard.**

  One thing for the owner to hear before the soldering iron comes out, because it couples
  D8 to the table above: **GPIO3 is one of the six pads that can wake this chip from deep
  sleep**, and the proposal spends it on an output. If the piezo moved to a pad outside
  GPIO0..5 and a future D1 put the *left* button inside it, a two-button deep sleep would
  become possible for the first time. That is the owner's call and P6-C4 did not make it —
  it is recorded so the pin map and the sleep design are decided together rather than
  twice.

- **Reversal cost stays one commit:** the five `#define`s sit in a single
  `// DECISION D1 PENDING` block in `src/core/config.h`.

## D2 — consequences of choosing ESP-NOW (recorded 2026-09-02)

- **Primary transport is ESP-NOW**, implemented as `networking/transport_espnow.cpp` behind
  the `Transport` interface of spec §59. The game logic never learns which transport
  carried a packet.
- **`FEATURE_BLE 0` in the release build.** The BLE plumbing kept in P2-C7b becomes a
  removal candidate in P7-C1; compiling it out recovers **712,618 B of flash and 23,496 B
  of static RAM**, re-measured at the **P7-C6 exit** as baseline minus `no-ble`
  (1,994,460 − 1,281,842 and 80,492 − 56,996). *(This line said 721,632 / 23,688 at phase 1
  and 712,466 / 23,504 at the P4-C6 exit. The number moves with every commit that touches the
  BLE-guarded code, so it is dated here rather than left to look permanent — the drift over
  three phases was 152 B of flash and 8 B of globals.)*
  **AND THE PLUMBING IS STILL HERE AT v0.7.0-social.** `ble_social.{h,cpp}`, `FEATURE_BLE`
  and `BlePeerInfo` are byte-identical across all four phase-7 commits, because P7-C1's own
  bullet gates that deletion on ESP-NOW having linked two real boards and ESP-NOW has never
  run on one. The exact bench test that unblocks it is written out at the end of this file.
- **No stack flip during a link session.** ESP-NOW runs on the same `WIFI_STA` residency the
  §40 scanner already needs, so the single-radio invariant holds without tearing a stack
  down and bringing another up mid-session — which was the weakest point of the BLE path.
- **Same-channel constraint.** ESP-NOW peers must sit on the same Wi-Fi channel. The
  discovery beacon announces the channel and the session pins it to `PB_LINK_CHANNEL`;
  P7-C1 must handle a peer found on another channel by re-tuning before HELLO, not by
  failing.
  **NARROWED BY WHAT P7-C1 ACTUALLY BUILT.** The beacon carries NO channel field and there is
  NO re-tune path: both devices apply `PB_LINK_CHANNEL` on every LINK bring-up, so a peer on
  another channel is not discoverable at all and there is nothing to re-tune to. The cost,
  stated: a device left on another channel by something outside this firmware is invisible to
  LINK, and this firmware cannot tell that from an empty room.
- **The Bluedroid leak no longer needs measuring** (it was a P7-C1 task only if BLE won).
- **Unchanged by this decision:** the protocol itself. P4-C5 still proves codec, session
  FSM and lockstep battle over an in-process loopback with drop/dup/reorder injection
  before any radio is involved.

### Verified against the installed core before closing (esp32 3.1.1, ESP32-C3)

Checked in `esp_wifi/include/esp_now.h` and `sdkconfig.h` of the toolchain this project
builds with, so P7-C1 starts from facts rather than from the audit's summary:

| Fact | Value | Why it matters |
|---|---|---|
| `ESP_NOW_MAX_DATA_LEN` | 250 B (`= ESP_NOW_MAX_IE_DATA_LEN`) | **As shipped by P4-C5a: a 14 B header + a payload of at most 148 B + a 2 B trailing CRC = a 164 B worst-case frame, so it fits with 86 B to spare.** `protocol.h` asserts exactly that (`PROTO_HDR_BYTES + PROTO_PAYLOAD_MAX + 2 <= 250`), and `PROTO_PAYLOAD_MAX` is DERIVED as `max(PROTO_LEN_OF[])` rather than chosen. *(Corrected in P4-C6: this row was written on 2026-09-02, before the codec existed, and quoted plan §1.4's sketch — a 12 B header and a 200 B cap, "38 B to spare". Neither number was ever in the tree.)* |
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
- **God-mode ×3600 neglect soak (P3-C5).** Drive the DIAG CSV in `dev/godmode.cpp` at
  `GOD_SCALE_4` and confirm on the board what `care_a_fortnight_of_neglect_only_pins_the_stats_the_player_owns`
  now gates on the host: no stat the simulation owns pinned at 0 for more than 6 simulated
  hours. Added here by the P3-C5 follow-up, because both the plan and the "Still not run on
  hardware" note below claimed it was *already* listed here and it was not.

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

**Host suite.** 21 binaries, **328 tests, 350,214 checks** (phase 2 shipped 17 / 205 /
185,482). Phase 3 added 4 binaries, 123 tests and 164,732 checks.

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

**How much of a gate, measured (P3-C5 follow-up).** One fixture, and a measured 2.1 h against
a 6 h cap — a factor of 2.8 of headroom — so the assertion is looser than the sentence above
sounds. Each of the three constants that governs the criterion was edited in turn and
`make -C tests check` re-run: **the criterion case stayed green in all three**, and the gate
went red through something else every time.

| Edit | `…only_pins_the_stats_the_player_owns` | What actually failed |
|---|---|---|
| `CARE_ENERGY_ASLEEP_MPH` 11,000 | **ok** | `care_a_finer_step…`, `CARE_GRID_SLEEP_MILLI` 283 ≠ 433 |
| `CARE_DECAY_MPH[CARE_ENERGY]` −8,000 | **ok** | the same constant check, 466 ≠ 433 |
| `SLEEP_AFTER_DUSK_MIN` 300 | **ok** | five sleep-window cases, none of them this one |

The gate is not blind to these edits, but what catches them is an unrelated grid-bound
equality, not the exit criterion. Asserting the criterion over a small genome × month-anchor
grid rather than the single `0x5EED0C7A` / day-100 fixture would put the bite where the
sentence says it is; that is open work, not a claim.

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
6 h number is not arbitrary, but it is not a knife edge either — **and the three thresholds
this paragraph used to name were all wrong** (P3-C5 follow-up). Re-measured by an independent
re-run of the same harness shape, 40 genesis genomes × 12 month anchors, one constant edited
at a time. The seeds differ from the exit harness's, so figures move about 1 %: the shipped
build reads 3.88 h here against the exit's 3.87 h.

| Edit to `data/balance.h` | Worst energy run at 0 | Fortnights over 6 h | Worst energy at wake |
|---|---|---|---|
| *shipped* | 3.88 h | 0/480 | 99 % |
| `CARE_ENERGY_ASLEEP_MPH` 13,300 | 3.92 h | 0/480 | 99 % |
| `CARE_ENERGY_ASLEEP_MPH` 12,200 | 4.95 h | 0/480 | 91 % |
| `CARE_ENERGY_ASLEEP_MPH` 12,000 | 5.15 h | 0/480 | 89 % |
| **`CARE_ENERGY_ASLEEP_MPH` 11,000** | **6.10 h** | **6/480** | 82 % |
| `CARE_ENERGY_ASLEEP_MPH` 8,000 | 8.93 h | 88/480 | 59 % |
| `CARE_ENERGY_ASLEEP_MPH` 5,000 | 6.38 h | 19/480 | 59 % |
| `CARE_ENERGY_ASLEEP_MPH` 4,000 | 5.23 h | **0/480 — met again** | 59 % |
| `SLEEP_AFTER_DUSK_MIN` 180 (3 h) | 5.38 h | 0/480 | 99 % |
| **`SLEEP_AFTER_DUSK_MIN` 240 (4 h)** | **6.38 h** | **18/480** | 99 % |
| `CARE_DECAY_MPH[CARE_ENERGY]` −7,000 | 5.68 h | 0/480 | 99 % |
| **`CARE_DECAY_MPH[CARE_ENERGY]` −8,000** | **7.03 h** | **36/480** | 99 % |

So the criterion first breaks at `CARE_ENERGY_ASLEEP_MPH` ≈ 11,000 (a 45 % cut, not "much
below 13,300"), at `SLEEP_AFTER_DUSK_MIN` ≈ 240 min (4 h, not ~3 h), and at
`CARE_DECAY_MPH[CARE_ENERGY]` ≈ −8,000 (a 33 % raise). **13,300 is not a threshold for the
6 h run at all** — at 13,300 nothing breaks and the worst run barely moves. What the asleep
rate protects near there is the OTHER half of the criterion, energy above 90 % at sunrise,
and that first fails around 12,100.

**And the dependence is not monotonic**, so no sentence of the form "dropped much below X
breaks it" can be true. Below about 5,000 the pebble is too flat at sunrise to satisfy
`SIM_WAKE_DAY_ENERGY_PCT` (60, `game/sim.cpp:672`), so instead of being pinned awake at 0 it
simply stays asleep into the day and keeps charging. The same harness counting minutes spent
asleep while it is light: **0** across all 480 fortnights at 20,000, 11,000 and 8,000;
697,889 at 5,000; 1,475,847 at 4,000 — mean sleep per fortnight 145.3 h, 145.3 h, 145.3 h,
169.5 h, 196.5 h. The run at 0 gets *shorter* as the recharge gets worse, and the criterion
is satisfied again at 4,000.

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
NOT re-recorded and no existing test changed. Cost: **28 B of flash, 0 B of static RAM**
(corrected from 32 B by the P3-C5 follow-up). Measured by rebuilding the baseline against the
fullest reconstruction of the pre-fix `poop_step()` — carry arithmetic, the `g.poop_rem`
field and its three resets all removed: 1,893,072 → 1,893,044. Removing only the carry
arithmetic and keeping the field costs 14 B (1,893,058). Globals are 70,348 in all three
builds, so "0 B of static RAM" is exact. No reconstruction reproduces 32 B.

**How far it reached, precisely.** `POOP_MAX` is 4, and a pebble already holding four
uncleaned poops has nowhere to put a fifth: the `poop_count` branch is not taken and the
trajectory is unchanged. So the *neglected* pebble — the one every long-run case in
`test_care.cpp` plays — never saw this bug at all; reintroducing the truncation leaves the
thirty-day, month-offline, fortnight and week-in-the-Box cases and the golden green, and
fails only the two cases that play a pebble which goes to bed clean. That is the played pet,
so the fix matters, but "a sleeping pebble could not poop overnight" is a statement about the
timer, not about every save file.

What remains is a bound, not a bug, and the test says that plainly: a rate CHANGE — a poop
arriving, the loneliness multiplier turning on, the sleep/wake flip — is evaluated on the
`SIM_SUBSTEP_S` grid, so a finer step charges the new rate up to one sub-step early. The
budget is one sub-step of the change in question: **25 milli** for the poop/loneliness pair
(`CARE_GRID_EVENT_MILLI`), and **433 milli** for the sleep/wake flip
(`CARE_GRID_SLEEP_MILLI`: energy swings 26,000 milli/h, from `CARE_DECAY_MPH[CARE_ENERGY]`
−6,000 to `CARE_ENERGY_ASLEEP_MPH` +20,000). Measured 9 milli over 2 awake hours (1 poop),
25 over 4 (3 poops), 11 over an 8 h night (2 poops at ×0.35 plus the loneliness edge), 43
across bedtime and 126 across sunrise. A displayed percent is 1,000 milli. `SIM_SUBSTEP_S`
moved to `game/sim.h` so a test can state that bound. Mutation-checked: reverting the poop
carry fails 11 checks across sections 9b and 9c — 6 before this follow-up widened them.

**Three corrections to the above, from the phase-3 exit verification (P3-C5 follow-up).**

1. *The largest rate change was not 25 milli, and the exit's own comment said "two exist".*
   The third is the sleep/wake energy flip, 17× the other two. The test could not see it
   because `care_run_chunked()` sets `SimEnv` once and never advances the wall clock, so no
   sleep/wake edge can fall inside any span it measures. `care_run_chunked_clock()` moves the
   clock the way `app.cpp` does; across bedtime the 1 s-vs-coarse gap is 43 milli and across
   sunrise 126, both bounded by one sub-step of the flip and both still far under a displayed
   percent.
2. *The chunk test never tested the chopper.* `sim_tick()` is nothing but a chopper, and the
   strengthened case ran the one hour — 10:00 → 11:00 on a fresh hatchling — that contains no
   rate change, so a single `sub_step(3600)` lands on the same 128 bytes as sixty
   `sub_step(60)`. Replacing the loop with `if (seconds > 0) sub_step(seconds);` kept the
   whole gate green. Section 9c now also runs a chunk *above* the grid over spans that do
   contain a rate change: gaps of 25 / 11 / 126 milli become 1,858 / 3,594 / 3,655 — up to
   3.7 displayed percent — without the chopper. God mode reaches this on hardware
   (`GOD_SCALE_4` is 3600 s per tick). Two related claims in that test were also false and are gone: the
   60 × 60 and 6 × 600 cases guard nothing, not even the "`SIM_SUBSTEP_S` still divides an
   hour" they were documented as guarding (set it to 7 and the case still passes), and the
   equality-only case passed all 44 checks with `sim_tick()` stubbed to a no-op, which a
   liveness assertion now prevents.
3. *"`stage_step()` lost the same class of leak" was not mutation-checked.* Reverting
   `g.acc_stage -= STAGE_CHECK_PERIOD_S` to `= 0` left the entire suite green. It is
   observable, and section 9d now observes it: driven 90 s or 100 s at a time — neither a
   multiple of the period — the carry has the k-th check land as soon as elapsed time reaches
   60 k, so the baby → child transition is noticed at 13,500 s exactly; with `= 0` the cadence
   stretches to the caller's chunk and it is noticed 90 s and 100 s late. A `static_assert`
   now pins `SIM_SUBSTEP_S <= STAGE_CHECK_PERIOD_S`, the assumption that makes one subtraction
   enough — which also makes `SIM_SUBSTEP_S = 3600` a compile error rather than one arithmetic
   identity in one test.

### Overstated claims in the phase-3 audit trail, corrected

- **P3-C3's "`test_pet_view` proves the body the view describes really changes" is not true
  of the shipping build**, and the box has been split so that the false half is now an OPEN
  item. `pet_view_fill()` — the only path that feeds `evo_state`'s stage bits to
  `sprite_form_of()`, and the one the test drives — **has no caller in `Pebblebol/src`**. The
  firmware fills the active pet through `pet_view_fill_sim()`, whose `form` comes from the
  genome, the minor form and the life stage, none of which an evolution moves;
  `sprite_lookup_pose()` never sees `species_id`, `SpeciesDef.sprite_id` is read by nothing,
  and `STR_SPC_NAME_1..3` are displayed nowhere. A player who confirms an evolution today
  watches **2.7 s** of ceremony and gets back the same body, the same name and a different
  `hp_max`. (2.7 s, not the 4.5 s this paragraph used to say: the EVOLVE arm of
  `ceremony_begin()` back-dates `s_t0` by `HATCH_T_FLASH` — there is no shell to rock or
  crack — and `CP_NAME` ends the show at `HATCH_TOTAL_MS`, so an evolution runs
  4,480 − 1,800 = **2,680 ms**. 4,480 ms is the HATCH ceremony. `ui/ceremony.cpp:69-73`
  and `:140-143`, constants from `core/config.h:187-206`.) The MODEL half of P3-C3 is genuinely complete; §18's visual transformation is
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

Four more, found by the P3-C5 follow-up's number and checkbox audits — the two adversarial
lenses that died on API errors before the tag was cut, so nothing below had been checked by
anyone when it was written.

- **"Every minigame run is a CONSTANT length" is a WORST CASE, not a constant, for four of
  the six.** Measured by driving each game's pure logic to `done()` and reading `MgCtx::t_ms`,
  over five seeds and four input tapes (idle, mash both, mash L, mash R):

  | Game | Idle | Mashed | Constant? |
  |---|---|---|---|
  | PING | 9,025 – 12,500 ms *(varies by seed)* | 75 ms | no |
  | SEQUENCE | 2,100 ms | 1,350 – 3,250 ms | no |
  | PACKET FLOOD | 11,675 ms | 9,875 ms | no |
  | FIREWALL | 12,000 ms | 12,000 ms | **yes** |
  | BUFFER | 10,000 ms | 10,000 ms | **yes** |
  | DELETE | 11,600 ms | 175 ms | no |

  What IS true, and is the property the claim was reaching for: **the idle figure is a fixed
  UPPER BOUND that no press can exceed** — 11,675 / 12,000 / 10,000 / 11,600 ms for the four
  §29 games, which is the hole both shipped games had. Presses can only shorten a run.
  DELETE ends the moment its charges run out (`delete_logic.cpp:211`, `s.charges == 0u`);
  PACKET FLOOD's `pf_press()` calls `pf_resolve()`, which advances `c.round` early
  (`packet_flood_logic.cpp:227`, `:176`). `test_minigames.cpp` knew this and said so in the
  body — `CHECK(sort_steps <= idle_steps); // playing can only shorten it` — while its own
  name claimed the opposite; the name is corrected too. PING and SEQUENCE are not constant in
  any sense: PING's idle length is seed-dependent, and SEQUENCE is the one game a player can
  *lengthen* (the plan's own stretch tape runs 14,100 ms against a 2,100 ms idle).
- **The sentence e2004e7 says it struck was struck only in the plan.** e2004e7's diffstat
  does not contain `tests/test_pet_view.cpp` at all (it touches `tests/Makefile` alone), so
  "`test_pet_view` proves the body the view describes really changes" survived verbatim in
  the two places a reader actually meets the test: the §P3-C3 banner and case name in
  `tests/test_pet_view.cpp`, and the `tests/Makefile` comment above its link line. Both now
  say what the case covers — the MODEL half, on `pet_view_fill()`, a path with no caller in
  `Pebblebol/src` — and point at the open P3-C3 bullet. The case is renamed
  `an_evolution_changes_the_body_the_view_would_describe`. Its banner also carried the same
  4.5 s figure corrected above.
- **The exit's "Cost: 32 B of flash" for the poop carry does not reproduce**; 28 B does. See
  the integrator section above for the three builds.
- **The CHANGELOG's "cost 11,696 B of flash and 72 B of static RAM … which is 79 % of the
  2,400,000 B gate cap" reads as 11,696 B being 79 % of the cap.** Both percentages are
  right — 1,893,072/2,400,000 = 78.9 % and 1,168,772/3,145,728 = 37.2 % — but the antecedent
  is the baseline total, not the phase delta. Reworded to match this file, which already had
  it right.

Not corrected, because it holds: the "9 milli over 2 awake hours (1 poop)" above was
challenged as no longer reproducible. It reproduces exactly from this tree — driving
`care_grid_gap(10, h*3600)` for h = 1…8 gives 0 / **9** / 13 / **25** / 31 / 31 / 31 / 31
milli at 0 / **1** / 2 / **3** / 4 / 4 / 4 / 4 poops. The 2 h span is not a *live assertion*
(§9c's first case starts at 4 h), but the sentence claims a measurement, and the measurement
is right.

### Still not run on hardware

Unchanged from the phase-2 exit and worth repeating at a tag: **this firmware has never run
on a physical board.** Every number above is a host measurement or a compile result. The
plan's god-mode ×3600 soak (`dev/godmode.cpp`, the DIAG CSV) stays as the on-hardware
confirmation and **is now genuinely listed** with the other first-flash measurements above —
the P3-C5 follow-up found that this sentence and the plan's had both been asserting a listing
that did not exist, and added the bullet; the criterion it was meant to check now lives in the suite, where
it runs on every gate instead of once, by hand, on a board nobody has.

## Phase-4 exit (P4-C6, 2026-09-04)

**Variant matrix.** `tools/build_matrix.sh` compiles all seven feature variants with
`--warnings all` and fails on any warning pointing into the sketch. Result at the
`v0.4.0-battle` tag, every variant at **0 project warnings**:

| Variant | Overrides | Flash (B) | Static RAM (B) | Δ flash vs 0.3.0-pet | Δ RAM vs 0.3.0-pet |
|---|---|---|---|---|---|
| baseline | — | 1,915,654 | 72,676 | +22,582 | +2,328 |
| no-ble | `FEATURE_BLE=0` | 1,203,188 | 49,172 | +22,504 | +2,304 |
| no-web | `FEATURE_WEB=0` | 1,286,128 | 51,732 | +22,512 | +2,312 |
| no-god | `GOD_MODE_ENABLED=0` | 1,903,770 | 72,508 | +22,716 | +2,304 |
| sh1106 | `DISPLAY_IS_SH1106=1` | 1,915,654 | 72,676 | +22,582 | +2,328 |
| all-off | every `FEATURE_*`=0 + `GOD_MODE_ENABLED=0` | 515,268 | 24,408 | +22,658 | +2,296 |
| **release** | `GOD_MODE_ENABLED=0 FEATURE_BLE=0` (D2) | **1,191,426** | **49,004** | +22,654 | +2,296 |

A whole phase — a generated content pack and a 36-species roster, the deterministic battle
engine, the AI, the battle on the device with six pixel goldens, one Pebble validator, the
§15 codec and a lockstep session proven over a faulty loopback — cost **22,582 B of flash
and 2,328 B of static RAM** on the baseline. Caps are `GATE_FLASH_MAX` 2,400,000 and
`GATE_GLOBALS_MAX` 90,000 (`config.h`), so the baseline sits at **79.8 % of the flash cap**
(484,346 B free) and **80.8 % of the globals cap** (17,324 B free); the release build is
1,191,426 B, **37.9 %** of the 3,145,728 B `app0` slot, where it has been since phase 2.

**Where the phase-4 cost actually landed.** Per-commit, every delta the difference of two
adjacent commits' own recorded `flash=`/`globals=` lines, and **the eleven deltas —
`git log --oneline v0.3.0-pet..d21f20f` is exactly eleven commits — sum to the
+22,582 / +2,328 in the table above** — which is the check the first version of this
paragraph failed:

| commit | chunk | flash | globals | after |
|---|---|---|---|---|
| `993e3b0` | P4-C1 content pack + roster | **+3,022** | 0 | 1,896,094 / 70,348 |
| `532ceff` `9997ed4` `61b42d2` | P4-C2, P4-C3, their follow-up | 0 | 0 | 1,896,094 / 70,348 |
| `c9c64f3` | P4-C4a species→body | +164 | 0 | 1,896,258 / 70,348 |
| `a440a4d` | P4-C4 battle screen | **+18,780** | **+2,304** | 1,915,038 / 72,652 |
| `ee75076` | P4-C4 follow-up | −124 | 0 | 1,914,914 / 72,652 |
| `66993bb` | P4-C5a validator + codec | +782 | +24 | 1,915,696 / 72,676 |
| `ab6ecfe` `250f73e` | P4-C5, its follow-up | 0 | 0 | 1,915,696 / 72,676 |
| `d21f20f` | P4-C6 exit | −42 | 0 | 1,915,654 / 72,676 |
| | **phase 4** | **+22,582** | **+2,328** | |

Two corrections are folded into that table and named here rather than quietly applied,
because the first version of this ledger was the seventh wide sentence of the kind this
exit exists to catch. It said **"P4-C1 and P4-C2 moved the baseline not at all"**: that is
true of GLOBALS and **false of flash**, where P4-C1 cost **+3,022 B**, exactly as
`git log -1 993e3b0` and `PEBBLEBOL_IMPLEMENTATION_PLAN.md:529` both record in bold. One
axis's true number had been generalised onto both, and the missing 3,022 B — 13.4 % of the
phase's whole flash bill — was left attributed to nothing. It also gave P4-C5a as **+658 B**,
which is 1,915,696 − 1,915,038, i.e. measured across the skipped `ee75076` follow-up rather
than from the adjacent commit; P4-C5a's own cost is **+782 B** and the follow-up's is
**−124 B**. What survives unchanged is the shape of the bill: **P4-C4, the battle screen, is
+18,780 B of flash and +2,304 B of globals** — the file-scope statics (`s_setup` 780 B,
`s_st` 212 B, `s_ring` 576 B, `s_ai` 8 B and the rest, `ui/screen_battle.cpp:72-101`) — and
phase 4's globals bill starts at P4-C4, not at the roster, because the tables are
`constexpr`/`.rodata` and a roster is a flash cost. **P4-C6 itself is −42 B of flash** and
−16 B of globals on `release`, from deleting four write-only view fields and the dead
mood-badge chain (below). **The "every variant" half is now measured rather than asserted**:
the P4-C6 follow-up ran `build_matrix.sh` at `250f73e` (baseline 1,915,696 · no-ble
1,203,230 · no-web 1,286,170 · no-god 1,903,812 · sh1106 1,915,696 · all-off 515,310 ·
release 1,191,468) and again on its own tree, which reproduces the exit's seven figures
exactly — **−42 B of flash on all seven**, and −16 B of globals on `release` alone
(49,020 → 49,004), the other six unchanged.

**Globals is the tight axis, and this is what the trend leaves for phases 5–10.** 17,324 B
free with six phases to go is 2,887 B a phase; phase 4 spent 2,328 B, so six more at
phase-4's rate lands at 86,644 of 90,000 — inside the cap with 3,356 B of margin, which is
not comfortable, and P7 and P8 are each larger in scope than P4. Three things make it
survivable. Two of the three are measured; the third is arithmetic over art that does not
exist yet, and it is labelled as such rather than carried inside the word "measured":

- **P7's session state is 468 B**, not a phase. `sizeof(Session)` compiled with
  `riscv32-esp-elf-g++` is 340 B and `sizeof(LinkEvent)` is 8 B, so a 16-entry ring is
  128 B — 2.7 % of the headroom, all caller-owned.
- **Phases 5 and 7 cost 0 new globals for their persisted state.** `Inventory` (32 B),
  `CooldownTable` (272 B) and `PendingTrade` (64 B) are already members of the one 1,936 B
  `GameState` that `persistence/game_state.cpp` already declares.
- **Phase 9's content growth is flash, not globals** — 36 → 60 species is **+576 B** of
  `SPECIES_TABLE`, measured: `sizeof(SpeciesDef)` under `riscv32-esp-elf-g++` is 24 B.
  **P10's sprite atlas is a PROJECTION, not a measurement**: 8,640 B is plan T13's
  arithmetic (60 species × 2 frames × 72 B XBM) for pixels nobody has drawn, against
  484,346 B free. The arithmetic is right; the art is hypothetical, and calling it measured
  would be the same move this exit corrected in the cost ledger above.

**And the 2,887 B-a-phase line is PESSIMISTIC FOR PHASE 5 SPECIFICALLY, measured at the
P4-C6 follow-up.** Phase 4's 2,328 B is not a phase's worth of state spread thin: it is
**one object**. `riscv32-esp-elf-nm` over the sketch objects gives `screen_battle.cpp.o`
**2,206 B** of `.bss`+`.data` (`s_setup` 780, `s_ring` 576, `s_st` 212, three `draw_*` row
caches 400, the rest) against **670 B for the other sixteen screens put together**
(`screen_box` 333, `screen_creator` 219, `screen_status` 38, median **8**). Phase 5's
persisted state is genuinely free — `s_gs` is already a linked **1,936 B** global
(`nm`: `s_gs` size `0x790`) and `Inventory` and `CooldownTable` are members of it, with
save, load, defaults and quarantine already wired in `save_manager.cpp` — so what phase 5
actually adds is a `ScanResult` buffer (6 B packed, 8 B aligned, × the scan cap) and two
screens at non-battle scale. **The pressure is P7** (a 340 B `Session`, a 128 B ring, and
ESP-NOW driver buffers that are not ours to size) **and P8**, not P5. The 2,887 B average
stays as the budget line because averaging is the honest way to plan six unknown phases;
this paragraph is why it should not be read as a forecast for the next one.

**And the relief D2 already bought is large:** compiling BLE out recovers **712,466 B of
flash and 23,504 B of static RAM**, measured here as baseline minus `no-ble`. The build that
ships sits at 54.4 % of the globals cap with 40,996 B free.

**THE CAVEAT, and it is load-bearing: none of P4-C5's NETWORKING is linked yet.** The
heading used to read "none of P4-C5 is linked", which is wider than the evidence under it:
P4-C5a's other half, `game/validate.cpp`, **is** linked — `riscv32-esp-elf-nm -C` on the
baseline ELF shows `T validate_pebble(PebbleInstance const&)`, reached from
`persistence/save_manager.cpp:610`. It is also, by elimination, where P4-C5a's **+782 B**
went: that chunk shipped the validator and the codec, and `--gc-sections` drops every symbol
of the codec, so the only half of it in the image is this one.

**What follows is about `src/networking` only.** `riscv32-esp-elf-nm` on the baseline ELF
finds zero `proto_`, `session_`, `pbw_`, `loopback_` or `transport_loopback` symbols and
none of `battle_link.cpp`'s five function names, because nothing under `app/`, `ui/` or
`persistence/` calls them and `--gc-sections` drops them. The 3,333 lines phase 4 added
under `src/networking` cost 0 B today. The 468 B above is a `sizeof`, not a measurement of a linked build.

**Host suite.** 31 binaries, **603 tests, 728,779 checks** (phase 3 shipped 21 / 328 /
350,214). Phase 4 added 10 binaries, 275 tests and 378,565 checks, and grew 6 existing
binaries. Goldens went 48 → **55** screen frames plus `tests/golden/battle_v1.txt`, a
22-round transcript — the only artifact in the tree that can catch a duration off-by-one
that both the Python model and `battle.cpp` share.

**Content gates.** `python3 tools/gen_content.py --check` → `8 files in sync,
CONTENT_VERSION 0x5B4A`; `cd tools/content && python3 verify.py` → `RESULT: 99 checks,
0 FAILED`. `tools/check.sh` additionally proves that **eleven of the thirteen** battle
tuning constants that decide hashed battle state agree between `src/data/balance.h` (which
the firmware compiles) and `tools/content/balance.json` (which the roster was tuned against);
the other two, `TYPE_MOD_SCALE` and `RISK_SELF_HP_PCT`, are 0 and name superseded rules, and
P4-C6 narrowed the gate's own justification to say so.

**Sanitizers.** `make -C tests check` under `-fsanitize=address,undefined
-fno-sanitize-recover=all` → **ALL PASS 31/31, zero ASan reports, zero UBSan runtime
errors**. It needs **ONE line** neutralised, `data/evolution_table.h:112`, where GCC 13.3
will not fold `&SPECIES_TABLE[0] == nullptr` inside a `constexpr` evaluation under ASan.
Commit `250f73e` said "the same three PRE-EXISTING GCC-13.3 `constexpr` sub-checks"; at this
commit one line — two sub-expressions — is the whole cost, re-derived by building and not
carried forward. Run by hand; deliberately not in `tools/check.sh`, because the gate must
build the firmware on a toolchain that does not have these sanitizers.

### D2 was already closed, and P4-C6's own plan bullet asked for it again

`PEBBLEBOL_IMPLEMENTATION_PLAN.md` carried "D2 decision formally requested from the owner
with the §0.2 evidence (needed by P7-C1)" in the P4-C6 box. **That obligation was already
discharged.** The owner-decision table at the top of this file records D2 as
**CLOSED — ESP-NOW (2026-09-02, owner)** (line 15), the consequences are written out under
"D2 — consequences of choosing ESP-NOW" (line 53), and the evidence P7-C1 starts from — the
250 B datagram, the 20-peer table, RSSI on the receive callback, the send-callback ACK, the
`esp_now_init()` ordering, the broadcast peer, `CONFIG_ESP_WIFI_ENABLED` — was verified
against the installed core (esp32 3.1.1, ESP32-C3) before it was closed. Asking again would
have been asking for a decision the owner had already made two days earlier. The plan bullet
is corrected in place rather than acted on.

Two numbers inside that D2 record were stale and are corrected at this exit rather than
repeated: the frame no longer "fits with 38 B to spare" as a 12 B header plus a 200 B
payload (that was plan §1.4's sketch, written before the codec existed) — the shipped frame
is 14 B + at most 148 B + a 2 B trailing CRC = **164 B, 86 B to spare**; and BLE removal
recovers **712,466 B / 23,504 B**, not the phase-1 audit's 721,632 / 23,688.

### What phase 4 did NOT measure, stated as unmeasured

- **The device frame budget.** `FRAME_BUDGET_US` is 50,000 µs (20 fps) and
  `rd_frame_time_us()` exists to be compared against it on real hardware. The battle screen
  is the heaviest thing this firmware draws — two mirrored 24×24 XBM bodies, bars, dither,
  flash and shake — and **it has never been timed on a panel.** The ≈24 ms `sendBuffer()`
  figure at 400 kHz is still an estimate.
- **Per-frame heap, the device half.** The host half is measured: 3,942 real
  `update()`+`render()` frames across all six battle modes for **0 allocations**, with the
  counter proved to fire first. `dev/godmode.cpp`'s `ESP.getFreeHeap()` sampling has still
  never run on a board.
- **The radio.** Every networking figure in this repository comes from an in-process
  loopback whose fault model is an independent per-direction Bernoulli draw from one seeded
  `Rng`. A real ESP-NOW link is bursty and correlated, so **the arm completion rates are
  claims about the model, not about the air.** Untested: the 250 B datagram in flight, the
  send-callback ACK, RSSI gating, the same-channel constraint, peer discovery, and whether
  1,000 ms is the right ladder rung — the whole ladder runs in virtual time, because
  `now_ms` is a parameter.
- **The roster's balance against the engine that ships.** The 40–63 % / 57–65 % win-rate
  band the content pack was tuned to came from `tools/content/sim_engine.py`, which diverges
  from `battle.cpp` in five ways `game/battle.h` names and **has no AI at all** — it scripts
  both sides. So no percentage anywhere in this repository is a statement about what the
  device plays, and none is quoted at this exit. **P9-C4** is the chunk that produces real
  ones (`tests/tools/balance_matrix.cpp`, 60×60 AI-vs-AI at 1,000 seeded battles a pair).
- **Four figures in this exit are HISTORICAL REVIEW MEASUREMENTS with no harness left in the
  tree**, and the P4-C6 follow-up could not re-derive them by running anything. They are
  internally consistent and each is recorded identically in every file that repeats it, but
  they rest on the reviewer's word rather than on a command a reader can re-run: **594 wins
  of 600** for maximum genome against minimum (200 + 200 + 194 across three configurations;
  `docs/protocol.md` LIMIT 4 and `game/validate.h`), **198 encoder-emitted frames its own
  decoder refused in 2,000,000** and the **35** the shipped case reproduces, **1,019
  injections over seventeen hours** on the RX budget, and the P4-C5b bug counts
  **263/472/88/18**. Everything else in this document was re-derived by running a command at
  this exit or at the follow-up.
- **Battery life.** D11 is still OPEN and is the single biggest open factor.
- **The 1 h heap soak (P2-C12) and the ×3600 neglect soak (P3-C5)** are still pending first
  flash. **This firmware has still never run on a physical board.**

## Phase-5 exit (P5-C5, 2026-09-05)

**Variant matrix.** `tools/build_matrix.sh` compiles all seven feature variants with
`--warnings all` and fails on any warning pointing into the sketch. Result at the
`v0.5.0-explore` tag, every variant at **0 project warnings**:

| Variant | Overrides | Flash (B) | Static RAM (B) | Δ flash vs 0.4.0-battle | Δ RAM vs 0.4.0-battle |
|---|---|---|---|---|---|
| baseline | — | 1,927,936 | 73,180 | +12,282 | +504 |
| no-ble | `FEATURE_BLE=0` | 1,215,574 | 49,676 | +12,386 | +504 |
| no-web | `FEATURE_WEB=0` | 1,299,260 | 52,236 | +13,132 | +504 |
| no-god | `GOD_MODE_ENABLED=0` | 1,916,046 | 73,012 | +12,276 | +504 |
| sh1106 | `DISPLAY_IS_SH1106=1` | 1,927,936 | 73,180 | +12,282 | +504 |
| all-off | every `FEATURE_*`=0 + `GOD_MODE_ENABLED=0` | 526,824 | 24,912 | +11,556 | +504 |
| **release** | `GOD_MODE_ENABLED=0 FEATURE_BLE=0` (D2) | **1,203,808** | **49,508** | +12,382 | +504 |

**+504 on all seven**, which is not a coincidence and is worth stating: nothing exploration
added lives inside a removable subsystem. The baseline sits at **80.3 % of `GATE_FLASH_MAX`**
(472,064 B free) and **81.3 % of `GATE_GLOBALS_MAX`** (16,820 B free); the release build —
the one that ships — is at **75.2 % of `GATE_RELEASE_FLASH_MAX`** and **76.2 % of
`GATE_RELEASE_GLOBALS_MAX`**, the caps `tools/build_matrix.sh` has enforced since `f496a3a`,
and **38.3 %** of the 3,145,728 B `app0` slot.

**Where the phase-5 cost landed.** Per commit, each delta the difference of two adjacent
commits' own recorded `flash=`/`globals=` lines; `git log --oneline v0.4.0-battle..HEAD` is
exactly four commits and the four sum to the table above:

| commit | chunk | flash | globals | after |
|---|---|---|---|---|
| `f496a3a` | budget + release caps (docs, `#define`s) | 0 | 0 | 1,915,654 / 72,676 |
| `84a8dae` | P5-C1 scan-only Wi-Fi + P5-C2 cooldowns | **−1,572** | **−112** | 1,914,082 / 72,564 |
| `6ec3355` | P5-C3 encounters + P5-C4 capture/items | **+13,854** | **+616** | 1,927,936 / 73,180 |
| this commit | P5-C5 exit (documents, one gate, comments) | 0 | 0 | 1,927,936 / 73,180 |
| | **phase 5** | **+12,282** | **+504** | |

**A phase that begins with a rebate.** P5-C1 deleted the Wi-Fi station path — credentials,
association, retry backoff, link-loss re-association, three `NetPhase` values, two `NetErr`
values, the creator screen's station branch and the golden that froze it — and the passive
scanner, the pure classifier, an 88-row token table and the cooldown module that replaced it
are **smaller than what went**. This is the only phase so far whose first commit is negative
on both axes.

**Every one of the +616 B of globals is attributed, symbol by symbol** (`riscv32-esp-elf-nm
-S` over the sketch objects of the baseline build), and the sum is checked against the linked
image rather than asserted:

| object | globals (B) | what holds them |
|---|---|---|
| `game/cooldowns.cpp` | 257 | `s_ram[32]` 256 + `s_dirty` — the per-boot fallback table |
| `ui/screen_care.cpp` (bag mode) | 178 | `bag_render()::rows` 176 (8 × 22 of list text) + mode + cursor |
| `ui/screen_network.cpp` | 143 | **`s_job` 136**, one `WifiScanJob`, + `s_t0` 4 + three phase bytes |
| `ui/screen_encounter.cpp` | 19 | `s_enc` 8 + `s_cap` 4 + `s_reward` 2 + five flag bytes |
| `networking/net.cpp` (new) | 5 | `s_scan_salt` 4 + `s_want_scan` |
| `game/inventory.cpp` | 4 | `s_mod`, the armed battle modifier |
| `game/{encounters,capture,corruption}.cpp` | **0** | pure functions over caller state |
| | **606** | symbols |
| | **10** | link alignment |
| | **616** | **measured image delta** |

The 136 B `WifiScanJob` is confirmed to be linked **once** by a second, independent
measurement: rebuilding with `WIFI_SCAN_MAX_RESULTS=8` moves globals 73,180 → **73,116**,
exactly −64 B, i.e. eight fewer 8 B rows in one buffer.

**Against `docs/budget.md`'s allowance, phase 5 came in under both lines.** The phase-5 row
read **15–25 KB of flash and 1.0–2.0 KB of globals**. Actual: **+12,282 flash** — below the
low end, because P5-C1's deletion offset 1,572 B of the 13,854 the loop then cost — and
**+504 globals**, which is **half the low end of that line and a quarter of its ceiling**. Two forecasts the phase-4 exit made about
phase 5 can now be scored instead of repeated:

- **"Phase 5 costs 0 new globals for its persisted state" — CORRECT.** `Inventory` and
  `CooldownTable` were already members of the one `GameState`, and none of the +616 is a
  persisted structure: it is the RAM fallback table, three screens' own fields and one
  scan buffer.
- **"What phase 5 actually adds is a `ScanResult` buffer and two screens at non-battle
  scale" — RIGHT IN SHAPE, SHORT BY 435 B.** The buffer is 136 B and the two screens are
  162 B together, which is the non-battle scale the forecast named. It did not anticipate the **257 B** per-boot cooldown
  table (which exists so that one calibration cannot free all 32 networks at once) or the **178 B**
  row cache the CARE screen's bag mode needs to render a list. Both are real and neither is
  a battle screen: the largest single object phase 5 adds is smaller than phase 4's
  `s_setup` alone.

**Does the globals trend still close? Yes, and by more margin than at the last exit.** At
the phase-4 exit, 17,324 B were free with six phases to go — 2,887 B a phase. Phase 5 spent
**504**, 17 % of its share, leaving **16,820 B free with five phases to go, 3,364 B a
phase**. `docs/budget.md`'s remaining projection (P6 0.3–0.8 K, P7 1.5–3.0 K, P8 1.5–3.0 K,
P9 0.2–0.5 K, P10 0.5–1.5 K) totals **4.0–8.8 KB**, so the ending state moves from the
document's ~80,700 to **~77,200–82,000 with BLE kept** and **~53,500–58,300 without**. The
conclusion is unchanged and so is its shape: **it fits on flash either way; on globals it
fits comfortably only if BLE goes**, and P7-C1 owns that deletion in the order the plan
fixes — ESP-NOW up on a board first, BLE out second.

**Host suite.** 37 binaries, **708 tests, 1,802,703 checks** (phase 4 shipped 31 / 603 /
728,779). Phase 5 added 6 binaries, 105 tests and 1,073,924 checks and changed 4 existing
binaries. Screen goldens 55 → **60**: six added (`network_scanning`, `encounter_wild`,
`encounter_special`, `capture_ready`, `care_bag_empty`, `care_bag_two`), one **deleted**
with the branch it froze (`creator_station`, a picture of a station link that no longer
exists), one renamed (`soon_network` → `soon_trade`, because SCR_NETWORK is a real screen
now) and two re-recorded (the CARE list gained a row). `tests/golden/battle_v1.txt` was
re-recorded twice, once per content commit, and both times the check was the same: mask the
`h=`/`hash=`/`content=` fields and diff — **empty both times**, so no balance number moved.

**Content gates.** `python3 tools/gen_content.py --check` → **9 files in sync,
CONTENT_VERSION 0x02B5** (phase 4: 8 files, 0x5B4A); `cd tools/content && python3 verify.py`
→ **127 checks, 0 FAILED** (phase 4: 99). `tools/check.sh` now proves **20** tuning constants
agree between `src/data/balance.h` and `tools/content/balance.json`, up from 13, and the gate
list itself grew by three greps: no association call anywhere under `src/`, no network
identifier named in `networking/wifi_scanner.h`, and — added at this exit — every assignment
to `ScanResult`'s padding pair under `src/networking` a literal zero.

**The third gate exists because the first two could not see the defect, and that was
measured, not argued.** Assigning those two padding bytes two bytes of a beacon name inside
`net.cpp`'s scan read path left **`GATE OK` and `ALL PASS 37/37`**: `net.cpp` is on
`check.sh`'s IMPURE list, `tests/Makefile` never compiles it, and the only host coverage of
those bytes is a fake driver that zeroes them itself. The existing layers catch a **rename**
— the header grep bites, and the offset case stops compiling because it names the member —
and neither can see a **value**. With gate 3 in place the same planted line fails by name.

**A MESSAGE THAT COULD NOT FIRE, in the matrix's own cap check.** `tools/build_matrix.sh`
verifies the release build against `GATE_RELEASE_*_MAX` and has an `else` arm reading
`MATRIX FAIL: could not read the release build's size line`. That arm was **unreachable**:
under `set -euo pipefail`, `rf=$(printf … | grep -oE 'flash=[0-9]+' | cut …)` dies on the
assignment when the grep matches nothing, so a failed release build ended the run with exit
1 and **no named reason at all**. Reproduced against the original file with a stub builder
that fails the release variant, before changing anything, so the finding is pre-existing
(`f496a3a`) and not introduced by this exit's edit to the same block. Fixed with the house
`{ … || true; }` form and then driven four ways — pass, flash cap breached, globals cap
breached, release build failing — each ending with a named line. It is the same defect the
dormant `WiFi.begin(` gate had when P5-C1 armed it, which is twice now in one phase: **a
gate whose grep can abort the thing it guards is this repository's most reliable way of
producing a check that cannot fail.**

**A ledger that did not add up, found by a verifier and fixed here.** `docs/budget.md`
attributed the 136 B `WifiScanJob` **twice** — once inside the +408 the P5-C1 probe
predicted, once inside the +208 it gave for the new screens — while the same document
asserts 408 + 208 = 616. The sentence was the one offered as *evidence* for the phases 6–10
projection, and it gave 208 B where the measured figure is 344 — 65 % more than it said. Which paragraph was wrong was
measured rather than reasoned about (the two measurements above), and only that paragraph
changed: **the three surfaces hold 344 B, of which 136 is a buffer**. This is the second
exit in a row to find its own cost ledger wrong in the same way — the P4-C6 follow-up found
"P4-C1 and P4-C2 moved the baseline not at all", true of globals and false of flash by
3,022 B. Two instances is a pattern, and the cheap defence is the one used here: make the
per-object attribution sum to the measured image delta, and print both.

**Sanitizers.** `make -C tests check` under `-fsanitize=address,undefined
-fno-sanitize-recover=all` → **ALL PASS 37/37, 1,802,703 checks, zero ASan reports, zero
UBSan runtime errors**, so all six of phase 5's new binaries are clean under both. Run by
hand on a COPY of the tree, deliberately not in `tools/check.sh`, because the gate must build
the firmware on a toolchain that does not have these sanitizers.

**It now needs TWO lines neutralised, not one, and the count is a leading indicator rather
than noise.** The phase-4 exit reported one: `data/evolution_table.h:112`, where GCC 13.3
will not fold `&SPECIES_TABLE[0] == nullptr` inside a `constexpr` evaluation under ASan.
Phase 5 adds the second, `data/encounter_table.h:336` (`&ITEMS_TABLE[0] == nullptr`, inside
`encounter_item_rows_have_a_drop()` — one of the guards this phase added). Both are the same
pre-existing compiler limitation and both sit in GENERATED headers, so the count grows by one
every time `gen_content.py` emits another compile-time guard that null-checks a table
pointer. That is the cost of the guards, it is real, and it is worth a line here so the next
exit does not rediscover it: at some point the fix is one shape of guard in the generator,
not N neutralised lines in a hand-copied tree.

### What phase 5 did NOT measure, stated as unmeasured

- **No radio has ever scanned anything.** Every scan in every test came from a fake
  `WifiScanDriver` behind the four-function seam that exists so the 12 s timeout and the B
  cancel could be host-driven at all. §67's "Wi-Fi scanning works" and "Wi-Fi shuts down
  after use" are therefore **left unticked**, and the P5-C5 bench line — NETWORK → scan →
  encounter within ≈ 5 s, radio off afterwards, a cooldown surviving a power cycle — is
  **not done**. What IS true and was not true at P5-C1: the scanner has a caller, so
  `--gc-sections` no longer drops it from the image these figures measure.
- **The salted hash's collision behaviour is arithmetic, not a measurement over real air.**
  48 bits of hardware address folded into 32 bits of FNV-1a; a 4,000-input sweep shows no
  zero output after the fold, and nothing has ever been scanned to say how many distinct
  access points a real room produces.
- **The evolution key opens nothing at this roster**, and the phase-5 exit says so in those
  words rather than "item 9 does something": debt 4 is discharged for the **class** and the
  **consumer** — item 9 has `ITEM_KLASS_EVOLUTION`, is obtainable (the HIDDEN drop row), and
  is offered to the rules and **kept** when none bites — and **not for the effect**, because
  `evolution.json`'s single `EVOC_ITEM` rule is species 53 → 54 and 53 is past the 36-species
  prefix. `the_evolution_key_has_its_own_class_and_no_shipped_rule_spends_it` asserts that
  emptiness, so the day P9 lands the rule the case fails and points at the paragraph.
- **One mutant survives.** Deleting `cap_attempt()`'s `validate_pebble()` post-condition
  leaves every host case green, because with a sealed genome the validator answers `VR_OK` on
  all 1,080 roster × level rows and the post-condition has no reachable falsifier at this
  roster — including under adversarial caller inputs, since `box_new_pebble()` clamps the
  level and a bad species is refused before construction. The **pre**-check is where the
  requirement bites and it is covered: dropping it turns all 1,080 rows red by name.
- **One commit title in this phase is wider than its own tree and cannot be edited.**
  `84a8dae` ends "…cooldowns that an uncalibrated clock cannot farm". What the design
  actually prevents is the CATASTROPHIC farm — one calibration freeing all 32 rows at once
  — and `game/cooldowns.h` says plainly that an uncalibrated device can still farm by
  rebooting, which is a trade and not a fix. The header, this section, the CHANGELOG and
  the §67 tracker all state the limit; the commit title stays as history.
- **The 1 h heap soak (P2-C12), the ×3600 neglect soak (P3-C5) and every battery figure
  (D11)** are still pending first flash. **This firmware has still never run on a physical
  board.**

## D8 — the tone engine landed against an open decision (recorded 2026-09-05, P6-C1)

D8 is still **OPEN**: the owner confirms the piezo GPIO when the sounder is soldered, and
nothing here has been heard on hardware. What P6-C1 committed is the code that will drive
it, plus the proposal `PIN_PIEZO 3` in `core/config.h` §2 — and three things worth
recording, because two of them are measurements and one is a rule.

### The pin has exactly one home, and that is now checked

`PIN_PIEZO` is defined once, beside the D1 map, and `hardware/audio.cpp` names the macro and
never a number. `tools/check.sh` gained a gate that fails any `#define PIN_...` outside
`core/config.h`; planting `#define PIN_PIEZO_ALT 4` in `audio.cpp` prints

    GATE FAIL: a PIN_ macro is defined outside core/config.h (1) - while D1 and D8 are open
    the pin map has exactly one home (spec section 68 r3)

The gate matches `#define` lines only, so prose may name the macro, and it says nothing
about a bare number handed to `pinMode()` — that is a different and much harder grep, and
this is a check on a SECOND DEFINITION, which is the failure that survives review.

**`PB_PINS_CONFIRMED` is still not defined anywhere.** Two dormant `static_assert`s were
added for D8 (the piezo may not share a pin with SDA, SCL, either button, the LED or the
battery divider; and it may not sit on a strapping pin), and they were checked by mutation
rather than by reading: compiled with `-DPB_PINS_CONFIRMED`, moving the piezo to 5 fails
*"D8: the piezo needs a pin of its own"* and moving it to 8 fails *"D8: no piezo on a
strapping pin (GPIO2/8/9)"*. **That compile also found a real latent defect and it is
fixed**: `PIN_VBAT_ADC` was declared *after* the guard block, so a build with the macro
defined did not fail an assertion, it failed to compile — `'PIN_VBAT_ADC' was not declared
in this scope`. Every pin the guards talk about is now declared before them. The
pre-existing *"D1: no button on a strapping pin"* failure is untouched and expected: D1 is
deferred, `PIN_BTN_R` is 2, and that is the owner's call to make.

### LEDC, not the core's `tone()` — and the reason is a number

The plan bullet allows either. Arduino-ESP32 3.1.1's `Tone.cpp` lazily creates a FreeRTOS
task with a **3,500-word (14,000 B) stack** and a **128 × 16 B = 2,048 B queue** the first
time `tone()` is called, at priority 10, and its `noTone()` calls `xQueueReset()` — which
discards a start that was queued and not yet run. Sixteen kilobytes of heap and a task to
click a piezo is not a trade this device can make, and the dropped-start race is the kind of
defect that would only ever appear on hardware. Read from the core source
(`cores/esp32/Tone.cpp`), **not measured on a board**. `tone()` would also not have been
cheaper in flash: it reaches the piezo through `ledcAttach`/`ledcWriteTone`/`ledcDetach`,
so it drags in the same driver *plus* the task and the queue.

`hardware/audio.cpp`'s device sink therefore attaches an LEDC channel for the duration of a
note and **detaches it between effects**, then drives the pin low — spec §18's "GPIO
inactive except during sound playback" and "avoid continuous PWM", and the §66 checklist
line "piezo output is inactive between effects".

### What it costs, attributed

Baseline **1,927,936 / 73,180 → 1,936,964 / 73,316**: **+9,028 B flash, +136 B globals**.
Release **1,203,808 / 49,508 → 1,212,844 / 49,660** (+9,036 / +152), against caps of
1,600,000 / 65,000.

The globals are attributed with `riscv32-esp-elf-nm` over both linked images:

| what | bytes |
|---|---|
| the engine's own state (`s_q`, head/count, current effect and step, step clock, sink and mute pointers, the sink's `s_attached`) | **22** |
| the ESP-IDF LEDC driver's statics (`s_ledc_fade_rec` 24, `p_ledc_obj`, `s_ledc_mutex`, `s_ledc_fade_isr_handle`, `s_ledc_slow_clk_rc_fast_freq`, `ledc_handle`, `clock_source`, `fade_initialized`, `s_periph_use_8m_flag`) | **50** |
| `__func__.0` — the LEDC HAL's IRAM-resident assert string | **24** |
| **named total** | **96** |
| link alignment and the HAL's other DRAM-resident assert strings (`/IDF/components/hal/ledc_hal_iram.c`, `range == 0`, found by diffing the two images' `.dram0.data` contents) | **40** |
| **measured `.dram0.data` +72 and `.dram0.bss` +64** | **136** |

The `.bss` half is exact: 64 B of named symbols, 64 B measured. The 40 B of slack is all in
`.data`, and the string diff above says what it is.

**The step table costs no RAM.** `kSteps` (92 B), `kFirst`, `kCount` and `kDeviceSink` link
into DROM at `0x3C1E_xxxx`, i.e. flash.

**Read against `docs/budget.md`, this is most of phase 6's flash line for one chunk of
three** (P6 is allowed 8–12 K flash and 0.3–0.8 K globals). About 7.5 KB of the 9,028 is
the LEDC driver and its log strings arriving with the first PWM output in the tree, and it
is paid once: P6-C3's power states and any later LED breathing reuse the same driver. The
globals are 17–45 % of the phase's line. If P7 needs the flash back, the cheap lever is a
`FEATURE_AUDIO` compile flag, which the seam already makes a one-file change — that is a
suggestion, not something P6-C1 built or measured.

### What is NOT covered by a test, said plainly

`hardware/audio.cpp` is a pure translation unit and `tests/test_audio.cpp` drives every path
of the engine through a recording sink. The **wiring** is a different claim. Of the four
places P6-C1 arms a cue, exactly one is reachable from a host binary —
`ui/screen_battle.cpp`'s hit and faint beats, covered by
`a_hit_and_a_faint_each_arm_their_own_cue_exactly_once` in `tests/test_battle_screen.cpp`.
The other three (`ui/ceremony.cpp`'s four phase edges, `ui/ui.cpp`'s level-up and its care
refusal) live in device translation units that no host binary compiles, so they are checked
by the compiler and by reading, and by nothing else. **Nothing has been heard.**


---

## D-P6C2 — where the activity day lives, and what pays for it (recorded 2026-09-05, P6-C2)

Four of phase 6's six carried-forward debts are in this one chunk, and each of them names a
byte count or an assert that blocks the obvious approach. What follows is the decision on
each, and the residual each decision leaves.

### 1. The day bucket is in `cd`, not in `inv`, and there is no schema bump

`CooldownTable.reserved_a[4]` became `act_day` (u16, the UTC day index) + `act_score`
(u16, that day's total, already capped per term). The blob is still 272 B, `rows` is still
at offset 12, and two new `offsetof` asserts pin the new fields — the same carve
`PebbleInstance.corrupt_until_epoch` got out of `reserved[12]` at P5-C3, on the same
argument: **an old blob reads 0 in both, day index 0 is 1970 and can never be a real day,
so 0 is unambiguously "no day opened yet"** — which is the correct state for every save
written before this commit. Nothing in the load path or `game/validate.cpp` checks a
reserved byte for zero, so the four bytes also round-trip untouched through an *older*
firmware.

The plan's bullet asked for `inv`, and `Inventory` is 32 B with **zero** padding and zero
reserved bytes, so there is no byte in it. Its three named alternatives:

- **(a) derive the day from `Inventory.ledger_epoch` — rejected, and not on cost.**
  `ledger_epoch` is re-stamped every time XP is *spent*, so `ledger_epoch / 86400` is "the
  day of the last XP spend", not "the day these counters belong to". Deriving one from the
  other gives one fact two owners, which is the objection `encounters.h` already makes to
  folding cooldown state into `EncounterInput`.
- **(b) grow `Inventory` behind a `SAVE_SCHEMA_VERSION` bump — rejected on a measured
  price, and the price is not "a migration plus a fixture".** A bump is not a row in
  `migrate_run()`'s step table: that table is reachable only from the legacy-`save`-key
  branch, and `pair_load()` sorts any non-equal version into `st.bad` so
  `load_all_inner()` returns `LOAD_CORRUPT` **before** the migration branch is reached. The
  phase-6 survey drove it: a healthy v2 save with its version byte aged to 1 and correct
  CRCs loads `LOAD_CORRUPT`, and `save_restore_checkpoint()` — the SAVE ERROR screen's
  "Recuperar" — returns 0. A real bump needs a version-tolerant pair reader written first
  and invalidates `box`, `cfg`, `cd`, `cs` and `tr` as collateral. Keep it for a real
  widening (`INVENTORY_SLOTS` 7→12).
- **(c) a new `ac0`/`ac1` pair — correct, and not needed for four bytes.** Version-
  transparent in both directions, at the cost of a `BlobOps` row, four call sites, 6 NVS
  entries and ~2× `sizeof` in globals (`GameState` is instantiated twice). Reach for it when
  the activity state outgrows four bytes.

**What is deliberately NOT persisted:** the four per-term counters, the distinct-network set
and the distinct-peer set are per-boot `.bss`. So a power cycle *does* clear the per-term
caps — and it buys nothing, because `act_score` is capped at `ACT_SCORE_MAX`, which is
exactly the sum of the four capped terms, i.e. exactly what an honest day reaches. **A
rebooting player gets to the ceiling sooner and never higher, and never twice in a day.**
That is the same shape of trade `game/cooldowns.h` took for the uncalibrated table:
a catastrophic farm exchanged for a linear one. Closing it costs
`CooldownTable.reserved_b[2]` and a write every minute, and nothing measured says it is
worth that yet.

### 2. No fifth metered XP slot — activity pays through `XP_SRC_CARRY`

`XP_LEDGER_SLOTS` is 4 and `xp.h` asserts the metered sources are exactly the first four
enumerators. A fifth makes `Inventory.xp_ledger` 5 B (moving `offsetof(items)` and failing
that struct's own assert) or keeps 32 B by taking `INVENTORY_SLOTS` 7→6, which silently
deletes the seventh item stack out of every save. `XP_SRC_CARRY` is *already* the
daily-windowed bucket and its declared meaning is "time carried awake", which is the
activity score's largest term.

**The cost, stated rather than glossed: activity XP and the passive carry drip now compete
for one `XP_CAP_CARRY` budget, so a heavy-walking day crowds out the drip.** They measure
the same thing, which is the argument for it; a day that earns 48 XP from carrying earns
none from networks, which is the price. A full activity day is 44 XP against that 48, so
the meter is close enough to bind on a heavy day and not so tight that an ordinary one is
thrown away.

### 3. The distinct-network set is exact, and the cap is why it can be

Ten `uint32_t` plus a count, 44 B of globals: **no false positives and no false
negatives.** A set never needs more entries than its term's cap, because past
`ACT_CAP_NETS` no further network can score. The phase-6 survey measured the alternatives
over 2,000 simulated days × 6 scans per access point, driven with real
`net_hash_from_bssid()` values: exact 41 B / 0 errors, a folded-u16 set 22 B / 3
under-counts, a 128-bit k=2 bitset 16 B / 43, a 256-bit one 32 B / 16. Every approximate
form errs in one direction only — a new network reading as already-seen, i.e.
under-counting — which is structural and is the safe direction, and is still an error. 25 B
was not worth buying one.

**The one inexactness that remains is upstream and is not claimed away:** `net_hash` is a
32-bit salted digest of a BSSID, so two access points can in principle collide and be
credited once between them, costing exactly one point in the player's disfavour.

### 4. The rare bonus is a declared input with its own stage — and the test that guards
that list could not see it

`EncounterInput` gained `uint16_t rare_bonus_pm` and `encounter_roll()` consumes it through
`ENC_STAGE_RARE`, one independent draw, after `pick_row()` and only for a WILD row: one step
up the rarity ladder within the same category. The outcome never changes, so the
WILD/ITEM/SPECIAL/NOTHING split is **invariant at every permille** and §22's 15 % NOTHING
floor is structurally safe rather than merely measured.

**`every_declared_input_reaches_the_answer` does not do what its name says.** It enumerates
the seven fields by hand and reads no `sizeof`; the phase-6 survey added an eighth declared
field, left it unread, and `test_encounters` printed **22/22**. The fix is a
`static_assert(sizeof(EncounterInput) == 20)` beside the struct: adding a field now stops
the build in front of the case list. The assert is the mechanism; the perturbation is the
proof.

**And that perturbation had to be paired, which is a finding about this kind of field.**
Every other input is folded into `encounter_seed()`, so sweeping it against one fixed base
moves the whole draw. This one is deliberately *not* in the seed (folding it in would make
a bigger bonus reshuffle rather than improve, so nothing could assert monotonicity), which
makes it a threshold on one independent draw — and for any single fixed input that draw
either clears the threshold or never does. Written as a sweep,
`CHECK(moved_bonus > 0)` **failed on a correctly wired field.** The case compares the same
input with the bonus off and on, across the same 64 networks the other fields use.

`ENC_RARE_BONUS_MAX_PM` is **250**, in `data/balance.h` because two modules read it (the
roll clamps to it, the score scales to it). At 1000 every common-band roll is promoted and
the common band empties completely. The obvious guard — "the common band is non-empty" — is
worthless: edited to 999 permille, about one roll in a thousand survives and the check
still passed in every category. The case asserts the **share** instead (a full activity day
may not even halve the chance of meeting an ordinary creature), which fails for any cap
above 500.

### What P6-C2 did NOT measure, stated as unmeasured

- **Nothing has run on a board.** The score, its persistence and its three rewards are host
  tests over pure modules; the wiring in `app/app.cpp`, `ui/ui.cpp` and
  `ui/screen_network.cpp` is checked by the compiler, by `tests/test_screens.cpp` (which
  links the real `activity.o`), and by reading.
- **The `peers met` term has no caller.** It is implemented, capped, deduplicated and tested,
  and P7-C1 is what will call it. Until then it contributes 0 to every real score.
- **No NVS wear measurement.** The activity half of the `cd` blob rides `cd_take_dirty()`'s
  existing cadence plus one flush per logic tick when the score actually moved; how many
  writes that is over a real day of walking is not known.


## Phase-6 exit (P6-C4, 2026-09-05)

**Variant matrix.** `tools/build_matrix.sh` compiles all seven feature variants with
`--warnings all` and fails on any warning pointing into the sketch. Result at the
`v0.6.0-activity` tag, every variant at **0 project warnings**:

| Variant | Overrides | Flash (B) | Static RAM (B) | Δ flash vs 0.5.0-explore | Δ RAM vs 0.5.0-explore |
|---|---|---|---|---|---|
| baseline | — | 1,948,802 | 73,580 | +20,866 | +400 |
| no-ble | `FEATURE_BLE=0` | 1,236,440 | 50,084 | +20,866 | +408 |
| no-web | `FEATURE_WEB=0` | 1,320,114 | 52,620 | +20,854 | +384 |
| no-god | `GOD_MODE_ENABLED=0` | 1,936,436 | 73,404 | +20,390 | +392 |
| sh1106 | `DISPLAY_IS_SH1106=1` | 1,948,802 | 73,580 | +20,866 | +400 |
| all-off | every `FEATURE_*`=0 + `GOD_MODE_ENABLED=0` | 547,274 | 25,324 | +20,450 | +412 |
| **release** | `GOD_MODE_ENABLED=0 FEATURE_BLE=0` (D2) | **1,224,210** | **49,924** | +20,402 | +416 |

The baseline sits at **81.2 % of `GATE_FLASH_MAX`** (451,198 B free) and **81.8 % of
`GATE_GLOBALS_MAX`** (16,420 B free); the release build — the one that ships — is at
**76.5 % of `GATE_RELEASE_FLASH_MAX`** and **76.8 % of `GATE_RELEASE_GLOBALS_MAX`**, and
**38.9 %** of the 3,145,728 B `app0` slot.

**Where the phase-6 cost landed.** Per commit, each delta the difference of two adjacent
commits' own recorded `flash=`/`globals=` lines; `git log --oneline v0.5.0-explore..HEAD` is
exactly four commits and the four sum to the table above:

| commit | chunk | flash | globals | after |
|---|---|---|---|---|
| `72263f9` | P6-C1 tone engine + motion capability | **+9,028** | **+136** | 1,936,964 / 73,316 |
| `e46416f` | P6-C2 activity score + three rewards | **+1,940** | **+72** | 1,938,904 / 73,388 |
| `e8701ea` | P6-C3 power ladder + sleep-correct clock | **+9,698** | **+192** | 1,948,602 / 73,580 |
| this commit | P6-C4 exit (three fixes, one gate, documents) | **+200** | **0** | 1,948,802 / 73,580 |
| | **phase 6** | **+20,866** | **+400** | |

**The phase overran its own flash line by 74 % and came in inside the scarce one.**
`docs/budget.md`'s P6 row reads **8–12 KB of flash and 0.3–0.8 KB of globals**. Actual:
**+20,866 flash**, i.e. 174 % of the top of the line, and **+400 globals**, half the low end.
The overrun is almost entirely two ESP-IDF drivers arriving in the tree for the first time —
LEDC with the first PWM output (~7.5 KB, P6-C1) and `esp_sleep` with the first
`esp_light_sleep_start()` (~9.6 KB, P6-C3). Both are paid once and reused: a later LED
effect and a deep-sleep rung link no new driver. Pebblebol's own phase-6 code is roughly
3.8 KB. **It is immaterial against the cap that matters** — 375,790 B of release flash remain
against an 80–115 KB forecast for P7–P10 — and it is the scarce axis that stayed healthy.

**The globals, attributed.** P6-C1 136 B: 22 B of tone-engine state, 50 B of LEDC driver
statics, 24 B of one HAL `__func__` assert string, the rest `.data` alignment and further
IRAM-resident assert strings named by diffing the two images' `.dram0.data`. P6-C2 72 B:
`game/activity.cpp`'s per-boot half and nothing else (the ten-entry network set 40, the
four-entry peer set 16, six counters, a seconds remainder, the pending gain, the dirty flag)
— **the persisted half costs 0**, because `act_day` and `act_score` are four bytes that were
already `CooldownTable.reserved_a[4]`. P6-C3 192 B: ~148 B of ESP-IDF's own sleep state
(`esp_sleep`'s `s_config` 80 B plus six smaller objects) against ~44 B of Pebblebol —
DIAG loop counters 16, the ladder's state 17, `app.cpp`'s idle clock 9. P6-C4 0 B.

### Three defects were fixed before the tag was cut

Listed worst first. Each is here because a **named** test or a gate now fails without the
fix, and each mutation below was run.

**1. The release artefact never advanced game time.** `app.cpp`'s `logic_tick()` moves the
world by `sim_step_seconds() * owed`. `sim_step_seconds()` returns `CareCtx::scale`, a
zero-initialised static whose only writer is `sim_set_time_scale()`, whose only two callers
are `dev/godmode.cpp:322` and `:595` — **both inside `#if GOD_MODE_ENABLED`** — and whose
`#else` stub `god_begin()` does not call it. `release` is `GOD_MODE_ENABLED=0`. So on the
build that ships, the scale stayed 0, every tick was `sim_tick(0)`, and care decay, ageing,
poop/sickness, the XP carry drip and the activity carried minute were dead while the device
ran; `gs_touch_lastseen()` still ran, so the next boot's absence was ~0 too and the pet
decayed **only for time the device spent powered OFF**. Pre-existing since the phase-2 stub,
but P6-C3 rewrote that exact line and rested the whole sleep ladder on it.

*Nothing in the suite could see it*, and that is the finding rather than an aside: every
case in `test_care.cpp` calls `sim_tick(n)` with its own `n`, so the number the firmware
multiplies by was never on the line. **Measured: planting
`uint32_t sim_step_seconds(void){ return 0; }` left the whole suite at ALL PASS 41/41.**
Fix: `sim_bind()` sets the default (`if (g.scale == 0u) g.scale = 1u;`) — a default that
lives inside a dev feature is not a default — and `sim.h` documents god mode as an override.
Guard: `care_a_freshly_bound_sim_advances_one_real_second_per_tick`, whose first assertion is
the value a release boot gets and whose second drives `app.cpp`'s own expression over a
half-hour sleep. **Mutation: removing the default fails it with 6 checks** (`0 != 1`, then
five care bars that never moved), 27/28.

**2. A typed clock plus a reboot was an unbounded activity farm.** `xp_ledger_restore()`
aged the saved anti-farm budget forward by `(now_epoch − saved_epoch) / refill_step`, clamped
to one window. Both epochs are wall clock; the wall clock is typed by the player on the TIME
screen; one window of "elapsed" refills a whole bucket.

**Measured, before:** 100 rounds of (clock +1 day, reboot, one Wi-Fi scan of the SAME ten
access points) spent **990 metered XP and 125,000 care milli-points in ZERO real seconds**,
against **37.8 XP for an honest day at full tilt** and a happiness bar (`PB_CARE_MILLI_MAX`)
that holds 100,000. The controls show each layer holds alone: 100 reboots with the clock left
where it was spent 0; 100 clock jumps with no reboot spent 0 metered XP — **but 125,000
milli of happiness, because the happiness half had no meter at all and needed no reboot.**

**This defeated the sentence `game/activity.h`'s whole five-layer argument leans on** —
"even a bug in every rule above it leaves the award rate-limited by a budget a reboot cannot
refill" — and **neither module's own test binary contained both modules**, which is how
`tests/test_activity.cpp` came to hold four named cheat cases (uncalibrated, insane, rolled
BACKWARDS, power-cycled) and miss the one direction of clock movement that pays.

Fix, in three parts. (a) The aging term is **removed**, not repaired: a typed day and a day
the device spent switched off are the same evidence, so there is nothing to repair. A restore
now hands back exactly the budget that was true at the last save, and `xp_ledger_tick()`
refills it out of seconds the device watched pass — which are the only seconds nobody can
type. (b) The happiness is scaled by the XP the ledger actually granted
(`act_happy_for_granted_xp`, pure and host-tested), so one budget covers both halves of the
reward. (c) The false sentences in `game/activity.h`, `game/xp.h`, `app/app.cpp`,
`data/balance.h` and `persistence/game_state.h` are corrected to what the code does.

**Measured, after:** the same 100 rounds, given a FULL bucket to start with, spend **exactly
`XP_CAP_CARRY` (48) and then nothing, for ever**; 100 more rounds add zero. The honest day is
unchanged at 37.8 XP.

**What the honest player pays for it, said plainly and not buried:** time the device spends
switched OFF no longer refills the XP budget. The meter now means "XP per day of device-**ON**
time" rather than per day of wall time. It is a smaller budget, it moves only in the player's
disfavour, and it is the only direction that cannot be typed. `game/xp.cpp` carries that
paragraph next to the code.

Guards: `activity.o` is linked into `test_xp` — the farm is cross-module, and the Makefile
says why — plus `a_typed_day_plus_a_reboot_cannot_refill_a_spent_activity_budget`,
`a_typed_day_without_a_reboot_cannot_pay_a_second_time_either` and
`an_honest_day_still_pays_what_it_always_paid`. Mutations: **restoring the aging term fails
four named cases** (`a_round_trip_under_reports_the_budget_and_never_over_reports_it` 463
checks, `a_reboot_cannot_refill_a_spent_budget` 17,
`the_battle_bucket_bounds_an_afternoon_of_fighting` 1,
`a_typed_day_plus_a_reboot_cannot_refill_a_spent_activity_budget` 4), 15/19;
**making `act_happy_for_granted_xp()` return the owed amount unscaled fails the two typed-day
cases**, 17/19. On the pure side, relaxing `open_day()`'s day comparison fails
`a_forward_clock_set_opens_one_day_at_a_time_and_never_more_than_one_budget` among 13 others.

**Two existing cases were NARROWED rather than deleted, and both narrowings are recorded in
their own bodies**, because the old wording is the reason the hole survived:
`a_round_trip_under_reports_the_budget_and_never_over_reports_it`'s
`CHECK_NEAR(live, back, 1)` became `CHECK_EQ(back, at_save)`; and
`the_battle_bucket_bounds_an_afternoon_of_fighting` drove its own "so a reboot cannot
shortcut it" claim **through the very call that was the shortcut**, and now drives it through
`xp_ledger_tick()`. `a_reboot_cannot_refill_a_spent_budget` kept its name and gained the case
it never had: a forward jump of a whole window, swept to four windows in 997 s steps, across
all four metered buckets.

**3. P6-C3's one load-bearing line had no guard at all.** `gt_mono_ms()`'s `ARDUINO` branch
(`esp_rtc_get_time_us() / 1000ULL`) is the whole of the chunk's "monotonic source that
survives sleep" and the whole of the uncalibrated-cooldown debt's discharge. Reverting it to
`millis()` — the exact regression the chunk was written to prevent — is invisible to the
entire host suite, because no host binary compiles that branch, and `tools/check.sh` gated
`ui_explore_clock()`, the CALLER, while nothing gated the line itself. The code was correct;
the guard was missing, which is this project's named recurring defect wearing a different
costume. A second gate now pins the device branch: it must name `esp_rtc_get_time_us()` and
must not name `millis()`. **Mutations, both halves: swapping the source → `GATE FAIL:
gt_mono_ms() does not read esp_rtc_get_time_us() on the device`; adding a `millis()` call
beside the correct source → `GATE FAIL: gt_mono_ms()'s device branch reads millis() (1)`.**
The stronger form — compiling that branch on the host against two fake headers so the CHOICE
OF CLOCK is executed rather than grepped — is written into the P7-C6 box, not claimed here.

### Sentences narrowed at this exit

- `PEBBLEBOL_IMPLEMENTATION_PLAN.md`'s P6-C2 box listed five anti-farm layers and ended
  "…and `XP_SRC_CARRY`'s meter underneath everything, **which a reboot cannot refill** and
  which `xp_ledger_restore()` re-seeds to ZERO on an untrusted clock". The last clause was
  false and it was the clause the list leaned on. Corrected in place, with the measurement.
- `game/activity.h`'s layer 2 claimed the day "only ever moves forward" as an anti-farm
  property. It moves forward *for free*, which is the farm's engine. The header now says so
  outright, says the module cannot tell a typed day from a day spent switched off, and points
  at the layer that actually bounds one.
- `tests/test_care.cpp`'s `care_a_thirty_minute_sleep_charges_thirty_minutes_of_care` claimed
  a property of the SLEEP PATH and never touched it — no `sim_step_seconds()`, no `owed`, no
  scheduler — so it passed on a build whose step size was zero. Renamed to
  `care_thirty_minutes_of_care_is_the_same_however_a_sleep_slices_it`, which is what it
  guards and is worth having.
- §67's `Device sleeps correctly` is held **open**, in the same words `Wi-Fi shuts down after
  use` has been held open since phase 5: the ladder is built and host-driven, but a CPU
  stopping is an observation and nothing has been observed. `Timed systems work after sleep`
  and `Time-based calculations work across reboot` are ticked, on the host standard every
  other ticked box uses.

### What phase 6 did NOT measure, stated as unmeasured

- **Nothing in this phase has run on hardware, and it is a larger caveat here than in any
  earlier phase, because two of the three chunks are about hardware.** No piezo has been
  soldered and no tone has been heard: every audio figure is a host measurement or a reading
  of the installed core's source. No board has slept: whether `esp_light_sleep_start()` really
  returns on a `GPIO_INTR_LOW_LEVEL` from GPIO10 and GPIO2 with the buttons' internal
  pull-ups is unverified, and the deepest two rungs are dead if it does not.
- **No current has been measured.** ~5 µA deep sleep is this repo's own citation; the
  ~130 µA light-sleep figure is a vendor number that appears nowhere in this repo and is the
  weakest input to D1's battery arithmetic. The boost module nobody has measured (D11) swings
  the same answer by 10× more than deep-versus-light does.
- **The audio wiring is covered by exactly one host test of four sites.** `ui/ceremony.cpp`'s
  four cues and `ui/ui.cpp`'s two are device translation units no host binary compiles; only
  `screen_battle.cpp`'s hit/faint pair is driven by a test.
- **The DIAG ENERGIA page's IDLE loop rate has never been read.** The ≤ 10/s acceptance
  figure is what a 1 s slice structurally produces, not a reading.
- **The `peers met` term still has no caller**, and will not until P7-C1. It is worth 60 of
  the 440 points a full day can reach, so the best day this firmware can actually have is 380
  points / 38 XP / 4,750 milli — which `tests/test_xp.cpp` asserts as such rather than
  claiming 440.
- **`sim_gain_restore()` still has the hole `xp_ledger_restore()` just lost.** Measured with a
  throwaway probe against this tree's own `sim.o`: 100 rounds of (clock +1 h, reboot) from a
  fully SPENT ledger manufacture **4,000 whole happiness gain points against a cap of 40 an
  hour**; the same restore with no gap manufactures 0. It is a different currency — permission
  to raise a stat by a care action, and the actions have their own cooldowns — so it was left
  alone rather than swept into this chunk's blast radius. It is written into the P7-C6 box
  with that number.
- **A Pebble at `XP_LEVEL_MAX` now earns no activity happiness**, because `xp_add()` returns
  at the top of the curve before it spends the meter and the happiness is scaled by what the
  meter spent. A loss, not a hole — the safe direction — recorded in `game/activity.h` with
  its one-line remedy, and carried to P7-C6 rather than decided at an exit.
- **No NVS wear measurement.** The activity half of the `cd` blob is bounded BY THE CAPS at at
  most 270 writes a day; it has not been observed on hardware.

## P7-C1 — the peer link as built (recorded 2026-09-05)

D2 chose ESP-NOW on paper on 2026-09-02. This is what it looks like in the tree, what was
narrowed on the way, and what is still unmeasured because there is no hardware here.

### 1. The seam took the radio unchanged — the P4-C5 claim, tested

`networking/transport.h` promised at P4-C5 that "P7's transport_espnow.cpp is the only file
in the tree that will include esp_now.h ... which is why networking/session.cpp and
networking/battle_link.cpp contain no conditional compilation at all." Both files are
**byte-identical across P7-C1**. `Transport` gained no field: `ctx` carries the endpoint
handle, `recv()` drains the ring the Wi-Fi callback fills, `send()` is `esp_now_send()`, and
`mtu` is `PROTO_FRAME_MAX` (**164, not `ESP_NOW_MAX_DATA_LEN`'s 250** — no legal frame
exceeds 164, so 164 is the tighter and correct ceiling; 250 would let an encoder bug put a
longer frame on air that the far end then names `PE_OVERSIZE`).

**The send-callback ACK is a statistic and is deliberately NOT plumbed into `send()`'s
bool.** `transport.h:36-40` and `transport_loopback.cpp:86-90` both require a refused send
and a dropped frame to be indistinguishable to the session; wiring the link-layer ACK back
would create a path the in-process loopback cannot exercise, which is the one thing the seam
exists to prevent. It surfaces as `espnow_stats().tx_ok/tx_fail` and `espnow_ever_acked()`,
for P7-C2's "the peer is still there" line.

Two things now hold that in place rather than a comment: a gate that fails the build if
`session.cpp` or `battle_link.cpp` grows a single preprocessor conditional, and
`tests/test_link_transport.cpp`, which runs **a whole battle between two real sessions over
a Transport whose `recv()` is `rxring_pop()`** — the same ring code the radio fills.

### 2. `LINK_RX_SLOTS` is 32, and the survey's 8 was wrong in the direction that hides

The P7-C1 survey reasoned from the protocol's shape: "a lockstep round has at most a handful
of frames in flight per direction ... Eight is generous, not tuned." Measured instead, over
300 varied battles per arm on the real ring:

| per-frame drop | worst frames one `session_poll()` put in the peer's ring | battles completed |
|---|---|---|
| 0 % | **10** | 300/300 |
| 10 % | **19** | 300/300 |
| 20 % | 18 | 300/300 |
| 30 % | 14 | 291/300 |
| 40 % | 13 | 229/300 |

A poll drains everything queued and answers all of it, so the answer arrives at the peer as
one burst; under loss the nine-rung ladder is re-sending while the peer is still catching
up, which is why the lossy worst is nearly twice the clean one. **Eight slots hold seven and
refused three frames per clean battle. Sixteen hold fifteen and would still have overflowed.**
Only frames refused while BOTH endpoints were still draining are counted: after one side
closes it stops draining altogether and the other's unanswered GOODBYE can fill a ring of any
size, which says nothing about the depth a live link needs.

**Why the number had to be measured rather than argued:** a frame lost to ring overflow is
INDISTINGUISHABLE to `networking/session.cpp` from a frame the radio dropped — that is
`transport.h:36-40`'s whole point — so the symptom of getting this wrong is a link that is
quietly slower than it should be, with no error anywhere and nothing in the host suite able
to see it. `EspNowStats.rx_ring_overflow` exists for the bench for the same reason.

Cost: 32 x 164 + 64 = **5,312 B of globals** against 1,328 for eight.

### 3. The hardware address has one home, and a gate says so

The sender's six bytes live in `s_slot_addr[LINK_PEER_CAP][6]` inside
`networking/transport_espnow.cpp`, plus the six of a bound session peer. What crosses into
`networking/discovery.h` — and therefore into the game, the UI and anything persisted — is an
opaque `slot` byte. `esp_now.h:111` says the recv info struct and all three of its pointers
are valid only inside the callback, so none is retained: the bytes are copied into a ring
slot and the pointer dies at the return.

`tools/check.sh` fails the build if `discovery.{h,cpp}` NAMES that identifier, in a field, a
parameter or a comment — the same gate `wifi_scanner.h` carries for a network's name, with
the prose cost paid in `transport_espnow.h`, the file that legitimately handles it.
**The shape not to copy is in this tree right now:** `core/nt_types.h`'s `BlePeerInfo` has
the raw address as its first member and `ble_social.cpp:243` copies it out of the scan
callback into the table the game reads.

**And the gate's limit is stated rather than glossed.** It catches a RENAME and it cannot see
a VALUE: six bytes smuggled through two `uint32_t` fields would leave it green, exactly as
`check.sh:490-509` measured for `ScanResult`'s two padding bytes. That is why
`a_disc_peer_has_no_room_to_hide_a_hardware_address` pins `sizeof(DiscPeer)` and every
offset; the struct declares 31 bytes, has one byte of tail padding and no room for six.

Address randomisation (`esp_wifi_set_mac`) is available and is deliberately not used: it
would break peer stability across a reboot and buys little, because the address is on air
whatever this firmware does. The honest statement is that the firmware does not amplify it,
does not store it, and does not show it to the player or to the game.

### 4. A NARROWING OF D2's "same-channel constraint", recorded

D2's consequences say: *"the discovery beacon announces the channel and the session pins it
to `PB_LINK_CHANNEL`; P7-C1 must handle a peer found on another channel by re-tuning before
HELLO, not by failing."* **What was built is smaller than that sentence.** The beacon carries
no channel field and there is no re-tune path. Both devices apply `PB_LINK_CHANNEL` on every
LINK bring-up, so a peer on another channel is not discoverable at all rather than
discoverable-and-unreachable — there is nothing to re-tune *to*, because a beacon from
another channel never lands in the callback.

The cost of the narrowing, stated: **a device left on another channel by something outside
this firmware is invisible to LINK, and this firmware cannot tell that from an empty room.**
Only a bench can say whether that happens in practice. The channel is re-applied on every
bring-up rather than once at boot because it is not stored in NVS and the section 40 scan
sweeps 1..13 and leaves the radio wherever its last dwell ended.

`PB_LINK_CHANNEL` must be 1..11 and `transport_espnow.cpp` static_asserts it:
`WiFi.setChannel()` refuses a channel outside the country range and the default country is
world-safe `"01"` (schan 1, nchan 11), so a 12 or 13 would be silently ignored and every
beacon would miss.

### 5. What `all-off` and `no-web` now mean — the phase-6 exit's carried bullet, discharged

`FEATURE_ESPNOW` is now a real `#define` in `config.h`, so `tools/build_matrix.sh` stops
skipping it in the `all-off` variant.

- **`all-off` did not change size and did change meaning: 547,274 / 25,324 at
  v0.6.0-activity and 547,274 / 25,324 here, identical to the byte.** With `FEATURE_ESPNOW 0`
  the driver compiles to its refusal stubs and `--gc-sections` drops the two pure modules
  nothing calls. It is now a build that has switched the peer link OFF rather than one that
  never had a switch.
- **`no-web` changed by half a megabyte, and that is the row a future reader would
  misattribute.** `net.cpp`'s `NT_NET_WANT_WIFI` was `(FEATURE_WEB)`; ESP-NOW is a Wi-Fi
  consumer that needs no web server, so it is `(FEATURE_WEB || FEATURE_ESPNOW)`.
  **1,320,114 / 52,620 → 1,900,532 / 77,364, +580,418 flash and +24,744 globals**, none of
  which is a phase-7 feature: it is the Wi-Fi driver, which that variant used to compile out
  entirely. `all-off` is now the only variant with no radio in it.
- `tools/build_matrix.sh` now PRINTS every define it drops. `FEATURE_WEATHER` and
  `FEATURE_TELEGRAM` are still fictional and now say so on every run. **The silence was the
  defect, not the define.**

### 6. Mutation testing — nineteen source mutations and seven gate mutations

Every one was planted, built and run, and each made a NAMED case or a NAMED gate fail.

| # | mutation | fails |
|---|---|---|
| 1 | `rxring_push` overwrites at `tail` when the ring is full | `a_full_ring_refuses_the_newest_and_keeps_every_older_one` (23 checks — on the PAYLOAD, not merely a counter) |
| 2 | `rxring_push` clamps an oversize record to `stride` instead of refusing | `an_oversize_record_is_refused_and_never_truncated` (the "ring is still empty" check) |
| 3 | `rxring_pop` returns 0 for a record that will not fit the caller's buffer | `a_pop_never_returns_zero_while_the_ring_still_holds_a_record` — the "0 is a lie" path `transport_loopback.cpp:107-118` wrote down for P7 |
| 4 | `LINK_RX_SLOTS` 32 → 16 | `a_lossy_link_bursts_harder_than_a_clean_one_and_the_ring_still_absorbs_it` (9 checks) |
| 5 | drop the `LINK_RSSI_MIN` floor | `a_peer_below_the_signal_floor_never_reaches_the_table` |
| 6 | qualify at 1 hit instead of `LINK_PEER_HITS_MIN` | `three_hits_are_what_makes_a_peer_real` |
| 7 | `act_note_peer()` on every accepted beacon instead of on the crossing | `a_peer_is_scored_once_however_many_beacons_it_sends` + 3 more |
| 8 | the TTL never expires | `a_peer_unheard_for_the_ttl_leaves_the_table` |
| 9 | the signal is the last packet, not the EMA | `the_signal_shown_is_an_average_and_not_the_last_packet` |
| 10 | drop the "never list yourself" filter | `a_device_never_lists_itself` |
| 11 | beacon on every `service()` instead of on the cadence | `a_beacon_goes_out_on_the_cadence_and_not_at_frame_rate` |
| 12 | drop the name's padding check (bytes after the first NUL) | `bytes_hidden_after_the_name_are_a_refusal_and_not_padding_to_ignore` |
| 13 | drop the reserved-capability-bit check | `a_beacon_that_is_wrong_about_itself_is_refused_even_when_it_is_sealed` |
| 14 | the section 47 ceiling never fires | `the_radio_is_released_exactly_once_on_every_exit_path` + 2 more |
| 15 | `release()` loses its `stopped` guard | `the_radio_is_released_exactly_once_on_every_exit_path` |
| 16 | beacon length `>=` instead of `==` | `every_way_a_beacon_can_be_wrong_has_its_own_named_code` |
| 17 | ignore `LINK_POLL_FAILED` from the driver | `the_radio_is_released_exactly_once_on_every_exit_path` |
| 18 | evict in place instead of remove-and-append | `a_ninth_device_evicts_the_oldest_and_never_the_newest` (8 checks) |
| 19 | drain the whole queue instead of `LINK_DRAIN_PER_SERVICE` | `one_service_drains_a_bounded_number_of_beacons_and_loses_none` |
| G1 | `discovery.h` names the hardware identifier in a comment | `GATE FAIL: networking/discovery.h names a hardware address (1)` |
| G2 | `net.cpp` includes `esp_now.h` and calls `esp_now_deinit()` | `GATE FAIL: the ESP-NOW API is called outside networking/transport_espnow.cpp (1)` |
| G3 | `session.cpp` grows an `#if defined(ARDUINO)` / `#endif` pair | `GATE FAIL: networking/session.cpp contains conditional compilation (2)` |
| G4 | `discovery.cpp` dropped from `PURE_NET_CPP` but kept in `PURE_NET` | `GATE FAIL: check.sh: networking/discovery.cpp is in PURE_NET but not PURE_NET_CPP` |
| G5 | a new unclassified `networking/trade_link.cpp` | `GATE FAIL: networking/trade_link.cpp is in neither check.sh's PURE_NET nor its IMPURE_NET list` |
| G6 | `discovery.cpp` grows a used file-scope mutable static | `GATE FAIL: networking/discovery.cpp holds file-scope mutable state (1)` |
| G7 | `discovery.cpp` includes `ui/gfx.h` | `GATE FAIL: networking/discovery.cpp includes a radio, hardware or renderer header (1)` |

**Two mutations were caught EARLIER than the gate, and that is worth recording rather than
counting as a gate hit.** An UNUSED file-scope static in `discovery.cpp` fails the firmware
build on `-Werror` before gate 2 runs (G6 was rewritten to use the static so the gate itself
could be seen to fire), and `#include <esp_now.h>` in `discovery.cpp` fails the HOST SUITE
outright, because the header does not exist off the target — a stronger answer than a grep,
and the structural reason the pure list is worth keeping pure.

**One mutation initially reported a false positive and the harness was fixed, not the
finding.** Replacing the `peers_age()` call with `(void)0` left `peers_age()` unused, which
fails `-Werror`, so the STALE binary from the previous mutation ran and reported the previous
mutation's failures. The mutation runner now reports "BUILD FAILED" instead of running a
stale binary, and the TTL mutation was re-planted as a TTL that never expires.

### What P7-C1 did NOT measure, stated as unmeasured

- **No radio ran.** Nothing in this environment can bring up ESP-NOW: the channel, the
  broadcast peer entry, `esp_now_init()` ordering after `esp_wifi_start()`, ESP-NOW's own
  duplicate suppression and MTU enforcement, and whether two boards hear each other are all
  bench facts. **The acceptance item "two boards see each other's beacon in LINK" is
  UNTICKED.**
- **The modem-sleep hazard is reasoned, not observed.** `WIFI_PS_MIN_MODEM` is the C3's
  default, it is applied at STA start, `CONFIG_ESP_WIFI_STA_DISCONNECTED_PM_ENABLE=y` makes
  it bite an unassociated station, and `net.cpp:scan_begin()` sets the cached value to true —
  a static that survives a mode change and `net_request(RADIO_OFF)`. `link_begin()` calls
  `WiFi.setSleep(false)` BEFORE `WiFi.mode(WIFI_STA)`. **No host binary can see any of that**;
  the beacon-count comparison before and after visiting the NETWORK screen is bench item 1.
- **The SPSC claim is a discipline, not a measurement.** A host binary is single-threaded, so
  nothing in `tests/test_link_transport.cpp` is evidence about the Wi-Fi task or about a push
  preempting a pop. `rxring.h` states the cursor convention, uses acquire/release rather than
  `volatile` (which is what `ble_social.cpp` does and `input.cpp` does not), and names both
  precedents; that is the whole argument and it is falsifiable only on a bench.
- **BLE IS NOT DELETED and `FEATURE_BLE` is untouched**, because the plan gates that deletion
  on two boards having linked and nothing here can link two boards. Until then `ble_social.cpp`
  costs the baseline 712,466 B of flash and 23,504 B of globals it has always cost, and the
  release build is still the one to read the budget against.
- **The peers-met activity term still contributes 0 to every real score.** `act_note_peer()`
  now has a call site — `link_service()`, once per peer that crosses the hit threshold, tested
  with `activity.o` in the same binary — but nothing in the firmware drives `link_service()`
  until P7-C2's LINK screen, and §42's consent belongs there with it.

---

## P7-C2/C3 — the LINK screen, consent, and the battle over the link (recorded 2026-09-05)

### 1. Consent is enforced twice, and only one of the two gates is host-observable

Spec §42 asks that `SESSION_REQUEST` / `SESSION_ACCEPT` need A on **both** devices. That is
not a dialog in front of an automatic handshake here; it is the only thing that starts one.
Until the local player has opened a peer's card and pressed A:

* **no `Session` exists on this device.** `session_init()` has not been called, so nothing
  drains the transport and a peer's HELLO is not looked at. This is the half a host binary
  can see, and `a_peer_in_the_room_does_not_start_a_session_on_its_own` is the case: the far
  device consents, shouts HELLO for its whole nine-rung ladder, and this screen ends the run
  still browsing with `link_screen_consents() == 0` and `lf_binds() == 0`.
* **no unicast peer is bound.** `networking/transport_espnow.cpp`'s receive callback compares
  every unicast frame against `s_bound_addr` and counts a mismatch as `rx_wrong_peer` before
  anything else in the firmware sees it. **This half is a bench fact and is stated as one:**
  the loopback the host tests run over delivers regardless of binding, so nothing here is
  evidence about it. Bench item 2.

The handshake is SYMMETRIC by construction (`session.h`: the roles come from the two device
ids, not from who spoke first), so each device speaks when ITS player presses A, and the one
that pressed alone reaches §47's "CONEXIÓN PERDIDA / A: Reintentar / B: Salir" —
**measured at 9,950 ms through the real screen**, against a `PROTO_RETX_MS * PROTO_RETX_MAX`
ladder of 9,000 ms.

`tools/check.sh` now fails the build if `session_init()` or `session_start()` is called
anywhere but `ui/screen_link.cpp`.

### 2. The operation is NOT announced, and §43's field list is not widened

The beacon still carries a device id, a name, a protocol version and a capability word, and
nothing else. Two devices that pick different operations discover it in the session. The
capability word is what the card uses to grey a row the peer cannot do — and this build
claims `DISC_CAP_BATTLE` only, because P7-C3 ships the linked battle and P7-C4/C5 have not
shipped trade or breeding. A peer that claims TRADE still gets the honest answer.

**UPDATED AT P7-C4:** the word is now `DISC_CAP_BATTLE | DISC_CAP_TRADE`, because an A on
INTERCAMBIO really opens a trade session. `DISC_CAP_BREED` is STILL not claimed —
`game/breeding.cpp` is complete and tested and no wire driver carries a breeding — so the
sentence above holds unchanged for CRIAR, which is what a capability word is for.

### 3. `link_hold()` — a call that was not in the plan, and the reason it had to exist

`link_cancel()` reaches `net_request(RADIO_OFF)`, which passes `wifi_down()`, which calls
`espnow_end()`. So ending the browse the obvious way when the two players consent would have
taken the stack down **one frame after they agreed to use it**. `link_hold()` stops the browse
— no beacon, no drain, no TTL sweep, no §47 ceiling on the BROWSE — and leaves the job
`LS_RUNNING`, so `link_is_busy()` and the power ladder still know the radio is out and
`link_cancel()` is still the ONE release path and still exactly-once.

**The ceiling is not lost; it changes owner.** From `link_hold()` the bound is the session's
own ladder, which is an order of magnitude tighter than `LINK_JOB_TIMEOUT_MS`. A job HELD with
no session behind it is the one shape that would hold the radio for ever, and the caller is
what must not create it: the hold is called only on the success path of the function that
opens a session.

### 4. A linked round gives each player nine seconds, MEASURED, and nothing papers over it

`session.h`'s rule R4 says only PROGRESS resets the ladder, and `battle_link.cpp`'s
`on_battle_state()` counts a waiting peer's probe as STALE. So a round that neither device
advances is closed `SE_LOST` after `PROTO_RETX_MAX * PROTO_RETX_MS` = 9,000 ms. **Measured
through the real screen against a real peer: 9,950 ms from the round opening to the link
dying**, with the screen's own countdown reading 8,950 ms at the start of it —
`a_player_who_thinks_longer_than_the_ladder_loses_the_link_and_is_paid_nothing`.

The clock resets on every frame that makes progress, so the FIRST mover has nine seconds from
the round opening and the SECOND has nine from the first mover's action. Nine seconds is still
nine seconds.

**What was done and why not more.** The linked battle's header shows the clock (`RONDA 3 7s`),
so the deadline arrives as a number rather than as a broken link. Substituting a move on a
timeout is forbidden in principle — `battle_link.cpp` says the two engines would disagree
about the substitute's legality — and stretching the ladder changes `session.h`'s own measured
3×3000-vs-9×1000 tuning, which a UI chunk has no business doing on the side. **The remedy, if
play says nine seconds is too short, is `PROTO_RETX_MAX` / `PROTO_RETX_MS`, and the cost is
that every other wait in the protocol gets longer with it.**

### 5. The reward gate is one expression, in one place, with one reader

`ui/screen_link.cpp`'s `link_battle_status()` answers `UI_LKB_WON` only where
`session_rewards_authorised()` is true — which `battle_link.cpp`'s `compare_end()` sets only
when the peer's `BATTLE_END` carried the same outcome AND the same final hash as ours. Every
other consumer asks that function; `tools/check.sh` fails the build if the flag is read
anywhere else.

`UI_LKB_BROKEN` is deliberately a FOURTH answer rather than `UI_LKB_LOST`. A desync is not a
defeat, and printing one as the other is the same class of mistake as `BO_ABORT` printing as
"Empate", which `game/battle.h` already records.

**The sharpest case is `a_win_the_peer_never_confirmed_pays_nothing`:** the local team is built
twenty levels stronger so the engine really does win, the peer's `BATTLE_END` is killed by
name on the loopback, and the ladder gives up nine seconds later. The engine holds a win, the
session authorised nothing, and the screen reports `won == 0` with the Box byte-identical.

### 6. `sm_home()` leaves a pushed screen unasked — found here, and closed here

`app/state_machine.cpp`'s `sm_home()` (LONG_BOTH) discards the whole back stack and runs ONLY
the current screen's `leave()` hook. A linked battle is `SCR_BATTLE` pushed on top of
`SCR_LINK`, and `ui/screen_link.cpp` deliberately does not release when it is pushed aside —
so LONG_BOTH out of a linked battle would have left the radio up, the session unpumped and the
power ladder clamped at DIM **for ever**, with nothing to end it: `session_poll()` is what runs
the ladder, and nothing was pumping.

`battle_leave()` runs on every route off `SCR_BATTLE` — the same property its one-report path
already depends on — so that is the hook that closes the session and gives the radio back.
`going_home_from_a_linked_battle_pays_nothing_and_gives_the_radio_back` is the case; deleting
the one line fails it.

### 7. Thirty-three hard-coded sides in the battle screen

`session.cpp` fixes the two sides from the two device ids, so on one board of every pair the
player is side 1. Every `0u` that meant "us" and every `1u` that meant "the foe" in
`ui/screen_battle.cpp` is now `s_me` / `s_foe()` - thirty-three lines: the legality
predicates, the submit, both `fill_art()` calls, the chrome's HP pair, the switch list, the
outcome word, the armed battle modifier and the four test queries. Left as literals they would have drawn the PEER's team as the player's on one of the
two devices and paid the wrong half of the fight.

### What P7-C2/C3 did NOT measure, stated as unmeasured

- **No radio ran, again.** Nothing here is evidence about ESP-NOW, the Wi-Fi task, a real
  channel, a real duplicate or callback threading. §67's "Local multiplayer works" is
  **UNTICKED**, and so is P7-C1's "two boards see each other's beacon in LINK".
- **The radio half of the consent gate is unobserved.** The loopback delivers regardless of
  binding; that an un-bound device's callback really drops the initiator's unicast is bench
  item 2 and nothing else.
- **One screen, one process.** A screen is a file-scope singleton, so
  `tests/test_link_screen.cpp` drives ONE LINK screen against a real caller-owned `Session` on
  the other end of a loopback. Both devices being real screens is a bench fact.
- **BLE is still not deleted and `FEATURE_BLE` is still untouched**, for the reason P7-C1
  gives: the deletion is gated on two boards having linked, and nothing here can link two
  boards.

### 8. Mutation testing — twenty source mutations and three gate mutations

Every one was planted in the shipping tree, built, and required to make a NAMED
test or a NAMED gate fail. A build that did not compile is reported as BUILD FAILED
and never as a pass, and every test binary runs under a 90-second leash so a
mutation that HANGS is a reported outcome rather than a stalled run.

| # | mutation | what failed |
|---|---|---|
| 1 | the browse opens a session as soon as a peer qualifies | HANGS — `test_link_screen` does not finish in 90 s. Consent moved into the frame loop makes the screen re-enter the whole handshake from the browse; the leash is what turns it into a result |
| 2 | hand off to the battle at `SS_HELLO` instead of `SS_VERIFY` | `a_session_that_was_never_accepted_never_reaches_a_battle`, `the_wait_for_a_peer_ends_at_the_ladder_and_not_a_second_later`, `a_lost_connection_offers_retry_and_exit_and_both_do_what_they_say`, and four more |
| 3 | `link_battle_status()` reports the ENGINE's outcome, not the session's | `a_win_the_peer_never_confirmed_pays_nothing` (2 checks) |
| 4 | `won_now()` reads the local engine for a linked battle | `a_win_the_peer_never_confirmed_pays_nothing` |
| 5 | a linked result is reported the moment the ENGINE finishes | `a_win_confirmed_a_rung_late_is_still_paid` — **and it was NOT CAUGHT until that case existed**: the session normally agrees while the transcript is still playing, so the guard is only reachable when the peer's `BATTLE_END` is delayed. Blocking exactly one of them by name is what opens the window |
| 6 | a linked entry wipes the `BattleState` the session built | six cases, including `no_frame_of_a_linked_battle_allocates_and_the_waiting_mode_is_drawn` |
| 7 | the local side is hard-coded to 0 again | `the_higher_device_id_plays_side_one_and_still_sees_its_own_team` — **NOT CAUGHT until `battle_screen_side()` existed.** Every other assertion in the file passed with the peer's team drawn as the player's, because the session still routed the action to the right side; the wrong half was only on the PANEL |
| 8 | `battle_leave()` no longer hands the link back | `going_home_from_a_linked_battle_pays_nothing_and_gives_the_radio_back`, `the_radio_is_released_exactly_once_on_every_way_off_this_screen`, `two_devices_...` |
| 9 | the linked submit also writes the engine directly | four cases: the double write is `BR_ALREADY_SUBMITTED` and the session dies |
| 10 | the round transcript is never re-opened | `two_devices_that_both_consent_fight_one_battle_reported_exactly_once` — **NOT CAUGHT until the case watched the transcript's own ROUND LABEL.** The 48-entry ring only overflows in a long fight; the label is wrong from round 2 |
| 11 | consenting cancels the browse instead of holding it | `consenting_stops_the_browse_and_keeps_the_radio` |
| 12 | `link_leave()` tears the link down on the way to the battle | eight cases |
| 13 | an operation the peer cannot do is offered anyway | `an_operation_the_peer_cannot_do_is_refused_by_name_and_opens_nothing` |
| 14 | the peer list offers an UNQUALIFIED peer | `only_a_peer_heard_often_enough_and_loudly_enough_can_be_picked` — **NOT CAUGHT until the case consented to row 0 and asserted the BOUND SLOT.** Counting qualified peers is not the same as mapping a row to one, and the count came from a different function than the row |
| 15 | `link_service()` ignores `link_hold()` and goes on browsing | `a_held_job_stops_browsing_and_keeps_the_radio` (4 checks) |
| 16 | GATE: a second module opens a peer session | `GATE FAIL: a peer session is opened outside ui/screen_link.cpp (1)` |
| 17 | GATE: the battle screen reads the reward flag itself | `GATE FAIL: session_rewards_authorised() is read outside ui/screen_link.cpp (1)` |
| 18 | GATE: the battle screen includes the LINK screen | `GATE FAIL: ui/screen_battle.cpp includes the LINK screen or a networking header (1)` |

**FOUR OF THEM WERE NOT CAUGHT ON THE FIRST RUN, and that is the useful half of
this table.** Numbers 5, 7, 10 and 14 all passed a green suite, and each one was
green for the same kind of reason: the assertion that would have caught it was
about a NEIGHBOURING fact. The tests were widened - a case that delays the
peer's confirmation, a query for the side the screen is actually drawing, a
watch on the transcript's own round label, and an assertion on which peer a
consent binds - and only then did the mutations fail.

**AND ONE MUTATION FAILED NOTHING BECAUSE THE CODE WAS WRONG, NOT THE TEST.**
`link_screen_busy()` read `link_is_busy(job) || (session live)`, and deleting
the second half broke nothing - because a session only ever exists over a job
`link_hold()` left `LS_RUNNING`, so the right-hand side could not answer true
where the left answered false. That is a sentence wider than the tree, so the
sentence was narrowed to `link_is_busy(s_job)` with the dependency on
`link_hold()` written beside it, rather than the test being stretched to cover
an unreachable branch.

---

## P7-C4/C5 — the atomic trade, breeding, and the god-taint gate

### The mutation table

Every row was applied to the tree, the suite was run, and the named case is what
went red. **Two of them failed nothing on the first run, and both times the
answer was to widen the test rather than to accept the green.**

| # | mutation | what failed |
|---|---|---|
| 1 | `W3` (the COMMIT record) is never written to flash | `a_power_cut_at_every_single_flash_write_leaves_the_box_whole` (5), `a_power_cut_inside_the_resolver_is_finished_by_the_next_boot` (55), `the_journal_names_the_id_the_incoming_pebble_will_have_here` (6), and two more |
| 2 | the incoming id is minted at APPLY time instead of before `W3` | six cases, including both wire cases — a replayed COMMIT mints a different id and the resolver cannot tell "done" from "not started" |
| 3 | the incoming presence test is dropped (`if (!box_id_in_use(in.id))` → `if (true)`) | the sweep (11) and the resolver sweep (14): a replay files the Pebble twice |
| 4 | a record below COMMIT rolls FORWARD instead of back | `a_power_cut_while_the_journal_is_being_opened_rolls_the_trade_back` (6) and two more |
| 5 | a COMMIT record rolls BACK instead of forward | four cases (24 checks) |
| 6 | a write that did not land is IGNORED (every `if (!write) return TDR_STORE` deleted) | **NOT CAUGHT.** See below |
| 7 | the quarantine rule is dropped from the offer path | `the_pebble_you_are_holding_is_not_for_sale_and_every_refusal_is_named` |
| 8 | the god-taint gate is dropped from the accept path | `a_tainted_pebble_cannot_enter_a_clean_dynasty_through_a_trade`, `a_tainted_offer_is_refused_on_the_wire_by_name_and_moves_nothing` |
| 9 | `commit_all()` stops writing the journal (the pre-existing defect, restored) | `the_checkpoint_recovery_path_no_longer_leaves_a_stale_journal_behind` |
| 10 | `maybe_ask_player()` is not called from `on_offer()` | `a_ready_that_arrives_before_its_own_offer_still_reaches_the_player` |
| 11 | the ladder's `SESSION_REQUEST` drops the operation byte | `the_retransmitted_session_request_still_says_which_operation_it_is` |
| 12 | consent is not required to apply (`local_accept` no longer checked) | four cases, both files |
| 13 | COMMIT no longer implies its sender's CONFIRM | `a_commit_frame_carries_its_senders_confirm_and_that_is_what_saves_the_pair` |
| 14 | the responder ignores `SESSION_REQUEST.rules` | `a_battle_session_and_a_trade_session_refuse_each_other_by_name` |
| 15 | the trade offer skips `game/trade.cpp`'s rules entirely | `the_pebble_the_player_is_holding_is_never_the_one_put_on_the_wire` (6) |
| 16 | walking away leaves the journal behind | `a_trade_the_local_player_never_accepts_moves_nothing_and_clears_its_journal` |
| 17 | a completed trade is reported as a lost link | `two_players_who_both_press_a_swap_one_pebble_each_and_the_box_says_so` |
| 18 | the session is opened as a BATTLE whatever the player picked | two cases (12 checks) |
| 19 | the second A is not required (the review auto-accepts) | two cases (17 checks) |
| 20 | the envelope clamp is deleted | `ten_thousand_bred_pairs_never_leave_the_genesis_care_envelope` |
| 21 | the `stage >= 1` rule is deleted from breeding | the 36 × 36 compat matrix, by species pair and by exact code |
| 22 | the `compat_group` rule is deleted | the same matrix |
| 23 | the taint gate is deleted from breeding | `a_clean_dynasty_refuses_a_tainted_parent_and_a_tainted_one_accepts_anything` |
| 24 | the offspring's family always comes from parent A | `the_offspring_is_the_base_stage_of_a_parents_family_and_passes_the_validator` |
| 25 | breeding reseeds `RNG_BREEDING` instead of installing a scripted source | `breeding_never_reseeds_or_consumes_the_shared_breeding_stream` |

### Mutation 6 was not caught, and a reboot is why

Deleting every `if (!write) return TDR_STORE` from `game/trade.cpp` left the
whole kill sweep **green**. The reason is not a weak assertion, it is the shape
of the experiment: once the fake store is dead, the RAM changes never reach
flash either way, so a reboot cannot tell "stopped at the failure" from "carried
on regardless". The difference is only visible **without** the reboot — a device
whose NVS page went bad and which keeps running — so
`a_write_that_did_not_land_stops_the_sequence_where_it_failed` asserts the two
things a reboot hides: the call reports `TDR_STORE` rather than `TDR_OK`, and
RAM stopped exactly where the write failed (a table of five cut points with the
expected Box state at each). With that case in place, mutation 6 fails by name.

### The two defects the lossy arm found, and why one arm would not have

Both are in `docs/protocol.md` §25 with their numbers. The shape is worth
repeating here because it is now the third time this project has found it: **an
endpoint deciding from the arrival that happened to be last instead of from its
own state.** P4-C5 found it in `on_action_result()`; P7-C4 found it twice more,
in `maybe_ask_player()` and in the ladder's regeneration of `SESSION_REQUEST`.
Neither shows on a clean link: 0 of 200 clean trials failed, 74 and 34 of 200 at
10 % drop. **A lossy arm that runs one fault level is a lossy arm that can
hide a defect at another**, which is why the case reports a table of four.

### A named reject nothing can return is a name, not a rule

`TDR_LAST_PEBBLE` was written, measured to be unreachable and deleted:
`box_active()` runs `mask_sync()`, which repairs an `active_slot` pointing
nowhere to the lowest occupied slot, so a Box holding one Pebble always reports
that Pebble as active and `TDR_ACTIVE` answers first. The same test was applied
to `BRD_UNKNOWN_SPECIES` in breeding, which WAS unreachable in the order first
written (`validate_pebble()` answers `VR_UNKNOWN_SPECIES` before the species
lookup could) — there the fix was to reorder the two checks so the code has a
producer, because "this content pack does not carry that species" deserves its
own word.

### One test that cannot fail is shipped, and it says so

`the_battle_variation_ceiling_is_structural_and_this_case_says_so` asserts that
a bred genome's stat variation stays in 0..2. It **cannot fail**:
`gvar(v) = v*3/16` folds 0..15 to 0..2 by construction and `game/validate.h`
rule (b) already says a gene-range check is a guard nobody can break. It is here
so the claim has an owner and a number in the output; the ceiling that CAN be
broken is the genesis envelope, and the case beside it breaks it deliberately in
a control arm (200 dynasties × 40 generations, unclamped: **8,655 escapes**,
clamped: **0**) so the clamp is guarding something measurable.

---

## Phase-7 exit (P7-C6, 2026-09-05)

**Variant matrix.** `tools/build_matrix.sh` compiles all seven feature variants with
`--warnings all` and fails on any warning pointing into the sketch. Result at the
`v0.7.0-social` tag, every variant at **0 project warnings**:

| Variant | Overrides | Flash (B) | Static RAM (B) | Δ flash vs 0.6.0-activity | Δ RAM vs 0.6.0-activity |
|---|---|---|---|---|---|
| baseline | — | 1,994,460 | 80,492 | +45,658 | +6,912 |
| no-ble | `FEATURE_BLE=0` | 1,281,842 | 56,996 | +45,402 | +6,912 |
| no-web | `FEATURE_WEB=0` | 1,936,472 | 78,444 | **+616,358** | **+25,824** |
| no-god | `GOD_MODE_ENABLED=0` | 1,981,944 | 80,332 | +45,508 | +6,928 |
| sh1106 | `DISPLAY_IS_SH1106=1` | 1,994,460 | 80,492 | +45,658 | +6,912 |
| all-off | every `FEATURE_*`=0 + `GOD_MODE_ENABLED=0` | 578,868 | 26,388 | +31,594 | +1,064 |
| **release** | `GOD_MODE_ENABLED=0 FEATURE_BLE=0` (D2) | **1,269,468** | **56,820** | +45,258 | +6,896 |

**THE `no-web` ROW IS NOT A PHASE-7 FEATURE AND MUST NOT BE READ AS ONE.** `net.cpp`'s
`NT_NET_WANT_WIFI` was `(FEATURE_WEB)` and is now `(FEATURE_WEB || FEATURE_ESPNOW)`, because
ESP-NOW is a Wi-Fi consumer that needs no web server. That variant therefore links the whole
Wi-Fi driver it used to compile out, and it has stopped being the "no Wi-Fi at all" build it
used to be. `all-off` is now the only variant with no radio in it, and `all-off` moved
(+31,594 / +1,064) because the LINK screen is in the screen table and drags the session, the
codec and the peer table with it — what `FEATURE_ESPNOW 0` removes is the RADIO, not the
screen: the driver compiles to refusal stubs, `link_start()` answers false and the screen
says "Radio no disponible" rather than hanging.

The baseline sits at **83.1 % of `GATE_FLASH_MAX`** (405,540 B free) and **89.4 % of
`GATE_GLOBALS_MAX`** (9,508 B free); the release build — the one that ships — is at
**79.3 % of `GATE_RELEASE_FLASH_MAX`** and **87.4 % of `GATE_RELEASE_GLOBALS_MAX`**, and
**63.4 %** of the 3,145,728 B `app0` slot (release: **40.4 %**).

**Where the phase-7 cost landed.** Per commit, each delta the difference of two adjacent
commits' own recorded `flash=`/`globals=` lines; `git log --oneline v0.6.0-activity..HEAD` is
exactly four commits and the four sum to the table above:

| commit | chunk | flash | globals | after |
|---|---|---|---|---|
| `3cd481f` | P7-C1 ESP-NOW behind the §59 seam | **+9,758** | **+5,832** | 1,958,560 / 79,412 |
| `837223b` | P7-C2/C3 LINK screen, consent, linked battle | **+26,520** | **+928** | 1,985,080 / 80,340 |
| `f8f2e51` | P7-C4/C5 atomic trade + breeding | **+8,874** | **+144** | 1,993,954 / 80,484 |
| this commit | P7-C6 exit: the blocking find, seven debts | **+506** | **+8** | 1,994,460 / 80,492 |
| | **phase 7** | **+45,658** | **+6,912** | |

**The exit's own +8 B of globals, attributed** (`riscv32-esp-elf-nm -S` over the release
`.elf`): `s_tick_lost_s` 4 + `s_tick_stalls` 2 = **6 B of symbols**, plus 2 B of link
alignment. `save_pebble_now()`, `act_adopt()` and the narrowed `sim_gain_restore()` add no
state at all; the +506 B of flash is those three functions and `pwr_tick_budget()`.

**AND EVERY ONE OF THEM IS IN THE RELEASE ARTEFACT, CHECKED RATHER THAN ASSUMED.**
`riscv32-esp-elf-nm -C` over the `release` `.elf` finds `save_pebble_now`, `act_adopt`,
`pwr_tick_budget` and `sim_gain_restore` as text symbols. That check is here because of what
the phase-6 exit found — the shipping build never advanced game time for four phases, because
a default lived inside a dev-only function — and a fix that is not in the shipping image is
not a fix.

### The blocking finding this exit opened with: a successful trade destroyed a Pebble

**On the SHIPPING build, every clean trade lost a Pebble, and the boot resolver carried the
identical defect through the identical shim.** `game/trade.cpp`'s apply step writes B1 (clear
the outgoing slot) then B2 (file the incoming one), and `box_add()` fills `first_free()` —
which is the slot B1 just released. So B1 and B2 write **the same key microseconds apart**.
`save_manager.cpp`'s `save_pebble()` DEFERS a second write of one key inside
`SAVE_MIN_GAP_MS` (1,000 ms) — `force` does not bypass that branch — **and returns true**.
The two P7-C4 shims returned that `true` straight back as `TradeStore.write_slot`, whose
contract in `game/trade.h` is literally "every function returns whether the bytes LANDED". B2
therefore never reached flash, B3 wrote the Box header over it and W4 cleared the journal to
IDLE, so nothing was left to repair it.

**Reproduced independently before it was fixed, and it is worse than the report said.** With
`app/app.cpp`'s own clock wiring bound in `tests/test_trade.cpp` — one line,
`save_set_clock(&clock_ms, &clock_epoch)` instead of `save_set_clock(nullptr, ...)` — the
CLEAN trade case fails with **no fault injection at all**: the Box holds 2 Pebbles instead of
3, `traded == 0`, and the trade costs **9 flash writes instead of 10**. The 15-point kill
sweep then fails at every cut point past the third.

**THE FIXTURE IS WHY IT SURVIVED 27,819 CHECKS, AND THAT IS THIS PROJECT'S NAMED PATTERN
WITH THE POLARITY INVERTED.** `save_set_clock(nullptr, ...)` switches OFF `save_manager.cpp`'s
entire wear-filter branch (`if (s_now_ms && s_have_written[slot])`). A 15-point kill sweep, a
9-point resolver sweep and an 800-trial lossy table all ran against a `save_manager` **the
release artefact does not execute**. Phase 6 found a default living inside a dev-only path;
here the DEFAULT is the safe one and the SHIPPING wiring is the dangerous one.

**The fix is a second entry point, not a widened flag.** `save_pebble_now()` writes with no
filter and no deferral and returns whether the bytes landed — there is no third answer.
`force=true` was deliberately NOT widened to mean this: `persistence/game_state.cpp` calls it
on every care action and RELIES on the deferral for flash wear. Two gates now hold it:
`save_pebble()` may not be called from `app/` or `ui/` at all, and no host test may bind
`save_set_clock(nullptr, ...)`. Both were proven to bite.

### Where the trade's atomicity claim stands after that

**Within one device it is proved, and now proved against the clock the device really runs.**
The sweep cuts the power at every flash write of the sequence, at the four that open the
journal and at nine points inside the resolver, each followed by a reboot, a resolve, the
pair invariant and a second reboot that must change nothing — with a real millisecond clock
bound and `tests/fakes/kv_mem.cpp`'s `kv_erase()` now also refusing once the store is dead
(it was not, so a swept "power cut" could still erase checkpoint slots after the device was
supposed to be gone — a fault model that refuses writes and permits deletes is not a power
cut).

**Across the pair it is still bounded and measured rather than proved**, exactly as
`game/trade.h` states: 800 trials over four fault arms, 0 split. The cross-pair rendezvous is
carried to phase 8.

### D2's OUTCOME: ESP-NOW is implemented and UNPROVEN ON HARDWARE

D2 chose ESP-NOW on paper on 2026-09-02. Phase 7 built it. **It has never run on a board, so
the decision is implemented but not validated, and the deletion D2 authorises is still
pending.**

| D2 clause | Outcome at v0.7.0-social |
|---|---|
| ESP-NOW behind the §59 `Transport` seam | **DONE and demonstrated.** `networking/transport_espnow.cpp` is the only file in the tree that includes `esp_now.h`. `networking/session.cpp` took the radio with no widening, no new field and no `#ifdef`, and a gate fails the build if it grows one. |
| The game never learns which transport carried a packet | **DONE and gated.** `tests/test_link_transport.cpp` runs a whole battle between two real sessions over a `Transport` whose `recv()` **is** `rxring_pop()`. |
| No stack flip during a link session | **DONE.** ESP-NOW rides the same `WIFI_STA` residency the §40 scanner needs; `espnow_end()` is inside `wifi_down()` so it cannot outlive the driver. |
| Same-channel constraint, "the beacon announces the channel and P7-C1 re-tunes" | **NARROWED, and the narrowing is a cost.** The beacon carries no channel field and there is no re-tune path: both devices apply `PB_LINK_CHANNEL` on every LINK bring-up, so a peer on another channel is not discoverable at all rather than discoverable-and-unreachable. A device left on another channel by something outside this firmware is invisible to LINK, and this firmware cannot tell that from an empty room. |
| The Bluedroid leak no longer needs measuring | **CORRECT — it was never measured, and it did not need to be.** |
| **`FEATURE_BLE 0` in the release build; `ble_social.cpp` becomes a removal candidate in P7-C1** | **NOT DONE, DELIBERATELY, AND THIS IS THE ONE OPEN CLAUSE.** See below. |

**THE BLE DELETION IS BLOCKED ON A BENCH TEST, AND THE ORDERING IS THE WHOLE POINT.**
`ble_social.{h,cpp}`, `FEATURE_BLE` and `core/nt_types.h`'s `BlePeerInfo` are byte-identical
across all four phase-7 commits. ESP-NOW was chosen on paper and has never linked two boards;
deleting the only fallback before the chosen transport is demonstrated is backwards, and no
amount of host testing can substitute — a host binary is single-threaded and is not evidence
about the Wi-Fi task, the channel, modem sleep, or a real receive callback.

**RE-MEASURED AT THIS TAG rather than re-quoted, because the headroom argument rests on it:**
deleting BLE recovers **712,618 B of flash and 23,496 B of static RAM** (baseline minus
`no-ble`: 1,994,460 − 1,281,842 and 80,492 − 56,996). The figure the tree has been quoting
since P4-C6 is **712,466 / 23,504** — a drift of 152 B of flash and 8 B of globals over three
phases, immaterial to the decision and corrected here so the next reader measures rather than
inherits.

**WHAT THE OWNER MUST RUN TO UNBLOCK IT, in one sentence:** flash two boards, open LINK on
both, and confirm **each board lists the other** — which needs `link_qualified_count()` to
reach 1, i.e. three beacons at 500 ms above `LINK_RSSI_MIN` (−70 dBm), within
`LINK_JOB_TIMEOUT_MS` (90 s). If both boards list each other, `ble_social.{h,cpp}` may be
deleted, `FEATURE_BLE` removed, `no-ble` promoted to the baseline and `BlePeerInfo` taken out
of `core/nt_types.h`. If they do not, **run bench item 1 first** (the modem-sleep check) —
`WIFI_PS_MIN_MODEM` is the C3 default, it is applied at STA start,
`CONFIG_ESP_WIFI_STA_DISCONNECTED_PM_ENABLE=y` makes it bite an UNASSOCIATED station, and
`net.cpp`'s `scan_begin()` sets the cached value to true in a static that survives a mode
change and `net_request(RADIO_OFF)`. `link_begin()` calls `WiFi.setSleep(false)` BEFORE
`WiFi.mode(WIFI_STA)` for exactly that reason; a materially lower beacon count after a
NETWORK scan means the call is missing or ordered wrong.

### The seven carried debts, and what happened to each

The phase-6 exit carried six bullets, one of which held two independently measured items.
None was dropped.

| # | Debt | Verdict | What was done |
|---|---|---|---|
| D1 | `sim_gain_restore()`'s typed-clock hole | **DEFECT — FIXED** | The `elapsed * cap / 3600` term is gone. Both epochs are wall clocks a player types on the time screen, and one hour of "elapsed" refilled a whole cap: 100 rounds of (clock +1 h, reboot) from a spent ledger manufactured **4,000** happiness gain points against a cap of 40. Same shape `xp_ledger_restore()` lost at P6-C4, closed the same way. |
| D2 | A Pebble at `XP_LEVEL_MAX` earns no activity happiness | **DEFECT — FIXED** | `meter_take()` moved above the top-of-curve early return in `xp_add()`. Measured before: level 29 paid 5,500 milli of the 5,500 owed, level 30 paid **0**. |
| D3 | No test for `gt_mono_ms()`'s choice of clock | **COVERAGE GAP — CLOSED** | `tests/fakes/arduino/` (two headers, one symbol each) + `gametime_arduino.o` + `tests/test_clock_device.cpp`: 4 cases driving the DEVICE branch across a wake that zeroes uptime while the RTC keeps counting. |
| D4 | Nothing counts a dropped tick | **DIAGNOSTICS GAP — CLOSED** | `pwr_tick_budget()` in `hardware/power.cpp` (so a host binary can drive arithmetic `app/app.cpp` cannot compile), two saturating counters on the ENERGIA page, and a case that asserts BOTH halves — the counter moved AND exactly one second was charged. |
| D5(a) | `CooldownTable.act_day` never validated on load | **DEFECT — FIXED** | `act_adopt()`, called once at boot. Measured: ten simulated years score **146,000** points from an honest table and **440** from one carrying `act_day = 0xFFFF`, because `open_day()`'s only roll condition is `day > t.act_day` and a u16 holds 65,535 while the widest day a u32 epoch can name is 49,710. |
| D5(b) | The environment is sampled once per `logic_tick()` | **DOCUMENTATION** | Worst measured care difference over half an hour at `EPOCH0`: **0 milli-points**. `app/app.cpp` now says so beside `sim_tick(step)`; the fix would be a broken-down-time conversion sixty times a minute for a number nobody can see. |
| D6 | A deep-sleep rung would need `app_loop()` changed too | **DOCUMENTATION** | The paragraph now sits directly under `hardware/power.h`'s `static_assert(!PWR_DEEP_WAKE_BOTH_BUTTONS)`, so the person who trips the tripwire opens both files. |

### The exit's mutations

Every fix above was mutation-tested; each mutation made a NAMED case or a NAMED gate fail.

| Mutation | What failed |
|---|---|
| The trade's slot shim returns `save_pebble(..., true)` | `a_clean_trade_moves_exactly_one_pebble_each_way_and_clears_its_journal` (4), `a_power_cut_at_every_single_flash_write_leaves_the_box_whole` (46), `a_power_cut_inside_the_resolver_is_finished_by_the_next_boot` (32), `a_write_that_did_not_land_stops_the_sequence_where_it_failed` (7), and four more |
| `save_pebble_now()` honours the wear floor | `save_pebble_now_reaches_flash_twice_inside_the_floor_and_never_says_it_did_not` (5) + the whole trade sweep |
| `save_pebble_now()` does not cancel a pending deferral | `save_pebble_now_reaches_flash_twice_inside_the_floor_and_never_says_it_did_not` (1, on the put COUNT) |
| GATE: `ui/ui.cpp`'s shim back to `save_pebble()` | `GATE FAIL: save_pebble() is called from app/ or ui/ (1)` |
| GATE: a host test binds `save_set_clock(nullptr, ...)` | `GATE FAIL: a host test binds save_set_clock(nullptr, ...) (2)` |
| `sim_gain_restore()` ages the snapshot forward again | `a_typed_hour_and_a_reboot_cannot_refill_a_spent_gain_budget` (202) |
| `xp_add()` returns at the top of the curve before `meter_take()` | `a_pebble_at_the_top_of_the_curve_still_earns_its_activity_happiness` (2), `the_meter_is_spent_at_the_top_of_the_curve_for_every_metered_source` (8) |
| `act_adopt()`'s bound is 60,000 instead of `ACT_DAY_MAX_INDEX` | `a_day_index_beyond_the_clock_freezes_the_score_and_the_boot_clamp_removes_it` (2) |
| `act_adopt()` clamps the day and keeps the score | the same case (1) |
| `pwr_tick_budget()` does not count the stall | `a_gap_wider_than_the_bound_charges_one_second_and_says_it_lost_the_rest` (3) |
| `pwr_tick_budget()` charges the whole stall | the same case (2) |
| `pwr_tick_budget()` counts how often, never how much | the same case (3) |
| `gt_mono_ms()`'s device branch reads `millis()` | all four `test_clock_device` cases — **and the grep gate too** |
| **`gt_mono_ms()` divides the RTC by 1,000,000 instead of 1,000** | **`GATE OK` — and all four `test_clock_device` cases fail.** This is the one that says why the test was worth building: a grep can prove the right identifier is in the right function and can never prove the clock behaves. |
| `session.cpp`'s `SS_SESSION` arm drops its `SR_INITIATOR` clause | `a_responder_refuses_a_session_accept_and_never_adopts_its_nonce` (4) — a guard that had NO test before this exit, and whose deletion left all 46 binaries green |

### Three things found in review that were recorded rather than swept

- **`networking/session.cpp` DID change in phase 7, and no earlier sentence says it plainly.**
  P7-C1's headline — "session.cpp and battle_link.cpp are byte-identical" — is true of P7-C1
  and stays true through P7-C3. **P7-C4 changed it** (+86/−3 lines, `session.h` +46/−6) to
  carry the trade as a second `SessionOp`: `SESSION_REQUEST.rules` and `SESSION_ACCEPT.op_echo`
  became real fields. The LOAD-BEARING claim is intact and gated — every added line was grepped
  for `esp_now|esp_wifi|WiFi|arduino|#if|espnow|radio|rxring|driver` and matched none — but
  "the radio chunks did not touch it; the second OPERATION did" is the accurate sentence.
- **`SessionEnd.detail` has FOUR readings, not three.** `session.h` said "a `VReject` for
  `SE_REJECTED`"; `networking/trade_link.cpp` closes `SE_REJECTED` with the `SessionDetail`
  `SD_TRADE_REFUSED` at three of its five sites. The two enums collide — `SD_TRADE_REFUSED`
  is 22 and `VReject` 22 is `VR_BAD_CARE` — so a reader following the header would print "a
  care value outside range" for a god-taint refusal. Nothing in the product reads
  `end.detail` today, so it is a trap for a log reader rather than a live defect; the header
  now says all four readings.
- **A pathspec trap for the next verifier.** The source tree is nested at
  `Pebblebol/Pebblebol/src/`, not `Pebblebol/src/`. `git diff v0.6.0-activity HEAD --
  src/networking/session.cpp` matches nothing and **exits 0**, which reads exactly like "the
  seam held". Any exit check written with the short path is a test that cannot fail.

### What phase 7 did NOT measure, stated as unmeasured

**No radio ran, and this is a larger caveat than in any previous phase**, because phase 7's
whole subject is two devices talking. Unobserved: whether two boards see each other's beacon
at all; the modem-sleep default on an unassociated station; the channel after a §40 scan;
ESP-NOW's own duplicate suppression and MTU enforcement; the receive callback's threading and
preemption (the SPSC claim in `rxring.h` rests on discipline and on two precedents, and a
host binary is single-threaded); the radio half of the consent gate (the loopback delivers
regardless of binding, so "an un-bound device's callback really drops the initiator's
unicast" is bench item 2 and nothing else); and a flash erase interrupted mid-page — a
`kv_mem` "power cut" is a fake store returning false, not a partially-programmed NVS page, so
the trade's within-device claim is if anything understated in one direction and untested in
that one.

**§67's "Local multiplayer works" and P7-C1's "two boards see each other's beacon in LINK"
are therefore UNTICKED**, and the plan carries an eleven-item bench list in the order to run
it. Everything else ticked in phase 7 is ticked on the host standard every earlier phase used.
