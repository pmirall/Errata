# Changelog

All notable changes to Pebblebol are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
Versions are tagged at phase boundaries of `PEBBLEBOL_IMPLEMENTATION_PLAN.md`; the
tag for a phase is cut only when its gate (`tools/check.sh`) and its variant matrix
(`tools/build_matrix.sh`) are both green.

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
     e2004e7. -->
