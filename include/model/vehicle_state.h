#pragma once
#include <stdint.h>
#include <atomic>

struct VehicleState {
    uint32_t timestampMs = 0;
    uint16_t rpm = 0;            // 0 - 8000
    uint8_t speedKmh = 0;        // 0 - 255
    int8_t coolantTempC = 0;     // -40 - 215
    int8_t intakeAirTempC = 0;   // -40 - 215
    float batteryVoltage = 0.0f; // 0.0 - 18.0
    uint8_t throttlePercent = 0; // 0 - 100
    uint8_t engineLoad = 0;      // 0 - 100
    float dashSlotValues[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
    float liveSlotValues[6]{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    uint64_t unsupportedCatalogBits = 0; // Bit i = 1 => catalog slot i marked N/A for current session
    bool isEngineRunning = false;
    bool isConnected = false;
    char gear = ' ';             // 'P', 'R', 'N', 'D', ' ' (unknown/unsupported)
    bool handbrakeActive = false;
    bool isDemoMode = false;
    bool milActive = false;
    uint8_t dtcCount = 0;
};

class VehicleStateManager {
public:
    VehicleStateManager();

    // Core 0 calls this to write new state (Lock-free atomic swap)
    void update(const VehicleState& newState);

    // Core 1 calls this to read latest snapshot (0ms wait, non-blocking)
    VehicleState getSnapshot() const;

    static VehicleStateManager& instance();

private:
    VehicleState state_{};
    std::atomic<uint32_t> seq_{0};
};
