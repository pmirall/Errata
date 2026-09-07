# The creature style guide

**What this is.** The rules that decide whether a 24x24 grid looks like a Pebble.
They are not checked by anything, and they cannot be: `tools/sprites/README.md`
says so in as many words — *"The pipeline cannot tell you whether a body looks
like a creature. A 24x24 of noise passes every check in this directory."*
`docs/content.md` §4 repeats it. This file is the missing half: the format
contract lives in the README, the roster contract in `content.md`, and the
**look** lives here.

**Where it comes from.** Nothing here was invented for this document. Every rule
below was already being obeyed by all sixty bodies and stated, in scattered form,
in the header comments of `tools/sprites/*.txt` — 23 of the 65 files argue from
legibility at 1x, 16 carry a `DEVIATIONS` block, 16 state the blink rule. This is
that consensus written down once, with the files that state it named so a reader
can go and check.

> **Branch note.** The roster this describes lives on
> `claude/repo-exploration-sync-bbonku` (P9-C3 and P9-C6), which is not merged to
> `main` at the time of writing. On `main` the paths below do not exist yet.

---

## 1. What a Pebble is, visually

> **A solid shape with holes punched in it, that fails a little more at each
> stage.**

That is the whole language. It is not a decorative choice: it is what a 1-bit
128x64 panel can actually hold at 24x24, and every rule in section 2 is a
consequence of it.

The fiction (§0 of the product spec) asks for a computer bug — a *defect*, not an
animal. The roster answers that by making **damage the growth axis**. A Pebble
does not get bigger or angrier from stage 0 to stage 2. It gets **more broken**,
inside the same silhouette and the same 24x24 box.

---

## 2. The five laws

Every one of the sixty bodies obeys all five. Breaking one is not forbidden, but
it is a decision to argue for in the file's header, the way the sixteen
`DEVIATIONS` blocks already do.

### 2.1 The body is one solid mass

Ink is filled, not outlined. A 1px lit outline does not survive at 1x, so shape
is carried by the silhouette and by what is cut *out* of it — never by internal
lit detail.

`paketo.txt` states the corollary that catches everyone once:

> *"the body is a solid silhouette, so a lit stripe inside it is invisible and an
> unlit one reads as a field divider."*

Interior structure is therefore always **subtractive**. Seams, visors, doorways,
creases, sockets and slots are all unlit.

**Connectivity is a decision, not an accident.** `--self-check` prints
`N piece(s)`; a number that moved when you did not mean it to is a part that came
loose. It found MURAX's crenellations floating free of their rim. One family
breaks this on purpose — LATENCY (`lagui`/`jitera`/`timaut`), *"the only bodies
in the roster drawn with a gap"* — and says so in its motif.

The starter got this wrong once and it is the best cautionary tale in the tree.
PAKETO's header seam ran the full height with no bridge, splitting the silhouette
into 8-connected masses of 69 and 63 px, each with its own pair of stub feet — so
the first creature every player ever sees read as **two small creatures standing
side by side**. P9-C6 bridged it at rows 16-18 and carried the fix to FRAGMAR and
RAFAGON.

### 2.2 The eye is a punched socket

An eye is an **enclosed unlit region** — background the outside cannot reach — of
at least 3 px, at most 8 rows tall and at most half the body wide.

This is not a style preference. `tools/gen_sprites.py` derives the blink bands
straight from the art by exactly that rule, and the README is explicit that the
rule *"is a rule only because every body in this atlas was drawn to it: a 1px lit
outline does not survive at 1x, so an eye is punched, not drawn."*

A **lit pupil may sit inside the socket**, and in three families it is the part
that carries the animation: BUG (*"knocked-out socket eyes with a lit pupil"*),
BITROT (*"one wide socket eye whose block pupil flicks from side to side"*) and
COOKIE, whose eye is *"always the same three rings: a 2px lit rim, an unlit
sclera inside it, and a LIT pupil inside that. The pupil is what moves — it is
the only moving part in all three stages, and it is why the family reads as
watching rather than as breathing."*

What is never legal is an eye drawn **as** a lit dot on a solid body, with no
socket punched around it: there is no contrast for it to sit against, and the
blink derivation has no hole to find.

Two consequences:

- **Where you put the hole is where it blinks.** If the blink lands wrong, move
  the hole — do not add a key. The band is a consequence of the drawing.
- **Any small enclosed hole will be read as an eye.** The generator cannot tell
  an eye from a porthole, a bolt or a bite. A decorative hole in the top of a
  body will get blinked.

The rule shipped a real defect before P9-C6 tightened it: on bodies whose upper
half is mostly gaps, the derived band covered most of the sprite and the blink
drew the creature **solid** — DENYRA gained 79 px on a 283 px body, and PANOPTIX
lost all nine of the eyes it is named for. The generator now runs the device's
own fill over each candidate band and rejects it if a single filled pixel is not
a hole pixel.

### 2.3 Nothing is one pixel

Two pixels is the floor for any feature that has to read: a bite, a dot, a gap, a
limb.

`--self-check` counts `N hairline px` — ink one pixel wide in both axes locally —
because *at 1x that is not a limb or an antenna, it is a stuck pixel*. The bodies
state the same thing from the other side:

- `bitto`: *"The corner bites are CHUNKS of 2-3 px, not single pixels. A
  one-pixel bite is invisible on the panel."*
- `nulix`: a slash across a 10px disc left 1px slivers, and *"rendered, that reads
  as dirt, not as a cut."* The disc went to 12px.
- `lagui`: the trail dots are 2px *"drawn at 1px on the eye line they read as more
  eyes, which is what the first render showed."*

### 2.4 The feet are on the last inked row

Bodies stand on `HOME_FLOOR_Y`. Keep the empty rows at the top of the grid, never
at the bottom — a body floating one pixel above the floor is not something a byte
count can see, which is why `--self-check` prints `N blank row(s) under the body`.

### 2.5 Frame 2 is motion, not flicker

Two frames at even duty is a hard constraint that is easy to forget. Sixteen files
state the same conclusion for the eyes: **a blink is a squint, never a shut eye**
(a 2x2 hole going to 2x1), because *"the two frames alternate at even duty, so a
fully closed eye reads as flicker."* The same logic governs everything else that
moves: an alternation the eye reads as a strobe is a defect, an alternation it
reads as breathing, walking or emitting is the point.

Byte-identical frames are a hard error — the generator exits non-zero and says
`frame 1 is byte-identical to frame 0`.

---

## 3. Growth is degradation

This is the roster's signature and the thing to protect. Across all twenty
families, stage 0 → 1 → 2 keeps the same root shape and **damages it further**.
It never scales it up.

| family | the same shape, three times |
|---|---|
| PACKET | *"whole, torn, then shattered"* |
| CRYPTO | *"shut, then faceted and double-locked, then broken open with the shackle snapped and the keyhole gone through to nothing"* |
| NULL | *"a slit of a mouth inside the disc, then a wedge that eats out through the rim, then a break that goes all the way and leaves the two halves standing apart"* |
| BITROT | *"crumbs, then pits through the face, then a lace with the bottom edge gone and drips hanging off it"* |
| PORT | *"one door, then three, then a wall so perforated that one doorway has torn its own frame open"* |
| MEMORY | *"clean comb, clean comb, then a melt"* |
| GLITCH | *"three slices and 2px, six slices and one slice missing altogether, then eleven slices, 5px throws and torn bands"* |
| LATENCY | *"the trail grows and the body hollows out"* |
| DAEMON | *"nothing, then a carried bar, then a bar that has come loose and floats"* |

Where a family does add rather than subtract, it adds **repetition, not bulk**:
COOKIE *"adds eyes rather than bulk"*, PING *"adds rank after rank outside the
shell"*, PROBE grows the stem between its two heads. The body itself stays the
size it was.

**This is why the flat 24x24 atlas is not a limitation.** Sixty bodies, two frames
each, all the same size (`60 x 2 x 24x24 = 8,640 B`); §19's "increasingly
destructive" is carried entirely by what is missing from the shape. Reaching for
a bigger sprite to show escalation would be solving a problem this roster has
already solved better.

---

## 4. The twenty families

Type is from `tools/content/species.json`; the motif is quoted from the family's
own `.txt` headers.

| # | type | family | stage 0 → 1 → 2 | root shape |
|---|---|---|---|---|
| 1 | SIGNAL | PACKET | Paketo → Fragmar → Rafagón | rounded rect cut by an unlit header seam, eye holes in the header field, stub feet |
| 2 | SIGNAL | RADIO | Bippo → Estátic → Jamrón | a thin mast, crossbars, a dish on top, the eye down on the foot |
| 3 | SIGNAL | LATENCY | Lagui → Jitera → Timaut | a solid body with a detached echo trail — the only bodies drawn with a gap |
| 4 | SIGNAL | PIXEL | Pixio → Artefax → Burnix | chunky discrete blocks with 1px seams, dissolving along a diagonal |
| 5 | SIGNAL | SPAM | Spamito → Kadenax → Blaklix | rectangular flats with knocked-out lines of junk text, linked by short joins |
| 6 | CORRUPT | BUG | Buggo → Exploid → Rootkar | domed shell, knocked-out socket eyes with a lit pupil, splayed legs |
| 7 | CORRUPT | WORM | Wormi → Parasix → Plagón | one serpentine body of even thickness, no legs ever, blunt rounded head |
| 8 | CORRUPT | PHISH | Karnada → Klonix → Estafex | an open hook with a barb, and a lure hung on it that has a face |
| 9 | CORRUPT | NULL | Nulix → Voidina → Segfalt | a round body opened by a diagonal void that grows one stage at a time |
| 10 | CORRUPT | BITROT | Bitto → Flipix → Podrix | a flat-topped rectangle eaten at the corners, one wide socket eye |
| 11 | SYSTEM | DAEMON | Daemi → Servik → Kernon | a solid stepped tower, an unlit eye slot, a status light that changes side |
| 12 | SYSTEM | FIREWALL | Proxi → Gateón → Murax | a broad pentagon shield carrying one unlit visor |
| 13 | SYSTEM | MEMORY | Kachi → Memoro → Lekron | a solid vertical slab, contact notches bitten out of the bottom edge |
| 14 | SYSTEM | FILE | Filito → Arkivo → Zipbom | a solid page with the top-right corner cut away, a 1px unlit crease |
| 15 | SIGNAL | PING | Pingo → Floodra → Denyra | a fat open crescent, the focus dot in its mouth as the eye |
| 16 | CORRUPT | GLITCH | Glitchi → Errox → Panika | a solid block cut into scanline slices, one eye straddling a seam |
| 17 | SYSTEM | PORT | Portu → Skanor → Bakdora | a slab pierced by doorways that reach the bottom edge |
| 18 | SYSTEM | CRYPTO | Klavik → Cifrax → Ransora | an angular lozenge carrying a shackle and a keyhole |
| 19 | SYSTEM | PROBE | Probix → Beakon → Twinix | two ovals side by side, one body that never stops being two |
| 20 | SIGNAL | COOKIE | Cookit → Trakkar → Panoptix | an eye on a stalk; never legs, never a torso |

Balance: SIGNAL 7 families (21 species), CORRUPT 6 (18), SYSTEM 7 (21).

### Type has no visual marker, and that is a choice

Nothing on a body says which of the three types it is. There is a *tendency* —
SYSTEM families are architectural and upright (tower, shield, slab, page,
lozenge), CORRUPT are organic or rotting (shell, worm, hook, void, rot), SIGNAL
are thin or emitting (mast, trail, crescent, stalk) — but PACKET and PIXEL are
SIGNAL and read as architecture, so it is a tendency and not a rule, and nothing
enforces it.

Treat it as an observation, not a constraint. Type has to be legible **in the
UI**, not in the silhouette; a 24x24 1-bit body has no spare channel to carry it
without spending the one that carries species identity.

---

## 5. What this means for the corruption mechanic (P9-C5)

P9-C5 specifies the `CORRUPTED` status as *"sprite glitch (petfx XOR-noise rows,
1 in 8 frames)"*. `PBS_CORRUPTED` is already defined in `save_schema.h` and
already reaches `PetView.corrupted`; nothing renders it yet.

**The collision to design around:** this roster is already made of broken bodies,
and one family is literally called GLITCH and is *drawn* as displaced scanlines.
Additive noise on top of PANIKA is not a signal — it is the family motif with more
of the same.

The distinction the roster itself suggests is **stability**, not vocabulary. Every
body here is a *stable* defect: it holds its shape, frame to frame, forever.
Corruption should be that shape **failing to hold** — the same pixels the body is
made of, no longer landing in the same place twice. That reads instantly against
sixty bodies that never move except in their two authored frames, and it needs no
visual vocabulary the roster has not already taught the player.

Whatever is chosen, check it on GLITCH and BITROT before anything else. If it is
invisible there, it is not a mechanic.

---

## 6. What this means for the creator (P8)

The mobile sprite editor ships a 24x24 grid to players. The five laws are what
separates a player's Pebble from a 24x24 of noise, and three of them can be
enforced or nudged in the editor cheaply:

- **one mass** — the editor already needs a connectivity count; show it;
- **eyes are holes** — the blink derivation will silently pick the largest
  enclosed hole, so show the player which holes will blink;
- **nothing is one pixel** — flag hairlines the way `--self-check` does.

Growth-by-degradation does not apply: a custom Pebble has one stage.

---

## 7. Adding a family

The mechanics are in `docs/content.md` §3 (the four-part package deal) and
`tools/sprites/README.md` §6 (the atlas is an ordered gapless tail). The design
side is:

1. **Pick a root shape that survives being wrecked twice.** The test is whether
   you can say the family's three stages as one sentence about the same object —
   the table in section 3 is twenty worked examples.
2. **Decide where the holes go before you draw**, because they are the face and
   the blink both.
3. **Render before you choose.** Sixteen files carry a `DEVIATIONS` block, and
   every one of them reads *"rendered before they were chosen"* — the first idea
   met a 24x24 grid and lost. Budget for that.
4. **Look at it compiled**, not at the source:
   ```bash
   python3 tools/gen_sprites.py --self-check tools/sprites/<name>.txt
   make -C tests spritetool
   ./tests/bin/sprite_dump text <NAME>
   ./tests/bin/sprite_dump blink <NAME>
   ./tests/bin/sprite_dump sheet /tmp/atlas.pbm
   ```
   `sprite_dump` decodes the compiled header the way `drawXBM()` does, so what it
   prints is what the panel shows. A rendering of the source would only prove the
   source says what it says.

---

## 8. Where this fits

| file | owns |
|---|---|
| `tools/sprites/README.md` | the file format, the grid rules, every error message, the atlas order |
| `docs/content.md` | the roster, the JSON packs, `CONTENT_VERSION`, the package deal for a 21st family |
| **this file** | what the bodies look like and why |
| `tools/gen_sprites.py` | the generator and every check that can be automated |
| `tests/tools/sprite_dump.cpp` | looking at the compiled result |
