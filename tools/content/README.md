# `tools/content/` — the content pack

The JSON in this directory is the SOURCE of every content table under
`Pebblebol/src/data/`. Nothing here is compiled; `tools/gen_content.py` reads it
and writes the headers.

    tools/content/species.json      60 species, 20 families x 3 stages
    tools/content/attacks.json      34 attacks
    tools/content/items.json        10 items
    tools/content/evolution.json    40 evolution rules
    tools/content/encounters.json   40 encounter rows
    tools/content/balance.json      every constant, enum and formula

## The gate that travels with it

    cd tools/content && python3 verify.py --fast     # 82 checks, 0 FAILED

`verify.py` re-checks the design rules against the shipped JSON and imports
nothing from the generator, so a green run proves the pack self-consistent even
if `gen_content.py` is wrong. `sim_engine.py` is its battle simulator (used by
the full, non-`--fast` run). `tools/check.sh` runs the `--fast` form on every
gate, together with `tools/gen_content.py --check`, which regenerates every
header in memory and fails if the tree and the JSON have drifted apart.

WHAT `verify.py` DOES NOT CHECK, found in P4-C1: it asserts that
`TOTAL_STAT_POINTS_BY_STAGE` is `[16, 22, 28]` but never checks a species row
against it — setting species 1's `base_hp` to 9 still reports "82 checks,
0 FAILED". The generated `src/data/creator_schema.h` rejects that at compile
time instead.

BAD INPUT NOW PRODUCES A SENTENCE, NOT A STACK (P4-C1 review). Two edits used
to kill `verify.py` before its `RESULT:` line: a non-contiguous species id
raised `KeyError` out of the section-6 evolution cross-check, and a non-Latin-1
character raised `UnicodeEncodeError` out of the flash-budget arithmetic — in
both cases the gate still failed, but the operator read a traceback instead of
the named `[FAIL]`. A missing id now resolves to a sentinel row that fails every
check that reads it by name, and the byte count falls back to UTF-8 for a string
the Latin-1 check has already failed. `gen_content.py` refuses the same two
inputs earlier and by name: it bound-checks every emitted integer against the
struct field it lands in, and it escapes `"` and `\` in a shipped string and
dies on a control character or anything past Latin-1 rather than writing C that
does not compile.

## Editing content

1. Edit the JSON.
2. `python3 tools/gen_content.py`
3. `tools/check.sh`

Step 2 is not optional: the headers are committed, and step 3 fails without it.
Editing a `_`-prefixed key (a design or art note) changes no header and does not
move `CONTENT_VERSION`; editing anything else moves both.

## What is emitted, and what is not

Emitted: `species_table.h`, `attacks_table.h`, `items_table.h`,
`evolution_table.h`, `encounter_table.h`, `creator_schema.h`,
`content_version.h`, and the marked block inside `core/strings_es.h`.

NOT emitted, deliberately: the battle constants (they are hand-placed in
`data/balance.h` §5 with their provenance written next to them), and the roster
beyond `ROSTER_FAMILIES` families — the emitted table is a PREFIX of the 60
species here, because `id == index + 1` is a `static_assert`. `ROSTER_FAMILIES`
is 20 since P9-C3, so the prefix is currently the whole pack.

**`XP_TABLE` IS NOT HERE AT ALL ANY MORE.** This file used to carry a second
31-entry curve — `inc(L) = 25 + 12*(L-1) + 4*(L-1)^2`, total 36,453, against the
shipped `10 + L*L` and 8,845 — and this README used to explain why it was not
emitted. P9-C4 **deleted it** rather than leaving it unemitted: two curves in two
files is a second source of truth, and checking either one cannot catch the pair
disagreeing. `verify.py` now reads the curve out of `Pebblebol/src/data/balance.h`
and **fails by name if an `XP_TABLE` key reappears here**. The evidence for
keeping the shipped curve is `tests/tools/sim_days.cpp`'s and is written out in
`data/balance.h` §4.
