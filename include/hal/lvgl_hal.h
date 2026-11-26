#pragma once
#ifndef UNIT_TEST
#include <lvgl.h>
#include "hal/cyd_display.h"

class LvglHal {
public:
    static bool init(LGFX_CYD& lcd);
    static void handle();
    static void setBrightness(uint8_t brightness);
    static uint8_t getBrightness() { return s_brightness_; }
    static void setRotation(uint8_t rotation);
    static uint8_t getRotation() { return s_rotation_; }
    static void waitDMA() { if (lcd_) lcd_->waitDMA(); }
    static LGFX_CYD* getLcd() { return lcd_; }

    static uint32_t getFlushCount();
    static uint32_t getFlushTimeUs();
    static uint32_t getTouchTimeUs();
    static void resetProfile();

    static lv_display_t* getDisplay() { return disp_; }
    static lv_indev_t* getIndev() { return indev_; }

    static bool loadTouchCalibration();
    static void calibrateTouch();
    static void resetTouchCalibration();

private:
    static void flushCallback(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map);
    static void touchCallback(lv_indev_t* indev, lv_indev_data_t* data);
    static uint32_t tickCallback();

    static inline LGFX_CYD* lcd_ = nullptr;
    static inline lv_display_t* disp_ = nullptr;
    static inline lv_indev_t* indev_ = nullptr;
    static inline uint8_t s_brightness_ = 180;
    static inline uint8_t s_rotation_ = 1;
};
#endif
