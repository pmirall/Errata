# PEBBLEBOL_IMPLEMENTATION_AUDIT.md

| Field | Value |
|---|---|
| Title | Pebblebol repository audit (spec §69, Phase 1 "repository archaeology" of §52) |
| Date | 2026-09-02 |
| Status | Audit complete. No source file was modified. Build verified today. |
| Source commit | `b53cfe4` ("first commit", the only commit; `docs/` is untracked) |
| Spec | `/home/user/Pebblebol/docs/PEBBLEBOL_PRODUCT_SYSTEM_SPEC.md` (2,708 lines) |
| Firmware audited | `/home/user/Pebblebol/sketch_aug30b/` — "Nottamagochi" `FW_VERSION "1.0.0"` (config.h:82-83), 26,703 lines in 16 `.cpp` + 1 `.ino` + 21 `.h` |
| Build | `arduino-cli compile --fqbn esp32:esp32:esp32c3:PartitionScheme=huge_app,CDCOnBoot=cdc --warnings all` — core esp32 3.1.1, U8g2 2.35.30; 2,105,548 B flash (66 % of 3,145,728), 72,748 B static RAM (22 % of 327,680), 0 project warnings |
| Hardware run status | Never executed on a physical board (README.md:801, CHANGELOG.md:193) |
| Companion document | `PEBBLEBOL_IMPLEMENTATION_PLAN.md` — **not yet written** (spec §69 second deliverable, to be produced next; absent from the repo as of this date). §20 here gives the phase-level plan |

## Executive summary

1. The repository is a flat Arduino sketch (`sketch_aug30b/`, one commit) implementing a Tamagotchi-style single pet with death, lineage, weather, Telegram, BLE mating, a phone web page with three browser games and a hidden god-mode console; none of the product-defining Pebblebol systems (Box, species/instance split, battle, encounters, capture, XP/levels, creator) exist yet.
2. The hardware is exactly what the spec expects: ESP32-C3 SuperMini, SSD1306 128x64 I2C OLED (SH1106 switchable), two buttons to GND, NVS on the 4 MB flash, one shared 2.4 GHz radio. There are no sensors of any kind beyond the buttons: no IMU, no buzzer, no battery ADC populated, no SD.
3. **Unresolved blocker: the GPIO map in `config.h:92-96` (SDA=8, SCL=9, BTN_L=10, BTN_R=2, LED=5) contradicts every document and the render module's own safety comments (SDA=6, SCL=7, BTN_L=3, BTN_R=4, LED=8) and places I2C and a button on ESP32-C3 strapping pins. This needs a decision from the board owner; it is reported here, not resolved.**
4. The layering the spec wants already exists in substance: `sim.cpp` has no hardware/clock/RNG, `render.cpp` is the only U8G2 owner, `net.cpp` the only radio owner, `storage.cpp` the only NVS owner, `gametime.cpp` the only libc-time owner. Only the physical file layout (flat, `.ino`-bound) and three layering inversions (petfx→sim, ui→webui, ui→godmode) deviate from §5.
5. Directly reusable, low-effort: NVS persistence with CRC/version/boot-kind classification, the frame scheduler and drawing primitives, the two-button gesture recogniser, the integer milli-point care integrator with offline catch-up, the QR encoder, the PIN-gated rate-limited web server skeleton, the BLE advert/scan plumbing and lock-free RX ring, the god-mode entry/marker/taint pattern.
6. Obsolete by spec mandate (§68 rules 4-6, 14, 15): weather (Open-Meteo + ip-api geolocation), Telegram (the only TLS user), the always-on Wi-Fi policy, death/memorial/lineage-grade, absence punishment ladder, the three browser minigames and the 47 KB phone page. Measured removal dividend: weather+Telegram alone give back 123,834 B flash and 3,208 B static RAM and eliminate the 1-8 s TLS stall in `loop()`.
7. The persisted wire formats are frozen by `static_assert`: `PetSave` is exactly 128 B with 2 spare bytes, `Config` 256 B with 3 spare bytes; a foreign version is refused and silently replaced by a fresh generation-0 pet (`sketch_aug30b.ino:279-283`), which violates §48 and §68 rule 14. A Pebble/Box schema needs `SAVE_SCHEMA_VERSION` + migration before anything else is stored.
8. Static RAM is 72,748 B of which project code is only 8,895 B; the BLE stack alone is 23,688 B static (721,632 B flash) and Wi-Fi/lwIP/PHY another ~25 KB. Running both stacks resident is not viable on this budget; the existing single-stack `RadioMode` invariant must survive into Pebblebol.
9. There are zero automated tests. Comments cite seven host harnesses and generators (`test_genome.cpp`, `test_qr.cpp`, `mkharness.py`, `t_adv.cpp`, `build.js`, `sprite_src.py`, `input` shim providers) that were never committed. `sim.cpp`, `genome.cpp`, `qr.cpp`, `gametime.cpp` and `input.cpp` compile on a host today, so a `tests/` tree is cheap to add and should be Phase 2's first deliverable.
10. Recommended path (§20): settle the pin map, remove the cloud modules and death, introduce the Pebble/Box schema with migration and host tests, then build care/XP/evolution on the existing integer engine, the battle engine as a pure module, Wi-Fi scanning as a new `NPH_SCANNING` phase in `net.cpp`, and pick the P2P session transport (ESP-NOW vs BLE) behind the §59 transport abstraction — the current 19-byte legacy-advert protocol cannot carry §15.

---

## 1. Repository structure

### 1.1 Tree with line counts

```
/home/user/Pebblebol                                  (git: 1 commit b53cfe4; .gitignore = "*.bak")
├── .gitignore                                    1
├── 00-Investigacion-inicial.docx                 —   (binary, 6,536 B, tracked; pre-firmware research note)
├── docs/                                             (UNTRACKED)
│   └── PEBBLEBOL_PRODUCT_SYSTEM_SPEC.md       2708   (45,890 B) the new master spec
└── sketch_aug30b/                                    flat Arduino sketch, folder name == .ino name
    ├── sketch_aug30b.ino                       545   entry: setup()/loop(), radio policy, 1 Hz tick, Config CRC detector
    ├── config.h                                672   every constant (Spanish user block 15-76, then 16 English sections)
    ├── nt_types.h                              653   all shared PODs/enums; PetSave/Config/Genome wire formats
    ├── strings_es.h                           1066   439 Spanish UI strings + StrId enum + S_* range macros
    ├── sim.cpp / sim.h                  2149 / 250   care simulation (only mutator of PetSave)
    ├── genome.cpp / genome.h             676 / 206   16 B genome, breeding, CRC, hex serialisation
    ├── storage.cpp / storage.h           993 / 228   NVS "notta", RTC_NOINIT nonce, boot classification
    ├── gametime.cpp / gametime.h         366 / 125   wall clock, SNTP, TZ, elapsed formatter
    ├── input.cpp / input.h               351 /  66   two-button debounce + 8-gesture recogniser
    ├── render.cpp / render.h            1074 / 433   U8G2 owner, frame scheduler, text/dither/bar primitives, panel FX
    ├── sprites.h                              1426   generated XBM atlas (10,893 B) + lookup tables
    ├── petfx.cpp / petfx.h              1532 / 253   creature renderer: mirroring, blink, behaviour automaton
    ├── actfx.cpp / actfx.h              1161 / 173   9 care-action films
    ├── ui.cpp / ui.h                    3578 / 189   16-screen state machine, 3 minigames, death/hatch ceremonies
    ├── net.cpp / net.h                   734 / 162   RadioMode OFF/WIFI/BLE lifecycle, STA backoff, AP portal, mDNS
    ├── ble_social.cpp / ble_social.h    1010 / 201   advert-only BLE beacon + 3-frame mating handshake
    ├── webui.cpp / webui.h              1166 / 218   WebServer: 8 routes, PIN, rate limiter, browser minigames
    ├── index_html.h                             80   47,181 B PROGMEM phone page (generated, source not in repo)
    ├── qr.cpp / qr.h                     569 /  92   QR v1-4 ECC-L encoder + U8G2 renderer
    ├── weather.cpp / weather.h          1314 / 137   Open-Meteo + ip-api client + weather overlay   [obsolete]
    ├── telegram.cpp / telegram.h        1058 / 100   TLS guilt-message channel                     [obsolete]
    ├── godmode.cpp / godmode.h          1494 / 203   hidden developer console (12 commands)
    ├── README.md                               801   Spanish product/bring-up manual (60,761 B)
    ├── CHANGELOG.md                            197   Spanish (27,346 B)
    ├── ESTUDIO_VISUAL.md                       517   Spanish visual-polish study (24,987 B)
    └── docs/estudio_visual.html               1633   interactive twin of the study (71,782 B)
```

Totals: `.cpp` 19,225 lines, `.h` 6,933 lines, `.ino` 545 lines = **26,703 lines of code**; docs 1,515 lines of Markdown + 1,633 of HTML. Largest translation units: `ui.cpp` 3,578, `sim.cpp` 2,149, `petfx.cpp` 1,532, `godmode.cpp` 1,494, `sprites.h` 1,426, `weather.cpp` 1,314.

### 1.2 Flat layout vs spec §5

The spec's `src/{app,game,ui,minigames,networking,persistence,hardware,data,tests}/` tree does not exist; everything sits in one folder because arduino-cli requires the `.ino` to share the folder name (README.md:131-134). arduino-cli compiles `src/` recursively, so the move is mechanical. The module boundaries, however, already correspond to the spec's layers:

| Spec §5 folder | Existing file(s) | Fit |
|---|---|---|
| `app/App, GameLoop, InputRouter` | `sketch_aug30b.ino` setup()/loop() (:340-413, :470-545), gesture drain (:475-481) | direct |
| `app/StateMachine` + `ui/Screens, Menus, Dialog` | `ui.cpp` (16 `ScreenId`, nt_types.h:73-91; three parallel switches ui.cpp:3329, :3419, :3510) | one TU, needs splitting |
| `game/Care, Cooldowns, Evolution` | `sim.cpp` | direct (single-pet, must become re-entrant) |
| `game/Breeding` | `genome.cpp` | direct |
| `game/Pebble, PebbleSpecies, PebbleDatabase, Battle, BattleAI, Capture, Inventory, Box, XP, Items, Encounters, Exploration` | — | **missing** |
| `ui/Renderer` | `render.cpp` | direct |
| `ui/SpriteRenderer, PetRenderer` | `sprites.h`, `petfx.cpp`, `actfx.cpp` | adapt |
| `ui/BattleRenderer` | — | missing |
| `minigames/Minigame, MinigameManager, games/` | `ui.cpp:1478-1776` (3 games inline) | adapt |
| `networking/WifiScanner` | — (no `scanNetworks` anywhere) | missing |
| `networking/LocalProtocol, DeviceDiscovery, *Protocol` | `ble_social.cpp` (discovery yes; protocol is mating-only, 19 B payload) | adapt/replace |
| `networking/CreatorServer` | `webui.cpp` + `index_html.h` + `qr.cpp` | adapt |
| `persistence/SaveManager, SaveSchema, Migration` | `storage.cpp`, `nt_types.h` (schema), no migration | adapt |
| `hardware/Display, Buttons, Storage, Clock, WiFi, BLE` | `render.cpp`, `input.cpp`, `storage.cpp`, `gametime.cpp`, `net.cpp` | direct (no interfaces, but single owners) |
| `hardware/Motion, Audio, Power` | — (no hardware) | missing by hardware |
| `data/*` | `config.h` sections 7-9 (#define tables), `sprites.h`, `strings_es.h` | adapt to tables |
| `tests/` | — | **missing** |

---

## 2. Detected ESP32 variant

| Item | Finding | Evidence |
|---|---|---|
| Board | ESP32-C3 SuperMini (RISC-V RV32IMC single core, 160 MHz, 4 MB flash, 400 KB SRAM of which Arduino reports 327,680 B usable for globals+heap) | README.md:28-60; build output "Maximum is 327680 bytes" |
| FQBN used and verified | `esp32:esp32:esp32c3:PartitionScheme=huge_app,CDCOnBoot=cdc` | ESTUDIO_VISUAL.md:9-10; orchestrator build today |
| FQBN in README (drift) | `esp32:esp32:nologo_esp32c3_super_mini:PartitionScheme=huge_app` | README.md:131-134 |
| Core | esp32 3.1.1 (IDF 5.3 release `idf-release_v5.3-cfea4f7c-v1`) | README.md:104; sdkconfig path `.../esp32-arduino-libs/idf-release_v5.3-cfea4f7c-v1/esp32c3/sdkconfig` |
| Library | U8g2 2.35.30 | README.md:104 |
| CPU features | No FPU, no hardware 64-bit divide: sim's int64 `/` and `%` in `accum()`/`rate_chain()` (sim.cpp:167-197) are libgcc soft ops. Per 60 s sub-step (`sub_step` sim.cpp:1210): `gain_refill` ≤ 7 `accum()` (:270-276), `decay_stats` 7 `rate_chain()` with 2-7 multipliers each + 7 `accum()` (:824-870), `health_step` ≤ 5+1 `rate_chain()` + ≤ 5+1 `accum()` (:908-935), `sickness_step` 1 `rate_chain()` (:1004); each `accum()` costs one 64-bit `/` and one `%`, each `rate_chain()` one `/` per multiplier plus one → on the order of 100 soft 64-bit divisions per sub-step | sim.cpp:167-197, :270-276, :824-870, :908-935, :1004, :1210 |
| USB | Native USB-CDC (no UART bridge); `CDCOnBoot=cdc` mandatory or the port vanishes; `Serial.begin(115200)` never waits for a host | README.md:138-155; sketch_aug30b.ino:344 |
| Radio | One 2.4 GHz radio shared by Wi-Fi and BLE; firmware enforces exactly one resident stack via `RadioMode {RADIO_OFF, RADIO_WIFI, RADIO_BLE}` | nt_types.h:139-144; net.h:9-16 |
| BLE host | Bluedroid (`CONFIG_BT_BLUEDROID_ENABLED=y` sdkconfig:629); BLE 5.0 features compiled (`CONFIG_BT_BLE_50_FEATURES_SUPPORTED=y` :850); `CONFIG_BT_CTRL_BLE_MAX_ACT=6` (:860) | sdkconfig |
| RTC clock | Internal RC (`CONFIG_RTC_CLK_SRC_INT_RC=y` sdkconfig:1405, ~136 kHz); system `time()` is RTC-timer backed (`CONFIG_ESP_TIME_FUNCS_USE_RTC_TIMER=y` :1584) so it survives soft reset and deep sleep, drifts without a 32 kHz crystal | sdkconfig |
| Strapping pins | GPIO2 (must be high at boot), GPIO8 (LED_BUILTIN), GPIO9 (low at boot = download mode) | README.md:73-85; config.h:89-90 |
| Safe free pins per README | GPIO3, 4, 5, 6, 7, 10; GPIO11-19 not broken out | README.md:85 |
| Task WDT | 5 s (`CONFIG_ESP_TASK_WDT_TIMEOUT_S=5` sdkconfig:1549) | sdkconfig |
| Hardware validation | None: "Nada de este firmware se ha ejecutado nunca en una placa real" | README.md:801; CHANGELOG.md:193 |

Constraint that follows: any Pebblebol design must assume time-multiplexed radio (Wi-Fi scan **or** BLE, never both), integer-only game math, and a clock that is only calibrated when something sets it (today: SNTP over Wi-Fi only).

---

## 3. Display hardware

| Item | Finding | Evidence |
|---|---|---|
| Panel | SSD1306 128x64 1-bit OLED over I2C; SH1106 variant selectable with `DISPLAY_IS_SH1106` (default 0) | config.h:64; render.h:34-38 |
| Driver | `U8G2_SSD1306_128X64_NONAME_F_HW_I2C` full-buffer (1,024 B framebuffer inside U8g2), single file-scope instance | render.cpp:22; render.h:3-7 |
| Constructor | `(U8G2_R0, U8X8_PIN_NONE, /*clock*/PIN_SCL, /*data*/PIN_SDA)` — argument order is clock then data; render.cpp:14-18 warns that dropping the pins silently moves I2C to GPIO8/9 | render.cpp:12-22 |
| Bus | `Wire.begin(PIN_SDA, PIN_SCL, 400000)` before `u8g2.begin()`; 800 kHz "only after HW validation" | render.cpp:322; config.h:115 |
| Address | probe 0x3C then 0x3D, bus sweep 0x08..0x77 logged on failure, then continue at 0x3C; `rd_begin()` false → `.ino` calls `rd_fatal()` (noreturn LED blink) | render.cpp:317-394 (`rd_begin`; probe/sweep :326-345); sketch_aug30b.ino:351-353 |
| Frame cost | `sendBuffer()` ≈ 1,064 B I2C ≈ 24 ms at 400 kHz of a 50 ms budget (`FRAME_BUDGET_US 50000`) | render.h:295-300; config.h:155 |
| FPS policy | 20 normal / 4 low (asleep, energy < 15 %, or web client active 10 s) / 1 memorial; `rd_set_fps` clamps 1..60 | config.h:150-154; render.cpp:489-495; ui.cpp:595-611 |
| Layout contract | status bar rows 0-8 (`STATUS_BAR_H 9`), sprite band rows 9-55 (`SPRITE_AREA_H 47`), affordance strip rows 56-63 (`AFFORDANCE_BAR_H 8`); HUD keep-out columns 0-13 and 115-127 enforced by `static_assert` in petfx.h:88-89 | config.h:119-145; petfx.h:42-89 |
| Fonts | 5x8_tf 1,715 B, 6x10_tf 2,000 B, t0_11b_tf 2,095 B, 4x6_tr 723 B, logisoso16_tn 287 B | render.h:47-63 |
| Panel register FX | 0xA6/0xA7 invert (flash), 0xD3 display offset (shake, wraps mod 64), 0x81 contrast ramp/breathe, 0xAE/0xAF power save (`rd_power`, never called) | render.cpp:198-310 (`fx_*` tick/apply/settle; register writes :267, :277, :283, :304-305), :399-411 (`rd_power` → `setPowerSave`), :911-995 (`rd_flash/rd_shake/rd_contrast_ramp/rd_breathe/rd_fx_reset`) |
| Contrast | default 140, dim 40 (only when the pet sleeps); no inactivity display-off exists | config.h:116-117; ui.cpp:435-449 |
| Pin conflict | `config.h:92-93` SDA=8/SCL=9 vs README.md:62-63 GPIO6/GPIO7 — see §18 risk 1 | — |

Consequence for §63: text helpers already truncate on UTF-8 codepoint boundaries (render.cpp:115-197, :591-673), u8g2 unsigned coordinates are guarded by `px_*` helpers (ui.cpp:273-298), and the stage/HUD contract prevents body clipping. The 24 ms I2C floor is the hard per-frame ceiling for any battle animation.

---

## 4. Input hardware

| Item | Finding | Evidence |
|---|---|---|
| Buttons | Two momentary switches to GND, `INPUT_PULLUP`, pressed == LOW (`BTN_ACTIVE_LEVEL 0`); no external resistors | input.cpp:43; config.h:106; README.md:69 |
| Pins (code) | `PIN_BTN_L 10`, `PIN_BTN_R 2` — GPIO2 is a strapping pin; config.h:89 itself forbids buttons on GPIO2/9 | config.h:94-95 |
| Pins (docs) | L = GPIO3, R = GPIO4; "Tu prototipo actual tiene un botón en GPIO2. Hay que moverlo a GPIO3." | README.md:66-67, :75 |
| Sampling | Pure polling of `digitalRead()` from `loop()`; no ISR, no timer; the promised 5 ms cadence (`INPUT_POLL_MS`, input.h:39) is not enforced and the real gap during a frame push is ~24 ms | input.cpp:42; config.h:168 |
| Debounce | 25 ms stability window per button, raw-edge timestamping | input.cpp:221-240; config.h:160 |
| Gestures | `GST_TAP_L/R`, `GST_DBL_L/R` (280 ms window), `GST_HOLD_L` (600 ms, repeats every 220 ms), `GST_HOLD_R` (once), `GST_BOTH` (skew ≤ 80 ms), `GST_LONG_BOTH` (1,500 ms) | nt_types.h:95-106; config.h:160-167 |
| FSM | 5 states IDLE/DOWN/HOLD/CHORD/SWALLOW; swallows the rest of an episode once classified (no phantom TAP before BOTH, no phantom TAP_R after HOLD_L+R) | input.cpp:55-61, :131-216 |
| Tap latency | ~305 ms (25 ms debounce + 280 ms double-tap window before a TAP is emitted) — conflicts with §46 "no visible input lag"; minigames bypass it with `input_raw()` edges | input.cpp:186-189, :283-286; ui.cpp:1722-1728; README.md:463 |
| Boot-held button | tolerated (FSM starts in SWALLOW) but cannot protect against strapping-pin sampling that happens before firmware | input.cpp:307-319 |
| Public API | `input_begin`, `input_poll`, `input_raw`, `input_hold_ms`, `input_flush`; indices `INPUT_BTN_L/R` are not GPIO numbers | input.h:23-64 |
| Static state | 47 B; zero heap, zero float | input.cpp:65-83 |

§7 compliance: exactly two inputs, short/long/simultaneous all present, debounced. Gap: the UI puts "back" on `HOLD_R` (ui.cpp:7, :3389-3395) whereas §7 wants B = back as a plain press; that is a ui.cpp policy, not a recogniser limitation.

---

## 5. Storage

| Item | Finding | Evidence |
|---|---|---|
| Medium | ESP32 NVS via `Preferences`, namespace `"notta"`; no SD card, no LittleFS/SPIFFS use | config.h:472; storage.cpp:19, :60, :357 |
| Partition | `huge_app.csv`: nvs @0x9000 size 0x5000 (20 KB = 5 pages, ~504 usable 32-B entries); otadata 0x2000; app0 0x300000 (3 MB, no OTA slot); spiffs @0x310000 0xE0000 (896 KB, **unused by firmware**); coredump 0x10000 | core `tools/partitions/huge_app.csv` |
| Keys | `"save"` PetSave 128 B; `"cfg"` Config 256 B; `"anc"` 16x12 B = 192 B ancestor ring (no magic/version/CRC); `"egg"` PendingEgg 24 B; `"gl"` GainSave 20 B; `"t"` uint64 last-seen epoch; `"ok"` uint32 canary — ~32 entries used today | config.h:473-479; storage.cpp |
| Integrity | CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) over every blob except `"anc"`; magic + version bytes; every `put*` verified by bytes-written comparison; sticky `STORE_E_*` bit field (all 8 bits used) | storage.cpp:140-156 (`store_crc16`), :112-127 (`store_fail`); storage.h:42-54 |
| Write cadence | `"t"` every 60 s (`SAVE_LASTSEEN_PERIOD_S`), `"save"` every 300 s unforced (`SAVE_FULL_PERIOD_S`) or forced after actions with a 1 s floor (deferred, never dropped); clock jump ≥ 3,600 s forces `"t"` with a 5 s floor | config.h:492-493; storage.h:37-39; storage.cpp:540-606 (`store_save`), :715-778 (`store_touch_lastseen`) |
| Boot classification | `RTC_NOINIT_ATTR RtcKeep` (32 B, budget 64) nonce read before re-arm + `esp_reset_reason()` → `BOOT_FIRST_RUN / POWER_LOSS / CRASH / SOFT_RESET / UNKNOWN`; `ESP_RST_DEEPSLEEP` currently folded into SOFT_RESET | storage.cpp:63 (`RtcKeep`), :181-222 (`classify_boot`; `ESP_RST_DEEPSLEEP` :207), :357-430 (`store_begin`, `esp_reset_reason()` :365), :431-454 (`store_boot_kind`); nt_types.h:231-238, :573-582 |
| Self-test | fresh `esp_random()` canary written and read back at each boot | storage.cpp:330-356 |
| Migration | **None.** Foreign version or bad CRC → `store_load()` returns false indistinguishably from "absent"; `boot_pet()` then creates a new generation-0 pet and `boot_absence()` overwrites the bad blob in the same boot | storage.cpp:492-539 (`store_load`; refusals :508-519); sketch_aug30b.ino:279-283 (`boot_pet`), :304-333 (`boot_absence`), :377, :396 |
| Layout guards | `static_assert(sizeof(PetSave)==128)` + 5 `offsetof` asserts; `sizeof(Config)==256` + 3 offsets; `Genome` 16 B; `GainSave` 20 B; `PendingEgg` 24 B; `AncestorRecord` 12 B | nt_types.h:472-479, :563-567, :330, :502, :525; storage.h:87-88 |
| Core hazard | Arduino `initArduino()` erases the whole NVS partition on `ESP_ERR_NVS_NO_FREE_PAGES` / `NEW_VERSION_FOUND` before `setup()` | core `esp32-hal-misc.c:287-300` |
| Wear | idle ≈ 4,032 entries/day (≈ 6.4 erase cycles per page per day); pathological 1 Hz forced saves ≈ 777k entries/day | storage.cpp:551-554 + arithmetic in storage map |
| Config-overrides-config.h trap | NVS `"cfg"` wins over `config.h` `CFG_*` from the second boot on | README.md:278-284; storage.cpp:826-843 |

---

## 6. Sensors

**There are none beyond the two buttons.** The spec's §4 interface list vs the board:

| Spec §4 interface | Present | Evidence |
|---|---|---|
| Display | yes (SSD1306) | §3 |
| Buttons | yes (2) | §4 |
| Storage | yes (NVS) | §5 |
| Clock | RTC-timer-backed `time()`, uncalibrated until SNTP; no external RTC, no 32 kHz crystal | gametime.cpp:266-278; sdkconfig:1405 |
| WiFi | yes | §7 |
| Bluetooth/BLE | yes | §7 |
| MotionSensor | **no** — no accelerometer/IMU wired, no driver, no I2C address probed for one (only the OLED sweep, render.cpp:326-345) | — |
| Buzzer/Audio | **no** — README.md:71 "No hay zumbador"; `DEATH_BUZZ_MS` (config.h:196) is unused; `CF_MUTE` only toggles a status icon (ui.cpp:879) | — |
| Power | **no** battery sense: `PIN_VBAT_ADC 0 // reserved, not populated in v1` and unused; no sleep code anywhere (`grep esp_sleep|deep_sleep|light_sleep|setCpuFrequency` → nothing) | config.h:97 |

Implications for §25 (growth through carrying) and §52 Phase 6 — the only honest activity signals on this hardware are:

1. **Elapsed time** — `PetSave.age_s` accumulated by the sim in sub-steps, clock-jump proof (nt_types.h:435; sim.cpp:1226-1231), plus the `now - last_seen` delta on boot (sketch_aug30b.ino:305-333).
2. **Interaction count** — actions, minigame plays, button presses per day (the ledger fields `minigames_won`, `snacks_total`, `last_interact_epoch` already exist, nt_types.h:434, :448-451).
3. **Network diversity** — the number of distinct hashed BSSIDs/SSIDs seen by the §5 Wi-Fi scanner (to be written), which is the spec's own proxy for "the device moved".
4. **Peer encounters** — distinct BLE peers seen (ble_social.cpp peer table, :216-311).

Do **not** invent an IMU: the spec explicitly allows "elapsed time" as a sensor (§25) and says "Do not claim precise GPS distance if the hardware cannot measure it". The `hardware/Motion` interface should exist as an abstraction whose only real implementation returns "unsupported" (capability detection per §4), so a future board revision can add one without touching game code. Deep-sleep integration (Phase 6) has a prerequisite in the clock: the uncalibrated estimate uses `esp_timer_get_time()` uptime that restarts on every wake (gametime.cpp:129, :297-298), so time asleep would be invisible until `time()` (RTC-backed) is used as the monotonic source.

---

## 7. Wi-Fi/BLE capabilities

### 7.1 What exists

| Capability | Status | Evidence |
|---|---|---|
| Single-stack invariant | `RadioMode` OFF/WIFI/BLE; `net_request()` tears the other stack down first, `delay(RADIO_SETTLE_MS=250)` per flip | nt_types.h:139-144; net.cpp:420-509, :142-144 |
| Wi-Fi STA | association sub-machine with 15 s timeout, exponential backoff 2 s<<n capped 30 s, link-loss re-association, `WiFi.setSleep(true)` modem sleep, `WiFi.persistent(false)` | net.cpp:291-303, :351-376, :511-593 |
| Wi-Fi AP + captive portal | open softAP `NOTTAMAGOCHI-XXXX` at 192.168.4.1/24, DNSServer, portal→STA retry every 120 s | net.cpp:305-348, :574-586; config.h:500-506 |
| mDNS | `nottamagochi.local` `_http._tcp` (Android unreliable per README.md:367) | net.cpp:232-253 |
| Wi-Fi OFF | `wifi_down()` → `WIFI_MODE_NULL` returns ~50 KB heap | net.cpp:268-283 |
| **Wi-Fi scanning** | **absent**: `grep scanNetworks|esp_wifi_scan|WiFi.scan` → nothing; every Wi-Fi path calls `WiFi.begin()` | net.cpp:291-303 |
| Wi-Fi policy | STA held up whenever `CF_WEB_ENABLED` (default on) or weather/Telegram enabled; retried from OFF every 30 s | sketch_aug30b.ino:182-226, :55; storage.cpp:801-803 |
| BLE | Bluedroid, advert-only (`ADV_TYPE_NONCONN_IND`, no GATT, no scan response), passive scan 100 ms/1000 ms (10 % duty) idle, 180/200 ms fast; adv 1-2 s idle / 100-200 ms fast; TX 0 dBm; RSSI gate -70 dBm | ble_social.cpp:358-374, :407-421, :666-670; config.h:551-556 |
| BLE frame | 22 B manufacturer data: `company_id` 0xFFFF (SIG test id) + `frame_type` + **19 B payload**; 3 frame types BEACON/MATE_OFFER/MATE_ACK; no version, session id, seq or length | ble_social.cpp:62-91; config.h:546-550 |
| BLE RX path | lock-free SPSC ring (8 deep) from the BTC task to `loop()` with `__atomic` acquire/release | ble_social.cpp:121-130, :321-346, :835-843 |
| BLE peer table | 8 peers, RSSI EMA, hit count ≥ 3, freshness 8 s, TTL 60 s | ble_social.cpp:216-311 |
| BLE session cap | 32 `BLEDevice::init/deinit` cycles per boot (claimed ~672 B Bluedroid leak per cycle, unmeasured) | config.h:559; net.cpp:196-204 |
| Bluedroid hazards (verified against core) | `deinit(true)` leaves `initialized==true`; `m_pScan`/`m_bleAdvertising` never freed; `init()` sets `initialized` before `btStart()` → liveness probe via `esp_bt_dev_get_address()` | ble_social.cpp:1-38, :646-679; core `BLEDevice.cpp:313, :317, :622-641` |
| TLS | only consumer is Telegram; 48 KiB max-alloc gate | net.cpp:616-626; telegram.cpp:727-738 |
| Identity | MAC-derived AP SSID / GAP name; no persisted random deviceId; peers keyed by low 3 MAC bytes | net.cpp:150-162; ble_social.cpp:226-235 |

### 7.2 Scan-only feasibility (spec §5, §40, §68 rule 5)

The core provides `WiFiScanClass::scanNetworks(async, show_hidden, passive, max_ms_per_chan=300, channel, ssid, bssid)`, `scanComplete()`, `scanDelete()`, per-result `RSSI()/BSSID()/encryptionType()/channel` (core `WiFi/src/WiFiScan.h:38-59`). `scanNetworks()` forces `WiFi.enableSTA(true)` (`WiFiScan.cpp:75`) without associating, so a scan needs `RADIO_WIFI` residency but never `NPH_STA_CONNECTING`. Implementation shape: add `NPH_SCANNING` to `NetPhase` (net.h:44-52), `net_scan_start()/net_scan_poll()/net_scan_results()`, hash BSSID+SSID on device, `scanDelete()`, then `net_request(RADIO_OFF)`. A passive scan of 13 channels at 300 ms ≈ 4 s of radio-on per encounter roll — compatible with §40.

### 7.3 BLE GATT vs advert-only vs ESP-NOW (spec §41, §15, §59)

| Option | Present in toolchain | Payload | Duplex | Power | Risk |
|---|---|---|---|---|---|
| Legacy advert (current) | yes, proven design | 19 B/frame, one frame in flight, ~1-2 s per hop at idle | half, broadcast | lowest (passive scan 10 %) | cannot carry §15 HELLO..GOODBYE + TEAM_SUBMIT (3 Pebbles); good for **discovery beacon only** |
| BLE 5 extended advertising | compiled (sdkconfig:850), API present (`BLEMultiAdvertising`, `BLEScan::startExtScan`, core `BLEAdvertising.h:88`, `BLEScan.h:78-84`) | up to 255 B/AD | still broadcast | low | unproven on this core/library; receiver must support ext scan |
| BLE GATT connection | `BLEServer/BLEClient` in library | 20-512 B/write | full | low-medium | author rejected for stability (ble_social.cpp:8-25); every BLE entry costs a session against the cap of 32 |
| ESP-NOW | core library `libraries/ESP_NOW` present; `ESP_NOW_MAX_DATA_LEN` = 250 B (`esp_now.h:52-53`) | 250 B/frame | full, unicast, ACKed | higher than BLE scan (Wi-Fi PHY on) | needs `WIFI_STA` mode — the same residency the scanner needs, so **no extra BLE session churn**; both peers must share a channel |

Recommendation grounded in the code: keep the BLE advert beacon for **Discovery** (proven, lowest power), do not carry sessions over 19-byte adverts, and pick the session transport behind the §59 `Transport` abstraction with ESP-NOW as the lowest-new-code candidate (scan already requires `RADIO_WIFI`) and BLE5/GATT as the lower-power but unproven alternatives. Heap arithmetic (net.cpp:5-7, author-measured, not re-measured): boot free 179,836 B − Bluedroid ~70 KB − Wi-Fi ~50 KB ≈ 60 KB with TLS gone — still too thin for dual residency; keep time multiplexing.

---

## 8. Current application flow

### 8.1 `setup()` (sketch_aug30b.ino:340-413)

`Serial.begin(115200)` → `rd_begin()` else `rd_fatal()` (noreturn) → `store_begin()+store_selftest()` → `store_boot_kind()/store_last_seen()` → `genome_set_rng(&esp_random)`, `sim_seed(esp_random())` → `store_load_cfg(g_cfg)` → `gt_begin()` → `store_bind_gain(&gain_source)` → `boot_pet()` (load or new gen-0 egg) → `input_begin()` → `net_begin()` → `wx_bind_config/wx_begin/tg_begin/god_begin` → `ui_bind_config/web_bind_config` → `ui_begin()` → `web_begin(80)` → `apply_config()` → `rd_splash()` → `boot_absence()` (absence = now − last_seen, BootKind-gated; `sim_catch_up_ex`) → boot toasts → Serial banner.

### 8.2 `loop()` (sketch_aug30b.ino:470-545), busy loop, no `delay()`

| Stage | What | Blocking hazard |
|---|---|---|
| 1 input | drain ≤ `NT_GESTURES_PER_LOOP` 4 gestures → `ui_handle()` | — |
| 2 logic | if `ms - g_tick_ms >= 1000`: `logic_tick()`; resync if stall ≥ `NT_TICK_RESYNC_MS` 4000 (never bursts) | — |
| 3 pumps | `ui_service()`, `god_service()` | — |
| 4 render | `rd_set_fps(ui_fps())`; `if rd_begin_frame() { ui_draw(); rd_end_frame(); }` | `sendBuffer` ~24 ms |
| 5 radio | `rd_fx_settle_now()` (kills every flash/shake after one frame), `radio_policy()`, `net_service()`, `gt_sync_start()` if STA up, `web_service()`, `tg_service()`, `wx_poll()`, `ble_scan_service()` | `tg_service` 1-3 s (8 s timeout); `net_request` 250 ms settle; `handleClient` up to 5 s on a stalled client |
| 6 notifications | `web_take_action()` → `ui_note_web_action()`; `web_cfg_dirty()` → `apply_config()` + toast | — |

`logic_tick()` (:421-462): `build_env()` (gt_now, local h:m, clock_valid, weather, god clock freeze) → `sim_set_env()` → `sim_tick(sim_step_seconds())` → `ui_note_events(sim_take_events())` → `store_touch_lastseen()` → `store_save(false)` → SNTP-landing edge: `sim_absence_retrofix()`.

### 8.3 Screens (ui.cpp; `ScreenId` nt_types.h:73-91)

| Id | Screen | Code | Purpose | Notes |
|---|---|---|---|---|
| S0 | HOME | ui.cpp:1137-1297 | pet + status bar (3 modes) + HUD badges | sticky; TAP_L→MENU, HOLD_L→STATUS, LONG_BOTH→SETTINGS, DBL_R→pet |
| S1 | MENU | :1300-1407 | 8-icon ring: FEED, CLEAN, PLAY, MEDICINE, STATUS, LIGHT, SOCIAL, SETTINGS | `MENU_ITEM_COUNT 8` config.h:186 |
| S2 | FEED | :1412-1457 | MEAL / SNACK list | |
| S3 | PLAY | :1415-1475 | REFLEJOS / MEMORIA / SALTO list | cooldown + energy gate |
| S4 | GAME | :1478-1776 | minigame runner (intro 1.6 s / run / result 2.2 s) | raw-edge input; sticky |
| S5 | STATUS_A | :1781-1816 | 7 stat bars | |
| S6 | STATUS_B | :1818-1895 | genome page; god-mode entry hold 5 s | `godmode.cpp:713` hard-codes this screen |
| S7 | LINEAGE | :1897-2004 | ancestor ring browser | death-oriented |
| S8 | SOCIAL | :2006-2230 | BLE peer list + mating; owns `RADIO_BLE` on entry/exit | 20 s auto-return applies (bug for §42) |
| S9 | SETTINGS | :2232-2349 | TELEGRAM, SOUND, WEATHER, WEB, BRIGHT, LIGHT, QR, INFO, RESET | RESET = two confirms → `store_wipe` |
| S10 | CONFIRM | :3017-3052 | modal, cursor defaults to NO | |
| S11 | ALERT | :3054-3090 | 8-deep queue, 1.2 s read guard; first press also **acts** (feeds/cleans) | contradicts documented invariant 7 (ui.cpp:17, re-asserted :3379); `handle_alert()` :3085-3090 calls `alert_act()` :3068 |
| S12 | MEMORIAL | :2443-2690 | 22 s death ceremony, bury, lineage, egg fade | sticky; obsolete |
| S13 | EGG | :2918-2984 | rub-to-hatch (10 alternating taps / 20 s), 600 s mourning lock, hatch ceremony 4,480 ms | sticky |
| S14 | GOD | godmode.cpp | developer console frame | ui only hosts |
| S15 | QR | :2351-2441 | URL/PIN QR, requests `RADIO_WIFI` on entry | |

Global rules as implemented: `HOLD_R` = back except S4/S12 (ui.cpp:3389-3394); `LONG_BOTH` = home except S12 (:3383-3387); auto-return after 20 s except HOME/GAME/MEMORIAL/EGG/GOD (:691-694, :3493-3497). Three transitions bypass the `ui_goto()` funnel by assigning `s_screen` directly: death (ui.cpp:2460), hatch (:2729), god entry (:3463).

---

## 9. Existing Pebble implementation

### 9.1 The pet model (Nottamagochi)

`PetSave` (nt_types.h:420-479), 128 B, only `sim.cpp` may mutate it (sim.h:3):

| Offset | Field | Meaning |
|---|---|---|
| 0-3 | `magic 0x544E`, `version 1`, `stage` | `Stage` EGG/BABY/CHILD/TEEN/ADULT/SENIOR/DEAD (nt_types.h:37-46) |
| 4-31 | `int32 stat[7]` | milli-points 0..100000: HUNGER (satiety), HAPPINESS, ENERGY, HYGIENE, HEALTH, BOND, DISCIPLINE (`StatId` nt_types.h:190-199) |
| 32-55 | epochs `birth/last_seen/death/egg/last_interact`, `age_s` | |
| 56-71 | `Genome` 16 B | see below |
| 72-85 | `int16 stat_rem[7]` | exact remainders (|r| < 3600) |
| 86-111 | `cq` (care quality 0..1000), `weight_dg`, `dmg_acc[5]`, `care_miss`, `sick_episodes`, `minigames_won`, `overfeed`, `snacks_total`, `wish_left_s` | ledgers |
| 112-113 | `flags` (16 `PF_*` bits: SICK, ASLEEP, LIGHT_ON, DEAD, BURIED, SCAR, COLD_EGG, INBRED, GOD_TAINTED, HYBRID_ELIG, ABS_UNKNOWN, …) | nt_types.h:396-412 |
| 114-123 | `adult_form`, `minor_form`, `poop_count`, `guilt_level`, `absence_tier`, `death_cause`, `unjust_scolds`, `happiness_avg`, `wish_id`, `events_done` | |
| 124-125 | `reserved[2]` | the only free bytes |
| 126-127 | `crc16` over 126 B | |

`Genome` (nt_types.h:309-330), 16 B packed, CRC over 14 B, `GENOME_PROTO_VER 1`: `lineage_id` u32, `g0` morphology (species 4 b, pattern 4 b, palette 3 b, body_size 3 b, ear_horn 2 b), `g1` physiology (appetite/metabolism/sociability/temperament 4 b each), `g2` (hardiness 4 b, luck 3 b, mutations 4 b, sex, rare, tainted, 2 reserved), `generation`, `parent_tag`. `SPECIES_COUNT 16` (12 real + 4 hybrid-only), only 8 baby bodies drawn (`species & 7`, sprites.h:1331). Genes map to per-mille care multipliers (genome.cpp:322-375), not battle stats.

Evolution today: age-gated stage ladder (`AGE_CHILD_S 13500`, `AGE_TEEN_S 72000`, `AGE_ADULT_S 172800`, `AGE_SENIOR_S 604800`, config.h:379-382; `stage_step` sim.cpp:1119-1139) with 6 adult forms picked by a care-usage branch score (`pick_adult_form` sim.cpp:551-603; `AdultForm` nt_types.h:49-58) — the visual outcome is care-driven, not species-driven.

Death: `do_death()` sim.cpp:700-722; causes hunger/filth/illness/sadness/old age/neglect/accident (`DeathCause` nt_types.h:60-70); health drains at 3 pt/h when satiety is 0 (config.h:251); README.md:681 documents death after ~19 h untouched; neglect death after 41.7 d offline (sim.cpp:1983-1985). Absence punishment ladder CORTA/LARGA/ABANDONO/GRAVE with sulk refusal and a permanent `PF_SCAR` (sim.cpp:1843-1897; config.h:427-455). Tuning is the opposite of §27 "fun if ignored for several hours": a baby starves in 3.8 h (RATE_HUNGER_MPH −12000 × STAGE_MULT_BABY 2200).

Simulation engine facts that matter for reuse: pure integer, no Arduino includes (sim.h:13-17, host-compiles clean), 60 s sub-step grid (sim.cpp:24), int64 `rate_chain()/accum()` with exact remainders (sim.cpp:167-197), offline catch-up in 1,800 s chunks up to 2,000 steps (sim.cpp:1899-1982), `SimEnv` push model (sim.h:33-48), event bitmask drain `sim_take_events()` (sim.h:58-78), hourly anti-farm gain caps with persisted snapshot (sim.cpp:259-409), god hooks (sim.cpp:2060-2149). **Single instance**: one `g_pet` pointer + 48 file-scope accumulators (sim.cpp:89-149).

### 9.2 Against spec §10 (species/instance split)

| §10 field | Present today | Where | Verdict |
|---|---|---|---|
| `id` | no (lineage_id is a dynasty id) | nt_types.h:320 | missing |
| `speciesId` | 4-bit gene, 16 values | nt_types.h:333-334 | inadequate for 60 species |
| `nickname` | per-device `Config.pet_name[13]`; deterministic name from `ui_name_for()` (144 names) | nt_types.h:550; ui.cpp:325-331 | adapt |
| `level`, `xp` | no | — | missing |
| `type` | no (2 reserved g2 bits could hold 3 values) | nt_types.h:365-366 | missing |
| `hp/maxHp/attack/defense/speed` | no | — | missing |
| `hunger`, `happiness`, `cleanliness` | `stat[HUNGER/HAPPINESS/HYGIENE]` milli-points | nt_types.h:426 | reuse (retune) |
| `status` | `flags` PF_SICK/PF_ASLEEP… | nt_types.h:396-412 | adapt (§55 corruption) |
| `moves[4]` | no | — | missing |
| `evolutionState` | `stage` + `adult_form/minor_form` | nt_types.h:424, :456-457 | adapt |
| lineage metadata | `Genome.lineage_id/generation/parent_tag`; `AncestorRecord` ring | nt_types.h:320-326, :507-526 | reuse |
| `creationSeed` | no (petfx seeds from `lineage_id^g0^g1^g2^generation`, petfx.cpp:493-499) | — | adapt |
| `customSpriteData` | no; all sprites are flash `constexpr` | sprites.h:9-10 | missing |
| `traits` | temperament class (4), rare, luck genes; usage counters `overfeed/snacks_total/minigames_won/unjust_scolds` | nt_types.h:380-387, :448-462 | adapt (§56) |
| `lifetimeStats` | partial ledger | nt_types.h:446-451 | adapt |
| `lastUpdated` | `last_seen_epoch` | nt_types.h:431 | reuse |
| `SpeciesDefinition` table | **none** — species is a gene value with a Spanish name string (strings_es.h:600-608) and a sprite set chosen by `sprite_set_id()` (sprites.h:1298-1337) | — | missing |
| Box (§9) | none — single pet, non-re-entrant sim | sim.cpp:89-149 | missing |

---

## 10. Existing games

### 10.1 On-device (ui.cpp:1478-1776, screen S4)

| Game | Code | Mechanic | Duration | Spec §29 counterpart |
|---|---|---|---|---|
| REFLEJOS | ui.cpp:1514-1566 | 5 rounds; a side lights up after 700-2,199 ms, press the matching button within 900 ms; 200 pts/round | ~8-15 s | close to **Ping / Signal Lock** (reaction) |
| MEMORIA | :1568-1626 | 6 levels, sequence of 3..8 flashes at 380+170 ms, replay with L/R | 18 s playback + input (exceeds 5-15 s target) | exactly the **Sequence** candidate |
| SALTO | :1628-1690 | one-button runner, 12 obstacles at 120 px/s, jump 700 ms/18 px | ~15-20 s | no counterpart |

Framework facts: `GameState` (ui.cpp:227-247), phases intro 1,600 ms / run / result 2,200 ms, fixed 25 ms step (:1731-1739), input via raw debounced edges `input_raw()` (:1722-1728) because a TAP is only classified after 280 ms, gestures ignored except `HOLD_R` quit (:1772-1776), score → per-mille → `sim_apply_play_result()` (:1501-1512; sim.cpp:1816-1838), shared 120 s cooldown `MG_COOLDOWN_S`, energy gate 12 %. RNG = `genome_rand()` (ui.cpp:1515, :1517, :1573, :1630) — shared with breeding, a §30 violation. No `Minigame` interface, no manager, no sequence play (always returns to the PLAY list, :1716).

### 10.2 Browser (index_html.h blob + webui.cpp)

| Game | Duration / max score | Server side | Notes |
|---|---|---|---|
| ZAMPADA (MG1) | 35 s / 200 | `h_game_start` webui.cpp:639-685 issues an 8-hex token; `h_game_submit` :687-779 validates token/elapsed ≥ 80 %/score cap, then `sim_apply_minigame()` (sim.cpp:1754-1814) | canvas touch game in the phone page |
| BURBUJAS (MG2) | 30 s / 120 | same | |
| NANA (MG3) | 40 s / 160 | same | |

Constants `MG1..3_*` config.h:590-614; rewards fixed-point `(score*NUM)>>8` capped; anti-replay single-slot token with 20 s grace. Obsolete per §1.5 ("browser games obsolete unless reused as infrastructure"); the server-decides-the-value pattern is the only piece worth keeping.

---

## 11. Current persistence

| Blob | Key | Size | Magic/Version | CRC span | Writer cadence | Reader behaviour on failure |
|---|---|---|---|---|---|---|
| `PetSave` | `save` | 128 B | 0x544E / 1 (nt_types.h:393-394) | 126 B | 300 s unforced; forced after actions (ui.cpp:569, :1507, :2512, :3009; webui.cpp:632, :743; godmode.cpp:257) | false → new gen-0 pet silently (ino:279-283) |
| `Config` | `cfg` | 256 B | 0x4643 / 1 (nt_types.h:534-535) | 254 B | on every settings change; sealed in caller's struct (`store_save_cfg` non-const) | compiled defaults (`store_cfg_defaults`), silently |
| ancestor ring | `anc` | 192 B (16 × 12 B) | **none** | **none** | on death | length == 192 and `AR_SLOT_USED` bit only |
| `PendingEgg` | `egg` | 24 B | 0x4745 / 1 | genome CRC only | on mating | ignored |
| `GainSave` | `gl` | 20 B | 0x474C / 1 (config.h:489-490) | 18 B | rides every `save` write with a wear filter | seed nothing |
| last seen | `t` | 8 B u64 | — | — | 60 s, or immediately on ≥ 3,600 s jump | reconciled as max(save, t, RTC) |
| canary | `ok` | 4 B | — | — | each boot | `STORE_E_CANARY` |
| `RtcKeep` | RTC_NOINIT | 32 B | nonce 0xB1C0FEED | — | every tick | crash-vs-power-loss discriminator |

Gaps against §31/§48/§60: no migration (foreign version refused, storage.cpp:511-519, :830-831); no previous-valid checkpoint (single key per blob; the Arduino core can format NVS wholesale before `setup()`); multi-key transactions are not atomic (save → gl → t are three `nvs_commit`s, storage.cpp:557-582; death writes save then `anc`, ui.cpp:2510-2519; wipe → cfg → save, ui.cpp:3001-3007); `store_error()` bit field exhausted (storage.h:42-54); no unified `SAVE_SCHEMA_VERSION/CONTENT_VERSION/PROTOCOL_VERSION`; nothing persisted for Box, inventory, cooldowns (RAM-only and reseeded as "just spent" on boot, sim.cpp:1355-1372), creator PIN (rolled per boot, webui.cpp:1013), deviceId, or `timeCalibrationState` (§26). Wi-Fi password is stored plaintext in `cfg` (nt_types.h:549).

---

## 12. Existing browser/web functionality

| Item | Finding | Evidence |
|---|---|---|
| Server | esp32 core 3.1.1 `WebServer` on port 80, single static instance, routes registered once, `enableDelay(false)`; binds only in `NPH_STA_UP` or `NPH_AP_PORTAL` (else lwIP assert) | webui.cpp:71, :1041-1098 |
| Routes | `GET /`, `GET /api/state`, `GET /api/sprites?id=N`, `POST /api/action?do=…&k=`, `POST /api/game/start`, `POST /api/game`, `GET/POST /api/cfg`, `onNotFound` → captive 302 | webui.cpp:1056-1064, :977-1005 |
| Auth | 4-digit PIN `esp_random()%10000` per boot, passed as query arg `k=`; `pin_ok()` → 403 `{"err":"pin"}`; 64-bit saturating `arg_u32` parse | webui.cpp:1011-1020, :352-364, :173-184 |
| Rate limit | token bucket 10 tokens, +4/s, read 1 / mutate 2, bare 429 when dry | webui.cpp:270-285 |
| Page | `index_html.h` 47,181 B PROGMEM raw string served with 4-arg `send_P` and `Cache-Control: no-cache`; minified Spanish dashboard: live pet canvas decoded from `/api/sprites` XBM, 6 action buttons, weather strip, 3 canvas minigames, PIN modal (auto-consumes `?k=` from the QR URL into sessionStorage) | index_html.h:26-78; webui.cpp:573-581 |
| Settings endpoint | `/api/cfg` edits ssid/pass/name/tz/lat/lon/tg/mute/br/sb — **no client**: the blob contains 0 occurrences of `/api/cfg`, `ssid`, `tz` (README.md:262, :387, :643 describe a settings UI that does not exist) | webui.cpp:821-968 |
| Sprite mirror | `/api/sprites` streams `{'S',1,frames,w,h,SPRITE_REV}` + raw XBM rows, immutable ETag `"id-rev"` | webui.cpp:792-819 |
| QR | `qr.cpp` v1-v4 ECC-L byte mode, constexpr GF(256) tables, 8-mask penalty; S15 shows `http://<ip>/?k=PIN` (≤ 30 B → v2, 2 px/module in a 62 px box) or `WIFI:S:<ssid>;;` in AP mode; v3+ degrades to 1 px/module | qr.cpp:470-567; ui.cpp:2357-2441; net.cpp:653-671 |
| Lifecycle | bound whenever the radio is up and `CF_WEB_ENABLED`; `web_client_seen_ms()` exists but no inactivity timeout, no auto Wi-Fi off | webui.cpp:1102-1130, :1022 |
| Body limits | none — `WebServer` mallocs `Content-Length` bytes before any handler (core `Parsing.cpp:44-75`, :208) | — |
| Cost | `FEATURE_WEB=0` saves 95,454 B flash and 1,656 B static RAM (2,105,548/72,748 → 2,010,094/71,092) | measured |

Against spec §38 endpoints: present 2/7 (`GET /`, `GET /api/state` — wrong content), missing `/api/schema`, `/api/validate`, `/api/pebble`, `/api/time`, `/api/ping`; extra 6 + captive catch-all. Against §34/§39: PIN embedded in the QR bypasses the PIN step; Wi-Fi is not confined to the connection screen; no inactivity shutdown; no on-device body validation (§35). Page source (`scratchpad/index_dev.html` + `build.js`, index_html.h:2) is not in the repo.

---

## 13. Dependencies

### 13.1 External

| Library / component | Version | Used by | Notes |
|---|---|---|---|
| esp32 Arduino core | 3.1.1 (IDF 5.3) | all | `Preferences`, `WiFi`, `WiFiClientSecure`, `HTTPClient`, `WebServer`, `ESPmDNS`, `DNSServer`, `BLE` (Bluedroid wrapper), `Wire` |
| U8g2 | 2.35.30 | render.cpp, petfx/actfx/ui/godmode via `rd_u8g2()`, weather.cpp, qr.cpp (renderer half) | full-buffer HW I2C class |
| ESP-IDF pieces | via core | `esp_random.h` (.ino, storage, webui, ble_social), `esp_system.h` reset reason (storage), `esp_attr.h` RTC_NOINIT (storage), `esp_timer.h` (gametime), `esp_mac.h` (net), `esp_bt_device.h` (ble_social) | |
| Cloud endpoints | — | weather.cpp (api.open-meteo.com:80, ip-api.com:80), telegram.cpp (api.telegram.org:443) | config.h:516-529; obsolete |
| NTP servers | — | gametime.cpp:244 | config.h:465-467; obsolete under phone-calibrated time |

### 13.2 Internal include graph (`.cpp` → project headers it includes; `config.h`/`nt_types.h` reach everything transitively)

| Module | Includes | Direction check |
|---|---|---|
| `sketch_aug30b.ino` | config, nt_types, strings_es, genome, sim, storage, gametime, input, render, net, weather, telegram, ble_social, ui, webui, godmode | entry (allowed to see all) |
| `sim.cpp` | sim, genome, strings_es | clean domain (no hardware) |
| `genome.cpp` | genome | pure |
| `storage.cpp` | storage | leaf; sim joined via `StoreGainFn` thunk (.ino:239-252) |
| `gametime.cpp` | gametime, config, nt_types, storage | reads `store_last_seen/store_load_cfg` |
| `input.cpp` | input | leaf |
| `render.cpp` | render | leaf (U8G2 owner) |
| `petfx.cpp` | petfx, render, sprites, genome, **sim** | **inversion**: renderer reads `sim_mood_score/sim_stat_pct/sim_sulk_left_s` (petfx.cpp:580, :764-770, :894) |
| `actfx.cpp` | actfx, petfx, render, sprites | reads `PetSave` fields directly (`actfx_begin(…, const PetSave& before)` actfx.cpp:588; :610, :636-638) |
| `ui.cpp` | actfx, ble_social, gametime, genome, godmode, input, net, petfx, qr, render, sim, sprites, storage, strings_es, telegram, ui, weather, **webui** | **inversion**: panel UI depends on `web_pose_of/web_mood_index` (ui.cpp:348, :920) and on godmode |
| `net.cpp` | net, strings_es | radio owner; WiFi/BLE headers hidden behind `FEATURE_*` |
| `ble_social.cpp` | ble_social, strings_es | deliberately does not link genome.cpp (own CRC copy :171-183) |
| `webui.cpp` | webui, index_html, config, nt_types, genome, net, render, sim, sprites, storage, telegram, weather | only TU with `WebServer.h` |
| `qr.cpp` | qr | pure encoder; U8G2 only under `#ifdef ARDUINO` |
| `weather.cpp` | weather, gametime, storage | obsolete |
| `telegram.cpp` | telegram, gametime, net, strings_es | obsolete; weak `nt_pet_view/nt_cfg_view` overridden by .ino:91-99 |
| `godmode.cpp` | godmode, ble_social, gametime, genome, input, net, render, sim, storage, strings_es, telegram, weather | application-layer client of every system |

Godmode-only seams (zero other callers): `wx_force`, `gt_skew_add`, `ble_debug_inject_*`, `store_rtc_mark_god/store_rtc_god_tainted`, `sim_set_time_scale`, all `sim_god_*`, `genome_from_hex32`. Dead exports: `sim_catch_up`, `sim_time_scale`, `sim_god_set_cq`, `genome_seed`, `genome_mate_success_permille`, `gene_luck_permille`, `gene_tantrum_permille`, `gene_mate_success_permille`, `store_rtc_service`, `rd_textf`, `rd_breathe`, `rd_display_ok`, `rd_frame_time_us`, `rd_set_contrast`, `web_pin_regenerate`, `web_game_*`, `web_client_*`, `web_requests_*`, `wx_draw_overlay`, `tg_url_encode`, `sprite_lookup/ghost/tomb/frame_bytes/set_bytes`, `sprite_wx_particle`.

### 13.3 Feature switches (config.h:68-72) and their consumers

| Flag | Consumers | Measured cost when ON |
|---|---|---|
| `FEATURE_WEATHER` | net.cpp:27, storage.cpp:795-797, .ino:52/:190, weather.cpp:620 (does **not** compile the module out) | with TELEGRAM: 123,834 B flash / 3,208 B RAM |
| `FEATURE_TELEGRAM` | net.cpp:27, storage.cpp:812-816, telegram.cpp:393 (real `#else` stub), .ino:52/:194 | (see above) |
| `FEATURE_BLE` | ble_social.cpp:42/:943, net.cpp:37/:168/:192/:220, ui.cpp:2018-2214, storage.cpp | 721,632 B flash / 23,688 B RAM |
| `FEATURE_WEB` | webui.cpp:65, net.cpp:27-29, storage.cpp:801-803, ui.cpp:3110 | 95,454 B / 1,656 B |
| `GOD_MODE_ENABLED` | godmode.cpp:16 (stubs :1476-1494) | 15,820 B / 176 B |

---

## 14. Build commands

Prerequisites (versions the code is written against, README.md:104):

```sh
arduino-cli core update-index
arduino-cli core install esp32:esp32@3.1.1
arduino-cli lib install "U8g2@2.35.30"
```

Compile (verified today, 0 project warnings):

```sh
arduino-cli compile \
  --fqbn esp32:esp32:esp32c3:PartitionScheme=huge_app,CDCOnBoot=cdc \
  --warnings all \
  /home/user/Pebblebol/sketch_aug30b
```

Upload / monitor (not verified — no hardware has ever been attached; port name is typical for USB-CDC on Linux):

```sh
arduino-cli upload  --fqbn esp32:esp32:esp32c3:PartitionScheme=huge_app,CDCOnBoot=cdc -p /dev/ttyACM0 /home/user/Pebblebol/sketch_aug30b
arduino-cli monitor -p /dev/ttyACM0 -c baudrate=115200
```

Notes:
- `PartitionScheme=huge_app` is mandatory: the default 4 MB-with-spiffs scheme gives the app 1,310,720 B and the image is 2.1 MB (README.md:107-126). `huge_app` = 3 MB app, no OTA, 896 KB unused spiffs (`huge_app.csv`).
- `CDCOnBoot=cdc` is mandatory or the serial port disappears when the sketch starts (README.md:138-155).
- README.md:132 uses FQBN `esp32:esp32:nologo_esp32c3_super_mini:PartitionScheme=huge_app` (CDC on by default there); the two board definitions produce slightly different sizes (README.md:109).
- The sketch folder name must equal the `.ino` basename; `index_html.h` must be included from exactly one TU (sketch_aug30b.ino:46-47).
- `GATE_FLASH_MAX 2400000` / `GATE_GLOBALS_MAX 90000` (config.h:642-643) are referenced nowhere; nothing enforces them.

Measured sizes (arduino-cli "Sketch uses / Global variables use" lines):

| Build variant | Flash (B) | % of 3,145,728 | Static RAM (B) | % of 327,680 | Project warnings |
|---|---|---|---|---|---|
| Baseline (all `FEATURE_*`=1, GOD=1) | 2,105,548 | 66 | 72,748 | 22 | 0 |
| `GOD_MODE_ENABLED=0` | 2,089,728 | 66 | 72,572 | 22 | 0 |
| `FEATURE_WEB=0` | 2,010,094 | 63 | 71,092 | 21 | — |
| `FEATURE_WEATHER=0` + `FEATURE_TELEGRAM=0` | 1,981,714 | 62 | 69,540 | 21 | 9 (unused statics telegram.cpp:97-358, outside its `#if`) |
| WEB+WEATHER+TELEGRAM=0 (BLE on) | 1,845,368 | 58 | 65,876 | 20 | — |
| `FEATURE_BLE=0` | 1,383,916 | 43 | 49,060 | 14 | — |
| All `FEATURE_*`=0, GOD=0 | 1,106,556 | 35 | 41,996 | 12 | 12 (telegram.cpp ×9, ui.cpp ×3) |

Each variant is the same compile command with the named `FEATURE_*`/`GOD_MODE_ENABLED` macro set to 0 in config.h:68-72; the figures are the arduino-cli "Sketch uses"/"Global variables use" lines and every delta quoted in §13.3/§19.1 is the plain subtraction from the baseline row (e.g. BLE: 2,105,548 − 1,383,916 = 721,632 B; 72,748 − 49,060 = 23,688 B).

Docs carry three stale size figures (README.md:136 2,066,680/70,788; ESTUDIO_VISUAL.md:66 2,084,592/71,708; :455 2,098,076/72,564).

---

## 15. Test commands

**None exist.** There is no `tests/` directory, no CI, no host Makefile/CMake, and `git log` shows a single commit. Every verification claim in the docs (CHANGELOG.md:193, README.md:801, ESTUDIO_VISUAL.md:513) refers to host harnesses that were never committed:

| Referenced artefact | Cited at | Present in repo |
|---|---|---|
| `scratchpad/test_genome.cpp` (MSVC unit tests) | genome.h:9 | no |
| `scratchpad/test_qr.cpp` + `QR_SPEC.md` | qr.cpp:7 | no |
| `scratchpad/petfx/mkharness.py` (PIXEL CORE slicer), `eyes.py` | petfx.cpp:8-11 | no |
| `t_adv.cpp [3]` (gain-ledger exploit test) | storage.cpp:686 | no |
| `scratchpad/index_dev.html` + `build.js` (page generator) | index_html.h:2 | no |
| `scratchpad/sprite_src.py` (ASCII art → XBM) | sprites.h:3-4 | no |
| host hooks `nt_input_test_millis/level`, `gt_host_millis32`, `GT_HOST_NEVER_VALID` | input.cpp:45-46; gametime.cpp:28, :257-263 | declared, no provider |

Host-portable today (no Arduino includes, or fenced behind `#if defined(ARDUINO)`): `sim.cpp`, `genome.cpp`, `nt_types.h`, `config.h`, `qr.cpp` (encoder), `gametime.cpp`, `input.cpp`, the PIXEL CORE of `petfx.cpp` (:64-289), `store_crc16()` (pure but inside an Arduino TU). `sim.cpp` compiles with `g++ -std=c++17 -Wall -Wextra` and 0 warnings.

Proposed shape (to be detailed in `PEBBLEBOL_IMPLEMENTATION_PLAN.md`, not yet written): `tests/` with a plain `Makefile`/CMake host build (`-std=c++17 -Wall -Wextra -Werror`), a tiny assertion runner (no framework dependency), one binary per domain area (care/cooldowns/catch-up, genome breed/CRC/validate, save CRC/version/migration/corruption, input waveforms through the shim, QR vectors, later battle/capture/protocol codec), all seeded through `sim_seed()`/`genome_seed()` for determinism (§30, §50, §68 rule 10). Invoked as `make -C tests && ./tests/run_all` and wired into the build script next to `arduino-cli compile`.

---

## 16. Reusable components

Effort: S = hours, M = 1-3 days, L = > 3 days (relative, single engineer).

| Component | Location | Spec § | Decision | Effort | Notes |
|---|---|---|---|---|---|
| setup/loop pipeline, 1 Hz tick scheduler, gesture drain | sketch_aug30b.ino:340-413, :470-545, :484-490 | §5 App/GameLoop, §26 | reuse | S | move out of `.ino` (removes ctags trap) |
| `SimEnv` push model + event bitmask drain | sim.h:33-78; sim.cpp:477-500, :1313 | §5, §68 r8 | reuse | S | keep boundary, drop `wx_*` fields |
| Integer milli-point integrator with remainders, 60 s sub-steps | sim.cpp:167-197, :24, :1278-1292 | §11, §27 | reuse | S | no-float, exact at any dt |
| Offline catch-up skeleton (1,800 s chunks, sanity guards) | sim.cpp:1899-1982; config.h:416-426 | §26, §27 | adapt | M | delete death/tier tail (:1983-2011); extend to Box recovery |
| Sleep machine, health regen rule, poop cadence | sim.cpp:741-768, :920-935, :949-978 | §27 | adapt | S | regen rule = Box passive recovery template |
| Anti-farm hourly gain caps + persisted `GainSave` | sim.cpp:259-409; storage.cpp:607-714 | §57 | adapt | S | keep caps, make refusals friendly |
| Stage-up event + ceremony hook | sim.cpp:1119-1139; ui.cpp:2692-2917 | §18 | adapt | M | level-gated data-driven `EvolutionRule` |
| Genome CRC/seal/validate, hex32 serialisation, RNG service | genome.cpp:28-45 (RNG service), :236-266 (CRC/seal/validate), :632-676 (hex32) | §17, §30, §31 | reuse | S | promote to the single seeded RNG |
| `genome_breed` controlled inheritance, `HYBRID_TABLE` | genome.cpp:420-511, :199-239 | §17 | reuse/adapt | M | add `compatibilityGroup`; wire `out_flags` |
| NVS store: begin/load/save/touch/wipe, CRC, boot-kind, RTC nonce | storage.cpp:140-156 (CRC), :181-222 + :357-454 (boot kind, RTC nonce), :492-606 (load/save), :715-778 (touch), :949-993 (wipe) | §5, §26, §31, §48 | adapt | M | tri-state load, checkpoint slot, migration table |
| `static_assert` wire-format discipline | nt_types.h:472-479, :563-567; storage.cpp:30-56 | §31, §60 | reuse | S | apply to every new struct |
| Clock HAL (`gt_now/gt_is_valid/gt_local_tm/gt_format_elapsed/gt_skew_add`) | gametime.cpp | §26, §66 | adapt | M | add `gt_set_epoch()` (phone/user), `timeCalibrationState`, drop SNTP |
| Two-button debounce + gesture FSM | input.cpp | §7, §65 | adapt | S | drop double-tap (−280 ms latency), add `input_pressed_edge()` |
| U8G2 owner, I2C probe, frame scheduler | render.cpp:22, :312-394 (probe + `rd_begin`), :445-547 (frame scheduler) | §4 Display, §46 | reuse | S | |
| Text/dither/bar/invert/affordance primitives | render.cpp:554-910 | §8, §63 | reuse | S | |
| Panel FX (flash/shake/contrast ramp) | render.cpp:911-995 | §14, §18 | reuse | S | remove `rd_fx_settle_now()` from loop once TLS is gone |
| XBM atlas format, `SpriteRef/SpriteSet`, size guards | sprites.h:1-38, :1230-1257 | §19, §37 | reuse | S | replace `sprite_set_id` with species table |
| petfx PIXEL CORE (mirror, ink scan, lids), hash PRNG, cache, stage contract | petfx.cpp:64-289 (PIXEL CORE), :293-318 (hash PRNG), :601-641 (cache); petfx.h:42-89 | §5 PetRenderer, §63 | adapt | M | feed a view struct instead of `sim.h`/`genome.h` |
| actfx pixel writer, easing, prop placement | actfx.cpp:234-353, :368-519 | §27, §14 | adapt | M | re-key films to new care actions |
| UI navigation core, modal/alert/toast layers, list/ring widgets | ui.cpp:585-650, :504-542, :711-856, :1300-1366 | §6, §8, §62 | adapt | L | split into Screen table |
| Minigame skeleton + MEMORIA/REFLEJOS | ui.cpp:1478-1776 | §28, §29 | adapt | M | `Minigame` interface + manager |
| Hatch ceremony (commit-then-show) | ui.cpp:2692-2917 | §18 | adapt | S | becomes EVOLUTION transient |
| Radio state machine `net_request/net_service`, `wifi_down`, AP portal, `net_url` | net.cpp:420-593, :268-283, :305-348, :653-671 | §38-§40 | adapt | M | add `NPH_SCANNING`, invert policy |
| BLE advert/scan plumbing, SPSC ring, peer table, hazard notes | ble_social.cpp:121-130, :216-311, :358-421, :646-703 | §41, §59 | adapt | M | new beacon payload, transport interface |
| `genome_wire_ok` hostile-input validator pattern | ble_social.cpp:186-193 | §58, §68 r11-12 | reuse | S | |
| WebServer skeleton: PIN gate, rate limiter, `send_P`, phase gate, stop | webui.cpp:270-364, :573-581, :1035-1138 | §34, §38 | adapt | M | new routes, body cap, inactivity timeout |
| QR encoder + renderer, S15 layout | qr.cpp; ui.cpp:2357-2441 | §39 | reuse | S | drop `?k=PIN` from payload |
| God-mode entry/marker/taint/compile-out, soak CSV, synthetic-peer pattern | godmode.cpp:16, :538-597, :639-671, :711-730, :1099-1118, :376-501 | §49, §66 | adapt | M | re-content commands |
| Deterministic name generator | ui.cpp:325-331; strings_es.h:449-455 | §54 | adapt | S | move to domain |
| `strings_es.h` StrId/ES[]/S_* mechanism | strings_es.h:32-1064 | §54, §63 | reuse | S | re-terminology |

---

## 17. Obsolete components

| Component | Removal surgery (files:lines) | Consequences / coupled edits |
|---|---|---|
| **Weather** (Open-Meteo + ip-api + overlay) | delete `weather.cpp/.h`; .ino:40, :52, :163-172, :190-192, :382-383, :522; ui.cpp:47, :141, :1219-1230, :1249-1268, :1283, :2238-2263 (SET_WEATHER), :2326, :3182, :3360; godmode.cpp:31, :71, :554, :596-598, :802, :831, :937-941, :1134-1138, :1194-1198, :1408-1411, :1464; webui.cpp:60, :154-159, :448-458, :490, :511, :831-849, :868-880, :917-932; sim.h:39-45 `SimEnv.wx_*`; sim.cpp:480-486, :493-495, :807-819, :823/:828/:838 array entries, :900-902, :997, :1308, :1521 (`WISH_SUN`); config.h:48-53, :68, :257, :259, :515-526, :650; nt_types.h:119-136 `WeatherGroup`, :599-612 `WeatherState`, :537 `CF_WX_ENABLED`, :540 `CF_GEO_AUTO`, :276 `WISH_SUN`; strings_es.h:126-140/:615-629, :328, :370, :394, :433, :475; sprites.h:119-134, :1108-1144, :1206-1218, :1247-1257, :1415-1419 (all already zero-caller) | `SimEnv` static_assert `sizeof==24` (sim.h:48) → keep 24 with padding or update; `WISH_SUN` must go or 1 in 5 daily wishes is unwinnable; `Config.lat/lon` removal → see Config row; six `ICO_SUN..ICO_STORM` icons tied to `spr_icon12` static_assert (sprites.h:1002); `FEATURE_WEATHER=0` does **not** compile the module out (weather.cpp:620 only) |
| **Telegram** (TLS guilt channel) | delete `telegram.cpp/.h`; .ino:41, :52, :84-99 (seam, after godmode fix), :113-117, :194-195, :384, :504-512, :521; ui.cpp:48, :180, :2085, :2238-2261 (SET_TELEGRAM), :2321-2324, :2453, :2542, :3156, :3228, :3266, :3272, :3359; godmode.cpp:32, :72, :170-171, :195-196, :662-666, :688-689, :742-746, :803, :832, :943-953, :1207-1217, :1275-1297, :1465; godmode.h:62-66; webui.cpp:62, :932; net.cpp:27, :616-626 (`net_heap_ok_for_tls`), net.h:119, :158; config.h:39-46, :69, :509-512, :528-543, :647-648; nt_types.h:240-267 (`TgMode/MsgId/Priority`), :551-552, :556; strings_es.h:71, :327, :371, :393, :397-404/:885-927, :431; storage.cpp:788-792, :811-816, :864-868 | `godmode.cpp:662` uses `nt_cfg_view()` — fix before deleting `telegram.h`; god menu is positional (`GD_MENU_STR` :122-128, switch :795-811, `GOD_CMD_COUNT` config.h:663 → 10, static_assert godmode.h:200); removes the only TLS user → `rd_fx_settle_now()` in .ino:513 can go so flash/shake regain duration |
| **Config field removals** (`tg_token[48]@119`, `tg_chat[17]@167`, `lat[12]@224`, `lon[12]@236`, `tg_mode@248`) | nt_types.h:551-556 | **Do not shift the layout**: `static_assert(offsetof(Config,tz)==184)` and `offsetof(crc16)==254` (nt_types.h:565-566) fail if bytes are removed, and `store_load_cfg` requires `n == sizeof(Config) && version == NT_CFG_VERSION` (storage.cpp:826-843, check :830-831) or silently reverts to defaults (units lose Wi-Fi creds, name, TZ, brightness). Two legal paths: (a) replace the fields in place with `uint8_t reserved_a[65]` (@119) and `reserved_b[25]` (@224), keep 256 B/CRC 254, delete `CF_WX_ENABLED/CF_GEO_AUTO` bit definitions; or (b) define `Config v2` with `deviceId`, `deviceName`, `timeCalibrationState`, creator PIN state, bump `NT_CFG_VERSION` to 2 and add a v1→v2 migration in `store_load_cfg` — the spec (§31, §43, §60) needs (b) anyway |
| **Always-on Wi-Fi policy** | .ino:182-226 (`wifi_wanted/radio_policy`), :55 `NT_WIFI_RETRY_MS`; net.cpp:574-586 portal→STA retry; storage.cpp:801-803 default `CF_WEB_ENABLED` | replace with on-demand requests (scan job, creator screen ui.cpp:3111, P2P session) + inactivity shutdown |
| **SNTP** | gametime.cpp:224-249 `gt_sync_start`, :50-56, :62-63, :208-209, :240-245; .ino:517-519; config.h:460-467 | requires the §26 phone/user calibration setter first, otherwise `gt_is_valid()` can never become true |
| **Death / memorial / lineage grade** | sim.cpp:684-722, :1141-1155, :1268-1272, :1285-1290, :529-537, :1451-1455, :1935-1943, :1983-2011, :2098-2104, :887-918; ui.cpp:90-98, :177-180, :2443-2690, :3208-3212, :3225, :3326, :3352-3355, :3384-3389, :3474, :3481-3482, :3536; ui.cpp:1897-2004 (LINEAGE S7); godmode.cpp:67, :82, :365-372, :828, :865-878, :886-888, :1190-1193, :1460; genome.cpp:522-624 `genome_death_egg`; storage.cpp:272-328 `store_push_ancestor`, :158-176 `store_grade_from_cq`; nt_types.h:44 `STAGE_DEAD`, :60-70 `DeathCause`, :81/:86 `SCR_LINEAGE/SCR_MEMORIAL`, :146-155 `AbsenceTier`, :203-210 `DmgId`, :401-402 `PF_DEAD/PF_BURIED`, :432/:446/:461 `death_epoch/dmg_acc/death_cause`; config.h:188-202, :251-259, :385-393, :431; strings_es.h:142-150, :208-222, :271-282 | `PetSave` fields become padding until the schema is replaced (128 B assert); `ScreenId` renumbering shifts `s_cursor[SCR_COUNT]` (ui.cpp:119) and the webui comment coupling (ui.cpp:186-189) |
| **Absence punishment ladder, sulk, scar, care_miss, loneliness, discipline/scold, weight/obesity, overfeed→sickness, adult forms, storm damage, force-feed penalty, PUNKI refusal** | sim.cpp:1843-1897, :2019-2055, :1545-1548, :1672-1675, :794-805, :1681-1698, :858-877, :896-899, :973-976, :1607-1614, :219-242, :551-679, :1088-1106, :1558-1566, :1578-1582; config.h:245-246, :272-276, :294, :302-304, :327-328, :332-359, :395-411, :427-455; nt_types.h:166 `ACT_SCOLD`, :196 `ST_DISCIPLINE`, :49-58 `AdultForm`, :400/:407/:410 `PF_SCAR/PF_ABS_UNKNOWN/PF_HYBRID_ELIG` | §27, §57, §68 r14-15. Removing `ST_DISCIPLINE` changes `ST_COUNT` → `GainSave.slots` check refuses old ledgers (storage.cpp:634-637) — acceptable once the save schema is versioned |
| **Browser minigames + phone page** | index_html.h:26-73 (keep the container :1-25, :75-80); webui.cpp:92-96, :103-109, :115, :186-224, :236-242, :326-347, :397-566, :591-779, :1119-1129, :1135, :1153-1164; webui.h:161-189, :210-216; .ino:530-536 (`web_take_action` drain); ui.cpp:3291-3310; config.h:590-614 `MG_*`; nt_types.h:281-287 `MinigameId`, :226 `AL_WEB_CLIENT` (never raised); strings_es.h:351-353/:839-841 | sim.cpp:411-416, :1754-1814 (`sim_apply_minigame`) consume `MG*`; `WEB_CD_*`/`WEB_ACTION_GLOBAL_CD_S` (config.h:582-587) are the device-side action cooldowns too (sim.cpp:320-343) — rename, do not delete |
| **`/api/cfg` settings endpoint** | webui.cpp:821-968, :1062-1063, :1119-1125; webui.h:136-159; .ino:388, :538-543 | has no client in the shipped page |
| **mDNS** (optional) | net.cpp:232-253, :612-614, :33; config.h:507; ui.cpp:2427-2428 | §39 prefers the IP URL; Android unreliable (README.md:367) |
| **God-mode commands MATAR/CLIMA/TELEGRAM, ENFERMAR/CACAS rows, TLS heap label, old mating frames** | godmode.cpp (see rows above), :90-93, :396-400, :444-498, :1389-1390; sim.cpp:2106-2149 `sim_god_set_cq/sick/poop`, `sim_god_kill` | keep entry/marker/taint/soak/console skeleton |
| **BLE mating protocol content** (frames, rules-in-transport, contagion, RAM cooldown) | ble_social.cpp:440-452, :498-553, :566-609, :731-789, :806-816, :899-941; ble_social.h:44-55, :68-83, :103-123; config.h:549-550, :560-567; ui.cpp:100-106, :219-225, :453-480, :2049-2230; strings_es.h:297-310 | keep radio handoff (ui.cpp:2010-2047) and peer list rendering (:2184-2207) |
| **Dead macros** | config.h:84 `FW_BUILD_PROTO`, :97 `PIN_VBAT_ADC`, :102 `LED_ACTIVE_LOW`, :114 `OLED_I2C_ADDR_8BIT`, :163 `TAP_MAX_MS`, :168 `INPUT_POLL_MS`, :191 `DEATH_BLACK_MS`, :195-196 `DEATH_HEARTBEAT_BPM_HI/DEATH_BUZZ_MS`, :231 `SIM_TICK_HZ`, :342, :354, :463 `SNTP_WAIT_MS`, :565-567 `BLE_MATE_*`, :629 `QR_MAX_PAYLOAD`, :637 `QR_ECC_LEVEL_L`, :642-643 `GATE_*`, :651 `CITY_MAX_LEN` | no code references outside config.h (`TAP_MAX_MS` and `INPUT_POLL_MS` appear only in comments, input.h:11, :13, :39) |
| **Dead exports / never-consumed events** | listed in §13.2; `SIM_EV_CARE_MISS`, `SIM_EV_WEIGHT_OBESE` (sim.h:67, :78); 39 unreferenced `StrId`s (of 439: enum members named nowhere outside strings_es.h **and** outside all 20 `static_assert`-bounded index ranges reached through the `S_*(n)` macros, strings_es.h:1019-1037, :1045-1064 — `STR_BOOTING`, `STR_BOOT_CLOCK/NOCLOCK`, `STR_AF_LESS/HOME/PET`, `STR_ABS_SULK/FORGIVE/BACK_SHORT`, `STR_MEM_*` ×6, `STR_EGG_NEW_LINEAGE`, `STR_WISH_TITLE`, `STR_EV_UMBRELLA`, `STR_LIN_GEN`, `STR_ST_AGE/WEIGHT`, `STR_SO_FOUND`, `STR_WEB_*` ×5, `STR_SET_WIFI/CLOCK/NAME`, `STR_GOD_TITLE/WORKING`, `STR_ERR_NO_TIME/PIN/TOO_FAST/WX`, `STR_UI_ASLEEP/SAVING/PEERS`); `PetSave.guilt_level` written only to 0 | |
| **Build artefacts and stale refs in source** | petfx.cpp:1 `#line 1 "G:\\Mi unidad\\..."`; comments citing GAME_DESIGN/BRIEF documents absent from the repo (genome.h:67…, input.h:3, godmode.h:3, ui.h:1-24) | |
| **Docs** | README.md §5 (:288-355), §6 (:356-414), §9 (:636-664); CHANGELOG.md:46-72; ESTUDIO_VISUAL.md + docs/estudio_visual.html; `00-Investigacion-inicial.docx`; stale size figures | rewrite in English under `docs/`; keep bring-up procedures README.md:89-215 after the pin decision |

---

## 18. Architecture risks

| # | Risk | Severity | Evidence | Owner action |
|---|---|---|---|---|
| 1 | **PIN CONFLICT (unresolved)**: `config.h:92-96` compiles SDA=8, SCL=9, BTN_L=10, BTN_R=2, LED=5 ("tu cableado actual del TinyLLM", "OBLIGATORIO cambiarlo: 8 ya es SDA"); README.md:60-67/:71/:75, CHANGELOG.md:170, render.h:29-31, render.cpp:14-18 all say SDA=6, SCL=7, BTN_L=3, BTN_R=4, LED=8 and warn that GPIO2/8/9 are strapping pins (GPIO9 low at boot = download mode; GPIO2 must be high; GPIO8 = LED_BUILTIN). As compiled, SCL sits on the BOOT strap, a button on GPIO2, and `rd_fatal()` blinks GPIO5 where no LED exists. Nothing has ever run on hardware, so neither map is validated. | **High** | config.h:89-96; README.md:73-85; render.cpp:12-22; ESTUDIO_VISUAL.md:387 | Board owner inspects the physical wiring and declares the single source of truth; then make `config.h` §2 authoritative and fix every document. Do not resolve from code (§68 rule 3). |
| 2 | Wi-Fi policy is the inverse of §40/§68 r6: STA held up permanently whenever the web toggle (default on) is set, retried every 30 s; ~50 KB heap and the radio busy on the home screen | High | .ino:182-226; storage.cpp:801-803; net.cpp:574-586 | invert policy to on-demand; add inactivity shutdown |
| 3 | Corrupt/foreign save silently becomes a new pet and the bad blob is overwritten the same boot (§48, §68 r14); no migration path for any of the 5 versioned blobs | High | storage.cpp:508-519, :826-843; .ino:279-283, :304-333, :396 | tri-state load + SAVE ERROR dialog + checkpoint + migration table before the schema changes |
| 4 | Single-radio constraint plus a legacy-advert protocol that cannot carry §15: 19 B payload, half-duplex, ~1-2 s per hop; 32 BLE init/deinit sessions per boot; every Wi-Fi scan forces BLE down | High | ble_social.cpp:62-91, :358-374; config.h:559; net.cpp:196-204; WiFiScan.cpp:75 | decide session transport (§7.3) behind a `Transport` interface; schedule scan vs discovery |
| 5 | Game rules inside the transport module and a responder that auto-accepts and persists an egg without confirmation — would violate §42/§68 r12-13 if reused for trade | High | ble_social.cpp:498-534, :749-771; ui.cpp:2071-2083 | move rules to `game/Breeding`; explicit consent step |
| 6 | `ui.cpp` monolith: 3,578 lines, 74 mutable file-scope `static` variables (column-0 `static` declarations that are neither functions nor `const`; 86 including `const` tables), three parallel switches (input :3402-3416, draw :3514-3550, service :3419-3508), enter/leave hooks for only 7/3 screens; every new spec state (BOX, BATTLE, TRADE, ENCOUNTER, CAPTURE, CREATOR, ERROR, SLEEP) would land in the same TU | High | ui.cpp | Screen table `{enter,update,render,input,leave}` per `ScreenId` |
| 7 | "Boolean spaghetti" the spec §6 forbids: three transitions bypass `ui_goto()` and re-implement its reset (death ui.cpp:2460, hatch :2729, god :3463); sub-states ride on screens as phase enums (`DeathPhase`, `HatchPhase`, `SocPhase`, `GameState.phase`, `s_set_page`) and are special-cased in `ui_fps/ui_input_locked/ui_service/ui_draw` | Medium | ui.cpp:596-599, :699, :3443-3452, :3477, :3540 | funnel everything through the state table |
| 8 | ctags traps in the `.ino`: an `#include` below a definition or a function-local `static` silently gives `nt_pet_view()/nt_cfg_view()` internal linkage (mutes Telegram with no diagnostic) and can break prototype injection | Medium | .ino:8-21; telegram.h:85-89 | move all logic out of the `.ino` (removes the trap) |
| 9 | Layering inversions: renderer reads live sim/genome (petfx.cpp:26-28, :580, :764-770, :894; actfx.cpp:588, :610, :636-638); panel UI depends on web module policy (ui.cpp:348, :920) and on godmode; replacing `PetSave` breaks compilation of petfx/actfx until a view struct exists | Medium | as cited | introduce a `PetView` struct; relocate `web_pose_of/web_mood_index` |
| 10 | Blocking calls on the single cooperative loop: Telegram TLS 1-3 s (8 s), `RADIO_SETTLE_MS` 250 ms per stack flip, `WiFi.disconnect(…,100)`, `handleClient()` up to 5 s on a stalled client; input is loop-polled with no ISR so presses during a stall are lost; ~305 ms tap latency from the double-tap window | Medium | .ino:504-513; net.cpp:142-144, :274; core `Parsing.cpp:48-52`; input.cpp:186-189, :283-286 | delete TLS; timed `NPH_SETTLING`; drop double-tap; consider a timer-driven sampler |
| 11 | Clock: without Wi-Fi `gt_is_valid()` can never become true (only SNTP sets it), so sleep window/daily systems are disabled and every boot charges zero absence; unknown-clock boots on power loss charge a 6 h penalty floor; TZ edits apply only after reboot; no rollback policy for a wrong phone time | Medium | gametime.cpp:266-278; .ino:316-326; sim.cpp:1920-1930; gametime.cpp:161-164; storage.cpp:675-680 | `gt_set_epoch()` from phone/user, persisted `timeCalibrationState`, rollback rule |
| 12 | No tests at all; host harnesses cited in comments were never committed; regressions during retuning are invisible | Medium | §15 | `tests/` in Phase 2 |
| 13 | Persisted wire formats frozen at 128 B/256 B with 2/3 spare bytes; 21 headers include `nt_types.h`; `GainSave.slots == ST_COUNT` check | Medium | nt_types.h:466, :472-479, :559, :563-567; storage.cpp:634-637 | new `SaveSchema` v2 with migration, not incremental patching |
| 14 | Documentation drift: three flash/RAM figures, two FQBNs, README claims a web settings UI that does not exist, docs cite uncommitted tests and a missing backup folder | Medium | README.md:132, :136, :262, :387, :643; ESTUDIO_VISUAL.md:10, :66, :455, :517 | rewrite docs in English after the pin decision |
| 15 | Display failure halts the device forever (`rd_fatal` noreturn) — a loose I2C wire freezes the game (§47) | Low | .ino:351-353; render.cpp:1034-1074 | recoverable ERROR state |
| 16 | No power management: busy loop, no sleep states, no display-off, `rd_power()` never called; uncalibrated clock estimate is blind to deep sleep | Low (until Phase 6) | .ino:470-545; render.cpp:399-411; gametime.cpp:129, :297-298 | §45 state machine on the `s_input_ms` inactivity hook |
| 17 | Product identity baked into persisted names (`NVS_NS "notta"`, AP prefix, mDNS) and the sketch folder name; renaming NVS_NS orphans saves | Low | config.h:472, :502, :507 | decide keep vs migrate |
| 18 | God mode ships ON, reachable by a 5 s hold with no PIN, permanently taints the save on entry, includes factory reset | Low | config.h:72; godmode.cpp:711-730, :639-671 | release build sets `GOD_MODE_ENABLED 0` |
| 19 | Repo hygiene: one commit, spec untracked, `.gitignore` = `*.bak`, stray `.docx`, no CI | Low | `git status` | commit docs, ignore build outputs, add CI later |

---

## 19. Memory/RAM risks

### 19.1 Where the 72,748 B of static RAM go (baseline build, linker map)

| Owner | Bytes | Notes |
|---|---|---|
| BLE (`bt` 20,347 + `btdm_app` 1,207 + `ble_mesh` 892) | 22,446 | `btm_cb` 2,628, `gatt_cb` 2,484, adv buffer pools 1,740+1,680+1,200+1,200; mesh objects linked though unused |
| Wi-Fi (`net80211` 8,726 + `pp` 3,833 + `phy` 2,025 + `wpa_supplicant` 1,782 + `coexist` 1,436) | 17,802 | `g_cnxMgr` 3,832, `s_wifi_nvs` 1,284, `gWpaSm` 1,048 |
| lwIP 4,415 + mDNS 1,958 | 6,373 | `dns_table` 1,184 |
| spi_flash 5,166, FreeRTOS 3,208, hal 1,150, libc 868, mbedTLS 914 | 11,306 | `xIsrStack` 2,096 |
| U8g2 1,101 (incl. 1,024 B framebuffer), Arduino core 1,097 | 2,198 | |
| **Project code** | **8,895** | weather 1,901 · webui 1,560 · telegram 1,219 · ble_social 770 · petfx 764 · ui 557 · .ino 401 (`g_cfg` 256 + `g_pet` 128) · render 343 · sim 315 · net 300 · storage 251 · godmode 181 · qr 166 · gametime 79 · input 47 · actfx 33 · genome 8 |
| Other IDF components | ~3,700 | nvs, esp_system, newlib, … |

`INDEX_HTML` (47,182 B) lives in `.flash.rodata`, **zero RAM** (nm labels it `d` only because the C3 linker script marks `.flash.rodata` writable; filter by DRAM address 0x3FC94600..0x3FCA6240). Map: `.dram0.data` @0x3FC94600 16,340 B + `.dram0.bss` @0x3FC985E0 56,408 B = 72,748 B; `_heap_start` = 0x3FCA6240. `.iram0.text` 83,278 B is carved from the same SRAM (`.dram0.dummy` 0x3FC80000 83,456 B mirrors it), so the Arduino "327,680 maximum" and the 254,932 B it prints as "leaving … for local variables" (327,680 − 72,748) are nominal, not link-time heap sizes: the linker's `dram0_0_seg` is 0x3FC80000 + 0x4E710, i.e. 0x3FCCE710 − 0x3FCA6240 = 165,072 B above `_heap_start` at link time, while the author's runtime measurement is 179,836 B free at boot (net.cpp:5-7) — the two are not directly comparable because the runtime allocator also registers SRAM outside the linker segment.

Measured deltas: BLE −23,688 B RAM / −721,632 B flash; WEB −1,656 / −95,454; WEATHER+TELEGRAM −3,208 / −123,834; GOD −176 / −15,820; all off → 41,996 B RAM.

### 19.2 Heap and coexistence

Author-measured, not re-measured (no hardware): boot free heap 179,836 B; Wi-Fi STA ≈ 50 KB; Bluedroid ≈ 70 KB; one TLS session ≈ 45 KB needing a 48 KiB contiguous block (net.cpp:5-7, :616-626; config.h:512). Both stacks resident would leave ~60 KB after TLS is gone, before lwIP pbufs, WebServer Strings, and fragmentation — **not viable**; the `RadioMode` single-stack invariant must be kept, and each flip costs 250 ms + `BLEDevice::init/deinit` (unmeasured) + one of 32 BLE sessions (claimed ~672 B leak/cycle, ~21.5 KB worst case per boot). Removing Telegram eliminates the largest heap spike in the firmware (mbedTLS record buffers + `HTTPClient` String body). Per-request heap churn remains in `WebServer` (Strings for args/headers, unbounded `Content-Length` malloc, core `Parsing.cpp:44-75`).

### 19.3 What the Pebblebol data will cost (estimates)

| Item | Sizing | RAM | Flash / NVS |
|---|---|---|---|
| `PebbleInstance` per §10 (id u32, speciesId u8, nickname[13], level u8, xp u16, type u8, hp/maxHp u16×2, atk/def/spd u8×3, hunger/happiness/cleanliness u8×3 or i32×3, status u8, moves[4], evolutionState u8, lineage 8-16 B, creationSeed u32, traits 2-4 B, lifetimeStats ~12 B, lastUpdated u32, header/CRC 6 B) | ≈ 78-96 B without sprite; +64 B (16×16 × 2 frames, 1-bit) if a custom sprite is inline | — | — |
| Box of 10 + `activePebbleId` | 10 × 96 B = 960 B (1,600 B with inline sprites) | 1-1.6 KB live copy | NVS blob 960 B ≈ 30 entries + header (1,600 B ≈ 50 entries) of ~504 usable; current use ~32 entries → fits, but keep custom sprites in their own keys so a full-Box write does not rewrite 1.6 KB |
| Per-Pebble sim accumulators (re-entrant Care) | today ~120 B of file-scope state per pet (sim.cpp:89-149) | 10 × 120 B = 1.2 KB | — |
| 60 `SpeciesDefinition` (id, family, stage, type, 4 base stats, moves[4], evolution {target, level}, spawn/rarity/compat, name ptr, sprite ptr ≈ 48 B) | 60 × 48 = 2,880 B + names ~600 B | 0 (constexpr rodata) | ~3.5 KB flash |
| ~30 attacks × ~12 B, ~10 items × ~8 B, encounter tables | — | 0 | < 1 KB flash |
| 60+ creature sprites, 2 frames each | 16×16: 32 B/frame → 3,840 B; 24×24: 72 B/frame → 8,640 B; 32×32: 128 B/frame → 15,360 B | 0 (flash); petfx cache 400 B handles ≤ 40×40 | current atlas 10,893 B under a 14,336 B assert (sprites.h:1259-1260) → 24×24 roster fits after removing the 38 Nottamagochi sets; 32×32 needs the budget raised (flash is not the constraint: 1.0 MB free) |
| Custom creator sprites (≤ 10, 16×16 × 2 frames) | 64 B each | 640 B if all resident, else 64 B per active | NVS 640 B ≈ 20 entries |
| Battle state (2 teams × 3 refs, HP/status copies, move log) | ≈ 300-500 B | < 1 KB | — |
| Wi-Fi scan result hash set (e.g. 64 × 4 B) | 256 B | 256 B | cooldown timestamps in NVS: 64 × 8 B = 512 B ≈ 16 entries |
| Creator server buffers (JSON body cap, e.g. 2 KB; validator scratch) | — | ~3 KB while active | — |

Net: **all Pebblebol domain data fits in < 10 KB static RAM and < 5 KB NVS**; the 20 KB NVS partition is adequate if the Box, cooldowns and custom sprites are separate keys. The real memory constraints stay the radio stacks (BLE 23.7 KB static + ~70 KB heap; Wi-Fi ~25 KB static + ~50 KB heap) and the 3 MB app partition without OTA (§61): the current 2.1 MB image leaves no room for a two-slot OTA layout on 4 MB flash unless BLE mesh/Telegram/weather are dropped (which returns ≥ 124 KB immediately and up to 722 KB if BLE were replaced by ESP-NOW).

---

## 20. Recommended migration plan

Phase-aligned with spec §52; the task-level checklist is to be written next as `PEBBLEBOL_IMPLEMENTATION_PLAN.md` (spec §69 second deliverable; not yet in the repo).

| Phase (§52) | Goal | Key moves grounded in this audit | Exit criterion |
|---|---|---|---|
| **0. Decisions (blocking)** | resolve the two owner decisions | (a) pin map from the physical board (§18 risk 1); (b) P2P session transport: ESP-NOW vs BLE5/GATT (§7.3); (c) keep or migrate `NVS_NS "notta"` | decisions recorded in the plan |
| **1. Archaeology** | this document | commit `docs/`, widen `.gitignore`, fix FQBN/size drift in docs | done |
| **2. Core engine** | buildable, testable core with no cloud | delete weather/Telegram/SNTP/browser games (§17 tables) keeping `Config` layout legal; invert radio policy to OFF-by-default; `tests/` host build for sim/genome/storage CRC/input/QR; `SaveSchema` v2 (`PebbleInstance`, Box[10], cooldowns, identity, `timeCalibrationState`, PIN state) with v1→v2 migration and tri-state load + SAVE ERROR dialog + checkpoint; `gt_set_epoch()` + `POST /api/time`; move code out of the `.ino` into `src/` per §1.2; introduce `PetView` to break petfx→sim | `arduino-cli compile` 0 warnings; `make -C tests` green; boots to HOME with a Box |
| **3. Virtual pet** | care/XP/level/evolution/minigames | make sim re-entrant (per-Pebble state), retune decay to §27 (hours not minutes), remove death/ladder/sulk/scold/weight, keep regen/sleep/anti-farm; XP from elapsed time + interactions (§6); level-gated `EvolutionRule` reusing `SIM_EV_STAGE_UP` + hatch ceremony; `Minigame` interface + manager wrapping MEMORIA/REFLEJOS and adding ≥ 3 spec games with a seeded RNG; drop double-tap; Screen table in ui | §67 Pet + Games items |
| **4. Battle engine** | pure `game/Battle` module | data tables for types/attacks/species in `data/`; deterministic battle with host tests; `BattleRenderer` on `rd_bar`/`rd_flash`/`rd_shake`/`af_blit`; local protocol codec as a pure module before any radio | battle vs AI on device; protocol codec tested on host |
| **5. Exploration** | scan-only Wi-Fi | `NPH_SCANNING` in `net.cpp` over `WiFi.scanNetworks(async)`; on-device BSSID/SSID hashing (§44); epoch-based persisted cooldowns; encounter/capture/items; Wi-Fi off after each scan | §67 Exploration items |
| **6. Activity** | honest activity score | `hardware/Motion` interface with "unsupported" implementation; activity = elapsed time + interactions + network diversity + peers; deep-sleep prerequisites: `time()` as monotonic source, `BOOT_DEEPSLEEP` kind, `rd_power()` | growth accrues across sleep |
| **7. Social** | discovery + sessions | keep BLE advert beacon (new payload: protocolVersion, deviceId, capabilities, CRC) for discovery; session transport behind `Transport`; consent on both sides; atomic trade; breeding on `genome_breed` with `compatibilityGroup`; unify the duplicated CRC/mating rules | two boards trade and breed |
| **8. Creator** | PIN + AP + QR + editor | reuse `web_begin/pin_ok/rate_take/send_P`, add `/api/schema|validate|pebble|time|ping`, body-size cap before parse, inactivity shutdown, QR without `?k=`; new page source committed with its generator; sprite format = existing XBM (32 B per 16×16 frame) | §67 Creator items |
| **9. Content** | 60+ species | generator for XBM atlas committed; species/attacks/items tables; names per §53/§54 | roster in `data/` |
| **10. Polish** | release | diagnostics screen from god-mode plumbing (§49 fields), `GOD_MODE_ENABLED 0` in release, power states, battery once hardware exists, English docs | §67 complete |

Rules of the road carried from §68 and this audit: never change a persisted layout without a version bump and migration; never add a second radio owner; keep `sim`/`genome`/`battle` free of Arduino includes so the host tests stay valid; compile after every subsystem (`arduino-cli compile … --warnings all`) and keep the warning count at zero.
