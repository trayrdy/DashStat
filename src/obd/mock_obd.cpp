#include "obd/mock_obd.h"
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cmath>
#include <algorithm>

MockOBD& MockOBD::instance() {
    static MockOBD inst;
    return inst;
}

void MockOBD::update(uint32_t deltaMs) {
    _simTimeMs += deltaMs;

    if (_parkedMode) {
        _telemetry.rpm = 740 + static_cast<uint16_t>((_simTimeMs / 100) % 25);
        _telemetry.speed = 0;
        _telemetry.gear = 'P';
        _telemetry.throttle = 0;
        _telemetry.engineLoad = 18;
        _telemetry.coolantTemp = 88;
        _telemetry.intakeTemp = 35;
        _telemetry.voltage = 14.1f;
        _telemetry.milActive = false;
        _telemetry.dtcCount = 0;
        _telemetry.handbrake = true;
        return;
    }

    uint32_t cycle = _simTimeMs % 32000;

    int32_t voltStep = static_cast<int32_t>((_simTimeMs / 500) % 7) - 3;
    _telemetry.voltage = 14.1f + voltStep * 0.05f;

    if (cycle < 3000) {
        _telemetry.rpm = 740 + (cycle % 25);
        _telemetry.speed = 0;
        _telemetry.throttle = 0;
        _telemetry.engineLoad = 18;
        _telemetry.gear = 'P';
        _telemetry.handbrake = true;
        _telemetry.milActive = false;
        _telemetry.dtcCount = 0;
        _telemetry.coolantTemp = 88;
    } else if (cycle < 7000) {
        _telemetry.speed = 0;
        _telemetry.gear = 'P';
        _telemetry.handbrake = true;
        _telemetry.milActive = false;
        _telemetry.dtcCount = 0;
        if (cycle < 5000) {
            float t = (cycle - 3000) / 2000.0f;
            _telemetry.throttle = static_cast<uint8_t>(std::sin(t * 3.14159f) * 60.0f);
            _telemetry.rpm = static_cast<uint16_t>(750 + std::sin(t * 3.14159f) * 3500.0f);
        } else {
            float t = (cycle - 5000) / 2000.0f;
            _telemetry.throttle = static_cast<uint8_t>(std::sin(t * 3.14159f) * 85.0f);
            _telemetry.rpm = static_cast<uint16_t>(750 + std::sin(t * 3.14159f) * 4800.0f);
        }
        _telemetry.engineLoad = 25 + _telemetry.throttle / 2;
        _telemetry.coolantTemp = 93;
    } else if (cycle < 14000) {
        float t = (cycle - 7000) / 7000.0f;
        _telemetry.gear = (cycle < 10500) ? '1' : '2';
        _telemetry.handbrake = false;
        _telemetry.throttle = 100;
        _telemetry.engineLoad = 96;
        _telemetry.speed = static_cast<uint8_t>(t * 115.0f);
        _telemetry.rpm = static_cast<uint16_t>(1200 + t * t * (6250 - 1200));
        _telemetry.milActive = false;
        _telemetry.dtcCount = 0;
        _telemetry.coolantTemp = static_cast<int16_t>(93.0f + t * 10.0f);
    } else if (cycle < 17000) {
        _telemetry.gear = '2';
        _telemetry.handbrake = false;
        _telemetry.throttle = 100;
        _telemetry.engineLoad = 98;
        _telemetry.speed = 115 + static_cast<uint8_t>((cycle % 3));
        _telemetry.rpm = 6250 + ((cycle / 80) % 2 == 0 ? 110 : -60);
        _telemetry.milActive = false;
        _telemetry.dtcCount = 0;
        _telemetry.coolantTemp = 110;
    } else if (cycle < 23000) {
        float t = (cycle - 17000) / 6000.0f;
        _telemetry.throttle = 0;
        _telemetry.engineLoad = 12;
        _telemetry.speed = static_cast<uint8_t>(115.0f * (1.0f - t));
        _telemetry.rpm = static_cast<uint16_t>(750 + (1.0f - t) * (2600 - 750));
        _telemetry.gear = (_telemetry.speed > 5) ? 'D' : 'P';
        _telemetry.handbrake = (_telemetry.speed == 0);
        _telemetry.milActive = false;
        _telemetry.dtcCount = 0;
        _telemetry.coolantTemp = static_cast<int16_t>(110.0f - t * 8.0f);
    } else {
        _telemetry.speed = 0;
        _telemetry.rpm = 740 + (cycle % 20);
        _telemetry.gear = 'P';
        _telemetry.handbrake = true;
        _telemetry.throttle = 0;
        _telemetry.engineLoad = 18;
        _telemetry.coolantTemp = 101;
        _telemetry.milActive = true;
        _telemetry.dtcCount = 1;
    }
}

bool MockOBD::query(const char* cmd, char* outBuffer, size_t maxLen) {
    if (!cmd || !outBuffer || maxLen == 0) return false;

    GuardResult result = CommandGuard::validate(cmd);
    if (result != GuardResult::ALLOWED) {
        std::snprintf(outBuffer, maxLen, "ERROR");
        return false;
    }

    char normalized[32] = {0};
    size_t nIdx = 0;
    for (size_t i = 0; cmd[i] != '\0' && nIdx < sizeof(normalized) - 1; ++i) {
        if (!std::isspace(static_cast<unsigned char>(cmd[i])) && cmd[i] != '\r' && cmd[i] != '\n') {
            normalized[nIdx++] = static_cast<char>(std::toupper(static_cast<unsigned char>(cmd[i])));
        }
    }

    if (std::strcmp(normalized, "ATZ") == 0 || std::strcmp(normalized, "ATWS") == 0) {
        std::snprintf(outBuffer, maxLen, "ELM327 v1.5");
        return true;
    }
    if (std::strncmp(normalized, "ATSP", 4) == 0 || std::strncmp(normalized, "ATAT", 4) == 0 ||
        std::strncmp(normalized, "ATST", 4) == 0 || std::strncmp(normalized, "ATE", 3) == 0 ||
        std::strncmp(normalized, "ATL", 3) == 0 || std::strncmp(normalized, "ATS", 3) == 0 ||
        std::strncmp(normalized, "ATH", 3) == 0 || std::strncmp(normalized, "ATSH", 4) == 0) {
        std::snprintf(outBuffer, maxLen, "OK");
        return true;
    }
    if (std::strcmp(normalized, "ATI") == 0) {
        std::snprintf(outBuffer, maxLen, "DashStat MockOBD v1.0");
        return true;
    }
    if (std::strcmp(normalized, "ATDPN") == 0) {
        std::snprintf(outBuffer, maxLen, "A6");
        return true;
    }
    if (std::strcmp(normalized, "ATRV") == 0) {
        std::snprintf(outBuffer, maxLen, "%.1fV", _telemetry.voltage);
        return true;
    }

    char pid[32] = {0};
    std::snprintf(pid, sizeof(pid), "%s", normalized);
    size_t pLen = std::strlen(pid);
    if ((pLen == 5 || pLen == 7) && pid[pLen - 1] >= '0' && pid[pLen - 1] <= '9') {
        pid[pLen - 1] = '\0';
    }

    if (std::strcmp(pid, "0101") == 0) {
        uint8_t a = _telemetry.milActive ? 0x81 : 0x00;
        std::snprintf(outBuffer, maxLen, "41 01 %02X 07 65 00", a);
        return true;
    }
    if (std::strcmp(normalized, "03") == 0) {
        if (_telemetry.milActive) {
            std::snprintf(outBuffer, maxLen, "43 01 01 00 00 00 00");
        } else {
            std::snprintf(outBuffer, maxLen, "43 00 00 00 00 00 00");
        }
        return true;
    }
    if (std::strcmp(normalized, "07") == 0) {
        std::snprintf(outBuffer, maxLen, "47 00 00 00 00 00 00");
        return true;
    }
    if (std::strcmp(pid, "010C") == 0) {
        uint16_t raw = _telemetry.rpm * 4;
        std::snprintf(outBuffer, maxLen, "41 0C %02X %02X", (raw >> 8) & 0xFF, raw & 0xFF);
        return true;
    }
    if (std::strcmp(pid, "010D") == 0) {
        std::snprintf(outBuffer, maxLen, "41 0D %02X", _telemetry.speed);
        return true;
    }
    if (std::strcmp(pid, "0105") == 0) {
        uint8_t a = static_cast<uint8_t>(_telemetry.coolantTemp + 40);
        std::snprintf(outBuffer, maxLen, "41 05 %02X", a);
        return true;
    }
    if (std::strcmp(pid, "0111") == 0) {
        uint8_t a = static_cast<uint8_t>((_telemetry.throttle * 255) / 100);
        std::snprintf(outBuffer, maxLen, "41 11 %02X", a);
        return true;
    }
    if (std::strcmp(pid, "010F") == 0) {
        uint8_t a = static_cast<uint8_t>(_telemetry.intakeTemp + 40);
        std::snprintf(outBuffer, maxLen, "41 0F %02X", a);
        return true;
    }
    if (std::strcmp(pid, "0104") == 0) {
        uint8_t a = static_cast<uint8_t>((_telemetry.engineLoad * 255) / 100);
        std::snprintf(outBuffer, maxLen, "41 04 %02X", a);
        return true;
    }

    std::snprintf(outBuffer, maxLen, "NO DATA");
    return true;
}
