// =============================================================================
//  PEBBLEBOL - core/strings_es.h
//  EVERY user-facing string in the product. Nothing else may contain a Spanish
//  literal. UTF-8, no BOM.
//
//  RENDERING RULES:
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

  // --- 2. main menu ring, spec section 8 order ---- <= 10 chars @ t0_11b -----
  //     PEBBLE CARE PLAY BOX NETWORK LINK SETTINGS
  //     S_MENU(i) indexes exactly these seven, in this order, and the guard at
  //     the bottom of the file ties them to MENU_ITEM_COUNT.
  STR_MENU_PEBBLE,
  STR_MENU_CARE,
  STR_MENU_PLAY,
  STR_MENU_BOX,
  STR_MENU_NETWORK,
  STR_MENU_LINK,
  STR_MENU_SETTINGS,
  //     Labels the ring no longer carries. They are still named one at a time
  //     by the CARE list, the SETTINGS list and the god console, so they stay -
  //     outside the indexed block, where nothing counts them.
  STR_MENU_FEED,
  STR_MENU_CLEAN,
  STR_MENU_HEALTH,
  STR_MENU_STATUS,
  STR_MENU_SOCIAL,

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

  // --- 5. life stages, parallel to Stage ---------- <= 8 chars ---------------
  STR_STAGE_EGG,
  STR_STAGE_BABY,
  STR_STAGE_CHILD,
  STR_STAGE_TEEN,
  STR_STAGE_ADULT,
  STR_STAGE_SENIOR,

  // --- 6. mood faces, parallel to Mood ------------ <= 9 chars ---------------
  STR_MOOD_MISERIA,
  STR_MOOD_TRISTE,
  STR_MOOD_NEUTRO,
  STR_MOOD_CONTENTO,
  STR_MOOD_FELIZ,
  STR_MOOD_EUFORICO,

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

  // --- 13. action names, parallel to ActionId ----- <= 10 chars --------------
  STR_ACT_NONE,
  STR_ACT_FEED_MEAL,
  STR_ACT_FEED_SNACK,
  STR_ACT_CLEAN,
  STR_ACT_MEDICINE,
  STR_ACT_PLAY,
  STR_ACT_PET,
  STR_ACT_SLEEP,

  // --- 14. reactions to actions ------------------- <= 25 chars @ 5x8 --------
  STR_RX_MEAL,
  STR_RX_SNACK,
  STR_RX_CLEAN,
  STR_RX_MED,
  STR_RX_PLAY,
  STR_RX_PET,
  STR_RX_SLEEP,
  STR_RX_WAKE,
  STR_RX_EVOLVE,
  STR_RX_LEVEL_UP,

  // --- 15. action errors, parallel to ActionErr --- <= 25 chars @ 5x8 --------
  STR_AERR_NONE,
  STR_AERR_COOLDOWN,
  STR_AERR_FULL,
  STR_AERR_TIRED,
  STR_AERR_NOT_SICK,
  STR_AERR_NOTHING_TODO,
  STR_AERR_ASLEEP,
  STR_AERR_IS_EGG,
  STR_AERR_BAD_ARG,

  // --- 16. welcome back ---------------------------------------------------
  //     {t} = exact elapsed time, never rounded. ui wraps these to 2-3 lines.
  STR_ABS_AWAY,
  STR_ABS_UNKNOWN,

  // --- 18. egg ------------------------------------ <= 25 chars @ 5x8 --------
  STR_EGG_TITLE,
  STR_EGG_NOT_YET,
  STR_EGG_COLD,
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
  STR_EV_BIRTHDAY,

  // --- 24. status screens ------------------- <= 12 chars --------------
  STR_ST_TITLE_A,
  STR_ST_TITLE_B,
  STR_ST_GEN,
  STR_ST_SEX,
  STR_ST_LUCK,
  STR_ST_MUTATIONS,
  STR_ST_RARE,
  STR_ST_SEX_O,
  STR_ST_SEX_X,
  STR_ST_LEVEL,
  STR_ST_XP,
  STR_ST_HP,

  // --- 25. social / BLE --------------------------- <= 25 chars @ 5x8 --------
  STR_SO_TITLE,
  STR_SO_SEARCHING,
  STR_SO_NOBODY,
  STR_SO_CAP,

  // --- 26. web / QR ------------------------------- <= 25 chars @ 5x8 --------
  STR_WEB_TITLE,
  STR_WEB_PIN,
  STR_WEB_CONNECTING,
  STR_WEB_NOWIFI,
  STR_WEB_AP_HINT,

  // --- 27. settings ------------------------------- <= 16 chars --------------
  STR_SET_TITLE,
  STR_SET_SOUND,
  STR_SET_BRIGHT,
  STR_SET_WEB,
  STR_SET_INFO,
  STR_SET_RESET,
  STR_SET_SAVED,
  STR_SET_MUTE_ON,
  STR_SET_MUTE_OFF,

  // --- 28. confirmations -------------------------- <= 25 chars @ 5x8 --------
  STR_CF_SURE,
  STR_CF_QUIT_GAME,
  STR_CF_WIPE,
  STR_CF_WIPE2,
  STR_CF_EVOLVE,

  // --- 29. minigames ------------------------------ <= 12 chars --------------
  //     The six names are INDEX-PARALLEL TO MgId (minigames/minigame.h) and the
  //     guard at the bottom of the file ties them to MG_ID_COUNT: PING and
  //     SECUENCIA are canonical for the persisted minigames_won counter, so
  //     nothing may be inserted before them.
  STR_MG_PING,
  STR_MG_SEQ,
  STR_MG_FLOOD,
  STR_MG_FIREWALL,
  STR_MG_BUFFER,
  STR_MG_DELETE,
  STR_GM_READY,
  STR_GM_GO,
  STR_GM_WIN,
  STR_GM_LOSE,
  STR_GM_SCORE,
  STR_GM_COOLDOWN,
  //     The affordance strip a RUNNING game shows. Both buttons are play
  //     inputs in every one of the six, so the strip may not advertise B as
  //     "PAUSA" alone; the pause is B HELD, in the same tap/hold shape the
  //     lists already use for "ATRAS/SEL". Wider than the 9-char affordance
  //     budget in group 3, which is why they live here: rd_affordance() caps a
  //     label at 12 glyphs of 5x8 and "JUGAR/PAUSA" is 11.
  STR_GM_AF_PLAY,
  STR_GM_AF_PLAY_PAUSE,

  // --- 30. god mode ------------------------------- <= 16 chars --------------
  STR_GOD_SPEED,
  STR_GOD_ABSENCE,
  STR_GOD_SETSTAT,
  STR_GOD_STAGE,
  STR_GOD_GENOME,
  STR_GOD_CLOCK,
  STR_GOD_WIPE,
  STR_GOD_RANDOM,
  STR_GOD_EDIT,
  STR_GOD_DUMP,
  STR_GOD_LOAD,
  STR_GOD_TAINTED,
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
  //  33. UI BLOCK - appended by ui.cpp. Nothing persists a StrId any more
  //  (the Telegram cooldown table that did is gone), so the ids above are free
  //  to renumber when a group is removed - the guards at the bottom of this
  //  file are what keep the table and the enums in step.
  // ===========================================================================

  // --- 33a. affordance labels the base set did not carry -- <= 9 ch @ 5x8 ---
  STR_AF_MENU,
  STR_AF_VIEW,
  STR_AF_PET,
  STR_AF_QUIT,
  STR_AF_RUB,

  // --- 33b. one-line help, BOTH on a list item -------
  //          <= 25 chars @ 5x8
  STR_HLP_FEED,
  STR_HLP_MEAL,
  STR_HLP_SNACK,
  STR_HLP_CLEAN,
  STR_HLP_PLAY,
  STR_HLP_HEALTH,
  STR_HLP_STATUS,
  STR_HLP_SOCIAL,
  STR_HLP_BOX,
  STR_HLP_NETWORK,
  STR_HLP_SETTINGS,
  STR_HLP_SOUND,
  STR_HLP_WEB,
  STR_HLP_BRIGHT,
  STR_HLP_INFO,
  STR_HLP_RESET,
  STR_HLP_PEER,
  STR_HLP_BACK,

  // --- 33c. on-device minigames --------------------------- <= 25 @ 5x8 ---
  //     The six hints are index-parallel to the six names above (and therefore
  //     to MgId); the guard at the bottom of the file ties both blocks to
  //     MG_ID_COUNT. A hint says WHAT the game is, never which button: the
  //     firmware speaks SIG. / SEL / ATRAS / PAUSA and never names a button.
  //     THE 25-CHARACTER BUDGET IS HARD HERE, not advisory: the GAME intro card
  //     draws a hint with rd_text_center() at 5x8 and does NOT wrap, so 26
  //     glyphs is 130 px on a 128 px panel. The help modal wraps; the intro
  //     card is what sets the limit.
  STR_MG_PING_HINT,
  STR_MG_SEQ_HINT,
  STR_MG_FLOOD_HINT,
  STR_MG_FIREWALL_HINT,
  STR_MG_BUFFER_HINT,
  STR_MG_DELETE_HINT,
  STR_GM_ROUND,
  STR_GM_NEXT,
  STR_GM_TOOSOON,
  STR_GM_REROUTE,
  STR_GM_WASTED,

  // --- 33d. deterministic dynasty names ------------------
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
  STR_UI_SOON,

  // --- 33f. time entry ---------------------------- <= 16 chars --------------
  STR_SET_CLOCK,
  STR_CLK_TITLE,
  STR_CLK_YEAR,
  STR_CLK_MONTH,
  STR_CLK_DAY,
  STR_CLK_HOUR,
  STR_CLK_MIN,
  STR_CLK_HOLD,
  STR_CLK_SAVED,
  STR_CLK_BAD,
  STR_HLP_CLOCK,
  STR_AF_ADD,

  // ===========================================================================
  //  34. GOD MODE BLOCK - appended by godmode.cpp.
  //  The base block (section 30) carries the surviving command labels;
  //  everything the debug console needs beyond those lives here.
  // ===========================================================================

  // --- 34a. list rows and panel titles ------------------- <= 16 ch @ 5x8 ---
  STR_GOD_AUTO,
  STR_GOD_HEAP,
  STR_GOD_RADIO,
  STR_GOD_STORE,
  STR_GOD_POWER,
  STR_GOD_INFO,
  STR_GOD_PERF,

  // --- 34b. feedback lines -------------------------------- <= 25 ch @ 5x8 --
  STR_GOD_PASTE,
  STR_GOD_DONE,
  STR_GOD_FAILED,
  STR_GOD_NOPET,
  STR_GOD_DUMP_ON,
  STR_GOD_DUMP_OFF,

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

  // --- 33i. BOOT / LOAD_SAVE / ERROR (P2-C9c). The save pipeline is the one
  //          place the device tells the user something went wrong with their
  //          pet's memory, so the words are plain and the choice is explicit.
  STR_BOOT_LOADING,
  STR_SAVE_ERR_TITLE,
  STR_SAVE_ERR_BODY,
  STR_SAVE_ERR_NEWER,
  STR_SAVE_ERR_A,
  STR_SAVE_ERR_B,
  STR_SAVE_NO_BACKUP,
  STR_SAVE_UPDATE_FW,
  STR_SAVE_ERR_EXIT,
  STR_SAVE_RECOVERED,
  STR_SAVE_FROM_BACKUP,
  STR_SAVE_UPDATED,

  // --- 33j. DISPLAY error (P2-C11a). The panel did not answer on the I2C
  //          bus, so nobody will ever read these: they are written for the
  //          retry that succeeds, when the screen comes back and the device
  //          has to explain what it has been blinking about.
  // --- 33j. the Phase 2 placeholders (P2-C11c). CREATOR IS NO LONGER ONE
  //          (P8-C5): STR_CREATOR_PHASE was "Creador - Fase 8", the line the
  //          screen printed while the page it points at did not exist yet, and
  //          phase 8 has arrived - so it is DELETED rather than left saying a
  //          phase number nobody needs. What replaces it is the spec section 34
  //          headline: the screen's job is to be photographed, and "ESCANEAME"
  //          is the instruction. LINK stopped being a placeholder at P7-C2 -
  //          STR_LINK_PHASE and STR_LINK_BODY went with the frame that drew
  //          them, and only the title is left.
  STR_CREATOR_TITLE,
  STR_CREATOR_SCAN,
  STR_CREATOR_WITH_PHONE,
  STR_LINK_TITLE,
  STR_DIAG_TITLE,
  STR_DIAG_OFF,

  STR_DISP_ERR_TITLE,
  STR_DISP_ERR_A,

  // --- 33k. P2-C11d. The affordance the two-button grammar needed, the BOX
  //          screen, and one title per spec section 6 state that has no
  //          implementation yet: the screen table is COMPLETE, so every state
  //          the spec names can be entered and every one of them says what it
  //          is and which phase brings it to life.
  STR_AF_BACK_SEL,
  STR_BOX_TITLE,
  STR_BOX_EMPTY,
  STR_BOX_ACTIVE,
  STR_BOX_FULL,
  STR_BOX_ACT_VIEW,
  STR_BOX_ACT_ACTIVATE,
  STR_BOX_ACT_SWAP,
  STR_BOX_ACT_RELEASE,
  STR_BOX_ACT_TRADE,
  STR_BOX_ACT_BREED,
  STR_BOX_SWAP_PICK,
  STR_BOX_SWAP_DONE,
  STR_BOX_ACTIVATED,
  STR_BOX_NO_RELEASE_ACTIVE,
  STR_BOX_RELEASED,
  STR_BOX_REL_Q1,
  STR_BOX_REL_Q2,
  STR_SOON_TITLE,
  STR_SOON_BODY,
  STR_SOON_NETWORK,
  STR_SOON_ENCOUNTER,
  STR_SOON_CAPTURE,
  STR_BT_TITLE,          // was STR_SOON_BATTLE: P4-C4 built the screen
  STR_SOON_TRADE,
  STR_SOON_BREED,
  STR_SOON_ITEM,
  STR_SOON_SLEEP,
  STR_PHASE_4,
  STR_PHASE_5,
  STR_PHASE_6,
  STR_PHASE_7,
  STR_PHASE_10,

  // --- 34. CONTENT STRINGS, written by tools/gen_content.py ---------------
  //     Names <= 10 chars @ t0_11b; flavor lines <= 25 chars @ 5x8.
  //     EVERYTHING BETWEEN THE TWO MARKERS BELOW IS GENERATED from
  //     tools/content/*.json. Edit the JSON and re-run the generator; a hand
  //     edit here is reverted by the next run and caught by
  //     `tools/gen_content.py --check`, which the gate runs.
  // >>> GEN StrId BLOCK - tools/gen_content.py - DO NOT EDIT BY HAND >>>
  //     ONE PAIR PER SPECIES ID, in roster order, then one name per attack
  //     and one per item. SpeciesDef.name_idx / .flavor_idx, AttackDef.name_idx
  //     and ItemDef.name_idx hold these StrIds directly.
  //     Appended at the END of the enum on purpose: every id above keeps the
  //     value it already had, and ids 1..3 keep the values P3-C3 froze.
  STR_SPC_NAME_1,       // species 1, Paketo
  STR_SPC_FLAV_1,
  STR_SPC_NAME_2,       // species 2, Fragmar
  STR_SPC_FLAV_2,
  STR_SPC_NAME_3,       // species 3, Rafagón
  STR_SPC_FLAV_3,
  STR_SPC_NAME_4,       // species 4, Bippo
  STR_SPC_FLAV_4,
  STR_SPC_NAME_5,       // species 5, Estátic
  STR_SPC_FLAV_5,
  STR_SPC_NAME_6,       // species 6, Jamrón
  STR_SPC_FLAV_6,
  STR_SPC_NAME_7,       // species 7, Lagui
  STR_SPC_FLAV_7,
  STR_SPC_NAME_8,       // species 8, Jitera
  STR_SPC_FLAV_8,
  STR_SPC_NAME_9,       // species 9, Timaut
  STR_SPC_FLAV_9,
  STR_SPC_NAME_10,      // species 10, Pixio
  STR_SPC_FLAV_10,
  STR_SPC_NAME_11,      // species 11, Artefax
  STR_SPC_FLAV_11,
  STR_SPC_NAME_12,      // species 12, Burnix
  STR_SPC_FLAV_12,
  STR_SPC_NAME_13,      // species 13, Spamito
  STR_SPC_FLAV_13,
  STR_SPC_NAME_14,      // species 14, Kadenax
  STR_SPC_FLAV_14,
  STR_SPC_NAME_15,      // species 15, Blaklix
  STR_SPC_FLAV_15,
  STR_SPC_NAME_16,      // species 16, Buggo
  STR_SPC_FLAV_16,
  STR_SPC_NAME_17,      // species 17, Exploid
  STR_SPC_FLAV_17,
  STR_SPC_NAME_18,      // species 18, Rootkar
  STR_SPC_FLAV_18,
  STR_SPC_NAME_19,      // species 19, Wormi
  STR_SPC_FLAV_19,
  STR_SPC_NAME_20,      // species 20, Parasix
  STR_SPC_FLAV_20,
  STR_SPC_NAME_21,      // species 21, Plagón
  STR_SPC_FLAV_21,
  STR_SPC_NAME_22,      // species 22, Karnada
  STR_SPC_FLAV_22,
  STR_SPC_NAME_23,      // species 23, Klonix
  STR_SPC_FLAV_23,
  STR_SPC_NAME_24,      // species 24, Estafex
  STR_SPC_FLAV_24,
  STR_SPC_NAME_25,      // species 25, Nulix
  STR_SPC_FLAV_25,
  STR_SPC_NAME_26,      // species 26, Voidina
  STR_SPC_FLAV_26,
  STR_SPC_NAME_27,      // species 27, Segfalt
  STR_SPC_FLAV_27,
  STR_SPC_NAME_28,      // species 28, Bitto
  STR_SPC_FLAV_28,
  STR_SPC_NAME_29,      // species 29, Flipix
  STR_SPC_FLAV_29,
  STR_SPC_NAME_30,      // species 30, Podrix
  STR_SPC_FLAV_30,
  STR_SPC_NAME_31,      // species 31, Daemi
  STR_SPC_FLAV_31,
  STR_SPC_NAME_32,      // species 32, Servik
  STR_SPC_FLAV_32,
  STR_SPC_NAME_33,      // species 33, Kernon
  STR_SPC_FLAV_33,
  STR_SPC_NAME_34,      // species 34, Proxi
  STR_SPC_FLAV_34,
  STR_SPC_NAME_35,      // species 35, Gateón
  STR_SPC_FLAV_35,
  STR_SPC_NAME_36,      // species 36, Murax
  STR_SPC_FLAV_36,
  STR_SPC_NAME_37,      // species 37, Kachi
  STR_SPC_FLAV_37,
  STR_SPC_NAME_38,      // species 38, Memoro
  STR_SPC_FLAV_38,
  STR_SPC_NAME_39,      // species 39, Lekron
  STR_SPC_FLAV_39,
  STR_SPC_NAME_40,      // species 40, Filito
  STR_SPC_FLAV_40,
  STR_SPC_NAME_41,      // species 41, Arkivo
  STR_SPC_FLAV_41,
  STR_SPC_NAME_42,      // species 42, Zipbom
  STR_SPC_FLAV_42,
  STR_SPC_NAME_43,      // species 43, Pingo
  STR_SPC_FLAV_43,
  STR_SPC_NAME_44,      // species 44, Floodra
  STR_SPC_FLAV_44,
  STR_SPC_NAME_45,      // species 45, Denyra
  STR_SPC_FLAV_45,
  STR_SPC_NAME_46,      // species 46, Glitchi
  STR_SPC_FLAV_46,
  STR_SPC_NAME_47,      // species 47, Errox
  STR_SPC_FLAV_47,
  STR_SPC_NAME_48,      // species 48, Panika
  STR_SPC_FLAV_48,
  STR_SPC_NAME_49,      // species 49, Portu
  STR_SPC_FLAV_49,
  STR_SPC_NAME_50,      // species 50, Skanor
  STR_SPC_FLAV_50,
  STR_SPC_NAME_51,      // species 51, Bakdora
  STR_SPC_FLAV_51,
  STR_SPC_NAME_52,      // species 52, Klavik
  STR_SPC_FLAV_52,
  STR_SPC_NAME_53,      // species 53, Cifrax
  STR_SPC_FLAV_53,
  STR_SPC_NAME_54,      // species 54, Ransora
  STR_SPC_FLAV_54,
  STR_SPC_NAME_55,      // species 55, Probix
  STR_SPC_FLAV_55,
  STR_SPC_NAME_56,      // species 56, Beakon
  STR_SPC_FLAV_56,
  STR_SPC_NAME_57,      // species 57, Twinix
  STR_SPC_FLAV_57,
  STR_SPC_NAME_58,      // species 58, Cookit
  STR_SPC_FLAV_58,
  STR_SPC_NAME_59,      // species 59, Trakkar
  STR_SPC_FLAV_59,
  STR_SPC_NAME_60,      // species 60, Panoptix
  STR_SPC_FLAV_60,

  //     Attack names, data/attacks_table.h --- <= 10 chars @ t0_11b ----------
  STR_ATK_NAME_1,      // attack 1, Ping
  STR_ATK_NAME_2,      // attack 2, Pulso
  STR_ATK_NAME_3,      // attack 3, Ráfaga
  STR_ATK_NAME_4,      // attack 4, Adelanto
  STR_ATK_NAME_5,      // attack 5, Interferir
  STR_ATK_NAME_6,      // attack 6, Amplificar
  STR_ATK_NAME_7,      // attack 7, Antena
  STR_ATK_NAME_8,      // attack 8, Eco Doble
  STR_ATK_NAME_9,      // attack 9, Bytazo
  STR_ATK_NAME_10,     // attack 10, Mordisco
  STR_ATK_NAME_11,     // attack 11, Plaga
  STR_ATK_NAME_12,     // attack 12, Infectar
  STR_ATK_NAME_13,     // attack 13, Infección
  STR_ATK_NAME_14,     // attack 14, Devorar
  STR_ATK_NAME_15,     // attack 15, Corromper
  STR_ATK_NAME_16,     // attack 16, Frenesí
  STR_ATK_NAME_17,     // attack 17, Gusano
  STR_ATK_NAME_18,     // attack 18, Escaneo
  STR_ATK_NAME_19,     // attack 19, Núcleo
  STR_ATK_NAME_20,     // attack 20, Sobrecarga
  STR_ATK_NAME_21,     // attack 21, Bloqueo
  STR_ATK_NAME_22,     // attack 22, Firewall
  STR_ATK_NAME_23,     // attack 23, Cifrado
  STR_ATK_NAME_24,     // attack 24, Permisos
  STR_ATK_NAME_25,     // attack 25, Reinicio
  STR_ATK_NAME_26,     // attack 26, Pánico
  STR_ATK_NAME_27,     // attack 27, Choque
  STR_ATK_NAME_28,     // attack 28, Apuesta
  STR_ATK_NAME_29,     // attack 29, Defrag
  STR_ATK_NAME_30,     // attack 30, Caché
  STR_ATK_NAME_31,     // attack 31, Sandbox
  STR_ATK_NAME_32,     // attack 32, Overclock
  STR_ATK_NAME_33,     // attack 33, Backup
  STR_ATK_NAME_34,     // attack 34, Depurar

  //     Item names, data/items_table.h --------------------------------------
  STR_ITEM_NAME_1,     // item 1, Bit Dulce
  STR_ITEM_NAME_2,     // item 2, Byte Dulce
  STR_ITEM_NAME_3,     // item 3, Megadulce
  STR_ITEM_NAME_4,     // item 4, Cebo
  STR_ITEM_NAME_5,     // item 5, Jaula Hash
  STR_ITEM_NAME_6,     // item 6, Parche
  STR_ITEM_NAME_7,     // item 7, Antivirus
  STR_ITEM_NAME_8,     // item 8, Turbo Chip
  STR_ITEM_NAME_9,     // item 9, Llave Raíz
  STR_ITEM_NAME_10,    // item 10, Escudo RAM

  //     SPECIAL event names, data/encounter_table.h (P5-C3) -----------------
  STR_SPECIAL_NAME_1,  // special event 1, Caché Suelta
  STR_SPECIAL_NAME_2,  // special event 2, Nodo Fantasma
  STR_SPECIAL_NAME_3,  // special event 3, Núcleo Roto
  STR_SPECIAL_NAME_4,  // special event 4, Virus Errante
  // <<< GEN StrId BLOCK - tools/gen_content.py - DO NOT EDIT BY HAND <<<

  // --- 35. THE BATTLE SCREEN (P4-C4) --------------- <= 25 chars @ 5x8 ------
  //     APPENDED AFTER THE GENERATED BLOCK, and that is not a style choice:
  //     every id inside the block above is written into data/species_table.h,
  //     data/attacks_table.h and data/items_table.h by tools/gen_content.py, so
  //     inserting anything before them renumbers content the generator has
  //     already emitted and `gen_content.py --check` fails on a tree that
  //     compiles. New hand-written ids go here, at the end, for ever.
  //     The transcript words are lower case on purpose: they are read as part
  //     of a sentence ("Paketo cae"), not as labels.
  STR_BT_PRACTICE,      // the PLAY row
  STR_BT_TEST,          // the god console's row (spec section 49)
  STR_BT_ROUND,
  STR_BT_PICK,
  STR_BT_READY,
  STR_BT_SWITCH,
  STR_BT_VS,
  STR_BT_WIN,
  STR_BT_LOSE,
  STR_BT_DRAW,
  STR_BT_NO_TEAM,
  STR_BT_FULL_TEAM,
  STR_BT_ILLEGAL,
  STR_BT_START_ERR,
  STR_BT_XP,
  STR_BT_HELP,
  STR_BT_ENTER,
  STR_BT_MISS,
  STR_BT_FAINT,
  STR_BT_PROTECT,
  STR_BT_DOT,
  STR_BT_CORRUPT,
  STR_BT_STUN,
  STR_BT_CLEANSE,
  STR_BT_SKIP,
  STR_BT_ATK,
  STR_BT_DEF,
  STR_BT_SPD,
  // BO_ABORT, which game/battle.h defines as "step 1 found the state moved
  // under a submitted action" and P4-C5 maps to BATTLE_END(DESYNC). It reached
  // the panel through outcome_word()'s default arm as "Empate" until P4-C4's
  // follow-up: the one outcome that means something went wrong was the one
  // outcome the screen called an ordinary result.
  STR_BT_ABORT,

  // --- 36. EXPLORATION (P5-C3/C4) ------------------ <= 25 chars @ 5x8 ------
  //     Appended at the END for the reason section 35 gives: every id inside
  //     the generated block is written into src/data/*_table.h, so inserting
  //     anything before them renumbers content the generator has emitted.
  STR_NET_TITLE,
  STR_NET_SCANNING,
  STR_NET_NOTHING,
  STR_NET_COOLING,
  STR_NET_EMPTY,
  STR_NET_FAILED,
  STR_NET_HELP,
  STR_ENC_TITLE,
  STR_ENC_WILD,
  STR_ENC_ITEM,
  STR_ENC_SPECIAL,
  STR_ENC_CORRUPT,
  STR_ENC_CATCH,
  STR_ENC_LEAVE,
  STR_ENC_BAG_FULL,
  STR_ENC_NO_CLOCK,
  STR_ENC_HELP,
  STR_CAP_TITLE,
  STR_CAP_THROW,
  STR_CAP_NO_ITEM,
  STR_CAP_CAUGHT,
  STR_CAP_ESCAPED,
  STR_CAP_FLED,
  STR_CAP_BOX_FULL,
  STR_CAP_TO_BOX,
  STR_CAP_ERROR,
  STR_ITEM_BAG,
  STR_ITEM_BAG_EMPTY,
  STR_ITEM_USED,
  STR_ITEM_NO_USE,
  STR_ITEM_NOT_HERE,
  STR_ITEM_NO_PET,
  STR_HLP_BAG,

  // --- 33n. P7-C2/C3. THE PEER LINK (spec sections 42, 47).
  //          Two things went from this table rather than into it:
  //          STR_SO_LINK_SOON ("LINK - Fase 7") and STR_LINK_PHASE
  //          ("Enlace - Fase 7"). Both printed an INTERNAL PLAN PHASE NUMBER on
  //          a player's screen, which is a sentence about this repository and
  //          not about the game. The rest of section 33j's placeholders keep
  //          theirs because the screens they belong to really are placeholders;
  //          LINK is not one any more.
  //          Width: the card and the list are GF_BODY (25 chars), the two
  //          headers are GF_HEAD (21).
  STR_LK_SEARCH,
  STR_LK_NOBODY,
  STR_LK_FOUND,
  STR_LK_OP_BATTLE,
  STR_LK_OP_TRADE,
  STR_LK_OP_BREED,
  STR_LK_OP_CANCEL,
  STR_LK_WAIT,
  STR_LK_BOTH_A,
  STR_LK_LOST,
  STR_LK_RETRY,
  STR_LK_EXIT,
  STR_LK_ST_HELLO,
  STR_LK_ST_CAPS,
  STR_LK_ST_AGREE,
  STR_LK_ST_TEAM,
  STR_LK_ST_READY,
  STR_LK_BROKEN,
  STR_LK_ENDED,
  STR_LK_CANCELLED,
  STR_LK_NO_TEAM,
  STR_LK_TEAM_BAD,
  STR_LK_NO_CAP,
  STR_LK_RADIO_ERR,
  STR_LK_HELP,
  STR_BT_WAIT_PEER,

  // P7-C4. THE BOOT RESOLVER'S TWO ANSWERS. A trade that was interrupted by a
  // power cut is finished or undone before the player sees anything, and these
  // are how the device says which - the alternative is a Pebble silently
  // appearing or silently going.
  STR_TR_RESOLVED,
  STR_TR_ROLLED_BACK,
  // The two offer rules a player can act on. A refusal that says only "no" is
  // a refusal the player cannot answer.
  STR_LK_TR_ACTIVE,
  STR_LK_TR_QUARANTINED,
  // The third answer the boot resolver can give, and the one nobody wants:
  // the outgoing Pebble had already left and the incoming record will not
  // decode. Saying "cancelado" there would be a lie.
  STR_TR_LOST,

  // --- P10-C4. FIRST BOOT ------------------------- <= 25 chars @ 5x8 -------
  //     The three questions a fresh device asks, in app/onboarding.h's order.
  //     STR_SU_HELLO is drawn ON the naming screen rather than raised as a
  //     toast, because a greeting that covers the only line telling the player
  //     how to save is worse than no greeting - which is exactly what the boot
  //     toast did over the TIME screen before this chunk.
  //     STR_SU_DONE is 30 characters and therefore 150 px: it is the toast
  //     that PROVES the banner wraps, and it was chosen that way on purpose.
  STR_SU_HELLO,
  STR_SU_NAME_TITLE,
  STR_SU_NAME_HINT,
  STR_SU_PICK_TITLE,
  STR_SU_PICK_HINT,
  STR_SU_SKIP,
  STR_SU_DONE,
  STR_AF_CHAR,
  STR_AF_OTHER,

  // P10-C6. Using an item said "Usado" and nothing else, for every item in the
  // game: game/inventory.cpp fills a whole ItemEffect (XP, levels, how many
  // care stats moved, which status bits cleared, the battle stage and its
  // rounds, whether an evolution fired) and ui/screen_care.cpp discarded it one
  // line later. Filling five care bars from empty, curing SICK and CORRUPTED at
  // once and jumping a Pebble eight levels all read the same single word, on
  // the half of the care loop that carries the rewards from exploring.
  STR_ITEM_FED,
  STR_ITEM_CURED,
  STR_ITEM_XP,
  STR_ITEM_LEVELED,
  STR_ITEM_BOOST,
  STR_ITEM_EVOLVED,
  // ...and the corruption readout, spec section 55. The state lasted 24 h with
  // one line of onset text and no way to check it afterwards.
  STR_ST_CORRUPT,

  STR_COUNT
};

// -----------------------------------------------------------------------------
//  THE TABLE. Index-parallel to StrId above. Keep them in lockstep.
//
//  `inline constexpr`, not `static` (C++17, the pattern sprites.h already
//  uses): 359 pointers = 1,436 B of .rodata, and with internal linkage EVERY
//  translation unit that includes this header pays for its own copy. The
//  string bytes themselves are deduplicated by the linker (.rodata.str1.1 is
//  a MERGE|STRINGS section) but the pointer array is not. As an inline
//  variable it has external linkage and the linker keeps exactly one.
// -----------------------------------------------------------------------------
inline constexpr const char* const ES[] = {
  /* STR_EMPTY */                 "",

  /* --- 1. boot / system --- */
  /* STR_APP_NAME */              "PEBBLEBOL",
  /* STR_BOOT_DIZZY */            "Me he mareado un momento.",
  /* STR_BOOT_FIRST */            "Hola. Soy nuevo aquí.",

  /* --- 2. main menu ring --- */
  /* STR_MENU_PEBBLE */           "PEBBLE",
  /* STR_MENU_CARE */             "CUIDAR",
  /* STR_MENU_PLAY */             "JUGAR",
  /* STR_MENU_BOX */              "CAJA",
  /* STR_MENU_NETWORK */          "RED",
  /* STR_MENU_LINK */             "ENLACE",
  /* STR_MENU_SETTINGS */         "AJUSTES",
  /* STR_MENU_FEED */             "COMER",
  /* STR_MENU_CLEAN */            "LIMPIAR",
  /* STR_MENU_HEALTH */           "SALUD",
  /* STR_MENU_STATUS */           "ESTADO",
  /* STR_MENU_SOCIAL */           "SOCIAL",

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

  /* --- 5. life stages --- */
  /* STR_STAGE_EGG */             "HUEVO",
  /* STR_STAGE_BABY */            "BEBÉ",
  /* STR_STAGE_CHILD */           "CRÍO",
  /* STR_STAGE_TEEN */            "JOVEN",
  /* STR_STAGE_ADULT */           "ADULTO",
  /* STR_STAGE_SENIOR */          "ANCIANO",

  /* --- 6. mood faces --- */
  /* STR_MOOD_MISERIA */          "MISERIA",
  /* STR_MOOD_TRISTE */           "TRISTE",
  /* STR_MOOD_NEUTRO */           "NEUTRO",
  /* STR_MOOD_CONTENTO */         "CONTENTO",
  /* STR_MOOD_FELIZ */            "FELIZ",
  /* STR_MOOD_EUFORICO */         "EUFÓRICO",

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

  /* --- 13. action names --- */
  /* STR_ACT_NONE */              "-",
  /* STR_ACT_FEED_MEAL */         "Comida",
  /* STR_ACT_FEED_SNACK */        "Chuche",
  /* STR_ACT_CLEAN */             "Limpiar",
  /* STR_ACT_MEDICINE */          "Medicina",
  /* STR_ACT_PLAY */              "Jugar",
  /* STR_ACT_PET */               "Mimo",
  /* STR_ACT_SLEEP */             "Dormir",

  /* --- 14. reactions --- */
  /* STR_RX_MEAL */               "Mmm. Arroz. Otra vez.",
  /* STR_RX_SNACK */              "Esto sí. Esto siempre.",
  /* STR_RX_CLEAN */              "Gracias. Ya olía raro.",
  /* STR_RX_MED */                "Sabe a castigo. Sirve.",
  /* STR_RX_PLAY */               "¡Venga! A ver si puedes.",
  /* STR_RX_PET */                "Vale. No te emociones.",
  /* STR_RX_SLEEP */              "Buenas noches. Supongo.",
  /* STR_RX_WAKE */               "Estaba bien dormido.",
  /* STR_RX_EVOLVE */             "¡Estoy cambiando!",
  /* STR_RX_LEVEL_UP */           "¡Subo de nivel!",

  /* --- 15. action errors --- */
  /* STR_AERR_NONE */             "",
  /* STR_AERR_COOLDOWN */         "Espera un poco.",
  /* STR_AERR_FULL */             "Ni de broma. Estoy lleno.",
  /* STR_AERR_TIRED */            "No tengo ni para mirarte.",
  /* STR_AERR_NOT_SICK */         "No estoy enfermo. Aún.",
  /* STR_AERR_NOTHING_TODO */     "Está limpio. Bastante.",
  /* STR_AERR_ASLEEP */           "Duermo. Increíble, ¿eh?",
  /* STR_AERR_IS_EGG */           "Es un huevo. No come.",
  /* STR_AERR_BAD_ARG */          "Eso no existe.",

  /* --- 16. welcome back --- */
  /* STR_ABS_AWAY */              "¿Dónde estabas? {t}.",
  // SHORTENED AT P10-C4, AND THE SWEEP IS WHY. At 68 characters this needed
  // FOUR wrapped lines and ui/ui.cpp's draw_absence_banner() draws three into a
  // 28 px card, so "Y sé que fuiste tú." - the whole point of the line - was
  // silently dropped on the device and had been since the card was written.
  // Nothing could see it: the card is in a translation unit no host binary
  // compiles, and no test had ever measured a string against the box it is
  // drawn in. It fits three lines now with the sting intact.
  /* STR_ABS_UNKNOWN */           "No sé cuánto tiempo pasó. Sé que fue mucho. Y que fuiste tú.",

  /* --- 18. egg --- */
  /* STR_EGG_TITLE */             "HUEVO",
  /* STR_EGG_NOT_YET */           "Todavía no.",
  /* STR_EGG_COLD */              "Se enfrió. Ha cambiado.",
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
  /* STR_EV_BIRTHDAY */           "¡Feliz cumpleaños!",

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
  /* STR_ST_LEVEL */              "Nv",
  /* STR_ST_XP */                 "XP",
  /* STR_ST_HP */                 "PV",

  /* --- 25. social / BLE --- */
  /* STR_SO_TITLE */              "SOCIAL",
  /* STR_SO_SEARCHING */          "Buscando...",
  /* STR_SO_NOBODY */             "Nadie cerca. Como siempre.",
  /* STR_SO_CAP */                "Reinicia para seguir buscando.",

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
  /* STR_CF_EVOLVE */             "¿Evolucionar ahora?",

  /* --- 29. minigames --- */
  /* STR_MG_PING */               "PING",
  /* STR_MG_SEQ */                "SECUENCIA",
  /* STR_MG_FLOOD */              "PAQUETES",
  /* STR_MG_FIREWALL */           "CORTAFUEGOS",
  /* STR_MG_BUFFER */             "BUFFER",
  /* STR_MG_DELETE */             "BORRAR",
  /* STR_GM_READY */              "¿Listo?",
  /* STR_GM_GO */                 "¡YA!",
  /* STR_GM_WIN */                "¡Ganaste!",
  /* STR_GM_LOSE */               "Otra vez será.",
  /* STR_GM_SCORE */              "Puntos",
  /* STR_GM_COOLDOWN */           "Descansa un poco.",
  /* STR_GM_AF_PLAY */            "JUGAR",
  /* STR_GM_AF_PLAY_PAUSE */      "JUGAR/PAUSA",

  /* --- 30. god mode --- */
  /* STR_GOD_SPEED */             "VELOCIDAD",
  /* STR_GOD_ABSENCE */           "SALTAR AUSENCIA",
  /* STR_GOD_SETSTAT */           "FIJAR STAT",
  /* STR_GOD_STAGE */             "FORZAR ETAPA",
  /* STR_GOD_GENOME */            "GENOMA",
  /* STR_GOD_CLOCK */             "RELOJ",
  /* STR_GOD_WIPE */              "BORRAR TODO",
  /* STR_GOD_RANDOM */            "ALEATORIO",
  /* STR_GOD_EDIT */              "EDITAR",
  /* STR_GOD_DUMP */              "VOLCAR",
  /* STR_GOD_LOAD */              "CARGAR",
  /* STR_GOD_TAINTED */           "Tocado. Para siempre.",
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
  /* STR_AF_PET */                "MIMO",
  /* STR_AF_QUIT */               "SALIR",
  /* STR_AF_RUB */                "FROTAR",

  /* --- 33b. one-line help --- */
  /* STR_HLP_FEED */              "Darle de comer.",
  /* STR_HLP_MEAL */              "Llena. Engorda poco.",
  /* STR_HLP_SNACK */             "Alegra. Engorda mucho.",
  /* STR_HLP_CLEAN */             "Recoger la caca.",
  /* STR_HLP_PLAY */              "Sube el ánimo. Cansa.",
  /* STR_HLP_HEALTH */            "Medicina si está malo.",
  /* STR_HLP_STATUS */            "Cómo va por dentro.",
  /* STR_HLP_SOCIAL */            "Buscar otros bichos.",
  /* STR_HLP_BOX */               "Tus Pebbles guardados.",
  /* STR_HLP_NETWORK */           "Salir a explorar la red.",
  /* STR_HLP_SETTINGS */          "Cosas de mayores.",
  /* STR_HLP_SOUND */             "Zumbido al avisar.",
  /* STR_HLP_WEB */               "Página y QR en el móvil.",
  /* STR_HLP_BRIGHT */            "Brillo de la pantalla.",
  /* STR_HLP_INFO */              "Versión, red y memoria.",
  /* STR_HLP_RESET */             "Borra todo. Todo.",
  /* STR_HLP_PEER */              "Acércalo y espera.",
  /* STR_HLP_BACK */              "Volver sin tocar nada.",

  /* --- 33c. on-device minigames --- */
  /* STR_MG_PING_HINT */          "Pulsa el lado que brille.",
  /* STR_MG_SEQ_HINT */           "Repite la secuencia.",
  /* STR_MG_FLOOD_HINT */         "Cada paquete a su puerto.",
  /* STR_MG_FIREWALL_HINT */      "Mueve el escudo a tiempo.",
  /* STR_MG_BUFFER_HINT */        "Mantén el cursor dentro.",
  /* STR_MG_DELETE_HINT */        "Borra solo los rotos.",
  /* STR_GM_ROUND */              "Ronda",
  /* STR_GM_NEXT */               "¿Otra?",
  /* STR_GM_TOOSOON */            "Demasiado pronto.",
  /* STR_GM_REROUTE */            "Puertos cambiados.",
  /* STR_GM_WASTED */             "Carga gastada.",

  /* --- 33d. dynasty name syllables --- */
  /* A */ "Bo", "Ti", "Nu", "Ma", "Ke", "Zu", "Pi", "Ro", "La", "Ve", "Gu", "Ña",
  /* B */ "ri", "po", "tán", "fu", "sco", "lín", "ma", "zo", "que", "nel", "bi", "rrón",

  /* --- 33e. misc ui --- */
  /* STR_UI_NOBODY */             "Aquí no hay nadie.",
  /* STR_UI_GOD_HOLD */           "···",
  /* STR_UI_NO_CLOCK */           "Sin hora fiable.",
  /* STR_UI_SOON */               "Aún no está listo.",

  /* --- 33f. time entry --- */
  /* STR_SET_CLOCK */             "Poner hora",
  /* STR_CLK_TITLE */             "PONER HORA",
  /* STR_CLK_YEAR */              "Año",
  /* STR_CLK_MONTH */             "Mes",
  /* STR_CLK_DAY */               "Día",
  /* STR_CLK_HOUR */              "Hora",
  /* STR_CLK_MIN */               "Minuto",
  /* STR_CLK_HOLD */              "Mantener A: guardar",
  /* STR_CLK_SAVED */             "Hora guardada.",
  /* STR_CLK_BAD */               "Fecha imposible.",
  /* STR_HLP_CLOCK */             "Pon la fecha y la hora.",
  /* STR_AF_ADD */                "+1",

  /* --- 34a. god mode: list rows and panel titles --- */
  /* STR_GOD_AUTO */              "AUTO",
  /* STR_GOD_HEAP */              "MEMORIA",
  /* STR_GOD_RADIO */             "RADIO",
  /* STR_GOD_STORE */             "GUARDADO",
  /* STR_GOD_POWER */             "ENERGIA",
  /* STR_GOD_INFO */              "SISTEMA",
  /* STR_GOD_PERF */              "RENDIMIENTO",

  /* --- 34b. god mode: feedback lines --- */
  /* STR_GOD_PASTE */             "Pega 32 hex y ENTER.",
  /* STR_GOD_DONE */              "Hecho.",
  /* STR_GOD_FAILED */            "No se pudo.",
  /* STR_GOD_NOPET */             "No hay bicho todavía.",
  /* STR_GOD_DUMP_ON */           "Volcado serie: ON",
  /* STR_GOD_DUMP_OFF */          "Volcado serie: OFF",

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
  /* STR_GN_RARE */               "RARO",

  /* --- 33i. boot / load / save error --- */
  /* STR_BOOT_LOADING */          "Cargando la partida...",
  /* STR_SAVE_ERR_TITLE */        "ERROR DE PARTIDA",
  /* STR_SAVE_ERR_BODY */         "No he podido leerla.",
  /* STR_SAVE_ERR_NEWER */        "Es de un firmware más nuevo.",
  /* STR_SAVE_ERR_A */            "A: Recuperar",
  /* STR_SAVE_ERR_B */            "B: Reset de fábrica",
  /* STR_SAVE_NO_BACKUP */        "No hay copia que recuperar.",
  /* STR_SAVE_UPDATE_FW */        "Actualiza el firmware.",
  /* STR_SAVE_ERR_EXIT */         "A+B largo: salir",
  /* STR_SAVE_RECOVERED */        "Partida reparada.",
  /* STR_SAVE_FROM_BACKUP */      "Recuperada de la copia.",
  /* STR_SAVE_UPDATED */          "Partida actualizada.",

  /* --- 33j. Phase 2 placeholders --- */
  /* STR_CREATOR_TITLE */         "CREADOR",
  /* The spec section 34 screen: "SCAN ME / [QR] / PIN: 1234 / Scan with your
     phone". Both lines are drawn in the 62 px right-hand column at 5x8, so the
     budget here is 12 characters and not 25: "ESCANEAME" is 9 and
     "Con el movil" is 12 exactly. Neither may grow. */
  /* STR_CREATOR_SCAN */          "ESCANÉAME",
  /* STR_CREATOR_WITH_PHONE */    "Con el móvil",
  /* STR_LINK_TITLE */            "ENLACE",

  /* STR_DIAG_TITLE */            "DIAG",
  /* STR_DIAG_OFF */              "Consola apagada.",

  /* STR_DISP_ERR_TITLE */        "PANTALLA",
  /* STR_DISP_ERR_A */            "A: Reintentar",

  /* --- 33k. P2-C11d: BOX, the complete state table, the B affordance --- */
  /* STR_AF_BACK_SEL */           "ATRÁS/SEL",
  /* STR_BOX_TITLE */             "CAJA",
  /* STR_BOX_EMPTY */             "Vacío",
  /* STR_BOX_ACTIVE */            "*",
  /* STR_BOX_FULL */              "La caja está llena.",
  /* STR_BOX_ACT_VIEW */          "Ver ficha",
  /* STR_BOX_ACT_ACTIVATE */      "Sacar a pasear",
  /* STR_BOX_ACT_SWAP */          "Mover de sitio",
  /* STR_BOX_ACT_RELEASE */       "Liberar",
  /* STR_BOX_ACT_TRADE */         "Intercambiar",
  /* STR_BOX_ACT_BREED */         "Criar",
  /* STR_BOX_SWAP_PICK */         "Elige el otro hueco.",
  /* STR_BOX_SWAP_DONE */         "Hueco cambiado.",
  /* STR_BOX_ACTIVATED */         "Ahora te acompaña.",
  /* STR_BOX_NO_RELEASE_ACTIVE */ "No puedes liberar al que te acompaña.",
  /* STR_BOX_RELEASED */          "Se ha ido.",
  /* STR_BOX_REL_Q1 */            "¿Liberar a este Pebble?",
  /* STR_BOX_REL_Q2 */            "No hay vuelta atrás. ¿Seguro?",
  /* STR_SOON_TITLE */            "PRÓXIMAMENTE",
  /* STR_SOON_BODY */             "Esta parte del juego todavía no existe.",
  /* STR_SOON_NETWORK */          "RED",
  /* STR_SOON_ENCOUNTER */        "ENCUENTRO",
  /* STR_SOON_CAPTURE */          "CAPTURA",
  /* STR_BT_TITLE */              "COMBATE",
  /* STR_SOON_TRADE */            "INTERCAMBIO",
  /* STR_SOON_BREED */            "CRÍA",
  /* STR_SOON_ITEM */             "PREMIO",
  /* STR_SOON_SLEEP */            "DESCANSO",
  /* STR_PHASE_4 */               "Fase 4",
  /* STR_PHASE_5 */               "Fase 5",
  /* STR_PHASE_6 */               "Fase 6",
  /* STR_PHASE_7 */               "Fase 7",
  /* STR_PHASE_10 */              "Fase 10",

  /* --- 34. content strings, written by tools/gen_content.py --- */
  /* >>> GEN ES[] BLOCK - tools/gen_content.py - DO NOT EDIT BY HAND >>> */
  /* STR_SPC_NAME_1   */          "Paketo",
  /* STR_SPC_FLAV_1   */          "Reparte cartas sin parar.",
  /* STR_SPC_NAME_2   */          "Fragmar",
  /* STR_SPC_FLAV_2   */          "Se parte para colarse.",
  /* STR_SPC_NAME_3   */          "Rafagón",
  /* STR_SPC_FLAV_3   */          "Mil trozos, una tormenta",
  /* STR_SPC_NAME_4   */          "Bippo",
  /* STR_SPC_FLAV_4   */          "Pita bajito para saludar.",
  /* STR_SPC_NAME_5   */          "Estátic",
  /* STR_SPC_FLAV_5   */          "Zumba y no deja pensar.",
  /* STR_SPC_NAME_6   */          "Jamrón",
  /* STR_SPC_FLAV_6   */          "Apaga todas las voces.",
  /* STR_SPC_NAME_7   */          "Lagui",
  /* STR_SPC_FLAV_7   */          "Siempre llega tarde.",
  /* STR_SPC_NAME_8   */          "Jitera",
  /* STR_SPC_FLAV_8   */          "Tiembla y te descoloca.",
  /* STR_SPC_NAME_9   */          "Timaut",
  /* STR_SPC_FLAV_9   */          "Nadie responde. Nunca.",
  /* STR_SPC_NAME_10  */          "Pixio",
  /* STR_SPC_FLAV_10  */          "Solo quiere brillar.",
  /* STR_SPC_NAME_11  */          "Artefax",
  /* STR_SPC_FLAV_11  */          "Mancha todo lo que miras.",
  /* STR_SPC_NAME_12  */          "Burnix",
  /* STR_SPC_FLAV_12  */          "Se queda grabado. Siempre",
  /* STR_SPC_NAME_13  */          "Spamito",
  /* STR_SPC_FLAV_13  */          "Ofertas que no pediste.",
  /* STR_SPC_NAME_14  */          "Kadenax",
  /* STR_SPC_FLAV_14  */          "Reenvía o algo pasará.",
  /* STR_SPC_NAME_15  */          "Blaklix",
  /* STR_SPC_FLAV_15  */          "Te borra de la lista.",
  /* STR_SPC_NAME_16  */          "Buggo",
  /* STR_SPC_FLAV_16  */          "Un fallito muy cariñoso.",
  /* STR_SPC_NAME_17  */          "Exploid",
  /* STR_SPC_FLAV_17  */          "Encuentra tu punto débil.",
  /* STR_SPC_NAME_18  */          "Rootkar",
  /* STR_SPC_FLAV_18  */          "Ya vive dentro de ti.",
  /* STR_SPC_NAME_19  */          "Wormi",
  /* STR_SPC_FLAV_19  */          "Se cuela por un hueco.",
  /* STR_SPC_NAME_20  */          "Parasix",
  /* STR_SPC_FLAV_20  */          "Vive de lo que tú tienes.",
  /* STR_SPC_NAME_21  */          "Plagón",
  /* STR_SPC_FLAV_21  */          "Ya no queda nada limpio.",
  /* STR_SPC_NAME_22  */          "Karnada",
  /* STR_SPC_FLAV_22  */          "Brilla para que piques.",
  /* STR_SPC_NAME_23  */          "Klonix",
  /* STR_SPC_FLAV_23  */          "Igualito, pero no es.",
  /* STR_SPC_NAME_24  */          "Estafex",
  /* STR_SPC_FLAV_24  */          "Te vacía con una sonrisa.",
  /* STR_SPC_NAME_25  */          "Nulix",
  /* STR_SPC_FLAV_25  */          "No es nada, y ahí está.",
  /* STR_SPC_NAME_26  */          "Voidina",
  /* STR_SPC_FLAV_26  */          "Se traga lo que toca.",
  /* STR_SPC_NAME_27  */          "Segfalt",
  /* STR_SPC_FLAV_27  */          "Aquí termina el programa.",
  /* STR_SPC_NAME_28  */          "Bitto",
  /* STR_SPC_FLAV_28  */          "Un bit feliz: cero o uno.",
  /* STR_SPC_NAME_29  */          "Flipix",
  /* STR_SPC_FLAV_29  */          "Le gusta cambiar de idea.",
  /* STR_SPC_NAME_30  */          "Podrix",
  /* STR_SPC_FLAV_30  */          "Pudre datos sin prisa.",
  /* STR_SPC_NAME_31  */          "Daemi",
  /* STR_SPC_FLAV_31  */          "Trabaja mientras duermes",
  /* STR_SPC_NAME_32  */          "Servik",
  /* STR_SPC_FLAV_32  */          "Siempre en segundo plano.",
  /* STR_SPC_NAME_33  */          "Kernon",
  /* STR_SPC_FLAV_33  */          "Manda en toda la máquina.",
  /* STR_SPC_NAME_34  */          "Proxi",
  /* STR_SPC_FLAV_34  */          "Lleva recados de ida.",
  /* STR_SPC_NAME_35  */          "Gateón",
  /* STR_SPC_FLAV_35  */          "Decide quién pasa hoy.",
  /* STR_SPC_NAME_36  */          "Murax",
  /* STR_SPC_FLAV_36  */          "Nada entra sin permiso.",
  /* STR_SPC_NAME_37  */          "Kachi",
  /* STR_SPC_FLAV_37  */          "Guarda tus cosas cerca.",
  /* STR_SPC_NAME_38  */          "Memoro",
  /* STR_SPC_FLAV_38  */          "Se acuerda de todo.",
  /* STR_SPC_NAME_39  */          "Lekron",
  /* STR_SPC_FLAV_39  */          "Pide más y nunca suelta.",
  /* STR_SPC_NAME_40  */          "Filito",
  /* STR_SPC_FLAV_40  */          "Un archivo con nombre.",
  /* STR_SPC_NAME_41  */          "Arkivo",
  /* STR_SPC_FLAV_41  */          "Guarda mil cosas dentro.",
  /* STR_SPC_NAME_42  */          "Zipbom",
  /* STR_SPC_FLAV_42  */          "Se abre y ya no cabe.",
  /* STR_SPC_NAME_43  */          "Pingo",
  /* STR_SPC_FLAV_43  */          "Solo quiere respuesta.",
  /* STR_SPC_NAME_44  */          "Floodra",
  /* STR_SPC_FLAV_44  */          "Pregunta mil veces.",
  /* STR_SPC_NAME_45  */          "Denyra",
  /* STR_SPC_FLAV_45  */          "Nadie más podrá entrar.",
  /* STR_SPC_NAME_46  */          "Glitchi",
  /* STR_SPC_FLAV_46  */          "Baila donde no debería.",
  /* STR_SPC_NAME_47  */          "Errox",
  /* STR_SPC_FLAV_47  */          "Rompe cosas sin querer.",
  /* STR_SPC_NAME_48  */          "Panika",
  /* STR_SPC_FLAV_48  */          "El sistema se rinde.",
  /* STR_SPC_NAME_49  */          "Portu",
  /* STR_SPC_FLAV_49  */          "Una puertecita educada.",
  /* STR_SPC_NAME_50  */          "Skanor",
  /* STR_SPC_FLAV_50  */          "Prueba todas las puertas.",
  /* STR_SPC_NAME_51  */          "Bakdora",
  /* STR_SPC_FLAV_51  */          "Deja una puerta abierta.",
  /* STR_SPC_NAME_52  */          "Klavik",
  /* STR_SPC_FLAV_52  */          "Guarda tu secreto bien.",
  /* STR_SPC_NAME_53  */          "Cifrax",
  /* STR_SPC_FLAV_53  */          "Nadie lee lo que escribe.",
  /* STR_SPC_NAME_54  */          "Ransora",
  /* STR_SPC_FLAV_54  */          "Te lo devuelve si pagas.",
  /* STR_SPC_NAME_55  */          "Probix",
  /* STR_SPC_FLAV_55  */          "¿Hay alguien ahí?",
  /* STR_SPC_NAME_56  */          "Beakon",
  /* STR_SPC_FLAV_56  */          "Grita su nombre sin parar",
  /* STR_SPC_NAME_57  */          "Twinix",
  /* STR_SPC_FLAV_57  */          "Se hace pasar por tu red.",
  /* STR_SPC_NAME_58  */          "Cookit",
  /* STR_SPC_FLAV_58  */          "Una miga que te sigue.",
  /* STR_SPC_NAME_59  */          "Trakkar",
  /* STR_SPC_FLAV_59  */          "Sabe por dónde has ido.",
  /* STR_SPC_NAME_60  */          "Panoptix",
  /* STR_SPC_FLAV_60  */          "Lo ve todo, siempre.",
  /* STR_ATK_NAME_1   */          "Ping",
  /* STR_ATK_NAME_2   */          "Pulso",
  /* STR_ATK_NAME_3   */          "Ráfaga",
  /* STR_ATK_NAME_4   */          "Adelanto",
  /* STR_ATK_NAME_5   */          "Interferir",
  /* STR_ATK_NAME_6   */          "Amplificar",
  /* STR_ATK_NAME_7   */          "Antena",
  /* STR_ATK_NAME_8   */          "Eco Doble",
  /* STR_ATK_NAME_9   */          "Bytazo",
  /* STR_ATK_NAME_10  */          "Mordisco",
  /* STR_ATK_NAME_11  */          "Plaga",
  /* STR_ATK_NAME_12  */          "Infectar",
  /* STR_ATK_NAME_13  */          "Infección",
  /* STR_ATK_NAME_14  */          "Devorar",
  /* STR_ATK_NAME_15  */          "Corromper",
  /* STR_ATK_NAME_16  */          "Frenesí",
  /* STR_ATK_NAME_17  */          "Gusano",
  /* STR_ATK_NAME_18  */          "Escaneo",
  /* STR_ATK_NAME_19  */          "Núcleo",
  /* STR_ATK_NAME_20  */          "Sobrecarga",
  /* STR_ATK_NAME_21  */          "Bloqueo",
  /* STR_ATK_NAME_22  */          "Firewall",
  /* STR_ATK_NAME_23  */          "Cifrado",
  /* STR_ATK_NAME_24  */          "Permisos",
  /* STR_ATK_NAME_25  */          "Reinicio",
  /* STR_ATK_NAME_26  */          "Pánico",
  /* STR_ATK_NAME_27  */          "Choque",
  /* STR_ATK_NAME_28  */          "Apuesta",
  /* STR_ATK_NAME_29  */          "Defrag",
  /* STR_ATK_NAME_30  */          "Caché",
  /* STR_ATK_NAME_31  */          "Sandbox",
  /* STR_ATK_NAME_32  */          "Overclock",
  /* STR_ATK_NAME_33  */          "Backup",
  /* STR_ATK_NAME_34  */          "Depurar",
  /* STR_ITEM_NAME_1   */         "Bit Dulce",
  /* STR_ITEM_NAME_2   */         "Byte Dulce",
  /* STR_ITEM_NAME_3   */         "Megadulce",
  /* STR_ITEM_NAME_4   */         "Cebo",
  /* STR_ITEM_NAME_5   */         "Jaula Hash",
  /* STR_ITEM_NAME_6   */         "Parche",
  /* STR_ITEM_NAME_7   */         "Antivirus",
  /* STR_ITEM_NAME_8   */         "Turbo Chip",
  /* STR_ITEM_NAME_9   */         "Llave Raíz",
  /* STR_ITEM_NAME_10  */         "Escudo RAM",
  /* STR_SPECIAL_NAME_1   */      "Caché Suelta",
  /* STR_SPECIAL_NAME_2   */      "Nodo Fantasma",
  /* STR_SPECIAL_NAME_3   */      "Núcleo Roto",
  /* STR_SPECIAL_NAME_4   */      "Virus Errante"
  /* <<< GEN ES[] BLOCK - tools/gen_content.py - DO NOT EDIT BY HAND <<< */

  /* --- 35. the battle screen (P4-C4) --- */
  //  THE LEADING COMMA IS LOAD-BEARING. tools/gen_content.py emits the block
  //  above with NO trailing comma after its last entry, and splices only the
  //  lines BETWEEN the two markers - so the comma that joins the generated half
  //  to this one has to live on this side of the marker or the next regeneration
  //  would delete it.
  , /* STR_BT_PRACTICE */         "COMBATE DE PRÁCTICA"
  , /* STR_BT_TEST */             "COMBATE PRUEBA"
  , /* STR_BT_ROUND */            "RONDA"
  , /* STR_BT_PICK */             "ELIGE EQUIPO"
  , /* STR_BT_READY */            "LISTO"
  , /* STR_BT_SWITCH */           "CAMBIAR"
  , /* STR_BT_VS */               "¡A COMBATIR!"
  , /* STR_BT_WIN */              "¡HAS GANADO!"
  , /* STR_BT_LOSE */             "Has perdido"
  , /* STR_BT_DRAW */             "Empate"
  , /* STR_BT_NO_TEAM */          "Elige al menos un Pebble"
  , /* STR_BT_FULL_TEAM */        "El equipo ya está lleno"
  , /* STR_BT_ILLEGAL */          "Ahora no puedes"
  , /* STR_BT_START_ERR */        "No se puede combatir"
  , /* STR_BT_XP */               "¡Victoria! Ganas XP"
  , /* STR_BT_HELP */             "A elige, B vuelve. Cambiar cuesta el turno."
  , /* STR_BT_ENTER */            "entra"
  , /* STR_BT_MISS */             "falla"
  , /* STR_BT_FAINT */            "cae"
  , /* STR_BT_PROTECT */          "se protege"
  , /* STR_BT_DOT */              "daño"
  , /* STR_BT_CORRUPT */          "corrupto"
  , /* STR_BT_STUN */             "aturdido"
  , /* STR_BT_CLEANSE */          "limpio"
  , /* STR_BT_SKIP */             "pierde el turno"
  , /* STR_BT_ATK */              "ATQ"
  , /* STR_BT_DEF */              "DEF"
  , /* STR_BT_SPD */              "VEL"
  , /* STR_BT_ABORT */            "Combate anulado"

  , /* STR_NET_TITLE */           "RED"
  , /* STR_NET_SCANNING */        "ESCANEANDO"
  , /* STR_NET_NOTHING */         "Aquí no hay nada"
  , /* STR_NET_COOLING */         "Ya exploraste estas redes"
  , /* STR_NET_EMPTY */           "No se ve ninguna red"
  , /* STR_NET_FAILED */          "El escáner ha fallado"
  , /* STR_NET_HELP */            "B cancela el escaneo y apaga la radio."
  , /* STR_ENC_TITLE */           "ENCUENTRO"
  , /* STR_ENC_WILD */            "¡PEBBLE SALVAJE!"
  , /* STR_ENC_ITEM */            "¡HAS ENCONTRADO ALGO!"
  , /* STR_ENC_SPECIAL */         "¡ALGO RARO PASA!"
  , /* STR_ENC_CORRUPT */         "Pebble corrompido 24 h"
  , /* STR_ENC_CATCH */           "CAPTURAR"
  , /* STR_ENC_LEAVE */           "DEJAR"
  , /* STR_ENC_BAG_FULL */        "La mochila está llena"
  , /* STR_ENC_NO_CLOCK */        "Pon la fecha primero"
  , /* STR_ENC_HELP */            "A elige, B vuelve. Dejarlo ir también vale."
  , /* STR_CAP_TITLE */           "CAPTURA"
  , /* STR_CAP_THROW */           "LANZAR"
  , /* STR_CAP_NO_ITEM */         "Sin cebo"
  , /* STR_CAP_CAUGHT */          "¡Capturado! Va a la caja."
  , /* STR_CAP_ESCAPED */         "Se ha escapado. Prueba otra vez."
  , /* STR_CAP_FLED */            "Ha huido."
  , /* STR_CAP_BOX_FULL */        "La caja está llena. Haz sitio."
  , /* STR_CAP_TO_BOX */          "CAJA"
  , /* STR_CAP_ERROR */           "Algo ha ido mal aquí dentro."
  , /* STR_ITEM_BAG */            "MOCHILA"
  , /* STR_ITEM_BAG_EMPTY */      "No llevas nada"
  , /* STR_ITEM_USED */           "Usado"
  , /* STR_ITEM_NO_USE */         "Ahora no hace nada"
  , /* STR_ITEM_NOT_HERE */       "Esto se usa al capturar"
  , /* STR_ITEM_NO_PET */         "No hay Pebble activo"
  , /* STR_HLP_BAG */             "Lo que has encontrado explorando."

  /* --- 33n. the peer link (P7-C2/C3) --- */
  , /* STR_LK_SEARCH */            "Buscando Pebbles..."
  , /* STR_LK_NOBODY */            "Nadie cerca todavía"
  , /* STR_LK_FOUND */             "¡PEBBLEBOL ENCONTRADO!"
  , /* STR_LK_OP_BATTLE */         "COMBATE"
  , /* STR_LK_OP_TRADE */          "INTERCAMBIO"
  , /* STR_LK_OP_BREED */          "CRIAR"
  , /* STR_LK_OP_CANCEL */         "CANCELAR"
  , /* STR_LK_WAIT */              "Esperando al otro"
  , /* STR_LK_BOTH_A */            "Pulsad A los dos"
  , /* STR_LK_LOST */              "CONEXIÓN PERDIDA"
  , /* STR_LK_RETRY */             "A: Reintentar"
  , /* STR_LK_EXIT */              "B: Salir"
  , /* STR_LK_ST_HELLO */          "Saludo"
  , /* STR_LK_ST_CAPS */           "Versiones"
  , /* STR_LK_ST_AGREE */          "Acordando"
  , /* STR_LK_ST_TEAM */           "Equipos"
  , /* STR_LK_ST_READY */          "Listo"
  , /* STR_LK_BROKEN */            "Enlace interrumpido"
  , /* STR_LK_ENDED */             "Enlace terminado"
  , /* STR_LK_CANCELLED */         "Enlace cancelado"
  , /* STR_LK_NO_TEAM */           "No tienes ningún Pebble"
  , /* STR_LK_TEAM_BAD */          "Tu equipo no es válido"
  , /* STR_LK_NO_CAP */            "El otro no puede eso"
  , /* STR_LK_RADIO_ERR */         "Radio no disponible"
  , /* STR_LK_HELP */              "Acerca otro Pebblebol y pulsad A en los dos."
  , /* STR_BT_WAIT_PEER */         "Esperando al rival"
  , /* STR_TR_RESOLVED */          "Intercambio completado"
  , /* STR_TR_ROLLED_BACK */       "Intercambio cancelado"
  , /* STR_LK_TR_ACTIVE */         "Guarda ese Pebble primero"
  , /* STR_LK_TR_QUARANTINED */    "Ese Pebble no es válido"
  , /* STR_TR_LOST */              "Intercambio incompleto"
  // --- P10-C4. FIRST BOOT ---------------------------------------------------
  , /* STR_SU_HELLO */             "Hola. Empecemos."
  , /* STR_SU_NAME_TITLE */        "PONLE NOMBRE"
  , /* STR_SU_NAME_HINT */         "Mantener A: aceptar"
  , /* STR_SU_PICK_TITLE */        "ELIGE PEBBLE"
  , /* STR_SU_PICK_HINT */         "Mantener A: elegir"
  , /* STR_SU_SKIP */              "A+B: saltar"
  , /* STR_SU_DONE */              "Listo. Encantado de conocerte."
  , /* STR_AF_CHAR */              "LETRA"
  , /* STR_AF_OTHER */             "OTRO"
  // P10-C6, the item reactions. One per ItemEffect shape rather than one for
  // all of them; ui/screen_care.cpp's item_reaction() is a pure function of the
  // struct and tests/test_screens.cpp drives every arm.
  , /* STR_ITEM_FED */            "Se encuentra mejor"
  , /* STR_ITEM_CURED */          "Ya está curado"
  , /* STR_ITEM_XP */             "Ha ganado experiencia"
  , /* STR_ITEM_LEVELED */        "¡Ha subido de nivel!"
  , /* STR_ITEM_BOOST */          "Listo para el combate"
  , /* STR_ITEM_EVOLVED */        "¡Algo está cambiando!"
  , /* STR_ST_CORRUPT */          "Corrupto"
};

// -----------------------------------------------------------------------------
//  ACCESSORS
// -----------------------------------------------------------------------------
#define S(id)          (ES[(uint16_t)(id)])

#define S_STAT(s)      (ES[STR_STAT_HUNGER    + (uint16_t)(s)])   // StatId
#define S_STAGE(s)     (ES[STR_STAGE_EGG      + (uint16_t)(s)])   // Stage
#define S_MOOD(m)      (ES[STR_MOOD_MISERIA   + (uint16_t)(m)])   // Mood
#define S_SPECIES(v)   (ES[STR_SPECIES_00     + (uint16_t)((v) & 0x0F)])
#define S_PATTERN(v)   (ES[STR_PATTERN_00     + (uint16_t)((v) & 0x0F)])
#define S_TEMPER(c)    (ES[STR_TEMPER_SOLAR   + (uint16_t)(c)])   // Temperament class
#define S_ACTION(a)    (ES[STR_ACT_NONE       + (uint16_t)(a)])   // ActionId
#define S_AERR(e)      (ES[STR_AERR_NONE      + (uint16_t)(e)])   // ActionErr
#define S_ALERT(a)     (ES[STR_AL_NONE        + (uint16_t)(a)])   // AlertId
#define S_WISH(w)      (ES[STR_WISH_NONE      + (uint16_t)(w)])   // WishId
#define S_MENU(i)      (ES[STR_MENU_PEBBLE    + (uint16_t)(i)])   // 0..6
#define S_SYL_A(i)     (ES[STR_SYL_A00        + (uint16_t)((i) % 12u)])  // name syllable 1
#define S_SYL_B(i)     (ES[STR_SYL_B00        + (uint16_t)((i) % 12u)])  // name syllable 2

// -----------------------------------------------------------------------------
//  GUARDS. If any of these fires, the table and the enums drifted apart.
// -----------------------------------------------------------------------------
static_assert(NT_ARRAY_LEN(ES) == (size_t)STR_COUNT,
              "strings_es.h: ES[] and StrId are out of sync");

static_assert(STR_STAT_BOND       - STR_STAT_HUNGER + 1 == (int)ST_COUNT,      "stat labels");
static_assert(STR_STAGE_SENIOR    - STR_STAGE_EGG   + 1 == (int)STAGE_COUNT,   "stage names");
static_assert(STR_MOOD_EUFORICO   - STR_MOOD_MISERIA+ 1 == (int)MOOD_COUNT,    "mood names");
static_assert(STR_SPECIES_15      - STR_SPECIES_00  + 1 == SPECIES_COUNT,      "species names");
static_assert(STR_PATTERN_15      - STR_PATTERN_00  + 1 == PATTERN_COUNT,      "pattern names");
static_assert(STR_TEMPER_GOTICO   - STR_TEMPER_SOLAR+ 1 == (int)TEMPER_COUNT,  "temperament names");
static_assert(STR_ACT_SLEEP       - STR_ACT_NONE    + 1 == (int)ACT_COUNT,     "action names");
static_assert(STR_AERR_BAD_ARG    - STR_AERR_NONE   + 1 == (int)AERR_COUNT,    "action errors");
static_assert(STR_AL_MATE_FOUND   - STR_AL_NONE     + 1 == (int)AL_COUNT,      "alert lines");
static_assert(STR_WISH_PET3       - STR_WISH_NONE   + 1 == (int)WISH_COUNT,    "wish lines");
static_assert(STR_MENU_SETTINGS   - STR_MENU_PEBBLE + 1 == MENU_ITEM_COUNT,    "menu labels");
static_assert(STR_SYL_A11         - STR_SYL_A00     + 1 == 12,                 "name syllables A");
static_assert(STR_SYL_B11         - STR_SYL_B00     + 1 == 12,                 "name syllables B");
// 19 -> 18: STR_HLP_LIGHT went with the light mechanic (P3-C2b).
static_assert(STR_HLP_BACK        - STR_HLP_FEED    + 1 == 18,                 "ui help block");
static_assert(STR_AF_ADD          - STR_SET_CLOCK   + 1 == 12,                 "time entry block");
// The six minigame names and the six hints are index-parallel to MgId, which is
// what lets ui/screen_care.cpp build the PLAY list by row index alone. Written
// against the literal 6 rather than MG_ID_COUNT because this header is included
// by translation units that never see the minigame contract; screen_care.cpp
// then folds these two blocks against MgId itself, which is where a row in the
// wrong PLACE (rather than a wrong count) becomes a build error.
static_assert(STR_MG_DELETE       - STR_MG_PING      + 1 == 6,                 "minigame names");
static_assert(STR_MG_DELETE_HINT  - STR_MG_PING_HINT + 1 == 6,                 "minigame hints");

#endif // NT_STRINGS_ES_H
