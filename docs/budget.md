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
| BLE (`ble_social.cpp`, 1,211 lines) | **712,466** | **23,504** | decided dead — see below |
| Wi-Fi stack + HTTP server (`FEATURE_WEB`) | 676,158 | 24,596 | **needed**: P8's creator server is built on it |
| god mode | 11,884 | 168 | dev only, already off in release |

The two radio stacks do not add: turning both off lands exactly on the 515,268 / 24,408 floor,
and ESP-NOW currently costs nothing because P7-C1 has not built it yet.

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
| P8 PIN, creator routes, mobile page + sprite editor (PROGMEM) | 30-45 K | 1.5-3.0 K |
| P9 roster 36→60, sprite atlas, corruption | 15-20 K | 0.2-0.5 K |
| P10 diagnostics, animation, polish | 10-15 K | 0.5-1.5 K |
| **total** | **105-150 K** | **5-11 K** |

### The projection

| ending state | flash | globals | headroom |
|---|---|---|---|
| BLE kept | ~2,045,000 (85 %) | ~80,700 (90 %) | **9 KB of globals** |
| BLE deleted | ~1,333,000 (56 %) | ~57,200 (64 %) | 32 KB of globals |

**It fits either way on flash. On globals it fits comfortably only if BLE goes.**

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

712,466 B of flash and 23,504 B of globals, more than the entire remaining headroom.
Decision **D2 closed on ESP-NOW** (2026-09-02, owner), and the plan's own inventory says
`ble_social.cpp` "becomes `transport_ble.cpp` + discovery **if D2 = BLE**" — which it is not.
P7-C1 already lists it as a removal candidate.

The reason this is a decision and not a cleanup: **ESP-NOW has never run on the board.**
It is chosen on paper (250 B frames, unicast with a send-callback ACK, the same `WIFI_STA`
residency the §40 scanner needs) and P4-C5 proved the protocol over a host loopback with no
radio at all. Deleting BLE removes the only fallback transport before the chosen one has
been demonstrated on hardware. The cheap order is therefore: **bring ESP-NOW up on the
device first (P7-C1), confirm a link, then delete BLE in the same chunk** — not before.

Until then the budget should be read against the release build, which already excludes it.

### 4.2 The gate should police the build that ships

`check.sh` builds only the baseline, so the release figures are seen only when someone runs
`build_matrix.sh`. The caps belong on the artefact that gets flashed. Two options, and the
second is cheaper: add a release build to `check.sh` (+90 s on every gate run), or enforce a
release cap inside `build_matrix.sh`, which already builds that variant. **Done: the matrix
now enforces `GATE_RELEASE_FLASH_MAX` / `GATE_RELEASE_GLOBALS_MAX`.**

### 4.3 The sprite budget breaks during the phase-9 art swap, not at its end

`data/sprites.h` carries `static_assert(SPRITE_DATA_BYTES <= 14336)` and sits at 10,623 B.
One species costs 144 B of art (24x24, two frames).

**A correction to the first version of this section, which had it wrong.** P9-C3 *deletes*
the 38 legacy Nottamagochi body sets as it lands the new roster, and those are 9,448 B of
the 10,623 — only 1,175 B is everything else (icons, emotes, the egg). So the END STATE is
`1,175 + 8,640 = 9,815 B` and it fits the original budget comfortably. The roster does not
need a bigger budget.

What needs headroom is the **window**: the natural order is to land the new art, check it
against the real screens, and only then delete the old sets — and while both are alive the
total is `10,623 + 8,640 = 19,263 B`, which the old 14,336 would have failed with no
explanation attached to the failure.

| state | total | against 14,336 |
|---|---|---|
| today | 10,623 | fits |
| both rosters alive (the swap window) | 19,263 | **breaks** |
| after the legacy sets go | 9,815 | fits |

**Done: raised to 24,576 as a transition allowance, with the derivation in the header and a
note that P9-C3 should bring it back down** — an allowance left standing after the thing it
allowed for is a budget that has stopped meaning anything.

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
`ble_social.cpp`'s 23,504 B still available to reclaim the day two boards have linked.

**The one number to watch is `LINK_RX_SLOTS`.** It is 5,312 of the 5,816 and it was set from
a measurement (a lossy link bursts 19 frames in one direction; eight slots refused three per
clean battle and sixteen would still have overflowed). If phase 8's creator server needs
globals back, that buffer is where they are — and the price of taking them is a link that is
quietly slower with no error anywhere, which is why the number is in
`tests/test_link_transport.cpp` as an assertion and not only in a comment.
