#pragma once

#include <stdint.h>
#include <stddef.h>

enum class PidCategory : uint8_t {
    ENGINE = 0,
    TEMPERATURE,
    AIR_PRESSURE,
    FUEL_SYSTEM,
    IGNITION,
    OXYGEN,
    ELECTRICAL,
    DIAGNOSTICS
};

enum class SuperPidGroup : uint8_t {
    ENGINE_THERMAL = 0, // Engine core & temperatures (8 PIDs)
    FUEL_AIR_TURBO,     // Air pressure, fuel system, oxygen (8 PIDs)
    ELECTRIC_DIAG,      // Electrical, pedal, ignition & diagnostics (8 PIDs)
    COUNT
};

enum class FormulaType : uint8_t {
    RAW_A = 0,               // A (e.g. Speed km/h, Baro kPa)
    TEMP_A_MINUS_40,         // A - 40 (°C) (Coolant, IAT, Ambient, Oil)
    PERCENT_A_255,           // (A * 100) / 255 (%) (Load, Throttle, Fuel Tank)
    PERCENT_AB_255,          // ((A * 256) + B) * 100 / 255 (%) (Absolute Load PID 0x43)
    PERCENT_A_TRIM,          // (A / 1.28) - 100 (%) (Short/Long Fuel Trim)
    RPM_AB_DIV_4,            // ((A * 256) + B) / 4 (RPM)
    AB_DIV_100,              // ((A * 256) + B) / 100 (MAF g/s)
    CATALYST_TEMP,           // ((A * 256) + B) / 10 - 40 (°C)
    FUEL_PRESSURE_A_MUL_3,   // A * 3 (kPa)
    FUEL_RAIL_VAC,           // ((A * 256) + B) * 0.079f (kPa)
    FUEL_RAIL_DIESEL,        // ((A * 256) + B) * 10.0f (kPa)
    FUEL_RATE,               // ((A * 256) + B) * 0.05f (L/h)
    TIMING_ADVANCE,          // (A / 2.0f) - 64.0f (deg)
    O2_VOLTAGE,              // A / 200.0f (V)
    LAMBDA_AB,               // ((A * 256) + B) / 32768.0f (Ratio)
    VOLTAGE_AB_DIV_1000,     // ((A * 256) + B) / 1000.0f (ECU V)
    RAW_AB,                  // (A * 256) + B (Seconds, Minutes, KM)
    BITFIELD,                // Bitmask status
    SPECIAL_ATRV             // Parsed directly from ATRV float
};

struct UniversalPidDef {
    uint8_t pidHex;
    const char* name;
    const char* unit;
    float minVal;
    float maxVal;
    uint16_t intervalMs;
    PidCategory category;
    FormulaType formula;
};

class UniversalPidCatalog {
public:
    static constexpr size_t kTotalPids = 24;

    static const UniversalPidDef& get(size_t index);
    static int findIndexByPid(uint8_t pidHex);
    static const char* getCategoryName(PidCategory cat);
    static size_t getSuperGroupCount(SuperPidGroup group);
    static uint8_t getSuperGroupPidIndex(SuperPidGroup group, size_t index);
    static const char* getSuperGroupName(SuperPidGroup group);
    static float decodeValue(const UniversalPidDef& def, const uint8_t* bytes, size_t len);
    static bool isCatalogIndexSupported(uint8_t catIdx, uint64_t unsupportedBits);
    static uint8_t getAlertLevel(uint8_t pidHex, float val);
    static const char* getDashboardName(uint8_t pidHex);
};
