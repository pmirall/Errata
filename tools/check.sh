#!/usr/bin/env bash
# tools/check.sh — THE GATE (plan §1.1). Run before every commit.
#
#   1. firmware build at 0 project warnings (tools/build.sh)
#   2. host tests: make -C tests check
#   3. size gates: GATE_FLASH_MAX / GATE_GLOBALS_MAX from config.h
#   4. grep gates (added as the corresponding plan commits land)
#
# Usage: tools/check.sh [--no-tests] [--no-build]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SKETCH="${SKETCH:-$ROOT/Pebblebol}"
DO_TESTS=1
DO_BUILD=1
for a in "$@"; do
  case "$a" in
    --no-tests) DO_TESTS=0 ;;
    --no-build) DO_BUILD=0 ;;
    *) echo "check.sh: unknown argument $a" >&2; exit 2 ;;
  esac
done

fail() { echo "GATE FAIL: $*" >&2; exit 1; }

CFG="$SKETCH/src/core/config.h"
[ -f "$CFG" ] || fail "config.h not found under $SKETCH"

gate_flash="$(grep -oE "^#define[[:space:]]+GATE_FLASH_MAX[[:space:]]+[0-9]+" "$CFG" | grep -oE "[0-9]+$")"
gate_glob="$(grep -oE "^#define[[:space:]]+GATE_GLOBALS_MAX[[:space:]]+[0-9]+" "$CFG" | grep -oE "[0-9]+$")"
[ -n "$gate_flash" ] && [ -n "$gate_glob" ] || fail "GATE_FLASH_MAX / GATE_GLOBALS_MAX missing from $(basename "$CFG")"

if [ $DO_BUILD -eq 1 ]; then
  out="$("$ROOT/tools/build.sh" --quiet)" || fail "firmware build"
  echo "$out"
  flash="$(sed -nE 's/.*flash=([0-9]+).*/\1/p' <<<"$out")"
  glob="$(sed -nE 's/.*globals=([0-9]+).*/\1/p' <<<"$out")"
  [ "$flash" -le "$gate_flash" ] || fail "flash $flash > GATE_FLASH_MAX $gate_flash"
  [ "$glob"  -le "$gate_glob"  ] || fail "globals $glob > GATE_GLOBALS_MAX $gate_glob"
fi

if [ $DO_TESTS -eq 1 ]; then
  make -C "$ROOT/tests" check || fail "host tests"
fi

# --- THE CONTENT GATE (P4-C1) ---------------------------------------------
# Two checks, both cheap (about 0.1 s together), both about the same thing: the
# committed headers under src/data/ are GENERATED, so nothing may hand-edit
# them and nothing may edit the JSON without re-running the generator.
#
#   1. tools/content/verify.py is the content pack's OWN gate. It travels with
#      the JSON and validates all 60 species against the design rules, importing
#      nothing from the generator - so a green run proves the deliverable
#      consistent even if gen_content.py is wrong.
#   2. gen_content.py --check regenerates every header in memory and diffs it
#      against the tree. It catches a hand edit to a generated header AND a JSON
#      edit that was never regenerated, which is the drift that would otherwise
#      only show up as a save recorded against a CONTENT_VERSION nothing built.
#
# Skipped with a WORD, not silently, if python3 is missing: a gate that decides
# not to run must say so. THE OUTER TEST IS A HARD FAIL, not a skip, and the
# difference matters: the other `if [ -f ... ]` guards in this file cover gates
# whose subject does not exist YET, while tools/content is a COMMITTED
# deliverable that src/data/*_table.h is generated from. Missing, it does not
# mean "not this phase", it means the gate's input was deleted - and until this
# `else` existed, `rm -rf tools/content` printed GATE OK and exited 0, which is
# the exact shape of failure this file's own comment forbids.
if [ -d "$ROOT/tools/content" ]; then
  if command -v python3 >/dev/null 2>&1; then
    ( cd "$ROOT/tools/content" && python3 verify.py --fast >/dev/null ) \
      || fail "tools/content/verify.py (the content pack's own gate)"
    python3 "$ROOT/tools/gen_content.py" --check >/dev/null \
      || fail "src/data/*_table.h and tools/content/*.json have drifted apart "\
"(run: python3 tools/gen_content.py)"
  else
    echo "check.sh: python3 not found, SKIPPING the content gate" >&2
  fi
else
  fail "tools/content is missing - src/data/*_table.h is generated from it and "\
"nothing can check them against it"
fi

# --- P4-C2/C3 FOLLOW-UP: THE FIRMWARE'S TUNING MATCHES THE CONTENT PACK'S ---
# NUMBERS THAT LIVE IN TWO PLACES AND NOTHING COMPARED. Every one of
# them is in tools/content/balance.json - which feeds CONTENT_VERSION, and which
# tools/content/sim_engine.py tuned the 36-species roster against - AND in
# Pebblebol/src/data/balance.h, which the firmware actually compiles. balance.h
# is NOT generated, so gen_content.py --check does not see it.
#
# WHY IT MATTERS, and it is a P4-C5 problem rather than a tidiness one: ELEVEN
# OF THE THIRTEEN decide hashed battle state. Retuning one in the HEADER alone moves
# every damage number while CONTENT_VERSION and BATTLE_ENGINE_VER both stay
# where they are - so two devices flashed from two commits agree on every
# version word they exchange and then disagree on the first round hash, which is
# exactly the silent desync game/battle.h's version block exists to prevent.
# MEASURED, on the tree this gate landed on: with BUFF_STAGE_MAX edited from +2
# to +3 in balance.h and nowhere else, `make -C tests check` printed
# ALL PASS 27/27 and `gen_content.py --check` printed "8 files in sync,
# CONTENT_VERSION 0x5B4A" - unchanged. Every clamp assertion in the tree is
# written in terms of the constant under test, so no literal pins it and nothing
# else was ever going to catch this.
#
# THE OTHER TWO DECIDE NOTHING, AND THE GATE SHOULD SAY WHICH (P4-C6). This
# paragraph used to claim the property for all thirteen. TYPE_MOD_SCALE is 0 and
# names the SUPERSEDED additive damage path (balance.h keeps it so the dead
# branch is visible rather than merely absent, and balance.json documents the
# TYPE_MOD_MODE=ADDITIVE A/B run P9-C4 may want); RISK_SELF_HP_PCT is 0 and names
# a self-damage rule the pack deliberately does not have. Neither is read by
# game/battle.cpp, so editing either in one file moves no damage number and
# desyncs nothing. They stay IN the gate on purpose - the day one of them stops
# being 0 it becomes load-bearing in both files at once, and that is precisely
# the moment a drift between them would be invisible - but the gate's own
# justification is about the eleven, not about these two.
#
# P5-C3/C4 ADDED SIX MORE AND THEY ARE NOT BATTLE NUMBERS. The capture clamp,
# the level-gap penalty, the attempt cap, the base-chance array and the 24 h
# corruption timer decide what EXPLORING hands the player; the mechanism that
# makes them belong here is identical (read from balance.h, tuned in
# balance.json, and balance.h is not generated), and the count in the first line
# is deliberately no longer a number this comment has to keep up to date.
#
# It compares VALUES, not formatting: the header writes (-2) and (+2) where the
# JSON writes -2 and 2, and the two files disagree on several names on purpose
# (JSON K is BATTLE_K, JSON LEVEL_MAX is XP_LEVEL_MAX), so the map is explicit.
# A DOTTED name reads a nested key (CORRUPTION.duration_s). TYPE_MUL_NUM/DEN and
# CAPTURE_BASE_PERMILLE are arrays and are compared element by element.
#
# Skipped with a WORD if python3 is missing, like the content gate above.
if [ -f "$ROOT/tools/content/balance.json" ] && [ -f "$SKETCH/src/data/balance.h" ]; then
  if command -v python3 >/dev/null 2>&1; then
    python3 - "$ROOT/tools/content/balance.json" "$SKETCH/src/data/balance.h" <<'PYGATE' \
      || fail "src/data/balance.h and tools/content/balance.json disagree on a tuning "\
"constant - the firmware would play a different game from the one the roster was tuned for, "\
"with no CONTENT_VERSION or BATTLE_ENGINE_VER change to say so"
import json, re, sys

pack = json.load(open(sys.argv[1], encoding="utf-8"))
hdr  = open(sys.argv[2], encoding="utf-8").read()

# JSON name -> balance.h macro name. Only the ones that really are the same
# number: everything else in balance.json belongs to a GENERATED header and
# gen_content.py --check already owns it.
SCALARS = {
    "K":                  "BATTLE_K",
    "LEVEL_MAX":          "XP_LEVEL_MAX",
    "TYPE_MOD_SCALE":     "TYPE_MOD_SCALE",
    "TYPE_MOD_MAX_HITS":  "TYPE_MOD_MAX_HITS",
    "PROTECT_DIVISOR":    "PROTECT_DIVISOR",
    "BUFF_STAGE_MIN":     "BUFF_STAGE_MIN",
    "BUFF_STAGE_MAX":     "BUFF_STAGE_MAX",
    "EVASION_PER_SPD":    "EVASION_PER_SPD",
    "EVASION_MAX_SPD_GAP":"EVASION_MAX_SPD_GAP",
    "ACCURACY_MIN":       "ACCURACY_MIN",
    "RISK_SELF_HP_PCT":   "RISK_SELF_HP_PCT",
    # P5-C3/C4. Not battle numbers, and the paragraph above is careful about
    # that: these decide what EXPLORING hands the player. They are in this gate
    # for the same mechanical reason - the firmware reads them from balance.h,
    # the pack tunes them in balance.json, balance.json feeds CONTENT_VERSION
    # and balance.h is not generated, so nothing else compares the two.
    "CAPTURE_LEVEL_GAP_PERMILLE": "CAPTURE_LEVEL_GAP_PERMILLE",
    "CAPTURE_MIN_PERMILLE":       "CAPTURE_MIN_PERMILLE",
    "CAPTURE_MAX_PERMILLE":       "CAPTURE_MAX_PERMILLE",
    "CAPTURE_MAX_ATTEMPTS":       "CAPTURE_MAX_ATTEMPTS",
    # A DOTTED PATH reads a nested key. The 24 h corruption timer is inside
    # balance.json's CORRUPTION block and has to be compared from there rather
    # than copied to the top level: a second copy inside the pack would be one
    # more pair of numbers nothing compares, which is the defect this gate is.
    "CORRUPTION.duration_s":      "CORRUPT_DURATION_S",
    "CORRUPTION.battle_infect_permille": "CORRUPT_BATTLE_INFECT_PERMILLE",
}
ARRAYS = ["TYPE_MUL_NUM", "TYPE_MUL_DEN", "CAPTURE_BASE_PERMILLE"]

def macro(name):
    m = re.search(r"^#define\s+%s\s+(\S+)" % re.escape(name), hdr, re.M)
    if not m:
        return None
    t = m.group(1).strip().strip("()")          # (-2) -> -2
    t = re.sub(r"[uUlL]+$", "", t)              # 86400UL -> 86400
    t = t.lstrip("+")
    try:
        return int(t, 0)
    except ValueError:
        return None

def array(name):
    m = re.search(r"^inline constexpr \w+ %s\[\d*\]\s*=\s*\{([^}]*)\}" % re.escape(name),
                  hdr, re.M)
    if not m:
        return None
    return [int(x.strip(), 0) for x in m.group(1).split(",") if x.strip()]

def packed(path):
    """balance.json's value at a dotted path, or None."""
    node = pack
    for part in path.split("."):
        if not isinstance(node, dict) or part not in node:
            return None
        node = node[part]
    return node

bad = 0
for jname, hname in sorted(SCALARS.items()):
    want = packed(jname)
    if want is None:
        print("balance gate: %s is not in balance.json" % jname); bad += 1
    got  = macro(hname)
    if got is None:
        print("balance gate: %s is not a plain #define in balance.h" % hname); bad += 1
    elif got != want:
        print("balance gate: %s is %r in balance.h but %s is %r in balance.json"
              % (hname, got, jname, want)); bad += 1
for name in ARRAYS:
    want = packed(name)
    got  = array(name)
    if got != want:
        print("balance gate: %s is %r in balance.h but %r in balance.json" % (name, got, want))
        bad += 1
if bad:
    sys.exit(1)
print("check.sh: %d battle tuning constants agree between balance.h and balance.json"
      % (len(SCALARS) + len(ARRAYS)))
PYGATE
  else
    echo "check.sh: python3 not found, SKIPPING the balance-constant gate" >&2
  fi
fi

# --- grep gates (each is enabled by the plan commit that makes it true) ---
# The screen state machine owns navigation. TREE-WIDE since P2-C11d: the
# exception this gate used to carry (dev/godmode.cpp had its own unrelated
# `static uint8_t s_screen` for the console sub-screen, 39 hits) was removed by
# renaming that variable to s_console_page, so the plan's own wording now holds
# with no scoping and no false positives to teach the next person to disable it.
if [ -d "$SKETCH/src" ]; then
  # `|| true` on BOTH greps: this script runs under `set -o pipefail`, so a grep
  # that finds nothing returns 1, fails the pipeline and — with `set -e` — kills
  # the run at the exact moment the gate should be approving. A gate must not be
  # able to abort the thing it guards.
  n=$( { grep -rE 's_screen[[:space:]]*=' "$SKETCH/src" --include='*.cpp' || true; } \
        | { grep -v 'app/state_machine\.cpp' || true; } | wc -l )
  [ "$n" -eq 0 ] || fail "navigation assigned outside app/state_machine.cpp ($n)"
fi

# The ui.cpp monolith no longer dispatches screens; the table does. True since P2-C11c.
if [ -f "$SKETCH/src/ui/ui.cpp" ]; then
  n=$(grep -c 'case SCR_' "$SKETCH/src/ui/ui.cpp" || true)
  [ "${n:-0}" -eq 0 ] || fail "ui.cpp still switches on ScreenId ($n cases)"
fi

# Every spec section 6 state has a table row with all five hooks (P2-C11d).
# The cheap shape check lives here; tests/test_statemachine.cpp is the real one.
if [ -f "$SKETCH/src/ui/screen_table.cpp" ]; then
  n=$(grep -c 'nullptr,' "$SKETCH/src/ui/screen_table.cpp" || true)
  [ "${n:-0}" -eq 0 ] || fail "screen_table.cpp still has a null hook ($n)"
fi

# Rendering reads views, not models (audit risk 9). True since P2-C11c.
# NOTE the `if`: `grep -q ... && fail ...` looks equivalent but returns grep's
# exit status when it finds nothing, which under `set -e` kills this script in
# the PASSING case — a gate that aborts the run instead of approving it.
for f in petfx actfx; do
  if [ -f "$SKETCH/src/ui/$f.cpp" ]; then
    if grep -qE '#include.*(sim|genome)\.h' "$SKETCH/src/ui/$f.cpp"; then
      fail "$f.cpp reaches past PetView into the model"
    fi
  fi
done

# ONE esp_random() IN THE WHOLE FIRMWARE (plan §1.1 / §1.4, core/rng.h:12-14).
# app.cpp seeds every stream once with it; everything else draws from rng.h, so
# a Pebble is reproducible from its seed. ADDED BY THE P3-C5 FOLLOW-UP: the plan
# listed this gate from P2-C2 but it had never been in this file, and as the
# plan worded it - `grep -c "genome_rand|esp_random" src/game src/minigames` == 0
# - it could never have passed, because genome_rand is DEFINED in
# src/game/genome.cpp. Comment lines are dropped: the rule is about calls, and
# five headers discuss the rule in prose.
if [ -d "$SKETCH/src" ]; then
  n=$( { grep -rn "esp_random" "$SKETCH/src" --include='*.cpp' --include='*.h' || true; } \
        | { grep -vE '^[^:]+:[0-9]+:[[:space:]]*//' || true; } \
        | { grep -v 'app/app\.cpp' || true; } | wc -l )
  [ "$n" -eq 0 ] || fail "esp_random outside app/app.cpp ($n)"
fi

# --- P4-C2: THE THREE BATTLE GATES ---------------------------------------
# 1. src/game IS PURE. Until P4-C2 this was enforced only by tests/Makefile
#    compiling sketch sources with no Arduino include path, so an
#    `#include <Arduino.h>` in a game module surfaced as a confusing compile
#    error inside `make -C tests check` and not as a named gate. The plan's
#    ground truth said this gate already existed; `grep -n Arduino tools/check.sh`
#    returned nothing, so it did not. It matches #include LINES ONLY: five game
#    headers discuss the rule in prose and must not trip it.
if [ -d "$SKETCH/src/game" ]; then
  n=$( { grep -rnE '^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"][^>"]*(Arduino\.h|u8g2|gfx\.h|render\.h)' \
          "$SKETCH/src/game" --include='*.cpp' --include='*.h' || true; } | wc -l )
  [ "$n" -eq 0 ] || fail "src/game includes a hardware or renderer header ($n)"
fi

# 2. THE BATTLE DRAWS ONLY FROM ITS OWN Rng. game/battle.h's determinism
#    contract is that every draw comes from BattleState.rng, seeded from one
#    u32, so a replay is exact and two peers stay in lockstep. A named global
#    stream is shared with the rest of the firmware, so a care roll on one
#    device would move the other device's damage numbers. This is the exact
#    failure mode "the AI reaches for RNG_BATTLE and the logs stop
#    reproducing", and it is mechanically checkable. Comment lines are dropped:
#    battle.h names these functions in prose on purpose.
if ls "$SKETCH"/src/game/battle* >/dev/null 2>&1; then
  n=$( { grep -rnE '\b(rng_u32|rng_below|rng_chance_permille|rng_seed|rng_seed_all|esp_random)[[:space:]]*\(' \
          "$SKETCH"/src/game/battle* || true; } \
        | { grep -vE '^[^:]+:[0-9]+:[[:space:]]*//' || true; } | wc -l )
  [ "$n" -eq 0 ] || fail "game/battle* draws from a named RNG stream ($n) - it must use st.rng only"
fi

# 3. THE NINE STEPS ARE CALLED IN ORDER. NARROW BY DESIGN, and the narrowness
#    is the point: this extracts the digits of the battle_s<N>_ calls inside
#    battle_step_round() and requires 1..9 ascending, squeezing repeats (step 2
#    is asked for both sides). It catches a REORDERING and it proves NOTHING
#    about whether each function does what its name says - tests/test_battle.cpp
#    is what does that. Do not widen this comment.
if [ -f "$SKETCH/src/game/battle.cpp" ]; then
  order=$(sed -n '/^BattleStepResult battle_step_round/,/^}/p' "$SKETCH/src/game/battle.cpp" \
          | { grep -oE 'battle_s[1-9]_' || true; } | { grep -oE '[1-9]' || true; } \
          | tr -d '\n' | tr -s '1-9')
  [ "$order" = "123456789" ] || \
    fail "battle_step_round() does not call the spec section 14 steps once each in order (got '$order')"
fi

# --- P4-C3: THE AI NEVER TAKES A MUTABLE BATTLE STATE --------------------
# game/battle_ai.h's two-stream argument rests on the AI being unable to write
# ANY byte of BattleState - not merely unable to draw from BattleState.rng. The
# const parameter on battle_ai_choose() is what enforces that today (rng_next()
# will not bind to a const member, so a draw from the battle stream does not
# compile), and this gate is what stops a helper added next year from quietly
# taking a mutable one and reopening the hole.
#
# NARROW, and the narrowness is the point: it matches DECLARATIONS ONLY -
# `BattleState&` or `BattleState &` not preceded by `const` - and it proves
# NOTHING about what the AI does with the const reference it gets. Gate 2 above
# already covers this file for named RNG streams, since it globs src/game/battle*.
# Comment lines are dropped: battle_ai.h discusses the rule in prose.
if ls "$SKETCH"/src/game/battle_ai.* >/dev/null 2>&1; then
  n=$( { grep -rnE 'BattleState[[:space:]]*&' "$SKETCH"/src/game/battle_ai.* || true; } \
        | { grep -vE '^[^:]+:[0-9]+:[[:space:]]*//' || true; } \
        | { grep -vE 'const[[:space:]]+BattleState[[:space:]]*&' || true; } | wc -l )
  [ "$n" -eq 0 ] || fail "game/battle_ai takes a MUTABLE BattleState ($n) - it must only ever see a const one"
fi

# --- P4-C5: THE FOUR NETWORKING GATES -------------------------------------
# THE TWO LISTS BELOW ARE DEFINED ONCE AND USED BY ALL OF THEM. Gates 1, 2 and
# 2b read the same names, because two lists that must agree is the disagreement
# this project keeps finding.
PURE_NET="protocol.h protocol.cpp session.h session.cpp battle_link.h battle_link.cpp transport.h transport_loopback.cpp net_classify.h net_classify.cpp wifi_scanner.h wifi_scanner.cpp rxring.h rxring.cpp discovery.h discovery.cpp"
PURE_NET_CPP="protocol.cpp session.cpp battle_link.cpp transport_loopback.cpp net_classify.cpp wifi_scanner.cpp rxring.cpp discovery.cpp"
# transport_espnow.* is IMPURE and that is HONEST rather than a dodge: esp_now.h's
# two callback typedefs have no user-context argument at all, so the sink a
# callback posts into is forced to be file-scope. A device has one radio and a
# singleton is the truth. What is NOT a singleton is the mechanism - the ring is
# rxring.cpp and the peer table is discovery.cpp, both pure, both caller-owned,
# both driven by a host binary.
IMPURE_NET="ble_social.h ble_social.cpp net.h net.cpp webui.h webui.cpp transport_espnow.h transport_espnow.cpp"

# 1. THE PURE NETWORKING MODULES ARE PURE, AND THE SCOPING IS BY FILENAME
#    BECAUSE THE DIRECTORY IS MIXED. networking/ble_social.cpp:45-53
#    legitimately includes Arduino.h, esp_bt_device.h and the BLE headers, and
#    net.cpp and webui.cpp are device modules too - so a directory-wide gate
#    here could only ever be a gate somebody disables. The list below is the
#    files that are compiled by tests/Makefile and must stay host-compilable;
#    each is checked only if it exists, so the entries for P4-C5's second half
#    (session, battle_link, transport) arm themselves when those files land.
#    It matches #include LINES ONLY: protocol.h discusses ESP-NOW in prose and
#    must not trip it.
if [ -d "$SKETCH/src/networking" ]; then
  for f in $PURE_NET; do
    [ -f "$SKETCH/src/networking/$f" ] || continue
    n=$( { grep -nE '^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"][^>"]*(Arduino\.h|esp_now|esp_wifi|WiFi|u8g2|gfx\.h|render\.h)' \
            "$SKETCH/src/networking/$f" || true; } | wc -l )
    [ "$n" -eq 0 ] || fail "networking/$f includes a radio, hardware or renderer header ($n)"
  done
fi

# 2. NO FILE-SCOPE MUTABLE STATE IN THE PURE NETWORKING MODULES. This is what
#    lets one host process run two endpoints against each other, exactly as
#    game/battle.h says of battle.cpp - and P4-C5's loopback depends on it.
#    NARROW, AND THE NARROWNESS IS THE POINT: it matches a line that begins
#    `static` and contains NO `(` (so every static function is excluded) and no
#    `const`/`constexpr` (so a lookup table in flash is excluded). It catches
#    `static uint16_t s_seq;` and it proves NOTHING about a mutable that a
#    function returns a reference to; the tests are what prove the modules are
#    re-entrant across two endpoints.
if [ -d "$SKETCH/src/networking" ]; then
  for f in $PURE_NET_CPP; do
    [ -f "$SKETCH/src/networking/$f" ] || continue
    n=$( { grep -nE '^[[:space:]]*static[[:space:]]' "$SKETCH/src/networking/$f" || true; } \
          | { grep -v '(' || true; } \
          | { grep -vE '\b(const|constexpr)\b' || true; } | wc -l )
    [ "$n" -eq 0 ] || fail "networking/$f holds file-scope mutable state ($n) - two endpoints could not share a process"
  done
fi

# 2b. EVERY FILE UNDER src/networking IS CLASSIFIED, so gate 1's filename
#    scoping cannot silently under-cover. Gate 1 lists the PURE files by name
#    for the reason above, and its failure mode was SILENCE: a ninth file - say
#    P7's esp_now_transport.cpp - would get no purity gate at all and nothing
#    would say so. This gate turns that into a red line. A new file must be
#    added to PURE_NET (or PURE_NET_CPP for a .cpp) or to IMPURE_NET, which is the
#    declared list of device modules that legitimately include Arduino.h and
#    the radio headers. Adding a name to IMPURE_NET is a deliberate act with a
#    reviewer looking at it; forgetting is now impossible.
if [ -d "$SKETCH/src/networking" ]; then
  # And the two lists must not drift from each other either: every .cpp named
  # pure must also be in the list gate 2 walks, or a pure module would be
  # checked for Arduino.h and not for file-scope mutable state.
  for f in $PURE_NET; do
    case "$f" in
      *.cpp) case " $PURE_NET_CPP " in *" $f "*) ;;
               *) fail "check.sh: networking/$f is in PURE_NET but not PURE_NET_CPP - gate 2 would skip it" ;;
             esac ;;
    esac
  done
  for path in "$SKETCH"/src/networking/*; do
    [ -f "$path" ] || continue
    b="$(basename "$path")"
    case " $PURE_NET $IMPURE_NET " in
      *" $b "*) ;;
      *) fail "networking/$b is in neither check.sh's PURE_NET nor its IMPURE_NET list - classify it (a pure module is gated for Arduino.h and file-scope mutable state; an impure one is a declared device module)" ;;
    esac
  done
fi

# 3. A TRIPWIRE, AND IT IS LABELLED AS ONE. spec section 15 says never trust a
#    peer's object; game/validate.h says the wire path REFUSES where
#    ui/screen_battle.cpp's copy_from_box() repairs. The word "repair" matches
#    zero lines under src/networking today and this gate's whole job is to fail
#    the day somebody teaches the wire path to mend what a peer sent.
if [ -d "$SKETCH/src/networking" ]; then
  n=$( { grep -rin "repair" "$SKETCH/src/networking" || true; } | wc -l )
  [ "$n" -eq 0 ] || fail "src/networking mentions repairing ($n) - the wire path refuses, it never mends"
fi

# --- P5-C1: THE TWO SCANNER GATES -----------------------------------------
# 1. NO STATION ASSOCIATION ANYWHERE (scan-only Wi-Fi, spec section 68 r5).
#    ARMED AT P5-C1, after three years dormant. It was dormant for a good
#    reason - it could not pass while networking/net.cpp had the call - and it
#    had TWO defects that only showed up when somebody tried to arm it:
#
#    (a) IT ABORTED IN THE PASSING CASE. It read
#          n=$(grep -rn "WiFi\.begin(" "$SKETCH/src" | wc -l)
#        and this script runs under `set -euo pipefail`. With ZERO hits grep
#        exits 1, pipefail propagates that through `| wc -l`, the assignment
#        inherits it and `set -e` kills the run BEFORE `echo "GATE OK"` - the
#        exact failure this file warns about twice in its own comments
#        (a gate must not be able to abort the thing it guards). Measured: a
#        src/ with no hits exited 1 with no message at all. The `|| true` below
#        is the house form used by every other grep gate here.
#
#    (b) EVEN CORRECTED IT FAILED ON PROSE. networking/net.h used to name the
#        association call twice in comments - once in its banner rule and once
#        as a phase's trailing comment - so the corrected gate counted 2 and
#        failed on a tree with no call sites. THE FIX IS TO REWORD THE PROSE,
#        NOT TO FILTER COMMENTS OUT: the comment-dropping filter the other
#        gates use catches a whole-line comment and not a trailing one, and
#        adding a filter here would reintroduce, in a new shape, the kind of
#        carve-out the P4-C6 follow-up removed from this very gate.
#
#    THE CARVE-OUT IS GONE (P4-C6 follow-up). This read `| grep -v
#    creator_server`, an exception the plan does not authorise: plan line 72
#    specifies the gate as `grep -c "WiFi.begin" src/` == 0 flat. It also
#    excused a file that does not exist, and would have excused nothing anyway:
#    the AP portal uses WiFi.softAP() and never the association call.
if [ -d "$SKETCH/src" ]; then
  n=$( { grep -rn "WiFi\.begin(" "$SKETCH/src" || true; } | wc -l )
  [ "$n" -eq 0 ] || fail "WiFi.begin() in src ($n) - the product never associates to a station"
fi

# 2. THE SCAN RESULT CARRIES NO NETWORK IDENTITY (spec section 44).
#    networking/wifi_scanner.h is where ScanResult is declared, and the claim is
#    that the struct a scan hands the game holds a salted hash, a signal
#    strength and an abstract category and NOTHING that identifies a network.
#    tests/test_exploration_hash.cpp pins sizeof AND every member's offset,
#    which a `sizeof == 8` alone could not: swapping the two reserved bytes for
#    two bytes of a network name keeps sizeof at 8. This gate is the half that
#    cannot be satisfied by accident - the words themselves may not appear in
#    the file, in a field, in a parameter or in a comment.
#
#    THE PROSE COST IS DELIBERATE AND IS NOT A DODGE: the rule is spelled out in
#    full in networking/net_classify.h, which is the file that legitimately
#    handles both (it is where they are destroyed), and wifi_scanner.h's banner
#    points at it. A gate that its own subject's comments cannot satisfy is a
#    gate somebody eventually filters.
if [ -f "$SKETCH/src/networking/wifi_scanner.h" ]; then
  n=$( { grep -rin "ssid\|bssid" "$SKETCH/src/networking/wifi_scanner.h" || true; } | wc -l )
  [ "$n" -eq 0 ] || fail "networking/wifi_scanner.h names a network identifier ($n) - ScanResult carries a salted hash and nothing that identifies a network (spec 44)"
fi

# 3. THE TWO BYTES THE SCAN RESULT DOES NOT USE STAY ZERO (spec section 44).
#    Gate 2 and tests/test_exploration_hash.cpp's offset case both catch a RENAME:
#    give those bytes a network's name and either the words appear in the header or
#    the test stops compiling against a member that no longer exists. NEITHER CAN
#    SEE A VALUE, and that gap was measured on this tree at the phase-5 exit rather
#    than argued about: assigning them two bytes of a beacon name inside net.cpp's
#    read path left `GATE OK` and `ALL PASS 37/37`. net.cpp is on the IMPURE list,
#    tests/Makefile never compiles it, and the only host coverage of those bytes is
#    a fake driver that zeroes them itself - so the leak that needs no rename had no
#    gate at all, and two bytes of every beacon name would have flowed into 136 B of
#    globals by way of the NETWORK screen's job.
#
#    This one is greppable where a host test is not. Count every assignment to one
#    of those bytes under src/networking; count the ones whose right-hand side is a
#    literal zero; require the two counts to agree. No comment filter and no
#    exception list: a carve-out here would be the mistake gate 1 above records.
if [ -d "$SKETCH/src/networking" ]; then
  n_all=$(  { grep -rn "reserved\[[01]\][[:space:]]*=" "$SKETCH/src/networking" || true; } | wc -l )
  n_zero=$( { grep -rn "reserved\[[01]\][[:space:]]*=[[:space:]]*0[uU]*[[:space:]]*;" "$SKETCH/src/networking" || true; } | wc -l )
  [ "$n_all" -eq "$n_zero" ] || fail "a ScanResult padding byte is assigned something other than 0 ($((n_all - n_zero)) of $n_all under src/networking) - those bytes are padding, and a network name is exactly what fits in them (spec 44)"
fi

# --- P6-C1: ONE PLACE DEFINES A PIN (spec section 68 r3) ------------------
# "No pin invented" is a rule the plan repeats three times - at P1 (line 375),
# at the hardware-abstraction row (line 178) and at the motion bullet (line 723)
# - and until this gate it was enforced by nobody. Decisions D1 (the button and
# LED map) and D8 (the piezo GPIO) are both OPEN, so the one thing that has to
# stay true while the owner is still holding a soldering iron is that every GPIO
# number in the firmware is in core/config.h section 2 and nowhere else. A
# second `#define PIN_...` in a driver is how a pin map comes to have two
# answers, and the owner would be reading the wrong one.
#
# NARROW, AND THE NARROWNESS IS THE POINT: it matches #define LINES ONLY, so a
# file may name PIN_PIEZO in prose - hardware/audio.cpp does, several times -
# and it says NOTHING about a bare number handed to pinMode() or ledcAttach().
# That is a different and much harder grep, and claiming it here would be the
# "sentence wider than the tree" this project keeps finding. What it catches is
# a SECOND DEFINITION, which is the failure that survives a review.
if [ -d "$SKETCH/src" ]; then
  n=$( { grep -rnE '^[[:space:]]*#[[:space:]]*define[[:space:]]+PIN_' "$SKETCH/src" \
          --include='*.cpp' --include='*.h' || true; } \
        | { grep -v 'core/config\.h' || true; } | wc -l )
  [ "$n" -eq 0 ] || fail "a PIN_ macro is defined outside core/config.h ($n) - while D1 and D8 are open the pin map has exactly one home (spec section 68 r3)"
fi

# --- P6-C2: ONE PLACE PAYS THE ACTIVITY SCORE -----------------------------
# game/activity.h's whole anti-farm argument rests on the score being NOTED in
# several places and PAID in exactly one: the notes are capped, day-bucketed and
# deduplicated inside the pure module, and app_pay_activity() is where the
# result reaches app_award_xp(XP_SRC_CARRY) and the active Pebble's happiness.
# A screen that drained the gain itself would pay a reward outside the XP funnel
# - the funnel is what puts the ledger decrease on flash - and would do it with
# no place left to look for it. That is the same failure mode the navigation
# gate above exists for, so it gets the same shape of gate.
#
# NARROW ON PURPOSE, exactly like the PIN_ gate: it counts CALL SITES of
# act_take_gain() and says nothing about who calls act_note_*(), which is meant
# to be several files. One call site, and it is the one in app/app.cpp.
if [ -f "$SKETCH/src/game/activity.h" ]; then
  n=$( { grep -rnE '\bact_take_gain[[:space:]]*\(' "$SKETCH/src" \
          --include='*.cpp' --include='*.h' || true; } \
        | { grep -vE '^[^:]+:[0-9]+:[[:space:]]*//' || true; } \
        | { grep -v 'game/activity\.h' || true; } \
        | { grep -v 'game/activity\.cpp' || true; } \
        | { grep -v 'app/app\.cpp' || true; } | wc -l )
  [ "$n" -eq 0 ] || fail "act_take_gain() is called outside app/app.cpp ($n) - the activity reward is paid in exactly one place (game/activity.h)"
fi

# --- P6-C3: THE POWER LADDER NEVER NAMES THE RADIO ------------------------
# THE CARRIED DEBT FROM THE PHASE-5 EXIT, AS A RED LINE. IDLE turns the radio
# off after two minutes and the NETWORK screen is the one consumer that can be
# mid-job: it owns a WifiScanJob and pumps it. A bare net_request(RADIO_OFF)
# from the power path leaves that job WSCAN_RUNNING with stopped == 0 - so
# wifi_scan_is_busy() goes on answering true to the very ladder that is trying
# to sleep, and the next poll reports FAILED to a player who caused nothing -
# while wifi_down()'s scanDelete() is what frees the driver's per-access-point
# array. wifi_scan_cancel() is the single path that stops the driver and
# releases the radio exactly once, and hardware/power.cpp reaches it by
# NAVIGATING: ui_home() runs the leaving screen's leave() hook.
#
# NARROW, AND THE NARROWNESS IS THE POINT, exactly like the PIN_ and
# act_take_gain() gates: it counts every mention of the radio API in
# hardware/power.* and says nothing about what any other file does with it.
# What it catches is the one-line "optimisation" that skips the navigation,
# which is the failure that survives a review because it looks more direct.
# Comment lines are dropped: power.h and power.cpp both explain the rule in
# prose and must not trip their own gate.
if ls "$SKETCH"/src/hardware/power.* >/dev/null 2>&1; then
  n=$( { grep -rnE '\b(net_request|net_service|WiFi\.|esp_wifi_|btStart|BLEDevice)' \
          "$SKETCH"/src/hardware/power.* || true; } \
        | { grep -vE '^[^:]+:[0-9]+:[[:space:]]*//' || true; } | wc -l )
  [ "$n" -eq 0 ] || fail "hardware/power.* names the radio directly ($n) - the power ladder releases the radio by navigating, so the owning screen's leave() hook cancels through wifi_scan_cancel() (hardware/power.h)"
fi

# --- P6-C3: THE EXPLORATION CLOCK SURVIVES A SLEEP ------------------------
# ui_explore_clock() is the ONE place game/cooldowns.h's monotonic millisecond
# clock comes from, and while gt_cal_state() == CAL_UNSET the cooldown table's
# deadlines are the only thing between an uncalibrated device and free
# encounters. millis() is UPTIME: it restarts at a deep-sleep wake and only
# carries a light sleep because esp_timer happens to be resynchronised from the
# RTC on the way out. gt_mono32() IS the RTC counter (hardware/gametime.h).
#
# This extracts the body of ui_explore_clock() and requires that it hands out
# gt_mono32() and not millis(). It proves NOTHING about the other millis()
# readers in ui.cpp, which are animation phase and modal lifetimes and are
# meant to stay: this is about the one reading that decides a game outcome.
if [ -f "$SKETCH/src/ui/ui.cpp" ]; then
  body=$(sed -n '/^void ui_explore_clock/,/^}/p' "$SKETCH/src/ui/ui.cpp")
  if [ -z "$body" ]; then
    fail "ui_explore_clock() not found in ui.cpp - the gate that keeps the cooldown clock sleep-proof has lost its subject"
  fi
  n=$( { printf '%s\n' "$body" | grep -nE 'now_ms[[:space:]]*=[[:space:]]*millis' || true; } | wc -l )
  [ "$n" -eq 0 ] || fail "ui_explore_clock() hands out millis() ($n) - the cooldown table's deadlines must be measured in gt_mono32(), which survives a sleep"
  n=$( { printf '%s\n' "$body" | grep -nE 'now_ms[[:space:]]*=[[:space:]]*gt_mono32' || true; } | wc -l )
  [ "$n" -eq 1 ] || fail "ui_explore_clock() does not hand out gt_mono32() - see hardware/gametime.h on which clock decides a game outcome"
fi

# --- P6-C4: THE MONOTONIC CLOCK ITSELF, NOT JUST ITS CALLER ---------------
# The gate above polices ui_explore_clock(), the CALLER. The line it depends on
# had nothing on it at all: gt_mono_ms()'s ARDUINO branch is the whole of P6-C3's
# "a monotonic source that survives sleep", and reverting it to millis() - the
# exact regression that chunk was written to prevent - is invisible to the host
# suite, because no host binary compiles that branch. Measured: the revert was
# planted and `make -C tests check` still reported ALL PASS.
#
# So the branch is pinned here. esp_rtc_get_time_us() is the RTC slow-clock
# counter and runs through a light sleep AND a deep sleep; millis() is
# esp_timer, i.e. uptime, and restarts at a deep-sleep wake. The HOST branch is
# deliberately left alone - it reads gt_host_millis32(), which is the test's own
# injected clock and has nothing to do with Arduino's millis().
if [ -f "$SKETCH/src/hardware/gametime.cpp" ]; then
  body=$(sed -n '/^static uint64_t gt_mono_ms/,/^}/p' "$SKETCH/src/hardware/gametime.cpp")
  if [ -z "$body" ]; then
    fail "gt_mono_ms() not found in gametime.cpp - the gate that keeps the monotonic clock sleep-proof has lost its subject"
  fi
  dev=$(printf '%s\n' "$body" | sed -n '/#if defined(ARDUINO)/,/#else/p' \
        | { grep -vE '^[[:space:]]*//' || true; })
  n=$( { printf '%s\n' "$dev" | grep -nE 'esp_rtc_get_time_us[[:space:]]*\(' || true; } | wc -l )
  [ "$n" -ge 1 ] || fail "gt_mono_ms() does not read esp_rtc_get_time_us() on the device - the monotonic clock every cooldown deadline is measured against must survive a sleep (hardware/gametime.h)"
  n=$( { printf '%s\n' "$dev" | grep -nE '(^|[^_[:alnum:]])millis[[:space:]]*\(' || true; } | wc -l )
  [ "$n" -eq 0 ] || fail "gt_mono_ms()'s device branch reads millis() ($n) - millis() is UPTIME and restarts at a deep-sleep wake; the uncalibrated cooldown table would freeze (game/cooldowns.h)"
fi

# --- P7-C1: THE PEER TABLE CARRIES NO HARDWARE ADDRESS (spec sections 43, 44)
# The same gate wifi_scanner.h carries for a network's name, pointed at the
# module that would carry a peer's. networking/discovery.{h,cpp} is what the
# game, the UI and anything persisted can see; the six bytes of a sender's
# address live in ONE private array inside networking/transport_espnow.cpp and
# the only thing that crosses the seam is an opaque slot index.
#
# THE SHAPE NOT TO COPY IS IN THIS TREE RIGHT NOW: core/nt_types.h's BlePeerInfo
# has the raw address as its FIRST member and ble_social.cpp copies it out of
# the scan callback into the table the game reads. This gate is what stops that
# shape being reproduced under a new name.
#
# THE PROSE COST IS DELIBERATE AND IS NOT A DODGE: the rule is spelled out in
# full in networking/transport_espnow.h, which is the file that legitimately
# handles the address, and discovery.h's banner points at it. A gate its own
# subject's comments cannot satisfy is a gate somebody eventually filters.
#
# AND ITS LIMIT, STATED: it catches a RENAME and it CANNOT SEE A VALUE - six
# bytes smuggled through two uint32_t fields would leave it green, exactly as
# the ScanResult finding above measured. tests/test_discovery.cpp pins sizeof
# and every offset of DiscPeer for that half.
for f in discovery.h discovery.cpp; do
  if [ -f "$SKETCH/src/networking/$f" ]; then
    n=$( { grep -ric "mac" "$SKETCH/src/networking/$f" || true; } )
    [ "$n" -eq 0 ] || fail "networking/$f names a hardware address ($n) - a DiscPeer carries a device id, a name, capabilities and an opaque slot, and nothing that identifies a piece of hardware (spec 43/44); the prose belongs in networking/transport_espnow.h"
  fi
done

# --- P7-C1: ONE FILE OWNS THE ESP-NOW API --------------------------------
# networking/net.h's rule is "exactly one module touches radio lifecycle", and
# transport.h promised at P4-C5 that "P7's transport_espnow.cpp is the only file
# in the tree that will include esp_now.h". This is that promise as a red line.
# It counts CALL SITES of the API (esp_now_<something>) rather than the include,
# because a second file that reached the API through a helper would pass an
# include gate. Prose naming the header is untouched: "esp_now.h" has no
# trailing identifier and does not match. transport_espnow.h is exempt WITH its
# .cpp and not instead of it: it is the declared owner's header, it is where
# discovery.h's privacy gate says the prose cost is paid, and a header does not
# call anything. Everywhere else the rule is the P5-C1 one - REWORD THE PROSE,
# do not add a filter - which is why networking/net.h names the failure and not
# the function.
if [ -d "$SKETCH/src" ]; then
  n=$( { grep -rnE 'esp_now_[a-z_]+[[:space:]]*\(' "$SKETCH/src" \
          --include='*.cpp' --include='*.h' || true; } \
        | { grep -v 'networking/transport_espnow\.' || true; } \
        | { grep -vE '^[^:]+:[0-9]+:[[:space:]]*//' || true; } | wc -l )
  [ "$n" -eq 0 ] || fail "the ESP-NOW API is called outside networking/transport_espnow.cpp ($n) - one file owns the radio API, and it is the one file tests/Makefile never compiles"
fi

# --- P7-C1: THE SESSION LOGIC HAS NO CONDITIONAL COMPILATION -------------
# networking/transport.h's whole claim for the seam is "ONE SEAM, TWO
# IMPLEMENTATIONS, NO #ifdef IN THE SESSION LOGIC ... which is why
# networking/session.cpp and networking/battle_link.cpp contain no conditional
# compilation at all". Until P7-C1 that was a sentence about a tree with no
# radio in it, which is the cheapest kind of true. Now that a real radio sits
# behind the seam it is a property that can be broken by one line, and the way
# it breaks is somebody making the radio work by editing the session instead of
# widening the seam. Neither file has a header guard, so the count is zero and
# not two: they are .cpp files.
for f in session.cpp battle_link.cpp; do
  if [ -f "$SKETCH/src/networking/$f" ]; then
    n=$( { grep -cE '^[[:space:]]*#[[:space:]]*(if|ifdef|ifndef|else|elif|endif)' \
            "$SKETCH/src/networking/$f" || true; } )
    [ "$n" -eq 0 ] || fail "networking/$f contains conditional compilation ($n) - the transport seam exists so the session logic never needs any; widen networking/transport.h instead (transport.h:5-11)"
  fi
done

# --- P7-C2: A SESSION IS OPENED IN EXACTLY ONE PLACE ----------------------
# THE CONSENT GATE, AS A RED LINE. ui/screen_link.h's claim is that until the
# local player has chosen a peer, chosen an operation and pressed A there is no
# Session on this device at all - not an idle one, NONE. That is only true while
# ONE function in the whole firmware calls session_init(), and it is
# ui/screen_link.cpp's open_session(). A second caller - a screen that
# "prepares" a session while browsing, a diagnostic that opens one to measure
# the link - would make the sentence false without changing a line of
# ui/screen_link.cpp, and nothing else would notice.
#
# NARROW, exactly like the PIN_ and act_take_gain() gates: it counts CALL SITES
# of session_init() and session_start() under src/ and says nothing about who
# calls session_poll(), which the battle screen legitimately drives through a
# ui.h seam. networking/ is exempt because that is the module the functions
# belong to; ui/screen_link.cpp is the one consumer. Comment lines are dropped:
# three headers discuss the rule in prose.
if [ -d "$SKETCH/src" ]; then
  n=$( { grep -rnE '\b(session_init|session_start)[[:space:]]*\(' "$SKETCH/src" \
          --include='*.cpp' --include='*.h' || true; } \
        | { grep -v 'networking/' || true; } \
        | { grep -v 'ui/screen_link\.cpp' || true; } \
        | { grep -vE '^[^:]+:[0-9]+:[[:space:]]*//' || true; } | wc -l )
  [ "$n" -eq 0 ] || fail "a peer session is opened outside ui/screen_link.cpp ($n) - consent is what opens a session, and the A press on the LINK card is the only thing that may (ui/screen_link.h)"
fi

# --- P7-C3: THE REWARD GATE HAS EXACTLY ONE READER ------------------------
# networking/session.h's session_rewards_authorised() is true only where BOTH
# endpoints agreed the outcome AND the final hash, and P7-C3's whole reward rule
# is that a linked battle pays where that is true and nowhere else. The flag is
# read ONCE, in ui/screen_link.cpp's link_battle_status(), and every other
# consumer asks that function. A second reader is how "rewards only on
# BATTLE_END(OK)" becomes two rules that can disagree.
#
# AND ITS LIMIT, STATED: it cannot see a screen that decides a reward from
# BattleState.outcome directly and never mentions this function at all. What it
# buys is that there is exactly ONE DOOR, so adding that second rule has to be a
# visible edit to link_battle_status() or to ui/screen_battle.cpp's won_now();
# tests/test_link_screen.cpp's desync and dead-link cases are the other half.
if [ -f "$SKETCH/src/networking/session.h" ]; then
  n=$( { grep -rnE '\bsession_rewards_authorised[[:space:]]*\(' "$SKETCH/src" \
          --include='*.cpp' --include='*.h' || true; } \
        | { grep -v 'networking/session\.h' || true; } \
        | { grep -v 'ui/screen_link\.cpp' || true; } \
        | { grep -vE '^[^:]+:[0-9]+:[[:space:]]*//' || true; } | wc -l )
  [ "$n" -eq 0 ] || fail "session_rewards_authorised() is read outside ui/screen_link.cpp ($n) - the linked battle's reward gate is one expression in one place (ui/screen_link.cpp link_battle_status())"
fi

# --- P7-C3: THE BATTLE SCREEN REACHES THE SESSION THROUGH ui.h ------------
# ui/screen_battle.cpp runs the engine and ui/screen_link.cpp owns the session,
# and the six calls between them go through ui.h. That is not tidiness: it is
# what keeps tests/test_battle_screen.cpp linking the battle screen and the
# things it drives and NOTHING else - the link line would otherwise have to drag
# session.o, battle_link.o, discovery.o and protocol.o into a binary whose whole
# argument is that it cannot start passing because some other module happened to
# be there.
if [ -f "$SKETCH/src/ui/screen_battle.cpp" ]; then
  n=$( { grep -nE '^[[:space:]]*#[[:space:]]*include[[:space:]]*"[^"]*(screen_link|networking/)' \
          "$SKETCH/src/ui/screen_battle.cpp" || true; } | wc -l )
  [ "$n" -eq 0 ] || fail "ui/screen_battle.cpp includes the LINK screen or a networking header ($n) - the linked battle's session is reached through the ui.h seams (ui.h, THE LINKED BATTLE'S SEAMS)"
fi

echo "GATE OK"
