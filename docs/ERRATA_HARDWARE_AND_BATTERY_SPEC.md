# ERRATA — Hardware Decisions & Battery Optimization Specification

**Status:** V1 hardware baseline  
**Purpose:** Define the intentionally minimal hardware architecture for Errata and the firmware rules required to maximize battery life.  
**Target hardware:** ESP32-C3 SuperMini + 0.96" OLED + 2× AAA + passive piezo + 2 buttons.

---

# 1. Hardware philosophy

Errata is deliberately designed as a **small, low-cost, low-power physical game device**.

The current hardware architecture removes all components that do not materially improve the core experience.

The device is intentionally limited to:

- ESP32-C3 SuperMini
- 0.96" 128×64 OLED
- 2 × AAA cells
- 2 × physical buttons
- 1 × passive piezoelectric sounder
- integrated 3D-printed battery holder/contact system

The following previously considered components are **removed from the V1 baseline**:

- LiPo battery
- TP4056 charger
- MAX98357A amplifier
- 3 W speaker
- external audio DAC
- external battery-management board
- external regulator, unless testing proves the selected ESP32-C3 board requires one

The principle is:

> **Every hardware component must justify its space, cost, complexity and power consumption.**

---

# 2. Current BOM baseline

The current component spreadsheet records the following approximate per-device material costs:

| Component | Quantity purchased | Unit cost used in BOM | Quantity/device |
|---|---:|---:|---:|
| ESP32-C3 SuperMini | 5 | €3.228 | 1 |
| 0.96" OLED | 5 | €3.118 | 1 |
| Buttons | 100 | €0.0556 | 2 |
| Passive piezo | 10 | €0.798 | 1 |
| AAA contacts/springs | 12 | €0.5825 | 1* |
| AAA batteries | 40 | €0.23525 | 2 |
| Box | 30 | €0.533 | 1 |
| Manual | material estimate | ~€0.072/page | 2 A4 sheets / manual |

The spreadsheet's current calculated component subtotal is approximately **€8.99 per device**. fileciteturn0file0L23-L97

\* The exact number and geometry of battery contacts must be finalized during mechanical design. The spreadsheet allocation of one contact set per device should not be treated as the final mechanical specification.

---

# 3. ESP32-C3

## Decision

Use the **TENSTAR ROBOT ESP32-C3 SuperMini** already purchased as the V1 controller.

The controller provides the functionality needed by Errata:

- Wi-Fi
- BLE
- GPIO
- I²C
- PWM
- low-power sleep modes
- sufficient CPU/RAM for the game
- integrated 3.3 V regulation on the selected development board

The application does not require the processing power of an ESP32-S3.

## Important constraint

Do **not** feed the raw battery directly into the board's `3V3` pin.

The correct battery-power path for the selected board must be validated experimentally against the board's actual regulator and 5 V/VBAT routing before committing the final PCB/mechanical wiring.

For V1 prototyping, power architecture is deliberately treated as an empirical engineering item rather than adding an external regulator speculatively.

---

# 4. Power source: 2× AAA

## Decision

Use **two AAA cells in series** as the primary power source.

Nominal battery voltage:

```text
1.5 V + 1.5 V ≈ 3.0 V
```

The ESP32-C3 operating range is around the 3.3 V rail, so the exact behavior of the selected SuperMini over the full AAA discharge curve must be validated in hardware.

This architecture is chosen because it eliminates:

- LiPo charging circuitry
- USB charging hardware
- LiPo connector dependence
- dedicated battery-management board
- integrated charger space
- charging complexity

It also makes the product easy to service:

> Open the battery compartment → replace two AAA cells → continue playing.

## Mechanical integration

The battery holder should be integrated directly into the printed ABS enclosure.

Prefer:

- 3D-printed cylindrical battery channels;
- spring/contact terminals purchased separately;
- no commercial plastic battery holder;
- direct battery access from the enclosure.

The battery compartment should be mechanically captive enough to prevent rattle, but removable enough to replace cells easily.

---

# 5. Battery contact architecture

The two AAA cells are wired in series:

```text
AAA #1 (+) ───── AAA #2 (-)

AAA #1 (-) ──────────────── SYSTEM -
AAA #2 (+) ──────────────── SYSTEM +
```

The central series connection may be implemented with a dedicated contact piece or two independent contacts depending on mechanical constraints.

Do not assume that the current BOM's "one contact" allocation is sufficient for the final enclosure.

Design the contact layout together with the mechanical battery cradle.

---

# 6. Audio: passive piezo only

## Decision

Remove the MAX98357A and conventional speaker from V1.

Use one **passive piezoelectric sounder**, approximately:

- diameter: **~15 mm**
- passive / externally driven
- suitable for direct low-power MCU excitation
- thin enough to fit the sandwich enclosure

The current BOM contains 10 piezos at an approximate unit cost of €0.798. fileciteturn0file0L55-L61

## Why

Errata needs:

- UI beeps
- short sound effects
- attack sounds
- damage sounds
- capture sound
- level-up
- evolution
- warning/error sounds

It does **not** need:

- speech
- music playback
- high-fidelity audio
- high volume
- 3 W output

The passive piezo can be driven by the ESP32-C3 using PWM/tone generation.

The final firmware should treat the piezo as an event-output device, not as a general audio speaker.

## Mechanical acoustic design

The enclosure should include a small acoustic opening/chamber.

Recommended arrangement:

```text
[ PIEZO ]
    ↓
acoustic cavity
    ↓
small sound port
    ↓
outside
```

Do not permanently glue the piezo to a flexible part of the enclosure without testing the acoustic result.

The enclosure itself can contribute to perceived volume.

---

# 7. Screen: 0.96" 128×64 OLED

## Decision

Use the current 0.96" OLED for V1.

The current BOM assigns approximately €3.118 per OLED. fileciteturn0file0L39-L45

The OLED is intentionally small and monochrome because the game's visual language is pixel-art and the screen must fit inside the sandwich enclosure.

The screen should be considered an **active-use peripheral**, not a permanently-on status display.

## Battery rule

After inactivity:

1. stop animation;
2. blank/disable the display;
3. enter a low-power state.

Never keep the OLED continuously refreshing just so that the Bug appears "alive".

---

# 8. Buttons

Use two physical buttons:

```text
A = primary / confirm / action
B = back / secondary / cancel
```

The current BOM assigns two buttons per device at approximately €0.1112 total. fileciteturn0file0L47-L53

Buttons should support:

- short press;
- long press where needed;
- debouncing.

The buttons should preferably be usable as wake-up sources from low-power states.

The ESP32-C3 supports GPIO wake-up mechanisms in low-power modes; GPIO0–GPIO5 can be used for Deep-sleep wake-up on the C3, while other GPIO wake-up behavior depends on sleep mode. citeturn964267search0turn964267search20

Therefore:

> Place the two user buttons on suitable wake-capable GPIOs if the selected SuperMini pinout allows it.

This is preferable to having an always-on polling loop.

---

# 9. Recommended physical stack

The enclosure should remain close to the volume dictated by the components.

Conceptual sandwich:

```text
┌──────────────────────────────┐
│          FRONT               │
│                              │
│          OLED                │
│                              │
│       A           B          │
│                              │
├──────────────────────────────┤
│      ESP32-C3 + PIEZO        │
├──────────────────────────────┤
│          AAA   AAA           │
│                              │
└──────────────────────────────┘
             BACK
```

The exact arrangement should be optimized after measuring the actual purchased modules.

The PCB/module dimensions, wiring exits and button travel matter more than the nominal component dimensions.

---

# 10. Power states

Errata should have four conceptual power states:

```text
ACTIVE
IDLE
SLEEP
DEEP SLEEP
```

## ACTIVE

Everything needed for immediate interaction can be active:

- CPU
- OLED
- input
- game logic
- piezo when required

Wi-Fi and BLE remain OFF unless actively required.

## IDLE

After short inactivity:

- OLED OFF
- Wi-Fi OFF
- BLE OFF
- CPU reduced where practical
- no unnecessary polling

The device remains able to wake rapidly.

## SLEEP

Longer inactivity:

- display OFF
- wireless OFF
- unnecessary peripherals OFF
- use low-power sleep

## DEEP SLEEP

Long unattended periods:

- only required RTC/wakeup functionality remains;
- wake on button/timer/event where appropriate.

Espressif documents deep sleep for the ESP32-C3 at approximately **5 µA at chip level** under the corresponding conditions. citeturn964267search1

The actual Errata current will be higher because the complete system includes the development board, regulator, OLED and other hardware.

---

# 11. The single biggest battery optimization

## Keep the radios OFF by default

Wi-Fi must NOT run continuously.

Normal operation:

```text
Wi-Fi = OFF
BLE  = OFF
```

Enable Wi-Fi only for:

- explicit Bug/network scan;
- local creator connection;
- any future feature that genuinely requires it.

Enable BLE only for:

- active peer discovery/session;
- battle;
- trade;
- breeding;
- other explicit local interactions.

After use:

```text
disconnect
↓
disable radio
↓
return to low power
```

Do not keep a wireless connection alive for convenience.

Wi-Fi/Bluetooth connections are not maintained during ESP32-C3 deep/light sleep in the usual disconnected low-power architecture; maintaining connectivity requires modem-sleep/light-sleep behavior instead, which costs significantly more power. citeturn964267search2

---

# 12. Wi-Fi scanning strategy

Wi-Fi scanning must be event-driven.

Correct:

```text
USER:
"Search networks"

       ↓

Wake
       ↓
Wi-Fi ON
       ↓
Scan
       ↓
Process results
       ↓
Generate encounter
       ↓
Wi-Fi OFF
       ↓
Return to sleep
```

Incorrect:

```text
scan
wait 30 s
scan
wait 30 s
scan
...
```

Never repeatedly scan in the background merely to make exploration feel "live".

The player should explicitly or implicitly trigger exploration.

---

# 13. BLE strategy

Do not advertise continuously unless a future product decision explicitly requires it.

For peer interaction:

```text
PLAYER STARTS LINK
        ↓
BLE ON
        ↓
DISCOVERY
        ↓
SESSION
        ↓
BATTLE / TRADE / BREED
        ↓
DISCONNECT
        ↓
BLE OFF
```

This preserves battery without damaging the core social interaction.

---

# 14. OLED optimization

OLED is one of the most important non-radio battery loads.

Use:

- short active display timeout;
- no full-frame redraw when unnecessary;
- static screens when possible;
- reduced animation frame rate;
- display shutdown/blanking during idle;
- re-enable only on interaction.

Suggested UX:

```text
0–30/60 s after interaction
→ normal display

after inactivity
→ screen off

long inactivity
→ deep/light sleep
```

The exact timeout must be tuned through real-world testing.

---

# 15. CPU optimization

Do not run the application at maximum CPU activity continuously.

Optimization rules:

- avoid busy loops;
- use timed waits;
- use interrupts for buttons;
- sleep between tasks;
- run animations only while visible;
- calculate pet time deltas on wake instead of continuously updating them;
- do not simulate second-by-second state when minute/hour granularity is sufficient.

For example, hunger should not be implemented as:

```text
every second:
    hunger -= tinyAmount
```

Prefer:

```text
on wake:
    elapsed = now - lastUpdate
    hunger = calculateHunger(lastState, elapsed)
```

This is both simpler and more power-efficient.

---

# 16. Time-based simulation

Persistent systems should use timestamps rather than continuous background execution.

Store:

```text
lastPetUpdate
lastActivityUpdate
lastNetworkScan
lastNetworkEncounter
lastInteraction
```

When waking:

```text
elapsed = currentTime - storedTime
```

Then update all relevant state.

This means Errata can safely sleep for hours without running its CPU.

The ESP32-C3 supports timer wake-up from sleep. citeturn964267search2

---

# 17. Sensor strategy

Do not continuously sample sensors unless the gameplay actually needs it.

For movement/activity:

- sample periodically;
- accumulate an abstract activity score;
- sleep between samples;
- do not attempt impossible precision without suitable hardware.

The goal is:

```text
"Bug has been carried a lot"
```

not:

```text
"Bug traveled exactly 2.3847 km"
```

Activity can be aggregated and applied when the CPU wakes.

---

# 18. Piezo optimization

The piezo should be electrically quiet when not producing sound.

Rules:

- GPIO inactive except during sound playback;
- generate tone only for the required duration;
- avoid continuous PWM;
- use a short event queue;
- stop the tone immediately after the sound effect.

Example:

```text
UI click      30–80 ms
Damage        50–150 ms
Capture       150–300 ms
Evolution     500–1500 ms
```

Exact durations should be tuned by UX testing.

The point is not maximum volume.

The point is recognizable sound identity with minimal energy.

---

# 19. Audio design for power efficiency

Prefer synthesized tones over stored audio files.

A small tone engine can create:

```text
beep
chirp
buzz
rise
fall
double-beep
glitch
```

This removes:
- audio file storage;
- decoding;
- streaming;
- amplifier hardware.

A large sound system is outside V1 scope.

---

# 20. GPIO configuration during sleep

Every GPIO that does not need to remain active should be placed into a known low-power state before sleeping.

Avoid:

- floating inputs;
- unnecessary pull resistors;
- active peripheral outputs;
- LEDs left on.

ESP-IDF provides GPIO hold/sleep configuration facilities for retaining or defining pin states across low-power modes. citeturn964267search0

Because ESP32-C3 digital GPIO states have specific Deep-sleep behavior, configure and test the selected pins explicitly rather than assuming normal runtime configuration will persist unchanged. citeturn964267search0

---

# 21. Avoid the most common hidden battery drains

The firmware must explicitly audit:

- debug logging;
- serial output;
- status LEDs;
- OLED refresh;
- Wi-Fi reconnect loops;
- BLE scanning;
- retry loops;
- timers firing too frequently;
- sensors left at maximum sample rate;
- PWM left enabled;
- piezo left active;
- unnecessary filesystem writes.

During release operation, verbose serial logging should be disabled or heavily throttled.

---

# 22. Save strategy

Frequent flash writes waste power and flash endurance.

Do not save on every frame or every tiny state change.

Prefer:

```text
change state in RAM
        ↓
mark save dirty
        ↓
save at logical checkpoints
        ↓
sleep
```

Good save points:

- after item acquisition;
- capture;
- evolution;
- trade;
- breeding;
- battle end;
- major care interaction;
- settings changes.

The save system must remain crash-safe.

---

# 23. Wireless encounter data

Wi-Fi scanning is a gameplay sensor, not a data-collection system.

The firmware should:

- scan locally;
- derive an ephemeral/local identity;
- generate the event;
- discard unnecessary raw network data;
- never connect to discovered networks merely for gameplay;
- avoid uploading scan data.

The encounter engine should operate without internet access.

---

# 24. Expected battery-life model

Battery life should be treated as a measured engineering target, not a theoretical promise.

For two AAA cells, the useful capacity depends strongly on:

- battery chemistry;
- discharge current;
- regulator efficiency;
- ESP32 radio activity;
- OLED current;
- firmware sleep behavior;
- brownout threshold.

Therefore the final specification should use a real discharge test.

## Initial product target

Design for:

> **≥30 days of normal use per pair of AAA cells**

with an aspirational target of:

> **~45–60 days**

provided the device spends most of its time asleep/idle, the OLED is off when unattended, and wireless radios are disabled except during active interactions.

These are **engineering targets, not guaranteed runtimes**.

---

# 25. How to measure real battery life

Create a repeatable battery test.

## Test A — Idle

Device powered but not interacted with.

Measure:

```text current
battery voltage
time to low-battery threshold
```

## Test B — Normal use

Simulate a realistic player:

```text 30–90 min/day active
1–3 short Wi-Fi scans/day
occasional sound
occasional menu interaction
occasional games
rest of time idle/sleep
```

Measure runtime.

## Test C — Heavy use

Simulate:

```text frequent screen usage
many minigames
many Wi-Fi scans
frequent BLE sessions
frequent sound
```

Measure runtime.

## Test D — Worst-case stress

Measure:
- Wi-Fi activity;
- audio;
- OLED;
- CPU;
- simultaneous load.

Check for resets/brownouts.

---

# 26. Low-battery behavior

Implement at least three levels:

```text
NORMAL
LOW
CRITICAL
```

At low battery:

- show a brief warning;
- avoid unnecessary Wi-Fi/BLE activity;
- reduce screen activity where appropriate.

At critical battery:

- stop nonessential radios;
- save;
- enter safe sleep;
- preserve the game state.

Never wait until a brownout corrupts the save.

---

# 27. Mechanical battery serviceability

The AAA compartment should:

- require no soldering for battery replacement;
- provide polarity markings;
- prevent incorrect insertion where practical;
- hold cells firmly;
- avoid direct compression against the OLED/PCB;
- keep contacts mechanically secure.

Battery contacts should have enough spring force to maintain contact under movement without requiring excessive insertion force.

---

# 28. Version-1 hardware rule

Do not add hardware just because a feature is imaginable.

The V1 hardware baseline is intentionally fixed:

```text
ESP32-C3
OLED
2 buttons
piezo
2× AAA
battery contacts
3D-printed enclosure
```

Anything else requires an explicit product-level justification.

In particular, do not reintroduce:

- external audio amplifier;
- large speaker;
- LiPo charging system;
- GPS;
- always-on BLE;
- always-on Wi-Fi;
- unnecessary sensors.

---

# 29. Future upgrade path

The architecture should leave room for:

- better OLED;
- accelerometer;
- larger battery;
- integrated PCB;
- better speaker;
- rechargeable battery option;
- additional GPIO accessories.

But these belong to later revisions.

The V1 objective is to prove that:

> **Errata is fun with extremely simple hardware.**

---

# 30. Hardware acceptance checklist

Before declaring V1 hardware stable:

- [ ] ESP32-C3 boots from the intended battery configuration.
- [ ] 3.3 V rail remains stable under normal load.
- [ ] Wi-Fi scan does not cause resets.
- [ ] OLED remains stable during Wi-Fi activity.
- [ ] piezo is clearly audible inside the enclosure.
- [ ] piezo does not cause ESP32 resets.
- [ ] two buttons can wake the device from the chosen sleep mode.
- [ ] battery contacts do not disconnect during movement.
- [ ] battery compartment is polarity-safe and serviceable.
- [ ] OLED turns off during inactivity.
- [ ] Wi-Fi is OFF during normal idle/sleep.
- [ ] BLE is OFF except during active local interaction.
- [ ] deep/light sleep works reliably.
- [ ] state survives sleep and battery replacement.
- [ ] low-battery warning works.
- [ ] no obvious parasitic load remains enabled.

---

# 31. Firmware optimization acceptance checklist

- [ ] No busy-wait loops in idle paths.
- [ ] No periodic Wi-Fi scan.
- [ ] No periodic BLE scan outside active interaction.
- [ ] OLED refresh stops when not needed.
- [ ] Piezo output is inactive between effects.
- [ ] Sensor polling is duty-cycled.
- [ ] Time-based state is calculated from timestamps.
- [ ] Save writes happen only at logical checkpoints.
- [ ] Debug output is disabled/throttled in release firmware.
- [ ] GPIO sleep states are explicit.
- [ ] Sleep wake-up sources are tested on the selected C3 board.
- [ ] Battery voltage is monitored if a suitable ADC arrangement is added.
- [ ] Wi-Fi/BLE shutdown is verified after every session.
- [ ] No radio reconnect loops occur in the background.

---

# 32. Final hardware decision

The Errata V1 hardware is intentionally:

```text
              ┌───────────────────┐
              │     OLED 0.96"    │
              │      128×64       │
              └───────────────────┘

        ┌─────────────────────────────┐
        │        ESP32-C3              │
        │                              │
        │   A button       B button    │
        │                              │
        │       passive piezo          │
        └─────────────────────────────┘

              ┌─────────────────┐
              │     AAA  AAA    │
              │      ~3 V       │
              └─────────────────┘
```

The design goal is not maximum hardware capability.

It is:

> **maximum game experience per component.**

If a future measurement shows the selected SuperMini cannot reliably operate from the 2×AAA configuration across the useful battery-discharge range, add the **smallest, lowest-loss power-conversion solution necessary**. Do not add it in advance.

---

# 33. Practical development priority

The next hardware milestone is not buying more components.

It is building one test unit and measuring:

1. battery voltage under load;
2. 3.3 V rail stability;
3. ESP32 current in ACTIVE;
4. OLED current;
5. Wi-Fi scan current;
6. piezo current;
7. sleep current;
8. total average consumption;
9. real AAA runtime.

Once those measurements exist, battery life becomes an engineering number instead of a guess.

---

# Final principle

Errata should behave like a tiny toy that is **mostly asleep, wakes instantly when touched, briefly activates the expensive peripherals when needed, and returns to sleep as soon as possible.**

The biggest battery-saving features are therefore not exotic components.

They are:

**OLED OFF when unattended.**

**Wi-Fi OFF when not scanning.**

**BLE OFF when not interacting.**

**Piezo OFF when silent.**

**Sensors duty-cycled.**

**Time computed from timestamps.**

**Deep sleep whenever the player is not actively using the device.**

That architecture is the primary battery optimization strategy for Errata V1.
