#pragma once
#include <stdint.h>

enum class AccelTimerState : uint8_t {
    IDLE = 0,
    ARMED,
    RUNNING,
    FINISHED
};

enum class SprintMode : uint8_t {
    SPEED_0_100 = 0,
    DISTANCE_QUARTER_MILE, // 402.3 m (1/4 Mile)
    DISTANCE_ONE_MILE,     // 1609.3 m (1 Mile)
    COUNT
};

struct SprintRecord {
    float lastTimeSec = 0.0f;
    float bestTimeSec = 0.0f;
    float lastTrapSpeedKmh = 0.0f;
    float bestTrapSpeedKmh = 0.0f;
};

struct TripTelemetry {
    float distanceKm = 0.0f;
    float maxSpeedKmh = 0.0f;
    uint32_t driveTimeSec = 0;
    uint32_t movingTimeSec = 0;
};

class TripComputer {
public:
    static TripComputer& instance();

    void begin();
    void update(uint8_t currentSpeedKmh, bool isEngineRunning, uint32_t nowMs);
    void resetTrip();
    void resetSprintBest(SprintMode mode);

    const TripTelemetry& getTelemetry() const { return telemetry_; }
    AccelTimerState getAccelState() const { return accelState_; }
    float getCurrentRunSeconds(uint32_t nowMs) const;
    float getCurrentRunDistanceM() const { return accelRunDistanceM_; }
    float getAverageSpeedKmh() const;

    void setSprintMode(SprintMode mode);
    bool isSprintActive() const { return sprintActive_; }
    void setSprintActive(bool active);
    SprintMode getSprintMode() const { return sprintMode_; }
    const SprintRecord& getSprintRecord(SprintMode mode) const {
        return sprintRecords_[static_cast<uint8_t>(mode)];
    }

private:
    TripComputer();

    void loadFromNvs();
    void saveSprintRecordToNvs(SprintMode mode);

    TripTelemetry telemetry_;
    AccelTimerState accelState_ = AccelTimerState::IDLE;
    SprintMode sprintMode_ = SprintMode::SPEED_0_100;
    bool sprintActive_ = false;

    SprintRecord sprintRecords_[static_cast<uint8_t>(SprintMode::COUNT)];

    uint32_t lastUpdateMs_ = 0;
    bool hasLastUpdate_ = false;

    uint32_t driveMsAcc_ = 0;
    uint32_t movingMsAcc_ = 0;

    uint32_t stoppedSinceMs_ = 0;
    uint32_t accelStartMs_ = 0;
    float accelRunDistanceM_ = 0.0f;

    static constexpr float kMovingThresholdKmh = 2.0f;
    static constexpr uint32_t kArmDelayMs = 500;
    static constexpr uint32_t kAccelTimeoutMs = 60000; // 60s timeout
    static constexpr float kTargetSpeedKmh = 100.0f;
    static constexpr float kQuarterMileMeters = 402.336f;
    static constexpr float kOneMileMeters = 1609.344f;
};
