# Flash and RAM budget through phase 10

Written after the phase-4 exit (`v0.4.0-battle`, `ab7b8d3`), to answer one question:
**does everything that is left fit?** Every figure below was produced by a build run at
the time of writing, not quoted from a commit message.

    tools/build.sh --quiet [--define MACRO=VALUE ...]

## 1. The caps, and which build they police

`core/config.h` sets `GATE_FLASH_MAX 2,400,000` and `GATE_GLOBALS_MAX 90,000`, and
`tools/check.sh` enforces them against the **baseline** variant — everything switched on,
including two subsystems V1 will not ship.

| build | flash | globals | what it is |
|---|---|---|---|
| baseline | 1,915,654 | 72,676 | every feature on, incl. BLE and the legacy web UI |
| release (`GOD_MODE_ENABLED=0 FEATURE_BLE=0`) | 1,191,426 | 49,004 | **the build that gets flashed** |
| no radio at all | 515,268 | 24,408 | the game by itself |

So the gate reports 80 % of flash and 81 % of globals consumed, while the artefact a player
would actually run sits at 50 % and 54 %. **For six more phases the number under pressure
would have been one that corresponds to nothing shippable.**

## 2. What each removable subsystem costs, measured

Against the 515,268 / 24,408 no-radio floor:

| subsystem | flash | globals | status |
|---|---|---|---|
| BLE (`ble_social.cpp`, 1,211 lines) | **712,618** | **23,496** | decided dead, **still here** — re-measured at the P7-C6 exit; blocked on a two-board bench test |
| Wi-Fi stack + HTTP server (`FEATURE_WEB`) | ~~676,158~~ **61,174** | ~~24,596~~ **2,072** | **STALE SINCE P7-C1, RE-MEASURED AT P8-C2 — see the note below** |
| god mode | 11,884 | 168 | dev only, already off in release |

The two radio stacks do not add: turning both off lands exactly on the 515,268 / 24,408 floor,
and ESP-NOW currently costs nothing because P7-C1 has not built it yet.

**THE `FEATURE_WEB` ROW WAS OFF BY A FACTOR OF TWELVE AND P8-C2 RE-MEASURED IT.** `baseline`
minus `no-web` at the P8-C1/C2 commit is **61,174 flash / 2,072 globals**, not 676,158 / 24,596.
The row was correct when it was written and stopped being correct at P7-C1, which made
`net.cpp`'s `NT_NET_WANT_WIFI` read `(FEATURE_WEB || FEATURE_ESPNOW)`: since then `FEATURE_WEB=0`
removes the HTTP half and leaves the whole Wi-Fi driver linked for ESP-NOW. Anyone budgeting
phase 8 off the old row would have believed the server costs 24 KB of globals and either
panicked or traded something away for nothing. **The number to quote is 61,174 / 2,072, and the
variants it is a difference of are `baseline` and `no-web`.** The old figure is struck rather
than deleted so the next reader can see that a variant changed meaning underneath a table — the
same failure §8 records.

**P7-C1 HAS NOW BUILT IT, AND THE LAST CLAUSE IS OBSOLETE — see §4 below.** ESP-NOW costs
**+9,758 flash and +5,832 globals** on the baseline, five sixths of the globals being one
buffer whose size was measured rather than guessed. The BLE row is unchanged: `ble_social.cpp`
is untouched by P7-C1 and its deletion is gated on two boards having linked, which no
environment without hardware can do.

## 3. What phases 5-10 will cost

Measured history, baseline deltas:

| phase | chunks | flash | globals |
|---|---|---|---|
| 3 (pet) | 5 | +11,696 | +72 |
| 4 (battle) | 6 | +22,582 | +2,328 |
| 5 (exploration), C1+C2 | 2 of 4 | -1,572 | -112 |
| 5 (exploration), C3+C4 | 2 of 4 | **+13,854** | **+616** |
| **5 total so far** | **4 of 4 built** | **+12,282** | **+504** |

**Phase 5's first two chunks are NEGATIVE on both axes, and that is a rebate rather than a
discount.** P5-C1 deleted the Wi-Fi station path - credentials, association, retry backoff,
link-loss re-association, `net_rssi()`, `net_is_sta_up()`, the creator screen's station
branch and its golden - and what it added (a passive scanner, a pure classifier, an 88-row
token table and the cooldown module) is smaller than what went. Measured, baseline:
1,915,654 / 72,676 -> **1,914,082 / 72,564**. All seven matrix variants moved the same way;
`release` is 1,191,426 / 49,004 -> **1,190,000 / 48,916**.

**THE BILL P5-C1/C2 LEFT UNPAID IS NOW PAID, AND IT CAME IN AT THE PROBE'S FIGURE.**
Nothing in the firmware CALLED the scanner or the cooldown table until P5-C3, so
`--gc-sections` dropped both from the image those figures measured. The probe that wired a
`WifiScanJob`, `net_scan_driver()` and `cd_ready`/`cd_arm` over the existing `GameState.cds`
predicted **+3,840 flash and +408 globals** the moment anything called them, and the 408 was
attributed as 264 (game/cooldowns.cpp's per-boot RAM table) + 136 (one `WifiScanJob`) + 8
(net.cpp's scan salt and intent byte).

**MEASURED AT P5-C3/C4: 1,914,082 / 72,564 -> 1,927,936 / 73,180, i.e. +13,854 flash and
+616 globals.** The globals figure is the probe's 408 plus 208 for everything the two new
screens and the inventory hold in their own right - the ENCOUNTER transient's 8 B
`EncounterResult` and its `CaptureState`, the NETWORK screen's phase bytes, the CARE
screen's bag mode and cursor, and `game/inventory.cpp`'s 4 B armed battle modifier. The
flash is the scanner and the cooldown module no longer being collected, plus four new game
modules (encounters, capture, inventory, corruption), two new screens, a payload table and
about forty strings.

**Against the phase-5 line of 1.0-2.0 KB of globals, phase 5 has spent 504 net** (the
P5-C1 rebate of -112 against this +616), so exploration finishes inside half of its own
allowance. `release` moved 1,190,000 / 48,916 -> **1,203,808 / 49,508**, which is
75 % of `GATE_RELEASE_FLASH_MAX` and 76 % of `GATE_RELEASE_GLOBALS_MAX`.

Phase 4's globals went almost entirely to ONE chunk — P4-C4, the battle screen and its
renderer, at +18,780 / +2,304. That is the shape: **code and data cost flash; screens and
buffers cost globals**, and globals is the scarce one.

Phases 5-10 are 26 chunks and about ten new screens (SCAN, ENCOUNTER, INVENTORY, ACTIVITY,
LINK, TRADE, BREEDING, PIN, QR, and the DIAG expansion). None is as heavy as the battle
screen, which carries a renderer and a 212 B engine state.

**AND THE FIRST THREE OF THOSE TEN ARE IN, WHICH IS EVIDENCE FOR THAT SENTENCE RATHER THAN
A RESTATEMENT OF IT.** SCAN, ENCOUNTER and the inventory surface together hold **344 B of
globals** - the +616 less the 264 the cooldown table keeps and the 8 `net.cpp` keeps - and
**136 of that 344 is one `WifiScanJob`, which is a BUFFER and not screen state**, leaving
208 for the three surfaces' own fields. The battle screen's +2,304 remains the outlier by
an order of magnitude and the projection below is unchanged.

**This paragraph said "+208 B ... 136 of it one `WifiScanJob`" until the phase-5 exit, and
that was this project's own signature defect: a ledger that does not add up.** The 136 was
inside the 408 of the paragraph two above AND inside the 208 of the paragraph above it,
while the document also said 408 + 208 = 616 - so one of the three had to be wrong, and the
sentence offered as EVIDENCE for the phases 6-10 projection gave 208 B where the measured
figure is 344 - 65 % more than it said. Which one was wrong was MEASURED, not reasoned about, twice and independently.
**(1)** `tools/build.sh --quiet --variant probe8 --define WIFI_SCAN_MAX_RESULTS=8` gives
`flash=1927936 globals=73116` against the baseline's 73,180: exactly **-64 B**, i.e. 8 fewer
`ScanResult` rows of 8 B in exactly **one** linked `WifiScanJob`, so the 136 is in the +616
once and not twice. **(2)** `riscv32-esp-elf-nm -S` over the sketch objects attributes the
+616 symbol by symbol, and the two halves agree to the byte:

| object | globals (B) | what holds them |
|---|---|---|
| `game/cooldowns.cpp` | 257 | `s_ram[32]` 256 + `s_dirty` - the per-boot RAM table |
| `ui/screen_care.cpp` (bag mode) | 178 | `bag_render()::rows` 176 (8 x 22 list text) + mode + cursor |
| `ui/screen_network.cpp` | 143 | **`s_job` 136** + `s_t0` 4 + three phase bytes |
| `ui/screen_encounter.cpp` | 19 | `s_enc` 8 + `s_cap` 4 + `s_reward` 2 + five flag bytes |
| `networking/net.cpp` (new) | 5 | `s_scan_salt` 4 + `s_want_scan` |
| `game/inventory.cpp` | 4 | `s_mod`, the armed battle modifier |
| `game/{encounters,capture,corruption}.cpp` | **0** | pure logic, no file-scope state |
| **symbols** | **606** | |
| link alignment | 10 | |
| **measured image delta** | **616** | |

**The two attributions reconcile exactly, which is the check that makes them worth
printing.** 143 + 178 + 19 + 4 = **344** is the three surfaces' own columns; 257 + 5 = 262
is what the two modules behind them keep in symbols, and the 10 B of link alignment falls
entirely on those two (257 -> 264, 5 -> 8), which is why the probe's link-level figures are
264 and 8 where `nm`'s symbol sums are 257 and 5. Either way round the surfaces hold 344.
The CARE bag's 176 B row cache is the single largest object of the three - larger than the
scan buffer - and nothing in the earlier prose named it at all, which is the other half of
why this enumeration was worth measuring instead of restating.

| item | flash | globals |
|---|---|---|
| ~~P5 scanner, encounters, capture, items~~ **SPENT: +12,282 / +504** | ~~15-25 K~~ | ~~1.0-2.0 K~~ |
| ~~P6 activity score, power states~~ **PHASE 6 CLOSED AT `v0.6.0-activity`: +20,866 flash / +400 globals** (baseline 1,927,936/73,180 -> 1,948,802/73,580; release 1,203,808/49,508 -> **1,224,210/49,924** = 76.5 % and 76.8 % of the caps `build_matrix.sh` enforces). **THE FLASH LINE WAS OVERRUN BY 74 % AND THE SCARCE LINE WAS NOT**, and the reason is worth keeping: about 17 K of the 20.9 K is two ESP-IDF drivers arriving in the tree for the FIRST time - LEDC with the first PWM output (~7.5 K, P6-C1) and `esp_sleep` with the first `esp_light_sleep_start()` (~9.6 K, P6-C3). Both are paid once and reused: a later LED effect and a deep-sleep rung link no new driver. Pebblebol's own phase-6 code is roughly 3.8 K. Per commit: P6-C1 +9,028/+136 (tone engine + motion capability), P6-C2 +1,940/+72 (activity score; its PERSISTED half costs 0 - four bytes that were already `CooldownTable.reserved_a[4]`), P6-C3 +9,698/+192 (power ladder + sleep-correct clock; ~148 B of the 192 is ESP-IDF's own sleep state, ~44 B is Pebblebol's), P6-C4 +200/+0 (the exit's three fixes, one gate, documents). **375,790 B of release flash and 15,076 B of release globals remain** against an 80-115 K / 3.7-8.0 K forecast for P7-P10. | ~~8-12 K~~ | ~~0.3-0.8 K~~ |
| P7 ESP-NOW transport, link/trade/breeding | 25-35 K | 1.5-3.0 K |
| ~~P8 PIN, creator routes, mobile page + sprite editor (PROGMEM)~~ **PHASE 8 CLOSED: +57,274 flash / +2,592 globals** on `release` (1,269,126 / 56,804 at `34aab11` -> **1,326,400 / 59,396** = 82.90 % and 91.38 % of the caps `build_matrix.sh` enforces). **THE FLASH LINE WAS OVERRUN BY 27 % AND THE SCARCE LINE WAS NOT**, and §§9-12 quoted the forecast they passed while saying nothing about the one they missed - which is the selective reading §8 was written to stop, repeated in the sections that cite §8. The overrun has ONE address: `data/index_html.h` grew from a 1,016 B placeholder to the 42,248 B committed creator page, +41,232 B of `.rodata`, and the served schema document is another 1,172 - so 42,404 of the 57,274 is DOCUMENTS, priced in §11 with the gzip lever (47,181 -> 17,947 B on this project's previous phone page) already costed if a later phase needs it back. Pebblebol's own phase-8 code and strings are **14,870 B**, inside half the forecast band on their own. Per commit, `release`: P8-C1/C2 +2,574/+16, P8-C3 +13,046/+2,576 (2,060 of the globals is `CS_BODY_MAX`), P8-C4 +41,658/+0 (all `.rodata`), P8-C5 +26/+0, P8-C6 -30/+0. **273,600 B of release flash and 5,604 B of release globals remain** against a 25-35 K / 0.7-2.0 K forecast for P9-P10. | ~~30-45 K~~ | ~~1.5-3.0 K~~ |
| P9 roster 36→60, sprite atlas, corruption | 15-20 K | 0.2-0.5 K |
| P10 diagnostics, animation, polish | 10-15 K | 0.5-1.5 K |
| **total** | **105-150 K** | **5-11 K** |

### The projection

| ending state | flash | globals | headroom |
|---|---|---|---|
| BLE kept | ~2,045,000 (85 %) | ~80,700 (90 %) | **9 KB of globals** |
| BLE deleted | ~1,333,000 (56 %) | ~57,200 (64 %) | 32 KB of globals |

**It fits either way on flash. On globals it fits comfortably only if BLE goes.**

**RE-SCORED AGAIN AT THE PHASE-8 EXIT (P8-C6), because the phase-6 re-scoring below is now
past on the axis it said was tight.** `release` today is **1,326,400 / 59,396**. Adding §3's
P9-P10 forecast (25-35 K flash, 0.7-2.0 K globals) gives an ending state of
**1,351,400-1,361,400 flash** (84.5-85.1 % of the cap, 238,600-248,600 B spare) and
**60,096-61,396 globals** — **92.5-94.5 % of the 65,000 cap, leaving 3,604-4,904 B.** The
phase-6 re-scoring below projected 53,624-57,924 globals; the artefact passed the TOP of that
band during phase 8 and is 1,472 B past it today. The conclusion the original table was drawn
to support still holds — it fits, and it fits because BLE went — but it fits with about 4 KB of
margin rather than 7, so **phase 9 and phase 10 are the first two phases in this project with
no room to be wrong by a buffer.** The row to watch is not a repeat of `CS_BODY_MAX`: that one
is spent and is now 36.8 % of what is left. It is any NEW buffer, and §10's instruction stands
— check this table before writing one.

**RE-SCORED AT THE PHASE-6 TAG (P6-C4), because this table still carries its phase-4-era
ending state and phase 6 overran its flash line.** Release today is **1,224,210 / 49,924**.
Adding the P7-P10 forecast of 80-115 K flash and 3.7-8.0 K globals gives an ending state of
**1,304,210-1,339,210 flash** and **53,624-57,924 globals** — against the projected
~1,333,000 / ~57,200 in the "BLE deleted" row. So flash lands inside the projection and the
top of the globals band now lands roughly **0.7 KB past it**, still at **~89 % of the 65,000
cap**. The conclusion the table was drawn to support is unchanged, and BLE is already off in
`release`. One line, not a rewrite: the projection was drawn before phase 6 linked two
ESP-IDF drivers, and it survived that.

## 4. Three things to change, in order of value

### 4.1 Delete BLE — and the reason it is not automatic

**712,618 B of flash and 23,496 B of globals** (re-measured at the P7-C6 exit: baseline minus
`no-ble`, 1,994,460 − 1,281,842 and 80,492 − 56,996), more than the entire remaining headroom.
Decision **D2 closed on ESP-NOW** (2026-09-02, owner), and the plan's own inventory says
`ble_social.cpp` "becomes `transport_ble.cpp` + discovery **if D2 = BLE**" — which it is not.

The reason this is a decision and not a cleanup: **ESP-NOW has never run on the board.**
It is chosen on paper (250 B frames, unicast with a send-callback ACK, the same `WIFI_STA`
residency the §40 scanner needs); P4-C5 proved the protocol over a host loopback and P7-C1
built the radio behind the same seam, both with no radio at all. Deleting BLE removes the only
fallback transport before the chosen one has been demonstrated on hardware.

**STATUS AT v0.7.0-social: STILL NOT DELETED, ON PURPOSE.** Phase 7 built ESP-NOW and did not
run it. The order is unchanged and the remaining step is one bench test: **flash two boards,
open LINK on both, confirm each lists the other** (`link_qualified_count()` reaching 1 — three
beacons at 500 ms above `LINK_RSSI_MIN`). If it passes, `ble_social.{h,cpp}` goes,
`FEATURE_BLE` goes, `no-ble` becomes the baseline and `BlePeerInfo` leaves
`core/nt_types.h` — and this 712,618 B is the single largest lever left in the budget.

Until then the budget should be read against the release build, which already excludes it.

### 4.2 The gate should police the build that ships

`check.sh` builds only the baseline, so the release figures are seen only when someone runs
`build_matrix.sh`. The caps belong on the artefact that gets flashed. Two options, and the
second is cheaper: add a release build to `check.sh` (+90 s on every gate run), or enforce a
release cap inside `build_matrix.sh`, which already builds that variant. **Done: the matrix
now enforces `GATE_RELEASE_FLASH_MAX` / `GATE_RELEASE_GLOBALS_MAX`.**

### 4.3 The sprite budget: the transition allowance is gone, the end state is measured

**Closed at P9-C3.** `data/sprites.h` carried `static_assert(SPRITE_DATA_BYTES <= 24576)` — a
TRANSITION allowance sized for the one window in which both rosters were in the tree at once.
Both rosters are no longer in the tree: the 36 legacy Nottamagochi body and pose sets are
deleted, the atlas is `data/sprites_pebbles.h`, and the ceiling came down to the end state plus
a stated margin.

| line | bytes | what it is |
|---|---|---|
| generated atlas | **9,216** | 2 eggs + SLEEP + SICK + 60 species bodies, every one 24x24x2 at 144 B (`PB_SPRITE_DATA_BYTES`) |
| survivors | **1,031** | `spr_icon12` 384 + `spr_mini8` 96 + `spr_badge12` 312 + the twelve emotes 239 |
| **end state** | **10,247** | `SPRITE_DATA_BYTES_DECLARED` |
| margin | 1,017 | seven more 24x24x2 sets (1,008 B): one more three-stage family and four effect sets |
| **ceiling** | **11,264** | `SPRITE_DATA_BYTES_MAX`, the two rounded up to 11 KiB |

That is 0.47 % of the 2,400,000 B flash cap and 4.1 % of the 273,600 B the release variant had
spare at 37511d5. There is a SECOND, TIGHTER ceiling in front of it:
`tools/gen_sprites.py`'s `PB_DATA_BYTES_MAX = 10240` covers the generated atlas alone, so a
runaway is refused by the generator — with a message naming the overrun — before it reaches
either static_assert. Raising them is a re-plan and both have to move together.

**Three numbers in this section used to be wrong and all three were fixed by measurement, not
by re-reading.** (1) It said 10,623 B, because `SPRITE_DATA_BYTES` was a hand-typed literal
guarded by `static_assert(<literal> <= <literal>)` — a comparison that cannot fail from an art
change, and which had already failed to notice P4-C6 deleting `spr_mood12` (144 B). P9-C1 made
the compiler walk the tables. (2) The 1,175 B of "everything else" double-counted the two egg
sets. (3) The predicted end state was 9,959 B; the real one is 10,247 B, because P9-C3 kept
TWO generic pose sets (SLEEP and SICK, 288 B) rather than none — see `data/sprites.h`'s LOOKUP
banner for why, and what dropping EAT's art cost instead.

**What the whole swap actually cost, measured on the release variant
(`GOD_MODE_ENABLED=0`).** Baseline at 37511d5 was 1,326,400 B flash / 59,396 B globals.

| step | flash | globals |
|---|---|---|
| the art swap alone (60 bodies in, 38 legacy sets out, eye bands generated, `PF_MAX_W/H` 40 -> 24) | 1,326,406 (**+6**) | 59,044 (**-352**) |
| the roster 36 -> 60 species (tables, evolution rules, 48 more Spanish strings) | 1,328,090 (**+1,684**) | 59,044 (**0**) |
| **P9-C3 total** | **+1,690** | **-352** |

Sixty bodies cost SIX BYTES net, because the legacy atlas they replace was almost exactly the
same size (9,448 B of sets against 9,216 B, plus a wider table and the generated eye bands).
The globals line moved DOWN, and every byte of it is accounted for: `PF_MAX_W/H` fell from 40
to 24 because every body is now 24x24, so `ui/petfx.cpp`'s two decode caches went from
2 x 200 B to 2 x 72 B and its four lid strips from 4 x 60 B to 4 x 24 B — exactly 352 B, and it
is a gift to phase 10 rather than a cost.

## 5. What is NOT in this budget

- **Heap.** Nothing here measures it. The device has ~180 KB of free heap at boot and the
  Wi-Fi and ESP-NOW stacks allocate from it at runtime; the AP + HTTP server in P8 is the
  largest consumer and is unmeasured on hardware.
- **The device-side frame budget.** P4-C4's "no per-frame heap" was measured on the host
  over 3,942 frames; it has never run on the board.
- **Battery life**, which decision D11 says is dominated by the boost module's quiescent
  current — a hardware measurement nobody has taken.

## 4. Phase 7, chunk 1 — the peer link (measured 2026-09-05)

All seven matrix variants, `v0.6.0-activity` -> P7-C1:

| Variant | flash before | flash after | Δ flash | globals before | globals after | Δ globals |
|---|---|---|---|---|---|---|
| baseline | 1,948,802 | 1,958,560 | +9,758 | 73,580 | 79,412 | +5,832 |
| no-ble | 1,236,440 | 1,246,000 | +9,560 | 50,084 | 55,916 | +5,832 |
| no-web | 1,320,114 | 1,900,532 | **+580,418** | 52,620 | 77,364 | **+24,744** |
| no-god | 1,936,436 | 1,946,186 | +9,750 | 73,404 | 79,236 | +5,832 |
| sh1106 | 1,948,802 | 1,958,560 | +9,758 | 73,580 | 79,412 | +5,832 |
| all-off | 547,274 | 547,274 | **0** | 25,324 | 25,324 | **0** |
| **release** | **1,224,210** | **1,233,762** | **+9,552** | **49,924** | **55,740** | **+5,816** |

**TWO ROWS ARE NOT WHAT THEY LOOK LIKE AND BOTH ARE EXPLAINED IN `docs/decisions.md`.**
`no-web`'s half-megabyte is NOT a phase-7 feature: `net.cpp`'s `NT_NET_WANT_WIFI` became
`(FEATURE_WEB || FEATURE_ESPNOW)`, so that variant now links the whole Wi-Fi driver it used
to compile out. `all-off` did not move at all because `FEATURE_ESPNOW 0` compiles the driver
to refusal stubs and `--gc-sections` drops the pure modules nothing calls — it changed
MEANING (a define that used to be silently skipped is now applied) without changing size.

### Where the 5,832 B of globals went, symbol by symbol

`riscv32-esp-elf-nm -S` over the sketch objects:

| symbol | bytes | what it is |
|---|---|---|
| `s_rx_bytes` | 5,248 | the session ring, `LINK_RX_SLOTS` (32) x `PROTO_FRAME_MAX` (164) |
| `s_bc_bytes` | 256 | the beacon ring, 8 x 32 B |
| `s_rx_lens` | 64 | the session ring's per-slot byte counts |
| `s_slot_addr` | 48 | **the only place a peer's hardware address exists in this firmware** |
| `s_st` | 44 | `EspNowStats`, eleven counters for the DIAG screen |
| `s_rx`, `s_bc` | 40 | the two `RxRing` headers (cursors, stride, counters) |
| `s_bc_lens` | 16 | the beacon ring's per-slot byte counts |
| `s_slot_used`, `s_bound_addr`, `s_ack_ms`, four flags | 22 | the bound peer and the slot table's occupancy |
| `net.cpp`'s `s_want_link` | 1 | the third intent on the Wi-Fi stack |
| **symbols** | **5,739** | all of it in `networking/transport_espnow.cpp` bar one byte |
| unattributed | 93 | link alignment and section padding; not chased further |
| **measured image delta** | **5,832** | |

`networking/rxring.cpp` and `networking/discovery.cpp` contribute **ZERO** bytes of globals,
which is not luck: both are on `tools/check.sh`'s pure list and the gate fails the build on
any file-scope mutable state in either. The peer table and the discovery job live in the
caller's `LinkJob` (32 B x 8 peers + 24 B of header = **280 B**), which is not in this table
because nothing declares one yet — P7-C2's LINK screen will, and that is where those 280 B
will appear, exactly as the NETWORK screen's 136 B `WifiScanJob` did at P5-C3.

### What it leaves

`release` is **1,233,762 / 55,740** = **77.1 % of `GATE_RELEASE_FLASH_MAX`** and **85.8 % of
`GATE_RELEASE_GLOBALS_MAX`** (9,260 B of globals free), and 39.2 % of the 3,145,728 B `app0`
slot. **Globals is the scarce axis and this chunk spent 5,816 of it in one commit** — more
than phase 5 and phase 6 spent together (504 + 400 = 904) — so the §3 projection's remaining
headroom for P7-C2..C5 and phases 8-10 is 9,260 B with BLE still compiled out and
`ble_social.cpp`'s 23,496 B still available to reclaim the day two boards have linked.

**The one number to watch is `LINK_RX_SLOTS`.** It is 5,312 of the 5,816 and it was set from
a measurement (a lossy link bursts 19 frames in one direction; eight slots refused three per
clean battle and sixteen would still have overflowed). If phase 8's creator server needs
globals back, that buffer is where they are — and the price of taking them is a link that is
quietly slower with no error anywhere, which is why the number is in
`tests/test_link_transport.cpp` as an assertion and not only in a comment.

## 5. Phase 7, chunks 2 and 3 — the LINK screen and the battle over the link (measured 2026-09-05)

All seven matrix variants, P7-C1 -> P7-C3:

| Variant | flash before | flash after | Δ flash | globals before | globals after | Δ globals |
|---|---|---|---|---|---|---|
| baseline | 1,958,560 | 1,985,080 | +26,520 | 79,412 | 80,340 | +928 |
| no-ble | 1,246,000 | 1,272,472 | +26,472 | 55,916 | 56,844 | +928 |
| no-web | 1,900,532 | 1,927,092 | +26,560 | 77,364 | 78,292 | +928 |
| no-god | 1,946,186 | 1,972,716 | +26,530 | 79,236 | 80,180 | +944 |
| sh1106 | 1,958,560 | 1,985,080 | +26,520 | 79,412 | 80,340 | +928 |
| all-off | 547,274 | 569,632 | +22,358 | 25,324 | 26,236 | +912 |
| **release** | **1,233,762** | **1,260,236** | **+26,474** | **55,740** | **56,668** | **+928** |

`all-off` moves this time and it is not a contradiction of P7-C1's zero: the LINK SCREEN is
in the screen table, so it is linked whatever `FEATURE_ESPNOW` says, and it drags the session,
the codec and the peer table with it. What `FEATURE_ESPNOW 0` still removes is the RADIO — the
driver compiles to refusal stubs, `link_start()` answers false, and the screen shows "Radio no
disponible" rather than hanging. That is the right shape: the feature switch turns the radio
off, not the screen into a hole in the table.

### Where the 928 B of globals went, symbol by symbol

`riscv32-esp-elf-nm -S` over `sketch/src/ui/screen_link.cpp.o`:

| symbol | bytes | what it is |
|---|---|---|
| `s_sess` | 340 | the caller-owned `Session` — 144 B of it is `my_rec[3][48]`, the frozen wire team |
| `s_job` | 276 | the caller-owned `LinkJob` — **the 280 B §4 predicted, now that something declares one** |
| `draw_browse`'s `rows` | 180 | nine 20-char list rows (`LINK_PEER_CAP` + "Volver") |
| `draw_browse`'s `vals` | 72 | their nine right-hand signal readings |
| `s_peer_name` | 13 | the agreed peer's name, copied out of the table so §47's retry can name it |
| scalars | 15 | the mode, the cursor, the operation, the two arming bytes, the clock |
| **screen_link.o** | **896** | |
| `ui/screen_battle.cpp`'s `s_me` + padding | 32 | the local side, and link alignment |
| **measured image delta** | **928** | |

The peer's SLOT, DEVICE ID and CAPABILITY WORD are deliberately not in that
table: they live in the `LinkJob`'s own row and in the `Session`, and a second
copy on the screen would be a second thing to keep true. The NAME is copied
because §47's "CONEXIÓN PERDIDA" has to be able to say who the player was
talking to after the table that held them has been reset.

**There is NO second `BattleSetup` and NO second `BattleState`,** which is the whole reason
`networking/session.h` takes both by pointer: the linked path borrows
`ui/screen_battle.cpp`'s 780 B setup and 212 B state, so this chunk's globals are the session's
own bookkeeping and a peer list, and not a second copy of a battle. A `LinkLog` was
deliberately NOT declared either — it is optional, it is 8 B per entry of transport telemetry
no player sees, and `SessionEnd`'s 40 B record already answers "why did it end" without a
rerun.

### What it leaves

`release` is **1,260,236 / 56,668** = **78.8 % of `GATE_RELEASE_FLASH_MAX`** and **87.2 % of
`GATE_RELEASE_GLOBALS_MAX`** — **8,332 B of globals free** for P7-C4 (trade), P7-C5 (breeding)
and phases 8-10, with `ble_social.cpp`'s 23,496 B still available to reclaim the day two boards
have linked.

**Flash moved twenty-six thousand bytes for a screen, and that is worth naming rather than
shrugging at.** It is not the screen's drawing: it is `networking/session.cpp`,
`battle_link.cpp`, `protocol.cpp`, `discovery.cpp` and `validate.cpp`'s wire path entering the
IMAGE for the first time. Every one of them existed at P7-C1 and none of them was reachable
from `setup()`, so `--gc-sections` dropped the lot; P7-C2 is the commit that gives them a
caller. The same will NOT happen again for P7-C4 and P7-C5 — `trade_link.cpp` and
`breed_link.cpp` add their own frames to a codec that is now already linked.


---

## 6. Phase 7, chunks 4 and 5 — the atomic trade and breeding (measured 2026-09-05)

All seven matrix variants, P7-C3 -> P7-C5:

| Variant | flash before | flash after | Δ flash | globals before | globals after | Δ globals |
|---|---|---|---|---|---|---|
| baseline | 1,985,080 | 1,993,954 | +8,874 | 80,340 | 80,484 | +144 |
| no-ble | 1,272,472 | 1,281,332 | +8,860 | 56,844 | 56,988 | +144 |
| no-web | 1,927,092 | 1,935,958 | +8,866 | 78,292 | 78,436 | +144 |
| no-god | 1,972,716 | 1,981,584 | +8,868 | 80,180 | 80,324 | +144 |
| sh1106 | 1,985,080 | 1,993,954 | +8,874 | 80,340 | 80,484 | +144 |
| all-off | 569,632 | 578,508 | +8,876 | 26,236 | 26,380 | +144 |
| **release** | **1,260,236** | **1,269,108** | **+8,872** | **56,668** | **56,812** | **+144** |

### Where the globals went

There is **one new object of any size in the whole chunk**, and it is caller-owned
for the reason everything else on this screen is:

`riscv32-esp-elf-nm -S` over the RELEASE `.elf`, and the four symbols below are
**the whole of the +144**:

| symbol | bytes | what it is |
|---|---|---|
| `ui/screen_link.cpp`'s `s_tl` | **140** (0x8c) | the caller-owned `TradeLink`: two 48 B wire records, the five hooks + ctx (24 B), the pair and offer CRCs, and twelve state bytes |
| `s_trade_slot` | 1 | which Box slot is on the wire |
| `app/app.cpp`'s `g_trade_toast` | 2 | the boot resolver's answer, queued for the first HOME frame |
| `game/trade.cpp`'s `s_last_reject` | 1 | a diagnostic; nothing branches on it |
| **total** | **144** | and the measured image delta is 144 |

**`ui/ui.cpp`'s `TradeHooks` table is `static const`**, so it is `.rodata` and
costs flash rather than RAM — and `networking/trade_link.cpp` holds no file-scope
mutable at all, which `tools/check.sh` gates by filename exactly as it does for
`session.cpp` and `battle_link.cpp`.

**THERE IS NO SECOND JOURNAL AND NO SECOND BOX.** `game/trade.cpp` drives the
write order through a `TradeStore` of four function pointers and mutates the ONE
`GameState` `persistence/game_state.cpp` owns; the 64 B `PendingTrade` has lived
inside `GameState` since P2-C9 and is not new RAM.

### Where the flash went, and why it is a tenth of P7-C2's

P7-C2 cost 26 KB because the session, the codec, the peer table and the wire half
of the validator entered the image for the first time — every one of them existed
at P7-C1 and none was reachable from `setup()`, so `--gc-sections` had dropped the
lot. That is exactly what P7-C2's budget section predicted would NOT happen again,
and it did not: `game/trade.cpp`, `networking/trade_link.cpp` and
`game/breeding.cpp` add their own frames to a codec that was already linked.

**`game/breeding.cpp` IS NOT IN THE RELEASE IMAGE, AND THAT WAS MEASURED RATHER
THAN ASSUMED.** `riscv32-esp-elf-nm -S` over the release `.elf` finds no
`breed_check`, no `breed_compute`, no `breed_commit` and no `genome_breed`:
nothing reachable from `setup()` calls any of them — `breed_link.cpp` is not
built and the LINK card's CRIAR row answers "Aún no está listo" — so
`--gc-sections` dropped the whole module. **So the +8,872 above is the TRADE's,
end to end, and breeding costs the shipping artefact nothing because the shipping
artefact cannot breed.** That is the fact behind the plan's sentence rather than
an inference from it, and it is the number to watch when `breed_link.cpp` lands:
the module is 196 lines of pure integer code and it will appear then, not before.

**This is the phase-6 defect's shape, caught with a `nm` instead of a bug
report.** P6 found that the SHIPPING build never advanced game time, for four
phases, because a default lived inside a dev-only function; the discipline that
came out of it is to ask what the RELEASE artefact does, not what the build in
front of you compiles. `game/trade.cpp` IS in the image — `trade_journal_sent`,
`trade_journal_live`, `trade_wire_codec`, `save_trade_journal`, the app's four
store shims and `s_tl` are all there — because `app_setup()` calls the boot
resolver and `ui/screen_link.cpp` opens trade sessions. Breeding is not, and the
plan says so in the one place a reader will look.

### What it leaves

`release` is **1,269,108 / 56,812** = **79.3 % of `GATE_RELEASE_FLASH_MAX`** and
**87.4 % of `GATE_RELEASE_GLOBALS_MAX`** — **8,188 B of globals free** for
`breed_link.cpp`, phase 8's creator and phases 9-10, with `ble_social.cpp`'s
23,496 B still available to reclaim the day two boards have linked.

## 7. Phase 7, chunk 6 — the exit (measured 2026-09-05)

**+506 flash / +8 globals on the baseline**, **+360 / +8 on release**. This is the smallest
chunk in the phase and it is the one that stopped the trade destroying a Pebble.

### Where the 8 B of globals went

`riscv32-esp-elf-nm -S` over the release `.elf`:

| symbol | B | what holds it |
|---|---|---|
| `s_tick_lost_s` (`hardware/power.cpp`) | 4 | whole seconds the dropped-tick clamp has discarded, saturating |
| `s_tick_stalls` (`hardware/power.cpp`) | 2 | how many times it fired, saturating |
| | **6** | symbols |
| | **2** | link alignment |
| | **8** | **measured image delta** |

`save_pebble_now()`, `act_adopt()` and the narrowed `sim_gain_restore()` add **no state at
all** — they are a second entry point, a boot-time clamp and a deleted term. The +506 B of
flash is those three plus `pwr_tick_budget()`.

### The one measurement this chunk exists for

**A clean trade cost 9 flash writes and should have cost 10.** `save_pebble()` deferred the
second write of one key inside `SAVE_MIN_GAP_MS` and reported it as landed, so the incoming
Pebble never reached flash while the journal was cleared over it. The counter is in the test
output (`one committed trade costs 10 flash writes`), which is the cheapest possible regression
detector for this class of bug and the reason the case prints it.

### Phase 7 against `docs/budget.md`'s own allowance

§3's phase-7 row read **20–35 KB of flash and 2.0–4.0 KB of globals**. Actual: **+45,658 flash
and +6,912 globals** — **over both lines**, and the overrun has one address. **5,832 of the
6,912 globals are P7-C1**, and 5,312 of those are `s_rx_bytes`, the session receive ring, whose
depth was raised from the survey's proposed 8 slots to a MEASURED 32 (one `session_poll()`
bursts 19 frames at a 10 % drop rate; sixteen would still have overflowed). The flash overrun
is P7-C2's 26,520, which is the session, the codec, the peer table and the wire half of the
validator entering the image for the first time — every one of them existed at P7-C1 and none
was reachable from `setup()`, so `--gc-sections` had dropped the lot.

**Neither is a surprise the budget can absorb quietly.** Release globals are now at **87.4 %**
of `GATE_RELEASE_GLOBALS_MAX` with **8,180 B free** for phase 8's creator server, phase 9's
content and phase 10 — and phase 8 is the one that brings up an HTTP server and a 2 KB body
buffer. **The lever is BLE**: 23,496 B, three times the remaining headroom, blocked on one
bench test.

### What it leaves

`release` is **1,269,468 / 56,820** = **79.3 % of `GATE_RELEASE_FLASH_MAX`** and **87.4 % of
`GATE_RELEASE_GLOBALS_MAX`**; baseline **1,994,460 / 80,492** = 83.1 % / 89.4 %. The `app0`
slot is 3,145,728 B, so the shipping image uses **40.4 %** of it.

## 8. Phase 8, chunk 0 — BLE deleted, and §7's headroom claim corrected (measured 2026-09-05)

**−712,960 flash / −23,512 globals on the baseline. −342 / −16 on release.**

| build | v0.7.0-social | after P8-C0 | delta | against its cap |
|---|---|---|---|---|
| `baseline` | 1,994,460 / 80,492 | 1,281,500 / 56,980 | −712,960 / −23,512 | 53.4 % / 63.3 % |
| `release` | 1,269,468 / 56,820 | 1,269,126 / 56,804 | **−342 / −16** | **79.3 % / 87.4 %** |
| `all-off` | 578,868 / 26,388 | 578,510 / 26,364 | −358 / −24 | — |

### §7 OF THIS DOCUMENT WAS WRONG AND THIS SECTION IS THE CORRECTION

§7 wrote: *"Release globals are now at 87.4 % of `GATE_RELEASE_GLOBALS_MAX` with 8,180 B free
for phase 8's creator server, phase 9's content and phase 10 — and phase 8 is the one that
brings up an HTTP server and a 2 KB body buffer. **The lever is BLE**: 23,496 B, three times the
remaining headroom, blocked on one bench test."*

**There was no lever.** The `release` variant has been `GOD_MODE_ENABLED=0 FEATURE_BLE=0` since
the variant was written, so the image the release caps police has never contained BLE. The
23,496 B is `baseline` minus `no-ble` — what BLE costs the DEV build — and §7 quoted it against
the SHIPPING build's headroom. Deleting BLE for real moved release globals by sixteen bytes,
and even those sixteen are the settle window, not BLE.

**The headroom figure §7 gave was right; only its explanation was wrong.** Release globals were
at 87.4 % of the cap and they still are: **8,196 B free** (65,000 − 56,804), against §3's
forecast of **1.5–3.0 KB for phase 8**. Phase 8 fits, with 5.2–6.7 KB left for phases 9 and 10
— which is tight but is not the crisis §7 described, and the difference between those two
readings is one variant definition nobody re-read.

### What actually threatens the globals cap now, in order

1. **Phase 8's `CS_BODY_MAX` 2 KB raw-body buffer** — a fixed array, and the largest single
   allocation the creator adds. It is 24 % of the remaining headroom on its own.
2. **Phase 9's roster** — 60 species. The tables are `const` and land in `.rodata`, so the
   flash line moves and the globals line should not; `tools/check.sh`'s content gate is what
   would catch a table that accidentally is not `const`.
3. **Phase 10's diagnostics** — historically where counters accumulate.

**The 48 KB page blob is `.rodata` and costs FLASH, not globals**, which the flash column can
absorb four times over: release is at 79.3 % of a 1.6 MB cap with 331 KB free, and the `app0`
partition is 3.1 MB.

### The measurement, and the grep that nearly lied about it

`riscv32-esp-elf-nm -S -C` over the release `.elf` matches **433 symbols** case-insensitively
against `ble` — `ATTACKS_TABLE`, `soc_get_available_memory_regions`, `PebbleInstance`,
`mimeTable`. **Two of them are Bluetooth**, both ESP-IDF stubs the core links unconditionally:

```
42081052 0000006c T esp_bt_controller_mem_release
42080fce 00000084 T esp_bt_controller_rom_mem_release
```

No Bluedroid, no `BLEDevice`, no `BLEScan`, no `btc_task`. Recorded because the first grep
returned 433 and looked like a failed deletion; the substring `ble` is in too many English
words to be a symbol filter, and the useful pattern is
`bluedroid|btc_task|esp_bt_controller|BLEDevice|BLEScan|BLEAdvertising`.

### What it leaves

`release` is **1,269,126 / 56,804** = **79.3 % of `GATE_RELEASE_FLASH_MAX`** and **87.4 % of
`GATE_RELEASE_GLOBALS_MAX`**. `baseline` is **1,281,500 / 56,980** — within **12,374 B of
flash and 176 B of globals of the artefact**, where it was 725 KB and 23.7 KB away before. That
is the number this chunk is actually worth: **`tools/check.sh` now polices a build shaped like
the one that ships**, which is the standing lesson of phases 6 and 7 applied to the gate itself.


## 9. Phase 8, chunks 1 and 2 — the PIN and the AP-only lifecycle (measured 2026-09-05)

**+2,574 flash / +16 globals on `release`. The same on `baseline`.** Every figure below names
its variant, and both builds were made with `tools/build.sh` at this commit.

| build | after P8-C0 | after P8-C1/C2 | delta | against its cap |
|---|---|---|---|---|
| `baseline` | 1,281,500 / 56,980 | 1,284,074 / 56,996 | +2,574 / **+16** | 53.5 % / 63.3 % |
| `release`  | 1,269,126 / 56,804 | **1,271,700 / 56,820** | +2,574 / **+16** | **79.5 % / 87.4 %** |
| `no-web`   | 1,222,722 / 54,924 | 1,222,900 / 54,924 | +178 / **0** | — |
| `all-off`  | 578,510 / 26,364 | 578,598 / 26,364 | +88 / **0** | — |

### The 16 bytes, by name

`riscv32-esp-elf-nm -S -td` over the **release** `.elf`:

```
1070162696 00000016 b _ZL6s_gate       <- the whole cost of this chunk
1070162712 00000364 b _ZL5s_srv        <- unchanged
1070162376 00000320 b _ZL6s_json       <- unchanged
```

`_ZL5s_pin`, the `uint16_t` boot PIN, is **gone**: the PIN lives in `ConfigV2` now and its
runtime state is inside `s_gate`. Section-level cross-check on the same `.elf`: `.dram0.data`
0x3a24 = 14,884 (unchanged) and `.dram0.bss` 0xa3d0 = 41,936 (was 0xa3c0 = 41,920), summing to
exactly the 56,820 the compiler printed.

The `+178` on `no-web` and `+88` on `all-off` with **zero** globals is the shape to expect:
`webui.cpp` compiles to its stubs in those variants and `networking/creator_gate.cpp` is a pure
translation unit whose whole state is the caller's struct, so what is left is `net.cpp`'s
`net_request_portal()` and the shorter `net_url()`.

### What it leaves

`release` globals are at **56,820 of 65,000 — 8,180 B free**, where §8 left 8,196. **This chunk
spent 16 B, 0.20 % of the remaining headroom**, against §3's 1.5–3.0 KB forecast for the whole
of phase 8. Flash is at 79.5 % with **328,300 B free**.

The row still to watch is unchanged and is P8-C3's: **`CS_BODY_MAX`, the 2 KB raw-body buffer,
is 25.0 % of what is left on its own** — 128 times this chunk. Nothing here pre-spends it.
`P8-C2` also leaves the page blob untouched: it is `.rodata` and lands on the flash line, which
can absorb it four times over.


## 10. Phase 8, chunk 3 — the seven routes, the raw-body cap and the creator registry (measured 2026-09-05)

**+13,046 flash / +2,576 globals on `release`; +13,044 / +2,576 on `baseline`.** Every figure names its
variant; `tools/build.sh` produced all of them at this commit and the release `.elf` was built
with `GOD_MODE_ENABLED=0` and its size line read back before any symbol was.

| build | after P8-C1/C2 | after P8-C3 | delta | against its cap |
|---|---|---|---|---|
| `baseline` | 1,284,074 / 56,996 | 1,297,118 / 59,572 | +13,044 / **+2,576** | 54.1 % / 66.2 % |
| `release`  | 1,271,700 / 56,820 | **1,284,746 / 59,396** | +13,046 / **+2,576** | **80.3 % / 91.4 %** |
| `no-web`   | 1,222,900 / 54,924 | 1,224,890 / 55,172 | +1,990 / **+248** | — |
| `all-off`  | 578,598 / 26,364 | 580,588 / 26,612 | +1,990 / **+248** | — |

### The 2,576 bytes, by name

`riscv32-esp-elf-nm -S -td` over the **release** `.elf`:

```
1070156992 00002060 b _ZL6s_body                 <- the raw-body accumulator
1070156736 00000256 b _ZL5s_out                  <- creator_server's response scratch
1070156236 00000240 b _ZL6s_rows                 <- the 10 projected SpeciesDef rows
1070197964 00000004 b _ZL5s_srv                  <- creator_server's WebServer*
1070197728 00000004 V SPECIES_CUSTOM_RESOLVER    <- the bound resolver
1070197748 00000002 b _ZL6s_mask                 <- the registry's occupancy mask
                    ------
                     2,566 named, +10 B of alignment
1007711080 00000745 d _ZL19CREATOR_SCHEMA_JSON   <- .rodata: FLASH, not globals
1070165268 00000364 b _ZL5s_srv                  <- webui's WebServer, unchanged
1070164932 00000320 b _ZL6s_json                 <- unchanged
1070165252 00000016 b _ZL6s_gate                 <- unchanged (P8-C1)
```

Section cross-check on the same `.elf`: `.dram0.data` 0x3a24 = 14,884 (**unchanged**) and
`.dram0.bss` 0xade0 = 44,512 (was 0xa3d0 = 41,936, **+2,576**), summing to exactly the 59,396
the compiler printed.

**THE `+248` ON `no-web` AND `all-off` IS THE SHAPE THAT MATTERS, and it is the half of this
chunk that is NOT the web server.** `game/species_custom.cpp` is reached by
`persistence/save_manager.cpp`'s load path, which every variant links, so the registry (240 B of
rows + 2 B of mask) and the 4 B resolver pointer are paid even in a build with no radio at all.
The web-only cost is therefore **2,576 − 248 = 2,328 B**: the 2,060 B accumulator, the 256 B
response scratch, a 4 B pointer and 8 B of padding.

**`CS_BODY_MAX` IS 2,060 B OF IT — 80 % OF THIS CHUNK AND 25.2 % OF THE HEADROOM THE CHUNK
STARTED WITH.** That is what the phase-8 preamble said it would be, and it landed on the number.
The worst LEGITIMATE upload is ~410 B (`networking/creator_body.h` carries the arithmetic beside
the struct), so the cap is five times the largest document the schema can produce. **If a later
chunk needs the bytes back, 1024 is still 2.7× the worst case and is one constant.**

### What it leaves

`release` globals are at **59,396 of 65,000 — 5,604 B free**, where §9 left 8,180. This chunk
spent **2,576 B, 31.5 % of the remaining headroom**, and phase 8's running total is **2,592 B**
against §3's 1.5–3.0 KB forecast for the whole phase: **inside the forecast, at the top of it.**

Flash is at **80.3 %** with **315,254 B free**, and that is the line P8-C4 spends: the page blob
is `.rodata`, `WEB_HTML_MAX` caps it at 49,152 B, and the flash line can absorb six of those.

**WHAT PHASES 9 AND 10 HAVE LEFT, SAID PLAINLY:** 5,604 B of globals. P8-C4 and P8-C5 should
cost nothing on that line (a page blob and a QR payload are both flash), so this is close to the
number phase 9 inherits. Phase 9's roster grows `SPECIES_TABLE`, `ATTACKS_TABLE` and the sprite
atlas — all `inline constexpr`, all flash — but P9-C5's corruption effects and P10-C1's
diagnostics are RAM. The one row to watch next is no longer `CS_BODY_MAX`: it is spent. It is
whether P10-C1's `dev/diagnostics.cpp` needs a buffer, and the honest advice is to check this
table before it writes one.

---

## 11. Phase 8, chunk 4 — the mobile page and the sprite editor (measured 2026-09-06)

**+41,658 flash / +0 globals on `release`; the same +41,658 / +0 on `baseline`; ZERO of either
on `no-web` and `all-off`.** Every figure names its variant. `tools/build_matrix.sh` produced all
six at this commit, and the release `.elf` the symbols below come from was built separately with
`--variant release --define GOD_MODE_ENABLED=0` and its size line read back **before** any symbol
was — the §8 rule, applied.

| build | after P8-C3 | after P8-C4 | delta | against its cap |
|---|---|---|---|---|
| `baseline` | 1,297,118 / 59,572 | 1,338,776 / 59,572 | **+41,658 / +0** | 55.8 % / 66.2 % |
| `release`  | 1,284,746 / 59,396 | **1,326,404 / 59,396** | **+41,658 / +0** | **82.9 % / 91.4 %** |
| `no-web`   | 1,224,890 / 55,172 | 1,224,890 / 55,172 | **+0 / +0** | — |
| `all-off`  | 580,588 / 26,612 | 580,588 / 26,612 | **+0 / +0** | — |

### The 41,658 bytes, by name — and they are all flash

`riscv32-esp-elf-nm -S -td` over the **release** `.elf`:

```
1007713068 00042248 d _ZL10INDEX_HTML            <- the creator page
1007711080 00001172 d _ZL19CREATOR_SCHEMA_JSON   <- was 745 B: +427 for the names
```

Both addresses are inside `.flash.rodata` (`objdump -h` puts that section at `0x3c0f0120`, length
`0x32cf4`), which is the whole claim in one line: the `d` class is initialised data, and on this
target initialised data whose address is in the rodata segment is **memory-mapped flash, not
RAM**. `INDEX_HTML` is 42,248 B for a 42,245 B page — the blob, its NUL and 2 B of alignment.

The old placeholder page was 1,016 B in the same section, so the page's own delta is
42,248 − 1,016 = **41,232**, plus **427** for the attack and type names added to the schema
document, giving **41,659** — one byte from the 41,658 the compiler reported, which is alignment.

**THE GLOBALS LINE DID NOT MOVE, AND THE SECTION CROSS-CHECK IS WHY THAT IS A MEASUREMENT AND
NOT A HOPE.** On the same `.elf`: `.dram0.data` `0x3a24` = 14,884 and `.dram0.bss` `0xade0` =
44,512, summing to exactly the 59,396 the compiler printed — and **both are byte-identical to
§10's**. Not "about the same": the same numbers. Nothing in this chunk allocates: the page is a
string literal, the schema names are a string literal, and the only new code in `src/` is the
two extra JSON keys inside a blob that was already there.

**`no-web` AND `all-off` PAID NOTHING AT ALL,** which is the number to quote when somebody asks
what the creator page costs a radio-less build. `data/index_html.h` is included by exactly one
translation unit (`networking/webui.cpp`) and `data/creator_schema_json.h` by exactly one
(`networking/creator_server.cpp`); both are inside `#if FEATURE_WEB`, so with the feature off
neither literal is ever referenced and `--gc-sections` drops it. Contrast §10, where the +248 on
those variants was real because `game/species_custom.cpp` sits on the load path every variant
links.

### What it leaves

`release` globals are at **59,396 of 65,000 — 5,604 B free, unchanged from §10.** Phase 8's
running total on the globals line is still **2,592 B** against §3's 1.5–3.0 KB forecast for the
whole phase, and P8-C5 (a QR payload and a screen) has no reason to move it either. **The number
phase 9 inherits is 5,604 B.**

`release` flash is at **82.9 % with 273,596 B free**, where §10 left 315,254. This chunk spent
**41,658 B — 13.2 % of the flash headroom it started with — and it is the largest single flash
allocation any chunk has made.** That is the trade the phase-8 preamble named: the page is
`.rodata`, flash is the line with 273 KB on it, and globals is the line with 5.6 KB.

### The page against its own cap, and the lever if it ever binds

**42,245 B of `WEB_HTML_MAX` 49,152 — 6,907 B free, 85.9 % used.** `tools/gen_index_html.py`
prints that line on every run and `tools/check.sh` fails the gate if the header and the source
have drifted, so the margin is a number somebody reads rather than a compiler error at 49,155 B.

**IT DOES NOT GROW WITH THE ROSTER, AND THAT IS THE POINT OF SERVING THE SCHEMA.** Every number
and every attack name the page draws comes from `GET /api/schema` at load time, so phase 9's
60-species roster grows `CREATOR_SCHEMA_JSON` (flash, ~1.2 KB today) and leaves the page blob
exactly where it is. What would move it is a new SCREEN.

**THE LEVER, PRICED SO NOBODY HAS TO GUESS AT IT LATER.** `send_P` serves a pre-compressed blob
with a `Content-Encoding: gzip` header — the core's own `serveStatic` does exactly that at
`WebServer.cpp:690` — and this project's previous phone page compressed 47,181 B to 17,947 B
(38.0 %). At that ratio the 48 KB cap becomes about 126 KB of page source. The cost is that the
blob stops being a string literal (gzip contains NULs and every byte value, so the generator must
emit a byte array), that `Accept-Encoding` has to join `collectHeaders()` with a plain fallback
for the captive-portal probe, and that `curl http://192.168.4.1/` stops being readable.
**Take it when the raw number actually crosses; do NOT raise `WEB_HTML_MAX` as the first move,
because the cap is the only thing that makes the overrun visible.**

---

## 12. Phase 8, chunk 5 — the §34 QR screen and the bench smoke script (measured 2026-09-06)

**+26 flash / +0 globals on `release`, and the same +26 / +0 on every one of the six variants
including `no-web` and `all-off`.** Every figure names its variant. `tools/build_matrix.sh`
produced all six at this commit; the release `.elf` the sections below come from was built
separately with `--variant release --define GOD_MODE_ENABLED=0`, and its size line was read back
**before** any section was — the §8 rule, applied.

| build | after P8-C4 | after P8-C5 | delta | against its cap |
|---|---|---|---|---|
| `baseline` | 1,338,776 / 59,572 | 1,338,802 / 59,572 | **+26 / +0** | 55.8 % / 66.2 % |
| `release`  | 1,326,404 / 59,396 | **1,326,430 / 59,396** | **+26 / +0** | **82.9 % / 91.4 %** |
| `no-web`   | 1,224,890 / 55,172 | 1,224,916 / 55,172 | **+26 / +0** | — |
| `all-off`  | 580,588 / 26,612 | 580,614 / 26,612 | **+26 / +0** | — |

### The 26 bytes, by section — and the base build was rebuilt to get them

Twenty-six bytes is too small for `nm -S` to attribute honestly: the symbol that changed is a
`static` render function the compiler is free to inline, and the strings are literals inside a
table. So the previous commit's **release** `.elf` was rebuilt from a worktree at `c1fa052` with
the identical command line, and the two were compared section by section with `objdump -h`:

```
                     c1fa052 (P8-C4)   this commit    delta
  .flash.text          0x000ea9a8       0x000ea9ba     +18   code
  .flash.rodata        0x00032cf4       0x00032cfc      +8   read-only data
  .dram0.data          0x00003a24       0x00003a24      +0   initialised globals
  .dram0.bss           0x0000ade0       0x0000ade0      +0   zeroed globals
  .iram0.text          0x00010aec       0x00010aec      +0
```

**+18 + 8 = 26, which is exactly the figure the compiler printed** — the two measurements are
independent and they agree.

The rodata half is the string table. `nm -S -td` on both `.elf`s puts `ES` at `0x2300` = 8,960 B
before and `0x2304` = 8,964 B after: **one more `StrId` pointer**, because `STR_CREATOR_PHASE`
("Creador - Fase 8", the line saying the page did not exist yet) was deleted and `STR_CREATOR_SCAN`
("ESCANÉAME") and `STR_CREATOR_WITH_PHONE` ("Con el móvil") were added. The literal text is
−17 + 11 + 14 = **+8 B nominal**; the section moved **+8 B total including the +4 pointer**, so
the linker packed 4 B of it into alignment slack the removed literal was already paying for. The
18 B of code is the render function's new shape: one `if` on `in.pin == 0`, one `snprintf`, and
five `gfx_text_fit` calls where there were four plus a `gfx_text_wrap`.

**EVERY VARIANT PAID, INCLUDING THE TWO THAT PAID NOTHING FOR THE PAGE**, and the reason is worth
writing down next to §11's contrary result: `core/strings_es.h` and `ui/screen_creator.cpp` are
not behind `#if FEATURE_WEB`. The CREATOR screen is a screen — it compiles and draws in a build
with no radio at all, where it says "Conectando..." and times out after `CREATOR_AP_WAIT_MS` —
so a layout change is on the load path of every artefact this project ships. §11's `+0` on
`no-web` was a property of `data/index_html.h` having exactly one `#if FEATURE_WEB` consumer, not
a property of creator work in general.

**THE GLOBALS LINE DID NOT MOVE, AND THE SECTION CROSS-CHECK IS THE MEASUREMENT.** `.dram0.data`
0x3a24 = 14,884 plus `.dram0.bss` 0xade0 = 44,512 is exactly the 59,396 the compiler printed, and
both are **byte-identical to §11's and to §10's**. The screen allocates nothing new: `s_key` was
already there, `creator_payload()` returns a pointer to it, and the two new strings are `.rodata`.

`tools/creator_smoke.sh` costs **zero bytes of firmware**. It is a bench instrument that runs on
the operator's laptop and is not compiled into anything.

### What it leaves

`release` globals are at **59,396 of 65,000 — 5,604 B free, unchanged from §11, §10 and the
phase-8 forecast.** Phase 8's running total on the globals line is **2,592 B** against §3's
1.5–3.0 KB forecast for the whole phase, and phase 8 is now complete: **the number phase 9
inherits is 5,604 B.**

`release` flash is at **82.9 % with 273,570 B free**, where §11 left 273,596. This chunk spent
26 B, which is 0.0095 % of the flash headroom it started with and the smallest allocation any
chunk in this phase has made.

**PHASE 8 SINCE P8-C0, ON THE RELEASE ARTEFACT** — and the variant is named because §8 exists:
`34aab11` left `release` at 1,269,126 / 56,804 (`baseline` was 1,281,500 / 56,980 at the same
commit, which is the pair of numbers §8 warns against confusing). It is now 1,326,430 / 59,396,
so chunks 1 through 5 cost **+57,304 flash and +2,592 globals**. The globals figure is the running
total §11 quotes and it is inside §3's 1.5–3.0 KB forecast for the whole phase; 41,658 of the
57,304 flash bytes are the page blob, which §11 prices and gives the gzip lever for.

---

## 13. Phase 8, chunk 6 — the exit (measured 2026-09-06)

**−30 flash / +0 globals on `release`, and the same −30 / +0 on `baseline`, `no-god` and
`sh1106`; `no-web` and `all-off` did not move at all.** Every figure names its variant.
`tools/build_matrix.sh` produced all six at this commit; the release `.elf` the sections and
symbols below come from was built separately with `--variant release --define
GOD_MODE_ENABLED=0` into its own build path, and **its size line was read back before any
section or symbol was** — §8's rule, applied.

| build | after P8-C5 | after P8-C6 (this commit) | delta | against its cap |
|---|---|---|---|---|
| `baseline` | 1,338,802 / 59,572 | 1,338,772 / 59,572 | **−30 / +0** | 55.8 % / 66.2 % |
| `release`  | 1,326,430 / 59,396 | **1,326,400 / 59,396** | **−30 / +0** | **82.90 % / 91.38 %** |
| `no-god`   | 1,326,430 / 59,396 | 1,326,400 / 59,396 | **−30 / +0** | — |
| `sh1106`   | 1,338,802 / 59,572 | 1,338,772 / 59,572 | **−30 / +0** | — |
| `no-web`   | 1,224,916 / 55,172 | 1,224,916 / 55,172 | **+0 / +0** | — |
| `all-off`  | 580,614 / 26,612 | 580,614 / 26,612 | **+0 / +0** | — |

### The −30 bytes, by section, against a rebuilt base

Thirty bytes is too small for `nm` alone, so `8b6a9bf`'s **release** `.elf` was rebuilt from a
worktree with the identical command line and the two compared with `objdump -h`. Only **two
loadable sections moved**:

```
                     8b6a9bf (P8-C5)   this commit    delta
  .flash.text          0x000ea9ba       0x000ea9a8     −18   code
  .eh_frame            0x00012098       0x0001208c     −12   CONTENTS, ALLOC, LOAD
  .flash.rodata        0x00032cfc       0x00032cfc      +0
  .dram0.data          0x00003a24       0x00003a24      +0   initialised globals
  .dram0.bss           0x0000ade0       0x0000ade0      +0   zeroed globals
  .iram0.text          0x00010aec       0x00010aec      +0
```

**−18 − 12 = −30, exactly the figure the compiler printed** — two independent measurements
agreeing. `.eh_frame` is easy to skip and is `ALLOC, LOAD` on this target, so it is part of
the flashed image; the other five sections that moved (`.debug_*`, and they moved by −202 net)
are not.

`nm -S -td` names the whole of it in one symbol: **`_ZL6h_rootv` 216 → 198 B**. That function
GAINED a `cs_body_done()` call at its throttled exit and got 18 bytes SMALLER, because both of
its exits now end in the same call and the compiler tail-merged them; the `.eh_frame` −12 is
that function's unwind record following its new shape. A fix that is smaller than the bug is
worth one line, because it is not the usual direction.

**ONE SYMBOL APPEARS THAT IS NOT AN ALLOCATION, AND SAYING SO IS THE POINT.** `nm` reports
`_ZL12CS_STATE_FMT` at **138 B of `d`** where the base build has no such symbol — and
`.flash.rodata` did not move by a single byte. The format string for `GET /api/state` was
already in `.rodata` as an anonymous literal; hoisting it into a named `static const char[]`
so `sizeof` could be taken for the new `static_assert` gave the same bytes a name.
`strings` finds the identical 138-byte literal (137 characters plus its NUL) exactly once in
BOTH `.elf`s. A `nm` diff read on its own would have reported "+138 B" for a commit that
allocated nothing, which is the kind of number this document exists to not print.

### Where phase 8's globals went, symbol by symbol — the whole phase, on `release`

`riscv32-esp-elf-nm -S -td` over this commit's release `.elf`. Every one of these is `b`
(zeroed) except the resolver, and none of them existed at `34aab11`:

| symbol | bytes | what holds them | chunk |
|---|---|---|---|
| `_ZL6s_body` | **2,060** | the raw-body accumulator: `CS_BODY_MAX` 2,048 + its 12 B of state | P8-C3 |
| `_ZL5s_out` | 256 | `creator_server.cpp`'s response scratch (`CS_OUT_BUF`) | P8-C3 |
| `_ZL6s_rows` | 240 | the ten projected `SpeciesDef` rows of the custom registry | P8-C3 |
| `_ZL6s_gate` | 16 | the one `CreatorGate` in `webui.cpp` — the whole PIN feature's RAM | P8-C1 |
| `_ZL5s_srv` | 4 | `creator_server.cpp`'s `WebServer*` | P8-C3 |
| `SPECIES_CUSTOM_RESOLVER` | 4 | the bound custom-species resolver | P8-C3 |
| `_ZL6s_mask` | 2 | the registry's occupancy mask | P8-C3 |
| **symbols** | **2,582** | | |
| link alignment | 10 | | |
| **measured image delta** | **2,592** | 56,804 → 59,396 | |

**The section cross-check is what makes that a measurement rather than a list.** On this
commit's release `.elf`, `.dram0.data` 0x3a24 = 14,884 plus `.dram0.bss` 0xade0 = 44,512 is
**exactly the 59,396 the compiler printed**, and both halves are byte-identical to §12's, §11's
and §10's. Nothing has allocated on this line since P8-C3.

**`_ZL6s_body` IS 79.5 % OF THE PHASE'S GLOBALS AND IT IS ONE CONSTANT.** The phase-8 preamble
called `CS_BODY_MAX` the one allocation big enough to matter on its own and put it at 24 % of
the headroom that existed before it was spent; measured against what is left **it is 36.8 %**.
It is spent, so it is not phase 9's row to watch — but it is the row to reach for if a later
phase needs bytes back: `CS_BODY_MAX 1024` is still about 2.7x the largest legitimate upload,
it is one `#define`, and `tools/creator_smoke.sh` reads the value out of `config.h` so the
bench script follows it without an edit.

### Where phase 8's flash went

| item | bytes | section | measured in |
|---|---|---|---|
| `_ZL10INDEX_HTML` — the creator page | **42,248** | `.flash.rodata` | §11 |
| `_ZL19CREATOR_SCHEMA_JSON` — the served schema | 1,172 | `.flash.rodata` | §10, §11 |
| less the 1,016 B `INDEX_HTML` **placeholder** that already existed at `34aab11` | −1,016 | `.flash.rodata` | §11 |
| everything else — the gate, the cap, the reader, the validator rules, the registry, the seven handlers, the §34 screen, the strings | **14,870** | `.flash.text` + `.rodata` | §§9-13 |
| **phase 8 total, `release`** | **+57,274** | | 1,269,126 → 1,326,400 |

Both blob addresses are inside `.flash.rodata` (`objdump -h` puts that section at
`0x3c0f0120`), which is the whole claim in one line: on this target initialised data whose
address is in the rodata segment is memory-mapped **FLASH, not RAM**.

### What it leaves, and it is the number phases 9 and 10 inherit

**`release` globals: 59,396 of `GATE_RELEASE_GLOBALS_MAX` 65,000 — 91.38 % used, 5,604 B FREE.**

**`release` flash: 1,326,400 of `GATE_RELEASE_FLASH_MAX` 1,600,000 — 82.90 % used, 273,600 B
FREE.**

`baseline` is 1,338,772 / 59,572 and is **not** the artefact those caps police; it is quoted
here only so the pair cannot be confused, which is the mistake §8 records.

Against §3's P9-P10 forecast of 25-35 K flash and 0.7-2.0 K globals, the ending state is
**1,351,400-1,361,400 flash (84.5-85.1 %)** and **60,096-61,396 globals (92.5-94.5 %)**, i.e.
**3,604-4,904 B of globals still free at the end of phase 10.** It fits. It fits with about
4 KB of margin, where the phase-6 re-scoring in §3 expected about 7, and §3 now says so.

**WHAT WILL ACTUALLY BITE PHASE 9, MEASURED RATHER THAN GUESSED.** Phase 9 is the roster to 60
species, the sprite swap and the corruption mechanic, and its budget line is **flash, not
globals** — which is the comfortable one here.

* `SPRITE_DATA_BYTES` is **10,247 B** since P9-C3 (measured by the compiler since P9-C1, not
  typed) against a `SPRITE_DATA_BYTES_MAX` of 11,264. That is the END STATE — 60 species
  bodies, 2 eggs and 2 pose sets at 144 B each, plus 1,031 B of icons and emotes — and the
  24,576 B transition allowance is gone with the legacy atlas it covered. §4.3 carries the
  arithmetic and the measured cost: **+1,690 B of flash and -352 B of globals** for the whole
  of P9-C3, of which the sixty bodies themselves are +6 B.
* `CREATOR_SCHEMA_JSON` grows with the ROSTER, not with the page. It is 1,172 B for 34 attacks
  today; a wider attack table moves it and moves `.flash.rodata`, not globals.
* `INDEX_HTML` does **not** grow with the roster — that is what serving the schema bought —
  and it is at **42,245 B of `WEB_HTML_MAX` 49,152, 85.9 % used with 6,907 B free**. What moves
  it is a new SCREEN. §11 prices the gzip lever if it ever binds; do not raise `WEB_HTML_MAX`
  as the first move, because the cap is the only thing that makes the overrun visible.
* On the globals line phase 9 forecasts 0.2-0.5 K. The registry that phase 8 left
  (`_ZL6s_rows`, 240 B for ten `SpeciesDef` rows) is sized by `CREATOR_SPECIES_SLOTS` and not
  by the roster, so growing the roster to 60 does not move it.
