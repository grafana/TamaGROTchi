#include "lvgl_port.h"
#include "../config.h"
#include <Arduino.h>

// Global display instance (referenced by lgfx_config.h extern)
LGFX gfx;

static lv_display_t* _disp     = nullptr;
static lv_color_t*   _buf1     = nullptr;
static lv_color_t*   _buf2     = nullptr;

// Backlight flash state machine
static uint8_t  _flash_remain  = 0;
static uint32_t _flash_next_ms = 0;
static bool     _flash_bright  = false;

// =============================================================================
// LVGL flush callback — writes a rendered rectangle to the ST7789V2 via LGFX
// =============================================================================
static void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;

    gfx.startWrite();
    gfx.setAddrWindow(area->x1, area->y1, w, h);
    gfx.writePixels(reinterpret_cast<lgfx::rgb565_t*>(px_map), w * h);
    gfx.endWrite();

    lv_display_flush_ready(disp);
}

// =============================================================================
// lvgl_port_init — call once in setup() after Serial.begin()
// =============================================================================
void lvgl_port_init(bool bgr_order) {
    // Force backlight pin HIGH before init — failsafe if LEDC setup fails
    pinMode(PIN_LCD_BL, OUTPUT);
    digitalWrite(PIN_LCD_BL, HIGH);

    // Bring up display
    gfx.setBgrOrder(bgr_order);
    gfx.init();
    gfx.setRotation(0);
    gfx.setBrightness(LCD_BL_NORMAL);
    gfx.fillScreen(TFT_BLACK);

    // Initialise LVGL
    lv_init();

    // Use millis() as the tick source (LVGL v9 API).
    // millis() returns unsigned long; lv_tick_get_cb_t expects uint32_t (*)()
    lv_tick_set_cb([]() -> uint32_t { return (uint32_t)millis(); });

    // Allocate double draw buffers in PSRAM — ps_malloc() keeps heap free
    const size_t buf_size = LCD_WIDTH * LV_BUF_LINES * sizeof(lv_color_t);
    _buf1 = reinterpret_cast<lv_color_t*>(ps_malloc(buf_size));
    _buf2 = reinterpret_cast<lv_color_t*>(ps_malloc(buf_size));

    if (!_buf1 || !_buf2) {
        // Fallback to internal heap if PSRAM unavailable
        if (!_buf1) _buf1 = reinterpret_cast<lv_color_t*>(malloc(buf_size));
        if (!_buf2) _buf2 = reinterpret_cast<lv_color_t*>(malloc(buf_size));
    }

    // Create LVGL display (v9 API)
    _disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
    lv_display_set_flush_cb(_disp, flush_cb);
    lv_display_set_buffers(_disp, _buf1, _buf2, buf_size,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    Serial.printf("[display] LVGL v%d.%d.%d init OK — buf=%lu bytes each\n",
                  LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH,
                  (unsigned long)buf_size);
}

// =============================================================================
// display_alert_flash — trigger N backlight on/off cycles (non-blocking)
// =============================================================================
void display_alert_flash(uint8_t flashes) {
    _flash_remain  = flashes * 2;  // each flash = 1 bright half + 1 dark half
    _flash_bright  = true;
    _flash_next_ms = millis();
}

// =============================================================================
// lvgl_port_tick — call every loop() to service LVGL timers and rendering
// =============================================================================
void lvgl_port_tick() {
    lv_timer_handler();

    // Advance backlight flash state machine
    if (_flash_remain > 0) {
        uint32_t now = millis();
        if (now >= _flash_next_ms) {
            gfx.setBrightness(_flash_bright ? 255 : 0);
            _flash_bright  = !_flash_bright;
            _flash_remain--;
            _flash_next_ms = now + 80;
            if (_flash_remain == 0) {
                gfx.setBrightness(LCD_BL_NORMAL);  // restore normal brightness
            }
        }
    }
}

void lvgl_port_set_brightness(uint8_t b) {
    // Don't override brightness mid-flash — the flash state machine restores
    // LCD_BL_NORMAL itself; the caller should re-apply after the flash ends.
    if (_flash_remain == 0) {
        gfx.setBrightness(b);
    }
}

lv_display_t* lvgl_get_disp() {
    return _disp;
}
