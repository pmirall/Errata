# -*- coding: utf-8 -*-
"""Stochastic battle simulator for the Pebblebol content set.

Reads ONLY the shipped JSON (species/attacks/balance). Imports nothing from the
generator, so a passing verify run proves the *deliverable* consistent, not the
tool that made it.

Implements exactly the formulas balance.json publishes:
  hp_max = 10 + 2*base_hp + level
  atk/def/spd = base + level/3 + genome_var(0..2)
  stage: stat_eff = max(1, stat + stage), stages clamped to [-2,+2]
  raw = max(1, power*atk_eff // (def_eff*K))
  dmg = max(1, raw*TYPE_MUL_NUM[m+1] // TYPE_MUL_DEN[m+1]) + rng(0..2)   (MULTIPLICATIVE)
  dmg = max(1, raw + TYPE_MOD_SCALE*m) + rng(0..2)                       (ADDITIVE fallback)
  PROTECT halves incoming (>>1, min 1)
"""
import json, os, random

DIR = os.path.dirname(os.path.abspath(__file__))
TYPE_IDX = {"SIGNAL": 0, "CORRUPT": 1, "SYSTEM": 2}


def load(d=DIR):
    sp = json.load(open(os.path.join(d, "species.json"), encoding="utf-8"))
    at = json.load(open(os.path.join(d, "attacks.json"), encoding="utf-8"))
    ba = json.load(open(os.path.join(d, "balance.json"), encoding="utf-8"))
    return sp, {a["id"]: a for a in at}, ba


class Fighter:
    def __init__(self, sp, atk_tbl, bal, level, gvar=(1, 1, 1, 1)):
        self.sp = sp
        self.level = level
        self.hp_max = 10 + 2 * sp["base_hp"] + level
        self.hp = self.hp_max
        self.atk = sp["base_atk"] + level // 3 + gvar[1]
        self.dfn = sp["base_def"] + level // 3 + gvar[2]
        self.spd = sp["base_spd"] + level // 3 + gvar[3]
        self.moves = [atk_tbl[m] for m in sp["moves"]]
        self.t = TYPE_IDX[sp["type"]]
        self.reset()

    def reset(self):
        self.hp = self.hp_max
        self.st = {"ATK": 0, "DEF": 0, "SPD": 0}
        self.cd = [0] * len(self.moves)
        self.protect = 0
        self.stun = 0
        self.dots = []          # [dmg_per_round, rounds_left]
        self.corrupt = 0        # EFF_CORRUPT rounds left
        self.tmod_used = 0      # type-modified hits spent this battle (TYPE_MOD_MAX_HITS)

    def eff(self, k, base):
        s = max(-2, min(2, self.st[k]))
        if k == "ATK" and self.corrupt > 0:
            s += 1
        if k == "DEF" and self.corrupt > 0:
            s -= 1
        return max(1, base + s)

    @property
    def a(self): return self.eff("ATK", self.atk)
    @property
    def d(self): return self.eff("DEF", self.dfn)
    @property
    def s(self): return self.eff("SPD", self.spd)
    @property
    def alive(self): return self.hp > 0


def type_mod(bal, at, dt):
    if at is None:                       # NEUTRAL attack: never indexes the chart
        return 0
    return bal["TYPE_CHART"][at][dt]


def raw_damage(bal, power, a, d):
    return max(1, power * a // (d * bal["K"]))


def apply_type(bal, raw, m):
    if bal.get("TYPE_MOD_MODE", "MULTIPLICATIVE") == "MULTIPLICATIVE":
        return max(1, raw * bal["TYPE_MUL_NUM"][m + 1] // bal["TYPE_MUL_DEN"][m + 1])
    return max(1, raw + bal["TYPE_MOD_SCALE"] * m)


def acc_eff(bal, mv, user, foe):
    """SPD is otherwise only a turn-order tiebreak, which leaves the FAST and
    EVASIVE archetypes paying 6-7 stat points for almost nothing. Evasion gives
    SPD a second job: a faster defender is harder to hit."""
    gap = max(0, foe.s - user.s)
    pen = bal["EVASION_PER_SPD"] * min(gap, bal["EVASION_MAX_SPD_GAP"])
    return max(bal["ACCURACY_MIN"], mv["accuracy"] - pen)


def expected_damage(bal, mv, user, foe):
    if mv["power"] == 0:
        return 0.0
    at = None if mv["type"] == "NEUTRAL" else TYPE_IDX[mv["type"]]
    m = type_mod(bal, at, foe.t)
    raw = raw_damage(bal, mv["power"], user.a, foe.d)
    return (apply_type(bal, raw, m) + 1.0) * acc_eff(bal, mv, user, foe) / 100.0


def score_move(bal, mv, i, user, foe, rnd):
    """Greedy AI. Damage is scored on expected value; utility moves are scored
    as a fraction of the best damage move so a defensive kit is not strictly
    dominated (the failure mode B's tune.py documented)."""
    if user.cd[i] > 0:
        return -1e9
    ed = expected_damage(bal, mv, user, foe)
    best = max((expected_damage(bal, m, user, foe) for m in user.moves), default=1.0) or 1.0
    e, v, dur = mv["effect"], mv["effect_value"], mv["effect_duration"]
    sc = ed
    if e == "RECOIL_PCT":
        sc -= ed * v / 100.0 * 0.9
    if e == "DRAIN_PCT":
        sc += ed * v / 100.0 * 0.8
    if e == "DOT":
        sc += v * dur * 0.55
    if e == "EFF_CORRUPT":
        sc += best * 0.45
    if e == "SELF_DEBUFF_DEF":
        sc -= best * 0.30
    if e == "SELF_STUN":
        sc -= best * 0.55
    if e == "DEBUFF_SPD" and mv["power"] > 0:
        sc += best * 0.12
    if e == "DEBUFF_ATK" and mv["power"] > 0:
        sc += best * 0.22
    if mv["power"] == 0:
        rounds_left = 4.0
        if e == "BUFF_ATK":
            sc = best * 0.55 * v * min(dur, rounds_left) / 3.0 if user.st["ATK"] < 2 else -1
        elif e == "BUFF_DEF":
            sc = best * 0.50 * v * min(dur, rounds_left) / 3.0 if user.st["DEF"] < 2 else -1
        elif e == "BUFF_SPD":
            sc = best * 0.35 * v * min(dur, rounds_left) / 3.0 if user.st["SPD"] < 2 else -1
        elif e == "DEBUFF_ATK":
            sc = best * 0.45 if foe.st["ATK"] > -2 else -1
        elif e == "DEBUFF_DEF":
            sc = best * 0.50 if foe.st["DEF"] > -2 else -1
        elif e == "DEBUFF_SPD":
            sc = best * 0.30 if foe.st["SPD"] > -2 else -1
        elif e == "PROTECT_HALF":
            sc = best * (1.05 if user.hp < user.hp_max * 0.55 else 0.42) * dur
        elif e == "HEAL_PCT":
            heal = min(user.hp_max * v / 100.0, user.hp_max - user.hp)
            sc = heal * 0.85
        elif e == "CLEANSE":
            bad = sum(1 for k in user.st if user.st[k] < 0) + len(user.dots) + (1 if user.corrupt else 0)
            sc = best * 0.9 * bad if bad else -1
        sc *= mv["accuracy"] / 100.0   # utility moves are not dodged
    return sc


def take_turn(bal, user, foe, rng, rnd):
    if user.stun > 0:
        user.stun -= 1
        return
    i = max(range(len(user.moves)), key=lambda j: score_move(bal, user.moves[j], j, user, foe, rnd))
    mv = user.moves[i]
    if mv["cooldown"]:
        user.cd[i] = mv["cooldown"] + 1
    hit_acc = acc_eff(bal, mv, user, foe) if mv["power"] > 0 else mv["accuracy"]
    if rng.randrange(100) >= hit_acc:
        return
    dealt = 0
    if mv["power"] > 0:
        at = None if mv["type"] == "NEUTRAL" else TYPE_IDX[mv["type"]]
        m = type_mod(bal, at, foe.t)
        if m != 0:
            if user.tmod_used >= bal.get("TYPE_MOD_MAX_HITS", 255):
                m = 0
            else:
                user.tmod_used += 1
        dmg = apply_type(bal, raw_damage(bal, mv["power"], user.a, foe.d), m) + rng.randrange(3)
        if foe.protect > 0:
            dmg = max(1, dmg >> 1)
        foe.hp -= dmg
        dealt = dmg
    e, v, dur = mv["effect"], mv["effect_value"], mv["effect_duration"]
    if e == "BUFF_ATK":   user.st["ATK"] = min(2, user.st["ATK"] + v)
    elif e == "BUFF_DEF": user.st["DEF"] = min(2, user.st["DEF"] + v)
    elif e == "BUFF_SPD": user.st["SPD"] = min(2, user.st["SPD"] + v)
    elif e == "DEBUFF_ATK": foe.st["ATK"] = max(-2, foe.st["ATK"] - v)
    elif e == "DEBUFF_DEF": foe.st["DEF"] = max(-2, foe.st["DEF"] - v)
    elif e == "DEBUFF_SPD": foe.st["SPD"] = max(-2, foe.st["SPD"] - v)
    elif e == "PROTECT_HALF": user.protect = dur
    elif e == "HEAL_PCT": user.hp = min(user.hp_max, user.hp + user.hp_max * v // 100)
    elif e == "DRAIN_PCT": user.hp = min(user.hp_max, user.hp + dealt * v // 100)
    elif e == "RECOIL_PCT": user.hp -= max(1, dealt * v // 100)
    elif e == "SELF_DEBUFF_DEF": user.st["DEF"] = max(-2, user.st["DEF"] - v)
    elif e == "SELF_STUN": user.stun = dur
    elif e == "DOT":
        foe.dots = [d for d in foe.dots if d[0] != v]   # DOT never stacks: it refreshes
        foe.dots.append([v, dur])
    elif e == "EFF_CORRUPT": foe.corrupt = max(foe.corrupt, dur)
    elif e == "CLEANSE":
        for k in user.st:
            if user.st[k] < 0: user.st[k] = 0
        user.dots = []; user.corrupt = 0


def end_of_round(f):
    for d in f.dots:
        f.hp -= d[0]; d[1] -= 1
    f.dots = [d for d in f.dots if d[1] > 0]
    if f.corrupt > 0: f.corrupt -= 1
    if f.protect > 0: f.protect -= 1
    for i in range(len(f.cd)):
        if f.cd[i] > 0: f.cd[i] -= 1


def duel(bal, A, B, seed, max_rounds=40):
    rng = random.Random(seed)
    A.reset(); B.reset()
    for rnd in range(1, max_rounds + 1):
        order = [A, B] if (A.s, rng.random()) > (B.s, rng.random()) else [B, A]
        for u in order:
            f = B if u is A else A
            if not (u.alive and f.alive):
                break
            take_turn(bal, u, f, rng, rnd)
        end_of_round(A); end_of_round(B)
        if not A.alive or not B.alive:
            if not A.alive and not B.alive:
                if A.hp == B.hp: return rng.randrange(2), rnd      # fair tie
                return (0 if A.hp > B.hp else 1), rnd
            return (1 if not A.alive else 0), rnd
    fa, fb = A.hp * B.hp_max, B.hp * A.hp_max
    if fa == fb: return rng.randrange(2), max_rounds               # fair tie
    return (0 if fa > fb else 1), max_rounds


def duel_pair(bal, spA, spB, atk_tbl, level, seeds=16):
    A = Fighter(spA, atk_tbl, bal, level)
    B = Fighter(spB, atk_tbl, bal, level)
    w = 0; rl = []
    for s in range(seeds):
        r, n = duel(bal, A, B, seed=(spA["id"] * 7919 + spB["id"] * 104729 + s))
        w += (r == 0); rl.append(n)
    return w / seeds, rl


# ---------------- 3v3 with switching (spec 14) ----------------
def battle_3v3(bal, teamA, teamB, atk_tbl, level, seed, max_rounds=60):
    rng = random.Random(seed)
    TA = [Fighter(s, atk_tbl, bal, level) for s in teamA]
    TB = [Fighter(s, atk_tbl, bal, level) for s in teamB]
    for f in TA + TB: f.reset()
    ia = ib = 0

    def best_switch(team, cur, foe):
        """Pick the ally with the best type matchup that is alive and not active."""
        best, bi = None, -1
        for i, f in enumerate(team):
            if i == cur or not f.alive: continue
            v = type_mod(bal, f.t, foe.t) - type_mod(bal, foe.t, f.t) + f.hp / f.hp_max
            if best is None or v > best:
                best, bi = v, i
        return bi

    def wants_switch(team, cur, foe):
        f = team[cur]
        bad = type_mod(bal, f.t, foe.t) < 0 or type_mod(bal, foe.t, f.t) > 0
        if not (bad and f.hp > f.hp_max * 0.45): return -1
        j = best_switch(team, cur, foe)
        if j < 0: return -1
        g = team[j]
        if type_mod(bal, g.t, foe.t) - type_mod(bal, foe.t, g.t) > type_mod(bal, f.t, foe.t) - type_mod(bal, foe.t, f.t):
            return j
        return -1

    for rnd in range(1, max_rounds + 1):
        sa = wants_switch(TA, ia, TB[ib])
        sb = wants_switch(TB, ib, TA[ia])
        # switching consumes the whole turn (spec 14)
        acts = []
        if sa >= 0: ia = sa; TA[ia].st = {"ATK": 0, "DEF": 0, "SPD": 0}
        else: acts.append(("A",))
        if sb >= 0: ib = sb; TB[ib].st = {"ATK": 0, "DEF": 0, "SPD": 0}
        else: acts.append(("B",))
        A, B = TA[ia], TB[ib]
        order = sorted(acts, key=lambda x: -( A.s if x[0] == "A" else B.s) + rng.random())
        for (side,) in order:
            u, f = (A, B) if side == "A" else (B, A)
            if not (u.alive and f.alive): break
            take_turn(bal, u, f, rng, rnd)
        end_of_round(A); end_of_round(B)
        if not A.alive:
            nxt = next((i for i, f in enumerate(TA) if f.alive), -1)
            if nxt < 0: return 1, rnd
            ia = nxt
        if not B.alive:
            nxt = next((i for i, f in enumerate(TB) if f.alive), -1)
            if nxt < 0: return 0, rnd
            ib = nxt
    la = sum(f.hp for f in TA); lb = sum(f.hp for f in TB)
    if la == lb: return rng.randrange(2), max_rounds               # fair tie
    return (0 if la > lb else 1), max_rounds
