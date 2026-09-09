#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_index_html.py - THE CREATOR PAGE PIPELINE (plan P8-C4, spec sections 33-38).

    web/creator/index.html        ->   Errata/src/data/index_html.h
    web/creator/app.js
    web/creator/sprite_editor.js

Usage
    python3 tools/gen_index_html.py            # write the header
    python3 tools/gen_index_html.py --check    # regenerate in memory and DIFF
                                               # against the tree; exit 1 on drift

WHY THIS EXISTS AT ALL. `src/data/index_html.h` is a blob nobody can review: a
40 KB C string literal is not a diff a human reads. Committing the SOURCE and
generating the header is what makes the page reviewable, and `--check` in
tools/check.sh is what stops the two from parting company - a hand edit to the
header, or a page edit that was never regenerated, fails the gate exactly the
way a hand-edited species_table.h does today (tools/gen_content.py --check).

WHAT --check PROVES, AND THE LIMIT, STATED HERE RATHER THAN ASSUMED. It proves
the committed header is byte-for-byte what the committed page source produces.
It proves NOTHING about whether the page works: it cannot open a browser, it
cannot run the sprite editor, and it cannot see that app.js reads a
/api/schema field the device does not emit. The size half is the static_assert
this file emits; the ROUTE half is a grep gate in tools/check.sh; the BEHAVIOUR
half is tools/page_test.mjs driving a real headless browser, and where that is
not run it is not tested at all.

------------------------------------------------------------------------------
DETERMINISM IS A CONTRACT, AND IT IS WHY THERE IS NO MINIFIER
------------------------------------------------------------------------------
Two runs over the same sources produce a byte-identical header on any machine
with a Python 3. Nothing here reads the clock, the environment or a random
source, and - the part that matters - nothing here depends on an npm package.
A generator whose output depends on a minifier's version is a generator whose
--check fails on somebody else's machine, which turns a gate into a nuisance
and then into a `--no-verify`.

So the transform is deliberately dumb and deliberately safe:

  * whole-line comments go (a line whose first non-space characters are `//`,
    a `/* ... */` that starts a line, and `<!-- ... -->` in the HTML);
  * leading and trailing whitespace on every line goes;
  * blank lines go;
  * NOTHING ELSE. No identifier renaming, no whitespace collapsing INSIDE a
    line, no newline removal, no dead-code elimination.

Newlines are kept, which costs about 1.5 KB and buys three things: the served
page is greppable with curl, a JS error's line number means something, and
joining lines is the one edit that can change what JavaScript means (automatic
semicolon insertion). The budget below has room for all three.

------------------------------------------------------------------------------
THREE GUARDS ON THE SOURCE, EACH FOR A FAILURE THAT WOULD OTHERWISE BE SILENT
------------------------------------------------------------------------------
1. PRINTABLE ASCII ONLY. The bytes below travel through a C++ raw string
   literal in a generated header, compiled by a toolchain whose source charset
   nobody here controls, and are served as text/html; charset=utf-8. `&aacute;`
   and `á` cannot become mojibake on the phone; a raw 0xE1 can, silently.
   ASCII also makes the byte count and the character count the same number,
   which is what makes the WEB_HTML_MAX margin below unambiguous.

   NOTE THE RULE THIS IS *NOT*. core/strings_es.h's Latin-1 rule is about what
   the DEVICE's fonts can draw. It does not apply to a phone browser and this
   guard is not a copy of it: this one is about transport.

2. NO MULTI-LINE TEMPLATE LITERAL AND NO <pre>. Stripping leading indentation
   is safe everywhere except inside text whose whitespace is significant, so
   the two constructs where it is not are refused by name. The backtick check
   is per line and counts parity, which cannot be fooled by a backtick inside a
   quoted string on the same line only if that string is balanced - so the
   check is stated as what it is: a tripwire for the multi-line case, not a JS
   lexer.

3. THE API-VERSION PLACEHOLDER MUST BE PRESENT EXACTLY ONCE. app.js carries
   `0 /*@CREATOR_API_VERSION@*/` and this script substitutes core/version.h's
   value. That is what lets a page cached from an older device SAY so instead
   of sending a document the newer one refuses by number, and requiring it
   means the tie cannot be deleted by accident - only deliberately, here.

All identifiers and comments English.
"""

import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "web", "creator")
DATA = os.path.join(ROOT, "Errata", "src", "data")
CONFIG_H = os.path.join(ROOT, "Errata", "src", "core", "config.h")
VERSION_H = os.path.join(ROOT, "Errata", "src", "core", "version.h")
OUT = os.path.join(DATA, "index_html.h")

# The raw-string delimiter the emitted header uses. Chosen so that the
# sequence cannot plausibly occur in HTML or JS, and checked anyway: a page
# containing it would silently end the literal and produce a header that does
# not compile, or worse, one that does.
DELIM = "PBHTML"

API_TOKEN = "0 /*@CREATOR_API_VERSION@*/"


def die(msg):
    sys.stderr.write("gen_index_html.py: %s\n" % msg)
    sys.exit(1)


def read(path):
    if not os.path.exists(path):
        die("missing page source %s" % os.path.relpath(path, ROOT))
    with open(path, encoding="utf-8") as f:
        return f.read()


def define_of(text, name, path):
    """One `#define NAME <integer>` out of a header, or a named death.

    Read rather than typed, for the reason every number in this project is:
    a copy is a second source of truth and this one would be a size cap that
    silently stops matching the one the firmware compiles.
    """
    m = re.search(r"^#define\s+%s\s+(\d+)" % re.escape(name), text, re.M)
    if not m:
        die("%s does not define %s" % (os.path.relpath(path, ROOT), name))
    return int(m.group(1))


# =============================================================================
#  THE TRANSFORM
# =============================================================================

def strip_js(text, what):
    """Whole-line comments and indentation out of a JavaScript source."""
    out = []
    in_block = False
    for n, line in enumerate(text.split("\n"), 1):
        if line.count("`") % 2 == 1:
            die("%s:%d has an odd number of backticks - a template literal that "
                "spans lines would lose its indentation to this script, and "
                "indentation inside one is significant" % (what, n))
        s = line.strip()
        if in_block:
            # A block comment that STARTED at the beginning of a line. Its end
            # may carry code after it, which would be lost - so that is refused
            # rather than dropped.
            if "*/" in s:
                in_block = False
                tail = s.split("*/", 1)[1].strip()
                if tail:
                    die("%s:%d has code after the end of a block comment; this "
                        "script drops comment lines whole" % (what, n))
            continue
        if s.startswith("//"):
            continue
        if s.startswith("/*"):
            if "*/" in s:
                tail = s.split("*/", 1)[1].strip()
                if tail:
                    die("%s:%d has code after a block comment on the same line"
                        % (what, n))
                continue
            in_block = True
            continue
        if not s:
            continue
        out.append(s)
    if in_block:
        die("%s: a block comment is never closed" % what)
    return "\n".join(out)


HTML_COMMENT = re.compile(r"<!--.*?-->", re.S)


def strip_html(text, what):
    """HTML comments and indentation out of the shell.

    Run BEFORE the scripts are inlined, so `-->` inside JavaScript can never be
    mistaken for the end of a comment. `<pre>` is refused because collapsing
    indentation inside one changes what the page renders.
    """
    # Comments FIRST, so the <pre> tripwire below cannot fire on this file's
    # own prose about it - and so `-->` can never be found inside JavaScript,
    # which is why the scripts are inlined after this runs.
    text = HTML_COMMENT.sub("", text)
    if "<pre" in text.lower():
        die("%s contains a <pre> element; this script strips leading "
            "indentation, which is significant inside one" % what)
    # Where the stylesheet is, so a CSS comment can be dropped without a rule
    # that could eat a line of page TEXT beginning with /*. Narrow on purpose:
    # inside <style> a line that starts with /* and ends with */ is a comment
    # and cannot be anything else.
    lo = text.find("<style>")
    hi = text.find("</style>")
    out = []
    at = 0
    for line in text.split("\n"):
        here = at
        at += len(line) + 1
        s = line.strip()
        if not s:
            continue
        if lo >= 0 and hi > lo and lo < here < hi \
           and s.startswith("/*") and s.endswith("*/"):
            continue
        out.append(s)
    return "\n".join(out)


def build_page():
    html = read(os.path.join(SRC, "index.html"))
    app = read(os.path.join(SRC, "app.js"))
    spr = read(os.path.join(SRC, "sprite_editor.js"))

    api = define_of(read(VERSION_H), "CREATOR_API_VERSION", VERSION_H)

    n = app.count(API_TOKEN)
    if n != 1:
        die("web/creator/app.js carries the CREATOR_API_VERSION placeholder %d "
            "times, not once - it is what lets a page cached from an older "
            "device say so instead of being refused by number" % n)
    app = app.replace(API_TOKEN, str(api))

    html = strip_html(html, "web/creator/index.html")
    spr = strip_js(spr, "web/creator/sprite_editor.js")
    app = strip_js(app, "web/creator/app.js")

    # Inlined into the two empty <script> tags the shell ends with. One
    # document, one route: spec section 38 says "serve only required assets",
    # and a page that fetches a stylesheet is a page with a second route.
    for tag, body in (('<script id="js-sprite"></script>', spr),
                      ('<script id="js-app"></script>', app)):
        if html.count(tag) != 1:
            die("index.html does not carry %s exactly once" % tag)
        html = html.replace(tag, "<script>\n%s\n</script>" % body)

    for i, ch in enumerate(html):
        o = ord(ch)
        if ch == "\n":
            continue
        if o < 0x20 or o > 0x7E:
            line = html.count("\n", 0, i) + 1
            die("the page is not printable ASCII: U+%04X on emitted line %d. "
                "Spanish accents are HTML entities in the markup and \\uXXXX "
                "escapes in the JavaScript; see the banner in this file."
                % (o, line))
    if ")%s" % DELIM in html:
        die("the page contains the raw-string delimiter )%s" % DELIM)
    return html, api


# =============================================================================
#  THE HEADER
# =============================================================================

BANNER = """// =============================================================================
//  ERRATA - data/index_html.h
//
//  GENERATED FILE. Do not edit: tools/gen_index_html.py rewrites it from
//  web/creator/*. `tools/gen_index_html.py --check` fails the gate if this
//  file and the page source have drifted apart.
//
//  THE CREATOR PAGE, AS THE BYTES GET / SERVES (spec sections 33 and 37).
//
//  ONE DOCUMENT AND ONE ROUTE. index.html, app.js and sprite_editor.js are
//  inlined into a single blob, so the phone makes exactly one request for the
//  page and the device serves exactly one asset - spec section 38's "serve
//  only required assets", held by there being nothing else to serve.
//
//  INCLUDE THIS FROM webui.cpp ONLY. INDEX_HTML has internal linkage (const at
//  namespace scope), so every translation unit that *uses* it keeps its own
//  copy in .rodata; -Wl,--gc-sections only drops the copies nobody references.
//
//  Serve it with the FOUR-argument send_P. The 3-arg form strlen_P()s the blob
//  (WebServer.cpp:619) and the 1-arg sendContent_P has the same bug:
//    server.sendHeader(F("Cache-Control"), F("no-cache"));
//    server.send_P(200, PSTR("text/html; charset=utf-8"), INDEX_HTML, INDEX_HTML_LEN);
//
//  IT IS .rodata AND COSTS ZERO GLOBALS. The page is FLASH; the row to watch
//  in phase 8 is globals, and this file is not on it.
//
//  THE PAGE IS PRINTABLE ASCII, which is why a raw string literal is safe here
//  and why sizeof() - 1 is the byte count, the character count and the
//  Content-Length all at once. The generator refuses anything else.
//
//  WHAT THE PAGE ENFORCES IS A COURTESY AND NEVER A CONTROL: every rule it
//  mirrors is enforced again on this device by game/validate.cpp, over the
//  bytes that actually arrived. See the banner of web/creator/app.js.
// ============================================================================="""


def emit(page, api, cap):
    o = [BANNER, ""]
    o.append("#ifndef NT_INDEX_HTML_H")
    o.append("#define NT_INDEX_HTML_H")
    o.append("")
    o.append("#include <Arduino.h>")
    o.append('#include "../core/config.h"')
    o.append('#include "../core/version.h"')
    o.append("")
    o.append("// The creator API version this page was generated against, copied out of")
    o.append("// core/version.h by the generator and substituted into app.js. The assert")
    o.append("// below is what makes it a TIE rather than a note: bump CREATOR_API_VERSION")
    o.append("// without regenerating the page and the firmware stops compiling, instead of")
    o.append("// shipping a page that tells the phone the wrong number.")
    o.append("#define INDEX_HTML_API_VERSION  %d" % api)
    o.append("")
    o.append('static const char INDEX_HTML[] PROGMEM = R"%s(' % DELIM)
    o.append(page)
    o.append(')%s";' % DELIM)
    o.append("")
    o.append("static const size_t INDEX_HTML_LEN = sizeof(INDEX_HTML) - 1;")
    o.append("")
    o.append("static_assert(INDEX_HTML_API_VERSION == CREATOR_API_VERSION,")
    o.append('              "the committed creator page was generated against a different "')
    o.append('              "CREATOR_API_VERSION than this firmware speaks: run "')
    o.append('              "python3 tools/gen_index_html.py");')
    o.append("static_assert(sizeof(INDEX_HTML) - 1 <= WEB_HTML_MAX,")
    o.append('              "index_html.h blob exceeds WEB_HTML_MAX (48 KB, amendment A2)");')
    o.append("")
    o.append("#endif // NT_INDEX_HTML_H")
    return "\n".join(o) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true",
                    help="regenerate in memory and diff against the tree")
    args = ap.parse_args()

    cap = define_of(read(CONFIG_H), "WEB_HTML_MAX", CONFIG_H)
    page, api = build_page()

    # The blob is what a phone downloads and what the static_assert measures.
    # Reported either way, because a cap nobody prints the margin of is a cap
    # somebody finds out about from a compiler error at 48 KB + 3 B.
    size = len(page.encode("ascii"))
    if size > cap:
        die("the page is %d B and WEB_HTML_MAX is %d B (over by %d). Do NOT "
            "raise the cap as the first move: the cap is the only thing that "
            "makes the overrun visible. The documented lever is gzip - send_P "
            "serves a pre-compressed blob with a Content-Encoding header (the "
            "core's own serveStatic does exactly that), and this project's "
            "previous phone page compressed 47,181 B to 17,947 B."
            % (size, cap, size - cap))

    text = emit(page, api, cap)
    old = None
    if os.path.exists(OUT):
        with open(OUT, encoding="utf-8") as f:
            old = f.read()

    rel = os.path.relpath(OUT, ROOT)
    if args.check:
        if old != text:
            sys.stderr.write("gen_index_html.py --check: %s differs from "
                             "web/creator/\n" % rel)
            sys.stderr.write("run: python3 tools/gen_index_html.py\n")
            return 1
        print("gen_index_html.py --check: in sync, page %d B of WEB_HTML_MAX %d "
              "(%d B free, %.1f %% used)"
              % (size, cap, cap - size, 100.0 * size / cap))
        return 0

    if old != text:
        with open(OUT, "w", encoding="utf-8") as f:
            f.write(text)
        print("gen_index_html.py: wrote %s" % rel)
    else:
        print("gen_index_html.py: (no change)")
    print("gen_index_html.py: page %d B, WEB_HTML_MAX %d, %d B free (%.1f %% used), "
          "CREATOR_API_VERSION %d"
          % (size, cap, cap - size, 100.0 * size / cap, api))
    return 0


if __name__ == "__main__":
    sys.exit(main())
