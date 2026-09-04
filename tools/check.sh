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

# P5-C1: no station association anywhere (scan-only Wi-Fi, spec §68 r5)
# if [ -d "$SKETCH/src" ]; then
#   n=$(grep -rn "WiFi\.begin(" "$SKETCH/src" | grep -v creator_server | wc -l); [ "$n" -eq 0 ] || fail "WiFi.begin outside creator_server ($n)"
# fi

echo "GATE OK"
