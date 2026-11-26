#include "model/dtc_database.h"
#include <cstdio>
#include <cctype>

namespace {

constexpr DtcSeverity I = DtcSeverity::INFO;
constexpr DtcSeverity W = DtcSeverity::WARNING;
constexpr DtcSeverity C = DtcSeverity::CRITICAL;

// Sorted by 16-bit code for binary search O(log N). Stored in Flash (RODATA).
const DtcDefinition kDtcEntries[] = {
    {0x0100, W, "Mass air flow circuit malfunction"},
    {0x0101, W, "Mass air flow circuit range/performance"},
    {0x0102, W, "Mass air flow circuit low input"},
    {0x0103, W, "Mass air flow circuit high input"},
    {0x0106, W, "Manifold pressure circuit range/performance"},
    {0x0107, W, "Manifold pressure circuit low input"},
    {0x0108, W, "Manifold pressure circuit high input"},
    {0x0110, W, "Intake air temperature circuit malfunction"},
    {0x0112, W, "Intake air temperature circuit low input"},
    {0x0113, W, "Intake air temperature circuit high input"},
    {0x0115, W, "Engine coolant temperature circuit malfunction"},
    {0x0116, W, "Engine coolant temperature circuit range/performance"},
    {0x0117, W, "Engine coolant temperature circuit low input"},
    {0x0118, W, "Engine coolant temperature circuit high input"},
    {0x0120, W, "Throttle position circuit malfunction"},
    {0x0121, W, "Throttle position circuit range/performance"},
    {0x0122, W, "Throttle position circuit low input"},
    {0x0123, W, "Throttle position circuit high input"},
    {0x0125, W, "Insufficient coolant temp for closed loop fuel control"},
    {0x0128, W, "Coolant thermostat malfunction"},
    {0x0130, W, "O2 sensor circuit malfunction (Bank 1 Sensor 1)"},
    {0x0131, W, "O2 sensor low voltage (Bank 1 Sensor 1)"},
    {0x0132, W, "O2 sensor high voltage (Bank 1 Sensor 1)"},
    {0x0133, W, "O2 sensor circuit slow response (Bank 1 Sensor 1)"},
    {0x0134, W, "O2 sensor circuit no activity detected (Bank 1 Sensor 1)"},
    {0x0135, W, "O2 sensor heater circuit malfunction (Bank 1 Sensor 1)"},
    {0x0136, W, "O2 sensor circuit malfunction (Bank 1 Sensor 2)"},
    {0x0137, W, "O2 sensor low voltage (Bank 1 Sensor 2)"},
    {0x0138, W, "O2 sensor high voltage (Bank 1 Sensor 2)"},
    {0x0140, W, "O2 sensor circuit no activity (Bank 1 Sensor 2)"},
    {0x0141, W, "O2 sensor heater circuit malfunction (Bank 1 Sensor 2)"},
    {0x0171, W, "System too lean (Bank 1)"},
    {0x0172, W, "System too rich (Bank 1)"},
    {0x0174, W, "System too lean (Bank 2)"},
    {0x0175, W, "System too rich (Bank 2)"},
    {0x0200, W, "Injector circuit malfunction"},
    {0x0201, C, "Injector circuit malfunction - Cylinder 1"},
    {0x0202, C, "Injector circuit malfunction - Cylinder 2"},
    {0x0203, C, "Injector circuit malfunction - Cylinder 3"},
    {0x0204, C, "Injector circuit malfunction - Cylinder 4"},
    {0x0300, C, "Random/Multiple cylinder misfire detected"},
    {0x0301, C, "Cylinder 1 misfire detected"},
    {0x0302, C, "Cylinder 2 misfire detected"},
    {0x0303, C, "Cylinder 3 misfire detected"},
    {0x0304, C, "Cylinder 4 misfire detected"},
    {0x0325, W, "Knock sensor circuit malfunction (Bank 1)"},
    {0x0335, C, "Crankshaft position sensor A circuit malfunction"},
    {0x0336, C, "Crankshaft position sensor A range/performance"},
    {0x0340, C, "Camshaft position sensor circuit malfunction (Bank 1)"},
    {0x0341, C, "Camshaft position sensor range/performance (Bank 1)"},
    {0x0400, W, "Exhaust gas recirculation (EGR) flow malfunction"},
    {0x0401, W, "EGR flow insufficient detected"},
    {0x0420, W, "Catalyst system efficiency below threshold (Bank 1)"},
    {0x0430, W, "Catalyst system efficiency below threshold (Bank 2)"},
    {0x0440, W, "Evaporative emission (EVAP) control system malfunction"},
    {0x0441, W, "EVAP control system incorrect purge flow"},
    {0x0442, W, "EVAP control system small leak detected"},
    {0x0455, W, "EVAP control system gross leak detected"},
    {0x0500, W, "Vehicle speed sensor (VSS) malfunction"},
    {0x0501, W, "Vehicle speed sensor range/performance"},
    {0x0505, W, "Idle air control (IAC) system malfunction"},
    {0x0506, W, "Idle air control RPM lower than expected"},
    {0x0507, W, "Idle air control RPM higher than expected"},
    {0x0560, W, "System voltage malfunction"},
    {0x0562, W, "System voltage low"},
    {0x0563, W, "System voltage high"},
    {0x0600, C, "Serial communication link malfunction"},
    {0x0606, C, "ECM / PCM processor fault"},
    {0x0700, W, "Transmission control system malfunction"},
    {0x0705, W, "Transmission range sensor circuit malfunction (PRNDL)"},
    {0x0715, W, "Input/Turbine speed sensor circuit malfunction"},
    {0x0720, W, "Output speed sensor circuit malfunction"},
    {0x0730, W, "Incorrect gear ratio"},
    {0x0740, W, "Torque converter clutch (TCC) circuit malfunction"},
    {0x0750, W, "Shift solenoid A malfunction"},
    {0x0755, W, "Shift solenoid B malfunction"},
    {0xC000, W, "CAN bus off / communication bus malfunction"},
    {0xC100, W, "Lost communication with ECM/PCM"},
    {0xC101, W, "Lost communication with TCM"},
    {0xC121, W, "Lost communication with ABS / ESP module"},
    {0xC140, W, "Lost communication with body control module (BCM)"},
    {0xC155, W, "Lost communication with instrument panel cluster"}
};

constexpr size_t kDtcCount = sizeof(kDtcEntries) / sizeof(kDtcEntries[0]);

const DtcDefinition* findEntry(uint16_t code) {
    int low = 0;
    int high = static_cast<int>(kDtcCount) - 1;
    while (low <= high) {
        int mid = (low + high) / 2;
        if (kDtcEntries[mid].code == code) return &kDtcEntries[mid];
        if (kDtcEntries[mid].code < code) low = mid + 1;
        else high = mid - 1;
    }
    return nullptr;
}

inline uint8_t firstDigit(uint16_t code) { return (code >> 12) & 0x03; }
inline uint8_t secondDigit(uint16_t code) { return (code >> 8) & 0x0F; }

} // namespace

void DtcDatabase::formatCode(uint16_t code, char* out, size_t len) {
    if (!out || len < 6) return;
    static const char prefixes[] = {'P', 'C', 'B', 'U'};
    char prefix = prefixes[(code >> 14) & 0x03];
    uint16_t num = code & 0x3FFF;
    std::snprintf(out, len, "%c%04X", prefix, num);
}

bool DtcDatabase::parseCode(const char* text, uint16_t& code) {
    if (!text || !text[0]) return false;
    uint16_t sys = 0;
    char p = static_cast<char>(std::toupper(text[0]));
    switch (p) {
        case 'P': sys = 0; break;
        case 'C': sys = 1; break;
        case 'B': sys = 2; break;
        case 'U': sys = 3; break;
        default: return false;
    }

    uint16_t val = 0;
    for (int i = 1; i < 5; ++i) {
        if (!text[i]) return false;
        char c = text[i];
        uint8_t nibble = 0;
        if (c >= '0' && c <= '9') nibble = c - '0';
        else if (c >= 'A' && c <= 'F') nibble = 10 + (c - 'A');
        else if (c >= 'a' && c <= 'f') nibble = 10 + (c - 'a');
        else return false;

        val = (val << 4) | nibble;
    }
    code = (sys << 14) | (val & 0x3FFF);
    return true;
}

DtcSystem DtcDatabase::getSystem(uint16_t code) {
    return static_cast<DtcSystem>((code >> 14) & 0x03);
}

const char* DtcDatabase::getSystemName(uint16_t code) {
    switch (getSystem(code)) {
        case DtcSystem::CHASSIS: return "Chassis";
        case DtcSystem::BODY: return "Body";
        case DtcSystem::NETWORK: return "Network";
        case DtcSystem::POWERTRAIN:
        default: return "Powertrain";
    }
}

bool DtcDatabase::isGeneric(uint16_t code) {
    uint8_t d1 = firstDigit(code);
    if (getSystem(code) == DtcSystem::POWERTRAIN) {
        if (d1 == 0 || d1 == 2) return true;
        if (d1 == 3) return secondDigit(code) >= 4;
        return false;
    }
    return (d1 == 0 || d1 == 3);
}

DtcSeverity DtcDatabase::getSeverity(uint16_t code) {
    const DtcDefinition* def = findEntry(code);
    if (def) return def->severity;
    return DtcSeverity::WARNING;
}

const char* DtcDatabase::describe(uint16_t code) {
    const DtcDefinition* def = findEntry(code);
    if (def) return def->description;

    // SAE J2012 Subsystem Range Fallback
    switch (getSystem(code)) {
        case DtcSystem::CHASSIS: return "Chassis system fault";
        case DtcSystem::BODY: return "Body electrical fault";
        case DtcSystem::NETWORK: return "CAN communication fault";
        case DtcSystem::POWERTRAIN: break;
    }

    if (!isGeneric(code)) return "Manufacturer specific powertrain fault";
    if (firstDigit(code) != 0) return "Emission or fuel control fault";

    switch (secondDigit(code)) {
        case 0x0:
        case 0x1:
        case 0x2: return "Fuel and air metering circuit fault";
        case 0x3: return "Ignition system or cylinder misfire";
        case 0x4: return "Auxiliary emission controls";
        case 0x5: return "Vehicle speed or idle control";
        case 0x6: return "ECU / PCM internal or output circuit fault";
        case 0x7:
        case 0x8:
        case 0x9: return "Transmission / Transaxle system fault";
        case 0xA: return "Hybrid propulsion system fault";
        default: return "General powertrain fault";
    }
}

size_t DtcDatabase::getDatabaseCount() {
    return kDtcCount;
}
