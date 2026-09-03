// =============================================================================
//  PEBBLEBOL - ui/screen_boot.cpp
//  BOOT and LOAD_SAVE: the two frames the device shows before it knows
//  anything. Migrated into the screen table by P2-C11a.
//
//  Both are drawable BEFORE ui_begin() - they read no pet and no config -
//  because that is exactly when they are needed: the save pipeline runs
//  between them (app.cpp calls ui_boot_screen()).
//
//  PURE translation unit: gfx.h only, no render.h, no Arduino.h. That is what
//  lets tests/test_screens.cpp render these two on the host and compare them
//  against tests/golden/screens/*.pbm at the real 128x64.
// =============================================================================
#include "screen_boot.h"

#include "../core/strings_es.h"
#include "gfx.h"

// -----------------------------------------------------------------------------
//  BOOT - the splash. Product name over the firmware version, nothing else:
//  every I/O this device does happens after this frame is already on the panel.
// -----------------------------------------------------------------------------
void boot_render(void) {
  gfx_text_center(GF_HEAD, 28, S(STR_APP_NAME));
  gfx_text_center(GF_TINY, 40, FW_VERSION);
}

// -----------------------------------------------------------------------------
//  LOAD_SAVE - the same splash with the pipeline's own line under it, so a
//  slow read (a migration, a pair recovery, a checkpoint restore) never looks
//  like a hang.
// -----------------------------------------------------------------------------
void load_save_render(void) {
  gfx_text_center(GF_HEAD, 28, S(STR_APP_NAME));
  gfx_text_center(GF_BODY, 42, S(STR_BOOT_LOADING));
}
