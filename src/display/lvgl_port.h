#pragma once
#include <lvgl.h>
#include "lgfx_config.h"

// Initialise display hardware and LVGL. Call once in setup().
// `bgr_order` = true swaps R/B channels for panel variants wired BGR.
void lvgl_port_init(bool bgr_order = false);

// Drive LVGL timers, rendering, and backlight flash. Call every loop() iteration.
void lvgl_port_tick();

// Trigger a non-blocking backlight flash (fires `flashes` on/off cycles).
// Driven automatically by lvgl_port_tick() — no extra loop() call needed.
void display_alert_flash(uint8_t flashes);

// Set backlight brightness (0–255). Call after lvgl_port_init().
void lvgl_port_set_brightness(uint8_t b);

// Access the LVGL display handle (e.g. to set theme).
lv_display_t* lvgl_get_disp();
