# Pebblebol — SaveSchema v2

Everything that reaches flash, and nothing else. This document is written from
`Pebblebol/src/persistence/save_schema.h`; that header is the authority, and any
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

## 2. Partition table (`Pebblebol/partitions.csv`, decision D6)

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

### PebbleInstance — 128 B, keys `pb<slot><copy>` (pair)

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
| 66 | 1 | `origin` | `PebbleOrigin` |
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
`device_name[13]` · `ap_pass[17]` · `tz[40]` · `reserved[152]` · `crc16`.

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

`magic 0x4443` · `version` · `n` · `seq` · `reserved_a[4]` · `rows[32]`
(`{net_hash, until_epoch}`) · `reserved_b[2]` · `crc16`. `net_hash` is the
abstract identity from the scanner: no SSID or BSSID ever reaches flash.

### CustomSpeciesRec — 192 B, keys `cs0`..`cs9` (single)

`magic 0x5343` · `version` · `slot` · `budget_used` · `type` · `compat_group` ·
`base[4]` · `moves[4]` · `name[13]` · `reserved[17]` · `sprite[2][72]` ·
`crc16`. A bad CRC here costs a sprite, never a Pebble.

### PendingTrade — 64 B, key `tr` (single)

`magic 0x5254` · `version` · `phase` · `out_id` · `peer_id` · `in_wire[48]` ·
`reserved[2]` · `crc16`. Written BEFORE either side of a trade commits and
resolved at the next boot, so a power cut can neither duplicate nor vaporise a
Pebble.

## 4. Key table

Every key is ASCII and ≤ 15 characters (`NVS_KEY_NAME_MAX_SIZE` is 16 with the
NUL). `save_schema.h` §9 is the only place they are spelled.

| Key | Blob | Partition |
|---|---|---|
| `pb<slot><copy>` | PebbleInstance | `KV_MAIN` |
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
| checkpoint | `SAVE_CKPT_PERIOD_S` 86400 s | plus level-up, evolution, capture and trade — the events that change what a Pebble *is* |

## 7. Loading

`save_load_all()` answers with one of seven results, worst-first:

| Result | Meaning | Writes? |
|---|---|---|
| `LOAD_OK` | everything read back clean | no |
| `LOAD_FRESH` | no save anywhere; build a starter | no |
| `LOAD_MIGRATED` | an older schema was read and converted | yes, the converted state |
| `LOAD_RECOVERED_PAIR` | one copy of some blob was bad; the other served | yes, the repair |
| `LOAD_RECOVERED_CKPT` | `KV_MAIN` had nothing; `nvs2` served | yes, the recovered state |
| `LOAD_CORRUPT` | both copies of the Box or of the active Pebble are bad | **no** |
| `LOAD_FOREIGN_NEWER` | a save from a newer firmware | **no** |

The last two are the point of the whole design. The v1 loader answered a
boolean and its caller reacted to `false` by building a fresh pet, which
silently destroyed a recoverable save (audit risk 3). Now the session goes
read-only, nothing is written, and `SCR_ERROR` asks the user: **A: Recuperar**
(restore the `nvs2` checkpoint) or **B: Reset de fábrica** (behind two
confirmations). A save from a newer firmware is offered no reset at all — those
bytes are good data and the fix is a firmware update.

## 8. Migration

`migrate_run(from, out)` is table-driven: adding v3 means adding a row, not a
branch. The only row today is v1 → v2, whose field map is:

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

## 9. The temporary bridge — GONE

`persistence/save_compat.cpp` and the `lgpet` key were the P2-C9 bridge that
mapped a live v1 `PetSave`/`Config` onto `PebbleInstance` slot 0 while the
simulation still ran on the v1 types. **P2-C10 deleted the module and the key**,
as the section itself said it would; this section described both in the present
tense for two more phases, which P4-C6 corrects. Neither exists in the tree —
`tests/test_game_state.cpp` asserts that `lgpet` is absent — and the key table
in §4 and the entry arithmetic in §5 no longer list it either.

There is no bridge left. A v1 save reaches v2 through `migrate_v1_to_v2()` (§8)
and nothing else.
