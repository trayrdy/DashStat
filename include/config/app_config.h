#pragma once
#include <stdint.h>

// DashStat System & FreeRTOS Configuration
#define DASHSTAT_VERSION "1.0.0"
#define DASHSTAT_VERSION_FULL "DashStat Stable 1.0.0"
#define DASHSTAT_REPO "Trayrdy/DashStat"

// FreeRTOS Tasks
#define CORE0_OBD_TASK_STACK 8192
#define CORE0_OBD_TASK_PRIORITY 2
#define CORE0_OBD_TASK_CORE 0

#define CORE1_UI_TASK_STACK 8192
#define CORE1_UI_TASK_PRIORITY 3
#define CORE1_UI_TASK_CORE 1

// Display & LVGL 9
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define LVGL_BUFFER_LINES 16 // 320 x 16 partial buffer x 2 for DMA (20.48 KB total instead of 25.6 KB)
#define TARGET_UI_FPS 30
#define UI_TICK_PERIOD_MS 33 // ~30 Hz
#define LVGL_REFR_PROBE 0    // 1 = Log refresh time if > 3 ms (Probe for Page Switch Baseline)

// OBD Scheduler & Verbose Logging (0 = lean zero-UART-block polling; 1 = serial dump per TX/RX)
#define OBD_LOOP_INTERVAL_MS 33 // ~30 Hz polling rate
#define OBD_VERBOSE_LOG 0
