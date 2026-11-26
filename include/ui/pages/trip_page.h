#pragma once

#include <stdint.h>
#include "model/vehicle_state.h"
#include "model/trip_computer.h"

#ifndef UNIT_TEST
#include <lvgl.h>
#include "ui/pages/page.h"

namespace ui {

class TripPage : public Page {
public:
    void create(lv_obj_t* parent) override;
    const char* title() const override { return "TRIP & RECORD"; }
    void onShow() override;
    void onHide() override;
    bool onBack() override;
    void update(const VehicleState& state, uint32_t nowMs) override;
    void onLayout(int32_t contentW) override;
    void applyTheme() override;

    void openSprintMode(SprintMode mode);
    void closeSprintMode();
    void resetTrip();
    void resetSprintBest();

    bool isFullView() const { return isFullView_; }
    SprintMode activeMode() const { return activeMode_; }

private:
    static void onModeSelectClicked(lv_event_t* e);
    static void onBackClicked(lv_event_t* e);
    static void onResetTripClicked(lv_event_t* e);
    static void onResetBestClicked(lv_event_t* e);

    void updateSplitView(const VehicleState& state, uint32_t nowMs);
    void updateSprintHud(const VehicleState& state, uint32_t nowMs, bool force = false);

    // View Containers (pre-allocated for zero heap churn)
    lv_obj_t* splitContainer_ = nullptr;
    lv_obj_t* fullContainer_ = nullptr;
    lv_obj_t* leftCol_ = nullptr;
    lv_obj_t* rightCol_ = nullptr;
    lv_obj_t* telemetryCards_[3]{};
    lv_obj_t* hudTopBar_ = nullptr;
    lv_obj_t* hudCenterBox_ = nullptr;
    lv_obj_t* hudBotBar_ = nullptr;

    // Split View - Left Hub (3 Mode Buttons)
    lv_obj_t* modeButtons_[static_cast<uint8_t>(SprintMode::COUNT)]{};
    lv_obj_t* modeSubLabels_[static_cast<uint8_t>(SprintMode::COUNT)]{};

    // Split View - Right Telemetry (3 Cards + 1 Button)
    lv_obj_t* distValLabel_ = nullptr;
    lv_obj_t* speedValLabel_ = nullptr;
    lv_obj_t* timeValLabel_ = nullptr;
    lv_obj_t* resetTripBtn_ = nullptr;

    // Full Screen Sub-View (Motorsport HUD)
    bool isFullView_ = false;
    SprintMode activeMode_ = SprintMode::SPEED_0_100;

    lv_obj_t* hudBackBtn_ = nullptr;
    lv_obj_t* hudTitleLabel_ = nullptr;
    lv_obj_t* hudStatusChip_ = nullptr;
    lv_obj_t* hudStatusLabel_ = nullptr;

    lv_obj_t* hudDigitsLabel_ = nullptr;
    lv_obj_t* hudUnitLabel_ = nullptr;
    lv_obj_t* hudProgressBar_ = nullptr;
    lv_obj_t* hudTrapLabel_ = nullptr;

    lv_obj_t* hudBestLabel_ = nullptr;
    lv_obj_t* hudLastLabel_ = nullptr;
    lv_obj_t* hudResetBestBtn_ = nullptr;

    // State tracking
    AccelTimerState lastAccelState_ = AccelTimerState::IDLE;
    uint32_t lastSplitUpdateMs_ = 0;
};

}  // namespace ui
#endif  // !UNIT_TEST
