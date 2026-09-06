# Pebblebol — the bench list

**What this file is.** Everything in the product that this repository cannot check, in the order
one person should run it, with two boards, a phone and a serial terminal. Nothing here is a
to-do: every item is a claim the firmware makes that no host binary, no gate and no browser
harness can settle, because settling it needs hardware.

**What this file is not.** It is not a summary of what is tested. `tools/check.sh` is green on 55
host binaries, an AddressSanitizer subset, 51 browser assertions and about thirty greps, and none
of that is evidence for a single line below.

**Why it exists as one document.** These items were spread across seven phase records, and a list
distributed over seven records is a list nobody runs. Written by P10-C2, the last build chunk.

---

## 0. Before anything

| | |
|---|---|
| **Hardware** | 2 x ESP32-C3 with the §68 pin map, 2 x SSD1306/SH1106 128x64 on I2C 0x3C, 2 x piezo on `PIN_PIEZO`, one phone with a camera and a browser, one USB serial console at 115200. |
| **Blocked on** | **decision D1 (the pin map) is still open and `PB_PINS_CONFIRMED` is never defined.** Nothing below can run until a board is wired to a decided map. This is the first thing the owner owes, and it gates all twenty items. |
| **Variants** | `tools/build_matrix.sh` builds six. Only two matter here: **`release`** (`GOD_MODE_ENABLED=0`) is the artefact that ships and the one every acceptance claim must be made about; **`baseline`** has the god console and is the only build with a DIAG screen at all. **Every item below names its variant. A reading taken on the wrong one is not a reading** — `docs/budget.md` §8 records a phase-7 exit that quoted a dev build against a shipping one. |
| **The serial line** | The release artefact prints `DIAG#,heap` and `DIAG#,perf` headers at boot and one row of each every 60 s (`GOD_HEAP_PERIOD_MS`), for ever, on every screen, with no console and no god mode. It also answers four typed commands: `help`, `info`, `show_save`, `stall <ms>`. That is the whole of the shipping build's diagnostics and it is what most of section A is read from. |

Capture every session: `arduino-cli monitor -p <port> -c baudrate=115200 | tee docs/bench/$(date +%F)-$(git rev-parse --short HEAD).log`

---

## A. One board, one seat — spec §46, the five performance thresholds

None of these five is gated by `tools/check.sh` and none can be. `micros()`, I2C and U8g2 do not
exist on the host; `ui/render.cpp`, `ui/ui.cpp`, `app/app.cpp`, `ui/petfx.cpp`, `ui/actfx.cpp` and
`ui/ceremony.cpp` are compiled by **zero** host binaries; and the dominant term in a frame is a
fixed ~24 ms of I2C to a panel that is not here. What the host gates instead is written under each
item, so a green suite is never mistaken for a green threshold.

### A1. A frame fits inside `FRAME_BUDGET_US` (50 ms) on every screen — **variant `baseline`**

1. Flash `baseline`. From STATUS_B hold both buttons for `GOD_ENTER_HOLD_MS` (5 s) to open the
   console. Navigate to **SYS**, then press A to page round to **RENDIMIENTO**.
2. The page reads `f now <us> max <us>`, `f worst <us> s<screen> over <n>`, `p worst <us>
   s<screen> over <n>`, `n <frames>/<passes> drop <n>`. The maxima are **since boot**, so a walk
   does not need resetting between screens.
3. Walk all nineteen reachable screens, sit ten seconds on each, then come back to the page.
4. **Four screens must be visited in a specific state**, because their expensive layer is
   conditional and a walk-past will not draw it: **HOME with the pet awake and wandering**
   (`ui/petfx.cpp`'s automaton), **HOME during an action film** (`ui/actfx.cpp` — feed it),
   **the hatch ceremony** (`ui/ceremony.cpp`, 4,480 ms, not re-enterable — read it off `f worst`
   afterwards), and **BATTLE mid-round against a live peer**.
5. **PASS:** `over` is 0 and `max` is below 50000 on every screen. A trailing `!` after `max`
   means the 16-bit per-screen maximum SATURATED at 65,535 us — that is a breach, not a near miss;
   read the true figure off `f worst`.
6. Cross-check on the shipping build: flash `release` and read `frame_max_us` / `frame_over` /
   `frame_sat` off the `DIAG,perf` line. `baseline` is conservative in the right direction
   (`release` does not pay for `god_draw_marker()`), so `release` should be the same or better.

*The host gates instead:* that a screen render performs **zero allocations** and stays under a
compositing ceiling of three panels' worth of pixels (`tests/test_screens.cpp`). Both are work,
not time; `tests/fakes/gfx_fb.cpp` says at length why a draw-call count is a proxy for the small
half of a frame, and why HOME's host number is a count of a composition that does not ship.

### A2. One `loop()` pass fits inside `PERF_PASS_BUDGET_US` (100 ms) — **variant `baseline`**

1. Same page, the `p worst` row. **What is measured is the WORK of a pass**: `app_loop()` stamps
   `perf_note_pass()` immediately above stage 7's yield, because `pwr_yield()` naps deliberately
   for up to `PWR_SLEEP_SLICE_MS` (8,000 ms) and a stamp taken after it would read eight seconds
   on a healthy sleeping board. `tools/check.sh` gates the order of those two lines.
2. Provoke the only three states that can plausibly blow it:
   * a **Wi-Fi scan in flight** on NETWORK (the ladder is clamped at DIM and stage 7 spins without
     the `delay(1)` — `app.cpp`'s own `!pin.held` exception says so);
   * a **phone holding a slow POST** to the creator portal, e.g.
     `curl --limit-rate 200 -X POST http://192.168.4.1/api/pebble -H 'X-Pin: NNNN' -d @body.json`
     — this is `WebServer::handleClient()` -> `readBytesWithTimeout()`, which `core/config.h`'s
     `CS_BODY_MAX` comment says outright blocks the whole firmware, and it is the single largest
     loop risk in the tree;
   * **a linked battle** with a peer.
3. **PASS:** `p over` stays 0 and `p worst` stays under 100000.
4. **PLAN CORRECTION, and it changes what to measure.** The plan bullet's exemption "except
   LOAD_SAVE catch-up (<= 1 s)" points at a state that is **never resident**: `SCR_LOAD_SAVE` is
   never `sm_current()`, `ui_boot_screen()` draws one frame and `gs_load()` runs synchronously
   with no loop underneath it. The 1 s allowance is therefore a claim about **boot**, not about
   `app_loop()`, and its instrument is the wall time between the first boot line and the first
   `DIAG,heap` row on the serial capture. Read it there.

*The host gates instead:* the tick scheduler's response to a long pass (`tests/test_power.cpp`,
17 cases including `a_gap_wider_than_the_bound_charges_one_second_and_says_it_lost_the_rest`) and
the span/wrap/saturation arithmetic (`tests/test_perf.cpp`). Neither can say a long pass never
happens; the worst case is a network peer's behaviour.

### A3. The per-frame heap delta is 0 on HOME, BATTLE and GAME — **variant `baseline`, tainted board**

1. This one costs you the pet on that board: `god_enter()` calls `gene_set_tainted()`, and the
   probe only accumulates while the console is engaged. Use a board you are willing to mark.
2. Console on, SYS -> **MEMORIA**. The `dF <moves>/<worst>` figure is frames whose free heap
   differed from the previous sample, and the widest single delta.
3. Sit 60 s each on HOME, a **real linked** BATTLE, and all four minigames.
4. **PASS:** `dF 0/0` throughout.
5. **Read the number correctly.** The interval between two samples spans input, `logic_tick`,
   `ui_service`, `audio_service`, `god_service`, the power ladder, `net_service` and
   `web_service` — so a non-zero `dF` says "something between two draws moved the heap"; it
   cannot separate a render allocation from a Wi-Fi one. And in the **`release`** build both
   accessors are hardcoded `return 0`: reading `dF 0/0` off a shipping board would be reading a
   constant, which is why this item names `baseline`.

*The host gates instead:* zero allocations across all 65 screen snapshots and across a whole
simulated day of the pure loop (`tests/test_screens.cpp`, `tests/test_soak.cpp`), plus
AddressSanitizer over five binaries. All three see only code compiled here — they say nothing
about the Wi-Fi stack, `WebServer`, U8g2's buffer or the ESP-NOW transport, which is where a leak
would live.

### A4. Input-to-render latency is at most two frames — **variant `release`**

1. `stall` ships in the release build on purpose, and this is what it is for. Open a plain serial
   terminal on a freshly flashed board and type `stall 1000`.
2. **During the stall, press and release a button.** The 5 ms `esp_timer` sampler
   (`hardware/input.cpp`) records the edge with its true instant; the gesture must be recognised
   on the poll that follows the stall, with its true duration, not swallowed and not stretched.
3. The `<= 2 frames` half needs a camera. Film the panel and the thumb at 240 fps, press A on
   MENU, and count frames from contact to the highlight moving. At 20 fps, two frames is 100 ms.
4. **PASS:** the gesture made during the stall lands; the filmed gap is under 100 ms.
5. **What a green reading does not prove.** The chain is press -> 5 ms sampler -> ring ->
   `input_poll()` -> `ui_handle()` -> the next `rd_begin_frame()` due at <= 50 ms -> 24 ms of
   I2C -> photons. Only the first link is host-visible, and the panel's own refresh is not in the
   firmware at all.

*The host gates instead:* the recogniser half on a fake clock against the real `input.cpp`
(`tests/test_input.cpp`'s `input_tap_left_lands_within_30ms_of_release` — one debounce plus one
poll), and the render scheduler's deadline arithmetic, which buys the second frame
(`tests/test_perf.cpp`: advance, resync-after-stall, the millis wrap, the overrun backoff). That
arithmetic had shipped since `8aff64f` with no test of any kind until P10-C2 extracted it.

### A5. 24 hours with a flat heap line — **variant `release`, twice**

1. This is the one item that MUST run on the shipping artefact, because it is the one the shipping
   artefact can do by itself. Flash `release`, `tee` the serial line, walk away.
2. **Run A:** 24 h on HOME with the radio off. **Run B:** 24 h with an hourly scan/link cycle,
   because that is where a leak would actually be.
3. Read the `DIAG,heap,<uptime_s>,<free_b>,<min_free_b>` rows. `min_free_b` is
   `ESP.getMinFreeHeap()`, the IDF's own low-water mark since boot, so a leak shows as a falling
   minimum even when the instantaneous free heap looks calm.
4. **PASS:** `min_free_b` flat over the last 20 h (a fall in the first hour or two is the Wi-Fi
   stack's high-water settling, not a leak); `frame_over` and `pass_over` on the `DIAG,perf` rows
   still 0; `uptime_s` strictly increasing, with **no gap wider than three periods** — a board
   that rebooted mid-run shows a beautifully flat heap and has proved nothing.
5. Ticking §67's **"Device boots reliably"** wants this run plus twenty power cycles with the
   save intact.

*The host runs instead:* `tests/test_soak.cpp` — one whole simulated day of the real loop (sim,
Box, cooldowns, the corruption deadline, the real `save_manager` writing every simulated minute)
with an allocation counter armed for the entire run, requiring zero, and a reload afterwards that
must still validate. **It is not this item and must never be quoted as one:** it links none of the
four subsystems that allocate on a device.

---

## B. Two boards — spec §67, the social block

Run these in order; each depends on the one above it. All on **`release`** unless a step says
otherwise, because a peer link is a claim about the artefact that ships.

**B1. Two boards list each other in LINK.** `link_qualified_count()` reaching 1 needs three
beacons at 500 ms above `LINK_RSSI_MIN`. This is also the modem-sleep check P7-C1 owes: confirm
the beacon count does not collapse after the radio has been idle.

**B2. The consent gate, on the air.** Press A on ONE board only. The other must stay on its peer
list, show nothing and start nothing; the first must reach "CONEXIÓN PERDIDA" after about nine
seconds. This is the half no host binary can see — that an unbound device's receive callback
really does drop the initiator's unicast (`espnow_stats().rx_wrong_peer` rising on the silent
board). *Closes: nothing on its own; it is the precondition for B3-B6.*

**B3. §67 "Local multiplayer works".** Press A on both, fight a whole battle. Both panels must
show the same round numbers, the same HP pair and complementary outcomes — one "¡GANASTE!", one
"Has perdido" — and exactly one device toasts the XP line.

**B4. The nine-second move clock, felt rather than measured.** Have one player wait through a
round. Both boards must reach the neutral "Enlace interrumpido" and NEITHER may pay. If nine
seconds is too short in play, the remedy is `PROTO_RETX_MAX` / `PROTO_RETX_MS` in
`networking/session.h` and the cost is that every other wait in the protocol lengthens with it —
decide it in writing.

**B5. The radio after a linked battle.** Leave with LONG_BOTH and confirm on `baseline`'s DIAG
RADIO page that the radio is off and the ladder is no longer clamped at DIM. Failure mode: a
battery draining in a pocket. *Also closes half of §67 "Wi-Fi shuts down after use".*

**B6. §67 "Trade is atomic" — the cross-pair half.** Trade between the two boards, then do it
again and **pull the battery on one board between the two A presses**. After both reboot, each
Box holds either the outgoing Pebble or the incoming one — never both, never neither. The
within-device half is swept exhaustively on the host (every flash write, ten per committed trade,
plus nine points inside the resolver); two parties over a lossy link with no third party cannot
make an exchange atomic, so this run is the residual and it is what the box waits for.

**B7. §67 "Breeding compatibility works" and "Generated Pebbles remain balanced".** These need
`networking/breed_link.cpp` **built and `DISC_CAP_BREED` claimed**, which no phase did. The
compat matrix is swept over all roster pairs on the host and the 10,000-pair gene ceiling is
measured; neither is a sentence about two Pebblebols. **These two boxes are not merely unrun —
they are unbuildable today**, and that is the honest state of them.

---

## C. Radio and power — one board

**C1. §67 "Wi-Fi scanning works".** NETWORK -> scan -> a real access point in the list ->
encounter, within about 5 s. Every scan in every host test came from a fake `WifiScanDriver`; no
radio has ever returned an access point in this repository.

**C2. §67 "Wi-Fi shuts down after use".** After C1 and after B5, confirm the radio is down on
DIAG RADIO (`baseline`) on every exit path: done, failed, timed out, cancelled. The job releases
it exactly once by construction and six host cases prove that against a fake driver; nobody has
watched an antenna.

**C3. §67 "Device sleeps correctly".** Leave the board untouched and watch the DIAG ENERGIA page
walk ACT -> DIM -> IDL -> SLP. **Acceptance number: loop rate in IDLE <= 10/s.** Then press a
button and confirm the wake is counted as activity and the ladder climbs straight back. Note the
deepest rung is a **light** sleep, because `PIN_BTN_L` is outside the C3's deep-sleep wake domain
— D1's consequences carry the table. Also read the ENERGIA page's `stall <n> x <s>s` row (the
DROPPED-TICK counters, not the `stall` serial command): a healthy board reads `stall 0 x 0s` for
its whole run, because `PWR_SLEEP_SLICE_MS` (8,000) is below `NT_TICK_MAX_OWED_S`.

**C4. The uncalibrated cooldown table across a sleep.** While `CAL_UNSET`, cooldowns live in
`.bss` on `millis()`. A light-sleep wake re-runs `setup()`, so ten idle minutes free all 32
networks with no keystroke. Confirm whether that reproduces; the three ways out are written in
the P6 carry-forward.

---

## D. The phone — spec §67, the creator block

One command does most of it: `tools/creator_smoke.sh --variant release --pin NNNN`. It refuses to
run without `--variant` on purpose.

**D1. §67 "PIN required".** Phase 3 of the smoke script: five failures, the sixth refused as
locked, the lockout refusing the CORRECT PIN as well, and the counter cleared afterwards so the
device is not left armed. Since P8-C6 it also sends an **unauthenticated POST with a valid body**
(403, and the Box count must not move), which is the case nothing in the repository had ever
exercised.

**D2. §67 "Wi-Fi activates only when necessary" and "Inactivity timeout works".** Phase 4 polls
the one ungated route every 15 s for the whole `creator_idle_s` budget and asserts both halves —
still serving at `creator_idle_s − 60`, gone by `creator_idle_s + 60` — and the operator confirms
the screen has left CREATOR with it.

**D3. §67 "QR connection works".** Point a phone camera at the CREATOR screen's QR. It is a 62 px
symbol on a 128x64 panel; the encoder is validated module-by-module against the Python `qrcode`
reference on the host and the drawn symbol is pinned by a golden. None of that is a phone.

**D4. §67 "Mobile editor works".** The page is driven end to end in headless Chromium at
390x844 by `tools/page_test.mjs` (51 assertions, inside `tools/check.sh`). A desktop browser has
one exact pixel of contact, no palm, no glove, no sunlight, no one-handed reach and no on-screen
keyboard eating half the viewport. This box is a thumb on glass.

**D5. §67 "Sprite editor works".** Same, with one specific thing to look at: at 24 cells across a
390 px phone a grid cell is about 16 CSS px where every button on the page is 44. The geometry is
`CustomSpeciesRec`'s and cannot change; the mitigations are the full-width grid, the live `x , y`
readout under the thumb, and DESHACER as a first-class tool. Whether that is enough is what this
box asks.

---

## E. The serial console, on the first board

`dev/godmode.cpp`'s shell is executed by **no host test**, and no gate can fix that: the gates
check presence and call sites, which is the half a grep can honestly claim. The line reader, the
field filling, `run_deferred()`, `shell_commit()` and the SYS drawing are unexecuted by any binary
in this repository.

**E1.** On `release`, at 115200, type `help`, `info`, `show_save`, `stall 100`. All four must
answer, and `help` must list only the four and say `GOD_MODE_ENABLED 0` is why the rest are
absent.

**E2.** On `baseline`, type `spawn 1 5` then `show_save`. The GOD bar must appear on the first
mutating command and the spawned Pebble must show the taint ribbon.

**E3.** `info`'s twelve §49 fields: confirm **battery reads `n/a`** (there is no `PIN_BATT`, no
divider fitted and no ADC read anywhere — D10 must be decided and a divider fitted before any code
is worth writing) and **BLE reads `none (removed in P8-C0, decision D2)`**.

---

## F. What is measured on the host and named here so it is not measured twice

| Claim | Where it is checked | What that does not cover |
|---|---|---|
| A screen render allocates nothing | `tests/test_screens.cpp`, 65 snapshots | the device allocator; the four subsystems that use it |
| A day of the pure loop allocates nothing | `tests/test_soak.cpp` | the same, plus NVS wear |
| Every state and every radio wait has a timeout and a user-visible exit | `tests/test_statemachine.cpp`, all 27 rows and all 3 ERROR kinds | two exemptions, both written into the table: ERRK_DISPLAY's exit is a retry that may never succeed, and "nothing navigates to the four never-resident rows" is a `check.sh` grep because no host binary compiles `ui.cpp` or `app.cpp` |
| The sound setting persists to the piezo | `tests/test_sound.cpp` through the real `save_manager` | that a piezo is fitted and audible |
| The frame/pass arithmetic, the wrap, the scheduler deadline | `tests/test_perf.cpp` | every microsecond |
| Wi-Fi never associates | `tools/check.sh` counts `WiFi.begin(` at zero | — |

---

## G. Recording a run

Commit the capture to `docs/bench/<date>-<sha>.log` and add one row per item to the table below,
with the **variant**, the **firmware version** (`info` prints it) and the reading. An item with no
row has not been run. A row taken on `baseline` may not be used to tick a box §67 asks about the
product.

| Item | Date | SHA | Variant | Reading | Verdict |
|---|---|---|---|---|---|
| *(none yet — no board exists)* | | | | | |
