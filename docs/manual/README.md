# The printed manual

The booklet that goes in the box. A6, saddle-stitched, one ink, Spanish and
English in the same booklet.

```
tools/build_manual.sh --draft         # holes visible, for reading
tools/build_manual.sh --draft --png   # + one PNG per page, for proofing
tools/build_manual.sh --print         # refuses to build with any hole left
```

Output lands in `docs/manual/out/`, which is not tracked.

## Installing Typst

Typst is a single static binary and is not vendored here.

```sh
curl -sSL -o /tmp/typst.tar.xz \
  https://github.com/typst/typst/releases/latest/download/typst-x86_64-unknown-linux-musl.tar.xz
tar -xf /tmp/typst.tar.xz -C /tmp
sudo install -m755 /tmp/typst-x86_64-unknown-linux-musl/typst /usr/local/bin/typst
```

Built and verified against Typst 0.15.1.

It was chosen over the alternatives for three reasons that matter to a booklet
going to a print shop: it writes a **TrimBox** into the PDF when page bleed is
non-zero (headless Chromium's `--print-to-pdf` writes neither bleed nor
TrimBox, and a printer cannot trim a file without one); its source is plain
text that **diffs in git** next to the firmware; and it **reads TOML natively**,
which is what lets every legal fact live in one file.

## How it is put together

| File | What it is |
|---|---|
| `product_facts.toml` | **Every legal and identity fact.** The only file to edit when filling the manual in. |
| `lib.typ` | Facts, bilingual layout, screens, callouts, cross-references. Imported by all three documents. |
| `manual.typ` | Page geometry, type, cover, back cover. Assembles the rest. |
| `content/guide.typ` | Pages 2-17, the player's guide. |
| `content/legal.typ` | Pages 18 onwards, the legal block. |
| `assets/screens/*.svg` | **Generated.** Do not edit; see below. |
| `assets/drawings/*.svg` | Hand-drawn. Three of them, none drawn yet. |

### Three rules the build enforces for you

**No legal fact is written in prose.** Everything identifying comes from
`product_facts.toml` through `fact()`. A value left as the string `"TODO"`
prints as a black `TODO: entity.name` block in a draft and **fails the build**
in `--print` — both in the shell script, which lists every hole at once, and
inside Typst, which panics on the first one so that bypassing the script
changes nothing. A draft with visible holes is useful. A print-ready PDF
missing a legally mandated field is a recall.

**No screenshot is drawn by hand.** Every device screen is generated from
`tests/golden/screens/*.pbm` by `tools/pbm2svg.py`, so a screen that changes in
the firmware changes in the manual too. `tools/check.sh` fails if a golden
moved and its SVG did not. Callouts are placed *over* the image from Typst,
never baked into the SVG, so regenerating a screen never destroys them and the
labels stay in the bilingual content file where they can be translated.

**No page number is typed.** The legal section grew by three pages during
drafting and every hard-coded "see page 19" silently became wrong. Sections
carry labels and references go through `pg(<sec-batteries>)`.

## Print specification

| | |
|---|---|
| Trim | 105 × 148 mm (A6) |
| Bleed | 3 mm on all four sides, TrimBox written into the PDF |
| Safety margin | 5 mm from trim; 12 mm at the spine for the fold |
| Extent | 28 pages — **must** stay a multiple of 4 (the build enforces it) |
| Binding | Saddle stitch, 2 staples, self-cover |
| Colour | **One ink, black.** The product's identity is monochrome pixel art; colour would double the cost and add nothing |
| Paper | 90-115 g/m² offset |
| Type | Guide 8/11.2 pt; legal 6.5/9.1 pt. **Never below 6 pt** |
| Delivery | Single pages, in reading order. **Do not impose** — the printer imposes, that is what they want, and hand-imposition is where booklets go wrong |

## Still to do

**The three drawings.** `content/guide.typ` marks them with dashed
`P10-M5` boxes. They cannot be drawn accurately until the enclosure is
finished, and a wrong diagram is worse than a marked gap.

| Slug | Page | What it must show |
|---|---|---|
| `exploded.svg` | 2 | Device, two AAA cells and the manual, laid out, numbered 1-2-3 to match the legend |
| `batteries.svg` | 3 | Opening the back cover, cell orientation, the moulded `+`/`-` marks, the cover clicking shut |
| `buttons.svg` | 5 | The front of the device with **A** on the left and **B** on the right, large enough to read at A6 |

Draw them monochrome, line only, no greys, as SVG at the same scale as the
screen assets. They print at one ink.

**The bin symbol.** The crossed-out wheeled bin on the disposal page is drawn
in `content/legal.typ` rather than imported. Check it against the published
figure in **EN 50419** before the first print run: getting a legally
prescribed mark approximately right is not the same as getting it right.

**A defect this manual cannot paper over.** The device still boots showing
`NOTTAMAGOCHI`, from `STR_APP_NAME` in `Pebblebol/src/core/strings_es.h:478`,
and the main menu header says the same. `boot_splash.svg` on page 4 of the
manual therefore shows the old product name. That is a firmware fix, not a
manual fix — but it blocks the print run.
