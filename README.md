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
> binaries pass, and the release image fits its budget with 251 KB of flash to
> spare — none of which is evidence that a board works. **Seventeen of the
> spec's acceptance boxes are open**, sixteen of them because nobody has watched
> a device do the thing, and the **pin map fails this project's own guard**.
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

**So: decide D1 and D8 first, edit those seven `#define` lines, define
`PB_PINS_CONFIRMED`, and let the assertions check your map.** Every pin number
in the firmware comes from that one block — `tools/check.sh` fails any other
file that defines a `PIN_` macro, so there is no second place to look.

Free, non-strapping GPIOs on this board: **3, 4, 6, 7, 10**. GPIO0 is reserved
for the battery divider. GPIO11–19 are not brought out.

---

## 2. What has never run on hardware

**Seventeen of the spec's §67 acceptance boxes are open, and every one of them is
open because nobody has watched a device do the thing.** Each has a written
owner step in **`docs/bench.md`**, which is the document to work from: it gives
the variant, the keystrokes, the acceptance number, and what a green reading
does *not* prove. Two of the seventeen had no step until this commit — "device-side
validation works" and "no obvious memory leak" — and they have one now.

| §67 box | Owner step | Needs |
|---|---|---|
| Device boots reliably | §0, then §A | one board, **after D1 is decided** |
| Local multiplayer works | B3 | two boards |
| Trade is atomic (the cross-pair half) | B6 | two boards + a battery to pull |
| Breeding compatibility works | B7 | two boards — **and `breed_link.cpp` is not built into any screen**, so this one is not merely unrun, it is unbuildable today |
| Generated Pebbles remain balanced | B7 | as above |
| Wi-Fi scanning works | C1 | one board and a real access point |
| Wi-Fi shuts down after use | B5, C2 | one board |
| Device sleeps correctly | C3 | one board and a multimeter |
| No obvious battery-draining loops | C3 | one board — the instrument is built; the number has been computed, never read |
| No obvious memory leak | C5 | one board, mains USB, 24 h and a captured serial log |
| PIN required | D1 | a phone (or `curl`); `tools/creator_smoke.sh` does most of it |
| Wi-Fi activates only when necessary | D2 | one board, five minutes |
| Inactivity timeout works | D2 | as above |
| QR connection works | D3 | a phone with a camera |
| Mobile editor works | D4 | a phone |
| Sprite editor works | D5 | a phone and a thumb |
| Device-side validation works | D6 | a phone or `curl` — the rules are host-proved 27 named codes deep and under a sanitiser, but "validation works" is a sentence about a socket, and `creator_server.cpp` is compiled by no host binary |

(The letters are `docs/bench.md` sections; a decision is written `D1` too, and
they are unrelated — bench `D1` is the PIN, decision **D1** is the pin map.)

Three more items are not §67 boxes and are worth the trip anyway:

- **F4 — pull the power in the middle of the first-boot flow** and confirm the
  device comes back on the question it was on. The single most valuable trip in
  the file: the step is persisted precisely so that this works, and the host can
  only prove it against a fake NVS.
- **G1–G3 — the save on a board.** A factory reset that really empties *both*
  partitions; a checkpoint restore after a real
  `esptool erase_region 0x9000 0x5000`; and the two-image flash that shows the
  v2 → v3 schema upgrade on real flash.
- **§46's five performance thresholds** (frame time, loop pass, per-frame heap,
  input latency, save time) are gated by nothing and are runnable today —
  `docs/bench.md` §A, five items, each with an acceptance number.

Also unmeasured, and named so nobody re-derives it: **battery life**. Decision
D9 chose a 3.3 V boost converter; D11 (the module's quiescent current) is open
and is the single biggest factor in runtime — a 100× spread between cheap
modules, which is the difference between nine days and twenty-six.

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
| `make -C tests check` | 58 host binaries, ~5.8 M assertions | ~40 s |
| `make -C tests asan` | the outside-input path under AddressSanitizer, 6 binaries | ~15 s |
| `tools/build.sh` | one firmware build, 0 project warnings enforced, prints flash/globals | ~90 s |
| `tools/build_matrix.sh` | six feature variants **and the release size caps** | ~9 min |
| `tools/check.sh` | everything above except the matrix, plus ~129 named grep gates | ~3 min |

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

```bash
tools/build.sh --variant release --define GOD_MODE_ENABLED=0
arduino-cli upload -p /dev/ttyACM0 \
  --fqbn esp32:esp32:esp32c3:PartitionScheme=huge_app,CDCOnBoot=cdc Pebblebol
```

`GOD_MODE_ENABLED=0` is what ships. The `baseline` build carries a hidden
developer console; **quote a size or a timing against the variant you measured
it on** — `docs/budget.md` §8 records a phase exit that compared a dev build
with a shipping one and drew the wrong conclusion.

### Sizes, measured at this commit

| Variant | Defines | Flash | Globals |
|---|---|---|---|
| **release** | `GOD_MODE_ENABLED=0` | **1,348,854** | **59,452** |
| baseline | — | 1,365,776 | 59,548 |
| no-web | `FEATURE_WEB=0` | 1,252,116 | 55,172 |
| no-god | `GOD_MODE_ENABLED=0` | 1,348,854 | 59,452 |
| sh1106 | `DISPLAY_IS_SH1106=1` | 1,365,776 | 59,548 |
| all-off | web, ESP-NOW and god mode off | 603,304 | 26,692 |

The release caps are **1,600,000 flash** and **65,000 globals**
(`GATE_RELEASE_*` in `config.h`, enforced by `build_matrix.sh`). That leaves
**251,146 B of flash and 5,548 B of globals free.**

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

**Five sets of files are on a red line the gate greps**: `src/game/**`,
`networking/{protocol,session}.cpp`, `core/{crc16,rng}.cpp`, `minigames/**` and
`ui/petfx_core.cpp`. An `#include <Arduino.h>` in any of them fails
`tools/check.sh` by name. (`persistence/` and `dev/diag_core.cpp` are
Arduino-free too and host-linked, but they are held by the test Makefile rather
than by a grep.) This is not tidiness — it is what lets 58 host
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
- **Every pin number is one of the seven `#define`s in `core/config.h` §2.** A
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
4. Toast: *«Hola. Soy nuevo aquí.»*
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
fields (spec §49) and twelve commands (spec §66).

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
  name.** This repository has thirty-six recorded instances of an instrument
  that could not fail, or failed on the wrong thing — a gate that counted a type
  name so deleting the line it guarded still passed; a fifteen-point power-cut
  sweep that ran against a code path the shipping build never executes; a
  drawing bug invisible because the file it lived in is compiled by no host
  binary. Every one was found by breaking the instrument, never by the
  instrument. The commit messages in `CHANGELOG.md` name them.
- Ask what the **release artefact** does, not what the build in front of you
  compiles. `GOD_MODE_ENABLED` is 1 on your desk and 0 on the device.
- A persisted or transmitted layout is never edited in place. Add to
  `reserved[]`, or bump the version and add a migration **and a test**.
