// =============================================================================
//  ERRATA - core/config.h
//  Pins, timings, feature flags, size caps, version strings. DATA ONLY.
//  No code, no allocation, no includes beyond <stdint.h>.
//
//  Identifiers and comments are English throughout. Every user-facing byte
//  lives in core/strings_es.h; never put a Spanish literal here.
// =============================================================================
#ifndef NT_CONFIG_H
#define NT_CONFIG_H

#include <stdint.h>

#include "version.h"   // FW_VERSION + the schema/protocol version numbers

// #############################################################################
// ##                                                                         ##
// ##   USER CONFIGURATION BLOCK                                              ##
// ##   -------------------------                                             ##
// ##   The only part of the firmware you have to touch before flashing.       ##
// ##   Change what sits between the "" quotes and save the file.              ##
// ##   Leaving a value empty ("") is fine: the device copes on its own.       ##
// ##                                                                         ##
// #############################################################################

// --- Wi-Fi ------------------------------------------------------------------
// THERE IS NOTHING TO FILL IN HERE ANY MORE, and that is the point (P5-C1).
// The device NEVER joins a Wi-Fi network. It listens for the networks around
// it - that is the exploration sensor - and it brings up its OWN network,
// ERRATA-XXXX, when you want to open the creator page from a phone.
// CFG_WIFI_SSID and CFG_WIFI_PASS used to live here; the code that would have
// used them is gone and tools/check.sh fails the build if it comes back.

// --- Your creature ----------------------------------------------------------
// What it is called. 12 characters at most. Left empty, it invents a name from
// its own genetics (always the same name for the same lineage).
#define CFG_PET_NAME        ""

// --- Time -------------------------------------------------------------------
// Time zone in POSIX form. Mainland Spain is already set.
// Canary Islands: "WET0WEST,M3.5.0/1,M10.5.0"   Mexico City: "CST6"
// Argentina:      "ART3"                        Chile:       "CLT4CLST,M9.1.6/24,M4.1.6/24"
#define CFG_TZ_STRING       "CET-1CEST,M3.5.0,M10.5.0/3"

// --- Display ----------------------------------------------------------------
// Leave this at 0. If the image comes up shifted two pixels sideways, or with a
// column of rubbish along the edge, your panel is an SH1106: set it to 1.
#define DISPLAY_IS_SH1106   0

// --- Master switches --------------------------------------------------------
// Set any of these to 0 to compile that part of the game out.
// FEATURE_BLE WAS HERE AND IS GONE (P8-C0). Decision D2 chose ESP-NOW for the
// peer link; BLE was kept one whole phase as the fallback and never became one.
// It cost 712 KB of flash and 23 KB of globals - three times the headroom left
// under GATE_RELEASE_GLOBALS_MAX - and phase 8 needs that room for the creator's
// HTTP server. THE BENCH TEST THAT WOULD HAVE PROVEN ESP-NOW FIRST WAS NOT RUN:
// see docs/decisions.md D2. Reverting the P8-C0 commit brings BLE back whole.
#define FEATURE_ESPNOW      1   // link two Erratas directly (trade, battle, breed)
#define FEATURE_WEB         1   // web page + QR
#define GOD_MODE_ENABLED    1   // hidden cheat menu (for testing)

// #############################################################################
// ##   END OF THE USER BLOCK. Below this line, better leave things alone.    ##
// #############################################################################


// =============================================================================
// 1. IDENTITY / VERSION
// =============================================================================
#define FW_NAME                 "Errata"
// FW_VERSION and the four wire/schema version numbers live in core/version.h
// (plan section 1.4), which is included above so every existing user of
// FW_VERSION keeps compiling with exactly one definition in the tree.

// =============================================================================
// 2. GPIO ASSIGNMENT
//    GPIO2 and GPIO9 are strapping pins: never wire a button to them.
//    GPIO8 = LED_BUILTIN. GPIO11-19 not broken out.
// =============================================================================
// DECISION D1 PENDING (docs/decisions.md). Nothing has ever run on hardware, so
// the five pins below stay at exactly the values the repository has always
// carried; the map is revisited once a board exists. The guards below spell out
// what a confirmed map has to satisfy. ER_PINS_CONFIRMED is deliberately NOT
// defined anywhere, so they are dormant until the decision closes.
#define PIN_SDA                 8    // the existing TinyLLM wiring
#define PIN_SCL                 9
#define PIN_BTN_L               10
#define PIN_BTN_R               2
#define PIN_LED                 5    // has to move: 8 is already SDA

// DECISION D8 IS OPEN (docs/decisions.md): the owner confirms the piezo GPIO
// when the sounder is soldered. 3 is the PROPOSAL - D8 lists 3, 4, 6 and 7 as
// the free non-strapping pins and keeps GPIO0 for the battery divider (D10) -
// and it is written down HERE, once. hardware/audio.cpp drives PIN_PIEZO and
// never a number, so closing D8 on 4, 6 or 7 is an edit to this line and to
// nothing else; tools/check.sh fails any other file that defines a PIN_ macro,
// so a second pin cannot be invented somewhere the owner would not look
// (spec section 68 r3). NOTHING here defines ER_PINS_CONFIRMED.
#define PIN_PIEZO               3    // D8 PROPOSED, not confirmed on hardware
// MOVED ABOVE THE GUARD BLOCK BY P6-C1, and it is a real fix rather than
// tidying: the D8 assert below names PIN_VBAT_ADC, and while this line sat
// after the `#endif` a build with ER_PINS_CONFIRMED defined did not fail on an
// assertion, it failed to COMPILE - "'PIN_VBAT_ADC' was not declared in this
// scope". Measured with `g++ -DER_PINS_CONFIRMED -c` over this header. Every
// pin the guards talk about is now declared before them.
#define PIN_VBAT_ADC            0           // reserved, not populated in v1

#ifdef ER_PINS_CONFIRMED
static_assert(PIN_SDA != PIN_LED, "D1: the LED cannot share a pin with I2C SDA");
static_assert(PIN_SCL != PIN_LED, "D1: the LED cannot share a pin with I2C SCL");
static_assert(PIN_SDA != PIN_SCL, "D1: SDA and SCL need two pins");
static_assert(PIN_BTN_L != PIN_BTN_R, "D1: the two buttons need two pins");
static_assert(PIN_BTN_L != 2 && PIN_BTN_L != 8 && PIN_BTN_L != 9 &&
              PIN_BTN_R != 2 && PIN_BTN_R != 8 && PIN_BTN_R != 9,
              "D1: no button on a strapping pin (GPIO2/8/9)");
// D8, dormant beside D1 and for the same reason: the piezo is a PWM output
// held low between effects, so sharing it with any other function is a short.
static_assert(PIN_PIEZO != PIN_SDA && PIN_PIEZO != PIN_SCL &&
              PIN_PIEZO != PIN_BTN_L && PIN_PIEZO != PIN_BTN_R &&
              PIN_PIEZO != PIN_LED && PIN_PIEZO != PIN_VBAT_ADC,
              "D8: the piezo needs a pin of its own");
static_assert(PIN_PIEZO != 2 && PIN_PIEZO != 8 && PIN_PIEZO != 9,
              "D8: no piezo on a strapping pin (GPIO2/8/9)");
#endif

// LED polarity. Clone-to-clone difference: validate on the physical board.
// LED_ON/LED_OFF are the numeric values of Arduino's LOW/HIGH so that this
// header stays includable from a host compiler with no Arduino.h.
#define LED_ON                  0           // == LOW
#define LED_OFF                 1           // == HIGH

#define BTN_ACTIVE_LEVEL        0           // == LOW (buttons are pull-up)

// =============================================================================
// 3. DISPLAY
// =============================================================================
#define OLED_W                  128
#define OLED_H                  64
#define OLED_I2C_ADDR_7BIT      0x3C
#define OLED_BUS_CLOCK_HZ       400000UL    // u8g2 default; 800k only after HW validation
#define OLED_CONTRAST_DEFAULT   140
#define OLED_CONTRAST_DIM       40

#define STATUS_BAR_H            9           // top strip, px
#define AFFORDANCE_BAR_H        8           // bottom strip, px
#define SPRITE_AREA_Y           (STATUS_BAR_H)
#define SPRITE_AREA_H           (OLED_H - STATUS_BAR_H - AFFORDANCE_BAR_H)

// -----------------------------------------------------------------------------
// THE HUD KEEP-OUT.  draw_home() stamps an OPAQUE 12x12 mood badge on top of
// the whole pet layer, on the right; the left band is reserved for the badge
// that goes there. On a 1-bit panel whoever draws second wins, so the actor is
// kept out of both bands altogether (PETFX_STAGE_L / PETFX_STAGE_R, petfx.h)
// and ui.cpp pushes emotes out of them too.
//
// These five numbers are the ONE definition of that keep-out, and everything
// that has to agree about it now derives from them: the px_spr() call that
// places the badge, emote_x(), and the static_assert in petfx.h that pins the
// stage to them. Before this the badges were placed with raw literals and the
// stage asserts only checked the stage against itself, so moving a badge,
// widening one to 14 px or adding a third compiled clean and silently re-created
// the whole "the badge ate the pet" class of defects. Now it breaks the BUILD.
//   left  HUD : columns 0 .. UI_HUD_L_END-1        (reserved, nothing drawn)
//   right HUD : columns UI_HUD_R_BEGIN .. OLED_W-1 (badge drawn at 115..126)
// -----------------------------------------------------------------------------
#define UI_HUD_BADGE_W          12          // both badges are 12x12 icon art
#define UI_HUD_L_X              2           // left badge origin
#define UI_HUD_L_END            (UI_HUD_L_X + UI_HUD_BADGE_W)      // 14
#define UI_HUD_R_X              (OLED_W - 1 - UI_HUD_BADGE_W)      // 115
#define UI_HUD_R_BEGIN          UI_HUD_R_X                         // 115

// =============================================================================
// 4. RENDER SCHEDULER
// =============================================================================
#define FPS_NORMAL              20
#define FPS_LOW                 4            // energy < FPS_LOW_ENERGY_PCT, or web client active
#define FPS_LOW_ENERGY_PCT      15
#define RENDER_WEB_BUSY_MS      10000UL      // drop to FPS_LOW this long after a web hit
#define FRAME_BUDGET_US         50000UL      // 20 fps; sendBuffer is ~24 ms of it
// How far a frame that blew the budget pushes the next deadline out, capped, so
// the renderer cannot run the loop at 100 % duty cycle. It was a bare 250 in
// ui/render.cpp until P10-C2 moved the arithmetic to core/perf.cpp, where a
// host binary can drive it - it had shipped since 8aff64f with no test at all.
#define FRAME_BACKOFF_MAX_MS    250UL

// --- spec section 46's two time budgets, and what they are NOT --------------
// PERF_PASS_BUDGET_US is section 46's "loop() iteration <= 100 ms". The figure
// core/perf.cpp records against it is the WORK of a pass, measured from the top
// of app_loop() to just BEFORE stage 7's yield: pwr_yield() naps deliberately
// for up to PWR_SLEEP_SLICE_MS, so a counter that spanned it would read
// 8,000,000 us on a healthy sleeping board and the budget would mean nothing.
// The placement is load-bearing and tools/check.sh gates it.
//
// NEITHER OF THESE IS CHECKED BY ANY HOST TEST OR BY tools/check.sh, and that
// is deliberate rather than unfinished: no host binary compiles ui/render.cpp
// or app/app.cpp, micros() does not exist here, and the ~24 ms sendBuffer()
// that dominates a frame is a property of a 400 kHz bus and a panel that are
// both absent. They are read on a board - docs/bench.md steps A1 and A2.
#define PERF_PASS_BUDGET_US     100000UL
// A span wider than this is longer than one micros() wrap can explain, so it is
// a bad stamp pair rather than a slow frame: core/perf.cpp discards it and
// COUNTS the discard, because a silently dropped sample is the phase-7 defect.
#define PERF_SANE_MAX_US        10000000UL

// =============================================================================
// 5. INPUT / GESTURES
// =============================================================================
#define DEBOUNCE_MS             25
#define HOLD_MS                 600
#define LONG_BOTH_MS            1500
#define BOTH_SYNC_MS            80
#define REPEAT_START_MS         600
#define REPEAT_RATE_MS          220
#define INPUT_POLL_MS           5

// Special holds
#define GOD_ENTER_HOLD_MS       5000UL       // BOTH on S6 STATUS_B
#define EGG_RUB_TAPS            10           // alternating L/R taps to hatch early
#define EGG_RUB_WINDOW_MS       20000UL

// =============================================================================
// 6b. POWER STATES (P6-C3, spec section 67 "Device sleeps correctly")
//     The idle ladder hardware/power.h implements. Every threshold is measured
//     from sm_idle_ms(), i.e. from the last gesture, and each rung is a
//     SUPERSET of the one above it: DIM only dims, IDLE also drops the panel
//     and the radio, SLEEP also stops the CPU between logic ticks.
// =============================================================================
#define PWR_DIM_MS              30000UL      // ACTIVE -> DIM
#define PWR_IDLE_MS             120000UL     // DIM    -> IDLE  (2 min)
#define PWR_SLEEP_MS            600000UL     // IDLE   -> SLEEP (10 min)
// The DIM rung's contrast. It is applied THROUGH ui.cpp's bright_service(),
// which is already the one owner of the contrast base (the user's brightness
// and the asleep-pet dim are the other two inputs), so the ramp duration is
// that function's asymmetric one - slow down, quick up - and not a fourth
// number here.
#define PWR_DIM_CONTRAST        40
#define PWR_IDLE_FPS            1            // the panel is off; this is the floor
// The longest the CPU may be stopped in one go, per rung. IDLE stays inside one
// logic tick so the pet keeps ticking at 1 Hz for a player who is still nearby;
// SLEEP coalesces up to PWR_SLEEP_SLICE_MS of them into a single wake, and
// app.cpp charges the whole slice to the simulation on the far side. Both are
// bounded by NT_TICK_MAX_OWED_S, which is what stops a slice from being
// resynchronised away instead of charged.
#define PWR_IDLE_SLICE_MS       1000UL
#define PWR_SLEEP_SLICE_MS      8000UL
// Whole seconds app_loop() will CHARGE to the simulation in one logic tick.
// Past it the 1 Hz scheduler resynchronises instead of firing a burst, because
// past it the gap is a stall (a long offline catch-up, an NVS write storm) and
// a stall is not elapsed game time. It lives beside the slices because it is
// the same number seen from the other side: a slice wider than this bound would
// be seconds the device really slept and the pet never received. app.cpp
// static_asserts the pair, and tests/test_power.cpp names it.
#define NT_TICK_MAX_OWED_S      16u

// =============================================================================
// 6. UI NAVIGATION
// =============================================================================
#define UI_AUTORETURN_MS        20000UL      // every screen except S0 and S4
#define UI_COUNTDOWN_MS        10000UL       // 3 px bar shows for the last 10 s
// The last stretch, where the bar BLINKS instead of only shrinking. A drain bar
// is a shape you have to be looking at; a change is something peripheral vision
// catches. The auto-return itself is unchanged at 20 s - what was wrong was that
// a 3 px bar appearing 5 s out is not a warning anybody sees while reading.
#define UI_COUNTDOWN_URGENT_MS  3000UL       // ... and blinks for the last 3 s
#define UI_COUNTDOWN_BLINK_MS    150UL       // half a blink period, ~3.3 Hz
#define UI_MODAL_HELP_MS        3000UL       // BOTH on a list = 1 line of help
#define UI_TOAST_MS             1800UL
#define UI_EVOLVE_FREEZE_MS     4000UL
// How long a declined evolution offer stays declined. The pending bit is NOT
// cleared by a "no" (spec section 27: no punishment, and a no now is not a no
// for ever), so this is the only thing standing between the player and the
// same question on the very next frame.
#define UI_EVOLVE_ASK_MS        300000UL     // 5 min
#define UI_HEX_DUMP_MS          5000UL       // DBL_R on S5/S6
#define UI_ALERT_MIN_MS         1200UL
#define MENU_ITEM_COUNT         7

// --- birth staging: 4.5 s in 7 phases ----------------------------------------
#define HATCH_WOBBLE_MS      1200UL
#define HATCH_CRACK_MS        600UL
#define HATCH_FLASH_MS         80UL
#define HATCH_SHARDS_MS       400UL
#define HATCH_GROW_MS         800UL
#define HATCH_LOOK_MS         800UL
#define HATCH_NAME_MS         600UL
#define HATCH_JOLT_GAP_MS     200UL   // the shell gives way in three steps
#define HATCH_JOLT_MS         180
#define HATCH_JOLT_PX           3
#define HATCH_WOBBLE_K     100000UL   // swings = el*el / K -> the rocking accelerates

// Cumulative marks. One clock, one comparison per phase.
#define HATCH_T_CRACK   (HATCH_WOBBLE_MS)
#define HATCH_T_FLASH   (HATCH_T_CRACK  + HATCH_CRACK_MS)
#define HATCH_T_SHARDS  (HATCH_T_FLASH  + HATCH_FLASH_MS)
#define HATCH_T_GROW    (HATCH_T_SHARDS + HATCH_SHARDS_MS)
#define HATCH_T_LOOK    (HATCH_T_GROW   + HATCH_GROW_MS)
#define HATCH_T_NAME    (HATCH_T_LOOK   + HATCH_LOOK_MS)
#define HATCH_TOTAL_MS  (HATCH_T_NAME   + HATCH_NAME_MS)

// =============================================================================
// 7. SIMULATION  -- all integer, milli-points
//    Stats are int32 milli-points 0..100000. Rates are in milli-points/hour.
//    per_tick = rate_mph / 3600, remainder carried in care_rem[].
// =============================================================================
#define STAT_MILLI_MAX          100000L
#define STAT_MILLI_MIN          0L
#define SEC_PER_HOUR            3600L

// The decay rates themselves, the health floor, the Box recovery rate, the
// action gains, the action cooldowns and the hourly gain caps live in
// src/data/balance.h (plan P3-C1): config.h keeps pins, timings, feature flags,
// size caps and versions, balance.h keeps everything a designer retunes.
//
// There are NO stage multipliers any more. Decay is a property of the species
// and of the genome, never of how old the creature is, so a fresh baby and a
// week-old veteran get exactly the same hours-scale rates (spec section 27).

// Contextual multipliers, x1000
#define MULT_SLEEP              350          // hunger/happiness/hygiene while asleep
#define MULT_LONELY             1500         // after LONELY_AFTER_S with no interaction
#define MULT_OFFLINE_DECAY      550          // offline x0.55
#define MULT_ONE                1000
#define LONELY_AFTER_S          21600UL      // 6 h

// Gene -> multiplier: mult_x1000 = GENE_MULT_BASE + v * GENE_MULT_STEP
#define GENE_MULT_BASE          600
#define GENE_MULT_STEP          60
#define GENE_SOC_BASE           700
#define GENE_SOC_STEP           40
#define GENE_HARDY_BASE         1300
#define GENE_HARDY_STEP         (-40)

// The sleep window is NOT a pair of fixed hours any more. P3-C2b drives it
// from the approximated daylight table in data/balance.h, read through
// game/daylight.h: bedtime is sunset + SLEEP_AFTER_DUSK_MIN and the creature
// wakes at sunrise, both interpolated by day of year.

// Poop
#define POOP_MAX                4
#define POOP_FIRST_MIN          90           // minutes after FEED_MEAL, / metabolism_mult
#define POOP_OFFLINE_MIN        120          // minutes per poop offline, / metabolism_mult
#define POOP_SUPPRESSED_SICK_PCT 12          // 5th poop suppressed -> +12 %/h sick risk

// Sickness roll, permille per hour before hardiness scaling
#define SICK_BASE_PPH           20           // 0.02
#define SICK_PER_POOP_PPH       50           // 0.05 each
#define SICK_HUNGRY_PPH         100          // hunger < 15
#define SICK_LOWHEALTH_PPH      80           // health < 50
#define SICK_ROLL_PERIOD_S      600          // every 10 min, p_hour/6
#define SICK_SUPPRESS_WINDOW_S  7200UL       // how long a suppressed poop counts
#define INBRED_SICK_MULT        1250         // x1.25 for life

// Care quality
#define CQ_START                500
#define CQ_MIN                  0
#define CQ_MAX                  1000
#define CQ_PERIOD_S             600          // evaluated every 10 min
#define CQ_GOOD_STATS_PCT       40
#define CQ_D_GOOD               (+1)
#define CQ_D_ZEROSTAT           (-1)
#define CQ_D_SICK_EPISODE       (-25)
#define CQ_D_MINIGAME           (+2)
#define CQ_D_WISH_OK            (+25)
#define CQ_D_WISH_FAIL          (-10)
#define CQ_MINIGAME_DAILY_CAP   40
#define CQ_GOOD_DAILY_CAP       144

// Alerts
#define ALERT_LOW_STAT_PCT      25           // a stat crossing below this raises an alert

// The action deltas and the play/mimo decay windows live in data/balance.h.

// =============================================================================
// 8. LIFE STAGES - seconds since hatch
// =============================================================================
#define AGE_EGG_S               900UL        // 15 min before hatch
#define AGE_BABY_S              0UL
#define AGE_CHILD_S             13500UL      // 3.75 h
#define AGE_TEEN_S              72000UL      // 20 h
#define AGE_ADULT_S             172800UL     // 48 h
#define AGE_SENIOR_S            604800UL     // 168 h = 7 d
#define STAGE_CHECK_PERIOD_S    60

#define SENIOR_MAXHEALTH_MIN    40
#define SENIOR_REGEN_MULT       500          // x0.5

// Weekly / scheduled events
#define EVENT_VISITA_H          72
#define EVENT_VISITA_SOC_MIN    10
#define EVENT_VISITA_HAPPINESS  20
#define EVENT_BIRTHDAY_H        168
#define EVENT_BIRTHDAY_HAPPY    40

// Daily wish
#define WISH_HOUR_MIN           9
#define WISH_HOUR_MAX           21
#define WISH_WINDOW_S           14400UL      // 4 h
#define WISH_HAPPINESS          15
#define WISH_BOND               6

// =============================================================================
// 9. ABSENCE
// =============================================================================
#define OFFLINE_STEP_S          1800UL
#define OFFLINE_MAX_STEPS       2000
#define ABSENCE_MAX_S           34560000UL   // 400 days; beyond = nonsense clock

// PH3 #1/#2. The single "is this value a real wall clock?" threshold, shared by
// every module that has to tell a Unix epoch apart from an uptime counter.
// 2017-01-01T00:00:00Z - the same constant gametime.cpp uses internally
// (GT_EPOCH_SANE_MIN) and the same heuristic as the core's getLocalTime().
// Before the clock is calibrated, time()/gt_now() return seconds-since-boot,
// i.e. a value near zero, so the test is unambiguous by ~47 years.
#define NT_EPOCH_SANE_MIN       1483228800UL // 2017-01-01T00:00:00Z

// Cold egg
#define EGG_COLD_AFTER_S        259200UL     // 72 h
#define EGG_COLD_HEALTH_PCT     90

// =============================================================================
// 10. PERSISTENCE - the write CADENCE only
//
// Every NVS key and both namespaces moved out of this file in P2-C9b: the key
// table is persistence/save_schema.h section 9 (one place, spelled once), the
// namespace "pbbl" and the "nvs2" partition label are hardware/kv_nvs.h, and
// the anti-farm ledger's 20 B layout is persistence/legacy_v1.h. What is left
// here is what belongs here - the two periods a designer might retune.
// =============================================================================
#define SAVE_LASTSEEN_PERIOD_S  60UL         // do NOT lower: NVS wear
#define SAVE_FULL_PERIOD_S      300UL
// The nvs2 checkpoint (D6). Daily, plus the events that change what a Bug
// IS rather than how it feels - a lost day of care is a bad afternoon, a lost
// evolution or trade is a different animal.
#define SAVE_CKPT_PERIOD_S      86400UL

// =============================================================================
// 11. RADIO / NETWORK
// =============================================================================
// THE STATION IS GONE (P5-C1). WIFI_CONNECT_TIMEOUT_MS, WIFI_RETRY_PERIOD_S
// and WIFI_MAX_FAILS timed an association this firmware can no longer make:
// networking/net.cpp has no association call site left and tools/check.sh
// counts them and fails the build at anything but zero - which is why this
// comment describes the call instead of naming it, exactly as net.h's banner
// does. (Spec section 68 r5.) The scan has its own section 47 budget
// below rather than borrowing the association's - they are different waits.
// D3's LAST OPEN PIECE, closed here in P8-C2. The persisted namespace became
// "pbbl" in P2-C9b and mDNS was deleted in P2-C5; the access-point name was
// scheduled for the chunk that builds the access point, which is this one.
// "ERRATA-" + 4 hex digits of the STA MAC = 14 chars, and net.h asserts
// SSID_MAX_LEN >= 15 for exactly that.
#define AP_SSID_PREFIX          "ERRATA-"

// -----------------------------------------------------------------------------
//  THE USER MANUAL'S ADDRESS, AND IT IS A COMPILE-TIME CONSTANT ON PURPOSE.
//
//  The MANUAL screen paints one QR and it never changes, so there is nothing to
//  fetch, nothing to configure and nothing that can be stale on a device that
//  has never been online. Point it somewhere else and the whole change is this
//  line plus a rebuild.
//
//  THE LENGTH IS THE DESIGN CONSTRAINT AND THE static_assert BELOW IS THE ONLY
//  THING STANDING BETWEEN A LONGER URL AND AN UNREADABLE SYMBOL. ui/qr.cpp's
//  version-2-L byte budget is 32 (data_cw - 2). At 33 bytes the encoder picks
//  version 3 - 29 modules - and 29 + 6 of quiet zone is 35, which in the 62 px
//  box this screen reserves is ONE pixel per module. A 35 px symbol on a 0.96"
//  panel is a coin flip, and nothing in the build would have said so: qrp_paint()
//  clamps the scale and draws it anyway.
//
//  WHY THE SCHEME IS MISSING. "https://pmirall.github.io/Pebblebol" is 35 bytes
//  and does not fit. Phone cameras resolve a bare host + path as a URL, so the
//  eight characters buy nothing a scanner needs. If the repository is ever
//  renamed to match the product, "https://pmirall.github.io/Errata" is 32 bytes
//  exactly and the scheme comes back for free.
#define MANUAL_URL              "pmirall.github.io/Pebblebol"
static_assert(sizeof(MANUAL_URL) - 1 <= 32,
              "MANUAL_URL is past QR version 2's 32-byte budget: the symbol would\n               fall to one pixel per module in the 62 px box and stop being\n               scannable on a 0.96\" panel. See ui/qr.cpp's QR_VER table.");

#define AP_IP_A                 192
#define AP_IP_B                 168
#define AP_IP_C                 4
#define AP_IP_D                 1

// --- SCAN-ONLY Wi-Fi (P5-C1, spec sections 20, 40, 44, 47) ------------------
// PASSIVE: the device listens for beacons and never sends a probe request, so
// nothing about it is broadcast while it explores.
#define WIFI_SCAN_DWELL_MS      300          // per channel; 13 channels ~ 3.9 s
// The software ceiling. The Arduino core's own scan timeout is 60,000 ms and
// its scanComplete() cannot tell "timed out" from "never triggered", so this is
// the clock that actually bounds the wait (networking/wifi_scanner.h).
#define WIFI_SCAN_TIMEOUT_MS    12000UL
// How many access points one job hands back. A WifiScanJob is 8 + 8*N bytes and
// is owned by whoever declares one, so this is a RAM decision: 16 covers a
// dense flat block and costs 136 B in the screen that holds the job.
#define WIFI_SCAN_MAX_RESULTS   16

// --- ENCOUNTER COOLDOWNS (P5-C2, spec section 21) ---------------------------
// "approximately 2 hours or greater per relevant network/event". One period,
// used by game/cooldowns.cpp for both the persisted table and the uncalibrated
// RAM fallback, so the two cannot be tuned apart.
#define ENCOUNTER_COOLDOWN_S    7200UL

// --- THE PEER LINK (P7-C1, decision D2 = ESP-NOW; spec sections 42, 43, 47) --
// The transport two devices in the same room talk over. It is a PHASE of the
// Wi-Fi stack and not a fourth radio: WiFi.mode(WIFI_STA) with no association
// is the whole residency ESP-NOW needs, so networking/net.h's RadioMode does
// not grow a value and its single-resident-stack invariant is untouched.
//
// THE CHANNEL IS FIXED AND IS SET ON EVERY BRING-UP. Two devices that never
// associate have no shared reason to be on the same channel, and the section 40
// scan sweeps 1..13 and leaves the radio wherever the last dwell ended - so
// "channel 1 by default" is not a property to rely on after a scan.
// WiFi.setChannel() refuses a channel outside the country range, and the
// default country is world-safe "01" (channels 1..11), so this must be 1..11:
// networking/transport_espnow.cpp static_asserts exactly that.
#define ER_LINK_CHANNEL         1
// The broadcast beacon cadence while the LINK screen is up.
#define LINK_BEACON_MS          500UL
// The peer table. Same three numbers the BLE table used - 8 entries, a 60 s
// TTL, a -70 dBm floor - because they describe a ROOM and not a radio.
#define LINK_PEER_CAP           8
#define LINK_PEER_TTL_S         60
#define LINK_RSSI_MIN           (-70)
// Beacons a device must send before it is offered to the player. One stray
// packet from a passing stranger is not a Errata in the room; at
// LINK_BEACON_MS this is 1.5 s of company.
#define LINK_PEER_HITS_MIN      3
// THE CEILING ON A DISCOVERY BROWSE (spec section 47), and it is load-bearing
// rather than belt and braces: hardware/power.h clamps the idle ladder at DIM
// while a screen holds a radio job, so a browse with no ceiling would hold the
// radio - and the beacon - on a device left face-up on a table. It MUST stay
// below PWR_IDLE_MS and networking/discovery.h static_asserts that it does.
#define LINK_JOB_TIMEOUT_MS     90000UL
// Received beacons drained per service() call. The ring is filled by the Wi-Fi
// task; this is what stops a crowded room from making one frame arbitrarily
// long. Anything still queued is drained on the next frame.
#define LINK_DRAIN_PER_SERVICE  8
// The two rings networking/transport_espnow.cpp posts into from the receive
// callback. Powers of two (networking/rxring.h masks with slots-1).
//
// 32 AND NOT 8, AND THE NUMBER WAS MEASURED RATHER THAN CHOSEN. The P7-C1
// survey reasoned that "a lockstep round has at most a handful of frames in
// flight per direction ... Eight is generous, not tuned", and that was WRONG.
// Running real battles between two real sessions over this exact ring
// (tests/test_link_transport.cpp), ONE session_poll() puts up to TEN frames in
// the peer's ring on a clean link - a poll drains everything queued and answers
// all of it - and up to NINETEEN at a 10 % per-frame drop, where the nine-rung
// ladder is re-sending while the peer is still catching up. Measured over 300
// varied battles per arm, counting only frames refused while BOTH endpoints
// were still draining - after one side closes it stops draining altogether and
// the other's unanswered GOODBYE can fill a ring of any size, which says
// nothing about the depth a live link needs.
//
// So eight slots (seven usable) refused three frames per clean battle, and
// SIXTEEN (fifteen usable) would still have overflowed on a lossy one. A
// refused frame is INDISTINGUISHABLE to networking/session.cpp from a frame the
// radio dropped, so the symptom on a bench is a link that is quietly slower
// than it should be, with no error anywhere - which is why this is sized from a
// measurement and not from the shape of the protocol. 31 usable slots cost
// 32 x 164 + 64 = 5,312 B of globals against 1,328, and
// `a_lossy_link_bursts_harder_than_a_clean_one_...` fails if the constant goes
// back down.
//
// The beacon ring is not under the same pressure: beacons are LINK_BEACON_MS
// apart from at most LINK_PEER_CAP devices, and 8 x 32 B = 256 B.
#define LINK_RX_SLOTS           32
#define LINK_BEACON_SLOTS       8

// =============================================================================
// 12. CREATOR SERVER
//    The web half is the server shell only: page budget, response buffer, PIN
//    and rate limiter. The Phase 8 creator API grows back on top of it.
// =============================================================================
#define WEB_PORT                80
#define WEB_HTML_MAX            49152        // A2: 48 KB cap on index_html.h
#define WEB_JSON_BUF            320
#define WEB_PIN_MAX             10000        // rng_below(RNG_MISC, 10000)
#define WEB_RATE_TOKENS         10
#define WEB_RATE_REFILL_PER_S   4
#define WEB_COST_READ           1
#define WEB_COST_MUTATE         2

// --- THE CREATOR PIN (spec section 34, P8-C1) -------------------------------
// The PIN is an AUTHORISATION gate against the person standing next to the
// device, not a cryptographic one, and networking/creator_gate.h says so at
// length. These three constants are what bound an online guess: without the
// lockout the rate limiter alone allows ~2 guesses/s and the whole 0..9999
// space falls in ~83 minutes.
#define CREATOR_PIN_DIGITS      4            // exactly four ASCII digits, always
#define CREATOR_PIN_FAIL_MAX    5            // consecutive failures before the lockout
#define CREATOR_PIN_LOCK_MS     60000UL      // how long one lockout holds

// --- THE PORTAL IDLE TIMEOUT (spec sections 34 and 40, decision D7) ---------
// ConfigV2.creator_idle_s carries the live value; 0 there means "never set",
// which resolves to the default rather than to an instant shutdown. The cap is
// a bound on a persisted number that survived a CRC, not on user input.
#define CREATOR_IDLE_S_DEFAULT  300          // D7
#define CREATOR_IDLE_S_MAX      3600

// --- THE RAW BODY CAP (spec section 38, audit section 12 "Body limits: none")
// EVERY BYTE BOUNDED HERE ARRIVES FROM OUTSIDE THE DEVICE. Without a raw
// handler the Arduino core reads a POST body through Parsing.cpp's
// readBytesWithTimeout(), whose ONLY bound on a malloc/realloc growth loop is
// the attacker's own Content-Length header - so "Content-Length: 4000000" plus
// a slow trickle walks the heap to exhaustion while handleClient() blocks
// loop(), i.e. the whole firmware. networking/creator_server.cpp registers
// every POST route with the 4-arg on() so the core uses its fixed
// HTTP_RAW_BUFLEN (1436 B, heap, freed per request) instead, and the chunks
// land in ONE fixed buffer of this size.
//
// WHY 2048 AND WHAT IT COSTS, MEASURED RATHER THAN GUESSED. The largest
// LEGITIMATE upload is 384 B: name 12 + type + base[4] + moves[4] + two 144-
// character sprite frames + the JSON syntax around them (the arithmetic is
// beside CreatorBody in networking/creator_body.h). 2048 is 5.3x that, and it
// is 2,048 B of .bss - 25 % of the release globals headroom this phase started
// with. It buys room for a page that grows a field without a firmware change.
// If a later chunk needs those bytes back, 1024 is still 2.7x the worst case
// and this is the one constant to move.
#define CS_BODY_MAX             2048

// The band between "refuse politely" and "hang up". A handler CANNOT stop the
// core's read loop (Parsing.cpp reads until totalSize == Content-Length), so a
// body above CS_BODY_MAX is DRAINED and answered 413 - which is only affordable
// while the drain is bounded. Above this the socket is closed from inside
// RAW_START, which makes the next readBytes() return 0 -> RAW_ABORTED -> the
// client is dropped with NO response. The split is deliberate and stated: a
// polite 413 up to 8 KB, an abrupt close above it, because a client declaring
// 100 MB would otherwise hold loop() for as long as it kept trickling.
#define CS_BODY_DRAIN_MAX       8192

// The creator server's own response scratch, separate from WEB_JSON_BUF so two
// files never write one buffer through calls that can nest. The widest response
// is GET /api/state at about 160 characters; 256 leaves room for a longer
// FW_VERSION without a silent truncation.
#define CS_OUT_BUF              256

// How long the CREATOR screen waits for the access point before giving up and
// going back (spec section 47: every radio wait has an exit). The screen is
// SF_STICKY - the 20 s navigation auto-return would otherwise tear the portal
// down while the user is still looking at their phone - so this is the timeout
// that replaces it on the failure path.
#define CREATOR_AP_WAIT_MS      20000UL

// The DEVICE action cooldowns (ACT_CD_*, named WEB_CD_* until P2-C5), the
// minigame cooldown and the hourly gain caps live in data/balance.h.

// =============================================================================
// 13. QR
// =============================================================================
#define QR_MIN_VERSION          1
#define QR_MAX_VERSION          4
#define QR_MAX_MODULES          33           // v4 = 17 + 4*4
#define QR_STRIDE_BYTES         5            // (33 + 7) >> 3
#define QR_BUF_BYTES            (QR_MAX_MODULES * QR_STRIDE_BYTES)   // 165
#define QR_TEXT_MAX             40
#define QR_PX_PER_MODULE        2
#define QR_QUIET_MODULES        3
#define QR_BOX_X                0
#define QR_BOX_Y                1
#define QR_BOX_SIZE             62           // white box: 25 modules * 2 px + 2*3*2 px
#define QR_GF_POLY              0x11D

// =============================================================================
// 14. SIZE CAPS / BUILD GATES
// =============================================================================
// The BASELINE caps: every feature on, including the legacy web UI, which V1 does
// not ship. This is a dev-build guard, not the number that matters. It used to
// carry BLE as well; P8-C0 deleted that, which is most of the step down you will
// see in this file's history and in docs/budget.md.
#define GATE_FLASH_MAX          2400000UL
#define GATE_GLOBALS_MAX        90000UL

// The RELEASE caps, enforced by tools/build_matrix.sh on the `release` variant
// (GOD_MODE_ENABLED=0) - the artefact that actually gets flashed.
// docs/budget.md measured release at 1,191,426 / 49,004 against a baseline of
// 1,915,654 / 72,676, so for four phases the gate was policing a build nobody
// would run. These are set at the projected phase-10 ending state plus margin:
// budget.md projects ~1,333,000 / ~57,200 once BLE goes.
#define GATE_RELEASE_FLASH_MAX    1600000UL
#define GATE_RELEASE_GLOBALS_MAX    65000UL
#define NAME_MAX_LEN            12           // + NUL = 13
// These two size the FROZEN Config.wifi_ssid / Config.wifi_pass fields (98 B of
// padding since P5-C1 deleted their only reader) and the access-point SSID
// buffer. The offsets of every Config field after them are static_asserted and
// pinned by tests/fixtures/config_v1.bin, so the bytes stay where they are; see
// core/nt_types.h.
#define SSID_MAX_LEN            32
#define PASS_MAX_LEN            64
#define TZ_MAX_LEN              39

// =============================================================================
// 15. GOD MODE
// =============================================================================
#define GOD_SCALE_0             1
#define GOD_SCALE_1             6
#define GOD_SCALE_2             60
#define GOD_SCALE_3             360
#define GOD_SCALE_4             3600
#define GOD_SCALE_COUNT         5
#define GOD_ABSENCE_COUNT       6            // 1 h, 6 h, 24 h, 72 h, 168 h, 720 h
#define GOD_CMD_COUNT           8            // P4-C4 added test_battle (spec 49)
#define GOD_BAR_H               9

// =============================================================================
// 16. RTC RETENTION  (<= 64 B budget, RTC fast mem, lost on power loss)
// =============================================================================
#define RTC_NONCE_MAGIC         0xB1C0FEEDUL
#define RTC_STRUCT_MAX_BYTES    64

// =============================================================================
// 17. AUDIO  (P6-C1; the pin is PIN_PIEZO in section 2, decision D8)
//     Hardware spec section 18: "use a short event queue". This is how short.
//     Four is what the ceremony needs at its widest - the shell gives way in
//     three jolts HATCH_JOLT_GAP_MS apart and each cue is shorter than the gap
//     - and the whole queue holds well under a second of sound, so a fifth
//     entry would be a beeper playing what the player did five events ago.
//     hardware/audio.cpp refuses the NEWEST play when it is full, which is
//     what keeps an effect that has started able to finish.
// =============================================================================
#define AUDIO_QUEUE_LEN         4
// LEDC duty resolution for the square wave. 10 bits is what the core's own
// ledcWriteTone() reconfigures the timer to, so asking for anything else here
// would be silently overwritten on the first note.
#define AUDIO_PWM_BITS          10

#endif // NT_CONFIG_H
