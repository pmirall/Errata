#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_content.py - THE CONTENT PIPELINE (plan P4-C1, section 1.5.2).

    tools/content/*.json   ->   Pebblebol/src/data/*_table.h
                                Pebblebol/src/data/creator_schema.h
                                Pebblebol/src/data/content_version.h
                                the generated block inside src/core/strings_es.h

Usage
    python3 tools/gen_content.py            # write the headers
    python3 tools/gen_content.py --check    # regenerate in memory and DIFF
                                            # against the tree; exit 1 on drift
    python3 tools/gen_content.py --families N   # override the roster size

DETERMINISM IS A CONTRACT. Two runs over the same JSON produce byte-identical
headers: every table is walked in file order, every dict is emitted with sorted
keys, and nothing here reads the clock, the environment or a random source.
`--check` is what proves it in the gate.

------------------------------------------------------------------------------
WHY THE ROSTER IS 36 SPECIES (12 families x 3) AND NOT 12 OR 60
------------------------------------------------------------------------------
The content pack in tools/content/ holds the FULL 60-species roster and its own
gate (`cd tools/content && python3 verify.py --fast`) validates all 60. What
gets EMITTED is a prefix of it, because SpeciesDef ids are contiguous
(`id == index + 1`, a static_assert) - so the only legal subsets are prefixes,
and a prefix is a whole number of families.

Three numbers were measured before choosing:

  * FLASH. It does not bind. The baseline firmware is 1,893,072 B against a
    GATE_FLASH_MAX of 2,400,000, i.e. 506,928 B of headroom, and the whole
    60-species content set is 5,397 B of tables plus strings (the pack's own
    14,037 B figure includes an 8,640 B sprite atlas that does not exist yet).
    Flash permits all 60 with three orders of magnitude to spare.

  * THE SPRITE ATLAS. It binds, hard. The pack's invariant is
    sprite_id == id - 1, and SPR_BABY_BLOB + sprite_id < SPRITE_SET_COUNT is
    2 + (N-1) < 38, i.e. N <= 36. Shipping 60 would mean deleting a live guard
    against real art. 36 is the largest roster today's atlas can address.

    P4-C4a NARROWED THIS PARAGRAPH TWICE. (1) It used to say sprite_id "is
    resolved as" that sum. It is not, and never was: nothing evaluated it to
    draw anything, and against the 38-set atlas species 25..36 would have
    landed on GHOST, TOMB and the ten pose sets. It is a FORWARD bound on the
    P10 atlas (2 eggs + one 24x24 body per species) and it still caps the
    roster at 36, which is all this paragraph needs it for. What the firmware
    draws is ui/pet_art.h's key folded into the authored pools, and
    game/species.cpp asserts THAT separately. (2) The guard is in
    game/species.cpp as a static_assert and in tests/test_content.cpp at
    runtime; tests/test_evolution.cpp carried a byte-identical copy of the
    runtime one until P4-C4a replaced it with the question that file owns.

  * WHAT 12 WOULD COST. Families 1..5 of the pack are all SIGNAL, so a
    12-species roster carries exactly one type: no shipped creature could ever
    hold a CORRUPT or SYSTEM attack (a learnset is "own type or NEUTRAL"), the
    type triangle of spec section 12 would evaluate to 0 for every pair of
    shipped species, and 20 of the 34 attacks would sit on no learnset at all -
    reintroducing, at ten times the scale, the exact orphan-attack defect that
    P4-C1 obligation 2 exists to close. 36 species spans all three types
    (SIGNAL families 1-5, CORRUPT 6-10, SYSTEM 11-12), leaves 2 attacks for the
    families still to come (13 Infeccion on species 46, 22 Firewall on 60), and
    gives the eight legacy v1 families eight DISTINCT base-stage destinations,
    which 4 families cannot do.

Raise ROSTER_FAMILIES to 20 when the P10 art pass grows SPRITE_SETS.
"""

import argparse
import json
import os
import sys

# --- the roster size, and the one place to change it -------------------------
ROSTER_FAMILIES = 12          # 12 x 3 = 36 species; see the banner above

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CONTENT = os.path.join(HERE, "content")
DATA = os.path.join(ROOT, "Pebblebol", "src", "data")
STRINGS_H = os.path.join(ROOT, "Pebblebol", "src", "core", "strings_es.h")

# The two generated regions of core/strings_es.h. The markers are DISTINCT
# strings, not one string used twice: an enum marker that is also a substring of
# the table marker makes the second splice re-cut the first block.
GEN_ENUM_BEGIN = "// >>> GEN StrId BLOCK - tools/gen_content.py - DO NOT EDIT BY HAND >>>"
GEN_ENUM_END = "// <<< GEN StrId BLOCK - tools/gen_content.py - DO NOT EDIT BY HAND <<<"
GEN_TAB_BEGIN = "/* >>> GEN ES[] BLOCK - tools/gen_content.py - DO NOT EDIT BY HAND >>> */"
GEN_TAB_END = "/* <<< GEN ES[] BLOCK - tools/gen_content.py - DO NOT EDIT BY HAND <<< */"

# =============================================================================
#  INPUT
# =============================================================================
def load(name):
    with open(os.path.join(CONTENT, name), encoding="utf-8") as f:
        return json.load(f)


class Content(object):
    def __init__(self, families):
        self.species_all = load("species.json")
        self.attacks = load("attacks.json")
        self.items = load("items.json")
        self.evo_all = load("evolution.json")
        self.encounters = load("encounters.json")
        self.networks = load("networks.json")
        self.balance = load("balance.json")
        self.families = families

        self.species = [s for s in self.species_all if s["family"] <= families]
        # EVOLUTION_RULES order is contractual: SpeciesDef.evo_rule indexes it.
        # The pack's invariant is evo_rule == (family-1)*2 + stage, so a family
        # prefix of the roster is exactly a prefix of the rule table.
        keep = set(s["id"] for s in self.species)
        self.evo = [r for r in self.evo_all if r["species"] in keep]

        self._check_prefix()
        self._check_field_widths()
        self._check_strings()

    def _check_prefix(self):
        ids = [s["id"] for s in self.species]
        if ids != list(range(1, len(ids) + 1)):
            die("the roster is not a contiguous prefix: %r" % ids[:8])
        for i, s in enumerate(self.species):
            want = (s["family"] - 1) * 2 + s["stage"]
            got = s["evo_rule"]
            if got != 255 and got != want:
                die("species %d: evo_rule %d, expected %d" % (s["id"], got, want))
        for i, r in enumerate(self.evo):
            src = self.species[r["species"] - 1]
            if src["evo_rule"] != i:
                die("evolution rule %d is not the one species %d points at"
                    % (i, r["species"]))

    # -------------------------------------------------------------------------
    #  EVERY EMITTED INTEGER IS CHECKED AGAINST THE FIELD IT LANDS IN
    #
    #  Without this an out-of-range JSON value was written verbatim into the
    #  table and the only thing that caught it was the C++ compiler: `target`
    #  999 in a uint8_t field surfaced as -Wnarrowing plus three "non-constant
    #  condition for static assertion" errors and named neither the field nor
    #  the JSON row. Every OTHER kind of bad input this generator takes lands on
    #  a named diagnostic, so this one does too. The bounds are the STRUCT's,
    #  read off data/*_table.h, not the content's design ranges - verify.py owns
    #  those, and a guard that duplicated them would go stale against the pack.
    # -------------------------------------------------------------------------
    def _check_field_widths(self):
        U8, I8, U16 = (0, 255), (-128, 127), (0, 65535)
        RAR = (0, 3)                       # SPECIES_RARITY_COMMON..SPECIAL
        specs = [
            ("species.json", self.species_all, "id", [
                ("id", U8), ("family", U8), ("stage", (0, 2)),
                ("base_hp", U8), ("base_atk", U8), ("base_def", U8),
                ("base_spd", U8), ("evo_rule", U8), ("rarity", RAR),
                ("spawn_weight", U8), ("compat_group", U8),
                ("category_mask", U8), ("sprite_id", U8)]),
            ("attacks.json", self.attacks, "id", [
                ("id", U8), ("power", U8), ("accuracy", U8), ("priority", I8),
                ("effect_value", U8), ("effect_duration", U8), ("cooldown", U8),
                ("anim_id", U8), ("budget_cost", U8)]),
            ("items.json", self.items, "id", [
                ("id", U8), ("value", U8), ("rarity", RAR)]),
            ("evolution.json", self.evo_all, "species", [
                ("species", U8), ("target", U8), ("level", U8),
                ("cond_value", U16)]),
            ("encounters.json", self.encounters, "weight", [
                ("weight", U8), ("rarity_min", RAR), ("rarity_max", RAR)]),
        ]
        # networks.json is not a list of rows, so it gets its own shape check
        # rather than a column walk. Every rule below is one the GENERATED
        # header cannot state for itself: a threshold outside int8_t, a token
        # that no scan can ever produce, or a token claimed by two classes -
        # which would make NET_TOKEN_TABLE's class column depend on emit order.
        self._check_networks()

        for fname, rows, idkey, fields in specs:
            for n, row in enumerate(rows):
                for key, (lo, hi) in fields:
                    v = row[key]
                    if not isinstance(v, int) or isinstance(v, bool):
                        die("%s row %d (%s %r): %s is %r, not an integer"
                            % (fname, n, idkey, row.get(idkey), key, v))
                    if v < lo or v > hi:
                        die("%s row %d (%s %r): %s = %d is outside the %d..%d "
                            "the generated field holds"
                            % (fname, n, idkey, row.get(idkey), key, v, lo, hi))
                for m in row.get("moves", []):
                    if not isinstance(m, int) or m < 0 or m > 255:
                        die("%s row %d (%s %r): move id %r is outside 0..255"
                            % (fname, n, idkey, row.get(idkey), m))
        for cat, drops in self.balance["ITEM_DROPS"].items():
            for iid, w in drops.items():
                if int(iid) < 0 or int(iid) > 255 or w < 0 or w > 255:
                    die("balance.json ITEM_DROPS[%s]: item %s weight %r is "
                        "outside the 0..255 ItemDropRow holds" % (cat, iid, w))

    def _check_networks(self):
        n = self.networks
        near, mid = n["RSSI_NEAR"], n["RSSI_MID"]
        for k in ("RSSI_NEAR", "RSSI_MID"):
            v = n[k]
            if not isinstance(v, int) or isinstance(v, bool) or v < -127 or v > 0:
                die("networks.json: %s = %r is outside the -127..0 an int8_t "
                    "RSSI holds" % (k, v))
        if not (mid < near):
            die("networks.json: RSSI_MID %d must be strictly weaker than "
                "RSSI_NEAR %d, or the HOME band is empty" % (mid, near))
        minlen = n["TOKEN_MIN_LEN"]
        if not isinstance(minlen, int) or minlen < 2 or minlen > 16:
            die("networks.json: TOKEN_MIN_LEN %r is outside 2..16" % (minlen,))
        # The scanner buffers one token at a time on the stack, in a loop that
        # runs once per access point. 32 is the ceiling this generator will
        # emit for that buffer.
        for cname in sorted(n["TOKENS"].keys()):
            for t in n["TOKENS"][cname]:
                if isinstance(t, str) and len(t) > 32:
                    die("networks.json: token %r in %s is longer than the 32 "
                        "characters net_classify.cpp will buffer" % (t, cname))
        bits = n["NET_TOKEN_CLASS_BITS"]
        seen_bit = {}
        for cname, b in sorted(bits.items()):
            if not isinstance(b, int) or b <= 0 or b > 255 or (b & (b - 1)) != 0:
                die("networks.json: token class %s = %r is not a single bit in "
                    "0..255" % (cname, b))
            if b in seen_bit:
                die("networks.json: token classes %s and %s share bit %d"
                    % (seen_bit[b], cname, b))
            seen_bit[b] = cname
        owner = {}
        for cname in sorted(n["TOKENS"].keys()):
            if cname not in bits:
                die("networks.json: TOKENS has a %s list but "
                    "NET_TOKEN_CLASS_BITS has no %s bit" % (cname, cname))
            if not n["TOKENS"][cname]:
                die("networks.json: token class %s is empty - a class no name "
                    "can carry is a rule the classifier can never reach" % cname)
            for t in n["TOKENS"][cname]:
                if not isinstance(t, str) or not t.isascii() or not t.isalpha() \
                        or t != t.lower():
                    die("networks.json: token %r in %s is not lowercase ASCII "
                        "letters - the scanner splits a name into runs of "
                        "letters, so nothing else can ever match" % (t, cname))
                if len(t) < minlen:
                    die("networks.json: token %r in %s is shorter than "
                        "TOKEN_MIN_LEN %d, so the scanner never looks it up"
                        % (t, cname, minlen))
                if t in owner:
                    die("networks.json: token %r is in both %s and %s"
                        % (t, owner[t], cname))
                owner[t] = cname
        ords = n["NET_AUTH_ENUM"]
        if sorted(ords.values()) != list(range(len(ords))):
            die("networks.json: NET_AUTH_ENUM ordinals are not 0..%d"
                % (len(ords) - 1))
        if ords.get("OTHER") != len(ords) - 1:
            die("networks.json: NET_AUTH_ENUM's OTHER must be the LAST ordinal "
                "- it is the sink every IDF auth mode the core gains next "
                "lands on")

    # -------------------------------------------------------------------------
    #  EVERY EMITTED STRING IS CHECKED BEFORE ANY FILE IS BUILT
    #
    #  strings_es.h is spliced, not written whole, so a string this generator
    #  cannot render must stop the run rather than corrupt the one file that is
    #  edited in place. c_str_literal() is what does the checking; calling it
    #  here means the diagnosis names the JSON row instead of arriving as a
    #  compiler error inside a generated block.
    # -------------------------------------------------------------------------
    def _check_strings(self):
        for s in self.species_all:
            c_str_literal(s["name"], "species %d name" % s["id"])
            c_str_literal(s["flavor_es"], "species %d flavor_es" % s["id"])
        for a in self.attacks:
            c_str_literal(a["name"], "attack %d name" % a["id"])
        for it in self.items:
            c_str_literal(it["name"], "item %d name" % it["id"])


def die(msg):
    sys.stderr.write("gen_content.py: %s\n" % msg)
    sys.exit(2)


# =============================================================================
#  ENUMS - MAPPED BY NAME, NEVER BY THE PACK'S ORDINAL
#
#  balance.json stores type / category / effect / klass / cond / outcome as
#  STRINGS, and three of the six enums disagree with the firmware's numbering.
#  EVO_COND_ENUM is the dangerous one: the pack orders it
#  NONE HAPPINESS_GE ACTIVITY_GE CORRUPTED BATTLES_WON_GE ITEM while
#  evolution_table.h orders it NONE HAPPINESS_GE CORRUPTED ITEM ACTIVITY_GE.
#  Emitting the pack's ordinal would give the CORRUPTED families EVOC_ITEM and
#  send one rule past EVOC_COUNT, which the compile-time guard rejects. The
#  firmware numbering wins - it sits one hop from persisted content - and every
#  lookup below raises on a name it does not know rather than guessing.
# =============================================================================
TYPE_ORD = {"SIGNAL": 0, "CORRUPT": 1, "SYSTEM": 2, "NEUTRAL": 3}
TYPE_NAME = ["TYPE_SIGNAL", "TYPE_CORRUPT", "TYPE_SYSTEM", "TYPE_NEUTRAL"]

RARITY_NAME = ["SPECIES_RARITY_COMMON", "SPECIES_RARITY_UNCOMMON",
               "SPECIES_RARITY_RARE", "SPECIES_RARITY_SPECIAL"]


def rarity_name(v, what):
    """The C name of a rarity band, or a named death. Indexing the list
    directly gave `IndexError: list index out of range` for a rarity of 7 -
    the one bad-input case in this generator that produced a traceback instead
    of a sentence. _check_field_widths() catches it first now; this is the
    backstop that stays honest if that table is ever trimmed."""
    if not isinstance(v, int) or v < 0 or v >= len(RARITY_NAME):
        die("%s: rarity %r is not one of the %d bands 0..%d"
            % (what, v, len(RARITY_NAME), len(RARITY_NAME) - 1))
    return RARITY_NAME[v]

# The firmware's EvoCond numbering (data/evolution_table.h). BATTLES_WON_GE is
# deliberately absent: no rule in the pack uses it and the firmware dropped it.
EVO_COND_ORD = {
    "NONE": 0, "HAPPINESS_GE": 1, "CORRUPTED": 2, "ITEM": 3, "ACTIVITY_GE": 4,
}
EVO_COND_NAME = ["EVOC_NONE", "EVOC_HAPPINESS_GE", "EVOC_CORRUPTED",
                 "EVOC_ITEM", "EVOC_ACTIVITY_GE"]


def enum_from(balance, key, prefix):
    """An ordinal map built from balance.json's own list order, plus the C names."""
    names = balance[key]
    return ({n: i for i, n in enumerate(names)}, [prefix + n for n in names])


# =============================================================================
#  CONTENT_VERSION - THE HASH, AND WHAT IT IS A HASH OF
#
#  Plan line 668: "CONTENT_VERSION changes when JSON changes". The input set is
#  stated here rather than left to whoever reads the number later:
#
#    * the seven JSON files, in the fixed order below (networks.json joined
#      them at P5-C1: an RSSI threshold decides a network's category, and a
#      category is what indexes the encounter table, so a threshold edit changes
#      what the game hands the player exactly as an encounter weight does);
#    * every key EXCEPT the `_`-prefixed ones. Those are design and art notes
#      (_silhouette, _move_names, _doc, ...). Folding them in would mean that
#      fixing a typo in a comment marks every save on every device as made
#      against foreign content;
#    * the emitted roster size, because two builds that ship different numbers
#      of species ARE different content even from identical JSON.
#
#  FNV-1a 32 over that canonical UTF-8 text, folded to u16 with h ^ (h >> 16),
#  and 0 is remapped to 1 so the value is never the "unset" one a zeroed blob
#  would carry.
# =============================================================================
HASH_FILES = ["species", "attacks", "items", "evolution", "encounters",
              "networks", "balance"]


def strip_notes(o):
    if isinstance(o, dict):
        return {k: strip_notes(v) for k, v in o.items() if not k.startswith("_")}
    if isinstance(o, list):
        return [strip_notes(v) for v in o]
    return o


def content_hash(c):
    parts = []
    for name in HASH_FILES:
        obj = {"species": c.species_all, "attacks": c.attacks, "items": c.items,
               "evolution": c.evo_all, "encounters": c.encounters,
               "networks": c.networks, "balance": c.balance}[name]
        parts.append(name + "=" + json.dumps(strip_notes(obj), sort_keys=True,
                                             separators=(",", ":"),
                                             ensure_ascii=False))
    parts.append("roster_families=%d" % c.families)
    blob = "\n".join(parts).encode("utf-8")
    h = 0x811C9DC5
    for b in blob:
        h ^= b
        h = (h * 0x01000193) & 0xFFFFFFFF
    v = (h ^ (h >> 16)) & 0xFFFF
    return v if v != 0 else 1


# =============================================================================
#  EMITTERS
# =============================================================================
def banner(title, body):
    out = ["// " + "=" * 77,
           "//  PEBBLEBOL - " + title,
           "//",
           "//  GENERATED FILE. Do not edit: tools/gen_content.py rewrites it from",
           "//  tools/content/*.json. `tools/gen_content.py --check` fails the gate if",
           "//  this file and the JSON have drifted apart.",
           "//"]
    for line in body:
        out.append("//  " + line if line else "//")
    out.append("// " + "=" * 77)
    return "\n".join(out) + "\n"


def c_str_comment(s):
    return s.replace("*/", "* /")


def c_str_literal(s, what):
    """A JSON string as the BODY of a C string literal, or a named death.

    Escaping is not cosmetic here. A name carrying a double quote or a backslash
    used to be copied raw into the ES[] array, so the generator produced C that
    does not compile and the operator read an error pointing at strings_es.h
    rather than at the JSON row that caused it. A control character is worse: it
    also splits the `// species N, <name>` comment lines in the StrId block.

    Latin-1 is checked here rather than left to the pack's verify.py, because
    this generator is what WRITES core/strings_es.h: u8g2_font_5x8_tf has no
    glyph past Latin-1, so an em dash or an ellipsis is a string the device
    cannot draw, and refusing to emit it is the only useful answer.
    """
    for ch in s:
        o = ord(ch)
        if o < 0x20 or o == 0x7F:
            die("%s: control character U+%04X - a shipped string is one line"
                % (what, o))
        if o > 0xFF:
            die("%s: %r contains U+%04X, which is not Latin-1 - "
                "core/strings_es.h renders through u8g2_font_5x8_tf and has no "
                "glyph for it (accents and n-tilde are fine; em dash, ellipsis, "
                "curly quotes and arrows are not)" % (what, s, o))
    return s.replace("\\", "\\\\").replace('"', '\\"')


def emit_species(c):
    sp = c.species
    fam_base = {}
    for s in sp:
        if s["stage"] == 0:
            fam_base[s["family"]] = s["id"]

    o = [banner("data/species_table.h", [
        "THE SPECIES ROSTER (plan 1.5.2, spec sections 11/12/19/22).",
        "",
        "%d species = %d families x 3 stages, a PREFIX of the %d-species content"
        % (len(sp), c.families, len(c.species_all)),
        "pack in tools/content/. Contiguity (id == index + 1) is a static_assert,",
        "so the only legal subset of the pack is a prefix; see the banner in",
        "tools/gen_content.py for the flash and sprite-atlas measurements that",
        "chose this number.",
        "",
        "Everything is `inline constexpr`, so the rows live in flash and no",
        "translation unit gets a private copy. Nothing derived is stored on a",
        "Pebble (spec section 10): hp_max, atk, def and spd are recomputed from",
        "these base numbers, the level and the genome on every read - see",
        "game/pebble.h.",
        "",
        "Pure header: stdint, the save schema's shared constants and",
        "core/strings_es.h for the StrIds in name_idx / flavor_idx. No Arduino.",
    ])]
    o.append("#ifndef PB_SPECIES_TABLE_H\n#define PB_SPECIES_TABLE_H\n")
    o.append('#include <stdint.h>\n#include <stddef.h>\n')
    o.append('#include "../core/strings_es.h"           // StrId: name_idx / flavor_idx below')
    o.append('#include "../persistence/save_schema.h"   // PB_MOVE_COUNT, PB_LEVEL_MAX\n')
    o.append("""// PebbleType (spec section 12). The chart is three-cornered and TYPE_COUNT is
// its dimension: SIGNAL beats CORRUPT beats SYSTEM beats SIGNAL.
//
// TYPE_NEUTRAL SHARES THE VALUE 3 WITH TYPE_COUNT, AND THAT IS DELIBERATE. It
// is an ATTACK-ONLY type (8 of the 34 attacks are NEUTRAL, two of them the
// damage moves every learnset is required to carry): it has no row and no
// column in TYPE_CHART and its modifier is 0 against everything. A SPECIES may
// never be NEUTRAL, which is why the species guard below still reads
// `type >= TYPE_COUNT` while the attack guard reads `type > TYPE_NEUTRAL`.
enum PebbleType : uint8_t {
  TYPE_SIGNAL = 0,
  TYPE_CORRUPT = 1,
  TYPE_SYSTEM = 2,
  TYPE_COUNT = 3,        // species types, and the dimension of TYPE_CHART
  TYPE_NEUTRAL = 3,      // attacks only: no chart row, modifier always 0
  TYPE_ATTACK_COUNT = 4
};

// Rarity bands (spec section 22).
#define SPECIES_RARITY_COMMON    0
#define SPECIES_RARITY_UNCOMMON  1
#define SPECIES_RARITY_RARE      2
#define SPECIES_RARITY_SPECIAL   3
#define SPECIES_RARITY_COUNT     4

#define SPECIES_EVO_NONE         0xFFu   // SpeciesDef.evo_rule: no evolution
#define SPECIES_ID_MIN           1u      // 0 marks an empty slot
#define SPECIES_ID_BUILTIN_MAX   199u    // 200..209 are the creator's cs0..cs9
#define SPECIES_ID_STARTER       1u      // the fresh-device starter (P2-C10)

struct SpeciesDef {                 // 24 B, plan 1.5.2
  uint8_t  id;                      // 1..199, contiguous == index + 1
  uint8_t  family;                  // 1..20+
  uint8_t  stage;                   // 0 base, 1 mid, 2 final (spec section 19)
  uint8_t  type;                    // PebbleType (spec section 12)
  uint8_t  base_hp, base_atk, base_def, base_spd;   // 1..10 (spec section 11)
  uint8_t  moves[PB_MOVE_COUNT];    // learnset
  uint8_t  evo_rule;                // index into EVOLUTION_RULES[], 0xFF = none
  uint8_t  rarity;                  // SPECIES_RARITY_*
  uint8_t  spawn_weight;            // relative weight inside its rarity band
  uint8_t  compat_group;            // breeding (spec section 17)
  uint8_t  category_mask;           // NetCategory bits it can spawn under
  uint8_t  sprite_id;               // body index in the atlas
  uint16_t name_idx;                // StrId of the Spanish species name
  uint16_t flavor_idx;              // StrId of the Spanish flavor line
  uint8_t  reserved[2];             // must be 0
};
static_assert(sizeof(SpeciesDef) == 24, "SpeciesDef layout drifted");
""")
    o.append("// --- the roster -------------------------------------------------------------")
    o.append("inline constexpr SpeciesDef SPECIES_TABLE[] = {")
    o.append("  //  id fam stg type          hp atk def spd  moves                evo\n  //    rarity                    wt grp cat spr  name             flavor")
    for s in sp:
        evo = "SPECIES_EVO_NONE" if s["evo_rule"] == 255 else str(s["evo_rule"])
        rar = rarity_name(s["rarity"], "species %d" % s["id"])
        mv = ", ".join("%2d" % m for m in s["moves"])
        o.append("  { %3d, %2d, %2d, %-13s %2d, %2d, %2d, %2d, { %s }, %-17s"
                 % (s["id"], s["family"], s["stage"],
                    TYPE_NAME[TYPE_ORD[s["type"]]] + ",",
                    s["base_hp"], s["base_atk"], s["base_def"], s["base_spd"],
                    mv, evo + ","))
        o.append("    %-25s %3d, %2d, %3d, %2d, %-16s %-16s { 0, 0 } },   // %s"
                 % (rar + ",", s["spawn_weight"], s["compat_group"], s["category_mask"],
                    s["sprite_id"], "STR_SPC_NAME_%d," % s["id"],
                    "STR_SPC_FLAV_%d," % s["id"], c_str_comment(s["name"])))
    o.append("};\n")
    o.append("inline constexpr uint8_t SPECIES_TABLE_COUNT =")
    o.append("    (uint8_t)(sizeof(SPECIES_TABLE) / sizeof(SPECIES_TABLE[0]));")
    o.append("inline constexpr uint8_t SPECIES_FAMILY_COUNT = %d;\n" % c.families)

    o.append("""// The BASE-stage species of every family, indexed by (family - 1). Two callers
// need it and neither should re-derive it: persistence/migration.cpp lands each
// legacy v1 family on a base-stage creature, and P7 breeding gives an offspring
// the base stage of its parent's family (plan line 633).""")
    o.append("inline constexpr uint8_t SPECIES_BASE_OF_FAMILY[SPECIES_FAMILY_COUNT] = {")
    row = []
    for f in range(1, c.families + 1):
        row.append("%3d" % fam_base[f])
    for i in range(0, len(row), 8):
        o.append("  " + ", ".join(row[i:i + 8]) + ",")
    o.append("};\n")

    # spawn weight sums, u16: ten of the twenty-four cells exceed 255 at 60 rows.
    bits = c.balance["NET_CATEGORY_BITS"]
    cats = list(c.balance["NET_CATEGORY_ORDINALS"].keys())
    cats.sort(key=lambda n: c.balance["NET_CATEGORY_ORDINALS"][n])
    o.append("""// SPAWN WEIGHT SUMS, precomputed per (network category, rarity band).
// plan 1.5.2 asks for the guard `sum(spawn_weight) > 0 per category`; the
// PICKER needs the sums themselves, and they must be uint16_t - a single
// category's common band already sums past 255 on this roster, so a u8 table
// would silently truncate the range the encounter roll draws from.""")
    o.append("#define NET_CATEGORY_COUNT  %d" % len(cats))
    o.append("inline constexpr uint16_t SPECIES_SPAWN_SUM[NET_CATEGORY_COUNT][SPECIES_RARITY_COUNT] = {")
    for cn in cats:
        bit = bits[cn]
        band = [0, 0, 0, 0]
        for s in sp:
            if s["category_mask"] & bit:
                band[s["rarity"]] += s["spawn_weight"]
        o.append("  { %5d, %5d, %5d, %5d },   // %s" % (band[0], band[1], band[2], band[3], cn))
    o.append("};\n")

    o.append("""// --- generator-emitted compile-time guards (plan 1.5.2) ----------------------
constexpr bool species_rows_are_well_formed(void) {
  for (uint8_t i = 0; i < (uint8_t)(sizeof(SPECIES_TABLE) / sizeof(SPECIES_TABLE[0])); ++i) {
    const SpeciesDef& sp = SPECIES_TABLE[i];
    if (sp.id != (uint8_t)(i + 1u))                    return false;
    if (sp.family == 0u)                               return false;
    if (sp.family > SPECIES_FAMILY_COUNT)              return false;
    if (sp.stage > 2u)                                 return false;
    if (sp.type >= (uint8_t)TYPE_COUNT)                return false;   // never NEUTRAL
    if (sp.base_hp == 0u || sp.base_atk == 0u)         return false;
    if (sp.base_def == 0u || sp.base_spd == 0u)        return false;
    if (sp.rarity > SPECIES_RARITY_SPECIAL)            return false;
    if (sp.compat_group == 0u)                         return false;
    if (sp.category_mask == 0u)                        return false;
    if (sp.name_idx   >= (uint16_t)STR_COUNT)          return false;
    if (sp.flavor_idx >= (uint16_t)STR_COUNT)          return false;
    if (sp.reserved[0] != 0u || sp.reserved[1] != 0u)  return false;
  }
  return true;
}

// Every family has exactly one stage-0 row and SPECIES_BASE_OF_FAMILY points at
// it. This is what makes a legacy migration and a bred offspring land on a
// creature that can still evolve.
constexpr bool species_family_bases_resolve(void) {
  for (uint8_t f = 0; f < SPECIES_FAMILY_COUNT; ++f) {
    const uint8_t id = SPECIES_BASE_OF_FAMILY[f];
    if (id < SPECIES_ID_MIN || id > SPECIES_TABLE_COUNT) return false;
    const SpeciesDef& sp = SPECIES_TABLE[id - 1u];
    if (sp.family != (uint8_t)(f + 1u)) return false;
    if (sp.stage != 0u)                 return false;
  }
  for (uint8_t i = 0; i < SPECIES_TABLE_COUNT; ++i) {
    const SpeciesDef& sp = SPECIES_TABLE[i];
    if (sp.stage != 0u) continue;
    if (SPECIES_BASE_OF_FAMILY[sp.family - 1u] != sp.id) return false;
  }
  return true;
}

// plan 1.5.2: `sum(spawn_weight) > 0 per category`. Nothing may be spawnable
// nowhere, and no category may be empty of common creatures.
constexpr bool species_spawn_sums_are_usable(void) {
  for (uint8_t c = 0; c < NET_CATEGORY_COUNT; ++c) {
    uint32_t total = 0;
    for (uint8_t r = 0; r < SPECIES_RARITY_COUNT; ++r) total += SPECIES_SPAWN_SUM[c][r];
    if (total == 0u) return false;
    if (SPECIES_SPAWN_SUM[c][SPECIES_RARITY_COMMON] == 0u) return false;
  }
  for (uint8_t i = 0; i < SPECIES_TABLE_COUNT; ++i)
    if (SPECIES_TABLE[i].spawn_weight == 0u) return false;
  return true;
}

static_assert(SPECIES_TABLE_COUNT >= 1, "the roster needs at least the starter");
static_assert(SPECIES_TABLE_COUNT <= SPECIES_ID_BUILTIN_MAX, "roster exceeds id 199");
static_assert(SPECIES_TABLE[0].id == SPECIES_ID_MIN, "species ids start at 1");
static_assert(SPECIES_TABLE[SPECIES_TABLE_COUNT - 1].id == SPECIES_TABLE_COUNT,
              "species ids must be contiguous: id == index + 1");
static_assert(SPECIES_TABLE_COUNT == SPECIES_FAMILY_COUNT * 3u,
              "a roster is whole families: 3 stages each");
static_assert(species_rows_are_well_formed(),
              "a species row has a bad id, family, stage, type, rarity or string index");
static_assert(species_family_bases_resolve(),
              "SPECIES_BASE_OF_FAMILY does not point at every family's stage-0 row");
static_assert(species_spawn_sums_are_usable(),
              "a network category has no spawnable species, or a row has weight 0");
static_assert(SPECIES_TABLE[SPECIES_ID_STARTER - 1].id == SPECIES_ID_STARTER,
              "the starter species must be the first row");

// Resolves a built-in species id. Returns nullptr for 0 (empty slot), for an
// id beyond the roster and for the custom range - P8 resolves cs* records here
// so no battle or validator code ever branches on "custom".
inline const SpeciesDef* species_get(uint8_t id) {
  if (id < SPECIES_ID_MIN || id > SPECIES_TABLE_COUNT) return nullptr;
  return &SPECIES_TABLE[id - 1u];
}

#endif // PB_SPECIES_TABLE_H""")
    return "\n".join(o) + "\n"


def emit_attacks(c):
    bal = c.balance
    cat_ord, cat_names = enum_from(bal, "ATTACK_CATEGORY_ENUM", "ATK_CAT_")
    eff_ord, eff_names = enum_from(bal, "EFFECT_ENUM", "ATK_EFF_")
    chart = bal["TYPE_CHART"]

    o = [banner("data/attacks_table.h", [
        "THE ATTACK TABLE and TYPE_CHART (plan 1.5.2, spec sections 12 and 13).",
        "",
        "An attack is DATA. Spec section 13: \"the combat engine must resolve",
        "attacks through data, not hard-coded species-specific logic\", so every",
        "field a round needs is a column here and P4-C2 reads nothing else.",
        "",
        "This header also carries the SPECIES <-> ATTACK cross guard, because it",
        "is the first file that has seen both tables.",
    ])]
    o.append("#ifndef PB_ATTACKS_TABLE_H\n#define PB_ATTACKS_TABLE_H\n")
    o.append('#include <stdint.h>\n#include <stddef.h>\n')
    o.append('#include "species_table.h"        // PebbleType, TYPE_CHART dimension, the roster')
    o.append('#include "../core/strings_es.h"   // StrId: name_idx\n')

    o.append("// Attack categories (spec section 13). All six ship.")
    o.append("enum AttackCategory : uint8_t {")
    for n in cat_names:
        o.append("  %s," % n)
    o.append("  ATK_CAT_COUNT")
    o.append("};\n")

    o.append("// What an attack DOES besides damage. One slot per shipped effect.")
    o.append("enum AttackEffect : uint8_t {")
    for n in eff_names:
        o.append("  %s," % n)
    o.append("  ATK_EFF_COUNT")
    o.append("};\n")

    o.append("""struct AttackDef {          // 16 B, plan 1.5.2
  uint8_t  id;                // 1..ATTACK_COUNT, contiguous == index + 1
  uint8_t  type;              // PebbleType, TYPE_NEUTRAL allowed
  uint8_t  category;          // AttackCategory
  uint8_t  power;             // 0..100, 0 for a pure status move
  uint8_t  accuracy;          // 0..100
  int8_t   priority;          // resolved BEFORE effective speed (spec 14 step 2)
  uint8_t  effect;            // AttackEffect
  uint8_t  effect_value;      // magnitude; unit depends on effect
  uint8_t  effect_duration;   // rounds; 0 = instant
  uint8_t  cooldown;          // rounds unavailable after use
  uint8_t  anim_id;           // index into the 14 animation films (P10-C3)
  uint8_t  budget_cost;       // spec section 36 creator pricing
  uint16_t name_idx;          // StrId of the Spanish attack name
  uint8_t  reserved[2];       // must be 0
};
static_assert(sizeof(AttackDef) == 16, "AttackDef layout drifted");
""")
    o.append("inline constexpr AttackDef ATTACKS_TABLE[] = {")
    o.append("  //  id type          category          pow acc pri effect                  val dur cd anim cost  name")
    for a in c.attacks:
        o.append("  { %2d, %-13s %-17s %3d, %3d, %2d, %-23s %2d, %2d, %2d, %2d, %3d, %-17s { 0, 0 } },   // %s"
                 % (a["id"], TYPE_NAME[TYPE_ORD[a["type"]]] + ",",
                    cat_names[cat_ord[a["category"]]] + ",",
                    a["power"], a["accuracy"], a["priority"],
                    eff_names[eff_ord[a["effect"]]] + ",",
                    a["effect_value"], a["effect_duration"], a["cooldown"],
                    a["anim_id"], a["budget_cost"],
                    "STR_ATK_NAME_%d," % a["id"], c_str_comment(a["name"])))
    o.append("};\n")
    o.append("inline constexpr uint8_t ATTACK_COUNT =")
    o.append("    (uint8_t)(sizeof(ATTACKS_TABLE) / sizeof(ATTACKS_TABLE[0]));\n")

    o.append("""// TYPE_CHART[attacker][defender], +1 advantage / 0 neutral / -1 disadvantage
// (spec section 12). A NEUTRAL attack never indexes this table: its modifier is
// 0 unconditionally, which is why the array is TYPE_COUNT and not
// TYPE_ATTACK_COUNT wide.""")
    o.append("inline constexpr int8_t TYPE_CHART[TYPE_COUNT][TYPE_COUNT] = {")
    for i, row in enumerate(chart):
        o.append("  { %2d, %2d, %2d },   // %s attacking" % (row[0], row[1], row[2],
                                                             TYPE_NAME[i]))
    o.append("};\n")

    o.append("""// The chart is the SIGNAL > CORRUPT > SYSTEM > SIGNAL cycle and nothing else:
// no self-advantage, and every pair is antisymmetric.
constexpr bool type_chart_is_a_cycle(void) {
  for (uint8_t a = 0; a < (uint8_t)TYPE_COUNT; ++a) {
    if (TYPE_CHART[a][a] != 0) return false;
    for (uint8_t d = 0; d < (uint8_t)TYPE_COUNT; ++d) {
      if (TYPE_CHART[a][d] < -1 || TYPE_CHART[a][d] > 1) return false;
      if (TYPE_CHART[a][d] != -TYPE_CHART[d][a]) return false;
    }
  }
  return true;
}

constexpr bool attack_rows_are_well_formed(void) {
  for (uint8_t i = 0; i < (uint8_t)(sizeof(ATTACKS_TABLE) / sizeof(ATTACKS_TABLE[0])); ++i) {
    const AttackDef& a = ATTACKS_TABLE[i];
    if (a.id != (uint8_t)(i + 1u))                    return false;
    if (a.type > (uint8_t)TYPE_NEUTRAL)               return false;
    if (a.category >= (uint8_t)ATK_CAT_COUNT)         return false;
    if (a.effect >= (uint8_t)ATK_EFF_COUNT)           return false;
    if (a.power > 100u)                               return false;
    if (a.accuracy == 0u || a.accuracy > 100u)        return false;
    if (a.anim_id == 0u)                              return false;
    if (a.name_idx >= (uint16_t)STR_COUNT)            return false;
    if (a.reserved[0] != 0u || a.reserved[1] != 0u)   return false;
  }
  return true;
}

// plan 1.5.2: "every moves[i] < ATTACK_COUNT". THE PLAN'S LITERAL FORM IS OFF
// BY ONE and would reject the last attack in the table: attack ids are 1-based
// (ATTACKS_TABLE[id - 1]), so the true bound is 1 <= id <= ATTACK_COUNT. This
// is the guard species_table.h could not carry until an attack table existed;
// its own comment said so and said P4-C1 would add it without moving a number.
constexpr bool species_learnsets_resolve(void) {
  for (uint8_t i = 0; i < SPECIES_TABLE_COUNT; ++i) {
    const SpeciesDef& sp = SPECIES_TABLE[i];
    for (uint8_t m = 0; m < (uint8_t)PB_MOVE_COUNT; ++m) {
      if (sp.moves[m] < 1u || sp.moves[m] > ATTACK_COUNT) return false;
      for (uint8_t n = (uint8_t)(m + 1u); n < (uint8_t)PB_MOVE_COUNT; ++n)
        if (sp.moves[m] == sp.moves[n]) return false;          // 4 DISTINCT moves
    }
  }
  return true;
}

// Spec section 13's learnset rule, as the content pack states it: every move a
// species knows is its own type or NEUTRAL, and at least one of the four does
// damage. A learnset of four status moves cannot win a battle.
constexpr bool species_learnsets_are_legal(void) {
  for (uint8_t i = 0; i < SPECIES_TABLE_COUNT; ++i) {
    const SpeciesDef& sp = SPECIES_TABLE[i];
    bool has_damage = false;
    for (uint8_t m = 0; m < (uint8_t)PB_MOVE_COUNT; ++m) {
      // species_learnsets_resolve() above already rejects an id outside
      // 1..ATTACK_COUNT by name, and its static_assert is declared first. The
      // bound is repeated here anyway because THIS function INDEXES the table:
      // a constexpr read past the end is not a false return, it is an
      // "array subscript ... outside the bounds" error with no sentence in it,
      // and it would be what an operator saw if that guard were ever deleted.
      if (sp.moves[m] < 1u || sp.moves[m] > ATTACK_COUNT) return false;
      const AttackDef& a = ATTACKS_TABLE[sp.moves[m] - 1u];
      if (a.type != sp.type && a.type != (uint8_t)TYPE_NEUTRAL) return false;
      if (a.power > 0u) has_damage = true;
    }
    if (!has_damage) return false;
  }
  return true;
}

static_assert(ATTACK_COUNT >= 1, "the attack table is empty");
static_assert(type_chart_is_a_cycle(),
              "TYPE_CHART is not the antisymmetric SIGNAL>CORRUPT>SYSTEM>SIGNAL cycle");
static_assert(attack_rows_are_well_formed(),
              "an attack row has a bad id, type, category, effect, accuracy or string index");
static_assert(species_learnsets_resolve(),
              "a species learnset holds an unknown or repeated attack id");
static_assert(species_learnsets_are_legal(),
              "a learnset holds an off-type attack, has no damaging move, or holds "
              "an id outside 1..ATTACK_COUNT (which the guard above names first)");

// Resolves an attack id. 0 is the empty move slot of PebbleInstance.moves and
// returns nullptr, exactly like an id past the table.
inline const AttackDef* attack_get(uint8_t id) {
  if (id < 1u || id > ATTACK_COUNT) return nullptr;
  return &ATTACKS_TABLE[id - 1u];
}

// The type modifier of `atk_type` against `def_type`: -1, 0 or +1. NEUTRAL and
// anything out of range answer 0 rather than indexing the chart.
inline int8_t type_mod_of(uint8_t atk_type, uint8_t def_type) {
  if (atk_type >= (uint8_t)TYPE_COUNT || def_type >= (uint8_t)TYPE_COUNT) return 0;
  return TYPE_CHART[atk_type][def_type];
}

#endif // PB_ATTACKS_TABLE_H""")
    return "\n".join(o) + "\n"


def emit_items(c):
    bal = c.balance
    kl_ord, kl_names = enum_from(bal, "ITEM_KLASS_ENUM", "ITEM_KLASS_")

    # A CARE item whose value is 0 restores nothing, by the unit contract three
    # lines below. That is not a typo: it is how the pack folds the EVOLUTION
    # KEY into a class list that has no slot for one. Found by hand during the
    # completeness pass and now DERIVED, so the banner cannot go stale against
    # the JSON - and so a second one appearing is not silently normal.
    keyed = {}
    for r in c.evo_all:
        if r["cond"] == "ITEM":
            keyed.setdefault(r["cond_value"], []).append(r)
    zero_care = [it for it in c.items
                 if it["klass"] == "CARE" and it["value"] == 0]
    hole = []
    if zero_care:
        hole = ["",
                "A THIRD, SMALLER HOLE OF THE SAME SHAPE, found the same way and",
                "left in the content rather than patched here:"]
        for it in zero_care:
            rules = keyed.get(it["id"], [])
            hole.append("  item %d %s is CARE with value 0 - a care item that"
                        % (it["id"], c_str_comment(it["name"])))
            hole.append("  restores nothing by the contract above. Its real role is")
            if rules:
                hole.append("  the EVOC_ITEM key: evolution.json turns species %s"
                            % ", ".join(str(r["species"]) for r in rules))
                hole.append("  into %s with cond ITEM, cond_value %d."
                            % (", ".join(str(r["target"]) for r in rules), it["id"]))
            else:
                hole.append("  NOT an evolution key either - no rule names it.")
        hole.append("  Spec section 24 names no evolution-item class, so the pack")
        hole.append("  folded it onto CARE. P5-C4 must not read it as a care item.")

    o = [banner("data/items_table.h", [
        "THE ITEM TABLE (plan 1.5.2, spec section 24).",
        "",
        "ItemDef.value is a MAGNITUDE whose unit depends on klass, which is the",
        "pack's own contract (balance.json _ITEM_VALUE_doc):",
        "  XP_CANDY    value * ITEM_XP_CANDY_SCALE = XP granted",
        "  CAPTURE     value = capture-chance bonus in permille",
        "  CARE        value = care points restored",
        "  BATTLE_MOD  value = stat stages granted at battle start",
        "",
        "TWO OF THOSE FOUR UNITS NAME NO TARGET, and the pack has no column for",
        "one: a CARE item does not say WHICH of the five care stats it restores,",
        "and a BATTLE_MOD does not say which stat it buffs. P5-C4 and P4-C2",
        "cannot resolve them from this table alone. Recorded here rather than",
        "guessed - see the completeness pass in the P4-C1 exit.",
    ] + hole)]
    o.append("#ifndef PB_ITEMS_TABLE_H\n#define PB_ITEMS_TABLE_H\n")
    o.append('#include <stdint.h>\n#include <stddef.h>\n')
    o.append('#include "species_table.h"        // SPECIES_RARITY_*')
    o.append('#include "../core/strings_es.h"   // StrId: name_idx\n')
    o.append("enum ItemKlass : uint8_t {")
    for n in kl_names:
        o.append("  %s," % n)
    o.append("  ITEM_KLASS_COUNT")
    o.append("};\n")
    o.append("// XP_CANDY value is scaled by this to get the XP it grants.")
    o.append("#define ITEM_XP_CANDY_SCALE  %d\n" % bal["XP_CANDY_SCALE"])
    o.append("""struct ItemDef {            // 8 B, plan 1.5.2
  uint8_t  id;              // 1..ITEM_COUNT, contiguous == index + 1
  uint8_t  klass;           // ItemKlass
  uint8_t  value;           // magnitude, unit per klass (see the banner)
  uint8_t  rarity;          // SPECIES_RARITY_*
  uint16_t name_idx;        // StrId of the Spanish item name
  uint8_t  reserved[2];     // must be 0
};
static_assert(sizeof(ItemDef) == 8, "ItemDef layout drifted");
""")
    o.append("inline constexpr ItemDef ITEMS_TABLE[] = {")
    o.append("  //  id klass                 val rarity                    name")
    for it in c.items:
        rar = rarity_name(it["rarity"], "item %d" % it["id"])
        o.append("  { %2d, %-21s %3d, %-25s %-18s { 0, 0 } },   // %s"
                 % (it["id"], kl_names[kl_ord[it["klass"]]] + ",", it["value"],
                    rar + ",", "STR_ITEM_NAME_%d," % it["id"],
                    c_str_comment(it["name"])))
    o.append("};\n")
    o.append("inline constexpr uint8_t ITEM_COUNT =")
    o.append("    (uint8_t)(sizeof(ITEMS_TABLE) / sizeof(ITEMS_TABLE[0]));\n")
    o.append("""constexpr bool item_rows_are_well_formed(void) {
  for (uint8_t i = 0; i < (uint8_t)(sizeof(ITEMS_TABLE) / sizeof(ITEMS_TABLE[0])); ++i) {
    const ItemDef& it = ITEMS_TABLE[i];
    if (it.id != (uint8_t)(i + 1u))                     return false;
    if (it.klass >= (uint8_t)ITEM_KLASS_COUNT)          return false;
    if (it.rarity > SPECIES_RARITY_SPECIAL)             return false;
    if (it.name_idx >= (uint16_t)STR_COUNT)             return false;
    if (it.reserved[0] != 0u || it.reserved[1] != 0u)   return false;
  }
  return true;
}

// Every one of spec section 24's four classes has at least one row. A class
// with no item is a menu entry the player can never fill.
constexpr bool item_klasses_are_all_populated(void) {
  for (uint8_t k = 0; k < (uint8_t)ITEM_KLASS_COUNT; ++k) {
    bool seen = false;
    for (uint8_t i = 0; i < ITEM_COUNT; ++i)
      if (ITEMS_TABLE[i].klass == k) seen = true;
    if (!seen) return false;
  }
  return true;
}

static_assert(ITEM_COUNT >= 1, "the item table is empty");
static_assert(item_rows_are_well_formed(),
              "an item row has a bad id, klass, rarity or string index");
static_assert(item_klasses_are_all_populated(),
              "a spec section 24 item class has no row");

inline const ItemDef* item_get(uint8_t id) {
  if (id < 1u || id > ITEM_COUNT) return nullptr;
  return &ITEMS_TABLE[id - 1u];
}

#endif // PB_ITEMS_TABLE_H""")
    return "\n".join(o) + "\n"


def emit_evolution(c):
    o = [banner("data/evolution_table.h", [
        "THE EVOLUTION RULES (spec section 18, plan 1.5.2).",
        "",
        "One row per species that can leave its stage. A row is DATA: a minimum",
        "level, an optional condition and the species on the other side.",
        "game/evolution.cpp is the only code that reads it, and it refuses any",
        "rule whose condition it cannot evaluate - see EvoContext in",
        "game/evolution.h.",
        "",
        "SpeciesDef.evo_rule is the INDEX into this array, so the ORDER here is",
        "contractual. The generator emits the pack's own ordering,",
        "evo_rule == (family - 1) * 2 + stage, and evo_roster_agrees() below",
        "fails the build if it ever drifts.",
        "",
        "EvoCond KEEPS THE FIRMWARE'S NUMBERING, not the content pack's. The two",
        "disagree on three of six values and this enum sits one hop from",
        "persisted content, so gen_content.py maps by NAME and errors on a name",
        "it does not know. BATTLES_WON_GE, which the pack lists and no rule uses,",
        "stays dropped.",
    ])]
    o.append("#ifndef PB_EVOLUTION_TABLE_H\n#define PB_EVOLUTION_TABLE_H\n")
    o.append('#include <stdint.h>\n#include <stddef.h>\n')
    o.append('#include "species_table.h"\n')
    o.append("""enum EvoCond : uint8_t {
  EVOC_NONE = 0,        // the level is the whole rule
  EVOC_HAPPINESS_GE,    // care[CARE_HAPPINESS] percentage >= cond_value
  EVOC_CORRUPTED,       // status carries PBS_CORRUPTED  (spec section 55)
  EVOC_ITEM,            // an item is being applied, id == cond_value      (P5)
  EVOC_ACTIVITY_GE,     // activity score 0..100 >= cond_value             (P6)
  EVOC_COUNT
};

struct EvolutionRule {          // 6 B: four bytes and an aligned u16
  uint8_t  species;             // source species id
  uint8_t  target;              // target species id
  uint8_t  level;               // minimum level to leave the source
  uint8_t  cond;                // EvoCond
  uint16_t cond_value;          // what the condition compares against
};
static_assert(sizeof(EvolutionRule) == 6, "EvolutionRule layout drifted");
""")
    o.append("inline constexpr EvolutionRule EVOLUTION_RULES[] = {")
    o.append("  // from   to  lvl  cond                  value")
    for i, r in enumerate(c.evo):
        if r["cond"] not in EVO_COND_ORD:
            die("evolution rule %d uses condition %r, which this firmware's "
                "EvoCond does not carry" % (i, r["cond"]))
        cond = EVO_COND_NAME[EVO_COND_ORD[r["cond"]]]
        o.append("  { %3d, %3d, %3d, %-21s %5d },   // [%2d] %s -> %s"
                 % (r["species"], r["target"], r["level"], cond + ",",
                    r["cond_value"], i, c_str_comment(r.get("_from", "?")),
                    c_str_comment(r.get("_to", "?"))))
    o.append("};\n")
    o.append("inline constexpr uint8_t EVOLUTION_RULES_COUNT =")
    o.append("    (uint8_t)(sizeof(EVOLUTION_RULES) / sizeof(EVOLUTION_RULES[0]));\n")
    o.append("""// Resolves a rule index. Returns nullptr for SPECIES_EVO_NONE and for anything
// past the table, which is what a species with no evolution looks like.
inline const EvolutionRule* evolution_rule_at(uint8_t idx) {
  if (idx >= EVOLUTION_RULES_COUNT) return nullptr;
  return &EVOLUTION_RULES[idx];
}

// -----------------------------------------------------------------------------
//  GENERATOR-EMITTED COMPILE-TIME GUARDS (plan 1.5.2)
//
//  constexpr so a bad table is a BUILD failure, not a runtime surprise on a
//  device in someone's pocket. tests/test_content.cpp re-checks every one of
//  them at runtime as well, so a future table that somehow slipped past a
//  compiler still fails the gate.
// -----------------------------------------------------------------------------
constexpr const SpeciesDef* evo_species_at(uint8_t id) {
  return (id < SPECIES_ID_MIN || id > SPECIES_TABLE_COUNT)
             ? nullptr : &SPECIES_TABLE[id - 1u];
}

// Every rule resolves on both ends, moves WITHIN one family and moves UP
// exactly one stage.
constexpr bool evo_rules_are_well_formed(void) {
  for (uint8_t i = 0; i < EVOLUTION_RULES_COUNT; ++i) {
    const EvolutionRule& r = EVOLUTION_RULES[i];
    const SpeciesDef* s = evo_species_at(r.species);
    const SpeciesDef* t = evo_species_at(r.target);
    if (s == nullptr || t == nullptr)            return false;
    if (r.target == r.species)                   return false;
    if (s->family != t->family)                  return false;
    if ((int)t->stage != (int)s->stage + 1)      return false;
    if (r.level == 0u || r.level > (uint8_t)PB_LEVEL_MAX) return false;
    if (r.cond >= (uint8_t)EVOC_COUNT)           return false;
    // A condition that compares against a value needs one; EVOC_NONE and
    // EVOC_CORRUPTED are the two that answer without reading cond_value.
    if ((r.cond == (uint8_t)EVOC_HAPPINESS_GE || r.cond == (uint8_t)EVOC_ACTIVITY_GE ||
         r.cond == (uint8_t)EVOC_ITEM) && r.cond_value == 0u) return false;
  }
  return true;
}

// A species may leave its stage in exactly one direction: two rows with the
// same source would make the outcome depend on lookup order.
constexpr bool evo_sources_are_unique(void) {
  for (uint8_t i = 0; i < EVOLUTION_RULES_COUNT; ++i)
    for (uint8_t j = (uint8_t)(i + 1u); j < EVOLUTION_RULES_COUNT; ++j)
      if (EVOLUTION_RULES[i].species == EVOLUTION_RULES[j].species) return false;
  return true;
}

// The roster's evo_rule field and this table agree: a species either has no
// evolution or points at the row whose source it is.
constexpr bool evo_roster_agrees(void) {
  for (uint8_t i = 0; i < SPECIES_TABLE_COUNT; ++i) {
    const SpeciesDef& sp = SPECIES_TABLE[i];
    if (sp.evo_rule == SPECIES_EVO_NONE) continue;
    if (sp.evo_rule >= EVOLUTION_RULES_COUNT) return false;
    if (EVOLUTION_RULES[sp.evo_rule].species != sp.id) return false;
  }
  return true;
}

// A final-stage species must say so: nothing may point past the family.
constexpr bool evo_final_stages_have_no_rule(void) {
  for (uint8_t i = 0; i < SPECIES_TABLE_COUNT; ++i) {
    const SpeciesDef& sp = SPECIES_TABLE[i];
    if (sp.evo_rule != SPECIES_EVO_NONE) continue;
    for (uint8_t j = 0; j < EVOLUTION_RULES_COUNT; ++j)
      if (EVOLUTION_RULES[j].species == sp.id) return false;
  }
  return true;
}

// Every non-final species has a way out of its stage. A stage-0 or stage-1 row
// marked SPECIES_EVO_NONE would be a dead end in the middle of a family.
constexpr bool evo_every_non_final_stage_has_a_rule(void) {
  for (uint8_t i = 0; i < SPECIES_TABLE_COUNT; ++i) {
    const SpeciesDef& sp = SPECIES_TABLE[i];
    if (sp.stage >= 2u) continue;
    if (sp.evo_rule == SPECIES_EVO_NONE) return false;
  }
  return true;
}

static_assert(EVOLUTION_RULES_COUNT >= 1, "the table needs at least one rule");
// SPECIES_EVO_NONE is the "no evolution" marker, so it must never also be a
// legal index: keep the table strictly shorter than 0xFF rows.
static_assert(EVOLUTION_RULES_COUNT < SPECIES_EVO_NONE,
              "0xFF must stay the 'no evolution' marker, not a valid index");
static_assert(EVOLUTION_RULES_COUNT == (uint8_t)(SPECIES_FAMILY_COUNT * 2u),
              "every family owes exactly two rules: stage 0 -> 1 and 1 -> 2");
static_assert(evo_rules_are_well_formed(),
              "an evolution rule does not resolve, crosses families, skips a stage "
              "or asks a condition with no value");
static_assert(evo_sources_are_unique(), "two rules share a source species");
static_assert(evo_roster_agrees(), "SpeciesDef.evo_rule and EVOLUTION_RULES disagree");
static_assert(evo_final_stages_have_no_rule(),
              "a species marked final is the source of a rule");
static_assert(evo_every_non_final_stage_has_a_rule(),
              "a stage-0 or stage-1 species has no way out of its stage");

#endif  // PB_EVOLUTION_TABLE_H""")
    return "\n".join(o) + "\n"


def fnv1a32(text):
    """FNV-1a 32 over the ASCII bytes of `text`. THE FIRMWARE COMPUTES THE SAME
    FUNCTION over the same bytes in networking/net_classify.cpp; if these two
    ever disagree every token lookup misses silently and every network reads
    UNKNOWN, which is why tests/test_exploration_hash.cpp pins a literal from
    this side against the C++ side."""
    h = 0x811C9DC5
    for b in text.encode("ascii"):
        h ^= b
        h = (h * 0x01000193) & 0xFFFFFFFF
    return h


def emit_networks(c):
    n = c.networks
    bal = c.balance
    bits = bal["NET_CATEGORY_BITS"]
    ords = bal["NET_CATEGORY_ORDINALS"]
    cats = sorted(ords.keys(), key=lambda k: ords[k])

    auth = n["NET_AUTH_ENUM"]
    auth_names = sorted(auth.keys(), key=lambda k: auth[k])
    tclass = n["NET_TOKEN_CLASS_BITS"]
    minlen = n["TOKEN_MIN_LEN"]

    rows = []
    for cname in sorted(n["TOKENS"].keys()):
        for t in n["TOKENS"][cname]:
            rows.append((fnv1a32(t), tclass[cname], t, cname))
    rows.sort(key=lambda r: r[0])
    for i in range(1, len(rows)):
        if rows[i][0] == rows[i - 1][0]:
            die("networks.json: tokens %r and %r both hash to 0x%08X - rename "
                "one; a 32-bit collision would give one of them the other's "
                "class" % (rows[i - 1][2], rows[i][2], rows[i][0]))

    all_bits = 0
    for b in tclass.values():
        all_bits |= b

    o = [banner("data/network_table.h", [
        "THE NETWORK CLASSIFIER'S TABLE (spec sections 20, 40, 44). P5-C1.",
        "",
        "Six abstract categories, the auth modes a passive scan can tell apart,",
        "two RSSI bands and a sorted token table. networking/net_classify.cpp is",
        "the one consumer; it is a PURE translation unit, so nothing here may",
        "name a radio type - networking/net.cpp maps wifi_auth_mode_t onto",
        "NetAuth and static_asserts that mapping against the real constants.",
        "",
        "NO NAME AND NO HARDWARE ADDRESS SURVIVES A LOOKUP. The scanner splits a",
        "beacon's name into runs of ASCII letters inside its own loop, hashes",
        "each run of at least NET_TOKEN_MIN_LEN letters, binary-searches this",
        "table and ORs the classes. What leaves that loop is a %d-bit bitmask."
        % (bin(all_bits).count("1")),
        "",
        "The category ENCODING lives here rather than in encounter_table.h",
        "because it is the classifier's OUTPUT and the encounter table's INDEX,",
        "and a value with two owners is a value that drifts. encounter_table.h",
        "includes this header and keeps the count cross-check against the",
        "species roster, which is the one thing this header cannot see.",
    ])]
    o.append("#ifndef PB_NETWORK_TABLE_H\n#define PB_NETWORK_TABLE_H\n")
    o.append("#include <stdint.h>\n#include <stddef.h>\n")

    o.append("// Network categories (spec section 20). ORDINALS index EncounterRow.category;")
    o.append("// BITS are what SpeciesDef.category_mask holds. Two encodings, one set.")
    o.append("enum NetCategory : uint8_t {")
    for cn in cats:
        o.append("  NET_CAT_%s = %d," % (cn, ords[cn]))
    o.append("  NET_CAT_COUNT")
    o.append("};\n")
    o.append("inline constexpr uint8_t NET_CATEGORY_BIT[NET_CAT_COUNT] = {")
    o.append("  " + ", ".join("%d" % bits[cn] for cn in cats) + "   // " + " ".join(cats))
    o.append("};\n")

    o.append("// What a beacon says about its own security, as the classifier sees it.")
    o.append("// A PROJECT enum: net_classify.cpp may not include a radio header, so")
    o.append("// net.cpp owns the one mapping from wifi_auth_mode_t and asserts it.")
    o.append("// NAUTH_OTHER is LAST on purpose - it is the sink every constant the")
    o.append("// core gains next lands on, so a new IDF value moves a category and")
    o.append("// never produces an unhandled case.")
    o.append("enum NetAuth : uint8_t {")
    for an in auth_names:
        o.append("  NAUTH_%s = %d," % (an, auth[an]))
    o.append("  NAUTH_COUNT")
    o.append("};\n")

    o.append("// Token CLASSES - a bitmask, never text.")
    for cn in sorted(tclass.keys(), key=lambda k: tclass[k]):
        o.append("#define NTOK_%-10s %du" % (cn, tclass[cn]))
    o.append("#define NTOK_ALL %s%du" % (" " * 8, all_bits))
    o.append("#define NET_TOKEN_MIN_LEN %du" % minlen)
    o.append("// The longest token in the table below. net_classify.cpp sizes its")
    o.append("// stack buffer from this, so adding a longer token to networks.json")
    o.append("// widens the buffer instead of silently becoming unmatchable.")
    o.append("#define NET_TOKEN_MAX_LEN %du\n" % max(len(r[2]) for r in rows))

    o.append("// The two signal bands. Stronger than NEAR is 'you are inside it';")
    o.append("// between NEAR and MID is 'you are next to it'; weaker than MID is a")
    o.append("// distant beacon nothing can be claimed about.")
    o.append("#define NET_RSSI_NEAR  (%d)" % n["RSSI_NEAR"])
    o.append("#define NET_RSSI_MID   (%d)\n" % n["RSSI_MID"])

    o.append("""struct NetTokenRow {        // 8 B
  uint32_t hash;            // FNV-1a 32 of the lowercase ASCII token
  uint8_t  klass;           // exactly one NTOK_* bit
  uint8_t  reserved[3];     // must be 0
};
static_assert(sizeof(NetTokenRow) == 8, "NetTokenRow layout drifted");
""")
    o.append("// SORTED BY HASH: net_classify.cpp binary-searches this, and the guard")
    o.append("// below is what makes that search legal.")
    o.append("inline constexpr NetTokenRow NET_TOKEN_TABLE[] = {")
    for h, k, t, cname in rows:
        o.append("  { 0x%08Xu, NTOK_%-8s { 0, 0, 0 } },   // %s" % (h, cname + ",", t))
    o.append("};\n")
    o.append("inline constexpr uint8_t NET_TOKEN_ROW_COUNT =")
    o.append("    (uint8_t)(sizeof(NET_TOKEN_TABLE) / sizeof(NET_TOKEN_TABLE[0]));\n")

    o.append("""// --- generator-emitted compile-time guards -----------------------------------
constexpr bool net_category_bits_are_one_bit_each_and_distinct(void) {
  uint32_t seen = 0;
  for (uint8_t i = 0; i < (uint8_t)NET_CAT_COUNT; ++i) {
    const uint32_t b = NET_CATEGORY_BIT[i];
    if (b == 0u || (b & (b - 1u)) != 0u) return false;
    if (seen & b)                        return false;
    seen |= b;
  }
  return true;
}

// STRICTLY ascending, which proves sortedness AND uniqueness in one walk. The
// binary search in net_classify.cpp is only correct on a sorted table, and a
// duplicate hash would give one token the other's class depending on where the
// search happened to land.
constexpr bool net_token_table_is_sorted_and_unique(void) {
  for (uint8_t i = 1; i < NET_TOKEN_ROW_COUNT; ++i)
    if (NET_TOKEN_TABLE[i].hash <= NET_TOKEN_TABLE[i - 1].hash) return false;
  return true;
}

constexpr bool net_token_rows_are_well_formed(void) {
  for (uint8_t i = 0; i < NET_TOKEN_ROW_COUNT; ++i) {
    const NetTokenRow& r = NET_TOKEN_TABLE[i];
    if (r.klass == 0u)                          return false;
    if ((r.klass & (uint8_t)(r.klass - 1u)) != 0u) return false;  // one bit
    if ((r.klass & ~(uint8_t)NTOK_ALL) != 0u)   return false;
    if (r.reserved[0] || r.reserved[1] || r.reserved[2]) return false;
  }
  return true;
}

// EVERY CLASS IS REACHABLE. A class no token carries is a branch of the ladder
// no scan can ever take - the shape of dead rule this project keeps finding.
constexpr bool net_every_token_class_is_populated(void) {
  uint8_t seen = 0;
  for (uint8_t i = 0; i < NET_TOKEN_ROW_COUNT; ++i) seen |= NET_TOKEN_TABLE[i].klass;
  return seen == (uint8_t)NTOK_ALL;
}

// The HOME band is [NET_RSSI_NEAR, 0] and the BUSINESS band is
// [NET_RSSI_MID, NET_RSSI_NEAR). Swap the two constants and BUSINESS is empty
// while HOME swallows everything down to the noise floor.
constexpr bool net_rssi_bands_are_ordered(void) {
  return NET_RSSI_MID < NET_RSSI_NEAR &&
         NET_RSSI_MID >= -127 && NET_RSSI_NEAR <= 0;
}

static_assert(NET_TOKEN_ROW_COUNT >= 1, "the network token table is empty");
static_assert(net_category_bits_are_one_bit_each_and_distinct(),
              "two network categories share a category_mask bit");
static_assert(net_token_table_is_sorted_and_unique(),
              "NET_TOKEN_TABLE is not strictly ascending by hash - the binary search is invalid");
static_assert(net_token_rows_are_well_formed(),
              "a network token row has no class, more than one class, or a dirty reserved byte");
static_assert(net_every_token_class_is_populated(),
              "a network token class has no tokens - the classifier branch that reads it is dead");
static_assert(net_rssi_bands_are_ordered(),
              "NET_RSSI_NEAR / NET_RSSI_MID are swapped or out of int8_t range");

#endif // PB_NETWORK_TABLE_H""")
    return "\n".join(o) + "\n"


def emit_encounter(c):
    bal = c.balance
    out_ord, out_names = enum_from(bal, "ENCOUNTER_OUTCOME_ENUM", "ENC_OUT_")
    bits = bal["NET_CATEGORY_BITS"]
    ords = bal["NET_CATEGORY_ORDINALS"]
    cats = sorted(ords.keys(), key=lambda n: ords[n])

    # The rarity bands the SHIPPED roster actually carries, per category.
    have = {}
    for cn in cats:
        bit = bits[cn]
        have[cn] = sorted({s["rarity"] for s in c.species if s["category_mask"] & bit})

    # CLAMP: a WILD row whose rarity band has no species in the shipped roster
    # would be a row the picker can never resolve. Fold it down to the highest
    # band the category does carry. At the full 60-species roster every band is
    # populated and this is a no-op, which is the point.
    rows = []
    clamped = []
    for r in c.encounters:
        lo, hi = r["rarity_min"], r["rarity_max"]
        if r["outcome"] == "WILD":
            avail = have[r["category"]]
            if not avail:
                die("category %s has no species at all" % r["category"])
            if not [b for b in avail if lo <= b <= hi]:
                nlo = nhi = max(b for b in avail if b <= hi) if [b for b in avail if b <= hi] \
                    else min(avail)
                clamped.append((r["category"], lo, hi, nlo))
                lo, hi = nlo, nhi
        rows.append((r, lo, hi))

    o = [banner("data/encounter_table.h", [
        "THE ENCOUNTER TABLE (plan 1.5.2, spec sections 20 and 22).",
        "",
        "Six network categories x {WILD by rarity band, ITEM, SPECIAL, NOTHING},",
        "each category's weights summing to exactly 100. P5-C3 rolls against it.",
        "",
        "ITEM_DROPS is the table plan 1.5.2 does not list and the ITEM outcome",
        "cannot resolve without: EncounterRow has no item column, so the drop is",
        "picked here, by weight within the network category, then rejected if the",
        "item's rarity falls outside the row's rarity_min..rarity_max.",
        "",
        "SPECIAL HAS NO PAYLOAD TABLE AND THAT IS A REAL HOLE. Spec section 22",
        "names four outcomes and this table has rows for all four, but the pack",
        "carries nothing behind SPECIAL - no event roster, no per-category",
        "weights, no ids - while it is 4 to 10 percent of every scan. P5-C3 must",
        "define it. Recorded here rather than papered over.",
    ] + (["",
          "ROWS CLAMPED FOR THIS ROSTER (%d species): a WILD row whose rarity band"
          % len(c.species),
          "holds no species in the shipped prefix is folded down to the highest",
          "band the category does carry, so every row resolves. The full",
          "60-species roster needs no clamping."] +
         ["  %-8s rarity %d..%d -> %d" % (cn, lo, hi, nlo) for cn, lo, hi, nlo in clamped]
         if clamped else []))]
    o.append("#ifndef PB_ENCOUNTER_TABLE_H\n#define PB_ENCOUNTER_TABLE_H\n")
    o.append('#include <stdint.h>\n#include <stddef.h>\n')
    o.append('#include "network_table.h"\n#include "species_table.h"\n'
             '#include "items_table.h"\n')

    o.append("// NetCategory and NET_CATEGORY_BIT moved to network_table.h at P5-C1: the")
    o.append("// encoding is the classifier's OUTPUT and this table's INDEX, and it now")
    o.append("// has one owner. What stays here is the cross-check against the roster,")
    o.append("// which network_table.h cannot see.")
    o.append("static_assert((uint8_t)NET_CAT_COUNT == NET_CATEGORY_COUNT,")
    o.append('              "NetCategory and SPECIES_SPAWN_SUM disagree on the category count");\n')

    o.append("enum EncounterOutcome : uint8_t {")
    for n in out_names:
        o.append("  %s," % n)
    o.append("  ENC_OUT_COUNT")
    o.append("};\n")

    o.append("""struct EncounterRow {       // 8 B, plan 1.5.2
  uint8_t category;         // NetCategory ORDINAL
  uint8_t outcome;          // EncounterOutcome
  uint8_t weight;           // per-category weights sum to exactly 100
  uint8_t rarity_min;       // WILD: the species rarity band this row draws from
  uint8_t rarity_max;
  uint8_t reserved[3];      // must be 0
};
static_assert(sizeof(EncounterRow) == 8, "EncounterRow layout drifted");
""")
    o.append("inline constexpr EncounterRow ENCOUNTER_TABLE[] = {")
    o.append("  //  category            outcome        wt  rarity")
    for r, lo, hi in rows:
        o.append("  { NET_CAT_%-9s %-17s %3d, %d, %d, { 0, 0, 0 } },"
                 % (r["category"] + ",", out_names[out_ord[r["outcome"]]] + ",",
                    r["weight"], lo, hi))
    o.append("};\n")
    o.append("inline constexpr uint8_t ENCOUNTER_ROW_COUNT =")
    o.append("    (uint8_t)(sizeof(ENCOUNTER_TABLE) / sizeof(ENCOUNTER_TABLE[0]));")
    o.append("#define ENCOUNTER_WEIGHT_TOTAL  100u\n")

    o.append("""struct ItemDropRow {        // 4 B
  uint8_t category;         // NetCategory ORDINAL
  uint8_t item_id;          // into ITEMS_TABLE
  uint8_t weight;           // per-category weights sum to exactly 100
  uint8_t reserved;         // must be 0
};
static_assert(sizeof(ItemDropRow) == 4, "ItemDropRow layout drifted");
""")
    drops = bal["ITEM_DROPS"]
    o.append("inline constexpr ItemDropRow ITEM_DROP_TABLE[] = {")
    for cn in cats:
        for iid in sorted(drops[cn].keys(), key=int):
            o.append("  { NET_CAT_%-9s %3s, %3d, 0 },"
                     % (cn + ",", iid, drops[cn][iid]))
    o.append("};\n")
    o.append("inline constexpr uint8_t ITEM_DROP_ROW_COUNT =")
    o.append("    (uint8_t)(sizeof(ITEM_DROP_TABLE) / sizeof(ITEM_DROP_TABLE[0]));\n")

    o.append("""// --- generator-emitted compile-time guards (plan 1.5.2) ----------------------
constexpr bool encounter_rows_are_well_formed(void) {
  for (uint8_t i = 0; i < (uint8_t)(sizeof(ENCOUNTER_TABLE) / sizeof(ENCOUNTER_TABLE[0])); ++i) {
    const EncounterRow& r = ENCOUNTER_TABLE[i];
    if (r.category >= (uint8_t)NET_CAT_COUNT)      return false;
    if (r.outcome >= (uint8_t)ENC_OUT_COUNT)       return false;
    if (r.weight == 0u)                            return false;
    if (r.rarity_min > r.rarity_max)               return false;
    if (r.rarity_max > SPECIES_RARITY_SPECIAL)     return false;
    if (r.reserved[0] != 0u || r.reserved[1] != 0u || r.reserved[2] != 0u) return false;
  }
  return true;
}

// Every category's weights sum to exactly 100, so a roll is a plain 0..99 draw
// with no normalisation step to get wrong.
constexpr bool encounter_weights_sum_to_100(void) {
  for (uint8_t c = 0; c < (uint8_t)NET_CAT_COUNT; ++c) {
    uint32_t sum = 0;
    for (uint8_t i = 0; i < ENCOUNTER_ROW_COUNT; ++i)
      if (ENCOUNTER_TABLE[i].category == c) sum += ENCOUNTER_TABLE[i].weight;
    if (sum != ENCOUNTER_WEIGHT_TOTAL) return false;
  }
  return true;
}

// Spec section 22: NOTHING is at least 15 % of every scan, so exploring can
// come back empty and the player learns the device is not a slot machine.
#define ENCOUNTER_NOTHING_MIN_PCT  15u
constexpr bool encounter_nothing_is_common_enough(void) {
  for (uint8_t c = 0; c < (uint8_t)NET_CAT_COUNT; ++c) {
    uint32_t sum = 0;
    for (uint8_t i = 0; i < ENCOUNTER_ROW_COUNT; ++i)
      if (ENCOUNTER_TABLE[i].category == c &&
          ENCOUNTER_TABLE[i].outcome == (uint8_t)ENC_OUT_NOTHING)
        sum += ENCOUNTER_TABLE[i].weight;
    if (sum < ENCOUNTER_NOTHING_MIN_PCT) return false;
  }
  return true;
}

// EVERY WILD ROW RESOLVES TO A NON-EMPTY SPECIES POOL. This is the guard the
// generator's rarity clamp exists to keep true: a WILD row whose band holds no
// species in the shipped roster is an outcome the picker cannot answer.
constexpr bool encounter_wild_rows_have_a_pool(void) {
  for (uint8_t i = 0; i < ENCOUNTER_ROW_COUNT; ++i) {
    const EncounterRow& r = ENCOUNTER_TABLE[i];
    if (r.outcome != (uint8_t)ENC_OUT_WILD) continue;
    uint32_t weight = 0;
    for (uint8_t s = 0; s < SPECIES_TABLE_COUNT; ++s) {
      const SpeciesDef& sp = SPECIES_TABLE[s];
      if ((sp.category_mask & NET_CATEGORY_BIT[r.category]) == 0u) continue;
      if (sp.rarity < r.rarity_min || sp.rarity > r.rarity_max) continue;
      weight += sp.spawn_weight;
    }
    if (weight == 0u) return false;
  }
  return true;
}

constexpr bool item_drop_rows_are_well_formed(void) {
  for (uint8_t i = 0; i < (uint8_t)(sizeof(ITEM_DROP_TABLE) / sizeof(ITEM_DROP_TABLE[0])); ++i) {
    const ItemDropRow& d = ITEM_DROP_TABLE[i];
    if (d.category >= (uint8_t)NET_CAT_COUNT) return false;
    if (d.item_id < 1u || d.item_id > ITEM_COUNT) return false;
    if (d.weight == 0u) return false;
    if (d.reserved != 0u) return false;
  }
  for (uint8_t c = 0; c < (uint8_t)NET_CAT_COUNT; ++c) {
    uint32_t sum = 0;
    for (uint8_t i = 0; i < ITEM_DROP_ROW_COUNT; ++i)
      if (ITEM_DROP_TABLE[i].category == c) sum += ITEM_DROP_TABLE[i].weight;
    if (sum != ENCOUNTER_WEIGHT_TOTAL) return false;
  }
  return true;
}

static_assert(ENCOUNTER_ROW_COUNT >= 1, "the encounter table is empty");
static_assert(encounter_rows_are_well_formed(),
              "an encounter row has a bad category, outcome, weight or rarity band");
static_assert(encounter_weights_sum_to_100(),
              "a network category's encounter weights do not sum to 100");
static_assert(encounter_nothing_is_common_enough(),
              "a network category finds something too often (spec 22: NOTHING >= 15 %)");
static_assert(encounter_wild_rows_have_a_pool(),
              "a WILD encounter row draws from a rarity band no shipped species is in");
static_assert(item_drop_rows_are_well_formed(),
              "an item drop row has a bad category or item, or a category does not sum to 100");

#endif // PB_ENCOUNTER_TABLE_H""")
    return "\n".join(o) + "\n"


def emit_creator_schema(c):
    bal = c.balance
    o = [banner("data/creator_schema.h", [
        "THE CREATOR VALIDATION SCHEMA (spec sections 35 and 36, plan line 526).",
        "",
        "One copy of every budget a custom Pebble is measured against, so the",
        "on-device validator (P8) and the phone page read the same numbers.",
        "",
        "WHAT IS HERE IS WHAT THE CONTENT PACK CARRIES, AND IT IS NOT ALL OF",
        "SPEC SECTION 35. Section 35 names thirteen validation inputs, and all",
        "thirteen are accounted for below - 2 + 1 + 2 + 8 - because a header",
        "whose job is to say what has no row has to add up.",
        "",
        "  TWO HAVE A ROW HERE: the stat budget and the four-move rule, plus",
        "  the section 36 budget arithmetic they are measured with.",
        "",
        "  ONE THIS HEADER CAN STATE: species / custom definition validity, as",
        "  the custom species id RANGE, because species_table.h already fixes",
        "  SPECIES_ID_BUILTIN_MAX.",
        "",
        "  TWO ARE DERIVABLE FROM TABLES THAT ALREADY SHIP, so P8 does not need",
        "  a row and must not invent one: TYPE VALIDITY is `type < TYPE_COUNT`",
        "  (data/attacks_table.h), and MOVE LEGALITY is the own-type-or-NEUTRAL,",
        "  four-distinct, at-least-one-damaging rule that",
        "  attacks_table.h::species_learnsets_are_legal() already holds every",
        "  built-in row to.",
        "",
        "  EIGHT HAVE NO ROW ANYWHERE and P8 cannot invent them from this",
        "  header. They are listed rather than guessed:",
        "    sprite dimensions and sprite data size",
        "    palette limits",
        "    name length and the allowed character set",
        "    creator payload size",
        "    the creator protocol version",
        "    whether a custom Pebble may carry an evolution rule at all",
    ])]
    o.append("#ifndef PB_CREATOR_SCHEMA_H\n#define PB_CREATOR_SCHEMA_H\n")
    o.append('#include <stdint.h>\n')
    o.append('#include "species_table.h"\n#include "attacks_table.h"\n')
    o.append("// Custom species ids: cs0..cs9 live directly above the built-in roster.")
    o.append("#define CREATOR_SPECIES_ID_MIN   (SPECIES_ID_BUILTIN_MAX + 1u)")
    o.append("#define CREATOR_SPECIES_SLOTS    10u")
    o.append("#define CREATOR_SPECIES_ID_MAX   (SPECIES_ID_BUILTIN_MAX + CREATOR_SPECIES_SLOTS)\n")
    o.append("// Spec section 36: a custom Pebble is capped at the STAGE-1 budget so it can")
    o.append("// never out-stat a final evolution (spec section 68 r17).")
    o.append("#define CREATOR_TOTAL_STAT_POINTS  %d" % bal["CREATOR_TOTAL_STAT_POINTS"])
    o.append("#define CREATOR_ATTACK_BUDGET      %d" % bal["CREATOR_ATTACK_BUDGET"])
    o.append("#define CREATOR_MOVE_COUNT         PB_MOVE_COUNT")
    o.append("#define CREATOR_BASE_STAT_MIN      %d" % bal["BASE_STAT_MIN"])
    o.append("#define CREATOR_BASE_STAT_MAX      %d\n" % bal["BASE_STAT_MAX"])
    o.append("// The built-in budgets a custom Pebble is measured against, by stage.")
    o.append("inline constexpr uint8_t CREATOR_STAT_POINTS_BY_STAGE[3] = { %s };"
             % ", ".join(str(v) for v in bal["TOTAL_STAT_POINTS_BY_STAGE"]))
    o.append("inline constexpr uint16_t CREATOR_ATTACK_BUDGET_BY_STAGE[3] = { %s };"
             % ", ".join(str(v) for v in bal["ATTACK_BUDGET_BY_STAGE"]))
    o.append("inline constexpr uint8_t CREATOR_POWER_CAP_BY_STAGE[3] = { %s };"
             % ", ".join(str(v) for v in bal["POWER_CAP_BY_STAGE"]))
    o.append("inline constexpr uint8_t CREATOR_RARITY_BUDGET_BONUS[SPECIES_RARITY_COUNT] = { %s };\n"
             % ", ".join(str(v) for v in bal["RARITY_BUDGET_BONUS"]))
    o.append("""// A custom Pebble may never be stronger than a stage-1 built-in, and never
// weaker than a stage-0 one. Both halves matter: the first is spec 68 r17, the
// second stops the creator being a way to make deliberately useless trade bait.
static_assert(CREATOR_TOTAL_STAT_POINTS == CREATOR_STAT_POINTS_BY_STAGE[1],
              "the creator stat budget is not the stage-1 budget");
static_assert(CREATOR_ATTACK_BUDGET == CREATOR_ATTACK_BUDGET_BY_STAGE[1],
              "the creator attack budget is not the stage-1 budget");
static_assert(CREATOR_TOTAL_STAT_POINTS >= CREATOR_STAT_POINTS_BY_STAGE[0],
              "the creator stat budget is below a stage-0 built-in");
static_assert(CREATOR_TOTAL_STAT_POINTS < CREATOR_STAT_POINTS_BY_STAGE[2],
              "a custom Pebble could out-stat a final evolution (spec 68 r17)");
static_assert(CREATOR_BASE_STAT_MIN >= 1 && CREATOR_BASE_STAT_MAX <= 10,
              "spec section 11 fixes base stats at 1..10");
static_assert(CREATOR_SPECIES_ID_MAX <= 255u,
              "custom species ids must fit PebbleInstance.species_id");
static_assert(CREATOR_MOVE_COUNT == 4u, "spec section 13: exactly four attacks");

// Every built-in row is inside the budgets the creator is held to for its own
// stage. A roster that breaks its own rules cannot be used to judge a player's.
constexpr bool builtin_rows_respect_the_creator_budgets(void) {
  for (uint8_t i = 0; i < SPECIES_TABLE_COUNT; ++i) {
    const SpeciesDef& sp = SPECIES_TABLE[i];
    // Bounds first, and not because they are expected to fire: species_table.h
    // and attacks_table.h already reject a bad stage, rarity or move id by
    // name. But this function INDEXES all three, and a constexpr read past an
    // array end is not a false return - it is "non-constant condition for
    // static assertion", an error with no sentence in it. If one of those named
    // guards is ever deleted, the failure should still say what broke.
    if (sp.stage >= 3u)                       return false;
    if (sp.rarity >= (uint8_t)SPECIES_RARITY_COUNT) return false;
    for (uint8_t m = 0; m < (uint8_t)PB_MOVE_COUNT; ++m) {
      if (sp.moves[m] < 1u || sp.moves[m] > ATTACK_COUNT) return false;
    }
    const uint16_t total = (uint16_t)sp.base_hp + sp.base_atk + sp.base_def + sp.base_spd;
    if (total != CREATOR_STAT_POINTS_BY_STAGE[sp.stage]) return false;
    if (sp.base_hp  < CREATOR_BASE_STAT_MIN || sp.base_hp  > CREATOR_BASE_STAT_MAX) return false;
    if (sp.base_atk < CREATOR_BASE_STAT_MIN || sp.base_atk > CREATOR_BASE_STAT_MAX) return false;
    if (sp.base_def < CREATOR_BASE_STAT_MIN || sp.base_def > CREATOR_BASE_STAT_MAX) return false;
    if (sp.base_spd < CREATOR_BASE_STAT_MIN || sp.base_spd > CREATOR_BASE_STAT_MAX) return false;

    uint16_t cost = 0;
    uint8_t  cap  = 0;
    for (uint8_t m = 0; m < (uint8_t)PB_MOVE_COUNT; ++m) {
      const AttackDef& a = ATTACKS_TABLE[sp.moves[m] - 1u];
      cost += a.budget_cost;
      if (a.power > cap) cap = a.power;
    }
    if (cost > (uint16_t)(CREATOR_ATTACK_BUDGET_BY_STAGE[sp.stage] +
                          CREATOR_RARITY_BUDGET_BONUS[sp.rarity])) return false;
    if (cap > CREATOR_POWER_CAP_BY_STAGE[sp.stage]) return false;
  }
  return true;
}
static_assert(builtin_rows_respect_the_creator_budgets(),
              "a built-in species breaks the stat total, the attack budget or the "
              "power cap its own stage is held to (or its stage, rarity or a move "
              "id is out of range, which the tables themselves name first)");

#endif // PB_CREATOR_SCHEMA_H""")
    return "\n".join(o) + "\n"


def emit_content_version(c, h):
    o = [banner("data/content_version.h", [
        "CONTENT_VERSION - a hash of the content, not a number somebody bumps.",
        "",
        "PebbleInstance and the save blobs record the CONTENT_VERSION they were",
        "written against (persistence/save_schema.h), so a save can say which",
        "roster it means. Plan line 668 requires that this number change when",
        "the JSON changes, and a hand-maintained counter does not.",
        "",
        "THE HASH INPUT SET, stated so the next reader does not have to guess:",
        "  * the six tools/content/*.json files, in a fixed order;",
        "  * every key EXCEPT the `_`-prefixed ones. Those are design and art",
        "    notes; folding them in would mean a typo fix in a comment marks",
        "    every save on every device as made against foreign content;",
        "  * the emitted roster size, because two builds shipping different",
        "    numbers of species ARE different content from identical JSON.",
        "FNV-1a 32 over that canonical UTF-8 text, folded with h ^ (h >> 16) and",
        "masked to 16 bits, with 0 remapped to 1 so the value is never the one a",
        "zeroed blob would carry.",
    ])]
    o.append("#ifndef PB_CONTENT_VERSION_H\n#define PB_CONTENT_VERSION_H\n")
    o.append("// %d species (%d families), %d attacks, %d items, %d evolution rules, %d encounter rows"
             % (len(c.species), c.families, len(c.attacks), len(c.items),
                len(c.evo), len(c.encounters)))
    o.append("#define CONTENT_VERSION  0x%04Xu\n" % h)
    o.append("#endif // PB_CONTENT_VERSION_H")
    return "\n".join(o) + "\n"


# =============================================================================
#  THE STRING BLOCK INSIDE core/strings_es.h
#
#  The pack's STRING_CONTRACT asks for four CONTIGUOUS blocks (60 names, then
#  60 flavors, then attack names, then item names). This generator emits the
#  species pairs INTERLEAVED instead, and that is deliberate: strings_es.h
#  already ships STR_SPC_NAME_1 / STR_SPC_FLAV_1 / STR_SPC_NAME_2 / ... at the
#  very end of the enum, and P4-C1 obligation 1 requires species 1's row to come
#  out BYTE-IDENTICAL - including its flavor_idx. A contiguous layout would move
#  flavor_idx from base+1 to base+36. Interleaving keeps ids 1..3 at exactly the
#  StrId values they have today and still lets the block grow by appending.
#
#      SpeciesDef.name_idx   = STR_SPC_NAME_<id>   (= base + 2*(id-1))
#      SpeciesDef.flavor_idx = STR_SPC_FLAV_<id>   (= base + 2*(id-1) + 1)
#      AttackDef.name_idx    = STR_ATK_NAME_<id>
#      ItemDef.name_idx      = STR_ITEM_NAME_<id>
# =============================================================================
def string_enum_block(c):
    o = []
    o.append("  " + GEN_ENUM_BEGIN)
    o.append("  //     ONE PAIR PER SPECIES ID, in roster order, then one name per attack")
    o.append("  //     and one per item. SpeciesDef.name_idx / .flavor_idx, AttackDef.name_idx")
    o.append("  //     and ItemDef.name_idx hold these StrIds directly.")
    o.append("  //     Appended at the END of the enum on purpose: every id above keeps the")
    o.append("  //     value it already had, and ids 1..3 keep the values P3-C3 froze.")
    for s in c.species:
        o.append("  STR_SPC_NAME_%d,%s// species %d, %s"
                 % (s["id"], " " * max(1, 8 - len(str(s["id"]))), s["id"], s["name"]))
        o.append("  STR_SPC_FLAV_%d," % s["id"])
    o.append("")
    o.append("  //     Attack names, data/attacks_table.h --- <= 10 chars @ t0_11b ----------")
    for a in c.attacks:
        o.append("  STR_ATK_NAME_%d,%s// attack %d, %s"
                 % (a["id"], " " * max(1, 7 - len(str(a["id"]))), a["id"], a["name"]))
    o.append("")
    o.append("  //     Item names, data/items_table.h --------------------------------------")
    for it in c.items:
        o.append("  STR_ITEM_NAME_%d,%s// item %d, %s"
                 % (it["id"], " " * max(1, 6 - len(str(it["id"]))), it["id"], it["name"]))
    o.append("  " + GEN_ENUM_END)
    return "\n".join(o)


def string_table_block(c):
    o = []
    o.append("  " + GEN_TAB_BEGIN)
    for s in c.species:
        o.append('  /* STR_SPC_NAME_%-3d */          "%s",'
                 % (s["id"], c_str_literal(s["name"], "species %d name" % s["id"])))
        o.append('  /* STR_SPC_FLAV_%-3d */          "%s",'
                 % (s["id"], c_str_literal(s["flavor_es"],
                                           "species %d flavor_es" % s["id"])))
    for a in c.attacks:
        o.append('  /* STR_ATK_NAME_%-3d */          "%s",'
                 % (a["id"], c_str_literal(a["name"], "attack %d name" % a["id"])))
    for i, it in enumerate(c.items):
        comma = "," if i + 1 < len(c.items) else ""
        o.append('  /* STR_ITEM_NAME_%-3d */         "%s"%s'
                 % (it["id"], c_str_literal(it["name"], "item %d name" % it["id"]),
                    comma))
    o.append("  " + GEN_TAB_END)
    return "\n".join(o)


def splice(text, begin_marker, end_marker, body, what):
    lines = text.split("\n")
    b = e = -1
    for i, ln in enumerate(lines):
        if begin_marker in ln and b < 0:
            b = i
        elif end_marker in ln and b >= 0 and e < 0:
            e = i
    if b < 0 or e < 0:
        die("could not find the generated %s markers in strings_es.h" % what)
    return "\n".join(lines[:b] + body.split("\n") + lines[e + 1:])


def emit_strings(c, current):
    t = splice(current, GEN_ENUM_BEGIN, GEN_ENUM_END, string_enum_block(c), "StrId")
    t = splice(t, GEN_TAB_BEGIN, GEN_TAB_END, string_table_block(c), "ES[]")
    return t


# =============================================================================
#  DRIVER
# =============================================================================
def tidy(text):
    """No trailing whitespace, exactly one terminating newline. Applied to every
    emitted file so a padded column never leaves invisible bytes behind."""
    return "\n".join(ln.rstrip() for ln in text.split("\n")).rstrip("\n") + "\n"


def build_all(families):
    c = Content(families)
    h = content_hash(c)
    files = {
        os.path.join(DATA, "content_version.h"): emit_content_version(c, h),
        os.path.join(DATA, "species_table.h"): emit_species(c),
        os.path.join(DATA, "attacks_table.h"): emit_attacks(c),
        os.path.join(DATA, "items_table.h"): emit_items(c),
        os.path.join(DATA, "evolution_table.h"): emit_evolution(c),
        os.path.join(DATA, "network_table.h"): emit_networks(c),
        os.path.join(DATA, "encounter_table.h"): emit_encounter(c),
        os.path.join(DATA, "creator_schema.h"): emit_creator_schema(c),
    }
    with open(STRINGS_H, encoding="utf-8") as f:
        files[STRINGS_H] = emit_strings(c, f.read())
    return c, h, {k: tidy(v) for k, v in files.items()}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true",
                    help="regenerate in memory and diff against the tree")
    ap.add_argument("--families", type=int, default=ROSTER_FAMILIES,
                    help="roster size in families of 3 (default %d)" % ROSTER_FAMILIES)
    args = ap.parse_args()

    c, h, files = build_all(args.families)

    drift = []
    for path, text in sorted(files.items()):
        old = None
        if os.path.exists(path):
            with open(path, encoding="utf-8") as f:
                old = f.read()
        if old == text:
            continue
        drift.append(path)
        if not args.check:
            with open(path, "w", encoding="utf-8") as f:
                f.write(text)

    rel = lambda p: os.path.relpath(p, ROOT)
    if args.check:
        if drift:
            sys.stderr.write("gen_content.py --check: %d file(s) differ from the JSON:\n"
                             % len(drift))
            for p in drift:
                sys.stderr.write("    %s\n" % rel(p))
            sys.stderr.write("run: python3 tools/gen_content.py\n")
            return 1
        print("gen_content.py --check: %d files in sync, CONTENT_VERSION 0x%04X"
              % (len(files), h))
        return 0

    print("gen_content.py: %d species (%d families), %d attacks, %d items, "
          "%d evolution rules, %d encounter rows, %d item drops, %d network tokens"
          % (len(c.species), c.families, len(c.attacks), len(c.items),
             len(c.evo), len(c.encounters),
             sum(len(v) for v in c.balance["ITEM_DROPS"].values()),
             sum(len(v) for v in c.networks["TOKENS"].values())))
    print("gen_content.py: CONTENT_VERSION 0x%04X" % h)
    if drift:
        for p in sorted(drift):
            print("  wrote %s" % rel(p))
    else:
        print("  (no change)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
