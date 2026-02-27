/**
 * @file lvgl_mem.cpp
 * LVGL v9 LV_STDLIB_CUSTOM allocator — routes all LVGL heap to PSRAM.
 *
 * LVGL calls lv_malloc_core / lv_free_core / lv_realloc_core as external C
 * symbols when LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM.  Placing LVGL's heap
 * in PSRAM (8 MB) frees ~144 KB of DRAM for the WiFi stack and MbedTLS TLS
 * buffers, fixing the "esp-sha: Failed to allocate buf memory" error.
 *
 * Fallback to internal DRAM if PSRAM is exhausted (shouldn't happen with 8 MB).
 */
#include <stddef.h>
#include <esp_heap_caps.h>
#include <lvgl.h>   /* lv_mem_monitor_t, lv_result_t */

extern "C" {

void lv_mem_init(void)   { /* nothing — PSRAM is always available */ }
void lv_mem_deinit(void) { /* nothing */ }

void * lv_malloc_core(size_t size) {
    void * p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_malloc(size, MALLOC_CAP_8BIT);  /* DRAM fallback */
    return p;
}

void lv_free_core(void * p) {
    if (p) heap_caps_free(p);
}

void * lv_realloc_core(void * p, size_t new_size) {
    void * np = heap_caps_realloc(p, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!np && new_size > 0) np = heap_caps_realloc(p, new_size, MALLOC_CAP_8BIT);
    return np;
}

void lv_mem_monitor_core(lv_mem_monitor_t * mon_p) {
    if (mon_p) {
        /* Zero all fields, then fill what we can from heap_caps stats */
        *mon_p = lv_mem_monitor_t{};
        mon_p->total_size        = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
        mon_p->free_size         = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        mon_p->free_biggest_size = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    }
}

lv_result_t lv_mem_test_core(void) {
    return LV_RESULT_OK;
}

} /* extern "C" */
