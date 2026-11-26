#pragma once

#include <cstdint>
#include <cstddef>
#include "obd/command_guard.h"

struct MockVehicleTelemetry {
    uint16_t rpm = 750;
    uint8_t  speed = 0;
    int16_t  coolantTemp = 45; // °C (warmed up dynamically)
    uint8_t  throttle = 0;     // %
    float    voltage = 14.1f;  // V
    int16_t  intakeTemp = 32;  // °C
    uint8_t  engineLoad = 18;  // %
    char     gear = 'P';       // Used by demo driving lock & standby checks
    bool     milActive = false;
    uint8_t  dtcCount = 0;
    bool     handbrake = true;
};

class MockOBD {
public:
    static MockOBD& instance();
    MockOBD() = default;

    bool begin() { return true; }
    bool query(const char* cmd, char* outBuffer, size_t maxLen);
    bool isConnected() const { return true; }
    const char* getName() const { return "MockOBD (OBD-II Sim)"; }

    // Simulation stepper
    void update(uint32_t deltaMs);
    void reset() { _simTimeMs = 0; }
    void setParkedMode(bool parked) { _parkedMode = parked; }
    bool isParkedMode() const { return _parkedMode; }

    const MockVehicleTelemetry& getTelemetry() const { return _telemetry; }

private:
    MockVehicleTelemetry _telemetry;
    uint32_t _simTimeMs = 0;
    bool _parkedMode = false;
};
