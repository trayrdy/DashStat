#include "model/alert_monitor.h"
#include "hal/buzzer_controller.h"
#include "model/settings_manager.h"
#include "model/trip_computer.h"
#include "obd/universal_pid_catalog.h"

void AlertMonitor::reset() {
    prevSpeed_ = 0;
    prevMilActive_ = false;
    prevWarnMask_ = 0;
    hasInitialized_ = false;
    speedAlertArmed_ = true;
    speedAlertCount_ = 0;
    milAlertCount_ = 0;
    pidAlertCount_ = 0;
}

static uint32_t calculateWarnMask(const VehicleState& state) {
    if (!state.isConnected) return 0;

    uint32_t mask = 0;
    const auto& settings = SettingsManager::instance().get();

    // Bit 0: RPM Warning Zone
    if (state.rpm > 0 && state.rpm >= settings.warnRpm) {
        mask |= (1U << 0);
    }

    // Bit 1: Coolant Temp Warning Zone (>= 100°C)
    if (state.coolantTempC > 0 && UniversalPidCatalog::getAlertLevel(0x05, static_cast<float>(state.coolantTempC)) >= 1) {
        mask |= (1U << 1);
    }

    // Bit 3: Battery Voltage Warning Zone (< 12.0V or > 14.8V)
    if (state.batteryVoltage > 5.0f && UniversalPidCatalog::getAlertLevel(0xFF, state.batteryVoltage) >= 1) {
        mask |= (1U << 3);
    }

    // Bits 4..9: 6 Live Slots
    for (uint8_t i = 0; i < 6; ++i) {
        if (i < sizeof(settings.liveSlots)) {
            if (settings.liveSlots[i] == 0xFF) continue;
            const auto& def = UniversalPidCatalog::get(settings.liveSlots[i]);
            float val = state.liveSlotValues[i];
            uint8_t lvl = UniversalPidCatalog::getAlertLevel(def.pidHex, val);
            if (lvl >= 1) {
                mask |= (1U << (4 + i));
            }
        }
    }

    // Bits 10..13: 4 Dashboard Slots
    for (uint8_t i = 0; i < 4; ++i) {
        if (i < sizeof(settings.dashSlots)) {
            if (settings.dashSlots[i] == 0xFF) continue;
            const auto& def = UniversalPidCatalog::get(settings.dashSlots[i]);
            float val = state.dashSlotValues[i];
            uint8_t lvl = UniversalPidCatalog::getAlertLevel(def.pidHex, val);
            if (lvl >= 1) {
                mask |= (1U << (10 + i));
            }
        }
    }

    return mask;
}

void AlertMonitor::update(const VehicleState& state, uint32_t nowMs) {
    (void)nowMs;

    if (!hasInitialized_) {
        prevSpeed_ = state.speedKmh;
        prevMilActive_ = state.milActive;
        prevWarnMask_ = calculateWarnMask(state);
        speedAlertArmed_ = (state.speedKmh < 90);
        hasInitialized_ = true;
        return;
    }

    // Re-arm speed alert when speed drops below 88 km/h (hysteresis against 89-90 flutter)
    if (state.speedKmh < 88) {
        speedAlertArmed_ = true;
    }

    // 1. Speed Alert: Crossing >= 90 km/h (specifically edge 89 -> 90) during normal driving
    // In sprint modes (0-100, 1/4 mile, 1 mile), speed alert is suppressed.
    // Decelerating 90 -> 89 does NOT alert. Alert only 1 loop (6 beeps).
    if (speedAlertArmed_ && state.speedKmh >= 90) {
        speedAlertArmed_ = false;
        AccelTimerState accelState = TripComputer::instance().getAccelState();
        bool inSprint = TripComputer::instance().isSprintActive() &&
                        (accelState == AccelTimerState::ARMED || accelState == AccelTimerState::RUNNING);
        if (!inSprint) {
            speedAlertCount_++;
            BuzzerController::instance().playSpeedAlert();
        }
    }
    prevSpeed_ = state.speedKmh;

    bool triggeredThisTick = false;

    // 2. Check Engine (MIL) & Monitored PIDs Alert (Aleart * =-- -- --)
    if (state.isConnected) {
        // MIL Edge Trigger: False -> True
        if (!prevMilActive_ && state.milActive) {
            milAlertCount_++;
            BuzzerController::instance().playAlert();
            triggeredThisTick = true;
        }
        prevMilActive_ = state.milActive;

        // Monitored PIDs Entering Warning Zone Edge Trigger
        uint32_t currWarnMask = calculateWarnMask(state);

        // Rising Edge Detection: Any PID newly entering warning zone
        uint32_t newlyWarned = currWarnMask & (~prevWarnMask_);
        if (newlyWarned != 0) {
            pidAlertCount_++;
            if (!triggeredThisTick) {
                BuzzerController::instance().playAlert();
            }
        }
        prevWarnMask_ = currWarnMask;
    } else {
        prevMilActive_ = false;
        prevWarnMask_ = 0;
    }
}
