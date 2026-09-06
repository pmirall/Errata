# `tools/sprites/` — the sprite source format

This directory is the **source of truth for Pebblebol's creature art**. The
pixels live here, as text. `Pebblebol/src/data/sprites_pebbles.h` is generated
from them by `tools/gen_sprites.py` and **must never be edited by hand** — the
gate regenerates it and fails on any byte of drift.

If you are here to draw a body, you need four things: the grid rules below, the
one worked example, `--self-check`, and `sprite_dump`. Everything else is
background.

---

## 1. What you write

One **plain-text file per sprite set**, named `<set name lowercased>.txt`.
A *set* is one creature (or one egg, or one effect) with **all of its frames in
the same file**, because the frames are a pair and a pair split across two files
is a pair nobody compares.

```
tools/sprites/paketo.txt        <- the art
tools/sprites/atlas.txt         <- the ORDER, which is a contract (section 6)
```

### The file

```
# Free-form notes. Only before the first `--- frame` line.
# Copy the pack's `_root` / `_silhouette` / `_frame2` brief in here: the
# generator carries these lines into the header, so a reviewer sees the brief
# and the pixels in the same place.

name: PAKETO
size: 24x24
frames: 2
species: 1

--- frame 0
........................
.....##############.....
...
--- frame 1
........................
......##############....
...
```

### The header keys

| key | required | meaning |
|---|---|---|
| `name:` | yes | `UPPER_SNAKE`. Becomes the enum tag `PBSPR_PAKETO` and the array `pb_spr_paketo`. **Must equal the filename**: `paketo.txt` declares `name: PAKETO` and nothing else. |
| `size:` | yes | `<width>x<height>` in pixels. **A species body is exactly `24x24`.** |
| `frames:` | yes | How many `--- frame` blocks follow. **A species body has exactly 2.** |
| `species:` | no | The roster id this body draws (`tools/content/species.json`). Present on creature bodies, absent on eggs and effects. Section 6 explains what it binds. |

There are **no other keys**. Inventing one (`eyes:`, `author:`, `pose:`) is a
hard error naming the line, because the vocabulary is a contract — five people
are writing these files and a key one of them invents is a key the generator
silently ignores for everyone else. If you need a new key, add it to
`tools/gen_sprites.py` and to this file in the same change.

### The grid

* **One character is one pixel.** `#` is ink (a lit pixel, bit 1), `.` is empty
  (bit 0). Nothing else is legal inside a frame block — not a space, not a tab,
  not an `o`, not a `X`.
* A row is **exactly `width` characters**. Not padded, not cropped: short and
  long are both errors.
* A frame block is **exactly `height` rows**, with **no blank lines inside it**
  (a blank line is indistinguishable from a missing row, so it is refused).
* Rows run **top to bottom**, and within a row, left to right.
* `#` is also the comment character — so **comments are only legal in the header
  region, before the first `--- frame`**. Inside a frame block a `#` is a pixel.
* Frame blocks are `--- frame 0`, `--- frame 1`, … **numbered from 0, in order**.
* No tabs anywhere. No trailing whitespace anywhere (it is invisible and it
  changes a row's length, so it is refused rather than trimmed). Save as UTF-8
  with LF endings; a trailing CR is tolerated, everything else is not.

### The floor

Bodies are drawn standing on `HOME_FLOOR_Y`, so **put the feet on the last row
that has ink and keep the empty rows at the top, not the bottom**. `--self-check`
prints `N blank row(s) under the body` when you have not, because a body floating
one pixel above the floor is not something a byte count can see.

---

## 2. The worked example, in full

An 8x8 two-frame set. This is a complete, legal file — an eye that blinks:

```
# BLINKER - the smallest legal set, used as the format's worked example.
# frame 0 open, frame 1 shut.

name: BLINKER
size: 8x8
frames: 2

--- frame 0
..####..
.######.
##.##.##
##.##.##
########
.######.
..####..
........
--- frame 1
..####..
.######.
########
##....##
########
.######.
..####..
........
```

Feed those 8 rows of frame 0 through the packer and you get
`0x3C, 0x7E, 0xDB, 0xDB, 0xFF, 0x7E, 0x3C, 0x00`. That is the whole bit
contract: row stride is `((width + 7) >> 3)` bytes, and **the LSB of a byte is
the LEFTMOST pixel** — pixel 0 is bit 0, pixel 7 is bit 7. Row 0 is
`..####..`, i.e. pixels 2..5, i.e. `0b00111100` = `0x3C`.

A real one to read: `tools/sprites/egg_idle.txt` is the egg the device has been
drawing since phase 1, transcribed back into this format pixel for pixel.
`tests/test_sprite_pipeline.cpp` asserts that the bytes the generator produces
from it are byte-identical to the hand-typed hex in `data/sprites.h`, which is
how the bit order above is *proved* rather than asserted.

---

## 3. Check your own work before handing it back

```bash
python3 tools/gen_sprites.py --self-check tools/sprites/paketo.txt
```

It parses the file, applies every rule in section 1, and then **renders both
frames side by side as text** with the ink box, the ink count, the number of
blank rows under the body, and the pixel difference between the frames. It
writes nothing and it exits non-zero if anything is wrong.

Run it on **every file you touched**, and *read the render* — that is the only
part of this pipeline that can tell you the body looks like a creature:

```bash
python3 tools/gen_sprites.py --self-check tools/sprites/*.txt
python3 tools/gen_sprites.py --self-check            # everything, manifest included
```

Two things it reports that are not errors but almost always mistakes:

* `FRAME 1 IS A COPY OF FRAME 0` — the body will not animate. Legal, but it is
  nearly always a copy-paste, and `tests/test_sprite_pipeline.cpp` **fails** on
  it, so it will not ship silently.
* `NOTE: … is not in tools/sprites/atlas.txt yet` — the art exists and would
  never be drawn. See section 6.

---

## 4. Every way a file can be wrong, and what you get

Nothing here is repaired silently. Every one of these is a hard failure that
names the file, the line and (where a column means something) the column.

| what you did | what it says |
|---|---|
| row of 23 chars in a `24x24` | `frame 0 row 1 is 23 characters, `size: 24x24` says 24` |
| row of 25 chars | `frame 0 row 1 is 25 characters, `size: 24x24` says 24` |
| 23 rows in a 24-high frame | `frame 0 has 23 rows, `size: 24x24` says 24. Nothing here pads or crops: draw the missing rows` |
| an `o` in the grid | `:20:1: character 'o' is not '#' (ink) or '.' (empty). Comments are not allowed inside a frame block - '#' there is a pixel` |
| a trailing space | `:14:25: trailing whitespace - a pixel row is exactly its own width and nothing else` |
| a tab | `a TAB character - a pixel grid is spaces-free and tab-free` |
| a blank line mid-frame | `blank line inside a frame block…` |
| `frames: 3`, two blocks | `` `frames: 3` but the file holds 2 `--- frame` block(s) `` |
| `--- frame 2` after frame 0 | `` frames must be numbered from 0 in order - expected `--- frame 1` `` |
| an all-`.` frame | `frame 1 is empty. A blank frame draws nothing at all - on a two-frame body it makes the pet vanish every other tick` |
| `name:` ≠ filename | `name 'EGG_IDLE2' does not match the filename 'egg_idle.txt'` |
| an invented key | `unknown key 'eyes'. The whole vocabulary is name, size, frames (required) and species (optional)` |
| a species body at `23x24` | `a species body is 24x24, not 23x24` |
| a species body with 1 frame | `a species body has exactly 2 frames, not 1` |

---

## 5. Regenerating, and what the gate checks

```bash
python3 tools/gen_sprites.py            # rewrite Pebblebol/src/data/sprites_pebbles.h
python3 tools/gen_sprites.py --check    # what tools/check.sh runs; exit 1 on drift
```

`--check` regenerates the header **in memory** and byte-compares it to the tree,
exactly as `tools/gen_content.py --check` does for `data/*_table.h`. It catches
both directions: a hand edit to the generated header, and an art edit that was
never regenerated. **Commit the `.txt` and the regenerated header together.**

On top of the text diff, the generator refuses to emit at all when:

* a name in `atlas.txt` has no file, **or** a `.txt` file is not in `atlas.txt`
  (art that would never ship — the worse of the two, because it looks like
  success);
* the species block is out of order, has a hole, is not the tail of the atlas,
  or names a species `tools/content/species.json` does not have;
* a body's set name does not match the pack's name for its id
  (`Rafagón` → `RAFAGON`);
* the art is over the byte budget.

The header it writes carries its own compile-time guards: every table row is
tied to the array it points at through `NT_SPR_SET_FITS`
(`data/sprite_types.h`), so a row that disagrees with its art is a **build**
failure — `drawXBM()` reads `((w+7)>>3)*h` bytes with no bound of its own, and a
row that lies reads straight into the next sprite.

---

## 6. `atlas.txt` — the order, and why a body cannot choose its slot

`atlas.txt` lists set names, one per line, in the order they are emitted. It is
a committed file rather than `os.listdir()` because the order is a contract:

1. **Fixed slots first.** `EGG_IDLE` must be id 0 — `sprite_set()` falls back to
   set 0 for any out-of-range id.
2. **The species block is the tail, in roster order, with no gaps.** The pack's
   invariant is `sprite_id == id - 1`, so a body's slot is *determined*:
   `PB_SPRITE_BODY_FIRST + (species_id - 1)`.

That is why `species:` is checked so hard. A body in the wrong slot draws the
**wrong creature** on HOME, and nothing else in the tree would notice: every id
is still in range, every array is still the right size, every assertion is still
green, and Paketo wears Murax's face. The generator is the only thing standing
between that and a release, so it refuses rather than warns.

To add a body: write `tools/sprites/<name>.txt`, add `<NAME>` to `atlas.txt` in
the species block at its id's position, run `--self-check`, then run the
generator, then run the gate.

---

## 7. Looking at what you made

The pipeline cannot tell you whether a body looks like a creature. **A 24x24 of
noise passes every check in this directory.** The instrument for that is:

```bash
make -C tests spritetool
./tests/bin/sprite_dump list                    # every set in both atlases
./tests/bin/sprite_dump text PAKETO             # one set, frames side by side
./tests/bin/sprite_dump sheet /tmp/atlas.pbm    # contact sheet, opens anywhere
```

`sprite_dump` reads the **compiled headers**, not these `.txt` files, and decodes
them the same way the device's `drawXBM` does — so what it prints is what the
panel will show. A rendering of the source would only prove the source says what
it says.

---

## 8. Where this fits

* `tools/gen_sprites.py` — the generator; its module docstring carries the rest
  of the rationale.
* `Pebblebol/src/data/sprite_types.h` — the XBM contract: stride, bit order,
  frame layout, and the two size-guard macros.
* `Pebblebol/src/data/sprites_pebbles.h` — **generated**, do not edit.
* `Pebblebol/src/data/sprites.h` — the legacy Nottamagochi atlas. Still what the
  firmware draws today. P9-C3 deletes its 36 body and pose sets and points the
  lookup at the generated atlas; the two eggs exist in both files and are
  asserted byte-identical until then.
* `tests/test_sprite_pipeline.cpp` — the assertions.
* `docs/content.md` — the authoring rules for the roster these bodies belong to.
