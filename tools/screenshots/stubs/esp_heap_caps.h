// Copyright © Charlie Howard 2026 All rights reserved
#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT 0
#define MALLOC_CAP_INTERNAL 0
#define MALLOC_CAP_DMA 0
static inline void* heap_caps_malloc(size_t n, unsigned caps) { (void)caps; return malloc(n); }
static inline void* heap_caps_realloc(void* p, size_t n, unsigned caps) { (void)caps; return realloc(p, n); }
static inline void heap_caps_free(void* p) { free(p); }
