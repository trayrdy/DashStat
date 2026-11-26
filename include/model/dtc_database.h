#pragma once
#include <stdint.h>
#include <stddef.h>

enum class DtcSeverity : uint8_t {
    INFO = 0,
    WARNING,
    CRITICAL
};

enum class DtcSystem : uint8_t {
    POWERTRAIN = 0, // P0xxx - P3xxx
    CHASSIS,        // C0xxx - C3xxx
    BODY,           // B0xxx - B3xxx
    NETWORK         // U0xxx - U3xxx
};

struct DtcDefinition {
    uint16_t code;
    DtcSeverity severity;
    const char* description;
};

struct DtcCodeInfo {
    char code[8]{0};
    const char* desc{nullptr};
    DtcSeverity severity{DtcSeverity::INFO};
    bool isPending{false};
};

class DtcDatabase {
public:
    static void formatCode(uint16_t code, char* out, size_t len);
    static bool parseCode(const char* text, uint16_t& code);

    static DtcSystem getSystem(uint16_t code);
    static const char* getSystemName(uint16_t code);

    static bool isGeneric(uint16_t code);
    static DtcSeverity getSeverity(uint16_t code);
    static const char* describe(uint16_t code);

    static size_t getDatabaseCount();
};
