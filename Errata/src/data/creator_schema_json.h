// =============================================================================
//  ERRATA - data/creator_schema_json.h
//
//  GENERATED FILE. Do not edit: tools/gen_content.py rewrites it from
//  tools/content/*.json. `tools/gen_content.py --check` fails the gate if
//  this file and the JSON have drifted apart.
//
//  THE CREATOR SCHEMA, AS THE BYTES GET /api/schema SERVES (plan T4).
//
//  One document, generated from the SAME content object as
//  data/creator_schema.h in the same run, so the numbers the phone page
//  reads and the numbers the on-device validator enforces cannot drift.
//  THREE THINGS HOLD THAT, and each catches what the others cannot:
//    1. `tools/gen_content.py --check` fails the gate on a hand edit to
//       either header.
//    2. The CREATOR_SCHEMA_JSON_* defines below are static_asserted
//       against the compiled constants, so two emitters in one script
//       drifting from each other is a BUILD failure.
//    3. tests/test_creator_api.cpp reads the blob's TEXT and asserts the
//       numbers in it, which is the only half that can see an emitter
//       whose defines are right and whose document is not.
//
//  NO Arduino.h AND NO PROGMEM, DELIBERATELY. On this target .rodata is
//  memory-mapped and send_P's PGM_P is an ordinary pointer, so the
//  attribute buys nothing - and leaving it off is what lets a HOST test
//  include this header at all. A blob no host binary can read is a blob
//  whose agreement with the schema is a comment.
//
//  Serve it with the FOUR-argument send_P; the 3-arg form strlen_P()s
//  the blob (WebServer.cpp:619) and the 1-arg sendContent_P has the
//  same bug.
//
//  The attack rows are POSITIONAL - [id, type, power, accuracy, cost] -
//  and not objects, which is the difference between this size and about
//  twice it. The page reads a[4] for the spec section 36 cost.
//
//  THE NAMES ARE HERE FOR THE SAME REASON THE NUMBERS ARE (P8-C4). The
//  page has to draw 34 attacks, and 34 Spanish strings typed into
//  web/creator/app.js would be a second source of truth no gate can see
//  drift in. "an" is one name per attack row IN THE TABLE'S ORDER, so
//  an[k] pairs with atk[k] positionally and the page needs no lookup;
//  "tn" is the type names, indexed by the type ordinal, with NEUTRAL
//  last at index TYPE_COUNT - the relation the page's pool filter and
//  game/validate.cpp both rely on.
//
//  The names are \uXXXX ESCAPES, which is what keeps this document
//  printable ASCII while carrying Rafaga and Infeccion. JSON.parse hands
//  the browser the real characters. THE DEVICE NEVER READS THIS HALF, so
//  core/strings_es.h's Latin-1 question does not arise on its side, and
//  tests/test_creator_api.cpp compares the two BY CODEPOINT because one
//  side is UTF-8 bytes and the other is escapes.
//
//  IT IS .rodata AND COSTS ZERO GLOBALS.
// =============================================================================

#ifndef ER_CREATOR_SCHEMA_JSON_H
#define ER_CREATOR_SCHEMA_JSON_H

#include <stddef.h>

#include "creator_schema.h"    // and, through it, core/version.h
#include "../core/config.h"     // NAME_MAX_LEN, WEB_HTML_MAX

// The numbers the document below declares, as values a static_assert can
// compare. They are a COPY: the definitions are creator_schema.h's and
// core/config.h's, and the asserts fail the build the moment a copy stops
// matching. What they cannot see is the document TEXT itself, which is
// what tests/test_creator_api.cpp reads.
#define CREATOR_SCHEMA_JSON_API        1
#define CREATOR_SCHEMA_JSON_STAT_MIN   16
#define CREATOR_SCHEMA_JSON_STAT_MAX   22
#define CREATOR_SCHEMA_JSON_ATK_BUDGET 185
#define CREATOR_SCHEMA_JSON_POW_CAP    90
#define CREATOR_SCHEMA_JSON_NAME_MAX   12
#define CREATOR_SCHEMA_JSON_SPR_W      24
#define CREATOR_SCHEMA_JSON_SPR_H      24
#define CREATOR_SCHEMA_JSON_SPR_FRAMES 2
#define CREATOR_SCHEMA_JSON_SPR_BYTES  72
#define CREATOR_SCHEMA_JSON_ID_MIN     200
#define CREATOR_SCHEMA_JSON_ID_SLOTS   10
#define CREATOR_SCHEMA_JSON_MOVES      4
#define CREATOR_SCHEMA_JSON_TYPES      3

static const char CREATOR_SCHEMA_JSON[] = R"JSON({"v":1,"types":3,"stat":{"min":16,"max":22,"lo":1,"hi":10},"atkbudget":185,"powcap":90,"moves":4,"sprite":{"w":24,"h":24,"f":2,"bytes":72},"name":{"max":12},"id":{"min":200,"max":209,"slots":10},"tn":["SIGNAL","CORRUPT","SYSTEM","NEUTRAL"],"atk":[[1,0,35,100,35],[2,0,55,95,52],[3,0,75,85,63],[4,0,40,100,52],[5,0,35,90,54],[6,0,0,100,21],[7,0,0,100,21],[8,0,90,75,51],[9,1,35,100,35],[10,1,55,95,52],[11,1,75,85,63],[12,1,30,90,49],[13,1,20,100,50],[14,1,50,90,65],[15,1,0,90,23],[16,1,0,100,20],[17,1,85,80,50],[18,2,35,100,35],[19,2,55,95,52],[20,2,75,85,63],[21,2,30,90,42],[22,2,0,100,34],[23,2,0,100,19],[24,2,0,100,19],[25,2,0,100,21],[26,2,95,70,36],[27,3,50,100,50],[28,3,100,55,47],[29,3,0,100,16],[30,3,0,100,38],[31,3,0,100,44],[32,3,0,100,21],[33,3,0,100,21],[34,3,0,100,14]],"an":["Ping","Pulso","R\u00e1faga","Adelanto","Interferir","Amplificar","Antena","Eco Doble","Bytazo","Mordisco","Plaga","Infectar","Infecci\u00f3n","Devorar","Corromper","Frenes\u00ed","Gusano","Escaneo","N\u00facleo","Sobrecarga","Bloqueo","Firewall","Cifrado","Permisos","Reinicio","P\u00e1nico","Choque","Apuesta","Defrag","Cach\u00e9","Sandbox","Overclock","Backup","Depurar"]})JSON";
static const size_t CREATOR_SCHEMA_JSON_LEN = sizeof(CREATOR_SCHEMA_JSON) - 1;

static_assert(CREATOR_SCHEMA_JSON_API == CREATOR_API_VERSION,
              "the served schema declares an API version core/version.h does not");
static_assert(CREATOR_SCHEMA_JSON_STAT_MIN == CREATOR_STAT_POINTS_MIN,
              "the served stat floor is not the compiled one");
static_assert(CREATOR_SCHEMA_JSON_STAT_MAX == CREATOR_TOTAL_STAT_POINTS,
              "the served stat budget is not the compiled one");
static_assert(CREATOR_SCHEMA_JSON_ATK_BUDGET == CREATOR_ATTACK_BUDGET,
              "the served attack budget is not the compiled one");
static_assert(CREATOR_SCHEMA_JSON_POW_CAP == CREATOR_POWER_CAP_BY_STAGE[1],
              "the served power cap is not the stage-1 cap the validator uses");
static_assert(CREATOR_SCHEMA_JSON_NAME_MAX == NAME_MAX_LEN,
              "the served name length is not core/config.h's");
static_assert(CREATOR_SCHEMA_JSON_SPR_W == CS_SPRITE_W &&
              CREATOR_SCHEMA_JSON_SPR_H == CS_SPRITE_H &&
              CREATOR_SCHEMA_JSON_SPR_FRAMES == CS_SPRITE_FRAMES &&
              CREATOR_SCHEMA_JSON_SPR_BYTES == CS_SPRITE_BYTES,
              "the served sprite geometry is not the one CustomSpeciesRec holds: "
              "the page would draw a grid the record cannot store");
static_assert(CREATOR_SCHEMA_JSON_ID_MIN == CREATOR_SPECIES_ID_MIN &&
              CREATOR_SCHEMA_JSON_ID_SLOTS == CREATOR_SPECIES_SLOTS,
              "the served custom id range is not the compiled one");
static_assert(CREATOR_SCHEMA_JSON_MOVES == CREATOR_MOVE_COUNT &&
              CREATOR_SCHEMA_JSON_TYPES == (int)TYPE_COUNT,
              "the served move count or type count is not the compiled one");
static_assert(CREATOR_SCHEMA_JSON_LEN < WEB_HTML_MAX,
              "the schema document has outgrown the page budget, which means it "
              "has stopped being a schema");

#endif // ER_CREATOR_SCHEMA_JSON_H
