#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
balance_report.py - WHAT THE ROSTER LOOKS LIKE. A REPORT, NOT A GATE.

    cd tools/content && python3 balance_report.py            # the whole report
    python3 balance_report.py --section stats                # one section
    python3 balance_report.py --json                         # machine-readable
    python3 balance_report.py --diff before.json             # what moved

IT IS DELIBERATELY NOT IN tools/check.sh, AND SAYING WHY IS THE POINT.

verify.py already ASSERTS the tight versions of everything below: base-stat
totals are exactly 16 / 22 / 28 with zero variance (verify.py:118), every kit is
inside ATTACK_BUDGET_BY_STAGE (:194), the type split is exactly 20/20/20 (:109),
rarity is exactly 30/18/9/3. So a "gate" built out of these numbers would assert
what is already asserted, in a looser form, and add a second place to update
when a rule changes. There is no defect it would catch.

WHAT IT IS FOR is the thing assertions cannot do: showing a REVIEWER which
species moved. P9-C2 retunes twenty families. Every assertion in the tree can
stay green through a change that quietly makes every CORRUPT stage-2 the
strongest thing in the game, because "inside the budget" and "balanced" are not
the same statement. The report is the artefact that change is read against:

    python3 balance_report.py --json > /tmp/before.json
    ...edit species.json, attacks.json...
    python3 balance_report.py --diff /tmp/before.json

prints only what moved, per type and per stage, so a reviewer sees the shape of
an edit instead of a 60-row diff of integers.

WHAT IT CANNOT TELL YOU, stated so nobody quotes it as evidence of balance: it
reports the TABLES. It never simulates a battle. Whether a kit wins is
P9-C4's balance_matrix (AI vs AI over the shipped engine); the win rates in
verify.py section 11 are over tools/content/sim_engine.py, which game/battle.h
lines 90-116 lists as diverging from the shipped engine in five ways. A species
with a strong-looking budget and only status moves reads well here and loses
every fight there.
"""

import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def load(name):
    with open(os.path.join(HERE, name), encoding="utf-8") as f:
        return json.load(f)


SPECIES = load("species.json")
ATTACKS = load("attacks.json")
ITEMS = load("items.json")
BALANCE = load("balance.json")
EVOLUTION = load("evolution.json")

ATK = {a["id"]: a for a in ATTACKS}
TYPES = BALANCE.get("TYPES", ["SIGNAL", "CORRUPT", "SYSTEM"])
STAGE_NAME = ["stage 0 (base)", "stage 1", "stage 2 (final)"]
RARITY_NAME = ["COMMON", "UNCOMMON", "RARE", "SPECIAL"]


def mean(xs):
    return (sum(xs) / float(len(xs))) if xs else 0.0


def budget_of(s):
    """A species' move budget: the cost of the four moves it ships with.
    Same sum tests/test_content.cpp and data/creator_schema.h charge a creator
    for, so a built-in row and an uploaded one are measured on one scale."""
    return sum(ATK[m]["budget_cost"] for m in s["moves"] if m in ATK)


def cap_of(s):
    caps = BALANCE.get("ATTACK_BUDGET_BY_STAGE", [])
    bonus = BALANCE.get("RARITY_BUDGET_BONUS", [0, 0, 0, 0])
    if not caps:
        return None
    return caps[s["stage"]] + bonus[s["rarity"]]


# =============================================================================
#  SECTIONS
# =============================================================================
def section_stats(out):
    """Per-type averages, split by stage. THE HEADLINE NUMBER OF THE REPORT:
    if one type is stronger than another at a stage, this is where it shows,
    and verify.py's exact-total assertions cannot see it because they check the
    TOTAL, not how it is spent."""
    out("== BASE STATS, per type and stage (mean of hp / atk / def / spd)")
    out("%-9s %-15s %4s %6s %6s %6s %6s %7s"
        % ("type", "stage", "n", "hp", "atk", "def", "spd", "total"))
    for t in TYPES:
        for st in range(3):
            rows = [s for s in SPECIES if s["type"] == t and s["stage"] == st]
            if not rows:
                continue
            out("%-9s %-15s %4d %6.2f %6.2f %6.2f %6.2f %7.2f"
                % (t, STAGE_NAME[st], len(rows),
                   mean([s["base_hp"] for s in rows]),
                   mean([s["base_atk"] for s in rows]),
                   mean([s["base_def"] for s in rows]),
                   mean([s["base_spd"] for s in rows]),
                   mean([s["base_hp"] + s["base_atk"] + s["base_def"]
                         + s["base_spd"] for s in rows])))
    out("")
    out("   The per-stage TOTAL is asserted exact (16 / 22 / 28, zero variance)")
    out("   by verify.py. What this table shows is how each type SPENDS it.")
    out("")


def section_spread(out):
    """The extremes. An average hides the species that is 2 points of ATK away
    from everything else at its stage, and that species is the one a player
    notices."""
    out("== THE EXTREMES, per stage (what an average hides)")
    for st in range(3):
        rows = [s for s in SPECIES if s["stage"] == st]
        if not rows:
            continue
        out("   %s" % STAGE_NAME[st])
        for key in ("base_hp", "base_atk", "base_def", "base_spd"):
            lo = min(rows, key=lambda s: s[key])
            hi = max(rows, key=lambda s: s[key])
            out("     %-8s low %2d %-10s   high %2d %-10s   spread %d"
                % (key[5:], lo[key], lo["name"], hi[key], hi["name"],
                   hi[key] - lo[key]))
    out("")


def section_budget(out):
    """THE BUDGET HISTOGRAM. How much of its allowance each species actually
    spends. A roster where everything sits at 100 % has no headroom left for a
    retune; one where everything sits at 60 % is paying for a cap nothing uses."""
    out("== MOVE BUDGET, spent against the cap (histogram of %-of-cap)")
    buckets = {}
    worst = []
    for s in SPECIES:
        cap = cap_of(s)
        if not cap:
            continue
        pct = 100.0 * budget_of(s) / cap
        b = int(pct // 10) * 10
        buckets[b] = buckets.get(b, 0) + 1
        worst.append((pct, s))
    for b in sorted(buckets):
        out("   %3d-%3d%%  %-40s %d"
            % (b, b + 9, "#" * buckets[b], buckets[b]))
    out("")
    worst.sort(key=lambda p: -p[0])
    out("   fullest kits:")
    for pct, s in worst[:5]:
        out("     %-10s stage %d %-8s %3d / %3d  (%.0f%%)"
            % (s["name"], s["stage"], s["type"], budget_of(s), cap_of(s), pct))
    out("   emptiest kits:")
    for pct, s in worst[-5:]:
        out("     %-10s stage %d %-8s %3d / %3d  (%.0f%%)"
            % (s["name"], s["stage"], s["type"], budget_of(s), cap_of(s), pct))
    over = [s for s in SPECIES if cap_of(s) and budget_of(s) > cap_of(s)]
    if over:
        out("   OVER CAP (verify.py and test_content.cpp both fail on these): %s"
            % ", ".join(s["name"] for s in over))
    out("")


def section_types(out):
    out("== TYPE AND RARITY COUNTS")
    for t in TYPES:
        rows = [s for s in SPECIES if s["type"] == t]
        out("   %-8s %2d species, %2d families"
            % (t, len(rows), len(set(s["family"] for s in rows))))
    out("")
    for r in range(4):
        rows = [s for s in SPECIES if s["rarity"] == r]
        out("   %-9s %2d species, spawn weight %d"
            % (RARITY_NAME[r], len(rows), sum(s["spawn_weight"] for s in rows)))
    out("")


def section_moves(out):
    """Which attacks the roster actually uses. An attack on no learnset is
    content nobody will ever see - P4-C1 obligation 2 exists because two of
    them shipped that way."""
    out("== ATTACK USAGE (how many learnsets each attack is on)")
    used = {a["id"]: 0 for a in ATTACKS}
    for s in SPECIES:
        for m in s["moves"]:
            if m in used:
                used[m] += 1
    orphans = [ATK[i]["name"] for i in sorted(used) if used[i] == 0]
    hot = sorted(used.items(), key=lambda kv: -kv[1])[:6]
    for i, n in hot:
        out("   %-12s %-8s power %3d  on %2d learnsets"
            % (ATK[i]["name"], ATK[i]["type"], ATK[i]["power"], n))
    out("   on NO learnset: %s" % (", ".join(orphans) if orphans else "(none)"))
    out("")


def section_evolution(out):
    out("== EVOLUTION")
    conds = {}
    for r in EVOLUTION:
        conds[r.get("cond", "NONE")] = conds.get(r.get("cond", "NONE"), 0) + 1
    for k in sorted(conds):
        out("   %-14s %d rule(s)" % (k, conds[k]))
    lv = [r["level"] for r in EVOLUTION]
    if lv:
        out("   levels %d..%d, mean %.1f" % (min(lv), max(lv), mean(lv)))
    out("")


SECTIONS = {
    "stats": section_stats,
    "spread": section_spread,
    "budget": section_budget,
    "types": section_types,
    "moves": section_moves,
    "evolution": section_evolution,
}


# =============================================================================
#  MACHINE-READABLE, AND THE DIFF THE REPORT EXISTS FOR
# =============================================================================
def as_json():
    rows = {}
    for s in SPECIES:
        rows[s["name"]] = {
            "id": s["id"], "family": s["family"], "stage": s["stage"],
            "type": s["type"], "rarity": s["rarity"],
            "hp": s["base_hp"], "atk": s["base_atk"],
            "def": s["base_def"], "spd": s["base_spd"],
            "budget": budget_of(s), "cap": cap_of(s),
            "moves": list(s["moves"]),
        }
    return {"species": rows, "count": len(SPECIES)}


def do_diff(path, out):
    with open(path, encoding="utf-8") as f:
        before = json.load(f)
    now = as_json()
    b, n = before["species"], now["species"]
    gone = sorted(set(b) - set(n))
    new = sorted(set(n) - set(b))
    moved = 0
    out("== WHAT MOVED since %s" % path)
    for name in sorted(set(b) & set(n)):
        d = [(k, b[name][k], n[name][k]) for k in n[name]
             if b[name].get(k) != n[name][k]]
        if d:
            moved += 1
            out("   %-10s %s" % (name, ", ".join(
                "%s %s -> %s" % (k, x, y) for k, x, y in d)))
    if new:
        out("   ADDED:   %s" % ", ".join(new))
    if gone:
        out("   REMOVED: %s" % ", ".join(gone))
    if not (moved or new or gone):
        out("   nothing")
    out("")
    out("   %d species changed, %d added, %d removed" % (moved, len(new), len(gone)))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--section", choices=sorted(SECTIONS),
                    help="print one section only")
    ap.add_argument("--json", action="store_true",
                    help="machine-readable snapshot, for --diff")
    ap.add_argument("--diff", metavar="BEFORE.json",
                    help="print what moved since a --json snapshot")
    args = ap.parse_args()

    if args.json:
        json.dump(as_json(), sys.stdout, indent=1, sort_keys=True)
        sys.stdout.write("\n")
        return 0

    lines = []
    out = lines.append
    if args.diff:
        rc = do_diff(args.diff, out)
    else:
        out("ERRATA CONTENT BALANCE REPORT - %d species, %d attacks, %d items"
            % (len(SPECIES), len(ATTACKS), len(ITEMS)))
        out("(a report over tools/content/*.json, NOT a gate and NOT a "
            "simulation - see the module docstring)")
        out("")
        for name in ("types", "stats", "spread", "budget", "moves", "evolution"):
            if args.section and args.section != name:
                continue
            SECTIONS[name](out)
        rc = 0
    sys.stdout.write("\n".join(lines) + "\n")
    return rc


if __name__ == "__main__":
    sys.exit(main())
