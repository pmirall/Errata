// =============================================================================
//  NOTTAMAGOCHI - config.h
//  Pins, timings, feature flags, size caps, version strings. DATA ONLY.
//  No code, no allocation, no includes beyond <stdint.h>.
//
//  Identifiers/comments: English (except the USER BLOCK, in Spanish on purpose).
//  User-facing text lives in strings_es.h. Never put a Spanish literal here
//  except inside the USER CONFIGURATION BLOCK below.
// =============================================================================
#ifndef NT_CONFIG_H
#define NT_CONFIG_H

#include <stdint.h>

// #############################################################################
// ##                                                                         ##
// ##   BLOQUE DE CONFIGURACIÓN DE USUARIO                                    ##
// ##   ------------------------------------                                  ##
// ##   Esto es LO ÚNICO que tienes que tocar antes de programar el bicho.     ##
// ##   Cambia lo que haya entre comillas "" y guarda el archivo.              ##
// ##   Si dejas algo vacío ("") el aparato se apaña solo. No pasa nada.       ##
// ##                                                                         ##
// #############################################################################

// --- WiFi -------------------------------------------------------------------
// Nombre de tu red WiFi (SSID). OJO: solo funcionan redes de 2,4 GHz.
// Si lo dejas vacío, el bicho monta su propia red llamada NOTTAMAGOCHI-XXXX
// para que te conectes con el móvil y le digas la contraseña desde el navegador.
#define CFG_WIFI_SSID       ""

// Contraseña de tu WiFi. Déjala vacía si tu red es abierta.
#define CFG_WIFI_PASS       ""

// --- Tu bicho ---------------------------------------------------------------
// Cómo se llama. Máximo 12 letras. Si lo dejas vacío se inventa un nombre él
// solo a partir de su genética (y será siempre el mismo para esa estirpe).
#define CFG_PET_NAME        ""

// --- Telegram (opcional, para que te dé la brasa al móvil) ------------------
// 1) En Telegram habla con @BotFather, manda /newbot y copia aquí el token.
// 2) Escríbele algo a tu bot y abre en el navegador
//    https://api.telegram.org/bot<TU_TOKEN>/getUpdates
//    Copia el número que aparece en   "chat":{"id": AQUÍ }
// Si dejas cualquiera de los dos vacío, Telegram se queda apagado.
#define CFG_TG_TOKEN        ""
#define CFG_TG_CHAT         ""

// --- Dónde vives (para el tiempo) -------------------------------------------
// Latitud y longitud en grados, con punto decimal, entre comillas.
// Ejemplo Barcelona: "41.3874" y "2.1686".
// Si los dejas vacíos, el bicho lo adivina solo a partir de tu conexión.
#define CFG_LATITUDE        ""
#define CFG_LONGITUDE       ""

// --- Hora -------------------------------------------------------------------
// Zona horaria en formato POSIX. La de España peninsular ya está puesta.
// Canarias:  "WET0WEST,M3.5.0/1,M10.5.0"    México DF: "CST6"
// Argentina: "ART3"                         Chile:     "CLT4CLST,M9.1.6/24,M4.1.6/24"
#define CFG_TZ_STRING       "CET-1CEST,M3.5.0,M10.5.0/3"

// --- Pantalla ---------------------------------------------------------------
// Déjalo en 0. Si al encender ves la imagen desplazada 2 píxeles hacia un lado
// o una columna de basura en el borde, tu pantalla es una SH1106: pon un 1.
#define DISPLAY_IS_SH1106   0

// --- Interruptores generales ------------------------------------------------
// Pon un 0 en cualquiera de estos si quieres apagar esa parte del juego.
#define FEATURE_WEATHER     1   // El tiempo real afecta al humor del bicho
#define FEATURE_TELEGRAM    1   // Mensajes pasivo-agresivos al móvil
#define FEATURE_BLE         1   // Emparejarse con otros bichos por Bluetooth
#define FEATURE_WEB         1   // Página web + QR + minijuegos en el móvil
#define GOD_MODE_ENABLED    1   // Menú de trampas escondido (para probar cosas)

// #############################################################################
// ##   FIN DEL BLOQUE DE USUARIO. De aquí para abajo, mejor no toques nada.  ##
// #############################################################################


// =============================================================================
// 1. IDENTITY / VERSION
// =============================================================================
#define FW_NAME                 "Pebblebol"
#define FW_VERSION              "0.2.0-dev"
#define FW_BUILD_PROTO          1           // BLE + save wire protocol version
#define WEB_API_SCHEMA_VER      1           // /api/state "v" field

// =============================================================================
// 2. GPIO ASSIGNMENT  (BRIEF 1.1 - binding)
//    GPIO2 and GPIO9 are strapping pins: never wire a button to them.
//    GPIO8 = LED_BUILTIN. GPIO11-19 not broken out.
// =============================================================================
#define PIN_SDA                 8    // tu cableado actual del TinyLLM
#define PIN_SCL                 9
#define PIN_BTN_L               10
#define PIN_BTN_R               2
#define PIN_LED                 5    // OBLIGATORIO cambiarlo: 8 ya es SDA
#define PIN_VBAT_ADC            0           // reserved, not populated in v1

// LED polarity. Clone-to-clone difference: validate on the physical board.
// LED_ON/LED_OFF are the numeric values of Arduino's LOW/HIGH so that this
// header stays includable from a host compiler with no Arduino.h.
#define LED_ACTIVE_LOW          1
#define LED_ON                  0           // == LOW
#define LED_OFF                 1           // == HIGH

#define BTN_ACTIVE_LEVEL        0           // == LOW (buttons are pull-up)

// =============================================================================
// 3. DISPLAY
// =============================================================================
#define OLED_W                  128
#define OLED_H                  64
#define OLED_I2C_ADDR_7BIT      0x3C
#define OLED_I2C_ADDR_8BIT      0x78        // u8g2 setI2CAddress() wants this form
#define OLED_BUS_CLOCK_HZ       400000UL    // u8g2 default; 800k only after HW validation
#define OLED_CONTRAST_DEFAULT   140
#define OLED_CONTRAST_DIM       40

#define STATUS_BAR_H            9           // top strip, px
#define AFFORDANCE_BAR_H        8           // bottom strip, px
#define SPRITE_AREA_Y           (STATUS_BAR_H)
#define SPRITE_AREA_H           (OLED_H - STATUS_BAR_H - AFFORDANCE_BAR_H)

// -----------------------------------------------------------------------------
// THE HUD KEEP-OUT.  draw_home() stamps two OPAQUE 12x12 badges on top of the
// whole pet layer: the weather badge on the left and the mood face on the
// right. On a 1-bit panel whoever draws second wins, so the actor is kept out
// of their columns altogether (PETFX_STAGE_L / PETFX_STAGE_R, petfx.h) and
// ui.cpp pushes emotes out of them too.
//
// These five numbers are the ONE definition of that keep-out, and everything
// that has to agree about it now derives from them: the two px_spr() calls that
// place the badges, emote_x(), and the static_assert in petfx.h that pins the
// stage to them. Before this the badges were placed with raw literals and the
// stage asserts only checked the stage against itself, so moving a badge,
// widening one to 14 px or adding a third compiled clean and silently re-created
// the whole "the badge ate the pet" class of defects. Now it breaks the BUILD.
//   left  HUD : columns 0 .. UI_HUD_L_END-1        (badge drawn at 2..13)
//   right HUD : columns UI_HUD_R_BEGIN .. OLED_W-1 (badge drawn at 115..126)
// -----------------------------------------------------------------------------
#define UI_HUD_BADGE_W          12          // both badges are 12x12 icon art
#define UI_HUD_L_X              2           // weather badge origin
#define UI_HUD_L_END            (UI_HUD_L_X + UI_HUD_BADGE_W)      // 14
#define UI_HUD_R_X              (OLED_W - 1 - UI_HUD_BADGE_W)      // 115
#define UI_HUD_R_BEGIN          UI_HUD_R_X                         // 115

// =============================================================================
// 4. RENDER SCHEDULER
// =============================================================================
#define FPS_NORMAL              20
#define FPS_LOW                 4            // energy < FPS_LOW_ENERGY_PCT, or web client active
#define FPS_MEMORIAL            1
#define FPS_LOW_ENERGY_PCT      15
#define RENDER_WEB_BUSY_MS      10000UL      // drop to FPS_LOW this long after a web hit
#define FRAME_BUDGET_US         50000UL      // 20 fps; sendBuffer is ~24 ms of it

// =============================================================================
// 5. INPUT / GESTURES  (GAME_DESIGN 8.1)
// =============================================================================
#define DEBOUNCE_MS             25
#define DOUBLE_TAP_WINDOW_MS    280
#define HOLD_MS                 600
#define TAP_MAX_MS              599
#define LONG_BOTH_MS            1500
#define BOTH_SYNC_MS            80
#define REPEAT_START_MS         600
#define REPEAT_RATE_MS          220
#define INPUT_POLL_MS           5

// Special holds
#define GOD_ENTER_HOLD_MS       5000UL       // BOTH on S6 STATUS_B
#define MEMORIAL_BURY_HOLD_MS   3000UL       // HOLD_R on S12
#define EGG_RUB_TAPS            10           // alternating L/R taps to hatch early
#define EGG_RUB_WINDOW_MS       20000UL

// =============================================================================
// 6. UI NAVIGATION
// =============================================================================
#define UI_AUTORETURN_MS        20000UL      // every screen except S0/S4/S12
#define UI_COUNTDOWN_MS         5000UL       // 3 px bar shows for the last 5 s
#define UI_MODAL_HELP_MS        3000UL       // BOTH on a list = 1 line of help
#define UI_TOAST_MS             1800UL
#define UI_EVOLVE_FREEZE_MS     4000UL
#define UI_HEX_DUMP_MS          5000UL       // DBL_R on S5/S6
#define UI_ALERT_MIN_MS         1200UL
#define MENU_ITEM_COUNT         8

// Death staging (GAME_DESIGN 9.2) - milliseconds from T+0
#define DEATH_HEARTBEAT_MS      12000UL
#define DEATH_COLLAPSE_MS       1200UL
#define DEATH_BLACK_MS          3000UL
#define DEATH_TEXT_START_MS     16200UL
#define DEATH_TEXT_LINE_MS      1200UL
#define DEATH_INPUT_LOCK_MS     22000UL
#define DEATH_HEARTBEAT_BPM_HI  60
#define DEATH_BUZZ_MS           40

// Memorial -> lineage -> egg
#define MEMORIAL_LINEAGE_MS     4000UL
#define MEMORIAL_EGG_FADE_MS    3000UL
#define EGG_MOURNING_LOCK_S     600UL        // 10 min, unskippable
#define EGG_AUTOHATCH_S         900UL        // 15 min

// --- birth staging. The mirror of the death script above: 4.5 s in 7 phases --
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

// Cumulative marks. One clock, one comparison per phase, exactly like DEATH_*.
#define HATCH_T_CRACK   (HATCH_WOBBLE_MS)
#define HATCH_T_FLASH   (HATCH_T_CRACK  + HATCH_CRACK_MS)
#define HATCH_T_SHARDS  (HATCH_T_FLASH  + HATCH_FLASH_MS)
#define HATCH_T_GROW    (HATCH_T_SHARDS + HATCH_SHARDS_MS)
#define HATCH_T_LOOK    (HATCH_T_GROW   + HATCH_GROW_MS)
#define HATCH_T_NAME    (HATCH_T_LOOK   + HATCH_LOOK_MS)
#define HATCH_TOTAL_MS  (HATCH_T_NAME   + HATCH_NAME_MS)

// =============================================================================
// 7. SIMULATION  (GAME_DESIGN 1.x)  -- all integer, milli-points
//    Stats are int32 milli-points 0..100000. Rates are in milli-points/hour.
//    per_tick = rate_mph / 3600, remainder carried in PetSave.stat_rem[].
// =============================================================================
#define SIM_TICK_HZ             1
#define STAT_MILLI_MAX          100000L
#define STAT_MILLI_MIN          0L
#define SEC_PER_HOUR            3600L

// Base decay rates, ADULT, awake, neutral weather, all genes = 8.
// Negative = decays. Units: milli-points per hour.
#define RATE_HUNGER_MPH         (-12000L)
#define RATE_HAPPINESS_MPH      (-8000L)
#define RATE_ENERGY_AWAKE_MPH   (-9000L)
#define RATE_ENERGY_ASLEEP_MPH  (+20000L)
#define RATE_HYGIENE_MPH        (-2000L)
#define RATE_HYGIENE_POOP_MPH   (-6000L)     // additional, per poop on screen
#define RATE_BOND_MPH           (-800L)
#define RATE_DISCIPLINE_MPH     (-250L)
#define RATE_WEIGHT_DG_MPH      (-3500L)     // milli-decigram per hour (=-0.35 g/h)
#define RATE_HEALTH_REGEN_MPH   (+4000L)
#define HEALTH_REGEN_MIN_PCT    55           // all four core stats >= 55 and !sick

// Health damage per hour (milli-points), additive while the condition holds.
#define DMG_HUNGER_ZERO_MPH     (3000L)
#define DMG_HYGIENE_ZERO_MPH    (2500L)
#define DMG_ENERGY_ZERO_MPH     (1500L)
#define DMG_HAPPINESS_ZERO_MPH  (1000L)
#define DMG_SICK_MPH            (1500L)
#define DMG_OBESE_MPH           (800L)
#define DMG_HEAT_MPH            (1000L)      // apparent temp > HEAT_DANGER_DC
#define OBESE_WEIGHT_DG         700
#define HEAT_DANGER_DC          350          // 35.0 C in deci-celsius

// Stage multipliers, x1000 (integer). Index by Stage.
#define STAGE_MULT_EGG          0
#define STAGE_MULT_BABY         2200
#define STAGE_MULT_CHILD        1500
#define STAGE_MULT_TEEN         1150
#define STAGE_MULT_ADULT        1000
#define STAGE_MULT_SENIOR       850
#define STAGE_MULT_DEAD         0

// Contextual multipliers, x1000
#define MULT_SLEEP              350          // hunger/happiness/hygiene while asleep
#define MULT_LONELY             1500         // after LONELY_AFTER_S with no interaction
#define MULT_OFFLINE_DECAY      550          // BRIEF/GAME_DESIGN 5.2 offline x0.55
#define MULT_LIGHT_ON_SLEEP     300          // energy regen while light is on
#define MULT_ONE                1000
#define LONELY_AFTER_S          21600UL      // 6 h

// Gene -> multiplier: mult_x1000 = GENE_MULT_BASE + v * GENE_MULT_STEP
#define GENE_MULT_BASE          600
#define GENE_MULT_STEP          60
#define GENE_SOC_BASE           700
#define GENE_SOC_STEP           40
#define GENE_HARDY_BASE         1300
#define GENE_HARDY_STEP         (-40)

// Sleep window (local hours). Honoured online and offline.
#define SLEEP_HOUR_START        23
#define SLEEP_HOUR_END          7

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
#define SICK_OVERFEED_PPH       100          // 3 overfeeds -> +10 %/h for 2 h
#define SICK_OVERFEED_WINDOW_S  7200UL
#define OVERFEED_TRIGGER        3
#define INBRED_SICK_MULT        1250         // x1.25 for life

// Care quality
#define CQ_START                500
#define CQ_MIN                  0
#define CQ_MAX                  1000
#define CQ_PERIOD_S             600          // evaluated every 10 min
#define CQ_GOOD_STATS_PCT       40
#define CQ_D_GOOD               (+1)
#define CQ_D_ZEROSTAT           (-1)
#define CQ_D_CARE_MISS          (-3)
#define CQ_D_SICK_EPISODE       (-25)
#define CQ_D_MINIGAME           (+2)
#define CQ_D_WISH_OK            (+25)
#define CQ_D_WISH_FAIL          (-10)
#define CQ_D_ABANDONO           (-40)
#define CQ_D_ABANDONO_GRAVE     (-100)
#define CQ_MINIGAME_DAILY_CAP   40
#define CQ_GOOD_DAILY_CAP       144

// care_miss
#define CARE_MISS_ALERT_PCT     25           // a stat crossing below this raises an alert
#define CARE_MISS_GRACE_S       720UL        // 12 min to address it
#define CARE_MISS_MIN_GAP_S     1800UL       // max 1 per 30 min

// Action deltas, in whole points (converted to milli by the sim).
#define ACT_MEAL_HUNGER         30
#define ACT_MEAL_WEIGHT_DG      10
#define ACT_MEAL_CQ             2
#define ACT_MEAL_REFUSE_PCT     90           // refused above this satiety
#define ACT_SNACK_HUNGER        10
#define ACT_SNACK_HAPPINESS     12
#define ACT_SNACK_WEIGHT_DG     25
#define ACT_SNACK_OVERFEED_PCT  70
#define ACT_CLEAN_HYGIENE       25
#define ACT_MED_HAPPINESS       (-10)
#define ACT_MED_SECOND_DOSE_PCT 30           // if health < 25
#define ACT_MED_SECOND_HEALTH   25
#define ACT_PLAY_HAPPINESS_MAX  6
#define ACT_PLAY_ENERGY         (-8)
#define ACT_PLAY_WEIGHT_DG      (-10)
#define ACT_PLAY_MIN_ENERGY_PCT 12
#define ACT_SCOLD_WINDOW_S      30
#define ACT_SCOLD_DISCIPLINE    15
#define ACT_SCOLD_HAPPINESS     (-8)
#define ACT_SCOLD_CQ            5
#define ACT_SCOLD_UNJUST_BOND   (-10)
#define ACT_SCOLD_UNJUST_HAP    (-12)
#define ACT_SCOLD_UNJUST_CQ     (-8)
#define ACT_PET_BOND            4
#define ACT_PET_HAPPINESS       3
#define ACT_FORCE_FEED_BOND     (-2)         // after 3 forced refusals
#define ACT_FORCE_FEED_LIMIT    3
#define WEIGHT_DG_MIN           50
#define WEIGHT_DG_MAX           990

// Diminishing returns on minigame happiness, permille, rolling 3 h window.
#define PLAY_DECAY_WINDOW_S     10800UL
#define PLAY_DECAY_0            1000
#define PLAY_DECAY_1            700
#define PLAY_DECAY_2            450
#define PLAY_DECAY_3            250
#define PLAY_DECAY_4            100
#define PLAY_DECAY_5            0
#define PLAY_DECAY_STEPS        6

// Mimo (PET) decay per hour: 4/3/2/1/0
#define PET_DECAY_STEPS         5

// =============================================================================
// 8. LIFE STAGES  (GAME_DESIGN 2.1) - seconds since hatch
// =============================================================================
#define AGE_EGG_S               900UL        // 15 min before hatch
#define AGE_BABY_S              0UL
#define AGE_CHILD_S             13500UL      // 3.75 h
#define AGE_TEEN_S              72000UL      // 20 h
#define AGE_ADULT_S             172800UL     // 48 h
#define AGE_SENIOR_S            604800UL     // 168 h = 7 d
#define STAGE_CHECK_PERIOD_S    60

// natural_death_h = 216 + (CQ-500)/6, clamped [192,288]
#define DEATH_NATURAL_BASE_H    216
#define DEATH_NATURAL_CQ_DIV    6
#define DEATH_NATURAL_MIN_H     192
#define DEATH_NATURAL_MAX_H     288
#define SENIOR_MAXHEALTH_MIN    40
#define SENIOR_REGEN_MULT       500          // x0.5
#define ACCIDENT_PPM_PER_DAY    1500         // 0.15 %/day, only if CQ < 350
#define ACCIDENT_CQ_MAX         350

// Weekly / scheduled events
#define EVENT_VISITA_H          72
#define EVENT_VISITA_SOC_MIN    10
#define EVENT_VISITA_HAPPINESS  20
#define EVENT_STORM_H           144
#define EVENT_STORM_DUR_S       7200UL
#define EVENT_STORM_HEALTH      (-8)
#define EVENT_STORM_PETS_NEEDED 3
#define EVENT_BIRTHDAY_H        168
#define EVENT_BIRTHDAY_HAPPY    40

// Daily wish
#define WISH_HOUR_MIN           9
#define WISH_HOUR_MAX           21
#define WISH_WINDOW_S           14400UL      // 4 h
#define WISH_HAPPINESS          15
#define WISH_BOND               6

// =============================================================================
// 9. ABSENCE  (GAME_DESIGN 5.x)
// =============================================================================
#define OFFLINE_STEP_S          1800UL
#define OFFLINE_MAX_STEPS       2000
#define ABSENCE_MAX_S           34560000UL   // 400 days; beyond = nonsense clock

// PH3 #1/#2. The single "is this value a real wall clock?" threshold, shared by
// every module that has to tell a Unix epoch apart from an uptime counter.
// 2017-01-01T00:00:00Z - the same constant gametime.cpp uses internally
// (GT_EPOCH_SANE_MIN) and the same heuristic as the core's getLocalTime().
// Before SNTP lands, time()/gt_now() return seconds-since-boot, i.e. a value
// near zero, so the test is unambiguous by ~47 years.
#define NT_EPOCH_SANE_MIN       1483228800UL // 2017-01-01T00:00:00Z
#define ABSENCE_CORTA_S         3600UL       // 1 h
#define ABSENCE_LARGA_S         21600UL      // 6 h
#define ABSENCE_ABANDONO_S      86400UL      // 24 h
#define ABSENCE_GRAVE_S         259200UL     // 72 h
#define ABSENCE_NEGLECT_DEATH_S 86400UL      // death inside an absence >= 24 h => ABANDONO

// Tier penalties, whole points, applied AFTER the offline sim, clamped at 0.
#define ABS_CORTA_BOND          (-3)
#define ABS_CORTA_HAP           (-5)
#define ABS_CORTA_HEA           0
#define ABS_LARGA_BOND          (-12)
#define ABS_LARGA_HAP           (-20)
#define ABS_LARGA_HEA           (-5)
#define ABS_ABANDONO_BOND       (-35)
#define ABS_ABANDONO_HAP        (-45)
#define ABS_ABANDONO_HEA        (-20)
#define ABS_GRAVE_BOND          (-70)
#define ABS_GRAVE_HAP           (-80)
#define ABS_GRAVE_HEA           (-45)
#define ABS_ABANDONO_MISSES     3
#define ABS_GRAVE_MISSES        8

// Sulk / refusal timers
#define SULK_LARGA_S            30
#define SULK_ABANDONO_S         120
#define SULK_GRAVE_S            300
#define SULK_PET_FORGIVE_S      20           // each mimo removes this much
#define BOND_ABSENCE_DECAY_MULT 2000         // x2.0 while the AUSENCIA flag is set

// Cold egg
#define EGG_COLD_AFTER_S        259200UL     // 72 h
#define EGG_COLD_HEALTH_PCT     90

// SNTP / unknown clock
#define SNTP_GIVEUP_S           30
#define SNTP_RETRY_S            300
#define SNTP_WAIT_MS            10000UL      // getLocalTime budget; SNTP start delay is 5 s
#define SNTP_RESYNC_S           10800UL      // 3 h
#define NTP_SERVER_1            "pool.ntp.org"
#define NTP_SERVER_2            "time.google.com"
#define NTP_SERVER_3            "time.cloudflare.com"

// =============================================================================
// 10. PERSISTENCE  (BRIEF 6.4) - NVS keys are max 15 chars
// =============================================================================
#define NVS_NS                  "notta"
#define NVS_KEY_LASTSEEN        "t"
#define NVS_KEY_SAVE            "save"
#define NVS_KEY_ANC             "anc"
#define NVS_KEY_CFG             "cfg"
#define NVS_KEY_EGG             "egg"        // pending BLE-mating egg (see nt_types.h)
#define NVS_KEY_CANARY          "ok"         // store_selftest()
#define NVS_KEY_GAIN            "gl"         // hourly-gain ledger (storage.h GainSave)

// PH4 6.1. The hourly gain budget is the anti-farm ceiling (section 12 below),
// and until now it lived only in RAM, so a power cut either restored it for
// free (farmable) or zeroed it (a 19 % pet told "esta lleno" for 29 minutes).
// It is persisted as its own 20 B blob rather than inside PetSave: PetSave is
// exactly 128 B, static_assert-ed with five offsetof guards, and reserved[] has
// two bytes left - changing that wire format would refuse every existing save.
// A separate key costs one extra NVS entry and rides the "save" write, so it
// adds no write cadence of its own.
#define NT_GAIN_MAGIC           0x474Cu      // 'G','L' little-endian
#define NT_GAIN_VERSION         1

#define SAVE_LASTSEEN_PERIOD_S  60UL         // do NOT lower: NVS wear
#define SAVE_FULL_PERIOD_S      300UL
#define ANCESTOR_MAX            16           // ring buffer, 16 * 12 = 192 B

// =============================================================================
// 11. RADIO / NETWORK  (BRIEF 1.3, NET_APIS)
// =============================================================================
#define WIFI_CONNECT_TIMEOUT_MS 15000UL
#define WIFI_RETRY_PERIOD_S     120UL
#define WIFI_MAX_FAILS          3
#define AP_SSID_PREFIX          "NOTTAMAGOCHI-"
#define AP_IP_A                 192
#define AP_IP_B                 168
#define AP_IP_C                 4
#define AP_IP_D                 1
#define MDNS_HOSTNAME           "nottamagochi"

#define HTTP_CONNECT_TIMEOUT_MS 8000         // HTTPClient::setConnectTimeout - MILLISECONDS
#define HTTP_READ_TIMEOUT_MS    8000         // HTTPClient::setTimeout      - MILLISECONDS (uint16!)
#define TLS_HANDSHAKE_TIMEOUT_S 10           // NetworkClientSecure         - SECONDS
#define TLS_MIN_MAXALLOC_HEAP   (48 * 1024)  // hard gate before any TLS attempt
#define RADIO_SETTLE_MS         250

// Weather (Open-Meteo, plain HTTP)
#define WX_HOST                 "api.open-meteo.com"
#define WX_PATH                 "/v1/forecast"
#define WX_FIELDS               "temperature_2m,apparent_temperature,is_day,precipitation,weather_code,wind_speed_10m"
#define WX_POLL_S               1800UL       // 30 min
#define WX_POLL_JITTER_S        240UL        // +/- 4 min
#define WX_STALE_S              21600UL      // data older than 6 h = WX_UNKNOWN
#define WX_MAX_FAILS            3
#define WX_BODY_MAX             1024
#define GEO_HOST                "ip-api.com"
#define GEO_PATH                "/line/?fields=status,city,lat,lon,timezone"
#define GEO_BODY_MAX            192

// Telegram (TLS mandatory)
#define TG_HOST                 "api.telegram.org"
#define TG_MAX_PER_DAY          4            // P1+P2 only
#define TG_MIN_GAP_S            6000UL       // 100 min
#define TG_QUIET_START_MIN      (23*60+30)   // 23:30 local
#define TG_QUIET_END_MIN        (8*60)       // 08:00 local
#define TG_DIGEST_MIN           (8*60+5)     // 08:05 local
#define TG_SAME_ID_COOLDOWN_S   172800UL     // 48 h
#define TG_TRIGGER_COOLDOWN_S   21600UL      // 6 h
#define TG_RETRY_1_S            60UL
#define TG_RETRY_2_S            300UL
#define TG_RETRY_3_S            1500UL
#define TG_QUEUE_CAP            8
#define TG_DROP_P2_OLDER_S      21600UL      // on boot
#define TG_TEXT_MAX             320
#define TG_URL_MAX              640

// BLE (connectionless, advertisement only)
#define BLE_COMPANY_ID          0xFFFF
#define BLE_MSD_LEN             22           // company_id(2) + frame_type(1) + payload(19)
#define BLE_FRAME_BEACON        0x01
#define BLE_FRAME_MATE_OFFER    0x02
#define BLE_FRAME_MATE_ACK      0x03
#define BLE_ADV_MIN_RAW         0x0640       // RAW 0.625 ms units = 1000 ms
#define BLE_ADV_MAX_RAW         0x0C80       // RAW 0.625 ms units = 2000 ms
#define BLE_SCAN_INTERVAL_MS    1000         // BLEScan::setInterval - MILLISECONDS
#define BLE_SCAN_WINDOW_MS      100          // BLEScan::setWindow   - MILLISECONDS
#define BLE_SCAN_DURATION_S     6
#define BLE_RSSI_MIN            (-70)
#define BLE_PEER_CAP            8
#define BLE_PEER_TTL_S          60
#define BLE_SESSION_CAP         32           // deinit/init leaks ~672 B per cycle
#define BLE_MATE_COOLDOWN_S     86400UL      // 24 h per unit
#define BLE_MATE_FAIL_COOLDOWN_S 2700UL      // 45 min
#define BLE_MATE_MIN_ENERGY_PCT 30
#define BLE_MATE_P_MIN_PCT      25
#define BLE_MATE_P_MAX_PCT      90
#define BLE_MATE_ENERGY_COST    (-30)
#define BLE_MATE_WIN_HAPPINESS  25
#define BLE_MATE_LOSE_HAPPINESS (-15)
#define BLE_QUEUE_CAP           8            // onResult runs on the BTC task: post, never draw

// =============================================================================
// 12. WEB UI  (BRIEF 1.6 + amendment A2)
// =============================================================================
#define WEB_PORT                80
#define WEB_HTML_MAX            49152        // A2: 48 KB cap on index_html.h
#define WEB_JSON_BUF            320
#define WEB_PIN_MAX             10000        // rng_below(RNG_MISC, 10000)
#define WEB_CLIENT_ACTIVE_MS    10000UL
#define WEB_RATE_TOKENS         10
#define WEB_RATE_REFILL_PER_S   4
#define WEB_COST_READ           1
#define WEB_COST_MUTATE         2
#define WEB_ACTION_GLOBAL_CD_S  2
#define WEB_CD_FEED_S           20
#define WEB_CD_CLEAN_S          15
#define WEB_CD_SLEEP_S          10
#define WEB_CD_PLAY_S           25
#define WEB_CD_MED_S            30

// Browser minigames (amendment A1: IN SCOPE for v1)
#define MG_COUNT_WEB            3
#define MG1_DURATION_S          35
#define MG2_DURATION_S          30
#define MG3_DURATION_S          40
#define MG1_SCORE_MAX           200
#define MG2_SCORE_MAX           120
#define MG3_SCORE_MAX           160
#define MG_COOLDOWN_S           120
#define MG_TOKEN_GRACE_S        20           // max overrun past dur
#define MG_MIN_ELAPSED_PERMILLE 800          // >= 80 % of the real duration
// Score -> stat, fixed point: delta = (score * NUM) >> 8. No floats.
#define MG1_HUN_NUM             77           // ~ x0.30
#define MG1_HAP_NUM             15           // ~ x0.06
#define MG1_HUN_CAP             30
#define MG1_HAP_CAP             6
#define MG1_NRG_COST            (-4)
#define MG2_HYG_NUM             154          // ~ x0.60
#define MG2_HAP_NUM             20           // ~ x0.08
#define MG2_HYG_CAP             60
#define MG2_HAP_CAP             8
#define MG2_NRG_COST            (-3)
#define MG3_NRG_NUM             90           // ~ x0.35
#define MG3_HAP_NUM             26           // ~ x0.10
#define MG3_NRG_CAP             45
#define MG3_HAP_CAP             12
// Per-stat hourly gain budget (whole points). The real anti-farm ceiling.
#define GAIN_CAP_HUNGER_H       60
#define GAIN_CAP_HYGIENE_H      80
#define GAIN_CAP_ENERGY_H       90
#define GAIN_CAP_HAPPINESS_H    40

// =============================================================================
// 13. QR  (BRIEF 1.4)
// =============================================================================
#define QR_MIN_VERSION          1
#define QR_MAX_VERSION          4
#define QR_MAX_MODULES          33           // v4 = 17 + 4*4
#define QR_STRIDE_BYTES         5            // (33 + 7) >> 3
#define QR_BUF_BYTES            (QR_MAX_MODULES * QR_STRIDE_BYTES)   // 165
#define QR_MAX_PAYLOAD          32           // v2-L byte mode capacity we design for
#define QR_TEXT_MAX             40
#define QR_PX_PER_MODULE        2
#define QR_QUIET_MODULES        3
#define QR_BOX_X                0
#define QR_BOX_Y                1
#define QR_BOX_SIZE             62           // white box: 25 modules * 2 px + 2*3*2 px
#define QR_GF_POLY              0x11D
#define QR_ECC_LEVEL_L          0

// =============================================================================
// 14. SIZE CAPS / BUILD GATES  (BRIEF 5)
// =============================================================================
#define GATE_FLASH_MAX          2400000UL
#define GATE_GLOBALS_MAX        90000UL
#define NAME_MAX_LEN            12           // + NUL = 13
#define SSID_MAX_LEN            32
#define PASS_MAX_LEN            64
#define TG_TOKEN_MAX_LEN        47
#define TG_CHAT_MAX_LEN         16
#define TZ_MAX_LEN              39
#define COORD_MAX_LEN           11
#define CITY_MAX_LEN            23

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
#define GOD_CMD_COUNT           12
#define GOD_BAR_H               9

// =============================================================================
// 16. RTC RETENTION  (<= 64 B budget, RTC fast mem, lost on power loss)
// =============================================================================
#define RTC_NONCE_MAGIC         0xB1C0FEEDUL
#define RTC_STRUCT_MAX_BYTES    64

#endif // NT_CONFIG_H
