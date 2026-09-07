# Pebblebol — project context

**Purpose of this file.** A single briefing an AI assistant can read to talk
about Pebblebol accurately, without opening the repository. It is a *summary of
sources*, not a source: every claim below is traceable to a file named beside it,
and where the repository and this file disagree, **the repository wins**.

**State captured:** `1.0.0-rc1`, 2026-09-07, at `main` — which now carries
phases 1-10 whole, the exploration branch having been merged in commit `124bb44`
while this file was being written.

**The one sentence that must never be softened:**

> **Nothing in this repository has ever run on hardware.** No board exists. The
> firmware compiles clean in six configurations and 59 host test binaries pass —
> none of which is evidence that a device works.

---

## 1. What the product is

A virtual pet on a coin-sized ESP32-C3 behind a 128×64 monochrome OLED with two
buttons. The player cares for one creature, carries the device around, and
**nearby Wi-Fi networks are the wilderness**: a scan seeds encounters, so
creatures are found by walking. Two devices meet over the air to battle and
trade.

The central fiction, from §0 of the product spec:

> A Pebble is a small digital creature — a **computer bug** — trapped in wireless
> networks.
> *"There are bugs hiding in the networks around me. I take mine with me,
> discover them, collect them, and make it stronger."*

The device **never connects** to the networks it scans. It hashes what it sees
and throws the rest away (§44, privacy).

**Naming.** The device is a *Pebblebol*; a creature is a *Pebble*; the collection
is the *Box* (10 slots, exactly one active). The UI is **Spanish**; the code,
comments and documents are **English** (decision D4, closed).

### Scale, in numbers

| | |
|---|---|
| Species | **60** — 20 families × 3 stages |
| Types | **3** — SIGNAL → CORRUPT → SYSTEM → SIGNAL |
| Attacks / items | 34 / 10 |
| Encounter rows / evolution rules | 40 / 40 |
| Levels | 1–30 |
| Box | 10 slots |
| Minigames | 6 |
| Screens | ~20 `ScreenId`s |
| Source | 226 files, ~67,100 lines under `Pebblebol/src/` |
| Host tests | **59 binaries**, ~6.2 M assertions, 75 PBM goldens |

---

## 2. Hardware, and what is undecided

**Target:** ESP32-C3 SuperMini · 0.96" 128×64 OLED (SSD1306 or SH1106) · 2×AAA
through a 3.3 V boost converter · passive piezo · 2 buttons · 3D-printed
sandwich enclosure.

### The pin map does not pass the project's own assertions

This is the first thing to say to anyone about to build one. `core/config.h`
compiles:

| Signal | GPIO | Problem |
|---|---|---|
| `PIN_SDA` | 8 | strapping pin; also the built-in LED on most clones |
| `PIN_SCL` | 9 | strapping pin — low at reset selects serial download mode |
| `PIN_BTN_L` | 10 | |
| `PIN_BTN_R` | **2** | **strapping pin, must be high at reset — and a button is a thing that can be held low** |
| `PIN_LED` | 5 | |
| `PIN_PIEZO` | 3 | decision D8, open |
| `PIN_VBAT_ADC` | 0 | reserved; **no divider fitted**, so there is no battery reading at all |

`config.h` carries seven `static_assert`s that a confirmed map must satisfy. They
are **dormant** because `PB_PINS_CONFIRMED` is never defined. Define it and the
build stops on `PIN_BTN_R`. Genuinely spare GPIOs: **1, 4, 6, 7**.

Archived Spanish documentation (`docs/legacy/README.es.md`) describes a
*different* map — SDA 6, SCL 7, BTN_L 3, BTN_R 4, LED 8. Neither has been on a
board.

Every pin number lives in that one block; `tools/check.sh` fails any other file
that defines a `PIN_` macro.

### Decisions still open at ship (`docs/decisions.md`)

| # | Open question | Consequence |
|---|---|---|
| **D1** | GPIO map | blocks every bench item; deferred by the owner, never revisited |
| **D5** | SSD1306 vs SH1106 | a property of the panel you bought; both drivers link and both are green |
| **D8** | Piezo GPIO | nothing has ever been heard |
| **D10** | Battery divider | not fitted, so §26's NORMAL/LOW/CRITICAL cannot exist; every battery field reads `n/a` with a reason |
| **D11** | Boost module quiescent current | **the single biggest factor in battery life** — a 100× spread between modules |
| **D12** | Bulk capacitor on the boost output | a few cents; the ESP32-C3 pulls ~350 mA in Wi-Fi TX |

Closed and worth knowing: **D2** — the peer transport is **ESP-NOW**, not BLE;
deleting BLE recovered 712,466 B of flash and 23,504 B of static RAM.
**D13** — the "light" mechanic was *deleted*, not defaulted; sleep follows an
approximated daylight table.

---

## 3. Architecture

Ten layers under `Pebblebol/src/`. The rule that organises everything is **which
layers may touch Arduino**:

```
app/         boot, main loop, state machine, first-boot onboarding   (8 files)
ui/          one file per screen, renderer, drawing primitives      (70 files)
game/        the rules — care, XP, battle, breeding, Box,
             evolution, capture, validation.            PURE        (43 files)
networking/  protocol + session (PURE); ESP-NOW, discovery,
             creator server, Wi-Fi drivers (Arduino)               (32 files)
persistence/ save schema, pair discipline, migration. Arduino-free   (9 files)
minigames/   six games, each split logic (PURE) / draw              (7 files)
core/        config, types, RNG, CRC, UTF-8, Spanish string table   (12 files)
data/        GENERATED content tables + the sprite atlas            (14 files)
hardware/    I2C, input, NVS, clock, deep sleep, audio              (15 files)
dev/         serial console and god-mode menu                        (4 files)
```

**The red line.** `src/game/**`, `src/minigames/**`,
`networking/{protocol,session}.cpp`, `core/{crc16,rng,utf8,perf}.cpp`,
`ui/{petfx_core,corrupt_fx,anim_ease}.cpp`, `dev/diag_core.{h,cpp}` and
`hardware/boot_reason.h` may not `#include <Arduino.h>` — and since the P10-C6
exit may not allocate either (`malloc`/`free`/`operator new`), because
`test_soak.cpp` counts `operator new` only, so a balanced `malloc`/`free` pair is
invisible to it *and* to AddressSanitizer. `tools/check.sh` greps for both and
fails by name. This purity is what lets 59 host binaries drive the real rules
against a fake flash, a fake framebuffer and a fake clock, with no board.

**Four conventions a newcomer breaks first** (the first three have gates):

- Navigation is assigned **only** in `app/state_machine.cpp`. No screen sets the
  next screen.
- `src/data/*.h` is **generated** from `tools/content/*.json`. Hand-editing fails
  the gate.
- Every pin number is one of the seven `PIN_` defines in `core/config.h`.
- Every Spanish literal lives in `core/strings_es.h` and reaches the screen as a
  `StrId`. Convention, not a gate.

**The host-test seam** is `ui/gfx.h`: `ui/gfx_u8g2.cpp` on the device,
`tests/fakes/gfx_fb.cpp` on the host — a 128×64 framebuffer with font metrics,
an out-of-bounds recorder and a malformed-UTF-8 recorder. Every screen is driven
at the worst content the product allows (twelve-character all-multi-byte
nickname, level 30, full Box, longest Spanish string) and held to zero
out-of-bounds draws and zero allocations. `test_screens.cpp` has one row per
`ScreenId` with a `static_assert` on its length, so **a screen added without a
fixture fails the build**.

**Files no host binary compiles** — anything including `ui/render.h`, i.e.
`ui/ui.cpp`, `ui/petfx.cpp`, `ui/actfx.cpp`, `ui/ceremony.cpp`, `app/app.cpp`,
`dev/godmode.cpp`. Logic put there is untested by construction; the established
move is a pure module beside it (`ui/petfx_core.cpp`, `app/onboarding.cpp`,
`dev/diag_core.cpp` all exist for that reason).

---

## 4. The game systems

### Care

Five stats, `CareId` order: **HUNGER, HAPPINESS, HEALTH, CLEANLINESS, ENERGY**,
each 0..`PB_CARE_MILLI_MAX` milli-points. Deliberately more relaxed than a
Tamagotchi (§1.1): only the active Pebble needs care, stored Pebbles recover
passively over ~24 h, and §57 requires that ignoring the device for 8 hours never
drops health below 60 %.

### Stats and battle

```
hp_max = 10 + 2*base_hp + level
atk/def/spd = base + level/3 + genome_var(0..2)
raw    = max(1, power * atk_eff / (def_eff * K))          K = 7
damage = max(1, raw * TYPE_MUL_NUM[mod+1] / TYPE_MUL_DEN[mod+1]) + rng(0..2)
```

Base stats are 1..10 with per-stage totals ≈ 16 / 22 / 28. Integer only, no FPU
anywhere in the project.

**Two balance decisions worth knowing, because both were measured and both
overturned the plan's original text:**

1. **The type modifier is multiplicative and capped at one hit per battle.** The
   plan specified a flat `+2*type_mod`; hits land for 4–8 points, so ±2 is a
   50–100 % swing and the triangle decided every fight — measured off-diagonal
   win rates of 88–98 %. It is now multiplicative, applied to the raw term before
   the RNG, and **an attacker gets `TYPE_MOD_MAX_HITS` = 1 type-modified hit per
   battle**, after which its modifier is forced to 0. The fiction fits: you
   exploit the weakness once and then it is patched, and the battle log says
   *¡Exploit!* on that hit and nothing after. With the cap the six off-diagonal
   1v1 cells land at 40–63 %.
2. **Evasion exists so SPD has a second job.** Under the original formulas SPD
   only broke turn order, so a fast Pebble paid 6–7 of its stat points for a
   tiebreak (FAST won 24 % of stage-0 duels, EVASIVE 31 %). Effective accuracy is
   now reduced by `EVASION_PER_SPD` per point of SPD gap, capped at −30. The
   constant was raised 2 → 3 after `balance_matrix.cpp` fought **all 3,600
   ordered pairs 1,000 times each at level 15** through the real battle code: at
   2, SIGNAL/CORRUPT/SYSTEM sat at 42.9/56.8/50.3 %; at 3, 46.6/55.0/48.5 %; at 4
   the correction overshoots. 3 is the measured answer, not the largest one.

Battle is 3-Pebble teams, 1v1 active, turn-based, four moves, switching costs the
whole turn.

### XP and progression

`XP_TABLE` is `10 + L*L`, total 8,845 to level 30, and it lives in
`data/balance.h` **only** — the content pack shipped a second, different curve
and `verify.py` now fails by name if an `XP_TABLE` key reappears in JSON. Sources
are care actions (2), minigames (permille × 8/1000), carrying (+1 per 10 min) and
battle wins (25), each with an hourly or daily cap.

Measured over 30 simulated days through the real `sim.cpp`/`xp.cpp`/
`encounters.cpp`: **18.8 XP/day light play, 109.4 normal, 352.8 heavy.** Level 30
arrives in 25 days of heavy play or 81 of normal, against a 3–4 week target.

### Exploration and encounters

A Wi-Fi scan classifies each network into one of six categories — **UNKNOWN,
HOME, PUBLIC, BUSINESS, OPEN, HIDDEN** — from encryption type, hidden flag, an
RSSI bucket and SSID token lists (`networks.json` carries Spanish ISP/CPE and
venue tokens: `movistar`, `vodafone`, `eduroam`, `invitados`, `renfe`…). The SSID
and BSSID **never leave the function**; what survives is an FNV-1a hash of
`BSSID‖SSID‖device_id`.

Outcomes: `WILD_PEBBLE`, `ITEM`, `SPECIAL_EVENT`, `NOTHING` (≥ 15 %). Cooldown
≈ 2 h per network, timestamped so it survives reboots and a dead battery.

Capture is `CAPTURE_BASE_PERMILLE[rarity]` − 12‰ per level the wild creature is
**above** the active one (never a bonus for being below), + 10 × the capture
item's value, clamped to [50‰, 950‰] — so nothing is certain and nothing is
impossible. Two failures and it flees.

### Corruption — the signature mechanic (§55)

`PBS_CORRUPTED`, 24 hours. It never destroys a Pebble, and it is **the only route
to two of the sixty species** (Errox → Panika, Artefax → Burnix). Effects:
+1 ATK / −1 DEF stage in battle; an `IDLE_CORRUPT` animation set (the blink is
replaced by a one-frame horizontal tear, the walk drops every fourth step); and a
sprite glitch — three 24 px rows XORed with a per-hour noise word, one frame in
eight, seeded from `pebble.id ^ (epoch/3600)`, so the glitch is stable within an
hour and moves between hours. It never touches the stored sprite. Sources: the
SPECIAL encounter, and attack 12 *Infectar* — a battle that ends with the loser
still carrying `EFF_CORRUPT` has a 12.0 % chance of it persisting, which is the
only way a battle can hand you an evolution.

### Breeding

Five compatibility groups (PROTOCOL, BROADCAST, DATA, INTRUSION, FAULT), each
deliberately spanning at least two types. Both parents must share a group, be
stage ≥ 1, level ≥ 15 and generation < 5. Offspring is stage 0 of **one parent's
family** by a fair coin — so cross-family pairing inside a group is the only way
to farm a family whose stage 0 you do not own.

The balance ceiling is three numbers: **base stats are never inherited**, genome
variation is capped at 2 and does not grow with generation, and **exactly one**
move slot crosses over (slot 4, and only if legal for the offspring's type,
power cap and budget).

### The creator (§33)

A phone reaches a PIN-protected local page by scanning a QR off the device's own
screen; Wi-Fi is an AP that exists only while the editor is open. A custom Pebble
is capped at the **stage-1** budget (22 stat points, 185 attack budget) so it can
never out-stat a final evolution, and it is priced by the same
`budget_cost` rule as the built-in roster — never hand-typed.

---

## 5. Content, and how it is generated

`tools/content/*.json` → `tools/gen_content.py` → `Pebblebol/src/data/*.h`.
`tools/content/verify.py` validates the pack against the design rules
independently, importing nothing from the generator. `CONTENT_VERSION` is a
**hash of the pack**, not a counter, stamped into the save header, every
`BattleState` and the session handshake.

### The twenty families

| # | type | family | stage 0 → 1 → 2 |
|---|---|---|---|
| 1 | SIGNAL | PACKET | Paketo → Fragmar → Rafagón |
| 2 | SIGNAL | RADIO | Bippo → Estátic → Jamrón |
| 3 | SIGNAL | LATENCY | Lagui → Jitera → Timaut |
| 4 | SIGNAL | PIXEL | Pixio → Artefax → Burnix |
| 5 | SIGNAL | SPAM | Spamito → Kadenax → Blaklix |
| 6 | CORRUPT | BUG | Buggo → Exploid → Rootkar |
| 7 | CORRUPT | WORM | Wormi → Parasix → Plagón |
| 8 | CORRUPT | PHISH | Karnada → Klonix → Estafex |
| 9 | CORRUPT | NULL | Nulix → Voidina → Segfalt |
| 10 | CORRUPT | BITROT | Bitto → Flipix → Podrix |
| 11 | SYSTEM | DAEMON | Daemi → Servik → Kernon |
| 12 | SYSTEM | FIREWALL | Proxi → Gateón → Murax |
| 13 | SYSTEM | MEMORY | Kachi → Memoro → Lekron |
| 14 | SYSTEM | FILE | Filito → Arkivo → Zipbom |
| 15 | SIGNAL | PING | Pingo → Floodra → Denyra |
| 16 | CORRUPT | GLITCH | Glitchi → Errox → Panika |
| 17 | SYSTEM | PORT | Portu → Skanor → Bakdora |
| 18 | SYSTEM | CRYPTO | Klavik → Cifrax → Ransora |
| 19 | SYSTEM | PROBE | Probix → Beakon → Twinix |
| 20 | SIGNAL | COOKIE | Cookit → Trakkar → Panoptix |

SIGNAL 7 families (21 species), CORRUPT 6 (18), SYSTEM 7 (21). The starter is
**Paketo** — SIGNAL, common, stage 0, BALANCED 4/4/4/4, deliberately the most
ordinary creature the roster allows. First boot offers a choice of three, one per
corner of the type chart: Paketo, Buggo, Daemi.

### The art

`tools/sprites/*.txt` (ASCII, `#` = lit pixel) → `tools/gen_sprites.py` →
`data/sprites_pebbles.h`. **60 bodies × 2 frames × 24×24 = 8,640 B**, flat — all
three stages are the same size. The legacy Nottamagochi atlas (36 addressable
bodies) was deleted in P9-C3; it was the reason the roster was clamped to 36
species, because `sprite_id == id - 1` means species *N* needs the *N*-th body.

The visual language is documented in `docs/creature_style.md`. In one line:
**a solid shape with holes punched in it, that fails a little more at each
stage** — growth is degradation, never scaling. Eyes are punched unlit sockets
because a 1px lit outline does not survive at 1×; the blink bands are *derived
from the art* by the generator rather than hand-tabulated.

---

## 6. Persistence

`SAVE_SCHEMA_VERSION` is **3**. Seven blob types in NVS. Five are **pairs** — Box
header, config, inventory, cooldown table, and each of the ten Pebble slots —
written twice as `<key>0`/`<key>1` with a sequence number. Every write goes to
the copy a reader would *not* pick, then reads back and compares, so a torn or
rotted copy always leaves the other intact (`LOAD_RECOVERED_PAIR`, not data
loss). The two singles are deliberate: a creator species record is content rather
than state, and the trade journal has nothing to choose between.

A load answers with one of seven results and **never destroys a save it could not
read**. `LOAD_CORRUPT` and `LOAD_FOREIGN_NEWER` write nothing; the session goes
read-only and the player is offered *Recuperar* or *Reset de fábrica* behind two
confirmations.

### `nvs2` is the whole point of the partition table

```
nvs      0x9000   0x5000     otadata  0xE000   0x2000
app0     0x10000  0x300000   nvs2     0x310000 0x10000
spiffs   0x320000 0xD0000    coredump 0x3F0000 0x10000
```

Arduino's `initArduino()` erases the **entire default `nvs` partition** when
`nvs_flash_init()` reports no free pages or a new version — *before `setup()`
runs*, so no care in this firmware can prevent it. `nvs2` is a private NVS
partition the core never touches; the checkpoint written there turns that erase
from "the creature is gone" into `LOAD_RECOVERED_CKPT`.

`PartitionScheme=huge_app` stays in the FQBN even though the CSV decides the real
table, because the board menu still supplies `upload.maximum_size` — dropping it
leaves the check at 1,310,720 B and fails a build that fits `app0` fine.

All of this is proved against a RAM NVS with fault injection
(`tests/fakes/kv_mem.cpp`). **A byte has never reached a real one.**

---

## 7. Radio

**ESP-NOW**, not BLE (decision D2). `docs/protocol.md` is the contract: a wire
Pebble is **48 bytes**, the state table is lockstep with three invariants, and §7
of that document enumerates what a malicious peer can still do. Trades are
atomic and journalled.

The Wi-Fi scan never calls `WiFi.begin()` — the gate greps for it. The creator's
access point is **opt-in**: `CF_WEB_ENABLED` is clear on a fresh device, so the
radio is off by default.

---

## 8. Build, test and the gate

| Command | What | Time |
|---|---|---|
| `make -C tests check` | 59 host binaries, ~6.2 M assertions | ~40 s |
| `make -C tests asan` | outside-input path under ASan, 8 binaries | ~20 s |
| `tools/build.sh` | one firmware build, 0 warnings enforced | ~90 s |
| `tools/build_matrix.sh` | six variants **and the release size caps** | ~9 min |
| `tools/check.sh` | **the gate** — the above minus the matrix, plus 159 named gates | ~3 min |

> **A number the README gives twice, differently.** §4 says 58 host binaries and
> the repository map says 59. `tests/Makefile` settles it —
> `TESTS := $(patsubst %.cpp,$(BIN)/%,$(sort $(wildcard test_*.cpp)))` over 59
> matching files — so **59** is the count used throughout this document.

The gate ends in exactly one line: `GATE OK` or `GATE FAIL: <what, and why it
matters>`. Everything is seeded through `rng_seed_all(0xC0FFEE)` so a failure
reproduces.

### Sizes at this commit

| Variant | Flash | Globals |
|---|---|---|
| **release** (`GOD_MODE_ENABLED=0`) | **1,350,840** | **59,452** |
| baseline | 1,367,774 | 59,548 |
| no-web | 1,254,122 | 55,172 |
| all-off | 605,298 | 26,724 |

Caps are 1,600,000 flash and 65,000 globals → **249,160 B and 5,548 B free**.

**Two build traps.** `--build-path` is not optional: without it `build.sh`
compiles into a `mktemp -d` it deletes on exit, prints `BUILD OK`, and leaves no
binary — and an operator who then runs a plain `arduino-cli compile` flashes the
**baseline** image (god console, `spawn` over the cable) believing it is
`release`. And always quote a size or a timing **against the variant it was
measured on**.

---

## 9. What is actually proven, and what is not

This is the section an assistant most needs, because the repository is unusually
green and unusually unproven at the same time, and confusing the two is the
easiest way to mislead someone.

### Eighteen of the spec's §67 acceptance boxes are open

- **Sixteen** because nobody has watched a device do the thing.
- **One** (`Device boots reliably`) waits on decision D1, the pin map.
- **Two** — breeding compatibility, and generated Pebbles staying balanced —
  wait on a module that **was never written**: `networking/breed_link.cpp` was
  planned in P7-C5 and does not exist. `DISC_CAP_BREED` is not claimed and the
  LINK screen's *CRIAR* row answers *«Aún no está listo»* on purpose. The
  underlying `game/breeding.cpp` rules are complete and swept over all 36×36
  roster pairs; it is the **link** half that is missing.

"Two-button input is robust" was **unticked at the final review**: the 5 ms
sampler and edge ring are behind `#if defined(ARDUINO)` and are compiled by no
host binary, so `nm` over `bin/test_input` finds neither. The 15/15 in
`test_input` is real, and it is about the state machine, not the sampler.

### `tests/fakes/SHADOWS.txt` — read this before trusting a green run

**Fifty shipping functions are shadowed by host fakes.** Every host binary that
links a fake drives the *fake's* body while a reader believes it drives the
firmware's — and **the linker can never tell you**, because all fifty shipping
bodies live in translation units no host binary compiles, so no object ever
defines them and there is never a collision. Two of the defects this product
shipped were exactly that.

`SHADOWS.txt` enumerates and classifies the set; `check.sh` §10 re-derives it
from source and fails when it differs. Each row says whether the fake *claims* to
imitate the shipping body (and which running assertion holds that claim) or is
deliberately something else. A row that claims imitation and names nothing is
refused.

### Three defects the final review found, each worth a day to an owner

- The two-board block was dead for anyone who typed an **accented name** at first
  boot: the beacon carried the drawn UTF-8 form into a field that is Latin-1 by
  wire contract, `disc_encode()` refused it, and the board emitted **no beacon at
  all**, silently.
- The phone block was dead for **everyone**: the power ladder tore the access
  point down after 120 s with no *button* pressed — which is exactly what using a
  phone looks like — 180 s before the portal's own timer.
- `rx_wrong_peer` had **no reader anywhere in the tree**.

All three are fixed and all three now fail by name in the host suite.

### The lesson the repository keeps re-learning

`README.md` §11 states it as one question. Not *"can this guard fail?"* but
**"is that the whole set?"** Recorded instances: a gate that counted a type name,
so deleting the line it guarded still passed; a fifteen-point power-cut sweep run
against a code path the shipping build never executes; a drawing bug invisible
because its file is compiled by no host binary; a completeness assertion that
counted table rows instead of enum members, so it fired on the correct fix and
stayed silent on the bug. **Every one was found by breaking the instrument, never
by the instrument.**

The final review added a fourth question: **what does this claim rest on one call
deep?** `ui_pet_name_latin1()`'s whole body is `{ pet_name_stored(out, cap); }`,
so a hash or a grep of that body is blind to the function doing the work.

And: **a comment that overstates a guard is worse than no comment**, because it
is what stops the next reader adding the real one.

---

## 10. First boot, exactly

1. Splash with the product name and `FW_VERSION`.
2. Load: nothing on flash → `LOAD_FRESH`. Clock stays `CAL_UNSET`.
3. A starter Pebble is minted into slot 0 and made active **before the player is
   asked anything**, so a player who reads nothing still ends up with a working
   device.
4. **No toast.** The boot greeting is suppressed while setup is pending. The
   reason is a bug fix, not a preference: the toast band is rows 45–55 and both
   setup screens' instruction lines live inside it, so for `UI_TOAST_MS` the only
   line telling a new player how to save was completely covered.
5. Three questions, in order: **name the device, set date and time, pick one of
   three starters**. L taps to advance, L held accepts, R changes the value, R
   held repeats. Any question can be skipped; holding both ends the flow.
6. HOME.

**The step is persisted, not inferred** — two config bits say which question the
device is on, so pulling the power mid-flow returns to the *next* question.
`OB_DONE` is zero, so every save written by any earlier firmware decodes as
"already set up": a device played for months must never be handed a wizard.

---

## 11. Diagnostics

The **release** artefact prints two CSV headers at boot and one row of each every
60 s, for ever, on every screen:

```
DIAG#,heap,uptime_s,free_b,min_free_b
DIAG#,perf,uptime_s,scr,frames,frame_max_us,frame_worst_us,frame_worst_scr,...
```

and answers four commands at 115200 baud: `help`, `info`, `show_save`,
`stall <ms>`. That is the whole of the shipping build's diagnostics.

`baseline` adds the god-mode menu, a twelve-field DIAG screen and eleven of §66's
twelve commands. **`help` is the only always-compiled output that differs between
the two artefacts** — `info` cannot name the variant, because `FW_VERSION` is
`1.0.0-rc1` on both. If `help` lists `spawn`, you are on `baseline`.

**Expect to lose the boot banner.** `Serial` is HWCDC with a 256-byte TX ring
that discards the *oldest* bytes when the host has not enumerated, and
`app_setup()` prints ~690 B. Have the monitor open and *then* reset the board;
native USB CDC has no DTR auto-reset, so opening the monitor does not restart it.

---

## 12. Repository map

```
README.md                  1,149 lines; §1 pin map, §2 the bench list
CHANGELOG.md               English, Keep a Changelog, one entry per phase
PEBBLEBOL_IMPLEMENTATION_PLAN.md   the plan every commit is answerable to
PEBBLEBOL_IMPLEMENTATION_AUDIT.md  the audit of the v1 codebase this replaced
Pebblebol/                 the sketch: Pebblebol.ino + src/ + partitions.csv
tests/                     59 binaries, fakes, fixtures, 75 PBM goldens
  fakes/SHADOWS.txt        the fifty shadowed functions — read before trusting green
tools/                     build.sh · check.sh · build_matrix.sh · generators
  content/                 the JSON pack + verify.py + balance_report.py
  sprites/                 65 ASCII art files + atlas.txt + README.md
web/creator/               the phone page, generated into src/data/index_html.h
docs/
  bench.md                 what a person with two boards and a phone must run
  decisions.md             the decision log; five decisions still OPEN at ship
  save_schema.md           every byte that reaches flash
  protocol.md              the radio contract
  budget.md                where the flash and the RAM went
  content.md               the content pack and how to regenerate it
  creature_style.md        the visual language of the sixty bodies
  PEBBLEBOL_PRODUCT_SYSTEM_SPEC.md      the product spec (§ cited throughout)
  PEBBLEBOL_HARDWARE_AND_BATTERY_SPEC.md
  legacy/                  the Spanish documents this one replaced
```

---

## 13. Glossary

| Term | Meaning |
|---|---|
| **Pebble** | one creature |
| **Pebblebol** | the device |
| **Box** | the 10-slot collection; exactly one slot is active |
| **the gate** | `tools/check.sh`; ends in `GATE OK` or `GATE FAIL: …` |
| **the pack** | `tools/content/*.json`, the generated source of all content tables |
| **the atlas** | the ordered sprite list; a body's slot is `sprite_id == id - 1` |
| **pair discipline** | writing a blob to `<key>0`/`<key>1` with a sequence number |
| **the red line** | the modules that may not include Arduino or allocate |
| **shadowed function** | a shipping function a host fake stands in for; see `SHADOWS.txt` |
| **release / baseline** | the shipping artefact vs the dev build with god mode |
| **`nvs2`** | the private checkpoint partition Arduino's core never erases |
| **P*n*-C*m*** | phase *n*, chunk *m* of the implementation plan |

---

## 14. Answering questions about this project

Rules that keep an assistant accurate here:

1. **Never say a feature "works".** Say it compiles, or that N host tests cover
   it, and name what has not been observed. Eighteen §67 boxes are open and no
   board exists.
2. **Distinguish "planned" from "built".** The plan is 637 KB and describes work
   in both states; `breed_link.cpp` is the standing example of a planned module
   that does not exist.
3. **Check the branch before quoting a file.** `main` carries phases 1-10 as of
   `124bb44`, but this project has run several long-lived branches at once and a
   statement true of one is routinely false of another — the sixty bodies did not
   reach `main` until that merge, and `claude/bicho-brand-identity-6pxmvd` still
   holds two superseded design documents that were written against the older
   tree and should not be quoted.
4. **Quote sizes and timings with their variant.** `release` ≠ `baseline`.
5. **Treat a green test as a claim about a *set*.** Ask what it enumerates, and
   whether a host fake shadows the thing it appears to exercise.
6. **The decisions log is authoritative** for anything hardware-shaped, and its
   status column is more current than any prose summary — including this file.
7. **Do not invent creature names, stats or constants.** The roster is sixty
   fixed names and the balance constants are measured values with recorded
   evidence; a plausible-sounding invention is worse here than an admission of
   not knowing.
