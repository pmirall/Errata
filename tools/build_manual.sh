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

# THE BUILD IS BYTE-REPRODUCIBLE, and that is load-bearing rather than tidy:
# docs/uso.pdf is COMMITTED so that reading the manual needs no
# toolchain, and a committed artefact that cannot be compared to its source is
# just a file that goes quietly stale. Typst stamps /CreationDate from the wall
# clock unless SOURCE_DATE_EPOCH says otherwise, which alone made two builds of
# identical input differ.
export SOURCE_DATE_EPOCH=0

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MANUAL="$ROOT/docs/manual"
OUT="$MANUAL/out"
MODE=draft
WANT_PNG=0
VERIFY=0

for a in "$@"; do
  case "$a" in
    --draft) MODE=draft ;;
    --print) MODE=print ;;
    --png)   WANT_PNG=1 ;;
    --verify) VERIFY=1 ;;
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
# The range is a saddle-stitch sanity check, not a budget: 4 pages could not
# hold the legal block and past ~64 the spine stops folding flat. The BUDGET is
# docs/manual/README.md, which prices the extent per unit.
[ "$n" -ge 16 ] && [ "$n" -le 64 ] || fail "page count $n outside the bindable 16-64 range"

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

# --- 5. orphan pages ----------------------------------------------------------
# A section that overruns its page by two lines leaves the next page 99% white.
# It is invisible in the source, invisible in the page count, and obvious the
# moment anyone holds the booklet. Only measurable once rendered, so it lives
# here rather than in the gate.
if [ "$WANT_PNG" -eq 1 ]; then
  python3 "$ROOT/tools/page_fill.py" "$OUT" || fail "orphan page(s); rebalance the section before them"
fi

# --- 6. the committed copy ----------------------------------------------------
# docs/uso.pdf is in git. --verify rebuilds and compares instead of writing,
# which is what tools/check.sh calls: the committed PDF may not drift from the
# sources next to it.
#
# THE NAME AND THE PLACE ARE BOTH DICTATED BY A QR CODE, which is worth saying
# because "uso.pdf at the docs root" looks arbitrary next to manual.typ. The
# MANUAL screen on the device (ui/screen_manual.cpp) paints one symbol, and
# ui/qr.cpp's version-2 byte budget is 32. GitHub Pages serves this repository's
# docs/ at pmirall.github.io/Errata, which is 24 of those 32 - so the whole
# path, slash and extension included, has EIGHT bytes to live in.
# "/manual/manual-draft.pdf" is 24 and does not fit at any scale; "/uso.pdf" is
# 8 and the URL lands on 32 exactly. core/config.h static_asserts it.
PUBLISHED="$ROOT/docs/uso.pdf"
if [ "$MODE" = draft ]; then
  if [ "$VERIFY" -eq 1 ]; then
    if ! cmp -s "$OUT/$NAME.pdf" "$PUBLISHED"; then
      echo "MANUAL FAIL: docs/uso.pdf is stale." >&2
      echo "             Rebuild it with tools/build_manual.sh --draft" >&2
      exit 1
    fi
    echo "manual: committed PDF matches the sources"
    exit 0
  fi
  cp "$OUT/$NAME.pdf" "$PUBLISHED"
fi

echo "manual: $NAME.pdf, $n pages (pad $pad), A6 105x148 mm + 3 mm bleed"
echo "        $OUT/$NAME.pdf"
[ "$MODE" = draft ] && echo "        published to docs/uso.pdf"
