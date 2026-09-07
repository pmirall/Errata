# Pebblebol — the bench list

**What this file is.** Everything in the product that this repository cannot check, in the order
one person should run it, with two boards, a phone and a serial terminal. Nothing here is a
to-do: every item is a claim the firmware makes that no host binary, no gate and no browser
harness can settle, because settling it needs hardware.

**What this file is not.** It is not a summary of what is tested. `tools/check.sh` is green on 58
host binaries, an AddressSanitizer subset of 8, 51 browser assertions and 159 named gates, and
none of that is evidence for a single line below.

**Why it exists as one document.** These items were spread across seven phase records, and a list
distributed over seven records is a list nobody runs. Written by P10-C2; sections F (first boot)
and G (the save) added by P10-C4 and P10-C5; C6, C7 and B8 added at the P10-C6 exit. **This is
the file the release hands to whoever has the hardware.** `README.md` §2 carries the same list in
dependency order, one line per item, and is where an owner should start; this file has the
keystrokes and the acceptance numbers.

**THREE ITEMS HERE COULD NOT HAVE PASSED BEFORE THE P10-C6 EXIT, AND THAT IS WORTH KNOWING
BEFORE YOU SPEND A DAY ON THEM.** B1-B6 were dead for any device whose owner typed an accented
name at first boot: the beacon carried the drawn (UTF-8) form of the name into a field that is
Latin-1 by wire contract, `disc_encode()` refused it, and the board emitted **no beacon at all** -
silently, since an unencodable beacon is dropped by design. D2 and D4/D5 were dead for everyone:
the power ladder tore the access point down after 120 s of no BUTTON press, which is exactly what
using a phone looks like, 180 s before D7's own timer. Both are fixed and both now fail by name
in the host suite if they come back.

---

## 0. Before anything

| | |
|---|---|
| **Hardware** | 2 x ESP32-C3 wired to the map decision **D1** settles on (`Pebblebol/src/core/config.h` §2 today; `docs/legacy/README.es.md` §2 documents the other, contradicting one. **Neither spec has a pin map** - §68 is twenty prose rules for the coding agent and the hardware spec ends at §33 - so an earlier version of this line pointing at "the §68 pin map" pointed at nothing), 2 x SSD1306/SH1106 128x64 on I2C 0x3C, 2 x piezo on `PIN_PIEZO`, one phone with a camera and a browser, one USB serial console at 115200. |
| **Blocked on** | **decision D1 (the pin map) is still open and `PB_PINS_CONFIRMED` is never defined.** Nothing below can run until a board is wired to a decided map. This is the first thing the owner owes, and it gates all **thirty-eight** items. |
| **Variants** | `tools/build_matrix.sh` builds six. Only two matter here: **`release`** (`GOD_MODE_ENABLED=0`) is the artefact that ships and the one every acceptance claim must be made about; **`baseline`** has the god console and is the only build with a DIAG screen at all. **Every item below names its variant, or inherits one from its section heading, and where a
reading only exists on a console page the item ALSO gives the release-side cross-check that
section I's recording rule requires. A reading taken on the wrong variant is not a reading** — `docs/budget.md` §8 records a phase-7 exit that quoted a dev build against a shipping one. |
| **The serial line** | The release artefact prints `DIAG#,heap`, `DIAG#,perf` and `DIAG#,link` headers at boot, plus a one-off `DIAG#,screens` line mapping every `ScreenId` to its name (added at P10-C6: the perf rows name the worst screen as a raw integer, and that enum was renumbered mid-list inside phase 10, so a capture without the map is not interpretable a phase later). One `heap` and one `perf` row every 60 s (`GOD_HEAP_PERIOD_MS`), for ever, on every screen, with no console and no god mode. It also answers four typed commands: `help`, `info`, `show_save`, `stall <ms>`. That is the whole of the shipping build's diagnostics and it is what most of section A is read from. |

Capture every session — **the `mkdir` is not optional**, `docs/bench/` is not in the repository
and `tee` exits 1 without it, which on a 24 h soak is a lost day:

```sh
mkdir -p docs/bench
arduino-cli monitor -p <port> -c baudrate=115200 \
  | tee docs/bench/$(date +%F)-$(git rev-parse --short HEAD).log
```

**Buttons are L and R** everywhere below, matching `PIN_BTN_L` / `PIN_BTN_R` and `README.md` §8.
Some strings on the device say "A" for the left button and "B" for the right; which physical
button is which is part of what decision D1 settles.

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

**B2. The consent gate, on the air. — either variant; the counters ship in both.** Press L on ONE
board only. The other must stay on its peer list: no session opens, nothing binds, no screen
changes. **The instrument is `DIAG,link` on the silent board's serial line** — a row every 60 s
whose `rx_wrong_peer` column counts unicast frames from a device it is not talking to. Until the
P10-C6 exit this item named `espnow_stats().rx_wrong_peer` with **no way to read it**:
`espnow_stats()` was declared, defined, and called by no screen, no page, no serial line and no
test, which is the dead-export shape this phase recorded three times. The line is silent on a
board whose radio has never come up, so a soak capture is not padded with zeros.

Read both halves: `rx_wrong_peer` rising on the silent board proves the frame ARRIVED and was
refused, which is what distinguishes a working consent gate from a packet that never came. If the
column stays at 0 while the pressing board's `tx_ok` climbs, the frames are going somewhere else -
check the channel before concluding anything about consent.

**B3. §67 "Local multiplayer works". — variant `release`, both boards.** Press A on both, fight a whole battle. Both panels must
show the same round numbers, the same HP pair and complementary outcomes — one "¡GANASTE!", one
"Has perdido" — and exactly one device toasts the XP line.

**B4. The nine-second move clock, felt rather than measured. — variant `release`.** Have one player wait through a
round. Both boards must reach the neutral "Enlace interrumpido" and NEITHER may pay. If nine
seconds is too short in play, the remedy is `PROTO_RETX_MAX` / `PROTO_RETX_MS` in
`networking/session.h` and the cost is that every other wait in the protocol lengthens with it —
decide it in writing.

**B5. The radio after a linked battle. — variant `release` for the box, `baseline` to see the
ladder.** Leave with LONG_BOTH. On `release`, `info`'s Wi-Fi field must read `mode=0` with `ap=-`;
that is the reading section I lets you tick "Wi-Fi shuts down after use" from. On `baseline` you
can additionally confirm on the DIAG RADIO page that the ladder is no longer clamped at DIM. Failure mode: a
battery draining in a pocket. *Also closes half of §67 "Wi-Fi shuts down after use".*

**B6. §67 "Trade is atomic" — the cross-pair half. — variant `release`, both boards.** Trade between the two boards, then do it
again and **pull the battery on one board between the two A presses**. After both reboot, each
Box holds either the outgoing Pebble or the incoming one — never both, never neither. The
within-device half is swept exhaustively on the host (every flash write, ten per committed trade,
plus nine points inside the resolver); two parties over a lossy link with no third party cannot
make an exchange atomic, so this run is the residual and it is what the box waits for.

**B7. §67 "Breeding compatibility works" and "Generated Pebbles remain balanced". — no variant:
nothing to run.** These need
`networking/breed_link.cpp` **built and `DISC_CAP_BREED` claimed**, which no phase did. The
compat matrix is swept over all roster pairs on the host and the 10,000-pair gene ceiling is
measured; neither is a sentence about two Pebblebols. **These two boxes are not merely unrun —
they are unbuildable today**, and that is the honest state of them.

**B8. A beacon goes out whatever the device is called. ADDED AT THE P10-C6 EXIT, because until
that commit B1-B6 could not have passed for a large class of owners and nothing would have said
why.** `DiscBeacon.name` is Latin-1 by wire contract and `ui_pet_name()` had been emitting UTF-8
since P10-C4, so every accented character the first-boot naming ring can type became a byte
`disc_encode()` refuses. The board emitted **no beacon at all** - and `link_service()` drops an
unencodable beacon by design, so `beacons_tx` stayed 0 and the LINK screen simply searched for
ever. A device called `ÑU` was invisible to every peer, and because the peer never saw it the
link could not be offered from either side either.

Name one board with an accent at first boot (F2 already has you do this), put both on LINK, and
confirm the accented one appears in the other's list **with its accent drawn correctly**. Then
swap: name the second board `ÁLEX` and repeat. On `baseline`, `show_save` prints the stored name
so you can compare what was typed, what is stored and what is drawn. Failure mode if it returns:
`DIAG,link`'s `beacons_tx` column stays at 0 on the accented board.

---

## C. Radio and power — one board

All on **`release`** unless an item says otherwise, because every §67 box in this section is a
claim about the artefact that ships. Where a reading only exists on a console page, the item says
so AND gives the release-side cross-check, per section I's rule.

**C1. §67 "Wi-Fi scanning works". — variant `release`.** NETWORK -> scan -> a real access point in the list ->
encounter, within about 5 s. Every scan in every host test came from a fake `WifiScanDriver`; no
radio has ever returned an access point in this repository.

**C2. §67 "Wi-Fi shuts down after use". — variant `release` for the box, `baseline` only to see
more.** THE READING THAT CLOSES THE BOX IS ON `release`: type `info` on the serial line and read
the Wi-Fi field, which prints `mode=… phase=… err=… ap=…` and is one of the four always-compiled
commands (`dev/diag_core.cpp` static_asserts that they cannot mutate anything). `mode=0` with
`ap=-` is the radio down. Section I forbids ticking a §67 box from a `baseline` row and this item
used to have no release-side reading at all, which made it a closed loop. On `baseline` you can
additionally confirm on
DIAG RADIO (`baseline`) on every exit path: done, failed, timed out, cancelled. The job releases
it exactly once by construction and six host cases prove that against a fake driver; nobody has
watched an antenna.

**C3. §67 "Device sleeps correctly". — variant `baseline` for the loop counter, `release` for the
box.** The ladder's rungs are only drawn on the console page, so read those on `baseline`; the
§67 box itself is closed on `release` by the `DIAG,perf` line, whose `passes` column stops
climbing at the sleeping rate and whose `pass_worst_us` must NOT jump to the sleep slice (that
would be the `perf_note_pass()` ordering defect, which `tools/check.sh` also gates by line
number). On `baseline`, watch the DIAG ENERGIA page
walk ACT -> DIM -> IDL -> SLP. **Acceptance number: loop rate in IDLE <= 10/s.** Then press a
button and confirm the wake is counted as activity and the ladder climbs straight back. Note the
deepest rung is a **light** sleep, because `PIN_BTN_L` is outside the C3's deep-sleep wake domain
— D1's consequences carry the table. Also read the ENERGIA page's `stall <n> x <s>s` row (the
DROPPED-TICK counters, not the `stall` serial command): a healthy board reads `stall 0 x 0s` for
its whole run, because `PWR_SLEEP_SLICE_MS` (8,000) is below `NT_TICK_MAX_OWED_S`.

**C4. The uncalibrated cooldown table across a sleep. — variant `release`.** While `CAL_UNSET`, cooldowns live in
`.bss` on `millis()`. A light-sleep wake re-runs `setup()`, so ten idle minutes free all 32
networks with no keystroke. Confirm whether that reproduces; the three ways out are written in
the P6 carry-forward.

**C5. §67 "No obvious memory leak" — the 24 h soak. — variant `release`. ADDED AT P10-C5, because this was the one
§67 box in the product with no owner step anywhere.** It is the oldest owed item in the file:
P2-C12 named it, P10-C2 named it again and said plainly that what it could run was one whole
SIMULATED day of the pure loop with the allocation counter armed and requiring zero — a different
claim from a device that has been powered for a day.

`release`, one board, mains USB, left alone for 24 h with the serial line captured:

```bash
arduino-cli monitor -p /dev/ttyACM0 -c baudrate=115200 \
  | tee docs/bench/$(date +%F)-soak.log
```

The artefact prints `DIAG,heap,<uptime_s>,<free_b>,<min_free_b>` every 60 s on every screen, with
no console and no god mode, and that format has been stable since P2-C12 precisely so a capture
can be parsed years later. **Acceptance: `free_b` at hour 24 is within a few hundred bytes of
`free_b` at hour 1, and `min_free_b` has stopped falling.** A slope is the finding; the absolute
number is not.

Run it twice if you can: once on HOME (the animated layer, `ui/petfx.cpp`, which no host binary
compiles) and once left on the CREATOR screen with the radio up, because the two exercise
completely different allocators. The screen the board was left on goes in the log.

---

**C6. §67 §64 — THE SOUND. ADDED AT THE P10-C6 EXIT, because spec §64 had no bench item at all
and the closed decision log sent the owner to two sections that do not mention audio.** — variant
`release`, and it needs the piezo decision **D8** settled first.

`hardware/audio.{h,cpp}` has shipped since P6-C1: a seven-effect tone engine behind a
two-function-pointer sink, driven note by note on the host by `tests/test_sound.cpp`. **Nothing
has ever been heard.** D8 (which pin the piezo is on) is open, so this item is blocked with D1.

Trigger each of the seven and confirm each is audible and distinguishable from its neighbours:
feed (CARE → *Comida*), clean (CARE → LIMPIAR), a hit and a faint (a practice battle from the
BATTLE screen), a protect beat (needs a species that knows one — species 1 does not, which is why
this effect went five phases without a picture either), a level-up (use a *Megadulce* from the
bag), and an error tone (a refused care action — feed a full Pebble). Then SETTINGS → *Sonido* →
off, **power-cycle**, and confirm the piezo is silent and stays silent. The persistence half is
host-proved through the real `save_manager` into the fake NVS; the audible half is this item.

Acceptance: seven distinguishable sounds, and the setting survives a reboot in both directions.
A board with no buzzer fitted must run the whole game unchanged — `audio_device_sink()` answers
`nullptr` and the engine no-ops, which is host-driven but not the same as a board with an empty
pad.

**C7. The reset reason really becomes the right `BootKind`. ADDED AT THE P10-C6 EXIT, REWRITTEN AT
THE FINAL REVIEW.** — variant **`baseline`**, and it costs **two** reboots.

Whether a reboot charges elapsed game time to the pet is decided by
`hardware/boot_reason.h`'s table, and until P10-C6 **nothing anywhere had ever turned a real
`esp_reset_reason()` into a `BootKind`**: `hardware/boot.cpp` is compiled by no host binary and
the fake that stood in for it collapsed seven kinds to two. The table is pure and swept over all
256 reason values now, and the ESP-IDF mapping is asserted at compile time — but a `BootKind`
produced by a real reset has still never existed. `boot.cpp`'s own header names the historical
instance: v1 folded `ESP_RST_DEEPSLEEP` into `BOOT_SOFT_RESET` and lost every sleep.

**THREE THINGS THE FINAL REVIEW MEASURED, WHICH CHANGE THIS ITEM. Read them before you spend the
time.**

**(a) `BootKind` 4 (DEEPSLEEP) IS UNREACHABLE ON THIS ARTEFACT, so the old step 3 could never
produce a third number.** `grep -rn 'esp_deep_sleep_start' Pebblebol/src` returns NOTHING.
`hardware/power.cpp` calls `esp_light_sleep_start()`, and `hardware/power.h` says in its own words
why a deep rung is deliberately not built ("a DEEP sleep does not resync `esp_timer`, so a deep rung
would take that clock back to zero on every wake"). **A light-sleep wake is not a reset**:
`esp_reset_reason()` is never re-read, `app_setup()` never re-runs, and the `[nt] boot=` line is
printed once per boot and never again. The old step 3 asked the operator to sit through ten
untouched minutes waiting for a serial line the firmware cannot print, and then fail an acceptance
criterion it is architecturally incapable of meeting.

**(b) A RESET-PIN PRESS READS 1, NOT 3, AND THE BARS WILL MOVE.** The ESP32-C3 ROM's `RESET_REASON`
enum has no external-pin value — `esp_system.h` documents `ESP_RST_EXT` as not applicable — so the
pin produces `POWERON_RESET`; `boot_classify()` sees `BR_POWERON` and answers `BOOT_POWER_LOSS` = 1,
and the pin reset also clears RTC fast memory so `s_intact` is false as well. **If you read 1 after
a reset-pin press, the table is right and the silicon cannot tell you otherwise.** `boot=3` comes
from a SOFTWARE restart, which is why step 2 below asks for one.

**(c) ON A CALIBRATED BOARD THE CLASSIFIER IS NEVER CONSULTED.** `app/app.cpp`'s `boot_absence()`
reads `boot_kind()` only inside `if (!known)`, and `known = (gt_cal_state() != CAL_UNSET)`. A board
that has been through §F's date screen is calibrated, so the "bars moved / bars did not move" half
measures `gt_elapsed_since()`, not `boot_classify()`. **To exercise the classifier, use an
uncalibrated board — the one item 21 / F5 produces by skipping the date question.**

The artefact prints `[nt] boot=<kind> nvs=<n> load=<r>` on every boot, above every guard
(`BootKind`: 0 FIRST_RUN, 1 POWER_LOSS, 2 CRASH, 3 SOFT_RESET, 4 DEEPSLEEP — unreachable, see (a) —
5 UNKNOWN). **Do it on `baseline`**, where god mode's DIAG SYS page prints `boot=%u rst=%u n=%lu` and
you can re-read it at leisure: on `release` the number appears exactly once, at the end of
`app_setup()`, on a USB link that does not survive an unplug, and it is flushed out of HWCDC's
256 B TX ring by the first DIAG rows within about two minutes.

1. **Unplug and replug** after five minutes → expect `boot=1`. On an UNCALIBRATED board the pet's
   care bars must have moved by five minutes' worth. This is the one that matters: if it reads 3,
   no absence is ever charged and the pet stops ageing while the device is off.
2. **A software restart** — god mode's `reboot`, or `esptool run` — → expect `boot=3`, and the
   bars must NOT jump. (A reset-pin press is step 1 again on this silicon; see (b).)

Acceptance: **two** different numbers, each matching the list, and care moving on 1 and not on 2.
Record them in the capture; it closes the last measurable link in §67's "Time-based calculations
work across reboot".

**What this item can no longer claim, and where the rest went.** The reason-to-`BootKind` mapping
itself is pure and swept over all 256 values by `tests/test_clock.cpp`, and
`tests/test_game_state.cpp` now holds the rearm/taint half that the host fake used to answer wrongly.
What is left here — and it genuinely needs a board — is that a real unplug loses RTC fast memory and
that a real software restart does not.

---

## D. The phone — spec §67, the creator block

All on **`release`** unless an item says otherwise. **`tools/creator_smoke.sh` takes `--variant`
as a LABEL for the record; it does not build or flash anything** — flash the board yourself first.

> **THE WHOLE OF THIS SECTION WAS UNRUNNABLE UNTIL THE P10-C6 EXIT AND IT WOULD HAVE LOOKED LIKE
> A FIRMWARE FAULT.** `ui_radio_job_busy()` — the power ladder's `held` input — named the scan
> job and the link job and not the portal, so after 120 s with no BUTTON pressed the ladder
> reached `PWR_IDLE`, `pwr_hook_release()` navigated home, and `creator_leave()` took the access
> point down. Using a phone is precisely what "no button pressed" looks like. D2 polls for
> `IDLE_S + 60` = 360 s without touching the device and would have watched the portal die at 120;
> D4 and D5 ask you to draw a 24x24 sprite, which takes longer than two minutes. Fixed, and
> `tests/test_screens.cpp` now fails by name if the hold is removed.

One command does most of it: `tools/creator_smoke.sh --variant release --pin NNNN`. It refuses to
run without `--variant` on purpose.

**D1. §67 "PIN required". — variant `release`.** Phase 3 of the smoke script: five failures, the sixth refused as
locked, the lockout refusing the CORRECT PIN as well, and the counter cleared afterwards so the
device is not left armed. Since P8-C6 it also sends an **unauthenticated POST with a valid body**
(403, and the Box count must not move), which is the case nothing in the repository had ever
exercised.

**D2. §67 "Wi-Fi activates only when necessary" and "Inactivity timeout works". — variant
`release`.** Phase 4 polls
the one ungated route every 15 s for the whole `creator_idle_s` budget and asserts both halves —
still serving at `creator_idle_s − 60`, gone by `creator_idle_s + 60` — and the operator confirms
the screen has left CREATOR with it.

**D3. §67 "QR connection works". — variant `release`.** Point a phone camera at the CREATOR screen's QR. It is a 62 px
symbol on a 128x64 panel; the encoder is validated module-by-module against the Python `qrcode`
reference on the host and the drawn symbol is pinned by a golden. None of that is a phone.

**D4. §67 "Mobile editor works". — variant `release`.** The page is driven end to end in headless Chromium at
390x844 by `tools/page_test.mjs` (51 assertions, inside `tools/check.sh`). A desktop browser has
one exact pixel of contact, no palm, no glove, no sunlight, no one-handed reach and no on-screen
keyboard eating half the viewport. This box is a thumb on glass.

**D5. §67 "Sprite editor works". — variant `release`.** Same, with one specific thing to look at: at 24 cells across a
390 px phone a grid cell is about 16 CSS px where every button on the page is 44. The geometry is
`CustomSpeciesRec`'s and cannot change; the mitigations are the full-width grid, the live `x , y`
readout under the thumb, and DESHACER as a first-class tool. Whether that is enough is what this
box asks.

**D6. §67 "Device-side validation works" — ADDED AT P10-C5, because it was the one §67 box in the
file with no item of its own.** The rules are the strongest-held thing in this repository: one
validator, 27 named reject codes, driven over the whole parse pipeline by
`tests/test_creator_api.cpp` **under AddressSanitizer on every commit**, and end to end from a
real browser by `tools/page_test.mjs`. None of that is a socket. `networking/creator_server.cpp`
is compiled by no host binary, so **the transport half is held by four greps and by this item.**

Phases 1 and 2 of `tools/creator_smoke.sh --variant release --pin NNNN` are the instrument:
a well-formed creature accepted; a body over the raw cap refused with 413 **including on an
UNMATCHED path**, which must answer 413 rather than 404; truncated JSON, a stat total over
budget, an unknown attack id and a name with an illegal byte each refused with **their own named
code**, not a generic 400. Read the codes off the responses and check them against
`game/validate.h`: a validator that answers `VR_OK` to one of these, or the same code to all of
them, is the failure this box is about. **And the Box count must not move** across the whole
phase.

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

## F. First boot, on a board that has never been switched on — spec §65 (P10-C4)

> **The items in this section were labelled `H1`..`H6` until P10-C5 and are `F1`..`F6`
> now.** The section was drafted as H and renumbered to F when it landed; the item
> prefixes did not move with it. Nothing outside this file cited them, which is exactly
> why it went unnoticed - and `README.md` cites F4 now, so it would not have again.
> **Two of the renamed references were still `H1` and `H2` in this very paragraph's own
> section until the P10-C6 exit**, which is the same defect one level down: a correction
> that did not correct itself.

Everything below **F1** can be checked on the host and is (`tests/test_onboarding.cpp` drives the
step through the real save pipeline; `tests/test_screens.cpp` drives both screens, all six
gestures and the goldens). What cannot be checked here is the only thing that matters about an
onboarding flow: **whether a person who has never seen the device can get through it.** That
needs a person, a board and no explanation.

**F1. A genuinely fresh board.** `esptool erase_flash`, then flash `release`. Expected, in order:
splash → "Cargando la partida..." → **the intro** → **ELIGE PEBBLE**. Not a toast over it: the
greeting is drawn on the screen itself (`STR_SU_HELLO`, on the naming screen, which is the SECOND
question now), because the toast band is rows 45–55 and both instruction lines live there. **If a
toast covers the bottom two lines, that is the defect P10-C4 fixed coming back.**

**F0. THE INTRO, AND IT IS THE ONE ITEM THIS WHOLE SECTION EXISTS FOR NOW. — variant `release`.**
It is the first sixteen seconds of the product and the only part of it a host golden cannot judge,
because `tests/fakes/gfx_fb.cpp` draws no real glyphs — the typed listing is a stack of bars in
every golden of it, and whether `pebble_t nuevo(void) {` is *legible at 4x6 on a 0.96" panel* is a
question only the panel answers. Watch it end to end, once, without touching a button:

  1. The listing types itself, left to right, one character at a time, with a caret after the last
     one and a click roughly every two characters. **Every line must fit the panel width.** A line
     that runs off the right edge is the `static_assert` on `GFX_ADV_TINY` having been defeated by
     a font change.
  2. The header reads `pebble.c`, then gains **COMPILANDO** and a bar. The bar must **stop short of
     full** and stay there — a bar that reaches 100 % and then reports a failure lied about its
     last frame.
  3. `ERROR: 3 BUGS`, and the band tears into jumping scanlines. The tear must **jump**, not slide.
  4. Three bugs climb out, one at a time, about a second apart, each with its own chirp.
  5. **THE CUT.** When the intro ends, the three bodies must not move by one pixel: the only things
     that appear are the selection frame, the species name and the hint. `tests/test_screens.cpp`
     asserts this on the host, so a visible jump here means the panel and the fake disagree about
     `gfx_xbm_t()` — the P10-C3 seam again.
  6. Total elapsed, splash to ELIGE PEBBLE: **under 25 s.** Time it. The whole budget is "cinematic
     plus three questions under two minutes" and the questions are the player's to pace.

Then repeat and **press a button in the middle**: the intro must stop immediately, the picker must
be on the FIRST creature (the press may not have spun the cursor), and nothing may have been
chosen.

**F2. Type a name with an accent in it. — variant `release`. See also B8, which is the same name
on the air, and which was broken until the P10-C6 exit.** Walk the ring to `Ñ` and accept. Then read the name back
on HOME and on the creator portal (`GET /api/state`). All three must show the same character. A
name that renders as one wrong glyph, or that loses the character *after* the accent, is the
Latin-1/UTF-8 seam (`core/utf8.h`) failing on the panel's own decoder — which is the half no host
test can see, because the host fake is not u8g2.

**F3. The starter, then the name, then the date. — variant `release`.** THE ORDER CHANGED: the
picker is FIRST now. Pick the third creature — HOLD L must go **forward** to PONLE NOMBRE, not
back — then type the name (F2), then the date. HOLD L on the date must go **forward to HOME**. On
HOME the Pebble must be the third creature, at level 1, with the name from F2.

**F4. THE POWER CUT, and this is the item worth the trip. — variant `release`.** Repeat F1, pick a
starter, and **pull the power while the naming screen is up**. On the next boot the device must
come back **on the naming screen with the starter already minted** — not at ELIGE PEBBLE, not on
HOME, and **without replaying the intro**: the cinematic is armed only on `BOOT_FIRST_RUN`, and a
resumed flow that plays it again is sixteen seconds charged to somebody whose battery died. Repeat
with the cut after the name is accepted: it must come back on the date screen. Then finish the
flow, power cycle twice more, and confirm **no setup screen is ever shown again**.

**F5. The player who reads nothing.** From a fresh board, skip the intro with one press, then hold
both buttons on the first screen.
The device must land on HOME with a working Pebble (species 1, the historical starter), no name,
and the clock unset — and it must never ask again.

**F6. A board that has been played.** Flash `release` over a device that already has a save from an
earlier firmware. It must go straight to HOME. Being handed a setup wizard is the failure mode the
`OB_DONE == 0` encoding exists to prevent, and it is the one that would look like data loss.

---

### F7. THE SCREEN TRACE, and read it before anything else in this file.
**— variant `release` or `baseline`; it is in both.**

Open the serial monitor at 115200 and drive the device. Every screen change prints one line:

```
DIAG,scr,<millis>,<ordinal>=<NAME>
```

The ordinals are printed once at boot in the `DIAG#,screens` header, so a capture is
self-describing and cannot rot. **The GAP between two rows is what it is for.** A push
followed 20 s later by `2=HOME` is the auto-return doing its job on a screen nobody
noticed; a push followed *immediately* by `2=HOME` is a navigation defect. Those two
need opposite fixes and are indistinguishable from the sofa - the first hardware session
spent an hour telling them apart by argument, and this line is what it produced.

The healthy trace for one exploration is exactly this, and anything else is the bug:

```
DIAG,scr,<t>,3=MENU
DIAG,scr,<t>,10=NETWORK
DIAG,scr,<t+4000..12000>,19=ENCOUNTER     <- 4 to 12 s later: the scan
DIAG,scr,<t+...>,20=CAPTURE               <- only if you press A on CAPTURAR
```

---

## G. The save, on a board — spec §31 (P10-C5)

**Variant: `release`.** Three flows the host proves *arithmetically* and cannot prove
*physically*. Every one of them is driven end to end in `tests/test_persistence.cpp` against
`tests/fakes/kv_mem.cpp`, a RAM NVS with fault injection — which is why the rules are known to be
right, and why none of that is a sentence about flash.

**What the host has already proved, so you do not run it twice:** the pair discipline (every
write to the copy a reader would not pick, read back and compared); a load never destroying a
save it could not read; both copies of every blob rotted, one copy rotted, a header/slot
mismatch, a torn write at each of ten points in a trade; the whole v1 -> v2 -> v3 migration
chain; and the factory reset and checkpoint restore below, against `kv_mem`'s erase path.

**What only a board can settle:** that NVS behaves like the fake. Real NVS has wear levelling, a
page allocator that can run out, a partition table that has to have been flashed, and an
`initArduino()` that erases things before `setup()` runs. Nothing in this repository has ever
written a byte to one.

**G1. Factory reset really empties both partitions. — variant `release`.** SETTINGS -> *Reset de fábrica* -> both
confirmations. The device must come back on the first-boot flow (section F), naming, clock and
starter all asked again. Then power-cycle: it must still be a fresh device, not the old one
returning. *The failure mode this looks for is a reset that clears `nvs` and leaves `nvs2`, so
the next boot restores the checkpoint of the creature the owner just deleted* — the two-partition
wipe is one line in `save_factory_reset()` and it is the line that matters.

**G2. Checkpoint restore after a real `nvs` erase. — variant `release`.** This is the flow `nvs2` exists for, and it
is worth doing deliberately because the accident it models is the Arduino core's own:

```bash
esptool.py --chip esp32c3 -p /dev/ttyACM0 erase_region 0x9000 0x5000   # "nvs" only
```

Play for a minute first so there is something to lose (name the device, catch or level something,
change the brightness). Then erase, reboot, and the device must come back with the creature, the
Box and the config **from the last checkpoint** and toast that it recovered — not a fresh
starter. Check `show_save` on the serial line before and after — `save slots=`, `active=` and
`count=` are the three that settle it. *Do not erase `nvs2`
(`0x310000`): that is the copy under test.* Then reboot a second time: the recovered state was
committed back to `nvs`, so the second boot must be ordinary.

**G3. The schema upgrade, on a device that has been played. — variant `release`, twice (the
previous image, then this one).** The one item here that needs two
firmware images, and the one that would have been catastrophic to get wrong:

1. Check out the previous commit (`git log` — the parent of the P10-C5 commit), build `release`,
   flash it, and **play**: name the device, set the clock, catch something, make a creature in the
   creator portal if you have the phone out. That image writes `SAVE_SCHEMA_VERSION` 2.
2. Build the current `release` and flash it **without erasing** (`Erase All Flash Before Sketch
   Upload = Disabled`, which is the default).
3. **Acceptance: the device comes up with everything intact** and toasts *«Partida
   actualizada»* — `LOAD_MIGRATED`. Not SAVE ERROR. The creature made in the creator must still
   be there and must not be quarantined.
4. Power-cycle. The second boot must be **ordinary** — no second «Partida actualizada» — and
   the serial line must agree: `show_save` reports `save schema=3 ... migrated=0`, and `info`'s
   SAVE VERSION field reads `fw=3 onflash=3 migrated=0`. Both are `DCF_ALWAYS`, so they answer on
   `release`.

*Why it is worth the trip.* Every part of this is proved on the host, including the byte-level
one: a whole played save is written, stamped back down to v2 and read by this firmware. What the
host cannot do is write it through a real NVS page allocator, lose power inside the rewrite, and
come back. If step 3 shows SAVE ERROR on a device whose data is intact, the fault is in
`BlobOps::min_version` or in `pair_write()` and `docs/save_schema.md` §8.2 is the account of both.

---

## H. What is measured on the host and named here so it is not measured twice

| Claim | Where it is checked | What that does not cover |
|---|---|---|
| A screen render allocates nothing | `tests/test_screens.cpp`, 65 snapshots | the device allocator; the four subsystems that use it |
| A day of the pure loop allocates nothing | `tests/test_soak.cpp` | the same, plus NVS wear |
| Every state and every radio wait has a timeout and a user-visible exit | `tests/test_statemachine.cpp`, all 27 rows and all 3 ERROR kinds | two exemptions, both written into the table: ERRK_DISPLAY's exit is a retry that may never succeed, and "nothing navigates to the four never-resident rows" is a `check.sh` grep because no host binary compiles `ui.cpp` or `app.cpp` |
| The sound setting persists to the piezo | `tests/test_sound.cpp` through the real `save_manager` | that a piezo is fitted and audible |
| The frame/pass arithmetic, the wrap, the scheduler deadline | `tests/test_perf.cpp` | every microsecond |
| Wi-Fi never associates | `tools/check.sh` counts `WiFi.begin(` at zero | — |
| Every screen draws inside 128x64 at a 12-character multi-byte name, level 30, 100 % stats and a full Box | `tests/test_screens.cpp` `kAudit[]`, one row per `ScreenId`, `static_assert`ed to be complete | the fake's font is fixed-advance, so a golden containing `GF_HEAD` or `GF_BIG` is layout-approximate; and `ui/petfx.cpp`, `ui/actfx.cpp` and `ui/ceremony.cpp` are compiled by no host binary, so HOME's animated layer is absent from every one of these renders |
| Every string in `strings_es.h` fits the banner the toast and HELP strip draw | `tests/test_screens.cpp` | the same font caveat: the device is proportional and the host is 5 px per codepoint, so a string measured at 124 px here can be a few pixels either side there |
| The first-boot step survives a power cut | `tests/test_onboarding.cpp` through the real `save_manager` and the fake NVS | a real power cut mid-write, real NVS wear, and `hardware/boot.cpp`'s reset-reason classification, none of which exist on the host |
| A v2 save is read, carried forward field by field and re-sealed at v3, and the second boot is ordinary | `tests/test_persistence.cpp` §8b, a whole played save stamped down to v2 | that real NVS returns the bytes it was given; a power cut inside the rewrite; the two-image flash of G3 |
| A factory reset empties both partitions and the next load is `LOAD_FRESH` | `tests/test_persistence.cpp`, against `kv_mem`'s erase path | that `kv_wipe()` reaches a real partition, and that `nvs2` goes with `nvs` (G1) |
| A checkpoint restores after `KV_MAIN` is wiped, at v2 or v3, through both entry points | `tests/test_persistence.cpp` | that the `nvs2` partition was actually flashed and that `initArduino()` leaves it alone (G2) |

---

## I. Recording a run

Commit the capture to `docs/bench/<date>-<sha>.log` and add one row per item to the table below,
with the **variant**, the **firmware version** (`info` prints it) and the reading. An item with no
row has not been run. A row taken on `baseline` may not be used to tick a box §67 asks about the
product.

| Item | Date | SHA | Variant | Reading | Verdict |
|---|---|---|---|---|---|
| *(none yet — no board exists)* | | | | | |
