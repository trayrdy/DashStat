#ifndef UNIT_TEST
#include "ui/ui_manager.h"
#include "obd/command_guard.h"
#include "hal/buzzer_controller.h"
#include "hal/lvgl_hal.h"
#include "config/hardware_pins.h"
#include "model/settings_manager.h"
#include "transport/ble_transport.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/fonts/ui_fonts.h"
#include <Arduino.h>

bool UiManager::init() {
    // SettingsManager::begin() is already called in setup()
    const AppSettings& settings = SettingsManager::instance().get();

    // Restore persisted theme
    ThemeManager::setTheme(static_cast<ThemeId>(settings.themeIndex));

    // Restore persisted buzzer mode & volume
    BuzzerController::instance().setMode(settings.buzzerMode);
    BuzzerController::instance().setVolume(settings.buzzerVolume);

    // Restore persisted screen rotation
    LvglHal::setRotation((settings.screenRotation == ScreenRotation::INVERTED) ? 3 : 1);

    // Restore initial display brightness
    if (settings.brightnessMode == BrightnessMode::MANUAL) {
        LvglHal::setBrightness(static_cast<uint8_t>(settings.manualBrightness * 2.55f));
    } else {
        uint16_t ldrRaw = analogRead(CYD_LDR_PIN);
        uint8_t targetDuty = map(constrain(ldrRaw, 200, 3800), 200, 3800, 255, 40);
        LvglHal::setBrightness(targetDuty);
    }

    rootScreen_ = lv_screen_active();

    // Initialize DashStat UI Shell
    ui::UiShell::instance().init(rootScreen_);

    // Setup Boot 0 Overlay (pitch black fullscreen overlay on top of shell)
    bootContainer_ = ui::box(rootScreen_);
    lv_obj_set_size(bootContainer_, 320, 240);
    lv_obj_set_pos(bootContainer_, 0, 0);
    lv_obj_set_style_bg_color(bootContainer_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(bootContainer_, LV_OPA_COVER, 0);
    lv_obj_add_flag(bootContainer_, LV_OBJ_FLAG_CLICKABLE);

    bootTitle_ = ui::label(bootContainer_, &ui_font_dashstat, ui::color::text(), "DASHSTAT");
    lv_obj_align(bootTitle_, LV_ALIGN_CENTER, 0, -18);

    bootSubtitle_ = ui::label(bootContainer_, ui::font::subtitle(), ui::color::text_dim(), "Simple Clean DASH!!");
    lv_obj_align(bootSubtitle_, LV_ALIGN_CENTER, 0, 16);

    bootAccentLine_ = ui::box(bootContainer_);
    lv_obj_set_size(bootAccentLine_, 0, 3);
    lv_obj_set_style_bg_color(bootAccentLine_, ui::color::accent(), 0);
    lv_obj_set_style_bg_opa(bootAccentLine_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bootAccentLine_, 2, 0);
    lv_obj_align(bootAccentLine_, LV_ALIGN_CENTER, 0, 36);

    bootStartMs_ = millis();
    boot1SubStateMs_ = bootStartMs_;
    bootState_ = BootState::BOOT0_SPLASH;

    lastFpsCalcTime_ = millis();
    return true;
}

void UiManager::switchScreen(uint8_t screenIdx) {
    if (bootState_ != BootState::DONE) return;
    if (screenIdx < static_cast<uint8_t>(ui::PageId::Count)) {
        ui::UiShell::instance().navigate(static_cast<ui::PageId>(screenIdx));
        BuzzerController::instance().playTapTick();
    }
}

void UiManager::applyTheme() {
    if (bootState_ != BootState::DONE) return;
    const AppSettings& settings = SettingsManager::instance().get();
    ThemeManager::setTheme(static_cast<ThemeId>(settings.themeIndex));
    ui::UiShell::instance().applyTheme();
    BuzzerController::instance().playTapTick();
}

void UiManager::finishBoot() {
    bootState_ = BootState::DONE;
    if (bootContainer_ != nullptr) {
        lv_obj_delete(bootContainer_);
        bootContainer_ = nullptr;
        bootTitle_ = nullptr;
        bootSubtitle_ = nullptr;
        bootAccentLine_ = nullptr;
        bootStatusLabel_ = nullptr;
    }
    ui::UiShell::instance().applyTheme();
}

void UiManager::handleBootSequence(uint32_t now) {
    switch (bootState_) {
    case BootState::BOOT0_SPLASH: {
        uint32_t elapsed = now - bootStartMs_;
        if (elapsed < 5000) {
            int32_t w = (static_cast<int64_t>(elapsed) * 160) / 5000;
            if (w < 0) w = 0;
            if (w > 160) w = 160;
            if (bootAccentLine_ && lv_obj_get_width(bootAccentLine_) != w) {
                lv_obj_set_size(bootAccentLine_, w, 3);
                lv_obj_align(bootAccentLine_, LV_ALIGN_CENTER, 0, 36);
            }
        } else {
            // Boot 0 complete -> Transition to Boot 1
            if (bootTitle_) { lv_obj_delete(bootTitle_); bootTitle_ = nullptr; }
            if (bootSubtitle_) { lv_obj_delete(bootSubtitle_); bootSubtitle_ = nullptr; }
            if (bootAccentLine_) { lv_obj_delete(bootAccentLine_); bootAccentLine_ = nullptr; }

            bootStatusLabel_ = ui::label(bootContainer_, ui::font::value(), ui::color::text(), "");
            lv_obj_set_style_text_align(bootStatusLabel_, LV_TEXT_ALIGN_CENTER, 0);

            bootStartMs_ = now;
            boot1SubStateMs_ = now;

            const AppSettings& s = SettingsManager::instance().get();
            if (s.operationMode == OperationMode::DEMO) {
                ui::set_text(bootStatusLabel_, "Booting in Demo mode...");
                lv_obj_align(bootStatusLabel_, LV_ALIGN_CENTER, 0, 0);
                bootState_ = BootState::BOOT1_DEMO;
            } else if (s.savedBtMac[0] == '\0') {
                ui::set_text(bootStatusLabel_, "No Paired OBD");
                lv_obj_align(bootStatusLabel_, LV_ALIGN_CENTER, 0, 0);
                bootState_ = BootState::BOOT1_NO_MAC;
            } else {
                ui::set_text(bootStatusLabel_, "Connecting...");
                lv_obj_align(bootStatusLabel_, LV_ALIGN_CENTER, 0, 0);
                bootState_ = BootState::BOOT1_CONNECTING;
            }
        }
        break;
    }

    case BootState::BOOT1_NO_MAC: {
        if (now - boot1SubStateMs_ >= 2500) {
            finishBoot();
        }
        break;
    }

    case BootState::BOOT1_CONNECTING: {
        if (BleTransport::instance().isConnected() || (now - bootStartMs_ >= 8000)) {
            if (bootStatusLabel_) {
                ui::set_text(bootStatusLabel_, "Booting...");
                lv_obj_align(bootStatusLabel_, LV_ALIGN_CENTER, 0, 0);
            }
            boot1SubStateMs_ = now;
            bootState_ = BootState::BOOT1_CONNECTED;
        }
        break;
    }

    case BootState::BOOT1_CONNECTED: {
        if (now - boot1SubStateMs_ >= 2000) {
            finishBoot();
        }
        break;
    }

    case BootState::BOOT1_DEMO: {
        if (now - boot1SubStateMs_ >= 2000) {
            finishBoot();
        }
        break;
    }

    case BootState::DONE:
    default:
        break;
    }
}

void UiManager::update(const VehicleState& state) {
    uint32_t now = millis();

    // 0. Drive Buzzer pattern state machine
    BuzzerController::instance().update(now);

    // 1. Boot sequence state machine or normal UI Shell update
    if (bootState_ != BootState::DONE) {
        handleBootSequence(now);
    } else {
        ui::UiShell::instance().update(state, now);
    }

    // 2. Dynamic backlight control (Auto LDR or Manual enforcement)
    static uint32_t lastLdrCheck = 0;
    if (now - lastLdrCheck >= 200) { // Check every 200ms
        lastLdrCheck = now;
        static AppSettings cachedUiSettings = SettingsManager::instance().get();
        static uint32_t lastUiSettingsVer = SettingsManager::instance().version();
        uint32_t curUiVer = SettingsManager::instance().version();
        if (curUiVer != lastUiSettingsVer) {
            cachedUiSettings = SettingsManager::instance().get();
            lastUiSettingsVer = curUiVer;
        }
        const AppSettings& s = cachedUiSettings;
        if (s.brightnessMode == BrightnessMode::AUTO) {
            uint16_t ldrRaw = analogRead(CYD_LDR_PIN);
            // CYD LDR divider: ~4000 in dark, ~200 in direct light.
            // Dark environment needs dim screen (40), bright environment needs bright screen (255).
            uint8_t targetDuty = map(constrain(ldrRaw, 200, 3800), 200, 3800, 255, 40);
            LvglHal::setBrightness(targetDuty);
        } else {
            uint8_t manualDuty = static_cast<uint8_t>(s.manualBrightness * 2.55f);
            if (LvglHal::getBrightness() != manualDuty) {
                LvglHal::setBrightness(manualDuty);
            }
        }
    }

    // 3. FPS & Profiler calculation (every 1s)
    frameCount_++;
    uint32_t elapsed = now - lastFpsCalcTime_;
    if (elapsed >= 1000) {
        float fps = (frameCount_ * 1000.0f) / elapsed;
        metrics_.currentFps = fps;
        metrics_.freeHeapBytes = ESP.getFreeHeap();

        frameCount_ = 0;
        lastFpsCalcTime_ = now;
    }
}
#endif // UNIT_TEST
