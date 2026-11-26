#include "obd/command_guard.h"
#include <cctype>

bool CommandGuard::isHexChar(char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'A' && c <= 'F') ||
           (c >= 'a' && c <= 'f');
}

GuardResult CommandGuard::validate(std::string_view rawCmd) {
    // 1. Check for ELM327 Soft Break / Prompt Refresh token (Rule R8 read-only safe)
    if (rawCmd == "\r" || rawCmd == "\r\r" || rawCmd == "\r\n") {
        return GuardResult::ALLOWED;
    }

    // 2. Normalize into fixed stack buffer (zero heap allocation)
    char buf[64];
    size_t len = 0;
    for (size_t i = 0; i < rawCmd.size(); ++i) {
        unsigned char uc = static_cast<unsigned char>(rawCmd[i]);
        if (!std::isspace(uc)) {
            if (len >= sizeof(buf) - 1) {
                _blockedCount++;
                return GuardResult::BLOCKED_MALFORMED;
            }
            buf[len++] = static_cast<char>(std::toupper(uc));
        }
    }
    if (len == 0) {
        return GuardResult::BLOCKED_MALFORMED;
    }
    buf[len] = '\0';
    std::string_view cmd(buf, len);

    // 2. Critical Blacklist Check (Rule R8: ECU Read-Only Policy)
    // Mode 04 / UDS 0x14: Clear DTC
    if (cmd == "04" || cmd.rfind("04", 0) == 0 || cmd == "14" || cmd.rfind("14", 0) == 0) {
        _blockedCount++;
        return GuardResult::BLOCKED_BLACKLIST;
    }

    // Mode 08: Actuator / Control
    if (cmd == "08" || cmd.rfind("08", 0) == 0) {
        _blockedCount++;
        return GuardResult::BLOCKED_BLACKLIST;
    }

    // Dangerous UDS Service IDs
    const char* blacklistPrefixes[] = {
        "10", // DiagnosticSessionControl
        "11", // ECUReset
        "27", // SecurityAccess
        "28", // CommunicationControl
        "2E", // WriteDataByIdentifier
        "2F", // InputOutputControlByIdentifier
        "31", // RoutineControl
        "34", // RequestDownload
        "35", // RequestUpload
        "36", // TransferData
        "37", // RequestTransferExit
        "3D", // WriteMemoryByAddress
        "85", // ControlDTCSetting
        "ATMA", // Raw CAN monitor
        "ATBD", // Raw buffer dump
        "STPX"  // Raw OBDLink transmit
    };

    for (const auto* prefix : blacklistPrefixes) {
        if (cmd.rfind(prefix, 0) == 0) {
            _blockedCount++;
            return GuardResult::BLOCKED_BLACKLIST;
        }
    }

    // 3. Whitelist Verification
    // 3.1 Allowed AT Commands
    const char* allowedAtExact[] = {
        "ATZ", "ATWS",
        "ATE0", "ATE1",
        "ATL0", "ATL1",
        "ATS0", "ATS1",
        "ATH0", "ATH1",
        "ATDPN", "ATDP",
        "ATRV", "ATI", "AT@1",
        "ATCAF0", "ATCAF1"
    };
    for (const auto* allowed : allowedAtExact) {
        if (cmd == allowed) {
            return GuardResult::ALLOWED;
        }
    }

    // ATSP protocol lock (e.g. ATSP0, ATSP6)
    if (cmd.rfind("ATSP", 0) == 0 && cmd.size() == 5) {
        char p = cmd[4];
        if ((p >= '0' && p <= '9') || (p >= 'A' && p <= 'C')) {
            return GuardResult::ALLOWED;
        }
    }

    // ATAT adaptive timing (e.g. ATAT0, ATAT1, ATAT2)
    if (cmd.rfind("ATAT", 0) == 0 && cmd.size() == 5 && (cmd[4] >= '0' && cmd[4] <= '2')) {
        return GuardResult::ALLOWED;
    }

    // ATST timeout (e.g. ATST32, ATSTFF)
    if (cmd.rfind("ATST", 0) == 0 && cmd.size() == 6 && isHexChar(cmd[4]) && isHexChar(cmd[5])) {
        return GuardResult::ALLOWED;
    }

    // ATCRA CAN RX filter (e.g. ATCRA7E8)
    if (cmd.rfind("ATCRA", 0) == 0 && (cmd.size() == 8 || cmd.size() == 13)) {
        bool allHex = true;
        for (size_t i = 5; i < cmd.size(); ++i) {
            if (!isHexChar(cmd[i])) { allHex = false; break; }
        }
        if (allHex) return GuardResult::ALLOWED;
    }

    // ATSH CAN Header (e.g. ATSH7DF, ATSH18DB33F1 for Mode 03/07 DTC broadcast)
    if (cmd.rfind("ATSH", 0) == 0 && (cmd.size() == 7 || cmd.size() == 12)) {
        bool valid = true;
        for (size_t i = 4; i < cmd.size(); ++i) {
            if (!isHexChar(cmd[i])) { valid = false; break; }
        }
        if (valid) return GuardResult::ALLOWED;
    }

    // 3.2 OBD Diagnostic Modes Whitelist
    // Mode 01: Standard Live Data (e.g. 010C, 010C1, 010C0D05)
    if (cmd.rfind("01", 0) == 0 && cmd.size() >= 4) {
        bool valid = true;
        for (size_t i = 2; i < cmd.size(); ++i) {
            if (!isHexChar(cmd[i])) {
                valid = false;
                break;
            }
        }
        if (valid) return GuardResult::ALLOWED;
    }

    // Mode 03 / 07: Read DTCs (Read Only)
    if (cmd == "03" || cmd == "031" || cmd == "07" || cmd == "071") {
        return GuardResult::ALLOWED;
    }

    // Mode 09: Vehicle Info / VIN
    if (cmd.rfind("09", 0) == 0 && (cmd.size() == 4 || cmd.size() == 5)) {
        bool valid = true;
        for (size_t i = 2; i < cmd.size(); ++i) {
            if (!isHexChar(cmd[i])) { valid = false; break; }
        }
        if (valid) return GuardResult::ALLOWED;
    }

    _blockedCount++;
    return GuardResult::BLOCKED_NOT_WHITELISTED;
}
