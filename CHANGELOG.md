# Changelog

All notable changes to Pebblebol are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
Versions are tagged at phase boundaries of `PEBBLEBOL_IMPLEMENTATION_PLAN.md`; the
tag for a phase is cut only when its gate (`tools/check.sh`) and its variant matrix
(`tools/build_matrix.sh`) are both green.

## [0.3.0-pet] — Unreleased

Phase 3 turns the Pebblebol core engine into a virtual pet. Care moved onto the hours scale
spec §27 asks for, XP and levels arrived with an anti-farm ledger, the light switch was
deleted and sleep now follows the sun, evolution became a data table with a ceremony behind
a confirmation, and the minigames left `ui.cpp` for a framework with six two-button games
in it. A tap lands 255 ms sooner than it did. Nothing here talks to a cloud service either.

Every commit in this range passes the gate: firmware compile with `--warnings all` at 0
project warnings, size caps, and `make -C tests check`.

### Added

- **Care on the hours scale** (`data/balance.h`, new): every tunable in one file — care
  rates (`-4,200 / -3,000 / 0 / -2,000 / -6,000` milli-points per hour), action gains,
  cooldowns, gain caps, `XP_TABLE[31]`, the daylight table and the sleep constants. Health is
  no longer a decay rate hiding in a decay array: the slot is 0 and the bleed is
  `CARE_HEALTH_BLEED_MPH`, behind `CARE_ZERO_GRACE_S`, with `HEALTH_FLOOR_PCT` under it.
- **XP and levels** (`game/xp.{h,cpp}`): a 30-level curve in data, multi-level carry-over, one
  shared `xp_hp_rescale()`, and a device-wide anti-farm ledger that rides two already-reserved
  `Inventory` fields — **no save-layout change**. A save/restore round trip can only
  under-report the budget, never invent a point, and a reboot cannot refill a spent one.
- **Sleep follows the daylight** (`game/daylight.{h,cpp}`, decision D13): a twelve-entry
  sunrise/sunset table interpolated by day of year, integer arithmetic only. Bedtime is
  sunset + 90 min, morning is sunrise. Three refused gestures inside ten seconds wake the pet
  and the third one then lands; waking costs no stat (§27 forbids punishment).
- **Data-driven evolution** (`data/evolution_table.h`, `game/evolution.{h,cpp}`): all five
  §18 condition kinds behind a **validity mask** — a condition whose input this phase cannot
  supply refuses, it never passes. A confirmation modal gates it, a decline leaves the pending
  bit set, and the model change plus both its flushes land **before** the ceremony's first
  frame. The roster grew from one placeholder row to family 1's three real species (D14).
- **Minigame framework** (`minigames/`): six hooks split into `MgLogic` (pure) and
  `MinigameDef` (plus `draw`), a fixed-size `MgCtx` with no heap, and a **pure** manager, so
  "exactly one report per game" is a host test rather than a hope. Six games — PING,
  SECUENCIA, PACKET FLOOD, FIREWALL, BUFFER, DELETE — each a `*_logic.cpp` / `*_draw.cpp`
  pair, every run reproducible from its seed and every run under a fixed ceiling.
  **Corrected after the exit:** this said "every run a constant length", which is true of two
  of the six. FIREWALL (12,000 ms) and BUFFER (10,000 ms) are constant under every tape. The
  four §29 games have a fixed *upper bound* no press can exceed — 11,675 / 12,000 / 10,000 /
  11,600 ms, the idle case — and presses can only shorten a run: PACKET FLOOD collapses to
  9,875 ms when it is played and DELETE to 175 ms when B is mashed. PING and SEQUENCE are
  not constant in any sense (PING's idle length varies with the seed; SEQUENCE is the one
  game a player can lengthen, 2,100 ms idle against a 14,100 ms stretch).
- **Host tests**: `test_xp` (16), `test_daylight` (7), `test_evolution` (18),
  `test_minigames` (49), plus 11 minigame pixel goldens and the confirmation snapshot.

### Changed

- **A tap is emitted at RELEASE**, ~25 ms after the button comes up instead of 280 ms.
  `GST_DBL_L/R` and `DOUBLE_TAP_WINDOW_MS` are gone from every header and every call site;
  `input_pressed_edge()` is the press-edge seam the games read.
- **The chunking test means something.** `care_one_hour_of_catch_up_is_the_same_however_it_is_chunked`
  could not fail as written — `sim_tick()` chops any dt into 60 s sub-steps, so its three cases
  were one case. The 1 s case is in it now, which is the step size the live device runs.
  **Corrected after the exit:** that case still could not see `sim_tick()`'s chopper — the hour
  it runs (10:00 → 11:00, fresh hatchling) contains no rate *change*, so one `sub_step(3600)`
  lands on the same 128 bytes as sixty `sub_step(60)`, and deleting the chopper outright left
  all 21 binaries green. `care_a_finer_step_moves_a_rate_change_by_less_than_one_substep` now
  runs three chunks *above* the grid over spans that **do** contain a rate change — four awake
  hours an hour at a time, a whole night in one 28,800 s call, and an hour at a time across
  sunrise with the wall clock moving — where the gaps go from 25 / 11 / 126 milli to
  1,858 / 3,594 / 3,655 without the chopper. Reachable on the device: `GOD_SCALE_4` is 3600
  and `app.cpp` calls `sim_tick(sim_step_seconds())`, so god mode hands `sim_tick()` an hour
  in one call. Two more claims in that test were false and are corrected: the 60 × 60 and
  6 × 600 cases guard nothing, not even the "*`SIM_SUBSTEP_S` still divides an hour*" they were
  documented as guarding (set it to 7 and the case still passes), and the whole equality-only
  case passed all 44 of its checks with `sim_tick()` stubbed to a no-op, which a liveness
  assertion now prevents.
- **`test_sim_golden.cpp` → `test_care_golden.cpp`**, `golden/sim_v1.txt` →
  `golden/care_v2.txt`. The `v1` meant the legacy v1 *simulation* the transcript was first
  recorded from; two retunes later that is not what it pins. Only the header line changed.
  The rename also touched `core/rng.cpp`, which the exit commit never explained: it is a
  one-line comment edit and nothing else — the header note "`tests/golden/sim_v1.txt` depends
  on it" became "`tests/golden/care_v2.txt` depends on it". `xorshift32` is untouched.

### Removed

- **The light.** `PF_LIGHT_ON`, `ACT_LIGHT_TOGGLE`, `MULT_LIGHT_ON_SLEEP`, five
  `STR_*_LIGHT`, the CARE row and the SETTINGS row. Bit 0x0004 of the live flag word and bit
  0x10 of `PebbleInstance.status` are reserved, never reused, never written; no other bit
  moved, and the v1 migration drops the old bit rather than carrying it. It was the worst
  mechanic in the game: a player who never found the toggle owned a Pebble that never slept.
- SALTO, which is in no §29 list, and `MinigameState` with it.
- The menu's repeat-last-action (in no spec section; its empty path toasted "bad argument")
  and `s_last_action`, which after that was written in three places and read in none.

### Fixed

- **A sleeping pebble could not poop overnight, ever, on real hardware.** `poop_step()`
  scaled its advance by `MULT_SLEEP` (×0.35) and truncated it every sub-step with no carry:
  `(1 * 350) / 1000` is 0, and 1 s is the step the device runs outside god mode. An offline
  catch-up over the same night (60 s sub-steps, an exact 21) produced two poops. It carries
  its remainder now, the way the stat integrator always has. Found by adding the 1 s case to
  the chunking test — the exercise the phase-3 exit asked for. Nothing at dt = 60 moved, so
  the care golden did not need re-recording. **Bounded by `POOP_MAX`:** a pebble already
  sitting on four uncleaned poops has nowhere to put a fifth, so a *neglected* pebble's
  trajectory does not move at all — reintroducing the truncation leaves every neglect case,
  the fortnight case and the golden green. It is the played pet, the one that goes to bed
  clean, that the bug took the overnight poop away from.
- **The stage carry, which shipped unverified.** `stage_step()` lost the same class of leak in
  the same commit (`g.acc_stage = 0` → `-=`), but nothing tested it: reverting it left the
  whole suite green. `care_the_stage_check_keeps_its_cadence_at_any_chunk_size` covers it now
  — driven 90 s and 100 s at a time, neither a multiple of `STAGE_CHECK_PERIOD_S`, the
  baby → child transition is noticed at the second it is due instead of a whole chunk late —
  and a `static_assert` pins the `SIM_SUBSTEP_S <= STAGE_CHECK_PERIOD_S` that makes one
  subtraction enough. That assert also turns `SIM_SUBSTEP_S = 3600` from a single arithmetic
  identity in one test into a compile error in the firmware and all 21 binaries.
- Three minigame framework defects, each of which would have bitten the four §29 games:
  `MgCtx::state` sat at offset 14, so every 32-bit game field was read at 2 mod 4;
  `draw_str_list()` sized its row array `CARE_ROWS` and clamped, so two PLAY rows would never
  have drawn; and the in-run pause was on the same physical press the games are played with,
  and the game kept stepping and eating press edges under the dialog it opened.
- `ui.cpp` no longer freezes a chained evolution for a frame, and the model change is flushed
  before the ceremony arms rather than after.

### Measured

| Variant | Overrides | Flash (B) | Static RAM (B) | Warnings |
|---|---|---|---|---|
| baseline | — | 1,893,072 | 70,348 | 0 |
| no-ble | `FEATURE_BLE=0` | 1,180,684 | 46,868 | 0 |
| no-web | `FEATURE_WEB=0` | 1,263,616 | 49,420 | 0 |
| no-god | `GOD_MODE_ENABLED=0` | 1,881,054 | 70,204 | 0 |
| sh1106 | `DISPLAY_IS_SH1106=1` | 1,893,072 | 70,348 | 0 |
| all-off | all `FEATURE_*`=0 + `GOD_MODE_ENABLED=0` | 492,610 | 22,112 | 0 |
| **release** | `GOD_MODE_ENABLED=0 FEATURE_BLE=0` | **1,168,772** | **46,708** | 0 |

The whole phase cost **11,696 B of flash and 72 B of static RAM**; the baseline sits at 79 %
of the 2,400,000 B gate cap and the release build at 37 % of the 3,145,728 B `app0` slot,
where phase 2 left it. (Reworded after the exit: as first written, "cost 11,696 B … which is
79 % of the cap" attached the percentage to the phase delta rather than to the 1,893,072 B
baseline total. Both figures were right — 1,893,072/2,400,000 = 78.9 %,
1,168,772/3,145,728 = 37.2 % — but the antecedent was not.) Host suite: **21 binaries, 328 tests, 350,214 checks** (phase 2 shipped
17 / 205 / 185,482).

**The phase-3 exit soak, run rather than asserted from a comment.** Fourteen simulated days
of total neglect — a hatched pebble, a trustworthy clock, not one action for a fortnight —
recording the longest *continuous* run each stat spends at 0:

| Stat | First reaches 0 | Longest run at 0 | Ends at |
|---|---|---|---|
| hunger | 30.8 h | 305.2 h | 0 % |
| happiness | 27.5 h | 160.2 h | 40 % |
| **energy** | 34.7 h | **2.1 h** | 79 % |
| cleanliness | 24.7 h | 311.4 h | 0 % |
| health | never | 0.0 h | 10 % |

Energy — the one core stat the simulation restores by itself — is never at 0 for more than
6 continuous hours and is above 90 % at all 14 sunrises (99 % every time). Over 480
fortnights (40 genesis genomes × 12 month anchors) its worst run is 3.87 h and it exceeds 6 h
in 0 of 480; at a metabolism gene of 15 (×1.50), which genesis cannot roll, it is 5.40 h.
Health reaches 0 in none of them. Hunger, happiness and cleanliness are pinned for most of
the fortnight and are meant to be — they are the ones only the player can refill. Scoped to a
valid clock on purpose: without one there is no night, and energy pins at 0 for 322.78 h of
the same 336. `docs/decisions.md` carries the full tables.

### Corrected after the exit (second P3-C5 follow-up)

Two of the five adversarial lenses meant to check this exit died on API errors before the tag
was cut. They were re-run, and the corrections above marked *"corrected after the exit"* come
from them. Four more that had no inline home:

- **The evolution ceremony is 2.7 s, not 4.5 s.** `docs/decisions.md`, the plan and
  `tests/test_pet_view.cpp` all said 4.5 s. That is the HATCH ceremony. The EVOLVE arm of
  `ceremony_begin()` back-dates `s_t0` by `HATCH_T_FLASH` (1,800 ms) because there is no shell
  to rock or crack, and the show ends at `HATCH_TOTAL_MS` (4,480 ms): 2,680 ms.
- **The three constants "6 h" was said to depend on were all wrong, and the dependence is not
  monotonic.** The criterion first breaks at `CARE_ENERGY_ASLEEP_MPH` ≈ 11,000 (not "much
  below 13,300" — 13,300 breaks nothing), `SLEEP_AFTER_DUSK_MIN` ≈ 240 min (not ~3 h) and
  `CARE_DECAY_MPH[CARE_ENERGY]` ≈ −8,000. Below ≈ 5,000 the pebble stays asleep past sunrise
  (`SIM_WAKE_DAY_ENERGY_PCT` = 60) and the run at 0 gets *shorter* again. Full sweep in
  `docs/decisions.md`.
- **The poop carry cost 28 B of flash, not 32 B** (0 B of static RAM either way, which is
  exact). Measured against a rebuild of the pre-fix `poop_step()`.
- **Three §67 boxes were due and had been left open** — "Two-button input is robust", "Box
  supports 10 Pebbles", "Active Pebble can be selected". The rule the section applies (tick
  when every commit named on the line has landed) is now written down above the list, because
  it was being applied unevenly.

Also: the sentence the exit says it struck from `test_pet_view` was struck only in the plan —
the test file and `tests/Makefile` still asserted it, and now do not; `tools/check.sh` gained
the `esp_random` gate the plan had listed for two phases without it ever existing; and the
god-mode ×3600 soak is now genuinely in the first-flash measurement list two documents claimed
it was already in.

### Not verified

**This firmware has still never run on a physical board.** No ESP32-C3, no panel, no cells.
Everything above is a host measurement or a compile result.

**Evolution is not visible yet.** The model half is done and tested — the rule table, the
conditions, the level gate, the stat recompute, the ceremony. But `pet_view_fill_sim()`, the
path the firmware actually runs, derives the body from the genome, the minor form and the
life stage, none of which an evolution moves, and `sprite_lookup_pose()` never sees
`species_id`. So confirming an evolution today gives back the same body, the same name and a
different `hp_max`. §18's visual transformation is carried to P4-C1, where the roster grows.

## [0.2.0-core] — Unreleased

Phase 2 turns "Nottamagochi", a single 26,703-line Arduino sketch that had never run
on hardware, into the Pebblebol core engine: a layered `src/` tree, a persistence
schema that survives a corrupted blob and an erased NVS partition, a table-driven
screen state machine, and a host test suite that runs the game logic with no board
attached. Nothing in this release talks to a cloud service.

Every commit in this range passes the gate: firmware compile with `--warnings all` at
0 project warnings, size caps, and `make -C tests check`.

### Added

- **Host test suite** (`tests/`, `SRC_ROOT=../Pebblebol/src`): 17 binaries, 205 tests,
  185,482 checks, built with `g++ -std=c++17 -Wall -Wextra` against the real sources —
  no board required. Covers care simulation (against a recorded golden trajectory),
  genome, CRC-16, RNG, clock and calibration, input gestures, persistence and
  migration, the Box, the Box↔sim seam, `game_state`, integer-overflow edges, the
  screen table and the §7 input router, and pixel-exact screen snapshots.
- **Save schema v2** (`persistence/save_schema.h`, `save_manager.cpp`,
  `migration.cpp`): every blob is `magic·version·…·seq·crc16`, `static_assert`ed for
  layout, and stored as a **pair** `<key>0`/`<key>1` with the higher `seq` winning, so
  a write torn by a power cut loses at most the newer copy. A v1 save is migrated
  in place on first boot; `tests/test_persistence.cpp` proves migration, corruption
  recovery, pair recovery and fault injection.
- **`nvs2` checkpoint partition** (decision D6): `Pebblebol/partitions.csv` carves a
  private 64 KB NVS partition, because Arduino's `initArduino()` erases the *whole*
  default `nvs` partition on `ESP_ERR_NVS_NO_FREE_PAGES` / `NEW_VERSION_FOUND` before
  `setup()` ever runs. `save_checkpoint_all()` writes there daily and on the events
  that change what a Pebble *is*. `tools/build.sh` gates both halves of D6: the app
  ceiling must equal `app0`, and the table about to be flashed must contain `nvs2`.
- **Box of ten** (`game/box.{h,cpp}`, `game/box_sim.{h,cpp}`): unique ids, exactly one
  active slot, swap, release behind a double confirm, and off-line recovery for stored
  Pebbles that never touches XP or level.
- **Screen table** (`ui/screen.h`, `app/state_machine.cpp`, `app/input_router.cpp`):
  the §6 set of 26 states as data — five hooks per row plus flags — with a back stack
  of 5, auto-return for non-sticky rows, and the §7 two-button grammar (A steps, B held
  chooses, B tapped goes back, both held goes home). `ui.cpp` has no screen dispatch
  left.
- **`gfx` seam** (`ui/gfx.h`): every screen draws through it, so `tests/fakes/gfx_fb.cpp`
  can render the real screen code into a 128×64 framebuffer, record out-of-bounds
  primitives and diff against committed `.pbm` goldens — the "tested at the actual
  physical resolution" requirement.
- **`ERROR` state** (decision T10) replacing the old blink-forever `rd_fatal()`, plus
  `BOOT` and `LOAD_SAVE`: a save this firmware refuses to touch is now a question with
  two on-screen answers instead of a dead board.
- **Clock calibration without SNTP**: the device learns the date from a human via the
  `TIME` screen or the phone page. An uncalibrated clock charges zero absence rather
  than guessing.
- **Heap trend line** (this commit): `DIAG,heap,<uptime_s>,<free_b>,<min_free_b>` on
  Serial every 60 s, on every screen, in the release build too — the baseline for the
  bench soak.
- **Documentation**: `PEBBLEBOL_IMPLEMENTATION_PLAN.md`, `PEBBLEBOL_IMPLEMENTATION_AUDIT.md`,
  `docs/decisions.md`, `docs/save_schema.md`, `docs/hardware_reconciliation.md`.

### Changed

- **The pivot.** The product is Pebblebol, not Nottamagochi: sketch folder renamed to
  `Pebblebol/`, sources moved under `Pebblebol/src/{core,app,game,ui,networking,persistence,hardware,data,dev}`,
  `Pebblebol.ino` reduced to a 3-line shim over `src/app/app.cpp`, and the persisted
  NVS namespace changed from `"notta"` to `"pbbl"` with a one-shot, idempotent import
  of a legacy save (decision D3, closed).
- **Radio OFF is the resting state** (decision T6). No always-on Wi-Fi policy; a stack
  is brought up only by a screen that needs one and torn down after.
- **Care runs on `PebbleInstance`**, not on the old flat `PetSave`; `sim_switch()`
  separates per-Pebble accumulators from the device-wide gain ledger so swapping the
  active slot cannot be used to farm.
- **`storage.cpp` split** into `hardware/kv_nvs.cpp` (the NVS key-value seam),
  `hardware/boot.cpp` (RTC nonce and reset-reason classification) and
  `persistence/save_manager.cpp` (the save policy), which is what let the pure layers
  compile on the host.
- **Transport decided** (D2): ESP-NOW, behind the §59 `Transport` seam; `FEATURE_BLE 0`
  in the release build. No Phase-2 code depends on it.

### Removed

- The weather subsystem, the Telegram subsystem, the browser minigames, the phone
  page's `/api/cfg` and `/api/sprites`, mDNS, and SNTP — every remote service the
  firmware used to reach for. Pebblebol has no cloud code.
- Death, memorial, lineage, the absence ladder, discipline, weight and adult forms —
  the punishment mechanics the product spec rejects.
- The BLE mating protocol content (the plumbing is kept until P7-C1 retires it).

### Fixed

- A restore could put an old `nvs2` checkpoint over a newer live save; the checkpoint
  is now taken only when it is not older than what is already there.
- A dead panel could swallow an unanswered save question; the question now survives to
  the next boot.
- The gate could pass over a stale test build.
- Audit §8.3 S11: a confirmation dialog started on YES.

### Measured

| Variant | Overrides | Flash (B) | Static RAM (B) | Warnings |
|---|---|---|---|---|
| baseline | — | 1,881,376 | 70,276 | 0 |
| no-ble | `FEATURE_BLE=0` | 1,168,978 | 46,780 | 0 |
| no-web | `FEATURE_WEB=0` | 1,251,972 | 49,332 | 0 |
| no-god | `GOD_MODE_ENABLED=0` | 1,869,150 | 70,116 | 0 |
| sh1106 | `DISPLAY_IS_SH1106=1` | 1,881,376 | 70,276 | 0 |
| all-off | all `FEATURE_*`=0 + `GOD_MODE_ENABLED=0` | 480,766 | 22,024 | 0 |
| **release** | `GOD_MODE_ENABLED=0 FEATURE_BLE=0` | **1,156,866** | **46,620** | 0 |

Baseline flash is 224,172 B below the 2,105,548 B measured on the source commit
`b53cfe4`; the release build occupies 37 % of the 3,145,728 B `app0` slot.
Worst-case offline catch-up (400 days) measures 8.4 ms on the host at `-O1`, projected
at ~421 ms on a 160 MHz ESP32-C3 — well under the 5 s Task WDT, so catch-up does not
need to be resumable.

### Corrected after the exit (second P3-C5 follow-up)

Two of the five adversarial lenses meant to check this exit died on API errors before the tag
was cut. They were re-run, and the corrections above marked *"corrected after the exit"* come
from them. Four more that had no inline home:

- **The evolution ceremony is 2.7 s, not 4.5 s.** `docs/decisions.md`, the plan and
  `tests/test_pet_view.cpp` all said 4.5 s. That is the HATCH ceremony. The EVOLVE arm of
  `ceremony_begin()` back-dates `s_t0` by `HATCH_T_FLASH` (1,800 ms) because there is no shell
  to rock or crack, and the show ends at `HATCH_TOTAL_MS` (4,480 ms): 2,680 ms.
- **The three constants "6 h" was said to depend on were all wrong, and the dependence is not
  monotonic.** The criterion first breaks at `CARE_ENERGY_ASLEEP_MPH` ≈ 11,000 (not "much
  below 13,300" — 13,300 breaks nothing), `SLEEP_AFTER_DUSK_MIN` ≈ 240 min (not ~3 h) and
  `CARE_DECAY_MPH[CARE_ENERGY]` ≈ −8,000. Below ≈ 5,000 the pebble stays asleep past sunrise
  (`SIM_WAKE_DAY_ENERGY_PCT` = 60) and the run at 0 gets *shorter* again. Full sweep in
  `docs/decisions.md`.
- **The poop carry cost 28 B of flash, not 32 B** (0 B of static RAM either way, which is
  exact). Measured against a rebuild of the pre-fix `poop_step()`.
- **Three §67 boxes were due and had been left open** — "Two-button input is robust", "Box
  supports 10 Pebbles", "Active Pebble can be selected". The rule the section applies (tick
  when every commit named on the line has landed) is now written down above the list, because
  it was being applied unevenly.

Also: the sentence the exit says it struck from `test_pet_view` was struck only in the plan —
the test file and `tests/Makefile` still asserted it, and now do not; `tools/check.sh` gained
the `esp_random` gate the plan had listed for two phases without it ever existing; and the
god-mode ×3600 soak is now genuinely in the first-flash measurement list two documents claimed
it was already in.

### Not verified

**This firmware has still never run on a physical board.** No ESP32-C3, no panel, no
cells. Everything above is a host measurement or a compile result. The 1 h heap soak
the phase-exit criterion asks for is instrumented but **not run**, and the on-device
half of the Phase-2 exit criteria (boots to HOME, state survives a power cycle, a
corrupted `pb00` recovers from `pb01`, radio OFF on HOME) is proven on the host only.
`docs/decisions.md` lists what to measure at first flash.

## 0.1.0 — 2026-09-02 (phase 1, not tagged)

The plan cuts tags from Phase 2 onward, so Phase 1 has no `v0.1.0` tag; it is commit
`63643ee`.

### Added

- Repository archaeology: audit of the inherited sketch, the ten-phase implementation
  plan, the decisions log, and a CI skeleton.

[0.3.0-pet]: https://github.com/pmirall/Pebblebol/commit/e2004e7
[0.2.0-core]: https://github.com/pmirall/Pebblebol/commit/db3feb3
<!-- The annotated tags v0.2.0-core and v0.3.0-pet exist in the local
     repository but this environment's git remote refuses tag pushes
     (send-pack disconnects), so each link points at a commit, which does
     resolve. Create the releases from those commits on GitHub to restore
     tag URLs.

     THE TWO LINKS ARE NOT EQUIVALENT, which this note used to imply and the
     P3-C5 follow-up corrected. db3feb3 IS the commit v0.2.0-core points at.
     e2004e7 is the phase-3 EXIT COMMIT, not what v0.3.0-pet points at: the
     tag has been moved forward twice since, by 4bc71d8 and then by this
     follow-up, as post-exit audits found and fixed things. A reader following
     the 0.3.0-pet link therefore lands on the exit as it was cut, before the
     corrections recorded above. That is the useful anchor for a changelog
     entry, but it is the exit commit, not the tag. -->
