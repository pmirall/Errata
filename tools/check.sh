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
# THIRTEEN NUMBERS LIVE IN TWO PLACES AND NOTHING COMPARED THEM. Every one of
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
# It compares VALUES, not formatting: the header writes (-2) and (+2) where the
# JSON writes -2 and 2, and the two files disagree on two names on purpose
# (JSON K is BATTLE_K, JSON LEVEL_MAX is XP_LEVEL_MAX), so the map is explicit.
# TYPE_MUL_NUM/DEN are arrays and are compared element by element.
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
}
ARRAYS = ["TYPE_MUL_NUM", "TYPE_MUL_DEN"]

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

bad = 0
for jname, hname in sorted(SCALARS.items()):
    want = pack.get(jname)
    got  = macro(hname)
    if got is None:
        print("balance gate: %s is not a plain #define in balance.h" % hname); bad += 1
    elif got != want:
        print("balance gate: %s is %r in balance.h but %s is %r in balance.json"
              % (hname, got, jname, want)); bad += 1
for name in ARRAYS:
    want = pack.get(name)
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
PURE_NET="protocol.h protocol.cpp session.h session.cpp battle_link.h battle_link.cpp transport.h transport_loopback.cpp"
PURE_NET_CPP="protocol.cpp session.cpp battle_link.cpp transport_loopback.cpp"
IMPURE_NET="ble_social.h ble_social.cpp net.h net.cpp webui.h webui.cpp"

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

# P5-C1: no station association anywhere (scan-only Wi-Fi, spec §68 r5)
# if [ -d "$SKETCH/src" ]; then
#   n=$(grep -rn "WiFi\.begin(" "$SKETCH/src" | grep -v creator_server | wc -l); [ "$n" -eq 0 ] || fail "WiFi.begin outside creator_server ($n)"
# fi

echo "GATE OK"
