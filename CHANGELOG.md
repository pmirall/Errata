# Changelog

All notable changes to Pebblebol are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
Versions are tagged at phase boundaries of `PEBBLEBOL_IMPLEMENTATION_PLAN.md`; the
tag for a phase is cut only when its gate (`tools/check.sh`) and its variant matrix
(`tools/build_matrix.sh`) are both green.

**This log restarts at `1.0.0-rc1` (P10-C5).** The ten `0.x` sections below it
are the build, one per phase, and they stay: they are where every decision in
this firmware was argued and they are the only record of the instruments that
could not fail until somebody broke them. **The count that used to sit here —
"thirty-six" — is gone**: no enumeration of it exists anywhere in the
repository, phase 9 closed at thirty-five, and phase 10 alone added at least
eight (the `gfx_xbm` opacity divergence, `pair_write()`'s converging upgrade,
`build_matrix.sh` never running in CI, the `rd_u8g2()` gate that could never
have run, the film call-site count, the `ErrKind` row count, the never-resident
name list, and the pure-layer allocator gate a test's banner claimed existed).
A precise number nobody can check is the same defect this project keeps finding,
one level up. The Spanish changelog that
covered the Nottamagochi codebase this replaced is archived unedited at
`docs/legacy/CHANGELOG.es.md`.

**Every size in this file names its variant.** `release` is
`GOD_MODE_ENABLED=0` and is the artefact that would be flashed; `baseline`
carries the developer console and ships to nobody. A number quoted without its
variant is not a number — `docs/budget.md` §8 records a phase exit that compared
one with the other.

## [Unreleased] — the creature you drew, 2026-09-07

**The owner made a Pebble in the creator, and the device drew somebody else's
body on it.** It appeared in the Box under its own name, so the record was
there; it walked onto HOME wearing species 1.

### Fixed — the 144 bytes the player drew were read by nothing

- **`csp_install()` set `sprite_id = 0` under a comment promising the renderer
  would learn to read `CustomSpeciesRec.sprite` "at P8-C4/P9-C3".** It never
  did. A grep for a reader of that field across the whole tree found none: the
  pixels were parsed, validated, CRC-covered, written to flash, migrated,
  served back to the phone — and drawn by nothing. Two phases of pipeline
  ending in a `0`.
- **`csp_sprite(species_id, frame)`** is the reader. `game/species_custom.cpp`
  keeps the frames beside the projected stats (1,440 B of globals: 10 slots ×
  2 frames × 72 B), because a pointer into the caller's `CustomSpeciesRec` is a
  pointer into a stack frame that is gone by the time anything draws. It is
  keyed on the OCCUPIED MASK and not on the bytes — an all-blank drawing is a
  legal drawing, and answering `nullptr` for one would hand that player species
  1's body instead of the empty creature they actually made.
- **`pet_body_ref()` in `ui/pet_art.h`** is the one lookup every body path now
  goes through. The geometry needed no conversion: `CS_SPRITE_W/H` are 24×24
  and `CS_SPRITE_FRAMES` is 2, byte for byte every body in
  `data/sprites_pebbles.h`.
- **Three poses are deliberately NOT overridden.** An EGG is an egg — nobody's
  drawing shows through a shell. A SICK body stays the shared one, because that
  silhouette is HOW a player reads "sick" and a custom body there would hide a
  state they need. SLEEP is derived rather than looked up, so it already wears
  the drawing without a branch.
- **Four draw sites, not one.** HOME's still body, `ui/petfx.cpp`'s animated
  cache, HOME's derived sleeper, and the battle field. The BOX card draws no
  body at all, which is why the creature was visible there the whole time and
  is why the report read the way it did. The encounter screen is deliberately
  untouched: a custom species has `spawn_weight = 0` and never appears wild.

### Fixed — two drawn Pebbles shared one cached face

- **Both body caches were keyed on the ATLAS set id**, and every creator species
  folds onto the same one — they have no row of their own. `ui/screen_home.cpp`'s
  derived sleeper and `ui/petfx.cpp`'s mirrored-body cache would each have handed
  the second custom Pebble the first one's silhouette. Both now carry the source
  frame's pointer as a fourth key. Found while writing the fix, not after it.
- **`petfx_draw_body()` left `s_qry_custom` stale on an EGG.** The assignment was
  inside the else-arm, so an egg kept whichever creator body the last Pebble
  drawn had brought — straight into the cache key and its first pass. It is
  written before the branch now.
- **`petfx_pose_ink_x()` scans the ATLAS**, which for a creator species holds a
  row that creature never wore, so it answered where the food bowl goes from a
  silhouette nobody has seen. A drawn body takes the live-ink-box fallback that
  was already there for the egg case.

### Coverage — the registry was linked by no screen binary

- **`species_custom.o` was in no screen link line, and the link error is the
  finding.** `test_screens`, `test_battle_screen`, `test_link_screen` and
  `test_pet_view` all now link the real registry; before this they could not
  have drawn a custom body if the code had existed. That is exactly how 144
  bytes sat unread for two phases with the suite green.
- **Eight cases, and each one was mutation-proven.** Disabling the lookup in
  `pet_body_ref()` kills four by name; dropping the sleeper's fourth key kills
  one; deriving the sleeper from the atlas kills one; ignoring the frame index
  in `csp_sprite()` kills one; dropping `out.body` in `fill_art()` and ignoring
  the brought body in `br_draw_body()` kill the two battle cases; and nulling
  `PetView.custom_bits` kills the one in `tests/test_pet_view.cpp` — which is
  the only host case that can reach the pointer `ui/petfx.cpp` draws the DEVICE's
  animated body out of, since that file is compiled by no host binary at all.
- **The field case asserts a DIFFERENCE, not a rectangle.** Its first draft
  compared the player's body box against the record byte for byte and failed on
  39 pixels — and on 12 of the ATLAS foe's, which is what proved the fault was
  the field's composition (the foe's name plate is drawn over the top of the
  player's body) and not the change under test. Carving that band out by hand
  would have baked an observed failure into a constant, so the field is rendered
  twice with two different drawings and the pixels that MOVE are the assertion.
  Under the old renderer nothing moves and it fails on its first check.

### Gated

- **`tools/check.sh` gains seven greps** over the five links in the chain, and
  they are mutation-proven too. Six of them duplicate something a test already
  holds. The seventh does not and cannot: `ui/petfx.cpp` includes `render.h`, so
  no host binary compiles it, and its two lines are the ones that decide what the
  DEVICE draws on HOME.

---

## [Unreleased] — playable in three minutes, 2026-09-07

Three changes the owner asked for after the first hardware session: two about
usability, one a feature the exploration loop was missing.

### Changed — a minigame has no cooldown any more
- **The brief, verbatim:** *"I want to use this when I'm in the bathroom for
  three minutes, and one minigame doesn't last three minutes."* `MG_COOLDOWN_S`
  was 120 s, so a visit to the device bought you exactly ONE twenty-second game
  and then a countdown. It is **deleted** — the constant, `sim_minigame_cooldown_s()`
  and all three of its checks.
- **`data/balance.h`'s decay curve is the whole anti-farm now**, and it takes
  the REWARD down instead of the button away. Geometric at about 0.62 per run,
  and it stops at a floor of 150‰ instead of falling to nothing, because a
  payout of zero is a lockout wearing a different hat — which is exactly what
  the old six-step curve did at its sixth step. At the floor a run still pays
  15 % of the happiness and one whole XP; two `static_assert`s hold both, and
  setting the last step back to 0 **does not compile**.
- The window is **1 h**, down from 3, so it is the same hour as
  `XP_WIN_MINIGAME_S`: the two independent brakes on the same activity now
  refill on one clock instead of arguing.
- **The XP is awarded on what the run was actually worth**, not on the raw
  score. `sim_apply_play_result()` reports the decayed value through a new
  out-param, because it is also the call that pushes the run into the rolling
  window — a caller that re-read the curve itself would get a different answer
  before and after, and the experience would drift one step out of step with the
  happiness the same run paid.
- **Nothing in the repository had ever driven any of this.** No test mentioned
  the cooldown or the curve, which is how a 120 s lockout survived ten phases.
  Three cases now do: a run is never refused for following another one; twelve
  in a row pay monotonically less, fall fast, and never reach zero; and an hour
  away puts the curve back at the top. All three confirmed failing by name
  against a re-introduced cooldown.

### Added — you can FIGHT a wild Pebble
- **The gap the owner named:** *"it lets you catch it or leave, you can't even
  fight it for XP."* Catching and walking away were a wild encounter's only two
  answers, so a creature you could not afford to keep was worth nothing at all —
  in a loop whose entire purpose is finding creatures.
- **`BT_ENTRY_WILD`**, a fourth battle entry. **One Pebble a side: your ACTIVE
  one against the one on the panel**, and no pick list — you did not choose a
  team to bump into a stranger with, you were carrying what you were carrying,
  and asking the player to assemble three creatures while one stands in front of
  them would be a menu in the middle of a moment.
- **The foe is not rolled.** `build_foe()`'s dice are skipped: the species and
  level are the ones `game/encounters.cpp` already rolled and already showed the
  player one screen ago. That is the entry's only promise, it is invisible in a
  rendered frame (one 24×24 body looks like another), and it is asserted over
  three different creatures on the setup itself — confirmed failing by name when
  the wild branch is deleted.
- **A win pays `XP_BATTLE_WIN` out of the same `XP_SRC_BATTLE` bucket as a
  practice win**, so this is not a new farm: `XP_CAP_BATTLE` is two wins an hour
  for the device, and a wild fight can only be reached through an encounter,
  which arms the network's own two-hour cooldown on the way in.
- **The fight REPLACES the encounter rather than stacking on it.** A wild Pebble
  you have just beaten is not still standing there waiting to be caught, so
  leaving the battle walks back to the scan and not onto a card offering
  CAPTURAR to something that fainted.
- The card carries three answers now — CAPTURAR / LUCHAR / DEJAR — in a row of
  three boxes at `GF_TINY`, because "CAPTURAR" is 40 px at `GF_BODY` and a third
  of the panel is 42. Each label is centred by measurement rather than by a
  hand-counted offset.

### Changed — the wild Pebble stays on screen while you decide
- **The complaint:** *"it appears for a millisecond, it goes, and it lets you
  catch it or leave."* Exactly right, and it was this session's own doing: the
  reveal drew a 24×24 body, the film ended, and what was left was three lines of
  prose asking the player to decide about something they could no longer see.
- The resting frame is a **card** now: the body on the left where the film left
  it standing, the species name and level beside it, the two options underneath.
  The `¡PEBBLE SALVAJE!` line is gone — the title bar says ENCOUNTER and the
  picture says the rest, and the row it occupied is what makes room for the
  body. Five `static_assert`s pin the layout against the header bar, the
  countdown bar and each other.
- **The films are about 70 % longer** (item 980→1660 ms, capture 1080→1840,
  wild 980→1700). They were written against host goldens, where a film is a
  still picture you study a frame at a time; on a 0.96" panel at arm's length a
  300 ms tear is four frames, and four frames of anything is a glitch in the
  literal sense rather than the intended one. A `static_assert` caps every film
  at two seconds so none of them can quietly grow into a wait.
- **The timetable moved into `ui/screen_encounter.h`** and the tests drive it by
  name. They held it as literal milliseconds before, which is two copies of a
  schedule — and the retiming is exactly the edit that makes those two copies
  disagree.

---

## [Unreleased] — the first board, 2026-09-07

**The firmware ran on hardware for the first time.** The intro played. The
Wi-Fi scan found 22 networks, read 16, rolled an encounter — and the encounter
never appeared.

### Fixed — the auto-return measured two different readings of one clock
- **`app/state_machine.cpp`: `sm_service()` compared `s_input_ms` against its
  `now_ms` PARAMETER.** That parameter is the frame stamp `ui.cpp` samples at
  the top of `ui_service()`, ninety lines earlier; `s_input_ms` is stamped by
  `sm_goto()` with `sm_now()`. When a screen navigates **from its own `update()`
  hook** — which `sm_service()` runs one line before the check — `s_input_ms`
  ends up milliseconds AHEAD of `now_ms`, the unsigned subtraction underflows to
  about four billion, that clears `UI_AUTORETURN_MS`, and **the screen that was
  just pushed is sent to HOME on the same tick**, back stack cleared, no message.
- `ui/screen_network.cpp` is the only screen in the tree that navigates from
  `update()` (a scan answers on a frame, not on a press) and it writes the
  cooldown to flash on the way, so the gap is milliseconds, not microseconds.
  **Every wild encounter on a real board vanished into HOME**, while the console
  reported `16 seen / 1 fresh, phase=4` — the roll had happened and
  `ui_push(SCR_ENCOUNTER)` had been called.
- **No test in `tests/test_statemachine.cpp` could have failed**: every one of
  them passes `host_ms()` as the parameter, which is the same reading `sm_now()`
  answers, so the two were equal by construction and the subtraction was always
  0. The project's own recurring defect, one level down, inside the navigation
  machine itself. Two new cases drive the readings apart; both were confirmed
  failing by name against the old line.

### Changed — B is BACK on the HOLD, and choosing moved to the TAP
- **The owner played it on a board and the grammar was backwards in a hand.**
  Until now B tapped cancelled and B held chose, which is what spec §7's wording
  says on paper; with the device in your hands the two presses are not
  symmetric. A tap is the cheap, frequent, low-consequence gesture and a hold is
  the deliberate one — so the cheap one should be what you do constantly
  (walking a list and picking a row) and the deliberate one should be what
  throws work away. The old grammar cost a 600 ms hold for every confirmation
  and left the screen on any accidental brush of B.
- **35 call sites in 14 files**, plus the router. What did NOT move, each for a
  reason written down in `app/input_router.h`: the R auto-repeat on `SCR_TIME`
  and both setup screens (there B is "+1 on the field", and a 46-entry ring
  needs the repeat); the minigame pause, which stays on `GST_HOLD_R` because
  that is the P3-C4a collision fix and not a preference; `SCR_HOME`, the root,
  where both presses have always been the caress; and the ERROR screen, whose
  two taps are both primary actions. The dev console needed no change at all —
  it has used tap-to-choose and hold-to-leave since it was written.
- `STR_AF_BACK_SEL` reads **"SEL/ATRÁS"** now, tap first.

### Fixed — the coverage that let a 35-site swap through on one failing test
- Swapping the whole grammar broke exactly **one** case in the suite. Every
  screen case in `tests/test_screens.cpp` drives its own `input()` hook and so
  cannot see the router at all, and the only router case named one screen.
- **`the_router_takes_b_held_and_never_b_tapped_on_any_screen`** sweeps the
  whole enum and asserts both halves: B held is consumed exactly where the flags
  say, and B tapped is consumed nowhere. A half-done swap leaves either two
  backs or none, and both pass a test that names one gesture. Confirmed failing
  across 12 screens against the pre-swap router.
- `tools/check.sh` §5b: the router must test `GST_HOLD_R` exactly once and name
  `GST_TAP_R` never.
- The `kExits` table named `GST_TAP_R` on rows whose driver never presses it —
  a column that was decorative and, after the swap, also wrong. Corrected.

### Added — the instrument that was missing
- **`DIAG,scr,<ms>,<n>=<NAME>`, one line per screen change**, in both
  `god_service()` bodies. `DIAG,perf` already carried a screen ordinal, but once
  a minute and only as a sample: a screen that lives for one frame never appears
  in it, which is exactly the case that needed watching. The gap between two
  rows is the diagnosis — a push followed 20 s later by HOME is the auto-return
  doing its job on a screen nobody noticed; a push followed *immediately* by
  HOME is a navigation defect. Those two need opposite fixes and look identical
  from the sofa. An hour of this evening went on telling them apart by argument.

### Measured on hardware, for the first time in eleven phases
- Flash and boot: clean. The `serial exception` esptool prints after
  `Hard resetting` is the native-USB port re-enumerating, not a failure.
- The first-boot intro plays and hands over to the picker.
- Wi-Fi scan: 22 access points seen, 16 read (the `WIFI_SCAN_MAX_RESULTS` cap),
  radio released. Classification, hashing and the encounter roll all correct.
- **Not yet exercised**: sound (no piezo fitted), battery (no cell fitted), and
  everything that needs a second board.

---

## [Unreleased] — first impressions, 2026-09-07

*(Four `[Unreleased]` sections stand here on purpose: none has been tagged, and
they are four different pieces of work on the same day. This one is what the
owner asked for after playing the build; above it is the first hardware session;
below it is the pre-hardware review. Inventing version numbers to keep the
headings unique would be claiming tags that were never cut.)*

**The owner played the build.** Everything in this section comes from that:
seven observations about how the game FEELS, a list of the moments that deserved
a picture and did not have one, and a first boot that now opens on a cinematic
instead of on a text field.

### Added — the first-boot intro
- **Sixteen seconds of cinematic** (`ui/screen_setup.cpp`), as a PHASE of
  `SCR_SETUP_STARTER` rather than a screen of its own. Pseudo-C types itself
  onto the panel one character at a time, a compile bar fills, the build dies at
  92 %, the content band tears into scanlines and **three bugs climb out of the
  failure** — and stop exactly where the picker draws them, because both ask the
  same `pick_geometry()`. That is the point of putting it inside the screen: the
  brief was "the animation's last frame is that screen's first", and a separate
  ScreenId can only approximate it. `tests/test_screens.cpp`'s
  `the_last_frame_of_the_intro_is_the_first_frame_of_the_picker` asserts every
  pixel of it.
- **It plays on a true first run and nowhere else.** `app/app.cpp` arms it only
  when `boot == BOOT_FIRST_RUN`, so a flow resumed after a power cut goes
  straight to the question. That is also why it is NOT an `ObStep`: the four
  two-bit values are all spoken for, and buying a fifth would cost a save
  migration for a beat that must not be resumable in the first place.
- **Any press skips it, and only skips it.** A press that also chose a starter
  would make the impatient player's very first act on the device an accident —
  and a permanent one. Driven for all five gestures.
- Five cues, all reused effects: a typewriter tick every two characters, one
  beep when the build starts, `SFX_GLITCH` when it dies, and one `SFX_CHIRP` per
  bug. They are armed from the update hook and not the render, because a cue
  armed in a render fires again on every redraw.

### Added — the moments that had no picture
- **The wild reveal** (`ui/screen_encounter.cpp`). An item drop had a film and a
  successful capture had a film; FINDING THE CREATURE — the event the whole
  exploration loop exists to produce — opened straight onto two menu options.
  About a second: the band tears, the body assembles out of the tear feet-first,
  and the band snaps to inverse on the last beat, which is what covers the cut
  to a menu that shares no pixel with it. **It plays once per encounter, not
  once per entry**: `encounter_enter()` runs again on every walk back out of
  `SCR_CAPTURE`.
- **A verdict cue** (`ui/screen_battle.cpp`). A win plays `SFX_FANFARE` on the
  `RLE_BATTLE_END` beat. A loss adds nothing on purpose: the beat before it is
  the player's last Pebble going down, which already played `SFX_FALL`.
- **The cursor clicks** — menu, care, settings, box, encounter. One rule: a
  cursor that moves clicks, a cursor that cannot move does not. The BOX card is
  silent because it has one row; the battle ring is silent because it is stepped
  while a transcript is playing cues.
- **Two effects** in `hardware/audio.cpp`, each earning its flash in more than
  one place. `SFX_TICK` is a 12 ms click, above `SFX_BEEP` and a fifth of its
  length, because a keystroke repeated forty times must not sound like forty
  confirmations. `SFX_FANFARE` is four rising notes and a HELD fifth — the hold
  is the whole difference from `SFX_RISE`, which is a flat slide: a slide says
  "going up", a resolution says "arrived". The step table now `static_assert`s
  that the effects tile it exactly once each.
- `ae_noise()` in `ui/anim_ease.cpp` — the deterministic scramble both tears are
  drawn from. Not an RNG and never asked to be one: a film that drew from a real
  generator would record a different golden every time.

### Changed — the onboarding order
- **PICK, THEN NAME, THEN TIME.** Naming the device before the player has met
  the creature meant typing a name for nothing in particular, and then being
  shown three bugs one of which was suddenly called that.
- **And it cost no save migration**, which is the interesting half. `ob_next()`
  was `step + 1`, which welded the ASKING order to the PERSISTED values — two
  bits of `Config.flags`, all four spoken for, `OB_DONE` pinned at 0 so every
  older save decodes as "already set up". The order is a table now, the values
  did not move, and a `static_assert` holds that the table asks every real step
  exactly once and never asks `OB_DONE`.

### Changed — the seven things the owner played and found
Four were fixed at `77b2094`; three were deliberate and are documented rather
than changed. The four: **CORTAFUEGOS** draws over all five lanes uniformly and
its score subtracts the idle expectation (`FW_LUCK_FLOOR`, which is DERIVED —
ten packets over five lanes is exactly 2), so standing still scores zero by
arithmetic instead of by construction; **PAQUETES** widened its gap floor;
**battle** gained an `RLE_TYPE_EDGE` line so the once-per-battle type advantage
says out loud when it is spent; **the auto-return countdown** went from 5 s to
10 s and blinks full-width over the last 3.

### Fixed
- `tools/check.sh`'s exact `enc_film_phase()` call-site count went 3 → 5 with
  its prose updated to say why, rather than being loosened to an inequality.
- `README.md` §4's size table, `docs/PROJECT_CONTEXT.md`'s and `docs/budget.md`
  §16's were all quoting **1,350,840**, a number two commits stale by the time
  the final review closed. They are re-measured here.

### Sizes
`release` **1,354,812** flash / **59,468** globals against caps 1,600,000 /
65,000 — 245,188 B and 5,532 B free. `baseline` 1,371,966 / 59,564. MATRIX OK on
all six variants at 0 project warnings. The whole section above costs **+3,294 B
of flash and +16 B of globals** over the final review's 1,351,518 / 59,452.

### Not verified
Nothing in this section has been on a board. The intro in particular is the one
piece of this firmware a host golden cannot judge: `tests/fakes/gfx_fb.cpp`
draws no real glyphs, so the typed listing is a stack of bars in every golden of
it, and whether 4x6 pseudo-C is legible on a 0.96" panel is a question only the
panel answers. `docs/bench.md` **F0** is written for exactly that.

## [Unreleased] — the final review, 2026-09-07

**The commit before the soldering iron.** Not a phase: a review of the whole
tree against one measured fact, and the fixes it produced.

### The fact
`tests/fakes/*.cpp` defines 143 functions and **FIFTY of them are also defined
under `Pebblebol/src`**, so every host binary that links a fake drives the
FAKE's body while the reader believes it drives the firmware's. Two of the
defects this product has already shipped were exactly that — P10-C6's stale copy
of `ui_pet_name_latin1()` and P7's fixture nulling `save_manager`'s wear filter —
and forty-nine of the fifty had no gate. **The linker can never catch this
class**, measured rather than assumed: the six fake objects and the other host
objects have ZERO defined symbols in common, because all fifty shipping bodies
live in translation units no host binary compiles.

### Added
- **`tests/fakes/SHADOWS.txt`** — the fifty, enumerated and classified LINKED /
  IMITATION / INJECTOR / RECORDER, each row carrying either a running assertion
  that holds its imitation claim or a stated reason why it is not one.
- **`tools/check.sh` §10** with `tools/shadow_defs.awk` — re-derives the set from
  source and fails when it differs from the manifest, and enforces each class.
  Five failure modes demonstrated by making them fail, including **the P10-C6
  defect put back into `ui/ui.cpp`**, which the review measured passing the full
  gate at exit 0 and which now fails by name.
- `tools/check.sh` §11-13 — body gates for the read-only guard, the two power
  ladder holds, the wipe paths' use of the Box constructor, the three §67 "joins"
  that were proven at both ends and at no point in the middle, and four gates a
  ticked box named and that did not exist (`god_begin`/`god_service` in the
  artefact, `webui.cpp`'s `Content-Type` registration, `#pragma GCC diagnostic`,
  `strings_es.h`'s Latin-1 rule).
- Host coverage where there was none: `tests/test_minigames.cpp` links
  `minigames/registry.cpp` and pins the pairing that ships; `test_persistence`
  drives the closed-partition branch `kv_mem.h` documented and no test had ever
  called; `test_perf` holds the frame-hold rule extracted into `core/perf.cpp`;
  `test_statemachine` pins the screen set the power ladder may skip;
  `test_game_state` pins the boot rearm; `test_link_screen` reaches two refusals
  that were dead code in all 59 binaries; `test_screens` proves both text
  recorders are live, and three more binaries now read them.
- README §2: 24 inline warning blocks (symptom → the cause you will try first →
  the cause it is → the one reading that separates them), and three bench items
  for CARE, BOX and PLAY, which the 38-item list had no step for.

### Fixed
Seven bench-afternoon defects: the power ladder navigating away from every
`SF_STICKY` screen at 120 s behind a blanked panel (the first-boot naming ring,
the date screen, a battle, an incubating egg and the ERROR screen with its
blinking LED); the birth ceremony played at one phase per loop pass to a dark
panel; the LINK backstop tearing ESP-NOW down 91 s into a battle; a factory
reset leaving a pet in no Box slot that could never be saved; a store that never
opened diagnosed as a corrupt save with an offer to wipe it; a read-only session
writing through the trade seam; and the renderer free-running for the whole of
every film. Plus the PING cue letter, `pet_species_name()` on an installed
creator species, the minigame registry's order, the ceremony's name buffer, the
offline alert flood, the console's whole-ring batch and its overlong-line tail,
three `creator_server` defects, four `kv_mem` divergences, `boot_rearm`, and the
input ring's permanently-lost edge.

### Changed
- `docs/bench.md` C7 rewritten: it asked for three reboots and two of the three
  were unrunnable on this silicon and this artefact.
- **UNTICKED: §67 "Two-button input is robust."** Its first named artefact —
  P2-C6's 5 ms sampler and edge ring — is behind `#if defined(ARDUINO)` and is
  compiled by no host binary. `test_input` 15/15 is real and is about the FSM.

### Sizes
`release` 1,351,518 flash / 59,452 globals (caps 1,600,000 / 65,000);
`baseline` 1,368,672 / 59,548. Both at 0 project warnings.

---

## [1.0.0-rc1] — 2026-09-07

Phase 10, **six chunks**, and this entry now records all six rather than only
the last. **Not `1.0.0`, and the suffix is the honest part**: the gate is green,
the six-variant matrix is green, the release image fits its caps with 249 KB of
flash to spare, and **no line of this firmware has ever run on hardware**.
Seventeen §67 acceptance boxes are open — fifteen for want of a board, one for
want of decision D1, and two for want of a networking module that was planned
and never written. `PB_PINS_CONFIRMED` has never been defined. See `README.md`
§2, which carries all 38 bench items in dependency order, and `docs/bench.md`,
which has the keystrokes.

> **The four middle chunks of this phase had no entry here at all until the
> P10-C6 exit.** This section documented P10-C5 and cited "P10-C3's record" for
> a price that existed only in the plan. That is the wrong way round for a log
> that restarts at the release: phase 10 is the phase a reader most needs, and
> `README.md` §11 sends people here for the instrument failures. They are below
> now, one paragraph each.

### Added — P10-C1, the diagnostics the shipping build did not have

- **`dev/diag_core.{h,cpp}`, and the split is the point.** `dev/godmode.cpp`
  includes `Arduino.h` and is compiled by **zero** host binaries, so even its
  `GOD_MODE_ENABLED 0` stubs were untested by construction. Everything with a
  right answer — the command table, the parser, the range checks, the taint
  decision, §49's twelve field formatters and every command that touches only
  pure modules — moved into a pure module driven by `tests/test_diag.cpp`, the
  first host binary ever to execute any part of `dev/`.
- **The release artefact had no diagnostics at all**: `SCR_DIAG` unreachable,
  no serial reader running (`cmd_service` was below the `#if`), and the whole
  of it one heap line a minute plus six boot lines. A read-only surface ships
  now — `help`, `info`, `show_save`, `stall` — with `DCF_ALWAYS ⇒ NOT
  DCF_MUTATES` static_asserted and tested.
- **The taint is a table column applied by `diag_exec()`, not a call remembered
  per arm**, because `box_new_pebble()` memsets flags and `genome_genesis()`
  clears the taint bit: a naive `spawn` would mint a CLEAN Pebble inside god
  mode that `taint_gate_ok()` would let into an honest dynasty.

### Added — P10-C2, a performance instrument that measures no time

- **`core/perf.{h,cpp}`**, pure and host-tested, in every variant including
  `release`, plus a `DIAG,perf` serial line the shipping build prints. **None of
  spec §46's five thresholds is gated and that IS the deliverable**: no host
  binary compiles `render.cpp`, `ui.cpp`, `app.cpp` or `petfx.cpp`, `micros()`
  and I2C do not exist here, and the dominant frame term is a fixed ~24 ms
  `sendBuffer()` over an absent bus. A green BENCH OK would have been a verdict
  about a firmware nobody flashes. They went to `docs/bench.md` instead.
- **§47 as a driven table.** `kExits[]` names how every state and every ERROR
  kind ends and drives it. The sweep found a state with **no way out at all**:
  `SCR_ERROR` with `ERRK_SAVE_NEWER` — `SF_STICKY|SF_LOCK_INPUT`, so the router
  returns before the universal escape, and `err_input()` had no `LONG_BOTH`
  case. Insert a card from a newer firmware and the device parks there
  permanently under a strip advertising two dead actions.
- **What IS gated and has been seen to fire**: zero allocations per screen
  render, and a compositing ceiling that catches eight whole-panel XORs — an
  identity every pixel golden passes.

### Added — P10-C3, the animation pass, and the drawing seam was lying

- **`gfx_xbm()` is OPAQUE on the device** (`render.cpp` leaves the panel in
  `setBitmapMode(0)`) **and the host fake painted only the 1-bits.** The two
  backends had disagreed about every blit in the firmware since P2-C11 and
  nothing could see it. Correcting the fake moved **zero** of the 65 goldens,
  which is the receipt that it was unobserved by luck of composition. This
  chunk ends the luck — an item icon crosses its own label — so the seam is
  explicit: `gfx_xbm` opaque, `gfx_xbm_t` transparent, both backends, both
  gated, both asserted by name.
- **The sleeping pose is DERIVED, at 0 B of art.** `pf_build_sleep()` shuts a
  species' own eyes with the blink's own lid machinery, merges its top ink rows
  and splays its bottom four — against 5,760 B, forty drawings and a re-plan of
  both art caps for the alternative. It runs on **both** of HOME's body paths,
  because deriving on one alone is the phase-9 blink defect with the sign
  flipped, and a gate fails by name if either call disappears.
- Battle PROTECT, capture success and item pickup, in the pure
  `screen_encounter.cpp` rather than in `actfx.cpp`, which no host binary
  compiles; the motion maths lifted into a pure `ui/anim_ease.cpp` whose first
  host execution found a false documented invariant and a silent `uint32_t`
  overflow.

### Added — P10-C4, onboarding that survives a power cut

- **The first-boot step is a persisted field, not an inference.** Two bits of
  `Config.flags`, with **`OB_DONE` as ZERO** so every save written by any
  earlier firmware decodes as "already set up" — written the natural way round,
  every device on the planet would be handed a setup wizard on the next flash,
  which is indistinguishable from data loss to the person holding it.
- **And the answers had to reach flash in a way nothing in this tree had ever
  stated**: `load_all_inner()` answers `LOAD_FRESH` the moment the Box pair is
  missing and **returns before it reads the config**, so a config written on its
  own is a config discarded. Had the test been written the usual way — set the
  bits, read the bits back — it would have passed, and the first power cut on a
  real board would have thrown away the name the player had just typed.
- **`core/utf8.{h,cpp}`: one codepoint rule where there were three.**
  `render.cpp`, `gfx_fb.cpp` and `pebble.cpp` each read a lead byte and trusted
  it, none ever executed against a malformed sequence — and `p += 4` on a lone
  `0xF1` walks past the terminator, while `0xF1` is a **legal nickname byte**.
- **Spec §63 as a completeness claim**: `kAudit[]` has one row per `ScreenId`
  with a `static_assert` on its length, so a screen added without a worst-case
  fixture fails the **build** by name.

### Added — P10-C5, the release build

### Fixed — P10-C6, the exit

- **A device with an accented name emitted no beacon at all.** `DiscBeacon.name`
  is Latin-1 by wire contract — `discovery.cpp`'s `name_ok()` refuses
  `0x80..0x9F` — and P10-C4 correctly made `ui_pet_name()` emit UTF-8 for
  everything that draws. `fill_self()` went on handing it to `disc_encode()`,
  and **every** Latin-1 accent the first-boot naming ring can type encodes to
  `0xC3` plus a byte inside that refused window. `link_service()` drops an
  unencodable beacon by design, so `beacons_tx` stayed 0 and the LINK screen
  searched for ever; the peer never saw it, so the link could not be offered
  from either side either. **LINK, trade and P2P battle, dead, silently, for
  anyone who typed a Spanish name in a Spanish product.** Invisible to all 58
  binaries because `ui/ui.cpp` is compiled by none of them **and the fake stood
  in for that function with the pre-P10-C4 body** — every discovery test drove a
  function the firmware no longer had.
- **The creator portal died after 120 s.** `ui_radio_job_busy()` — the power
  ladder's `held` input — named the scan job and the link job and not the
  portal, the third radio owner. After two minutes with no *button* pressed the
  ladder reached `PWR_IDLE`, `pwr_hook_release()` navigated home and
  `creator_leave()` took the access point down — 180 s before decision D7's own
  timer, and using a phone is exactly what "no button pressed" looks like.
  Drawing a 24×24 sprite takes longer than two minutes, so the mobile editor,
  the sprite editor and bench D2 were all unusable.
- **The device called itself NOTTAMAGOCHI.** `STR_APP_NAME` was the boot splash,
  the load-save splash and the main menu's header bar, while SETTINGS →
  *Acerca de* two taps away printed `Pebblebol` and the AP was `PEBBLEBOL-XXXX`.
  The same power-on drew both names within seconds. Decision D3 is recorded
  CLOSED with "D3 has nothing open" — it closed every name the *machine* sees
  and never touched the one the human does.
- **Five accented Spanish strings were drawn in an ASCII-only font**, three of
  them on the first-boot flow. `u8g2`'s `drawUTF8()` emits no glyph **and no
  advance** for a codepoint the face lacks, so the character vanishes and the
  line closes up; the host fake painted a synthetic glyph for every codepoint at
  a fixed advance, so all of it was correct in every golden. The fix is at the
  seam: the fake records `fb_no_glyph()` per font and every snapshot asserts it.
- **Using an item said "Usado" for every item in the game**, discarding a whole
  `ItemEffect` one line after receiving it; and **corruption, a 24 h state, had
  no readout at all** — `cor_left_s()` had no caller in `src/`, and rendering
  STATUS_A with and without the bit differed by zero pixels.
- **The README's build recipe could not produce the artefact**, and an operator
  working around the error would have flashed the *baseline* image believing it
  was `release`.
- Instruments: the never-resident exemption is derived from the exit table
  rather than retyped; the `ErrKind` completeness claim counts the **enum**
  instead of the rows; the film call-site counts exclude the definition they
  were accidentally counting; `perf_note_pass()` must be called **exactly
  once**; the pure layers are gated against `malloc`/`free`/`new` (the gate
  `tests/test_soak.cpp`'s banner had claimed existed and did not); and
  `hardware/boot_reason.h` lifts the reset-reason table out of a device-only
  file so something can execute it.

### Measured

| Variant | Defines | Flash | Globals |
|---|---|---|---|
| **release** | `GOD_MODE_ENABLED=0` | **1,350,840** | **59,452** |
| baseline | — | 1,367,774 | 59,548 |
| no-web | `FEATURE_WEB=0` | 1,254,122 | 55,172 |
| no-god | `GOD_MODE_ENABLED=0` | 1,350,840 | 59,452 |
| sh1106 | `DISPLAY_IS_SH1106=1` | 1,367,774 | 59,548 |
| all-off | web + ESP-NOW + god mode off | 605,298 | 26,724 |

Release caps `GATE_RELEASE_FLASH_MAX` 1,600,000 and
`GATE_RELEASE_GLOBALS_MAX` 65,000, enforced by `tools/build_matrix.sh`:
**249,160 B of flash and 5,548 B of globals free.** Six variants, 0 project
warnings each, `--warnings all`. `docs/budget.md` §16 is the final account
against every cap, including the two sprite atlases.

`ALL PASS 58/58` host binaries (6,210,627 assertions), `ASAN OK 8/8`,
`PAGE TEST OK 51/51`, `GATE OK`, `MATRIX OK` — all five from a clean
`git archive` export of this commit.

The whole build, phase by phase, on the release variant:

| Phase exit | Flash | Globals | Source |
|---|---|---|---|
| 7 (social) | 1,269,468 | 56,820 | `docs/budget.md` §7 |
| 8 (creator) | 1,326,400 | 59,396 | `docs/budget.md` §13 |
| 9 (content) | 1,329,972 | 59,044 | `docs/budget.md` §14 |
| 10 (polish) | **1,350,840** | **59,452** | this commit |

Phase 10 cost **20,868 B of flash and 408 B of globals** for diagnostics in the
shipping artefact, an error-recovery sweep, a performance instrument, six
animation films, a first-boot flow, one codepoint rule where there were three,
the schema bump below, and the exit's fifteen fixes. `docs/budget.md` §15 is the
chunk-by-chunk account and §15.3 attributes the exit symbol by symbol.

### Added

- **`SAVE_SCHEMA_VERSION` 2 → 3, with a no-op migration and the machinery to
  survive it** (spec §31). The number moved because the migration path had to be
  *exercised* before the release rather than merely present — and the first
  thing the bump did was prove that "bump the number and add a row" was not all
  it takes. Three things had to be written first:
  - **`BlobOps::min_version`, the read side.** `pair_load()` and `single_load()`
    tested `stored == o.version` and nothing else, so a v2 blob under a v3
    firmware was a *bad* copy. Both copies of the Box bad is `LOAD_CORRUPT`,
    which is the SAVE ERROR screen, on every played device, on the first flash.
    The mutation is in the log: reverting one line turns the new test's whole
    field sweep into zeroes.
  - **`pair_write()`, the write side, which no test found first.** It asked
    `blob_ok()` — current version only — so on the upgrade boot both copies
    looked unusable: the `seq` restarted at 1 while the untouched copy kept its
    old, much higher one, and `pair_load()` takes the highest `seq`. The copy
    the migration had just written was the copy the next boot would *not* read.
    Not data loss, and it converges on the third boot, but "upgraded" has to
    mean upgraded. Nothing in the suite could see it until a test wrote **both**
    copies of every pair with real sequence numbers.
  - **The creator registry, which is outside `GameState` entirely.** `cs0..cs9`
    are not part of the state the chain transforms, so their upgrade lives in
    `custom_species_install_all()`. Without it `validate_custom_species()`
    refuses the record by name and every Pebble pointing at it comes back
    `VR_UNKNOWN_SPECIES`: the creature the owner designed, gone on the first
    boot after a firmware update.
- **The v2 → v3 step, and how much of it is load-bearing.** It changes no field
  and only re-seals. Emptied to `return MIGRATE_OK;` the suite fails on three of
  the five blobs (`inv`, `cds`, `trade`); the Box and the config come out right
  anyway, because the loader seals the header before it calls the chain and
  `save_config()` seals into the caller's struct rather than a copy. The comment
  in `migration.cpp` says which two are an accident, because the first draft
  claimed credit for all five.
- **The migration chain is checked by the compiler.** `migration.cpp` carries a
  `constexpr` walk from `SAVE_SCHEMA_VERSION_V1` to `SAVE_SCHEMA_VERSION` and a
  `static_assert` that it lands exactly. Bumping the version without adding a
  `MIGRATE_STEPS` row now **fails the build, by name**, instead of shipping and
  reading every existing save as `LOAD_CORRUPT`. A grep could not check it: the
  numbers live in two files and one of them is a table.
- **`SAVE_SCHEMA_INPLACE_MIN`** — the oldest schema readable in place. It is a
  claim about *layout*, not about kindness, and it has a floor: a blob stamped
  v1 under the v2/v3 keys is refused (`LOAD_CORRUPT`, nothing written) rather
  than reinterpreted through the wrong offsets. Asserted at compile time.
- **`README.md` at the repository root, in English**, written for someone
  holding the hardware who has never seen this repository: the pin map that
  fails its own guard *first*, then the seventeen open acceptance boxes, then the
  toolchain, the build, the gate, the architecture, the partition table and the
  save. There was no README at the root before this commit.
- **`tests/test_persistence.cpp` §8b** — nine cases driving a whole played save
  (two Pebbles, one of them a creator species, a changed config, a bag, a
  cooldown, a trade journal, **both copies of every pair**) stamped back down to
  v2 and read by this firmware: carried forward field by field, re-sealed, and
  an ordinary `LOAD_OK` on the second boot. Plus a v2 checkpoint restored after
  an `nvs` erase, through both of its entry points; the in-place floor; the
  foreign-newer end; the v1 → v2 → v3 chain running two hops for the first time;
  and a factory reset after a migration.
- **Five gates in `tools/check.sh`** for the bump, each mutated and each failing
  by name: the three flash readers judge a stored blob with `blob_class()` and
  never with `blob_ok()` (function-body scoped, because `blob_ok` appears eleven
  times in that file and ten of them are the exported helpers); the chain
  assertion still exists; the in-place floor is still asserted; the creator
  record is still re-sealed; and the test still pins the literal version number.

### Changed

- **`FW_VERSION` `0.2.0-dev` → `1.0.0-rc1`.** Exactly nine characters, like the
  string it replaces, which is not a coincidence:
  `networking/creator_server.cpp` budgets `GET /api/state` against
  `sizeof(FW_VERSION)` with a `static_assert`, so a longer string is a real
  change to a response buffer.
- **The Spanish documents moved to `docs/legacy/`** with an archival header in
  both languages naming what replaced them: `README.es.md`, `CHANGELOG.es.md`,
  `ESTUDIO_VISUAL.es.md`, `estudio_visual.es.html`. Unedited otherwise. The only
  part of the old README still material is its §2, the wiring — which documents
  the **other** pin map, and that conflict is exactly what decision D1 is.
- **`docs/decisions.md` is closed.** An index at the top, every one of the
  fourteen decisions given a terminal state in the table, the **six still open
  at ship** written out again at the end with an owner step each (D1 the pin
  map, D5 the panel variant, D8 the piezo pin, D10 the battery divider, D11 the
  boost module's idle draw, D12 the bulk capacitor), the two superseded sections
  marked where they stand, and every "what phase N did NOT measure" section
  pointed at `docs/bench.md`. 238 KB and twenty phase narratives kept: closing a
  log is not deleting it, and rewriting a phase record from a later phase's
  knowledge is how a log stops being a record.
- **`docs/bench.md` gains section G (the save on a board), items C5 and D6, and
  a correction.** The items in section F were labelled `H1`..`H6` — the section
  was drafted as H and renumbered to F when it landed, and the item prefixes did
  not move with it. Nothing outside that file cited them, which is why it went
  unnoticed; `README.md` cites F4 now, so it would not have again.
- **`.github/workflows/ci.yml` runs the six-variant matrix and the release size
  caps.** The finding is worth more than the fix: the caps every acceptance
  claim in this project is made against — 1,600,000 flash, 65,000 globals — live
  in `build_matrix.sh`, and **no CI job had ever run `build_matrix.sh`**. The
  firmware job runs `check.sh --no-tests`, which builds the *baseline* and
  checks it against `GATE_FLASH_MAX` 2,400,000 and `GATE_GLOBALS_MAX` 90,000 —
  ceilings so far above the artefact that they could not fail. The release caps
  had been enforced only on a developer's machine, by hand, for eight phases.

### THE HANDOVER — what a person picking this repository up needs to know

Not a phase-11 handover: there is no phase 11. This is what is true of the thing
on disk.

**PROVED, on the host, by something that fails by name if you break it.** The
game rules — care, XP, evolution, the battle engine and its type chart, capture,
encounters, items, breeding compatibility, corruption, the activity meter, the
cooldown table. The save: pair discipline, tri-state load, the `nvs2`
checkpoint, fault injection at every write a trade performs, and a v2 image read
by this firmware and re-sealed at v3. The wire: one validator with 27 named
reject codes running independently on both ends, driven under a sanitiser. The
screens: all 29 drawn at the worst content the product allows — a twelve-
character all-multi-byte name, level 30, a full Box — with zero out-of-bounds
draws, zero malformed UTF-8, zero allocations and now zero glyphs the font
cannot draw. 58 binaries, 6.2 M assertions, 159 named gates.

**BOUNDED — measured, with the bound written down, and not the same as proved.**
Trade atomicity *across the pair*: 800 trials over four fault arms with zero
splits, over a loopback, which is not a radio. Session security: an off-path
device that reads one frame can inject well-formed ones, and its power is
bounded to denial (both ends close, nothing written, `paid == 0`); closing that
needs a key exchange spec §15 does not contain. The compositing ceiling and the
per-frame allocation count: exact on the host, and the host's font is
fixed-advance while the device's is not.

**ASSUMED, and each one is a thing to check first.** That the committed pin map
is right — it is a *proposal* and it fails this repository's own
`static_assert`s. That `esp_light_sleep_start()` returns on a
`GPIO_INTR_LOW_LEVEL` from two buttons with internal pull-ups. That a real NVS
behaves like `tests/fakes/kv_mem.cpp` — real NVS has wear levelling, a page
allocator that can run out, and an `initArduino()` that erases things before
`setup()`. That `esp_reset_reason()` produces the values
`hardware/boot_reason.h` maps (asserted at compile time; never observed). That a
62 px QR on a 0.96" panel is readable by a phone camera.

**NEVER RUN ON HARDWARE — all of it.** No byte has reached a real flash chip, no
radio has been switched on, no pixel has been lit, and **no sound has ever been
produced**. `README.md` §2 is the 38-item list in dependency order.

**SIX `ui/` TRANSLATION UNITS AND `app/app.cpp` ARE COMPILED BY NO HOST BINARY**
— `ui.cpp`, `petfx.cpp`, `actfx.cpp`, `ceremony.cpp`, `render.cpp`,
`gfx_u8g2.cpp`, plus `net.cpp`, `creator_server.cpp`, `webui.cpp`,
`transport_espnow.cpp`, `kv_nvs.cpp`, `boot.cpp`, `registry.cpp` and
`godmode.cpp`. Sixteen files, about 3,200 lines of device-only networking and
boot code. Anything in them is untested by construction, and **both of this
exit's blocking defects lived there.**

**THE FIRST THING TO BUILD, AND IT IS THE SHAPE OF THIS EXIT'S OWN WORST BUG.**
Forty-nine shipping symbols are *shadowed* by host fakes — 11 `boot_*`, 18
`gfx_*`, 5 `kv_*`, 15 `ui_*` — and every one of the four shadowed source files
is in the list above, so **for all 49 the fake IS the only executed body.**
Nothing in this tree compares a fake against the function it stands in for.
That is exactly how the beacon bug survived: P10-C4 changed one function in
`ui.cpp`, the fake was not touched, all 58 binaries stayed green, and LINK died
for every Spanish name. Two of the 49 were closed at this exit (`ui_pet_name`,
and the boot classifier lifted out from under its fake entirely); **47 remain,
each one edit away from the same outcome.** The gate is a manifest: extract both
bodies with `awk`, strip comments with `cpp -fpreprocessed`, compare a
normalised hash, and fail by pair name — the same shape as the eight
function-body gates `tools/check.sh` already carries.

**AND THE PROJECT'S RECURRING DEFECT, IN THE SHAPE IT TOOK IN PHASE 10.** Every
instance across ten phases is now the same sentence: *the guard is keyed to an
enumeration narrower than the real state space.* One radio owner out of three.
`ScreenId` rather than `ScreenId × ErrKind`. `ScreenId` rather than
`ScreenId × render mode` (`kAudit[]` is still one row per screen, and eight
screens branch inside their render hook — an open gap). The shipping symbol
rather than the pair {symbol, fake}. When you add a guard, the question to ask
is not "can this fail?" but "what is it enumerating, and is that the whole set?"

### Not verified, and named so it is not assumed

- **Nothing has run on hardware.** Seventeen §67 acceptance boxes are open, and
  every one of them has a written owner step in `docs/bench.md`. **Two had none
  until P10-C5**, which is the sort of gap a bench list acquires by
  being written a phase at a time: *device-side validation works* — the box with
  the strongest host evidence in the repository (27 named reject codes, driven
  under AddressSanitizer on every commit) and no item of its own, because
  "validation works" is a sentence about a socket and `creator_server.cpp` is
  compiled by no host binary — and *no obvious memory leak*, the 24 h soak,
  named as owed by P2-C12 and again by P10-C2 and never given a procedure.
  Bench D6 and C5. **Three more items were added at the P10-C6 exit** and none
  of them is a §67 box the others cover: C6 (spec §64's sound — the engine has
  shipped since P6-C1 and nothing has ever been heard, and decision D8's owner
  step pointed at two sections that do not mention audio), C7 (the reset reason
  really becoming the right `BootKind`, which nothing had ever produced) and B8
  (a beacon goes out whatever the device is called).
- **The tag is not cut.** `v1.0.0` is the plan's acceptance and it belongs to
  whoever runs the bench list, because the tag is a claim about a device.
- **`POSE_SICK` is one body for sixty species**, and `POSE_EAT` has no art at
  all. Decided rather than left unmentioned; the price is in P10-C3's record
  above and in `PEBBLEBOL_IMPLEMENTATION_PLAN.md`.
- **Sixteen translation units are compiled by no host binary**, about 3,200
  lines of device-only networking, rendering and boot code. Anything in them is
  untested by construction, and both of this exit's blocking defects lived
  there. See the handover above.
- **`networking/breed_link.cpp` does not exist.** It was planned in P7-C5 and
  never written — earlier documents said "not built", which reads as "the module
  exists and nothing links it" and sends an owner looking for a file to wire in.
  Two §67 boxes wait on writing it.

## [0.9.0-content] — Unreleased

### Added — the phase-9 exit (P9-C6)

- **The pixel half of `petfx` is host-linkable for the first time: `ui/petfx_core.{h,cpp}`.**
  `pf_stride` / `pf_get` / `pf_set`, `pf_scan_ink` and `pf_build_lids` moved out of
  `ui/petfx.cpp` unchanged. They had sat since phase 4 behind a banner reading *"everything
  between these markers is host-testable: it depends on sprites.h and stdint only"* — true of
  the CODE and false of the BUILD, because `petfx.cpp` includes `render.h` → `Arduino.h`, so no
  host binary had ever compiled a line of it. This is the third module moved out of that file
  for exactly this reason (`xbm_mirror.cpp` P4-C4, `corrupt_fx.cpp` P9-C5) and `tools/check.sh`
  now holds the same red line over it.
- **`the_blink_closes_holes_and_never_draws_over_the_body`** — the shipped `pf_build_lids()`
  run over the shipped atlas for all 128 (set, frame) pairs, asserting that **every pixel a
  blink fills is an enclosed hole of that frame's own drawing**, computed here by a flood fill
  rather than read back from the generator. Plus `the_over_fill_recorder_would_see_a_blink_that
  _swallowed_the_body`, which hands the same instrument a band that MUST over-fill and requires
  it to say so — because a containment sweep that never fires is what a broken recorder looks
  like.
- **`sprite_dump blink [NAME]`** — frame 0 | THE BLINK | the band, side by side, with `+` for a
  pixel the blink adds and `-` for one it takes away. The composited blink frame is in neither
  authored frame of the atlas and nothing in this repository had ever drawn it.
- **`the_idle_animation_has_an_amplitude_and_a_spread`** — 4-45 % of ink, at least 6 px, over at
  least 2 rows and 2 columns. `diff > 0` was previously the only thing the tree said about the
  idle animation.
- **`tools/check.sh` builds `tests/tools/`** (`spritetool corrupttool balancetool pagetool`,
  ~4 s). Five review instruments were in no build at all, so any of them could stop compiling
  without a word — named by P9-C5 and declined there as a change to what every commit pays for,
  which is the phase exit's call to make.
- **`tools/check.sh` runs `gen_sprites.py --self-check`** over the whole tree. `--check` proves
  only that the header matches the `.txt` files; it passes a frame 1 that is a byte copy of
  frame 0, a body carrying another species' pixels, and a body lifted off the floor. The command
  five art agents were told to trust is now the command the gate runs — and `--self-check` with
  no arguments FAILS on identical frames, where it used to only warn.

### Fixed — the phase-9 exit (P9-C6)

- **THE BLINK WAS FILLING FOUR BODIES SOLID, AND THE FRAME IT HAPPENS ON WAS ONE NOTHING IN THE
  TREE HAD EVER DRAWN.** The eye band was "the bounding box of every enclosed hole in the top
  60 % of the ink box" and `pf_build_lids()` fills every interior run on every row of the band,
  so on a body whose upper half is made of gaps the band is most of the sprite: **DENYRA +79 px
  on a 283 px body, BLAKLIX +78 on 264, MURAX +68 on 242, and PANOPTIX losing all nine of the
  eyes it is named for**, for 90 ms every few seconds on the home screen. The rule is now stated
  in terms of the drawing — **the eyes are the biggest group of enclosed holes that can be
  closed together without closing anything that is not a hole** — and verified against the
  device's own fill before a band is emitted. Restoring the old rule fails the new case by name
  on twelve bodies.
- **Three species were silently emitted as non-blinkers while carrying a drawn face.** BIPPO
  ("one eye on the foot"), TIMAUT ("the hollow is the face") and KLONIX ("the mask's eye slots
  blink shut") all draw their face BELOW the old 60 % cut. The rule's docstring claimed "every
  one of the sixty bodies was drawn to that rule on purpose"; it was not. There is no cut now.
- **Five bodies redrawn or repaired after the art review.** **TIMAUT** was the only body in the
  roster whose ink did not grow at stage 2 (§18) — 108 px in four detached blobs, largest mass
  68 against its own stage 1's 98 — and is now 157 px in one 128 px hull with a 28 px socket.
  **ARTEFAX** had the worst silhouette cohesion in the roster (128 px in four equal pieces) and
  2x2 eyes in 4x4 blocks, the 1px-wall construction `pixio.txt` had already diagnosed and fixed
  five files away; it is 173 px in ONE piece with Pixio's own socket. **PAKETO**, the starter,
  was split into two masses of 69 and 63 by a full-height seam and read as two creatures; it and
  **RAFAGON** got `pixio.txt`'s 3px bridge. **FRAGMAR**'s ink fell below its own stage 0 and its
  "shatter" was a 3px dash that would read as dirt (123 → 139). **COOKIT** put a 7px eye on the
  family whose root is "an eye on a stalk", leaving no room for an unlit ring, so no frame of
  the family's stage 0 contained a readable eye (64 → 92 px). **Every family's ink now grows
  across all three stages**; three did not before.
- **Four idle animations were out of band.** KLONIX's entire idle was six pixels on ONE row of a
  187 px body; JITERA changed 70 of its 109 px in a shear that reads as tearing, which its own
  header offered a reviewer the fix for. KLONIX, MEMORO, MURAX and JITERA edited.
- **`tools/check.sh`'s `cor_service()` gate could be defeated by a block comment.** It stripped
  `//` comments only, so wrapping the one call site in `/* ... */` left the gate AND the suite
  green — the defect the gate exists to prevent, surviving inside it, for the second time. It
  strips comments with `cpp -fpreprocessed` now.
- **The eye-band assertion was a floor with 54 entries of slack** (`blinkers > 64` against 118):
  53 of 128 bands could be dropped one at a time with the suite green. Demonstrated by moving
  the derivation's cut from 60 % to 40 %, which took 26 bodies' blinks away and printed ALL PASS
  51/51 and GATE OK. It is an equality now.
- **A 1,536-check loop in `test_pet_view` asserted `f(x) == f(x)`** — both operands the same
  constexpr call, the loop variable discarded on the next line — under a comment promising a
  sweep over `minor_form`'s whole range. Deleted: P9-C3 made the property a build error.
- **`the_name_cap_never_splits_a_utf8_sequence` had no instrument for the CAP half of its own
  name.** A stray `out[cap] = 0` in `pebble_name_join()` passed the whole suite; the 0x7F fill
  that was already there "so a write can be detected" is now inspected past the cap.
- **`verify.py`'s five-key CORRUPTION mask** reported four different failures under one name and
  died with an unhandled `KeyError` on the fifth. Five named checks now.
- **The padding half of `every_generated_frame_has_ink_and_no_stray_padding_bits` was dead
  code** — every generated set is 24x24, so the mask is always 0. It walks `SPRITE_EMOTES`
  (5x7, 6x8, 12x10, 14x8) as well now, which is the shape it was written for.
- **Three documents carried the same wrong survivors decomposition** (`spr_mini8` 96 and emotes
  239, wrong by ∓72 B and cancelling under one aggregate assertion). Corrected to 384 + 168 +
  312 + 167 in `data/sprites.h`, `docs/budget.md` and `test_sprite_pipeline.cpp`, **and the test
  now asserts the four terms**. Also corrected: `PF_STRIP_BYTES` is 36 not 24 (640 → 288 B of
  .bss, phase 10 gets 352 not 400), the art budget quoted against the baseline cap rather than
  the release one (0.70 %, not 0.47 %), `tests/Makefile`'s claim that `sim_days` links `box.o`
  "for the Box recovery half of the care model" (it is linked dead and the simulated day is one
  ACTIVE Pebble), and the generated atlas banner's claim that `SPRITE_REV` breaks the build
  (P9-C3 deleted that `static_assert`; the hash is a DIAG readout and a cache key).

### Measured — the phase-9 exit (P9-C6)

- Release (`GOD_MODE_ENABLED=0`): flash **1,329,830 → 1,329,972 (+142)**, globals **59,044,
  unchanged**. The +142 is the eye-band table's new values, the redrawn art (identical byte
  count — every set is 144 B whatever is drawn in it) and `pf_scan_ink` / `pf_build_lids`
  becoming cross-TU calls; the globals line did not move because nothing left `.rodata` and
  `petfx_core.o` has no state.
- Baseline **1,342,202 → 1,342,344 (+142)**, globals 59,220 unchanged. Every variant moved by
  the same +142, so nothing is hiding behind `GOD_MODE_ENABLED`.
- **Phase 9 in total, release: 1,326,400 → 1,329,972 flash (+3,572) and 59,396 → 59,044 globals
  (−352).** The phase GAVE BACK 352 B of the scarcest budget in the project. Phase 10 inherits
  **270,028 B of flash and 5,956 B of globals**.
- `ALL PASS 51/51`, ASAN 4/4, PAGE TEST 51/51, GATE OK, MATRIX OK on six variants.

### Still owed after P9-C6

- **Nothing in phase 9 has been seen on a 128x64 OLED.** Every judgement about whether a body
  reads at 1x came from a rendered image on a monitor. Nobody has watched a frame pair animate
  at `UI_ANIM_FRAME_MS`, watched a blink land, or watched the corruption glitch on a panel.
- **The art the exit saw and did not fix**, in the order it would matter: family 11 (DAEMON)
  reads as architecture and collides with family 12 at 1x; BLAKLIX is the one body whose rim
  pixels toggle between frames, which is the pattern a slow passive mono LCD boils; ZIPBOM's
  nested corners read as furniture; ERROX's detached head can read as two objects; NULIX, BITTO
  and PORTU are the three babies with no protruding feature and cluster tightly; TWINIX is the
  only stage-2 less spiky than its stage 1; ESTAFEX is the weakest body in the roster.
- **The blink has no floor.** LEKRON's blink changes 2 px of a 303 px body and is invisible at
  1x; a body whose only small hole is 3 px gets a 3 px blink. Whether an animation READS is the
  class of question no assertion in this repository can settle.
- **The idle SHAPE bound is deliberately weak** (>= 2 rows, >= 2 columns). A 3-row rule would
  fail six bodies that move one 4-8 px feature between two rows, and would fight family 20's
  design, where the pupil is meant to be the only moving part.
- **`ui/petfx.cpp` is still compiled by no host binary**, and it is still 1,100 lines of
  automaton, choreography and draw path. What moved out is the pixel core; the blink's PAINTING
  loop, the corruption edge re-derive and every renderer call remain unexecuted by any test.
- Everything on the P9-C3/C4/C5 "still owed" lists below stands: the HOME layout's 19 blank
  rows, the v1 stage floor, the care rates, the five species outside the win-rate band, 3v3
  balance, and `CORRUPT_BATTLE_INFECT_PERMILLE` still being read by nothing.

### Added — the corruption effects (P9-C5)

- **`cor_service()`, and the timer that had no reader.** `cor_expire()` was called by nothing in
  `Pebblebol/src`: the encounter armed the 24 h deadline and the Antivirus cleared the bit, so
  `PBS_CORRUPTED` was permanent on every device while `verify.py`'s "corruption clears by timer"
  check passed over the JSON for four phases. `cor_service()` is a Box-wide walk in
  `game/corruption.cpp` (pure, host-tested) called once a second from `app/app.cpp`'s
  `logic_tick()`, and `tools/check.sh` gates the call site because `app.cpp` cannot be linked on
  the host.
- **`ui/corrupt_fx.{h,cpp}`** — the glitch's row geometry, the 1-in-8 gate and the behaviour-row
  choice, in a pure translation unit, because a bound that lives in `petfx.cpp` is a bound no
  test can check. `petfx_draw_body()` paints what it is handed with the existing
  `rd_dither_rect_phase()` at draw colour 2, inside the ink box it already publishes.
- **The battle modifier's missing half was `battle_init()`.** `battle_stat_eff()`'s +1 ATK /
  −1 DEF has read `corrupt_left` since P4-C2 and nothing ever set it from the stored Pebble, so
  a creature corrupted out of battle walked into one cured. One ASSIGNMENT into the same field
  attack 12 Infectar writes, so the two sources are one fact and cannot stack.
  `BATTLE_ENGINE_VER` 2 → 3; `tests/golden/battle_v1.txt` re-recorded, and split with an
  intermediate build to prove the whole diff is the version stamp and the hashes.
- **`tests/test_corruption.cpp`** — 19 cases, 231,174 checks, the 51st host binary. The glitch
  containment is measured on PIXELS painted into the host framebuffer over a control render,
  not on the struct fields, with `fb_oob()` watching the panel; a corrupted Pebble survives a
  REAL NVS round trip, a REAL wire round trip and a REAL breeding with its deadline intact.
- **`tests/tools/corrupt_view.cpp`** — the glitch drawn over a real body from the compiled atlas
  at the real floor line, plus a `strip` mode for the 1-in-8 rate over a minute and a `temper`
  mode for the behaviour row, because a still frame cannot show motion.

### Measured — the corruption effects (P9-C5)

- Release (`GOD_MODE_ENABLED=0`): flash **1,328,580 → 1,329,830 (+1,250)**, globals **59,044,
  unchanged** — no table left `.rodata` and the new `PF_TEMPER` row (16 B) is still
  `static const`.

### Added

- **The balance pass, with two instruments that print numbers a person reads (P9-C4).**
  `tests/tools/balance_matrix.cpp` fights **all 3,600 ordered pairs of the roster 1,000 times
  each** — 3.6 M battles in 16.2 s, both sides driven by the real `game/battle_ai.cpp` over the
  real `game/battle.cpp` — so nothing was sampled and the grid was never shrunk.
  `tests/tools/sim_days.cpp` runs **30 simulated days** of the real `game/sim.cpp`, the real
  `game/xp.cpp` ledger and the real `game/encounters.cpp` roll under four written-down player
  profiles. Neither is in `make check`: they answer a human, which is what `tests/tools/` means.
  `make -C tests balancetool` builds both.
- **`EVASION_PER_SPD` 2 → 3, the one tuning number this pass moved**, in `data/balance.h` and
  `tools/content/balance.json` together. The matrix found a **type** imbalance with a **speed**
  cause: SIGNAL carries 11 of the 12 speed archetypes and CORRUPT 13 of the 13 damage ones, so a
  rule that underpays SPD reads as "SIGNAL is weak". Same-stage cross-type win rates
  **42.9 / 56.8 / 50.3 % → 46.6 / 55.0 / 48.5 %**, species outside the 35-65 % band **9 → 5**,
  and the type edge itself 59.9 → 59.5 %, still inside the 54-65 % §12 asks for. 4 and 5 were
  measured and overshoot, so 3 is the measured answer rather than the largest available one.
- **The dynasty name is host-testable for the first time (P9-C4, §54).** The hash and the two
  syllable indices moved from `ui/ui.cpp` — which includes `<Arduino.h>`, so no host binary could
  ever link it — into `game/pebble.cpp` as `pebble_name_syllables()`. The syllable repertoire
  stays in `core/strings_es.h` and the caller does the lookup, so neither `snprintf` nor the
  Spanish string block enters the pure layer. `pebble_name_join()` replaces the `snprintf` and
  **fixes a real defect**: the old join truncated on a BYTE boundary and the repertoire is UTF-8,
  so `"Ña" + "rrón"` (6 glyphs, 8 bytes) could be cut mid-sequence; the new one truncates on a
  CHARACTER boundary and always yields a PREFIX of the whole name.

### Changed

- **THE XP CURVE DECISION IS CLOSED AFTER THREE DEFERRALS, AND THE LOSER IS DELETED.**
  `tools/content/balance.json`'s rival 31-entry curve (`25 + 12*(L-1) + 4*(L-1)^2`, total 36,453)
  is **gone from the file**, not merely unadopted; `data/balance.h`'s `10 + L*L` / 8,845 is the
  only XP curve in the tree. `tools/content/verify.py` now reads that curve out of `balance.h`
  and **fails by name if an `XP_TABLE` key reappears in the pack**, so the second source of truth
  cannot come back. THE EVIDENCE, days to level 30 at each profile's measured XP/day
  (shipped / pack): light **471 / 1,942**, normal **81 / 333**, heavy **25 / 103**, saturate
  **9 / 35**. Against the 21-28 day target the shipped curve lands inside the band for a heavy
  player; **the pack's reaches it under no profile at all**, including the physically unreachable
  ceiling. The criterion itself is the PLAN's gloss and not a §57 line — §57 contains no number,
  no week and no level 30 — and the exit says so.
- **`tests/golden/battle_v1.txt` re-recorded, and the diff is only the version stamp and the
  hashes.** All 23 rounds, every event and the RNG cursor are byte-identical. Split with an
  intermediate build: `CONTENT_VERSION` alone accounts for the whole diff, because
  `battle_hash_basis()` mixes it in; `EVASION_PER_SPD` moved not one byte of that transcript.
  **The pixel goldens did not move at all** — `XP_TABLE[1]` is still 11, which is the 23 px the
  HOME rule records.

### Measured

- Release (`GOD_MODE_ENABLED=0`): flash **1,328,090 → 1,328,580 (+490)**, globals **59,044,
  unchanged** — the balance pass moved no table, so nothing left `.rodata`. Split with an
  intermediate build: the balance constant, the regenerated `CONTENT_VERSION` and the
  JSON/documentation edits are **+4 B**; the nickname move is **+486**, of which **+358 is named
  `.text`** (`pebble_name_join` 236 B, `pebble_name_syllables` 74, `utf8_fit` 64, and
  `ui_name_for` shrinking 118 → 102) and the rest is alignment. `pebble_name_join` is the
  expensive half and it is the half that fixes the UTF-8 truncation.
- `ALL PASS 50/50`, ASAN 4/4, PAGE TEST 51/51, GATE OK, MATRIX OK on six variants;
  release caps 1,328,580/1,600,000 flash and 59,044/65,000 globals, i.e. 271,420 B of flash and
  5,956 B of globals in front of phase 10 **as of P9-C4** — P9-C5 and P9-C6 spend 1,392 of the
  flash between them and the exit's own Measured block above carries the closing figure.

### Still owed after P9-C4

- **The v1 stage clock gives away the first twenty levels.** `game/sim.cpp` advances the retired
  life stage by AGE alone and `stage_commit()` writes it back into `PebbleInstance.level`, so a
  Pebble earning **no XP at all** reaches level 5 at 2 h, 10 at 10 h, 15 at 24 h and **20 at
  3.5 days**. `sim.cpp`'s own comment says "P3-C2 makes level XP-driven and this map becomes
  read-only", and that sentence is false while `stage_commit()` still writes. 2,660 of the
  curve's 8,845 points are never earned by anybody. Measured, named, not fixed: undoing it is a
  save-visible change to what `level` means.
- **"3-4 touches a day is a well-kept Pebble" is optimistic and `balance.h` now says by how
  much.** Satiety falls 100.8 points a day against a 30-point meal, so 3.36 MEALS is break-even
  and a touch also has to cover cleaning and play. The NORMAL player lands 3.3 care actions a day
  and satiety reaches zero every night. §27 holds (the Pebble is never lost) and §57's own
  criterion holds with margin, so no rate was tuned — moving `ACT_MEAL_HUNGER` or
  `CARE_DECAY_MPH` re-records `tests/golden/care_v1.txt` and is its own commit.
- **Five species remain outside 35-65 % against their own stage** (ids 12, 23, 25, 26, 29, all
  high-ATK). Species 26 was swept and **no** one-point redistribution of its fixed 22 points helps
  — every variant tested made it stronger. The lever is its learnset, and pulling it trades
  species 26 for species 56 at no net gain, so it was measured and left.
- **The matrix measures the greedy-vs-greedy metagame only.** `battle_ai.cpp` scores every
  power-0 move at 0 and cannot see `DRAIN_PCT` at all, so a kit built on status or sustain reads
  as weak here for a reason that is the AI's and not the roster's.

- **Sixty creature bodies, drawn (P9-C3).** `tools/sprites/*.txt`, 24x24, two frames each, one
  per roster id, in twenty families of three. The atlas is **64 sets / 9,216 B**: the two eggs,
  a `SLEEP` and a `SICK` pose set, and the sixty bodies. Every one of the 120 frames was
  rendered from the COMPILED header and looked at — as text, as a 5x contact sheet per family
  and as a whole-roster sheet at 1x, 2x and 4x — because nothing automated can tell you whether
  a body looks like a creature and this repository says so in four separate files.
- **`PB_SPRITE_EYES`, the eyelid table, generated.** `ui/petfx.cpp` carried 24 rows of blink
  bands read off the decoded art BY HAND, 1,100 lines from the pixels they indexed, held in
  step by `static_assert(SPRITE_REV == 1, "re-verify it")`. Sixty bodies would have made it
  sixty rows of the same. `tools/gen_sprites.py` derives the band from the same `.txt` file as
  the pixels by one stated rule — an eye is a HOLE in the top 60 % of the ink box — so they
  cannot drift, and what the rule gets wrong is written down in `eye_band()` rather than
  discovered. `SPRITE_REV` is now the derived art hash, so it also cannot be forgotten.
- **The §63 sweep.** `tests/test_screens.cpp` renders **every species at every stage, every
  pose and both frames on HOME** (2,400 renders), every species in the BOX list, and every
  species on both sides of the BATTLE field including the mirror — all with `fb_oob() == 0`,
  the body's own last row inked so it stands on the floor rule, and the draw box inside the
  panel. Sixty-five goldens contain four species between them; fifty-six were drawn by no
  golden at all.
- **`home_sleeping.pbm` and `home_sick.pbm`.** The two new pose bodies were drawn by NOTHING:
  no fixture set `POSE_SLEEP` or `POSE_SICK`, so two brand-new 24x24 drawings would have
  shipped unrecorded. `a_pose_changes_the_body_and_nothing_else_on_home` additionally pins that
  a pose changes pixels only inside the body box — and that `POSE_EAT` changes **zero** pixels,
  which is P9-C3's answer to the pose question rather than an oversight.

### Changed

- **The roster ships the whole pack: `ROSTER_FAMILIES` 12 → 20, 36 → 60 species.** The clamp
  was never the tables, it was the art: the old guard `SPR_BABY_BLOB + sprite_id < 38` capped
  the roster at 36 because the legacy atlas held 36 addressable bodies. The same three lines
  now read `PB_SPRITE_BODY_FIRST + sprite_id < 64` and describe the resolution the firmware
  performs. **Every one of the 34 attacks is on a reachable learnset for the first time** (13
  Infección on species 46, 22 Firewall on 60), and the evolution key (item 9) has a lock:
  species 53 → 54, driven end to end in `test_inventory.cpp`.
- **`SPRITE_DATA_BYTES_MAX` 24,576 → 11,264 B.** That number was a TRANSITION allowance sized
  for the window in which both rosters were in the tree at once. Both rosters are no longer in
  the tree, so it is the measured end state (**10,247 B** — 9,216 of atlas plus 1,031 of icons,
  mini-icons, badges and emotes) plus **1,017 B**, which is seven more 24x24x2 sets: one more
  three-stage family and four effect sets. `tools/gen_sprites.py`'s own `PB_DATA_BYTES_MAX`
  came down from 12,288 to 10,240 and now actually refuses to emit over it — README section 5
  had claimed that refusal since P9-C1 and there was no such check.
- **`br_body_set_id()` no longer folds.** Sixty species resolve onto sixty distinct 24x24
  combat bodies, `worst == 1`. The old case asserted `n == SPRITE_BABY_BODIES` (8) with at most
  5 species sharing a body; raising 8 to 60 alone would have made it un-failable again, because
  `sprite_set_id()` clamps an out-of-range form to the first body — so the case asserts the
  EXACT slot per species and `worst == 1`, and a mutation that points species 60 past the end
  of the atlas fails it by both.

### Removed

- **The 38 legacy Nottamagochi sets, `enum SpriteSetId` and `SPRITE_SETS`.** `data/sprites.h`
  keeps the icons, mini-icons, badges, emotes, the pose enum, the budget assert over both
  atlases' worth of art, and the hand-written LOOKUP block, and it includes the generated
  atlas. `tests/tools/sprite_dump.cpp` lost its transcribed 38-name list and needs no edit when
  a body is added, which is what its P9-C1 comment predicted.
- **The care-quality body.** `SPR_CHILD_GOOD`/`POOR` and `SPR_TEEN_GOOD`/`POOR` carried the care
  quality `sim.cpp` froze into `minor_form` when the pet grew. One 24x24 body per species has
  nowhere to put a second variant — it would be sixty more drawings — so the distinction is
  **deleted**, not refactored. `sprite_form_of()` lost its `minor_form` parameter so every call
  site had to be visited rather than silently keeping a no-op; `test_pet_view.cpp` pins both
  directions. A screen that wants care quality back should draw it with the renderer.
  **What this bought:** a Pebble's body is its species at EVERY stage, so all forty evolution
  rules now move the drawn body at the level they actually fire at — including the twenty that
  fire at CHILD or TEEN, every family's first evolution, the starter's Paketo → Fragmar among
  them. That number was 12 of 24 seeing "only the name" and is now 0 of 40.
- **Seven pose sets, replaced by two.** `SLEEP` at four sizes and `SICK` and `EAT` at three
  become ONE generic `SLEEP` and ONE generic `SICK`, both 24x24 (288 B); `POSE_EAT` gets no art
  at all and falls through to the species body. The identity loss on sleep and sickness is
  **inherited, not introduced** — the old `sprite_set_id()` ignored `form` in every pose branch,
  so a sleeping Gato and a sleeping Pez were already the same blob. **The cost of dropping
  EAT's art, stated:** the authored "leaning over the bowl" silhouette is gone, and what
  replaces it (the `EMO_BOWL` prop `actfx` already parks and `petfx_squash()`) lives in
  device-only translation units no host binary compiles. It is the one place this chunk moves
  behaviour into the layer the suite cannot see, and it is the pose that was already invisible
  to it — `pet_pose_of()` never returns `POSE_EAT` and no golden has ever recorded one.

### Fixed

- **`--self-check` called two real defects OK and exited 0.** Both were found by the art agents
  writing against it. A file copied to another species' id — the worst failure this format has,
  because a body in the wrong slot draws the wrong creature and every other check stays green —
  printed `1 file(s) OK`, because the slot check was reachable only through the whole atlas. A
  frame 1 that was a byte-for-byte copy of frame 0 printed `WARNING: this body will not
  animate` and then `OK`. Both are hard failures now, and the command five art agents were told
  to run agrees with the test that had always caught them.
- **Three bodies were exactly symmetric about their own centre column** (`PORTU`, `PROXI`,
  `GATEON`), so `ui/xbm_mirror.cpp` produced a byte-identical picture and the two fighters on
  the battle field faced the same way. Nothing in the pipeline can see that — the mirror still
  runs, the ink count is the same, the bounding box is the same. Found by a test that renders
  both and requires them to differ; fixed in the art, which §18/19 wanted asymmetric anyway.
- **Five bodies redrawn and two edited after looking at all sixty together**, which is the only
  way any of it could have been found: `RAFAGON` (its family's stage 2 read as confetti and kept
  none of the family's vocabulary), `KADENAX` (seven plates whose links bridged whole rows fused
  into a circuit board), `PANIKA` (carried its identity in texture and flattened to television
  static at 1x), `PIXIO` (three blocks and a crumb read as an icon, not an animal), `BAKDORA`
  (nine 2px through-holes read as stripes, not holes); `KERNON`'s halo bar was floating clear of
  its spire and now has a stem, `ESTAFEX` lost 36 px of 1-3 pixel barb islands that read as dirt.
- **Stale atlas arithmetic in three documents.** `docs/budget.md` §4.3 and §12 and
  `Pebblebol/ESTUDIO_VISUAL.md` carried 10,893 B against a 14,336 B ceiling and a 9,959 B end
  state that double-counted the eggs and predicted no pose sets. All three now carry the
  measured numbers and the measured cost.

### Measured

- **Release variant (`GOD_MODE_ENABLED=0`): flash 1,326,400 → 1,328,090 (+1,690 B), globals
  59,396 → 59,044 (−352 B).** Split by cause, with an intermediate build: the **art swap alone**
  — sixty bodies in, thirty-eight legacy sets out, the eye table generated, `PF_MAX_W/H` 40 → 24
  — is **+6 B of flash**, because the atlas it replaces was almost exactly the same size. The
  remaining **+1,684 B** is the roster 36 → 60 (24 species rows, 16 evolution rules, encounter
  rows, 48 more Spanish strings). The globals line moved DOWN, and every byte is accounted for:
  `ui/petfx.cpp`'s two decode caches go from 2 x 200 B to 2 x 72 B and its four lid strips from
  4 x 60 B to 4 x 24 B — **exactly 352 B**, returned to phase 10 rather than spent.
- Matrix green on six variants; release caps 1,328,090/1,600,000 flash and 59,044/65,000
  globals. `ALL PASS 50/50`, ASAN 4/4, PAGE TEST 51/51, GATE OK.

### Still owed

- **The HOME layout.** A 24 px body standing on `HOME_FLOOR_Y` leaves **19 blank rows** above it
  on every still frame, where the 40 px adult filled the band. The re-recorded goldens show it.
  It is a layout decision for P10-C3/C4, not a defect a golden can report.
- **`XP_TABLE`.** `data/balance.h` ships one 31-entry curve and `tools/content/balance.json`
  ships a different one; the plan sequences the decision into **P9-C4** and its instrument
  (`tests/tools/sim_days.cpp`) does not exist yet. Two sources of truth, untouched here.
  **CLOSED BY P9-C4:** the shipped curve won on measured evidence and the pack's is deleted.
- **Nothing on this list was seen on hardware.** There is no board and no panel in this
  environment. Every judgement about whether a body reads at 1x is a judgement from a rendered
  image on a monitor, and the thin features are the ones at risk: `PLAGON`'s 1-2 px teeth,
  `BUGGO`'s 1 px pupils, `PANOPTIX`'s nine pupils, `TWINIX`'s four eyes, `BAKDORA`'s hanging
  door. `KLONIX`'s 6 px blink is the subtlest idle in the atlas and may be invisible in practice.

## [0.8.0-creator] — Unreleased

### Added

- **The creator PIN, persisted and gated (P8-C1).** `ConfigV2.creator_pin` is minted at the
  first CREATOR entry from `RNG_MISC` in the range **1..9999** — never the `0` that means "none
  issued" — persisted before it is shown, and the same number across reboots so a scanned QR and
  a written-down PIN stay valid. `pin_ok()` reads an `X-Pin` **header**, not a `?k=` query
  argument: the old form put the secret in the URL bar, the browser history and any `Referer`.
  Five consecutive failures arm a 60 s lockout, an expired lockout grants exactly one guess, and
  a malformed PIN and a wrong PIN are one answer.
- **`networking/creator_gate.{h,cpp}`** — the rules as a pure, caller-owned 16 B struct, so a
  host binary can drive a lockout, a reboot and a 300 s idle expiry in microseconds.
  `networking/webui.cpp` keeps transport only and decides nothing.
- **`POST /api/ping`**, the first of spec §38's seven routes: the PIN-gated keep-alive, and the
  only thing that extends the portal's life. Registered with the 4-arg `on()` overload so a
  hostile `Content-Length` goes down the core's fixed raw buffer instead of
  `readBytesWithTimeout()`'s `malloc` growth loop.
- **`net_request_portal()`** — the AP-only bring-up. `net_request(RADIO_WIFI)` answers "already
  on the WiFi track" for any non-OFF phase, so asking for the portal while a scan or peer link
  held the radio returned success and produced no access point.
- **The D7 inactivity shutdown (P8-C2).** `ConfigV2.creator_idle_s` (default 300 s), measured on
  the monotonic clock and reset **only by a request that passed the PIN gate** — otherwise
  anyone in radio range holds the access point up forever by fetching one URL every 299 s.
- **All seven spec §38 routes (P8-C3).** `GET /`, `GET /api/schema`, `GET /api/state`,
  `POST /api/validate`, `POST /api/pebble`, `POST /api/time` and `POST /api/ping`.
  `/api/schema` is served from the generated `data/creator_schema_json.h` with the 4-arg
  `send_P` — 744 B of `.rodata`, **zero globals** — so the page and the device cannot disagree
  about a budget; `/api/time` calls `gt_set_epoch(CAL_PHONE)`. `/api/schema` is the one ungated
  route (compiled constants, identical on every device); everything else needs the PIN, which
  may now ride in an `X-Pin` header **or** the JSON body, through the same one gate and the same
  one failure counter.
- **`networking/creator_body.{h,cpp}` — THE RAW-BODY CAP, and it closes audit §12's
  "Body limits: none".** Every POST route is registered with the 4-arg `on()` overload, so a
  body arrives in the core's fixed 1436 B chunks instead of `readBytesWithTimeout()` growing a
  `malloc` to the attacker's own `Content-Length`. `RAW_START` decides the whole body's fate
  from the declared length: over `CS_BODY_MAX` (2048) it is drained and answered **413**; over
  `CS_BODY_DRAIN_MAX` (8192), or NEGATIVE, the socket is closed from inside the hook, because a
  client declaring 100 MB would otherwise hold `loop()` — no render, no simulation — for as long
  as it kept trickling. A `Content-Length` of 0 is **411**, never an empty upload: chunked and
  absent look identical to the core and would hand the validator an empty Pebble.
- **`networking/creator_parse.{h,cpp}` — a fixed-schema reader, and no JSON library.** It reads
  exactly the two documents the page may send and answers everything else with a named code. It
  is **length-bounded, never NUL-bounded** (an embedded NUL is `CP_NUL`, not a terminator),
  duplicate keys are `CP_DUP_KEY` rather than last-wins, and **there is no recursion and no
  depth counter**: no value reader can read an object or an array that was not expected there,
  so ten thousand open braces is one error and a constant amount of stack.
- **`validate_custom_species()` in the ONE validator (spec §15).** Fourteen new named
  `VR_CS_*` codes over the §35/§36 rules — type, the stat band, the four move rules, the
  stage-1 power cap, the attack budget, the name and its character set, the reserve, and
  `budget_used`, which is **recomputed and refused on disagreement** because a page that prices
  its own Pebble prices it at zero. The same function runs on every `cs*` record read off flash
  at boot: "it survived a CRC" is not evidence about a stat total.
- **`game/species_custom.{h,cpp}` — creator species resolve through `species_get()`.**
  `data/species_table.h` has promised since P4-C1 that "P8 resolves cs* records here so no
  battle or validator code ever branches on custom"; this is that resolution, through a bound
  resolver in the generated header. A custom row is projected with `family = 0`,
  `evo_rule = SPECIES_EVO_NONE`, `spawn_weight = 0` and `compat_group = 0`, which answers §35's
  evolution and breeding inputs structurally instead of with a check.

- **The creator page and the sprite editor (P8-C4, spec §33 and §37).** `web/creator/` is
  committed source — one HTML shell, `app.js` and `sprite_editor.js`, vanilla JS, no framework,
  no CDN, offline after load — and `tools/gen_index_html.py` inlines it into
  `src/data/index_html.h` as ONE document served on ONE route. **42,245 B of `WEB_HTML_MAX`
  49,152 (6,907 B free, 85.9 % used); +41,658 B of flash and ZERO globals on `release`, and
  zero of either on `no-web` and `all-off`.** The nine §33 screens are CONNECT, NOMBRE, TIPO,
  CUERPO, SPRITE, ATAQUES, VALIDAR, VISTA PREVIA and ENVIAR.
- **The sprite editor: 24×24, two frames, one bit per pixel.** Draw, erase, flood fill, undo
  (32 deep), clear, mirror H and V, copy frame 1 → 2, and live previews at 1× and 2×, exported
  as XBM rows — 3 bytes per row, **the low bit of each byte the leftmost pixel**, which is
  `drawXBM`'s layout and `ui/xbm_mirror.h`'s. Spec §37's palette, transparency and dimension
  normalisation are the FORMAT rather than checks: 1 bpp at a fixed 24×24 is all three, decided
  before the user draws, which is also why it accepts no image upload.
- **`tools/gen_index_html.py`, with `--check`.** Regenerates the header in memory and hard-fails
  on any byte of drift, so a hand-edited header and a page edit that was never regenerated both
  fail the gate exactly as `gen_content.py --check` makes them. **No minifier and no npm
  dependency:** whole-line comments and indentation out, nothing else — a generator whose output
  depends on a package version is a generator whose `--check` fails on somebody else's machine.
  It also substitutes `CREATOR_API_VERSION` into the page from `core/version.h` and emits a
  `static_assert` tying the two, so a page cached from an older device says so instead of being
  refused by number.
- **Attack and type NAMES in `GET /api/schema`.** `"an"` (34 names, positional with `"atk"`) and
  `"tn"` (SIGNAL, CORRUPT, SYSTEM, NEUTRAL), emitted as `\uXXXX` escapes so the served document
  stays printable ASCII. **427 B of `.rodata`, zero globals, and `CONTENT_VERSION` unchanged at
  `0x02B5`** — it hashes the JSON, and this is an emitter change. The alternative was 34 Spanish
  strings typed into the page, which is the second source of truth this document exists to
  prevent.
- **`tools/page_test.mjs` — the page driven in a real headless browser, in the gate.** It loads
  the blob `index_html.h` serves, answers it with the document `creator_schema_json.h` serves,
  draws with real pointer events, and pipes the body the page POSTs into `tests/bin/creator_decode`,
  which links **the real `creator_parse.cpp` and the real `validate.cpp`**. 51 assertions, ~3 s,
  skipped with a printed word where there is no browser. **It does NOT tick §67's "Mobile editor
  works" or "Sprite editor works" — those are bench items and a desktop Chromium is not a thumb.**
- **Two new gates in `tools/check.sh`:** every `/api/...` the page calls must be a route
  registered under `src/networking` (the half a byte-diff structurally cannot see), and the page
  may not name an off-device host (§33 "works offline after connection").

- **The CREATOR screen is spec §34's own screen (P8-C5).** "ESCANÉAME" / the symbol / `PIN` in
  9×19 digits / "Con el móvil", down the 62 px column beside the 62 px symbol, with the line under
  the headline naming whichever of the two the symbol currently encodes — the SSID while the
  "join me" symbol is up, the address while the URL symbol is up. §34's "do not clutter this
  screen with unrelated UI" is why the connection hint and the always-on IP line are gone with the
  placeholder. **+26 B of flash and ZERO globals, on every variant.** The five row baselines carry
  four `static_asserts` (the digits may not overlap the label, the hint may not overlap the digits,
  the hint may not land in the affordance strip, the headline may not run off the top), so the
  panel is checked by the compiler and not only by the golden.
- **`creator_payload()` — the bytes the symbol actually encodes, and the §39 test that reads
  them.** `tools/check.sh`'s "no secret in the QR" gate greps `src/networking`, which is where
  `net_url()`'s old `?k=` lived and is **not** where `ui/screen_creator.cpp` is: a PIN appended in
  `build()`, the one function that decides what `qr_encode()` is handed, would have shipped with
  the gate green. Three host cases close it, the middle one exhaustively — **the payload is
  byte-identical at all 9,999 PINs `cg_mint_pin()` can produce, both symbols.** A substring search
  would have been the wrong test: `PEBBLEBOL-1234` is an ordinary real SSID.
- **`tools/creator_smoke.sh` — the bench instrument for four §67 boxes.** All seven §38 routes over
  curl, a body over `CS_BODY_MAX` answered 413 and one over `CS_BODY_DRAIN_MAX` answered with a
  closed socket, five wrong PINs then a sixth refused as locked (and the lockout refusing the
  correct PIN too), and the portal dying on schedule under 5 minutes of **unauthenticated** polling.
  **It binds nothing — it speaks HTTP to a device that ran `app_setup()`** — and it therefore
  cannot run in a build environment, so it ticks nothing here. `--variant` is required and has no
  default (`docs/budget.md` §8), every constant is read out of the header that owns it, and phase 1
  refuses to proceed unless the board's API version, `FW_VERSION`, `CONTENT_VERSION`, advertised
  body cap and served page length all match this tree.
- **Three more gates in `tools/check.sh`:** `creator_smoke.sh --dry-run` must still parse the tree
  (a renamed macro fails the gate by name instead of failing at the bench), the set of routes it
  probes must EQUAL the set `src/networking` registers **in both directions**, and it may not name
  a loopback host — a green run against a mock is the fixture-that-is-not-the-firmware defect this
  project has hit three times.

- **The sanitiser is a gate stage (P8-C6).** `make -C tests asan` builds `test_creator_api`,
  `test_creator_gate`, `test_validate` and `test_persistence` with `-fsanitize=address` and runs
  them; `tools/check.sh` runs it on every commit, ~9 s from cold, **skipped with a printed word**
  where the toolchain has no `libasan`. It is the only instrument three case names in
  `test_creator_api.cpp` have ever had: `test_creator_api.cpp` mallocs every fuzz body at exactly
  its own length **so that** a one-byte over-read is a heap error, and nothing had ever compiled
  with a sanitiser. Measured — `cp_skip_ws()`'s `while (c.i < c.n)` changed to `<=` printed
  **ALL PASS 49/49** on the plain build and `heap-buffer-overflow at creator_parse.cpp:62` here.
- **`every_required_key_is_required_on_its_own` (P8-C6).** Each of the six required keys is
  excised from the valid document on its own and must give `CP_MISSING_KEY`, and then put back
  and give `CP_OK` — the round trip is what makes the refusal attributable to the missing key
  rather than to a mangled document. All six mutation-tested one at a time.
- **The PIN gate on the routes that WRITE now has probes (P8-C6).** `tools/creator_smoke.sh`
  phase 3 sends `POST /api/pebble` with a valid document and no `X-Pin` (403, and the Box count
  must not move), `POST /api/time` with a wrong header, a wrong PIN carried in the **body**, and
  the correct PIN carried in the body **accepted** — the last because a one-sided assertion
  passes against a firmware that refuses every body PIN. Phase 2 gained the only probe that can
  see a catch-all narrowed to `HTTP_GET` on a real socket: an oversize POST to an **unmatched**
  path must be answered 413, not read whole and then 404'd.
- **A compile-time bound on `GET /api/state` (P8-C6).** `creator_server.h` said every response
  body "has a worst case provable at compile time" and no such proof existed — `config.h` carried
  a prose estimate. `CS_STATE_WORST` is now derived from `sizeof` the format plus each field's own
  bound and `static_assert`ed against `CS_OUT_BUF`. It matters because it moves with `FW_VERSION`,
  which phase 9's tag and phase 10's both grow, and the failure mode is a silently truncated JSON
  body the page refuses. Mutation-tested: the build stops by name.
- **Four more gates in `tools/check.sh` (P8-C6):** the catch-all must be registered exactly once
  as `.on(UriAny(), HTTP_ANY, ...)`, and the bench script must keep its unauthenticated POST to
  the write route, its body-carried-PIN probe and its oversize POST to an unmatched path. Each
  was mutation-tested.

### Fixed

- **A test named `creator_payload_carries_no_pin` did not read the payload (P8-C5).** It asserted
  three things about `CreatorInfo.url` — no `k=`, no `1234`, equal to `http://192.168.4.1/` — all
  of which the host fixture types into that field forty lines above. It could not fail for the
  reason its name gave: a PIN appended inside `build()`, between reading `in.url` and encoding the
  symbol, left every check green. Replaced by three cases that read `creator_payload()`, and the
  surviving half renamed to what it holds
  (`creator_join_string_fits_the_version_2_byte_budget`).
- **`STR_CREATOR_PHASE` was Spanish prose in an ASCII-only font.** "Creador - Fase 8" was drawn in
  `GF_TINY` (`u8g2_font_4x6_tr`), which `strings_es.h`'s own rendering rules reserve for "version
  strings, IPs and hex". The string is deleted with the placeholder it belonged to.
- **The CREATOR screen was timed out from under the user after 20 seconds.** `SCR_CREATOR` had
  the default screen flags, so invariant 3's navigation auto-return applied to the one screen
  whose whole purpose is that the user is looking at a phone instead of pressing buttons:
  entering CREATOR and walking away sent the device HOME and tore the access point down in less
  time than joining a network takes. The row is `SF_STICKY` now, with two screen-owned exits in
  its place — `CREATOR_AP_WAIT_MS` (§47) and the D7 grace period — both leaving through the same
  teardown a B press runs.
- **A CREATOR PEBBLE WOULD HAVE BEEN QUARANTINED ON THE FIRST POWER CYCLE, and the defect was
  already waiting in the tree.** `species_get(200)` answered `nullptr`, so `box_new_pebble()`
  refused a creator species outright and a Pebble filed any other way was flagged
  `VR_UNKNOWN_SPECIES` by `save_manager.cpp`'s `quarantine_scan()` at the next boot. The load
  path now rebuilds the registry from the `cs*` records **before** the scan runs, and
  `tests/test_validate.cpp::a_creator_pebble_is_an_ordinary_pebble_to_the_one_validator` drives
  both halves.
- **An over-long string left the reader's cursor inside it.** `cp_read_string()` returned
  `CP_STRING_LEN` the moment the buffer filled, without consuming to the closing quote — so the
  one caller that tolerates that code (`cp_read_pin()`, for which an unusable PIN is not a
  malformed document) read the tail of the value as the next token, and a body carrying a long
  `"pin"` was refused as `CP_SYNTAX` instead of being read and gated. Found by
  `tests/test_creator_api.cpp`, fixed at the reader.
- **A throttled or PIN-refused client could still degrade the display.** `note_request()` ran
  BEFORE the rate limiter in `h_root` and `h_notfound`, so a client being answered 429 still
  dropped the renderer to `FPS_LOW` — a permanent, free degradation for anyone in radio range.
  P8-C2 recorded it; the order is now rate limit, then hint.
- **`POST /anything-else` still walked the unbounded body reader.** Registering raw handlers
  protects only the URIs that have them: `Parsing.cpp` requires `_currentHandler` non-null, and
  `onNotFound()` is not a handler. A catch-all `Uri` is now registered LAST, with the same body
  hook, and `onNotFound()` is gone.
- **A multipart POST was a null dereference any client could ask for.** The same function is
  invoked as the UPLOAD hook on the multipart path, where `_currentRaw` is null and
  `WebServer::raw()` dereferences it with no check. The hook now reads the collected
  `Content-Type` first — which is why `Content-Type` joined `X-Pin` in `collectHeaders()`.
- **A PIN could not survive a reboot.** `gs_load()` zeroed `creator_pin`, `pin_fail_count` and
  `pin_lock_until` on every load — a correct P2-era guard for a feature that did not exist yet,
  and the reason the first round-trip test failed. Removed; the concern it named is answered by
  never restoring the lockout *deadline*, only the armed state.

- **The catch-all gate could not fail, and it guarded the phase's headline security claim
  (P8-C6).** `tools/check.sh` held it with `grep -rn 'UriAny' | wc -l` >= 2 — a count of a TYPE
  NAME. The struct definition alone contributes three occurrences, so deleting the registration
  left 3 and the gate passed; narrowing `HTTP_ANY` to `HTTP_GET` on that one line also passed,
  built clean at 0 warnings, and puts every unmatched POST back on `readBytesWithTimeout()`'s
  malloc growth loop. Both reproduced (`GATE OK` in each case) before the gate was rewritten to
  match the registration itself.
- **The required-key mask was asserted only in aggregate (P8-C6).** The `CP_MISSING_KEY` case
  drove `{}` and `{"v":1}`, so whichever single key was dropped from `creator_parse.cpp`'s mask,
  `CPK_V` or `CPK_NAME` still fired. With `CPK_TYPE` and `CPK_SPRITE` both removed the suite
  printed **ALL PASS 49/49**, and a document with neither key parsed `CP_OK` and validated
  `VR_OK` — **a type-defaulted, entirely blank creature accepted by `POST /api/pebble`**. Neither
  key has a downstream guard: a zeroed `type` is `TYPE_SIGNAL`, and the validator never inspects
  the sprite bytes. The mask is the only thing standing there, and now it is tested key by key.
- **`h_root`'s throttled exit was the one server exit without `cs_body_done()` (P8-C6)**, against
  an invariant `creator_server.h` states as holding on EVERY handler. Nothing reachable today
  inherits a stale accumulator through it; "unreachable" is a claim about today's exits. The fix
  made the function 18 B SMALLER — both exits now end in the same call and the compiler
  tail-merged them.
- **`docs/budget.md` reconciled phase 8 against the forecast it passed and not the one it missed
  (P8-C6).** §3 forecast 30-45 K flash; phase 8 spent **+57,274** on `release`, 27 % past the top
  of the band, and §§9-12 each quoted the 1.5-3.0 KB globals line while naming neither. The P8 row
  is now struck with SPENT figures on both axes the way the P5 and P6 rows are, the overrun has
  its one address written down, and the ending projection is re-scored — the globals projection
  was already past, since the phase-6 re-scoring expected 53,624-57,924 at the end of phase 10 and
  the artefact is at **59,396 today**.

### Changed

- **The PIN left the QR payload (spec §39).** `net_url()` emits `http://<ip>/` and lost the
  `pin` argument entirely; `tools/check.sh` gained a gate that fails the build if a formatted
  query parameter reappears under `src/networking`.
- **`AP_SSID_PREFIX` is `PEBBLEBOL-`**, closing decision D3's last open piece. The soft AP stays
  **open**: no WPA passphrase of any length fits the join QR (32 B of fixed text against a 32 B
  version-2 budget), and `docs/decisions.md` carries the arithmetic and the owner's remaining
  choice.
- `ConfigV2.pin_fail_count` holds **0 or 5 and nothing between** — only the armed edge reaches
  flash, so an attack episode costs two writes rather than one per guess.
- **The spec §36 stat rule is a BAND, not an equality (P8-C3):**
  `CREATOR_STAT_POINTS_MIN (16) <= hp+atk+def+spd <= CREATOR_TOTAL_STAT_POINTS (22)`. Three
  shipped sentences disagreed, and the measurement that settles it is Appendix C's own worked
  example: **72 % is unreachable at a full 22-point stat budget** — the cheapest legal four-move
  set costs 84..86 depending on type, which prices at 73 % — so requiring equality would have
  made the product spec's own screenshot impossible to produce. `docs/decisions.md` carries the
  enumeration; `tests/test_validate.cpp` runs it rather than quoting it.
- **A custom Pebble does not travel.** `networking/protocol.cpp` already refuses a wire record
  with `species > 199` as `VR_WIRE_CUSTOM_UNRESOLVED`; that now has a stated consequence —
  custom Pebbles are local until a later phase sends the `cs*` record alongside them.
- **`CS_SPRITE_W` / `CS_SPRITE_H`** name the 24x24 geometry that had lived only in a comment,
  with a `static_assert` tying them to `CS_SPRITE_BYTES`. §35's "sprite dimensions" now has a
  source the served schema can quote.

- **Four residuals are recorded rather than patched (P8-C6),** on the rule that at a phase exit a
  behaviour change which weakens a tested property is worse than a written trade.
  `creator_server.h`'s NOT-TRUSTED table read as comprehensive and was not: **the request line,
  every header and the URL are read by the pinned core before any handler runs, into heap-growing
  Strings with no length bound and a per-byte timeout that resets on every byte** — so a trickling
  client blocks `handleClient()`, and there is no rescue because the loop task WDT is off in this
  core and this tree never arms it (`app/app.h`'s "5 s Task WDT" sentence is corrected to say so).
  `creator_gate.h` now argues the lockout from the **availability** side too: an absent `X-Pin` is
  a counted failure, so five unauthenticated requests a minute keep the OWNER out, across a
  reboot, and the idle teardown becomes the attacker's tool — with the two obvious mitigations
  named and why neither is better. `webui.cpp`'s "persisted BEFORE it is shown" is qualified for
  the read-only session, where it is not. And `web/creator/app.js` and `creator_server.h` now name
  the **one** page rule with no device twin — the empty-sprite check — and state the direction:
  the page is narrower there, never wider. A blank creature is legal; §35 asks for sprite
  dimensions, data size and palette, all three structural here.
- **`cb_state_name()` and `cb_ready()`'s `seen == declared` clause are documented as what they are
  (P8-C6):** the first has no firmware caller and is absent from both the release and the baseline
  image (`--gc-sections`), existing for the tests and for the `static_assert` beside `CB_NAMES`;
  the second is an equivalent mutant today by construction and is kept as a bound against a core
  that over-delivers. Both named so the next sweep does not file them as gaps.

### Removed

- **Bluetooth Low Energy, entirely.** `networking/ble_social.{h,cpp}`, `FEATURE_BLE`, the
  fourteen `BLE_*` tuning constants, `BlePeerInfo`, `RADIO_BLE`, the three `NERR_BLE_*` codes,
  the `no-ble` build variant and `app_loop()`'s `ble_scan_service()` rung. Decision D2 chose
  ESP-NOW for the peer link in phase 7 and nothing has called `ble_begin()`, `ble_service()` or
  `ble_peer()` since P7-C2 gave the LINK screen the new transport.
  **The bench test that D2 gated this on was NOT run** — there is no hardware in this
  environment — and the owner authorised the deletion on that basis. §67 "Local multiplayer
  works" stays unticked. It is one commit; `git revert` restores BLE whole.
- **The radio settle window**, with it: `NPH_SETTLING`, `RADIO_SETTLE_MS`, `begin_settle()`,
  the deferred half of `net_request()`, the settle timer in `net_service()` and the
  `NPH_SETTLING` arms in the scan and link drivers. It existed only because Bluedroid and the
  Wi-Fi driver could not both be resident; a Wi-Fi-to-Wi-Fi transition never entered it.
  `net.h`'s "exactly one radio stack is resident" invariant went the same way — it was a real
  constraint with no subject left.

### Changed

- `CF_BLE_ENABLED` (0x04) is now `CF_RESERVED_BLE`. **Same value, still round-tripped through
  the v1/v2 config conversion**: a save written before the deletion has the bit set, and
  `tests/fixtures/config_v1.bin` is one. Fresh devices mint it clear.
- Comments across `networking/`, `game/taint.h`, `ui/ui.h` and `tests/` that cited
  `ble_social.cpp` line numbers as precedent now cite
  `git show ed9b099:Pebblebol/src/networking/ble_social.cpp`.

### Fixed

- **`docs/budget.md` §7's headroom argument, which was wrong.** It called BLE "the lever" for
  the 8,180 B free under `GATE_RELEASE_GLOBALS_MAX` — "23,496 B, three times the remaining
  headroom". The `release` variant was `GOD_MODE_ENABLED=0 FEATURE_BLE=0`, so BLE was never in
  the image those caps police: that 23,496 B is the `baseline` minus `no-ble` delta, a dev-build
  figure read as a shipping one. **Deleting BLE moved release globals by sixteen bytes.** The
  headroom figure itself was right and is unchanged. Corrected in `docs/budget.md` §8,
  `docs/decisions.md` and the plan's phase-8 preamble.

### Measured

- `baseline` 1,994,460 / 80,492 → **1,281,500 / 56,980** (−712,960 / −23,512).
- `release` 1,269,468 / 56,820 → **1,269,126 / 56,804** (−342 / −16) = 79.3 % and 87.4 % of
  the release caps. The baseline is now within 12,374 B of flash and 176 B of globals of the
  artefact, where it was 725 KB and 23.7 KB away — so `tools/check.sh`, which polices the
  baseline, now polices a build shaped like the one that ships.
- `nm` over the release `.elf`: no Bluedroid, no `BLEDevice`, no `BLEScan`; the only Bluetooth
  symbols are `esp_bt_controller_mem_release` and `esp_bt_controller_rom_mem_release`, ESP-IDF
  stubs the core links unconditionally.
- Gate green (47 host binaries), MATRIX OK on six variants at 0 project warnings.

## [0.7.0-social] — Unreleased

Phase 7 is the phase where a Pebblebol stops being alone. Two devices find each other over
ESP-NOW, agree to talk only when **both** players press A, fight a lockstep battle whose reward
neither device can award by itself, and swap a Pebble through a flash journal that survives the
power being cut at any single write. `game/breeding.{h,cpp}` computes a balanced offspring from
two parents and a shared seed. And the phase exit found that a successful trade **destroyed a
Pebble on the shipping build**, fixed it, and then discharged the seven debts phase 6 carried
forward.

**Two acceptance items are deliberately UNTICKED and neither was run.** §67's "Local
multiplayer works" and P7-C1's "two boards see each other's beacon in LINK" are BENCH tests,
there is no hardware in this environment, and a bench item is not something a host binary can
tick. **BLE is therefore still in the tree at this tag**: D2 authorises its deletion, and
P7-C1's own bullet gates that deletion on ESP-NOW having linked two real boards. Deleting the
only fallback before the chosen transport has ever run would be exactly backwards.
*(0.8.0-creator deleted it anyway, on the owner's explicit decision to bet on ESP-NOW without
the bench test — and measured that the 23,496 B of static RAM this paragraph calls a lever was
never in the shipping image at all. See that entry.)*


### P7-C1 — the device transport, per decision D2 (ESP-NOW)

Two Pebblebols can now, in principle, find each other. `networking/transport_espnow.cpp`
brings ESP-NOW up on a `WIFI_STA` residency that still never associates — nothing in
`esp_now.h` mentions credentials, an access point, an IP or a netif, and the gate that counts
association call sites under `src/` still reads zero — broadcasts a 24-byte beacon every
500 ms while the radio is in the new `NPH_LINK` phase, and unicasts session frames to one
bound peer. `networking/discovery.{h,cpp}` is the beacon codec and a peer table of eight with
an RSSI moving average, a three-hit threshold and a 60 s TTL; `networking/rxring.{h,cpp}` is
the single-producer/single-consumer ring the Wi-Fi callback posts into. Both of those are
PURE and caller-owned and reach the radio through a four-call driver struct, so the whole of
discovery is driven by two new host binaries with no radio at all: 41 → **43 binaries**,
787 → **827 tests**.

**The seam held, and that is the headline.** `networking/session.cpp` and
`networking/battle_link.cpp` are BYTE-IDENTICAL across this chunk. The transport interface
P4-C5 built for a radio that did not exist took the radio with no widening, no new field and
no conditional compilation, and `tests/test_link_transport.cpp` runs a whole battle between
two real sessions over a `Transport` whose `recv()` **is** the ring the radio fills — so the
claim is demonstrated rather than asserted. A new gate keeps it true: those two files may
contain zero preprocessor conditionals, and `esp_now_*` may be called from exactly one file.

**The ring depth the design survey proposed was wrong by a factor of four, and it would have
failed silently.** The survey reasoned from the protocol's shape that eight slots were
"generous, not tuned". Measured over 300 varied battles: one `session_poll()` puts up to
**ten** frames in the peer's ring on a clean link and **nineteen** at a 10 % per-frame drop,
where the retransmission ladder is re-sending while the peer is still catching up. Eight
slots hold seven and refused three frames per clean battle; sixteen would still have
overflowed. `LINK_RX_SLOTS` is 32. The reason the number had to be measured is the reason it
matters: **a frame lost to ring overflow is indistinguishable to the session from a frame the
radio dropped**, so getting it wrong produces a link that is quietly slower than it should be
with no error anywhere.

**A peer's hardware address has exactly one home and a gate that says so.** The six bytes live
in one private array inside `transport_espnow.cpp`; what crosses into the peer table — and
therefore into the game, the screen and anything persisted — is an opaque slot index. The
build fails if `discovery.{h,cpp}` so much as names that identifier, in a field, a parameter
or a comment. The shape being avoided is in this tree right now: `BlePeerInfo` carries the
raw address as its first member. And the gate's limit is stated rather than glossed — it
catches a rename and cannot see a value — so a test pins `sizeof(DiscPeer)` and every offset.

`act_note_peer()` finally has a call site, in the peer table where the plan said it belongs
and paid **once per peer that crosses the hit threshold, never once per beacon**, with the
activity module linked into the same binary because that rule is a property of the two
together. It still contributes 0 to every real score, because nothing drives the discovery
job until the LINK screen exists in P7-C2 and §42's consent belongs there with it.

**BLE IS NOT DELETED.** The plan gates that deletion on ESP-NOW having linked two real
boards, and the ordering is deliberate: the chosen transport has still never run on hardware,
so removing the only fallback first would be backwards. `ble_social.cpp` and `FEATURE_BLE`
are untouched.

**Not measured, not claimed:** no radio ran. Two boards seeing each other's beacon, the
modem-sleep default that makes an unassociated ESP32-C3 station a duty-cycled receiver, the
channel, ESP-NOW's own duplicate suppression and the receive callback's threading are all
bench facts, and the plan carries them as a five-item bench list in the order to run them.

Nineteen source mutations and seven gate mutations were planted; each made a named test or a
named gate fail, and the table is in `docs/decisions.md`.

Sizes, all seven matrix variants green with zero project warnings: baseline
1,948,802 / 73,580 → **1,958,560 / 79,412**; release **1,233,762 / 55,740**, which is 77.1 %
and 85.8 % of the caps `tools/build_matrix.sh` enforces. Two variants changed meaning and
`docs/budget.md` says so: `no-web` now links the whole Wi-Fi driver (`+580,418` flash) because
ESP-NOW is a Wi-Fi consumer that needs no web server, and `all-off` is byte-identical at
547,274 / 25,324 while having become a build that switches the peer link off rather than one
that never had a switch.

### P7-C2/C3 — the LINK screen, the consent that gates it, and the battle over the link

Two Pebblebols can now fight each other, and neither can be made to by the other one.

**Consent is not implied by proximity, and it is enforced twice.** The LINK screen browses,
lists the peers it has heard three times above the signal floor, and opens §42's card —
"¡PEBBLEBOL ENCONTRADO!" over COMBATE / INTERCAMBIO / CRIAR / CANCELAR — on the one the player
picks. Pressing A there is the ONLY thing in the firmware that calls `session_init()`: until
it happens this device has no session at all, drains no transport and has no unicast peer
bound, so the far device's HELLO is neither answered nor, on real hardware, even delivered —
the receive callback drops an unbound peer's frame as `rx_wrong_peer`. The handshake is
symmetric by construction, so the device whose player pressed A alone climbs its own ladder
into §47's "CONEXIÓN PERDIDA / A: Reintentar / B: Salir", **measured at 9,950 ms**. Both
devices show the peer's name, the operation and the session's own state throughout. A new gate
fails the build if a session is opened anywhere but that one screen.

**Rewards only where both devices agreed.** `session_rewards_authorised()` — true only when
the peer's `BATTLE_END` carried the same outcome AND the same final hash as ours — is read in
exactly one place, and the battle screen asks that function rather than its own engine. So a
desync, a dead link and a walk-out all report `won == 0`, write no Box and touch no ledger,
and a divergence prints a NEUTRAL "Enlace interrumpido" rather than "has perdido", because a
lost radio is not a defeat. The sharpest case builds the local team twenty levels stronger so
the engine really does win, kills the peer's `BATTLE_END` by name on the link, and asserts
that the win is worth nothing.

**Every "side 0 is the player" in the battle screen became a byte.** The session fixes the two
sides from the two device ids, so on one board of every pair the player is side 1; thirty-three
lines — the legality predicates, the submit, both art fills, the header's HP pair, the switch
list, the outcome word, the armed battle modifier — would otherwise have drawn the PEER's team
as the player's and paid the wrong half of the fight.

**Two holes were found rather than designed around.** `link_cancel()` reaches
`net_request(RADIO_OFF)`, so ending the browse the obvious way when two players consent would
have put the radio down one frame later; `link_hold()` now stops the browse and keeps the
stack, and the §47 ceiling changes owner to the session's own ladder. And `sm_home()` discards
the back stack and runs only the CURRENT screen's leave hook, so LONG_BOTH out of a linked
battle would have left the radio up and the power ladder clamped for ever — `battle_leave()`,
which runs on every route off that screen, now hands the link back.

**A linked round gives each player nine seconds, measured, and the screen says so.** Only
progress resets the retransmission ladder, so a round neither device advances is closed
`SE_LOST` after `PROTO_RETX_MAX * PROTO_RETX_MS`. The linked battle's header carries the
remaining seconds; substituting a move on a timeout is forbidden in principle and widening the
ladder is a change to the session's own measured tuning, so the constraint is written into the
plan with its one-line remedy and its cost rather than papered over.

`STR_SO_LINK_SOON` ("LINK - Fase 7") is deleted, and `STR_LINK_PHASE` ("Enlace - Fase 7"),
which was the string the screen actually drew, went with it. The BOX's inspect menu's
INTERCAMBIAR and CRIAR rows are entry points now instead of a toast: each pre-selects that
Pebble and opens LINK with the intent, consenting to nothing. `act_note_peer()` has its first
driver in the product. One new host binary — 43 -> **44**, 827 -> **854 tests**, 3,672,108 ->
**3,672,806 checks**: `tests/test_link_screen.cpp` drives the real screen against a real
caller-owned `Session` with a real AI over a real loopback, because a screen is a singleton and
two of them cannot share a process.

Twenty source mutations and three gate mutations were planted; **four of them were not caught
on the first run**, and the table in `docs/decisions.md` says which and why the assertion that
should have caught each one was about a neighbouring fact — a linked result reported the moment
the ENGINE finishes, the local side hard-coded back to 0, a transcript never re-opened, and a
peer list offering a device heard once. A fifth failed nothing because the CODE was wrong
rather than the test: `link_screen_busy()`'s second clause could not answer true where the
first answered false, so the sentence was narrowed instead of the test being stretched around
a branch that cannot happen.

**Not measured, not claimed:** no radio ran, again. §67's "Local multiplayer works" is
UNTICKED, and so is the radio half of the consent gate — the loopback delivers regardless of
binding. The plan carries a five-item bench list for this chunk on top of P7-C1's.

Sizes, all seven matrix variants green with zero project warnings: baseline
1,958,560 / 79,412 → **1,985,080 / 80,340**; release **1,260,236 / 56,668**, 78.8 % and 87.2 %
of the caps. The 26 KB of flash is not the screen's drawing: it is the session, the codec, the
peer table and the wire half of the validator entering the image for the first time, because
until this commit nothing reachable from `setup()` called any of them.

### P7-C4 / P7-C5 — the atomic trade, breeding, and the god-taint gate

**Two Pebblebols can trade, and a power cut in the middle cannot make a Pebble twice or
none.** `game/trade.{h,cpp}` owns the journal and the write order; `networking/trade_link.cpp`
owns the five wire steps; `ui/screen_link.cpp` drives both, so the INTERCAMBIO row that
answered "Aún no está listo" since P7-C2 opens a real session now and `LK_SELF_CAPS` claims
`DISC_CAP_TRADE`. `app_setup()` calls the boot resolver between the save load and the pet
binding — until this commit `save_load_all()` filled `gs.trade` and
`grep -rn 'gs.trade' app/ ui/ game/` returned nothing, so the journal was bytes nobody read.

**The fault injection is a SWEEP, and the knob for it did not exist.** `tests/fakes/kv_mem.h`
offered a one-shot `kv_mem_fail_next_put()`, so a test built on it could only cut where it
already knew the index — which is to say at the moments whoever wrote it was already thinking
about. `kv_mem_fail_after_n_puts(n)` plus `kv_mem_power_restore()` turn the cut into a
parameter, and `tests/test_trade.cpp` sweeps **every flash write the sequence performs** (a
committed trade costs 10: five through the store seam and five inside `save_checkpoint_all()`),
plus the four that open the journal, plus nine cut points **inside the resolver itself** — each
followed by a reboot, a resolve, the pair invariant, and a second reboot that must change
nothing.

**Across the pair it is bounded and measured, not proved, and the header says so.** Two parties
over a lossy link with no third party cannot make an exchange atomic; `COMMIT` subsumes
`CONFIRM` so losing it needs two frames gone for a whole ladder rather than one, and the test
reports the residual as a table instead of asserting it away: 800 trials over four fault arms,
**0 split**, 195/200 completing at 30 % drop with 20 % reorder.

**Two defects the lossy arm found that a clean link could not**, both the same shape as the R4
hole P4-C5 found — an endpoint deciding from the arrival that happened to be last instead of
from its own state. A READY that arrived before its own OFFER left an endpoint parked with both
halves in hand (**74 of 200 at 10 % drop, 183 of 200 at 30 %**), and the ladder's regeneration
of `SESSION_REQUEST` dropped the new operation byte, so the retransmitted request said "battle"
while its sender was trading and the responder's refusal was read as an agreement (**34 of 200
at 10 % drop, 0 of 200 clean**). A field added to a message is two edits, not one: the sender
and the regeneration.

**`PROTOCOL_VERSION` 1 → 2, and the reason this repository gave for the bump was false.** The
plan and `docs/protocol.md` both said P7's trade messages would exceed `PROTO_PAYLOAD_MAX 148`.
A `TRADE_OFFER` is 52 bytes and the cap did not move; the bump was mandatory because the TYPE
SPACE was closed at 1..12. Four types appended (13..16), two reserved bytes became real fields
(`SESSION_REQUEST.rules` is the operation, `SESSION_ACCEPT.op_echo` the answer), and the golden
ACTION frame was re-recorded with only byte 0 and its two CRC bytes moved.

**Two pre-existing persistence defects, fixed because the journal rests on them.**
`commit_all()` rewrote five of the six blobs and not `tr`, so after the SAVE ERROR screen's
"Recuperar" RAM said IDLE and flash still held a live record. And `save_checkpoint_all()` does
NOT checkpoint the journal and must not — a mid-trade checkpoint is a second, stale source of
truth for one transaction — which is now written down instead of left as an omission.

**Breeding.** `game/breeding.{h,cpp}` over `genome_breed()`: the same non-zero `compat_group`,
both parents at stage ≥ 1, distinct ids. `SpeciesDef.compat_group` has existed since P4-C1 and
was read by nothing in `src/`. The compat matrix test sweeps all **36 × 36 roster pairs with
the exact reject code** for each, because a handful of hand-picked pairs still passes with the
stage rule deleted. Determinism comes from a scripted `genome_set_rng()` source seeded from the
shared seed and never a `genome_seed()` reseed — `game/capture.h` named that trap and this is
the first caller to walk around it.

**Two plan bullets were rewritten rather than implemented, with the measurement behind each.**
"One inherited move from each parent" cannot be built against the shipped validator: any
substituted move is `VR_UNLEARNABLE_MOVESET`, the child would be quarantined on the next load,
and widening the rule reverses a measurement (attack 11 on a Paketo took a scripted 1v1 from
0/200 wins to 100). And `lineage_id = hash(A,B)` is a trap: `Genome.crc16` covers `lineage_id`,
so overwriting it without resealing produces `VR_BAD_GENOME` on the next load.

**The "hard balance ceiling" is two halves and only one can be broken, and the tests say which.**
The battle half is structural — `gvar(v) = v*3/16` folds 0..15 to 0..2 whatever any number of
generations does — so the case asserting it is labelled as one that cannot fail. What is not
free is the CARE envelope, and `breed_compute()` clamps the six numeric genes back into the
genesis band: **200 dynasties × 40 generations unclamped left the band 8,655 times and reached
the full 0..15; clamped, 0 times**, and the case asserts the control arm escapes so the clamp is
guarding something measurable.

**The god-taint gate is back, in the game layer.** `game/taint.h` — two inline functions, no
`.cpp` — shared verbatim by trade and breeding. Not in `validate_pebble()` (a tainted Pebble is
a legal object and the flag is inside the accepted mask on purpose), not in the transport (what
P2-C7b correctly removed). It reads BOTH markers, because `migration.cpp` sets the instance flag
from a v1 save without touching the genome bit, and it is honour-based, which the header says
rather than implies.

Twenty-five mutations were planted and the table is in `docs/decisions.md`. **One was not
caught**: deleting every "the write did not land" check left the sweep green, because once the
fake store is dead a reboot cannot tell "stopped at the failure" from "carried on regardless" —
the case that catches it asserts what a reboot hides, and a `TDR_LAST_PEBBLE` code was written,
measured to be unreachable and deleted in the same spirit.

Two new host binaries — 44 → **46**, 854 → **898 tests**, 3,672,806 → **3,802,557 checks**.

**Not measured, not claimed:** no radio ran. §67's "Trade is atomic" is host-proved within one
device and measured, not proved, across the pair; the two-board bench item is written into the
plan. `breed_link.cpp` is NOT built and `DISC_CAP_BREED` is NOT claimed, so CRIAR still says
so honestly rather than opening a session that could only time out. BLE is untouched.

**And `game/breeding.cpp` is NOT IN THE RELEASE IMAGE — measured with `nm`, not assumed.** No
`breed_check`, no `breed_compute`, no `genome_breed`: nothing reachable from `setup()` calls
them, so `--gc-sections` dropped the module. The flash below is the TRADE's, end to end. That
check exists because of what phase 6 found — the shipping build never advanced game time for
four phases — and the rule it left behind is to ask what the RELEASE artefact does rather than
what the build in front of you compiles. `game/trade.cpp` passes it: the boot resolver, the
journal writers, the wire codec and `s_tl` are all in the image.

Sizes, all seven matrix variants green with zero project warnings: baseline
1,985,080 / 80,340 → **1,993,954 / 80,484**; release **1,269,108 / 56,812**, 79.3 % and 87.4 % of the caps.

### P7-C6 — the exit: one destroyed Pebble, seven debts, and a grep that could not see a unit slip

**The exit opened on a blocking defect: a successful trade destroyed a Pebble on the shipping
build, and the boot resolver carried the identical defect through the identical shim.**
`game/trade.cpp` clears the outgoing slot (B1) and then files the incoming Pebble (B2), and
`box_add()` fills the lowest free slot — which is the slot B1 just released. So the sequence
writes **the same flash key microseconds apart**. `save_pebble()` DEFERS a second write of one
key inside `SAVE_MIN_GAP_MS` (1,000 ms) — `force` does not bypass that branch — **and returns
true**. The two shims returned that `true` as `TradeStore.write_slot`, whose contract is
literally "every function returns whether the bytes LANDED". B2 never reached flash, the Box
header was written over it and the journal was cleared to IDLE, so nothing was left to repair
it. The peer had already applied. The Pebble was gone.

**Reproduced before it was fixed, and it is worse than a race.** Binding `app/app.cpp`'s own
clock wiring in `tests/test_trade.cpp` — one line — makes the CLEAN trade case fail with **no
fault injection at all**: the Box holds 2 Pebbles instead of 3 and the trade costs **9 flash
writes instead of 10**.

**The fixture is why 27,819 checks could not see it, and that is this project's named pattern
with the polarity inverted.** `save_set_clock(nullptr, ...)` switches off `save_manager.cpp`'s
entire wear-filter branch. A 15-point kill sweep, a 9-point resolver sweep and an 800-trial
lossy table all ran against a save manager **the release artefact does not execute**. Phase 6
found a default living inside a dev-only path; here the DEFAULT was the safe one and the
SHIPPING wiring was the dangerous one. The fix is `save_pebble_now()` — no filter, no deferral,
no third answer — and two gates: `save_pebble()` may not be called from `app/` or `ui/`, and no
host test may bind a null millisecond clock.

**The seven debts phase 6 carried forward are all discharged, three as defects and two as
documentation.**

- **The hourly care-gain ledger could be refilled by typing a date.** `sim_gain_restore()`
  aged its snapshot forward by `elapsed * cap / 3600` from two wall clocks a player sets on the
  time screen: 100 rounds of (clock +1 h, reboot) from a spent ledger manufactured **4,000
  happiness gain points** against a cap of 40 an hour. The term is deleted, the same way
  `xp_ledger_restore()` lost its at P6-C4. **The honest cost is written into `game/sim.h`**:
  off-time no longer refills the budget, so a player back after an hour away may wait up to 29
  minutes for a full meal. An unkind hour is recoverable; a stat budget a power cycle refills is
  not.
- **A Pebble at level 30 earned no activity happiness at all.** `xp_add()` returned at the top
  of the curve *before* spending the meter, and the activity reward is scaled by what the meter
  spent — so a maxed Pebble was paid 0 of the 5,500 milli it had earned. One line moved. It
  changes what the device-wide ledger means (XP handed out, not XP that found a home) and a
  named case now owns that behaviour source by source.
- **A cooldown blob could freeze the activity score for the life of the device.**
  `CooldownTable.act_day` is a u16 that holds 65,535 while the widest day a `uint32_t` epoch can
  name is 49,710, and the day only ever rolls forward — so a blob in between named a day nothing
  could beat. Measured: ten simulated years score **146,000** points honestly and **440** with a
  poisoned field. `act_adopt()` is one clamp at boot, with an unclamped control arm in the test
  so the guard is guarding something.
- **Nothing counted a dropped tick.** A gap wider than 16 s is resynchronised away and one
  second charged — correct, and silent for four phases, so a device stalling for a minute an
  hour looked identical to a healthy one. Two saturating counters now sit on the ENERGIA page,
  and the arithmetic moved into `hardware/power.cpp` so a host binary can drive it: the case
  asserts **both** halves, that the counter moved *and* that exactly one second was charged.
- **`gt_mono_ms()`'s choice of clock now has a test and not only a grep.** Two ~20-line fake
  headers let `tests/` compile `hardware/gametime.cpp` a second time with `-DARDUINO`, and four
  cases drive the DEVICE branch across a wake that zeroes uptime while the RTC keeps counting.
  **The mutation that justifies the whole exercise: divide the RTC by 1,000,000 instead of
  1,000 and the gate still prints `GATE OK` while all four cases fail.** A grep can prove the
  right identifier is in the right function; it can never prove the clock behaves.
- **Two items were documentation and are recorded as such.** The simulation environment is
  sampled once per tick rather than once per simulated second, so a slice straddling a day edge
  is charged on the wrong side by up to 8 s — worst measured care difference over half an hour:
  **0 milli-points**, and both files that described the charge as environment-exact now say so.
  And the paragraph explaining that a deep-sleep rung would need `app_loop()`'s uptime clock
  changed too now sits directly under the `static_assert` that trips it.

**One more untested load-bearing guard, found in review.** `session.cpp` refuses a
`SESSION_ACCEPT` at a RESPONDER — and deleting the role clause left all 46 host binaries green.
It is not redundant: one forged, well-formed frame drives a responder out of `SS_SESSION` and
makes it adopt the attacker's nonce, which is a `session_derive_seed()` input. The consequence
is bounded to denial, which `session.h` already states; the guard now has a case.

**Fifteen mutations, each making a NAMED case or a NAMED gate fail**, and one of them failed
nothing — `save_pebble_now()`'s cancellation of a pending deferred write buys one fewer flash
write and not correctness, because `save_service()` would have written the same RAM. The
**comment was narrowed and the assertion changed to count puts**, rather than the test stretched
around a claim wider than the tree.

Sizes: baseline 1,993,954 / 80,484 → **1,994,460 / 80,492** (+506 / +8, and the 8 B are the two
new counters plus alignment); release **1,269,468 / 56,820**, 79.3 % and 87.4 % of the caps.
`riscv32-esp-elf-nm` confirms `save_pebble_now`, `act_adopt`, `pwr_tick_budget` and
`sim_gain_restore` are all **in the release image** — a fix that is not in the shipping artefact
is not a fix, which is the rule phase 6 left behind.

### Phase 7 in numbers

Host suite 41 → **47 binaries**, 787 → **915 tests**, 3,658,841 → **3,802,956 checks**. Seven
new host binaries' worth of new subject matter: the ring, discovery, the LINK screen, the trade,
breeding and the device clock. `CONTENT_VERSION` unchanged. Screen goldens: four added, one
deleted, none of the existing 59 changed.

`tools/check.sh` gained **nine gates** this phase, each proven to bite by planting the thing it
forbids: `networking/discovery.{h,cpp}` may not name a hardware address, in a field, a parameter
or a comment; the ESP-NOW API may be called from exactly one file; `session.cpp`,
`battle_link.cpp` and `trade_link.cpp` may contain zero preprocessor conditionals; every file
under `networking/` must be classified pure or impure; `session_init()`/`session_start()` may be
called only from the LINK screen (the consent gate as a red line); `session_rewards_authorised()`
has exactly one reader; `ui/screen_battle.cpp` may not include the LINK screen or a networking
header; `save_pebble()` may not be called from `app/` or `ui/`; and no host test may bind
`save_set_clock(nullptr, ...)`.

Sizes, all seven matrix variants green with zero project warnings: baseline
1,948,802 / 73,580 → **1,994,460 / 80,492**; release 1,224,210 / 49,924 → **1,269,468 / 56,820**,
which is 79.3 % of `GATE_RELEASE_FLASH_MAX` and 87.4 % of `GATE_RELEASE_GLOBALS_MAX` and 40.4 %
of the 3,145,728 B `app0` slot. **Phase 7 went over its budgeted allowance on both axes** (20–35
KB flash and 2.0–4.0 KB globals forecast; +45,658 and +6,912 actual) and `docs/budget.md` §7 says
where: 5,312 of the globals are one receive ring whose depth was measured rather than guessed,
and 26,520 of the flash is the session, the codec and the wire validator entering the image for
the first time when the LINK screen finally made them reachable. **`no-web` moved +616,358 flash
and none of it is a phase-7 feature** — ESP-NOW is a Wi-Fi consumer that needs no web server, so
that variant now links the Wi-Fi driver it used to compile out, and `all-off` is the only
variant left with no radio in it.

**Nothing in this phase has run on hardware, and that is a larger caveat here than in any
earlier one, because the whole subject is two devices talking.** Unobserved: whether two boards
see each other's beacon at all; the modem-sleep default on an unassociated station; the channel
after a §40 scan; ESP-NOW's duplicate suppression and real MTU; the receive callback's threading
(the single-producer/single-consumer claim rests on discipline and two precedents, and a host
binary is single-threaded); the radio half of the consent gate, since the loopback delivers
regardless of binding; and a flash erase interrupted mid-page. The plan carries an eleven-item
bench list in the order to run it, and its **first** item is the modem-sleep check.

## [0.6.0-activity] — Unreleased

Phase 6 is about the three things the device does when nobody is pressing a button: it
scores the day you carried it, it makes a noise, and it goes to sleep. The activity score
(§25) is four capped terms — carried minutes ×1, care interactions ×2, distinct new
`net_hash` ×10, peers met ×15, 440 points for a full day — computed by a pure module that
takes the wall clock as an argument and pays XP, happiness and a rare-encounter permille.
The tone engine is spec §19's seven words as `{Hz, ms}` step lists in flash, driven one step
per service call behind a recording seam, so 21 host cases assert the emitted notes with no
piezo attached. And the power ladder is ACTIVE → DIM → IDLE → SLEEP with a pure decision
half and four hooks, whose deepest rung is a **light** sleep — not because light is better,
but because `PIN_BTN_L` is GPIO10 and the ESP32-C3 can only wake from deep sleep on
GPIO0..GPIO5, so a deep sleep on this pin map would leave the left button physically unable
to wake the device. That is decision D1's deferral showing up as a product fact, and
`docs/decisions.md` now carries the table.

**The exit found three defects and fixed them before cutting the tag, and the first is the
one worth reading twice.**

- **THE SHIPPING BUILD NEVER ADVANCED GAME TIME.** `logic_tick()` moves the world by
  `sim_step_seconds() * owed`, and `sim_step_seconds()` returned a zero-initialised static
  whose only writer, `sim_set_time_scale()`, is called from two lines that both live inside
  `dev/godmode.cpp`'s `#if GOD_MODE_ENABLED` half. The `release` variant — the artefact
  `config.h` itself calls "the one that actually gets flashed" — is `GOD_MODE_ENABLED=0`, so
  the scale stayed **0**: every tick was `sim_tick(0)`, and care decay, ageing,
  poop/sickness, the XP carry drip and the activity carried minute were dead for as long as
  the device was switched on. `gs_touch_lastseen()` still ran, so the next boot's absence was
  ~0 as well — the pet decayed **only for the hours the device spent powered OFF**.
  Pre-existing since the phase-2 stub, and P6-C3 rewrote that exact line and rested the sleep
  ladder on it. **The suite could not see it:** every case in `test_care.cpp` calls
  `sim_tick(n)` with its own `n`, so the number the firmware multiplies by was never on the
  line — planting `sim_step_seconds() { return 0; }` left it at ALL PASS 41/41. The default
  is `sim_bind()`'s now, god mode is documented as an override, and
  `care_a_freshly_bound_sim_advances_one_real_second_per_tick` fails with six checks without
  it.
- **A TYPED CLOCK PLUS A REBOOT WAS AN UNBOUNDED XP AND HAPPINESS FARM.**
  `xp_ledger_restore()` aged the saved anti-farm budget forward by
  `(now_epoch − saved_epoch) / refill_step`. Both epochs are wall clock; the wall clock is
  typed by the player on the TIME screen. **Measured: 100 rounds of (clock +1 day, reboot,
  one Wi-Fi scan of the SAME ten access points) spent 990 metered XP and 125,000 care
  milli-points in ZERO real seconds** — against 37.8 XP for an honest day at full tilt and a
  happiness bar that only holds 100,000. Neither half does it alone (100 reboots with the
  clock still: 0; 100 clock jumps with no reboot: 0 metered XP) and **neither module's own
  test binary contained both modules**, which is how `tests/test_activity.cpp` came to hold
  four named cheat cases — uncalibrated, insane, rolled backwards, power-cycled — and miss
  the one direction that pays. The aging term is gone; the happiness, which had no meter at
  all and needed no reboot to farm, is now scaled by the XP the ledger actually granted;
  `activity.o` is linked into `test_xp` and three named cross-module cases hold it. **After:
  the same 100 rounds, started from a FULL bucket, spend exactly `XP_CAP_CARRY` (48) and then
  nothing, ever.** What the honest player pays for that is stated rather than buried: time
  the device spends switched OFF no longer refills the XP budget, so the meter means "per day
  of device-ON time" — smaller, one-directional, and the only direction that cannot be typed.
  The sentence in `0.4.0-battle` below about a device that "was off for an hour comes back
  full" is superseded by this, and `data/balance.h` says so at the constant.
- **P6-C3's ONE LOAD-BEARING LINE HAD NO GUARD.** `gt_mono_ms()`'s device branch
  (`esp_rtc_get_time_us()`) is the whole of "a monotonic source that survives sleep";
  reverting it to `millis()` is invisible to the host suite, because no host binary compiles
  that branch, and `tools/check.sh` gated the CALLER while nothing gated the line. It has its
  own gate now, mutation-proven both ways.

Measured at the exit, each figure re-derived by running the command rather than copied
forward:

- **Firmware:** baseline **1,948,802 B of flash and 73,580 B of static RAM**, up
  **+20,866 / +400** on `0.5.0-explore`. All seven `build_matrix.sh` variants at 0 project
  warnings; the release build (`GOD_MODE_ENABLED=0 FEATURE_BLE=0`, decision D2) is
  **1,224,210 / 49,924** — **76.5 %** of `GATE_RELEASE_FLASH_MAX` and **76.8 %** of
  `GATE_RELEASE_GLOBALS_MAX`, and **38.9 %** of the 3,145,728 B `app0` slot.
- **The phase is 174 % of the top of its own flash line and comfortably inside the scarce
  one.** `docs/budget.md` allows P6 8–12 KB of flash and 0.3–0.8 KB of globals; actual
  **+20,866 flash** and **+400 globals**. Most of the overrun is not Pebblebol's code: two
  ESP-IDF drivers arrive in the tree for the first time — LEDC with the first PWM output
  (~7.5 KB, P6-C1) and `esp_sleep` with the first `esp_light_sleep_start()` (~9.6 KB,
  P6-C3) — and both are paid once and reused. It is immaterial against the cap that matters:
  **375,790 B of release flash remain** against an 80–115 KB forecast for P7–P10.
- **Per commit**, each delta the difference of two adjacent commits' own recorded
  `flash=`/`globals=` lines, and the four sum to the phase:

  | commit | chunk | flash | globals | after (baseline) |
  |---|---|---|---|---|
  | `72263f9` | P6-C1 tone engine + motion capability | **+9,028** | **+136** | 1,936,964 / 73,316 |
  | `e46416f` | P6-C2 activity score + rewards | **+1,940** | **+72** | 1,938,904 / 73,388 |
  | `e8701ea` | P6-C3 power ladder + sleep-correct clock | **+9,698** | **+192** | 1,948,602 / 73,580 |
  | this commit | P6-C4 exit (three fixes, one gate, documents) | **+200** | **0** | 1,948,802 / 73,580 |
  | | **phase 6** | **+20,866** | **+400** | |

- **Where the 400 B of globals went, and most of it is not ours.** P6-C1's 136 B is 22 B of
  tone-engine state and 114 B of LEDC driver statics and HAL assert strings; P6-C2's 72 B is
  `game/activity.cpp`'s entire per-boot half (the ten-entry network set 40, the four-entry
  peer set 16, six counters, a seconds remainder, the pending gain, the dirty flag) and its
  PERSISTED half costs **0**, because `act_day` and `act_score` are four bytes that were
  already `CooldownTable.reserved_a[4]`; P6-C3's 192 B is ~148 B of ESP-IDF sleep state
  against ~44 B of ladder, DIAG counters and idle clock. **No schema bump, no migration, no
  fixture** — the activity day was carved out of reserved bytes exactly as
  `corrupt_until_epoch` was at P5-C3, and the blob is still 272 B with every row where it was.
- **Host suite:** 37 → **41 binaries, 708 → 787 tests, 1,802,703 → 3,658,841 checks.** Four
  new binaries (`test_audio`, `test_motion`, `test_activity`, `test_power`) and six existing
  ones changed. Screen goldens unchanged at **60**; `CONTENT_VERSION` unchanged at
  **0x02B5** — phase 6 added no content, and `ENC_RARE_BONUS_MAX_PM` lives in
  `data/balance.h` because two modules read it, not because the pack grew.
- **`tools/check.sh` gained five gates this phase**, each proven to bite by planting the
  thing it forbids: no `#define PIN_` outside `core/config.h` (while D1 and D8 are open the
  pin map has one home); `act_take_gain()` may be called only in `app/app.cpp` (the activity
  reward is paid in exactly one place); `hardware/power.*` may not name the radio API (the
  ladder releases the radio by NAVIGATING, so the owning screen's `leave()` hook cancels
  through `wifi_scan_cancel()`); `ui_explore_clock()` must hand out `gt_mono32()` and not
  `millis()`; and `gt_mono_ms()`'s device branch must read `esp_rtc_get_time_us()` and must
  not read `millis()`.

**Two decisions are still open and phase 6 did not close either.** **D1** (pin map) stays
deferred: `PB_PINS_CONFIRMED` is not defined anywhere, the five `#define`s are byte-for-byte
what the repository carried, and the consequence this phase paid for it is a light sleep
instead of a deep one — worth ~2.7 days of ~26 on D11's own battery model, against D11's own
unmeasured 100× spread, which swings the same answer from 26 days to 9. **D8** (piezo GPIO)
stays open: `PIN_PIEZO 3` is a proposal, the tone engine names the macro and never a number,
and the owner should know before soldering that GPIO3 is one of the six pads that could wake
this chip from deep sleep.

**Nothing in this phase has run on hardware, and that is a larger caveat here than in any
earlier one.** No piezo has been soldered and no tone has been heard. No board has slept, so
whether `esp_light_sleep_start()` really returns on a `GPIO_INTR_LOW_LEVEL` from GPIO10 and
GPIO2 with the buttons' internal pull-ups is unverified, and every current figure in the
tree is arithmetic on D11's published points or a vendor number. §67's `Device sleeps
correctly` is held open for exactly that reason, in the same words `Wi-Fi shuts down after
use` has been held open since phase 5: a device sleeping is an observation, not a property.
`Time-based calculations work across reboot` and `Timed systems work after sleep` are ticked,
on the host standard every other ticked box uses.

## [0.5.0-explore] — Unreleased

Phase 5 gives the pet a world outside itself, and the first thing it did was take a
capability away. Wi-Fi stopped being a thing the device joins and became a thing it
listens to: one passive scan, every access point turned into a salted 32-bit hash and one
of six abstract categories on the device, the radio off again. No name and no hardware
address survives the function that reads them, and the code that could have associated to
a network is deleted rather than disabled, so §44's promise is a property of the tree and
not of a setting. On top of that sensor sit an exploration loop — a NETWORK screen behind
a twelve-second timeout with a cancel, a two-hour per-network cooldown that no single
calibration can unlock wholesale (an uncalibrated device can still farm by rebooting, and
the header says so), a seeded encounter, a capture that files a validator-clean Pebble
through the tree's one constructor, and a bag the CARE screen can spend. There is still no
radio in the sense that matters: none of this has run on a board.

Measured at the exit, each figure re-derived by running the command rather than copied
forward:

- **Firmware:** baseline **1,927,936 B of flash and 73,180 B of static RAM**, up
  **+12,282 / +504** on `0.4.0-battle`. All seven `build_matrix.sh` variants at 0 project
  warnings; the release build (`GOD_MODE_ENABLED=0 FEATURE_BLE=0`, decision D2) is
  **1,203,808 / 49,508** — **75.2 %** of `GATE_RELEASE_FLASH_MAX` and **76.2 %** of
  `GATE_RELEASE_GLOBALS_MAX`, the caps `build_matrix.sh` has enforced since `f496a3a`.
- **The phase cost 504 B of globals against a budget line of 1.0–2.0 KB**, and the two
  halves pull opposite ways: P5-C1/C2 is a **rebate** of −1,572 / −112 (a deletion bigger
  than what replaced it), P5-C3/C4 is **+13,854 / +616**. Six objects hold the whole +616
  and `riscv32-esp-elf-nm` names them: the cooldown RAM table 257, the CARE bag's row cache
  178, the NETWORK screen 143 (136 of it one `WifiScanJob` buffer), the ENCOUNTER screen
  19, `net.cpp`'s scan salt and intent byte 5, the armed battle modifier 4 — 606 of symbols
  and 10 of link alignment. `game/encounters.cpp`, `game/capture.cpp` and
  `game/corruption.cpp` hold **zero**: they are pure functions over caller state.
- **Host suite:** 31 → **37 binaries, 603 → 708 tests, 728,779 → 1,802,703 checks.** Six
  new binaries (`test_exploration_hash`, `test_cooldowns`, `test_encounters`,
  `test_capture`, `test_box_full_capture`, `test_inventory`) and four existing ones changed.
  Screen goldens 55 → **60**: six added, one deleted with the branch it froze, one renamed.
- **Content pack:** 8 → **9 generated files**, `CONTENT_VERSION` **0x5B4A → 0x02B5**,
  `tools/content/verify.py` **99 → 127 checks, 0 FAILED**. `tools/check.sh` now proves
  **20** tuning constants agree between `src/data/balance.h` and `tools/content/balance.json`
  (13 before), and gained three gates: no association call under `src/`, no network
  identifier named in `wifi_scanner.h`, and every write to `ScanResult`'s padding pair a
  literal zero.

**Two defects in this phase's own work were found by running something rather than reading
it, and both are recorded where they happened**: an encounter picker whose second draw was
conditioned on its first, which made one SPECIAL event unreachable in the only two
categories that carry it (measured over 8,000 scans per category), and an eviction test
that passed a mutant because the row it armed first was also the row that expired first.


### P5-C1 scan-only Wi-Fi · P5-C2 cooldowns

**The device stopped being able to join a Wi-Fi network, and that is the feature.** Wi-Fi is
now a sensor: it listens passively for the networks around it, turns each one into a salted
32-bit hash and one of six abstract categories, and switches off again. It never associates
to anything — not because a setting says so, but because the code that could is gone.

- **The station path is deleted, not disabled.** `sta_start()`, `sta_failed()`,
  `refresh_sta_ip()`, the retry backoff, the link-loss re-association, `net_rssi()`,
  `net_is_sta_up()`, `net_set_credentials()`, the three `NPH_STA_*` phases, the creator
  screen's "joined your network" branch and its frozen golden. `CFG_WIFI_SSID` and
  `CFG_WIFI_PASS` are out of the user configuration block — there is nothing to fill in.
  Until now this was a promise held up by an empty string: the credentials were always `""`,
  so the association line was already unreachable. `tools/check.sh`'s dormant `WiFi.begin(`
  gate is now armed and counts them at zero.
- **`Config.wifi_ssid` and `Config.wifi_pass` stay, empty, on purpose.** 98 bytes of frozen
  padding: their offsets are `static_assert`ed and pinned by `tests/fixtures/config_v1.bin`,
  so removing them would move five fields and stop a v1 save loading.
- **`networking/wifi_scanner.{h,cpp}`** — one scan as a caller-owned state machine with a
  12-second software timeout and a cancel. The Arduino core's own scan timeout is 60 s and
  its `scanComplete()` cannot tell "timed out" from "never triggered", so that clock is
  load-bearing. The radio is released exactly once per run on every exit path.
- **`networking/net_classify.{h,cpp}` + `tools/content/networks.json`** — the classifier the
  plan had promised and never specified. Auth mode, the hidden flag, a clamped RSSI and a
  three-bit token-class mask go in; one of `UNKNOWN / HOME / PUBLIC / BUSINESS / OPEN /
  HIDDEN` comes out. The thresholds and the 88 tokens are content, generated into
  `src/data/network_table.h` and folded into `CONTENT_VERSION` (`0x5B4A` → `0x54BD`) —
  because a threshold decides a category and a category indexes the encounter table.
- **Privacy (spec §44) is structural.** `ScanResult` is 8 bytes: a salted hash, a signal
  strength, a category and two zero bytes. A network's name and hardware address exist only
  as locals inside one loop; the hash is `fnv1a32(address ‖ fnv1a32("pbl-scan" ‖ device_id))`,
  with zero folded away because zero means "empty row" in the cooldown table. The test pins
  `sizeof` **and** every member's offset, and a gate greps the header for both words —
  because `sizeof == 8` alone cannot fail when two reserved bytes become two bytes of a name,
  which was measured rather than assumed.
- **`game/cooldowns.{h,cpp}`** — two hours per network, absolute deadlines, LRU over 32 rows,
  persisted in the `cd` pair. A clock that rolls backwards can only lengthen a wait. While
  the clock is untrustworthy a **separate per-boot table** is used in both directions, on the
  monotonic millisecond clock, and a calibration landing mid-session promotes its live rows
  instead of stepping past them: without that, one keystroke on the time screen freed all
  thirty-two networks at once. What it does not close — a reboot clears the fallback — is
  written into the header, not left to be discovered.

Measured, and both figures are **rebates**: baseline **1,915,654 / 72,676 → 1,914,082 /
72,564** (−1,572 flash, −112 globals); `release` **1,191,426 / 49,004 → 1,190,000 / 48,916**.
All seven matrix variants moved the same way. Nothing calls the scanner yet — the NETWORK
screen is P5-C3 — so `--gc-sections` drops it from the image those figures measure; a probe
build that wires it says P5-C3 inherits **+3,840 flash and +408 globals**. Host suite
**31 → 33 binaries, 603 → 643 tests, 728,779 → 798,806 checks** — `test_exploration_hash`
(24 cases) and `test_cooldowns` (17), less `snapshot_creator_station`, whose subject was deleted.

`tests/golden/battle_v1.txt` was re-recorded: `battle_hash_basis()` mixes `CONTENT_VERSION`,
so a pack change moves every round hash by design. **Every event line in the transcript is
byte-identical** — only the version-stamped hashes and the header's `content=` field moved,
which was checked by diffing the file with the hash fields masked out.

### P5-C3 encounters · P5-C4 capture, inventory and items

**Exploring is a loop now.** The MENU's RED row opens a real screen: it asks the radio for one
passive scan, spins for at most twelve seconds, takes B as a cancel, and on an answer picks the
first access point that is off cooldown, arms that cooldown and rolls an encounter against the
generated tables. A wild Pebble can be caught into the Box, an item goes into a bag the CARE
screen can spend, and a special event pays experience or corrupts the creature for a day.

**The roll is a pure function of what §20 says it should be** — the salted network hash, the
six-hour bucket, the per-device seed, the category, the signal and how many Pebbles are filed —
and draws from no global stream. That is a **deviation from the plan's `RNG_ENCOUNTER`**, with a
reason: a shared stream would make two scans of the same network in the same bucket answer
differently, which is the opposite of §20's "deterministic from a seed", and would make the
stream position part of the answer so no host test could pin one. `RNG_ENCOUNTER` is where the
CAPTURE roll comes from, which is the draw that must be real.

**A defect in that seeding was found by a distribution test and fixed.** The first version took
successive steps of one xorshift32 — pick the outcome, then pick the payload. The SPECIAL branch
is only reached when the first draw lands in a 4-to-10 wide band mod 100, and the second draw is
a deterministic function of the first, so conditioning on that band left it badly structured.
Measured over 8,000 scans per category: HIDDEN's four events, weighted 20/30/20/30, came out
53/32/**0**/15 per cent — one event was **unreachable** in two of the six categories, and the
membership-only test passed anyway because the commonest event is in every category's list.
Every stage now derives its own seed from the encounter seed and a one-byte tag.

**THE FIVE CARRIED-FORWARD DEBTS, ALL DISCHARGED**

- **SPECIAL had no payload table.** `tools/content/specials.json` is one now — an event roster
  and per-category weights summing to 100, shaped exactly like `ITEM_DROPS` so both two-stage
  picks are the same walk, with three generated guards. Exactly the two kinds the plan promised:
  an XP burst and a corruption event. **The 24-hour timer had nowhere to live**: `PebbleInstance`
  carried the status bit and no deadline, so corruption could be set and never expire. Four of
  the twelve reserved bytes are `corrupt_until_epoch` — no migration (nothing in the tree set the
  bit before this commit), no wire change (`reserved[12]` is not in the 48-byte record and
  `PBW_STATUS_MASK` refuses the bit outright) and no new VReject. The tripwire that pinned
  `CONTENT_VERSION` **fired as designed and is deleted rather than re-derived**: it guarded a hole
  that no longer exists, and what replaces it asks the same question of the thing that does.
- **The ITEM outcome had no resolvability guard.** `encounter_item_rows_have_a_drop()` is emitted
  beside its WILD twin. The bullet's claim was half wrong and the correction is recorded: the
  pack's own `verify.py` has checked this since P4-C1 and is in the gate — what was missing was
  the firmware half, which is not skippable when python3 is. **And the host case that named the
  property could not fail on it (instance eighteen)**: measured, it stayed green under the exact
  mutation the plan asked for.
- **Item 9 does something.** It gets `ITEM_KLASS_EVOLUTION`, the class §24 lacks — it is what the
  item is, the `EVOC_ITEM` machinery was already built and already tested, and §24 says "initial"
  classes. At this roster no shipped rule spends it, so `inv_use()` offers it to the rules and
  **does not consume it** when none bites: a key that vanished into a lock that does not exist is
  one step worse than the hole.
- **The two item units name a target.** `items.json` gains `target`, `duration` and `clears`;
  `ItemDef` spends its two `reserved` bytes on `target` and `param` **without growing**; and a
  third unit nobody had named — the CAPTURE bonus, which the pack stated three incompatible ways —
  is settled as a number (`ITEM_CAPTURE_SCALE`), so prose can no longer disagree with prose.
- **A captured Pebble passes the validator**, and the shape of that answer was measured first:
  across 36 species × 30 levels, a sealed genome gives **0 of 1,080** rejects and an unsealed one
  gives `VR_BAD_GENOME` on **all 1,080**. The seal is the only input that can make a constructed
  Pebble invalid, so capture refuses it by name before it builds anything and validates the filed
  slot as a post-condition. The Pebble is **not destroyed** on that path, and that is a decision:
  `box_new_pebble()` files as it constructs, the first Pebble in an empty Box becomes the active
  one, and `box_release()` refuses the active slot — so "file, validate, undo" would have an
  unreachable branch in exactly the case a first-boot player hits.

**Measured.** Baseline **1,914,082 / 72,564 → 1,927,936 / 73,180** (+13,854 flash, +616 globals);
`release` **1,190,000 / 48,916 → 1,203,808 / 49,508**. The +616 is the +408 the P5-C1 probe
predicted for calling the scanner and the cooldown table at all, plus 208 the two new screens, the
CARE bag mode and the inventory's armed modifier hold in their own right. **Phase 5 has spent 504
of its 1–2 KB globals line, net of P5-C1's rebate** — this entry first said **328**, which is
not 616 − 112 and is not any other pair of figures in the tree; the exit re-derived it and
attributed all 616 symbol by symbol (`riscv32-esp-elf-nm`, `docs/budget.md` §3). Host suite
**33 → 37 binaries, 643 → 708 tests, 798,806 → 1,802,703 checks**.

`tests/golden/battle_v1.txt` was re-recorded again, for the same reason and with the same check:
the pack changed, `battle_hash_basis()` mixes `CONTENT_VERSION`, and diffing the transcript with
the hash fields masked out is empty — every event line is byte-identical. `care_list.pbm` and
`care_list_back.pbm` moved because the CARE list gained a row.

### Fixed (P5-C5 — what three hostile verifiers found in the exit tree)

- **`docs/budget.md`'s own ledger did not add up, and it was the sentence offered as
  evidence for the phases 6–10 projection.** The 136 B `WifiScanJob` was attributed twice —
  once inside the +408 the P5-C1 probe predicted, once inside the +208 the paragraph two
  below it gave for the new screens — while the same document says 408 + 208 = 616. Which
  of the three paragraphs was wrong was **measured, twice and independently**: a build with
  `WIFI_SCAN_MAX_RESULTS=8` moves globals by exactly **−64 B**, so exactly one job of 136 B
  is linked and it is counted once; and `riscv32-esp-elf-nm` attributes all 616 B symbol by
  symbol, agreeing to the byte. The three new surfaces hold **344 B**, not 208 — 136 of it
  a buffer rather than screen state. The totals, the caps and the phase-5 figure are
  unchanged; the arithmetic under them now closes.
- **A leak that needed no rename had no gate at all.** `ScanResult`'s two padding bytes were
  guarded by an offset test and by a grep for the words a network is named by — between them
  a *rename*, from two directions. Neither can see a *value*: assigning those bytes two bytes
  of a beacon name inside `net.cpp`'s read path left **`GATE OK` and `ALL PASS 37/37`**,
  because `net.cpp` is on the impure list and `tests/Makefile` never compiles it. `check.sh`
  gate 3 requires every assignment to that pair under `src/networking` to be a literal zero;
  the same planted leak now fails the gate by name. Measured on this tree, both ways.
- **`tools/build_matrix.sh` built the release variant twice** and said in a comment that this
  "costs nothing but the comparison". It cost one whole compile: the seven-variant matrix was
  eight builds. It is seven again, and the whole run is **8 m 11 s** wall on this machine.
- **And the cap block's own failure message could never fire.** Found while re-testing the
  block that change touches, against a stub builder that fails the release variant: under
  `set -euo pipefail` a `grep` that matches nothing fails, the command substitution around it
  fails with it, and the script **dies on the assignment** — so `MATRIX FAIL: could not read
  the release build's size line` was unreachable and a failed release build ended the matrix
  in silence with exit 1 and no named reason. **Pre-existing since `f496a3a`**, reproduced on
  the original file before the fix, and it is the same shape as the dormant `WiFi.begin(` gate
  P5-C1 armed — a gate that aborts the run instead of reporting. The house `|| true` form makes
  the branch reachable; all four paths were then driven with the stub (pass, flash cap
  breached, globals cap breached, release build failing) and each now ends with a named line. The cap check now reads the size line the loop already printed, which is also
  the stricter reading — the capped figures cannot be a different compile's.
- **Sentences narrowed to what the tree does.** `net.h` claimed a gate "counts them and fails
  the build at anything but zero"; that gate is one grep for one spelling of one call, two
  ways past it were tried on this tree, and what makes "never joins a network" structural is
  the deletion, not the grep — the banner now says so. `wifi_scanner.h` claimed its two layers
  covered the struct; they cover a rename, gate 3 covers the value, and the note that said the
  offset case would *pass* a renamed member with eight green checks is corrected against a re-run:
  the header-only swap **does not compile** (the test names the member at five sites), the swap
  applied everywhere passes **24/24, 69,702 checks** — so `sizeof` and `offsetof` really cannot
  tell one two-byte member from another, which is the argument's content — and the grep gate bites.
  The mechanism was the compiler, not the assertion. The plan's P5-C1 line still quoted
  `CONTENT_VERSION 0x54BD`, three content commits stale. **And this entry corrected itself
  twice**: it said phase 5 had spent **328** globals (it is **504**, and 328 is not the
  difference of any two figures in the tree), and its own opening called the cooldown one
  "an uncalibrated clock cannot farm" — it stops one calibration freeing thirty-two rows and
  it does not stop a reboot, which `cooldowns.h` has said all along. Commit title `84a8dae`
  carries the same overstatement and is history; `docs/decisions.md` records that.

### Not verified

**This firmware has still never run on a physical board.** No ESP32-C3, no panel, no cells,
and — the one that matters most for this phase — **no radio has ever scanned anything.**
Every scan in every test came from a fake `WifiScanDriver` behind the four-function seam.
`§67 "Wi-Fi scanning works"` and `"Wi-Fi shuts down after use"` are therefore left **unticked**:
the code path is real and reachable (`--gc-sections` no longer drops the scanner, because the
NETWORK screen calls it), the release-once-per-run property has six host cases, and neither
fact is an observation on hardware. The P5-C5 bench line — NETWORK → scan → encounter within
≈ 5 s, radio off afterwards, a cooldown surviving a power cycle — is **not done** and stays
open in the plan.

**The exploration loop has three known holes, all of them written into headers rather than
left to be discovered.** An access point that rotates its hardware address reads as a new
network on every scan and no cooldown can close that. An uncalibrated device can still farm
by rebooting: with no trustworthy timestamp there is nothing to persist, so the per-boot RAM
table trades a catastrophic farm (one calibration frees all 32) for a linear one. And the
evolution key (item 9) has its class, its consumer arm and no lock at this roster — it is
offered to the rules and kept when none bites; the rule that would spend it is species 53,
past the 36-species prefix, and lands in P9.

**One mutant survives and is reported rather than hidden.** Deleting `cap_attempt()`'s
`validate_pebble()` post-condition leaves every host case green: with a sealed genome the
validator answers `VR_OK` on all 1,080 roster × level rows, so the post-condition has no
reachable falsifier at this roster. The *pre*-check is where the requirement bites and it is
covered — dropping it turns all 1,080 rows red by name.

## [0.4.0-battle] — Unreleased

Phase 4 gives the pet something to do with the stats phase 3 gave it. The content stopped
being a placeholder and became a generated pack — 36 species in 12 families of three, 34
attacks, a three-type chart, 24 evolution rules and 10 items, all authored as JSON and
regenerated into `src/data/*_table.h` by a script with its own gate. The battle engine is
spec §14's nine steps as nine functions over a 212-byte state with no heap, no float and no
clock in it, so a battle is a pure function of its seed and its actions and can be replayed
and hashed. The AI plays it, the device plays it, and the species finally chooses the body:
before this phase all 36 species wore one of eight genome bodies and an evolution moved
nothing on screen. And the whole §15 exchange — one validator, a 14-byte frame, twelve
message types, a nine-state session and a lockstep battle — is proven end to end over a
loopback that drops, duplicates and reorders frames. There is still no radio; that is P7.

Measured at the exit, each figure re-derived by running the command rather than copied
forward:

- **Firmware:** baseline **1,915,654 B of flash and 72,676 B of static RAM**, up
  **22,582 / 2,328** on `0.3.0-pet`. All seven `build_matrix.sh` variants at 0 project
  warnings; the release build (`GOD_MODE_ENABLED=0 FEATURE_BLE=0`, decision D2) is
  **1,191,426 / 49,004**, 37.9 % of the `app0` slot. Almost the whole bill is one chunk:
  P4-C4, the battle screen, is +18,780 B of flash and +2,304 B of globals, and it is all
  file-scope statics, which is the same fact as "no per-frame heap".
- **Host suite:** 21 → **31 binaries, 328 → 603 tests, 350,214 → 728,779 checks.** Screen
  goldens 48 → 55, plus `tests/golden/battle_v1.txt`, a 22-round transcript.
- **The lossy-link acceptance run:** **3,000 trials over six fault arms, 2,971 completed and
  agreed, 0 silent divergences, and no trial left either Box changed.** The **clean arm and
  the four 10 % arms** complete 500 of 500. This entry first said "the five 10 % arms", which
  counted the UNFAULTED arm as a fault arm and inflated the run's fault coverage by a whole
  arm; there are four (drop, duplicate, reorder w4, and all three together), each at 100 ‰.
  The harsh arm (30 % drop, **10 % duplicate**, 20 % reorder — `hard.dup_permille = 100u`
  in `the_acceptance_run_completes_or_aborts_by_name_and_never_diverges_in_silence`; the
  printed arm label names only two of its three faults) completes 471, loses 28 by name
  and half-pays 1 — which is what makes "or aborts cleanly" a measured half of the promise
  rather than a decoration.
  The Box assertion is a **guard, not a discovery**: `boxes_untouched()` memcmps both
  endpoints' Boxes across every trial, and at this commit nothing under `app/`, `ui/` or
  `persistence/` links the session module at all, so there is no code path that could have
  written one. It is waiting for P7.
- **Sanitizers:** ALL PASS 31/31 under `-fsanitize=address,undefined
  -fno-sanitize-recover=all`, zero reports, with exactly one line neutralised for a
  pre-existing GCC 13.3 `constexpr` fold.

**No win-rate percentage appears anywhere in this entry, on purpose.** The only ones that
exist were measured on `tools/content/sim_engine.py`, a Python model that diverges from the
shipped engine in five ways `game/battle.h` names and that has no AI at all. P9-C4 produces
real ones.

Every commit in this range passes the gate: firmware compile with `--warnings all` at 0
project warnings, size caps, and `make -C tests check`.

### Added

- **The BATTLE screen** (`ui/screen_battle.{h,cpp}`, `ui/battle_renderer.{h,cpp}`): the only
  place on the device where the engine and the AI actually run. Six modes on one screen —
  the Box team pick, the stare-down, a four-attack + `CAMBIAR` menu on the shared list
  widget, the bench list, the round's transcript played back one event at a time, and the
  result — with `SF_STICKY | SF_OWNS_BACK`, so B walks that ladder one level at a time and
  invariant 3 cannot drop a player out of a fight. Two 24x24 creatures face each other,
  each drawn from the SAME `ui/pet_art.h` resolution HOME and the BOX use, so the creature
  in a battle is the creature that has been cared for. **THE RENDERER IS A PURE
  TRANSLATION UNIT**, against the plan's own wording: three of the five calls the plan
  named have a `gfx.h` face that forwards to exactly them on the device, and the two that
  do not (`rd_flash`, `rd_shake`) draw no pixel and reach `render.h` through one-line
  seams — which is what lets `tests/test_screens.cpp` render a REAL battle at the real
  128x64 and diff six goldens, where a `render.h` module could not have been snapshotted
  at all. **NO PER-FRAME HEAP IS STRUCTURAL**: the `BattleSetup`, the `BattleState`, the
  `BattleAi`, the 48-entry event ring and every string buffer are file-scope statics, so
  there is nothing left to allocate.
- **`ui/xbm_mirror.{h,cpp}`**: `ui/petfx.cpp`'s horizontal XBM flip and its 256-byte
  bit-reversal table, lifted out of that device-only file so the battle renderer can face
  a combatant the other way without a second copy of a routine whose whole difficulty is
  one off-by-four on a 28 px sprite. **It is executed for the first time**: `petfx.cpp`
  said the flip was verified by `scratchpad/petfx/mkharness.py`, and that harness is not
  in this repository, so nothing in the tree had ever run it. The banner now says which
  half of that sentence was true.
- **MENU → PLAY → "COMBATE DE PRÁCTICA"** and the god console's **`test_battle`** row
  (spec §49) — the only two single-device battle entries in V1, because there are no wild
  battles (§68 r18). They differ in the three things that matter: the practice entry draws
  a fresh `RNG_BATTLE` seed, uses the player's Box and pays `XP_BATTLE_WIN`; the
  diagnostic pins `BT_DIAG_SEED`, builds a synthetic team so it runs on a device that has
  never filled its Box, and pays nothing.
- **A per-frame heap probe in DIAG** (`god_frame_heap_moves()` / `god_frame_heap_worst()`,
  drawn on the SYS/HEAP page as `dF n/worst`). Sampled inside `god_draw_marker()`, which is
  the last thing every DRAWN frame does and a no-op while god mode is off — so it measures
  frames rather than loops and costs nothing in the shipped configuration.

### Added (P4-C5a)

- **`game/validate.{h,cpp}` — THE ONE PEBBLE VALIDATOR** (spec §15, P4-C5a). Spec §15's
  second governing sentence is "the same validator used for custom Pebbles should be used
  for exchanged Pebbles", and this is it: a `const PebbleInstance&` in, one of twenty-seven
  named `VReject` codes out, no policy flag and no scope parameter. A validator that
  cannot write cannot repair, and a repairing validator is the exact failure §15's first
  sentence is written against — `ui/screen_battle.cpp:205 copy_from_box()` repairs, and
  that is defensible for a Pebble the device made and not for one a peer sent. Every §15
  reject reason has a live code: impossible stats (`VR_HP_OVER_MAX` against the DERIVED
  maximum, through the same `pebble_derive_stats()` call `battle.cpp` makes), illegal moves
  (`VR_UNKNOWN_MOVE` and `VR_UNLEARNABLE_MOVESET`), impossible levels (`VR_BAD_LEVEL`),
  invalid species (`VR_UNKNOWN_SPECIES`), invalid evolution state (`VR_BAD_EVO_STAGE`,
  `VR_BAD_EVO_PENDING`) and the rest.
- **`networking/protocol.{h,cpp}` — THE §15 FRAME AND THE 48 B WIRE PEBBLE.** A 14-byte
  header carrying all six things §15 requires of every packet (version, type, session id,
  payload length, sequence number, and a trailing CRC-16/CCITT over the whole frame INCLUDING
  the header), the twelve §15 message types at twelve FIXED payload lengths, and a decode
  that is total in eleven pinned steps. `PROTO_PAYLOAD_MAX` is **derived** as
  `max(PROTO_LEN_OF[])` = 148 rather than chosen, so the cap can never be looser than the
  protocol needs, and `PROTO_HDR_BYTES + PROTO_PAYLOAD_MAX + 2 <= 250` is asserted against
  ESP-NOW's datagram. Every multi-byte field is read and written **byte by byte with
  explicit shifts** — never a struct memcpy — and one golden frame plus one golden 48 B
  record are pinned byte for byte, because a grep cannot express that rule and a
  memory-image codec would make the host test prove nothing about the device.
- **`tests/test_validate.cpp` (25 cases, 3,566 checks) and `tests/test_protocol.cpp`
  (25 cases, 234,356 checks)**, plus a `networking/` pattern rule in `tests/Makefile` —
  until now `grep -c networking
  tests/Makefile` was **0** and nothing under `src/networking` could link on the host at
  all. Totality is swept rather than sampled: every truncation length of every type, every
  single-bit flip at every bit position of every type (3,680 flips), every truncation
  again out of a heap block sized to EXACTLY the delivered byte count so ASan's redzone
  sits on the first illegal read, and a 20,000-iteration structured fuzzer that applies
  exactly one of eleven mutations to a frame built from one printable `u32`.
- **Three networking gates in `tools/check.sh`**, all scoped BY FILENAME because
  `src/networking/` is a mixed directory (`ble_social.cpp:45` legitimately includes
  `Arduino.h`): the pure modules include no radio, hardware or renderer header; they hold
  no file-scope mutable state, which is what will let one host process run two endpoints;
  and the word `repair` may not appear under `src/networking` — a labelled tripwire that
  matches zero lines today and exists to fail the day the wire path learns to mend what a
  peer sent.

### Added (P4-C5b)

- **`networking/session.{h,cpp}` — THE §15 SESSION FSM.** Nine states
  (`SS_IDLE`..`SS_CLOSED`), one terminal, and a fixed **40 B `SessionEnd`** written on the
  way into it that makes every terminal answerable without a rerun: both hashes side by
  side for a desync, the obligation and the round for a loss, a `VReject` and a member
  index for a refusal, and six named counters (`rx_ok`, `rx_dup`, `rx_stale`, `rx_gap`,
  `rx_wrong_state`, `rx_reject`). Four rules run before the table and are what keep it
  small: a frame the codec refused never reaches the FSM **and never touches a timer**; a
  frame for another session is dropped by `proto_decode()` itself; a legal message that is
  unexpected here is dropped and counted; and **only progress touches the ladder**, so a
  duplicate, a re-acknowledgement, a stale frame and a wrong-state frame all leave it
  where it was. Roles come from the device id, never from who spoke first, and the shared
  seed is mixed from **both** nonces and **both** team CRCs so neither side fixes it alone.
- **`networking/battle_link.cpp` — THE LOCKSTEP.** Both sides submit an ACTION carrying the
  round's `open_hash`, both resolve locally through the one engine, and both exchange
  `ROUND_RESULT{hash_before, hash_after}`; a mismatch is `BATTLE_END(SE_DESYNC)` and
  **pays nobody on either side**. It holds no `BattleState` and no `BattleSetup` of its
  own — both are caller-owned and the peer's team is decoded straight into
  `setup->member[peer_side][]` — and **the local team goes through the same
  `pbw_decode()` the peer's does**, so the two endpoints' 780 B setups are byte-identical.
  Three hashes answer three different questions (we entered the round disagreeing; we
  disagree about the action pair; we disagree about the rules) and the banner says plainly
  that none of them catches a liar: a forged `ROUND_RESULT` is exactly indistinguishable
  from a genuine divergence, and the protocol's whole answer is that neither side is paid.
- **`networking/transport.h` + `transport_loopback.cpp` — THE SEAM AND THE FAULT
  INJECTOR.** A struct of function pointers from a factory, so the transport is a runtime
  value and **there is no `#ifdef` anywhere in the session logic**; `mtu` is a field rather
  than a `#define`, so a test moves it to 60 and drives the oversize path without
  recompiling; and a refused send is indistinguishable from a dropped frame, which is what
  makes an injected loopback drop and a real radio failure exercise the identical path.
  The loopback is two in-process queues with configurable drop / duplicate / reorder-window
  percentages and a scripted "kill the next N frames of this named type" fault, all drawn
  from **one seeded `Rng`**, so a failing acceptance trial reproduces from its printed seed.
- **`tests/test_session.cpp` (41 cases, 2,493 checks) and `docs/protocol.md`.** The
  acceptance census runs 500 trials per arm over six fault models and asserts that **no
  trial ever diverges in silence** — 0 in 3,000 — while requiring the clean arm to complete
  every trial and the harsh arm to show **both** a completion and a clean abort, because a
  run in which everything aborted immediately would satisfy the promise and prove nothing.
  `docs/protocol.md` is the wire format, the state table, the three invariants and
  nineteen honest LIMITS.

### Fixed (P4-C5b — found by the fault runs, in the design and in the protocol)

- **A retransmitted ACTION carried the CURRENT round's open hash** instead of the hash of
  its own round: **263 `SD_OPEN_HASH` desyncs in 500 trials at 10 % drop, on a link that
  never corrupted a byte.**
- **A `ROUND_RESULT` that arrived before we had resolved that round was recorded and never
  matched**, so the agreement barrier only ever looked forward: a link that dropped nothing
  and **only reordered** ended **472 of 500** trials in `SE_LOST`.
- **A message from an earlier PHASE was dropped as out-of-state**, so a `CAPABILITIES` that
  overtook its `HELLO` stranded an honest pair. The design gave the answer-from-state rule
  to battle rounds only; the handshake needs it too. All three are now zero.
- **A peer that echoed one of our own Pebble ids reached `battle_init()`** and came back as
  `BR_DUPLICATE_ID` — which this device reports as an INTERNAL fault, because the engine's
  duplicate rule spans all six members and `validate_team()` is about one team by
  construction. It was this device blaming itself for a lie the peer told; there is now a
  cross-team id check that answers `VR_DUPLICATE_ID` and names the peer.
- **One injected frame could deafen an endpoint permanently.** The sequence window slid
  forward to whatever seq it was shown, so a forged frame carrying seq 0x7000 pushed it
  past every number the honest peer would ever send and every real frame afterwards was
  dropped as stale. `SESSION_SEQ_MAX_JUMP` refuses a jump no honest peer can make
  **without moving the window**.
- **Agreeing is not leaving.** Closing as soon as both final hashes matched paid one side
  and not the other in **88 of 500** trials at 10 % drop and **18 of 500 on a link that
  only reordered**. The endpoint now stays in `SS_ENDING` until the peer's GOODBYE, so the
  two-generals asymmetry needs nine consecutive losses in one direction: **0 in 3,000**.

### Fixed (P4-C5 follow-up — six hostile verifiers, and what they found)

- **`hp_cur == 0` WAS THE LAST THING THREE CHECKERS ACCEPTED AND THE ENGINE REFUSED, AND
  THE HONEST DEVICE BLAMED ITSELF FOR IT.** A fainted Pebble is a legal thing to have
  *stored* and an illegal thing to bring to a battle, so `validate_pebble()`,
  `validate_team()` and `pbw_decode()` all answered `VR_OK` for one and `battle_init()`
  answered `BR_MEMBER_FAINTED` — which `battle_link.cpp` turned into
  `SE_PROTOCOL(SD_INTERNAL)`, *"a bug in `game/validate.cpp`"*, with `bad_index == 0xFF`
  so neither the log nor the peer was told which member did it. Measured: a peer sending
  `member[1].hp_cur = 0` closed the honest endpoint that way, and so did an honest player
  whose own team held a fainted Pebble, **with nobody lying at all** — both endpoints
  recorded an internal fault over a legal team. It is byte for byte the class P4-C5b had
  already fixed once for cross-team duplicate ids and left one instance of. There is now a
  `validate_battle_ready()` beside `validate_level_band()` — a set rule in a context, no
  policy flag in the shared body — answering a named **`VR_MEMBER_FAINTED`**; it runs on
  the LOCAL team in `session_set_team()` before a frame is sent and on the PEER's in
  `on_team_submit()`, so the player is told before a session starts and the peer is told
  by the same code with the member index. `game/validate.h`'s containment claim is now the
  strong one — after the whole chain `battle_init()` returns `BR_OK` — and
  `a_battle_ready_team_is_one_the_engine_accepts_outright` asserts it over 36 species x 5
  levels x 8 hostile variants.
- **A PEER COULD CHOOSE OUR TERMINAL LABEL AFTER IT HAD ALREADY MADE US PAY.**
  `session.h` said `session_rewards_authorised()` is true only after `SE_DONE` and
  `docs/protocol.md` said a desync pays nobody on both sides; `on_battle_end()` closed
  `SE_DESYNC` or `SE_LOST` straight from the peer's own reason byte without ever
  consulting `s.paid`, and nothing cleared `s.paid`. Measured: with the endpoint parked in
  `SS_ENDING` with rewards committed, one forged `BATTLE_END(SE_DESYNC)` carrying a final
  hash we never computed closed us `SE_DESYNC` **while paid**, and a flood that exhausted
  `SESSION_MAX_RX` did the same with no `BATTLE_END` at all. A caller following the doc
  and one following the header then disagreed about the same session and the **peer**
  picked which. The rule now lives in `session_close()`, the one place a terminal is
  decided: **a session that has authorised rewards closes `SE_DONE`.** The label is
  corrected and not the payment — `paid` is set only by our own comparison of the peer's
  outcome *and* final hash against ours — because refusing to pay would hand an attacker a
  free denial for the price of one frame.
- **ONE LEGAL FRAME, REPLAYED, HELD A SESSION OPEN FOREVER.** R4 says only progress touches
  the ladder, and every handler guards its own repeat — except `on_action_result()`, which
  treated *every* ACTION_RESULT matching our outstanding ACTION as progress, including the
  second and every one after. A re-acknowledged ACTION_RESULT is exactly what an honest
  peer regenerates from state, so a peer that said nothing else and replayed one frame at a
  spacing **it** chose kept the session alive for 1,019 injections and seventeen hours of
  virtual time, ending `SE_PROTOCOL(SD_RX_BUDGET)` with `tx_retx == 0` — neither the
  `SE_LOST` the LIMITS promise nor the `tx_retx == 9` the abort record's own signature
  relies on. The existing flood case could not see it: it injects `PT_ACTION_RESULT` from
  `SS_TEAM`, where it is a wrong-state frame that never reaches the handler.
- **THE FIRST SEQUENCE NUMBER WAS UNBOUNDED.** `SESSION_SEQ_MAX_JUMP` bounded a jump from
  an *established* last; the first frame an endpoint accepted set that last to whatever it
  claimed, so the one-packet deafness P4-C5b closed was still available **before the peer
  had spoken** — through a HELLO, which carries session 0 and needs no session id at all.
  Measured: an injected HELLO with seq 0x7000 took `rx_seq_last` to 28,672 and both
  endpoints ended `SE_LOST` on a link that dropped nothing. The bound now applies from an
  implicit last of 0.
- **`proto_encode()` COULD EMIT A FRAME NO DECODER WOULD EVER ACCEPT.** `protocol.h`
  claimed the encoder applies the same rules as the decoder and cited a named test for it;
  `grep` found that name in exactly one place in the repository — the citation itself. The
  claim was also false: the encoder applied no session rule, so a HELLO with a non-zero
  session encoded happily and step 10 refused it for *every* `expect_session`. A review's
  encoder fuzzer found 198 of them in 2,000,000 random messages; the case written here
  reproduces it 35 times when the rule is removed again. The missing rule is added and
  `every_frame_the_encoder_emits_its_own_decoder_accepts` now exists, sweeping the twelve
  types and then fuzzing the header — **and writing it found a second thing**: drawing the
  header's `round` as `(uint8_t)rng_next(rng)` produced **zero zeroes in 40,000 draws** on
  this xorshift32, so the fuzz arm would never have built the one message the case exists
  for. A low byte is not a uniform small integer; it draws through `rng_next_below()` now.

### Changed (P4-C5 follow-up — sentences narrowed to what is true)

- `game/validate.cpp` and `docs/protocol.md` said **"nothing in `src/` writes a nickname
  today"**. `persistence/migration.cpp:221-226` does, on the v1 -> v2 migration. The
  load-bearing half survives — that loop is bounded and always stores the NUL, so a peer
  would still be the first producer of an *unterminated* nickname — and both sentences now
  say so.
- `docs/protocol.md` quoted **`Session` at 360 B inside the device RAM budget**. That is
  the x86-64 host figure; compiled with `riscv32-esp-elf-g++` it is **340 B**, so the
  total is 468 B and not 488. Conservative, but a host number wearing a device number's
  clothes.
- LIMIT 1's *"what the player sees"* and `session.h`'s *"the caller pays XP and writes the
  Box"* were present tense about code that does not exist: nothing under `app/`, `ui/` or
  `persistence/` calls into this module at this commit. Both are marked P7's, the way the
  radio already was.
- LIMIT 4's **"genome variance is 0..2 by construction"** reads as a reassurance about the
  stakes and is only a statement about the range. Measured: a mirror match at level 10,
  same species and moves, AI both sides, 200 seeds per configuration, maximum genome
  against minimum, is **594 wins of 600 to the forged side**, symmetric across sides. Two
  points of atk/def/spd is not a rounding error, and the clause says so now.
- The acceptance census records the **instrument's own error bar**: the loopback's 24-deep
  queue counts a send onto a full queue as a drop, so an arm's effective loss is its
  declared loss plus that. **All six arms, named** — the first listing gave five unnamed
  pairs for six arms and silently dropped the duplicate arm: clean 0 of 70,298; 10 % drop
  283 of 109,656; 10 % duplicate 0 of 70,298; 10 % reorder w4 192 of 88,042; all three at
  10 % 540 of 120,856; harsh 43 of 157,896. It now asserts that an unfaulted link overflows
  nothing and that no arm lets overflow become its dominant fault. It also states that the
  harness
  advances its virtual clock only on a poll round in which no frame moved anywhere, so the
  census measures session logic under loss and not a duty cycle.
- The harsh acceptance arm reads **471/500** (was 474): three trials had been completing on
  a ladder a re-acknowledgement wrongly reset.
- `tests/test_session.cpp`'s census classifier returned `LE_HALF_PAID` before it consulted
  state agreement, so a trial paying exactly one side **on a divergent state** would have
  landed in an accepted bucket. It has never fired — a latent hole, closed for one
  condition.
- P4-C5b's *"every variant byte-identical to `66993bb`"* is not a checkable sentence about
  this build system, and the substance behind it is now measured more strongly than that
  wording could be: building the parent and this commit into two fixed build paths,
  `.flash.text` (sha256 `d1616c8e...`), `.iram0.text` and `.dram0.data` are byte-identical
  and `.flash.rodata` differs in **exactly one byte** — the minute digit of the `__TIME__`
  literal. The 66 bytes that differ in the whole `.bin` are that one, the 32 B
  `app_elf_sha256`, and the 32 B trailing image hash both propagate into.
- `tools/check.sh` gained a gate that requires **every** file under `src/networking` to be
  classified as pure or as a declared device module. The purity gates are filename-scoped
  on purpose (the directory is mixed), and their failure mode was silence: P7's transport
  would have arrived with no gate on it and nothing would have said so. The three
  networking gates now read **one** pair of lists rather than three copies, and a fourth
  check keeps those two lists from drifting apart.

### Changed

- **`save_load_all()` now ends in the shared validator, and `save_manager.h` stopped
  advertising a stage it did not have.** The header promised "read, checksum, schema
  validation, migration, runtime validation"; `grep -c valid` inside the old
  `save_load_all()` was **0** and `blob_ok()` — magic, CRC and a version byte — was the
  whole of it. Spec §15 names the load path as a consumer, so every occupied slot is now
  run through `validate_pebble()`. The rule is **QUARANTINE**: the slot is flagged in an
  in-RAM mask with its named `VReject` (`save_quarantine_mask()` /
  `save_quarantine_reason()`), the Box still loads and not one byte is repaired. Refusing
  the Box would brick a device on a content-pack change, because `VR_UNKNOWN_SPECIES` is
  exactly what an older save legitimately produces.

- **`XP_SRC_BATTLE` is metered.** `game/xp.cpp` carried its row as `{0, 0}` — "reserved but
  not yet metered, until P4-C4 exists to spend it", in its own words — and P4-C4 is that
  commit. `XP_CAP_BATTLE` is 50 XP an hour (two wins at `XP_BATTLE_WIN` 25), the same shape
  as the minigame row, because a practice battle is a minute of button presses repeatable
  at will and would otherwise have been the only uncapped XP source in the game. **The
  save-compat consequence is stated in `data/balance.h` rather than left to be found**: the
  byte was persisted as 0 while the slot was unmetered and 0 now means "empty bucket", so a
  device upgrading across this commit waits for the bucket to refill — `xp_ledger_restore()`
  credits elapsed real time first, so a device that was off for an hour comes back full.
- **The PLAY list's row fold was widened, not appended to.** `play_input()` launches game
  number `s_play` with no lookup table, so a row inserted among the games would silently
  start the wrong one. `PLAY_ROWS` is `MG_ID_COUNT + 2` and `play_rows_are_in_mgid_order()`
  now pins the battle row and the way out by name AND by position.
- `ui/screen_soon.cpp`'s `soon_battle()` was **deleted** with the row that pointed at it.
  An unreferenced non-static function raises no warning, so nothing would ever have failed
  on account of it; `STR_PHASE_4` is now the one string in `core/strings_es.h` with no
  consumer and stays only because deleting an id renumbers the generated block behind it.

- **The battle engine** (`game/battle.{h,cpp}`): spec §14's nine numbered steps as nine
  functions carrying those numbers, called once each in order by a driver whose body is
  exactly those calls. Teams of up to three with one active a side, switching that costs
  the turn and outruns every attack, buffs and protection with real durations, a forced
  replacement after a faint, a caller-owned `BattleLog` ring, and `battle_state_hash()` —
  FNV-1a 32 over the whole 212-byte state, random cursor included, so a divergence in the
  NUMBER of draws taken is caught on the round it happens rather than three rounds later.
  PURE: no `Arduino.h`, no heap, no `std::` container, no floating point, no clock, and no
  file-scope mutable variable, which is what will let P4-C5's loopback run two engines in
  one process. Sizes are compiled, not estimated: `BattleCombatant` 32 B, `BattleSide`
  100 B, `BattleState` 212 B, `BattleEvent` 12 B, each padding-free under a sum-of-members
  `static_assert`. The module declares no instance, so it spends 0 B of globals, and it
  spends 0 B of flash TODAY for a measured reason rather than a hopeful one: the firmware
  compiles the file (its `static_assert`s fire, and it is clean under `--warnings all`) but
  nothing calls it yet, so `nm` finds no `battle_*` symbol in the linked ELF and the image
  is byte-identical to P4-C1's on all 7 variants. The marginal code it adds is the
  `size -A` `.text` sum, 6,030 B after the P4-C2/C3 follow-up, which is what P4-C4 should
  expect to start paying. (This first read "8,533 B of `.text`" — the Berkeley `size` `text`
  column, which also counts `.rodata` the linker shares with every other user of the
  generated content tables.)
- **The local opponent** (`game/battle_ai.{h,cpp}`): a greedy chooser that ranks legal
  moves by expected damage times effective accuracy — the type modifier included, and the
  per-combatant type-edge budget respected so it stops paying for an advantage it has
  already spent — and switches when HP is strictly below 25 % (`BATTLE_AI_SWITCH_HP_PCT`)
  AND a benched Pebble is better typed against the foe. IT CANNOT PRODUCE AN INVALID
  ACTION, and the reason is structural rather than careful: the AI does not decide legality
  at all. It enumerates the seven actions that exist, hands each to
  `battle_validate_action()`, and may return only one that came back `BR_OK` — so there is
  no second copy of "is this on cooldown" or "has this side submitted" to drift from the
  engine's, and a forced replacement is not a special case but the branch where the
  validator has refused every attack. A side with nothing legal gets `BACT_NONE`, which the
  validator itself refuses by name. It carries **its own `Rng`**, in its own object outside
  `BattleState`: not hashed, not on P4-C5's wire, and — because `battle_ai_choose()` takes
  a `const BattleState&` and no function in the module takes a mutable one — unable to draw
  from the battle's stream or write any byte of the hashed state. That is measured and not
  merely argued: an AI-driven battle replays byte-for-byte through `battle_replay()`, which
  never constructs an AI, and the round-by-round cursor comparison would catch a single
  stolen draw. Compiled cost is 1,148 B by the `size -A` `.text` sum (Berkeley `size`
  prints 2,013, the difference being shared `.rodata` and `.eh_frame`), 0 `.data`, 0 `.bss`,
  and flash stays byte-identical at 1,896,094 on all 7 variants because nothing calls it yet.
- **`test_battle_ai.cpp`**, 26 → 27 binaries: 25 cases / 3,159 checks. Every `BACT_NONE`
  answer is paired with a brute-force sweep of all 65,536 action patterns proving nothing
  was legal either; 64 AI-vs-AI battles submit every action through the real validator with
  a floor on the count so the headline claim cannot pass vacuously; the type modifier is
  walked on all three arms of the chart with exact integer scores; the 25 % rule is pinned
  at the threshold and one point below it, and as a FRACTION rather than a raw hit-point
  count.
- **A fourth grep gate**, proven failable in both the header and the source before it
  landed: `game/battle_ai.*` may not name a mutable `BattleState&`. The existing purity and
  named-RNG-stream gates already glob `src/game/battle*` and so cover the new file too —
  which was confirmed by breaking each of them and watching it fail.
- **One damage rule and one accuracy rule, not two.** `battle_damage_pre_roll()` and
  `battle_accuracy_eff()` were factored out of `battle.cpp`'s round so the AI predicts a
  hit by calling the engine's own arithmetic instead of carrying a copy that drifts the
  first time a constant in `balance.h` moves. Behaviour-preserving: the 22-round golden
  transcript recorded before the refactor still matches event by event after it.
- **A rejection contract with teeth.** A `BattleAction` is two untrusted bytes and all
  65,536 patterns are swept in the tests; every one maps to a legal action or to a NAMED
  `BR_*` refusal, and the engine NEVER CLAMPS a peer-supplied value. The validator takes a
  `const BattleState&`, and because the per-battle `Rng` lives inside the state, `const`
  also freezes the random cursor — so "stash the attempt", "decrement the cooldown you
  just checked" and "roll for the tie you are about to need" do not compile.
  `battle_submit_action()` has exactly one write site and it is unreachable on a reject,
  which the tests prove with `memcmp` over the whole struct rather than by reading fields.
- **Three new host tests**, 23 → 26 binaries: `test_battle.cpp` (60 cases at P4-C2, 65 after the
  follow-up — every §50 battle case, each invalid action asserting the exact reject code
  with a positive control beside it), `test_battle_replay.cpp` (8 cases — twin engines run INTERLEAVED with equal
  hashes every round, a seeded negative control, a pinned RNG cursor and exact draw count,
  and replay from a recorded log) and `test_battle_golden.cpp` over
  `tests/golden/battle_v1.txt` (a 22-round 3v3 recorded event by event: it is the only
  thing that can catch a duration off-by-one two engines share, and the only thing that
  watches the log at all, since the log lives outside the hashed state on purpose).
- **Three grep gates** in `tools/check.sh`, each proven failable before it landed: `src/game`
  may not include `Arduino.h`/`u8g2`/`gfx.h`/`render.h` (this gate did not previously
  exist — purity was enforced only as a confusing compile error inside the host tests);
  `src/game/battle*` may not touch a named RNG stream; and `battle_step_round()`'s nine step
  calls must read 1..9 in order.
- **The content pipeline** (`tools/gen_content.py`, `tools/content/*.json`): the species,
  attack, item, evolution and encounter tables under `Pebblebol/src/data/` are now
  GENERATED from JSON and committed. Running the generator twice on the same JSON produces
  byte-identical headers, and `tools/check.sh` runs both `tools/gen_content.py --check`
  (regenerate in memory, diff against the tree) and the content pack's own `verify.py`
  (82 checks) — so a hand edit to a generated header and a JSON edit that was never
  regenerated each fail the gate.
- **The roster grew from 3 species to 36** (12 families x 3), with 34 attacks, 10 items,
  24 evolution rules, 40 encounter rows and 33 item-drop rows. **Species 1 (Paketo) is
  byte-identical to what Phase 3 froze** and `tests/golden/care_v2.txt` was not re-recorded.
  The roster is a PREFIX of the 60-species pack because `id == index + 1` is a
  `static_assert`; 36 is the largest prefix today's 38-slot sprite atlas can address
  (`SPR_BABY_BLOB + sprite_id < SPRITE_SET_COUNT`). Flash does not bind: +3,022 B against
  503,906 B of headroom. Globals is the budget that will: the tables are all `.rodata`, so
  this step spent 0 B of it on all 7 variants and it stays at 70,348 of 90,000 — 19,652 B
  free, against flash's 503,906.
- **`CONTENT_VERSION` is a hash** (`data/content_version.h`, generated) rather than a
  hand-bumped counter: FNV-1a over the six JSON files with design notes excluded, plus the
  roster size. Moving a real value changes it; fixing a typo in a `_`-prefixed note does not,
  so a comment edit cannot mark every save on every device as foreign content.
- **Battle constants** (`data/balance.h` §5): the §1.5.1 damage formula's terms, the
  MULTIPLICATIVE type modifier that supersedes the plan's `+ 2*type_mod`, protection,
  buff stages, the evasion rule, and three constants the content pack does not carry and
  which are named as decisions here — the round cap with an integer HP tiebreak and an
  explicit draw, the team size, and what a win pays.
- **Derived stats** (`game/pebble.{h,cpp}`): `pebble_derive_stats()` — integer only, nothing
  stored, variation from the genome with the `creation_seed` reading recorded as rejected.
- **Content accessors** (`game/species.{h,cpp}`): family bases, the type modifier, the
  weighted encounter pick and the two-stage item drop. It is also the one translation unit
  that includes every generated header, so their compile-time guards actually fire.
- **Two new host tests**, 21 → 23 binaries: `test_content.cpp` (31 cases, every §1.5.2 guard
  re-asserted at runtime, the starter pinned field by field, and `evolution_apply()` driven
  through a FAILING condition for the first time) and `test_stats.cpp` (11 cases).
- **A species you can see** (P4-C4a, closing the obligation carried since P3-C3). The sprite
  atlas was keyed on `gene_species(genome)`, the four-bit nibble the v1 save carried, so all
  36 species wore one of eight genome bodies, an evolution moved the FORM and never the
  CREATURE, and `SpeciesDef.sprite_id` was asserted by two guards and drawn by nothing. New
  `ui/pet_art.h` holds the one resolution — the species row's `sprite_id`, falling back to
  the genome nibble only when there is no row; `sprite_form_of()` takes that KEY instead of a
  `Genome` and `sprite_set_id()` has no `species` parameter left, so no path from a genome to
  a body exists that does not pass a species row first. `pet_view_attach()` re-derives the
  design where `species_id` actually arrives, one call after `pet_view_fill_sim()` which
  cannot see it. The NAME follows: `ui_pet_name()` is nickname → `STR_SPC_NAME_*` → dynasty
  syllables, and the BOX list stopped calling ten different creatures SETA. **Measured**: all
  24 evolution rules change the drawn set id at BABY, ADULT and SENIOR and none at CHILD or
  TEEN (those two designs carry the care quality, and there is no species art at either
  stage); the no-row fallback — id 0, the creator's 200..209, anything past the roster — is
  bit-identical to the old arithmetic; +164 B of flash, 0 B of globals, and no new art, since
  36 more 24×24 sets would be 15,807 B against a 14,336 B budget assert. What it does not
  claim: 36 species do not fit 8 + 6 authored designs, so two species can share a body until
  P10's art pass and the name beside it is what tells them apart.

### Fixed

- **`box_new_pebble()` never wrote `evo_state`, so every Pebble it minted at a stage-1 or
  stage-2 species carried stage bits 0** — a fact about the creature that disagreed with
  its own species row. `grep -c evo_state game/box.cpp` was 0, and
  `tests/test_battle_screen.cpp:107` creates species 1, 5 and 9, of which 5 is stage 1 and
  9 is stage 2. It was harmless only because nothing outside the evolve ceremony read the
  bits; `game/validate.cpp` reads them now, so a legitimately captured mid-stage Pebble
  would have been refused on the wire and quarantined on load. The fix is one line **at the
  writer**, never a repair inside the validator, and reverting it turns
  `a_constructed_pebble_validates` red.

#### P4-C4 follow-up — a won battle that paid nothing, and an evolution the tick was wider than

- **A BATTLE THAT WAS ALREADY WON PAID NOTHING ON THE WRONG EXIT.** The engine decides the
  outcome inside `battle_step_round()`, while the victory transcript is still playing — the
  last blow, the faint and the `BATTLE_END` line are four more beats, about 2.4 s.
  `battle_leave()` reported a flat `report_once(0u)` for that whole window, so `LONG_BOTH`
  (the global HOME invariant; `SCR_BATTLE` carries no `SF_LOCK_INPUT`) or a hatch ceremony
  arriving there turned a WIN into a loss and paid no XP, while B in the identical state ran
  `end_playback()` and paid. Same battle, same state, different exit, different reward.
  **Measured: 35 of 64 seeded practice battles sit decided-but-unreported in that window**,
  and all 35 were mis-reported. `battle_leave()` reports the engine's own outcome now; an
  abandoned battle is still `BO_UNDECIDED` and still pays nothing, which was the intent all
  along. `a_decided_battle_left_before_its_transcript_ends_still_reports_the_win` drives all
  64 to that exact state through the real screen.
- **THE EVOLUTION TICK WAS WIDER THAN THE TREE FOR HALF THE ROSTER, and the number is now
  written down and checked.** P4-C4a closed §18's visual obligation and measured "24 of 24
  rules change the drawn body at BABY, ADULT and SENIOR". That is true, and those are three
  STAGES, not three moments: `sim.cpp`'s ladder is `>= 20` SENIOR / `>= 15` ADULT / `>= 10`
  TEEN / `>= 5` CHILD, and **12 of the 24 shipped rules have a minimum level of 8, 10 or 12
  — every family's FIRST evolution, the starter's Paketo → Fragmar among them — so they fire
  at CHILD or TEEN, which are the two stages the species deliberately does not key**. At
  those twelve confirmations the drawn body is bit-identical and only the NAME and `hp_max`
  move; the body follows when the pet reaches ADULT. The policy is unchanged and is right
  (the atlas authors two CHILD and two TEEN designs and both pairs already carry care
  quality), so what was fixed is the CLAIM: `every_rule_measured_at_the_level_it_actually_
  fires_at` binds a Pebble at each rule's own level, asks the SIMULATION which stage that
  is rather than restating the ladder, and asserts 12 / 12 with the name moving for all 24
  and the body moving for all 24 at ADULT. The plan, the §67 line and the obligation bullet
  all carry the number now.
- **AND ON A MIGRATED DEVICE THE NAME DID NOT MOVE EITHER**, which made the twelve above a
  change of nothing at all. `migrate_v1_to_v2()` synthesized the v1 DYNASTY name into
  `PebbleInstance.nickname` for a pet nobody had ever renamed; `game_state.cpp` copies
  `pebbles[0].nickname` into `Config.pet_name`; `ui_pet_name()` answers `Config.pet_name`
  before anything else; and no v2 path ever clears it. So a migrated player saw the dynasty
  syllables for the life of the device and the species name never appeared once — the exact
  P3-C3 complaint P4-C4a set out to close, on every device that had a v1 save. A fresh v2
  device was fine (`box.cpp` writes no nickname, ever). **The write was display-neutral when
  it was written** — `ui_name_for()` computes the same hash over the same syllable tables,
  so an empty nickname drew the identical word — and stopped being neutral the moment the
  ladder grew a middle rung. A migrated pet with no typed name now arrives unnamed;
  `migrate_default_name()` is deleted with its only call site; a v1 owner who DID type a
  name still keeps it.
- **`BO_ABORT` reached the player as the word "Empate".** `game/battle.h` defines it as "step
  1 found the state moved under a submitted action" (P4-C5's `BATTLE_END(DESYNC)`), and it
  arrived at `outcome_word()`'s `default` arm — so the one outcome that means something went
  wrong was the one outcome the panel called an ordinary result. It has its own string now.
  Unreachable from this screen today, which is why the fix is a name rather than a recovery.
- **`battle_update()` had no `s_live` guard**, unlike `battle_render()` and `battle_input()`.
  A DIAG entry whose `battle_init()` was refused still ran the INTRO timeout and left the
  reported mode at `BTM_MENU` while the panel drew the start-error page. Not exploitable —
  B still leaves — but a screen whose reported mode disagrees with its picture is a screen
  no other case can be written against.

#### P4-C4a — three more tests that could not fail, and one dead function

- **`every_species_row_points_at_a_real_sprite_set` existed byte-identically in
  `test_content.cpp` and `test_evolution.cpp`, and neither copy could fail in the way that
  mattered.** Both asserted that `SPR_BABY_BLOB + sprite_id` was arithmetically inside
  `SPRITE_SETS` — and nothing in the tree evaluated that sum to draw anything. Against the
  38-set atlas it sends species 25..36 onto `SPR_GHOST`, `SPR_TOMB` and the ten pose sets;
  Murax would have been drawn as an adult eating, and both tests passed. `test_content.cpp`
  now resolves every row at every stage through `sprite_set_id()` and requires a creature
  body, and states as a number (12 species, first at id 25) how much of the roster the naive
  sum would mis-draw. `test_evolution.cpp` takes the question that file owns instead: every
  one of the 24 rules changes the body. A `static_assert` in `game/species.cpp` checks the
  same property at compile time, and it is what fires first when the naive resolution is
  restored.
- **`test_pet_view.cpp`'s care-percentage case was cited as proof of the CareId/StatId
  mapping while driving the fill that has no mapping in it.** The hazard lives in
  `pet_view_fill_sim()`'s `kStatOfCare[]`; the case drove `pet_view_fill()`, a straight copy
  of `inst.care[]`. It drives the live fill now, through five DISTINCT `sim_god_set_stat()`
  values with the distinctness asserted — with five equal stats any permutation passes.
- **`pet_view_fill()` is deleted.** No caller in `Pebblebol/src` for three phases; a stage
  ladder its own comment called "the same rule `sim_bind()` uses" that disagreed with
  `sim.cpp` on three of four thresholds and could never return `STAGE_SENIOR`; and an
  evolution case that "proved" a changed body by feeding `evo_state & 3` in as `minor_form`,
  i.e. by drawing the pet as badly cared for. `PetView.hp_pct` went with it — that function
  alone wrote it and nothing read it.
- **One claim of this step's own was refuted before it shipped.** The first draft of both
  evolution cases said they would fail if `SPRITE_BABY_BODIES` shrank 8 → 3. Measured, they
  do not: every rule is a single step between CONSECUTIVE art keys, so `k % P != (k+1) % P`
  for every pool size except 1. Both comments now name the mutation each case really fails on
  (8 → 1, 6 → 1) and say which case catches a pool of 3 instead.

#### The P4-C2/C3 follow-up — six blocking review findings

- **A hash that did not carry the version it said it carried.** `battle.h` promised that a
  peer running different RULES could never produce a matching `battle_state_hash()`, because
  `BATTLE_ENGINE_VER` rode in the FNV basis. It did not — the basis carried
  `BATTLE_HASH_VERSION` — so bumping the engine version, which the very next sentence orders
  a maintainer to do for a rules change, moved no hash at all. Not an argument: at commit
  9997ed4 the macro appeared in `battle.cpp` exactly three times, all three of them the
  setup-carried version check, so every state hashed the same at either version. All three
  version words are now mixed through the FNV step byte by byte rather than XORed into the
  basis, which also removes a genuine collision — `(hash 1, content 0x5B4A)` and
  `(3, 0x5B48)` both folded to `0x5B4B`. `battle_hash_basis()` is parameterised so a host
  test can require each word to change the answer without recompiling the engine three times.
- **`battle_init()` accepted a move the species cannot learn** (spec §67). It checked only
  that the four ids resolved through `attack_get()`, so a peer-supplied team could hand any
  species any of the 34 attacks — including the off-type ones `attacks_table.h`'s own
  `species_learnsets_are_legal()` declares impossible. Decisive and measured: a species-1
  Paketo that wins **0 of 200** scripted 1v1 seeds against species 17 wins **100** with
  attack 11 Plaga written into slot 0 — and **103** with the ON-TYPE attack 3 Rafaga, so a
  rule that only checked the move's type would have stopped nothing. The rule is the closure
  of the only two writers of `PebbleInstance.moves[]` in the tree: it must be the verbatim
  learnset of some species in the same family at a stage no higher than this one's, which
  accepts an evolved Pebble still carrying the kit it grew up with and refuses everything
  else. New reject code `BR_UNLEARNABLE_MOVE`, and `BATTLE_ENGINE_VER` is bumped to 2
  because `battle_init()` now refuses what it used to accept.
- **Three more tests that could not fail** — the seventh, eighth and ninth instances of this
  project's named recurring defect, each fixed and each verified by breaking the guard it
  names and watching that test fail. (1) `protection_halves_after_the_roll_and_never_below_one`
  never tested "never below one": deleting the `DMG_MIN` floor inside the protection branch
  left the whole suite green. (2) The two `DMG_MIN` floors in `battle_damage_pre_roll()`
  **mutually masked**, because every case reached only a NEUTRAL matchup where `TYPE_MUL` is
  1/1 — deleting either alone was green, and without the post-multiply floor a disadvantaged
  minimum hit deals literally nothing. (3) The debuff arm of
  `a_buff_stage_clamps_at_two_and_a_debuff_can_never_wrap_the_stat` used base ATK 3, where
  `3 + BUFF_STAGE_MIN` is also `STAT_EFF_MIN` — so the later floor answered and the clamp the
  test names was never reached.
- **Four of the nine steps indexed `team[active]` without range-checking it.** `battle.h`
  advertises the steps as individually callable and P4-C5 hands in state this engine did not
  build; with `side[1].active = 3`, `battle_s3_determine_order()` is a stack-buffer-overflow
  read past the end of the 212 B `BattleState` under `-fsanitize=address`. It is not
  reachable through `battle_step_round()` — step 1 refuses that state as `BR_EMPTY_ACTIVE`
  and the driver aborts — so the new guards are labelled in the code as untested defence in
  depth rather than left looking proven.
- **A fifth grep gate: the firmware's tuning must match the content pack's.** Thirteen
  numbers live in both `tools/content/balance.json` (which feeds `CONTENT_VERSION`, and
  which the roster was tuned against) and `src/data/balance.h` (which the firmware compiles,
  and which is not generated). Nothing compared them, so a header-only retune changed hashed
  battle state with no `CONTENT_VERSION` and no `BATTLE_ENGINE_VER` moving to say so —
  measured: editing `BUFF_STAGE_MAX` from `+2` to `+3` in the header alone left the entire
  gate green. Proven failable three ways and restored.
- **Four sentences wider than the tree, corrected against measurement.** `battle_ai.h`
  claimed its protected-defender estimate "preserves the ranking"; it does not — 757 of
  233,280 plain-roster protected move pairs (0.32 %) rank the wrong way round, worst case
  5.9 % of the better move's expected damage — though the conservative half of the claim
  held, at no more than 2/6 of a point from the engine's exact expectation. The comment
  defending step 8's second fainting pass said a DOT-killed Pebble "never yields a victory";
  `combatant_alive()` requires `hp_cur > 0`, so the victory is found either way and what
  actually goes missing is the `BCF_FAINTED` flag and the transcript's FAINT event.
  `battle_ai_move_score()`'s "widest reachable" 89,400 is an arithmetic ceiling; the widest
  the shipped roster reaches is 34,170. And the two object-size figures were printed side by
  side from two different measures.
- **Also**: `BATTLE_AI_VER` is labelled as having no consumer; a provably dead `DMG_MIN`
  floor in `battle_ai.cpp` is deleted rather than labelled; three public queries that were
  only ever reached through a caller carrying its own copy of the same check
  (`battle_action_legal_now`'s side bound, `battle_move_ready`'s three guards, and the
  driver's two mutually-masking early-outs) now have tests that reach them directly; and
  `battle.h` records that swapping steps 6 and 7 leaves every round hash byte-identical, so
  the golden transcript and the third grep gate are the only things holding that boundary.

- **`balance.h` contradicted itself about who owns the type-advantage cap.** The
  `TYPE_MOD_MAX_HITS` paragraph said "per attacker per battle" in one sentence and "P4-C2
  owns one counter per side" in the next. `tools/content/sim_engine.py`, which is what the
  roster was tuned against, keeps the counter on the attacking FIGHTER; the ownership
  sentence was the wrong one and is corrected, together with three semantics read out of
  the same code rather than guessed (a miss does not spend the edge, a power-0 move does
  not spend it, and the cap zeroes a DISADVANTAGE as well as an advantage). The rejected
  reading is recorded beside it.

- **The legacy v1 family map** (`persistence/migration.cpp`): it mapped the eight v1 families
  onto ids 1..8, written before ids 1..3 became the three STAGES of one family. Five of the
  eight resolved to nothing at all — no `hp_max`, no evolution, a fabricated full HP meter —
  and two landed mid-family. Every legacy family now lands on a distinct BASE-stage species,
  read from the generated `SPECIES_BASE_OF_FAMILY[]`, under a `static_assert`.
- **A migrated pet arrived with no attacks and 0 HP.** `migrate_v1_to_v2()` never wrote
  `p.moves` (0 is the empty move slot) or `p.hp_cur` (which `xp_hp_rescale()` only scales, so
  0 stayed 0 for ever and every HP meter showed 0 %). Both come from the species row now.
- **Two open-coded copies of `hp_max`** in `ui/ui.cpp` and `game/box.cpp` now call
  `xp_hp_max()`, which became `inline constexpr` in `game/xp.h`.

### Measured (P4-C4 follow-up)

- **No frame of a battle allocates — measured, not argued.** P4-C4 could only offer the
  design argument (every byte the screen owns is a file-scope static) because there is no
  device here to read `ESP.getFreeHeap()` on. `no_frame_of_a_battle_allocates` interposes
  `operator new` / `new[]` / `malloc` / `calloc` / `realloc` and drives **3,942 real
  `update()`+`render()` frames across all six modes for 0 allocations** — after allocating
  on purpose first, so the zero is not a broken counter. The device half of the claim
  (`dev/godmode.cpp`'s SYS/HEAP page) has still never run on hardware and says so.
- **A menu always has at least one legal row**, which is the property the cursor ring's whole
  promise stands on and which nothing named. It holds because of `battle.h`'s
  `static_assert battle_every_learnset_has_an_always_ready_move()` and because
  `battle_s9_check_victory()` decides the battle at `alive_count == 0`. Both are re-checked
  at runtime now, over 905 menus reached across 64 battles; **the tightest offered exactly 1
  legal row**, so the case has met the situation it is about rather than merely survived it.
- **`the_species_chooses_the_combat_body` had a name wider than its body** and is renamed
  `the_roster_folds_onto_the_authored_combat_bodies`. It called `br_body_set_id(sp->
  sprite_id)` straight from the row, so it never touched `pet_art_key()` — the expression
  that IS the species → body link — and mutating that function to `return gene_species` left
  it green while `test_pet_view` and three goldens failed. It resolves through
  `pet_art_key()` now, exactly as `resolve_art()` does, and that mutation fails it at 37
  checks.

### Known gaps

- **The SPECIAL encounter outcome has no payload table** — 4 to 10 % of every scan resolves
  to an outcome nothing defines. P5-C3 owns it; `test_content.cpp` states the gap.
- **CARE and BATTLE_MOD items name no target stat**, and item 9 *Llave Raíz* is a CARE item
  with value 0 — the pack's EVOC_ITEM key folded onto a class list with no slot for one.
  `items_table.h` derives all three into its banner and `test_content.cpp` pins the third.
- **Eight of spec §35's thirteen creator validation inputs have no row anywhere.**
  `creator_schema.h` lists those eight, states the id range, and names type validity and move
  legality as derivable from tables that already ship — 2 + 1 + 2 + 8, so the header adds up.

### Removed (P4-C6 — the dead code the P4-C4a sweep stopped one item short of)

- **The mood badge, all four links of it.** P4-C4a deleted `PetView.hp_pct` because one
  function wrote it and nothing read it, and stopped at the first instance. A symbol-level
  sweep — every global present in a `.o` and absent from the linked ELF, cross-checked
  against a comment-stripped grep — found the next three and the chain behind them:
  `PetView.mood`, `PetView.asleep` and `PetView.sick` (each assigned once in
  `pet_view_fill_sim()` and read by no line in `Pebblebol/src` or `tests`),
  `PebbleView.mood_face` (written by `ui.cpp` every frame, drawn by no screen),
  `sprite_mood_face()` (**zero references in the whole tree**, its last caller gone since
  P2-C11b) and `spr_mood12`, 144 B of authored 12×12 art that `--gc-sections` had been
  dropping from every build. `asleep` and `sick` were worse than unused: they were a second
  copy of `PF_ASLEEP` and `PF_SICK`, which every real consumer reads from `flags` —
  `petfx.cpp:873` and `actfx.cpp:595` do exactly that. Two places to be wrong about one
  fact is the defect. **Measured: −42 B of flash on every variant and −16 B of globals on
  `release`.** The Mood enum, the `pet_mood_index()` ladder and the Spanish mood words stay
  and stay live; recover the pixels with `git show 250f73e:Pebblebol/src/data/sprites.h` if
  P10 designs a screen that wants a badge.

### Fixed (P4-C6 — documents that had stopped describing the tree)

- **`session_state_name()` and `session_detail_name()` shipped with no caller and no test**,
  while their three siblings (`proto_err_name`, `validate_reject_name`,
  `session_reason_name`) all had both. They are wired into the acceptance census now — a
  hung trial names the state each endpoint stopped in, and an arm that loses a trial names
  the `SessionDetail` each non-completing pair carried instead of numbering it (the print
  is per-detail and conditional, so the five arms that complete 500 of 500 print none at
  all and the harsh arm prints `SD_NONE=58`, which is 29 trials x 2 endpoints) — and
  `every_session_state_and_detail_has_a_distinct_english_name_and_the_lookup_is_total`
  gives all three enums the totality case only two of them had.
- **`docs/protocol.md` carried false test counts one commit after they changed.** Its header
  said 24 and 36 cases / 1,053 checks; commit `250f73e` edited 161 lines of that file and
  added the cases without touching the sentence. It says 25 / 234,356 and 41 / 2,493 now.
- **`docs/save_schema.md` §8's v1→v2 field map had three wrong rows and two missing ones**,
  and §9 documented a module and an NVS key that P2-C10 deleted two phases ago. The legacy
  family map is `SPECIES_BASE_OF_FAMILY[]` — `{1, 4, 7, 10, 13, 16, 19, 22}` — not the
  literal `1..8` table P4-C1 deleted as a defect; `moves[]` and `hp_cur` were added to the
  migration by P4-C1 and were not in the table; and the "else the deterministic dynasty
  name" arm is gone. The `lgpet` row is out of the key table and out of the entry
  arithmetic, which drops from 231 to **226 of 504**.
- **Two numbers inside decision D2 were written before the code they describe.** The frame
  does not fit "with 38 B to spare" as a 12 B header and a 200 B payload — that was plan
  §1.4's sketch; the shipped frame is 14 B + at most 148 B + a 2 B trailing CRC = **164 B,
  86 B to spare**, which `protocol.h` asserts. And compiling BLE out recovers **712,466 B /
  23,504 B**, re-measured here, not the phase-1 audit's 721,632 / 23,688.
- **"Zero `link_` symbols in the ELF" was false as written.** The substance holds — none of
  `battle_link.cpp`'s five functions is linked — but the ELF carries six `link_*` symbols:
  two from `ui/screen_link.cpp` and four from the Bluetooth stack. `docs/protocol.md` names
  the five functions now instead of a prefix that also catches an unrelated screen.
- **An obligation answered in another file and left open where it was filed.**
  `game/evolution.cpp` still said "there is no attack table until P4-C1 … P4-C1 adds the
  table and decides what a learnset change owes an existing creature". P4-C1 landed the
  table and did not decide; **P4-C2 did**, through `BR_UNLEARNABLE_MOVE`, which accepts the
  verbatim learnset of any same-family species at a stage ≤ this one's — so an evolved
  Pebble keeping the moves it was raised with is legal by construction. The decision, and
  the price it carries (a later stage's own learnset is unreachable in V1), are written
  where the reader is standing.
- **Four sentences narrowed to what the tree does.** `migration.cpp` claimed "nothing is
  lost" by leaving a migrated nickname empty and then described the loss in its own next
  clause — the v1 dynasty word is gone from that device for good and V1 has no rename
  screen; `pet_art.h` claimed two callers for `pet_art_design()` and has one, because
  `screen_home.cpp` correctly needs a stage ladder that covers EGG/CHILD/TEEN and this one
  does not; `tools/check.sh` claimed all thirteen of its balance constants "decide hashed
  battle state" when `TYPE_MOD_SCALE` and `RISK_SELF_HP_PCT` are 0 and decide nothing; and
  `data/balance.h` now names the chunk that owns each of its two consumerless corruption
  constants (`CORRUPT_DURATION_S` had said nothing at all). `screen_status.cpp` keeps the
  genome's species word on the genome page and now records that as a choice; and
  `game_state.cpp`'s slot-0 (rather than active-slot) name source is recorded as the latent
  P5-C4 bug it is, in the chunk that will be able to write a failing test for it.

### Fixed (P4-C6 follow-up — what four hostile verifiers found in the exit itself)

The exit above was written against phase 3's failure — four false numbers that a later audit
caught — and then reproduced two of its shapes. Every item here was confirmed against the
tree before it was touched, and every one was **narrowed** rather than deleted.

- **The phase-4 cost ledger in `docs/decisions.md` did not add up, and its first item was
  contradicted by the commit it cited.** "P4-C1 and P4-C2 moved the baseline not at all" is
  true of GLOBALS and **false of flash**: P4-C1 cost **+3,022 B**, which `993e3b0`'s own
  message and `PEBBLEBOL_IMPLEMENTATION_PLAN.md:529` both state in bold, and which the
  P4-C1 section of this same entry states three hundred lines above ("Flash does not bind:
  +3,022 B"). One axis's true number had been generalised
  onto both — the phase-3 shape exactly — and the consequence was arithmetic: the deltas the
  paragraph listed summed to **19,560 B** against the **+22,582 B** it stated four lines
  earlier, leaving 13.4 % of the phase's flash bill attributed to nothing. It also
  understated P4-C5a by measuring it across a skipped follow-up (+658 rather than +782 and
  −124). It is a per-commit table now, and **the eleven deltas sum to the phase total**.
- **The "nothing is lost" sentence the exit struck from `migration.cpp` survived verbatim in
  `migration.h`** — the same sentence, born in the same commit (`ee75076`), in the header
  that points the reader at the file that now says the opposite. This is precisely the
  phase-3 exit's documented failure (a struck sentence surviving in other places), recurring
  in the exit written to prevent it. `ui_pet_name()` reaches the dynasty rung only when
  `pet_species_name(species_id)` is null, and `migrate_species_of()` always returns a real
  roster id, so a migrated pet never reaches it; the header now says what `migration.cpp`,
  `docs/save_schema.md` §8 and two cases in `tests/test_persistence.cpp` already said.
- **Three numbers in this entry were wrong and one of them was a number this entry announces
  it corrected elsewhere.** "The five 10 % arms complete 500 of 500" counted the CLEAN arm
  as a fault arm; `tests/test_validate.cpp` and `tests/test_protocol.cpp` were given as 24
  cases each and `tests/test_session.cpp` as 36 cases / 1,053 checks — the very counts the
  Fixed section above says were corrected in `docs/protocol.md` one commit after they
  changed. Measured: **25 / 3,566**, **25 / 234,356**, **41 / 2,493**. The harsh arm's label
  names two of its three faults (it duplicates at 10 % as well), and the queue-overflow
  error bar listed five unnamed pairs for six arms, dropping the duplicate arm.
- **A test asserted a hole that a future commit could close without failing.**
  `tests/test_content.cpp`'s SPECIAL case said it "will FAIL when P5-C3 fills it"; every
  assertion in it was about the ENCOUNTER rows, so a payload table could have landed beside
  them with the case still green. It pins the pack hash (`CONTENT_VERSION 0x5B4A`) now, and
  the comment states both what that catches (any pack edit) and what it misses.
- **Three more plan §5 module-contract rows described APIs that do not exist, and one was
  made false by the exit commit itself**: `game/pebble.h` (`pebble_new()`,
  `pebble_identity()`, `pebble_display_name()` — zero definitions tree-wide),
  `game/box.h` (`bool box_add(Box&, …, uint8_t*)` against the tree's
  `uint8_t box_add(const PebbleInstance&)`), and `ui/pet_view.h`, which still declared the
  `mood`, `asleep` and `sick` fields the Removed section above deletes, plus a
  `pet_view_fill()` that P4-C4a deleted.
- **Four claims were wider than their evidence and are narrowed**: "all 3,333 lines of
  `src/networking`" (3,333 is what phase 4 ADDED; the directory is 5,247 lines and
  `ble_social`/`net`/`webui` **are** linked); "none of P4-C5 is linked" (`game/validate.cpp`
  is — `riscv32-esp-elf-nm -C` shows `T validate_pebble(PebbleInstance const&)`, reached
  from `save_manager.cpp:610`, and by elimination that is where P4-C5a's +782 B went, the
  codec half being dropped entirely by `--gc-sections`); "every arm prints
  its `SessionDetail` distribution" (only an arm with a non-completing pair prints anything
  — today just the harsh one); and "all three are measured rather than hoped" in
  `decisions.md`, where P10's 8,640 B sprite atlas is arithmetic for art nobody has drawn.
- **`−42 B on every variant` is now a measurement rather than an inheritance.**
  `build_matrix.sh` was run at `250f73e` and compared variant by variant against the exit:
  **−42 B of flash on all seven**, **−16 B of globals on `release` alone**.

### Not verified

**This firmware has still never run on a physical board.** No ESP32-C3, no panel, no cells.
Every number above is a host measurement or a compile result.

**There is no radio.** All **3,333 lines phase 4 added** under `src/networking`
(`git diff --stat v0.3.0-pet..HEAD` — the directory itself is 5,247 lines, and the other
1,914 are `ble_social`/`net`/`webui`, device modules that ARE linked) are proven over an
in-process loopback and are not linked into any build: `riscv32-esp-elf-nm` finds none of
the new ones in the ELF, so they cost 0 B today and the 468 B forecast for P7 is a
`sizeof`, not a measurement.
A real ESP-NOW link is bursty and correlated where the loopback's fault model is an
independent Bernoulli draw, so the completion rates above describe the model, not the air.

**The battle screen has never been timed on a panel**, and the on-device half of "no
per-frame heap" (`dev/godmode.cpp`'s `ESP.getFreeHeap()` sampling) has never run on
hardware. The host half is measured: 3,942 frames, 0 allocations.

**The roster's balance has never been measured against `battle.cpp`.** The win-rate band it
was tuned to came from a Python model with five named divergences from the shipped engine
and no AI at all. P9-C4 owns the real matrix.

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

[0.7.0-social]: https://github.com/pmirall/Pebblebol/commit/f8f2e51
<!-- 0.7.0-social names f8f2e51 (P7-C4/C5), the last commit before the exit,
     for the same reason 0.6.0-activity names e8701ea rather than b434491: an
     entry cannot contain the hash of the commit that adds it, since writing it
     in changes the hash. The ANNOTATED TAG v0.7.0-social points at the exit
     commit itself, which is the accurate anchor; this link points at the last
     commit that exists independently of it. Neither is on the remote — this
     session was instructed not to push — so both currently 404, exactly as the
     0.5.0 note below records for the same situation. -->
[0.6.0-activity]: https://github.com/pmirall/Pebblebol/commit/e8701ea
[0.5.0-explore]: https://github.com/pmirall/Pebblebol/commit/6ec3355
[0.4.0-battle]: https://github.com/pmirall/Pebblebol/commit/250f73e
[0.3.0-pet]: https://github.com/pmirall/Pebblebol/commit/e2004e7
[0.2.0-core]: https://github.com/pmirall/Pebblebol/commit/db3feb3
<!-- The annotated tags v0.2.0-core, v0.3.0-pet and v0.4.0-battle exist in the local
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
     entry, but it is the exit commit, not the tag.

     THE 0.4.0-battle LINK IS NEITHER, AND SAYS SO. 250f73e is the LAST COMMIT
     OF PHASE 4's SUBSTANCE - the P4-C5 follow-up - and NOT the P4-C6 exit
     commit, because the exit commit is the one that adds this line and no
     commit can carry its own SHA before it exists. The tag v0.4.0-battle points
     one commit later than this link; a reader following it lands before the
     exit's own document corrections.

     THE REPOINT IS DELIBERATELY STILL NOT DONE, and the reason changed at the
     P4-C6 follow-up. The paragraph above used to say "repoint it at the exit
     commit's SHA in the first commit after the tag" - and the follow-up IS that
     commit, so this is where it would have happened. It did not, because
     `git rev-parse origin/claude/repo-exploration-sync-bbonku` is still
     250f73e: the exit commit d21f20f and this follow-up exist on ONE MACHINE.
     Only TAG pushes are refused here; the branch push works, and until somebody
     runs it a link to d21f20f would 404 where the current one resolves. So the
     link stays at the last commit the remote actually has, and the repoint
     belongs in the FIRST COMMIT AFTER THE BRANCH IS PUSHED - at which point it
     should point at the exit commit, the way the 0.3.0-pet link points at
     e2004e7.

     THE 0.5.0-explore LINK IS 6ec3355 AND IT DOES NOT RESOLVE YET. SAID
     PLAINLY BECAUSE THE ALTERNATIVE IS WORSE. 6ec3355 is phase 5's last
     commit of substance (P5-C3/C4) - the same role 250f73e plays for phase 4,
     and the correct anchor for this entry. It is NOT on the remote: at the
     phase-5 exit `git rev-parse origin/claude/repo-exploration-sync-bbonku`
     is f496a3a, so the two exploration commits and this exit exist on one
     machine, and this session was instructed not to push. The rule the 0.4.0
     paragraph above states - link the last commit the remote actually has -
     would have pointed 0.5.0-explore at f496a3a, which is the BUDGET commit:
     it precedes every line of exploration code and would name the wrong thing
     to a reader who could reach it. A link that is right and temporarily 404s
     beats a link that resolves and lies, so the rule is narrowed rather than
     followed: point at the phase's last substance commit, and say here when it
     is not yet pushed. Both repoints - 0.4.0-battle to d21f20f and this one's
     verification - belong in the first commit after the branch is pushed. -->
