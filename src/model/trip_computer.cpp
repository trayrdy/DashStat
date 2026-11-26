#include "model/trip_computer.h"
#ifndef UNIT_TEST
#include <Arduino.h>
#include <Preferences.h>
#endif
#include <cmath>
#include "hal/buzzer_controller.h"

TripComputer& TripComputer::instance() {
    static TripComputer inst;
    return inst;
}

TripComputer::TripComputer() {
    loadFromNvs();
}

void TripComputer::begin() {
    loadFromNvs();
}

void TripComputer::loadFromNvs() {
#ifndef UNIT_TEST
    Preferences prefs;
    if (prefs.begin("dashstat_trip", true)) {
        sprintRecords_[static_cast<uint8_t>(SprintMode::SPEED_0_100)].bestTimeSec = prefs.getFloat("best_0_100", 0.0f);
        sprintRecords_[static_cast<uint8_t>(SprintMode::SPEED_0_100)].bestTrapSpeedKmh = prefs.getFloat("trap_0_100", 0.0f);

        sprintRecords_[static_cast<uint8_t>(SprintMode::DISTANCE_QUARTER_MILE)].bestTimeSec = prefs.getFloat("best_quarter", 0.0f);
        sprintRecords_[static_cast<uint8_t>(SprintMode::DISTANCE_QUARTER_MILE)].bestTrapSpeedKmh = prefs.getFloat("trap_quarter", 0.0f);

        sprintRecords_[static_cast<uint8_t>(SprintMode::DISTANCE_ONE_MILE)].bestTimeSec = prefs.getFloat("best_mile", 0.0f);
        sprintRecords_[static_cast<uint8_t>(SprintMode::DISTANCE_ONE_MILE)].bestTrapSpeedKmh = prefs.getFloat("trap_mile", 0.0f);
        prefs.end();
    }
#endif
}

void TripComputer::saveSprintRecordToNvs(SprintMode mode) {
#ifndef UNIT_TEST
    BuzzerController::instance().mute();
    Preferences prefs;
    if (prefs.begin("dashstat_trip", false)) {
        const auto& rec = sprintRecords_[static_cast<uint8_t>(mode)];
        if (mode == SprintMode::SPEED_0_100) {
            prefs.putFloat("best_0_100", rec.bestTimeSec);
            prefs.putFloat("trap_0_100", rec.bestTrapSpeedKmh);
        } else if (mode == SprintMode::DISTANCE_QUARTER_MILE) {
            prefs.putFloat("best_quarter", rec.bestTimeSec);
            prefs.putFloat("trap_quarter", rec.bestTrapSpeedKmh);
        } else if (mode == SprintMode::DISTANCE_ONE_MILE) {
            prefs.putFloat("best_mile", rec.bestTimeSec);
            prefs.putFloat("trap_mile", rec.bestTrapSpeedKmh);
        }
        prefs.end();
    }
#endif
}

void TripComputer::resetTrip() {
    telemetry_ = TripTelemetry{};
    lastUpdateMs_ = 0;
    hasLastUpdate_ = false;
    driveMsAcc_ = 0;
    movingMsAcc_ = 0;
    sprintActive_ = false;
    accelState_ = AccelTimerState::IDLE;
    stoppedSinceMs_ = 0;
}

void TripComputer::resetSprintBest(SprintMode mode) {
    auto& rec = sprintRecords_[static_cast<uint8_t>(mode)];
    rec.bestTimeSec = 0.0f;
    rec.bestTrapSpeedKmh = 0.0f;
    rec.lastTimeSec = 0.0f;
    rec.lastTrapSpeedKmh = 0.0f;
    saveSprintRecordToNvs(mode);
}

void TripComputer::setSprintMode(SprintMode mode) {
    if (mode < SprintMode::COUNT) {
        sprintMode_ = mode;
        sprintActive_ = true;
        accelState_ = AccelTimerState::IDLE;
        stoppedSinceMs_ = 0;
        accelStartMs_ = 0;
        accelRunDistanceM_ = 0.0f;
    }
}

void TripComputer::setSprintActive(bool active) {
    sprintActive_ = active;
    if (!active) {
        accelState_ = AccelTimerState::IDLE;
        stoppedSinceMs_ = 0;
        accelStartMs_ = 0;
        accelRunDistanceM_ = 0.0f;
    }
}

float TripComputer::getCurrentRunSeconds(uint32_t nowMs) const {
    if (accelState_ == AccelTimerState::RUNNING && accelStartMs_ > 0 && nowMs >= accelStartMs_) {
        return (nowMs - accelStartMs_) / 1000.0f;
    }
    return 0.0f;
}

float TripComputer::getAverageSpeedKmh() const {
    if (telemetry_.movingTimeSec > 0) {
        float hours = telemetry_.movingTimeSec / 3600.0f;
        return telemetry_.distanceKm / hours;
    }
    return 0.0f;
}

void TripComputer::update(uint8_t currentSpeedKmh, bool isEngineRunning, uint32_t nowMs) {
    if (!hasLastUpdate_) {
        lastUpdateMs_ = nowMs;
        hasLastUpdate_ = true;
        if (currentSpeedKmh == 0) {
            stoppedSinceMs_ = nowMs;
        }
        return;
    }

    uint32_t dtMs = (nowMs >= lastUpdateMs_) ? (nowMs - lastUpdateMs_) : 0;
    lastUpdateMs_ = nowMs;

    // Ignore excessive pauses / gaps (> 1.5s)
    if (dtMs == 0 || dtMs > 1500) {
        return;
    }

    float speed = static_cast<float>(currentSpeedKmh);
    float dtHours = dtMs / 3600000.0f;

    // 1. Accumulate Distance & Speeds
    if (speed > 0.0f) {
        telemetry_.distanceKm += (speed * dtHours);
        if (speed > telemetry_.maxSpeedKmh) {
            telemetry_.maxSpeedKmh = speed;
        }
    }

    // 2. Engine Run Time & Moving Time
    if (isEngineRunning) {
        driveMsAcc_ += dtMs;
        telemetry_.driveTimeSec += driveMsAcc_ / 1000;
        driveMsAcc_ %= 1000;
    }

    if (speed >= kMovingThresholdKmh) {
        movingMsAcc_ += dtMs;
        telemetry_.movingTimeSec += movingMsAcc_ / 1000;
        movingMsAcc_ %= 1000;
    }

    // 3. Performance Sprint Timer State Machine
    if (!sprintActive_) {
        accelState_ = AccelTimerState::IDLE;
        stoppedSinceMs_ = 0;
        return;
    }

    switch (accelState_) {
        case AccelTimerState::IDLE:
        case AccelTimerState::FINISHED:
            // Arm if vehicle is completely stopped (< 0.5 km/h) for at least kArmDelayMs
            if (speed < 0.5f) {
                if (stoppedSinceMs_ == 0) stoppedSinceMs_ = nowMs;
                if ((nowMs - stoppedSinceMs_) >= kArmDelayMs) {
                    accelState_ = AccelTimerState::ARMED;
                    accelRunDistanceM_ = 0.0f;
#ifndef UNIT_TEST
                    Serial.println("[TripComputer] Auto-Armed! Ready to launch on roll.");
#endif
                    BuzzerController::instance().playArmed();
                }
            } else {
                stoppedSinceMs_ = 0;
            }
            break;

        case AccelTimerState::ARMED:
            if (speed >= 1.0f) {
                // Vehicle started rolling! Trigger timer
                accelState_ = AccelTimerState::RUNNING;
                accelStartMs_ = nowMs;
                accelRunDistanceM_ = 0.0f;
#ifndef UNIT_TEST
                Serial.printf("[TripComputer] Launch roll detected! Timer started (mode: %u)\n", static_cast<unsigned>(sprintMode_));
#endif
            }
            break;

        case AccelTimerState::RUNNING: {
            uint32_t runMs = (nowMs >= accelStartMs_) ? (nowMs - accelStartMs_) : 0;
            float dtSec = dtMs / 1000.0f;
            float speedMs = (speed * 1000.0f) / 3600.0f;
            accelRunDistanceM_ += speedMs * dtSec;

            bool isFinished = false;
            bool isNewRecord = false;
            float elapsedSec = runMs / 1000.0f;
            auto& rec = sprintRecords_[static_cast<uint8_t>(sprintMode_)];

            if (sprintMode_ == SprintMode::SPEED_0_100) {
                if (speed >= kTargetSpeedKmh) {
                    isFinished = true;
                    rec.lastTimeSec = elapsedSec;
                    rec.lastTrapSpeedKmh = speed;
                    if (std::isnan(rec.bestTimeSec) || rec.bestTimeSec <= 0.0f || elapsedSec < rec.bestTimeSec) {
                        isNewRecord = true;
                        rec.bestTimeSec = elapsedSec;
                        rec.bestTrapSpeedKmh = speed;
                        saveSprintRecordToNvs(sprintMode_);
                    }
                }
            } else if (sprintMode_ == SprintMode::DISTANCE_QUARTER_MILE) {
                if (accelRunDistanceM_ >= kQuarterMileMeters) {
                    isFinished = true;
                    rec.lastTimeSec = elapsedSec;
                    rec.lastTrapSpeedKmh = speed;
                    if (std::isnan(rec.bestTimeSec) || rec.bestTimeSec <= 0.0f || elapsedSec < rec.bestTimeSec) {
                        isNewRecord = true;
                        rec.bestTimeSec = elapsedSec;
                        rec.bestTrapSpeedKmh = speed;
                        saveSprintRecordToNvs(sprintMode_);
                    }
                }
            } else if (sprintMode_ == SprintMode::DISTANCE_ONE_MILE) {
                if (accelRunDistanceM_ >= kOneMileMeters) {
                    isFinished = true;
                    rec.lastTimeSec = elapsedSec;
                    rec.lastTrapSpeedKmh = speed;
                    if (std::isnan(rec.bestTimeSec) || rec.bestTimeSec <= 0.0f || elapsedSec < rec.bestTimeSec) {
                        isNewRecord = true;
                        rec.bestTimeSec = elapsedSec;
                        rec.bestTrapSpeedKmh = speed;
                        saveSprintRecordToNvs(sprintMode_);
                    }
                }
            }

            if (isFinished) {
                accelState_ = AccelTimerState::FINISHED;
                stoppedSinceMs_ = 0;
#ifndef UNIT_TEST
                Serial.printf("[TripComputer] RUN FINISHED! Mode: %u | Time: %.2f s | Trap: %.1f km/h | Best: %.2f s\n",
                              static_cast<unsigned>(sprintMode_), elapsedSec, speed, rec.bestTimeSec);
#endif
                if (isNewRecord) {
                    BuzzerController::instance().playNewRecord();
                } else {
                    BuzzerController::instance().playFinish();
                }
            } else if (runMs > kAccelTimeoutMs || (runMs > 3000 && speed < 0.5f)) {
                // Aborted or timed out
                accelState_ = AccelTimerState::IDLE;
                stoppedSinceMs_ = 0;
#ifndef UNIT_TEST
                Serial.println("[TripComputer] Run aborted or timed out.");
#endif
            }
            break;
        }
    }
}
