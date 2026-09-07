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

**Seventeen of the spec's §67 acceptance boxes are open.** Fifteen are open
because nobody has watched a device do the thing; one (`Device boots reliably`)
waits on decision **D1** above; and two — breeding compatibility and generated
Pebbles staying balanced — wait on a module that **was never written**, which is
a different sentence and is spelled out at item 30 below.

This is the whole list, consolidated from phases 5 to 10 and **ordered so that
each item's preconditions are above it**. `docs/bench.md` has the same items
with the keystrokes, the acceptance numbers and what a green reading does *not*
prove; the bracketed code is its section. Nothing here has been run.

> **THREE THINGS THE P10-C6 EXIT FOUND THAT WOULD HAVE COST AN OWNER A DAY EACH.**
> Items 20–27 (the two-board block) were dead for any device whose owner typed
> an accented name at first boot — the beacon carried the drawn UTF-8 form of
> the name into a field that is Latin-1 by wire contract, `disc_encode()`
> refused it, and the board emitted **no beacon at all**, silently. Items 31–36
> (the phone block) were dead for everyone: the power ladder tore the access
> point down after 120 s with no *button* pressed, which is exactly what using a
> phone looks like, 180 s before the portal's own timer. And item 12's
> `rx_wrong_peer` had **no reader anywhere in the tree**. All three are fixed,
> and all three now fail by name in the host suite if they come back.

### Before anything (blocks all 38 items)

1. **Decide D1 and D8, wire a board, define `PB_PINS_CONFIRMED`.** §1 above.
   Until this, nothing below can run. `[§0]`
2. **`mkdir -p docs/bench`** before the first capture. It is not in the
   repository and `tee` exits 1 without it — on a 24 h soak that is a lost day.
   `[§0]`

### One board, one seat — spec §46's five thresholds

3. Frame time inside `FRAME_BUDGET_US` (50 ms) on every screen. Four screens
   must be visited in a specific state, because their expensive layer is
   conditional. **`baseline`** for the console page, cross-checked against
   `release`'s `DIAG,perf` line. `[A1]`
4. One `loop()` pass inside `PERF_PASS_BUDGET_US` (100 ms), provoked three ways
   — one of them `curl --limit-rate 200` at the creator portal. **`baseline`**.
   `[A2]`
5. Per-frame heap delta 0 on HOME, BATTLE and GAME. **`baseline`, on a board you
   are willing to taint** — the probe only runs while the console is engaged.
   `[A3]`
6. Input-to-render latency ≤ 2 frames: the `stall` command and a 240 fps phone
   camera. **`release`**. `[A4]`
7. §67 **No obvious battery-draining loops** — the loop rate per rung. The
   instrument is built and the number has been *computed and never read*.
   `[C3]`

### The serial console (do this first on any board — it tells you which build you flashed)

8. On **`release`**: `help`, `info`, `show_save`, `stall 100`. `help` must say
   `GOD_MODE_ENABLED 0` is why the rest is absent. If it lists `spawn`, you are
   holding the baseline image. `[E1]`
9. On **`baseline`**: `spawn 1 5`, then `show_save`. The GOD bar must appear on
   the first mutating command and the Pebble must carry the taint ribbon.
   `[E2]`
10. `info`'s twelve §49 fields. Battery must read `n/a` (there is no
    `PIN_BATT`, no divider, decision **D10** is open) and BLE `none`. `[E3]`

### Radio and power — one board

11. §67 **Wi-Fi scanning works**: a real access point in the list, then an
    encounter. Every scan in every host test came from a fake driver. `[C1]`
12. §67 **Wi-Fi shuts down after use**: `info`'s Wi-Fi field reads `mode=0`,
    `ap=-` after a scan and after a link. **`release`** — this reading exists on
    the shipping build only since P10-C1, and its counters only since P10-C6.
    `[C2, B5]`
13. §67 **Device sleeps correctly**: `esp_light_sleep_start()` has never been
    called on a board, and whether it returns on a `GPIO_INTR_LOW_LEVEL` from
    the two buttons with their internal pull-ups is the whole of this box.
    `[C3]`
14. The uncalibrated cooldown table across a sleep — a light-sleep wake re-runs
    `setup()`. `[C4]`
15. §67 **No obvious memory leak**: 24 h on mains USB with the serial line
    captured; `free_b` at hour 24 within a few hundred bytes of hour 1 and
    `min_free_b` no longer falling. Twice: radio off, then hourly cycling.
    **`release`**. `[A5, C5]`
16. §64 **the sound**. Seven effects, each audible and distinguishable, then the
    setting toggled and power-cycled. **Nothing in this product has ever been
    heard.** Blocked on **D8**. `[C6]`
17. **The reset reason really becomes the right `BootKind`.** Three reboots —
    unplug, reset pin, sleep-and-wake — reading `[nt] boot=` each time. If a
    power cut reads 3 instead of 1, no absence is ever charged and the pet stops
    ageing while the device is off, which is phase 6's defect through another
    door. `[C7]`

### First boot, on a board that has never been switched on — spec §65

18. A genuinely fresh board: `esptool erase_flash`, flash `release`, then
    splash → *Cargando la partida...* → PONLE NOMBRE. **No toast over the
    setup screens**; the greeting is drawn on the naming screen itself. `[F1]`
19. Type a name with an accent, read it back on HOME and in SETTINGS →
    *Acerca de*; then the date, then the starter. `[F2, F3]`
20. **Pull the power in the middle of the flow.** The device must come back on
    the question it was on, with the name already stored. The single most
    valuable trip in the file: the step is persisted precisely so this works,
    and the host can only prove it against a fake NVS. `[F4]`
21. The player who reads nothing: hold both buttons on all three screens and
    still reach a playable device. `[F5]`
22. A board that has been played, re-flashed with `release`: **no setup wizard**.
    `OB_DONE` is zero so every older save decodes as "already set up". `[F6]`

### Two boards — spec §67's social block (each item depends on the one above)

23. **A beacon goes out whatever the device is called.** Name one board with an
    accent (item 19) and confirm the other lists it, accent drawn correctly.
    This is the P10-C6 defect; if it returns, `DIAG,link`'s `beacons_tx` stays
    at 0 on the accented board. `[B8]`
24. Two boards list each other in LINK. `[B1]`
25. The consent gate on the air: press L on **one** board only; the other must
    not move. Read `rx_wrong_peer` climbing in the silent board's `DIAG,link`
    row. `[B2]`
26. §67 **Local multiplayer works**: press L on both, fight a whole battle, both
    panels agreeing on the result. `[B3]`
27. The nine-second move clock, felt rather than measured. `[B4]`
28. §67 **Trade is atomic**, the cross-pair half: trade, then trade again and
    **pull the battery on one board between the two presses**. Each Box holds
    the outgoing Pebble or the incoming one, never both, never neither. `[B6]`
29. §67 **Breeding compatibility works** and **Generated Pebbles remain
    balanced**. **These cannot be run and it is not a scheduling problem:
    `networking/breed_link.cpp` does not exist** — it was planned in P7-C5 and
    never written, `DISC_CAP_BREED` is not claimed, and the LINK card's CRIAR
    row answers *"Aún no está listo"* on purpose. The owner step is to write
    that module against the `game/breeding.cpp` rules, which are complete and
    swept over all 36×36 roster pairs; then items 24–28 apply to it. `[B7]`

### The phone — spec §67's creator block

30. §67 **PIN required**: `tools/creator_smoke.sh --variant release --pin NNNN`
    phase 3 — five failures, the sixth refused as locked, the lockout refusing
    the *correct* PIN too, and the counter cleared afterwards. `[D1]`
31. §67 **Wi-Fi activates only when necessary** and **Inactivity timeout
    works**: phase 4 polls for `IDLE_S + 60` s without touching the device.
    `[D2]`
32. §67 **QR connection works**: a phone camera on a 62 px symbol on a 0.96"
    panel, in the light you actually have. `[D3]`
33. §67 **Mobile editor works** — a thumb on glass, which a headless Chromium at
    390×844 is not. `[D4]`
34. §67 **Sprite editor works** — 24 cells across a 390 px phone is a ~16 px
    grid cell where every button on the page is 44. `[D5]`
35. §67 **Device-side validation works** — 27 named reject codes, host-proved
    and under a sanitiser on every commit, but "validation works" is a sentence
    about a socket and `creator_server.cpp` is compiled by no host binary.
    `[D6]`

### The save, on a board — spec §31

36. Factory reset really empties **both** partitions. `[G1]`
37. Checkpoint restore after a real `esptool erase_region 0x9000 0x5000`. `[G2]`
38. **The schema upgrade on real flash**: flash the previous image, play, flash
    this one, and watch a v2 save come back as v3. The only way to see it.
    `[G3]`

### Also unmeasured, and named so nobody re-derives it

**Battery life.** Decision D9 chose a 3.3 V boost converter; **D11** (the
module's quiescent current) is open and is the single biggest factor in runtime
— a 100× spread between cheap modules, which is the difference between nine days
and twenty-six.

**The host font is fixed-advance and the device's is not.** Every width in the
string sweep and every layout in a golden containing `GF_HEAD` or `GF_BIG` is
approximate. Only a board settles it. `[§H]`

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
tests/                     58 host binaries, fakes, fixtures, 74 PBM goldens
tools/                     build.sh · check.sh · build_matrix.sh · the content
                           generator · the sprite generator · the page test
web/creator/               the phone page the device serves (generated into
                           src/data/index_html.h)
docs/
  bench.md                 ← what a person with two boards and a phone must run
  decisions.md             the decision log, CLOSED at this commit
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
- Ask what the **release artefact** does, not what the build in front of you
  compiles. `GOD_MODE_ENABLED` is 1 on your desk and 0 on the device.
- A persisted or transmitted layout is never edited in place. Add to
  `reserved[]`, or bump the version and add a migration **and a test**.
