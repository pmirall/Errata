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

## 3. What phases 5-10 will cost

Measured history, baseline deltas:

| phase | chunks | flash | globals |
|---|---|---|---|
| 3 (pet) | 5 | +11,696 | +72 |
| 4 (battle) | 6 | +22,582 | +2,328 |

Phase 4's globals went almost entirely to ONE chunk — P4-C4, the battle screen and its
renderer, at +18,780 / +2,304. That is the shape: **code and data cost flash; screens and
buffers cost globals**, and globals is the scarce one.

Phases 5-10 are 26 chunks and about ten new screens (SCAN, ENCOUNTER, INVENTORY, ACTIVITY,
LINK, TRADE, BREEDING, PIN, QR, and the DIAG expansion). None is as heavy as the battle
screen, which carries a renderer and a 212 B engine state.

| item | flash | globals |
|---|---|---|
| P5 scanner, encounters, capture, items | 15-25 K | 1.0-2.0 K |
| P6 activity score, power states | 8-12 K | 0.3-0.8 K |
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
