# Errata — SaveSchema v2

Everything that reaches flash, and nothing else. This document is written from
`Errata/src/persistence/save_schema.h`; that header is the authority, and any
disagreement is a bug in this file. Plan §1.5.

- **Endianness:** little (ESP32-C3, and every blob is memcpy'd whole).
- **Checksum:** CRC-16/CCITT-FALSE over every byte but the trailing `crc16`.
- **Rule:** a persisted layout is never edited in place. Add to `reserved[]`, or
  bump `SAVE_SCHEMA_VERSION` and add a migration plus a test.
- **Guards:** every struct pins its size and its load-bearing offsets with
  `static_assert`, so a compiler or ABI change fails the build instead of the
  field unit.

## 1. Where the bytes live

| | Partition | Namespace | Written by | Erased by |
|---|---|---|---|---|
| `KV_MAIN` | `nvs` (default) | `pbbl` | every save | **the Arduino core**, wholesale, before `setup()` — see D6 |
| `KV_CKPT` | `nvs2` (private) | `pbbl` | `save_checkpoint_all()` | nothing but a factory reset |

`initArduino()` erases the entire default `nvs` partition when
`nvs_flash_init()` returns `ESP_ERR_NVS_NO_FREE_PAGES` or
`ESP_ERR_NVS_NEW_VERSION_FOUND`, before any firmware code runs. `nvs2` is the
answer: the checkpoint written there is what turns that erase from "the pet is
gone" into `LOAD_RECOVERED_CKPT`.

The device side of the key/value seam is `hardware/kv_nvs.cpp` and it is the
only translation unit in the tree allowed to open `Preferences`. The host side
is `tests/fakes/kv_mem.cpp`.

## 2. Partition table (`Errata/partitions.csv`, decision D6)

| Name | Type | SubType | Offset | Size |
|---|---|---|---|---|
| `nvs` | data | nvs | `0x9000` | `0x5000` (20 KB) |
| `otadata` | data | ota | `0xE000` | `0x2000` (8 KB) |
| `app0` | app | ota_0 | `0x10000` | `0x300000` (3 MB) |
| `nvs2` | data | nvs | `0x310000` | `0x10000` (64 KB) |
| `spiffs` | data | spiffs | `0x320000` | `0xD0000` (832 KB, reserved) |
| `coredump` | data | coredump | `0x3F0000` | `0x10000` (64 KB) |

4 MB, no gap, single app image (OTA is a V1 non-goal; updates arrive over USB).
arduino-cli copies a sketch-local `partitions.csv` into the build directory and
esptool flashes that one, so this table is what a unit ends up with. The board
menu still owns the compile-time size ceiling, which is why the FQBN keeps
`PartitionScheme=huge_app` — its 3,145,728 B ceiling is exactly `app0` above.
`tools/build.sh` gates both facts on every build.

## 3. Layouts

### BugInstance — 128 B, keys `pb<slot><copy>` (pair)

| Off | Size | Field | Notes |
|---|---|---|---|
| 0 | 2 | `magic` | `0x4250` = 'P','B' |
| 2 | 1 | `layout_ver` | 1 (the schema version lives in `BoxHeader`) |
| 3 | 1 | `species_id` | 0 empty · 1..199 built-in · 200..209 custom |
| 4 | 4 | `id` | 0 = empty; unique inside the Box |
| 8 | 4 | `creation_seed` | individual variation source |
| 12 | 4 | `birth_epoch` | 0 when the clock was `CAL_UNSET` |
| 16 | 4 | `last_updated_epoch` | drives `box_recover()` and catch-up |
| 20 | 4 | `age_s` | |
| 24 | 2 | `xp` | inside the current level |
| 26 | 1 | `level` | 1..30 |
| 27 | 1 | `evo_state` | bits 1:0 stage in family, bit 7 pending |
| 28 | 20 | `care[5]` | `int32`, milli-points 0..100000, by `CareId` |
| 48 | 10 | `care_rem[5]` | `int16` integrator remainders |
| 58 | 2 | `hp_cur` | max is derived, never stored |
| 60 | 1 | `status` | `PBS_*` |
| 61 | 1 | `flags` | `PBF_*` |
| 62 | 4 | `moves[4]` | AttackId, 0 = empty |
| 66 | 1 | `origin` | `BugOrigin` |
| 67 | 1 | `trait_id` | |
| 68 | 6 | `battles_won/lost`, `minigames_won` | |
| 74 | 2 | `evolutions`, `trades` | |
| 76 | 4 | `lifetime_active_s` | |
| 80 | 16 | `genome` | the v1 `Genome`, copied whole |
| 96 | 13 | `nickname` | NUL-terminated |
| 109 | 1 | `custom_sprite` | `0xFF` = none |
| 110 | 4 | `seq` | pair sequence number |
| 114 | 12 | `reserved` | must be 0 |
| 126 | 2 | `crc16` | over 0..125 |

`CareId` order — HUNGER, HAPPINESS, HEALTH, CLEANLINESS, ENERGY — is
contractual and deliberately differs from the legacy `StatId` order. Translating
between the two is most of what `persistence/migration.cpp` does.

### BoxHeader — 32 B, keys `box0`/`box1` (pair)

`magic 0x5842` · `schema_version` · `active_slot` (`0xFF` = none) · `slot_mask`
(bit s = slot s occupied) · `content_version` · `next_id_counter` ·
`saved_epoch` · `seq` · `protocol_version` · `flags` · `captures` · `battles` ·
`reserved[4]` · `crc16`.

The slot blobs are the truth and the mask is a cache: `save_load_all()` heals a
disagreement in favour of the blobs, and re-points `active_slot` at the first
occupied slot when it points at an empty one.

### ConfigV2 — 256 B, keys `cfg0`/`cfg1` (pair)

`magic 0x5643` · `version` · `time_cal_state` (`TimeCal`) · `device_id` (§43,
drawn once) · `time_cal_epoch` · `last_known_epoch` · `pin_lock_until` · `seq` ·
`creator_pin` · `creator_idle_s` · `flags` · `brightness` · `pin_fail_count` ·
`device_name[13]` · `ap_pass[17]` · `tz[40]` · `dex[15]` · `reserved[137]` ·
`crc16`.

**`dex[15]` was the front of `reserved[152]` until P10-C8**, and it is the whole
persisted wiki: sixty species at two bits each (SEEN, CAUGHT), which
`game/dex.h` asserts against the roster so a content pack that grows the roster
fails to compile rather than dropping the species past the end. **No schema
bump**, on exactly the argument `act_day`/`act_score` used below: an older blob
reads 0 in all fifteen bytes, and an all-zero wiki is *nothing discovered yet* —
which is the correct state for every save written before the commit. The bytes
also round-trip untouched through an older firmware, which writes them back
verbatim from its own struct.

**It is NOT carved out of the runtime `Config`, and the reason is a defect the
suite caught before it shipped.** The first draft took `Config.reserved_b[24]`
instead, and `tests/test_fixtures.cpp` failed: the *v1* fixture — a real
Nottamagochi save — carries latitude and longitude as ASCII in those bytes
(`"41.3874"`, `"2.1686"`). A migrated save would have opened with half the
roster "discovered" out of decimal digits. `ConfigV2` is built fresh by the
migration and has no such history, which is why the wiki lives only there;
`persistence/game_state.cpp` binds it on load and `cfg_to_v2()` carries a
comment saying the runtime `Config` must never write it back.

No Wi-Fi credentials, no cloud token, no coordinates: the device never
associates to a station (spec §68 r5) and talks to nobody's cloud. `flags`:
`MUTE 0x0001` · `SH1106 0x0002` · `SBAR 0x000C` (2 bits at shift 2) ·
`WEB 0x0010` · `BLE 0x0020`.

### Inventory — 32 B, keys `inv0`/`inv1` (pair)

`magic 0x5649` · `version` · `slots` · `seq` · `ledger_epoch` · `xp_ledger[4]` ·
`items[7]` (`{item_id, count}`) · `crc16`. Seven item kinds, not the plan's
twelve: twelve plus the header plus the ledger is 41 B of content in a 32 B
blob, and the 32 B is the load-bearing number (it is what the entry budget
below is computed from).

### CooldownTable — 272 B, keys `cd0`/`cd1` (pair)

`magic 0x4443` · `version` · `n` · `seq` · `act_day` · `act_score` · `rows[32]`
(`{net_hash, until_epoch}`) · `reserved_b[2]` · `crc16`. `net_hash` is the
abstract identity from the scanner: no SSID or BSSID ever reaches flash.

**`act_day` + `act_score` were `reserved_a[4]` until P6-C2**, and they are the
whole persisted half of the daily activity score (spec §25): the UTC day index
the counters belong to, and that day's total, already capped per term. **No
schema bump**, on the same argument `BugInstance.corrupt_until_epoch` was
carved out of `reserved[12]` at P5-C3: an old blob reads 0 in both, day index 0
is 1970 and can never be a real day, so 0 is unambiguously *no day opened yet* —
which is the correct state for every save written before the commit. Nothing in
the load path or `game/validate.cpp` checks a reserved byte for zero, and a
`save_cooldowns()` write is the caller's struct verbatim, so the four bytes also
round-trip untouched through an **older** firmware.

Why not `Inventory`, which the plan's bullet asked for: it is 32 B with zero
padding and zero reserved bytes, so growing it is a `SAVE_SCHEMA_VERSION` bump —
and a bump is not "add a row to `migrate_run()`". `pair_load()` sorted any
non-equal version into `bad` and `load_all_inner()` returned `LOAD_CORRUPT`
before the migration branch was reached, so a bump needed a version-tolerant
pair reader written first and invalidated `box`, `cfg`, `cd`, `cs` and `tr` as
collateral (five blob types that did not change), plus the `ck_*` checkpoint the
SAVE ERROR screen's *Recuperar* offers. That is the right price for a real
widening (`INVENTORY_SLOTS` 7→12); it was the wrong price for four bytes that
were already reserved.

**That paragraph was a prediction and P10-C5 collected on it.** The version
number moved 2 → 3 for spec §31 — the migration machinery has to be *exercised*
before the release, not merely present — and the first thing the bump did was
reproduce, exactly, the failure described above: every played device would have
booted into SAVE ERROR with perfectly intact data. §8 below is what had to be
written to make a bump survivable.

The four per-term counters and the distinct-network set are **not** here: they
are per-boot RAM in `game/activity.cpp`, and that header states exactly what a
power cycle therefore buys (the per-term caps reset; the day's total does not,
so a rebooting player reaches the honest daily ceiling sooner but never higher).

### CustomSpeciesRec — 192 B, keys `cs0`..`cs9` (single)

`magic 0x5343` · `version` · `slot` · `budget_used` · `type` · `compat_group` ·
`base[4]` · `moves[4]` · `name[13]` · `reserved[17]` · `sprite[2][72]` ·
`crc16`. A bad CRC here costs a sprite, never a Bug.

### PendingTrade — 64 B, key `tr` (single)

`magic 0x5254` · `version` · `phase` · `out_id` · `peer_id` · `in_wire[48]` ·
`reserved[2]` · `crc16`. Written BEFORE either side of a trade commits and
resolved at the next boot, so a power cut can neither duplicate nor vaporise a
Bug.

**IT HAS A READER SINCE P7-C4, AND UNTIL THEN IT DID NOT.** `save_load_all()`
loaded the record into `gs.trade` and `grep -rn 'gs.trade' app/ ui/ game/`
returned nothing: the journal was bytes nobody used. `app_setup()` now calls
`boot_trade()` between the load and the pet binding, and `game/trade.h` owns
what each phase means:

| phase found at boot | what happens |
|---|---|
| absent, CRC bad, or `TRADE_IDLE` | nothing — `single_load()` fails and `gs.trade` stays at its defaults |
| `TRADE_SENT` | **roll back**: clear the journal. No Box byte was ever written |
| `TRADE_RECEIVED` | **roll back**: ditto. The peer's record was journalled, never filed |
| `TRADE_COMMIT` | **roll forward, idempotently**: two independent presence tests, each a no-op when already done |

**`in_wire` carries the id the incoming Bug will have LOCALLY, not the id its
sender gave it.** The local id is minted with `box_mint_id()` *before* the COMMIT
record is written, patched into the record at `BUGW_OFF_ID` and the record's own
CRC resealed. Minting it afterwards would produce a *different* id on a replayed
COMMIT — `next_id_counter` moves — and the resolver could not tell "already
done" from "not yet started". `reserved[2]` cannot hold a `uint32_t`, which is
why the id goes inside the record rather than beside it.

**`commit_all()` writes this key, and it did not before P7-C4.** It rewrites a
whole state that flash does not hold — after a migration and after the SAVE
ERROR screen's "Recuperar" — and it wrote five of the six blobs. RAM said IDLE
and flash still held the old record, so the next boot resolved a trade against a
Box restored from a checkpoint that predates it. It writes a sealed IDLE record
rather than erasing the key, because an absent key and a rotted key look
identical to `single_load()` and what the resolver needs to know is "there is
nothing pending", which only a record can say.

**`save_checkpoint_all()` does NOT checkpoint it, deliberately.** The checkpoint
is a snapshot of a *consistent* Box and the write order takes one immediately
after the journal is cleared; a mid-trade checkpoint would be a second, stale
source of truth for one transaction.

**THE ONE RESIDUAL, written down rather than implied:** a `tr` record that is
written cleanly and then ROTS fails `blob_ok`, so `single_load()` leaves
`gs.trade` at IDLE and a half-applied Box would never be finished. The blob is
single-key by design (no `seq`, no second copy to choose between), so pairing it
is not a free change; the window is milliseconds wide.

## 4. Key table

Every key is ASCII and ≤ 15 characters (`NVS_KEY_NAME_MAX_SIZE` is 16 with the
NUL). `save_schema.h` §9 is the only place they are spelled.

| Key | Blob | Partition |
|---|---|---|
| `pb<slot><copy>` | BugInstance | `KV_MAIN` |
| `box0` / `box1` | BoxHeader | `KV_MAIN` |
| `cfg0` / `cfg1` | ConfigV2 | `KV_MAIN` |
| `inv0` / `inv1` | Inventory | `KV_MAIN` |
| `cd0` / `cd1` | CooldownTable | `KV_MAIN` |
| `cs0`..`cs9` | CustomSpeciesRec | `KV_MAIN` |
| `tr` | PendingTrade | `KV_MAIN` |
| `t` | last-seen epoch, 8 B | `KV_MAIN` |
| `ok` | canary, 4 B | both |
| `gl` | the v1 anti-farm ledger, 20 B, carried forward unchanged | `KV_MAIN` |
| `ck_box`, `ck_cfg`, `ck_pb0`..`ck_pb9` | the checkpoint | `KV_CKPT` |
| `save`, `cfg`, `egg`, `anc` | v1 keys, read once by the migration then erased | `KV_MAIN` |

## 5. NVS entry budget

NVS stores 32-byte entries; a blob costs one index entry plus `ceil(size / 32)`
data entries. A 4 KB page holds 126 usable entries and one page of each
partition is always reserved for compaction.

`nvs` is `0x5000` = 5 pages → **504 usable entries**. Worst case, everything
present at once:

| Key | Bytes | Keys | Entries each | Total |
|---|---|---|---|---|
| `pb<slot><copy>` | 128 | 20 | 5 | 100 |
| `box0`/`box1` | 32 | 2 | 2 | 4 |
| `cfg0`/`cfg1` | 256 | 2 | 9 | 18 |
| `inv0`/`inv1` | 32 | 2 | 2 | 4 |
| `cd0`/`cd1` | 272 | 2 | 10 | 20 |
| `cs0`..`cs9` | 192 | 10 | 7 | 70 |
| `tr` | 64 | 1 | 3 | 3 |
| `t` | 8 | 1 | 2 | 2 |
| `ok` | 4 | 1 | 2 | 2 |
| `gl` | 20 | 1 | 2 | 2 |
| namespace index | — | — | — | 1 |
| | | | | **226 / 504** |

The plan's §1.5 quotes 262; that figure predates the seven-slot `Inventory` and
rounded the pair keys differently. 226 is what the committed layouts actually
cost. **It was 231 here until P4-C6**, which included the five entries of the
`lgpet` blob P2-C10 deleted (see §9). Either way the headroom is roughly a
factor of two, which is what matters: NVS needs free entries to compact.

`nvs2` is `0x10000` = 16 pages → **1,890 usable entries**. The checkpoint costs
`ck_box` 2 + `ck_cfg` 9 + ten `ck_pb*` 50 + canary 2 + namespace 1 = **64**.

## 6. Writing

Commit order, per blob (plan §1.5.3):

    construct → validate → serialise → CRC → kv_put the INACTIVE copy of the
    pair → read the bytes back and compare → the new copy is authoritative,
    because it now carries the higher seq

The previous copy is never touched during a write, so a failure at any point
leaves the last good state loadable. Multi-blob transactions (trade) write the
`tr` journal first.

Cadence, all in `config.h`:

| | Period | |
|---|---|---|
| unforced pet write | `SAVE_FULL_PERIOD_S` 300 s | `force=true` means "state changed" and is deferred by the 1 s floor, never dropped |
| key `t` | `SAVE_LASTSEEN_PERIOD_S` 60 s | the RTC mirror is updated every tick and costs nothing |
| checkpoint | `SAVE_CKPT_PERIOD_S` 86400 s | plus level-up, evolution, capture and trade — the events that change what a Bug *is* |

## 7. Loading

`save_load_all()` answers with one of seven results, worst-first:

| Result | Meaning | Writes? |
|---|---|---|
| `LOAD_OK` | everything read back clean | no |
| `LOAD_FRESH` | no save anywhere; build a starter | no |
| `LOAD_MIGRATED` | an older schema was read and converted | yes, the converted state |
| `LOAD_RECOVERED_PAIR` | one copy of some blob was bad; the other served | yes, the repair |
| `LOAD_RECOVERED_CKPT` | `KV_MAIN` had nothing; `nvs2` served | yes, the recovered state |
| `LOAD_CORRUPT` | both copies of the Box or of the active Bug are bad | **no** |
| `LOAD_FOREIGN_NEWER` | a save from a newer firmware | **no** |

The last two are the point of the whole design. The v1 loader answered a
boolean and its caller reacted to `false` by building a fresh pet, which
silently destroyed a recoverable save (audit risk 3). Now the session goes
read-only, nothing is written, and `SCR_ERROR` asks the user: **A: Recuperar**
(restore the `nvs2` checkpoint) or **B: Reset de fábrica** (behind two
confirmations). A save from a newer firmware is offered no reset at all — those
bytes are good data and the fix is a firmware update.

## 8. Migration

`migrate_run(from, out)` is table-driven: adding a version means adding a row,
not a branch. There are two rows: **v1 → v2** (the field map below) and
**v2 → v3** (P10-C5, a no-op transform — see §8.2). The chain is checked at
compile time: `migration.cpp` carries a `constexpr` walk from
`SAVE_SCHEMA_VERSION_V1` to `SAVE_SCHEMA_VERSION` and a `static_assert` that it
lands exactly, so bumping the number without adding a row **fails the build**
rather than turning into `LOAD_CORRUPT` on somebody's device.

| v1 | v2 |
|---|---|
| `gene_species & 7` | legacy family map → `SPECIES_BASE_OF_FAMILY[]`, i.e. one of `{1, 4, 7, 10, 13, 16, 19, 22}` |
| `stage` | `level`: EGG/BABY 1, CHILD 5, TEEN 10, ADULT 15, SENIOR 20 |
| `stat[]`, `stat_rem[]` | `care[]`, `care_rem[]`, reordered `StatId` → `CareId`, values unchanged |
| `Genome` | copied whole |
| — | `moves[4]`: the destination species' learnset, verbatim (P4-C1) |
| — | `hp_cur`: `xp_hp_max()` at the mapped level — a migrated pet arrives at FULL health (P4-C1) |
| `Config.pet_name` when the player typed one, otherwise **nothing** | `nickname` |
| `birth_epoch`, `last_seen`, `age_s` | `birth_epoch`, `last_updated_epoch`, `age_s` |
| `flags` SICK/ASLEEP | `status`; GOD_TAINTED → `flags`; LIGHT_ON is dropped (P3-C2b deleted the light mechanic; `status` bit 0x10 is reserved) |
| `Config.tz/brightness/mute/statusbar` | `ConfigV2` |
| `gl` | untouched: same key, same 20 B layout |

**Three rows of that table were wrong until P4-C6 and are corrected above**,
because this is the document the next migration author reads to learn what the
current one does:

- **"starter species 1..8" was the literal `{1,2,…,8}` table P4-C1 deleted as a
  defect.** It landed legacy family 1 on a mid-stage creature and families 3..7
  on species ids with no row at all (`species_get()` → `nullptr`). The map is
  `SPECIES_BASE_OF_FAMILY[(gene_species & 7) % SPECIES_FAMILY_COUNT]`, which is
  a family BASE stage by construction.
- **`moves[]` and `hp_cur` were missing from the table**, and they were added to
  `migrate_v1_to_v2()` by P4-C1 precisely because their absence was silent: a
  migrated pet had four move ids of 0 and an HP meter pinned at 0 % for ever.
- **The "else the deterministic dynasty name" arm is gone.** The P4-C4 follow-up
  deleted `migrate_default_name()`; a v1 pet whose owner never typed a name now
  arrives with an EMPTY nickname, so `ui_pet_name()` falls to its species-name
  rung and the creature can say what it is — and what it becomes when it
  evolves, which was the whole point. The cost is stated at
  `persistence/migration.cpp`: the v1 dynasty word is gone from that device for
  good, and V1 has no rename screen to put it back.

The migration writes nothing itself: committing the result and erasing the v1
keys is `save_load_all()`'s job, so a power cut in the middle leaves the v1 save
intact and the migration simply runs again. The v1 blobs reach `pbbl` through
the one-shot import in `kv_begin()` (decision D3).

### 8.1 A step has two shapes

`migrate_run()` hands each step the same `GameState&`, and what a step does with
it depends on where it starts.

- **v1 → v2 fills it.** v1 lived under its own keys (`save`, `cfg`) with its own
  magics and its own layouts, so the step reads those keys and writes a complete
  v2 state over whatever was in `out`.
- **Every step from v2 onwards transforms it in place.** Those generations share
  every key, every magic and every layout, so the caller has already loaded the
  state and the step's job is to change fields and re-seal. `out` must therefore
  already hold the loaded save when `from` ≥ 2, and is ignored on entry when
  `from` is 1.

### 8.2 v2 → v3 (P10-C5): the bump, and what it cost

**Not one field moves.** Every struct has the same size, the same offsets and
the same meaning in v2 and v3; the `static_assert`s in `save_schema.h` pin all
three. The number moved because spec §31 asks for the migration path to be
exercised before the release, and because a release is the honest moment to find
out whether "bump the number and add a row" is really all it takes.

It was not. Three things had to be written first, and each has a mutation that
shows what it is holding up:

1. **`BlobOps::min_version` — the read side.** `pair_load()` and `single_load()`
   tested `stored == o.version` and nothing else, so a v2 blob under a v3
   firmware was a *bad* copy. Both copies of the Box bad is `LOAD_CORRUPT`,
   which is the SAVE ERROR screen, on every played device, on the first flash.
   The readers now accept anything in `[SAVE_SCHEMA_INPLACE_MIN, version]` and
   report which version won; `SAVE_SCHEMA_INPLACE_MIN` is 2, and it is a claim
   about *layout* — a blob inside the range can be read straight into the live
   struct. v1 is outside it and has a transform instead.
2. **`pair_write()` — the write side.** It asked `blob_ok()`, current version
   only, so on the upgrade boot both copies looked unusable: the `seq` restarted
   at 1 while the untouched copy kept its old, much higher one. `pair_load()`
   takes the highest `seq`, so the copy the migration had just written was the
   copy the next boot would *not* read — it read the old one and migrated the
   whole save again. Not data loss (the transform is a no-op, so both copies
   hold the same fields, and it converges on the third boot), but "upgraded"
   has to mean upgraded.
3. **The creator registry — outside `GameState` entirely.** `cs0..cs9` are not
   part of the state the chain transforms, so their upgrade lives in
   `custom_species_install_all()`. Without it `validate_custom_species()`
   refuses the record by name (`VR_CS_BAD_HEADER`), the slot stays empty, and
   every Bug pointing at it comes back `VR_UNKNOWN_SPECIES`: the creature the
   owner designed, gone on the first boot after a firmware update.

The checkpoint path needed the same treatment (`checkpoint_load()` reads through
`single_load()`), and both its entry points — the automatic
`LOAD_RECOVERED_CKPT` and the SAVE ERROR screen's *Recuperar* — run the chain
before committing. With today's no-op transform the version bytes would come out
right either way, because `checkpoint_load()` re-seals the Box and
`save_config()` seals into the caller's struct; `save_was_migrated()` is what
observes the call, and it is also the honest answer for the SAVE VERSION diag
line. The next bump will not be a no-op.

**One copy of each pair is still at the old version after the upgrade, and that
is correct.** `commit_all()` writes each pair once, into the copy a reader would
not pick, so the winner is at the new version and the loser is the spare — which
is what a pair is *for*: if the new copy rots, the old one still serves and is
upgraded in its turn. The spare catches up on the next ordinary write of that
blob, with no rule of its own, because the upgraded copy carries the highest
`seq` and `pair_write()`'s ordinary "overwrite the older one" already points at
the spare.

Driven by `tests/test_persistence.cpp` §8b: a whole played save (two Bugs,
one of them a creator species, a changed config, a bag, a cooldown, a trade
journal, both copies of every pair written) stamped back down to v2 and read by
this firmware.

## 9. The temporary bridge — GONE

`persistence/save_compat.cpp` and the `lgpet` key were the P2-C9 bridge that
mapped a live v1 `PetSave`/`Config` onto `BugInstance` slot 0 while the
simulation still ran on the v1 types. **P2-C10 deleted the module and the key**,
as the section itself said it would; this section described both in the present
tense for two more phases, which P4-C6 corrects. Neither exists in the tree —
`tests/test_game_state.cpp` asserts that `lgpet` is absent — and the key table
in §4 and the entry arithmetic in §5 no longer list it either.

There is no bridge left. A v1 save reaches v2 through `migrate_v1_to_v2()` (§8)
and nothing else.
