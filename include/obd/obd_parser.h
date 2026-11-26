#pragma once

#include <cstdint>
#include <cstddef>

enum class ParseStatus : uint8_t {
    OK = 0,
    NO_DATA,
    ERROR_RESPONSE,
    INCOMPLETE,
    INVALID_FORMAT
};

class ObdParser {
public:
    static ParseStatus parseBatteryVoltage(const char* raw, float& outVoltage);
    static ParseStatus parseDtcCodes(const char* raw, uint16_t* outCodes, size_t maxCodes, size_t& outCount, bool hasCountPrefix = true);
    static ParseStatus parseDtcResponse(const char* raw, bool isPending, struct DtcCodeInfo* outList, size_t maxCodes, size_t& outCount, bool hasCountPrefix = true);
    static ParseStatus parseMode01PidResponse(const char* raw, uint8_t expectedPid, uint8_t* outData, size_t maxData, size_t& outLen, bool& outIsNegative);
    static ParseStatus parseProtocolNumber(const char* raw, uint8_t& outProto);

    // Helpers
    static bool isErrorResponse(const char* raw);
    static bool isNoDataResponse(const char* raw);
    static uint8_t hexToByte(char high, char low);
    static bool isHexChar(char c);
    static size_t extractHexBytes(const char* raw, uint8_t* outBytes, size_t maxBytes);
};
