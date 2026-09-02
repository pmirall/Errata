#!/usr/bin/env bash
# tools/build.sh — arduino-cli wrapper for the Pebblebol firmware.
#
# Compiles the sketch with --warnings all, fails on any warning that points into
# the sketch (core/library warnings are ignored, exactly like the baseline), and
# prints the parsed "Sketch uses" / "Global variables use" figures.
#
# Usage: tools/build.sh [--variant NAME] [--define MACRO=VALUE]... [--build-path DIR]
#   --define      overrides a `#define MACRO <number>` line in config.h in a temporary
#                 copy of the sketch (used by build_matrix.sh); the repo is untouched.
#   --quiet       print only the summary line.
# Environment: FQBN (default below), SKETCH (default <repo>/Pebblebol).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SKETCH="${SKETCH:-$ROOT/Pebblebol}"
FQBN="${FQBN:-esp32:esp32:esp32c3:PartitionScheme=huge_app,CDCOnBoot=cdc}"
VARIANT="baseline"
BUILD_PATH=""
QUIET=0
DEFINES=()

while [ $# -gt 0 ]; do
  case "$1" in
    --variant)    VARIANT="$2"; shift 2 ;;
    --define)     DEFINES+=("$2"); shift 2 ;;
    --build-path) BUILD_PATH="$2"; shift 2 ;;
    --quiet)      QUIET=1; shift ;;
    *) echo "build.sh: unknown argument $1" >&2; exit 2 ;;
  esac
done

command -v arduino-cli >/dev/null || { echo "build.sh: arduino-cli not on PATH" >&2; exit 2; }
[ -d "$SKETCH" ] || { echo "build.sh: sketch dir $SKETCH not found" >&2; exit 2; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
LOG="$WORK/compile.log"

# A variant build compiles a temporary copy so config.h in the repo is never edited.
SRC="$SKETCH"
if [ ${#DEFINES[@]} -gt 0 ]; then
  SRC="$WORK/$(basename "$SKETCH")"
  cp -r "$SKETCH" "$SRC"
  CFG="$SRC/config.h"
  [ -f "$CFG" ] || CFG="$SRC/src/core/config.h"
  for kv in "${DEFINES[@]}"; do
    k="${kv%%=*}"; v="${kv##*=}"
    if ! grep -qE "^#define[[:space:]]+$k[[:space:]]+[0-9]+" "$CFG"; then
      echo "build.sh: --define $k: no numeric #define in $(basename "$CFG")" >&2; exit 2
    fi
    sed -i -E "s/^(#define[[:space:]]+$k)[[:space:]]+[0-9]+/\1 $v/" "$CFG"
  done
fi

[ -n "$BUILD_PATH" ] || BUILD_PATH="$WORK/build"

set +e
arduino-cli compile --fqbn "$FQBN" --warnings all --build-path "$BUILD_PATH" "$SRC" >"$LOG" 2>&1
RC=$?
set -e

# Warnings that point into the sketch itself (any path under the sketch copy).
PROJ_WARN="$(grep -E "warning:" "$LOG" | grep -F "$SRC/" || true)"
PROJ_WARN_N="$(printf '%s' "$PROJ_WARN" | grep -c . || true)"
FLASH="$(grep -oE "Sketch uses [0-9]+ bytes" "$LOG" | grep -oE "[0-9]+" || echo 0)"
GLOBALS="$(grep -oE "Global variables use [0-9]+ bytes" "$LOG" | grep -oE "[0-9]+" || echo 0)"

if [ $RC -ne 0 ]; then
  [ $QUIET -eq 1 ] || grep -E "error:|Error|undefined reference" "$LOG" | head -40 >&2
  echo "BUILD FAIL variant=$VARIANT (arduino-cli exit $RC) — full log: see above" >&2
  exit 1
fi
if [ "$PROJ_WARN_N" -gt 0 ]; then
  [ $QUIET -eq 1 ] || printf '%s\n' "$PROJ_WARN" >&2
  echo "BUILD FAIL variant=$VARIANT: $PROJ_WARN_N project warning(s) with --warnings all" >&2
  exit 1
fi

echo "BUILD OK variant=$VARIANT flash=$FLASH globals=$GLOBALS warnings=0"
