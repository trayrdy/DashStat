#ifndef UNIT_TEST
#include <Arduino.h>
#include <cstdio>

#include "ui/pages/dashboard_page.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/fonts/ui_icons.h"
#include "model/settings_manager.h"
#include "obd/universal_pid_catalog.h"

namespace ui {

// Canvas Buffers for Zero-Allocation 60 FPS Direct Blit
static constexpr int32_t kBarW = 158;
static constexpr int32_t kBarH = 9;
static uint16_t s_rpmCanvasBuf[kBarW * kBarH];
static uint16_t s_throtCanvasBuf[kBarW * kBarH];

static inline uint16_t toSwapped(lv_color_t c) {
    uint16_t u = lv_color_to_u16(c);
    return (uint16_t)((u >> 8) | (u << 8));
}

// Zero-allocation, direct-to-RAM rasterizer for true slanted parallelograms (3px sport slant)
// Replaces 96 slow vector draw tasks with a single 5-microsecond array fill
static void renderSlantedBarToBuffer(uint16_t* buf, float fill_px, bool isRpm, uint32_t scaleMax, uint32_t redline, uint32_t warn) {
    const uint16_t c_bg = toSwapped(color::bg());
    const uint16_t c_track = toSwapped(color::track());
    const uint16_t c_accent = toSwapped(color::accent());
    const uint16_t c_warn = toSwapped(color::warn());
    const uint16_t c_crit = toSwapped(color::crit());
    const uint16_t c_text = toSwapped(color::text());

    // 1. Fill entire canvas with background
    for (int i = 0; i < kBarW * kBarH; ++i) {
        buf[i] = c_bg;
    }

    constexpr int32_t kBlockW = 12;
    constexpr int32_t kGap = 1;

    for (uint8_t i = 0; i < 12; ++i) {
        int32_t block_x = i * (kBlockW + kGap);
        float block_start = static_cast<float>(block_x);
        float block_end = block_start + static_cast<float>(kBlockW);

        int32_t active_w = 0;
        if (fill_px >= block_end) {
            active_w = kBlockW;
        } else if (fill_px > block_start) {
            active_w = static_cast<int32_t>(fill_px - block_start);
            if (active_w < 1) active_w = 1;
            if (active_w > kBlockW) active_w = kBlockW;
        }

        uint16_t active_c = c_accent;
        if (isRpm) {
            uint32_t blockMidRpm = (static_cast<uint32_t>(i) * 2 + 1) * scaleMax / 24;
            if (blockMidRpm >= redline) {
                active_c = c_crit;
            } else if (blockMidRpm >= warn) {
                active_c = c_warn;
            } else {
                active_c = c_text;
            }
        }

        for (int32_t y = 0; y < kBarH; ++y) {
            int32_t shift = (3 * (kBarH - 1 - y)) / (kBarH - 1);
            int32_t x_start = block_x + shift;
            uint16_t* row = &buf[y * kBarW];

            // Active portion
            for (int32_t x = x_start; x < x_start + active_w; ++x) {
                row[x] = active_c;
            }
            // Inactive portion
            for (int32_t x = x_start + active_w; x < x_start + kBlockW; ++x) {
                row[x] = c_track;
            }
        }
    }
}

void DashboardPage::create(lv_obj_t* parent) {
    root_ = box(parent);
    lv_obj_set_size(root_, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(root_, color::bg(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);

    // 1. Center Digital Speed (Centered in static box)
    centerBox_ = box(root_);
    lv_obj_set_size(centerBox_, 160, 68);
    lv_obj_align(centerBox_, LV_ALIGN_CENTER, 0, -10);

    speedLabel_ = label(centerBox_, font::speedFont(SettingsManager::instance().get().speedFont), color::text(), "0");
    lv_obj_center(speedLabel_);

    // 2. Center-Bottom Sport Throttle Bar (12 Parallelogram Blocks, Canvas Direct Blit)
    throttleBox_ = column(root_, 2);
    lv_obj_set_size(throttleBox_, 158, 30);
    lv_obj_align(throttleBox_, LV_ALIGN_CENTER, 0, 43);
    lv_obj_set_flex_align(throttleBox_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    throttleRow_ = box(throttleBox_);
    lv_obj_set_size(throttleRow_, 158, 18);

    throttleLabel_ = label(throttleRow_, font::caption(), color::text_dim(), "THROTTLE");
    lv_obj_align(throttleLabel_, LV_ALIGN_LEFT_MID, 2, 0);

    throttleNumLabel_ = label(throttleRow_, font::title(), color::text(), "0");
    lv_obj_align(throttleNumLabel_, LV_ALIGN_LEFT_MID, 133, 0);

    throttleBar_ = lv_canvas_create(throttleBox_);
    lv_canvas_set_buffer(throttleBar_, s_throtCanvasBuf, kBarW, kBarH, LV_COLOR_FORMAT_RGB565_SWAPPED);

    // 4. Top-Center Sport Slanted Tachometer (12 Parallelogram Blocks, Canvas Direct Blit)
    // Symmetrically spaced: center y=-63 creates 4px gap to speedBox (same as throttleBox below)
    rpmBox_ = column(root_, 2);
    lv_obj_set_size(rpmBox_, 158, 30);
    lv_obj_align(rpmBox_, LV_ALIGN_CENTER, 0, -63);
    lv_obj_set_flex_align(rpmBox_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    rpmBar_ = lv_canvas_create(rpmBox_);
    lv_canvas_set_buffer(rpmBar_, s_rpmCanvasBuf, kBarW, kBarH, LV_COLOR_FORMAT_RGB565_SWAPPED);

    rpmRow_ = box(rpmBox_);
    lv_obj_set_size(rpmRow_, 158, 18);

    rpmUnitLabel_ = label(rpmRow_, font::caption(), color::text_dim(), "RPM");
    lv_obj_align(rpmUnitLabel_, LV_ALIGN_LEFT_MID, 0, -1);

    rpmNumLabel_ = label(rpmRow_, font::title(), color::text(), "0");
    lv_obj_align(rpmNumLabel_, LV_ALIGN_LEFT_MID, 117, -1);

    // 4. Corner StatTiles (Top: 76 x 38 px with title on top; Bottom: 90 x 38 px with value on top)
    // 0: Top-Left  -> Coolant Temp (Title on top, value on bottom)
    tiles_[0].create(root_, false, 76, true);
    lv_obj_align(tiles_[0].root(), LV_ALIGN_TOP_LEFT, 3, 8);
    tiles_[0].set_title("COOLANT", ICON_COOLANT);

    // 1: Top-Right -> Battery Voltage (Title on top, value on bottom)
    tiles_[1].create(root_, true, 76, true);
    lv_obj_align(tiles_[1].root(), LV_ALIGN_TOP_RIGHT, -3, 8);
    tiles_[1].set_title("BATTERY", ICON_BATTERY);

    // 2: Bottom-Left -> Boost / MAP PSI (configurable corner tile)
    tiles_[2].create(root_, false, 90);
    lv_obj_align(tiles_[2].root(), LV_ALIGN_BOTTOM_LEFT, 5, -6);
    tiles_[2].set_title("BOOST", ICON_TURBO);

    // 3: Bottom-Right -> Intake Air Temp
    tiles_[3].create(root_, true, 90);
    lv_obj_align(tiles_[3].root(), LV_ALIGN_BOTTOM_RIGHT, -5, -6);
    tiles_[3].set_title("INTAKE AIR", ICON_AIR);
}

void DashboardPage::onShow() {
    activeFillPx_ = 0.0f;
    lastDrawnFillPx_ = -1;
    activeThrottlePx_ = 0.0f;
    lastDrawnThrottlePx_ = -1;
    lastSlowUpdateMs_ = 0;

    const auto& s = SettingsManager::instance().get();
    scaleMax_ = (s.maxRpm >= 3000) ? s.maxRpm : 8000;
    redline_ = (s.redlineRpm > 0) ? s.redlineRpm : 6200;
    warn_ = (s.warnRpm > 0) ? s.warnRpm : 5400;

    if (speedLabel_ != nullptr) {
        lv_obj_set_style_text_font(speedLabel_, font::speedFont(s.speedFont), 0);
    }

    renderSlantedBarToBuffer(s_rpmCanvasBuf, 0.0f, true, scaleMax_, redline_, warn_);
    renderSlantedBarToBuffer(s_throtCanvasBuf, 0.0f, false, 0, 0, 0);
    if (rpmBar_) lv_obj_invalidate(rpmBar_);
    if (throttleBar_) lv_obj_invalidate(throttleBar_);

    for (uint8_t i = 0; i < 4; ++i) {
        uint8_t catIdx = s.dashSlots[i];
        if (catIdx == 0xFF) {
            tiles_[i].set_title("---", ICON_CLOSE);
            tiles_[i].set_value("---", "", color::text_dim());
        } else {
            const auto& def = UniversalPidCatalog::get(catIdx);
            const char* icon = ICON_GAUGE;
            switch (def.category) {
                case PidCategory::TEMPERATURE:  icon = ICON_THERMOMETER; break;
                case PidCategory::AIR_PRESSURE: icon = ICON_AIR; break;
                case PidCategory::FUEL_SYSTEM:  icon = ICON_FUEL; break;
                case PidCategory::ELECTRICAL:   icon = ICON_BATTERY; break;
                case PidCategory::IGNITION:     icon = ICON_TIMING; break;
                case PidCategory::ENGINE:       icon = ICON_ENGINE; break;
                default:                        icon = ICON_GAUGE; break;
            }
            if (def.pidHex == 0x05) icon = ICON_COOLANT;
            else if (def.pidHex == 0xFF || def.pidHex == 0x42) icon = ICON_BATTERY;
            else if (def.pidHex == 0xF0) icon = ICON_TURBO;

            const char* title = UniversalPidCatalog::getDashboardName(def.pidHex);
            tiles_[i].set_title(title, icon);
        }
    }
}

void DashboardPage::applyTheme() {
    if (root_ != nullptr) {
        lv_obj_set_style_bg_color(root_, color::bg(), 0);
    }
    if (speedLabel_ != nullptr) {
        lv_obj_set_style_text_font(speedLabel_, font::speedFont(SettingsManager::instance().get().speedFont), 0);
    }
    lastDrawnFillPx_ = -1;
    lastDrawnThrottlePx_ = -1;
}

void DashboardPage::update(const VehicleState& state, uint32_t nowMs) {
    // ---- Fast Path (every frame, ~30 Hz): Speed and Slanted Tachometer ----
    char speedBuf[8];
    snprintf(speedBuf, sizeof(speedBuf), "%u", state.speedKmh);
    set_text(speedLabel_, speedBuf);

    // Update Slanted Tachometer with continuous sub-block fill & smooth 30/60 FPS Lerp
    // Dynamic Max RPM scale from Settings (Petrol 8000, Diesel 6000, Sport 9000)
    // Total span: 11 * 13 + 12 = 155 px (12 blocks x 12px + 11 gaps x 1px)
    float maxRpm = static_cast<float>(scaleMax_);

    float targetFillPx = (state.rpm >= maxRpm) ? 155.0f :
                         (state.rpm > 200 ? (static_cast<float>(state.rpm) * 155.0f / maxRpm) : 0.0f);

    float diff = targetFillPx - activeFillPx_;
    if (std::abs(diff) < 0.35f) {
        activeFillPx_ = targetFillPx;
    } else {
        // Linear Interpolation (Lerp) factor: 0.35 gives ~60ms smooth settling without lag
        activeFillPx_ += diff * 0.35f;
    }

    int16_t drawnPx = static_cast<int16_t>(roundf(activeFillPx_));
    if (drawnPx != lastDrawnFillPx_) {
        lastDrawnFillPx_ = drawnPx;
        renderSlantedBarToBuffer(s_rpmCanvasBuf, activeFillPx_, true, scaleMax_, redline_, warn_);
        if (rpmBar_) lv_obj_invalidate(rpmBar_);
    }

    // Update Sport Throttle Bar with continuous sub-block fill & smooth 30 FPS Lerp
    // Total span: 155 px (0 - 100% throttle => 1.55 px per 1%)
    float targetThrottlePx = (state.throttlePercent >= 100) ? 155.0f :
                             (static_cast<float>(state.throttlePercent) * 1.55f);

    float tDiff = targetThrottlePx - activeThrottlePx_;
    if (std::abs(tDiff) < 0.35f) {
        activeThrottlePx_ = targetThrottlePx;
    } else {
        activeThrottlePx_ += tDiff * 0.35f;
    }

    int16_t drawnThrottlePx = static_cast<int16_t>(roundf(activeThrottlePx_));
    if (drawnThrottlePx != lastDrawnThrottlePx_) {
        lastDrawnThrottlePx_ = drawnThrottlePx;
        renderSlantedBarToBuffer(s_throtCanvasBuf, activeThrottlePx_, false, 0, 0, 0);
        if (throttleBar_) lv_obj_invalidate(throttleBar_);
    }

    // Quantize RPM display to 25 RPM steps to eliminate 30 Hz text invalidation churn
    uint16_t dispRpm = (state.rpm + 12) / 25 * 25;
    char rpmBuf[16];
    snprintf(rpmBuf, sizeof(rpmBuf), "%u", dispRpm);
    set_text(rpmNumLabel_, rpmBuf);

    // Update Throttle display (above Block 11)
    char throtBuf[8];
    snprintf(throtBuf, sizeof(throtBuf), "%u", state.throttlePercent);
    set_text(throttleNumLabel_, throtBuf);

    // ---- Slow Path (every 200 ms): Corner Sensor Tiles ----
    if (nowMs - lastSlowUpdateMs_ >= 200 || lastSlowUpdateMs_ == 0) {
        lastSlowUpdateMs_ = nowMs;
        updateTiles(state);
    }
}

void DashboardPage::updateTiles(const VehicleState& state) {
    char valBuf[16];
    const auto& s = SettingsManager::instance().get();

    for (uint8_t i = 0; i < 4; ++i) {
        uint8_t catIdx = s.dashSlots[i];
        if (catIdx == 0xFF) {
            tiles_[i].set_value("---", "", color::text_dim());
            continue;
        }

        const auto& def = UniversalPidCatalog::get(catIdx);
        if (!UniversalPidCatalog::isCatalogIndexSupported(catIdx, state.unsupportedCatalogBits)) {
            tiles_[i].set_value("--", def.unit, color::text_dim());
            continue;
        }
        float slotVal = state.dashSlotValues[i];

        if (def.pidHex == 0x05) { // Coolant Temp
            int8_t temp = (slotVal != 0.0f) ? static_cast<int8_t>(slotVal) : state.coolantTempC;
            if (temp != 0) {
                snprintf(valBuf, sizeof(valBuf), "%d", temp);
                uint8_t alert = UniversalPidCatalog::getAlertLevel(0x05, temp);
                lv_color_t cltColor = (alert == 2) ? color::crit() :
                                      ((alert == 1) ? color::warn() :
                                      ((temp < 50) ? color::info() : color::text()));
                tiles_[i].set_value(valBuf, def.unit, cltColor);
            } else {
                tiles_[i].set_value("--", def.unit, color::text_dim());
            }
        } else if (def.pidHex == 0xFF || def.pidHex == 0x42) { // Battery / Module Voltage
            float volt = (slotVal > 0.1f) ? slotVal : state.batteryVoltage;
            if (volt > 0.1f) {
                snprintf(valBuf, sizeof(valBuf), "%.1f", volt);
                uint8_t alert = UniversalPidCatalog::getAlertLevel(0xFF, volt);
                lv_color_t battColor = (alert == 2) ? color::crit() :
                                       ((alert == 1) ? color::warn() : color::text());
                tiles_[i].set_value(valBuf, def.unit, battColor);
            } else {
                tiles_[i].set_value("--", def.unit, color::text_dim());
            }
        } else if (def.pidHex == 0x0F) { // Intake Air Temp
            int8_t iat = (slotVal != 0.0f) ? static_cast<int8_t>(slotVal) : state.intakeAirTempC;
            if (iat != 0) {
                snprintf(valBuf, sizeof(valBuf), "%d", iat);
                uint8_t alert = UniversalPidCatalog::getAlertLevel(0x0F, iat);
                lv_color_t iatColor = (alert == 2) ? color::crit() :
                                      ((alert == 1) ? color::warn() : color::text());
                tiles_[i].set_value(valBuf, def.unit, iatColor);
            } else {
                tiles_[i].set_value("--", def.unit, color::text_dim());
            }
        } else if (def.pidHex == 0x0C) { // RPM
            uint16_t rpm = (slotVal != 0.0f) ? static_cast<uint16_t>(slotVal) : state.rpm;
            snprintf(valBuf, sizeof(valBuf), "%u", rpm);
            tiles_[i].set_value(valBuf, def.unit, color::text());
        } else if (def.pidHex == 0x0D) { // Speed
            uint8_t spd = (slotVal != 0.0f) ? static_cast<uint8_t>(slotVal) : state.speedKmh;
            snprintf(valBuf, sizeof(valBuf), "%u", spd);
            tiles_[i].set_value(valBuf, def.unit, color::text());
        } else if (def.pidHex == 0x04) { // Engine Load
            uint8_t load = (slotVal != 0.0f) ? static_cast<uint8_t>(slotVal) : state.engineLoad;
            snprintf(valBuf, sizeof(valBuf), "%u", load);
            tiles_[i].set_value(valBuf, def.unit, color::text());
        } else if (def.pidHex == 0x11) { // Throttle
            uint8_t throt = (slotVal != 0.0f) ? static_cast<uint8_t>(slotVal) : state.throttlePercent;
            snprintf(valBuf, sizeof(valBuf), "%u", throt);
            tiles_[i].set_value(valBuf, def.unit, color::text());
        } else {
            // General sensor (including Boost 0xF0, MAP, MAF, Timing, Trims, etc.)
            if (state.isConnected || slotVal != 0.0f) {
                snprintf(valBuf, sizeof(valBuf), "%.1f", slotVal);
                uint8_t alert = UniversalPidCatalog::getAlertLevel(def.pidHex, slotVal);
                lv_color_t alertColor = (alert == 2) ? color::crit() :
                                        ((alert == 1) ? color::warn() : color::text());
                tiles_[i].set_value(valBuf, def.unit, alertColor);
            } else {
                tiles_[i].set_value("--", def.unit, color::text_dim());
            }
        }
    }
}

}  // namespace ui
#endif  // UNIT_TEST
