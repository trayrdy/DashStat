#ifndef UNIT_TEST
#include "ui/shell/ui_shell.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "core/util.h"
#include "hal/buzzer_controller.h"
#include "hal/lvgl_hal.h"
#include "model/telemetry_sync.h"
#include "model/trip_computer.h"
#include "model/settings_manager.h"
#include "model/alert_monitor.h"
#include "transport/ble_transport.h"

namespace ui {

void UiShell::onNavSelect(PageId page) {
    auto& shell = instance();
    if (shell.drivingLock_.isLocked) {
        shell.drivingLock_.triggerTouchAttempt(millis());
        BuzzerController::instance().playTouchWhenLock();
        return;
    }
    BuzzerController::instance().playNavrailTouch();
    Serial.printf("[UiShell] onNavSelect: %d\n", static_cast<int>(page));
    shell.navigate(page);
}

void UiShell::onContentClicked(lv_event_t* e) {
    UiShell* shell = static_cast<UiShell*>(lv_event_get_user_data(e));
    if (shell == nullptr) return;

    if (shell->drivingLock_.isLocked) {
        shell->drivingLock_.triggerTouchAttempt(millis());
        BuzzerController::instance().playTouchWhenLock();
        return;
    }

    // Only allow tap-to-toggle NavRail on Dashboard.
    // Cut toggle functionality on Live Sensor page to avoid accidental collapsing and scale distortion.
    if (shell->currentPage_ != PageId::Dashboard) return;

    // Driving Safety Lock: ignore taps while moving
    if (!shell->isSafeToOperate_) return;

    NavRailMode railMode = SettingsManager::instance().get().navRailMode;
    // In ALWAYS_ON and TAP_TO_CALL, tapping dashboard content must NOT toggle the rail.
    // In TAP_TO_CALL, toggling is done exclusively by tapping the topbar.
    if (railMode != NavRailMode::AUTO_HIDE) {
        return;
    }

    BuzzerController::instance().playTapTick();
    shell->toggleNavRail();
}

void UiShell::onTopBarClicked(lv_event_t* e) {
    UiShell* shell = static_cast<UiShell*>(lv_event_get_user_data(e));
    if (shell == nullptr) return;

    if (shell->drivingLock_.isLocked) {
        shell->drivingLock_.triggerTouchAttempt(millis());
        BuzzerController::instance().playTouchWhenLock();
        return;
    }

    NavRailMode railMode = SettingsManager::instance().get().navRailMode;
    if (railMode == NavRailMode::ALWAYS_ON) {
        return;
    }

    BuzzerController::instance().playTapTick();
    shell->toggleNavRail();
}

void UiShell::onBackClicked(lv_event_t* e) {
    UiShell* shell = static_cast<UiShell*>(lv_event_get_user_data(e));
    if (shell == nullptr) return;

    if (shell->drivingLock_.isLocked) {
        shell->drivingLock_.triggerTouchAttempt(millis());
        BuzzerController::instance().playTouchWhenLock();
        return;
    }

    Serial.printf("[UiShell] onBackClicked on page %d\n", static_cast<int>(shell->currentPage_));
    BuzzerController::instance().playButtonBack();
    Page* p = shell->pages_[static_cast<int>(shell->currentPage_)];
    if (p != nullptr && p->onBack()) {
        return;
    }
    if (shell->currentPage_ != PageId::Dashboard) {
        shell->navigate(PageId::Dashboard);
    }
}

void UiShell::applyLayout(int32_t railWidth) {
    const int32_t contentW = layout::kScreenW - railWidth;
    rail_.setPos(railWidth - layout::kRailW, 0);
    statusBar_.setLayout(railWidth, contentW);
    if (contentArea_ != nullptr) {
        lv_obj_set_pos(contentArea_, railWidth, layout::kStatusH);
        lv_obj_set_size(contentArea_, contentW, layout::kContentH);
    }
    for (int i = 0; i < static_cast<int>(PageId::Count); ++i) {
        if (pages_[i] != nullptr) {
            pages_[i]->onLayout(contentW);
        }
    }
}

void UiShell::setNavRailVisible(bool visible) {
    if (railVisible_ == visible) return;
    railVisible_ = visible;
    applyLayout(visible ? layout::kRailW : 0);
}

void UiShell::toggleNavRail() {
    setNavRailVisible(!railVisible_);
}

void UiShell::init(lv_obj_t* parentScreen) {
    // 1. Root Screen Style
    lv_obj_set_style_bg_color(parentScreen, color::bg(), 0);
    lv_obj_set_style_bg_opa(parentScreen, LV_OPA_COVER, 0);

    // 2. Create Left NavRail (56 x 240)
    rail_.create(parentScreen, onNavSelect);

    // 3. Create Top StatusBar (264 x 28)
    statusBar_.create(parentScreen, onBackClicked, this);
    lv_obj_add_flag(statusBar_.root(), LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(statusBar_.root(), onTopBarClicked, LV_EVENT_CLICKED, this);

    // 4. Create Content Area (264 x 212)
    contentArea_ = box(parentScreen);
    lv_obj_set_pos(contentArea_, layout::kRailW, layout::kStatusH);
    lv_obj_set_size(contentArea_, layout::kContentW, layout::kContentH);
    lv_obj_set_style_bg_color(contentArea_, color::bg(), 0);
    lv_obj_set_style_bg_opa(contentArea_, LV_OPA_COVER, 0);
    lv_obj_set_clickable(contentArea_, true);
    lv_obj_add_event_cb(contentArea_, onContentClicked, LV_EVENT_CLICKED, this);

    // 5. Pre-allocate Page Stubs inside Content Area
    pages_[static_cast<int>(PageId::Dashboard)] = &dashPage_;
    pages_[static_cast<int>(PageId::Live)] = &livePage_;
    pages_[static_cast<int>(PageId::Diagnostics)] = &diagPage_;
    pages_[static_cast<int>(PageId::Trip)] = &tripPage_;
    pages_[static_cast<int>(PageId::Settings)] = &settingsPage_;

    for (int i = 0; i < static_cast<int>(PageId::Count); ++i) {
        if (i == static_cast<int>(PageId::Diagnostics)) continue; // Lazy-load Diagnostics on demand to maximize free heap
        if (pages_[i] != nullptr) {
            pages_[i]->create(contentArea_);
            set_visible(pages_[i]->root(), false);
        }
    }

    // 6. Navigate to initial page (Dashboard)
    currentPage_ = PageId::Dashboard;
    NavRailMode initRailMode = SettingsManager::instance().get().navRailMode;
    railVisible_ = (initRailMode != NavRailMode::TAP_TO_CALL);
    applyLayout(railVisible_ ? layout::kRailW : 0);
    if (pages_[static_cast<int>(currentPage_)] != nullptr) {
        set_visible(pages_[static_cast<int>(currentPage_)]->root(), true);
        statusBar_.setTitle(pages_[static_cast<int>(currentPage_)]->title());
        rail_.setActive(currentPage_);
        pages_[static_cast<int>(currentPage_)]->onShow();
    }
}

void UiShell::navigate(PageId id) {
    if (id >= PageId::Count || contentArea_ == nullptr) return;
    if (id == currentPage_ || id == pendingPage_) return;
    Serial.printf("[UiShell] navigate: %d -> %d\n", static_cast<int>(currentPage_), static_cast<int>(id));
    pendingPage_ = id;
}

void UiShell::applyTheme() {
    lv_obj_t* scr = lv_screen_active();
    if (scr != nullptr) {
        lv_obj_set_style_bg_color(scr, color::bg(), 0);
    }
    if (contentArea_ != nullptr) {
        lv_obj_set_style_bg_color(contentArea_, color::bg(), 0);
    }
    rail_.applyTheme();
    for (int i = 0; i < static_cast<int>(PageId::Count); ++i) {
        if (pages_[i] != nullptr) {
            pages_[i]->applyTheme();
        }
    }
    if (pages_[static_cast<int>(currentPage_)] != nullptr) {
        statusBar_.setTitle(pages_[static_cast<int>(currentPage_)]->title());
    }
    if (scr != nullptr) {
        lv_obj_invalidate(scr);
    }
}

void UiShell::refreshActivePage() {
    Page* p = pages_[static_cast<int>(currentPage_)];
    if (p != nullptr) {
        p->onShow();
    }
}

void UiShell::update(const VehicleState& state, uint32_t nowMs) {
    // Process queued page navigation safely outside LVGL timer/event callbacks
    if (pendingPage_ != PageId::Count) {
        PageId target = pendingPage_;
        pendingPage_ = PageId::Count;

        // 1. Cut backlight to 0 (Dark) to conceal partial DMA scanline tearing
        const uint8_t prevBrightness = LvglHal::getBrightness();
        if (prevBrightness > 0) {
            LvglHal::setBrightness(0);
        }

        // 2. Hide old page (destroy if Diagnostics to free RAM)
        Page* oldPage = pages_[static_cast<int>(currentPage_)];
        if (oldPage != nullptr) {
            oldPage->onHide();
            if (currentPage_ == PageId::Diagnostics) {
                diagPage_.destroy();
            } else if (oldPage->root() != nullptr) {
                set_visible(oldPage->root(), false);
            }
        }

        // 3. Show new page (create on-demand if Diagnostics)
        currentPage_ = target;
        TelemetrySync::activePageId.store(static_cast<uint8_t>(currentPage_));
        NavRailMode railMode = SettingsManager::instance().get().navRailMode;
        if (currentPage_ == PageId::Dashboard) {
            if (railMode == NavRailMode::TAP_TO_CALL) {
                setNavRailVisible(false);
            } else if (railMode == NavRailMode::ALWAYS_ON) {
                setNavRailVisible(true);
            } else {
                setNavRailVisible(state.speedKmh < 5);
            }
        } else {
            if (railMode == NavRailMode::ALWAYS_ON) {
                setNavRailVisible(true);
            }
        }

        if (currentPage_ == PageId::Diagnostics) {
            diagPage_.create(contentArea_);
            int32_t w = railVisible_ ? layout::kContentW : layout::kScreenW;
            diagPage_.onLayout(w);
            diagPage_.applyTheme();
        }

        Page* newPage = pages_[static_cast<int>(currentPage_)];
        if (newPage != nullptr && newPage->root() != nullptr) {
            set_visible(newPage->root(), true);
            rail_.setActive(currentPage_);
            statusBar_.setTitle(newPage->title());
            newPage->onShow();
        }

        // 4. Force synchronous layout & full-screen render pass while screen is blacked out
        lv_obj_update_layout(lv_screen_active());
        lv_refr_now(LvglHal::getDisplay());
        LvglHal::waitDMA();

        // 5. Restore backlight cleanly to reveal completely rendered frame
        if (prevBrightness > 0) {
            LvglHal::setBrightness(prevBrightness);
        }
    }

    isDemoMode_ = state.isDemoMode;

    drivingLock_.update(state.speedKmh, nowMs);
    statusBar_.setDrivingLock(drivingLock_.isLocked, drivingLock_.isFlashing(nowMs));

    if (state.speedKmh > 0) {
        stationaryStartMs_ = 0;
        isSafeToOperate_ = false;
    } else {
        if (stationaryStartMs_ == 0) {
            stationaryStartMs_ = nowMs;
        }
        uint32_t stillMs = core::elapsed(nowMs, stationaryStartMs_);
        isSafeToOperate_ = core::is_safe_to_operate_ui(state.speedKmh, state.gear, state.handbrakeActive, stillMs);
    }

    if (!isDemoMode_ && drivingLock_.isLocked && (currentPage_ == PageId::Settings || currentPage_ == PageId::Diagnostics)) {
        navigate(PageId::Dashboard);
    }

    // NavRail mode enforcement (runs in both demo & live car modes)
    static NavRailMode cachedRailMode = SettingsManager::instance().get().navRailMode;
    static uint32_t lastShellSettingsVer = SettingsManager::instance().version();
    uint32_t curShellVer = SettingsManager::instance().version();
    if (curShellVer != lastShellSettingsVer) {
        cachedRailMode = SettingsManager::instance().get().navRailMode;
        lastShellSettingsVer = curShellVer;
    }
    NavRailMode railMode = cachedRailMode;
    if (railMode == NavRailMode::ALWAYS_ON) {
        if (!railVisible_) setNavRailVisible(true);
    } else if (railMode == NavRailMode::AUTO_HIDE) {
        if (state.speedKmh >= 5) {
            if (railVisible_) setNavRailVisible(false);
        } else if (prevSpeedKmh_ >= 5 && state.speedKmh < 5) {
            if (!railVisible_) setNavRailVisible(true);
        }
    } else if (railMode == NavRailMode::TAP_TO_CALL) {
        // Controlled exclusively by topbar touch on all pages. Speed is not a factor.
    }
    prevSpeedKmh_ = state.speedKmh;

    statusBar_.update(state, nowMs);

    // Continuous real-time trip & performance tracking across all screens
    TripComputer::instance().update(state.speedKmh, state.isEngineRunning, nowMs);

    // Vehicle telemetry alerts (Speed alert > 90, MIL on, PID warnings)
    AlertMonitor::instance().update(state, nowMs);

    Page* p = pages_[static_cast<int>(currentPage_)];
    if (p != nullptr) {
        p->update(state, nowMs);
    }
}

void UiShell::triggerLockFlash() {
    drivingLock_.triggerTouchAttempt(millis());
    BuzzerController::instance().playTouchWhenLock();
}

}  // namespace ui
#endif // UNIT_TEST
