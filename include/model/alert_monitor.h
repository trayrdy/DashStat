#pragma once
#include <stdint.h>
#include "model/vehicle_state.h"

class AlertMonitor {
public:
    static AlertMonitor& instance() {
        static AlertMonitor inst;
        return inst;
    }

    void reset();
    void update(const VehicleState& state, uint32_t nowMs);

    uint8_t getPrevSpeed() const { return prevSpeed_; }
    bool getPrevMilActive() const { return prevMilActive_; }
    uint32_t getPrevWarnMask() const { return prevWarnMask_; }

    uint32_t getSpeedAlertCount() const { return speedAlertCount_; }
    uint32_t getMilAlertCount() const { return milAlertCount_; }
    uint32_t getPidAlertCount() const { return pidAlertCount_; }
    bool isSpeedAlertArmed() const { return speedAlertArmed_; }

private:
    AlertMonitor() = default;

    uint8_t prevSpeed_ = 0;
    bool prevMilActive_ = false;
    uint32_t prevWarnMask_ = 0;
    bool hasInitialized_ = false;
    bool speedAlertArmed_ = false;

    uint32_t speedAlertCount_ = 0;
    uint32_t milAlertCount_ = 0;
    uint32_t pidAlertCount_ = 0;
};
