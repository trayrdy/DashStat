#pragma once
#ifndef UNIT_TEST
#include <lvgl.h>
#include "model/vehicle_state.h"
#include "model/debug_metrics.h"
#include "ui/shell/ui_shell.h"
#include "ui/theme/theme_manager.h"

enum class BootState : uint8_t {
    BOOT0_SPLASH = 0,
    BOOT1_NO_MAC = 1,
    BOOT1_CONNECTING = 2,
    BOOT1_CONNECTED = 3,
    BOOT1_DEMO = 4,
    DONE = 5
};

class UiManager {
public:
    static bool init();
    static void update(const VehicleState& state);
    static void switchScreen(uint8_t screenIdx);
    static void applyTheme();
    static uint8_t getCurrentScreen() { return static_cast<uint8_t>(ui::UiShell::instance().currentPage()); }
    static const DebugMetrics& getMetrics() { return metrics_; }
    static bool isBootComplete() { return bootState_ == BootState::DONE; }

private:
    static void handleBootSequence(uint32_t now);
    static void finishBoot();

    static inline lv_obj_t* rootScreen_ = nullptr;
    static inline DebugMetrics metrics_;

    static inline uint32_t frameCount_ = 0;
    static inline uint32_t lastFpsCalcTime_ = 0;

    static inline BootState bootState_ = BootState::BOOT0_SPLASH;
    static inline uint32_t bootStartMs_ = 0;
    static inline uint32_t boot1SubStateMs_ = 0;
    static inline lv_obj_t* bootContainer_ = nullptr;
    static inline lv_obj_t* bootTitle_ = nullptr;
    static inline lv_obj_t* bootSubtitle_ = nullptr;
    static inline lv_obj_t* bootAccentLine_ = nullptr;
    static inline lv_obj_t* bootStatusLabel_ = nullptr;
};
#endif
