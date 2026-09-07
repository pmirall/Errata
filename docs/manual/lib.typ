// =============================================================================
//  docs/manual/lib.typ - everything the manual pages are built out of.
//
//  This lives apart from manual.typ because Typst's `include` does not carry
//  `let` bindings across files: content/guide.typ and content/legal.typ have
//  to `import` these, so they cannot be defined in the document that includes
//  them.
//
//  `set` and `show` rules DO reach included content, so the page geometry and
//  the type stay in manual.typ where the document is assembled.
// =============================================================================

#let facts = toml("product_facts.toml")

// `draft=0` is passed by build_manual.sh --print. In that mode an unfilled
// fact stops the build: a draft PDF with visible holes is useful, a
// print-ready PDF missing a legally mandated field is a recall.
#let is-draft = sys.inputs.at("draft", default: "1") != "0"

// A saddle-stitched booklet is folded sheets, so its page count MUST be a
// multiple of four. Typst cannot know the total until it has laid the document
// out, so tools/build_manual.sh compiles twice: once to count, then again with
// `pad` set to the blank leaves needed before the back cover. Padding at the
// back is the only place a blank leaf is invisible to a reader.
#let pad = int(sys.inputs.at("pad", default: "0"))

// ---------------------------------------------------------------------------
//  Facts
// ---------------------------------------------------------------------------

#let fact(path) = {
  let parts = path.split(".")
  let v = facts
  for p in parts {
    if type(v) != dictionary or p not in v {
      panic("product_facts.toml has no key " + path)
    }
    v = v.at(p)
  }
  if v == "TODO" {
    if not is-draft {
      panic("product_facts.toml: " + path + " is still TODO; cannot build for print")
    }
    box(fill: black, inset: (x: 2pt, y: 1pt), radius: 1pt,
        text(fill: white, weight: "bold", size: 6pt, "TODO: " + path))
  } else {
    str(v)
  }
}

// The postal address as GPSR art. 19 wants it: one legible block.
#let address-block = [
  #fact("entity.name") #fact("entity.legal_form") \
  #fact("entity.street") \
  #fact("entity.postcode") #fact("entity.city") -- #fact("entity.country") \
  NIF #fact("entity.tax_id")
]

#let heading-rule = line(length: 100%, stroke: 0.6pt + black)

#show heading.where(level: 1): it => block(below: 3.5mm, above: 0mm)[
  #set text(size: 11pt, weight: "bold")
  #it.body
  #v(-1mm)
  #heading-rule
]
#show heading.where(level: 2): it => block(below: 1.6mm, above: 2.6mm)[
  #set text(size: 8.5pt, weight: "bold")
  #it.body
]

// On-screen text is quoted in mono so the player can tell "what the manual
// says" from "what the device says". src/core/strings_es.h is Latin-1 only:
// no em dash, no ellipsis glyph, no curly quotes. Quoting it any other way
// would print something the panel cannot render.
#let scr(t) = box(fill: luma(90%), inset: (x: 1.5pt, y: 0.5pt), radius: 1pt,
                  text(font: "Liberation Mono", size: 7pt, t))

// ---------------------------------------------------------------------------
//  Bilingual layout
//
//  Two strategies, on purpose:
//
//  `bi`  - guide pages. A shared illustration carries the meaning and two
//          narrow columns carry the words. Running Spanish and English as a
//          flip-book instead would need every illustration twice and push the
//          booklet past 40 pages; illustrations are the expensive part.
//
//  `seq` - legal pages. Continuous legal prose reads badly in a 40 mm column,
//          and it is read rarely, so Spanish runs full width and English
//          follows it.
// ---------------------------------------------------------------------------

#let bi(es, en) = grid(
  columns: (1fr, 1fr), column-gutter: 4mm,
  [#set text(lang: "es"); #es],
  [#set text(lang: "en"); #set par(leading: 3.2pt); #text(fill: luma(25%), en)],
)

#let seq(es, en) = {
  set text(size: 6.5pt)
  set par(leading: 2.6pt)
  [#set text(lang: "es"); #es]
  v(1.6mm)
  line(length: 30%, stroke: 0.4pt + luma(60%))
  v(1.2mm)
  [#set text(lang: "en"); #text(fill: luma(25%), en)]
}

// ---------------------------------------------------------------------------
//  Screens and callouts
//
//  Callouts are numbered, never worded, inside the picture: one drawing serves
//  both languages and the legend under it is translated like any other text.
//  Coordinates are fractions of the image box, so they survive a resize.
// ---------------------------------------------------------------------------

#let dot(n) = box(baseline: 1.3mm, circle(
  radius: 2.1mm, fill: white, stroke: 0.5pt + black,
  align(center + horizon, text(size: 6pt, weight: "bold", str(n))),
))

#let screen(name, width: 46mm, marks: ()) = {
  // The panel is 128x64, so the drawn height follows from the width. Both the
  // image and the callout dots are PLACED inside a box of exactly that size:
  // letting the image flow and then placing over it puts the dots below it,
  // because `place` measures from the box origin and a flowed image has
  // already advanced the cursor.
  let h = width / 2
  box(width: width, height: h, {
    place(top + left, image("assets/screens/" + name + ".svg", width: width))
    place(top + left, rect(width: width, height: h, stroke: 0.5pt + black))
    for m in marks {
      // Centre the dot on the feature rather than hanging it off the corner.
      place(top + left, dx: m.at(0) * width - 2.1mm, dy: m.at(1) * h - 2.1mm,
            dot(m.at(2)))
    }
  })
}

// A legend line: the number, then the two languages.
#let mark(n, es, en) = grid(
  columns: (5.5mm, 1fr, 1fr), column-gutter: 2mm, row-gutter: 1.2mm,
  dot(n), [#set text(size: 7pt); #es],
  [#set text(size: 7pt, fill: luma(25%)); #en],
)

// Cross-references. Page numbers in this booklet MUST be computed: the legal
// section grew by three pages during drafting and every hard-coded "see page
// 19" silently became wrong. Label a section, point at the label.
#let pg(lbl) = context str(counter(page).at(lbl).first())

// breakable: false because a warning split across a page break reads as two
// half-warnings, and the page-count pad absorbs the leftover space anyway.
#let warnbox(body) = block(
  width: 100%, inset: 2.2mm, stroke: 1pt + black, radius: 0.6mm,
  above: 2mm, below: 2mm, breakable: false, body,
)

// Placeholder for the three drawings that cannot come from a screenshot.
// P10-M5 replaces each with a real SVG; until the enclosure is finished there
// is nothing accurate to draw, and a wrong diagram is worse than a marked gap.
#let drawing(slug, caption, height: 34mm) = block(
  width: 100%, height: height, inset: 2mm,
  stroke: (paint: luma(55%), thickness: 0.5pt, dash: "dashed"),
  above: 1.5mm, below: 1.5mm,
  align(center + horizon)[
    #set text(size: 6.5pt, fill: luma(40%))
    *P10-M5* -- assets/drawings/#(slug + ".svg") \
    #caption
  ],
)

