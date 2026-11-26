#pragma once
#include <lvgl.h>
#include <stdint.h>
#include "core/util.h"
#include "model/vehicle_state.h"
#include "ui/pages/page.h"
#include "ui/components/nav_rail.h"
#include "ui/components/status_bar.h"
#include "ui/pages/dashboard_page.h"
#include "ui/pages/live_page.h"
#include "ui/pages/trip_page.h"
#include "ui/pages/settings_page.h"
#include "ui/pages/diagnostics_page.h"

namespace ui {

class UiShell {
public:
    static UiShell& instance() {
        static UiShell inst;
        return inst;
    }

    void init(lv_obj_t* parentScreen);
    void navigate(PageId id);
    void applyTheme();
    void update(const VehicleState& state, uint32_t nowMs);
    PageId currentPage() const { return currentPage_; }

    void setNavRailVisible(bool visible);
    bool isNavRailVisible() const { return railVisible_; }
    void toggleNavRail();

    void showBackButton(bool show) { statusBar_.showBack(show); }
    void setTitle(const char* title) { statusBar_.setTitle(title); }
    void refreshActivePage();

    bool isDrivingLocked() const { return drivingLock_.isLocked; }
    void triggerLockFlash();

private:
    UiShell() = default;

    static void onNavSelect(PageId page);
    static void onContentClicked(lv_event_t* e);
    static void onBackClicked(lv_event_t* e);
    static void onTopBarClicked(lv_event_t* e);

    void applyLayout(int32_t railWidth);

    NavRail rail_;
    StatusBar statusBar_;
    lv_obj_t* contentArea_ = nullptr;

    DashboardPage dashPage_;
    LivePage livePage_;
    DiagnosticsPage diagPage_;
    TripPage tripPage_;
    SettingsPage settingsPage_;

    Page* pages_[static_cast<int>(PageId::Count)] = {};
    PageId currentPage_ = PageId::Dashboard;
    PageId pendingPage_ = PageId::Count;

    bool railVisible_ = true;
    bool isSafeToOperate_ = true;
    uint32_t stationaryStartMs_ = 0;
    bool isDemoMode_ = false;
    uint8_t prevSpeedKmh_ = 0;
    core::DrivingLockState drivingLock_;
};

}  // namespace ui
