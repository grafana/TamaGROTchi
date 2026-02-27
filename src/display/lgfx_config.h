#pragma once
#include <LovyanGFX.hpp>
#include "../config.h"

// =============================================================================
// LGFX — LovyanGFX configuration for Waveshare ESP32-S3-Touch-LCD-1.69
//
// Display: ST7789V2, 240×280 pixels (controller VRAM is 240×320)
// SPI: SPI2_HOST at 80 MHz
// Panel visible window: offset_y=20 (rows 20-299 of the 320-row VRAM)
// =============================================================================

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789 _panel;
    lgfx::Bus_SPI      _bus;
    lgfx::Light_PWM    _bl;

public:
    LGFX() {
        // ---- SPI bus --------------------------------------------------------
        {
            auto cfg = _bus.config();
            cfg.spi_host     = SPI2_HOST;        // ESP32-S3: use SPI2_HOST (not VSPI_HOST)
            cfg.spi_mode     = 0;
            cfg.freq_write   = 40000000UL;       // 40 MHz — safer initial value
            cfg.freq_read    = 6000000UL;
            cfg.spi_3wire    = false;            // use 4-wire; pin_miso=-1 disables MISO
            cfg.use_lock     = true;
            cfg.dma_channel  = SPI_DMA_CH_AUTO;
            cfg.pin_sclk     = PIN_LCD_SCLK;
            cfg.pin_mosi     = PIN_LCD_MOSI;
            cfg.pin_miso     = -1;
            cfg.pin_dc       = PIN_LCD_DC;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }

        // ---- Panel ----------------------------------------------------------
        {
            auto cfg = _panel.config();
            cfg.pin_cs       = PIN_LCD_CS;
            cfg.pin_rst      = PIN_LCD_RST;
            cfg.pin_busy     = -1;

            // Physical panel is 240×280 but ST7789V2 VRAM is 240×320.
            // The visible 280 rows start at VRAM row 20.
            cfg.panel_width  = LCD_WIDTH;   // 240
            cfg.panel_height = LCD_HEIGHT;  // 280
            cfg.memory_width = 240;
            cfg.memory_height= 320;
            cfg.offset_x     = 0;
            cfg.offset_y     = 20;          // VRAM offset for 280-row panel
            cfg.offset_rotation = 0;

            cfg.invert       = true;        // ST7789V2 requires colour inversion
            cfg.rgb_order    = true;        // RGB (panel is RGB order; false would swap R↔B)
            cfg.readable     = false;
            cfg.bus_shared   = false;
            _panel.config(cfg);
        }

        // ---- Backlight (LEDC channel 1) -------------------------------------
        {
            auto cfg = _bl.config();
            cfg.pin_bl      = PIN_LCD_BL;
            cfg.invert      = false;
            cfg.freq        = 44100;
            cfg.pwm_channel = 1;            // channel 0 reserved for buzzer
            _bl.config(cfg);
            _panel.setLight(&_bl);
        }

        setPanel(&_panel);
    }
};

extern LGFX gfx;   // defined in lvgl_port.cpp
