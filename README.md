# Pebblebol

A creature that lives on a coin-sized ESP32-C3 behind a 128×64 monochrome OLED
and two buttons. You feed it, it grows, it fights and trades with a second one
over the air, and it knows how long you left it alone. Sixty species, thirty
levels, a Box that holds ten, a creature editor you reach by scanning a QR code
off the device's own screen, and a save that survives having the power pulled at
any instant.

The user interface is in Spanish. This document, the code and the comments are
in English.

> **Version `1.0.0-rc1`. Nothing in this repository has ever run on hardware.**
> The firmware compiles at zero warnings in six configurations, 58 host test
> binaries pass, and the release image fits its budget with 249 KB of flash to
> spare — none of which is evidence that a board works. **Seventeen of the
> spec's acceptance boxes are open**: fifteen because nobody has watched a
> device do the thing, one because the pin map is undecided, and two because a
> networking module that was planned was never written. The **pin map fails this
> project's own guard**.
> Both are the next two sections, before anything else, because they are what a
> person holding the hardware needs first.

---

## 1. Read this before you solder anything

**The committed pin map is a proposal, it contradicts the older wiring
documentation, and it does not satisfy the assertions this repository wrote for
it.** Decision **D1** is open and has been deferred by the owner
(`docs/decisions.md`).

What `Pebblebol/src/core/config.h` compiles today:

| Signal | GPIO | Note |
|---|---|---|
| `PIN_SDA` | 8 | GPIO8 is a **strapping pin** and the built-in LED on most SuperMini clones |
| `PIN_SCL` | 9 | GPIO9 is a **strapping pin**: low at reset selects the serial download mode |
| `PIN_BTN_L` | 10 | |
| `PIN_BTN_R` | 2 | GPIO2 is a **strapping pin** and must be high at reset |
| `PIN_LED` | 5 | |
| `PIN_PIEZO` | 3 | decision **D8**, also open: the owner confirms the pin when the sounder is soldered |
| `PIN_VBAT_ADC` | 0 | reserved; **no divider is fitted**, so there is no battery reading (decision D10, open) |

`config.h` carries seven `static_assert`s that a confirmed map must satisfy.
They are dormant because `PB_PINS_CONFIRMED` is never defined anywhere. Define
it and the build stops:

```console
$ g++ -DPB_PINS_CONFIRMED -fsyntax-only Pebblebol/src/core/config.h
core/config.h:114: error: static assertion failed: D1: no button on a strapping pin (GPIO2/8/9)
note: the comparison reduces to '(2 != 2)'
```

`PIN_BTN_R` is 2. A button is a thing that can be held low; GPIO2 held low
across the power-on edge puts the chip into a boot mode that is not the one you
want, and GPIO9 held low puts it into the bootloader — a board that looks dead
and is only waiting to be flashed.

The archived Spanish README (`docs/legacy/README.es.md`, §2) documents a
**different** map — SDA 6, SCL 7, BTN_L 3, BTN_R 4, LED 8 — with a wiring
diagram. Neither map has been on a board. `docs/decisions.md` D1 records the
conflict and the owner's deferral.

**So: decide D1 and D8 first, edit the six `#define`s they own (the seventh,
`PIN_VBAT_ADC`, belongs to D10 and there is no divider fitted), define
`PB_PINS_CONFIRMED`, and let the assertions check your map.** Every pin number
in the firmware comes from that one block — `tools/check.sh` fails any other
file that defines a `PIN_` macro, so there is no second place to look.

Non-strapping GPIOs brought out on this board: **1, 3, 4, 5, 6, 7, 10**. Of
those, **3 is the proposed piezo (D8), 5 is the LED and 10 is `PIN_BTN_L`** —
all three are already spoken for by the committed map twelve lines above, so
the genuinely spare pins are **1, 4, 6 and 7**, and `PIN_BTN_R` must move off
GPIO2 into one of them, which is the assertion that fails today. GPIO0 is
reserved
for the battery divider. GPIO11–19 are not brought out.

---

## 2. What has never run on hardware — the bench list, in order

**Eighteen of the spec's §67 acceptance boxes are open.** Sixteen are open
because nobody has watched a device do the thing; one (`Device boots reliably`)
waits on decision **D1** above; and two — breeding compatibility and generated
Pebbles staying balanced — wait on a module that **was never written**, which is
a different sentence and is spelled out at item 29 below. *(Seventeen until the
final review, which UNTICKED "Two-button input is robust": its first named
artefact — P2-C6's 5 ms sampler and edge ring — is behind `#if defined(ARDUINO)`
and is compiled by no host binary, so `nm` over `bin/test_input` finds neither the
ring nor the callback. `test_input` 15/15 is real and is about the FSM.)*

This is the whole list, consolidated from phases 5 to 10 and **ordered so that
each item's preconditions are above it**. `docs/bench.md` has the same items
with the keystrokes, the acceptance numbers and what a green reading does *not*
prove; the bracketed code is its section. Nothing here has been run.

> **THREE THINGS THE P10-C6 EXIT FOUND THAT WOULD HAVE COST AN OWNER A DAY EACH.**
> Items 23-28 (the two-board block) were dead for any device whose owner typed
> an accented name at first boot — the beacon carried the drawn UTF-8 form of
> the name into a field that is Latin-1 by wire contract, `disc_encode()`
> refused it, and the board emitted **no beacon at all**, silently. Items 30-35
> (the phone block) were dead for everyone: the power ladder tore the access
> point down after 120 s with no *button* pressed, which is exactly what using a
> phone looks like, 180 s before the portal's own timer. And item 25's
> `rx_wrong_peer` had **no reader anywhere in the tree**. All three are fixed,
> and all three now fail by name in the host suite if they come back.

> **THE FINAL REVIEW, THE COMMIT BEFORE THE SOLDERING IRON.** Every item below
> that has a known failure mode, or a symptom you would reasonably blame on the
> wrong thing, now carries a **⚠ block** underneath it: what you will see, the
> cause you will try first, the cause it actually is, and the ONE reading that
> separates them. They are there because the review measured them, not because
> they are theoretically possible.
>
> **Four things to do before item 1, in this order.** They cost about ten
> minutes between them and each one saves an afternoon.
>
> 1. **Flash `baseline` first, not `release`.** `release` has no DIAG page, no
>    re-readable `boot=`, `god_frame_heap_*` hardcoded to 0, and no way to build
>    the device state items 22/28/38 need. Prove the panel, both buttons and NVS
>    on `baseline`; then `esptool erase_flash` and flash `release` for the F and
>    G blocks. **Plan for two boards** — one on each — rather than reflashing
>    back and forth; §I forbids ticking a §67 box from a `baseline` reading.
> 2. **The first command is `help`, not `info`.** It is the ONLY always-compiled
>    output that differs between the two artefacts. `info` **cannot** name the
>    variant: `FW_VERSION` is `1.0.0-rc1` on both and `FW_BUILD_STAMP` is
>    `__DATE__ " " __TIME__`, a compile timestamp. If `help` lists `spawn`, you
>    are on `baseline`.
> 3. **Expect to lose the boot banner, and do not debug it.** `Serial` is HWCDC
>    (`CDCOnBoot=cdc`) with a **256-byte TX ring that discards the OLDEST bytes**
>    when the host has not enumerated. `app_setup()` prints ~690 B before
>    returning — the version line, four `DIAG#,` headers and a 29-name ScreenId
>    map — so only the last ~256 B survive. Symptom: a capture that starts
>    mid-word in `DIAG#,screens` with no `DIAG#,heap`/`perf`/`link` headers and
>    no `[nt] boot=`. **Have the monitor already open and then reset the board;
>    do not flash-then-open.** Native USB CDC has no DTR auto-reset, so opening
>    the monitor does not restart the board.
> 4. **Do item 7's three-minute check before you commit a day to item 15.** It
>    costs nothing and it gates items 13 and 15.
>
> **Order that is not the order below.** (a) Item **35** runs BEFORE items 30 and
> 31 — they are phases 1-2, 3 and 4 of ONE `creator_smoke.sh` invocation, and
> running them as three costs three runs, three Box slots and three of the ten
> creator-species slots. (b) Item **28** (trade) needs Box room, so do it before
> any writing smoke run, or pass `--no-write`. (c) Item **14**'s real repro is a
> reboot on the uncalibrated board item **21** produces, so do 21 first.
> (d) Item **38** needs your D1 pin edits applied to the OLD commit before you
> build its image — decide that now, not at step 1 of G3.

### Before anything (blocks all 41 items)

1. **Decide D1 and D8, wire a board, define `PB_PINS_CONFIRMED`.** §1 above.
   Until this, nothing below can run. `[§0]`

   > ⚠ **§1 does not mention D5 (SSD1306 vs SH1106), and `docs/decisions.md`
   > records it as OPEN AT SHIP.** There is no build recipe anywhere in the tree
   > for a shipping SH1106 artefact: `tools/build_matrix.sh`'s `sh1106` row sets
   > `DISPLAY_IS_SH1106=1` and **not** `GOD_MODE_ENABLED=0`, which is why §4's
   > size table shows `sh1106` flash identical to `baseline`.
   > **Symptom** on a 1.3" panel: a coherent picture shifted about 2 px with wrap
   > on both edges, identically on every screen including the splash.
   > **You will blame** the sprite atlas or a framebuffer off-by-two.
   > **It is** the wrong panel driver. **Spell the variant yourself:**
   > `tools/build.sh --variant release --define GOD_MODE_ENABLED=0 --define DISPLAY_IS_SH1106=1 --build-path build/release-sh1106`

2. **`mkdir -p docs/bench`** before the first capture. It is not in the
   repository and `tee` exits 1 without it — on a 24 h soak that is a lost day.
   `[§0]` *(Verified still missing at this commit.)*

### One board, one seat — spec §46's five thresholds

3. Frame time inside `FRAME_BUDGET_US` (50 ms) on every screen. Four screens
   must be visited in a specific state, because their expensive layer is
   conditional. **`baseline`** for the console page, cross-checked against
   `release`'s `DIAG,perf` line. `[A1]`

   > ⚠ **The RENDIMIENTO page's `max` can only ever be SCR_DIAG's own maximum.**
   > It reads `perf_frame_max_us(perf_screen())`, and while you are looking at
   > the page `perf_screen()` IS the page (SCR_DIAG carries `SF_OWNS_FRAME`, so
   > no modal can be over it). There is no gesture that shows another screen's
   > figure, and it rises while you read it, which makes it look live.
   > **Read `f worst <us> s<n>` and `over` instead** — those ARE all-screen.
   > Tell it apart by walking to HOME, sitting ten seconds and re-entering the
   > console: `max` is back at the console's own small number.
   >
   > ⚠ **You cannot sit on a screen long enough to read it.** `PWR_DIM_MS` is
   > 30 s and `PWR_IDLE_MS` is 120 s. **Symptom:** you go to fetch a tape
   > measure and come back to a black panel on HOME. **You will blame** a crash
   > or a dead panel — which is item 18's failure symptom. **It is the ladder.**
   > Press a button: if the panel returns and `DIAG,heap`'s `uptime_s` is still
   > climbing from before, nothing rebooted. Budget every screen visit under 30 s.
   > *(The final review fixed the half of this that navigated AWAY from sticky
   > screens; the dim and the panel-off at 120 s are by design.)*

4. One `loop()` pass inside `PERF_PASS_BUDGET_US` (100 ms), provoked three ways
   — one of them `curl --limit-rate 200` at the creator portal. **`baseline`**.
   `[A2]`

   > ⚠ **The third provocation WILL fail this budget and it is not a defect in
   > this tree.** `WebServer::handleClient()` owns the socket read on the loop
   > thread: `_parseRequest` begins as soon as one byte is available and blocks
   > until the whole request has arrived, bounded by the core's own timeouts
   > (5 s per `readStringUntil`/`readBytes`, 1 s per write `select`). A slow POST
   > puts the entire request inside ONE pass. **Expect `p worst` in the millions
   > and `p over` non-zero, attributed to SCR_CREATOR** — and note it climbs once
   > per HTTP request, so a phone loading the 42 KB page does it before the probe
   > is even run. Record it as the number, do not debug it; the remedy is a
   > bounded body reader, which is a design decision.
   > **Also read `p disc`.** `core/perf.cpp` DISCARDS any pass over
   > `PERF_SANE_MAX_US` (10 s) — it bumps `discards` and never `passes`,
   > `pass_worst` or `pass_over` — so the very worst passes the portal can
   > produce are INVISIBLE in the two figures this item names. A non-zero
   > `discards` means the worst pass was thrown away.

5. Per-frame heap delta 0 on HOME, BATTLE and GAME. **`baseline`, on a board you
   are willing to taint** — the probe only runs while the console is engaged.
   `[A3]`

   > ⚠ **A non-zero `dF` is the Wi-Fi allocator more often than the renderer.**
   > Run the HOME leg with the radio never brought up (never enter NETWORK, LINK
   > or CREATOR since boot) and confirm `info`'s Wi-Fi field reads `mode=0 ap=-`
   > before you start the 60 s sit. If `dF` is 0/0 with the radio down and
   > non-zero after a scan, this probe cannot separate them and A3.5 says so.
   > **This item is skippable if time is short** — the host already proves zero
   > allocations across the screen snapshots and a simulated day, and item 15's
   > `min_free_b` is the better instrument.

6. Input-to-render latency ≤ 2 frames: the `stall` command and a 240 fps phone
   camera. **`release`**. `[A4]`

   > ⚠ **Type `stall` ONE LINE AT A TIME.** Before the final review this function
   > drained the whole serial RX and executed every complete line it found, so a
   > paste or a held Enter was N consecutive busy-waits in ONE pass — one full
   > 256-byte HWCDC ring is 23 lines of `stall 3000`, i.e. **69 seconds** with no
   > render, no input poll and no yield. It is fixed (one command per pass now),
   > but if you ever see a multi-second freeze after a paste, that is the shape.
   > Second-order tell: the console's ENERGIA page would then show a non-zero
   > stall counter where its own comment promises `0 0` for a healthy run — that
   > would be your paste, not a power-ladder bug.

7. §67 **No obvious battery-draining loops** — the loop rate per rung. The
   instrument is built and the number has been *computed and never read*.
   `[C3]`

   > ⚠ **Do this immediately after item 8. It costs three minutes and it gates
   > items 13 and 15.** It is really a binary test of whether
   > `esp_light_sleep_start()` works at all on your board: if the call is
   > REFUSED, `hardware/power.cpp` returns 0 and `app_loop()` spins at full speed
   > with no `delay(1)`. **The reading:** on `release`, two `DIAG,perf` rows 60 s
   > apart with the board untouched should differ by about 60 in the `passes`
   > column at IDLE and about 8 at SLEEP. **A difference in the thousands means
   > light sleep never happened.**

### The serial console (do this first on any board — it tells you which build you flashed)

8. On **`release`**: `help`, `info`, `show_save`, `stall 100`. `help` must say
   `GOD_MODE_ENABLED 0` is why the rest is absent. If it lists `spawn`, you are
   holding the baseline image. `[E1]`
9. On **`baseline`**: `spawn 1 5`, then `show_save`. The GOD bar must appear on
   the first mutating command and the Pebble must carry the taint ribbon.
   `[E2]`
10. `info`'s twelve §49 fields. Battery must read `n/a` (there is no
    `PIN_BATT`, no divider, decision **D10** is open) and BLE `none`. `[E3]`

    > ⚠ **`load=` is a bare integer with no legend on the device or in any
    > document.** `LoadResult`: 0 OK, 1 FRESH, 2 MIGRATED, 3 RECOVERED_PAIR,
    > 4 RECOVERED_CKPT, 5 CORRUPT, 6 FOREIGN_NEWER. The interesting ones are 1 on
    > item 18's fresh board and 3/5 on the corruption items.
    > ⚠ **`wfails=` does not count writes.** `kv_write_fails()` is bumped by every
    > error bit, so a partition that would not open at boot lands in a counter the
    > header calls "short/failed writes since boot" — a board with no nvs2 reads
    > `wfails=1` before anything has been written. *(The final review fixed the
    > separate defect where this field was truncated to 8 bits and printed the
    > true count modulo 256.)*
    > ⚠ **Read `nvs=` as "a write has failed at some point since this boot", not
    > "the store is broken now".** It is sticky and only a wipe clears it.

### Radio and power — one board

11. §67 **Wi-Fi scanning works**: a real access point in the list, then an
    encounter. Every scan in every host test came from a fake driver. `[C1]`
12. §67 **Wi-Fi shuts down after use**: `info`'s Wi-Fi field reads `mode=0`,
    `ap=-` after a scan and after a link. **`release`** — this reading exists on
    the shipping build only since P10-C1, and its counters only since P10-C6.
    `[C2, B5]`

    > ⚠ **THE INSTRUMENT REPORTS INTENT, NOT STATE.** `info`'s `mode=`/`ap=` are
    > `net.cpp`'s own `s_mode`/`s_ap_up` bookkeeping; neither reads
    > `WiFi.getMode()`. In `wifi_down()` the return of `WiFi.mode(WIFI_MODE_NULL)`
    > is discarded and the state machine advances regardless. So `mode=0 ap=-`
    > proves the firmware BELIEVES the radio is down.
    > **Symptom:** flat batteries overnight in a pocket while `info` says the
    > radio is off. **You will blame** D11, the boost module's quiescent current —
    > this README even primes you for it under "Also unmeasured".
    > **The independent witness is free heap.** `net.cpp` says
    > `WiFi.mode(WIFI_MODE_NULL)` is "the call that genuinely returns the ~50 KB".
    > Read `DIAG,heap`'s `free_b` on HOME with the radio never up, then again
    > 60 s after leaving NETWORK/LINK/CREATOR. **If it has not come back up by
    > roughly 40-50 KB, the driver is still resident whatever `info` says.**
    > Works on `release`, needs no second board and no meter.

13. §67 **Device sleeps correctly**: `esp_light_sleep_start()` has never been
    called on a board, and whether it returns on a `GPIO_INTR_LOW_LEVEL` from
    the two buttons with their internal pull-ups is the whole of this box.
    `[C3]`

    > ⚠ **LIGHT SLEEP ALMOST CERTAINLY DROPS THE USB CDC LINK, AND THIS
    > CONTAMINATES ITEMS 3, 7, 13, 15 AND EVERY LONG CAPTURE.**
    > `hardware/power.h` predicts it in its own words: the board is
    > `CDCOnBoot=cdc`, so the console rides the USB Serial/JTAG peripheral and a
    > light sleep is very likely to drop it. `CONFIG_PM_ENABLE` is not set in the
    > installed sdkconfig, so nothing holds the sleep off while a console is
    > attached. **Symptom:** the capture stops, or `/dev/ttyACM0` disappears,
    > roughly two minutes after you last touched the board — `PWR_IDLE_MS` is
    > 120 s, not the 600 s of the SLEEP rung. **You will blame** a reboot or a
    > crash, and item 15's own acceptance text pushes you there.
    > **The discriminator is `DIAG,heap`'s `uptime_s`:** if the log resumes with
    > `uptime_s` CONTINUING, the board never rebooted and you lost the link; if it
    > restarts near zero, it really rebooted. **Decide your capture tooling on
    > this before any long run** — use something that reopens the port
    > (`tio --auto-reconnect`, or a shell loop around `cat`), not a single
    > `arduino-cli monitor`.

14. The uncalibrated cooldown table across a sleep. `[C4]`

    > ⚠ **THIS ITEM'S STATED ACTION IS THE WRONG ONE AND CANNOT REPRODUCE.**
    > Both this item and `docs/bench.md` C4 used to assert that "a light-sleep
    > wake re-runs `setup()`". It does not: `hardware/power.cpp` calls
    > `esp_light_sleep_start()` and execution resumes at the next line —
    > `setup()` is not re-entered, `cd_begin()` is not re-called, and
    > `esp_light_sleep_start()` resynchronises the clock so the RAM deadlines age
    > correctly. Plan box P6-C3 discharged exactly this and the two documents
    > disagreed.
    > **Symptom if you run it as written:** you sit through ten minutes, wake it,
    > the cooldowns are intact, and you tick the box. **The conclusion you will
    > draw** — "the uncalibrated fallback is safe" — is wrong.
    > **The real exploit needs a REBOOT**, which `game/cooldowns.h` says in its
    > own words. **Run it as:** from item 21's uncalibrated board, arm a
    > cooldown, then unplug and replug. If the cooldown is gone you have
    > reproduced it, and the item as written never would have.
15. §67 **No obvious memory leak**: 24 h on mains USB with the serial line
    captured; `free_b` at hour 24 within a few hundred bytes of hour 1 and
    `min_free_b` no longer falling. Twice: radio off, then hourly cycling.
    **`release`**. `[A5, C5]`

    > ⚠ **"Left alone for 24 h" means no button press, which means the ladder
    > reaches SLEEP after TEN MINUTES and the USB console is expected to drop
    > (item 13).** A recipe run as written can produce a log that ends around
    > `uptime_s` 600 with a monitor that exited when the port disappeared. **Two
    > lost days, twice.** Before walking away: `mkdir -p docs/bench` (item 2),
    > confirm the first `DIAG,heap` row actually landed in the file, and use a
    > capture tool that reopens the port. If the console link will not survive on
    > your host, run the soak over a **wired UART** rather than the native CDC.
    > **Read `uptime_s` monotonicity before you read `free_b`:** a gap with
    > `uptime_s` continuing is a dropped link, a gap with it restarting voids the
    > run.
    > Also expected and not a fault: **`DIAG,link` is silent on the radio-off
    > run** — the row is suppressed until some counter is non-zero, deliberately,
    > so the soak capture stays clean.
16. §64 **the sound**. Seven effects, each audible and distinguishable, then the
    setting toggled and power-cycled. **Nothing in this product has ever been
    heard.** Blocked on **D8**. `[C6]`
17. **The reset reason really becomes the right `BootKind`.** **TWO** reboots —
    unplug and a SOFTWARE restart — reading `[nt] boot=` each time, on
    **`baseline`** and on an **uncalibrated** board. If a power cut reads 3
    instead of 1, no absence is ever charged and the pet stops ageing while the
    device is off, which is phase 6's defect through another door. `[C7]`

    > ⚠ **THIS ITEM SAID "THREE REBOOTS — UNPLUG, RESET PIN, SLEEP-AND-WAKE" AND
    > TWO OF THE THREE WERE UNRUNNABLE.** Rewritten at the final review; the full
    > account is in `docs/bench.md` C7. In short:
    > **(a)** Nothing in this tree calls `esp_deep_sleep_start` — `grep` it — so
    > `BootKind` 4 is unreachable and a sleep-and-wake prints no boot line at
    > all. The old step 3 asked for ten untouched minutes waiting for a line the
    > firmware cannot emit.
    > **(b)** The ESP32-C3 ROM has **no external-pin reset reason**, so the reset
    > pin reads `boot=1`, not 3, and the care bars WILL move. **You will blame**
    > `boot_reason.h`'s table — and then read a table that is swept over all 256
    > values and is correct. Use a software restart for the `boot=3` case.
    > **(c)** On a CALIBRATED board `boot_absence()` never consults the
    > classifier at all (`if (!known)`), so the bars-moved half measures
    > `gt_elapsed_since()`. Use item 21's uncalibrated board.
    > **Read it on `baseline`**, where the DIAG SYS page shows `boot=` at
    > leisure; on `release` it appears once and is flushed out of the 256 B ring.

### First boot, on a board that has never been switched on — spec §65

18. A genuinely fresh board: `esptool erase_flash`, flash `release`, then
    splash → *Cargando la partida...* → PONLE NOMBRE. **No toast over the
    setup screens**; the greeting is drawn on the naming screen itself. `[F1]`

    > ⚠ **If the panel is blank, read the boot serial line before you touch a
    > meter.** `rd_begin()` failing is not fatal: the boot continues headless,
    > prints `[nt] no display on the I2C bus`, and then dumps a **full bus scan
    > naming SDA/SCL and every address found**. That line is the whole diagnosis.
    > If the panel is blank and **that line is ABSENT**, something answered at
    > 0x3C/0x3D and the problem is the driver (SH1106, item 1) or contrast — not
    > the wiring. Have the monitor open across the reset (see the preamble).
    > ⚠ **A corrupt-save screen on a fresh board is a partition problem, not a
    > save problem.** *(The final review fixed the case where a store that never
    > OPENED was diagnosed as two rotten copies of the Box — `LOAD_CORRUPT`, a
    > read-only session, and an offer to WIPE a save that was never damaged.)*
    > If it comes back: `info` reads `load=5 nvs=BAD kverr=0x01` (`KV_E_OPEN_MAIN`)
    > and SETTINGS → *Acerca de* shows `nvs 01`. **A genuinely corrupt save reads
    > `kverr=0x00`.**

19. Type a name with an accent, read it back on HOME and in SETTINGS →
    *Acerca de*; then the date, then the starter. `[F2, F3]`

    > ⚠ **If ONE character vanishes and the line closes up** — rather than a box
    > or a wrong glyph — **that is a missing glyph in a `_tr` face, not an
    > encoding bug.** `GF_TINY` is `u8g2_font_4x6_tr`, 95 glyphs, ASCII only, and
    > `drawUTF8()` emits nothing AND ADVANCES NOTHING for a codepoint it lacks.
    > Note which screen and which row. The host now refuses this class in three
    > more binaries, so it should not reach you.
    > ⚠ **Run the first-boot flow a SECOND time on an erased board and SKIP the
    > name (press both).** That is one gesture and it is a documented outcome, so
    > it is what a hurried operator produces — and it is the input class the
    > review found a live defect in. Confirm the far board's LINK list then shows
    > the species word ("Paketo"), not a blank row. *(Fixed: with a creator
    > custom species active, an unnamed device produced an EMPTY name, beaconed
    > twelve zero bytes, and appeared on the other board as "Buscando
    > Pebbles..." — visible but anonymous.)*

20. **Pull the power in the middle of the flow.** The device must come back on
    the question it was on, with the name already stored. The single most
    valuable trip in the file: the step is persisted precisely so this works,
    and the host can only prove it against a fake NVS. `[F4]`

    > ⚠ **THE FLOW USED TO END ITSELF BEFORE YOU COULD PULL THE POWER, AND THIS
    > WAS THE MOST VALUABLE DEFECT THIS REVIEW FOUND.** The three setup screens
    > are `SF_STICKY`, which protected them from the 20 s auto-return and **not**
    > from the power ladder: at 120 s `pwr_hook_release()` returned early only on
    > HOME, so it called `ui_home()` from SCR_SETUP_NAME and blanked the panel.
    > Two minutes on a 60-glyph naming ring is entirely ordinary.
    > **Symptom:** "it left the wizard on its own, went black, and when I woke it
    > there was a nameless pet on HOME and it never asked again."
    > **You will blame** the onboarding persistence — which is item 22's stated
    > failure mode and exactly what you would go and read `app/onboarding.cpp`
    > about. **It was the ladder.**
    > **It is fixed** (the guard is `sm_is_sticky()` now, pinned by
    > `tests/test_statemachine.cpp` and by a gate on the body). If you ever see
    > it again: press a button — if the panel returns instantly to HOME with
    > `uptime_s` still climbing, it was the ladder and not a crash. The same
    > defect took SCR_TIME, SCR_BATTLE, SCR_EVOLUTION, SCR_GAME and **SCR_ERROR**
    > (whose leave hook kills the blinking LED that is a panel-less board's only
    > voice) to HOME behind a dark screen.

21. The player who reads nothing: hold both buttons on all three screens and
    still reach a playable device. `[F5]`

    > ⚠ **Boot FIRST, then hold.** `PIN_BTN_R` is GPIO2, a strapping pin that
    > must be HIGH at reset, so holding both buttons across the power edge is a
    > boot-mode selection and not a skip. A board held that way comes up dead or
    > in download mode, which reads as "the skip gesture bricked it". Release,
    > replug, and it boots.
    > **Do this item BEFORE item 14** — its product (a device with no name and
    > `CAL_UNSET`) is exactly the board item 14 and item 17 need.
22. A board that has been played, re-flashed with `release`: **no setup wizard**.
    `OB_DONE` is zero so every older save decodes as "already set up". `[F6]`

### Two boards — spec §67's social block (each item depends on the one above)

23. **A beacon goes out whatever the device is called.** Name one board with an
    accent (item 19) and confirm the other lists it, accent drawn correctly.
    This is the P10-C6 defect; if it returns, `DIAG,link`'s `beacons_tx` stays
    at 0 on the accented board. `[B8]`

    > ⚠ **"The two boards do not find each other" is most often the RSSI FLOOR,
    > and it lands in the same counter as a corrupt frame.** `LINK_RSSI_MIN` is
    > **-70 dBm** and any beacon below it is dropped INTO `beacons_bad` — the
    > same counter a CRC failure lands in. Two C3 SuperMinis on chip antennas, in
    > different rooms or with a body between them, routinely sit below -70.
    > **You will blame,** in this order: the accented-name defect (which this
    > very item has just primed you for), the channel, the antenna.
    > **THE THREE-WAY DISCRIMINATOR, all from one `DIAG,link` row on each board,
    > on `release`:**
    > **(a)** `beacons_tx == 0` on the accented board → the name/encoding defect
    > is back. **(b)** `beacons_tx` climbing on A but `rx_beacon == 0` on B → the
    > frame never arrived: wrong channel (`PB_LINK_CHANNEL` is 1) or the radio
    > never came up. **(c)** `beacons_tx` climbing on A **and** `rx_beacon`
    > climbing on B but B's list stays empty → the frame arrived and was refused.
    > **Put the boards 30 cm apart and retry; if the peer appears, it was RSSI.**
    > ⚠ **Confirm `beacons_tx` is RISING on BOTH boards (roughly two per second)
    > before you conclude anything about the room.** A radio that refuses every
    > broadcast is bit-for-bit indistinguishable from an empty room at the job
    > level — `discovery.cpp` handles a refused beacon by not counting it and
    > doing nothing else, by design, and no screen reads `beacons_tx`. A board
    > with `beacons_tx` stuck at 0 while the other climbs is deaf-mute, and the
    > ninety seconds of "no peers" it reports is a lie about the room.
    > `tx_refused` and `tx_no_mem` in the same row separate a refusing radio from
    > a quiet one. *(This case is now driven on the host too, by
    > `lf_set_beacon_ok(false)`.)*

24. Two boards list each other in LINK. `[B1]`

    > ⚠ Same failure as item 23 with a different label: a peer needs **three**
    > beacons at 500 ms above the floor before `DP_QUALIFIED` is set, and
    > `link_qualified_count()` counts only qualified peers — so a peer can be
    > heard and present in the table without being listed. The job's `beacons_rx`
    > rising while the list stays empty means beacons are being dropped
    > intermittently, i.e. distance again. **Get one successful run at 30 cm
    > before you separate them.**

25. The consent gate on the air: press L on **one** board only; the other must
    not move. Read `rx_wrong_peer` climbing in the silent board's `DIAG,link`
    row. `[B2]`

    > ⚠ **Confirm the silent board is printing a `DIAG,link` row AT ALL before
    > you read `rx_wrong_peer`.** The row is suppressed until one of
    > {`rx_session`, `rx_beacon`, `rx_wrong_peer`, `rx_malformed`, `tx_ok`,
    > `tx_fail`, `beacons_tx`} is non-zero — deliberately, so a soak capture stays
    > clean — so a board whose radio never came up prints NOTHING, which looks
    > identical to "the counters are dead". (`rx_ring_ovf`, `rx_beacon_ovf`,
    > `tx_refused` and `tx_no_mem` are not in that guard, so a board with only
    > those non-zero is also silent.)
    > *(The row gained two columns at the final review — `ever_acked` and
    > `ack_ms` — which are the instrument the plan's phase-7 acceptance step
    > names and which had no reader anywhere in the tree on any variant.)*

26. §67 **Local multiplayer works**: press L on both, fight a whole battle, both
    panels agreeing on the result. `[B3]`

    > ⚠ **A battle that dies at about ninety seconds is not a flaky radio.**
    > *(Fixed at the final review.)* `net.cpp`'s `NPH_LINK` backstop measured
    > 91,000 ms from the moment the LINK SCREEN OPENED, not from the last time
    > anybody used the radio — and `link_hold()` deliberately stops the discovery
    > job being serviced during a session, so nothing refreshed that clock. It
    > tore ESP-NOW down under a live battle. **The recognition signature, if you
    > ever see it again:** always ~91 s after the LINK screen was opened, never a
    > fixed time after the battle started, completely independent of what the
    > players do; `DIAG,link`'s `tx_ok`/`beacons_tx` stop dead at that instant;
    > both panels freeze and show *Enlace interrumpido* about 9 s later; and the
    > serial says "link backstop fired - nobody serviced the job", which was a
    > lie — the job was being serviced, it was merely held.
    > ⚠ Panels DISAGREEING on round or HP is a session-layer question and is
    > host-testable — reproduce it against `transport_loopback`, not on the bench.
    > Frames simply not arriving is a transport question and only a board sees it.
    > `tx_ok` vs `tx_fail` is the reading; broadcasts are excluded from `tx_ok`
    > deliberately, so it is a true measure of the session link.

27. The nine-second move clock, felt rather than measured. `[B4]`

    > ⚠ **The screen will dim mid-round and that is correct.** Waiting through a
    > round means not pressing a button, and `PWR_DIM_MS` is 30 s.
    > `link_screen_busy()` is in `ui_radio_job_busy()` so the ladder is clamped at
    > DIM and will NOT navigate home — the dim is cosmetic. Do not read it as the
    > link dropping. **Skippable if time is short:** it is a feel judgement with
    > no acceptance number.
28. §67 **Trade is atomic**, the cross-pair half: trade, then trade again and
    **pull the battery on one board between the two presses**. Each Box holds
    the outgoing Pebble or the incoming one, never both, never neither. `[B6]`
    > ⚠ **Make Box room first, or pass `--no-write` to the smoke script.** Each
    > writing run consumes one Box slot and one of the ten creator-species slots
    > (see item 30's ordering note). Do this item before you burn slots on the
    > phone block.
    > ⚠ `show_save` before and after each reboot — `save slots=`, `active=`,
    > `count=` are the three fields that settle it. Four readings: both boards,
    > both reboots. It answers on `release`.

29. §67 **Breeding compatibility works** and **Generated Pebbles remain
    balanced**. **These cannot be run and it is not a scheduling problem:
    `networking/breed_link.cpp` does not exist** — it was planned in P7-C5 and
    never written, `DISC_CAP_BREED` is not claimed, and the LINK card's CRIAR
    row answers *"Aún no está listo"* on purpose. The owner step is to write
    that module against the `game/breeding.cpp` rules, which are complete and
    swept over all 36×36 roster pairs; then items 24–28 apply to it. `[B7]`

### The phone — spec §67's creator block

> ⚠ **BEFORE ANY OF THE PHONE BLOCK: turn `SETTINGS → Web y QR` ON.** The radio
> is off by default on a fresh device — `gs_cfg_defaults()` clears
> `CF_WEB_ENABLED` deliberately, "the creator server is opt-in" — and
> `ui_creator_radio(true)` is gated on that flag and **does nothing and says
> nothing** when it is clear. **Symptom:** the CREATOR screen paints
> *Conectando...* for twenty seconds and then bounces you back to SETTINGS with
> no explanation, which is exactly what a broken radio looks like. **You will
> blame** the access point or `net_request_portal()`. **The tell:** the row's own
> value column reads `OFF`, and `info`'s Wi-Fi field never leaves `mode=0`. The
> prerequisite was written down only in `tools/creator_smoke.sh`'s header
> comment; it is here now because item 18 produces exactly the board that hits it.

30. §67 **PIN required**: `tools/creator_smoke.sh --variant release --pin NNNN`
    phase 3 — five failures, the sixth refused as locked, the lockout refusing
    the *correct* PIN too, and the counter cleared afterwards. `[D1]`

    > ⚠ **RUN THE SCRIPT ONCE AND READ ITEMS 35, 30 AND 31 OFF THAT ONE RUN.**
    > The script's own phase order is 1,2 (item 35) → 3 (item 30) → 4 (item 31).
    > Read top-to-bottom, this list asks for THREE separate invocations — three
    > runs, three Box slots, three of ten irreplaceable creator-species slots,
    > and two extra six-minute idle waits.
31. §67 **Wi-Fi activates only when necessary** and **Inactivity timeout
    works**: phase 4 polls for `IDLE_S + 60` s without touching the device.
    `[D2]`
32. §67 **QR connection works**: a phone camera on a 62 px symbol on a 0.96"
    panel, in the light you actually have. `[D3]`

    > ⚠ **You have 30 seconds before the panel dims and the contrast drop will
    > hurt the camera.** Press a button before each attempt. Try it in dim light
    > first — an OLED at full contrast in daylight is the usual failure.
    > ⚠ **Accented text in the phone's device card rendering as `Ni?o` is fixed
    > and should not recur.** *(The final review found `json_copy_name()` emitting
    > raw Latin-1 high bytes inside a body served as `application/json`, which is
    > UTF-8 by definition. It emits `\u00XX` now.)* If you do see it, the tell is
    > that the SAME name renders correctly on the OLED and only the phone shows
    > the replacement glyph.
33. §67 **Mobile editor works** — a thumb on glass, which a headless Chromium at
    390×844 is not. `[D4]`
34. §67 **Sprite editor works** — 24 cells across a 390 px phone is a ~16 px
    grid cell where every button on the page is 44. `[D5]`
35. §67 **Device-side validation works** — 27 named reject codes, host-proved
    and under a sanitiser on every commit, but "validation works" is a sentence
    about a socket and `creator_server.cpp` is compiled by no host binary.
    `[D6]`

### The first five minutes — ADDED AT THE FINAL REVIEW

> These three are not in the original 38 and the omission is why they matter.
> **Five §67 boxes — Care works, Active Pebble can be selected, XP and leveling
> work, At least 5 minigames, Minigames transition cleanly — are proven at BOTH
> ENDS and, until this commit, at neither point in the middle.** The screen
> picks the right action id (a screen test), the model does the right thing with
> it (a model test), and the body that JOINS them lives in `ui/ui.cpp`, which no
> host binary compiles — `tests/test_screens.cpp` replaces every one of those
> joins with a one-line recorder. `tools/check.sh` §13 now reads those bodies,
> which is the cheap half. **This is the other half, and it is the first thing
> anybody does with the device.**

39. **CARE → Comida.** The hunger bar must move and an XP toast must appear.
    Then power-cycle: it must still be fed. `[new]`
40. **BOX → activate slot 1** (after item 28 or a `spawn` has put something
    there). HOME must show the other creature; power-cycle, and it must still be
    the active one. `[new]`
41. **PLAY → each of the six rows in turn.** Each must start the game whose name
    is on the row, and return to PLAY once. `[new]`

    > ⚠ **There are SIX minigames, not four.** `docs/bench.md` A3 step 3 said
    > "all four minigames"; P3-C4b appended PACKET FLOOD, FIREWALL, BUFFER and
    > DELETE to the original two, and `MG_ID_COUNT` is 6. Stopping at four leaves
    > the two most drawing-heavy frames unmeasured for A3's heap probe.
    > ⚠ **Watch for a game drawing the WRONG frame** — the row's name and the
    > picture disagreeing, or a screen that looks like garbage with buttons that
    > do something unrelated. *(The pairing that decides this had no order check
    > at all until this commit; it is correct today and is now held by
    > `tests/test_minigames.cpp`.)*
    > ⚠ **The PING cue letter must be centred in the LIT half.** *(Fixed: it was
    > centred on the whole 128 px panel while the slab it labels is 64 px, so it
    > always straddled the midline — and because the panel is in solid font mode,
    > the half hanging off the slab was a bright mark standing alone in the DARK
    > half.)* If you see a smear on the centre line that swaps sides between the
    > A round and the B round, that is this and nothing to do with the panel.

### The save, on a board — spec §31

36. Factory reset really empties **both** partitions. `[G1]`

    > ⚠ **Check `info`'s kv field BEFORE you start.** If the boot line printed
    > `[NVS] FAIL begin(nvs2)`, or SETTINGS → *Acerca de* reads `nvs 02`, you
    > flashed without the sketch's `partitions.csv` — re-upload with
    > `--input-dir build/release` as §4 spells out. On such a board the
    > checkpoint partition has NEVER been written and item 37 cannot pass either.
    > *(The final review fixed the related defect where `save_factory_reset()`
    > returned `a && b` and therefore reported FAILURE — toasting "No consigo
    > recordar nada." — after a reset of the main store that worked perfectly,
    > purely because nvs2 was not there.)*
    > ⚠ **Then check the Box.** *(Also fixed: both wipe paths minted the new
    > starter with a bare `sim_new_pet()`, which is not a Box constructor and
    > mints no slot — so the device came back with a playable creature,
    > `box_count() == 0`, and EVERY SAVE FROM THEN ON A SILENT NO-OP. The next
    > power cut lost everything since the reset AND re-ran the first-boot
    > wizard.)* **The signature, if you ever see it:** `show_save` immediately
    > after the reset reads `slots=0x0000 active=255 count=0/10` while a creature
    > is on the panel.

37. Checkpoint restore after a real `esptool erase_region 0x9000 0x5000`. `[G2]`

    > ⚠ **Do NOT erase 0x310000 — that is the copy under test.** And do the
    > SECOND reboot: the recovered state is committed back to nvs, so boot two
    > must be ordinary. `show_save` before and after settles it; a fresh starter
    > instead of the checkpoint means the nvs2 READ failed, not the erase.

38. **The schema upgrade on real flash**: flash the previous image, play, flash
    this one, and watch a v2 save come back as v3. The only way to see it.
    `[G3]`

    > ⚠ **THE OLD COMMIT CARRIES THE PRE-DECISION PIN MAP, AND THIS LANDS AT THE
    > WORST POSSIBLE MOMENT.** Your D1 decision is a working-tree edit at HEAD; it
    > does not exist at the v2 commit. Check that commit out and you build an
    > image for the committed, un-decided map onto the board you rewired at
    > item 1. **Symptom:** you flash the old image onto a board carrying a save
    > you care about and it comes up blank, or with dead buttons. **You will
    > blame** the old firmware or the migration — and you are one panicked
    > `erase_flash` away from destroying the v2 save the whole item exists to
    > migrate. **Apply your D1/D8 `#define`s to the old commit's `core/config.h`
    > before you build it.** If it does come up blank, the boot serial line
    > settles it in one look: `[nt] no display on the I2C bus` plus the bus scan
    > means wrong pins, not a bad save. **DO NOT ERASE.**
    > ⚠ **This board is read-only behind *actualiza el firmware* when you flash
    > the OLDER image over a v3 save, and that is the point of the item.**
    > *(Fixed at the final review: the trade seam and the explore commit wrote
    > straight through `save_manager` rather than the `gs_*` facade where the
    > read-only guard lives, so a read-only device would write into the save it
    > had just disowned — and re-flashing the newer firmware afterwards would not
    > find the save it left.)*

### Also unmeasured, and named so nobody re-derives it

**Battery life.** Decision D9 chose a 3.3 V boost converter; **D11** (the
module's quiescent current) is open and is the single biggest factor in runtime
— a 100× spread between cheap modules, which is the difference between nine days
and twenty-six.

**The host font is fixed-advance and the device's is not.** Every width in the
string sweep and every layout in a golden containing `GF_BIG` is approximate.
Only a board settles it. `[§H]` *(Corrected at the final review: `GF_HEAD`
(`u8g2_font_t0_11b_tf`) was measured against the real library and is **fixed-pitch
at 6**, not proportional, so the advance table is right for all four prose fonts.
The one real error is `GF_BIG`'s three punctuation glyphs, which advance 6 rather
than 10 — and both `GF_BIG` call sites draw digits only.)*

**Text is SOLID on the panel and TRANSPARENT in every golden.** `ui/render.cpp`
puts the display in `setFontMode(0)` for the whole session and never takes it
out, so a glyph's 0-bits are painted in the INVERSE of the draw colour — the
same rule `setBitmapMode(0)` applies to sprites, which is the divergence P10-C3
found. The host fake ORs only the "on" pixels. **This matters only where text is
drawn over existing ink**, and after the final review there is no shipping site
where the two disagree visibly (the PING cue letter, which was the one, has been
moved inside its slab). **On the bench:** if a photograph does not match a golden
and the difference is TEXT over ink, this is the first thing to suspect and it is
expected. Geometry is not affected — the eleven non-text drawing primitives are
measured pixel-exact against the real U8g2 over a 223,776-case sweep.

**The corner affordance markers are triangles on the panel and solid squares in
the goldens.** 58 of the 75 screen goldens carry the square form. Expected; not a
rendering fault.

**Nothing in this repository has ever run on a device, and 15 of the tree's `.cpp`
files are compiled by no host binary** — `app/app.cpp`, `dev/godmode.cpp`,
`hardware/boot.cpp`, `hardware/kv_nvs.cpp`, `networking/creator_server.cpp`,
`networking/net.cpp`, `networking/transport_espnow.cpp`, `networking/webui.cpp`,
`ui/actfx.cpp`, `ui/ceremony.cpp`, `ui/gfx_u8g2.cpp`, `ui/petfx.cpp`,
`ui/render.cpp`, `ui/ui.cpp`, plus `minigames/registry.cpp` until this commit
linked it. Both defects P10 shipped lived in that set. `tests/fakes/SHADOWS.txt`
enumerates the fifty shipping functions the host fakes stand in for, and
`tools/check.sh` §10 fails when that set changes without a human classifying the
newcomer.

## 3. Toolchain

Pinned to exactly what CI installs. Other versions may work; nothing has been
checked against them.

```bash
# arduino-cli
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | BINDIR=/usr/local/bin sh

arduino-cli config init --overwrite
arduino-cli config add board_manager.additional_urls \
  https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32@3.1.1
arduino-cli lib install "U8g2@2.35.30"
```

For the host test suite you need only a C++17 `g++` and `make`. `python3` is
needed for the content generator and its gate; `node` for the browser page
test. Both are skipped **with a printed line** if absent — never silently.

---

## 4. Build, test, gate

| Command | What it does | Time |
|---|---|---|
| `make -C tests check` | 58 host binaries, ~6.2 M assertions | ~40 s |
| `make -C tests asan` | the outside-input path under AddressSanitizer, 8 binaries | ~20 s |
| `tools/build.sh` | one firmware build, 0 project warnings enforced, prints flash/globals | ~90 s |
| `tools/build_matrix.sh` | six feature variants **and the release size caps** | ~9 min |
| `tools/check.sh` | everything above except the matrix, plus 159 named gates | ~3 min |

`tools/check.sh` is **the gate**: run it before every commit. `--no-tests`
builds only, `--no-build` tests only; CI splits it that way across two jobs so
neither job does the other's work. It ends in exactly one line — `GATE OK` or
`GATE FAIL: <what, and why it matters>`.

A single test binary, filtered:

```bash
make -C tests bin/test_persistence && ./tests/bin/test_persistence --filter migrat
```

Everything is seeded through `rng_seed_all(0xC0FFEE)` unless a test passes its
own seed, so a failure reproduces.

### Building for a board

**`--build-path` is not optional.** Without it `tools/build.sh` compiles into a
`mktemp -d` it deletes on exit (`trap 'rm -rf "$WORK"' EXIT`), so the command
prints `BUILD OK` and leaves no binary anywhere on the machine — and the upload
then reads `arduino-cli`'s own sketch cache, which `build.sh` never writes to.
The recipe in this section did exactly that until the P10-C6 exit, and the
failure was worse than a missing file: an operator who ran a plain
`arduino-cli compile` to get past the error would have flashed the **baseline**
image — god console, `spawn` over the cable — while believing they had flashed
`release`, because it is the same sketch and the size looks plausible. Verified
by disassembling both: only the cached one contains `[god] ENTER (tainted
forever)`.

```bash
# the artefact that ships
tools/build.sh --variant release --define GOD_MODE_ENABLED=0 --build-path build/release
arduino-cli upload -p /dev/ttyACM0 --input-dir build/release \
  --fqbn esp32:esp32:esp32c3:PartitionScheme=huge_app,CDCOnBoot=cdc Pebblebol

# the dev build, for the DIAG screens a few bench items read
tools/build.sh --variant baseline --build-path build/baseline
arduino-cli upload -p /dev/ttyACM0 --input-dir build/baseline \
  --fqbn esp32:esp32:esp32c3:PartitionScheme=huge_app,CDCOnBoot=cdc Pebblebol
```

`build/` is already in `.gitignore`. `GOD_MODE_ENABLED=0` is what ships; the
`baseline` build carries a hidden developer console. **Quote a size or a timing
against the variant you measured it on** — `docs/budget.md` §8 records a phase
exit that compared a dev build with a shipping one and drew the wrong
conclusion, and `docs/bench.md` §I refuses a `baseline` row for any §67 box.

### Sizes, measured at this commit

| Variant | Defines | Flash | Globals |
|---|---|---|---|
| **release** | `GOD_MODE_ENABLED=0` | **1,350,840** | **59,452** |
| baseline | — | 1,367,774 | 59,548 |
| no-web | `FEATURE_WEB=0` | 1,254,122 | 55,172 |
| no-god | `GOD_MODE_ENABLED=0` | 1,350,840 | 59,452 |
| sh1106 | `DISPLAY_IS_SH1106=1` | 1,367,774 | 59,548 |
| all-off | web, ESP-NOW and god mode off | 605,298 | 26,724 |

The release caps are **1,600,000 flash** and **65,000 globals**
(`GATE_RELEASE_*` in `config.h`, enforced by `build_matrix.sh` and, since
P10-C5, by CI). That leaves **249,160 B of flash and 5,548 B of globals free.**
`docs/budget.md` §15 is the per-chunk account of phase 10 and §16 is the final
one: what the v1.0 artefact costs, against every cap, subsystem by subsystem.

`no-god` and `release` are the same defines and therefore the same number: the
matrix keeps both rows because one measures a feature and the other names the
artefact, and the row that names the artefact is the one the caps are applied
to.

---

## 5. Partition table

`Pebblebol/partitions.csv` is flashed, not the board menu's table — arduino-cli
copies a sketch-local `partitions.csv` into the build directory and esptool
writes that one.

```
nvs,      data, nvs,      0x9000,   0x5000
otadata,  data, ota,      0xE000,   0x2000
app0,     app,  ota_0,    0x10000,  0x300000
nvs2,     data, nvs,      0x310000, 0x10000
spiffs,   data, spiffs,   0x320000, 0xD0000
coredump, data, coredump, 0x3F0000, 0x10000
```

**`nvs2` is the point of the file.** Arduino's `initArduino()` erases the whole
default `nvs` partition when `nvs_flash_init()` reports no free pages or a new
version — *before `setup()` runs*, so no care in this firmware can prevent it.
`nvs2` is a second, private NVS partition the core never touches; the checkpoint
written there turns that erase from "the creature is gone" into
`LOAD_RECOVERED_CKPT`.

**`PartitionScheme=huge_app` stays in the FQBN** even though the CSV decides
the real table, and the reason is not obvious: the board menu still supplies
`upload.maximum_size`, the ceiling the compile is checked against. Dropping the
option leaves that check at the default scheme's 1,310,720 B and fails a build
that fits `app0` perfectly well. `huge_app`'s ceiling is 3,145,728 B, which is
exactly `app0` above. `tools/build.sh` gates both facts: the reported app
maximum must equal 3,145,728, and the table about to be flashed must contain
`nvs2`.

No OTA slot. An update arrives over USB.

---

## 6. Architecture

`Pebblebol/src/` is ten layers. The rule that matters is **which of them may
touch Arduino**:

```
app/         boot, the main loop, the state machine, first-boot onboarding
  ↓
ui/          35 files: one per screen, the renderer, the drawing primitives
  ↓
game/        21 files: the rules. Care, XP, battle, breeding, the Box,
             evolution, capture, validation. PURE.
networking/  protocol and session (PURE) + the ESP-NOW, discovery,
             creator-server and Wi-Fi drivers (Arduino)
persistence/ the save schema, the pair discipline, migration. Arduino-free
             (it reaches flash only through the kv_store.h seam)
minigames/   six games, each split logic (PURE) / draw
core/        config, types, RNG, CRC, UTF-8, the Spanish string table
data/        GENERATED content tables — 60 species, 34 attacks, 10 items,
             40 encounters, 40 evolution rules, the sprite atlas
hardware/    I2C, input, NVS, the clock, deep sleep, audio
dev/         the serial console and the god-mode menu
```

**These files are on a red line the gate greps**: `src/game/**`,
`src/minigames/**`, `networking/{protocol,session}.cpp`,
`core/{crc16,rng,utf8,perf}.cpp`, `ui/{petfx_core,corrupt_fx,anim_ease}.cpp`,
`dev/diag_core.{h,cpp}` and `hardware/boot_reason.h`. An `#include <Arduino.h>`
in any of them fails `tools/check.sh` by name, **and since the P10-C6 exit so
does an allocation** — `malloc`, `free` or `operator new` anywhere on that line,
because `tests/test_soak.cpp` counts `operator new` only, so a balanced
`malloc`/`free` pair is invisible to it *and* to AddressSanitizer, which sees a
leak rather than churn. (An earlier version of this paragraph named five sets,
omitted `anim_ease.cpp`, and said `dev/diag_core.cpp` was **not** grep-held when
it has its own named gate — which invites exactly the silent repair that gate's
own comment predicts. `persistence/` really is held by the test Makefile alone.)
This is not tidiness — it is what lets 58 host
binaries drive the rules directly, against fake flash, a fake framebuffer and a
fake clock, with no board and no emulator.

Four more rules a newcomer will otherwise break. The first three have a gate
behind them:

- **Navigation is assigned only in `app/state_machine.cpp`.** No screen sets the
  next screen.
- **`src/data/*.h` is generated** by `tools/gen_content.py` from
  `tools/content/*.json`. Hand-editing one fails the gate. Regenerate with
  `python3 tools/gen_content.py`; `tools/content/verify.py` validates the pack
  against the design rules on its own, importing nothing from the generator.
- **Every pin number is one of the seven `PIN_` `#define`s in `core/config.h`
  §2** — six of which D1 and D8 own, the seventh being D10's battery divider. A
  `PIN_` macro anywhere else fails the gate — there is no second place for the
  owner to look when D1 closes.
- **Every Spanish literal lives in `core/strings_es.h`** and reaches the screen
  as a `StrId`. That one is a convention, not a gate. The table is UTF-8 with a
  Latin-1 repertoire; a *stored* name (a nickname, a peer's name off the air) is
  raw Latin-1, and everything draws with `drawUTF8()`, so `core/utf8.h` owns the
  one crossing and the one codepoint rule. There were three copies of that rule
  until P10-C4 and they disagreed; a gate holds the unification now.

### Where a screen becomes host-testable

`ui/gfx.h` is the seam. Two backends implement it: `ui/gfx_u8g2.cpp` on the
device and `tests/fakes/gfx_fb.cpp` on the host — a 128×64 framebuffer with
font metrics, an **out-of-bounds recorder** and a **malformed-UTF-8 recorder**.
Every screen with a render hook is driven through it at the worst content the
product allows (a twelve-character all-multi-byte nickname, level 30, a full
Box, the longest Spanish string) and held to zero out-of-bounds draws, zero
malformed strings and zero allocations. `tests/test_screens.cpp` carries one
table row per `ScreenId` with a `static_assert` on its length, so **a screen
added to the enum without a fixture fails the build**.

Files that include `ui/render.h` — and therefore `Arduino.h` — are compiled by
no host binary: `ui/ui.cpp`, `ui/petfx.cpp`, `ui/actfx.cpp`, `ui/ceremony.cpp`,
`app/app.cpp`, `dev/godmode.cpp`. Anything you add there is untested by
construction, and the honest move is to put the logic in a pure module beside
it (`ui/petfx_core.cpp`, `app/onboarding.cpp`, `dev/diag_core.cpp` all exist
for exactly this reason) and leave the Arduino file as the caller.

---

## 7. The save

Seven blob types in NVS. Five of them are **pairs** — the Box header, the
config, the inventory, the cooldown table and each of the ten Pebble slots —
written twice under `<key>0` / `<key>1` with a sequence number. Every write goes
to the copy a reader would *not* pick, then reads back and compares, so one torn
or rotted copy always leaves the other intact: that is `LOAD_RECOVERED_PAIR`,
not data loss. The other two are single keys with no sequence number, on
purpose: a creator species record (`cs0..cs9`) is content rather than state, and
the trade journal (`tr`) has nothing to choose between.

A load answers with one of seven results and **never destroys a save it could
not read**. `LOAD_CORRUPT` and `LOAD_FOREIGN_NEWER` write nothing at all: the
session goes read-only and the error screen asks the player — *Recuperar*
(restore the `nvs2` checkpoint) or *Reset de fábrica*, behind two confirmations.

`SAVE_SCHEMA_VERSION` is **3**. A save at version 2 is read in place and
re-sealed; a save at version 1 (the legacy Nottamagochi blobs, different keys
and different layouts) is transformed. The chain is checked at compile time —
bumping the version without adding a migration row fails the build by name.
`docs/save_schema.md` §8 is the full account, including what a naive bump does
(every played device into SAVE ERROR on the first flash) and the three things
that had to be written to stop it.

All of that is proved against a RAM NVS with fault injection
(`tests/fakes/kv_mem.cpp`) — every failure mode a real NVS can produce, provoked
on purpose, because a recovery path nobody can provoke is a recovery path nobody
has tested. **A byte has never reached a real one.** `docs/bench.md` §G is the
three trips that would settle it: a factory reset that empties both partitions,
a checkpoint restore after `esptool erase_region 0x9000 0x5000`, and the
two-image flash that shows the v2 → v3 upgrade on real flash.

---

## 8. First boot, honestly

What a genuinely fresh device does, in order:

1. Splash with the product name and `FW_VERSION`.
2. Load: nothing on flash, so `LOAD_FRESH`. Clock stays `CAL_UNSET`.
3. A starter Pebble is minted into slot 0 and made active — **before** the
   player is asked anything, so a player who reads nothing still ends up with a
   working device.
4. **No toast.** The boot greeting `STR_BOOT_FIRST` is *suppressed* while the
   setup flow is pending, and the naming screen says hello itself
   (`STR_SU_HELLO`, *«Hola. Empecemos.»*) on a row of its own. The reason is
   the interesting part and it is a bug fix rather than a preference: the toast
   band is rows 45–55 and **both** of the setup screens' instruction lines live
   inside it, so for `UI_TOAST_MS` the only line telling a new player how to
   save was completely covered. `docs/bench.md` F1 says that if a toast covers
   the bottom two lines, that defect is back. (This section claimed the toast
   as expected behaviour until the P10-C6 exit — it documented, as correct, the
   exact thing the bench item is written to catch.)
5. **Three questions, in this order: name the device, set the date and time,
   pick one of three starters** (Paketo, Buggo, Daemi — one per corner of the
   type chart). L taps to move on, L held accepts, R changes the thing under the
   cursor, R held repeats. Any question can be skipped, and holding both buttons
   ends the flow.
6. HOME.

**The step is persisted, not inferred.** Two bits of the config say which
question the device is on, so naming it and then pulling the power comes back on
the *next* question rather than on none of them. `OB_DONE` is zero, so every
save written by any earlier firmware decodes as "already set up" — a device that
has been played for months must never be handed a setup wizard.

Bench item **F4** is the one that has to be tried on a board: pull the power
mid-flow and confirm the device comes back on the question it was on.

---

## 9. The serial console

The **release** artefact — no god mode, no dev console — prints two CSV headers
at boot and one row of each every 60 s, on every screen, for ever:

```
DIAG#,heap,uptime_s,free_b,min_free_b
DIAG#,perf,uptime_s,scr,frames,frame_max_us,frame_worst_us,frame_worst_scr,
      frame_over,frame_sat,passes,pass_worst_us,pass_worst_scr,pass_over,discards
```

and answers four typed commands at 115200 baud: `help`, `info`, `show_save`,
`stall <ms>`. That is the whole of the shipping build's diagnostics, and most
of the §46 performance sheet is read from it. `docs/bench.md` §E is the walk.

The `baseline` build adds the god-mode menu and a DIAG screen with twelve
fields (spec §49) and **eleven** of §66's twelve commands (spec §66) — the
twelfth, `show_save`, is `DCF_ALWAYS` and ships in `release` too, as the
paragraph above says.

---

## 10. Repository map

```
README.md                  this file
CHANGELOG.md               English, Keep a Changelog, one entry per phase
PEBBLEBOL_IMPLEMENTATION_PLAN.md   the plan every commit is answerable to
PEBBLEBOL_IMPLEMENTATION_AUDIT.md  the audit of the v1 codebase this replaced
Pebblebol/                 the sketch (Pebblebol.ino + src/ + partitions.csv)
tests/                     59 host binaries, fakes, fixtures, 75 PBM goldens
  fakes/SHADOWS.txt        THE FIFTY SHIPPING FUNCTIONS THE HOST FAKES STAND
                           IN FOR, enumerated and classified. Every host
                           binary that links a fake drives the FAKE's body
                           while the reader believes it drives the firmware's,
                           and the linker CANNOT see this class - all fifty
                           shipping bodies live in translation units no host
                           binary compiles, so no object ever defines them and
                           there is never a collision. Two of the defects this
                           product shipped were exactly that. check.sh section
                           10 re-derives the set from source and fails when it
                           differs from this file. READ IT BEFORE TRUSTING A
                           GREEN RUN ABOUT ANYTHING IT LISTS.
tools/                     build.sh · check.sh · build_matrix.sh · the content
                           generator · the sprite generator · the page test
web/creator/               the phone page the device serves (generated into
                           src/data/index_html.h)
docs/
  bench.md                 ← what a person with two boards and a phone must run
  decisions.md             the decision log. CLOSED TO NEW ENTRIES, but five
                           decisions are still OPEN AT SHIP inside it: D1, D5,
                           D8, D10, D11. Read its status column, not this line.
                           D5 (SSD1306 vs SH1106) is the one that costs an hour
                           at item 18 if you have a 1.3" panel, and section 1
                           does not mention it.
  save_schema.md           every byte that reaches flash
  protocol.md              the radio contract
  budget.md                where the flash and the RAM went
  content.md               the content pack and how to regenerate it
  PEBBLEBOL_PRODUCT_SYSTEM_SPEC.md    the product spec (§ numbers cited
  PEBBLEBOL_HARDWARE_AND_BATTERY_SPEC.md   throughout the code)
  hardware_reconciliation.md
  legacy/                  the Spanish documents this one replaced
```

---

## 11. If you are about to change something

- Run `tools/check.sh`. It is three minutes and it is the difference between a
  green tree and a green-looking one.
- **When you add a test or a gate, break what it guards and watch it fail by
  name.** This repository has dozens of recorded instances of an instrument that
  could not fail, or failed on the wrong thing — a gate that counted a type name
  so deleting the line it guarded still passed; a fifteen-point power-cut sweep
  that ran against a code path the shipping build never executes; a drawing bug
  invisible because the file it lived in is compiled by no host binary; a
  completeness assertion that counted table rows instead of enum members, so it
  fired on the correct fix and stayed silent on the bug. Every one was found by
  breaking the instrument, never by the instrument. `CHANGELOG.md` names them.
- **Ask what your guard ENUMERATES.** That is the one sentence all of them
  share: the guard was keyed to a set narrower than the real state space — one
  radio owner out of three, `ScreenId` instead of `ScreenId × ErrKind`, the
  shipping symbol instead of the pair {symbol, the fake that replaces it}. "Can
  this fail?" is the wrong question; "is that the whole set?" is the right one.
- **If you touch a file `tests/fakes/` also defines a function of, open
  `tests/fakes/SHADOWS.txt` first.** Fifty shipping functions are shadowed. Every
  host binary that links a fake drives the FAKE's body while you believe it
  drives the firmware's, and **the linker can never tell you** — measured: the
  six fake objects and the other host objects have ZERO defined symbols in
  common, because all fifty shipping bodies live in translation units no host
  binary compiles. `tools/check.sh` §10 re-derives the set and fails when it
  changes; the manifest says, per row, whether the fake CLAIMS to imitate the
  shipping body (and which running assertion holds that claim) or is deliberately
  something else. A row that claims imitation and names nothing is refused.
- **The final review added a fourth question to the three above: what does this
  claim rest on ONE CALL DEEP?** `ui_pet_name_latin1()`'s whole body is
  `{ pet_name_stored(out, cap); }`, so a hash or a grep of that body is blind to
  the function that does the work. Every guard in §10 that could be behavioural
  is behavioural for that reason.
- **A comment that overstates a guard is worse than no comment**, because it is
  what stops the next reader adding the real one. The final review found five:
  a fake banner claiming five entry points where the file has eleven, a
  "DEVICE translation unit" that reaches no device header and was therefore left
  untested, a `static_assert` described as keeping a table in order when it
  counts rows, "(gated)" beside a rule with no gate, and an overlong-line
  handler whose comment promised the exact opposite of what it did.
- Ask what the **release artefact** does, not what the build in front of you
  compiles. `GOD_MODE_ENABLED` is 1 on your desk and 0 on the device.
- A persisted or transmitted layout is never edited in place. Add to
  `reserved[]`, or bump the version and add a migration **and a test**.
