// =============================================================================
//  NOTTAMAGOCHI - strings_es.h
//  EVERY user-facing string in the product. Nothing else may contain a Spanish
//  literal. UTF-8, no BOM.
//
//  RENDERING RULES (BRIEF 1.5, non-negotiable):
//    * Draw ONLY with u8g2.drawUTF8() / measure with u8g2.getUTF8Width().
//      drawStr() renders 'n' + tilde as two wrong glyphs. It is a review blocker.
//    * Use a _tf font (5x8_tf, 6x10_tf, t0_11b_tf). Those carry 191 glyphs =
//      ASCII + Latin-1. _tr fonts (4x6_tr) are ASCII-only: digits, IPs, hex.
//    * THEREFORE: every character in this file must exist in Latin-1.
//      Allowed beyond ASCII: a-e-i-o-u with acute, u-diaeresis, n-tilde,
//      inverted ? and !, degree sign, ordinals, and the middle dot (0xB7).
//      FORBIDDEN: em dash, ellipsis character, curly quotes, arrows, emoji.
//      Use "-" and "..." instead.
//
//  WIDTH BUDGET on the 128 px panel (drawUTF8 at x=0):
//      u8g2_font_4x6_tr    -> 32 chars    (ASCII only)
//      u8g2_font_5x8_tf    -> 25 chars    <- the default body font
//      u8g2_font_6x10_tf   -> 21 chars
//      u8g2_font_t0_11b_tf -> 21 chars
//  Each group below states its own budget.
//
//  ORDERING CONTRACT: the grouped blocks are index-parallel to the enums in
//  nt_types.h. Use the S_*() helpers rather than doing the arithmetic by hand.
// =============================================================================
#ifndef NT_STRINGS_ES_H
#define NT_STRINGS_ES_H

#include "nt_types.h"

enum StrId : uint16_t {
  // --- 0. filler -------------------------------------------------------------
  STR_EMPTY = 0,

  // --- 1. boot / system --------------------------- <= 25 chars @ 5x8 --------
  STR_APP_NAME,
  STR_BOOT_DIZZY,
  STR_BOOT_FIRST,

  // --- 2. main menu ring, S1 ---------------------- <= 10 chars @ t0_11b -----
  STR_MENU_FEED,
  STR_MENU_CLEAN,
  STR_MENU_PLAY,
  STR_MENU_HEALTH,
  STR_MENU_STATUS,
  STR_MENU_LIGHT,
  STR_MENU_SOCIAL,
  STR_MENU_SETTINGS,

  // --- 3. affordance strip + generic words -------- <= 9 chars @ 5x8 ---------
  STR_AF_NEXT,
  STR_AF_SEL,
  STR_AF_BACK,
  STR_AF_OK,
  STR_AF_CANCEL,
  STR_AF_PAUSE,
  STR_AF_PREV,
  STR_AF_FWD,
  STR_AF_MORE,
  STR_AF_BURY,
  STR_YES,
  STR_NO,
  STR_ON,
  STR_OFF,
  STR_ITEM_BACK,

  // --- 4. stat labels, parallel to StatId --------- <= 11 chars @ 5x8 --------
  STR_STAT_HUNGER,
  STR_STAT_HAPPINESS,
  STR_STAT_ENERGY,
  STR_STAT_HYGIENE,
  STR_STAT_HEALTH,
  STR_STAT_BOND,
  STR_STAT_DISCIPLINE,

  // --- 5. life stages, parallel to Stage ---------- <= 8 chars ---------------
  STR_STAGE_EGG,
  STR_STAGE_BABY,
  STR_STAGE_CHILD,
  STR_STAGE_TEEN,
  STR_STAGE_ADULT,
  STR_STAGE_SENIOR,
  STR_STAGE_DEAD,

  // --- 6. mood faces, parallel to Mood ------------ <= 9 chars ---------------
  STR_MOOD_MISERIA,
  STR_MOOD_TRISTE,
  STR_MOOD_NEUTRO,
  STR_MOOD_CONTENTO,
  STR_MOOD_FELIZ,
  STR_MOOD_EUFORICO,

  // --- 7. adult forms, parallel to AdultForm ------ <= 11 chars --------------
  STR_FORM_BOLOTA,
  STR_FORM_ZAMPASALTO,
  STR_FORM_BUHO,
  STR_FORM_PUNKI,
  STR_FORM_MOHO,
  STR_FORM_QUIMERA,

  // --- 8. species 0..15 (genome g0 bits 3:0) ------ <= 10 chars --------------
  STR_SPECIES_00, STR_SPECIES_01, STR_SPECIES_02, STR_SPECIES_03,
  STR_SPECIES_04, STR_SPECIES_05, STR_SPECIES_06, STR_SPECIES_07,
  STR_SPECIES_08, STR_SPECIES_09, STR_SPECIES_10, STR_SPECIES_11,
  STR_SPECIES_12, STR_SPECIES_13, STR_SPECIES_14, STR_SPECIES_15,

  // --- 9. patterns 0..15 (genome g0 bits 7:4) ----- <= 11 chars --------------
  STR_PATTERN_00, STR_PATTERN_01, STR_PATTERN_02, STR_PATTERN_03,
  STR_PATTERN_04, STR_PATTERN_05, STR_PATTERN_06, STR_PATTERN_07,
  STR_PATTERN_08, STR_PATTERN_09, STR_PATTERN_10, STR_PATTERN_11,
  STR_PATTERN_12, STR_PATTERN_13, STR_PATTERN_14, STR_PATTERN_15,

  // --- 10. temperament, parallel to Temperament --- <= 10 chars --------------
  STR_TEMPER_SOLAR,
  STR_TEMPER_TRANQUILO,
  STR_TEMPER_NERVIOSO,
  STR_TEMPER_GOTICO,

  // --- 12. death causes, parallel to DeathCause --- <= 11 chars --------------
  STR_CAUSE_NONE,
  STR_CAUSE_HUNGER,
  STR_CAUSE_FILTH,
  STR_CAUSE_ILLNESS,
  STR_CAUSE_SADNESS,
  STR_CAUSE_OLD_AGE,
  STR_CAUSE_NEGLECT,
  STR_CAUSE_ACCIDENT,

  // --- 13. action names, parallel to ActionId ----- <= 10 chars --------------
  STR_ACT_NONE,
  STR_ACT_FEED_MEAL,
  STR_ACT_FEED_SNACK,
  STR_ACT_CLEAN,
  STR_ACT_MEDICINE,
  STR_ACT_PLAY,
  STR_ACT_PET,
  STR_ACT_SCOLD,
  STR_ACT_LIGHT,
  STR_ACT_SLEEP,

  // --- 14. reactions to actions ------------------- <= 25 chars @ 5x8 --------
  STR_RX_MEAL,
  STR_RX_SNACK,
  STR_RX_CLEAN,
  STR_RX_MED,
  STR_RX_PLAY,
  STR_RX_PET,
  STR_RX_SCOLD_OK,
  STR_RX_SCOLD_BAD,
  STR_RX_LIGHT_ON,
  STR_RX_LIGHT_OFF,
  STR_RX_SLEEP,
  STR_RX_WAKE,
  STR_RX_OVERFED,
  STR_RX_EVOLVE,
  STR_RX_NOW_FORM,                       // "Ahora soy un {f}."

  // --- 15. action errors, parallel to ActionErr --- <= 25 chars @ 5x8 --------
  STR_AERR_NONE,
  STR_AERR_COOLDOWN,
  STR_AERR_FULL,
  STR_AERR_TIRED,
  STR_AERR_NOT_SICK,
  STR_AERR_NOTHING_TODO,
  STR_AERR_ASLEEP,
  STR_AERR_REFUSED,
  STR_AERR_SULKING,
  STR_AERR_DEAD,
  STR_AERR_IS_EGG,
  STR_AERR_BAD_ARG,

  // --- 16. absence ladder, parallel to AbsenceTier ---------------------------
  //     {t} = exact elapsed time, never rounded. ui wraps these to 2-3 lines.
  STR_ABS_NONE,
  STR_ABS_CORTA,
  STR_ABS_LARGA,
  STR_ABS_ABANDONO,
  STR_ABS_GRAVE,
  STR_ABS_MUERTO,
  STR_ABS_UNKNOWN,

  // --- 17. memorial, S12 -------------------------- <= 21 chars @ 6x10 -------
  STR_MEM_DIED,
  STR_MEM_SLEPT,
  STR_MEM_ALONE,
  STR_MEM_LIVED,
  STR_MEM_CAUSE,
  STR_MEM_EGG_LEFT_3P,
  STR_MEM_EGG_COLD,
  STR_MEM_BURY_HINT,

  // --- 18. egg, S13 ------------------------------- <= 25 chars @ 5x8 --------
  STR_EGG_TITLE,
  STR_EGG_NOT_YET,
  STR_EGG_MOURN,
  STR_EGG_RUB,
  STR_EGG_KIN,
  STR_EGG_HATCHING,
  STR_EGG_HATCHED,
  STR_EGG_NAMED,
  STR_HATCH_LOOK,
  STR_HATCH_WELCOME,

  // --- 19. alerts, parallel to AlertId ------------ <= 25 chars @ 5x8 --------
  STR_AL_NONE,
  STR_AL_HUNGRY,
  STR_AL_DIRTY,
  STR_AL_SAD,
  STR_AL_TIRED,
  STR_AL_SICK,
  STR_AL_POOP,
  STR_AL_LOW_HEALTH,
  STR_AL_WISH,
  STR_AL_EVOLVING,
  STR_AL_BIRTHDAY,
  STR_AL_STORM,
  STR_AL_MATE_FOUND,

  // --- 20. daily wish, parallel to WishId --------- <= 25 chars @ 5x8 --------
  STR_WISH_NONE,
  STR_WISH_PLAY,
  STR_WISH_SNACK,
  STR_WISH_CLEAN,
  STR_WISH_PET3,
  STR_WISH_OK,
  STR_WISH_FAIL,

  // --- 21. scheduled events ----------------------- <= 25 chars @ 5x8 --------
  STR_EV_VISITA,
  STR_EV_STORM,
  STR_EV_BIRTHDAY,
  STR_EV_SCARE,

  // --- 22. lineage, S7 ---------------------------- <= 12 chars @ 4x6/5x8 ----
  STR_LIN_TITLE,
  STR_LIN_LIVED,
  STR_LIN_DIED_OF,
  STR_LIN_GRADE,
  STR_LIN_EMPTY,
  STR_LIN_TAINTED,
  STR_LIN_OF,

  // --- 23. care grades A..F, parallel to CareGrade -- 1 char -----------------
  STR_GRADE_A, STR_GRADE_B, STR_GRADE_C, STR_GRADE_D, STR_GRADE_E, STR_GRADE_F,

  // --- 24. status screens S5/S6 ------------------- <= 12 chars --------------
  STR_ST_TITLE_A,
  STR_ST_TITLE_B,
  STR_ST_GEN,
  STR_ST_SEX,
  STR_ST_LUCK,
  STR_ST_MUTATIONS,
  STR_ST_RARE,
  STR_ST_SEX_O,
  STR_ST_SEX_X,

  // --- 25. social / BLE, S8 ----------------------- <= 25 chars @ 5x8 --------
  STR_SO_TITLE,
  STR_SO_SEARCHING,
  STR_SO_NOBODY,
  STR_SO_ASK,
  STR_SO_WIN,
  STR_SO_LOSE,
  STR_SO_COOLDOWN,
  STR_SO_FAR,
  STR_SO_TIRED,
  STR_SO_YOUNG,
  STR_SO_CAP,
  STR_SO_EGG_MADE,

  // --- 26. web / QR, S15 -------------------------- <= 25 chars @ 5x8 --------
  STR_WEB_TITLE,
  STR_WEB_PIN,
  STR_WEB_CONNECTING,
  STR_WEB_NOWIFI,
  STR_WEB_AP_HINT,

  // --- 27. settings, S9 --------------------------- <= 16 chars --------------
  STR_SET_TITLE,
  STR_SET_SOUND,
  STR_SET_BRIGHT,
  STR_SET_WEB,
  STR_SET_INFO,
  STR_SET_RESET,
  STR_SET_SAVED,
  STR_SET_MUTE_ON,
  STR_SET_MUTE_OFF,

  // --- 28. confirmations, S10 --------------------- <= 25 chars @ 5x8 --------
  STR_CF_SURE,
  STR_CF_QUIT_GAME,
  STR_CF_WIPE,
  STR_CF_WIPE2,
  STR_CF_KILL,

  // --- 29. minigames ------------------------------ <= 12 chars --------------
  STR_DG_REFLEX,
  STR_DG_MEMORY,
  STR_DG_JUMP,
  STR_GM_READY,
  STR_GM_GO,
  STR_GM_WIN,
  STR_GM_LOSE,
  STR_GM_SCORE,
  STR_GM_COOLDOWN,

  // --- 30. god mode, S14 -------------------------- <= 16 chars --------------
  STR_GOD_SPEED,
  STR_GOD_ABSENCE,
  STR_GOD_SETSTAT,
  STR_GOD_STAGE,
  STR_GOD_FORM,
  STR_GOD_KILL,
  STR_GOD_GENOME,
  STR_GOD_BLE,
  STR_GOD_CLOCK,
  STR_GOD_WIPE,
  STR_GOD_RANDOM,
  STR_GOD_EDIT,
  STR_GOD_DUMP,
  STR_GOD_LOAD,
  STR_GOD_TAINTED,
  STR_GOD_NO_REVIVE,
  STR_GOD_EXIT,

  // --- 31. errors --------------------------------- <= 25 chars @ 5x8 --------
  STR_ERR_NO_WIFI,
  STR_ERR_NO_NET,
  STR_ERR_BUSY,
  STR_ERR_MEM,
  STR_ERR_NVS,
  STR_ERR_OLED,
  STR_ERR_GENOME,

  // ===========================================================================
  //  33. UI BLOCK - appended by ui.cpp. APPEND ONLY, NEVER RENUMBER: every id
  //  above this line is already persisted in save blobs and cooldown tables.
  // ===========================================================================

  // --- 33a. affordance labels the base set did not carry -- <= 9 ch @ 5x8 ---
  STR_AF_MENU,
  STR_AF_VIEW,
  STR_AF_QUIT,
  STR_AF_RUB,
  STR_AF_MATE,

  // --- 33b. one-line help, BOTH on a list item (GAME_DESIGN 8.3, 3 s) -------
  //          <= 25 chars @ 5x8
  STR_HLP_FEED,
  STR_HLP_MEAL,
  STR_HLP_SNACK,
  STR_HLP_CLEAN,
  STR_HLP_PLAY,
  STR_HLP_HEALTH,
  STR_HLP_STATUS,
  STR_HLP_LIGHT,
  STR_HLP_SOCIAL,
  STR_HLP_SETTINGS,
  STR_HLP_SOUND,
  STR_HLP_WEB,
  STR_HLP_BRIGHT,
  STR_HLP_INFO,
  STR_HLP_RESET,
  STR_HLP_PEER,
  STR_HLP_BACK,

  // --- 33c. on-device (S4) minigames ----------------------- <= 25 @ 5x8 ---
  STR_DG_REFLEX_HINT,
  STR_DG_MEMORY_HINT,
  STR_DG_JUMP_HINT,
  STR_GM_ROUND,
  STR_GM_TOOSOON,

  // --- 33d. deterministic ancestor names (GAME_DESIGN 9.3) -----------------
  //  name = SYL_A[h % 12] + SYL_B[(h / 12) % 12], h = hash(lineage_id, gen).
  //  Same dynasty + same generation yields the same name on every device.
  STR_SYL_A00, STR_SYL_A01, STR_SYL_A02, STR_SYL_A03,
  STR_SYL_A04, STR_SYL_A05, STR_SYL_A06, STR_SYL_A07,
  STR_SYL_A08, STR_SYL_A09, STR_SYL_A10, STR_SYL_A11,
  STR_SYL_B00, STR_SYL_B01, STR_SYL_B02, STR_SYL_B03,
  STR_SYL_B04, STR_SYL_B05, STR_SYL_B06, STR_SYL_B07,
  STR_SYL_B08, STR_SYL_B09, STR_SYL_B10, STR_SYL_B11,

  // --- 33e. misc ui lines --------------------------------------------------
  STR_UI_NOBODY,
  STR_UI_GOD_HOLD,
  STR_UI_NO_CLOCK,

  // ===========================================================================
  //  34. GOD MODE BLOCK - appended by godmode.cpp. APPEND ONLY, NEVER RENUMBER.
  //  The base block (section 30) carries the twelve GAME_DESIGN 10.1 command
  //  labels; everything the debug console needs beyond those lives here.
  // ===========================================================================

  // --- 34a. list rows and panel titles ------------------- <= 16 ch @ 5x8 ---
  STR_GOD_SICK,
  STR_GOD_POOP,
  STR_GOD_AUTO,
  STR_GOD_HEAP,
  STR_GOD_RADIO,
  STR_GOD_STORE,

  // --- 34b. feedback lines -------------------------------- <= 25 ch @ 5x8 --
  STR_GOD_PASTE,
  STR_GOD_DONE,
  STR_GOD_FAILED,
  STR_GOD_MATING,
  STR_GOD_CHILD_OK,
  STR_GOD_CHILD_NO,
  STR_GOD_NOPET,
  STR_GOD_DUMP_ON,
  STR_GOD_DUMP_OFF,
  STR_GOD_NEED_BLE,

  // --- 34c. gene names, index-parallel to godmode.cpp's editor table --------
  //          <= 10 chars @ 5x8. Guarded by a static_assert in godmode.h.
  STR_GN_SPECIES,
  STR_GN_PATTERN,
  STR_GN_PALETTE,
  STR_GN_BODYSIZE,
  STR_GN_EARHORN,
  STR_GN_APPETITE,
  STR_GN_METAB,
  STR_GN_SOCIAB,
  STR_GN_TEMPER,
  STR_GN_HARDY,
  STR_GN_LUCK,
  STR_GN_SEX,
  STR_GN_RARE,

  STR_COUNT
};

// -----------------------------------------------------------------------------
//  THE TABLE. Index-parallel to StrId above. Keep them in lockstep.
//
//  `inline constexpr`, not `static` (C++17, the pattern sprites.h already
//  uses): 347 pointers = 1,388 B of .rodata, and with internal linkage EVERY
//  translation unit that includes this header pays for its own copy. The
//  string bytes themselves are deduplicated by the linker (.rodata.str1.1 is
//  a MERGE|STRINGS section) but the pointer array is not. As an inline
//  variable it has external linkage and the linker keeps exactly one.
// -----------------------------------------------------------------------------
inline constexpr const char* const ES[] = {
  /* STR_EMPTY */                 "",

  /* --- 1. boot / system --- */
  /* STR_APP_NAME */              "NOTTAMAGOCHI",
  /* STR_BOOT_DIZZY */            "Me he mareado un momento.",
  /* STR_BOOT_FIRST */            "Hola. Soy nuevo aquí.",

  /* --- 2. main menu ring --- */
  /* STR_MENU_FEED */             "COMER",
  /* STR_MENU_CLEAN */            "LIMPIAR",
  /* STR_MENU_PLAY */             "JUGAR",
  /* STR_MENU_HEALTH */           "SALUD",
  /* STR_MENU_STATUS */           "ESTADO",
  /* STR_MENU_LIGHT */            "LUZ",
  /* STR_MENU_SOCIAL */           "SOCIAL",
  /* STR_MENU_SETTINGS */         "AJUSTES",

  /* --- 3. affordances + generic --- */
  /* STR_AF_NEXT */               "SIG.",
  /* STR_AF_SEL */                "SEL",
  /* STR_AF_BACK */               "ATRÁS",
  /* STR_AF_OK */                 "OK",
  /* STR_AF_CANCEL */             "CANCELAR",
  /* STR_AF_PAUSE */              "PAUSA",
  /* STR_AF_PREV */               "ANTES",
  /* STR_AF_FWD */                "DESPUÉS",
  /* STR_AF_MORE */               "MÁS",
  /* STR_AF_BURY */               "ENTERRAR",
  /* STR_YES */                   "SÍ",
  /* STR_NO */                    "NO",
  /* STR_ON */                    "ON",
  /* STR_OFF */                   "OFF",
  /* STR_ITEM_BACK */             "Volver",

  /* --- 4. stat labels --- */
  /* STR_STAT_HUNGER */           "Saciedad",
  /* STR_STAT_HAPPINESS */        "Ánimo",
  /* STR_STAT_ENERGY */           "Energía",
  /* STR_STAT_HYGIENE */          "Higiene",
  /* STR_STAT_HEALTH */           "Salud",
  /* STR_STAT_BOND */             "Cariño",
  /* STR_STAT_DISCIPLINE */       "Disciplina",

  /* --- 5. life stages --- */
  /* STR_STAGE_EGG */             "HUEVO",
  /* STR_STAGE_BABY */            "BEBÉ",
  /* STR_STAGE_CHILD */           "CRÍO",
  /* STR_STAGE_TEEN */            "JOVEN",
  /* STR_STAGE_ADULT */           "ADULTO",
  /* STR_STAGE_SENIOR */          "ANCIANO",
  /* STR_STAGE_DEAD */            "MUERTO",

  /* --- 6. mood faces --- */
  /* STR_MOOD_MISERIA */          "MISERIA",
  /* STR_MOOD_TRISTE */           "TRISTE",
  /* STR_MOOD_NEUTRO */           "NEUTRO",
  /* STR_MOOD_CONTENTO */         "CONTENTO",
  /* STR_MOOD_FELIZ */            "FELIZ",
  /* STR_MOOD_EUFORICO */         "EUFÓRICO",

  /* --- 7. adult forms --- */
  /* STR_FORM_BOLOTA */           "BOLOTA",
  /* STR_FORM_ZAMPASALTO */       "ZAMPASALTO",
  /* STR_FORM_BUHO */             "BÚHO",
  /* STR_FORM_PUNKI */            "PUNKI",
  /* STR_FORM_MOHO */             "MOHO",
  /* STR_FORM_QUIMERA */          "QUIMERA",

  /* --- 8. species --- */
  /* 00 */ "BLOB",      /* 01 */ "ORUGA",   /* 02 */ "PÁJARO",  /* 03 */ "GATO",
  /* 04 */ "SETA",      /* 05 */ "CACTUS",  /* 06 */ "PEZ",     /* 07 */ "ROBOT",
  /* 08 */ "FANTASMA",  /* 09 */ "CONEJO",  /* 10 */ "DRAGÓN",  /* 11 */ "MEDUSA",
  /* 12 */ "ESPEJO",    /* 13 */ "NUDO",    /* 14 */ "ECO",     /* 15 */ "VACÍO",

  /* --- 9. patterns --- */
  /* 00 */ "LISO",      /* 01 */ "RAYAS H", /* 02 */ "RAYAS V", /* 03 */ "LUNARES",
  /* 04 */ "DAMERO",    /* 05 */ "DEGRADADO", /* 06 */ "MOTEADO", /* 07 */ "CEBRA",
  /* 08 */ "ANTIFAZ",   /* 09 */ "CALCETINES", /* 10 */ "TRAMA 25", /* 11 */ "TRAMA 50",
  /* 12 */ "ESPIRAL",   /* 13 */ "ESTRELLA", /* 14 */ "GRIETA",  /* 15 */ "NINGUNO",

  /* --- 10. temperament --- */
  /* STR_TEMPER_SOLAR */          "SOLAR",
  /* STR_TEMPER_TRANQUILO */      "TRANQUILO",
  /* STR_TEMPER_NERVIOSO */       "NERVIOSO",
  /* STR_TEMPER_GOTICO */         "GÓTICO",

  /* --- 12. death causes --- */
  /* STR_CAUSE_NONE */            "-",
  /* STR_CAUSE_HUNGER */          "HAMBRE",
  /* STR_CAUSE_FILTH */           "SUCIEDAD",
  /* STR_CAUSE_ILLNESS */         "ENFERMEDAD",
  /* STR_CAUSE_SADNESS */         "TRISTEZA",
  /* STR_CAUSE_OLD_AGE */         "VEJEZ",
  /* STR_CAUSE_NEGLECT */         "ABANDONO",
  /* STR_CAUSE_ACCIDENT */        "ACCIDENTE",

  /* --- 13. action names --- */
  /* STR_ACT_NONE */              "-",
  /* STR_ACT_FEED_MEAL */         "Comida",
  /* STR_ACT_FEED_SNACK */        "Chuche",
  /* STR_ACT_CLEAN */             "Limpiar",
  /* STR_ACT_MEDICINE */          "Medicina",
  /* STR_ACT_PLAY */              "Jugar",
  /* STR_ACT_PET */               "Mimo",
  /* STR_ACT_SCOLD */             "Regañar",
  /* STR_ACT_LIGHT */             "Luz",
  /* STR_ACT_SLEEP */             "Dormir",

  /* --- 14. reactions --- */
  /* STR_RX_MEAL */               "Mmm. Arroz. Otra vez.",
  /* STR_RX_SNACK */              "Esto sí. Esto siempre.",
  /* STR_RX_CLEAN */              "Gracias. Ya olía raro.",
  /* STR_RX_MED */                "Sabe a castigo. Sirve.",
  /* STR_RX_PLAY */               "¡Venga! A ver si puedes.",
  /* STR_RX_PET */                "Vale. No te emociones.",
  /* STR_RX_SCOLD_OK */           "Vale. Me he pasado.",
  /* STR_RX_SCOLD_BAD */          "¿Y eso a qué ha venido?",
  /* STR_RX_LIGHT_ON */           "Luz. Qué maravilla.",
  /* STR_RX_LIGHT_OFF */          "Luz fuera. Por fin.",
  /* STR_RX_SLEEP */              "Buenas noches. Supongo.",
  /* STR_RX_WAKE */               "Estaba bien dormido.",
  /* STR_RX_OVERFED */            "Me va a explotar algo.",
  /* STR_RX_EVOLVE */             "¡Estoy cambiando!",
  /* STR_RX_NOW_FORM */           "Ahora soy un {f}.",

  /* --- 15. action errors --- */
  /* STR_AERR_NONE */             "",
  /* STR_AERR_COOLDOWN */         "Espera un poco.",
  /* STR_AERR_FULL */             "Ni de broma. Estoy lleno.",
  /* STR_AERR_TIRED */            "No tengo ni para mirarte.",
  /* STR_AERR_NOT_SICK */         "No estoy enfermo. Aún.",
  /* STR_AERR_NOTHING_TODO */     "Está limpio. Bastante.",
  /* STR_AERR_ASLEEP */           "Duermo. Increíble, ¿eh?",
  /* STR_AERR_REFUSED */          "No me apetece.",
  /* STR_AERR_SULKING */          "Ahora mismo no.",
  /* STR_AERR_DEAD */             "Ya no hay nadie aquí.",
  /* STR_AERR_IS_EGG */           "Es un huevo. No come.",
  /* STR_AERR_BAD_ARG */          "Eso no existe.",

  /* --- 16. absence ladder --- */
  /* STR_ABS_NONE */              "",
  /* STR_ABS_CORTA */             "¿Dónde estabas? {t}.",
  /* STR_ABS_LARGA */             "{t}. Sin avisar. Sin nada.",
  /* STR_ABS_ABANDONO */          "{t}. He contado los segundos. Todos.",
  /* STR_ABS_GRAVE */             "{t}. Ya no sé si esto lo arreglas.",
  /* STR_ABS_MUERTO */            "{t}. Llegas tarde.",
  /* STR_ABS_UNKNOWN */           "No sé cuánto tiempo ha pasado. Sé que fue mucho. Y sé que fuiste tú.",

  /* --- 17. memorial --- */
  /* STR_MEM_DIED */              "{n} murió.",
  /* STR_MEM_SLEPT */             "{n} se durmió.",
  /* STR_MEM_ALONE */             "Moriste solo.",
  /* STR_MEM_LIVED */             "Vivió {t}.",
  /* STR_MEM_CAUSE */             "Causa: {c}.",
  /* STR_MEM_EGG_LEFT_3P */       "Dejó un huevo.",
  /* STR_MEM_EGG_COLD */          "Se enfrió. Ha cambiado.",
  /* STR_MEM_BURY_HINT */         "Mantén DCHA: enterrar",

  /* --- 18. egg --- */
  /* STR_EGG_TITLE */             "HUEVO",
  /* STR_EGG_NOT_YET */           "Todavía no.",
  /* STR_EGG_MOURN */             "Todavía no. Dale un momento.",
  /* STR_EGG_RUB */               "Frótalo. Con ganas.",
  /* STR_EGG_KIN */               "Parientes. Ya veremos.",
  /* STR_EGG_HATCHING */          "Se está abriendo...",
  /* STR_EGG_HATCHED */           "¡Ha salido!",
  /* STR_EGG_NAMED */             "Se llama {n}.",
  /* STR_HATCH_LOOK */            "¿Y tú quién eres?",
  /* STR_HATCH_WELCOME */         "Cuídalo bien.",

  /* --- 19. alerts --- */
  /* STR_AL_NONE */               "",
  /* STR_AL_HUNGRY */             "Tengo hambre.",
  /* STR_AL_DIRTY */              "Esto está asqueroso.",
  /* STR_AL_SAD */                "Estoy hundido.",
  /* STR_AL_TIRED */              "No puedo más.",
  /* STR_AL_SICK */               "Estoy enfermo.",
  /* STR_AL_POOP */               "He hecho caca.",
  /* STR_AL_LOW_HEALTH */         "Me estoy apagando.",
  /* STR_AL_WISH */               "Quiero una cosa.",
  /* STR_AL_EVOLVING */           "Algo está pasando...",
  /* STR_AL_BIRTHDAY */           "¡Es mi cumpleaños!",
  /* STR_AL_STORM */              "Va a caer una buena.",
  /* STR_AL_MATE_FOUND */         "Hay alguien cerca.",

  /* --- 20. daily wish --- */
  /* STR_WISH_NONE */             "",
  /* STR_WISH_PLAY */             "Hoy quiero jugar.",
  /* STR_WISH_SNACK */            "Hoy quiero una chuche.",
  /* STR_WISH_CLEAN */            "Hoy quiero estar limpio.",
  /* STR_WISH_PET3 */             "Hoy quiero tres mimos.",
  /* STR_WISH_OK */               "Era eso. Justo eso.",
  /* STR_WISH_FAIL */             "Se te ha pasado. Otra vez.",

  /* --- 21. scheduled events --- */
  /* STR_EV_VISITA */             "Ha venido alguien. Se ha ido.",
  /* STR_EV_STORM */              "Esta noche truena.",
  /* STR_EV_BIRTHDAY */           "¡Feliz cumpleaños!",
  /* STR_EV_SCARE */              "¡AY! Eso ha dolido.",

  /* --- 22. lineage --- */
  /* STR_LIN_TITLE */             "ESTIRPE",
  /* STR_LIN_LIVED */             "vivió",
  /* STR_LIN_DIED_OF */           "murió de",
  /* STR_LIN_GRADE */             "nota",
  /* STR_LIN_EMPTY */             "Sin antepasados. Aún.",
  /* STR_LIN_TAINTED */           "* tocado por dios",
  /* STR_LIN_OF */                "de",

  /* --- 23. care grades --- */
  /* STR_GRADE_A */ "A", /* B */ "B", /* C */ "C", /* D */ "D", /* E */ "E", /* F */ "F",

  /* --- 24. status screens --- */
  /* STR_ST_TITLE_A */            "ESTADO",
  /* STR_ST_TITLE_B */            "ADN",
  /* STR_ST_GEN */                "Gen.",
  /* STR_ST_SEX */                "Sexo",
  /* STR_ST_LUCK */               "Suerte",
  /* STR_ST_MUTATIONS */          "Mutaciones",
  /* STR_ST_RARE */               "RARO",
  /* STR_ST_SEX_O */              "o",
  /* STR_ST_SEX_X */              "x",

  /* --- 25. social / BLE --- */
  /* STR_SO_TITLE */              "SOCIAL",
  /* STR_SO_SEARCHING */          "Buscando...",
  /* STR_SO_NOBODY */             "Nadie cerca. Como siempre.",
  /* STR_SO_ASK */                "¿Emparejar?",
  /* STR_SO_WIN */                "Ha salido bien.",
  /* STR_SO_LOSE */               "No ha cuajado.",
  /* STR_SO_COOLDOWN */           "Espera 24 h. Hay normas.",
  /* STR_SO_FAR */                "Demasiado lejos.",
  /* STR_SO_TIRED */              "No tengo energía para esto.",
  /* STR_SO_YOUNG */              "Aún soy muy joven.",
  /* STR_SO_CAP */                "Reinicia para seguir emparejando.",
  /* STR_SO_EGG_MADE */           "Va a haber huevo.",

  /* --- 26. web / QR --- */
  /* STR_WEB_TITLE */             "MÓVIL",
  /* STR_WEB_PIN */               "PIN",
  /* STR_WEB_CONNECTING */        "Conectando...",
  /* STR_WEB_NOWIFI */            "Sin WiFi. Modo directo.",
  /* STR_WEB_AP_HINT */           "Conéctate a la red:",

  /* --- 27. settings --- */
  /* STR_SET_TITLE */             "AJUSTES",
  /* STR_SET_SOUND */             "Sonido",
  /* STR_SET_BRIGHT */            "Brillo",
  /* STR_SET_WEB */               "Web y QR",
  /* STR_SET_INFO */              "Acerca de",
  /* STR_SET_RESET */             "Empezar de cero",
  /* STR_SET_SAVED */             "Guardado.",
  /* STR_SET_MUTE_ON */           "Sonido apagado.",
  /* STR_SET_MUTE_OFF */          "Sonido encendido.",

  /* --- 28. confirmations --- */
  /* STR_CF_SURE */               "¿Seguro?",
  /* STR_CF_QUIT_GAME */          "¿Abandonar? Pierdes la partida.",
  /* STR_CF_WIPE */               "¿Borrar todo? No hay vuelta.",
  /* STR_CF_WIPE2 */              "¿De verdad? Última oportunidad.",
  /* STR_CF_KILL */               "¿Matarlo? Va en serio.",

  /* --- 29. minigames --- */
  /* STR_DG_REFLEX */             "REFLEJOS",
  /* STR_DG_MEMORY */             "MEMORIA",
  /* STR_DG_JUMP */               "SALTO",
  /* STR_GM_READY */              "¿Listo?",
  /* STR_GM_GO */                 "¡YA!",
  /* STR_GM_WIN */                "¡Ganaste!",
  /* STR_GM_LOSE */               "Otra vez será.",
  /* STR_GM_SCORE */              "Puntos",
  /* STR_GM_COOLDOWN */           "Descansa un poco.",

  /* --- 30. god mode --- */
  /* STR_GOD_SPEED */             "VELOCIDAD",
  /* STR_GOD_ABSENCE */           "SALTAR AUSENCIA",
  /* STR_GOD_SETSTAT */           "FIJAR STAT",
  /* STR_GOD_STAGE */             "FORZAR ETAPA",
  /* STR_GOD_FORM */              "FORZAR FORMA",
  /* STR_GOD_KILL */              "MATAR",
  /* STR_GOD_GENOME */            "GENOMA",
  /* STR_GOD_BLE */               "BLE FALSO",
  /* STR_GOD_CLOCK */             "RELOJ",
  /* STR_GOD_WIPE */              "BORRAR TODO",
  /* STR_GOD_RANDOM */            "ALEATORIO",
  /* STR_GOD_EDIT */              "EDITAR",
  /* STR_GOD_DUMP */              "VOLCAR",
  /* STR_GOD_LOAD */              "CARGAR",
  /* STR_GOD_TAINTED */           "Tocado. Para siempre.",
  /* STR_GOD_NO_REVIVE */         "Resucitar no. Matar sí.",
  /* STR_GOD_EXIT */              "Saliendo. Guardado.",

  /* --- 31. errors --- */
  /* STR_ERR_NO_WIFI */           "Sin WiFi.",
  /* STR_ERR_NO_NET */            "No hay red.",
  /* STR_ERR_BUSY */              "Ocupado. Espera.",
  /* STR_ERR_MEM */               "Sin memoria libre.",
  /* STR_ERR_NVS */               "No consigo recordar nada.",
  /* STR_ERR_OLED */              "Nada en 0x3C. Mira los cables.",
  /* STR_ERR_GENOME */            "Genoma corrupto. Ignorado.",

  /* --- 33a. affordance labels --- */
  /* STR_AF_MENU */               "MENÚ",
  /* STR_AF_VIEW */               "VISTA",
  /* STR_AF_QUIT */               "SALIR",
  /* STR_AF_RUB */                "FROTAR",
  /* STR_AF_MATE */               "CORTEJAR",

  /* --- 33b. one-line help --- */
  /* STR_HLP_FEED */              "Darle de comer.",
  /* STR_HLP_MEAL */              "Llena. Engorda poco.",
  /* STR_HLP_SNACK */             "Alegra. Engorda mucho.",
  /* STR_HLP_CLEAN */             "Recoger la caca.",
  /* STR_HLP_PLAY */              "Sube el ánimo. Cansa.",
  /* STR_HLP_HEALTH */            "Medicina si está malo.",
  /* STR_HLP_STATUS */            "Cómo va por dentro.",
  /* STR_HLP_LIGHT */             "Apágala para dormir.",
  /* STR_HLP_SOCIAL */            "Buscar otros bichos.",
  /* STR_HLP_SETTINGS */          "Cosas de mayores.",
  /* STR_HLP_SOUND */             "Zumbido al avisar.",
  /* STR_HLP_WEB */               "Página y QR en el móvil.",
  /* STR_HLP_BRIGHT */            "Brillo de la pantalla.",
  /* STR_HLP_INFO */              "Versión, red y memoria.",
  /* STR_HLP_RESET */             "Borra todo. Todo.",
  /* STR_HLP_PEER */              "Acércalo y espera.",
  /* STR_HLP_BACK */              "Volver sin tocar nada.",

  /* --- 33c. on-device minigames --- */
  /* STR_DG_REFLEX_HINT */        "Pulsa el lado que brille.",
  /* STR_DG_MEMORY_HINT */        "Repite la secuencia.",
  /* STR_DG_JUMP_HINT */          "Pulsa para saltar.",
  /* STR_GM_ROUND */              "Ronda",
  /* STR_GM_TOOSOON */            "Demasiado pronto.",

  /* --- 33d. ancestor name syllables --- */
  /* A */ "Bo", "Ti", "Nu", "Ma", "Ke", "Zu", "Pi", "Ro", "La", "Ve", "Gu", "Ña",
  /* B */ "ri", "po", "tán", "fu", "sco", "lín", "ma", "zo", "que", "nel", "bi", "rrón",

  /* --- 33e. misc ui --- */
  /* STR_UI_NOBODY */             "Aquí no hay nadie.",
  /* STR_UI_GOD_HOLD */           "···",
  /* STR_UI_NO_CLOCK */           "Sin hora fiable.",

  /* --- 34a. god mode: list rows and panel titles --- */
  /* STR_GOD_SICK */              "ENFERMAR",
  /* STR_GOD_POOP */              "CACAS",
  /* STR_GOD_AUTO */              "AUTO",
  /* STR_GOD_HEAP */              "MEMORIA",
  /* STR_GOD_RADIO */             "RADIO",
  /* STR_GOD_STORE */             "GUARDADO",

  /* --- 34b. god mode: feedback lines --- */
  /* STR_GOD_PASTE */             "Pega 32 hex y ENTER.",
  /* STR_GOD_DONE */              "Hecho.",
  /* STR_GOD_FAILED */            "No se pudo.",
  /* STR_GOD_MATING */            "Buscando pareja...",
  /* STR_GOD_CHILD_OK */          "Cría lista. SEL la pone.",
  /* STR_GOD_CHILD_NO */          "Sin cría.",
  /* STR_GOD_NOPET */             "No hay bicho todavía.",
  /* STR_GOD_DUMP_ON */           "Volcado serie: ON",
  /* STR_GOD_DUMP_OFF */          "Volcado serie: OFF",
  /* STR_GOD_NEED_BLE */          "Necesito la radio BLE.",

  /* --- 34c. god mode: gene names --- */
  /* STR_GN_SPECIES */            "ESPECIE",
  /* STR_GN_PATTERN */            "PATRÓN",
  /* STR_GN_PALETTE */            "PALETA",
  /* STR_GN_BODYSIZE */           "TAMAÑO",
  /* STR_GN_EARHORN */            "OREJAS",
  /* STR_GN_APPETITE */           "APETITO",
  /* STR_GN_METAB */              "METABOL.",
  /* STR_GN_SOCIAB */             "SOCIAL",
  /* STR_GN_TEMPER */             "CARÁCTER",
  /* STR_GN_HARDY */              "DUREZA",
  /* STR_GN_LUCK */               "SUERTE",
  /* STR_GN_SEX */                "SEXO",
  /* STR_GN_RARE */               "RARO"
};

// -----------------------------------------------------------------------------
//  ACCESSORS
// -----------------------------------------------------------------------------
#define S(id)          (ES[(uint16_t)(id)])

#define S_STAT(s)      (ES[STR_STAT_HUNGER    + (uint16_t)(s)])   // StatId
#define S_STAGE(s)     (ES[STR_STAGE_EGG      + (uint16_t)(s)])   // Stage
#define S_MOOD(m)      (ES[STR_MOOD_MISERIA   + (uint16_t)(m)])   // Mood
#define S_FORM(f)      (ES[STR_FORM_BOLOTA    + (uint16_t)(f)])   // AdultForm (not UNSET)
#define S_SPECIES(v)   (ES[STR_SPECIES_00     + (uint16_t)((v) & 0x0F)])
#define S_PATTERN(v)   (ES[STR_PATTERN_00     + (uint16_t)((v) & 0x0F)])
#define S_TEMPER(c)    (ES[STR_TEMPER_SOLAR   + (uint16_t)(c)])   // Temperament class
#define S_CAUSE(c)     (ES[STR_CAUSE_NONE     + (uint16_t)(c)])   // DeathCause
#define S_ACTION(a)    (ES[STR_ACT_NONE       + (uint16_t)(a)])   // ActionId
#define S_AERR(e)      (ES[STR_AERR_NONE      + (uint16_t)(e)])   // ActionErr
#define S_ABSENCE(t)   (ES[STR_ABS_NONE       + (uint16_t)(t)])   // AbsenceTier
#define S_ALERT(a)     (ES[STR_AL_NONE        + (uint16_t)(a)])   // AlertId
#define S_WISH(w)      (ES[STR_WISH_NONE      + (uint16_t)(w)])   // WishId
#define S_GRADE(g)     (ES[STR_GRADE_A        + (uint16_t)(g)])   // CareGrade
#define S_MENU(i)      (ES[STR_MENU_FEED      + (uint16_t)(i)])   // 0..7
#define S_SYL_A(i)     (ES[STR_SYL_A00        + (uint16_t)((i) % 12u)])  // name syllable 1
#define S_SYL_B(i)     (ES[STR_SYL_B00        + (uint16_t)((i) % 12u)])  // name syllable 2

// -----------------------------------------------------------------------------
//  GUARDS. If any of these fires, the table and the enums drifted apart.
// -----------------------------------------------------------------------------
static_assert(NT_ARRAY_LEN(ES) == (size_t)STR_COUNT,
              "strings_es.h: ES[] and StrId are out of sync");

static_assert(STR_STAT_DISCIPLINE - STR_STAT_HUNGER + 1 == (int)ST_COUNT,      "stat labels");
static_assert(STR_STAGE_DEAD      - STR_STAGE_EGG   + 1 == (int)STAGE_COUNT,   "stage names");
static_assert(STR_MOOD_EUFORICO   - STR_MOOD_MISERIA+ 1 == (int)MOOD_COUNT,    "mood names");
static_assert(STR_FORM_QUIMERA    - STR_FORM_BOLOTA + 1 == (int)FORM_COUNT,    "form names");
static_assert(STR_SPECIES_15      - STR_SPECIES_00  + 1 == SPECIES_COUNT,      "species names");
static_assert(STR_PATTERN_15      - STR_PATTERN_00  + 1 == PATTERN_COUNT,      "pattern names");
static_assert(STR_TEMPER_GOTICO   - STR_TEMPER_SOLAR+ 1 == (int)TEMPER_COUNT,  "temperament names");
static_assert(STR_CAUSE_ACCIDENT  - STR_CAUSE_NONE  + 1 == (int)DEATH_COUNT,   "cause names");
static_assert(STR_ACT_SLEEP       - STR_ACT_NONE    + 1 == (int)ACT_COUNT,     "action names");
static_assert(STR_AERR_BAD_ARG    - STR_AERR_NONE   + 1 == (int)AERR_COUNT,    "action errors");
static_assert(STR_ABS_UNKNOWN     - STR_ABS_NONE    + 1 == (int)ABS_COUNT,     "absence lines");
static_assert(STR_AL_MATE_FOUND   - STR_AL_NONE     + 1 == (int)AL_COUNT,      "alert lines");
static_assert(STR_WISH_PET3       - STR_WISH_NONE   + 1 == (int)WISH_COUNT,    "wish lines");
static_assert(STR_GRADE_F         - STR_GRADE_A     + 1 == (int)GRADE_COUNT,   "care grades");
static_assert(STR_MENU_SETTINGS   - STR_MENU_FEED   + 1 == MENU_ITEM_COUNT,    "menu labels");
static_assert(STR_SYL_A11         - STR_SYL_A00     + 1 == 12,                 "name syllables A");
static_assert(STR_SYL_B11         - STR_SYL_B00     + 1 == 12,                 "name syllables B");
static_assert(STR_HLP_BACK        - STR_HLP_FEED    + 1 == 17,                 "ui help block");

#endif // NT_STRINGS_ES_H
