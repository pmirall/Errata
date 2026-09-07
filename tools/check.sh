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

  # --- THE INSTRUMENTS MUST STILL COMPILE (P9-C6) ---------------------------
  # tests/tools/ holds five binaries that answer a HUMAN rather than an
  # assertion - sprite_dump (the only instrument for "does this body look like a
  # creature", and now for the composited blink frame), corrupt_view,
  # balance_matrix, sim_days, creator_decode - and none of them is in
  # `make check`, correctly, because they assert nothing. But nothing BUILT them
  # either, so any of them could stop compiling without a word and the next
  # phase would find its review instrument gone at the moment it reached for it.
  # P9-C5 named this and declined to fix it on the grounds that adding a build
  # to the gate is a change to what every future commit pays for. It is the
  # phase exit's call to make: it costs about 4 s and the alternative is a review
  # instrument that rots in silence.
  make -C "$ROOT/tests" spritetool corrupttool balancetool pagetool \
    >/dev/null || fail "tests/tools/ instruments no longer build - they are the "\
"only answer this project has to \"does the art look right\" (tests/Makefile)"

  # --- THE SANITISER RUN (P8 exit) ------------------------------------------
  # THE ONE PROPERTY THE PLAIN SUITE STRUCTURALLY CANNOT CHECK, on the first
  # code in this product that reads bytes from outside the device.
  # networking/creator_parse.h's whole design is "length-bounded, never
  # NUL-bounded", three test names in tests/test_creator_api.cpp say "never
  # over-reads", and the harness mallocs every fuzz body at exactly its own
  # length SO THAT a one-byte over-read is a heap error - and until this line
  # existed nothing ever compiled with a sanitiser, so none of those three
  # could fail on the property they name.
  #
  # MEASURED: cp_skip_ws()'s `while (c.i < c.n)` changed to `<=` printed
  # ALL PASS 49/49 on the plain build and, here, AddressSanitizer:
  # heap-buffer-overflow at creator_parse.cpp:62. About 9 s from cold over four
  # binaries (tests/Makefile's ASAN_SET: the outside-input path and the
  # validator it ends in), so it runs on every commit rather than being an
  # opt-in nobody opts in to.
  #
  # SKIPPED WITH A WORD, never silently, if the toolchain has no libasan -
  # the same distinction the content and page gates draw. A gate that decides
  # it cannot run must SAY so, because a silent skip is a green run that
  # checked nothing.
  if printf 'int main(void){return 0;}\n' \
       | ${CXX:-g++} -fsanitize=address -x c++ - -o /dev/null >/dev/null 2>&1; then
    make -C "$ROOT/tests" asan || fail "the sanitiser run reported an error - read the AddressSanitizer report above; it is a real over-read, not a flaky test"
  else
    echo "check.sh: no -fsanitize=address support in ${CXX:-g++}, SKIPPING the sanitiser run" >&2
  fi
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
    # 3. THE PLAN'S "CONTENT_VERSION CHANGES WHEN THE JSON CHANGES", actually
    #    checked. It is the one content requirement a C++ test structurally
    #    cannot cover - test_content.cpp never sees the JSON - and the case that
    #    claimed it rejected 2 of 65,536 values. --selftest perturbs each
    #    tools/content/*.json in memory ONE AT A TIME and requires the hash to
    #    move for every one of them, requires it NOT to move for a `_`-prefixed
    #    design note, and requires the roster size to be in it. ~0.4 s.
    python3 "$ROOT/tools/gen_content.py" --selftest >/dev/null \
      || fail "CONTENT_VERSION does not change when tools/content/*.json does - "\
"read the named files above (run: python3 tools/gen_content.py --selftest)"
  else
    echo "check.sh: python3 not found, SKIPPING the content gate" >&2
  fi
else
  fail "tools/content is missing - src/data/*_table.h is generated from it and "\
"nothing can check them against it"
fi

# --- THE SPRITE GATE (P9-C1) -----------------------------------------------
# src/data/sprites_pebbles.h is GENERATED from tools/sprites/*.txt exactly the
# way src/data/*_table.h is generated from tools/content/*.json, and for the
# same reason: 8.6 KB of hex is not a diff anybody reads, so the ASCII art is
# what gets reviewed and the header has to be provably what that art produces.
# `--check` regenerates in memory and hard-fails on any byte of drift, so a
# hand-edited header AND an art edit that was never regenerated both fail here
# instead of shipping.
#
# THIS GATE IS WHY tools/sprites/ CAN BE HANDED TO FIVE PEOPLE AT ONCE. The
# generator refuses the whole tree - not just the file it is looking at - when a
# name in atlas.txt has no file, when a .txt file is in no manifest (art that
# would never ship), when the species block is out of order or has a hole, or
# when a body is bound to a species the pack does not have. Those are the
# failures that a per-file check cannot see and that a human reviewing 120
# frames will not see either.
#
# WHAT IT DOES NOT PROVE, SAID PLAINLY: nothing about whether a body looks like
# a creature. A 24x24 of noise passes every check in the generator and every
# case in tests/test_sprite_pipeline.cpp. `make -C tests spritetool &&
# ./bin/sprite_dump text <NAME>` renders the compiled atlas for a person, and it
# is a person that has to look.
#
# Skipped with a WORD if python3 is missing, and a HARD FAIL if tools/sprites is
# gone - the same distinction the content and page gates draw, for the same
# reason.
if [ -f "$ROOT/tools/gen_sprites.py" ]; then
  if [ -d "$ROOT/tools/sprites" ]; then
    if command -v python3 >/dev/null 2>&1; then
      python3 "$ROOT/tools/gen_sprites.py" --check --quiet >/dev/null \
        || fail "src/data/sprites_pebbles.h and tools/sprites/ have drifted apart "\
"(run: python3 tools/gen_sprites.py)"
      # AND THE PER-FILE ART CONTRACT, ADDED AT P9-C6. `--check` proves only that
      # the header matches the .txt files; it passes a frame 1 that is a byte
      # copy of frame 0, a body carrying another species' pixels, and a body
      # lifted off the floor - the three defects the art agents were told to run
      # `--self-check` for and that no gate ran. It costs 0.2 s and it refuses the
      # WHOLE tree (manifest, species binding, budget, identical frames), so the
      # command five hands were told to trust is the command the gate runs.
      # The host suite still owns the rest and names the body: see
      # tests/test_sprite_pipeline.cpp (no_two_species_bodies_hold_the_same_pixels,
      # every_body_has_ink_on_its_last_row, the_blink_closes_holes_...).
      python3 "$ROOT/tools/gen_sprites.py" --self-check >/dev/null \
        || fail "tools/sprites/ fails its own --self-check "\
"(run: python3 tools/gen_sprites.py --self-check)"
    else
      echo "check.sh: python3 not found, SKIPPING the sprite gate" >&2
    fi
  else
    fail "tools/sprites is missing - src/data/sprites_pebbles.h is generated "\
"from it and nothing else can check them against each other"
  fi
fi

# --- THE PAGE GATE (P8-C4) -------------------------------------------------
# src/data/index_html.h is GENERATED from web/creator/*, exactly the way
# src/data/*_table.h is generated from tools/content/*.json, and for the same
# reason: a 42 KB C string literal is not a diff anybody reads, so the SOURCE is
# what gets reviewed and the header has to be provably what that source
# produces. `--check` regenerates it in memory and hard-fails on any byte of
# drift, so a hand-edited header AND a page edit that was never regenerated both
# fail here instead of shipping.
#
# WHAT IT DOES NOT PROVE, SAID PLAINLY: nothing about whether the page WORKS. It
# cannot open a browser, run the sprite editor, or see that app.js reads a
# /api/schema field the device does not emit. The size half is the
# static_assert the generator emits (WEB_HTML_MAX); the ROUTE half is the gate
# immediately below; the BEHAVIOUR half is tools/page_test.mjs, and where that
# does not run it is not tested at all.
#
# Skipped with a WORD if python3 is missing, and a HARD FAIL if web/creator is
# gone - the same distinction the content gate above draws, for the same
# reason: a committed deliverable that has vanished means the gate's input was
# deleted, not that the phase has not landed.
if [ -f "$ROOT/tools/gen_index_html.py" ]; then
  if [ -d "$ROOT/web/creator" ]; then
    if command -v python3 >/dev/null 2>&1; then
      python3 "$ROOT/tools/gen_index_html.py" --check >/dev/null \
        || fail "src/data/index_html.h and web/creator/ have drifted apart "\
"(run: python3 tools/gen_index_html.py)"
    else
      echo "check.sh: python3 not found, SKIPPING the page gate" >&2
    fi
  else
    fail "web/creator is missing - src/data/index_html.h is generated from it "\
"and nothing else can check them against each other"
  fi
fi

# --- P8-C4: THE PAGE CALLS NO ROUTE THE DEVICE DOES NOT SERVE --------------
# The half `gen_index_html.py --check` structurally cannot see. It proves the
# header matches the source; it has no idea what the source ASKS FOR. A page
# that fetches /api/pebbles gets a 404 the user reads as "the creator is
# broken", and the byte-diff is green the whole time.
#
# NARROW ON PURPOSE: every '/api/...' literal in the page source must appear as
# a quoted registration under src/networking. It cannot see a path built by
# concatenation - so the page does not build one, and that is a rule this gate
# is the reason for.
if [ -d "$ROOT/web/creator" ] && [ -d "$SKETCH/src/networking" ]; then
  missing=0
  for r in $( { grep -rhoE "'/api/[a-z]+'" "$ROOT/web/creator" || true; } \
                | tr -d "'" | sort -u ); do
    if ! grep -rqF "\"$r\"" "$SKETCH/src/networking"; then
      echo "GATE FAIL: the creator page calls $r and no route under src/networking registers it" >&2
      missing=$((missing + 1))
    fi
  done
  [ "$missing" -eq 0 ] || fail "$missing page route(s) the device does not serve"

  # AND THE PAGE FETCHES NOTHING OFF-DEVICE. Spec section 33: "works offline
  # after connection", section 38: "serve only required assets". One document,
  # no CDN, no font, no analytics - and an absolute URL in the page is the one
  # way that stops being true without anything else changing.
  n=$( { grep -rnoE "(https?:)?//[a-z0-9.-]+\.[a-z]{2,}" "$ROOT/web/creator" \
          --include='*.js' --include='*.html' || true; } | wc -l )
  [ "$n" -eq 0 ] || fail "the creator page names an off-device host ($n) - it must work offline after connection (spec section 33)"
fi

# --- P8-C4: THE PAGE IS DRIVEN IN A REAL BROWSER ---------------------------
# The only check in this file that can see whether the creator page WORKS. It
# extracts the blob src/data/index_html.h serves, answers it with the document
# src/data/creator_schema_json.h serves, drives it through a headless Chromium
# with real pointer events, and pipes the body it POSTs into a host binary that
# links THE REAL networking/creator_parse.cpp and game/validate.cpp. That last
# hop is the only place the two ends of the XBM sentence can be compared: the
# page writes the bits, the device reads them, and nothing else in this tree
# sees both.
#
# IT IS NOT A PHONE, AND THIS GATE MUST NOT BE QUOTED AS IF IT WERE. Spec
# section 67's "Mobile editor works" and "Sprite editor works" are BENCH items;
# a desktop Chromium at a phone-shaped viewport is not a thumb on glass and
# neither box is ticked by a green run here.
#
# THE THREE-WAY EXIT IS THE POINT: 0 pass, 1 a real failure (hard fail), 2 "I
# cannot run at all" - no node, no playwright, no browser - which SKIPS WITH A
# PRINTED WORD, the same convention the content gate uses for a missing
# python3. About 3 s when it runs.
if [ -f "$ROOT/tools/page_test.mjs" ] && [ $DO_TESTS -eq 1 ]; then
  if command -v node >/dev/null 2>&1; then
    make -C "$ROOT/tests" pagetool >/dev/null \
      || fail "tests/bin/creator_decode (the page harness's device end) did not build"
    set +e
    out="$(node "$ROOT/tools/page_test.mjs" 2>&1)"
    rc=$?
    set -e
    case "$rc" in
      0) echo "$out" | tail -1 ;;
      2) echo "check.sh: no headless browser available, SKIPPING the page browser test" >&2 ;;
      *) echo "$out" >&2; fail "the creator page failed in a real browser (tools/page_test.mjs)" ;;
    esac
  else
    echo "check.sh: node not found, SKIPPING the page browser test" >&2
  fi
fi

# --- P8-C5: THE BENCH SMOKE SCRIPT STILL DESCRIBES THIS FIRMWARE -----------
# tools/creator_smoke.sh is the ONLY instrument for four spec section 67 boxes
# (PIN required, device-side validation, Wi-Fi only when necessary, inactivity
# timeout) and NOTHING IN THIS ENVIRONMENT CAN RUN IT: it needs a board, an
# access point and a PIN off the device's own screen. What CAN rot without a
# board is the script's picture of the firmware - a renamed macro, a route that
# moved, a status code that changed - and a bench script that is wrong is worse
# than none, because it fails at the bench and the operator debugs the device.
#
# THREE CHECKS, none of which pretends to be a run:
#
#  1. `--dry-run` parses the tree. Every number the script asserts is read out
#     of the header that owns it (CS_BODY_MAX, CREATOR_PIN_FAIL_MAX,
#     CREATOR_IDLE_S_DEFAULT, CREATOR_API_VERSION, FW_VERSION, CONTENT_VERSION
#     and the measured INDEX_HTML_LEN); a lookup that finds nothing is exit 2
#     naming the macro. Rename a macro in config.h and this fails HERE instead
#     of in a field.
#  2. THE ROUTE SETS MUST BE EQUAL, in both directions. A route the script does
#     not probe is a route nobody smoke-tests; a route the script probes that
#     nothing registers is a bench failure with no cause. This is the same
#     comparison the page gate makes one direction of, made two-way because
#     this script's whole job is coverage.
#  3. NO LOOPBACK HOST. The one edit that would turn this script into the
#     defect this project keeps repeating is pointing it at a mock so it can go
#     green in CI. There is no --fake and no localhost default, and this line
#     is what keeps it that way.
if [ -f "$ROOT/tools/creator_smoke.sh" ]; then
  bash -n "$ROOT/tools/creator_smoke.sh" \
    || fail "tools/creator_smoke.sh does not parse - the bench script must at least run"
  "$ROOT/tools/creator_smoke.sh" --dry-run >/dev/null \
    || fail "tools/creator_smoke.sh --dry-run failed: it can no longer read its expectations out of "\
"src/ (a renamed macro, or the generated page literal changed shape). Run it for the name."

  if [ -d "$SKETCH/src/networking" ]; then
    # What the firmware registers: srv.on("/api/xxx", ...) in webui.cpp and
    # creator_server.cpp. "/" is registered too and is checked separately
    # because a bare slash is not greppable the same way.
    reg="$( { grep -rhoE '\.on\("/api/[a-z]+"' "$SKETCH/src/networking" || true; } \
            | grep -oE '/api/[a-z]+' | sort -u )"
    # What the script probes. /api/pebbles is DELIBERATELY absent from this set:
    # the script asks for it on purpose to prove the catch-all answers, so it is
    # excluded here rather than being allowed to look like a real route.
    prb="$( { grep -ohE '(GET|POST) /api/[a-z]+' "$ROOT/tools/creator_smoke.sh" || true; } \
            | grep -oE '/api/[a-z]+' | grep -v '^/api/pebbles$' | sort -u )"
    if [ "$reg" != "$prb" ]; then
      echo "GATE FAIL: tools/creator_smoke.sh and src/networking disagree about the spec 38 route set" >&2
      echo "  registered but never smoke-tested: $(comm -23 <(echo "$reg") <(echo "$prb") | tr '\n' ' ')" >&2
      echo "  smoke-tested but not registered:   $(comm -13 <(echo "$reg") <(echo "$prb") | tr '\n' ' ')" >&2
      fail "the bench script's route coverage has drifted from the firmware"
    fi
    grep -q 'req GET / ' "$ROOT/tools/creator_smoke.sh" \
      || fail "tools/creator_smoke.sh never fetches GET / - the page route is one of the seven"

    # 2b. AND THE COVERAGE THE ROUTE SET CANNOT SEE (added at the phase-8 exit).
    #     Equal route sets prove every route is TOUCHED. They proved nothing
    #     about whether the PIN gate on the routes that WRITE was ever exercised
    #     - and it was not. Every PIN probe drove GET /api/state, and every
    #     unauthenticated POST carried a deliberately broken body that
    #     creator_body.cpp answers (413/411/415) BEFORE the PIN is consulted. So
    #     no check anywhere in this repository sent an unauthenticated POST with
    #     a VALID body, and creator_server.cpp is never compiled by
    #     tests/Makefile, which left pin_after_body() with no executor at all.
    #
    #     MEASURED: pin_after_body() replaced by `return true;` built clean
    #     (release 1,326,274 / 59,396, 0 warnings), ran ALL PASS 49/49, and
    #     passed every networking gate above - a firmware where omitting X-Pin
    #     creates a Pebble and sets the clock, with the whole gate green.
    #
    #     These three lines are narrow on purpose: they check that the probes
    #     EXIST, not what they assert. The script itself is the only thing that
    #     can check the latter, and only on a board.
    grep -q 'req POST /api/pebble - --data-binary' "$ROOT/tools/creator_smoke.sh" \
      || fail "tools/creator_smoke.sh never sends an unauthenticated POST with a VALID body to the route that WRITES - the PIN gate on POST /api/pebble would have no instrument anywhere (§67 'PIN required' points at this script)"
    grep -q 'doc_bodypin' "$ROOT/tools/creator_smoke.sh" \
      || fail "tools/creator_smoke.sh no longer probes the BODY-carried PIN - creator_server.cpp's pin_after_body() is then executed by nothing in this tree"
    grep -q 'req POST /api/pebbles - --data-binary' "$ROOT/tools/creator_smoke.sh" \
      || fail "tools/creator_smoke.sh no longer POSTs an oversize body to an UNMATCHED path - a catch-all narrowed to HTTP_GET would look identical on every other probe"
  fi

  n=$( { grep -nE '127\.0\.0\.1|localhost|::1' "$ROOT/tools/creator_smoke.sh" || true; } | wc -l )
  [ "$n" -eq 0 ] || fail "tools/creator_smoke.sh names a loopback host ($n) - it drives a BOARD. A green run against a mock is the fixture-that-is-not-the-firmware defect this project has hit three times"
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
PURE_NET="protocol.h protocol.cpp session.h session.cpp battle_link.h battle_link.cpp trade_link.h trade_link.cpp transport.h transport_loopback.cpp net_classify.h net_classify.cpp wifi_scanner.h wifi_scanner.cpp rxring.h rxring.cpp discovery.h discovery.cpp creator_gate.h creator_gate.cpp creator_body.h creator_body.cpp creator_parse.h creator_parse.cpp"
PURE_NET_CPP="protocol.cpp session.cpp battle_link.cpp trade_link.cpp transport_loopback.cpp net_classify.cpp wifi_scanner.cpp rxring.cpp discovery.cpp creator_gate.cpp creator_body.cpp creator_parse.cpp"
# transport_espnow.* is IMPURE and that is HONEST rather than a dodge: esp_now.h's
# two callback typedefs have no user-context argument at all, so the sink a
# callback posts into is forced to be file-scope. A device has one radio and a
# singleton is the truth. What is NOT a singleton is the mechanism - the ring is
# rxring.cpp and the peer table is discovery.cpp, both pure, both caller-owned,
# both driven by a host binary.
IMPURE_NET="net.h net.cpp webui.h webui.cpp creator_server.h creator_server.cpp transport_espnow.h transport_espnow.cpp"

# 1. THE PURE NETWORKING MODULES ARE PURE, AND THE SCOPING IS BY FILENAME
#    BECAUSE THE DIRECTORY IS MIXED. net.cpp, webui.cpp and
#    transport_espnow.cpp legitimately include Arduino.h and the radio headers
#    - so a directory-wide gate here could only ever be a gate somebody
#    disables. (Until P8-C0 the fourth impure file was ble_social.cpp.) The list below is the
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

# 2c. EVERY BODY-CARRYING ROUTE PASSES THE RAW HOOK (P8-C3, audit section 12).
#    The FOURTH argument of WebServer::on() is what makes canRaw() true
#    (detail/RequestHandlersImpl.h:97-103), and canRaw() is the only thing that
#    keeps a request body off Parsing.cpp:44-74's malloc/realloc growth loop -
#    whose sole bound is the attacker's own Content-Length header. A POST route
#    registered with the 3-arg on() is therefore not a smaller version of the
#    same route: it is an unbounded heap allocation any client can ask for, and
#    it would look identical in review.
#
#    NARROW, AND THE NARROWNESS IS THE POINT: it matches registration lines that
#    name a non-GET method and requires each to name the shared hook. It proves
#    NOTHING about what the hook does - tests/test_creator_api.cpp is what does
#    that - and it cannot see a route registered across two lines. Comment lines
#    are dropped: creator_server.h discusses the overload in prose.
if [ -d "$SKETCH/src/networking" ]; then
  n=$( { grep -rnE '\.on\(.*(HTTP_POST|HTTP_PUT|HTTP_PATCH|HTTP_DELETE|HTTP_ANY)' \
          "$SKETCH/src/networking" || true; } \
        | { grep -vE '^[^:]+:[0-9]+:[[:space:]]*//' || true; } \
        | { grep -v 'cs_body_hook' || true; } | wc -l )
  [ "$n" -eq 0 ] || fail "a non-GET route is registered without the raw body hook ($n) - the 4-arg on() overload is what keeps a hostile Content-Length off the heap (audit section 12)"

  # AND THE CATCH-ALL MUST EXIST, AND IT MUST STILL BE HTTP_ANY. Registering
  # raw handlers protects only the URIs that have them: Parsing.cpp:182
  # requires _currentHandler non-null and onNotFound() is NOT a handler, so
  # without a registered catch-all every unmatched POST still walks the growth
  # loop. This gate is the reason onNotFound() was deleted rather than kept
  # "just in case".
  #
  # THIS GATE COULD NOT FAIL UNTIL THE PHASE-8 EXIT, AND IT IS WORTH RECORDING
  # HOW. It used to be `grep -rn 'UriAny' | wc -l` >= 2 - a count of a TYPE
  # NAME, not a check on a call. The struct definition alone contributes three
  # occurrences (`struct UriAny`, the `UriAny()` ctor, `new UriAny()` in
  # clone()), so DELETING the registration line entirely left 3 and the gate
  # passed. Measured at the exit, both forms: with the registration deleted,
  # and with HTTP_ANY narrowed to HTTP_GET on that one line (which keeps
  # `notfound` used so no -Wunused fires, and is not matched by the raw-hook
  # gate above because that one only reads lines naming a non-GET method),
  # `SKETCH=<mutant> tools/check.sh --no-build --no-tests` printed GATE OK and
  # the release build was clean at 0 warnings. An unmatched POST would have
  # been back on the malloc growth loop with every gate green.
  #
  # So the gate now matches THE REGISTRATION ITSELF and requires exactly one:
  # a second would mean two catch-alls, and zero means the hole is open again.
  # tools/creator_smoke.sh phase 2 carries the other half - the bench probe
  # that an oversize POST to an UNMATCHED path is answered 413 rather than
  # read whole and then 404'd, which is the only way to tell the two apart on
  # a real socket.
  n=$( { grep -rnE '\.on\([[:space:]]*UriAny\(\)[[:space:]]*,[[:space:]]*HTTP_ANY[[:space:]]*,' \
          "$SKETCH/src/networking" || true; } \
        | { grep -vE '^[^:]+:[0-9]+:[[:space:]]*//' || true; } | wc -l )
  [ "$n" -eq 1 ] || fail "the catch-all is not registered exactly once as .on(UriAny(), HTTP_ANY, ...) (found $n) - POST /anything-else would walk readBytesWithTimeout()'s unbounded growth loop again"
  n=$( { grep -rn 'onNotFound' "$SKETCH/src/networking" || true; } \
        | { grep -vE '^[^:]+:[0-9]+:[[:space:]]*//' || true; } | wc -l )
  [ "$n" -eq 0 ] || fail "onNotFound() is registered again ($n) - it is consulted AFTER the body has been parsed, so it cannot bound one; the catch-all handler is what does"
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

# 1b. THE CREATOR URL CARRIES NO SECRET (spec section 39, P8-C1).
#    net_url() emitted "http://<ip>/?k=NNNN" until this chunk, and the NNNN was
#    the PIN - so the authorisation secret was in a symbol anyone can photograph
#    from across a room, in the phone's URL bar, in its history and in any
#    Referer the page later sends. Spec section 39 is explicit that the PIN is
#    the user-facing authorisation layer and that the QR carries no more than it
#    must.
#
#    THIS IS A GREP BECAUSE NOTHING ELSE CAN SEE IT. net.cpp is on the IMPURE
#    list, tests/Makefile never compiles it, and the host fixtures that fill a
#    CreatorInfo write the URL themselves - so a test asserting "no k= in the
#    payload" would only be asserting what the fixture typed. The one place the
#    format string actually lives is greppable, and this counts it.
#
#    NARROW ON PURPOSE, exactly like the WiFi.begin gate above: it matches a
#    query parameter being FORMATTED into a URL under src/networking, not the
#    letter k anywhere. A future route that legitimately needs a query argument
#    will have to argue with this line, which is the point.
if [ -d "$SKETCH/src/networking" ]; then
  n=$( { grep -rnE '"[^"]*\?[A-Za-z_]+=%' "$SKETCH/src/networking" || true; } | wc -l )
  [ "$n" -eq 0 ] || fail "a URL with a formatted query parameter is built under src/networking ($n) - the creator URL is \"http://<ip>/\" and the PIN travels in an X-Pin header, never in the QR (spec 39)"
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
# THE SHAPE NOT TO COPY WAS IN THIS TREE UNTIL P8-C0: core/nt_types.h's
# BlePeerInfo had the raw address as its FIRST member and ble_social.cpp copied
# it out of the scan callback into the table the game read. Both are deleted
# (`git show ed9b099:Pebblebol/src/networking/ble_social.cpp`), and THIS GATE IS
# WHY THE DELETION DOES NOT RELAX ANYTHING: what it forbids is the shape, not
# the file, so it goes on stopping that shape being reproduced under a new
# name.
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
# widening the seam. No file below has a header guard, so the count is zero and
# not three: they are .cpp files. P7-C4 added trade_link.cpp, because the trade
# runs over the identical seam and the identical session.
for f in session.cpp battle_link.cpp trade_link.cpp; do
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

# --- P7-C6: THE TRADE'S SLOT WRITES MAY NOT GO THROUGH THE THROTTLED PATH ---
# save_pebble(slot, p, force) DEFERS a second write of one key inside
# SAVE_MIN_GAP_MS and RETURNS TRUE. That is correct for the care loop, which
# calls it every action and lets save_service() flush; it is a lie for
# game/trade.cpp's store seam, whose whole contract is "the bytes landed".
# The trade writes ONE key TWICE microseconds apart (B1 releases the outgoing
# slot, box_add() refills the slot B1 just released), so with the shipping
# millisecond clock bound the second write was deferred, reported as landed, and
# the journal was cleared over an empty flash slot - a Pebble destroyed with no
# record left to repair it. See persistence/save_manager.h save_pebble_now().
#
# THE RULE: app/ and ui/ call save_pebble_now(). The throttled entry point has
# exactly one family of callers, persistence/game_state.cpp, which already reads
# save_pebble_landed() on every call.
#
# ITS LIMIT, STATED: this is a call-site gate. It cannot see a THIRD shim in
# persistence/ that returns save_pebble()'s answer without asking
# save_pebble_landed(); tests/test_persistence.cpp's two named write-path cases
# and tests/test_trade.cpp's kill sweep (which now binds a real ms clock) are
# the other half.
if [ -f "$SKETCH/src/persistence/save_manager.h" ]; then
  n=$( { grep -rnE '\bsave_pebble[[:space:]]*\(' "$SKETCH/src/app" "$SKETCH/src/ui" \
          --include='*.cpp' --include='*.h' || true; } \
        | { grep -vE '^[^:]+:[0-9]+:[[:space:]]*//' || true; } | wc -l )
  [ "$n" -eq 0 ] || fail "save_pebble() is called from app/ or ui/ ($n) - the throttled write DEFERS and still returns true; the trade's store seam must call save_pebble_now() (persistence/save_manager.h)"
fi

# --- P7-C6: A HOST BINARY THAT DRIVES save_manager BINDS A REAL ms CLOCK ---
# save_set_clock(nullptr, ...) switches OFF save_manager.cpp's entire wear-filter
# branch (`if (s_now_ms && s_have_written[slot])`), so a fixture that passes it
# is testing a save_manager the release artefact does not execute. That is how
# 27,819 checks, a 15-point kill sweep and an 800-trial lossy table all missed a
# Pebble being destroyed on every clean trade. app/app.cpp binds a real one.
if [ -d "$ROOT/tests" ]; then
  n=$( { grep -rnE '\bsave_set_clock[[:space:]]*\([[:space:]]*(nullptr|NULL|0)[[:space:]]*,' \
          "$ROOT/tests" --include='*.cpp' --include='*.h' || true; } | wc -l )
  [ "$n" -eq 0 ] || fail "a host test binds save_set_clock(nullptr, ...) ($n) - that disables the wear filter the shipping build runs; bind a fake ms clock the case can freeze (tests/test_trade.cpp)"
fi

# --- P7-C6: THE FAKE ARDUINO CORE IS FOR ONE OBJECT --------------------------
# tests/fakes/arduino/ holds two headers with one symbol each (millis(),
# esp_rtc_get_time_us()) so that hardware/gametime.cpp's DEVICE branch can be
# compiled and DRIVEN on the host. That branch was guarded by the grep above
# and by nothing else, and a grep cannot see a unit slip: dividing the RTC by
# 1,000,000 instead of 1,000 leaves this file green and fails all four cases in
# tests/test_clock_device.cpp.
#
# THE FAKE MUST STAY THAT NARROW. One Makefile rule may pass -Ifakes/arduino; a
# second would be the beginning of a private Arduino core that can drift from
# the real one in silence, and every other host object is compiled against no
# Arduino at all, which is what makes the purity gates above mean anything.
if [ -d "$ROOT/tests/fakes/arduino" ]; then
  n=$( { grep -nE -- '-Ifakes/arduino' "$ROOT/tests/Makefile" || true; } | wc -l )
  [ "$n" -eq 1 ] || fail "tests/fakes/arduino is reached by $n Makefile rules, not 1 - it exists for gametime_arduino.o alone (tests/fakes/arduino/Arduino.h)"
  n=$( { find "$ROOT/tests/fakes/arduino" -type f | wc -l; } )
  [ "$n" -eq 2 ] || fail "tests/fakes/arduino holds $n files, not 2 - a wider fake is a second Arduino core nobody diffs against the real one"
fi

# --- P9-C5: THE CORRUPTION DEADLINE HAS A READER IN THE FIRMWARE ------------
# THE DEFECT THIS EXISTS FOR WAS REAL AND SHIPPED FOR FOUR PHASES. P5-C3 landed
# game/corruption.cpp with cor_apply() called from ui/screen_encounter.cpp and
# cor_clear() called from game/inventory.cpp - and cor_expire() called by
# NOTHING outside tests/. So the 24 h deadline was armed on every device and
# read on none, PBS_CORRUPTED was permanent until an Antivirus cleared it, and
# tools/content/verify.py's own "corruption clears by timer" check passed over
# the JSON the whole time the property was false in the firmware. Five host
# cases in tests/test_encounters.cpp drove cor_expire() perfectly and none of
# them could see that no device ever called it.
#
# WHAT THIS GATE IS AND IS NOT, said exactly, because "the test that cannot
# fail" is this project's recurring defect and a grep is the shape it usually
# takes. It asserts that game/corruption.h's expiry walk HAS A CALL SITE in
# app/, which is the one thing tests/ structurally cannot: app/app.cpp includes
# Arduino.h, so no host binary links it. It proves NOTHING about whether that
# call runs, how often, or with the right clock - tests/test_corruption.cpp's
# `the_timer_walk_expires_every_due_slot_and_leaves_the_rest_alone` is what
# drives the walk itself. Deleting the line in app.cpp fails HERE by name;
# breaking what the walk does fails THERE by name.
#
# THE FIRST VERSION OF THIS GATE COULD NOT FAIL, AND THE MUTATION IS WHY IT SAYS
# SO. It dropped lines that BEGIN with `//` and counted `cor_service` anywhere
# else - so `#include "../game/corruption.h"  // cor_service(): the 24 h
# deadline`, the include comment three hundred lines above the call, satisfied
# it. Deleting the actual call printed GATE OK. That is the same defect the
# phase-8 review found (a gate counting a TYPE NAME instead of a registration),
# reproduced here by the mutation that was supposed to confirm the gate worked.
# The form below strips every trailing comment FIRST and then requires a call
# with an ARGUMENT - `cor_service(` followed by something that is not `)` - so a
# mention in prose, in a comment or in a declaration cannot stand in for one.
#
# AND IT MISSED THE OTHER COMMENT SYNTAX UNTIL P9-C6. `sed 's://.*::'` strips
# line comments only, so wrapping the call site in /* ... */ - which is what a
# person bisecting a bug actually does - left the gate green AND the suite green,
# because test_corruption.cpp drives cor_service() directly and cannot see that
# no device calls it. That is the four-phase-old defect this gate exists to stop
# (cor_expire() with no caller) surviving inside the gate written to stop it. The
# preprocessor is what strips comments correctly, so that is what strips them:
# `cpp -fpreprocessed -dD -E -P` removes /* */ and // without touching #include
# lines or expanding anything. If cpp is unavailable the sed fallback still runs,
# and says so, rather than the gate quietly passing.
if [ -f "$SKETCH/src/game/corruption.cpp" ]; then
  if command -v cpp >/dev/null 2>&1; then
    strip_comments() { cpp -fpreprocessed -dD -E -P - 2>/dev/null; }
  else
    echo "check.sh: NOTE - cpp not found, cor_service gate falls back to line-comment stripping only"
    strip_comments() { sed 's://.*::'; }
  fi
  n=$( { cat "$SKETCH"/src/app/*.cpp 2>/dev/null || true; } \
        | strip_comments \
        | { grep -cE '\bcor_service[[:space:]]*\([^)]' || true; } )
  [ "${n:-0}" -ge 1 ] || fail "cor_service() has NO caller in src/app ($n) - the 24 h corruption deadline would be written and never read on a device, exactly as cor_expire() was from P5-C3 to P9-C5 (game/corruption.h)"
fi

# --- P9-C5: ui/corrupt_fx.cpp STAYS HOST-LINKABLE ---------------------------
# The module exists for ONE reason: ui/petfx.cpp includes render.h, hence
# Arduino.h and U8g2lib.h, so no host binary can link it and every rule left
# inside it is a rule no test in this repository can execute. The glitch's
# geometry and the behaviour-row choice were moved out so that
# tests/test_corruption.cpp can drive them and so that
# tests/tools/corrupt_view.cpp can draw them.
#
# A single #include of render.h here would undo all of that silently: the file
# would still compile in the firmware, the host binaries would stop linking,
# and the natural repair is to drop the module from tests/Makefile. So the
# include is a red line rather than a convention. It matches #include LINES
# ONLY - corrupt_fx.h discusses render.h and rd_dither_rect_phase() in prose on
# purpose, and must not trip it.
# P9-C6 adds ui/petfx_core.{h,cpp} to the same red line, for the same reason and
# with a bigger receipt: pf_build_lids() lived inside petfx.cpp behind a banner
# that CLAIMED it was host-testable, no host binary ever compiled it, and the
# blink it composites was drawing over four species' bodies while every byte
# check in the tree passed. It is the guarded file now, so it stays linkable.
# P10-C3 adds ui/anim_ease.{h,cpp} - the parabola, the lunge, the lerp, the
# window and the dissolve frontier that SEVEN SHIPPED FILMS have run on since
# P6-C3 and that no host binary had ever compiled, because they were `static`
# inside ui/actfx.cpp. The first run of tests/test_anim.cpp found a documented
# invariant that was false and a uint32_t overflow; both are in the commit.
if [ -f "$SKETCH/src/ui/corrupt_fx.cpp" ]; then
  for f in corrupt_fx.h corrupt_fx.cpp petfx_core.h petfx_core.cpp anim_ease.h anim_ease.cpp; do
    n=$( { grep -nE '^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"][^>"]*(Arduino\.h|u8g2|U8g2|render\.h|gfx\.h|petfx\.h|pet_view\.h)' \
            "$SKETCH/src/ui/$f" || true; } | wc -l )
    [ "$n" -eq 0 ] || fail "ui/$f includes a renderer or device header ($n) - it exists to be host-linkable (ui/corrupt_fx.h, ui/petfx_core.h)"
  done
fi

# --- P10-C1: THE DIAGNOSTICS SURFACE, THREE GATES ---------------------------
#
# 1. dev/diag_core.{h,cpp} STAYS HOST-LINKABLE. Same red line ui/corrupt_fx.cpp
#    and ui/petfx_core.cpp carry, and for the same measured reason: dev/
#    godmode.cpp includes <Arduino.h> and U8g2 through ui/render.h, so every
#    rule inside the console is a rule no host binary can execute. diag_core
#    exists to hold the parsing, the range checks, the taint decision and the
#    field formatting OUTSIDE that wall. A single #include of Arduino.h or
#    render.h here would undo it silently: the firmware would still build, only
#    tests/bin/test_diag would stop linking, and the natural repair is to drop
#    the module from tests/Makefile. #include LINES ONLY - diag_core.h discusses
#    Arduino.h and render.h in prose on purpose.
if [ -f "$SKETCH/src/dev/diag_core.cpp" ]; then
  for f in diag_core.h diag_core.cpp; do
    n=$( { grep -nE '^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"][^>"]*(Arduino\.h|u8g2|U8g2|render\.h|gfx\.h|godmode\.h)' \
            "$SKETCH/src/dev/$f" || true; } | wc -l )
    [ "$n" -eq 0 ] || fail "dev/$f includes a renderer or device header ($n) - it exists to be host-linkable (dev/diag_core.h)"
  done
  # And no LINK LINE in tests/ may reach for the console itself, which would
  # make the whole split pointless the moment somebody tried.
  #
  # COMMENT LINES ARE DROPPED FIRST, and that is not tidiness: the first version
  # of this gate counted the word anywhere and fired on the paragraph in
  # tests/Makefile that EXPLAINS why dev/godmode.cpp may never be linked. A gate
  # that a correct explanation trips is a gate people delete. Makefile comments
  # begin with '#' at the start of a line.
  n=$( { grep -v '^[[:space:]]*#' "$ROOT/tests/Makefile" || true; } \
        | { grep -c 'godmode' || true; } )
  [ "${n:-0}" -eq 0 ] || fail "a tests/Makefile rule names dev/godmode ($n) - that file includes Arduino.h and cannot be host-compiled (dev/diag_core.h)"
fi

# 2. EVERY god_*() THE HEADER DECLARES IS DEFINED IN THE RELEASE CONFIGURATION.
#
#    THIS IS THE ONE PLACE A GATE CAN SEE SOMETHING NO TEST CAN. dev/godmode.cpp
#    is compiled by ZERO host binaries, so the GOD_MODE_ENABLED 0 bodies - the
#    sixteen functions that ARE the shipping artefact's whole god-mode API - are
#    untested by construction. A missing one is a link error in a build nobody
#    runs between phase tags; a WRONG one is not an error at all, and this gate
#    cannot see that either. It checks PRESENCE, which is the half a grep can
#    honestly claim.
#
#    "THE RELEASE CONFIGURATION" IS THE TEXT ABOVE THE #if PLUS THE #else ARM,
#    not the #else arm alone, because P10-C1 deliberately moved two functions
#    (god_note_load / god_load_result) above the guard - the shipping build's
#    `info` reads the boot LoadResult, and the phase-6 defect is what happens
#    when something the artefact needs lives inside a dev-only branch.
#
#    COMMENTS ARE STRIPPED FIRST AND A DEFINITION MUST HAVE A BODY. The first
#    version of this gate carried a fallback that accepted the NAME ANYWHERE,
#    and the #else arm's own explanatory comment names god_note_load() - so the
#    fallback made the strict check dead and the gate would have passed with the
#    function deleted. That is the P9-C6 defect (a gate satisfied by a mention
#    in prose) reproduced inside the gate written against it, caught by the
#    mutation run that was supposed to confirm the gate worked.
if [ -f "$SKETCH/src/dev/godmode.cpp" ]; then
  if command -v cpp >/dev/null 2>&1; then
    strip_comments3() { cpp -fpreprocessed -dD -E -P - 2>/dev/null; }
  else
    echo "check.sh: NOTE - cpp not found, the god stub gate falls back to line-comment stripping only"
    strip_comments3() { sed 's://.*::'; }
  fi
  # The declarations: `<type> god_name(` at the start of a line in the header.
  decls=$( { grep -oE '^[A-Za-z_][A-Za-z0-9_]*[[:space:]]+(god_[a-z0-9_]+)[[:space:]]*\(' \
              "$SKETCH/src/dev/godmode.h" || true; } \
            | grep -oE 'god_[a-z0-9_]+' | sort -u )
  [ -n "$decls" ] || fail "check.sh: parsed no god_*() declarations out of dev/godmode.h - the stub gate would pass over an empty list"
  above=$(sed -n '1,/^#if GOD_MODE_ENABLED/p' "$SKETCH/src/dev/godmode.cpp")
  stub=$(sed -n '/^#else  *\/\/ ---/,/^#endif \/\/ GOD_MODE_ENABLED/p' "$SKETCH/src/dev/godmode.cpp")
  [ -n "$stub" ] || fail "check.sh: could not find the #else arm of dev/godmode.cpp - the stub gate cannot see what it guards"
  release_src=$(printf '%s\n%s\n' "$above" "$stub" | strip_comments3)
  for d in $decls; do
    printf '%s\n' "$release_src" | grep -qE "\b$d[[:space:]]*\(.*\)[[:space:]]*\{" \
      || fail "dev/godmode.h declares $d() and the GOD_MODE_ENABLED 0 configuration of dev/godmode.cpp has no body for it - the release build would not link, and no host test can see that (dev/godmode.h, section 5)"
  done
  # The always-compiled half must be reached from BOTH arms. This is the shape
  # of the phase-6 defect exactly: sim_set_time_scale()'s only callers were
  # below the #if and the shipping build ran sim_tick(0) for four phases.
  stub_code=$(printf '%s\n' "$stub" | strip_comments3)
  for fn in heap_trend_begin shell_line_reset; do
    printf '%s\n' "$stub_code" | grep -qE "\b$fn[[:space:]]*\([^)]*\)[[:space:]]*;" \
      || fail "the GOD_MODE_ENABLED 0 god_begin() does not call $fn() - the shipping artefact would lose it (dev/godmode.cpp section 0c)"
  done
  for fn in heap_trend_service shell_line_service; do
    printf '%s\n' "$stub_code" | grep -qE "\b$fn[[:space:]]*\([^)]*\)[[:space:]]*;" \
      || fail "the GOD_MODE_ENABLED 0 god_service() does not call $fn() - the shipping artefact would be deaf on the serial line, which is what it was until P10-C1 (dev/godmode.cpp section 0c)"
  done
fi

# 3. god_note_load() HAS A CALLER IN src/app. Same form and same reason as the
#    cor_service() gate above: app/app.cpp includes Arduino.h, so no host binary
#    links it, and a recorded-then-never-recorded value is this project's most
#    repeated defect. Without the call, spec section 49's "Last error" field
#    reports LOAD_OK for ever on every device and nothing anywhere notices.
#    Comments stripped by the preprocessor first, and the call must have an
#    ARGUMENT, so a mention in prose or a declaration cannot satisfy it.
if [ -f "$SKETCH/src/dev/godmode.cpp" ]; then
  if command -v cpp >/dev/null 2>&1; then
    strip_comments2() { cpp -fpreprocessed -dD -E -P - 2>/dev/null; }
  else
    echo "check.sh: NOTE - cpp not found, god_note_load gate falls back to line-comment stripping only"
    strip_comments2() { sed 's://.*::'; }
  fi
  n=$( { cat "$SKETCH"/src/app/*.cpp 2>/dev/null || true; } \
        | strip_comments2 \
        | { grep -cE '\bgod_note_load[[:space:]]*\([^)]' || true; } )
  [ "${n:-0}" -ge 1 ] || fail "god_note_load() has NO caller in src/app ($n) - the boot LoadResult would be dropped again and spec 49's Last error field would report LOAD_OK for ever (dev/godmode.h)"
fi

# --- P10-C2: THE PERFORMANCE INSTRUMENT, FIVE GATES -------------------------
#
# WHAT NONE OF THESE DOES, SAID FIRST, BECAUSE IT IS THE POINT OF THE CHUNK.
# NOT ONE OF THEM ASSERTS A MICROSECOND, AND NONE MAY. Spec section 46's five
# thresholds - frame <= FRAME_BUDGET_US, loop() <= PERF_PASS_BUDGET_US, a
# per-frame heap delta of 0, input-to-render latency <= 2 frames, and a 24 h
# soak with a flat heap line - are all UNMEASURABLE in this repository, and
# structurally rather than temporarily: micros(), I2C and U8g2 do not exist on
# the host, ui/render.cpp and app/app.cpp are compiled by ZERO host binaries,
# the ~24 ms of sendBuffer() that dominates a frame is a property of a 400 kHz
# bus and a panel that are both absent, and the worst loop() pass is a network
# peer's TCP behaviour through WebServer::handleClient(), which is never linked
# here. A green "frame <= 50 ms" line printed by this script would be a verdict
# about a firmware nobody flashes - the phase-6 and phase-7 defect exactly.
#
# So the five thresholds go to docs/bench.md, on a board, with a variant named
# for each, and these gates check the only things a grep honestly can: that the
# instrument is PURE, that it is WIRED, and that the one edit which would turn
# it into a lie without breaking anything is refused.
if [ -f "$SKETCH/src/core/perf.cpp" ]; then
  # 1. core/perf.{h,cpp} STAY HOST-LINKABLE. The same red line ui/corrupt_fx.cpp,
  #    ui/petfx_core.cpp and dev/diag_core.cpp carry, for the same measured
  #    reason: the arithmetic is the half that can be wrong here, and it is only
  #    testable while it is on this side of the Arduino wall.
  for f in perf.h perf.cpp; do
    n=$( { grep -nE '^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"][^>"]*(Arduino\.h|u8g2|U8g2|render\.h|gfx\.h)' \
            "$SKETCH/src/core/$f" || true; } | wc -l )
    [ "$n" -eq 0 ] || fail "core/$f includes a renderer or device header ($n) - it exists to be host-linkable (core/perf.h)"
  done

  if command -v cpp >/dev/null 2>&1; then
    strip_comments4() { cpp -fpreprocessed -dD -E -P - 2>/dev/null; }
  else
    echo "check.sh: NOTE - cpp not found, the perf gates fall back to line-comment stripping only"
    strip_comments4() { sed 's://.*::'; }
  fi

  # 2. THE THREE STAMP SITES STILL EXIST. Delete any one and the instrument goes
  #    dark IN SILENCE: render.cpp, ui.cpp and app.cpp are compiled by no host
  #    binary, so tests/test_perf.cpp would go on passing over arithmetic nobody
  #    feeds. Same form as the cor_service() and god_note_load() gates -
  #    comments stripped by the preprocessor first, a call with an ARGUMENT
  #    required - so a mention in prose or a declaration cannot stand in for one.
  n=$( { cat "$SKETCH/src/ui/render.cpp" 2>/dev/null || true; } | strip_comments4 \
        | { grep -cE '\bperf_note_frame[[:space:]]*\([^)]' || true; } )
  [ "${n:-0}" -ge 1 ] || fail "perf_note_frame() has NO caller in ui/render.cpp ($n) - the frame instrument would record nothing and no host test can see that (core/perf.h)"
  n=$( { cat "$SKETCH"/src/app/*.cpp 2>/dev/null || true; } | strip_comments4 \
        | { grep -cE '\bperf_note_pass[[:space:]]*\([^)]' || true; } )
  [ "${n:-0}" -ge 1 ] || fail "perf_note_pass() has NO caller in src/app ($n) - the loop() instrument would record nothing (core/perf.h)"
  n=$( { cat "$SKETCH"/src/app/*.cpp 2>/dev/null || true; } | strip_comments4 \
        | { grep -cE '\bperf_set_screen[[:space:]]*\([^)]' || true; } )
  [ "${n:-0}" -ge 1 ] || fail "perf_set_screen() has NO caller in src/app ($n) - every frame and every pass would be billed to screen 0 (core/perf.h)"

  # 3. THE ONE EDIT THAT WOULD TURN THE INSTRUMENT INTO A LIE WITHOUT BREAKING
  #    ANYTHING. app_loop()'s stage 7 yield is a DELIBERATE nap of up to
  #    PWR_SLEEP_SLICE_MS (8,000 ms). perf_note_pass() measures the WORK of a
  #    pass and must therefore be stamped BEFORE it; moved after, the worst pass
  #    reads 8,000,000 us on a healthy sleeping board, spec section 46's 100 ms
  #    budget stops meaning anything, and every gate in this file and every case
  #    in tests/test_perf.cpp stays green. That is the silent-lie shape this
  #    project has hit in nine consecutive phases, so it is a line-number
  #    comparison rather than a comment.
  app_src="$SKETCH/src/app/app.cpp"
  if [ -f "$app_src" ]; then
    ln_note=$( { grep -nE '^[^/]*\bperf_note_pass[[:space:]]*\(' "$app_src" || true; } | head -1 | cut -d: -f1 )
    ln_yield=$( { grep -nE '^[^/]*\bpwr_yield[[:space:]]*\(' "$app_src" || true; } | head -1 | cut -d: -f1 )
    [ -n "$ln_note" ]  || fail "check.sh: no perf_note_pass( call found in app/app.cpp - the yield-order gate cannot see what it guards"
    [ -n "$ln_yield" ] || fail "check.sh: no pwr_yield( call found in app/app.cpp - the yield-order gate cannot see what it guards"
    [ "$ln_note" -lt "$ln_yield" ] || fail "perf_note_pass() is at app.cpp:$ln_note, BELOW pwr_yield() at :$ln_yield - the worst loop() pass would then read PWR_SLEEP_SLICE_MS by construction and stop meaning work (core/config.h, PERF_PASS_BUDGET_US)"
  fi

  # 4. NO HOST BINARY MAY ASSERT A TIME. tests/test_perf.cpp exists to drive
  #    arithmetic over stamps it invented; the day somebody writes
  #    CHECK(perf_frame_worst_us() < FRAME_BUDGET_US) over those stamps,
  #    tools/check.sh starts printing a frame verdict about a device it has
  #    never seen. That is the theatre this whole design refuses, so it is
  #    refused mechanically and not by a comment.
  #    `.*` AND NOT `[^\n]*`: grep's ERE does not read \n inside a bracket
  #    expression, so `[^\n]` means "not a backslash and not the letter n" - and
  #    the very line this gate exists to catch,
  #    `CHECK(perf_frame_worst_us() < (uint32_t)FRAME_BUDGET_US)`, contains an
  #    `n` in `uint32_t` and slipped straight through. Found by planting it.
  n=$( { grep -nE '(CHECK|CHECK_EQ).*(FRAME_BUDGET_US|PERF_PASS_BUDGET_US)' \
          "$ROOT/tests"/test_*.cpp 2>/dev/null || true; } \
        | { grep -v 'perf_overrun_backoff_ms\|perf_note_frame\|perf_note_pass\|perf_frame_overruns\|perf_pass_overruns' || true; } | wc -l )
  [ "$n" -eq 0 ] || fail "a host test asserts something against FRAME_BUDGET_US / PERF_PASS_BUDGET_US outside the overrun-boundary cases ($n) - no host binary can measure a frame or a pass; spec section 46's thresholds are docs/bench.md's"
fi

# --- P10-C2: THE FOUR STATES SPEC 47 IS EXEMPTED FOR STAY UNREACHABLE -------
# tests/test_statemachine.cpp's kExits table answers "how does this state end"
# for all 27 rows, and FOUR of them are answered "it is never resident": BOOT
# and LOAD_SAVE are drawn one frame at a time by ui_boot_screen() with gs_load()
# running synchronously underneath and no loop at all, and CONFIRM / ALERT are
# ui/dialog.cpp's overlays which never become sm_current(). A host binary can
# assert that sm_begin() does not land on one; it CANNOT see app/app.cpp or
# ui/ui.cpp, so the other half of the exemption - that nothing navigates there -
# is this gate. The moment one becomes reachable the exemption is a lie and the
# state needs a real exit, so the build stops here instead.
if [ -f "$ROOT/tests/test_statemachine.cpp" ]; then
  if command -v cpp >/dev/null 2>&1; then
    strip_comments5() { cpp -fpreprocessed -dD -E -P - 2>/dev/null; }
  else
    strip_comments5() { sed 's://.*::'; }
  fi
  nav_src=$( { cat "$SKETCH"/src/ui/*.cpp "$SKETCH"/src/app/*.cpp 2>/dev/null || true; } | strip_comments5 )
  for scr in SCR_BOOT SCR_LOAD_SAVE SCR_CONFIRM SCR_ALERT; do
    n=$( printf '%s\n' "$nav_src" \
          | { grep -cE "\b(ui_goto|ui_push|sm_goto|sm_push|sm_replace_root|nav_push|nav_goto)[[:space:]]*\([[:space:]]*$scr\b" || true; } )
    [ "${n:-0}" -eq 0 ] || fail "$scr is navigated to from src/ui or src/app ($n) - tests/test_statemachine.cpp's kExits table exempts it as never-resident, so it now needs a real timeout and a user-visible exit (spec section 47)"
  done
fi

# --- P10-C2: THE SOUND SETTING REACHES THE PIEZO THROUGH ONE PREDICATE ------
# core/nt_types.h's cfg_sound_muted() exists because app/app.cpp is compiled by
# NO host binary: with the expression written inline in app_audio_muted(), every
# link of "the sound setting persists" was tested and the JOIN was tested
# nowhere, and a test that re-stated the expression would have asserted a copy.
# tests/test_sound.cpp drives the real function through the real save_manager;
# this is the half that says the artefact still calls it. Same form as the
# cor_service() gate: comments stripped, an argument required.
if [ -f "$SKETCH/src/hardware/audio.cpp" ]; then
  if command -v cpp >/dev/null 2>&1; then
    strip_comments6() { cpp -fpreprocessed -dD -E -P - 2>/dev/null; }
  else
    strip_comments6() { sed 's://.*::'; }
  fi
  app_txt=$( { cat "$SKETCH"/src/app/*.cpp 2>/dev/null || true; } | strip_comments6 )
  n=$( printf '%s\n' "$app_txt" | { grep -cE '\bcfg_sound_muted[[:space:]]*\([^)]' || true; } )
  [ "${n:-0}" -ge 1 ] || fail "cfg_sound_muted() has NO caller in src/app ($n) - the persisted CF_MUTE would stop reaching the piezo and no host test could see it (core/nt_types.h, tests/test_sound.cpp)"
  n=$( printf '%s\n' "$app_txt" | { grep -cE '\baudio_bind[[:space:]]*\([^)]' || true; } )
  [ "${n:-0}" -ge 1 ] || fail "audio_bind() has NO caller in src/app ($n) - the tone engine would run with no sink and no mute hook (hardware/audio.h)"
fi

# --- P10-C3: THE ANIMATION PASS, SIX GATES ----------------------------------
#
# THE ONE THAT MATTERS MOST IS (2). Everything else here is presence; that one
# is the shape of the phase-9 defect, which cost four species a visible body on
# HOME because a rule lived where no host binary could execute it.
if [ -f "$SKETCH/src/ui/anim_ease.cpp" ]; then
  if command -v cpp >/dev/null 2>&1; then
    strip_comments7() { cpp -fpreprocessed -dD -E -P - 2>/dev/null; }
  else
    strip_comments7() { sed 's://.*::'; }
  fi

  # 1. ONE COPY OF THE MOTION MATHS. ui/actfx.cpp's af_hop/af_lunge/af_lerp/
  #    af_pct are one-line forwards into ui/anim_ease.cpp now. If somebody
  #    re-opens one of them the firmware still builds, the suite still passes,
  #    and the seven films silently stop being the thing tests/test_anim.cpp
  #    drives - so the arithmetic is required to be a forward and nothing else.
  af_txt=$( strip_comments7 < "$SKETCH/src/ui/actfx.cpp" )
  for fn in ae_hop ae_lunge ae_lerp ae_pct; do
    n=$( printf '%s\n' "$af_txt" | { grep -cE "\\b${fn}[[:space:]]*\\(" || true; } )
    [ "${n:-0}" -ge 1 ] || fail "ui/actfx.cpp no longer calls ${fn}() ($n) - the motion maths would be a second copy again and tests/test_anim.cpp would be driving code no film runs (ui/anim_ease.h)"
  done
  # ...and the Bayer table it used to own must not come back beside the one in
  # ui/anim_ease.cpp: two dither matrices is how two effects come to shimmer
  # against each other.
  n=$( printf '%s\n' "$af_txt" | { grep -cE '\bAF_BAYER\b' || true; } )
  [ "${n:-0}" -eq 0 ] || fail "ui/actfx.cpp has its own Bayer matrix again ($n) - ui/anim_ease.cpp owns the only one (ae_bayer)"

  # 2. THE DERIVED SLEEPING BODY IS DERIVED ON BOTH OF HOME'S BODY PATHS.
  #
  #    HOME draws its creature two ways: ui/petfx.cpp's animated automaton on
  #    the device, and ui/screen_home.cpp's still draw_static_body() - which is
  #    the ONLY one a host binary compiles and the only one any golden shows.
  #    Deriving the pose in just one of them leaves the device showing a species
  #    while every snapshot in the suite shows the authored blob, WITH THE BUILD
  #    AND THE WHOLE SUITE GREEN. That is exactly the phase-9 blink defect, and
  #    the only thing that can see it from outside is this.
  for f in petfx.cpp screen_home.cpp; do
    n=$( strip_comments7 < "$SKETCH/src/ui/$f" | { grep -cE '\bpf_build_sleep[[:space:]]*\([^)]' || true; } )
    [ "${n:-0}" -ge 1 ] || fail "ui/$f does not call pf_build_sleep() ($n) - HOME has TWO body paths and deriving the sleeping pose on only one shows the device a species and every golden a blob, with the suite green (ui/petfx_core.h)"
  done

  # 3. THE GLITCH IS PAINTED WHERE A GOLDEN CAN SEE IT. Same argument. P9-C5
  #    shipped cfx_rows() with nineteen host tests on its geometry and its only
  #    painter in ui/petfx.cpp, so no snapshot in this repository had ever drawn
  #    a corrupted creature. ui/screen_home.cpp is the second painter.
  n=$( strip_comments7 < "$SKETCH/src/ui/screen_home.cpp" | { grep -cE '\bcfx_rows[[:space:]]*\([^)]' || true; } )
  [ "${n:-0}" -ge 1 ] || fail "ui/screen_home.cpp does not call cfx_rows() ($n) - the corruption glitch would again be painted only in ui/petfx.cpp, which no host binary compiles and no golden covers (ui/corrupt_fx.h)"

  # 4. BOTH BLITS EXIST IN BOTH BACKENDS. The device's gfx_xbm() is OPAQUE
  #    (setBitmapMode(0), ui/render.cpp:368) and the host fake drew only the
  #    1-bits until P10-C3, so the two disagreed about every blit in the
  #    firmware for five phases and nothing could see it. A backend that loses
  #    gfx_xbm_t() would send every film back through the opaque one and start
  #    punching holes in whatever it crosses.
  for f in "$SKETCH/src/ui/gfx_u8g2.cpp" "$ROOT/tests/fakes/gfx_fb.cpp"; do
    n=$( strip_comments7 < "$f" | { grep -cE '\bgfx_xbm_t[[:space:]]*\(' || true; } )
    [ "${n:-0}" -ge 1 ] || fail "$(basename "$f") does not implement gfx_xbm_t() ($n) - the transparent blit is half of the drawing seam and a film without it erases what it crosses (ui/gfx.h)"
    n=$( strip_comments7 < "$f" | { grep -cE '\bgfx_dither_rect_phase[[:space:]]*\(' || true; } )
    [ "${n:-0}" -ge 1 ] || fail "$(basename "$f") does not implement gfx_dither_rect_phase() ($n) - ui/corrupt_fx.cpp's rows carry a phase and a backend that ignores it draws a constant instead of a shimmer (ui/gfx.h)"
  done
  # And the DEVICE side must actually flip the bitmap mode: forwarding
  # gfx_xbm_t() to a plain drawXBM under render.cpp's setBitmapMode(0) would
  # compile, link, ship, and be opaque.
  n=$( strip_comments7 < "$SKETCH/src/ui/gfx_u8g2.cpp" | { grep -cE 'setBitmapMode[[:space:]]*\([[:space:]]*1' || true; } )
  [ "${n:-0}" -ge 1 ] || fail "ui/gfx_u8g2.cpp never sets setBitmapMode(1) ($n) - gfx_xbm_t() would be opaque on the panel and transparent in every golden (ui/gfx.h)"

  # 5. THE TWO NEW FILMS ARE BOUNDED BY THE CLOCK AND NOT BY A CANCEL.
  #    ui/actfx.h:88-93's contract, and this is the first time it is checkable:
  #    enc_film_phase() must be reached from the render hooks, and the screen
  #    must still cancel on input and on leave. A film whose only bound was a
  #    cancel is a screen nailed to an animation until power-cycle.
  enc_txt=$( strip_comments7 < "$SKETCH/src/ui/screen_encounter.cpp" )
  n=$( printf '%s\n' "$enc_txt" | { grep -cE '\benc_film_phase[[:space:]]*\(' || true; } )
  [ "${n:-0}" -ge 3 ] || fail "ui/screen_encounter.cpp reaches enc_film_phase() only $n time(s) - both render hooks and the phase query need it, or a film draws with nothing deciding whether it is over (ui/screen_encounter.h)"
  #    THE COUNT IS FIVE AND IT IS EXACT: encounter_input, capture_input,
  #    encounter_leave, capture_leave and capture_enter. It was written >= 4 and
  #    the mutation run walked straight through it - deleting the cancel from
  #    capture_leave() left four and passed. A count gate cannot see WHICH
  #    function a call sits in, so the real cover is
  #    any_gesture_skips_a_film_and_the_screen_answers_at_once, which drives all
  #    six gestures on both screens and both leave hooks; this is the backstop
  #    that says the call sites still exist at all.
  n=$( printf '%s\n' "$enc_txt" | { grep -cE '\benc_film_cancel[[:space:]]*\(' || true; } )
  [ "${n:-0}" -ge 5 ] || fail "ui/screen_encounter.cpp calls enc_film_cancel() only $n time(s), not the 5 it needs - both input hooks, both leave hooks and capture_enter must skip a film in flight (ui/screen_encounter.h)"

  # 6. NO HOST TEST MAY ASSERT THE SLEEP FLOOR AGAINST A LITERAL. The floor is
  #    PF_SLEEP_MIN_DIFF and it is measured; a test that hard-coded 10 would
  #    keep passing when somebody moved the constant to make a build green,
  #    which is the one failure mode a measured threshold has.
  n=$( { grep -rn 'PF_SLEEP_MIN_DIFF' "$ROOT/tests/test_sprite_pipeline.cpp" || true; } | wc -l )
  [ "${n:-0}" -ge 3 ] || fail "tests/test_sprite_pipeline.cpp names PF_SLEEP_MIN_DIFF only $n time(s) - the sleep floor must be asserted through the constant, not against a literal (ui/petfx_core.h)"
fi

# =============================================================================
#  P10-C4 - ONBOARDING, ACCESSIBILITY AND THE SECTION 63 AUDIT
# =============================================================================
if [ -f "$SKETCH/src/core/utf8.h" ]; then
  if printf '' | cpp -fpreprocessed -dD -E -P - >/dev/null 2>&1; then
    strip_comments10() { cpp -fpreprocessed -dD -E -P - 2>/dev/null; }
  else
    strip_comments10() { sed 's://.*::'; }
  fi

  # ---------------------------------------------------------------------------
  # 1. THE PLAN'S OWN rd_u8g2() GATE, WHICH HAD NEVER RUN.
  #
  # PEBBLEBOL_IMPLEMENTATION_PLAN.md:72 has carried
  #     grep -c "rd_u8g2()" src/app src/minigames src/game == 0   (P10-C4)
  # since phase 2, and three separate things were wrong with it:
  #
  #   (a) it was in the PLAN and not in this file, so it had never run once;
  #   (b) as written it is a grep ERROR, not a check - no -r, so grep says
  #       "Is a directory" three times and exits 2, and under `set -euo
  #       pipefail` that kills the run BEFORE "GATE OK" is printed. The same
  #       failure shape the P5-C1 comment at the top of this file records;
  #   (c) with -r it passes VACUOUSLY on two of the three directories. src/game
  #       cannot call rd_u8g2() because the gate above already forbids
  #       #include of render.h there - it would not compile - and
  #       minigames/games/*_draw.cpp include ui/gfx.h and nothing lower. Only
  #       src/app has real bite: app/app.cpp includes ../ui/render.h, so it
  #       COULD call rd_u8g2() and does not.
  #
  # SO THE GATE IS THE ONE THE PLAN MEANT AND NOT THE ONE IT WROTE: rd_u8g2()
  # is the single U8G2 instance and it may be reached only from ui/. The scope
  # gains src/dev, which is where the actual leak is - dev/godmode.cpp calls it
  # five times - and that file is EXEMPTED BY NAME with the reason written
  # down, in the PURE_NET / IMPURE_NET idiom this file already uses: all five
  # sit inside `#if GOD_MODE_ENABLED`, so they are not in the release artefact
  # at all. A textual grep cannot see that, which is exactly the phase-8 defect
  # shape ("a gate that counts a token instead of asking what the release
  # artefact does"), so the exemption is a decision recorded here rather than a
  # silence.
  UI_ONLY_EXEMPT="godmode.cpp"
  for d in app minigames game dev networking persistence hardware core data; do
    [ -d "$SKETCH/src/$d" ] || continue
    while IFS= read -r f; do
      case " $UI_ONLY_EXEMPT " in *" $(basename "$f") "*) continue ;; esac
      # `|| true` on the WHOLE pipeline, not just on grep: under `pipefail` a
      # cpp that chokes on a file fails the assignment and `set -e` kills the
      # run silently before "GATE OK". Two generated headers under src/data
      # carry a '#' inside a comment, which -fpreprocessed reads as a bad
      # directive - found by this gate exiting 1 with nothing printed.
      n=$( { strip_comments10 < "$f" | grep -cE '\brd_u8g2[[:space:]]*\(' ; } || true )
      [ "${n:-0}" -eq 0 ] || fail "src/$d/$(basename "$f") calls rd_u8g2() ($n) - the ONE U8G2 instance belongs to ui/ and nothing else may reach past the drawing seam for it (ui/gfx.h). If a layer needs to draw, it draws through gfx.h."
    done <<EOF
$(find "$SKETCH/src/$d" -name '*.cpp' -o -name '*.h' 2>/dev/null)
EOF
  done
  # And the exemption is checked rather than assumed: godmode.cpp's calls must
  # STILL be inside the guard. If somebody moves one above `#if
  # GOD_MODE_ENABLED` the release build starts linking U8G2 through a dev file
  # and this gate would otherwise say nothing.
  gm="$SKETCH/src/dev/godmode.cpp"
  if [ -f "$gm" ]; then
    before=$( awk '/^#if GOD_MODE_ENABLED/{exit} {print}' "$gm" \
              | { grep -cE '\brd_u8g2[[:space:]]*\(' || true; } )
    [ "${before:-0}" -eq 0 ] || fail "dev/godmode.cpp calls rd_u8g2() ($before time(s)) ABOVE its #if GOD_MODE_ENABLED guard - the exemption in this gate is that those calls are compiled out of the release artefact, and that is no longer true"
  fi

  # ---------------------------------------------------------------------------
  # 2. THE CODEPOINT RULE IS ONE RULE. core/utf8.cpp exists because
  #    ui/render.cpp, tests/fakes/gfx_fb.cpp and game/pebble.cpp each carried a
  #    private copy and the three disagreed - one of them by four bytes past a
  #    terminator, which AddressSanitizer reported as a heap over-read. A file
  #    that re-opens its own copy puts the divergence straight back, and
  #    nothing else in the tree could see it.
  for f in "$SKETCH/src/ui/render.cpp" "$ROOT/tests/fakes/gfx_fb.cpp" \
           "$SKETCH/src/game/pebble.cpp"; do
    n=$( strip_comments10 < "$f" | { grep -cE '\bu8_(len|fit|count|cat|cat_n|cat_latin1|from_latin1|well_formed)[[:space:]]*\(' || true; } )
    [ "${n:-0}" -ge 1 ] || fail "$(basename "$f") no longer calls core/utf8.h ($n) - it carried its own copy of the codepoint rule until P10-C4 and the three copies disagreed (core/utf8.h)"
    n=$( strip_comments10 < "$f" | { grep -cE '(0xE0|0xF0|0xF8)[^\n]*(0xC0|0xE0|0xF0)' || true; } )
    [ "${n:-0}" -eq 0 ] || fail "$(basename "$f") has re-opened its own UTF-8 lead-byte table ($n line(s)) - there is exactly one, in core/utf8.cpp (core/utf8.h)"
  done
  # core/utf8.cpp stays on the Arduino-free red line: it is linked into every
  # host binary AND into the firmware, the same standing core/perf.cpp,
  # ui/petfx_core.cpp and dev/diag_core.cpp have.
  for f in utf8.h utf8.cpp; do
    n=$( { grep -cE '^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"](Arduino\.h|U8g2lib\.h)' "$SKETCH/src/core/$f" || true; } )
    [ "${n:-0}" -eq 0 ] || fail "core/$f includes an Arduino header - it is the one codepoint rule and it must stay host-linkable (core/utf8.h)"
  done

  # ---------------------------------------------------------------------------
  # 3. THE TOAST AND THE HELP STRIP ARE ONE BANNER, drawn by ui/gfx_widgets.cpp
  #    - the translation unit BOTH backends link. Before P10-C4 they were two
  #    unbounded centred lines in two files, one of which (ui/ui.cpp) includes
  #    Arduino.h, so no golden in this repository had ever drawn a toast; six of
  #    the twenty-five help strings and four of the fifty toast strings are
  #    wider than the panel. A caller that goes back to drawing its own line
  #    goes back to being unmeasurable.
  n=$( strip_comments10 < "$SKETCH/src/ui/dialog.cpp" | { grep -cE '\bgfx_banner[[:space:]]*\(' || true; } )
  [ "${n:-0}" -ge 1 ] || fail "ui/dialog.cpp does not call gfx_banner() ($n) - the HELP strip would again be an unbounded centred line, and six of the strings that reach it are wider than the panel (ui/gfx.h)"
  n=$( strip_comments10 < "$SKETCH/src/ui/ui.cpp" | { grep -cE '\bgfx_banner[[:space:]]*\(' || true; } )
  [ "${n:-0}" -ge 1 ] || fail "ui/ui.cpp does not call gfx_banner() ($n) - the toast would again be drawn in a translation unit no host binary compiles, where nothing can measure it (ui/gfx.h)"
  n=$( strip_comments10 < "$SKETCH/src/ui/gfx_widgets.cpp" | { grep -cE '\bgfx_banner[[:space:]]*\(' || true; } )
  [ "${n:-0}" -ge 1 ] || fail "ui/gfx_widgets.cpp no longer defines gfx_banner() ($n) - it is implemented ONCE, in the unit both backends link (ui/gfx.h)"

  # ---------------------------------------------------------------------------
  # 4. THE FIRST-BOOT FLOW HAS TO REACH FLASH, AND BOTH HALVES OF IT.
  #    persistence/save_manager.cpp's load_all_inner() answers LOAD_FRESH the
  #    moment the Box pair is missing and returns BEFORE it looks at the config,
  #    so a step written into a config with no Box beside it is a step the next
  #    boot never sees - and the whole "resume after a power cut" requirement
  #    would be a decoration. ui_setup_persist() writes both; this is the gate
  #    on it, because app/app.cpp and ui/ui.cpp are compiled by no host binary.
  #    THE GATE IS SCOPED TO THE FUNCTION BODY, AND THE MUTATION RUN IS WHY.
  #    Written as "ui/ui.cpp calls gs_save_box() at least once" it counted a
  #    token that is not unique - ui_box_activate() and ui_box_swap() call it
  #    too - so deleting the call from ui_setup_persist() walked straight
  #    through and check.sh said GATE OK. That is the phase-8 "gate counted a
  #    type name" defect in a new shape, found by the mutation meant to confirm
  #    the gate rather than by the gate. It reads the BODY now.
  body=$( awk '/^void ui_setup_persist\(void\) \{/{f=1} f{print} f&&/^\}/{exit}' \
            "$SKETCH/src/ui/ui.cpp" | strip_comments10 )
  [ -n "$body" ] || fail "ui/ui.cpp has no ui_setup_persist() body - the first-boot flow would have nothing to persist its answers with (ui/ui.h)"
  n=$( printf '%s\n' "$body" | { grep -cE '\bgs_save_box[[:space:]]*\(' || true; } )
  [ "${n:-0}" -ge 1 ] || fail "ui_setup_persist() does not call gs_save_box() ($n) - persistence/save_manager.cpp answers LOAD_FRESH the moment the Box pair is missing and returns BEFORE it reads the config, so a first boot interrupted by a power cut would come back with every answer discarded (ui/ui.h)"
  n=$( printf '%s\n' "$body" | { grep -cE '\bcfg_persist[[:space:]]*\([[:space:]]*false' || true; } )
  [ "${n:-0}" -ge 1 ] || fail "ui_setup_persist() does not persist the Config silently ($n) - either the step is not written at all, or it is written with a toast that lands on top of the next question (ui/screen_setup.h)"
  # ui_set_starter() IS THE ONE CALL IN THE TREE THAT DESTROYS THE ACTIVE
  # PEBBLE - game/box.cpp's box_release() refuses to, by rule B4 - so it carries
  # two independent locks and this gate is on the one a host binary cannot see.
  # game/box.cpp's lock (an untouched ORIGIN_STARTER) is driven by
  # tests/test_box.cpp; this one is "only while the flow is standing on
  # OB_STARTER", and it lives in ui/ui.cpp, which no host binary compiles.
  body=$( awk '/^bool ui_set_starter\(uint8_t species_id\) \{/{f=1} f{print} f&&/^\}/{exit}' \
            "$SKETCH/src/ui/ui.cpp" | strip_comments10 )
  [ -n "$body" ] || fail "ui/ui.cpp has no ui_set_starter() body - the starter choice would have nothing to write (ui/ui.h)"
  n=$( printf '%s\n' "$body" | { grep -cE '\bob_step[[:space:]]*\(' || true; } )
  [ "${n:-0}" -ge 1 ] || fail "ui_set_starter() does not check ob_step() ($n) - the one call in the tree that replaces the ACTIVE Pebble would be reachable outside the first-boot flow, on every device, for ever (ui/ui.h)"
  n=$( printf '%s\n' "$body" | { grep -cE '\bbox_reroll_starter[[:space:]]*\(' || true; } )
  [ "${n:-0}" -ge 1 ] || fail "ui_set_starter() does not go through box_reroll_starter() ($n) - the second lock, the one about the PEBBLE rather than about the moment, would be bypassed (game/box.h)"

  n=$( strip_comments10 < "$SKETCH/src/app/app.cpp" | { grep -cE '\bob_boot_step[[:space:]]*\([^)]' || true; } )
  [ "${n:-0}" -ge 1 ] || fail "app/app.cpp does not call ob_boot_step() ($n) - the first-boot flow would never be entered, and no host binary compiles this file to say so (app/onboarding.h)"
  # AND NOBODY OUTSIDE app/onboarding.cpp MAY READ THE STEP BITS BY HAND. Two
  # bits in a flags byte are exactly the kind of thing a second reader
  # open-codes with the wrong shift; the accessors exist so there is one.
  while IFS= read -r f; do
    case "$f" in *onboarding.h|*onboarding.cpp|*nt_types.h|*game_state.cpp|*save_schema.h) continue ;; esac
    n=$( { strip_comments10 < "$f" | grep -cE 'CF_SETUP_MASK|CFGV2_F_SETUP_MASK' ; } || true )
    [ "${n:-0}" -eq 0 ] || fail "$(basename "$f") reads the first-boot step bits by hand ($n) - use ob_step()/ob_set_step(), which are the only place the shift is written (app/onboarding.h)"
  done <<EOF
$(find "$SKETCH/src" -name '*.cpp' -o -name '*.h' 2>/dev/null)
EOF

  # ---------------------------------------------------------------------------
  # 5. THE SECTION 63 AUDIT IS A COMPLETENESS CLAIM AND NOT A LIST. kAudit[] in
  #    tests/test_screens.cpp has one row per ScreenId and a static_assert on
  #    its length, so a screen added without a fixture fails the BUILD. The gate
  #    is that the table and its assertion still exist: deleting the assertion
  #    would turn the audit back into "whatever somebody remembered", silently.
  n=$( { grep -c 'kAudit' "$ROOT/tests/test_screens.cpp" || true; } )
  [ "${n:-0}" -ge 4 ] || fail "tests/test_screens.cpp no longer carries the kAudit[] table ($n mentions) - the section 63 audit is what makes a screen added without a fixture a FAILURE instead of a silence"
  n=$( { grep -cE 'static_assert\(sizeof kAudit' "$ROOT/tests/test_screens.cpp" || true; } )
  [ "${n:-0}" -ge 1 ] || fail "tests/test_screens.cpp has lost the static_assert that kAudit[] is SCR_COUNT long ($n) - without it a new screen simply goes unaudited"
fi

# =============================================================================
#  P10-C5 - THE RELEASE BUILD AND THE SCHEMA BUMP
# =============================================================================
if grep -qE '^#define[[:space:]]+SAVE_SCHEMA_INPLACE_MIN[[:space:]]+[0-9]+' \
     "$SKETCH/src/core/version.h"; then
  if printf '' | cpp -fpreprocessed -dD -E -P - >/dev/null 2>&1; then
    strip_comments11() { cpp -fpreprocessed -dD -E -P - 2>/dev/null; }
  else
    strip_comments11() { sed 's://.*::'; }
  fi

  # ---------------------------------------------------------------------------
  # 1. THE WRITE SIDE OF A SCHEMA BUMP, WHICH IS THE HALF NO TEST FOUND FIRST.
  #
  # save_manager.cpp's three flash readers - pair_load(), pair_write() and
  # single_load() - must all judge a stored blob with blob_class(), which knows
  # about BlobOps::min_version, and NOT with blob_ok(), which is "exactly this
  # firmware's version" and is kept only for the exported *_blob_ok() helpers
  # that judge a struct in RAM.
  #
  # pair_write() IS THE ONE THIS GATE EXISTS FOR. It decides which copy of a
  # pair to overwrite and what seq to give it. Asking blob_ok() there meant that
  # on the upgrade boot BOTH copies looked unusable, so the seq restarted at 1
  # beside a copy still holding 99 - and pair_load() takes the highest seq, so
  # the copy the migration had just written was the copy the next boot would not
  # read. It migrated the whole save again. Nothing in the suite could see it
  # until a test wrote BOTH copies of every pair with real seqs, which is a
  # setup no earlier test had needed; the gate is cheaper than remembering that.
  #
  # SCOPED TO THE FUNCTION BODY, for P10-C4's reason: blob_ok() appears eleven
  # times in this file and ten of them are the exported helpers, so a count over
  # the whole file would be a gate that counts a token it cannot judge.
  sm="$SKETCH/src/persistence/save_manager.cpp"
  [ -f "$sm" ] || fail "persistence/save_manager.cpp is missing - it is the only reader of every blob on flash"
  for fn in pair_load pair_write single_load; do
    case "$fn" in
      pair_load)   sig='^static void pair_load' ;;
      pair_write)  sig='^static bool pair_write' ;;
      single_load) sig='^static bool single_load' ;;
    esac
    body=$( awk -v re="$sig" '$0 ~ re {f=1} f{print} f&&/^\}/{exit}' "$sm" | strip_comments11 )
    [ -n "$body" ] || fail "persistence/save_manager.cpp has no $fn() body - every blob on flash is read through it"
    n=$( printf '%s\n' "$body" | { grep -cE '\bblob_class[[:space:]]*\(' || true; } )
    [ "${n:-0}" -ge 1 ] || fail "$fn() does not call blob_class() ($n) - it is judging a stored blob against this firmware's version alone, so a save written by the PREVIOUS firmware is a bad copy: LOAD_CORRUPT, SAVE ERROR, on every played device, on the first flash (persistence/save_manager.cpp, core/version.h)"
    n=$( printf '%s\n' "$body" | { grep -cE '\bblob_ok[[:space:]]*\(' || true; } )
    [ "${n:-0}" -eq 0 ] || fail "$fn() calls blob_ok() ($n) - that is \"exactly this firmware's version\" and it is the wrong question for bytes that came off flash; blob_class() is the one that knows about SAVE_SCHEMA_INPLACE_MIN (persistence/save_manager.cpp)"
  done

  # ---------------------------------------------------------------------------
  # 2. THE MIGRATION CHAIN IS COMPLETE, AND THE COMPILER SAYS SO.
  #
  # The failure is a one-character edit: bump SAVE_SCHEMA_VERSION and forget the
  # MIGRATE_STEPS row. migrate_run() would answer MIGRATE_UNSUPPORTED, the
  # loader would turn that into LOAD_CORRUPT, and the first person to find out
  # would be a player watching SAVE ERROR after a firmware update. The check
  # itself is a static_assert in migration.cpp, because the numbers live in two
  # files and one of them is a table - a grep cannot walk it. THIS gate is that
  # the static_assert still exists: deleting it is silent, and it is exactly the
  # kind of line somebody deletes to make a build green.
  mg="$SKETCH/src/persistence/migration.cpp"
  n=$( { grep -c 'migrate_chain_reaches_current' "$mg" || true; } )
  [ "${n:-0}" -ge 2 ] || fail "persistence/migration.cpp has lost migrate_chain_reaches_current() ($n mentions) - a SAVE_SCHEMA_VERSION bump with no MIGRATE_STEPS row would compile, ship, and read every existing save as LOAD_CORRUPT"
  n=$( { grep -cE 'static_assert\([[:space:]]*migrate_chain_reaches_current' "$mg" || true; } )
  [ "${n:-0}" -ge 1 ] || fail "persistence/migration.cpp defines migrate_chain_reaches_current() but no longer asserts it ($n) - a function nobody calls is not a gate"
  n=$( { grep -cE 'static_assert\([[:space:]]*SAVE_SCHEMA_INPLACE_MIN' "$mg" || true; } )
  [ "${n:-0}" -ge 1 ] || fail "persistence/migration.cpp no longer asserts the SAVE_SCHEMA_INPLACE_MIN range ($n) - the floor is a claim about LAYOUT, and a floor of 1 would read v1's differently shaped structs in place"

  # ---------------------------------------------------------------------------
  # 3. THE CREATOR REGISTRY IS OUTSIDE GameState, SO THE CHAIN CANNOT REACH IT.
  #
  # cs0..cs9 are not part of the state migrate_run() transforms, so their
  # upgrade lives in custom_species_install_all(). Without it
  # validate_custom_species() refuses the record by name (VR_CS_BAD_HEADER), the
  # slot stays empty, and every Pebble pointing at it comes back
  # VR_UNKNOWN_SPECIES - the creature the owner designed, gone on the first boot
  # after a firmware update.
  body=$( awk '/^static void custom_species_install_all\(void\) \{/{f=1} f{print} f&&/^\}/{exit}' \
            "$sm" | strip_comments11 )
  [ -n "$body" ] || fail "persistence/save_manager.cpp has no custom_species_install_all() body - species_get() resolves a creator id through that registry and through nothing else"
  n=$( printf '%s\n' "$body" | { grep -cE '\bcustom_species_seal[[:space:]]*\(' || true; } )
  [ "${n:-0}" -ge 1 ] || fail "custom_species_install_all() does not re-seal a downlevel record ($n) - a schema bump would delete every creature made in the creator portal and quarantine the Pebbles that point at them (persistence/save_manager.cpp)"

  # ---------------------------------------------------------------------------
  # 4. THE TWO VERSION NUMBERS AGREE WITH EACH OTHER AND WITH THE TEST.
  #    tests/test_persistence.cpp pins the literal, which is the only thing that
  #    makes a bump a deliberate act rather than a typo somebody carries.
  sv=$( grep -oE '^#define[[:space:]]+SAVE_SCHEMA_VERSION[[:space:]]+[0-9]+' \
          "$SKETCH/src/core/version.h" | grep -oE '[0-9]+$' )
  n=$( { grep -cE "CHECK_EQ\(SAVE_SCHEMA_VERSION, $sv\)" "$ROOT/tests/test_persistence.cpp" || true; } )
  [ "${n:-0}" -ge 1 ] || fail "tests/test_persistence.cpp does not pin SAVE_SCHEMA_VERSION at $sv ($n) - the number moved and the test that makes moving it deliberate did not"
fi

# =============================================================================
#  P10-C6 - THE EXIT GATES
#
#  Every one of these was broken and watched fail by name before it was kept.
#  They exist because the thing they guard lives in a file NO HOST BINARY
#  COMPILES - ui/ui.cpp, app/app.cpp, ui/petfx.cpp, dev/godmode.cpp - or in a
#  place a count can be satisfied by something adjacent to the thing that
#  matters, which is this project's oldest recurring defect.
# =============================================================================
if printf '' | cpp -fpreprocessed -dD -E -P - >/dev/null 2>&1; then
  strip_comments12() { cpp -fpreprocessed -dD -E -P - 2>/dev/null; }
else
  strip_comments12() { sed 's://.*::'; }
fi
# ...and for the allocator gate, STRING LITERALS TOO. cpp does not touch them,
# and game/battle.cpp carries a static_assert whose MESSAGE contains "a new one
# would resolve as", which is prose about a battle slot and not an allocation.
strip_code12() { strip_comments12 | sed 's/"[^"]*"/""/g'; }

# -----------------------------------------------------------------------------
# 1. THE NAME ON THE WIRE IS THE STORED ENCODING, NOT THE DRAWN ONE.
#
#    DiscBeacon.name is LATIN-1 by contract: networking/discovery.cpp's
#    name_ok() refuses 0x80..0x9F, and every Latin-1 accent (0xC0..0xDF, exactly
#    the uppercase set ui/screen_setup.cpp's naming ring can type) encodes to
#    0xC3 plus a byte inside that window. P10-C4 made ui_pet_name() emit UTF-8 -
#    correctly, for everything that DRAWS - and fill_self() went on handing it
#    to disc_encode(), which answered DE_NAME. A device with an accented name
#    emitted NO BEACON AT ALL: invisible to every peer, LINK/trade/P2P battle
#    dead, and silent, because link_service() drops an unencodable beacon by
#    design. The host suite could not see it: ui/ui.cpp is compiled by no
#    binary and the fake stood in with the pre-P10-C4 body.
body=$( awk '/^static void fill_self\(DiscBeacon& b\) \{/{f=1} f{print} f&&/^\}/{exit}' \
          "$SKETCH/src/ui/screen_link.cpp" | strip_comments12 )
[ -n "$body" ] || fail "ui/screen_link.cpp has no fill_self() body - nothing fills the beacon this device puts on the air"
n=$( printf '%s\n' "$body" | { grep -cE '\bui_pet_name_latin1[[:space:]]*\(' || true; } )
[ "${n:-0}" -ge 1 ] || fail "fill_self() does not call ui_pet_name_latin1() ($n) - DiscBeacon.name is LATIN-1 by contract and discovery.cpp's name_ok() refuses 0x80..0x9F, so a UTF-8 name makes disc_encode() answer DE_NAME and the device emits NO BEACON AT ALL for any accented name (ui/ui.h)"
n=$( printf '%s\n' "$body" | { grep -cE '\bui_pet_name[[:space:]]*\(' || true; } )
[ "${n:-0}" -eq 0 ] || fail "fill_self() calls ui_pet_name() ($n) - that is the DRAWN form, UTF-8, and the beacon field is Latin-1 (core/utf8.h)"
# ...AND THE FAKE THAT STANDS IN FOR IT MUST NOT BE THE OLD BODY. That is what
# hid this for a whole chunk: tests/fakes/link_fake.cpp defined its own
# ui_pet_name() as a plain snprintf, so every discovery and link test drove a
# function the firmware no longer had.
n=$( strip_comments12 < "$ROOT/tests/fakes/link_fake.cpp" | { grep -cE '\bui_pet_name_latin1[[:space:]]*\(' || true; } )
[ "${n:-0}" -ge 1 ] || fail "tests/fakes/link_fake.cpp does not implement ui_pet_name_latin1() ($n) - it shadows ui/ui.cpp's name seam, so the link tests would be driving a body the firmware does not have (the P10-C6 defect exactly)"
n=$( strip_comments12 < "$ROOT/tests/fakes/link_fake.cpp" | { grep -cE '\bu8_from_latin1[[:space:]]*\(' || true; } )
[ "${n:-0}" -ge 1 ] || fail "tests/fakes/link_fake.cpp's ui_pet_name() does not cross to UTF-8 ($n) - it would be the pre-P10-C4 body again and no test could see the difference"

# -----------------------------------------------------------------------------
# 2. ALL THREE RADIO OWNERS ARE IN THE POWER LADDER'S `held` INPUT.
#
#    app/app.cpp feeds PowerInput.held from ui_radio_job_busy(). The creator
#    portal is the third radio owner and was in neither term, so the ladder
#    reached PWR_IDLE at 120,000 ms and pwr_hook_release() ran ui_home(), whose
#    sm_goto() runs creator_leave() - the AP torn down under a phone that was
#    still drawing on it, 180 s before decision D7's own timer. Neither the
#    mobile editor nor the sprite editor could be used, and docs/bench.md D2
#    could not pass as written. networking/discovery.h:254 static_asserts the
#    same invariant for the link job; nothing held the portal.
body=$( awk '/^bool ui_radio_job_busy\(void\) \{/{f=1} f{print} f&&/^\}/{exit}' \
          "$SKETCH/src/ui/ui.cpp" | strip_comments12 )
[ -n "$body" ] || fail "ui/ui.cpp has no ui_radio_job_busy() body - the power ladder's `held` input would be gone"
for owner in network_screen_busy link_screen_busy creator_screen_busy; do
  n=$( printf '%s\n' "$body" | { grep -cE "\\b${owner}[[:space:]]*\\(" || true; } )
  [ "${n:-0}" -ge 1 ] || fail "ui_radio_job_busy() does not name ${owner}() ($n) - that radio owner is not held, so hardware/power.cpp's PWR_IDLE rung takes the radio away from it at 120 s while the screen believes it still has it (ui/screen_creator.h)"
done
n=$( strip_comments12 < "$SKETCH/src/app/app.cpp" | { grep -cE '\bui_radio_job_busy[[:space:]]*\(' || true; } )
[ "${n:-0}" -ge 1 ] || fail "app/app.cpp does not feed PowerInput.held from ui_radio_job_busy() ($n) - every radio owner would be dropped at the idle rung and no host binary compiles this file to say so"
# The portal's hold is set and cleared by the ONE teardown, so it can never
# outlive the access point it speaks for.
for fn in creator_enter creator_leave; do
  body=$( awk "/^void ${fn}\\(void\\) \\{/{f=1} f{print} f&&/^\\}/{exit}" \
            "$SKETCH/src/ui/screen_creator.cpp" | strip_comments12 )
  [ -n "$body" ] || fail "ui/screen_creator.cpp has no ${fn}() body"
  n=$( printf '%s\n' "$body" | { grep -cE '\bs_holding[[:space:]]*=' || true; } )
  [ "${n:-0}" -ge 1 ] || fail "${fn}() does not move the portal's radio hold ($n) - set in enter and cleared in leave, or it outlives the access point (ui/screen_creator.h)"
done

# -----------------------------------------------------------------------------
# 3. GF_TINY IS ASCII-ONLY AND NO SPANISH PROSE MAY BE DRAWN IN IT.
#
#    RD_FONT_TINY is u8g2_font_4x6_tr: 95 glyphs, ASCII. u8g2's drawUTF8()
#    emits NOTHING and ADVANCES NOTHING for a codepoint the face lacks, so the
#    character vanishes and the line closes up. Five accented strings were
#    drawn in it - three on the first-boot flow, the first screens a device
#    ever shows - and every golden was correct, because tests/fakes/gfx_fb.cpp
#    painted a synthetic glyph for any codepoint at a fixed advance. The rule
#    was written down twice in the tree (ui/gfx.h, ui/screen_creator.cpp) and
#    enforced nowhere.
#
#    THE FAKE'S RECORDER IS THE REAL INSTRUMENT and it holds for a string
#    nobody thought to list. This is the backstop: it can only see a LITERAL
#    S(STR_X) at a GF_TINY call site, which is the form all five took.
tiny_ids=$( grep -rhoE 'gfx_text[a-z_]*\([[:space:]]*GF_TINY[^;]*S\([[:space:]]*(STR_[A-Z0-9_]+)' \
              "$SKETCH/src/ui" "$SKETCH/src/minigames" --include='*.cpp' 2>/dev/null \
            | grep -oE 'STR_[A-Z0-9_]+$' | sort -u )
for id in $tiny_ids; do
  line=$( grep -E "/\\* ${id} \\*/" "$SKETCH/src/core/strings_es.h" | head -1 )
  [ -n "$line" ] || continue
  if printf '%s' "$line" | LC_ALL=C grep -q '[^ -~]'; then
    fail "$id is drawn in GF_TINY and holds a byte outside ASCII - RD_FONT_TINY is the 95-glyph 4x6 face (ui/render.h) and drawUTF8() emits NO GLYPH AND NO ADVANCE for a codepoint it lacks, so the character disappears on the panel and the line closes up, while every host golden renders it correctly"
  fi
done
n=$( strip_comments12 < "$ROOT/tests/fakes/gfx_fb.cpp" | { grep -cE '\bnote_glyph[[:space:]]*\(' || true; } )
[ "${n:-0}" -ge 2 ] || fail "tests/fakes/gfx_fb.cpp does not record glyphs the font cannot draw ($n) - the recorder is the only instrument that sees this class for a string nobody listed (tests/fakes/gfx_fb.h)"
n=$( { grep -cE '\bfb_no_glyph[[:space:]]*\(' "$ROOT/tests/test_screens.cpp" || true; } )
[ "${n:-0}" -ge 3 ] || fail "tests/test_screens.cpp does not assert fb_no_glyph() ($n) - the recorder would record and nothing would read it"

# -----------------------------------------------------------------------------
# 4. THE NEVER-RESIDENT EXEMPTION IS DERIVED FROM THE TABLE, NOT RETYPED.
#
#    tests/test_statemachine.cpp's kExits answers XK_NOT_RESIDENT for a screen
#    nothing navigates to, and that answer asserts only sm_current() != scr -
#    trivially true for a screen nothing has pushed YET. The real content is
#    "nothing in the firmware ever navigates there", and it was delegated to a
#    HARDCODED LIST OF FOUR NAMES in this file. A new XK_NOT_RESIDENT row was
#    therefore an unverified assertion: a screen with SF_STICKY|SF_LOCK_INPUT
#    and a live ui_goto() passed the whole suite and printed GATE OK while
#    being a true dead end. The list comes out of the table now.
nr=$( awk '/^static const ExitRow kExits/,/^};/' "$ROOT/tests/test_statemachine.cpp" \
        | grep 'XK_NOT_RESIDENT' | sed -n 's/.*{[[:space:]]*\(SCR_[A-Z_0-9]*\).*/\1/p' | sort -u )
[ -n "$nr" ] || fail "tests/test_statemachine.cpp's kExits has no XK_NOT_RESIDENT row - the derived exemption list below would be empty and this gate would pass vacuously"
for scr in $nr; do
  n=$( cat "$SKETCH/src/ui"/*.cpp "$SKETCH/src/app"/*.cpp 2>/dev/null | strip_comments12 \
       | { grep -cE "\\bui_(goto|push)[[:space:]]*\\([[:space:]]*${scr}[[:space:]]*\\)" || true; } )
  [ "${n:-0}" -eq 0 ] || fail "$scr is navigated to from src/ui or src/app ($n) - kExits exempts it as never-resident, so its exit is asserted by nothing and it now needs a real timeout and a user-visible exit (spec section 47)"
done

# -----------------------------------------------------------------------------
# 5. THE FILM CALL-SITE COUNTS ARE EXACT, AND THE DEFINITION IS NOT A CALL.
#
#    Both counts were one short: `grep -c enc_film_cancel` over the stripped
#    file matches the FUNCTION DEFINITION as well as the five call sites, so
#    the >= 5 threshold had a spare unit and any one call site could be
#    deleted with GATE OK printed. The gate's own comment claimed "THE COUNT IS
#    FIVE AND IT IS EXACT". It is exact now: definitions are excluded and the
#    comparison is -eq.
enc="$SKETCH/src/ui/screen_encounter.cpp"
n=$( strip_comments12 < "$enc" \
     | { grep -E '\benc_film_cancel[[:space:]]*\([[:space:]]*\)[[:space:]]*;' || true; } \
     | { grep -vcE '^void[[:space:]]+enc_film_cancel[[:space:]]*\(' || true; } )
[ "${n:-0}" -eq 5 ] || fail "ui/screen_encounter.cpp has $n enc_film_cancel() CALL SITES, not 5 - the five are encounter_leave, encounter_input, capture_enter, capture_leave and capture_input, and the count excludes the definition now because including it left a spare unit that let any one of them be deleted with GATE OK printed"
n=$( strip_comments12 < "$enc" \
     | { grep -E '\benc_film_phase[[:space:]]*\([[:space:]]*\)' || true; } \
     | { grep -vcE '^uint8_t[[:space:]]+enc_film_phase[[:space:]]*\(' || true; } )
[ "${n:-0}" -eq 3 ] || fail "ui/screen_encounter.cpp has $n enc_film_phase() USES, not 3 (the definition is excluded now) - the two render call sites and the guard are what draw the films at all"

# -----------------------------------------------------------------------------
# 6. THE PASS STAMP IS ABOVE THE YIELD **AND THERE IS EXACTLY ONE OF IT**.
#
#    P10-C2 gated the ORDER by line number, and the mutation that beats a line
#    comparison is not a move but an ADDITION: a second perf_note_pass() below
#    pwr_yield() leaves the first one above it, so the gate stays green while
#    the worst pass reads PWR_SLEEP_SLICE_MS on a healthy sleeping board -
#    which is the exact silent lie the original gate exists to prevent.
n=$( strip_comments12 < "$SKETCH/src/app/app.cpp" | { grep -cE '\bperf_note_pass[[:space:]]*\(' || true; } )
[ "${n:-0}" -eq 1 ] || fail "app/app.cpp calls perf_note_pass() $n times, not exactly once - a second stamp below pwr_yield() would record the deliberate nap as work and read up to PWR_SLEEP_SLICE_MS on a healthy sleeping board, with every test and the line-order gate still green (core/perf.h)"

# -----------------------------------------------------------------------------
# 7. NO HEAP IN THE PURE LAYERS - THE GATE tests/test_soak.cpp SAID EXISTED.
#
#    That file's banner claimed "tools/check.sh already forbids new/malloc
#    inside src/game/** by grep". It did not: the only src/game gate was the
#    #include purity one, and a balanced malloc/free pair in sim_tick() ran
#    86,400 times inside the armed window with both cases green, because the
#    counter replaces operator new only. AddressSanitizer catches the LEAKING
#    shape; a balanced pair is allocator churn that fragments a real heap and
#    nothing could see it. Now the claim is true.
# Comments are stripped PER FILE, because three of the matches this gate first
# produced were prose - "brand new creature", "a new one would resolve as", "a
# new Pebble is a well one" - two of them trailing comments and one a string
# literal inside a static_assert. A line-position filter cannot see those.
while IFS= read -r f; do
  [ -n "$f" ] || continue
  n=$( strip_code12 < "$f" | { grep -cE '\b(malloc|calloc|realloc|strdup|aligned_alloc|free)[[:space:]]*\(' || true; } )
  [ "${n:-0}" -eq 0 ] || fail "${f#$SKETCH/src/} calls a C allocator ($n) - the pure layers allocate nothing at all, and tests/test_soak.cpp's zero is a count of operator new only, so a balanced malloc/free pair would be invisible to it AND to AddressSanitizer (which sees a leak, not churn)"
  n=$( strip_code12 < "$f" | { grep -cE '(^|[^_[:alnum:]:])new[[:space:]]+[A-Za-z_(]' || true; } )
  [ "${n:-0}" -eq 0 ] || fail "${f#$SKETCH/src/} uses operator new ($n) - no heap in game logic (the red line in PEBBLEBOL_IMPLEMENTATION_PLAN.md section 2)"
done <<EOF
$(find "$SKETCH/src/game" "$SKETCH/src/minigames" -name '*.cpp' -o -name '*.h' 2>/dev/null)
$SKETCH/src/ui/petfx_core.cpp
$SKETCH/src/ui/anim_ease.cpp
$SKETCH/src/ui/corrupt_fx.cpp
$SKETCH/src/core/crc16.cpp
$SKETCH/src/core/rng.cpp
$SKETCH/src/core/utf8.cpp
$SKETCH/src/core/perf.cpp
$SKETCH/src/dev/diag_core.cpp
$SKETCH/src/networking/protocol.cpp
$SKETCH/src/networking/session.cpp
EOF

# -----------------------------------------------------------------------------
# 8. THE ERROR SCREEN'S KINDS ARE COUNTED AGAINST THE ENUM.
#
#    tests/test_statemachine.cpp claimed its assertion "is what fails if a
#    fourth kind is added without a row" and counted ROWS IN kExits. Adding a
#    fourth ErrKind with no row left the whole suite green and printed GATE OK.
# COMMENTS STRIPPED, AND THE MUTATION IS WHY. Written as a plain grep over the
# header this gate printed GATE OK with ERRK_COUNT renamed away, because the
# nine-line comment ABOVE the enum explains what ERRK_COUNT is for and the grep
# counted that. A gate satisfied by prose about the thing it guards is this
# project's oldest defect, reproduced inside the gate written against it.
n=$( strip_comments12 < "$SKETCH/src/ui/screen_error.h" | { grep -cE '\bERRK_COUNT\b' || true; } )
[ "${n:-0}" -ge 1 ] || fail "ui/screen_error.h has no ERRK_COUNT ($n) - SCR_ERROR is three states wearing one ScreenId and the completeness claim about them would have nothing to count against"
n=$( strip_comments12 < "$ROOT/tests/test_statemachine.cpp" | { grep -cE '\bERRK_COUNT\b' || true; } )
[ "${n:-0}" -ge 2 ] || fail "tests/test_statemachine.cpp does not derive its ERROR-kind sweep from ERRK_COUNT ($n) - it would be a hand-typed list again, and a fourth kind with no exit row would pass"

# -----------------------------------------------------------------------------
# 8b. THE RESET-REASON TABLE IS THE ONE THE HOST SWEPT.
#
#     §67's "Time-based calculations work across reboot" was ticked on the
#     arithmetic that decides what a BootKind MEANS, and nothing had ever
#     turned an esp_reset_reason() into one: hardware/boot.cpp is compiled by
#     no host binary and tests/fakes/boot_host.cpp collapsed seven kinds to
#     two. The table is pure and host-swept now; this holds the two halves
#     together - boot.cpp must go through it rather than keeping a second copy,
#     and the ESP-IDF mapping must still be asserted where the real enum is in
#     scope.
n=$( strip_comments12 < "$SKETCH/src/hardware/boot.cpp" | { grep -cE '\bboot_classify[[:space:]]*\(' || true; } )
[ "${n:-0}" -ge 1 ] || fail "hardware/boot.cpp does not call boot_classify() ($n) - the reset-reason table would be a second copy again, in a file no host binary compiles, deciding whether a reboot charges elapsed game time at all (hardware/boot_reason.h)"
n=$( strip_comments12 < "$SKETCH/src/hardware/boot.cpp" | { grep -cE 'static_assert\(\(int\)BR_' || true; } )
[ "${n:-0}" -ge 16 ] || fail "hardware/boot.cpp asserts only $n of the 16 esp_reset_reason_t values against hardware/boot_reason.h - the host sweeps BR_* by VALUE because it cannot see the real enum, and those static_asserts are what make that a fact rather than a hopeful copy"
n=$( strip_comments12 < "$SKETCH/src/hardware/boot_reason.h" | { grep -cE '(Arduino\.h|esp_system\.h|esp_attr\.h)' || true; } )
[ "${n:-0}" -eq 0 ] || fail "hardware/boot_reason.h includes a device header ($n) - it exists to be host-linkable (tests/test_clock.cpp)"
n=$( { grep -cE '\bboot_classify[[:space:]]*\(' "$ROOT/tests/test_clock.cpp" || true; } )
[ "${n:-0}" -ge 5 ] || fail "tests/test_clock.cpp does not sweep boot_classify() ($n) - the classifier that feeds the whole absence path would be executed by nothing again"

# -----------------------------------------------------------------------------
# 9. THE PERF RECEIPT'S SCREEN COLUMN IS INTERPRETABLE, AND THE ITEM SAYS WHAT
#    IT DID. Two dead-data findings, both in the shipping artefact.
n=$( strip_comments12 < "$SKETCH/src/dev/godmode.cpp" | { grep -cE '\bdiag_screen_name[[:space:]]*\(' || true; } )
[ "${n:-0}" -ge 1 ] || fail "dev/godmode.cpp does not emit the ScreenId map ($n) - the DIAG,perf row's scr columns are raw enum values, docs/bench.md A1 reads them, and this enum was renumbered mid-list inside phase 10 (dev/diag_core.h)"
n=$( strip_comments12 < "$SKETCH/src/dev/godmode.cpp" | { grep -cE '\bespnow_stats[[:space:]]*\(' || true; } )
[ "${n:-0}" -ge 1 ] || fail "dev/godmode.cpp has no reader for espnow_stats() ($n) - docs/bench.md B2 names rx_wrong_peer as its acceptance instrument and it was a dead export with no reader anywhere in the tree"
body=$( awk '/^static void bag_use\(void\) \{/{f=1} f{print} f&&/^\}/{exit}' \
          "$SKETCH/src/ui/screen_care.cpp" | strip_comments12 )
[ -n "$body" ] || fail "ui/screen_care.cpp has no bag_use() body"
n=$( printf '%s\n' "$body" | { grep -cE '\bitem_reaction[[:space:]]*\(' || true; } )
[ "${n:-0}" -ge 1 ] || fail "bag_use() does not report the ItemEffect ($n) - game/inventory.cpp fills the whole struct and the screen would discard it again, answering one word for every item in the game (game/inventory.h)"

echo "GATE OK"
