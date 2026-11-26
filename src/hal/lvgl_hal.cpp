#ifndef UNIT_TEST
#include "hal/lvgl_hal.h"
#include "config/app_config.h"
#include "model/settings_manager.h"
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <Preferences.h>
#include "hal/buzzer_controller.h"

static constexpr size_t BUF_PIXELS = SCREEN_WIDTH * LVGL_BUFFER_LINES;
static constexpr size_t BUF_BYTES = BUF_PIXELS * sizeof(lv_color16_t);
static uint8_t* buf1 = nullptr;
static uint8_t* buf2 = nullptr;

static uint32_t s_flushCount = 0;
static uint32_t s_flushTimeUs = 0;
static uint32_t s_touchTimeUs = 0;

#if LVGL_REFR_PROBE
static int64_t s_refr_t0 = 0;
static void refrProbeCallback(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_REFR_START) {
        s_refr_t0 = esp_timer_get_time();
    } else if (code == LV_EVENT_REFR_READY) {
        LvglHal::waitDMA();
        const int64_t us = esp_timer_get_time() - s_refr_t0;
        if (us > 3000) {
            Serial.printf("[REFR Probe] %lld us (%ld ms) | Flushes=%lu\n",
                          (long long)us, (long)(us / 1000), (unsigned long)s_flushCount);
        }
    }
}
#endif

uint32_t LvglHal::tickCallback() {
    return millis();
}

void LvglHal::flushCallback(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    uint32_t tStart = micros();
    const uint32_t w = (area->x2 - area->x1 + 1);
    const uint32_t h = (area->y2 - area->y1 + 1);

    if (lcd_) {
        lcd_->pushImageDMA(area->x1, area->y1, w, h, reinterpret_cast<const uint16_t*>(px_map));
    }
    lv_display_flush_ready(disp);
    s_flushTimeUs += (micros() - tStart);
    s_flushCount++;
}

void LvglHal::touchCallback(lv_indev_t* indev, lv_indev_data_t* data) {
    uint32_t tStart = micros();
    if (!lcd_) return;
    int32_t x = 0, y = 0;
    if (lcd_->getTouch(&x, &y)) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = x;
        data->point.y = y;
        static uint32_t lastLog = 0;
        if (millis() - lastLog > 250) {
            Serial.printf("[Touch] Tap registered at X=%ld, Y=%ld\n", (long)x, (long)y);
            lastLog = millis();
        }
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
    s_touchTimeUs += (micros() - tStart);
}

bool LvglHal::init(LGFX_CYD& lcd) {
    lcd_ = &lcd;
    lcd_->init();
    uint8_t rot = (SettingsManager::instance().get().screenRotation == ScreenRotation::INVERTED) ? 3 : 1;
    setRotation(rot);
    setBrightness(180);
    loadTouchCalibration();

    lv_init();
    lv_tick_set_cb(tickCallback);

    // Allocate partial DMA buffers from Internal DMA-capable DRAM
    buf1 = static_cast<uint8_t*>(heap_caps_malloc(BUF_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    buf2 = static_cast<uint8_t*>(heap_caps_malloc(BUF_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    if (!buf1) buf1 = static_cast<uint8_t*>(malloc(BUF_BYTES));
    if (!buf2) buf2 = static_cast<uint8_t*>(malloc(BUF_BYTES));

    disp_ = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_display_set_color_format(disp_, LV_COLOR_FORMAT_RGB565_SWAPPED);
    lv_display_set_flush_cb(disp_, flushCallback);
    lv_display_set_buffers(disp_, buf1, buf2, BUF_BYTES, LV_DISPLAY_RENDER_MODE_PARTIAL);

#if LVGL_REFR_PROBE
    lv_display_add_event_cb(disp_, refrProbeCallback, LV_EVENT_REFR_START, nullptr);
    lv_display_add_event_cb(disp_, refrProbeCallback, LV_EVENT_REFR_READY, nullptr);
#endif

    indev_ = lv_indev_create();
    lv_indev_set_type(indev_, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev_, touchCallback);
    lv_indev_set_long_press_time(indev_, 500);
    lv_indev_set_long_press_repeat_time(indev_, 150);

    return true;
}

void LvglHal::handle() {
    lv_timer_handler();
}

void LvglHal::setBrightness(uint8_t brightness) {
    s_brightness_ = brightness;
    if (lcd_) {
        lcd_->setBrightness(brightness);
    }
}

void LvglHal::setRotation(uint8_t rotation) {
    s_rotation_ = rotation;
    if (lcd_) {
        waitDMA();
        lcd_->setRotation(rotation);
        if (disp_) {
            lv_obj_invalidate(lv_display_get_screen_active(disp_));
        }
    }
}

uint32_t LvglHal::getFlushCount() { return s_flushCount; }
uint32_t LvglHal::getFlushTimeUs() { return s_flushTimeUs; }
uint32_t LvglHal::getTouchTimeUs() { return s_touchTimeUs; }
void LvglHal::resetProfile() {
    s_flushCount = 0;
    s_flushTimeUs = 0;
    s_touchTimeUs = 0;
}

bool LvglHal::loadTouchCalibration() {
    if (!lcd_) return false;
    Preferences prefs;
    if (prefs.begin("dashstat", true)) {
        uint16_t cal[8];
        size_t len = prefs.getBytes("touch_cal", cal, sizeof(cal));
        prefs.end();
        if (len == sizeof(cal)) {
            lcd_->setTouchCalibrate(cal);
            Serial.printf("[TouchCal] Loaded calibration from NVS: %u, %u, %u, %u, %u, %u, %u, %u\n",
                          cal[0], cal[1], cal[2], cal[3], cal[4], cal[5], cal[6], cal[7]);
            return true;
        }
    }
    Serial.println("[TouchCal] Using default factory calibration");
    return false;
}

void LvglHal::calibrateTouch() {
    if (!lcd_) return;
    waitDMA();

    uint16_t cal[8];
    // LovyanGFX built-in calibration:
    // Draws calibration points at the 4 screen corners with fg/bg colors,
    // collects 8 samples per point, and configures the touch transform.
    lcd_->calibrateTouch(cal, TFT_WHITE, TFT_BLACK, 14);

    // Save 16-byte calibration array to NVS
    BuzzerController::instance().mute();
    Preferences prefs;
    if (prefs.begin("dashstat", false)) {
        prefs.putBytes("touch_cal", cal, sizeof(cal));
        prefs.end();
        Serial.printf("[TouchCal] Saved calibration to NVS: %u, %u, %u, %u, %u, %u, %u, %u\n",
                      cal[0], cal[1], cal[2], cal[3], cal[4], cal[5], cal[6], cal[7]);
    }

    if (disp_) {
        lv_obj_invalidate(lv_display_get_screen_active(disp_));
    }
}

void LvglHal::resetTouchCalibration() {
    if (!lcd_) return;
    BuzzerController::instance().mute();
    Preferences prefs;
    if (prefs.begin("dashstat", false)) {
        prefs.remove("touch_cal");
        prefs.end();
    }
    // Re-apply default factory calibration coordinates for CYD XPT2046
    // From cyd_display.h: x_min=300, x_max=3900, y_min=3700, y_max=200
    // Parameters format: { x_min, y_min, x_min, y_max, x_max, y_min, x_max, y_max }
    uint16_t defCal[8] = { 300, 3700, 300, 200, 3900, 3700, 3900, 200 };
    lcd_->setTouchCalibrate(defCal);
    Serial.println("[TouchCal] Reset touch calibration to hardware defaults");

    if (disp_) {
        lv_obj_invalidate(lv_display_get_screen_active(disp_));
    }
}
#endif
