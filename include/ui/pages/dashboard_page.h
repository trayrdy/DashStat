/**
 * @file dashboard_page.h
 * Main cluster dashboard page for DashStat:
 * - Center: RingGauge tachometer arc with digital speed and gear indicator
 * - 4 Corners: StatTiles for Coolant, Battery, CVT Fluid (Hero Param), and Intake Air Temp
 *
 * Fully retained static view, zero-allocation render loop, 30+ FPS optimized.
 */
#pragma once

#include <stdint.h>
#include "model/vehicle_state.h"

#ifndef UNIT_TEST
#include <lvgl.h>
#include "ui/pages/page.h"
#include "ui/components/stat_tile.h"

namespace ui {

class DashboardPage : public Page {
public:
    void create(lv_obj_t* parent) override;
    const char* title() const override { return "DASHSTAT"; }
    void onShow() override;
    void applyTheme() override;
    void update(const VehicleState& state, uint32_t nowMs) override;
    uint8_t activeSegments() const { return static_cast<uint8_t>((activeFillPx_ + 6.0f) / 13.0f); }
    float activeFillPixels() const { return activeFillPx_; }
    float activeThrottlePixels() const { return activeThrottlePx_; }
    uint32_t scaleMax() const { return scaleMax_; }
    uint32_t redline() const { return redline_; }
    uint32_t warn() const { return warn_; }

private:
    void updateTiles(const VehicleState& state);

    uint32_t scaleMax_ = 8000;
    uint32_t redline_ = 6200;
    uint32_t warn_ = 5400;

    lv_obj_t* centerBox_ = nullptr;
    lv_obj_t* speedLabel_ = nullptr;

    lv_obj_t* rpmBox_ = nullptr;
    lv_obj_t* rpmBar_ = nullptr;
    lv_obj_t* rpmRow_ = nullptr;
    lv_obj_t* rpmNumLabel_ = nullptr;
    lv_obj_t* rpmUnitLabel_ = nullptr;
    float activeFillPx_ = 0.0f;
    int16_t lastDrawnFillPx_ = -1;

    lv_obj_t* throttleBox_ = nullptr;
    lv_obj_t* throttleRow_ = nullptr;
    lv_obj_t* throttleLabel_ = nullptr;
    lv_obj_t* throttleNumLabel_ = nullptr;
    lv_obj_t* throttleBar_ = nullptr;
    float activeThrottlePx_ = 0.0f;
    int16_t lastDrawnThrottlePx_ = -1;

    StatTile tiles_[4];  // 0: CLT, 1: BATT, 2: BOOST, 3: IAT
    uint32_t lastSlowUpdateMs_ = 0;
};

}  // namespace ui
#endif  // UNIT_TEST
