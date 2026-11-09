// Copyright © Charlie Howard 2026 All rights reserved
//
// LVGL memory hooks: prefer PSRAM, fall back to internal RAM.
#pragma once
#include <stddef.h>
#include "esp_heap_caps.h"

static inline void* lv_psram_malloc(size_t size) {
  void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : heap_caps_malloc(size, MALLOC_CAP_8BIT);
}
static inline void lv_psram_free(void* p) { heap_caps_free(p); }
static inline void* lv_psram_realloc(void* p, size_t size) {
  void* n = heap_caps_realloc(p, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return n ? n : heap_caps_realloc(p, size, MALLOC_CAP_8BIT);
}
