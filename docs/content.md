# Adding content to Pebblebol

Written for the person who opens this a year from now wanting to add a species,
retune an attack or draw a body — not as a record of what P9-C1 built.

Everything the player meets is **generated from data files**. There is no
species defined in C++, no attack table anybody edits by hand, no sprite typed
as hex. If you are about to edit something under `Pebblebol/src/data/`, stop:
that directory is output.

```
tools/content/*.json  --(tools/gen_content.py)-->  Pebblebol/src/data/*_table.h
                                                   Pebblebol/src/data/creator_schema*.h
                                                   Pebblebol/src/data/content_version.h
                                                   the generated blocks in src/core/strings_es.h

tools/sprites/*.txt   --(tools/gen_sprites.py)-->  Pebblebol/src/data/sprites_pebbles.h
web/creator/*         --(tools/gen_index_html.py)->Pebblebol/src/data/index_html.h
```

`tools/check.sh` regenerates all three in memory and fails on any byte of drift,
in both directions: a hand-edited header fails, and a source edit that was never
regenerated fails.

---

## 1. The loop

```bash
$EDITOR tools/content/species.json          # 1. edit the source
cd tools/content && python3 verify.py       # 2. the pack's own gate, all 60 species
cd ../.. && python3 tools/gen_content.py    # 3. rewrite the headers
tools/check.sh                              # 4. build, host tests, size gates, grep gates
git add tools/content/species.json Pebblebol/src/data/ Pebblebol/src/core/strings_es.h
```

**Commit the source and the regenerated headers together.** They are one change;
splitting them leaves a commit that fails its own gate.

Two things about step 2. `verify.py` validates **all 60 species in the pack**,
including the ones this build does not ship (section 3), and it imports nothing
from the generator — so a green run says the *deliverable* is consistent even if
`gen_content.py` is wrong. `tools/check.sh` runs it with `--fast`, which skips
its 17 balance and reachability checks (they take seconds, not milliseconds);
**run it without `--fast` after any change to stats, moves, rarity, encounters or
the corruption content**, because those 17 are the only checks in the tree that
look at whether the roster is *balanced* rather than merely *well-formed*.

To see the shape of what you changed rather than a diff of integers:

```bash
cd tools/content
python3 balance_report.py --json > /tmp/before.json
# ...edit...
python3 balance_report.py --diff /tmp/before.json
python3 balance_report.py                 # the full report: per-type averages,
                                          # the budget histogram, the extremes
```

`balance_report.py` is deliberately **not** in the gate. Everything it prints is
already asserted more tightly by `verify.py`; what it is for is letting a
reviewer see *which species moved*. It reports the tables and never simulates a
battle — whether a kit actually wins is `tests/tools/balance_matrix.cpp`
(P9-C4), over the shipped engine.

---

## 2. The §53 authoring rules

These are the rules a new row has to satisfy. Each one names where it is
enforced, because "the rule" and "the thing that checks the rule" drift apart
otherwise.

### A species (`tools/content/species.json`)

| field | rule | enforced by |
|---|---|---|
| `id` | contiguous from 1; `id == index + 1` | `gen_content.py` `_check_prefix`, a `static_assert` in `species_table.h` |
| `family`, `stage` | three stages per family, stages 0/1/2 | `verify.py`, `test_content.cpp` |
| `name` | a Spanish **proper noun**, ≤ 9 glyphs, Latin-1 only | `gen_content.py` `width_limit()`, `test_content.cpp` `every_shipped_name_fits_the_box_the_screen_gives_it` |
| `flavor_es` | ≤ 25 glyphs (one line of the 128 px panel at 5x8) | same |
| `type` | `SIGNAL` / `CORRUPT` / `SYSTEM`, never `NEUTRAL`; the roster stays 20/20/20 | `verify.py`, `test_content.cpp` |
| `base_*` | each 1..10; the four total **exactly** 16 / 22 / 28 by stage | `verify.py` (zero variance), `creator_schema.h` `static_assert` |
| `moves` | exactly 4, each on the attack table, own type or `NEUTRAL`, total `budget_cost` inside the stage cap plus the rarity bonus, no move over the stage power cap | `creator_schema.h` `static_assert`, `test_content.cpp` `every_builtin_row_respects_the_budgets_the_creator_is_held_to` |
| `evo_rule` | `(family - 1) * 2 + stage`, or 255 for a final stage | `gen_content.py`, `evolution_table.h` |
| `rarity` | 0..3; the roster stays 30/18/9/3; **a family's stage 0 may not be RARE or SPECIAL** — a family nothing can start is a family nobody sees | `verify.py`, `test_content.cpp` `every_family_has_exactly_one_base_stage_and_it_is_reachable` |
| `spawn_weight`, `category_mask` | > 0; every network category must have something common to spawn | `test_content.cpp` `every_network_category_has_something_to_spawn` |
| `compat_group` | > 0; five groups across the roster (breeding) | `verify.py` |
| `sprite_id` | **`id - 1`, always** | `gen_content.py`, `test_content.cpp`, `gen_sprites.py`'s slot check |

Keys beginning with `_` (`_root`, `_silhouette`, `_frame2`, `_move_names`,
`_archetype`, …) are **design notes**. They are not emitted, and they are
deliberately excluded from `CONTENT_VERSION` — fixing a typo in a comment must
not mark every save on every device as made against foreign content. Keep them
accurate anyway: `_silhouette` and `_frame2` are the brief an artist draws from,
and `gen_sprites.py` carries them into the generated header.

### An attack (`attacks.json`)

Name ≤ 10 glyphs (four of them share the battle menu's two-column box). `power`
0..100 — **0 means a status or buff move**, and note that `battle_ai.cpp` scores
every zero-power move at 0, so a species whose kit leans on them will read as
weak in any AI-vs-AI matrix. `accuracy` 1..100. `budget_cost` is what a species
pays to carry it, and it is the same scale the creator charges an uploaded
species, so the two are comparable by construction. Every attack should be on at
least one learnset: an attack nobody can hold is content nobody will see, and
`balance_report.py --section moves` prints the orphans.

### An evolution rule (`evolution.json`)

Target is **the next stage of the same family** — not a different family, not two
stages up. `level` 1..`PB_LEVEL_MAX`. A `cond` other than `NONE` needs its
`cond_value`. Conditions are mapped **by name**, never by the pack's ordinal:
the pack and the firmware order `EvoCond` differently, and mapping by ordinal
would turn the `CORRUPTED` families into `EVOC_ITEM`. Keep `EVO_COND_CORRUPTED`
to at most five families (§19); it is the signature mechanic, not the norm.

### A string

Every user-facing string lives in `core/strings_es.h` and nowhere else. It must
be **Latin-1** — the u8g2 `_tf` fonts carry ASCII + Latin-1 and no more, so an em
dash, an ellipsis character, a curly quote or an arrow draws as the wrong glyph
or as nothing. Write `-` and `...`. Accents and ñ are fine. Width budgets on the
128 px panel: 25 glyphs at 5x8, 21 at 6x10 and t0_11b, 32 at 4x6 (ASCII only).
Lengths are counted in **glyphs, not bytes** — `Rafagón` is 7 glyphs in 8 bytes.

---

## 3. Why the shipped roster is smaller than the pack

`tools/content/` holds **60 species in 20 families**. `data/species_table.h`
ships **36**, and the clamp is one constant — `ROSTER_FAMILIES` at the top of
`tools/gen_content.py`, whose banner explains it at length.

**The clamp is the art, not the tables.** Species ids are contiguous, so the only
legal subsets are prefixes, and the pack's `sprite_id == id - 1` invariant means
species *N* needs the *N*-th body in the atlas. The legacy atlas holds 36
addressable bodies. Flash is not the constraint and never was: all 60 species'
tables and strings are about 5.4 KB against 273 KB of release headroom.

Raising it is P9-C3's job and it is a package deal:

1. draw the bodies (section 4) so `PB_SPRITE_BODY_COUNT` reaches 60;
2. raise `ROSTER_FAMILIES` to 20 and regenerate;
3. repair the two cases that are **written to fail** at this moment —
   `test_content.cpp`'s `the_pack_is_complete_and_the_roster_is_clamped_by_the_atlas`
   and `the_naive_sprite_sum_would_mis_draw_a_third_of_the_roster`. They are pins,
   not bugs; each says in its own comment what the correct repair is.

`SPECIES_PACK_COUNT` (60) and `SPECIES_TABLE_COUNT` (36) are both emitted, so
both halves — "the content is complete" and "the ship is clamped by the art" —
are assertable from the headers rather than only from Python.

---

## 4. Drawing a body

The full contract is **`tools/sprites/README.md`**. In one paragraph: one
plain-text file per set, `#` is a lit pixel and `.` is an empty one, one
character per pixel, 24x24 and exactly two frames for a species body, both frames
in the same file, the file names its own set and the set's slot comes from
`tools/sprites/atlas.txt` in roster order. Nothing is ever padded or cropped —
every malformed file is a hard error naming file, line and column.

```bash
python3 tools/gen_sprites.py --self-check tools/sprites/paketo.txt   # validate + render
python3 tools/gen_sprites.py                                          # regenerate
make -C tests spritetool && ./tests/bin/sprite_dump text PAKETO       # look at it
./tests/bin/sprite_dump sheet /tmp/atlas.pbm                          # contact sheet
```

**Nothing automated can tell you whether a body looks like a creature.** A 24x24
of noise satisfies every check in the pipeline. `sprite_dump` renders the
*compiled* atlas — the bytes that will reach the panel — and a person has to look
at it. That is the whole reason it exists.

---

## 5. `CONTENT_VERSION`

A hash of the pack, not a counter anybody bumps. It is stamped into `BoxHeader`,
into every `BattleState` and into the session handshake, so a save and a peer can
say which roster they mean; a battle whose `content_ver` disagrees is refused
rather than replayed against different tables.

The input set: the eight `tools/content/*.json` files, every key except the
`_`-prefixed notes, plus the emitted roster size (two builds shipping different
numbers of species *are* different content from identical JSON). FNV-1a 32 over
that canonical text, folded to 16 bits, with 0 remapped to 1.

That it moves when the JSON moves is checked by
`python3 tools/gen_content.py --selftest`, which the gate runs. It perturbs each
pack file **one at a time** and requires the version to change for every one of
them — per file, never in aggregate, because an aggregate check lets files drop
out of the hash one by one while it stays green. It also requires the version
*not* to move for a `_`-prefixed note, and requires the roster size to be in it.
A C++ test cannot check any of this: `test_content.cpp` never sees the JSON, and
the case that used to claim this property accepted 65,534 of 65,536 possible
values.

---

## 6. Where the files are

| path | what |
|---|---|
| `tools/content/*.json` | the content. Edit here. |
| `tools/content/verify.py` | the pack's own gate, 127 checks, all 60 species |
| `tools/content/balance_report.py` | the human-readable report; not a gate |
| `tools/content/sim_engine.py` | a **Python model** of the battle engine. `game/battle.h` lines 90-116 list five ways it diverges from the shipped one; its win rates are a claim about the model. |
| `tools/gen_content.py` | the generator. Its banner carries the roster-size reasoning. |
| `tools/sprites/` | the art, as text. `README.md` there is the format contract. |
| `tools/gen_sprites.py` | the sprite generator |
| `Pebblebol/src/data/*_table.h` | **generated.** Do not edit. |
| `Pebblebol/src/data/sprites_pebbles.h` | **generated.** Do not edit. |
| `Pebblebol/src/data/sprites.h` | the legacy hand-written atlas, still what the firmware draws |
| `Pebblebol/src/data/sprite_types.h` | the XBM contract: stride, bit order, frame layout, size guards |
| `Pebblebol/src/core/strings_es.h` | every user-facing string; two generated blocks inside a hand-written file |
| `tests/test_content.cpp` | every compile-time content guard, re-asserted at runtime |
| `tests/test_sprite_pipeline.cpp` | the atlas's assertions, including the bit-order pin |
| `tests/tools/sprite_dump.cpp` | renders the compiled atlas for a human |
