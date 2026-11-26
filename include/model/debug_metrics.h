#pragma once
#include <stdint.h>

struct DebugMetrics {
    float currentFps = 0.0f;
    uint32_t freeHeapBytes = 0;
};
