# PEBBLEBOL_IMPLEMENTATION_PLAN.md

| Field | Value |
|---|---|
| Title | Pebblebol implementation plan (spec §69 second deliverable; task checklist for §52 Phases 1-10) |
| Date | 2026-09-02 |
| Status | Plan complete. Phase 1 (archaeology + housekeeping) done; Phases 2-10 open. |
| Source commit | `b53cfe4` ("first commit", the only commit; `docs/` and the audit are untracked) |
| Inputs | Audit: `/home/user/Pebblebol/PEBBLEBOL_IMPLEMENTATION_AUDIT.md` (669 lines). Spec: `/home/user/Pebblebol/docs/PEBBLEBOL_PRODUCT_SYSTEM_SPEC.md` (2,708 lines; §5 architecture, §6 states, §52 phases, §67 done, §68 rules, §69 audit items). Three plan drafts (buildable / reuse / spec-purist) and two judge verdicts, merged here. |
| Firmware | `/home/user/Pebblebol/sketch_aug30b/` — "Nottamagochi" `FW_VERSION "1.0.0"`, 26,703 lines in 16 `.cpp` + 1 `.ino` + 21 `.h` |
| Build baseline (verified 2026-09-02) | `arduino-cli compile --fqbn esp32:esp32:esp32c3:PartitionScheme=huge_app,CDCOnBoot=cdc --warnings all` — core esp32 3.1.1, U8g2 2.35.30; 2,105,548 B flash (66 % of 3,145,728), 72,748 B static RAM (22 % of 327,680), 0 project warnings. All `FEATURE_*`=0 + `GOD_MODE_ENABLED`=0: 1,106,556 B / 41,996 B, 12 unused-function warnings (telegram.cpp x9, ui.cpp x3). |
| Tests baseline | None in the repo (audit §15). `g++ 13.3` on the host compiles `sim.cpp genome.cpp qr.cpp gametime.cpp input.cpp` with `-std=c++17 -Wall -Wextra` at 0 warnings. |
| Hardware run status | Never executed on a physical board (README.md:801, CHANGELOG.md:193). |
| Hardware baseline | `docs/PEBBLEBOL_HARDWARE_AND_BATTERY_SPEC.md` (V1: ESP32-C3 SuperMini + 0.96" OLED + 2 buttons + **passive piezo** + **2xAAA**). Reconciled in `docs/hardware_reconciliation.md`: the piezo makes `hardware/audio.h` a real implementation instead of a null stub, and the 2xAAA supply raises decisions D9/D10. Radios-off, OLED-off, timestamp-driven state and checkpoint saves were already the plan's direction. |
| Language rule | Code, comments, docs in English. User-facing UI strings may stay Spanish (`strings_es.h`). |
| Effort key | S = hours, M = 1-3 days, L = > 3 days (one engineer). |
| Task count | 139 checkbox tasks in §3 (2 done, 137 open; per phase: P1 8, P2 54, P3 13, P4 12, P5 11, P6 8, P7 11, P8 7, P9 7, P10 8). Every `- [ ]` / `- [x]` line in §3 is one task; §2 is an ordered surgery list executed by Phase-2 commits and §5 is spec §67 copied as done-criteria — neither adds tasks. |

Conventions used throughout:

- `Pn-Cm` = commit `m` of Phase `n`. Every commit must pass **the gate** (§1.1). Each `Pn-Cm` is one git commit; the phase-level commit message in §3 is the message of the phase's exit/tag commit.
- `file:line` citations refer to the flat sketch at `b53cfe4` until the `src/` move (P2-C8); after the move the same line numbers apply to the moved files (a pure move changes no lines).
- "audit §N" = section of `PEBBLEBOL_IMPLEMENTATION_AUDIT.md`; "spec §N" or "§N" = the product spec.

---

## 0. Decisions

### 0.1 Decisions taken by this plan (owner may override; each is one commit to reverse)

| # | Decision | Reason / evidence |
|---|---|---|
| T1 | **Base approach: always-buildable strangler-fig.** Every commit compiles at 0 project warnings and keeps `make -C tests check` green; removals happen one obsolete subsystem per commit in the flat layout before the `src/` move so the audit's `file:line` surgery lists stay valid; `ui.cpp` is migrated one screen per commit with fall-through to the old switches. | §51 "keep the repository buildable", §68 r19; audit §17 line lists are only valid in the flat layout. |
| T2 | **Reuse over rewrite (§68 r20).** File names and `nt_`/`sim_`/`rd_`/`gt_`/`net_`/`store_` prefixes are kept; files are `git mv`'d into `src/…`; `render.cpp` stays one TU (it is both §5 `hardware/Display` and `ui/Renderer`) and lives in `src/ui/`; `sim.cpp` stays single-instance because spec §27 says "Active Pebble only" (stored Pebbles use a stateless `box_recover()`); the 16 B `Genome` (nt_types.h:309-330) is embedded in `PebbleInstance` so `genome_breed()` (genome.cpp:420-511, `genome.h:85`) is reused whole; the milli-point integrator keeps `int32 care[5]` + `int16 care_rem[5]` exactly as `accum_stat()` needs them (sim.cpp:179-197, `STAT_MILLI_MAX 100000` config.h:232). | audit §16 reuse table; judges' verdict that u8 care stats break `accum_stat`. |
| T3 | **ENERGY is kept as a fifth, mostly internal care stat.** The sleep machine reads `p.stat[ST_ENERGY]`/`pct_milli(ST_ENERGY)` (sim.cpp:764-765) and the regen rule keys off it (sim.cpp:920-935). The four §27 stats are the displayed ones. | audit §16 "Sleep machine, health regen rule" = reuse. |
| T4 | **Content = generated `constexpr` tables from committed JSON** (`tools/content/*.json` → `src/data/*_table.h`), never parsed at runtime; the same generator emits the creator `/api/schema` JSON constants so device and page cannot disagree. `PROGMEM` is a no-op on ESP32; `constexpr` already lands in `.flash.rodata` (sprites.h:9-10 pattern). | §32 "follow the existing project and ESP32 memory constraints" (spec example shape is JSON). |
| T5 | **Persistence = versioned blobs, one NVS key pair per blob** (`<key>0`/`<key>1`, header `seq`), loader picks the highest valid `seq`, writer overwrites the inactive copy and verifies bytes written (storage.cpp:568-574 pattern); plus a **second NVS partition `nvs2`** (custom `partitions.csv`, honoured by core 3.1.1 `platform.txt:113`) holding a daily/level-up checkpoint of Box+active, because `initArduino()` erases the whole default `nvs` partition on `ESP_ERR_NVS_NO_FREE_PAGES`/`NEW_VERSION_FOUND` before `setup()` (core `esp32-hal-misc.c:287-300`). | §48 "at least one previous valid save", §31 "must survive firmware restart"; audit §5 core hazard. |
| T6 | **Radio: OFF is the resting state; exactly one stack resident** (`RadioMode`, nt_types.h:139-144). Wi-Fi is requested only by SCAN (NETWORK), LINK and CREATOR states and released on exit. SNTP is deleted; the clock is calibrated from the phone (`POST /api/time`) or the on-device time screen. | §40, §68 r5-r6; audit risks 2 and 11. |
| T7 | **Local protocol proven on host first**: codec + session + lockstep battle protocol run over an in-process loopback with fault injection (drop/dup/reorder) in Phase 4; the device transport (D2) arrives in Phase 7 behind the §59 `Transport` seam. Battle over link is **lockstep with a per-round state hash**; a mismatch ends the battle with `DESYNC` and no rewards — neither side trusts the other (§68 r12). | audit §7.3; §15, §59. |
| T8 | **Input**: the double-tap gesture is retired (tap latency 305 ms → ~25 ms, input.cpp:283-286); button sampling moves to a 5 ms `esp_timer` sampler feeding a small ring so presses during blocking peripheral init are not lost (input is loop-polled today, input.cpp:42). §7 grammar: A = TAP_L (confirm/next), B = TAP_R (back/cancel); `HOLD_R = back` (ui.cpp:3389-3395) retired. | §7, §45 "use hardware timers", §46 "responsive even while peripheral operations are initialized"; audit risk 10. |
| T9 | **Unknown clock charges zero absence** (drops the 6 h `ABSENCE_LARGA_S` floor, sim.cpp:1920-1930, config.h:428) and encounter cooldowns fall back to a per-boot RAM cooldown while `CAL_UNSET`. | §27 "never require attention", §21 "avoid resetting when battery dies". |
| T10 | **`rd_fatal()` (noreturn, render.cpp:1034-1074) is replaced by the `ERROR` state in Phase 2**, not Phase 10. | §47 "must never get stuck permanently"; audit risk 15. |
| T11 | **Sketch folder renamed `sketch_aug30b/` → `Pebblebol/`** and `.ino` → `Pebblebol.ino` in P2-C1 (folder must equal `.ino` basename, README.md:131-134). Owner may keep the old name (decision D3 below); nothing else depends on it. | audit risk 17. |
| T12 | **UI strings stay Spanish in `strings_es.h`**; identifiers, comments and docs become English (Spanish config.h:15-76 block translated in P2-C8; Spanish README/CHANGELOG/ESTUDIO_VISUAL archived under `docs/legacy/` in P10-C5). | Language rule in the task context; spec allows. |
| T13 | **Species/attack/item roster sprites are 24x24, 2 frames, XBM (72 B/frame)**; 60 species = 8,640 B, fits the atlas after the 38 Nottamagochi sets are removed (`SPRITE_DATA_BYTES` assert sprites.h:1259-1260; audit §19.3). Custom creator sprites use the same format. | audit §19.3; §37 one pipeline. |

### 0.2 Open decisions for the owner (reported, not resolved; blocking commits named)

| # | Decision | Evidence | Blocks | Plan default meanwhile |
|---|---|---|---|---|
| **D1** | GPIO map (pin conflict) | **DEFERRED BY OWNER (2026-09-02)** — keep the values the repository already carries; revisit later | `config.h` compiles `PIN_SDA 8, PIN_SCL 9, PIN_BTN_L 10, PIN_BTN_R 2, PIN_LED 5` with the human comments "tu cableado actual del TinyLLM" / "OBLIGATORIO cambiarlo: 8 ya es SDA". README §2, CHANGELOG, `render.h`, `render.cpp` all document the other map (SDA=6, SCL=7, BTN_L=3, BTN_R=4, LED=8) and warn that GPIO2/8/9 are strapping pins. Nothing has ever run on hardware. | **Values stay exactly as committed.** `PB_PINS_CONFIRMED` is NOT defined, so the guard `static_assert`s added in P2-C8 stay dormant. | P2-C0 first flash (hardware track only), P6-C3 deep sleep | Owner defers; consequences recorded below the table. |
| **D2** | Peer-session transport: BLE vs Wi-Fi (ESP-NOW) | **CLOSED — ESP-NOW (2026-09-02, owner)** | The legacy BLE advert carries 19 B/frame and cannot carry the §15 message set; GATT was rejected by the original author for stability; each BLE bring-up burns one of 32 sessions with a claimed ~672 B Bluedroid leak; BLE costs 721,632 B flash / 23,688 B static RAM. ESP-NOW ships in the core (250 B/frame, unicast + send-callback ACK) and needs the same `WIFI_STA` residency the §40 scanner already requires. | ESP-NOW is the transport, behind the §59 `Transport` seam. `FEATURE_BLE 0` in the release build. | P7-C1 | Decided: ESP-NOW. See the consequences below the table. |
| **D3** | **Sketch folder rename** `sketch_aug30b` → `Pebblebol` (T11) and product identity in persisted names: `NVS_NS "notta"` (config.h:472), AP prefix `NOTTAMAGOCHI-` (config.h:502), mDNS `nottamagochi.local` (config.h:507). No device has ever run this firmware, so renaming orphans nothing real. | audit risk 17 | P2-C1 (folder), P2-C9 (NVS namespace) | Rename folder in P2-C1; new namespace `"pbbl"` in P2-C9 with a one-shot import of a legacy `"notta"` save (migration test proves the machinery); AP prefix `PEBBLEBOL-` in P8-C2; mDNS deleted (P2-C5). |
| **D4** | **Language of UI strings.** Spanish (`strings_es.h`, 439 strings, StrId mechanism) vs English. | task language rule; spec header | P2-C11 (new string blocks) | Keep Spanish user-facing strings (T12); new BOX/BATTLE/NET/LINK/CREATOR/ERROR blocks written in Spanish in the same mechanism; an `strings_en.h` twin is a one-file swap later if the owner wants English. |
| **D5** | **Panel variant** SSD1306 vs SH1106 (`DISPLAY_IS_SH1106` config.h:64; render.h:34-38). | audit §3 | P2-C0 | Keep 0 (SSD1306). |
| **D6** | **Partition table / OTA room (§61).** `huge_app.csv` = nvs 20 KB @0x9000, otadata 8 KB, app0 3 MB, spiffs 896 KB **unused**, coredump 64 KB (core `tools/partitions/huge_app.csv`); no OTA slot. Core 3.1.1 honours a `partitions.csv` in the sketch folder (`platform.txt:113`). | audit §5, §19.3 | P2-C9d | Custom `Pebblebol/partitions.csv`: `nvs 0x9000 0x5000 | otadata 0xE000 0x2000 | app0 0x10000 0x300000 | nvs2 0x310000 0x10000 | spiffs 0x320000 0xD0000 (reserved, unused) | coredump 0x3F0000 0x10000`. `nvs2` (64 KB) is the checkpoint partition (T5). OTA stays a non-goal for V1; if D2 = ESP-NOW (`FEATURE_BLE 0`, image ~1.4 MB) a two-slot `app0/app1 0x1E0000` layout fits later without touching game code — recorded, not scheduled. |
| **D7** | **Creator inactivity grace** (§34): 120 s vs 300 s. | §34 | P8-C2 | `ConfigV2.creator_idle_s` default 300, editable in SETTINGS. |

Decisions live in `docs/decisions.md` (created P1-C1); each closed decision is one commit that also updates that file. **D1 is deferred by the owner** (keep the committed pin values) and **D2 is closed in favour of ESP-NOW** — see that file for the consequences each one carries into later phases.

---

## 1. Target architecture

### 1.1 The gate every commit must pass

`tools/check.sh` (created P2-C1, run before every commit; CI from P1-C1 for tests, P2-C1 for the firmware build):

1. `arduino-cli compile --fqbn esp32:esp32:esp32c3:PartitionScheme=huge_app,CDCOnBoot=cdc --warnings all /home/user/Pebblebol/Pebblebol` exits 0 **and** the log contains zero `warning:` lines pointing into the sketch (core/library warnings ignored, as the baseline does). The FQBN **keeps** `PartitionScheme=huge_app`. P2-C9d measured why: arduino-cli does copy the sketch's `partitions.csv` into the build and esptool flashes that table, so `nvs2` is genuinely in force, but `upload.maximum_size` comes from the board menu rather than the CSV. Dropping the option lowered the size ceiling to 1,310,720 B and failed the 1.87 MB build. Two gates pin the real behaviour instead: the app maximum must read 3,145,728 B, and the table about to be flashed must contain `nvs2`. Recorded in `docs/decisions.md` D6.
2. `make -C tests check` exits 0 (`g++ -std=c++17 -Wall -Wextra -Werror`, no external framework).
3. The "Sketch uses" / "Global variables use" lines are parsed and compared with `GATE_FLASH_MAX` / `GATE_GLOBALS_MAX` (config.h:642-643 — referenced nowhere today, audit §14); the gate makes them real.
4. Grep gates (added as the corresponding commits land): `grep -rE "s_screen\s*=" src/ui src/app` == 0 outside `app/state_machine.cpp` (P2-C11 exit). **Scoped to `ui/` and `app/` deliberately**: `dev/godmode.cpp` has an unrelated `static uint8_t s_screen` for its console sub-screen (33 hits), so the tree-wide form fails on false positives; `grep -c "WiFi.begin" src/` == 0 (P5-C1); `grep -c "genome_rand\|esp_random" src/game src/minigames` == 0 (P2-C2/P3-C4); `grep -c "rd_u8g2()" src/app src/minigames src/game` == 0 (P10-C4).

`tools/build_matrix.sh` (P2-C1, run at every phase tag): baseline, `FEATURE_BLE=0`, `FEATURE_WEB=0`, `GOD_MODE_ENABLED=0`, all-off, release. Warning-free in every variant from P2-C5 on (today all-off has 12 unused-function warnings).

### 1.2 Directory tree

arduino-cli compiles `<sketch>/src/**` recursively; every `#include` inside `src/` is relative to the including file (`"../core/config.h"`), so neither toolchain needs an extra `-I`. **Probe this first** (P2-C8 skeleton compile) before moving anything.

```text
/home/user/Pebblebol/
  .gitignore                        build/, tests/bin/, *.o, *.bak, .arduino-cli/
  .github/workflows/ci.yml          make -C tests check (P1-C1); + arduino-cli compile with cached core (P2-C1)
  README.md                         English: bring-up (after D1), build, test, architecture (P10-C5)
  PEBBLEBOL_IMPLEMENTATION_AUDIT.md
  PEBBLEBOL_IMPLEMENTATION_PLAN.md  (this file)
  docs/
    PEBBLEBOL_PRODUCT_SYSTEM_SPEC.md
    decisions.md                    D1..D7 with outcomes (P1-C1)
    save_schema.md                  byte layouts, NVS keys, partition table (P2-C9)
    protocol.md                     §15 wire format as implemented (P4-C5)
    content.md                      authoring rules §53, regeneration, balance sheet (P9-C1)
    legacy/                         Spanish README, CHANGELOG, ESTUDIO_VISUAL, estudio_visual.html (P10-C5)
  tools/
    build.sh                        arduino-cli wrapper: FQBN, --warnings all, size parse, warning grep
    check.sh                        build.sh + make -C tests check + gates (§1.1)
    build_matrix.sh                 FEATURE_* / GOD / release variants
    gen_content.py                  tools/content/*.json -> src/data/*_table.h + src/data/creator_schema.h (P4-C1, complete P9-C1)
    gen_sprites.py                  tools/sprites/*.txt (ASCII art) -> src/data/sprites_pebbles.h (P9-C3; re-creation of the uncommitted sprite_src.py, sprites.h:3-4)
    gen_index_html.py               web/creator/index.html -> src/data/index_html.h (P8-C4; re-creation of the uncommitted build.js, index_html.h:2)
    creator_smoke.sh                curl script over the 7 §38 routes incl. 413 and 403 (P8-C5)
    content/ species.json attacks.json items.json evolution.json encounters.json balance.json
    sprites/ <species>.txt
  web/creator/                      page source: index.html app.js sprite_editor.js (P8-C4)
  tests/                            host only (§4)
    Makefile  nt_test.h  host_shims.cpp  fakes/  fixtures/  legacy/  golden/  test_*.cpp  tools/
  Pebblebol/                        the sketch (folder == .ino basename; D3)
    Pebblebol.ino                   #include "src/app/app.h"  setup(){app_setup();}  loop(){app_loop();}
    partitions.csv                  D6 (P2-C9d)
    src/
      core/        config.h  nt_types.h  strings_es.h  version.h  crc16.h/.cpp  rng.h/.cpp  platform.h
      app/         app.h/.cpp (ex .ino setup/loop/logic_tick)  state_machine.h/.cpp  input_router.h/.cpp
                   screens_table.cpp (one ScreenDef row per §6 state)
      game/        pebble.h/.cpp  species.h/.cpp  box.h/.cpp  sim.h/.cpp (Care)  xp.h/.cpp  evolution.h/.cpp
                   battle.h/.cpp  battle_ai.h/.cpp  validate.h/.cpp  capture.h/.cpp  inventory.h/.cpp
                   cooldowns.h/.cpp  encounters.h/.cpp  exploration.h/.cpp  activity.h/.cpp
                   breeding.h/.cpp (over genome)  genome.h/.cpp  trade.h/.cpp  corruption.h/.cpp
      minigames/   minigame.h  manager.h/.cpp  games/<name>_logic.cpp  games/<name>_draw.cpp
      ui/          render.h/.cpp (U8G2 owner = §5 Display + Renderer)  gfx.h  gfx_u8g2.cpp
                   screen.h  screens/*.cpp  ui_core.h/.cpp (px_*, nav, modal/alert/toast, list/ring widgets)
                   dialog.h/.cpp  menu.h/.cpp  pet_view.h/.cpp  petfx.h/.cpp  actfx.h/.cpp
                   battle_renderer.h/.cpp  sprite_renderer.h/.cpp  qr.h/.cpp  ceremony.cpp (ex hatch)
      networking/  net.h/.cpp (radio owner)  wifi_scanner.h/.cpp  transport.h  transport_loopback.cpp
                   transport_espnow.cpp  transport_ble.cpp (D2 fallback)  protocol.h/.cpp  session.h/.cpp
                   discovery.h/.cpp  battle_link.cpp  trade_link.cpp  breed_link.cpp  creator_server.h/.cpp
                   ble_social.h/.cpp (plumbing only, FEATURE_BLE)
      persistence/ save_schema.h  save_manager.h/.cpp  migration.h/.cpp  legacy_v1.h  kv_store.h
      hardware/    kv_nvs.cpp  boot.h/.cpp (RTC nonce, reset reason)  input.h/.cpp (Buttons + timer sampler)
                   gametime.h/.cpp (Clock)  motion.h  motion_null.cpp  audio.h  audio_null.cpp  power.h/.cpp
      data/        species_table.h  attacks_table.h  items_table.h  evolution_table.h  encounter_tables.h
                   balance.h  creator_schema.h  sprites.h (atlas format + UI icons)  sprites_pebbles.h  index_html.h
      dev/         godmode.h/.cpp -> diagnostics.h/.cpp (P10-C1)
```

The 16 existing modules map onto this tree without rewriting (audit §1.2 table); `render.cpp` and `sim.cpp` are deliberately **not** split (T2).

### 1.3 Layer rules

1. **Pure layers** — `src/core/{crc16,rng}.cpp`, `src/game/**`, `src/persistence/{save_manager,migration}.cpp`, `src/networking/{protocol,session,battle_link,trade_link,breed_link}.cpp`, `src/minigames/games/*_logic.cpp` — include only `<stdint.h>`, `<string.h>`, `core/config.h`, `core/nt_types.h`, `core/platform.h`. They never include `Arduino.h`. The host Makefile compiles them directly with `-Werror` and no Arduino include path (§68 r8, r10).
2. **Single owners** (already true today, audit exec summary item 4; preserved by every move): one radio owner `net.cpp`, one U8G2 owner `render.cpp`, one NVS owner `hardware/kv_nvs.cpp`, one libc-time owner `gametime.cpp`, one `esp_random()` caller `app.cpp` (seeds `rng_seed_all`).
3. **Rendering reads views, not models**: `petfx`/`actfx`/screens read `PetView` (pet_view.h), never `sim.h`/`genome.h` (audit risk 9: petfx.cpp:26-28, :580, :764-770, :894; actfx.cpp:588).
4. **State machine is the only navigation**: `ui_goto()`/`sm_goto()` is the sole funnel; `ScreenDef{enter,update,render,input,leave}` per §6 state; the three bypasses (ui.cpp:2460, :2729, :3463) are removed; grep gate in §1.1.
5. **Persisted or transmitted layouts are never edited in place**: bump `SAVE_SCHEMA_VERSION`/`PROTOCOL_VERSION`, add a migration or a reject path, add a test (§31, §60).
6. **Radio is screen-owned**: `net_request(RADIO_WIFI)` only from NETWORK (scan), LINK, CREATOR `enter()`; `net_request(RADIO_OFF)` in their `leave()` (§40, §68 r6). No `WiFi.begin()` anywhere except the creator softAP path (§68 r5).
7. **No blocking in `loop()`**: no `delay()` > 1 ms, no synchronous radio waits; long work (catch-up, scans, server) is phased and pumped. A single `loop()` iteration must stay < 1 s (Task WDT is 5 s, `CONFIG_ESP_TASK_WDT_TIMEOUT_S`, audit §2).
8. **Drawing goes through `gfx.h`/`rd_*`** so screens, minigames and the battle renderer render into the host framebuffer fake for §63 snapshot tests (P2-C11 onward).

### 1.4 Module contracts (header + key functions)

C-style free functions (existing `sim_*/store_*/rd_*` convention) so moves are mechanical and host tests need no C++ runtime features. Pure unless marked (device).

| Header | Key functions | Origin / notes | Commit |
|---|---|---|---|
| `core/crc16.h` | `uint16_t crc16_ccitt(const void* p, size_t n)` | the one CRC (today 3 copies: storage.cpp:140-156, `genome_crc16`, ble_social.cpp:171-183); check value `"123456789"` → `0x29B1` (genome.h:54) | P2-C2 |
| `core/rng.h` | `enum RngStream { RNG_CARE, RNG_ENCOUNTER, RNG_BATTLE, RNG_BREEDING, RNG_MINIGAME, RNG_LOOT, RNG_MISC, RNG_STREAM_COUNT }`; `void rng_seed(RngStream, uint32_t)`; `void rng_seed_all(uint32_t boot_seed)`; `uint32_t rng_u32(RngStream)`; `uint32_t rng_below(RngStream, uint32_t n)`; `bool rng_chance_permille(RngStream, uint16_t)`; `struct Rng{uint32_t s;}` + `rng_next(Rng&)`/`rng_next_below(Rng&, n)` for per-battle/per-minigame streams | xorshift32 from genome.cpp:28-45 (seed 0 remapped); `genome_rand()` → wrapper on `RNG_BREEDING`; sim's private `g_rng/rnd()` (sim.cpp:92, :154-160) → `RNG_CARE`; minigames stop using `genome_rand()` (ui.cpp:1515, :1517, :1573, :1630); `esp_random()` called exactly once in `app_setup()` | P2-C2 |
| `core/version.h` | `FW_VERSION`, `SAVE_SCHEMA_VERSION 2`, `CONTENT_VERSION`, `PROTOCOL_VERSION 1`, `CREATOR_API_VERSION 1` | §60 | P2-C9 |
| `persistence/kv_store.h` | `int kv_get(KvPart, const char* key, void* buf, size_t cap)` (bytes, 0 absent, <0 error); `bool kv_put(KvPart, key, buf, n)`; `bool kv_erase(KvPart, key)`; `bool kv_wipe(KvPart)`; `bool kv_healthy(KvPart)`; `enum KvPart { KV_MAIN /*"nvs"*/, KV_CKPT /*"nvs2"*/ }` | device impl `hardware/kv_nvs.cpp` = Preferences half of storage.cpp (`Preferences::begin(ns, ro, partition_label)`, Preferences.h:43); host impl `tests/fakes/kv_mem.cpp` with fault injection `kv_mem_fail_next_put()`, `kv_mem_corrupt(key, byte)`, `kv_mem_wipe_partition(KV_MAIN)` | P2-C9 |
| `persistence/save_schema.h` | structs of §1.5 + `static_assert` on every size/offset; key table (all ≤ 15 chars) | discipline of nt_types.h:472-479 | P2-C9 |
| `persistence/save_manager.h` | `LoadResult save_load_all(GameState&)` → `{LOAD_OK, LOAD_FRESH, LOAD_MIGRATED, LOAD_RECOVERED_PAIR, LOAD_RECOVERED_CKPT, LOAD_CORRUPT, LOAD_FOREIGN_NEWER}`; `bool save_pebble(uint8_t slot, const PebbleInstance&, bool force)`; `bool save_box_header(const BoxHeader&)`; `bool save_config(ConfigV2&)`; `bool save_inventory/cooldowns/trade_journal(...)`; `bool save_checkpoint_all(void)` (writes Box+active+header to `KV_CKPT`); `bool save_factory_reset(void)`; `void save_touch_lastseen(uint32_t)` | policy from storage.cpp:540-606, :715-778; tri-state replaces the indistinguishable `false` of storage.cpp:508-519 (audit risk 3) | P2-C9 |
| `persistence/migration.h` | `MigrateResult migrate_v1_to_v2(const uint8_t petsave128[128], const uint8_t cfg256[256], GameState& out)`; `bool migration_needed(uint8_t found)`; table-driven `migrate_run(from, GameState&)` | field map in P2-C9a; `legacy_v1.h` holds frozen `PetSave`/`Config`/`GainSave` v1 with their asserts | P2-C9 |
| `game/pebble.h` | `bool pebble_new(PebbleInstance&, uint8_t species, uint8_t level, uint32_t id, uint32_t seed, uint32_t now, Origin)`; `void pebble_derive_stats(const PebbleInstance&, DerivedStats&)` (maxHp/atk/def/spd from base + level + genome variation; never stored, §10); `uint32_t pebble_identity(const PebbleInstance&)` (`id ^ creation_seed`; feeds petfx, replaces `pf_identity` petfx.cpp:493-499); `void pebble_seal(PebbleInstance&)`; `bool pebble_is_empty(const PebbleInstance&)`; `const char* pebble_display_name(const PebbleInstance&, char*, size_t)` | new | P2-C9 |
| `game/species.h` | `const SpeciesDef* species_get(uint8_t id)` (built-in table or `cs*` record via the same pointer); `uint8_t species_count(void)`; `const AttackDef* attack_get(uint8_t)`; `const ItemDef* item_get(uint8_t)`; `const EvolutionRule* evolution_rule(uint8_t species)`; `const SpriteSet* species_sprite(uint8_t)` | reads `src/data/*` | P4-C1 |
| `game/box.h` | `bool box_add(Box&, const PebbleInstance&, uint8_t* slot_out)` (never overwrites); `bool box_release(Box&, uint8_t slot)` (explicit only; refuses the active slot); `bool box_set_active(Box&, uint8_t)`; `bool box_swap(Box&, uint8_t, uint8_t)`; `PebbleInstance* box_active(Box&)`; `void box_recover(PebbleInstance&, uint32_t elapsed_s)` (stateless passive recovery, §9/§27); invariants: 10 slots, unique non-zero ids, exactly one active, one location | new | P2-C10 |
| `game/sim.h` (Care, kept name) | keep `sim_tick`, `sim_apply_action`, `sim_take_events`, `sim_set_env`, `sim_action_cooldown_s`, `sim_gain_left/snapshot/restore`, `sim_apply_play_result`, `sim_stat_pct`, `sim_mood_score`, `sim_catch_up_ex`; change `sim_init(PetSave&)` → `void sim_bind(PebbleInstance&)`; add `void sim_switch(PebbleInstance& next)` (rebinds **without** resetting the device-wide anti-farm gain ledger: `reset_ram_state(fresh)` sim.cpp:1340 split in two); `SimEnv` → `CareEnv{now_epoch, day_of_year, local_hour, local_min, clock_state, reserved}` (weather fields deleted; `sizeof == 24` assert sim.h:48 kept) | single instance (§27 "Active Pebble only"); the 48 file-scope statics (sim.cpp:89-149) are wrapped in one `struct CareCtx g` mechanically (golden-preserving) but not made re-entrant | P2-C10 |
| `game/xp.h`, `game/evolution.h` | `uint16_t xp_for_level(uint8_t)`; `bool xp_add(PebbleInstance&, uint16_t amount, XpSource, uint8_t* levels_gained)` (saturating, cap 30); `uint16_t xp_daily_left(const XpLedger&, XpSource)`; `const EvolutionRule* evolution_check(const PebbleInstance&)`; `bool evolution_apply(PebbleInstance&, const EvolutionRule&)` | anti-farm caps generalised from sim.cpp:259-409 + `GainSave` (storage.cpp:607-714) | P3-C2/C3 |
| `minigames/minigame.h` | `struct Minigame { uint16_t name_str, hint_str; uint16_t duration_ms; void (*start)(MgCtx&, uint32_t seed); void (*update)(MgCtx&, uint16_t dt_ms); void (*render)(const MgCtx&); void (*input)(MgCtx&, MgButton, MgEdge); bool (*finished)(const MgCtx&); void (*finish)(MgCtx&); uint16_t (*score_permille)(const MgCtx&); }` (§28 six hooks incl. `finish()`); `struct MgCtx { Rng rng; uint32_t t_ms; uint16_t score; uint8_t phase; uint8_t mem[64]; }` (no heap); `manager.h`: `mg_start_sequence(uint8_t count, uint32_t seed)`, `mg_service(now)`, `mg_render()`, `mg_input(...)`, `bool mg_take_result(uint16_t& permille)` | `render` in `*_draw.cpp` via `gfx.h`; logic files host-compiled; 25 ms fixed step (ui.cpp:1731-1739) and intro/run/result phases kept | P3-C4 |
| `game/battle.h`, `battle_ai.h`, `validate.h` | `int8_t type_modifier(uint8_t att, uint8_t def)` (+1/0/-1, §12); `void battle_init(Battle&, const Team&, const Team&, uint32_t seed)`; `BattleErr battle_submit(Battle&, uint8_t side, const BattleAction&)`; `bool battle_resolve_round(Battle&, RoundLog&)` (§14 steps 1-9); `uint32_t battle_state_hash(const Battle&)` (FNV-1a over hp/buffs/actives/round — lockstep cross-check); `BattlePhase battle_phase(...)`; `uint8_t battle_winner(...)`; `BattleAction ai_choose(const Battle&, uint8_t side, Rng&)`; `ValidateErr validate_pebble(const PebbleInstance&, const CustomSpeciesRec*)`; `validate_team(const Team&)`; `validate_custom_species(const CustomSpeciesRec&, uint16_t* budget_used)` — one validator for load, peers and creator (§15, §35, §68 r11-12) | new | P4 |
| `networking/protocol.h`, `transport.h`, `session.h` | header `{u8 version, u8 type, u8 flags, u8 seq, u32 session, u16 len, u16 crc16}` = 12 B, payload ≤ 200 B (fits ESP-NOW 250 B and a BLE GATT write); `size_t proto_encode(const ProtoMsg&, uint8_t*, size_t)`; `ProtoErr proto_decode(const uint8_t*, size_t, ProtoMsg&)` (MALFORMED/VERSION/OVERSIZE/CRC/LEN); types = §15 list + `DISCOVERY_BEACON`, `TRADE_*`, `BREED_*`, `TIME_SYNC`, `PING`; `struct Transport { bool (*begin)(void); void (*end)(void); bool (*send)(const PeerAddr&, const uint8_t*, uint8_t); int (*recv)(PeerAddr&, uint8_t*, uint8_t cap); bool (*beacon)(const uint8_t*, uint8_t); void (*service)(void); uint8_t mtu; }`; `transport_loopback()` (host + on-device self-test; configurable drop/dup/reorder), `transport_espnow()`, `transport_ble()` (D2); `session_init(Session&, device_id, Role)`; `SessionEvt session_step(Session&, const ProtoMsg* in, ProtoMsg* out, uint32_t now_ms)` (HELLO..GOODBYE, 3 s per-step timeout x3 → `SESSION_LOST`, seq window rejects dup/out-of-order); `session_abort(Session&, reason)` | `TEAM_SUBMIT` = 3 x 64 B wire instance (subset of `PebbleInstance`: id, species, level, xp, hp, moves[4], genome, nickname, crc) | P4-C5 |
| `networking/wifi_scanner.h` + `net.h` additions | `NetPhase` (net.h:44-52) gains `NPH_SETTLING` (replaces blocking `delay(RADIO_SETTLE_MS)` net.cpp:142-144), `NPH_SCANNING`, `NPH_LINK`, `NPH_AP_CREATOR`; `bool net_scan_start(void)` (`net_request(RADIO_WIFI)` without `WiFi.begin()`; `WiFi.scanNetworks(async=true, show_hidden=true, passive=true, 300)`); `bool net_scan_poll(ScanResult*, uint8_t cap, uint8_t* n)`; `void net_scan_finish(void)` (`scanDelete()` → `net_request(RADIO_OFF)`); `ScanResult = {u32 net_hash, i8 rssi, u8 category}` — SSID/BSSID never leave the function (§44) | audit §7.2 | P5-C1 |
| `networking/creator_server.h` (ex webui) | keep `web_begin/web_service/web_stop/web_running`, `rate_take`, the 4-arg `send_P` and the bind-phase gate (all still in `creator_server.cpp`). **`pin_ok`, `arg_u32` and `parse_tok8` no longer exist**: P2-C5 deleted them because a callerless `static` trips `-Wunused-function` and the gate allows no suppression. Recover the implementations with `git show aa2b7ee:Pebblebol/webui.cpp` — `pin_ok` is rewritten here anyway (X-Pin header + `ConfigV2.creator_pin`), so treat the old body as a reference, not as code to restore verbatim; routes exactly `GET /`, `GET /api/state`, `GET /api/schema`, `POST /api/validate`, `POST /api/pebble`, `POST /api/time`, `POST /api/ping` (§38); body cap: register POST routes with the 4-arg `on(uri, HTTP_POST, fn, rawfn)` overload — `FunctionRequestHandler::canRaw()` (core `RequestHandlersImpl.h:97`) routes the body through `raw()` in `HTTP_RAW_BUFLEN` chunks (core `Parsing.cpp:182-193`) instead of `readBytesWithTimeout()` mallocing `Content-Length` bytes (`Parsing.cpp:44`); at `RAW_START` the handler checks `server.clientContentLength()` (WebServer.h:196) `> CS_BODY_MAX (2048)` → discard + 413; `uint32_t cs_idle_ms(void)`; `bool cs_take_upload(PebbleInstance&, CustomSpeciesRec&)`; `bool cs_take_time(uint32_t&)` | audit §12 "Body limits: none" | P8-C3 |
| `hardware/gametime.h` (Clock) | keep `gt_now/gt_is_valid/gt_local_tm/gt_format_elapsed/gt_skew_add`, millis-wrap extension (gametime.cpp:114-135); add `bool gt_set_epoch(uint32_t epoch, TimeCal src)` with rollback guard, `TimeCal gt_cal_state(void)` → `{CAL_UNSET, CAL_ESTIMATED, CAL_USER, CAL_PHONE}`; delete `gt_sync_start` and SNTP (gametime.cpp:224-249); monotonic source across deep sleep = RTC-backed `time()` (P6-C3) | audit §16 Clock HAL | P2-C6 |
| `hardware/input.h` (Buttons) | keep FSM (input.cpp:55-61, :131-216), debounce (:221-240), `input_begin/input_poll/input_raw/input_hold_ms/input_flush`; delete `GST_DBL_L/R` and `DOUBLE_TAP_WINDOW_MS` (TAP emitted on release); add `bool input_pressed_edge(uint8_t btn)` (replaces the `input_raw` diffing ui.cpp:1722-1728); **timer sampler**: a 5 ms `esp_timer` callback samples both GPIOs into a 16-deep SPSC ring of `{ms, levels}`; `input_poll()` drains the ring and feeds the FSM (host shims `nt_input_test_millis/level` input.cpp:45-46 unchanged) | T8; `INPUT_POLL_MS` (config.h:168, dead today) becomes real | P2-C6 (sampler), P3-C4 (double-tap) |
| `ui/screen.h` + `app/state_machine.h` | `struct ScreenDef { void (*enter)(void); void (*update)(uint32_t now_ms); void (*render)(void); void (*input)(Gesture); void (*leave)(void); uint8_t fps; uint8_t flags; /* SF_STICKY(no auto-return), SF_LOCK_INPUT, SF_OWNS_FRAME */ }`; `extern const ScreenDef SCREENS[SCR_COUNT]`; `sm_goto(ScreenId)`, `sm_push`, `sm_back`, `sm_home`, `sm_current`, `sm_service(now)`, `sm_handle(Gesture)`, `sm_draw()` | implementation = `ui_goto()` + `nav_*` (ui.cpp:617-650) with the three switches (ui.cpp:3329, :3419, :3510) replaced by table dispatch; stack depth 5 (ui.h:43); auto-return 20 s only when `!(flags & SF_STICKY)` (config.h:179) | P2-C11 |
| `ui/gfx.h` | `gfx_pixel/gfx_hline/gfx_vline/gfx_rect/gfx_fill/gfx_xbm(x,y,w,h,bits)/gfx_text(font,x,y,s)/gfx_text_w(font,s)/gfx_invert_rect/gfx_dither_rect` | device impl `gfx_u8g2.cpp` = the existing render.cpp primitives (render.cpp:554-910); host impl `tests/fakes/gfx_fb.cpp` = 128x64 1-bit framebuffer + font advance tables + out-of-bounds recorder; every migrated screen draws only through `gfx_*`/`rd_*` | P2-C11 |
| `ui/pet_view.h` | `struct PetView { uint32_t identity; const SpriteSet* body; uint8_t species_id, level, stage, pose, mood, hp_pct; uint8_t care_pct[5]; uint8_t asleep, sick, corrupted; uint16_t flags; char name[13]; }`; `void pet_view_fill(PetView&, const PebbleInstance&, const SpeciesDef&, uint8_t pose)` | breaks petfx→sim inversion; `petfx_*`/`actfx_begin` take `const PetView&`; `web_pose_of/web_mood_index` (ui.cpp:348, :920) move into `pet_view.cpp` | P2-C11 |
| `hardware/motion.h`, `audio.h`, `power.h` | `bool motion_supported(void)` → false; `bool audio_supported(void)` → false (no IMU, no buzzer: README.md:71, audit §6); `PowerState power_state(void)`; `void power_note_input(void)`; `void power_service(void)` (ACTIVE→DIM→IDLE→DEEP_SLEEP, §45) | capability detection per §4; no pin invented (§68 r3) | P6 |

### 1.5 Data formats

#### 1.5.1 `PebbleInstance` — 128 B, packed, little-endian, CRC-16/CCITT-FALSE over bytes 0..125

Same size as today's `PetSave` (nt_types.h:420-479) so the NVS arithmetic is known; 12 reserved bytes avoid a schema bump for small additions (§60).

```text
off size field                notes
  0   2  magic                0x4250 'PB'
  2   1  layout_ver           PEBBLE_LAYOUT_VER = 1 (SAVE_SCHEMA_VERSION lives in BoxHeader)
  3   1  species_id           0 = empty slot; 1..199 built-in roster; 200..209 custom (cs0..cs9)
  4   4  id                   0 = empty; hash32(device_id, next_id_counter); unique in the Box (§9 "no duplication")
  8   4  creation_seed        individual variation source (§10); nothing derived from it is stored
 12   4  birth_epoch          0 when the clock was CAL_UNSET at creation
 16   4  last_updated_epoch   §10 lastUpdated; drives box_recover() and care catch-up
 20   4  age_s                accumulated by care ticks, clock-jump proof (PetSave.age_s semantics)
 24   2  xp                   XP inside the current level (u16; XP_TABLE max < 65535, static_assert)
 26   1  level                1..30 (§11)
 27   1  evo_state            bits1:0 stage-in-family 0..2; bit7 EVOLVE_PENDING
 28  20  care[5]              int32 milli-points 0..100000: HUNGER, HAPPINESS, HEALTH, CLEANLINESS, ENERGY (T3)
 48  10  care_rem[5]          int16 exact remainders (|r| < 3600) — the existing integrator (sim.cpp:179-197)
 58   2  hp_cur               current battle HP; maxHp derived
 60   1  status               bit0 SICK bit1 ASLEEP bit2 CORRUPTED (§55) bit3 FAINTED bit4 LIGHT_ON
 61   1  flags                bit0 CUSTOM bit1 TRADED bit2 BRED bit3 GOD_TAINTED bit4 RARE bit5 HAS_CUSTOM_SPRITE
 62   4  moves[4]             AttackId, 0 = empty; validator requires exactly 4 legal (§13, §35)
 66   1  origin               STARTER / WILD / BRED / TRADED / CREATOR
 67   1  trait_id             0 = none (§56, optional)
 68   2  battles_won
 70   2  battles_lost
 72   2  minigames_won
 74   1  evolutions
 75   1  trades
 76   4  lifetime_active_s    seconds spent as the active Pebble (activity input, §25)
 80  16  genome               existing 16 B Genome (nt_types.h:309-330): lineage_id, generation, parent_tag, g0..g2
 96  13  nickname[13]         NAME_MAX_LEN 12 + NUL (config.h:644)
109   1  custom_sprite        0xFF none; else cs slot index
110   4  seq                  pair sequence number (T5); loader picks the highest valid seq of <key>0/<key>1
114  12  reserved[12]         must be 0
126   2  crc16                crc16_ccitt over bytes 0..125
```

`static_assert(sizeof(PebbleInstance) == 128)` + `offsetof` guards on `species_id`, `care`, `care_rem`, `moves`, `genome`, `nickname`, `seq`, `crc16`. Derived (never stored, §10): `hp_max = 10 + 2*base_hp + level`; `atk/def/spd = base + level/3 + genome variation (0..2)`; `type` = `SpeciesDef.type` (custom species carry their own type in `cs*`). All integer (no FPU, audit §2).

#### 1.5.2 Content tables (`src/data/*_table.h`, generated, `inline constexpr`, flash-resident)

```cpp
struct SpeciesDef {                 // 24 B, 60+ rows ≈ 1.5 KB flash
  uint8_t  id;                      // 1..199, contiguous == index+1 (static_assert)
  uint8_t  family;                  // 1..20+
  uint8_t  stage;                   // 0 base, 1 mid, 2 final (§19)
  uint8_t  type;                    // PebbleType: TYPE_SIGNAL / TYPE_CORRUPT / TYPE_SYSTEM (§12)
  uint8_t  base_hp, base_atk, base_def, base_spd;   // 1..10 (§11)
  uint8_t  moves[4];                // learnset (validator: moves must be in this list or the family pool)
  uint8_t  evo_rule;                // index into EVOLUTION_RULES[], 0xFF = none (§18)
  uint8_t  rarity;                  // 0 common, 1 uncommon, 2 rare, 3 special (§22)
  uint8_t  spawn_weight;            // relative weight inside its rarity band (§20)
  uint8_t  compat_group;            // breeding (§17)
  uint8_t  category_mask;           // NetCategory bits it can spawn under (§20)
  uint8_t  sprite_id;               // index into SPRITE_SETS (sprites_pebbles.h), 24x24 x 2 frames (T13)
  uint16_t name_idx;                // index into SPECIES_NAMES[] (proper nouns, App. A)
  uint16_t flavor_idx;              // StrId of the Spanish flavor line (§53)
  uint8_t  reserved[2];
};
static_assert(sizeof(SpeciesDef) == 24, "SpeciesDef layout drifted");

struct AttackDef {                  // 16 B, ~30 rows
  uint8_t  id, type, category;      // category: DAMAGE / DEFENSIVE / OFF_BUFF / SPEED / PROTECT / RISK (§13)
  uint8_t  power, accuracy;         // 0..100
  int8_t   priority;
  uint8_t  effect, effect_value, effect_duration, cooldown, anim_id, budget_cost;   // budget_cost feeds §36
  uint16_t name_idx;                // §13 "name"
  uint8_t  reserved[2];
};
static_assert(sizeof(AttackDef) == 16);

struct ItemDef  { uint8_t id, klass, value, rarity; uint16_t name_idx; uint8_t reserved[2]; };   // 8 B (§24: XP candy, capture, care, battle modifier)
struct EvolutionRule { uint8_t species, target, level, cond, cond_value; uint8_t reserved[3]; };  // 8 B (§18); cond: NONE, HAPPINESS_GE, ACTIVITY_GE, CORRUPTED, BATTLES_WON_GE, ITEM
struct EncounterRow  { uint8_t category, outcome, weight, rarity_min, rarity_max; uint8_t reserved[3]; }; // 8 B (§22)
inline constexpr int8_t TYPE_CHART[3][3] = { /* SIGNAL>CORRUPT>SYSTEM>SIGNAL */ };
```

Generator-emitted compile-time guards: id contiguity; every `moves[i] < ATTACK_COUNT`; every `EvolutionRule.target` is `stage+1` of the same family; every `sprite_id < SPRITE_SET_COUNT`; `sum(spawn_weight) > 0` per category; `XP_TABLE[LEVEL_MAX] < 65535`. `species_get()` also resolves `cs*` records so battle/validator code never branches on "custom".

#### 1.5.3 Save blobs (`SAVE_SCHEMA_VERSION 2`; the legacy Nottamagochi blobs are version 1)

All blobs: `magic u16, version u8, …, seq u32, …, crc16 u16` over `size-2`; `static_assert`ed; stored as **pairs** `<key>0`/`<key>1` unless marked single. NVS keys ≤ 15 chars. Namespace `"pbbl"` (D3).

| Key(s) | Struct | Bytes | Written | Notes |
|---|---|---|---|---|
| `pb00`/`pb01` … `pb90`/`pb91` | `PebbleInstance` | 128 | active slot: every 300 s unforced or forced after actions (1 s floor, deferred never dropped — storage.cpp:549-559); stored slots: on Box mutation only | one pair per slot; the active Pebble is an **index**, not a copy (§9) |
| `box0`/`box1` | `BoxHeader` | 32 | on Box mutation | `magic 'BX' u16, schema_version u8, active_slot u8 (0xFF none), slot_mask u16, content_version u16, next_id_counter u32, saved_epoch u32, seq u32, protocol_version u8 (§31), flags u8, captures u16, battles u16, reserved[4], crc16` |
| `cfg0`/`cfg1` | `ConfigV2` | 256 | settings change | `device_id u32` (§43, generated once), `device_name[13]`, `time_cal_state u8`, `time_cal_epoch u32`, `last_known_epoch u32` (§26), `creator_pin u16`, `pin_fail_count u8`, `pin_lock_until u32`, `creator_idle_s u16` (D7), `ap_pass[17]`, `brightness u8`, `flags u16` (mute, sh1106 override, statusbar mode), `tz[40]`, `seq u32`, `reserved[…]`, `crc16`. **No Wi-Fi credentials, no Telegram, no lat/lon** (§68 r5). |
| `inv0`/`inv1` | `Inventory` | 32 | item change | 12 x `{u8 item_id, u8 count}` + header + `xp_ledger[4] u8` + `ledger_epoch u32` (anti-farm, generalised `GainSave`) + crc (§24 "no enormous inventory") |
| `cd0`/`cd1` | `CooldownTable` | 272 | after each encounter roll | header 14 B (`magic, ver, n, seq, reserved[6]`) + 32 x `{u32 net_hash, u32 until_epoch}` + crc (§21) |
| `cs0`..`cs9` (single) | `CustomSpeciesRec` | 192 | creator upload | `magic, ver, slot, name[13], type, base[4], moves[4], compat_group, budget_used u16, sprite[2][72] (24x24 XBM), reserved[17], crc`; CRC failure → instance shows a placeholder sprite and `HAS_CUSTOM_SPRITE` is cleared, never destroyed |
| `tr` (single) | `PendingTrade` | 64 | only during a trade COMMIT | two-phase journal `{phase u8, out_id u32, in PebbleInstance-wire 48 B, peer_id u32, crc}` resolved at boot (§16) |
| `gl` (single) | `GainSave` | 20 | rides every forced save (storage.cpp:607-714 wear filter) | kept as-is (anti-farm ledger, storage.h:86-95) |
| `t`, `ok` (single) | u64, u32 | 8, 4 | 60 s / boot | kept as-is (storage.cpp:715-778, :330-356) |
| **`nvs2` partition** `ck_box`, `ck_pb0..9`, `ck_cfg` (single) | same structs | 32 + 10x128 + 256 | daily, on level-up/evolution/capture/trade, on factory reset (T5) | survives `initArduino()`'s wholesale erase of `nvs`; loader uses it when `nvs` has no `box*` and no `ok` (`LOAD_RECOVERED_CKPT`, shown to the user) |
| removed v1 keys | `save`, `cfg`, `anc`, `egg` in namespace `"notta"` | — | — | read once by the migration, then erased |

NVS budget (20 KB partition ≈ 504 usable 32 B entries, audit §5): 128 B blob = 6 entries, 256 B = 10, 32 B = 3, 272 B = 11, 192 B = 8, 64 B = 4. Live worst case: `pb` pairs 10x2x6 = 120 + `box` pair 6 + `cfg` pair 20 + `inv` pair 6 + `cd` pair 22 + `cs` 80 + `tr` 4 + `gl` 2 + `t`/`ok` 2 = **262 entries (52 %)**. Idle wear ≈ active slot 288/day x 6 + `t` 1,440 ≈ 3,200 entries/day (today 4,032, audit §5). `nvs2` (64 KB) holds ≈ 1,900 entries; the checkpoint set is 6x10 + 3 + 10 = 73. Live RAM for the whole game state: 10x128 + 32 + 256 + 32 + 272 + 64 = 1,936 B (custom species loaded one at a time).

Commit order (§31): construct → `validate_pebble` → serialise → CRC → `kv_put` the inactive copy → verify bytes written → the new copy is authoritative by `seq`. Multi-blob transactions (trade, capture into a full Box) write `tr` first. NVS itself commits one key atomically, so the pair protects against firmware bugs and bit rot, the `nvs2` checkpoint against the core's wholesale erase.

#### 1.5.4 Load pipeline (§48)

`read → checksum → schema validation (magic/version/size) → migration (v1 → v2 via legacy_v1.h) → runtime validation (validate_pebble on every slot; header/slot disagreement: slot magic is truth) → result`. `LOAD_CORRUPT` → `SCR_ERROR` "SAVE ERROR / A: Recover (checkpoint from nvs2) / B: Factory reset (two confirms)"; `LOAD_FOREIGN_NEWER` → `SCR_ERROR` "Save from newer firmware — A: Power off / B: Factory reset (two confirms)"; never auto-wipe (replaces sketch_aug30b.ino:279-283).

### 1.6 RNG service (§30)

- One xorshift32 core (genome.cpp:28-45, seed 0 remapped) behind `core/rng.h`; named streams `RNG_CARE, RNG_ENCOUNTER, RNG_BATTLE, RNG_BREEDING, RNG_MINIGAME, RNG_LOOT, RNG_MISC`, each seeded independently by `rng_seed_all(boot_seed)` (`boot_seed = esp_random()` once in `app_setup()`; tests pass constants).
- Per-session streams (`struct Rng`) for a battle (seed = `hash(idA, idB, nonceA, nonceB)` agreed in SESSION_ACCEPT, so both peers run identical resolution — lockstep) and for each minigame run (seed from the manager) so scripted-input tests are reproducible.
- Grep gate: no `esp_random()`/`random()`/`rand()` outside `app.cpp`; no `genome_rand()` outside `genome.cpp`.

### 1.7 Time model (§26)

- Source of truth: RTC-timer-backed `time()` (`CONFIG_ESP_TIME_FUNCS_USE_RTC_TIMER=y`, survives soft reset and deep sleep, drifts without a 32 kHz crystal; audit §2). Uptime (`esp_timer_get_time()`, gametime.cpp:129, :297-298) is used only for `millis()`-class intervals.
- Calibration state `TimeCal { CAL_UNSET, CAL_ESTIMATED, CAL_USER, CAL_PHONE }` persisted in `ConfigV2.time_cal_state` with `time_cal_epoch`; `last_known_epoch` persisted at the existing 60 s `t` cadence (storage.cpp:715-778).
- Setters: `POST /api/time` (CAL_PHONE) and the on-device time screen (CAL_USER). Rollback guard: a set that moves the clock backwards by more than 5 min is refused unless `src == CAL_USER` and confirmed; `epoch < NT_EPOCH_SANE_MIN` refused.
- Absence on boot = `now − last_seen` only when `gt_cal_state() != CAL_UNSET`, `BootKind ∉ {FIRST_RUN, CRASH, SOFT_RESET}`, `now > last_seen`, `now − last_seen ≤ ABSENCE_MAX_S` (400 d, config.h:418); otherwise **zero** (T9). `BOOT_DEEPSLEEP` (`ESP_RST_DEEPSLEEP`, folded into SOFT_RESET today at storage.cpp:207) charges elapsed time as normal absence.
- Every long-term system (care decay/recovery, box recovery, cooldowns, sleep window, growth, daily buckets) uses epoch deltas clamped to `[0, ABSENCE_MAX_S]`, never tick counts; all u32 accumulators saturate; `test_overflow.cpp` covers xp u16, `age_s`/`lifetime_active_s` u32, epoch rollback, cooldown `until` wrap, millis wrap.
- Catch-up budget: `sim_catch_up_ex` runs 1,800 s chunks up to 2,000 steps (sim.cpp:1899-1982) with ~100 soft 64-bit divisions per 60 s sub-step (audit §2). P2-C10 measures the worst case (400 d) on device via DIAG; if it exceeds 1 s it is made resumable and pumped from the LOAD_SAVE state ("Poniéndose al día…") so no `loop()` iteration approaches the 5 s Task WDT.

### 1.8 Reuse ledger (what happens to each existing module)

| Module (lines) | Verdict | Precise changes |
|---|---|---|
| `sketch_aug30b.ino` (545) | reuse → `app/app.cpp` | setup/loop/logic_tick moved verbatim (P2-C8); weather/telegram/SNTP/radio-policy/web-action drain deleted (P2-C3..C6); `boot_pet` → `LOAD_SAVE` state with tri-state (P2-C9); `.ino` = 3-line shim (removes ctags traps .ino:8-21, weak seam :91-99) |
| `sim.cpp/.h` (2,399) | adapt (Care) | `sim_bind/sim_switch` over `PebbleInstance`; delete death, ladder, sulk, scold, weight, storm, wish, adult forms, `sim_apply_minigame` (~900 lines); `ST_DISCIPLINE/ST_BOND` removed, ENERGY kept; rates in hours (P3-C1); keep integrator/60 s grid/ledger/catch-up/events/sleep/regen/god hooks |
| `genome.cpp/.h` (882) | reuse (Breeding + RNG source) | RNG lifted to `core/rng`; CRC to `core/crc16`; `genome_death_egg` (:522-624) deleted; `genome_breed` (:420-511) kept whole; `compat_group` gate added in `game/breeding` |
| `storage.cpp/.h` (1,221) | split | Preferences half → `hardware/kv_nvs.cpp`; RTC nonce + reset-reason (:63, :181-222, :357-454) → `hardware/boot.cpp`; policy (:540-606, :715-778) → `persistence/save_manager.cpp` with tri-state/pairs/journal/checkpoint; ancestor ring (:272-328), grade (:158-176) deleted |
| `gametime.cpp/.h` (491) | adapt (Clock) | −SNTP (:224-249); +`gt_set_epoch`/cal state/rollback; RTC `time()` as monotonic source (P6-C3) |
| `input.cpp/.h` (417) | adapt (Buttons) | +timer sampler ring (P2-C6); −double tap, +`input_pressed_edge` (P3-C4); FSM untouched |
| `render.cpp/.h` (1,507) | reuse | one TU in `src/ui/`; `rd_fx_settle_now` no longer called from the loop (P2-C4); `rd_fatal` replaced by ERROR state (P2-C11); `rd_power` finally used (P6-C3); `rd_frame_time_us` (dead export, audit §13.2) wired into DIAG (P10-C2) |
| `sprites.h` (1,426) | adapt (data) | format/`SpriteRef/SpriteSet`/emotes/UI icons kept; weather icons/particles deleted (P2-C3); 38 Nottamagochi body sets deleted when `sprites_pebbles.h` lands (P9-C3) |
| `petfx.cpp/.h` (1,785) | adapt (PetRenderer) | parameter `const PetView&`; identity from `pebble_identity`; sim reads → view fields; PIXEL CORE (:64-289) untouched; `#line` artefact (petfx.cpp:1) removed |
| `actfx.cpp/.h` (1,334) | adapt | parameter `const PetView&`; films re-keyed to FEED/CLEAN/REST/PET; SCOLD/MEDICINE/LIGHT films deleted; pixel writer/easing reused for capture/level-up films (P10-C3) |
| `ui.cpp/.h` (3,767) | adapt (strangler split) | nav core/widgets/modals kept in `ui_core.cpp`; one screen file per §6 state; LINEAGE/MEMORIAL/EGG/inline games deleted (~1,100 lines); table dispatch; hatch ceremony → `ceremony.cpp` (EVOLUTION) |
| `net.cpp/.h` (896) | adapt (radio owner) | +`NPH_SETTLING/NPH_SCANNING/NPH_LINK/NPH_AP_CREATOR`; −mDNS (:232-253), −STA credentials path, −TLS gate (:616-626), −portal→STA retry (:574-586); AP path (:305-348) kept for the creator |
| `ble_social.cpp/.h` (1,211) | adapt → plumbing behind `FEATURE_BLE` (D2) | keep ring/callbacks/profiles/hazards/peer table (:121-130, :216-311, :358-421, :646-703, ~450 lines); mating protocol/contagion/rules-in-transport deleted (P2-C7b); becomes `transport_ble.cpp` + `discovery` if D2 = BLE |
| `webui.cpp/.h` (1,384) | adapt → `creator_server` | keep lifecycle/PIN/rate/`send_P`/phase gate (~300 lines); routes replaced (P8-C3); raw-body cap; idle timeout |
| `index_html.h` (80 + 47 KB blob) | regenerate | container kept with a 1 KB placeholder (P2-C5) so webui.cpp:63 and `h_root` (webui.cpp:573-581) keep compiling; page rebuilt from committed source (P8-C4) |
| `qr.cpp/.h` (661) | reuse | payload without `?k=PIN` only (P8-C5) |
| `godmode.cpp/.h` (1,697) | adapt (dev console → diagnostics) | −MATAR/CLIMA/TELEGRAM/ENFERMAR/CACAS/mating rows (with each removal group); entry hold (:711-730), marker/taint (:639-671), soak CSV (:538-597), compile-out (:16, :1476-1494) kept; §49 fields + §66 commands (P10-C1) |
| `strings_es.h` (1,066) | reuse mechanism | −death/absence/weather/telegram/web-game blocks and the 39 dead StrIds; +BOX/BATTLE/NET/LINK/CREATOR/ERROR/TIME blocks; range asserts (:1019-1064) updated per commit |
| `config.h` (672) | adapt | D1 block + `PB_PINS_CONFIRMED` asserts; Spanish block :15-76 translated; features trimmed to `FEATURE_BLE/FEATURE_WEB/FEATURE_ESPNOW/GOD_MODE_ENABLED`; dead macros pruned (P2-C5); tunables move to `data/balance.h` |
| `nt_types.h` (653) | adapt | v1 wire structs → `persistence/legacy_v1.h`; `ScreenId/Gesture/StatId/ActionId/AlertId` re-cut; `RadioMode`, `BootKind`, `ActionResult` kept |
| `weather.*` (1,451), `telegram.*` (1,158) | delete | P2-C3, P2-C4 |

Net expectation: ≈ 9,000 existing lines deleted, ≈ 14,000 kept or moved, ≈ 8,000-10,000 new (game/, protocol/session/transport, creator page, tables, tests).

---

## 2. Removal surgery (audit §17), ordered, with the compile check after each group

Each group is one commit in the flat layout (T1) so the audit's line lists apply verbatim; the check after every group is **the gate (§1.1)** plus the named extras. Groups G1-G6 precede the `src/` move (P2-C8); G7-G8 follow it.

| # | Group | Surgery (audit §17 row; files:lines) | Coupled edits | Check | Commit |
|---|---|---|---|---|---|
| G1 | **Weather** | delete `weather.cpp/.h`; .ino:40, :52, :163-172, :190-192, :382-383, :522; ui.cpp:47, :141, :1219-1230, :1249-1268, :1283, :2238-2263, :2326, :3182, :3360; godmode.cpp:31, :71, :554, :596-598, :802, :831, :937-941, :1134-1138, :1194-1198, :1408-1411, :1464; webui.cpp:60, :154-159, :448-458, :490, :511, :831-849, :868-880, :917-932; sim.h:39-45 `SimEnv.wx_*`; sim.cpp:480-486, :493-495, :807-819, :823/:828/:838, :900-902, :997, :1308, :1521; config.h:48-53, :68, :257, :259, :515-526, :650; nt_types.h:119-136, :599-612, :537, :540, :276; strings_es.h:126-140/:615-629, :328, :370, :394, :433, :475; sprites.h:119-134, :1108-1144, :1206-1218, :1247-1257, :1415-1419 | `SimEnv` keeps `sizeof == 24` via `reserved[]` (sim.h:48); `WISH_SUN` removed so no unwinnable wish; `Config.lat/lon` @224/@236 → `reserved_b[24]` **in place** (offset asserts nt_types.h:565-566 hold); `ICO_SUN..ICO_STORM` and the `spr_icon12` assert (sprites.h:1002); `FEATURE_WEATHER` deleted | gate; golden unchanged; flash drops | P2-C3 |
| G2 | **Telegram** | delete `telegram.cpp/.h`; .ino:41, :52, :84-99, :113-117, :194-195, :384, :504-512, :521; ui.cpp:48, :180, :2085, :2238-2261, :2321-2324, :2453, :2542, :3156, :3228, :3266, :3272, :3359; godmode.cpp:32, :72, :170-171, :195-196, :662-666 (`nt_cfg_view()` — fix **before** deleting telegram.h), :688-689, :742-746, :803, :832, :943-953, :1207-1217, :1275-1297, :1465; godmode.h:62-66; webui.cpp:62, :932; net.cpp:27, :616-626; net.h:119, :158; config.h:39-46, :69, :509-512, :528-543, :647-648; nt_types.h:240-267, :551-552, :556; strings_es.h:71, :327, :371, :393, :397-404/:885-927, :431; storage.cpp:788-792, :811-816, :864-868 | `Config` `tg_token[48]@119`, `tg_chat[17]@167` → `reserved_a[65]`, `tg_mode@248` → `reserved_c` (layout 256 B / CRC 254 unchanged); god menu positional (`GD_MENU_STR` :122-128, `GOD_CMD_COUNT` config.h:663 → recount, assert godmode.h:200); remove `rd_fx_settle_now()` from the loop (.ino:513) — its only justification was the TLS stall; `FEATURE_TELEGRAM` deleted | gate; matrix: 9 telegram warnings gone; flash ≈ 1,981,714 B (audit §14 row) | P2-C4 |
| G3 | **Browser minigames, phone page body, `/api/cfg`, mDNS, dead macros/exports/StrIds** | index_html.h:26-73 → 1 KB placeholder page (container :1-25, :75-80 kept); webui.cpp:92-96, :103-109, :115, :186-224, :236-242, :326-347, :397-566, :591-779, :792-819 (`/api/sprites`), :821-968 (`/api/cfg`), :1056-1064, :1119-1129, :1135, :1153-1164; webui.h:136-189, :210-216; .ino:388, :530-543; ui.cpp:3291-3310; config.h:590-614 `MG_*`; nt_types.h:281-287 `MinigameId`, :226 `AL_WEB_CLIENT`; strings_es.h:351-353/:839-841; sim.cpp:411-416, :1754-1814 `sim_apply_minigame`; mDNS net.cpp:232-253, :612-614, :33, config.h:507, ui.cpp:2427-2428; dead macros config.h:84, :102, :114, :163, :168 (`INPUT_POLL_MS` — **kept**, becomes real in P2-C6), :191, :195-196, :231, :342, :354, :463, :565-567, :629, :637, :651 (`PIN_VBAT_ADC` config.h:97 **kept as reserved**; `GATE_*` :642-643 **kept**, enforced by the gate); 39 dead `StrId`s (audit §17 "Dead exports" row); dead exports of audit §13.2 except `rd_frame_time_us`, `rd_power`, `rd_set_contrast` (used by P6/P10) | `WEB_CD_*`/`WEB_ACTION_GLOBAL_CD_S` (config.h:582-587) **renamed `ACT_CD_*`**, not deleted — they are the device cooldowns (sim.cpp:320-343); fence the 3 ui.cpp functions that warn when `FEATURE_WEB=0`/`FEATURE_BLE=0` | gate; `build_matrix.sh` all-off = 0 warnings; flash ≈ 1.85 MB with BLE on | P2-C5 |
| G4 | **Always-on Wi-Fi policy + SNTP** | .ino:182-226 `wifi_wanted/radio_policy`, :55 `NT_WIFI_RETRY_MS`, :517-519; net.cpp:574-586 portal→STA retry; storage.cpp:801-803 `CF_WEB_ENABLED` default; gametime.cpp:224-249 `gt_sync_start`, :50-56, :62-63, :208-209, :240-245; config.h:460-467 `SNTP_*` | `gt_set_epoch()` lands **in the same commit** so `gt_is_valid()` can still become true (gametime.cpp:266-278); SCR_QR requests `RADIO_WIFI` on enter (ui.cpp:3111) — add release on leave; `ABS_UNKNOWN` retro-fix now triggers on `gt_set_epoch` (was SNTP landing, .ino:438-460) | gate; `test_clock.cpp` set-epoch/rollback cases; device boots `RADIO_OFF` and stays there on HOME | P2-C6 |
| G5 | **Death / memorial / lineage grade / absence ladder / sulk / scar / care_miss / discipline-scold / weight-obesity / overfeed→sickness / adult forms / storm / force-feed penalty / PUNKI** | sim.cpp:684-722 (`do_death`), :1141-1155, :1268-1272, :1285-1290, :529-537, :1451-1455, :1935-1943, :1983-2011, :2098-2104, :887-918, :1843-1897, :2019-2055, :1545-1548, :1672-1675, :794-805, :1681-1698, :858-877, :896-899, :973-976, :1607-1614, :219-242, :551-679 (`pick_adult_form`), :1088-1106, :1558-1566, :1578-1582, :2106-2149 (`sim_god_set_cq/sick/poop/kill`); ui.cpp:90-98, :177-180, :2443-2690 (MEMORIAL), :1897-2004 (LINEAGE), :3208-3212, :3225, :3326, :3352-3355, :3384-3389, :3474, :3481-3482, :3536; godmode.cpp:67, :82, :90-93, :365-372, :396-400, :444-498, :828, :865-878, :886-888, :1190-1193, :1389-1390, :1460; genome.cpp:522-624 `genome_death_egg`; storage.cpp:272-328 `store_push_ancestor`, :158-176; nt_types.h:44, :49-58, :60-70, :81/:86, :146-155 (keep `ABS_UNKNOWN` as a bool), :166 `ACT_SCOLD`, :196 `ST_DISCIPLINE`, :203-210, :400-402/:407/:410, :432/:446/:461; config.h:188-202, :245-246, :251-259, :272-276, :294, :302-304, :327-328, :332-359, :385-393, :395-411, :427-455; strings_es.h:142-150, :208-222, :271-282 | `PetSave` removed fields become `pad` bytes (128 B assert nt_types.h:472-479 holds until P2-C9); `ScreenId` renumbering shifts `s_cursor[SCR_COUNT]` (ui.cpp:119) and the webui comment coupling (ui.cpp:186-189); `ST_COUNT` change makes `GainSave.slots` refuse old ledgers (storage.cpp:634-637) — acceptable, schema is versioned in P2-C9; `health_step` gets a floor (HEALTH never below 10 %, decays only while a core stat is 0); hatch ceremony (ui.cpp:2692-2917) **kept** for EVOLUTION; `retune:` golden regenerated | gate; `test_care.cpp`: 30 d neglect never reaches HEALTH 0, no death flag; string range asserts (strings_es.h:1043-1064) updated | P2-C7 |
| G6 | **BLE mating protocol content** (frames, rules-in-transport, contagion, RAM cooldown) | ble_social.cpp:440-452, :498-553, :566-609, :731-789, :806-816, :899-941; ble_social.h:44-55, :68-83, :103-123; config.h:549-550, :560-567; ui.cpp:100-106, :219-225, :453-480, :2049-2230; strings_es.h:297-310; godmode old mating frames | keep radio handoff (ui.cpp:2010-2047), peer list rendering (:2184-2207), ring/callbacks/profiles/hazards/peer table (ble_social.cpp:121-130, :216-311, :358-421, :646-703); SOCIAL screen becomes the LINK placeholder in P2-C11 | gate; `FEATURE_BLE=0` and `=1` both warning-free | P2-C7b |
| G7 | **Build artefacts, stale refs, Spanish comment block** | petfx.cpp:1 `#line 1 "G:\\Mi unidad\\..."`; comments citing absent GAME_DESIGN/BRIEF documents (genome.h:67, input.h:3, godmode.h:3, ui.h:1-24, config.h section banners); config.h:15-76 Spanish user block → English | done during the move so the moved files are clean | gate; binary size identical to before the move | P2-C8 |
| G8 | **Docs** | README.md §5 (:288-355), §6 (:356-414), §9 (:636-664); CHANGELOG.md:46-72; ESTUDIO_VISUAL.md + docs/estudio_visual.html; `00-Investigacion-inicial.docx`; stale sizes README.md:136, ESTUDIO_VISUAL.md:66/:455; FQBN README.md:131-134 | English README/CHANGELOG under repo root; Spanish originals archived under `docs/legacy/`; bring-up procedure README.md:89-215 kept after D1 | `git status` clean; links valid | P1-C1 (drift fixes), P10-C5 (rewrite) |

---

## 3. Phases (exactly spec §52, 1..10)

Every commit lists tasks (files), acceptance (the gate is implied; extras named), size. Phase commit message = message of the phase exit commit; each `Pn-Cm` is its own commit titled `Pn-Cm: <summary>`; a commit that changes sim numbers is titled `retune: …` and regenerates the golden with the reason in the body.

### Phase 1 — Repository archaeology — size S — goal: audit + plan committed, repo hygiene

- [x] **P1-C0** Write `PEBBLEBOL_IMPLEMENTATION_AUDIT.md` (spec §69 items 1-20; 669 lines; build verified 2,105,548 B / 72,748 B / 0 warnings). Done 2026-09-02.
- [x] **P1-C0** Write `PEBBLEBOL_IMPLEMENTATION_PLAN.md` (this file: decisions, architecture, surgery list, §52 phases, §50 test map, §67 done, risks). Done 2026-09-02.
- [x] **P1-C1** `git add docs/PEBBLEBOL_PRODUCT_SYSTEM_SPEC.md PEBBLEBOL_IMPLEMENTATION_AUDIT.md PEBBLEBOL_IMPLEMENTATION_PLAN.md` (both untracked today, `git status`).
- [x] **P1-C1** Widen `.gitignore` from `*.bak` to `build/`, `tests/bin/`, `*.o`, `*.bin`, `*.elf`, `.arduino-cli/`, `.vscode/`.
- [x] **P1-C1** Create `docs/decisions.md` with D1-D7 (evidence copied from §0.2, default, "outcome" column empty; D1 marked "owner must inspect the physical board").
- [x] **P1-C1** Fix the doc drifts that mislead a builder now: FQBN README.md:131-134 (`nologo_esp32c3_super_mini`) → the verified `esp32:esp32:esp32c3:PartitionScheme=huge_app,CDCOnBoot=cdc`; stale sizes README.md:136, ESTUDIO_VISUAL.md:66/:455 → 2,105,548 / 72,748 with the date.
- [x] **P1-C1** `.github/workflows/ci.yml` running `make -C tests check` on push (no-op until P2-C2 adds tests; the firmware compile job is added in P2-C1 with a cached core). CI is not optional: §51 "keep the repository buildable" must be enforced by something other than a local script.
- [x] **P1-C1** Remove the stray tracked `00-Investigacion-inicial.docx` (6,536 B, pre-firmware note) or move it to `docs/legacy/`.
- Acceptance: `git status` clean; `arduino-cli compile` output unchanged (no source touched); CI green (empty test run).
- Commit message: `phase-1: repository archaeology — audit, plan, decisions log, CI skeleton, doc drift fixes`

### Phase 2 — Core engine — size L — goal: game state, save/load, clock, input, state machine, Pebble model, Box; buildable and host-tested with no cloud code

**P2-C0 First flash / hardware smoke (hardware track; blocked by D1; does not block any host commit)** — S
- [ ] Flash the current image (or the P2-C1 rename) with the D1-confirmed pins; confirm the I2C probe finds 0x3C (Serial log of render.cpp:326-345; bus sweep on failure), the panel renders the splash, both buttons read LOW when pressed (`input_raw`, `BTN_ACTIVE_LEVEL 0`, input.cpp:43), the LED blinks, the NVS canary passes (storage.cpp:330-356).
- [ ] Record on `docs/decisions.md` (D1 outcome section): real boot free heap vs the author's 179,836 B (net.cpp:5-7), real `sendBuffer()` frame time vs the ≈ 24 ms estimate (render.h:295-300, `FRAME_BUDGET_US 50000` config.h:155), USB-CDC port name.
- Acceptance: the device boots to the legacy HOME with the pin map in `config.h`; no invented pins (§68 r3).

**P2-C1 Build gate, sketch rename, CI compile job** — S
- [x] `tools/build.sh` (FQBN, `--warnings all`, parses "Sketch uses"/"Global variables use", fails on any project `warning:` line), `tools/check.sh` (build + `make -C tests check` + `GATE_FLASH_MAX`/`GATE_GLOBALS_MAX` from config.h:642-643 + grep gates as they land), `tools/build_matrix.sh` (variants of §1.1).
- [x] `git mv sketch_aug30b Pebblebol && git mv Pebblebol/sketch_aug30b.ino Pebblebol/Pebblebol.ino` (D3; folder == basename rule README.md:131-134).
- [x] `FW_NAME "Pebblebol"`, `FW_VERSION "0.2.0-dev"` (config.h:82-83); nothing else in config.h.
- [x] CI: add the `arduino-cli compile` job (core 3.1.1 + U8g2 2.35.30 cached) calling `tools/check.sh`.
- Acceptance: gate passes; flash/RAM within a few bytes of 2,105,548 / 72,748 (string change only).

**P2-C2 Host test tree over the modules as they are** — M
- [x] `tests/Makefile`, `tests/nt_test.h` (static registry, `TEST`, `CHECK`, `CHECK_EQ`, `CHECK_NEAR`, file:line on failure, non-zero exit; `main()` accepts `--seed N` and `--filter`), `tests/host_shims.cpp` providing `nt_input_test_millis/nt_input_test_level` (input.cpp:45-46) and `gt_host_millis32` (gametime.cpp:28).
- [x] Extract `core/crc16.{h,cpp}` from storage.cpp:140-156; make `store_crc16`, `genome_crc16` and the BLE copy (ble_social.cpp:171-183) call it. `test_crc16.cpp` (`"123456789"` → 0x29B1, genome.h:54).
- [x] Extract `core/rng.{h,cpp}` from genome.cpp:28-45 with the `RngStream` enum of §1.4; `genome_rand()` → `rng_u32(RNG_BREEDING)`; sim's `g_rng/rnd()` (sim.cpp:92, :154-160) → `RNG_CARE`; `sim_seed()`/`genome_set_rng()` kept as wrappers; `esp_random()` called once in the `.ino` (`rng_seed_all`), removing the calls at .ino:365-366, storage, webui, ble_social. `test_rng.cpp` (determinism per stream, independence, `rng_below` bounds, seed 0 remapped).
- [x] `test_sim_golden.cpp`: fixed seeds, fixed `SimEnv`, scripted 6 h (feed hourly, clean at 2 h, play at 3 h), hash of `stat[]` + `flags` every 60 s → `tests/golden/sim_v1.txt`. Record from the **pre-change** sim (build the test first, then switch CRC/RNG callers — hash must still match).
- [x] `test_genome.cpp` (genesis/seal/validate/breed determinism/hex32 round trip), `test_input.cpp` (waveforms through the FSM input.cpp:131-216: tap, hold-repeat, both, long-both, boot-held swallow :307-319, no phantom TAP before BOTH), `test_qr.cpp` (v1-v4 capacities 17/32/53/78 B, known vector), `test_clock.cpp` (`gt_format_elapsed` cases, millis-wrap with `GT_HOST_NEVER_VALID`).
- [x] `tests/legacy/mkfixtures.cpp`: a host program linking the legacy `PetSave`/`Config`/`GainSave` structs (nt_types.h) that writes `tests/fixtures/petsave_v1_adult.bin`, `petsave_v1_egg.bin`, `config_v1.bin`, `gainsave_v1.bin`; fixtures committed; `test_fixtures.cpp` asserts their CRCs.
- Files: `tests/*`, `Pebblebol/crc16.{h,cpp}`, `Pebblebol/rng.{h,cpp}` (flat for now), `storage.cpp`, `genome.cpp`, `sim.cpp`, `ble_social.cpp`, `Pebblebol.ino`.
- Acceptance: `make -C tests check` green (≥ 8 binaries); golden recorded and matching after the CRC/RNG switch; device build unchanged in behaviour.

**P2-C3 Remove weather (G1)** — M
- [x] Execute §2 G1 row by row; `SimEnv` keeps `sizeof == 24` with `reserved[]`; `Config.lat/lon` → `reserved_b[24]` in place; delete `FEATURE_WEATHER`, `WISH_SUN`, `CF_WX_ENABLED`, `CF_GEO_AUTO`, weather icons/particles.
- Acceptance: gate; golden unchanged; flash drops (part of the 123,834 B weather+telegram dividend, audit §14).

**P2-C4 Remove Telegram (G2)** — M
- [x] Execute §2 G2; fix godmode.cpp:662 (`nt_cfg_view`) first; `Config` reserved fields in place; `GOD_CMD_COUNT` recount; remove `rd_fx_settle_now()` from the loop (.ino:513); delete `FEATURE_TELEGRAM`.
- Acceptance: gate; matrix shows the 9 telegram warnings gone; flash ≈ 1,981,714 B.

**P2-C5 Remove browser minigames, page body, `/api/cfg`, mDNS; prune dead macros/StrIds/exports (G3)** — M
- [x] `index_html.h`: keep the container (:1-25, :75-80) with a 1 KB placeholder ("Pebblebol creator — Phase 8"); webui.cpp keeps `web_begin/web_service/web_stop/pin_ok/rate_take/send_P` and the `NPH_STA_UP||NPH_AP_PORTAL` bind gate (webui.cpp:1035-1098, :270-285, :352-364); `h_root` (webui.cpp:573-581) still serves `INDEX_HTML/INDEX_HTML_LEN`.
- [x] sim.cpp: delete `sim_apply_minigame` (:1754-1814) and `MG*` consumers (:411-416); rename `WEB_CD_*`/`WEB_ACTION_GLOBAL_CD_S` (config.h:582-587) → `ACT_CD_*`.
- [x] Delete `MinigameId`, `AL_WEB_CLIENT`, `MG1..3_*`, `/api/cfg`, `/api/sprites`, `web_take_action` drain, mDNS (net.cpp:232-253, :612-614; config.h:507; ui.cpp:2427-2428).
- [x] Prune dead macros (config.h:84, :102, :114, :163, :191, :195-196, :231, :342, :354, :463, :565-567, :629, :637, :651; **keep** `PIN_VBAT_ADC` :97 as reserved, `INPUT_POLL_MS` :168 and `GATE_*` :642-643 which become live), the 39 dead `StrId`s and the dead exports of audit §13.2 (keep `rd_frame_time_us`, `rd_power`, `rd_set_contrast`, `rd_breathe` for P6/P10).
- [x] ui.cpp: fence the 3 functions that warn under `FEATURE_WEB=0`/`FEATURE_BLE=0`.
- Acceptance: gate; `build_matrix.sh` all-off = 0 warnings; flash ≈ 1.85 MB with BLE on.

> **Known transient state entering P2-C6** (created by P2-C5, closed by Phase 8): with `/api/cfg` deleted and `index_html.h` a static placeholder, there is **no runtime Wi-Fi provisioning path**. Credentials reach `net_set_credentials()` only from the compile-time `CFG_WIFI_*` defaults through `g_cfg`. The captive-portal redirect still works, it just has no form to serve. This is expected, not a regression: the creator API restores provisioning in P8-C3.

**P2-C6 Radio OFF by default; clock calibration without SNTP; non-blocking settle; timer-driven buttons; overflow tests (G4)** — M
- [x] Execute §2 G4: delete `wifi_wanted/radio_policy` (.ino:182-226), `NT_WIFI_RETRY_MS`, portal→STA retry (net.cpp:574-586), `CF_WEB_ENABLED` default (storage.cpp:801-803). Radio requests become screen-owned: SCR_QR requests `RADIO_WIFI` on enter (ui.cpp:3111) — add the release in `screen_leave` (ui.cpp:3129).
- [x] `gametime`: add `gt_set_epoch(epoch, src)` + `gt_cal_state()` with the §1.7 rollback guard; `gt_is_valid()` (gametime.cpp:266-278) becomes true after a set; delete `gt_sync_start` and `SNTP_*` (config.h:460-467); `time()` stays the RTC-backed source.
- [x] Minimal on-device time entry screen (year/month/day/hour/minute; L = next field, R = increment, hold = confirm) reachable from SETTINGS. **New strings required**: P2-C5 pruned `STR_SET_CLOCK`, `STR_BOOT_CLOCK`, `STR_BOOT_NOCLOCK` and `STR_ERR_NO_TIME` as dead, so this screen adds its own block to `strings_es.h` (enum + `ES[]` together, range asserts updated). `STR_UI_NO_CLOCK` survived and is reusable. calls `gt_set_epoch(..., CAL_USER)` + `store_touch_lastseen`; also the §26 first-boot "ask for time during setup" step.
- [x] Absence on boot (.ino:304-333): **unknown clock charges zero** — `sim_catch_up_ex` no longer applies the `ABSENCE_LARGA_S` floor (sim.cpp:1920-1930; config.h:428 deleted); `ABS_UNKNOWN` retro-fix triggers on `gt_set_epoch` instead of SNTP landing (.ino:438-460).
- [x] Replace `settle()`'s `delay(RADIO_SETTLE_MS)` (net.cpp:142-144) by a timed `NPH_SETTLING` phase pumped by `net_service()`.
- [x] `input.cpp`: 5 ms `esp_timer` sampler → 16-deep SPSC ring `{ms, levels}` → `input_poll()` drains into the unchanged FSM; `INPUT_POLL_MS` (config.h:168) is now real; host shims unchanged. DIAG/god command `stall <ms>` (busy-waits in `loop()`) to prove presses during a 300 ms stall are still recognised.
- [x] `test_clock.cpp` += set-epoch, rollback refused (> 5 min backwards unless CAL_USER), forward jump accepted, `CAL_UNSET` → zero absence; new `test_overflow.cpp`: `now < last_seen` → 0, `now − last_seen > ABSENCE_MAX_S` → unknown, u32 saturating adds, millis wrap.
- Acceptance: gate; device boots `RADIO_OFF` and stays there on HOME; `test_clock`/`test_overflow`/`test_input` green.

**P2-C7 Remove death, memorial, lineage grade, absence ladder and the punishment mechanics (G5) — flat layout** — L
- [x] Execute §2 G5 in sim/ui/genome/storage/godmode/nt_types/config/strings; `health_step` floor (HEALTH ≥ 10 %, decays only while a core stat is 0 — §27 "inconveniently unhappy at worst"); keep the hatch ceremony (ui.cpp:2692-2917) for EVOLUTION; `PetSave` removed fields → `pad` (128 B assert holds).
- [x] `retune:` regenerate the golden (the ladder tail changed catch-up); body of the commit says why.
- [x] `test_care.cpp` (new): 30 days of neglect never reaches HEALTH 0 and never sets a death flag; string table guards (strings_es.h:1043-1064) updated in the same commit.
- Acceptance: gate; `test_care` green; `ScreenId` has no MEMORIAL/LINEAGE.

**P2-C7b Strip the BLE mating protocol content (G6) — flat layout** — M
- [x] Execute §2 G6; keep the radio handoff (ui.cpp:2010-2047), the peer list rendering (:2184-2207) and the ble_social plumbing (:121-130, :216-311, :358-421, :646-703); `genome_wire_ok` (:186-193) pattern noted for `proto_decode`.
- Acceptance: gate with `FEATURE_BLE=0` and `=1`; `grep -c MATE ble_social.cpp` == 0.

**P2-C8 Skeleton compile, then the mechanical move into `src/` (G7)** — S
- [x] **Skeleton compile first**: move only `config.h`/`nt_types.h`/`strings_es.h` to `src/core/` with relative includes plus one trivial `src/app/app.cpp`; confirm arduino-cli compiles `src/**` and resolves `"../core/nt_types.h"` before moving anything else.
- [x] `git mv` every remaining file into the §1.2 tree (names unchanged); rewrite includes to relative paths; `Pebblebol.ino` = 3 lines; `app.cpp` receives `setup/loop/logic_tick/boot_*` verbatim; `index_html.h` included from exactly one TU (`creator_server.cpp`, ex webui.cpp:63).
- [x] `src/core/config.h` §2: the five pins in one `// DECISION D1 PENDING` block, values verbatim, plus `#ifdef PB_PINS_CONFIRMED static_assert(PIN_SDA != PIN_LED …); static_assert(PIN_BTN_L != 2 && PIN_BTN_L != 8 && PIN_BTN_L != 9 && PIN_BTN_R != 2 && …) #endif`.
- [x] G7: translate the Spanish block config.h:15-76 to English; delete stale GAME_DESIGN/BRIEF references (genome.h:67, input.h:3, godmode.h:3, ui.h:1-24) and petfx.cpp:1 `#line`; `tests/Makefile -I../Pebblebol/src` updated, no test changes.
- Acceptance: gate; **globals byte-identical** to P2-C7b and flash within ~100 B, with any delta accounted for symbol by symbol. The original wording demanded a byte-identical flash figure, which this step's own mandated 3-line `.ino` shim makes unattainable: `setup(){app_setup();}` adds two trampolines, and moving code shifts addresses enough to change RISC-V compressed-branch relaxation on unrelated functions. Measured outcome: +68 B (+4 trampolines, ~+32 relaxation jitter over 35 functions, rest section padding), globals identical. What actually proves the move was pure is a normalised per-file diff against the previous commit plus an `nm` symbol diff, both of which P2-C8 ran; golden unchanged.

**P2-C9 SaveSchema v2, KvStore split, migration, tri-state load, pairs, `nvs2` checkpoint, SAVE ERROR (four sub-commits)** — L
- [x] **C9a (pure, host first)**: `persistence/save_schema.h` (§1.5 layouts + asserts), `core/version.h`, `persistence/save_manager.cpp` (pair write/read by `seq`, verify-after-write, §1.5.3 commit order), `persistence/migration.cpp` + `legacy_v1.h` (field map: `gene_species & 7` → legacy family map → starter species; `stage` → level EGG/BABY 1, CHILD 5, TEEN 10, ADULT 15, SENIOR 20; `stat[HUNGER/HAPPINESS/HEALTH/HYGIENE/ENERGY]` + `stat_rem[]` → `care[]/care_rem[]` unchanged (milli-points); `Genome` copied whole; `pet_name` → nickname (else the deterministic name ui.cpp:325-331); `birth/last_seen/age_s` kept; `Config.tz/brightness/mute` → `ConfigV2`; `gl` untouched), `persistence/kv_store.h`; `tests/fakes/kv_mem.cpp` (two partitions, fault injection); `test_persistence.cpp` (round trip every blob; CRC flip → other copy → `LOAD_RECOVERED_PAIR`; both copies bad → `LOAD_CORRUPT`; v1 fixtures → `LOAD_MIGRATED` with field map asserted; newer version → `LOAD_FOREIGN_NEWER`, nothing written; header/slot mismatch self-heal; `kv_put` failure mid-commit → previous copy still loads (`test_save_atomic` case); `KV_MAIN` wiped + `KV_CKPT` present → `LOAD_RECOVERED_CKPT`; key lengths ≤ 15).
- [x] **C9b (device)**: `hardware/kv_nvs.cpp` = the Preferences half of storage.cpp (open, put/get/erase/wipe, canary, sticky error bits; `Preferences::begin(ns, ro, "nvs2")` for `KV_CKPT`); `hardware/boot.cpp` = RTC nonce + reset-reason classification (storage.cpp:63, :181-222, :357-454) with `BOOT_DEEPSLEEP` split out of SOFT_RESET (:207); `storage.cpp` deleted; namespace `"pbbl"` (D3) with one-shot import of `"notta"`.
- [x] **C9c (UI/app)**: `BOOT → LOAD_SAVE` (§6) calls `save_load_all`; `LOAD_CORRUPT` → `SCR_ERROR` "SAVE ERROR / A: Recuperar / B: Reset de fábrica" (two confirms, ui.cpp:3001-3007 pattern); `LOAD_FOREIGN_NEWER` → `SCR_ERROR` without reset by default; `LOAD_RECOVERED_*` → toast; never auto-wipes (replaces .ino:279-283). `ConfigV2.device_id` generated once from `esp_random()` (§43); `time_cal_state` persisted; creator PIN fields zeroed until P8.
- [x] **C9d (partition)**: `Pebblebol/partitions.csv` per D6 (`nvs2` 64 KB); `tools/build.sh` drops `PartitionScheme=huge_app` (the sketch CSV wins, `platform.txt:113`); `save_checkpoint_all()` writes `ck_*` to `KV_CKPT` daily and on level-up/evolution/capture/trade; `docs/save_schema.md` written from the header (layouts, keys, partition table, entry budget 262/504).
- Acceptance: gate; `test_persistence.cpp` green incl. migration, corruption, pair recovery, checkpoint recovery, fault injection; device (when hardware exists): flash a v1 image, play, flash v2 → same pet in slot 0; erase `nvs` with `esptool` → boot shows "recovered from checkpoint".

**P2-C10 Care on `PebbleInstance`; `sim_switch`; Box module; first-boot starter; catch-up budget** — L
- [x] Step 1 (mechanical): wrap sim.cpp's 48 file-scope statics (sim.cpp:89-149) into `struct CareCtx` + one `static CareCtx g;` — golden must match.
- [x] Step 2: `sim_bind(PebbleInstance&)` replaces `sim_init(PetSave&)`; `PetSave` deleted from live code (kept in `legacy_v1.h`); field map `stat[]`→`care[]`, `stat_rem[]`→`care_rem[]`, `flags`→`status/flags`, `last_seen_epoch`→`last_updated_epoch`; golden re-derived through the map (same numbers, new field names) — no `retune:`.
- [x] `sim_switch(PebbleInstance& next)`: split `reset_ram_state(fresh)` (sim.cpp:1340) into per-Pebble accumulators (reset) and the device-wide gain ledger (kept), so changing the active slot cannot be used to farm.
- [x] `game/box.{h,cpp}` + `test_box.cpp` (capacity 10, unique ids, exactly one active, swap, release requires explicit flag and refuses the active, recover caps at 100 %, never touches xp/level); `box_recover()` initial rule = the regen rule (sim.cpp:920-935) generalised at a stored rate.
- [x] Boot: `box_recover(elapsed)` for every stored slot (computed from `last_updated_epoch`, written only on mutation), `sim_catch_up_ex` for the active (replaces `boot_absence`, .ino:304-333).
- [x] First-boot starter: `pebble_new(species 1, level 1, origin STARTER)` into slot 0 (replaces `sim_new_pet(genome_genesis())`, .ino:283); onboarding polish in P10-C4.
- [x] Catch-up budget **MEASURED on the host instead of via a god command** (equivalent for the decision it gates, and available with no board): `sim_catch_up_ex(400 days)` takes **8.4 ms** on the host at -O1. Assuming the plan's ~50x factor for a 160 MHz RISC-V that is **~421 ms**, against a 1 s threshold and a 5 s Task WDT. **Catch-up therefore does NOT need to be resumable**, and risk 25 is closed by measurement rather than estimate. An on-device `catchup <days>` command stays optional for bench confirmation.
- Acceptance: gate; `test_care.cpp` + `test_box.cpp` green; golden green; catch-up µs recorded in `docs/decisions.md`.

**P2-C11 Screen table (strangler), `gfx` seam, PetView, BOX screen, BOOT/LOAD_SAVE/ERROR states, §7 input grammar** — L (one screen per sub-commit)
- [x] `ui/screen.h` `ScreenDef` table + `app/state_machine.cpp` (`sm_goto/push/back/home` from ui.cpp:617-650); `ui_handle/ui_service/ui_draw` switches (ui.cpp:3329, :3419, :3510) delegate to `SCREENS[s]` for migrated screens and fall through to the old `switch` for the rest.
- [x] `ui/gfx.h` + `gfx_u8g2.cpp` (device, over render.cpp:554-910) + `tests/fakes/gfx_fb.cpp` (128x64 framebuffer, font advance tables for 5x8/6x10/4x6/t0_11b, out-of-bounds recorder); `tests/test_screens.cpp` renders each migrated screen with fixture states and (a) asserts zero out-of-bounds primitives, (b) compares against `tests/golden/screens/<screen>_<state>.pbm` (regenerated only by `snapshot:` commits) — the §63 "tested at the actual physical resolution" gate.
> **Trap for the first non-sticky screen (P2-C11b).** `draw_countdown()` is still a per-legacy-screen call inside `ui.cpp` and has **no `gfx.h` equivalent**. The three screens P2-C11a migrated are all `SF_STICKY`, so none needed it — but MENU, CARE, STATUS and SETTINGS are not sticky and would **silently lose navigation invariant 3's drain bar**. Add a `gfx_countdown()` seam, and mirror it in `tests/fakes/gfx_fb.cpp`, *before* migrating any of them.
> Also absent from `gfx.h` and needed by HOME and STATUS: the BIGNUM font, `gfx_bar()` and `gfx_header()`.
> And note `tests/fakes/gfx_fb.cpp` renders `GF_HEAD` (a proportional face on the device) at fixed advances, so goldens involving it are layout-approximate rather than glyph-exact.

- [ ] (P2-C11a did BOOT, LOAD_SAVE and ERROR; **P2-C11b did HOME, MENU, the CARE and PLAY lists, STATUS_A/STATUS_B and SETTINGS incl. the TIME entry screen**, and added the `gfx_countdown()/gfx_bar()/gfx_header()/gfx_list()` widgets, the `GF_BIG` font and the `ui/screen_view.h` PebbleView seam the trap above asked for; **P2-C11c did the CONFIRM/ALERT overlays as `ui/dialog.{h,cpp}` — including the audit §8.3 S11 fix — plus CREATOR (ex QR), LINK (ex SOCIAL), DIAG (ex GOD) and EVOLUTION (ex EGG) with the ceremony in `ui/ceremony.{h,cpp}`; `grep -c "case SCR_" ui/ui.cpp` is already 0 and GAME is the only screen left in ui.cpp, migrating with P3-C4**; only BOX is still open) Migrate in this order, one commit each: BOOT (splash; `rd_begin()` false → `SCR_ERROR` "PANTALLA / A: Reintentar" with LED blink — replaces `rd_fatal` noreturn, .ino:351-353, render.cpp:1034-1074; T10), LOAD_SAVE, ERROR (§47 layout "A: Retry / B: Exit"), HOME (§8: sprite + name/level/HP/hunger/happiness/XP), MENU (§8 order PEBBLE/CARE/PLAY/BOX/NETWORK/LINK/SETTINGS; `MENU_ITEM_COUNT 7` config.h:186, assert strings_es.h:1060), CARE lists (ex FEED/PLAY), STATUS, SETTINGS (+ time entry), CONFIRM/ALERT overlays (fix audit §8.3 S11 "first press also acts", ui.cpp:3068-3090 vs invariant 7 ui.cpp:17), QR → CREATOR placeholder (Wi-Fi request on enter/release on leave), SOCIAL → LINK placeholder, GOD/DIAG, EGG → EVOLUTION shell (hatch ceremony as `ceremony.cpp`), new **BOX** (list 10 slots via `draw_str_list`/`UI_LIST_ROWS` ui.h:37-39; inspect → STATUS; select active = `box_set_active` + `sim_switch`; swap; release behind double confirm; TRADE/BREED entries stubbed until P7-C2, §9).
- [ ] `ScreenId` renumbered to the §6 set (`BOOT, LOAD_SAVE, HOME, MENU, CARE, PLAY, GAME, BOX, STATUS, NETWORK, LINK, CREATOR, SETTINGS, TIME, CONFIRM, ALERT, ENCOUNTER, CAPTURE, BATTLE, TRADE, BREED, EVOLUTION, ITEM_REWARD, ERROR, SLEEP, DIAG`); `s_cursor[SCR_COUNT]` (ui.cpp:119) follows; the three `s_screen =` bypasses (ui.cpp:2460, :2729, :3463) removed; grep gate `s_screen\s*=` == 0 outside `state_machine.cpp`.
- [x] `ui/pet_view.{h,cpp}` (§1.4, with `identity` from `pebble_identity`); `petfx.cpp` stops including `sim.h`/`genome.h` (petfx.cpp:26-28; reads :580, :764-770, :894, :965); `actfx_begin(action, const PetView&)` (actfx.cpp:588); `web_pose_of/web_mood_index` (ui.cpp:348, :920) move into `pet_view.cpp`. **Done in P2-C11c**, with three recorded deviations: (a) `game/pebble.h` was never created (P2-C9 filed its contents into `game/box.cpp` + `persistence/save_schema.h`), so `pebble_identity` lives in `pet_view.cpp` as `pet_view_identity()` = `id ^ creation_seed`, with the old genome hash kept as the fallback for a pet not yet in the Box; (b) §1.4's `const SpriteSet* body` is replaced by the three atlas coordinates (`gene_species`, `stage`, `form`) because `pf_width_of()` needs FOUR sets at once and one resolved pointer cannot serve it; (c) the view also carries the decoded cosmetic genes, `mood_pct` and `care_pct[]` (CareId-indexed), which is what `sim_mood_score()`/`sim_stat_pct()`/`gene_*()` used to be read for inside petfx. `web_pose_of/web_mood_index` are now `pet_pose_of()/pet_mood_index()` and are gone from `webui.{h,cpp}`. New `tests/test_pet_view.cpp`.
- [ ] `app/input_router.cpp`: §7 grammar table — A = TAP_L (confirm/next), B = TAP_R (back/cancel), HOLD_L/HOLD_R secondary, BOTH help, LONG_BOTH home; `HOLD_R = back` (ui.cpp:3389-3395) retired; double-tap stays in the recogniser until P3-C4.
- [ ] `tests/test_statemachine.cpp`: table completeness (every `ScreenId` has all five hooks non-null), back-stack depth 5 (ui.h:43), auto-return only without `SF_STICKY`, ERROR reachable from every `LoadResult`, BOOT→ERROR on display failure.
- Acceptance: each sub-commit passes the gate; after the last one `grep -c "case SCR_" ui.cpp` == 0 for the three old switches; `test_statemachine` + `test_screens` green.

**P2-C12 Phase-2 exit** — S
- [ ] `tools/build_matrix.sh` warning-free in every variant; tag `v0.2.0-core`; `docs/decisions.md` records D3 outcome.
- [ ] Heap trend baseline: DIAG prints free heap and min-free-heap every 60 s; 1 h soak on the bench (when hardware exists) shows a flat line on HOME with radio OFF; numbers recorded in `docs/decisions.md`.
- Exit criterion (audit §20 Phase 2): compile 0 warnings; tests green; boots to HOME with a Box holding the starter; state persists across power-cycle; a corrupted `pb00` recovers from `pb01`; both corrupted show SAVE ERROR with two choices; radio OFF on HOME.
- Commit message: `phase-2: core engine — schema v2 with pairs + nvs2 checkpoint, host tests, screen table, radio off by default, clock calibration, no cloud code`

### Phase 3 — Virtual pet — size L — goal: care, XP, level, minigame framework, 5 minigames, evolution

**P3-C1 `retune:` care to §27 + stored recovery** — M
- [ ] `data/balance.h`: `CARE_DECAY_MPH[5]` (hours-scale: hunger −4,200/h ≈ 24 h full→empty, happiness −3,000, cleanliness −2,000, energy −6,000 awake / +20,000 asleep, health −2,000 only while a core stat is 0 for ≥ 2 h, floor 10 %), `BOX_RECOVER_MPH +4,200` toward 100, action gains, `ACT_CD_*`, anti-farm caps (`GAIN_CAP_*` kept; refusals are friendly toasts, never penalties).
- [ ] Remove `STAGE_MULT_*` age-based decay multipliers (config.h:262-268) — decay depends on species/traits only.
- [ ] `test_care.cpp` += "ignored 8 h stays > 60 % happiness", "24 h neglect never below floor", "stored 7 days → full", catch-up chunk equivalence (3600 s in one call == 60 x 60 s), sleep window halves decay; regenerate golden (`retune:`).
- Acceptance: gate; §67 "Care works", "Stored Pebbles recover".

**P3-C2 XP and levels** — M
- [ ] `game/xp.{h,cpp}`; `XP_TABLE[31]` in `balance.h` (`static_assert` sum < 65535); sources: care action +2 (hourly cap), minigame `permille*8/1000`, carried time +1/10 min awake (daily cap 48), later battle +25 / capture +10 / items; ledger snapshot/restore semantics from `GainSave`; `SIM_EV_LEVEL_UP` event → toast + `rd_flash`; HOME shows level + XP bar (§8); on level-up `hp_cur` rescales with `hp_max`.
- [ ] `test_xp.cpp` (curve monotonic, multi-level carry-over, cap 30, caps, ledger under-reports never over-reports).
- Acceptance: gate; §67 "XP and leveling work".

**P3-C3 Evolution** — M
- [ ] `game/evolution.{h,cpp}` on `EvolutionRule` (`data/evolution_table.h`, §18 optional conditions); `EVOLVE_PENDING` set by `xp_add`, consumed by the `EVOLUTION` state reusing the hatch ceremony (`ceremony.cpp`) with `rd_flash/rd_shake/rd_dither_rect` dissolve; A confirms (§18); `evolution_apply` committed **before** the first frame (ui.cpp:2700-2708 ordering); `save_checkpoint_all()` after.
- [ ] `test_evolution.cpp` (rule lookup, condition flags, apply recomputes stats, final stage never evolves, invalid target rejected by the validator).
- Acceptance: gate; §67 "Evolution works".

**P3-C4 Minigame framework + input latency fix** — L
- [ ] `minigames/minigame.h` (§1.4, six hooks incl. `finish()`), `manager.{h,cpp}` (pool via `RNG_MINIGAME`, sequence play of N=3 with a 1.2 s "¿SIGUIENTE?" card, A = next, B = stop; `finish()` called on completion or abort; cooldown from the care ledger), `MgCtx` fixed-size (no heap).
- [ ] Port REFLEJOS → `ping` (Signal Lock) and MEMORIA → `sequence` (shortened to 3-5 steps) into `games/*_logic.cpp` + `*_draw.cpp` (from ui.cpp:1514-1626); delete SALTO (:1628-1690) and `GameState` (ui.cpp:227-247).
- [ ] New logic files for the §29 set: `packet_flood`, `firewall`, `buffer`, `delete`; each 5-15 s, fixed 25 ms step (ui.cpp:1731-1739 pattern), per-run `Rng`, drawing only through `gfx_*`.
- [ ] Input: delete `GST_DBL_L/R` and `DOUBLE_TAP_WINDOW_MS` so a TAP is emitted at release (+25 ms debounce); add `input_pressed_edge(btn)` (replaces ui.cpp:1722-1728); HOME `DBL_R` pet → `TAP_R`; `test_input.cpp` asserts TAP ≤ 30 ms after release.
- [ ] `test_minigames.cpp` (each game: same seed + scripted input tape → identical score; finishes ≤ 15,000 ms; score ∈ 0..1000; manager sums correctly; `finish()` called exactly once); `test_screens` snapshots for each game's intro/run/result.
- Acceptance: gate; §67 "At least 5 minigames", "playable with two buttons", "transition cleanly" (6 games: ping, sequence, packet_flood, firewall, buffer, delete).

**P3-C5 Phase-3 exit** — S
- [ ] Matrix + tag `v0.3.0-pet`; `test_sim_golden` renamed `test_care_golden`; bench: god-mode x3600 soak (godmode.cpp:538-597 CSV) shows no stat pinned at 0 for > 6 simulated hours of neglect.
- Commit message: `phase-3: virtual pet — hours-scale care, XP/levels, data-driven evolution, minigame framework with 6 two-button games, 25 ms tap latency`

### Phase 4 — Battle engine — size L — goal: types, attacks, battle state machine, 3-Pebble teams, switching, local battle protocol (host-proven)

**P4-C1 Content pipeline v0 + minimal roster** — M
- [ ] `tools/gen_content.py` (JSON → `src/data/{species,attacks,items,evolution,encounter}_table.h` + `src/data/creator_schema.h` + the §1.5.2 compile-time guards + `CONTENT_VERSION` hash); `tools/content/*.json` with 12 species (4 families x 3), ~12 attacks covering every §13 category, 4 items, `TYPE_CHART`; `data/balance.h` battle constants (§1.5.1 formulas; damage `max(1, power*atk/(def*K)) + 2*type_mod + rng(0..2)`, protection halves, buffs ±1 stage for N rounds).
- [ ] `game/species.cpp` accessors; `game/pebble.cpp` `pebble_derive_stats()`; `test_stats.cpp` (integer, monotonic in level, bounds); `test_content.cpp` (guards as runtime tests).
- Acceptance: gate; generator output committed; tables compile with all guards.

**P4-C2 Battle engine (pure)** — L
- [ ] `game/battle.{h,cpp}`: teams of ≤ 3, one active each; round = collect both actions → validate → priority → effective speed (tie by the shared `Rng`) → resolve first/second → faint processing (forced switch) → status ticks → end check (§14 steps 1-9); switching consumes the turn; buffs/protection with durations; `RoundLog` ring; `battle_state_hash()`.
- [ ] `test_battle.cpp` — every §50 battle case (type advantage, damage, speed order, switching, fainting, buffs, protection, victory, invalid moves incl. unknown move / empty slot / acting for fainted / out of turn → rejected, state untouched) + `test_battle_replay.cpp` (two engines, same seed + actions → identical hash every round; replaying a log yields the same final state).
- Acceptance: gate.

**P4-C3 Battle AI** — S
- [ ] `game/battle_ai.cpp`: greedy expected damage with type mod; switch when HP < 25 % and a better type is benched; never invalid; own `Rng` stream. `test_battle_ai.cpp`.

**P4-C4 Battle on device vs AI + DIAG `test_battle`** — M
- [ ] `ui/battle_renderer.cpp` on `rd_bar/rd_flash/rd_shake/rd_dither_rect` + `gfx_xbm` (24x24 sprites, PIXEL CORE mirror petfx.cpp:64-289); `BATTLE` state; team pick from BOX (3 slots); 4-attack + SWITCH menu on the list widget; 20 fps via `rd_hold_fps`; no per-frame heap (`ESP.getFreeHeap()` delta 0 per frame in DIAG).
- [ ] MENU → PLAY → "COMBATE DE PRÁCTICA" and DIAG entry `test_battle` (§49) — the only single-device battle entry in V1 (no wild battles, §68 r18); XP on win via `xp_add`.
- [ ] `test_screens` snapshots: battle intro, menu, hit, faint, result.
- Acceptance: gate; §67 "3-Pebble team", "1v1 active combat", "Four attacks", "Three-type effectiveness", "Buffs/protection", "Switching costs a turn".

**P4-C5 Local protocol codec, session FSM, lockstep battle protocol, shared validator, fault-injecting loopback (no radio)** — L
- [ ] `networking/protocol.{h,cpp}` (§1.4 header, §15 message set incl. `TEAM_SUBMIT` 3 x 64 B wire instance), `networking/session.{h,cpp}` (HELLO → CAPABILITIES → SESSION_REQUEST/ACCEPT with shared seed → op → GOODBYE; 3 s x 3 timeouts → `SESSION_LOST`; seq window), `networking/battle_link.cpp` (lockstep: both send ACTION, both resolve locally with the shared seed, exchange `ROUND_RESULT{round, hash}`; mismatch → `BATTLE_END(DESYNC)`, no rewards; `TEAM_SUBMIT` → `validate_team` on both sides, `TEAM_VALIDATION` carries the verdict), `game/validate.{h,cpp}` (§15 reject list; reused by load and creator), `networking/transport.h` + `transport_loopback.cpp` (two in-process endpoints; configurable drop %, duplicate %, reorder window).
- [ ] `test_protocol.cpp` (codec round trip every type; truncate/flip → `PROTO_E_*`; version reject; oversize; TEAM_SUBMIT with level 31 / unknown species / 3 moves / hp > max / bad genome CRC → rejected), `test_session.cpp` (full battle between two endpoints; disconnect at each state → `SESSION_LOST`, both Boxes untouched; duplicate seq dropped; gap detected; **10 % drop/dup/reorder → battle still completes or aborts cleanly, never desyncs silently**), `test_validate.cpp` (Appendix C vectors).
- [ ] `docs/protocol.md`.
- Acceptance: gate; §67 "Invalid peers cannot inject illegal data" proven on host; loopback fault run green.

**P4-C6 Phase-4 exit** — S
- [ ] Matrix + tag `v0.4.0-battle`; D2 decision formally requested from the owner with the §0.2 evidence (needed by P7-C1).
- Commit message: `phase-4: battle engine — deterministic 3v3 engine with replay hash, AI, device practice battle, §15 codec + lockstep session proven over a faulty loopback`

### Phase 5 — Exploration — size M — goal: Wi-Fi scanner (scan only), abstract network identity, cooldown, encounter generation, capture, items

**P5-C1 Scan-only Wi-Fi** — M
- [ ] `net.cpp`: `NPH_SCANNING`; `net_scan_start()` = `net_request(RADIO_WIFI)` without `WiFi.begin()` (scan forces `enableSTA` only, audit §7.2), `WiFi.scanNetworks(async=true, show_hidden=true, passive=true, 300 ms/ch)`; `net_scan_poll()` hashes `BSSID‖SSID‖device_id` with FNV-1a into `net_hash` and classifies `category` (OPEN/HIDDEN/HOME/BUSINESS/PUBLIC/UNKNOWN from `encryptionType()`, hidden flag, RSSI bucket, SSID token hash lists in `balance.h`, §20); `scanDelete()`; `net_request(RADIO_OFF)`; never `WiFi.begin()` (§68 r5) — grep gate.
- [ ] `networking/wifi_scanner.{h,cpp}` job wrapper with a 12 s software timeout (§47) and cancel (B).
- [ ] `test_exploration_hash.cpp` (hash stable for same input, differs by device salt; no SSID field exists in `ScanResult` — checked by `sizeof`/field list).
- Acceptance: gate; §67 "Wi-Fi scanning works", "Device does not connect to scanned networks", "Wi-Fi shuts down after use" (radio reports `RADIO_OFF` ≤ 5 s after results).

**P5-C2 Cooldowns (persisted, epoch-based; RAM fallback when uncalibrated)** — S
- [ ] `game/cooldowns.{h,cpp}` on `CooldownTable` (`cd` pair): `cd_ready(hash, now)`, `cd_arm(hash, now + 2 h)`, LRU eviction of 32; when `gt_cal_state() == CAL_UNSET` a per-boot RAM table is used instead so an uncalibrated device cannot farm (T9); a clock rollback never shortens a cooldown.
- [ ] `test_cooldowns.cpp` (reboot survival via `kv_mem`; expiry by epoch; eviction order; rollback; uncalibrated path).

**P5-C3 Encounters** — M
- [ ] `game/encounters.{h,cpp}`: `encounter_roll(ScanResult, day_bucket(6 h), progress, RNG_ENCOUNTER) → {WILD, ITEM, SPECIAL, NOTHING}` from `encounter_tables.h` weights (§22; NOTHING ≥ 15 %); wild species by rarity ∩ `category_mask`; wild level = clamp(active ± 2, 1..30); SPECIAL outcomes: XP burst and **corruption event** (sets `CORRUPTED` status on the active with a 24 h timer — hook for P9-C5); deterministic under seed (§20).
- [ ] `NETWORK` screen: "ESCANEANDO…" spinner (cancel with B) → result → `ENCOUNTER` transient ("¡PEBBLE SALVAJE! [CAPTURAR] [DEJAR]", §23); nothing found → toast. `test_encounters.cpp` (same seed → same outcome; weights over 10k rolls within tolerance; NOTHING occurs; category mask honoured; level clamp).
- Acceptance: gate; §67 "Encounters work".

**P5-C4 Capture + inventory + items** — M
- [ ] `game/capture.{h,cpp}`: `capture_chance_permille(wild, item)` (rarity base − level gap + item bonus); one roll per capture item or one free attempt; flee after 2 failures; success → `pebble_new(origin WILD)` → `box_add`; full Box → BOX screen in "elige un hueco o cancela" mode — never silent, active never removed (§23, §68 r14).
- [ ] `game/inventory.{h,cpp}` on `Inventory` (`inv` pair), `data/items_table.h` (§24 classes: XP candy, capture chip, care ration, battle modifier); `ITEM_REWARD` transient; items usable from CARE/BOX menus.
- [ ] `test_capture.cpp` (probability bounds and monotonicity; determinism; never deletes the active), `test_inventory.cpp` (add/use/underflow), `test_box_full_capture.cpp` (both branches leave 10 valid Pebbles).
- Acceptance: gate; §67 "Cooldowns persist", "Capture works", "Items work".

**P5-C5 Phase-5 exit** — S
- [ ] Matrix + tag `v0.5.0-explore`; bench: NETWORK → scan → encounter within ≈ 5 s, radio OFF afterwards, cooldown survives a power-cycle.
- Commit message: `phase-5: exploration — scan-only Wi-Fi with on-device hashing, persisted epoch cooldowns, seeded encounters, capture, inventory and items`

### Phase 6 — Activity — size M — goal: sensor abstraction, activity score, growth rewards, deep-sleep integration

**P6-C1 Sensor abstraction** — S
- [ ] `hardware/audio.{h,cpp}` is now a REAL implementation, not a stub: the V1 hardware baseline adds a passive piezo (decision D8). Tone engine over `tone()`/`noTone()` or LEDC, non-blocking event queue, GPIO idle between effects, `noTone()` on every effect end, honours `CF_MUTE`. Vocabulary per hardware spec §19 (beep, chirp, buzz, rise, fall, double-beep, glitch) with the §18 durations.
- [ ] `hardware/motion.h` (`bool motion_supported(void)`, `bool motion_poll(MotionSample&)`), `motion_null.cpp` returns unsupported (audit §6: no IMU; §4 capability detection; §68 r3 no pin invented).

**P6-C2 Activity score + growth rewards** — M
- [ ] `game/activity.{h,cpp}`: daily score = carried minutes awake x1 + interactions x2 + distinct new `net_hash` x10 + peers met x15, each capped (§25, §57); feeds `xp_add(XP_ACTIVITY)`, a happiness bonus and a rare-encounter permille bonus for the next roll; day bucket persisted in `inv` ledger fields; zero when `CAL_UNSET`.
- [ ] `test_activity.cpp` (caps, day rollover, reward monotonicity, uncalibrated → 0).

**P6-C3 Power states + sleep-correct time** — L (deep sleep depends on D1)
- [ ] `hardware/power.{h,cpp}` + `SLEEP` state: ACTIVE → DIM (contrast 40 via `rd_contrast_ramp`, 30 s idle) → IDLE (`rd_power(false)` render.cpp:399-411, FPS 1, radio OFF, 2 min) → DEEP_SLEEP (10 min idle, no radio job; `esp_deep_sleep_enable_gpio_wakeup` on both buttons **only if D1 gives buttons on GPIO0-5**, otherwise light sleep with `gpio_wakeup_enable`); forced save + `save_touch_lastseen` before any sleep; Wi-Fi/BLE forced OFF first (§45).
- [ ] `gametime`: monotonic source across sleep = RTC-backed `time()` (sdkconfig `CONFIG_ESP_TIME_FUNCS_USE_RTC_TIMER=y`) instead of `esp_timer_get_time()` uptime (gametime.cpp:129, :297-298); `BOOT_DEEPSLEEP` charges elapsed time as normal absence (never as a crash); care/box/cooldown catch-up runs on wake exactly like boot.
- [ ] `app.cpp`: the busy loop yields (`delay(1)` when no frame is due and no job is pending; light-sleep tick in IDLE) — audit risk 16.
- [ ] `test_clock.cpp` += elapsed across a simulated sleep gap; `test_care.cpp` += recovery accrues across sleep; DIAG shows loop iterations/s per power state.
- Acceptance: gate; §67 "Device sleeps correctly", "Timed systems work after sleep", "No obvious battery-draining loops" (bench: a 30 min deep sleep charges 30 min of care and box recovery; loop rate in IDLE ≤ 10/s).

**P6-C4 Phase-6 exit** — S
- [ ] Matrix + tag `v0.6.0-activity`.
- Commit message: `phase-6: activity — honest activity score (time, interactions, network diversity, peers), motion/audio capability stubs, power states with sleep-correct time`

### Phase 7 — Social — size L — goal: peer discovery, trade, breeding, transaction safety

**P7-C1 Device transport per D2** — L
- [ ] Primary: `transport_espnow.cpp` (`WIFI_STA` residency via `net_request` minus `WiFi.begin()`, fixed `PB_LINK_CHANNEL`, broadcast beacon `{PROTOCOL_VERSION, device_id, name[12], caps, crc16}` every 500 ms while in LINK, unicast + send-callback ACK after HELLO, `esp_now_register_recv_cb` → SPSC ring modelled on ble_social.cpp:121-130; never touch UI from the callback); `NetPhase` gains `NPH_LINK`.
- [ ] Fallback (if D2 = BLE): `transport_ble.cpp` = discovery beacon v2 + INVITE frame on the retained ble_social plumbing (ble_social.cpp:358-421), session over ESP-NOW on the INVITE channel or GATT; `BLE_SESSION_CAP` respected; **measure** the claimed ~672 B/cycle Bluedroid leak (config.h:559, net.cpp:196-204) over 32 init/deinit cycles via DIAG and record it in `docs/decisions.md`.
- [ ] Discovery beacon carries only `device_id`, `device_name`, `protocol_version`, capabilities (§43, §44); `discovery.cpp` peer table (8, RSSI EMA, hit ≥ 3, TTL 60 s — ble_social.cpp:216-311 semantics); `test_discovery.cpp` (beacon codec, TTL, RSSI gate).
- Acceptance: gate; loopback tests unchanged (transport-agnostic, §59); bench: two boards see each other's beacon in LINK.

**P7-C2 LINK screen, consent, BOX → LINK entry points** — M
- [ ] Fold or drop `STR_SO_LINK_SOON` ("LINK - Fase 7"), the P2-C7b placeholder: it shows an internal plan phase number to the player.
- [ ] `LINK` state: peer list → "¡PEBBLEBOL ENCONTRADO! [COMBATE][INTERCAMBIO][CRIAR][CANCELAR]" (§42) → `SESSION_REQUEST/ACCEPT` requires A on **both** devices; both show peer name + operation + state; every wait has a timeout → "CONEXIÓN PERDIDA / A: Reintentar / B: Salir" (§47).
- [ ] BOX inspect menu gains "INTERCAMBIAR" and "CRIAR" (§9 "initiate breeding; initiate trade"): pre-selects the Pebble and enters LINK with that intent.
- [ ] `test_screens` snapshots for LINK states.

**P7-C3 Battle over link (lockstep)** — M
- [ ] `BATTLE` state driven by `battle_link.cpp` over the real transport; rewards only on `BATTLE_END(OK)`; `DESYNC` shows a neutral message, no rewards, both Boxes untouched.
- Acceptance: §67 "Local multiplayer works" (bench, two boards).

**P7-C4 Trade (atomic)** — M
- [ ] `game/trade.{h,cpp}` + `trade_link.cpp`: OFFER (wire instance) → both `validate_pebble` → READY → CONFIRM → COMMIT (§16); `PendingTrade` (`tr`) written **before** COMMIT with both ids and a phase byte; on boot a pending record in phase < COMMIT rolls back, in COMMIT completes; incoming instance gets a fresh `id` + `TRADED` flag + `origin TRADED`; `save_checkpoint_all()` after; `test_trade.cpp` with fault injection at each of the 5 phases (loopback disconnect + `kv_mem` power loss): never duplicated, never lost.
- Acceptance: §67 "Trade is atomic".

**P7-C5 Breeding** — M
- [ ] **Re-implement the god-taint gate in the game layer.** P2-C7b deleted `peer_block_reason()` along with the rest of the mating protocol, and with it the rule that a god-tainted unit must not pollute a real dynasty (it refused `BLE_BF_DEBUG` peers unless the local unit was itself tainted). The flag is still received and stored in `BlePeerInfo.peer_flags`, but **nothing reads it any more**. Correct for a transport-only module, wrong as a permanent state: without this gate a tainted genome can enter a real lineage through breeding or trade. Enforce it here and in `game/trade` (`GN_TAINT` in the incoming genome vs the local one), not in the transport.
- [ ] `game/breeding.{h,cpp}` over `genome_breed` (genome.cpp:420-511): compatibility = same `compat_group`, both stage ≥ 1, distinct ids (§17); offspring species = base stage of the family of the parent chosen by the shared seed; genome variation clamped to the built-in budget (§17 "hard balance ceiling"); one inherited move from each parent if legal; `lineage_id = hash(A,B)`, `generation = max+1`; both devices compute the same child from the shared seed; result goes to the Box only with a free slot and A-confirm on both sides (fixes audit risk 5: no auto-accepted egg); `breed_link.cpp`.
- [ ] `test_breeding.cpp` (compat matrix; offspring passes `validate_pebble`; budget ceiling holds over 10k random pairs; determinism; genome CRC valid).
- Acceptance: §67 "Breeding compatibility works", "Generated Pebbles remain balanced".

**P7-C6 Phase-7 exit** — S
- [ ] Matrix + tag `v0.7.0-social`; D2 outcome recorded; if BLE was kept, the measured leak x 32 sessions must fit the heap budget or `FEATURE_BLE` goes to 0.
- Commit message: `phase-7: social — ESP-NOW (or BLE per D2) transport behind the §59 seam, consent-gated LINK, lockstep P2P battle, journaled atomic trade, balanced breeding`

### Phase 8 — Creator — size L — goal: PIN, Wi-Fi AP/server, QR, mobile UI, sprite editor, validator, upload

**P8-C1 PIN state** — S
- [ ] `ConfigV2.creator_pin` generated at first CREATOR entry (random 4 digits, shown on the device only), `pin_fail_count` + `pin_lock_until` (5 failures → 60 s); `pin_ok()` (rewritten from scratch; the pre-P2-C5 body is at `git show aa2b7ee:Pebblebol/webui.cpp` and used the 64-bit accumulator fix) reads it from an `X-Pin` header or JSON field; PIN removed from the QR payload (`net_url`, net.cpp:653-671; §39).

**P8-C2 AP-only Wi-Fi lifecycle** — M
- [ ] `CREATOR` state `enter`: `net_request(RADIO_WIFI)` → `NPH_AP_CREATOR` (softAP `PEBBLEBOL-XXXX` + generated `ap_pass`, net.cpp:305-348 reused; no STA path); server binds only in the AP phase (gate exists, webui.cpp:1088-1094); inactivity timer (`web_client_seen_ms`, webui.cpp:127) `ConfigV2.creator_idle_s` (default 300, D7) → `web_stop()` + `net_request(RADIO_OFF)` + `sm_back()` (§34, §40); `leave` = same teardown.
- Acceptance: §67 "Wi-Fi activates only when necessary", "Inactivity timeout works".

**P8-C3 Endpoints + on-device validation + raw body cap** — M
- [ ] `creator_server.cpp`: the 7 §38 routes; `GET /api/schema` served from the generated `creator_schema.h` (types, attacks with `budget_cost`, sprite dims 24x24x2, name rules, `CREATOR_API_VERSION`) so device and page never disagree (T4); `POST /api/time` → `gt_set_epoch(CAL_PHONE)`; rate limiter kept (webui.cpp:270-285); body cap via the 4-arg `on(uri, HTTP_POST, fn, rawfn)` overload + `canRaw()` path (core `RequestHandlersImpl.h:97`, `Parsing.cpp:182-193`): at `RAW_START` reject `clientContentLength() > CS_BODY_MAX (2048)` with 413 and accumulate chunks into a fixed 2 KB buffer (no `String` growth, no `Content-Length` malloc, audit §12); fixed-schema JSON tokenizer (name, type, base[4], moves[4], sprite hex 288 chars, cosmetics; anything else → 400); every upload → `validate_custom_species` (§35) + budget (§36) → `cs*` record + `pebble_new(origin CREATOR)` into a free slot (full Box → device asks, never overwrites).
- [ ] `test_validate.cpp` += creator schema cases (App. C valid 72 %, "too strong", name charset, sprite size); `test_creator_api.cpp` (body parser fuzz: truncated, wrong types, oversize → error, never crash).
- Acceptance: §67 "PIN required", "Device-side validation works".

**P8-C4 Mobile page + sprite editor** — L
- [ ] `web/creator/index.html` + `app.js` + `sprite_editor.js` (vanilla JS, offline after load, screens of §33: CONNECT → INFO → TYPE → BODY → SPRITE → ATTACKS → VALIDATE → PREVIEW → UPLOAD) + `tools/gen_index_html.py` → `src/data/index_html.h` (cap `WEB_HTML_MAX` 48 KB, config.h:574); sprite editor 24x24 x 2 frames (T13) with draw/erase/fill/undo/clear/flip/preview 1x/2x, exported as XBM rows; budget bar "PRESUPUESTO 82 %" computed from `/api/schema` costs; PIN modal; client-side validation mirrors the schema but the device re-validates (§35).
- Acceptance: §67 "Mobile editor works", "Sprite editor works".

**P8-C5 QR screen, smoke script, exit** — S
- [ ] CREATOR screen "ESCANÉAME / [QR] / PIN: NNNN" (§34); QR payload alternates `WIFI:S:<ssid>;T:WPA;P:<pass>;;` and `http://192.168.4.1/` (existing alternation ui.cpp:2357-2441 minus `?k=`); `qr.cpp` untouched.
- [ ] `tools/creator_smoke.sh`: curl over all 7 routes incl. oversize body → 413, wrong PIN → 403, 6th wrong PIN → locked, idle timeout observed.
- Acceptance: §67 "QR connection works"; matrix + tag `v0.8.0-creator`.
- Commit message: `phase-8: creator — persisted PIN, AP-only Wi-Fi with idle shutdown, §38 routes with raw-body cap and shared validator, committed page source with 24x24 sprite editor, secret-free QR`

### Phase 9 — Content — size L — goal: 60+ Pebbles, attacks, items, evolution families, encounter tables; corruption mechanic

**P9-C1 Content pipeline complete** — M
- [ ] `gen_content.py` finished (all guards, `balance_report.py` printing per-type averages and budget histogram); `test_content.cpp` extended (≥ 60 species, every family has a spawnable stage 0, every evolution target is the next stage of the same family, every built-in 4-move set passes the budget, names ≤ 9 chars at 5x8 width, `CONTENT_VERSION` changes when JSON changes); `docs/content.md` (§53 authoring rules, regeneration).

**P9-C2 Roster** — L
- [ ] 20 families x 3 stages from Appendix A (§19, §53): proper-noun names, types balanced 20/20/20, base stats 1..10 with per-stage totals (≈ 16 / 22 / 28), 4 legal moves each from ~30 attacks, `evo_level` 10/20 pattern with `evo_cond` used by ≤ 5 families, rarity 30/18/9/3, `category_mask`, 5 compat groups; ~10 items; encounter tables per category (NOTHING ≥ 15 %); Spanish flavor lines (≤ 25 chars at 5x8, strings_es.h:17-22 width rule) and the §54 terminology pass.

**P9-C3 Sprites** — L
- [ ] `tools/gen_sprites.py` (ASCII art → XBM; re-creation of the uncommitted `sprite_src.py`, sprites.h:3-4) + `tools/sprites/*.txt`; 60 x 2 frames 24x24 = 8,640 B into `sprites_pebbles.h`; delete the 38 Nottamagochi sets; `SPRITE_DATA_BYTES` assert re-set (sprites.h:1259-1260); final stages visibly "more destructive" (§18-§19); `test_screens` renders every species at 1x and 2x on HOME/BOX/BATTLE fixtures with zero out-of-bounds (§63).

**P9-C4 Balance pass** — M
- [ ] `tests/tools/balance_matrix.cpp` (host): 60x60 AI-vs-AI at equal level, 1,000 seeded battles per pair; flag > 65/35 splits and any type < 40 % aggregate; adjust JSON, regenerate.
- [ ] `tests/tools/sim_days.cpp` (host): 30 simulated days of care/XP/encounters under seeds; prints level curve, hunger dips, encounter counts; tune `balance.h` until §57 holds (ignoring 8 h never drops health below 60 %; level 30 ≈ 3-4 weeks of normal play).
- [ ] Nickname syllable generator (§54) moved from ui.cpp:325-331 to `game/pebble.cpp`.

**P9-C5 Corruption mechanic (§55)** — M
- [ ] `game/corruption.{h,cpp}`: `CORRUPTED` status set by the SPECIAL encounter (P5-C3) or a rare trait; effects for 24 h: sprite glitch (petfx XOR-noise rows, 1 in 8 frames), altered behaviour (idle animation set), battle modifier (+1 atk / −1 def stage), and `EVO_COND_CORRUPTED` unlocking a unique evolution in ≤ 2 families; clears by timer or a care item; never destroys (§55). `test_corruption.cpp` (timer, effects bounded, clears, evolution condition).
- Acceptance: gate; §67 content roster ≥ 60 (Appendix A); matrix + tag `v0.9.0-content`.
- Commit message: `phase-9: content — 60-species roster with generated tables and 24x24 atlas, ~30 attacks, items, evolution families, encounter tables, corruption as a signature mechanic, balance passes`

### Phase 10 — Polish — size M — goal: animation, sound abstraction, balance, UX, battery, diagnostics, release build

**P10-C1 Diagnostics from god-mode plumbing** — M
- [ ] `dev/diagnostics.cpp` replaces `godmode.cpp` content: §49 fields (firmware, build, free/min heap, battery "n/a", Wi-Fi/BLE state, clock + cal state, save version, Pebble count, last scan, last error, protocol version) + §49 actions (`test_encounter`, `test_evolution`, `test_battle`, `test_minigame`, `test_save_load`, `set RNG seed`) + §66 serial commands (`give_item`, `spawn`, `set_level`, `set_time`, `set_activity`, `heal`, `fill_box`, `clear_box`, `start_battle`, `start_creator`, `scan_wifi`, `show_save`); entry stays the 5 s hold (godmode.cpp:711-730) with marker/taint; compiled out by `GOD_MODE_ENABLED 0` (godmode.cpp:16, :1476-1494 stub pattern).

**P10-C2 Error recovery completeness + performance gates (§46)** — M
- [ ] Every radio wait has retry/exit (§47); every state has a timeout path listed in `test_statemachine`.
- [ ] Performance: `rd_frame_time_us` (dead export today, audit §13.2) → DIAG shows max frame µs and worst `loop()` iteration µs per screen; acceptance thresholds in `tools/check.sh` bench log: frame ≤ `FRAME_BUDGET_US` (50 ms, config.h:155) on every screen, `loop()` iteration ≤ 100 ms except LOAD_SAVE catch-up (≤ 1 s), per-frame heap delta 0 on HOME/BATTLE/GAME, input-to-render latency ≤ 2 frames measured with the DIAG `stall` command; 24 h soak with the heap line flat (§46 "no memory leaks").
- [ ] `hardware/audio.h` + `audio_null.cpp` (no buzzer, README.md:71; §64) so a future board can add one without game changes; sound setting persists.

**P10-C3 Animation pass** — M
- [ ] Battle hit/faint/protect effects on `rd_flash/rd_shake/rd_dither_rect`; evolution dissolve; capture success and item pickup films on the actfx pixel writer (actfx.cpp:234-353); idle/blink/walk from PetRenderer for 24x24 bodies; corruption glitch visuals (P9-C5); all time-based and interruptible (actfx.h:88-93 contract); each new film gets a `test_screens` snapshot.

**P10-C4 Onboarding, accessibility, §63 audit** — M
- [ ] First boot: name device, set time (P2-C6 screen), pick starter (3 choices); §65: no time-critical menus, all text via `rd_text_fit`/`rd_text_wrap` (render.cpp:591-673), long nicknames truncated on codepoint boundaries; `test_screens` re-run with 12-char names and level 30 / 100 % stats fixtures on every screen (§63); grep gate `rd_u8g2()` == 0 outside `ui/`.

**P10-C5 Release build + docs** — M
- [ ] `tools/build.sh release`: `GOD_MODE_ENABLED 0`, `FEATURE_BLE` per D2, size gates; `build_matrix.sh` clean; save migration exercised once more by bumping `SAVE_SCHEMA_VERSION` to 3 with a no-op migration + test (proves the machinery is live, §31); factory reset flow verified; checkpoint restore verified after an `esptool` erase of `nvs`.
- [ ] English `README.md` (bring-up from README.md:89-215 after D1, build, test, architecture map, partition table), `CHANGELOG.md` restarted with measured flash/RAM; Spanish docs moved to `docs/legacy/`; `docs/decisions.md` closed.
- Acceptance: §67 "Quality" block; tag `v1.0.0`.
- Commit message: `phase-10: polish — diagnostics state and dev commands, performance gates and 24 h soak, animation pass, onboarding and §63 audit, release build, English docs`

---

## 4. Test plan

### 4.1 Layout

```text
tests/
  Makefile               g++ -std=c++17 -Wall -Wextra -Werror -O1 -I../Pebblebol/src -DNT_HOST
  nt_test.h              TEST(name) static registry; CHECK/CHECK_EQ/CHECK_NEAR (file:line); main(--seed N, --filter substr); non-zero exit on failure
  host_shims.cpp         nt_input_test_millis/level (input.cpp:45-46), gt_host_millis32 (gametime.cpp:28)
  fakes/                 kv_mem.cpp (two partitions, fault injection)  gfx_fb.cpp (128x64 framebuffer + font metrics + OOB recorder)
                         clock_fake.cpp  transport_loopback_host.cpp (drop/dup/reorder knobs)
  legacy/                mkfixtures.cpp (links legacy PetSave/Config/GainSave, writes fixtures/)
  fixtures/              petsave_v1_adult.bin  petsave_v1_egg.bin  config_v1.bin  gainsave_v1.bin
  golden/                sim_v1.txt (→ care_v2.txt after retunes)   screens/<screen>_<state>.pbm
  tools/                 balance_matrix.cpp  sim_days.cpp  (host simulators, not part of `check`)
  test_*.cpp             one binary per file (list in §4.3)
```

Makefile shape: `SRC_PURE = ../Pebblebol/src/core/{crc16,rng}.cpp ../Pebblebol/src/game/*.cpp ../Pebblebol/src/persistence/{save_manager,migration}.cpp ../Pebblebol/src/networking/{protocol,session,battle_link,trade_link,breed_link}.cpp ../Pebblebol/src/minigames/games/*_logic.cpp ../Pebblebol/src/minigames/manager.cpp ../Pebblebol/src/hardware/{input,gametime}.cpp ../Pebblebol/src/ui/qr.cpp`; each `test_X.cpp` links `nt_test.h` + the objects it needs + `fakes/`; targets `all`, `check` (runs every binary, stops on first failure, prints `ALL PASS n/n`), `golden` (regenerates goldens; used only by `retune:`/`snapshot:` commits), `fixtures`. Everything seeded explicitly through `rng_seed_all(0xC0FFEE)` unless a test passes its own seed (§30, §68 r10). No external framework, no network, no Arduino headers.

### 4.2 Harness rules

- Pure modules compile with `-Werror` and no Arduino include path; a stray `#include <Arduino.h>` in `src/game/**` fails the build.
- Goldens: `tests/golden/sim_v1.txt` pins the care trajectory before any refactor (P2-C2); only `retune:` commits regenerate it. Screen snapshots (`*.pbm`) are regenerated only by `snapshot:` commits.
- Fault injection: `kv_mem_fail_next_put()`, `kv_mem_corrupt(key, byte)`, `kv_mem_wipe_partition(KV_MAIN)`; loopback `lb_set_drop_permille/lb_set_dup_permille/lb_set_reorder_window`.
- `tests/tools/*` are simulators run by hand at balance passes; they are built by `make tools`, not by `check`.

### 4.3 §50 list mapped to test files

| §50 area | Item | Test file(s) | Phase |
|---|---|---|---|
| Domain | XP | `test_xp.cpp` | 3 |
| Domain | level-up | `test_xp.cpp` (carry-over, cap 30, hp rescale) | 3 |
| Domain | evolution | `test_evolution.cpp` | 3 |
| Domain | stat calculation | `test_stats.cpp`, `test_content.cpp` (stage totals) | 4, 9 |
| Domain | care decay | `test_sim_golden.cpp` (P2), `test_care.cpp` (P2-C7, P3-C1) | 2, 3 |
| Domain | box rules | `test_box.cpp`, `test_box_full_capture.cpp` | 2, 5 |
| Domain | breeding compatibility | `test_breeding.cpp` (compat matrix) | 7 |
| Domain | breeding balance | `test_breeding.cpp` (budget ceiling over 10k pairs), `test_genome.cpp` | 7, 2 |
| Battle | type advantage | `test_battle.cpp` `type_mod_chart` | 4 |
| Battle | damage | `test_battle.cpp` `damage_vectors` (advantage > neutral > disadvantage; buffs) | 4 |
| Battle | speed order | `test_battle.cpp` `speed_and_priority_order` (tie deterministic under seed) | 4 |
| Battle | switching | `test_battle.cpp` `switch_costs_turn` (cannot switch to fainted or self) | 4 |
| Battle | fainting | `test_battle.cpp` `faint_forces_switch`, `wipe_is_victory` | 4 |
| Battle | buffs | `test_battle.cpp` `buff_duration` | 4 |
| Battle | protection | `test_battle.cpp` `protection_negates_once` | 4 |
| Battle | victory | `test_battle.cpp` `victory_detection`, `test_battle_replay.cpp` (hash equality) | 4 |
| Battle | invalid moves | `test_battle.cpp` `illegal_action_rejected_state_untouched`, `test_battle_ai.cpp` (never invalid) | 4 |
| Capture | probability | `test_capture.cpp` | 5 |
| Capture | inventory | `test_inventory.cpp` | 5 |
| Capture | full box | `test_box_full_capture.cpp` | 5 |
| Exploration | cooldown | `test_cooldowns.cpp` (persist, expiry, eviction, rollback, uncalibrated RAM path) | 5 |
| Exploration | encounter selection | `test_encounters.cpp` (weights, category mask, level clamp) | 5 |
| Exploration | deterministic seeds | `test_encounters.cpp`, `test_rng.cpp`, `test_exploration_hash.cpp` | 5, 2 |
| Persistence | save | `test_persistence.cpp` `roundtrip_every_blob`, `pair_write_verify`, `save_atomic_fault_injected` | 2 |
| Persistence | load | `test_persistence.cpp` `load_tristate`, `header_slot_mismatch_self_heal`, `ckpt_recovery_after_main_wipe` | 2 |
| Persistence | checksum | `test_crc16.cpp`, `test_persistence.cpp` `crc_flip_recovers_from_other_copy` | 2 |
| Persistence | migration | `test_persistence.cpp` `migrate_v1_fixtures`, `foreign_newer_rejected_untouched`; `test_fixtures.cpp` | 2 |
| Persistence | corruption | `test_persistence.cpp` `both_copies_bad_is_LOAD_CORRUPT_no_autoreset` | 2 |
| Protocol | malformed packet | `test_protocol.cpp` `decode_truncated_flipped_oversize_len_mismatch` | 4 |
| Protocol | unsupported version | `test_protocol.cpp` `version_reject` | 4 |
| Protocol | invalid Pebble | `test_protocol.cpp` `team_submit_invalid_rejected`, `test_validate.cpp` | 4, 8 |
| Protocol | disconnect | `test_session.cpp` `drop_at_each_state_no_mutation`, `test_trade.cpp` | 4, 7 |
| Protocol | replay/out-of-order | `test_session.cpp` `dup_seq_dropped`, `gap_detected`, `faulty_loopback_10pct` | 4 |
| Beyond §50 | infrastructure | `test_rng.cpp`, `test_genome.cpp`, `test_input.cpp` (waveforms, tap latency ≤ 30 ms, no phantom TAP), `test_qr.cpp`, `test_clock.cpp` (format, wrap, set-epoch, rollback, sleep gap), `test_overflow.cpp` (§26) | 2, 3, 6 |
| Beyond §50 | state machine + screens (§6, §63) | `test_statemachine.cpp`, `test_screens.cpp` (OOB = 0, PBM goldens, long-name fixtures) | 2-10 |
| Beyond §50 | minigames (§28-§29) | `test_minigames.cpp` (determinism, ≤ 15 s, `finish()` once) | 3 |
| Beyond §50 | activity, discovery, creator body, corruption, content | `test_activity.cpp`, `test_discovery.cpp`, `test_creator_api.cpp`, `test_corruption.cpp`, `test_content.cpp` | 6, 7, 8, 9 |
| Beyond §50 | balance simulators | `tests/tools/balance_matrix.cpp`, `tests/tools/sim_days.cpp` | 9 |

Invocation everywhere: `make -C tests check [ARGS="--seed N --filter battle"]`; `tools/check.sh` runs it after `arduino-cli compile`; CI runs both.

---

## 5. Definition of done (spec §67 as checkboxes, annotated with the closing commit)

### Core
- [ ] Device boots reliably. — P2-C0 (first flash after D1), P2-C11 (BOOT/ERROR states, no `rd_fatal`), P10-C2 (24 h soak)
- [ ] Game state persists. — P2-C9 (schema v2, pairs, `nvs2` checkpoint), P10-C5 (migration bump proof)
- [ ] Two-button input is robust. — P2-C6 (timer sampler), P3-C4 (25 ms tap), P2-C11 (§7 grammar)
- [ ] Box supports 10 Pebbles. — P2-C10, P2-C11 (BOX screen)
- [ ] Active Pebble can be selected. — P2-C11 (`box_set_active` + `sim_switch`)

### Pet
- [ ] Care works. — P3-C1
- [ ] Stored Pebbles recover. — P2-C10 (`box_recover`), P3-C1 (rates)
- [ ] Time-based calculations work across reboot. — P2-C6 (clock model), P2-C10 (catch-up), P6-C3 (sleep)
- [ ] XP and leveling work. — P3-C2
- [ ] Evolution works. — P3-C3

### Games
- [ ] At least 5 minigames. — P3-C4 (6 games)
- [ ] All are playable with two buttons. — P3-C4
- [ ] Minigames transition cleanly. — P3-C4 (manager sequence, `finish()`)

### Battle
- [ ] 3-Pebble team. — P4-C2, P4-C4
- [ ] 1v1 active combat. — P4-C2, P4-C4
- [ ] Four attacks. — P4-C1, P4-C2
- [ ] Three-type effectiveness. — P4-C1 (`TYPE_CHART`), P4-C2
- [ ] Buffs/protection. — P4-C2
- [ ] Switching costs a turn. — P4-C2
- [ ] Local multiplayer works. — P7-C3 (lockstep over the D2 transport)
- [ ] Invalid peers cannot inject illegal data. — P4-C5 (shared validator, host-proven), P7-C3

### Exploration
- [ ] Wi-Fi scanning works. — P5-C1
- [ ] Device does not connect to scanned networks. — P5-C1 (grep gate `WiFi.begin` == 0)
- [ ] Cooldowns persist. — P5-C2
- [ ] Encounters work. — P5-C3
- [ ] Capture works. — P5-C4
- [ ] Items work. — P5-C4

### Social
- [ ] Trade is atomic. — P7-C4 (journal + fault-injected test)
- [ ] Breeding compatibility works. — P7-C5
- [ ] Generated Pebbles remain balanced. — P7-C5 (10k-pair ceiling test)

### Creator
- [ ] PIN required. — P8-C1, P8-C3
- [ ] QR connection works. — P8-C5
- [ ] Wi-Fi activates only when necessary. — P2-C6 (OFF by default), P8-C2
- [ ] Inactivity timeout works. — P8-C2
- [ ] Mobile editor works. — P8-C4
- [ ] Sprite editor works. — P8-C4
- [ ] Device-side validation works. — P8-C3

### Power
- [ ] Wi-Fi shuts down after use. — P5-C1, P8-C2
- [ ] Device sleeps correctly. — P6-C3
- [ ] Timed systems work after sleep. — P6-C3
- [ ] No obvious battery-draining loops. — P6-C3 (idle yield), P10-C2 (loop rate in DIAG)

### Quality
- [ ] Automated tests pass. — P2-C2 onward; CI from P1-C1
- [ ] Release build compiles cleanly. — P10-C5 (`build.sh release`, matrix 0 warnings)
- [ ] No known save corruption path. — P2-C9 (tri-state, pairs, checkpoint, fault injection), P7-C4 (journal), P10-C5
- [ ] No obvious memory leak. — P2-C12 (1 h baseline), P7-C1 (BLE cycle measurement if kept), P10-C2 (24 h soak flat)
- [ ] Diagnostics available. — P4-C4 (`test_battle` entry), P10-C1 (§49 fields/actions, §66 commands)

---

## 6. Risks and mitigations carried from the audit (§18) plus the gaps closed by this synthesis

| # | Risk | Severity | Mitigation / where closed |
|---|---|---|---|
| 1 | **Pin conflict** (config.h:92-96 vs README/render comments; strapping pins GPIO2/8/9; no LED on GPIO5) | High | D1 owner decision; `config.h` untouched; `PB_PINS_CONFIRMED` static_asserts (P2-C8); P2-C0 smoke flash; deep-sleep variant of P6-C3 depends on the outcome |
| 2 | Always-on Wi-Fi policy (.ino:182-226; storage.cpp:801-803; net.cpp:574-586) | High | P2-C6 (OFF by default, screen-owned requests), P8-C2 (idle shutdown) |
| 3 | Corrupt/foreign save silently replaced (storage.cpp:508-519; .ino:279-283) | High | P2-C9 tri-state + SAVE ERROR + pairs + `nvs2` checkpoint + migration tests |
| 4 | Single radio + 19 B advert protocol cannot carry §15 (ble_social.cpp:62-91; config.h:559) | High | P4-C5 codec/session on loopback; P7-C1 transport per D2 behind the seam |
| 5 | Game rules inside the transport; responder auto-accepts an egg (ble_social.cpp:498-534; ui.cpp:2071-2083) | High | P2-C7b strips it; P7-C2/P7-C5 consent on both sides; rules in `game/breeding` |
| 6 | `ui.cpp` monolith (3,578 lines, 74 mutable statics, three parallel switches) | High | P2-C11 strangler with per-screen commits and grep exit gate |
| 7 | Boolean spaghetti / `ui_goto` bypasses (ui.cpp:2460, :2729, :3463) | Medium | P2-C11 table dispatch; grep gate `s_screen\s*=` |
| 8 | ctags traps in the `.ino` (.ino:8-21) | Medium | P2-C8 3-line `.ino` |
| 9 | Layering inversions petfx→sim, ui→webui, ui→godmode | Medium | P2-C11 `PetView` (with `identity`), `pet_view.cpp` owns pose/mood policy |
| 10 | Blocking calls in the cooperative loop; loop-polled input loses presses; 305 ms tap latency | Medium | P2-C4 (TLS gone), P2-C6 (`NPH_SETTLING`, timer sampler), P3-C4 (double-tap gone), P10-C2 (loop-iteration and latency gates) |
| 11 | Clock never valid without Wi-Fi; 6 h unknown-clock floor; TZ after reboot only; no rollback rule | Medium | P2-C6 (`gt_set_epoch`, cal state, rollback guard, zero absence when unknown), P8-C3 (`/api/time`), P5-C2 (RAM cooldown when uncalibrated) |
| 12 | No tests; retune regressions invisible | Medium | P2-C2 harness + golden before any refactor; CI from P1-C1 |
| 13 | Frozen wire formats with 2/3 spare bytes; `GainSave.slots == ST_COUNT` | Medium | P2-C9 schema v2 with 12 reserved bytes, versioning, migration table |
| 14 | Documentation drift (two FQBNs, three size figures, non-existent web settings UI) | Medium | P1-C1 (drift fixes), P10-C5 (English rewrite, legacy archive) |
| 15 | `rd_fatal` halts the device forever (render.cpp:1034-1074) | Low | P2-C11 BOOT → ERROR with retry (T10) |
| 16 | No power management; busy loop; uptime blind to deep sleep | Low → Phase 6 | P6-C3 power states, RTC `time()` as monotonic source, idle yield |
| 17 | Product identity in persisted names (`notta`, AP prefix, mDNS) | Low | D3; P2-C5 (mDNS gone), P2-C9 (`pbbl`), P8-C2 (AP prefix) |
| 18 | God mode ships on, no PIN, taints the save | Low | P10-C1 (DIAG), P10-C5 (`GOD_MODE_ENABLED 0` in release) |
| 19 | Repo hygiene (one commit, untracked spec, `.gitignore` = `*.bak`, stray `.docx`, no CI) | Low | P1-C1 |
| 20 | **`initArduino()` erases the whole `nvs` partition** on `NO_FREE_PAGES`/`NEW_VERSION_FOUND` (core `esp32-hal-misc.c:287-300`) — every in-partition checkpoint dies with it | High | T5/D6: `nvs2` checkpoint partition via sketch `partitions.csv` (P2-C9d); `LOAD_RECOVERED_CKPT` path tested with `kv_mem_wipe_partition(KV_MAIN)`; bench test with `esptool` erase (P10-C5) |
| 21 | **No hardware has ever run this code**; features could accumulate on an unverified pin map / panel / heap figure | High | P2-C0 explicit first-flash milestone (display probe, buttons, canary, LED, heap, frame time) scheduled before Phase 3; each phase exit carries a bench line |
| 22 | **§46 performance never measured**: `FRAME_BUDGET_US` and `rd_frame_time_us` are dead; sendBuffer ≈ 24 ms of a 50 ms budget | Medium | P2-C12 heap baseline; P4-C4 zero per-frame heap; P10-C2 frame/loop/latency/heap gates and 24 h soak |
| 23 | **§63 screens never tested at 128x64** on the host | Medium | P2-C11 `gfx.h` seam + `gfx_fb.cpp` + `test_screens.cpp` PBM goldens; every new screen/film adds a snapshot; P10-C4 long-name/maxed-stat fixture run |
| 24 | **§26 integer overflow** (xp u16 vs curve, u32 ages, epoch deltas, cooldown `until`) untested | Medium | `test_overflow.cpp` (P2-C6), `static_assert` on `XP_TABLE` (P3-C2), clamped deltas in §1.7 |
| 25 | **Task WDT 5 s vs offline catch-up in `setup()`** (2,000 steps x ~100 soft 64-bit divisions) | **CLOSED (P2-C10, measured)** | 400 d worst case = 8.4 ms on the host at -O1, ~421 ms on a 160 MHz C3 at the plan's 50x factor. Under the 1 s threshold and 12x under the WDT, so catch-up stays non-resumable. |
| 26 | **§61 OTA room**: `huge_app` has no OTA slot; image 2.1 MB | Low (non-goal V1) | D6 records the partition decision; `FEATURE_BLE 0` (D2) makes a two-slot layout possible later; save data lives in `nvs`/`nvs2`, untouched by app slots |
| 27 | Language rule: Spanish comment block config.h:15-76, stale BRIEF/GAME_DESIGN references in kept headers | Low | P2-C8 (G7), P10-C5 |
| 28 | §28 `finish()` hook, §9 BOX → trade/breed entry points, §55 corruption — spec items all three drafts missed | Low | P3-C4 (`finish()`), P7-C2 (BOX menu entries), P5-C3 + P9-C5 + P10-C3 (corruption) |
| 29 | Bluedroid ~672 B/cycle leak claim (config.h:559) unmeasured; 32-session cap | Medium if D2 = BLE | P7-C1 measures over 32 cycles before the fallback is accepted; `FEATURE_BLE 0` otherwise |
| 30 | Milli-point integrator vs u8 care fields (two drafts proposed u8 stats while claiming the integrator unchanged) | Design | T2: `int32 care[5]` + `int16 care_rem[5]` retained exactly as `accum_stat()` needs (sim.cpp:192-197) |

Rules of the road (§68 + audit) that every commit obeys: never change a persisted or transmitted layout without a version bump, migration/reject path and test; never add a second radio owner; keep `game/`, `persistence`, `protocol`, `minigames/*_logic` free of Arduino includes so the host tests stay valid; compile with `--warnings all` at 0 warnings and run the tests after every commit; no pin edit, NVS namespace edit or transport choice without an entry in `docs/decisions.md`; no `retune:` without regenerating the golden and stating why.
