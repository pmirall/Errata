// =============================================================================
//  PEBBLEBOL - core/config.h
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
// The name (SSID) of your Wi-Fi network. Only 2.4 GHz networks work.
// If you leave it empty the device brings up its own network, NOTTAMAGOCHI-XXXX,
// so you can connect with a phone and hand it the password from the browser.
#define CFG_WIFI_SSID       ""

// Your Wi-Fi password. Leave it empty for an open network.
#define CFG_WIFI_PASS       ""

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
#define FEATURE_BLE         1   // see other creatures nearby over Bluetooth
#define FEATURE_WEB         1   // web page + QR
#define GOD_MODE_ENABLED    1   // hidden cheat menu (for testing)

// #############################################################################
// ##   END OF THE USER BLOCK. Below this line, better leave things alone.    ##
// #############################################################################


// =============================================================================
// 1. IDENTITY / VERSION
// =============================================================================
#define FW_NAME                 "Pebblebol"
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
// what a confirmed map has to satisfy. PB_PINS_CONFIRMED is deliberately NOT
// defined anywhere, so they are dormant until the decision closes.
#define PIN_SDA                 8    // the existing TinyLLM wiring
#define PIN_SCL                 9
#define PIN_BTN_L               10
#define PIN_BTN_R               2
#define PIN_LED                 5    // has to move: 8 is already SDA

#ifdef PB_PINS_CONFIRMED
static_assert(PIN_SDA != PIN_LED, "D1: the LED cannot share a pin with I2C SDA");
static_assert(PIN_SCL != PIN_LED, "D1: the LED cannot share a pin with I2C SCL");
static_assert(PIN_SDA != PIN_SCL, "D1: SDA and SCL need two pins");
static_assert(PIN_BTN_L != PIN_BTN_R, "D1: the two buttons need two pins");
static_assert(PIN_BTN_L != 2 && PIN_BTN_L != 8 && PIN_BTN_L != 9 &&
              PIN_BTN_R != 2 && PIN_BTN_R != 8 && PIN_BTN_R != 9,
              "D1: no button on a strapping pin (GPIO2/8/9)");
#endif

#define PIN_VBAT_ADC            0           // reserved, not populated in v1

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
// 6. UI NAVIGATION
// =============================================================================
#define UI_AUTORETURN_MS        20000UL      // every screen except S0 and S4
#define UI_COUNTDOWN_MS         5000UL       // 3 px bar shows for the last 5 s
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
// The nvs2 checkpoint (D6). Daily, plus the events that change what a Pebble
// IS rather than how it feels - a lost day of care is a bad afternoon, a lost
// evolution or trade is a different animal.
#define SAVE_CKPT_PERIOD_S      86400UL

// =============================================================================
// 11. RADIO / NETWORK
// =============================================================================
#define WIFI_CONNECT_TIMEOUT_MS 15000UL
#define WIFI_RETRY_PERIOD_S     120UL
#define WIFI_MAX_FAILS          3
#define AP_SSID_PREFIX          "NOTTAMAGOCHI-"
#define AP_IP_A                 192
#define AP_IP_B                 168
#define AP_IP_C                 4
#define AP_IP_D                 1

#define RADIO_SETTLE_MS         250

// BLE (connectionless, advertisement only)
#define BLE_COMPANY_ID          0xFFFF
#define BLE_MSD_LEN             22           // company_id(2) + frame_type(1) + payload(19)
#define BLE_FRAME_BEACON        0x01
#define BLE_ADV_MIN_RAW         0x0640       // RAW 0.625 ms units = 1000 ms
#define BLE_ADV_MAX_RAW         0x0C80       // RAW 0.625 ms units = 2000 ms
#define BLE_SCAN_INTERVAL_MS    1000         // BLEScan::setInterval - MILLISECONDS
#define BLE_SCAN_WINDOW_MS      100          // BLEScan::setWindow   - MILLISECONDS
#define BLE_SCAN_DURATION_S     6
#define BLE_RSSI_MIN            (-70)
#define BLE_PEER_CAP            8
#define BLE_PEER_TTL_S          60
#define BLE_SESSION_CAP         32           // deinit/init leaks ~672 B per cycle
#define BLE_QUEUE_CAP           8            // onResult runs on the BTC task: post, never draw

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
// The BASELINE caps: every feature on, including BLE and the legacy web UI, neither
// of which V1 ships. This is a dev-build guard, not the number that matters.
#define GATE_FLASH_MAX          2400000UL
#define GATE_GLOBALS_MAX        90000UL

// The RELEASE caps, enforced by tools/build_matrix.sh on the `release` variant
// (GOD_MODE_ENABLED=0 FEATURE_BLE=0) - the artefact that actually gets flashed.
// docs/budget.md measured release at 1,191,426 / 49,004 against a baseline of
// 1,915,654 / 72,676, so for four phases the gate was policing a build nobody
// would run. These are set at the projected phase-10 ending state plus margin:
// budget.md projects ~1,333,000 / ~57,200 once BLE goes.
#define GATE_RELEASE_FLASH_MAX    1600000UL
#define GATE_RELEASE_GLOBALS_MAX    65000UL
#define NAME_MAX_LEN            12           // + NUL = 13
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

#endif // NT_CONFIG_H
