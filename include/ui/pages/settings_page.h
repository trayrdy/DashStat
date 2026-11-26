#pragma once

#include "ui/pages/page.h"
#include "model/settings.h"
#include "model/settings_manager.h"
#include "obd/universal_pid_catalog.h"
#ifndef UNIT_TEST
#include "obd/elm327_engine.h"
#endif

namespace ui {

enum class SettingsView : uint8_t {
    ROOT_MENU = 0,
    PROTOCOL_PICKER,
    VEHICLE_PARAMS,
    PID_SLOTS_DASH,
    PID_SLOTS_LIVE,
    PID_GROUP_PICKER,
    PID_SENSOR_PICKER,
    BLUETOOTH_DEVICES,
    TOUCH_CALIBRATE,
    OBD_LOG,
    ABOUT
};

enum class ResetConfirmStage : uint8_t {
    NORMAL = 0,
    CONFIRM,
    TRIGGERED
};

#ifndef UNIT_TEST
class SettingsPage : public Page {
public:
    void create(lv_obj_t* parent) override;
    const char* title() const override;
    void onShow() override;
    void onHide() override;
    bool onBack() override;
    void update(const VehicleState& state, uint32_t nowMs) override;
    void onLayout(int32_t contentW) override;
    void applyTheme() override;

    SettingsView currentView() const { return currentView_; }
    uint8_t rootPage() const { return selectedIndex_ / 3; }

private:
    void renderCurrentView();
    void renderRootMenu();
    void renderProtocolPicker();
    void renderVehicleParams();
    void renderPidSlots();
    void renderPidGroupPicker();
    void renderPidSensorPicker();
    void renderBluetoothDevices();
    void renderTouchCalibrate();
    void renderObdLog();
    void renderAbout();
    void refreshLogCache();

    void setRow(uint8_t i, const char* icon, const char* labelText, const char* subText, const char* valText);
    void highlightRow(uint8_t i, bool isSelected);

    // Event callbacks (< ^ v SELECT D-pad footer)
    static void onBackClicked(lv_event_t* e);
    static void onUpClicked(lv_event_t* e);
    static void onDownClicked(lv_event_t* e);
    static void onSelectEvent(lv_event_t* e);

    void handleBack(bool playTone = true);
    void handleUp();
    void handleDown();
    void handleSelect();
    void handleSelectLongPress();

    // 3 Static Rows (allocated once in create(), reused everywhere - 48px height touch target)
    // Non-clickable to prevent accidental touches while driving; navigated via footer buttons.
    lv_obj_t* rows_[3]{ nullptr };
    lv_obj_t* rowIcons_[3]{ nullptr };
    lv_obj_t* rowLabels_[3]{ nullptr };
    lv_obj_t* rowSubLabels_[3]{ nullptr };
    lv_obj_t* rowValues_[3]{ nullptr };

    // About View Card (overlays the 3 rows area when currentView_ == ABOUT)
    lv_obj_t* aboutCard_ = nullptr;
    lv_obj_t* aboutVersionLbl_ = nullptr;
    lv_obj_t* aboutDivider_ = nullptr;
    lv_obj_t* aboutGithubIcon_ = nullptr;
    lv_obj_t* aboutRepoLbl_ = nullptr;

    // Footer Bar (< ^ v SELECT)
    lv_obj_t* footerContainer_ = nullptr;
    lv_obj_t* btnBack_ = nullptr;
    lv_obj_t* lblBack_ = nullptr;
    lv_obj_t* btnUp_ = nullptr;
    lv_obj_t* lblUp_ = nullptr;
    lv_obj_t* btnDown_ = nullptr;
    lv_obj_t* lblDown_ = nullptr;
    lv_obj_t* btnSelect_ = nullptr;
    lv_obj_t* lblSelect_ = nullptr;

    // State
    SettingsView currentView_ = SettingsView::ROOT_MENU;
    uint8_t selectedIndex_ = 0;  // Active item index within current view
    uint8_t targetSlot_ = 0;     // slot currently being edited
    bool isDashSlots_ = true;    // true = editing dash slots, false = editing live slots
    SuperPidGroup targetGroup_ = SuperPidGroup::ENGINE_THERMAL;
    ResetConfirmStage resetStage_ = ResetConfirmStage::NORMAL;
    uint32_t resetConfirmStartMs_ = 0;
    size_t lastBtCount_ = 0;
    bool lastBtScanning_ = false;
    uint8_t lastBtState_ = 0;
    uint32_t lastBtAnimMs_ = 0;
    bool rpmLongPressActive_ = false;
    bool logPaused_ = false;
    uint32_t lastLogRefreshMs_ = 0;
    static constexpr size_t kMaxCachedLogs = Elm327Engine::kMaxLogEntries;
    ObdLogEntry cachedLogs_[kMaxCachedLogs]{};
    size_t cachedLogCount_ = 0;
    mutable char titleBuf_[40]{ 0 };
};
#endif

} // namespace ui
