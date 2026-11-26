#include "obd/universal_pid_catalog.h"

static const UniversalPidDef s_catalog[UniversalPidCatalog::kTotalPids] = {
    // === Group 0: ENGINE & THERMAL (8 PIDs, indices 0..7) ===
    { 0x0C, "ENGINE RPM", "RPM", 0.0f, 8000.0f, 100, PidCategory::ENGINE, FormulaType::RPM_AB_DIV_4 },
    { 0x0D, "VEHICLE SPEED", "KM/H", 0.0f, 255.0f, 100, PidCategory::ENGINE, FormulaType::RAW_A },
    { 0x04, "CALC ENGINE LOAD", "%", 0.0f, 100.0f, 200, PidCategory::ENGINE, FormulaType::PERCENT_A_255 },
    { 0x43, "ABSOLUTE LOAD", "%", 0.0f, 400.0f, 200, PidCategory::ENGINE, FormulaType::PERCENT_AB_255 },
    { 0x11, "THROTTLE POS", "%", 0.0f, 100.0f, 100, PidCategory::ENGINE, FormulaType::PERCENT_A_255 },
    { 0x45, "REL THROTTLE POS", "%", 0.0f, 100.0f, 200, PidCategory::ENGINE, FormulaType::PERCENT_A_255 },
    { 0x05, "COOLANT TEMP", "°C", -40.0f, 150.0f, 5000, PidCategory::TEMPERATURE, FormulaType::TEMP_A_MINUS_40 },
    { 0x0F, "INTAKE AIR TEMP", "°C", -40.0f, 120.0f, 5000, PidCategory::TEMPERATURE, FormulaType::TEMP_A_MINUS_40 },

    // === Group 1: AIR, FUEL & O2 (8 PIDs, indices 8..15) ===
    { 0x0B, "MANIFOLD PRESSURE", "KPA", 0.0f, 255.0f, 100, PidCategory::AIR_PRESSURE, FormulaType::RAW_A },
    { 0x10, "MASS AIR FLOW", "G/S", 0.0f, 655.0f, 100, PidCategory::AIR_PRESSURE, FormulaType::AB_DIV_100 },
    { 0x33, "BAROMETRIC PRESS", "KPA", 0.0f, 255.0f, 15000, PidCategory::AIR_PRESSURE, FormulaType::RAW_A },
    { 0xF0, "TURBO BOOST", "PSI", -15.0f, 45.0f, 100, PidCategory::AIR_PRESSURE, FormulaType::RAW_A }, // Virtual MAP - BARO
    { 0x03, "FUEL SYS STATUS", "", 0.0f, 16.0f, 5000, PidCategory::FUEL_SYSTEM, FormulaType::BITFIELD },
    { 0x06, "SHORT FUEL TRIM 1", "%", -100.0f, 99.2f, 1000, PidCategory::FUEL_SYSTEM, FormulaType::PERCENT_A_TRIM },
    { 0x07, "LONG FUEL TRIM 1", "%", -100.0f, 99.2f, 5000, PidCategory::FUEL_SYSTEM, FormulaType::PERCENT_A_TRIM },
    { 0x14, "O2 SENSOR B1S1", "V", 0.0f, 1.275f, 200, PidCategory::OXYGEN, FormulaType::O2_VOLTAGE },

    // === Group 2: VOLT, PEDAL & DIAG (8 PIDs, indices 16..23) ===
    { 0xFF, "BATTERY VOLTAGE", "V", 0.0f, 18.0f, 5000, PidCategory::ELECTRICAL, FormulaType::SPECIAL_ATRV },
    { 0x42, "CONTROL MODULE V", "V", 0.0f, 18.0f, 5000, PidCategory::ELECTRICAL, FormulaType::VOLTAGE_AB_DIV_1000 },
    { 0x0E, "TIMING ADVANCE", "DEG", -64.0f, 63.5f, 100, PidCategory::IGNITION, FormulaType::TIMING_ADVANCE },
    { 0x49, "ACCEL PEDAL D", "%", 0.0f, 100.0f, 100, PidCategory::ENGINE, FormulaType::PERCENT_A_255 },
    { 0x4A, "ACCEL PEDAL E", "%", 0.0f, 100.0f, 100, PidCategory::ENGINE, FormulaType::PERCENT_A_255 },
    { 0x4C, "CMD THROTTLE ACT", "%", 0.0f, 100.0f, 200, PidCategory::AIR_PRESSURE, FormulaType::PERCENT_A_255 },
    { 0x15, "O2 SENSOR B1S2", "V", 0.0f, 1.275f, 1000, PidCategory::OXYGEN, FormulaType::O2_VOLTAGE },
    { 0x1F, "RUN TIME", "SEC", 0.0f, 65535.0f, 5000, PidCategory::DIAGNOSTICS, FormulaType::RAW_AB }
};

const UniversalPidDef& UniversalPidCatalog::get(size_t index) {
    if (index >= kTotalPids) {
        return s_catalog[0];
    }
    return s_catalog[index];
}

int UniversalPidCatalog::findIndexByPid(uint8_t pidHex) {
    if (pidHex == 0x00) return -1;
    for (size_t i = 0; i < kTotalPids; ++i) {
        if (s_catalog[i].pidHex == pidHex) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const char* UniversalPidCatalog::getCategoryName(PidCategory cat) {
    switch (cat) {
        case PidCategory::ENGINE: return "ENGINE";
        case PidCategory::TEMPERATURE: return "TEMPERATURE";
        case PidCategory::AIR_PRESSURE: return "AIR & PRESSURE";
        case PidCategory::FUEL_SYSTEM: return "FUEL SYSTEM";
        case PidCategory::IGNITION: return "IGNITION";
        case PidCategory::OXYGEN: return "OXYGEN / LAMBDA";
        case PidCategory::ELECTRICAL: return "ELECTRICAL";
        case PidCategory::DIAGNOSTICS: return "DIAGNOSTICS";
        default: return "OTHER";
    }
}

float UniversalPidCatalog::decodeValue(const UniversalPidDef& def, const uint8_t* b, size_t len) {
    if (!b || len == 0) return 0.0f;
    uint8_t a = b[0];
    uint8_t bVal = (len > 1) ? b[1] : 0;

    switch (def.formula) {
        case FormulaType::RAW_A:
            return static_cast<float>(a);
        case FormulaType::TEMP_A_MINUS_40:
            return static_cast<float>(static_cast<int16_t>(a) - 40);
        case FormulaType::PERCENT_A_255: {
            float val = (static_cast<float>(a) * 100.0f) / 255.0f;
            if (val > 100.0f) val = 100.0f;
            if (val < 0.0f) val = 0.0f;
            return val;
        }
        case FormulaType::PERCENT_AB_255: {
            float val = (static_cast<float>((static_cast<uint16_t>(a) << 8) | bVal) * 100.0f) / 255.0f;
            if (val < 0.0f) val = 0.0f;
            return val;
        }
        case FormulaType::PERCENT_A_TRIM: {
            float val = (static_cast<float>(a) / 1.28f) - 100.0f;
            if (val > def.maxVal) val = def.maxVal;
            if (val < def.minVal) val = def.minVal;
            return val;
        }
        case FormulaType::RPM_AB_DIV_4:
            return static_cast<float>((static_cast<uint16_t>(a) << 8) | bVal) / 4.0f;
        case FormulaType::AB_DIV_100:
            return static_cast<float>((static_cast<uint16_t>(a) << 8) | bVal) / 100.0f;
        case FormulaType::CATALYST_TEMP:
            return (static_cast<float>((static_cast<uint16_t>(a) << 8) | bVal) / 10.0f) - 40.0f;
        case FormulaType::FUEL_PRESSURE_A_MUL_3:
            return static_cast<float>(a * 3);
        case FormulaType::FUEL_RAIL_VAC:
            return static_cast<float>((static_cast<uint16_t>(a) << 8) | bVal) * 0.079f;
        case FormulaType::FUEL_RAIL_DIESEL:
            return static_cast<float>((static_cast<uint16_t>(a) << 8) | bVal) * 10.0f;
        case FormulaType::FUEL_RATE:
            return static_cast<float>((static_cast<uint16_t>(a) << 8) | bVal) * 0.05f;
        case FormulaType::TIMING_ADVANCE:
            return (static_cast<float>(a) / 2.0f) - 64.0f;
        case FormulaType::O2_VOLTAGE:
            return static_cast<float>(a) / 200.0f;
        case FormulaType::LAMBDA_AB:
            return static_cast<float>((static_cast<uint16_t>(a) << 8) | bVal) / 32768.0f;
        case FormulaType::VOLTAGE_AB_DIV_1000:
            return static_cast<float>((static_cast<uint16_t>(a) << 8) | bVal) / 1000.0f;
        case FormulaType::RAW_AB:
            return static_cast<float>((static_cast<uint16_t>(a) << 8) | bVal);
        case FormulaType::BITFIELD:
            return static_cast<float>(a);
        case FormulaType::SPECIAL_ATRV:
        default:
            return 0.0f;
    }
}

bool UniversalPidCatalog::isCatalogIndexSupported(uint8_t catIdx, uint64_t unsupportedBits) {
    if (catIdx >= kTotalPids) return false;
    if (s_catalog[catIdx].formula == FormulaType::SPECIAL_ATRV) return true;
    return (unsupportedBits & (1ULL << catIdx)) == 0;
}

static const uint8_t s_superEngineThermal[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
static const uint8_t s_superFuelAirTurbo[8]  = { 8, 9, 10, 11, 12, 13, 14, 15 };
static const uint8_t s_superElectricDiag[8]  = { 16, 17, 18, 19, 20, 21, 22, 23 };

size_t UniversalPidCatalog::getSuperGroupCount(SuperPidGroup group) {
    switch (group) {
        case SuperPidGroup::ENGINE_THERMAL: return sizeof(s_superEngineThermal);
        case SuperPidGroup::FUEL_AIR_TURBO: return sizeof(s_superFuelAirTurbo);
        case SuperPidGroup::ELECTRIC_DIAG:  return sizeof(s_superElectricDiag);
        default: return 0;
    }
}

uint8_t UniversalPidCatalog::getSuperGroupPidIndex(SuperPidGroup group, size_t index) {
    switch (group) {
        case SuperPidGroup::ENGINE_THERMAL:
            return (index < sizeof(s_superEngineThermal)) ? s_superEngineThermal[index] : 0;
        case SuperPidGroup::FUEL_AIR_TURBO:
            return (index < sizeof(s_superFuelAirTurbo)) ? s_superFuelAirTurbo[index] : 0;
        case SuperPidGroup::ELECTRIC_DIAG:
            return (index < sizeof(s_superElectricDiag)) ? s_superElectricDiag[index] : 0;
        default:
            return 0;
    }
}

const char* UniversalPidCatalog::getSuperGroupName(SuperPidGroup group) {
    switch (group) {
        case SuperPidGroup::ENGINE_THERMAL: return "ENGINE & THERMAL";
        case SuperPidGroup::FUEL_AIR_TURBO: return "AIR, FUEL & O2";
        case SuperPidGroup::ELECTRIC_DIAG:  return "VOLT, PEDAL & DIAG";
        default: return "OTHER";
    }
}

uint8_t UniversalPidCatalog::getAlertLevel(uint8_t pidHex, float val) {
    switch (pidHex) {
        case 0x05: // Coolant Temperature (°C)
            if (val >= 108.0f) return 2;
            if (val >= 100.0f) return 1;
            break;
        case 0x0F: // Intake Air Temperature (°C)
            if (val >= 65.0f) return 2;
            if (val >= 55.0f) return 1;
            break;
        case 0x06: case 0x07: // Fuel Trims (%)
            if (val <= -20.0f || val >= 20.0f) return 2;
            if (val <= -12.0f || val >= 12.0f) return 1;
            break;
        case 0xFF: case 0x42: // Battery / Module Voltage (V)
            if (val > 0.5f) {
                if (val <= 11.5f || val >= 15.2f) return 2;
                if (val <= 12.0f || val >= 14.8f) return 1;
            }
            break;
        case 0xF0: // Derived Turbo Boost PSI
            if (val >= 22.0f) return 2;
            if (val >= 18.0f) return 1;
            break;
        case 0x0B: // MAP kPa
            if (val >= 240.0f) return 2;
            if (val >= 210.0f) return 1;
            break;
        default:
            break;
    }
    return 0;
}

const char* UniversalPidCatalog::getDashboardName(uint8_t pidHex) {
    switch (pidHex) {
        case 0x05: return "COOLANT";
        case 0xFF:
        case 0x42: return "BATTERY";
        case 0x0F: return "INTAKE AIR";
        case 0x04: return "ENG LOAD";
        case 0x43: return "ABS LOAD";
        case 0x11: return "THROTTLE";
        case 0x45: return "REL THROT";
        case 0x49: return "PEDAL D";
        case 0x4A: return "PEDAL E";
        case 0x4C: return "CMD THROT";
        case 0xF0: return "BOOST";
        case 0x0B: return "MAP";
        case 0x10: return "MAF";
        case 0x33: return "BARO";
        case 0x0E: return "TIMING";
        case 0x0C: return "RPM";
        case 0x0D: return "SPEED";
        case 0x1F: return "RUN TIME";
        case 0x03: return "FUEL SYS";
        case 0x06: return "STFT 1";
        case 0x07: return "LTFT 1";
        case 0x14: return "O2 B1S1";
        case 0x15: return "O2 B1S2";
        default: {
            int idx = findIndexByPid(pidHex);
            if (idx >= 0 && idx < static_cast<int>(kTotalPids)) {
                return s_catalog[idx].name;
            }
            return "---";
        }
    }
}
