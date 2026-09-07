#!/usr/bin/env bash
# tools/build_manual.sh - build the printed manual (docs/manual/manual.typ).
#
#   --draft   (default)  holes rendered as visible TODO blocks, for reading
#   --print              refuses to build while any legal fact is missing
#   --png                also render one PNG per page, for proofing on screen
#
# THREE THINGS THIS SCRIPT EXISTS TO GUARANTEE:
#
#   1. The screen illustrations match the firmware. They are regenerated from
#      tests/golden/screens/*.pbm on every build, so a screen that changed in
#      the code cannot silently go stale in the manual.
#
#   2. A print-ready PDF can never be missing a legally mandated field. Every
#      identifying fact lives in docs/manual/product_facts.toml; any value left
#      as "TODO" fails --print here AND panics inside Typst, so neither path
#      produces a shippable file with a hole in it.
#
#   3. The page count is a multiple of four. The booklet is saddle-stitched
#      folded sheets; anything else cannot be bound. Typst only knows the total
#      after laying the document out, so this compiles twice: once to count,
#      then again with the blank leaves needed before the back cover.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MANUAL="$ROOT/docs/manual"
OUT="$MANUAL/out"
MODE=draft
WANT_PNG=0

for a in "$@"; do
  case "$a" in
    --draft) MODE=draft ;;
    --print) MODE=print ;;
    --png)   WANT_PNG=1 ;;
    *) echo "build_manual.sh: unknown argument $a" >&2; exit 2 ;;
  esac
done

fail() { echo "MANUAL FAIL: $*" >&2; exit 1; }

command -v typst >/dev/null 2>&1 || fail "typst not found. See docs/manual/README.md"
command -v python3 >/dev/null 2>&1 || fail "python3 not found"

mkdir -p "$OUT"

# --- 1. screens ---------------------------------------------------------------
python3 "$ROOT/tools/pbm2svg.py" --all >/dev/null || fail "pbm2svg"

# --- 2. the TODO gate ---------------------------------------------------------
# Typst panics on the first hole it meets. That is the right behaviour for a
# build but a poor experience for a person, who wants the whole list at once,
# so the list is produced here and Typst stays the backstop.
if [ "$MODE" = print ]; then
  missing="$(python3 - "$MANUAL/product_facts.toml" <<'PY'
import sys, tomllib, pathlib
facts = tomllib.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
holes = [f"{s}.{k}" for s, sec in facts.items()
         if isinstance(sec, dict)
         for k, v in sec.items() if v == "TODO"]
print("\n".join(holes))
PY
)"
  if [ -n "$missing" ]; then
    echo "MANUAL FAIL: product_facts.toml still has unfilled legal facts:" >&2
    echo "$missing" | sed 's/^/  /' >&2
    echo >&2
    echo "A draft with visible holes is useful. A print-ready PDF missing a" >&2
    echo "legally mandated field is a recall. Fill these in, or build --draft." >&2
    exit 1
  fi
fi

DRAFT_FLAG=1
NAME=manual-draft
[ "$MODE" = print ] && { DRAFT_FLAG=0; NAME=manual-print; }

# --- 3. two-pass build for the multiple-of-four page count ---------------------
count_pages() {
  python3 - "$1" <<'PY'
import re, sys, pathlib
d = pathlib.Path(sys.argv[1]).read_bytes()
print(len(re.findall(rb"/Type\s*/Page\b(?!s)", d)))
PY
}

compile_with() { # $1 = pad
  typst compile --root "$MANUAL" \
    --input "draft=$DRAFT_FLAG" --input "pad=$1" \
    "$MANUAL/manual.typ" "$OUT/$NAME.pdf" || fail "typst compile (pad=$1)"
}

compile_with 0
n="$(count_pages "$OUT/$NAME.pdf")"
pad=$(( (4 - (n % 4)) % 4 ))
if [ "$pad" -ne 0 ]; then
  compile_with "$pad"
  n="$(count_pages "$OUT/$NAME.pdf")"
fi

[ $(( n % 4 )) -eq 0 ] || fail "page count $n is not a multiple of 4; cannot saddle-stitch"
[ "$n" -ge 16 ] && [ "$n" -le 32 ] || fail "page count $n outside the agreed 16-32 range"

# --- 4. the print PDF must carry a TrimBox ------------------------------------
# Bleed with no TrimBox is bleed the printer cannot trim to. Typst writes one
# whenever page bleed is non-zero; check it rather than trust it.
python3 - "$OUT/$NAME.pdf" <<'PY' || exit 1
import re, sys, pathlib
d = pathlib.Path(sys.argv[1]).read_bytes()
if not re.search(rb"TrimBox", d):
    sys.exit("MANUAL FAIL: no TrimBox in the PDF; the printer cannot trim it")
PY

if [ "$WANT_PNG" -eq 1 ]; then
  rm -f "$OUT"/p*.png
  typst compile --root "$MANUAL" --format png --ppi 150 \
    --input "draft=$DRAFT_FLAG" --input "pad=$pad" \
    "$MANUAL/manual.typ" "$OUT/p{0p}.png" || fail "png render"
fi

echo "manual: $NAME.pdf, $n pages (pad $pad), A6 105x148 mm + 3 mm bleed"
echo "        $OUT/$NAME.pdf"
