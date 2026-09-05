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

# name|defines (space separated). Macros that no longer exist are skipped.
VARIANTS=(
  "baseline|"
  "no-ble|FEATURE_BLE=0"
  "no-web|FEATURE_WEB=0"
  "no-god|GOD_MODE_ENABLED=0"
  "sh1106|DISPLAY_IS_SH1106=1"
  "all-off|FEATURE_WEATHER=0 FEATURE_TELEGRAM=0 FEATURE_BLE=0 FEATURE_WEB=0 FEATURE_ESPNOW=0 GOD_MODE_ENABLED=0"
  "release|GOD_MODE_ENABLED=0 FEATURE_BLE=0"
)

rc=0
rel_line=""
for v in "${VARIANTS[@]}"; do
  name="${v%%|*}"; defs="${v#*|}"
  args=()
  for d in $defs; do
    k="${d%%=*}"
    if has "$k"; then args+=(--define "$d"); fi
  done
  # The output is captured rather than streamed so the release variant's own size
  # line can be reused below instead of being built a second time; it is printed
  # immediately either way, and a failing build's diagnostics come through it.
  if out=$("$B" --quiet --variant "$name" "${args[@]}" 2>&1); then :; else rc=1; fi
  printf '%s\n' "$out"
  if [ "$name" = "release" ]; then rel_line=$(printf '%s' "$out" | tail -1); fi
done
# THE SHIPPING BUILD'S OWN CAPS. check.sh polices the baseline, which carries BLE and
# the legacy web UI - neither ships. Measured at the phase-4 exit the two differ by
# 724,228 B of flash and 23,672 B of globals (docs/budget.md section 1), so the number
# under pressure was one that corresponds to no artefact.
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
