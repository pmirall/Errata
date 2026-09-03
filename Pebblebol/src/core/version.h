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

#define SAVE_SCHEMA_VERSION     2
#define CONTENT_VERSION         1
#define PROTOCOL_VERSION        1
#define CREATOR_API_VERSION     1

// The legacy Nottamagochi save generation, kept as a name so the migration
// table reads as a version map instead of a magic number.
#define SAVE_SCHEMA_VERSION_V1  1

#endif // PB_VERSION_H
