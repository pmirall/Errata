// =============================================================================
//  Pebblebol host tests - fakes/alloc_count.cpp
//  See fakes/alloc_count.h. Kept in its own translation unit on purpose.
// =============================================================================
#include "alloc_count.h"

#include <stdlib.h>

static uint32_t s_count = 0;
static bool     s_armed = true;      // on by default; a case narrows the window

void     alloc_count_arm(bool on)  { s_armed = on; }
uint32_t alloc_count(void)         { return s_count; }
void     alloc_count_reset(void)   { s_count = 0; }

void* operator new(size_t n) {
  if (s_armed) ++s_count;
  void* p = malloc(n ? n : 1);
  if (!p) abort();
  return p;
}
void* operator new[](size_t n) {
  if (s_armed) ++s_count;
  void* p = malloc(n ? n : 1);
  if (!p) abort();
  return p;
}
void operator delete(void* p) noexcept        { free(p); }
void operator delete[](void* p) noexcept      { free(p); }
void operator delete(void* p, size_t) noexcept   { free(p); }
void operator delete[](void* p, size_t) noexcept { free(p); }

// The escape hatch alloc_count_probe() writes through; see the header.
void* g_alloc_probe_sink = nullptr;
