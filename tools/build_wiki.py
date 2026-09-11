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


def manual_url():
    p = os.path.join(ROOT, "Errata", "src", "core", "config.h")
    with open(p, encoding="utf-8") as f:
        m = re.search(r'#define\s+MANUAL_URL\s+"([^"]*)"', f.read())
    if not m:
        die("cannot find MANUAL_URL in config.h")
    return m.group(1)


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

    stat_max = max(max(s["hp"], s["atk"], s["dfn"], s["spd"]) for s in out_species)

    return {
        "statMax": stat_max,
        "species": out_species,
        "attacks": [{"name": a["name"], "type": a["type"], "cat": a["category"],
                     "power": a["power"], "acc": a["accuracy"], "effect": a["effect"]}
                    for a in attacks],
        "items": [{"name": i["name"], "klass": i["klass"], "value": i["value"],
                   "rarity": i.get("_rarity_name", "")} for i in items],
        "screens": screens,
        "manualUrl": manual_url(),
    }


# -----------------------------------------------------------------------------
#  3. THE PAGE
#
#  No framework and no CDN: it is served by GitHub Pages to a phone that has
#  just scanned a QR off a 0.96" panel, so it is one file that parses fast and
#  works offline once loaded. The 84 screenshots are LINKED, not inlined.
#
#  THE LOOK IS THE DEVICE'S. One ink on paper, the pixel grid left visible,
#  sprites drawn from the same 24x24 bitmaps the firmware ships and animated on
#  the device's own two-frame idle. It follows the reader's light/dark setting
#  because it is read on a phone, in a room, wherever the player happens to be.
# -----------------------------------------------------------------------------
TPL = r"""<!doctype html>
<html lang="es">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<title>Errata</title>
<meta name="description" content="Errata - un bicho virtual portátil. El manual, los sesenta bichos y todas las pantallas.">
<meta name="theme-color" content="#141414">
<style>
:root{
  --paper:#f4f2ec; --ink:#141414; --ink-soft:#5d5a52; --rule:#d6d2c6;
  --panel:#fffefa; --accent:#141414; --shadow:rgba(20,20,20,.10);
}
@media (prefers-color-scheme:dark){
  :root:not([data-theme="light"]){
    --paper:#121210; --ink:#ece9e1; --ink-soft:#928d81; --rule:#2e2c28;
    --panel:#1a1917; --accent:#ece9e1; --shadow:rgba(0,0,0,.5);
  }
}
:root[data-theme="dark"]{
  --paper:#121210; --ink:#ece9e1; --ink-soft:#928d81; --rule:#2e2c28;
  --panel:#1a1917; --accent:#ece9e1; --shadow:rgba(0,0,0,.5);
}
*{box-sizing:border-box}
body{
  margin:0;background:var(--paper);color:var(--ink);
  font:16px/1.55 ui-sans-serif,system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;
  -webkit-text-size-adjust:100%;
}
.wrap{max-width:960px;margin:0 auto;padding:0 18px}
a{color:inherit}
h1,h2,h3{line-height:1.15;margin:0}
.mono{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}

/* ---- header ---- */
header{
  position:sticky;top:0;z-index:30;background:var(--paper);
  border-bottom:1px solid var(--rule);
}
.bar{display:flex;align-items:center;gap:14px;height:54px}
.mark{
  font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;
  font-weight:700;letter-spacing:.22em;font-size:17px;
}
.bar nav{margin-left:auto;display:flex;gap:4px;align-items:center}
.bar nav a{
  text-decoration:none;font-size:13px;padding:6px 9px;border-radius:6px;
  color:var(--ink-soft);
}
.bar nav a:hover{color:var(--ink);background:var(--rule)}
.langs{display:flex;border:1px solid var(--rule);border-radius:6px;overflow:hidden;margin-left:6px;flex:none}
/* A 390 px phone is the DESIGN TARGET, not an edge case: this page is reached by
   scanning a QR off the device. The section links do not fit next to the
   wordmark and the language toggle there, and the toggle is the one a reader
   cannot get to any other way - the sections are one scroll away. */
@media (max-width:520px){ .bar nav a{display:none} }
.langs button{
  border:0;background:transparent;color:var(--ink-soft);font:600 11px/1 inherit;
  padding:7px 9px;cursor:pointer;letter-spacing:.06em;
}
.langs button[aria-pressed="true"]{background:var(--ink);color:var(--paper)}

/* ---- hero ---- */
/* LONGHAND, and it is not a style preference: this element carries BOTH .wrap
   and .hero, so a `padding:46px 0 34px` shorthand here zeroes the 18px
   horizontal padding .wrap sets and the headline bleeds off the left edge of
   a phone. Measured, not eyeballed. */
.hero{padding-top:46px;padding-bottom:34px;border-bottom:1px solid var(--rule)}
.hero h1{
  font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;
  font-size:clamp(38px,11vw,72px);letter-spacing:.06em;font-weight:700;
}
.hero .sub{margin-top:14px;font-size:clamp(16px,4vw,20px);max-width:44ch;color:var(--ink-soft)}
.hero .cta{display:flex;flex-wrap:wrap;gap:10px;margin-top:24px}
.btn{
  display:inline-block;text-decoration:none;font-size:14px;font-weight:600;
  padding:10px 16px;border:1px solid var(--ink);border-radius:8px;
}
.btn.solid{background:var(--ink);color:var(--paper)}

section{padding:44px 0;border-bottom:1px solid var(--rule);scroll-margin-top:62px}
.eyebrow{
  font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;
  font-size:11px;letter-spacing:.2em;color:var(--ink-soft);text-transform:uppercase;
}
section h2{font-size:clamp(23px,6vw,30px);margin-top:8px}
section .lede{margin-top:12px;max-width:62ch;color:var(--ink-soft)}

/* ---- filters ---- */
.filters{display:flex;flex-wrap:wrap;gap:8px;margin:22px 0 6px;align-items:center}
.filters input{
  flex:1 1 190px;min-width:150px;background:var(--panel);color:var(--ink);
  border:1px solid var(--rule);border-radius:8px;padding:9px 12px;font:inherit;font-size:14px;
}
.chip{
  border:1px solid var(--rule);background:var(--panel);color:var(--ink-soft);
  border-radius:999px;padding:7px 13px;font:600 12px/1 inherit;cursor:pointer;
  letter-spacing:.04em;
}
.chip[aria-pressed="true"]{background:var(--ink);color:var(--paper);border-color:var(--ink)}
.count{font-size:12px;color:var(--ink-soft);margin-left:auto}

/* ---- grid ---- */
.grid{
  display:grid;gap:10px;margin-top:18px;
  grid-template-columns:repeat(auto-fill,minmax(104px,1fr));
}
.card{
  background:var(--panel);border:1px solid var(--rule);border-radius:10px;
  padding:11px 8px 10px;text-align:center;cursor:pointer;
  display:flex;flex-direction:column;align-items:center;gap:5px;
  font:inherit;color:inherit;
}
.card:hover{border-color:var(--ink)}
.card canvas{
  width:72px;height:72px;image-rendering:pixelated;display:block;
}
.card .num{font-family:ui-monospace,monospace;font-size:10px;color:var(--ink-soft)}
.card .nm{font-size:13px;font-weight:600;line-height:1.2}
.t{
  font-family:ui-monospace,monospace;font-size:9px;letter-spacing:.1em;
  padding:2px 6px;border-radius:999px;border:1px solid var(--rule);color:var(--ink-soft);
}
.t.SIGNAL{border-style:solid}
.t.CORRUPT{border-style:dashed}
.t.SYSTEM{border-style:dotted}
.unseen .nm,.unseen .num{opacity:.5}

/* ---- detail sheet ---- */
.sheet{
  position:fixed;inset:0;z-index:50;display:none;
  background:rgba(0,0,0,.45);backdrop-filter:blur(2px);
}
.sheet[open],.sheet.on{display:block}
.sheet .inner{
  position:absolute;left:50%;top:50%;transform:translate(-50%,-50%);
  width:min(560px,calc(100vw - 28px));max-height:min(86vh,760px);overflow:auto;
  background:var(--panel);border:1px solid var(--rule);border-radius:14px;
  box-shadow:0 18px 60px var(--shadow);padding:22px;
}
@media (max-width:560px){
  .sheet .inner{
    left:0;top:auto;bottom:0;transform:none;width:100vw;max-height:90vh;
    border-radius:14px 14px 0 0;padding:18px 16px calc(18px + env(safe-area-inset-bottom));
  }
}
.sheet .x{
  position:sticky;top:0;float:right;border:0;background:transparent;color:var(--ink-soft);
  font-size:24px;line-height:1;cursor:pointer;padding:0 2px;
}
.dhead{display:flex;gap:16px;align-items:center}
.dhead canvas{width:96px;height:96px;image-rendering:pixelated;flex:none}
.dhead h3{font-size:26px}
.stats{display:grid;grid-template-columns:auto 1fr auto;gap:5px 10px;align-items:center;margin-top:18px}
.stats .k{font-family:ui-monospace,monospace;font-size:11px;color:var(--ink-soft);letter-spacing:.08em}
.stats .v{font-family:ui-monospace,monospace;font-size:12px}
.meter{height:7px;background:var(--rule);border-radius:99px;overflow:hidden}
.meter i{display:block;height:100%;background:var(--ink)}
.dsec{margin-top:20px}
.dsec h4{
  font-family:ui-monospace,monospace;font-size:10px;letter-spacing:.18em;
  color:var(--ink-soft);text-transform:uppercase;margin:0 0 8px;font-weight:600;
}
.moves{display:grid;gap:6px}
.mv{display:flex;gap:8px;align-items:center;font-size:13px;border:1px solid var(--rule);border-radius:8px;padding:7px 10px}
.mv .pw{margin-left:auto;font-family:ui-monospace,monospace;font-size:11px;color:var(--ink-soft)}
.chain{display:flex;align-items:center;gap:8px;flex-wrap:wrap}
.chain .step{display:flex;flex-direction:column;align-items:center;gap:3px;cursor:pointer}
.chain canvas{width:48px;height:48px;image-rendering:pixelated}
.chain .arrow{font-family:ui-monospace,monospace;font-size:11px;color:var(--ink-soft)}
.chain .here{font-weight:700}
.quote{
  border-left:2px solid var(--ink);padding:2px 0 2px 12px;font-size:14px;
  font-style:italic;color:var(--ink-soft);
}

/* ---- screens gallery ---- */
.shots{display:grid;gap:12px;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));margin-top:20px}
.shot{border:1px solid var(--rule);border-radius:8px;background:var(--panel);padding:8px}
.shot img{width:100%;display:block;image-rendering:pixelated;background:#000;border-radius:3px}
.shot figcaption{font-family:ui-monospace,monospace;font-size:10px;color:var(--ink-soft);margin-top:6px;word-break:break-all}

/* ---- tables ---- */
.tw{overflow-x:auto;margin-top:18px;-webkit-overflow-scrolling:touch}
table{border-collapse:collapse;width:100%;font-size:13px;min-width:430px}
th,td{text-align:left;padding:7px 10px;border-bottom:1px solid var(--rule);white-space:nowrap}
th{font-family:ui-monospace,monospace;font-size:10px;letter-spacing:.12em;color:var(--ink-soft);text-transform:uppercase}
td.n{font-family:ui-monospace,monospace}

.facts{display:grid;gap:0;margin-top:20px;border:1px solid var(--rule);border-radius:10px;overflow:hidden}
.facts div{display:flex;gap:12px;padding:10px 14px;border-bottom:1px solid var(--rule);font-size:14px}
.facts div:last-child{border-bottom:0}
.facts b{flex:0 0 40%;font-weight:600}
.facts span{color:var(--ink-soft)}

footer{padding:34px 0 48px;font-size:13px;color:var(--ink-soft)}
footer a{text-decoration:underline}
[data-en]{display:none}
html[lang="en"] [data-es]{display:none}
html[lang="en"] [data-en]{display:revert}
</style>
</head>
<body>

<header>
  <div class="wrap bar">
    <span class="mark">ERRATA</span>
    <nav>
      <a href="#bichos"><span data-es>Bichos</span><span data-en>Bugs</span></a>
      <a href="#pantallas"><span data-es>Pantallas</span><span data-en>Screens</span></a>
      <a href="#manual"><span data-es>Manual</span><span data-en>Manual</span></a>
      <div class="langs">
        <button id="es" aria-pressed="true">ES</button>
        <button id="en" aria-pressed="false">EN</button>
      </div>
    </nav>
  </div>
</header>

<div class="wrap hero">
  <h1>ERRATA</h1>
  <p class="sub">
    <span data-es>Un bicho virtual de bolsillo. El bicho es un bug informático, y crecer es estropearse.</span>
    <span data-en>A pocket virtual pet. The creature is a software bug, and growing up means falling apart.</span>
  </p>
  <!-- THE MANUAL GOES FIRST, and it is not a taste. This page is what the QR on
       the aparato's MANUAL screen opens, under a hint that reads "Manual y wiki
       en el movil." Somebody who has just scanned it came for the booklet, and
       core/config.h justifies the shorter URL by saying the site carries the
       manual as its first link. Reorder these two and that comment is a lie. -->
  <div class="cta">
    <a class="btn solid" href="uso.pdf"><span data-es>Leer el manual (PDF)</span><span data-en>Read the manual (PDF)</span></a>
    <a class="btn" href="#bichos"><span data-es>Ver los 60 bichos</span><span data-en>See all 60 bugs</span></a>
  </div>
</div>

<section id="bichos" class="wrap">
  <div class="eyebrow" data-es>El bestiario</div><div class="eyebrow" data-en>The bestiary</div>
  <h2><span data-es>Los sesenta</span><span data-en>The sixty</span></h2>
  <p class="lede">
    <span data-es>Veinte familias de tres etapas. Un bicho no se hace más grande al crecer: se hace
    más roto, dentro de la misma silueta. Los sprites de abajo son los que lleva el aparato,
    animados con sus dos fotogramas.</span>
    <span data-en>Twenty families of three stages each. A Bug does not get bigger as it grows: it gets
    more broken, inside the same silhouette. The sprites below are the ones the device ships,
    animated on its own two frames.</span>
  </p>

  <div class="filters">
    <input id="q" type="search" placeholder="Buscar..." aria-label="Buscar">
    <button class="chip" data-type="SIGNAL" aria-pressed="false">SIGNAL</button>
    <button class="chip" data-type="CORRUPT" aria-pressed="false">CORRUPT</button>
    <button class="chip" data-type="SYSTEM" aria-pressed="false">SYSTEM</button>
    <span class="count" id="count"></span>
  </div>
  <div class="grid" id="grid"></div>
</section>

<section id="pantallas" class="wrap">
  <div class="eyebrow" data-es>El aparato, por dentro</div><div class="eyebrow" data-en>Inside the device</div>
  <h2><span data-es>Todas las pantallas</span><span data-en>Every screen</span></h2>
  <p class="lede">
    <span data-es>Generadas del firmware, no dibujadas a mano: son 128x64 píxeles de un solo bit,
    exactamente lo que dibuja el aparato.</span>
    <span data-en>Generated from the firmware, not drawn by hand: 128x64 pixels at one bit, exactly
    what the device draws.</span>
  </p>
  <div class="shots" id="shots"></div>
</section>

<section id="ataques" class="wrap">
  <div class="eyebrow" data-es>Combate</div><div class="eyebrow" data-en>Battle</div>
  <h2><span data-es>Ataques</span><span data-en>Moves</span></h2>
  <p class="lede">
    <span data-es>SIGNAL gana a CORRUPT, CORRUPT gana a SYSTEM, SYSTEM gana a SIGNAL.</span>
    <span data-en>SIGNAL beats CORRUPT, CORRUPT beats SYSTEM, SYSTEM beats SIGNAL.</span>
  </p>
  <div class="tw"><table id="atk"></table></div>
</section>

<section id="objetos" class="wrap">
  <div class="eyebrow" data-es>La mochila</div><div class="eyebrow" data-en>The bag</div>
  <h2><span data-es>Objetos</span><span data-en>Items</span></h2>
  <div class="tw"><table id="itm"></table></div>
</section>

<section id="aparato" class="wrap">
  <div class="eyebrow" data-es>Hardware</div><div class="eyebrow" data-en>Hardware</div>
  <h2><span data-es>Qué es</span><span data-en>What it is</span></h2>
  <!-- THE STRONGEST CLAIM ON THIS PAGE, so it says exactly what the source does
       and not a word more. Checked against networking/net.cpp: the scan is
       PASSIVE (it listens for beacons and sends no probe request) and no
       association call exists anywhere in the tree, so "no se conecta" is
       literal. But the device DOES run a server - its own, an open AP plus an
       HTTP server, for the creator - and the first version of this paragraph
       said "no habla con ningun servidor" while that code shipped. A blanket
       denial that the product's own feature contradicts is worse than the
       longer sentence, so the creator is named here. -->
  <p class="lede">
    <span data-es>Explorar es mirar los nombres de las redes Wi-Fi que hay en el aire. El aparato
    solo escucha: no se conecta a ninguna, no pide cuenta y no manda nada a internet. Dos Erratas
    cercanas hablan directamente entre ellas para pelear, intercambiar y criar. El único servidor
    que hay en todo esto es el suyo: para diseñar un bicho desde el móvil, el aparato levanta su
    propia red Wi-Fi y sirve su propia página.</span>
    <span data-en>Exploring means looking at the names of the Wi-Fi networks in the air. The device
    only listens: it joins none of them, asks for no account and sends nothing to the internet. Two
    nearby Erratas talk straight to each other to battle, trade and breed. The only server anywhere
    in this is its own: to design a Bug from your phone, the device brings up its own Wi-Fi network
    and serves its own page.</span>
  </p>
  <div class="facts">
    <div><b data-es>Pantalla</b><b data-en>Display</b><span>OLED 0.96", 128 x 64, 1 bit</span></div>
    <div><b data-es>Controles</b><b data-en>Controls</b><span data-es>Dos botones, A y B</span><span data-en>Two buttons, A and B</span></div>
    <div><b data-es>Cerebro</b><b data-en>Brain</b><span>ESP32-C3</span></div>
    <div><b data-es>Pilas</b><b data-en>Batteries</b><span data-es>2 x AAA</span><span data-en>2 x AAA</span></div>
    <div><b data-es>Sonido</b><b data-en>Sound</b><span data-es>Piezo, una voz</span><span data-en>Piezo, one voice</span></div>
    <div><b data-es>Especies</b><b data-en>Species</b><span id="nsp"></span></div>
  </div>
</section>

<section id="manual" class="wrap">
  <div class="eyebrow" data-es>El cuadernillo</div><div class="eyebrow" data-en>The booklet</div>
  <h2><span data-es>Manual de usuario</span><span data-en>User manual</span></h2>
  <p class="lede">
    <span data-es>El que va en la caja: A6, grapado, una tinta, español e inglés en el mismo
    cuadernillo. Esta es la misma copia, al día. El QR de <span class="mono">Manual</span>, en los
    ajustes del aparato, trae a esta página.</span>
    <span data-en>The one that goes in the box: A6, saddle-stitched, one ink, Spanish and English in
    the same booklet. This is that copy, kept up to date. The
    <span class="mono">Manual</span> QR, in the device's settings, lands on this page.</span>
  </p>
  <div class="cta"><a class="btn solid" href="uso.pdf">uso.pdf</a></div>
</section>

<div class="wrap"><footer>
  <p>
    <span data-es>Esta página se genera del código: los sprites, las fichas y las capturas salen de
    la misma fuente que lleva el aparato, así que no puede quedarse vieja sin que falle la
    compilación.</span>
    <span data-en>This page is generated from the source: the sprites, the stats and the screenshots
    come from the same place the device does, so it cannot go stale without failing the build.</span>
  </p>
  <p><a href="https://github.com/pmirall/Pebblebol">github.com/pmirall/Pebblebol</a></p>
</footer></div>

<div class="sheet" id="sheet"><div class="inner" id="sheetInner"></div></div>

<script>
const DATA = __DATA__;
const T = {
  hp:["VIDA","HP"], atk:["ATAQUE","ATTACK"], dfn:["DEFENSA","DEFENCE"], spd:["VELOC.","SPEED"],
  moves:["Ataques que aprende","Moves it learns"], chain:["Su familia","Its family"],
  look:["Notas de dibujo","How it is drawn"], stage:["Etapa","Stage"],
  /* THE ART NOTES ARE ENGLISH AND STAY ENGLISH. They are quoted verbatim from
     the sprite files' own headers - the art bible, not prose written for this
     page - so translating them would make this page the source of a rule it
     does not own. The Spanish label says where they come from instead. */
  looknote:["Del fichero del sprite, en inglés","From the sprite's own file"],
  none:["Nada coincide con eso.","Nothing matches that."],
  showing:["%d de %d","%d of %d"], lvl:["Nv %d","Lv %d"], last:["última etapa","final stage"],
  name:["Nombre","Name"], type:["Tipo","Type"], power:["Potencia","Power"], acc:["Prec.","Acc."],
  kind:["Clase","Kind"], value:["Valor","Value"], rarity:["Rareza","Rarity"], effect:["Efecto","Effect"]
};
let L = 0;                                   // 0 = es, 1 = en
const t = k => T[k][L];
const STAGES = [["cría","joven","adulto"],["hatchling","young","adult"]];

/* --- sprites: 24 hex rows, MSB is the leftmost pixel ------------------------ */
function draw(cv, hex, on){
  const g = cv.getContext('2d');
  g.clearRect(0,0,24,24);
  g.fillStyle = on;
  for(let y=0;y<24;y++){
    const row = parseInt(hex.substr(y*6,6),16);
    for(let x=0;x<24;x++) if(row & (1<<(23-x))) g.fillRect(x,y,1,1);
  }
}
const ink = () => getComputedStyle(document.body).color;
const sprites = [];                          // {cv, frames}
function makeSprite(sp, cls){
  const cv = document.createElement('canvas');
  cv.width = 24; cv.height = 24; if(cls) cv.className = cls;
  const rec = {cv, f: sp.f};
  sprites.push(rec);
  draw(cv, sp.f[0], ink());
  return cv;
}
let phase = 0;
setInterval(() => {
  phase ^= 1;
  const c = ink();
  for(const s of sprites) draw(s.cv, s.f[phase], c);
}, 420);

/* --- grid ------------------------------------------------------------------- */
const byId = Object.fromEntries(DATA.species.map(s => [s.id, s]));
const grid = document.getElementById('grid');
let fType = null, fQ = '';

function render(){
  sprites.length = 0;
  grid.textContent = '';
  const q = fQ.trim().toLowerCase();
  const rows = DATA.species.filter(s =>
    (!fType || s.type === fType) &&
    (!q || s.name.toLowerCase().includes(q) || s.familyName.toLowerCase().includes(q)));
  for(const s of rows){
    const b = document.createElement('button');
    b.className = 'card';
    b.appendChild(makeSprite(s));
    const n = document.createElement('span'); n.className = 'num';
    n.textContent = String(s.id).padStart(2,'0'); b.appendChild(n);
    const nm = document.createElement('span'); nm.className = 'nm';
    nm.textContent = s.name; b.appendChild(nm);
    const ty = document.createElement('span'); ty.className = 't ' + s.type;
    ty.textContent = s.type; b.appendChild(ty);
    b.addEventListener('click', () => open(s.id));
    grid.appendChild(b);
  }
  if(!rows.length){
    const p = document.createElement('p'); p.className = 'lede';
    p.textContent = t('none'); grid.appendChild(p);
  }
  document.getElementById('count').textContent =
    t('showing').replace('%d', rows.length).replace('%d', DATA.species.length);
}

/* --- detail ----------------------------------------------------------------- */
const sheet = document.getElementById('sheet'), inner = document.getElementById('sheetInner');
function el(tag, cls, txt){
  const e = document.createElement(tag);
  if(cls) e.className = cls;
  if(txt !== undefined) e.textContent = txt;
  return e;
}
function open(id){
  const s = byId[id];
  sprites.length = 0;
  inner.textContent = '';

  const x = el('button','x','×');
  x.setAttribute('aria-label', L ? 'close' : 'cerrar');
  x.addEventListener('click', close);
  inner.appendChild(x);

  const head = el('div','dhead');
  head.appendChild(makeSprite(s));
  const ht = el('div');
  ht.appendChild(el('div','num mono', 'Nº ' + String(s.id).padStart(2,'0')));
  ht.appendChild(el('h3', null, s.name));
  const tags = el('div'); tags.style.marginTop = '6px';
  tags.appendChild(el('span','t ' + s.type, s.type));
  tags.appendChild(document.createTextNode(' '));
  tags.appendChild(el('span','t', s.familyName));
  tags.appendChild(document.createTextNode(' '));
  tags.appendChild(el('span','t', STAGES[L][s.stage] || ''));
  ht.appendChild(tags);
  head.appendChild(ht);
  inner.appendChild(head);

  if(s.flavor) inner.appendChild(el('p','quote dsec', s.flavor));

  const st = el('div','stats');
  const max = DATA.statMax;          // the roster's own ceiling, not a guess
  for(const [k, v] of [['hp',s.hp],['atk',s.atk],['dfn',s.dfn],['spd',s.spd]]){
    st.appendChild(el('span','k', t(k)));
    const m = el('div','meter'); const i = el('i');
    i.style.width = Math.min(100, v*100/max) + '%'; m.appendChild(i); st.appendChild(m);
    st.appendChild(el('span','v', String(v)));
  }
  inner.appendChild(st);

  const ms = el('div','dsec'); ms.appendChild(el('h4',null,t('moves')));
  const mw = el('div','moves');
  for(const m of s.moves){
    const r = el('div','mv');
    r.appendChild(el('span','t ' + m.type, m.type));
    r.appendChild(el('span',null,m.name));
    r.appendChild(el('span','pw', m.power ? (m.power + ' / ' + m.acc + '%') : (m.cat.toLowerCase())));
    mw.appendChild(r);
  }
  ms.appendChild(mw); inner.appendChild(ms);

  const fam = DATA.species.filter(o => o.family === s.family).sort((a,b) => a.stage - b.stage);
  if(fam.length > 1){
    const cs = el('div','dsec'); cs.appendChild(el('h4',null,t('chain')));
    const ch = el('div','chain');
    fam.forEach((o, i) => {
      if(i) {
        const ev = s.evo && fam[i-1].evo && fam[i-1].evo[0];
        ch.appendChild(el('span','arrow', ev ? ('→ ' + t('lvl').replace('%d', fam[i-1].evo[0].level)) : '→'));
      }
      const stp = el('div','step' + (o.id === s.id ? ' here' : ''));
      stp.appendChild(makeSprite(o));
      stp.appendChild(el('span','num', o.name));
      if(o.id !== s.id) stp.addEventListener('click', () => open(o.id));
      ch.appendChild(stp);
    });
    cs.appendChild(ch); inner.appendChild(cs);
  }

  if(s.silhouette){
    const ls = el('div','dsec');
    const h = el('h4',null,t('look'));
    const note = el('span',null,'  \u00b7  ' + t('looknote'));
    note.style.opacity = '.7'; note.style.textTransform = 'none';
    h.appendChild(note);
    ls.appendChild(h);
    ls.appendChild(el('p', null, s.silhouette));
    if(s.frame2) ls.appendChild(el('p','lede', s.frame2));
    inner.appendChild(ls);
  }

  sheet.classList.add('on');
  document.body.style.overflow = 'hidden';
  inner.scrollTop = 0;
  location.hash = 'b' + s.id;
}
function close(){
  sheet.classList.remove('on');
  document.body.style.overflow = '';
  if(location.hash.startsWith('#b')) history.replaceState(null,'','#bichos');
  render();
}
sheet.addEventListener('click', e => { if(e.target === sheet) close(); });
document.addEventListener('keydown', e => { if(e.key === 'Escape' && sheet.classList.contains('on')) close(); });

/* --- tables, screens -------------------------------------------------------- */
function table(id, cols, rows){
  const tb = document.getElementById(id);
  tb.textContent = '';
  const tr = document.createElement('tr');
  for(const c of cols) tr.appendChild(el('th',null,c));
  tb.appendChild(tr);
  for(const r of rows){
    const x = document.createElement('tr');
    r.forEach((v, i) => x.appendChild(el('td', i ? 'n' : '', String(v))));
    tb.appendChild(x);
  }
}
function tables(){
  table('atk', [t('name'), t('type'), t('power'), t('acc'), t('effect')],
    DATA.attacks.map(a => [a.name, a.type, a.power || '—', a.acc + '%',
                           a.effect === 'NONE' ? '—' : a.effect.toLowerCase()]));
  table('itm', [t('name'), t('kind'), t('value'), t('rarity')],
    DATA.items.map(i => [i.name, i.klass.toLowerCase().replace(/_/g,' '), i.value || '—',
                         i.rarity.toLowerCase()]));
}
const shots = document.getElementById('shots');
for(const s of DATA.screens){
  const f = document.createElement('figure'); f.className = 'shot';
  const im = document.createElement('img');
  im.src = 'manual/assets/screens/' + s + '.svg';
  im.alt = s; im.loading = 'lazy'; im.width = 128; im.height = 64;
  f.appendChild(im); f.appendChild(el('figcaption', null, s.replace(/_/g,' ')));
  shots.appendChild(f);
}
document.getElementById('nsp').textContent = DATA.species.length;

/* --- language --------------------------------------------------------------- */
function setLang(n){
  L = n;
  document.documentElement.lang = n ? 'en' : 'es';
  document.getElementById('es').setAttribute('aria-pressed', String(!n));
  document.getElementById('en').setAttribute('aria-pressed', String(!!n));
  document.getElementById('q').placeholder = n ? 'Search…' : 'Buscar…';
  try { localStorage.setItem('errata.lang', n ? 'en' : 'es'); } catch(e){}
  tables();
  if(sheet.classList.contains('on')){
    const id = parseInt(location.hash.slice(2), 10);
    if(id) open(id); else render();
  } else render();
}
document.getElementById('es').addEventListener('click', () => setLang(0));
document.getElementById('en').addEventListener('click', () => setLang(1));

document.getElementById('q').addEventListener('input', e => { fQ = e.target.value; render(); });
for(const c of document.querySelectorAll('.chip[data-type]')){
  c.addEventListener('click', () => {
    const was = c.getAttribute('aria-pressed') === 'true';
    for(const o of document.querySelectorAll('.chip[data-type]')) o.setAttribute('aria-pressed','false');
    c.setAttribute('aria-pressed', String(!was));
    fType = was ? null : c.dataset.type;
    render();
  });
}

let saved = null;
try { saved = localStorage.getItem('errata.lang'); } catch(e){}
setLang(saved === 'en' ? 1 : (saved === 'es' ? 0 : (navigator.language || '').startsWith('en') ? 1 : 0));

if(location.hash.startsWith('#b')){
  const id = parseInt(location.hash.slice(2), 10);
  if(byId[id]) open(id);
}
</script>
</body>
</html>
"""


def build_html():
    data = build_data()
    return TPL.replace("__DATA__", json.dumps(data, ensure_ascii=False, separators=(",", ":")))


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
    print("build_wiki: wrote docs/index.html (%d species, %d moves, %d items, %d screens, %.0f KB)"
          % (len(d["species"]), len(d["attacks"]), len(d["items"]), len(d["screens"]),
             len(html.encode("utf-8")) / 1024.0))


if __name__ == "__main__":
    main()
