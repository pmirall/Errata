#!/usr/bin/env python3
"""tools/pbm2svg.py - turn a screen capture into a print-quality SVG.

THE SOURCE IS NOT THE GOLDENS, AND THAT IS THE WHOLE POINT. This script used
to read tests/golden/screens/*.pbm and claimed they were "the ONLY pixel-exact
record of what the device actually draws". That was FALSE, and the file that
says so had said so all along - tests/fakes/gfx_fb.h:

    Text is NOT glyph-accurate and does not pretend to be: each codepoint is
    drawn as a fixed-width cell whose row pattern is a function of the
    codepoint...

The goldens are a layout instrument. They catch a name that overflows its row,
a truncation on the wrong codepoint, a primitive drawn off the panel - and they
do it with a cheap deterministic fake that paints each character as a barcode.
Every screen in the printed manual came out as barcodes.

So the manual reads tests/capture/golden/screens/*.pbm instead, written by
`make -C tests capture`: the same scenes, drawn by tests/fakes/gfx_real.cpp
with the real U8g2 the firmware links, in the panel's own solid font mode, with
the affordance triangles the device actually draws. Two artefacts, two
contracts; neither pretends to be the other.

Output is one <rect> per horizontal run of set pixels inside a 128x64 viewBox,
with shape-rendering="crispEdges". That is vector (so it scales to any print
size), about 13 KB, and keeps the pixel grid exact at 1200 dpi instead of the
soft edges a scaled-up bitmap would give.

Nothing is annotated here on purpose. Callouts are drawn in Typst on top of the
image, so regenerating a screen never destroys them and their labels stay in
the bilingual content file where they can be translated.

Usage:
    tools/pbm2svg.py IN.pbm [-o OUT.svg]      one file, stdout if no -o
    tools/pbm2svg.py --all                    every capture -> docs/manual/assets/screens
    tools/pbm2svg.py --check                  exit 1 if any output is stale
"""

import argparse
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CAPTURE = ROOT / "tests" / "capture" / "golden" / "screens"
OUTDIR = ROOT / "docs" / "manual" / "assets" / "screens"


def read_pbm(path):
    """Return (width, height, rows) from an ASCII P1 file.

    Comments may appear anywhere a token may, so strip them line by line
    before tokenising rather than assuming the single header comment the
    goldens happen to carry today.
    """
    text = path.read_text(encoding="ascii")
    lines = [ln.split("#", 1)[0] for ln in text.splitlines()]
    tokens = " ".join(lines).split()

    if not tokens or tokens[0] != "P1":
        raise ValueError(f"{path}: not an ASCII P1 PBM")
    try:
        width, height = int(tokens[1]), int(tokens[2])
    except (IndexError, ValueError):
        raise ValueError(f"{path}: unreadable dimensions") from None

    # P1 allows bits to run together without whitespace ("0101"), so walk
    # characters rather than tokens.
    bits = []
    for tok in tokens[3:]:
        for ch in tok:
            if ch in "01":
                bits.append(ch == "1")
            else:
                raise ValueError(f"{path}: unexpected character {ch!r} in raster")

    expected = width * height
    if len(bits) != expected:
        raise ValueError(f"{path}: {len(bits)} pixels, expected {expected}")

    return width, height, [bits[y * width:(y + 1) * width] for y in range(height)]


def to_svg(width, height, rows, title):
    """Emit the SVG, merging each row's set pixels into horizontal runs."""
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}"',
        f' width="{width}" height="{height}" shape-rendering="crispEdges">',
        f"<title>{title}</title>",
        # The panel is a lit OLED: white paper behind the pixels would invert
        # the product's own look, so the frame is drawn dark and the pixels
        # light, matching what the player is holding.
        f'<rect width="{width}" height="{height}" fill="#000"/>',
        '<g fill="#fff">',
    ]
    for y, row in enumerate(rows):
        x = 0
        while x < width:
            if not row[x]:
                x += 1
                continue
            run = x
            while run < width and row[run]:
                run += 1
            parts.append(f'<rect x="{x}" y="{y}" width="{run - x}" height="1"/>')
            x = run
    parts.append("</g></svg>\n")
    return "".join(parts)


def convert(path):
    width, height, rows = read_pbm(path)
    return to_svg(width, height, rows, f"Pebblebol screen: {path.stem}")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("source", nargs="?", type=pathlib.Path)
    ap.add_argument("-o", "--out", type=pathlib.Path)
    ap.add_argument("--all", action="store_true", help="convert every capture")
    ap.add_argument("--check", action="store_true",
                    help="exit 1 if any generated SVG is missing or stale")
    args = ap.parse_args()

    if args.all or args.check:
        shots = sorted(CAPTURE.glob("*.pbm"))
        if not shots:
            print(f"pbm2svg: no captures under {CAPTURE}.\n"
                  f"         Run: make -C tests capture", file=sys.stderr)
            return 1
        stale = []
        for src in shots:
            svg = convert(src)
            dst = OUTDIR / (src.stem + ".svg")
            if args.check:
                if not dst.exists() or dst.read_text(encoding="utf-8") != svg:
                    stale.append(dst.relative_to(ROOT))
                continue
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_text(svg, encoding="utf-8")
        if args.check:
            if stale:
                print("pbm2svg: stale or missing, run tools/pbm2svg.py --all:",
                      file=sys.stderr)
                for s in stale:
                    print(f"  {s}", file=sys.stderr)
                return 1
            print(f"pbm2svg: {len(shots)} screens up to date")
            return 0
        print(f"pbm2svg: wrote {len(shots)} screens to "
              f"{OUTDIR.relative_to(ROOT)}")
        return 0

    if not args.source:
        ap.error("give a source file, or --all / --check")
    svg = convert(args.source)
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(svg, encoding="utf-8")
    else:
        sys.stdout.write(svg)
    return 0


if __name__ == "__main__":
    sys.exit(main())
