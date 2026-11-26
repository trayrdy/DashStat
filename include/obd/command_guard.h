#pragma once

#include <cstdint>
#include <cstddef>
#include <string_view>

enum class GuardResult {
    ALLOWED,
    BLOCKED_BLACKLIST,
    BLOCKED_NOT_WHITELISTED,
    BLOCKED_MALFORMED
};

// Rule R8: Strictly ECU Read-Only Policy.
// Whitelisted OBD Modes: Mode 01 (Realtime), Mode 03/07 (Read DTCs), Mode 09 (Vehicle Info)
// Whitelisted AT Setup: ATZ, ATWS, ATE/L/S/H, ATSP, ATAT, ATST, ATCRA, ATSH, ATRV, ATI, AT@1, ATCAF
class CommandGuard {
public:
    static GuardResult validate(std::string_view command);
    static bool isAllowed(std::string_view command) {
        return validate(command) == GuardResult::ALLOWED;
    }

    static size_t getBlockedCount() { return _blockedCount; }
    static void resetBlockedCount() { _blockedCount = 0; }

private:
    static inline size_t _blockedCount = 0;
    static bool isHexChar(char c);
};
