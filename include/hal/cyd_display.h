#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// Hardware Pin Definitions for CYD ESP32-2432S028R
#define CYD_LCD_BL     21
#define CYD_LED_RED    4
#define CYD_LED_GREEN  16
#define CYD_LED_BLUE   17
#define CYD_LDR        34

class LGFX_CYD : public lgfx::LGFX_Device {
    lgfx::Panel_ILI9341 _panel_instance;
    lgfx::Bus_SPI       _bus_instance;
    lgfx::Light_PWM     _light_instance;
    lgfx::Touch_XPT2046 _touch_instance;

public:
    LGFX_CYD(void) {
        // 1. SPI Bus Configuration for ILI9341 LCD Panel (HSPI)
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host = SPI2_HOST; // Native HSPI for Panel
            cfg.spi_mode = 0;
            cfg.freq_write = 60000000; // 60 MHz stable high-speed SPI
            cfg.freq_read  = 16000000;
            cfg.spi_3wire  = false;
            cfg.use_lock   = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = 14;
            cfg.pin_mosi = 13;
            cfg.pin_miso = 12;
            cfg.pin_dc   = 2;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        // 2. LCD Panel Configuration (ILI9341 320x240 Landscape)
        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs           =   15;
            cfg.pin_rst          =   -1;
            cfg.pin_busy         =   -1;
            cfg.memory_width     =  240;
            cfg.memory_height    =  320;
            cfg.panel_width      =  240;
            cfg.panel_height     =  320;
            cfg.offset_x         =    0;
            cfg.offset_y         =    0;
            cfg.offset_rotation  =    0;
            cfg.dummy_read_pixel =    8;
            cfg.dummy_read_bits  =    1;
            cfg.readable         = false;
            cfg.invert           = false;
            cfg.rgb_order        = false;
            cfg.dlen_16bit       = false;
            cfg.bus_shared       = false;
            _panel_instance.config(cfg);
        }

        // 3. Backlight Configuration
        {
            auto cfg = _light_instance.config();
            cfg.pin_bl = CYD_LCD_BL;
            cfg.invert = false;
            cfg.freq   = 44100;
            cfg.pwm_channel = 7;
            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }

        // 4. Touch Controller Configuration (XPT2046)
        {
            auto cfg = _touch_instance.config();
            cfg.x_min      = 300;
            cfg.x_max      = 3900;
            cfg.y_min      = 3700;
            cfg.y_max      = 200;
            cfg.pin_int    = -1; // -1: Query directly over SPI, avoids floating IRQ pin on CYD
            cfg.bus_shared = false;
            cfg.offset_rotation = 0;
            cfg.spi_host   = -1; // Software SPI frees SPI3_HOST (VSPI)
            cfg.pin_sclk   = 25;
            cfg.pin_mosi   = 32;
            cfg.pin_miso   = 39;
            cfg.pin_cs     = 33;
            cfg.freq       = 2500000; // 2.5 MHz SPI for fast touch reading
            _touch_instance.config(cfg);
            _panel_instance.setTouch(&_touch_instance);
        }

        setPanel(&_panel_instance);
    }
};
