#ifndef UNIT_TEST
#include <Arduino.h>
#include <cstring>
#include "config/hardware_pins.h"
#include "config/app_config.h"
#include "hal/cyd_display.h"
#include "hal/lvgl_hal.h"
#include "hal/buzzer_controller.h"
#include "model/vehicle_state.h"
#include "obd/mock_obd.h"
#include "ui/ui_manager.h"
#include "transport/ble_transport.h"
#include "model/settings_manager.h"
#include "model/trip_computer.h"
#include "obd/elm327_engine.h"
#include "obd/universal_pid_catalog.h"
#include "model/telemetry_sync.h"
#include "obd/obd_parser.h"

static LGFX_CYD lcd;
static MockOBD& mockObd = MockOBD::instance();
TaskHandle_t g_obdTaskHandle = nullptr;

static void logMemory(const char* step) {
    Serial.printf("[MemoryAudit] %-25s | Free: %6lu | MinFree: %6lu | MaxAlloc: %6lu\n",
                  step,
                  (unsigned long)ESP.getFreeHeap(),
                  (unsigned long)ESP.getMinFreeHeap(),
                  (unsigned long)ESP.getMaxAllocHeap());
}


// Core 0 Task: OBD Data Acquisition via Elm327Engine & CommandGuard (30 Hz)
void obdTaskLoop(void* pvParameters) {
    Serial.printf("[Core 0] OBD Task running on Core %d (Priority %d)\n", xPortGetCoreID(), uxTaskPriorityGet(NULL));

    BleTransport::instance().setNotifyTask(xTaskGetCurrentTaskHandle());
    static Elm327Engine elmEngine(BleTransport::instance(), VehicleStateManager::instance());
    elmEngine.begin();

    VehicleState localState{};
    localState.isConnected = false;
    localState.isEngineRunning = false;
    localState.isDemoMode = false;

    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t frequency = pdMS_TO_TICKS(OBD_LOOP_INTERVAL_MS);

    uint32_t lastBleAttemptTime = 0;
    uint8_t bleFailCount = 0;
    AppSettings cachedSettings = SettingsManager::instance().get();
    uint32_t lastSettingsVer = SettingsManager::instance().version();

    while (true) {
        uint32_t now = millis();
        BleTransport::instance().processEvents();

        uint32_t curVer = SettingsManager::instance().version();
        if (curVer != lastSettingsVer) {
            cachedSettings = SettingsManager::instance().get();
            lastSettingsVer = curVer;
        }
        const AppSettings& settings = cachedSettings;
        bool hasSavedMac = (settings.savedBtMac[0] != '\0');

        // 1. Auto-Reconnect State Machine with backoff (4s -> 8s -> 15s) on Core 0
        if (settings.operationMode == OperationMode::LAUNCH && hasSavedMac) {
            if (!BleTransport::instance().isConnected() && !BleTransport::instance().isScanning()) {
                uint32_t retryMs = (bleFailCount == 0) ? 4000U : ((bleFailCount == 1) ? 8000U : 15000U);
                if (now - lastBleAttemptTime >= retryMs) {
                    lastBleAttemptTime = now;
                    Serial.printf("[OBD Task] Connecting to OBD BLE: %s\n", settings.savedBtMac);
                    if (BleTransport::instance().connect(settings.savedBtMac)) {
                        bleFailCount = 0;
                    } else if (bleFailCount < 2) {
                        bleFailCount++;
                    }
                }
            } else if (BleTransport::instance().isConnected()) {
                bleFailCount = 0;
            }
        }

        // 2. OBD Engine / Simulator Dispatch
        elmEngine.getScheduler().setActivePage(TelemetrySync::activePageId.load());
        elmEngine.getScheduler().setBoostedSlot(TelemetrySync::boostedSlot.load());
        elmEngine.getScheduler().setLiveSlots(settings.liveSlots);
        elmEngine.getScheduler().setDashSlots(settings.dashSlots);

        static OperationMode lastOpMode = static_cast<OperationMode>(0xFF);
        if (settings.operationMode != lastOpMode) {
            lastOpMode = settings.operationMode;
            if (settings.operationMode == OperationMode::DEMO) {
                mockObd.setParkedMode(false);
                mockObd.reset();
                BleTransport::instance().disconnect();
            } else if (settings.operationMode == OperationMode::LAUNCH) {
                // Entering LAUNCH mode: immediately zero out state buffers and reset engines
                localState = VehicleState{};
                localState.timestampMs = now;
                localState.isConnected = false;
                localState.isDemoMode = false;
                localState.isEngineRunning = false;
                for (uint8_t i = 0; i < 6; ++i) localState.liveSlotValues[i] = 0.0f;
                for (uint8_t i = 0; i < 4; ++i) localState.dashSlotValues[i] = 0.0f;
                VehicleStateManager::instance().update(localState);
                mockObd.reset();
                elmEngine.reset();
            }
        }

        if (settings.operationMode == OperationMode::LAUNCH) {
            // Update Elm327Engine (handles disconnect cleanup and discovery timeouts internally)
            elmEngine.update(now);

            if (!BleTransport::instance().isConnected()) {
                // Launch mode (Disconnected / Standby): All dials 0, waiting for OBD adapter
                localState.timestampMs = now;
                localState.isConnected = false;
                localState.isDemoMode = false;
                localState.isEngineRunning = false;
                localState.rpm = 0;
                localState.speedKmh = 0;
                localState.gear = ' ';
                localState.throttlePercent = 0;
                localState.engineLoad = 0;
                localState.batteryVoltage = 0.0f;
                localState.handbrakeActive = true;
                localState.milActive = false;
                localState.dtcCount = 0;
                for (uint8_t i = 0; i < 6; ++i) {
                    localState.liveSlotValues[i] = 0.0f;
                }
                for (uint8_t i = 0; i < 4; ++i) {
                    localState.dashSlotValues[i] = 0.0f;
                }
                VehicleStateManager::instance().update(localState);
            }
        } else {
            // DEMO MODE: Rich 32s test sequence (Park -> Rev -> Launch -> Redline 3s -> Decel -> MIL On)
            mockObd.update(OBD_LOOP_INTERVAL_MS);
            localState.timestampMs = now;
            localState.isConnected = true;
            localState.isDemoMode = true;

            const auto& telem = mockObd.getTelemetry();
            localState.rpm = telem.rpm;
            localState.speedKmh = telem.speed;
            localState.isEngineRunning = (telem.rpm > 300);
            localState.gear = telem.gear;
            localState.throttlePercent = telem.throttle;
            localState.engineLoad = telem.engineLoad;
            localState.coolantTempC = static_cast<int8_t>(telem.coolantTemp);
            localState.intakeAirTempC = static_cast<int8_t>(telem.intakeTemp);
            localState.batteryVoltage = telem.voltage;
            localState.handbrakeActive = telem.handbrake;
            localState.milActive = telem.milActive;
            localState.dtcCount = telem.dtcCount;

            auto resolveDemoPid = [&](uint8_t catIdx) -> float {
                const auto& def = UniversalPidCatalog::get(catIdx);
                if (def.pidHex == 0x0F) return static_cast<float>(localState.intakeAirTempC);
                if (def.pidHex == 0x05) return static_cast<float>(localState.coolantTempC);
                if (def.pidHex == 0x04) return static_cast<float>(localState.engineLoad);
                if (def.pidHex == 0x43) return static_cast<float>(localState.engineLoad) * 1.2f;
                if (def.pidHex == 0x11 || def.pidHex == 0x45 || def.pidHex == 0x49 ||
                    def.pidHex == 0x4A || def.pidHex == 0x4C) {
                    return static_cast<float>(localState.throttlePercent);
                }
                if (def.pidHex == 0x0C) return static_cast<float>(localState.rpm);
                if (def.pidHex == 0x0D) return static_cast<float>(localState.speedKmh);
                if (def.pidHex == 0xFF || def.pidHex == 0x42) return localState.batteryVoltage;
                if (def.pidHex == 0x0E) return (telem.throttle > 20) ? 24.0f : 12.0f;
                if (def.pidHex == 0xF0) {
                    return (telem.throttle > 20) ? ((telem.throttle - 20) * 0.25f) : -6.0f;
                }
                if (def.pidHex == 0x0B) {
                    return (telem.throttle > 20) ? (101.3f + (telem.throttle - 20) * 1.7f) : 60.0f;
                }
                if (def.pidHex == 0x10) return (localState.rpm * 0.004f);
                if (def.pidHex == 0x33) return 101.3f;
                if (def.pidHex == 0x03) return 2.0f;
                if (def.pidHex == 0x06 || def.pidHex == 0x07) return 1.5f;
                if (def.pidHex == 0x14 || def.pidHex == 0x15) return 0.65f;
                if (def.pidHex == 0x1F) return static_cast<float>(now / 1000U);
                return (def.minVal + def.maxVal) * 0.5f;
            };

            for (uint8_t i = 0; i < 4; ++i) {
                localState.dashSlotValues[i] = (settings.dashSlots[i] == 0xFF)
                                               ? 0.0f : resolveDemoPid(settings.dashSlots[i]);
            }

            uint8_t curPage = TelemetrySync::activePageId.load();
            if (curPage == 1) { // Only update 6 live slots when on Live Stat page
                const auto& slots = settings.liveSlots;
                for (uint8_t i = 0; i < 6; ++i) {
                    localState.liveSlotValues[i] = (slots[i] == 0xFF)
                                                   ? 0.0f : resolveDemoPid(slots[i]);
                }
            }

            VehicleStateManager::instance().update(localState);
        }

        if (settings.operationMode == OperationMode::LAUNCH && BleTransport::instance().isConnected()) {
            // Wake immediately (<0.1ms) on BLE '>' prompt arrival; cap wait at 20ms for timeouts
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20));
            lastWakeTime = xTaskGetTickCount();
        } else {
            vTaskDelayUntil(&lastWakeTime, frequency);
        }
    }
}

// Core 1 Task: UI Refresh & LVGL 9 Task Loop (Capped at exactly 30 FPS for test)
void uiTaskLoop(void* pvParameters) {
    Serial.printf("[Core 1] UI Task running on Core %d (Priority %d)\n", xPortGetCoreID(), uxTaskPriorityGet(NULL));

    TickType_t lastWakeTime = xTaskGetTickCount();
    uint8_t cadence = 0; // 3-frame cadence (33ms, 33ms, 34ms = 100ms for 3 frames => exactly 30 FPS)

    while (true) {
        // 1. Fetch latest snapshot (0ms wait, non-blocking)
        VehicleState snapshot = VehicleStateManager::instance().getSnapshot();

        // 2. Update UI screen labels & metrics
        UiManager::update(snapshot);

        // 3. Drive LVGL 9 timer & DMA render
        LvglHal::handle();

        static uint32_t lastProfileTime = 0;
        if (millis() - lastProfileTime >= 1000) {
            lastProfileTime = millis();
            const auto& s = SettingsManager::instance().get();
            Serial.printf("[Profile] Screen %u | OpMode: %s | RPM: %u | Spd: %u | Clt: %d | MIL: %d | FPS: %.1f | Heap: %lu\n",
                          UiManager::getCurrentScreen(),
                          (s.operationMode == OperationMode::DEMO) ? "DEMO" : "LAUNCH",
                          snapshot.rpm, snapshot.speedKmh, snapshot.coolantTempC, snapshot.milActive,
                          UiManager::getMetrics().currentFps,
                          (unsigned long)ESP.getFreeHeap());
            LvglHal::resetProfile();
        }

        // 4. Strict 30 FPS FreeRTOS cadence: 33 + 33 + 34 = 100ms (10 x 100ms = 1000ms = 30 FPS)
        uint32_t delayMs = (cadence == 2) ? 34 : 33;
        cadence = (cadence + 1) % 3;
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(delayMs));
    }
}

void setup() {
    Serial.begin(115200);
    delay(400);

    Serial.println("\n========================================================");
    Serial.println("  DashStat — Main Phase M3 Memory Audit                ");
    Serial.println("========================================================");
    esp_reset_reason_t rstReason = esp_reset_reason();
    Serial.printf("[ResetReason] code=%d\n", (int)rstReason);
    logMemory("0. Clean Boot");
    Serial.printf("[DashStat] Chip: %s (Rev %d, %d cores)\n", ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores());

    // 0. Load persisted settings & trip records before peripheral & display init
    SettingsManager::instance().begin();
    TripComputer::instance().begin();

    // 1. Init Buzzer (SD driver excluded to save SRAM)
    BuzzerController::instance().init(CYD_BUZZER_PIN);
    BuzzerController::instance().playTapTick();
    logMemory("1. After Buzzer");

    // 2. Init Display & LVGL 9 Driver
    if (!LvglHal::init(lcd)) {
        Serial.println("[Error] LvglHal::init failed!");
    }
    logMemory("2. After LvglHal");

    // 3. Init UI Screens & Debug Overlay
    if (!UiManager::init()) {
        Serial.println("[Error] UiManager::init failed!");
    }
    logMemory("3. After UiManager");

    // 4. Init BLE Transport
    BleTransport::instance().begin();
    logMemory("4. After NimBLE Init");

    // 5. Init Mock OBD (Full dynamic simulation enabled)
    mockObd.begin();
    mockObd.setParkedMode(false);
    logMemory("5. After MockOBD");

    // 6. Spawn Dual-Core FreeRTOS Tasks
    xTaskCreatePinnedToCore(
        obdTaskLoop,
        "OBDTask",
        CORE0_OBD_TASK_STACK,
        NULL,
        CORE0_OBD_TASK_PRIORITY,
        &g_obdTaskHandle,
        CORE0_OBD_TASK_CORE
    );

    xTaskCreatePinnedToCore(
        uiTaskLoop,
        "UITask",
        CORE1_UI_TASK_STACK,
        NULL,
        CORE1_UI_TASK_PRIORITY,
        NULL,
        CORE1_UI_TASK_CORE
    );
    logMemory("6. After FreeRTOS Tasks");

    Serial.println("[DashStat] Architecture Skeleton initialized successfully!");
}

void loop() {
    // Both Core 0 & Core 1 tasks run independently in FreeRTOS
    vTaskDelete(NULL);
}
#endif
