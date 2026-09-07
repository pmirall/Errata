#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_sprites.py - THE SPRITE PIPELINE (plan P9-C3, built at P9-C1).

    tools/sprites/atlas.txt   (ordered manifest)
    tools/sprites/*.txt       (ASCII art, one character per pixel)
                              ->  Pebblebol/src/data/sprites_pebbles.h

Usage
    python3 tools/gen_sprites.py               # write the header
    python3 tools/gen_sprites.py --check       # regenerate in memory and DIFF
                                               # against the tree; exit 1 on drift
    python3 tools/gen_sprites.py --self-check [FILE...]
                                               # what an ART AGENT runs on its own
                                               # files: parse, validate, RENDER both
                                               # frames as text, report ink and the
                                               # frame-to-frame difference.
                                               # Writes nothing. Exit 1 on any error.
    python3 tools/gen_sprites.py --render NAME # print one set as text

WHY THIS EXISTS AT ALL. data/sprites.h claimed for two years to be generated
from `scratchpad/sprite_src.py`, a file that has never existed in this
repository (`git log --all` over 77 commits finds no trace; ui/petfx.cpp:13 and
ui/xbm_mirror.h:19 already said so). So the atlas was labelled GENERATED, its
generator was unreachable, tools/check.sh had no rule for it, and 9.4 KB of
hand-typed hex was the source of truth for the thing the player actually looks
at. This file is the generator that was missing, written before the art it will
consume, because SIXTY BODIES x TWO FRAMES ARE ABOUT TO BE DRAWN BY FIVE
DIFFERENT HANDS and the format they write against has to be a contract with a
gate behind it rather than a convention.

DETERMINISM IS A CONTRACT, exactly as it is in gen_content.py: two runs over the
same sources produce a byte-identical header. Sets are emitted in manifest
order, never in os.listdir() order; nothing here reads the clock, the
environment or a random source. `--check` is what proves it in the gate.

THE SOURCE FORMAT IS SPECIFIED IN tools/sprites/README.md. That file, not this
one, is what an art agent reads. Everything this script rejects is described
there with the message it prints.

WHAT THIS PIPELINE CANNOT CHECK, said plainly because the rest of the phase
depends on believing it: nothing here knows whether a body looks like a
creature. It checks that the grid is rectangular, that the bytes match the
dimensions the table advertises, that a frame is not empty and that the two
frames of a set differ. A perfectly rectangular 24x24 of noise passes every
check in this file. The only instrument for the property that matters is
tests/tools/sprite_dump.cpp, which renders the COMPILED atlas for a human to
look at.
"""

import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SPRITES = os.path.join(HERE, "sprites")
CONTENT = os.path.join(HERE, "content")
OUT_H = os.path.join(ROOT, "Pebblebol", "src", "data", "sprites_pebbles.h")
MANIFEST = os.path.join(SPRITES, "atlas.txt")

# --- the shape of a species body, and the one place to change it -------------
# A species body is 24x24x2 and that is not a preference. petfx's decode cache
# is sized by PF_MAX_W/H, xbm_mirror.h caps a mirrored row at 40 px, the HOME
# sprite band is 43 rows, and the end-state budget is 60 x 144 B = 8,640 B. A
# 32x32 species body would be 128 B each, 7,680 B more across the roster, and
# would silently re-widen the RAM caches this atlas is meant to shrink.
BODY_W = 24
BODY_H = 24
BODY_FRAMES = 2

# Hard limits for ANY set, species or not. 40 is PF_MAX_W / PF_MAX_H
# (ui/petfx.cpp) and XBM_MIRROR_MAX_W (ui/xbm_mirror.h): a wider bitmap
# over-runs the mirror scratch buffer at runtime with no diagnostic.
MAX_W = 40
MAX_H = 40
MAX_FRAMES = 4

# The generated atlas's own ceiling, TIGHTENED AT P9-C3 from 12,288.
#
# 12,288 was this file's half of data/sprites.h's 24,576 B TRANSITION allowance,
# which covered both atlases while they were in the tree at once. The legacy
# atlas is deleted, so the transition is over and both numbers came down to the
# end state plus a stated margin. This one: 64 sets x 144 B = 9,216 B of art
# today, plus 1,024 B - seven more 24x24x2 sets, which is one more three-stage
# family and four effect sets - rounded to 10 KiB.
#
# It is deliberately the TIGHTER of the two ceilings, so a runaway is refused
# by the generator (with a message naming the overrun) before it reaches
# data/sprites.h's 11,264 B static_assert, which counts the icons and emotes
# too. Raising either is a re-plan, not a fix: say so in the commit.
PB_DATA_BYTES_MAX = 10240

INK = "#"
GAP = "."
FRAME_MARK = "--- frame "
KEYS_REQUIRED = ("name", "size", "frames")
KEYS_OPTIONAL = ("species",)


def rel(p):
    """A path as a reader should see it: repo-relative inside the tree, verbatim
    outside it. os.path.relpath alone turns a /tmp path into ../../../tmp/... ."""
    a = os.path.abspath(p)
    return os.path.relpath(a, ROOT) if a.startswith(ROOT + os.sep) else p


class SrcError(Exception):
    """A source error, carrying the position a human needs to fix it.

    Every message this generator prints about a .txt file goes through here, so
    they all look the same and they all name file, line and (where a column
    means something) column. An art agent gets `tools/sprites/paketo.txt:31:25:
    row is 25 characters, size says 24` and not `ValueError`.
    """

    def __init__(self, path, line, col, msg):
        self.path = path
        self.line = line
        self.col = col
        self.msg = msg
        where = rel(path)
        if line is not None:
            where += ":%d" % line
            if col is not None:
                where += ":%d" % col
        Exception.__init__(self, "%s: %s" % (where, msg))


def die(msg):
    sys.stderr.write("gen_sprites.py: %s\n" % msg)
    sys.exit(2)


# =============================================================================
#  PARSING - STRICT, AND LOUD ABOUT EVERY WAY A FILE CAN BE WRONG
#
#  THE ONE RULE THAT MATTERS: this parser never pads a short row, never crops a
#  long one, never guesses a missing frame and never accepts a character it does
#  not know. Silently repairing art is how a body ends up one pixel narrower
#  than the table says it is, which is an out-of-bounds read on the device, and
#  how frame 1 ends up a copy of frame 0 in a roster nobody has time to eyeball
#  twice.
# =============================================================================
def parse_set(path):
    with open(path, "rb") as f:
        raw = f.read()
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as e:
        raise SrcError(path, None, None,
                       "not valid UTF-8 (%s) - save it as UTF-8, LF" % e)
    if "\t" in text:
        line = text[:text.index("\t")].count("\n") + 1
        raise SrcError(path, line, None,
                       "a TAB character - a pixel grid is spaces-free and "
                       "tab-free; use '%s' and '%s' only" % (GAP, INK))

    lines = text.split("\n")
    # A single trailing CR per line is accepted and dropped (an editor writing
    # CRLF is not an art mistake); any OTHER trailing whitespace is an error,
    # because it is invisible and it changes a row's length.
    lines = [ln[:-1] if ln.endswith("\r") else ln for ln in lines]

    stem = os.path.splitext(os.path.basename(path))[0]
    head = {}
    frames = []          # list of list-of-rows
    cur = None
    seen_frame = False

    for i, ln in enumerate(lines):
        n = i + 1
        if ln != ln.rstrip():
            raise SrcError(path, n, len(ln.rstrip()) + 1,
                           "trailing whitespace - a pixel row is exactly its "
                           "own width and nothing else")

        if ln.startswith(FRAME_MARK):
            rest = ln[len(FRAME_MARK):]
            if not re.fullmatch(r"[0-9]+", rest):
                raise SrcError(path, n, len(FRAME_MARK) + 1,
                               "expected `%s<N>` with N a decimal number, got %r"
                               % (FRAME_MARK, ln))
            # Close the frame in progress FIRST, so the number this block
            # announces is checked against how many are already complete.
            if cur is not None:
                _finish_frame(frames, cur)
            want = len(frames)
            if int(rest) != want:
                raise SrcError(path, n, len(FRAME_MARK) + 1,
                               "frames must be numbered from 0 in order - "
                               "expected `%s%d`, got `%s`" % (FRAME_MARK, want, ln))
            cur = []
            seen_frame = True
            continue

        if cur is None:
            # --- header region: keys and comments only ---
            if ln == "" or ln.startswith("#"):
                continue
            m = re.fullmatch(r"([a-z_]+):[ ]*(.*)", ln)
            if not m:
                raise SrcError(path, n, 1,
                               "expected `key: value`, a `# comment`, a blank "
                               "line, or `%s0` - got %r" % (FRAME_MARK, ln))
            key, val = m.group(1), m.group(2)
            if key not in KEYS_REQUIRED and key not in KEYS_OPTIONAL:
                raise SrcError(path, n, 1,
                               "unknown key %r. The whole vocabulary is %s "
                               "(required) and %s (optional). Keys are a "
                               "contract: do not invent one, add it to "
                               "tools/gen_sprites.py and README.md instead"
                               % (key, ", ".join(KEYS_REQUIRED),
                                  ", ".join(KEYS_OPTIONAL)))
            if key in head:
                raise SrcError(path, n, 1, "duplicate key %r" % key)
            head[key] = (val, n)
            continue

        # --- inside a frame block: pixel rows, and nothing else ---
        if ln == "":
            if all(x == "" for x in lines[i:]):
                continue          # trailing newline(s) at end of file
            raise SrcError(path, n, 1,
                           "blank line inside a frame block. A frame is exactly "
                           "%s rows with nothing between them; a blank line here "
                           "is indistinguishable from a missing row"
                           % _val_or(head, "size", "its declared height"))
        bad = _first_bad_char(ln)
        if bad >= 0:
            raise SrcError(path, n, bad + 1,
                           "character %r is not %r (ink) or %r (empty). "
                           "Comments are not allowed inside a frame block - "
                           "'#' there is a pixel"
                           % (ln[bad], INK, GAP))
        cur.append(ln)

    if not seen_frame:
        raise SrcError(path, None, None,
                       "no `%s0` block - the file declares a sprite and then "
                       "draws nothing" % FRAME_MARK)
    _finish_frame(frames, cur)

    return _validate_head(path, stem, head, frames)


def _val_or(head, key, alt):
    return head[key][0] if key in head else alt


def _first_bad_char(ln):
    for j, ch in enumerate(ln):
        if ch != INK and ch != GAP:
            return j
    return -1


def _finish_frame(frames, rows):
    frames.append(rows)


def _validate_head(path, stem, head, frames):
    for k in KEYS_REQUIRED:
        if k not in head:
            raise SrcError(path, None, None,
                           "missing required key `%s:`" % k)

    name, name_line = head["name"]
    if not re.fullmatch(r"[A-Z][A-Z0-9_]*", name):
        raise SrcError(path, name_line, 1,
                       "name %r must be UPPER_SNAKE: [A-Z][A-Z0-9_]*. It becomes "
                       "the C enum tag PBSPR_%s and the array pb_spr_%s"
                       % (name, name, name.lower()))
    if name.lower() != stem:
        raise SrcError(path, name_line, 1,
                       "name %r does not match the filename %r. They are the "
                       "same identity: %s.txt must declare `name: %s`"
                       % (name, stem + ".txt", name.lower(), stem.upper()))

    size, size_line = head["size"]
    m = re.fullmatch(r"([0-9]+)x([0-9]+)", size)
    if not m:
        raise SrcError(path, size_line, 1,
                       "size %r is not `<width>x<height>`, e.g. `size: %dx%d`"
                       % (size, BODY_W, BODY_H))
    w, h = int(m.group(1)), int(m.group(2))
    if w < 1 or h < 1 or w > MAX_W or h > MAX_H:
        raise SrcError(path, size_line, 1,
                       "size %dx%d is outside 1x1..%dx%d. %d is PF_MAX_W/H "
                       "(ui/petfx.cpp) and XBM_MIRROR_MAX_W (ui/xbm_mirror.h): "
                       "a wider bitmap over-runs a runtime scratch buffer"
                       % (w, h, MAX_W, MAX_H, MAX_W))

    nf, nf_line = head["frames"]
    if not re.fullmatch(r"[0-9]+", nf):
        raise SrcError(path, nf_line, 1, "frames %r is not a number" % nf)
    nf = int(nf)
    if nf < 1 or nf > MAX_FRAMES:
        raise SrcError(path, nf_line, 1,
                       "frames %d is outside 1..%d" % (nf, MAX_FRAMES))
    if len(frames) != nf:
        raise SrcError(path, None, None,
                       "`frames: %d` but the file holds %d `%s` block(s)"
                       % (nf, len(frames), FRAME_MARK.strip()))

    species = None
    if "species" in head:
        sval, sline = head["species"]
        if not re.fullmatch(r"[0-9]+", sval):
            raise SrcError(path, sline, 1, "species %r is not a number" % sval)
        species = int(sval)
        if species < 1 or species > 199:
            raise SrcError(path, sline, 1,
                           "species %d is outside 1..199 (200+ is the creator "
                           "range, networking/protocol.cpp)" % species)
        if (w, h) != (BODY_W, BODY_H):
            raise SrcError(path, size_line, 1,
                           "a species body is %dx%d, not %dx%d - see the size "
                           "note in tools/gen_sprites.py"
                           % (BODY_W, BODY_H, w, h))
        if nf != BODY_FRAMES:
            raise SrcError(path, nf_line, 1,
                           "a species body has exactly %d frames, not %d"
                           % (BODY_FRAMES, nf))

    for fi, rows in enumerate(frames):
        if len(rows) != h:
            raise SrcError(path, None, None,
                           "frame %d has %d rows, `size: %dx%d` says %d. Nothing "
                           "here pads or crops: draw the missing rows"
                           % (fi, len(rows), w, h, h))
        for ri, r in enumerate(rows):
            if len(r) != w:
                raise SrcError(path, None, None,
                               "frame %d row %d is %d characters, `size: %dx%d` "
                               "says %d" % (fi, ri, len(r), w, h, w))
        if all(ch == GAP for row in rows for ch in row):
            raise SrcError(path, None, None,
                           "frame %d is empty. A blank frame draws nothing at "
                           "all - on a two-frame body it makes the pet vanish "
                           "every other tick" % fi)

    return {
        "path": path,
        "name": name,
        "w": w,
        "h": h,
        "frames": frames,
        "species": species,
        "notes": _comment_block(path),
    }


def _comment_block(path):
    """The leading `#` comments, verbatim, so the pack's silhouette brief travels
    from the .txt into the generated header and a reviewer sees the brief and
    the pixels in the same place."""
    out = []
    with open(path, encoding="utf-8") as f:
        for ln in f:
            ln = ln.rstrip("\n").rstrip("\r")
            if ln.startswith(FRAME_MARK):
                break
            if ln.startswith("#"):
                out.append(ln[1:].strip())
    return [x for x in out if x]


# =============================================================================
#  THE MANIFEST, AND THE SPECIES BINDING
# =============================================================================
def read_manifest():
    if not os.path.exists(MANIFEST):
        die("%s is missing - it is the atlas ORDER, which is a wire contract "
            "(data/sprites.h:112), and it may not be inferred from the "
            "filesystem" % os.path.relpath(MANIFEST, ROOT))
    names = []
    with open(MANIFEST, encoding="utf-8") as f:
        for i, ln in enumerate(f):
            ln = ln.strip()
            if not ln or ln.startswith("#"):
                continue
            if not re.fullmatch(r"[A-Z][A-Z0-9_]*", ln):
                die("%s:%d: %r is not an UPPER_SNAKE set name"
                    % (os.path.relpath(MANIFEST, ROOT), i + 1, ln))
            if ln in names:
                die("%s:%d: %s is listed twice"
                    % (os.path.relpath(MANIFEST, ROOT), i + 1, ln))
            names.append(ln)
    if not names:
        die("%s lists no sets" % os.path.relpath(MANIFEST, ROOT))
    return names


def load_sets():
    """Every set in the atlas, in manifest order, parsed and cross-checked.

    Both directions are checked. A manifest entry with no file is a build that
    would emit a dangling pointer; a FILE with no manifest entry is art somebody
    drew that would silently never ship, which is the worse of the two because
    it looks like success.
    """
    order = read_manifest()
    on_disk = {}
    for fn in sorted(os.listdir(SPRITES)):
        if not fn.endswith(".txt") or fn == "atlas.txt":
            continue
        on_disk[os.path.splitext(fn)[0].upper()] = os.path.join(SPRITES, fn)

    missing = [n for n in order if n not in on_disk]
    if missing:
        die("listed in atlas.txt with no source file: %s (expected %s)"
            % (", ".join(missing),
               ", ".join("tools/sprites/%s.txt" % n.lower() for n in missing)))
    unlisted = [n for n in sorted(on_disk) if n not in order]
    if unlisted:
        die("art that would never ship - these files exist but are not in "
            "atlas.txt, so nothing would reference them: %s"
            % ", ".join("tools/sprites/%s.txt" % n.lower() for n in unlisted))

    sets = []
    for n in order:
        try:
            sets.append(parse_set(on_disk[n]))
        except SrcError as e:
            die(str(e))
    check_species_binding(sets)
    return sets


def species_names():
    """id -> name from the content pack, or None when the pack is absent.

    The pack is the roster's source of truth; this generator only ever READS it,
    so a body cannot be bound to a species that does not exist and cannot sit in
    another species' slot.
    """
    path = os.path.join(CONTENT, "species.json")
    if not os.path.exists(path):
        return None
    with open(path, encoding="utf-8") as f:
        return {s["id"]: s["name"] for s in json.load(f)}


def slug(name):
    """A species name folded to the A-Z0-9_ a C identifier and a set name allow.
    Latin-1 accents fold to their base letter; everything else is dropped."""
    table = {"Á": "A", "É": "E", "Í": "I", "Ó": "O", "Ú": "U", "Ü": "U",
             "Ñ": "N", "Ç": "C"}
    out = []
    for ch in name.upper():
        ch = table.get(ch, ch)
        if re.fullmatch(r"[A-Z0-9]", ch):
            out.append(ch)
        elif ch in " -'":
            out.append("_")
    return "".join(out)


def check_species_binding(sets):
    """THE SLOT CHECK. A body in the wrong slot draws the wrong creature on HOME
    and nothing else in the tree would notice: every id stays in range, every
    array is the right size, the gate stays green and Paketo wears Murax's face.

    The invariant, from tools/content/species.json and data/species_table.h:
    `sprite_id == id - 1`, so a species body's position in the atlas is
    determined, not chosen. Here that means: the species-bound sets are the TAIL
    of the manifest, their ids run 1, 2, 3, ... with no gaps, and each one's name
    matches the pack's name for that id.
    """
    bound = [(i, s) for i, s in enumerate(sets) if s["species"] is not None]
    if not bound:
        return
    first = bound[0][0]
    tail = sets[first:]
    for s in tail:
        if s["species"] is None:
            die("%s has no `species:` key but sits inside the species block "
                "(after %s). The species bodies are the TAIL of atlas.txt: "
                "eggs and effects first, then one body per species in roster "
                "order" % (rel(s["path"]), sets[first]["name"]))
    for k, (_, s) in enumerate(bound):
        want = k + 1
        if s["species"] != want:
            die("%s says `species: %d` but it is entry %d of the species block, "
                "which must be species %d. The pack's invariant is "
                "sprite_id == id - 1: a body's slot is DETERMINED by its id, so "
                "atlas.txt lists them 1, 2, 3, ... with no gaps and no "
                "reordering" % (rel(s["path"]), s["species"], k + 1, want))

    pack = species_names()
    if pack is None:
        return
    for _, s in bound:
        sid = s["species"]
        if sid not in pack:
            die("%s is bound to species %d, which tools/content/species.json "
                "does not contain (the pack holds ids 1..%d)"
                % (rel(s["path"]), sid, max(pack)))
        want = slug(pack[sid])
        if s["name"] != want:
            die("%s is bound to species %d (%s in the pack), so its set must be "
                "named %s and the file %s.txt - not %s"
                % (rel(s["path"]), sid, pack[sid], want, want.lower(), s["name"]))


# =============================================================================
#  WHAT A GRID LOOKS LIKE, MEASURED
#
#  Everything here answers a question a byte count cannot: how many separate
#  pieces is this body in, how much of its ink is one pixel wide (which reads as
#  dirt at 1x, not as anatomy), and where are its eyes. They are reported by
#  --self-check and, for the eyes, EMITTED, so ui/petfx.cpp's eyelid table stops
#  being a second hand-maintained copy of the art it indexes.
# =============================================================================
def _lit(rows, x, y, w, h):
    return 0 <= x < w and 0 <= y < h and rows[y][x] == INK


def components(rows, w, h):
    """Number of 4-connected runs of ink. A body drawn as one mass is 1; a
    deliberate detached part (a pupil, a trail, a hanging door) raises it.

    THIS IS THE ONE CHECK THAT CATCHES A CLASS OF DEFECT EVERY BYTE CHECK
    PASSES. It is what found MURAX's two crenellations floating free of their
    rim: --self-check called that file OK before and after the break, because
    the grid was still rectangular and the frames still differed. It is
    reported, never fatal - detached ink is legal and several bodies use it -
    but a number that moves when you did not mean to move it is the point.
    """
    seen = [[False] * w for _ in range(h)]
    n = 0
    for y in range(h):
        for x in range(w):
            if rows[y][x] == INK and not seen[y][x]:
                n += 1
                stack = [(x, y)]
                seen[y][x] = True
                while stack:
                    cx, cy = stack.pop()
                    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                        nx, ny = cx + dx, cy + dy
                        if _lit(rows, nx, ny, w, h) and not seen[ny][nx]:
                            seen[ny][nx] = True
                            stack.append((nx, ny))
    return n


def hairlines(rows, w, h):
    """Ink pixels that are 1 px wide in BOTH axes locally - no lit neighbour
    left or right AND none above or below. At 1x these do not read as a limb or
    an antenna, they read as a stuck pixel."""
    n = 0
    for y in range(h):
        for x in range(w):
            if rows[y][x] != INK:
                continue
            if (not _lit(rows, x - 1, y, w, h) and not _lit(rows, x + 1, y, w, h)
                    and not _lit(rows, x, y - 1, w, h)
                    and not _lit(rows, x, y + 1, w, h)):
                n += 1
    return n


# The band pf_build_lids() closes when a body blinks. y1 < y0 means "this body
# does not blink" and ui/petfx_core.cpp's pf_build_lids() already returns 0 for
# it.
NO_EYES = (255, 0, 0, 0)

# An eye is a small hole. These two numbers are what "small" means, and they are
# the difference between a blink and a body going solid:
#   EYE_MAX_H  an eye socket is not nine rows tall. Above this the hole is a
#              cavity, a slot or the gap inside an arc. (The DEVICE truncates a
#              band at PF_EYE_MAX_H = 12 without saying so; staying under 8 means
#              that truncation can never fire.)
#   EYE_MIN_PX a one- or two-pixel nick is not an eye, and a "blink" that moves
#              two pixels is a claim the panel does not honour.
# A hole wider than half the body is not an eye either.
EYE_MAX_H = 8
EYE_MIN_PX = 3
EYE_MAX_CAND = 8            # bounds the subset search below; see _eye_groups()


def _holes(rows, w, h):
    """Unlit pixels the background cannot reach: the enclosed holes of the
    drawing. Flood 4-connected from the border, everything unlit and unvisited
    is a hole."""
    outside = [[False] * w for _ in range(h)]
    stack = []
    for x in range(w):
        for y in (0, h - 1):
            if rows[y][x] != INK and not outside[y][x]:
                outside[y][x] = True
                stack.append((x, y))
    for y in range(h):
        for x in (0, w - 1):
            if rows[y][x] != INK and not outside[y][x]:
                outside[y][x] = True
                stack.append((x, y))
    while stack:
        cx, cy = stack.pop()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = cx + dx, cy + dy
            if (0 <= nx < w and 0 <= ny < h and rows[ny][nx] != INK
                    and not outside[ny][nx]):
                outside[ny][nx] = True
                stack.append((nx, ny))
    return set((x, y) for y in range(h) for x in range(w)
               if rows[y][x] != INK and not outside[y][x])


def _hole_clusters(rows, w, h):
    """The holes grouped into 4-connected clusters: one cluster is one socket.
    Biggest first, then topmost, then leftmost, so the order is deterministic."""
    hs = _holes(rows, w, h)
    seen, out = set(), []
    for p in sorted(hs):
        if p in seen:
            continue
        stack, cl = [p], []
        seen.add(p)
        while stack:
            c = stack.pop()
            cl.append(c)
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                q = (c[0] + dx, c[1] + dy)
                if q in hs and q not in seen:
                    seen.add(q)
                    stack.append(q)
        xs = [a for a, _ in cl]
        ys = [b for _, b in cl]
        out.append({"n": len(cl), "x0": min(xs), "x1": max(xs),
                    "y0": min(ys), "y1": max(ys)})
    out.sort(key=lambda c: (-c["n"], c["y0"], c["x0"]))
    return out


def blink_fill(rows, w, h, band):
    """WHAT THE DEVICE WILL ACTUALLY FILL, by the same rule as the device.

    This is a transcription of ui/petfx_core.cpp's pf_build_lids() fill loop -
    every run of unlit pixels bounded by ink ON ITS OWN ROW, inside the x window,
    on every row of the band. It is a SECOND implementation and it is here on
    purpose: the generator's job is to emit a band that this rule handles, so it
    has to know the rule. It is not the guard. The guard is
    tests/test_sprite_pipeline.cpp, which calls the SHIPPED pf_build_lids() over
    the SHIPPED atlas and asserts the same property; if this transcription ever
    drifts from that function, that test fails and this one goes quiet, which is
    the right way round.

    Returns the set of (x, y) the fill would light, or None for "no band".
    """
    y0, y1, x0, x1 = band
    if y0 == 255 or y1 >= h or y1 < y0:
        return None
    bh = min(y1 - y0 + 1, 12)                    # PF_EYE_MAX_H
    x1 = min(x1, w - 1)
    out = set()
    for r in range(bh):
        row = rows[y0 + r]
        lit = [x for x in range(w) if row[x] == INK]
        if not lit:
            continue
        last = lit[-1]
        x = 0
        while x < last and row[x] != INK:
            x += 1
        while x < last:
            if row[x] == INK:
                x += 1
                continue
            s = x
            while x <= last and row[x] != INK:
                x += 1
            if s >= x0 and (x - 1) <= x1:
                for k in range(s, x):
                    out.add((k, y0 + r))
    return out or None


def _eye_groups(cand):
    """Every set of candidate holes that can be CLOSED TOGETHER: their rows must
    overlap, or one would be shut while the other is open. Yielded best-first -
    most holes, then most pixels, then highest on the body - so the first one
    that survives verification is the answer and the search stops."""
    import itertools
    idx = range(len(cand))
    subs = []
    for r in range(len(cand), 0, -1):
        for sub in itertools.combinations(idx, r):
            g = [cand[i] for i in sub]
            if any(not (g[i]["y0"] <= g[j]["y1"] and g[i]["y1"] >= g[j]["y0"])
                   for i in range(len(g)) for j in range(i + 1, len(g))):
                continue
            subs.append(g)
    subs.sort(key=lambda g: (-len(g), -sum(c["n"] for c in g),
                             min(c["y0"] for c in g)))
    return subs


def eye_band(rows, w, h):
    """WHERE THE EYES ARE, DERIVED FROM THE ART instead of measured by hand.

    THE RULE, and it is about the drawing rather than about a percentage of the
    box:

        an eye is an enclosed HOLE - unlit pixels the background cannot reach -
        of at least EYE_MIN_PX pixels, at most EYE_MAX_H rows tall and at most
        half the body wide. THE EYES are the biggest group of such holes that
        can be closed together WITHOUT CLOSING ANYTHING THAT IS NOT A HOLE.

    That last clause is the whole of it, and it is checked rather than assumed:
    the candidate band is run through blink_fill() - the device's own row-wise
    rule - and rejected if one filled pixel is not a hole pixel. Groups are tried
    best-first (most holes, then most pixels, then highest), so a PAIR of eyes
    beats a single decorative slot even when the slot has more pixels; PAKETO is
    exactly that case.

    WHAT THIS REPLACED, because the failure is the reason the rule is written
    this way. Until P9-C6 the rule was "the bounding box of every enclosed hole
    in the top 60 % of the ink box", and its own docstring called what it got
    wrong "an ugly blink on one body". It was four bodies and it was not ugly:
    the band became most of the sprite, pf_build_lids() fills every interior run
    on every row of the band, and the blink drew the creature solid - DENYRA
    +79 px on 283, BLAKLIX +78 on 264, MURAX +68 on 242, and PANOPTIX losing all
    nine of the eyes it is named for. Nothing in the tree could see it: the
    composited blink frame is not in the atlas, and the only assertion about the
    band was that it lay inside the sprite and contained one unlit pixel.

    The old rule also claimed "every one of the sixty bodies was drawn to that
    rule on purpose". It was not: BIPPO ("one eye on the foot"), TIMAUT ("the
    hollow is the face") and KLONIX ("the mask's eye slots blink shut") all draw
    their face BELOW the 60 % cut and all three were silently emitted as
    non-blinkers. There is no cut any more.

    WHAT THIS ONE CANNOT DO, said as plainly. It cannot tell an eye from any
    other small enclosed hole - a porthole, a bolt, a bite - so a body whose only
    small hole is decorative blinks with that instead. It never fills anything
    that is not a hole, and it never reaches outside the body, so the worst case
    is a blink in the wrong place rather than a body erased. Which bodies blink,
    where, and by how many pixels is printed per file by --self-check and drawn
    by `tests/bin/sprite_dump blink NAME`, and a human has to look.
    """
    x0b, y0b, x1b, y1b, ink = ink_box(rows)
    if not ink:
        return NO_EYES
    hs = _holes(rows, w, h)
    cand = [c for c in _hole_clusters(rows, w, h)
            if c["n"] >= EYE_MIN_PX
            and (c["y1"] - c["y0"] + 1) <= EYE_MAX_H
            and (c["x1"] - c["x0"] + 1) <= w // 2][:EYE_MAX_CAND]
    if not cand:
        return NO_EYES
    for g in _eye_groups(cand):
        band = (min(c["y0"] for c in g), max(c["y1"] for c in g),
                max(0, min(c["x0"] for c in g) - 1),
                min(w - 1, max(c["x1"] for c in g) + 1))
        fill = blink_fill(rows, w, h, band)
        if fill and all(p in hs for p in fill):
            return band
    return NO_EYES


# =============================================================================
#  XBM
# =============================================================================
def pack_frame(rows, w):
    """One ASCII frame to XBM bytes. Row stride ((w+7)>>3), LSB = leftmost pixel.

    This is the WHOLE bit-order contract, in six lines, and it is checked
    end-to-end rather than believed: tools/sprites/egg_idle.txt and
    egg_crack.txt are the legacy art transcribed back into ASCII, and
    tests/test_sprite_pipeline.cpp asserts the bytes this function produces for
    them are byte-identical to data/sprites.h's hand-typed hex. If the shift
    below were backwards, that test fails on 288 bytes of art that has been on
    screen since phase 1.
    """
    stride = (w + 7) >> 3
    out = bytearray()
    for r in rows:
        row = bytearray(stride)
        for x, ch in enumerate(r):
            if ch == INK:
                row[x >> 3] |= 1 << (x & 7)
        out += row
    return bytes(out)


def pack_set(s):
    return b"".join(pack_frame(f, s["w"]) for f in s["frames"])


def set_bytes(s):
    return ((s["w"] + 7) >> 3) * s["h"] * len(s["frames"])


def art_hash(sets):
    """FNV-1a 32 over the emitted bytes and the set order, folded to 16 bits.

    THE SAME SHAPE AS CONTENT_VERSION and for the same reason: SPRITE_REV is a
    number a human is supposed to bump on any art change, and a number a human
    is supposed to bump is a number that does not get bumped (data/sprites.h's
    own SPRITE_DATA_BYTES was wrong by 144 B for two phases). This one is
    derived, so P9-C3 can wire SPRITE_REV to it and the eyelid table's
    `static_assert(SPRITE_REV == 1)` in ui/petfx.cpp:41 becomes a build break
    that fires exactly when the art it was measured against moves.
    """
    h = 0x811C9DC5
    for s in sets:
        eyes = b"".join(bytes(eye_band(f, s["w"], s["h"])) for f in s["frames"])
        for b in (s["name"].encode("ascii") + bytes([s["w"], s["h"],
                                                     len(s["frames"])])
                  + pack_set(s) + eyes):
            h ^= b
            h = (h * 0x01000193) & 0xFFFFFFFF
    v = (h ^ (h >> 16)) & 0xFFFF
    return v if v != 0 else 1


# =============================================================================
#  EMIT
# =============================================================================
def hexrows(data, per_line=16, indent="  "):
    out = []
    for i in range(0, len(data), per_line):
        chunk = data[i:i + per_line]
        out.append(indent + " ".join("0x%02X," % b for b in chunk))
    return "\n".join(out)


def emit(sets):
    bodies = [s for s in sets if s["species"] is not None]
    body_first = sets.index(bodies[0]) if bodies else len(sets)
    total = sum(set_bytes(s) for s in sets)
    o = []
    o.append("// " + "=" * 77)
    o.append("//  PEBBLEBOL - data/sprites_pebbles.h - THE GENERATED ATLAS")
    o.append("//")
    o.append("//  GENERATED FILE. Do not edit: tools/gen_sprites.py rewrites it from")
    o.append("//  tools/sprites/*.txt (ASCII art, one character per pixel).")
    o.append("//  `tools/gen_sprites.py --check` fails the gate if this file and the ASCII")
    o.append("//  sources have drifted apart, in either direction.")
    o.append("//")
    o.append("//  Format: XBM - data/sprite_types.h owns the stride, the bit order and the")
    o.append("//  two size-guard macros. Row stride ((w+7)>>3) bytes, LSB of a byte is the")
    o.append("//  LEFTMOST pixel, frames stored back to back in one array.")
    o.append("//")
    o.append("//  NOTHING IN THE FIRMWARE INCLUDES THIS HEADER YET, and that is deliberate:")
    o.append("//  P9-C1 built the pipeline, P9-C3 draws the 60 bodies and performs the swap")
    o.append("//  (delete the 36 legacy body/pose sets from data/sprites.h, point")
    o.append("//  sprite_set_id() at PB_SPRITE_BODY_FIRST + sprite_id, re-record the nine")
    o.append("//  goldens that contain body art). Until then the only consumer is")
    o.append("//  tests/test_sprite_pipeline.cpp, which is what keeps this file compiling")
    o.append("//  and what proves the two EGG sets here are byte-identical to the ones")
    o.append("//  data/sprites.h has been drawing since phase 1.")
    o.append("//")
    o.append("//  %d set(s), %d species bod%s, %d B of art."
             % (len(sets), len(bodies), "y" if len(bodies) == 1 else "ies", total))
    o.append("// " + "=" * 77)
    o.append("#ifndef PB_SPRITES_PEBBLES_H")
    o.append("#define PB_SPRITES_PEBBLES_H")
    o.append("")
    o.append("#include <stdint.h>")
    o.append('#include "sprite_types.h"')
    o.append("")
    o.append("// Derived from the emitted bytes, the set order and the eye bands (FNV-1a 32")
    o.append("// folded to 16), so it moves when the art moves and cannot be forgotten.")
    o.append("// data/sprites.h defines SPRITE_REV as this value.")
    o.append("//")
    o.append("// WHAT IT DOES NOT BUY, corrected at P9-C6 because this banner claimed it for")
    o.append("// three chunks: there is no static_assert(SPRITE_REV == n) anywhere any more.")
    o.append("// P9-C3 deleted it together with the hand-measured eyelid table it froze - a")
    o.append("// derived hash pinned to a literal would have to be re-typed after every art")
    o.append("// edit, which is the failure mode the assert existed to prevent. This number")
    o.append("// is a DIAG readout and a cache key. The guards on the art are")
    o.append("// tools/gen_sprites.py --check (drift), the static_asserts below (geometry)")
    o.append("// and tests/test_sprite_pipeline.cpp (everything else).")
    o.append("#define PB_SPRITE_ART_HASH 0x%04Xu" % art_hash(sets))
    o.append("")
    o.append("// Set ids. The ORDER IS A CONTRACT and comes from tools/sprites/atlas.txt,")
    o.append("// never from the filesystem. The species block is the tail: a body's slot is")
    o.append("// PB_SPRITE_BODY_FIRST + (species_id - 1), which is the pack's")
    o.append("// `sprite_id == id - 1` invariant expressed in the atlas.")
    o.append("enum PbSpriteSetId : uint8_t {")
    for i, s in enumerate(sets):
        tag = "PBSPR_%s = %d," % (s["name"], i)
        note = "species %d" % s["species"] if s["species"] else "fixed slot"
        o.append("  %-38s // %s" % (tag, note))
    o.append("  PB_SPRITE_SET_COUNT = %d" % len(sets))
    o.append("};")
    o.append("")
    o.append("#define PB_SPRITE_BODY_FIRST  %d" % body_first)
    o.append("#define PB_SPRITE_BODY_COUNT  %d" % len(bodies))
    o.append("")
    o.append("// The set names, for tools/ and tests/ that have to PRINT one - chiefly")
    o.append("// tests/tools/sprite_dump.cpp, which renders this atlas for a human to look")
    o.append("// at. Emitted rather than transcribed so a name list can never drift from the")
    o.append("// art it labels. `inline constexpr` and referenced by no firmware translation")
    o.append("// unit, so the linker keeps none of it in .rodata.")
    o.append("inline constexpr const char* const PB_SPRITE_NAMES[PB_SPRITE_SET_COUNT] = {")
    for s in sets:
        o.append('  "%s",' % s["name"])
    o.append("};")
    o.append("")
    o.append("// " + "-" * 77)
    o.append("//  PIXEL DATA")
    o.append("// " + "-" * 77)
    for s in sets:
        o.append("")
        o.append("// %s  %dx%d x%d  (%d B)  <- %s"
                 % (s["name"], s["w"], s["h"], len(s["frames"]),
                    set_bytes(s), rel(s["path"])))
        for line in s["notes"]:
            o.append("// %s" % line)
        o.append("inline constexpr uint8_t pb_spr_%s[] = {" % s["name"].lower())
        o.append(hexrows(pack_set(s)))
        o.append("};")
    o.append("")
    o.append("// " + "-" * 77)
    o.append("//  TABLE")
    o.append("// " + "-" * 77)
    o.append("inline constexpr SpriteSet PB_SPRITE_SETS[PB_SPRITE_SET_COUNT] = {")
    for s in sets:
        o.append("  { pb_spr_%s, %d, %d, %d },  // %s"
                 % (s["name"].lower(), s["w"], s["h"], len(s["frames"]), s["name"]))
    o.append("};")
    o.append("")
    o.append("// " + "-" * 77)
    o.append("//  EYE BANDS - DERIVED FROM THE ART, NOT MEASURED BY HAND")
    o.append("//")
    o.append("//  ui/petfx.cpp used to carry a 24-row table of blink bands read off the")
    o.append("//  decoded art BY EYE, 1,100 lines away from the pixels it indexed, frozen")
    o.append("//  by static_assert(SPRITE_REV == 1) because nothing else could notice when")
    o.append("//  the two drifted. Sixty bodies would have made it sixty rows of the same.")
    o.append("//  These come out of the same .txt files as the pixels, by one stated rule -")
    o.append("//  an eye is an enclosed HOLE, and THE EYES are the biggest group of holes")
    o.append("//  that can be closed together without closing anything that is not a hole -")
    o.append("//  so they cannot drift from the art, and what the rule gets wrong is written")
    o.append("//  down in gen_sprites.py's eye_band() rather than discovered.")
    o.append("//")
    o.append("//  THE GUARD IS NOT HERE. The band is only half the blink; the other half is")
    o.append("//  ui/petfx_core.cpp's pf_build_lids(), and until P9-C6 that function was in")
    o.append("//  a translation unit no host binary could link, so the composited blink")
    o.append("//  frame was a picture nothing in the tree had ever drawn. It is now:")
    o.append("//  tests/test_sprite_pipeline.cpp runs the shipped fill over the shipped")
    o.append("//  atlas, and tests/bin/sprite_dump blink NAME draws it for a human.")
    o.append("//")
    o.append("//  { 255, 0, 0, 0 } means THIS BODY DOES NOT BLINK: y1 < y0, which")
    o.append("//  pf_build_lids() already answers 0 for. It is what a body with no")
    o.append("//  small enclosed hole gets, and it is a legal answer.")
    o.append("inline constexpr SpriteEyeBand PB_SPRITE_EYES[PB_SPRITE_SET_COUNT][%d] = {"
             % max(len(s["frames"]) for s in sets))
    for s in sets:
        cells = []
        for f in s["frames"]:
            cells.append("{ %3d, %3d, %3d, %3d }" % eye_band(f, s["w"], s["h"]))
        while len(cells) < max(len(x["frames"]) for x in sets):
            cells.append("{ 255,   0,   0,   0 }")
        o.append("  { %s },  // %s" % (", ".join(cells), s["name"]))
    o.append("};")
    o.append("")
    o.append("// Every row tied to the array it points at, frames included. drawXBM() reads")
    o.append("// ((w+7)>>3)*h bytes with no bound of its own, so a row that disagrees with")
    o.append("// its array is a silent read into the NEXT sprite - see data/sprite_types.h.")
    for s in sets:
        o.append("NT_SPR_SET_FITS(pb_spr_%s, PB_SPRITE_SETS, PBSPR_%s);"
                 % (s["name"].lower(), s["name"]))
    o.append("")
    o.append("// The art budget, MEASURED by the compiler through the table. The declared")
    o.append("// number is what the generator counted; the equality assert is what makes a")
    o.append("// disagreement between the two a named build failure instead of a comment.")
    o.append("constexpr unsigned pb_sprite_atlas_bytes() {")
    o.append("  unsigned n = 0;")
    o.append("  for (unsigned i = 0; i < (unsigned)PB_SPRITE_SET_COUNT; ++i)")
    o.append("    n += spr_set_bytes(PB_SPRITE_SETS[i]);")
    o.append("  return n;")
    o.append("}")
    o.append("#define PB_SPRITE_DATA_BYTES          (pb_sprite_atlas_bytes())")
    o.append("#define PB_SPRITE_DATA_BYTES_DECLARED %du" % total)
    o.append("#define PB_SPRITE_DATA_BYTES_MAX      %du" % PB_DATA_BYTES_MAX)
    o.append("static_assert(PB_SPRITE_DATA_BYTES == PB_SPRITE_DATA_BYTES_DECLARED,")
    o.append('              "generated atlas size disagrees with the generator");')
    o.append("static_assert(PB_SPRITE_DATA_BYTES <= PB_SPRITE_DATA_BYTES_MAX,")
    o.append('              "generated sprite art over its half of the flash budget");')
    o.append("")
    o.append("// EVERY EYE BAND LIES INSIDE THE BODY IT BELONGS TO. pf_build_lids() blits")
    o.append("// the fill and lash strips at (x, y + y0) for (y1 - y0 + 1) rows with a plain")
    o.append("// drawXBM, so a band that runs past the sprite's own height writes rows that")
    o.append("// are not in the sprite - silently, on the device, with no diagnostic. The")
    o.append("// bands are derived, so this cannot fail from a typo; it CAN fail from a")
    o.append("// change to eye_band() or to the sprite dimensions, which is the whole reason")
    o.append("// it is a compile error and not a comment.")
    o.append("constexpr bool pb_sprite_eyes_fit() {")
    o.append("  for (unsigned i = 0; i < (unsigned)PB_SPRITE_SET_COUNT; ++i)")
    o.append("    for (unsigned f = 0; f < 2u; ++f) {")
    o.append("      const SpriteEyeBand& e = PB_SPRITE_EYES[i][f];")
    o.append("      if (e.y1 < e.y0) continue;            // 'does not blink'")
    o.append("      if (e.y1 >= PB_SPRITE_SETS[i].h) return false;")
    o.append("      if (e.x1 >= PB_SPRITE_SETS[i].w) return false;")
    o.append("      if (e.x1 < e.x0) return false;")
    o.append("    }")
    o.append("  return true;")
    o.append("}")
    o.append("static_assert(pb_sprite_eyes_fit(),")
    o.append('              "an eye band runs outside the sprite it indexes");')
    o.append("")
    o.append("#endif  // PB_SPRITES_PEBBLES_H")
    text = "\n".join(ln.rstrip() for ln in o)
    return text.rstrip("\n") + "\n"


# =============================================================================
#  RENDER - the text a human reads
# =============================================================================
def render_frame(s, fi):
    return list(s["frames"][fi])


def render_side_by_side(s):
    cols = [render_frame(s, i) for i in range(len(s["frames"]))]
    out = []
    head = "   ".join(("frame %d" % i).ljust(s["w"]) for i in range(len(cols)))
    out.append("  " + head)
    for y in range(s["h"]):
        out.append("  " + "   ".join(c[y] for c in cols))
    return "\n".join(out)


def ink_box(rows):
    x0, y0, x1, y1, n = 999, 999, -1, -1, 0
    for y, r in enumerate(rows):
        for x, ch in enumerate(r):
            if ch == INK:
                n += 1
                x0, x1 = min(x0, x), max(x1, x)
                y0, y1 = min(y0, y), max(y1, y)
    return (x0, y0, x1, y1, n) if n else (0, 0, 0, 0, 0)


def frame_diff(s):
    if len(s["frames"]) < 2:
        return None
    a, b = s["frames"][0], s["frames"][1]
    return sum(1 for y in range(s["h"]) for x in range(s["w"])
               if a[y][x] != b[y][x])


def describe(s, verbose=True):
    out = []
    out.append("%s  %s  %dx%d x%d  %d B"
               % (rel(s["path"]), s["name"], s["w"], s["h"],
                  len(s["frames"]), set_bytes(s)))
    if s["species"] is not None:
        out.append("  species %d" % s["species"])
    for fi in range(len(s["frames"])):
        rows = s["frames"][fi]
        x0, y0, x1, y1, n = ink_box(rows)
        out.append("  frame %d: %d ink px, box x %d..%d, y %d..%d%s"
                   % (fi, n, x0, x1, y0, y1,
                      "" if y1 == s["h"] - 1 else
                      "  (%d blank row(s) under the body)" % (s["h"] - 1 - y1)))
        hair = hairlines(rows, s["w"], s["h"])
        eb = eye_band(rows, s["w"], s["h"])
        if eb == NO_EYES:
            eyes = "none (this body does not blink)"
        else:
            fill = blink_fill(rows, s["w"], s["h"], eb) or set()
            eyes = ("y %d..%d x %d..%d, blink closes %d px (%d%% of the ink)"
                    % (eb[0], eb[1], eb[2], eb[3], len(fill),
                       (100 * len(fill) + n // 2) // max(1, n)))
        out.append("           %d piece(s), %d hairline px%s, eyes %s"
                   % (components(rows, s["w"], s["h"]), hair,
                      "  <-- 1px-wide ink reads as dirt at 1x" if hair else "",
                      eyes))
    d = frame_diff(s)
    if d is not None:
        out.append("  frames differ in %d px%s"
                   % (d, "  <-- FRAME 1 IS A COPY OF FRAME 0: the idle animation "
                         "does not animate" if d == 0 else ""))
    if verbose:
        out.append(render_side_by_side(s))
    return "\n".join(out)


# =============================================================================
#  DRIVER
# =============================================================================
def warn_identical_frames(sets):
    same = [s["name"] for s in sets if frame_diff(s) == 0]
    if same:
        sys.stderr.write(
            "gen_sprites.py: WARNING - frame 1 is a byte-for-byte copy of "
            "frame 0 in %d set(s): %s\n"
            "  These bodies will not animate. That is legal (a set may be "
            "deliberately still) but it is\n"
            "  almost always a copy-paste, so it is reported every run rather "
            "than left to be noticed.\n" % (len(same), ", ".join(same)))
    return same


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true",
                    help="regenerate in memory and diff against the tree")
    ap.add_argument("--self-check", nargs="*", metavar="FILE", default=None,
                    help="validate and RENDER the named .txt files (or all of "
                         "them); write nothing")
    ap.add_argument("--render", metavar="NAME",
                    help="print one set as text and exit")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    if args.self_check is not None:
        return self_check(args.self_check)

    sets = load_sets()

    # THE BYTE BUDGET, REFUSED AT THE SOURCE.
    #
    # tools/sprites/README.md section 5 has always said "the generator refuses
    # to emit at all when ... the art is over the byte budget". IT DID NOT: the
    # only budget check lived in self_check() with no arguments, so
    # `python3 tools/gen_sprites.py` happily wrote a header 1,152 B over the
    # ceiling and exited 0, leaving the emitted static_assert to fail the next
    # build with a message about a file nobody hand-edited. Found by mutation at
    # P9-C3 (eight spare effect sets appended); a documented refusal that does
    # not refuse is the same defect as a test that cannot fail.
    total_art = sum(set_bytes(s) for s in sets)
    if total_art > PB_DATA_BYTES_MAX:
        die("%d B of art is over the %d B budget by %d B (%d sets). This is a "
            "re-plan, not a build error: raise PB_DATA_BYTES_MAX in "
            "tools/gen_sprites.py AND SPRITE_DATA_BYTES_MAX in "
            "Pebblebol/src/data/sprites.h together, and say so in the commit"
            % (total_art, PB_DATA_BYTES_MAX, total_art - PB_DATA_BYTES_MAX,
               len(sets)))

    if args.render:
        for s in sets:
            if s["name"] == args.render.upper():
                print(describe(s))
                return 0
        die("no set named %s in the atlas (have: %s)"
            % (args.render.upper(), ", ".join(s["name"] for s in sets)))

    text = emit(sets)
    warn_identical_frames(sets)

    old = None
    if os.path.exists(OUT_H):
        with open(OUT_H, encoding="utf-8") as f:
            old = f.read()

    if args.check:
        if old != text:
            sys.stderr.write(
                "gen_sprites.py --check: %s differs from tools/sprites/*.txt\n"
                "run: python3 tools/gen_sprites.py\n" % rel(OUT_H))
            return 1
        if not args.quiet:
            print("gen_sprites.py --check: %s in sync, %d sets, %d B, "
                  "art hash 0x%04X"
                  % (rel(OUT_H), len(sets),
                     sum(set_bytes(s) for s in sets), art_hash(sets)))
        return 0

    if old != text:
        with open(OUT_H, "w", encoding="utf-8") as f:
            f.write(text)
        print("gen_sprites.py: wrote %s" % rel(OUT_H))
    else:
        print("gen_sprites.py: %s (no change)" % rel(OUT_H))
    print("gen_sprites.py: %d sets, %d species bodies, %d B of art, "
          "art hash 0x%04X"
          % (len(sets), sum(1 for s in sets if s["species"] is not None),
             sum(set_bytes(s) for s in sets), art_hash(sets)))
    return 0


def self_check(files):
    """WHAT AN ART AGENT RUNS BEFORE HANDING BACK A FILE.

    With no arguments it checks the whole atlas, manifest and species binding
    included. With file arguments it checks exactly those files and does NOT
    require them to be in atlas.txt yet - it says so instead, because "I drew it
    but have not registered it" is a normal state halfway through the work and
    an error message about it would train people to ignore errors.
    """
    if not files:
        sets = load_sets()          # dies on the first problem, by name
        for s in sets:
            print(describe(s))
            print("")
        same = warn_identical_frames(sets)
        total = sum(set_bytes(s) for s in sets)
        print("self-check: %d set(s) OK, %d B of art, %d over budget"
              % (len(sets), total, max(0, total - PB_DATA_BYTES_MAX)))
        rc = 0
        # A SET WHOSE TWO FRAMES ARE THE SAME BYTES IS AN ERROR HERE TOO (P9-C6).
        # The per-file path has failed on it since P9-C3 ("this body does not
        # animate", exit 1); this path only WARNED and returned 0, so the whole
        # -tree form of the same command was the weaker one - and it is the form
        # tools/check.sh runs. Two behaviours for one flag is how a check gets
        # trusted for something it does not do.
        if same:
            sys.stderr.write("gen_sprites.py: %d set(s) do not animate: %s\n"
                             % (len(same), ", ".join(same)))
            rc = 1
        if total > PB_DATA_BYTES_MAX:
            sys.stderr.write("gen_sprites.py: %d B of art over the %d B budget\n"
                             % (total, PB_DATA_BYTES_MAX))
            rc = 1
        return rc

    try:
        listed = set(read_manifest())
    except SystemExit:
        listed = set()
    pack = species_names()
    bad = 0
    for path in files:
        if not os.path.exists(path):
            sys.stderr.write("gen_sprites.py: %s does not exist\n" % path)
            bad += 1
            continue
        try:
            s = parse_set(path)
        except SrcError as e:
            sys.stderr.write("gen_sprites.py: %s\n" % e)
            bad += 1
            continue
        print(describe(s))
        if s["name"] not in listed:
            print("  NOTE: %s is not in tools/sprites/atlas.txt yet, so it "
                  "would not ship. Add it" % s["name"])

        # THE SLOT CHECK, ON THE FILES THIS RUN WAS HANDED (P9-C3).
        #
        # This was the hole an art agent found and reproduced: with file
        # arguments, self_check() never reached check_species_binding(), which
        # only runs through load_sets(). So copying kachi.txt and changing
        # `species: 37` to `species: 44` printed "self-check: 1 file(s) OK" and
        # exited 0 - the worst failure the format has (a body in the wrong slot
        # draws the wrong creature and every other check stays green), declared
        # fine by the exact command five art agents were told to run.
        #
        # The ORDER half of the binding still needs the whole atlas and is still
        # only checkable in load_sets(). The ID-TO-NAME half needs only
        # species.json, so it runs here, per file.
        if s["species"] is not None and pack is not None:
            sid = s["species"]
            if sid not in pack:
                sys.stderr.write(
                    "gen_sprites.py: %s is bound to species %d, which "
                    "tools/content/species.json does not contain (the pack "
                    "holds ids 1..%d)\n" % (rel(path), sid, max(pack)))
                bad += 1
                continue
            want = slug(pack[sid])
            if s["name"] != want:
                sys.stderr.write(
                    "gen_sprites.py: %s says `species: %d`, which is %s in the "
                    "pack, so this set must be named %s and the file %s.txt - "
                    "not %s. A body in another species' slot draws the wrong "
                    "creature and nothing else in the tree notices\n"
                    % (rel(path), sid, pack[sid], want, want.lower(), s["name"]))
                bad += 1
                continue

        # A FRAME 1 THAT IS A COPY OF FRAME 0 IS A FAILURE HERE, NOT A NOTE.
        # It used to print `WARNING: this body will not animate` and then
        # `self-check: 1 file(s) OK` with exit 0 - a check that names the defect
        # and then calls the file OK is a check people learn to skim.
        # tests/test_sprite_pipeline.cpp has always failed on it; the command
        # the README tells an art agent to run now agrees with the test.
        if frame_diff(s) == 0:
            sys.stderr.write(
                "gen_sprites.py: %s: frame 1 is byte-identical to frame 0 - "
                "this body does not animate\n" % rel(path))
            bad += 1
            continue
        print("")
    if bad:
        sys.stderr.write("gen_sprites.py: %d file(s) rejected\n" % bad)
        return 1
    print("self-check: %d file(s) OK" % len(files))
    return 0


if __name__ == "__main__":
    sys.exit(main())
