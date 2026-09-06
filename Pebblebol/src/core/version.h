// =============================================================================
//  PEBBLEBOL - core/version.h
//  The four version numbers that leave the device, plus the firmware string.
//  Plan section 1.4 (module contracts) and section 60 of the product spec.
//
//  Each number answers exactly one question and moves for exactly one reason:
//
//    FW_VERSION           human-facing build name. Cosmetic; nothing branches
//                         on it. The single definition lives here - config.h
//                         includes this header so every old user still sees it.
//    SAVE_SCHEMA_VERSION  the shape of the NVS blobs (persistence/save_schema.h).
//                         Bump it when a persisted layout changes; a save that
//                         carries a HIGHER number is refused, never rewritten
//                         (LOAD_FOREIGN_NEWER), and a LOWER one is migrated
//                         (persistence/migration.h). Version 1 = the legacy
//                         Nottamagochi blobs frozen in legacy_v1.h.
//    CONTENT_VERSION      the content tables (species/attacks/items/encounters).
//                         DEFINED IN data/content_version.h, generated.
//                         Recorded in BoxHeader so a save made against newer
//                         content can be spotted; it never blocks a load,
//                         because unknown ids are validated per field.
//    PROTOCOL_VERSION     the byte 0 of every radio frame (networking/protocol.h).
//                         Peers with a different number are refused politely.
//    CREATOR_API_VERSION  the HTTP contract the phone page speaks (spec 38).
//
//  Header-only, no includes: anything in the tree may pull this in, including
//  the pure layers compiled by the host test Makefile.
// =============================================================================
#ifndef PB_VERSION_H
#define PB_VERSION_H

#define FW_VERSION              "0.2.0-dev"

// THE BUILD STAMP (P10-C1). Spec section 49's field list asks for "Firmware"
// AND "Build" as two separate lines, and until this phase the second one did
// not exist anywhere: `grep -rn "__DATE__" Pebblebol/src tools` returned
// nothing, so a bench operator holding two boards had no way to tell which
// image was on which. FW_VERSION answers "what release is this"; this answers
// "which compile", which is the question that matters when the same version
// string has been flashed nine times in an afternoon.
//
// It is FIXED WIDTH ("MMM DD YYYY HH:MM:SS", 20 chars + NUL) whatever the
// clock says, so it never moves the size numbers between two builds of the
// same tree. It is the ONE thing in the firmware that is not a pure function
// of the sources, and it is deliberately confined to this one string: nothing
// branches on it, nothing persists it and nothing transmits it.
#define FW_BUILD_STAMP          (__DATE__ " " __TIME__)

#define SAVE_SCHEMA_VERSION     2
// CONTENT_VERSION LIVES IN data/content_version.h SINCE P4-C1. It is a HASH of
// tools/content/*.json emitted by tools/gen_content.py, not a counter somebody
// remembers to bump, because plan line 668 requires it to change when the JSON
// changes and a hand-maintained number does not. This header stays include-free
// (the pure layers pull it in), so the define is not forwarded from here -
// persistence/save_schema.h includes both.
// BUMPED 1 -> 2 BY P7-C4. Plan rule 1.3 section 5 - "persisted or transmitted
// layouts are never edited in place" - makes this mandatory: the trade needs
// four message types and networking/protocol.h's type space was CLOSED at 1..12
// (proto_decode() refuses anything else with PE_TYPE, and there is no reserved
// forward channel to smuggle them through). A peer speaking version 1 is
// refused politely at CAPABILITIES rather than handed a frame it would read as
// a different message. BoxHeader.protocol_version follows automatically.
#define PROTOCOL_VERSION        2
#define CREATOR_API_VERSION     1

// The legacy Nottamagochi save generation, kept as a name so the migration
// table reads as a version map instead of a magic number.
#define SAVE_SCHEMA_VERSION_V1  1

#endif // PB_VERSION_H
