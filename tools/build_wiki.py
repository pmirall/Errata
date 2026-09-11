#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
build_wiki.py - THE PUBLIC WIKI, generated from the same data the device holds.

    tools/content/*.json      \
    tools/sprites/*.txt        >--->  docs/index.html
    Errata/src/core/config.h  /

Usage
    python3 tools/build_wiki.py            # write docs/index.html
    python3 tools/build_wiki.py --check    # regenerate in memory and DIFF
                                           # against the tree; exit 1 on drift

WHY IT IS GENERATED AND NOT WRITTEN.

A hand-written bestiary is a second copy of the roster, and this repository has
spent ten phases learning what a second copy does: it agrees with the first one
on the day it is written and never again. Sixty creatures, thirty-four moves,
forty evolutions and sixty pairs of 24x24 frames are far past what anybody will
re-check by eye after a balance pass.

So the wiki is a BUILD ARTEFACT. Rename a species, retune a stat, redraw a
sprite, add a move - the page changes with it, or `--check` fails the gate. It
is the same contract tools/gen_content.py and tools/gen_index_html.py already
have with their generated headers, and the same one tools/pbm2svg.py has with
the manual's screenshots.

WHAT THE GATE CANNOT SEE is the prose: the section that explains what a Bug IS
cannot be generated from species.json, and nothing here notices when it stops
being true. That half is CLAUDE.md's guide rule, which sends a player-visible
change through an agent that reads the page. Mechanical half here, reading half
there - the same split the printed manual uses.

THE SCREENSHOTS ARE NOT INLINED. docs/manual/assets/screens/*.svg is already
generated from the goldens by tools/pbm2svg.py and already served by Pages, so
this page LINKS them. Inlining would have been a third copy of the same pixels
and would have put 1.5 MB into one file a phone has to parse.
"""

import glob
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "docs", "index.html")

TYPES = ["SIGNAL", "CORRUPT", "SYSTEM"]
STAGES_ES = ["cría", "joven", "adulto"]
STAGES_EN = ["hatchling", "young", "adult"]


def die(msg):
    print("build_wiki: %s" % msg, file=sys.stderr)
    sys.exit(1)


# -----------------------------------------------------------------------------
#  1. LOAD, and refuse anything that does not add up.
#
#  Every count below is checked against a SECOND source rather than trusted:
#  the roster against the C table's own length, the sprites against the roster,
#  the moves against the attack table. A generator that silently drops a species
#  produces a page that looks finished and is not.
# -----------------------------------------------------------------------------
def load_json(rel):
    p = os.path.join(ROOT, "tools", "content", rel)
    if not os.path.exists(p):
        die("missing %s" % rel)
    with open(p, encoding="utf-8") as f:
        return json.load(f)


def load_sprites(species_ids):
    """species id -> [frame0, frame1], each 24 rows of 24 chars."""
    out = {}
    for path in sorted(glob.glob(os.path.join(ROOT, "tools", "sprites", "*.txt"))):
        with open(path, encoding="utf-8") as f:
            text = f.read()
        m = re.search(r"^species:\s*(\d+)", text, re.M)
        if not m:
            continue                      # egg, sleep, sick, atlas: not a species
        sid = int(m.group(1))
        size = re.search(r"^size:\s*(\d+)x(\d+)", text, re.M)
        if not size or size.group(1) != "24" or size.group(2) != "24":
            die("%s is not 24x24" % os.path.basename(path))
        frames = []
        for block in re.split(r"^--- frame \d+\s*$", text, flags=re.M)[1:]:
            rows = [ln for ln in block.splitlines() if re.fullmatch(r"[#.]{24}", ln)]
            if len(rows) != 24:
                die("%s frame has %d rows, want 24" % (os.path.basename(path), len(rows)))
            frames.append(rows)
        if len(frames) != 2:
            die("%s has %d frames, want 2" % (os.path.basename(path), len(frames)))
        out[sid] = frames
    missing = [i for i in species_ids if i not in out]
    if missing:
        die("no sprite for species %s" % missing)
    return out


def c_table_count():
    """How many rows species_table.h actually ships. The roster's second source."""
    p = os.path.join(ROOT, "Errata", "src", "data", "species_table.h")
    with open(p, encoding="utf-8") as f:
        text = f.read()
    m = re.search(r"SPECIES_TABLE\[\]\s*=\s*\{(.*?)\n\};", text, re.S)
    if not m:
        die("cannot find SPECIES_TABLE[] in species_table.h")
    return len(re.findall(r"^\s*\{\s*\d+\s*,", m.group(1), re.M))


def wiki_url():
    p = os.path.join(ROOT, "Errata", "src", "core", "config.h")
    with open(p, encoding="utf-8") as f:
        m = re.search(r'#define\s+WIKI_URL\s+"([^"]*)"', f.read())
    if not m:
        die("cannot find WIKI_URL in config.h")
    return m.group(1)


def manual_url():
    p = os.path.join(ROOT, "Errata", "src", "core", "config.h")
    with open(p, encoding="utf-8") as f:
        m = re.search(r'#define\s+MANUAL_URL\s+"([^"]*)"', f.read())
    if not m:
        die("cannot find MANUAL_URL in config.h")
    return m.group(1)


# WHICH SCREEN BELONGS TO WHICH PART OF THE DEVICE. Eighty-odd thumbnails with
# their filenames under them is a dump, not a gallery: a reader who has never
# held the device cannot tell "mg_ping_lit" from "mg_buffer_run". The grouping
# is by filename prefix because that IS how the goldens are named, and an
# unknown prefix is a HARD FAILURE rather than an "other" bucket - a new screen
# that lands in a junk drawer is a new screen nobody wrote a caption for.
SCREEN_AREAS = [
    ("start",    ("Encender y empezar", "Switching on"),      ["boot", "intro", "setup", "load", "time"]),
    ("home",     ("El día a día", "Day to day"),              ["home", "menu", "alert", "status", "help"]),
    ("care",     ("Cuidar y jugar", "Care and play"),         ["care", "play", "mg"]),
    ("explore",  ("Explorar y capturar", "Exploring"),        ["network", "encounter", "capture"]),
    ("bugs",     ("La Caja y la wiki", "The Box and the wiki"), ["box", "dex", "evolution"]),
    ("battle",   ("Combate", "Battle"),                       ["battle"]),
    ("link",     ("Enlace y creador", "Link and creator"),    ["link", "creator"]),
    ("system",   ("Ajustes y avisos", "Settings and warnings"), ["settings", "manual", "wiki", "confirm", "error", "soon", "diag"]),
]


def group_screens(screens):
    where = {}
    for key, label, prefixes in SCREEN_AREAS:
        for pfx in prefixes:
            if pfx in where:
                die("screen prefix %r is claimed by two areas" % pfx)
            where[pfx] = key
    buckets = {key: [] for key, _, _ in SCREEN_AREAS}
    for name in screens:
        pfx = name.split("_")[0]
        if pfx not in where:
            die("screen %r has prefix %r, which no area in SCREEN_AREAS claims - "
                "add it to an area (and give it a caption) rather than letting a "
                "screen appear in the gallery with nothing to say about it" % (name, pfx))
        buckets[where[pfx]].append(name)
    out = []
    for key, label, _ in SCREEN_AREAS:
        if buckets[key]:
            out.append({"key": key, "label": list(label), "screens": buckets[key]})
    total = sum(len(a["screens"]) for a in out)
    if total != len(screens):
        die("grouped %d screens out of %d" % (total, len(screens)))
    return out


def hero_family(species):
    """The family the hero strip shows: the STARTER's, because it is the first
    creature every player meets and its three stages are the clearest reading of
    'growth is damage' in the roster. Resolved from the data rather than
    hardcoded, so a roster reorder moves the hero instead of breaking it."""
    first = min(species, key=lambda s: s["id"])
    return first["family"]


def pack(frame):
    """24 rows of '#.' -> 24 hex values, one per row, MSB = leftmost pixel."""
    return "".join("%06x" % int(row.replace(".", "0").replace("#", "1"), 2) for row in frame)


# -----------------------------------------------------------------------------
#  2. BUILD THE DATA THE PAGE SHIPS WITH
# -----------------------------------------------------------------------------
def build_data():
    species = load_json("species.json")
    attacks = load_json("attacks.json")
    items = load_json("items.json")
    evos = load_json("evolution.json")

    n_c = c_table_count()
    if n_c != len(species):
        die("species.json has %d rows, species_table.h ships %d" % (len(species), n_c))

    sprites = load_sprites([s["id"] for s in species])
    amap = {a["id"]: a for a in attacks}

    evo_from = {}
    for e in evos:
        evo_from.setdefault(e["species"], []).append(e)

    out_species = []
    for s in species:
        for mid in s["moves"]:
            if mid not in amap:
                die("species %d references move %d, which attacks.json does not have"
                    % (s["id"], mid))
        out_species.append({
            "id": s["id"],
            "name": s["name"],
            "type": s["type"],
            "family": s["family"],
            "familyName": s.get("_family_name", ""),
            "stage": s["stage"],
            "rarity": s["rarity"],
            "rarityName": s.get("_rarity_name", ""),
            "hp": s["base_hp"], "atk": s["base_atk"],
            "dfn": s["base_def"], "spd": s["base_spd"],
            "flavor": s["flavor_es"],
            "silhouette": s.get("_silhouette", ""),
            "frame2": s.get("_frame2", ""),
            "root": s.get("_root", ""),
            "moves": [{"name": amap[m]["name"], "type": amap[m]["type"],
                       "power": amap[m]["power"], "acc": amap[m]["accuracy"],
                       "cat": amap[m]["category"]} for m in s["moves"]],
            "evo": [{"to": e["target"], "level": e["level"], "cond": e["cond"]}
                    for e in evo_from.get(s["id"], [])],
            "f": [pack(sprites[s["id"]][0]), pack(sprites[s["id"]][1])],
        })

    screens = sorted(os.path.basename(p)[:-4] for p in
                     glob.glob(os.path.join(ROOT, "docs", "manual", "assets", "screens", "*.svg")))
    if not screens:
        die("no generated screens found - run make -C tests capture && tools/pbm2svg.py --all")
    areas = group_screens(screens)

    stat_max = max(max(s["hp"], s["atk"], s["dfn"], s["spd"]) for s in out_species)

    return {
        "statMax": stat_max,
        "species": out_species,
        "attacks": [{"name": a["name"], "type": a["type"], "cat": a["category"],
                     "power": a["power"], "acc": a["accuracy"], "effect": a["effect"]}
                    for a in attacks],
        "items": [{"name": i["name"], "klass": i["klass"], "value": i["value"],
                   "rarity": i.get("_rarity_name", "")} for i in items],
        "screenAreas": areas,
        "heroFamily": hero_family(out_species),
        "manualUrl": manual_url(),
    }


# -----------------------------------------------------------------------------
#  3. THE PAGE
#
#  No framework and no CDN: it is served by GitHub Pages to a phone that has
#  just scanned a QR off a 0.96" panel, so it is one file that parses fast and
#  works offline once loaded. The screenshots are LINKED, not inlined, and the
#  page counts them itself rather than trusting a number typed into the prose.
#
#  THE LOOK IS THE DEVICE'S. One ink on paper, the pixel grid left visible,
#  sprites drawn from the same 24x24 bitmaps the firmware ships and animated on
#  the device's own two-frame idle. It follows the reader's light/dark setting
#  because it is read on a phone, in a room, wherever the player happens to be.
# -----------------------------------------------------------------------------
TPL_PATH = os.path.join(ROOT, "tools", "wiki", "page.html")


# -----------------------------------------------------------------------------
#  3. THE PAGE
#
#  The template is a FILE and not a string in this module, for the reason
#  tools/gen_index_html.py already gives about its own 40 KB header: an HTML
#  document inlined in a .py is not a diff anybody reads. tools/wiki/page.html
#  is the reviewable half; everything here is the data half, and the only join
#  between them is the __DATA__ placeholder.
#
#  THE PROSE IN THAT FILE IS NOT GENERATED AND NOTHING CHECKS IT. The cards,
#  the tables and the gallery come from the JSON below and go stale loudly;
#  the paragraphs that explain what a Bug IS can quietly stop being true. That
#  half is CLAUDE.md's guide rule.
# -----------------------------------------------------------------------------


def manual_href():
    """The page's link to the booklet, DERIVED FROM THE FIRMWARE'S OWN CONSTANT.

    The device's MANUAL row encodes MANUAL_URL and this page links the same
    file, so the two must name the same thing - and until now they only did
    because two people typed "uso.pdf" in two files. The path is cut out of
    MANUAL_URL and substituted into the template, so renaming the PDF moves the
    QR and this link together or fails here."""
    url = manual_url()
    if ".github.io/" not in url:
        die("MANUAL_URL (%s) is not a github.io address, so the page cannot "
            "derive its own link to the booklet from it" % url)
    rest = url.split(".github.io/", 1)[1]
    if "/" not in rest:
        die("MANUAL_URL (%s) has no path after the repo name - it points at the "
            "site root, which is the wiki and not the booklet" % url)
    path = rest.split("/", 1)[1]
    if not path.endswith(".pdf"):
        die("MANUAL_URL (%s) does not end in .pdf - the page's 'Manual (PDF)' "
            "link would be a lie" % url)
    if not os.path.exists(os.path.join(ROOT, "docs", path)):
        die("MANUAL_URL points at %s and docs/%s does not exist" % (url, path))
    return path


def build_html():
    data = build_data()
    if not os.path.exists(TPL_PATH):
        die("missing tools/wiki/page.html")
    with open(TPL_PATH, encoding="utf-8") as f:
        tpl = f.read()
    for ph in ("__DATA__", "__MANUAL_HREF__"):
        if ph not in tpl:
            die("tools/wiki/page.html has no %s placeholder" % ph)
    tpl = tpl.replace("__MANUAL_HREF__", manual_href())
    return tpl.replace("__DATA__", json.dumps(data, ensure_ascii=False, separators=(",", ":")))


def main():
    html = build_html()
    if "--check" in sys.argv:
        if not os.path.exists(OUT):
            die("docs/index.html does not exist - run tools/build_wiki.py")
        with open(OUT, encoding="utf-8") as f:
            cur = f.read()
        if cur != html:
            die("docs/index.html is STALE - regenerate it with tools/build_wiki.py")
        print("build_wiki: docs/index.html matches its sources")
        return
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(html)
    d = build_data()
    n_scr = sum(len(a["screens"]) for a in d["screenAreas"])
    print("build_wiki: wrote docs/index.html (%d species, %d moves, %d items, "
          "%d screens in %d areas, %.0f KB)"
          % (len(d["species"]), len(d["attacks"]), len(d["items"]), n_scr,
             len(d["screenAreas"]), len(html.encode("utf-8")) / 1024.0))


if __name__ == "__main__":
    main()
