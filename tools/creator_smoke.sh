#!/usr/bin/env bash
# =============================================================================
#  tools/creator_smoke.sh - THE CREATOR API, OVER A REAL SOCKET (P8-C5)
#
#  Drives every spec section 38 route on a BOARD from a host joined to the
#  board's access point, and asserts the four behaviours no host binary in this
#  tree can observe: a body over the cap answered 413, a wrong PIN answered 403,
#  a sixth wrong PIN answered with the lockout, and the portal tearing itself
#  down after ConfigV2.creator_idle_s of unauthenticated traffic.
#
#  =========================================================================
#  THERE IS NO FIXTURE HERE, AND THAT IS THE POINT
#  =========================================================================
#  Phase 7's fifteen-point power-cut sweep ran against a save_manager the
#  release artefact does not execute, because the FIXTURE bound a null clock;
#  phase 6 shipped four phases of frozen game time because a default lived in a
#  dev-only function. The instruction that comes out of both is that a harness
#  must bind what app/app.cpp binds. THIS SCRIPT BINDS NOTHING. It speaks HTTP
#  to a device that ran app_setup() - the same web_bind_config(&g_cfg), the same
#  web_begin(WEB_PORT), the same rng_seed_all(esp_random()), the same gt_begin()
#  and the same one 2 KB CreatorBody - so the wiring is not reproduced, it is
#  the wiring. There is deliberately NO --fake, NO mock server and NO loopback
#  default: a green run against a stub would be worth less than no run at all,
#  and tools/check.sh has a gate that fails if a loopback host ever appears in
#  this file.
#
#  =========================================================================
#  WHAT IT THEREFORE CANNOT CONTROL, AND WHAT IT DOES ABOUT EACH
#  =========================================================================
#  1. WHICH VARIANT IS FLASHED. docs/budget.md section 8 records a phase-7 exit
#     that quoted a dev build against a shipping one. --variant is REQUIRED and
#     has no default, and it is printed in the header of every run, so a result
#     that does not name its artefact cannot be produced by accident. The
#     release build is GOD_MODE_ENABLED=0; a baseline result is evidence about
#     baseline and nothing else.
#  2. WHICH TREE THE BOARD WAS BUILT FROM. Checked, not assumed: phase 1 reads
#     GET /api/state and GET / and compares the API version, FW_VERSION,
#     CONTENT_VERSION, CS_BODY_MAX and the page's exact Content-Length against
#     this working tree. A stale board fails at the first phase instead of
#     producing a bench result about firmware nobody has.
#  3. CF_WEB_ENABLED. SETTINGS -> "Web y QR" must be on or the socket never
#     opens. The script cannot set it; a connection refusal in phase 1 names it.
#  4. THE DEVICE MUST BE ON THE CREATOR SCREEN. The portal exists only while it
#     is (spec section 40), so the operator opens it and leaves it open.
#  5. THE BOX AND THE cs REGISTRY. POST /api/bug answers 409 "nopet" on an
#     empty Box, "boxfull" or "csfull" when there is no room. The script READS
#     the free counts from /api/state and refuses the write phase with a stated
#     reason rather than reporting an unexplained 409.
#  6. ConfigV2.creator_idle_s. Read from the device it cannot be - /api/state
#     does not serve it - so the idle phase assumes the CREATOR_IDLE_S_DEFAULT
#     compiled into this tree and --idle-s overrides it.
#
#  =========================================================================
#  SIDE EFFECTS ON THE DEVICE - THIS IS NOT A READ-ONLY SCRIPT
#  =========================================================================
#    * POST /api/time SETS THE DEVICE CLOCK from this host's clock, exactly as
#      a phone would (CAL_PHONE). Do not run it from a host whose clock is
#      wrong.
#    * POST /api/bug CREATES ONE BUG named SMOKE and consumes one Box
#      slot and one of the ten creator-species slots. --no-write skips it.
#    * The PIN failure counter is driven to the lockout and then CLEARED by a
#      successful authorisation, so the device is not left armed. The lockout
#      phase costs CREATOR_PIN_LOCK_MS of waiting for that reason.
#    * Nothing else is written. No route this script calls can delete a Bug.
#
#  Usage:
#    tools/creator_smoke.sh --variant release --pin 1234 [options]
#
#      --variant NAME   REQUIRED. The build flashed on the board, as named by
#                       tools/build_matrix.sh (release, baseline, no-god, ...).
#      --pin NNNN       REQUIRED. The four digits the CREATOR screen is showing.
#      --url URL        default http://192.168.4.1
#      --no-write       skip POST /api/bug (no Bug is created)
#      --no-lock        skip the wrong-PIN and lockout phases
#      --no-idle        skip the idle-timeout phase (which takes ~6 minutes)
#      --idle-s N       the device's ConfigV2.creator_idle_s, if not the default
#      --dry-run        print the constants and the plan, touch no network
#
#  Exit: 0 every assertion held, 1 an assertion failed, 2 it could not run.
#  All identifiers and comments English; nothing here is user-facing text.
# =============================================================================
set -uo pipefail
# NOT -e. Every check below is an assertion that must be able to fail, print
# what it saw and let the remaining phases run: a script that dies on the first
# 403 tells the operator one thing when it could have told them nine.

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SKETCH="${SKETCH:-$ROOT/Errata}"

CFG="$SKETCH/src/core/config.h"
VER="$SKETCH/src/core/version.h"
CVH="$SKETCH/src/data/content_version.h"
IDX="$SKETCH/src/data/index_html.h"

die() { printf 'creator_smoke.sh: %s\n' "$*" >&2; exit 2; }

for f in "$CFG" "$VER" "$CVH" "$IDX"; do
  [ -f "$f" ] || die "$f not found - this script reads its expectations out of the tree it belongs to"
done

# -----------------------------------------------------------------------------
#  THE CONSTANTS COME OUT OF THE TREE, NEVER OUT OF THIS FILE
#
#  A smoke script that types 2048 is a smoke script that keeps asserting 2048
#  after somebody moves CS_BODY_MAX, and it fails for a reason that has nothing
#  to do with the device. Every number below is read from the header that owns
#  it, and a lookup that finds nothing is a hard exit 2 with the macro named -
#  which is what makes `--dry-run` a gate tools/check.sh can run.
# -----------------------------------------------------------------------------
# THESE RETURN, THEY DO NOT die(). A `die` inside `$(...)` exits the SUBSHELL
# and leaves the script running with an empty value - which is how the first
# version of this file printed "not found" and then exited 0, a gate that could
# not fail written into the file whose whole subject is gates that cannot fail.
# Every call site below is `|| exit 2`, and the mutation that renames a macro in
# config.h is what proved it.
def_num() {   # def_num FILE MACRO -> the decimal value, U/L suffixes stripped
  local v
  v="$(grep -oE "^#define[[:space:]]+$2[[:space:]]+[0-9]+[UuLl]*" "$1" | head -1 \
       | grep -oE "[0-9]+[UuLl]*$" | tr -d 'UuLl')"
  [ -n "$v" ] || { printf 'creator_smoke.sh: #define %s not found in %s (renamed? this script'"'"'s expectations are out of date)\n' "$2" "$1" >&2; return 1; }
  printf '%s' "$v"
}
def_hex() {   # def_hex FILE MACRO -> the value of a 0x... macro, as decimal
  local v
  v="$(grep -oE "^#define[[:space:]]+$2[[:space:]]+0[xX][0-9a-fA-F]+" "$1" | head -1 \
       | grep -oE "0[xX][0-9a-fA-F]+$")"
  [ -n "$v" ] || { printf 'creator_smoke.sh: #define %s not found in %s\n' "$2" "$1" >&2; return 1; }
  printf '%d' "$v"
}
def_str() {   # def_str FILE MACRO -> the contents of a "quoted" macro
  local v
  v="$(sed -nE "s/^#define[[:space:]]+$2[[:space:]]+\"([^\"]*)\".*/\1/p" "$1" | head -1)"
  [ -n "$v" ] || { printf 'creator_smoke.sh: #define %s not found in %s\n' "$2" "$1" >&2; return 1; }
  printf '%s' "$v"
}

CS_BODY_MAX="$(def_num "$CFG" CS_BODY_MAX)" || exit 2
CS_BODY_DRAIN_MAX="$(def_num "$CFG" CS_BODY_DRAIN_MAX)" || exit 2
PIN_FAIL_MAX="$(def_num "$CFG" CREATOR_PIN_FAIL_MAX)" || exit 2
PIN_LOCK_MS="$(def_num "$CFG" CREATOR_PIN_LOCK_MS)" || exit 2
IDLE_S_DEFAULT="$(def_num "$CFG" CREATOR_IDLE_S_DEFAULT)" || exit 2
RATE_TOKENS="$(def_num "$CFG" WEB_RATE_TOKENS)" || exit 2
RATE_REFILL="$(def_num "$CFG" WEB_RATE_REFILL_PER_S)" || exit 2
WEB_PORT="$(def_num "$CFG" WEB_PORT)" || exit 2
# ...AND IT IS ACTUALLY USED, SINCE THE FINAL REVIEW. It was read, printed in
# the run banner as "target $URL (port $WEB_PORT)", and never appended - so the
# banner would have told the operator it targeted the right port while every
# request in the D block went to 80. Harmless today (WEB_PORT is 80) and a
# silent wrong-target failure the moment it is not.
if [ "$WEB_PORT" != "80" ]; then URL="$URL:$WEB_PORT"; fi
API_VERSION="$(def_num "$VER" CREATOR_API_VERSION)" || exit 2
FW_VERSION="$(def_str "$VER" FW_VERSION)" || exit 2
CONTENT_VERSION="$(def_hex "$CVH" CONTENT_VERSION)" || exit 2

# INDEX_HTML_LEN is `sizeof(INDEX_HTML) - 1` and therefore not a literal any
# grep can read. It is measured here the way the compiler measures it: the raw
# string literal begins immediately after the `R"PBHTML(` that ends its line -
# so the newline that follows IS the first byte - and ends at the newline before
# `)PBHTML";`. That is the Content-Length the device serves, and it is two bytes
# more than the page size gen_index_html.py prints (the literal's own opening
# and closing newlines), which is why this is measured rather than copied.
page_len() {
  awk '
    /^static const char INDEX_HTML\[\] PROGMEM = R"PBHTML\($/ { inb = 1; n = 1; next }
    /^\)PBHTML";$/                                            { if (inb) { inb = 0; print n; exit } }
    inb                                                       { n += length($0) + 1 }
  ' "$1"
}
PAGE_LEN="$(page_len "$IDX")"
[ -n "$PAGE_LEN" ] && [ "$PAGE_LEN" -gt 1000 ] \
  || die "could not measure the page blob in $IDX (the generated raw-string literal changed shape)"

# -----------------------------------------------------------------------------
#  ARGUMENTS
# -----------------------------------------------------------------------------
# WEB_PORT is appended below, once config.h has been read. Until the final
# review it was parsed, PRINTED IN THE RUN BANNER, and never used - so if the
# port ever moved, every request in the D block would have gone to 80 while the
# banner told the operator it had targeted the right one.
URL="http://192.168.4.1"
PIN=""
VARIANT=""
DO_WRITE=1
DO_LOCK=1
DO_IDLE=1
IDLE_S="$IDLE_S_DEFAULT"
DRY=0
TIMEOUT=10

while [ $# -gt 0 ]; do
  case "$1" in
    --url)      URL="${2:-}"; shift 2 ;;
    --pin)      PIN="${2:-}"; shift 2 ;;
    --variant)  VARIANT="${2:-}"; shift 2 ;;
    --idle-s)   IDLE_S="${2:-}"; shift 2 ;;
    --no-write) DO_WRITE=0; shift ;;
    --no-lock)  DO_LOCK=0; shift ;;
    --no-idle)  DO_IDLE=0; shift ;;
    --dry-run)  DRY=1; shift ;;
    -h|--help)  sed -n '2,80p' "$0"; exit 0 ;;
    *) die "unknown argument $1" ;;
  esac
done

URL="${URL%/}"

# The pacing. WEB_RATE_TOKENS tokens, WEB_RATE_REFILL_PER_S per second, 1 per
# read and 2 per mutation: at this spacing the bucket has refilled more than a
# mutation costs before the next request leaves, so a 429 is a finding rather
# than this script racing itself.
PACE="$(awk -v r="$RATE_REFILL" 'BEGIN { printf "%.2f", 3.0 / r }')"

if [ "$DRY" -eq 1 ]; then
  echo "creator_smoke.sh --dry-run: reading its expectations out of $SKETCH/src"
  echo "  CS_BODY_MAX            $CS_BODY_MAX"
  echo "  CS_BODY_DRAIN_MAX      $CS_BODY_DRAIN_MAX"
  echo "  CREATOR_PIN_FAIL_MAX   $PIN_FAIL_MAX"
  echo "  CREATOR_PIN_LOCK_MS    $PIN_LOCK_MS"
  echo "  CREATOR_IDLE_S_DEFAULT $IDLE_S_DEFAULT"
  echo "  WEB_RATE_TOKENS        $RATE_TOKENS"
  echo "  WEB_RATE_REFILL_PER_S  $RATE_REFILL"
  echo "  WEB_PORT               $WEB_PORT"
  echo "  CREATOR_API_VERSION    $API_VERSION"
  echo "  FW_VERSION             $FW_VERSION"
  echo "  CONTENT_VERSION        $CONTENT_VERSION"
  echo "  INDEX_HTML_LEN         $PAGE_LEN"
  echo "  pace between requests  ${PACE}s"
  echo "  routes probed          / /api/schema /api/state /api/validate /api/bug /api/time /api/ping"
  echo "creator_smoke.sh --dry-run: NOTHING WAS SENT AND NOTHING WAS OBSERVED."
  echo "  A green --dry-run means this script can still read the tree. It is not"
  echo "  evidence about any device and must never be reported as one."
  exit 0
fi

[ -n "$VARIANT" ] || die "--variant is required: name the build flashed on the board (docs/budget.md section 8 - a result that does not name its artefact is not a result)"
[ -n "$PIN" ] || die "--pin is required: the four digits the CREATOR screen is showing"
case "$PIN" in
  [0-9][0-9][0-9][0-9]) ;;
  *) die "--pin must be exactly four digits (networking/creator_gate.cpp cg_parse_pin)" ;;
esac
command -v curl >/dev/null 2>&1 || die "curl not found"

# -----------------------------------------------------------------------------
#  PLUMBING
# -----------------------------------------------------------------------------
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
BODYF="$TMP/body"
ERRF="$TMP/err"

FAILS=0
CHECKS=0
STATUS=""
CURL_EXIT=0

ok()   { CHECKS=$((CHECKS + 1)); printf '  ok    %s\n' "$*"; }
bad()  { CHECKS=$((CHECKS + 1)); FAILS=$((FAILS + 1)); printf '  FAIL  %s\n' "$*"; }
note() { printf '        %s\n' "$*"; }
phase() { printf '\n== %s\n' "$*"; }

body_head() { head -c 200 "$BODYF" 2>/dev/null | tr -d '\n'; }

# One request. `pin` is the X-Pin header to send, or "-" for none.
raw() {  # raw METHOD PATH PIN [extra curl args...]
  local method="$1" path="$2" pin="$3"; shift 3
  local args=(-sS -o "$BODYF" -w '%{http_code}' -X "$method" --max-time "$TIMEOUT")
  [ "$pin" = "-" ] || args+=(-H "X-Pin: $pin")
  CURL_EXIT=0
  STATUS="$(curl "${args[@]}" "$@" "$URL$path" 2>"$ERRF")" || CURL_EXIT=$?
  [ -n "$STATUS" ] || STATUS="000"
  sleep "$PACE"
}

# The same, retrying ONCE on 429. The rate limiter is a real behaviour and not a
# flake, so a throttled request is retried after a full bucket refill and then
# reported if it throttles again - never swallowed.
req() {
  raw "$@"
  if [ "$STATUS" = "429" ]; then
    note "429 from $2 - waiting for the bucket ($RATE_TOKENS tokens at $RATE_REFILL/s) and retrying once"
    sleep "$(awk -v t="$RATE_TOKENS" -v r="$RATE_REFILL" 'BEGIN { printf "%.1f", t / r + 0.5 }')"
    raw "$@"
  fi
}

expect() {  # expect STATUS "what was being claimed"
  if [ "$STATUS" = "$1" ]; then ok "$2"
  else bad "$2 -- got HTTP $STATUS (curl exit $CURL_EXIT), body: $(body_head)"; fi
}

# The device builds every response with snprintf and emits no whitespace, so
# these could be tighter. They allow it anyway: a reader that only matches the
# exact byte spacing of today's format string turns a cosmetic change into a
# smoke run that reports "absent" for a field that is right there.
json_num() { grep -oE "\"$1\"[[:space:]]*:[[:space:]]*[0-9]+" "$BODYF" | head -1 | grep -oE "[0-9]+$" ; }
json_str() { sed -nE "s/.*\"$1\"[[:space:]]*:[[:space:]]*\"([^\"]*)\".*/\1/p" "$BODYF" | head -1 ; }

expect_num() {  # expect_num KEY WANT "claim"
  local got; got="$(json_num "$1")"
  if [ "$got" = "$2" ]; then ok "$3"
  else bad "$3 -- \"$1\" is ${got:-absent}, this tree says $2"; fi
}

# -----------------------------------------------------------------------------
#  THE DOCUMENT
#
#  The same shape tests/test_creator_api.cpp's VALID_BODY uses, so a refusal
#  here is about the socket and not about the document: base [6,5,5,5] and
#  moves [1,6,32,34] are inside the section 36 budget and the two sprite frames
#  are exactly 2*CS_SPRITE_BYTES hex characters each.
#
#  NO "budget" KEY AND NO "slot" KEY, and their absence is the assertion: the
#  device prices the Bug itself and picks the slot itself, so a document
#  carrying either is refused CP_UNKNOWN_KEY at the character it appears.
# -----------------------------------------------------------------------------
ZEROS="$(printf '%0144d' 0)"
EFFS="$(printf '%0144d' 0 | tr '0' 'f')"
FRAME0="${ZEROS:0:142}ff"
FRAME1="${EFFS:0:48}${ZEROS:0:96}"
DOC="{\"v\":$API_VERSION,\"name\":\"SMOKE\",\"type\":0,\"base\":[6,5,5,5],\
\"moves\":[1,6,32,34],\"sprite\":[\"$FRAME0\",\"$FRAME1\"]}"
printf '%s' "$DOC" > "$TMP/doc.json"

# THE WRONG PIN, derived once and used by every probe below that needs one.
# (It used to be derived inside phase 3; the body-PIN documents need it too.)
WRONG="$(printf '%04d' $(( (10#$PIN + 1111) % 10000 )))"

# TWO MORE DOCUMENTS, IDENTICAL TO $DOC BUT FOR ONE FIELD: they carry the PIN
# in the BODY instead of in a header. Spec section 38 allows both, and until
# this script ran, NOTHING IN THE TREE HAD EVER EXECUTED THE BODY HALF -
# creator_server.cpp's pin_after_body() had no caller that any host test, the
# browser harness or this script reached, because the page sends X-Pin on every
# request and every other unauthenticated POST here is refused by the BODY
# layer (413/411/415) before the PIN is consulted. Spliced off $DOC rather than
# retyped so a refusal cannot be about a second difference.
DOC_BODYPIN_BAD="{\"v\":$API_VERSION,\"pin\":\"$WRONG\",${DOC#*,}"
DOC_BODYPIN_OK="{\"v\":$API_VERSION,\"pin\":\"$PIN\",${DOC#*,}"
printf '%s' "$DOC_BODYPIN_BAD" > "$TMP/doc_bodypin_bad.json"
printf '%s' "$DOC_BODYPIN_OK"  > "$TMP/doc_bodypin_ok.json"

cat <<HDR

=============================================================================
 ERRATA creator smoke - $(date -u '+%Y-%m-%dT%H:%M:%SZ')
   target        $URL   (port $WEB_PORT)
   VARIANT       $VARIANT     <- the build flashed on the board, as reported by
                                the operator. Every result below is about THIS
                                artefact and no other (docs/budget.md 8).
   tree          $SKETCH/src
   expectations  api v$API_VERSION, fw $FW_VERSION, content $CONTENT_VERSION,
                 body cap $CS_BODY_MAX B, page $PAGE_LEN B,
                 lockout after $PIN_FAIL_MAX failures for $((PIN_LOCK_MS / 1000)) s,
                 portal idle budget ${IDLE_S}s
=============================================================================
HDR

# =============================================================================
#  PHASE 1 - THE SEVEN ROUTES (spec section 38)
# =============================================================================
phase "phase 1: the seven routes"

req GET / -
expect 200 "GET / answered"
if [ "$STATUS" = "200" ]; then
  got="$(wc -c < "$BODYF" | tr -d ' ')"
  if [ "$got" = "$PAGE_LEN" ]; then
    ok "GET / served $got B, exactly the blob this tree generated"
  else
    bad "GET / served $got B, this tree's index_html.h is $PAGE_LEN B -- the board is running different firmware, so nothing below is evidence about this tree"
  fi
fi

req GET /api/schema -
expect 200 "GET /api/schema answered (ungated: the document is compiled constants)"
expect_num v "$API_VERSION" "the served schema names this tree's CREATOR_API_VERSION"

req GET /api/state "$PIN"
expect 200 "GET /api/state answered with the PIN"
if [ "$STATUS" = "200" ]; then
  expect_num v "$API_VERSION" "/api/state agrees on the API version"
  expect_num content "$CONTENT_VERSION" "/api/state agrees on CONTENT_VERSION"
  expect_num body "$CS_BODY_MAX" "/api/state advertises this tree's CS_BODY_MAX"
  fw="$(json_str fw)"
  if [ "$fw" = "$FW_VERSION" ]; then ok "/api/state agrees on FW_VERSION ($fw)"
  else bad "/api/state says fw \"$fw\", this tree says \"$FW_VERSION\""; fi
  BOX_USED="$(sed -nE 's/.*"box"[[:space:]]*:[[:space:]]*\{[[:space:]]*"used"[[:space:]]*:[[:space:]]*([0-9]+).*/\1/p' "$BODYF" | head -1)"
  BOX_FREE="$(sed -nE 's/.*"box"[^}]*"free"[[:space:]]*:[[:space:]]*([0-9]+).*/\1/p' "$BODYF" | head -1)"
  CS_FREE="$(sed -nE 's/.*"cs"[^}]*"free"[[:space:]]*:[[:space:]]*([0-9]+).*/\1/p' "$BODYF" | head -1)"
  RO="$(json_num ro)"
  note "box used ${BOX_USED:-?} free ${BOX_FREE:-?}, creator slots free ${CS_FREE:-?}, read-only ${RO:-?}"
else
  BOX_FREE=""; BOX_USED=""; CS_FREE=""; RO=""
fi

req POST /api/ping "$PIN" -H 'Content-Length: 0'
expect 200 "POST /api/ping answered - the keep-alive, and the only route that moves the idle clock"

req POST /api/validate "$PIN" --data-binary "@$TMP/doc.json" -H 'Content-Type: application/json'
expect 200 "POST /api/validate accepted the document the page would send"
if [ "$STATUS" = "200" ]; then
  note "device priced it at $(json_num pct)% of the section 36 budget (stat $(json_num stat), atk $(json_num atk))"
fi

# POST /api/time. THE PHONE SETS THE CLOCK AND THIS IS THAT ROUTE: gt_set_epoch()
# owns the judgement, including the rollback tolerance, so a host clock that is
# behind the device's by more than GT_ROLLBACK_TOLERANCE_S is answered 409 - a
# real device answer and a real thing to look at, not something to swallow.
NOW_EPOCH="$(date -u +%s)"
printf '{"v":%s,"epoch":%s}' "$API_VERSION" "$NOW_EPOCH" > "$TMP/time.json"
req POST /api/time "$PIN" --data-binary "@$TMP/time.json" -H 'Content-Type: application/json'
expect 200 "POST /api/time set the device clock from this host (CAL_PHONE)"
if [ "$STATUS" = "409" ]; then
  note "409 means gt_set_epoch() refused it. Check this host's clock first; the"
  note "other three reasons are in hardware/gametime.h."
fi

if [ "$DO_WRITE" -eq 1 ]; then
  if [ "${RO:-1}" = "1" ]; then
    bad "POST /api/bug SKIPPED: the device reported a read-only save (ro:1). It refuses the write by design; fix the save first"
  elif [ "${BOX_USED:-0}" = "0" ]; then
    bad "POST /api/bug SKIPPED: the Box is empty, so the device answers 409 nopet by design. Hatch the starter first"
  elif [ "${BOX_FREE:-0}" = "0" ] || [ "${CS_FREE:-0}" = "0" ]; then
    bad "POST /api/bug SKIPPED: no free Box slot (${BOX_FREE:-?}) or creator slot (${CS_FREE:-?}). Release one first"
  else
    req POST /api/bug "$PIN" --data-binary "@$TMP/doc.json" -H 'Content-Type: application/json'
    expect 200 "POST /api/bug created a Bug"
    if [ "$STATUS" = "200" ]; then
      note "box slot $(json_num slot), creator slot $(json_num cs), species $(json_num species), id $(json_num id)"
      note "THIS BUG IS REAL AND IS CALLED SMOKE. Release it from the BOX when you are done."
    fi
  fi
else
  note "POST /api/bug skipped (--no-write): six of the seven routes were driven"
fi

# The catch-all, which is a route in every sense that matters: it is registered
# LAST with the same body hook, and it is what stops POST /anything with a
# nine-digit Content-Length walking the core's malloc growth loop.
req GET /api/bugs -
if [ "$STATUS" = "404" ] || [ "$STATUS" = "302" ]; then
  ok "an unknown path is answered by the registered catch-all (HTTP $STATUS), not by a missing handler"
else
  bad "an unknown path answered HTTP $STATUS -- expected 404, or 302 if the Host header made it a captive-portal probe"
fi

# =============================================================================
#  PHASE 2 - THE RAW BODY CAP (networking/creator_body.cpp)
# =============================================================================
phase "phase 2: the raw-body cap"

# Over the cap and under the drain limit: the body is drained so a polite answer
# is still possible, and the answer is 413 with the cap in it. NO PIN IS SENT
# and that is deliberate - creator_server.cpp answers the body BEFORE the PIN,
# so this is the one refusal an unauthenticated client is entitled to.
head -c "$((CS_BODY_MAX + 64))" /dev/zero 2>/dev/null | tr '\0' 'x' > "$TMP/big.bin"
if [ ! -s "$TMP/big.bin" ]; then
  bad "could not build the oversize body (no /dev/zero?)"
else
  req POST /api/validate - --data-binary "@$TMP/big.bin" -H 'Content-Type: application/json'
  expect 413 "a $((CS_BODY_MAX + 64)) B body is answered 413, over the $CS_BODY_MAX B cap"
  expect_num max "$CS_BODY_MAX" "the 413 names the cap this tree compiled"

  # THE SAME BODY TO AN UNMATCHED PATH, AND THIS IS THE ONLY PROBE THAT CAN
  # TELL A REGISTERED CATCH-ALL FROM A MISSING HANDLER.
  #
  # The GET in phase 1 proves an unknown path is ANSWERED; it says nothing
  # about which code answered it, because the core answers an unmatched GET
  # 404 all by itself. The difference only shows on a POST WITH A BODY:
  #
  #   catch-all registered HTTP_ANY with cs_body_hook  -> canRaw() true, the
  #     body is decided by creator_body.cpp before a byte is buffered -> 413
  #   catch-all missing, or narrowed to HTTP_GET       -> _currentHandler is
  #     null (Parsing.cpp:182), the raw path is skipped, the WHOLE declared
  #     body goes through readBytesWithTimeout()'s malloc growth loop, and the
  #     core then answers 404 - the audit's section 12 hole, wide open, with a
  #     status code that looks fine
  #
  # SO A 404 HERE IS A FAILURE, AND IT IS THE FAILURE THAT MATTERS. The gate in
  # tools/check.sh holds the source half; nothing but a socket holds this half.
  # /api/bugs is the script's designated unmatched path (it is excluded from
  # the route-set comparison by name for exactly this reason).
  req POST /api/bugs - --data-binary "@$TMP/big.bin" -H 'Content-Type: application/json'
  if [ "$STATUS" = "413" ]; then
    ok "an oversize POST to an UNMATCHED path is answered 413 by the registered catch-all - the body was bounded before it was read"
  else
    bad "an oversize POST to an unmatched path answered HTTP $STATUS $(body_head) -- expected 413. A 404 here means no handler matched, the body walked readBytesWithTimeout()'s unbounded growth loop, and the catch-all is gone or is no longer HTTP_ANY"
  fi

  req GET /api/schema -
  expect 200 "and the device is still answering after it - an unmatched POST did not exhaust the heap"
fi

# Over the DRAIN limit the socket is closed instead: there is nothing left to
# write a 413 to, and that trade is the reason CS_BODY_DRAIN_MAX exists. curl
# sees an empty reply (exit 52) or a reset, never a status line.
head -c "$((CS_BODY_DRAIN_MAX + 1024))" /dev/zero 2>/dev/null | tr '\0' 'x' > "$TMP/huge.bin"
raw POST /api/validate - --data-binary "@$TMP/huge.bin" -H 'Content-Type: application/json'
if [ "$CURL_EXIT" -ne 0 ] || [ "$STATUS" = "000" ]; then
  ok "a $((CS_BODY_DRAIN_MAX + 1024)) B body closes the socket (curl exit $CURL_EXIT) - above CS_BODY_DRAIN_MAX the connection is not worth the seconds"
else
  bad "a body above CS_BODY_DRAIN_MAX was answered HTTP $STATUS -- creator_body.cpp says the socket is stopped instead"
fi

# THE OTHER TWO BANDS creator_body.cpp DECIDES, both of which look like a
# legitimate request and are not. They are here because the P8-C3 box lists them
# as owner curl steps, and a script that claims to supersede that list has to
# cover it.
#
#  411: Content-Length 0 - which the core also reports for an ABSENT header and
#       for a chunked body (Parsing.cpp:112, :175-177). In all three the read
#       loop never runs and the handler is offered a legitimate-looking EMPTY
#       upload; answering 400 would tell the page its JSON was wrong when its
#       framing was.
#  415: a multipart Content-Type. The shared raw hook is ALSO the core's upload
#       hook, and on that path WebServer::raw() dereferences a null unique_ptr -
#       so this request is a crash any client could ask for if the guard were
#       ever removed. THE ASSERTION IS PARTLY "THE DEVICE IS STILL THERE": the
#       next request after it must still be answered.
req POST /api/validate - -H 'Content-Length: 0' -H 'Content-Type: application/json'
expect 411 "an empty body is answered 411 (framing), not 400 (content)"

# *** A WELL-FORMED MULTIPART, SINCE THE FINAL REVIEW. ***
# This used to send `--data-binary 'x'` with a multipart Content-Type, and that
# request CANNOT be answered 415 by a correct board - so the probe reported a
# failure on a healthy device and froze it for about ten seconds first.
#
# Why: a Content-Type starting "multipart/" makes the core set isForm, so the
# bounded raw branch is skipped and _parseForm() runs. Its first two calls are
# readStringUntil('\r') and readStringUntil('\n') against _timeout =
# HTTP_MAX_SEND_WAIT (5000 ms), and Stream::timedRead() spins with NO YIELD - so
# a body of "x" that never terminates a line burns the full 5 s each, inside
# handleClient(), inside app_loop(). The line "x" then fails the boundary
# compare, _parseForm returns false, _parseRequest returns false, and
# WebServer.cpp NEVER REACHES _handleRequest(): the client is dropped with no
# response at all. curl reports 000, the assertion fails, and the operator - who
# has just watched the panel and both buttons freeze - is sent into
# creator_body.cpp and CS_BODY_MAX, neither of which is on this path.
#
# `--form` emits a conforming body with a filename= disposition, which is the
# only shape that reaches _currentHandler->upload() -> cs_body_hook() -> the
# guard -> CB_IDLE -> cs_body_answered() -> 415. That is the assertion as
# written and the only form of it that touches the guard at all.
req POST /api/validate - --form 'f=@/dev/null'
expect 415 "a multipart POST is answered 415 - the raw hook refuses the upload path it shares"

req GET /api/schema -
expect 200 "and the device is still answering afterwards - the multipart path did not take it down"

# =============================================================================
#  PHASE 3 - THE PIN (spec section 34, networking/creator_gate.cpp)
# =============================================================================
if [ "$DO_LOCK" -eq 1 ]; then
phase "phase 3: the PIN, the five failures and the lockout"

# A KNOWN STATE FIRST. cg_verify() clears fail_count on success, and the gate
# comes back from a reboot with ConfigV2.pin_fail_count restored - so a device
# that was left armed by an earlier run would answer "locked" to the very first
# probe below and the phase would report a lockout it did not cause.
req GET /api/state "$PIN"
expect 200 "an authorised request first, so the failure counter starts at zero"

# EVERY REFUSAL BELOW IS A COUNTED FAILURE, INCLUDING THIS ONE. web_pin_ok()
# reads an absent X-Pin as the empty string and hands it to the same
# cg_verify(), so "no PIN" walks the same path as a wrong one and increments the
# same counter. Each probe is therefore followed by an authorised request that
# clears it, or the count arriving at the lockout run below would be wrong -
# which is exactly the arithmetic a smoke script gets silently wrong.
req GET /api/state -
expect 403 "no X-Pin header at all is refused"
if [ "$(json_str err)" = "pin" ]; then ok "and an absent PIN is answered as a wrong one, not as a different case"
else bad "an absent PIN answered \"$(json_str err)\", expected \"pin\""; fi
req GET /api/state "$PIN"; expect 200 "the counter is cleared again"

req GET /api/state 'abcd'
expect 403 "a malformed PIN is refused"
if [ "$(json_str err)" = "pin" ]; then ok "with the same answer a wrong one gets - the reason a login form does not say which half was wrong"
else bad "a malformed PIN answered \"$(json_str err)\", expected \"pin\""; fi
req GET /api/state "$PIN"; expect 200 "the counter is cleared again"

# =============================================================================
#  THE PIN ON THE ROUTES THAT WRITE, WHICH IS THE HALF NOTHING COVERED.
#
#  ADDED AT THE PHASE-8 EXIT, and the reason is worth stating in the file: every
#  PIN probe above drives GET /api/state, and every unauthenticated POST
#  anywhere else in this script carries a DELIBERATELY BROKEN body (413, 411,
#  415) that creator_server.cpp answers BEFORE it consults the PIN. So until
#  these probes existed, no check in the repository - host binary, browser
#  harness or bench script - ever sent an unauthenticated POST with a VALID
#  body, and section 67's "PIN required" box pointed at an instrument that
#  would have reported all green against a firmware where POST /api/bug
#  needed no PIN at all. Measured at the exit: making pin_after_body() return
#  true unconditionally built clean (release 1,326,274 / 59,396, 0 warnings),
#  passed ALL PASS 49/49 and passed every networking gate in tools/check.sh.
#
#  networking/creator_server.cpp is never compiled by tests/Makefile - there is
#  no socket in any host binary - so THIS IS THE ONLY INSTRUMENT THESE FOUR
#  ASSERTIONS HAVE ANYWHERE.
#
#  Each refusal below is a counted failure (web_pin_ok() reads an absent header
#  as the empty string and hands it to the same cg_verify()), so each is
#  followed by an authorised request that clears the counter - the same
#  discipline the lockout run below depends on.

# (a) THE WRITE ROUTE, UNAUTHENTICATED, WITH A DOCUMENT THE DEVICE WOULD OTHERWISE
#     ACCEPT. The status is half the assertion; the other half is that the Box
#     did not move. json_num reads the FIRST "used" in the response, which is
#     box.used - the "cs" object follows it.
req GET /api/state "$PIN"
BOX_BEFORE="$(json_num used)"
req POST /api/bug - --data-binary "@$TMP/doc.json" -H 'Content-Type: application/json'
expect 403 "POST /api/bug with a VALID document and no X-Pin is refused"
if [ "$(json_str err)" = "pin" ]; then ok "and it is refused for the PIN, not for the document"
else bad "the unauthenticated write answered \"$(json_str err)\", expected \"pin\""; fi
req GET /api/state "$PIN"
if [ "$(json_num used)" = "$BOX_BEFORE" ]; then
  ok "and NOTHING was written - the Box still holds $BOX_BEFORE Bug(s)"
else
  bad "the Box moved from $BOX_BEFORE to $(json_num used) on a request that was refused 403 -- the refusal came after the write"
fi

# (b) THE CLOCK ROUTE WITH A WRONG HEADER. POST /api/time is the route an
#     attacker most wants: networking/creator_gate.h is written on the
#     assumption that this value is hostile, and the PIN is what stands there.
req POST /api/time "$WRONG" --data-binary "@$TMP/time.json" -H 'Content-Type: application/json'
expect 403 "POST /api/time with a WRONG X-Pin is refused - the clock is not settable by anyone in radio range"
if [ "$(json_str err)" = "pin" ]; then ok "and for the PIN"
else bad "the clock route answered \"$(json_str err)\", expected \"pin\""; fi
req GET /api/state "$PIN"; expect 200 "the counter is cleared again"

# (c) THE BODY-CARRIED PIN, REFUSED. No X-Pin header at all, so the document's
#     own "pin" field is what pin_after_body() gates on - the ONE path in
#     creator_server.cpp that nothing else in this tree has ever executed.
req POST /api/validate - --data-binary "@$TMP/doc_bodypin_bad.json" -H 'Content-Type: application/json'
expect 403 "a WRONG PIN carried in the body is refused, exactly as a wrong header is"
if [ "$(json_str err)" = "pin" ]; then ok "and by the same gate, with the same answer"
else bad "the body-carried wrong PIN answered \"$(json_str err)\", expected \"pin\""; fi
req GET /api/state "$PIN"; expect 200 "the counter is cleared again"

# (d) AND ACCEPTED, which is the half that stops (c) passing against a firmware
#     that simply refuses every body PIN. A one-sided assertion here would be
#     this project's recurring defect written into its own bench script.
req POST /api/validate - --data-binary "@$TMP/doc_bodypin_ok.json" -H 'Content-Type: application/json'
expect 200 "and the CORRECT PIN carried in the body is accepted - the body half of spec section 38's gate really works, in both directions"

# THE LOCKOUT RUN. Exactly CREATOR_PIN_FAIL_MAX consecutive wrong PINs, each
# answered "pin"; the LAST of them arms the lockout, and it is the attempt AFTER
# it that is refused as locked. Getting "locked" early would mean the counter
# was not where this script thought it was.
i=0
while [ "$i" -lt "$PIN_FAIL_MAX" ]; do
  i=$((i + 1))
  req GET /api/state "$WRONG"
  if [ "$STATUS" = "403" ] && [ "$(json_str err)" = "pin" ]; then
    ok "wrong PIN $i of $PIN_FAIL_MAX answered \"pin\""
  else
    bad "wrong PIN $i answered HTTP $STATUS $(body_head) -- expected \"pin\"; the ${PIN_FAIL_MAX}th failure ARMS the lockout, it is not refused by it"
  fi
done

req GET /api/state "$WRONG"
if [ "$STATUS" = "403" ] && [ "$(json_str err)" = "locked" ]; then
  ok "attempt $((PIN_FAIL_MAX + 1)) is refused as LOCKED, $(json_num s) s left"
else
  bad "attempt $((PIN_FAIL_MAX + 1)) answered HTTP $STATUS $(body_head) -- expected the lockout"
fi

# THE LOCKOUT REFUSES THE CORRECT PIN TOO, and that is the property rather than
# an inconvenience: a gate that let the right PIN through while locked would let
# an attacker who guessed it through. The attempt is deliberately not counted,
# so this does not extend the wait.
req GET /api/state "$PIN"
if [ "$STATUS" = "403" ] && [ "$(json_str err)" = "locked" ]; then
  ok "the lockout refuses the CORRECT PIN as well"
else
  bad "the correct PIN answered HTTP $STATUS while locked -- the lockout is not a lockout"
fi

# Wait it out, and leave the device unarmed. The gate allows exactly ONE attempt
# after the deadline and re-locks on a failure, so this attempt must be right.
WAIT_S=$(( PIN_LOCK_MS / 1000 + 3 ))
note "waiting ${WAIT_S}s for CREATOR_PIN_LOCK_MS to expire (this also clears ConfigV2.pin_fail_count, so the device is not left armed)"
sleep "$WAIT_S"
req GET /api/state "$PIN"
expect 200 "after the lockout expires the correct PIN is accepted again"
req GET /api/state "$PIN"
expect 200 "and the failure counter really was cleared - the next request is not treated as a sixth failure"
else
phase "phase 3 skipped (--no-lock): the PIN and the lockout were NOT observed"
fi

# =============================================================================
#  PHASE 4 - THE IDLE TIMEOUT (spec sections 34 and 40, decision D7)
# =============================================================================
if [ "$DO_IDLE" -eq 1 ]; then
phase "phase 4: the portal tears itself down after ${IDLE_S}s of UNAUTHENTICATED traffic"
note "This phase takes about $(( (IDLE_S + 60) / 60 )) minutes and sends an UNAUTHORISED request every 15 s."
note "Only cg_verify() success moves last_seen_ms, so this traffic must NOT keep the"
note "portal alive: if it does, anyone in radio range owns the device's radio."

START="$(date +%s)"
LAST_OK=0
DIED_AT=""
DEADLINE=$(( IDLE_S + 60 ))
while :; do
  T=$(( $(date +%s) - START ))
  [ "$T" -le "$DEADLINE" ] || break
  raw GET /api/schema -
  if [ "$STATUS" = "200" ]; then
    LAST_OK="$T"
  elif [ "$STATUS" = "429" ]; then
    :                                  # throttled, not dead; the next poll tells
  else
    DIED_AT="$T"
    note "the portal stopped answering at t=${T}s (HTTP $STATUS, curl exit $CURL_EXIT)"
    break
  fi
  sleep 15
done

# Two claims, and the phase needs BOTH: a portal that dies at once satisfies
# "it shuts down" and is a broken feature, and a portal that never dies
# satisfies "it stayed up long enough" and is spec section 40's own
# counter-example. EARLY is one poll interval short of the budget, floored at
# two thirds of it so a short --idle-s cannot make the first claim vacuous.
EARLY=$(( IDLE_S - 60 ))
[ "$EARLY" -ge $(( IDLE_S * 2 / 3 )) ] || EARLY=$(( IDLE_S * 2 / 3 ))
if [ "$LAST_OK" -ge "$EARLY" ]; then
  ok "the portal was still serving at t=${LAST_OK}s, past ${EARLY}s - it does not die early"
else
  bad "the portal stopped serving at t=${DIED_AT:-?}s, well before the ${IDLE_S}s budget (last good answer t=${LAST_OK}s) -- something other than the idle timer took it down"
fi
if [ -n "$DIED_AT" ]; then
  ok "the portal was gone by t=${DIED_AT}s, and EVERY request in this phase was UNAUTHENTICATED - unauthenticated traffic cannot hold the access point up"
  note "CONFIRM ON THE DEVICE: the screen has left CREATOR and the Wi-Fi indicator is off."
else
  bad "the portal was still answering ${DEADLINE}s after the last authorised request, past its ${IDLE_S}s budget -- either the idle timer is not running or unauthenticated traffic is extending it, and spec section 40 forbids both"
fi
else
phase "phase 4 skipped (--no-idle): the inactivity timeout was NOT observed"
fi

# =============================================================================
printf '\n=============================================================================\n'
if [ "$FAILS" -eq 0 ]; then
  printf ' CREATOR SMOKE OK  %d/%d  variant=%s  target=%s\n' "$CHECKS" "$CHECKS" "$VARIANT" "$URL"
  printf ' This result is about the %s build and about no other.\n' "$VARIANT"
  printf '=============================================================================\n'
  exit 0
fi
printf ' CREATOR SMOKE FAILED  %d of %d checks  variant=%s  target=%s\n' "$FAILS" "$CHECKS" "$VARIANT" "$URL"
printf '=============================================================================\n'
exit 1
