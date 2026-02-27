#pragma once
#include <lvgl.h>
#include "lgfx_config.h"

// Initialise display hardware and LVGL. Call once in setup().
void lvgl_port_init();

// Drive LVGL timers, rendering, and backlight flash. Call every loop() iteration.
void lvgl_port_tick();

// Trigger a non-blocking backlight flash (fires `flashes` on/off cycles).
// Driven automatically by lvgl_port_tick() — no extra loop() call needed.
void display_alert_flash(uint8_t flashes);

// Access the LVGL display handle (e.g. to set theme).
lv_display_t* lvgl_get_disp();
