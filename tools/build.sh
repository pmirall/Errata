#!/usr/bin/env bash
# tools/build.sh — arduino-cli wrapper for the Errata firmware.
#
# Compiles the sketch with --warnings all, fails on any warning that points into
# the sketch (core/library warnings are ignored, exactly like the baseline), and
# prints the parsed "Sketch uses" / "Global variables use" figures.
#
# Usage: tools/build.sh [--variant NAME] [--define MACRO=VALUE]... [--build-path DIR]
#   --define      overrides a `#define MACRO <number>` line in config.h in a temporary
#                 copy of the sketch (used by build_matrix.sh); the repo is untouched.
#   --quiet       print only the summary line.
# Environment: FQBN (default below), SKETCH (default <repo>/Errata).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SKETCH="${SKETCH:-$ROOT/Errata}"
# PARTITIONS (decision D6). Errata/partitions.csv is the table that is
# actually flashed: arduino-cli copies a sketch-local partitions.csv into the
# build directory and esptool writes THAT, whatever the board menu says. What
# the menu still controls is the SIZE CHECK - upload.maximum_size comes from the
# selected PartitionScheme, and dropping the option here (P2-C9d's first attempt)
# left the check at the default scheme's 1,310,720 B and failed a 1.87 MB build
# that fits app0 perfectly well. So PartitionScheme=huge_app stays, and its
# 3,145,728 B ceiling is exactly app0's size in the CSV - which the two gates
# below pin: the app maximum must be that number, and the table that will be
# flashed must contain the private nvs2 partition the checkpoint lives in.
FQBN="${FQBN:-esp32:esp32:esp32c3:PartitionScheme=huge_app,CDCOnBoot=cdc}"
EXPECT_APP_MAX="${EXPECT_APP_MAX:-3145728}"
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
  CFG="$SRC/src/core/config.h"
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
APPMAX="$(grep -oE "Maximum is [0-9]+ bytes" "$LOG" | head -1 | grep -oE "[0-9]+" || echo 0)"
GLOBALS="$(grep -oE "Global variables use [0-9]+ bytes" "$LOG" | grep -oE "[0-9]+" || echo 0)"

if [ $RC -ne 0 ]; then
  [ $QUIET -eq 1 ] || grep -E "error:|Error|undefined reference" "$LOG" | head -40 >&2
  echo "BUILD FAIL variant=$VARIANT (arduino-cli exit $RC) — full log: see above" >&2
  exit 1
fi
# D6 gates. (1) the app ceiling still matches app0 in partitions.csv; (2) the
# table that will be flashed is the sketch's, i.e. it has the nvs2 partition.
if [ "$APPMAX" != "$EXPECT_APP_MAX" ]; then
  echo "BUILD FAIL variant=$VARIANT: app maximum is $APPMAX bytes, expected $EXPECT_APP_MAX" >&2
  echo "  (the app0 size in Errata/partitions.csv and the FQBN's PartitionScheme disagree - see D6)" >&2
  exit 1
fi
if ! grep -qE "^[[:space:]]*nvs2[[:space:]]*," "$BUILD_PATH/partitions.csv" 2>/dev/null; then
  echo "BUILD FAIL variant=$VARIANT: the flashed partition table has no nvs2 partition" >&2
  echo "  (Errata/partitions.csv was not picked up - see D6)" >&2
  exit 1
fi

if [ "$PROJ_WARN_N" -gt 0 ]; then
  [ $QUIET -eq 1 ] || printf '%s\n' "$PROJ_WARN" >&2
  echo "BUILD FAIL variant=$VARIANT: $PROJ_WARN_N project warning(s) with --warnings all" >&2
  exit 1
fi

echo "BUILD OK variant=$VARIANT flash=$FLASH globals=$GLOBALS warnings=0"
