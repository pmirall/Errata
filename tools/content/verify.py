# -*- coding: utf-8 -*-
"""verify.py - re-checks every DESIGN RULE against the SHIPPED JSON only.

Imports nothing from build_final.py, so a green run proves the deliverable
consistent even if the generator is wrong (method grafted from draft_B-balance's
verify_json.py). sim_engine.py is a JSON-only reader, not part of the generator.

    python3 verify.py           # full run, ~6 min (simulation dominates)
    python3 verify.py --fast    # tallies only, no simulation
"""
import json, os, re, sys, collections, statistics, random, itertools, unicodedata
import sim_engine as SE

HERE = os.path.dirname(os.path.abspath(__file__))
FAST = "--fast" in sys.argv
OUT, FAIL = [], []

def P(s=""): OUT.append(s); print(s, flush=True)
def check(name, cond, detail=""):
    P("  [%s] %-58s %s" % ("PASS" if cond else "FAIL", name, detail))
    if not cond: FAIL.append(name)
    return cond

J = lambda n: json.load(open(os.path.join(HERE, n), encoding="utf-8"))


# --- THE ONE THING THIS FILE READS THAT IS NOT JSON -------------------------
# THE XP CURVE. balance.json used to carry a SECOND 31-entry XP_TABLE - a
# different curve from the one the firmware compiles, 36,453 points against
# 8,845 - and P9-C4 deleted it, because two curves in two files is a second
# source of truth and no amount of checking either one catches the pair
# disagreeing. The curve now lives in exactly one place, Pebblebol/src/data/
# balance.h, and the checks that were about it read it FROM THERE.
#
# This does not weaken the docstring's claim. That claim is about the
# GENERATOR: nothing here imports gen_content.py, so a green run still proves
# the deliverable consistent even if the generator is wrong. balance.h is not
# the generator - it is a hand-maintained firmware header that tools/check.sh's
# balance gate already parses with exactly this kind of read, and it is the
# authority for every number in it.
#
# A MISSING HEADER IS A FAILURE, NOT A SKIP. tools/check.sh says in as many
# words that "a gate that decides not to run must say so", and a curve check
# that silently passes because it could not find the curve is the exact shape
# of defect this project keeps shipping.
_BALANCE_H = os.path.join(HERE, "..", "..", "Pebblebol", "src", "data", "balance.h")


def _shipped_xp_table(path=_BALANCE_H):
    """The 31 entries of XP_TABLE as data/balance.h declares them, or []."""
    try:
        src = open(path, encoding="utf-8").read()
    except OSError:
        return []
    i = src.find("XP_TABLE[XP_LEVEL_MAX + 1] = {")
    if i < 0:
        return []
    j = src.find("};", i)
    if j < 0:
        return []
    body = src[i + len("XP_TABLE[XP_LEVEL_MAX + 1] = {"):j]
    body = re.sub(r"/\*.*?\*/", " ", body, flags=re.S)   # /*  7 */ index labels
    body = re.sub(r"//[^\n]*", " ", body)                 # trailing prose
    out = []
    for tok in body.split(","):
        tok = tok.strip()
        if not tok:
            continue
        if not tok.isdigit():
            return []
        out.append(int(tok))
    return out
SPECIES, ATTACKS, ITEMS = J("species.json"), J("attacks.json"), J("items.json")
EVO, ENC, BAL = J("evolution.json"), J("encounters.json"), J("balance.json")
NET = J("networks.json")
SPEC = J("specials.json")
ATK = {a["id"]: a for a in ATTACKS}
# A species id nothing defines used to reach through this dict and raise
# `KeyError: 5` out of the section-6 cross-check - a traceback, with no RESULT
# line and no named FAILED, for exactly the kind of hand edit a content author
# makes. The sentinel keeps the run going: every check that reads it fails by
# NAME (its evo_rule is -1, its family is -1, its stage is -1), which is what
# the author needs to read.
SP_MISSING = dict((k, -1) for k in SPECIES[0])
SP_MISSING.update({"name": "<no such species>", "flavor_es": "", "type": "?",
                   "moves": [], "_family_name": "?", "_archetype": "?",
                   "_rarity_name": "?"})


class _ById(dict):
    def __missing__(self, key):
        return SP_MISSING


BYID = _ById((s["id"], s) for s in SPECIES)
CATS = ["UNKNOWN", "HOME", "PUBLIC", "BUSINESS", "OPEN", "HIDDEN"]
BIT = BAL["NET_CATEGORY_BITS"]

# =============================================================== 1. STRUCTURE
P("=" * 78); P("1. ROSTER STRUCTURE  (spec 19, 53)"); P("=" * 78)
fam = collections.defaultdict(list)
for s in SPECIES: fam[s["family"]].append(s)
check("60 species", len(SPECIES) == 60, "%d" % len(SPECIES))
check("20 families x exactly 3 stages", len(fam) == 20 and all(
    sorted(x["stage"] for x in v) == [0, 1, 2] for v in fam.values()),
    "%d families" % len(fam))
check("ids contiguous 1..60 and id == index+1",
      all(s["id"] == i + 1 for i, s in enumerate(SPECIES)))
check("sprite_id == id - 1 (flat SPRITE_SETS walk)",
      all(s["sprite_id"] == s["id"] - 1 for s in SPECIES))
check("evo_rule == (family-1)*2 + stage, 255 at stage 2",
      all(s["evo_rule"] == (255 if s["stage"] == 2 else (s["family"] - 1) * 2 + s["stage"])
          for s in SPECIES))
spawnable0 = [s for s in SPECIES if s["stage"] == 0 and s["category_mask"]]
check("every stage-0 spawnable somewhere", len(spawnable0) == 20, "%d/20" % len(spawnable0))

# ================================================================= 2. NAMING
P(); P("=" * 78); P("2. NAMING AND TEXT  (spec 53, 54; strings_es.h width rule)"); P("=" * 78)
POKE = {"pikachu","charmander","bulbasaur","squirtle","eevee","mewtwo","pokemon","pokeball",
        "gym","badge","trainer","evolve stone","hp bar","poke"}
DICT_ES = {"proceso","muralla","silencio","abismo","umbral","rastro","acecho","choque","vacio",
           "ruina","colapso","diluvio","enjambre","colmena","acopio","vertido","rotura",
           "desborde","podrido","memoria","archivo","entropia","fuego","agua","planta"}
names = [s["name"] for s in SPECIES]
check("60 distinct names", len(set(names)) == 60)
check("every name <= 9 chars (5x8 font next to a level)",
      all(len(n) <= 9 for n in names), "max %d (%s)" % (max(len(n) for n in names),
      max(names, key=len)))
check("every flavor_es <= 25 chars", all(len(s["flavor_es"]) <= 25 for s in SPECIES),
      "max %d" % max(len(s["flavor_es"]) for s in SPECIES))
def latin1(x):
    try: x.encode("latin-1"); return True
    except UnicodeEncodeError: return False
allstr = names + [s["flavor_es"] for s in SPECIES] + [a["name"] for a in ATTACKS] + \
         [i["name"] for i in ITEMS]
check("every shipped string is Latin-1 (u8g2_font_5x8_tf)", all(latin1(x) for x in allstr))
naccent = sum(1 for x in allstr if any(ord(c) > 127 for c in x))
check("Spanish layer intact (accents/n-tilde present, not stripped)", naccent >= 15,
      "%d of %d strings carry a non-ASCII glyph" % (naccent, len(allstr)))
check("no dictionary word used as a species name",
      not any(n.lower() in DICT_ES for n in names),
      "checked against %d common nouns" % len(DICT_ES))
check("no Pokemon terminology anywhere",
      not any(w in " ".join(allstr).lower() for w in POKE))
check("every attack name <= 12 chars", all(len(a["name"]) <= 12 for a in ATTACKS),
      "max %d" % max(len(a["name"]) for a in ATTACKS))
check("every item name <= 12 chars", all(len(i["name"]) <= 12 for i in ITEMS))
check("every species carries a silhouette and a frame-2 motion",
      all(s.get("_silhouette") and s.get("_frame2") and s.get("_root") for s in SPECIES))
roots = {s["_family_name"]: s["_root"] for s in SPECIES}
check("20 topologically distinct family roots (R1, never reused)",
      len(set(roots.values())) == 20, "%d distinct" % len(set(roots.values())))

# ================================================================== 3. TYPES
P(); P("=" * 78); P("3. TYPES AND STATS  (spec 11, 12, 19; brief)"); P("=" * 78)
tc = collections.Counter(s["type"] for s in SPECIES)
check("types balanced 20/20/20", all(tc[t] == 20 for t in ("SIGNAL", "CORRUPT", "SYSTEM")), dict(tc))
const = [f for f, v in fam.items() if len({x["type"] for x in v}) == 1]
shift = [f for f, v in fam.items() if len({x["type"] for x in v}) > 1]
check("at least 6 families with a constant type", len(const) >= 6, "%d constant" % len(const))
check("at most 6 families shift type", len(shift) <= 6, "%d shift" % len(shift))
check("every shift happens at stage 2 only",
      all(v[0]["type"] == v[1]["type"] != v[2]["type"]
          for f, v in fam.items() if f in shift))
tot = {0: 16, 1: 22, 2: 28}
check("base-stat totals exactly 16/22/28 (0 variance)",
      all(s["_base_total"] == tot[s["stage"]] for s in SPECIES))
check("every base stat in 1..10",
      all(1 <= s[k] <= 10 for s in SPECIES for k in ("base_hp","base_atk","base_def","base_spd")))
arch = collections.Counter(s["_archetype"] for s in SPECIES)
check("6 distinct archetypes present", len(arch) == 6, dict(arch))
# archetypes must not be a proxy for type
mix = collections.defaultdict(collections.Counter)
for s in SPECIES: mix[s["type"]][s["_archetype"]] += 1
worst = max(max(c.values()) / sum(c.values()) for c in mix.values())
check("archetype decorrelated from type (no type is >45% one archetype)", worst <= 0.45,
      "worst share %.0f%%" % (100 * worst))

# ================================================================ 4. ATTACKS
P(); P("=" * 78); P("4. ATTACKS  (spec 13, 36)"); P("=" * 78)
check("attack ids contiguous from 1", [a["id"] for a in ATTACKS] == list(range(1, len(ATTACKS)+1)),
      "%d attacks" % len(ATTACKS))
atc = collections.Counter(a["type"] for a in ATTACKS)
check("at least 8 attacks per damage type",
      all(atc[t] >= 8 for t in ("SIGNAL", "CORRUPT", "SYSTEM")), dict(atc))
cats = collections.Counter(a["category"] for a in ATTACKS)
check("all six spec 13 categories present",
      set(cats) == set(BAL["ATTACK_CATEGORY_ENUM"]), dict(cats))
check("3-4 RISK moves", 3 <= cats["RISK"] <= 4, "%d" % cats["RISK"])
check("exactly three damage TYPES (NEUTRAL never indexes TYPE_CHART)",
      len({a["type"] for a in ATTACKS} - {"NEUTRAL"}) == 3)
check("no SpeciesDef is NEUTRAL", not any(s["type"] == "NEUTRAL" for s in SPECIES))
ndmg = [a for a in ATTACKS if a["type"] == "NEUTRAL" and a["power"] > 0]
check("typeless DAMAGE exists (the release valve)", len(ndmg) >= 2,
      ", ".join("%s pow %d" % (a["name"], a["power"]) for a in ndmg))
check("every attack maps to a declared ANIM_FILMS id",
      all(a["anim_id"] in {f["id"] for f in BAL["ANIM_FILMS"]} for a in ATTACKS))
check("every attack appears on at least one learnset",
      {m for s in SPECIES for m in s["moves"]} == set(ATK),
      "%d/%d used" % (len({m for s in SPECIES for m in s["moves"]}), len(ATK)))
check("effects used are all in EFFECT_ENUM",
      {a["effect"] for a in ATTACKS} <= set(BAL["EFFECT_ENUM"]))

# budget_cost recomputed from the published formula
def eweight(e, v, d):
    if e in ("BUFF_ATK","BUFF_DEF","BUFF_SPD"): return 16*v + 5*(d-2)
    if e in ("DEBUFF_ATK","DEBUFF_DEF","DEBUFF_SPD"): return 18*v + 5*(d-2)
    if e == "PROTECT_HALF": return 46 + 10*(d-1)
    if e == "HEAL_PCT": return v + 8
    if e == "CLEANSE": return 14
    if e == "DRAIN_PCT": return 2*v//5
    if e == "DOT": return 2*v*d
    if e == "EFF_CORRUPT": return 34
    if e == "SELF_DEBUFF_DEF": return -18*v
    if e == "SELF_STUN": return -22*d
    return 0
def bcost(a):
    c = a["power"]*a["accuracy"]//100
    if a["power"] > 0 and a["priority"] > 0: c += 12*a["priority"]
    c += eweight(a["effect"], a["effect_value"], a["effect_duration"])
    c -= min(12, 4*a["cooldown"])
    if a["effect"] == "RECOIL_PCT": c -= 2*a["effect_value"]//3
    return max(1, c)
bad = [a["name"] for a in ATTACKS if bcost(a) != a["budget_cost"]]
check("budget_cost is COMPUTED, not typed (formula re-derived)", not bad, str(bad[:4]))

# ============================================================== 5. LEARNSETS
P(); P("=" * 78); P("5. LEARNSETS  (brief; spec 13, 35, 36)"); P("=" * 78)
def kit(s): return [ATK[m] for m in s["moves"]]
check("exactly 4 distinct moves each", all(len(set(s["moves"])) == 4 for s in SPECIES))
ill = [s["name"] for s in SPECIES if any(m["type"] not in (s["type"], "NEUTRAL") for m in kit(s))]
check("zero illegal learnset entries (own type or NEUTRAL)", not ill, str(ill[:4]))
check("every kit has a TYPED damage move",
      all(any(m["power"] > 0 and m["type"] != "NEUTRAL" for m in kit(s)) for s in SPECIES))
nv = set(BAL["NEUTRAL_DAMAGE_IDS"])
check("every kit has a NEUTRAL damage move (no all-disadvantaged kit exists)",
      all(nv & set(s["moves"]) for s in SPECIES))
check("every kit has a non-DAMAGE/RISK move",
      all(any(m["category"] not in ("DAMAGE","RISK") for m in kit(s)) for s in SPECIES))
capb, capp = BAL["ATTACK_BUDGET_BY_STAGE"], BAL["POWER_CAP_BY_STAGE"]
over = [s["name"] for s in SPECIES if sum(m["budget_cost"] for m in kit(s)) > capb[s["stage"]]]
check("every kit inside ATTACK_BUDGET_BY_STAGE %s" % capb, not over, str(over[:4]))
overp = [s["name"] for s in SPECIES if any(m["power"] > capp[s["stage"]] for m in kit(s))]
check("every move inside POWER_CAP_BY_STAGE %s" % capp, not overp, str(overp[:4]))
nsets = len({tuple(sorted(s["moves"])) for s in SPECIES})
P("  learnset budget (graft: draft_B-balance histogram)")
for st in (0, 1, 2):
    v = [sum(m["budget_cost"] for m in kit(s)) for s in SPECIES if s["stage"] == st]
    P("    stage %d  min %3d  mean %5.1f  max %3d   cap %3d  (%.0f%% of cap used)"
      % (st, min(v), sum(v)/len(v), max(v), capb[st], 100*sum(v)/len(v)/capb[st]))
allv = [sum(m["budget_cost"] for m in kit(s)) for s in SPECIES]
P("    all      min %3d  mean %5.1f  max %3d   distinct 4-move sets: %d/60"
  % (min(allv), sum(allv)/len(allv), max(allv), nsets))

# ============================================================== 6. EVOLUTION
P(); P("=" * 78); P("6. EVOLUTION  (spec 18, 55; P9-C5)"); P("=" * 78)
check("40 rules = 20 families x 2 steps", len(EVO) == 40)
check("EVOLUTION_RULES[i] is the rule species i points at",
      all(BYID[r["species"]]["evo_rule"] == i for i, r in enumerate(EVO)))
check("every target is stage+1 of the same family",
      all(BYID[r["target"]]["family"] == BYID[r["species"]]["family"] and
          BYID[r["target"]]["stage"] == BYID[r["species"]]["stage"] + 1 for r in EVO))
pat = collections.Counter()
for f, v in fam.items():
    lv = tuple(r["level"] for r in EVO if BYID[r["species"]]["family"] == f)
    pat[lv] += 1
check("evo levels follow the 10->20 pattern with a few 8/18 and 12/24",
      set(pat) <= {(10,20),(8,18),(12,24)} and pat[(10,20)] >= 12, dict(pat))
condfam = {BYID[r["species"]]["_family_name"] for r in EVO if r["cond"] != "NONE"}
check("evo_cond used by <= 5 families", len(condfam) <= 5, sorted(condfam))
corr = [r for r in EVO if r["cond"] == "CORRUPTED"]
check("CORRUPTED unlocks an evolution in 1-2 families (P9-C5)", 1 <= len(corr) <= 2,
      ", ".join("%s -> %s" % (r["_from"], r["_to"]) for r in corr))
check("every cond value is in EVO_COND_ENUM",
      {r["cond"] for r in EVO} <= set(BAL["EVO_COND_ENUM"]))

# ================================================= 7. RARITY / SPAWN / BREED
P(); P("=" * 78); P("7. RARITY, SPAWN, BREEDING  (spec 17, 20, 22)"); P("=" * 78)
rc = collections.Counter(s["rarity"] for s in SPECIES)
check("rarity distribution 30/18/9/3", [rc[i] for i in range(4)] == [30,18,9,3],
      [rc[i] for i in range(4)])
check("spawn_weight in 1..255", all(1 <= s["spawn_weight"] <= 255 for s in SPECIES))
cg = collections.defaultdict(set)
for s in SPECIES: cg[s["compat_group"]].add(s["_family_name"])
check("5 compat groups x 4 families", len(cg) == 5 and all(len(v) == 4 for v in cg.values()),
      {k: len(v) for k, v in sorted(cg.items())})
spread = {}
for g, fams in cg.items():
    spread[g] = len({s["type"] for s in SPECIES if s["compat_group"] == g and s["stage"] == 0})
check("every compat group spans >= 2 types (so cross-family breeding can cross type)",
      all(v >= 2 for v in spread.values()), spread)
check("BREEDING block states the mechanic, not just the number",
      all(k in BAL["BREEDING"] for k in ("offspring_species","move_inheritance",
          "stat_inheritance","GENERATION_MAX","INHERITED_MOVE_SLOTS")))
percat = {c: [s for s in SPECIES if s["category_mask"] & BIT[c]] for c in CATS}
P("  category masks: " + "  ".join("%s %d (s0 %d)" % (c, len(percat[c]),
    sum(1 for s in percat[c] if s["stage"] == 0)) for c in CATS))
check("every category has >= 2 spawnable stage-0 species",
      all(sum(1 for s in percat[c] if s["stage"] == 0) >= 2 for c in CATS))
check("HIDDEN is where the specials live",
      all(s["category_mask"] & BIT["HIDDEN"] for s in SPECIES if s["rarity"] == 3))

# ================================================================= 8. ITEMS
P(); P("=" * 78); P("8. ITEMS  (spec 24)"); P("=" * 78)
check("~10 items", 9 <= len(ITEMS) <= 12, "%d" % len(ITEMS))
check("item ids contiguous from 1", [i["id"] for i in ITEMS] == list(range(1, len(ITEMS)+1)))
kl = collections.Counter(i["klass"] for i in ITEMS)
check("every declared item class is used", set(kl) == set(BAL["ITEM_KLASS_ENUM"]), dict(kl))
# P5-C4: the two units that named no target. These mirror gen_content.py's
# _check_items() on purpose - this file imports nothing from the generator, so a
# green run here proves the deliverable consistent even if the emitter is wrong.
CARE_TGT, BSTAT = BAL["CARE_TARGET_ENUM"], BAL["BATTLE_STAT_ENUM"]
check("every item row carries the target/duration/clears columns",
      all(all(k in i for k in ("target", "duration", "clears")) for i in ITEMS),
      str([i["id"] for i in ITEMS
           if not all(k in i for k in ("target", "duration", "clears"))]))
_care = [i for i in ITEMS if i["klass"] == "CARE"]
check("every CARE item names one of the five care stats (or ALL)",
      all(i["target"] in CARE_TGT for i in _care),
      str([(i["id"], i["target"]) for i in _care if i["target"] not in CARE_TGT]))
check("every CARE value is 1..100 percent of a full bar",
      all(1 <= i["value"] <= 100 for i in _care),
      str([(i["id"], i["value"]) for i in _care if not 1 <= i["value"] <= 100]))
_mod = [i for i in ITEMS if i["klass"] == "BATTLE_MOD"]
check("every BATTLE_MOD names a battle stat and a duration in rounds",
      all(i["target"] in BSTAT and i["duration"] >= 1 for i in _mod),
      str([(i["id"], i["target"], i["duration"]) for i in _mod
           if not (i["target"] in BSTAT and i["duration"] >= 1)]))
check("every BATTLE_MOD value is inside the engine's stage clamp",
      all(1 <= i["value"] <= BAL["BUFF_STAGE_MAX"] for i in _mod))
_other = [i for i in ITEMS if i["klass"] not in ("CARE", "BATTLE_MOD")]
check("a klass with no target vocabulary carries none",
      all(i["target"] == "NONE" and i["duration"] == 0 and not i["clears"]
          for i in _other),
      str([i["id"] for i in _other if i["target"] != "NONE" or i["duration"]
           or i["clears"]]))
check("no item sets both duration and clears (they are one emitted byte)",
      all(not (i["duration"] and i["clears"]) for i in ITEMS))
check("every cleared status is a real PEBBLE_STATUS_BITS name",
      all(b in BAL["PEBBLE_STATUS_BITS"] for i in ITEMS for b in i["clears"]))
check("no item is reachable and does nothing (spec 24)",
      all(i["value"] > 0 or i["klass"] == "EVOLUTION" for i in ITEMS),
      str([i["id"] for i in ITEMS if i["value"] == 0 and i["klass"] != "EVOLUTION"]))
check("every EVOLUTION item is the cond_value of a real ITEM rule",
      all(any(r["cond"] == "ITEM" and r["cond_value"] == i["id"] for r in EVO)
          for i in ITEMS if i["klass"] == "EVOLUTION"),
      str([i["id"] for i in ITEMS if i["klass"] == "EVOLUTION"
           and not any(r["cond"] == "ITEM" and r["cond_value"] == i["id"] for r in EVO)]))
check("every ItemDef.value fits the uint8_t in the 8 B struct",
      all(0 <= i["value"] <= 255 for i in ITEMS),
      "max %d" % max(i["value"] for i in ITEMS))
# THE FIVE CORRUPTION KEYS ARE CHECKED ONE AT A TIME AND HERE, BEFORE ANYTHING
# DEREFERENCES ONE (P9-C6). They used to be one aggregate `all(k in ...)` mask
# 130 lines below - which is the shape the phase-8 review was caught with: four
# of the five failed under a single name that did not say which key had gone.
# The fifth, cleared_by_item, never reached the mask at all: the line under this
# comment dereferenced it first and died with an unhandled KeyError, printing no
# RESULT line and no named FAILED - "exactly the kind of hand edit a content
# author" makes, in this file's own words.
for _k in ("sprite_glitch", "idle_anim_set", "cleared_by_item", "evo_families",
           "battle_infect_permille"):
    check("balance.json CORRUPTION carries %s (P9-C5)" % _k, _k in BAL["CORRUPTION"])
check("CORRUPTION gates exactly two evolution families",
      len(BAL["CORRUPTION"].get("evo_families", [])) == 2)

check("the CARE cure item clears corruption and is reachable",
      BAL["CORRUPTION"].get("cleared_by_item") in {i["id"] for i in ITEMS})
# ...and it does so IN THE COLUMN, not only in balance.json's prose. Until
# items.json had a `clears` column these two files could not be compared at all.
_cure = [i for i in ITEMS if i["id"] == BAL["CORRUPTION"].get("cleared_by_item")]
check("CORRUPTION.cleared_by_item really clears CORRUPTED in its own row",
      bool(_cure) and "CORRUPTED" in _cure[0]["clears"],
      str(_cure[0]["clears"]) if _cure else "no such item")
drops = BAL["ITEM_DROPS"]
check("ITEM_DROPS covers all six categories", set(drops) == set(CATS))
check("every ITEM_DROPS entry is a real item",
      all(int(k) in {i["id"] for i in ITEMS} for c in drops for k in drops[c]))

# ============================================================ 9. ENCOUNTERS
P(); P("=" * 78); P("9. ENCOUNTER TABLES  (spec 20, 22, 57)"); P("=" * 78)
by = collections.defaultdict(list)
for r in ENC: by[r["category"]].append(r)
check("all six categories have a table", set(by) == set(CATS))
check("every table sums to 100", all(sum(r["weight"] for r in by[c]) == 100 for c in CATS),
      {c: sum(r["weight"] for r in by[c]) for c in CATS})
noth = {c: sum(r["weight"] for r in by[c] if r["outcome"] == "NOTHING") for c in CATS}
item = {c: sum(r["weight"] for r in by[c] if r["outcome"] == "ITEM") for c in CATS}
spec = {c: sum(r["weight"] for r in by[c] if r["outcome"] == "SPECIAL") for c in CATS}
wild = {c: sum(r["weight"] for r in by[c] if r["outcome"] == "WILD") for c in CATS}
check("NOTHING >= 15%% in every category", all(v >= 15 for v in noth.values()), noth)
check("ITEM meaningful but not dominant (10-20%)",
      all(10 <= v <= 20 for v in item.values()), item)
check("SPECIAL is reachable everywhere (>= 4%)", all(v >= 4 for v in spec.values()), spec)
check("WILD is the dominant outcome everywhere", all(wild[c] > item[c] for c in CATS), wild)
def pool(cat, lo, hi):
    return [s for s in SPECIES if s["category_mask"] & BIT[cat] and lo <= s["rarity"] <= hi]
unres = [(r["category"], r["rarity_min"], r["rarity_max"]) for r in ENC
         if r["outcome"] == "WILD" and not pool(r["category"], r["rarity_min"], r["rarity_max"])]
check("every WILD row resolves to a non-empty species pool", not unres, str(unres))
noitem = [(r["category"],) for r in ENC if r["outcome"] == "ITEM" and not
          [k for k in drops[r["category"]]
           if next(i for i in ITEMS if i["id"] == int(k))["rarity"] in
              range(r["rarity_min"], r["rarity_max"]+1)]]
check("every ITEM row resolves to a non-empty drop pool", not noitem, str(noitem))

# P5-C3: THE SPECIAL PAYLOAD. Until specials.json existed, SPECIAL was 4-10 % of
# every scan with no roster, no ids and no weights behind it, and the two checks
# below could not be written at all. They are the twin of the ITEM pair above.
SPW = SPEC["WEIGHTS"]
SPE = SPEC["EVENTS"]
check("SPECIAL_EVENTS ids contiguous from 1",
      [e["id"] for e in SPE] == list(range(1, len(SPE) + 1)))
check("every SPECIAL event kind is declared in SPECIAL_KIND_ENUM",
      all(e["kind"] in BAL["SPECIAL_KIND_ENUM"] for e in SPE),
      str(sorted({e["kind"] for e in SPE})))
check("every declared SPECIAL kind has an event",
      set(e["kind"] for e in SPE) == set(BAL["SPECIAL_KIND_ENUM"]))
check("no XP_BURST pays zero XP (an event with no effect)",
      all(e["value"] >= 1 for e in SPE if e["kind"] == "XP_BURST"))
check("SPECIAL weights cover all six categories", set(SPW) == set(CATS), str(sorted(SPW)))
check("every SPECIAL weight table sums to 100",
      all(sum(SPW[c].values()) == 100 for c in CATS),
      {c: sum(SPW[c].values()) for c in CATS})
check("every SPECIAL weight entry is a real event and carries weight",
      all(int(k) in {e["id"] for e in SPE} and v >= 1
          for c in SPW for k, v in SPW[c].items()))
nospec = [c for c in CATS if not SPW[c]]
check("every SPECIAL row resolves to a non-empty event pool", not nospec, str(nospec))
check("the corruption event exists and every category can reach one",
      all(any(next(e for e in SPE if e["id"] == int(k))["kind"] == "CORRUPTION"
              for k in SPW[c]) for c in CATS),
      str({c: [int(k) for k in SPW[c]] for c in CATS}))

# ======================================================= 9b. NETWORK CLASSIFIER
P(); P("=" * 78); P("9b. NETWORK CLASSIFIER  (spec 20, 40, 44)"); P("=" * 78)
_tok = NET["TOKENS"]
_all = [t for v in _tok.values() for t in v]
check("token classes match NET_TOKEN_CLASS_BITS",
      set(_tok) == set(NET["NET_TOKEN_CLASS_BITS"]), str(sorted(_tok)))
check("every token class is populated", all(len(v) > 0 for v in _tok.values()),
      {k: len(v) for k, v in _tok.items()})
check("no token is claimed by two classes", len(_all) == len(set(_all)),
      "%d tokens, %d unique" % (len(_all), len(set(_all))))
check("every token is lowercase ASCII letters at least TOKEN_MIN_LEN long",
      all(t.isascii() and t.isalpha() and t == t.lower()
          and len(t) >= NET["TOKEN_MIN_LEN"] for t in _all),
      str([t for t in _all if not (t.isascii() and t.isalpha() and t == t.lower()
                                   and len(t) >= NET["TOKEN_MIN_LEN"])]))
def _fnv(t):
    h = 0x811C9DC5
    for b in t.encode("ascii"):
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h
check("no two tokens collide under FNV-1a 32",
      len({_fnv(t) for t in _all}) == len(set(_all)))
check("RSSI_MID is strictly weaker than RSSI_NEAR, both inside int8_t",
      -127 <= NET["RSSI_MID"] < NET["RSSI_NEAR"] <= 0,
      "MID %d NEAR %d" % (NET["RSSI_MID"], NET["RSSI_NEAR"]))
check("NET_AUTH_ENUM is 0..N-1 with OTHER last (the sink for a new IDF mode)",
      sorted(NET["NET_AUTH_ENUM"].values()) == list(range(len(NET["NET_AUTH_ENUM"])))
      and NET["NET_AUTH_ENUM"]["OTHER"] == len(NET["NET_AUTH_ENUM"]) - 1,
      str(NET["NET_AUTH_ENUM"]))
# EVERY CATEGORY THE CLASSIFIER CAN EMIT HAS CONTENT BEHIND IT. This is the
# join between this section and section 9: a ladder rung that lands on a
# category with no encounter rows is an outcome the game cannot answer.
check("every classifier output category has encounter rows",
      all(any(r["category"] == c for r in ENC) for c in CATS), str(CATS))

# ============================================================== 10. BALANCE
P(); P("=" * 78); P("10. BALANCE CONSTANTS  (plan 1.5.1/1.5.2, spec 11, 36)"); P("=" * 78)
# THE CURVE IS READ FROM data/balance.h, NOT FROM balance.json. See
# _shipped_xp_table() above for why the pack's second curve was deleted.
xp = _shipped_xp_table()
check("data/balance.h's XP_TABLE was found and parsed", len(xp) == 31,
      "%d entries" % len(xp))
if not xp:
    xp = [0] * 31          # keep the rest of the section running and failing
check("XP_TABLE has 31 entries (level 1..30)", len(xp) == 31)
check("sum(XP_TABLE) < 65535 (u16 xp field)", 0 < sum(xp) < 65535, "%d" % sum(xp))
check("max XP increment < 65535", max(xp) < 65535, "%d" % max(xp))
check("XP_TABLE rises strictly over 1..29 and is 0 at both ends",
      xp[0] == 0 and xp[30] == 0 and all(xp[i + 1] > xp[i] for i in range(1, 29)))
# THE DELETION, ENFORCED. P9-C4 removed balance.json's rival curve; without this
# check nothing would stop the next content edit putting one back, and the two
# files would silently disagree again exactly as they did for five phases.
check("balance.json carries NO second XP curve (P9-C4 deleted it)",
      not any(k in BAL for k in ("XP_TABLE", "XP_TOTAL_TO_MAX")),
      "the curve lives only in Pebblebol/src/data/balance.h")
check("TYPE_CHART is the SIGNAL>CORRUPT>SYSTEM>SIGNAL triangle",
      BAL["TYPE_CHART"] == [[0,1,-1],[-1,0,1],[1,-1,0]])
check("TYPE_MOD_SCALE shipped as a real key (draft_B graft)", "TYPE_MOD_SCALE" in BAL)
check("stat totals per stage are the brief's 16/22/28",
      BAL["TOTAL_STAT_POINTS_BY_STAGE"] == [16, 22, 28])
check("creator budget <= stage-1 budget (cannot out-stat a final evolution)",
      BAL["CREATOR_TOTAL_STAT_POINTS"] <= 22 and
      BAL["CREATOR_ATTACK_BUDGET"] <= BAL["ATTACK_BUDGET_BY_STAGE"][1])
check("creator POWER% formula exists (Appendix C bar)",
      "creator_power_pct" in BAL["FORMULAS"])
check("STRING_CONTRACT names the four StrId blocks and their bases",
      len(BAL["STRING_CONTRACT"]["blocks"]) == 4)

# XP economy (graft: draft_B XP/day table)
P("  XP curve: total to L30 = %d" % sum(xp))
def day_to(level, per_day):
    need = sum(xp[1:level]); return (need + per_day - 1)//per_day
P("    XP/day    L10 by day   L20 by day   L30 by day")
for pd in (600, 1000, 1500, 2500):
    P("    %5d     %8d     %8d     %8d" % (pd, day_to(10,pd), day_to(20,pd), day_to(30,pd)))
sc = BAL["XP_CANDY_SCALE"]
P("  candy anchoring (value * XP_CANDY_SCALE=%d):" % sc)
for i in ITEMS:
    if i["klass"] == "XP_CANDY":
        P("    %-11s %5d XP  -> %5.1f of them to take one Pebble 1 -> 30"
          % (i["name"], i["value"]*sc, sum(xp)/(i["value"]*sc)))

# flash budget (shared gap 8: nobody costed the strings)
P("  flash budget (spec 46, P10-C2):")
tbl = len(SPECIES)*24 + len(ATTACKS)*16 + len(ITEMS)*8 + len(EVO)*8 + len(ENC)*8
ndrops = sum(len(v) for v in drops.values())
tbl += ndrops*4
def enc_len(x):
    # A non-Latin-1 string is ALREADY a named FAILED check above. Dying here
    # with an unhandled UnicodeEncodeError would kill the run before the RESULT
    # line prints, so the gate would still fail - but with a stack instead of
    # the diagnosis. Count the bytes it would actually take and carry on.
    try:                       return len(x.encode("latin-1"))
    except UnicodeEncodeError: return len(x.encode("utf-8"))
strb = sum(enc_len(x)+1 for x in allstr)
idxb = (len(SPECIES)*2 + len(SPECIES)*2 + len(ATTACKS)*2 + len(ITEMS)*2)
atlas = 60*2*72
P("    struct tables            %6d B  (species %d, attacks %d, items %d, evo %d, encounter %d, item-drops %d)"
  % (tbl, len(SPECIES)*24, len(ATTACKS)*16, len(ITEMS)*8, len(EVO)*8, len(ENC)*8, ndrops*4))
P("    string bytes             %6d B  (%d strings, Latin-1, NUL-terminated)" % (strb, len(allstr)))
P("    string index arrays      %6d B  (u16 offsets)" % idxb)
P("    sprite atlas 60 x 2 x 24x24  %6d B" % atlas)
P("    ------------------------------------------")
P("    CONTENT TOTAL            %6d B  (%.2f KB)" % (tbl+strb+idxb+atlas, (tbl+strb+idxb+atlas)/1024))

if FAST:
    P(); P("(--fast: simulation skipped)")
    P("=" * 78)
    P("RESULT: %d checks, %d FAILED" % (len([l for l in OUT if l.startswith("  [")]), len(FAIL)))
    open(os.path.join(HERE,"_verify_out.txt"),"w",encoding="utf-8").write("\n".join(OUT)+"\n")
    sys.exit(1 if FAIL else 0)

# ================================================ 11. SIMULATION - 1v1 MATRIX
P(); P("=" * 78); P("11. AI-vs-AI MATRIX, 1v1  (P9-C4 proxy; level 15, 8 seeds/pair)"); P("=" * 78)
def matrix(bb, stage, seeds=8, level=15):
    grp = [s for s in SPECIES if s["stage"] == stage]
    res = {}
    for a in grp:
        w = n = 0
        for b in grp:
            if a["id"] == b["id"]: continue
            wr, _ = SE.duel_pair(bb, a, b, ATK, level, seeds=seeds)
            w += wr; n += 1
        res[a["id"]] = w / n
    return res
FLAT = dict(BAL); FLAT["TYPE_MUL_NUM"] = [1,1,1]; FLAT["TYPE_MUL_DEN"] = [1,1,1]
rounds_all = []
P("  round length, stage 1, level 15, all 190 pairings:")
grp1 = [s for s in SPECIES if s["stage"] == 1]
for i in range(len(grp1)):
    for j in range(i+1, len(grp1)):
        _, rl = SE.duel_pair(BAL, grp1[i], grp1[j], ATK, 15, seeds=8)
        rounds_all += rl
rounds_all.sort(); n = len(rounds_all)
med, p10, p90 = rounds_all[n//2], rounds_all[int(.1*n)], rounds_all[int(.9*n)]
band = sum(1 for r in rounds_all if 4 <= r <= 7)/n
P("    median %d   p10 %d   p90 %d   %.1f%% inside the 4-7 round target  (n=%d)"
  % (med, p10, p90, 100*band, n))
check("a level-15 stage-1 fight lasts 4-7 rounds", 4 <= med <= 7 and p10 >= 3 and p90 <= 8,
      "median %d, p10 %d, p90 %d" % (med, p10, p90))

P("  per-species win rate (typed / type-neutral):")
P("  (rarity is checked on the TYPE-NEUTRAL matrix: rarity and type are independent")
P("   properties, and the rarity bands do not contain equal numbers of each type,")
P("   so the typed matrix confounds the two. Typed means are printed as well.)")
mono_ok = True
for st in (0, 1, 2):
    rt, rf = matrix(BAL, st), matrix(FLAT, st)
    vt = list(rt.values()); vf = list(rf.values())
    P("    stage %d  typed  min %.0f%% max %.0f%% sd %.1f   |   flat  min %.0f%% max %.0f%% sd %.1f"
      % (st, 100*min(vt), 100*max(vt), 100*statistics.pstdev(vt),
         100*min(vf), 100*max(vf), 100*statistics.pstdev(vf)))
    ba = collections.defaultdict(list); br = collections.defaultdict(list); bt = collections.defaultdict(list)
    for sid, v in rt.items():
        s = BYID[sid]; ba[s["_archetype"]].append(v); br[s["rarity"]].append(v); bt[s["type"]].append(v)
    P("      archetype " + "  ".join("%s %.0f%%" % (k[:4], 100*sum(v)/len(v)) for k, v in sorted(ba.items())))
    P("      type      " + "  ".join("%s %.0f%%" % (k[:3], 100*sum(v)/len(v)) for k, v in bt.items()))
    brf = collections.defaultdict(list)
    for sid, v in rf.items(): brf[BYID[sid]["rarity"]].append(v)
    means = [(r, sum(v)/len(v)) for r, v in sorted(brf.items())]
    tmeans = [(r, sum(v)/len(v)) for r, v in sorted(br.items())]
    P("      rarity flat  " + "  ".join("%s %.0f%%" % (BAL["RARITY_ENUM"][r][:4], 100*m) for r, m in means))
    P("      rarity typed " + "  ".join("%s %.0f%%" % (BAL["RARITY_ENUM"][r][:4], 100*m) for r, m in tmeans))
    if len(means) > 1 and any(means[i+1][1] < means[i][1] - 0.005 for i in range(len(means)-1)):
        mono_ok = False
    if len(tmeans) > 1 and any(tmeans[i+1][1] < tmeans[i][1] - 0.04 for i in range(len(tmeans)-1)):
        mono_ok = False
    if st == 1:
        outside = sum(1 for v in vt if not 0.35 <= v <= 0.65)
        P("      %d of %d stage-1 species outside the 35-65%% band" % (outside, len(vt)))
check("mean win rate rises with rarity in every stage (flat exact, typed +-4 pts)", mono_ok)
sd1 = statistics.pstdev(list(matrix(BAL, 1).values()))
check("stage-1 spread is tight (sd <= 8 points)", 100*sd1 <= 8.0, "sd %.1f" % (100*sd1))

P("  3x3 cross-type matrix, stage 1 (the check draft A and draft C did NOT do):")
T = ("SIGNAL", "CORRUPT", "SYSTEM")
byt = {t: [s for s in SPECIES if s["stage"] == 1 and s["type"] == t] for t in T}
def xmat(bb):
    M = {}
    for a in T:
        for d in T:
            if a == d: continue
            w = n = 0
            for x in byt[a]:
                for y in byt[d]:
                    wr, _ = SE.duel_pair(bb, x, y, ATK, 15, seeds=8); w += wr; n += 1
            M[(a, d)] = w/n
    return M
MT, MF = xmat(BAL), xmat(FLAT)
P("           attacker\\defender      typed      type-neutral")
for k in sorted(MT):
    P("           %-8s > %-8s   %5.1f%%        %5.1f%%" % (k[0], k[1], 100*MT[k], 100*MF[k]))
check("with the triangle OFF the roster itself is balanced (all cells 40-60%)",
      all(0.40 <= v <= 0.60 for v in MF.values()),
      "flat range %.0f-%.0f%%" % (100*min(MF.values()), 100*max(MF.values())))
check("typed 1v1 cells inside 35-65% (all three drafts measured 88-98%)",
      all(0.35 <= v <= 0.65 for v in MT.values()),
      "typed range %.0f-%.0f%%" % (100*min(MT.values()), 100*max(MT.values())))

# ============================== 12. SIMULATION - THE FORMAT THE GAME SHIPS
P(); P("=" * 78); P("12. AI-vs-AI, 3 v 3 WITH SWITCHING  (spec 14 - the actual format)"); P("=" * 78)
rng = random.Random(20260903)
def t3(bb, TA, TB, k=1):
    w = n = 0; rr = []
    for _ in range(k):
        r, x = SE.battle_3v3(bb, TA, TB, ATK, 15, seed=rng.randrange(10**7))
        w += (r == 0); n += 1; rr.append(x)
    return w, n, rr
P("  mono-type team vs the type that counters it (the cost of a mono build):")
for a, d in (("SIGNAL","CORRUPT"), ("CORRUPT","SYSTEM"), ("SYSTEM","SIGNAL")):
    w = n = 0
    for _ in range(120):
        A = rng.sample(byt[a], 3); B = rng.sample(byt[d], 3)
        r, _x = SE.battle_3v3(BAL, A, B, ATK, 15, seed=rng.randrange(10**7)); w += (r == 0); n += 1
    P("    3x%-8s vs 3x%-8s  %5.1f%%" % (a, d, 100*w/n))
w = n = 0; rr = []
for _ in range(800):
    A = [rng.choice(byt[t]) for t in T]; B = [rng.choice(byt[t]) for t in T]
    r, x = SE.battle_3v3(BAL, A, B, ATK, 15, seed=rng.randrange(10**7)); w += (r == 0); n += 1; rr.append(x)
P("  balanced team (one of each type) vs the same: %.1f%% (median %d rounds)" % (100*w/n, statistics.median(rr)))
check("a balanced 3v3 mirror is a coin flip", 0.455 <= w/n <= 0.545, "%.1f%% of %d" % (100*w/n, n))
P("  balanced teams, LEAD type advantage (the real expression of the triangle):")
lead = {}
for a, d in (("SIGNAL","CORRUPT"), ("CORRUPT","SYSTEM"), ("SYSTEM","SIGNAL")):
    w = n = 0
    for _ in range(500):
        A = [rng.choice(byt[a])] + [rng.choice(byt[t]) for t in T if t != a]
        B = [rng.choice(byt[d])] + [rng.choice(byt[t]) for t in T if t != d]
        r, _x = SE.battle_3v3(BAL, A, B, ATK, 15, seed=rng.randrange(10**7)); w += (r == 0); n += 1
    lead[(a, d)] = w/n
    P("    lead %-8s vs lead %-8s  %5.1f%%" % (a, d, 100*w/n))
check("spec 12: type advantage MATTERS (lead advantage >= 54%) ...",
      all(v >= 0.54 for v in lead.values()), "min %.1f%%" % (100*min(lead.values())))
check("... WITHOUT deciding the battle (lead advantage <= 65%)",
      all(v <= 0.65 for v in lead.values()), "max %.1f%%" % (100*max(lead.values())))

# ================================================= 13. SPAWN ECONOMICS (§22, §57)
P(); P("=" * 78); P("13. SPAWN ECONOMICS  (spec 21, 22, 57 - what a player actually sees)"); P("=" * 78)
# A day of ordinary carrying: the 2 h per-network cooldown means repeats do not
# roll, so the model is "distinct networks encountered per day, weighted by the
# category mix of a commuting adult". Deliberately conservative.
ROLLS_PER_DAY = 8
MIX = {"HOME": .30, "PUBLIC": .25, "UNKNOWN": .20, "BUSINESS": .12, "OPEN": .10, "HIDDEN": .03}
P("  model: %d encounter rolls/day (2 h cooldown per network), category mix %s"
  % (ROLLS_PER_DAY, {k: "%.0f%%" % (100*v) for k, v in MIX.items()}))
out = collections.Counter()
band_p = collections.Counter()
for c, share in MIX.items():
    for r in by[c]:
        p = share * r["weight"] / 100.0
        out[r["outcome"]] += p
        if r["outcome"] == "WILD":
            band_p[r["rarity_min"]] += p
P("  per week (%d rolls):" % (7*ROLLS_PER_DAY))
for k in ("WILD", "ITEM", "SPECIAL", "NOTHING"):
    P("    %-8s %5.1f  (%4.1f%% of rolls)" % (k, 7*ROLLS_PER_DAY*out[k], 100*out[k]))
P("  wild sightings per week by rarity:")
for b in sorted(band_p):
    P("    %-9s %5.2f" % (BAL["RARITY_ENUM"][b], 7*ROLLS_PER_DAY*band_p[b]))
check("carrying the device is worthwhile (>= 20 wild sightings/week)",
      7*ROLLS_PER_DAY*out["WILD"] >= 20, "%.1f/week" % (7*ROLLS_PER_DAY*out["WILD"]))
check("exploration is not a slot machine (NOTHING between 15% and 35%)",
      0.15 <= out["NOTHING"] <= 0.35, "%.1f%%" % (100*out["NOTHING"]))
# per-species reachability
def p_species(s):
    tot = 0.0
    for c, share in MIX.items():
        if not (s["category_mask"] & BIT[c]): continue
        for r in by[c]:
            if r["outcome"] != "WILD" or not (r["rarity_min"] <= s["rarity"] <= r["rarity_max"]): continue
            poolw = sum(x["spawn_weight"] for x in pool(c, r["rarity_min"], r["rarity_max"]))
            tot += share * r["weight"]/100.0 * s["spawn_weight"]/poolw
    return tot
ps = {s["name"]: p_species(s) for s in SPECIES}
unreach = [k for k, v in ps.items() if v <= 0]
check("every species is reachable by a wild encounter", not unreach, str(unreach[:5]))
P("  expected days to a FIRST sighting (1/p at %d rolls/day):" % ROLLS_PER_DAY)
for b in range(4):
    grp = [(k, v) for k, v in ps.items() if BYID[next(s["id"] for s in SPECIES if s["name"] == k)]["rarity"] == b]
    d = sorted(1.0/(v*ROLLS_PER_DAY) for _, v in grp)
    P("    %-9s  %2d species   median %5.1f d   worst %6.1f d (%s)"
      % (BAL["RARITY_ENUM"][b], len(grp), d[len(d)//2], d[-1],
         min(grp, key=lambda x: x[1])[0]))
LIMIT = {0: 60, 1: 120, 2: 150, 3: 60}
worst = {b: max(1.0/(ps[s["name"]]*ROLLS_PER_DAY) for s in SPECIES if s["rarity"] == b)
         for b in range(4)}
check("no species is unreachable in practice (common<=60d, unc<=120d, rare<=150d, special<=60d)",
      all(worst[b] <= LIMIT[b] for b in range(4)),
      ", ".join("%s %.0fd" % (BAL["RARITY_ENUM"][b][:4], worst[b]) for b in range(4)))
check("the special band is a month-scale chase, not a year-scale one",
      worst[3] <= 60, "worst special %.0f days" % worst[3])
P("  HIDDEN networks are only %.0f%% of the mix, so the three specials are the "
  "content that rewards going somewhere new." % (100*MIX["HIDDEN"]))

# ============================================== 14. CORRUPTION REACHABILITY
P(); P("=" * 78); P("14. CORRUPTION  (spec 55; P9-C5)"); P("=" * 78)
C = BAL["CORRUPTION"]
p_special = out["SPECIAL"]
P("  SPECIAL outcome rate      %.1f%% of rolls -> %.1f/week -> one every %.1f days"
  % (100*p_special, 7*ROLLS_PER_DAY*p_special, 1/(p_special*ROLLS_PER_DAY)))
check("the corruption event fires at least weekly (spec 55: a signature mechanic)",
      1/(p_special*ROLLS_PER_DAY) <= 7, "every %.1f days" % (1/(p_special*ROLLS_PER_DAY)))
P("  in-battle route          attack %d (%s), %.1f%% chance the status outlives the battle"
  % (C["sources"]["attack_id"], ATK[C["sources"]["attack_id"]]["name"], C["battle_infect_permille"]/10))
users = [s["name"] for s in SPECIES if C["sources"]["attack_id"] in s["moves"]]
check("the corrupting attack is actually on the roster", len(users) >= 3,
      "%d species carry it" % len(users))
check("corruption gates 2 evolutions and nothing else can reach them",
      len(C["evo_rules"]) == 2 and all(
        next(r for r in EVO if r["species"] == e["species"])["cond"] == "CORRUPTED"
        for e in C["evo_rules"]))
check("corruption clears by timer AND by a real item, and never destroys",
      C["cleared_by_timer"] and C["cleared_by_item"] in {i["id"] for i in ITEMS} and C["never_destroys"])

# ===================================================================== DONE
P(); P("=" * 78)
nchecks = len([l for l in OUT if l.startswith("  [")])
P("RESULT: %d checks, %d FAILED%s" % (nchecks, len(FAIL), ("  -> " + ", ".join(FAIL)) if FAIL else ""))
P("=" * 78)
open(os.path.join(HERE, "_verify_out.txt"), "w", encoding="utf-8").write("\n".join(OUT) + "\n")
sys.exit(1 if FAIL else 0)
