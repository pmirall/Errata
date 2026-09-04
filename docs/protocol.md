# Pebblebol link protocol — spec §15 as implemented (P4-C5)

This is the wire format, the state table and the honest limits of the
device-to-device battle protocol. It documents what is **in the tree at this
commit**, not what is planned: everything below is implemented in
`Pebblebol/src/networking/{protocol,session,battle_link,transport_loopback}.cpp`
and exercised by `tests/test_protocol.cpp` (24 cases) and
`tests/test_session.cpp` (36 cases, 1,053 checks).

**There is no radio yet.** Every claim here is proven on a host loopback that
drops, duplicates and reorders frames. ESP-NOW arrives in P7 behind the same
`Transport` seam, and §12 of the LIMITS says exactly what that leaves unproven.

Spec §15 has two governing sentences and everything here serves them:

> Never transmit raw unvalidated game objects and trust the peer.
>
> The same validator used for custom Pebbles should be used for exchanged
> Pebbles.

---

## 1. The frame

`PROTO_HDR_BYTES` = 14, then `payload[len]`, then a 2-byte trailing CRC. Every
multi-byte field is little-endian, **written and read byte by byte with explicit
shifts — never a struct memcpy**. The contrast with `battle_state_hash()` is
deliberate: that one *is* a memory-image hash and is valid only between
little-endian builds of the same struct, and a codec that inherited the same
property would make a host test prove nothing about the device.
`tests/test_protocol.cpp` pins one frame and one 48 B record as **golden byte
arrays**, which is what catches an endianness shortcut a grep could not.

| off | sz | field | rule |
|---|---|---|---|
| 0 | 1 | `version` | `== 1`, `static_assert`ed equal to `core/version.h`'s `PROTOCOL_VERSION` |
| 1 | 1 | `type` | 1..12, §15's order. **0 is reserved**, so an all-zero buffer is not a frame |
| 2 | 1 | `flags` | `PF_RETX 0x01` — "this is a retransmission", **diagnostic only, never branched on**. Every other bit must be 0 |
| 3 | 1 | `round` | 1..60 for BATTLE_STATE, ACTION, ACTION_RESULT and ROUND_RESULT; **0 for the other eight**. A per-type rule in the same table as the length |
| 4 | 4 | `session` | 0 on HELLO (which predates the session); the session id on the other eleven |
| 8 | 2 | `seq` | per-sender, **fresh on every frame including retransmissions** — see §4 |
| 10 | 2 | `ack` | **diagnostic only**: carried, logged, never compared. `test_protocol.cpp` fuzzes it across a whole round trip |
| 12 | 2 | `len` | payload bytes, `<= PROTO_PAYLOAD_MAX` |
| 14 | len | payload | |
| 14+len | 2 | `crc16` | CRC-16/CCITT-FALSE over bytes `0 .. 14+len-1` — **the whole frame including the header** |

`PROTO_PAYLOAD_MAX` is **derived, not chosen**: it is `max(PROTO_LEN_OF[])` =
**148**, computed at compile time, with
`static_assert(14 + 148 + 2 <= 250)` for ESP-NOW. A cap picked by hand at 200
would reserve 52 bytes of attack surface for messages that do not exist.

**Why the CRC is last.** `crc16_ccitt(p, n)` is one-shot with no continuation
seed. A header-embedded CRC splits the covered span into two runs and would need
a scratch copy or a new core API. Trailing gives one call over exactly the bytes
received, puts the header *inside* the span — an attacker cannot flip `type` or
`session` for free — and matches the tree's own convention
(`PebbleInstance.crc16` at 126 over 0..125, `PendingTrade` at 62 over 0..61).

### Decode order, pinned. Each step uses only fields already proved.

1. `n` — **the transport's** byte count, never a frame field — in [16, 164] → `PE_SHORT` / `PE_OVERSIZE`
2. `len` at 12..13, `<= PROTO_PAYLOAD_MAX` → `PE_OVERSIZE`
3. `14 + len + 2 == n` **exactly** → `PE_LEN` (not `>=`: trailing bytes are a covert channel)
4. the trailing CRC over `buf[0 .. n-3]` → `PE_CRC`
5. `version == 1` → `PE_VERSION`
6. `1 <= type <= 12` → `PE_TYPE`
7. no reserved flag bit → `PE_FLAGS`
8. `len == PROTO_LEN_OF[type]` → `PE_LEN_FOR_TYPE`
9. the header round obeys the per-type rule → `PE_ROUND`
10. `session`, HELLO excepted → `PE_SESSION`
11. the payload's reserved bytes, and TEAM_SUBMIT's count and padding → `PE_RESERVED` / `PE_COUNT_RANGE` / `PE_TEAM_PAD`

Step 2 is the one field read before the CRC; it is bounded against a
compile-time constant before use and is never used as an index. Step 3
cross-checks a frame field against a quantity the frame did not supply, which is
what makes `len` a **check** rather than a trusted input. **The decode is total**
and never partially writes its output: `out` is memset on every reject, which
the tests prove by poisoning it with `0xA5` first.

**The encoder holds the same rules, and that is asserted rather than assumed.**
`proto_encode()` applies the type, flag, round, count, reserved and
HELLO-session rules, so a frame it emits is one `proto_decode()` accepts when
given the session the frame names —
`every_frame_the_encoder_emits_its_own_decoder_accepts` sweeps the twelve types
and then fuzzes the header. **That case did not exist until the P4-C5 follow-up,
and when it was written the claim it checks was false:** the encoder applied no
session rule at all, so a HELLO with a non-zero session encoded happily and step
10 then refused it for *every* `expect_session` — a frame deliverable to nobody.
A review's encoder fuzzer found 198 of them in 2,000,000 random messages, all
the same class; with the rule removed again, the case written here reproduces it
**35 times** across its sweep and its 40,000 fuzz iterations. Nothing was live
(`session.cpp` forces the HELLO session to 0); the defect was a sentence wider
than the tree, with a citation to a named test that did not exist anywhere in the
repository.

### The twelve messages, at twelve fixed lengths

| # | type | len | payload |
|---|---|---|---|
| 1 | HELLO | 12 | `u32 device_id, u32 hello_nonce, u32 reserved` |
| 2 | CAPABILITIES | 16 | `u8 wire_ver, team_max, level_max, rsv; u16 engine_ver, hash_ver, content_ver, max_payload; u32 hash_basis` |
| 3 | SESSION_REQUEST | 12 | `u32 nonce_a; u8 team_count, lvl_lo, lvl_hi, rules(0); u32 rsv` |
| 4 | SESSION_ACCEPT | 12 | `u32 nonce_b; u8 verdict, team_count, lvl_lo, lvl_hi; u32 rsv` |
| 5 | TEAM_SUBMIT | 148 | `u8 count(1..3), rsv; u16 team_crc; PBW slot[3]` |
| 6 | TEAM_VALIDATION | 8 | `u8 verdict(VReject), bad_index; u16 team_crc_echo; u32 rsv` |
| 7 | BATTLE_STATE | 12 | `u32 open_hash; u8 phase, outcome; u16 rsv; u32 rsv` |
| 8 | ACTION | 8 | `u8 kind, index; u16 rsv; u32 open_hash` |
| 9 | ACTION_RESULT | 8 | `u8 reject(BattleReject), kind_echo, index_echo, rsv; u32 open_hash` |
| 10 | ROUND_RESULT | 12 | `u32 hash_before, hash_after; u8 outcome, rsv[3]` |
| 11 | BATTLE_END | 16 | `u8 reason, outcome, detail, rsv; u32 final_hash; u16 rounds; u16 rsv; u32 rsv` |
| 12 | GOODBYE | 4 | `u8 reason, rsv[3]` |

**No message has a variable length**, which is what makes step 8 an equality and
the fuzzer's accept count computable. **Every reserved field must be zero on
ingest**: a reserved byte carrying a value is a field from a version we do not
speak, and accepting it is how a forward-compatible decoder becomes a lying one.

TEAM_SUBMIT is fixed at 148 whatever `count` is, and slots at or above `count`
must be all-zero. `count` is authoritative for how many records are decoded; the
zero rule is a cross-check, not a second source of truth. `count` outside 1..3 is
`PE_COUNT_RANGE` — a protocol-layer **indexing precondition**, not a game rule,
so it does not duplicate `VR_TEAM_SIZE`.

`CAPABILITIES.hash_basis` carries `battle_hash_basis(engine, hash, content)`
**alongside** the three words. The pair catches two different things for free:
the words catch a nameable content or engine skew, and *words equal but basis
different* catches a build where a version word failed to reach the hash — which
is exactly the bug the P4-C2/C3 follow-up fixed and which nothing else in this
protocol can see.

---

## 2. The wire Pebble — 48 bytes, and 64 is not available

`PBW_BYTES` = 48, `static_assert`ed equal to `TR_WIRE_BYTES`. That constant lives
inside `PendingTrade`, a **persisted** 64 B blob pinned by four `static_assert`s,
and `save_schema.h`'s own rule is that a persisted layout is never edited in
place. §15's "the same validator" sentence makes the battle record and the trade
record one object, so choosing 64 now would mean two wire forms across time.

| off | sz | field | rule on ingest |
|---|---|---|---|
| 0 | 2 | `magic` | `PBW_MAGIC` — **not** `PEBBLE_MAGIC`: a 48 B wire record and a 128 B save blob must not answer to one magic |
| 2 | 1 | `wire_ver` | `PBW_LAYOUT_VER` — its own number, not the protocol's and not the save schema's |
| 3 | 1 | `species_id` | a real row; 200..209 refused with its own code |
| 4 | 4 | `id` | non-zero, distinct across all six members |
| 8 | 2 | `hp_cur` | `1 <= hp_cur <=` the **derived** hp_max |
| 10 | 2 | `xp` | below what this level costs; 0 at level 30 |
| 12 | 1 | `level` | 1..30 |
| 13 | 1 | `reserved0` | **must be 0** — this is where `evo_state` would have gone |
| 14 | 1 | `status` | `PBS_SICK | PBS_ASLEEP` only |
| 15 | 1 | `flags` | `PBF_CUSTOM` / `PBF_HAS_CUSTOM_SPRITE` refused |
| 16 | 1 | `origin` | `< ORIGIN_COUNT` |
| 17 | 1 | `trait_id` | must be 0 — no traits table exists, so a value is uninterpretable |
| 18 | 4 | `moves[4]` | four real attacks, **and** the verbatim in-order learnset of some species in the same family at this stage or below |
| 22 | 16 | `genome` | the sealed 16 B object whole, field by field, `genome_valid()` applied |
| 38 | 8 | `reserved[8]` | every byte 0 |
| 46 | 2 | `crc16` | over bytes 0..45 |

**`evo_state` is not transmitted.** It is *exactly* derivable — the stage bits
are `species_get(id)->stage` and the PENDING bit is `evolution_level_ready()`, a
pure function of species and level — so the receiver computes it from **its own**
tables and §15's "invalid evolution state" is answered by **inexpressibility**
rather than by a check. One behavioural consequence, stated: a Pebble may arrive
with PENDING **set** where the sender had it clear.

**Not transmitted at all:** `nickname[13]`, `care[5]`, `care_rem[5]`, all four
epochs, all five lifetime counters, `creation_seed`, `custom_sprite` and the save
bookkeeping. The nickname is the one deliberate *feature* loss and the reason is
memory safety: `ui/screen_battle.cpp:156` hands it out as a bare `const char*`
and `ui/pet_view.cpp:150` snprintf's `"%s"` from it — both cap the **output** at
13 and read the **source** to NUL — and the only nickname *writer* in `src/` is
`persistence/migration.cpp:221-226`, whose loop is bounded by
`o + 1 < sizeof p.nickname` and always stores the NUL, so a peer would be the
first producer of an **unterminated** one. (This read "nothing in `src/` writes a
nickname today" until the P4-C5 follow-up, which was wider than the tree; the
load-bearing half survives the correction.) Not
transmitting it deletes the class instead of guarding it, and after this **every
wire field is a bounded integer and there is no string parsing on the ingest
path at all**.

**The decoder produces a complete `PebbleInstance`, not a wire object.**
`pbw_decode()` fills a **local**, derives `evo_state`, stamps the save magic,
runs `validate_pebble()` — the one validator, one call, no policy flag — and
copies to the caller **only** on `VR_OK`, memsetting on any reject. There is no
window in which a caller holds a half-trusted instance, and the wire path
**refuses where `ui/screen_battle.cpp:205`'s `copy_from_box()` mends**: that is
defensible for a Pebble this device created and not for one a peer sent.

**The local team goes through the same decoder.** `session_set_team()` encodes
this device's own Pebbles to the 48 B form and `link_begin()` decodes them back
into the setup, so the two endpoints' 780 B `BattleSetup`s are **byte-identical**
and the lockstep rests on an object both sides built the same way.
(`the_local_team_enters_the_engine_through_the_decoder_the_peer_s_team_uses`.)

---

## 3. The state table

**States:** `SS_IDLE, SS_HELLO, SS_CAPS, SS_SESSION, SS_TEAM, SS_VERIFY,
SS_BATTLE, SS_ENDING, SS_CLOSED`. One terminal state; *why* is
`SessionEnd.reason` ∈ `SE_DONE, SE_LOST, SE_DESYNC, SE_REJECTED, SE_PROTOCOL,
SE_INCOMPATIBLE, SE_LOCAL_CANCEL`.

**Roles.** The **lower `device_id` is the initiator and plays side A (0)**;
equal ids are `SE_PROTOCOL(SD_SELF)`, because a peer that echoes our HELLO
verbatim makes us our own peer and the seed mix would be symmetric, so the
reflection would be invisible. `session = FNV(min(idA,idB), max(idA,idB),
initiator_hello_nonce)` — computed identically by both sides with no extra round
trip, so a retransmitted HELLO is recognised rather than starting a second
session. Whoever *speaks* first is irrelevant.

**Four rules run before the table, which is what keeps it small.**

* **R1** A frame the codec refused never reaches the FSM and **never touches a
  timer**. Otherwise a malformed flood keeps a dead session alive for as long as
  the attacker keeps typing.
* **R2** A frame for another session is dropped before the FSM (HELLO excepted).
  `proto_decode()` does this itself, given the session id the endpoint holds.
* **R3** A legal message that is not expected in this state is **dropped and
  counted**, never aborted on.
* **R4** **Only progress touches the ladder.** A duplicate, a re-acknowledgement,
  a stale frame and a wrong-state frame all leave it where it was. **Every
  handler has to guard its own repeat for this to be true, and one did not:**
  `on_action_result()` treated *every* ACTION_RESULT matching our outstanding
  ACTION as progress — including the second and every one after, which is
  precisely what an honest peer regenerates from state. Measured before the
  guard: one legal frame replayed into a cut link, at a spacing the **attacker**
  chose, kept the session alive for 1,019 injections and seventeen hours of
  virtual time and then ended `SE_PROTOCOL(SD_RX_BUDGET)` with `tx_retx == 0` —
  neither the `SE_LOST` LIMIT 2 promises nor the `tx_retx == 9` the abort
  record's own signature relies on. It is leashed now, and
  `one_legal_frame_replayed_forever_cannot_hold_the_session_open` replays a
  BATTLE_STATE in the same shape as a control that always obeyed the rule.

**The ladder.** `PROTO_RETX_MS 1000` × `PROTO_RETX_MAX 9`. The plan asked for
3 s × 3; this is the same nine-second deadline spent as nine attempts. **Measured
on the same 500 seeds per arm, changing only the two constants:**

| arm | 3 × 3000 ms | 9 × 1000 ms |
|---|---|---|
| 10 % drop | 483 completed, 17 `SE_LOST` | **500 completed, 0 lost** |
| 10 % drop + dup + reorder | 482 completed, 18 lost | **500 completed, 0 lost** |
| 30 % drop, 20 % reorder | 108 completed, 391 lost, 1 half-paid | **471 completed, 28 lost, 1 half-paid** |

The design predicted worse than that for three rungs — it reasoned that an
obligation needs its frame out and the clearing reply back (≈0.81 per attempt),
so three attempts leave 6.9e-3 per obligation against ~240 obligations, and
therefore that *most* battles would abort. **The measurement says 3.4 %, not
most, and the reason is worth writing down:** the answer-from-state rules mean an
obligation rarely spends its own ladder alone. A peer that is behind pulls what
it needs from the peer that is ahead, so two ladders and several pull paths
cooperate on every gap, and the independent-obligation arithmetic is pessimistic.
Nine rungs is still the right number — it is the difference between 17 aborts and
0 at the stated fault level, and between 108 and 474 completions at the harsh one
— but the number that justified it is a measurement and not the estimate.
(The harsh arm read 474 before the P4-C5 follow-up closed the R4 hole above.
Three trials of five hundred had been completing on a ladder that a
re-acknowledgement wrongly reset, and they are not a loss worth keeping.)

**Retransmission is by regeneration.** Nothing stores a transmitted frame; the
session re-encodes what its own state says it owes — **exactly one frame per
rung**, and each of the three is both what we owe and the pull the peer needs.
The agreed design also sent a BATTLE_STATE alongside from rung 3 onward, to
"turn a nine-second silence into an early named desync". **That branch is
deleted and its constant with it**, because no case could be written that it
makes pass: an outstanding ACTION already carries this round's open hash and a
mismatch is `SD_OPEN_HASH` before any probe, an outstanding ROUND_RESULT already
carries both of that round's hashes, and the third arm sends a BATTLE_STATE
anyway. Its one **measured** effect was liveness, not detection: the harsh
acceptance arm moved from 487 to 474 completions of 500 when it was removed, at
a fault level three times the stated criterion, and 500/500 either way at the
stated one. It was removed rather than kept behind a corrected comment because a
branch whose claim no test can hold is the defect this project keeps finding —
and the 13 trials it cost are recorded here rather than rounded away.

**A message from an earlier phase — or an earlier round — is ANSWERED with the
reply we already gave, regenerated from state, and advances nothing.** This is
the single most load-bearing rule in the file and it was measured into
existence: before it, a link that **dropped nothing at all** and only reordered
ended **472 of 500** acceptance trials in `SE_LOST`, because a CAPABILITIES that
overtook its HELLO was dropped as out-of-state and the peer then waited out its
whole ladder for a frame that had already been delivered and discarded.

### Per state

**SS_IDLE** — `session_start()` sends HELLO → SS_HELLO. *rx HELLO, peer id ≠
ours*: derive the session and the roles, send HELLO and CAPABILITIES → SS_CAPS.
*rx HELLO, peer id == ours*: `SE_PROTOCOL(SD_SELF)`. Everything else carries a
non-zero session that R2 already dropped.

**SS_HELLO** — *rx HELLO*: as above → SS_CAPS. *rx GOODBYE / BATTLE_END*:
`SE_LOST(SD_PEER_GOODBYE)`. *ladder expires*: `SE_LOST`.

**SS_CAPS** — *rx CAPABILITIES, all eight words equal*: the initiator sends
SESSION_REQUEST, the responder waits → SS_SESSION. *a named word differs*:
GOODBYE, `SE_INCOMPATIBLE(word)`. *words equal, basis differs*:
`SE_PROTOCOL(SD_BASIS_SKEW)`.

**SS_SESSION** — *responder rx SESSION_REQUEST, band acceptable*: SESSION_ACCEPT
**and immediately, unprompted, TEAM_SUBMIT** → SS_TEAM. *band unacceptable*:
SESSION_ACCEPT(verdict), GOODBYE, `SE_REJECTED(SD_BAND)`. *initiator rx
SESSION_ACCEPT(0)*: TEAM_SUBMIT → SS_TEAM. *initiator rx SESSION_REQUEST*:
dropped and counted — both-initiator glare is forbidden by the device-id rule, so
it is a modified or reflected peer, not a race.

**SS_TEAM** — *rx TEAM_SUBMIT, first*: `pbw_decode` × count → `validate_team` →
`validate_level_band` → `validate_battle_ready` → **the cross-team id check** →
TEAM_VALIDATION carrying the verdict. The three per-team rules run before the one
that spans both teams, so a team with two defects is named by its own. A non-`VR_OK` verdict also sends GOODBYE
and closes `SE_REJECTED(VReject)` with the member index. *rx TEAM_SUBMIT again,
same `team_crc`*: re-send the same TEAM_VALIDATION. *different `team_crc`*:
`SE_PROTOCOL(SD_TEAM_CHANGED)`. *rx TEAM_VALIDATION(OK) echoing our crc*: when
both verdicts are in, seed the battle and → SS_VERIFY. *`battle_init()` refuses a
team that whole chain accepted*: `SE_PROTOCOL(SD_INTERNAL)` — **a bug in this
tree, never a peer capability, and the peer is not blamed**.

**`validate_battle_ready` is the last link in that chain and it was missing.**
`hp_cur == 0` is a legal thing to have **stored** and an illegal thing to bring
to a battle, so `validate_pebble()`, `validate_team()` and `pbw_decode()` all
answered `VR_OK` for it and `battle_init()` answered `BR_MEMBER_FAINTED` — and
the driver above turned that into `SE_PROTOCOL(SD_INTERNAL)` with
`bad_index == 0xFF`. That is **this device recording a bug in its own validator
for a lie the peer told**, byte for byte the class the cross-team id check had
already fixed once, and it fired with no adversary at all: an honest player whose
own team held a fainted Pebble got `VR_OK` from `session_set_team()` and then
watched **both** endpoints close `SD_INTERNAL`. `PBS_FAINTED` cannot make the
trip (`VR_WIRE_STATUS_BITS` refuses it), so `hp_cur` was the single field that
got through. The rule is now named `VR_MEMBER_FAINTED`, it runs on the **local**
team in `session_set_team()` before a frame is sent and on the **peer's** in
`on_team_submit()`, and `game/validate.h`'s containment claim is now the strong
one: after the whole chain, `battle_init()` returns `BR_OK` — asserted over
36 species × 5 levels × 8 hostile variants by
`a_battle_ready_team_is_one_the_engine_accepts_outright`.

**SS_VERIFY** — the round-1 agreement barrier. *rx BATTLE_STATE(1), open hash
equal* → SS_BATTLE. *differs*: BATTLE_END, `SE_DESYNC(SD_SETUP)`, no rewards.
*rx ACTION(1)*: the peer has already matched, so the barrier is lifted and the
action takes the ordinary path. *rx BATTLE_END*: it carries a REASON and is read
as one — the peer telling us it found a desync is recorded as `SE_DESYNC`, not
flattened into `SE_LOST`, which is the one code that exists to mean the opposite.

**SS_BATTLE** — see §4.

**SS_ENDING** — *rx BATTLE_END, outcome and final hash agree*: **rewards commit
here and nowhere else**, GOODBYE is sent, and the endpoint **stays** in
SS_ENDING until the peer's GOODBYE. *either disagrees*:
`SE_DESYNC(SD_END_DISAGREE)`, no rewards. *rx GOODBYE*: `SE_DONE` if we already
agreed, otherwise counted and ignored — a GOODBYE that overtook the BATTLE_END
it follows proves nothing about the battle. *ladder expires*: `SE_DONE` if we
agreed, `SE_LOST` otherwise.

**Once the rewards are committed, the terminal is `SE_DONE` — whatever reaches us
next.** The rule lives in `session_close()`, which is the one place a terminal is
decided, so "`session_rewards_authorised()` is true only where `reason` is
`SE_DONE`" is structural rather than a habit of the callers. It was not, until
the P4-C5 follow-up: `on_battle_end()` took the peer's own reason byte without
consulting `paid`, so a peer that had **already made us pay** could then choose
our label — a forged `BATTLE_END(SE_DESYNC)` carrying a final hash we never
computed closed us `SE_DESYNC` **while paid**, and a `BATTLE_END` with any other
reason closed us `SE_LOST` while paid. A flood that exhausted `SESSION_MAX_RX`
after the agreement did the same with no `BATTLE_END` at all, which is why the
fix could not live in `on_battle_end()`. A caller following LIMIT 1
(`if reason == SE_DONE`) and one following the header
(`if rewards_authorised()`) disagreed about the same session, and the **peer**
picked which. The label is what is corrected and not the payment: `paid` is set
only by **our own** comparison of the peer's outcome and final hash against ours,
after our own engine finished the battle, and nothing that happens afterwards can
un-decide that. Refusing to pay instead would hand an attacker a free denial for
the price of one frame.

**SS_CLOSED** — terminal; every type is dropped.

### The abort record

A fixed **40 B `SessionEnd`** written on the way into SS_CLOSED, plus an 8 B
`LinkEvent` ring the caller sizes (16 entries on the device, thousands in a
test), with a saturating `dropped` like `BattleLog`'s. It carries the session,
both hashes, the round, the state, the role, the reason, a reason-tagged detail
byte, the type we were owed, and six counters: `rx_ok`, `rx_dup`, `rx_stale`,
`rx_gap`, `rx_wrong_state`, `rx_reject`, plus `tx_frames` and `tx_retx`. That is
what makes every terminal answerable **without a rerun**: `SE_LOST` shows
`tx_retx == 9` with `peer_hash == 0` and names the frame and round it died on;
`SE_DESYNC` shows both hashes side by side; `SE_REJECTED` shows a `VReject` and a
member index.

---

## 4. The lockstep, and the three invariants

Everything rests on one structural fact: `battle_step_round()` returns
`BS_NEED_ACTIONS` **with the state bit-identical** when either `pending_kind` is
`BACT_NONE`. So *attempting* to resolve is a free, idempotent no-op, and the
protocol never schedules resolution — it simply tries after every accepted
ACTION, every local submit and every barrier that comes down.

**INV-1 (one-round window).** At every point where `session_poll()` returns,
`|round_A − round_B| <= 1`, and when they differ the side at the higher round has
its barrier **raised**. Corollary: a frame for `round − 1` is the oldest thing
that can legitimately arrive, which is what makes the answer-from-state rule
finite and a one-round memory provably sufficient. A frame for a **future** round
is `SE_PROTOCOL(SD_ROUND_GAP)`; an old one is ordinary on a reordering link and
is answered, not aborted on.

**INV-2 (nothing advances unmatched).** A side resolves round *N* only when its
own action is submitted, it has accepted an ACTION whose header round is *N* and
whose `open_hash` equals its own, and — for *N > 1* — round *N−1*'s ROUND_RESULT
arrived with **both** hashes matching. `open_hash` is defined exactly:
`battle_state_hash(st)` at the instant `st.round == N` **and both pendings are
`BACT_NONE`**. It is the one 212-byte object both peers demonstrably agree on,
and it is why an ACTION can be self-authenticating against state without either
peer ever sending state. **A retransmitted ACTION carries the open hash of its
own round**, never of the round the sender has since moved to — before that
field existed the 10 % drop arm reported 263 `SD_OPEN_HASH` desyncs in 500 trials
on a link that never corrupted a byte.

**INV-3 (agreement or declaration).** At the moment both endpoints are terminal,
**either** both carry `SE_DONE` with equal outcome, equal
`battle_state_hash()` and `memcmp(&st_a, &st_b, 212) == 0`, **or** at least one
is not `SE_DONE` and neither wrote its Box. Any other pair is a silent
divergence, and `CHECK_EQ(tally[LE_SILENT], 0)` is the assertion the acceptance
run makes.

**Three hashes, three different questions.** `ACTION.open_hash` — we entered this
round already disagreeing (or this is a stale or replayed ACTION).
`ROUND_RESULT.hash_before` — we agreed on the state and I received a different
ACTION than you sent (the hash is taken *after* both pendings are written, so it
already commits to the action pair). `ROUND_RESULT.hash_after` — we agreed on the
inputs and disagree on the **rules**. Twelve bytes per round per side, and it
buys **nothing** against a liar.

**Never substitute a missing action.** Not on a timeout, not ever, and it is
impossible in principle rather than merely undesirable: there is no pass action,
and even a fully deterministic rule such as "timeout ⇒ attack slot 0" fails,
because the timed-out peer may have slot 0 on cooldown, so the two engines would
compute different legality and desynchronise **on the substitute**.

**The sequence number is a de-duplication window, not the replay defence.**
Every frame gets a **fresh** seq, retransmissions included (they carry `PF_RETX`,
which is diagnostic). So a repeated seq means exactly one thing — the transport
delivered the same frame twice — and dropping it is free and correct. A 32-frame
sliding window also names a **gap** (a seq above the expected one) and a **stale**
frame (below the window). *This deviates from the agreed design, which said a
retransmission reuses its seq verbatim: the two rules cannot both hold, because a
window that drops duplicates would then discard exactly the retransmissions the
ladder depends on.* A seq more than `SESSION_SEQ_MAX_JUMP` (64) ahead is refused
**without moving the window** — a host test found that one forged frame carrying
a seq thousands ahead otherwise deafened the endpoint permanently.

---

## 5. The transport seam

```c
struct Transport {
  void*    ctx;
  bool     (*send)(void* ctx, const uint8_t* frame, uint16_t n);
  uint16_t (*recv)(void* ctx, uint8_t* buf, uint16_t cap);   // 0 == nothing
  uint16_t mtu;
};
```

A struct of function pointers from a factory — not virtual, no heap, no RTTI —
and the choice is a **runtime value the caller passes in**, which is what removes
the `#ifdef` from `session.cpp` and `battle_link.cpp`. `ctx` is what lets one
process run two endpoints.

**Datagram, never a stream.** `recv()` returns the exact byte count of one frame,
and the decode order uses that count as the quantity the frame did not supply —
over a byte stream `len` would have to be believed and this codec provides no
reframing. **Non-blocking and poll-driven, with no callback into session code**:
an ESP-NOW receive callback runs on the Wi-Fi task and letting it call
`session_poll()` would put the battle engine on another stack. **`mtu` is data,
not a `#define`**, so a test sets it to 60 and drives the oversize path without
recompiling. `send()` returns bool and carries no radio vocabulary, because the
session's reaction to a refused send is identical to its reaction to a dropped
frame — which is the property that makes an injected loopback drop and a real
radio failure exercise the same path.

`transport_loopback.cpp` is one implementation: two in-process queues, a
configurable per-frame drop / duplicate / reorder-window percentage and a
scripted "kill the next N frames of this named type" fault, all drawn from **one
seeded `Rng`** so a failing trial reproduces from its printed seed alone.
`transport_espnow.cpp` is P7's and will be the only file in the tree that
includes `esp_now.h`; it is excluded from the host build **by file selection**.

---

## 6. Budget

Nothing in `app/` or `ui/` references these modules yet, so the ESP32 link
(`--gc-sections`) drops them entirely: flash and globals are **unchanged**
against the previous commit at 1,915,696 / 72,676, confirmed by
`riscv32-esp-elf-nm` finding zero `proto_`, `session_`, `link_` or `pbw_` symbols
in the ELF. When P7 wires it: `Session` is **340 B on the device** (it already contains the
40 B `SessionEnd` and the 144 B of frozen wire records it must be able to
retransmit), plus a 16-entry `LinkEvent` ring at 128 B — **468 B**, all
caller-owned, about 2.7 % of the 17,324 B of globals headroom. The 340 is
measured by compiling this header with `riscv32-esp-elf-g++`; the **host** figure
is 360, and this line said 360 until the P4-C5 follow-up — a host number wearing
a device number's clothes, conservative but wrong. `sizeof(SessionEnd) == 40`,
`sizeof(LinkEvent) == 8` and `sizeof(LoopbackLink) == 8020` are identical on both,
and `tests/test_session.cpp` asserts only the *bound* on `sizeof(Session)`
because an equality there would pin the host's alignment and claim nothing about
the target. The lockstep holds **no**
`BattleState` and **no** `BattleSetup` of its own — it takes both by reference
and decodes the peer's team straight into `setup->member[peer_side][]`, so the
linked path borrows the one `ui/screen_battle.cpp` already owns at file scope.
`LoopbackLink` is about 8 kB and is **test-only**: nothing on the device
declares one.

---

## 7. LIMITS — what a malicious peer can still do

Understated deliberately. Each entry says what the code does and what the player
sees.

1. **A peer that lies about a hash cannot be caught and cannot be attributed.** A
   forged ROUND_RESULT is *exactly* indistinguishable from a genuine divergence:
   no third party, no signature, no shared secret. **What happens:** `SE_DESYNC`
   on the round the mismatch is seen; no XP, no `battles_won` increment, no hp
   write-back, no checkpoint, both Boxes untouched, on **both** sides. **What the
   player will see, in P7:** a neutral "the battle could not be agreed" with the
   round number — never "the other player cheated", because the protocol cannot
   make that claim. Written in the future tense on purpose: nothing under `app/`,
   `ui/` or `persistence/` calls into this module at this commit, so the screen
   is P7's the same way the radio is, and the same applies to "the caller pays XP
   and writes the Box". A lying peer's guaranteed power is **denial**: it can burn any
   battle at will. It cannot move our state, because it never supplies state —
   only two bytes of intent and a digest we compare against our own.
2. **A peer that stalls forever** gets nine attempts at one second per
   obligation, then `SE_LOST` naming the obligation and the round, with no
   rewards. **This was false for one message type until the P4-C5 follow-up** —
   see R4 in §3: a peer that said nothing else and replayed one legal
   ACTION_RESULT reset the ladder every time, so it held the session open for as
   long as it kept typing and ended `SE_PROTOCOL(SD_RX_BUDGET)` with
   `tx_retx == 0`. A peer that answers at the last millisecond of every ladder is
   **inside every rule**: the ladder bounds silence, not slowness, and there is
   no per-round time budget and **no total-session deadline** — a slow but alive
   link can drag a 60-round battle out for minutes. The cheapest fix is a
   `PROTO_SESSION_MAX_MS` and it is deliberately absent because no measurement
   supports a number.
3. **A peer running modified firmware wins by playing legally.** A level-30 team
   beats a level-10 team, and nothing on the wire proves a level was *earned* —
   `xp` is XP inside the current level, not a cumulative total, so there is no
   running figure to check against. "Impossible level" can only ever mean
   "outside 1..30". The level band in SESSION_REQUEST/SESSION_ACCEPT turns that
   from an unbounded legal win into a refusal, **but only because the band was
   agreed**: it is a matchmaking control wearing a protocol field's clothes, and
   a peer can decline a narrow band or send a level-12 team it never raised.
4. **A forged genome is legal and validating it buys zero against that.** The
   gene masks are all `0x0F`, so genome variance is 0..2 **by construction**, and
   `genome_valid()` checks only the signature, the proto version, `lineage_id != 0`
   and the CRC — a forgery resealed with a correct CRC passes all four. The
   genome is validated for lineage and trade integrity and for nothing else.
   **"0..2" is a bound on the range and not a reassurance about the stakes**, and
   the difference is measured: a mirror match at level 10, same species, same
   moves, same level, AI on both sides, 200 seeds per configuration, with one
   side carrying the maximum genome (+2/+2/+2) and the other the minimum
   (+0/+0/+0), gives **594 wins of 600 to the forged side** (species 1 and 2:
   200/200 each; species 3: 194/200) — and it is symmetric across sides, so side
   bias does not explain it. Two points of atk, def and spd is not a rounding
   error at the levels this game is played at. This is LIMIT 3's class, not a
   separate hole: it is a peer winning by sending data that is legal.
5. **The protocol cannot stop a peer cheating itself.** There is no shared ledger
   and no server; a modified peer awards itself the win locally whatever it
   reports. The claim this design supports is §67's actual one — *invalid peers
   cannot inject illegal data into my device* — never "peers cannot cheat".
6. **The CRC is an error detector, not an authenticator**, and the session id
   travels in clear in every frame, so it is a demultiplexing key and not a
   secret. On P7's shared medium an off-path device that reads one frame can
   inject well-formed ones. Its power is bounded to denial — an injected ACTION
   for a round already submitted terminates the session by name rather than being
   applied — but it is real, and there is no fix at this layer without a key
   exchange that §15's message set does not contain.
7. **A chosen-hash "attack" is definitionally free.** The hash is 32-bit,
   non-cryptographic, and a **memory-image** hash valid only between
   little-endian builds of the same struct; the peer knows both teams, both
   actions and the seed. A peer that can compute our hash is a peer running our
   engine, which is what lockstep wanted.
8. **The hash is blind to the step-6 / step-7 order**, and the lockstep inherits
   that blindness. Swapping `battle_s6_process_fainting()` with
   `battle_s7_process_status()` leaves every round hash and the final hash
   byte-identical — measured — because `tick_status()` never reads `BCF_FAINTED`.
   Two peers shipping opposite orders would agree on every hash they exchange and
   produce different transcripts, and CAPABILITIES cannot see it because both
   report the same `BATTLE_ENGINE_VER`. Only `tests/golden/battle_v1.txt` and
   `tools/check.sh`'s third battle gate hold that boundary, and both are **local**.
   Nothing here claims the hash covers "the whole round".
9. **The two-generals problem: a completed battle can pay one side and not the
   other.** Rewards commit only when our BATTLE_END was sent **and** the peer's
   was received with a matching outcome and final hash. If the peer's BATTLE_END
   is lost through all nine attempts, it reaches `SE_DONE` and we reach
   `SE_LOST`; both agree on the outcome and one is paid. This is unfixable over
   an unreliable channel. The design makes the asymmetry always fall on the side
   of **not** awarding, and the acceptance census counts it in its own bucket.
   Measured: with the endpoint closing as soon as it agreed, half-payment
   happened in 88 of 500 trials at 10 % drop and in **18 of 500 on a link that
   only reordered**; waiting in SS_ENDING for the peer's GOODBYE made it need
   nine consecutive losses in one direction, and the same 3,000 trials now show
   zero.
10. **The second mover can bias the seed**, and no commit-reveal in this tree
    would fix it. The seed is mixed from both nonces **and both team CRCs**, so
    neither side fixes it alone, and an honest responder commits its nonce and
    sends its team unprompted before seeing the initiator's. A **modified**
    responder that delays its TEAM_SUBMIT until the initiator's arrives can
    grind. That stall is visible in the event log; it is not prevented. A
    commit-reveal was deliberately not built: the only hashes in this tree are
    CRC-16 and FNV-1a-32, neither one-way, and a 32-bit commitment is
    preimage-searched in seconds.
11. **Whichever TEAM_SUBMIT arrives second was chosen knowing the first.**
    Somebody's packet is physically first and §15 contains no two-phase team
    reveal. The FSM freezes the local team at `session_set_team()`, so an honest
    implementation *cannot* counter-pick; that is the exact boundary of the claim.
12. **Everything here is host-proven and nothing has touched a radio.** The
    loopback's drop/dup/reorder is a **model** of a lossy channel, not a
    measurement of one; a uniform independent 10 % drop is not what a 2.4 GHz
    room does. ESP-NOW's own duplicate suppression, ack semantics, real MTU
    enforcement and callback threading are unobserved until P7. **A green fault
    run here is evidence about the session logic, not about the radio.**
13. **Completion is not promised at arbitrary loss — only "completes or aborts
    named".** Measured over 500 trials per arm: clean 500/500, 10 % duplicate
    500/500, 10 % reorder (window 4) 500/500, 10 % drop 500/500, 10 % of all
    three together 500/500, and **30 % drop with 20 % reorder 471 completed, 28
    clean `SE_LOST` and 1 half-paid**. Silent divergences: **0 in all 3,000
    trials**, and not one trial wrote a Box byte. **Two things about the
    instrument, said here rather than left to be re-derived.** (a) The loopback's
    queue is `LB_QUEUE_CAP 24` deep and a send onto a full queue is counted as a
    drop, so an arm's *effective* loss is its declared loss **plus** that:
    measured 0 of 70,298 sends on the clean arm, 283 of 109,656 at 10 % drop, 192
    of 88,042 on the reorder arm, 540 of 120,856 with all three and 43 of 157,896
    on the harsh arm — under half a percent everywhere, and the census now
    asserts both that an unfaulted link overflows nothing and that no arm lets
    overflow become its dominant fault. (b) The harness advances its virtual
    clock only on a poll round in which **no frame moved anywhere in the
    system**, so an endpoint never climbs its ladder while its peer is
    transmitting something unrelated — which no real device does. The census is
    therefore a measurement of the session **logic** under loss, not of a duty
    cycle.
14. **The anti-exhaustion constants are heuristics.** `SESSION_MAX_RX 1024`,
    `SESSION_MAX_TX 4096`, `SESSION_MAX_REACK_PER_ROUND 8`,
    `SESSION_MAX_HANDSHAKE_REACK 16` and `SESSION_SEQ_MAX_JUMP 64` come from the
    worst honest case plus slack, not from an adversary, because there is no
    radio in this step. `SESSION_SEQ_MAX_JUMP` bounds the **baseline** as well as
    the jumps after it, and that half was missing until the P4-C5 follow-up: the
    rule covered a jump from an *established* last, while the first frame an
    endpoint accepted set that last to whatever it claimed — so the same
    one-packet deafness was still available before the peer had spoken, through a
    HELLO, which needs no session id at all. They bound how long one hostile session holds the link;
    they do nothing to stop the peer opening another immediately. Discovery-level
    rate limiting does not exist and belongs to P7.
15. **The validator's own bugs are the trust boundary, and half of it has no
    second line.** Keeping `battle_init()` as an independent checker means a
    single-guard bug is usually caught — **but only for the rules the engine also
    has**, and only where the two agree about *which layer* owns the answer: the
    faint rule was in the engine and nowhere above it, so the engine caught it
    and the layer above misattributed it (§3, SS_TEAM). The rules that exist only in `validate.cpp` (the genome seal, the xp
    curve, the status masks, the evolution state, the reserved bytes, the wire
    seal) are guarded by nothing but their own tests.
16. **A Pebble's history cannot be checked.** `battles_won`, `age_s`, `trades`
    and every epoch are not on the wire at all, and their stored values are
    checked against nothing, because no rule exists to check them against.
17. **This pre-commits P7's trade path.** A traded Pebble will arrive unnamed, at
    zero care, zero age and zero counters, with `evo_state` recomputed from the
    receiver's tables, and with the custom flags refused. The record is full at
    48 B with nine spare bytes and a nickname needs thirteen, so reversing any of
    it needs a separate message, not a wider record.
18. **The protocol is not forward compatible and does not pretend to be.** Any
    type outside 1..12, any version other than 1, any length other than its
    type's exact length and any non-zero reserved byte is refused. There is no
    downgrade path. P7's trade messages will exceed `PROTO_PAYLOAD_MAX 148` and
    the answer must be a **version bump**, not quietly raising the cap toward 234
    where it breaks ESP-NOW irreversibly.
19. **Nothing is confidential.** Teams cross in clear. A passive listener in P7
    learns the whole roster, both device ids and the session id. Not addressed
    here and not addressable at this layer.
