// =============================================================================
//  PEBBLEBOL - data/content_version.h
//
//  GENERATED FILE. Do not edit: tools/gen_content.py rewrites it from
//  tools/content/*.json. `tools/gen_content.py --check` fails the gate if
//  this file and the JSON have drifted apart.
//
//  CONTENT_VERSION - a hash of the content, not a number somebody bumps.
//
//  PebbleInstance and the save blobs record the CONTENT_VERSION they were
//  written against (persistence/save_schema.h), so a save can say which
//  roster it means. Plan line 668 requires that this number change when
//  the JSON changes, and a hand-maintained counter does not.
//
//  THE HASH INPUT SET, stated so the next reader does not have to guess:
//    * the six tools/content/*.json files, in a fixed order;
//    * every key EXCEPT the `_`-prefixed ones. Those are design and art
//      notes; folding them in would mean a typo fix in a comment marks
//      every save on every device as made against foreign content;
//    * the emitted roster size, because two builds shipping different
//      numbers of species ARE different content from identical JSON.
//  FNV-1a 32 over that canonical UTF-8 text, folded with h ^ (h >> 16) and
//  masked to 16 bits, with 0 remapped to 1 so the value is never the one a
//  zeroed blob would carry.
// =============================================================================

#ifndef PB_CONTENT_VERSION_H
#define PB_CONTENT_VERSION_H

// 60 species (20 families), 34 attacks, 10 items, 40 evolution rules, 40 encounter rows
#define CONTENT_VERSION  0x8403u

#endif // PB_CONTENT_VERSION_H
