// =============================================================================
//  docs/manual/manual.typ - the printed manual that goes in the box.
//
//  A6 (105 x 148 mm), 24 pages, saddle-stitched, ONE ink (black).
//  Bilingual: Spanish and English in the same booklet, never as two booklets.
//
//  BUILD:  tools/build_manual.sh --draft     holes visible, for reading
//          tools/build_manual.sh --print     refuses to build with any hole
//
//  Two rules this file exists to enforce:
//
//  1. NO LEGAL FACT IS WRITTEN HERE. Everything identifying - company, address,
//     registration numbers, the DoC URL, transmit power - comes from
//     product_facts.toml through `fact()`. In print mode a missing value is a
//     hard failure, not a blank line.
//
//  2. NO SCREENSHOT IS DRAWN BY HAND. Every device screen is generated from
//     tests/golden/screens/*.pbm by tools/pbm2svg.py, so a screen that changes
//     in the firmware changes here too and the gate catches a stale one.
//     Callouts are placed OVER the image from here, never baked into the SVG.
// =============================================================================

#import "lib.typ": *

// ---------------------------------------------------------------------------
//  Page and type
// ---------------------------------------------------------------------------

#set document(title: "Pebblebol - Manual de usuario / User manual")
#set page(
  width: 105mm, height: 148mm,
  bleed: 3mm,                 // writes a TrimBox; the printer needs both
  // A6 is small, so the margins earn their keep or go. 5 mm is the safety
  // distance from the trim; everything beyond that was habit. The spine keeps
  // 11 mm because the fold eats into it and `binding` adds no room of its own.
  margin: (inside: 11mm, outside: 8mm, top: 7.5mm, bottom: 8.5mm),
  binding: left,              // +3mm at the spine for the saddle stitch
  header: context { if sys.inputs.at("debug", default: "0") == "1" {
    set text(size: 5pt, fill: red)
    let hs = query(selector(heading).before(here()))
    if hs.len() > 0 { align(right, hs.last().body) }
  } },
  footer: context {
    let n = counter(page).get().first()
    let total = counter(page).final().first()
    // no folio on the cover or the back cover
    if n > 1 and n < total {
      set text(size: 6pt, fill: luma(40%))
      align(center, str(n))
    }
  },
)
#set text(font: "Liberation Sans", size: 8pt, lang: "es", hyphenate: true)
#set par(leading: 3.2pt, justify: false)

// ---------------------------------------------------------------------------
//  1. Cover
// ---------------------------------------------------------------------------

#page(margin: 9mm, {
  align(center + horizon, block[
    #text(size: 22pt, weight: "bold", tracking: 1pt, "PEBBLEBOL")
    #v(2mm)
    #line(length: 40mm, stroke: 1pt + black)
    #v(4mm)
    #text(size: 9pt)[Manual de usuario]
    #v(0.5mm)
    #text(size: 9pt, fill: luma(25%))[User manual]
    #v(10mm)
    #box(stroke: 1.2pt + black, inset: 2mm, radius: 1mm,
         text(size: 10pt, weight: "bold", "14+"))
    #v(2mm)
    #text(size: 6.5pt)[No es un juguete / Not a toy]
  ])
  place(bottom + center, text(size: 6pt, fill: luma(45%))[
    #fact("product.name") #fact("product.model") -- v#fact("manual.version")
  ])
})

#include "content/guide.typ"
#include "content/legal.typ"

// ---------------------------------------------------------------------------
//  Back cover, always the last leaf
// ---------------------------------------------------------------------------

#for _ in range(pad) { pagebreak() }
#pagebreak()
#[
  = Software libre / Open source

  #set text(size: 6pt)
  #set par(leading: 2.4pt)

  Pebblebol usa software de terceros. Sus avisos de copyright se reproducen
  aqui como exigen sus licencias. / Pebblebol uses third-party software. Their
  copyright notices are reproduced here as their licences require.

  #v(1.5mm)
  *U8g2* -- Copyright (c) 2016, olikraus\@gmail.com. All rights reserved.
  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:
  (1) Redistributions of source code must retain the above copyright notice,
  this list of conditions and the following disclaimer. (2) Redistributions in
  binary form must reproduce the above copyright notice, this list of
  conditions and the following disclaimer in the documentation and/or other
  materials provided with the distribution. THIS SOFTWARE IS PROVIDED BY THE
  COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
  EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
  INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES HOWEVER
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
  LIABILITY, OR TORT ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
  IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

  #v(1.2mm)
  *arduino-esp32* -- Copyright (c) Espressif Systems. Licensed under the GNU
  Lesser General Public License v2.1 or later. El texto completo de la licencia
  y el codigo fuente correspondiente estan disponibles en
  #fact("contact.support_url"). / The full licence text and the corresponding
  source code are available at #fact("contact.support_url").

  #v(2mm)
  #line(length: 100%, stroke: 0.4pt + black)
  #v(1.5mm)

  #grid(columns: (1fr, auto), align: (left, right),
    [
      #fact("product.name") #fact("product.model") \
      Firmware #fact("product.fw_version") \
      Manual v#fact("manual.version") -- #fact("manual.date")
    ],
    [
      #fact("entity.name") \
      #fact("contact.website") \
      Hecho en #fact("product.country_of_manufacture") \
      Made in #fact("product.country_of_manufacture")
    ],
  )
]
