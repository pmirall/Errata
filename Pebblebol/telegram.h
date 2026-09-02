// =============================================================================
//  telegram.h - Nottamagochi
//  The guilt channel: passive-aggressive Telegram messages about the pet.
//
//  This is the ONLY TLS consumer in the firmware (BRIEF 1.7, NET_APIS 3).
//  Every send is gated on RADIO_WIFI + no BLE + >= TLS_MIN_MAXALLOC_HEAP.
//
//  Public API (BRIEF 4, module 16) - exactly five entry points:
//      tg_begin(), tg_queue(), tg_service(), tg_set_mode(), tg_url_encode()
//
//  All user-facing text comes from strings_es.h. No Spanish literal lives here.
// =============================================================================
#ifndef NT_TELEGRAM_H
#define NT_TELEGRAM_H

#include <stddef.h>
#include "nt_types.h"

// -----------------------------------------------------------------------------
//  tg_begin()
//  Reset the queue and the anti-spam ledger, cache the Telegram mode from the
//  live Config, and seed the guilt index from the live PetSave.
//  Safe to call again at any time (e.g. after a factory reset). Does no I/O.
// -----------------------------------------------------------------------------
void tg_begin(void);

// -----------------------------------------------------------------------------
//  tg_queue(id, prio)
//  Enqueue one message. Returns true if it was accepted.
//
//  Rejected when: the module is off, the id is out of range, the id is already
//  queued, the id is inside its 48 h cooldown, or the queue is full and nothing
//  of lower priority can be evicted.
//
//  The module raises the state-derived guilt messages on its own (T01..T08,
//  T10..T13, T15 - see GAME_DESIGN 7.2). T09 lost its only source when weather
//  was removed and can no longer be raised at all. Callers only need to queue
//  the event-driven ones, which this module cannot observe:
//      MSG_T14  death                     PRIO_P0   (whoever stages the death)
//      MSG_P01  evolution                 PRIO_P1
//      MSG_P02  wish granted              PRIO_P1
//      MSG_P03  BLE mating                PRIO_P1
//      MSG_P04  hatch                     PRIO_P0
//      MSG_P05  weekly summary (Sun 20h)  PRIO_P2
//  Call from loop() context only - never from a BLE/WiFi callback task.
// -----------------------------------------------------------------------------
bool tg_queue(MsgId id, Priority prio);

// -----------------------------------------------------------------------------
//  tg_service()
//  Pump. Call once per loop(). Cheap on every pass except the one that actually
//  transmits: HTTPClient is synchronous, so a real send blocks for 1-3 s. That
//  pass is gated behind an idle window (see TG_IDLE_BEFORE_SEND_S in the .cpp)
//  so the hitch never lands while the player is pressing buttons.
// -----------------------------------------------------------------------------
void tg_service(void);

// -----------------------------------------------------------------------------
//  tg_set_mode(mode)
//  TG_OFF / TG_ONLY_SEVERE (P0 only) / TG_ON. Does NOT persist - the settings
//  screen owns Config.tg_mode and storage owns writing it.
// -----------------------------------------------------------------------------
void tg_set_mode(TgMode mode);

// -----------------------------------------------------------------------------
//  tg_url_encode(in, out, out_cap)
//  RFC 3986 percent-encoding of a UTF-8 string, byte by byte. Unreserved set is
//  A-Z a-z 0-9 - _ . ~ ; everything else becomes %XX with uppercase hex, so
//  "ñ" -> %C3%B1 and "\xF0\x9F\x90\xA3" -> %F0%9F%90%A3.
//  Returns the number of bytes written, excluding the NUL. Returns 0 and writes
//  an empty string if the result would not fit (never truncates silently).
//  Pure function - host-tested.
// -----------------------------------------------------------------------------
size_t tg_url_encode(const char* in, char* out, size_t out_cap);

// -----------------------------------------------------------------------------
//  INTEGRATION SEAM (not part of the tg_* API)
//
//  telegram.cpp needs a read-only view of the live pet and of the live config.
//  It defines both of these with WEAK linkage returning nullptr, so the module
//  links and runs standalone; define a STRONG version anywhere in the sketch
//  and it takes over.
//
//  These prototypes are declared here - NOT merely documented - on purpose.
//  Without a visible declaration, a strong definition whose signature drifted
//  (PetSave* instead of const PetSave*, an argument added, a different return
//  type) would be a new OVERLOAD rather than an override: the weak nullptr
//  version stays bound, Telegram goes permanently mute, and there is no
//  compile-time and no link-time error. With them, any drift is a hard error.
//
//  The integrator must define both, exactly as declared:
//      const PetSave* nt_pet_view(void) { ... }   // nullptr until sim_init()
//      const Config*  nt_cfg_view(void) { ... }   // nullptr until store_load_cfg()
//  Without them the module stays silent: no token, no pet, nothing to say.
// -----------------------------------------------------------------------------

const PetSave* nt_pet_view(void);   // nullptr until sim_init()
const Config*  nt_cfg_view(void);   // nullptr until store_load_cfg()

#endif // NT_TELEGRAM_H
