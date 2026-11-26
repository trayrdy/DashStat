/**
 * @file live_page.h
 * Live Sensor Matrix Page for DashStat:
 * - Grid View: 2 columns x 3 rows (6 cards: Engine Load, Throttle %, Coolant Temp, CVT Temp, Intake Temp, Battery Voltage)
 * - Detail View: Waveform line chart (lv_chart, 60 points @ 10 Hz), Min / Avg / Max running stats, Reset button
 * - Retained static view, zero-allocation render loop, threshold-based color alerts.
 */
#pragma once

#include <stdint.h>
#include "model/vehicle_state.h"

#ifndef UNIT_TEST
#include <lvgl.h>
#include "ui/pages/page.h"

namespace ui {

struct SensorCard {
    lv_obj_t* container = nullptr;
    lv_obj_t* topRow = nullptr;
    lv_obj_t* valRow = nullptr;
    lv_obj_t* iconLabel = nullptr;
    lv_obj_t* nameLabel = nullptr;
    lv_obj_t* valueLabel = nullptr;
    lv_obj_t* unitLabel = nullptr;
    float lastVal = -9999.0f;
    uint8_t alertState = 0; // 0=normal, 1=warn, 2=crit
    int8_t connState = -1;  // -1=uninit, 0=disconnected, 1=connected
};

class LivePage : public Page {
public:
    void create(lv_obj_t* parent) override;
    const char* title() const override { return "LIVE STAT"; }
    void onShow() override;
    void onHide() override;
    bool onBack() override;
    void update(const VehicleState& state, uint32_t nowMs) override;
    void onLayout(int32_t contentW) override;
    void applyTheme() override;

    void openDetail(uint8_t index);
    void closeDetail();
    void resetStats();
    bool isDetailOpen() const { return isDetailOpen_; }
    uint8_t selectedSensor() const { return selectedSensor_; }

    void applySlotSensor(uint8_t slotIdx, uint8_t catalogIdx);

    static constexpr uint8_t kSensorCount = 6;

private:
    static void onCardClicked(lv_event_t* e);
    static void onResetClicked(lv_event_t* e);

    void updateGrid(const VehicleState& state, uint32_t nowMs);
    void updateDetail(const VehicleState& state, uint32_t nowMs);
    float getSensorValue(uint8_t index, const VehicleState& state) const;

    // View containers (both pre-allocated for zero memory churn)
    lv_obj_t* gridContainer_ = nullptr;
    lv_obj_t* detailContainer_ = nullptr;

    // Grid View
    SensorCard cards_[kSensorCount];

    // Detail View
    uint8_t selectedSensor_ = 0;
    bool isDetailOpen_ = false;

    lv_obj_t* detailHeader_ = nullptr;
    lv_obj_t* detailIcon_ = nullptr;
    lv_obj_t* detailName_ = nullptr;
    lv_obj_t* detailVal_ = nullptr;
    lv_obj_t* detailUnit_ = nullptr;

    lv_obj_t* statMin_ = nullptr;
    lv_obj_t* statAvg_ = nullptr;
    lv_obj_t* statMax_ = nullptr;

    lv_obj_t* chartCard_ = nullptr;
    lv_obj_t* chart_ = nullptr;
    lv_chart_series_t* series_ = nullptr;
    lv_obj_t* resetBtn_ = nullptr;

    // Detail Statistics
    uint32_t samples_ = 0;
    float sum_ = 0.0f;
    float min_ = 0.0f;
    float max_ = 0.0f;
    int32_t axisMin_ = 0;
    int32_t axisMax_ = 0;
    uint32_t lastSampleMs_ = 0;
    uint32_t lastGridUpdateMs_ = 0;
    uint8_t detailAlertState_ = 0xFF;
    int8_t detailConnState_ = -1;
};

}  // namespace ui
#endif  // !UNIT_TEST
