# Hardware baseline: what it changes for the firmware

**Input:** `docs/ERRATA_HARDWARE_AND_BATTERY_SPEC.md` (V1 hardware baseline).
**Written:** 2026-09-02, against branch `claude/repo-exploration-sync-bbonku` at P2-C6.
**Purpose:** reconcile the hardware spec with the implementation plan, flag what the
firmware cannot fix, and schedule what it can.

The hardware spec is compatible with the plan almost everywhere. Radios off by default,
OLED off when unattended, timestamps instead of continuous simulation, saves at logical
checkpoints — all of that was already the plan's direction, and P2-C6 has just landed the
radio half of it. Two things genuinely change, and one number does not add up.

---

## 1. New: the device now has a sound output

Every document until now said "no buzzer in v1", and the firmware reflects that: the
`CF_MUTE` flag is persisted and drawn in the status bar, but nothing is ever emitted.
The V1 baseline adds a **passive piezo, ~15 mm, PWM-driven**.

This is cheap to implement and was already anticipated by the plan's `hardware/audio.h`
seam, which existed only to keep a future board from forcing game-code changes. That seam
now gets a real implementation instead of `audio_null.cpp`.

**Verified available in the installed core (esp32 3.1.1, ESP32-C3):**

| Facility | Status |
|---|---|
| `tone(pin, freq, duration)` / `noTone(pin)` | present in `cores/esp32/Arduino.h:247-248` |
| LEDC peripheral driver | `esp_driver_ledc/include/driver/ledc.h` |

So a tone engine needs no library and no external component beyond the piezo itself.

**Design per spec §18/§19** — synthesized tones, never stored audio:

```
beep · chirp · buzz · rise · fall · double-beep · glitch
```

with the event durations the spec gives (UI click 30-80 ms, damage 50-150 ms, capture
150-300 ms, evolution 500-1500 ms). The rules that matter for battery: the GPIO is
inactive between effects, tones are queued rather than blocking, and `noTone()` is called
the moment an effect ends so no PWM is left running.

**Pin required.** This is new hardware, so it does not conflict with the deferred D1
wiring. Free, non-strapping, non-ADC-reserved GPIOs on this board: **3, 4, 6, 7**.
Proposal: `PIN_PIEZO 3`. Recorded as decision **D8** — the owner confirms when soldering.

---

## 2. New: 2×AAA — and this is where the numbers stop working

The baseline moves from an unspecified supply to **two AAA cells in series, ~3.0 V
nominal**, feeding the SuperMini. The spec correctly flags this as an item to validate
empirically. Validation is not needed to see the first problem.

### The voltage window

An alkaline AAA is 1.5 V at rest and drops to roughly 1.4 V the moment it is loaded, then
spends most of its life on a 1.35-1.2 V plateau before collapsing. Two in series:

| Cell state | Pack voltage |
|---|---|
| Fresh, unloaded | 3.0 V |
| Fresh, under load | ~2.8 V |
| ~50 % discharged | ~2.6 V |
| ~80 % discharged | ~2.4 V |

**The ESP32-C3's brownout detector is armed at level 7 in the core this project builds
with** — verified in `sdkconfig.h`: `CONFIG_ESP_BROWNOUT_DET 1`,
`CONFIG_ESP_BROWNOUT_DET_LVL 7`. Level 7 is the *highest* threshold, around 3.0 V.

That is the whole discharge curve sitting below the reset threshold. Powered this way the
device would brown out almost immediately, and the failure mode is a reset loop, not a
graceful shutdown. Two further points make it worse rather than better:

- **The board's regulator cannot help.** An LDO is a step-down device. Feeding 3.0 V into
  `5V`/`VIN` cannot produce 3.3 V; it produces roughly 2.8 V minus dropout. Feeding the
  pack into `3V3` bypasses the regulator, which the hardware spec §3 explicitly warns
  against, and still leaves the chip seeing raw pack voltage.
- **Lowering the brownout threshold is not a real fix.** Level 0 is ~2.5 V, which buys
  part of the curve, but arduino-cli cannot change that setting without rebuilding the
  core, and disabling brownout in software invites flash corruption during the exact
  low-voltage window it exists to protect against. The save system is crash-safe by
  design; it is not corrupt-flash-safe.

### The options, cheapest first

| Option | Cost | Usable capacity | Notes |
|---|---|---|---|
| **Boost converter** (e.g. TPS61023, or a generic module) | ~€0.40-0.80 | ~90 % — runs down to 1.8 V pack | Keeps 3.3 V flat across the whole curve. Pick one with low quiescent current (TPS61023 is ~12 µA); a cheap PFM module with 1 mA quiescent would eat the battery budget by itself. **Recommended.** |
| **2×AAA lithium** (Energizer Ultimate) | ~€1/pair | high, flat curve | 1.8 V fresh, long 1.5 V plateau. Works into `3V3` with no added electronics, but costs more per swap and still ends below 3.0 V eventually. |
| **3×AAA** (4.5 V) | one more cell + volume | good | The board's own LDO finally has headroom. Costs enclosure space and wastes ~27 % in the LDO. |

This is a hardware decision, not a firmware one, so it is recorded as **D9** rather than
resolved here. The firmware work below is unaffected by which option wins.

### The other number: the board's power LED

The SuperMini carries an always-on power LED. Typical draw is 1-3 mA, continuously,
whether the device is asleep or not.

The arithmetic for the spec's **≥30-day target**, assuming the voltage problem is solved
and ~1100 mAh is actually usable:

```
30 days = 720 h  →  budget = 1100 mAh / 720 h ≈ 1.5 mA average
```

Against that budget, using the spec's own "normal use" profile of 30-90 min/day:

| Load | Draw | Daily cost |
|---|---|---|
| Active use, 60 min/day (CPU + OLED) | ~25 mA | 25 mAh |
| Deep sleep, 23 h/day (chip 5 µA + regulator quiescent) | ~0.1 mA | 2.3 mAh |
| **Subtotal** | | **~27 mAh/day → ~40 days** |
| Power LED, 24 h/day | 2 mA | **48 mAh/day** |
| **With the LED left in place** | | **~75 mAh/day → ~14 days** |

**The power LED alone costs more per day than the entire rest of the device.** Removing it
is not an optimization, it is the difference between hitting the target and missing it by
half. It is a single desoldering or a cut trace.

The same logic is why the firmware's `PIN_LED` blink path is worth keeping dormant: on the
deferred D1 map it points at GPIO5 where nothing is populated, so it costs nothing today,
and the on-screen ERROR state already carries the diagnostic job.

---

## 3. What the firmware already does right

Worth stating so these do not get "optimized" again later:

- **Radios off by default** — landed in P2-C6 (§11 of the hardware spec). Wi-Fi is
  requested only by the screens that need it and released on exit. SNTP is gone, so there
  is no background reconnect loop.
- **No periodic scanning** — the scanner (P5-C1) is explicitly user-triggered, matching
  §12's correct/incorrect diagram.
- **Timestamps, not continuous simulation** — §15/§16 ask for exactly the model already
  in place: a 1 Hz tick only while awake, and `sim_catch_up_ex()` reconstructing elapsed
  time on wake. Care decay is already computed from an epoch delta, not accumulated
  tick by tick.
- **Saves at checkpoints** — the 300 s cadence plus forced saves after real actions is
  §22's model. One refinement is worth making: the 60 s `t` timestamp write is 1440 NVS
  writes/day, and can drop to a write-before-sleep plus a longer cadence.

## 4. What changes in the plan

| Where | Change |
|---|---|
| **P6-C1** | `motion_null.cpp` stays (no accelerometer in V1 — the hardware spec puts it in §29 future upgrades), but `audio.h` stops being a null stub. |
| **P6-C3** | Gains the piezo-aware sleep rules from §20: every GPIO to a known state before sleeping, PWM off, no floating inputs. Deep sleep still blocked behind `ER_PINS_CONFIRMED` by D1, and the hardware spec §8 independently confirms the GPIO0-5 wake constraint that made me flag it. |
| **P10-C2** | `audio_null.cpp` becomes a real `audio_piezo.cpp` tone engine, and the sound setting finally does something. Moved earlier if the piezo arrives before Phase 10. |
| **New: battery monitor** | §26 wants NORMAL/LOW/CRITICAL. `PIN_VBAT_ADC 0` is already reserved and GPIO0 is ADC1_CH0, so this needs only two resistors as a divider. Recorded as **D10**; the firmware side is small and lands with the power states. |
| **§21 audit** | Release build disables verbose serial. Already scheduled in P10-C5's release target; now it has a battery justification, not just a tidiness one. |

## 5. New decisions to record

| # | Decision | Status | Default |
|---|---|---|---|
| **D8** | Piezo GPIO | OPEN (owner confirms when soldering) | Propose `PIN_PIEZO 3` — free, non-strapping, not an ADC pin the battery monitor wants |
| **D9** | Supply architecture for 2×AAA | **OPEN — blocks the ≥30-day target** | Recommend a low-quiescent boost converter; alternatives are lithium AAA or 3 cells |
| **D10** | Battery sense divider on GPIO0 | OPEN | Two resistors; without it the LOW/CRITICAL levels of §26 cannot exist |

## 6. The one thing to do before buying anything else

The hardware spec's §33 already says the next milestone is measurement, not purchasing.
Agreed, with one addition: **measure the pack voltage under load with the OLED on and a
Wi-Fi scan running**, because that is the moment the brownout will fire if it is going to.
That single measurement decides D9, and D9 decides whether the 30-day target is a firmware
problem at all.

---

## 7. D9 closed: boost converter (2026-09-03)

The owner fitted a 3.3 V boost module fed from the 2×AAA pack, and is desoldering the
board's power LED. Both problems in §2 are therefore solved, and the arithmetic changes.

### New energy budget

A boost lets the cells run down to ~1.0 V each instead of dying at 3.0 V pack voltage:

```
2×AAA to 1.0 V/cell        ≈ 1100 mAh at ~2.4 V average  ≈ 2640 mWh
boost efficiency ~85 %                                    ≈ 2244 mWh
usable at 3.3 V                                           ≈ 680 mAh
```

Against the hardware spec's own "normal use" profile of 60 min/day active:

| Idle draw (boost quiescent + sleeping chip) | Daily cost | Runtime |
|---|---|---|
| 25 µA | 25.6 mAh | **~26 days** |
| 200 µA | 29.6 mAh | ~23 days |
| 1 mA | 48 mAh | ~14 days |
| 2 mA | 71 mAh | ~9 days |

Active use is 25 mAh/day of that in every row. So the whole spread between nine days and
twenty-six comes from one number nobody has measured yet: **what the boost module draws
while doing nothing**. That is decision D11, and it is now what the power LED used to be —
the single component that decides whether the target is reachable.

Measuring it is a multimeter in series with the cells, chip in deep sleep, OLED off,
radios off. No instrumentation beyond that.

### The new failure mode: transient collapse, not slow decline

The old risk was the pack sagging below brownout over weeks. The new one is instantaneous.
The ESP32-C3 draws ~350 mA in Wi-Fi TX; pulled through a boost from 2.4 V cells whose
internal resistance is around 0.3 Ω, that transient can drop the rail far enough to reset
the chip. It would show up exactly where the hardware checklist §30 already looks: "Wi-Fi
scan does not cause resets".

Two mitigations, both cheap:

- **Hardware (D12):** a 100–470 µF electrolytic across the boost output absorbs the
  transient. Cents.
- **Firmware (already specified):** plan P5-C1 calls `WiFi.scanNetworks(async, show_hidden,
  **passive=true**, …)`. A passive scan listens on each channel instead of broadcasting
  probe requests, so it avoids most of the TX bursts a normal scan would produce. This was
  chosen for privacy reasons and turns out to be the low-current option too.

If the bench still shows resets with both in place, the next firmware lever is
`esp_wifi_set_max_tx_power()` — but a passive scan barely transmits, so reach for the
capacitor first.
