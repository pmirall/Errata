#!/usr/bin/env bash
# tools/build_matrix.sh — compile every feature variant (plan §1.1) and require
# 0 project warnings in each. Run at every phase tag.
#
# Variants are expressed as config.h numeric #define overrides applied to a
# temporary copy of the sketch by tools/build.sh; the repo is never edited.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
B="$ROOT/tools/build.sh"

CFG="$ROOT/Pebblebol/src/core/config.h"
has() { grep -qE "^#define[[:space:]]+$1[[:space:]]+[0-9]+" "$CFG"; }

# name|defines (space separated). Macros that do not exist in config.h are SKIPPED
# AND SAID SO ON STDOUT (see the loop below) - which they were not until P7-C1,
# and the silence was the defect the phase-6 exit carried forward. `all-off`
# names six macros and only three of them existed: FEATURE_WEATHER,
# FEATURE_TELEGRAM and FEATURE_ESPNOW had no numeric #define, so the variant was
# really FEATURE_WEB=0 GOD_MODE_ENABLED=0 (and, until P8-C0, FEATURE_BLE=0) and
# nothing said so.
# P7-C1 ADDS A REAL FEATURE_ESPNOW, so `all-off` changed meaning at this tag: it
# now also compiles the peer link out, which is a different and stricter floor
# than the one v0.6.0-activity measured. That is exactly the change the plan
# said must not happen with no line in the log, so the log now carries one on
# every run and the phase-7 exit's variant table records the step.
#
# THE `no-ble` VARIANT WENT IN P8-C0 with FEATURE_BLE itself. It was here to
# measure what deleting BLE would be worth; the answer was taken and acted on,
# so the row it produced is now `baseline`. Seven variants became six.
VARIANTS=(
  "baseline|"
  "no-web|FEATURE_WEB=0"
  "no-god|GOD_MODE_ENABLED=0"
  "sh1106|DISPLAY_IS_SH1106=1"
  "all-off|FEATURE_WEATHER=0 FEATURE_TELEGRAM=0 FEATURE_WEB=0 FEATURE_ESPNOW=0 GOD_MODE_ENABLED=0"
  "release|GOD_MODE_ENABLED=0"
)

rc=0
rel_line=""
for v in "${VARIANTS[@]}"; do
  name="${v%%|*}"; defs="${v#*|}"
  args=()
  for d in $defs; do
    k="${d%%=*}"
    if has "$k"; then
      args+=(--define "$d")
    else
      # LOUD, not silent. A variant whose name says "all off" and whose defines
      # are quietly dropped is a floor that means something different from what
      # it is called, and the difference only shows up as a size number nobody
      # can explain two phases later.
      echo "variant $name: SKIPPING $d - no numeric #define $k in config.h"
    fi
  done
  # The output is captured rather than streamed so the release variant's own size
  # line can be reused below instead of being built a second time; it is printed
  # immediately either way, and a failing build's diagnostics come through it.
  if out=$("$B" --quiet --variant "$name" "${args[@]}" 2>&1); then :; else rc=1; fi
  printf '%s\n' "$out"
  if [ "$name" = "release" ]; then rel_line=$(printf '%s' "$out" | tail -1); fi
done
# THE SHIPPING BUILD'S OWN CAPS. check.sh polices the baseline, which carries the
# legacy web UI and god mode - neither ships. It also carried BLE until P8-C0, which
# is where most of the gap went: measured at the phase-4 exit the two builds differed
# by 724,228 B of flash and 23,672 B of globals (docs/budget.md section 1), so the
# number under pressure was one that corresponded to no artefact.
#
# The size line comes from the release variant the loop above ALREADY built. Until the
# phase-5 exit this block built it a second time and said "this costs nothing but the
# comparison", which was wrong by one whole compile: the matrix is eight builds of a
# seven-variant table, and one of them was the same build twice. Reusing $rel_line is
# also the stricter reading - the capped figures are now, by construction, the ones the
# matrix printed, and cannot silently be a different compile's.
rel_max_f=$(grep -oP '^#define\s+GATE_RELEASE_FLASH_MAX\s+\K[0-9]+' "$CFG" || echo 0)
rel_max_g=$(grep -oP '^#define\s+GATE_RELEASE_GLOBALS_MAX\s+\K[0-9]+' "$CFG" || echo 0)
if [ "$rel_max_f" -gt 0 ]; then
  # `|| true` is load-bearing and its absence was a defect this exit MEASURED, not a
  # style point. Under `set -euo pipefail` a grep that matches nothing fails, the
  # command substitution fails with it, and the script dies ON THE ASSIGNMENT - so the
  # `else` arm below, "could not read the release build's size line", COULD NEVER FIRE.
  # Reproduced with a stub builder that fails the release variant: the original block
  # exited 1 in silence, printing no MATRIX FAIL line at all. Same shape as the dormant
  # WiFi.begin gate P5-C1 armed, and the same house form fixes it.
  rf=$( { printf '%s' "$rel_line" | grep -oE 'flash=[0-9]+'   || true; } | cut -d= -f2)
  rg=$( { printf '%s' "$rel_line" | grep -oE 'globals=[0-9]+' || true; } | cut -d= -f2)
  if [ -n "$rf" ] && [ -n "$rg" ]; then
    echo "release caps: flash $rf/$rel_max_f  globals $rg/$rel_max_g"
    [ "$rf" -le "$rel_max_f" ] || { echo "MATRIX FAIL: release flash $rf over $rel_max_f" >&2; rc=1; }
    [ "$rg" -le "$rel_max_g" ] || { echo "MATRIX FAIL: release globals $rg over $rel_max_g" >&2; rc=1; }
  else
    echo "MATRIX FAIL: could not read the release build's size line" >&2; rc=1
  fi
fi

[ $rc -eq 0 ] && echo "MATRIX OK" || { echo "MATRIX FAIL" >&2; exit 1; }
