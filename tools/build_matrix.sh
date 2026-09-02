#!/usr/bin/env bash
# tools/build_matrix.sh — compile every feature variant (plan §1.1) and require
# 0 project warnings in each. Run at every phase tag.
#
# Variants are expressed as config.h numeric #define overrides applied to a
# temporary copy of the sketch by tools/build.sh; the repo is never edited.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
B="$ROOT/tools/build.sh"

CFG="$ROOT/Pebblebol/config.h"; [ -f "$CFG" ] || CFG="$ROOT/Pebblebol/src/core/config.h"
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
for v in "${VARIANTS[@]}"; do
  name="${v%%|*}"; defs="${v#*|}"
  args=()
  for d in $defs; do
    k="${d%%=*}"
    if has "$k"; then args+=(--define "$d"); fi
  done
  if "$B" --quiet --variant "$name" "${args[@]}"; then :; else rc=1; fi
done
[ $rc -eq 0 ] && echo "MATRIX OK" || { echo "MATRIX FAIL" >&2; exit 1; }
