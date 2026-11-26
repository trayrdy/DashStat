#include "obd/obd_parser.h"
#include "model/dtc_database.h"
#include <cctype>
#include <cstring>
#include <cstdlib>

bool ObdParser::isHexChar(char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'A' && c <= 'F') ||
           (c >= 'a' && c <= 'f');
}

uint8_t ObdParser::hexToByte(char high, char low) {
    auto hexVal = [](char c) -> uint8_t {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return 0;
    };
    return (hexVal(high) << 4) | hexVal(low);
}

bool ObdParser::isErrorResponse(const char* raw) {
    if (!raw) return true;
    if (std::strstr(raw, "CAN ERROR") != nullptr) return true;
    if (std::strstr(raw, "UNABLE TO CONNECT") != nullptr) return true;
    if (std::strstr(raw, "ERROR") != nullptr) return true;
    if (std::strstr(raw, "?") != nullptr) return true;
    if (std::strstr(raw, "STOPPED") != nullptr) return true;
    return false;
}

bool ObdParser::isNoDataResponse(const char* raw) {
    if (!raw) return false;
    return (std::strstr(raw, "NO DATA") != nullptr);
}

static bool lineContainsToken(const char* s, size_t len, const char* token) {
    size_t tokLen = std::strlen(token);
    if (tokLen == 0 || len < tokLen) return false;
    for (size_t i = 0; i + tokLen <= len; ++i) {
        if (std::strncmp(s + i, token, tokLen) == 0) {
            return true;
        }
    }
    return false;
}

static bool isEchoLine(const char* s, size_t len, bool isFirstLine) {
    if (len == 0) return false;
    // Check for AT command echo: "AT..."
    if (len >= 2 && (s[0] == 'A' || s[0] == 'a') && (s[1] == 'T' || s[1] == 't')) {
        return true;
    }
    if (!isFirstLine) {
        return false; // ISO-TP consecutive frames (21/22...) on subsequent lines are never echoes
    }
    // Check for request command echo on the first line only:
    // 01XX (4), 01XX1 (5), 010C0D1 (7), 21XX (4), 21XX1 (5), 22XXXX (6), 22XXXX1 (7)
    bool allHex = true;
    for (size_t i = 0; i < len; ++i) {
        if (!ObdParser::isHexChar(s[i])) {
            allHex = false;
            break;
        }
    }
    if (!allHex) return false;

    if (s[0] == '0' && s[1] == '1') {
        if (len == 4 || ((len == 5 || len == 7) && s[len - 1] == '1')) return true;
    } else if (s[0] == '0' && (s[1] == '3' || s[1] == '7')) {
        if (len == 2 || (len == 3 && s[2] == '1')) return true;
    } else if (s[0] == '2' && s[1] == '1') {
        if (len == 4 || (len == 5 && s[len - 1] == '1')) return true;
    } else if (s[0] == '2' && s[1] == '2') {
        if (len == 6 || (len == 7 && s[len - 1] == '1')) return true;
    }
    return false;
}

static void stripIsoTpPci(const char*& s, size_t& len) {
    while (len > 0 && (*s == ' ' || *s == '\t')) { s++; len--; }
    // Single Frame: "0N" or "0N " (N = 1..7)
    if (len >= 2 && s[0] == '0' && (s[1] >= '1' && s[1] <= '7')) {
        if (len == 2 || s[2] == ' ' || s[2] == '\t' || ObdParser::isHexChar(s[2])) {
            s += 2;
            len -= 2;
            while (len > 0 && (*s == ' ' || *s == '\t')) { s++; len--; }
            return;
        }
    }
    // First Frame: "1X YY" or "1XYY"
    if (len >= 4 && s[0] == '1' && ObdParser::isHexChar(s[1])) {
        if (len >= 5 && (s[2] == ' ' || s[2] == '\t') && ObdParser::isHexChar(s[3]) && ObdParser::isHexChar(s[4])) {
            s += 5;
            len -= 5;
            while (len > 0 && (*s == ' ' || *s == '\t')) { s++; len--; }
            return;
        } else if (ObdParser::isHexChar(s[2]) && ObdParser::isHexChar(s[3])) {
            s += 4;
            len -= 4;
            while (len > 0 && (*s == ' ' || *s == '\t')) { s++; len--; }
            return;
        }
    }
    // Consecutive Frame: "2X" or "2X "
    if (len >= 2 && s[0] == '2' && ObdParser::isHexChar(s[1])) {
        if (len == 2 || s[2] == ' ' || s[2] == '\t' || ObdParser::isHexChar(s[2])) {
            s += 2;
            len -= 2;
            while (len > 0 && (*s == ' ' || *s == '\t')) { s++; len--; }
            return;
        }
    }
}

static size_t extractLineHexBytes(const char* s, size_t len, bool isFirstLine, uint8_t* outBytes, size_t maxBytes) {
    if (!s || len == 0 || !outBytes || maxBytes == 0 || *s == '>') return 0;

    bool isIsoTpLenLine = (len == 3 && ObdParser::isHexChar(s[0]) && ObdParser::isHexChar(s[1]) && ObdParser::isHexChar(s[2]));
    bool isStatusLine = lineContainsToken(s, len, "BUS INIT") || lineContainsToken(s, len, "SEARCHING");
    if (isIsoTpLenLine || isStatusLine || isEchoLine(s, len, isFirstLine)) {
        return 0;
    }

    // 1. Strip CAN frame index like "0:" or "1:"
    if (len >= 2 && ObdParser::isHexChar(s[0]) && s[1] == ':') {
        s += 2;
        len -= 2;
        while (len > 0 && (*s == ' ' || *s == '\t')) { s++; len--; }
    }
    // 2. Strip 3-character 11-bit CAN ID (e.g. "7E8", "7E9", "7DF", "7E0".."7EF")
    else if (len >= 4 && s[0] == '7' && (s[1] == 'E' || s[1] == 'e' || ((s[1] == 'D' || s[1] == 'd') && (s[2] == 'F' || s[2] == 'f'))) && ObdParser::isHexChar(s[2])) {
        if (s[3] == ' ' || s[3] == '\t' || (len >= 5 && ObdParser::isHexChar(s[3]))) {
            s += 3;
            len -= 3;
            stripIsoTpPci(s, len);
        }
    }
    // 3. Strip 8-character 29-bit CAN ID (e.g. "18DAF110 " or ATS0 compact "18DAF110...")
    else if ((len >= 10 && (s[8] == ' ' || s[8] == '\t')) ||
             (len >= 12 && s[0] == '1' && s[1] == '8' &&
              (s[2] == 'D' || s[2] == 'd') &&
              (s[3] == 'A' || s[3] == 'a' || s[3] == 'B' || s[3] == 'b'))) {
        bool isCan29 = true;
        for (size_t i = 0; i < 8; ++i) {
            if (!ObdParser::isHexChar(s[i])) { isCan29 = false; break; }
        }
        if (isCan29) {
            s += 8;
            len -= 8;
            stripIsoTpPci(s, len);
        }
    }

    size_t count = 0;
    while (len > 0 && count < maxBytes) {
        while (len > 0 && (*s == ' ' || *s == '\t')) { s++; len--; }
        if (len == 0 || *s == '>') break;
        if (ObdParser::isHexChar(*s)) {
            char high = *s++; len--;
            while (len > 0 && (*s == ' ' || *s == '\t')) { s++; len--; }
            if (len > 0 && ObdParser::isHexChar(*s)) {
                char low = *s++; len--;
                outBytes[count++] = ObdParser::hexToByte(high, low);
            } else {
                break;
            }
        } else {
            s++; len--;
        }
    }
    return count;
}

size_t ObdParser::extractHexBytes(const char* raw, uint8_t* outBytes, size_t maxBytes) {
    if (!raw || !outBytes || maxBytes == 0) return 0;

    size_t count = 0;
    const char* lineStart = raw;
    bool isFirstLine = true;

    while (*lineStart != '\0' && count < maxBytes) {
        const char* lineEnd = lineStart;
        while (*lineEnd != '\0' && *lineEnd != '\r' && *lineEnd != '\n') {
            lineEnd++;
        }

        const char* s = lineStart;
        while (s < lineEnd && (*s == ' ' || *s == '\t')) s++;
        const char* e = lineEnd;
        while (e > s && (*(e - 1) == ' ' || *(e - 1) == '\t')) e--;
        size_t len = static_cast<size_t>(e - s);

        if (len > 0 && *s != '>') {
            bool wasFirstLine = isFirstLine;
            isFirstLine = false;
            count += extractLineHexBytes(s, len, wasFirstLine, outBytes + count, maxBytes - count);
        }

        lineStart = lineEnd;
        while (*lineStart == '\r' || *lineStart == '\n') lineStart++;
    }

    return count;
}

ParseStatus ObdParser::parseBatteryVoltage(const char* raw, float& outVoltage) {
    if (!raw) return ParseStatus::INVALID_FORMAT;
    if (isNoDataResponse(raw)) return ParseStatus::NO_DATA;
    if (isErrorResponse(raw)) return ParseStatus::ERROR_RESPONSE;

    // Look for number with optional decimal point followed by optional 'V'
    const char* p = raw;
    while (*p != '\0' && !std::isdigit(static_cast<unsigned char>(*p))) {
        p++;
    }
    if (*p == '\0') return ParseStatus::INVALID_FORMAT;

    char* endPtr = nullptr;
    float val = std::strtof(p, &endPtr);
    if (endPtr != p && val >= 0.0f && val <= 30.0f) {
        outVoltage = val;
        return ParseStatus::OK;
    }

    return ParseStatus::INVALID_FORMAT;
}

static ParseStatus parseSingleDtcBlock(const uint8_t* bytes, size_t count, uint16_t* outCodes, size_t maxCodes, size_t& outCount, bool hasCountPrefix) {
    outCount = 0;
    if (count == 0) return ParseStatus::OK;

    // Search for 0x43 (Mode 03 response) or 0x47 (Mode 07 response)
    size_t startIdx = 0;
    bool foundHeader = false;
    for (size_t i = 0; i < count; ++i) {
        if (bytes[i] == 0x43 || bytes[i] == 0x47) {
            startIdx = i + 1;
            foundHeader = true;
            break;
        }
    }

    if (!foundHeader) {
        if (bytes[0] == 0x00) return ParseStatus::OK;
        return ParseStatus::INVALID_FORMAT;
    }

    size_t rem = count - startIdx;
    if (rem == 0) return ParseStatus::OK;

    bool isCountPrefixed = false;
    uint8_t numReported = 0;
    if (hasCountPrefix) {
        // ISO 15765-4 CAN: first byte after 0x43/0x47 is DTC count
        numReported = bytes[startIdx];
        if (numReported == 0) {
            return ParseStatus::OK;
        }
        if (numReported <= 10 && rem >= (1 + static_cast<size_t>(numReported) * 2)) {
            bool allNonZero = true;
            for (size_t k = 0; k < numReported; ++k) {
                uint16_t c = (static_cast<uint16_t>(bytes[startIdx + 1 + 2 * k]) << 8) |
                              static_cast<uint16_t>(bytes[startIdx + 2 + 2 * k]);
                if (c == 0) {
                    allNonZero = false;
                    break;
                }
            }
            if (allNonZero) {
                isCountPrefixed = true;
            }
        }
    }

    size_t found = 0;
    if (isCountPrefixed) {
        for (size_t k = 0; k < numReported && found < maxCodes; ++k) {
            uint16_t code = (static_cast<uint16_t>(bytes[startIdx + 1 + 2 * k]) << 8) |
                             static_cast<uint16_t>(bytes[startIdx + 2 + 2 * k]);
            if (code != 0) {
                outCodes[found++] = code;
            }
        }
    } else {
        // Legacy K-Line / J1850 (or unprefixed response): 2-byte pairs directly from startIdx
        for (size_t k = 0; k + 1 < rem && found < maxCodes; k += 2) {
            uint16_t code = (static_cast<uint16_t>(bytes[startIdx + k]) << 8) |
                             static_cast<uint16_t>(bytes[startIdx + k + 1]);
            if (code != 0) {
                outCodes[found++] = code;
            }
        }
    }

    outCount = found;
    return ParseStatus::OK;
}

ParseStatus ObdParser::parseDtcCodes(const char* raw, uint16_t* outCodes, size_t maxCodes, size_t& outCount, bool hasCountPrefix) {
    outCount = 0;
    if (!raw || !outCodes || maxCodes == 0) return ParseStatus::INVALID_FORMAT;
    if (isNoDataResponse(raw)) return ParseStatus::NO_DATA;
    if (isErrorResponse(raw)) return ParseStatus::ERROR_RESPONSE;

    // Split multi-ECU broadcast responses (e.g. 7E8 + 7E9 on ATSH7DF) into separate blocks
    uint8_t blockBytes[64];
    size_t blockCount = 0;
    bool anyValidBlock = false;

    const char* lineStart = raw;
    bool isFirstLine = true;
    while (*lineStart != '\0') {
        const char* lineEnd = lineStart;
        while (*lineEnd != '\0' && *lineEnd != '\r' && *lineEnd != '\n') {
            lineEnd++;
        }

        const char* s = lineStart;
        while (s < lineEnd && (*s == ' ' || *s == '\t')) s++;
        const char* e = lineEnd;
        while (e > s && (*(e - 1) == ' ' || *(e - 1) == '\t')) e--;
        size_t len = static_cast<size_t>(e - s);

        if (len > 0 && *s != '>') {
            bool wasFirstLine = isFirstLine;
            isFirstLine = false;
            bool isIndexedCf = (len >= 2 && isHexChar(s[0]) && s[1] == ':' && s[0] != '0');
            bool isCanCf = (len >= 6 && s[0] == '7' && (s[1] == 'E' || s[1] == 'e') && isHexChar(s[2]) &&
                            ((s[3] == ' ' && s[4] == '2' && isHexChar(s[5])) || (s[3] == '2' && isHexChar(s[4])))) ||
                           (len >= 12 && (s[8] == ' ' || s[8] == '\t') && s[9] == '2' && isHexChar(s[10]));
            uint8_t lineBytes[32];
            size_t lineByteCount = extractLineHexBytes(s, len, wasFirstLine, lineBytes, sizeof(lineBytes));

            if (lineByteCount > 0) {
                bool isNewEcuBlock = (blockCount > 0) && !isIndexedCf && !isCanCf &&
                                     (lineBytes[0] == 0x43 || lineBytes[0] == 0x47);
                if (isNewEcuBlock) {
                    if (outCount < maxCodes) {
                        size_t added = 0;
                        if (parseSingleDtcBlock(blockBytes, blockCount, outCodes + outCount, maxCodes - outCount, added, hasCountPrefix) == ParseStatus::OK) {
                            anyValidBlock = true;
                            outCount += added;
                        }
                    }
                    blockCount = 0;
                }

                for (size_t i = 0; i < lineByteCount && blockCount < sizeof(blockBytes); ++i) {
                    blockBytes[blockCount++] = lineBytes[i];
                }
            }
        }

        lineStart = lineEnd;
        while (*lineStart == '\r' || *lineStart == '\n') {
            lineStart++;
        }
    }

    if (blockCount > 0) {
        if (outCount < maxCodes) {
            size_t added = 0;
            if (parseSingleDtcBlock(blockBytes, blockCount, outCodes + outCount, maxCodes - outCount, added, hasCountPrefix) == ParseStatus::OK) {
                anyValidBlock = true;
                outCount += added;
            }
        } else {
            anyValidBlock = true;
        }
    } else if (!anyValidBlock) {
        return ParseStatus::OK;
    }

    return anyValidBlock ? ParseStatus::OK : ParseStatus::INVALID_FORMAT;
}

ParseStatus ObdParser::parseDtcResponse(const char* raw, bool isPending, DtcCodeInfo* outList, size_t maxCodes, size_t& outCount, bool hasCountPrefix) {
    outCount = 0;
    if (!raw || !outList || maxCodes == 0) return ParseStatus::INVALID_FORMAT;

    uint16_t codes[16];
    size_t rawCount = 0;
    ParseStatus status = parseDtcCodes(raw, codes, 16, rawCount, hasCountPrefix);
    if (status != ParseStatus::OK) {
        return status;
    }

    for (size_t i = 0; i < rawCount && outCount < maxCodes; ++i) {
        uint16_t c = codes[i];
        DtcDatabase::formatCode(c, outList[outCount].code, sizeof(outList[outCount].code));
        outList[outCount].desc = DtcDatabase::describe(c);
        outList[outCount].severity = DtcDatabase::getSeverity(c);
        outList[outCount].isPending = isPending;
        outCount++;
    }

    return ParseStatus::OK;
}

ParseStatus ObdParser::parseMode01PidResponse(const char* raw, uint8_t expectedPid,
                                              uint8_t* outData, size_t maxData,
                                              size_t& outLen, bool& outIsNegative) {
    outLen = 0;
    outIsNegative = false;
    if (!raw || !outData || maxData == 0) return ParseStatus::INVALID_FORMAT;
    if (isNoDataResponse(raw)) return ParseStatus::NO_DATA;
    if (isErrorResponse(raw)) return ParseStatus::ERROR_RESPONSE;

    bool sawNegative = false;
    bool isFirstLine = true;
    const char* lineStart = raw;
    while (*lineStart != '\0') {
        const char* lineEnd = lineStart;
        while (*lineEnd != '\0' && *lineEnd != '\r' && *lineEnd != '\n') lineEnd++;

        const char* s = lineStart;
        while (s < lineEnd && (*s == ' ' || *s == '\t')) s++;
        const char* e = lineEnd;
        while (e > s && (*(e - 1) == ' ' || *(e - 1) == '\t')) e--;
        size_t len = static_cast<size_t>(e - s);

        if (len > 0 && *s != '>') {
            bool wasFirstLine = isFirstLine;
            isFirstLine = false;
            uint8_t lineBytes[32];
            size_t cnt = extractLineHexBytes(s, len, wasFirstLine, lineBytes, sizeof(lineBytes));
            for (size_t i = 0; i + 2 < cnt; ++i) {
                if (lineBytes[i] == 0x41 && lineBytes[i + 1] == expectedPid) {
                    size_t avail = cnt - (i + 2);
                    size_t n = (avail < maxData) ? avail : maxData;
                    std::memcpy(outData, lineBytes + i + 2, n);
                    outLen = n;
                    outIsNegative = false;
                    return ParseStatus::OK;
                }
            }
            if (cnt >= 3 && lineBytes[0] == 0x7F && lineBytes[1] == 0x01) {
                sawNegative = true;
            }
        }

        lineStart = lineEnd;
        while (*lineStart == '\r' || *lineStart == '\n') lineStart++;
    }

    if (sawNegative) {
        outIsNegative = true;
        return ParseStatus::ERROR_RESPONSE;
    }
    return ParseStatus::INVALID_FORMAT;
}

ParseStatus ObdParser::parseProtocolNumber(const char* raw, uint8_t& outProto) {
    if (!raw) return ParseStatus::INVALID_FORMAT;
    if (isErrorResponse(raw)) return ParseStatus::ERROR_RESPONSE;
    if (isNoDataResponse(raw)) return ParseStatus::NO_DATA;

    const char* lineStart = raw;
    while (*lineStart != '\0') {
        const char* lineEnd = lineStart;
        while (*lineEnd != '\0' && *lineEnd != '\r' && *lineEnd != '\n') lineEnd++;

        const char* s = lineStart;
        while (s < lineEnd && (*s == ' ' || *s == '\t')) s++;
        const char* e = lineEnd;
        while (e > s && (*(e - 1) == ' ' || *(e - 1) == '\t' || *(e - 1) == '>')) e--;
        size_t len = static_cast<size_t>(e - s);

        if (len > 0 && !(len >= 2 && (s[0] == 'A' || s[0] == 'a') && (s[1] == 'T' || s[1] == 't'))) {
            // Skip optional 'A' Auto prefix in ELM327 ATDPN (e.g. "A6", "A3", "AC")
            if (len >= 2 && (s[0] == 'A' || s[0] == 'a')) {
                s++;
                len--;
            }
            if (len == 1 && isHexChar(s[0])) {
                uint8_t val = hexToByte('0', s[0]);
                if (val <= 12) {
                    outProto = val;
                    return ParseStatus::OK;
                }
            } else if (len == 2 && isHexChar(s[0]) && isHexChar(s[1])) {
                uint8_t val = hexToByte(s[0], s[1]);
                if (val <= 12) {
                    outProto = val;
                    return ParseStatus::OK;
                }
            }
        }

        lineStart = lineEnd;
        while (*lineStart == '\r' || *lineStart == '\n') lineStart++;
    }
    return ParseStatus::INVALID_FORMAT;
}


