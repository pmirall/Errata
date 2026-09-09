#!/usr/bin/env python3
"""tools/page_fill.py - find orphan pages in the rendered manual.

A section that overruns its page by two lines leaves the next page 99% white.
Nothing else can see it: the source looks fine, the page count looks fine, and
the booklet looks broken. This measures ink per rendered page and fails on any
page that is nearly empty and is not one the design expects to be.

Called by tools/build_manual.sh --png. Reads the PNGs it already renders, with
no dependency beyond the standard library.

Expected-sparse pages, and why each is allowed:
  - the cover, which is a wordmark on white;
  - a page whose only element is a P10-M5 drawing placeholder (a dashed box);
  - the blank leaves the saddle-stitch pad adds before the back cover.
"""
import glob, re, struct, zlib, pathlib, sys
def png_ink(path):
    d = pathlib.Path(path).read_bytes()
    pos, w, h, bd, ct = 8, 0, 0, 0, 0; idat = b""
    while pos < len(d):
        ln = struct.unpack(">I", d[pos:pos+4])[0]; typ = d[pos+4:pos+8]
        body = d[pos+8:pos+8+ln]
        if typ == b"IHDR": w, h, bd, ct = *struct.unpack(">II", body[:8]), body[8], body[9]
        elif typ == b"IDAT": idat += body
        pos += 12 + ln
    raw = zlib.decompress(idat); ch = {0:1,2:3,4:2,6:4}[ct]; bpp = ch*(bd//8)
    stride = w*bpp; prev = bytearray(stride); ink = 0; i = 0
    for _ in range(h):
        f = raw[i]; i += 1
        line = bytearray(raw[i:i+stride]); i += stride
        for x in range(stride):
            a = line[x-bpp] if x >= bpp else 0
            b = prev[x]; c = prev[x-bpp] if x >= bpp else 0
            if   f==1: line[x]=(line[x]+a)&255
            elif f==2: line[x]=(line[x]+b)&255
            elif f==3: line[x]=(line[x]+(a+b)//2)&255
            elif f==4:
                pp=a+b-c; pa,pb,pc=abs(pp-a),abs(pp-b),abs(pp-c)
                pr=a if(pa<=pb and pa<=pc) else (b if pb<=pc else c)
                line[x]=(line[x]+pr)&255
        for x in range(0, stride, bpp):
            if line[x] < 200: ink += 1
        prev = line
    return ink, w*h
res=[]
import os
out = sys.argv[1] if len(sys.argv) > 1 else "docs/manual/out"
for f in sorted(glob.glob(os.path.join(out, "p*.png"))):
    ink, tot = png_ink(f); res.append((int(re.search(r"p(\d+)",f).group(1)), 100.0*ink/tot))
avg = sum(p for _, p in res) / len(res)
last = max(n for n, _ in res)
# the cover, the drawing-placeholder page, and the pad leaves before the back
ALLOWED = {1, 2} | {n for n in range(last - 3, last + 1)}
orphans = [(n, p) for n, p in res if p < 3.0 and n not in ALLOWED]
print(f"page fill: {len(res)} pages, mean ink {avg:.1f}%")
if orphans:
    print("ORPHAN PAGES - a section before each of these overruns its page:",
          file=sys.stderr)
    for n, p in orphans:
        print(f"  p{n:02d}  {p:.1f}% ink", file=sys.stderr)
    sys.exit(1)
print("page fill: no orphan pages")
