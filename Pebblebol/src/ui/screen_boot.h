// =============================================================================
//  PEBBLEBOL - ui/screen_boot.h
//  The render hooks of the two pre-state screens, for ui/screen_table.cpp and
//  for the host snapshot test. Neither has input, an update or a cursor: they
//  are frames, not states with behaviour.
// =============================================================================
#ifndef NT_SCREEN_BOOT_H
#define NT_SCREEN_BOOT_H

void boot_render(void);
void load_save_render(void);

#endif  // NT_SCREEN_BOOT_H
