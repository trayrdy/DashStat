#pragma once

#include <atomic>
#include <stdint.h>

struct TelemetrySync {
    static inline std::atomic<uint8_t> activePageId{0};
    static inline std::atomic<int8_t> boostedSlot{-1};
};
