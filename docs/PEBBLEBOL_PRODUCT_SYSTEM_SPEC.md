# PEBBLEBOL — Product, System & Software Design Specification

**Document status:** Master implementation specification  
**Target:** ESP32-based handheld virtual pet / exploration / battle game  
**Primary implementation agent:** Claude Code, Opus/Sonnet-class coding model, maximum reasoning / autonomous implementation mode  
**Language:** English for code/comments and this specification; UI text may be Spanish or configurable  
**Goal:** Build Pebblebol end-to-end from the existing project, replacing obsolete browser-game functionality and implementing the complete product loop.

---

# 0. Executive mandate

Pebblebol is a physical virtual-pet game built around an ESP32.

A Pebble is a small digital creature — a "computer bug" — trapped in wireless networks. The player cares for one active Pebble, carries the device around, discovers events through nearby Wi-Fi networks, captures and evolves Pebbles, collects them in a box, and interacts with other Pebblebol devices through battle, trading, and breeding.

The product should feel like a **real standalone toy**, not like an ESP32 development board with a demo application.

The central fantasy is:

> **"There are bugs hiding in the networks around me. I take mine with me, discover them, collect them, and make it stronger."**

The system should prioritize:
- immediate interaction;
- two-button gameplay;
- short sessions;
- strong pixel-art identity;
- low battery consumption;
- deterministic and testable game logic;
- offline-first operation;
- local device-to-device interaction;
- safe, bounded user-generated content;
- simple but meaningful RPG mechanics.

Do not reproduce Pokémon mechanically. Pebblebol should have its own terminology, progression, creature design language, and gameplay identity.

---

# 1. Product pillars

## 1.1 Care

The player maintains one selected Pebble.

Core virtual-pet systems:
- hunger;
- health;
- happiness;
- optional cleanliness;
- sleep/rest;
- simple play interaction.

Only the selected Pebble requires active care.

Pebbles stored in the Box do not require manual care. Their passive state improves slowly while stored, up to healthy limits, over approximately 24 hours.

The system must never require constant attention. Care mechanics are deliberately relaxed compared with a classic Tamagotchi.

---

## 1.2 Explore

The player carries the Pebblebol.

Nearby Wi-Fi networks become the "web" in which Pebbles live.

The device may scan for networks and use the scan results as an encounter seed. **It must not need to connect to those networks.**

A discovered network can produce:
- a wild Pebble;
- a rare Pebble;
- an item;
- a special event;
- nothing.

Different Pebble species have different spawn rates.

A network/event has a generous cooldown, generally **2+ hours**, to encourage movement instead of repeated farming in one location.

The exact cooldown system must be configurable.

---

## 1.3 Collect

The player can:
- encounter Pebbles;
- capture them;
- evolve them;
- breed compatible Pebbles;
- trade them;
- store up to 10 Pebbles in the Box.

The selected Pebble is the one that is actively carried and cared for.

---

## 1.4 Battle

Local device-to-device battle.

Each player brings up to **3 Pebbles**.

Battle is:
- 1v1 active Pebble;
- turn-based;
- four attacks per Pebble;
- three-type effectiveness triangle;
- support/status moves;
- switching allowed;
- switching costs a turn, Pokémon-style;
- simple enough to understand on a tiny screen.

Do not implement a complex elemental type matrix.

---

## 1.5 Create

The player can eventually create a custom Pebble from a mobile interface.

The creator allows:
- species/body appearance;
- type;
- attack selection;
- sprite drawing;
- name;
- cosmetic characteristics.

The creator **must not allow arbitrary stat creation**.

The system validates the generated Pebble against the same balance rules used by built-in Pebbles.

The device exposes the creator through a protected connection flow:
1. open Creator/Connection screen;
2. enter PIN;
3. show QR code for Wi-Fi connection;
4. show PIN;
5. mobile scans QR;
6. mobile opens the creator interface;
7. Wi-Fi is enabled only while this connection/editor flow is active;
8. inactivity timeout exits editor and disables Wi-Fi.

Existing browser-game functionality is considered obsolete unless it can be reused as infrastructure without preserving its old UX.

---

# 2. Product loop

The complete product loop is:

```text
CARE
  ↓
PLAY
  ↓
LEAVE HOME
  ↓
EXPLORE
  ↓
SCAN / DISCOVER NETWORK
  ↓
ENCOUNTER
  ├── Wild Pebble
  ├── Item
  ├── Event
  └── Nothing
  ↓
CAPTURE / USE / IGNORE
  ↓
GAIN XP / GROW
  ↓
EVOLVE
  ↓
MEET OTHER PEBBLEBOLS
  ├── BATTLE
  ├── TRADE
  └── BREED
  ↓
RETURN HOME
  ↓
CARE / BOX MANAGEMENT
  ↓
REPEAT
```

Every feature should strengthen this loop.

Avoid features that exist only because they are technically possible.

---

# 3. Scope priorities

## P0 — Required foundation

Implement first:

- persistent game state;
- Pebble data model;
- Box with 10 slots;
- selected Pebble;
- care system;
- time handling;
- 2-button input framework;
- menu/state-machine framework;
- rendering framework;
- minigame framework;
- 5+ short minigames;
- XP/levels;
- evolution;
- three-type battle system;
- four attacks;
- local battle protocol;
- robust save/load;
- battery/deep-sleep infrastructure.

## P1 — Product-defining systems

Then implement:

- Wi-Fi scanning;
- network encounter generation;
- network cooldowns;
- wild Pebble encounters;
- item encounters;
- capture;
- growth/activity tracking;
- 20+ Pebble families;
- 60+ total evolution-stage creatures.

## P2 — Social

Then:

- device discovery;
- battle;
- trading;
- breeding;
- transfer validation;
- anti-corruption / protocol validation.

## P3 — Creator

Then:

- PIN-protected local web UI;
- QR connection;
- Wi-Fi lifecycle;
- mobile sprite editor;
- custom Pebble generator;
- serverless/offline validation;
- upload to device.

## P4 — Polish

Finally:

- animation;
- sound;
- balancing;
- onboarding;
- accessibility;
- save migration;
- error recovery;
- factory reset;
- diagnostics;
- performance optimization.

---

# 4. Hardware abstraction

The application must not directly depend on a specific ESP32 board wherever avoidable.

Create a hardware abstraction layer.

Recommended interfaces:

```text
Hardware
├── Display
├── Buttons
├── Storage
├── Clock
├── WiFi
├── Bluetooth / BLE
├── MotionSensor
├── Buzzer / Audio
└── Power
```

Each interface must have:
- initialization;
- capability detection where appropriate;
- clean shutdown;
- error handling;
- test/mock implementation where practical.

The exact ESP32 variant and connected peripherals must be detected from the existing project/hardware before implementation.

Do not invent pins.

Do not overwrite an existing working hardware mapping without inspecting the project first.

---

# 5. Software architecture

Use a layered architecture.

```text
APPLICATION
    ↓
GAME SYSTEMS
    ↓
DOMAIN MODEL
    ↓
SERVICES
    ↓
HARDWARE ABSTRACTION
    ↓
ESP32 / PERIPHERALS
```

Suggested structure:

```text
src/
  app/
    App
    StateMachine
    InputRouter
    GameLoop

  game/
    Pebble
    PebbleSpecies
    PebbleDatabase
    Evolution
    Battle
    BattleAI
    Breeding
    Capture
    Inventory
    Box
    Care
    XP
    Items
    Encounters
    Exploration
    Cooldowns

  ui/
    Renderer
    Screens
    Menus
    Dialog
    SpriteRenderer
    BattleRenderer
    PetRenderer

  minigames/
    Minigame
    MinigameManager
    games/

  networking/
    WifiScanner
    LocalProtocol
    DeviceDiscovery
    TradeProtocol
    BattleProtocol
    BreedingProtocol
    CreatorServer

  persistence/
    SaveManager
    SaveSchema
    Migration

  hardware/
    Display
    Buttons
    Storage
    Clock
    WiFi
    BLE
    Motion
    Audio
    Power

  data/
    species
    attacks
    items
    evolution
    balance

  tests/
```

Adapt this to the existing repository instead of blindly creating a second architecture.

---

# 6. Application state machine

Pebblebol must be implemented as an explicit state machine.

High-level states:

```text
BOOT
  ↓
LOAD_SAVE
  ↓
HOME
  ├── CARE
  ├── PLAY
  ├── BOX
  ├── NETWORK
  ├── LINK
  ├── CREATOR
  └── SETTINGS
```

Additional transient states:

```text
ENCOUNTER
CAPTURE
BATTLE
TRADE
BREED
EVOLUTION
ITEM_REWARD
ERROR
SLEEP
```

Every state must define:
- enter();
- update();
- render();
- handleInput();
- exit().

Avoid global boolean spaghetti such as:

```text
isBattle
isMenu
isWifi
isEvolution
...
```

Use explicit states.

---

# 7. Input design

The game is designed around exactly two primary buttons.

Treat them as:

```text
A
B
```

Required semantics:

- A: primary action / next / confirm;
- B: back / cancel / secondary action.

Support:
- short press;
- long press;
- simultaneous press if hardware supports it.

Input handling must debounce hardware.

All game systems should be playable with the two-button constraint.

Do not create a system that requires a third input.

---

# 8. Main UI

The main screen should communicate:

```text
[PEBBLE SPRITE]

Name
Level
HP / health
Hunger
Happiness
XP
```

Avoid persistent information overload.

Main menu:

```text
PEBBLE
CARE
PLAY
BOX
NETWORK
LINK
SETTINGS
```

The selected Pebble is the active one.

---

# 9. Box

Maximum capacity:

```text
10 Pebbles
```

Box functions:

- inspect;
- select active Pebble;
- swap;
- release/discard if explicitly supported;
- evolve;
- view stats;
- view moves;
- view species;
- initiate breeding;
- initiate trade.

Stored Pebbles:
- do not require active feeding/cleaning;
- passively recover over time;
- must not exceed defined maximum values;
- must not lose important progression merely because they are stored.

Suggested model:

```text
activePebbleId
box[10]
```

A Pebble can only occupy one location.

No duplication.

---

# 10. Pebble domain model

Each Pebble should contain at minimum:

```text
id
speciesId
nickname
level
xp
type
hp
maxHp
attack
defense
speed
hunger
happiness
cleanliness
status
moves[4]
evolutionState
parentIds / lineage metadata if required
creationSeed
customSpriteData
traits
lifetimeStats
lastUpdated
```

Do not store redundant values if they can be deterministically derived.

Separate:

```text
SpeciesDefinition
```

from:

```text
PebbleInstance
```

Species defines:
- base stats;
- type;
- moves;
- evolution;
- appearance;
- spawn rate;
- compatibility;
- rarity.

Instance defines:
- current level;
- XP;
- current HP;
- individual variation;
- nickname;
- history.

---

# 11. Stats and balancing

Keep the numerical system intentionally small.

Recommended core stats:

```text
HP
ATK
DEF
SPD
```

Prefer small integer ranges.

Example target:

```text
base stat: 1–10
level: 1–30
```

The exact progression must be data-driven and balanceable.

Avoid giant Pokémon-style values.

The objective is readable strategy, not spreadsheet complexity.

---

# 12. Three-type system

Use exactly three primary types.

They should have an original Pebblebol terminology tied to computing/network concepts.

Example conceptual model:

```text
SIGNAL
  beats
CORRUPT

CORRUPT
  beats
SYSTEM

SYSTEM
  beats
SIGNAL
```

Do not copy Pokémon names or iconography.

Type advantage should be simple.

Suggested implementation:

```text
advantage: +1 damage
neutral: 0
disadvantage: -1 damage
```

Or another similarly small modifier.

Balance should ensure that type advantage matters without deciding every battle automatically.

---

# 13. Attacks

Each Pebble has exactly:

```text
4 equipped attacks
```

Attack categories:

### Damage
Direct damage.

### Defensive
Increase DEF or reduce incoming damage.

### Offensive buff
Increase ATK.

### Speed/status
Modify turn order or actions.

### Protection
Reduce or negate damage for a limited duration.

### Risk/reward
High damage with a drawback.

Each attack definition should contain:

```text
id
name
type
power
accuracy
priority
category
effect
effectValue
effectDuration
cooldown / limitation if needed
animationId
```

The combat engine must resolve attacks through data, not hard-coded species-specific logic.

---

# 14. Battle

Battle format:

```text
Player A: up to 3 Pebbles
Player B: up to 3 Pebbles
```

Each round:

```text
SELECT ACTION
  ├── Attack 1
  ├── Attack 2
  ├── Attack 3
  ├── Attack 4
  └── Switch
```

Action resolution:

1. validate action;
2. determine priority;
3. determine effective speed;
4. resolve first action;
5. resolve second action;
6. process fainting;
7. process status effects;
8. determine end of round;
9. check victory.

Switching costs the turn.

No item spam during battle unless explicitly designed as a future feature.

Battle must be deterministic enough to reproduce bugs from logs.

---

# 15. Battle protocol

Device-to-device communication must use an explicit protocol.

Never transmit raw unvalidated game objects and trust the peer.

Use:

```text
HELLO
CAPABILITIES
SESSION_REQUEST
SESSION_ACCEPT
TEAM_SUBMIT
TEAM_VALIDATION
BATTLE_STATE
ACTION
ACTION_RESULT
ROUND_RESULT
BATTLE_END
GOODBYE
```

Every packet must contain:
- protocol version;
- message type;
- session identifier;
- payload length;
- integrity check;
- sequence number where appropriate.

Reject:
- malformed packets;
- unsupported protocol versions;
- impossible stats;
- illegal moves;
- impossible levels;
- invalid species;
- invalid evolution state;
- oversized payloads.

The same validator used for custom Pebbles should be used for exchanged Pebbles.

---

# 16. Trading

When two Pebblebol devices connect:

```text
TRADE
```

The UI should show:

```text
YOUR PEBBLE
      ⇅
THEIR PEBBLE
```

Both players must explicitly confirm.

Use a two-phase confirmation:

```text
READY
  ↓
CONFIRM
  ↓
COMMIT
```

If communication fails before commit:
- both devices retain their original Pebble.

Trading must be atomic.

---

# 17. Breeding

Breeding is preserved from the existing concept.

Only compatible Pebbles may breed.

Compatibility should be data-driven.

Possible model:

```text
compatibilityGroup
```

or:

```text
breedRules[]
```

Breeding should produce a valid Pebble instance using controlled inheritance.

Potential inherited properties:
- species family;
- cosmetic traits;
- move pool;
- small stat variation;
- sprite characteristics;
- personality trait.

Do not allow breeding to generate unbounded stat escalation.

Generation mechanics must have a hard balance ceiling.

---

# 18. Evolution

Pebbles can evolve according to level and species-specific rules.

Evolution rules should be data-driven.

Example:

```text
EvolutionRule:
  sourceSpecies
  requiredLevel
  targetSpecies
  optionalConditions
```

Evolution should be a memorable event:
- animation;
- sound;
- visual transformation;
- confirmation.

The final evolution should generally look more dangerous/destructive than its previous stage.

Evolution is part of the fiction:

> The harmless network bug becomes increasingly destructive.

---

# 19. Creature design system

Target content:

**At least 60 total Pebble creatures.**

Recommended structure:

**~20 evolution families × 3 stages = ~60 creatures.**

Not every family must have three stages.

Possible distributions:

```text
10 families × 3 stages = 30
10 families × 2 stages = 20
10 standalone = 10
TOTAL = 60
```

Or another equivalent distribution.

Do not create 60 unrelated species if doing so damages quality.

Each family should have a strong visual/conceptual progression.

Example:

```text
PING
  ↓
FLOOD
  ↓
DDOS
```

```text
BUG
  ↓
EXPLOIT
  ↓
ROOTKIT
```

```text
BYTE
  ↓
CORRUPT
  ↓
OVERFLOW
```

The design language should remain:
- pixel-art friendly;
- recognizable at tiny resolution;
- simple silhouette;
- limited palette;
- expressive;
- increasingly destructive across evolution.

---

# 20. Spawn system

Wild encounter generation must be deterministic from a seed where practical.

Inputs may include:

```text
network identity
network category
signal strength
time bucket
day
device random seed
player progress
cooldown state
```

Do not transmit or store unnecessary network information.

Do not require connection to networks.

The system should classify discovered networks into abstract categories rather than exposing private network details to gameplay.

Example:

```text
UNKNOWN
HOME
PUBLIC
BUSINESS
OPEN
HIDDEN
```

Network identity must be privacy-conscious.

Prefer a local hash/derived identifier over persistent raw SSIDs where possible.

---

# 21. Encounter cooldowns

Cooldown target:

**approximately 2 hours or greater per relevant network/event.**

Cooldowns must:
- survive reboot;
- use timestamps;
- avoid resetting when battery dies;
- prevent repeated farming;
- remain generous enough to encourage movement.

A cooldown should be associated with a stable local identifier for the network/event.

Do not make the player wait actively on-screen.

---

# 22. Encounter outcomes

A scan can yield:

```text
WILD_PEBBLE
ITEM
SPECIAL_EVENT
NOTHING
```

Suggested encounter weighting:

```text
common Pebbles: frequent
uncommon: moderate
rare: low
special: very low
items: meaningful but not dominant
nothing: possible
```

Do not make exploration feel like a slot machine.

The player should generally feel that carrying the device around is worthwhile.

---

# 23. Capture

Capture is an interaction, not an automatic pickup.

Possible flow:

```text
WILD PEBBLE FOUND!

[CAPTURE]
[LEAVE]
```

If capture mechanics are implemented:
- capture items have limited effects;
- rare creatures may be harder;
- failure should not feel excessively punishing;
- the encounter must never delete the active Pebble.

If the Box is full:
- player must decide whether to release/replace;
- never silently discard.

---

# 24. Items

Initial item classes:

```text
XP / candy item
Capture item
Care item
Battle modifier
```

Items are discovered through networks.

Do not add an enormous inventory system initially.

Items should reinforce exploration and progression.

---

# 25. Growth through carrying

Pebbles grow when carried.

Do not claim precise GPS distance if the hardware cannot measure it.

Use an abstract **activity score**.

Possible sensors:
- accelerometer;
- IMU;
- movement detection;
- available ESP32 motion/peripheral data;
- elapsed time.

The system should calculate something like:

```text
activity += movementScore
```

rather than:

```text
distance = 3.72 km
```

unless accurate location hardware is actually present.

Activity can influence:
- XP;
- happiness;
- growth;
- rare event chance;
- trait development.

Movement must never require continuous Wi-Fi.

---

# 26. Time model

The device needs a reliable internal concept of time.

When connecting to a mobile device, use the mobile's current time to calibrate the device clock if possible.

If not possible:
- ask for time during setup.

Persist:

```text
lastKnownTimestamp
timeCalibrationState
```

All long-term systems must calculate elapsed time safely.

Never assume the device stayed powered on.

Use timestamp deltas for:
- care decay/recovery;
- box recovery;
- cooldowns;
- sleep;
- growth;
- daily events.

Protect against:
- clock rollback;
- absurd timestamp jumps;
- integer overflow;
- uninitialized time.

---

# 27. Care system

Active Pebble only.

Suggested values:

```text
hunger: 0–100
happiness: 0–100
health: 0–100
cleanliness: 0–100
```

Care should decay slowly.

The player should not need to interact every few minutes.

The game should be fun even if the player ignores it for several hours.

When stored:
- stats recover slowly;
- no manual care required;
- recovery caps at healthy maximums.

Avoid death.

Pebbles should be inconveniently unhappy at worst, not permanently destroyed.

---

# 28. Minigame framework

The minigames are inspired by the design philosophy of **Pureya**: extremely short games, simple controls, immediate understanding.

Target:
- 5–15 seconds per game;
- two buttons;
- almost no tutorial text;
- immediate feedback;
- rapid transitions.

Create a common interface:

```text
Minigame
  start()
  update()
  render()
  handleInput()
  finish()
  score()
```

The manager selects games from a pool.

The player should be able to play a sequence without returning to the menu between every game.

---

# 29. Initial minigame set

Implement at least 5 initially.

### 29.1 Ping
Press A when a moving indicator enters the target zone.

### 29.2 Packet Flood
Press A/B to redirect packets into the correct channel.

### 29.3 Firewall
Move a shield left/right with the two buttons.

### 29.4 Buffer
Keep a cursor inside a moving safe zone.

### 29.5 Delete
Select and remove appearing corrupted blocks.

Additional candidates:

### Sequence
Remember and reproduce a short A/B sequence.

### Overflow
Prevent a meter from reaching the limit.

### Packet Race
Rapid alternating A/B input.

### Signal Lock
Stop a scanning bar in the correct region.

### Malware Sweep
Choose which side to block as threats approach.

All games must be deterministic/testable when supplied a fixed RNG seed.

---

# 30. Randomness

Use a centralized RNG service.

Do not scatter calls to platform-specific random functions throughout gameplay.

Support:

```text
RNG(seed)
```

for deterministic tests.

Randomness must be controlled for:
- encounters;
- battle effects;
- breeding;
- minigames;
- loot.

---

# 31. Persistence

The game must survive:
- reboot;
- battery loss;
- firmware restart;
- normal sleep/wake.

Use versioned save data.

Example:

```text
SAVE_VERSION = 1
```

Save schema must support migration.

Never write partially updated critical state.

Recommended strategy:
1. construct new state;
2. validate;
3. serialize;
4. checksum;
5. commit atomically.

At minimum store:
- active Pebble;
- Box;
- inventory;
- cooldowns;
- timestamps;
- progression;
- settings;
- creator PIN/security state;
- protocol version;
- statistics.

---

# 32. Data-driven content

Species, attacks, items, evolution and balance should live in data tables/files rather than source-code conditionals.

Example:

```json
{
  "id": "bug_001",
  "name": "Ping",
  "type": "SIGNAL",
  "baseStats": {
    "hp": 5,
    "atk": 3,
    "def": 4,
    "spd": 7
  },
  "moves": [
    "byte_bite",
    "packet_rush"
  ],
  "evolution": {
    "level": 10,
    "target": "bug_002"
  }
}
```

The exact serialization format should follow the existing project and ESP32 memory constraints.

---

# 33. Custom Pebble creator

The creator runs locally on the device and is accessed through a mobile browser.

Requirements:

- no cloud dependency;
- no account;
- no external backend;
- responsive mobile UI;
- works offline after connection;
- validates before upload.

Creator screens:

```text
CONNECT
  ↓
PEBBLE INFO
  ↓
TYPE
  ↓
BODY / COSMETICS
  ↓
SPRITE
  ↓
ATTACKS
  ↓
VALIDATE
  ↓
PREVIEW
  ↓
UPLOAD
```

---

# 34. Creator security flow

The creator must be protected by a PIN.

Without the correct PIN:
- user may see connection information;
- user cannot modify the device.

QR screen:

```text
SCAN ME

[QR CODE]

PIN: 1234

Scan with your phone
```

Do not clutter this screen with unrelated UI.

Wi-Fi should be active:
- while the connection screen is open;
- while the mobile editor is actively being used.

Wi-Fi should automatically shut down after an inactivity timeout.

After timeout:
- close creator connection;
- disable Wi-Fi;
- return to previous screen or home.

Provide a configurable but sensible inactivity grace period.

---

# 35. Creator validation

Never trust client-side validation.

The ESP32 must independently validate uploaded Pebbles.

Validation must check:

```text
species / custom definition validity
type validity
stat budget
move legality
move count = 4
sprite dimensions
sprite data size
palette limits
name length
allowed characters
evolution validity
payload size
protocol version
```

A custom Pebble must fit the same competitive balance envelope as built-in Pebbles.

---

# 36. Custom stat budget

Do not let users assign arbitrary stats.

Example model:

```text
TOTAL_STAT_POINTS = fixed budget

HP + ATK + DEF + SPD <= budget
```

But use weighted costs if necessary.

Powerful attacks should consume more budget.

Example:

```text
strong_damage_attack → high cost
protection → high cost
status effect → medium cost
weak attack → low cost
```

The final validator computes the actual budget.

The UI should show:

```text
POWER BUDGET
████████░░ 82%
```

The user creates within constraints rather than editing raw numbers.

---

# 37. Sprite editor

Sprite editor must be optimized for mobile.

Requirements:
- pixel grid;
- draw;
- erase;
- fill if practical;
- palette;
- undo;
- clear;
- preview;
- flip/rotate if useful;
- fixed dimensions.

Keep sprites small enough for ESP32 memory.

Do not allow arbitrary image uploads without conversion/validation.

The sprite pipeline should normalize:
- dimensions;
- palette;
- transparency;
- storage format.

---

# 38. Local web server

The creator server must:
- bind only when needed;
- stop when inactive;
- serve only required assets;
- avoid unnecessary libraries;
- limit request body size;
- validate all input;
- reject malformed requests;
- avoid dynamic code execution;
- never expose unrelated filesystem data.

Endpoints should be minimal.

Conceptually:

```text
GET /
GET /api/state
GET /api/schema
POST /api/validate
POST /api/pebble
POST /api/time
POST /api/ping
```

Do not expose debugging endpoints in production builds.

---

# 39. QR connection

The QR should encode the information required for local discovery/connection.

Prefer a compact local URL such as:

```text
http://<device-address>/
```

or the appropriate captive-portal/local-network mechanism.

Do not put secrets into the QR beyond what is necessary.

The PIN remains the user-facing authorization layer.

---

# 40. Wi-Fi lifecycle

Wi-Fi should normally be OFF.

Enable it only for:
- network scanning;
- creator connection;
- device interaction if Wi-Fi is chosen for that protocol.

After use:
- disconnect;
- stop radio;
- return to low-power state.

Never keep Wi-Fi running on the home screen just because it makes implementation easier.

---

# 41. Bluetooth / BLE

If the chosen ESP32 hardware has BLE and it provides a better low-power local interaction path, evaluate it for:
- Pebblebol discovery;
- battle;
- trade;
- breeding.

Do not assume Wi-Fi is the correct technology for everything.

Decision criterion:
- power;
- reliability;
- implementation complexity;
- peer-to-peer UX;
- compatibility with the hardware.

The application layer must remain transport-agnostic where practical.

---

# 42. Local multiplayer UX

When another Pebblebol is discovered:

```text
PEBBLEBOL FOUND!

[ BATTLE ]
[ TRADE ]
[ BREED ]
[ CANCEL ]
```

Connection should require minimal steps.

Both devices should clearly show:
- peer name/id;
- selected operation;
- confirmation state.

Never silently modify another device.

---

# 43. Identity

Each Pebblebol device needs a local identity.

Suggested:

```text
deviceId
deviceName
protocolVersion
```

Device ID should be generated securely/randomly once and persisted.

Do not expose personally identifying information.

---

# 44. Privacy

The game must not:
- connect to random Wi-Fi networks for gameplay;
- upload scan results to a server;
- require an account;
- store raw network information longer than necessary;
- expose other people's SSIDs to other devices.

Wi-Fi scanning is a gameplay sensor, not a data collection system.

Prefer ephemeral/local hashed network identifiers.

---

# 45. Power management

Power is a first-class feature.

Implement explicit power states:

```text
ACTIVE
DIM
IDLE
DEEP_SLEEP
```

Wi-Fi is disabled in low-power states.

Sensor polling should be duty-cycled.

Display should sleep after inactivity.

Game systems based on elapsed time must work correctly across deep sleep.

Avoid busy loops.

Use hardware timers / sleep mechanisms where appropriate.

---

# 46. Performance constraints

Target:
- smooth rendering at the display's native refresh rate;
- no visible input lag;
- no unnecessary heap churn;
- no memory leaks;
- no long blocking operations on the UI thread;
- predictable RAM usage.

Avoid allocating large temporary objects every frame.

Minigames and battle should remain responsive even while peripheral operations are initialized.

---

# 47. Error handling

The device must never get stuck permanently because:
- Wi-Fi failed;
- peer disconnected;
- save is corrupted;
- creator sent invalid data;
- battery died during a transaction;
- sensor is missing;
- clock is invalid.

Every subsystem must have:
- timeout;
- cancellation;
- fallback;
- recoverable error state.

Example:

```text
CONNECTION LOST

A: Retry
B: Exit
```

---

# 48. Save corruption recovery

Maintain at least one previous valid save/checkpoint where practical.

On load:

```text
read
↓
checksum
↓
schema validation
↓
migration
↓
runtime validation
```

If invalid:

```text
SAVE ERROR

A: Recover
B: Factory Reset
```

Never silently start a blank game and destroy the user's collection.

---

# 49. Diagnostics

Add a developer diagnostics mode.

It should be inaccessible during normal play unless intentionally enabled.

Diagnostics may show:

```text
Firmware
Build
Free RAM
Battery
Wi-Fi state
BLE state
Clock
Save version
Pebble count
Last scan
Last error
Protocol version
```

Also support:
- test encounter;
- test evolution;
- test battle;
- test minigame;
- test save/load;
- deterministic RNG seed.

This will dramatically reduce development time.

---

# 50. Testing strategy

The project must include automated tests for pure game logic.

Minimum test coverage:

## Domain
- XP;
- level-up;
- evolution;
- stat calculation;
- care decay;
- box rules;
- breeding compatibility;
- breeding balance.

## Battle
- type advantage;
- damage;
- speed order;
- switching;
- fainting;
- buffs;
- protection;
- victory;
- invalid moves.

## Capture
- probability;
- inventory;
- full box.

## Exploration
- cooldown;
- encounter selection;
- deterministic seeds.

## Persistence
- save;
- load;
- checksum;
- migration;
- corruption.

## Protocol
- malformed packet;
- unsupported version;
- invalid Pebble;
- disconnect;
- replay/out-of-order message.

---

# 51. Build/test workflow

The implementation agent must first inspect the repository.

Before changing code:

1. identify ESP32 variant;
2. identify display;
3. identify buttons;
4. identify storage;
5. identify sensors;
6. identify current firmware architecture;
7. identify build system;
8. identify existing working features;
9. identify obsolete browser-game code;
10. run the existing test/build process.

Do not replace working infrastructure without understanding it.

Then create a written implementation plan in the repository before performing large changes.

After every major subsystem:
- compile;
- run tests;
- fix errors;
- keep the repository buildable.

Do not leave the project in a half-migrated state.

---

# 52. Implementation order

Recommended order:

## Phase 1 — Repository archaeology

Inspect everything.

Output:
- architecture summary;
- hardware map;
- existing functionality;
- reusable code;
- obsolete code;
- dependency map;
- risks.

Do not code major features yet.

## Phase 2 — Core engine

Implement:
- game state;
- save/load;
- clock;
- input;
- state machine;
- Pebble model;
- Box.

## Phase 3 — Virtual pet

Implement:
- care;
- XP;
- level;
- minigame framework;
- 5 minigames;
- evolution.

## Phase 4 — Battle engine

Implement:
- types;
- attacks;
- battle state machine;
- 3-Pebble teams;
- switching;
- local battle protocol.

## Phase 5 — Exploration

Implement:
- Wi-Fi scanner;
- abstract network identity;
- cooldown;
- encounter generation;
- capture;
- items.

## Phase 6 — Activity

Implement:
- sensor abstraction;
- activity score;
- growth rewards;
- deep-sleep integration.

## Phase 7 — Social

Implement:
- peer discovery;
- trade;
- breeding;
- transaction safety.

## Phase 8 — Creator

Implement:
- PIN;
- Wi-Fi AP/server;
- QR;
- mobile UI;
- sprite editor;
- validator;
- upload.

## Phase 9 — Content

Implement:
- 60+ Pebbles;
- attacks;
- items;
- evolution families;
- encounter tables.

## Phase 10 — Polish

Implement:
- animation;
- sound;
- balance;
- UX;
- battery;
- diagnostics;
- release build.

---

# 53. Content production rules

Every built-in Pebble needs:

```text
id
display name
type
sprite
base stats
4 legal moves or move pool
evolution rule
spawn rarity
breeding compatibility
description/flavor
```

Evolution families should communicate increasing destructiveness.

Avoid generic fantasy creatures unless their computing/network concept is strong.

Prefer concepts such as:
- packets;
- bugs;
- memory;
- overflow;
- exploits;
- malware;
- bots;
- daemons;
- firewalls;
- corrupted files;
- dead links;
- spam;
- rootkits;
- worms;
- proxies;
- signals;
- encryption;
- crashes;
- processes.

The creatures should remain cute enough to care about, even when destructive.

---

# 54. Naming system

Terminology should feel coherent.

Prefer:

```text
Pebble
Pebblebol
Network
Bug
Capture
Box
Evolution
Protocol
Corruption
Signal
System
Battle
Trade
Breed
```

Avoid directly cloning Pokémon terminology where unnecessary.

Potential fictional terminology should be introduced only if it improves the world.

---

# 55. Corruption system

Optional P1/P2 feature, but strongly encouraged.

Pebbles are computer bugs and may occasionally become "corrupted".

Possible effects:
- temporary sprite glitch;
- altered behavior;
- temporary stat modification;
- special encounter;
- unique evolution condition.

Corruption must never permanently destroy a Pebble.

If implemented, make it a signature mechanic rather than a random annoyance.

---

# 56. Traits

Optional feature.

A Pebble instance may have a small number of traits derived from usage.

Examples:

```text
WANDERER
AGGRESSIVE
CURIOUS
LAZY
PLAYFUL
RESILIENT
```

Traits can be cosmetic or have small mechanical effects.

Do not make traits mandatory for V1 if they complicate balancing.

---

# 57. Player-friendly balance principles

Pebblebol should reward:
- exploration;
- experimentation;
- collection;
- social play;
- caring for a favorite Pebble.

It should not reward:
- sitting beside one Wi-Fi router for hours;
- constant button mashing;
- keeping the device awake all day;
- obsessive maintenance;
- exploiting save resets.

Anti-exploit mechanisms should preserve fun rather than punish legitimate play.

---

# 58. Security model

Threat model:

The player owns the hardware, so physical access is assumed.

The goal is not military-grade security.

The goal is:
- prevent accidental corruption;
- prevent trivial multiplayer cheating;
- prevent malformed network input from crashing the device;
- prevent unauthorized casual creator access;
- prevent data loss.

Do not waste resources implementing heavyweight security that the hardware cannot reasonably support.

---

# 59. Networking architecture

Abstract transport:

```text
Transport
├── BLE
└── WiFi
```

Application protocol:

```text
Discovery
Session
Validation
Battle
Trade
Breeding
TimeSync
```

The game logic must not know whether packets arrived through BLE or Wi-Fi.

---

# 60. Versioning

Every persisted and transmitted structure must be versioned.

Examples:

```text
SAVE_SCHEMA_VERSION
CONTENT_VERSION
PROTOCOL_VERSION
CREATOR_API_VERSION
```

When incompatible:
- reject gracefully;
- display a useful error;
- never corrupt existing data.

---

# 61. Firmware update considerations

The architecture should leave room for future OTA updates, but OTA is not a P0 feature.

Do not sacrifice core gameplay architecture for OTA.

If implemented later:
- require explicit user action;
- preserve save data;
- verify firmware integrity;
- support rollback where hardware permits.

---

# 62. UX philosophy

Pebblebol should be:
- fast;
- playful;
- tactile;
- readable;
- slightly mysterious;
- rewarding.

Avoid:
- long text;
- complex settings;
- unnecessary animations;
- nested menus;
- loading screens;
- configuration before play.

The device should communicate primarily through:
- sprite;
- icon;
- sound;
- tiny amounts of text.

---

# 63. Screen constraints

The UI must be designed for the actual display resolution discovered in Phase 1.

Do not assume a modern GUI.

Every screen must be tested at the actual physical resolution.

Text must:
- remain readable;
- not overflow;
- have a consistent font;
- support long names safely.

Sprites must have:
- consistent pixel scale;
- predictable bounding boxes;
- no accidental clipping.

---

# 64. Audio

Audio is optional but highly recommended.

Prefer short sound effects:
- boot;
- menu move;
- confirm;
- cancel;
- encounter;
- capture;
- level-up;
- evolution;
- battle hit;
- victory;
- error.

Avoid continuous music initially.

This saves battery and preserves the toy-like feel.

---

# 65. Accessibility / usability

Minimum:
- high contrast;
- no information conveyed solely through color;
- adjustable sound;
- predictable controls;
- clear confirmation for destructive actions;
- no requirement for fast reaction outside minigames.

---

# 66. Developer commands

Create a debug-only command layer.

Examples:

```text
give_item <id>
spawn <species>
set_level <n>
set_time <timestamp>
set_activity <value>
heal
fill_box
clear_box
start_battle
start_creator
scan_wifi
show_save
```

Do not ship dangerous debug commands in the normal release UI.

---

# 67. Definition of done

The implementation is considered complete only when:

### Core
- [ ] Device boots reliably.
- [ ] Game state persists.
- [ ] Two-button input is robust.
- [ ] Box supports 10 Pebbles.
- [ ] Active Pebble can be selected.

### Pet
- [ ] Care works.
- [ ] Stored Pebbles recover.
- [ ] Time-based calculations work across reboot.
- [ ] XP and leveling work.
- [ ] Evolution works.

### Games
- [ ] At least 5 minigames.
- [ ] All are playable with two buttons.
- [ ] Minigames transition cleanly.

### Battle
- [ ] 3-Pebble team.
- [ ] 1v1 active combat.
- [ ] Four attacks.
- [ ] Three-type effectiveness.
- [ ] Buffs/protection.
- [ ] Switching costs a turn.
- [ ] Local multiplayer works.
- [ ] Invalid peers cannot inject illegal data.

### Exploration
- [ ] Wi-Fi scanning works.
- [ ] Device does not connect to scanned networks.
- [ ] Cooldowns persist.
- [ ] Encounters work.
- [ ] Capture works.
- [ ] Items work.

### Social
- [ ] Trade is atomic.
- [ ] Breeding compatibility works.
- [ ] Generated Pebbles remain balanced.

### Creator
- [ ] PIN required.
- [ ] QR connection works.
- [ ] Wi-Fi activates only when necessary.
- [ ] Inactivity timeout works.
- [ ] Mobile editor works.
- [ ] Sprite editor works.
- [ ] Device-side validation works.

### Power
- [ ] Wi-Fi shuts down after use.
- [ ] Device sleeps correctly.
- [ ] Timed systems work after sleep.
- [ ] No obvious battery-draining loops.

### Quality
- [ ] Automated tests pass.
- [ ] Release build compiles cleanly.
- [ ] No known save corruption path.
- [ ] No obvious memory leak.
- [ ] Diagnostics available.

---

# 68. Critical implementation rules for the coding agent

1. **Inspect before rewriting.**
2. **Preserve working hardware code unless there is a concrete reason to replace it.**
3. **Do not invent hardware pins or peripherals.**
4. **Do not build cloud infrastructure. Pebblebol is offline-first.**
5. **Do not connect to arbitrary Wi-Fi networks for gameplay. Scan only.**
6. **Keep Wi-Fi OFF whenever it is not needed.**
7. **Do not create a third-button dependency.**
8. **Keep gameplay logic independent from rendering and hardware.**
9. **Keep content data-driven.**
10. **Use deterministic tests for game logic.**
11. **Validate all network and creator input on-device.**
12. **Never trust another Pebblebol to provide legal game state.**
13. **Make trades atomic.**
14. **Never silently destroy a Pebble.**
15. **Never make care mechanics require constant attention.**
16. **Avoid Pokémon-level complexity.**
17. **Do not let custom Pebbles become pay-to-win / stat-cheat machines.**
18. **Do not overengineer features that are not needed for the core loop.**
19. **Compile and test after every meaningful subsystem.**
20. **If an existing implementation is better than the proposed architecture, adapt the architecture rather than replacing working code for aesthetic reasons.**

---

# 69. First task for Claude Code

Before implementing features, perform a repository audit.

Produce:

```text
PEBBLEBOL_IMPLEMENTATION_AUDIT.md
```

containing:

1. repository structure;
2. detected ESP32 variant;
3. display hardware;
4. input hardware;
5. storage;
6. sensors;
7. Wi-Fi/BLE capabilities;
8. current application flow;
9. existing Pebble implementation;
10. existing games;
11. current persistence;
12. existing browser/web functionality;
13. dependencies;
14. build commands;
15. test commands;
16. reusable components;
17. obsolete components;
18. architecture risks;
19. memory/RAM risks;
20. recommended migration plan.

Then create:

```text
PEBBLEBOL_IMPLEMENTATION_PLAN.md
```

with a concrete task checklist.

Do not begin a massive rewrite before this audit.

---

# 70. Final product philosophy

Pebblebol succeeds if the player can forget about it for a while, pick it up, press two buttons, smile, and think:

> **"What did my little bug find this time?"**

The product should feel like a mysterious little object that lives in the player's pocket.

The technical architecture exists to support that feeling.

Do not optimize for feature count.

Optimize for:

**identity → interaction → discovery → collection → attachment → social play → replayability.**

---

# Appendix A — Suggested first content roster

Initial families can include concepts such as:

```text
PING → FLOOD → DDOS
BUG → EXPLOIT → ROOTKIT
BYTE → CORRUPT → OVERFLOW
PACKET → FRAGMENT → SWARM
BOT → BOTNET → HIVE
SPAM → FLOODMAIL → BLACKLIST
DAEMON → PROCESS → KERNEL
GLITCH → ERROR → CRASH
LINK → DEADLINK → VOID
CACHE → MEMORY → LEAK
PORT → OPENPORT → BACKDOOR
PROXY → GATEWAY → FIREWALL
WORM → PARASITE → PLAGUE
SCRIPT → MACRO → PAYLOAD
PIXEL → ARTIFACT → CORRUPTION
SIGNAL → NOISE → JAMMER
FILE → ARCHIVE → DATAHOARD
COOKIE → TRACKER → STALKER
HASH → COLLISION → ENTROPY
NULL → VOID → NULLPOINT
```

These are conceptual starting points, not final names.

Final designs must be visually distinct and suitable for tiny pixel-art sprites.

---

# Appendix B — Example battle

```text
PLAYER A
Pebble: FLOOD
HP: ██████
Type: CORRUPT

PLAYER B
Pebble: FIREWALL
HP: ███████
Type: SYSTEM
```

FLOOD has type advantage.

Available:

```text
A: PACKET FLOOD
B: GLITCH
A+B: FIREWALL
B: SWITCH
```

The player chooses `PACKET FLOOD`.

Battle engine:
1. validate move;
2. determine priority;
3. compare speed;
4. calculate base damage;
5. apply type modifier;
6. apply buffs/status;
7. subtract HP;
8. render;
9. check faint;
10. continue.

The player should understand what happened without seeing complex formulas.

---

# Appendix C — Example custom Pebble validation

Input:

```text
Name: MyBug
Type: SIGNAL
Sprite: 16×16
Moves:
  1. Byte Bite
  2. Packet Rush
  3. Firewall
  4. Ping
```

Validator:

```text
✓ valid name
✓ valid sprite
✓ valid palette
✓ valid type
✓ valid moves
✓ 4 moves
✓ stat budget valid
✓ attack budget valid
✓ payload size valid
```

Result:

```text
VALID PEBBLE

POWER
███████░░░ 72%

[UPLOAD]
```

If invalid:

```text
PEBBLE TOO STRONG

Attack budget exceeded.

Remove or replace one powerful move.
```

---

# Appendix D — Non-goals for V1

Do not implement unless the core game is already stable:

- cloud accounts;
- online matchmaking;
- global leaderboard;
- multiplayer servers;
- GPS maps;
- complex inventory;
- dozens of status effects;
- dozens of stats;
- procedural infinite creature generation;
- real-money economy;
- advertisements;
- social media integration;
- always-on Wi-Fi;
- always-on Bluetooth;
- exact distance tracking without suitable hardware.

---

# End state

The finished Pebblebol should be a coherent, self-contained physical game system:

```text
          ┌──────────────┐
          │    PEBBLE    │
          │  Virtual Pet │
          └──────┬───────┘
                 │
       ┌─────────┼─────────┐
       ↓         ↓         ↓
     CARE       PLAY     EXPLORE
       │         │         │
       └─────────┼─────────┘
                 ↓
             DISCOVER
                 ↓
             CAPTURE
                 ↓
              LEVEL
                 ↓
             EVOLVE
                 ↓
        ┌────────┴────────┐
        ↓                 ↓
      BOX             SOCIAL
                          │
                 ┌────────┼────────┐
                 ↓        ↓        ↓
               BATTLE   TRADE    BREED
```

**The core principle is simple:**

> A Pebble is not a menu item. It is a little digital creature with a life that happens while the player carries the device through the real world.
